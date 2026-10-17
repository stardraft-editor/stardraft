// File > Export map image (Ctrl+Shift+E) and the New page's larger preview (portable, core.inc; after map_scene.inc).
//
// Export map image: the whole map, the current view or the selected area (the terrain selection, else the selected
// location) as a PNG, at a scale (64 pixels a tile in HD art, 32 in the game's own size, or 1/2, 1/4, 1/8 of it), with
// the units, the doodads, the sprites, the locations and their names, and the grid that the user selects. The dialog
// (MapImageView) and the scene of each part of the picture (mapImageScene) are here; the shell draws the parts with the
// map's renderer into a texture and writes the PNG a band at a time (sdl_map_image.inc: shellMapImageJob), so a
// picture of a 256 x 256 map at full size needs one texture of 1024 x 1024 pixels on the GPU. The export draws one
// part at a time while a progress window shows (MapImageProgressView, runMapImageJob): the window stays live, and
// Cancel stops the export (the file that was there before stays).
//
// The New page's larger preview (NewPreviewView): the map of the page's choices in the tileset's tiles, in the art that
// View > graphics selects (HD2 tiles for the HD art, else the game's tiles), drawn without the GPU (the library's
// renderTerrain and renderHdTerrain); Export writes it as a PNG at the scale selected (writeTerrainPng).

// --- The options and the area ----------------------------------------------------------------------------------------

struct MapImageScale {
    int px;  // pixels a tile in the picture
    const wchar_t* name;
};
constexpr MapImageScale kMapImageScales[] = {{64, L"Full size in HD (64 pixels a tile)"},
                                             {32, L"Full size (32 pixels a tile)"},
                                             {16, L"1/2 (16 pixels a tile)"},
                                             {8, L"1/4 (8 pixels a tile)"},
                                             {4, L"1/8 (4 pixels a tile)"}};

enum class MapImageArea { Whole, View, Selection };

struct MapImageOptions {
    MapImageArea area = MapImageArea::Whole;
    int px = 8;
    bool units = true, doodads = true, sprites = true, locations = false, grid = false;
};

// The options of the last export in this session (the dialog shows them again).
inline MapImageOptions gMapImageLast;

// The selected area in map pixels: the terrain selection (Terrain mode, Select), else the selected location. False
// when there is none.
inline bool mapImageSelection(Rect& out) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return false;
    Editor& ed = *app.ed;
    Rect r{};
    if (app.hasTerrainSel) r = app.terrainSel;
    else if (const int loc = ed.selectedLocation(); loc >= 0 && loc != kAnywhereLocation && ed.locationUsed(loc)) r = ed.locationRect(loc);
    else return false;
    if (r.left > r.right) std::swap(r.left, r.right);
    if (r.top > r.bottom) std::swap(r.top, r.bottom);
    r.left = std::max(0, r.left), r.top = std::max(0, r.top);
    r.right = std::min(ed.width() * 32, r.right), r.bottom = std::min(ed.height() * 32, r.bottom);
    if (r.right - r.left < 1 || r.bottom - r.top < 1) return false;
    out = r;
    return true;
}

// The area of the picture in map pixels (empty when there is no map, or no selection for Selection).
inline Rect mapImageRect(const MapImageOptions& o) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return {};
    const int W = app.ed->width() * 32, H = app.ed->height() * 32;
    if (o.area == MapImageArea::Selection) {
        Rect r{};
        return mapImageSelection(r) ? r : Rect{};
    }
    if (o.area == MapImageArea::View) {
        int vw = 0, vh = 0;
        viewSize(vw, vh);
        const float z = std::max(app.zoom, 1.0f / 64);
        Rect r{int(std::floor(app.camX)), int(std::floor(app.camY)), int(std::ceil(app.camX + vw / z)), int(std::ceil(app.camY + vh / z))};
        r.left = std::clamp(r.left, 0, W), r.top = std::clamp(r.top, 0, H), r.right = std::clamp(r.right, 0, W), r.bottom = std::clamp(r.bottom, 0, H);
        return r;
    }
    return {0, 0, W, H};
}

