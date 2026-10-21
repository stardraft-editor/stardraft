// Crop or extend the map (Map properties; Discord feedback #140; portable, core.inc, after views_map_props.inc): the new
// size and the place of the map's content on a picture of the map, as an image editor's crop tool. The picture is the
// minimap's (one color a tile) of the whole map with a rectangle over it: the 8 handles (the edges and the corners)
// change the size, a drag in the rectangle moves it, and the rectangle can go past the map's edges to make the map
// larger. In the rectangle the picture is the map as it is after the change: Editor::resizeMap on a copy of the map (the
// same code as the change), made again while the rectangle moves (at most each kResizePreviewMs). What is outside the
// rectangle is dim: the change deletes it. The fields at the right (the size, the offset, the place of the content,
// the new ground) show the values and change them too. The program (sdl_map_pages.inc: sdlMapPageResize) runs the
// dialog and makes the change (one undo step).

// The dialog's results (what its buttons return).
constexpr int kResizeOk = 1, kResizeCancel = 2;
// The time between two previews while the rectangle moves (a preview of a 256 x 256 map takes some tens of ms).
constexpr int kResizePreviewMs = 120;

// The change: the new size (tiles) and the offset of the map's content in it (the shift of Editor::resizeMap, tiles,
// always a shift that resizeMap uses unchanged: Editor::resizeShift), and the terrain of the new ground.
struct ResizeValues {
    int w = 0, h = 0, shiftX = 0, shiftY = 0;
    uint16_t fill = 1;
    bool operator==(const ResizeValues&) const = default;
};

// The shift that resizeMap uses (x even, x / 2 + y even) nearest to (x, y) tiles.
inline void resizeNearestShift(double x, double y, int& sx, int& sy) {
    double best = 1e18;
    const int cx = int(std::floor(x / 2.0)), cy = int(std::floor(y));
    for (int dx = cx - 1; dx <= cx + 2; ++dx)
        for (int ty = cy - 2; ty <= cy + 2; ++ty) {
            if ((dx + ty) & 1) continue;
            const double d = (dx * 2 - x) * (dx * 2 - x) + (ty - y) * (ty - y);
            if (d < best) best = d, sx = dx * 2, sy = ty;
        }
}

// The sprites of `ed` that are not a doodad's (a doodad's overlay sprite goes and comes with its doodad).
inline size_t resizePlainSprites(const Editor& ed) {
    std::map<uint16_t, int> overlayOf;                // doodad id -> its overlay sprite (-1: none)
    std::set<std::tuple<int, int, int>> overlays;     // (x, y, sprite) of each doodad's overlay
    for (const Doodad& d : ed.doodads()) {
        auto it = overlayOf.find(d.id);
        if (it == overlayOf.end()) {
            const DoodadType* t = ed.doodadTypes().find(d.id);
            it = overlayOf.emplace(d.id, t ? int(t->overlaySprite) : -1).first;
        }
        if (it->second >= 0) overlays.emplace(int(d.x), int(d.y), it->second);
    }
    size_t n = 0;
    for (const MapSprite& s : ed.sprites()) n += !overlays.count({int(s.x), int(s.y), int(s.id)});
    return n;
}

// The map after a change, as the minimap shows it: made by resizeMap on a copy of the map.
struct ResizePreview {
    ResizeValues v;  // the change it shows
    bool made = false;
    std::string error;             // why the change cannot be made (made is false)
    int w = 0, h = 0;              // tiles
    std::vector<uint32_t> colors;  // the minimap's color of each tile (0x00RRGGBB, row by row)
    std::vector<uint16_t> tiles;   // the tiles (MTXM)
    size_t units = 0, doodads = 0, sprites = 0;  // what the map has after the change (sprites: not the doodads')
    uint64_t revision = 0;  // the map's revision that the copy is of
    uint64_t serial = 0;    // which preview of the program it is (the copy holds the last one: resizeCopyHolds)
};
inline uint64_t gResizeCopySerial = 0;  // the preview that the copy holds (0: none)

// The copy of the map that the previews change (one for the program: its unit data loads once).
inline Editor& resizeCopy(const GameData& data) {
    static std::unique_ptr<Editor> copy;
    static const GameData* of = nullptr;
    if (!copy || of != &data) copy = std::make_unique<Editor>(data), of = &data;
    return *copy;
}

