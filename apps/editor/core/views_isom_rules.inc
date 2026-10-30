// Tools > Cosmonarchy > Custom isom terrain editor (portable, core.inc; research/terrain-builder/STATUS.md). The mod's
// isometric terrain rules (stardraft/mods/terrain_rules.h): each mod terrain is a game border (its template: a stand-in
// terrain on a ground) with the mod's tiles in the template's rects. Left: the tileset's terrains (search; New terrain
// from a template, with the game's tiles or empty). Middle: the example, the terrain painted by the real brush on its
// bordering terrain (a NULL half: black with the game's tile dimmed, a red frame; a borrowed rect, the game's own tiles:
// a blue frame); under it the target rect large (the game's tiles beside yours) and the checklist: every rect of the
// template by its role (edge N, corner SE top...) with its state. Click a rect, a hole or a checklist row: it is the
// target. Right of the example (or left: the tiles' right-click menu): the tileset's tiles, a tile group a row (16
// subtiles), the column's width dragged at the edge between them; a click puts the tile into the target (2 tiles: both halves, the tile and the next in its row; 1 tile: the half), and a
// done rect hands the target to the next rect with no tile; Up / Down walk the checklist; the tiles scroll to the
// target's row. Ctrl+Z / Ctrl+Y undo
// and redo; Clear all makes every rect NULL; Holes get the game's tiles; Copy from takes another terrain's tiles by
// role; Draft marks a terrain as not checked yet. Load and Save the rules file (.sdterrain); the ISOM terrain rules
// override (Program options > Plugins) uses such a file. Closing with unsaved changes asks Save, Do not save or Cancel.
// Terse texts: a tool for the mod's tile makers (not STE100).

const GameData* cmbwGameData(std::string& err);  // (core/cmbw.inc)
void cmbwApplyTerrainOverride();
void shellShowIsomRulesWindow();  // (the host: the window, once)
int shellContextMenuAt(draw::View& from, draw::Point at, const std::vector<std::wstring>& items);  // (the host)