// The picture's size in pixels for the options (0 x 0: nothing to export).
inline draw::Point mapImageSize(const MapImageOptions& o) {
    const Rect r = mapImageRect(o);
    const int w = int(int64_t(r.right - r.left) * o.px / 32), h = int(int64_t(r.bottom - r.top) * o.px / 32);
    return {std::max(0, w), std::max(0, h)};
}

// The terrain of the picture: with the doodads' tiles (MTXM), or without them (TILE, as View > Layers > Doodads off).
inline TerrainLayer mapImageTerrain(const Editor& ed, bool doodads) {
    if (doodads || !ed.chk().has("TILE")) return ed.terrain();
    return readTerrain(ed.chk(), "TILE");
}

// The scene of one part of the picture: the camera at (x, y) in map pixels, `zoom` picture pixels a map pixel, the part
// w x h pixels. Only what the options show: no selection, ghosts or other marks of the editor.
inline render::Scene mapImageScene(const MapImageOptions& o, float x, float y, float zoom, int w, int h) {
    AppState& app = shellState();
    render::Scene s;
    s.camera = {x, y, zoom};
    if (!app.ed || !app.ed->isOpen()) return s;
    Editor& ed = *app.ed;
    UnitSprites& sp = ed.unitSprites();
    syncMapUnitGraphics();
    const float x1 = std::min(ed.width() * 32.0f, x + w / zoom), y1 = std::min(ed.height() * 32.0f, y + h / zoom);
    if (o.grid) {  // (the grid of View > Grid size)
        const float t = 1 / zoom, gw = float(gDock.gridW), gh = float(gDock.gridH);
        const float ox = float(gDock.gridX % gDock.gridW), oy = float(gDock.gridY % gDock.gridH);
        const float x0 = std::max(0.0f, x), y0 = std::max(0.0f, y);
        for (float gx = std::ceil((x0 - ox) / gw) * gw + ox; gx <= x1; gx += gw) s.rects.push_back({gx, y0, gx + t, y1, 0xFFFFFF30u, true});
        for (float gy = std::ceil((y0 - oy) / gh) * gh + oy; gy <= y1; gy += gh) s.rects.push_back({x0, gy, x1, gy + t, 0xFFFFFF30u, true});
    }
    // The doodads' overlays go with the doodads, the other map sprites with the sprites (as View > Layers).
    std::set<std::tuple<uint16_t, uint16_t, uint16_t>> overlays;
    for (const Doodad& dd : ed.doodads())
        if (const DoodadType* t = ed.doodadTypes().find(dd.id); t && t->overlaySprite) overlays.insert({dd.x, dd.y, t->overlaySprite});
    for (const MapSprite& m : ed.sprites()) {
        const bool overlay = (m.flags & SpriteFlag::DrawAsSprite) && overlays.count({m.x, m.y, m.id});
        if (overlay ? !o.doodads : !o.sprites) continue;
        render::SpriteDraw d;
        if (m.flags & SpriteFlag::DrawAsSprite) {
            d.image = sp.imageForSprite(m.id);
            d.frame = sp.script(d.image).startFrameOn(ed.era());
        } else {
            d.image = sp.imageFor(m.id);
            d.frame = sp.defaultFrame(m.id, d.image, d.flip);
        }
        d.x = m.x, d.y = m.y;
        d.owner = uint8_t(std::min<int>(m.owner, 11));
        addImage(s, d);
    }
    if (o.units)
        for (const Unit& u : ed.units()) addUnitSprites(s, u, 0, 0, 1, false, false);
    if (o.locations)
        for (int i = 0; i < ed.locationCount(); ++i) {
            if (!ed.locationUsed(i) || i == kAnywhereLocation) continue;
            const Rect r = ed.locationRect(i);
            const bool inv = ed.locationInverted(i);
            s.rects.push_back({float(r.left), float(r.top), float(r.right), float(r.bottom), inv ? 0xFF404030u : 0x2060FF30u, true});
            outline(s, r, inv ? 0xFF5050E0u : 0x5080FFE0u, 1.0f);
            std::string name = narrow(widen(ed.locationName(i)));
            if (app.locationWrap) name = wrapLocationName(name, float(r.right - r.left) * zoom - 6, float(r.bottom - r.top) * zoom - 6);
            s.texts.push_back({float(r.left) + 3 / zoom, float(r.top) + 3 / zoom, name, 0xFFFFFFFFu});
        }
    return s;
}

