// Tools > Cosmonarchy > Ramp and bridge mapper (portable, core.inc; stardraft/passages.h). A mapping is a canvas: the
// user picks a block of tiles in the tile grid (right; a drag), places it in the canvas (middle; a click, as often as
// needed, from any places of the grid), then paints the cells' roles (Wall A, Slope, Wall B; a bridge's Start, Deck,
// End) and marks (Cliff, Ground; a bridge's Water, Coast). The marks give the ISOM corners the pieces need (dots in the
// canvas: passages::cornersOf), the facing (Auto: from the cliff to the ground) and the grid of the ISOM corners (Auto:
// from the tiles' halves of an ISOM rect, IsomTiles). The Ramp brush and the Bridge tool use the mappings at once
// (passages::use; src/ramps.cpp, src/bridges.cpp); Save writes the passages file (.scpass). The window is not tied to
// the open map: its Tileset list (as the Custom isom terrain editor's) opens that tileset of the mod on the worker. A
// tool for the mod's tile makers: terse texts (not STE100), a one-line step strip.

void shellShowPassagesWindow();                          // (the host: the window, once)
void shellSetPassagesTitle(const std::wstring& title);   // (the host: the window's title)
void shellPaletteRebuilt();
bool shellOpenGameData();  // (the host: the game's own data, which Cosmonarchy mode does not open at the start)

namespace spass = stardraft::passages;

namespace passagemap {

constexpr int kMaxTerrainRows = 4;
constexpr int kCanvasW = spass::kCanvasW, kCanvasH = spass::kCanvasH;  // (the canvas's cells)

// A brush of the strip: a role or a mark (or Erase), its color and its key.
struct BrushDef {
    const wchar_t* name;
    uint8_t role, mark;
    bool erase;
    draw::Color color;
};
inline const BrushDef* brushes(bool bridge, int& n) {
    static const BrushDef kRamp[] = {{L"Wall A", spass::kWallA, 0, false, RGB(236, 96, 80)},   {L"Slope", spass::kSlope, 0, false, RGB(96, 204, 112)},
                                     {L"Wall B", spass::kWallB, 0, false, RGB(92, 152, 244)},  {L"Cliff", 0, spass::kHigh, false, RGB(240, 196, 64)},
                                     {L"Ground", 0, spass::kLow, false, RGB(64, 210, 210)},     {L"Erase", 0, 0, true, RGB(160, 160, 160)}};
    static const BrushDef kBridge[] = {{L"Start", spass::kStart, 0, false, RGB(236, 96, 80)},  {L"Deck", spass::kDeck, 0, false, RGB(96, 204, 112)},
                                       {L"End", spass::kEnd, 0, false, RGB(92, 152, 244)},     {L"Water", 0, spass::kHigh, false, RGB(64, 210, 210)},
                                       {L"Coast", 0, spass::kLow, false, RGB(214, 160, 90)},   {L"Erase", 0, 0, true, RGB(160, 160, 160)}};
    n = 6;
    return bridge ? kBridge : kRamp;
}
inline draw::Color roleColor(bool bridge, uint8_t role) {
    int n = 0;
    const BrushDef* b = brushes(bridge, n);
    for (int i = 0; i < n; ++i)
        if (!b[i].erase && b[i].role && b[i].role == role) return b[i].color;
    return draw::kNone;
}
inline draw::Color markColor(bool bridge, uint8_t mark) {
    int n = 0;
    const BrushDef* b = brushes(bridge, n);
    for (int i = 0; i < n; ++i)
        if (!b[i].erase && b[i].mark && b[i].mark == mark) return b[i].color;
    return draw::kNone;
}

// The tileset a mapping is made on, opened on the worker: a blank map of it (its tiles and its terrains' names, as the
// tools see them on a map).
inline const GameData* mapperData(std::string& err) {
    if (cmbwMode()) return cmbwGameData(err);
    if (!shellState().data.isOpen()) shellOpenGameData();
    if (!shellState().data.isOpen()) return err = "no game data", nullptr;
    return &shellState().data;
}

}  // namespace passagemap

class PassagesView final : public draw::View {
public:
    ~PassagesView() override { *alive_ = false; }
    PassagesView()
        : addRow_(L"+", stardraft::kit::Tone::Normal, [this] { addTerrainRow(); }),
          removeRow_(L"\x2212", stardraft::kit::Tone::Normal, [this] { removeTerrainRow(); }) {
        addRow_.setTip(L"Add terrains");
        removeRow_.setTip(L"Remove the last terrains");
        std::vector<stardraft::kit::DropdownItem> eras;
        for (int e = 0; e < 8; ++e) eras.push_back({isomrules::tilesetTitle(e)});
        eraPick_.setItems(eras);
        eraPick_.onChange = [this](int e) { openEra(e); };
        list_.label = [this](int i) { return rowLabel(i); };
        list_.onSelect = [this] { selected(); };
        list_.setEmptyHint(L"No mappings");
        name_.onChange = [this] {
            if (std::string* n = nameOf()) *n = narrow(name_.text()), edited(true), list_.invalidate();
        };
        name_.setPlaceholder(L"Name");
        facing_.onChange = [this](int i) { pickFacing(i); };
        facing_.wheelSteps = false;
        grid_.onChange = [this](int i) { pickGrid(i); };
        grid_.wheelSteps = false;
        grid_.setTip(L"Where the ISOM corners are in the canvas (Auto: from the tiles' rect halves)");
        repeat_.setLabel(L"Repeat"), repeat_.setPlainLabel(true);
        repeat_.setTip(L"The slope repeats along the edge (any width); off: as painted");
        repeat_.onChange = [this](bool on) {
            if (spass::RampMapping* r = ramp()) r->repeat = on, edited();
        };
        jump_.setPlaceholder(L"Group");
        jump_.setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
        jump_.setMaxLength(5);
        jump_.onEnter = [this] { scrollPaletteTo(jump_.text().empty() ? 0 : std::stoi(jump_.text())); };
        for (int r = 0; r < passagemap::kMaxTerrainRows; ++r)
            for (int c = 0; c < 3; ++c) {
                terr_[r][c].searchable = true, terr_[r][c].wheelSteps = false;
                terr_[r][c].onChange = [this, r, c](int i) { pickTerrain(r, c, i); };
                add(terr_[r][c], {0, 0, 0, 0});
            }
        guideA_ = stardraft::kit::Button(L"", stardraft::kit::Tone::Primary, [this] { if (guideRunA_) guideRunA_(); });
        emptyRamp_ = stardraft::kit::Button(L"New ramp", stardraft::kit::Tone::Primary, [this] { addMapping(false); });
        emptyBridge_ = stardraft::kit::Button(L"New bridge", stardraft::kit::Tone::Primary, [this] { addMapping(true); });
        for (draw::View* v : std::initializer_list<draw::View*>{&bar_, &eraPick_, &addRow_, &removeRow_, &list_, &name_, &facing_, &grid_, &repeat_, &jump_, &guideA_,
                                                                 &emptyRamp_, &emptyBridge_, &palBar_, &canvasV_, &canvasH_})
            add(*v, {0, 0, 0, 0});
        palBar_.onScroll = [this](int pos) { palScroll_ = pos, invalidate(); };
        canvasV_.onScroll = [this](int pos) { scrollY_ = pos, invalidate(); };
        canvasH_.onScroll = [this](int pos) { scrollX_ = pos, invalidate(); };
    }
    static draw::Point size() { return {scale(1320), scale(820)}; }

    // Tools > Cosmonarchy > Ramp and bridge mapper: the window with the passages in use, on the last tileset.
    void open() {
        if (!shown_) {
            loadSettingFile();
            shown_ = true;
        }
        if (era_ < 0) openEra(firstEra());
        refresh();
        shellShowPassagesWindow();
    }
    // A snapshot (--dialog passages:<file>[|<row>]): that file's passages, the tileset of its first mapping (opened at
    // once, not on the worker), a row selected.
    void openForShot(const std::filesystem::path& f, int row) {
        spass::Passages p;
        std::string err;
        if (!spass::load(f, p, err)) std::printf("%s\n", err.c_str());
        doc_ = p, file_ = f, dirty_ = false, shown_ = true, async_ = false, settingsOff_ = true;
        spass::use(doc_), reset();
        int era = 0;
        if (!doc_.ramps.empty()) era = doc_.ramps.front().era;
        else if (!doc_.bridges.empty()) era = doc_.bridges.front().era;
        openEra(era);
        sel_ = row;
        refresh();
        if (sel_ >= 0 && sel_ < int(rows_.size())) list_.selectOnly(sel_), showSelected();
        std::printf("passages: tileset %d, %d groups, %zu rows, %s\n", era_, paletteRows(), rows_.size(), ed_ ? "open" : "not open");
    }
    // The file the passages in use came from (Save writes it; the setting PassagesFile).
    static std::filesystem::path& inUseFile() {
        static std::filesystem::path f;
        return f;
    }
    static bool& inUseOn() {
        static bool on = false;
        return on;
    }
    // Program options' Ramp and bridge mappings changed: the passages of the file in use (none when off), also in the
    // window when it has no unsaved changes of its own.
    void settingChanged() {
        if (dirty_ && shown_) {  // (its unsaved work stays, but the tools keep the setting's until a Save makes this the one in use)
            detached_ = true;
            return setMessage(L"Options file changed; unsaved edits kept (not in use until Save)");
        }
        loadSettingFile(), sel_ = -1;
        if (shown_) refresh();
    }
    // The setting's file into the window; none or unreadable: a new document with no file (Save asks for one).
    void loadSettingFile() {
        spass::Passages p;
        std::string err;
        file_.clear(), doc_ = {};
        if (!inUseFile().empty() && spass::load(inUseFile(), p, err)) doc_ = std::move(p), file_ = inUseFile();
        else if (inUseFile().empty()) doc_ = spass::inUse();
        dirty_ = false, detached_ = false, reset();
    }
    // Whether the window's mappings are the ones the tools use: the setting's file is the window's and on, or no file is
    // set yet (a first mapping: in use at once, before its first Save).
    bool live() const { return !detached_ && (inUseFile().empty() || (inUseOn() && file_ == inUseFile())); }
    void useIfLive() {
        if (live()) spass::use(doc_);
    }
    bool confirmClose() {
        if (!dirty_) return true;
        const int r = msgbox::ask(*this, L"Ramp and bridge mapper", L"Save the changes to the passages?", {L"&Save", L"Do &not save", L"Cancel"},
                                  msgbox::Icon::Question);
        if (r == 0) return save(false);
        if (r != 1) return false;
        spass::Passages p;
        std::string err;
        if (!file_.empty() && spass::load(file_, p, err)) doc_ = p;
        else doc_ = {};
        useIfLive(), dirty_ = false, reset(), shellPaletteRebuilt();
        refresh();
        return true;
    }
    bool closeRequested() override { return confirmClose(); }
    // Each turn of the event loop (nothing to follow: the window has its own tileset).
    void follow() {}

