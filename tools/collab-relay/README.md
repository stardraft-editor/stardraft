# Collaboration relay

StarDraft's shared maps go through this relay. It forwards encrypted messages between the host of a shared map and the
people who join it. It cannot read them: the keys are in the invite links' fragments (the part after `#`), which browsers
never send to a server. It keeps nothing on disk and logs no addresses, room ids or contents.

Node 18 or later, no packages:

```
node tools/collab-relay/relay.mjs --port 8787
```

It listens on 127.0.0.1 only. To let others join from the internet, run a Cloudflare tunnel to it:

```
cloudflared tunnel --url http://localhost:8787
```

cloudflared prints an address such as `https://abc-def.trycloudflare.com`. In StarDraft, set
**File > Shared map > Server address** to it before you share a map. The invite links then use that address.

What the relay serves:

- `GET /j/<room>`: the invite link's page. It opens `stardraft://join?link=...` (the program asks before it joins).
- `GET /relay`: the WebSocket. The host sends `create` with two tokens (one from each invite link's secret), the others
  send `join` with the token of their link. After that, binary messages go from the host to one member, and from a
  member to the host. Members never talk to each other directly, and no one sees another's IP address.

Limits: 8 members a room, 3 rooms an address, 20 joins a minute an address, 1.5 MB a message, rate limits a
connection, a room ends after 24 hours or 30 minutes with no data, or when the host leaves.
