// The document tabs (portable, core.inc): moved from main.cpp. The strip above the workspace (TabsView: a tab per open
// document, its title and an x or a dot when it is unsaved, and the + that posts New), the tab's title (docTitle), and
// the switch between documents (activateDoc, showActive, closeDoc, moveDoc). The instance (gTabs) is made in main.cpp,
// where the window is; the strip's native window, the host's redraws and the focus are the seam (shell.h).

// Defined by the program (main_sdl.cpp and its sdl_*.inc files) until they move into the core.
const wchar_t* gNewTitle();
std::wstring docTitle(int i, bool withMode = true);
void mapPageClose();  // map_pages.inc: the open map page goes (its working copy stays with its tab)
void pageSync();
void openStartPage();
// The page that shows in the Map workspace in place of the map (the start page, the New page) has a tab of its own:
// its title, or nullptr when no page shows; its x closes it (as Esc) when there is something under it.
const wchar_t* shellPageTab();
bool shellPageTabClosable();
void shellPageTabClose();
bool mapPagesConfirm();  // map_pages.inc: the map pages with changes that are not applied: apply them, or not
bool docMapPagesChanged(int i);  // map_pages.inc: tab i's map pages have changes that are not applied
void typingFlush();  // the texts being typed (a map page's field, the Briefing's text) go into their map now
// The save question's host parts (shell.h): shellAskSaveChanges answers 0 Save, 1 Do not save or 2 Cancel; shellSaveChanges
// saves the map (or the unsaved project, which asks where it goes) and says whether it was saved.
int shellAskSaveChanges(bool project);
bool shellSaveChanges(bool project);

// Closing the tab (or the window) with work in it: the question, and what it does. False: the close is cancelled.
bool confirmDiscard() {
    AppState& app = appState();
    if (gSelfTest || activePage()) return true;  // a page tab holds no work
    if (!mapPagesConfirm()) return false;        // map pages with changes that are not applied: apply them, or not
    if (const EudProject* pr = activeProject(); isUnsavedProject(pr)) {  // a new project: nothing to keep unless it holds work
        if (!unsavedProjectTouched(*pr)) return true;
        const int r = shellAskSaveChanges(true);
        if (r == 2) return false;
        return r == 1 || shellSaveChanges(true);
    }
    if (!app.ed || !app.ed->modified()) return true;
    const int r = shellAskSaveChanges(false);
    if (r == 2) return false;
    return r == 1 || shellSaveChanges(false);
}
void dropUnsavedProject(const std::shared_ptr<EudProject>& pr);
void forgetProject(const EudProject* pr);
void setMode(Mode m);
void updateTitle();
void setWorkspace(Workspace w);
void setMessage(const std::wstring& m);

struct TabHit {
    RECT tab, close;
};
std::vector<TabHit> gTabHits;  // laid out by the last paint
RECT gNewTabHit{};
TabHit gPageTabHit{};  // the page's tab (shellPageTab), empty when no page shows
int gTabHot = -1, gTabHotClose = -1;  // (the page's tab: gDocs.size())

const Editor* docEditor(int i) { return i == gActive ? shellState().ed.get() : gDocs[size_t(i)].ed.get(); }

// The tab strip and the title bar: "Name (project)" for a project, "Name (map)" for a classic map. withMode false
// gives the name only.
const wchar_t* gNewTitle();
std::wstring docTitle(int i, bool withMode) {
    const Editor* ed = docEditor(i);
    if (!ed) return L"";
    if (const int page = gDocs[size_t(i)].page) return page == kPageStart ? L"Start" : gNewTitle();
    int n = gDocs[size_t(i)].untitled;
    std::wstring name = !ed->path().empty() ? ed->path().filename().wstring() : n > 1 ? L"Untitled " + std::to_wstring(n) : L"Untitled";
    // A project's map is shown by the project's name.
    const auto& pr = gDocs[size_t(i)].project;
    if (pr) name = widen(pr->name());
    if (withMode) name += pr ? L" (project)" : L" (map)";
    return (ed->modified() ? L"* " : L"") + name;
}
int tabStripHeight() { return scale(30); }
void stashActive() {
    AppState& app = shellState();
    if (gActive < 0 || gActive >= int(gDocs.size()) || !app.ed) return;
    typingFlush();  // a text being typed goes into this tab's map before the tab goes to the back
    mapPageClose();
    Doc& d = gDocs[size_t(gActive)];
    d.workspace = gWorkspace;
    d.camX = app.camX, d.camY = app.camY, d.zoom = app.zoom;
    d.hasTerrainSel = app.hasTerrainSel, d.terrainSel = app.terrainSel, d.terrainSelMask = app.terrainSelMask;
    d.pasting = app.pasting, d.terrainPasting = app.terrainPasting;
    panelsStash();  // (its active panel keeps its state for when the tab is back: panel_state.inc)
    d.ed = std::move(app.ed);
}
// Program options > Placing units > Many start locations for each player (ManyStartLocations; off: a player's new start
// location moves the one that the player has).
bool manyStartLocations() {
    bool many = false;
    if (!gSelfTest && !gSettingsOff) prefs::flag(L"ManyStartLocations", many);
    return many;
}
// The setting changed: every open map follows.
void applyManyStartLocations() {
    const bool many = manyStartLocations();
    for (auto& d : gDocs)
        if (d.ed) d.ed->setManyStartLocations(many);
    if (shellState().ed) shellState().ed->setManyStartLocations(many);
}

