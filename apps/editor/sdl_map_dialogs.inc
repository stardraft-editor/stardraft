// The map's dialogs and menus on this system: Unit properties, Location properties and the text prompt (core/views_unit_props.inc,
// core/views_location_props.inc, core/views_text_prompt.inc) as modal kit dialogs over the window (Host::runModal), and
// the map's right-click menu (core/views_map_menu.inc) as a kit Menu. The Windows hosts are main.cpp's showUnitProperties,
// showLocationEdit, prompt and mapMenu. Included by main_sdl.cpp after sdl_options.inc.

// Edit > Tools > Place map revealers: the players' dialog over the window (core/views_revealers_dialog.inc). True with the
// chosen players in `owners`.
bool showMapRevealersDialog(std::vector<uint8_t>& owners) {
    return gRoot && runRevealersDialog(*gRoot, owners);
}

// The prompt (Rename location, a sound's name): OK puts the typed text into `text` and returns true.
bool prompt(const wchar_t* title, std::wstring& text) {
    if (!gRoot) return false;
    TextPromptView* shown = nullptr;
    return runTextPrompt(*gRoot, title, text, true, shown);
}

void dataGoTo(const std::string& dat, int e);  // (sdl_data.inc: the entry in its tab)

// Unit properties of unit `index` (and the rest of the selection, when it is part of it), as on Windows.
// The Unit properties window's default size: 1000 x 640 points (a list with several columns and many rows), never larger
// than the main window. (The size of the last window is kept: unitWindowSaved.)
draw::Point unitWindowDefaultSize() {
    const int w = gRoot && gRoot->width() > 0 ? gRoot->width() : scale(1000), h = gRoot && gRoot->height() > 0 ? gRoot->height() : scale(640);
    return draw::Point{std::min(scale(1000), w), std::min(scale(640), h)};
}