// The map `ed` after the change `v` (its tileset stays): the same resizeMap as the change itself, on a copy.
inline ResizePreview makeResizePreview(const Editor& ed, const ResizeValues& v) {
    static uint64_t serials = 0;
    ResizePreview p;
    p.v = v, p.revision = ed.revision(), p.serial = ++serials;
    Editor& copy = resizeCopy(ed.gameData());
    gResizeCopySerial = 0;
    if (!copy.openChk(ed.chk(), p.error) ||
        !copy.resizeMap(uint16_t(v.w), uint16_t(v.h), ed.era(), v.shiftX, v.shiftY, v.fill, p.error))
        return p;
    gResizeCopySerial = p.serial;
    static std::vector<uint32_t> megatiles;  // (the colors of the copy's tileset; the minimap's are the same)
    static int megatilesEra = -1;
    if (megatilesEra != int(copy.era()) || megatiles.empty()) megatiles = megatileColors(copy.tileset()), megatilesEra = int(copy.era());
    p.made = true;
    p.w = copy.width(), p.h = copy.height();
    p.colors = minimapTileColors(copy.tileset(), megatiles, copy.terrain());
    p.tiles = copy.terrain().tiles;
    p.units = copy.units().size(), p.doodads = copy.doodads().size(), p.sprites = resizePlainSprites(copy);
    return p;
}

// The copy holds the map of preview `p` of the map `ed` as it is now: the change can take the copy's map (the terrain
// core selects some tiles at random, so a second resizeMap can give other tiles than the preview showed).
inline bool resizeCopyHolds(const ResizePreview& p, const Editor& ed) {
    return p.made && p.serial == gResizeCopySerial && p.revision == ed.revision();
}

// The last change that the dialog made, for the snapshots' check (--check resize): the preview at OK, and the units
// before the change with the shift in pixels (the program sets them when it makes the change).
struct ResizeRecord {
    bool made = false, fromCopy = false;  // fromCopy: the map took the copy of the preview (Editor::takeScenario)
    ResizePreview preview;
    std::vector<Unit> unitsBefore;
    int oldW = 0, oldH = 0, shiftPxX = 0, shiftPxY = 0;
};
inline ResizeRecord gResizeRecord;

class ResizeView;
inline ResizeView* gResizeShown = nullptr;  // the dialog that shows (the snapshots' ui line)

// The layout in pixels at 100 %: the picture at the left, the fields at the right of it.
namespace resizeLayout {
constexpr int kPad = 16;                            // ui::kDialogPad
constexpr int kPicture = 520;                       // the picture's width and height
constexpr int kColX = kPicture + 20;                // the fields' column: its labels
constexpr int kLabelW = 112, kFieldX = kColX + kLabelW;
constexpr int kFieldW = 88, kSepW = 24;             // a number's field, the gap between two (with "x" or "Y" in it)
constexpr int kContentW = kFieldX + kFieldW * 2 + kSepW;
constexpr int kRowH = 24, kPitch = 32;              // ui::kFieldH, ui::kFormRowH
constexpr int kSizeY = 0, kOffsetY = kPitch, kAnchorY = 2 * kPitch, kFillY = 3 * kPitch, kInfoY = 4 * kPitch + 12;
constexpr int kButtonH = 28;
constexpr int kWindowW = kContentW + 2 * kPad, kWindowH = kPad + kPicture + 24 + kButtonH + kPad;
}  // namespace resizeLayout

// The picture with the rectangle (a child of ResizeView). Tab stops on it; the arrow keys then move the rectangle.
class ResizePicture final : public draw::View {
public:
    explicit ResizePicture(ResizeView& owner) : owner_(owner) {}
    // Fits the map and the rectangle into the picture (a value typed or selected; not while a drag goes on).
    void fit();
    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override;
    bool focusable() const override { return true; }
    void focusChanged(bool) override { invalidate(); }
    void captureLost() override { endDrag(); }
    void resized() override { fit(); }
    bool dragging() const { return part_ != Part::None; }
    // The old map's picture (made again when the map's tiles change).
    draw::Bitmap mapPicture, previewPicture;

private:
    enum class Part { None, Move, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };
    Part hit(draw::Point pt) const;
    void endDrag();
    // Tiles to the picture's pixels and back.
    int px(double tx) const { return int(std::lround(ox_ + (tx - wx0_) * s_)); }
    int py(double ty) const { return int(std::lround(oy_ + (ty - wy0_) * s_)); }
    draw::Rect tileRect(double l, double t, double r, double b) const { return {px(l), py(t), px(r), py(b)}; }
    void place();  // ox_, oy_ and s_ for the world rectangle
    void grow();   // the world rectangle takes the crop rectangle too (while a drag goes on it only grows)

    ResizeView& owner_;
    double wx0_ = 0, wy0_ = 0, wx1_ = 1, wy1_ = 1;  // the world rectangle (tiles): the map, the rectangle and a margin
    double ox_ = 0, oy_ = 0, s_ = 1;                  // the world's top left corner in the picture, pixels a tile
    Part part_ = Part::None;
    draw::Point dragFrom_{};
    double dragScale_ = 1;  // pixels a tile when the drag started (the picture grows while the rectangle moves)
    ResizeValues dragStart_;
};

