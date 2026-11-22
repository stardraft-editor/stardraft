// The Map properties page (portable, core.inc): the map's title and description (the program's game text editors, in the
// boxes this view gives them), the tileset, the size, where the map's content goes and the new ground (kit dropdowns),
// and the two buttons under them, "Change the size and tileset" and "Reset", with the text that says what they do; "Crop
// or extend" beside the size opens the size on a picture of the map (views_resize.inc). The
// players and forces under the page are another view (map_pages.inc). The values are the page's working copy
// (MapPropsValues); the program puts a change into the map (MapPropsHost), and the page shows what the copy holds.

// The sizes StarEdit offers (also New map) and the anchor names of "Map content at".
const uint16_t kMapSizes[] = {64, 96, 128, 192, 256};
const uint16_t kHyperionMapSizes[] = {64, 96, 128, 192, 256, 384, 512};  // (Hyperion's: up to 512 a side for now)
// The sizes offered: StarEdit's, and with the Hyperion plugin 384 and 512 too (core/hyperion.inc); the largest side.
std::span<const uint16_t> mapSizes() {
    if (hyperionMode()) return kHyperionMapSizes;
    return kMapSizes;
}
int maxMapSide() { return mapSizes().back(); }
// A size dropdown's item (its number hidden: a typed size finds it) and the typing of a size: the open list's search
// row takes a whole number, and one that is no item's gives a row of its own (16 to the largest side, as Crop or extend
// and New map take). `pick` gets the typed size.
stardraft::kit::DropdownItem mapSizeItem(int s) {
    stardraft::kit::DropdownItem it{std::to_wstring(s), -1, s};
    it.numberHidden = true;
    return it;
}
void mapSizeTyping(stardraft::kit::Dropdown& d, std::function<void(int)> pick) {
    d.searchable = true, d.numberMax = maxMapSide();
    d.numberItem = [](int n) { return mapSizeItem(n); };
    d.onNumber = [pick = std::move(pick)](int n) { pick(std::clamp(n, 16, maxMapSide())); };
}
const wchar_t* kAnchorNames[] = {L"Top left", L"Top", L"Top right", L"Left", L"Center", L"Right", L"Bottom left", L"Bottom", L"Bottom right"};

// The first terrain of a tileset (the default terrain of new ground).
inline uint16_t mapPropsFirstTerrain(int era) {
    if (const IsomTerrainTable* t = IsomTerrainTable::forEra(uint16_t(era)))
        for (uint16_t i = 1; i <= t->terrainCount(); ++i)
            if (t->isTerrain(i)) return i;
    return 1;
}

// The page's values: the working copy (the program's copy is a MapPropsDialog, which holds these and its editors).
struct MapPropsValues {
    std::string nameText, descText;  // the title and the description as bytes (the game's codes)
    std::string notesText, notesBase;  // the map's notes (UTF-8)
    int era = 0, w = 0, h = 0, anchorIndex = 0;
    uint16_t fillTerrain = 1;
    std::wstring infoText;  // what "Change the size and tileset" will do ("": the size and tileset are the map's)
    // What the fields showed when the page first showed the map's values: a change is a difference from these.
    bool baseSet = false;
    std::string nameBase, descBase;
    int eraBase = 0, wBase = 0, hBase = 0;
};

// The program's part of the page (map_pages.inc: MapPropsProgram): the map's changes and the editors' heights.
class MapPropsHost {
public:
    virtual ~MapPropsHost() = default;
    // A value changed by the page (a dropdown): the program updates the text under the buttons and the page's change.
    virtual void changed() = 0;
    virtual void applyTerrain() = 0;  // "Change the size and tileset"
    virtual void reset() = 0;         // "Reset": the map's values again
    virtual void cropOrExtend() = 0;  // "Crop or extend": the size and the offset on a picture of the map (a dialog)
    virtual void resizeOnMap() = 0;   // "Resize on the map": Edit > Resize map (the map's edges, dragged on the map)
    // The height of a game text editor (its box of boxH, and its tools under it) at boxW.
    virtual int textHeight(bool description, int boxW, int boxH) = 0;
    // The page's content is w x h (the program sizes the window it is in).
    virtual void fitHeight(int w, int h) = 0;
    // The width that the page can use (Terrain goes beside Map where both fit). 0: not known.
    virtual int availableWidth() { return 0; }
};

class MapPropsView : public draw::View {
public:
    // Terrain: the tileset (a change goes into the map at once: one undo step) and the size, which changes on the map
    // (Edit > Resize map) or on a picture of it (Crop or extend).
    MapPropsView(MapPropsHost& host, MapPropsValues& v) : host_(host), v_(v) {
        tileset_.onChange = [this](int i) {
            if (i == v_.era) return;
            v_.era = i;
            v_.w = v_.wBase, v_.h = v_.hBase, v_.anchorIndex = 0;  // (the size stays)
            v_.fillTerrain = mapPropsFirstTerrain(i);  // the new tileset's first terrain
            edited();
            host_.applyTerrain();
        };
        std::vector<stardraft::kit::DropdownItem> names;
        for (int e = 0; e < 8; ++e) names.push_back({kTilesetNames[e], -1, -1});
        tileset_.setItems(names);
        tileset_.setTip(L"The map's tileset: a change goes into the map at once (one undo step)");
        tileset_.wheelSteps = false;  // (each step would be a change of the map's tileset: the list or the keys only)
        crop_.setText(L"Crop or extend\x2026");
        crop_.setTip(L"Change the size on a picture of the map: drag the edges of a rectangle (one undo step)");
        crop_.onClick = [this] { host_.cropOrExtend(); };
        onMap_.setText(L"Resize on the map");
        onMap_.setTip(L"Edit > Resize map: drag the map's edges on the map itself (one undo step)");
        onMap_.onClick = [this] { host_.resizeOnMap(); };
        add(tileset_, {0, 0, 0, 0}), add(crop_, {0, 0, 0, 0}), add(onMap_, {0, 0, 0, 0});
        syncControls();
    }

