// The palette panels (New panel like this: a palette's View menu, the duplicate box of a floating palette or a panel):
// palettes of their own, each in a window of its own (core/views_palette_float.inc: PalettePanelView), with its own type,
// view, order, search and scroll. A panel belongs to the map tab it was made in: it shows while that tab is active and
// the Map workspace shows its map, and goes when the tab closes; its window may be over the main window or outside it (on a screen). Not kept in the
// settings (a panel lives as long as its tab). Included by main_sdl.cpp after sdl_palette_float.inc.

struct PanelWindow {
    std::unique_ptr<PalettePanelView> view;
    int doc = 0;           // the map tab's key (Doc::key)
    draw::Rect at;         // its place and size in the main window's view (pixels, from the window's top left)
    int fullH = 0;         // its height while it is not collapsed (points; a snapshot's: pixels)
    bool open = false;     // its window is open
    bool closing = false;  // the program closes the window (its tab is hidden): the panel stays
};
std::vector<std::unique_ptr<PanelWindow>> gPanels;

// A mode's floating palette is its side bar panel in a window: as it floats, the window takes the side bar panel's state
// (panel_state.inc) and its section, search, scroll, view and order; as it docks, they go back. In between each is its
// own (the side bar shows another list of that side).
bool gPanelLoading = false;  // (the state is being set in the panel: its boxes' changes are not the user's)
void palettePanelRebuild(PalettePanelView& v);
PanelWindow* palettePanelOf(const PalettePanelView& v);
void panelSaveToDocked(PalettePanelView& v) {
    PanelWindow* p = palettePanelOf(v);
    if (!p || !v.floating) return;
    panelCopy(dockedPanel(v.mode), v.state());
    dockedPaletteSet(p->doc, v.mode, {v.list.scrollTop(), v.search.text(), v.section});
    gViewByMode[int(v.mode)] = v.view, gSortByMode[int(v.mode)] = v.sort;
}
void panelLoadFromDocked(PalettePanelView& v, int doc) {
    if (!v.floating) return;
    panelCopy(v.state(), dockedPanel(v.mode));
    const PaletteMemory s = dockedPaletteGet(doc, v.mode);
    v.view = gViewByMode[int(v.mode)], v.sort = gSortByMode[int(v.mode)];
    gPanelLoading = true;
    v.section = s.section;
    if (v.search.text() != s.search) v.search.setText(s.search);
    gPanelLoading = false;
    palettePanelRebuild(v);
    v.list.scrollTo(s.scroll);
}
// One of the panel's states is the active panel.
bool panelHasActive(PalettePanelView& v) {
    for (PanelState& st : v.states)
        if (&st == gActivePanel) return true;
    return false;
}

// The panel types, in the type box's order.
constexpr Mode kPanelModes[] = {Mode::Terrain, Mode::Units, Mode::Doodads, Mode::Sprites};

PanelWindow* palettePanelOf(const PalettePanelView& v) {
    for (auto& p : gPanels)
        if (p->view.get() == &v) return p.get();
    return nullptr;
}

// The panel's rows in its mode, order and search; its pick shows while its mode is the active one.
void palettePanelRebuild(PalettePanelView& v) {
    const std::vector<PaletteUnit> units = paletteUnitsOf(v.mode);
    // The section box: all, then the type's sections (a section that is gone goes back to all), as the side bar's.
    const std::vector<std::wstring> names = paletteSectionNames(units);
    std::vector<kit::DropdownItem> items;
    items.push_back({v.mode == Mode::Doodads ? shellTr(L"All doodads") : v.mode == Mode::Sprites ? shellTr(L"All sprites") : shellTr(L"All units")});
    for (const auto& n : names) items.push_back({n});
    v.sections.setItems(std::move(items));
    const auto at = std::find(names.begin(), names.end(), v.section);
    if (at == names.end()) v.section.clear();
    v.sections.setValue(at == names.end() ? 0 : int(1 + (at - names.begin())));
    v.list.state = &v.state();
    v.owner.setValue(panelFresh(v.state()).owner);
    int pinned = 0;
    v.list.searchWords = lowerPanelText(v.search.text());
    std::vector<PaletteRow> rows = paletteRows(units, v.mode, v.list.searchWords, v.mode == Mode::Terrain ? std::wstring() : v.section, pinned, v.sort);
    v.list.setRows(std::move(rows), v.mode, pinned);
    v.place();
    v.name = kModeNames[int(v.mode)];
    for (size_t i = 0; i < std::size(kPanelModes); ++i)
        if (kPanelModes[i] == v.mode) v.type.setValue(int(i));
    v.invalidate();
}
void palettePanelsRebuild() {
    for (auto& p : gPanels)
        if (p->open) palettePanelRebuild(*p->view);
}
// A palette window's text field has the keyboard (its search box, the Tiles panel's search in Terrain's window): the
// map's one-letter keys wait, as for the side bar's search box (MainView::key).
bool palettePanelsTyping() {
    for (auto& p : gPanels)
        if (p->open && MainView::typingFocusIn(*p->view)) return true;
    return false;
}
// The active panel's player changed: its owner box shows it (each panel's box shows its own player).
void palettePanelsOwner(int) {
    for (auto& p : gPanels)
        if (p->open && &p->view->state() == gActivePanel) p->view->owner.setValue(gState.playerOwner), p->view->list.invalidate();
}
// The players of the owner boxes follow the tab (a Cosmonarchy map has more: core/palette_tools.inc ownerChoices): the
// active tab's panels get its players (the other tabs' panels keep theirs until their tab is active again).
void palettePanelsOwnerItems(const std::vector<kit::DropdownItem>& owners) {
    for (auto& p : gPanels) {
        if (p->doc != activeDocKey() || p->view->owner.items().size() == owners.size()) continue;
        std::vector<kit::DropdownItem> copy = owners;
        for (kit::DropdownItem& d : copy) d.swatch = draw::kNone;  // (the panels' boxes have no color squares)
        p->view->owner.setItems(std::move(copy));
        int& owner = p->view->state().owner;
        if (owner >= int(owners.size())) owner = 11;  // (a player the tab does not have: Player 12)
        p->view->owner.setValue(&p->view->state() == gActivePanel ? gState.playerOwner : owner);
    }
}
void palettePanelsReveal(int value) {
    for (auto& p : gPanels)
        if (p->open && &p->view->state() == gActivePanel) p->view->list.reveal(value);
}
void palettePanelsChanged() {  // (the picks changed, or the active panel: shellPaletteChanged, shellPanelsChanged)
    for (auto& p : gPanels)
        if (p->open) p->view->owner.setValue(panelFresh(p->view->state()).owner), p->view->tools.invalidate(), p->view->list.invalidate(), p->view->invalidate();
}

