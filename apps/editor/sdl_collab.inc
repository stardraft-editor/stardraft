// File > Collaborative editing (a preview): the open map's terrain, units, doodads, sprites and locations edited live by
// several people. The
// host invites with a link (edit or view), accepts each request, and can change roles or remove people in the session
// window; the others join with the link. include/stardraft/collab.h has the model; tools/collab-relay the server. One
// session at a time. The socket's events go to the UI thread (shellPost); a timer reads this program's edits and
// cursor every 50 ms. A participant (not the host) has the Map workspace only, and no Map layers.

void collabFollowToggle(uint32_t id);  // (below: the top bar's circles, the pop-out)

namespace collabui {

namespace cb = stardraft::collab;

constexpr const wchar_t* kDefaultServer = L"https://collab.stardraft.app";  // (tools/collab-relay/worker)

struct Conn final : cb::Outbox {
    std::mutex m;
    std::deque<std::function<void()>> q;
    std::unique_ptr<stardraft::platform::WebSocket> ws;  // (last: it goes first, before the queue its threads write to)
    ~Conn() override {
        if (ws) ws->close();  // (its threads end here, while the queue is still there)
    }
    void text(const std::string& json) override {
        if (ws) ws->sendText(json);
    }
    void binary(Bytes b) override {
        if (ws) ws->sendBinary(std::move(b));
    }
    void close() override {
        if (ws) ws->close();
    }
};

std::unique_ptr<Conn> gConn;
std::unique_ptr<cb::Session> gSession;
bool gPumping = false, gEnded = false, gShowWindow = false;
std::wstring gEndWhy;
int gTimer = 0;
int gSharedKey = 0;  // the tab (Doc::key) of the session's map
draw::View* gWindowView = nullptr;

double clockNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::wstring savedName() {
    std::wstring name;
    prefs::text(L"CollabName", name);
    return name;
}
std::wstring savedServer() {
    std::wstring s;
    if (!prefs::text(L"CollabServer", s) || s.empty()) s = kDefaultServer;
    return s;
}

// The user's name in sessions (Program options > Collaboration): asked only when there is none. False: Cancel or no name.
bool askName() {
    std::wstring name = savedName();
    if (!name.empty()) return true;
    if (!shellPromptText(shellTr(L"Display name in the session"), name)) return false;
    while (!name.empty() && name.back() == L' ') name.pop_back();
    while (!name.empty() && name.front() == L' ') name.erase(name.begin());
    if (name.empty()) return false;
    prefs::setText(L"CollabName", name.substr(0, 32));
    return true;
}

// The session's map is still open in its tab (the same editor: an editor at the same address in another tab is not it).
bool editorAlive(const Editor* e) {
    if (!e || !gSharedKey) return false;
    for (size_t i = 0; i < gDocs.size(); ++i)
        if (gDocs[i].key == gSharedKey) return (int(i) == gActive ? gState.ed.get() : gDocs[i].ed.get()) == e;
    return false;
}
// The tab of the session's map is gone: the session ends (before anything uses its editor).
void checkEditor() {
    if (gSession && !gEnded && gSession->editor() && !editorAlive(gSession->editor())) gSession->editorClosed();
}
bool guestHere() {
    return gSession && !gEnded && !gSession->isHost() && gSession->editor() && gSession->editor() == gState.ed.get();
}

void finish();
void pump();
void tickTimer();
void awarenessReset();
void closePopover();
void joinProgressChanged();
void showJoinProgress();
void awarenessTick();
void chatShownCheck();

// Cursor chat (as Figma's): / on the map opens a bubble at the mouse, the text goes to the others as it is typed, Enter
// keeps it for kChatKeep seconds, Esc removes it at once.
bool gChatOpen = false;
std::string gChatText;
bool gChatSkipSlash = false;  // (the next text is the / that opened the chat)
double gChatUntil = 0;  // (Enter: the text goes at this time)
constexpr double kChatKeep = 5;
constexpr size_t kChatMax = 100;  // bytes (collab.cpp kMaxChat)
void chatSend() {
    if (gSession && !gEnded) gSession->chat(gChatText);
}
void chatClose(bool clear) {
    if (gChatOpen && gRoot)
        if (draw::Host* h = gRoot->map.host()) h->textInput(gRoot->map, false, {});
    gChatOpen = false;
    if (clear) gChatText.clear(), chatSend();
    invalidateMapView();
}

void armTimer() {
    if (gTimer || !gHost || !gSession) return;
    gTimer = gHost->timer(50, [] {
        gTimer = 0;
        tickTimer();
        armTimer();
    });
}

// The session ended (it said why): the socket and the session go after the event that ended it.
void finish() {
    if (!gEnded) return;
    gEnded = false;
    // (a guest that had the map keeps it: the end says so)
    if (gSession && !gSession->isHost() && gSession->editor() && editorAlive(gSession->editor()))
        gEndWhy += shellTr(L" You keep your copy of the map. Save it to keep the changes.");
    gSession.reset();
    gConn.reset();  // (its socket's threads end first)
    gSharedKey = 0;
    awarenessReset();
    closePopover();
    chatShownCheck();
    joinProgressChanged();
    invalidateMapView();
    shellMenusChanged();
    shellStatusChanged();
    if (gRoot) gRoot->tabs.invalidate();
    setMessage(gEndWhy);
    // (an end that this user did not start: a message box too, so it is not missed)
    if (gEndWhy.rfind(L"You ", 0) != 0 && gRoot) msgbox::ask(*gRoot, shellTr(L"Collaborative editing"), gEndWhy, {L"OK"}, msgbox::Icon::Info);
}

void changed() {
    invalidateMapView();
    shellMenusChanged();
    shellStatusChanged();
    if (gWindowView) gWindowView->invalidate();
    if (gRoot) gRoot->tabs.invalidate();  // (the tab's session dot)
    joinProgressChanged();
}

void pump() {
    if (gPumping || !gConn) return;  // (a message box of the session's: its events wait for it)
    gPumping = true;
    checkEditor();
    Editor* shared = gSession ? gSession->editor() : nullptr;
    const uint64_t rev = shared ? shared->revision() : 0;
    std::deque<std::function<void()>> now;
    {
        std::lock_guard<std::mutex> lock(gConn->m);
        now.swap(gConn->q);
    }
    for (auto& f : now)
        if (gSession && !gEnded) f();
    if (gSession && !gEnded) gSession->tick();
    gPumping = false;
    if (shared && gSession && gSession->editor() == shared && editorAlive(shared) && shared->revision() != rev) {
        gState.dirty = true;  // (others' edits: the map is drawn again)
        invalidateMapView();
    }
    if (gEnded) finish();
}

void tickTimer() {
    chatShownCheck();
    if (!gSession || gEnded) return chatClose(true);
    checkEditor();
    if (!gChatOpen && !gChatText.empty() && clockNow() > gChatUntil) chatClose(true);
    if (gChatOpen && (!gState.mouseIn || gWorkspace != Workspace::Map || gSession->editor() != gState.ed.get())) chatClose(true);
    Editor* ed = gSession && !gEnded ? gSession->editor() : nullptr;
    if (ed && gState.ed.get() == ed && gWorkspace == Workspace::Map && gState.mouseIn && gState.zoom > 0) {
        const float wx = gState.camX + float(gState.mouseX) / gState.zoom, wy = gState.camY + float(gState.mouseY) / gState.zoom;
        const bool in = wx >= 0 && wy >= 0 && wx < ed->width() * 32.0f && wy < ed->height() * 32.0f;
        gSession->cursor(int(wx), int(wy), in);
    } else if (gSession && !gEnded) {
        gSession->cursor(0, 0, false);
    }
    awarenessTick();
    // A participant edits the map only (the preview): the Map workspace, not Map layers.
    if (guestHere() && !gState.ed->inStroke()) {
        if (gWorkspace != Workspace::Map) setWorkspace(Workspace::Map), setMessage(shellTr(L"Collaborative editing (experimental): invited users edit the map only"));
        if (gState.mode == Mode::Layers) setMode(Mode::Terrain);
    }
    pump();
}

// Each socket has a number: the events of one that a reconnect replaced do not reach the session.
std::atomic<int> gConnGen{0};
void post(int gen, std::function<void()> f) {
    if (!gConn || gen != gConnGen.load()) return;
    {
        std::lock_guard<std::mutex> lock(gConn->m);
        gConn->q.push_back([gen, f = std::move(f)] {
            if (gen == gConnGen.load()) f();
        });
    }
    shellPost(pump);
}

void connect(const std::string& url) {
    stardraft::platform::WebSocket::Handlers h;
    const int g = ++gConnGen;
    h.opened = [g] { post(g, [] { gSession->opened(); }); };
    h.text = [g](std::string t) { post(g, [t] { gSession->received(t); }); };
    h.binary = [g](std::vector<uint8_t> b) { post(g, [b] { gSession->received(Bytes(b.begin(), b.end())); }); };
    h.closed = [g](std::string why) { post(g, [why] { gSession->closed(why); }); };
    gConn->ws = stardraft::platform::WebSocket::connect(url, h);
    armTimer();
}

// The connection was lost: tries again after 1, 2, 4, 8, 8, ... seconds, for Session::kReconnectFor; then the session ends.
double gReconnectSince = 0;
int gReconnectTries = 0;
bool gReconnectTimer = false;  // (one try at a time)
void reconnectLater() {
    if (!gSession || gEnded || !gHost || gReconnectTimer) return;
    if (!gReconnectSince) gReconnectSince = clockNow(), gReconnectTries = 0;
    if (clockNow() - gReconnectSince > cb::Session::kReconnectFor) {
        gSession->reconnectFailed();
        return shellPost(pump);
    }
    const int ms = 1000 * std::min(8, 1 << std::min(gReconnectTries, 3));
    ++gReconnectTries;
    gReconnectTimer = true;
    gHost->timer(ms, [] {
        gReconnectTimer = false;
        if (!gSession || gEnded || !gSession->reconnecting() || !gConn) return;
        ++gConnGen;  // (the old socket's last events are not this connection's)
        gConn->ws.reset();
        {
            std::lock_guard<std::mutex> lock(gConn->m);
            gConn->q.clear();
        }
        connect(gSession->reconnectUrl());
    });
}

std::wstring roleText(cb::Role r) { return r == cb::Role::Edit ? shellTr(L"edit") : shellTr(L"view"); }

void showWindow();
void closePopover();
void addChange(const cb::Change& c);
void addPing(uint32_t who, int x, int y);
void addChatLine(uint32_t who, const std::string& text);
void chatShownCheck();
bool gAutoAccept = false;  // host: people with a link come in without the question (the session window; this session only)

cb::Session::Events events() {
    cb::Session::Events ev;
    ev.status = [](const std::string& t) {
        setMessage(widen(t));
        changed();
    };
    ev.ended = [](const std::string& why) {
        gEnded = true;
        gEndWhy = widen(why);
    };
    ev.peersChanged = [] { changed(); };
    ev.connection = [](bool lost) {
        if (lost) reconnectLater();
        else gReconnectSince = 0, gReconnectTries = 0;
        changed();
    };
    ev.changed = [](const cb::Change& c) { addChange(c); };
    ev.ping = [](uint32_t who, int x, int y) { addPing(who, x, y); };
    ev.said = [](uint32_t who, const std::string& text) { addChatLine(who, text); };
    ev.becameHost = [] {
        ++gConnGen;  // (the member's socket: its close is not the new host's)
        gAutoAccept = false;
        changed();
    };
    ev.linksChanged = [] {
        changed();
        if (gShowWindow) gShowWindow = false, shellPost(showWindow);
    };
    ev.joinRequest = [](const cb::Peer& p) {
        const uint32_t id = p.id;
        if (gAutoAccept) {  // (the host lets people with a link in without asking: the session window's option)
            shellPost([id] {
                if (gSession && !gEnded) gSession->approve(id, true);
                shellPost(pump);
            });
            return;
        }
        const std::wstring text = widen(p.name) + (p.role == cb::Role::Edit ? shellTr(L" requests edit access.") : shellTr(L" requests view access.")) +
                                  L"\n\n" + shellTr(L"Verification code: ") + widen(p.code) + L"\n" +
                                  shellTr(L"The same code shows on their computer. Compare the codes if you do not know who sent the request.");
        shellPost([id, text] {
            if (!gSession || !gRoot) return;
            const int k = msgbox::ask(*gRoot, shellTr(L"Join request"), text, {shellTr(L"Accept"), shellTr(L"Decline")}, msgbox::Icon::Question);
            if (gSession && !gEnded) gSession->approve(id, k == 0);
            shellPost(pump);
        });
    };
    ev.welcome = [](const cb::Terrain& t, const std::string& hostName, cb::Role) {
        auto ed = std::make_unique<Editor>(gState.data);
        std::string err;
        if (!ed->newMap(t.width, t.height, uint16_t(t.era & 7), 1, err)) return false;
        if (!t.hasTile) ed->editTerrainSections([](Chk& c) { c.remove("TILE"); });
        Editor* raw = ed.get();
        addDocument(std::move(ed), false);
        gSharedKey = activeDocKey();
        gSession->setEditor(raw);
        if (!gSession || !gSession->editor()) return false;
        fitMap();
        updateTitle();
        setWorkspace(Workspace::Map);
        setMode(Mode::Terrain);
        setMessage(shellTr(L"Joined the session of ") + widen(hostName));
        return true;
    };
    return ev;
}

// --- Commands ------------------------------------------------------------------------------------------------------------

void copyLink(cb::Role r) {
    if (!gSession) return;
    const std::string link = gSession->link(r);
    if (link.empty()) return setMessage(shellTr(L"The invite links are not ready yet"));
    shellSetClipboardText(link);
    setMessage(r == cb::Role::Edit ? shellTr(L"Copied the edit invite link") : shellTr(L"Copied the view invite link"));
}

void newLinks() {
    if (!gSession || !gRoot) return;
    if (msgbox::ask(*gRoot, shellTr(L"Reset invite links"), shellTr(L"Reset the invite links? The old links stop working. The people in the session stay."),
                    {shellTr(L"Make new links"), shellTr(L"Cancel")}, msgbox::Icon::Question) == 0 &&
        gSession && !gEnded && gSession->newLinks())
        setMessage(shellTr(L"Reset the invite links"));
}

void endSession() {
    if (!gSession || gEnded) return;
    if (gSession->isHost() && gRoot) {
        size_t in = 0;
        for (const cb::Peer& p : gSession->peers()) in += p.approved;
        if (in && msgbox::ask(*gRoot, shellTr(L"End session"), shellTr(L"End the session for all participants?"), {shellTr(L"End session"), shellTr(L"Cancel")},
                              msgbox::Icon::Question) != 0)
            return;
    }
    if (gSession && !gEnded) gSession->stop();
    if (gEnded) finish();
}

// Invite: the open map's session starts (with the server), then the session window shows the links.
void invite() {
    if (gSession) return showWindow();
    if (!docOpen() || !gState.ed) return;
    if (!stardraft::platform::webSocketsAvailable())
        return shellErrorBox(shellTr(L"Collaborative editing is not available on this system yet."), shellTr(L"Could not start the session"));
    if (!askName()) return;
    const std::wstring server = savedServer();
    std::string err, normalized;
    if (!cb::normalizeServer(narrow(server), normalized, err)) return shellErrorBox(widen(err), shellTr(L"Could not start the session"));
    auto conn = std::make_unique<Conn>();
    auto s = cb::Session::host(*gState.ed, normalized, narrow(savedName()), *conn, events(), err);
    if (!s) return shellErrorBox(widen(err), shellTr(L"Could not start the session"));
    s->setClock(clockNow);
    gConn = std::move(conn);
    gSession = std::move(s);
    gSharedKey = activeDocKey();
    gShowWindow = true;
    setMessage(shellTr(L"Connecting to ") + widen(normalized) + L"\x2026");
    connect(cb::relayUrl(normalized));
    changed();
}

// An invite link (pasted, or from the stardraft:// handler): the join starts at once (the host accepts it or not).
void join(const std::string& text) {
    cb::InviteLink link;
    std::string err;
    if (!cb::parseInviteLink(text, link, err)) return shellErrorBox(widen(err), shellTr(L"Could not join the session"));
    if (gSession)
        return shellErrorBox(gSession->isHost() ? shellTr(L"This program hosts a session, so it cannot join one. An invite link opens the StarDraft that runs "
                                                          L"first. To try a session on one computer, paste the link in the other program: File > "
                                                          L"Collaborative editing > Join session.")
                                                : shellTr(L"A session is open. Leave it first."),
                             shellTr(L"Could not join the session"));
    if (!stardraft::platform::webSocketsAvailable())
        return shellErrorBox(shellTr(L"Collaborative editing is not available on this system yet."), shellTr(L"Could not join the session"));
    if (!shellMapsReady())
        return shellErrorBox(shellTr(L"No StarCraft game files found. Select the StarCraft folder on the start page (Browse)."), shellTr(L"Could not join the session"));
    if (!gRoot) return;
    if (!askName()) return;
    auto conn = std::make_unique<Conn>();
    auto s = cb::Session::join(link, narrow(savedName()), narrow(shellVersion()), *conn, events());
    s->setClock(clockNow);
    gConn = std::move(conn);
    gSession = std::move(s);
    setMessage(shellTr(L"Connecting to ") + widen(link.server) + L"\x2026");
    connect(cb::relayUrl(link.server, link.room));
    changed();
    shellPost(showJoinProgress);
}

// --- Joining: the steps, until the map opens ------------------------------------------------------------------------------
// A box with the steps of a join (connect, the host's answer with the safety code, the map) and Cancel. It closes when
// the map opens or the join ends.
struct JoinView final : draw::View {
    void paint(draw::Canvas& c) override {
        const int w = width(), rowH = scale(26);
        c.fill({0, 0, w, height()}, kBg);
        if (!gSession) return;
        const std::string code = gSession->safetyCode();
        const bool map = gSession->editor() != nullptr, keyed = !code.empty();
        const int step = map ? 3 : keyed ? 1 : 0;
        const std::wstring names[3] = {shellTr(L"Connect to the server"), shellTr(L"Wait for the host to let you in"), shellTr(L"Get the map")};
        int y = 0;
        for (int i = 0; i < 3; ++i) {
            const bool done = i < step, now = i == step;
            const draw::Rect mark{0, y + scale(6), scale(14), y + scale(20)};
            if (done) c.ellipse(mark, kGood, kGood);
            else if (now) c.ring(mark, kAccentBright, std::max(1, scale(2)));
            else c.ring(mark, kBorder);
            c.text({scale(24), y, w, y + rowH}, names[i], done ? kDim : now ? kText : kFaint, draw::kMiddle);
            y += rowH;
        }
        if (keyed && !map) {
            const std::wstring t = shellTr(L"Safety code: ") + widen(code) + L"\n" + shellTr(L"The host sees the same code. If the codes are not the same, cancel.");
            c.text({0, y + scale(8), w, height()}, t, kDim, draw::kWrap);
        }
    }
};
JoinView gJoinView;
stardraft::kit::Dialog* gJoinDlg = nullptr;

void showJoinProgress() {
    if (!gSession || gEnded || gSession->editor() || !gRoot || gJoinDlg) return;
    stardraft::kit::Dialog dlg;
    dlg.setContent(gJoinView);
    dlg.setButtons({{shellTr(L"Cancel"), 1, false, true}});
    gJoinDlg = &dlg;
    const int r = dlg.run(*gRoot, shellTr(L"Joining the session"), draw::Point{scale(420), scale(170)});
    gJoinDlg = nullptr;
    // (Cancel before the map came; a host that does not wait, a snapshot's, returns at once: no answer then)
    if (r == 1 && dlg.closed() && gSession && !gEnded && !gSession->editor()) {
        gSession->stop();
        shellPost(pump);
    }
}
// (changed(): the steps again; the map came or the join ended: the box closes)
void joinProgressChanged() {
    if (!gJoinDlg) return;
    gJoinView.invalidate();
    if (!gSession || gEnded || gSession->editor()) gJoinDlg->close(0);
}

void joinPrompt() {
    std::wstring text = widen(shellClipboardText());
    if (text.find(L"/j/") == std::wstring::npos || text.size() > 400) text.clear();  // (a link on the clipboard: there at first)
    if (!shellPromptText(shellTr(L"Invite link"), text) || text.empty()) return;
    join(narrow(text));
}

void serverAddress() { showOptionsTab(L"Collaboration"), showOptionsSdl(); }

// --- The session window ------------------------------------------------------------------------------------------------
// The session's state, the invite links (host), the participants (host: their role and Remove), End or Leave. It is
// drawn again as the session changes.
constexpr uint32_t kColors[8] = {0x3FD45A, 0x3D8BE8, 0xE8A33D, 0xC24BD6, 0xE85A3D, 0x1FB5C2, 0xD6C21F, 0x8C6CF0};
draw::Color personColor(const cb::Peer& p) {
    const uint32_t rgb = kColors[(p.id == 0 ? 0 : p.color) & 7];
    return draw::rgb(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb));
}