// For the self-tests: a unit in the picture (a player's unit, else any) and its place in the picture's pixels; -1: none.
inline int mapImageTestUnit(const MapImageOptions& o, int& px, int& py) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return -1;
    const Rect r = mapImageRect(o);
    const auto& units = app.ed->units();
    int found = -1;
    for (int pass = 0; pass < 2 && found < 0; ++pass)
        for (size_t i = 0; i < units.size(); ++i) {
            const Unit& u = units[i];
            if (u.x < r.left + 16 || u.y < r.top + 16 || u.x >= r.right - 16 || u.y >= r.bottom - 16) continue;
            if (pass == 0 && (u.owner >= 8 || u.type == 214)) continue;  // (first a player's unit; not a start location)
            found = int(i);
            break;
        }
    if (found < 0) return -1;
    px = int(int64_t(units[size_t(found)].x - r.left) * o.px / 32), py = int(int64_t(units[size_t(found)].y - r.top) * o.px / 32);
    return found;
}

// An export of the open map's picture into a PNG file, one part at a time (the shell's: sdl_map_image.inc).
struct MapImageJob {
    virtual ~MapImageJob() = default;  // (not finished: the file stays as it was before the export)
    virtual int parts() const = 0;     // the parts of the picture (each 1024 x 1024 pixels at most)
    virtual int done() const = 0;      // the parts drawn
    // Draws the next part. False when it could not (the reason in `err`).
    virtual bool step(std::string& err) = 0;
    // After the last part: completes the file. False when it could not (the reason in `err`).
    virtual bool finish(std::string& err) = 0;
};
// The shell's export of the open map for the options into `file`. Null, with the reason in `err`, when it cannot start.
std::unique_ptr<MapImageJob> shellMapImageJob(const MapImageOptions& o, const fs::path& file, std::string& err);
// A test run (a snapshot): the export runs with no progress window; > 0: it stops after that many parts, as a
// Cancel does (--mapimage cancel=N).
bool shellMapImageProgressShown();
int shellMapImageCancelAt();
void shellMapImageCanceled(const fs::path& file, int done, int parts);
// The renderer has the HD art (View > High definition, and a GPU that can draw it): the 64 pixels a tile scale.
bool shellMapImageHd();
// A picture file was written (the shell's test runs print it: the snapshots' checks read the file).
void shellMapImageWritten(const fs::path& file, int w, int h, const std::string& what);

// --- The dialog ------------------------------------------------------------------------------------------------------

constexpr int kMapImageExport = 1, kMapImageCancel = 2;

namespace mapImageLayout {
constexpr int kPad = 16, kRowH = 24, kPitch = 32, kLabelW = 96, kFieldW = 360;
constexpr int kContentW = kLabelW + kFieldW;
constexpr int kAreaY = kPad, kScaleY = kAreaY + 3 * kPitch + 8, kShowY = kScaleY + kPitch + 8, kSizeY = kShowY + 3 * kPitch + 8;
constexpr int kButtonH = 28, kButtonsY = kSizeY + kPitch + 16, kWindowH = kButtonsY + kButtonH + kPad;
constexpr int kColW = 140;  // the first column of the check boxes (the second is wider: Locations and their names)
}  // namespace mapImageLayout