    void resized() override;
    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override;
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || ((name_.hasFocus() || jump_.hasFocus()) && !(e.mods & draw::kCtrl))) return false;
        const bool ctrl = e.mods == draw::kCtrl;
        if (e.key == draw::Key::Escape && !e.mods) {
            if (testPic_) return testPic_.reset(), invalidate(), true;
            if (stamp_.on) return stamp_.on = false, brush_ = std::max(0, brush_), invalidate(), true;
            return false;
        }
        if (!e.mods && e.key >= draw::Key('1') && e.key <= draw::Key('6') && sel_ >= 0) return pickBrush(int(e.key) - int(draw::Key('1'))), true;
        if (e.key == draw::Key::Delete && !e.mods && sel_ >= 0) return pickBrush(5), true;
        if (!e.mods && sel_ >= 0 && (e.key == draw::Key::Left || e.key == draw::Key::Right || e.key == draw::Key::Up || e.key == draw::Key::Down))
            return moveCells(e.key == draw::Key::Left ? -1 : e.key == draw::Key::Right ? 1 : 0, e.key == draw::Key::Up ? -1 : e.key == draw::Key::Down ? 1 : 0), true;
        if (e.key == draw::Key('S') && (e.mods & draw::kCtrl)) return save((e.mods & draw::kShift) != 0), true;
        if (e.key == draw::Key('O') && ctrl) return loadFile(), true;
        if (e.key == draw::Key('N') && ctrl) return addMapping(false), true;
        if (e.key == draw::Key('B') && ctrl) return addMapping(true), true;
        if (e.key == draw::Key('T') && ctrl) return runTest(), true;
        if (e.key == draw::Key('M') && ctrl) return marksFromHeights(), true;
        if (e.key == draw::Key('H') && ctrl) return overlays_ = !overlays_, invalidate(), true;
        if (e.key == draw::Key('Z') && (e.mods & draw::kCtrl)) return undo((e.mods & draw::kShift) != 0), true;
        if (e.key == draw::Key('Y') && ctrl) return undo(true), true;
        return false;
    }
    bool focusable() const override { return true; }

    // For a snapshot's checks: the selected mapping's shape, as a line.
    std::string shapeLine() {
        if (spass::RampMapping* r = ramp()) {
            const spass::RampShape s = spass::rampShape(*r);
            return s.ok ? "ramp ok: facing " + std::string(stardraft::rampFacingName(s.facing)) + ", " + std::to_string(s.corners.size()) + " corners" : "ramp: " + s.why;
        }
        if (spass::BridgeMapping* b = bridge()) {
            const spass::BridgeShape s = spass::bridgeShape(*b);
            return s.ok ? "bridge ok: deck " + std::string(s.deck ? "u" : "d") + ", " + std::to_string(s.corners.size()) + " corners" : "bridge: " + s.why;
        }
        return "no mapping";
    }