// --- Awareness: pings, the others' edits (a flash and the activity list), following, the others' selections ----------------
const cb::Peer* findPeer(uint32_t id) {
    if (!gSession) return nullptr;
    for (const cb::Peer& p : gSession->peers())
        if (p.id == id) return &p;
    return nullptr;
}
draw::Color darker(draw::Color c) { return draw::rgb(uint8_t(GetRValue(c) / 3), uint8_t(GetGValue(c) / 3), uint8_t(GetBValue(c) / 3)); }
std::wstring initialOf(const std::string& name) {
    const std::wstring w = widen(name);
    return w.empty() ? L"?" : std::wstring(1, wchar_t(std::towupper(w[0])));
}

struct PingMark {
    uint32_t who = 0;
    int x = 0, y = 0;
    double at = 0;
};
std::vector<PingMark> gPings;
constexpr double kPingFor = 4;
struct Flash {
    std::vector<std::array<int, 4>> rects;
    draw::Color color = 0;
    double at = 0;
};
std::vector<Flash> gFlashes;
constexpr double kFlashFor = 1.2;
struct Activity {
    uint32_t who = 0;
    std::wstring name;
    draw::Color color = 0;
    uint8_t what = 0;
    uint32_t count = 0;
    int x = 0, y = 0;  // where (map pixels)
    double at = 0;
};
std::deque<Activity> gActivity;  // the newest first
int64_t gFollow = -1;            // the person whose view this user follows (-1: none)
float gFollowCamX = -1e9f, gFollowCamY = -1e9f, gFollowZoom = -1, gFollowCx = -1, gFollowCy = -1;
struct Chip {  // an arrow at the map's edge to a person or a ping out of the view (a click goes there)
    draw::Rect r;
    int x = 0, y = 0;
};
std::vector<Chip> gChips;
std::string gSoftLockSaid;
bool gMinimapOthers = true;  // View > Collaborators on the minimap (prefs CollabMinimap)

