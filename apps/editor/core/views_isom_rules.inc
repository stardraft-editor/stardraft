// Tools > Cosmonarchy > Custom isom terrain editor (portable, core.inc; research/terrain-builder/PLAN.md). The mod's
// isometric terrain rules (stardraft/mods/terrain_rules.h): for each mod terrain of a tileset, the mod tile of each rect
// of its model (a game terrain's border). Left: the tileset's terrains (search; New terrain). Middle: the puzzle, the
// selected terrain painted by the real brush on its bordering terrain in small examples (diamonds of 0.5 to 3 and two
// that overlap: their inner corners); a rect with no mod tile is a NULL hole (black, a red frame). Click a rect or a hole:
// it is the target. Right: the tileset's tiles, a tile group a row (16 subtiles), the column's width dragged at its left
// edge; a click puts the tile into the target (Rect: both halves, the subtile pair). Load and Save the rules file
// (.sdterrain); the ISOM terrain rules override (Program options > Plugins) uses such a file. Closing with unsaved
// changes asks Save, Do not save or Cancel. Terse texts: a tool for the mod's tile makers (not STE100).

const GameData* cmbwGameData(std::string& err);  // (core/cmbw.inc)
void cmbwApplyTerrainOverride();
void shellShowIsomRulesWindow();  // (the host: the window, once)

namespace isomrules {

using stardraft::terrainrules::Kind;
using ModTerrain = stardraft::Tileset::ModTerrain;

inline const wchar_t* kindName(Kind k) {
    return k == Kind::Field ? L"field" : k == Kind::Path ? L"path" : k == Kind::Wall ? L"wall" : L"?";
}
inline bool isNull(const std::array<uint16_t, 3>& p) { return p[1] == 0 && p[2] == 1; }
// A half with no tile yet (the null tile's subtile: 0 or 1); a piece with one: not done.
inline bool nullHalf(uint16_t v) { return v <= 1; }
inline bool anyNull(const std::array<uint16_t, 3>& p) { return nullHalf(p[1]) || nullHalf(p[2]); }

// A picture of map tiles (SD), as a bitmap (each tile 32 x 32).
inline std::unique_ptr<draw::Bitmap> tilesBitmap(const stardraft::Tileset& ts, const std::vector<uint16_t>& tiles, int w, int h) {
    const stardraft::Sprite s = stardraft::mapTilesPicture(ts, tiles, w, 0, 0, w, h);
    if (s.width <= 0 || s.height <= 0) return nullptr;
    auto b = std::make_unique<draw::Bitmap>(s.width, s.height);
    uint32_t* px = b->pixels();
    for (size_t i = 0; i < size_t(s.width) * size_t(s.height); ++i) {
        const uint8_t* p = &s.rgba[i * 4];
        px[i] = p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | 0xFF000000u;
    }
    b->changed();
    return b;
}

// The window's worker thread: the tileset loads and the examples paint there, one job after the other (the window stays
// open and answers meanwhile). It is never stopped: the program's end stops it.
class Worker {
public:
    void post(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (stopped_) return;
            if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
            jobs_.push_back(std::move(f));
        }
        cv_.notify_one();
    }
    // The program ends: the jobs that wait go, the one that runs ends first (before the window and the game data go).
    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stopped_ = true, jobs_.clear();
        }
        cv_.notify_one();
        if (thread_.joinable()) thread_.join();
    }

private:
    void run() {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return !jobs_.empty() || stopped_; });
                if (stopped_) return;
                f = std::move(jobs_.front());
                jobs_.pop_front();
            }
            f();
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    std::thread thread_;
    bool stopped_ = false;
};
inline Worker& worker() {
    static Worker w;
    return w;
}

}  // namespace isomrules

class IsomRulesView final : public draw::View {
public:
    IsomRulesView()
        : load_(L"Load\x2026", stardraft::kit::Tone::Normal, [this] { loadFile(); }),
          loadDefault_(L"Load default", stardraft::kit::Tone::Normal, [this] { loadDefault(); }),
          save_(L"Save", stardraft::kit::Tone::Primary, [this] { save(false); }),
          saveAs_(L"Save as\x2026", stardraft::kit::Tone::Normal, [this] { save(true); }),
          nextNull_(L"Next null (N)", stardraft::kit::Tone::Normal, [this] { nextNull(); }),
          newTerrain_(L"New terrain\x2026", stardraft::kit::Tone::Normal, [this] { newTerrain(); }) {
        std::vector<stardraft::kit::DropdownItem> eras;
        for (int e = 0; e < 8; ++e) eras.push_back({widen(tilesetName(uint16_t(e)))});
        era_.setItems(eras), era_.setValue(0);
        era_.onChange = [this](int e) { openEra(e); };
        search_.setPlaceholder(L"Search");
        search_.onChange = [this] { filter(); };
        list_.label = [this](int i) {
            if (i < 0 || i >= int(shown_.size())) return std::wstring();
            const auto& x = rules().terrains[size_t(shown_[size_t(i)])];
            const int n = nulls(x.t);
            return widen(x.t.name) + L"   " + isomrules::kindName(x.kind) + (n ? L" \x00b7 " + std::to_wstring(n) + L" null" : L" \x00b7 ok");
        };
        list_.onSelect = [this] { selectTerrain(); };
        ground_.onChange = [this](int i) { setGround(i); };
        row_.searchable = true, row_.wheelSteps = false;
        row_.setTip(L"Go to a row of the tiles (a group: type its number or its terrain). Ctrl+click a rect of the example: its tiles");
        row_.onChange = [this](int g) { showRow(g, -1); };
        palBar_.onScroll = [this](int pos) { palScroll_ = pos, invalidate(); };
        for (draw::View* v : std::initializer_list<draw::View*>{&era_, &load_, &loadDefault_, &save_, &saveAs_, &nextNull_, &row_, &search_,
                                                               &list_, &newTerrain_, &ground_, &palBar_})
            add(*v, {0, 0, 0, 0});
    }
    static draw::Point size() { return {scale(1500), scale(900)}; }

