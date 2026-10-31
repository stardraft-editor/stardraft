// Tools > Cosmonarchy > Custom isom terrain editor (portable, core.inc; research/terrain-builder/STATUS.md). The mod's
// isometric terrain rules (stardraft/mods/terrain_rules.h): each mod terrain is a game border (its template: a stand-in
// terrain on a ground) with the mod's tiles in the template's rects. Left: the tileset's terrains (search; New terrain
// from a template, with the game's tiles or empty). Middle: the example, the terrain painted by the real brush on its
// bordering terrain (a NULL half: black, a red frame; a borrowed rect, the game's own tiles:
// a blue frame); under it the target rect large (the game's tiles beside yours) and the checklist: every rect of the
// template by its role (edge N, corner SE top...) with its state. Click a rect, a hole or a checklist row: it is the
// target. Right of the example (or left: the tiles' right-click menu): the tileset's tiles, a tile group a row (16
// subtiles), the column's width dragged at the edge between them; a click puts the tile into the target (2 tiles: both halves, the tile and the next in its row; 1 tile: the half), and a
// done rect hands the target to the next rect with no tile; Up / Down walk the checklist; the tiles scroll to the
// target's row. Ctrl+Z / Ctrl+Y undo
// and redo; Clear all makes every rect NULL; Holes get the game's tiles; Copy from takes another terrain's tiles by
// role; Draft marks a terrain as not checked yet. Load and Save the rules file (.isom); the ISOM terrain rules
// override (Program options > Plugins) uses such a file. Closing with unsaved changes asks Save, Do not save or Cancel.
// Terse texts: a tool for the mod's tile makers (not STE100).

const GameData* cmbwGameData(std::string& err);  // (core/cmbw.inc)
void cmbwApplyTerrainOverride();
bool cmbwIsTerrainOverride(const std::filesystem::path& f);   // (core/cmbw.inc: f is the ISOM terrain rules override in use)
bool cmbwReloadTerrainRules(const std::filesystem::path& f);  // (f is the override in use: the open map loads its rules again)
void shellShowIsomRulesWindow();  // (the host: the window, once)
void shellSetIsomRulesTitle(const std::wstring& title);  // (the host: the window's title)
void setMessage(const std::wstring& m);  // (the program's status bar)
inline void isomProgramMessage(const std::wstring& m) { setMessage(m); }  // (the view's own setMessage hides it)
void shellSetTerrainOverride(const std::filesystem::path& file);  // (the host: Program options > Plugins' override)
int shellContextMenuAt(draw::View& from, draw::Point at, const std::vector<std::wstring>& items);  // (the host)

namespace isomrules {

// A header's dropdown: Up, Down and the wheel do not step it while its list is closed (they are the editor's: the rects,
// the palette), so a pick leaves no trap for the keys.
struct HeaderPick final : stardraft::kit::Dropdown {
    HeaderPick() : Dropdown({}, -1) { wheelSteps = false; }
    bool key(const draw::KeyEvent& e) override {
        if (!listOpen() && (e.key == draw::Key::Up || e.key == draw::Key::Down) && !(e.mods & draw::kAlt)) return false;
        return Dropdown::key(e);
    }
};
// The menu bar: Up and Down go to the editor while no menu is open (the bar keeps the focus after Escape).
struct EditorMenuBar final : stardraft::kit::MenuBar {
    bool key(const draw::KeyEvent& e) override {
        if (openIndex() < 0 && (e.key == draw::Key::Up || e.key == draw::Key::Down) && !e.mods) return false;
        return MenuBar::key(e);
    }
};

using stardraft::terrainrules::Family;
using stardraft::terrainrules::PieceState;
using stardraft::terrainrules::TemplateRect;
using ModTerrain = stardraft::Tileset::ModTerrain;

// A label in title case: each word's first letter capital, but the small words inside ("edge S top 1": "Edge S Top 1").
inline std::wstring titleCase(std::wstring s) {
    static const wchar_t* kSmall[] = {L"a", L"an", L"and", L"as", L"at", L"for", L"in", L"of", L"on", L"or", L"the", L"to", L"vs"};
    for (size_t i = 0; i < s.size();) {
        size_t j = i;
        while (j < s.size() && s[j] != L' ') ++j;
        const std::wstring w = s.substr(i, j - i);
        const bool minor = i > 0 && std::any_of(std::begin(kSmall), std::end(kSmall), [&](const wchar_t* k) { return w == k; });
        if (!minor && j > i && s[i] >= L'a' && s[i] <= L'z') s[i] = wchar_t(s[i] - L'a' + L'A');
        i = j + 1;
    }
    return s;
}
// A tileset's name as the program's lists have it (Jungle World, Ash World).
inline std::wstring tilesetTitle(int e) {
    static const wchar_t* kNames[8] = {L"Badlands", L"Space Platform", L"Installation", L"Ash World", L"Jungle World", L"Desert World", L"Ice World", L"Twilight World"};
    return kNames[e & 7];
}
inline std::wstring familyName(Family f) { return titleCase(widen(stardraft::terrainrules::familyName(f))); }
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

// New Terrain: a small window with the name, the reference (the template: a game border or a terrain to copy) and the
// bordering terrain (each item: its tile, then its name). Enter: Create; Esc: Cancel.
class NewTerrainDialog final : public draw::View {
public:
    NewTerrainDialog(std::vector<stardraft::kit::DropdownItem> models, int model, std::vector<stardraft::kit::DropdownItem> grounds, int ground)
        : create_(L"Create", stardraft::kit::Tone::Primary, [this] { finish(true); }), cancel_(L"Cancel", stardraft::kit::Tone::Normal, [this] { finish(false); }) {
        model_.setItems(std::move(models)), model_.setValue(model);
        ground_.setItems(std::move(grounds)), ground_.setValue(ground);
        for (stardraft::kit::Dropdown* d : {&model_, &ground_})  // (long lists in a small window: 20 rows, past its edge)
            d->searchable = true, d->enterOpens = false, d->listRows = 20, d->listInWindow = false;
        model_.onChange = [this](int v) {
            if (onModel) onModel(v);
        };
        ground_.onChange = [this](int) { autoName(); };
        name_.onChange = [this] { named_ = true; };
        name_.onEnter = [this] { finish(true); };
        name_.onEscape = [this] { finish(false); };
        add(name_, {}), add(model_, {}), add(ground_, {}), add(cancel_, {}), add(create_, {});
        autoName();
    }
    // The template changed (index): the caller sets the bordering terrain to its ground (setGround).
    std::function<void(int)> onModel;
    void setGround(int i) { ground_.setValue(i), autoName(); }
    int model() const { return model_.value(); }
    int ground() const { return ground_.value(); }
    const std::wstring& name() const { return name_.text(); }
    bool created() const { return created_; }
    static draw::Point size() { return {scale(460), scale(200)}; }

    void resized() override {
        const int pad = scale(16), lw = scale(90), fh = scale(stardraft::kit::kFieldH), rowH = scale(36);
        for (int i = 0; i < 3; ++i) {
            const int y = pad + i * rowH;
            draw::View& f = i == 0 ? static_cast<draw::View&>(name_) : i == 1 ? static_cast<draw::View&>(model_) : ground_;
            f.setFrame({pad + lw, y, width() - pad, y + fh});
        }
        const int bw = scale(stardraft::kit::kButtonMinW), bh = scale(stardraft::kit::kButtonH), by = height() - pad - bh;
        cancel_.setFrame({width() - pad - 2 * bw - scale(8), by, width() - pad - bw - scale(8), by + bh});
        create_.setFrame({width() - pad - bw, by, width() - pad, by + bh});
    }
    void paint(draw::Canvas& c) override {
        if (!focused_ && host()) focused_ = true, name_.focus();  // (the window shows: the keys go to the name)
        const auto& th = stardraft::kit::theme();
        c.fill({0, 0, width(), height()}, th.bg);
        const int pad = scale(16), fh = scale(stardraft::kit::kFieldH), rowH = scale(36);
        const wchar_t* labels[] = {L"Name", L"Reference", L"Bordering"};
        for (int i = 0; i < 3; ++i) c.text({pad, pad + i * rowH, pad + scale(86), pad + i * rowH + fh}, labels[i], th.dim, draw::kMiddle);
    }
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || stardraft::kit::Dropdown::anyListOpen()) return false;
        if (e.key == draw::Key::Enter && !e.mods) return finish(true), true;
        if (e.key == draw::Key::Escape && !e.mods) return finish(false), true;
        return false;
    }

private:
    void autoName() {  // (the name follows the bordering terrain until it is typed)
        if (named_) return;
        const auto& g = ground_.items();
        const int i = ground_.value();
        name_.setText(L"Primary (" + (i >= 0 && i < int(g.size()) ? g[size_t(i)].label : std::wstring()) + L")");
    }
    void finish(bool create) {
        if (create && (model_.value() < 0 || ground_.value() < 0 || name_.text().empty())) return;
        created_ = create;
        if (draw::Host* h = host()) h->closeWindow(*this);
    }
    stardraft::kit::Dropdown model_{{}, -1}, ground_{{}, -1};
    stardraft::kit::TextField name_;
    stardraft::kit::Button create_, cancel_;
    bool named_ = false, created_ = false, focused_ = false;
};

}  // namespace isomrules

class IsomRulesView final : public draw::View {
public:
    ~IsomRulesView() { *alive_ = false; }
    IsomRulesView()
        : newTerrain_(L"New Terrain\x2026", stardraft::kit::Tone::Normal, [this] { newTerrain(); }) {
        std::vector<stardraft::kit::DropdownItem> eras;
        for (int e = 0; e < 8; ++e) eras.push_back({isomrules::tilesetTitle(e)});
        era_.setItems(eras), era_.setValue(0);
        era_.onChange = [this](int e) { openEra(e); };
        search_.setPlaceholder(L"Search (Name, Family, Draft)");
        search_.onChange = [this] { filter(); };
        sideBar_.onScroll = [this](int pos) { sideScroll_ = pos, invalidate(); };
        ground_.onChange = [this](int i) { setGround(i); };
        refPick_.searchable = true, refPick_.wheelSteps = false;
        refPick_.setTip(L"Reference border rendered in the same shape (Template: the terrain's own; any tileset's border). Ctrl+R: the overlay on or off");
        refPick_.onChange = [this](int) {
            if (!refOn_ && refOf(terrain()) >= 0) refOn_ = true, refShow_.setOn(true);  // (a border picked: the overlay shows it)
            rebuild();
        };
        refShow_.setLabel(L"Reference Overlay"), refShow_.setPlainLabel(true);
        refShow_.onChange = [this](bool) { toggleReference(); };
        refShow_.setTip(L"Render the reference border over the terrain canvas. Key: Ctrl+R");
        refOpacity_.setTip(L"Reference opacity; 100 % shows the reference alone. Keys: Ctrl+Shift+Up, Ctrl+Shift+Down");
        refOpacity_.onChange = [this](int) { invalidate(); };
        row_.searchable = true, row_.wheelSteps = false;
        row_.setTip(L"Scroll the palette to a group (number or terrain name). Ctrl+click a rect: its tile. Ctrl+M: next block of this terrain's groups");
        row_.onChange = [this](int g) { showRow(g, -1); };
        palBar_.onScroll = [this](int pos) { palScroll_ = pos, invalidate(); };
        pairPick_.setItems({{L"Tile Pair"}, {L"Single Tile"}}), pairPick_.setValue(0);
        pairPick_.onChange = [this](int v) { if ((v == 0) != wide_) toggleWidth(); };
        pairPick_.setTip(L"What a slot and a click in Tiles take: a tile pair (both halves: the tile and the next in its row) or a single tile (one half). Key: Ctrl+T");
        minePick_.setItems({{L"All Tile Groups"}, {L"This Terrain's Groups"}}), minePick_.setValue(0);
        minePick_.onChange = [this](int v) { if ((v == 1) != palMine_) togglePalMine(); };
        minePick_.setTip(L"The tile groups shown: all, or this terrain's (its pieces' rows and the rows its maps use). Key: Ctrl+G");
        for (draw::View* v : std::initializer_list<draw::View*>{&bar_, &era_, &row_, &refPick_, &refShow_, &refOpacity_, &search_,
                                                               &sideBar_, &newTerrain_, &ground_, &palBar_, &minePick_, &pairPick_})
            add(*v, {0, 0, 0, 0});
    }
    static draw::Point size() { return {scale(1500), scale(900)}; }