// Opens the panel's window at its place (its tab is active and the Map workspace shows the map).
void palettePanelOpen(PanelWindow& p) {
    if (p.open || !gHost || !gRoot || gRoot->width() <= 0 || gRoot->map.frame().empty()) return;  // (not before the window has its layout)
    PanelWindow* pp = &p;
    const bool lone = !gRoot->host();  // (a snapshot's script: the root is in the host only while a picture is made)
    if (lone) gRoot->attach(gHost);
    gHost->openWindow(*gRoot, *p.view, draw::WindowSpec{draw::WindowKind::Tool, p.at, p.view->name}, [pp] {
        if (pp->closing) return;
        pp->open = false;  // the window's close button (Windows, Linux): the panel goes
        PalettePanelView* v = pp->view.get();
        shellPost([v] { palettePanelClose(*v); });
    });
    if (lone) gRoot->attach(nullptr);
    p.open = p.view->host() != nullptr;
    if (!p.open) return;
    if (SDL_Window* sw = gHost->sdlWindow(*p.view)) {
        sdlPanelWindowSetup(*p.view);
        // A collapsed panel opens as its strip (its height fixed), else at least the palettes' smallest size.
        const float d = SDL_GetWindowPixelDensity(sw);
        const int stripH = int(std::ceil(p.view->titleH / (d > 0 ? d : 1.0f)));
        if (p.view->collapsed) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(sw, &ww, &wh);
            SDL_SetWindowMinimumSize(sw, kFloatPalMinW, stripH);
            SDL_SetWindowMaximumSize(sw, 0, stripH);
            SDL_SetWindowSize(sw, ww, stripH);
        } else {
            SDL_SetWindowMinimumSize(sw, kFloatPalMinW, kFloatPalMinH);
        }
        sdlPanelKeepOnScreen(sw);
    } else {
        p.view->titleH = panelStripH();  // (a snapshot: the strip, as in the window)
    }
    palettePanelRebuild(*p.view);
    p.view->place();
}
void palettePanelHide(PanelWindow& p) {
    if (!p.open || !gHost) return;
    p.closing = true;
    gHost->closeWindow(*p.view);
    p.closing = false;
    p.open = false;
}

// The panel goes: its window closes and it is forgotten (a floating palette: the side bar shows the mode's palette
// again).
void palettePanelClose(PalettePanelView& v) {
    for (auto it = gPanels.begin(); it != gPanels.end(); ++it)
        if ((*it)->view.get() == &v) {
            const bool floating = v.floating, wasActive = panelHasActive(v);
            const Mode mode = v.mode;
            panelSaveToDocked(v);  // (a floating palette: the side bar's panel goes on from where it was)
            for (PanelState& st : v.states) panelForget(&st);
            palettePanelHide(**it);
            gPanels.erase(it);
            // Its panel was the active one: the side bar's of its mode (it docked), else the mode's panel used last.
            if (wasActive) activatePanel(floating ? &dockedPanel(mode) : panelHomeOf(mode));
            if (floating && gRoot) gRoot->relayout(), shellMenusChanged();
            return;
        }
}

// A palette window collapses to its strip, or back to its height (the floating palette's, a panel's). `fullH`: the
// height to go back to (points); collapsing keeps it there. Its window keeps its width; collapsed, its height is fixed.
void sdlPanelCollapse(PanelStripView& v, bool on, int& fullH) {
    SDL_Window* w = gHost ? gHost->sdlWindow(v) : nullptr;
    v.collapsed = on;
    if (w) {
        const float d = SDL_GetWindowPixelDensity(w);
        const int stripH = int(std::ceil(v.titleH / (d > 0 ? d : 1.0f)));
        int ww = 0, wh = 0;
        SDL_GetWindowSize(w, &ww, &wh);
        if (on) {
            fullH = wh;
            SDL_SetWindowMinimumSize(w, kFloatPalMinW, stripH);
            SDL_SetWindowMaximumSize(w, 0, stripH);
            SDL_SetWindowSize(w, ww, stripH);
        } else {
            SDL_SetWindowMaximumSize(w, 0, 0);
            SDL_SetWindowMinimumSize(w, kFloatPalMinW, kFloatPalMinH);
            SDL_SetWindowSize(w, ww, std::max(kFloatPalMinH, fullH));
            sdlPanelKeepOnScreen(w);
        }
    }
    v.invalidate();
}
void palettePanelCollapse(PalettePanelView& v, bool on) {
    PanelWindow* p = palettePanelOf(v);
    if (!p) return;
    if (!gHost || !gHost->sdlWindow(v)) {  // (a snapshot: the window's picture is its place: it opens again at the new size)
        if (on) p->fullH = p->at.height(), p->at.bottom = p->at.top + v.titleH;
        else p->at.bottom = p->at.top + std::max(p->fullH, scale(kFloatPalMinH));
        v.collapsed = on;
        palettePanelHide(*p);
        palettePanelOpen(*p);
        return;
    }
    int full = p->fullH;
    sdlPanelCollapse(v, on, full);
    p->fullH = full;
    v.place();
}

void palettePanelsLoad(int doc);  // (below: the map's own file's panels, once a tab)
void palettePanelsSave();

// Shows the panels of the active tab in the Map workspace and hides the others; forgets those of closed tabs.
// Each map's palettes float or not as the map was left (its file's panels); a new map's are docked.
void palettePanelsSync() {
    std::erase_if(gPanels, [](const std::unique_ptr<PanelWindow>& p) {
        for (const Doc& d : gDocs)
            if (d.key == p->doc) return false;
        if (p->open && gHost) p->closing = true, gHost->closeWindow(*p->view);
        return true;
    });
    const bool mapShown = gRoot && gWorkspace == Workspace::Map && gRoot->map.shown() && docOpen();
    const int doc = activeDocKey();
    if (mapShown) palettePanelsLoad(doc);
    for (auto& p : gPanels) {
        if (mapShown && p->doc == doc) palettePanelOpen(*p);
        else palettePanelHide(*p);
    }
}

void tabDragWindowFrame(Mode m, SDL_Window* w, bool live = false);  // (below)
SDL_Window* floatingSdlWindowOf(Mode m);
PanelWindow* floatingPanelOf(int doc, Mode m);
// Each turn of the event loop: each open panel's place as the user moves it (kept on a screen), and the
// active tab's panels in its map's own file when they changed.
void sdlPalettePanelsFrame() {
    palettePanelsSync();
    palettePanelsSave();
    for (const Mode m : kModeOrder) tabDragWindowFrame(m, floatingSdlWindowOf(m));  // (a floating palette moved near a side's tabs docks)
    if (gFloatPalSaveAt && SDL_GetTicks() >= gFloatPalSaveAt) gFloatPalSaveAt = 0, saveDock();
    SDL_Window* main = gHost ? gHost->mainWindow() : nullptr;
    if (!main) return;
    int mx = 0, my = 0;
    SDL_GetWindowPosition(main, &mx, &my);
    const float d = SDL_GetWindowPixelDensity(main);
    for (auto& p : gPanels) {
        SDL_Window* w = p->open ? gHost->sdlWindow(*p->view) : nullptr;
        if (!w) continue;
        sdlPanelKeepOnScreenAfterDrag(w);
        int x = 0, y = 0, ww = 0, wh = 0;
        SDL_GetWindowPosition(w, &x, &y), SDL_GetWindowSize(w, &ww, &wh);
        const int l = int((x - mx) * d), t = int((y - my) * d);
        p->at = draw::Rect(l, t, l + int(ww * d), t + int(wh * d));
        if (p->view->floating) {  // (a mode's floating palette: the next one opens at its place, saved a moment after)
            FloatPalette& kept = gFloatPal[int(p->view->mode)];
            FloatPalette now = kept;
            now.x = x, now.y = y, now.w = ww;
            if (!p->view->collapsed) now.h = wh;
            if (!(now == kept)) kept = now, gFloatPalSaveAt = SDL_GetTicks() + 500;
        }
    }
}