void minimapChanged() {
    if (gRoot) gRoot->minimap.invalidate();
}
// The session ended: all of it goes.
void awarenessReset() {
    gPings.clear(), gFlashes.clear(), gActivity.clear(), gChips.clear(), gSoftLockSaid.clear();
    gFollow = -1, gAutoAccept = false, gReconnectSince = 0, gReconnectTries = 0;
    minimapChanged();
}

// The text of an activity: "changed 24 tiles", "changed a unit".
std::wstring activityText(const Activity& a) {
    static const wchar_t* const kOne[] = {L"a tile", L"a unit", L"a doodad", L"a sprite", L"a location"};
    static const wchar_t* const kMany[] = {L" tiles", L" units", L" doodads", L" sprites", L" locations"};
    const int k = std::min<int>(a.what, 4);
    return a.count == 1 ? shellTr(L"changed ") + shellTr(kOne[k]) : shellTr(L"changed ") + std::to_wstring(a.count) + shellTr(kMany[k]);
}

void addChange(const cb::Change& c) {
    const cb::Peer* p = findPeer(c.who);
    if (!p) return;
    const double now = clockNow();
    const draw::Color col = personColor(*p);
    if (!c.rects.empty()) {
        gFlashes.push_back({c.rects, col, now});
        if (gFlashes.size() > 64) gFlashes.erase(gFlashes.begin());
        invalidateMapView();
    }
    int x = 0, y = 0;
    if (!c.rects.empty()) x = (c.rects[0][0] + c.rects[0][2]) / 2, y = (c.rects[0][1] + c.rects[0][3]) / 2;
    // (one person's edits of one kind in a row are one line: a stroke sends many)
    if (!gActivity.empty() && gActivity.front().who == c.who && gActivity.front().what == c.what && now - gActivity.front().at < 5) {
        Activity& a = gActivity.front();
        a.count += c.count, a.at = now, a.name = widen(p->name), a.color = col;
        if (!c.rects.empty()) a.x = x, a.y = y;
    } else {
        gActivity.push_front({c.who, widen(p->name), col, c.what, c.count, x, y, now});
        if (gActivity.size() > 30) gActivity.pop_back();
    }
    if (gWindowView) gWindowView->invalidate();
}

void addPing(uint32_t who, int x, int y) {
    gPings.push_back({who, x, y, clockNow()});
    if (gPings.size() > 16) gPings.erase(gPings.begin());
    invalidateMapView();
    minimapChanged();
}

// The camera to a map point (its middle there), as a jump; the zoom stays.
void centerOn(float x, float y) {
    int vw = 0, vh = 0;
    shellViewSize(vw, vh);
    if (!gState.ed || gState.zoom <= 0 || vw <= 0) return;
    mapViewGlideTo(gState, gState.zoom, x, y);
    gState.dirty = true;
    invalidateMapView();
    minimapChanged();
}

void follow(int64_t id) {
    gFollow = id;
    gFollowCamX = gFollowCamY = -1e9f, gFollowZoom = -1, gFollowCx = gFollowCy = -1;
    const cb::Peer* p = id >= 0 ? findPeer(uint32_t(id)) : nullptr;
    setMessage(p ? shellTr(L"Following ") + widen(p->name) + shellTr(L". Move the map to stop.") : shellTr(L"Stopped following"));
    changed();
}

// Each timer tick: this user's presence (view, tool, selection); following; a note when another person has the same
// unit or location selected.
void awarenessTick() {
    Editor* ed = gSession && !gEnded ? gSession->editor() : nullptr;
    if (!ed) return;
    const double now = clockNow();
    if (std::erase_if(gPings, [&](const PingMark& m) { return now - m.at > kPingFor; })) invalidateMapView(), minimapChanged();
    std::erase_if(gFlashes, [&](const Flash& f) { return now - f.at > kFlashFor; });
    if (gState.ed.get() != ed || gWorkspace != Workspace::Map) return;
    int vw = 0, vh = 0;
    shellViewSize(vw, vh);
    cb::Presence pr;
    if (gState.zoom > 0 && vw > 0 && vh > 0)
        pr.viewX = int(gState.camX), pr.viewY = int(gState.camY), pr.viewW = int(float(vw) / gState.zoom), pr.viewH = int(float(vh) / gState.zoom),
        pr.zoom = uint16_t(std::clamp<long>(std::lround(gState.zoom * 256), 1, 65535));
    pr.follows = gFollow >= 0 ? uint32_t(gFollow) : cb::kNobody;
    if (gState.mode == Mode::Terrain && gState.hasTerrainSel && gState.terrainSel.right > gState.terrainSel.left && gState.terrainSel.bottom > gState.terrainSel.top)
        pr.selLeft = gState.terrainSel.left, pr.selTop = gState.terrainSel.top, pr.selRight = gState.terrainSel.right, pr.selBottom = gState.terrainSel.bottom;
    switch (gState.mode) {
    case Mode::Terrain: pr.tool = cb::Tool::Terrain; break;
    case Mode::Units: pr.tool = cb::Tool::Units; break;
    case Mode::Doodads: pr.tool = cb::Tool::Doodads; break;
    case Mode::Sprites: pr.tool = cb::Tool::Sprites; break;
    case Mode::Locations: pr.tool = cb::Tool::Locations; break;
    default: break;
    }
    if (pr.tool == cb::Tool::Terrain && !inSelection()) {
        pr.brushW = uint8_t(std::clamp<long>(std::lround(gState.brushSize), 1, 64));
        pr.brushH = uint8_t(std::clamp<long>(std::lround(brushHeightOf(gState)), 1, 64));
    }
    for (int i : ed->selection())
        if (i >= 0 && size_t(i) < ed->units().size() && pr.units.size() < cb::kMaxSelected) pr.units.push_back(ed->units()[size_t(i)].serial);
    for (int i : ed->selectedLocations())
        if (i >= 0 && pr.locations.size() < cb::kMaxSelected) pr.locations.push_back(uint16_t(i));
    gSession->presence(pr);
    // Following: the camera goes with the other's view (its middle) and zoom; a move or zoom of this user's own stops it.
    // Two people who follow each other stay where they are (each would go after the other).
    if (gFollow >= 0) {
        const cb::Peer* p = findPeer(uint32_t(gFollow));
        if (!p) {
            follow(-1);
        } else if (gFollowCamX > -1e8f &&
                   (std::abs(gState.camX - gFollowCamX) > 2 || std::abs(gState.camY - gFollowCamY) > 2 || gState.zoom != gFollowZoom)) {
            follow(-1);
        } else if (p->presence.follows == gSession->selfId()) {
            gFollowCamX = gState.camX, gFollowCamY = gState.camY, gFollowZoom = gState.zoom;
        } else if (p->presence.viewW > 0 && gState.zoom > 0) {
            const float cx = float(p->presence.viewX + p->presence.viewW / 2), cy = float(p->presence.viewY + p->presence.viewH / 2);
            const float z = p->presence.zoom ? std::clamp(float(p->presence.zoom) / 256.0f, 1.0f / 16, 16.0f) : gState.zoom;
            if (cx != gFollowCx || cy != gFollowCy || z != gState.zoom) {
                gFollowCx = cx, gFollowCy = cy;
                mapZoomGlideStop();
                gState.gameView = false;  // (as a zoom of this user's own)
                gState.zoom = z;
                gState.camX = cx - float(vw) / 2.0f / z, gState.camY = cy - float(vh) / 2.0f / z;
                clampCamera();
                gState.dirty = true;
                invalidateMapView(), minimapChanged();
            }
            gFollowCamX = gState.camX, gFollowCamY = gState.camY, gFollowZoom = gState.zoom;
        }
    }
    // Soft locks: the same unit or location selected by another person: a note (once for each new overlap).
    std::string said;
    std::wstring note;
    for (const cb::Peer& p : gSession->peers()) {
        for (uint32_t s : p.presence.units)
            if (std::find(pr.units.begin(), pr.units.end(), s) != pr.units.end()) {
                said += p.name + "/u" + std::to_string(s) + ";";
                if (note.empty()) note = widen(p.name) + shellTr(L" has this unit selected too");
            }
        for (uint16_t l : p.presence.locations)
            if (std::find(pr.locations.begin(), pr.locations.end(), l) != pr.locations.end()) {
                said += p.name + "/l" + std::to_string(l) + ";";
                if (note.empty()) note = widen(p.name) + shellTr(L" has this location selected too");
            }
    }
    if (said != gSoftLockSaid) {
        if (!said.empty()) setMessage(note);
        gSoftLockSaid = said;
    }
}

