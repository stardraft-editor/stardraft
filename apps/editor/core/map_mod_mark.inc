// The mark next to the mouse on the map (MapView::paintOver paints it): while a modifier key is down and a click or a
// drag there would do something else because of it, the icon of what the key does: Add puts in the selection, Remove
// takes out of it, Pipette picks. It follows mapMouseDown and mapMouseUp (core/map_input.inc); a new modifier there gets its mark here too.
// With no effect at that place (no selection to take out of, nothing under the pipette, a tool with no such key): no mark.

constexpr UiIcon kNoModMark = UiIcon::Count;

// Alt+click would take something (pipette): the same tests, with no change to the pick.
bool pipetteFinds(AppState& app, float wx, float wy) {
    Editor& ed = *app.ed;
    switch (app.mode) {
    case Mode::Terrain:
        if (app.terrainRect && !gTiles->solid) {
            const int tx = int(wx) / 32, ty = int(wy) / 32;
            return !gTiles->stamps && wx >= 0 && wy >= 0 && tx < ed.width() && ty < ed.height();
        }
        return ed.terrainAt(int(wx), int(wy)) != 0;
    case Mode::Units: return ed.unitAt(int(wx), int(wy)) >= 0;
    case Mode::Doodads: return ed.doodadAt(int(wx), int(wy)) >= 0;
    case Mode::Sprites: {
        const int i = mapSpriteAt(ed, int(wx), int(wy));
        return i >= 0 && (ed.sprites()[size_t(i)].flags & SpriteFlag::DrawAsSprite);
    }
    case Mode::Locations: return ed.locationAt(int(wx), int(wy)) >= 0;
    default: return false;
    }
}

// What Shift does to a shape tool (terrain_shapes.inc), or kNoModMark.
UiIcon shapeShiftMark(int tool) {
    switch (tool) {
    case TLine:
    case TPolygon: return UiIcon::Lock;
    case TRect:
    case TRectFill: return UiIcon::ShapeSquare;
    case TEllipse:
    case TEllipseFill: return UiIcon::ShapeCircle;
    case TDiamond:
    case TDiamondFill: return UiIcon::Diamond;
    case TRamp: return UiIcon::Erase;
    default: return kNoModMark;
    }
}

// A press in a selection of objects (selectItemAt, Units): on one, the key takes it in or out; on the ground, a box adds.
UiIcon itemSelectMark(bool hit, bool selected) { return hit && selected ? UiIcon::Remove : UiIcon::Add; }

// Rectangle's Select: a Ctrl click at the map point on a tile out of the selection picks that tile (mapMouseUp; a drag
// from it takes the tiles out of the selection).
bool ctrlClickPicksTile(const AppState& app, float wx, float wy) {
    return app.terrainRect && !gTiles->stamps && !gTiles->solid && !terrainSelHas(int(std::floor(wx / 32)), int(std::floor(wy / 32)));
}

// The drag under way: the keys that it reads while it goes on, and what its press chose (that stays after the key is up).
UiIcon dragModMark(AppState& app, float wx, float wy, bool shift, bool ctrl) {
    switch (app.drag) {
    case Drag::TerrainSelect:
        if (app.terrainSelOp == 2 && !app.dragMoved && ctrlClickPicksTile(app, app.dragWX, app.dragWY)) return UiIcon::Pipette;
        return app.terrainSelOp == 1 ? UiIcon::Add : app.terrainSelOp == 2 ? UiIcon::Remove : kNoModMark;
    case Drag::BoxSelect: return shift || ctrl ? UiIcon::Add : kNoModMark;
    case Drag::Tiles: return gTiles->scatter && !gTilesPasteDrag && tilesStampOn() ? UiIcon::Jumble : kNoModMark;
    case Drag::Shape:
        if (gShape.tool == TRamp) return gShape.unramp ? shapeShiftMark(TRamp) : kNoModMark;
        return shift ? shapeShiftMark(gShape.tool) : kNoModMark;
    case Drag::LocCreate: {
        const Rect r = previewLocationRect(wx, wy);
        if (r.right - r.left >= 8 && r.bottom - r.top >= 8) return shift || ctrl ? UiIcon::MirrorTopBottom : kNoModMark;
        return app.locToggle >= 0 && app.ed->locationUsed(app.locToggle) ? itemSelectMark(true, app.ed->locationSelected(app.locToggle)) : kNoModMark;
    }
    default: return kNoModMark;
    }
}

