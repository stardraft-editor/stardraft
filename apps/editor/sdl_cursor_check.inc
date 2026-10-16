// --cursors (a snapshot run, after the picture): the cursors that the window shows at some points, as text lines (the
// system's cursor is not in a snapshot). The checks are those of the Windows self-test for GitHub feedback #76: the map
// view's cursor (core/map_input.inc: mapCursorAt) on the selected location's edges and corners and inside it, over a
// selected unit and over one that is not selected; and a kit view's (the palette's search field: the I-beam). The
// window's views give the cursor as the host asks for it (draw::Router::cursor; SdlHost::setCursor shows it).

const char* sdlCursorName(draw::Cursor c) {
    switch (c) {
    case draw::Cursor::Hand: return "hand";
    case draw::Cursor::IBeam: return "I-beam";
    case draw::Cursor::SizeAll: return "move";
    case draw::Cursor::SizeWE: return "width";
    case draw::Cursor::SizeNS: return "height";
    case draw::Cursor::SizeNWSE: return "diagonal (top left)";
    case draw::Cursor::SizeNESW: return "diagonal (top right)";
    case draw::Cursor::No: return "no";
    case draw::Cursor::Wait: return "wait";
    default: return "arrow";
    }
}

void sdlCursorChecks() {
    if (!gRoot) return;
    draw::Router router(*gRoot);
    auto report = [](const char* what, draw::Cursor got, draw::Cursor want) {
        std::printf("cursor: %s: %s %s\n", what, sdlCursorName(got), got == want ? "(yes)" : "(WRONG)");
    };
    auto atView = [&](draw::View& v, int x, int y) {
        const draw::Point o = v.origin();
        return router.cursor({o.x + x, o.y + y});
    };
    MapView& map = gRoot->map;
    auto atWorld = [&](float wx, float wy) {  // (the view at the point first: 200 px from the view's top left, at 100 %)
        gState.zoom = 1, gState.camX = wx - 200, gState.camY = wy - 200;
        return atView(map, int(std::lround((wx - gState.camX) * gState.zoom)), int(std::lround((wy - gState.camY) * gState.zoom)));
    };
    if (!gState.ed || !gState.ed->isOpen()) {
        std::printf("cursor: no map open: the map view's cursors are not checked\n");
    } else {
        Editor& ed = *gState.ed;
        // Locations: the first location of at least 64 x 64 pixels, selected.
        int loc = -1;
        for (int i = 0; i < ed.locationCount() && loc < 0; ++i)
            if (ed.locationUsed(i)) {
                const Rect r = ed.locationRect(i);
                if (r.right - r.left >= 64 && r.bottom - r.top >= 64) loc = i;
            }
        // A map with none: a new one of 256 x 160 pixels (a snapshot run saves nothing).
        if (loc < 0) loc = ed.createLocation(Rect{256, 256, 512, 416});
        if (loc >= 0) {
            setMode(Mode::Locations);
            gRoot->relayout();
            ed.selectLocation(loc);
            const Rect r = ed.locationRect(loc);
            const float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f;
            report("the location's left edge", atWorld(float(r.left), cy), draw::Cursor::SizeWE);
            report("the location's bottom edge", atWorld(cx, float(r.bottom)), draw::Cursor::SizeNS);
            report("the location's bottom right corner", atWorld(float(r.right), float(r.bottom)), draw::Cursor::SizeNWSE);
            report("the location's top right corner", atWorld(float(r.right), float(r.top)), draw::Cursor::SizeNESW);
            report("inside the location", atWorld(cx, cy), draw::Cursor::Arrow);
        } else {
            std::printf("cursor: no location could be made: the location cursors are not checked\n");
        }
        // Units: a selected unit (a press moves it) and one that is not selected (a click selects it).
        setMode(Mode::Units);
        gRoot->relayout();
        gState.palette = -1, gState.toolByMode[int(Mode::Units)] = UPlace;
        const auto& units = ed.units();
        int a = -1, b = -1;
        for (int i = 0; i < int(units.size()) && (a < 0 || b < 0); ++i)
            if (ed.unitAt(units[i].x, units[i].y) == i) (a < 0 ? a : b) = i;
        if (a >= 0 && b >= 0) {
            ed.select(a, false);
            report("a selected unit", atWorld(units[a].x, units[a].y), draw::Cursor::SizeAll);
            report("a unit that is not selected", atWorld(units[b].x, units[b].y), draw::Cursor::Arrow);
        } else {
            std::printf("cursor: the map has fewer than two units: the unit cursors are not checked\n");
        }
    }
    // A kit view: the palette's search field (shown in Units mode, and with no map: the start page has none).
    kit::TextField& field = gSearchSdl.field();
    if (field.shown() && field.width() > 0) report("the palette's search field", atView(field, field.width() / 2, field.height() / 2), draw::Cursor::IBeam);
    else std::printf("cursor: the palette's search field is not shown: not checked\n");
}