// The overlays on the map, under the cursors: the change flashes, the others' selections and brushes, the pings, and
// the arrows at the edge to people and pings out of the view. `view`: the map view (its pixels).
void awarenessPaint(draw::Canvas& c, draw::Rect view) {
    const Editor& ed = *gSession->editor();
    const double now = clockNow();
    const float z = gState.zoom;
    auto sx = [&](float x) { return view.left + int(std::lround((x - gState.camX) * z)); };
    auto sy = [&](float y) { return view.top + int(std::lround((y - gState.camY) * z)); };
    bool animate = false;
    for (const Flash& f : gFlashes) {
        const double t = (now - f.at) / kFlashFor;
        if (t >= 1) continue;
        animate = true;
        const uint8_t a = uint8_t(110 * (1 - t));
        for (const auto& r : f.rects) c.fill({sx(float(r[0])), sy(float(r[1])), sx(float(r[2])), sy(float(r[3]))}, f.color, a);
    }
    const auto old = c.setFont(c.font({draw::Face::Ui, scale(11)}));
    auto tag = [&](int x, int y, const std::wstring& text, draw::Color col) {
        const int w = c.measure(text) + scale(8), h = c.metrics().height + scale(2);
        const draw::Rect r{x, y - h, x + w, y};
        c.roundBox(r, col, darker(col), float(scale(3)));
        c.text(r, text, draw::rgb(16, 16, 16), draw::kCenter | draw::kMiddle);
    };
    for (const cb::Peer& p : gSession->peers()) {
        const draw::Color col = personColor(p);
        // Their selected units: the unit's outline (its box), dashed in their color.
        bool named = false;
        for (uint32_t s : p.presence.units)
            for (const Unit& u : ed.units())
                if (u.serial == s) {
                    const Rect b = ed.unitBox(u);
                    const draw::Rect r{sx(float(b.left)), sy(float(b.top)), sx(float(b.right)), sy(float(b.bottom))};
                    c.frame(r, col, std::max(1, scale(2)), draw::LineStyle::Dash);
                    if (!named) tag(r.left, r.top - scale(1), widen(p.name), col), named = true;
                    break;
                }
        for (uint16_t l : p.presence.locations)
            if (l < ed.locations().size()) {
                const Location& L = ed.locations()[l];
                const float x0 = float(std::min(L.left, L.right)), x1 = float(std::max(L.left, L.right));
                const float y0 = float(std::min(L.top, L.bottom)), y1 = float(std::max(L.top, L.bottom));
                const draw::Rect r{sx(x0), sy(y0), sx(x1), sy(y1)};
                c.frame(r, col, std::max(1, scale(2)), draw::LineStyle::Dash);
                if (!named) tag(r.left, r.top - scale(1), widen(p.name), col), named = true;
            }
        // Their terrain selection: its box, dashed in their color.
        if (p.presence.selRight > p.presence.selLeft && p.presence.selBottom > p.presence.selTop) {
            const draw::Rect r{sx(float(p.presence.selLeft)), sy(float(p.presence.selTop)), sx(float(p.presence.selRight)), sy(float(p.presence.selBottom))};
            c.frame(r, col, std::max(1, scale(2)), draw::LineStyle::Dash);
            if (!named) tag(r.left, r.top - scale(1), widen(p.name), col), named = true;
        }
        // Their terrain brush at their cursor (a diamond is 64 x 32 map pixels).
        if (p.cursorOn && p.presence.tool == cb::Tool::Terrain && p.presence.brushW && now - p.cursorTime < 30) {
            const float w = float(p.presence.brushW) * 32, h = float(p.presence.brushH) * 16;
            const float cx = std::round(float(p.x) / 32) * 32, cy = std::round(float(p.y) / 16) * 16;
            const draw::Point d[4] = {{sx(cx), sy(cy - h)}, {sx(cx + w), sy(cy)}, {sx(cx), sy(cy + h)}, {sx(cx - w), sy(cy)}};
            for (int k = 0; k < 4; ++k) c.line(d[k].x, d[k].y, d[(k + 1) % 4].x, d[(k + 1) % 4].y, col, std::max(1, scale(2)), draw::LineStyle::Dash);
        }
    }
    // Pings: rings that grow, in the color of the person who pinged, with the name.
    for (const PingMark& m : gPings) {
        const double t = now - m.at;
        if (t > kPingFor) continue;
        animate = true;
        const cb::Peer* p = findPeer(m.who);
        const draw::Color col = p ? personColor(*p) : draw::rgb(240, 240, 240);
        const int x = sx(float(m.x)), y = sy(float(m.y));
        for (int k = 0; k < 2; ++k) {
            const double f = std::fmod(t + k * 0.5, 1.0);
            const int rad = scale(8) + int(f * scale(36));
            c.ring({x - rad, y - rad, x + rad, y + rad}, col, std::max(1, scale(3) - int(f * scale(2))));
        }
        c.ellipse({x - scale(5), y - scale(5), x + scale(5), y + scale(5)}, col, darker(col));
        tag(x + scale(8), y - scale(8), p ? widen(p->name) : shellTr(L"You"), col);
    }
    // Arrows at the edge: people (their cursor, else the middle of their view) and pings out of the view.
    gChips.clear();
    const int m = scale(14), cr = scale(11);
    auto chip = [&](float mx, float my, const std::wstring& text, draw::Color col) {
        const int x = sx(mx), y = sy(my);
        if (x >= view.left && x < view.right && y >= view.top && y < view.bottom) return;
        const int cx = std::clamp(x, view.left + m, view.right - m), cy = std::clamp(y, view.top + m, view.bottom - m);
        const draw::Rect r{cx - cr, cy - cr, cx + cr, cy + cr};
        // (a small triangle from the circle toward the place)
        const float dx = float(x - cx), dy = float(y - cy), len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
        const float ux = dx / len, uy = dy / len;
        const draw::Point tri[3] = {{cx + int(ux * (cr + scale(7))), cy + int(uy * (cr + scale(7)))},
                                    {cx + int(-uy * cr * 0.7f + ux * cr * 0.6f), cy + int(ux * cr * 0.7f + uy * cr * 0.6f)},
                                    {cx + int(uy * cr * 0.7f + ux * cr * 0.6f), cy + int(-ux * cr * 0.7f + uy * cr * 0.6f)}};
        c.polygon(tri, 3, col, darker(col));
        c.ellipse(r, col, darker(col));
        c.text(r, text, draw::rgb(16, 16, 16), draw::kCenter | draw::kMiddle);
        gChips.push_back({r, int(mx), int(my)});
    };
    for (const cb::Peer& p : gSession->peers()) {
        if (p.cursorOn && now - p.cursorTime < 30) chip(float(p.x), float(p.y), initialOf(p.name), personColor(p));
        else if (p.presence.viewW > 0)
            chip(float(p.presence.viewX + p.presence.viewW / 2), float(p.presence.viewY + p.presence.viewH / 2), initialOf(p.name), personColor(p));
    }
    for (const PingMark& pm : gPings) {
        const cb::Peer* p = findPeer(pm.who);
        chip(float(pm.x), float(pm.y), L"!", p ? personColor(*p) : draw::rgb(240, 240, 240));
    }
    c.setFont(old);
    if (animate) shellPost([] { invalidateMapView(); });
}

}  // namespace collabui

// A click on the map: an arrow at the edge (to a person or a ping out of the view) moves the view there. True: taken.
bool collabMapClick(int x, int y) {
    using namespace collabui;
    if (!gSession || gEnded || gSession->editor() != gState.ed.get()) return false;  // (the arrows are the shared map's)
    for (const Chip& ch : gChips)
        if (x >= ch.r.left && x < ch.r.right && y >= ch.r.top && y < ch.r.bottom) {
            if (gFollow >= 0) follow(-1);
            centerOn(float(ch.x), float(ch.y));
            return true;
        }
    return false;
}