namespace isomrules {

using stardraft::terrainrules::Family;
using stardraft::terrainrules::PieceState;
using stardraft::terrainrules::TemplateRect;
using ModTerrain = stardraft::Tileset::ModTerrain;

inline std::wstring familyName(Family f) { return widen(stardraft::terrainrules::familyName(f)); }
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
        : menu_(L"Menu", stardraft::kit::Tone::Normal, [this] { showMenu(); }),
          refShow_(L"Reference overlay", stardraft::kit::Tone::Normal, [this] { toggleReference(); }),
          newTerrain_(L"New terrain\x2026", stardraft::kit::Tone::Normal, [this] { newTerrain(); }) {
        std::vector<stardraft::kit::DropdownItem> eras;
        for (int e = 0; e < 8; ++e) eras.push_back({widen(tilesetName(uint16_t(e)))});
        era_.setItems(eras), era_.setValue(0);
        era_.onChange = [this](int e) { openEra(e); };
        search_.setPlaceholder(L"Search (name, family, draft)");
        search_.onChange = [this] { filter(); };
        sideBar_.onScroll = [this](int pos) { sideScroll_ = pos, invalidate(); };
        ground_.onChange = [this](int i) { setGround(i); };
        menu_.setTip(L"File, terrain and rect actions. Keys: N next NULL, T pair / single, Del NULL, V verified, F to-do filter, G palette filter, M next group block, "
                     L"R reference, Up / Down rects, Ctrl+Up / Down opacity, Ctrl+Z / Y undo, Ctrl+S save");
        refPick_.searchable = true, refPick_.wheelSteps = false;
        refPick_.setTip(L"Reference border rendered in the same shape (Template: the terrain's own). R cycles off / overlay / only");
        refPick_.onChange = [this](int) { rebuild(); };
        refShow_.setTip(L"Render the reference border over the example. Key: R");
        refOpacity_.setTip(L"Reference opacity; 100 % shows the reference alone. Keys: Ctrl+Up, Ctrl+Down");
        refOpacity_.onChange = [this](int) { invalidate(); };
        row_.searchable = true, row_.wheelSteps = false;
        row_.setTip(L"Scroll the palette to a group (number or terrain name). Ctrl+click a rect: its tile. Key M: next block of this terrain's groups");
        row_.onChange = [this](int g) { showRow(g, -1); };
        palBar_.onScroll = [this](int pos) { palScroll_ = pos, invalidate(); };
        loadColumns();
        for (draw::View* v : std::initializer_list<draw::View*>{&era_, &menu_, &row_, &refPick_, &refShow_, &refOpacity_, &search_,
                                                               &sideBar_, &newTerrain_, &ground_, &palBar_})
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
        if (int v = 0; prefs::number(L"Layout/IsomTileIds", v, 0, 2)) tileIds_ = v;
        if (doc_.tilesets.empty()) openEra(app.ed && app.ed->isOpen() ? int(app.ed->era()) & 7 : 0);
        else rebuild();
    }
    bool dirty() const { return dirty_; }
    // Another tileset (a snapshot's isomrules:<era>/<name>).
    void showEra(int e) { openEra(e & 7); }
    // Selects the terrain of that name in the list (a snapshot's isomrules:<name>); false: none.
    bool selectByName(const std::string& name) {
        for (size_t i = 0; i < shown_.size(); ++i)
            if (shown_[i] >= 0 ? rules().terrains[size_t(shown_[i])].t.name == name : narrow(stdName(-1 - shown_[i])) == name)
                return selectValue(shown_[i]), true;
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
    void paintSide(draw::Canvas& c);
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || search_.hasFocus()) return false;
        if (e.key == static_cast<draw::Key>('N') && !e.mods) return nextNull(), true;
        if (e.key == draw::Key::Delete && !e.mods) return setNull(), true;
        if (e.key == static_cast<draw::Key>('R') && !e.mods) return toggleReference(), true;
        if ((e.key == draw::Key::Up || e.key == draw::Key::Down) && (e.mods & draw::kCtrl)) return stepOpacity(e.key == draw::Key::Up ? 1 : -1), true;
        if (e.key == static_cast<draw::Key>('T') && !e.mods) return toggleWidth(), true;
        if (e.key == static_cast<draw::Key>('M') && !e.mods) return nextMyRows(), true;
        if (e.key == static_cast<draw::Key>('V') && !e.mods) return toggleDraft(), true;
        if (e.key == static_cast<draw::Key>('F') && !e.mods) return toggleTodo(), true;
        if (e.key == static_cast<draw::Key>('G') && !e.mods) return togglePalMine(), true;
        if ((e.key == draw::Key::Up || e.key == draw::Key::Down) && !e.mods) return stepTarget(e.key == draw::Key::Down ? 1 : -1), true;
        if (e.key == static_cast<draw::Key>('S') && (e.mods & draw::kCtrl)) return save(false), true;
        if (e.key == static_cast<draw::Key>('Z') && (e.mods & draw::kCtrl)) return undo(e.mods & draw::kShift), true;
        if (e.key == static_cast<draw::Key>('Y') && (e.mods & draw::kCtrl)) return undo(true), true;
        return false;
    }
    std::wstring tip(draw::Point p) override;
    draw::Cursor cursor(draw::Point p) override { return split_ >= 0 && std::abs(p.x - split_) < scale(4) ? draw::Cursor::SizeWE : draw::Cursor::Arrow; }

    // (the snapshot's lines: the terrain, its holes and the target)
    std::string state() const {
        const auto* t = terrain();
        if (!t) return "isom rules: no terrain";
        const auto [done, total] = stardraft::terrainrules::completeness(tileset_, t->t);
        int borrowed = 0;
        for (size_t k = 0; k < t->t.pieces.size(); ++k)
            if (k < t->t.setPieces || k + t->t.gapPieces >= t->t.pieces.size())
                borrowed += stardraft::terrainrules::stateOf(t->t.pieces[k]) == isomrules::PieceState::Borrowed ? 1 : 0;
        return "isom rules: " + t->t.name + ", " + std::to_string(nulls(t->t)) + " null, " + std::to_string(holes_.size()) + " holes in the examples, " +
               std::to_string(absent_.size()) + " not in them, " + std::to_string(visibleGroups_.size()) + " rects shown, " + std::to_string(done) + " of " +
               std::to_string(total) + " rects done, " + std::to_string(borrowed) + " borrowed, " + stardraft::terrainrules::familyName(family_) +
               (t->t.draft ? ", draft" : ", verified") + (target_ >= 0 ? ", target piece " + std::to_string(target_) : "");
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
    const stardraft::terrainrules::TilesetRules& rules() const { return const_cast<IsomRulesView*>(this)->rules(); }
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
        undo_.clear(), redo_.clear();
        shown_.clear(), sideRows_.clear(), familyCache_.clear(), sel_ = -1, target_ = -1, places_.clear(), holes_.clear(), absent_.clear(), picture_.reset();
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
            tileset_ = stardraft::Tileset(), pal_.clear(), chips_.clear(), palScroll_ = 0, jump_ = -1, row_.setItems({}), ground_.setItems({}), groundIds_.clear();
            invalidate();
            shellErrorBox(widen(o.err), L"Custom isom terrain editor");
            return;
        }
        tileset_ = std::move(o.tileset);
        if (!rulesOrNull() || o.plain) setRules(std::move(o.rules));
        for (auto& x : rules().terrains) stardraft::terrainrules::normalize(tileset_, x.t);  // (every template rect present, the engine's order)
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
        rowNames_ = of, rowPics_.clear();
        stdPairs_.clear(), std_ = -1;
        std::vector<stardraft::kit::DropdownItem> refs{{L"Template"}, {L"None"}};
        if (const IsomTerrainTable* game = IsomTerrainTable::forEra(uint16_t(era())))
            for (const auto& pr : game->pairs())
                if (game->isTerrain(pr.a) && game->isTerrain(pr.b)) stdPairs_.push_back({pr.a, pr.b}), refs.push_back({stdName(int(stdPairs_.size()) - 1)});
        refPick_.setItems(refs), refPick_.setValue(0);
        pal_.clear(), chips_.clear(), palScroll_ = 0, jump_ = -1;
        sel_ = -1;
        filter();
        if (!shown_.empty()) selectValue(shown_[0]);
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
            if (!q.empty() && !tileset_.groups.empty()) {  // (the label too: "draft", a family, "36/36")
                const auto [done, total] = stardraft::terrainrules::completeness(tileset_, ts.terrains[i].t);
                const auto [a, b] = stardraft::terrainrules::templateOf(tileset_, ts.terrains[i].t);
                n += L" " + isomrules::familyName(stardraft::terrainrules::familyOf(stardraft::terrainrules::templateRects(tileset_, a, b), tileset_)) + L" " +
                     std::to_wstring(done) + L"/" + std::to_wstring(total) + (ts.terrains[i].t.draft ? L" draft" : L" verified");
            }
            std::transform(n.begin(), n.end(), n.begin(), ::towlower);
            if (q.empty() || n.find(q) != std::wstring::npos) shown_.push_back(int(i));
        }
        for (size_t k = 0; k < stdPairs_.size(); ++k) {  // (the game's own: -1 - k)
            std::wstring n = stdName(int(k)) + L" game";
            std::transform(n.begin(), n.end(), n.begin(), ::towlower);
            if (q.empty() || n.find(q) != std::wstring::npos) shown_.push_back(-1 - int(k));
        }
        buildSideRows();
    }
    // The family of a list value (a terrain of the rules, or -1 - k: the game's border k), kept until the rules change.
    isomrules::Family familyOfValue(int v) {
        const uint32_t key = v >= 0 ? uint32_t(rules().terrains[size_t(v)].t.id) : 0x10000u | uint32_t(-1 - v);
        if (const auto it = familyCache_.find(key); it != familyCache_.end()) return it->second;
        isomrules::Family f = isomrules::Family::Other;
        if (!tileset_.groups.empty()) {
            const auto [a, b] = v >= 0 ? stardraft::terrainrules::templateOf(tileset_, rules().terrains[size_t(v)].t) : stdPairs_[size_t(-1 - v)];
            f = stardraft::terrainrules::familyOf(stardraft::terrainrules::templateRects(tileset_, a, b), tileset_);
        }
        return familyCache_[key] = f;
    }
    // The sidebar's rows: the terrains by family (cliffs first), each family a header with its count, then the game's
    // borders; the shown terrain's row in view.
    void buildSideRows() {
        sideRows_.clear();
        static const isomrules::Family kOrder[] = {isomrules::Family::Cliff, isomrules::Family::Wall, isomrules::Family::Blend, isomrules::Family::Pavement,
                                                   isomrules::Family::Patch, isomrules::Family::Other};
        int y = 0;
        auto header = [&](const std::wstring& text, int n) {
            sideRows_.push_back({kSideHeader, text + L"  \x00b7  " + std::to_wstring(n), y, scale(24)}), y += scale(24);
        };
        for (isomrules::Family f : kOrder) {
            std::vector<int> in;
            for (int v : shown_)
                if (v >= 0 && familyOfValue(v) == f) in.push_back(v);
            if (in.empty()) continue;
            header(isomrules::familyName(f), int(in.size()));
            for (int v : in) sideRows_.push_back({v, {}, y, scale(44)}), y += scale(44);
        }
        std::vector<int> games;
        for (int v : shown_)
            if (v < 0) games.push_back(v);
        if (!games.empty()) {
            header(L"game borders", int(games.size()));
            for (int v : games) sideRows_.push_back({v, {}, y, scale(30)}), y += scale(30);
        }
        sideTotal_ = y;
        revealSideRow();
        invalidate();
    }
    void revealSideRow() {
        if (side_.height() <= 0) return;  // (before the first layout: the layout calls again)
        for (const SideRow& r : sideRows_)
            if (r.value != kSideHeader && ((sel_ >= 0 && r.value == sel_) || (std_ >= 0 && r.value == -1 - std_))) {
                if (r.y < sideScroll_ || r.y + r.h > sideScroll_ + side_.height()) sideScroll_ = std::max(0, r.y - side_.height() / 3);
                for (const SideRow& h : sideRows_)  // (its family's header above it, when the row is near the top)
                    if (h.value == kSideHeader && h.y < r.y && h.y + scale(24 * 2) > r.y && h.y < sideScroll_) sideScroll_ = h.y;
                break;
            }
    }
    // The tile that stands for a list value in the sidebar: a mod terrain's first fill tile (else its stand-in's solid
    // tile), a game border's terrain's solid tile. 0: none.
    uint16_t swatchOf(int v) const {
        if (tileset_.groups.empty()) return 0;
        if (v < 0) {
            const int g = stardraft::solidTileGroup(tileset_, stdPairs_[size_t(-1 - v)].second);
            return g >= 0 ? gameTile(uint32_t(g)) : 0;
        }
        const auto& m = rules().terrains[size_t(v)].t;
        for (size_t k = m.setPieces; k + m.gapPieces < m.pieces.size(); ++k)
            if (!isomrules::nullHalf(m.pieces[k][1])) return m.pieces[k][1];
        const int g = stardraft::solidTileGroup(tileset_, m.standIn);
        return g >= 0 ? gameTile(uint32_t(g)) : 0;
    }
    // A standard terrain's name: Primary (Bordering), as the mod's.
    std::wstring stdName(int k) const {
        const IsomTerrainTable* game = IsomTerrainTable::forEra(uint16_t(era()));
        if (!game || k < 0 || k >= int(stdPairs_.size())) return {};
        return widen(game->terrainName(stdPairs_[size_t(k)].second)) + L" (" + widen(game->terrainName(stdPairs_[size_t(k)].first)) + L")";
    }
    // A list value shown: a terrain of the rules (v >= 0), or the game's border k (v = -1 - k).
    void selectValue(int v) {
        sel_ = v >= 0 ? v : -1, std_ = v < 0 && v > -1000000 ? -1 - v : -1;
        revealSideRow();
        target_ = -1;
        updateMyRows();
        updateTemplate();
        if (const auto* t = terrain())
            for (size_t k = 0; k < groundIds_.size(); ++k)
                if (groundIds_[k] == t->t.ground) ground_.setValue(int(k));
        rebuild();
    }
    void setGround(int i) {
        auto* t = sel_ >= 0 ? &rules().terrains[size_t(sel_)] : nullptr;
        if (!t || i < 0 || i >= int(groundIds_.size()) || groundIds_[size_t(i)] == t->t.id) return;
        const uint16_t g = groundIds_[size_t(i)];
        remember();
        const uint16_t tpl = stardraft::terrainrules::templateOf(tileset_, t->t).first;  // (the template stays: its ground)
        t->t.ground = g;
        t->t.groundTemplate = g == tpl ? 0 : tpl;
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
        isomrules::Family family = isomrules::Family::Other;  // (the terrain's template's: its shape)
        std::vector<std::pair<int, int>> extra;  // (more spots of the shape, found for this terrain: moreRects_)
        int extend = 0;                          // (search for more spots that show the rects the shape does not: 1 by itself, 2 More rects)
        std::vector<isomrules::ModTerrain> all;  // (the working rules: the terrain's pieces as the stand-in values)
        std::vector<uint16_t> free;              // (the stand-in values: piece k's halves are 2k and 2k + 1)
        const GameData* data = nullptr;
        bool standard = false;  // (a game border: stdTerrain on root, no pieces)
        uint16_t stdTerrain = 0, refGround = 0, refTerrain = 0;  // (refTerrain 0: no reference)
    };
    struct BuildOut {
        std::vector<Place> places;
        std::vector<int> holes, absent;
        std::vector<std::pair<int, int>> extra;  // (the extra spots, after a search)
        int gained = 0;                          // (the search: rects shown that were not)
        draw::Rect crop;
        std::vector<uint16_t> ids;  // (the crop's tiles, row by row; kNullId: a NULL half)
        std::unique_ptr<draw::Bitmap> picture, reference;
    };
    static constexpr uint16_t kNullId = 0xFFFF;
    void rebuild() {
        const auto* t = terrain();
        std::string err;
        const GameData* data = (t || std_ >= 0) && !loading_ ? cmbwGameData(err) : nullptr;
        auto in = std::make_shared<BuildIn>();
        in->gen = ++buildGen_;
        if ((!t && std_ < 0) || !data) {
            building_ = false, places_.clear(), holes_.clear(), absent_.clear(), picture_.reset(), refPicture_.reset();
            return invalidate();
        }
        in->era = era(), in->data = data;
        if (const int ref = refOf(t); ref >= 0 && refOn_) in->refGround = stdPairs_[size_t(ref)].first, in->refTerrain = stdPairs_[size_t(ref)].second;
        if (!t) {  // (a standard terrain: the game's border, read only)
            in->standard = true, in->root = stdPairs_[size_t(std_)].first, in->stdTerrain = stdPairs_[size_t(std_)].second;
            return start(in, -2 - std_);
        }
        in->t = *t, in->family = family_;
        if (const auto it = moreRects_.find(t->t.id); it != moreRects_.end()) in->extra = it->second;
        in->extend = extendNext_, extendNext_ = 0;
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
        start(in, sel_);
    }
    void start(std::shared_ptr<BuildIn> in, int shown) {
        if (shownId_ != shown)
            places_.clear(), holes_.clear(), absent_.clear(), picture_.reset(), refPicture_.reset(), visibleGroups_.clear(), shownId_ = shown;
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
                ids_ = std::move(out->ids);
                visibleGroups_.clear();
                if (const auto* t = terrain())
                    for (const Place& p : places_)
                        if (p.piece >= 0 && p.piece < int(t->t.pieces.size())) visibleGroups_.insert(t->t.pieces[size_t(p.piece)][0]);
                picture_ = std::move(out->picture), refPicture_ = std::move(out->reference);
                if (in->extend) {
                    moreRects_[in->t.t.id] = out->extra;
                    if (out->gained || in->extend == 2)
                        setMessage(out->gained ? L"Extend example: +" + std::to_wstring(out->gained) + L" rects (" + std::to_wstring(out->extra.size()) + L" extra positions)"
                                               : L"Extend example: no position exposes more rects (" + std::to_wstring(out->extra.size()) + L" extra positions)");
                } else if (!moreRects_.count(in->t.t.id)) {  // (a terrain's first example: the search follows it by itself)
                    if (absent_.empty()) moreRects_[in->t.t.id] = {};
                    else extendNext_ = 1, rebuild();
                }
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
        const int cx = W * 16, cy = H * 16;
        // (a game terrain on its ground in a shape; the map's tiles of a part)
        auto paintGame = [&](uint16_t ground, uint16_t terrain, const std::vector<std::pair<int, int>>& shape) {
            ed->paintBrush(cx, cy, ground, Brush{Brush::Diamond, 64.0f});
            for (const auto& [i, j] : shape) ed->paintBrush(cx + i * 64, cy + j * 32, terrain, Brush{Brush::Diamond, 1.0f});
        };
        auto pictureOf = [&](const draw::Rect& crop) {
            const stardraft::TerrainLayer l = stardraft::readTerrain(ed->chk(), "TILE");
            std::vector<uint16_t> part;
            for (int y = crop.top; y < crop.bottom; ++y)
                for (int x = crop.left; x < crop.right; ++x) part.push_back(l.tiles[size_t(y) * W + x]);
            return isomrules::tilesBitmap(ed->tileset(), part, crop.width(), crop.height());
        };
        auto addReference = [&](const std::vector<std::pair<int, int>>& shape) {
            if (!in.refTerrain || !out.picture) return;
            paintGame(in.refGround, in.refTerrain, shape);
            out.reference = pictureOf(out.crop);
            if (out.reference && (out.reference->width() != out.picture->width() || out.reference->height() != out.picture->height())) out.reference.reset();
        };
        if (in.standard) {  // (the game's border: the rects whose ISOM differs from its ground alone)
            ed->useModTerrains({});
            ed->paintBrush(cx, cy, root, Brush{Brush::Diamond, 64.0f});
            const std::vector<uint8_t> before = ed->chk().find("ISOM") ? ed->chk().find("ISOM")->data : std::vector<uint8_t>();
            paintGame(root, in.stdTerrain, shapeOf(isomrules::Family::Other));
            const std::vector<uint8_t> after = ed->chk().find("ISOM") ? ed->chk().find("ISOM")->data : std::vector<uint8_t>();
            int x0 = W, y0 = H, x1 = -1, y1 = -1;
            const int cols = W / 2 + 1;  // (ISOM: a rect each 2 tiles across and 1 down, 4 values of 2 bytes each)
            for (size_t r = 0; r * 8 + 8 <= std::min(before.size(), after.size()); ++r)
                if (!std::equal(before.begin() + long(r * 8), before.begin() + long(r * 8 + 8), after.begin() + long(r * 8))) {
                    const int x = int(r % size_t(cols)) * 2, y = int(r / size_t(cols));
                    if (y < H) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, std::min(W - 1, x + 1)), y1 = std::max(y1, y);
                }
            if (x1 < 0) x0 = 0, y0 = 0, x1 = W - 1, y1 = H - 1;
            x0 = std::max(0, x0 - 2), y0 = std::max(0, y0 - 1), x1 = std::min(W - 1, x1 + 2), y1 = std::min(H - 1, y1 + 1);
            out.crop = {x0, y0, x1 + 1, y1 + 1};
            const stardraft::TerrainLayer l = stardraft::readTerrain(ed->chk(), "TILE");
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) out.ids.push_back(l.tiles[size_t(y) * W + x]);
            out.picture = pictureOf(out.crop);
            addReference(shapeOf(isomrules::Family::Other));
            return;
        }
        std::unordered_map<uint16_t, uint32_t> index;  // (a stand-in value -> piece * 2 + half)
        for (uint32_t i = 0; i < in.free.size(); ++i) index[in.free[i]] = i;
        ed->useModTerrains(in.all);
        // (one shape for each kind: diamonds of 1, in steps of 64 to the side and 32 up or down from the middle; the
        // places that show the most rects of the mod's walls and paths: a search over all their terrains found the same
        // few each time)
        std::vector<std::pair<int, int>> shape = shapeOf(in.family);
        shape.insert(shape.end(), in.extra.begin(), in.extra.end());
        auto paint = [&]() {  // (the bordering terrain all around: no other terrain in the picture; the shape)
            ed->paintBrush(cx, cy, root, Brush{Brush::Diamond, 64.0f});
            ed->paintBrush(cx, cy, t->t.ground ? t->t.ground : 1, Brush{Brush::Diamond, 24.0f});
            for (const auto& [i, j] : shape) ed->paintBrush(cx + i * 64, cy + j * 32, t->t.id, Brush{Brush::Diamond, 1.0f});
        };
        // (the rects of the terrain (its set's and its gaps', by game group) that the painted map shows)
        auto shownGroups = [&]() {
            const stardraft::TerrainLayer l = stardraft::readTerrain(ed->chk(), "TILE");
            std::set<uint16_t> g;
            for (uint16_t v : l.tiles)
                if (const auto it = index.find(v); it != index.end() && !(it->second & 1)) g.insert(t->t.pieces[it->second / 2][0]);
            return g;
        };
        paint();
        if (in.extend) {  // (more spots, one at a time: each must show a rect that none showed, and lose none)
            std::set<uint16_t> want;
            for (size_t k = 0; k < t->t.pieces.size(); ++k)
                if (k < t->t.setPieces || k + t->t.gapPieces >= t->t.pieces.size()) want.insert(t->t.pieces[k][0]);
            std::set<uint16_t> have = shownGroups();
            const size_t before = have.size();
            for (int round = 0; round < 8 && in.gen == buildGen_; ++round) {
                std::pair<int, int> best{0, 0};
                std::set<uint16_t> bestHave;
                size_t bestGain = 0;
                for (int j = -6; j <= 6; ++j)
                    for (int i = -4; i <= 4; ++i) {
                        if (std::find(shape.begin(), shape.end(), std::make_pair(i, j)) != shape.end()) continue;
                        shape.push_back({i, j});
                        paint();
                        std::set<uint16_t> now = shownGroups();
                        shape.pop_back();
                        if (!std::includes(now.begin(), now.end(), have.begin(), have.end())) continue;
                        size_t gain = 0;
                        for (uint16_t g : now)
                            if (want.count(g) && !have.count(g)) ++gain;
                        if (gain > bestGain) bestGain = gain, best = {i, j}, bestHave = std::move(now);
                    }
                if (!bestGain) break;
                shape.push_back(best), have = std::move(bestHave);
            }
            out.gained = int(have.size() - before);
            out.extra.assign(shape.begin() + long(shapeOf(in.family).size()), shape.end());
            paint();
        }
        stardraft::TerrainLayer tl = stardraft::readTerrain(ed->chk(), "TILE");
        int x0 = W, y0 = H, x1 = -1, y1 = -1;
        std::set<uint16_t> seen;
        std::vector<bool> nullAt(tl.tiles.size());
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
                nullAt[size_t(ty) * W + tx] = isomrules::nullHalf(q[right ? 2 : 1]);
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
            for (int x = x0; x <= x1; ++x) {
                part.push_back(tl.tiles[size_t(y) * W + x]);
                out.ids.push_back(nullAt[size_t(y) * W + x] ? kNullId : part.back());
            }
        out.picture = isomrules::tilesBitmap(ed->tileset(), part, x1 - x0 + 1, y1 - y0 + 1);
        addReference(shape);
    }
    // A family's shape: diamonds of 1, in steps of 64 to the side and 32 up or down from the middle (the places that show
    // the most rects of walls and of blends: a search over all the mod's terrains found the same few each time; More
    // rects adds to them); the game's borders (Other): both.
    static const std::vector<std::pair<int, int>>& shapeOf(isomrules::Family f) {
        static const std::vector<std::pair<int, int>> kWall = {{0, 0}, {0, -2}, {-1, -3}, {-1, 3}, {3, -3}};
        static const std::vector<std::pair<int, int>> kPath = {{0, 0}, {0, -2}, {-1, 1}, {0, 2}, {3, 1}, {3, -3}, {-3, -3}};
        static const std::vector<std::pair<int, int>> kBoth = {{0, 0}, {0, -2}, {-1, -3}, {-1, 3}, {3, -3}, {-1, 1}, {0, 2}, {3, 1}, {-3, -3}};
        return f == isomrules::Family::Wall || f == isomrules::Family::Cliff ? kWall : f == isomrules::Family::Other ? kBoth : kPath;
    }
    // The reference over the example, on or off (R); its opacity is the slider (Ctrl+Up / Ctrl+Down: 10 % steps; 100 %:
    // the reference alone).
    void toggleReference() {
        refOn_ = !refOn_;
        refShow_.setTone(refOn_ ? stardraft::kit::Tone::Primary : stardraft::kit::Tone::Normal);
        if (refOn_ && refOf(terrain()) < 0) setMessage(L"No reference border: select one in the Reference list");
        if (refOn_ && !refPicture_) return rebuild();  // (it paints only while it shows)
        invalidate();
    }
    void stepOpacity(int dir) {
        refOpacity_.setValue(refOpacity_.value() + 10 * dir);
        if (!refOn_) toggleReference();
        setMessage(L"Reference " + std::to_wstring(refOpacity_.value()) + L" %");
        invalidate();
    }
    // The reference border (an index of stdPairs_; -1: none): the one picked, or (Auto) the terrain's template: its
    // stand-in on the game terrain its ground stands for.
    int refOf(const stardraft::terrainrules::Terrain* t) const {
        const int c = refPick_.value();
        if (c >= 2 && c - 2 < int(stdPairs_.size())) return c - 2;
        if (c != 0 || !t) return -1;
        const uint16_t ground = t->t.groundTemplate ? t->t.groundTemplate : t->t.ground, terrain = t->t.standIn;
        for (size_t k = 0; k < stdPairs_.size(); ++k)
            if (stdPairs_[k].first == ground && stdPairs_[k].second == terrain) return int(k);
        for (size_t k = 0; k < stdPairs_.size(); ++k)
            if (stdPairs_[k].first == terrain && stdPairs_[k].second == ground) return int(k);
        return -1;
    }
    // The target (the rect, or the half: 1 tile) has no tile: NULL (a place that needs work; it stays where it is: the
    // engine reads no NULL half as a game tile).
    void setNull() {
        if (loading_) return;
        if (sel_ < 0 || target_ < 0) return setMessage(std_ >= 0 ? L"Game borders are read-only" : L"Select a rect first");
        auto& m = rules().terrains[size_t(sel_)].t;
        if (target_ >= int(m.pieces.size())) return;
        const size_t k = size_t(target_);
        if (k >= m.setPieces && k < m.pieces.size() - m.gapPieces) return setMessage(L"Fill pieces cannot be NULL");
        remember();
        auto p = m.pieces[k];
        if (wide_) p[1] = 0, p[2] = 1;
        else p[size_t(1 + targetHalf_)] = uint16_t(targetHalf_);  // (the null tile's subtiles: 0 left, 1 right)
        m.pieces[k] = p;
        changed();
        setMessage(L"Rect " + std::to_wstring(p[0]) + L": " + (wide_ ? std::wstring(L"NULL") : std::wstring(targetHalf_ ? L"right half NULL" : L"left half NULL")));
    }
    // The tiles' row g in view, and tile v framed (-1: the whole row) until another is asked for.
    void showRow(int g, int v) {
        if (g < 0 || g >= int(tileset_.groups.size())) return;
        jump_ = v >= 0 ? v : g << 4, jumpRow_ = v < 0;
        row_.setValue(g);
        scrollToRow(g);
        invalidate();
    }
    // Row g of the tiles in view (as it is when it shows already).
    void scrollToRow(int g) {
        const int i = palIndexOf(g);
        if (i < 0 || i >= palRowCount()) return;
        const int cell = palCell(), y = i * cell;
        if (y < palScroll_ || y + cell > palScroll_ + palette_.height()) palScroll_ = std::clamp(y - palette_.height() / 2, 0, palScrollMax());
    }

    // A tile of the palette went into the target (Rect: the pair; Tile: the half that was clicked).
    void place(uint16_t value) {
        if (loading_) return;
        if (sel_ < 0 || target_ < 0) return setMessage(std_ >= 0 ? L"Game borders are read-only" : L"Select a rect first");
        auto& m = rules().terrains[size_t(sel_)].t;
        if (target_ >= int(m.pieces.size())) return;
        auto p = m.pieces[size_t(target_)];
        if (wide_) {  // (the tile and the next one in its row: the two cells framed under the mouse)
            if ((value & 15) == 15 || tileset_.megatileFor(uint16_t(value + 1)) <= 0) return setMessage(L"Pair: subtile k+1 is empty in this row; use Single");
            remember();
            p[1] = value, p[2] = uint16_t(value + 1);
        } else {
            remember();
            p[size_t(1 + targetHalf_)] = value;  // (the half only: the other half's tile is the user's choice too)
        }
        m.pieces[size_t(target_)] = p;
        if (!wide_ && isomrules::nullHalf(p[size_t(2 - targetHalf_)])) targetHalf_ = 1 - targetHalf_;  // (next: the other half, still NULL)
        changed();  // (a complete piece moves into the set: the target follows it)
        auto half = [](uint16_t v) { return isomrules::nullHalf(v) ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15); };
        std::wstring msg = L"Rect " + std::to_wstring(p[0]) + L": " + half(p[1]) + L" " + half(p[2]);
        if (!isomrules::anyNull(p)) {  // (the rect is done: the next one with no tile is the target)
            const int next = findNull(target_);
            if (next >= 0) setTarget(next, -1), msg += L"  \x2192 next " + std::to_wstring(m.pieces[size_t(next)][0]);
            else msg += L"  \x2192 no NULL rects left";
        }
        setMessage(msg);
    }
    // The next piece after `from` (-1: the start) of the set or the gaps with no tile; -1: none. The order: the rects
    // the example shows (their context is in view), then the tray's, each by piece; round.
    int findNull(int from) const {
        const auto* t = terrain();
        if (!t) return -1;
        const auto& m = t->t;
        auto shown = [&](int i) { return visibleGroups_.count(m.pieces[size_t(i)][0]) > 0; };
        std::vector<int> nulls;
        for (int i = 0; i < int(m.pieces.size()); ++i)
            if ((size_t(i) < m.setPieces || size_t(i) + m.gapPieces >= m.pieces.size()) && isomrules::anyNull(m.pieces[size_t(i)])) nulls.push_back(i);
        if (nulls.empty()) return -1;
        std::vector<int> seen, unseen;  // (the example's first, while it has any; then the tray's)
        for (int i : nulls) (shown(i) ? seen : unseen).push_back(i);
        const std::vector<int>& v = seen.empty() ? unseen : seen;
        const auto it = std::upper_bound(v.begin(), v.end(), from);  // (the first after `from`, round)
        return it == v.end() ? v.front() : *it;
    }
    // Piece k is the target (half -1: its first NULL half, else the left), and the tiles show its row.
    void setTarget(int k, int half) {
        const auto* t = terrain();
        if (!t || k < 0 || k >= int(t->t.pieces.size())) return;
        const auto& q = t->t.pieces[size_t(k)];
        target_ = k, targetHalf_ = half >= 0 ? half : isomrules::nullHalf(q[1]) ? 0 : 1;
        uint16_t v = q[size_t(1 + targetHalf_)];  // (its tile's row in view, or the other half's: the tiles near it)
        if (isomrules::nullHalf(v)) v = q[size_t(2 - targetHalf_)];
        if (!isomrules::nullHalf(v)) scrollToRow(v >> 4);
        const std::vector<CheckRow> rows = checkRows();  // (the checklist shows the target's row)
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].group == q[0]) {
                const int y = int(i) * scale(kCheckRow), page = check_.height() - scale(kCheckHead);
                if (y < checkScroll_ || y + scale(kCheckRow) > checkScroll_ + page) checkScroll_ = std::max(0, y - page / 2);
                break;
            }
        invalidate();
    }
    // The tiles go to the next block of the terrain's own rows (marked at the left), round.
    void nextMyRows() {
        if (myRows_.empty()) return setMessage(L"No groups assigned to this terrain yet");
        const int cell = palCell(), top = palGroupAt(palScroll_ / std::max(1, cell));
        std::vector<int> starts;  // (each block's first row)
        for (int g : myRows_)
            if (starts.empty() || !myRows_.count(g - 1)) starts.push_back(g);
        int go = starts.front();
        for (int g : starts)
            if (g > top + 1) { go = g; break; }
        palScroll_ = std::clamp(std::max(0, palIndexOf(go)) * cell - scale(4), 0, palScrollMax());
        row_.setValue(go);
        setMessage(L"Groups " + std::to_wstring(go) + L"+: this terrain");
        invalidate();
    }
    // More rects: the next example searches for more places that show the rects it does not (on the worker).
    void moreRects() {
        if (loading_) return setMessage(L"Tileset loading");
        if (sel_ < 0) return setMessage(std_ >= 0 ? L"Game borders show every rect" : L"Select a terrain first");
        extendNext_ = 2;
        setMessage(L"Extend example: searching\x2026");
        rebuild();
    }
    // The target walks the checklist: the next (or the previous) rect of the template, round.
    void stepTarget(int dir) {
        const auto* t = terrain();
        if (!t) return;
        const std::vector<CheckRow> rows = checkRows();
        if (rows.empty()) return;
        int at = -1;
        if (target_ >= 0 && target_ < int(t->t.pieces.size()))
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].group == t->t.pieces[size_t(target_)][0]) at = int(i);
        for (int step = 0; step < int(rows.size()); ++step) {  // (a row with no piece: the next)
            at = at < 0 ? (dir > 0 ? 0 : int(rows.size()) - 1) : ((at + dir) % int(rows.size()) + int(rows.size())) % int(rows.size());
            if (rows[size_t(at)].piece >= 0) break;
        }
        if (rows[size_t(at)].piece < 0) return;
        setTarget(rows[size_t(at)].piece, -1);
        setMessage(rows[size_t(at)].role + L"  (rect " + std::to_wstring(rows[size_t(at)].group) + L")");
    }
    // The Menu button: the file, the terrain and the rect actions (the keys are in the button's tip).
    void showMenu() {
        const auto* t = terrain();
        const std::vector<std::wstring> items = {L"Load\x2026",
                                                 L"Load default",
                                                 L"Save",
                                                 L"Save as\x2026",
                                                 L"New terrain\x2026",
                                                 L"Next NULL rect",
                                                 wide_ ? L"Pick single half" : L"Pick pair",
                                                 L"Set target to NULL",
                                                 L"Clear rects",
                                                 L"Copy tiles\x2026",
                                                 L"Extend example",
                                                 t && t->t.draft ? L"Mark verified" : L"Mark draft",
                                                 checkTodo_ ? L"Checklist: every rect" : L"Checklist: to do only",
                                                 palMine_ ? L"Palette: all groups" : L"Palette: this terrain's groups",
                                                 tileIds_ == 0 ? L"Tile IDs: on hover" : tileIds_ == 1 ? L"Tile IDs: always" : L"Tile IDs: off"};
        std::vector<std::wstring> all = items;
        std::vector<int> hidden;
        for (int k = 0; k < 4; ++k)
            if (!colShown_[k]) hidden.push_back(k), all.push_back(std::wstring(L"Show ") + colName(k));
        const int pick = shellContextMenuAt(menu_, {0, menu_.frame().bottom - menu_.frame().top}, all);  // (under the button)
        if (pick >= 16 && pick - 16 < int(hidden.size())) {
            setColumnShown(hidden[size_t(pick - 16)], true), saveColumns(), layout(), invalidate();
            return;
        }
        switch (pick) {
            case 1: return loadFile();
            case 2: return loadDefault();
            case 3: return void(save(false));
            case 4: return void(save(true));
            case 5: return newTerrain();
            case 6: return nextNull();
            case 7: return toggleWidth();
            case 8: return setNull();
            case 9: return clearTerrain();
            case 10: return copyFrom();
            case 11: return moreRects();
            case 12: return toggleDraft();
            case 13: return toggleTodo();
            case 14: return togglePalMine();
            case 15:
                tileIds_ = (tileIds_ + 1) % 3, prefs::setNumber(L"Layout/IsomTileIds", tileIds_), invalidate();
                return setMessage(tileIds_ == 0 ? L"Tile IDs: off" : tileIds_ == 1 ? L"Tile IDs: on hover" : L"Tile IDs: always");
            default: return;
        }
    }
    void toggleWidth() {
        wide_ = !wide_, invalidate();
        setMessage(wide_ ? L"Pick: pair (both halves)" : L"Pick: single (the selected half)");
    }
    // Before an edit: the terrain as it is (Ctrl+Z brings it back).
    void remember() {
        const auto* t = terrain();
        if (!t) return;
        undo_.push_back({t->t.id, *t, target_, targetHalf_}), redo_.clear();
        if (undo_.size() > 200) undo_.erase(undo_.begin());
    }
    void undo(bool redo) {
        auto& from = redo ? redo_ : undo_;
        auto& to = redo ? undo_ : redo_;
        if (loading_) return;
        if (from.empty()) return setMessage(redo ? L"Nothing to redo" : L"Nothing to undo");
        auto& ts = rules();
        int i = -1;
        for (size_t k = 0; k < ts.terrains.size(); ++k)
            if (ts.terrains[k].t.id == from.back().id) i = int(k);
        if (i < 0) return setMessage(L"Terrain no longer exists");  // (the entry stays)
        Edit e = std::move(from.back());
        from.pop_back();
        const bool same = sel_ == i && std_ < 0;
        to.push_back({e.id, ts.terrains[size_t(i)], same ? target_ : -1, same ? targetHalf_ : 0});
        ts.terrains[size_t(i)] = std::move(e.before);
        if (!same) sel_ = i, std_ = -1, revealSideRow();
        for (size_t k = 0; k < groundIds_.size(); ++k)  // (the Bordering box: the ground as it was)
            if (groundIds_[k] == ts.terrains[size_t(i)].t.ground) ground_.setValue(int(k));
        places_.clear(), holes_.clear(), absent_.clear();  // (the piece numbers may have moved: none until the new example)
        target_ = e.target, targetHalf_ = e.half;
        changed();
        setMessage((redo ? L"Redo: " : L"Undo: ") + widen(ts.terrains[size_t(i)].t.name) + L" (" + std::to_wstring(nulls(ts.terrains[size_t(i)].t)) + L" NULL)");
    }
    // A fresh start for the terrain: each rect of its set and its gaps once, NULL (as a new terrain's); its fill stays.
    void clearTerrain() {
        if (loading_) return;
        if (sel_ < 0) return setMessage(std_ >= 0 ? L"Standard terrains are read only" : L"Select a terrain first");
        auto& m = rules().terrains[size_t(sel_)].t;
        remember();
        std::vector<std::array<uint16_t, 3>> fill(m.pieces.begin() + long(m.setPieces), m.pieces.end() - long(m.gapPieces)), gaps;
        std::set<uint16_t> games;
        for (size_t k = 0; k < m.pieces.size(); ++k)
            if ((k < m.setPieces || k + m.gapPieces >= m.pieces.size()) && games.insert(m.pieces[k][0]).second) gaps.push_back({m.pieces[k][0], 0, 1});
        m.pieces = std::move(fill);
        m.setPieces = 0, m.gapPieces = gaps.size();
        m.pieces.insert(m.pieces.end(), gaps.begin(), gaps.end());
        places_.clear(), holes_.clear(), absent_.clear();
        target_ = -1, targetHalf_ = 0;
        changed();
        if (const int first = findNull(-1); first >= 0) setTarget(first, -1);
        setMessage(std::to_wstring(gaps.size()) + L" rects set to NULL. Ctrl+Z undoes");
    }
    // Another terrain's tiles into this one's rects, by role: a terrain of the same family has the same roles, whatever
    // its template; its own pieces go in (not borrowed ones, not holes).
    void copyFrom() {
        if (loading_) return;
        if (sel_ < 0) return setMessage(std_ >= 0 ? L"Standard terrains are read only" : L"Select a terrain first");
        auto& ts = rules();
        std::vector<ListPickItem> items;
        for (size_t i = 0; i < ts.terrains.size(); ++i) {
            if (int(i) == sel_ || ts.terrains[i].t.variantOf) continue;
            const auto [a, b] = stardraft::terrainrules::templateOf(tileset_, ts.terrains[i].t);
            const auto rects = stardraft::terrainrules::templateRects(tileset_, a, b);
            const auto [done, total] = stardraft::terrainrules::completeness(tileset_, ts.terrains[i].t);
            const auto f = stardraft::terrainrules::familyOf(rects, tileset_);
            items.push_back({widen(ts.terrains[i].t.name) + L" \x00b7 " + isomrules::familyName(f) + L" \x00b7 " + std::to_wstring(done) + L"/" + std::to_wstring(total) +
                                 (f == family_ ? L"" : L"  (other family)"),
                             L"", std::to_string(i), "", -1});
        }
        if (items.empty()) return setMessage(L"No other terrain in this tileset");
        const draw::Rect at = menu_.frame();
        ListPickView pick(L"Copy tiles from", items, std::string());
        if (!runListPick(pick, {at.left, at.top + scale(30)}) || pick.result().empty()) return;
        const auto& src = ts.terrains[size_t(std::stoi(pick.result()))];
        const auto [a, b] = stardraft::terrainrules::templateOf(tileset_, src.t);
        const auto srcRects = stardraft::terrainrules::templateRects(tileset_, a, b);
        // (a role repeated in a template is numbered "edge N 1", "edge N 2": the source's n-th of that base role, else its first)
        auto base = [](const std::string& role) {
            size_t i = role.size();
            while (i > 0 && std::isdigit(static_cast<unsigned char>(role[i - 1]))) --i;
            return i > 0 && i < role.size() && role[i - 1] == ' ' ? role.substr(0, i - 1) : role;
        };
        auto nth = [](const std::string& role) {
            size_t i = role.size();
            while (i > 0 && std::isdigit(static_cast<unsigned char>(role[i - 1]))) --i;
            return i > 0 && i < role.size() && role[i - 1] == ' ' ? std::stoi(role.substr(i)) : 1;
        };
        std::map<std::string, std::vector<uint16_t>> roleGroups;  // (the source's base role -> its game groups, in order)
        for (const auto& r : srcRects) roleGroups[base(r.role)].push_back(r.group);
        std::map<uint16_t, std::array<uint16_t, 3>> srcPiece;  // (the source's game group -> its first own piece)
        for (const auto& p : src.t.pieces)
            if (stardraft::terrainrules::stateOf(p) == isomrules::PieceState::Own && !srcPiece.count(p[0])) srcPiece[p[0]] = p;
        auto& m = ts.terrains[size_t(sel_)].t;
        int copied = 0, noRole = 0, noTile = 0;
        for (const auto& r : tmpl_) {
            const auto it = roleGroups.find(base(r.role));
            if (it == roleGroups.end()) { ++noRole; continue; }
            const size_t n = size_t(std::max(1, nth(r.role)) - 1);
            const auto sp = srcPiece.find(it->second[std::min(n, it->second.size() - 1)]);
            if (sp == srcPiece.end()) { ++noTile; continue; }
            bool put = false;
            for (auto& p : m.pieces)
                if (p[0] == r.group && !put) {
                    if (!copied) remember();
                    p[1] = sp->second[1], p[2] = sp->second[2], put = true, ++copied;
                }
        }
        if (!copied) return setMessage(widen(src.t.name) + L": no mod tiles for these roles");
        target_ = -1;
        changed();
        setMessage(L"Copied " + std::to_wstring(copied) + L" rects from " + widen(src.t.name) + (noRole ? L"; " + std::to_wstring(noRole) + L" roles not in source" : L"") +
                   (noTile ? L"; " + std::to_wstring(noTile) + L" without mod tiles" : L"") + L". Ctrl+Z undoes");
    }
    void toggleDraft() {
        if (loading_ || sel_ < 0) return setMessage(std_ >= 0 ? L"Game borders are read-only" : L"Select a terrain first");
        auto& t = rules().terrains[size_t(sel_)];
        remember();
        t.t.draft = !t.t.draft;
        dirty_ = true;
        familyCache_.clear(), invalidate();
        setMessage(widen(t.t.name) + (t.t.draft ? L": draft" : L": verified"));
    }
    void changed() {
        dirty_ = true;
        if (auto* t = sel_ >= 0 && sel_ < int(rules().terrains.size()) ? &rules().terrains[size_t(sel_)] : nullptr) {
            // (the engine's order: complete pieces first; the target follows its piece)
            const std::array<uint16_t, 3> was = target_ >= 0 && target_ < int(t->t.pieces.size()) ? t->t.pieces[size_t(target_)] : std::array<uint16_t, 3>{0xFFFF, 0, 0};
            const std::vector<std::array<uint16_t, 3>> before = t->t.pieces;
            stardraft::terrainrules::normalize(tileset_, t->t);
            if (was[0] != 0xFFFF) {
                target_ = -1;
                for (size_t k = 0; k < t->t.pieces.size(); ++k)
                    if (t->t.pieces[k] == was) { target_ = int(k); break; }
            }
            if (t->t.pieces != before) places_.clear(), holes_.clear(), absent_.clear();  // (the piece numbers moved: none until the new example)
            updateTemplate();
        }
        familyCache_.clear();
        updateMyRows();
        rebuild();
    }
    // The rows of the tiles that are the shown terrain's: its pieces' tiles' rows, and the groups its maps use with it.
    void updateMyRows() {
        myRows_.clear();
        const auto* t = terrain();
        if (!t) return;
        for (size_t k = 0; k < t->t.pieces.size(); ++k)
            for (int h = 1; h <= 2; ++h)
                if (!isomrules::nullHalf(t->t.pieces[k][size_t(h)])) myRows_.insert(t->t.pieces[k][size_t(h)] >> 4);
        for (const auto& [g, tr, other] : tileset_.groupTerrains)
            if (tr == t->t.id || other == t->t.id) myRows_.insert(int(g));
        palRows_.assign(myRows_.begin(), myRows_.end());
        if (palMine_) palScroll_ = std::min(palScroll_, palScrollMax());
    }
    void nextNull() {
        if (!terrain()) return;
        const int next = findNull(target_);
        if (next < 0) return setMessage(L"No NULL rects");
        setTarget(next, -1);
        setMessage(L"Rect " + std::to_wstring(terrain()->t.pieces[size_t(next)][0]) + (visibleGroups_.count(terrain()->t.pieces[size_t(next)][0]) ? L"" : L" (not in the example)"));
    }

    // New terrain: its template (a game border, by family: its rects and roles; or a mod terrain: its template and its
    // tiles, a copy to change), its bordering terrain, how it starts (the game's own tiles in every rect, or empty: NULL),
    // then its name ("Primary (Bordering)").
    void newTerrain() {
        if (loading_) return setMessage(L"Tileset loading");
        auto& ts = rules();
        struct Std {
            int k;
            isomrules::Family f;
            size_t rects;
        };
        std::vector<Std> stds;
        for (size_t k = 0; k < stdPairs_.size(); ++k) {
            const auto rects = stardraft::terrainrules::templateRects(tileset_, stdPairs_[k].first, stdPairs_[k].second);
            stds.push_back({int(k), stardraft::terrainrules::familyOf(rects, tileset_), rects.size()});
        }
        std::stable_sort(stds.begin(), stds.end(), [](const Std& a, const Std& b) { return int(a.f) > int(b.f); });  // (cliffs first, patches last)
        std::vector<ListPickItem> models;
        for (const Std& d : stds)
            models.push_back({isomrules::familyName(d.f) + L"  \x00b7  " + stdName(d.k) + L"  \x00b7  " + std::to_wstring(d.rects) + L" rects", L"", "s" + std::to_string(d.k), "", -1});
        for (size_t i = 0; i < ts.terrains.size(); ++i)
            if (!ts.terrains[i].t.variantOf) {
                const auto [done, total] = stardraft::terrainrules::completeness(tileset_, ts.terrains[i].t);
                models.push_back({L"copy  \x00b7  " + widen(ts.terrains[i].t.name) + L"  \x00b7  " + std::to_wstring(done) + L"/" + std::to_wstring(total), L"", std::to_string(i), "", -1});
            }
        if (models.empty()) return setMessage(L"No templates in this tileset");
        const draw::Rect at = newTerrain_.frame();
        int preset = -1;
        if (std_ >= 0) preset = std_;
        else if (sel_ >= 0)
            for (size_t k = 0; k < stdPairs_.size(); ++k)
                if (stdPairs_[k] == tmplPair_) preset = int(k);
        ListPickView modelPick(L"Template", models, preset >= 0 ? "s" + std::to_string(preset) : std::string());
        if (!runListPick(modelPick, {at.left, at.top - scale(400)}) || modelPick.result().empty()) return;
        const bool fromStd = modelPick.result()[0] == 's';
        const int modelIx = std::stoi(fromStd ? modelPick.result().substr(1) : modelPick.result());
        const uint16_t tplGround = fromStd ? stdPairs_[size_t(modelIx)].first : stardraft::terrainrules::templateOf(tileset_, ts.terrains[size_t(modelIx)].t).first;
        const uint16_t tplTerrain = fromStd ? stdPairs_[size_t(modelIx)].second : ts.terrains[size_t(modelIx)].t.standIn;
        std::vector<ListPickItem> grounds;
        for (size_t k = 0; k < groundIds_.size(); ++k) grounds.push_back({ground_.items()[k].label, L"", std::to_string(groundIds_[k]), "", -1});
        ListPickView groundPick(L"Bordering", grounds, std::to_string(fromStd ? tplGround : ts.terrains[size_t(modelIx)].t.ground));
        if (!runListPick(groundPick, {at.left, at.top - scale(400)}) || groundPick.result().empty()) return;
        const uint16_t ground = uint16_t(std::stoi(groundPick.result()));
        std::wstring groundName;
        for (const auto& g : grounds)
            if (g.value == groundPick.result()) groundName = g.label;
        std::wstring name = L"Primary (" + groundName + L")";
        if (!shellPromptText(L"New terrain name (Primary (Bordering))", name) || name.empty()) return;
        std::set<uint16_t> ids;
        for (const auto& o : ts.terrains) ids.insert(o.t.id);
        for (const auto& m : tileset_.modTerrains) ids.insert(m.id);  // (and the variants' ids)
        uint16_t id = stardraft::Tileset::kModTerrainIds;
        while (ids.count(id) && id < stardraft::Tileset::kModVariantIds) ++id;
        if (id >= stardraft::Tileset::kModVariantIds) return shellErrorBox(L"No free terrain id in this tileset.", L"New terrain");
        stardraft::terrainrules::Terrain x;
        x.t.id = id, x.t.name = narrow(name), x.t.standIn = tplTerrain, x.t.ground = ground, x.t.draft = false;
        const isomrules::ModTerrain* gm = tileset_.modTerrain(ground);
        (void)gm;
        x.t.groundTemplate = ground == tplGround ? 0 : tplGround;  // (another bordering terrain: the template's ground stays)
        if (fromStd) {
            x.t.fill = false, x.t.fillKey = 0;
            for (const auto& r : stardraft::terrainrules::templateRects(tileset_, tplGround, tplTerrain)) x.t.pieces.push_back({r.group, 0, 1});  // (every rect NULL: each tile is a decision)
        } else {
            const auto& src = ts.terrains[size_t(modelIx)].t;
            x.t.fill = src.fill, x.t.fillKey = src.fillKey, x.t.pieces = src.pieces, x.t.setPieces = src.setPieces, x.t.gapPieces = src.gapPieces;
        }
        stardraft::terrainrules::normalize(tileset_, x.t);
        ts.terrains.push_back(std::move(x));
        familyCache_.clear();
        filter();
        selectValue(int(ts.terrains.size()) - 1);
        dirty_ = true;
        setMessage(L"New terrain " + name + L": " + (fromStd ? L"template " + stdName(modelIx) + L", every rect NULL" : L"copy of " + widen(ts.terrains[size_t(modelIx)].t.name)) +
                   L", id " + std::to_wstring(id));
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
    // The game's tile of a group (its first subtile that is a tile): what a rect of that group shows in the game, the
    // tile the mod's tile stands in for. 0: none.
    uint16_t gameTile(uint32_t group) const { return stardraft::terrainrules::gameTile(tileset_, group); }
    // The checklist's rows: the template's rects, then the set's pieces for rects outside it ("other").
    struct CheckRow {
        uint16_t group = 0;
        std::wstring role;
        int piece = -1;  // (the first piece of that group; -1: none)
        int more = 0;    // (more pieces of that group: variants)
    };
    std::vector<CheckRow> checkRows() const {
        std::vector<CheckRow> rows;
        const auto* t = terrain();
        for (const auto& r : tmpl_) {
            CheckRow row{r.group, widen(r.role), -1, 0};
            if (t) {
                bool bad = false;  // (a group with an incomplete piece among its variants: that one is the row's)
                for (size_t k = 0; k < t->t.pieces.size(); ++k)
                    if (t->t.pieces[k][0] == r.group) {
                        const bool incomplete = isomrules::anyNull(t->t.pieces[k]);
                        if (row.piece < 0 || (incomplete && !bad)) {
                            if (row.piece >= 0) ++row.more;
                            row.piece = int(k), bad = bad || incomplete;
                        } else ++row.more;
                    }
            }
            rows.push_back(std::move(row));
        }
        if (t) {
            std::set<uint16_t> in;
            for (const auto& r : tmpl_) in.insert(r.group);
            for (size_t k = 0; k < t->t.setPieces && k < t->t.pieces.size(); ++k)
                if (const uint16_t g = t->t.pieces[k][0]; !in.count(g)) in.insert(g), rows.push_back({g, L"other " + std::to_wstring(g), int(k), 0});
        }
        if (checkTodo_ && t)  // (to do: the rects whose piece is not the mod's own)
            rows.erase(std::remove_if(rows.begin(), rows.end(),
                                      [&](const CheckRow& r) {
                                          return r.piece >= 0 && r.piece < int(t->t.pieces.size()) &&
                                                 stardraft::terrainrules::stateOf(t->t.pieces[size_t(r.piece)]) == isomrules::PieceState::Own;
                                      }),
                       rows.end());
        return rows;
    }
    void toggleTodo() {
        checkTodo_ = !checkTodo_, checkScroll_ = 0;
        setMessage(checkTodo_ ? L"Checklist: rects without mod tiles" : L"Checklist: every rect");
        invalidate();
    }
    static constexpr int kCheckRow = 32;  // (a checklist row's height, before scaling)
    // The template of the shown terrain (or the standard border): its rects by role, its family.
    void updateTemplate() {
        tmpl_.clear(), family_ = isomrules::Family::Other, tmplPair_ = {0, 0}, checkScroll_ = 0;
        if (const auto* t = terrain()) tmplPair_ = stardraft::terrainrules::templateOf(tileset_, t->t);
        else if (std_ >= 0 && std_ < int(stdPairs_.size())) tmplPair_ = stdPairs_[size_t(std_)];
        else return;
        tmpl_ = stardraft::terrainrules::templateRects(tileset_, tmplPair_.first, tmplPair_.second);
        family_ = stardraft::terrainrules::familyOf(tmpl_, tileset_);
    }
    // "12/43 verified · " for the tileset's terrains (none loaded: empty).
    std::wstring verifiedText() const {
        const auto* r = rulesOrNull();
        if (!r || r->terrains.empty()) return {};
        int n = 0, v = 0;
        for (const auto& x : r->terrains)
            if (!x.t.variantOf) ++n, v += x.t.draft ? 0 : 1;
        return std::to_wstring(n) + L" terrains \x00b7 " + std::to_wstring(v) + L" verified";
    }
    std::wstring templateName() const {
        const IsomTerrainTable* game = IsomTerrainTable::forEra(uint16_t(era()));
        if (!game || !tmplPair_.second) return L"?";
        return widen(game->terrainName(tmplPair_.second)) + L" (" + widen(game->terrainName(tmplPair_.first)) + L")";
    }
    // The role of a rect (by game group) in the shown template; "rect N" outside it.
    std::wstring roleOf(uint16_t group) const {
        for (const auto& r : tmpl_)
            if (r.group == group) return widen(r.role);
        return L"rect " + std::to_wstring(group);
    }
    // A tile into r (a NULL value: black; `ghost`: the game's tile of that half's group, dimmed: what belongs there).
    void drawTile(draw::Canvas& c, uint16_t v, draw::Rect r, uint32_t ghostGroup = 0xFFFFFFFFu) {
        c.fill(r, 0x000000);
        const bool ghost = isomrules::nullHalf(v);
        if (ghost) v = ghostGroup == 0xFFFFFFFFu ? 0 : gameTile(ghostGroup);
        if (!v) return;
        auto& b = chips_[v];
        if (!b) b = isomrules::tilesBitmap(tileset_, {v}, 1, 1);
        if (b) c.bitmap(*b, {0, 0, b->width(), b->height()}, r, false, ghost ? 100 : 255);
    }

    stardraft::kit::Dropdown era_{{}, -1}, ground_{{}, -1}, row_{{}, -1}, refPick_{{}, -1};
    std::vector<std::pair<uint16_t, uint16_t>> stdPairs_;  // (the game's borders: its ground, the terrain on it)
    int std_ = -1;  // (the standard terrain shown: an index of stdPairs_; -1: a terrain of the rules)
    bool refOn_ = false;  // (the reference over the example; it paints only while it shows)
    stardraft::kit::Slider refOpacity_{10, 100, 50, 10};  // (its opacity in %; 100: alone)
    std::set<int> myRows_;  // (the rows of the tiles that are the shown terrain's: its pieces' and the ones its maps use)
    std::set<uint16_t> visibleGroups_;  // (the rects the example shows, by game group: they keep when the pieces move)
    std::unique_ptr<draw::Bitmap> refPicture_;
    stardraft::kit::ScrollBar palBar_;  // (the tiles' scroll bar, at the right edge)
    int palTotal_ = -1, palPage_ = -1;  // (the bar's range as it was last set)
    stardraft::kit::Button menu_, refShow_, newTerrain_;
    std::vector<isomrules::TemplateRect> tmpl_;  // (the shown terrain's template: its rects by role, in the game's order)
    std::pair<uint16_t, uint16_t> tmplPair_{0, 0};  // (the template: the game ground and the game terrain on it)
    isomrules::Family family_ = isomrules::Family::Other;
    int checkScroll_ = 0, checkHover_ = -1;  // (the checklist: its scroll, the row under the mouse)
    std::map<uint16_t, std::vector<std::pair<int, int>>> moreRects_;  // (a terrain id -> the extra spots More rects found)
    int extendNext_ = 0;  // (the next example searches for more spots: 1 by itself, 2 More rects)
    // Undo (Ctrl+Z) and redo (Ctrl+Y): a terrain as it was before an edit (its id: the list can change).
    struct Edit {
        uint16_t id = 0;
        stardraft::terrainrules::Terrain before;
        int target = -1, half = 0;
    };
    std::vector<Edit> undo_, redo_;
    stardraft::kit::TextField search_;
    // The sidebar: the terrains by family, each a row with its swatch, name, family, done/total and draft mark.
    static constexpr int kSideHeader = -1000000;
    struct SideRow {
        int value;  // (a list value, or kSideHeader)
        std::wstring header;
        int y, h;  // (in the sidebar's scroll space)
    };
    std::vector<SideRow> sideRows_;
    std::map<uint32_t, isomrules::Family> familyCache_;
    stardraft::kit::ScrollBar sideBar_;
    draw::Rect side_;
    int sideScroll_ = 0, sideTotal_ = 0, sideHover_ = -1, sideBarTotal_ = -1, sideBarPage_ = -1;
    bool checkTodo_ = false;  // (the checklist: only the rects without mod tiles)
    stardraft::terrainrules::Rules doc_;  // (the rules being edited: each tileset opened, or the loaded file's)
    std::filesystem::path file_;
    bool dirty_ = false, wide_ = false;  // (wide_: a pick fills both halves: the tile and the next one in its row; T)
    int palHover_ = -1;  // (the tile under the mouse in the tiles)
    int barEnd_ = 0;  // (the bar's controls end: the file's name after them)
    stardraft::Tileset tileset_;
    std::vector<uint16_t> groundIds_;
    std::vector<int> shown_;
    int sel_ = -1, target_ = -1, targetHalf_ = 0, hover_ = -1;
    std::vector<Place> places_;
    std::vector<uint16_t> ids_;  // (the example's tiles, crop_ row by row: the tile IDs)
    int tileIds_ = 0, idHover_ = -1;  // (tileIds_: the example's tile IDs 0 off, 1 on hover, 2 always; idHover_: the cell in ids_)
    std::vector<int> holes_, absent_;
    std::unique_ptr<draw::Bitmap> picture_;
    struct Spot { int x, y; float s; };
    std::unique_ptr<Editor> puzzleEd_;  // (the examples' map, kept between rebuilds: its unit data loads once; the worker's)
    bool async_ = true, loading_ = false, building_ = false;
    int openGen_ = 0, shownId_ = -1, jump_ = -1;  // (jump_: the tile framed in the tiles; jumpRow_: its whole row)
    bool jumpRow_ = false;
    std::atomic<int> buildGen_{0};  // (the newest example asked for: the worker leaves out older ones)
    draw::Rect crop_, puzzle_, palette_, strip_, check_, refRow_;
    // The four columns (Terrains, Rects, Example, Tiles): their order and which show, kept in the layout settings
    // (Layout/IsomColumns: the order in 2-bit digits, the shown bits from bit 8). Each has a header; its right-click
    // menu moves or hides it (a hidden one comes back from any header's menu or the Menu button).
    enum Col { kColTerrains = 0, kColRects = 1, kColExample = 2, kColTiles = 3 };
    int colOrder_[4] = {kColTerrains, kColRects, kColExample, kColTiles};
    bool colShown_[4] = {true, true, true, true};
    draw::Rect colHead_[4], colBody_[4];  // (empty while hidden)
    int split_ = -1;  // (the x between the tiles and the example when they stand side by side: dragged, the tiles' width)
    bool tilesLeftOfExample_ = false, laidOut_ = false;
    static const wchar_t* colName(int c) { return c == kColTerrains ? L"Terrains" : c == kColRects ? L"Rects" : c == kColExample ? L"Example" : L"Tiles"; }
    int colAt(draw::Point p) const {
        for (int c = 0; c < 4; ++c)
            if (colShown_[c] && colHead_[c].contains(p)) return c;
        return -1;
    }
    void saveColumns() {
        int v = 0;
        for (int i = 0; i < 4; ++i) v |= colOrder_[i] << (2 * i);
        for (int c = 0; c < 4; ++c) v |= (colShown_[c] ? 1 : 0) << (8 + c);
        prefs::setNumber(L"Layout/IsomColumns", v);
    }
    void loadColumns() {
        int v = 0;
        if (!prefs::number(L"Layout/IsomColumns", v, 0, 0xFFF)) return;
        int order[4];
        bool seen[4] = {};
        for (int i = 0; i < 4; ++i) {
            order[i] = (v >> (2 * i)) & 3;
            if (seen[order[i]]) return;  // (not a permutation: the default stays)
            seen[order[i]] = true;
        }
        for (int i = 0; i < 4; ++i) colOrder_[i] = order[i];
        bool any = false;
        for (int c = 0; c < 4; ++c) colShown_[c] = ((v >> (8 + c)) & 1) != 0, any = any || colShown_[c];
        if (!any)
            for (int c = 0; c < 4; ++c) colShown_[c] = true;
    }
    void moveColumn(int c, int dir) {
        int i = 0;
        while (i < 4 && colOrder_[i] != c) ++i;
        int j = i + dir;
        while (j >= 0 && j < 4 && !colShown_[colOrder_[j]]) j += dir;  // (past hidden ones)
        if (j < 0 || j >= 4) return;
        std::swap(colOrder_[i], colOrder_[j]);
    }
    void setColumnShown(int c, bool on) {
        int shown = 0;
        for (int k = 0; k < 4; ++k) shown += colShown_[k] ? 1 : 0;
        if (!on && shown <= 1) return setMessage(L"The last column stays");
        colShown_[c] = on;
    }
    // A header's right-click menu: move the column, hide it, show a hidden one.
    void columnMenu(int c, draw::Point at) {
        std::vector<std::wstring> items = {L"Move left", L"Move right", L"Hide"};
        std::vector<int> hidden;
        for (int k = 0; k < 4; ++k)
            if (!colShown_[k]) hidden.push_back(k), items.push_back(std::wstring(L"Show ") + colName(k));
        const int pick = shellContextMenuAt(*this, at, items);
        if (pick == 1) moveColumn(c, -1);
        else if (pick == 2) moveColumn(c, 1);
        else if (pick == 3) setColumnShown(c, false);
        else if (pick >= 4 && pick - 4 < int(hidden.size())) setColumnShown(hidden[size_t(pick - 4)], true);
        else return;
        saveColumns(), layout(), invalidate();
    }
    static constexpr int kCheckHead = 22;  // (the checklist's column names)
    float zoom_ = 1;
    int paletteW_ = 0, palScroll_ = 0;
    // The palette's rows: every group, or (G) only the terrain's own groups (myRows_, sorted); the header above the rows.
    bool palMine_ = false;
    std::vector<int> palRows_;
    std::map<int, std::unique_ptr<draw::Bitmap>> rowPics_;  // (a row's picture, by group: the terrain-rows mode)
    std::vector<std::wstring> rowNames_;  // (a group -> the terrain its tiles are, from the mod's maps; the tip)
    int palRowCount() const { return palMine_ ? int(palRows_.size()) : int(tileset_.groups.size()); }
    int palGroupAt(int i) const { return palMine_ ? (i >= 0 && i < int(palRows_.size()) ? palRows_[size_t(i)] : -1) : i; }
    int palIndexOf(int g) const {
        if (!palMine_) return g;
        const auto it = std::lower_bound(palRows_.begin(), palRows_.end(), g);
        return it != palRows_.end() && *it == g ? int(it - palRows_.begin()) : -1;
    }
    int palCell() const { return std::max(scale(8), (palette_.width() - scale(56)) / 16); }
    int palScrollMax() const { return std::max(0, palRowCount() * palCell() - palette_.height()); }
    void togglePalMine() {
        if (!palMine_ && palRows_.empty()) return setMessage(L"No groups assigned to this terrain yet");
        palMine_ = !palMine_, palScroll_ = 0;
        setMessage(palMine_ ? L"Tiles: this terrain's groups" : L"Tiles: all groups");
        invalidate();
    }
    bool dragging_ = false;
    std::map<int, std::unique_ptr<draw::Bitmap>> pal_;  // (the palette's pictures, 64 groups each)
    std::map<uint16_t, std::unique_ptr<draw::Bitmap>> chips_;  // (the tray's tiles, one each)
};

