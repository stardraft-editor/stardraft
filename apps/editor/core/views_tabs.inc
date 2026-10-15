// The document tabs (portable, core.inc): moved from main.cpp. The strip above the workspace (TabsView: a tab per open
// document, its title and an x or a dot when it is unsaved, and the + that posts New), the tab's title (docTitle), and
// the switch between documents (activateDoc, showActive, closeDoc, moveDoc). The instance (gTabs) is made in main.cpp,
// where the window is; the strip's native window, the host's redraws and the focus are the seam (shell.h).

// Defined in main.cpp (the Win32 TU until they move into the core).
const wchar_t* gNewTitle();
std::wstring docTitle(int i, bool withMode = true);
void mapPageClose();  // map_pages.inc: the open map page goes (its working copy stays with its tab)
void pageSync();
void openStartPage();
bool mapPagesConfirm();  // map_pages.inc: the map pages with changes that are not applied: apply them, or not
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
int gTabHot = -1, gTabHotClose = -1;

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
    mapPageClose();
    Doc& d = gDocs[size_t(gActive)];
    d.camX = app.camX, d.camY = app.camY, d.zoom = app.zoom;
    d.hasTerrainSel = app.hasTerrainSel, d.terrainSel = app.terrainSel;
    d.pasting = app.pasting, d.terrainPasting = app.terrainPasting;
    d.ed = std::move(app.ed);
}
// Shows the active editor: renderer sources, palette (the tileset may differ), title and tabs.
void showActive() {
    AppState& app = shellState();
    app.drag = Drag::None;
    app.ed->setAllowIllegalUnits(app.allowIllegalUnits);
    app.ed->setAllowStacking(app.allowStacking);
    app.ed->setMirrors(app.symmetryByMode[int(app.mode)]);
    app.terrainRev = 0;
    shellRendererSources();  // the host's renderer shows this editor's sources (quality, sprites, tileset)
    setMode(app.mode);
    updateTitle();
    shellRedrawTabs();
    app.dirty = true;
    pageSync();  // a page tab shows its page over the map
}
void activateDoc(int i) {
    AppState& app = shellState();
    if (i < 0 || i >= int(gDocs.size()) || (i == gActive && app.ed)) return;
    stashActive();
    gActive = i;
    Doc& d = gDocs[size_t(i)];
    app.ed = std::move(d.ed);
    app.camX = d.camX, app.camY = d.camY, app.zoom = d.zoom;
    shellRelayout();  // the minimap takes this map's shape
    app.hasTerrainSel = d.hasTerrainSel, app.terrainSel = d.terrainSel;
    app.pasting = d.pasting, app.terrainPasting = d.terrainPasting;
    showActive();
    if (gWorkspace != Workspace::Map) setWorkspace(d.page ? Workspace::Map : gWorkspace);  // a map tab falls back to Map; a project reloads
    shellInvalidateWorkspaceBar();
    setMessage(L"Switched to " + docTitle(i));
}
// Closes tab i (asking to save it if modified). Returns false if the user canceled.
bool closeDoc(int i) {
    AppState& app = shellState();
    if (i < 0 || i >= int(gDocs.size())) return true;
    activateDoc(i);
    if (!confirmDiscard()) return false;
    mapPageClose();
    app.ed.reset();
    shellForgetAnims();  // they held that editor's sprites (a new editor can get the same address)
    const auto pr = gDocs[size_t(i)].project;
    gDocs.erase(gDocs.begin() + i);
    forgetProject(pr.get());
    dropUnsavedProject(pr);  // an unsaved project's folder goes with its tab
    gActive = -1;
    if (gDocs.empty()) openStartPage();  // the last tab closed: the start page
    else activateDoc(std::min(i, int(gDocs.size()) - 1));
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
struct TabsView final : stardraft::draw::View {
    void paint(stardraft::draw::Canvas& c) override {
        const unsigned center = stardraft::draw::kCenter | stardraft::draw::kMiddle;
        const unsigned ellipsis = stardraft::draw::kMiddle | stardraft::draw::kEllipsis;
        const int h = height();
        c.fill({0, 0, width(), h}, kBg);
        gTabHits.clear();
        int x = 0, pad = scale(10), closeW = scale(16);
        for (int i = 0; i < int(gDocs.size()); ++i) {
            std::wstring t = docTitle(i);
            const bool unsaved = t.rfind(L"* ", 0) == 0;  // unsaved: a dot where the close button is (the x under the mouse)
            if (unsaved) t.erase(0, 2);
            const int w = std::clamp(c.measure(t) + pad * 3 + closeW, scale(90), scale(260));
            const RECT r{x, 0, x + w, h};
            const bool active = i == gActive;
            c.fill(r, active ? kCtl : (i == gTabHot ? kHot : kBg));
            if (active) c.fill({r.left, 0, r.right, scale(2)}, kAccent);
            c.text({r.left + pad, 0, r.right - pad - closeW, h}, t, active ? kText : kDim, ellipsis);
            const RECT cr{r.right - pad / 2 - closeW, (h - closeW) / 2, r.right - pad / 2, (h + closeW) / 2};
            if (i == gTabHotClose) c.fill(cr, kHot);
            if (unsaved && i != gTabHot) {
                const int d = scale(8), cx = (cr.left + cr.right) / 2, cy = (cr.top + cr.bottom) / 2;
                c.ellipse({cx - d / 2, cy - d / 2, cx - d / 2 + d + 1, cy - d / 2 + d + 1}, active ? kText : kDim, stardraft::draw::kNone);
            } else {
                c.text(cr, L"\x00D7", i == gTabHotClose ? kText : kDim, center);
            }
            gTabHits.push_back({r, cr});
            x += w + 1;
        }
        gNewTabHit = {x, 0, x + scale(30), h};
        c.text(gNewTabHit, L"+", kDim, center);
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
            if (h != gTabHot || hc != gTabHotClose) gTabHot = h, gTabHotClose = hc, invalidate();
            return true;
        }
        case MouseKind::Leave:
            gTabHot = gTabHotClose = -1;
            invalidate();
            return true;
        case MouseKind::Down:
        case MouseKind::DoubleClick:  // (the old tab strip had no double clicks: a second press is a press)
            if (e.button != 0) return false;
            if (PtInRect(&gNewTabHit, pt)) {
                shellPostCommand(ID_FILE_NEW);  // the New page: a map or a project
                return true;
            }
            pressAt(pt, false);
            return true;
        case MouseKind::Up:
            if (e.button == 2) {  // the middle button closes the tab under the mouse
                pressAt(pt, true);
                return true;
            }
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
    // A press on the strip (or a middle click): the tab's x or a middle click closes it; a press on the tab activates
    // it and starts a drag.
    void pressAt(POINT pt, bool middle) {
        for (int i = 0; i < int(gTabHits.size()); ++i) {
            if (!PtInRect(&gTabHits[size_t(i)].tab, pt)) continue;
            if (middle || PtInRect(&gTabHits[size_t(i)].close, pt)) closeDoc(i);
            else {
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