UiIcon terrainModMark(AppState& app, float wx, float wy, bool shift, bool ctrl) {
    const Editor& ed = *app.ed;
    const int tool = currentTool(app);
    if (tool == TMoveSel || tool == TErase) return kNoModMark;
    if (isShapeTool(tool) && !(ctrl && !gTiles->stamps)) {
        if (!shift) return kNoModMark;
        if (tool == TBridge || tool == TRamp || !app.terrainRect) {
            if (!ed.canPaintTerrain() || (tool != TBridge && tool != TRamp && app.palette <= 0)) return kNoModMark;
        } else if (!tilesHavePick()) {
            return kNoModMark;
        }
        return shapeShiftMark(tool);
    }
    if (tool == TWand || tool == TLasso) return ctrl ? (app.hasTerrainSel ? UiIcon::Remove : kNoModMark) : shift ? UiIcon::Add : kNoModMark;
    if (app.terrainRect && !(gTiles->solid && ctrl && tool != TSelect)) {  // (tilesMouseDown)
        if (tool == TSelect) {
            if (app.terrainPasting && ed.hasTileClipboard()) return kNoModMark;
            if (shift) return UiIcon::Add;
            if (!ctrl) return kNoModMark;
            if (ctrlClickPicksTile(app, wx, wy)) return UiIcon::Pipette;
            return app.hasTerrainSel ? UiIcon::Remove : kNoModMark;
        }
        if (ctrl && !gTiles->stamps) return UiIcon::Pipette;  // (Custom: Ctrl does nothing, Shift still does)
        if (!shift || !tilesHavePick()) return kNoModMark;
        if (tool == TFill) return UiIcon::SelectAll;
        return tool == TBrush && tilesStampOn() ? UiIcon::Jumble : kNoModMark;
    }
    if (!ed.canPaintTerrain()) return kNoModMark;
    if (tool == TSelect && !app.terrainPasting) {
        if (shift) return UiIcon::Add;
        if (ctrl && app.hasTerrainSel) return UiIcon::Remove;
    }
    if (ctrl) return UiIcon::Pipette;
    return tool == TFill && app.palette > 0 && ed.terrainAt(int(wx), int(wy)) != uint16_t(app.palette) ? UiIcon::SelectAll : kNoModMark;
}

bool mapBrushActive(const AppState& app);  // (core/map_input.inc)

// What Alt + wheel does at (wx, wy) (mapWheel, also with Ctrl or Shift): the palette's next doodad or the brush size (a
// value up or down), or the tile's next variation (Terrain with no brush, a tile with no doodad on it, the layer shown,
// no paste). kNoModMark: nothing.
UiIcon altWheelMark(AppState& app, float wx, float wy) {
    if ((app.mode == Mode::Doodads && !objectSprayOn() && app.palette >= 0) || mapBrushActive(app)) return UiIcon::Sort;
    if (app.mode != Mode::Terrain || modeLayerHidden() || app.pasting || wx < 0 || wy < 0) return kNoModMark;
    const int tx = int(wx) / 32, ty = int(wy) / 32;
    if (app.ed->doodadAt(tx * 32 + 16, ty * 32 + 16) >= 0) return kNoModMark;  // (setTileVariation does not change it)
    return tileVariations(*app.ed, mapTileAt(*app.ed, tx, ty)).subs.size() > 1 ? UiIcon::Shuffle : kNoModMark;
}

