// StarDraft's collaboration relay: forwards encrypted frames between the host of a shared map and the people who join
// it. It cannot read them (the keys are in the invite links' fragments, which browsers never send). It keeps nothing on
// disk and logs no addresses, room ids or contents. No packages: Node's own http and crypto only.
//
//   node tools/collab-relay/relay.mjs [--port 8787] [--host 127.0.0.1]
//
// A Cloudflare tunnel makes it public: cloudflared tunnel --url http://localhost:8787 (README.md).

import http from 'node:http';
import crypto from 'node:crypto';

const args = process.argv.slice(2);
const opt = (name, def) => {
  const i = args.indexOf('--' + name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
};
const PORT = Number(opt('port', process.env.PORT || 8787));
const HOST = opt('host', '127.0.0.1');

// --- Limits -------------------------------------------------------------------------------------------------------
const MAX_FRAME = 1536 * 1024;       // one message (a map's terrain at join is at most about 600 KB)
const MAX_TEXT = 4096;               // a control message (JSON)
const MAX_MEMBERS = 8;               // people in a room besides the host
const MAX_ROOMS = 200;
const MAX_ROOMS_PER_IP = 3;
const MAX_CONN_PER_IP = 16;
const JOINS_PER_MIN_PER_IP = 20;     // (guessing room ids or tokens)
const ROOM_LIFETIME_MS = 24 * 3600e3;
const IDLE_MS = 30 * 60e3;           // a room with no data for this long ends
const PING_MS = 25e3;                // (Cloudflare ends idle sockets at 100 s)
const HELLO_MS = 10e3;               // a connection must create or join within this time
const RATE = {                       // token buckets: bytes/s and messages/s, burst
  host: { bytes: 8 << 20, burstBytes: 16 << 20, msgs: 1500, burstMsgs: 3000 },  // (each change goes to each member)
  member: { bytes: 1 << 20, burstBytes: 2 << 20, msgs: 100, burstMsgs: 200 },
};

const rooms = new Map();  // id -> { host, members: Map<id, conn>, view, edit, created, lastData, ip }
const connsPerIp = new Map();
const joinTimes = new Map();  // ip -> [ms]

const log = (...a) => console.log(new Date().toISOString(), ...a);
const randomId = () => crypto.randomBytes(16).toString('base64url');
const sameToken = (a, b) => {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length !== 64 || b.length !== 64) return false;
  return crypto.timingSafeEqual(Buffer.from(a), Buffer.from(b));
};
const validToken = (t) => t === null || (typeof t === 'string' && /^[0-9a-f]{64}$/.test(t));

// The client's address: Cloudflare's header only from a local tunnel (else anyone could set it).
function clientIp(req) {
  const peer = req.socket.remoteAddress || '';
  const local = peer === '127.0.0.1' || peer === '::1' || peer === '::ffff:127.0.0.1';
  const cf = req.headers['cf-connecting-ip'];
  return local && typeof cf === 'string' && cf.length < 64 ? cf : peer;
}

// --- The landing page of an invite link ------------------------------------------------------------------------------
function landingPage(res) {
  const nonce = crypto.randomBytes(16).toString('base64');
  res.writeHead(200, {
    'Content-Type': 'text/html; charset=utf-8',
    'Cache-Control': 'no-store',
    'Referrer-Policy': 'no-referrer',
    'X-Content-Type-Options': 'nosniff',
    'X-Frame-Options': 'DENY',
    'Content-Security-Policy': `default-src 'none'; script-src 'nonce-${nonce}'; style-src 'nonce-${nonce}'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`,
  });
  res.end(`<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>StarDraft shared map</title><style nonce="${nonce}">
body{font:16px/1.5 system-ui,sans-serif;background:#16181d;color:#e6e6e6;margin:0;display:grid;place-items:center;min-height:100vh}
main{max-width:34em;padding:24px}a.b{display:inline-block;background:#3d7bfd;color:#fff;padding:.6em 1.2em;border-radius:6px;text-decoration:none}
code{background:#262a33;padding:.1em .3em;border-radius:4px;word-break:break-all}p.s{color:#9aa0aa;font-size:14px}</style></head>
<body><main><h1>Shared StarDraft map</h1><p id="what">You have an invite to a map that someone shares in StarDraft 2.</p>
<p><a class="b" id="open" href="#">Open in StarDraft 2</a></p>
<p class="s">Nothing happens? Start StarDraft 2, then select <b>File &gt; Shared map &gt; Join with an invite link</b> and paste this page's address.</p>
<p class="s">The map goes directly between the programs, encrypted. This server cannot read it.</p></main>
<script nonce="${nonce}">
const f = location.hash.slice(1);
if (/^[ev]\\.[A-Za-z0-9_-]{43}$/.test(f)) {
  document.getElementById('what').textContent = f[0] === 'e' ? 'You have an invite to edit a map that someone shares in StarDraft 2.' : 'You have an invite to view a map that someone shares in StarDraft 2.';
  const link = 'stardraft://join?link=' + encodeURIComponent(location.href);
  const a = document.getElementById('open');
  a.href = link;
  location.href = link;
} else {
  document.getElementById('what').textContent = 'This invite link is not complete. Ask for the link again.';
  document.getElementById('open').remove();
}
</script></body></html>`);
}