// A session shows the active map (the map's menu has Ping here).
bool collabPingable() {
    using namespace collabui;
    return gSession && !gEnded && gSession->editor() && gSession->editor() == gState.ed.get() && !gSession->reconnecting();
}
// A ping at a map point (the map's menu, Alt+click on the minimap): the others see it; this user too.
bool collabPing(int x, int y) {
    using namespace collabui;
    if (!collabPingable()) return false;
    gSession->ping(x, y);
    addPing(gSession->isHost() ? 0 : 0xFFFFFFFFu, x, y);
    shellPost(pump);
    return true;
}

// The minimap: the others' views (boxes in their colors; View > Collaborators on the minimap) and the pings.
void collabMinimapPaint(draw::Canvas& c, RECT rr, float sc) {
    using namespace collabui;
    if (!collabPingable() || sc <= 0) return;
    const draw::Rect r = rr;
    auto mx = [&](float x) { return r.left + int(x * sc); };
    auto my = [&](float y) { return r.top + int(y * sc); };
    bool collabMinimapShown();
    if (collabMinimapShown())
        for (const cb::Peer& p : gSession->peers())
            if (p.presence.viewW > 0) {
                const draw::Rect v{mx(float(p.presence.viewX)), my(float(p.presence.viewY)), mx(float(p.presence.viewX + p.presence.viewW)),
                                   my(float(p.presence.viewY + p.presence.viewH))};
                c.fill(v, personColor(p), 30);
                c.frame(v, personColor(p));
            }
    const double now = clockNow();
    bool any = false;
    for (const PingMark& m : gPings) {
        const double t = now - m.at;
        if (t > kPingFor) continue;
        any = true;
        const cb::Peer* p = findPeer(m.who);
        const draw::Color col = p ? personColor(*p) : draw::rgb(240, 240, 240);
        const int x = mx(float(m.x)), y = my(float(m.y)), rad = scale(3) + int(std::fmod(t, 1.0) * scale(8));
        c.ring({x - rad, y - rad, x + rad, y + rad}, col, std::max(1, scale(2)));
        c.ellipse({x - scale(2), y - scale(2), x + scale(2), y + scale(2)}, col, col);
    }
    if (any) shellPost([] { if (gRoot) gRoot->minimap.invalidate(); });
}