class ResizeView final : public draw::View {
public:
    // `v`: the first values (its shift is made one that resizeMap uses).
    ResizeView(const Editor& ed, const ResizeValues& v)
        : ed_(ed), oldW_(ed.width()), oldH_(ed.height()), picture_(*this), alive_(std::make_shared<bool>(true)) {
        v_ = v;
        Editor::resizeShift(v_.shiftX, v_.shiftY);
        homeY_ = v_.shiftY;
        plainSprites_ = resizePlainSprites(ed);
        // The sizes: StarEdit's (as Map properties' size fields), and the map's own size when it is not one of them.
        for (int* sizes : {&oldW_, &oldH_}) {
            std::vector<int>& out = sizes == &oldW_ ? sizesW_ : sizesH_;
            for (uint16_t s : kMapSizes) out.push_back(s);
            if (std::find(out.begin(), out.end(), *sizes) == out.end()) out.push_back(*sizes), std::sort(out.begin(), out.end());
        }
        const std::vector<uint32_t> colors = megatileColors(ed.tileset());
        minimapPixels(picture_.mapPicture, minimapTileColors(ed.tileset(), colors, ed.terrain()), oldW_, oldH_);
        add(picture_, {0, 0, 0, 0});

        std::vector<stardraft::kit::DropdownItem> ws, hs, anchors, fills;
        for (int s : sizesW_) ws.push_back({std::to_wstring(s), -1, -1});
        for (int s : sizesH_) hs.push_back({std::to_wstring(s), -1, -1});
        wDrop_.setItems(ws), hDrop_.setItems(hs);
        wDrop_.onChange = [this](int i) { sizeTo(sizesW_[size_t(i)], v_.h); };
        hDrop_.onChange = [this](int i) { sizeTo(v_.w, sizesH_[size_t(i)]); };
        for (int k = 0; k < 9; ++k) anchors.push_back({kAnchorNames[k], -1, -1});
        anchor_.setItems(anchors);
        anchor_.onChange = [this](int k) { set(anchorValues(k, v_.w, v_.h), true); };
        if (const IsomTerrainTable* t = IsomTerrainTable::forEra(ed.era()))
            for (uint16_t i = 1; i <= t->terrainCount(); ++i)
                if (t->isTerrain(i)) fills.push_back({widen(t->terrainName(i)), -1, -1}), fillIds_.push_back(i);
        fill_.setItems(fills);
        fill_.onChange = [this](int i) {
            ResizeValues n = v_;
            n.fill = fillIds_[size_t(i)];
            set(n, true);
        };
        for (stardraft::kit::Dropdown* d : {&wDrop_, &hDrop_, &anchor_, &fill_}) d->enterOpens = false;  // (Enter: the dialog's OK)
        wDrop_.setTip(L"The width of the map after the change, in tiles");
        hDrop_.setTip(L"The height of the map after the change, in tiles");
        anchor_.setTip(L"Put the map's content at this side or corner of the new size");
        fill_.setTip(L"The terrain of the ground that the change adds");
        for (stardraft::kit::TextField* f : {&xField_, &yField_}) {
            f->setFilter([](wchar_t ch) { return (ch >= L'0' && ch <= L'9') || ch == L'-'; });
            f->setMaxLength(5);
            f->onFocusLost = [this] { syncFields(true); };
        }
        xField_.onChange = [this] { typed(true); };
        yField_.onChange = [this] { typed(false); };
        // Tab goes through the picture, the size, the offset, the place of the content and the new ground.
        add(wDrop_, {0, 0, 0, 0}), add(hDrop_, {0, 0, 0, 0}), add(xField_, {0, 0, 0, 0}), add(yField_, {0, 0, 0, 0});
        add(anchor_, {0, 0, 0, 0}), add(fill_, {0, 0, 0, 0});
        syncFields(true);
        preview_ = makeResizePreview(ed_, v_);
        previewChanged();
    }
    ~ResizeView() override { *alive_ = false; }

    const Editor& editor() const { return ed_; }
    int oldW() const { return oldW_; }
    int oldH() const { return oldH_; }
    const ResizeValues& values() const { return v_; }
    const ResizePreview& preview() const { return preview_; }
    const std::vector<int>& sizesW() const { return sizesW_; }
    const std::vector<int>& sizesH() const { return sizesH_; }
    // The values differ from the map's (OK is on).
    bool changed() const { return v_.w != oldW_ || v_.h != oldH_ || v_.shiftX != 0 || v_.shiftY != 0; }
    // OK takes the values: they differ from the map's, and the preview of them (when it is made) could be made.
    bool canApply() const { return changed() && (!(preview_.v == v_) || preview_.made); }
    // The dialog this view is in (its OK follows the values).
    void setDialog(stardraft::kit::Dialog* d) {
        dlg_ = d;
        if (dlg_) dlg_->setEnabledFor(kResizeOk, canApply());
    }

