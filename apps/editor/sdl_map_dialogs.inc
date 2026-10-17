// The map's dialogs and menus on this system: Unit properties, Location properties and the text prompt (core/views_unit_props.inc,
// core/views_location_props.inc, core/views_text_prompt.inc) as modal kit dialogs over the window (Host::runModal), and
// the map's right-click menu (core/views_map_menu.inc) as a kit Menu. The Windows hosts are main.cpp's showUnitProperties,
// showLocationEdit, prompt and mapMenu. Included by main_sdl.cpp after sdl_options.inc.

// The prompt (Rename location, a sound's name): OK puts the typed text into `text` and returns true.
bool prompt(const wchar_t* title, std::wstring& text) {
    if (!gRoot) return false;
    TextPromptView* shown = nullptr;
    return runTextPrompt(*gRoot, title, text, true, shown);
}

// Unit properties of unit `index` (and the rest of the selection, when it is part of it), as on Windows.
void showUnitProperties(int index) {
    if (!gRoot || !gState.ed || index < 0 || index >= int(gState.ed->units().size())) return;
    if (!gState.ed->isSelected(index)) gState.ed->clearSelection(), gState.ed->select(index, false);
    const std::vector<int> units = gState.ed->selection();
    const Unit first = gState.ed->units()[size_t(index)];
    gState.dirty = true;
    UnitPropsView* shown = nullptr;
    const int type = first.type, owner = first.owner;
    runUnitPropsDialog(*gRoot, *gState.ed, units, first, draw::Image{}, false, shown, [type, owner](draw::Canvas& c, draw::Rect box) {
        if (draw::Bitmap* b = paletteBitmap(Mode::Units, type, owner)) drawPaletteArt(c, *b, box);  // (the palette's picture)
    });
    updateTitle();
    gState.dirty = true;
    invalidateMapView();
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
        const int u = ed.unitAt(int(wx), int(wy));
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
        draw::Bitmap* b = paletteBitmap(mode, u.value, gState.playerOwner);  // (nullptr for a unit with nothing to show)
        if (!b) return false;
        drawPaletteArt(c, *b, box);
        return true;
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

// Opens the picker (Q: the mode's things; replace: Replace with... for the selection; open: Quick open), or closes it
// when it is open.
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
    stardraft::kit::Dialog dlg;
    draw::View* view = nullptr;
    draw::Point size;
};
bool mapDialogShot(const std::string& name, MapDialogShot& s) {
    if (!gState.ed || !gState.ed->isOpen()) return false;
    Editor& ed = *gState.ed;
    if (name == "unitprops" && !ed.units().empty()) {
        const Unit first = ed.units()[0];
        s.unit = std::make_unique<UnitPropsView>(first, std::vector<int>{0}, draw::Image{}, false);
        const int type = first.type, owner = first.owner;
        s.unit->paintIcon = [type, owner](draw::Canvas& c, draw::Rect box) {
            if (draw::Bitmap* b = paletteBitmap(Mode::Units, type, owner)) drawPaletteArt(c, *b, box);
        };
        s.size = unitPropsDialogSetup(s.dlg, *s.unit), s.view = &s.dlg;
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
    if (name == "quick") {
        gQuickViewSdl.start(gQuickHostSdl, QuickPickKind::Map, gState.mode, false, false, false);
        s.size = QuickPickView::windowSize(), s.view = &gQuickViewSdl;
        return true;
    }
    return false;
}
