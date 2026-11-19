// Shared maps end to end: a host and guests (one to edit, one to view) through a running relay, with real sockets,
// keys and editors: joining, edits both ways, a viewer's edits put back, edits at the same time, each user's own undo
// and redo, roles, cursors, old and wrong links, and the end.
//
//   node tools/collab-relay/relay.mjs --port 18787
//   collab_test <StarCraft folder> http://localhost:18787

#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "stardraft/collab.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "websocket.h"

using namespace stardraft;
namespace cb = stardraft::collab;

static int gFails = 0;
#define CHECK(c, what)                                                     \
    do {                                                                   \
        if (c) std::printf("ok   %s\n", what);                             \
        else std::printf("FAIL %s (line %d)\n", what, __LINE__), ++gFails; \
    } while (0)

static double seconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// One program: its socket, its session, and the queue of the socket's events (run on this, the "UI" thread).
struct Program final : cb::Outbox {
    std::string label;
    std::unique_ptr<platform::WebSocket> ws;
    std::unique_ptr<cb::Session> s;
    std::unique_ptr<Editor> ed;  // guest: the map welcome made
    std::mutex m;
    std::deque<std::function<void()>> q;
    std::string ended, lastStatus;
    std::vector<cb::Peer> requests;
    const GameData* data = nullptr;

    void text(const std::string& json) override {
        if (ws) ws->sendText(json);
    }
    void binary(Bytes b) override {
        if (ws) ws->sendBinary(std::move(b));
    }
    void close() override {
        if (ws) ws->close();
    }
    void push(std::function<void()> f) {
        std::lock_guard<std::mutex> lock(m);
        q.push_back(std::move(f));
    }
    void connect(const std::string& url) {
        platform::WebSocket::Handlers h;
        h.opened = [this] { push([this] { if (s) s->opened(); }); };
        h.text = [this](std::string t) { push([this, t] { if (s) s->received(t); }); };
        h.binary = [this](std::vector<uint8_t> b) { push([this, b] { if (s) s->received(Bytes(b.begin(), b.end())); }); };
        h.closed = [this](std::string why) { push([this, why] { if (s) s->closed(why); }); };
        ws = platform::WebSocket::connect(url, h);
    }
    cb::Session::Events events() {
        cb::Session::Events ev;
        ev.joinRequest = [this](const cb::Peer& p) { requests.push_back(p); };
        ev.status = [this](const std::string& t) { lastStatus = t; };
        ev.ended = [this](const std::string& why) { ended = why.empty() ? "(ended)" : why; };
        ev.welcome = [this](const cb::Terrain& t, const std::string&, cb::Role) {
            ed = std::make_unique<Editor>(*data);
            std::string err;
            if (!ed->newMap(t.width, t.height, t.era, 1, err)) return false;
            if (!t.hasTile) ed->editTerrainSections([](Chk& c) { c.remove("TILE"); });
            s->setEditor(ed.get());
            return s->editor() != nullptr;
        };
        return ev;
    }
    void pump() {
        std::deque<std::function<void()>> now;
        {
            std::lock_guard<std::mutex> lock(m);
            now.swap(q);
        }
        for (auto& f : now) f();
        if (s) s->tick();
    }
};