// The sidebar: a header per family, a row per terrain (its swatch, its name, "family · done/total", a progress bar, a
// draft mark), the game's borders last; the shown one in the selection colour.
void IsomRulesView::paintSide(draw::Canvas& c) {
    const int pad = scale(6);
    c.fill(side_, kPanel);
    sideScroll_ = std::clamp(sideScroll_, 0, std::max(0, sideTotal_ - side_.height()));
    if (sideTotal_ != sideBarTotal_ || side_.height() != sideBarPage_) sideBar_.setRange(sideTotal_, side_.height()), sideBarTotal_ = sideTotal_, sideBarPage_ = side_.height();
    sideBar_.setPos(sideScroll_);
    c.pushClip(side_);
    const draw::Color kDraft = 0x50AAF0;  // (COLORREF: a warm orange)
    for (size_t i = 0; i < sideRows_.size(); ++i) {
        const SideRow& r = sideRows_[i];
        const int y = side_.top + r.y - sideScroll_;
        if (y + r.h < side_.top || y > side_.bottom) continue;
        if (r.value == kSideHeader) {
            c.fill({side_.left, y + r.h - 1, side_.right, y + r.h}, kBorder);
            c.text({side_.left + pad, y, side_.right - pad, y + r.h}, r.header, kDim, draw::kMiddle);
            continue;
        }
        const bool selected = (sel_ >= 0 && r.value == sel_) || (std_ >= 0 && r.value == -1 - std_);
        if (selected) c.fill({side_.left, y, side_.right, y + r.h}, kSel);
        else if (int(i) == sideHover_) c.fill({side_.left, y, side_.right, y + r.h}, kCtlHot);
        const int sw = r.h - scale(12);
        drawTile(c, swatchOf(r.value), {side_.left + pad, y + scale(6), side_.left + pad + sw, y + scale(6) + sw});
        const int x = side_.left + pad + sw + scale(8);
        if (r.value < 0) {
            c.text({x, y, side_.right - pad, y + r.h}, stdName(-1 - r.value), selected ? kText : kDim, draw::kMiddle | draw::kEllipsis);
            continue;
        }
        const auto& m = rules().terrains[size_t(r.value)].t;
        const auto [done, total] = stardraft::terrainrules::completeness(tileset_, m);
        c.text({x, y + scale(4), side_.right - pad - (m.draft ? scale(40) : 0), y + scale(22)}, widen(m.name), selected ? kText : kText, draw::kMiddle | draw::kEllipsis);
        if (m.draft) c.text({side_.right - pad - scale(38), y + scale(4), side_.right - pad, y + scale(22)}, L"draft", kDraft, draw::kMiddle | draw::kRight);
        c.text({x, y + scale(22), side_.right - pad, y + scale(36)}, isomrules::familyName(familyOfValue(r.value)) + L"  \x00b7  " + std::to_wstring(done) + L"/" + std::to_wstring(total) + L" rects",
               kDim, draw::kMiddle | draw::kEllipsis);
        const int bx0 = x, bx1 = side_.right - pad, by = y + r.h - scale(5);  // (the progress: done of total)
        c.fill({bx0, by, bx1, by + scale(2)}, kBorder);
        if (total > 0) c.fill({bx0, by, bx0 + (bx1 - bx0) * done / total, by + scale(2)}, done == total ? kAccent : kDraft);
    }
    c.popClip();
    c.fill({side_.left, side_.top, side_.right, side_.top + 1}, kBorder);
}