    // New values (`fit`: the picture fits them; not while the rectangle is dragged). The fields show them, and the
    // preview follows: at once, or (while the rectangle moves) at most each kResizePreviewMs.
    void set(const ResizeValues& wanted, bool fit, bool keepHome = false) {
        ResizeValues n = wanted;
        // The rectangle keeps at least 2 tiles of the map in each direction (the change keeps a part of the map).
        n.shiftX = std::clamp(n.shiftX, 2 - oldW_, n.w - 2), n.shiftY = std::clamp(n.shiftY, 2 - oldH_, n.h - 2);
        Editor::resizeShift(n.shiftX, n.shiftY);
        if (n == v_) return syncFields(false);  // (a typed offset that the rectangle cannot take: the field shows it later)
        v_ = n;
        if (!keepHome) homeY_ = v_.shiftY;
        syncFields(false);
        if (fit) picture_.fit();
        if (dlg_) dlg_->setEnabledFor(kResizeOk, canApply());
        requestPreview(!picture_.dragging());
        picture_.invalidate();
        invalidate();
    }
    // The arrow keys: the rectangle 2 tiles to the side (the content's offset goes the other way; its offset down
    // stays, or is one less where resizeMap needs it), or 2 tiles up or down.
    void nudge(int dx, int dy) {
        ResizeValues n = v_;
        if (dx) {
            n.shiftX -= dx * 2, n.shiftY = homeY_;
            Editor::resizeShift(n.shiftX, n.shiftY);
            set(n, true, true);
        } else {
            n.shiftY -= dy * 2;
            set(n, true);
        }
    }
    // The preview for the values that show now, made at once (OK, the end of a drag).
    void finishPreview() {
        cancelTimer();
        if (!(preview_.v == v_)) preview_ = makeResizePreview(ed_, v_), previewChanged();
    }
    // The values of anchor k (Map properties' "Map content at") for a size of w x h.
    ResizeValues anchorValues(int k, int w, int h) const {
        ResizeValues n = v_;
        n.w = w, n.h = h, n.shiftX = (w - oldW_) * (k % 3) / 2, n.shiftY = (h - oldH_) * (k / 3) / 2;
        Editor::resizeShift(n.shiftX, n.shiftY);
        return n;
    }
    // The anchor whose values these are (-1: none).
    int anchorOf(const ResizeValues& n) const {
        for (int k = 0; k < 9; ++k)
            if (anchorValues(k, n.w, n.h) == n) return k;
        return -1;
    }