    // Off: the work is done at once, on this thread (a snapshot's run).
    void setAsync(bool on) { async_ = on; }
    // The program ends: the worker ends (after the job that runs).
    void shutdown() {
        ++buildGen_, ++openGen_;
        isomrules::worker().stop();
    }
    // Opens with the rules in use now (the built-in ones, the mod's file, the override) of the open map's tileset (it
    // loads on the worker: the window shows Loading until then).
    void open() {
        const AppState& app = shellState();
        if (doc_.tilesets.empty()) openEra(app.ed && app.ed->isOpen() ? int(app.ed->era()) & 7 : 0);
        else rebuild();
    }
    bool dirty() const { return dirty_; }
    // Selects the terrain of that name in the list (a snapshot's isomrules:<name>); false: none.
    bool selectByName(const std::string& name) {
        for (size_t i = 0; i < shown_.size(); ++i)
            if (rules().terrains[size_t(shown_[i])].t.name == name) return list_.selectOnly(int(i)), selectTerrain(), true;
        return false;
    }
    // The window is closing: unsaved changes ask Save, Do not save or Cancel. False: it stays open.
    bool confirmClose() {
        if (!dirty_) return true;
        const int r = msgbox::ask(*this, L"Custom isom terrain editor", L"Save the changes to the terrain rules?", {L"&Save", L"Do&n't save", L"Cancel"},
                                  msgbox::Icon::Question);
        if (r == 0) return save(false);
        if (r != 1) return false;
        doc_ = {}, dirty_ = false;  // (Don't save: the edits go; the next open starts from the rules in use)
        return true;
    }
    bool closeRequested() override { return confirmClose(); }

    void resized() override { layout(); }
    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || search_.hasFocus()) return false;
        if (e.key == static_cast<draw::Key>('N') && !e.mods) return nextNull(), true;
        if (e.key == static_cast<draw::Key>('S') && (e.mods & draw::kCtrl)) return save(false), true;
        return false;
    }
    std::wstring tip(draw::Point p) override;
    draw::Cursor cursor(draw::Point p) override { return std::abs(p.x - palette_.left) < scale(4) ? draw::Cursor::SizeWE : draw::Cursor::Arrow; }

    // (the snapshot's lines: the terrain, its holes and the target)
    std::string state() const {
        const auto* t = terrain();
        return t ? "isom rules: " + t->t.name + ", " + std::to_string(nulls(t->t)) + " null, " + std::to_string(holes_.size()) + " holes in the examples, " +
                       std::to_string(absent_.size()) + " not in them" + (target_ >= 0 ? ", target piece " + std::to_string(target_) : "")
                 : "isom rules: no terrain";
    }

