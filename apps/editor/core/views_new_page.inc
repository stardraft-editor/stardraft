// The New page (portable, core.inc; docs/dev/CONTROLS.md, "New page and start page"): the settings of a kind that a card
// on the start page selected. Three columns: Project (the name, the folder and the output file, only for an EUD project),
// Terrain (the tileset and the size) and Start (a blank map filled with a terrain, a generated map, or an imported map),
// with a preview of the map under the Start choices; with Generate the generator's settings are to the right of the
// preview. Create makes the map or the project, Cancel and Back leave the page. A view: it paints on a Canvas; its text
// fields are kit text fields (children); the host (new_page.inc on Windows) makes the maps and projects, picks files and
// folders, shows the list pickers, and switches the tab's page.

#include <chrono>
#include <cmath>
#include <cstring>


// The list the Terrain and Symmetry pickers show: a label and the value that comes back.
struct NewListItem {
    std::wstring label;
    std::string value;
};

// The program's part of the New page (new_page.inc): the folders and files, the list pickers, the page's own changes,
// and making the map or the project.
class NewPageHost {
public:
    virtual ~NewPageHost() = default;
    // The folder that new projects and maps go into (the default location of the Location field).
    virtual std::filesystem::path defaultFolder() = 0;
    // A folder chosen by the user (""): the folder for the project.
    virtual std::wstring pickFolder() = 0;
    // An output file's name chosen by the user, starting from `name` ("": none).
    virtual std::wstring pickOutputFile(const std::wstring& name) = 0;
    // A map file chosen by the user ("": none).
    virtual std::wstring pickMapFile() = 0;
    // A list picker under the rectangle `below` (the page's coordinates); `picked` gets the value chosen.
    virtual void pickList(const std::wstring& title, const std::vector<NewListItem>& items, const std::string& current, draw::Rect below,
                          const std::function<void(const std::string&)>& picked) = 0;
    // Back to the start page, and Cancel (the tab closes or goes back to the start page).
    virtual void back() = 0;
    virtual void cancel() = 0;
    // Makes the map (or the project) with the page's choices (NewPageView's members).
    virtual bool make() = 0;
    // Shows a message in the status bar (what is wrong with the choices, what was made).
    virtual void message(const std::wstring& text) = 0;
    // The page's height or its place changed: the host scrolls or repaints.
    virtual void pageChanged() {}
};

// A slider of the generator: its name, its range and value (a terrain's share in half percent, or a shape value).
struct NewSlider {
    std::wstring label;
    int lo, hi, value;
    uint16_t terrain = 0;  // a terrain's share (value in half percent); 0: a shape slider
};