const server = http.createServer((req, res) => {
  if (req.method !== 'GET') return res.writeHead(405).end();
  const url = req.url || '/';
  if (/^\/j\/[A-Za-z0-9_-]{22}$/.test(url)) return landingPage(res);
  if (url === '/') {
    res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' });
    return res.end('StarDraft collaboration relay\n');
  }
  res.writeHead(404, { 'Content-Type': 'text/plain' }).end('Not found\n');
});

// --- WebSocket (RFC 6455), server side --------------------------------------------------------------------------------
server.on('upgrade', (req, socket) => {
  const fail = (code) => { socket.end(`HTTP/1.1 ${code}\r\nConnection: close\r\n\r\n`); };
  if (req.url !== '/relay') return fail('404 Not Found');
  const key = req.headers['sec-websocket-key'];
  if (req.headers['sec-websocket-version'] !== '13' || typeof key !== 'string' || !/^[A-Za-z0-9+/]{22}==$/.test(key)) return fail('400 Bad Request');
  const ip = clientIp(req);
  if ((connsPerIp.get(ip) || 0) >= MAX_CONN_PER_IP) return fail('429 Too Many Requests');
  connsPerIp.set(ip, (connsPerIp.get(ip) || 0) + 1);
  const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  socket.setNoDelay(true);
  new Conn(socket, ip);
});

class Conn {
  constructor(socket, ip) {
    this.socket = socket;
    this.ip = ip;
    this.buf = Buffer.alloc(0);
    this.parts = [];
    this.partsSize = 0;
    this.partsOp = 0;
    this.room = null;
    this.id = 0;  // 0: the host; else the member's number
    this.role = null;  // 'host' | 'member'
    this.closed = false;
    this.alive = true;
    this.bucket = null;
    this.helloTimer = setTimeout(() => { if (!this.role) this.close(1008, 'hello'); }, HELLO_MS);
    this.pingTimer = setInterval(() => {
      if (!this.alive) return this.close(1001, 'timeout');
      this.alive = false;
      this.send(0x9, Buffer.alloc(0));
    }, PING_MS);
    socket.on('data', (d) => this.data(d));
    socket.on('error', () => this.gone());
    socket.on('close', () => this.gone());
  }