void IsomRulesView::layout() {
    const int pad = scale(8), barH = scale(stardraft::kit::kButtonH), W = width(), H = height(), headH = scale(22), barW = scale(stardraft::kit::kScrollBarW);
    laidOut_ = true;
    int x = pad;
    auto put = [&](draw::View& v, int w) { v.setFrame({x, pad, x + w, pad + barH}), x += w + pad; };
    put(era_, scale(130)), put(menu_, scale(80));
    x += scale(12);
    put(row_, scale(200));
    barEnd_ = x;
    const int top = pad * 2 + barH, leftW = scale(260), checkW = scale(300);
    if (!paletteW_) paletteW_ = 16 * scale(26) + scale(24);
    // The columns in their order; the example takes what the others leave (300 at least: the tiles give way), the tiles
    // take the rest when the example is hidden.
    auto fixedOf = [&](int c) { return c == kColTerrains ? leftW : c == kColRects ? checkW : c == kColTiles ? paletteW_ + barW : 0; };
    int fixed = 0;
    for (int c = 0; c < 4; ++c)
        if (colShown_[c] && c != kColTiles) fixed += fixedOf(c);
    if (colShown_[kColExample] && colShown_[kColTiles]) paletteW_ = std::clamp(paletteW_, scale(200), std::max(scale(200), W - fixed - scale(300) - barW));
    if (colShown_[kColTiles]) fixed += fixedOf(kColTiles);
    const int exampleW = colShown_[kColExample] ? std::max(scale(300), W - fixed) : 0;
    int cx = 0;
    split_ = -1;
    for (int i = 0; i < 4; ++i) {
        const int c = colOrder_[i];
        if (!colShown_[c]) {
            colHead_[c] = colBody_[c] = {0, 0, 0, 0};
            continue;
        }
        int w = c == kColExample ? exampleW : fixedOf(c);
        if (c == kColTiles && !colShown_[kColExample]) {  // (the rest of the width)
            int after = 0;
            for (int k = i + 1; k < 4; ++k)
                if (colShown_[colOrder_[k]]) after += fixedOf(colOrder_[k]);
            w = std::max(fixedOf(c), W - cx - after);
        }
        colHead_[c] = {cx, top, cx + w, top + headH}, colBody_[c] = {cx, top + headH, cx + w, H};
        cx += w;
    }
    for (int i = 0; i + 1 < 4; ++i) {  // (the tiles beside the example: the edge between them drags the tiles' width)
        int a = colOrder_[i], j = i + 1;
        while (j < 4 && !colShown_[colOrder_[j]]) ++j;
        if (!colShown_[a] || j >= 4) continue;
        const int b = colOrder_[j];
        if ((a == kColTiles && b == kColExample) || (a == kColExample && b == kColTiles)) split_ = colBody_[b].left, tilesLeftOfExample_ = a == kColTiles;
    }
    const draw::Rect none{0, 0, 0, 0};
    {  // Terrains: the search, the list with its bar, New terrain at the bottom.
        const draw::Rect& b = colBody_[kColTerrains];
        if (b.width() > 0) {
            search_.setFrame({b.left + pad, b.top + pad, b.right - pad, b.top + pad + barH});
            side_ = {b.left + pad, b.top + pad * 2 + barH, b.right - pad - barW, b.bottom - pad * 2 - barH};
            sideBar_.setFrame({side_.right, side_.top, side_.right + barW, side_.bottom});
            newTerrain_.setFrame({b.left + pad, b.bottom - pad - barH, b.right - pad, b.bottom - pad});
            revealSideRow();
        } else side_ = none, search_.setFrame(none), sideBar_.setFrame(none), newTerrain_.setFrame(none);
    }
    check_ = colBody_[kColRects];  // Rects: the checklist.
    {  // Example: the Bordering box and the terrain's line, the example, the target's strip, the reference row.
        const draw::Rect& b = colBody_[kColExample];
        if (b.width() > 0) {
            ground_.setFrame({b.left + scale(90), b.top + scale(4), std::min(b.right - pad, b.left + scale(300)), b.top + scale(4) + barH});
            const int refH = barH + pad;
            refRow_ = {b.left, b.bottom - refH, b.right, b.bottom};
            strip_ = {b.left, refRow_.top - scale(84), b.right, refRow_.top};
            puzzle_ = {b.left, b.top + scale(8) + barH, b.right, strip_.top};
            int rx = b.left + pad;
            const int ry = refRow_.top + pad / 2;
            refShow_.setFrame({rx, ry, rx + scale(130), ry + barH}), rx += scale(130) + pad;
            refOpacity_.setFrame({rx, ry + scale(4), rx + scale(90), ry + barH - scale(4)}), rx += scale(90) + pad;
            refPick_.setFrame({rx, ry, std::max(rx + scale(60), b.right - pad), ry + barH});
        } else puzzle_ = strip_ = refRow_ = none, ground_.setFrame(none), refShow_.setFrame(none), refOpacity_.setFrame(none), refPick_.setFrame(none);
    }
    {  // Tiles: the palette with its bar at its right.
        const draw::Rect& b = colBody_[kColTiles];
        if (b.width() > 0) palette_ = {b.left, b.top, b.right - barW, b.bottom}, palBar_.setFrame({b.right - barW, b.top, b.right, b.bottom});
        else palette_ = none, palBar_.setFrame(none);
    }
}

