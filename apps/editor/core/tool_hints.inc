// The key hints of the map (StatusView::paint paints them in the status bar, View > Show key
// hints): what the modifier keys and the wheel do with the tool that is on, before a key is down. The mark at the mouse
// (core/map_mod_mark.inc) shows what a held key does at that place; the hints say which keys there are. They follow
// mapMouseDown, mapMouseUp and mapWheel (core/map_input.inc): a new modifier there gets its hint here too.

struct KeyHint {
    const wchar_t* keys;  // the key or the keys with the mouse ("Shift+drag")
    const wchar_t* what;  // what they do, in a few words
    unsigned mod = 0;     // the modifier of the keys (draw::kShift, kCtrl, kAlt): the hint is bright while it is down
};

// The hints for the mode, the tool and the pick that are on now: the most useful first, five at most.
std::vector<KeyHint> mapKeyHints(const AppState& app) {
    std::vector<KeyHint> out;
    if (!app.ed || !app.ed->isOpen() || app.mode == Mode::Layers) return out;
    if (mapPeekOn(app)) return {{L"Release", L"Go to the place at the mouse"}};  // (the overview: core/map_input.inc)
    const int tool = currentTool(app);
    auto add = [&](const wchar_t* keys, const wchar_t* what, unsigned mod = 0) {
        if (out.size() < 5) out.push_back({keys, what, mod});
    };
    switch (app.mode) {
    case Mode::Terrain: {
        const bool tiles = app.terrainRect;  // (Rectangle: tiles; else the terrain brushes)
        if (tool == TMoveSel || tool == TErase) break;
        if (tool == TLine || tool == TPolygon) add(L"Shift", L"Lock angle", draw::kShift);
        else if (tool == TRect || tool == TRectFill) add(L"Shift", L"Square", draw::kShift);
        else if (tool == TEllipse || tool == TEllipseFill) add(L"Shift", L"Circle", draw::kShift);
        else if (tool == TDiamond || tool == TDiamondFill) add(L"Shift", L"Equal sides", draw::kShift);
        else if (tool == TRamp) add(L"Shift+drag", L"Remove ramp", draw::kShift);
        else if (tool == TSelect || tool == TWand || tool == TLasso) {
            add(L"Shift", L"Add to selection", draw::kShift);
            add(L"Ctrl", L"Remove from selection", draw::kCtrl);
        } else if (tool == TFill) {
            add(L"Shift+click", L"Fill all of this terrain", draw::kShift);
        } else if (tool == TBrush && tiles && tilesStampOn()) {
            add(L"Shift+drag", L"Jumble", draw::kShift);
        }
        if (tool != TSelect && tool != TWand && tool != TLasso && !(tiles && gTiles->stamps))
            add(L"Ctrl+click", tiles && !gTiles->solid ? L"Use this tile" : L"Use this terrain", draw::kCtrl);  // (Solid: its terrain)
        if (tool == TBrush || tool == TSpray || tool == TJumble) add(L"Alt+wheel", L"Brush size", draw::kAlt);
        if (!tiles) add(L"Space", L"Terrain palette");
        break;
    }
    case Mode::Units:
        if (app.palette < 0 && tool == UPlace) {
            add(L"Shift", L"Add to selection", draw::kShift);
            add(L"Ctrl", L"Add or remove", draw::kCtrl);
        }
        add(L"Alt+click", L"Use this unit", draw::kAlt);
        if (tool == USpray) add(L"Alt+wheel", L"Brush size", draw::kAlt);
        break;
    case Mode::Doodads:
        if (app.palette < 0 && tool == DPlace) add(L"Shift+click", L"Add or remove", draw::kShift);  // (selectItemAt: Ctrl too)
        add(L"Alt+click", L"Use this doodad", draw::kAlt);
        if (objectSprayOn()) add(L"Alt+wheel", L"Brush size", draw::kAlt);
        else if (app.palette >= 0) add(L"Alt+wheel", L"Next doodad", draw::kAlt);
        break;
    case Mode::Sprites:
        if (app.palette < 0) add(L"Shift+click", L"Add or remove", draw::kShift);
        add(L"Alt+click", L"Use this sprite", draw::kAlt);
        break;
    case Mode::Locations:
        if (tool == LErase) break;
        add(L"Shift+drag", L"Inverted location", draw::kShift);
        add(L"Alt+drag", L"Select in a box", draw::kAlt);
        add(L"Shift+click", L"Add or remove", draw::kShift);
        break;
    default: break;
    }
    add(L"Q", L"Quick picker");
    if (app.mode == Mode::Units || app.mode == Mode::Doodads || app.mode == Mode::Sprites) add(L"Space", L"Pie palette");  // (held)
    add(L"Ctrl+Space", L"Overview");  // (held: mapPeekStart)
    add(L"Right-click", inSelection() ? L"Menu" : L"Stop");  // (mapRightClickSdl: placing or painting stops first)
    return out;
}