namespace collabui {

// The session's pop-out (the top bar's Invite button, File > Collaborative editing > Invite): the state, the invite links
// (host), the people (host: role, Make host, Remove; this user: Rename), what the others changed last (a click goes there),
// and End or Leave. It closes on a click outside it or Esc; the map stays usable. It is drawn again as the session
// changes; a new height opens it again at that height.
bool gPopOpen = false;
int gPopH = 0;  // the height it opened at (the paint measures what it needs)
void closePopover();
void reopenPopover();

std::wstring agoText(double seconds) {
    if (seconds < 5) return shellTr(L"now");
    if (seconds < 60) return std::to_wstring(int(seconds)) + shellTr(L" s ago");
    return std::to_wstring(int(seconds / 60)) + shellTr(L" min ago");
}

void renameMe() {
    std::wstring name = savedName();
    if (!shellPromptText(shellTr(L"Display name in the session"), name)) return;
    while (!name.empty() && name.back() == L' ') name.pop_back();
    while (!name.empty() && name.front() == L' ') name.erase(name.begin());
    if (name.empty()) return;
    prefs::setText(L"CollabName", name.substr(0, 32));
    if (gSession && !gEnded) gSession->rename(narrow(name.substr(0, 32)));
    shellSettingsChanged();
    changed();
}

void makeHost(uint32_t id) {
    const cb::Peer* p = findPeer(id);
    if (!gSession || !gRoot || !p) return;
    const std::wstring name = widen(p->name);
    if (msgbox::ask(*gRoot, shellTr(L"Make host"),
                    shellTr(L"Make ") + name + shellTr(L" the host of the session? You leave the session. You keep your map, and the others stay in the session."),
                    {shellTr(L"Make host"), shellTr(L"Cancel")}, msgbox::Icon::Question) != 0)
        return;
    if (gSession && !gEnded && !gSession->handOver(id)) setMessage(shellTr(L"Could not make ") + name + shellTr(L" the host"));
    shellPost(pump);
}

struct SessionView final : draw::View {
    kitui::Surface surface;
    bool mouse(const draw::MouseEvent& e) override { return surface.mouse(*this, e); }
    // (a button that opens a box closes the pop-out first: `closes`)
    void button(draw::Canvas& c, draw::Rect r, const std::wstring& label, kitpaint::Tone tone, std::function<void()> fn, bool on = true, bool closes = false) {
        kitpaint::drawButtonFace(c, r, label, tone, !on ? kitpaint::ButtonState::Disabled : surface.hot(r) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
        if (on)
            surface.add({r, nullptr,
                         [fn, closes] {
                             if (closes) closePopover();
                             shellPost(fn);
                         },
                         label, draw::Cursor::Hand});
    }
    void paint(draw::Canvas& c) override {
        const int w = width(), pad = scale(16), bh = scale(28), sbh = scale(24);
        c.fill({0, 0, w, height()}, kBg);
        c.frame({0, 0, w, height()}, kBorder);
        surface.begin();
        int y = pad;
        if (!gSession || gEnded) {
            c.text({pad, y, w - pad, y + bh}, shellTr(L"No session"), kDim, draw::kMiddle);
            return;
        }
        const bool host = gSession->isHost();
        size_t in = 0;
        for (const cb::Peer& p : gSession->peers()) in += p.approved;
        auto header = [&](const std::wstring& t) {
            c.text({pad, y, w - pad, y + scale(20)}, t, kDim, draw::kMiddle);
            y += scale(22);
        };
        // The state: green while others are in the session, yellow while the connection comes back.
        const bool lost = gSession->reconnecting(), live = in || (!host && gSession->editor());
        const std::wstring state = lost ? shellTr(L"Reconnecting\x2026")
                                   : host ? (in ? shellTr(L"Live \x00B7 ") + std::to_wstring(in + 1) + shellTr(L" people")
                                                : gSession->link(cb::Role::Edit).empty() ? shellTr(L"Connecting\x2026") : shellTr(L"Waiting for people"))
                                          : (gSession->editor() ? (gSession->role() == cb::Role::Edit ? shellTr(L"Editing ") : shellTr(L"Viewing ")) + widen(gSession->hostName()) + shellTr(L"'s map")
                                                                : shellTr(L"Waiting for the host\x2026"));
        const int sh = std::max(bh, c.measureBox(state, w - 2 * pad - scale(20), draw::kWrap).y + scale(10));
        const draw::Rect stateR{pad, y, w - pad, y + sh};
        kitpaint::drawBox(c, stateR, lost ? kCtl : live ? kGoodFill : kCtl, lost ? kWarn : live ? kGoodLine : kBorder);
        c.text({stateR.left + scale(10), stateR.top + scale(5), stateR.right - scale(10), stateR.bottom - scale(5)}, state, lost ? kWarn : live ? kGood : kText,
               draw::kWrap | draw::kMiddle);
        y += sh + scale(14);
        if (host) {
            header(shellTr(L"Invite"));
            // What each link lets people do (the guest locks: sdlCollabLocks, collabModeLocked).
            const std::wstring note =
                shellTr(L"Editors can change terrain, units, doodads, sprites and locations. Each person keeps a local copy of the map, also "
                        L"after you remove them.");
            const int nh = c.measureBox(note, w - 2 * pad, draw::kWrap).y;
            c.text({pad, y, w - pad, y + nh}, note, kDim, draw::kWrap);
            y += nh + scale(10);
            const bool ready = !gSession->link(cb::Role::Edit).empty();
            const int bw = (w - 2 * pad - 2 * scale(8)) / 3;
            button(c, {pad, y, pad + bw, y + bh}, shellTr(L"Copy edit link"), kitpaint::Tone::Primary, [] { copyLink(cb::Role::Edit); }, ready);
            button(c, {pad + bw + scale(8), y, pad + 2 * bw + scale(8), y + bh}, shellTr(L"Copy view link"), kitpaint::Tone::Normal, [] { copyLink(cb::Role::View); }, ready);
            button(c, {pad + 2 * (bw + scale(8)), y, w - pad, y + bh}, shellTr(L"Reset links\x2026"), kitpaint::Tone::Normal, [] { newLinks(); }, ready, true);
            y += bh + scale(10);
            // Let people in without the question (this session only).
            const draw::Rect box{pad, y + scale(4), pad + scale(16), y + scale(20)};
            const draw::Rect hit{pad, y, w - pad, y + sbh};
            const bool over = surface.hot(hit);
            kitpaint::drawCheckMark(c, box, gAutoAccept, over, true);
            c.text({box.right + scale(8), y, w - pad, y + sbh}, shellTr(L"Let people in without asking"), kText, draw::kMiddle);
            surface.add({hit, nullptr,
                         [] {
                             gAutoAccept = !gAutoAccept;
                             changed();
                         },
                         shellTr(L"Let people in without asking"), draw::Cursor::Hand});
            y += sbh + scale(14);
        }
        header(shellTr(L"People"));
        auto row = [&](const cb::Peer& p, bool self) {
            const draw::Rect r{pad, y, w - pad, y + bh};
            const draw::Color col = personColor(p);
            const draw::Rect dot{r.left, r.top + scale(4), r.left + scale(20), r.top + scale(24)};
            c.ellipse(dot, col, darker(col));
            c.text(dot, initialOf(p.name), draw::rgb(16, 16, 16), draw::kCenter | draw::kMiddle);
            int right = r.right;
            auto rowButton = [&](const std::wstring& label, kitpaint::Tone tone, std::function<void()> fn, bool closes = false) {
                const int bw = c.measure(label) + scale(16);
                button(c, {right - bw, r.top + scale(2), right, r.bottom - scale(2)}, label, tone, std::move(fn), true, closes);
                right -= bw + scale(4);
            };
            if (self) {
                rowButton(shellTr(L"Rename\x2026"), kitpaint::Tone::Normal, [] { renameMe(); }, true);
            } else if (host) {
                const uint32_t id = p.id;
                rowButton(shellTr(L"Remove"), kitpaint::Tone::Danger, [id] {
                    if (gSession && !gEnded) gSession->remove(id);
                    changed();
                });
                if (p.role == cb::Role::Edit) rowButton(shellTr(L"Make host\x2026"), kitpaint::Tone::Normal, [id] { makeHost(id); }, true);
                const cb::Role to = p.role == cb::Role::Edit ? cb::Role::View : cb::Role::Edit;
                rowButton(to == cb::Role::Edit ? shellTr(L"Set edit") : shellTr(L"Set view"), kitpaint::Tone::Normal, [id, to] {
                    if (gSession && !gEnded) gSession->setRole(id, to);
                    changed();
                });
            } else if (gFollow == int64_t(p.id) || p.presence.viewW > 0) {
                const uint32_t id = p.id;
                rowButton(gFollow == int64_t(id) ? shellTr(L"Stop following") : shellTr(L"Follow"), kitpaint::Tone::Normal, [id] { collabFollowToggle(id); });
            }
            const std::wstring label = widen(p.name) + (self ? shellTr(L" (you)") : L"");
            const std::wstring role = p.id == 0 ? shellTr(L"host") : roleText(p.role);
            const int roleW = c.measure(role) + scale(10);
            c.text({dot.right + scale(8), r.top, right - roleW, r.bottom}, label, kText, draw::kMiddle | draw::kEllipsis);
            c.text({right - roleW, r.top, right, r.bottom}, role, kDim, draw::kMiddle);
            y += bh + scale(4);
        };
        cb::Peer me;
        me.id = host ? 0 : 0xFFFFFFFFu, me.name = narrow(savedName()), me.role = gSession->role(), me.color = 0;
        if (host) row(me, true);
        for (const cb::Peer& p : gSession->peers())
            if (p.approved) row(p, false);
        if (!host) row(me, true);
        y += scale(10);
        // What the others changed last (a click: the view goes there).
        header(shellTr(L"Recent changes"));
        const double now = clockNow();
        if (gActivity.empty()) {
            c.text({pad, y, w - pad, y + sbh}, shellTr(L"None"), kFaint, draw::kMiddle);
            y += sbh;
        }
        for (size_t i = 0; i < gActivity.size() && i < 5; ++i) {
            const Activity& a = gActivity[i];
            const draw::Rect r{pad, y, w - pad, y + sbh};
            const bool over = surface.hot(r);
            if (over) kitpaint::drawBox(c, r, kCtlHot, draw::kNone);
            c.ellipse({r.left + scale(4), r.top + scale(8), r.left + scale(12), r.top + scale(16)}, a.color, a.color);
            const std::wstring ago = agoText(now - a.at);
            const int agoW = c.measure(ago) + scale(8);
            c.text({r.left + scale(18), r.top, r.right - agoW, r.bottom}, a.name + L" " + activityText(a), kText, draw::kMiddle | draw::kEllipsis);
            c.text({r.right - agoW, r.top, r.right - scale(4), r.bottom}, ago, kDim, draw::kMiddle | draw::kRight);
            const int ax = a.x, ay = a.y;
            surface.add({r, nullptr,
                         [ax, ay] {
                             closePopover();
                             shellPost([ax, ay] { centerOn(float(ax), float(ay)); });
                         },
                         shellTr(L"Show the place on the map"), draw::Cursor::Hand});
            y += sbh;
        }
        y += pad;
        const draw::Rect endR{pad, y, pad + scale(130), y + bh};
        button(c, endR, host ? shellTr(L"End session") : shellTr(L"Leave session"), kitpaint::Tone::Danger, [] { endSession(); }, true, true);
        // (the display name and the server: Program options > Collaboration)
        button(c, {w - pad - scale(100), y, w - pad, y + bh}, shellTr(L"Options\x2026"), kitpaint::Tone::Normal, [] { serverAddress(); }, true, true);
        y += bh + pad;
        surface.paintFocus(c, *this);
        if (std::abs(y - gPopH) > scale(2)) gPopH = y, shellPost(reopenPopover);  // (another height: open it again at that one)
    }
};
SessionView gSessionView;

// --- Chat: a column right of the Log while a session is open --------------------------------------------------------
// The lines of the session (the newest last), the name and color as they were when the line came.
struct ChatLine {
    std::wstring name, text;
    draw::Color color;
    bool mine = false;
};
std::deque<ChatLine> gChatLines;
constexpr size_t kChatLines = 200;

class ChatPaneView final : public draw::View {
public:
    ChatPaneView() {
        input_.setPlaceholder(shellTr(L"Message"));
        input_.setMaxLength(stardraft::collab::kMaxSay);  // (Send cuts a longer UTF-8 text at kMaxSay bytes)
        input_.onEnter = [this] { send(); };
        input_.onEscape = [this] { input_.setText(L""); };
        input_.onChange = [this] {  // the / that opened the box from the map: out of the text again (where it went)
            if (!slashPending()) return;
            const std::wstring now = input_.text();
            for (size_t i = 0; i < now.size(); ++i)
                if (now[i] == L'/' && now.size() == slashBefore_.size() + 1 && now.substr(0, i) + now.substr(i + 1) == slashBefore_) {
                    slashUntil_ = 0;
                    shellPost([this, before = slashBefore_] { input_.setText(before); });
                    return;
                }
        };
        add(input_, draw::Rect(0, 0, 0, 0));
    }
    ~ChatPaneView() override { remove(input_); }
    void changed() { invalidate(); }
    // The keyboard goes to the message box (the Log opens first: the box is in the open column). slash: the / key on
    // the map opened it (its text, which comes after the key, does not go in the box).
    void focusInput(bool slash = false) {
        if (slash) slashBefore_ = input_.text(), slashUntil_ = clockNow() + 0.5;
        if (!gLogPane.expanded()) gLogPane.setExpanded(true);
        input_.setVisible(true);
        input_.focus();
    }
    bool slashPending() const { return clockNow() < slashUntil_; }
    void slashTaken() { slashUntil_ = 0; }
    // A box that hides gives the keyboard back to the map (else keys would go to a box that cannot be seen).
    void giveBackFocus() {
        if (input_.hasFocus() && gRoot) gRoot->map.focus();
    }
    void resized() override {
        const int bar = scale(24), pad = scale(6), ih = scale(26);
        const bool open = height() > bar + ih + scale(8);
        if (!open) giveBackFocus();
        input_.setVisible(open);
        input_.setFrame(draw::Rect(pad, std::max(bar, height() - ih - scale(4)), std::max(pad, width() - pad), std::max(bar, height() - scale(4))));
    }
    void paint(draw::Canvas& c) override {
        const int w = width(), h = height(), bar = scale(24), pad = scale(8);
        c.fill({0, 0, w, h}, kBg);
        c.fill({0, 0, w, bar}, kPanel);
        c.fill({0, 0, w, 1}, kBorder);
        c.fill({0, 0, 1, h}, kBorder);  // (the line to the Log)
        c.text({pad, 0, pad + scale(50), bar}, shellTr(L"Chat"), kDim, draw::kMiddle);
        const bool open = input_.visible();
        if (!open) {  // closed: the latest line in the bar
            if (!gChatLines.empty()) {
                const ChatLine& l = gChatLines.back();
                const int nw = c.measure(l.name + L": ");
                c.text({pad + scale(50), 0, pad + scale(50) + nw, bar}, l.name + L":", l.color, draw::kMiddle);
                c.text({pad + scale(50) + nw, 0, w - pad, bar}, l.text, kText, draw::kMiddle | draw::kEllipsis);
            }
            return;
        }
        // Open: the lines from the newest (at the bottom, over the message box) up, as many as fit.
        const int top = bar + scale(4), bottom = input_.frame().top - scale(4);
        if (gChatLines.empty()) {
            c.text({pad, top, w - pad, top + scale(20)}, shellTr(L"No messages yet. Write to the others in the session here."), kFaint, draw::kWrap);
            return;
        }
        c.pushClip({0, top, w, bottom});
        int y = bottom;
        for (size_t i = gChatLines.size(); i-- > 0 && y > top;) {
            const ChatLine& l = gChatLines[i];
            const std::wstring who = l.name + L": ";
            const int nw = c.measure(who);
            const int tw = std::max(scale(40), w - 2 * pad - nw);
            const int th = std::max(c.metrics().height, c.measureBox(l.text, tw, draw::kWrap).y);
            y -= th + scale(3);
            c.text({pad, y, pad + nw, y + c.metrics().height}, who, l.color, 0);
            c.text({pad + nw, y, pad + nw + tw, y + th}, l.text, kText, draw::kWrap);
        }
        c.popClip();
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Down && e.button == 0 && e.pt.y < scale(24)) {  // the bar: open or close (with the Log)
            if (gLogPane.expanded()) giveBackFocus(), gLogPane.setExpanded(false);
            else focusInput();
            return true;
        }
        return e.kind == draw::MouseKind::Down;
    }
    draw::Cursor cursor(draw::Point p) override { return p.y < scale(24) ? draw::Cursor::Hand : draw::Cursor::Arrow; }

private:
    void send() {
        std::wstring t = input_.text();
        while (!t.empty() && iswspace(t.back())) t.pop_back();
        while (!t.empty() && iswspace(t.front())) t.erase(t.begin());
        if (t.empty() || !gSession || gEnded) return;
        std::string said = narrow(t);  // (at most kMaxSay bytes, a whole character at the end: as the others get it)
        if (said.size() > stardraft::collab::kMaxSay) {
            size_t n = stardraft::collab::kMaxSay;
            while (n > 0 && (uint8_t(said[n]) & 0xC0) == 0x80) --n;
            said.resize(n);
        }
        if (said.empty()) return;
        gSession->say(said);
        gChatLines.push_back({shellTr(L"You"), widen(said), kText, true});
        if (gChatLines.size() > kChatLines) gChatLines.pop_front();
        input_.setText(L"");
        invalidate();
    }
    stardraft::kit::TextField input_;
    std::wstring slashBefore_;
    double slashUntil_ = 0;
};
ChatPaneView& chatPane() {
    static ChatPaneView v;
    return v;
}

void addChatLine(uint32_t who, const std::string& text) {
    const cb::Peer* p = findPeer(who);
    gChatLines.push_back({p ? widen(p->name) : shellTr(L"Someone"), widen(text), p ? personColor(*p) : kDim, false});
    if (gChatLines.size() > kChatLines) gChatLines.pop_front();
    chatPane().changed();
}

// The column shows while a session is open (this program hosts, or it has the host's map): the window places it again
// when that changes.
bool gChatShown = false;
bool chatShown() { return gSession && !gEnded && (gSession->isHost() || gSession->editor()); }
void chatShownCheck() {
    const bool on = chatShown();
    if (on == gChatShown) return;
    gChatShown = on;
    if (on) gChatLines.clear();  // (a new session: its own lines)
    else chatPane().giveBackFocus();
    shellRelayout();
}

void closePopover() {
    if (!gPopOpen) return;
    gPopOpen = false;
    gWindowView = nullptr;
    if (gHost) gHost->closeWindow(gSessionView);
}

void openPopover() {
    if (!gSession || !gRoot || !gHost || gPopOpen) return;
    const int w = scale(440), h = std::max(gPopH, scale(200));
    if (!gPopH) gPopH = h;
    draw::Rect a = gCollabAnchor;
    if (a.right <= a.left) a = {gRoot->width() - scale(12), scale(40), gRoot->width() - scale(12), scale(40)};
    const int x = std::clamp(a.right - w, scale(8), std::max(scale(8), gRoot->width() - w - scale(8)));
    const int y = a.bottom + scale(4);
    gPopOpen = true;
    gWindowView = &gSessionView;
    gHost->openWindow(*gRoot, gSessionView, draw::WindowSpec{draw::WindowKind::Popup, draw::Rect(x, y, x + w, y + gPopH), L""}, [] {
        gPopOpen = false;  // (a click outside, or Esc)
        gWindowView = nullptr;
    });
}

void reopenPopover() {
    if (!gPopOpen) return;
    closePopover();
    openPopover();
}

void showWindow() {
    if (gPopOpen) return closePopover();
    openPopover();
}

}  // namespace collabui