class MapImageView final : public draw::View {
public:
    // `o`: the options shown first; `hd`: the HD art is there (the 64 pixels a tile scale); `selection`: there is a
    // selected area.
    MapImageView(const MapImageOptions& o, bool hd, bool selection) : opts_(o) {
        using stardraft::kit::LabelTone;
        for (auto [l, t] : {std::pair{&labels_[0], L"Area"}, std::pair{&labels_[1], L"Scale"}, std::pair{&labels_[2], L"Show"},
                            std::pair{&labels_[3], L"Size"}}) {
            l->setText(tr(t)), l->setTone(LabelTone::Dim);
            add(*l, {0, 0, 0, 0});
        }
        areas_[0] = std::make_unique<stardraft::kit::RadioButton>(tr(L"Whole map"), group_, o.area == MapImageArea::Whole);
        areas_[1] = std::make_unique<stardraft::kit::RadioButton>(tr(L"Current view"), group_, o.area == MapImageArea::View);
        areas_[2] = std::make_unique<stardraft::kit::RadioButton>(tr(L"Selected area"), group_, o.area == MapImageArea::Selection && selection);
        areas_[2]->setEnabled(selection);
        areas_[2]->setTip(tr(L"The terrain selection (Terrain mode, Select), else the selected location"));
        if (!selection && o.area == MapImageArea::Selection) areas_[0]->setOn(true), opts_.area = MapImageArea::Whole;
        for (auto& a : areas_) a->setPlainLabel(true), add(*a, {0, 0, 0, 0});
        group_.onChange = [this](stardraft::kit::RadioButton& b) {
            for (int k = 0; k < 3; ++k)
                if (&b == areas_[k].get()) opts_.area = MapImageArea(k);
            update();
        };
        std::vector<stardraft::kit::DropdownItem> items;
        for (const MapImageScale& s : kMapImageScales)
            if (s.px != 64 || hd) items.push_back({tr(s.name), -1, -1}), scales_.push_back(s.px);
        scale_.setItems(std::move(items));
        int at = 0;
        for (size_t i = 0; i < scales_.size(); ++i)
            if (scales_[i] == o.px) at = int(i);
        if (scales_[size_t(at)] != o.px) at = int(std::find(scales_.begin(), scales_.end(), 8) - scales_.begin()), opts_.px = 8;
        scale_.setValue(at);
        scale_.onChange = [this](int i) {
            if (i >= 0 && i < int(scales_.size())) opts_.px = scales_[size_t(i)];
            update();
        };
        add(scale_, {0, 0, 0, 0});
        const std::pair<bool MapImageOptions::*, const wchar_t*> shows[5] = {{&MapImageOptions::units, L"Units"},
                                                                             {&MapImageOptions::doodads, L"Doodads"},
                                                                             {&MapImageOptions::sprites, L"Sprites"},
                                                                             {&MapImageOptions::locations, L"Locations and their names"},
                                                                             {&MapImageOptions::grid, L"Grid"}};
        for (int k = 0; k < 5; ++k) {
            bool MapImageOptions::*field = shows[k].first;
            shows_[k].setLabel(tr(shows[k].second));
            shows_[k].setOn(opts_.*field);
            shows_[k].setPlainLabel(true);
            shows_[k].onChange = [this, field](bool on) { opts_.*field = on; };
            add(shows_[k], {0, 0, 0, 0});
        }
        size_.setTone(LabelTone::Normal);
        add(size_, {0, 0, 0, 0});
        update();
    }

    void setDialog(stardraft::kit::Dialog* d) {
        dlg_ = d;
        update();
    }
    const MapImageOptions& options() const { return opts_; }
    bool valid() const { return size_ok_; }