// A panel of the tab `doc` with its wiring (not placed, not open yet).
std::unique_ptr<PanelWindow> palettePanelMake(Mode m, const PaletteView& view, PaletteSort sort, int doc) {
    auto p = std::make_unique<PanelWindow>();
    p->view = std::make_unique<PalettePanelView>();
    p->doc = doc;
    PalettePanelView& v = *p->view;
    v.mode = m, v.view = view, v.sort = sort;
    if (m != Mode::Units) v.view.sprites = false;
    std::vector<kit::DropdownItem> types;
    for (Mode t : kPanelModes) types.push_back({shellTr(kModeNames[int(t)])});
    v.type.setItems(std::move(types));
    PalettePanelView* pv = &v;
    v.type.onChange = [pv](int i) {
        if (i < 0 || i >= int(std::size(kPanelModes)) || kPanelModes[i] == pv->mode) return;
        const Mode from = pv->mode, to = kPanelModes[i];
        const bool wasActive = panelHasActive(*pv);
        // A mode's floating palette becomes the new mode's: that mode's floating palette (if it has one) takes the old
        // mode (the two swap), else the old mode's panel goes back to the side bar and the new mode's comes from it.
        PanelWindow* other = nullptr;
        if (pv->floating)
            for (auto& q : gPanels)
                if (q->view.get() != pv && q->doc == activeDocKey() && q->view->floating && q->view->mode == to) other = q.get();
        if (pv->floating && !other) panelSaveToDocked(*pv);
        auto retitle = [](PalettePanelView& w) {
            if (SDL_Window* sw = gHost ? gHost->sdlWindow(w) : nullptr) SDL_SetWindowTitle(sw, narrow(shellTr(w.name.c_str())).c_str());
        };
        if (other) {
            PalettePanelView& o = *other->view;
            panelCopy(o.states[size_t(from)], pv->states[size_t(from)]);
            panelCopy(pv->states[size_t(to)], o.states[size_t(to)]);
            std::swap(pv->view, o.view), std::swap(pv->sort, o.sort), std::swap(pv->section, o.section);
            const std::wstring os = o.search.text(), ps = pv->search.text();
            gPanelLoading = true;
            o.search.setText(ps), pv->search.setText(os);
            gPanelLoading = false;
            if (&o.state() == gActivePanel) gActivePanel = nullptr, activatePanel(&o.states[size_t(from)]);  // (its state went to pv)
            o.mode = from;
            o.list.scrollTo(0);
            palettePanelRebuild(o);
            retitle(o);
        }
        pv->mode = to;
        if (!other) {
            pv->section.clear();
            if (pv->mode != Mode::Units) pv->view.sprites = false;
            pv->list.scrollTo(0);
        }
        palettePanelRebuild(*pv);
        if (pv->floating && !other) panelLoadFromDocked(*pv, palettePanelOf(*pv)->doc);
        retitle(*pv);
        // The panel was the active one, or a mode's floating palette: its new mode's state is the active panel.
        if (wasActive || pv->floating) activatePanel(&pv->state());
        if (pv->floating && gRoot) gRoot->relayout(), shellMenusChanged();
    };
    v.search.onChange = [pv] {
        if (gPanelLoading) return;
        pv->list.scrollTo(0);
        palettePanelRebuild(*pv);
    };
    std::vector<kit::DropdownItem> owners;  // the players, as the side bar's owner box
    for (int i = 0; i < 12; ++i) owners.push_back({L"Player " + std::to_wstring(i + 1) + (i == 11 ? L" (neutral)" : L"")});
    v.owner.setItems(std::move(owners));
    v.owner.onChange = [pv](int i) {
        activatePanel(&pv->state());  // (its own player)
        setPlayer(gState, i);
        shellFocusView();
    };
    v.sections.onChange = [pv](int i) {
        const std::vector<std::wstring> names = paletteSectionNames(paletteUnitsOf(pv->mode));
        pv->section = i <= 0 || i > int(names.size()) ? std::wstring() : names[size_t(i - 1)];
        pv->list.scrollTo(0);
        palettePanelRebuild(*pv);
    };
    v.onView = [pv] { paletteMenuSdl(pv->list, pv->viewButton, {0, pv->viewButton.height()}, -1, pv); };
    v.onSort = [pv] { paletteSortMenuSdl(pv->mode, pv->sortButton, pv); };
    v.onCollapse = [pv] { shellPost([pv] { palettePanelCollapse(*pv, !pv->collapsed); }); };
    v.list.picture = panelPicture;
    // A pick picks in the panel's mode: the active mode becomes it (as a click in the mode's own palette).
    v.list.picked = [pv](int row, unsigned mods) {
        if (row < 0 || row >= int(pv->list.rows.size())) return;
        const int value = pv->list.rows[size_t(row)].value;
        if (value < 0) return;
        activatePanel(&pv->state());  // (its own picks)
        // Ctrl adds to the pick; Shift picks every item from the last pick (as the side bar's palette).
        const bool ctrl = (mods & draw::kCtrl) != 0;
        const bool second = (mods & draw::kShift) && gState.mode == Mode::Terrain && !gState.terrainRect;  // (no pick before: adds it, as the side bar's)
        if (!((mods & draw::kShift) && paletteShiftPick(gState, pv->list.rows, row, ctrl))) pickUnit(gState, value, ctrl || second);
        shellPaletteChanged();
        invalidateMapView();
        shellFocusView();
    };
    v.list.context = [pv](int row, draw::Point pt) { paletteMenuSdl(pv->list, pv->list, pt, row, pv); };
    v.keys = [](const draw::KeyEvent& e) { return gRoot && gRoot->key(e); };
    v.onClose = [pv] { shellPost([pv] { palettePanelClose(*pv); }); };
    v.onDuplicate = [pv] {
        const Mode mm = pv->mode;
        const PaletteView vv = pv->view;
        const PaletteSort ss = pv->sort;
        shellPost([mm, vv, ss, pv] { palettePanelNew(mm, vv, ss, pv); });
    };
    return p;
}

// A new panel like a palette (its mode, view and order), of the active tab; by the window `nearView` (a floating palette or
// a panel: a step down and right of it), else over the map by the side bar.
void palettePanelNew(Mode m, const PaletteView& view, PaletteSort sort, draw::View* nearView) {
    if (!gRoot || !docOpen() || m == Mode::Locations) return;
    auto p = palettePanelMake(m, view, sort, activeDocKey());
    PalettePanelView& v = *p->view;
    // Its place: a step from the window it comes from (its size too), else over the map by the side bar.
    const int step = scale(24);
    draw::Rect at;
    PanelWindow* from = nullptr;
    if (auto* nv = dynamic_cast<PalettePanelView*>(nearView)) from = palettePanelOf(*nv);
    if (from)
        at = from->at;
    else if (nearView == &gFloatPalView && gFloatPalOpen) {
        at = gFloatPalShotAt;
        if (SDL_Window* fw = gHost ? gHost->sdlWindow(gFloatPalView) : nullptr) {
            SDL_Window* main = gHost->mainWindow();
            int mx = 0, my = 0, x = 0, y = 0, w = 0, h = 0;
            SDL_GetWindowPosition(main, &mx, &my), SDL_GetWindowPosition(fw, &x, &y), SDL_GetWindowSize(fw, &w, &h);
            const float d = SDL_GetWindowPixelDensity(main);
            at = draw::Rect(int((x - mx) * d), int((y - my) * d), int((x - mx + w) * d), int((y - my + h) * d));
        }
    } else {
        const draw::Rect map = gRoot->map.frame();
        const int w = scale(280), h = std::clamp(map.height() - scale(32), scale(300), scale(600));
        const int x = std::max(map.left, map.right - scale(16) - w) - step * int(gPanels.size() % 8), y = map.top + scale(16) + step * int(gPanels.size() % 8);
        at = draw::Rect(x, y, x + w, y + h);
    }
    if (nearView) at = draw::Rect(at.left + step, at.top + step, at.right + step, at.bottom + step);
    p->at = at;
    // Like the panel it comes from: its state too (its pick, tools, brush and the rest; else the side bar's of the mode).
    PanelState* like = from ? &from->view->state() : &dockedPanel(m);
    if (like->mode == m) panelCopy(v.state(), *like);
    gPanels.push_back(std::move(p));
    palettePanelOpen(*gPanels.back());
    setMessage(shellTr(L"New palette panel: ") + shellTr(v.name.c_str()));
}