// Shows the active editor: renderer sources, palette (the tileset may differ), title and tabs.
void showActive() {
    AppState& app = shellState();
    app.drag = Drag::None;
    app.ed->setAllowIllegalUnits(app.allowIllegalUnits);
    app.ed->setAllowStacking(app.allowStacking);
    app.ed->setResourceDistance(app.resourceDistance);
    app.ed->setManyStartLocations(manyStartLocations());
    setBuildingSnap(app.snapBuildings);  // Options > Units > Snap buildings to tile edges
    app.terrainRev = 0;
    shellRendererSources();  // the host's renderer shows this editor's sources (quality, sprites, tileset)
    panelsShowTab();  // its panel that was active (panel_state.inc), with its state
    updateTitle();
    shellRedrawTabs();
    app.dirty = true;
    pageSync();  // a page tab shows its page over the map
}
bool takeTerrainCopy(Editor& ed);  // (map_edit_commands.inc)

void activateDoc(int i) {
    AppState& app = shellState();
    if (i < 0 || i >= int(gDocs.size()) || (i == gActive && app.ed)) return;
    stashActive();
    gActive = i;
    Doc& d = gDocs[size_t(i)];
    app.ed = std::move(d.ed);
    if (app.ed) takeTerrainCopy(*app.ed);  // the terrain copied last, in another map too (GitHub #146)
    app.camX = d.camX, app.camY = d.camY, app.zoom = d.zoom;
    shellRelayout();  // the minimap takes this map's shape
    app.hasTerrainSel = d.hasTerrainSel, app.terrainSel = d.terrainSel, app.terrainSelMask = d.terrainSelMask;
    app.pasting = d.pasting, app.terrainPasting = d.terrainPasting;
    showActive();
    // Each tab its own workspace or map page: the one it was on (a page tab: Map; one this tab has not: setWorkspace
    // falls back to Map).
    if (const Workspace w = d.page ? Workspace::Map : d.workspace; w != gWorkspace || w != Workspace::Map) setWorkspace(w);
    shellInvalidateWorkspaceBar();
    setMessage(L"Switched to " + docTitle(i));
}
void protectedTerrainSync();  // (core/protected_layer.inc)
// Closes tab i (asking to save it if modified). Returns false if the user canceled.
bool closeDoc(int i) {
    AppState& app = shellState();
    if (i < 0 || i >= int(gDocs.size())) return true;
    activateDoc(i);
    if (!confirmDiscard()) return setWorkspace(gWorkspace), false;  // (canceled: the map page its question closed opens again)
    protectedTerrainSync();  // (its protected terrain kept when its question saved it: core/protected_layer.inc)
    const std::wstring closed = docTitle(i);
    mapPageClose();
    app.ed.reset();
    shellForgetAnims();  // they held that editor's sprites (a new editor can get the same address)
    const auto pr = gDocs[size_t(i)].project;
    gDocs.erase(gDocs.begin() + i);
    forgetProject(pr.get());
    dropUnsavedProject(pr);  // an unsaved project's folder goes with its tab
    gActive = -1;
    if (gDocs.empty()) {  // the last tab closed: the start page, and no part of the closed map's status stays
        for (std::wstring& part : app.statusText) part.clear();
        shellStatusChanged();
        openStartPage();
        updateTitle();  // (the closed map's name goes from the title bar)
        setMessage(L"Closed " + closed);
    }
    else activateDoc(std::min(i, int(gDocs.size()) - 1));
    return true;
}