private:
    struct Row {
        bool bridge;
        int index;  // in doc_.ramps or doc_.bridges
    };

    // --- The tileset ---------------------------------------------------------------------------
    int firstEra() const {
        int64_t v = 0;
        if (!gSettingsOff && prefs::number(L"Layout/PassagesEra", v) && v >= 0 && v < 8) return int(v);
        const AppState& app = shellState();
        return app.ed && app.ed->isOpen() ? app.ed->era() & 7 : 0;
    }
    // Opens a tileset (its blank map on the worker; the window shows Loading until then).
    void openEra(int e) {
        e &= 7;
        eraPick_.setValue(e);
        if (e == era_ && (ed_ || loading_)) return;
        era_ = e, loading_ = true, ed_.reset(), clearPictures(), palScroll_ = 0, scrollX_ = scrollY_ = 0, stamp_.on = false, testPic_.reset();
        if (!gSettingsOff && !settingsOff_) prefs::setNumber(L"Layout/PassagesEra", e);
        const int gen = ++openGen_;
        std::shared_ptr<bool> alive = alive_;
        // (the game's data: the mod's opens on the worker, as the Custom isom terrain editor's; the game's own here)
        std::string firstErr;
        const GameData* own = cmbwMode() ? nullptr : passagemap::mapperData(firstErr);
        auto job = [this, gen, e, alive, own, firstErr] {
            std::string err = firstErr;
            auto ed = std::make_shared<std::unique_ptr<Editor>>();
            if (const GameData* data = own ? own : cmbwMode() ? passagemap::mapperData(err) : nullptr) {
                *ed = std::make_unique<Editor>(*data);
                (*ed)->setUndoLimit(1), (*ed)->setTerrainOnly(true);
                if (!(*ed)->newMap(16, 16, uint16_t(e), 1, err)) ed->reset();
            }
            auto done = [this, gen, ed, err, alive] {
                if (!*alive || gen != openGen_) return;
                loading_ = false, ed_ = std::move(*ed);
                if (!ed_) setMessage(L"Tileset not opened: " + widen(err));
                refresh();
            };
            if (async_) shellPost(done);
            else done();
        };
        invalidate();
        if (async_) isomrules::worker().post(job);
        else job();
    }
    const stardraft::Tileset* tiles() const { return ed_ ? &ed_->tileset() : nullptr; }
    int paletteRows() const { return ed_ ? int(ed_->tileset().groups.size()) : 0; }
    void clearPictures() { tilePics_.clear(), rowPics_.clear(); }

    // A tile's half of an ISOM rect and the rect's parity (half * 2 + parity), -1: not known (the grid's Auto).
    int rectParity(uint16_t v) const {
        if (!ed_) return -1;
        const stardraft::Tileset& ts = ed_->tileset();
        const stardraft::IsomTiles* it = stardraft::IsomTiles::forTileset(ts);
        const auto parityOf = [&](uint16_t group) {
            if (!it) return -1;
            const auto& rs = it->rectsOf(group);
            if (rs.empty()) return -1;
            for (const auto& r : rs)
                if (r.parity != rs.front().parity) return -1;
            return int(rs.front().parity);
        };
        const uint16_t g = uint16_t(v >> 4);
        if (const int p = parityOf(g); p >= 0) return p;
        if (g > 0)
            if (const int p = parityOf(uint16_t(g - 1)); p >= 0) return 2 + p;
        for (const auto& [left, right, game] : ts.groupPieces) {  // (a mod's piece of a rect: the game group's rect)
            if (left == v)
                if (const int p = parityOf(game); p >= 0) return p;
            if (right == v)
                if (const int p = parityOf(game); p >= 0) return 2 + p;
        }
        return -1;
    }
    int gridGuess(const std::vector<spass::Cell>& cells) const {
        return spass::guessGrid(cells, [this](uint16_t v) { return rectParity(v); });
    }

    // --- The mappings --------------------------------------------------------------------------
    bool rowOk() const {
        if (sel_ < 0 || sel_ >= int(rows_.size())) return false;
        const Row& r = rows_[size_t(sel_)];
        return r.index >= 0 && size_t(r.index) < (r.bridge ? doc_.bridges.size() : doc_.ramps.size());
    }
    spass::RampMapping* ramp() { return rowOk() && !rows_[size_t(sel_)].bridge ? &doc_.ramps[size_t(rows_[size_t(sel_)].index)] : nullptr; }
    spass::BridgeMapping* bridge() { return rowOk() && rows_[size_t(sel_)].bridge ? &doc_.bridges[size_t(rows_[size_t(sel_)].index)] : nullptr; }
    bool isBridge() const { return rowOk() && rows_[size_t(sel_)].bridge; }
    std::vector<spass::Cell>* cells() {
        if (spass::RampMapping* r = ramp()) return &r->cells;
        if (spass::BridgeMapping* b = bridge()) return &b->cells;
        return nullptr;
    }
    std::string* nameOf() {
        if (spass::RampMapping* r = ramp()) return &r->name;
        if (spass::BridgeMapping* b = bridge()) return &b->name;
        return nullptr;
    }
    int* gridOf(bool** autoOn = nullptr) {
        if (spass::RampMapping* r = ramp()) return autoOn ? *autoOn = &r->gridAuto, &r->grid : &r->grid;
        if (spass::BridgeMapping* b = bridge()) return autoOn ? *autoOn = &b->gridAuto, &b->grid : &b->grid;
        return nullptr;
    }
    std::wstring rowLabel(int i) {
        if (i < 0 || i >= int(rows_.size())) return {};
        const Row& r = rows_[size_t(i)];
        if (r.bridge) {
            const spass::BridgeMapping& b = doc_.bridges[size_t(r.index)];
            const spass::BridgeShape s = spass::bridgeShape(b);
            return L"Bridge  " + (b.name.empty() ? std::wstring(L"(no name)") : widen(b.name)) +
                   (s.ok ? std::wstring(s.deck ? L"  u" : L"  d") : L"  \x2717 " + widen(s.why));
        }
        const spass::RampMapping& m = doc_.ramps[size_t(r.index)];
        const spass::RampShape s = spass::rampShape(m);
        return L"Ramp  " + (m.name.empty() ? std::wstring(L"(no name)") : widen(m.name)) +
               (s.ok ? L"  " + widen(stardraft::rampFacingName(s.facing)) + (m.repeat ? L" \x00d7n" : L"") : L"  \x2717 " + widen(s.why));
    }

    // The tileset's mappings into the list; the fields.
    void refresh() {
        rows_.clear();
        for (int i = 0; i < int(doc_.ramps.size()); ++i)
            if (doc_.ramps[size_t(i)].era == era_) rows_.push_back({false, i});
        for (int i = 0; i < int(doc_.bridges.size()); ++i)
            if (doc_.bridges[size_t(i)].era == era_) rows_.push_back({true, i});
        list_.setCount(int(rows_.size()));
        if (sel_ >= int(rows_.size())) sel_ = -1;
        if (sel_ < 0 && !rows_.empty()) sel_ = 0;
        if (sel_ >= 0) list_.selectOnly(sel_);
        terrainNames_.clear();
        std::vector<stardraft::kit::DropdownItem> names{{L"(none)"}};
        if (ed_)
            for (uint16_t id = 1; id < 256; ++id)
                if (const std::string n = ed_->terrainName(id); !n.empty())
                    terrainNames_.push_back(n), names.push_back({widen(n), -1, -1, draw::kNone, isomrules::terrainPicture(ed_->tileset(), id)});
        for (auto& row : terr_)
            for (auto& d : row) d.setItems(names);
        showSelected();
        title();
    }
    void selected() {
        sel_ = list_.cursor() >= 0 && list_.cursor() < int(rows_.size()) ? list_.cursor() : -1;
        stamp_.on = false, testPic_.reset(), merging_ = false;  // (another mapping's typing: an undo step of its own)
        showSelected();
    }
    void showSelected() {
        name_.setText(nameOf() ? widen(*nameOf()) : L"");
        std::vector<stardraft::kit::DropdownItem> f;
        if (spass::RampMapping* r = ramp()) {
            const int guess = spass::facingOfCorners(spass::cornersOf(r->cells, r->grid, false));
            f.push_back({L"Facing: auto" + (guess >= 0 ? L" (" + widen(stardraft::rampFacingName(guess)) + L")" : std::wstring())});
            for (int i = 0; i < 8; ++i) f.push_back({L"Facing: " + widen(stardraft::rampFacingName(i))});
            facing_.setItems(f), facing_.setValue(r->facing + 1);
            repeat_.setOn(r->repeat);
        } else if (spass::BridgeMapping* b = bridge()) {
            const int guess = spass::bridgeDeck({b->name, b->era, -1, b->grid, b->gridAuto, {}, b->cells});
            f.push_back({L"Deck: auto" + (guess >= 0 ? std::wstring(guess ? L" (u)" : L" (d)") : std::wstring())});
            f.push_back({L"Deck: d (down-right)"}), f.push_back({L"Deck: u (up-right)"});
            facing_.setItems(f), facing_.setValue(b->deck + 1);
        }
        if (bool* a = nullptr; int* g = gridOf(&a)) {
            std::vector<stardraft::kit::DropdownItem> gs{{L"Grid: auto (" + std::to_wstring(*g) + L")"}};
            for (int i = 0; i < 4; ++i) gs.push_back({L"Grid: " + std::to_wstring(i)});
            grid_.setItems(gs), grid_.setValue(*a ? 0 : *g + 1);
        }
        showTerrains();
        resized();
        invalidate();
    }
    void pickFacing(int i) {
        if (spass::RampMapping* r = ramp()) r->facing = std::clamp(i - 1, -1, 7);
        else if (spass::BridgeMapping* b = bridge()) b->deck = std::clamp(i - 1, -1, 1);
        else return;
        edited(), showSelected();
    }
    void pickGrid(int i) {
        bool* a = nullptr;
        int* g = gridOf(&a);
        if (!g) return;
        *a = i <= 0;
        if (i > 0) *g = i - 1;
        else if (const std::vector<spass::Cell>* cs = cells(); cs)
            if (const int guess = gridGuess(*cs); guess >= 0) *g = guess;
        edited(), showSelected();
    }

    // The terrain rows: Ground, Cliff (a ramp) or Coast A, Water, Coast B (a bridge), each a name of the tileset's.
    int terrainRows() {
        if (spass::RampMapping* r = ramp()) return int(r->terrains.size());
        if (spass::BridgeMapping* b = bridge()) return int(b->terrains.size());
        return 0;
    }
    int terrainCols() { return bridge() ? 3 : ramp() ? 2 : 0; }
    std::string* terrainAt(int row, int col) {
        if (spass::RampMapping* r = ramp(); r && row < int(r->terrains.size())) return col == 0 ? &r->terrains[size_t(row)].ground : col == 1 ? &r->terrains[size_t(row)].cliff : nullptr;
        if (spass::BridgeMapping* b = bridge(); b && row < int(b->terrains.size()))
            return col == 0 ? &b->terrains[size_t(row)].coastA : col == 1 ? &b->terrains[size_t(row)].water : &b->terrains[size_t(row)].coastB;
        return nullptr;
    }
    void showTerrains() {
        for (int r = 0; r < passagemap::kMaxTerrainRows; ++r)
            for (int c = 0; c < 3; ++c) {
                const std::string* t = terrainAt(r, c);
                int v = 0;
                if (t) {  // (the name, else a mod's name of it with its ground: "High Dirt (Dirt)")
                    for (size_t i = 0; i < terrainNames_.size() && !v; ++i)
                        if (terrainNames_[i] == *t) v = int(i) + 1;
                    for (size_t i = 0; i < terrainNames_.size() && !v && !t->empty(); ++i)
                        if (terrainNames_[i].rfind(*t + " (", 0) == 0) v = int(i) + 1;
                }
                terr_[r][c].setValue(v);
            }
        addRow_.setEnabled(sel_ >= 0 && terrainRows() < passagemap::kMaxTerrainRows);
        removeRow_.setEnabled(terrainRows() > 1);
    }
    void pickTerrain(int row, int col, int i) {
        std::string* t = terrainAt(row, col);
        if (!t) return;
        *t = i > 0 && i <= int(terrainNames_.size()) ? terrainNames_[size_t(i - 1)] : std::string();
        edited();
    }
    void addTerrainRow() {
        if (spass::RampMapping* r = ramp()) r->terrains.push_back(r->terrains.empty() ? spass::RampTerrains{} : r->terrains.back());
        else if (spass::BridgeMapping* b = bridge()) b->terrains.push_back(b->terrains.empty() ? spass::BridgeTerrains{} : b->terrains.back());
        else return;
        edited(), showTerrains(), resized();
    }
    void removeTerrainRow() {
        if (spass::RampMapping* r = ramp(); r && r->terrains.size() > 1) r->terrains.pop_back();
        else if (spass::BridgeMapping* b = bridge(); b && b->terrains.size() > 1) b->terrains.pop_back();
        else return;
        edited(), showTerrains(), resized();
    }

    void addMapping(bool asBridge) {
        if (!ed_) return setMessage(L"No tileset open");
        if (asBridge) {
            spass::BridgeMapping b;
            b.name = "Bridge " + std::to_string(doc_.bridges.size() + 1), b.era = era_, b.terrains.push_back({});
            doc_.bridges.push_back(std::move(b));
        } else {
            spass::RampMapping r;
            r.name = "Ramp " + std::to_string(doc_.ramps.size() + 1), r.era = era_, r.terrains.push_back({});
            doc_.ramps.push_back(std::move(r));
        }
        refresh();
        for (int i = 0; i < int(rows_.size()); ++i)
            if (rows_[size_t(i)].bridge == asBridge && rows_[size_t(i)].index == int((asBridge ? doc_.bridges.size() : doc_.ramps.size()) - 1)) sel_ = i;
        list_.selectOnly(sel_);
        edited();
        showSelected();
        brush_ = 0, stamp_.on = false;
        setMessage(L"Drag a block in the tile grid, click the canvas");
    }
    void removeMapping() {
        if (!rowOk()) return;
        const Row r = rows_[size_t(sel_)];
        if (r.bridge) doc_.bridges.erase(doc_.bridges.begin() + r.index);
        else doc_.ramps.erase(doc_.ramps.begin() + r.index);
        sel_ = std::min(sel_, int(rows_.size()) - 2);
        refresh();
        edited();
    }
    // Mapping > Game ramp / Game bridge: a game kit's tiles, roles and marks into the canvas (a mod tileset with the
    // game's tiles; also a model of how a mapping looks).
    void fromGameRamp(int kit) {
        spass::RampMapping* r = ramp();
        if (!r) return;
        const spass::RampMapping g = stardraft::rampCanvasOfKit(kit);
        r->cells = g.cells, r->facing = g.facing, r->gridAuto = false;
        for (spass::Cell& c : r->cells) c.x += 4, c.y += 4;
        r->grid = gridShifted(g.grid, 4, 4);
        if (r->terrains.empty() || (r->terrains.size() == 1 && r->terrains[0].ground.empty() && r->terrains[0].cliff.empty())) r->terrains = g.terrains;
        edited(), showSelected(), setMessage(L"Game ramp " + widen(stardraft::rampFacingName(stardraft::rampKitFacing(kit))) + L" in the canvas");
    }
    void fromGameBridge(bool u) {
        spass::BridgeMapping* b = bridge();
        if (!b) return;
        const spass::BridgeMapping g = stardraft::bridgeCanvasOfKit(era_, u);
        if (g.cells.empty()) return setMessage(L"No game bridge on this tileset");
        b->cells = g.cells, b->deck = -1, b->gridAuto = false;
        for (spass::Cell& c : b->cells) c.x += 4, c.y += 4;
        b->grid = gridShifted(g.grid, 4, 4);
        if (b->terrains.empty() || (b->terrains.size() == 1 && b->terrains[0].water.empty())) b->terrains = g.terrains;
        edited(), showSelected(), setMessage(L"Game bridge in the canvas");
    }
    // Mapping > Marks from heights: each tile's mark from its minitiles' ground level (VF4): mostly high ground: Cliff;
    // mostly low: Ground; else none. (A ramp's; the user corrects them.)
    void marksFromHeights() {
        std::vector<spass::Cell>* cs = cells();
        if (!cs || !ed_ || isBridge()) return;
        const stardraft::Tileset& ts = ed_->tileset();
        // (each tile's level: the one most of its walkable minitiles have (VF4: 0x4 high, 0x2 middle, else low), with 8 of
        // 16 or more; the canvas's highest level is the cliff, its lowest the ground)
        std::vector<int> level(cs->size(), -1);
        int top = -1, bottom = 3;
        for (size_t i = 0; i < cs->size(); ++i) {
            const spass::Cell& c = (*cs)[i];
            const int m = c.tile >= 0 ? ts.megatileFor(uint16_t(c.tile)) : -1;
            if (m < 0 || size_t(m) >= ts.minitileFlags.size()) continue;
            int count[3] = {0, 0, 0};
            for (const uint16_t f : ts.minitileFlags[size_t(m)])
                if (f & stardraft::Tileset::kWalkable) ++count[(f & 4) ? 2 : (f & 2) ? 1 : 0];
            for (int l = 0; l < 3; ++l)
                if (count[l] >= 8) level[i] = l;
            if (level[i] >= 0) top = std::max(top, level[i]), bottom = std::min(bottom, level[i]);
        }
        if (top <= bottom) return setMessage(L"Marks from heights: the tiles are of one level");
        for (size_t i = 0; i < cs->size(); ++i)
            if ((*cs)[i].tile >= 0) (*cs)[i].mark = level[i] == top ? uint8_t(spass::kHigh) : level[i] == bottom ? uint8_t(spass::kLow) : uint8_t(spass::kUnmarked);
        edited(), showSelected(), setMessage(L"Marks from the tiles' heights (VF4)");
    }
    // The grid of a canvas moved by (dx, dy) cells: the one whose corners are the old ones moved.
    static int gridShifted(int grid, int dx, int dy) {
        for (int g = 0; g < 4; ++g) {
            bool same = true;
            for (int y = 0; y < 4 && same; ++y)
                for (int x = 0; x < 8 && same; ++x) same = spass::cornerOnGrid(g, x + dx, y + dy) == spass::cornerOnGrid(grid, x, y);
            if (same) return g;
        }
        return grid;
    }

    // --- The canvas ----------------------------------------------------------------------------
    struct Stamp {
        bool on = false;
        int g0 = 0, c0 = 0, g1 = 0, c1 = 0;  // (the block of the tile grid: rows (tile groups), columns (subtiles))
        int w() const { return c1 - c0 + 1; }
        int h() const { return g1 - g0 + 1; }
    };
    int cellPx() const { return scale(cellSize_); }
    // The canvas's cell under a point (false: outside the canvas).
    bool cellAt(draw::Point pt, int& x, int& y) const {
        if (!canvas_.contains(pt)) return false;
        x = (pt.x - canvas_.left + scrollX_) / cellPx(), y = (pt.y - canvas_.top + scrollY_) / cellPx();
        return x >= 0 && y >= 0 && x < passagemap::kCanvasW && y < passagemap::kCanvasH;
    }
    draw::Rect cellRect(int x, int y) const {
        const int s = cellPx(), l = canvas_.left - scrollX_ + x * s, t = canvas_.top - scrollY_ + y * s;
        return {l, t, l + s, t + s};
    }
    spass::Cell* cellOf(int x, int y) {
        if (std::vector<spass::Cell>* cs = cells())
            for (spass::Cell& c : *cs)
                if (c.x == x && c.y == y) return &c;
        return nullptr;
    }
    // The stamp's tiles into the canvas from cell (x, y) (a cell keeps its role and mark).
    void placeStamp(int x, int y) {
        std::vector<spass::Cell>* cs = cells();
        if (!cs || !stamp_.on || !ed_) return;
        int n = 0;
        for (int dy = 0; dy < stamp_.h(); ++dy)
            for (int dx = 0; dx < stamp_.w(); ++dx) {
                const int cx = x + dx, cy = y + dy;
                const uint16_t v = uint16_t((stamp_.g0 + dy) << 4 | (stamp_.c0 + dx));
                if (cx >= passagemap::kCanvasW || cy >= passagemap::kCanvasH || ed_->tileset().megatileFor(v) <= 0) continue;
                if (spass::Cell* c = cellOf(cx, cy)) c->tile = v;
                else cs->push_back({cx, cy, int(v), spass::kNone, spass::kUnmarked});
                ++n;
            }
        if (!n) return;
        edited();
        setMessage(std::to_wstring(n) + L" tiles placed  \x00b7  1\x2013" + L"6 paint roles, Esc ends placing");
    }
    // The brush on a cell (right: its layer cleared). Returns whether it changed.
    bool paintCell(int x, int y, bool clear) {
        std::vector<spass::Cell>* cs = cells();
        if (!cs || brush_ < 0) return false;
        int n = 0;
        const passagemap::BrushDef& b = passagemap::brushes(isBridge(), n)[brush_];
        spass::Cell* c = cellOf(x, y);
        if (b.erase) {
            if (!c) return false;
            cs->erase(cs->begin() + (c - cs->data()));
            return true;
        }
        if (b.role) {
            if (!c || c->tile < 0) return false;  // (a piece is of tiles)
            const uint8_t to = clear ? uint8_t(spass::kNone) : b.role;
            if (c->role == to) return false;
            c->role = to;
            return true;
        }
        if (clear) {
            if (!c || c->mark == spass::kUnmarked) return false;
            c->mark = spass::kUnmarked;
            if (c->tile < 0 && c->role == spass::kNone) cs->erase(cs->begin() + (c - cs->data()));
            return true;
        }
        if (!c) cs->push_back({x, y, -1, spass::kNone, b.mark});  // (a mark needs no tile)
        else if (c->mark == b.mark) return false;
        else c->mark = b.mark;
        return true;
    }
    void pickBrush(int i) {
        int n = 0;
        passagemap::brushes(isBridge(), n);
        if (i < 0 || i >= n) return;
        brush_ = i, stamp_.on = false, invalidate();
    }
    // Arrows: all the cells one cell over (the canvas's edges stop it); the grid moves with them.
    void moveCells(int dx, int dy) {
        std::vector<spass::Cell>* cs = cells();
        if (!cs || cs->empty()) return;
        for (const spass::Cell& c : *cs)
            if (c.x + dx < 0 || c.y + dy < 0 || c.x + dx >= passagemap::kCanvasW || c.y + dy >= passagemap::kCanvasH) return;
        for (spass::Cell& c : *cs) c.x += dx, c.y += dy;
        if (int* g = gridOf()) *g = gridShifted(*g, dx, dy);
        edited(true);
    }

    // --- Edits, undo, files --------------------------------------------------------------------
    // An edit of doc_ (made): an undo step with the passages before it (merge: a name typed on or a stroke painted on,
    // one step for it).
    void edited(bool merge = false) {
        if (bool* a = nullptr; int* g = gridOf(&a))  // (the grid's Auto: from the tiles as they are now)
            if (*a)
                if (const int guess = gridGuess(*cells()); guess >= 0) *g = guess;
        if (!(merge && merging_)) {
            undo_.push_back({before_, sel_}), redo_.clear();
            if (undo_.size() > 200) undo_.erase(undo_.begin());
        }
        merging_ = merge, before_ = doc_;
        dirty_ = true;
        useIfLive();
        toolsFollow();
        testPic_.reset();
        if (sel_ >= 0 && !merge) {  // (the Auto items show what auto is now)
            const int f = facing_.value(), g = grid_.value();
            showSelectedLight();
            facing_.setValue(f), grid_.setValue(g);
        }
        list_.invalidate();
        title();
        invalidate();
    }
    void showSelectedLight() {
        if (spass::RampMapping* r = ramp()) {
            std::vector<stardraft::kit::DropdownItem> f = facing_.items();
            const int guess = spass::facingOfCorners(spass::cornersOf(r->cells, r->grid, false));
            if (!f.empty()) f[0].label = L"Facing: auto" + (guess >= 0 ? L" (" + widen(stardraft::rampFacingName(guess)) + L")" : std::wstring());
            facing_.setItems(f);
        }
        if (int* g = gridOf()) {
            std::vector<stardraft::kit::DropdownItem> gs = grid_.items();
            if (!gs.empty()) gs[0].label = L"Grid: auto (" + std::to_wstring(*g) + L")";
            grid_.setItems(gs);
        }
    }
    // The tool strip: the Bridge and the Ramp where the open map's tileset has them now.
    void toolsFollow() {
        const AppState& app = shellState();
        if (!app.ed || !app.ed->isOpen()) return;
        const bool had[2] = {stardraft::bridgesAvailable(*app.ed), stardraft::rampsAvailable(*app.ed)};
        if (had[0] != toolsHad_[0] || had[1] != toolsHad_[1]) toolsHad_[0] = had[0], toolsHad_[1] = had[1], shellPaletteRebuilt();
    }
    void reset() { undo_.clear(), redo_.clear(), before_ = doc_, merging_ = false; }
    void undo(bool redo) {
        auto& from = redo ? redo_ : undo_;
        auto& to = redo ? undo_ : redo_;
        if (from.empty()) return;
        to.push_back({doc_, sel_});
        const Step st = std::move(from.back());
        from.pop_back();
        doc_ = st.doc, before_ = doc_, merging_ = false, dirty_ = true;
        useIfLive();
        toolsFollow();
        sel_ = st.sel, testPic_.reset();
        refresh();
    }
    void setMessage(const std::wstring& m) { status_ = m, invalidate(); }
    void title() { shellSetPassagesTitle((dirty_ ? L"*" : L"") + (file_.empty() ? std::wstring(L"(no file)") : file_.filename().wstring()) + L" - Ramp and bridge mapper"); }
    void loadFile() {
        if (!confirmClose()) return;
        ShellFilePick d;
        d.title = L"Load passages";
        d.filters = {{L"Passages (*.scpass)", L"*.scpass"}};
        const std::filesystem::path f = shellPickFile(d);
        if (f.empty()) return;
        spass::Passages p;
        std::string err;
        if (!spass::load(f, p, err)) return shellErrorBox(widen(err), L"Load passages");
        doc_ = p, file_ = f, dirty_ = false, sel_ = -1;
        reset();
        useFile();
        refresh();
    }
    bool save(bool as) {
        std::filesystem::path f = file_;
        if (as || f.empty()) {
            ShellFilePick d;
            d.title = L"Save passages";
            d.filters = {{L"Passages (*.scpass)", L"*.scpass"}};
            d.defaultExtension = L"scpass", d.fileName = L"passages", d.save = true;
            f = shellPickFile(d);
            if (f.empty()) return false;
        }
        std::string err;
        if (!spass::save(f, doc_, err)) return shellErrorBox(widen(err), L"Save passages"), false;
        file_ = f, dirty_ = false;
        useFile();
        title();
        setMessage(L"Saved; in use");
        return true;
    }
    // The file is the one in use, also at the next start (PassagesFile and PassagesOn: Program options > Plugins >
    // Ramp and bridge mappings).
    void useFile() {
        spass::use(doc_);
        inUseFile() = file_, inUseOn() = true, detached_ = false;
        if (!gSettingsOff && !settingsOff_) prefs::setText(L"PassagesFile", file_.wstring()), prefs::setNumber(L"PassagesOn", 1);
        shellPaletteRebuilt();
    }

    // --- Test: the mapping alone on a small map of its terrains ---------------------------------
    // Ctrl+T: a ramp: a plateau of its first terrains' cliff on their ground and the Ramp brush round its edge; a bridge:
    // a river of its water and a drag over it with the Bridge tool. The map's picture shows in the canvas (Esc: back).
    void runTest() {
        if (!ed_ || sel_ < 0) return;
        const spass::Passages saved = spass::inUse();
        spass::Passages only;
        std::wstring result;
        Editor& ed = *ed_;
        std::string err;
        const auto idOf = [&](const std::string& n) {
            for (uint16_t id = 1; id < 256; ++id)
                if (!n.empty() && ed.terrainName(id) == n) return id;
            return uint16_t(0);
        };
        constexpr int W = 48, H = 40;
        if (spass::RampMapping* r = ramp()) {
            const spass::RampShape s = spass::rampShape(*r);
            if (!s.ok) return setMessage(L"Test: " + widen(s.why));
            if (r->terrains.empty() || !idOf(r->terrains[0].ground) || !idOf(r->terrains[0].cliff)) return setMessage(L"Test: set Ground and Cliff");
            only.ramps.push_back(*r), only.ramps.back().terrains.resize(1);
            const uint16_t low = idOf(r->terrains[0].ground), high = idOf(r->terrains[0].cliff);
            if (!ed.newMap(W, H, uint16_t(era_), low, err)) return setMessage(L"Test: " + widen(err));
            stardraft::Brush b;
            b.shape = stardraft::Brush::Diamond, b.size = 10;
            ed.paintBrush(W * 16, H * 16, high, b);
            int rx = 0, ry = 0;
            while (rx < W * 16 - 32 && ed.terrainAt(W * 16 + rx, H * 16) == high) rx += 8;
            while (ry < H * 16 - 32 && ed.terrainAt(W * 16, H * 16 + ry) == high) ry += 8;
            std::vector<std::pair<int, int>> ring;
            for (int k = 0; k <= 64; ++k) {
                const double t = k / 64.0 * 4, f = t - int(t);
                const int side = int(t) % 4;
                const std::pair<double, double> c[5] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                ring.push_back({W * 16 + int((c[side].first + (c[side + 1].first - c[side].first) * f) * rx),
                                H * 16 + int((c[side].second + (c[side + 1].second - c[side].second) * f) * ry)});
            }
            spass::use(only);
            const std::vector<stardraft::RampPlan> plans = stardraft::planRamps(ed, stardraft::rampBrushStroke(ring, 2));
            int mine = 0;
            for (const stardraft::RampPlan& p : plans)
                if (p.kit >= 0 && stardraft::rampKitFromPassage(p.kit)) ed.placeTileValues(p.tiles), ++mine;
            result = L"Test: " + std::to_wstring(mine) + L" ramps round a plateau (" + widen(r->terrains[0].cliff) + L" on " + widen(r->terrains[0].ground) + L")";
        } else if (spass::BridgeMapping* bm = bridge()) {
            const spass::BridgeShape s = spass::bridgeShape(*bm);
            if (!s.ok) return setMessage(L"Test: " + widen(s.why));
            if (bm->terrains.empty() || !idOf(bm->terrains[0].coastA) || !idOf(bm->terrains[0].water)) return setMessage(L"Test: set Coast A and Water");
            only.bridges.push_back(*bm), only.bridges.back().terrains.resize(1);
            const uint16_t coast = idOf(bm->terrains[0].coastA), water = idOf(bm->terrains[0].water);
            if (!ed.newMap(W, H, uint16_t(era_), coast, err)) return setMessage(L"Test: " + widen(err));
            stardraft::Brush b;
            b.size = 2;
            const int cx = W * 16, cy = H * 16, d = s.deck ? -1 : 1;  // (the river across the deck: down-right for an up-right deck)
            ed.paintTerrainLine(cx - 64 * 6, cy + d * 32 * 6, cx + 64 * 6, cy - d * 32 * 6, water, b);
            spass::use(only);
            const stardraft::BridgePlan p = stardraft::planBridge(ed, cx - 64 * 7, cy - d * 32 * 7, cx + 64 * 7, cy + d * 32 * 7, 1, 7);
            const bool ok = p.problem == stardraft::BridgePlan::Problem::None && p.kit >= 0 && stardraft::applyBridge(ed, p);
            result = ok ? L"Test: bridge of " + std::to_wstring(p.length) + L" decks over " + widen(bm->terrains[0].water) : L"Test: no bridge (problem " + std::to_wstring(int(p.problem)) + L")";
        }
        spass::use(saved);
        testPic_ = isomrules::tilesBitmap(ed.tileset(), ed.terrain().tiles, W, H);
        ed.newMap(16, 16, uint16_t(era_), 1, err);
        setMessage(result + L"  \x00b7  Esc: back");
    }

    // --- The guide (a one-line strip) ----------------------------------------------------------
    struct Guide {
        std::vector<std::wstring> steps;
        int at = 0;
        std::wstring text, a;
        std::function<void()> runA;
    };
    Guide guide() {
        Guide g;
        const std::vector<spass::Cell>* cs = cells();
        if (!cs) return g;
        bool tiles = false, roles = false;
        for (const spass::Cell& c : *cs) tiles = tiles || c.tile >= 0, roles = roles || (c.tile >= 0 && c.role != spass::kNone);
        const auto pick = [this](int c) { return [this, c] { terr_[0][c].openList(); }; };
        if (spass::RampMapping* r = ramp()) {
            g.steps = {L"Tiles", L"Pieces", L"Cliff", L"Terrains", L"Save"};
            const spass::RampShape s = spass::rampShape(*r);
            const spass::RampTerrains t = r->terrains.empty() ? spass::RampTerrains{} : r->terrains[0];
            if (!tiles) g.at = 0, g.text = L"drag a block in the tile grid, click the canvas";
            else if (!roles) g.at = 1, g.text = L"paint Wall A, Slope, Wall B (1\x2013" L"3)", g.a = L"Wall A", g.runA = [this] { pickBrush(0); };
            else if (!s.ok) g.at = 2, g.text = widen(s.why) + L": paint Cliff (high ground, 4), Ground (5)", g.a = L"Cliff", g.runA = [this] { pickBrush(3); };
            else if (t.cliff.empty()) g.at = 3, g.a = L"Cliff\x2026", g.runA = pick(1);
            else if (t.ground.empty()) g.at = 3, g.a = L"Ground\x2026", g.runA = pick(0);
            else if (dirty_) g.at = 4, g.text = L"Ctrl+T test", g.a = L"Save", g.runA = [this] { save(false); };
            else g.at = 5, g.text = L"Ramp brush ready", g.a = L"Test", g.runA = [this] { runTest(); };
        } else if (spass::BridgeMapping* b = bridge()) {
            g.steps = {L"Tiles", L"Pieces", L"Water", L"Terrains", L"Save"};
            const spass::BridgeShape s = spass::bridgeShape(*b);
            const spass::BridgeTerrains t = b->terrains.empty() ? spass::BridgeTerrains{} : b->terrains[0];
            bool water = false;
            for (const spass::Cell& c : *cs) water = water || c.mark == spass::kHigh;
            if (!tiles) g.at = 0, g.text = L"drag a block in the tile grid, click the canvas";
            else if (!s.ok) g.at = 1, g.text = widen(s.why) + L": paint Start, Deck, End (1\x2013" L"3)", g.a = L"Start", g.runA = [this] { pickBrush(0); };
            else if (!water) g.at = 2, g.text = L"paint Water (4) under the deck, Coast (5)", g.a = L"Water", g.runA = [this] { pickBrush(3); };
            else if (t.water.empty()) g.at = 3, g.a = L"Water\x2026", g.runA = pick(1);
            else if (t.coastA.empty()) g.at = 3, g.a = L"Coast A\x2026", g.runA = pick(0);
            else if (dirty_) g.at = 4, g.text = L"Ctrl+T test", g.a = L"Save", g.runA = [this] { save(false); };
            else g.at = 5, g.text = L"Bridge tool ready", g.a = L"Test", g.runA = [this] { runTest(); };
        }
        return g;
    }

    // --- The menus -----------------------------------------------------------------------------
    std::string menuKey() {
        return {char('0' + (sel_ >= 0)), char('0' + isBridge()), char('0' + !undo_.empty()), char('0' + !redo_.empty()), char('0' + overlays_), char('0' + corners_),
                char('0' + (ed_ != nullptr)), char('0' + (terrainRows() > 1)), char('0' + (terrainRows() < passagemap::kMaxTerrainRows)), char('0' + era_ + 1)};
    }
    void updateMenus() {
        if (menuKey_ == menuKey() || bar_.openIndex() >= 0) return;
        menuKey_ = menuKey();
        using Item = stardraft::kit::MenuItem;
        auto item = [this](std::wstring label, std::wstring keys, std::function<void()> run, bool enabled = true) {
            Item it;
            it.label = std::move(label), it.shortcut = std::move(keys), it.enabled = enabled;
            it.run = [this, run = std::move(run)] { run(), focus(); };
            return it;
        };
        auto check = [&](std::wstring label, std::wstring keys, bool on, std::function<void()> run, bool enabled = true) {
            Item it = item(std::move(label), std::move(keys), std::move(run), enabled);
            it.checked = on;
            return it;
        };
        Item sep;
        sep.separator = true;
        const bool any = sel_ >= 0, open = ed_ != nullptr;
        std::vector<Item> file = {item(L"New ramp", L"Ctrl+N", [this] { addMapping(false); }, open), item(L"New bridge", L"Ctrl+B", [this] { addMapping(true); }, open), sep,
                                  item(L"Open\x2026", L"Ctrl+O", [this] { loadFile(); }), sep,
                                  item(L"Save", L"Ctrl+S", [this] { save(false); }), item(L"Save as\x2026", L"Ctrl+Shift+S", [this] { save(true); })};
        std::vector<Item> edit = {item(L"Undo", L"Ctrl+Z", [this] { undo(false); }, !undo_.empty()),
                                  item(L"Redo", L"Ctrl+Y", [this] { undo(true); }, !redo_.empty()), sep,
                                  item(L"Clear canvas", L"", [this] { if (std::vector<spass::Cell>* cs = cells(); cs && !cs->empty()) cs->clear(), edited(); }, any),
                                  item(L"Add terrains", L"", [this] { addTerrainRow(); }, any && terrainRows() < passagemap::kMaxTerrainRows),
                                  item(L"Remove the last terrains", L"", [this] { removeTerrainRow(); }, any && terrainRows() > 1), sep,
                                  item(L"Delete mapping", L"", [this] { removeMapping(); }, any)};
        std::vector<Item> mapping;
        {
            int n = 0;
            const passagemap::BrushDef* bs = passagemap::brushes(isBridge(), n);
            for (int i = 0; i < n; ++i) mapping.push_back(item(bs[i].name, std::to_wstring(i + 1), [this, i] { pickBrush(i); }, any));
        }
        mapping.push_back(sep);
        mapping.push_back(item(L"Marks from heights", L"Ctrl+M", [this] { marksFromHeights(); }, any && open && !isBridge()));
        mapping.push_back(item(L"Test", L"Ctrl+T", [this] { runTest(); }, any && open));
        if (any && !isBridge()) {  // (the tileset's game ramps: a start where the mod's tileset has their tiles)
            Item games;
            games.label = L"Game ramp into canvas";
            for (const int k : stardraft::rampKitsOf(era_, true))
                if (!stardraft::rampKitFromPassage(k))
                    games.sub.push_back(item(widen(stardraft::rampKitGroup(k)) + L" " + widen(stardraft::rampFacingName(stardraft::rampKitFacing(k))), L"",
                                             [this, k] { fromGameRamp(k); }));
            games.enabled = !games.sub.empty();
            mapping.push_back(games);
        }
        if (any && isBridge()) {
            mapping.push_back(item(L"Game bridge into canvas (d)", L"", [this] { fromGameBridge(false); }, stardraft::bridgesAvailable(era_)));
            mapping.push_back(item(L"Game bridge into canvas (u)", L"", [this] { fromGameBridge(true); }, stardraft::bridgesAvailable(era_)));
        }
        std::vector<Item> viewMenu = {check(L"Roles and marks", L"Ctrl+H", overlays_, [this] { overlays_ = !overlays_, invalidate(); }),
                                      check(L"ISOM corners", L"", corners_, [this] { corners_ = !corners_, invalidate(); }), sep,
                                      item(L"Zoom in", L"Ctrl+Wheel", [this] { zoom(1); }), item(L"Zoom out", L"", [this] { zoom(-1); }), sep,
                                      item(L"Go to tile group\x2026", L"", [this] { focus(), jump_.focus(); })};
        if (ed_) {  // (the tileset's ramp doodads: where a mod's ramp art usually is)
            Item doodads;
            doodads.label = L"Go to a ramp doodad";
            for (const uint16_t id : stardraft::rampDoodads(*ed_))
                if (const stardraft::DoodadType* t = ed_->doodadTypes().find(id))
                    doodads.sub.push_back(item(L"Doodad " + std::to_wstring(id) + L"  (" + std::to_wstring(t->width) + L"\x00d7" + std::to_wstring(t->height) + L", group " +
                                                   std::to_wstring(t->firstGroup) + L")",
                                               L"", [this, g = int(t->firstGroup)] { scrollPaletteTo(g); }));
            doodads.enabled = !doodads.sub.empty();
            viewMenu.push_back(doodads);
        }
        bar_.setMenus({{L"File", std::move(file)}, {L"Edit", std::move(edit)}, {L"Mapping", std::move(mapping)}, {L"View", std::move(viewMenu)}});
    }
    static int statusH() { return scale(26); }
    void zoom(int d) {
        const int was = cellSize_;
        cellSize_ = std::clamp(cellSize_ + d * 4, 12, 48);
        if (cellSize_ != was) scrollX_ = scrollX_ * cellSize_ / was, scrollY_ = scrollY_ * cellSize_ / was, clampCanvas(), invalidate();
    }
    void clampCanvas() {
        scrollX_ = std::clamp(scrollX_, 0, std::max(0, passagemap::kCanvasW * cellPx() - canvas_.width()));
        scrollY_ = std::clamp(scrollY_, 0, std::max(0, passagemap::kCanvasH * cellPx() - canvas_.height()));
    }

    // --- The tile grid -------------------------------------------------------------------------
    draw::Bitmap* tilePic(uint16_t v) {
        if (!ed_) return nullptr;
        auto& b = tilePics_[v];
        if (!b) b = isomrules::tilesBitmap(ed_->tileset(), {v}, 1, 1);
        return b.get();
    }
    draw::Bitmap* rowPic(int g) {
        if (!ed_) return nullptr;
        auto& b = rowPics_[g];
        if (!b) {
            std::vector<uint16_t> t;
            for (int s = 0; s < 16; ++s) t.push_back(uint16_t(g << 4 | s));
            b = isomrules::tilesBitmap(ed_->tileset(), t, 16, 1);
        }
        return b.get();
    }
    void scrollPaletteTo(int g) {
        palScroll_ = std::clamp(g, 0, std::max(0, paletteRows() - 1)) * palRowH();
        clampPalette();
        invalidate();
    }
    void clampPalette() { palScroll_ = std::clamp(palScroll_, 0, std::max(0, paletteRows() * palRowH() - pal_.height())); }
    int palTile() const { return scale(palTileW_); }
    int palRowH() const { return palTile() + scale(2); }
    // The columns' widths (a drag at the edge between two columns), kept in the settings.
    int sideW_ = 240, palTileW_ = 20;
    int colDrag_ = -1;
    bool colsRead_ = false;
    int colEdge(draw::Point pt) const {
        const int pad = scale(12);
        if (pt.y < side_.top || pt.y > side_.bottom) return -1;
        if (pt.x >= side_.right && pt.x < side_.right + 2 * pad) return 0;
        if (pt.x >= pal_.left - 2 * pad && pt.x < pal_.left) return 1;
        return -1;
    }

    // The parts of the window (resized).
    draw::Rect top_, side_, pal_, canvas_, guideBox_, strip_, empty_;
    std::vector<draw::Rect> chips_;  // (the brush strip's chips: Place, then the brushes)

    struct Step {
        spass::Passages doc;
        int sel;
    };
    std::vector<Step> undo_, redo_;
    spass::Passages before_;
    bool merging_ = false;

    std::vector<Row> rows_;
    std::vector<std::string> terrainNames_;
    spass::Passages doc_;
    std::filesystem::path file_;
    bool dirty_ = false, shown_ = false, async_ = true, settingsOff_ = false, loading_ = false;
    int sel_ = -1, era_ = -1, openGen_ = 0, palScroll_ = 0, scrollX_ = 0, scrollY_ = 0, cellSize_ = 28;
    std::unique_ptr<Editor> ed_;  // (the tileset's blank map: its tiles and terrains' names)
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    bool toolsHad_[2] = {false, false};
    std::map<uint16_t, std::unique_ptr<draw::Bitmap>> tilePics_;
    std::map<int, std::unique_ptr<draw::Bitmap>> rowPics_;
    std::unique_ptr<draw::Bitmap> testPic_;

    int brush_ = 0;           // (the strip's brush; the stamp when stamp_.on)
    Stamp stamp_;
    bool palDrag_ = false;    // (a drag over the tile grid: the stamp's block)
    int dragRow0_ = 0, dragCol0_ = 0, dragRow1_ = 0, dragCol1_ = 0;
    int paintBtn_ = -1;       // (a stroke in the canvas: 0 left (the brush), 1 right (its layer cleared))
    int hoverX_ = -1, hoverY_ = -1;  // (the canvas's cell under the mouse)
    int panFrom_[4] = {0, 0, 0, 0};  // (a middle drag: the point and the scroll it started at)
    bool panning_ = false;

    isomrules::EditorMenuBar bar_;
    std::string menuKey_;
    std::wstring status_, guideKey_;
    bool overlays_ = true, corners_ = true;
    stardraft::kit::Button addRow_, removeRow_;
    stardraft::kit::Button guideA_, emptyRamp_, emptyBridge_;
    stardraft::kit::ScrollBar palBar_, canvasV_{stardraft::kit::Axis::Vertical}, canvasH_{stardraft::kit::Axis::Horizontal};
    int palRange_[2] = {-1, -1}, canvasRange_[4] = {-1, -1, -1, -1};
    std::function<void()> guideRunA_;
    bool detached_ = false;
    stardraft::kit::ListView list_;
    stardraft::kit::TextField name_, jump_;
    stardraft::kit::Dropdown eraPick_, facing_, grid_;
    stardraft::kit::CheckBox repeat_;
    stardraft::kit::Dropdown terr_[passagemap::kMaxTerrainRows][3];
};