// The mark at the point (sx, sy) of the map view with the keys `mods` down (draw::Mods). kNoModMark: none.
UiIcon mapModMark(AppState& app, int sx, int sy, unsigned mods) {
    if (!app.ed || !app.ed->isOpen()) return kNoModMark;
    Editor& ed = *app.ed;
    const bool shift = mods & draw::kShift, ctrl = mods & draw::kCtrl, alt = mods & draw::kAlt;
    float wx, wy;
    toWorld(app, sx, sy, wx, wy);
    if (app.drag != Drag::None) return dragModMark(app, wx, wy, shift, ctrl);
    if (!shift && !ctrl && !alt) return kNoModMark;
    // (off the map: the tools of Terrain have no tile, only Alt + wheel's brush size; a box of objects can start there)
    if (app.mode == Mode::Terrain && (wx < 0 || wy < 0 || wx >= ed.width() * 32.0f || wy >= ed.height() * 32.0f))
        return alt && mapBrushActive(app) ? UiIcon::Sort : kNoModMark;
    if (alt) {
        // (Ctrl + Alt and Shift + Alt: the wheel's mark first, as their click is Alt's or Ctrl's own; Alt alone: the
        // click's mark first, then the wheel's)
        if (shift || ctrl)
            if (const UiIcon w = altWheelMark(app, wx, wy); w != kNoModMark) return w;
        if (app.mode == Mode::Units && app.palette < 0 && !app.pasting && !modeLayerHidden() && ed.unitsAt(int(wx), int(wy)).size() > 1) return UiIcon::Log;
        if (pipetteFinds(app, wx, wy)) return app.mode == Mode::Locations ? UiIcon::Copy : UiIcon::Pipette;
        if (const UiIcon w = altWheelMark(app, wx, wy); w != kNoModMark) return w;  // (the click does nothing: the wheel's)
    }
    if (modeLayerHidden() || trigUnitPickActive() || mapLayersEraseOn(app) || app.mode == Mode::Layers || app.pasting) return kNoModMark;
    if (objectEraseOn() && !app.pastingItems) return kNoModMark;
    const int tool = currentTool(app);
    switch (app.mode) {
    case Mode::Units: {
        if ((!shift && !ctrl) || tool == UFill || tool == USpray || tool == UReplace || app.palette >= 0) return kNoModMark;
        const int hit = ed.unitAt(int(wx), int(wy));
        if (hit >= 0 && shift && !ctrl && ed.isSelected(hit)) return kNoModMark;  // (Shift keeps a selected unit; only Ctrl takes it out)
        return itemSelectMark(hit >= 0, hit >= 0 && ed.isSelected(hit));
    }
    case Mode::Terrain: return shift || ctrl ? terrainModMark(app, wx, wy, shift, ctrl) : kNoModMark;
    case Mode::Doodads: {
        if ((!shift && !ctrl) || tool == DFill || tool == DSpray || app.palette >= 0 || app.pastingItems) return kNoModMark;
        const int hit = ed.doodadAt(int(wx), int(wy));
        return itemSelectMark(hit >= 0, std::find(app.selDoodads.begin(), app.selDoodads.end(), hit) != app.selDoodads.end());
    }
    case Mode::Sprites: {
        if ((!shift && !ctrl) || app.palette >= 0 || app.pastingItems) return kNoModMark;
        const int hit = mapSpriteAt(ed, int(wx), int(wy));
        return itemSelectMark(hit >= 0, std::find(app.selSprites.begin(), app.selSprites.end(), hit) != app.selSprites.end());
    }
    case Mode::Locations: {
        if (app.pastingItems) return kNoModMark;
        const bool adds = shift || ctrl;
        const int edges = locationEdgesAt(wx, wy), inside = ed.locationAt(int(wx), int(wy));
        if (alt && !edges) return adds ? UiIcon::Add : UiIcon::Select;
        if (!adds) return kNoModMark;
        return inside >= 0 ? itemSelectMark(true, ed.locationSelected(inside)) : UiIcon::MirrorTopBottom;
    }
    default: return kNoModMark;
    }
}