// When the Unit properties window closes: its place and size are kept in the settings (the next one opens there).
void unitWindowSaved(const UnitPropsView* v) {
    if (!v || !gHost) return;
    SDL_Window* w = gHost->sdlWindow(*v);
    if (!w) return;
    int x = 0, y = 0, pw = 0, ph = 0;
    SDL_GetWindowPosition(w, &x, &y), SDL_GetWindowSizeInPixels(w, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;
    gDock.unitWinX = x, gDock.unitWinY = y, gDock.unitWinW = pw, gDock.unitWinH = ph;
    saveDock();
}

void showUnitProperties(int index) {
    if (!gRoot || !gState.ed || index < 0 || index >= int(gState.ed->units().size())) return;
    // The window has the Units list (core/units_list.inc): a pick, Delete, undo or redo shows the window again (for the
    // picked unit, or else the map's first unit), until it is closed for good.
    for (;;) {
        Editor& ed = *gState.ed;
        if (!ed.isSelected(index)) ed.clearSelection(), ed.select(index, false);
        const std::vector<int> units = ed.selection();
        const Unit first = ed.units()[size_t(index)];
        gState.dirty = true;
        UnitPropsView* shown = nullptr;
        const int type = first.type, owner = first.owner;
        const bool project = activeProject() != nullptr;
        int next = -1;
        UnitPropsWindow win;  // (resizable: its place and size as the settings keep them; the default size otherwise)
        win.placed = gDock.unitWinW > 0;
        win.place = draw::Point{gDock.unitWinX, gDock.unitWinY};
        win.size = gDock.unitWinW > 0 ? draw::Point{gDock.unitWinW, gDock.unitWinH} : unitWindowDefaultSize();
        win.closing = [&shown] { unitWindowSaved(shown); };
        const int r = runUnitPropsDialog(*gRoot, ed, units, first, draw::Image{}, project, shown, [type, owner](draw::Canvas& c, draw::Rect box) {
            if (draw::Bitmap* b = paletteBitmap(Mode::Units, type, owner)) drawPaletteArt(c, *b, box);  // (the palette's picture)
        }, &next, &win);
        if (r == kUnitPropsEditType) {  // Edit unit type: the type in Data (a project) or on the Map data page (a classic map)
            if (project) setWorkspace(Workspace::Data), dataGoTo("units", type);
            else sdlMapDataShowUnit(type);
        }
        updateTitle();
        gState.dirty = true;
        invalidateMapView();
        if (r == kUnitPropsPick && next >= 0 && next < int(gState.ed->units().size())) {
            index = next;
            continue;
        }
        if (r == kUnitPropsRefresh && !gState.ed->units().empty()) {
            index = 0;
            continue;
        }
        break;
    }
}

// Location properties of location `loc` (it is selected first), as on Windows.
void showLocationEdit(int loc) {
    static bool open = false;
    if (!gRoot || !gState.ed || !gState.ed->locationUsed(loc) || open) return;
    gState.ed->selectLocation(loc);
    setListSel(int(std::find(gState.items.begin(), gState.items.end(), loc) - gState.items.begin()));
    gState.drag = Drag::None;
    gState.dirty = true;
    open = true;
    LocationPropsView* shown = nullptr;
    const int r = runLocationPropsDialog(*gRoot, *gState.ed, loc, true, shown);
    open = false;
    if (r == kLocPropsOk) rebuildList(gState), setListSel(int(std::find(gState.items.begin(), gState.items.end(), loc) - gState.items.begin()));
    updateTitle();
    gState.dirty = true;
    invalidateMapView();
}

// The map view's double click (the Windows map's WM_LBUTTONDBLCLK): in Locations the location's properties (the selected
// one when it is under the mouse, else the smallest there); in Units, with nothing to place, the unit's. True when it
// opens one (after the click's event: the dialog's loop does not run inside the map's mouse event).
bool mapDoubleClickSdl(int x, int y) {
    if (!gState.ed || !gState.ed->isOpen()) return false;
    float wx, wy;
    toWorld(x, y, wx, wy);
    Editor& ed = *gState.ed;
    if (gState.mode == Mode::Locations) {
        const int sel = ed.selectedLocation();
        const int loc = ed.locationUsed(sel) && ed.locationRect(sel).contains(int(wx), int(wy)) ? sel : ed.locationAt(int(wx), int(wy));
        if (loc < 0) return false;
        gState.drag = Drag::None;
        shellPost([loc] { showLocationEdit(loc); });
        return true;
    }
    if (gState.mode == Mode::Units && gState.palette < 0) {
        const int u = stackUnitAt(ed, wx, wy);  // (a stack: the unit that a click again or the list selected, else the top one)
        if (u < 0) return false;
        gState.drag = Drag::None;
        shellPost([u] { showUnitProperties(u); });
        return true;
    }
    return false;
}

// Enter on the map (the Windows map's VK_RETURN): the selected location's or unit's properties. True when it opens one.
bool mapEnterSdl() {
    if (!gState.ed || !gState.ed->isOpen() || gState.drag != Drag::None) return false;
    Editor& ed = *gState.ed;
    if (gState.mode == Mode::Locations && ed.locationUsed(ed.selectedLocation())) {
        const int loc = ed.selectedLocation();
        shellPost([loc] { showLocationEdit(loc); });
        return true;
    }
    if (gState.mode == Mode::Units && !ed.selection().empty() && !gState.pasting) {
        const int u = ed.selection().front();
        shellPost([u] { showUnitProperties(u); });
        return true;
    }
    return false;
}

// The list of the units of a stack (core/views_map_menu.inc: showUnitStack) at the map view's point (sx, sy): the list
// picker with each unit's icon. True with the picked unit's value and the modifier keys held at the pick.
draw::Image sdlDataImage(DataPicture p);
bool shellPickStackUnit(std::vector<ListPickItem> items, const std::string& current, int sx, int sy, std::string& out, unsigned& mods) {
    if (!gRoot || items.empty()) return false;
    const draw::Point o = gRoot->map.origin();
    ListPickView v(tr(L"Search the units here"), std::move(items), current);
    v.iconOf = [](const ListPickItem& it) { return it.iconDat.empty() || it.icon < 0 ? draw::Image{} : sdlDataImage(dataRefPicture(it.iconDat, uint32_t(it.icon))); };
    if (!runListPick(v, {o.x + sx, o.y + sy})) return false;
    out = v.result(), mods = v.pickMods();
    return true;
}

// Scenario > Check pathing's list (core/pathing_layer.inc): the list picker at the map view's top-left corner. True with
// the picked row's value.
bool shellPickPathingProblem(std::vector<ListPickItem> items, std::string& out) {
    if (!gRoot || items.empty()) return false;
    const draw::Point o = gRoot->map.origin();
    ListPickView v(tr(L"Search the pathing problems"), std::move(items), "");
    if (!runListPick(v, {o.x + scale(16), o.y + scale(16)})) return false;
    out = v.result();
    return true;
}

// The map menu's items as kit items: the core's model with its own enabled states; a pick runs mapMenuPick. The icons
// are the Win32 menu's (setMenuIcons) for the items that have one.
std::vector<kit::MenuItem> mapMenuKit(const MenuModel& items) {
    static const std::pair<int, UiIcon> icons[] = {{ID_EDIT_CUT, UiIcon::Cut},           {ID_EDIT_COPY, UiIcon::Copy},
                                                   {ID_EDIT_PASTE, UiIcon::Paste},       {ID_EDIT_DELETE, UiIcon::Delete},
                                                   {ID_EDIT_SELECTALL, UiIcon::SelectAll}, {ID_EDIT_DUPLICATE, UiIcon::Duplicate}};
    std::vector<kit::MenuItem> out;
    for (const MenuItem& it : items) {
        kit::MenuItem k;
        if (it.kind == MenuItem::Kind::Separator) k.separator = true;
        else {
            k.label = accessFree(it.label), k.shortcut = it.shortcut, k.checked = it.checked, k.enabled = !it.grayed && it.id != 0;
            for (const auto& [id, icon] : icons)
                if (id == it.id) k.icon = int(icon);
            const int id = it.id;
            k.run = [id] {
                mapMenuPick(id);
                gState.dirty = true;
                invalidateMapView();
            };
        }
        out.push_back(std::move(k));
    }
    return out;
}

// The right button let go on the map (the Windows map's WM_RBUTTONUP): placing, pasting or painting stops (back to the
// mode's selection); else the map's menu opens at the mouse for what is under it.
void mapRightClickSdl(draw::View& map, draw::Point at) {
    if (!gState.ed || !gState.ed->isOpen() || gState.drag != Drag::None) return;
    if (trigUnitPickActive()) return trigCancelUnitPick();  // (cancelStep: a unit pick for a trigger stops first)
    if (!inSelection()) {  // (the Windows program's cancelStep: no stroke is going on here)
        enterSelection();
        setMessage(std::wstring(kModeNames[int(gState.mode)]) + L": select");
        gState.dirty = true;
        return;
    }
    float wx, wy;
    toWorld(at.x, at.y, wx, wy);
    const std::vector<kit::MenuItem> items = mapMenuKit(mapMenuModel(wx, wy));
    gState.dirty = true;
    map.invalidate();
    sdlPopupMenu(map, at, items, sdlFeedbackOn(map, at, L"the map"));  // (sdl_menus.inc: with Report feedback, as on Windows)
}

// --- The quick picker (Q, Quick open, Replace with...; core/views_quick_pick.inc) -----------------------------------

// The picker's host on this system: a Popup window over the map (Quick open: over the window); the rows' pictures are
// the palette's. The Data jump is not here (no projects on this system yet).
QuickPickView gQuickViewSdl;
struct QuickPickSdl final : QuickPickHost {
    bool open = false;
    bool drawPicture(draw::Canvas& c, QuickPickKind kind, Mode mode, const PaletteUnit& u, draw::Rect box) override {
        if (kind == QuickPickKind::Data) return false;
        if (mode == Mode::Terrain && tilesOn()) {  // Rectangle: the row's tile, as the Tiles list shows it
            draw::Rect src;
            draw::Bitmap& t = tilesPixels(uint16_t(u.value), src);
            c.bitmap(t, src, box);
            return true;
        }
        draw::Bitmap* b = paletteBitmap(mode, u.value, gState.playerOwner);  // (nullptr for a unit with nothing to show)
        if (!b) return false;
        drawPaletteArt(c, *b, box);
        return true;
    }
    // Rectangle: the rows draw the Tiles list's pictures, which only the paints let go of; the picker's window paints
    // alone when it scrolls (the Tiles list does not), so its paints let go of them too.
    void paintStarts(QuickPickKind kind, Mode mode) override {
        if (kind == QuickPickKind::Map && mode == Mode::Terrain && tilesOn()) tilesPixelsTrim();
    }
    // After the event that picked (the window goes after it): close, then the pick.
    void closed(int v) override {
        const QuickPickKind kind = gQuickViewSdl.kind();
        const bool replace = gQuickViewSdl.replacing();
        shellPost([this, kind, replace, v] {
            close();
            quickPickRun(kind, replace, v);
            gState.dirty = true;
            invalidateMapView();
        });
    }
    void close() {
        if (!open || !gHost) return;
        open = false;
        gHost->closeWindow(gQuickViewSdl);
        if (gRoot) gRoot->focus();  // the keys go back to the window (the map takes them there)
    }
};
QuickPickSdl gQuickHostSdl;
// The quick picker's window is a pop-out that the system does not give the keyboard (macOS): its keys come to the main
// window, where its letters are typing, not the menus' one-letter keys (S: Sprites mode).
bool sdlQuickPickTyping() { return gQuickHostSdl.open; }

// Opens the picker (Q: the mode's things; replace: Replace with... for the selection; open: Quick open), or closes it
// when it is open.
// The quick picker with gQuickCustom's rows (the trigger editors' Quick add and search), in the middle of the window.
void showQuickPickerCustomSdl() {
    if (!gRoot || !gHost) return;
    if (gQuickHostSdl.open) return;  // (already open: a key and its text both ask for it)
    const draw::Point size = QuickPickView::windowSize();
    const draw::Rect v(0, 0, gRoot->width(), gRoot->height());
    const int x = v.left + (v.width() - size.x) / 2, y = v.top + std::max(0, (v.height() - size.y) / 3);
    gQuickViewSdl.start(gQuickHostSdl, QuickPickKind::Custom, gState.mode, false, false, true);
    gQuickHostSdl.open = true;
    gHost->openWindow(*gRoot, gQuickViewSdl, draw::WindowSpec{draw::WindowKind::Popup, draw::Rect(x, y, x + size.x, y + size.y), L""}, [] {
        gQuickHostSdl.open = false;  // a click outside closed it
    });
    gQuickViewSdl.field().focus();
}

void showQuickPickerSdl(bool replace, bool open) {
    if (!gRoot || !gHost) return;
    if (!open && (!gState.ed || !gState.ed->isOpen())) return;
    if (gQuickHostSdl.open) return gQuickHostSdl.close();
    const draw::Point size = QuickPickView::windowSize();
    const draw::Rect v = open || !gRoot->map.shown() ? draw::Rect(0, 0, gRoot->width(), gRoot->height()) : gRoot->map.frame();
    const int x = v.left + (v.width() - size.x) / 2, y = v.top + std::max(0, (v.height() - size.y) / 3);
    // In the Data workspace of a project: its entries (Go to a .dat entry), as the Windows program's; else the map's.
    const bool data = !open && gWorkspace == Workspace::Data && activeProject();
    gQuickViewSdl.start(gQuickHostSdl, open ? QuickPickKind::Open : data ? QuickPickKind::Data : QuickPickKind::Map, gState.mode, replace, false, true);
    gQuickHostSdl.open = true;
    gHost->openWindow(*gRoot, gQuickViewSdl, draw::WindowSpec{draw::WindowKind::Popup, draw::Rect(x, y, x + size.x, y + size.y), L""}, [] {
        gQuickHostSdl.open = false;  // a click outside closed it
    });
    gQuickViewSdl.field().focus();  // (at once: the keys typed before its first paint are its own)
}

// --- Snapshots (--snapshot with --dialog unitprops|locprops|prompt|quick) -------------------------------------------

// A dialog's view as the snapshot shows it: the dialog (its content and its buttons) at its own size, without a window.
// unitprops: the first unit of the map; locprops: the first location in use (else Anywhere); prompt: Rename location
// with the location's name; quick: the quick picker of the mode (--mode).
struct MapDialogShot {
    std::unique_ptr<UnitPropsView> unit;
    std::unique_ptr<LocationPropsView> loc;
    std::unique_ptr<TextPromptView> prompt;
    std::unique_ptr<MapImageView> image;  // mapimage: File > Export map image (core/map_image.inc)
    std::unique_ptr<MapImageProgressView> progress;  // mapimage-progress: its progress window, at part 3 of 16
    std::unique_ptr<RevealersView> revealers;        // revealers: Edit > Tools > Place map revealers
    stardraft::kit::Dialog dlg;
    draw::View* view = nullptr;
    draw::Point size;
};
bool mapDialogShot(const std::string& name, MapDialogShot& s) {
    if (!gState.ed || !gState.ed->isOpen()) return false;
    Editor& ed = *gState.ed;
    if (name == "unitprops" && !ed.units().empty()) {
        const Unit first = ed.units()[0];
        s.unit = std::make_unique<UnitPropsView>(ed, first, std::vector<int>{0}, draw::Image{}, activeProject() != nullptr);
        const int type = first.type, owner = first.owner;
        s.unit->paintIcon = [type, owner](draw::Canvas& c, draw::Rect box) {
            if (draw::Bitmap* b = paletteBitmap(Mode::Units, type, owner)) drawPaletteArt(c, *b, box);
        };
        s.size = unitPropsDialogSetup(s.dlg, *s.unit), s.view = &s.dlg;
        return true;
    }
    if ((name == "unitslist" || name.rfind("unitslist:", 0) == 0) && !ed.units().empty()) {  // the window with the Units list (View > Units list): the first selected unit
        // unitslist:move=<unit>:<before>: a move in the map's order first (one undo step, as a drop in the list gives it).
        if (name.find("move=") != std::string::npos) {
            int from = -1, before = -1;
            std::sscanf(name.c_str() + name.find("move=") + 5, "%d:%d", &from, &before);
            if (from >= 0 && from < int(ed.units().size()) && before >= 0) ed.moveUnits({from}, before);
        }
        // unitslist:nydus[:linked]: two Nydus canals placed for the links; both selected, or linked with the first selected.
        if (name.find("nydus") != std::string::npos) {
            const PlaceResult a = ed.placeUnit(134, 0, 40 * 32, 40 * 32, true), b = ed.placeUnit(134, 0, 48 * 32, 40 * 32, true);
            if (a.placed && b.placed) {
                if (name.find("linked") != std::string::npos) ed.linkUnits(a.index, b.index), ed.select(a.index, false);
                else ed.select(a.index, false), ed.select(b.index, true);
            }
        }
        const int u = ed.selection().empty() ? 0 : ed.selection().front();
        if (!ed.isSelected(u)) ed.select(u, false);
        const Unit first = ed.units()[size_t(u)];
        s.unit = std::make_unique<UnitPropsView>(ed, first, std::vector<int>{u}, draw::Image{}, activeProject() != nullptr);
        const int type = first.type, owner = first.owner;
        s.unit->paintIcon = [type, owner](draw::Canvas& c, draw::Rect box) {
            if (draw::Bitmap* b = paletteBitmap(Mode::Units, type, owner)) drawPaletteArt(c, *b, box);
        };
        unitPropsDialogSetup(s.dlg, *s.unit);
        // unitslist:<column>[:desc]: the list sorted by a column first (a snapshot of the order a header click gives).
        if (name.size() > 10) {
            const std::string opt = name.substr(10);
            static const char* const cols[5] = {"type", "owner", "x", "y", "index"};
            for (int k = 0; k < 5; ++k)
                if (opt.rfind(cols[k], 0) == 0) s.unit->setSort(k, opt.find(":desc") != std::string::npos);
        }
        s.size = unitWindowDefaultSize(), s.view = &s.dlg;  // (the window's default size, as a run opens it)
        return true;
    }
    int loc = kAnywhereLocation;
    for (int i = 0; i < ed.locationCount(); ++i)
        if (i != kAnywhereLocation && ed.locationUsed(i)) {
            loc = i;
            break;
        }
    if (name == "locprops") {
        s.loc = std::make_unique<LocationPropsView>(widen(ed.locationName(loc)), ed.locationEdges(loc), ed.locationElevation(loc), loc == kAnywhereLocation,
                                                    false);
        s.size = locPropsDialogSetup(s.dlg, *s.loc), s.view = &s.dlg;
        return true;
    }
    if (name == "prompt") {
        s.prompt = std::make_unique<TextPromptView>(widen(ed.locationName(loc)), false);
        s.dlg.setContent(*s.prompt);
        s.prompt->setDialog(&s.dlg);
        s.dlg.setInsets({0, 0, 0, 0, 0, 0});  // (as runTextPrompt: the view places its field and buttons)
        s.size = {scale(promptLayout::kW), scale(promptLayout::kH)}, s.view = &s.dlg;
        return true;
    }
    if (name == "revealers") {
        s.revealers = std::make_unique<RevealersView>();
        s.size = revealersDialogSetup(s.dlg, *s.revealers), s.view = &s.dlg;
        return true;
    }
    if (name == "mapimage") {
        Rect sel{};
        s.image = std::make_unique<MapImageView>(gMapImageLast, shellMapImageHd(), mapImageSelection(sel));
        s.size = mapImageDialogSetup(s.dlg, *s.image), s.view = &s.dlg;
        return true;
    }
    if (name == "mapimage-progress") {
        s.progress = std::make_unique<MapImageProgressView>(L"map.png");
        s.size = mapImageProgressSetup(s.dlg, *s.progress), s.view = &s.dlg;
        s.progress->setProgress(2, 16);
        std::printf("map image progress: %s, %d%%\n", narrow(s.progress->partText()).c_str(), int(s.progress->value() * 100 + 0.5f));
        return true;
    }
    if (name == "quick") {
        gQuickViewSdl.start(gQuickHostSdl, QuickPickKind::Map, gState.mode, false, false, false);
        s.size = QuickPickView::windowSize(), s.view = &gQuickViewSdl;
        return true;
    }
    return false;
}