inline void PassagesView::resized() {
    if (!colsRead_) {
        int64_t v = 0;
        colsRead_ = true;
        if (!gSettingsOff && prefs::number(L"Layout/PassagesListWidth", v)) sideW_ = std::clamp(int(v), 140, 600);
        if (!gSettingsOff && prefs::number(L"Layout/PassagesTileSize", v)) palTileW_ = std::clamp(int(v), 10, 40);
    }
    const int pad = scale(12), gap = scale(8), bh = scale(stardraft::kit::kButtonH), sideW = scale(sideW_), palW = scale(44) + 16 * palTile() + scale(14);
    const int menuH = scale(28), bottom = height() - statusH() - pad;
    bar_.setFrame({0, 0, width(), menuH});
    const int y0 = menuH + pad;
    top_ = {0, height() - statusH(), width(), height()};
    eraPick_.setFrame({pad, y0, pad + sideW, y0 + bh});
    side_ = {pad, y0 + bh + gap, pad + sideW, bottom};
    list_.setFrame(side_);
    pal_ = {width() - pad - palW, y0 + bh + gap, width() - pad, bottom};
    jump_.setFrame({pal_.left, y0, pal_.left + scale(100), y0 + bh});
    const int mx = side_.right + pad * 2, mr = pal_.left - pad * 2, lw = scale(64);
    const bool any = sel_ >= 0;
    int y = y0;
    // (the first row: name, facing or deck, repeat, grid)
    name_.setFrame({mx + lw, y, mx + lw + scale(170), y + bh});
    facing_.setFrame({name_.frame().right + gap, y, name_.frame().right + gap + scale(150), y + bh});
    const bool rampRow = any && !isBridge();
    repeat_.setFrame(rampRow ? draw::Rect{facing_.frame().right + gap, y, facing_.frame().right + gap + scale(84), y + bh} : draw::Rect{0, 0, 0, 0});
    const int gx = rampRow ? repeat_.frame().right + gap : facing_.frame().right + gap;
    grid_.setFrame({gx, y, std::min(mr, gx + scale(130)), y + bh});
    y += bh + gap;
    const int cols = std::max(1, terrainCols()), rows = terrainRows();
    const int cw = std::min(scale(220), (mr - mx - lw - 2 * (bh + gap)) / cols - gap);
    for (int r = 0; r < passagemap::kMaxTerrainRows; ++r)
        for (int c = 0; c < 3; ++c) {
            const bool on = r < rows && c < terrainCols();
            terr_[r][c].setFrame(on ? draw::Rect{mx + lw + c * (cw + gap), y + scale(18) + r * (bh + gap), mx + lw + c * (cw + gap) + cw, y + scale(18) + r * (bh + gap) + bh}
                                    : draw::Rect{0, 0, 0, 0});
            terr_[r][c].setVisible(on);
        }
    const int ty = y + scale(18), tx = mx + lw + cols * (cw + gap);
    addRow_.setFrame(any ? draw::Rect{tx, ty, tx + bh, ty + bh} : draw::Rect{0, 0, 0, 0});
    removeRow_.setFrame(any ? draw::Rect{tx + bh + gap, ty, tx + 2 * bh + gap, ty + bh} : draw::Rect{0, 0, 0, 0});
    y = ty + std::max(1, rows) * (bh + gap);
    // (the guide: a one-line strip)
    guideBox_ = any ? draw::Rect{mx, y, mr, y + bh + scale(8)} : draw::Rect{0, 0, 0, 0};
    {
        const int bw = scale(110), by = guideBox_.top + scale(4);
        guideA_.setFrame(any ? draw::Rect{guideBox_.right - scale(4) - bw, by, guideBox_.right - scale(4), by + bh} : draw::Rect{0, 0, 0, 0});
    }
    y = guideBox_.bottom + gap;
    // (the brush strip: Place, then the brushes)
    strip_ = any ? draw::Rect{mx, y, mr, y + bh} : draw::Rect{0, 0, 0, 0};
    y = strip_.bottom + gap;
    const int barW = scale(stardraft::kit::kScrollBarW);
    canvas_ = any ? draw::Rect{mx, y, mr - barW, bottom - scale(22) - barW} : draw::Rect{0, 0, 0, 0};
    canvasV_.setFrame(any ? draw::Rect{canvas_.right, canvas_.top, canvas_.right + barW, canvas_.bottom} : draw::Rect{0, 0, 0, 0});
    canvasH_.setFrame(any ? draw::Rect{canvas_.left, canvas_.bottom, canvas_.right, canvas_.bottom + barW} : draw::Rect{0, 0, 0, 0});
    for (draw::View* v : std::initializer_list<draw::View*>{&name_, &facing_, &grid_, &addRow_, &removeRow_, &guideA_, &canvasV_, &canvasH_}) v->setVisible(any);
    repeat_.setVisible(rampRow);
    {  // (no mapping: two big buttons in the middle)
        const int bw = scale(200), bh2 = scale(44), cx = (mx + mr) / 2, cy = y0 + scale(170);
        empty_ = !any && ed_ ? draw::Rect{mx, y0 + scale(60), mr, cy + bh2 + scale(60)} : draw::Rect{0, 0, 0, 0};
        emptyRamp_.setFrame(empty_.empty() ? draw::Rect{0, 0, 0, 0} : draw::Rect{cx - bw - scale(8), cy, cx - scale(8), cy + bh2});
        emptyBridge_.setFrame(empty_.empty() ? draw::Rect{0, 0, 0, 0} : draw::Rect{cx + scale(8), cy, cx + bw + scale(8), cy + bh2});
        emptyRamp_.setVisible(!empty_.empty()), emptyBridge_.setVisible(!empty_.empty());
    }
    palBar_.setFrame({pal_.right - barW, pal_.top, pal_.right, pal_.bottom});
    clampPalette(), clampCanvas();
}