    std::wstring feedbackName(draw::Point pt) override {
        for (const auto& l : labels_)
            if (l.frame().contains(pt)) return l.text();
        return tr(L"Export map image");
    }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }
    void resized() override {
        using namespace mapImageLayout;
        auto put = [](draw::View& v, int x, int y, int w, int h) { v.setFrame({scale(x), scale(y), scale(x + w), scale(y + h)}); };
        put(labels_[0], 0, kAreaY, kLabelW - 8, kRowH);
        for (int k = 0; k < 3; ++k) put(*areas_[k], kLabelW, kAreaY + k * kPitch, kFieldW, kRowH);
        put(labels_[1], 0, kScaleY, kLabelW - 8, kRowH);
        put(scale_, kLabelW, kScaleY, kFieldW, kRowH);
        put(labels_[2], 0, kShowY, kLabelW - 8, kRowH);
        for (int k = 0; k < 5; ++k) put(shows_[k], kLabelW + (k / 3) * kColW, kShowY + (k % 3) * kPitch, k < 3 ? kColW - 8 : kFieldW - kColW, kRowH);
        put(labels_[3], 0, kSizeY, kLabelW - 8, kRowH);
        put(size_, kLabelW, kSizeY, kFieldW, kRowH);
    }

private:
    // The size line; Export is off while there is nothing to export.
    void update() {
        const draw::Point sz = mapImageSize(opts_);
        size_ok_ = sz.x > 0 && sz.y > 0;
        size_.setText(size_ok_ ? std::to_wstring(sz.x) + L" x " + std::to_wstring(sz.y) + L" " + tr(L"pixels") : L"\x2014");
        if (dlg_) dlg_->setEnabledFor(kMapImageExport, size_ok_);
    }

    MapImageOptions opts_;
    stardraft::kit::Dialog* dlg_ = nullptr;
    stardraft::kit::Label labels_[4], size_;
    stardraft::kit::RadioGroup group_;
    std::unique_ptr<stardraft::kit::RadioButton> areas_[3];
    stardraft::kit::Dropdown scale_;
    std::vector<int> scales_;
    stardraft::kit::CheckBox shows_[5];
    bool size_ok_ = false;
};

inline draw::Point mapImageDialogSetup(stardraft::kit::Dialog& dlg, MapImageView& view) {
    using namespace mapImageLayout;
    dlg.setContent(view);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"Export\x2026"), kMapImageExport, true, false, false, 0});
    buttons.push_back({tr(L"Cancel"), kMapImageCancel, false, true, false, 0});
    dlg.setButtons(std::move(buttons));
    dlg.setAllow([&view](int r) { return r != kMapImageExport || view.valid(); });
    view.setDialog(&dlg);
    dlg.setInsets({kPad, 0, kPad, kPad, -(kButtonH + kPad), 8});
    return draw::Point{scale(kContentW + 2 * kPad), scale(kWindowH)};
}

// --- The progress window ---------------------------------------------------------------------------------------------

constexpr int kMapImageDone = 3, kMapImageFailed = 4;

namespace mapImageProgressLayout {
constexpr int kW = 420, kRowH = 24, kBarY = kRowH + 8, kBarH = 8, kPartY = kBarY + kBarH + 8, kH = kPartY + kRowH;
}

// The file's name, the progress bar and "Part 3 of 16". The dialog's Cancel stops the export.
class MapImageProgressView final : public draw::View {
public:
    explicit MapImageProgressView(const std::wstring& name) {
        file_.setText(name);
        part_.setTone(stardraft::kit::LabelTone::Dim);
        add(file_, {0, 0, 0, 0});
        add(bar_, {0, 0, 0, 0});
        add(part_, {0, 0, 0, 0});
        setProgress(0, 1);
    }
    void setProgress(int done, int parts) {
        wchar_t b[96];
        std::swprintf(b, 96, tr(L"Part %d of %d"), std::min(done + 1, parts), parts);
        part_.setText(b);
        bar_.setValue(parts > 0 ? float(done) / float(parts) : 0.0f);
    }
    const std::wstring& partText() const { return part_.text(); }
    float value() const { return bar_.value(); }

    std::wstring feedbackName(draw::Point) override { return tr(L"Export map image"); }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }
    void resized() override {
        using namespace mapImageProgressLayout;
        const int w = width();
        file_.setFrame({0, 0, w, scale(kRowH)});
        bar_.setFrame({0, scale(kBarY), w, scale(kBarY + kBarH)});
        part_.setFrame({0, scale(kPartY), w, scale(kPartY + kRowH)});
    }