private:
    // A rect of the examples: its tile place, its piece of the terrain (-1: another terrain's).
    struct Place {
        int tx = 0, ty = 0;
        int piece = -1;
    };
    // The rules of the open tileset (made by openEra: an empty one, not kept in the file, until the tileset loads).
    stardraft::terrainrules::TilesetRules& rules() {
        for (auto& ts : doc_.tilesets)
            if (ts.era == era()) return ts;
        static stardraft::terrainrules::TilesetRules none;
        none = {};
        return none;
    }
    void setRules(stardraft::terrainrules::TilesetRules r) {
        // (the file keeps no variants: they are made from their terrains when the rules load)
        r.terrains.erase(std::remove_if(r.terrains.begin(), r.terrains.end(), [](const auto& x) { return x.t.variantOf != 0; }), r.terrains.end());
        for (auto& ts : doc_.tilesets)
            if (ts.era == r.era) return void(ts = std::move(r));
        doc_.tilesets.push_back(std::move(r));
    }
    const stardraft::terrainrules::TilesetRules* rulesOrNull() const {
        for (const auto& ts : doc_.tilesets)
            if (ts.era == era()) return &ts;
        return nullptr;
    }
    uint8_t era() const { return uint8_t(std::max(0, era_.value()) & 7); }
    const stardraft::terrainrules::Terrain* terrain() const {
        const auto* r = rulesOrNull();
        return r && sel_ >= 0 && sel_ < int(r->terrains.size()) ? &r->terrains[size_t(sel_)] : nullptr;
    }
    static int nulls(const isomrules::ModTerrain& m) {
        return int(std::count_if(m.pieces.begin(), m.pieces.end(), [](const auto& p) { return isomrules::anyNull(p); }));
    }

    // The tileset of an era with the rules files (shipped, override), or without the override (`plain`). (On the worker.)
    static bool loadTileset(stardraft::Tileset& ts, int e, bool plain, std::string& err) {
        const GameData* data = cmbwGameData(err);
        if (!data) return false;
        stardraft::cmbw::skipTerrainOverride(plain);
        const bool ok = ts.load(*data, uint16_t(e), err);
        stardraft::cmbw::skipTerrainOverride(false);
        return ok;
    }
    // A job on the worker (off: now), and its end back on the window's thread.
    void background(std::function<void()> f) {
        if (!async_) return f();
        isomrules::worker().post(std::move(f));
    }
    void foreground(std::function<void()> f) {
        if (!async_) return f();
        shellPost(std::move(f));
    }
    struct Opened {
        stardraft::Tileset tileset;
        stardraft::terrainrules::TilesetRules rules;
        std::string err;
        bool ok = false, plain = false;
    };
    void openEra(int e, bool plain = false) {
        era_.setValue(e);
        const int gen = ++openGen_;
        ++buildGen_, building_ = false, loading_ = true, shownId_ = -1;
        shown_.clear(), sel_ = -1, target_ = -1, list_.setCount(0), places_.clear(), holes_.clear(), absent_.clear(), picture_.reset();
        invalidate();
        background([this, gen, e, plain] {
            auto o = std::make_shared<Opened>();
            o->plain = plain;
            o->ok = loadTileset(o->tileset, e, plain, o->err);
            if (o->ok) o->rules = stardraft::terrainrules::rulesOf(o->tileset);
            foreground([this, gen, o] { opened(gen, *o); });
        });
    }
    void opened(int gen, Opened& o) {
        if (gen != openGen_) return;  // (another tileset was asked for meanwhile)
        loading_ = false;
        if (!o.ok) {  // (no tiles: not the last tileset's under this one's name)
            tileset_ = stardraft::Tileset(), pal_.clear(), palScroll_ = 0, jump_ = -1, row_.setItems({}), ground_.setItems({}), groundIds_.clear();
            invalidate();
            shellErrorBox(widen(o.err), L"Custom isom terrain editor");
            return;
        }
        tileset_ = std::move(o.tileset);
        if (!rulesOrNull() || o.plain) setRules(std::move(o.rules));
        groundIds_.clear();
        std::vector<stardraft::kit::DropdownItem> grounds;
        std::map<uint16_t, std::wstring> names;  // (a terrain id -> its name: the rows' terrains)
        if (const IsomTerrainTable* game = IsomTerrainTable::forEra(uint16_t(era())))
            for (uint16_t i = 1; i <= game->terrainCount(); ++i)
                if (game->isTerrain(i)) groundIds_.push_back(i), grounds.push_back({widen(game->terrainName(i))}), names[i] = widen(game->terrainName(i));
        for (const auto& x : rules().terrains) groundIds_.push_back(x.t.id), grounds.push_back({widen(x.t.name)}), names[x.t.id] = widen(x.t.name);
        ground_.setItems(grounds);
        // (the rows: each group of the tileset, by its number and the terrain its tiles are)
        std::vector<std::wstring> of(tileset_.groups.size());
        for (const auto& [g, tr, other] : tileset_.groupTerrains)
            if (g < of.size() && names.count(tr))
                of[g] = names[tr] + (other && names.count(other) ? L" / " + names[other] : L"");
        std::vector<stardraft::kit::DropdownItem> rows;
        for (size_t g = 0; g < tileset_.groups.size(); ++g) rows.push_back({std::to_wstring(g) + (of[g].empty() ? L"" : L"  " + of[g])});
        row_.setItems(rows), row_.setValue(-1);
        pal_.clear(), palScroll_ = 0, jump_ = -1;
        sel_ = -1;
        filter();
        if (!shown_.empty()) list_.selectOnly(0), selectTerrain();
        invalidate();
    }
    void filter() {
        shown_.clear();
        std::wstring q = search_.text();
        std::transform(q.begin(), q.end(), q.begin(), ::towlower);
        const auto& ts = rules();
        for (size_t i = 0; i < ts.terrains.size(); ++i) {
            if (ts.terrains[i].t.variantOf) continue;  // (variants: the brush's, made from their terrain)
            std::wstring n = widen(ts.terrains[i].t.name);
            std::transform(n.begin(), n.end(), n.begin(), ::towlower);
            if (q.empty() || n.find(q) != std::wstring::npos) shown_.push_back(int(i));
        }
        list_.setCount(int(shown_.size()));
    }
    void selectTerrain() {
        const int i = list_.cursor();
        sel_ = i >= 0 && i < int(shown_.size()) ? shown_[size_t(i)] : -1;
        target_ = -1;
        if (const auto* t = terrain())
            for (size_t k = 0; k < groundIds_.size(); ++k)
                if (groundIds_[k] == t->t.ground) ground_.setValue(int(k));
        rebuild();
    }
    void setGround(int i) {
        auto* t = sel_ >= 0 ? &rules().terrains[size_t(sel_)] : nullptr;
        if (!t || i < 0 || i >= int(groundIds_.size()) || groundIds_[size_t(i)] == t->t.id) return;
        const uint16_t g = groundIds_[size_t(i)];
        t->t.ground = g;
        const isomrules::ModTerrain* gm = tileset_.modTerrain(g);
        t->t.groundTemplate = gm ? gm->standIn : 0;
        changed();
    }

    // The example: a new map of the tileset, its bordering terrain, then the terrain in the fixed shape of its kind (a
    // wall's, a path's: diamonds of 1 that show its edges, corners and joints), with the working rules;
    // each rect of the terrain with its piece. It paints on the worker; another terrain's picture goes at once, the same
    // terrain's stays until the new one comes.
    struct BuildIn {
        int gen = 0;
        uint8_t era = 0;
        uint16_t root = 1;
        stardraft::terrainrules::Terrain t;
        std::vector<isomrules::ModTerrain> all;  // (the working rules: the terrain's pieces as the stand-in values)
        std::vector<uint16_t> free;              // (the stand-in values: piece k's halves are 2k and 2k + 1)
        const GameData* data = nullptr;
    };
    struct BuildOut {
        std::vector<Place> places;
        std::vector<int> holes, absent;
        draw::Rect crop;
        std::unique_ptr<draw::Bitmap> picture;
    };
    void rebuild() {
        const auto* t = terrain();
        std::string err;
        const GameData* data = t && !loading_ ? cmbwGameData(err) : nullptr;
        auto in = std::make_shared<BuildIn>();
        in->gen = ++buildGen_;
        if (!t || !data) {
            building_ = false, places_.clear(), holes_.clear(), absent_.clear(), picture_.reset();
            return invalidate();
        }
        in->era = era(), in->t = *t, in->data = data;
        uint16_t root = t->t.ground;  // (a game terrain under its grounds)
        for (int n = 0; n < 16 && root >= stardraft::Tileset::kModTerrainIds; ++n)
            if (const isomrules::ModTerrain* m = tileset_.modTerrain(root)) root = m->ground; else break;
        in->root = root ? root : 1;
        // (each half of each piece of the terrain as a value that no tile uses: past the tileset's groups, else an empty
        // subtile; the painted map then tells which piece is in each rect, and its half; the picture puts the piece's own
        // tiles there)
        const size_t need = 2 * t->t.pieces.size();
        for (uint32_t v = uint32_t(tileset_.groups.size()) * 16; v <= 0xFFFF && in->free.size() < need; ++v) in->free.push_back(uint16_t(v));
        for (uint32_t v = 16; v <= 0xFFFF && in->free.size() < need; ++v)
            if (v / 16 < tileset_.groups.size() && !tileset_.groups[v / 16].megatiles[v % 16]) in->free.push_back(uint16_t(v));
        if (in->free.size() < need) {
            building_ = false, places_.clear(), holes_.clear(), absent_.clear(), picture_.reset();
            return invalidate();
        }
        for (const auto& x : rules().terrains) {
            in->all.push_back(x.t);
            if (x.t.id == t->t.id)
                for (size_t k = 0; k < in->all.back().pieces.size(); ++k)
                    in->all.back().pieces[k][1] = in->free[2 * k], in->all.back().pieces[k][2] = in->free[2 * k + 1];
        }
        if (shownId_ != sel_) places_.clear(), holes_.clear(), absent_.clear(), picture_.reset(), shownId_ = sel_;
        building_ = true;
        invalidate();
        background([this, in] {
            if (in->gen != buildGen_) return;  // (a newer one comes after it)
            auto out = std::make_shared<BuildOut>();
            build(*in, *out);
            foreground([this, in, out] {
                if (in->gen != buildGen_) return;
                building_ = false;
                places_ = std::move(out->places), holes_ = std::move(out->holes), absent_ = std::move(out->absent), crop_ = out->crop;
                picture_ = std::move(out->picture);
                invalidate();
            });
        });
    }
    // (on the worker: puzzleEd_ is its own)
    void build(const BuildIn& in, BuildOut& out) {
        const auto* t = &in.t;
        std::string err;
        if (!puzzleEd_) puzzleEd_ = std::make_unique<Editor>(*in.data);
        Editor* ed = puzzleEd_.get();
        const uint16_t root = in.root;
        constexpr int W = 40, H = 28;
        if (!ed->isOpen() || ed->era() != in.era) {
            if (!ed->newMap(W, H, in.era, root, err)) return;
        }
        std::unordered_map<uint16_t, uint32_t> index;  // (a stand-in value -> piece * 2 + half)
        for (uint32_t i = 0; i < in.free.size(); ++i) index[in.free[i]] = i;
        ed->useModTerrains(in.all);
        const int cx = W * 16, cy = H * 16;
        // (one shape for each kind: diamonds of 1, in steps of 64 to the side and 32 up or down from the middle; the
        // places that show the most rects of the mod's walls and paths: a search over all their terrains found the same
        // few each time)
        static const std::vector<std::pair<int, int>> kWall = {{0, 0}, {0, -2}, {-1, -3}, {-1, 3}, {3, -3}};
        static const std::vector<std::pair<int, int>> kPath = {{0, 0}, {0, -2}, {-1, 1}, {0, 2}, {3, 1}, {3, -3}, {-3, -3}};
        std::vector<Spot> blob;
        for (const auto& [i, j] : t->kind == stardraft::terrainrules::Kind::Wall ? kWall : kPath) blob.push_back({cx + i * 64, cy + j * 32, 1.0f});
        auto paint = [&]() {  // (the bordering terrain all around: no other terrain in the picture; the shape)
            ed->paintBrush(cx, cy, root, Brush{Brush::Diamond, 64.0f});
            ed->paintBrush(cx, cy, t->t.ground ? t->t.ground : 1, Brush{Brush::Diamond, 24.0f});
            for (const auto& k : blob) ed->paintBrush(k.x, k.y, t->t.id, Brush{Brush::Diamond, k.s});
        };
        paint();
        stardraft::TerrainLayer tl = stardraft::readTerrain(ed->chk(), "TILE");
        int x0 = W, y0 = H, x1 = -1, y1 = -1;
        std::set<uint16_t> seen;
        for (int ty = 0; ty < H; ++ty)
            for (int tx = 0; tx < W; ++tx) {
                uint16_t& v = tl.tiles[size_t(ty) * W + tx];
                const auto it = index.find(v);
                if (it == index.end()) continue;
                const size_t k = it->second / 2;
                const auto& q = t->t.pieces[k];
                const bool right = it->second & 1;
                if (!right) {
                    seen.insert(q[0]);
                    if (isomrules::anyNull(q)) out.holes.push_back(int(out.places.size()));
                    out.places.push_back({tx, ty, int(k)});
                    x0 = std::min(x0, tx), y0 = std::min(y0, ty), x1 = std::max(x1, tx + 1), y1 = std::max(y1, ty);
                }
                v = isomrules::nullHalf(q[right ? 2 : 1]) ? 0 : q[right ? 2 : 1];
            }
        for (size_t k = 0; k < t->t.pieces.size(); ++k)  // (its set's pieces and its gaps, not its fill: kindOf's rects)
            if (const uint16_t g = t->t.pieces[k][0]; (k < t->t.setPieces || k + t->t.gapPieces >= t->t.pieces.size()) && !seen.count(g) &&
                std::none_of(out.absent.begin(), out.absent.end(), [&](int a) { return t->t.pieces[size_t(a)][0] == g; }))
                out.absent.push_back(int(k));
        if (x1 < 0) x0 = 0, y0 = 0, x1 = W - 1, y1 = H - 1;
        x0 = std::max(0, x0 - 2), y0 = std::max(0, y0 - 1), x1 = std::min(W - 1, x1 + 2), y1 = std::min(H - 1, y1 + 1);
        out.crop = {x0, y0, x1 + 1, y1 + 1};
        std::vector<uint16_t> part;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) part.push_back(tl.tiles[size_t(y) * W + x]);
        out.picture = isomrules::tilesBitmap(ed->tileset(), part, x1 - x0 + 1, y1 - y0 + 1);
        if (out.picture) {  // (the tiles that are no rect of the terrain: dimmed, so its own pieces stand out)
            std::vector<uint8_t> own(size_t(x1 - x0 + 1) * size_t(y1 - y0 + 1), 0);
            for (const Place& pl : out.places)
                for (int d = 0; d < 2; ++d) own[size_t(pl.ty - y0) * size_t(x1 - x0 + 1) + size_t(pl.tx + d - x0)] = 1;
            uint32_t* px = out.picture->pixels();
            const int pw = out.picture->width();
            for (int y = 0; y < out.picture->height(); ++y)
                for (int x = 0; x < pw; ++x)
                    if (!own[size_t(y / 32) * size_t(x1 - x0 + 1) + size_t(x / 32)]) {
                        uint32_t& c = px[size_t(y) * pw + x];
                        c = 0xFF000000u | ((c >> 2) & 0x3F3F3F);
                    }
            out.picture->changed();
        }
    }
    // The tiles' row g in view, and tile v framed (-1: the whole row) until another is asked for.
    void showRow(int g, int v) {
        if (g < 0 || g >= int(tileset_.groups.size())) return;
        jump_ = v >= 0 ? v : g << 4, jumpRow_ = v < 0;
        row_.setValue(g);
        const int cell = std::max(scale(8), (palette_.width() - scale(56)) / 16);
        const int y = g * cell;
        if (y < palScroll_ || y + cell > palScroll_ + palette_.height())
            palScroll_ = std::clamp(y - palette_.height() / 2, 0, std::max(0, int(tileset_.groups.size()) * cell - palette_.height()));
        invalidate();
    }

    // A tile of the palette went into the target (Rect: the pair; Tile: the half that was clicked).
    void place(uint16_t value) {
        if (loading_) return;
        if (sel_ < 0 || target_ < 0) return setMessage(L"Click a rect or a NULL hole first");
        auto& m = rules().terrains[size_t(sel_)].t;
        if (target_ >= int(m.pieces.size())) return;
        auto p = m.pieces[size_t(target_)];
        p[size_t(1 + targetHalf_)] = value;  // (the half only: the other half's tile is the user's choice too)
        if (!isomrules::anyNull(p) && size_t(target_) >= m.pieces.size() - m.gapPieces) {  // (a gap's piece with both tiles: one of its set's now)
            m.pieces.erase(m.pieces.begin() + target_);
            m.pieces.insert(m.pieces.begin() + long(m.setPieces), p);
            target_ = int(m.setPieces), ++m.setPieces, --m.gapPieces;
            places_.clear(), holes_.clear(), absent_.clear();  // (their piece numbers moved: none until the new example)
        } else {
            m.pieces[size_t(target_)] = p;
        }
        if (isomrules::nullHalf(p[size_t(2 - targetHalf_)])) targetHalf_ = 1 - targetHalf_;  // (next: the other half, still NULL)
        changed();
        auto half = [](uint16_t v) { return isomrules::nullHalf(v) ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15); };
        setMessage(L"Piece " + std::to_wstring(p[0]) + L": " + half(p[1]) + L" " + half(p[2]));
    }
    void changed() {
        dirty_ = true;
        list_.invalidate();
        rebuild();
    }
    void nextNull() {
        const auto* t = terrain();
        if (!t) return;
        std::vector<int> nulls;
        for (int h : holes_) nulls.push_back(places_[size_t(h)].piece);
        for (int a : absent_)
            if (isomrules::anyNull(t->t.pieces[size_t(a)])) nulls.push_back(a);
        std::sort(nulls.begin(), nulls.end());
        if (nulls.empty()) return setMessage(L"No null left");
        auto it = std::upper_bound(nulls.begin(), nulls.end(), target_);
        target_ = it == nulls.end() ? nulls.front() : *it, targetHalf_ = isomrules::nullHalf(t->t.pieces[size_t(target_)][1]) ? 0 : 1;
        invalidate();
    }

    // New terrain: its model (an existing terrain: its rects, the game border it is made as), its bordering terrain
    // (the model's, or another known one), then its name ("Primary (Bordering)"); every rect NULL. Its kind comes from
    // the model.
    void newTerrain() {
        if (loading_) return setMessage(L"The tileset is loading");
        auto& ts = rules();
        std::vector<ListPickItem> models;
        for (size_t i = 0; i < ts.terrains.size(); ++i)
            if (!ts.terrains[i].t.variantOf)
                models.push_back({widen(ts.terrains[i].t.name) + L" \x00b7 " + isomrules::kindName(ts.terrains[i].kind), L"", std::to_string(i), "", -1});
        if (models.empty()) return setMessage(L"No terrain to copy a model from in this tileset");
        const draw::Rect at = newTerrain_.frame();
        ListPickView modelPick(L"Model: like", models, sel_ >= 0 ? std::to_string(sel_) : std::string());
        if (!runListPick(modelPick, {at.left, at.top - scale(400)}) || modelPick.result().empty()) return;
        const auto model = ts.terrains[size_t(std::stoi(modelPick.result()))];
        std::vector<ListPickItem> grounds;
        for (size_t k = 0; k < groundIds_.size(); ++k) grounds.push_back({ground_.items()[k].label, L"", std::to_string(groundIds_[k]), "", -1});
        ListPickView groundPick(L"Bordering", grounds, std::to_string(model.t.ground));
        if (!runListPick(groundPick, {at.left, at.top - scale(400)}) || groundPick.result().empty()) return;
        const uint16_t ground = uint16_t(std::stoi(groundPick.result()));
        std::wstring groundName;
        for (const auto& g : grounds)
            if (g.value == groundPick.result()) groundName = g.label;
        std::wstring name = L"Primary (" + groundName + L")";
        if (!shellPromptText(L"New terrain: its name (Primary (Bordering))", name) || name.empty()) return;
        std::set<uint16_t> ids, games;
        for (const auto& o : ts.terrains) ids.insert(o.t.id);
        for (const auto& m : tileset_.modTerrains) ids.insert(m.id);  // (and the variants' ids)
        uint16_t id = stardraft::Tileset::kModTerrainIds;
        while (ids.count(id) && id < stardraft::Tileset::kModVariantIds) ++id;
        if (id >= stardraft::Tileset::kModVariantIds) return shellErrorBox(L"No free terrain id left in this tileset.", L"New terrain");
        stardraft::terrainrules::Terrain x;
        x.kind = model.kind, x.t.id = id, x.t.name = narrow(name), x.t.standIn = model.t.standIn, x.t.ground = ground;
        const isomrules::ModTerrain* gm = tileset_.modTerrain(ground);
        x.t.groundTemplate = ground == model.t.ground ? model.t.groundTemplate : gm ? gm->standIn : 0;
        x.t.fill = false, x.t.fillKey = 0;
        for (const auto& p : model.t.pieces)  // (each rect of the model once, NULL; not its fill)
            if (games.insert(p[0]).second) x.t.pieces.push_back({p[0], 0, 1});
        x.t.setPieces = 0, x.t.gapPieces = x.t.pieces.size();
        ts.terrains.push_back(std::move(x));
        filter();
        for (size_t i = 0; i < shown_.size(); ++i)
            if (shown_[i] == int(ts.terrains.size()) - 1) list_.selectOnly(int(i));
        selectTerrain();
        dirty_ = true;
        setMessage(L"New terrain " + name + L": like " + widen(model.t.name) + L", id " + std::to_wstring(id));
    }

    void loadDefault() {
        if (dirty_ && !confirmClose()) return;
        doc_ = {}, file_.clear(), dirty_ = false;
        openEra(era(), true);
    }
    void loadFile() {
        if (dirty_ && !confirmClose()) return;
        ShellFilePick d;
        d.title = L"Load terrain rules";
        d.filters = {{L"Terrain rules (*.sdterrain)", L"*.sdterrain"}};
        const std::filesystem::path f = shellPickFile(d);
        if (f.empty()) return;
        stardraft::terrainrules::Rules r;
        std::string err;
        if (!stardraft::terrainrules::load(f, r, err)) return shellErrorBox(widen(err), L"Load terrain rules");
        doc_ = std::move(r), file_ = modsOwn(f) ? std::filesystem::path() : f, dirty_ = false;  // (the mod's own file: Save asks for another)
        openEra(era());  // (a tileset the file has no rules for: the ones in use now)
        setMessage(L"Loaded " + f.filename().wstring());
    }
    // A file in the mod's install folder (its own terrains.sdterrain: the default rules, with the built-in ones): never
    // written, never the file Save writes to.
    static bool modsOwn(const std::filesystem::path& f) {
        const std::filesystem::path install = stardraft::cmbw::findInstall();
        if (install.empty()) return false;
        std::error_code ec;
        const std::wstring a = std::filesystem::weakly_canonical(f, ec).wstring(), b = std::filesystem::weakly_canonical(install, ec).wstring();
        return a.size() > b.size() && std::equal(b.begin(), b.end(), a.begin(), [](wchar_t x, wchar_t y) { return ::towlower(x) == ::towlower(y); });
    }
    bool save(bool as) {
        if (doc_.tilesets.empty()) return false;
        std::filesystem::path f = file_;
        for (;;) {
            if (as || f.empty()) {
                ShellFilePick d;
                d.title = L"Save terrain rules";
                d.filters = {{L"Terrain rules (*.sdterrain)", L"*.sdterrain"}};
                d.defaultExtension = L"sdterrain", d.fileName = L"terrains", d.save = true;
                d.initialDir = shellDocumentsFolder() / "StarDraft";
                std::error_code ec;
                std::filesystem::create_directories(d.initialDir, ec);
                f = shellPickFile(d);
                if (f.empty()) return false;
            }
            if (!modsOwn(f)) break;
            shellErrorBox(L"The default rules cannot be overwritten. Save your rules to a file outside the mod's folder.", L"Save terrain rules");
            as = true;
        }
        std::string err;
        if (!stardraft::terrainrules::save(f, doc_, err)) return shellErrorBox(widen(err), L"Save terrain rules"), false;
        file_ = f, dirty_ = false;
        setMessage(L"Saved " + f.filename().wstring());
        invalidate();
        return true;
    }

    void layout();

    stardraft::kit::Dropdown era_{{}, -1}, ground_{{}, -1}, row_{{}, -1};
    stardraft::kit::ScrollBar palBar_;  // (the tiles' scroll bar, at the right edge)
    int palTotal_ = -1, palPage_ = -1;  // (the bar's range as it was last set)
    stardraft::kit::Button load_, loadDefault_, save_, saveAs_, nextNull_, newTerrain_;
    stardraft::kit::TextField search_;
    stardraft::kit::ListView list_;
    stardraft::terrainrules::Rules doc_;  // (the rules being edited: each tileset opened, or the loaded file's)
    std::filesystem::path file_;
    bool dirty_ = false;
    stardraft::Tileset tileset_;
    std::vector<uint16_t> groundIds_;
    std::vector<int> shown_;
    int sel_ = -1, target_ = -1, targetHalf_ = 0, hover_ = -1;
    std::vector<Place> places_;
    std::vector<int> holes_, absent_;
    std::unique_ptr<draw::Bitmap> picture_;
    struct Spot { int x, y; float s; };
    std::unique_ptr<Editor> puzzleEd_;  // (the examples' map, kept between rebuilds: its unit data loads once; the worker's)
    bool async_ = true, loading_ = false, building_ = false;
    int openGen_ = 0, shownId_ = -1, jump_ = -1;  // (jump_: the tile framed in the tiles; jumpRow_: its whole row)
    bool jumpRow_ = false;
    std::atomic<int> buildGen_{0};  // (the newest example asked for: the worker leaves out older ones)
    draw::Rect crop_, puzzle_, palette_, tray_;
    float zoom_ = 1;
    int paletteW_ = 0, palScroll_ = 0;
    bool dragging_ = false;
    std::map<int, std::unique_ptr<draw::Bitmap>> pal_;  // (the palette's pictures, 64 groups each)
};