  data(d) {
    this.alive = true;
    this.buf = this.buf.length ? Buffer.concat([this.buf, d]) : d;
    if (this.buf.length > MAX_FRAME + 64) return this.close(1009, 'too big');
    for (;;) {
      if (this.buf.length < 2) return;
      const b0 = this.buf[0], b1 = this.buf[1];
      const fin = (b0 & 0x80) !== 0, op = b0 & 0x0f, masked = (b1 & 0x80) !== 0;
      if (b0 & 0x70) return this.close(1002, 'rsv');
      if (!masked) return this.close(1002, 'unmasked');
      let len = b1 & 0x7f, at = 2;
      if (len === 126) { if (this.buf.length < 4) return; len = this.buf.readUInt16BE(2); at = 4; }
      else if (len === 127) {
        if (this.buf.length < 10) return;
        const big = this.buf.readBigUInt64BE(2);
        if (big > BigInt(MAX_FRAME)) return this.close(1009, 'too big');
        len = Number(big); at = 10;
      }
      if (len > MAX_FRAME) return this.close(1009, 'too big');
      if (this.buf.length < at + 4 + len) return;
      const mask = this.buf.subarray(at, at + 4);
      const payload = Buffer.from(this.buf.subarray(at + 4, at + 4 + len));
      for (let i = 0; i < payload.length; ++i) payload[i] ^= mask[i & 3];
      this.buf = this.buf.subarray(at + 4 + len);
      if (op >= 0x8) {  // control frames: never fragmented, at most 125 bytes
        if (!fin || len > 125) return this.close(1002, 'control');
        if (op === 0x8) return this.close(1000, '');
        if (op === 0x9) this.send(0xA, payload);
        continue;
      }
      if (op !== 0 && op !== 1 && op !== 2) return this.close(1002, 'opcode');
      if (op !== 0) { if (this.parts.length) return this.close(1002, 'fragment'); this.partsOp = op; }
      else if (!this.parts.length) return this.close(1002, 'continuation');
      this.parts.push(payload);
      this.partsSize += payload.length;
      if (this.partsSize > MAX_FRAME) return this.close(1009, 'too big');
      if (!fin) continue;
      const msg = this.parts.length === 1 ? this.parts[0] : Buffer.concat(this.parts);
      const kind = this.partsOp;
      this.parts = []; this.partsSize = 0;
      if (!this.spend(msg.length)) return this.close(1008, 'rate');
      if (kind === 1) this.text(msg); else this.binary(msg);
      if (this.closed) return;
    }
  }

  spend(bytes) {
    if (!this.bucket) return true;  // (before create or join: one small message)
    const r = this.bucket.rate, now = Date.now(), dt = (now - this.bucket.t) / 1000;
    this.bucket.t = now;
    this.bucket.bytes = Math.min(r.burstBytes, this.bucket.bytes + dt * r.bytes) - bytes;
    this.bucket.msgs = Math.min(r.burstMsgs, this.bucket.msgs + dt * r.msgs) - 1;
    return this.bucket.bytes >= 0 && this.bucket.msgs >= 0;
  }

  send(op, payload) {
    if (this.closed) return;
    const len = payload.length;
    const head = len < 126 ? Buffer.from([0x80 | op, len]) : len < 65536 ? Buffer.alloc(4) : Buffer.alloc(10);
    if (len >= 126 && len < 65536) { head[0] = 0x80 | op; head[1] = 126; head.writeUInt16BE(len, 2); }
    else if (len >= 65536) { head[0] = 0x80 | op; head[1] = 127; head.writeBigUInt64BE(BigInt(len), 2); }
    this.socket.write(Buffer.concat([head, payload]));
  }
  sendJson(o) { this.send(0x1, Buffer.from(JSON.stringify(o))); }

  close(code, why) {
    if (this.closed) return;
    const reason = Buffer.from(String(why).slice(0, 100));
    const p = Buffer.alloc(2 + reason.length);
    p.writeUInt16BE(code, 0);
    reason.copy(p, 2);
    this.send(0x8, p);
    this.closed = true;
    this.socket.end();
    this.gone();
  }

  gone() {
    if (this.dead) return;
    this.dead = this.closed = true;
    clearTimeout(this.helloTimer);
    clearInterval(this.pingTimer);
    const n = (connsPerIp.get(this.ip) || 1) - 1;
    if (n > 0) connsPerIp.set(this.ip, n); else connsPerIp.delete(this.ip);
    const room = this.room;
    if (!room) return;
    if (this.role === 'host') endRoom(room, 'host left');
    else if (room.members.get(this.id) === this) {
      room.members.delete(this.id);
      room.host.sendJson({ t: 'left', id: this.id });
    }
    this.socket.destroy();
  }

  text(msg) {
    if (msg.length > MAX_TEXT) return this.close(1009, 'too big');
    let m;
    try { m = JSON.parse(msg.toString('utf8')); } catch { return this.close(1003, 'json'); }
    if (!m || typeof m !== 'object' || typeof m.t !== 'string') return this.close(1003, 'json');
    if (!this.role) {
      if (m.t === 'create') return this.create(m);
      if (m.t === 'join') return this.join(m);
      return this.close(1008, 'hello');
    }
    if (this.role !== 'host') return this.close(1008, 'not host');
    const room = this.room;
    if (m.t === 'tokens') {
      if (!validToken(m.view) || !validToken(m.edit)) return this.close(1003, 'token');
      room.view = m.view; room.edit = m.edit;
      return this.sendJson({ t: 'tokens-ok' });
    }
    if (m.t === 'kick' && Number.isInteger(m.id)) {
      const c = room.members.get(m.id);
      if (c) { room.members.delete(m.id); c.close(1000, 'removed'); }
      return;
    }
    if (m.t === 'close') return endRoom(room, 'closed');
    this.close(1003, 'unknown');
  }