private:
    stardraft::kit::Label file_, part_;
    stardraft::kit::ProgressBar bar_;
};

inline draw::Point mapImageProgressSetup(stardraft::kit::Dialog& dlg, MapImageProgressView& view) {
    using namespace mapImageProgressLayout;
    dlg.setContent(view);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"Cancel"), kMapImageCancel, false, true, false, 0});
    dlg.setButtons(std::move(buttons));
    const stardraft::kit::DialogInsets in;
    return draw::Point{scale(kW + in.left + in.right), scale(in.top + kH + in.gap + 28 + in.bottom)};
}

// Runs the export a part at a time: kMapImageDone, kMapImageCancel (Cancel, or the window closed) or kMapImageFailed
// (the reason in `err`). A host timer draws each part, so the progress window takes its input between the parts.
inline int runMapImageJob(draw::View& from, MapImageJob& job, const std::wstring& name, std::string& err) {
    // (one part: no window, it would only flash; a test run: no window, and cancel=N stops after N parts)
    if (!shellMapImageProgressShown() || !from.host() || job.parts() <= 1) {
        const int cancelAt = shellMapImageCancelAt();
        while (job.done() < job.parts()) {
            if (cancelAt > 0 && job.done() >= cancelAt) return kMapImageCancel;
            if (!job.step(err)) return kMapImageFailed;
        }
        return job.finish(err) ? kMapImageDone : kMapImageFailed;
    }
    draw::Host& host = *from.host();
    MapImageProgressView view(name);
    stardraft::kit::Dialog dlg;
    const draw::Point size = mapImageProgressSetup(dlg, view);
    view.setProgress(0, job.parts());
    int timer = 0;
    std::function<void()> next = [&] {
        timer = 0;
        if (dlg.closed()) return;
        if (job.done() < job.parts()) {
            if (!job.step(err)) return dlg.close(kMapImageFailed);
            view.setProgress(job.done(), job.parts());
        }
        if (job.done() >= job.parts()) return dlg.close(job.finish(err) ? kMapImageDone : kMapImageFailed);
        timer = host.timer(1, [&] { next(); });
    };
    timer = host.timer(1, [&] { next(); });
    const int r = dlg.run(from, tr(L"Export map image"), size);
    if (timer) host.cancelTimer(timer);
    return r == kMapImageDone || r == kMapImageFailed ? r : kMapImageCancel;
}

// A PNG file to save (the system's save dialog), starting with `name`. Empty: canceled.
inline fs::path pickPngFile(const std::wstring& title, const std::wstring& name) {
    ShellFilePick d;
    d.title = title, d.save = true, d.fileName = name, d.defaultExtension = L"png";
    d.filters = {{L"PNG pictures (*.png)", L"*.png"}};
    return shellPickFile(d);
}

// File > Export map image: the dialog, the file, then the picture. The dialog shows the options of the last export.
inline void exportMapImage(draw::View& from) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return;
    Rect sel{};
    MapImageView view(gMapImageLast, shellMapImageHd(), mapImageSelection(sel));
    stardraft::kit::Dialog dlg;
    const draw::Point size = mapImageDialogSetup(dlg, view);
    if (dlg.run(from, tr(L"Export map image"), size) != kMapImageExport || !view.valid()) return;
    const MapImageOptions o = view.options();
    gMapImageLast = o;
    std::wstring base = app.ed->path().empty() ? std::wstring(L"map") : app.ed->path().stem().wstring();
    const fs::path file = pickPngFile(L"Export map image", base + L".png");
    if (file.empty()) return;
    std::string err;
    const draw::Point sz = mapImageSize(o);
    std::unique_ptr<MapImageJob> job = shellMapImageJob(o, file, err);
    const int r = job ? runMapImageJob(from, *job, file.filename().wstring(), err) : kMapImageFailed;
    if (r == kMapImageCancel) {
        const int done = job->done(), parts = job->parts();
        job.reset();  // (the file stays as it was before the export)
        shellMapImageCanceled(file, done, parts);
        setMessage(tr(L"Canceled the map image export"));
        return;
    }
    job.reset();
    if (r != kMapImageDone) {
        shellErrorBox(widen(err), tr(L"Could not export the map image"));
        return;
    }
    {
        const Rect r = mapImageRect(o);
        int ux = 0, uy = 0;
        const int u = mapImageTestUnit(o, ux, uy);
        shellMapImageWritten(file, sz.x, sz.y,
                             "area " + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom) +
                                 ", " + std::to_string(o.px) + " px a tile, units " + (o.units ? "on" : "off") + "; " +
                                 (u >= 0 ? "unit " + std::to_string(u) + " at " + std::to_string(ux) + "," + std::to_string(uy) : std::string("no unit")));
    }
    setMessage(tr(L"Exported the map image ") + file.filename().wstring() + L" (" + std::to_wstring(sz.x) + L" x " + std::to_wstring(sz.y) + L" " +
               tr(L"pixels") + L")");
}