void IsomRulesView::layout() {
    const int pad = scale(8), barH = scale(stardraft::kit::kButtonH), W = width(), H = height();
    int x = pad;
    auto put = [&](draw::View& v, int w) { v.setFrame({x, pad, x + w, pad + barH}), x += w + pad; };
    put(era_, scale(150)), put(load_, scale(80)), put(loadDefault_, scale(110)), put(save_, scale(70)), put(saveAs_, scale(90));
    x += scale(20);
    put(nextNull_, scale(110));
    x += scale(20);
    put(row_, scale(240));
    const int top = pad * 2 + barH, leftW = scale(280);
    if (!paletteW_) paletteW_ = 16 * scale(32) + scale(24);
    paletteW_ = std::clamp(paletteW_, scale(200), std::max(scale(200), W - leftW - scale(300)));
    search_.setFrame({pad, top, leftW - pad, top + barH});
    list_.setFrame({pad, top + barH + pad, leftW - pad, H - pad * 2 - barH});
    newTerrain_.setFrame({pad, H - pad - barH, leftW - pad, H - pad});
    const int barW = scale(stardraft::kit::kScrollBarW);
    palette_ = {W - paletteW_, top, W - barW, H};
    palBar_.setFrame({W - barW, top, W, H});
    const int mx0 = leftW, mx1 = W - paletteW_;
    ground_.setFrame({mx0 + scale(90), top, std::min(mx1 - pad, mx0 + scale(380)), top + barH});
    puzzle_ = {mx0, top + barH + pad, mx1, H - scale(70)};
    tray_ = {mx0, H - scale(70), mx1, H};
}