// Whether tab i holds work that its save question asks about: a changed map, an unsaved project with work in it, or
// map pages with changes that are not applied. (The active tab: confirmDiscard asks itself.)
bool docHasWork(int i) {
    if (i < 0 || i >= int(gDocs.size())) return false;
    const Doc& d = gDocs[size_t(i)];
    if (d.page) return false;
    const Editor* ed = docEditor(i);
    if (isUnsavedProject(d.project.get())) return unsavedProjectTouched(*d.project, ed) || docMapPagesChanged(i);
    return (ed && ed->modified()) || docMapPagesChanged(i);
}

// Before StarDraft ends (quit, the window's close, Restart now, an update's install): each tab with work asks its save
// question (Save, Do not save, Cancel), the active tab first; the texts being typed go into their maps first. False:
// canceled (the tab that asked stays active, with its page). No tab closes, so a cancel loses nothing.
bool confirmAllDocs() {
    typingFlush();
    auto stay = [] {
        setWorkspace(gWorkspace);  // (the save question closed the map page: the tab that asked shows it again)
        return false;
    };
    const int first = gActive;
    if (first >= 0 && first < int(gDocs.size()) && !confirmDiscard()) return stay();
    for (int i = 0; i < int(gDocs.size()); ++i) {
        if (i == first || !docHasWork(i)) continue;
        activateDoc(i);
        if (!confirmDiscard()) return stay();
    }
    return true;
}

// Dragging a tab sideways moves it (GitHub feedback #82): the tab that is dragged, and where the press was.
int gTabDrag = -1, gTabDragX = 0;
bool gTabDragging = false;

// Moves tab `from` to the place of tab `to` (the others move up or down one place); the active tab stays active.
void moveDoc(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= int(gDocs.size()) || to >= int(gDocs.size())) return;
    const int active = gActive;
    if (from < to) std::rotate(gDocs.begin() + from, gDocs.begin() + from + 1, gDocs.begin() + to + 1);
    else std::rotate(gDocs.begin() + to, gDocs.begin() + from, gDocs.begin() + from + 1);
    if (active == from) gActive = to;
    else if (from < to && active > from && active <= to) gActive = active - 1;
    else if (from > to && active >= to && active < from) gActive = active + 1;
    shellInvalidateTabs();
}
// --- The document tabs (a View) ---------------------------------------------------------------------------------
// The strip above the workspace: a tab per open document (its title, an x, or a dot where it is unsaved), and the +
// that posts New. A press on a tab activates it and starts a drag: a tab dragged sideways takes the places of the tabs
// it passes (moveDoc). The middle button or the x closes a tab.
// A swipe to the left or right on the trackpad (dir -1: the fingers moved to the left): the next tab, or the tab
// before, round again at the ends (as Ctrl+Tab and Ctrl+Shift+Tab). With one tab, nothing.
void swipeTabs(int dir) {
    if (gDocs.size() < 2 || dir == 0) return;
    const int n = int(gDocs.size());
    activateDoc(((gActive + (dir < 0 ? 1 : -1)) % n + n) % n);
}

struct TabsView final : stardraft::draw::View {
    void paint(stardraft::draw::Canvas& c) override {
        const unsigned center = stardraft::draw::kCenter | stardraft::draw::kMiddle;
        const unsigned ellipsis = stardraft::draw::kMiddle | stardraft::draw::kEllipsis;
        const int h = height();
        c.fill({0, 0, width(), h}, kBg);
        gTabHits.clear();
        int x = 0, pad = scale(10), closeW = scale(16);
        const wchar_t* page = shellPageTab();
        gPageTabHit = {};
        if ((page != nullptr) != hadPage_) gTabHot = gTabHotClose = -1, hadPage_ = page != nullptr;  // (the tabs moved)
        for (int i = 0; i <= int(gDocs.size()); ++i) {
            if (i == int(gDocs.size()) && !page) break;
            const bool pageTab = i == int(gDocs.size());  // (the page's tab is the last, and the active one)
            std::wstring t = pageTab ? std::wstring(page) : docTitle(i);
            const bool unsaved = t.rfind(L"* ", 0) == 0;  // unsaved: a dot where the close button is (the x under the mouse)
            if (unsaved) t.erase(0, 2);
            const int w = std::clamp(c.measure(t) + pad * 3 + closeW, scale(90), scale(260));
            const RECT r{x, 0, x + w, h};
            const bool active = page ? pageTab : i == gActive;
            c.fill(r, active ? kCtl : (i == gTabHot ? kHot : kBg));
            if (active) c.fill({r.left, 0, r.right, scale(2)}, kAccent);
            c.text({r.left + pad, 0, r.right - pad - closeW, h}, t, active ? kText : kDim, ellipsis);
            const RECT cr{r.right - pad / 2 - closeW, (h - closeW) / 2, r.right - pad / 2, (h + closeW) / 2};
            if (i == gTabHotClose) c.fill(cr, kHot);
            if (pageTab && !shellPageTabClosable()) {
            } else if (unsaved && i != gTabHot) {
                const int d = scale(8), cx = (cr.left + cr.right) / 2, cy = (cr.top + cr.bottom) / 2;
                c.ellipse({cx - d / 2, cy - d / 2, cx - d / 2 + d + 1, cy - d / 2 + d + 1}, active ? kText : kDim, stardraft::draw::kNone);
            } else {
                c.text(cr, L"\x00D7", i == gTabHotClose ? kText : kDim, center);
            }
            if (pageTab) gPageTabHit = {r, shellPageTabClosable() ? cr : RECT{}};
            else gTabHits.push_back({r, cr});
            x += w + 1;
        }
        gNewTabHit = {x, 0, x + scale(30), h};
        c.text(gNewTabHit, L"+", kDim, center);
        if (keys.shows(*this, itemCount())) paintFocusOutline(c, itemRect(keys.at));
    }