// The host closes all the snapshot's windows after its inputs (sdlFloatPaletteInputEnd): before, the panels know the
// program closes them; after, they are closed (the relayout after it opens them again for the picture).
void palettePanelsWindowsGone(bool before) {
    for (auto& p : gPanels) {
        if (before) p->closing = true;
        else p->closing = false, p->open = false;
    }
}

void shellNewPalettePanel(int mode) {
    if (mode < 0 || mode >= kModeCount) return;
    const Mode m = Mode(mode);
    palettePanelNew(m, gViewByMode[int(m)], gSortByMode[int(m)], nullptr);
}

// --- The panels in the map's own file (core/map_prefs.inc): "Panels", one text a panel -----------------------------
// Each: its type, order, view (3 bits: grid, sprites, large), collapsed, its place and size and its whole height (points,
// from the main window's top left), its section, its search, a mode's floating palette (1) or a panel (0); tab-separated
// (tabs and line ends in a search are spaces).
std::set<int> gPanelsLoaded;                           // the tabs whose panels came from their file
std::map<int, std::vector<std::string>> gPanelsKept;  // the panels each tab's file has (written when they differ)
Uint64 gPanelsSaveAt = 0;                             // the next look at whether they changed

fs::path palettePanelsFile(int doc) {
    for (size_t i = 0; i < gDocs.size(); ++i)
        if (gDocs[i].key == doc) {
            const Editor* ed = int(i) == gActive ? gState.ed.get() : gDocs[i].ed.get();
            return ed && !gDocs[i].page ? mapPrefsFile(ed->path()) : fs::path();
        }
    return {};
}
float palettePanelPointPx() { return scale(1000) / 1000.0f; }  // (pixels a point)

std::vector<std::string> palettePanelsText(int doc) {
    std::vector<std::string> out;
    const float k = palettePanelPointPx();
    auto clean = [](std::wstring t) {
        for (wchar_t& c : t)
            if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
        return narrow(t);
    };
    for (const auto& p : gPanels) {
        if (p->doc != doc) continue;
        const PalettePanelView& v = *p->view;
        const int bits = int(v.view.grid) | int(v.view.sprites) << 1 | int(v.view.large) << 2 | int(v.view.xl) << 3;
        const int full = v.collapsed ? p->fullH : 0;
        char head[160];
        std::snprintf(head, sizeof head, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", int(v.mode), int(v.sort), bits, int(v.collapsed), int(p->at.left / k),
                      int(p->at.top / k), int(p->at.width() / k), int(p->at.height() / k), full);
        out.push_back(std::string(head) + "\t" + clean(v.section) + "\t" + clean(v.search.text()) + "\t" + (v.floating ? "1" : "0") + "\t" +
                      panelStateText(p->view->state()));  // (its own state: panel_state.inc)
    }
    return out;
}
// The tab's side bar panels ("SidePanels"): each mode's state.
std::vector<std::string> sidePanelsText(int doc) {
    std::vector<std::string> out;
    DockedPanels& d = dockedPanelsOf(doc);
    for (PanelState& st : d.modes) out.push_back(panelStateText(st));
    return out;
}