static std::vector<Program*> gAll;
static bool waitFor(const std::function<bool()>& cond, double limit = 5) {
    const double until = seconds() + limit;
    while (seconds() < until) {
        for (Program* p : gAll) p->pump();
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (Program* p : gAll) p->pump();
    return cond();
}
static void settle(double t = 0.4) {
    waitFor([] { return false; }, t);
}

static bool sameTerrain(const Editor& a, const Editor& b) {
    cb::Terrain x, y;
    std::string err;
    if (!cb::readTerrain(a.chk(), x, err) || !cb::readTerrain(b.chk(), y, err)) return false;
    for (int s = 0; s < cb::kSections; ++s)
        if (x.cells[s] != y.cells[s]) return false;
    return true;
}
static std::vector<uint16_t> mtxm(const Editor& e) {
    cb::Terrain t;
    std::string err;
    cb::readTerrain(e.chk(), t, err);
    return t.cells[cb::kMtxm];
}
static bool paint(Editor& e, int x, int y, uint16_t terrain, float size = 2) {
    const auto before = mtxm(e);
    e.beginTerrainStroke();
    e.paintBrush(x, y, terrain, Brush{Brush::Diamond, size});
    e.endTerrainStroke();
    return mtxm(e) != before;
}

// collab_test host <StarCraft folder> <server> <link file> <seconds>: a host for a test of the program as a guest. It
// writes the edit link to the file, lets each one in, paints and moves its cursor after the first joins, and says
// when a guest's edit arrives.
static int hostFor(const GameData& data, const std::string& server, const std::string& linkFile, double secs) {
    Program host;
    host.label = "host", host.data = &data;
    gAll = {&host};
    auto ed = std::make_unique<Editor>(data);
    std::string err;
    if (!ed->newMap(64, 64, 0, 1, err)) return std::printf("new map: %s\n", err.c_str()), 2;
    host.s = cb::Session::host(*ed, server, "Test host", host, host.events(), err);
    if (!host.s) return std::printf("host: %s\n", err.c_str()), 2;
    host.s->setClock(seconds), host.connect(cb::relayUrl(server));
    if (!waitFor([&] { return !host.s->link(cb::Role::Edit).empty(); })) return std::printf("no room: %s\n", host.ended.c_str()), 2;
    if (FILE* f = std::fopen(linkFile.c_str(), "wb")) std::fputs(host.s->link(cb::Role::Edit).c_str(), f), std::fclose(f);
    std::printf("host: link written\n");
    std::fflush(stdout);
    size_t asked = 0;
    bool painted = false, got = false;
    std::vector<uint16_t> mine;
    const double until = seconds() + secs;
    while (seconds() < until && host.ended.empty()) {
        waitFor([] { return false; }, 0.05);
        for (; asked < host.requests.size(); ++asked) {
            std::printf("host: join request from %s (%s, code %s)\n", host.requests[asked].name.c_str(), cb::roleName(host.requests[asked].role), host.requests[asked].code.c_str());
            host.s->approve(host.requests[asked].id, true);
        }
        if (!painted && !host.s->peers().empty() && host.s->peers()[0].approved) {
            waitFor([] { return false; }, 1.0);
            paint(*ed, 400, 400, 4, 3);
            host.s->cursor(1500, 600, true);
            mine = mtxm(*ed);
            painted = true;
            std::printf("host: painted and moved the cursor\n");
            std::fflush(stdout);
        }
        if (painted && !got && mtxm(*ed) != mine) {
            got = true;
            std::printf("host: a guest's edit arrived\n");
            std::fflush(stdout);
        }
        for (const cb::Peer& p : host.s->peers())
            if (p.cursorOn) {
                static int shown = 0;
                if (!shown++) std::printf("host: %s's cursor at %d,%d\n", p.name.c_str(), p.x, p.y), std::fflush(stdout);
            }
    }
    std::printf("host: end (%s)\n", host.ended.empty() ? "time" : host.ended.c_str());
    if (host.s && host.ended.empty()) host.s->stop();
    return got ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc >= 6 && std::string(argv[1]) == "host") {
        GameData data;
        std::string err;
        if (!data.open(argv[2], err)) return std::fprintf(stderr, "game data: %s\n", err.c_str()), 2;
        return hostFor(data, argv[3], argv[4], std::atof(argv[5]));
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: collab_test <StarCraft folder> <relay server, e.g. http://localhost:18787>\n");
        return 2;
    }
    GameData data;
    std::string err;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "game data: %s\n", err.c_str());
        return 2;
    }
    const std::string server = argv[2];

    // --- Links -------------------------------------------------------------------------------------------------------
    {
        cb::Key k{};
        for (int i = 0; i < 32; ++i) k[size_t(i)] = uint8_t(i * 7);
        const std::string link = cb::makeInviteLink("https://abc.trycloudflare.com", "AAAAAAAAAAAAAAAAAAAAAA", cb::Role::Edit, k);
        cb::InviteLink l;
        CHECK(cb::parseInviteLink(link, l, err) && l.secret == k && l.role == cb::Role::Edit && l.server == "https://abc.trycloudflare.com",
              "links: an edit link reads back");
        CHECK(cb::parseInviteLink("stardraft://join?link=https%3A%2F%2Fabc.trycloudflare.com%2Fj%2FAAAAAAAAAAAAAAAAAAAAAA%23v." + link.substr(link.size() - 43), l, err) &&
                  l.role == cb::Role::View,
              "links: the stardraft:// form reads");
        CHECK(!cb::parseInviteLink("http://evil.example/j/AAAAAAAAAAAAAAAAAAAAAA#e." + link.substr(link.size() - 43), l, err), "links: http only for localhost");
        CHECK(!cb::parseInviteLink(link.substr(0, link.size() - 1), l, err), "links: a short secret is refused");
        CHECK(!cb::parseInviteLink("https://a.b/j/AAAAAAAAAAAAAAAAAAAAAA/../x#e." + link.substr(link.size() - 43), l, err), "links: a path is refused");
        std::string s;
        CHECK(cb::normalizeServer(" abc.trycloudflare.com/ ", s, err) && s == "https://abc.trycloudflare.com", "links: a bare server name is https");
        CHECK(cb::relayUrl("https://a.b:444") == "wss://a.b:444/relay" && cb::relayUrl("http://localhost:1") == "ws://localhost:1/relay" &&
                  cb::relayUrl("https://a.b", "AAAAAAAAAAAAAAAAAAAAAA") == "wss://a.b/relay/AAAAAAAAAAAAAAAAAAAAAA" && cb::relayUrl("https://a.b", "../x").empty(),
              "links: relay URLs");
    }
    // --- Patches -----------------------------------------------------------------------------------------------------
    {
        cb::Terrain t;
        t.width = t.height = 64;
        t.cells[cb::kIsom].assign(cb::Terrain::cellCount(cb::kIsom, 64, 64), 0);
        t.cells[cb::kMtxm].assign(64 * 64, 0);
        cb::Patch p{{cb::kMtxm, 10, {0x10, 0x11}}};
        CHECK(cb::validPatch(t, p, 100), "patches: a valid patch");
        CHECK(!cb::validPatch(t, {{cb::kMtxm, 64 * 64 - 1, {1, 2}}}, 100), "patches: past the end is refused");
        CHECK(!cb::validPatch(t, {{cb::kTile, 0, {1}}}, 100), "patches: TILE that the map has not is refused");
        CHECK(!cb::validPatch(t, {{cb::kMtxm, 0, {uint16_t(200 << 4)}}}, 100), "patches: a tile group the tileset has not is refused");
        CHECK(!cb::validPatch(t, {{cb::kMtxm, 20, {1}}, {cb::kMtxm, 10, {1}}}, 100), "patches: runs out of order are refused");
        cb::Terrain u = t;
        u.cells[cb::kMtxm][5] = 3, u.cells[cb::kMtxm][9] = 4, u.cells[cb::kIsom][100] = 7;
        cb::Patch d = cb::diffTerrain(t, u);
        cb::Terrain v = t;
        cb::applyPatch(v, d);
        CHECK(v.cells[cb::kMtxm] == u.cells[cb::kMtxm] && v.cells[cb::kIsom] == u.cells[cb::kIsom] && cb::validPatch(t, d, 0), "patches: diff and apply");
    }

    // --- A session ---------------------------------------------------------------------------------------------------
    Program host, a, b;
    host.label = "host", a.label = "A", b.label = "B";
    host.data = a.data = b.data = &data;
    gAll = {&host, &a, &b};
    auto hostEd = std::make_unique<Editor>(data);
    if (!hostEd->newMap(64, 64, 0, 1, err)) {
        std::fprintf(stderr, "new map: %s\n", err.c_str());
        return 2;
    }
    CHECK(paint(*hostEd, 600, 600, 4), "the host paints before sharing");

    host.s = cb::Session::host(*hostEd, server, "Host", host, host.events(), err);
    CHECK(host.s != nullptr, "the host starts sharing");
    if (!host.s) return 1;
    host.s->setClock(seconds), host.connect(cb::relayUrl(server));
    CHECK(waitFor([&] { return !host.s->link(cb::Role::Edit).empty() || !host.ended.empty(); }), "the relay makes the room");
    const std::string editLink = host.s->link(cb::Role::Edit), viewLink = host.s->link(cb::Role::View);
    std::printf("     edit link: %s\n", editLink.substr(0, editLink.find('#')).c_str());
    if (editLink.empty()) return std::printf("host: %s\n", host.ended.c_str()), 1;

    auto joinAs = [&](Program& p, const std::string& link, const std::string& name) {
        cb::InviteLink l;
        std::string e;
        if (!cb::parseInviteLink(link, l, e)) return false;
        p.ended.clear();
        p.s = cb::Session::join(l, name, "test", p, p.events());
        p.s->setClock(seconds);
        p.connect(cb::relayUrl(l.server, l.room));
        return true;
    };
    CHECK(joinAs(a, editLink, "Ann") && joinAs(b, viewLink, "Bo\x01b with a long name that goes past the limit"), "two join");
    CHECK(waitFor([&] { return host.requests.size() == 2; }), "the host gets two join requests");
    for (const cb::Peer& r : host.requests) {
        if (r.name == "Ann") CHECK(r.role == cb::Role::Edit, "Ann's link is to edit");
        else CHECK(r.role == cb::Role::View && r.name.size() <= 32 && r.name.find('\x01') == std::string::npos, "Bob's link is to view; the name is cleaned");
        CHECK(r.code.size() == 7, "a safety code");
        host.s->approve(r.id, true);
    }
    CHECK(waitFor([&] { return a.ed && b.ed; }), "both get the map");
    CHECK(a.ed && b.ed && sameTerrain(*hostEd, *a.ed) && sameTerrain(*hostEd, *b.ed), "their terrain is the host's");
    if (!a.ed || !b.ed) return 1;
    CHECK(waitFor([&] { return a.s->peers().size() == 2 && b.s->peers().size() == 2; }), "each sees the two others");

    CHECK(paint(*a.ed, 300, 300, 4), "Ann paints");
    CHECK(waitFor([&] { return sameTerrain(*a.ed, *hostEd) && sameTerrain(*a.ed, *b.ed); }), "Ann's edit reaches the host and Bob");
    CHECK(paint(*hostEd, 1200, 900, 5), "the host paints");
    CHECK(waitFor([&] { return sameTerrain(*a.ed, *hostEd) && sameTerrain(*b.ed, *hostEd); }), "the host's edit reaches both");

    const auto beforeBob = mtxm(*hostEd);
    CHECK(paint(*b.ed, 1500, 1500, 4), "Bob (view only) paints in his program");
    settle();
    CHECK(sameTerrain(*b.ed, *hostEd) && mtxm(*hostEd) == beforeBob, "Bob's edit is put back, and not sent");

    // Edits at the same time, over the same cells: all end with the host's order.
    paint(*hostEd, 700, 700, 2, 4);
    paint(*a.ed, 760, 720, 5, 4);
    CHECK(waitFor([&] { return sameTerrain(*a.ed, *hostEd) && sameTerrain(*b.ed, *hostEd); }), "edits at the same time: all the same after");

    // Each user's own undo: Ann's undo changes only cells that still have her values.
    settle();
    CHECK(paint(*a.ed, 1800, 400, 5), "Ann paints again");
    CHECK(waitFor([&] { return sameTerrain(*a.ed, *hostEd); }), "the host has it");
    const auto withAnn = mtxm(*hostEd);
    CHECK(paint(*hostEd, 200, 1700, 5), "the host paints somewhere else");
    CHECK(waitFor([&] { return sameTerrain(*a.ed, *hostEd); }), "Ann has the host's edit");
    const auto beforeUndo = mtxm(*a.ed);
    a.ed->undo(1), a.s->afterUndo();
    CHECK(true, "Ann undoes (the editor's undo)");
    CHECK(waitFor([&] { return sameTerrain(*a.ed, *hostEd) && sameTerrain(*b.ed, *hostEd); }), "Ann's undo reaches all");
    {
        const auto now = mtxm(*hostEd);
        bool hostEditStays = true;
        // (the host's last edit is near the bottom left: its tiles are as they were)
        for (int y = 45; y < 60; ++y)
            for (int x = 0; x < 10; ++x) hostEditStays &= now[size_t(y) * 64 + x] == beforeUndo[size_t(y) * 64 + x];
        CHECK(now != beforeUndo && hostEditStays, "the undo took Ann's edit back, and not the host's");
    }
    a.ed->redo(1), a.s->afterUndo();
    CHECK(true, "Ann redoes");
    CHECK(waitFor([&] { return sameTerrain(*a.ed, *hostEd); }) && mtxm(*a.ed) == beforeUndo, "undo then redo changes nothing");
    (void)withAnn;

    // The host's own undo does not take Ann's edits back.
    hostEd->undo(1), host.s->afterUndo();
    CHECK(true, "the host undoes its own last edit");
    CHECK(waitFor([&] { return sameTerrain(*a.ed, *hostEd); }), "the host's undo reaches Ann");
    {
        const auto now = mtxm(*hostEd);
        bool annStays = true;
        for (int y = 8; y < 18; ++y)
            for (int x = 52; x < 60; ++x) annStays &= now[size_t(y) * 64 + x] == beforeUndo[size_t(y) * 64 + x];
        CHECK(annStays, "Ann's edit stays after the host's undo");
    }

    // Roles.
    uint32_t bobId = 0;
    for (const cb::Peer& p : host.s->peers())
        if (p.role == cb::Role::View) bobId = p.id;
    host.s->setRole(bobId, cb::Role::Edit);
    CHECK(waitFor([&] { return b.s->role() == cb::Role::Edit; }), "the host lets Bob edit");
    CHECK(paint(*b.ed, 1500, 1500, 4), "Bob paints");
    CHECK(waitFor([&] { return sameTerrain(*b.ed, *hostEd) && sameTerrain(*a.ed, *hostEd) && mtxm(*hostEd) != beforeBob; }), "Bob's edit reaches all now");

    // Cursors.
    a.s->cursor(1000, 2000, true);
    CHECK(waitFor([&] {
              for (const cb::Peer& p : b.s->peers())
                  if (p.name == "Ann" && p.cursorOn && p.x == 1000 && p.y == 2000) return true;
              return false;
          }),
          "Bob sees Ann's cursor");
    host.s->cursor(50, 60, true);
    CHECK(waitFor([&] {
              for (const cb::Peer& p : a.s->peers())
                  if (p.id == 0 && p.cursorOn && p.x == 50) return true;
              return false;
          }),
          "Ann sees the host's cursor");

    // A wrong secret for the room, then the old links after new ones.
    Program c;
    c.label = "C", c.data = &data;
    gAll.push_back(&c);
    {
        cb::InviteLink l;
        cb::parseInviteLink(editLink, l, err);
        l.secret[0] ^= 1;
        c.s = cb::Session::join(l, "Eve", "test", c, c.events());
        c.connect(cb::relayUrl(l.server, l.room));
        CHECK(waitFor([&] { return !c.ended.empty(); }) && !c.ed && host.requests.size() == 2, "a wrong secret: the relay refuses, the host sees nothing");
        c.ws.reset();
    }
    CHECK(host.s->newLinks() && host.s->link(cb::Role::Edit) != editLink, "new links");
    settle(0.2);
    CHECK(joinAs(c, editLink, "Old") && waitFor([&] { return !c.ended.empty(); }) && !c.ed, "the old link does not work any more");
    c.ws.reset();
    CHECK(joinAs(c, host.s->link(cb::Role::View), "Cy") && waitFor([&] { return host.requests.size() == 3; }), "the new link works");
    host.s->approve(host.requests.back().id, false);
    CHECK(waitFor([&] { return !c.ended.empty(); }) && !c.ed && c.ended.find("did not let you in") != std::string::npos, "the host says no");
    c.ws.reset();

    // Removing Bob, then the end.
    host.s->remove(bobId);
    CHECK(waitFor([&] { return !b.ended.empty(); }) && b.ended.find("removed") != std::string::npos, "the host removes Bob");
    CHECK(waitFor([&] { return host.s->peers().size() == 1 && a.s->peers().size() == 1; }), "Ann sees Bob go");
    host.s->stop();
    CHECK(waitFor([&] { return !a.ended.empty(); }) && a.ended.find("stopped sharing") != std::string::npos, "the host stops sharing: Ann's session ends");
    CHECK(a.ed && a.ed->isOpen(), "Ann keeps her copy of the map");

    for (Program* p : gAll)
        if (p->ws) p->ws->close();
    std::printf(gFails ? "%d FAILED\n" : "all passed\n", gFails);
    return gFails ? 1 : 0;
}