    void paint(draw::Canvas& c) override {
        using namespace resizeLayout;
        c.fill({0, 0, width(), height()}, kBg);
        auto label = [&](const wchar_t* t, int y) { c.text({scale(kColX), scale(y), scale(kFieldX) - scale(8), scale(y + kRowH)}, t, kDim, draw::kMiddle); };
        label(L"Size (tiles)", kSizeY), label(L"Offset (tiles)", kOffsetY), label(L"Map content at", kAnchorY), label(L"New ground", kFillY);
        const int sep = scale(kFieldX + kFieldW);
        c.text({sep, scale(kSizeY), sep + scale(kSepW), scale(kSizeY + kRowH)}, L"x", kDim, draw::kMiddle | draw::kCenter);
        c.text({sep, scale(kOffsetY), sep + scale(kSepW), scale(kOffsetY + kRowH)}, L"Y", kDim, draw::kMiddle | draw::kCenter);
        c.text({scale(kFieldX) - scale(18), scale(kOffsetY), scale(kFieldX) - scale(4), scale(kOffsetY + kRowH)}, L"X", kDim, draw::kMiddle);
        const draw::Rect info{scale(kColX), scale(kInfoY), scale(kContentW), height()};
        const int h = c.measureBox(infoText_, info.width(), draw::kWrap).y;
        c.text(info, infoText_, kText, draw::kWrap);
        c.text({info.left, info.top + h + scale(12), info.right, info.bottom}, hintText(), kDim, draw::kWrap);
    }
    void resized() override {
        using namespace resizeLayout;
        auto put = [](draw::View& v, int x, int y, int w, int h) { v.setFrame({scale(x), scale(y), scale(x + w), scale(y + h)}); };
        put(picture_, 0, 0, kPicture, kPicture);
        put(wDrop_, kFieldX, kSizeY, kFieldW, kRowH), put(hDrop_, kFieldX + kFieldW + kSepW, kSizeY, kFieldW, kRowH);
        put(xField_, kFieldX, kOffsetY, kFieldW, kRowH), put(yField_, kFieldX + kFieldW + kSepW, kOffsetY, kFieldW, kRowH);
        put(anchor_, kFieldX, kAnchorY, kFieldW * 2 + kSepW, kRowH), put(fill_, kFieldX, kFillY, kFieldW * 2 + kSepW, kRowH);
    }

private:
    static std::wstring hintText() {
        return L"Drag an edge or a corner of the rectangle to change the size. Drag in the rectangle to move it. The arrow keys "
               L"move it in steps of 2 tiles (the terrain's diamonds are 2 tiles wide).";
    }
    // What the change does, under the fields (the counts are the preview's).
    void info() {
        std::wstring t;
        if (!changed()) {
            t = L"The map keeps its size.";
        } else {
            t = L"The map becomes " + std::to_wstring(v_.w) + L" x " + std::to_wstring(v_.h) + L" tiles (now " + std::to_wstring(oldW_) +
                L" x " + std::to_wstring(oldH_) + L"). StarDraft 2 deletes the parts of the map outside the new size.";
            if (preview_.made) {
                const size_t units = ed_.units().size() - std::min(ed_.units().size(), preview_.units);
                const size_t doodads = ed_.doodads().size() - std::min(ed_.doodads().size(), preview_.doodads);
                const size_t sprites = plainSprites_ - std::min(plainSprites_, preview_.sprites);
                std::vector<std::wstring> parts;
                auto count = [&](size_t n, const wchar_t* one, const wchar_t* many) {
                    if (n) parts.push_back(std::to_wstring(n) + L" " + (n == 1 ? one : many));
                };
                count(units, L"unit", L"units"), count(doodads, L"doodad", L"doodads"), count(sprites, L"sprite", L"sprites");
                if (!parts.empty()) {
                    t += L" Outside the new size: ";
                    for (size_t i = 0; i < parts.size(); ++i) t += (i ? (i + 1 == parts.size() ? L" and " : L", ") : L"") + parts[i];
                    t += L" (dim in the picture).";
                }
            }
            t += L" The locations move with the map and stop at its new edges.";
        }
        if (!preview_.error.empty()) t += L"\r\n\r\nStarDraft 2 cannot make this change: " + widen(preview_.error) + L".";
        infoText_ = t;
    }
    // The fields show the values (`all`: also the offset field that the user types in).
    void syncFields(bool all) {
        for (size_t i = 0; i < sizesW_.size(); ++i)
            if (sizesW_[i] == v_.w) wDrop_.setValue(int(i));
        for (size_t i = 0; i < sizesH_.size(); ++i)
            if (sizesH_[i] == v_.h) hDrop_.setValue(int(i));
        wDrop_.setChanged(v_.w != oldW_), hDrop_.setChanged(v_.h != oldH_);
        anchor_.setValue(anchorOf(v_));
        int at = -1;
        for (size_t i = 0; i < fillIds_.size(); ++i)
            if (fillIds_[i] == v_.fill) at = int(i);
        fill_.setValue(at);
        if (all || !xField_.hasFocus()) xField_.setText(std::to_wstring(v_.shiftX));
        if (all || !yField_.hasFocus()) yField_.setText(std::to_wstring(v_.shiftY));
        xField_.setChanged(v_.shiftX != 0), yField_.setChanged(v_.shiftY != 0);
        info();
    }
    // A size selected: the content stays at its anchor (when it is at one), else at its offset.
    void sizeTo(int w, int h) {
        const int k = anchorOf(v_);
        if (k >= 0) return set(anchorValues(k, w, h), true);
        ResizeValues n = v_;
        n.w = w, n.h = h;
        set(n, true);
    }
    // An offset typed: the nearest offset that resizeMap uses (the field shows it when it loses the keyboard).
    void typed(bool x) {
        const std::wstring t = (x ? xField_ : yField_).text();
        int value = 0;
        bool digits = false;
        for (size_t i = t.size() && t[0] == L'-' ? 1 : 0; i < t.size(); ++i) {
            if (t[i] < L'0' || t[i] > L'9') return;  // (a "-" that is not the first character: not a number yet)
            value = std::min(value * 10 + (t[i] - L'0'), 999), digits = true;
        }
        if (!digits) return;
        value = std::clamp(t[0] == L'-' ? -value : value, -256, 256);
        int sx = 0, sy = 0;
        resizeNearestShift(x ? value : v_.shiftX, x ? v_.shiftY : value, sx, sy);
        ResizeValues n = v_;
        n.shiftX = sx, n.shiftY = sy;
        set(n, true);
    }
    void cancelTimer() {
        if (timer_ && host()) host()->cancelTimer(timer_);
        timer_ = 0;
    }
    // A preview for the values: now, or (while the rectangle moves) when kResizePreviewMs passed since the last one.
    void requestPreview(bool now) {
        using clock = std::chrono::steady_clock;
        if (now || std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - last_).count() >= kResizePreviewMs) {
            cancelTimer();
            if (!(preview_.v == v_)) preview_ = makeResizePreview(ed_, v_), previewChanged();
            return;
        }
        if (timer_ || !host()) return;
        std::weak_ptr<bool> alive = alive_;
        timer_ = host()->timer(kResizePreviewMs, [this, alive] {
            if (alive.expired() || !*alive.lock()) return;
            timer_ = 0;
            if (!(preview_.v == v_)) preview_ = makeResizePreview(ed_, v_), previewChanged();
        });
    }
    void previewChanged() {
        last_ = std::chrono::steady_clock::now();
        if (preview_.made) minimapPixels(picture_.previewPicture, preview_.colors, preview_.w, preview_.h);
        if (dlg_) dlg_->setEnabledFor(kResizeOk, canApply());
        info();
        picture_.invalidate();
        invalidate();
    }

    const Editor& ed_;
    int oldW_, oldH_;
    ResizeValues v_;
    int homeY_ = 0;  // the offset down that the arrow keys to the side keep
    std::vector<int> sizesW_, sizesH_;
    size_t plainSprites_ = 0;  // the map's sprites that are not the doodads' (resizePlainSprites, once)
    std::vector<uint16_t> fillIds_;  // the terrain of each item of New ground
    ResizePicture picture_;
    stardraft::kit::Dropdown wDrop_, hDrop_, anchor_, fill_;
    stardraft::kit::TextField xField_, yField_;
    stardraft::kit::Dialog* dlg_ = nullptr;
    ResizePreview preview_;
    std::wstring infoText_;
    int timer_ = 0;
    std::chrono::steady_clock::time_point last_{};
    std::shared_ptr<bool> alive_;  // (a timer that runs after the view went does nothing)
};