    // Off: the work is done at once, on this thread (a snapshot's run).
    void setAsync(bool on) { async_ = on; }
    // The New Terrain window as it opens, for a snapshot (--dialog isomnew:<terrain>); null: no templates.
    std::unique_ptr<isomrules::NewTerrainDialog> newDialogShot() {
        std::vector<int> modelOf;
        return makeNewDialog(modelOf);
    }
    // The default columns, widths and view options, whatever the layout settings hold (a snapshot's run). Not saved.
    void useDefaultLayout() {
        std::copy(std::begin(kDefaultOrder), std::end(kDefaultOrder), colOrder_);
        std::copy(std::begin(kDefaultW), std::end(kDefaultW), colW_);
        for (bool& s : colShown_) s = true;
        tileIds_ = 0, frames_ = true;
        if (laidOut_) layout();
    }
    // The program ends: the worker ends (after the job that runs).
    void shutdown() {
        ++buildGen_, ++openGen_;
        isomrules::worker().stop();
    }
    // Opens with the rules in use now (the built-in ones, the mod's file, the override) of the open map's tileset (it
    // loads on the worker: the window shows Loading until then).
    void open() {
        const AppState& app = shellState();
        title_.clear(), copyAsked_ = false;  // (a new window: its title is set again; the copy question once)
        loadColumns();  // (here, not when the view is made: the program's settings are not read yet then)
        if (int v = 0; prefs::number(L"Layout/IsomTileIds", v, 0, 2)) tileIds_ = v;
        if (int v = 0; prefs::number(L"Layout/IsomRectFrames", v, 0, 1)) frames_ = v != 0;
        if (int v = 0; prefs::number(L"Layout/IsomHeights", v, 0, 1)) heights_ = v != 0;
        if (!doc_.tilesets.empty()) return rebuild();
        if (std::wstring last; !gSettingsOff && prefs::text(L"Layout/IsomRulesFile", last) && !last.empty()) {  // (the file saved last)
            std::error_code ec;
            stardraft::terrainrules::Rules r;
            std::string err;
            if (const std::filesystem::path f(last); std::filesystem::exists(f, ec) && !modsOwn(f) && stardraft::terrainrules::load(f, r, err))
                doc_ = std::move(r), file_ = f, dirty_ = false;
        }
        openEra(app.ed && app.ed->isOpen() ? int(app.ed->era()) & 7 : 0);
    }
    bool dirty() const { return dirty_; }
    // Another tileset (a snapshot's isomrules:<era>/<name>).
    void showEra(int e) { openEra(e & 7); }
    // The snapshot's isomrules:<name>!remove (testRemoveStrip).
    std::string removeStripTest() { return testRemoveStrip(); }
    // The terrains of the list, one a line: name, family (a snapshot's isomrules:<era>/*).
    std::string terrainNames() {
        std::string out;
        for (int v : shown_)
            if (v >= 0) out += "isom terrain: " + rules().terrains[size_t(v)].t.name + " | " + narrow(isomrules::familyName(familyOfValue(v))) + "\n";
        return out;
    }
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
    void paintBody(draw::Canvas& c);
    void paintSide(draw::Canvas& c);
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || search_.hasFocus()) return false;
        const bool ctrl = e.mods == draw::kCtrl;  // (the window's letter keys: Ctrl+ only)
        if (!ctrl || (e.key != draw::Key::Left && e.key != draw::Key::Right && e.key != draw::Key::Up && e.key != draw::Key::Down)) nudgeSlot_ = -1;
        if (e.key == static_cast<draw::Key>('L') && ctrl) return nextNull(), true;
        if (e.key == draw::Key::Delete && !e.mods) return setNull(), true;
        if (e.key == draw::Key::F2 && !e.mods) return renameTerrain(sel_), true;
        if (e.key == draw::Key::Escape && !e.mods && (brush_ >= 0 || slotKeys_ || target_ >= 0)) return deselectAll(), true;
        if (slotKeys_ && target_ >= 0 && ctrl) {  // (the clicked slot: Ctrl+arrows step its tile through the tiles)
            if (e.key == draw::Key::Left || e.key == draw::Key::Right) return nudge(e.key == draw::Key::Right ? 1 : -1, 0), true;
            if (e.key == draw::Key::Up || e.key == draw::Key::Down) return nudge(0, e.key == draw::Key::Down ? 1 : -1), true;
        }
        if (e.key == static_cast<draw::Key>('R') && ctrl) return toggleReference(), true;
        if (e.key == static_cast<draw::Key>('H') && ctrl) return toggleFrames(), true;
        if (e.key == static_cast<draw::Key>('E') && ctrl) return toggleHeights(), true;
        if ((e.key == draw::Key::RightBracket || e.key == draw::Key::LeftBracket) && ctrl) return stepVariant(e.key == draw::Key::RightBracket ? 1 : -1), true;
        if (e.key == static_cast<draw::Key>('K') && ctrl) return nextDifferingVariant(), true;
        if ((e.key == draw::Key::Up || e.key == draw::Key::Down) && e.mods == (draw::kCtrl | draw::kShift)) return stepOpacity(e.key == draw::Key::Up ? 1 : -1), true;
        if (e.key == static_cast<draw::Key>('M') && ctrl) return nextMyRows(), true;
        if (e.key == static_cast<draw::Key>('D') && ctrl) return toggleDraft(), true;
        if (e.key == static_cast<draw::Key>('G') && ctrl) return togglePalMine(), true;
        if (e.key == static_cast<draw::Key>('T') && ctrl) return toggleWidth(), true;
        if ((e.key == draw::Key::Up || e.key == draw::Key::Down) && !e.mods) return stepTarget(e.key == draw::Key::Down ? 1 : -1), true;
        if (e.key == static_cast<draw::Key>('S') && (e.mods & draw::kCtrl)) return save((e.mods & draw::kShift) != 0), true;
        if (e.key == static_cast<draw::Key>('N') && (e.mods & draw::kCtrl)) return newTerrain(), true;
        if (e.key == static_cast<draw::Key>('O') && (e.mods & draw::kCtrl)) return loadFile(), true;
        if (e.key == static_cast<draw::Key>('Z') && (e.mods & draw::kCtrl)) return undo(e.mods & draw::kShift), true;
        if (e.key == static_cast<draw::Key>('Y') && (e.mods & draw::kCtrl)) return undo(true), true;
        return false;
    }
    std::wstring tip(draw::Point p) override;
    draw::Cursor cursor(draw::Point p) override { return dragEdge_ >= 0 || edgeAt(p) >= 0 ? draw::Cursor::SizeWE : draw::Cursor::Arrow; }

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
               (t->t.draft ? ", draft" : ", verified") + (target_ >= 0 ? ", target piece " + std::to_string(target_) : "") + ", " +
               std::to_string(strays_) + " strays" + coverage(t->t);
    }
    // (the snapshot's line, more: the variants of the set's and the gaps' rects with a place of their own, the fill
    // pieces with a place, and the rects not in the examples)
    std::string coverage(const isomrules::ModTerrain& m) const {
        std::set<int> placed;
        for (const Place& p : places_) placed.insert(p.piece);
        // (a piece with no place whose rect's game group is no rect's left tile in StarEdit's tables (an odd group, as a
        // fill's right half): the brush's tiles never take it; only its seam pass can (isom.cpp putPieces: the pieces
        // of the even group's rects), when the group before it has rects. Counted apart)
        const stardraft::IsomTiles* tiles = stardraft::IsomTiles::forTileset(tileset_);
        auto brushPlaces = [&](uint16_t g) { return !tiles || !tiles->rectsOf(g).empty(); };
        int variants = 0, variantsPlaced = 0, fills = 0, fillsPlaced = 0, never = 0, seamsOnly = 0;
        for (size_t k = 0; k < m.pieces.size(); ++k) {
            const bool fill = k >= m.setPieces && k + m.gapPieces < m.pieces.size();
            if (!placed.count(int(k)) && !brushPlaces(m.pieces[k][0])) {
                ++(brushPlaces(uint16_t(m.pieces[k][0] & ~1u)) ? seamsOnly : never);
                continue;
            }
            (fill ? fills : variants) += 1;
            if (placed.count(int(k))) (fill ? fillsPlaced : variantsPlaced) += 1;
        }
        std::map<uint16_t, int> fillGroups;  // (the fill's rects and their pieces)
        for (size_t k = m.setPieces; k + m.gapPieces < m.pieces.size(); ++k) ++fillGroups[m.pieces[k][0]];
        std::string out = "; variants with a place " + std::to_string(variantsPlaced) + " of " + std::to_string(variants) + ", fill pieces with a place " +
                          std::to_string(fillsPlaced) + " of " + std::to_string(fills) + ", " + std::to_string(seamsOnly) +
                          " pieces the brush places only at seams, " + std::to_string(never) + " pieces the brush never places; fill rects:";
        for (const auto& [g, n] : fillGroups) {
            if (!brushPlaces(g)) continue;  // (counted apart)
            int at = 0;  // (its pieces with a place)
            for (size_t k = m.setPieces; k + m.gapPieces < m.pieces.size(); ++k) at += m.pieces[k][0] == g && placed.count(int(k));
            int places = 0;  // (its places)
            for (const Place& p : places_) places += p.piece >= 0 && size_t(p.piece) < m.pieces.size() && m.pieces[size_t(p.piece)][0] == g;
            out += " " + std::to_string(g) + "x" + std::to_string(n) + " (" + std::to_string(at) + " with a place, " + std::to_string(places) + " places)";
        }
        out += "; rects with fewer places than variants:";
        {
            std::map<uint16_t, int> variantsOf, placesOf;
            for (size_t k = 0; k < m.pieces.size(); ++k)
                if (k < m.setPieces || k + m.gapPieces >= m.pieces.size()) ++variantsOf[m.pieces[k][0]];
            for (const Place& p : places_)
                if (p.piece >= 0 && size_t(p.piece) < m.pieces.size()) ++placesOf[m.pieces[size_t(p.piece)][0]];
            for (const auto& [g, n] : variantsOf)
                if (placesOf[g] > 0 && placesOf[g] < n) out += " " + std::to_string(g) + "(" + std::to_string(placesOf[g]) + "/" + std::to_string(n) + ")";
        }
        out += "; variants with no place:";
        for (size_t k = 0; k < m.pieces.size(); ++k) {
            if (!(k < m.setPieces || k + m.gapPieces >= m.pieces.size()) || placed.count(int(k)) || isomrules::anyNull(m.pieces[k]) || !brushPlaces(m.pieces[k][0])) continue;
            bool led = false;  // (a piece of the set whose tiles one row down are this one's)
            for (size_t i = 0; i < m.pieces.size() && !led; ++i) led = i != k && continues(m.pieces[i], m.pieces[k]);
            out += " " + std::to_string(k) + "(rect " + std::to_string(m.pieces[k][0]) + (led ? ", led" : ", top") + ")";
        }
        out += "; rects not in them:";
        for (int k : absent_)
            if (k >= 0 && size_t(k) < m.pieces.size()) out += " " + std::to_string(m.pieces[size_t(k)][0]) + (isomrules::anyNull(m.pieces[size_t(k)]) ? "(null)" : "");
        return out;
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
        busySince_ = std::chrono::steady_clock::now(), armBusy();
        undo_.clear(), redo_.clear(), endStroke(), brush_ = -1, slotKeys_ = false, pins_.clear(), savedPins_.clear();
        shown_.clear(), sideRows_.clear(), familyCache_.clear(), sel_ = -1, target_ = -1, places_.clear(), holes_.clear(), absent_.clear(), strays_ = 0, strayTiles_.clear(), picture_.reset();
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
        const std::map<uint16_t, std::wstring> names = fillGrounds();  // (a terrain id -> its name: the rows' terrains)
        // (the rows: each group of the tileset, by its number and the terrain its tiles are)
        std::vector<std::wstring> of(tileset_.groups.size());
        for (const auto& [g, tr, other] : tileset_.groupTerrains)
            if (g < of.size() && names.count(tr))
                of[g] = names.at(tr) + (other && names.count(other) ? L" / " + names.at(other) : L"");
        std::vector<stardraft::kit::DropdownItem> rows;
        for (size_t g = 0; g < tileset_.groups.size(); ++g) rows.push_back({std::to_wstring(g) + (of[g].empty() ? L"" : L"  " + of[g])});
        row_.setItems(rows), row_.setValue(-1);
        rowNames_ = of, rowPics_.clear();
        stdPairs_.clear(), std_ = -1;
        stdPairs_ = bordersOf(era());
        refPick_.setValue(0), fillRefs();
        pal_.clear(), chips_.clear(), palScroll_ = 0, jump_ = -1;
        sel_ = -1;
        filter();
        if (!shown_.empty()) selectValue(shown_[0]);
        loadRefTilesets();  // (after the first example's job: it does not wait for them)
        invalidate();
        if (file_.empty() && !copyAsked_ && !gSettingsOff && host()) {  // (no rules file: the rules in use are the mod's; a copy to change)
            copyAsked_ = true;
            std::shared_ptr<bool> alive = alive_;
            host()->timer(1, [this, alive] {
                if (!*alive || !file_.empty()) return;
                if (msgbox::ask(*this, L"Custom isom terrain editor",
                                L"No terrain rules file (.isom) is open: these are the rules in use. Make a local copy to modify?",
                                {L"&Make Copy\x2026", L"&Not Now"}, msgbox::Icon::Question) == 0)
                    save(true);
            });
        }
    }
    bool copyAsked_ = false;  // (the local copy question: once a window)
    // The Bordering list: the game's terrains, then the rules'; the shown terrain's ground picked. Its names by id.
    std::map<uint16_t, std::wstring> fillGrounds() {
        groundIds_.clear();
        std::vector<stardraft::kit::DropdownItem> grounds;
        std::map<uint16_t, std::wstring> names;
        if (const IsomTerrainTable* game = IsomTerrainTable::forEra(uint16_t(era())))
            for (uint16_t i = 1; i <= game->terrainCount(); ++i)
                if (game->isTerrain(i)) groundIds_.push_back(i), grounds.push_back({widen(game->terrainName(i))}), names[i] = widen(game->terrainName(i));
        for (const auto& x : rules().terrains)
            if (!x.t.variantOf) groundIds_.push_back(x.t.id), grounds.push_back({widen(x.t.name)}), names[x.t.id] = widen(x.t.name);
        ground_.setItems(grounds), ground_.setValue(-1);
        if (const auto* t = terrain())
            for (size_t k = 0; k < groundIds_.size(); ++k)
                if (groundIds_[k] == shownGround(*t)) ground_.setValue(int(k));
        return names;
    }
    // Rename: the terrain's name (an undo step of the terrain; the Bordering list too).
    // A terrain's variants (the brush's: its pieces on another ground that looks the same inside, made when the tileset
    // loads; the rules file does not keep them). The list shows each under its terrain (a value kVariantRow - its id);
    // its row shows the terrain on the variant's ground: the pieces are the terrain's, its edits the terrain's.
    static constexpr int kVariantRow = -2000000;
    uint16_t variantView_ = 0;  // (the variant shown: its id; 0: none)
    static bool isVariantRow(int v) { return v <= kVariantRow; }
    // The Bordering box's ground of terrain t: the variant's while one is shown.
    uint16_t shownGround(const stardraft::terrainrules::Terrain& t) const {
        const isomrules::ModTerrain* v = variantView_ ? tileset_.modTerrain(variantView_) : nullptr;
        return v ? v->ground : t.t.ground;
    }
    const isomrules::ModTerrain* variantOfRow(int v) const { return isVariantRow(v) ? tileset_.modTerrain(uint16_t(kVariantRow - v)) : nullptr; }
    int indexOfId(uint16_t id) const {
        for (size_t i = 0; i < rules().terrains.size(); ++i)
            if (rules().terrains[i].t.id == id) return int(i);
        return -1;
    }
    void renameTerrain(int i) {
        if (loading_ || i < 0 || i >= int(rules().terrains.size())) return;
        if (i != sel_) selectValue(i);
        const std::wstring was = widen(rules().terrains[size_t(i)].t.name);
        std::wstring name = was;
        if (!shellPromptText(L"Terrain name", name)) return;
        name.erase(0, name.find_first_not_of(L' ')), name.erase(name.find_last_not_of(L' ') + 1);  // (no spaces around it)
        if (name.empty() || name == was) return;
        if (!remember()) return;
        rules().terrains[size_t(i)].t.name = narrow(name);
        dirty_ = true;
        filter(), fillGrounds(), invalidate();
        setMessage(L"Renamed " + was + L" to " + name);
    }
    // Delete: the terrain goes from the rules (not while another terrain borders on it). No undo: it asks first.
    void deleteTerrain(int i) {
        if (loading_ || i < 0 || i >= int(rules().terrains.size())) return;
        auto& ts = rules();
        const uint16_t id = ts.terrains[size_t(i)].t.id;
        const std::wstring name = widen(ts.terrains[size_t(i)].t.name);
        if (ts.terrains[size_t(i)].t.locked) return setMessage(name + L" is protected. To delete it, right-click it in the list > Unprotect first");
        std::wstring users;
        for (const auto& o : ts.terrains)
            if (o.t.id != id && !o.t.variantOf && (o.t.ground == id || o.t.groundTemplate == id)) users += (users.empty() ? L"" : L", ") + widen(o.t.name);
        if (!users.empty())
            return shellErrorBox(name + L" is the bordering terrain of " + users + L". Give them another bordering terrain first.", L"Delete terrain");
        if (msgbox::ask(*this, L"Delete terrain", L"Delete " + name + L"? Maps that use it keep their tiles, but the brush no longer has it. Undo does not bring it back.",
                        {L"&Delete", L"Cancel"}, msgbox::Icon::Warning) != 0)
            return;
        ts.terrains.erase(std::remove_if(ts.terrains.begin(), ts.terrains.end(), [id](const auto& x) { return x.t.id == id || x.t.variantOf == id; }), ts.terrains.end());
        auto gone = [id](const Edit& e) { return e.id == id || e.before.t.ground == id || e.before.t.groundTemplate == id; };  // (an undo: no link to it again)
        undo_.erase(std::remove_if(undo_.begin(), undo_.end(), gone), undo_.end());
        redo_.erase(std::remove_if(redo_.begin(), redo_.end(), gone), redo_.end());
        moreRects_.erase(id), savedPins_.erase(id), familyCache_.clear();
        dirty_ = true;
        sel_ = -1, target_ = -1, pins_.clear(), shownId_ = -1;  // (the next terrain may get its index: its example is new)
        filter(), fillGrounds();
        if (!shown_.empty()) selectValue(shown_[0]);
        else rebuild();
        setMessage(L"Deleted " + name);
    }
    void filter() {
        shown_.clear();
        std::wstring q = search_.text();
        std::transform(q.begin(), q.end(), q.begin(), ::towlower);
        const auto& ts = rules();
        std::vector<bool> match(ts.terrains.size());
        for (size_t i = 0; i < ts.terrains.size(); ++i) {
            std::wstring n = widen(ts.terrains[i].t.name);
            if (!q.empty() && !tileset_.groups.empty()) {  // (the label too: "draft", a family, "36/36")
                const auto [done, total] = stardraft::terrainrules::completeness(tileset_, ts.terrains[i].t);
                const auto [a, b] = stardraft::terrainrules::templateOf(tileset_, ts.terrains[i].t);
                n += L" " + isomrules::familyName(stardraft::terrainrules::familyOf(stardraft::terrainrules::templateRects(tileset_, a, b), tileset_)) + L" " +
                     std::to_wstring(done) + L"/" + std::to_wstring(total) + (ts.terrains[i].t.draft ? L" Draft" : L" Verified");
            }
            std::transform(n.begin(), n.end(), n.begin(), ::towlower);
            match[i] = q.empty() || n.find(q) != std::wstring::npos;
        }
        // (each terrain, then its variants: shown when it or they match)
        for (size_t i = 0; i < ts.terrains.size(); ++i) {
            if (ts.terrains[i].t.variantOf) continue;
            std::vector<int> vars;
            for (const isomrules::ModTerrain& v : tileset_.modTerrains)
                if (v.variantOf && v.variantOf == ts.terrains[i].t.id) {
                    std::wstring n = widen(v.name);
                    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
                    if (match[i] || n.find(q) != std::wstring::npos) vars.push_back(kVariantRow - int(v.id));
                }
            if (!match[i] && vars.empty()) continue;
            shown_.push_back(int(i));
            shown_.insert(shown_.end(), vars.begin(), vars.end());
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
        static const isomrules::Family kOrder[] = {isomrules::Family::Cliff, isomrules::Family::Wall, isomrules::Family::Field, isomrules::Family::HardEdge,
                                                   isomrules::Family::Patch, isomrules::Family::Other};
        int y = 0;
        auto header = [&](const std::wstring& text, int n) {
            sideRows_.push_back({kSideHeader, text + L"  \x00b7  " + std::to_wstring(n), y, scale(24)}), y += scale(24);
        };
        for (isomrules::Family f : kOrder) {
            std::vector<int> in;
            for (int v : shown_)
                if (const isomrules::ModTerrain* m = variantOfRow(v)) {
                    if (const int p = indexOfId(m->variantOf); p >= 0 && familyOfValue(p) == f) in.push_back(v);
                } else if (v >= 0 && familyOfValue(v) == f) in.push_back(v);
            if (in.empty()) continue;
            header(isomrules::familyName(f), int(std::count_if(in.begin(), in.end(), [](int v) { return v >= 0; })));
            for (int v : in) sideRows_.push_back({v, {}, y, scale(44)}), y += scale(44);
        }
        std::vector<int> games;
        for (int v : shown_)
            if (v < 0 && !isVariantRow(v)) games.push_back(v);
        if (!games.empty()) {
            header(L"Game Tilesets", int(games.size()));
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
    // A terrain id's tile, for its picture: a game terrain's solid tile, or a terrain of the rules' swatch.
    uint16_t terrainTile(uint16_t id) const {
        for (size_t i = 0; i < rules().terrains.size(); ++i)
            if (rules().terrains[i].t.id == id) return swatchOf(int(i));
        const int g = tileset_.groups.empty() ? -1 : stardraft::solidTileGroup(tileset_, id);
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
        keepPins();
        variantView_ = 0;
        if (const isomrules::ModTerrain* m = variantOfRow(v)) {
            if (const int p = indexOfId(m->variantOf); p >= 0) v = p, variantView_ = m->id;
            else return;
        }
        sel_ = v >= 0 ? v : -1, std_ = v < 0 && v > -1000000 ? -1 - v : -1;
        revealSideRow();
        target_ = -1, pins_.clear();
        if (const auto* t = terrain()) pins_ = savedPins_[t->t.id];  // (its pins as it had them)
        updateMyRows();
        updateTemplate();
        if (const auto* t = terrain()) {
            const isomrules::ModTerrain* var = variantView_ ? tileset_.modTerrain(variantView_) : nullptr;
            for (size_t k = 0; k < groundIds_.size(); ++k)
                if (groundIds_[k] == (var ? var->ground : t->t.ground)) ground_.setValue(int(k));
            if (var) setMessage(widen(var->name) + L": the tiles of " + widen(t->t.name) + L" on " + widen(tileset_.modTerrain(var->ground) ? tileset_.modTerrain(var->ground)->name : std::string("its ground")) +
                                L". Edits change " + widen(t->t.name));
        }
        rebuild();
    }
    void setGround(int i) {
        auto* t = sel_ >= 0 ? &rules().terrains[size_t(sel_)] : nullptr;
        if (!t || i < 0 || i >= int(groundIds_.size()) || groundIds_[size_t(i)] == t->t.id) return;
        if (const isomrules::ModTerrain* v = variantView_ ? tileset_.modTerrain(variantView_) : nullptr) {  // (a variant's ground is the brush's)
            for (size_t k = 0; k < groundIds_.size(); ++k)
                if (groundIds_[k] == v->ground) ground_.setValue(int(k));
            return setMessage(widen(v->name) + L": a variant's bordering terrain is the brush's. Select " + widen(t->t.name) + L" to change its own");
        }
        const uint16_t g = groundIds_[size_t(i)];
        if (!remember()) {  // (the box shows its ground again)
            for (size_t k = 0; k < groundIds_.size(); ++k)
                if (groundIds_[k] == t->t.ground) ground_.setValue(int(k));
            return;
        }
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
        std::set<std::pair<int, int>> hidden;    // (the strips taken off the canvas: hiddenOf)
        std::pair<int, int> removeAt{INT_MIN, INT_MIN};  // (Remove From Canvas: the place's tile; the build finds its strip only)
        bool extend = false;                     // (search for more spots that show the rects the shape does not: a terrain's first example)
        uint16_t cacheId = 0;                    // (moreRects_'s key: the terrain's id, a variant's id)
        std::wstring extraKey;                   // (what the search's spots depend on: extraKeyOf)
        std::vector<isomrules::ModTerrain> all;  // (the working rules: the terrain's pieces as the stand-in values)
        std::vector<uint16_t> free;              // (the stand-in values: piece k's halves are 2k and 2k + 1)
        std::map<std::pair<int, int>, std::pair<uint16_t, int>> pins;  // (pins_)
        const GameData* data = nullptr;
        bool standard = false;  // (a game border: stdTerrain on root, no pieces)
        uint16_t stdTerrain = 0, refGround = 0, refTerrain = 0;  // (refTerrain 0: no reference)
        uint8_t refEra = 0;  // (the reference's tileset)
        size_t gameGroups = 0, refGroups = 0;  // (the game's own tile groups of the tileset, of the reference's; 0: all)
    };
    struct BuildOut {
        std::vector<Place> places;
        std::vector<int> holes, absent;
        std::vector<std::pair<int, int>> extra;  // (the extra spots, after a search)
        std::optional<std::pair<int, int>> removeSpot;  // (Remove From Canvas: the strip that paints the place; none: no strip does)
        int gained = 0;                          // (the search: rects shown that were not)
        int strays = 0;                          // (the strays the canvas shows: the snapshot line)
        std::vector<std::pair<int, int>> strayTiles;  // (their tiles: STARDRAFT_SHOW_STRAYS frames them)
        draw::Rect crop;
        std::vector<uint16_t> ids;  // (the crop's tiles, row by row; kNullId: a NULL half)
        std::unique_ptr<draw::Bitmap> picture, reference;
    };
    static constexpr uint16_t kNullId = 0xFFFF;
    static constexpr int kHalfSpot = 10000;  // (a spot of the example's shape painted with the 0.5 brush: i + kHalfSpot, on the grid of 32 x 16)
    static constexpr int kTopSpot = 20000;   // (the cliff tops' spot: i + kTopSpot, a diamond of kTopSize, on the grid of 64 x 32)
    static constexpr int kTopSize = 5;       // (a diamond of n reaches n x 32 pixels up and down, n x 64 to the sides; inside: about 45 places of a fill rect, for 26 variants)
    static constexpr int kThinRow = 34;  // (the thin shapes' row: half spots 34 x 16 pixels under the middle, 3 up or down, and diamonds on its middle; 4 free tile rows under the shape's lowest spot, so the two do not join)
    static constexpr int kTopRow = 29;   // (the cliff tops' row: its diamond 29 x 32 pixels under the middle; its top 2 tile rows under the thin shapes' walls, so the two do not join)
    // A spot's kind (0: the 1 brush, 1: the 0.5 brush, 2: the cliff tops' diamond) and its middle, from the map's middle.
    static int spotKind(int i) { return i >= (kHalfSpot + kTopSpot) / 2 ? 2 : i >= kHalfSpot / 2 ? 1 : 0; }
    static std::pair<int, int> spotAt(std::pair<int, int> a) {
        const int k = spotKind(a.first);
        return k == 2 ? std::make_pair((a.first - kTopSpot) * 64, a.second * 32) : k == 1 ? std::make_pair((a.first - kHalfSpot) * 32, a.second * 16)
                                                                                         : std::make_pair(a.first * 64, a.second * 32);
    }
    static Brush spotBrush(int i) { return Brush{Brush::Diamond, spotKind(i) == 2 ? float(kTopSize) : spotKind(i) == 1 ? 0.5f : 1.0f}; }
    void rebuild() {
        const auto* t = terrain();
        std::string err;
        const GameData* data = (t || std_ >= 0) && !loading_ ? cmbwGameData(err) : nullptr;
        auto in = std::make_shared<BuildIn>();
        in->gen = ++buildGen_;
        if ((!t && std_ < 0) || !data) {
            building_ = false, places_.clear(), holes_.clear(), absent_.clear(), strays_ = 0, strayTiles_.clear(), picture_.reset(), refPicture_.reset();
            return invalidate();
        }
        in->era = era(), in->data = data, in->gameGroups = gameGroups(era());
        in->refEra = era();
        if (const int ref = refOf(t); ref >= 0 && refOn_)
            in->refEra = uint8_t(refEntries_[size_t(ref)].era), in->refGround = refEntries_[size_t(ref)].ground, in->refTerrain = refEntries_[size_t(ref)].terrain;
        in->refGroups = gameGroups(in->refEra);
        if (!t) {  // (a standard terrain: the game's border, read only)
            in->standard = true, in->root = stdPairs_[size_t(std_)].first, in->stdTerrain = stdPairs_[size_t(std_)].second;
            return start(in, -2 - std_);
        }
        stardraft::terrainrules::Terrain on = *t;  // (a variant shown: the terrain on the variant's ground)
        const isomrules::ModTerrain* var = variantView_ ? tileset_.modTerrain(variantView_) : nullptr;
        if (var) on.t.ground = var->ground, on.t.groundTemplate = var->groundTemplate;
        const uint16_t cacheId = var ? var->id : t->t.id;  // (the extra spots: the variant's own)
        in->t = on, in->family = family_, in->cacheId = cacheId;
        in->extraKey = extraKeyOf(on);
        in->hidden = hiddenOf(in->extraKey);
        in->removeAt = removeAt_, removeAt_ = {INT_MIN, INT_MIN};
        if (!moreRects_.count(cacheId))  // (another terrain of the same template and rects, now or in an earlier run: its spots)
            if (const auto known = knownExtra(in->extraKey)) moreRects_[cacheId] = *known;
        if (const auto it = moreRects_.find(cacheId); it != moreRects_.end()) in->extra = it->second;
        in->extend = !moreRects_.count(cacheId);  // (a terrain's first example: the search before it shows, one picture)
        in->pins = pins_;
        uint16_t root = on.t.ground;  // (a game terrain under its grounds)
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
            building_ = false, places_.clear(), holes_.clear(), absent_.clear(), strays_ = 0, strayTiles_.clear(), picture_.reset();
            return invalidate();
        }
        for (const auto& x : rules().terrains) {
            in->all.push_back(x.t);
            if (x.t.id == t->t.id) in->all.back().ground = on.t.ground, in->all.back().groundTemplate = on.t.groundTemplate;
            if (x.t.id == t->t.id)
                for (size_t k = 0; k < in->all.back().pieces.size(); ++k)
                    in->all.back().pieces[k][1] = in->free[2 * k], in->all.back().pieces[k][2] = in->free[2 * k + 1];
        }
        start(in, var ? kVariantRow - int(var->id) : sel_);
    }
    void start(std::shared_ptr<BuildIn> in, int shown) {
        if (shownId_ != shown) viewZoom_ = 1, panX_ = panY_ = 0;  // (another terrain: the fit)
        if (shownId_ != shown)
            places_.clear(), holes_.clear(), absent_.clear(), strays_ = 0, strayTiles_.clear(), picture_.reset(), refPicture_.reset(), visibleGroups_.clear(), shownId_ = shown;
        if (!building_) busySince_ = std::chrono::steady_clock::now();
        building_ = true;
        progress(kStepPaint);
        armBusy();
        invalidate();
        background([this, in] {
            if (in->gen != buildGen_) return;  // (a newer one comes after it)
            auto out = std::make_shared<BuildOut>();
            build(*in, *out);
            foreground([this, in, out] {
                if (in->gen != buildGen_) return;
                building_ = false;
                shapeKey_ = in->extraKey;
                if (in->extend) moreRects_[in->cacheId] = out->extra, keepExtra(in->extraKey, out->extra);
                if (in->removeAt.first != INT_MIN) {  // (Remove From Canvas: the strip found; the canvas again without it)
                    if (!out->removeSpot) {
                        invalidate();
                        return setMessage(L"Remove From Canvas: this piece is part of the canvas's shape; only the pieces of the small extra strips can go");
                    }
                    auto hidden = hiddenOf(in->extraKey);
                    hidden.insert(*out->removeSpot);
                    keepHidden(in->extraKey, hidden);
                    target_ = -1;
                    rebuild();
                    return setMessage(L"Removed the piece from the canvas (right-click > Restore Removed Pieces brings back all " + std::to_wstring(hidden.size()) +
                                      (hidden.size() == 1 ? L" piece)" : L" pieces)"));
                }
                places_ = std::move(out->places), holes_ = std::move(out->holes), absent_ = std::move(out->absent), crop_ = out->crop, ++placesRev_;
                strays_ = out->strays, strayTiles_ = std::move(out->strayTiles);
                ids_ = std::move(out->ids), picIds_ = ids_, idHover_ = -1;  // (a new example: the hover tag waits for the mouse)
                visibleGroups_.clear();
                if (const auto* t = terrain())
                    for (const Place& p : places_)
                        if (p.piece >= 0 && p.piece < int(t->t.pieces.size())) visibleGroups_.insert(t->t.pieces[size_t(p.piece)][0]);
                picture_ = std::move(out->picture), refPicture_ = std::move(out->reference);
                invalidate();
            });
        });
    }
    // (on the worker: puzzleEd_ is its own)
    void build(const BuildIn& in, BuildOut& out) {
        const auto* t = &in.t;
        std::string err;
        if (!puzzleEd_) puzzleEd_ = std::make_unique<Editor>(*in.data), puzzleEd_->setUndoLimit(1);  // (painted again and again: no undo history)
        Editor* ed = puzzleEd_.get();
        const uint16_t root = in.root;
        constexpr int W = 40, H = 54;  // (the shape at the middle (cy); under it, the row of the thin shapes (kThinRow), then the cliff tops (kTopRow))
        if (!ed->isOpen() || ed->era() != in.era) {
            if (!ed->newMap(W, H, in.era, root, err)) return;
        }
        const int cx = W * 16, cy = 15 * 32;  // (the middle: room for the shape above it, the rows under it)
        // (a game terrain on its ground in a shape; the map's tiles of a part)
        auto paintGame = [&](Editor* on, uint16_t ground, uint16_t terrain, const std::vector<std::pair<int, int>>& shape) {
            on->paintBrush(cx, cy, ground, Brush{Brush::Diamond, 64.0f});
            for (const auto& sp : shape) {  // (as the example: each spot with its brush)
                const auto [x, y] = spotAt(sp);
                on->paintBrush(cx + x, cy + y, terrain, spotBrush(sp.first));
            }
        };
        auto pictureOf = [&](Editor* on, const draw::Rect& crop) {
            const stardraft::TerrainLayer l = stardraft::readTerrain(on->chk(), "TILE");
            std::vector<uint16_t> part;
            for (int y = crop.top; y < crop.bottom; ++y)
                for (int x = crop.left; x < crop.right; ++x) part.push_back(l.tiles[size_t(y) * W + x]);
            return isomrules::tilesBitmap(on->tileset(), part, crop.width(), crop.height());
        };
        // (a game tileset's map: the game's own tile groups only (the mod's groups past them have the same edge codes:
        // the brush would put them in); its own map of the same size: the shape falls on the same tiles)
        auto gameMap = [&](uint8_t e, uint16_t ground, size_t groups) -> Editor* {
            if (!gameEd_) gameEd_ = std::make_unique<Editor>(*in.data);
            Editor* g = gameEd_.get();
            if (!g->isOpen() || g->era() != e || gameEdGroups_ != groups) {
                g->limitGroups(groups);
                if (!g->newMap(W, H, e, ground, err)) return nullptr;
                g->useModTerrains({});
                gameEdGroups_ = groups;
            }
            return g;
        };
        auto addReference = [&](const std::vector<std::pair<int, int>>& shape) {
            if (!in.refTerrain || !out.picture) return;
            progress(kStepReference);
            Editor* on = gameMap(in.refEra, in.refGround, in.refGroups);
            if (!on) return;
            paintGame(on, in.refGround, in.refTerrain, shape);
            out.reference = pictureOf(on, out.crop);
            if (out.reference && (out.reference->width() != out.picture->width() || out.reference->height() != out.picture->height())) out.reference.reset();
        };
        if (in.standard) {  // (the game's border: the rects whose ISOM differs from its ground alone)
            Editor* const ed = gameMap(in.era, root, in.gameGroups);
            if (!ed) return;
            ed->paintBrush(cx, cy, root, Brush{Brush::Diamond, 64.0f});
            const std::vector<uint8_t> before = ed->chk().find("ISOM") ? ed->chk().find("ISOM")->data : std::vector<uint8_t>();
            paintGame(ed, root, in.stdTerrain, shapeOf(isomrules::Family::Other));
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
            out.picture = pictureOf(ed, out.crop);
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
        std::erase_if(shape, [&](const std::pair<int, int>& sp) { return spotKind(sp.first) == 1 && in.hidden.count(sp) > 0; });  // (the strips taken off)
        // (the bordering terrain all around: no other terrain in the picture; the shape, and one spot more when `more`)
        auto paintOn = [&](Editor* e, const std::pair<int, int>* more, size_t without = SIZE_MAX) {  // (`without`: a spot of the shape left out)
            std::srand(1);  // (the terrain core picks rare tiles with rand, each thread its own: the same shape paints the same, on any thread, after any build)
            e->paintBrush(cx, cy, root, Brush{Brush::Diamond, 64.0f});
            e->paintBrush(cx, cy, t->t.ground ? t->t.ground : 1, Brush{Brush::Diamond, 56.0f});
            auto spot = [&](std::pair<int, int> sp) {
                const auto [x, y] = spotAt(sp);
                e->paintBrush(cx + x, cy + y, t->t.id, spotBrush(sp.first));
            };
            for (size_t k = 0; k < shape.size(); ++k)
                if (k != without) spot(shape[k]);
            if (more) spot(*more);
        };
        auto paint = [&]() { paintOn(ed, nullptr); };
        // (the rects of the terrain (its set's and its gaps', by game group) that the painted map shows)
        auto shownGroups = [&]() {
            const stardraft::TerrainLayer l = stardraft::readTerrain(ed->chk(), "TILE");
            std::set<uint16_t> g;
            for (uint16_t v : l.tiles)
                if (const auto it = index.find(v); it != index.end() && !(it->second & 1)) g.insert(t->t.pieces[it->second / 2][0]);
            return g;
        };
        paint();
        // (how many places of each rect the painted map shows)
        auto countsOn = [&](const Editor* e) {
            const stardraft::TerrainLayer l = stardraft::readTerrain(e->chk(), "TILE");
            std::map<uint16_t, int> n;
            for (uint16_t v : l.tiles)
                if (const auto it = index.find(v); it != index.end() && !(it->second & 1)) ++n[t->t.pieces[it->second / 2][0]];
            return n;
        };
        auto shownCounts = [&]() { return countsOn(ed); };
        // (the counts of the shape with each spot more, at once: each a map of its own (helpers_ and this one), on the
        // processor's cores. Each map is painted whole, as paint() does)
        // (n trials at once, f(map, k) each: on the maps of helpers_, a thread each, on the processor's cores)
        auto onEach = [&](size_t n, const std::function<void(Editor*, size_t)>& f) {
            if (!pool_) {  // (once: a thread and a map for each core but this one; the threads stay, so their caches do)
                const size_t n = std::clamp<size_t>(std::thread::hardware_concurrency(), 2, 8) - 1;
                for (size_t k = 0; k < n; ++k) helpers_.push_back(std::make_unique<Editor>(*in.data)), helpers_.back()->setUndoLimit(1);
                pool_ = std::make_unique<TrialPool>(n);
            }
            if (helpersFor_ != in.gen) {  // (each build: its tileset and the working rules)
                helpersFor_ = in.gen;
                std::string ignored;
                for (auto& h : helpers_) {
                    if (!h->isOpen() || h->era() != in.era) h->newMap(W, H, in.era, root, ignored);
                    h->useModTerrains(in.all);
                }
            }
            // (each map its trials in a fixed order: trial k on the (k mod m)-th map that can take this tileset. A paint
            // depends a little on what its map painted before: so each run gives the same canvas)
            std::vector<size_t> usable;
            for (size_t w = 0; w < helpers_.size(); ++w)
                if (helpers_[w]->isOpen() && helpers_[w]->era() == in.era) usable.push_back(w);
            if (usable.empty()) {
                for (size_t k = 0; k < n && in.gen == buildGen_; ++k) f(ed, k);
                return;
            }
            pool_->run([&](size_t w) {
                const auto at = std::find(usable.begin(), usable.end(), w);
                if (at == usable.end()) return;  // (a map that could not take this tileset: the others do its share)
                for (size_t k = size_t(at - usable.begin()); k < n && in.gen == buildGen_; k += usable.size()) f(helpers_[w].get(), k);
            });
        };
        // (the strays: a few tiles of the terrain alone on the ground, as where two diamonds meet and the pieces around
        // draw as ground. As the eye sees it: a wall pixel is one of a color that the ground's tiles use little or not (a mod's
        // own dirt is drawn with the ground's colors); a tile with 32 wall pixels or more is part of the terrain, a NULL
        // half too (a piece to make). Two such tiles side by side are one shape only where their wall pixels meet at the
        // side between them, within 2 pixels, or at a corner (a box's bottom tile is dirt under its wall: a piece under it
        // stands apart; an isometric edge goes from tile to tile on a slant). A shape of
        // kStrayTiles tiles or fewer, with no tile of another shape within a tile of it: a stray. Each build, the spots go (the shape's too) whose removal leaves fewer
        // strays and every rect its places out of strays, up to its variants)
        const Tileset& ts = ed->tileset();
        auto pixelsOf = [&](uint16_t value, auto&& f) {  // (f(palette index) for each pixel of the tile)
            const int mg = ts.megatileFor(value);
            if (mg < 0 || size_t(mg) >= ts.megatiles.size()) return;
            for (uint32_t e : ts.megatiles[size_t(mg)])
                if ((e >> 1) < ts.minitiles.size())
                    for (uint8_t p : ts.minitiles[e >> 1]) f(p);
        };
        const std::tuple<uint8_t, uint16_t, uint16_t> groundKey{in.era, root, t->t.ground};
        if (!groundColors_.count(groundKey)) {  // (the ground's colors: its tiles' on the map with no shape)
            const auto keep = std::move(shape);
            shape.clear();
            paint();
            std::array<bool, 256>& used = groundColors_[groundKey];
            std::array<int, 256> count{};  // (a color is the ground's when it is 1 pixel in 500 of the ground or more: a
            int all = 0;                   // few dark pixels of dirt are not asphalt's gray)
            const stardraft::TerrainLayer l = stardraft::readTerrain(ed->chk(), "TILE");
            for (int ty = std::max(0, cy / 32 - 8); ty < std::min(H, cy / 32 + 8); ++ty)  // (the middle: far from the ground's border with the root)
                for (int tx = W / 2 - 8; tx < W / 2 + 8; ++tx) pixelsOf(l.tiles[size_t(ty) * W + tx], [&](uint8_t p) { ++count[p], ++all; });
            for (int p = 0; p < 256; ++p) used[size_t(p)] = count[size_t(p)] * 500 >= all && count[size_t(p)] > 0;
            shape = keep;
            paint();
        }
        const std::array<bool, 256> groundUsed = groundColors_[groundKey];
        const auto& Q = t->t.pieces;
        struct Edges {  // (a tile's wall pixels on its four sides, a bit for each pixel; none: no part of the terrain)
            uint32_t top = 0, bottom = 0, left = 0, right = 0;
            bool part = false;
            uint8_t corners = 0;  // (a wall pixel in the 3 x 3 of a corner: 1 top left, 2 top right, 4 bottom left, 8 bottom right)
        };
        auto edgesOf = [&](uint16_t value) {
            Edges sides;
            if (isomrules::nullHalf(value)) return Edges{~0u, ~0u, ~0u, ~0u, true, 15};
            const int mg = ts.megatileFor(value);
            if (mg < 0 || size_t(mg) >= ts.megatiles.size()) return sides;
            int walls = 0;
            for (int my = 0; my < 4; ++my)
                for (int mx = 0; mx < 4; ++mx) {
                    const uint32_t e = ts.megatiles[size_t(mg)][size_t(my * 4 + mx)];
                    if ((e >> 1) >= ts.minitiles.size()) continue;
                    const auto& mini = ts.minitiles[e >> 1];
                    for (int py = 0; py < 8; ++py)
                        for (int px = 0; px < 8; ++px) {
                            if (groundUsed[mini[size_t(py * 8 + ((e & 1) ? 7 - px : px))]]) continue;
                            const int x = mx * 8 + px, y = my * 8 + py;
                            ++walls;
                            if (y == 0) sides.top |= 1u << x;
                            if (y == 31) sides.bottom |= 1u << x;
                            if (x == 0) sides.left |= 1u << y;
                            if (x == 31) sides.right |= 1u << y;
                            if ((x < 3 || x > 28) && (y < 3 || y > 28)) sides.corners |= uint8_t(1u << ((y > 28 ? 2 : 0) + (x > 28 ? 1 : 0)));
                        }
                }
            sides.part = walls >= 32;
            return sides;
        };
        std::vector<std::array<Edges, 2>> edges(Q.size());  // (each piece's halves)
        for (size_t k = 0; k < Q.size(); ++k)
            for (int h : {0, 1}) edges[k][size_t(h)] = edgesOf(Q[k][size_t(1 + h)]);
        auto meet = [](uint32_t a, uint32_t b) { return (a & (b | b << 1 | b >> 1 | b << 2 | b >> 2)) != 0; };  // (2 pixels apart at most: an isometric edge crosses a side on a slant)
        struct Look {
            std::map<uint16_t, int> counts;  // (each rect's places out of strays)
            std::vector<std::pair<int, int>> strayTiles;  // (the strays' tiles)
            int strays = 0;
        };
        constexpr size_t kStrayTiles = 6;  // (as the eye sees them: tiles, not rects; a rect can be ground on one half and wall on the other)
        auto lookOn = [&](const Editor* e) {
            const stardraft::TerrainLayer l = stardraft::readTerrain(e->chk(), "TILE");
            std::map<std::pair<int, int>, size_t> at;           // (a place's left tile -> its piece)
            std::map<std::pair<int, int>, const Edges*> wall;  // (the tiles that are part of the terrain, their sides)
            for (int ty = 0; ty < H; ++ty)
                for (int tx = 0; tx < W; ++tx)
                    if (const auto it = index.find(l.tiles[size_t(ty) * W + tx]); it != index.end() && !(it->second & 1)) {
                        const size_t k = it->second / 2;
                        at[{tx, ty}] = k;
                        for (int h : {0, 1})
                            if (edges[k][size_t(h)].part) wall[{tx + h, ty}] = &edges[k][size_t(h)];
                    }
            Look o;
            std::set<std::pair<int, int>> stray;  // (the tiles of the strays)
            std::set<std::pair<int, int>> done;
            for (const auto& [p, pe] : wall) {
                if (done.count(p)) continue;
                std::vector<std::pair<int, int>> group{p}, todo{p};
                done.insert(p);
                while (!todo.empty()) {
                    const auto [x, y] = todo.back();
                    todo.pop_back();
                    const Edges& me = *wall.at({x, y});
                    auto link = [&](std::pair<int, int> nb, bool touch) {
                        if (touch && !done.count(nb)) done.insert(nb), group.push_back(nb), todo.push_back(nb);
                    };
                    if (const auto r = wall.find({x + 1, y}); r != wall.end()) link(r->first, meet(me.right, r->second->left));
                    if (const auto lt = wall.find({x - 1, y}); lt != wall.end()) link(lt->first, meet(me.left, lt->second->right));
                    if (const auto d = wall.find({x, y + 1}); d != wall.end()) link(d->first, meet(me.bottom, d->second->top));
                    if (const auto u = wall.find({x, y - 1}); u != wall.end()) link(u->first, meet(me.top, u->second->bottom));
                    for (const auto [dx, dy] : {std::pair{1, 1}, std::pair{-1, 1}, std::pair{1, -1}, std::pair{-1, -1}})  // (at a corner: an isometric edge goes from tile to tile on a slant)
                        if (const auto c = wall.find({x + dx, y + dy}); c != wall.end()) {
                            const int mine = (dy > 0 ? 2 : 0) + (dx > 0 ? 1 : 0), theirs = 3 - mine;
                            link(c->first, (me.corners >> mine & 1) && (c->second->corners >> theirs & 1));
                        }
                }
                // (alone: no tile of another shape within a tile of it (a corner's pebble, an edge's bit of the
                // tile's art next to the shape is the shape's look, not a stray))
                bool alone = group.size() <= kStrayTiles;
                for (size_t g = 0; alone && g < group.size(); ++g)
                    for (int dy = -1; alone && dy <= 1; ++dy)
                        for (int dx = -1; alone && dx <= 1; ++dx)
                            if (const std::pair<int, int> nb{group[g].first + dx, group[g].second + dy};
                                wall.count(nb) && std::find(group.begin(), group.end(), nb) == group.end())
                                alone = false;
                if (alone) ++o.strays, stray.insert(group.begin(), group.end()), o.strayTiles.insert(o.strayTiles.end(), group.begin(), group.end());
            }
            for (const auto& [p, k] : at) {  // (a place counts unless its wall tiles are all a stray's)
                const bool a = edges[k][0].part, b = edges[k][1].part;
                const bool kept = (!a && !b) || (a && !stray.count(p)) || (b && !stray.count({p.first + 1, p.second}));
                if (kept) ++o.counts[Q[k][0]];
            }
            return o;
        };
        std::vector<int> straysOfTrials;  // (countsWithEach: each trial's strays)
        auto countsWithEach = [&](const std::vector<std::pair<int, int>>& spots) {
            std::vector<std::map<uint16_t, int>> out(spots.size());
            straysOfTrials.assign(spots.size(), 0);
            onEach(spots.size(), [&](Editor* e, size_t k) { paintOn(e, &spots[k]), out[k] = countsOn(e), straysOfTrials[k] = lookOn(e).strays; });
            return out;
        };
        if (in.extend) {  // (more spots, one at a time: each must show a rect that none showed, or one more place of a rect
            // with fewer places than variants (each variant then shows at a place of its own); and lose no rect)
            std::set<uint16_t> want;
            std::map<uint16_t, int> need;  // (a rect's variants: its set's and its gaps' pieces)
            for (size_t k = 0; k < t->t.pieces.size(); ++k)
                if (k < t->t.setPieces || k + t->t.gapPieces >= t->t.pieces.size()) want.insert(t->t.pieces[k][0]), ++need[t->t.pieces[k][0]];
            auto score = [&](const std::map<uint16_t, int>& n) {  // (a rect shown first, then the variants' places)
                size_t sc = 0;
                for (const auto& [g, c] : n)
                    if (want.count(g)) sc += 1000 + size_t(std::min(c, need[g]));
                return sc;
            };
            std::map<uint16_t, int> counts = shownCounts();
            std::set<uint16_t> have;
            for (const auto& [g, c] : counts) have.insert(g);
            auto done = [&] {
                for (uint16_t g : want)
                    if (!counts.count(g) || counts[g] < need[g]) return false;
                return true;
            };
            const size_t before = have.size();
            // (each spot's gain of the round before is the most it can gain now (a spot shows no more rects next to more
            // spots): a round tries the spots by that gain and stops at the first that cannot beat the best; the first round
            // tries them all)
            struct Spot {
                std::pair<int, int> at;
                size_t bound;
            };
            std::vector<Spot> spots;
            for (int j = -9; j <= 9; ++j)
                for (int i = -6; i <= 6; ++i)
                    if (std::find(shape.begin(), shape.end(), std::make_pair(i, j)) == shape.end()) spots.push_back({{i, j}, SIZE_MAX});
            for (int round = 0; round < 40 && in.gen == buildGen_ && !done(); ++round) {  // (every rect and variant shown: no search)
                std::stable_sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.bound > b.bound; });
                int best = -1;
                std::map<uint16_t, int> bestCounts;
                size_t bestGain = 0;
                const size_t base = score(counts);
                paint();
                const int curStrays = lookOn(ed).strays;  // (the map as the shape is now)
                const size_t batch = 2 * std::max<size_t>(helpers_.size(), 1);  // (the spots tried at once: a few for each core)
                for (size_t s = 0; s < spots.size() && spots[s].bound > bestGain && in.gen == buildGen_;) {
                    std::vector<std::pair<int, int>> some;
                    for (size_t e = s; e < spots.size() && some.size() < std::max<size_t>(batch, 2) && spots[e].bound > bestGain; ++e) some.push_back(spots[e].at);
                    progress(kStepSearch, int(s + some.size()), int(spots.size()), round + 1);
                    std::vector<std::map<uint16_t, int>> seen = countsWithEach(some);
                    for (size_t b = 0; b < some.size() && spots[s].bound > bestGain; ++b, ++s) {  // (in turn, as one at a time: the first best wins; the rest of the batch, as one at a time would not try it, does not count)
                        std::map<uint16_t, int>& now = seen[b];
                        size_t gain = 0;
                        const bool newRect = std::any_of(want.begin(), want.end(), [&](uint16_t g) { return now.count(g) && !have.count(g); });
                        if (std::all_of(have.begin(), have.end(), [&](uint16_t g) { return now.count(g) > 0; }) &&  // (a spot that loses a rect: none)
                            (straysOfTrials[b] <= curStrays || newRect))  // (one that makes a stray: none, unless it shows a rect that none showed)
                            gain = score(now) > base ? score(now) - base : 0;
                        spots[s].bound = gain;
                        if (gain > bestGain) bestGain = gain, best = int(s), bestCounts = std::move(now);
                    }
                }
                if (best < 0) break;
                const auto [bi, bj] = spots[size_t(best)].at;
                bool lost = false;  // (a rect with fewer places than before, in reach of its variants: every spot may gain again)
                for (const auto& [g, c] : counts)
                    if (const auto it = bestCounts.find(g); it != bestCounts.end() && std::min(it->second, need[g]) < std::min(c, need[g])) lost = true;
                shape.push_back(spots[size_t(best)].at), counts = std::move(bestCounts);
                for (const auto& [g, c] : counts) have.insert(g);
                if (lost)
                    for (Spot& sp : spots) sp.bound = SIZE_MAX;
                spots.erase(spots.begin() + best);
                auto px = [](std::pair<int, int> a) {  // (a spot's middle in pixels: a half spot on the grid of 32 x 16)
                    return spotAt(a);
                };
                const auto [bx, by] = px({bi, bj});
                for (Spot& sp : spots)  // (the spots next to it, full or half: a joint with it can show rects they did not alone; tried again)
                    if (const auto [sx, sy] = px(sp.at); std::abs(sx - bx) <= 96 && std::abs(sy - by) <= 64) sp.bound = SIZE_MAX;
            }
            // (the led variants: a variant that the brush puts only under another one (its tiles that one's one row down)
            // needs that one's rect right above a place of its rect. The stacks each rect pair needs (a led variant each)
            // against the stacks the map has; spots one at a time, each giving more of the stacks needed and taking no
            // place from any rect, until none does)
            {
                const auto& Q = t->t.pieces;
                std::map<std::pair<uint16_t, uint16_t>, int> stacksNeed;  // (the rect above, the rect under it) -> led variants
                for (size_t k = 0; k < Q.size(); ++k) {
                    if (!(k < t->t.setPieces || k + t->t.gapPieces >= Q.size()) || isomrules::anyNull(Q[k])) continue;
                    for (size_t i = 0; i < Q.size(); ++i)
                        if (i != k && continues(Q[i], Q[k])) { ++stacksNeed[{Q[i][0], Q[k][0]}]; break; }
                }
                auto stacksOn = [&](const Editor* e) {
                    const stardraft::TerrainLayer l = stardraft::readTerrain(e->chk(), "TILE");
                    std::map<std::pair<uint16_t, uint16_t>, int> have;
                    for (int ty = 0; ty + 1 < H; ++ty)
                        for (int tx = 0; tx < W; ++tx) {
                            const auto a = index.find(l.tiles[size_t(ty) * W + tx]), b = index.find(l.tiles[size_t(ty + 1) * W + tx]);
                            if (a != index.end() && b != index.end() && !(a->second & 1) && !(b->second & 1)) ++have[{Q[a->second / 2][0], Q[b->second / 2][0]}];
                        }
                    return have;
                };
                auto stacksScore = [&](const std::map<std::pair<uint16_t, uint16_t>, int>& have) {
                    int sc = 0;
                    for (const auto& [p, n] : stacksNeed)
                        if (const auto it = have.find(p); it != have.end()) sc += std::min(it->second, n);
                    return sc;
                };
                paint();
                int curStacks = stacksScore(stacksOn(ed)), curStrays = lookOn(ed).strays;
                int needAll = 0;
                for (const auto& [p, n] : stacksNeed) needAll += n;
                for (int round = 0; round < 24 && curStacks < needAll && in.gen == buildGen_; ++round) {
                    std::vector<std::pair<int, int>> tries;
                    for (int j = -9; j <= 9; ++j)
                        for (int i = -6; i <= 6; ++i)
                            if (std::find(shape.begin(), shape.end(), std::make_pair(i, j)) == shape.end()) tries.push_back({i, j});
                    progress(kStepSearch, round + 1, 24, round + 1);
                    std::vector<std::pair<std::map<uint16_t, int>, std::map<std::pair<uint16_t, uint16_t>, int>>> seen(tries.size());
                    std::vector<int> strays(tries.size());
                    onEach(tries.size(), [&](Editor* e, size_t k) { paintOn(e, &tries[k]), seen[k] = {countsOn(e), stacksOn(e)}, strays[k] = lookOn(e).strays; });
                    int best = -1, bestStacks = curStacks;
                    for (size_t k = 0; k < tries.size(); ++k) {
                        const int sc = stacksScore(seen[k].second);
                        if (sc <= bestStacks || strays[k] > curStrays) continue;  // (no stray more)
                        bool keeps = true;  // (no rect loses a place, up to its variants)
                        for (const auto& [g, nv] : need) {
                            const auto a = counts.find(g), b = seen[k].first.find(g);
                            keeps = keeps && std::min(b == seen[k].first.end() ? 0 : b->second, nv) >= std::min(a == counts.end() ? 0 : a->second, nv);
                        }
                        if (keeps) best = int(k), bestStacks = sc;
                    }
                    if (best < 0) break;
                    shape.push_back(tries[size_t(best)]), counts = seen[size_t(best)].first, curStacks = bestStacks, curStrays = strays[size_t(best)];
                    for (const auto& [g, c] : counts) have.insert(g);
                }
            }
            // (the thin shapes: the rects that only the 0.5 brush's strips show (a thin strip has no room for a terrain's
            // walls), alone or where they join a wall. In a row of their own under the shape (kThinRow): three small
            // diamonds to join, then half spots one at a time: each shows a rect that none showed, and loses none. No rect
            // gained: the row goes)
            const size_t mainSpots = shape.size();
            const auto mainCounts = counts;
            const auto mainHave = have;
            for (int i : {-4, 0, 4}) shape.push_back({i, kThinRow / 2});
            paint();
            counts = shownCounts();
            have.insert(mainHave.begin(), mainHave.end());
            for (const auto& [g, c] : counts) have.insert(g);
            bool thinGained = std::any_of(want.begin(), want.end(), [&](uint16_t g) { return have.count(g) && !mainHave.count(g); });
            auto allShown = [&] { return std::all_of(want.begin(), want.end(), [&](uint16_t g) { return have.count(g) > 0; }); };
            // (half spots in rows jLo to jHi, one at a time, each for a rect that none showed. A spot that showed no new rect in a
            // round shows none in the next (the rects to find only get fewer) unless the spot added was next to it: it is
            // tried again only then)
            auto thinSearch = [&](int jLo, int jHi) {
                bool gained = false;
                auto px = [](std::pair<int, int> a) {  // (a spot's middle in pixels: a half spot on the grid of 32 x 16)
                    return spotAt(a);
                };
                auto nextTo = [&](std::pair<int, int> a, std::pair<int, int> b, int dx, int dy) {
                    const auto [ax, ay] = px(a);
                    const auto [bx, by] = px(b);
                    return std::abs(ax - bx) <= dx && std::abs(ay - by) <= dy;
                };
                std::set<std::pair<int, int>> dull;  // (the spots that showed no new rect the round before)
                std::pair<int, int> added{INT_MIN, INT_MIN};
                for (int round = 0; round < 24 && in.gen == buildGen_ && !allShown(); ++round) {
                int bestI = 0, bestJ = 0;
                size_t bestNew = 0;
                std::map<uint16_t, int> bestCounts;
                std::vector<std::pair<int, int>> tries;
                for (int j = jLo; j <= jHi; ++j)
                    for (int i = -12; i <= 12; ++i) {
                        const std::pair<int, int> sp{kHalfSpot + i, j};
                        if ((i + j) % 2 != 0 || std::find(shape.begin(), shape.end(), sp) != shape.end()) continue;
                        if (dull.count(sp) && !(added.first != INT_MIN && nextTo(sp, added, 96, 64))) continue;
                        tries.push_back(sp);
                    }
                progress(kStepSearch, 0, int(tries.size()), round + 1);
                std::vector<std::map<uint16_t, int>> seen = countsWithEach(tries);
                for (size_t k = 0; k < tries.size() && in.gen == buildGen_; ++k) {
                    const std::pair<int, int> sp = tries[k];
                    std::map<uint16_t, int>& now = seen[k];
                    dull.erase(sp);
                    if (!std::all_of(have.begin(), have.end(), [&](uint16_t g) { return now.count(g) > 0; })) continue;
                    size_t fresh = 0;
                    for (uint16_t g : want) fresh += now.count(g) && !have.count(g);
                    if (!fresh) dull.insert(sp);
                    if (fresh > bestNew) bestNew = fresh, bestI = sp.first - kHalfSpot, bestJ = sp.second, bestCounts = std::move(now);
                }
                if (!bestNew || in.gen != buildGen_) break;
                shape.push_back({kHalfSpot + bestI, bestJ}), counts = std::move(bestCounts), gained = true, added = shape.back();
                for (const auto& [g, c] : counts) have.insert(g);
                }
                return gained;
            };
            if (thinSearch(kThinRow - 3, kThinRow + 3)) thinGained = true;
            if (!thinGained) shape.resize(mainSpots), counts = mainCounts, have = mainHave;
            // (a rect that shows only where a thin strip joins the shape's walls is not added to the shape: such a strip
            // stands alone on the ground, a shape the terrain does not use. It stays in the rects not on the canvas)
            // (the cliff tops: the fill's pieces (the inside's tiles, each rect with its variants) need a wide inside to
            // each have a place: a diamond of kTopSize in a row of its own, when a fill rect has more variants than the
            // shape's small insides hold)
            std::map<uint16_t, int> fillVariants;
            for (size_t k = t->t.setPieces; k + t->t.gapPieces < t->t.pieces.size(); ++k) ++fillVariants[t->t.pieces[k][0]];
            if (std::any_of(fillVariants.begin(), fillVariants.end(), [](const auto& f) { return f.second > 2; })) {
                shape.push_back({kTopSpot, kTopRow});
                paint();
                const std::map<uint16_t, int> now = shownCounts();
                if (!std::all_of(have.begin(), have.end(), [&](uint16_t g) { return now.count(g) > 0; })) shape.pop_back();  // (a rect lost: no tops)
                else counts = now;
            }
            out.gained = int(have.size() - before);
            out.extra.assign(shape.begin() + long(shapeOf(in.family).size()), shape.end());
            paint();
        }
        {  // (the strays of every terrain: counted and taken away; the standard border returned before this)
            std::map<uint16_t, int> variants;  // (every rect (the set's, the gaps', the fill's: the cliff tops stay), its variants)
            for (size_t k = 0; k < Q.size(); ++k) ++variants[Q[k][0]];
            auto placesOf = [&](const Look& x, uint16_t g, int n) {
                const auto it = x.counts.find(g);
                return std::min(it == x.counts.end() ? 0 : it->second, n);
            };
            uint64_t cleanKey = 1469598103934665603ull;  // (the shape and each piece's sides: a tile changed, the cleanup runs again)
            auto mix = [&](uint64_t v) { cleanKey = (cleanKey ^ v) * 1099511628211ull; };
            mix(uint64_t(in.era) << 32 | uint64_t(root) << 16 | t->t.ground);
            for (const auto& [i, j] : shape) mix(uint64_t(uint32_t(i)) << 32 | uint32_t(j));
            for (const auto& e2 : edges)
                for (const Edges& e1 : e2) mix(uint64_t(e1.top) << 32 | e1.bottom), mix(uint64_t(e1.left) << 32 | e1.right), mix(e1.part);
            if (const auto it = cleaned_.find(cleanKey); it != cleaned_.end()) {
                if (it->second != shape) shape = it->second, paint();
            } else {
            Look cur = lookOn(ed);
            bool tried = false;
            // (first no rect loses a place (up to its variants); then, for the strays left, a rect keeps a place at least:
            // a stray is worse than a variant that shares a place)
            for (int round = 0, strict = 1; round < 24 && cur.strays > 0 && in.gen == buildGen_; ++round) {
                tried = true;
                progress(kStepSearch, round + 1, 16, 1);
                std::vector<Look> without(shape.size());
                onEach(shape.size(), [&](Editor* e, size_t k) { paintOn(e, nullptr, k), without[k] = lookOn(e); });
                int best = -1;
                for (size_t k = 0; k < shape.size(); ++k) {
                    if (without[k].strays >= (best < 0 ? cur.strays : without[size_t(best)].strays)) continue;
                    bool keeps = true;  // (every rect its places out of strays; or one at least)
                    for (const auto& [g, n] : variants) keeps = keeps && placesOf(without[k], g, strict ? n : 1) >= placesOf(cur, g, strict ? n : 1);
                    if (keeps) best = int(k);
                }
                if (best < 0 && strict) {
                    strict = 0;
                    continue;
                }
                if (best < 0) {  // (a swap: a spot whose removal leaves fewer strays but takes a rect away, for one spot of the
                    // shape's grid that shows the rects again with no stray more)
                    auto shown = [](const Look& x) {
                        std::set<uint16_t> g;
                        for (const auto& [r, n] : x.counts)
                            if (n > 0) g.insert(r);
                        return g;
                    };
                    const std::set<uint16_t> had = shown(cur);
                    bool swapped = false;
                    for (size_t k = 0; k < shape.size() && !swapped && in.gen == buildGen_; ++k) {
                        if (without[k].strays >= cur.strays || spotKind(shape[k].first) != 0) continue;
                        const auto keep = shape;
                        shape.erase(shape.begin() + long(k));
                        std::vector<std::pair<int, int>> tries;
                        for (int j = -9; j <= 9; ++j)
                            for (int i = -6; i <= 6; ++i)
                                if (std::find(keep.begin(), keep.end(), std::make_pair(i, j)) == keep.end()) tries.push_back({i, j});
                        std::vector<Look> with(tries.size());
                        onEach(tries.size(), [&](Editor* e, size_t c) { paintOn(e, &tries[c]), with[c] = lookOn(e); });
                        for (size_t c = 0; c < tries.size() && !swapped; ++c) {
                            const std::set<uint16_t> now = shown(with[c]);
                            if (with[c].strays >= cur.strays || !std::includes(now.begin(), now.end(), had.begin(), had.end())) continue;
                            shape.push_back(tries[c]), cur = std::move(with[c]), swapped = true;
                        }
                        if (!swapped) shape = keep;
                    }
                    if (swapped) continue;
                }
                if (best < 0) break;
                cur = std::move(without[size_t(best)]);
                shape.erase(shape.begin() + best);
            }
            if (tried) paint();  // (the trials painted the maps: this one again, whole)
            if (in.gen == buildGen_) cleaned_[cleanKey] = shape;
            }
            const Look last = lookOn(ed);
            out.strays = last.strays, out.strayTiles = last.strayTiles;
        }
        if (in.removeAt.first != INT_MIN) {  // (Remove From Canvas: of the 0.5 brush's strips, the one whose removal takes the place away with the fewest other tiles changed)
            const stardraft::TerrainLayer full = stardraft::readTerrain(ed->chk(), "TILE");
            const auto [rx, ry] = in.removeAt;
            auto piece = [&](const stardraft::TerrainLayer& l) { return rx >= 0 && ry >= 0 && rx < W && ry < H && index.count(l.tiles[size_t(ry) * W + rx]) > 0; };
            const std::vector<std::pair<int, int>> all = shape;
            size_t best = SIZE_MAX;
            for (size_t k = 0; k < all.size() && in.gen == buildGen_; ++k) {
                if (spotKind(all[k].first) != 1) continue;
                shape = all;
                shape.erase(shape.begin() + long(k));
                paint();
                const stardraft::TerrainLayer l = stardraft::readTerrain(ed->chk(), "TILE");
                if (piece(l)) continue;  // (the place is still there)
                size_t changed = 0;
                for (size_t i = 0; i < l.tiles.size(); ++i) changed += l.tiles[i] != full.tiles[i];
                if (changed < best) best = changed, out.removeSpot = all[k];
            }
            return;
        }
        stardraft::TerrainLayer tl = stardraft::readTerrain(ed->chk(), "TILE");
        int x0 = W, y0 = H, x1 = -1, y1 = -1;
        std::set<uint16_t> seen;
        std::vector<bool> nullAt(tl.tiles.size());
        // (each place's piece: a pinned place (Show variant, Make Unique) its variant; else the rect's variants in turn over
        // its places, so that each variant shows at a place of its own; under a piece of the terrain the piece one row down
        // at the same subtiles where the rect has it, as the brush puts it)
        const auto& P = t->t.pieces;
        std::map<std::pair<int, int>, size_t> chosen;  // (a place's left tile -> its piece)
        std::set<std::pair<int, int>> pinnedAt;
        {
            auto isFill = [&](size_t k) { return k >= t->t.setPieces && k + t->t.gapPieces < P.size(); };
            auto variantsOf = [&](uint16_t g) {  // (the rect's variants: its set's and its gaps' pieces; a fill rect: its fill pieces)
                std::vector<size_t> vs;
                for (size_t i = 0; i < P.size(); ++i)
                    if (P[i][0] == g && !isFill(i)) vs.push_back(i);
                if (vs.empty())
                    for (size_t i = 0; i < P.size(); ++i)
                        if (P[i][0] == g && isFill(i)) vs.push_back(i);
                return vs;
            };
            // (the places in turn, top down; a pinned place (Show variant, Make Unique) keeps its variant)
            std::vector<std::pair<int, int>> order;
            std::map<std::pair<int, int>, uint16_t> rectAt;
            for (int ty = 0; ty < H; ++ty)
                for (int tx = 0; tx < W; ++tx) {
                    const auto it = index.find(tl.tiles[size_t(ty) * W + tx]);
                    if (it == index.end() || (it->second & 1)) continue;
                    size_t k = it->second / 2;
                    const uint16_t g = P[k][0];
                    rectAt[{tx, ty}] = g;
                    if (const auto pin = in.pins.find({tx, ty}); pin != in.pins.end() && pin->second.first == g) {
                        int n = pin->second.second;
                        for (size_t i = 0; i < P.size(); ++i)
                            if (P[i][0] == g && n-- == 0) { k = i; break; }
                        pinnedAt.insert({tx, ty}), chosen[{tx, ty}] = k;
                    } else {
                        order.push_back({tx, ty}), chosen[{tx, ty}] = k;
                    }
                }
            // (first the led variants: a variant that the brush puts only under another one gets that one on a place of
            // its rect right above a place of its rect, the led one under it; both stay)
            std::map<size_t, int> uses;
            for (size_t k = 0; k < P.size(); ++k) {
                if (isFill(k) || isomrules::anyNull(P[k])) continue;
                size_t leader = SIZE_MAX;
                for (size_t i = 0; i < P.size() && leader == SIZE_MAX; ++i)
                    if (i != k && continues(P[i], P[k])) leader = i;
                if (leader == SIZE_MAX) continue;
                for (const auto& p : order) {
                    const std::pair<int, int> below{p.first, p.second + 1};
                    const auto r = rectAt.find(below);
                    if (rectAt[p] != P[leader][0] || r == rectAt.end() || r->second != P[k][0] || (pinnedAt.count(p) && chosen[p] != leader) || pinnedAt.count(below)) continue;  // (a place pinned with the leader already: the rows of a block chain, as a wall's)
                    chosen[p] = leader, chosen[below] = k, pinnedAt.insert(p), pinnedAt.insert(below), ++uses[leader], ++uses[k];
                    break;
                }
            }
            // (then each other place: the variant of its rect with the fewest places so far, in turn among those)
            std::map<uint16_t, size_t> turn;
            for (const auto& p : order) {
                if (pinnedAt.count(p)) continue;
                const std::vector<size_t> vs = variantsOf(rectAt[p]);
                if (vs.empty()) continue;
                size_t& at = turn[rectAt[p]];
                size_t best = vs[at % vs.size()];
                for (size_t n = 0; n < vs.size(); ++n)
                    if (const size_t c = vs[(at + n) % vs.size()]; uses[c] < uses[best]) best = c;
                ++at, chosen[p] = best, ++uses[best];
            }
            auto follow = [&] {
                for (int ty = 1; ty < H; ++ty)  // (the rows of a block, top down)
                    for (int tx = 0; tx < W; ++tx) {
                        const auto me = chosen.find({tx, ty}), up = chosen.find({tx, ty - 1});
                        if (me == chosen.end() || up == chosen.end() || pinnedAt.count({tx, ty})) continue;
                        if (isFill(me->second)) continue;  // (a fill piece: no block, as the brush)
                        for (size_t i = 0; i < P.size(); ++i)
                            if (P[i][0] == P[me->second][0] && continues(P[up->second], P[i])) { me->second = i; break; }
                    }
            };
            follow();
            // (a variant with no place yet: a place of its rect takes it, or, when the brush puts it only under another
            // variant (its tiles that one's one row down), a place of that one's rect over a place of its rect takes that
            // one; only a place whose variant has another place, so no variant loses its last; then the rows follow again)
            for (int pass = 0; pass < 8; ++pass) {
                std::map<size_t, int> uses;
                for (const auto& [p, k] : chosen) ++uses[k];
                bool changed = false;
                for (size_t k = 0; k < P.size(); ++k) {
                    if (isFill(k) || uses.count(k) || isomrules::anyNull(P[k])) continue;
                    auto spare = [&](const std::pair<int, int>& p, uint16_t rect) {  // (a place of `rect` whose variant has another place)
                        const auto c = chosen.find(p);
                        return c != chosen.end() && P[c->second][0] == rect && !pinnedAt.count(p) && uses[c->second] > 1;
                    };
                    size_t leader = SIZE_MAX;
                    for (size_t i = 0; i < P.size() && leader == SIZE_MAX; ++i)
                        if (i != k && continues(P[i], P[k])) leader = i;
                    for (const auto& [p, c] : chosen) {
                        const std::pair<int, int> below{p.first, p.second + 1};
                        if (leader != SIZE_MAX ? spare(p, P[leader][0]) && chosen.count(below) && P[chosen[below]][0] == P[k][0] : spare(p, P[k][0])) {
                            --uses[c], chosen[p] = leader != SIZE_MAX ? leader : k, ++uses[chosen[p]], changed = true;
                            break;
                        }
                    }
                }
                if (!changed) break;
                follow();
            }
        }
        for (int ty = 0; ty < H; ++ty)
            for (int tx = 0; tx < W; ++tx) {
                uint16_t& v = tl.tiles[size_t(ty) * W + tx];
                const auto it = index.find(v);
                if (it == index.end()) continue;
                size_t k = it->second / 2;
                const bool right = it->second & 1;
                if (const auto c = chosen.find({right ? tx - 1 : tx, ty}); c != chosen.end() && P[c->second][0] == P[k][0]) k = c->second;
                const auto& q = t->t.pieces[k];
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
        progress(kStepPicture);
        out.picture = isomrules::tilesBitmap(ed->tileset(), part, x1 - x0 + 1, y1 - y0 + 1);
        addReference(shape);
    }
    // A family's shape: diamonds of 1, in steps of 64 to the side and 32 up or down from the middle (the places that show
    // the most rects of walls and of blends: a search over all the mod's terrains found the same few each time; More
    // rects adds to them); the game's borders (Other): both.
    static const std::vector<std::pair<int, int>>& shapeOf(isomrules::Family f) {
        // (the thin strips maps have (the 0.5 brush) are not in it: the search puts those that show a rect in a row of
        // their own under it, kThinRow)
        static const std::vector<std::pair<int, int>> kWall = {{0, 0}, {0, -2}, {-1, -3}, {-1, 3}, {3, -3}};
        static const std::vector<std::pair<int, int>> kPath = {{0, 0}, {0, -2}, {-1, 1}, {0, 2}, {3, 1}, {3, -3}, {-3, -3}};
        static const std::vector<std::pair<int, int>> kBoth = {{0, 0}, {0, -2}, {-1, -3}, {-1, 3}, {3, -3}, {-1, 1}, {0, 2}, {3, 1}, {-3, -3}};
        return f == isomrules::Family::Wall || f == isomrules::Family::Cliff ? kWall : f == isomrules::Family::Other ? kBoth : kPath;
    }
    // The reference over the example, on or off (Ctrl+R); its opacity is the slider (Ctrl+Shift+Up / Ctrl+Shift+Down: 10 % steps; 100 %:
    // the reference alone).
    void toggleReference() {
        refOn_ = !refOn_;
        refShow_.setOn(refOn_);
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
    // The reference border (an index of refEntries_; -1: none): the one picked, or (Template) the terrain's template:
    // its stand-in on the game terrain its ground stands for, in the tileset edited.
    int refOf(const stardraft::terrainrules::Terrain* t) const {
        const int c = refPick_.value();
        if (c >= 2 && c < int(refEntries_.size())) return refEntries_[size_t(c)].era >= 0 ? c : -1;
        if (c != 0 || !t) return -1;
        const uint16_t ground = t->t.groundTemplate ? t->t.groundTemplate : t->t.ground, terrain = t->t.standIn;
        for (int pass = 0; pass < 2; ++pass)
            for (size_t k = 0; k < refEntries_.size(); ++k)
                if (const RefEntry& r = refEntries_[k]; r.era == int(era()) &&
                    (pass == 0 ? r.ground == ground && r.terrain == terrain : r.ground == terrain && r.terrain == ground))
                    return int(k);
        return -1;
    }
    // A tileset's game borders (ground, terrain on it).
    static std::vector<std::pair<uint16_t, uint16_t>> bordersOf(int e) {
        std::vector<std::pair<uint16_t, uint16_t>> out;
        if (const IsomTerrainTable* game = IsomTerrainTable::forEra(uint16_t(e)))
            for (const auto& pr : game->pairs())
                if (game->isTerrain(pr.a) && game->isTerrain(pr.b)) out.push_back({pr.a, pr.b});
        return out;
    }
    // The Reference list: Template, None, then each tileset's borders under its name (the tileset edited first), each
    // with its tile (another tileset's tiles once it is loaded: refPics_). refEntries_: what each item is.
    struct RefEntry {
        int era = -1;  // (-1: Template, None or a heading)
        uint16_t ground = 0, terrain = 0;
    };
    std::vector<RefEntry> refEntries_;
    std::map<int, std::map<uint16_t, std::shared_ptr<draw::Bitmap>>> refPics_;  // (another tileset: its terrains' tiles, for the list)
    bool refsDue_ = false;  // (tiles came while the list was open: it fills when it closes)
    void fillRefs() {
        const int keep = refPick_.value();
        const RefEntry kept = keep >= 0 && keep < int(refEntries_.size()) ? refEntries_[size_t(keep)] : RefEntry{};
        std::vector<stardraft::kit::DropdownItem> refs{{L"Template"}, {L"None"}};
        refEntries_ = {{}, {}};
        int value = keep == 1 ? 1 : 0;
        for (int n = 0; n < 8; ++n) {
            const int e = n == 0 ? int(era()) : n <= int(era()) ? n - 1 : n;  // (the tileset edited, then the others in order)
            const IsomTerrainTable* game = IsomTerrainTable::forEra(uint16_t(e));
            const auto pics = refPics_.find(e);
            const auto borders = bordersOf(e);
            if (!game || borders.empty()) continue;
            refs.push_back({isomrules::tilesetTitle(e)}), refs.back().heading = true, refEntries_.push_back({});
            for (const auto& [g, t] : borders) {
                refs.push_back({widen(game->terrainName(t)) + L" (" + widen(game->terrainName(g)) + L")"});
                if (e == int(era())) refs.back().image = terrainPic(tileset_, t);
                else if (pics != refPics_.end())
                    if (const auto pic = pics->second.find(t); pic != pics->second.end()) refs.back().image = pic->second;
                if (kept.era == e && kept.ground == g && kept.terrain == t) value = int(refs.size()) - 1;
                refEntries_.push_back({e, g, t});
            }
        }
        refPick_.setItems(refs);
        refPick_.setValue(value);
    }
    // The game's own tile groups of a tileset (the mod's groups past them: not in a game tileset); 0: unknown.
    static size_t gameGroups(int e) { return stardraft::cmbw::gameTileGroups(e); }
    // A game terrain's tile (its solid group's), as a picture; null: none.
    static std::shared_ptr<draw::Bitmap> terrainPic(const stardraft::Tileset& ts, uint16_t terrain) {
        if (ts.groups.empty()) return nullptr;
        const int g = stardraft::solidTileGroup(ts, terrain);
        const uint16_t v = g >= 0 ? stardraft::terrainrules::gameTile(ts, uint32_t(g)) : 0;
        return v ? std::shared_ptr<draw::Bitmap>(isomrules::tilesBitmap(ts, {v}, 1, 1)) : nullptr;
    }
    // The other tilesets load on the worker one after the other (a job each: an example waits for one at most); each
    // gives the list its borders' tiles, then goes.
    void loadRefTilesets() {
        int next = -1;
        for (int e = 0; e < 8 && next < 0; ++e)
            if (e != int(era()) && !refPics_.count(e)) next = e;
        if (next < 0 || !async_) return;
        refPics_[next];  // (asked for)
        std::shared_ptr<bool> alive = alive_;
        background([this, next, alive] {
            auto pics = std::make_shared<std::map<uint16_t, std::shared_ptr<draw::Bitmap>>>();
            stardraft::Tileset ts;
            std::string err;
            if (loadTileset(ts, next, false, err))
                for (const auto& [g, t] : bordersOf(next))
                    if (!pics->count(t)) (*pics)[t] = terrainPic(ts, t);
            foreground([this, next, alive, pics] {
                if (!*alive) return;
                refPics_[next] = std::move(*pics);
                if (refPick_.listOpen()) refsDue_ = true;  // (an open list: not now, its search and keys stay)
                else fillRefs();
                loadRefTilesets();
            });
        });
    }
    // The target (the rect, or the half: 1 tile) has no tile: NULL (a place that needs work; it stays where it is: the
    // engine reads no NULL half as a game tile).
    void setNull() {
        if (loading_) return;
        if (sel_ < 0 || target_ < 0) return setMessage(std_ >= 0 ? L"Game tilesets are read-only" : L"Select a rect first");
        auto& m = rules().terrains[size_t(sel_)].t;
        if (target_ >= int(m.pieces.size())) return;
        const size_t k = size_t(target_);
        if (k >= m.setPieces && k < m.pieces.size() - m.gapPieces) return setMessage(L"Fill pieces cannot be NULL");
        if (!remember()) return;
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

    // The brush is the tile (pair mode: the pair) at value; the example's slots get it.
    void endStroke() {
        if (!painting_) return;
        painting_ = strokeSaved_ = false, lastPaint_ = -1;
        if (host()) host()->capture(*this, false);
        invalidate();
    }
    // Paint mode (a tile of the Tiles as a brush, click or drag on slots): off for now, selection only (a slot selected,
    // then a click in the Tiles fills it).
    static constexpr bool kBrushMode = false;
    void pickBrush(int value) {
        if (!kBrushMode) {
            if (value >= 0) setMessage(L"Select a slot first: a click in Tiles fills the selected slot");
            return;
        }
        if (value < 0) endStroke();  // (2: no brush, no stroke)
        brush_ = value;
        const int v = brushValue();
        setMessage(v < 0 ? std::wstring(L"Brush: none") : L"Brush: " + std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15) + (wide_ ? L"-" + std::to_wstring((v & 15) + 1) : L""));
        invalidate();
    }
    // The target's slot is NULL (pair mode: both halves; single: the target's half). A stroke is one undo step.
    void erase() {
        if (loading_) return;
        if (sel_ < 0 || target_ < 0) return setMessage(std_ >= 0 ? L"Game tilesets are read-only" : L"Select a rect first");
        auto& m = rules().terrains[size_t(sel_)].t;
        const size_t k = size_t(target_);
        if (k >= m.pieces.size()) return;
        if (k >= m.setPieces && k < m.pieces.size() - m.gapPieces) return setMessage(L"Fill pieces cannot be NULL");
        auto p = m.pieces[k];
        const auto was = p;
        if (wide_) p[1] = 0, p[2] = 1;
        else p[size_t(1 + targetHalf_)] = uint16_t(targetHalf_);  // (the null tile's subtiles: 0 left, 1 right)
        if (p == was) return;
        if (!painting_ || !strokeSaved_) {
            if (!remember()) return;
            strokeSaved_ = painting_;
        }
        m.pieces[k] = p;
        changed();
        auto half = [](uint16_t v) { return isomrules::nullHalf(v) ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15); };
        setMessage(L"Rect " + std::to_wstring(p[0]) + L": " + half(p[1]) + L" " + half(p[2]));
    }
    void deselectAll() {
        endStroke();
        brush_ = -1, target_ = -1, slotKeys_ = false;
        setMessage(L"Nothing selected");
        invalidate();
    }
    // Fill From Row (Shift+click in the Tiles): the target's rect takes every pair of tile group `row` (subtiles 0-1,
    // 2-3, ... 14-15 where both are tiles: the mod's layout, a pair side by side) as its variants, in place of the ones
    // it had. One undo step.
    void fillFromRow(int row) {
        if (loading_) return;
        if (sel_ < 0 || target_ < 0) return setMessage(std_ >= 0 ? L"Game tilesets are read-only" : L"Select a rect first");
        auto& m = rules().terrains[size_t(sel_)].t;
        if (target_ >= int(m.pieces.size())) return;
        const size_t k = size_t(target_);
        if (k >= m.setPieces && k < m.pieces.size() - m.gapPieces) return setMessage(L"Fill pieces cannot have variants");
        const uint16_t g = m.pieces[k][0];
        std::vector<std::array<uint16_t, 3>> pairs;
        for (int c = 0; c + 1 < 16; c += 2) {
            const uint16_t l = uint16_t(row << 4 | c), r = uint16_t(row << 4 | (c + 1));
            if (tileset_.megatileFor(l) > 0 && tileset_.megatileFor(r) > 0) pairs.push_back({g, l, r});
        }
        if (pairs.empty()) return setMessage(L"Row " + std::to_wstring(row) + L" has no tile pairs (subtiles 0-1, 2-3, ...)");
        if (!remember()) return;
        // (the rect's pieces go, from the set and the gaps; the row's pairs go into the set: normalize puts them in order)
        std::vector<std::array<uint16_t, 3>> set, fill, gaps;
        for (size_t i = 0; i < m.pieces.size(); ++i) {
            auto& to = i < m.setPieces ? set : i + m.gapPieces >= m.pieces.size() ? gaps : fill;
            if (m.pieces[i][0] != g || &to == &fill) to.push_back(m.pieces[i]);
        }
        set.insert(set.end(), pairs.begin(), pairs.end());
        m.pieces = set, m.setPieces = set.size();
        m.pieces.insert(m.pieces.end(), fill.begin(), fill.end());
        m.pieces.insert(m.pieces.end(), gaps.begin(), gaps.end()), m.gapPieces = gaps.size();
        for (auto it = pins_.begin(); it != pins_.end();)  // (the rect's pins: its variants are new)
            it = it->second.first == g ? pins_.erase(it) : std::next(it);
        target_ = -1;
        for (size_t i = 0; i < m.pieces.size() && target_ < 0; ++i)
            if (m.pieces[i] == pairs.front()) target_ = int(i);
        changed();
        setMessage(L"Rect " + std::to_wstring(g) + L": " + std::to_wstring(pairs.size()) + (pairs.size() == 1 ? L" pair" : L" pairs") + L" of row " + std::to_wstring(row) +
                   L" (the brush picks one of them for each place)");
    }
    // Make Unique: a copy of the target's piece is a new variant of its rect (the engine picks one of a rect's variants
    // where it paints the rect), and the target is the copy: its edits leave the other variant.
    void makeUnique() {
        if (loading_) return;
        if (sel_ < 0 || target_ < 0) return setMessage(std_ >= 0 ? L"Game tilesets are read-only" : L"Select a rect first");
        auto& m = rules().terrains[size_t(sel_)].t;
        const size_t k = size_t(target_);
        if (k >= m.pieces.size()) return;
        if (k >= m.setPieces && k < m.pieces.size() - m.gapPieces) return setMessage(L"Fill pieces cannot have variants");
        const auto q = m.pieces[k];
        if (isomrules::nullHalf(q[1]) && isomrules::nullHalf(q[2])) return setMessage(L"Rect " + std::to_wstring(q[0]) + L" is NULL: paint it first");
        // (each piece's number among the rect's pieces; the copy comes right after the original)
        auto ordinal = [&](size_t i) {
            int o = 0;
            for (size_t j = 0; j < i; ++j) o += m.pieces[j][0] == q[0] ? 1 : 0;
            return o;
        };
        const int ordK = ordinal(k);
        // (every place of the rect keeps the variant it shows now: the engine would spread the new one over them too; the
        // clicked place shows the copy)
        std::map<std::pair<int, int>, std::pair<uint16_t, int>> keep;
        for (const Place& p : places_)
            if (p.piece >= 0 && p.piece < int(m.pieces.size()) && m.pieces[size_t(p.piece)][0] == q[0]) {
                const int o = ordinal(size_t(p.piece));
                keep[{p.tx, p.ty}] = {q[0], o > ordK ? o + 1 : o};
            }
        if (pinAt_.first != INT_MIN) keep[pinAt_] = {q[0], ordK + 1};
        else if (!keep.empty()) keep.begin()->second.second = ordK + 1;  // (from the checklist: the rect's first place shows the copy)
        if (!remember()) return;
        m.pieces.insert(m.pieces.begin() + long(k) + 1, q);
        if (k < m.setPieces) ++m.setPieces;
        for (const auto& [at, pin] : keep) pins_[at] = pin;
        changed();
        int n = 0, copy = -1;  // (the copy: the rect's piece number ordK + 1)
        for (size_t i = 0; i < m.pieces.size(); ++i)
            if (m.pieces[i][0] == q[0]) {
                if (n == ordK + 1) copy = int(i);
                ++n;
            }
        if (copy >= 0) setTarget(copy, targetHalf_, false), slotFocus(true);
        setMessage(L"Rect " + std::to_wstring(q[0]) + L": variant " + std::to_wstring(n) + L" of " + std::to_wstring(n) + L" (the brush picks one of them for each place)" +
                   (visibleGroups_.count(q[0]) ? L"" : L"; not on the terrain canvas"));
    }
    // The right button's menu of slot (piece k, half): Set to NULL, Use as Brush, Show in Tiles, Make Unique.
    std::pair<int, int> pinAt_{INT_MIN, INT_MIN};  // (the place of the slot's menu: its tile place)
    // Remove: the target's piece goes. One of the rect's variants: only that one; the rect's only piece: NULL (the
    // template has every rect); a rect the template does not have: gone from the terrain. One undo step.
    void removePiece() {
        if (loading_) return;
        if (sel_ < 0 || target_ < 0) return setMessage(std_ >= 0 ? L"Game tilesets are read-only" : L"Select a rect first");
        auto& m = rules().terrains[size_t(sel_)].t;
        const size_t k = size_t(target_);
        if (k >= m.pieces.size()) return;
        if (k >= m.setPieces && k < m.pieces.size() - m.gapPieces) {  // (a fill piece: the terrain's inside; one of them stays)
            const auto q = m.pieces[k];
            const size_t fillEnd = m.pieces.size() - m.gapPieces, fills = fillEnd - m.setPieces;
            const size_t same = size_t(std::count(m.pieces.begin() + long(m.setPieces), m.pieces.begin() + long(fillEnd), q));
            if (same >= fills) return setMessage(L"Remove: fill " + std::to_wstring(q[0]) + L": every fill piece has these tiles (kept: a solid place always shows a fill piece; set its tiles instead)");
            if (!remember()) return;
            const uint16_t g = q[0];
            for (size_t i = fillEnd; i-- > m.setPieces;)  // (every fill piece with these tiles: else the next one shows the same tile)
                if (m.pieces[i] == q) m.pieces.erase(m.pieces.begin() + long(i));
            target_ = -1;
            changed();
            return setMessage(L"Remove: fill " + std::to_wstring(g) + L": " + std::to_wstring(same) + (same > 1 ? L" pieces" : L" piece") + L" with these tiles removed (" + std::to_wstring(m.pieces.size() - m.gapPieces - m.setPieces) + L" fill pieces left)");
        }
        const uint16_t g = m.pieces[k][0];
        const auto q = m.pieces[k];
        if (isomrules::nullHalf(q[1]) && isomrules::nullHalf(q[2])) return setMessage(L"Remove: rect " + std::to_wstring(g) + L" is NULL");
        const auto n = std::count_if(m.pieces.begin(), m.pieces.end(), [g](const auto& p) { return p[0] == g; });
        const auto same = std::count(m.pieces.begin(), m.pieces.end(), q);  // (the variants with these tiles: all go, else the tile stays in sight)
        const bool inTemplate = std::any_of(tmpl_.begin(), tmpl_.end(), [g](const auto& r) { return r.group == g; });
        if (!remember()) return;
        std::wstring what;
        // (every piece with these tiles goes; the segments' counts follow)
        std::vector<std::array<uint16_t, 3>> keep;
        size_t set = 0, gaps = 0;
        for (size_t i = 0; i < m.pieces.size(); ++i) {
            if (m.pieces[i] == q) continue;
            keep.push_back(m.pieces[i]);
            set += i < m.setPieces, gaps += i + m.gapPieces >= m.pieces.size();
        }
        if (n > same || !inTemplate) {
            m.pieces = std::move(keep), m.setPieces = set, m.gapPieces = gaps;
            what = n > same ? std::to_wstring(same) + (same > 1 ? L" pieces" : L" piece") + L" removed, " + std::to_wstring(n - same) + L" variants left"
                            : L"removed (group not in template)";
        } else {  // (its only tiles: NULL, once)
            keep.insert(keep.begin() + long(set), {g, 0, 1});
            m.pieces = std::move(keep), m.setPieces = set + 1, m.gapPieces = gaps;
            what = L"last piece removed: NULL";
        }
        for (auto it = pins_.begin(); it != pins_.end();)  // (the rect's pins: its variants changed)
            it = it->second.first == g ? pins_.erase(it) : std::next(it);
        target_ = -1;
        for (size_t i = 0; i < m.pieces.size() && target_ < 0; ++i)
            if (m.pieces[i][0] == g) target_ = int(i);
        changed();
        setMessage(L"Remove: rect " + std::to_wstring(g) + L": " + what);
    }
    void slotMenu(int k, int half, draw::Point at, int place) {
        pinAt_ = place >= 0 && place < int(places_.size()) ? std::make_pair(places_[size_t(place)].tx, places_[size_t(place)].ty) : std::make_pair(INT_MIN, INT_MIN);
        setTarget(k, half, false);
        invalidate();
        const uint16_t v = terrain()->t.pieces[size_t(k)][size_t(1 + half)];
        std::vector<std::wstring> items{L"Set to NULL"};
        if (kBrushMode) items.push_back(L"Use as Brush");
        for (const wchar_t* s : {L"Show in Tiles", L"Make Unique (Unlink From Linked Tiles)", L"Remove"}) items.push_back(s);
        if (place >= 0) items.push_back(L"Remove From Canvas");
        if (!shapeKey_.empty() && !hiddenOf(shapeKey_).empty()) items.push_back(L"Restore Removed Pieces");
        const int r = shellContextMenuAt(*this, at, items);
        const std::wstring picked = r >= 1 && r <= int(items.size()) ? items[size_t(r - 1)] : std::wstring();
        if (picked == L"Set to NULL") erase();
        else if (picked == L"Use as Brush" || picked == L"Show in Tiles") {
            if (isomrules::nullHalf(v)) setMessage(L"The slot is NULL");
            else {
                if (picked == L"Use as Brush") pickBrush(v);
                showRow(v >> 4, v);
            }
        } else if (picked == L"Make Unique (Unlink From Linked Tiles)") makeUnique();
        else if (picked == L"Remove") removePiece();
        else if (picked == L"Remove From Canvas") removeFromCanvas(place);
        else if (picked == L"Restore Removed Pieces") restoreRemoved();
    }
    // The brush goes into the target (pair mode: both halves; single: the target's half). A stroke is one undo step.
    void place(uint16_t value) {
        if (loading_) return;
        if (sel_ < 0 || target_ < 0) return setMessage(std_ >= 0 ? L"Game tilesets are read-only" : L"Select a rect first");
        auto& m = rules().terrains[size_t(sel_)].t;
        if (target_ >= int(m.pieces.size())) return;
        auto p = m.pieces[size_t(target_)];
        const auto was = p;
        if (wide_) {  // (the tile and the next one in its row)
            if ((value & 15) == 15 || tileset_.megatileFor(uint16_t(value + 1)) <= 0) return setMessage(L"Pair: subtile k+1 is empty in this row; use Single");
            p[1] = value, p[2] = uint16_t(value + 1);
        } else {
            p[size_t(1 + targetHalf_)] = value;  // (the half only: the other half's tile is the user's choice too)
        }
        if (p == was) return;
        if (!painting_ || !strokeSaved_) {
            if (!remember()) return;
            strokeSaved_ = painting_;
        }
        m.pieces[size_t(target_)] = p;
        const int linked = relink(m, was, p, size_t(target_));  // (the rows of its block follow)
        lightEdit_ = !isomrules::anyNull(was) && !isomrules::anyNull(p);  // (no piece moves: the example stays, its slots show the tiles)
        changed();  // (a complete piece moves into the set: the target follows it)
        auto half = [](uint16_t v) { return isomrules::nullHalf(v) ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15); };
        setMessage(L"Rect " + std::to_wstring(p[0]) + L": " + half(p[1]) + L" " + half(p[2]) +
                   (linked ? L"  \x00b7  " + std::to_wstring(linked) + (linked == 1 ? L" linked row follows" : L" linked rows follow") : L""));
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
    void setTarget(int k, int half, bool follow = true) {
        const auto* t = terrain();
        if (!t || k < 0 || k >= int(t->t.pieces.size())) return;
        const auto& q = t->t.pieces[size_t(k)];
        target_ = k, targetHalf_ = half >= 0 ? half : isomrules::nullHalf(q[1]) ? 0 : 1;
        uint16_t v = q[size_t(1 + targetHalf_)];  // (its tile's row in view, or the other half's: the tiles near it)
        if (isomrules::nullHalf(v)) v = q[size_t(2 - targetHalf_)];
        if (follow && !isomrules::nullHalf(v)) scrollToRow(v >> 4), row_.setValue(v >> 4);  // (the group picker follows the target)
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
    // The menu bar's menus (File, Edit, View), made again when a check mark or an item's state changes (menuKey()).
    std::string menuKey() const {
        const auto* t = terrain();
        std::string k{char('0' + wide_), char('0' + checkTodo_), char('0' + palMine_), char('0' + refOn_), char('0' + tileIds_), char('0' + frames_), char('0' + heights_),
                      char('0' + (t != nullptr)), char('0' + (t && t->t.draft)), char('0' + (t && t->t.locked)), char('0' + !undo_.empty()), char('0' + !redo_.empty())};
        for (int c = 0; c < 4; ++c) k += char('0' + colShown_[c]);
        return k;
    }
    // The menu bar, by the Windows menu conventions: File, Edit, Terrain, View; a choice of a group is a radio item
    // (a bullet), an on / off setting a check box item, a command a plain item (off while it cannot run).
    void updateMenus() {
        if (menuKey_ == menuKey() || bar_.openIndex() >= 0) return;
        menuKey_ = menuKey();
        using Item = stardraft::kit::MenuItem;
        auto item = [this](std::wstring label, std::wstring keys, std::function<void()> run, bool enabled = true) {
            Item it;
            it.label = std::move(label), it.shortcut = std::move(keys), it.enabled = enabled;
            it.run = [this, run = std::move(run)] { run(), focus(); };  // (the keys go back to the editor, not the menu bar)
            return it;
        };
        auto check = [&](std::wstring label, std::wstring keys, bool on, std::function<void()> run, bool enabled = true) {
            Item it = item(std::move(label), std::move(keys), std::move(run), enabled);
            it.checked = on;
            return it;
        };
        // (a radio item: picking the one that is on does nothing)
        auto radio = [&](std::wstring label, std::wstring keys, bool on, std::function<void()> run, bool enabled = true) {
            Item it = item(std::move(label), std::move(keys), [on, run = std::move(run)] { if (!on) run(); }, enabled);
            it.checked = on, it.radio = true;
            return it;
        };
        Item sep;
        sep.separator = true;
        const auto* t = terrain();
        const bool own = t != nullptr;  // (a terrain of the rules: a game border is read-only)
        std::vector<Item> file = {item(L"New Terrain\x2026", L"Ctrl+N", [this] { newTerrain(); }), sep,
                                  item(L"Open\x2026", L"Ctrl+O", [this] { loadFile(); }), item(L"Open Default Rules", L"", [this] { loadDefault(); }), sep,
                                  item(L"Save", L"Ctrl+S", [this] { save(false); }), item(L"Save As\x2026", L"Ctrl+Shift+S", [this] { save(true); })};
        std::vector<Item> edit = {item(L"Undo", L"Ctrl+Z", [this] { undo(false); }, !undo_.empty()),
                                  item(L"Redo", L"Ctrl+Y", [this] { undo(true); }, !redo_.empty()), sep,
                                  item(L"Set Target to NULL", L"Del", [this] { setNull(); }, own),
                                  item(L"Clear All Rects", L"", [this] { clearTerrain(); }, own),
                                  item(L"Copy Tiles From\x2026", L"", [this] { copyFrom(); }, own), sep,
                                  radio(L"Pick Tile Pair", L"Ctrl+T", wide_, [this] { toggleWidth(); }),
                                  radio(L"Pick Single Tile", L"Ctrl+T", !wide_, [this] { toggleWidth(); })};
        std::vector<Item> terrainMenu = {radio(L"Draft", L"Ctrl+D", t && t->t.draft, [this] { toggleDraft(); }, own),
                                         radio(L"Verified", L"Ctrl+D", t && !t->t.draft, [this] { toggleDraft(); }, own), sep,
                                         item(L"Next NULL Rect", L"Ctrl+L", [this] { nextNull(); }, own),
                                         item(L"Next Group Block", L"Ctrl+M", [this] { nextMyRows(); }, own), sep,
                                         item(L"Next Variant", L"Ctrl+]", [this] { stepVariant(1); }, own),
                                         item(L"Previous Variant", L"Ctrl+[", [this] { stepVariant(-1); }, own),
                                         item(L"Next Unpaired Variant", L"Ctrl+K", [this] { nextDifferingVariant(); }, own), sep,
                                         item(L"Rename Terrain\x2026", L"F2", [this] { renameTerrain(sel_); }, own),
                                         check(L"Protected", L"", t && t->t.locked, [this] { toggleProtect(sel_); }, own),
                                         item(L"Delete Terrain\x2026", L"", [this] { deleteTerrain(sel_); }, own)};
        Item ids;
        ids.label = L"Tile IDs";
        for (int m = 0; m < 3; ++m) ids.sub.push_back(radio(m == 0 ? L"Off" : m == 1 ? L"On Hover" : L"Always", L"", tileIds_ == m, [this, m] { setTileIds(m); }));
        Item cols;
        cols.label = L"Columns";
        for (int c = 0; c < 4; ++c)
            cols.sub.push_back(check(colName(c), L"", colShown_[c], [this, c] { setColumnShown(c, !colShown_[c]), saveColumns(), layout(), invalidate(); }));
        std::vector<Item> view = {radio(L"All Tile Groups", L"Ctrl+G", !palMine_, [this] { togglePalMine(); }),
                                  radio(L"This Terrain's Tile Groups", L"Ctrl+G", palMine_, [this] { togglePalMine(); }), sep,
                                  check(L"Reference Overlay", L"Ctrl+R", refOn_, [this] { toggleReference(); }),
                                  check(L"Rect Frames", L"Ctrl+H", frames_, [this] { toggleFrames(); }),
                                  check(L"Height Overlay", L"Ctrl+E", heights_, [this] { toggleHeights(); }), sep, ids, cols};
        bar_.setMenus({{L"File", std::move(file)}, {L"Edit", std::move(edit)}, {L"Terrain", std::move(terrainMenu)}, {L"View", std::move(view)}});
    }
    void setTileIds(int m) {
        tileIds_ = m, prefs::setNumber(L"Layout/IsomTileIds", tileIds_), invalidate();
        setMessage(tileIds_ == 0 ? L"Tile IDs: off" : tileIds_ == 1 ? L"Tile IDs: on hover" : L"Tile IDs: always");
    }
    // Before an edit: the terrain as it is (Ctrl+Z brings it back). False (and a message): the terrain is protected, the
    // edit does not happen.
    bool remember() {
        nudgeSlot_ = -1;  // (another undo step: the arrow keys' run ends)
        const auto* t = terrain();
        if (!t) return true;
        if (t->t.locked) {
            setMessage(widen(t->t.name) + L" is protected. To change it, right-click it in the list > Unprotect");
            return false;
        }
        undo_.push_back({t->t.id, *t, target_, targetHalf_, pins_}), redo_.clear();
        if (undo_.size() > 200) undo_.erase(undo_.begin());
        return true;
    }
    // Protect / Unprotect (the list's menu, the Terrain menu): a protected terrain takes no edits until it is unprotected.
    void toggleProtect(int i) {
        if (loading_ || i < 0 || i >= int(rules().terrains.size())) return;
        auto& m = rules().terrains[size_t(i)].t;
        endStroke(), nudgeSlot_ = -1;  // (a stroke or a Ctrl+arrow run under way: the next edit asks again)
        m.locked = !m.locked;
        dirty_ = true;
        invalidate();
        setMessage(widen(m.name) + (m.locked ? L": protected (no edits until it is unprotected)" : L": unprotected"));
    }
    void undo(bool redo) {
        nudgeSlot_ = -1;
        auto& from = redo ? redo_ : undo_;
        auto& to = redo ? undo_ : redo_;
        if (loading_) return;
        if (from.empty()) return setMessage(redo ? L"Nothing to redo" : L"Nothing to undo");
        auto& ts = rules();
        int i = -1;
        for (size_t k = 0; k < ts.terrains.size(); ++k)
            if (ts.terrains[k].t.id == from.back().id) i = int(k);
        if (i < 0) return setMessage(L"Terrain no longer exists");  // (the entry stays)
        if (ts.terrains[size_t(i)].t.locked)
            return setMessage(widen(ts.terrains[size_t(i)].t.name) + (redo ? L" is protected: no redo" : L" is protected: no undo") + L". Right-click it in the list > Unprotect");
        Edit e = std::move(from.back());
        from.pop_back();
        const bool same = sel_ == i && std_ < 0;
        if (!same) keepPins();  // (the terrain shown now: its pins wait for it)
        to.push_back({e.id, ts.terrains[size_t(i)], same ? target_ : -1, same ? targetHalf_ : 0, same ? pins_ : savedPins_[e.id]});
        pins_ = std::move(e.pins);
        ts.terrains[size_t(i)] = std::move(e.before);
        if (!same) sel_ = i, std_ = -1, variantView_ = 0, revealSideRow();
        for (size_t k = 0; k < groundIds_.size(); ++k)  // (the Bordering box: the ground as it was)
            if (groundIds_[k] == shownGround(ts.terrains[size_t(i)])) ground_.setValue(int(k));
        places_.clear(), holes_.clear(), absent_.clear(), strays_ = 0, strayTiles_.clear();  // (the piece numbers may have moved: none until the new example)
        fillGrounds();  // (a name may come back: the Bordering list's)
        target_ = e.target, targetHalf_ = e.half;
        changed();
        setMessage((redo ? L"Redo: " : L"Undo: ") + widen(ts.terrains[size_t(i)].t.name) + L" (" + std::to_wstring(nulls(ts.terrains[size_t(i)].t)) + L" NULL)");
    }
    // A fresh start for the terrain: each rect of its set and its gaps once, NULL (as a new terrain's); its fill stays.
    void clearTerrain() {
        if (loading_) return;
        if (sel_ < 0) return setMessage(std_ >= 0 ? L"Standard terrains are read only" : L"Select a terrain first");
        auto& m = rules().terrains[size_t(sel_)].t;
        if (!remember()) return;
        std::vector<std::array<uint16_t, 3>> fill(m.pieces.begin() + long(m.setPieces), m.pieces.end() - long(m.gapPieces)), gaps;
        std::set<uint16_t> games;
        for (size_t k = 0; k < m.pieces.size(); ++k)
            if ((k < m.setPieces || k + m.gapPieces >= m.pieces.size()) && games.insert(m.pieces[k][0]).second) gaps.push_back({m.pieces[k][0], 0, 1});
        m.pieces = std::move(fill);
        m.setPieces = 0, m.gapPieces = gaps.size();
        m.pieces.insert(m.pieces.end(), gaps.begin(), gaps.end());
        places_.clear(), holes_.clear(), absent_.clear(), strays_ = 0, strayTiles_.clear();
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
        const draw::Rect at = bar_.frame();
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
                    if (!copied && !remember()) return;
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
        if (loading_ || sel_ < 0) return setMessage(std_ >= 0 ? L"Game tilesets are read-only" : L"Select a terrain first");
        auto& t = rules().terrains[size_t(sel_)];
        if (!remember()) return;
        t.t.draft = !t.t.draft;
        dirty_ = true;
        familyCache_.clear(), invalidate();
        setMessage(widen(t.t.name) + (t.t.draft ? L": draft" : L": verified"));
    }
    bool lightEdit_ = false;  // (the next changed(): only a complete piece's tiles; the example is not painted again)
    void changed() {
        dirty_ = true;
        bool light = std::exchange(lightEdit_, false);
        if (auto* t = sel_ >= 0 && sel_ < int(rules().terrains.size()) ? &rules().terrains[size_t(sel_)] : nullptr) {
            // (the engine's order: complete pieces first; the target follows its piece)
            const std::array<uint16_t, 3> was = target_ >= 0 && target_ < int(t->t.pieces.size()) ? t->t.pieces[size_t(target_)] : std::array<uint16_t, 3>{0xFFFF, 0, 0};
            const std::vector<std::array<uint16_t, 3>> before = t->t.pieces;
            stardraft::terrainrules::normalize(tileset_, t->t);
            if (t->t.pieces != before) remapPins(before, t->t.pieces);
            if (was[0] != 0xFFFF) {  // (the same one of equal pieces: a copy of Make Unique and its original)
                int occ = 0;
                for (int j = 0; j < target_; ++j) occ += before[size_t(j)] == was ? 1 : 0;
                target_ = -1;
                for (size_t k = 0; k < t->t.pieces.size(); ++k)
                    if (t->t.pieces[k] == was && occ-- == 0) { target_ = int(k); break; }
            }
            if (t->t.pieces != before) places_.clear(), holes_.clear(), absent_.clear(), strays_ = 0, strayTiles_.clear(), light = false;  // (the piece numbers moved: none until the new example)
            updateTemplate();
        } else light = false;
        familyCache_.clear();
        updateMyRows();
        if (light) return syncIds(), invalidate();
        rebuild();
    }
    // The pieces' order changed (a variant set to NULL goes to the gaps): each pin follows its piece (the same tiles, the
    // same one of equal pieces: the order keeps equal pieces in their order) to its new number among the rect's pieces.
    void remapPins(const std::vector<std::array<uint16_t, 3>>& before, const std::vector<std::array<uint16_t, 3>>& after) {
        for (auto& [at, pin] : pins_) {
            const auto [g, nth] = pin;
            int seen = 0, i = -1;
            for (size_t j = 0; j < before.size() && i < 0; ++j)
                if (before[j][0] == g && seen++ == nth) i = int(j);
            if (i < 0) continue;
            int occ = 0;
            for (int j = 0; j < i; ++j) occ += before[size_t(j)] == before[size_t(i)] ? 1 : 0;
            int o = 0;
            for (const auto& p : after) {
                if (p == before[size_t(i)] && occ-- == 0) break;
                o += p[0] == g ? 1 : 0;
            }
            pin.second = o;
        }
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
        setMessage(L"Rect " + std::to_wstring(terrain()->t.pieces[size_t(next)][0]) + (visibleGroups_.count(terrain()->t.pieces[size_t(next)][0]) ? L"" : L" (not on the terrain canvas)"));
    }

    // New terrain (a small window): its template (a game border: its rects and roles, every rect NULL; or a mod terrain:
    // its template and its tiles, a copy to change), its bordering terrain and its name ("Primary (Bordering)").
    // The New Terrain window, filled (modelOf: each template item's game border, -1 - k, or terrain of the rules to copy);
    // null: no templates.
    std::unique_ptr<isomrules::NewTerrainDialog> makeNewDialog(std::vector<int>& modelOf) {
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
        auto chip = [this](uint16_t v) { return std::shared_ptr<draw::Bitmap>(v ? isomrules::tilesBitmap(tileset_, {v}, 1, 1) : nullptr); };
        std::vector<stardraft::kit::DropdownItem> models;
        modelOf.clear();
        for (const Std& d : stds) {
            models.push_back({stdName(d.k) + L"  \x00b7  " + isomrules::familyName(d.f) + L"  \x00b7  " + std::to_wstring(d.rects) + L" Rects"});
            models.back().image = chip(swatchOf(-1 - d.k)), modelOf.push_back(-1 - d.k);
        }
        for (size_t i = 0; i < ts.terrains.size(); ++i)
            if (!ts.terrains[i].t.variantOf) {
                const auto [done, total] = stardraft::terrainrules::completeness(tileset_, ts.terrains[i].t);
                models.push_back({L"Copy of " + widen(ts.terrains[i].t.name) + L"  \x00b7  " + std::to_wstring(done) + L"/" + std::to_wstring(total) + L" Rects"});
                models.back().image = chip(swatchOf(int(i))), modelOf.push_back(int(i));
            }
        if (models.empty()) return nullptr;
        std::vector<stardraft::kit::DropdownItem> grounds;
        for (size_t k = 0; k < groundIds_.size(); ++k) {
            grounds.push_back({ground_.items()[k].label});
            grounds.back().image = chip(terrainTile(groundIds_[k]));
        }
        int preset = 0;
        for (size_t m = 0; m < modelOf.size(); ++m)
            if (modelOf[m] < 0 && ((std_ >= 0 && -1 - modelOf[m] == std_) || (std_ < 0 && sel_ >= 0 && stdPairs_[size_t(-1 - modelOf[m])] == tmplPair_))) preset = int(m);
        auto groundOf = [this, modelOf](int m) {  // (a template's own bordering terrain: the ground's item)
            const int v = modelOf[size_t(m)];
            const uint16_t g = v < 0 ? stdPairs_[size_t(-1 - v)].first : rules().terrains[size_t(v)].t.ground;
            for (size_t k = 0; k < groundIds_.size(); ++k)
                if (groundIds_[k] == g) return int(k);
            return -1;
        };
        auto dlg = std::make_unique<isomrules::NewTerrainDialog>(models, preset, grounds, groundOf(preset));
        dlg->onModel = [d = dlg.get(), groundOf](int m) { d->setGround(groundOf(m)); };
        return dlg;
    }
    void newTerrain() {
        if (loading_) return setMessage(L"Tileset loading");
        auto& ts = rules();
        std::vector<int> modelOf;
        const auto made = makeNewDialog(modelOf);
        if (!made) return setMessage(L"No templates in this tileset");
        isomrules::NewTerrainDialog& dlg = *made;
        const draw::Point sz = isomrules::NewTerrainDialog::size();
        host()->runModal(*this, dlg, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect{0, 0, sz.x, sz.y}, L"New Terrain"});
        if (!dlg.created() || dlg.model() < 0 || dlg.ground() < 0) return;
        const int mv = modelOf[size_t(dlg.model())];
        const bool fromStd = mv < 0;
        const int modelIx = fromStd ? -1 - mv : mv;
        const uint16_t tplGround = fromStd ? stdPairs_[size_t(modelIx)].first : stardraft::terrainrules::templateOf(tileset_, ts.terrains[size_t(modelIx)].t).first;
        const uint16_t tplTerrain = fromStd ? stdPairs_[size_t(modelIx)].second : ts.terrains[size_t(modelIx)].t.standIn;
        const uint16_t ground = groundIds_[size_t(dlg.ground())];
        const std::wstring name = dlg.name();
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
        d.filters = {{L"Terrain rules (*.isom)", L"*.isom"}};
        const std::filesystem::path f = shellPickFile(d);
        if (f.empty()) return;
        stardraft::terrainrules::Rules r;
        std::string err;
        if (!stardraft::terrainrules::load(f, r, err)) return shellErrorBox(widen(err), L"Load terrain rules");
        doc_ = std::move(r), file_ = modsOwn(f) ? std::filesystem::path() : f, dirty_ = false;  // (the mod's own file: Save asks for another)
        openEra(era());  // (a tileset the file has no rules for: the ones in use now)
        setMessage(L"Loaded " + f.filename().wstring());
    }
    // A file in the mod's install folder (its own terrains.isom: the default rules, with the built-in ones): never
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
                d.filters = {{L"Terrain rules (*.isom)", L"*.isom"}};
                d.defaultExtension = L"isom", d.fileName = L"terrains", d.save = true;
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
        const bool first = file_.empty();
        file_ = f, dirty_ = false;
        if (first && !gSettingsOff && !cmbwIsTerrainOverride(f) &&  // (the first save of these rules: the maps can use them)
            msgbox::ask(*this, L"Save terrain rules",
                        L"Use " + f.filename().wstring() + L" as the ISOM terrain rules override? Maps then paint with its rules.\r\n"
                        L"Program options > Plugins can change this later.",
                        {L"&Use as Override", L"&Not Now"}, msgbox::Icon::Question) == 0)
            shellSetTerrainOverride(f);
        if (!gSettingsOff) prefs::setText(L"Layout/IsomRulesFile", f.wstring());  // (the next open opens it)
        // (the file the maps use (ISOM terrain rules override): the open map takes the new rules now)
        setMessage(L"Saved " + f.filename().wstring() + (cmbwReloadTerrainRules(f) ? L"; the open map uses the new rules" : L""));
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
                if (const uint16_t g = t->t.pieces[k][0]; !in.count(g)) in.insert(g), rows.push_back({g, L"Other " + std::to_wstring(g), int(k), 0});
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
        return std::to_wstring(n) + L" Terrains \x00b7 " + std::to_wstring(v) + L" Verified";
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
    // A tile into r (a NULL value: black, nothing guessed).
    void drawTile(draw::Canvas& c, uint16_t v, draw::Rect r) {
        c.fill(r, 0x000000);
        if (isomrules::nullHalf(v) || !v) return;
        auto& b = chips_[v];
        if (!b) b = isomrules::tilesBitmap(tileset_, {v}, 1, 1);
        if (b) c.bitmap(*b, {0, 0, b->width(), b->height()}, r);
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
    isomrules::EditorMenuBar bar_;
    std::string menuKey_;  // (the state the menus were made for: updateMenus)
    std::wstring status_, title_;  // (the status bar's message; the window's title)
    static int statusH() { return scale(26); }
    void setMessage(const std::wstring& m) {
        status_ = m, invalidate();
        isomProgramMessage(m);
    }
    stardraft::kit::Button newTerrain_;    stardraft::kit::CheckBox refShow_;
    // The headers' choices: the rects shown, the tile groups shown, what a click on a tile puts in (they follow the keys).
    isomrules::HeaderPick minePick_, pairPick_;
    void syncButtons() {
        if (minePick_.value() != (palMine_ ? 1 : 0)) minePick_.setValue(palMine_ ? 1 : 0);
    }
    std::vector<isomrules::TemplateRect> tmpl_;  // (the shown terrain's template: its rects by role, in the game's order)
    std::pair<uint16_t, uint16_t> tmplPair_{0, 0};  // (the template: the game ground and the game terrain on it)
    isomrules::Family family_ = isomrules::Family::Other;
    int checkScroll_ = 0, checkHover_ = -1;  // (the checklist: its scroll, the row under the mouse)
    int listHover_ = -1;  // (the rect (group) of the checklist row under the mouse: its places framed in the example)
    std::map<uint16_t, std::vector<std::pair<int, int>>> moreRects_;  // (a terrain id -> the extra spots its first example found)
    // The search's spots depend on the tileset, the terrain's ground and template, its shape and the rects it has (not
    // its tiles): the key they are kept by (v1: the search as it is now).
    std::wstring extraKeyOf(const stardraft::terrainrules::Terrain& t) const {
        std::map<uint16_t, uint32_t> want;  // (each rect, its variants: the search gives each variant a place)
        for (size_t k = 0; k < t.t.pieces.size(); ++k)
            if (k < t.t.setPieces || k + t.t.gapPieces >= t.t.pieces.size()) ++want[t.t.pieces[k][0]];
        uint32_t h = 2166136261u;
        for (const auto& [g, n] : want) h = (h ^ g) * 16777619u, h = (h ^ n) * 16777619u;
        std::map<uint16_t, uint32_t> fill;  // (the fill's rects and their variants: the cliff tops' row comes with more than 2)
        for (size_t k = t.t.setPieces; k + t.t.gapPieces < t.t.pieces.size(); ++k) ++fill[t.t.pieces[k][0]];
        for (const auto& [g, n] : fill) h = (h ^ (g | 0x10000u)) * 16777619u, h = (h ^ std::min<uint32_t>(n, 3)) * 16777619u;
        h = (h ^ uint32_t(tileset_.groups.size())) * 16777619u;  // (the tileset's groups: a mod's own numbering)
        for (uint16_t g = t.t.ground, n = 0; g >= stardraft::Tileset::kModTerrainIds && n < 16; ++n) {  // (a mod ground: its own rules, down to the game's)
            const stardraft::terrainrules::Terrain* m = nullptr;
            for (const auto& o : rules().terrains)
                if (o.t.id == g) m = &o;
            if (!m) break;
            for (uint32_t v : {uint32_t(m->t.ground), uint32_t(m->t.standIn), uint32_t(m->t.groundTemplate), uint32_t(m->t.pieces.size())}) h = (h ^ v) * 16777619u;
            g = m->t.ground;
        }
        return L"IsomExtra/v11-" + std::to_wstring(era()) + L"-" + std::to_wstring(t.t.ground) + L"-" + std::to_wstring(t.t.standIn) + L"-" +
               std::to_wstring(t.t.groundTemplate) + L"-" + std::to_wstring(int(family_)) + L"-" + std::to_wstring(h);
    }
    std::map<std::wstring, std::vector<std::pair<int, int>>> extraKnown_;  // (key -> spots, this run)
    std::optional<std::vector<std::pair<int, int>>> knownExtra(const std::wstring& key) {
        if (const auto it = extraKnown_.find(key); it != extraKnown_.end()) return it->second;
        std::wstring v;
        if (gSettingsOff || !prefs::text(key, v) || v.empty()) return std::nullopt;
        std::vector<std::pair<int, int>> spots;  // ("i,j;i,j"; "-": none)
        for (size_t at = 0; v != L"-" && at < v.size();) {
            size_t end = v.find(L';', at);
            if (end == std::wstring::npos) end = v.size();
            int i = 0, j = 0;
            if (std::swscanf(v.substr(at, end - at).c_str(), L"%d,%d", &i, &j) == 2) spots.push_back({i, j});
            at = end + 1;
        }
        return extraKnown_[key] = spots;
    }
    // The 0.5 brush's strips taken off the canvas (Remove From Canvas), by the search's key (extraKeyOf), kept for the
    // next run; Restore Removed Pieces brings them back.
    std::wstring shapeKey_;  // (the key of the canvas shown)
    std::pair<int, int> removeAt_{INT_MIN, INT_MIN};  // (the next build's Remove From Canvas: the place's tile)
    std::map<std::wstring, std::set<std::pair<int, int>>> hiddenKnown_;
    std::set<std::pair<int, int>> hiddenOf(const std::wstring& key) {
        if (const auto it = hiddenKnown_.find(key); it != hiddenKnown_.end()) return it->second;
        std::set<std::pair<int, int>> out;
        std::wstring v;
        if (!gSettingsOff && prefs::text(L"IsomHidden-" + key, v))
            for (size_t at = 0; at < v.size();) {
                size_t end = v.find(L';', at);
                if (end == std::wstring::npos) end = v.size();
                int i = 0, j = 0;
                if (std::swscanf(v.substr(at, end - at).c_str(), L"%d,%d", &i, &j) == 2) out.insert({i, j});
                at = end + 1;
            }
        return hiddenKnown_[key] = out;
    }
    void keepHidden(const std::wstring& key, const std::set<std::pair<int, int>>& spots) {
        hiddenKnown_[key] = spots;
        if (gSettingsOff) return;
        std::wstring v;
        for (const auto& [i, j] : spots) v += (v.empty() ? L"" : L";") + std::to_wstring(i) + L"," + std::to_wstring(j);
        prefs::setText(L"IsomHidden-" + key, v);
    }
    void removeFromCanvas(int place) {
        if (place < 0 || place >= int(places_.size()) || loading_) return;
        removeAt_ = {places_[size_t(place)].tx, places_[size_t(place)].ty};
        rebuild();
    }
    // The snapshot's isomrules:<name>!remove (a run without the window's timers): Remove From Canvas on the canvas's
    // places from the bottom up (the thin shapes are under the shape) until one takes a strip away, then Restore Removed
    // Pieces. The line tells the places before, after the removal and after the restore.
    std::string testRemoveStrip() {
        if (shapeKey_.empty() || places_.empty()) return "remove test: no canvas";
        const std::wstring key = shapeKey_;
        const size_t before = places_.size(), hiddenBefore = hiddenOf(key).size();
        std::string out = "remove test: places " + std::to_string(before);
        int tries = 0;
        for (int i = int(places_.size()) - 1; i >= 0 && tries < 80; --i, ++tries) {
            const Place p = places_[size_t(i)];
            if (const auto* t = terrain(); t && p.piece >= int(t->t.setPieces) && size_t(p.piece) + t->t.gapPieces < t->t.pieces.size()) {
                --tries;  // (a fill piece: the cliff tops' diamond, no strip)
                continue;
            }
            removeFromCanvas(i);
            if (hiddenOf(key).size() > hiddenBefore) {
                out += ", a strip removed at tile " + std::to_string(p.tx) + "," + std::to_string(p.ty) + " after " + std::to_string(tries + 1) +
                       " tries: places " + std::to_string(places_.size());
                restoreRemoved();
                return out + ", restored: places " + std::to_string(places_.size()) + ", removed strips " + std::to_string(hiddenOf(key).size());
            }
            if (places_.size() != before) return out + ", a refused removal changed the canvas (WRONG)";
        }
        return out + ", no strip removed in " + std::to_string(tries) + " tries";
    }
    void restoreRemoved() {
        if (shapeKey_.empty()) return;
        const size_t n = hiddenOf(shapeKey_).size();
        keepHidden(shapeKey_, {});
        rebuild();
        setMessage(L"Restored " + std::to_wstring(n) + (n == 1 ? L" removed piece" : L" removed pieces") + L" on the canvas");
    }
    void keepExtra(const std::wstring& key, const std::vector<std::pair<int, int>>& spots) {
        extraKnown_[key] = spots;
        if (gSettingsOff) return;
        std::wstring v;
        for (const auto& [i, j] : spots) v += (v.empty() ? L"" : L";") + std::to_wstring(i) + L"," + std::to_wstring(j);
        prefs::setText(key, v.empty() ? L"-" : v);
    }
    // Undo (Ctrl+Z) and redo (Ctrl+Y): a terrain as it was before an edit (its id: the list can change).
    struct Edit {
        uint16_t id = 0;
        stardraft::terrainrules::Terrain before;
        int target = -1, half = 0;
        std::map<std::pair<int, int>, std::pair<uint16_t, int>> pins;  // (the example's pins then: they follow the pieces)
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
    static constexpr bool checkTodo_ = false;  // (the checklist: every rect; a to-do filter: not for now)
    stardraft::terrainrules::Rules doc_;  // (the rules being edited: each tileset opened, or the loaded file's)
    std::filesystem::path file_;
    bool dirty_ = false;
    bool wide_ = true;  // (a pick fills both halves: the tile and the next one in its row (the default); off: one tile, the half; Ctrl+T)
    void toggleWidth() {
        wide_ = !wide_, pairPick_.setValue(wide_ ? 0 : 1), invalidate();
        setMessage(wide_ ? L"Pick: tile pair (both halves)" : L"Pick: single tile (the selected half)");
    }
    int palHover_ = -1;  // (the tile under the mouse in the tiles)
    int barEnd_ = 0;  // (the bar's controls end: the file's name after them)
    stardraft::Tileset tileset_;
    std::vector<uint16_t> groundIds_;
    std::vector<int> shown_;
    int sel_ = -1, target_ = -1, targetHalf_ = 0, hover_ = -1, hoverHalf_ = 0, hoverPlace_ = -1;
    int brush_ = -1;  // (the tile the example's slots get, -1: none; in pair mode its pair)
    bool painting_ = false, strokeSaved_ = false;  // (a paint stroke: one undo step)
    int lastPaint_ = -1;  // (the stroke's last slot: piece * 2 + half)
    int nudgeSlot_ = -1;  // (the slot the last arrow keys changed; -1: another key, a click or an edit since)
    // The piece of the slot under the mouse in the example (-1: none).
    int hoverPiece() const {
        const auto* t = terrain();
        if (!t || hoverPlace_ < 0 || hoverPlace_ >= int(places_.size())) return -1;
        const int k = places_[size_t(hoverPlace_)].piece;
        return k >= 0 && k < int(t->t.pieces.size()) ? k : -1;
    }
    draw::Rect modeBar_;  // (under the example: Brush or Selection, and what a click does)
    // The example's places pinned to a variant of their rect (Make Unique): map tile place -> the rect's group and the
    // variant's number among the group's pieces. The editor's only: the game's brush picks a variant at random.
    std::map<std::pair<int, int>, std::pair<uint16_t, int>> pins_;
    std::map<uint16_t, std::map<std::pair<int, int>, std::pair<uint16_t, int>>> savedPins_;  // (each terrain's pins while another shows)
    void keepPins() {
        if (const auto* t = terrain()) savedPins_[t->t.id] = pins_;
    }
    // --- Variants: the pieces of one rect (the game's brush picks one of them for each place) ---
    // The variants of piece k's rect: its set's and its gaps' pieces of that group (not the fill), in order.
    std::vector<int> variantsOf(int k) const {
        std::vector<int> out;
        const auto* t = terrain();
        if (!t || k < 0 || k >= int(t->t.pieces.size())) return out;
        const auto& m = t->t;
        for (size_t i = 0; i < m.pieces.size(); ++i)
            if (m.pieces[i][0] == m.pieces[size_t(k)][0] && !(i >= m.setPieces && i < m.pieces.size() - m.gapPieces)) out.push_back(int(i));
        return out;
    }
    // The variant check, as the brush places a terrain's pieces: in a block of rows of one terrain, the rect under a
    // piece takes the piece one tile row down at the same subtiles (left + 16, right + 16: the next row of the mod's
    // sheet), so that a wall's gate or door keeps its parts together; with none, it picks one at random. For each pair of
    // rects that stand one under the other on the terrain canvas (and continue each other at least once): a variant
    // that the rect below does not continue, and a variant that continues none above. Piece -> why.
    std::map<int, std::wstring> variantWhy_;
    std::vector<std::array<uint16_t, 3>> variantFor_;
    int variantEra_ = -1, variantSel_ = -1, variantPlaces_ = -1, placesRev_ = 0;
    std::set<uint16_t> variantChecked_;  // (the rects the check saw: on the canvas)
    // Two pieces one under the other as the brush continues them: b's tiles one row under a's.
    static bool continues(const std::array<uint16_t, 3>& a, const std::array<uint16_t, 3>& b) {
        return !isomrules::anyNull(a) && !isomrules::anyNull(b) && b[1] == uint16_t(a[1] + 16) && b[2] == uint16_t(a[2] + 16);
    }
    // A piece's tiles changed from `was` to `now`: the rows of its block follow (one undo step with it). The piece of the
    // rect below that continued `was` (its tiles one row down) now continues `now`, the piece above that `was` continued
    // now leads to it, and so on down and up the block, where the tiles one row down (up) are tiles of the tileset. The
    // rects that stand one under the other: the terrain canvas's. The pieces changed.
    int relink(isomrules::ModTerrain& m, const std::array<uint16_t, 3>& was, const std::array<uint16_t, 3>& now, size_t edited) {
        if (isomrules::anyNull(was) || isomrules::anyNull(now) || was == now) return 0;
        std::set<std::pair<uint16_t, uint16_t>> stacked;  // (the rect above, the rect under it)
        for (const Place& p : places_)
            if (p.piece >= 0 && p.piece < int(m.pieces.size()))
                if (const int q = placeAtTile(p.tx, p.ty - 1); q >= 0 && places_[size_t(q)].piece >= 0 && places_[size_t(q)].piece < int(m.pieces.size()))
                    stacked.insert({m.pieces[size_t(places_[size_t(q)].piece)][0], m.pieces[size_t(p.piece)][0]});
        auto tile = [&](int v) { return v >= 16 && (v >> 4) < int(tileset_.groups.size()) && tileset_.megatileFor(uint16_t(v)) > 0; };
        struct Step { std::array<uint16_t, 3> was, now; int dir; };
        std::vector<Step> todo{{was, now, 0}};
        std::set<size_t> done{edited};  // (the piece edited keeps its tiles)
        int n = 0;
        while (!todo.empty()) {
            const Step st = todo.back();
            todo.pop_back();
            // (another piece of the rect has the old tiles still (Make Unique's original): its block keeps its rows)
            if (std::find(m.pieces.begin(), m.pieces.end(), st.was) != m.pieces.end()) continue;
            for (int dir : {1, -1}) {
                if (dir == -st.dir) continue;  // (not back where it came from)
                const int d = 16 * dir;
                if (!tile(st.now[1] + d) || !tile(st.now[2] + d)) continue;
                for (size_t i = 0; i < m.pieces.size(); ++i) {
                    auto& q = m.pieces[i];
                    if (done.count(i) || !stacked.count(dir > 0 ? std::make_pair(st.was[0], q[0]) : std::make_pair(q[0], st.was[0]))) continue;
                    if (dir > 0 ? !continues(st.was, q) : !continues(q, st.was)) continue;
                    const std::array<uint16_t, 3> old = q;
                    q[1] = uint16_t(st.now[1] + d), q[2] = uint16_t(st.now[2] + d);
                    done.insert(i), ++n;
                    todo.push_back({old, q, dir});
                }
            }
        }
        return n;
    }
    // The place of the canvas at tile (tx, ty), -1 none.
    int placeAtTile(int tx, int ty) const {
        for (size_t i = 0; i < places_.size(); ++i)
            if (places_[i].tx == tx && places_[i].ty == ty) return int(i);
        return -1;
    }
    std::vector<int> piecesOfGroup(uint16_t g) const {
        std::vector<int> out;
        if (const auto* t = terrain())
            for (size_t k = 0; k < t->t.pieces.size(); ++k)
                if (t->t.pieces[k][0] == g) out.push_back(int(k));
        return out;
    }
    const std::map<int, std::wstring>& variantIssues() {
        const auto* t = terrain();
        if (!t) return variantWhy_.clear(), variantFor_.clear(), variantChecked_.clear(), variantWhy_;
        if (variantFor_ == t->t.pieces && variantSel_ == sel_ && variantEra_ == era() && variantPlaces_ == placesRev_) return variantWhy_;
        variantFor_ = t->t.pieces, variantSel_ = sel_, variantEra_ = era(), variantPlaces_ = placesRev_, variantWhy_.clear(), variantChecked_.clear();
        const auto& P = t->t.pieces;
        std::set<std::pair<uint16_t, uint16_t>> stacked;  // (the rect above, the rect under it)
        for (const Place& p : places_) {
            if (p.piece < 0 || p.piece >= int(P.size())) continue;
            variantChecked_.insert(P[size_t(p.piece)][0]);
            if (const int q = placeAtTile(p.tx, p.ty - 1); q >= 0 && places_[size_t(q)].piece >= 0 && places_[size_t(q)].piece < int(P.size()))
                stacked.insert({P[size_t(places_[size_t(q)].piece)][0], P[size_t(p.piece)][0]});
        }
        std::map<int, std::vector<std::wstring>> why;
        for (const auto& [ga, gb] : stacked) {
            const std::vector<int> as = piecesOfGroup(ga), bs = piecesOfGroup(gb);
            bool any = false;  // (no variant continues another: not a block of the mod's sheet, the brush picks freely)
            for (int a : as)
                for (int b : bs) any = any || continues(P[size_t(a)], P[size_t(b)]);
            if (!any) continue;
            for (int a : as)
                if (!isomrules::anyNull(P[size_t(a)]) && std::none_of(bs.begin(), bs.end(), [&](int b) { return continues(P[size_t(a)], P[size_t(b)]); }))
                    why[a].push_back(L"rect " + std::to_wstring(gb) + L" below has no variant that continues it (" + std::to_wstring((P[size_t(a)][1] + 16) >> 4) + L"." +
                                     std::to_wstring(P[size_t(a)][1] & 15) + L"): the brush puts a random one there");
            for (int b : bs)
                if (!isomrules::anyNull(P[size_t(b)]) && std::none_of(as.begin(), as.end(), [&](int a) { return continues(P[size_t(a)], P[size_t(b)]); }))
                    why[b].push_back(L"continues no variant of rect " + std::to_wstring(ga) + L" above (" + std::to_wstring((P[size_t(b)][1] - 16) >> 4) + L"." +
                                     std::to_wstring(P[size_t(b)][1] & 15) + L" is not one)");
        }
        for (const auto& [k, ws] : why) {
            std::wstring w;
            for (const auto& x : ws) w += (w.empty() ? L"" : L"; ") + x;
            variantWhy_[k] = w;
        }
        return variantWhy_;
    }
    // The rects (by game group) with a variant that differs.
    std::set<uint16_t> differingRects() {
        std::set<uint16_t> out;
        if (const auto* t = terrain())
            for (const auto& [k, why] : variantIssues())
                if (k < int(t->t.pieces.size())) out.insert(t->t.pieces[size_t(k)][0]);
        return out;
    }
    // Variant `piece` of its rect at every place of the rect on the terrain canvas (pinned: a new example keeps it), and
    // selected. No new picture: the slots show the piece's tiles. The strip shows every variant (also of a rect that the
    // canvas does not show).
    void showVariant(int piece) {
        const auto* t = terrain();
        if (!t || piece < 0 || piece >= int(t->t.pieces.size())) return;
        const uint16_t g = t->t.pieces[size_t(piece)][0];
        int ord = 0;  // (its number among the rect's pieces: the pins')
        for (int j = 0; j < piece; ++j) ord += t->t.pieces[size_t(j)][0] == g ? 1 : 0;
        int at = 0;
        for (Place& p : places_)
            if (p.piece >= 0 && p.piece < int(t->t.pieces.size()) && t->t.pieces[size_t(p.piece)][0] == g) p.piece = piece, pins_[{p.tx, p.ty}] = {g, ord}, ++at;
        // (the block it stands in follows, as the brush continues it: under it the piece one row down at the same
        // subtiles, above it the one a row up, where the rects have them)
        const auto& P = t->t.pieces;
        auto pin = [&](Place& n, int k) {
            int o = 0;
            for (int j = 0; j < k; ++j) o += P[size_t(j)][0] == P[size_t(k)][0] ? 1 : 0;
            n.piece = k, pins_[{n.tx, n.ty}] = {P[size_t(k)][0], o};
        };
        for (size_t i = 0; i < places_.size(); ++i) {
            if (places_[i].piece != piece) continue;
            for (int dir : {1, -1}) {
                int from = piece, ty = places_[i].ty;
                for (;;) {  // (each row of a block continues the row above it)
                    ty += dir;
                    const int q = placeAtTile(places_[i].tx, ty);
                    if (q < 0 || places_[size_t(q)].piece < 0 || places_[size_t(q)].piece >= int(P.size()) || P[size_t(places_[size_t(q)].piece)][0] == g) break;
                    int next = -1;
                    for (int k : piecesOfGroup(P[size_t(places_[size_t(q)].piece)][0]))
                        if (dir > 0 ? continues(P[size_t(from)], P[size_t(k)]) : continues(P[size_t(k)], P[size_t(from)])) next = k;
                    if (next < 0) break;
                    pin(places_[size_t(q)], next), from = next;
                }
            }
        }
        syncIds();
        setTarget(piece, -1), slotFocus(true);
        const std::vector<int> vs = variantsOf(piece);
        const int n = int(std::find(vs.begin(), vs.end(), piece) - vs.begin()) + 1;
        const auto& why = variantIssues();
        const auto it = why.find(piece);
        setMessage(L"Rect " + std::to_wstring(g) + L": variant " + std::to_wstring(n) + L" of " + std::to_wstring(vs.size()) +
                   (at ? L" at its " + std::to_wstring(at) + (at == 1 ? L" place" : L" places") : std::wstring(L" (not on the terrain canvas: the strip shows it)")) +
                   (it != why.end() ? L"  \x00b7  " + it->second : L""));
    }
    // Ctrl+] / Ctrl+[: the target's rect shows its next (previous) variant.
    void stepVariant(int dir) {
        if (!terrain() || target_ < 0) return setMessage(std_ >= 0 ? L"Game tilesets are read-only" : L"Select a rect first");
        const std::vector<int> vs = variantsOf(target_);
        const auto it = std::find(vs.begin(), vs.end(), target_);
        if (vs.size() < 2 || it == vs.end()) return setMessage(L"Rect " + std::to_wstring(terrain()->t.pieces[size_t(target_)][0]) + L" has one variant");
        const int n = int(vs.size()), i = int(it - vs.begin());
        showVariant(vs[size_t(((i + dir) % n + n) % n)]);
    }
    // Ctrl+K: the next variant that differs from its rect's usual one, shown.
    void nextDifferingVariant() {
        if (!terrain()) return setMessage(std_ >= 0 ? L"Game tilesets are read-only" : L"Select a terrain first");
        const auto& why = variantIssues();
        if (why.empty()) return setMessage(L"Every variant on the terrain canvas continues the one above it");
        auto it = why.upper_bound(target_);
        if (it == why.end()) it = why.begin();
        showVariant(it->first);
    }
    std::vector<std::pair<draw::Rect, int>> variantChips_;  // (the strip's variants: where each is, its piece)
    // Piece k's tiles as they show: the target's as a click in the Tiles would make them while the mouse is on a tile there
    // (a selected slot: the pair, or (single) the tile in its half), else as they are.
    std::array<uint16_t, 3> shownPiece(int k) const {
        const auto* t = terrain();
        std::array<uint16_t, 3> q = t->t.pieces[size_t(k)];
        if (k != target_ || palHover_ < 0 || !slotKeys_ || brushValue() >= 0) return q;
        if (!wide_) q[size_t(1 + targetHalf_)] = uint16_t(palHover_);
        else if ((palHover_ & 15) < 15 && tileset_.megatileFor(uint16_t(palHover_ + 1)) > 0) q[1] = uint16_t(palHover_), q[2] = uint16_t(palHover_ + 1);
        return q;
    }
    // A slot clicked in the example: Ctrl+arrow keys change its tile to the next one in the tiles; its frame blinks.
    bool slotKeys_ = false, blinkOn_ = true;
    int blinkId_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // (false once destroyed: the blink's timer may still run)
    void slotFocus(bool on) {
        slotKeys_ = on, blinkOn_ = true;
        if (on) armBlink();
        invalidate();
    }
    void armBlink() {
        if (blinkId_ || !host()) return;
        std::shared_ptr<bool> alive = alive_;
        blinkId_ = host()->timer(1000, [this, alive] {
            if (!*alive) return;
            blinkId_ = 0;
            blinkOn_ = slotKeys_ ? !blinkOn_ : true;
            invalidate();
            if (slotKeys_) armBlink();
        });
    }
    // The target's tile goes to the next tile of the tiles: dx along its row (pair mode: a pair), dy to the next row shown
    // (the same subtile). Empty tiles are skipped.
    void nudge(int dx, int dy) {
        const auto* t = terrain();
        if (!t || target_ < 0 || target_ >= int(t->t.pieces.size())) return;
        const auto& q = t->t.pieces[size_t(target_)];
        const uint16_t v = wide_ ? q[1] : q[size_t(1 + targetHalf_)];
        if (isomrules::nullHalf(v)) return setMessage(L"The slot is NULL: paint a tile first");
        const int step = wide_ ? 2 : 1, s = wide_ ? (v & 15) & ~1 : v & 15;
        auto ok = [&](int g, int k) {
            return g >= 0 && g < int(tileset_.groups.size()) && k >= 0 && k + step - 1 < 16 && tileset_.megatileFor(uint16_t(g << 4 | k)) > 0 &&
                   (!wide_ || tileset_.megatileFor(uint16_t(g << 4 | (k + 1))) > 0);
        };
        int to = -1;
        if (dx) {
            for (int k = s + dx * step; k >= 0 && k < 16 && to < 0; k += dx * step)
                if (ok(v >> 4, k)) to = (v >> 4) << 4 | k;
        } else if (const int at = palIndexOf(v >> 4); at >= 0) {  // (the rows the tiles show)
            for (int i = at + dy; i >= 0 && i < palRowCount() && to < 0; i += dy)
                if (ok(palGroupAt(i), s)) to = palGroupAt(i) << 4 | s;
        } else {
            for (int g = (v >> 4) + dy; g >= 0 && g < int(tileset_.groups.size()) && to < 0; g += dy)
                if (ok(g, s)) to = g << 4 | s;
        }
        if (to < 0) return setMessage(dx ? L"No more tiles in this row" : L"No more rows with this tile");
        if (painting_) place(uint16_t(to));  // (a paint stroke under way: its undo step)
        else {
            painting_ = true, strokeSaved_ = nudgeSlot_ == target_;  // (the presses on one slot: one undo step, as a stroke)
            place(uint16_t(to));
            painting_ = strokeSaved_ = false, nudgeSlot_ = target_;
        }
        scrollToRow(to >> 4), row_.setValue(to >> 4);
        blinkOn_ = true, invalidate();
    }
    int brushValue() const { return brush_ < 0 ? -1 : wide_ ? brush_ & ~1 : brush_; }
    std::vector<Place> places_;
    std::vector<uint16_t> ids_;     // (the example's tiles, crop_ row by row: the tile IDs, with the edits since)
    std::vector<uint16_t> picIds_;  // (the tiles of the picture as it was painted)
    // The slots' tiles in ids_ as the pieces have them now (after an edit that does not paint the example again).
    void syncIds() {
        const auto* t = terrain();
        if (!t || ids_.size() != size_t(crop_.width()) * size_t(crop_.height())) return;
        for (const Place& p : places_)
            if (p.piece >= 0 && p.piece < int(t->t.pieces.size()))
                for (int half = 0; half < 2; ++half) {
                    const int cx = p.tx + half - crop_.left, cy = p.ty - crop_.top;
                    if (cx < 0 || cy < 0 || cx >= crop_.width() || cy >= crop_.height()) continue;
                    const uint16_t v = t->t.pieces[size_t(p.piece)][size_t(1 + half)];
                    ids_[size_t(cy) * size_t(crop_.width()) + size_t(cx)] = isomrules::nullHalf(v) ? kNullId : v;
                }
    }
    int tileIds_ = 0, idHover_ = -1;
    bool frames_ = true;  // (the example's rect frames: holes red, borrowed blue, the hovered rect; H. The target's stays)
    void toggleFrames() {
        frames_ = !frames_, prefs::setNumber(L"Layout/IsomRectFrames", frames_ ? 1 : 0), invalidate();
        setMessage(frames_ ? L"Rect frames: on" : L"Rect frames: off");
    }  // (tileIds_: the example's tile IDs 0 off, 1 on hover, 2 always; idHover_: the cell in ids_)
    bool heights_ = false;  // (the terrain canvas's height overlay: each minitile's VF4 height; Ctrl+E)
    void toggleHeights() {
        heights_ = !heights_, prefs::setNumber(L"Layout/IsomHeights", heights_ ? 1 : 0), invalidate();
        setMessage(heights_ ? L"Height overlay: low (blue), middle (yellow), high (red); not walkable: dark" : L"Height overlay: off");
    }
    // The height of minitile `m` (0-15) of tile v: 0 no tile; 1-3 low, middle, high; +3 not walkable.
    int heightKind(uint16_t v, int m) const {
        const int mega = v == kNullId ? -1 : tileset_.megatileFor(v);
        if (mega <= 0 || size_t(mega) >= tileset_.minitileFlags.size()) return 0;
        const uint16_t f = tileset_.minitileFlags[size_t(mega)][size_t(m)];
        return ((f & 4) ? 3 : (f & 2) ? 2 : 1) + ((f & 1) ? 0 : 3);
    }
    static constexpr draw::Color kHeightColors[7] = {0, 0xFF8040, 0x30C8E8, 0x2850F0, 0x804020, 0x186474, 0x142878};  // (COLORREF)
    static uint8_t heightAlpha(int k) { return k > 3 ? 150 : 110; }
    // The height overlay of one tile in r (a cell of the Tiles): its 4 x 4 minitiles, runs of one color on each row.
    void paintTileHeights(draw::Canvas& c, uint16_t v, draw::Rect r) {
        for (int my = 0; my < 4; ++my)
            for (int mx = 0; mx < 4;) {
                const int k = heightKind(v, my * 4 + mx);
                int end = mx + 1;
                while (end < 4 && heightKind(v, my * 4 + end) == k) ++end;
                if (k)
                    c.fill({r.left + r.width() * mx / 4, r.top + r.height() * my / 4, r.left + r.width() * end / 4, r.top + r.height() * (my + 1) / 4},
                           kHeightColors[k], heightAlpha(k));
                mx = end;
            }
    }
    // The height overlay over the terrain canvas: each minitile (8 px) of the tiles in ids_ (the edits too) in the color of
    // its VF4 height, runs of one color on each minitile row as one fill. Not walkable: a darker fill.
    void paintHeights(draw::Canvas& c, int ox, int oy) {
        const int cw = crop_.width(), ch = crop_.height();
        if (ids_.size() != size_t(cw) * size_t(ch)) return;
        const float ms = 8 * zoom_;
        auto kindAt = [&](int mx, int my) { return heightKind(ids_[size_t(my / 4) * size_t(cw) + size_t(mx / 4)], (my % 4) * 4 + mx % 4); };
        for (int my = 0; my < ch * 4; ++my)
            for (int mx = 0; mx < cw * 4;) {
                const int k = kindAt(mx, my);
                int end = mx + 1;
                while (end < cw * 4 && kindAt(end, my) == k) ++end;
                if (k)
                    c.fill({ox + int(mx * ms), oy + int(my * ms), ox + int(end * ms), oy + int((my + 1) * ms)}, kHeightColors[k], heightAlpha(k));
                mx = end;
            }
    }
    std::vector<int> holes_, absent_;
    std::unique_ptr<draw::Bitmap> picture_;
    struct Spot { int x, y; float s; };
    std::unique_ptr<Editor> gameEd_;  // (the game tilesets' map: the game borders and the reference; the worker's)
    size_t gameEdGroups_ = 0;         // (its group limit)
    std::unique_ptr<Editor> puzzleEd_;  // (the examples' map, kept between rebuilds: its unit data loads once; the worker's)
    std::vector<std::unique_ptr<Editor>> helpers_;  // (more maps for the search's trials, one for each core: the worker's)
    int helpersFor_ = -1;                           // (the build they were made ready for)
    // The threads that paint the search's trials, one for each map of helpers_ (they stay for the window: their caches
    // stay warm). run(f) calls f(k) on each thread k and returns when all are done.
    class TrialPool {
    public:
        explicit TrialPool(size_t n) {
            for (size_t k = 0; k < n; ++k)
                threads_.emplace_back([this, k] {
                    uint64_t seen = 0;
                    for (;;) {
                        std::function<void(size_t)> job;
                        {
                            std::unique_lock<std::mutex> l(m_);
                            wake_.wait(l, [&] { return stop_ || id_ != seen; });
                            if (stop_) return;
                            seen = id_, job = job_;
                        }
                        job(k);
                        std::lock_guard<std::mutex> l(m_);
                        if (--busy_ == 0) done_.notify_all();
                    }
                });
        }
        ~TrialPool() {
            {
                std::lock_guard<std::mutex> l(m_);
                stop_ = true;
            }
            wake_.notify_all();
            for (auto& t : threads_) t.join();
        }
        void run(std::function<void(size_t)> f) {
            std::unique_lock<std::mutex> l(m_);
            job_ = std::move(f), busy_ = threads_.size(), ++id_;
            wake_.notify_all();
            done_.wait(l, [&] { return busy_ == 0; });
        }

    private:
        std::vector<std::thread> threads_;
        std::mutex m_;
        std::condition_variable wake_, done_;
        std::function<void(size_t)> job_;
        uint64_t id_ = 0;
        size_t busy_ = 0;
        bool stop_ = false;
    };
    std::unique_ptr<TrialPool> pool_;  // (after helpers_: it goes first)
    std::map<std::tuple<uint8_t, uint16_t, uint16_t>, std::array<bool, 256>> groundColors_;  // (the ground's palette colors, by its tileset, root and ground: the worker's)
    std::map<uint64_t, std::vector<std::pair<int, int>>> cleaned_;  // (a shape with its strays gone, by the shape and the pieces' sides: the worker's)
    bool async_ = true, loading_ = false, building_ = false;
    // The example's progress, as the worker tells it: the step (kStep...), its round, done of total in it.
    enum : int { kStepPaint, kStepSearch, kStepReference, kStepPicture };
    std::atomic<int> progStep_{kStepPaint}, progRound_{0}, progDone_{0}, progTotal_{0};
    std::chrono::steady_clock::time_point busySince_;  // (a short build shows no bar)
    int busyTimer_ = 0;
    void progress(int step, int done = 0, int total = 0, int round = 0) { progStep_ = step, progDone_ = done, progTotal_ = total, progRound_ = round; }
    // While the tileset loads or the example renders: a paint every 100 ms (the bar moves).
    void armBusy() {
        if (busyTimer_ || !host() || !async_) return;
        std::shared_ptr<bool> alive = alive_;
        busyTimer_ = host()->timer(100, [this, alive] {
            if (!*alive) return;
            busyTimer_ = 0;
            if (loading_ || building_) invalidate(), armBusy();
        });
    }
    int openGen_ = 0, shownId_ = -1, jump_ = -1;  // (jump_: the tile framed in the tiles; jumpRow_: its whole row)
    bool jumpRow_ = false;
    std::atomic<int> buildGen_{0};  // (the newest example asked for: the worker leaves out older ones)
    draw::Rect crop_, puzzle_, palette_, strip_, check_, refRow_;
    int strays_ = 0;  // (the strays of the canvas shown: BuildOut::strays)
    std::vector<std::pair<int, int>> strayTiles_;  // (their tiles: the environment's STARDRAFT_SHOW_STRAYS frames them red, for the sweep's pictures)
    // The four columns (Terrains, Rects, Example, Tiles): their order and which show, kept in the layout settings
    // (Layout/IsomColumns: the order in 2-bit digits, the shown bits from bit 8). Each has a header; its right-click
    // menu moves or hides it (a hidden one comes back from any header's menu or the Menu button).
    enum Col { kColTerrains = 0, kColRects = 1, kColExample = 2, kColTiles = 3 };
    static constexpr int kDefaultOrder[4] = {kColTerrains, kColRects, kColExample, kColTiles};
    int colOrder_[4] = {kColTerrains, kColRects, kColExample, kColTiles};
    bool colShown_[4] = {true, true, true, true};
    draw::Rect colHead_[4], colBody_[4];  // (empty while hidden)
    // Every edge between two columns drags: the width of the column beside it that is not the flexible one (the example,
    // else the last column shown, takes what the others leave). Kept in the layout settings (Layout/IsomColW<c>, unscaled).
    struct Edge {
        int x = 0, a = 0, b = 0;  // (the columns left and right of it)
    };
    std::vector<Edge> edges_;
    static constexpr int kDefaultW[4] = {260, 300, 600, 16 * 26 + 24 + 14};  // (unscaled)
    int colW_[4] = {260, 300, 600, 16 * 26 + 24 + 14};
    int dragEdge_ = -1, hoverEdge_ = -1, flex_ = -1, colsTop_ = 0;
    static int minColW(bool flex) { return scale(flex ? 300 : 120); }
    int edgeAt(draw::Point p) const {
        if (p.y < colsTop_ || p.y >= height() - statusH()) return -1;  // (the columns' height only)
        for (size_t i = 0; i < edges_.size(); ++i)
            if (std::abs(p.x - edges_[i].x) <= scale(4)) return int(i);
        return -1;
    }
    bool laidOut_ = false;
    static const wchar_t* colName(int c) { return c == kColTerrains ? L"Terrains" : c == kColRects ? L"Rects" : c == kColExample ? L"Terrain Canvas" : L"Tiles"; }
    int colAt(draw::Point p) const {
        for (int c = 0; c < 4; ++c)
            if (colShown_[c] && colHead_[c].contains(p)) return c;
        return -1;
    }
    void saveWidths() {
        for (int c = 0; c < 4; ++c) prefs::setNumber(L"Layout/IsomColW" + std::to_wstring(c), colW_[c]);
    }
    void saveColumns() {
        int v = 0;
        for (int i = 0; i < 4; ++i) v |= colOrder_[i] << (2 * i);
        for (int c = 0; c < 4; ++c) v |= (colShown_[c] ? 1 : 0) << (8 + c);
        prefs::setNumber(L"Layout/IsomColumns", v);
    }
    void loadColumns() {
        int v = 0;
        for (int c = 0; c < 4; ++c)
            if (int w = 0; prefs::number(L"Layout/IsomColW" + std::to_wstring(c), w, 120, 4000)) colW_[c] = w;
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
        std::vector<std::wstring> items = {L"Move Left", L"Move Right", L"Hide"};
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
    float viewZoom_ = 1;  // (the wheel's zoom over the example: 1 fits it, up to 8; the tiles nearest neighbor)
    int panX_ = 0, panY_ = 0;  // (the example moved from the middle while zoomed in: a middle-button drag)
    bool panning_ = false;
    draw::Point panFrom_{};
    // The example's top left on the view (zoom_: the fit times the wheel's zoom); a picture larger than the area stays
    // over it, a smaller one in its middle.
    void exampleOrigin(int& ox, int& oy) {
        const int pad = scale(8);
        const float fit = std::max(0.1f, std::min(std::min(float(puzzle_.width() - 2 * pad) / float(picture_->width()), float(puzzle_.height() - 2 * pad) / float(picture_->height())), 2.0f));
        zoom_ = fit * viewZoom_;
        const int pw = int(float(picture_->width()) * zoom_), ph = int(float(picture_->height()) * zoom_);
        const int cx = puzzle_.left + (puzzle_.width() - pw) / 2, cy = puzzle_.top + (puzzle_.height() - ph) / 2;
        ox = pw > puzzle_.width() ? std::clamp(cx + panX_, puzzle_.right - pw, puzzle_.left) : cx;
        oy = ph > puzzle_.height() ? std::clamp(cy + panY_, puzzle_.bottom - ph, puzzle_.top) : cy;
        panX_ = ox - cx, panY_ = oy - cy;
    }
    int palScroll_ = 0;
    // The palette's rows: every group, or (Ctrl+G) only the terrain's own groups (myRows_, sorted); the header above the rows.
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
    // The palette: each group's 16 subtiles in a row, in a fine grid (the lines between pairs 0-1, 2-3, ... brighter).
    int palLeft() const { return palette_.left + scale(38); }  // (the group numbers' margin: 4 digits)
    int palCell() const { return std::max(scale(8), (palette_.right - palLeft() - scale(2)) / 16); }
    int palX(int sub) const { return palLeft() + sub * palCell(); }
    // The subtile under x (in pair mode the pair's left one); -1 outside the tiles.
    int palSubAt(int x) const {
        const int dx = x - palLeft();
        if (dx < 0 || dx >= 16 * palCell()) return -1;
        const int sub = dx / palCell();
        return wide_ ? sub & ~1 : sub;
    }
    int palScrollMax() const { return std::max(0, palRowCount() * palCell() - palette_.height()); }
    void togglePalMine() {
        if (!palMine_ && palRows_.empty()) return setMessage(L"No groups assigned to this terrain yet");
        palMine_ = !palMine_, palScroll_ = 0;
        setMessage(palMine_ ? L"Tiles: this terrain's groups" : L"Tiles: all groups");
        invalidate();
    }
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
        const isomrules::ModTerrain* variant = variantOfRow(r.value);
        const bool selected = variant ? variantView_ == variant->id : (sel_ >= 0 && r.value == sel_ && !variantView_) || (std_ >= 0 && r.value == -1 - std_);
        if (selected) c.fill({side_.left, y, side_.right, y + r.h}, kSel);
        else if (int(i) == sideHover_) c.fill({side_.left, y, side_.right, y + r.h}, kCtlHot);
        const int sw = r.h - scale(12);
        if (variant) {  // (a variant: under its terrain, the terrain's tiles on the variant's ground)
            const int x = side_.left + pad + sw + scale(8);
            c.fill({side_.left + pad + sw / 2, y, side_.left + pad + sw / 2 + scale(1), y + r.h}, kBorder);
            drawTile(c, terrainTile(variant->ground), {x - scale(4) - sw / 2, y + (r.h - sw / 2) / 2, x - scale(4), y + (r.h + sw / 2) / 2});
            c.text({x, y + scale(4), side_.right - pad, y + scale(22)}, widen(variant->name), selected ? kText : kDim, draw::kMiddle | draw::kEllipsis);
            const int p = indexOfId(variant->variantOf);
            c.text({x, y + scale(22), side_.right - pad, y + scale(36)}, L"Variant  \x00b7  the tiles of " + (p >= 0 ? widen(rules().terrains[size_t(p)].t.name) : std::wstring(L"?")), kDim,
                   draw::kMiddle | draw::kEllipsis);
            continue;
        }
        drawTile(c, swatchOf(r.value), {side_.left + pad, y + scale(6), side_.left + pad + sw, y + scale(6) + sw});
        const int x = side_.left + pad + sw + scale(8);
        if (r.value < 0) {
            c.text({x, y, side_.right - pad, y + r.h}, stdName(-1 - r.value), selected ? kText : kDim, draw::kMiddle | draw::kEllipsis);
            continue;
        }
        const auto& m = rules().terrains[size_t(r.value)].t;
        const auto [done, total] = stardraft::terrainrules::completeness(tileset_, m);
        c.text({x, y + scale(4), side_.right - pad - (m.draft ? scale(40) : 0), y + scale(22)}, widen(m.name), selected ? kText : kText, draw::kMiddle | draw::kEllipsis);
        if (m.draft) c.text({side_.right - pad - scale(38), y + scale(4), side_.right - pad, y + scale(22)}, L"Draft", kDraft, draw::kMiddle | draw::kRight);
        c.text({x, y + scale(22), side_.right - pad - (m.locked ? scale(64) : 0), y + scale(36)}, isomrules::familyName(familyOfValue(r.value)) + L"  \x00b7  " + std::to_wstring(done) + L"/" + std::to_wstring(total) + L" Rects",
               kDim, draw::kMiddle | draw::kEllipsis);
        if (m.locked) c.text({side_.right - pad - scale(64), y + scale(22), side_.right - pad, y + scale(36)}, L"Protected", kAccent, draw::kMiddle | draw::kRight);
        const int bx0 = x, bx1 = side_.right - pad, by = y + r.h - scale(5);  // (the progress: done of total)
        c.fill({bx0, by, bx1, by + scale(2)}, kBorder);
        if (total > 0) c.fill({bx0, by, bx0 + (bx1 - bx0) * done / total, by + scale(2)}, done == total ? kAccent : kDraft);
    }
    c.popClip();
    c.fill({side_.left, side_.top, side_.right, side_.top + 1}, kBorder);
}

void IsomRulesView::layout() {
    const int pad = scale(8), barH = scale(stardraft::kit::kButtonH), W = width(), H = height(), headH = scale(34), barW = scale(stardraft::kit::kScrollBarW);
    laidOut_ = true;
    const int menuH = scale(28);  // (the menu bar, as the main window's)
    bar_.setFrame({0, 0, W, menuH});
    int x = pad;
    auto put = [&](draw::View& v, int w) { v.setFrame({x, menuH + pad, x + w, menuH + pad + barH}), x += w + pad; };
    x += scale(52);  // ("Tileset", painted)
    put(era_, scale(150));
    barEnd_ = x;
    const int top = menuH + pad * 2 + barH;
    // The columns in their order: each its own width, the flexible one (the example, else the last shown) the rest.
    int flex = colShown_[kColExample] ? int(kColExample) : -1;
    for (int i = 3; i >= 0 && flex < 0; --i)
        if (colShown_[colOrder_[i]]) flex = colOrder_[i];
    flex_ = flex, colsTop_ = top;
    int fixed = 0, shownFixed = 0;
    for (int c = 0; c < 4; ++c)
        if (colShown_[c] && c != flex) fixed += scale(colW_[c]), ++shownFixed;
    // (a window too narrow for the widths: the fixed columns give way in proportion, the flexible one keeps its least)
    const int room = std::max(shownFixed * minColW(false), W - minColW(true));
    auto widthOf = [&](int c) { return fixed > room ? std::max(minColW(false), scale(colW_[c]) * room / fixed) : scale(colW_[c]); };
    int used = 0;
    for (int c = 0; c < 4; ++c)
        if (colShown_[c] && c != flex) used += widthOf(c);
    const int flexW = std::max(minColW(true), W - used);
    int cx = 0;
    edges_.clear();
    int prev = -1;
    for (int i = 0; i < 4; ++i) {
        const int c = colOrder_[i];
        if (!colShown_[c]) {
            colHead_[c] = colBody_[c] = {0, 0, 0, 0};
            continue;
        }
        if (prev >= 0) edges_.push_back({cx, prev, c});
        const int w = c == flex ? flexW : widthOf(c);
        colHead_[c] = {cx, top, cx + w, top + headH}, colBody_[c] = {cx, top + headH, cx + w, H - statusH()};
        cx += w, prev = c;
    }
    {  // (the header buttons: at the header's right)
        const int bw = scale(170), by = top + (headH - barH) / 2;
        const draw::Rect& r = colHead_[kColRects];
        const draw::Rect& t = colHead_[kColTiles];
        minePick_.setFrame(t.width() > 0 ? draw::Rect{std::max(t.left + scale(60), t.right - pad - bw), by, t.right - pad, by + barH} : draw::Rect{0, 0, 0, 0});
        const int pr = minePick_.frame().left - pad;  // (the pick's width when there is room for it; else in Edit and Ctrl+T only)
        pairPick_.setFrame(t.width() > 0 && pr - scale(120) >= t.left + scale(60) ? draw::Rect{pr - scale(120), by, pr, by + barH} : draw::Rect{0, 0, 0, 0});
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
            modeBar_ = {b.left, strip_.top - scale(24), b.right, strip_.top};
            puzzle_ = {b.left, b.top + scale(8) + barH, b.right, modeBar_.top};
            int rx = b.left + pad;
            const int ry = refRow_.top + pad / 2;
            refShow_.setFrame({rx, ry, rx + scale(130), ry + barH}), rx += scale(130) + pad;
            refOpacity_.setFrame({rx, ry + scale(4), rx + scale(90), ry + barH - scale(4)}), rx += scale(90) + pad;
            refPick_.setFrame({rx, ry, std::max(rx + scale(60), std::min(rx + scale(240), b.right - pad)), ry + barH});  // (its own width, not the row's)
        } else puzzle_ = strip_ = refRow_ = modeBar_ = none, ground_.setFrame(none), refShow_.setFrame(none), refOpacity_.setFrame(none), refPick_.setFrame(none);
    }
    {  // Tiles: the group picker, the palette with its bar at its right.
        const draw::Rect& b = colBody_[kColTiles];
        if (b.width() > 0) {
            row_.setFrame({b.left + pad, b.top + pad, b.right - pad, b.top + pad + barH});
            const int py = b.top + pad * 2 + barH;
            palette_ = {b.left, py, b.right - barW, b.bottom}, palBar_.setFrame({b.right - barW, py, b.right, b.bottom});
        } else palette_ = none, palBar_.setFrame(none), row_.setFrame(none);
    }
}

void IsomRulesView::paint(draw::Canvas& c) {
    if (refsDue_ && !refPick_.listOpen()) refsDue_ = false, fillRefs();  // (the Reference list closed: its new tiles)
    c.nearest(true);  // (the tiles, the terrain canvas zoomed in: sharp pixels)
    paintBody(c);
    c.nearest(false);
    if (modeBar_.width() > 0 && terrain()) {  // (the terrain view's mode)
        const int pad = scale(8), b = brushValue();
        c.fill(modeBar_, kPanel), c.fill({modeBar_.left, modeBar_.top, modeBar_.right, modeBar_.top + 1}, kBorder);
        const auto* t = terrain();
        const bool sel = target_ >= 0 && target_ < int(t->t.pieces.size());
        const std::wstring mode = b >= 0 ? L"Brush" : L"Selection";
        std::wstring what = b >= 0 ? std::to_wstring(b >> 4) + L"." + std::to_wstring(b & 15) + L"-" + std::to_wstring((b & 15) + 1) + L"  \x00b7  Click or drag on slots to paint. Esc: clear"
                                : sel   ? L"Rect " + std::to_wstring(t->t.pieces[size_t(target_)][0]) + L"  \x00b7  Ctrl+arrows or a click in Tiles set its tiles; Shift+click: the whole row. Esc: clear"
                                        : std::wstring(kBrushMode ? L"Click a slot to select it, or a tile in Tiles to paint with it" : L"Click a slot to select it");
        const int mw = c.measure(mode) + 2 * pad;
        int right = modeBar_.right - pad;
        if (hoverPiece() >= 0) {  // (the slot under the mouse: its rect, role and tiles in place of the hint)
            const auto& q = t->t.pieces[size_t(hoverPiece())];
            std::wstring role;
            for (const CheckRow& r : checkRows())
                if (r.group == q[0]) { role = isomrules::titleCase(r.role); break; }
            auto tile = [](uint16_t v) { return isomrules::nullHalf(v) ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15); };
            const std::wstring info = L"Rect " + std::to_wstring(q[0]) + (role.empty() ? L"" : L"  \x00b7  " + role) + L"  \x00b7  " + tile(q[1]) + L" " + tile(q[2]);
            what = info, right = -1;
        }
        c.fill({modeBar_.left + pad / 2, modeBar_.top + scale(4), modeBar_.left + pad / 2 + mw, modeBar_.bottom - scale(3)}, b >= 0 ? 0x30C8FF : kAccent, 60);
        c.text({modeBar_.left + pad / 2, modeBar_.top, modeBar_.left + pad / 2 + mw, modeBar_.bottom}, mode, 0xFFFFFF, draw::kCenter | draw::kMiddle);
        c.text({modeBar_.left + mw + pad, modeBar_.top, modeBar_.right - pad, modeBar_.bottom}, what, right < 0 ? kText : kDim, draw::kMiddle | draw::kEllipsis);
    }
    // The lines between the columns, over everything: the one under the mouse or dragged is wide and in the accent.
    for (size_t i = 0; i < edges_.size(); ++i) {
        const int x = edges_[i].x;
        const bool hot = int(i) == dragEdge_ || (dragEdge_ < 0 && int(i) == hoverEdge_);
        if (hot) c.fill({x - scale(2), colsTop_, x + scale(2), height() - statusH()}, kAccent);
        else c.fill({x, colsTop_, x + 1, height() - statusH()}, kBorderHot);
    }
}

void IsomRulesView::paintBody(draw::Canvas& c) {
    const int W = width(), H = height(), pad = scale(8);
    const draw::Color kBorrowed = 0xFFA05A, kHole = 0x4650E6;  // (COLORREF: a blue, a red)
    (void)H;
    c.fill({0, 0, W, H}, kBg);
    if (!laidOut_) layout();
    updateMenus(), syncButtons();
    const int menuH = bar_.frame().bottom;
    c.fill({0, menuH, W, menuH + 1}, kBorder);
    c.text({pad, menuH + pad, era_.frame().left - scale(6), menuH + pad + scale(stardraft::kit::kButtonH)}, L"Tileset", kDim, draw::kMiddle);
    {  // The status bar: the last message at the left; the file and its state at the right.
        const draw::Rect sb{0, H - statusH(), W, H};
        c.fill(sb, kPanel), c.fill({0, sb.top, W, sb.top + 1}, kBorder);
        const std::wstring file = file_.empty() ? std::wstring(L"Untitled") : file_.filename().wstring();
        // (the file edited: its whole path; its name alone when the path does not fit)
        std::wstring shown = file_.empty() ? std::wstring(L"Rules in use (no file: Save As to keep them)") : file_.wstring();
        if (c.measure(shown) + 2 * pad > W / 2 && !file_.empty()) shown = file;
        shown += cmbwIsTerrainOverride(file_) ? L"  \x00b7  used by maps" : L"";
        const int fw = std::min(W / 2, c.measure(shown) + 2 * pad);
        c.text({sb.right - fw, sb.top, sb.right - pad, sb.bottom}, shown, kText, draw::kMiddle | draw::kRight | draw::kEllipsis);
        // (the title bar, as Windows programs show a file: an asterisk while it has unsaved changes)
        if (const std::wstring title = (dirty_ ? L"*" : L"") + file + L" - Custom Isom Terrain Editor"; title != title_) title_ = title, shellSetIsomRulesTitle(title);
        c.text({pad, sb.top, sb.right - fw - pad, sb.bottom}, status_.empty() ? std::wstring(L"Ready") : status_, kDim, draw::kMiddle | draw::kEllipsis);
    }
    const auto* t = terrain();
    // The columns' headers: the name at the left, the column's own switch at the right (the right-click menu moves or hides).
    for (int col = 0; col < 4; ++col) {
        if (!colShown_[col]) continue;
        const draw::Rect& h = colHead_[col];
        c.fill(h, kPanel);
        c.fill({h.left, h.bottom - 1, h.right, h.bottom}, kBorder);
        c.text({h.left + pad, h.top, h.right - pad, h.bottom}, colName(col), kText, draw::kMiddle);
        std::wstring right;
        draw::Color tone = kDim;
        if (col == kColTerrains) right = verifiedText();
        else if (col == kColExample && t) right = isomrules::familyName(family_) + L" \x00b7 " + templateName();
        if (!right.empty()) c.text({h.left + pad, h.top, h.right - pad, h.bottom}, right, tone, draw::kMiddle | draw::kRight | draw::kEllipsis);
    }
    if (side_.width() > 0) paintSide(c);
    if (puzzle_.width() > 0) {
        c.text({puzzle_.left + pad, puzzle_.top - scale(stardraft::kit::kButtonH) - pad, puzzle_.left + scale(88), puzzle_.top - pad}, L"Bordering", kDim, draw::kMiddle);
        if (t || std_ >= 0)
            c.text({std::min(puzzle_.right - pad, ground_.frame().right + pad), puzzle_.top - scale(stardraft::kit::kButtonH) - pad, puzzle_.right - pad, puzzle_.top - pad},
                   t ? std::to_wstring(stardraft::terrainrules::completeness(tileset_, t->t).first) + L"/" + std::to_wstring(tmpl_.size()) + (t->t.draft ? L" \x00b7 Draft" : L" \x00b7 Verified")
                     : std::to_wstring(tmpl_.size()) + L" Rects \x00b7 Game Border",
                   kDim, draw::kMiddle | draw::kRight | draw::kEllipsis);
    }
    // The puzzle: the examples, fit to the area; NULL holes framed red, the target orange, the hovered piece's places.
    c.fill(puzzle_, 0x151515);
    if (puzzle_.width() > 0 && picture_ && (t || std_ >= 0)) {
        const bool refShown = refOn_ && refPicture_ != nullptr, refAlone = refShown && refOpacity_.value() >= 100;
        int ox = 0, oy = 0;
        exampleOrigin(ox, oy);
        c.pushClip(puzzle_);
        const int pw = int(float(picture_->width()) * zoom_), ph = int(float(picture_->height()) * zoom_);
        c.bitmap(*picture_, {0, 0, picture_->width(), picture_->height()}, {ox, oy, ox + pw, oy + ph});
        const float ts = 32 * zoom_;
        auto rectOf = [&](const Place& p) {
            const int x = ox + int((p.tx - crop_.left) * ts), y = oy + int((p.ty - crop_.top) * ts);
            return draw::Rect{x, y, x + int(2 * ts), y + int(ts)};
        };
        auto halfOf = [](draw::Rect r, int half) {
            return half ? draw::Rect{(r.left + r.right) / 2, r.top, r.right, r.bottom} : draw::Rect{r.left, r.top, (r.left + r.right) / 2, r.bottom};
        };
        for (const Place& p : places_) {  // (each slot's working tiles: a NULL half black (the reference shows through
            // it), a tile changed since the example was painted over it)
            if (!t || refAlone) break;
            if (p.piece < 0 || p.piece >= int(t->t.pieces.size())) continue;
            const auto& q = t->t.pieces[size_t(p.piece)];
            for (int half = 0; half < 2; ++half) {
                const uint16_t v = q[size_t(1 + half)];
                const int cx = p.tx + half - crop_.left, cy = p.ty - crop_.top;
                const size_t i = size_t(cy) * size_t(crop_.width()) + size_t(cx);
                if (isomrules::nullHalf(v)) drawTile(c, 0, halfOf(rectOf(p), half));
                else if (cx >= 0 && cy >= 0 && cx < crop_.width() && i < picIds_.size() && picIds_[i] != v) drawTile(c, v, halfOf(rectOf(p), half));
            }
        }
        if (refShown) c.bitmap(*refPicture_, {0, 0, refPicture_->width(), refPicture_->height()}, {ox, oy, ox + pw, oy + ph}, false, uint8_t(255 * refOpacity_.value() / 100));
        if (heights_ && !refAlone) paintHeights(c, ox, oy);
        if (static const bool showStrays = std::getenv("STARDRAFT_SHOW_STRAYS") != nullptr; showStrays)  // (tools/isom_canvas_sweep.py)
            for (const auto& [x, y] : strayTiles_) {
                const int sx = ox + int((x - crop_.left) * ts), sy = oy + int((y - crop_.top) * ts);
                c.frame({sx, sy, sx + int(ts), sy + int(ts)}, 0x2020FF, scale(2));
            }
        for (const Place& p : places_)  // (the slots with a NULL half: red frames while the reference covers their black)
            if (t && refShown && !refAlone && frames_ && p.piece >= 0 && p.piece < int(t->t.pieces.size()) && isomrules::anyNull(t->t.pieces[size_t(p.piece)]))
                c.frame(rectOf(p), 0x4650E6, scale(2));
        for (const Place& p : places_)  // (the hovered rect's places, over the reference too)
            if (frames_ && hover_ >= 0 && p.piece == hover_ && p.piece != target_) c.frame(rectOf(p), kBorderHot, scale(1));  // (not the selected one: its blue frame)
        if (t && !refAlone && frames_)
            for (const Place& p : places_)  // (a borrowed rect: the game's own tiles, a blue frame)
                if (p.piece >= 0 && p.piece < int(t->t.pieces.size()) && stardraft::terrainrules::stateOf(t->t.pieces[size_t(p.piece)]) == isomrules::PieceState::Borrowed)
                    c.frame(rectOf(p), kBorrowed, scale(1));
        if (const int b = brushValue(); t && b >= 0 && hover_ >= 0 && !painting_ && !refAlone)  // (the brush, as it would sit in the slot under the mouse)
            for (const Place& p : places_)
                if (p.piece == hover_) {
                    if (wide_) {
                        drawTile(c, uint16_t(b), halfOf(rectOf(p), 0));
                        if ((b & 15) < 15) drawTile(c, uint16_t(b + 1), halfOf(rectOf(p), 1));
                    } else {
                        drawTile(c, uint16_t(b), halfOf(rectOf(p), hoverHalf_));
                    }
                    c.frame(wide_ ? rectOf(p) : halfOf(rectOf(p), hoverHalf_), 0xFFFFFF, scale(1));
                }
        if (t && palHover_ >= 0 && (!wide_ || ((palHover_ & 15) < 15 && tileset_.megatileFor(uint16_t(palHover_ + 1)) > 0)) && brushValue() < 0 && slotKeys_ &&
            target_ >= 0 && !refAlone)  // (a selected slot: what a click in the Tiles puts in, in it: the pair, or (single) the tile in its half)
            for (const Place& p : places_)
                if (p.piece == target_) {
                    if (!wide_) {
                        drawTile(c, uint16_t(palHover_), halfOf(rectOf(p), targetHalf_));
                        continue;
                    }
                    drawTile(c, uint16_t(palHover_), halfOf(rectOf(p), 0));
                    drawTile(c, uint16_t(palHover_ + 1), halfOf(rectOf(p), 1));
                }
        if (t)
            for (const Place& p : places_)  // (the rect under the mouse in the Rects column: its places)
            if (listHover_ >= 0 && p.piece >= 0 && p.piece < int(t->t.pieces.size()) && t->t.pieces[size_t(p.piece)][0] == listHover_) c.frame(rectOf(p), 0xFFFFFF, scale(2));
        if (hoverPlace_ >= 0 && hoverPlace_ < int(places_.size()) && brushValue() < 0 && !refAlone &&
            (places_[size_t(hoverPlace_)].piece != target_ || (!wide_ && hoverHalf_ != targetHalf_))) {  // (the cursor: the slot under the mouse; pair mode 2x1; the selected one: its blue frame only)
            const draw::Rect r = rectOf(places_[size_t(hoverPlace_)]);
            c.frame(wide_ ? r : halfOf(r, hoverHalf_), 0xFFFFFF, scale(1));
        }
        for (const Place& p : places_)
            if (p.piece == target_ && target_ >= 0 && (!slotKeys_ || blinkOn_)) c.frame(wide_ ? rectOf(p) : halfOf(rectOf(p), targetHalf_), kAccent, scale(1));
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
            c.setFont(of);
            if (tileIds_ == 1 && idHover_ >= 0 && size_t(idHover_) < ids_.size()) {  // (on hover: a tag in the UI's large text over the tile)
                const size_t i = size_t(idHover_);
                const int x = ox + int(float(i % size_t(crop_.width())) * ts), y = oy + int(float(i / size_t(crop_.width())) * ts);
                const uint16_t v = ids_[i];
                const std::wstring text = v == kNullId ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15);
                const draw::Font big = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)}));
                const int tw = c.measure(text) + scale(16), th = scale(30), cx = x + int(ts) / 2;
                int ty = y - th - scale(4);
                if (ty < puzzle_.top) ty = y + int(ts) + scale(4);
                const draw::Rect box{std::clamp(cx - tw / 2, puzzle_.left, std::max(puzzle_.left, puzzle_.right - tw)), ty, 0, ty + th};
                const draw::Rect tag{box.left, box.top, box.left + tw, box.bottom};
                draw::Rect cur{x, y, x + int(ts), y + int(ts)};  // (pair mode: the rect's two tiles)
                if (wide_) {
                    const int tx = crop_.left + int(i % size_t(crop_.width())), ty0 = crop_.top + int(i / size_t(crop_.width()));
                    for (const Place& p : places_)
                        if (p.ty == ty0 && (tx == p.tx || tx == p.tx + 1)) { cur = rectOf(p); break; }
                }
                c.frame(cur, 0xFFFFFF, scale(1));
                c.fill(tag, 0x000000, 210);
                c.frame(tag, kBorder, scale(1));
                c.text(tag, text, v == kNullId ? kHole : 0xFFFFFF, draw::kCenter | draw::kMiddle);
                c.setFont(big);
            }
        }
        c.popClip();
    } else if (puzzle_.width() > 0 && !loading_ && !building_) {
        c.text(puzzle_, t ? L"No terrain canvas (mod data missing)" : L"Select a terrain", kDim, draw::kCenter | draw::kMiddle);
    }
    // (the tileset loading or the example rendering, after a moment: a bar and what goes on, in the middle)
    if (puzzle_.width() > 0 && (loading_ || building_) && (!picture_ || std::chrono::steady_clock::now() - busySince_ > std::chrono::milliseconds(300))) {
        const int bw = std::min(scale(440), puzzle_.width() - 2 * pad), bh = scale(12), mx = (puzzle_.left + puzzle_.right) / 2, my = (puzzle_.top + puzzle_.bottom) / 2;
        const draw::Rect panel{mx - bw / 2 - pad, my - scale(26), mx + bw / 2 + pad, my + scale(34)};
        if (picture_) c.fill(panel, kPanel, 230), c.frame(panel, kBorder, scale(1));
        const draw::Rect bar{mx - bw / 2, my - bh / 2 - scale(8), mx + bw / 2, my + bh / 2 - scale(8)};
        c.fill(bar, kBorder);
        const int done = progDone_, total = progTotal_;
        // (one bar for the whole render: each step its part, so it only goes on: paint 0-5 %, the spot search 5-85 % (round r
        // the half of what is left: the rounds' number is not known), the reference 85-95 %, the picture 95-100 %)
        float part = 0;
        switch (int(progStep_)) {
        case kStepPaint: part = 0.02f; break;
        case kStepSearch: {
            const int r = std::max(1, int(progRound_));
            const float from = 1.0f - std::pow(0.5f, float(r - 1)), to = 1.0f - std::pow(0.5f, float(r));
            part = 0.05f + 0.80f * (from + (to - from) * (total > 0 ? float(std::min(done, total)) / float(total) : 0.0f));
            break;
        }
        case kStepReference: part = 0.90f; break;
        default: part = 0.97f; break;
        }
        if (!loading_) c.fill({bar.left, bar.top, bar.left + int(float(bar.width()) * part), bar.bottom}, kAccent);
        else {  // (no count: a block that goes across)
            const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - busySince_).count());
            const int w = bw / 4, x = bar.left + (ms / 4) % (bw + w) - w;
            c.fill({std::max(bar.left, x), bar.top, std::min(bar.right, x + w), bar.bottom}, kAccent);
        }
        std::wstring label;
        switch (loading_ ? -1 : int(progStep_)) {
        case -1: label = L"Loading tileset: CV5, VX4EX, VR4, terrain rules"; break;
        case kStepPaint: label = L"ISOM paint: terrain canvas"; break;
        case kStepSearch:
            label = L"Spot search: round " + std::to_wstring(int(progRound_)) + L" \x00b7 spot " + std::to_wstring(done) + L"/" + std::to_wstring(total) +
                    L" \x00b7 rects not on canvas";
            break;
        case kStepReference: label = L"ISOM paint: reference"; break;
        default: label = L"Render: TILE to bitmap"; break;
        }
        c.text({puzzle_.left + pad, bar.bottom + scale(6), puzzle_.right - pad, bar.bottom + scale(28)}, label, kDim, draw::kCenter | draw::kMiddle | draw::kEllipsis);
    }
    // The checklist (the Rects column): every rect of the template by its role, the template's tiles, the custom tiles,
    // its state; the target's row marked. The column names on top.
    if (check_.width() > 0) {
        c.fill(check_, kPanel);
        const std::vector<CheckRow> rows = checkRows();
        const std::set<uint16_t> differing = differingRects();
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
            const bool isTarget = t && target_ >= 0 && row.piece >= 0 && t->t.pieces[size_t(target_)][0] == row.group;
            // (the target's row: the target (a variant too), with the tile under the mouse in the Tiles in it)
            const std::array<uint16_t, 3> rowPiece = isTarget ? shownPiece(target_) : t && row.piece >= 0 && row.piece < int(t->t.pieces.size()) ? t->t.pieces[size_t(row.piece)] : std::array<uint16_t, 3>{};
            const std::array<uint16_t, 3>* p = t && row.piece >= 0 && row.piece < int(t->t.pieces.size()) ? &rowPiece : nullptr;
            const isomrules::PieceState st = p ? stardraft::terrainrules::stateOf(*p) : isomrules::PieceState::Null;
            if (isTarget) c.fill({check_.left, y, check_.right - 1, y + rh}, kSel);
            else if (int(i) == checkHover_ || (t && hoverPiece() >= 0 && t->t.pieces[size_t(hoverPiece())][0] == row.group))
                c.fill({check_.left, y, check_.right - 1, y + rh}, kCtlHot);  // (the row under the mouse, or the rect under it in the example)
            const bool shown = visibleGroups_.count(row.group) > 0 || std_ >= 0;
            c.text({x0, y, xt - scale(4), y + rh}, isomrules::titleCase(row.role), isTarget ? kAccent : shown ? kText : kDim, draw::kMiddle | draw::kEllipsis);
            const int ty = y + (rh - th) / 2;
            drawTile(c, gameTile(row.group), {xt, ty, xt + th, ty + th});
            drawTile(c, gameTile(uint32_t(row.group) + 1), {xt + th, ty, xt + 2 * th, ty + th});
            c.frame({xt, ty, xt + 2 * th, ty + th}, kBorder, scale(1));
            if (t) {
                drawTile(c, p ? (*p)[1] : 0, {xc, ty, xc + th, ty + th});
                drawTile(c, p ? (*p)[2] : 0, {xc + th, ty, xc + 2 * th, ty + th});
                if (isTarget && slotKeys_ && blinkOn_)  // (the selected slot: its blinking frame, as on the canvas)
                    c.frame(wide_ ? draw::Rect{xc, ty, xc + 2 * th, ty + th} : targetHalf_ ? draw::Rect{xc + th, ty, xc + 2 * th, ty + th} : draw::Rect{xc, ty, xc + th, ty + th}, kAccent, scale(2));
                c.frame({xc, ty, xc + 2 * th, ty + th}, st == isomrules::PieceState::Own ? kBorder : st == isomrules::PieceState::Borrowed ? kBorrowed : kHole, scale(1));
                // (a variant that differs: in place of Custom or Borrowed; NULL and Partial stay in sight)
                const bool differs = differing.count(row.group) > 0 && (st == isomrules::PieceState::Own || st == isomrules::PieceState::Borrowed);
                const std::wstring state = differs ? L"Unpaired"
                                           : st == isomrules::PieceState::Own ? L"Custom" : st == isomrules::PieceState::Borrowed ? L"Borrowed" : st == isomrules::PieceState::Partial ? L"Partial" : L"NULL";
                c.text({xs, y, check_.right - pad, y + rh}, state, differs ? draw::Color(0x50AAF0) : st == isomrules::PieceState::Own ? kDim : st == isomrules::PieceState::Borrowed ? kBorrowed : kHole,
                       draw::kMiddle | draw::kEllipsis);
            }
        }
        c.popClip();
    }
    // The target's strip: the game's tiles of its rect (the groups the mod's tiles stand in for) beside its own, large.
    c.fill({strip_.left, strip_.top, strip_.right, strip_.top + 1}, kBorder);
    c.fill({refRow_.left, refRow_.top, refRow_.right, refRow_.top + 1}, kBorder);
    variantChips_.clear();
    if (strip_.width() > 0 && t && target_ >= 0 && target_ < int(t->t.pieces.size())) {
        const std::array<uint16_t, 3> q = shownPiece(target_);  // (with the tile under the mouse in the Tiles)
        const int T = scale(44), y0 = strip_.top + pad + scale(16);
        int x = strip_.left + pad;
        auto half = [](uint16_t v) { return isomrules::nullHalf(v) ? std::wstring(L"NULL") : std::to_wstring(v >> 4) + L"." + std::to_wstring(v & 15); };
        c.text({x, strip_.top + pad, x + scale(104), y0}, isomrules::titleCase(roleOf(q[0])), kText, draw::kMiddle | draw::kEllipsis);
        if (roleOf(q[0]) != L"rect " + std::to_wstring(q[0])) c.text({x, y0, x + scale(104), y0 + scale(20)}, L"Rect " + std::to_wstring(q[0]), kFaint, draw::kMiddle);
        c.text({x, y0 + scale(20), x + scale(104), y0 + scale(40)}, half(q[1]) + L"  " + half(q[2]), kDim, draw::kMiddle);
        c.text({x + scale(112), strip_.top + pad, x + scale(112) + 2 * T, y0}, L"Template", kDim, draw::kMiddle);
        c.text({x + scale(112) + 2 * T + scale(24), strip_.top + pad, strip_.right - pad, y0}, L"Custom", kDim, draw::kMiddle);
        x += scale(112);
        drawTile(c, gameTile(q[0]), {x, y0, x + T, y0 + T}), drawTile(c, gameTile(uint32_t(q[0]) + 1), {x + T, y0, x + 2 * T, y0 + T});
        c.frame({x, y0, x + 2 * T, y0 + T}, kBorder, scale(1));
        c.text({x + 2 * T, y0, x + 2 * T + scale(24), y0 + T}, L"\x2192", kDim, draw::kCenter | draw::kMiddle);
        x += 2 * T + scale(24);
        drawTile(c, q[1], {x, y0, x + T, y0 + T}), drawTile(c, q[2], {x + T, y0, x + 2 * T, y0 + T});
        if (heights_) paintTileHeights(c, q[1], {x, y0, x + T, y0 + T}), paintTileHeights(c, q[2], {x + T, y0, x + 2 * T, y0 + T});
        c.frame({x, y0, x + 2 * T, y0 + T}, kAccent, scale(1));
        if (!wide_) c.frame(targetHalf_ ? draw::Rect{x + T, y0, x + 2 * T, y0 + T} : draw::Rect{x, y0, x + T, y0 + T}, kAccent, scale(2));
        x += 2 * T + scale(16);
        if (const std::vector<int> vs = variantsOf(target_); vs.size() >= 2 && x + scale(40) <= strip_.right - pad) {  // (the rect's variants: a click shows one)
            const auto& why = variantIssues();
            const int differ = int(std::count_if(vs.begin(), vs.end(), [&](int k) { return why.count(k) > 0; }));
            const draw::Color kDiffer = 0x50AAF0;  // (COLORREF: the Draft orange)
            const bool checked = variantChecked_.count(t->t.pieces[size_t(target_)][0]) > 0;
            c.text({x, strip_.top + pad, strip_.right - pad, y0}, L"Variants " + std::to_wstring(vs.size()) + (differ ? L"  \x00b7  " + std::to_wstring(differ) + L" unpaired" : checked ? L"" : L"  \x00b7  not on the canvas"),
                   differ ? kDiffer : kDim, draw::kMiddle | draw::kEllipsis);
            const int ch = (T - scale(4)) / 2, cw = 2 * ch, gap = scale(4), cols = (strip_.right - pad - x + gap) / (cw + gap);
            for (size_t i = 0; i < vs.size() && int(i) < 2 * cols; ++i) {
                const int k = vs[i];
                const auto& v = t->t.pieces[size_t(k)];
                const int cx = x + int(i) % cols * (cw + gap), cy = y0 + int(i) / cols * (ch + gap);
                const draw::Rect r{cx, cy, cx + cw, cy + ch};
                if (int(i) == 2 * cols - 1 && int(vs.size()) > 2 * cols) {  // (more than fit: how many more)
                    c.text(r, L"+" + std::to_wstring(vs.size() - i), kDim, draw::kCenter | draw::kMiddle);
                    break;
                }
                drawTile(c, v[1], {r.left, r.top, r.left + ch, r.bottom}), drawTile(c, v[2], {r.left + ch, r.top, r.right, r.bottom});
                if (heights_) paintTileHeights(c, v[1], {r.left, r.top, r.left + ch, r.bottom}), paintTileHeights(c, v[2], {r.left + ch, r.top, r.right, r.bottom});
                c.frame(r, k == target_ ? kAccent : why.count(k) ? kDiffer : kBorder, scale(k == target_ || why.count(k) ? 2 : 1));
                variantChips_.push_back({r, k});
            }
        }
    } else if (strip_.width() > 0) {
        c.text({strip_.left + pad, strip_.top, strip_.left + scale(300), strip_.bottom}, L"Select a rect: template vs custom tiles", kDim, draw::kMiddle);
    }
    // The palette (the Tiles column): a tile group a row, its 16 subtiles; the target's tiles framed.
    if (palette_.width() <= 0) return;
    c.fill(palette_, kPanel);
    const int cell = palCell(), x0 = palLeft();
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
    const int gridR = palX(16);
    c.fill({x0, palette_.top, gridR + 1, std::min(palette_.bottom, palette_.top + rows * cell - palScroll_ + 1)}, kBorder);  // (the grid's lines)
    auto tileAt = [&](int sub, int y) { return draw::Rect{palX(sub) + 1, y + 1, palX(sub) + cell, y + cell}; };  // (a cell inside its lines)
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
            if (b)
                for (int k = 0; k < 16; ++k) c.bitmap(*b, {k * 32, (g % 64) * 32, k * 32 + 32, (g % 64) * 32 + 32}, tileAt(k, y));
        } else {
            auto& b = rowPics_[g];
            if (!b) {
                std::vector<uint16_t> tiles;
                for (int s = 0; s < 16; ++s) tiles.push_back(tileset_.megatileFor(uint16_t(g << 4 | s)) > 0 ? uint16_t(g << 4 | s) : 0);
                b = isomrules::tilesBitmap(tileset_, tiles, 16, 1);
            }
            if (b)
                for (int k = 0; k < 16; ++k) c.bitmap(*b, {k * 32, 0, k * 32 + 32, 32}, tileAt(k, y));
        }
        if (heights_)  // (the height overlay (Ctrl+E) over the Tiles too)
            for (int k = 0; k < 16; ++k)
                if (tileset_.megatileFor(uint16_t(g << 4 | k)) > 0) paintTileHeights(c, uint16_t(g << 4 | k), tileAt(k, y));
    }
    for (int k = 0; k <= 16; k += 2) c.fill({palX(k), palette_.top, palX(k) + 1, std::min(palette_.bottom, palette_.top + rows * cell - palScroll_ + 1)}, kBorderHot);
    auto rowY = [&](int g) { const int i = palIndexOf(g); return i < 0 ? INT_MIN : palette_.top + i * cell - palScroll_; };
    if (t && target_ >= 0 && target_ < int(t->t.pieces.size()))
        for (int half = 1; half <= 2; ++half) {  // (pair mode: one 2x1 frame, the pair of the left tile (else of the right))
            const auto& q = t->t.pieces[size_t(target_)];
            if (wide_ && half == 2 && !isomrules::nullHalf(q[1])) break;
            const uint16_t v = wide_ ? uint16_t(q[size_t(half)] & ~1) : q[size_t(half)];
            if (isomrules::nullHalf(q[size_t(half)]) || rowY(v >> 4) == INT_MIN) continue;
            const int y = rowY(v >> 4), x = palX(v & 15);
            c.frame({x, y, x + (wide_ ? 2 : 1) * cell + 1, y + cell + 1}, kAccent, scale(2));
            if (wide_) break;
        }
    if (palHover_ >= 0 && rowY(palHover_ >> 4) != INT_MIN) {  // (what a click picks: 1 or 2 tiles)
        const int y = rowY(palHover_ >> 4), x = palX(palHover_ & 15);
        c.frame({x, y, x + (wide_ && (palHover_ & 15) < 15 ? 2 : 1) * cell + 1, y + cell + 1}, 0xFFFFFF, scale(1));
    }
    if (brush_ >= 0 && rowY(brush_ >> 4) != INT_MIN) {  // (the brush)
        const int v = brushValue(), y = rowY(v >> 4), x = palX(v & 15);
        c.frame({x - scale(1), y - scale(1), x + (wide_ ? 2 : 1) * cell + 1 + scale(1), y + cell + 1 + scale(1)}, 0x30C8FF, scale(3));
    }
    if (jump_ >= 0 && rowY(jump_ >> 4) != INT_MIN) {  // (the row or the tile that was asked for)
        const int y = rowY(jump_ >> 4);
        const int x = jumpRow_ ? x0 : palX(wide_ ? jump_ & 14 : jump_ & 15), w = jumpRow_ ? palX(15) + cell - x0 : (wide_ ? 2 : 1) * cell;
        c.frame({x - 1, y - 1, x + w + 1, y + cell + 1}, 0xFFFFFF, scale(2));  // (white: the brush's frame is yellow)
    }
    c.popClip();
}