void IsomRulesView::paint(draw::Canvas& c) {
    const int W = width(), H = height(), pad = scale(8);
    const draw::Color kBorrowed = 0xFFA05A, kHole = 0x4650E6;  // (COLORREF: a blue, a red)
    (void)H;
    c.fill({0, 0, W, H}, kBg);
    if (!laidOut_) layout();
    // The top bar's right: the file and the unsaved dot.
    c.text({barEnd_, pad, W - pad, pad + scale(stardraft::kit::kButtonH)}, (file_.empty() ? std::wstring(L"(not saved)") : file_.filename().wstring()) + (dirty_ ? L"  \x25cf" : L""),
           dirty_ ? kAccent : kDim, draw::kMiddle | draw::kRight | draw::kEllipsis);
    const auto* t = terrain();
    // The columns' headers: the name at the left, the column's own switch at the right (the right-click menu moves or hides).
    for (int col = 0; col < 4; ++col) {
        if (!colShown_[col]) continue;
        const draw::Rect& h = colHead_[col];
        c.fill(h, kPanel);
        c.fill({h.left, h.bottom - 1, h.right, h.bottom}, kBorder);
        if (h.left > 0) c.fill({h.left, h.top, h.left + 1, H}, kBorder);
        c.text({h.left + pad, h.top, h.right - pad, h.bottom}, colName(col), kText, draw::kMiddle);
        std::wstring right;
        draw::Color tone = kDim;
        if (col == kColTerrains) right = verifiedText();
        else if (col == kColRects) right = checkTodo_ ? L"to do" : L"all", tone = checkTodo_ ? kAccent : kDim;
        else if (col == kColTiles) right = palMine_ ? L"this terrain" : L"all groups", tone = palMine_ ? kAccent : kDim;
        else if (col == kColExample && t) right = isomrules::familyName(family_) + L" \x00b7 " + templateName();
        if (!right.empty()) c.text({h.left + pad, h.top, h.right - pad, h.bottom}, right, tone, draw::kMiddle | draw::kRight | draw::kEllipsis);
    }
    if (split_ >= 0) c.fill({split_ - 1, colHead_[kColTiles].top, split_ + 1, H}, kBorderHot);
    if (side_.width() > 0) paintSide(c);
    if (puzzle_.width() > 0) {
        c.text({puzzle_.left + pad, puzzle_.top - scale(stardraft::kit::kButtonH) - pad, puzzle_.left + scale(88), puzzle_.top - pad}, L"Bordering", kDim, draw::kMiddle);
        if (t || std_ >= 0)
            c.text({std::min(puzzle_.right - pad, ground_.frame().right + pad), puzzle_.top - scale(stardraft::kit::kButtonH) - pad, puzzle_.right - pad, puzzle_.top - pad},
                   t ? std::to_wstring(stardraft::terrainrules::completeness(tileset_, t->t).first) + L"/" + std::to_wstring(tmpl_.size()) + (t->t.draft ? L" \x00b7 draft" : L" \x00b7 verified")
                     : std::to_wstring(tmpl_.size()) + L" rects \x00b7 game border",
                   kDim, draw::kMiddle | draw::kRight | draw::kEllipsis);
    }
    // The puzzle: the examples, fit to the area; NULL holes framed red, the target orange, the hovered piece's places.
    c.fill(puzzle_, 0x151515);
    if (puzzle_.width() > 0 && picture_ && (t || std_ >= 0)) {
        const bool refShown = refOn_ && refPicture_ != nullptr, refAlone = refShown && refOpacity_.value() >= 100;
        zoom_ = std::min(float(puzzle_.width() - 2 * pad) / picture_->width(), float(puzzle_.height() - 2 * pad) / picture_->height());
        zoom_ = std::max(0.1f, std::min(zoom_, 2.0f));
        c.pushClip(puzzle_);
        const int pw = int(picture_->width() * zoom_), ph = int(picture_->height() * zoom_);
        const int ox = puzzle_.left + (puzzle_.width() - pw) / 2, oy = puzzle_.top + (puzzle_.height() - ph) / 2;
        c.bitmap(*picture_, {0, 0, picture_->width(), picture_->height()}, {ox, oy, ox + pw, oy + ph});
        if (refShown) c.bitmap(*refPicture_, {0, 0, refPicture_->width(), refPicture_->height()}, {ox, oy, ox + pw, oy + ph}, false, uint8_t(255 * refOpacity_.value() / 100));
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
            if (!t || refAlone) break;
            const Place& p = places_[size_t(h)];
            if (p.piece < 0 || p.piece >= int(t->t.pieces.size())) continue;
            const auto& q = t->t.pieces[size_t(p.piece)];
            for (int half = 0; half < 2; ++half)  // (a NULL half: black, with the game's tile of its group dimmed: what belongs there)
                if (isomrules::nullHalf(q[size_t(1 + half)])) drawTile(c, 0, halfOf(rectOf(p), half), uint32_t(q[0]) + uint32_t(half));
            c.frame(rectOf(p), 0x4650E6, scale(2));
        }
        if (t && !refAlone)
            for (const Place& p : places_)  // (a borrowed rect: the game's own tiles, a blue frame)
                if (p.piece >= 0 && p.piece < int(t->t.pieces.size()) && stardraft::terrainrules::stateOf(t->t.pieces[size_t(p.piece)]) == isomrules::PieceState::Borrowed)
                    c.frame(rectOf(p), kBorrowed, scale(1));
        if (t && palHover_ >= 0 && target_ >= 0 && target_ < int(t->t.pieces.size()) && !refAlone)  // (the tile under the mouse, as it would sit in the target)
            for (const Place& p : places_)
                if (p.piece == target_) {
                    if (wide_) {
                        drawTile(c, uint16_t(palHover_), halfOf(rectOf(p), 0));
                        if ((palHover_ & 15) < 15) drawTile(c, uint16_t(palHover_ + 1), halfOf(rectOf(p), 1));
                    } else {
                        drawTile(c, uint16_t(palHover_), halfOf(rectOf(p), targetHalf_));
                    }
                    c.frame(wide_ ? rectOf(p) : halfOf(rectOf(p), targetHalf_), 0xFFFFFF, scale(1));
                }
        for (const Place& p : places_)
            if (p.piece == target_ && target_ >= 0) c.frame(wide_ ? rectOf(p) : halfOf(rectOf(p), targetHalf_), kAccent, scale(2));
        if (tileIds_ && ids_.size() == size_t(crop_.width()) * size_t(crop_.height())) {  // (the tile IDs: group over subtile)
            const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 4)}));
            auto label = [&](size_t i) {
                const int x = ox + int(float(i % size_t(crop_.width())) * ts), y = oy + int(float(i / size_t(crop_.width())) * ts);
                const draw::Rect r{x, y, x + int(ts), y + int(ts)};
                const uint16_t v = ids_[i];
                c.fill({r.left + 1, r.top + 1, r.right - 1, r.bottom - 1}, 0x000000, 150);
                if (v == kNullId) return c.text(r, L"NULL", kHole, draw::kCenter | draw::kMiddle);
                const int mid = (r.top + r.bottom) / 2;
                c.text({r.left, r.top, r.right, mid + scale(2)}, std::to_wstring(v >> 4), 0xFFFFFF, draw::kCenter | draw::kBottom);
                c.text({r.left, mid - scale(2), r.right, r.bottom}, L"." + std::to_wstring(v & 15), 0xFFFFFF, draw::kCenter);
            };
            if (tileIds_ == 2)
                for (size_t i = 0; i < ids_.size(); ++i) label(i);
            else if (idHover_ >= 0 && size_t(idHover_) < ids_.size())
                label(size_t(idHover_));
            c.setFont(of);
        }
        c.popClip();
    } else if (puzzle_.width() > 0) {
        c.text(puzzle_, loading_ ? L"Loading tileset\x2026" : building_ ? L"Rendering example\x2026" : t ? L"No example (mod data missing)" : L"Select a terrain",
               kDim, draw::kCenter | draw::kMiddle);
    }
    if (building_ && picture_)
        c.text({puzzle_.left + pad, puzzle_.top + pad, puzzle_.right - pad, puzzle_.top + scale(28)}, L"Rendering\x2026", kDim, draw::kRight);
    // The checklist (the Rects column): every rect of the template by its role, the template's tiles, the custom tiles,
    // its state; the target's row marked. The column names on top.
    if (check_.width() > 0) {
        c.fill(check_, kPanel);
        const std::vector<CheckRow> rows = checkRows();
        const int rh = scale(kCheckRow), th = scale(24), head = scale(kCheckHead);
        const int total = int(rows.size()) * rh;
        checkScroll_ = std::clamp(checkScroll_, 0, std::max(0, total - (check_.height() - head)));
        const int x0 = check_.left + pad, xt = x0 + scale(110), xc = xt + 2 * th + scale(16), xs = xc + 2 * th + pad;
        c.text({x0, check_.top, xt, check_.top + head}, L"Role", kDim, draw::kMiddle);
        c.text({xt, check_.top, xt + 2 * th + scale(16), check_.top + head}, L"Template", kDim, draw::kMiddle);
        c.text({xc, check_.top, check_.right - pad, check_.top + head}, L"Custom", kDim, draw::kMiddle);
        c.fill({check_.left, check_.top + head - 1, check_.right, check_.top + head}, kBorder);
        c.pushClip({check_.left, check_.top + head, check_.right, check_.bottom});
        for (size_t i = 0; i < rows.size(); ++i) {
            const int y = check_.top + head + int(i) * rh - checkScroll_;
            if (y + rh < check_.top + head || y > check_.bottom) continue;
            const CheckRow& row = rows[i];
            const std::array<uint16_t, 3>* p = t && row.piece >= 0 && row.piece < int(t->t.pieces.size()) ? &t->t.pieces[size_t(row.piece)] : nullptr;
            const isomrules::PieceState st = p ? stardraft::terrainrules::stateOf(*p) : isomrules::PieceState::Null;
            const bool isTarget = t && target_ >= 0 && row.piece >= 0 && t->t.pieces[size_t(target_)][0] == row.group;
            if (isTarget) c.fill({check_.left, y, check_.right - 1, y + rh}, kSel);
            else if (int(i) == checkHover_) c.fill({check_.left, y, check_.right - 1, y + rh}, kCtlHot);
            const bool shown = visibleGroups_.count(row.group) > 0 || std_ >= 0;
            c.text({x0, y, xt - scale(4), y + rh}, row.role, isTarget ? kAccent : shown ? kText : kDim, draw::kMiddle | draw::kEllipsis);
            const int ty = y + (rh - th) / 2;
            drawTile(c, gameTile(row.group), {xt, ty, xt + th, ty + th});
            drawTile(c, gameTile(uint32_t(row.group) + 1), {xt + th, ty, xt + 2 * th, ty + th});
            c.frame({xt, ty, xt + 2 * th, ty + th}, kBorder, scale(1));
            if (t) {
                drawTile(c, p ? (*p)[1] : 0, {xc, ty, xc + th, ty + th}, row.group);
                drawTile(c, p ? (*p)[2] : 0, {xc + th, ty, xc + 2 * th, ty + th}, uint32_t(row.group) + 1);
                c.frame({xc, ty, xc + 2 * th, ty + th}, st == isomrules::PieceState::Own ? kBorder : st == isomrules::PieceState::Borrowed ? kBorrowed : kHole, scale(1));
                const std::wstring state = st == isomrules::PieceState::Own ? L"custom" : st == isomrules::PieceState::Borrowed ? L"borrowed" : st == isomrules::PieceState::Partial ? L"partial" : L"NULL";
                c.text({xs, y, check_.right - pad, y + rh}, state, st == isomrules::PieceState::Own ? kDim : st == isomrules::PieceState::Borrowed ? kBorrowed : kHole,
                       draw::kMiddle | draw::kEllipsis);
            }
        }
        c.popClip();
    }
    // The target's strip: the game's tiles of its rect (the groups the mod's tiles stand in for) beside its own, large.
    c.fill({strip_.left, strip_.top, strip_.right, strip_.top + 1}, kBorder);
    c.fill({refRow_.left, refRow_.top, refRow_.right, refRow_.top + 1}, kBorder);
    if (strip_.width() > 0 && t && target_ >= 0 && target_ < int(t->t.pieces.size())) {
        const auto& q = t->t.pieces[size_t(target_)];
        const int T = scale(44), y0 = strip_.top + pad + scale(16);
        int x = strip_.left + pad;
        auto half = [](uint16_t v) { return isomrules::nullHalf(v) ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15); };
        c.text({x, strip_.top + pad, x + scale(104), y0}, roleOf(q[0]), kText, draw::kMiddle | draw::kEllipsis);
        if (roleOf(q[0]) != L"rect " + std::to_wstring(q[0])) c.text({x, y0, x + scale(104), y0 + scale(20)}, L"rect " + std::to_wstring(q[0]), kFaint, draw::kMiddle);
        c.text({x, y0 + scale(20), x + scale(104), y0 + scale(40)}, half(q[1]) + L"  " + half(q[2]), kDim, draw::kMiddle);
        c.text({x + scale(112), strip_.top + pad, x + scale(112) + 2 * T, y0}, L"Template", kDim, draw::kMiddle);
        c.text({x + scale(112) + 2 * T + scale(24), strip_.top + pad, strip_.right - pad, y0}, L"Custom", kDim, draw::kMiddle);
        x += scale(112);
        drawTile(c, gameTile(q[0]), {x, y0, x + T, y0 + T}), drawTile(c, gameTile(uint32_t(q[0]) + 1), {x + T, y0, x + 2 * T, y0 + T});
        c.frame({x, y0, x + 2 * T, y0 + T}, kBorder, scale(1));
        c.text({x + 2 * T, y0, x + 2 * T + scale(24), y0 + T}, L"\x2192", kDim, draw::kCenter | draw::kMiddle);
        x += 2 * T + scale(24);
        drawTile(c, q[1], {x, y0, x + T, y0 + T}, q[0]), drawTile(c, q[2], {x + T, y0, x + 2 * T, y0 + T}, uint32_t(q[0]) + 1);
        c.frame({x, y0, x + 2 * T, y0 + T}, kAccent, scale(1));
        if (!wide_) c.frame(targetHalf_ ? draw::Rect{x + T, y0, x + 2 * T, y0 + T} : draw::Rect{x, y0, x + T, y0 + T}, kAccent, scale(2));
    } else if (strip_.width() > 0) {
        c.text({strip_.left + pad, strip_.top, strip_.left + scale(300), strip_.bottom}, L"Select a rect: template vs custom tiles", kDim, draw::kMiddle);
    }
    // The palette (the Tiles column): a tile group a row, its 16 subtiles; the target's tiles framed.
    if (palette_.width() <= 0) return;
    c.fill(palette_, kPanel);
    const int cell = palCell(), x0 = palette_.left + scale(48);
    const int rows = palRowCount();
    if (!palMine_) {  // (the pictures of 64 groups each: only those near the view stay, about 4 MB each)
        const int first = palScroll_ / std::max(1, cell) / 64, last = (palScroll_ + palette_.height()) / std::max(1, cell) / 64;
        for (auto it = pal_.begin(); it != pal_.end();)
            it = it->first < first - 1 || it->first > last + 1 ? pal_.erase(it) : std::next(it);
    }
    if (rowPics_.size() > 300) rowPics_.clear();
    if (const int total = rows * cell, page = palette_.height(); total != palTotal_ || page != palPage_) palBar_.setRange(total, page), palTotal_ = total, palPage_ = page;
    palBar_.setPos(palScroll_);
    c.pushClip(palette_);
    for (int i = std::max(0, palScroll_ / cell); i < rows && (i * cell - palScroll_) < palette_.height(); ++i) {
        const int g = palGroupAt(i);
        if (g < 0) continue;
        const int y = palette_.top + i * cell - palScroll_;
        const bool mine = myRows_.count(g) > 0;  // (a row of the terrain's own tiles: marked)
        if (mine) c.fill({palette_.left + 1, y, palette_.left + scale(4), y + cell}, kAccent);
        if (palMine_ || g % 4 == 0 || mine) c.text({palette_.left + scale(4), y, x0 - scale(4), y + cell}, std::to_wstring(g), mine ? kAccent : kDim, draw::kMiddle | draw::kRight);
        if (!palMine_) {
            const int chunk = g / 64;
            auto& b = pal_[chunk];
            if (!b) {
                std::vector<uint16_t> tiles;
                for (int k = chunk * 64; k < chunk * 64 + 64; ++k)
                    for (int s = 0; s < 16; ++s) tiles.push_back(k < int(tileset_.groups.size()) && tileset_.megatileFor(uint16_t(k << 4 | s)) > 0 ? uint16_t(k << 4 | s) : 0);
                b = isomrules::tilesBitmap(tileset_, tiles, 16, 64);
            }
            if (b) c.bitmap(*b, {0, (g % 64) * 32, 16 * 32, (g % 64) * 32 + 32}, {x0, y, x0 + 16 * cell, y + cell});
        } else {
            auto& b = rowPics_[g];
            if (!b) {
                std::vector<uint16_t> tiles;
                for (int s = 0; s < 16; ++s) tiles.push_back(tileset_.megatileFor(uint16_t(g << 4 | s)) > 0 ? uint16_t(g << 4 | s) : 0);
                b = isomrules::tilesBitmap(tileset_, tiles, 16, 1);
            }
            if (b) c.bitmap(*b, {0, 0, 16 * 32, 32}, {x0, y, x0 + 16 * cell, y + cell});
        }
    }
    auto rowY = [&](int g) { const int i = palIndexOf(g); return i < 0 ? INT_MIN : palette_.top + i * cell - palScroll_; };
    if (t && target_ >= 0 && target_ < int(t->t.pieces.size()))
        for (int half = 1; half <= 2; ++half) {
            const uint16_t v = t->t.pieces[size_t(target_)][size_t(half)];
            if (isomrules::nullHalf(v) || rowY(v >> 4) == INT_MIN) continue;
            const int y = rowY(v >> 4), x = x0 + int(v & 15) * cell;
            c.frame({x, y, x + cell, y + cell}, kAccent, scale(2));
        }
    if (palHover_ >= 0 && rowY(palHover_ >> 4) != INT_MIN) {  // (what a click puts in: 1 or 2 tiles)
        const int y = rowY(palHover_ >> 4), x = x0 + (palHover_ & 15) * cell;
        c.frame({x, y, x + (wide_ && (palHover_ & 15) < 15 ? 2 : 1) * cell, y + cell}, 0xFFFFFF, scale(1));
    }
    if (jump_ >= 0 && rowY(jump_ >> 4) != INT_MIN) {  // (the row or the tile that was asked for)
        const int y = rowY(jump_ >> 4);
        const int x = jumpRow_ ? x0 : x0 + (jump_ & 15) * cell, w = jumpRow_ ? 16 * cell : cell;
        c.frame({x - 1, y - 1, x + w + 1, y + cell + 1}, 0x30C8FF, scale(2));
    }
    c.popClip();
}