// --- Terrain pictures without the GPU (the New page) -----------------------------------------------------------------

// The terrain at `px` pixels a tile: from the HD tiles when `hd` has them, else from the tileset's own tiles. `px` is
// 64 (HD only), 32, 16, 8, 4, 2 or 1.
inline Image terrainPicture(const Tileset& ts, const HdTiles* hd, const TerrainLayer& t, int px) {
    if (hd && hd->tileSize > 0 && !hd->tiles.empty() && px <= hd->tileSize) return renderHdTerrain(ts, *hd, t, std::max(1, hd->tileSize / px));
    return renderTerrain(ts, t, std::max(1, 32 / std::min(32, px)));
}

// The terrain as a PNG at `px` pixels a tile, written a band of tile rows at a time (a large map at full size does
// not need the whole picture in memory).
inline bool writeTerrainPng(const fs::path& file, const Tileset& ts, const HdTiles* hd, const TerrainLayer& t, int px, std::string& err) {
    if (!(hd && hd->tileSize >= px) && px > 32) px = 32;
    PngWriter png;
    if (t.width == 0 || t.height == 0 || !png.open(file, t.width * px, t.height * px)) return err = "The file cannot be written.", false;
    const int band = std::max(1, 1024 / px);  // tile rows in each band
    for (int y = 0; y < t.height; y += band) {
        TerrainLayer part;
        part.width = t.width, part.height = uint16_t(std::min(band, t.height - y));
        part.tiles.assign(t.tiles.begin() + ptrdiff_t(y) * t.width, t.tiles.begin() + ptrdiff_t(y + part.height) * t.width);
        const Image img = terrainPicture(ts, hd, part, px);
        if (img.width != t.width * px || !png.addRows(img.rgba.data(), img.height)) return err = "The file cannot be written.", false;
    }
    if (!png.finish()) return err = "The file cannot be written.", false;
    return true;
}

// --- The New page's larger preview -----------------------------------------------------------------------------------

constexpr int kNewPreviewExport = 1, kNewPreviewClose = 2;
inline int gNewPreviewPx = 8;  // the scale of the larger preview's Export (the last one selected in this session)

// The picture of the map (the New page's choices) as large as the dialog, and the scale of Export under it.
class NewPreviewView final : public draw::View {
public:
    // `hd`: the HD2 tiles of the tileset (null or empty: the game's tiles). `info`: the line under the picture.
    NewPreviewView(const Tileset& ts, const HdTiles* hd, TerrainLayer terrain, std::wstring info)
        : ts_(ts), hd_(hd && !hd->tiles.empty() ? hd : nullptr), terrain_(std::move(terrain)), info_(std::move(info)) {
        label_.setText(tr(L"Scale")), label_.setTone(stardraft::kit::LabelTone::Dim);
        add(label_, {0, 0, 0, 0});
        std::vector<stardraft::kit::DropdownItem> items;
        for (const MapImageScale& s : kMapImageScales)
            if (s.px != 64 || hd_) items.push_back({tr(s.name), -1, -1}), scales_.push_back(s.px);
        scale_.setItems(std::move(items));
        const auto at = std::find(scales_.begin(), scales_.end(), gNewPreviewPx);
        scale_.setValue(at == scales_.end() ? int(std::find(scales_.begin(), scales_.end(), 8) - scales_.begin()) : int(at - scales_.begin()));
        scale_.onChange = [this](int i) {
            if (i >= 0 && i < int(scales_.size())) gNewPreviewPx = scales_[size_t(i)];
        };
        add(scale_, {0, 0, 0, 0});
    }
    int exportPx() const {
        const int i = scale_.value();
        return i >= 0 && i < int(scales_.size()) ? scales_[size_t(i)] : 8;
    }
    // The pixels a tile of the picture on the screen (after the first paint), and its size.
    int shownPx = 0;
    draw::Point pictureSize() const { return {bitmap_.width(), bitmap_.height()}; }
    bool hdArt() const { return hd_ != nullptr; }