namespace newpage {

const wchar_t* const kSymmetryNames[] = {L"None", L"Mirror left and right", L"Mirror top and bottom", L"Mirror both (4 players)", L"Turn 180 degrees (2 players)"};
const std::vector<NewSlider> kShapeDefaults = {{L"Land scale", 5, 150, 30}, {L"Coastline", 20, 200, 75}, {L"Detail", 60, 400, 180}, {L"Roughness", 0, 100, 35}};

// The average color of each minitile of each megatile (4 x 4 per megatile), 0x00RRGGBB, for the previews.
std::vector<uint32_t> megaColors(const Tileset& ts) {
    std::vector<uint32_t> out(ts.megatiles.size() * 16, 0);
    for (size_t m = 0; m < ts.megatiles.size(); ++m)
        for (int k = 0; k < 16; ++k) {
            const size_t mini = ts.megatiles[m][k] >> 1;
            if (mini >= ts.minitiles.size()) continue;
            uint32_t sum[3] = {0, 0, 0};
            for (uint8_t idx : ts.minitiles[mini]) {
                const Rgb& c = ts.palette[idx];
                sum[0] += c.r, sum[1] += c.g, sum[2] += c.b;
            }
            out[m * 16 + k] = (sum[0] / 64) << 16 | (sum[1] / 64) << 8 | (sum[2] / 64);
        }
    return out;
}

// The average color of each megatile (from the averages of its minitiles).
std::vector<uint32_t> tileColors(const std::vector<uint32_t>& mega) {
    std::vector<uint32_t> out(mega.size() / 16, 0);
    for (size_t m = 0; m < out.size(); ++m) {
        uint32_t sum[3] = {0, 0, 0};
        for (int k = 0; k < 16; ++k) {
            const uint32_t c = mega[m * 16 + size_t(k)];
            sum[0] += c >> 16 & 255, sum[1] += c >> 8 & 255, sum[2] += c & 255;
        }
        out[m] = (sum[0] / 16) << 16 | (sum[1] / 16) << 8 | (sum[2] / 16);
    }
    return out;
}

// The map's tiles: a blank map filled with `terrain`, or (`generate`) terrain generated with `p`.
bool makeTiles(const Tileset& ts, const TerrainGenParams& p, bool generate, uint16_t terrain, std::vector<uint16_t>& tiles, int& tw, int& th) {
    Chk chk;
    IsomGrid grid;
    std::string err;
    if (!grid.create(chk, p.width, p.height, p.era, generate ? uint16_t(1) : terrain, err)) return false;
    if (generate && !generateTerrain(chk, ts, p, err)) return false;
    TerrainLayer t = readTerrain(chk);
    tiles = std::move(t.tiles), tw = t.width, th = t.height;
    return true;
}

// A picture of the tiles, `pw` x `ph` pixels (made at that size, not stretched): the tiles' own pixels when a tile has
// 8 pixels or more, the average colors of its minitiles from 3, else the average color of each tile.
void paintTiles(const Tileset& ts, const std::vector<uint32_t>& mega, const std::vector<uint32_t>& tileAvg, const std::vector<uint16_t>& tiles, int tw,
                int th, int pw, int ph, std::vector<uint32_t>& px) {
    px.assign(size_t(std::max(0, pw)) * std::max(0, ph), 0);
    if (tw <= 0 || th <= 0 || pw <= 0 || ph <= 0 || tiles.size() < size_t(tw) * th) return;
    const int perTile = pw / tw;
    std::vector<int> megaOf(tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i) megaOf[i] = ts.megatileFor(tiles[i]);
    for (int oy = 0; oy < ph; ++oy) {
        const int my = int((int64_t(oy) * 2 + 1) * th * 32 / (int64_t(ph) * 2));  // the map pixel at the middle of this one
        for (int ox = 0; ox < pw; ++ox) {
            const int mx = int((int64_t(ox) * 2 + 1) * tw * 32 / (int64_t(pw) * 2));
            const int m = megaOf[size_t(my / 32) * tw + mx / 32];
            if (m < 0 || size_t(m) >= ts.megatiles.size()) continue;
            const int k = (my % 32 / 8) * 4 + mx % 32 / 8;  // the minitile
            uint32_t c = 0;
            if (perTile >= 8) {
                const uint32_t e = ts.megatiles[size_t(m)][size_t(k)];
                const size_t mini = e >> 1;
                if (mini >= ts.minitiles.size()) continue;
                const int x = e & 1 ? 7 - mx % 8 : mx % 8;
                const Rgb& rgb = ts.palette[ts.minitiles[mini][size_t(my % 8 * 8 + x)]];
                c = uint32_t(rgb.r) << 16 | uint32_t(rgb.g) << 8 | rgb.b;
            } else if (perTile >= 3) {
                if (size_t(m) * 16 + size_t(k) < mega.size()) c = mega[size_t(m) * 16 + size_t(k)];
            } else if (size_t(m) < tileAvg.size()) {
                c = tileAvg[size_t(m)];
            }
            px[size_t(oy) * pw + ox] = c;
        }
    }
}

// A 0x00RRGGBB picture as the canvas's bitmap (0xAABBGGRR, opaque).
void toBitmap(const std::vector<uint32_t>& px, int w, int h, draw::Bitmap& out) {
    out.resize(std::max(0, w), std::max(0, h));
    out.opaque = true;
    uint32_t* d = out.pixels();
    for (size_t i = 0; i < px.size() && i < size_t(std::max(0, w)) * size_t(std::max(0, h)); ++i) {
        const uint32_t p = px[i];
        d[i] = 0xFF000000u | (p & 0xFF) << 16 | (p >> 8 & 0xFF) << 8 | (p >> 16 & 0xFF);
    }
    out.changed();
}

// The page's sizes of the field's name column (the Start section's labels and the generator's).
int scaled(int v) { return scale(v); }

}  // namespace newpage