// The tab's panels from its map's file, the first time the tab shows (none kept: none).
void palettePanelsLoad(int doc) {
    if (!doc || gPanelsLoaded.count(doc)) return;
    gPanelsLoaded.insert(doc);
    const fs::path f = palettePanelsFile(doc);
    std::error_code ec;
    std::optional<std::vector<std::string>> list, side;
    if (!f.empty() && fs::exists(f, ec)) {
        const stardraft::Settings set(f);
        list = set.list("Panels"), side = set.list("SidePanels");
    }
    // The side bar's panels as the map was left (none kept, a new map: as a new panel).
    if (side) {
        DockedPanels& d = dockedPanelsOf(doc);
        for (const std::string& line : *side) {
            std::vector<std::string> part;
            size_t a = 0;
            for (size_t b; (b = line.find('\t', a)) != std::string::npos; a = b + 1) part.push_back(line.substr(a, b - a));
            part.push_back(line.substr(a));
            const int m = part.empty() ? -1 : std::atoi(part[0].c_str());
            if (m >= 0 && m < kModeCount) panelStateRead(d.modes[size_t(m)], part, 0, Mode(m));
        }
        if (doc == activeDocKey()) panelsReloaded();
    }
    if (!list) {
        gPanelsKept[doc] = palettePanelsText(doc);
        for (std::string& t : sidePanelsText(doc)) gPanelsKept[doc].push_back("side\t" + t);
        return;  // (no window kept: nothing floats until the user pops a palette out)
    }
    const float k = palettePanelPointPx();
    for (const std::string& line : *list) {
        std::vector<std::string> part;
        size_t a = 0;
        for (size_t b; (b = line.find('\t', a)) != std::string::npos; a = b + 1) part.push_back(line.substr(a, b - a));
        part.push_back(line.substr(a));
        if (part.size() < 11) continue;
        const int mode = std::atoi(part[0].c_str());
        if (std::find(std::begin(kPanelModes), std::end(kPanelModes), Mode(mode)) == std::end(kPanelModes)) continue;
        const int bits = std::atoi(part[2].c_str());
        PaletteView view{(bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0};
        auto p = palettePanelMake(Mode(mode), view, PaletteSort(std::clamp(std::atoi(part[1].c_str()), 0, kPaletteSortCount - 1)), doc);
        const int x = std::atoi(part[4].c_str()), y = std::atoi(part[5].c_str());
        const int w = std::max(kFloatPalMinW, std::atoi(part[6].c_str())), h = std::max(1, std::atoi(part[7].c_str()));
        p->at = draw::Rect(int(x * k), int(y * k), int((x + w) * k), int((y + h) * k));
        p->fullH = std::atoi(part[8].c_str());
        p->view->collapsed = part[3] == "1";
        p->view->section = widen(part[9]);
        p->view->search.setText(widen(part[10]));
        p->view->floating = part.size() > 11 && part[11] == "1";
        panelStateRead(p->view->state(), part, 12, Mode(mode));  // (its own state, when kept)
        gPanels.push_back(std::move(p));
    }
    gPanelsKept[doc] = palettePanelsText(doc);
    for (std::string& t : sidePanelsText(doc)) gPanelsKept[doc].push_back("side\t" + t);
}

// The active tab's panels into its map's file when they changed (at most twice a second).
void palettePanelsSave() {
    const int doc = activeDocKey();
    if (!doc || !gPanelsLoaded.count(doc) || SDL_GetTicks() < gPanelsSaveAt) return;
    gPanelsSaveAt = SDL_GetTicks() + 500;
    const std::vector<std::string> panels = palettePanelsText(doc), side = sidePanelsText(doc);
    std::vector<std::string> now = panels;
    for (const std::string& t : side) now.push_back("side\t" + t);
    const auto kept = gPanelsKept.find(doc);
    if (kept != gPanelsKept.end() && kept->second == now) return;
    const fs::path f = palettePanelsFile(doc);
    if (f.empty()) return;
    stardraft::Settings s(f);
    if (s.set("Panels", panels) && s.set("SidePanels", side)) gPanelsKept[doc] = now;
}

// The script's panel verb (sdl_soak.inc), for snapshots: what it does, then a line with each panel (its type, order,
// rows, open or hidden, place).
void runPosted();
void sdlPanelScript(const std::string& what) {
    PalettePanelView* last = gPanels.empty() ? nullptr : gPanels.back()->view.get();
    const std::string arg = what.find(' ') == std::string::npos ? std::string() : what.substr(what.find(' ') + 1);
    if (what == "new") palettePanelNew(gState.mode, gViewByMode[int(gState.mode)], gSortByMode[int(gState.mode)], nullptr);
    else if (what == "dup" && last && last->onDuplicate) last->onDuplicate();
    else if (what.rfind("sort ", 0) == 0 && last) {
        last->sort = arg == "name" ? PaletteSort::Name : arg == "number" ? PaletteSort::Number : arg == "tier" ? PaletteSort::Tier
                   : arg == "name-down" ? PaletteSort::NameDown : arg == "number-down" ? PaletteSort::NumberDown : PaletteSort::Category;
        palettePanelRebuild(*last);
    } else if (what.rfind("type ", 0) == 0 && last) {
        for (size_t i = 0; i < std::size(kPanelModes); ++i)
            if (lowerPanelText(widen(arg)) == lowerPanelText(kModeNames[int(kPanelModes[i])]) && last->type.onChange) last->type.onChange(int(i));
    } else if (what.rfind("search ", 0) == 0 && last) last->search.setText(widen(arg));
    else if (what == "close" && last && last->onClose) last->onClose();
    else if (what == "list") {}  // (only the line with the panels)
    else if (what == "collapse" && last && last->onCollapse) last->onCollapse();
    else if (what == "save") {
        gPanelsSaveAt = 0, palettePanelsSave();
        std::printf("  panels' file: %s\n", palettePanelsFile(activeDocKey()).string().c_str());
    }
    else std::printf("panel %s: no such panel verb, or no panel (WRONG)\n", what.c_str());
    runPosted();
    if (gRoot) gRoot->relayout();
    sdlInputLine("panel " + what);
    for (size_t i = 0; i < gPanels.size(); ++i) {
        const PalettePanelView& v = *gPanels[i]->view;
        const char* sorts[] = {"category", "name", "number", "tier", "name-down", "number-down"};
        std::printf("  panel %zu: %s, by %s, %zu rows (first: %s), section \"%s\", search \"%s\"%s, %s at %d,%d %dx%d\n", i + 1,
                    narrow(kModeNames[int(v.mode)]).c_str(), sorts[int(v.sort)], v.list.rows.size(),
                    v.list.rows.empty() ? "-" : narrow(v.list.rows.front().label).c_str(), narrow(v.section).c_str(), narrow(v.search.text()).c_str(),
                    v.collapsed ? ", collapsed" : "", gPanels[i]->open ? "open" : "hidden", gPanels[i]->at.left, gPanels[i]->at.top,
                    gPanels[i]->at.width(), gPanels[i]->at.height());
    }
}

// --- The modes' floating palettes: a palette panel of the tab with its type fixed (PalettePanelView::floating) --------

PanelWindow* floatingPanelOf(int doc, Mode m) {
    for (auto& p : gPanels)
        if (p->doc == doc && p->view->floating && p->view->mode == m) return p.get();
    return nullptr;
}
bool paletteFloats(Mode m) {
    if (m == Mode::Locations) return gFloatPal[int(m)].floating;
    return floatingPanelOf(activeDocKey(), m) != nullptr;
}

// A floating palette's place: its window's kept place (gFloatPal: the last used, in points on the screens), else over
// the map by its side bar (as the floating palettes opened before).
draw::Rect floatingPanelPlace(Mode m) {
    if (gFloatDropAt) return *gFloatDropAt;  // (a tab dropped off the side bars: where it was dropped)
    const draw::Rect map = gRoot ? gRoot->map.frame() : draw::Rect();
    const FloatPalette& kept = gFloatPal[int(m)];
    int w = scale(280), h = std::clamp(map.height() - scale(32), scale(300), scale(600));
    SDL_Window* main = gHost ? gHost->mainWindow() : nullptr;
    if (main && kept.w > 0 && kept.h > 0 && floatPaletteOnScreens(kept, floatPaletteScreens())) {
        int mx = 0, my = 0;
        SDL_GetWindowPosition(main, &mx, &my);
        const float d = SDL_GetWindowPixelDensity(main);
        return draw::Rect(int((kept.x - mx) * d), int((kept.y - my) * d), int((kept.x - mx + kept.w) * d), int((kept.y - my + kept.h) * d));
    }
    if (kept.w > 0 && kept.h > 0) w = kept.w, h = kept.h;  // (a snapshot: its size, one pixel a point)
    const bool right = gDock.toolsRight[int(m)];
    const int x = right ? std::max(map.left, map.right - scale(16) - w) : map.left + scale(16), y = map.top + scale(16);
    return draw::Rect(x, y, x + w, y + h);
}

// The active tab's floating palette of `m` opens (as its palette: its view and order) or goes. False: no change.
bool floatingPanelSet(Mode m, bool on) {
    const int doc = activeDocKey();
    PanelWindow* now = floatingPanelOf(doc, m);
    if (on == (now != nullptr) || !gRoot || !docOpen()) return false;
    if (!on) {
        palettePanelClose(*now->view);
        return true;
    }
    auto p = palettePanelMake(m, gViewByMode[int(m)], gSortByMode[int(m)], doc);
    p->view->floating = true;
    p->at = floatingPanelPlace(m);
    gPanels.push_back(std::move(p));
    PalettePanelView& v = *gPanels.back()->view;
    const bool wasActive = gActivePanel == &dockedPanel(m);
    panelLoadFromDocked(v, doc);  // (the side bar's panel: its state, section, search, scroll, view and order)
    if (wasActive) activatePanel(&v.state());
    palettePanelOpen(*gPanels.back());
    return true;
}

// The state of the floating panels for the layout (sdl_palette_float.inc: floatPaletteSync): Terrain's holds Rectangle
// mode's Tiles panel while its Rectangle is on, in every mode (its whole panel floats); true: the active mode's floats.
bool gPanelsShown = false;  // (the Map workspace shows a map: the windows' Tiles lists can show)
void floatPalettesTiles() {
    for (auto& p : gPanels) {
        PalettePanelView& v = *p->view;
        const bool rect = gPanelsShown && v.mode == Mode::Terrain && panelShowsTiles(v.state()) && gState.ed && gState.ed->isOpen();
        if (rect != v.altShown || v.alt != &v.tilesPanel) v.holdAlt(&v.tilesPanel, rect);
        if (rect) v.tilesGrid.invalidate();
    }
}
bool floatPalettesSync(Mode active, bool shown, bool, draw::Rect) {
    gPanelsShown = shown;
    floatPalettesTiles();  // (each Terrain panel in Rectangle shows its own Tiles list)
    return active != Mode::Locations && floatingPanelOf(activeDocKey(), active) != nullptr;
}
PanelState* floatingPanelStateOf(Mode m) {
    PanelWindow* p = m == Mode::Locations ? nullptr : floatingPanelOf(activeDocKey(), m);
    return p ? &p->view->states[size_t(m)] : nullptr;
}

draw::View* floatingWindowOf(Mode m) {
    if (m == Mode::Locations) return gFloatPalOpen ? &gFloatPalView : nullptr;
    PanelWindow* p = floatingPanelOf(activeDocKey(), m);
    return p && p->open ? p->view.get() : nullptr;
}
std::string floatingPanelLine(Mode m) {
    PanelWindow* p = floatingPanelOf(activeDocKey(), m);
    if (!p || !p->open) return "no window";
    PalettePanelView& v = *p->view;
    int x = p->at.left, y = p->at.top;
    if (SDL_Window* w = gHost ? gHost->sdlWindow(v) : nullptr) SDL_GetWindowPosition(w, &x, &y);
    draw::Point size;
    const bool keysHere = gHost && gRoot && &gHost->snapshotTop(*gRoot, size) == static_cast<const draw::View*>(&v);
    char buf[512];
    std::snprintf(buf, sizeof buf, "window %dx%d px at %d,%d, %s; keys=%s; search=\"%s\"%s", v.width(), v.height(), x, y, v.altShown ? "tiles" : "list",
                  keysHere ? "window" : "main", narrow(v.search.text()).c_str(), v.search.field().hasFocus() ? " (typing)" : "");
    return buf;
}
// A pick in a panel gives the keyboard to the map: its search box loses it, and the main window gets the system's
// keyboard focus when a panel's window had it.
void palettePanelsKeysToMap() {
    if (!gHost) return;
    SDL_Window* main = gHost->mainWindow();
    for (auto& p : gPanels) {
        if (!p->open || !p->view->host()) continue;
        p->view->focus();
        if (SDL_Window* pw = gHost->sdlWindow(*p->view); pw && main && SDL_GetKeyboardFocus() == pw) SDL_RaiseWindow(main);
    }
}

// A floating panel's strips are drawn again when the tools change (a tool, a size, a mirror; the active mode).
void palettePanelsToolsChanged() {
    for (auto& p : gPanels)
        if (p->open) p->view->tools.invalidate(), p->view->invalidate();
    if (gFloatPalOpen) gFloatPalView.tools.invalidate(), gFloatPalView.invalidate();
}

// A press in a panel (its tools, its list, its Tiles list, its boxes) makes it the active panel first, before the press
// goes to the view under it (the panel used last is the active one: panel_state.inc): what it does is in that panel's
// state. In the main window, the side bar's panels: their lists, their strips, their boxes. An event watch: it runs as
// the event comes, before it is handled. (The title strips, the splitters, the map: no panel.)
PanelState* sidePanelAt(draw::Point pt);  // (sdl_main_view.inc)
bool SDLCALL palettePanelsPress(void*, SDL_Event* e) {
    if (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN || !gHost) return true;
    auto at = [e](const draw::View& v, draw::Point& pt) {
        SDL_Window* w = gHost->sdlWindow(v);
        if (!w || SDL_GetWindowID(w) != e->button.windowID) return false;
        const float d = SDL_GetWindowPixelDensity(w);
        pt = {int(e->button.x * d), int(e->button.y * d)};
        return true;
    };
    draw::Point pt;
    for (auto& p : gPanels)
        if (p->open && at(*p->view, pt)) {
            if (pt.y >= p->view->titleH && &p->view->state() != gActivePanel) activatePanel(&p->view->state());
            return true;
        }
    if (gRoot && at(*gRoot, pt))
        if (PanelState* s = sidePanelAt(pt); s && s != gActivePanel && s->mode == gState.mode) activatePanel(s);  // (another mode's: its pick)
    // (The location list's window: its pick makes Locations the mode, locationListPicked.)
    return true;
}
bool SDLCALL palettePanelsLive(void*, SDL_Event* e);
void palettePanelsSetup() {
    static bool done = false;
    if (done) return;
    done = true;
    SDL_AddEventWatch(palettePanelsPress, nullptr);
    SDL_AddEventWatch(palettePanelsLive, nullptr);  // (below: a floating palette's drop target during the system's move)
}

// --- A side bar tab's drag (core/views_panel_tabs.inc: PanelTabsView): it docks or floats --------------------------
// While a tab is dragged, the side bar under the mouse shows a blue outline (a side with no room: the side bar that
// opens at the map's edge, with the tab at its top), and its tabs a line where the tab goes. The release there docks
// the mode's panel on that side, at that place in its tabs (in its own tabs: a new order); elsewhere its panel floats,
// its window's strip under the mouse. A floating palette's window that is moved (by its strip) near a side's tabs, or
// to the map's edge where a side has no room, shows the same target; let go while it shows, the palette docks there.
// The tabs' order and sides are in the settings (Layout/<Mode>TabOrder, <Mode>Right: dock_prefs.inc); a floating
// palette's place is its kept place (gFloatPal).
struct PanelDragState {
    bool on = false;
    bool window = false;  // a floating palette's window is moved (else: a tab of the side bars)
    Mode m = Mode::Terrain;
    draw::Point at;       // the mouse, in the main window's pixels
    int side = -1;        // the drop target: 0 left, 1 right; -1 none (it floats)
    int index = 0;        // ... the place in that side's tabs (the dragged one not counted)
};
PanelDragState gPanelDrag;

// The tabs of side `s` with their places in the main window, less the dragged mode's.
std::vector<std::pair<Mode, draw::Rect>> tabDragTabs(int s) {
    std::vector<std::pair<Mode, draw::Rect>> out;
    const PanelTabsView& t = gPanelTabsSdl[s];
    if (!t.shown() || t.frame().empty()) return out;
    const draw::Point o = t.origin();
    for (const auto& tab : t.tabs)
        if (tab.m != gPanelDrag.m) out.push_back({tab.m, draw::Rect(tab.r.left + o.x, tab.r.top + o.y, tab.r.right + o.x, tab.r.bottom + o.y)});
    return out;
}
// The side bar that a drop on side `s` opens when the side has no room now: the side's width, at the map's edge.
draw::Rect tabNewSideRect(int s) {
    const draw::Rect m = gRoot->mapAt;
    const int w = std::min(layoutScale(s == 0 ? gDock.leftW : gDock.rightW, int(gDpi)), m.width() / 2);
    return s == 0 ? draw::Rect(m.left, m.top, m.left + w, m.bottom) : draw::Rect(m.right - w, m.top, m.right, m.bottom);
}
// The place that takes a drop on side `s`: its column, else (no room) the side bar that opens; empty: none.
draw::Rect tabDropRect(int s) {
    if (!gRoot || gRoot->mapAt.empty()) return {};
    if (!gRoot->docks[s].empty()) return gRoot->docks[s];
    return tabNewSideRect(s);
}
// Where the mouse makes side `s` the drop target. A side with no room: a band at the map's edge. A tab: else the side's
// column. A floating palette's window (the mouse is on its strip): near the side's tabs (no tabs: its column), so that
// a window put over the side bar or the map stays floating; its mouse can be a little out of the main window. While
// the target shows (`held`) the place is larger: a side with no room, the side bar that opens.
draw::Rect tabDragZone(int s, bool held) {
    if (!gRoot || gRoot->mapAt.empty()) return {};
    const draw::Rect col = gRoot->docks[s], m = gRoot->mapAt;
    auto outward = [s](draw::Rect r, int by) {
        if (s == 0) r.left -= by;
        else r.right += by;
        return r;
    };
    const int out = gPanelDrag.window ? scale(24) : 0;
    if (col.empty()) {
        if (held) return outward(tabNewSideRect(s), out);
        const int band = scale(48);
        return outward(s == 0 ? draw::Rect(m.left, m.top, m.left + band, m.bottom) : draw::Rect(m.right - band, m.top, m.right, m.bottom), out);
    }
    if (!gPanelDrag.window) return col;
    const PanelTabsView& pt = gPanelTabsSdl[s];
    if (!pt.shown() || pt.frame().empty()) return outward(col, out);
    const int reach = scale(held ? 64 : 40), y = pt.origin().y;
    return outward(draw::Rect(col.left, std::max(col.top, y - reach), col.right, std::min(col.bottom, y + pt.height() + reach)), out);
}
// The drop target under the mouse (the one that shows stays while the mouse is near it: tabDragZone).
void tabDragTarget() {
    const int was = gPanelDrag.side;
    gPanelDrag.side = -1, gPanelDrag.index = 0;
    for (int s = 0; s < 2; ++s) {
        const draw::Rect r = tabDragZone(s, s == was);
        if (r.empty() || !r.contains(gPanelDrag.at)) continue;
        const std::vector<std::pair<Mode, draw::Rect>> tabs = tabDragTabs(s);
        const PanelTabsView& pt = gPanelTabsSdl[s];
        const draw::Point o = pt.origin();
        const bool stripShown = pt.shown() && !pt.frame().empty();
        const draw::Rect strip = stripShown ? draw::Rect(o.x, o.y - scale(8), o.x + pt.width(), o.y + pt.height() + scale(8)) : draw::Rect();
        const bool overTabs = stripShown && (gPanelDrag.window || strip.contains(gPanelDrag.at));  // (a window: only near the tabs)
        gPanelDrag.side = s;
        if (overTabs) {  // the place between the tabs nearest to the mouse
            for (const auto& [m, tr] : tabs)
                if ((tr.left + tr.right) / 2 < gPanelDrag.at.x) ++gPanelDrag.index;
            return;
        }
        // Elsewhere on the side: at the end; on its own side the tab keeps its place.
        gPanelDrag.index = int(tabs.size());
        if (gDock.toolsRight[int(gPanelDrag.m)] == (s == 1) && !paletteFloats(gPanelDrag.m)) {
            gPanelDrag.index = 0;
            for (Mode m : dockTabOrder(gDock)) {
                if (m == gPanelDrag.m) break;
                if (gDock.toolsRight[int(m)] == (s == 1) && !paletteFloats(m)) ++gPanelDrag.index;
            }
        }
        return;
    }
}

// The panel of `m` docks on side `s`, at place `index` of its tabs (the other tabs keep their order); it is the mode.
void tabDockAt(Mode m, int s, int index) {
    if (paletteFloats(m)) shellFloatPalette(m, false);
    std::vector<Mode> order = dockTabOrder(gDock);
    order.erase(std::find(order.begin(), order.end(), m));
    std::vector<Mode> on;
    for (Mode x : order)
        if (gDock.toolsRight[int(x)] == (s == 1) && !paletteFloats(x)) on.push_back(x);
    auto at = order.end();
    if (index < int(on.size())) at = std::find(order.begin(), order.end(), on[size_t(std::max(0, index))]);
    else if (!on.empty()) at = std::find(order.begin(), order.end(), on.back()) + 1;
    order.insert(at, m);
    for (size_t k = 0; k < order.size(); ++k) gDock.tabOrder[int(order[k])] = int(k);
    gDock.toolsRight[int(m)] = s == 1;
    gLastOnSide[s] = m;
    saveDock();
    setMode(m);
    shellRelayout();
    setMessage(std::wstring(kModeNames[int(m)]) + L" panel on the " + (s == 1 ? L"right" : L"left"));
}

// The panel of `m` floats, its window's strip under `at` (the main window's pixels): its kept size, else the program's.
void tabFloatAt(Mode m, draw::Point at) {
    if (paletteFloats(m) || !gRoot || (m != Mode::Locations && !docOpen())) return;
    FloatPalette& kept = gFloatPal[int(m)];
    SDL_Window* main = gHost ? gHost->mainWindow() : nullptr;
    const float d = main ? SDL_GetWindowPixelDensity(main) : 1.0f;
    const bool sized = kept.w > 0 && kept.h > 0;
    const int w = sized ? int(kept.w * d) : scale(280), h = sized ? int(kept.h * d) : scale(420);
    const int x = at.x - std::min(w / 2, scale(60)), y = at.y - scale(12);
    gFloatDropAt = draw::Rect(x, y, x + w, y + h);
    if (main) {  // (the kept place too: on the screens, in points)
        int mx = 0, my = 0;
        SDL_GetWindowPosition(main, &mx, &my);
        kept.x = mx + int(x / d), kept.y = my + int(y / d), kept.w = int(w / d), kept.h = int(h / d);
    }
    shellFloatPalette(m, true);
    gFloatDropAt.reset();
}

void shellTabDrag(int phase, int mode, int x, int y) {
    if (mode < 0 || mode >= kModeCount) return;
    gPanelDrag.m = Mode(mode), gPanelDrag.window = false;
    gPanelDrag.at = {x, y};
    if (phase == 0) gPanelDrag.side = -1;  // (no target held from the last drag)
    if (phase == 0 || phase == 1) {
        gPanelDrag.on = true;
        tabDragTarget();
    } else {
        const bool drop = phase == 2;
        if (drop) tabDragTarget();
        gPanelDrag.on = false;
        if (drop && gPanelDrag.side >= 0) tabDockAt(gPanelDrag.m, gPanelDrag.side, gPanelDrag.index);
        else if (drop) tabFloatAt(gPanelDrag.m, gPanelDrag.at);
        else setMessage(L"The panel did not move");
    }
    if (gRoot) gRoot->invalidate();
}

// Over the main window (MainView::paintOver): the drop target's outline and its place in the tabs, and the ghost at the
// mouse (the tab; with no target, the outline of the window it becomes).
void paintTabDrag(draw::Canvas& c) {
    if (!gPanelDrag.on) return;
    auto frame = [&](draw::Rect r, int width) {
        c.fill({r.left, r.top, r.right, r.top + width}, kAccent), c.fill({r.left, r.bottom - width, r.right, r.bottom}, kAccent);
        c.fill({r.left, r.top, r.left + width, r.bottom}, kAccent), c.fill({r.right - width, r.top, r.right, r.bottom}, kAccent);
    };
    const std::wstring name = shellTr(kModeNames[int(gPanelDrag.m)]);
    if (gPanelDrag.side >= 0) {
        const draw::Rect r = tabDropRect(gPanelDrag.side);
        const bool opens = gRoot && gRoot->docks[gPanelDrag.side].empty();
        if (opens) c.fill(r, kBg, 230);  // (the side bar that opens: its panel's tab at its top, as the side bar shows it)
        c.fill(r, kAccent, 40);
        frame(r, scale(2));
        if (opens) {
            const int pad = scale(4);
            const draw::Rect tab(r.left + pad, r.top + pad, r.right - pad, r.top + pad + scale(30));
            c.fill(tab, kSel);
            c.fill({tab.left, tab.bottom - scale(2), tab.right, tab.bottom}, kAccent);
            c.text(tab, name, kAccent, draw::kCenter | draw::kMiddle);
        }
        const std::vector<std::pair<Mode, draw::Rect>> tabs = tabDragTabs(gPanelDrag.side);
        if (!tabs.empty()) {  // the line where the tab goes
            const size_t i = size_t(std::clamp(gPanelDrag.index, 0, int(tabs.size())));
            const draw::Rect& ref = tabs[std::min(i, tabs.size() - 1)].second;
            const int lx = i < tabs.size() ? ref.left : ref.right;
            c.fill({lx - scale(2), ref.top, lx + scale(2), ref.bottom}, kAccent);
        }
    }
    if (gPanelDrag.window) return;  // (the window itself follows the mouse)
    const draw::Point p = gPanelDrag.at;
    if (gPanelDrag.side < 0) {  // the window's outline, where it opens
        const FloatPalette& kept = gFloatPal[int(gPanelDrag.m)];
        const bool sized = kept.w > 0 && kept.h > 0;
        SDL_Window* main = gHost ? gHost->mainWindow() : nullptr;  // (the size tabFloatAt gives the window)
        const float d = main ? SDL_GetWindowPixelDensity(main) : 1.0f;
        const int w = sized ? int(kept.w * d) : scale(280), h = sized ? int(kept.h * d) : scale(420);
        const int x = p.x - std::min(w / 2, scale(60)), y = p.y - scale(12);
        c.fill({x, y, x + w, y + h}, kPanel, 120);
        frame({x, y, x + w, y + h}, scale(1));
    }
    const int gw = c.measure(name) + scale(20), gh = scale(26);
    const draw::Rect g(p.x + scale(12), p.y + scale(14), p.x + scale(12) + gw, p.y + scale(14) + gh);  // (below the mouse: the line stays in view)
    c.fill(g, kSel, 220);
    frame(g, scale(1));
    c.text(g, name, kText, draw::kCenter | draw::kMiddle);
}

// The SDL window of mode `m`'s floating palette (floatingWindowOf), null: none is open.
SDL_Window* floatingSdlWindowOf(Mode m) {
    draw::View* v = gHost ? floatingWindowOf(m) : nullptr;
    return v ? gHost->sdlWindow(*v) : nullptr;
}

// A floating palette's window of mode `m` moved by the user, the mouse at `at` (the main window's pixels), the left
// button `down`: while it is held the drop target shows, as a tab's drag (`paint`: the main window paints at once, as
// the event loop waits for the system's move); let go while it shows, the palette docks there (else it stays where it
// is). `stop`: the move stops with no dock (Esc). The side it docks on (the release), else -1.
int tabDragWindowStep(Mode m, draw::Point at, bool down, bool paint, bool stop = false) {
    const bool was = gPanelDrag.on && gPanelDrag.window && gPanelDrag.m == m;
    const int shownSide = was ? gPanelDrag.side : -1, shownIndex = was ? gPanelDrag.index : 0;
    if (stop) {
        if (was) gPanelDrag.on = false, gPanelDrag.side = -1;
        if (was && gRoot) gRoot->invalidate();
        return -1;
    }
    if (!was) gPanelDrag.side = -1;  // (no target held from the last drag)
    gPanelDrag.on = true, gPanelDrag.window = true, gPanelDrag.m = m;
    gPanelDrag.at = at;
    tabDragTarget();
    const bool changed = !was || gPanelDrag.side != shownSide || (gPanelDrag.side >= 0 && gPanelDrag.index != shownIndex);
    if (down) {
        if (changed && gRoot) gRoot->invalidate();
        if (changed && paint && gHost) gHost->paintNow();
        return -1;
    }
    // The release: the target that showed (a target first seen at the release does not dock: the user did not see it).
    gPanelDrag.on = false;
    if (gRoot) gRoot->invalidate();
    const int docks = shownSide >= 0 && gPanelDrag.side == shownSide ? shownSide : -1;
    if (docks >= 0) {
        const int i = gPanelDrag.index;
        shellPost([m, docks, i] { tabDockAt(m, docks, i); });  // (after the frame: the window closes)
    }
    gPanelDrag.side = -1;
    return docks;
}

// For a floating palette's window `w` of mode `m` (null: it is not open), each turn of the event loop and (`live`) at
// each of SDL's updates while the system moves it (Windows: the strip's drag is the system's move loop, and the event
// loop waits until it ends; SDL's timer in that loop sends SDL_EVENT_WINDOW_EXPOSED with data1 1, palettePanelsLive):
// while the user moves it with the left button held, the drop target shows (tabDragWindowStep).
void tabDragWindowFrame(Mode m, SDL_Window* w, bool live) {
    struct Kept {
        SDL_Window* win = nullptr;
        int x = 0, y = 0, wi = 0, he = 0;
        bool known = false, moving = false;
        bool live = false;     // the system's move loop sent updates (it was on since the last turn of the event loop)
        bool stopped = false;  // ... and it ended with the button still held (Esc): nothing until the release
    };
    static Kept kept[kModeCount];
    Kept& k = kept[int(m)];
    SDL_Window* main = gHost ? gHost->mainWindow() : nullptr;
    if (!w || !main || !gRoot) {
        if (k.moving) tabDragWindowStep(m, {}, false, false, true);
        k = Kept{};
        return;
    }
    if (k.win != w) k = Kept{}, k.win = w;  // (another window: the tab's own, or a new one)
    int x = 0, y = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(w, &x, &y), SDL_GetWindowSize(w, &ww, &wh);
    float gx = 0, gy = 0;
    const bool down = (SDL_GetGlobalMouseState(&gx, &gy) & SDL_BUTTON_LMASK) != 0;
    // The event loop runs again after the system's move loop while the button is still held: the loop ended with Esc
    // (the window is back at its place). No dock.
    if (!live && std::exchange(k.live, false) && down && k.moving) {
        k.moving = false, k.stopped = true;
        tabDragWindowStep(m, {}, false, false, true);
    }
    if (live) k.live = true;
    if (k.stopped && !down) k.stopped = false;
    // A move (not a change of its size while the button is held; the system's move loop may change its size on a
    // screen of another scale, seen only after the release).
    const bool moved = k.known && (x != k.x || y != k.y) && ((ww == k.wi && wh == k.he) || !down);
    k.known = true, k.x = x, k.y = y, k.wi = ww, k.he = wh;
    if (k.stopped) return;
    if (moved) k.moving = true;
    if (!k.moving) return;
    int mx = 0, my = 0;
    SDL_GetWindowPosition(main, &mx, &my);
    const float d = SDL_GetWindowPixelDensity(main);
    if (!down) k.moving = false;
    tabDragWindowStep(m, {int((gx - float(mx)) * d), int((gy - float(my)) * d)}, down, live);
}

// An event watch: SDL's updates while the system moves a window (Windows: its move loop; the event loop waits) go to
// the floating palette's window that moves (tabDragWindowFrame), so that the drop target shows during the move.
bool SDLCALL palettePanelsLive(void*, SDL_Event* e) {
    if (e->type != SDL_EVENT_WINDOW_EXPOSED || e->window.data1 != 1 || !gHost) return true;
    for (const Mode m : kModeOrder)
        if (SDL_Window* w = floatingSdlWindowOf(m); w && SDL_GetWindowID(w) == e->window.windowID) tabDragWindowFrame(m, w, true);
    return true;
}

// --floatmove x,y[:up|:esc] in a snapshot's script (sdl_soak.inc): the active mode's floating palette's window is moved
// by its strip, the mouse at x, y (the main window's pixels) with the button held; up: the button goes up there; esc:
// the move stops (Esc). As tabDragWindowFrame gives it (the window itself stays: the snapshot has no system's move).
void sdlFloatMoveShot(const std::string& value) {
    const size_t colon = value.find(':');
    const std::string how = colon == std::string::npos ? "" : value.substr(colon + 1);
    int x = 0, y = 0;
    if (std::sscanf(value.c_str(), "%d,%d", &x, &y) != 2 && how != "esc") return sdlInputLine("floatmove " + value + ": no point (WRONG)");
    const Mode m = gState.mode;
    if (!paletteFloats(m)) return sdlInputLine("floatmove " + value + ": the palette does not float (WRONG)");
    const int docks = tabDragWindowStep(m, {x, y}, how != "up", false, how == "esc");
    const int s = gPanelDrag.on ? gPanelDrag.side : docks;
    const char* sideName = s < 0 ? "none" : s == 0 ? "left" : "right";
    sdlInputLine("floatmove " + value + (how == "up" ? std::string(s < 0 ? ": stays floating" : ": docks ") + (s < 0 ? "" : sideName) : how == "esc" ? std::string(": stopped") : std::string(": target ") + sideName + (s < 0 ? "" : " at " + std::to_string(gPanelDrag.index))));
}