void IsomRulesView::paint(draw::Canvas& c) {
    const int W = width(), H = height(), pad = scale(8);
    c.fill({0, 0, W, H}, kBg);
    if (!puzzle_.width()) layout();
    // The title line's right: the file and the unsaved dot.
    c.text({W - scale(420), pad, W - pad, pad + scale(stardraft::kit::kButtonH)},
           (file_.empty() ? std::wstring(L"(not saved)") : file_.filename().wstring()) + (dirty_ ? L"  \x25cf" : L""), dirty_ ? kAccent : kDim,
           draw::kMiddle | draw::kRight | draw::kEllipsis);
    const auto* t = terrain();
    c.text({puzzle_.left + pad, puzzle_.top - scale(stardraft::kit::kButtonH) - pad, puzzle_.left + scale(88), puzzle_.top - pad}, L"Bordering", kDim,
           draw::kMiddle);
    if (t)
        c.text({std::min(puzzle_.right - pad, puzzle_.left + scale(390)), puzzle_.top - scale(stardraft::kit::kButtonH) - pad, puzzle_.right - pad,
                puzzle_.top - pad},
               std::wstring(isomrules::kindName(t->kind)) + L" \x00b7 id " + std::to_wstring(t->t.id) + L" \x00b7 " + std::to_wstring(nulls(t->t)) + L" null",
               kDim, draw::kMiddle | draw::kRight | draw::kEllipsis);
    // The puzzle: the examples, fit to the area; NULL holes framed red, the target orange, the hovered piece's places.
    c.fill(puzzle_, 0x151515);
    if (picture_ && t) {
        zoom_ = std::min(float(puzzle_.width() - 2 * pad) / picture_->width(), float(puzzle_.height() - 2 * pad) / picture_->height());
        zoom_ = std::max(0.1f, std::min(zoom_, 2.0f));
        c.pushClip(puzzle_);
        const int pw = int(picture_->width() * zoom_), ph = int(picture_->height() * zoom_);
        const int ox = puzzle_.left + (puzzle_.width() - pw) / 2, oy = puzzle_.top + (puzzle_.height() - ph) / 2;
        c.bitmap(*picture_, {0, 0, picture_->width(), picture_->height()}, {ox, oy, ox + pw, oy + ph});
        const float ts = 32 * zoom_;
        auto rectOf = [&](const Place& p) {
            const int x = ox + int((p.tx - crop_.left) * ts), y = oy + int((p.ty - crop_.top) * ts);
            return draw::Rect{x, y, x + int(2 * ts), y + int(ts)};
        };
        for (const Place& p : places_)
            if (hover_ >= 0 && p.piece == hover_) c.frame(rectOf(p), kBorderHot, scale(1));
        auto halfOf = [](draw::Rect r, int half) {
            return half ? draw::Rect{(r.left + r.right) / 2, r.top, r.right, r.bottom} : draw::Rect{r.left, r.top, (r.left + r.right) / 2, r.bottom};
        };
        for (int h : holes_) {  // (the halves with no tile: black, framed red)
            const Place& p = places_[size_t(h)];
            if (p.piece < 0 || p.piece >= int(t->t.pieces.size())) continue;
            const auto& q = t->t.pieces[size_t(p.piece)];
            for (int half = 0; half < 2; ++half)
                if (isomrules::nullHalf(q[size_t(1 + half)])) c.fill(halfOf(rectOf(p), half), 0x000000);
            c.frame(rectOf(p), 0x4650E6, scale(2));
        }
        for (const Place& p : places_)
            if (p.piece == target_ && target_ >= 0) c.frame(halfOf(rectOf(p), targetHalf_), kAccent, scale(2));
        c.popClip();
    } else {
        c.text(puzzle_, loading_ ? L"Loading the tileset\x2026" : building_ ? L"Painting the example\x2026" : t ? L"No picture (the mod's data?)" : L"Select a terrain",
               kDim, draw::kCenter | draw::kMiddle);
    }
    if (building_ && picture_)
        c.text({puzzle_.left + pad, puzzle_.top + pad, puzzle_.right - pad, puzzle_.top + scale(28)}, L"Painting\x2026", kDim, draw::kRight);
    // The tray: the rects of the model that the examples do not show.
    c.fill({tray_.left, tray_.top, tray_.right, tray_.top + 1}, kBorder);
    c.text({tray_.left + pad, tray_.top + pad, tray_.left + scale(110), tray_.top + scale(28)}, L"Not in examples", kDim, draw::kMiddle);
    if (t) {
        int x = tray_.left + scale(120);
        for (int a : absent_) {
            const auto& p = t->t.pieces[size_t(a)];
            const draw::Rect r{x, tray_.top + pad, x + scale(70), tray_.top + scale(28)};
            const int mid = (r.left + r.right) / 2;
            for (int half = 0; half < 2; ++half)  // (a half each: black while it has no tile)
                c.fill(half ? draw::Rect{mid, r.top, r.right, r.bottom} : draw::Rect{r.left, r.top, mid, r.bottom},
                       isomrules::nullHalf(p[size_t(1 + half)]) ? 0x000000 : kPanel);
            c.fill({mid, r.top, mid + 1, r.bottom}, kBorder);
            c.frame(r, isomrules::anyNull(p) ? 0x4650E6 : kBorder, scale(1));
            if (a == target_)
                c.frame(targetHalf_ ? draw::Rect{mid, r.top, r.right, r.bottom} : draw::Rect{r.left, r.top, mid, r.bottom}, kAccent, scale(2));
            c.text(r, std::to_wstring(p[0]), isomrules::anyNull(p) ? draw::Color(0xE0E0E0) : kText, draw::kCenter | draw::kMiddle);
            x += scale(76);
            if (x > tray_.right - scale(76)) break;
        }
    }
    // The palette: a tile group a row, its 16 subtiles; the target's tiles framed.
    c.fill(palette_, kPanel);
    c.fill({palette_.left, palette_.top, palette_.left + 1, palette_.bottom}, kBorder);
    const int cell = std::max(scale(8), (palette_.width() - scale(56)) / 16), x0 = palette_.left + scale(48);
    const int rows = int(tileset_.groups.size());
    {  // (the pictures of 64 groups each: only those near the view stay, about 4 MB each)
        const int first = palScroll_ / std::max(1, cell) / 64, last = (palScroll_ + palette_.height()) / std::max(1, cell) / 64;
        for (auto it = pal_.begin(); it != pal_.end();)
            it = it->first < first - 1 || it->first > last + 1 ? pal_.erase(it) : std::next(it);
    }
    if (const int total = rows * cell, page = palette_.height(); total != palTotal_ || page != palPage_) palBar_.setRange(total, page), palTotal_ = total, palPage_ = page;
    palBar_.setPos(palScroll_);
    c.pushClip(palette_);
    for (int g = std::max(0, palScroll_ / cell); g < rows && (g * cell - palScroll_) < palette_.height(); ++g) {
        const int y = palette_.top + g * cell - palScroll_;
        if (g % 4 == 0) c.text({palette_.left + scale(4), y, x0 - scale(4), y + cell}, std::to_wstring(g), kDim, draw::kMiddle | draw::kRight);
        const int chunk = g / 64;
        auto& b = pal_[chunk];
        if (!b) {
            std::vector<uint16_t> tiles;
            for (int k = chunk * 64; k < chunk * 64 + 64; ++k)
                for (int s = 0; s < 16; ++s) tiles.push_back(k < rows && tileset_.megatileFor(uint16_t(k << 4 | s)) > 0 ? uint16_t(k << 4 | s) : 0);
            b = isomrules::tilesBitmap(tileset_, tiles, 16, 64);
        }
        if (b) c.bitmap(*b, {0, (g % 64) * 32, 16 * 32, (g % 64) * 32 + 32}, {x0, y, x0 + 16 * cell, y + cell});
    }
    if (t && target_ >= 0 && target_ < int(t->t.pieces.size()))
        for (int half = 1; half <= 2; ++half) {
            const uint16_t v = t->t.pieces[size_t(target_)][size_t(half)];
            if (isomrules::nullHalf(v)) continue;
            const int y = palette_.top + int(v >> 4) * cell - palScroll_, x = x0 + int(v & 15) * cell;
            c.frame({x, y, x + cell, y + cell}, kAccent, scale(2));
        }
    if (jump_ >= 0) {  // (the row or the tile that was asked for)
        const int g = jump_ >> 4, y = palette_.top + g * cell - palScroll_;
        const int x = jumpRow_ ? x0 : x0 + (jump_ & 15) * cell, w = jumpRow_ ? 16 * cell : cell;
        c.frame({x - 1, y - 1, x + w + 1, y + cell + 1}, 0x30C8FF, scale(2));
    }
    c.popClip();
}

