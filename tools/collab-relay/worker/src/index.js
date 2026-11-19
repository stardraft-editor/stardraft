// StarDraft's collaboration relay on Cloudflare (collab.stardraft.app): the same protocol as ../relay.mjs, with a
// Durable Object for each room (WebSocket hibernation: an idle room costs nothing). It forwards encrypted frames between
// the host and each member and cannot read them; it keeps a room's two link tokens (hashes, not the links' secrets)
// while the room is open, and nothing after. It logs no addresses, room ids or contents.
//
//   GET /                the server's name
//   GET /j/<room>        an invite link's page: it opens stardraft://join?link=... (the program asks first)
//   GET /relay           WebSocket: a host makes a room ({"t":"create",...})
//   GET /relay/<room>    WebSocket: a member joins it ({"t":"join",...})

const MAX_FRAME = 1024 * 1024;  // (a 256 x 256 map's terrain at join is about 530 KB)
const MAX_TEXT = 4096;
const MAX_MEMBERS = 8;
const MAX_SOCKETS = 16;  // in a room, with the ones that did not join yet
const ROOM_LIFETIME_MS = 24 * 3600e3;
const IDLE_MS = 30 * 60e3;
const HELLO_MS = 10e3;
const RATE = {
  host: { bytes: 8 << 20, burstBytes: 16 << 20, msgs: 1500, burstMsgs: 3000 },
  member: { bytes: 1 << 20, burstBytes: 2 << 20, msgs: 100, burstMsgs: 200 },
  none: { bytes: 8192, burstBytes: 8192, msgs: 4, burstMsgs: 4 },
};
const ROOM_RE = /^[A-Za-z0-9_-]{22}$/;
const TOKEN_RE = /^[0-9a-f]{64}$/;

function randomId() {
  const b = new Uint8Array(16);
  crypto.getRandomValues(b);
  return btoa(String.fromCharCode(...b)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
function sameToken(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length !== 64 || b.length !== 64) return false;
  const x = new TextEncoder().encode(a), y = new TextEncoder().encode(b);
  return crypto.subtle.timingSafeEqual(x, y);
}
const validToken = (t) => t === null || (typeof t === 'string' && TOKEN_RE.test(t));

const SECURITY_HEADERS = {
  'Cache-Control': 'no-store',
  'Referrer-Policy': 'no-referrer',
  'X-Content-Type-Options': 'nosniff',
  'X-Frame-Options': 'DENY',
  'Strict-Transport-Security': 'max-age=31536000',
};

function landingPage() {
  const nonce = randomId();
  const html = `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>StarDraft collaborative editing</title><style nonce="${nonce}">
body{font:16px/1.5 system-ui,sans-serif;background:#16181d;color:#e6e6e6;margin:0;display:grid;place-items:center;min-height:100vh}
main{max-width:34em;padding:24px}a.b{display:inline-block;background:#3d7bfd;color:#fff;padding:.6em 1.2em;border-radius:6px;text-decoration:none}
p.s{color:#9aa0aa;font-size:14px}</style></head>
<body><main><h1>StarDraft collaborative editing</h1><p id="what">You have an invite to a StarDraft 2 session.</p>
<p><a class="b" id="open" href="#">Open in StarDraft 2</a></p>
<p class="s">Nothing happens? Start StarDraft 2, select <b>File &gt; Collaborative editing &gt; Join session</b> and paste the address of this page.</p>
<p class="s">The map goes between the programs end to end encrypted. This server cannot read it.</p></main>
<script nonce="${nonce}">
const f = location.hash.slice(1);
if (/^[ev]\\.[A-Za-z0-9_-]{43}$/.test(f)) {
  document.getElementById('what').textContent = f[0] === 'e' ? 'You have an invite to edit in a StarDraft 2 session.' : 'You have an invite to view a StarDraft 2 session.';
  const link = 'stardraft://join?link=' + encodeURIComponent(location.href);
  const a = document.getElementById('open');
  a.href = link;
  location.href = link;
} else {
  document.getElementById('what').textContent = 'This invite link is not complete. Ask for the link again.';
  document.getElementById('open').remove();
}
</script></body></html>`;
  return new Response(html, {
    headers: {
      ...SECURITY_HEADERS,
      'Content-Type': 'text/html; charset=utf-8',
      'Content-Security-Policy': `default-src 'none'; script-src 'nonce-${nonce}'; style-src 'nonce-${nonce}'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`,
    },
  });
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    if (req.method !== 'GET') return new Response(null, { status: 405 });
    if (url.pathname === '/') return new Response('StarDraft collaboration relay\n', { headers: { ...SECURITY_HEADERS, 'Content-Type': 'text/plain' } });
    if (/^\/j\/[A-Za-z0-9_-]{22}$/.test(url.pathname)) return landingPage();
    const m = url.pathname.match(/^\/relay(?:\/([A-Za-z0-9_-]{22}))?$/);
    if (!m) return new Response('Not found\n', { status: 404 });
    if (req.headers.get('Upgrade') !== 'websocket') return new Response('WebSocket only\n', { status: 426 });
    const ip = req.headers.get('CF-Connecting-IP') || 'none';
    const limit = m[1] ? env.JOIN_LIMIT : env.CREATE_LIMIT;
    if (limit && !(await limit.limit({ key: ip })).success) return new Response('Too many requests\n', { status: 429 });
    const room = m[1] || randomId();
    const stub = env.ROOMS.get(env.ROOMS.idFromName(room));
    const headers = new Headers(req.headers);
    headers.set('X-Room', room);  // (set here: a client cannot choose them)
    headers.set('X-Create', m[1] ? '0' : '1');
    return stub.fetch(new Request(req.url, { headers }));
  },
};