// --- What the rest of the program asks ----------------------------------------------------------------------------------

bool sdlCollabRuns(int id) { return id >= ID_COLLAB_SHARE && id <= ID_COLLAB_MINIMAP; }

// View > Collaborators on the minimap (the default: on).
bool collabMinimapShown() {
    static bool read = false;
    if (!read) read = true, prefs::flag(L"CollabMinimap", collabui::gMinimapOthers);
    return collabui::gMinimapOthers;
}

bool sdlCollabEnabled(int id) {
    using namespace collabui;
    const bool on = gSession && !gEnded, host = on && gSession->isHost();
    switch (id) {
    case ID_COLLAB_SHARE: return on || docOpen();
    case ID_COLLAB_COPYEDIT:
    case ID_COLLAB_COPYVIEW:
    case ID_COLLAB_NEWLINKS: return host && !gSession->link(stardraft::collab::Role::Edit).empty();
    case ID_COLLAB_PEOPLE: return on;
    case ID_COLLAB_STOP: return host;
    case ID_COLLAB_LEAVE: return on && !host;
    case ID_COLLAB_JOIN: return !on;
    case ID_COLLAB_SERVER:
    case ID_COLLAB_MINIMAP: return true;
    default: return false;
    }
}

// A participant's tab (the preview): Map layers is off (gray in the mode bar).
bool collabModeLocked(int mode) { return mode == int(Mode::Layers) && collabui::guestHere(); }

// A participant's tab (the preview): Map layers, the other workspaces and the map pages are off.
bool sdlCollabLocks(int id) {
    if (!collabui::guestHere()) return false;
    switch (id) {
    case ID_MODE_LAYERS:
    case ID_SCEN_PROPS:
    case ID_SCEN_PLAYERS:
    case ID_SCEN_CLASSIC:
    case ID_SCEN_CLASSICTRIG:
    case ID_FILE_SAVE:  // (the map is the host's: Save as keeps a copy)
        return true;
    default: return (id > ID_WS_MAP && id <= ID_WS_FILES) || (id >= ID_GO_TRIGGERS && id <= ID_GO_DATA) || (id >= ID_RAIL_KEY + 1 && id < ID_RAIL_KEY + 9);
    }
}

bool sdlCollabCommand(int id) {
    using namespace collabui;
    if (!sdlCollabRuns(id)) return false;
    checkEditor();
    if (gEnded) finish();
    if (!sdlCollabEnabled(id)) return true;
    switch (id) {
    case ID_COLLAB_SHARE: invite(); break;
    case ID_COLLAB_COPYEDIT: copyLink(stardraft::collab::Role::Edit); break;
    case ID_COLLAB_COPYVIEW: copyLink(stardraft::collab::Role::View); break;
    case ID_COLLAB_PEOPLE: showWindow(); break;
    case ID_COLLAB_NEWLINKS: newLinks(); break;
    case ID_COLLAB_STOP:
    case ID_COLLAB_LEAVE: endSession(); break;
    case ID_COLLAB_JOIN: joinPrompt(); break;
    case ID_COLLAB_SERVER: serverAddress(); break;
    case ID_COLLAB_MINIMAP:
        gMinimapOthers = !collabMinimapShown();
        prefs::setNumber(L"CollabMinimap", gMinimapOthers ? 1 : 0);
        minimapChanged();
        break;
    }
    shellMenusChanged();
    return true;
}

// Unit properties is open: others' edits wait (the window holds the units by their places in the list).
void collabHold(bool on) {
    if (collabui::gSession) collabui::gSession->setHold(on);
}

// openAny's first step: a stardraft:// link (the system gives it to the program) joins, after the question.
bool collabOpenLink(const std::filesystem::path& path) {
    const std::string s = narrow(path.wstring());
    if (s.rfind("stardraft:", 0) != 0) return false;
    shellPost([s] { collabui::join(s); });
    return true;
}

// After the editor's undo or redo (sdl_history.inc): in a session, the cells that others wrote last stay (each user's
// undo takes back only their own edits).
void collabAfterUndo() {
    using namespace collabui;
    checkEditor();
    if (!gSession || gEnded || !gSession->editor() || gSession->editor() != gState.ed.get()) return;
    gSession->afterUndo();
    gState.dirty = true;
    invalidateMapView();
    shellPost(pump);
}

// The status bar's part while a session is open: the text, and whether others are in it (a green box then).
std::wstring collabStatusText(bool& live) {
    using namespace collabui;
    live = false;
    if (!gSession || gEnded) return {};
    if (gSession->reconnecting()) return shellTr(L"Reconnecting\x2026");
    size_t in = 0;
    for (const stardraft::collab::Peer& p : gSession->peers()) in += p.approved;
    if (gSession->isHost()) {
        live = in > 0;
        return live ? shellTr(L"Live \x00B7 ") + std::to_wstring(in + 1) + shellTr(L" people") : shellTr(L"Session: waiting for participants");
    }
    live = gSession->editor() != nullptr;
    return live ? shellTr(L"Live \x00B7 ") + std::to_wstring(in + 1) + shellTr(L" people") : shellTr(L"Session: connecting\x2026");
}
bool collabReconnecting() { return collabui::gSession && !collabui::gEnded && collabui::gSession->reconnecting(); }

// The top bar's circles: the others in the session (a guest: the host first).
std::vector<CollabAvatar> collabAvatars() {
    using namespace collabui;
    std::vector<CollabAvatar> out;
    if (!gSession || gEnded || !gSession->editor()) return out;
    for (const cb::Peer& p : gSession->peers())
        if (p.approved) out.push_back({p.id, widen(p.name), initialOf(p.name), personColor(p), gFollow == int64_t(p.id)});
    std::stable_sort(out.begin(), out.end(), [](const CollabAvatar& a, const CollabAvatar& b) { return a.id < b.id; });
    return out;
}
void collabFollowToggle(uint32_t id) {
    using namespace collabui;
    if (!gSession || gEnded) return;
    if (gSession->editor() && gSession->editor() != gState.ed.get()) {  // (the session's map to the front first)
        for (size_t i = 0; i < gDocs.size(); ++i)
            if (gDocs[i].key == gSharedKey) activateDoc(int(i));
    }
    follow(gFollow == int64_t(id) ? -1 : int64_t(id));
}