bool IsomRulesView::mouse(const draw::MouseEvent& e) {
    const auto* t = terrain();
    const int cell = palCell(), x0 = palLeft();
    if (e.kind == draw::MouseKind::Down && e.button == 0)
        for (const auto& [r, k] : variantChips_)
            if (r.contains(e.pt)) return showVariant(k), true;  // (a variant in the strip: shown at the rect's places)
    if (e.kind == draw::MouseKind::Wheel && puzzle_.contains(e.pt) && picture_) {  // (the example: zoom at the mouse, the fit at least)
        int ox = 0, oy = 0;
        exampleOrigin(ox, oy);
        const float before = zoom_, px = float(e.pt.x - ox) / before, py = float(e.pt.y - oy) / before;  // (the picture's point under the mouse)
        viewZoom_ = std::clamp(viewZoom_ * std::pow(1.25f, draw::wheelDistance(e, 40.0f) / 40.0f), 1.0f, 8.0f);
        int nx = 0, ny = 0;
        exampleOrigin(nx, ny);  // (zoom_ now the new one)
        panX_ += int(float(e.pt.x) - px * zoom_) - nx, panY_ += int(float(e.pt.y) - py * zoom_) - ny;
        exampleOrigin(nx, ny);  // (clamped)
        invalidate();
        return true;
    }
    if (e.button == 2 && (e.kind == draw::MouseKind::Down || panning_)) {  // (the middle button: the example moves while zoomed in)
        if (e.kind == draw::MouseKind::Down && puzzle_.contains(e.pt)) panning_ = true, panFrom_ = e.pt;
        else if (e.kind == draw::MouseKind::Up) panning_ = false;
        return true;
    }
    if (panning_ && e.kind == draw::MouseKind::Move) {
        panX_ += e.pt.x - panFrom_.x, panY_ += e.pt.y - panFrom_.y, panFrom_ = e.pt;
        invalidate();
        return true;
    }
    if (e.kind == draw::MouseKind::Wheel && palette_.contains(e.pt)) {
        palScroll_ = std::clamp(palScroll_ - int(draw::wheelDistance(e, float(cell * 3))), 0, palScrollMax());
        invalidate();
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 0 && dragEdge_ < 0) {  // (an edge between two columns: drag its width)
        if (const int k = edgeAt(e.pt); k >= 0) {
            dragEdge_ = k;
            if (host()) host()->capture(*this, true);
            return true;
        }
    }
    if (dragEdge_ >= 0 && e.kind == draw::MouseKind::Move) {  // (a splitter: the two columns beside the edge, the others stay)
        const Edge g = edges_[size_t(dragEdge_)];
        const int left = colBody_[g.a].left, right = colBody_[g.b].right;
        const int x = std::clamp(e.pt.x, left + minColW(g.a == flex_), right - minColW(g.b == flex_));
        auto unscaled = [](int w) { return w * 100 / scale(100); };
        if (g.a != flex_) colW_[g.a] = unscaled(x - left);
        if (g.b != flex_) colW_[g.b] = unscaled(right - x);
        const int k = dragEdge_;
        layout(), invalidate();
        dragEdge_ = k < int(edges_.size()) ? k : -1;
        return true;
    }
    if (dragEdge_ >= 0 && e.kind == draw::MouseKind::Up) {
        dragEdge_ = -1, hoverEdge_ = edgeAt(e.pt), saveWidths();
        if (host()) host()->capture(*this, false);
        invalidate();
        return true;
    }
    if (painting_ && e.kind == draw::MouseKind::Up) {  // (the stroke's end, wherever the button goes up)
        endStroke();
        return true;
    }
    if (const int col = colAt(e.pt); col >= 0) {  // (a column's header: the right button, its menu)
        if (e.kind == draw::MouseKind::Up && e.button == 1) return columnMenu(col, e.pt), true;
        return e.kind == draw::MouseKind::Down;
    }
    if (e.kind == draw::MouseKind::Wheel && side_.contains(e.pt)) {
        sideScroll_ = std::clamp(sideScroll_ - int(draw::wheelDistance(e, float(scale(44) * 2))), 0, std::max(0, sideTotal_ - side_.height()));
        invalidate();
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 1 && side_.contains(e.pt) && !loading_) {  // (a terrain's row: Rename, Delete)
        const int sy = e.pt.y - side_.top + sideScroll_;
        for (const SideRow& r : sideRows_)
            if (r.value != kSideHeader && sy >= r.y && sy < r.y + r.h) {
                if (isVariantRow(r.value)) return selectValue(r.value), true;  // (a variant: the terrain's own row renames and deletes)
                if (r.value < 0) return selectValue(r.value), setMessage(L"Game tilesets are read-only"), true;
                selectValue(r.value);
                const bool locked = rules().terrains[size_t(r.value)].t.locked;
                switch (shellContextMenuAt(*this, e.pt, {L"Rename\x2026", L"Delete\x2026", locked ? L"Unprotect" : L"Protect"})) {
                case 1: renameTerrain(r.value); break;
                case 2: deleteTerrain(r.value); break;
                case 3: toggleProtect(r.value); break;
                default: break;
                }
                return true;
            }
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 0 && side_.contains(e.pt)) {  // (a terrain's row: shown)
        const int sy = e.pt.y - side_.top + sideScroll_;
        for (const SideRow& r : sideRows_)
            if (r.value != kSideHeader && sy >= r.y && sy < r.y + r.h) {
                if (!loading_) {
                    selectValue(r.value);
                    if (const auto* tt = terrain()) {  // (the Tiles: the row the terrain's tiles use most, at the top)
                        std::map<int, int> uses;
                        for (const auto& q : tt->t.pieces)
                            for (int h = 1; h <= 2; ++h)
                                if (!isomrules::nullHalf(q[size_t(h)])) ++uses[q[size_t(h)] >> 4];
                        int g = -1, most = 0;
                        for (const auto& [row, n] : uses)
                            if (n > most) g = row, most = n;
                        if (const int i = g >= 0 ? palIndexOf(g) : -1; i >= 0) {
                            palScroll_ = std::clamp(i * palCell() - scale(4), 0, palScrollMax());
                            row_.setValue(g);
                        }
                    }
                }
                return true;
            }
        return true;
    }
    if (e.kind == draw::MouseKind::Wheel && check_.contains(e.pt)) {
        checkScroll_ = std::max(0, checkScroll_ - int(draw::wheelDistance(e, float(scale(kCheckRow) * 2))));
        invalidate();
        return true;
    }
    // (the examples' place under the point: its rect, and the half)
    auto placeAt = [&](draw::Point pt, int& half) -> int {
        if (!picture_ || !puzzle_.contains(pt)) return -1;
        int ox = 0, oy = 0;
        exampleOrigin(ox, oy);
        const int tx = crop_.left + int(float(pt.x - ox) / (32 * zoom_)), ty = crop_.top + int(float(pt.y - oy) / (32 * zoom_));
        for (size_t i = 0; i < places_.size(); ++i)
            if (places_[i].ty == ty && (tx == places_[i].tx || tx == places_[i].tx + 1)) return half = tx - places_[i].tx, int(i);
        return -1;
    };
    if (e.kind == draw::MouseKind::Leave) {
        if (idHover_ >= 0 || hoverEdge_ >= 0 || palHover_ >= 0 || hoverPlace_ >= 0 || checkHover_ >= 0)
            idHover_ = -1, hoverEdge_ = -1, palHover_ = -1, hoverPlace_ = -1, hover_ = -1, checkHover_ = -1, listHover_ = -1, invalidate();
        return false;
    }
    if (e.kind == draw::MouseKind::Move) {
        if (const int k = edgeAt(e.pt); k != hoverEdge_) hoverEdge_ = k, invalidate();
        int v = -1;
        if (palette_.contains(e.pt) && e.pt.x >= x0) {
            const int g = palGroupAt((e.pt.y - palette_.top + palScroll_) / cell), s = palSubAt(e.pt.x);
            if (g >= 0 && g < int(tileset_.groups.size()) && s >= 0 && s < 16) v = g << 4 | s;
        }
        if (v != palHover_) palHover_ = v, invalidate();
        int half = 0;
        const int i = placeAt(e.pt, half), h = i >= 0 ? places_[size_t(i)].piece : -1;
        if (h != hover_ || half != hoverHalf_ || i != hoverPlace_) hover_ = h, hoverHalf_ = half, hoverPlace_ = i, invalidate();
        if (painting_ && brushValue() >= 0 && h >= 0 && h * 2 + half != lastPaint_) {  // (a drag paints each slot it crosses)
            lastPaint_ = h * 2 + half;
            setTarget(h, half, false), place(uint16_t(brushValue()));
        }
        int cell = -1;  // (the example's tile under the mouse: its ID on hover)
        if (picture_ && puzzle_.contains(e.pt) && crop_.width() > 0) {
            int ox = 0, oy = 0;
            exampleOrigin(ox, oy);
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
        if (ch != checkHover_) {
            checkHover_ = ch, listHover_ = -1;
            if (ch >= 0)
                if (const std::vector<CheckRow> rows = checkRows(); ch < int(rows.size())) listHover_ = rows[size_t(ch)].group;
            invalidate();
        }
        return false;
    }
    if ((e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::Up) && e.button == 1 && t && !loading_) {  // (the right button: the slot's menu)
        int half = 0, k = -1, place = placeAt(e.pt, half);
        if (place >= 0) k = places_[size_t(place)].piece;
        else if (check_.contains(e.pt)) {  // (a custom tile of the checklist)
            const int dy = e.pt.y - check_.top - scale(kCheckHead) + checkScroll_;
            const std::vector<CheckRow> rows = checkRows();
            if (dy >= 0 && dy / scale(kCheckRow) < int(rows.size())) {
                const CheckRow& row = rows[size_t(dy / scale(kCheckRow))];
                const int th = scale(24), xc = check_.left + scale(8) + scale(110) + 2 * th + scale(16);
                half = e.pt.x >= xc + th && e.pt.x < xc + 2 * th ? 1 : e.pt.x >= xc && e.pt.x < xc + th ? 0 : -1;
                if (half >= 0) k = row.piece;
            }
        }
        if (k < 0 && puzzle_.contains(e.pt)) {  // (no slot under it: nothing selected, no brush)
            if (e.kind == draw::MouseKind::Up) deselectAll();
            return true;
        }
        if (k < 0 || k >= int(t->t.pieces.size())) return check_.contains(e.pt);
        if (e.kind == draw::MouseKind::Up) slotMenu(k, half, e.pt, place);
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 0 && (e.mods & draw::kAlt) && t) {  // (Alt+click in the example: its tile is the brush)
        int half = 0;
        if (const int at = placeAt(e.pt, half); at >= 0) {
            const int k = places_[size_t(at)].piece;
            if (k >= 0 && k < int(t->t.pieces.size())) {
                const uint16_t v = t->t.pieces[size_t(k)][size_t(1 + half)];
                if (isomrules::nullHalf(v)) setMessage(L"Rect " + std::to_wstring(t->t.pieces[size_t(k)][0]) + L": half is NULL");
                else pickBrush(v), showRow(v >> 4, v);
            }
            return true;
        }
    }
    if (e.kind == draw::MouseKind::Down) nudgeSlot_ = -1;
    if (e.kind != draw::MouseKind::Down || e.button != 0 || !t || loading_) return false;
    if (int half = 0; placeAt(e.pt, half) >= 0) {
        const int k = places_[size_t(placeAt(e.pt, half))].piece;
        if (brushValue() >= 0 && !(e.mods & draw::kCtrl) && k >= 0) {  // (the brush paints the slot; a drag goes on painting)
            painting_ = true, strokeSaved_ = false, lastPaint_ = k * 2 + half;
            if (host()) host()->capture(*this, true);
            setTarget(k, half, false), place(uint16_t(brushValue()));
            slotFocus(true);
            return true;
        }
        setTarget(k, half);
        slotFocus(true);
        if (e.mods & draw::kCtrl) {  // (its tiles in the palette: the half clicked)
            const auto& q = t->t.pieces[size_t(target_)];
            if (isomrules::nullHalf(q[size_t(1 + half)])) setMessage(L"Rect " + std::to_wstring(q[0]) + L": half is NULL");
            else showRow(q[1 + half] >> 4, q[1 + half]);
        }
        invalidate();
        return true;
    }
    if (puzzle_.contains(e.pt) && picture_ && !(e.mods & draw::kAlt)) return deselectAll(), true;  // (off the slots: nothing selected)
    if (check_.contains(e.pt)) {  // (a row of the checklist: its rect is the target; the half: the custom tile clicked, then
        // selected for the Tiles: the tile under the mouse there shows in it, a click puts it in)
        slotFocus(false);
        const int dy = e.pt.y - check_.top - scale(kCheckHead) + checkScroll_;
        const std::vector<CheckRow> rows = checkRows();
        if (dy >= 0 && dy / scale(kCheckRow) < int(rows.size())) {
            const CheckRow& row = rows[size_t(dy / scale(kCheckRow))];
            const int th = scale(24), xc = check_.left + scale(8) + scale(110) + 2 * th + scale(16);  // (the custom tiles' left)
            if (row.piece >= 0) {
                const int half = e.pt.x >= xc + th && e.pt.x < xc + 2 * th ? 1 : e.pt.x >= xc && e.pt.x < xc + th ? 0 : -1;
                const int k = target_ >= 0 && t->t.pieces[size_t(target_)][0] == row.group ? target_ : row.piece;  // (the variant shown: it stays)
                setTarget(k, half);
                if (half >= 0) slotFocus(true);
                const auto& q = t->t.pieces[size_t(k)];  // (the Tiles show its tiles; a NULL rect: the template's group)
                const uint16_t v = !isomrules::nullHalf(q[size_t(1 + targetHalf_)]) ? q[size_t(1 + targetHalf_)] : q[size_t(2 - targetHalf_)];
                if (!isomrules::nullHalf(v)) showRow(v >> 4, wide_ ? v & ~1 : v);
                else showRow(q[0], -1);
            }
        }
        return true;
    }
    if (palette_.contains(e.pt) && e.pt.x >= x0) {
        const int g = palGroupAt((e.pt.y - palette_.top + palScroll_) / cell), s = palSubAt(e.pt.x);
        if (g >= 0 && g < int(tileset_.groups.size()) && s >= 0 && s < 16 && tileset_.megatileFor(uint16_t(g << 4 | s)) > 0)
        {
            if ((e.mods & draw::kShift) && target_ >= 0) fillFromRow(g), blinkOn_ = true;  // (Shift: the whole row, each pair a variant)
            else if (slotKeys_ && target_ >= 0) place(uint16_t(g << 4 | s)), blinkOn_ = true;  // (a slot selected (blinking): the tile goes into it)
            else pickBrush(brush_ == (g << 4 | s) ? -1 : g << 4 | s);  // (the brush again: none)
        }
        return true;
    }
    return false;
}

std::wstring IsomRulesView::tip(draw::Point p) {
    const auto* t = terrain();
    if (!t) return {};
    const int cell = palCell(), x0 = palLeft();
    if (palette_.contains(p) && p.x >= x0) {
        const int g = palGroupAt((p.y - palette_.top + palScroll_) / cell), s = palSubAt(p.x);
        if (g < 0 || s < 0) return {};
        return std::to_wstring(g) + L"." + std::to_wstring(s) + (g < int(rowNames_.size()) && !rowNames_[size_t(g)].empty() ? L"  \x00b7  " + rowNames_[size_t(g)] : L"");
    }
    if (const int col = colAt(p); col >= 0) {
        return std::wstring(colName(col)) + L". Right-click: move the column or hide it";
    }
    if (check_.contains(p) && checkHover_ >= 0) {
        const std::vector<CheckRow> rows = checkRows();
        if (checkHover_ < int(rows.size())) {
            const CheckRow& row = rows[size_t(checkHover_)];
            return L"rect " + std::to_wstring(row.group) + (row.more ? L"  \x00b7  " + std::to_wstring(row.more + 1) + L" variants" : L"") +
                   (visibleGroups_.count(row.group) || std_ >= 0 ? L"" : L"  \x00b7  not on the terrain canvas");
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