inline void PassagesView::paint(draw::Canvas& c) {
    c.fill({0, 0, width(), height()}, kBg);
    for (const int x : {side_.right + scale(12), pal_.left - scale(12)})
        c.fill({x, side_.top, x + 1, side_.bottom}, colDrag_ >= 0 ? kAccent : kBorder);
    const auto label = [&](draw::Rect r, const std::wstring& t, draw::Color col = kDim) { c.text(r, t, col, draw::kLeft | draw::kMiddle | draw::kEllipsis); };
    updateMenus();
    c.fill({0, bar_.frame().bottom, width(), bar_.frame().bottom + 1}, kBorder);
    {  // The status bar: the last message at the left; the tileset and the file at the right.
        const int pad = scale(8);
        c.fill(top_, kPanel), c.fill({0, top_.top, width(), top_.top + 1}, kBorder);
        std::wstring right = era_ >= 0 ? isomrules::tilesetTitle(era_) + (ed_ ? L", " + std::to_wstring(paletteRows()) + L" groups" : std::wstring(L", loading\x2026")) + L"  \x00b7  "
                                       : std::wstring();
        right += file_.empty() ? std::wstring(L"Untitled") : file_.wstring();
        const int fw = std::min(width() / 2, c.measure(right) + 2 * pad);
        c.text({top_.right - fw, top_.top, top_.right - pad, top_.bottom}, right, kText, draw::kMiddle | draw::kRight | draw::kEllipsis);
        c.text({top_.left + pad, top_.top, top_.right - fw - pad, top_.bottom}, loading_ ? std::wstring(L"Loading the tileset\x2026") : status_, kDim,
               draw::kMiddle | draw::kLeft | draw::kEllipsis);
    }
    const int mx = side_.right + scale(24), lw = scale(64);
    if (!empty_.empty())
        c.text({empty_.left, empty_.top, empty_.right, empty_.top + scale(30)}, isomrules::tilesetTitle(era_) + L": ramps & bridges", kText, draw::kCenter | draw::kMiddle);

    // The tile grid: a group a row; the stamp's block framed.
    c.fill(pal_, kPanel);
    if (const int total = paletteRows() * palRowH(); total != palRange_[0] || pal_.height() != palRange_[1])
        palBar_.setRange(total, pal_.height()), palRange_[0] = total, palRange_[1] = pal_.height();
    palBar_.setPos(palScroll_);
    c.pushClip(pal_);
    {
        const int first = palScroll_ / palRowH(), last = std::min(paletteRows(), (palScroll_ + pal_.height()) / palRowH() + 1), x = pal_.left + scale(44);
        // (the tiles in the canvas: a dot)
        std::set<uint16_t> used;
        if (const std::vector<spass::Cell>* cs = const_cast<PassagesView*>(this)->cells())
            for (const spass::Cell& cl : *cs)
                if (cl.tile >= 0) used.insert(uint16_t(cl.tile));
        for (int row = first; row < last; ++row) {
            const int y = pal_.top + row * palRowH() - palScroll_;
            label({pal_.left + scale(4), y, x, y + palTile()}, std::to_wstring(row));
            if (draw::Bitmap* b = rowPic(row)) c.bitmap(*b, {0, 0, b->width(), b->height()}, {x, y, x + 16 * palTile(), y + palTile()});
            for (int k = 0; k < 16; ++k)
                if (used.count(uint16_t(row << 4 | k))) {
                    const draw::Rect r{x + k * palTile(), y, x + (k + 1) * palTile(), y + palTile()};
                    c.fill({r.right - scale(6), r.top, r.right, r.top + scale(6)}, kAccentBright);
                }
        }
        const auto block = [&](int g0, int c0, int g1, int c1, draw::Color col, int w) {
            c.frame({x + c0 * palTile() - 1, pal_.top + g0 * palRowH() - palScroll_ - 1, x + (c1 + 1) * palTile() + 1, pal_.top + g1 * palRowH() + palTile() - palScroll_ + 1}, col,
                    w);
        };
        if (palDrag_) block(std::min(dragRow0_, dragRow1_), std::min(dragCol0_, dragCol1_), std::max(dragRow0_, dragRow1_), std::max(dragCol0_, dragCol1_), kAccentBright, scale(2));
        else if (stamp_.on) block(stamp_.g0, stamp_.c0, stamp_.g1, stamp_.c1, kAccentBright, scale(2));
    }
    if (loading_) c.text(pal_, L"Loading\x2026", kDim, draw::kCenter | draw::kMiddle);
    c.popClip();

    if (sel_ < 0) return;
    const bool bridgeRow = isBridge();
    label({mx, name_.frame().top, mx + lw, name_.frame().bottom}, L"Name");
    {
        const int y = name_.frame().bottom + scale(8);
        const wchar_t* heads[3] = {bridgeRow ? L"Coast A" : L"Ground", bridgeRow ? L"Water" : L"Cliff", L"Coast B"};
        for (int k = 0; k < terrainCols(); ++k) label({terr_[0][k].frame().left, y, terr_[0][k].frame().right, y + scale(18)}, heads[k]);
        label({mx, y + scale(18), mx + lw, y + scale(18) + scale(stardraft::kit::kButtonH)}, L"Terrains");
    }
    {  // The guide: a dot a step, the step's name and text, its button.
        const Guide g = guide();
        guideRunA_ = g.runA;
        if (const std::wstring key = g.a + std::to_wstring(g.at); key != guideKey_) guideKey_ = key, guideA_.setText(g.a);
        guideA_.setVisible(!g.a.empty());
        const int aw = guideA_.preferredWidth(c);
        const draw::Rect gb = guideA_.frame();
        guideA_.setFrame({guideBox_.right - scale(4) - aw, gb.top, guideBox_.right - scale(4), gb.bottom});
        c.fill(guideBox_, kCtl), c.frame(guideBox_, kAccentLine, 1);
        int x = guideBox_.left + scale(10);
        const int dy = guideBox_.top + guideBox_.height() / 2, d = scale(8);
        for (size_t i = 0; i < g.steps.size(); ++i) {
            const draw::Rect r{x, dy - d / 2, x + d, dy + d / 2};
            if (int(i) <= g.at) c.fill(r, int(i) == g.at ? kAccentBright : kAccentLine);
            else c.frame(r, kBorder, 1);
            x = r.right + scale(4);
        }
        const std::wstring now = g.at < int(g.steps.size()) ? g.steps[size_t(g.at)] : std::wstring(L"Done");
        c.text({x + scale(6), guideBox_.top, x + scale(6) + c.measure(now), guideBox_.bottom}, now, kText, draw::kLeft | draw::kMiddle);
        x += scale(12) + c.measure(now);
        c.text({x, guideBox_.top, (g.a.empty() ? guideBox_.right : guideA_.frame().left) - scale(8), guideBox_.bottom}, g.text, kDim, draw::kLeft | draw::kMiddle | draw::kEllipsis);
    }
    // The brush strip: Place (the stamp), then the roles and marks, each with its color and key: the canvas's key.
    int nb = 0;
    const passagemap::BrushDef* bs = passagemap::brushes(bridgeRow, nb);
    {
        chips_.clear();
        int x = strip_.left;
        const int sw = scale(12);
        const auto chip = [&](const std::wstring& t, draw::Color col, bool on, bool hatch, int key) {
            const int w = scale(18) + sw + c.measure(t) + (key ? scale(22) : 0);
            const draw::Rect r{x, strip_.top, x + w, strip_.bottom};
            c.fill(r, on ? kCtlHot : kCtl), c.frame(r, on ? kAccentBright : kBorder, on ? scale(2) : 1);
            const draw::Rect s{r.left + scale(7), r.top + (r.height() - sw) / 2, r.left + scale(7) + sw, r.top + (r.height() - sw) / 2 + sw};
            if (col != draw::kNone) {
                if (hatch) c.frame(s, col, scale(2)), c.fill({s.left + scale(3), s.top + scale(3), s.right - scale(3), s.bottom - scale(3)}, col, 120);
                else c.fill(s, col);
            }
            c.text({s.right + scale(5), r.top, r.right, r.bottom}, t, on ? kText : kDim, draw::kLeft | draw::kMiddle);
            if (key) c.text({r.right - scale(20), r.top, r.right - scale(4), r.bottom}, std::to_wstring(key), kDim, draw::kRight | draw::kMiddle);
            chips_.push_back(r);
            x = r.right + scale(6);
        };
        chip(stamp_.on ? L"Place " + std::to_wstring(stamp_.w()) + L"\x00d7" + std::to_wstring(stamp_.h()) : std::wstring(L"Place"), draw::kNone, stamp_.on, false, 0);
        for (int i = 0; i < nb; ++i) chip(bs[i].name, bs[i].erase ? draw::kNone : bs[i].color, !stamp_.on && brush_ == i, bs[i].mark != 0, i + 1);
        // (the corners' key)
        if (corners_) {
            const int r0 = scale(5), cy = strip_.top + strip_.height() / 2;
            x += scale(8);
            for (int k = 0; k < 2; ++k) {
                const draw::Color col = passagemap::markColor(bridgeRow, k ? spass::kLow : spass::kHigh);
                c.ellipse({x, cy - r0, x + 2 * r0, cy + r0}, col, RGB(20, 20, 20));
                const std::wstring t = k ? (bridgeRow ? L"coast corner" : L"ground corner") : (bridgeRow ? L"water corner" : L"cliff corner");
                c.text({x + 2 * r0 + scale(4), strip_.top, x + 2 * r0 + scale(4) + c.measure(t), strip_.bottom}, t, kDim, draw::kLeft | draw::kMiddle);
                x += 2 * r0 + scale(12) + c.measure(t);
            }
        }
    }
    // The canvas: the cells' tiles, their roles (a fill) and marks (an inner frame), the ISOM corners the tools check.
    c.fill(canvas_, kPanel);
    {
        const int total[2] = {passagemap::kCanvasW * cellPx(), passagemap::kCanvasH * cellPx()};
        if (total[0] != canvasRange_[0] || canvas_.width() != canvasRange_[1]) canvasH_.setRange(total[0], canvas_.width()), canvasRange_[0] = total[0], canvasRange_[1] = canvas_.width();
        if (total[1] != canvasRange_[2] || canvas_.height() != canvasRange_[3]) canvasV_.setRange(total[1], canvas_.height()), canvasRange_[2] = total[1], canvasRange_[3] = canvas_.height();
        canvasH_.setPos(scrollX_), canvasV_.setPos(scrollY_);
    }
    c.pushClip(canvas_);
    const int s = cellPx();
    if (testPic_) {  // (Test: the map's picture, as large as fits)
        const int w = testPic_->width(), h = testPic_->height();
        const double k = std::min(double(canvas_.width()) / w, double(canvas_.height()) / h);
        const int dw = int(w * k), dh = int(h * k), l = canvas_.left + (canvas_.width() - dw) / 2, t = canvas_.top + (canvas_.height() - dh) / 2;
        c.bitmap(*testPic_, {0, 0, w, h}, {l, t, l + dw, t + dh}, true);
        c.popClip();
        return;
    }
    {  // (the grid lines, faint; the canvas's end)
        const int x0 = canvas_.left - scrollX_, y0 = canvas_.top - scrollY_;
        for (int gx = 0; gx <= passagemap::kCanvasW; ++gx) c.fill({x0 + gx * s, std::max(canvas_.top, y0), x0 + gx * s + 1, std::min(canvas_.bottom, y0 + passagemap::kCanvasH * s)}, kBorder, 90);
        for (int gy = 0; gy <= passagemap::kCanvasH; ++gy) c.fill({std::max(canvas_.left, x0), y0 + gy * s, std::min(canvas_.right, x0 + passagemap::kCanvasW * s), y0 + gy * s + 1}, kBorder, 90);
    }
    std::vector<spass::Cell>* cs = cells();
    if (cs) {
        for (const spass::Cell& cl : *cs) {
            const draw::Rect r = cellRect(cl.x, cl.y);
            if (r.right < canvas_.left || r.left > canvas_.right || r.bottom < canvas_.top || r.top > canvas_.bottom) continue;
            if (cl.tile >= 0)
                if (draw::Bitmap* b = tilePic(uint16_t(cl.tile))) c.bitmap(*b, {0, 0, b->width(), b->height()}, r);
            if (!overlays_) continue;
            if (const draw::Color rc = passagemap::roleColor(bridgeRow, cl.role); rc != draw::kNone && cl.tile >= 0) c.fill(r, rc, 80), c.frame(r, rc, 1);
            if (const draw::Color mc = passagemap::markColor(bridgeRow, cl.mark); mc != draw::kNone) {
                const int in = std::max(2, s / 7);
                if (cl.tile < 0) c.fill({r.left + 1, r.top + 1, r.right, r.bottom}, mc, 70);  // (a mark with no tile: the cell painted)
                else c.frame({r.left + in, r.top + in, r.right - in, r.bottom - in}, mc, std::max(2, s / 10));
            }
        }
        // (the repeat: the next slope or deck, dashed, where the tools put it)
        std::vector<spass::Cell> next;
        int sx = 0, sy = 0;
        if (spass::RampMapping* r = ramp(); r && r->repeat) {
            const spass::RampShape sh = spass::rampShape(*r);
            if (sh.ok) next = sh.pieces[1], sx = sh.stepX, sy = sh.stepY;
        } else if (spass::BridgeMapping* b = bridge()) {
            const spass::BridgeShape sh = spass::bridgeShape(*b);
            if (sh.ok) next = sh.pieces[1], sx = sh.stepX, sy = sh.stepY;
        }
        for (const spass::Cell& cl : next) {
            const draw::Rect r = cellRect(cl.x + sx, cl.y + sy);
            c.frame({r.left + 1, r.top + 1, r.right - 1, r.bottom - 1}, passagemap::roleColor(bridgeRow, bridgeRow ? spass::kDeck : spass::kSlope), 1, draw::LineStyle::Dash);
        }
        // (the ISOM corners the tools check: a dot each)
        if (corners_) {
            bool* a = nullptr;
            const int* g = gridOf(&a);
            const int r0 = std::max(3, s / 7);
            for (const spass::CanvasCorner& k : spass::cornersOf(*cs, g ? *g : 0, bridgeRow)) {
                const int px = canvas_.left - scrollX_ + k.x * s, py = canvas_.top - scrollY_ + k.y * s;
                c.ellipse({px - r0, py - r0, px + r0, py + r0}, passagemap::markColor(bridgeRow, k.high ? spass::kHigh : spass::kLow), RGB(20, 20, 20));
            }
        }
    }
    // (the stamp where a click puts it; the brush's cell)
    if (hoverX_ >= 0 && stamp_.on && ed_) {
        for (int dy = 0; dy < stamp_.h(); ++dy)
            for (int dx = 0; dx < stamp_.w(); ++dx) {
                const draw::Rect r = cellRect(hoverX_ + dx, hoverY_ + dy);
                if (draw::Bitmap* b = tilePic(uint16_t((stamp_.g0 + dy) << 4 | (stamp_.c0 + dx)))) c.bitmap(*b, {0, 0, b->width(), b->height()}, r, false, 150);
            }
        const draw::Rect a = cellRect(hoverX_, hoverY_), b = cellRect(hoverX_ + stamp_.w() - 1, hoverY_ + stamp_.h() - 1);
        c.frame({a.left, a.top, b.right, b.bottom}, kAccentBright, scale(2));
    } else if (hoverX_ >= 0 && brush_ >= 0 && brush_ < nb) {
        c.frame(cellRect(hoverX_, hoverY_), bs[brush_].erase ? kText : bs[brush_].color, scale(2));
    }
    if (cs && cs->empty() && !stamp_.on)
        c.text(canvas_, L"Drag a block in the tile grid \x2192 click here", kDim, draw::kCenter | draw::kMiddle);
    c.popClip();
    // (the shape the tools read: under the canvas)
    {
        std::wstring line;
        if (spass::RampMapping* r = ramp()) {
            const spass::RampShape sh = spass::rampShape(*r);
            int hi = 0;
            for (const spass::CanvasCorner& k : sh.corners) hi += k.high;
            line = sh.ok ? L"Ramp " + widen(stardraft::rampFacingName(sh.facing)) + (r->facing < 0 ? L" (auto)" : L"") + L"  \x00b7  " +
                               (r->repeat ? L"slope \x00d7 1\x2013" L"64, step " + std::to_wstring(sh.stepX) + L"," + std::to_wstring(sh.stepY)
                                          : std::wstring(sh.pieces[1].empty() ? L"walls only" : L"as painted")) +
                               L"  \x00b7  corners " + std::to_wstring(hi) + L" cliff, " + std::to_wstring(int(sh.corners.size()) - hi) + L" ground  \x00b7  grid " +
                               std::to_wstring(r->grid)
                         : L"\x2717 " + widen(sh.why);
        } else if (spass::BridgeMapping* b = bridge()) {
            const spass::BridgeShape sh = spass::bridgeShape(*b);
            int hi = 0;
            for (const spass::CanvasCorner& k : sh.corners) hi += k.high;
            line = sh.ok ? std::wstring(L"Bridge deck ") + (sh.deck ? L"u" : L"d") + (b->deck < 0 ? L" (auto)" : L"") + L"  \x00b7  deck \x00d7 1\x2013" L"64, step " +
                               std::to_wstring(sh.stepX) + L"," + std::to_wstring(sh.stepY) + L"  \x00b7  corners " + std::to_wstring(hi) + L" water, " +
                               std::to_wstring(int(sh.corners.size()) - hi) + L" coast  \x00b7  grid " + std::to_wstring(b->grid)
                         : L"\x2717 " + widen(sh.why);
        }
        label({canvas_.left, canvasH_.frame().bottom + scale(2), canvas_.right, canvasH_.frame().bottom + scale(22)}, line, kText);
    }
}