// --- The picture -------------------------------------------------------------------------------------------------------

inline void ResizePicture::place() {
    const double ww = std::max(1.0, wx1_ - wx0_), wh = std::max(1.0, wy1_ - wy0_);
    const int pad = scale(10);
    s_ = std::max(0.01, std::min((width() - 2 * pad) / ww, (height() - 2 * pad) / wh));
    ox_ = (width() - ww * s_) / 2, oy_ = (height() - wh * s_) / 2;
}

inline void ResizePicture::fit() {
    const ResizeValues& v = owner_.values();
    const double l = std::min(0, -v.shiftX), t = std::min(0, -v.shiftY);
    const double r = std::max(owner_.oldW(), -v.shiftX + v.w), b = std::max(owner_.oldH(), -v.shiftY + v.h);
    const double m = std::max(4.0, std::max(r - l, b - t) * 0.06);  // a margin: the handles at the edges are in the picture
    wx0_ = l - m, wy0_ = t - m, wx1_ = r + m, wy1_ = b + m;
    place();
    invalidate();
}

inline void ResizePicture::grow() {
    const ResizeValues& v = owner_.values();
    const double l = -v.shiftX, t = -v.shiftY, r = l + v.w, b = t + v.h;
    if (l >= wx0_ && t >= wy0_ && r <= wx1_ && b <= wy1_) return;
    const double m = std::max(4.0, std::max(r - l, b - t) * 0.06);
    wx0_ = std::min(wx0_, l - m), wy0_ = std::min(wy0_, t - m), wx1_ = std::max(wx1_, r + m), wy1_ = std::max(wy1_, b + m);
    place();
}

inline ResizePicture::Part ResizePicture::hit(draw::Point pt) const {
    const ResizeValues& v = owner_.values();
    const draw::Rect r = tileRect(-v.shiftX, -v.shiftY, -v.shiftX + v.w, -v.shiftY + v.h);
    const int k = scale(7);
    const bool nearL = std::abs(pt.x - r.left) <= k, nearR = std::abs(pt.x - r.right) <= k;
    const bool nearT = std::abs(pt.y - r.top) <= k, nearB = std::abs(pt.y - r.bottom) <= k;
    const bool inX = pt.x > r.left - k && pt.x < r.right + k, inY = pt.y > r.top - k && pt.y < r.bottom + k;
    if (nearL && nearT) return Part::TopLeft;
    if (nearR && nearT) return Part::TopRight;
    if (nearL && nearB) return Part::BottomLeft;
    if (nearR && nearB) return Part::BottomRight;
    if (nearL && inY) return Part::Left;
    if (nearR && inY) return Part::Right;
    if (nearT && inX) return Part::Top;
    if (nearB && inX) return Part::Bottom;
    if (r.contains(pt)) return Part::Move;
    return Part::None;
}

inline draw::Cursor ResizePicture::cursor(draw::Point pt) {
    switch (dragging() ? part_ : hit(pt)) {
    case Part::Move: return draw::Cursor::SizeAll;
    case Part::Left:
    case Part::Right: return draw::Cursor::SizeWE;
    case Part::Top:
    case Part::Bottom: return draw::Cursor::SizeNS;
    case Part::TopLeft:
    case Part::BottomRight: return draw::Cursor::SizeNWSE;
    case Part::TopRight:
    case Part::BottomLeft: return draw::Cursor::SizeNESW;
    default: return draw::Cursor::Arrow;
    }
}

inline void ResizePicture::endDrag() {
    if (part_ == Part::None) return;
    part_ = Part::None;
    owner_.finishPreview();  // (the preview of where the rectangle stops)
    invalidate();
}