export class Room {
  constructor(ctx) {
    this.ctx = ctx;
    this.buckets = new Map();  // socket -> token bucket (in memory: a room that wakes from hibernation starts full)
    this.lastData = Date.now();
  }

  async fetch(req) {
    const create = req.headers.get('X-Create') === '1', room = req.headers.get('X-Room');
    if (!ROOM_RE.test(room || '')) return new Response(null, { status: 400 });
    if (this.ctx.getWebSockets().length >= MAX_SOCKETS) return new Response('Room is full\n', { status: 429 });
    const meta = await this.ctx.storage.get('meta');
    if (create && meta) return new Response(null, { status: 409 });
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({ role: null, id: 0, create, room, at: Date.now() });
    if (!create && !meta) {  // (one answer for a room that is not there and a wrong token)
      server.send(JSON.stringify({ t: 'error', code: 'not-found' }));
      server.close(1008, 'not found');
    } else {
      await this.ctx.storage.setAlarm(Date.now() + HELLO_MS + 1000);
    }
    return new Response(null, { status: 101, webSocket: client });
  }

  sockets(role) {
    return this.ctx.getWebSockets().filter((w) => {
      const a = w.deserializeAttachment();
      return a && (role === undefined || a.role === role);
    });
  }
  hostSocket() { return this.sockets('host')[0]; }
  memberSocket(id) { return this.sockets('member').find((w) => w.deserializeAttachment().id === id); }

  spend(ws, role, bytes) {
    const r = RATE[role || 'none'], now = Date.now();
    let b = this.buckets.get(ws);
    if (!b || b.role !== role) b = { role, t: now, bytes: r.burstBytes, msgs: r.burstMsgs };
    const dt = (now - b.t) / 1000;
    b.t = now;
    b.bytes = Math.min(r.burstBytes, b.bytes + dt * r.bytes) - bytes;
    b.msgs = Math.min(r.burstMsgs, b.msgs + dt * r.msgs) - 1;
    this.buckets.set(ws, b);
    return b.bytes >= 0 && b.msgs >= 0;
  }

  send(ws, data) { try { ws.send(data); } catch { /* (gone) */ } }
  close(ws, code, why) { try { ws.close(code, why); } catch { /* (gone) */ } }

  async end(why) {
    for (const w of this.ctx.getWebSockets()) this.close(w, 1000, why);
    await this.ctx.storage.deleteAll();
    await this.ctx.storage.deleteAlarm();
  }

  async webSocketMessage(ws, msg) {
    const a = ws.deserializeAttachment();
    const size = typeof msg === 'string' ? msg.length : msg.byteLength;
    if (size > (typeof msg === 'string' ? MAX_TEXT : MAX_FRAME)) return this.close(ws, 1009, 'too big');
    if (!this.spend(ws, a.role, size)) return this.close(ws, 1008, 'rate');
    if (typeof msg !== 'string') return this.binary(ws, a, msg);
    let m;
    try { m = JSON.parse(msg); } catch { return this.close(ws, 1003, 'json'); }
    if (!m || typeof m !== 'object' || typeof m.t !== 'string') return this.close(ws, 1003, 'json');
    if (!a.role) {
      if (a.create && m.t === 'create') return this.create(ws, a, m);
      if (!a.create && m.t === 'join') return this.join(ws, a, m);
      return this.close(ws, 1008, 'hello');
    }
    if (a.role !== 'host') return this.close(ws, 1008, 'not host');
    if (m.t === 'tokens') {
      if (!validToken(m.view) || !validToken(m.edit)) return this.close(ws, 1003, 'token');
      const meta = await this.ctx.storage.get('meta');
      meta.view = m.view, meta.edit = m.edit;
      await this.ctx.storage.put('meta', meta);
      return this.send(ws, JSON.stringify({ t: 'tokens-ok' }));
    }
    if (m.t === 'kick' && Number.isInteger(m.id)) {
      const w = this.memberSocket(m.id);
      if (w) {
        w.serializeAttachment({ ...w.deserializeAttachment(), role: 'gone' });
        this.close(w, 1000, 'removed');
      }
      return;
    }
    if (m.t === 'close') return this.end('closed');
    this.close(ws, 1003, 'unknown');
  }

