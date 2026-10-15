// The Map properties page (portable, core.inc): the map's title and description (the program's game text editors, in the
// boxes this view gives them), the tileset, the size, where the map's content goes and the new ground (kit dropdowns),
// and the two buttons under them, "Change the size and tileset" and "Reset", with the text that says what they do. The
// players and forces under the page are another view (map_pages.inc). The values are the page's working copy
// (MapPropsValues); the program puts a change into the map (MapPropsHost), and the page shows what the copy holds.

// The sizes StarEdit offers (also New map) and the anchor names of "Map content at".
const uint16_t kMapSizes[] = {64, 96, 128, 192, 256};
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
    // The height of a game text editor (its box of boxH, and its tools under it) at boxW.
    virtual int textHeight(bool description, int boxW, int boxH) = 0;
    // The page's content is w x h (the program sizes the window it is in).
    virtual void fitHeight(int w, int h) = 0;
};

class MapPropsView : public draw::View {
public:
    MapPropsView(MapPropsHost& host, MapPropsValues& v) : host_(host), v_(v) {
        tileset_.onChange = [this](int i) {
            v_.era = i;
            v_.fillTerrain = mapPropsFirstTerrain(i);  // the new tileset's first terrain
            edited();
        };
        wDrop_.onChange = [this](int i) { v_.w = wVals_[size_t(i)], edited(); };
        hDrop_.onChange = [this](int i) { v_.h = hVals_[size_t(i)], edited(); };
        anchor_.onChange = [this](int i) { v_.anchorIndex = i, edited(); };
        fill_.onChange = [this](int i) { v_.fillTerrain = fillIds_[size_t(i)], edited(); };
        std::vector<stardraft::kit::DropdownItem> names;
        for (int e = 0; e < 8; ++e) names.push_back({kTilesetNames[e], -1, -1});
        tileset_.setItems(names);
        std::vector<stardraft::kit::DropdownItem> anchors;
        for (int k = 0; k < 9; ++k) anchors.push_back({kAnchorNames[k], -1, -1});
        anchor_.setItems(anchors);
        change_.setText(L"Change the size and tileset"), change_.setTone(stardraft::kit::Tone::Primary);
        change_.setTip(L"Set the new size and tileset in the map (one undo step)");
        change_.onClick = [this] { host_.applyTerrain(); };
        reset_.setText(L"Reset"), reset_.setTip(L"Discard the changes that are not applied and show the map's values");
        reset_.onClick = [this] { host_.reset(); };
        for (stardraft::kit::Dropdown* d : {&tileset_, &wDrop_, &hDrop_, &anchor_, &fill_}) add(*d, {0, 0, 0, 0});
        add(change_, {0, 0, 0, 0}), add(reset_, {0, 0, 0, 0});
        syncControls();
    }

    // The width of the page (its labels, the fields and the buttons' row).
    int preferredWidth() const { return scale(stardraft::kit::kDialogPad) * 2 + scale(120) + scale(440); }
    // The two game text editors' boxes, in the page's coordinates (their tools are under the box: the host's height).
    draw::Rect nameBox() {
        const PropsRows l = rows(0);
        return {l.x1, l.nameY, l.x1 + l.boxW, l.nameY + l.nameH};
    }
    draw::Rect descBox() {
        const PropsRows l = rows(0);
        return {l.x1, l.descY, l.x1 + l.boxW, l.descY + l.descH};
    }
    // The page's height with no text under the buttons (its first paint makes it as tall as the text needs).
    int baseHeight() { return rows(0).height; }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        syncControls();  // the dropdowns show the copy's values
        const int textH = v_.infoText.empty() ? 0 : c.measureBox(v_.infoText, scale(440), draw::kWrap).y + scale(4);
        const PropsRows l = rows(textH);
        if (height() != l.height) host_.fitHeight(width(), l.height);  // the text under the buttons is taller or shorter
        const int right = l.x1 + l.boxW;
        auto heading = [&](const wchar_t* t, int y) {
            c.text({l.pad, y, right, y + l.rowH}, t, kText, draw::kMiddle);
            c.fill({l.pad, y + l.rowH, right, y + l.rowH + 1}, kBorder);
        };
        auto label = [&](const wchar_t* t, int y) { c.text({l.pad, y, l.x1 - scale(8), y + l.rowH}, t, kDim, draw::kMiddle); };
        heading(L"Map", l.pad);
        label(L"Title", l.nameY), label(L"Description", l.descY);  // (the editors are the program's windows, in their boxes)
        heading(L"Terrain", l.terrainY);
        label(L"Tileset", l.tilesetY), label(L"Size (tiles)", l.sizeY), label(L"Map content at", l.anchorY), label(L"New ground", l.fillY);
        const int sw = scale(96);
        place(tileset_, {l.x1, l.tilesetY, right, l.tilesetY + l.rowH});
        place(wDrop_, {l.x1, l.sizeY, l.x1 + sw, l.sizeY + l.rowH});
        c.text({l.x1 + sw, l.sizeY, l.x1 + sw + scale(24), l.sizeY + l.rowH}, L"x", kDim, draw::kMiddle | draw::kCenter);
        place(hDrop_, {l.x1 + sw + scale(24), l.sizeY, l.x1 + sw + scale(24) + sw, l.sizeY + l.rowH});
        place(anchor_, {l.x1, l.anchorY, right, l.anchorY + l.rowH});
        place(fill_, {l.x1, l.fillY, right, l.fillY + l.rowH});
        // The buttons, then what they do (only the size and the tileset wait for them).
        c.text({l.x1, l.infoY, right, l.infoY + l.infoH}, v_.infoText, kDim, draw::kWrap);
        const int bh = scale(stardraft::kit::kButtonH);
        const int cw = change_.preferredWidth(c), rw = reset_.preferredWidth(c);
        place(change_, {l.x1, l.buttonsY, l.x1 + cw, l.buttonsY + bh});
        place(reset_, {l.x1 + cw + scale(stardraft::kit::kButtonGap), l.buttonsY, l.x1 + cw + scale(stardraft::kit::kButtonGap) + rw, l.buttonsY + bh});
    }