// The New page's view: the choices, the tileset's generator, the preview, and the painting and the clicks of the page.
class NewPageView final : public draw::View {
public:
    // The start page covers the page (its window is over this one): nothing is painted or loaded under it.
    bool covered = false;

    explicit NewPageView(NewPageHost& host) : host_(host) {
        for (draw::View* f : {static_cast<draw::View*>(&name_), static_cast<draw::View*>(&location_), static_cast<draw::View*>(&output_),
                              static_cast<draw::View*>(&seed_), static_cast<draw::View*>(&fromMap_)})
            add(*f, {0, 0, 0, 0}), f->setVisible(false);
        name_.onChange = location_.onChange = output_.onChange = fromMap_.onChange = [this] { invalidate(); };
        seed_.onChange = [this] { changed(generate); };
        seed_.setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
        output_.setPlaceholder(L"MyMap_EUD.scx");
        fromMap_.setPlaceholder(L"The map to copy (.scm or .scx)");
        for (auto* f : {&name_, &location_, &output_, &seed_, &fromMap_}) f->setTabStop(true);
        seed_.setText(L"12345");
        shape = newpage::kShapeDefaults;
    }

    // The page's choices (the host reads them to make the map; the self-tests set them).
    bool project = false, generate = false;
    bool copyMap = false;      // an EUD project: Start "Import an existing map" (a copy of the map in fromMap); generate is off then
    bool terrainOnly = false;  // Start "The terrain of a map": a new map with only the terrain of the map in fromMap
    bool keep = false;         // Back went to the start page: a card opens the settings with the same choices
    int card = 0;              // the start page's card that the keyboard is on (0 or 1)
    uint16_t era = 0, terrain = 0, w = 128, h = 128;  // terrain 0: the tileset's base ground
    int symmetry = 0, feature = 0;
    std::vector<NewSlider> shares;
    std::vector<NewSlider> shape;
    TerrainGenParams params() {
        newLoadTileset();
        TerrainGenParams p;
        p.width = w, p.height = h, p.era = era, p.seed = seedValue();
        for (const auto& sl : shares) {
            if (p.shares.size() <= sl.terrain) p.shares.resize(sl.terrain + 1, -1.0f);
            p.shares[sl.terrain] = sl.value / 200.0f;
        }
        p.landScale = shape[0].value, p.coastline = shape[1].value, p.detail = shape[2].value;
        p.roughness = shape[3].value / 100.0f;
        p.minFeature = feature == 1 ? 0.5f : 1.0f;
        p.symmetry = GenSymmetry(symmetry);
        return p;
    }
    uint32_t seedValue() const { return uint32_t(std::wcstoul(seed_.text().c_str(), nullptr, 10)); }
    // The map's tiles of the preview (blank or generated), and the terrain the blank map is filled with.
    std::vector<uint16_t> tiles;
    int tilesW = 0, tilesH = 0, tilesVersion = 0;
    bool previewMade = false;  // the tiles are made for the current choices
    Tileset tileset;
    std::vector<GenTerrain> roles;
    uint16_t terrainId();
    // The text of the Name, Location, Output file and map fields (kit text fields).
    std::wstring nameText() const { return name_.text(); }
    std::wstring locationText() const { return location_.text(); }
    std::wstring outputText() const { return output_.text(); }
    std::wstring fromMapText() const { return fromMap_.text(); }
    // A field's text as the program sets it: the caret goes to the start (as SetWindowText left it).
    static void setFieldText(stardraft::kit::TextField& f, const std::wstring& t) {
        f.setText(t);
        f.model().setCaret(0);
    }
    void setNameText(const std::wstring& t) { setFieldText(name_, t); }
    void setLocationText(const std::wstring& t) { setFieldText(location_, t); }
    void setFromMapText(const std::wstring& t) { setFieldText(fromMap_, t); }
    void setOutputText(const std::wstring& t) { setFieldText(output_, t); }
    void setSeedText(const std::wstring& t) { setFieldText(seed_, t); }