inline bool ResizePicture::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    if (e.kind == MouseKind::Down && e.button == 0) {
        focus();
        part_ = hit(e.pt);
        if (part_ == Part::None) return true;
        dragFrom_ = e.pt, dragStart_ = owner_.values(), dragScale_ = s_;
        if (auto* h = host()) h->capture(*this, true);
        return true;
    }
    if (e.kind == MouseKind::Move && dragging()) {
        const double dx = (e.pt.x - dragFrom_.x) / dragScale_, dy = (e.pt.y - dragFrom_.y) / dragScale_;  // tiles
        const ResizeValues& a = dragStart_;
        const double l0 = -a.shiftX, t0 = -a.shiftY, r0 = l0 + a.w, b0 = t0 + a.h;
        // The size nearest to `want` of the sizes the game takes (Map properties' sizes).
        auto nearest = [](const std::vector<int>& sizes, double want) {
            int best = sizes.front();
            for (int s : sizes)
                if (std::abs(s - want) < std::abs(best - want)) best = s;
            return best;
        };
        ResizeValues n = a;
        double wantX = a.shiftX, wantY = a.shiftY;
        const bool left = part_ == Part::Left || part_ == Part::TopLeft || part_ == Part::BottomLeft;
        const bool right = part_ == Part::Right || part_ == Part::TopRight || part_ == Part::BottomRight;
        const bool top = part_ == Part::Top || part_ == Part::TopLeft || part_ == Part::TopRight;
        const bool bottom = part_ == Part::Bottom || part_ == Part::BottomLeft || part_ == Part::BottomRight;
        if (part_ == Part::Move) wantX = a.shiftX - dx, wantY = a.shiftY - dy;
        if (left) n.w = nearest(owner_.sizesW(), r0 - (l0 + dx)), wantX = -(r0 - n.w);  // (the right edge stays)
        if (right) n.w = nearest(owner_.sizesW(), r0 + dx - l0);
        if (top) n.h = nearest(owner_.sizesH(), b0 - (t0 + dy)), wantY = -(b0 - n.h);  // (the bottom edge stays)
        if (bottom) n.h = nearest(owner_.sizesH(), b0 + dy - t0);
        resizeNearestShift(wantX, wantY, n.shiftX, n.shiftY);
        owner_.set(n, false);
        grow();
        invalidate();
        return true;
    }
    if (e.kind == MouseKind::Up && e.button == 0 && dragging()) {
        if (auto* h = host()) h->capture(*this, false);
        endDrag();
        return true;
    }
    return e.kind == MouseKind::Down || e.kind == MouseKind::Up;
}

inline bool ResizePicture::key(const draw::KeyEvent& e) {
    if (!e.down || e.mods) return false;
    switch (e.key) {
    case draw::Key::Left: owner_.nudge(-1, 0); return true;
    case draw::Key::Right: owner_.nudge(1, 0); return true;
    case draw::Key::Up: owner_.nudge(0, -1); return true;
    case draw::Key::Down: owner_.nudge(0, 1); return true;
    default: return false;
    }
}