bool IsomRulesView::mouse(const draw::MouseEvent& e) {
    const auto* t = terrain();
    const int cell = std::max(scale(8), (palette_.width() - scale(56)) / 16), x0 = palette_.left + scale(48);
    if (e.kind == draw::MouseKind::Wheel && palette_.contains(e.pt)) {
        palScroll_ = std::clamp(palScroll_ - int(draw::wheelDistance(e, float(cell * 3))), 0, std::max(0, int(tileset_.groups.size()) * cell - palette_.height()));
        invalidate();
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 0 && std::abs(e.pt.x - palette_.left) < scale(4)) {
        dragging_ = true;
        if (host()) host()->capture(*this, true);
        return true;
    }
    if (dragging_ && e.kind == draw::MouseKind::Move) {
        paletteW_ = width() - e.pt.x;
        layout(), invalidate();
        return true;
    }
    if (dragging_ && e.kind == draw::MouseKind::Up) {
        dragging_ = false;
        if (host()) host()->capture(*this, false);
        return true;
    }
    // (the examples' place under the point: its rect, and the half)
    auto placeAt = [&](draw::Point pt, int& half) -> int {
        if (!picture_ || !puzzle_.contains(pt)) return -1;
        const int pw = int(picture_->width() * zoom_), ph = int(picture_->height() * zoom_);
        const int ox = puzzle_.left + (puzzle_.width() - pw) / 2, oy = puzzle_.top + (puzzle_.height() - ph) / 2;
        const int tx = crop_.left + int(float(pt.x - ox) / (32 * zoom_)), ty = crop_.top + int(float(pt.y - oy) / (32 * zoom_));
        for (size_t i = 0; i < places_.size(); ++i)
            if (places_[i].ty == ty && (tx == places_[i].tx || tx == places_[i].tx + 1)) return half = tx - places_[i].tx, int(i);
        return -1;
    };
    if (e.kind == draw::MouseKind::Move) {
        int half = 0;
        const int i = placeAt(e.pt, half), h = i >= 0 ? places_[size_t(i)].piece : -1;
        if (h != hover_) hover_ = h, invalidate();
        return false;
    }
    if (e.kind != draw::MouseKind::Down || e.button != 0 || !t || loading_) return false;
    if (int half = 0; placeAt(e.pt, half) >= 0) {
        target_ = places_[size_t(placeAt(e.pt, half))].piece, targetHalf_ = half;
        if (e.mods & draw::kCtrl) {  // (its tiles in the palette: the half clicked)
            const auto& q = t->t.pieces[size_t(target_)];
            if (isomrules::nullHalf(q[size_t(1 + half)])) setMessage(L"Rect " + std::to_wstring(q[0]) + L": this half is NULL, no tile");
            else showRow(q[1 + half] >> 4, q[1 + half]);
        }
        invalidate();
        return true;
    }
    if (tray_.contains(e.pt)) {  // (a rect that the examples do not show)
        const int k = (e.pt.x - tray_.left - scale(120)) / scale(76), in = (e.pt.x - tray_.left - scale(120)) % scale(76);
        if (k >= 0 && k < int(absent_.size()) && in < scale(70))  // (the chip's half that was clicked)
            target_ = absent_[size_t(k)], targetHalf_ = in >= scale(35) ? 1 : 0, invalidate();
        return true;
    }
    if (palette_.contains(e.pt) && e.pt.x >= x0) {
        const int g = (e.pt.y - palette_.top + palScroll_) / cell, s = (e.pt.x - x0) / cell;
        if (g >= 0 && g < int(tileset_.groups.size()) && s >= 0 && s < 16 && tileset_.megatileFor(uint16_t(g << 4 | s)) > 0) place(uint16_t(g << 4 | s));
        return true;
    }
    return false;
}