    // What the last paint showed (the self-tests read these).
    bool genShown = false, projectShown = false, createEnabled = true;
    bool fieldShown(const char* which) const {
        if (!std::strcmp(which, "name")) return name_.visible();
        if (!std::strcmp(which, "location")) return location_.visible();
        if (!std::strcmp(which, "output")) return output_.visible();
        if (!std::strcmp(which, "seed")) return seed_.visible();
        return fromMap_.visible();
    }
    draw::Rect terrainBox{}, footer{};  // the Blank row's terrain box; the Create and Cancel row (in the page's area)
    draw::Rect previewRect{}, genRect{};  // the preview; the generator's settings (empty when they do not show)
    int sizeX0 = 0, sizeX1 = 0, sizeY[2] = {0, 0};  // the Width and Height sliders: the track's ends and the middle of each
    bool footerStuck = false;  // the Create and Cancel row is held at the bottom of the page (the content scrolls under it)
    std::wstring warning;
    int tallest = 0;   // the highest bottom the page had since it opened: Create stays there (the page does not jump)
    int blockH = 0;    // the settings: the height of the block at the last paint (to put it in the middle)
    int aboveH = 0;    // at the last paint: from the top of the block to the top of the preview
    int height_ = 0;   // the page's content height at the last paint
    int scroll = 0;
    // The preview: the map's tiles as a picture at the size they have on the page (made again when they change).
    std::vector<uint32_t> pixels;
    draw::Bitmap previewBitmap;
    int previewW = 0, previewH = 0, pixelsVersion = -1;
    // A picture of each tileset (a small generated map), made the first time the page shows.
    std::vector<uint32_t> swatch[8];
    int swatchSide = 0;
    bool swatchesMade = false;
    draw::Bitmap swatchBitmap[8];
    // The generator's tileset: loaded for `loadedEra`; the colors of its megatiles.
    uint16_t loadedEra = 0xFFFF;
    std::vector<uint32_t> megaColor, tileColor;

    // The tab's kind and fresh choices (the open map's tileset, 128 x 128, blank). kind: 0 a classic project, 1 an EUD project.
    void reset(int kind) {
        project = kind == 1, card = kind == 1 ? 1 : 0, keep = false;
        generate = false, copyMap = false, terrainOnly = false;
        era = openEra_;
        terrain = 0, w = h = 128, symmetry = 0, feature = 0, scroll = 0;
        loadedEra = 0xFFFF;
        shape = newpage::kShapeDefaults;
        setNameText(newFreeName(L"MyMap", host_.defaultFolder()));
        setLocationText(host_.defaultFolder().make_preferred().wstring());
        setOutputText(L"");
        setSeedText(L"12345");
        previewMade = false;
    }
    // The era of the open map (for the fresh choices of a map that is open); set by the host.
    void setOpenEra(uint16_t e) { openEra_ = e; }

    // Randomize: shares around the tileset's averages (half to one and a half times), shape within useful ranges, a new
    // seed. `all`: the tileset and the size too.
    void randomize(bool all) {
        uint32_t r = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ 0x9E3779B9u;
        auto roll = [&r](int lo, int hi) {
            r = r * 1664525u + 1013904223u;
            return lo + int((r >> 8) % uint32_t(hi - lo + 1));
        };
        if (all) era = uint16_t(roll(0, 7)), terrain = 0;  // the tileset; the size stays (the user chose it)
        newLoadTileset();
        for (const auto& r0 : roles)
            for (auto& sl : shares)
                if (sl.terrain == r0.id) sl.value = std::clamp(int(std::lround(r0.defaultShare * 200)) * roll(50, 150) / 100, sl.lo, sl.hi);
        const int ranges[4][2] = {{15, 60}, {50, 140}, {120, 300}, {10, 70}};
        for (size_t i = 0; i < shape.size(); ++i) shape[i].value = roll(ranges[i][0], ranges[i][1]);
        setSeedText(std::to_wstring(roll(1, 999999)));
        changed();
    }