inline draw::Cursor PassagesView::cursor(draw::Point pt) {
    if (colDrag_ >= 0 || colEdge(pt) >= 0) return draw::Cursor::SizeWE;
    return draw::Cursor::Arrow;
}

inline bool PassagesView::mouse(const draw::MouseEvent& e) {
    // The columns: a drag at the edge between two.
    if (colDrag_ >= 0 && (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Up)) {
        const int unit = scale(1000);
        if (colDrag_ == 0) sideW_ = std::clamp((e.pt.x - scale(12)) * 1000 / unit, 140, 600);
        else palTileW_ = std::clamp(((width() - scale(12) - e.pt.x - scale(12)) * 1000 / unit - 58) / 16, 10, 40);
        if (e.kind == draw::MouseKind::Up) {
            colDrag_ = -1;
            if (host()) host()->capture(*this, false);
            if (!gSettingsOff && !settingsOff_) prefs::setNumber(L"Layout/PassagesListWidth", sideW_), prefs::setNumber(L"Layout/PassagesTileSize", palTileW_);
        }
        resized(), invalidate();
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 0)
        if (const int edge = colEdge(e.pt); edge >= 0) {
            colDrag_ = edge;
            if (host()) host()->capture(*this, true);
            return true;
        }
    if (e.kind == draw::MouseKind::Wheel) {
        if (pal_.contains(e.pt)) return palScroll_ += int(std::lround(-e.wheel * 3 * palRowH())), clampPalette(), invalidate(), true;
        if (canvas_.contains(e.pt)) {
            if (e.mods & draw::kCtrl) return zoom(e.wheel > 0 ? 1 : -1), true;
            const int d = int(std::lround(-e.wheel * 3 * cellPx()));
            ((e.mods & draw::kShift) || e.horizontal ? scrollX_ : scrollY_) += d;
            return clampCanvas(), invalidate(), true;
        }
        return false;
    }
    const auto palAt = [&](draw::Point pt, int& row, int& col) {
        row = std::clamp((pt.y - pal_.top + palScroll_) / palRowH(), 0, std::max(0, paletteRows() - 1));
        col = std::clamp((pt.x - pal_.left - scale(44)) / palTile(), 0, 15);
    };
    // A drag over the tile grid: the stamp's block (released: placing in the canvas).
    if (palDrag_ && (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Up)) {
        palAt(e.pt, dragRow1_, dragCol1_);
        if (e.kind == draw::MouseKind::Up) {
            palDrag_ = false;
            if (host()) host()->capture(*this, false);
            stamp_ = {true, std::min(dragRow0_, dragRow1_), std::min(dragCol0_, dragCol1_), std::max(dragRow0_, dragRow1_), std::max(dragCol0_, dragCol1_)};
            setMessage(L"Block " + std::to_wstring(stamp_.w()) + L"\x00d7" + std::to_wstring(stamp_.h()) + L" (group " + std::to_wstring(stamp_.g0) + L")  \x00b7  click the canvas");
        }
        return invalidate(), true;
    }
    // A middle drag pans the canvas.
    if (panning_ && (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Up)) {
        scrollX_ = panFrom_[2] - (e.pt.x - panFrom_[0]), scrollY_ = panFrom_[3] - (e.pt.y - panFrom_[1]), clampCanvas();
        if (e.kind == draw::MouseKind::Up) {
            panning_ = false;
            if (host()) host()->capture(*this, false);
        }
        return invalidate(), true;
    }
    // A stroke in the canvas: each cell it goes over.
    if (paintBtn_ >= 0 && (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Up)) {
        int x = 0, y = 0;
        if (cellAt(e.pt, x, y)) {
            hoverX_ = x, hoverY_ = y;
            if (paintCell(x, y, paintBtn_ == 1)) edited(true);
        }
        if (e.kind == draw::MouseKind::Up) {
            paintBtn_ = -1, merging_ = false;
            if (host()) host()->capture(*this, false);
            showSelectedLight();
        }
        return invalidate(), true;
    }
    if (e.kind == draw::MouseKind::Move) {
        int x = -1, y = -1;
        if (!cellAt(e.pt, x, y)) x = y = -1;
        if (x != hoverX_ || y != hoverY_) hoverX_ = x, hoverY_ = y, invalidate();
        return false;
    }
    if (e.kind == draw::MouseKind::Leave && hoverX_ >= 0) return hoverX_ = hoverY_ = -1, invalidate(), false;
    if (e.kind != draw::MouseKind::Down) return false;
    if (pal_.contains(e.pt) && e.pt.x >= pal_.left + scale(44) && e.button == 0) {
        if (paletteRows() <= 0) return true;
        palAt(e.pt, dragRow0_, dragCol0_);
        dragRow1_ = dragRow0_, dragCol1_ = dragCol0_, palDrag_ = true;
        if (host()) host()->capture(*this, true);
        return invalidate(), true;
    }
    if (sel_ < 0) return false;
    if (testPic_ && canvas_.contains(e.pt)) return testPic_.reset(), invalidate(), true;
    // The brush strip: Place (the stamp again) or a brush.
    for (size_t i = 0; i < chips_.size(); ++i)
        if (chips_[i].contains(e.pt) && e.button == 0) {
            if (i == 0) {
                if (stamp_.g1 >= stamp_.g0 && (stamp_.g0 || stamp_.g1 || stamp_.c1)) stamp_.on = true;
                else setMessage(L"Drag a block in the tile grid first");
            } else pickBrush(int(i) - 1);
            return invalidate(), true;
        }
    int x = 0, y = 0;
    if (!cellAt(e.pt, x, y)) return false;
    focus();
    if (e.button == 2) {
        panning_ = true, panFrom_[0] = e.pt.x, panFrom_[1] = e.pt.y, panFrom_[2] = scrollX_, panFrom_[3] = scrollY_;
        if (host()) host()->capture(*this, true);
        return true;
    }
    if (stamp_.on && e.button == 0) return placeStamp(x, y), true;
    if (stamp_.on && e.button == 1) return stamp_.on = false, invalidate(), true;
    paintBtn_ = e.button == 1 ? 1 : 0;
    merging_ = false;
    if (paintCell(x, y, paintBtn_ == 1)) edited(true);
    if (host()) host()->capture(*this, true);
    return invalidate(), true;
}