bool IsomRulesView::mouse(const draw::MouseEvent& e) {
    const auto* t = terrain();
    const int cell = palCell(), x0 = palette_.left + scale(48);
    if (e.kind == draw::MouseKind::Wheel && palette_.contains(e.pt)) {
        palScroll_ = std::clamp(palScroll_ - int(draw::wheelDistance(e, float(cell * 3))), 0, palScrollMax());
        invalidate();
        return true;
    }
    if (const int col = colAt(e.pt); col >= 0) {  // (a column's header: its switch at the right; the right button: its menu)
        if (e.kind == draw::MouseKind::Up && e.button == 1) return columnMenu(col, e.pt), true;
        if (e.kind == draw::MouseKind::Down && e.button == 0 && e.pt.x >= colHead_[col].right - scale(120)) {
            if (col == kColTiles) togglePalMine();
            else if (col == kColRects) toggleTodo();
        }
        return e.kind == draw::MouseKind::Down;
    }
    if (e.kind == draw::MouseKind::Wheel && side_.contains(e.pt)) {
        sideScroll_ = std::clamp(sideScroll_ - int(draw::wheelDistance(e, float(scale(44) * 2))), 0, std::max(0, sideTotal_ - side_.height()));
        invalidate();
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 0 && side_.contains(e.pt)) {  // (a terrain's row: shown)
        const int sy = e.pt.y - side_.top + sideScroll_;
        for (const SideRow& r : sideRows_)
            if (r.value != kSideHeader && sy >= r.y && sy < r.y + r.h) {
                if (!loading_) selectValue(r.value);
                return true;
            }
        return true;
    }
    if (e.kind == draw::MouseKind::Wheel && check_.contains(e.pt)) {
        checkScroll_ = std::max(0, checkScroll_ - int(draw::wheelDistance(e, float(scale(kCheckRow) * 2))));
        invalidate();
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 0 && split_ >= 0 && std::abs(e.pt.x - split_) < scale(4)) {
        dragging_ = true;
        if (host()) host()->capture(*this, true);
        return true;
    }
    if (dragging_ && e.kind == draw::MouseKind::Move) {
        paletteW_ = tilesLeftOfExample_ ? e.pt.x - colBody_[kColTiles].left - scale(stardraft::kit::kScrollBarW) : colBody_[kColTiles].right - scale(stardraft::kit::kScrollBarW) - e.pt.x;
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
        int v = -1;
        if (palette_.contains(e.pt) && e.pt.x >= x0) {
            const int g = palGroupAt((e.pt.y - palette_.top + palScroll_) / cell), s = (e.pt.x - x0) / cell;
            if (g >= 0 && g < int(tileset_.groups.size()) && s >= 0 && s < 16) v = g << 4 | s;
        }
        if (v != palHover_) palHover_ = v, invalidate();
        int half = 0;
        const int i = placeAt(e.pt, half), h = i >= 0 ? places_[size_t(i)].piece : -1;
        if (h != hover_) hover_ = h, invalidate();
        int cell = -1;  // (the example's tile under the mouse: its ID on hover)
        if (picture_ && puzzle_.contains(e.pt) && crop_.width() > 0) {
            const int pw = int(picture_->width() * zoom_), ph = int(picture_->height() * zoom_);
            const int ox = puzzle_.left + (puzzle_.width() - pw) / 2, oy = puzzle_.top + (puzzle_.height() - ph) / 2;
            if (e.pt.x >= ox && e.pt.y >= oy) {
                const int cx = int(float(e.pt.x - ox) / (32 * zoom_)), cy = int(float(e.pt.y - oy) / (32 * zoom_));
                if (cx < crop_.width() && cy < crop_.height()) cell = cy * crop_.width() + cx;
            }
        }
        if (cell != idHover_) {
            idHover_ = cell;
            if (tileIds_ == 1) invalidate();
        }
        int sh = -1;
        if (side_.contains(e.pt))
            for (size_t i = 0; i < sideRows_.size(); ++i)
                if (const int sy = e.pt.y - side_.top + sideScroll_; sideRows_[i].value != kSideHeader && sy >= sideRows_[i].y && sy < sideRows_[i].y + sideRows_[i].h) sh = int(i);
        if (sh != sideHover_) sideHover_ = sh, invalidate();
        const int cdy = e.pt.y - check_.top - scale(kCheckHead) + checkScroll_;
        const int ch = check_.contains(e.pt) && cdy >= 0 ? cdy / scale(kCheckRow) : -1;
        if (ch != checkHover_) checkHover_ = ch, invalidate();
        return false;
    }
    if (e.kind != draw::MouseKind::Down || e.button != 0 || !t || loading_) return false;
    if (int half = 0; placeAt(e.pt, half) >= 0) {
        setTarget(places_[size_t(placeAt(e.pt, half))].piece, half);
        if (e.mods & draw::kCtrl) {  // (its tiles in the palette: the half clicked)
            const auto& q = t->t.pieces[size_t(target_)];
            if (isomrules::nullHalf(q[size_t(1 + half)])) setMessage(L"Rect " + std::to_wstring(q[0]) + L": half is NULL");
            else showRow(q[1 + half] >> 4, q[1 + half]);
        }
        invalidate();
        return true;
    }
    if (check_.contains(e.pt)) {  // (a row of the checklist: its rect is the target; the half: the custom tile clicked)
        const int dy = e.pt.y - check_.top - scale(kCheckHead) + checkScroll_;
        const std::vector<CheckRow> rows = checkRows();
        if (dy >= 0 && dy / scale(kCheckRow) < int(rows.size())) {
            const CheckRow& row = rows[size_t(dy / scale(kCheckRow))];
            const int th = scale(24), xc = check_.left + scale(8) + scale(110) + 2 * th + scale(16);  // (the custom tiles' left)
            if (row.piece >= 0) setTarget(row.piece, e.pt.x >= xc + th && e.pt.x < xc + 2 * th ? 1 : e.pt.x >= xc && e.pt.x < xc + th ? 0 : -1);
        }
        return true;
    }
    if (palette_.contains(e.pt) && e.pt.x >= x0) {
        const int g = palGroupAt((e.pt.y - palette_.top + palScroll_) / cell), s = (e.pt.x - x0) / cell;
        if (g >= 0 && g < int(tileset_.groups.size()) && s >= 0 && s < 16 && tileset_.megatileFor(uint16_t(g << 4 | s)) > 0) place(uint16_t(g << 4 | s));
        return true;
    }
    return false;
}