    std::wstring feedbackName(draw::Point) override { return tr(L"Larger preview"); }
    void resized() override {
        const int y = height() - scale(24);
        label_.setFrame({0, y, scale(56), height()});
        scale_.setFrame({scale(64), y, scale(64 + 300), height()});
    }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        const draw::Rect pv{0, 0, width(), height() - scale(36)};
        c.fill(pv, RGB(0, 0, 0));  // (a picture of the game: dark in both themes)
        if (!made_) make(std::min(pv.width(), pv.height()));
        if (bitmap_.width() > 0 && bitmap_.height() > 0) {
            const double k = std::min(double(pv.width()) / bitmap_.width(), double(pv.height()) / bitmap_.height());
            const int bw = int(bitmap_.width() * k), bh = int(bitmap_.height() * k);
            const int x = (pv.width() - bw) / 2, y = (pv.height() - bh) / 2;
            c.bitmap(bitmap_, {0, 0, bitmap_.width(), bitmap_.height()}, {x, y, x + bw, y + bh}, true);
        }
        c.roundBox(pv, draw::kNone, kBorder, float(scale(2)));
        c.text({scale(64 + 300 + 16), height() - scale(24), width(), height()}, info_, kDim, draw::kMiddle | draw::kEllipsis);
    }

private:
    // The picture at the smallest scale that has at least `side` pixels on the long side (the canvas makes it smaller).
    void make(int side) {
        made_ = true;
        const int tiles = std::max<int>(terrain_.width, terrain_.height), maxPx = hd_ ? 64 : 32;
        int px = 1;
        while (px < maxPx && tiles * px < side) px *= 2;
        shownPx = px;
        const Image img = terrainPicture(ts_, hd_, terrain_, px);
        bitmap_.resize(img.width, img.height);
        bitmap_.opaque = true;
        if (!img.rgba.empty()) std::memcpy(bitmap_.pixels(), img.rgba.data(), std::min(img.rgba.size(), size_t(img.width) * img.height * 4));
        for (int i = 0; i < img.width * img.height; ++i) bitmap_.pixels()[i] |= 0xFF000000u;
        bitmap_.changed();
    }

    const Tileset& ts_;
    const HdTiles* hd_;
    TerrainLayer terrain_;
    std::wstring info_;
    stardraft::kit::Label label_;
    stardraft::kit::Dropdown scale_;
    std::vector<int> scales_;
    draw::Bitmap bitmap_;
    bool made_ = false;
};

// The larger preview's dialog around `view`: Export and Close. Returns the window's size: `room` (the main window's
// size) less a margin.
inline draw::Point newPreviewDialogSetup(stardraft::kit::Dialog& dlg, NewPreviewView& view, draw::Point room) {
    dlg.setContent(view);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"Export\x2026"), kNewPreviewExport, false, false, false, 0});
    buttons.push_back({tr(L"Close"), kNewPreviewClose, true, true, false, 0});
    dlg.setButtons(std::move(buttons));
    return {std::clamp(room.x - scale(80), scale(480), scale(1200)), std::clamp(room.y - scale(80), scale(420), scale(1000))};
}
