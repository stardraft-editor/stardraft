// Shared maps end to end: a host and guests (one to edit, one to view) through a running relay, with real sockets,
// keys and editors: joining, edits both ways, a viewer's edits put back, edits at the same time, each user's own undo
// and redo, roles, cursors, old and wrong links, and the end.
//
//   node tools/collab-relay/relay.mjs --port 18787
//   collab_test <StarCraft folder> http://localhost:18787

#include <algorithm>
#include <chrono>
#include <random>
#include <set>
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

    size_t sentBytes = 0, sentMsgs = 0;
    void text(const std::string& json) override {
        sentBytes += json.size(), ++sentMsgs;
        if (ws) ws->sendText(json);
    }
    void binary(Bytes b) override {
        sentBytes += b.size(), ++sentMsgs;
        if (ws) ws->sendBinary(std::move(b));
    }
    void close() override {
        if (ws) ws->close();
    }
    void push(std::function<void()> f) {
        std::lock_guard<std::mutex> lock(m);
        q.push_back(std::move(f));
    }
    // (each connection has a number: the events of one that was dropped do not reach the session)
    int gen = 0;
    bool autoReconnect = true;
    double retryAt = 0;
    std::vector<cb::Change> changes;
    std::vector<std::array<uint32_t, 3>> pings;
    void connect(const std::string& url) {
        platform::WebSocket::Handlers h;
        const int g = ++gen;
        auto in = [this, g](std::function<void()> f) { push([this, g, f] { if (s && g == gen) f(); }); };
        h.opened = [this, in] { in([this] { s->opened(); }); };
        h.text = [this, in](std::string t) { in([this, t] { s->received(t); }); };
        h.binary = [this, in](std::vector<uint8_t> b) { in([this, b] { s->received(Bytes(b.begin(), b.end())); }); };
        h.closed = [this, in](std::string why) { in([this, why] { s->closed(why); }); };
        ws = platform::WebSocket::connect(url, h);
    }
    // The connection is lost (as a network that goes away).
    void drop() {
        ++gen;
        ws.reset();
        push([this] { if (s) s->closed("the connection was lost"); });
    }
    cb::Session::Events events() {
        cb::Session::Events ev;
        ev.connection = [this](bool lost) {
            if (lost) retryAt = seconds() + 0.3;
        };
        ev.changed = [this](const cb::Change& c) { changes.push_back(c); };
        ev.ping = [this](uint32_t who, int x, int y) { pings.push_back({who, uint32_t(x), uint32_t(y)}); };
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
        if (s && retryAt && autoReconnect && seconds() >= retryAt) {
            retryAt = 0;
            if (s->reconnecting()) ++gen, ws.reset(), connect(s->reconnectUrl());
        }
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
static bool sameObjects(const Editor& a, const Editor& b) {
    for (uint8_t s = 0; s < cb::kObjSections; ++s)
        if (cb::readObjects(a.chk(), s) != cb::readObjects(b.chk(), s)) return false;
    return true;
}
static bool sameMap(const Editor& a, const Editor& b) { return sameTerrain(a, b) && sameObjects(a, b); }
static std::set<uint32_t> serials(const Editor& e) {
    std::set<uint32_t> out;
    for (const Unit& u : e.units()) out.insert(u.serial);
    return out;
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

// collab_test stress <StarCraft folder> <server> <members> <seconds> [<map size>]: a host and members that all edit at
// once (terrain, units, doodads, sprites, undo, redo) for that time, then stop: all maps must be the same. It prints the
// join time, how long the programs' work took (each pump: the received messages and the tick), the time until all were
// the same, and the counts.
static int stress(const GameData& data, const std::string& server, int members, double secs, int size, uint32_t seed, int startUnits) {
    std::vector<std::unique_ptr<Program>> ps;
    for (int i = 0; i <= members; ++i) {
        ps.push_back(std::make_unique<Program>());
        ps.back()->label = i ? "member " + std::to_string(i) : "host";
        ps.back()->data = &data;
    }
    gAll.clear();
    for (auto& p : ps) gAll.push_back(p.get());
    Program& host = *ps[0];
    auto hostEd = std::make_unique<Editor>(data);
    std::string err;
    if (!hostEd->newMap(uint16_t(size), uint16_t(size), 0, 1, err)) return std::printf("new map: %s\n", err.c_str()), 2;
    for (int k = 0; k < startUnits; ++k)
        hostEd->placeUnit(0, uint8_t(k % 8), 100 + (k * 97) % (size * 32 - 200), 100 + (k * 61 + k / 300 * 13) % (size * 32 - 200), false);
    host.s = cb::Session::host(*hostEd, server, "Host", host, host.events(), err);
    if (!host.s) return std::printf("host: %s\n", err.c_str()), 2;
    host.s->setClock(seconds), host.connect(cb::relayUrl(server));
    if (!waitFor([&] { return !host.s->link(cb::Role::Edit).empty(); }, 10)) return std::printf("no room: %s\n", host.ended.c_str()), 1;
    const std::string link = host.s->link(cb::Role::Edit);
    const double joinStart = seconds();
    const int late = members;  // (the last one joins halfway through, while the others edit)
    auto joinOne = [&](int i) {
        cb::InviteLink l;
        cb::parseInviteLink(link, l, err);
        Program& p = *ps[size_t(i)];
        p.s = cb::Session::join(l, "Member " + std::to_string(i), "test", p, p.events());
        p.s->setClock(seconds);
        p.connect(cb::relayUrl(l.server, l.room));
    };
    for (int i = 1; i < late; ++i) joinOne(i);
    size_t asked = 0;
    const bool joined = waitFor([&] {
        for (; asked < host.requests.size(); ++asked) host.s->approve(host.requests[asked].id, true);
        for (int i = 1; i < late; ++i)
            if (!ps[size_t(i)]->ed) return false;
        return true;
    }, 30);
    const double joinTime = seconds() - joinStart;
    std::printf("stress: %d x %d map, %zu units; %d members joined in %.2f s%s\n", size, size, hostEd->units().size(), members, joinTime, joined ? "" : " (NOT ALL)");
    if (!joined) {
        for (auto& p : ps)
            if (!p->ed || !p->ended.empty()) std::printf("  %s: %s\n", p->label.c_str(), p->ended.empty() ? "no map yet" : p->ended.c_str());
        return 1;
    }

    std::vector<uint16_t> doodads;
    for (uint16_t id = 0; id < 4000 && doodads.size() < 40; ++id)
        if (hostEd->doodadTypes().find(id)) doodads.push_back(id);
    const uint16_t terrains[] = {2, 4, 5}, unitTypes[] = {0, 1, 7, 37, 64};
    std::mt19937 rng(seed);
    auto pick = [&](int n) { return int(rng() % uint32_t(n)); };
    std::vector<Editor*> eds{hostEd.get()};
    for (int i = 1; i < late; ++i) eds.push_back(ps[size_t(i)]->ed.get());
    bool lateAsked = false;
    double lateJoined = -1;
    long ops = 0, undos = 0;
    double pumpMax = 0, pumpSum = 0;
    long pumps = 0;
    auto pumpAll = [&] {
        for (auto& p : ps) {
            const double t = seconds();
            p->pump();
            const double d = seconds() - t;
            pumpMax = std::max(pumpMax, d), pumpSum += d, ++pumps;
        }
    };
    const double until = seconds() + secs;
    while (seconds() < until) {
        if (!lateAsked && seconds() > until - secs / 2) joinOne(late), lateAsked = true, lateJoined = seconds();
        for (; asked < host.requests.size(); ++asked) host.s->approve(host.requests[asked].id, true);
        if (lateAsked && eds.size() < size_t(late) + 1 && ps[size_t(late)]->ed) {
            eds.push_back(ps[size_t(late)]->ed.get());
            std::printf("stress: member %d joined halfway through, in %.2f s\n", late, seconds() - lateJoined);
        }
        for (size_t i = 0; i < eds.size(); ++i) {
            if (pick(100) >= 12) continue;  // (each program: an edit about 6 times a second)
            Editor& e = *eds[i];
            Program& p = *ps[i];
            const int x = 64 + pick(size * 32 - 128), y = 64 + pick(size * 32 - 128);
            const int op = pick(100);
            ++ops;
            if (op < 40) paint(e, x, y, terrains[pick(3)], float(1 + pick(3)));
            else if (op < 60) e.placeUnit(unitTypes[pick(5)], uint8_t(pick(8)), x, y);
            else if (op < 68 && !e.units().empty()) {
                e.select(pick(int(e.units().size())), false);
                e.deleteSelection();
            } else if (op < 78 && !doodads.empty()) e.placeDoodad(doodads[size_t(pick(int(doodads.size())))], x, y, false);
            else if (op < 84) e.placeSprite(uint16_t(130 + pick(20)), x, y, uint8_t(pick(8)));
            else if (op < 94) e.undo(1), p.s->afterUndo(), ++undos;
            else e.redo(1), p.s->afterUndo(), ++undos;
            p.s->cursor(x, y, true);
        }
        pumpAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    const double stop = seconds();
    bool same = false;
    while (seconds() - stop < 30 && !(same = [&] {
               for (size_t i = 1; i < eds.size(); ++i)
                   if (!sameMap(*eds[0], *eds[i])) return false;
               return true;
           }())) {
        pumpAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const double settleTime = seconds() - stop;
    if (!same)
        for (size_t i = 1; i < eds.size(); ++i) {
            std::string diff;
            if (!sameTerrain(*eds[0], *eds[i])) diff += " terrain";
            for (uint8_t s = 0; s < cb::kObjSections; ++s) {
                const Bytes a = cb::readObjects(eds[0]->chk(), s), b = cb::readObjects(eds[i]->chk(), s);
                if (a != b) diff += " section" + std::to_string(s) + "(" + std::to_string(a.size()) + "/" + std::to_string(b.size()) + ")";
            }
            if (!diff.empty()) std::printf("  member %zu differs:%s\n", i, diff.c_str());
        }
    bool alive = true;
    for (auto& p : ps) alive &= p->ended.empty();
    std::printf("stress: %ld edits (%ld undo or redo) in %.0f s by %d programs\n", ops, undos, secs, members + 1);
    std::printf("stress: all the same %.2f s after the last edit: %s\n", settleTime, same ? "yes" : "NO");
    std::printf("stress: a program's work each pump: average %.2f ms, longest %.1f ms\n", 1000 * pumpSum / std::max(1L, pumps), 1000 * pumpMax);
    std::printf("stress: sent by the host %.1f MB in %zu messages (%.0f KB/s); by a member on average %.1f MB in %zu messages\n", ps[0]->sentBytes / 1e6,
                ps[0]->sentMsgs, ps[0]->sentBytes / 1e3 / secs, [&] { double t = 0; for (int i = 1; i <= members; ++i) t += ps[size_t(i)]->sentBytes; return t / members / 1e6; }(),
                [&] { size_t t = 0; for (int i = 1; i <= members; ++i) t += ps[size_t(i)]->sentMsgs; return t / size_t(members); }());
    std::printf("stress: host %zu units, %zu doodads, %zu sprites; sessions %s\n", hostEd->units().size(), hostEd->doodads().size(), hostEd->sprites().size(),
                alive ? "all open" : "SOME ENDED");
    for (auto& p : ps)
        if (!p->ended.empty()) std::printf("  %s: %s\n", p->label.c_str(), p->ended.c_str());
    if (host.s) host.s->stop();
    settle(0.5);
    return same && alive ? 0 : 1;
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
    bool painted = false, got = false, gotUnit = false;
    const size_t units = ed->units().size();
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
            // (for a look at the guest's screen: two units the host selected, its brush, a chat, a ping)
            ed->placeUnit(0, 0, 700, 1300), ed->placeUnit(0, 0, 760, 1340);
            cb::Presence pr;
            pr.viewX = 900, pr.viewY = 200, pr.viewW = 1000, pr.viewH = 700, pr.tool = cb::Tool::Terrain, pr.brushW = 3, pr.brushH = 3;
            for (const Unit& u : ed->units()) pr.units.push_back(u.serial);
            host.s->presence(pr);
            host.s->chat("Ramp to the high ground here?");
            host.s->ping(1200, 1500);
            mine = mtxm(*ed);
            painted = true;
            std::printf("host: painted and moved the cursor\n");
            std::fflush(stdout);
        }
        static double nextPaint = 0;  // (more edits, each second: the guest sees them flash)
        if (painted && seconds() > nextPaint) {
            static int k = 0;
            nextPaint = seconds() + 1.0;
            paint(*ed, 1300 + (k % 4) * 64, 700 + (k / 4 % 3) * 64, uint16_t(4 + k % 2), 2);
            if (k % 3 == 0) host.s->ping(1200, 1500);
            ++k;
            mine = mtxm(*ed);
        }
        if (painted && !gotUnit && ed->units().size() > units + 2) {
            gotUnit = true;
            std::printf("host: a guest's unit arrived\n");
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
    if (argc >= 6 && std::string(argv[1]) == "stress") {
        GameData data;
        std::string err;
        if (!data.open(argv[2], err)) return std::fprintf(stderr, "game data: %s\n", err.c_str()), 2;
        return stress(data, argv[3], std::atoi(argv[4]), std::atof(argv[5]), argc >= 7 ? std::atoi(argv[6]) : 256, argc >= 8 ? uint32_t(std::atoi(argv[7])) : 12345,
                      argc >= 9 ? std::atoi(argv[8]) : 200);
    }
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
        cb::Secret k{};
        for (int i = 0; i < 16; ++i) k[size_t(i)] = uint8_t(i * 7);
        const std::string link = cb::makeInviteLink("https://abc.trycloudflare.com", "AAAAAAAAAAAA", cb::Role::Edit, k);
        cb::InviteLink l;
        CHECK(cb::parseInviteLink(link, l, err) && l.secret == k && l.role == cb::Role::Edit && l.server == "https://abc.trycloudflare.com",
              "links: an edit link reads back");
        CHECK(cb::parseInviteLink("stardraft://join?link=https%3A%2F%2Fabc.trycloudflare.com%2Fj%2FAAAAAAAAAAAA%23v" + link.substr(link.size() - 22), l, err) &&
                  l.role == cb::Role::View,
              "links: the stardraft:// form reads");
        const std::string enc = "https%3A%2F%2Fabc.trycloudflare.com%2Fj%2FAAAAAAAAAAAA%23e" + link.substr(link.size() - 22);
        CHECK(cb::parseInviteLink("stardraft://join/?link=" + enc, l, err) && l.secret == k && cb::parseInviteLink("stardraft://join?link=" + enc + "/", l, err) &&
                  cb::parseInviteLink("StarDraft:join?link=" + enc, l, err) && l.secret == k,
              "links: the stardraft:// form as browsers give it (a / before the ? or at the end, no //, any case)");
        CHECK(cb::parseInviteLink("\"" + link + "\"", l, err) && cb::parseInviteLink("<" + link + ">", l, err) && cb::parseInviteLink(link + "/", l, err) &&
                  l.secret == k,
              "links: a pasted link in quotes, in angle brackets or with a / at the end");
        CHECK(!cb::parseInviteLink("http://evil.example/j/AAAAAAAAAAAA#e" + link.substr(link.size() - 22), l, err), "links: http only for localhost");
        CHECK(!cb::parseInviteLink(link.substr(0, link.size() - 1), l, err), "links: a short secret is refused");
        CHECK(!cb::parseInviteLink("https://a.b/j/AAAAAAAAAAAA/../x#e" + link.substr(link.size() - 22), l, err), "links: a path is refused");
        std::string s;
        CHECK(link.size() < 72, "links: an invite link is short");
        CHECK(cb::normalizeServer(" abc.trycloudflare.com/ ", s, err) && s == "https://abc.trycloudflare.com", "links: a bare server name is https");
        CHECK(cb::relayUrl("https://a.b:444") == "wss://a.b:444/relay" && cb::relayUrl("http://localhost:1") == "ws://localhost:1/relay" &&
                  cb::relayUrl("https://a.b", "AAAAAAAAAAAA") == "wss://a.b/relay/AAAAAAAAAAAA" && cb::relayUrl("https://a.b", "../x").empty(),
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
    // --- Object edits ----------------------------------------------------------------------------------------------------
    {
        auto unit = [](uint32_t serial, uint16_t x) {
            Bytes u(36, 0);
            for (int b = 0; b < 4; ++b) u[size_t(b)] = uint8_t(serial >> (8 * b));
            u[4] = uint8_t(x), u[5] = uint8_t(x >> 8);
            return u;
        };
        Bytes list;
        for (uint32_t s = 1; s <= 3; ++s) {
            const Bytes u = unit(s, uint16_t(s * 10));
            list.insert(list.end(), u.begin(), u.end());
        }
        // Two users move unit 2: the second edit removes the first's version (the unit of serial 2), no copy is left.
        cb::ObjEdit first{cb::oUnits, {unit(2, 20)}, {unit(2, 99)}}, second{cb::oUnits, {unit(2, 20)}, {unit(2, 77)}};
        Bytes after = cb::applyObjects(cb::oUnits, cb::applyObjects(cb::oUnits, list, first), second);
        CHECK(after.size() == 3 * 36, "objects: two moves of one unit leave one unit");
        // Many removals: the merge is quick (a member cannot stop the host with one message).
        Bytes many, none;
        cb::ObjEdit big;
        big.section = cb::oDoodads;
        for (uint32_t k = 0; k < 65535; ++k) {
            const Bytes r{uint8_t(k), uint8_t(k >> 8), 1, 2, 3, 4, 5, 6};
            many.insert(many.end(), r.begin(), r.end());
            big.removed.push_back(Bytes{uint8_t(k), uint8_t(k >> 8), 9, 9, 9, 9, 9, 9});
        }
        const double t0 = seconds();
        none = cb::applyObjects(cb::oDoodads, many, big);
        CHECK(none.size() == many.size() && seconds() - t0 < 2, "objects: 65535 removals of 65535 doodads in under 2 seconds");
        CHECK(!cb::validObjects(cb::oLocations, Bytes(1025 * 20, 0), 0) && !cb::validObjects(cb::oUnits, Bytes(37, 0), 0), "objects: sizes are checked");
        // The changes the host sends: the list comes out the same, in order, for random lists and edits.
        std::mt19937 rng(5);
        bool roundTrip = true;
        size_t deltaBytes = 0, wholeBytes = 0;
        for (int round = 0; round < 300 && roundTrip; ++round) {
            Bytes from;
            const int n = int(rng() % 400);
            for (int k = 0; k < n; ++k) {
                const Bytes u = unit(uint32_t(rng() % 50), uint16_t(rng()));  // (some the same: serials repeat)
                from.insert(from.end(), u.begin(), u.end());
            }
            Bytes to = from;
            const int edits = int(rng() % 6);
            for (int e = 0; e < edits; ++e) {
                const size_t count = to.size() / 36;
                const int kind = int(rng() % 3);
                if (kind == 0 || !count) {
                    const Bytes u = unit(uint32_t(rng()), uint16_t(rng()));
                    to.insert(to.end(), u.begin(), u.end());
                } else if (kind == 1) {
                    const size_t k = rng() % count;
                    to.erase(to.begin() + long(k * 36), to.begin() + long(k * 36 + 36));
                } else {
                    to[(rng() % count) * 36 + 5] ^= 0x55;  // (a move in place)
                }
            }
            const Bytes delta = cb::deltaObjects(cb::oUnits, from, to);
            Bytes back;
            roundTrip = cb::undeltaObjects(cb::oUnits, from, delta, back) && back == to && cb::objectsHash(back) == cb::objectsHash(to);
            deltaBytes += delta.size(), wholeBytes += to.size();
        }
        CHECK(roundTrip, "objects: 300 random changes come out the same, in order");
        CHECK(deltaBytes * 4 < wholeBytes, "objects: the changes are much smaller than the lists");
        Bytes bad;
        const Bytes evil{0, 0xFF, 0xFF, 0xFF, 0xFF, 1, 0, 0, 0};  // (a run past the member's list)
        CHECK(!cb::undeltaObjects(cb::oUnits, Bytes(36, 0), evil, bad), "objects: a change that reads past the list is refused");
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

    // Units, doodads, sprites, locations.
    {
        const size_t hostUnits = hostEd->units().size();
        b.ed->placeUnit(0, 0, 900, 900);  // (a Terran Marine of Player 1)
        settle();
        CHECK(sameMap(*b.ed, *hostEd) && hostEd->units().size() == hostUnits, "Bob (view only) places a unit: it is put back, and not sent");

        a.ed->placeUnit(0, 0, 400, 400);
        CHECK(waitFor([&] { return hostEd->units().size() == hostUnits + 1 && sameMap(*a.ed, *hostEd) && sameMap(*b.ed, *hostEd); }),
              "Ann places a unit: the host and Bob have it");
        // At the same time: the two new units have serials of their own.
        hostEd->placeUnit(0, 1, 1200, 400);
        a.ed->placeUnit(0, 0, 450, 400);
        CHECK(waitFor([&] { return hostEd->units().size() == hostUnits + 3 && sameMap(*a.ed, *hostEd) && sameMap(*b.ed, *hostEd); }),
              "units placed at the same time: all have both");
        CHECK(serials(*hostEd).size() == hostEd->units().size(), "each unit has a serial of its own");

        uint16_t doodad = 0;
        for (uint16_t id = 0; id < 4000 && !doodad; ++id)
            if (hostEd->doodadTypes().find(id)) doodad = id;
        const size_t doodads = hostEd->doodads().size();
        CHECK(doodad && a.ed->placeDoodad(doodad, 1600, 1600, false), "Ann places a doodad");
        CHECK(waitFor([&] { return hostEd->doodads().size() == doodads + 1 && sameMap(*a.ed, *hostEd) && sameMap(*b.ed, *hostEd); }),
              "the doodad (DD2, THG2 and its tiles) reaches all");
        const size_t sprites = hostEd->sprites().size();
        CHECK(hostEd->placeSprite(130, 300, 1600, 0), "the host places a sprite");
        CHECK(waitFor([&] { return a.ed->sprites().size() == sprites + 1 && sameMap(*a.ed, *hostEd) && sameMap(*b.ed, *hostEd); }), "the sprite reaches all");
        const int loc = a.ed->createLocation(Rect{64, 64, 320, 256}, false, 3, "Ann's base");
        CHECK(loc >= 0, "Ann makes a location");
        CHECK(waitFor([&] {
                  return sameMap(*a.ed, *hostEd) && sameMap(*b.ed, *hostEd) && size_t(loc) < hostEd->locations().size() &&
                         hostEd->locationName(loc) == "Ann's base";
              }),
              "the location and its name reach all");

        // Ann's undo takes back her location, not the host's sprite or units.
        const size_t unitsNow = hostEd->units().size(), spritesNow = hostEd->sprites().size();
        a.ed->undo(1), a.s->afterUndo();
        CHECK(waitFor([&] { return sameMap(*a.ed, *hostEd) && hostEd->locationName(loc) != "Ann's base"; }),
              "Ann's undo takes her location back on all");
        CHECK(hostEd->units().size() == unitsNow && hostEd->sprites().size() == spritesNow, "the host's units and sprite stay");
        a.ed->redo(1), a.s->afterUndo();
        CHECK(waitFor([&] { return sameMap(*a.ed, *hostEd) && hostEd->locationName(loc) == "Ann's base"; }),
              "Ann's redo puts it back");
        settle();

        // Undo and redo of a unit placement.
        const size_t before = hostEd->units().size();
        a.ed->placeUnit(0, 0, 500, 1100);
        CHECK(waitFor([&] { return hostEd->units().size() == before + 1 && sameMap(*a.ed, *hostEd); }), "Ann places one more unit");
        settle(0.2);
        a.ed->undo(1), a.s->afterUndo();
        CHECK(waitFor([&] { return hostEd->units().size() == before && sameMap(*a.ed, *hostEd); }), "Ann's undo takes the unit back");
        settle(0.2);
        a.ed->redo(1), a.s->afterUndo();
        CHECK(waitFor([&] { return hostEd->units().size() == before + 1 && sameMap(*a.ed, *hostEd) && sameMap(*b.ed, *hostEd); }),
              "Ann's redo puts the unit back on all");

        // A unit that Ann places while the host is in a stroke waits for the stroke's end (it is not lost).
        hostEd->beginStroke();
        a.ed->placeUnit(0, 0, 700, 1100);
        settle(0.5);
        CHECK(hostEd->units().size() == before + 1, "the host's stroke: Ann's unit waits");
        hostEd->endStroke();
        CHECK(waitFor([&] { return hostEd->units().size() == before + 2 && sameMap(*a.ed, *hostEd) && sameMap(*b.ed, *hostEd); }),
              "after the stroke the host has Ann's unit, and all are the same");
        settle();
    }

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
    // Cursor chat: the text goes with the cursor, cleaned (no control characters), and goes away.
    a.s->chat("hi \x01there");
    CHECK(waitFor([&] {
              for (const cb::Peer& p : b.s->peers())
                  if (p.name == "Ann" && p.chat == "hi there") return true;
              return false;
          }),
          "Bob sees Ann's chat, cleaned");
    host.s->chat("hello");
    CHECK(waitFor([&] {
              for (const cb::Peer& p : a.s->peers())
                  if (p.id == 0 && p.chat == "hello") return true;
              return false;
          }),
          "Ann sees the host's chat");
    a.s->chat("");
    CHECK(waitFor([&] {
              for (const cb::Peer& p : host.s->peers())
                  if (p.name == "Ann" && p.chat.empty()) return true;
              return false;
          }),
          "Ann's chat goes away");
    // A new display name: the others see it, from a member and from the host.
    a.s->rename("Anna");
    auto named = [&](Program& who, uint32_t id, const char* name) {
        for (const cb::Peer& p : who.s->peers())
            if ((id == 0 ? p.id == 0 : p.id != 0) && p.name == name) return true;
        return false;
    };
    CHECK(waitFor([&] { return named(b, 1, "Anna") && named(host, 1, "Anna"); }), "a member's new name reaches all");
    host.s->rename("Hosty");
    CHECK(waitFor([&] { return named(a, 0, "Hosty") && named(b, 0, "Hosty"); }), "the host's new name reaches all");
    a.s->rename("Ann");
    CHECK(waitFor([&] { return named(b, 1, "Ann"); }), "and back");

    // Presence: Ann's view, tool and selection reach all.
    {
        cb::Presence pr;
        pr.viewX = 64, pr.viewY = 96, pr.viewW = 640, pr.viewH = 480, pr.tool = cb::Tool::Terrain, pr.brushW = 3, pr.brushH = 2;
        pr.units = {7, 9}, pr.locations = {2};
        a.s->presence(pr);
        auto sees = [&](Program& who) {
            for (const cb::Peer& p : who.s->peers())
                if (p.name == "Ann" && p.presence == pr) return true;
            return false;
        };
        CHECK(waitFor([&] { return sees(b) && sees(host); }), "Ann's view, tool and selection reach all");
        cb::Presence hp;
        hp.viewW = 100, hp.viewH = 100, hp.tool = cb::Tool::Units;
        host.s->presence(hp);
        CHECK(waitFor([&] {
                  for (const cb::Peer& p : a.s->peers())
                      if (p.id == 0 && p.presence == hp) return true;
                  return false;
              }),
              "the host's presence reaches the members");
    }
    // Pings: all but the one who pings see it, with who.
    {
        a.pings.clear(), host.pings.clear(), b.pings.clear();
        b.s->ping(100, 200);
        CHECK(waitFor([&] { return a.pings.size() == 1 && host.pings.size() == 1; }) && b.pings.empty(), "Bob's ping reaches the others");
        CHECK(!a.pings.empty() && a.pings[0][1] == 100 && a.pings[0][2] == 200 && a.pings[0][0] == host.pings[0][0], "at its place, from Bob");
        host.s->ping(300, 40);
        CHECK(waitFor([&] { return a.pings.size() == 2 && b.pings.size() == 1; }) && b.pings[0][0] == 0, "the host's ping reaches the members");
    }
    // Change reports: the others' edits, with their places.
    {
        a.changes.clear(), b.changes.clear(), host.changes.clear();
        CHECK(paint(*a.ed, 900, 300, 5), "Ann paints again");
        auto terrain = [](const Program& p) {
            for (const cb::Change& c : p.changes)
                if (c.what == 0 && c.count && !c.rects.empty() && c.who != 0) return true;
            return false;
        };
        CHECK(waitFor([&] { return terrain(b) && terrain(host); }) && a.changes.empty(), "Ann's terrain edit is reported to the others, with its place");
        hostEd->placeUnit(0, 0, 640, 640);
        CHECK(waitFor([&] {
                  for (const cb::Change& c : a.changes)
                      if (c.what == 1 && c.who == 0 && !c.rects.empty() && c.rects[0][0] <= 640 && c.rects[0][2] >= 640) return true;
                  return false;
              }),
              "the host's unit is reported to Ann, at its place");
    }
    // The host's connection is lost: all come back to the same room, with their tickets (no new requests).
    {
        const size_t asked = host.requests.size();
        host.drop();
        CHECK(waitFor([&] { return host.s->reconnecting() || a.s->reconnecting(); }, 2), "the host's connection is lost");
        CHECK(waitFor([&] { return !host.s->reconnecting() && !a.s->reconnecting() && !b.s->reconnecting() && host.s->peers().size() == 2 &&
                                   a.s->peers().size() == 2 && b.s->peers().size() == 2; },
                      15),
              "all are back in the session");
        CHECK(host.ended.empty() && a.ended.empty() && b.ended.empty() && host.requests.size() == asked, "with their tickets: no new join requests");
        CHECK(paint(*a.ed, 1000, 1000, 4), "Ann paints after it");
        CHECK(waitFor([&] { return sameTerrain(*a.ed, *hostEd) && sameTerrain(*b.ed, *hostEd); }), "the edit reaches all");
    }
    // Ann's connection is lost; what she paints while it is away goes to the others when she is back.
    {
        a.autoReconnect = false;
        a.drop();
        CHECK(waitFor([&] { return a.s->reconnecting() && host.s->peers().size() == 1; }, 5), "Ann's connection is lost");
        CHECK(paint(*a.ed, 1300, 400, 5), "Ann paints while away");
        const size_t units = hostEd->units().size();
        a.ed->placeUnit(0, 0, 1800, 300);
        CHECK(host.s->newLinks(), "the host makes new links while she is away");
        a.autoReconnect = true, a.retryAt = seconds();
        CHECK(waitFor([&] { return !a.s->reconnecting() && sameTerrain(*a.ed, *hostEd) && sameTerrain(*b.ed, *hostEd); }, 15),
              "she is back (with the old link: the members' secret), and her edit reaches all");
        CHECK(waitFor([&] { return hostEd->units().size() == units + 1 && sameMap(*a.ed, *hostEd) && sameMap(*b.ed, *hostEd); }),
              "the unit she placed while away reaches all, once");
        CHECK(a.ended.empty() && named(host, 1, "Ann"), "as Ann");
        for (const cb::Peer& p : host.s->peers())
            if (p.name != "Ann") bobId = p.id;
    }

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

    // Handing the session over: Ann hosts it, Bob comes back to her with his ticket.
    {
        settle(0.3);
        host.ended.clear(), host.requests.clear(), a.requests.clear();
        host.s = cb::Session::host(*hostEd, server, "Host", host, host.events(), err);
        CHECK(host.s != nullptr, "a new session");
        if (!host.s) return 1;
        host.s->setClock(seconds), host.connect(cb::relayUrl(server));
        CHECK(waitFor([&] { return !host.s->link(cb::Role::Edit).empty(); }), "its links");
        CHECK(joinAs(a, host.s->link(cb::Role::Edit), "Ann") && joinAs(b, host.s->link(cb::Role::View), "Bob"), "two join it");
        CHECK(waitFor([&] { return host.requests.size() == 2; }), "two requests");
        for (const cb::Peer& r : host.requests) host.s->approve(r.id, true);
        CHECK(waitFor([&] { return a.ed && b.ed && a.s->editor() && b.s->editor() && a.s->peers().size() == 2 && b.s->peers().size() == 2; }), "both are in");
        uint32_t annId = 0, viewer = 0;
        for (const cb::Peer& p : host.s->peers()) {
            if (p.name == "Ann") annId = p.id;
            if (p.name == "Bob") viewer = p.id;
        }
        CHECK(host.s->newLinks(), "new links before the handover (Bob comes back with the members' secret)");
        settle(0.2);
        CHECK(!host.s->handOver(viewer), "a viewer cannot get the session");
        CHECK(host.s->handOver(annId), "the host hands the session to Ann");
        CHECK(waitFor([&] { return !host.ended.empty() && a.s->isHost() && !a.s->reconnecting() && !b.s->reconnecting() && a.s->peers().size() == 1 &&
                                   b.s->peers().size() == 1; },
                      15),
              "Ann hosts the session now; Bob is back");
        CHECK(host.ended.find("handed") != std::string::npos && a.requests.empty() && b.ended.empty(), "with his ticket: no request; the old host left");
        CHECK(!a.s->link(cb::Role::Edit).empty(), "Ann has the links");
        CHECK(paint(*a.ed, 500, 1500, 4), "Ann paints as the host");
        CHECK(waitFor([&] { return sameTerrain(*a.ed, *b.ed); }), "Bob gets it");
        a.s->stop();
        CHECK(waitFor([&] { return !b.ended.empty(); }), "Ann ends it");
    }

    for (Program* p : gAll)
        if (p->ws) p->ws->close();
    std::printf(gFails ? "%d FAILED\n" : "all passed\n", gFails);
    return gFails ? 1 : 0;
}