  async create(ws, a, m) {
    if (m.v !== 1) return this.close(ws, 1008, 'version');
    if (!validToken(m.view) || !validToken(m.edit) || (!m.view && !m.edit)) return this.close(ws, 1003, 'token');
    if (await this.ctx.storage.get('meta')) return this.close(ws, 1008, 'exists');
    const created = Date.now();
    await this.ctx.storage.put('meta', { view: m.view, edit: m.edit, created, next: 1 });
    ws.serializeAttachment({ ...a, role: 'host', id: 0 });
    this.lastData = created;
    await this.ctx.storage.setAlarm(created + 60e3);
    this.send(ws, JSON.stringify({ t: 'created', room: a.room, expires: created + ROOM_LIFETIME_MS }));
  }

  async join(ws, a, m) {
    const notFound = () => {
      this.send(ws, JSON.stringify({ t: 'error', code: 'not-found' }));
      this.close(ws, 1008, 'not found');
    };
    const meta = await this.ctx.storage.get('meta');
    const host = this.hostSocket();
    if (m.v !== 1 || m.room !== a.room || !meta || !host || !(sameToken(m.token, meta.view) || sameToken(m.token, meta.edit))) return notFound();
    if (this.sockets('member').length >= MAX_MEMBERS) {
      this.send(ws, JSON.stringify({ t: 'error', code: 'full' }));
      return this.close(ws, 1008, 'full');
    }
    const id = meta.next++;
    await this.ctx.storage.put('meta', meta);
    ws.serializeAttachment({ ...a, role: 'member', id });
    this.send(ws, JSON.stringify({ t: 'joined', id }));
    this.send(host, JSON.stringify({ t: 'peer', id }));
  }

  binary(ws, a, msg) {
    if (!a.role || a.role === 'gone') return this.close(ws, 1008, 'hello');
    this.lastData = Date.now();
    const data = new Uint8Array(msg);
    if (a.role === 'host') {
      if (data.length < 4) return this.close(ws, 1003, 'short');
      const to = this.memberSocket(new DataView(data.buffer, data.byteOffset).getUint32(0));
      if (to) this.send(to, data.subarray(4));
      return;
    }
    const host = this.hostSocket();
    if (!host) return;
    const out = new Uint8Array(data.length + 4);
    new DataView(out.buffer).setUint32(0, a.id);
    out.set(data, 4);
    this.send(host, out);
  }

  async webSocketClose(ws) { await this.gone(ws); }
  async webSocketError(ws) { await this.gone(ws); }

  async gone(ws) {
    const a = ws.deserializeAttachment();
    this.buckets.delete(ws);
    if (!a) return;
    if (a.role === 'host') return this.end('host left');
    if (a.role === 'member' || a.role === 'gone') {
      const host = this.hostSocket();
      if (host) this.send(host, JSON.stringify({ t: 'left', id: a.id }));
    }
  }

  // Each minute: connections that did not create or join in time go; a room ends after its lifetime, when it had no
  // data for IDLE_MS, or when its host is gone.
  async alarm() {
    const now = Date.now();
    for (const w of this.ctx.getWebSockets()) {
      const a = w.deserializeAttachment();
      if (a && !a.role && now - a.at > HELLO_MS) this.close(w, 1008, 'hello');
    }
    const meta = await this.ctx.storage.get('meta');
    if (!meta) {
      if (this.ctx.getWebSockets().length) await this.ctx.storage.setAlarm(now + HELLO_MS);
      return;
    }
    if (now - meta.created > ROOM_LIFETIME_MS || now - this.lastData > IDLE_MS || !this.hostSocket()) return this.end('expired');
    await this.ctx.storage.setAlarm(now + 60e3);
  }
}