inline void ResizePicture::paint(draw::Canvas& c) {
    const ResizeValues& v = owner_.values();
    const ResizePreview& p = owner_.preview();
    const Editor& ed = owner_.editor();
    c.fill({0, 0, width(), height()}, kPanel);
    // Where there is no map: a checkered area (the world rectangle only).
    const draw::Rect world = tileRect(wx0_, wy0_, wx1_, wy1_);
    c.pushClip(world);
    const int cell = scale(8);
    for (int y = world.top; y < world.bottom; y += cell)
        for (int x = world.left; x < world.right; x += cell)
            c.fill({x, y, x + cell, y + cell}, (((x - world.left) / cell + (y - world.top) / cell) & 1) ? kCtlHot : kCtl);
    // The map as it is, then in the rectangle the map after the change (the preview: where it was made, in the rectangle).
    const draw::Rect map = tileRect(0, 0, owner_.oldW(), owner_.oldH());
    c.bitmap(mapPicture, {0, 0, mapPicture.width(), mapPicture.height()}, map, false);
    const draw::Rect crop = tileRect(-v.shiftX, -v.shiftY, -v.shiftX + v.w, -v.shiftY + v.h);
    if (p.made) {
        c.pushClip(crop);
        c.bitmap(previewPicture, {0, 0, previewPicture.width(), previewPicture.height()},
                 tileRect(-p.v.shiftX, -p.v.shiftY, -p.v.shiftX + p.w, -p.v.shiftY + p.h), false);
        c.popClip();
    }
    // Outside the rectangle: dim (the change deletes it).
    const uint8_t dim = 150;
    c.fill({world.left, world.top, world.right, crop.top}, RGB(0, 0, 0), dim);
    c.fill({world.left, crop.bottom, world.right, world.bottom}, RGB(0, 0, 0), dim);
    c.fill({world.left, crop.top, crop.left, crop.bottom}, RGB(0, 0, 0), dim);
    c.fill({crop.right, crop.top, world.right, crop.bottom}, RGB(0, 0, 0), dim);
    // The map's parts at their places (the change moves them with the rectangle): dim when the change deletes them.
    const int sx = v.shiftX * 32, sy = v.shiftY * 32, mw = v.w * 32, mh = v.h * 32;
    auto kept = [&](int x, int y) { return x + sx >= 0 && y + sy >= 0 && x + sx < mw && y + sy < mh; };
    auto at = [&](int x, int y) { return draw::Point{px(x / 32.0), py(y / 32.0)}; };
    auto outline = [&](draw::Rect r, COLORREF col, uint8_t a) {
        c.fill({r.left, r.top, r.right, r.top + 1}, col, a), c.fill({r.left, r.bottom - 1, r.right, r.bottom}, col, a);
        c.fill({r.left, r.top, r.left + 1, r.bottom}, col, a), c.fill({r.right - 1, r.top, r.right, r.bottom}, col, a);
    };
    for (int i = 0; i < ed.locationCount(); ++i) {
        if (i == kAnywhereLocation || !ed.locationUsed(i)) continue;
        const Rect e = ed.locationEdges(i).normalized();
        const bool in = e.right + sx > 0 && e.bottom + sy > 0 && e.left + sx < mw && e.top + sy < mh;
        outline({px(e.left / 32.0), py(e.top / 32.0), std::max(px(e.right / 32.0), px(e.left / 32.0) + 2), std::max(py(e.bottom / 32.0), py(e.top / 32.0) + 2)},
                RGB(120, 220, 140), in ? 200 : 60);
    }
    const int d = std::max(2, int(std::lround(s_ * 0.9)));
    for (const MapSprite& s : ed.sprites()) {
        const draw::Point q = at(s.x, s.y);
        c.fill({q.x - 1, q.y - 1, q.x + 1, q.y + 1}, RGB(230, 230, 230), kept(s.x, s.y) ? 200 : 50);
    }
    for (const Unit& u : ed.units()) {
        const draw::Point q = at(u.x, u.y);
        const int k = std::clamp(d, 2, 6);
        c.fill({q.x - k / 2, q.y - k / 2, q.x - k / 2 + k, q.y - k / 2 + k}, kMinimapPlayerColors[std::min<int>(u.owner, 11)], kept(u.x, u.y) ? 255 : 70);
    }
    c.popClip();
    // The rectangle, its handles and its size.
    outline(crop, RGB(0, 0, 0), 160);
    outline({crop.left + 1, crop.top + 1, crop.right - 1, crop.bottom - 1}, kAccentBright, 255);
    const int hs = scale(4);
    const int xs[3] = {crop.left, (crop.left + crop.right) / 2, crop.right}, ys[3] = {crop.top, (crop.top + crop.bottom) / 2, crop.bottom};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            if (i == 1 && j == 1) continue;
            c.fill({xs[i] - hs, ys[j] - hs, xs[i] + hs, ys[j] + hs}, RGB(0, 0, 0));
            c.fill({xs[i] - hs + 1, ys[j] - hs + 1, xs[i] + hs - 1, ys[j] + hs - 1}, kAccentBright);
        }
    const std::wstring size = std::to_wstring(v.w) + L" x " + std::to_wstring(v.h);
    const int tw = c.measure(size) + scale(12), th = scale(20);
    const draw::Rect chip{crop.left + scale(6), crop.top + scale(6), crop.left + scale(6) + tw, crop.top + scale(6) + th};
    c.fill(chip, RGB(0, 0, 0), 170);
    c.text(chip, size, RGB(240, 240, 240), draw::kMiddle | draw::kCenter);  // (on the dark chip in each theme)
    if (hasFocus()) outline({0, 0, width(), height()}, kAccent, 255);
}

// The dialog around `view`: OK (on while the values differ from the map's) and Cancel. Returns the window's size (the
// snapshots show it without a window).
inline draw::Point resizeDialogSetup(stardraft::kit::Dialog& dlg, ResizeView& view) {
    using namespace resizeLayout;
    dlg.setContent(view);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"Change the size"), kResizeOk, true, false, false, 0});
    buttons.push_back({tr(L"Cancel"), kResizeCancel, false, true, false, 0});
    dlg.setButtons(std::move(buttons));
    dlg.setAllow([&view](int r) { return r != kResizeOk || view.canApply(); });
    view.setDialog(&dlg);
    return draw::Point{scale(kWindowW), scale(kWindowH)};
}

// Runs Crop or extend from `from` (the program's anchor view) for the open map `ed`. OK puts the values into `v` (and
// the preview at OK into gResizeRecord.preview); Cancel and the window's close box leave it as it was.
inline bool runResizeDialog(draw::View& from, const Editor& ed, ResizeValues& v) {
    ResizeView view(ed, v);
    stardraft::kit::Dialog dlg;
    const draw::Point size = resizeDialogSetup(dlg, view);
    gResizeShown = &view;
    const int r = dlg.run(from, tr(L"Crop or extend the map"), size);
    gResizeShown = nullptr;
    if (r != kResizeOk || !view.changed()) return false;
    view.finishPreview();
    v = view.values();
    gResizeRecord = ResizeRecord{};
    gResizeRecord.preview = view.preview();
    return true;
}