// The overview's outline (mapPeekStart): the view that letting go of Space gives, round the mouse.
void paintMapPeek(draw::Canvas& c, const AppState& app, int viewW, int viewH) {
    if (!mapPeekOn(app) || !app.mouseIn || app.zoom <= 0 || gMapPeek.zoom <= 0) return;
    const int hw = std::max(scale(6), int(viewW * app.zoom / gMapPeek.zoom / 2)), hh = std::max(scale(6), int(viewH * app.zoom / gMapPeek.zoom / 2));
    if (hw * 2 >= viewW && hh * 2 >= viewH) return;  // (the view from before showed all of this)
    c.roundBox({app.mouseX - hw, app.mouseY - hh, app.mouseX + hw, app.mouseY + hh}, draw::kNone, kAccent, float(scale(3)), std::max(1, scale(2)));
}

// The hints in the status bar (StatusView::paint gives the room that its parts leave): each hint is its keys in a key
// cap, then its text, all in the middle of the room. `mods`: the modifier keys that are down (their hints are bright,
// the others dim). A narrow room shows the hints that fit, from the first.
void paintKeyHints(draw::Canvas& c, const AppState& app, draw::Rect room, unsigned mods) {
    const std::vector<KeyHint> hints = mapKeyHints(app);
    if (hints.empty()) return;
    const int h = room.bottom - room.top, capH = std::min(scale(18), h - scale(4)), gap = scale(12), capPad = scale(5);
    struct Item { std::wstring keys, what; int capW, textW; bool on; };
    std::vector<Item> items;
    int w = 0;
    for (const KeyHint& k : hints) {
        Item it{shellKeysText(k.keys), tr(k.what), 0, 0, k.mod && (mods & k.mod)};
        it.capW = c.measure(it.keys) + 2 * capPad, it.textW = c.measure(it.what);
        const int iw = it.capW + scale(5) + it.textW + (items.empty() ? 0 : gap);
        if (w + iw > room.right - room.left) break;
        w += iw;
        items.push_back(std::move(it));
    }
    if (items.empty()) return;
    const bool anyOn = std::any_of(items.begin(), items.end(), [](const Item& i) { return i.on; });
    int x = room.left + (room.right - room.left - w) / 2;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        if (i) x += gap;
        const draw::Rect cap(x, room.top + (h - capH) / 2, x + it.capW, room.top + (h - capH) / 2 + capH);
        c.roundBox(cap, it.on ? kSel : kCtl, it.on ? kAccentLine : kBorderStrong, float(scale(3)));
        c.text(cap, it.keys, it.on ? kAccent : kText, draw::kCenter | draw::kMiddle);
        x += it.capW + scale(5);
        c.text({x, room.top, x + it.textW + 1, room.bottom}, it.what, it.on || !anyOn ? kText : kDim, draw::kMiddle);
        x += it.textW;
    }
}

// The hints that the status bar shows are not the ones of the call before (or they went on or off): the bar is drawn again.
bool keyHintsChanged(const AppState& app) {
    static std::vector<const wchar_t*> last;
    std::vector<const wchar_t*> now;
    if (app.keyHints)
        for (const KeyHint& h : mapKeyHints(app)) now.push_back(h.keys), now.push_back(h.what);
    if (now == last) return false;
    last = std::move(now);
    return true;
}