    // The preview again, a moment after the last change (a slider drag changes it many times).
    void changed(bool regenerate = true) {
        if (regenerate) previewMade = false;
        if (regenerate && !importing()) schedule(60);
        invalidate();
    }
    // The preview's map: blank (filled with the selected terrain) or generated.
    void regenerate() {
        if (importing()) return;
        const TerrainGenParams p = params();
        previewMade = newpage::makeTiles(tileset, p, generate, terrainId(), tiles, tilesW, tilesH);
        ++tilesVersion;
        invalidate();
    }
    // Import an existing map (an EUD project) or a map's terrain: the map has the terrain, so there is no preview.
    bool importing() const { return (project && copyMap) || terrainOnly; }

    // The page's problem: "" when Create can make the map or project (else what is wrong, shown under the fields).
    std::wstring problem() {
        if (!project) {  // a classic project: only the map whose terrain it takes
            std::error_code ec;
            const std::wstring map = fromMapText();
            if (terrainOnly && map.empty()) return L"Select the map whose terrain to use";
            if (terrainOnly && !fs::is_regular_file(fs::path(map), ec)) return tr(L"The map ") + map + tr(L" is not found");
            return L"";
        }
        const std::wstring name = nameText();
        if (name.empty()) return L"Type a name for the project";
        if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return L"The name cannot hold \\ / : * ? \" < > |";
        if (locationText().empty()) return L"Type the folder for the project";
        std::error_code ec;
        const fs::path dir = projectFolder();
        if (fs::exists(dir, ec) && !fs::is_empty(dir, ec))
            return L"There is already a folder " + fs::path(dir).make_preferred().wstring() + L". Type another name, for example " +
                   newFreeName(name, fs::path(locationText())) + L".";
        if (copyMap || terrainOnly) {
            const std::wstring map = fromMapText();
            if (map.empty()) return terrainOnly ? L"Select the map whose terrain to use" : L"Select the map to copy into the project";
            if (!fs::is_regular_file(fs::path(map), ec)) return L"The map " + map + L" is not found";
        }
        return L"";
    }
    // The project's folder (<location>\<name>).
    fs::path projectFolder() const { return fs::path(locationText()) / fs::path(nameText()); }
    // A project name whose folder in `location` is not there yet (or is empty): `base`, else "base 2", "base 3", ...
    static std::wstring newFreeName(const std::wstring& base, const fs::path& location) {
        std::error_code ec;
        for (int k = 1; k < 1000; ++k) {
            const std::wstring name = k == 1 ? base : base + L" " + std::to_wstring(k);
            const fs::path dir = location / fs::path(name);
            if (!fs::exists(dir, ec) || fs::is_empty(dir, ec)) return name;
        }
        return base;
    }

    // Create: the map (blank or generated) in this tab; for a project, the project folder around it.
    bool create() {
        if (const std::wstring p = problem(); !p.empty()) return host_.message(p), false;
        return host_.make();
    }
    // Cancel and Back.
    void cancelPage() { keep = false, host_.cancel(); }
    void backToStart() { host_.back(); }

    // Each tileset's picture: a small generated map of it (once).
    void makeSwatches() {
        if (swatchesMade) return;
        swatchesMade = true;
        for (uint16_t era2 = 0; era2 < 8; ++era2) {
            Tileset ts;
            std::string err;
            if (!ts.load(appState().data, era2, err)) continue;
            TerrainGenParams p;
            p.width = p.height = 64, p.era = era2, p.seed = 7 + era2;
            std::vector<uint16_t> ts2;
            int tw = 0, th = 0;
            if (!newpage::makeTiles(ts, p, true, 1, ts2, tw, th)) continue;
            const std::vector<uint32_t> mega = newpage::megaColors(ts);
            newpage::paintTiles(ts, mega, newpage::tileColors(mega), ts2, tw, th, tw * 4, th * 4, swatch[era2]);
            newpage::toBitmap(swatch[era2], tw * 4, th * 4, swatchBitmap[era2]);
            swatchSide = tw * 4;
        }
    }

