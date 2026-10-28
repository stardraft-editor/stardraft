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

// Each turn of the event loop: each open panel's place as the user moves it (kept on a screen), and the
// active tab's panels in its map's own file when they changed.
void sdlPalettePanelsFrame() {
    palettePanelsSync();
    palettePanelsSave();
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
        const bool second = (mods & draw::kShift) && gState.mode == Mode::Terrain && !gState.terrainRect;  // (as the side bar's)
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
        const bool rect = gPanelsShown && v.mode == Mode::Terrain && panelFresh(v.state()).terrainRect && gState.ed && gState.ed->isOpen();
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
void palettePanelsSetup() {
    static bool done = false;
    if (done) return;
    done = true;
    SDL_AddEventWatch(palettePanelsPress, nullptr);
}