std::wstring IsomRulesView::tip(draw::Point p) {
    const auto* t = terrain();
    if (!t) return {};
    const int cell = std::max(scale(8), (palette_.width() - scale(56)) / 16), x0 = palette_.left + scale(48);
    if (palette_.contains(p) && p.x >= x0) {
        const int g = (p.y - palette_.top + palScroll_) / cell, s = (p.x - x0) / cell;
        return std::to_wstring(g) + L"." + std::to_wstring(s);
    }
    if (hover_ >= 0 && hover_ < int(t->t.pieces.size())) {
        const auto& q = t->t.pieces[size_t(hover_)];
        auto half = [](uint16_t v) { return isomrules::nullHalf(v) ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15); };
        return L"rect " + std::to_wstring(q[0]) + L" \x2192 " + half(q[1]) + L" " + half(q[2]);
    }
    return {};
}

IsomRulesView gIsomRulesView;

// Tools > Cosmonarchy > Custom isom terrain editor.
void isomRulesShow() {
    gIsomRulesView.open();
    shellShowIsomRulesWindow();
}
// The program's end: the window's worker stops (before the windows and the game data go).
void isomRulesShutdown() { gIsomRulesView.shutdown(); }
// Quit: the editor's unsaved rules ask first (false: the quit stops).
bool isomRulesConfirmQuit() { return gIsomRulesView.confirmClose(); }