    // Loads the tileset for the generator: its terrains and one share slider for each (not the base ground).
    void newLoadTileset() {
        if (loadedEra == era) return;
        std::string err;
        if (!tileset.load(appState().data, era, err)) return;
        loadedEra = era;
        roles = generatorTerrains(era, tileset);
        megaColor = newpage::megaColors(tileset);
        tileColor = newpage::tileColors(megaColor);
        shares.clear();
        for (const auto& r : roles) {
            if (r.role == GenTerrain::Base) continue;
            std::wstring name = std::wstring(size_t(std::max(0, r.depth - 1)) * 3, L' ') + widen(r.name);
            if (r.role == GenTerrain::Sea) name += L" (water)";
            if (r.role == GenTerrain::High) name += L" (high ground)";
            const int def = int(std::lround(r.defaultShare * 200));
            shares.push_back({name, 0, std::clamp(def * 3, 40, 160), def, r.id});
        }
        previewMade = false;
    }

    // The schedule of the preview's regeneration (a timer of the host).
    void schedule(int ms) {
        if (timer_) {
            if (draw::Host* hs = host()) hs->cancelTimer(timer_);
            timer_ = 0;
        }
        if (draw::Host* hs = host()) timer_ = hs->timer(ms, [this] {
            timer_ = 0;
            regenerate();
        });
    }

    // The page was shown again: it starts at the top.
    void resetScroll() {
        if (scroll) scroll = 0, invalidate();
    }
    // The page's kind and choices the card selected (the start page's cards: the same choices after Back).
    void setKind(int kind) {
        project = kind == 1, card = kind;
        keep = false, tallest = 0;
        if (!importing() && !previewMade) schedule(10);
        invalidate();
    }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override {
        if (e.down && e.key == draw::Key::Escape) return backToStart(), true;
        return false;
    }
    bool vscroll(int code, int pos, int track, int) override;
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, height_ - 1), page = height(), pos = scroll;
        return true;
    }
    void resized() override { host_.pageChanged(); }
    draw::Cursor cursor(draw::Point pt) override {
        const int hi = hitAt(pt);
        return hi >= 0 ? hits[size_t(hi)].cursor : draw::Cursor::Arrow;
    }
    std::wstring tip(draw::Point pt) override {
        const int hi = tipAt(pt);
        return hi >= 0 ? hits[size_t(hi)].tip : std::wstring();
    }
    bool hoverTip() const override { return true; }
    std::wstring feedbackName(draw::Point pt) override {
        const int hi = tipAt(pt);
        if (hi >= 0 && !hits[size_t(hi)].name.empty()) return hits[size_t(hi)].name;
        if (hi >= 0) return hits[size_t(hi)].tip;
        return L"the New page";
    }
    bool focusable() const override { return false; }
    void captureLost() override;

    // The painted parts (what the last paint put under the mouse).
    struct Hit {
        draw::Rect r;
        std::function<void()> click;            // a press that acts once
        std::function<void(draw::Point)> press;  // a press at a point, and the drag after it (a slider)
        std::function<void(int)> wheel;         // a notch of the wheel over it (up: 1)
        std::wstring tip, name;
        draw::Cursor cursor = draw::Cursor::Arrow;
    };
    std::vector<Hit> hits;
    std::vector<std::pair<draw::Rect, std::function<void(int)>>> wheels;

    // The text fields are placed by the paint (hidden where they do not show, and under the Create bar).
    void placeField(stardraft::kit::TextField& f, draw::Rect r, bool show, int clipTop, int clipBottom) {
        show = show && r.top >= clipTop && r.bottom <= clipBottom;
        if (show && !sameRect(f.frame(), r)) f.setFrame(r);
        if (f.visible() != show) f.setVisible(show);
    }
    // The fields are hidden (the page is not shown: the start page's).
    void hideFields() {
        for (stardraft::kit::TextField* f : {&name_, &location_, &output_, &seed_, &fromMap_}) placeField(*f, {}, false, 0, 0);
    }