PassagesView gPassagesView;
void passagesShow() { gPassagesView.open(); }
bool passagesConfirmQuit() { return gPassagesView.confirmClose(); }
void passagesFollow() { gPassagesView.follow(); }
// At the start (Cosmonarchy): the passages file of the settings (PassagesFile, PassagesOn: on when not set), for the tools.
void cmbwUsePassagesSetting() {
    std::wstring saved;
    int64_t on = 1;
    if (gSettingsOff || !cmbwMode() || !prefs::text(L"PassagesFile", saved) || saved.empty()) return;  // (the mod's tilesets only)
    prefs::number(L"PassagesOn", on);
    PassagesView::inUseFile() = std::filesystem::path(saved), PassagesView::inUseOn() = on != 0;
    if (!on) return;
    stardraft::passages::Passages p;
    std::string err;
    if (!stardraft::passages::load(std::filesystem::path(saved), p, err)) return shellLogLine(L"The passages file could not be read: " + widen(err));
    stardraft::passages::use(p);
}
// Program options > Plugins > Ramp and bridge mappings (its check box, its file and Browse).
bool passagesOn() { return PassagesView::inUseOn() && !PassagesView::inUseFile().empty(); }
std::filesystem::path passagesFile() { return PassagesView::inUseFile(); }
// The file on or off for the tools (now: the open maps' Ramp brush and Bridge tool too).
void passagesApply() {
    stardraft::passages::Passages p;
    std::string err;
    if (passagesOn() && !stardraft::passages::load(passagesFile(), p, err)) {
        shellErrorBox(widen(err), L"Ramp and bridge mappings");
        PassagesView::inUseOn() = false;
    }
    stardraft::passages::use(passagesOn() ? p : stardraft::passages::Passages{});
    if (!gSettingsOff) prefs::setText(L"PassagesFile", passagesFile().wstring()), prefs::setNumber(L"PassagesOn", passagesOn() ? 1 : 0);
    gPassagesView.settingChanged();
    shellPaletteRebuilt();
    setMessage(passagesOn() ? L"Ramp and bridge mappings on: the Ramp brush and the Bridge tool use " + passagesFile().filename().wstring()
                            : std::wstring(L"Ramp and bridge mappings off: the game's ramps and bridges only"));
}
void passagesChoose() {
    ShellFilePick d;
    d.title = L"Select a passages file";
    d.filters = {{L"Passages (*.scpass)", L"*.scpass"}};
    if (!passagesFile().empty()) d.initialDir = passagesFile().parent_path();
    const std::filesystem::path f = shellPickFile(d);
    if (f.empty()) return;
    stardraft::passages::Passages p;
    std::string err;
    if (!stardraft::passages::load(f, p, err)) return shellErrorBox(widen(err), L"Ramp and bridge mappings");  // (the file in use stays)
    PassagesView::inUseFile() = f, PassagesView::inUseOn() = true;
    passagesApply();
}
void passagesToggle() {
    if (!passagesOn() && passagesFile().empty()) return passagesChoose();
    PassagesView::inUseOn() = !PassagesView::inUseOn();
    passagesApply();
}