    // The width of the page (its labels, the fields and the buttons' row).
    int preferredWidth() const { return scale(stardraft::kit::kDialogPad) * 2 + scale(120) + scale(440); }
    // The two game text editors' boxes, in the page's coordinates (their tools are under the box: the host's height).
    draw::Rect nameBox() {
        const PropsRows l = rows();
        return {l.x1, l.nameY, l.x1 + l.boxW, l.nameY + l.nameH};
    }
    draw::Rect descBox() {
        const PropsRows l = rows();
        return {l.x1, l.descY, l.x1 + l.boxW, l.descY + l.descH};
    }
    draw::Rect notesBox() {
        const PropsRows l = rows();
        return {l.x1, l.notesY, l.x1 + l.boxW, l.notesY + l.notesH};
    }
    int baseHeight() { return rows().height; }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        syncControls();  // the tileset shows the copy's value
        const PropsRows l = rows();
        if (height() != l.height || width() != l.width) host_.fitHeight(l.width, l.height);
        const int right = l.x1 + l.boxW, tx = l.tx0, tRight = l.tx1 + l.boxW;
        auto heading = [&](const wchar_t* t, int y, int x0, int x1) {
            c.text({x0, y, x1, y + l.rowH}, t, kText, draw::kMiddle);
            c.fill({x0, y + l.rowH, x1, y + l.rowH + 1}, kBorder);
        };
        auto label = [&](const wchar_t* t, int y, int x0, int x1) { c.text({x0, y, x1 - scale(8), y + l.rowH}, t, kDim, draw::kMiddle); };
        heading(L"Map", l.pad, l.pad, right);
        // (the editors are the program's windows, in their boxes)
        label(L"Title", l.nameY, l.pad, l.x1), label(L"Description", l.descY, l.pad, l.x1), label(L"Notes", l.notesY, l.pad, l.x1);
        heading(L"Terrain", l.terrainY, tx, tRight);
        label(L"Tileset", l.tilesetY, tx, l.tx1), label(L"Size (tiles)", l.sizeY, tx, l.tx1);
        tileset_.setFrame({l.tx1, l.tilesetY, tRight, l.tilesetY + l.rowH});
        const std::wstring size = std::to_wstring(v_.wBase) + L" x " + std::to_wstring(v_.hBase);
        const int sizeW = scale(96);
        c.text({l.tx1, l.sizeY, l.tx1 + sizeW, l.sizeY + l.rowH}, size, kText, draw::kMiddle);
        const int cropX = l.tx1 + sizeW, cw = crop_.preferredWidth(c);
        crop_.setFrame({cropX, l.sizeY, cropX + cw, l.sizeY + l.rowH});
        const int mapX = cropX + cw + scale(stardraft::kit::kButtonGap);
        onMap_.setFrame({mapX, l.sizeY, mapX + onMap_.preferredWidth(c), l.sizeY + l.rowH});
    }

private:
    struct PropsRows {
        int pad, rowH, pitch, x1, boxW, right, tx0, tx1, width, nameY, nameH, descY, descH, notesY, notesH, terrainY, tilesetY, sizeY, height;
    };
    PropsRows rows() {
        PropsRows l{};
        l.pad = scale(stardraft::kit::kDialogPad), l.rowH = scale(stardraft::kit::kFieldH), l.pitch = l.rowH + scale(8);
        l.boxW = scale(440), l.x1 = l.pad + scale(120), l.right = l.x1 + l.boxW;
        l.nameY = l.pad + l.pitch;  // under the "Map" heading (each editor: its box, then its colors under it)
        l.nameH = host_.textHeight(false, l.boxW, l.rowH);
        l.descY = l.nameY + l.nameH + scale(10), l.descH = host_.textHeight(true, l.boxW, scale(96));
        l.notesY = l.descY + l.descH + scale(10), l.notesH = scale(96);
        // Terrain: beside Map where both fit, else under it.
        const int oneW = l.right + l.pad, twoW = l.right + 2 * l.pad + (l.x1 - l.pad) + l.boxW + l.pad;
        const bool beside = host_.availableWidth() >= twoW;
        l.width = beside ? twoW : oneW;
        l.tx0 = beside ? l.right + 2 * l.pad : l.pad, l.tx1 = l.tx0 + (l.x1 - l.pad);
        const int mapEnd = l.notesY + l.notesH;
        l.terrainY = beside ? l.pad : mapEnd + l.pad;
        l.tilesetY = l.terrainY + l.pitch;
        l.sizeY = l.tilesetY + l.pitch;
        l.height = std::max(l.sizeY + l.rowH, beside ? mapEnd : 0) + l.pad;
        return l;
    }
    // A value changed by the page: the program takes it (the copy), then the page again.
    void edited() {
        syncControls();
        host_.changed();
        invalidate();
    }
    void syncControls() { tileset_.setValue(v_.era & 7); }

    MapPropsHost& host_;
    MapPropsValues& v_;
    stardraft::kit::Dropdown tileset_;
    stardraft::kit::Button crop_, onMap_;
};