  create(m) {
    if (m.v !== 1) return this.close(1008, 'version');
    if (!validToken(m.view) || !validToken(m.edit) || (!m.view && !m.edit)) return this.close(1003, 'token');
    if (rooms.size >= MAX_ROOMS) { this.sendJson({ t: 'error', code: 'busy' }); return this.close(1013, 'busy'); }
    let mine = 0;
    for (const r of rooms.values()) if (r.ip === this.ip) ++mine;
    if (mine >= MAX_ROOMS_PER_IP) { this.sendJson({ t: 'error', code: 'too-many-rooms' }); return this.close(1008, 'rooms'); }
    clearTimeout(this.helloTimer);
    let id;
    do id = randomId(); while (rooms.has(id));
    const room = { id, host: this, members: new Map(), next: 1, view: m.view, edit: m.edit, created: Date.now(), lastData: Date.now(), ip: this.ip };
    rooms.set(id, room);
    this.room = room; this.role = 'host'; this.id = 0;
    this.bucket = { rate: RATE.host, t: Date.now(), bytes: RATE.host.burstBytes, msgs: RATE.host.burstMsgs };
    this.sendJson({ t: 'created', room: id, expires: room.created + ROOM_LIFETIME_MS });
    log('room created; rooms:', rooms.size);
  }

  join(m) {
    const now = Date.now();
    const times = (joinTimes.get(this.ip) || []).filter((t) => now - t < 60e3);
    times.push(now);
    joinTimes.set(this.ip, times);
    const notFound = () => { this.sendJson({ t: 'error', code: 'not-found' }); this.close(1008, 'not found'); };
    if (times.length > JOINS_PER_MIN_PER_IP) { this.sendJson({ t: 'error', code: 'slow-down' }); return this.close(1008, 'rate'); }
    if (m.v !== 1 || typeof m.room !== 'string' || !/^[A-Za-z0-9_-]{22}$/.test(m.room)) return notFound();
    const room = rooms.get(m.room);
    // (one answer for a room that is not there and a wrong token: no way to tell rooms apart)
    if (!room || !(sameToken(m.token, room.view) || sameToken(m.token, room.edit))) return notFound();
    if (room.members.size >= MAX_MEMBERS) { this.sendJson({ t: 'error', code: 'full' }); return this.close(1008, 'full'); }
    clearTimeout(this.helloTimer);
    this.room = room; this.role = 'member'; this.id = room.next++;
    this.bucket = { rate: RATE.member, t: Date.now(), bytes: RATE.member.burstBytes, msgs: RATE.member.burstMsgs };
    room.members.set(this.id, this);
    this.sendJson({ t: 'joined', id: this.id });
    room.host.sendJson({ t: 'peer', id: this.id });
  }

  binary(msg) {
    const room = this.room;
    if (!room) return this.close(1008, 'hello');
    room.lastData = Date.now();
    if (this.role === 'host') {
      if (msg.length < 4) return this.close(1003, 'short');
      const to = room.members.get(msg.readUInt32BE(0));
      if (to) to.send(0x2, msg.subarray(4));
      return;
    }
    const head = Buffer.alloc(4);
    head.writeUInt32BE(this.id, 0);
    room.host.send(0x2, Buffer.concat([head, msg]));
  }
}

function endRoom(room, why) {
  if (!rooms.has(room.id)) return;
  rooms.delete(room.id);
  for (const c of room.members.values()) { c.room = null; c.close(1000, why); }
  room.members.clear();
  room.host.room = null;
  room.host.close(1000, why);
  log('room ended; rooms:', rooms.size);
}

setInterval(() => {
  const now = Date.now();
  for (const room of [...rooms.values()])
    if (now - room.created > ROOM_LIFETIME_MS || now - room.lastData > IDLE_MS) endRoom(room, 'expired');
  for (const [ip, t] of joinTimes) if (!t.length || now - t[t.length - 1] > 60e3) joinTimes.delete(ip);
}, 30e3).unref();

server.listen(PORT, HOST, () => log(`relay on http://${HOST}:${PORT}`));