private:
    int hitAt(draw::Point pt) const {
        for (size_t i = hits.size(); i-- > 0;)
            if (hits[i].r.contains(pt)) return int(i);
        return -1;
    }
    int tipAt(draw::Point pt) const {
        for (size_t i = hits.size(); i-- > 0;)
            if (hits[i].r.contains(pt) && (!hits[i].tip.empty() || !hits[i].name.empty())) return int(i);
        return -1;
    }
    bool hot(draw::Rect r) const { return host() && r.contains(host()->mouse(*this)); }
    void hitAdd(draw::Rect r, std::function<void()> click, std::wstring tip = L"", draw::Cursor cur = draw::Cursor::Hand) {
        Hit h;
        h.r = r, h.click = std::move(click), h.tip = std::move(tip), h.cursor = cur;
        hits.push_back(std::move(h));
    }
    int dragging_ = -1;  // the hit with a press at a point (a slider) that the mouse holds
    static bool sameRect(draw::Rect a, draw::Rect b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
    // The kit's button face (Normal, Primary; the states as the page had them).
    enum class BtnState { Normal, Hot, Down, Disabled };
    void buttonFace(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool primary, BtnState s);
    int buttonWidth(draw::Canvas& c, const std::wstring& text) { return std::max(scale(stardraft::kit::kButtonMinW), c.measure(text) + scale(28)); }
    void button(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool primary, std::function<void()> click, const std::wstring& tip = L"", bool enabled = true);
    int smallButton(draw::Canvas& c, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"");
    int labelAt(draw::Canvas& c, int x, int yy, int w, const wchar_t* t, bool enabled = true);
    int paragraph(draw::Canvas& c, draw::Rect r, const std::wstring& text, draw::Color color);
    int titleBlock(draw::Canvas& c, int x, int y, int right, const std::wstring& text, const std::wstring& sub, UiIcon icon);
    int sectionHeader(draw::Canvas& c, int x0, int colRight, int y, int rowH, const wchar_t* name);
    void pickBoxFace(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool changed, bool over, bool enabled = true);
    void sliderFace(draw::Canvas& c, int x0, int x1, int cy, float frac, bool over, bool enabled = true);
    void radioMark(draw::Canvas& c, draw::Rect b, bool on, bool over);
    void listRowFace(draw::Canvas& c, draw::Rect r, bool selected, bool over);
    void paintPreview(draw::Canvas& c, draw::Rect pv);
    void paintSizeRow(draw::Canvas& c, int x, int y, int labelW, int right, const wchar_t* name, uint16_t& value, int which);
    void paintSliderRow(draw::Canvas& c, int x, int y, int labelW, int right, NewSlider& sl, bool percent);
    void pickTerrain(draw::Rect b);
    void pickSymmetry(draw::Rect b);
    int openEra_ = 0;
    NewPageHost& host_;
    stardraft::kit::TextField name_, location_, output_, seed_, fromMap_;
    int timer_ = 0;
    int hover_ = -1;
};

// The tileset's terrain that a blank map is filled with (or the base ground).
uint16_t NewPageView::terrainId() {
    newLoadTileset();
    if (terrain) return terrain;
    return roles.empty() ? uint16_t(1) : roles[0].id;
}

// Makes the view's preview picture from its tiles (at the size it has on the page).
void NewPageView::paintPreview(draw::Canvas& c, draw::Rect pv) {
    c.fill(pv, RGB(0, 0, 0));  // a picture of the game: dark in both themes
    // The last picture stays until the new one is made (a change makes it again a moment later; no black frames).
    if (tilesW > 0 && tilesH > 0) {
        const int side = std::min(pv.width(), pv.height()), big = std::max(tilesW, tilesH);
        const int pw = std::max(1, side * tilesW / big), ph = std::max(1, side * tilesH / big);
        if (pixelsVersion != tilesVersion || previewW != pw || previewH != ph) {
            newpage::paintTiles(tileset, megaColor, tileColor, tiles, tilesW, tilesH, pw, ph, pixels);
            newpage::toBitmap(pixels, pw, ph, previewBitmap);
            previewW = pw, previewH = ph, pixelsVersion = tilesVersion;
        }
        // Fitted into the preview, keeping the proportions, in the middle.
        const int rw = pv.width(), rh = pv.height();
        const double k = std::min(double(rw) / pw, double(rh) / ph);
        const int bw = int(pw * k), bh2 = int(ph * k);
        c.bitmap(previewBitmap, {0, 0, pw, ph}, {pv.left + (rw - bw) / 2, pv.top + (rh - bh2) / 2, pv.left + (rw - bw) / 2 + bw, pv.top + (rh - bh2) / 2 + bh2},
                 true);
    }
    c.roundBox(pv, draw::kNone, kBorder, float(scale(2)));
}