std::wstring IsomRulesView::tip(draw::Point p) {
    const auto* t = terrain();
    if (!t) return {};
    const int cell = palCell(), x0 = palette_.left + scale(48);
    if (palette_.contains(p) && p.x >= x0) {
        const int g = palGroupAt((p.y - palette_.top + palScroll_) / cell), s = (p.x - x0) / cell;
        if (g < 0) return {};
        return std::to_wstring(g) + L"." + std::to_wstring(s) + (g < int(rowNames_.size()) && !rowNames_[size_t(g)].empty() ? L"  \x00b7  " + rowNames_[size_t(g)] : L"");
    }
    if (const int col = colAt(p); col >= 0) {
        const bool right = p.x >= colHead_[col].right - scale(120);
        if (col == kColRects && right) return checkTodo_ ? L"Only the rects without custom tiles. Click or F: every rect" : L"Every rect of the template. Click or F: only the rects without custom tiles";
        if (col == kColTiles && right) return palMine_ ? L"Only this terrain's groups: its pieces' rows and the rows its maps use. Click or G: all groups" : L"All groups. Click or G: only this terrain's groups";
        return std::wstring(colName(col)) + L". Right-click: move the column or hide it";
    }
    if (check_.contains(p) && checkHover_ >= 0) {
        const std::vector<CheckRow> rows = checkRows();
        if (checkHover_ < int(rows.size())) {
            const CheckRow& row = rows[size_t(checkHover_)];
            return L"rect " + std::to_wstring(row.group) + (row.more ? L"  \x00b7  " + std::to_wstring(row.more + 1) + L" variants" : L"") +
                   (visibleGroups_.count(row.group) || std_ >= 0 ? L"" : L"  \x00b7  not in the example");
        }
    }
    if (hover_ >= 0 && hover_ < int(t->t.pieces.size())) {
        const auto& q = t->t.pieces[size_t(hover_)];
        auto half = [](uint16_t v) { return isomrules::nullHalf(v) ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15); };
        return roleOf(q[0]) + L" (rect " + std::to_wstring(q[0]) + L") \x2192 " + half(q[1]) + L" " + half(q[2]);
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