// A snapshot's --wait while a session is open: that time passes in real time too, with the network's events and the
// session's timer run (a test of the program in a session).
void runPosted();
bool sdlCollabWait(int ms) {
    runPosted();  // (a join waits there: it makes the session)
    if (!collabui::gSession) return false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until && collabui::gSession) {
        runPosted();
        collabui::tickTimer();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

// The others' cursors on the map: an arrow in the person's color, with a dark outline, and their name.
// Cursor chat's keys: / (or Shift+7, the / of Nordic and German keyboards) on the map opens it; while it is open it takes
// the keys (Enter keeps the text a few seconds, Esc removes it, Backspace removes a character), not the Ctrl ones.
bool collabChatKey(const draw::KeyEvent& e, bool typing) {
    using namespace collabui;
    if (!gChatOpen) {
        if (!e.down || e.repeat || typing || !gRoot || !gSession || gEnded || !gSession->editor() || gSession->editor() != gState.ed.get()) return false;
        if (gWorkspace != Workspace::Map || !gRoot->map.shown() || !gState.mouseIn) return false;
        const bool slash = (e.key == draw::Key::Slash && !e.mods) || (e.numberRow == 7 && e.mods == draw::kShift);
        if (!slash) return false;
        chatPane().focusInput(true);
        return true;
    }
    if (e.mods & (draw::kCtrl | draw::kAlt | draw::kSuper)) return false;
    if (!e.down) return true;
    if (e.key == draw::Key::Escape) return chatClose(true), true;
    if (e.key == draw::Key::Enter) {
        gChatUntil = clockNow() + kChatKeep;
        chatClose(gChatText.empty());
        return true;
    }
    if (e.key == draw::Key::Backspace && !gChatText.empty()) {
        size_t n = gChatText.size() - 1;
        while (n > 0 && (uint8_t(gChatText[n]) & 0xC0) == 0x80) --n;  // (a whole UTF-8 character)
        gChatText.resize(n), chatSend(), invalidateMapView();
    }
    return true;  // (the map's own keys wait while the chat is open)
}
bool collabChatText(const std::string& utf8) {
    using namespace collabui;
    if (utf8 == "/" && chatPane().slashPending()) return chatPane().slashTaken(), true;  // (the / that opened the chat box)
    if (!gChatOpen) return false;
    if (gChatText.size() + utf8.size() <= kChatMax) gChatText += utf8, chatSend(), invalidateMapView();
    return true;
}

// A chat bubble: the text in a rounded box in the person's color (wrapped, at most 260 px wide), its top left at x, y.
void chatBubble(draw::Canvas& c, int x, int y, const std::wstring& text, draw::Color fill, draw::Color line, draw::Color ink) {
    const int pad = scale(8), maxW = scale(260);
    const draw::Point sz = c.measureBox(text, maxW, draw::kWrap);
    const draw::Rect r{x, y, x + std::min(maxW, sz.x) + 2 * pad, y + sz.y + scale(10)};
    c.roundBox(r, fill, line, float(scale(10)));
    c.text({r.left + pad, r.top + scale(5), r.right - pad + scale(2), r.bottom - scale(5)}, text, ink, draw::kWrap);
}

void collabCursorsPaint(draw::Canvas& c, draw::Rect view) {
    using namespace collabui;
    if (!gSession || gEnded || !gSession->editor() || gSession->editor() != gState.ed.get()) return;
    const double now = clockNow();
    // The cursors glide to the place that came last (they come about 10 times a second): each frame a part of the way,
    // and the map is drawn again until they are there. A far jump (more than 40 tiles) or a new cursor does not glide.
    struct Shown {
        float x = 0, y = 0;
    };
    static std::map<uint32_t, Shown> shown;
    static double lastPaint = 0;
    const double dt = std::clamp(now - lastPaint, 0.0, 0.1);
    lastPaint = now;
    const float k = float(1.0 - std::exp(-dt / 0.06));
    std::map<uint32_t, Shown> next;
    bool moving = false;
    c.pushClip(view);
    awarenessPaint(c, view);
    const auto old = c.setFont(c.font({draw::Face::Ui, scale(12)}));
    for (const stardraft::collab::Peer& p : gSession->peers()) {
        if (!p.cursorOn || now - p.cursorTime > 30) continue;  // (a cursor that did not move for 30 seconds goes)
        Shown at{float(p.x), float(p.y)};
        if (auto it = shown.find(p.id); it != shown.end() && std::abs(it->second.x - at.x) + std::abs(it->second.y - at.y) < 32 * 40) {
            at = {it->second.x + (float(p.x) - it->second.x) * k, it->second.y + (float(p.y) - it->second.y) * k};
            if (std::abs(at.x - float(p.x)) + std::abs(at.y - float(p.y)) < 0.5f / std::max(gState.zoom, 0.01f)) at = {float(p.x), float(p.y)};
            else moving = true;
        }
        next[p.id] = at;
        const draw::Color col = personColor(p);
        const draw::Color dark = draw::rgb(uint8_t(GetRValue(col) / 3), uint8_t(GetGValue(col) / 3), uint8_t(GetBValue(col) / 3));
        const int x = view.left + int(std::lround((at.x - gState.camX) * gState.zoom));
        const int y = view.top + int(std::lround((at.y - gState.camY) * gState.zoom));
        if (x < view.left - 200 || y < view.top - 40 || x > view.right || y > view.bottom) continue;
        // The arrow: a pixel drawing, 20 x 21 at 100 % (each pixel scale(1) square), its tip at the place. A: the dark
        // outline, B: the person's color, C: lighter, D: darker; '.': nothing (the map shows through).
        static const char* const kArrow[21] = {
            "A...................", "AA..................", "ABA.................", "ABBA................", "ABBBA...............",
            "ABBBBA..............", "ABBABBA.............", "ABB.ABBA............", "ABB..ABBA...........", "ABB...ABBA..........",
            "ABB....ABBA.........", "ABB.....ABBA........", "ABB...AA.ABBA.......", "ACB..ADDAAABBA......", "ACB.ADAADDAABBA.....",
            "ACBABA..AADDABBA....", "ACCDD.....AACCBBA...", "ACDA........AACCCA..", "ADA...........AACBA.", "AA..............AACA",
            "A.................AA"};
        auto mix = [](draw::Color a, uint8_t r, uint8_t g, uint8_t b, int t) {  // (t/8 of the way from a to r, g, b)
            return draw::rgb(uint8_t((GetRValue(a) * (8 - t) + r * t) / 8), uint8_t((GetGValue(a) * (8 - t) + g * t) / 8), uint8_t((GetBValue(a) * (8 - t) + b * t) / 8));
        };
        const draw::Color ink[4] = {draw::rgb(40, 16, 23), col, mix(col, 255, 255, 255, 2), mix(col, 0, 0, 40, 4)};
        const int px = std::max(1, scale(1));
        for (int r = 0; r < 21; ++r)
            for (int k = 0; k < 20; ++k) {
                const char ch = kArrow[r][k];
                if (ch < 'A' || ch > 'D') continue;
                int run = 1;  // (a run of the same color in the row: one rectangle)
                while (k + run < 20 && kArrow[r][k + run] == ch) ++run;
                c.fill({x + k * px, y + r * px, x + (k + run) * px, y + (r + 1) * px}, ink[ch - 'A']);
                k += run - 1;
            }
        const std::wstring name = widen(p.name);
        const int w = c.measure(name) + scale(10), h = c.metrics().height + scale(4);
        const draw::Rect label{x + scale(10), y + scale(22), x + scale(10) + w, y + scale(22) + h};
        c.roundBox(label, col, dark, float(scale(4)));
        c.text(label, name, draw::rgb(16, 16, 16), draw::kCenter | draw::kMiddle);
        if (!p.chat.empty()) chatBubble(c, label.left, label.bottom + scale(3), widen(p.chat), col, dark, draw::rgb(16, 16, 16));
    }
    if (gChatOpen || !gChatText.empty()) {  // (this user's bubble, at the mouse; open: a caret, or a cue when empty)
        const int x = view.left + gState.mouseX + scale(14), y = view.top + gState.mouseY + scale(18);
        const bool cue = gChatOpen && gChatText.empty();
        std::wstring t = cue ? shellTr(L"Say something") : widen(gChatText);
        if (gChatOpen && !cue) t += L"|";
        chatBubble(c, x, y, t, draw::rgb(250, 250, 250), draw::rgb(60, 60, 60), cue ? draw::rgb(120, 120, 120) : draw::rgb(16, 16, 16));
    }
    c.popClip();
    c.setFont(old);
    shown.swap(next);
    if (moving) shellPost([] { invalidateMapView(); });  // (the next frame)
}

// Program options > Collaboration: the display name (spaces at the ends removed; empty: asked at the next host or join)
// and the server (checked: a wrong address is not kept; empty: the default server).
void OptionsProgramSdl::setCollabName(const std::wstring& name) {
    std::wstring n = name;
    while (!n.empty() && n.back() == L' ') n.pop_back();
    while (!n.empty() && n.front() == L' ') n.erase(n.begin());
    prefs::setText(L"CollabName", n.substr(0, 32));
    if (!n.empty() && collabui::gSession && !collabui::gEnded) collabui::gSession->rename(narrow(n.substr(0, 32))), collabui::changed();
    shellSettingsChanged();
    gOptionsView.invalidate();
}
std::wstring OptionsProgramSdl::setCollabServer(const std::wstring& server) {
    std::wstring out;
    if (!server.empty()) {
        std::string norm, err;
        if (!stardraft::collab::normalizeServer(narrow(server), norm, err)) {
            const std::wstring why = widen(err);
            shellPost([why] { shellErrorBox(why, shellTr(L"Could not change the server")); });  // (after the field's change ends)
            std::wstring old;
            prefs::text(L"CollabServer", old);
            return old;
        }
        out = widen(norm);
    }
    prefs::setText(L"CollabServer", out);
    shellSettingsChanged();
    gOptionsView.invalidate();
    return out;
}

// The document tab's session state (core/views_tabs.inc): 0 none, 1 in a session alone, 2 live with others.
int collabTabState(int doc) {
    using namespace collabui;
    if (!gSession || gEnded || doc < 0 || doc >= int(gDocs.size())) return 0;
    const Editor* e = doc == gActive ? gState.ed.get() : gDocs[size_t(doc)].ed.get();
    if (!e || gSession->editor() != e) return 0;
    if (!gSession->isHost()) return 2;  // (a guest with the map: the host is there)
    for (const stardraft::collab::Peer& p : gSession->peers())
        if (p.approved) return 2;
    return 1;
}

// A snapshot: the session's pop-out closes with the input's windows; it opens again as a picture at its place.
bool sdlCollabPopoverOpen() { return collabui::gPopOpen; }
void sdlCollabPopoverInputEnd(kit_sdl::SdlHost& host, draw::View& root, bool was) {
    if (!was || collabui::gPopOpen || !gRoot || !gHost) return;
    root.attach(&host);
    collabui::openPopover();
    root.attach(nullptr);
}

// The chat column for the main window's layout (sdl_main_view.inc).
draw::View& collabChatView() { return collabui::chatPane(); }
bool collabChatShown() { return collabui::chatShown(); }