    // The keyboard: Tab goes through each tab and its x, then the +; Left and Right too. Enter and Space activate the
    // tab, close it (its x) or open the New page (the +).
    ItemFocus keys;
    int itemCount() const { return int(gTabHits.size()) * 2 + 1; }
    stardraft::draw::Rect itemRect(int k) const {
        if (k >= int(gTabHits.size()) * 2) return stardraft::draw::Rect(gNewTabHit);
        const TabHit& t = gTabHits[size_t(k / 2)];
        return stardraft::draw::Rect(k % 2 ? t.close : t.tab);
    }
    bool focusable() const override { return true; }
    bool focusOnClick() const override { return false; }  // (a press gives the keyboard to the map: pressAt)
    void focusChanged(bool) override { invalidate(); }
    void tabbedTo(bool back) override { keys.enter(back, itemCount()), invalidate(); }
    bool tabWithin(bool back) override { return keys.step(back, itemCount()) && (invalidate(), true); }
    bool key(const stardraft::draw::KeyEvent& e) override {
        const int k = keys.index(itemCount());
        if (k < 0) return false;
        if (pressKey(e)) {
            const int i = k / 2;
            keys.keys = true;
            if (k >= int(gTabHits.size()) * 2) pressFromKeys([] { shellPostCommand(ID_FILE_NEW); });
            else if (k % 2) pressFromKeys([i] { closeDoc(i); });
            else pressFromKeys([i] { activateDoc(i); });
            keys.at = std::min(keys.at, int(gDocs.size()) * 2);  // (a closed tab: its strip is shorter after the repaint)
            invalidate();
            return true;
        }
        if (e.down && !e.mods && (e.key == stardraft::draw::Key::Left || e.key == stardraft::draw::Key::Right)) {
            keys.move(e.key == stardraft::draw::Key::Left ? -1 : 1, itemCount());
            invalidate();
            return true;
        }
        return false;
    }
    std::wstring focusedItem() const override {
        const int k = keys.index(itemCount());
        if (k < 0) return {};
        if (k >= int(gTabHits.size()) * 2 || k / 2 >= int(gDocs.size())) return L"New";
        return (k % 2 ? L"Close " : L"") + docTitle(k / 2);
    }
    bool mouse(const stardraft::draw::MouseEvent& e) override {
        using stardraft::draw::MouseKind;
        const POINT pt{e.pt.x, e.pt.y};
        switch (e.kind) {
        case MouseKind::Move: {
            if (gTabDrag >= 0) {  // a tab is dragged: past the middle of another tab, it takes that place
                if (!gTabDragging && std::abs(pt.x - gTabDragX) > scale(6)) gTabDragging = true;
                if (gTabDragging)
                    for (int i = 0; i < int(gTabHits.size()); ++i) {
                        const RECT& r = gTabHits[size_t(i)].tab;
                        const int mid = (r.left + r.right) / 2;
                        if (i != gTabDrag && pt.x >= r.left && pt.x < r.right && (i < gTabDrag ? pt.x < mid : pt.x >= mid)) {
                            moveDoc(gTabDrag, i);
                            gTabDrag = i;
                            shellUpdateTabs(*this);  // the new places of the tabs for the next move
                            break;
                        }
                    }
                return true;
            }
            int h = -1, hc = -1;
            for (int i = 0; i < int(gTabHits.size()); ++i) {
                if (PtInRect(&gTabHits[size_t(i)].close, pt)) hc = i;
                if (PtInRect(&gTabHits[size_t(i)].tab, pt)) h = i;
            }
            if (PtInRect(&gPageTabHit.close, pt)) hc = int(gDocs.size());
            if (h != gTabHot || hc != gTabHotClose) gTabHot = h, gTabHotClose = hc, invalidate();
            return true;
        }
        case MouseKind::Leave:
            gTabHot = gTabHotClose = -1;
            invalidate();
            return true;
        case MouseKind::Wheel:  // macOS's trackpad: a swipe of two fingers to the left or right on the strip changes the tab
            return e.exact && swipeStep(e);
        case MouseKind::Down:
        case MouseKind::DoubleClick:  // (the old tab strip had no double clicks: a second press is a press)
            if (e.button == 2) {  // the middle button closes the tab under the mouse (a map page's tab too)
                if (PtInRect(&gPageTabHit.tab, pt) || PtInRect(&gPageTabHit.close, pt)) {
                    if (shellPageTabClosable()) shellPageTabClose();
                } else {
                    pressAt(pt, true);
                }
                return true;
            }
            if (e.button != 0) return false;
            keys.pressed();
            if (PtInRect(&gNewTabHit, pt)) {
                shellPostCommand(ID_FILE_NEW);  // the New page: a map or a project
                return true;
            }
            if (PtInRect(&gPageTabHit.close, pt)) {
                shellPageTabClose();
                return true;
            }
            pressAt(pt, false);
            return true;
        case MouseKind::Up:
            if (e.button == 2) return true;  // (the middle button's press closed the tab)
            if (e.button != 0) return false;
            if (gTabDrag >= 0) {
                if (gTabDragging) setMessage(L"Moved the tab \"" + docTitle(gTabDrag) + L"\"");
                gTabDrag = -1, gTabDragging = false;
                if (auto* h = host()) h->capture(*this, false);
            }
            return true;
        default: return false;
        }
    }
    void captureLost() override { gTabDrag = -1, gTabDragging = false; }
    stardraft::draw::Cursor cursor(stardraft::draw::Point) override { return stardraft::draw::Cursor::Arrow; }

private:
    bool hadPage_ = false;  // the last paint had the page's tab
    // A two-finger scroll on the strip (its exact distance: MouseEvent::exact): once it went 50 px sideways (more than
    // up or down), one tab to that side (swipeTabs). The rest of that scroll and its momentum change no more tabs: a
    // new swipe starts after a pause of 0.3 s.
    float swipeX_ = 0, swipeY_ = 0;
    bool swiped_ = false;
    std::chrono::steady_clock::time_point swipeLast_{};
    bool swipeStep(const stardraft::draw::MouseEvent& e) {
        const auto now = std::chrono::steady_clock::now();
        if (now - swipeLast_ > std::chrono::milliseconds(300)) swipeX_ = swipeY_ = 0, swiped_ = false;
        swipeLast_ = now;
        swipeX_ += e.panX, swipeY_ += e.panY;
        if (swiped_ || std::fabs(swipeX_) < float(scale(50)) || std::fabs(swipeX_) < std::fabs(swipeY_)) return swipeX_ != 0;
        swiped_ = true;
        swipeTabs(swipeX_ < 0 ? -1 : 1);  // (the content moves left with the fingers: the next tab)
        return true;
    }

    // A press on the strip (or a middle click): the tab's x or a middle click closes it; a press on the tab activates
    // it and starts a drag.
    void pressAt(POINT pt, bool middle) {
        for (int i = 0; i < int(gTabHits.size()); ++i) {
            if (!PtInRect(&gTabHits[size_t(i)].tab, pt)) continue;
            if (middle || PtInRect(&gTabHits[size_t(i)].close, pt)) closeDoc(i);
            else {
                if (shellPageTab() && shellPageTabClosable()) shellPageTabClose();  // a map's tab: the page goes, the map shows
                activateDoc(i);
                gTabDrag = i, gTabDragX = pt.x, gTabDragging = false;  // a drag sideways moves the tab
                if (auto* h = host()) h->capture(*this, true);
            }
            break;
        }
        invalidate();
        shellFocusView();
    }
};
