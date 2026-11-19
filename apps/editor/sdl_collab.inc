// File > Collaborative editing (a preview): the open map's terrain, units, doodads, sprites and locations edited live by
// several people. The
// host invites with a link (edit or view), accepts each request, and can change roles or remove people in the session
// window; the others join with the link. include/stardraft/collab.h has the model; tools/collab-relay the server. One
// session at a time. The socket's events go to the UI thread (shellPost); a timer reads this program's edits and
// cursor every 50 ms. A participant (not the host) has the Map workspace only, and no Map layers.

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
stardraft::kit::Dialog* gWindowDlg = nullptr;
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
    if (gWindowDlg) gWindowDlg->close(0);
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
    // A participant edits the map only (the preview): the Map workspace, not Map layers.
    if (guestHere() && !gState.ed->inStroke()) {
        if (gWorkspace != Workspace::Map) setWorkspace(Workspace::Map), setMessage(shellTr(L"Collaborative editing (experimental): invited users edit the map only"));
        if (gState.mode == Mode::Layers) setMode(Mode::Terrain);
    }
    pump();
}

void post(std::function<void()> f) {
    if (!gConn) return;
    {
        std::lock_guard<std::mutex> lock(gConn->m);
        gConn->q.push_back(std::move(f));
    }
    shellPost(pump);
}

void connect(const std::string& url) {
    stardraft::platform::WebSocket::Handlers h;
    h.opened = [] { post([] { gSession->opened(); }); };
    h.text = [](std::string t) { post([t] { gSession->received(t); }); };
    h.binary = [](std::vector<uint8_t> b) { post([b] { gSession->received(Bytes(b.begin(), b.end())); }); };
    h.closed = [](std::string why) { post([why] { gSession->closed(why); }); };
    gConn->ws = stardraft::platform::WebSocket::connect(url, h);
    armTimer();
}

std::wstring roleText(cb::Role r) { return r == cb::Role::Edit ? shellTr(L"edit") : shellTr(L"view"); }