private:
    // The page's rows (the same at each paint); textH: the text under the buttons.
    struct PropsRows {
        int pad, rowH, pitch, x1, boxW, right, nameY, nameH, descY, descH, terrainY, tilesetY, sizeY, anchorY, fillY, buttonsY, infoY, infoH, height;
    };
    PropsRows rows(int textH) {
        PropsRows l{};
        l.pad = scale(stardraft::kit::kDialogPad), l.rowH = scale(stardraft::kit::kFieldH), l.pitch = l.rowH + scale(8);
        l.boxW = scale(440), l.x1 = l.pad + scale(120), l.right = l.x1 + l.boxW;
        l.nameY = l.pad + l.pitch;  // under the "Map" heading (each editor: its box, then its colors under it)
        l.nameH = host_.textHeight(false, l.boxW, l.rowH);
        l.descY = l.nameY + l.nameH + scale(10), l.descH = host_.textHeight(true, l.boxW, scale(96));
        l.terrainY = l.descY + l.descH + l.pad;
        l.tilesetY = l.terrainY + l.pitch;
        l.sizeY = l.tilesetY + l.pitch, l.anchorY = l.sizeY + l.pitch, l.fillY = l.anchorY + l.pitch;
        l.buttonsY = l.fillY + l.pitch + scale(4);
        l.infoY = l.buttonsY + scale(stardraft::kit::kButtonH) + scale(8);
        l.infoH = std::max(scale(20), textH);  // as tall as the text, at least one line
        l.height = l.infoY + l.infoH + l.pad;
        return l;
    }
    // A child's frame, set only when it differs (a set repaints).
    void place(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
    }
    // A value changed by the page: the copy's change into the map (the program), then the page again.
    void edited() {
        syncControls();
        host_.changed();
        invalidate();
    }
    // The dropdowns show the copy's values (the program may have changed them too).
    void syncControls() {
        tileset_.setValue(v_.era & 7), tileset_.setChanged(v_.era != v_.eraBase);
        syncSizes(wDrop_, wVals_, wOdd_, v_.w, v_.w != v_.wBase);
        syncSizes(hDrop_, hVals_, hOdd_, v_.h, v_.h != v_.hBase);
        anchor_.setValue(std::clamp(v_.anchorIndex, 0, 8));
        if (fillEra_ != (v_.era & 7)) {  // the new tileset's terrains
            fillEra_ = v_.era & 7, fillIds_.clear();
            std::vector<stardraft::kit::DropdownItem> items;
            if (const IsomTerrainTable* t = IsomTerrainTable::forEra(uint16_t(fillEra_)))
                for (uint16_t i = 1; i <= t->terrainCount(); ++i)
                    if (t->isTerrain(i)) items.push_back({widen(t->terrainName(i)), -1, -1}), fillIds_.push_back(i);
            fill_.setItems(items);
        }
        int at = -1;
        for (size_t i = 0; i < fillIds_.size(); ++i)
            if (fillIds_[i] == v_.fillTerrain) at = int(i);
        fill_.setValue(at);
        const bool differs = !v_.infoText.empty();
        if (change_.enabled() != differs) change_.setEnabled(differs);
        if (reset_.enabled() != differs) reset_.setEnabled(differs);
    }
    // A size's dropdown: the standard sizes, and the map's own size when it is not one of them (kept as a choice).
    void syncSizes(stardraft::kit::Dropdown& d, std::vector<int>& vals, int& odd, int value, bool changed) {
        const bool standard = std::find(std::begin(kMapSizes), std::end(kMapSizes), value) != std::end(kMapSizes);
        const int want = standard ? -1 : value;
        if (want != odd || vals.empty()) {
            odd = want, vals.clear();
            std::vector<stardraft::kit::DropdownItem> items;
            for (uint16_t s : kMapSizes) items.push_back({std::to_wstring(s), -1, -1}), vals.push_back(s);
            if (!standard) items.push_back({std::to_wstring(value), -1, -1}), vals.push_back(value);
            d.setItems(items);
        }
        for (size_t i = 0; i < vals.size(); ++i)
            if (vals[i] == value) {
                d.setValue(int(i));
                break;
            }
        d.setChanged(changed);
    }

    MapPropsHost& host_;
    MapPropsValues& v_;
    stardraft::kit::Dropdown tileset_, wDrop_, hDrop_, anchor_, fill_;
    stardraft::kit::Button change_, reset_;
    std::vector<int> wVals_, hVals_;  // the value of each item of the size dropdowns
    std::vector<uint16_t> fillIds_;   // the terrain of each item of New ground
    int wOdd_ = -1, hOdd_ = -1, fillEra_ = -1;
};