void showWindow();

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
    ev.linksChanged = [] {
        changed();
        if (gShowWindow) gShowWindow = false, shellPost(showWindow);
    };
    ev.joinRequest = [](const cb::Peer& p) {
        const uint32_t id = p.id;
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

struct SessionView final : draw::View {
    kitui::Surface surface;
    stardraft::kit::Dialog* dlg = nullptr;
    bool mouse(const draw::MouseEvent& e) override { return surface.mouse(*this, e); }
    bool key(const draw::KeyEvent& e) override {
        if (e.down && e.key == draw::Key::Escape && dlg) return dlg->close(0), true;
        return false;
    }
    void button(draw::Canvas& c, draw::Rect r, const std::wstring& label, kitpaint::Tone tone, std::function<void()> fn, bool on = true) {
        kitpaint::drawButtonFace(c, r, label, tone, !on ? kitpaint::ButtonState::Disabled : surface.hot(r) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
        if (on) surface.add({r, nullptr, [fn] { shellPost(fn); }, label, draw::Cursor::Hand});
    }
    void paint(draw::Canvas& c) override {
        const int w = width(), h = height(), pad = scale(16), bh = scale(28);
        c.fill({0, 0, w, h}, kBg);
        surface.begin();
        int y = pad;
        if (!gSession || gEnded) {
            c.text({pad, y, w - pad, y + bh}, shellTr(L"No session"), kDim, draw::kMiddle);
            return;
        }
        const bool host = gSession->isHost();
        size_t in = 0;
        for (const cb::Peer& p : gSession->peers()) in += p.approved;
        // The state: a green box while others are in the session.
        const std::wstring state = host ? (in ? shellTr(L"Hosting: ") + std::to_wstring(in) + (in == 1 ? shellTr(L" participant") : shellTr(L" participants"))
                                              : gSession->link(cb::Role::Edit).empty() ? shellTr(L"Connecting\x2026") : shellTr(L"Hosting: waiting for participants"))
                                         : (gSession->editor() ? shellTr(L"Joined: ") + roleText(gSession->role()) + shellTr(L" access") : shellTr(L"Waiting for the host\x2026"));
        const draw::Rect stateR{pad, y, w - pad, y + bh};
        kitpaint::drawBox(c, stateR, in || (!host && gSession->editor()) ? kGoodFill : kCtl, in || (!host && gSession->editor()) ? kGoodLine : kBorder);
        c.text({stateR.left + scale(10), stateR.top, stateR.right - scale(10), stateR.bottom}, state, in || (!host && gSession->editor()) ? kGood : kText, draw::kMiddle);
        y += bh + pad;
        if (host) {
            c.text({pad, y, w - pad, y + scale(20)}, shellTr(L"Invite links"), kDim, draw::kMiddle);
            y += scale(24);
            // What each link lets people do (the guest locks: sdlCollabLocks, collabModeLocked).
            const std::wstring note =
                shellTr(L"Invite people to view or to edit the map. With the edit link, they can change terrain, units, doodads, sprites and "
                        L"locations. They cannot change triggers, map layers or other map data. When the session ends, each collaborator keeps "
                        L"a copy of the map and can save it on their own computer. This feature is experimental.");
            const int nh = c.measureBox(note, w - 2 * pad, draw::kWrap).y;
            c.text({pad, y, w - pad, y + nh}, note, kText, draw::kWrap);
            y += nh + scale(12);
            const bool ready = !gSession->link(cb::Role::Edit).empty();
            const int bw = (w - 2 * pad - 2 * scale(8)) / 3;
            button(c, {pad, y, pad + bw, y + bh}, shellTr(L"Copy edit link"), kitpaint::Tone::Primary, [] { copyLink(cb::Role::Edit); }, ready);
            button(c, {pad + bw + scale(8), y, pad + 2 * bw + scale(8), y + bh}, shellTr(L"Copy view link"), kitpaint::Tone::Normal, [] { copyLink(cb::Role::View); }, ready);
            button(c, {pad + 2 * (bw + scale(8)), y, w - pad, y + bh}, shellTr(L"Reset links\x2026"), kitpaint::Tone::Normal, [] { newLinks(); }, ready);
            y += bh + pad;
        }
        c.text({pad, y, w - pad, y + scale(20)}, shellTr(L"Participants"), kDim, draw::kMiddle);
        y += scale(24);
        auto row = [&](const cb::Peer& p, bool self) {
            const draw::Rect r{pad, y, w - pad, y + bh};
            c.fill({r.left, r.top + scale(9), r.left + scale(10), r.top + scale(19)}, personColor(p));
            std::wstring label = widen(p.name) + (self ? shellTr(L" (you)") : L"");
            const std::wstring role = p.id == 0 ? shellTr(L"host") : roleText(p.role);
            c.text({r.left + scale(18), r.top, r.right - scale(200), r.bottom}, label, kText, draw::kMiddle | draw::kEllipsis);
            c.text({r.right - scale(196), r.top, r.right - scale(150), r.bottom}, role, kDim, draw::kMiddle);
            if (host && p.id != 0) {
                const uint32_t id = p.id;
                const cb::Role to = p.role == cb::Role::Edit ? cb::Role::View : cb::Role::Edit;
                button(c, {r.right - scale(146), r.top + scale(2), r.right - scale(74), r.bottom - scale(2)}, to == cb::Role::Edit ? shellTr(L"Set edit") : shellTr(L"Set view"),
                       kitpaint::Tone::Normal, [id, to] {
                           if (gSession && !gEnded) gSession->setRole(id, to);
                           changed();
                       });
                button(c, {r.right - scale(70), r.top + scale(2), r.right, r.bottom - scale(2)}, shellTr(L"Remove"), kitpaint::Tone::Danger, [id] {
                    if (gSession && !gEnded) gSession->remove(id);
                    changed();
                });
            }
            y += bh + scale(4);
        };
        cb::Peer me;
        me.id = host ? 0 : 0xFFFFFFFFu, me.name = narrow(savedName()), me.role = gSession->role(), me.color = 0;
        if (host) row(me, true);
        for (const cb::Peer& p : gSession->peers())
            if (p.approved) row(p, false);
        if (!host) row(me, true);
        const draw::Rect endR{pad, h - pad - bh, pad + scale(130), h - pad};
        button(c, endR, host ? shellTr(L"End session") : shellTr(L"Leave session"), kitpaint::Tone::Danger, [] { endSession(); });
        button(c, {w - pad - scale(90), h - pad - bh, w - pad, h - pad}, shellTr(L"Close"), kitpaint::Tone::Normal, [] {
            if (gWindowDlg) gWindowDlg->close(0);
        });
        surface.paintFocus(c, *this);
    }
};

void showWindow() {
    if (!gSession || !gRoot || gWindowDlg) return;
    SessionView view;
    stardraft::kit::Dialog dlg;
    dlg.setContent(view);
    dlg.setInsets({0, 0, 0, 0, 0, 0});
    dlg.setCenterOnWorkArea(true);
    view.dlg = &dlg;
    gWindowDlg = &dlg, gWindowView = &view;
    size_t rows = 1;
    for (const cb::Peer& p : gSession->peers()) rows += p.approved;
    rows = std::max<size_t>(rows, 4);
    dlg.run(*gRoot, shellTr(L"Collaborative editing session (experimental)"), draw::Point{scale(480), scale(190) + int(rows) * scale(32) + (gSession->isHost() ? scale(140) : 0)});
    gWindowDlg = nullptr, gWindowView = nullptr;
}

}  // namespace collabui

// --- What the rest of the program asks ----------------------------------------------------------------------------------

bool sdlCollabRuns(int id) { return id >= ID_COLLAB_SHARE && id <= ID_COLLAB_SERVER; }

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
    case ID_COLLAB_SERVER: return true;
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
    size_t in = 0;
    for (const stardraft::collab::Peer& p : gSession->peers()) in += p.approved;
    if (gSession->isHost()) {
        live = in > 0;
        return live ? shellTr(L"Live: ") + std::to_wstring(in + 1) + shellTr(L" users") : shellTr(L"Session: waiting for participants");
    }
    live = gSession->editor() != nullptr;
    return live ? shellTr(L"Live: ") + std::to_wstring(in + 1) + shellTr(L" users") : shellTr(L"Session: connecting\x2026");
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
        gChatOpen = true, gChatText.clear(), chatSend();
        gChatSkipSlash = true;  // (the / that opened it comes as text too: not in the message)
        if (draw::Host* h = gRoot->map.host())
            h->textInput(gRoot->map, true, {gState.mouseX + scale(14), gState.mouseY + scale(18), gState.mouseX + scale(200), gState.mouseY + scale(40)});
        invalidateMapView();
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
    if (!gChatOpen) return false;
    const bool skip = gChatSkipSlash && utf8 == "/";
    gChatSkipSlash = false;
    if (skip) return true;
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
    const auto old = c.setFont(c.font({draw::Face::Ui, scale(12)}));
    c.pushClip(view);
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
