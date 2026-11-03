// Tools > Cosmonarchy > Ramp and bridge mapper (portable, core.inc; stardraft/passages.h). The mod's tilesets have
// ramps and bridges of their own tiles; a mapping takes a game kit as its template (a ramp block in its 8 facings, or a
// tileset's bridge kit) and gives each template tile a tile of the open map's tileset, and the ISOM terrains it fits
// on: Ground and Cliff for a ramp, Coast A, Water and Coast B for a bridge. The template comes from the terrains: a
// ramp's is the game's ramp block of the cliff its Cliff stands in for (a mod terrain's shapes are a game terrain's), a
// bridge's the tileset's own bridge kit (a Template list only on a tileset with none). The Ramp brush and the Bridge
// tool use the mappings at once (passages::use); Save writes the passages file (.scpass), which loads again at the next
// start. Left: the mappings of the map's tileset. Middle: the mapping's name, shape, terrains; the template and the result
// (one facing or orientation); the pieces. Right: the tileset's tiles, a tile group a row. The pieces: a ramp's wall A,
// slope and wall B of the shown facing; a bridge's blocks of template tiles (a run of tile groups each: a doodad's rows).
// Each has a color: its tiles show it in the template, the result and the tileset's tiles. Pick a piece, then click a
// tileset tile: the piece goes there (that tile: its top left), with its rows and columns as the template has them; the
// next piece with tiles to map is picked. Or click a template tile (the target), then a tileset tile: the target's whole row of tiles maps to
// that row, at the same offset (Alt+click: the target only); a drag over a block of tileset tiles maps the block onto the
// template's tiles from the target (rows: tile groups, columns: subtiles, as the template's doodads have them). Delete
// clears the target's tile. As the Custom isom terrain editor: a menu bar (File, Edit, Mapping, View: the commands, with
// their keys), a status bar (the window's messages; the tileset and the file), and its keys: Up / Down step the target,
// Ctrl+L the next piece with tiles to map, Esc no target, Ctrl+Z / Ctrl+Y undo and redo, Ctrl+S (Shift: Save as),
// Ctrl+O, Ctrl+N (a ramp), Ctrl+H the pieces' colors. The steps it leads through: New ramp opens the Cliff list (the
// shape and the name come from it); then each piece in turn is picked, with the places in the tileset's tiles whose
// pictures are most like its template tiles framed (1 the best; F3 the next, Enter puts it there), or Map all pieces puts
// each at its best place at once (one undo step); the Facing list shows each facing's pieces done. The guide (a card
// over the pictures, View > Guide) shows those steps for the selected mapping: the step it is at, what to do, one or two
// buttons that do it, and a frame round the part of the window it is about; with no mapping, two big buttons start one.
// Terse texts: a tool for the mod's tile makers (not STE100).

void shellShowPassagesWindow();                          // (the host: the window, once)
void shellSetPassagesTitle(const std::wstring& title);   // (the host: the window's title)
void shellPaletteRebuilt();
bool shellOpenGameData();  // (the host: the game's own data, which Cosmonarchy mode does not open at the start)

namespace spass = stardraft::passages;

namespace passagemap {

constexpr int kMaxTerrainRows = 4;
inline uint16_t groupOf(uint16_t v) { return uint16_t(v >> 4); }

// The game kits of a tileset's ramp block (its first doodad): one a facing.
inline std::vector<int> blockKits(int era, int block) {
    std::vector<int> out;
    for (const int k : stardraft::rampKitsOf(era, true))
        if (!stardraft::rampKitFromPassage(k) && stardraft::rampKitBase(k) == block) out.push_back(k);
    return out;
}

}  // namespace passagemap

class PassagesView final : public draw::View {
public:
    PassagesView()
        : addRow_(L"+", stardraft::kit::Tone::Normal, [this] { addTerrainRow(); }),
          removeRow_(L"\x2212", stardraft::kit::Tone::Normal, [this] { removeTerrainRow(); }) {
        addRow_.setTip(L"Add terrains it fits on");
        removeRow_.setTip(L"Remove the last terrains");
        list_.label = [this](int i) { return rowLabel(i); };
        list_.onSelect = [this] { selected(); };
        list_.setEmptyHint(L"No mappings for this tileset");
        name_.onChange = [this] {
            if (std::string* n = nameOf()) *n = narrow(name_.text()), edited(true), list_.invalidate();
        };
        name_.setPlaceholder(L"Name");
        template_.onChange = [this](int i) { pickTemplate(i); };
        template_.searchable = true, template_.wheelSteps = false;
        view_.onChange = [this](int i) { view_.setValue(i), target_ = -1, activePiece_ = activePiece_ >= 0 && ramp() ? 0 : activePiece_, gridScroll_ = 0, invalidate(); };
        view_.wheelSteps = false;
        jump_.setPlaceholder(L"Group");
        jump_.setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
        jump_.setMaxLength(5);
        jump_.onEnter = [this] {
            const int g = jump_.text().empty() ? 0 : std::stoi(jump_.text());
            scrollPaletteTo(g);
        };
        for (int r = 0; r < passagemap::kMaxTerrainRows; ++r)
            for (int c = 0; c < 3; ++c) {
                terr_[r][c].searchable = true, terr_[r][c].wheelSteps = false;
                terr_[r][c].onChange = [this, r, c](int i) { pickTerrain(r, c, i); };
                add(terr_[r][c], {0, 0, 0, 0});
            }
        guideA_ = stardraft::kit::Button(L"", stardraft::kit::Tone::Primary, [this] { if (guideRunA_) guideRunA_(); });
        guideB_ = stardraft::kit::Button(L"", stardraft::kit::Tone::Normal, [this] { if (guideRunB_) guideRunB_(); });
        guideHide_ = stardraft::kit::Button(L"\x00d7", stardraft::kit::Tone::Quiet, [this] { guideOn_ = false, resized(), invalidate(), setMessage(L"View > Guide shows it again"); });
        guideHide_.setTip(L"Hide the guide (View > Guide)");
        emptyRamp_ = stardraft::kit::Button(L"New ramp", stardraft::kit::Tone::Primary, [this] { addMapping(false); });
        emptyBridge_ = stardraft::kit::Button(L"New bridge", stardraft::kit::Tone::Primary, [this] { addMapping(true); });
        for (draw::View* v : std::initializer_list<draw::View*>{&bar_, &addRow_, &removeRow_, &list_, &name_, &template_, &view_, &jump_, &guideA_, &guideB_, &guideHide_,
                                                                 &emptyRamp_, &emptyBridge_})
            add(*v, {0, 0, 0, 0});
    }
    static draw::Point size() { return {scale(1280), scale(800)}; }

    // Tools > Cosmonarchy > Ramp and bridge mapper: the window with the passages in use.
    void open() {
        if (!shown_) {
            loadSettingFile();
            shown_ = true;
        }
        refresh();
        shellShowPassagesWindow();
    }
    // A snapshot (--dialog passages:<file>[:<row>]): that file's passages in the window, a row selected, its first tile
    // the target.
    void openForShot(const std::filesystem::path& f, int row) {
        spass::Passages p;
        std::string err;
        if (!spass::load(f, p, err)) std::printf("%s\n", err.c_str());
        doc_ = p, file_ = f, dirty_ = false, shown_ = true;
        spass::use(doc_), reset();
        sel_ = row;
        refresh();
        if (sel_ >= 0 && sel_ < int(rows_.size())) list_.selectOnly(sel_), showSelected();
        if (!pieces().empty()) activePiece_ = 0;
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
        if (dirty_ && shown_) {  // (2: its unsaved work stays, but the tools keep the setting's until a Save makes this the one in use)
            detached_ = true;
            return setMessage(L"Options file changed; unsaved edits kept (not in use until Save)");
        }
        loadSettingFile(), sel_ = -1;
        if (shown_) refresh();
    }
    // The setting's file into the window (its mappings as the file has them, on or off); none or unreadable: a new
    // document with no file (Save asks for one).
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
        // (Do not save: the passages of the file again)
        spass::Passages p;
        std::string err;
        if (!file_.empty() && spass::load(file_, p, err)) doc_ = p;
        else doc_ = {};
        useIfLive(), dirty_ = false, reset(), shellPaletteRebuilt();
        return true;
    }
    bool closeRequested() override { return confirmClose(); }

    // The map's tileset again (another tab, another map): the lists, the pictures.
    void refresh() {
        const AppState& app = shellState();
        ed_ = app.ed && app.ed->isOpen() ? app.ed.get() : nullptr;
        era_ = ed_ ? ed_->era() & 7 : -1;
        tilesFor_ = ed_ ? &ed_->tileset() : nullptr;
        groupsFor_ = ed_ ? ed_->tileset().groups.size() : 0;
        target_ = -1, activePiece_ = -1;  // (another row may be the selected one now: undo sets its target after this)
        modPics_.clear(), rowPics_.clear(), gamePics_.clear(), palScroll_ = 0, gridScroll_ = 0;
        terrainNames_.clear(), terrainIds_.clear();
        modSigs_.clear(), gameSigs_.clear(), rampRows_.clear(), rampRowsFor_ = size_t(-1), plainRows_.clear(), plainRowsFor_ = size_t(-1), suggest_.clear(), menuKey_.clear();  // (the tileset's)
        std::vector<stardraft::kit::DropdownItem> names{{L"(none)"}};
        if (ed_)
            for (uint16_t id = 1; id < 256; ++id)
                if (const std::string n = ed_->terrainName(id); !n.empty()) terrainNames_.push_back(n), terrainIds_.push_back(id), names.push_back({widen(n), -1, -1, draw::kNone, isomrules::terrainPicture(ed_->tileset(), id)});  // (no id: three lists in a row are narrow)
        for (auto& row : terr_)
            for (auto& d : row) d.setItems(names);
        rows_.clear();
        for (int i = 0; i < int(doc_.ramps.size()); ++i)
            if (doc_.ramps[size_t(i)].era == era_) rows_.push_back({false, i});
        for (int i = 0; i < int(doc_.bridges.size()); ++i)
            if (doc_.bridges[size_t(i)].era == era_) rows_.push_back({true, i});
        list_.setCount(int(rows_.size()));
        if (sel_ >= int(rows_.size())) sel_ = -1;
        if (sel_ < 0 && !rows_.empty()) sel_ = 0;
        if (sel_ >= 0) list_.selectOnly(sel_);
        showSelected();
        title();
    }
    // Each turn of the event loop: another tab or map in the main window.
    void follow() {
        const AppState& app = shellState();
        if (stale()) refresh();
    }
    // Whether the map (its editor, tileset or tile count) is not the one the window shows.
    bool stale() const {
        const AppState& app = shellState();
        const Editor* ed = app.ed && app.ed->isOpen() ? app.ed.get() : nullptr;
        return ed != ed_ || (ed && ((ed->era() & 7) != era_ || ed->tileset().groups.size() != groupsFor_));
    }

    void resized() override;
    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override {
        // (the text fields have the keys: their typing and their text keys; the Ctrl keys they do not take are the window's)
        if (!e.down || ((name_.hasFocus() || jump_.hasFocus()) && !(e.mods & draw::kCtrl))) return false;
        const bool ctrl = e.mods == draw::kCtrl;
        if (e.key == draw::Key::Delete && !e.mods) return clearTarget(), true;
        if (e.key == draw::Key::Escape && !e.mods && (target_ >= 0 || activePiece_ >= 0)) return target_ = -1, activePiece_ = -1, invalidate(), true;
        if ((e.key == draw::Key::Up || e.key == draw::Key::Down) && !e.mods) return stepTarget(e.key == draw::Key::Down ? 1 : -1), true;
        if (e.key == draw::Key('L') && ctrl) return nextPiece(), true;
        if (e.key == draw::Key('H') && ctrl) return overlays_ = !overlays_, invalidate(), true;
        if (e.key == draw::Key('F') && ctrl) return findPlaces(true), true;
        if (e.key == draw::Key('M') && e.mods == (draw::kCtrl | draw::kShift)) return mapAll(), true;
        if (e.key == draw::Key::F3 && suggesting() && !(e.mods & ~draw::kShift))
            return suggestAt_ = (suggestAt_ + ((e.mods & draw::kShift) ? int(suggest_.size()) - 1 : 1)) % int(suggest_.size()), showSuggestion(), true;
        if (e.key == draw::Key::Enter && !e.mods && suggesting())
            return placePiece(activePiece_, suggest_[size_t(suggestAt_)].row, suggest_[size_t(suggestAt_)].col), true;
        if (e.key == draw::Key('S') && (e.mods & draw::kCtrl)) return save((e.mods & draw::kShift) != 0), true;
        if (e.key == draw::Key('O') && ctrl) return loadFile(), true;
        if (e.key == draw::Key('N') && ctrl) return addMapping(false), true;
        if (e.key == draw::Key('Z') && (e.mods & draw::kCtrl)) return undo((e.mods & draw::kShift) != 0), true;
        if (e.key == draw::Key('Y') && ctrl) return undo(true), true;
        return false;
    }
    bool focusable() const override { return true; }

private:
    struct Row {
        bool bridge;
        int index;  // in doc_.ramps or doc_.bridges
    };
    // A tile picture of the open view: its tiles (MTXM values, 0xFFFF: none) in a w x h box.
    struct Picture {
        int w = 0, h = 0;
        std::vector<uint16_t> tiles;
    };

    // The selected row's mapping (none while the rows are not the document's: a delete, before refresh()).
    bool rowOk() const {
        if (sel_ < 0 || sel_ >= int(rows_.size())) return false;
        const Row& r = rows_[size_t(sel_)];
        return r.index >= 0 && size_t(r.index) < (r.bridge ? doc_.bridges.size() : doc_.ramps.size());
    }
    spass::RampMapping* ramp() { return rowOk() && !rows_[size_t(sel_)].bridge ? &doc_.ramps[size_t(rows_[size_t(sel_)].index)] : nullptr; }
    spass::BridgeMapping* bridge() { return rowOk() && rows_[size_t(sel_)].bridge ? &doc_.bridges[size_t(rows_[size_t(sel_)].index)] : nullptr; }
    spass::TileMap* tiles() {
        if (spass::RampMapping* r = ramp()) return &r->tiles;
        if (spass::BridgeMapping* b = bridge()) return &b->tiles;
        return nullptr;
    }
    std::string* nameOf() {
        if (spass::RampMapping* r = ramp()) return &r->name;
        if (spass::BridgeMapping* b = bridge()) return &b->name;
        return nullptr;
    }
    std::wstring rowLabel(int i) {
        if (i < 0 || i >= int(rows_.size())) return {};
        const Row& r = rows_[size_t(i)];
        const std::string& n = r.bridge ? doc_.bridges[size_t(r.index)].name : doc_.ramps[size_t(r.index)].name;
        const std::vector<uint16_t> t = templateTiles(r);
        const spass::TileMap& m = r.bridge ? doc_.bridges[size_t(r.index)].tiles : doc_.ramps[size_t(r.index)].tiles;
        int mapped = 0;
        for (const uint16_t v : t) mapped += m.count(v) != 0;
        return std::wstring(r.bridge ? L"Bridge: " : L"Ramp: ") + (n.empty() ? L"(no name)" : widen(n)) + L"  " + std::to_wstring(mapped) + L"/" +
               std::to_wstring(t.size());
    }

    // All the template's tiles (the ones to map).
    std::vector<uint16_t> templateTiles(const Row& r) const {
        std::set<uint16_t> v;
        if (r.bridge) {
            const spass::BridgeMapping& b = doc_.bridges[size_t(r.index)];
            if (b.templateEra >= 0)
                for (const uint16_t t : stardraft::bridgeKitTileValues(b.templateEra)) v.insert(t);
        } else {
            const spass::RampMapping& m = doc_.ramps[size_t(r.index)];
            for (const int k : passagemap::blockKits(m.era, m.block))
                for (const Editor::TileAt& c : stardraft::rampKitTiles(k, 1, 0, 0)) v.insert(c.value);
        }
        return std::vector<uint16_t>(v.begin(), v.end());
    }
    // The template's tiles of the open view (a ramp's facing: its ramp of 1 slope; a bridge: one of length and width 1),
    // as a picture, and the tiles of it in the grid (a bridge: all the template's).
    Picture templatePicture() const {
        Picture p;
        std::vector<Editor::TileAt> t;
        if (sel_ < 0) return p;
        const Row& r = rows_[size_t(sel_)];
        if (r.bridge) {
            const spass::BridgeMapping& b = doc_.bridges[size_t(r.index)];
            if (b.templateEra >= 0) t = stardraft::bridgeTiles(b.templateEra, view_.value() == 1, 1, 1, 0, 0, 0);
        } else {
            const spass::RampMapping& m = doc_.ramps[size_t(r.index)];
            for (const int k : passagemap::blockKits(m.era, m.block))
                if (stardraft::rampKitFacing(k) == view_.value()) t = stardraft::rampKitTiles(k, 1, 0, 0);
        }
        if (t.empty()) return p;
        int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
        for (const Editor::TileAt& c : t) x0 = std::min(x0, c.x), y0 = std::min(y0, c.y), x1 = std::max(x1, c.x), y1 = std::max(y1, c.y);
        p.w = x1 - x0 + 1, p.h = y1 - y0 + 1;
        p.tiles.assign(size_t(p.w) * p.h, 0xFFFF);
        for (const Editor::TileAt& c : t) p.tiles[size_t(c.y - y0) * p.w + (c.x - x0)] = c.value;
        return p;
    }
    // A piece: a block of template tiles that goes into the tileset's tiles as one (w x h; 0xFFFF: no tile there).
    struct Piece {
        std::wstring name;
        draw::Color color;
        int w = 0, h = 0;
        std::vector<uint16_t> tiles;
    };
    static draw::Color pieceColor(int i, bool isBridge) {
        static const draw::Color kRamp[3] = {RGB(236, 96, 80), RGB(96, 204, 112), RGB(92, 152, 244)};  // (wall A, slope, wall B)
        static const draw::Color kCycle[8] = {RGB(236, 96, 80),  RGB(96, 204, 112), RGB(92, 152, 244), RGB(240, 196, 64),
                                              RGB(200, 110, 230), RGB(64, 210, 210), RGB(244, 150, 60), RGB(170, 220, 90)};
        return isBridge ? kCycle[i % 8] : kRamp[i % 3];
    }
    // The pieces of the open view: a ramp's three in the shown facing; a bridge's runs of tile groups (all of its template).
    std::vector<Piece> pieces() const { return piecesFor(view_.value()); }
    std::vector<Piece> piecesFor(int facing) const {
        std::vector<Piece> out;
        if (!rowOk()) return out;
        const Row& r = rows_[size_t(sel_)];
        if (!r.bridge) {
            const spass::RampMapping& m = doc_.ramps[size_t(r.index)];
            static const wchar_t* kNames[3] = {L"Wall A", L"Slope", L"Wall B"};
            for (const int k : passagemap::blockKits(m.era, m.block)) {
                if (stardraft::rampKitFacing(k) != facing) continue;
                for (int kind = 0; kind < 3; ++kind) {
                    Piece q;
                    const auto [w, h] = stardraft::rampKitPieceSize(k, kind);
                    q.name = widen(stardraft::rampFacingName(facing)) + L" " + kNames[kind];
                    q.color = pieceColor(kind, false), q.w = w, q.h = h, q.tiles.assign(size_t(std::max(0, w)) * std::max(0, h), 0xFFFF);
                    for (const Editor::TileAt& c : stardraft::rampKitPieceTiles(k, kind))
                        if (c.x >= 0 && c.y >= 0 && c.x < w && c.y < h) q.tiles[size_t(c.y) * w + c.x] = c.value;
                    out.push_back(std::move(q));
                }
            }
            return out;
        }
        // (a bridge: the template's tiles by tile group; groups one after the other are a piece, as a doodad's rows)
        const std::vector<uint16_t> all = templateTiles(r);
        std::map<int, std::vector<uint16_t>> byGroup;
        for (const uint16_t v : all) byGroup[v >> 4].push_back(v);
        int prev = -2;
        for (const auto& [g, vs] : byGroup) {
            if (g != prev + 1 || out.empty() || out.back().h >= 8) {  // (a new run; at most 8 rows a piece)
                Piece q;
                q.color = pieceColor(int(out.size()), true);
                q.name = L"Groups " + std::to_wstring(g);
                out.push_back(std::move(q));
                runStart_ = g;
            }
            Piece& q = out.back();
            const int y = q.h++;
            q.w = std::max(q.w, 1);
            for (const uint16_t v : vs) q.w = std::max(q.w, int(v & 15) + 1);
            std::vector<uint16_t> grown(size_t(q.w) * q.h, 0xFFFF);
            const int oldW = q.h > 1 ? int(q.tiles.size()) / (q.h - 1) : 0;
            for (int yy = 0; yy + 1 < q.h; ++yy)
                for (int xx = 0; xx < oldW; ++xx) grown[size_t(yy) * q.w + xx] = q.tiles[size_t(yy) * oldW + xx];
            for (const uint16_t v : vs) grown[size_t(y) * q.w + (v & 15)] = v;
            q.tiles = std::move(grown);
            q.name = L"Groups " + std::to_wstring(runStart_) + (q.h > 1 ? L"\x2013" + std::to_wstring(g) : L"");
            prev = g;
        }
        return out;
    }
    // A piece's tiles that have a mod tile, and all of them.
    std::pair<int, int> pieceDone(const Piece& q) const {
        int n = 0, all = 0;
        const spass::TileMap* m = const_cast<PassagesView*>(this)->tiles();
        for (const uint16_t v : q.tiles)
            if (v != 0xFFFF) ++all, n += m && m->count(v);
        return {n, all};
    }
    // The piece of each template tile, by its color (the overlays).
    std::map<uint16_t, draw::Color> pieceColors(const std::vector<Piece>& ps) const {
        std::map<uint16_t, draw::Color> out;
        for (const Piece& q : ps)
            for (const uint16_t v : q.tiles)
                if (v != 0xFFFF) out.emplace(v, q.color);
        return out;
    }
    // A piece into the tileset's tiles with its top left at (row, col): each of its tiles takes the tile there.
    void placePiece(int i, int row, int col) {
        const std::vector<Piece> ps = pieces();
        spass::TileMap* m = tiles();
        if (!m || i < 0 || i >= int(ps.size())) return;
        const Piece& q = ps[size_t(i)];
        int n = 0;
        for (int y = 0; y < q.h; ++y)
            for (int x = 0; x < q.w; ++x)
                if (const uint16_t v = q.tiles[size_t(y) * q.w + x]; v != 0xFFFF && col + x < 16 && row + y < paletteRows())
                    (*m)[v] = uint16_t((row + y) << 4 | (col + x)), ++n;
        if (!n) return;
        edited(), list_.invalidate(), suggest_.clear();
        // (the next piece with tiles to map: in this view, then (a ramp) in the next facings)
        const int facings = ramp() ? 8 : 1;
        for (int f = 0; f < facings; ++f) {
            if (f) view_.setValue((view_.value() + 1) % 8);
            const std::vector<Piece> next = pieces();
            for (int k = 0; k < int(next.size()); ++k) {
                const int j = f ? k : (i + 1 + k) % int(next.size());
                if (const auto [done, all] = pieceDone(next[size_t(j)]); done < all) {
                    activePiece_ = j;
                    setMessage(q.name + L": placed (" + std::to_wstring(n) + L" tiles). Next: " + next[size_t(j)].name);
                    if (suggestOn_) findPlaces(false, q.name + L": placed. ");
                    return invalidate();
                }
            }
        }
        if (facings > 1) view_.setValue((view_.value() + 1) % 8);  // (back to the facing it was)
        activePiece_ = -1;
        setMessage(q.name + L": placed. Every piece has its tiles");
    }
    // The pictures' likeness (the suggestions): each tile as 8 x 8 cells of its colors (the mean of each cell), and its
    // mean color. A tile with no picture (the null tile, all black) is empty: no place uses it.
    static constexpr int kCells = 8, kCellPx = 32 / kCells, kSig = kCells * kCells * 3;
    struct Sig {
        std::array<float, kSig> v{};
        float mean[3] = {0, 0, 0};
        bool empty = true;
    };
    static Sig sigOf(const stardraft::Sprite& sp, int ox) {
        Sig g;
        if (sp.width < ox + 32 || sp.height < 32) return g;
        double lit = 0;
        const int px = kCellPx * kCellPx;
        for (int cy = 0; cy < kCells; ++cy)
            for (int cx = 0; cx < kCells; ++cx)
                for (int ch = 0; ch < 3; ++ch) {
                    double sum = 0;
                    for (int y = cy * kCellPx; y < (cy + 1) * kCellPx; ++y)
                        for (int x = ox + cx * kCellPx; x < ox + (cx + 1) * kCellPx; ++x) sum += sp.rgba[(size_t(y) * sp.width + x) * 4 + ch];
                    g.v[size_t((cy * kCells + cx) * 3 + ch)] = float(sum / px), g.mean[ch] += float(sum / px / (kCells * kCells)), lit += sum;
                }
        g.empty = lit < 32.0 * 32 * 3 * 4;  // (all but black)
        return g;
    }
    // How unlike two tiles look: their colors, and (more) their shapes (each without its mean color: a recolor is alike).
    static float unlike(const Sig& a, const Sig& b) {
        float color = 0, shape = 0;
        for (size_t i = 0; i < size_t(kSig); ++i) {
            const float da = a.v[i] - a.mean[i % 3], db = b.v[i] - b.mean[i % 3];
            color += std::fabs(a.v[i] - b.v[i]), shape += std::fabs(da - db);
        }
        return (0.4f * color + shape) / kSig;
    }
    const std::vector<Sig>& modSigs() {
        if (modSigsFor_ == groupsFor_ && !modSigs_.empty()) return modSigs_;
        modSigs_.assign(size_t(paletteRows()) * 16, Sig{}), modSigsFor_ = groupsFor_;
        if (!tilesFor_) return modSigs_;
        shellWaitCursor();
        for (int g = 0; g < paletteRows(); ++g) {
            std::vector<uint16_t> t;
            for (int k = 0; k < 16; ++k) t.push_back(uint16_t(g << 4 | k));
            const stardraft::Sprite sp = stardraft::mapTilesPicture(*tilesFor_, t, 16, 0, 0, 16, 1);
            for (int k = 0; k < 16; ++k)
                if (tilesFor_->megatileFor(uint16_t(g << 4 | k)) > 0) modSigs_[size_t(g) * 16 + k] = sigOf(sp, k * 32);
        }
        return modSigs_;
    }
    const Sig& gameSig(int era, uint16_t v) {
        auto& g = gameSigs_[uint32_t(era) << 16 | v];
        if (g.empty)
            if (const stardraft::Tileset* ts = gameTileset(era)) g = sigOf(stardraft::mapTilesPicture(*ts, {v}, 1, 0, 0, 1, 1), 0);
        return g;
    }
    // The best places for a piece in the tileset's tiles (its top left), most alike first, none overlapping another: the
    // sum of its tiles' unlikeness; a tile that another template tile has already costs more (two pieces seldom share).
    struct Place {
        int row, col;
        float cost;
    };
    // The tile groups of the tileset's ramp doodads (where a mod's ramp art is): the rows a ramp's places are searched in
    // first (View > Search all tile groups: every row).
    const std::vector<uint8_t>& rampRows() {
        if (rampRowsFor_ == groupsFor_) return rampRows_;
        rampRowsFor_ = groupsFor_;
        rampRows_.assign(size_t(paletteRows()), 0);
        if (ed_)
            for (const uint16_t id : stardraft::rampDoodads(*ed_))
                if (const stardraft::DoodadType* t = ed_->doodadTypes().find(id))
                    for (int r = 0; r < t->height; ++r)
                        if (t->firstGroup + r < paletteRows()) rampRows_[size_t(t->firstGroup + r)] = 1;
        return rampRows_;
    }
    // The tile groups of the isometric terrains (solid ground, cliffs, their transitions): never a ramp's or a bridge's
    // art. A piece mapped there puts plain ground into the kit, and the Ramp brush then finds the whole map a slope.
    const std::vector<uint8_t>& plainRows() {
        if (plainRowsFor_ == groupsFor_) return plainRows_;
        plainRowsFor_ = groupsFor_;
        plainRows_.assign(size_t(paletteRows()), 0);
        if (ed_) {
            const std::vector<std::array<uint16_t, 2>> t = stardraft::tileGroupTerrains(ed_->tileset());
            for (size_t g = 0; g < t.size() && g < plainRows_.size(); ++g) plainRows_[g] = t[g][0] != 0 || t[g][1] != 0;
        }
        return plainRows_;
    }
    bool hasRampRows() {
        const std::vector<uint8_t>& r = rampRows();
        return std::find(r.begin(), r.end(), uint8_t(1)) != r.end();
    }
    std::vector<Place> placesFor(const Piece& q, int count) {
        // (the ramp doodads' rows first; none there: all of them)
        if (ramp() && !searchAll_ && hasRampRows()) {
            std::vector<Place> in = placesIn(q, count, true);
            if (!in.empty()) return in;
        }
        return placesIn(q, count, false);
    }
    std::vector<Place> placesIn(const Piece& q, int count, bool onlyRamps) {
        std::vector<Place> out;
        const std::vector<uint8_t>& rows = rampRows();
        const std::vector<uint8_t>& plain = plainRows();
        const std::vector<Sig>& mods = modSigs();
        const int era = templateEra();
        const spass::TileMap* m = tiles();
        std::set<uint16_t> used;  // (the mod tiles the other template tiles have)
        if (m)
            for (const auto& [v, to] : *m)
                if (std::find(q.tiles.begin(), q.tiles.end(), v) == q.tiles.end()) used.insert(to);
        struct Cell {
            int x, y;
            const Sig* sig;
        };
        std::vector<Cell> cells;
        for (int y = 0; y < q.h; ++y)
            for (int x = 0; x < q.w; ++x)
                if (const uint16_t v = q.tiles[size_t(y) * q.w + x]; v != 0xFFFF)
                    if (const Sig& g = gameSig(era, v); !g.empty) cells.push_back({x, y, &g});
        if (cells.empty() || q.w > 16) return out;
        std::vector<Place> all;
        // (the best places so far, kept count * 8 of them for the overlap filter: a place's sum stops at their worst)
        const size_t keep = size_t(std::max(1, count)) * 8;
        auto worse = [](const Place& a, const Place& b) { return a.cost < b.cost; };
        std::vector<Place> heap;  // (a max-heap by cost)
        const float scaleBy = float(cells.size());
        for (int row = 0; row + q.h <= paletteRows(); ++row) {
            bool in = true;
            for (int y = 0; y < q.h && in; ++y) in = (!onlyRamps || rows[size_t(row + y)] != 0) && (searchAll_ || !plain[size_t(row + y)]);
            if (!in) continue;
            for (int col = 0; col + q.w <= 16; ++col) {
                float cost = 0;
                bool ok = true;
                const float bound = heap.size() >= keep ? heap.front().cost * scaleBy : 1e30f;
                for (const Cell& c : cells) {
                    const size_t at = size_t(row + c.y) * 16 + (col + c.x);
                    if (mods[at].empty) { ok = false; break; }
                    cost += unlike(*c.sig, mods[at]) + (used.count(uint16_t(at)) ? 40.f : 0.f);
                    if (cost >= bound) { ok = false; break; }
                }
                if (!ok) continue;
                heap.push_back({row, col, cost / scaleBy}), std::push_heap(heap.begin(), heap.end(), worse);
                if (heap.size() > keep) std::pop_heap(heap.begin(), heap.end(), worse), heap.pop_back();
            }
        }
        all = std::move(heap);
        std::sort(all.begin(), all.end(), [](const Place& a, const Place& b) { return a.cost < b.cost; });
        for (const Place& pl : all) {
            bool clash = false;
            for (const Place& o : out) clash = clash || (pl.row < o.row + q.h && o.row < pl.row + q.h && pl.col < o.col + q.w && o.col < pl.col + q.w);
            if (!clash) out.push_back(pl);
            if (int(out.size()) >= count) break;
        }
        return out;
    }
    // The places found are still the picked piece's (the same piece, view and mapping): else there are none.
    bool suggesting() const {
        return !suggest_.empty() && suggestPiece_ == activePiece_ && suggestView_ == view_.value() && suggestSel_ == sel_ && suggestTiles_ == groupsFor_;
    }
    // Ctrl+F (and after each placing): the picked piece's best places, framed in the tileset's tiles; the best shown.
    void findPlaces(bool asked, const std::wstring& before = L"") {
        const std::vector<Piece> ps = pieces();
        if (activePiece_ < 0 || activePiece_ >= int(ps.size())) {
            if (!asked) return;
            nextPiece();
            if (activePiece_ < 0) return;
            return findPlaces(true);
        }
        suggest_ = placesFor(ps[size_t(activePiece_)], 5), suggestAt_ = 0;
        suggestPiece_ = activePiece_, suggestView_ = view_.value(), suggestSel_ = sel_, suggestTiles_ = groupsFor_;
        if (suggest_.empty()) return setMessage(before + L"No match for " + ps[size_t(activePiece_)].name);
        suggestFor_ = before + ps[size_t(activePiece_)].name;
        showSuggestion();
    }
    void showSuggestion() {
        if (suggest_.empty()) return;
        const Place& pl = suggest_[size_t(suggestAt_)];
        if (pl.row * palRowH() < palScroll_ || (pl.row + 3) * palRowH() > palScroll_ + pal_.height()) scrollPaletteTo(std::max(0, pl.row - 3));
        setMessage(suggestFor_ + L": place " + std::to_wstring(suggestAt_ + 1) + L"/" + std::to_wstring(suggest_.size()) + L", group " + std::to_wstring(pl.row) +
                   L"  \x00b7  Enter place, F3 next");
        invalidate();
    }
    // Mapping > Map all pieces: each piece with tiles to map (a ramp: in all its facings) at its best place, one undo step.
    void mapAll() {
        spass::TileMap* m = tiles();
        if (!m) return;
        const int facings = ramp() ? 8 : 1, was = view_.value();
        int placed = 0, none = 0;
        for (int f = 0; f < facings; ++f) {
            if (facings > 1) view_.setValue(f);
            const std::vector<Piece> ps = pieces();
            for (const Piece& q : ps) {
                if (const auto [done, all] = pieceDone(q); done == all) continue;
                const std::vector<Place> best = placesFor(q, 1);
                if (best.empty()) {
                    ++none;
                    continue;
                }
                for (int y = 0; y < q.h; ++y)
                    for (int x = 0; x < q.w; ++x)
                        if (const uint16_t v = q.tiles[size_t(y) * q.w + x]; v != 0xFFFF) (*m)[v] = uint16_t((best[0].row + y) << 4 | (best[0].col + x));
                ++placed;
            }
        }
        view_.setValue(was);
        suggest_.clear(), activePiece_ = -1;
        if (placed) edited(), list_.invalidate();
        setMessage(placed ? L"Mapped " + std::to_wstring(placed) + L" pieces" + (none ? L", " + std::to_wstring(none) + L" no match" : L"") +
                                L" (Ctrl+Z undo)"
                          : L"Every piece has its tiles");
    }
    // The Facing list: each facing with its pieces done (a check mark: all).
    std::vector<stardraft::kit::DropdownItem> facingItems() {
        std::vector<stardraft::kit::DropdownItem> items;
        for (int f = 0; f < 8; ++f) {
            int done = 0, all = 0;
            for (const Piece& q : piecesFor(f)) {
                const auto [d, a] = pieceDone(q);
                done += d == a, ++all;
            }
            items.push_back({widen(stardraft::rampFacingName(f)) + (all && done == all ? L"   \x2713" : L"   " + std::to_wstring(done) + L"/" + std::to_wstring(all))});
        }
        return items;
    }

    // The pieces' cards in the pieces' box: each a header, the template's tiles over the mod's (small tiles).
    std::vector<draw::Rect> pieceCards(const std::vector<Piece>& ps) const {
        std::vector<draw::Rect> out;
        const int t = miniTile(), pad = scale(6), head = scale(18);
        int x = grid_.left + pad, y = grid_.top + pad - gridScroll_, rowH = 0;
        for (const Piece& q : ps) {
            const int w = std::max(scale(110), q.w * t) + 2 * pad, h = head + 2 * q.h * t + 3 * pad;
            if (x + w > grid_.right - pad && x > grid_.left + pad) x = grid_.left + pad, y += rowH + pad, rowH = 0;
            out.push_back({x, y, x + w, y + h});
            x += w + pad, rowH = std::max(rowH, h);
        }
        return out;
    }
    int miniTile() const { return scale(16); }

    std::vector<uint16_t> gridTiles() const {
        if (sel_ < 0) return {};
        if (rows_[size_t(sel_)].bridge) return templateTiles(rows_[size_t(sel_)]);
        std::set<uint16_t> v;
        for (const uint16_t t : templatePicture().tiles)
            if (t != 0xFFFF) v.insert(t);
        return std::vector<uint16_t>(v.begin(), v.end());
    }
    int templateEra() {
        if (spass::BridgeMapping* b = bridge()) return b->templateEra;
        if (spass::RampMapping* r = ramp()) return r->era;
        return -1;
    }

    // The selected mapping into the fields.
    void selected() {
        sel_ = list_.cursor() >= 0 && list_.cursor() < int(rows_.size()) ? list_.cursor() : -1;
        target_ = -1, activePiece_ = -1, gridScroll_ = 0;
        showSelected();
    }
    void showSelected() {
        const bool any = sel_ >= 0;
        name_.setText(any && nameOf() ? widen(*nameOf()) : L"");
        std::vector<stardraft::kit::DropdownItem> items;
        templates_.clear();
        int value = -1;
        if (spass::RampMapping* r = ramp()) {
            // (the tileset's ramp blocks: the first doodad of each, with its terrains)
            std::set<int> seen;
            for (const int k : stardraft::rampKitsOf(era_, true)) {
                if (stardraft::rampKitFromPassage(k) || !seen.insert(stardraft::rampKitBase(k)).second) continue;
                const auto [low, high] = stardraft::rampKitTerrains(k);
                templates_.push_back(stardraft::rampKitBase(k));
                items.push_back({widen(std::string(stardraft::rampKitGroup(k)) + ": " + high + " on " + (low.empty() ? "any" : low)), -1, stardraft::rampKitBase(k)});
                if (r->block == stardraft::rampKitBase(k)) value = int(templates_.size()) - 1;
            }
            view_.setItems(facingItems());
        } else if (spass::BridgeMapping* b = bridge()) {
            for (int e = 0; e < 8; ++e)
                if (stardraft::bridgesAvailable(e)) {
                    templates_.push_back(e);
                    items.push_back({isomrules::tilesetTitle(e) + L" bridge"});
                    if (b->templateEra == e) value = int(templates_.size()) - 1;
                }
            view_.setItems({{L"Down-right deck (d)"}, {L"Up-right deck (u)"}});
        } else {
            view_.setItems({});
        }
        template_.setItems(items), template_.setValue(value);
        shape_ = shapeText();
        if (any && view_.value() < 0) view_.setValue(0);
        if (ramp() && view_.value() > 7) view_.setValue(0);
        if (bridge() && view_.value() > 1) view_.setValue(0);
        template_.setEnabled(any), view_.setEnabled(any);
        showTerrains();
        resized();
        invalidate();
    }
    // The terrain rows: Ground, Cliff (a ramp) or Coast A, Water, Coast B (a bridge), each a name in the map's list.
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
        if (spass::RampMapping* r = ramp(); r && row == 0)  // (the first row's terrains pick the shape)
            if (const int b = blockFor(r->terrains[0]); b >= 0 && b != r->block) r->block = b, target_ = -1, activePiece_ = -1, gridScroll_ = 0, suggest_.clear();
        if (std::string* n = nameOf(); n && row == 0 && col == 1 && !t->empty()) {  // (a name not typed yet: the terrain's)
            const std::string def = ramp() ? "Ramp " : "Bridge ";
            bool numbered = n->rfind(def, 0) == 0 && n->size() > def.size();
            for (size_t k = def.size(); numbered && k < n->size(); ++k) numbered = (*n)[k] >= '0' && (*n)[k] <= '9';
            const std::string made = ramp() ? "Ramp: " : "Bridge: ";  // (a name the mapper made from a terrain)
            if (n->empty() || numbered || n->rfind(made, 0) == 0) *n = made + *t, name_.setText(widen(*n));
        }
        edited();
        if (ramp() && row == 0 && col == 1 && !t->empty()) {  // (the next step: the first piece, and where it may be)
            nextPiece();
            if (suggestOn_) findPlaces(false);
        }
        shape_ = shapeText();
        list_.invalidate();
    }
    // The game terrain a terrain of the map stands in for (a mod terrain: its stand-in; a game terrain: itself), by its
    // name without the ground a mod's list adds ("High Dirt (Dirt)": "High Dirt").
    std::string gameName(const std::string& name) const {
        uint16_t id = 0;
        for (size_t i = 0; i < terrainNames_.size() && !id; ++i)
            if (terrainNames_[i] == name) id = terrainIds_[i];
        if (!id || !ed_) return name.substr(0, name.find(" ("));
        if (const stardraft::Tileset::ModTerrain* m = ed_->tileset().modTerrain(id); m && m->standIn) id = m->standIn;
        const std::string n = ed_->terrainName(id);
        return n.substr(0, n.find(" ("));
    }
    // The ramp block for a Ground and Cliff: the game's block whose high ground is the cliff's stand-in (and, of those,
    // whose ground below is the ground's); -1: none.
    int blockFor(const spass::RampTerrains& t) const {
        if (t.cliff.empty()) return -1;
        const std::string high = gameName(t.cliff), low = t.ground.empty() ? std::string() : gameName(t.ground);
        int best = -1;
        for (const int k : stardraft::rampKitsOf(era_, true)) {
            if (stardraft::rampKitFromPassage(k)) continue;
            const auto [kl, kh] = stardraft::rampKitTerrains(k);
            if (kh != high) continue;
            if (best < 0) best = stardraft::rampKitBase(k);
            if (!low.empty() && kl == low) return stardraft::rampKitBase(k);
        }
        return best;
    }
    // The line where the Template list was: the shape the terrains give.
    std::wstring shapeText() {
        if (spass::RampMapping* r = ramp()) {
            if (r->terrains.empty() || r->terrains[0].cliff.empty()) return L"No cliff set";
            if (blockFor(r->terrains[0]) < 0) return L"No game ramp for the stand-in of " + widen(r->terrains[0].cliff);
            for (const int k : passagemap::blockKits(r->era, r->block)) {
                const auto [low, high] = stardraft::rampKitTerrains(k);
                // (a mod cliff: the game cliff it stands in for, after an arrow)
                const std::string& cliff = r->terrains[0].cliff;
                const std::wstring from = gameName(cliff) == cliff.substr(0, cliff.find(" (")) ? std::wstring() : widen(cliff) + L" \x2192 ";
                return from + widen(std::string(stardraft::rampKitGroup(k)) + ": " + high + " on " + (low.empty() ? "any" : low));
            }
        }
        if (spass::BridgeMapping* b = bridge(); b && b->templateEra >= 0) return isomrules::tilesetTitle(b->templateEra) + L" bridge";
        return {};
    }
    // The Template list shows only for a bridge on a tileset with no bridge kit of its own.
    bool templatePick() { return bridge() && !stardraft::bridgesAvailable(era_); }
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
    void pickTemplate(int i) {
        if (i < 0 || i >= int(templates_.size())) return;
        if (spass::RampMapping* r = ramp()) r->block = templates_[size_t(i)];
        else if (spass::BridgeMapping* b = bridge()) b->templateEra = templates_[size_t(i)];
        target_ = -1, activePiece_ = -1, gamePics_.clear(), edited(), list_.invalidate();
    }

    void addMapping(bool isBridge) {
        if (era_ < 0) return setMessage(L"Open a map of the tileset first");
        if (isBridge) {
            spass::BridgeMapping b;
            b.name = "Bridge " + std::to_string(doc_.bridges.size() + 1), b.era = era_;
            b.templateEra = stardraft::bridgesAvailable(era_) ? era_ : -1;
            for (int e = 0; e < 8 && b.templateEra < 0; ++e)
                if (stardraft::bridgesAvailable(e)) b.templateEra = e;
            b.terrains.push_back({});
            doc_.bridges.push_back(std::move(b));
        } else {
            spass::RampMapping r;
            r.name = "Ramp " + std::to_string(doc_.ramps.size() + 1), r.era = era_;
            for (const int k : stardraft::rampKitsOf(era_, true))
                if (!stardraft::rampKitFromPassage(k)) {
                    r.block = stardraft::rampKitBase(k);
                    break;
                }
            r.terrains.push_back({});
            doc_.ramps.push_back(std::move(r));
        }
        edited();
        refresh();
        for (int i = 0; i < int(rows_.size()); ++i)
            if (rows_[size_t(i)].bridge == isBridge && rows_[size_t(i)].index == int((isBridge ? doc_.bridges.size() : doc_.ramps.size()) - 1)) sel_ = i;
        list_.selectOnly(sel_);
        showSelected();
        setMessage(isBridge ? L"Set water" : L"Set cliff");
        terr_[0][1].openList();  // (the Cliff or the Water: the first thing to pick)
    }
    void removeMapping() {
        if (sel_ < 0) return;
        const Row r = rows_[size_t(sel_)];
        if (r.bridge) doc_.bridges.erase(doc_.bridges.begin() + r.index);
        else doc_.ramps.erase(doc_.ramps.begin() + r.index);
        sel_ = std::min(sel_, int(rows_.size()) - 2);
        refresh();  // (the rows of the document as it is now, before the edit's work reads them)
        edited();
    }

    // The target's mod tile: the clicked tile (one), or its row at the same offset for the template's tiles of the
    // target's row (a row of a doodad is one tile group).
    void assign(uint16_t to, bool one) {
        spass::TileMap* m = tiles();
        if (!m || target_ < 0) return;
        const uint16_t from = uint16_t(target_);
        if (one) {
            (*m)[from] = to;
        } else {
            const int shift = int(to & 15) - int(from & 15);
            for (const uint16_t v : templateTiles(rows_[size_t(sel_)]))
                if (passagemap::groupOf(v) == passagemap::groupOf(from))
                    if (const int s = int(v & 15) + shift; s >= 0 && s < 16) (*m)[v] = uint16_t(passagemap::groupOf(to) << 4 | s);
        }
        edited(), list_.invalidate();
        // (the next template tile with no mod tile, in the grid's order)
        const std::vector<uint16_t> g = gridTiles();
        const auto at = std::find(g.begin(), g.end(), from);
        for (auto i = at; i != g.end(); ++i)
            if (!m->count(*i)) return void(target_ = *i);
        for (auto i = g.begin(); i != at; ++i)
            if (!m->count(*i)) return void(target_ = *i);
    }
    void clearTarget() {
        if (spass::TileMap* m = tiles(); m && target_ >= 0 && m->erase(uint16_t(target_))) edited(), list_.invalidate();
    }
    // Each template tile with no mod tile: the template's own (a mod's tileset that has the game's tiles there).
    void fillWithTemplate() {
        spass::TileMap* m = tiles();
        if (!m) return;
        int n = 0;
        for (const uint16_t v : templateTiles(rows_[size_t(sel_)]))
            if (m->emplace(v, v).second) ++n;
        if (n) edited(), list_.invalidate();
        setMessage(std::to_wstring(n) + L" template tiles mapped to themselves");
    }

    // An edit of doc_ (made): an undo step with the passages before it (merge: a name typed on, one step for the typing).
    void edited(bool merge = false) {
        if (!(merge && merging_)) {
            undo_.push_back({before_, sel_, target_}), redo_.clear();
            if (undo_.size() > 200) undo_.erase(undo_.begin());
        }
        merging_ = merge, before_ = doc_;
        dirty_ = true;
        checked_.erase(checkKey());  // (7: an edit: the result is to check again)
        useIfLive();
        toolsFollow();
        if (ramp()) {
            const int v = view_.value();
            view_.setItems(facingItems()), view_.setValue(v);
        }
        title();
        invalidate();
    }
    // The tool strip: the Bridge and the Ramp where the open map's tileset has them now (a mapping made whole, or deleted).
    void toolsFollow() {
        const AppState& app = shellState();
        if (!app.ed || !app.ed->isOpen()) return;
        const bool had[2] = {stardraft::bridgesAvailable(*app.ed), stardraft::rampsAvailable(*app.ed)};
        if (had[0] != toolsHad_[0] || had[1] != toolsHad_[1]) toolsHad_[0] = had[0], toolsHad_[1] = had[1], shellPaletteRebuilt();
    }
    // A new document (opened, loaded): no undo steps.
    void reset() { undo_.clear(), redo_.clear(), before_ = doc_, merging_ = false; }
    void undo(bool redo) {
        auto& from = redo ? redo_ : undo_;
        auto& to = redo ? undo_ : redo_;
        if (from.empty()) return;
        to.push_back({doc_, sel_, target_});
        const Step st = std::move(from.back());
        from.pop_back();
        doc_ = st.doc, before_ = doc_, merging_ = false, dirty_ = true;
        useIfLive();
        toolsFollow();
        sel_ = st.sel;
        refresh();
        target_ = st.target;
        invalidate();
    }
    // The guide: the steps of the selected mapping, the step it is at (steps.size(): done), what to do and its buttons,
    // and the part of the window it is about.
    struct Guide {
        std::vector<std::wstring> steps;
        int at = 0;
        std::wstring text, a, b;
        std::function<void()> runA, runB;
        draw::Rect target;
    };
    std::wstring checkKey() {
        if (const std::string* n = nameOf()) return (ramp() ? L"r" : L"b") + std::to_wstring(rows_[size_t(sel_)].index) + L":" + widen(*n);
        return {};
    }
    Guide guide() {
        Guide g;
        int done = 0, all = 0;  // (the template tiles mapped: a ramp's in all its facings)
        for (int f = 0; f < (ramp() ? 8 : 1); ++f)
            for (const Piece& q : ramp() ? piecesFor(f) : pieces()) {
                const auto [d, a] = pieceDone(q);
                done += d, all += a;
            }
        const bool checked = checked_.count(checkKey()) != 0;
        auto pick = [this](int c) { return [this, c] { terr_[0][c].openList(); }; };
        if (spass::RampMapping* r = ramp()) {
            g.steps = {L"Cliff", L"Pieces", L"Check", L"Save"};
            const spass::RampTerrains t = r->terrains.empty() ? spass::RampTerrains{} : r->terrains[0];
            if (t.cliff.empty()) {
                g.at = 0, g.a = L"Cliff\x2026", g.runA = pick(1), g.target = terr_[0][1].frame();
            } else if (t.ground.empty()) {
                g.at = 0, g.a = L"Ground\x2026", g.runA = pick(0), g.target = terr_[0][0].frame();
            } else if (done < all) {
                g.at = 1, g.text = std::to_wstring(done) + L"/" + std::to_wstring(all) + L" tiles";
                g.a = L"Map all", g.runA = [this] { mapAll(); }, g.target = grid_;
            } else if (!checked) {
                g.at = 2, g.text = widen(stardraft::rampFacingName(view_.value())) + L" vs template";
                g.a = L"Next facing", g.runA = [this] { view_.onChange((view_.value() + 1) % 8); };
                g.b = L"OK", g.runB = [this] { checked_.insert(checkKey()), invalidate(); };
                g.target = resultPic_;
            } else if (dirty_) {
                g.at = 3, g.a = L"Save", g.runA = [this] { save(false); };
            } else {
                g.at = 4, g.text = L"Ramp brush ready";
                g.a = L"New ramp", g.runA = [this] { addMapping(false); };
            }
        } else if (spass::BridgeMapping* b = bridge()) {
            g.steps = {L"Water", L"Coasts", L"Tiles", L"Check", L"Save"};
            const spass::BridgeTerrains t = b->terrains.empty() ? spass::BridgeTerrains{} : b->terrains[0];
            if (t.water.empty()) {
                g.at = 0, g.a = L"Water\x2026", g.runA = pick(1), g.target = terr_[0][1].frame();
            } else if (t.coastA.empty()) {
                g.at = 1, g.a = L"Coast A\x2026", g.runA = pick(0), g.b = L"Coast B\x2026", g.runB = pick(2), g.target = terr_[0][0].frame();
            } else if (done < all) {
                g.at = 2, g.text = std::to_wstring(done) + L"/" + std::to_wstring(all) + L" tiles";
                g.a = L"Use vanilla", g.runA = [this] { fillWithTemplate(); };
                g.b = L"Map all", g.runB = [this] { mapAll(); };
                g.target = grid_;
            } else if (!checked) {
                g.at = 3, g.text = L"vs template";
                g.a = L"Other deck", g.runA = [this] { view_.onChange(1 - std::clamp(view_.value(), 0, 1)); };
                g.b = L"OK", g.runB = [this] { checked_.insert(checkKey()), invalidate(); };
                g.target = resultPic_;
            } else if (dirty_) {
                g.at = 4, g.a = L"Save", g.runA = [this] { save(false); };
            } else {
                g.at = 5, g.text = L"Bridge tool ready";
                g.a = L"New bridge", g.runA = [this] { addMapping(true); };
            }
        }
        return g;
    }
    // The guide's buttons as its step has them (only when they change: a button's text repaints it).
    void syncGuide(const Guide& g) {
        const std::wstring key = g.a + L"|" + g.b + L"|" + std::to_wstring(g.at);
        guideRunA_ = g.runA, guideRunB_ = g.runB;
        if (key == guideKey_) return;
        guideKey_ = key;
        guideA_.setText(g.a), guideB_.setText(g.b);
        if (g.b.empty() && guideB_.hasFocus()) guideA_.focus();  // (a hidden button keeps no keys)
        guideB_.setVisible(guideShown_ && !g.b.empty());
    }

    // The window's own message, in its status bar (hides the program's setMessage here).
    void setMessage(const std::wstring& m) { status_ = m, invalidate(); }
    // Ctrl+L: the next piece with tiles to map (this view's after the picked one, then (a ramp) the next facings').
    void nextPiece() {
        const int facings = ramp() ? 8 : 1, from = view_.value();
        for (int f = 0; f < facings; ++f) {
            const int view = (from + f) % std::max(1, facings);
            const std::vector<Piece> ps = facings > 1 ? piecesFor(view) : pieces();
            for (int k = 0; k < int(ps.size()); ++k) {
                const int j = f ? k : (activePiece_ + 1 + k) % int(ps.size());
                if (const auto [done, all] = pieceDone(ps[size_t(j)]); done < all) {
                    if (facings > 1) view_.setValue(view);
                    activePiece_ = j, target_ = -1;
                    setMessage(ps[size_t(j)].name + L" \x2192 tile list");
                    return followTarget();
                }
            }
        }
        setMessage(L"Every piece has its tiles");
    }
    // The menu bar's menus, made again when an item's state changes (menuKey()), as the isom terrain editor's.
    std::string menuKey() {
        return {char('0' + (sel_ >= 0)), char('0' + (sel_ >= 0 && !rows_[size_t(sel_)].bridge)), char('0' + !undo_.empty()), char('0' + !redo_.empty()),
                char('0' + (target_ >= 0)), char('0' + overlays_), char('0' + others_), char('0' + view_.value() + 1), char('0' + (era_ >= 0)),
                char('0' + (terrainRows() > 1)), char('0' + (terrainRows() < passagemap::kMaxTerrainRows)), char('0' + suggesting()),
                char('0' + (activePiece_ >= 0)), char('0' + suggestOn_), char('0' + int(groupsFor_ % 10)), char('0' + searchAll_), char('0' + guideOn_)};
    }
    void updateMenus() {
        if (menuKey_ == menuKey() || bar_.openIndex() >= 0) return;
        menuKey_ = menuKey();
        using Item = stardraft::kit::MenuItem;
        auto item = [this](std::wstring label, std::wstring keys, std::function<void()> run, bool enabled = true) {
            Item it;
            it.label = std::move(label), it.shortcut = std::move(keys), it.enabled = enabled;
            it.run = [this, run = std::move(run)] { run(), focus(); };  // (the keys go back to the window, not the menu bar)
            return it;
        };
        auto check = [&](std::wstring label, std::wstring keys, bool on, std::function<void()> run, bool enabled = true) {
            Item it = item(std::move(label), std::move(keys), std::move(run), enabled);
            it.checked = on;
            return it;
        };
        auto radio = [&](std::wstring label, bool on, std::function<void()> run, bool enabled = true) {
            Item it = item(std::move(label), L"", [on, run = std::move(run)] { if (!on) run(); }, enabled);
            it.checked = on, it.radio = true;
            return it;
        };
        Item sep;
        sep.separator = true;
        const bool any = sel_ >= 0, map = era_ >= 0, isRamp = any && !rows_[size_t(sel_)].bridge;
        std::vector<Item> file = {item(L"New ramp", L"Ctrl+N", [this] { addMapping(false); }, map), item(L"New bridge", L"", [this] { addMapping(true); }, map), sep,
                                  item(L"Open\x2026", L"Ctrl+O", [this] { loadFile(); }), sep,
                                  item(L"Save", L"Ctrl+S", [this] { save(false); }), item(L"Save as\x2026", L"Ctrl+Shift+S", [this] { save(true); })};
        std::vector<Item> edit = {item(L"Undo", L"Ctrl+Z", [this] { undo(false); }, !undo_.empty()),
                                  item(L"Redo", L"Ctrl+Y", [this] { undo(true); }, !redo_.empty()), sep,
                                  item(L"Clear the target's tile", L"Del", [this] { clearTarget(); }, any && target_ >= 0),
                                  item(L"Template tiles for the rest", L"", [this] { fillWithTemplate(); }, any), sep,
                                  item(L"Add terrains it fits on", L"", [this] { addTerrainRow(); }, any && terrainRows() < passagemap::kMaxTerrainRows),
                                  item(L"Remove the last terrains", L"", [this] { removeTerrainRow(); }, any && terrainRows() > 1), sep,
                                  item(L"Delete mapping", L"", [this] { removeMapping(); }, any)};
        std::vector<Item> mapping = {item(L"Find the piece's tiles", L"Ctrl+F", [this] { findPlaces(true); }, any),
                                     item(L"Next place", L"F3", [this] { if (suggesting()) suggestAt_ = (suggestAt_ + 1) % int(suggest_.size()), showSuggestion(); }, suggesting()),
                                     item(L"Put the piece there", L"Enter",
                                          [this] {
                                              if (suggesting())
                                                  placePiece(activePiece_, suggest_[size_t(suggestAt_)].row, suggest_[size_t(suggestAt_)].col);
                                          },
                                          suggesting()),
                                     item(L"Map all pieces", L"Ctrl+Shift+M", [this] { mapAll(); }, any), sep,
                                     item(L"Next piece to map", L"Ctrl+L", [this] { nextPiece(); }, any),
                                     item(L"Next template tile", L"Down", [this] { stepTarget(1); }, any),
                                     item(L"Previous template tile", L"Up", [this] { stepTarget(-1); }, any),
                                     item(L"No target", L"Esc", [this] { target_ = -1, activePiece_ = -1, invalidate(); }, any), sep};
        {  // (the facing or orientation shown: a radio group)
            Item shows;
            shows.label = isRamp ? L"Facing" : L"Deck";
            shows.enabled = any;
            const int n = isRamp ? 8 : 2;
            for (int i = 0; any && i < n; ++i)
                shows.sub.push_back(radio(isRamp ? widen(stardraft::rampFacingName(i)) : i ? std::wstring(L"Up-right deck (u)") : std::wstring(L"Down-right deck (d)"),
                                          view_.value() == i, [this, i] { view_.onChange(i); }));
            mapping.push_back(shows);
        }
        std::vector<Item> viewMenu = {check(L"Guide", L"", guideOn_, [this] { guideOn_ = !guideOn_, resized(), invalidate(); }), sep,
                                      check(L"Pieces' colors", L"Ctrl+H", overlays_, [this] { overlays_ = !overlays_, invalidate(); }),
                                      check(L"Other facings' tiles", L"", others_, [this] { others_ = !others_, invalidate(); }, isRamp), sep,
                                      check(L"Find tiles after each piece", L"", suggestOn_, [this] { suggestOn_ = !suggestOn_; }),
                                      check(L"Search all tile groups", L"", searchAll_, [this] { searchAll_ = !searchAll_, suggest_.clear(), invalidate(); }), sep,
                                      item(L"Go to tile group\x2026", L"", [this] { focus(), jump_.focus(); })};
        if (ed_) {  // (the tileset's ramp doodads: where a mod's ramp art usually is)
            Item doodads;
            doodads.label = L"Go to a ramp doodad";
            for (const uint16_t id : stardraft::rampDoodads(*ed_))
                if (const stardraft::DoodadType* t = ed_->doodadTypes().find(id))
                    doodads.sub.push_back(item(L"Doodad " + std::to_wstring(id) + L"  (" + std::to_wstring(t->width) + L" x " + std::to_wstring(t->height) + L", group " +
                                                   std::to_wstring(t->firstGroup) + L")",
                                               L"", [this, g = int(t->firstGroup)] { scrollPaletteTo(g); }));
            doodads.enabled = !doodads.sub.empty();
            viewMenu.push_back(doodads);
        }
        bar_.setMenus({{L"File", std::move(file)}, {L"Edit", std::move(edit)}, {L"Mapping", std::move(mapping)}, {L"View", std::move(viewMenu)}});
    }
    static int statusH() { return scale(26); }

    // Up / Down: the next or the previous template tile of the grid (the checklist); Ctrl+L: the next with no mod tile.
    void stepTarget(int d) {
        const std::vector<uint16_t> g = gridTiles();
        if (g.empty()) return;
        const auto at = std::find(g.begin(), g.end(), uint16_t(std::max(0, target_)));
        const int i = target_ < 0 || at == g.end() ? (d > 0 ? 0 : int(g.size()) - 1) : std::clamp(int(at - g.begin()) + d, 0, int(g.size()) - 1);
        target_ = g[size_t(i)], activePiece_ = -1;
        followTarget();
    }
    void nextUnmapped() {
        const std::vector<uint16_t> g = gridTiles();
        const spass::TileMap* m = tiles();
        if (!m || g.empty()) return;
        const auto at = std::find(g.begin(), g.end(), uint16_t(std::max(0, target_)));
        const size_t from = target_ < 0 || at == g.end() ? 0 : size_t(at - g.begin()) + 1;
        for (size_t k = 0; k < g.size(); ++k)
            if (const uint16_t v = g[(from + k) % g.size()]; !m->count(v)) return void((target_ = v, activePiece_ = -1, followTarget()));
        setMessage(L"Every template tile has a mod tile");
    }
    // The palette shows the target's mod tile (its row near the top), and the grid the target.
    void followTarget() {
        uint16_t to = 0;
        if (const spass::TileMap* m = tiles(); m && target_ >= 0 && spass::mapTile(*m, uint16_t(target_), to)) scrollPaletteTo(std::max(0, (to >> 4) - 3));
        const std::vector<uint16_t> g = gridTiles();
        if (const auto at = std::find(g.begin(), g.end(), uint16_t(std::max(0, target_))); target_ >= 0 && at != g.end()) {
            const int cw = cell() + scale(6), ch = 2 * cell() + scale(8), perRow = std::max(1, (grid_.width() - scale(4)) / cw);
            const int y = int(at - g.begin()) / perRow * ch;
            if (y < gridScroll_) gridScroll_ = y;
            else if (y + ch > gridScroll_ + grid_.height()) gridScroll_ = y + ch - grid_.height() + scale(8);
        }
        invalidate();
    }
    // A block of tileset tiles (dragged over the palette: rows g0 to g1, subtiles c0 to c1) onto the template's tiles from
    // the target: the template tile `dy` groups and `dx` subtiles from the target takes the block's tile there.
    void assignBlock(int g0, int c0, int g1, int c1) {
        spass::TileMap* m = tiles();
        if (!m || target_ < 0) return;
        const std::vector<uint16_t> t = templateTiles(rows_[size_t(sel_)]);
        const std::set<uint16_t> in(t.begin(), t.end());
        const int tg = target_ >> 4, ts = target_ & 15;
        int n = 0;
        for (int dy = 0; dy <= g1 - g0; ++dy)
            for (int dx = 0; dx <= c1 - c0 && ts + dx < 16; ++dx)
                if (const uint16_t v = uint16_t((tg + dy) << 4 | (ts + dx)); in.count(v)) (*m)[v] = uint16_t((g0 + dy) << 4 | (c0 + dx)), ++n;
        if (!n) return;
        edited(), list_.invalidate();
        setMessage(std::to_wstring(n) + L" template tiles mapped");
    }
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
    // The file is the one in use, also at the next start (the settings PassagesFile and PassagesOn: Program options >
    // Plugins > Ramp and bridge mappings).
    void useFile() {
        spass::use(doc_);
        inUseFile() = file_, inUseOn() = true, detached_ = false;
        if (!gSettingsOff) prefs::setText(L"PassagesFile", file_.wstring()), prefs::setNumber(L"PassagesOn", 1);
        shellPaletteRebuilt();  // (the tool strip: the Bridge and the Ramp where the tileset now has them)
    }

    // Pictures (cached: a tile of the map's tileset, a row of 16 of it, a tile of the template's game tileset).
    draw::Bitmap* modTile(uint16_t v) {
        if (!tilesFor_) return nullptr;
        auto& b = modPics_[v];
        if (!b) b = isomrules::tilesBitmap(*tilesFor_, {v}, 1, 1);
        return b.get();
    }
    draw::Bitmap* modRow(int g) {
        if (!tilesFor_) return nullptr;
        auto& b = rowPics_[g];
        if (!b) {
            std::vector<uint16_t> t;
            for (int s = 0; s < 16; ++s) t.push_back(uint16_t(g << 4 | s));
            b = isomrules::tilesBitmap(*tilesFor_, t, 16, 1);
        }
        return b.get();
    }
    const stardraft::Tileset* gameTileset(int era) {
        if (era < 0) return nullptr;
        auto& t = gameTilesets_[era];
        if (!t) {
            t = std::make_unique<stardraft::Tileset>();
            std::string err;
            if (!shellState().data.isOpen()) shellOpenGameData();
            if (!t->load(shellState().data, uint16_t(era), err))
                t->groups.clear(), shellLogLine(L"Ramp and bridge mapper: the game's tileset did not load: " + widen(err)), std::printf("game tileset %d: %s\n", era, err.c_str());
        }
        return t->groups.empty() ? nullptr : t.get();
    }
    draw::Bitmap* gameTile(int era, uint16_t v) {
        const stardraft::Tileset* ts = gameTileset(era);
        if (!ts) return nullptr;
        auto& b = gamePics_[uint32_t(era) << 16 | v];
        if (!b) b = isomrules::tilesBitmap(*ts, {v}, 1, 1);
        return b.get();
    }
    int paletteRows() const { return tilesFor_ ? int(tilesFor_->groups.size()) : 0; }
    void scrollPaletteTo(int g) {
        palScroll_ = std::clamp(g, 0, std::max(0, paletteRows() - 1)) * palRowH();
        clampPalette();
        invalidate();
    }
    void clampPalette() { palScroll_ = std::clamp(palScroll_, 0, std::max(0, paletteRows() * palRowH() - pal_.height())); }
    int palTile() const { return scale(20); }
    int palRowH() const { return palTile() + scale(2); }
    int cell() const { return scale(32); }

    // The parts of the window (resized).
    draw::Rect top_, side_, pal_, templatePic_, resultPic_, grid_, shapeBox_;
    int pictureScale_ = 1;

    struct Step {
        spass::Passages doc;
        int sel, target;
    };
    std::vector<Step> undo_, redo_;
    spass::Passages before_;  // (doc_ before the next edit)
    bool merging_ = false;
    int activePiece_ = -1;          // (the piece a click in the tileset's tiles puts down; -1: none, the target's way)
    int hoverRow_ = -1, hoverCol_ = -1;  // (the tileset's tile under the mouse)
    mutable int runStart_ = 0;     // (pieces(): the run's first group)
    // A drag over the palette: its first and its last tile (row: tile group, column: subtile).
    bool palDrag_ = false;
    int dragRow0_ = 0, dragCol0_ = 0, dragRow1_ = 0, dragCol1_ = 0;
    int palMods_ = 0;

    std::vector<Row> rows_;
    std::vector<int> templates_;  // (the Template list's values: a ramp block's first doodad, or a bridge kit's tileset)
    std::vector<std::string> terrainNames_;
    std::vector<uint16_t> terrainIds_;
    std::wstring shape_;
    spass::Passages doc_;
    std::filesystem::path file_;
    bool dirty_ = false, shown_ = false;
    int sel_ = -1, target_ = -1, era_ = -1, palScroll_ = 0, gridScroll_ = 0;
    const Editor* ed_ = nullptr;
    const stardraft::Tileset* tilesFor_ = nullptr;
    size_t groupsFor_ = 0;
    bool toolsHad_[2] = {false, false};  // (the open map had the Bridge and the Ramp tools at the last edit)
    std::map<uint16_t, std::unique_ptr<draw::Bitmap>> modPics_;
    std::map<int, std::unique_ptr<draw::Bitmap>> rowPics_;
    std::map<uint32_t, std::unique_ptr<draw::Bitmap>> gamePics_;
    std::map<int, std::unique_ptr<stardraft::Tileset>> gameTilesets_;

    isomrules::EditorMenuBar bar_;
    std::string menuKey_;
    std::wstring status_;
    bool overlays_ = true, others_ = true;  // (View: the pieces' colors; the other facings' tiles, dim)
    bool suggestOn_ = true;                 // (View: the next piece's places found after each piece)
    std::vector<Place> suggest_;            // (the picked piece's best places, the best first)
    int suggestAt_ = 0, suggestPiece_ = -1, suggestView_ = -1, suggestSel_ = -1;
    size_t suggestTiles_ = 0;
    std::wstring suggestFor_;
    std::vector<Sig> modSigs_;
    std::vector<uint8_t> rampRows_;
    size_t rampRowsFor_ = size_t(-1);
    std::vector<uint8_t> plainRows_;
    size_t plainRowsFor_ = size_t(-1);
    bool searchAll_ = false;  // (View > Search all tile groups)
    size_t modSigsFor_ = 0;
    std::map<uint32_t, Sig> gameSigs_;
    stardraft::kit::Button addRow_, removeRow_;
    stardraft::kit::Button guideA_, guideB_, guideHide_, emptyRamp_, emptyBridge_;
    std::function<void()> guideRunA_, guideRunB_;
    bool guideOn_ = true;
    bool guideShown_ = false;              // (resized() made room for it)
    bool detached_ = false;                // (Program options changed the file while this window had unsaved work)
    std::set<std::wstring> checked_;       // (the mappings whose result the user said looks right: "r" / "b" and the name)
    draw::Rect guide_, empty_;
    std::wstring guideKey_;
    stardraft::kit::ListView list_;
    stardraft::kit::TextField name_, jump_;
    stardraft::kit::Dropdown template_, view_;
    stardraft::kit::Dropdown terr_[passagemap::kMaxTerrainRows][3];
};

inline void PassagesView::resized() {
    const int pad = scale(12), gap = scale(8), bh = scale(stardraft::kit::kButtonH), sideW = scale(260), palW = scale(44) + 16 * palTile() + scale(14);
    const int menuH = scale(28), bottom = height() - statusH() - pad;  // (the menu bar, as the main window's; the status bar)
    bar_.setFrame({0, 0, width(), menuH});
    const int y0 = menuH + pad;
    top_ = {0, height() - statusH(), width(), height()};
    side_ = {pad, y0, pad + sideW, bottom};
    list_.setFrame(side_);
    pal_ = {width() - pad - palW, y0 + bh + gap, width() - pad, bottom};
    jump_.setFrame({pal_.left, y0, pal_.left + scale(100), y0 + bh});
    // The middle: fields, terrains, view, pictures, grid.
    const int mx = side_.right + pad * 2, mr = pal_.left - pad * 2, lw = scale(80);
    int y = y0;
    name_.setFrame({mx + lw, y, mx + lw + scale(170), y + bh});
    template_.setFrame(templatePick() ? draw::Rect{mx + lw + scale(170) + gap + scale(50), y, mr, y + bh} : draw::Rect{0, 0, 0, 0});
    shapeBox_ = {mx + lw + scale(170) + gap + scale(50), y, mr, y + bh};
    y += bh + gap;
    const int cols = std::max(1, terrainCols()), rows = terrainRows();
    const int cw = (mr - mx - lw - 2 * (bh + gap)) / cols - gap;
    for (int r = 0; r < passagemap::kMaxTerrainRows; ++r)
        for (int c = 0; c < 3; ++c) {
            const bool on = r < rows && c < terrainCols();
            terr_[r][c].setFrame(on ? draw::Rect{mx + lw + c * (cw + gap), y + scale(18) + r * (bh + gap), mx + lw + c * (cw + gap) + cw, y + scale(18) + r * (bh + gap) + bh}
                                    : draw::Rect{0, 0, 0, 0});
        }
    const int ty = y + scale(18);
    addRow_.setFrame(sel_ >= 0 ? draw::Rect{mr - 2 * bh - gap, ty, mr - bh - gap, ty + bh} : draw::Rect{0, 0, 0, 0});
    removeRow_.setFrame(sel_ >= 0 ? draw::Rect{mr - bh, ty, mr, ty + bh} : draw::Rect{0, 0, 0, 0});
    y = ty + std::max(1, rows) * (bh + gap) + gap;
    view_.setFrame({mx + lw, y, mx + lw + scale(200), y + bh});
    y += bh + gap;
    guideShown_ = guideOn_ && sel_ >= 0;
    if (sel_ < 0)  // (no mapping: none of its fields; the empty window's text and buttons)
        for (draw::View* v : std::initializer_list<draw::View*>{&name_, &template_, &view_, &jump_, &addRow_, &removeRow_}) v->setFrame({0, 0, 0, 0});
    for (draw::View* v : std::initializer_list<draw::View*>{&name_, &view_, &jump_, &addRow_, &removeRow_}) v->setVisible(sel_ >= 0);
    template_.setVisible(sel_ >= 0 && templatePick());
    if (guideShown_) {  // (the guide: its steps over its text at the left, its buttons at the right)
        guide_ = {mx, y, mr, y + bh + scale(12)};
        const int bw = scale(140), by = guide_.top + scale(6), hx = guide_.right - scale(6) - bh;
        guideHide_.setFrame({hx, by, hx + bh, by + bh});
        guideA_.setFrame({hx - scale(6) - bw, by, hx - scale(6), by + bh});
        guideB_.setFrame({guideA_.frame().left - scale(6) - bw, by, guideA_.frame().left - scale(6), by + bh});
        guideA_.setVisible(true), guideHide_.setVisible(true), guideB_.setVisible(!guideB_.text().empty());
        y = guide_.bottom + gap;
    } else {
        guide_ = {0, 0, 0, 0};
        for (stardraft::kit::Button* b : {&guideA_, &guideB_, &guideHide_}) b->setFrame({0, 0, 0, 0}), b->setVisible(false);
    }
    y += scale(18);
    {  // (no mapping selected: two big buttons in the middle)
        const int bw = scale(200), bh2 = scale(44), cx = (mx + mr) / 2, cy = y0 + scale(170);
        empty_ = sel_ < 0 && era_ >= 0 ? draw::Rect{mx, y0 + scale(60), mr, cy + bh2 + scale(60)} : draw::Rect{0, 0, 0, 0};
        emptyRamp_.setFrame(empty_.empty() ? draw::Rect{0, 0, 0, 0} : draw::Rect{cx - bw - scale(8), cy, cx - scale(8), cy + bh2});
        emptyBridge_.setFrame(empty_.empty() ? draw::Rect{0, 0, 0, 0} : draw::Rect{cx + scale(8), cy, cx + bw + scale(8), cy + bh2});
        emptyRamp_.setVisible(!empty_.empty()), emptyBridge_.setVisible(!empty_.empty());
    }
    const int picH = std::max(scale(120), (bottom - y) * 5 / 10);
    const int half = (mr - mx - gap) / 2;
    templatePic_ = {mx, y, mx + half, y + picH};
    resultPic_ = {mx + half + gap, y, mr, y + picH};
    grid_ = {mx, y + picH + gap + scale(18), mr, bottom};
    clampPalette();
}

inline void PassagesView::paint(draw::Canvas& c) {
    c.fill({0, 0, width(), height()}, kBg);
    if (stale()) {  // (the map's tab closed or changed before the loop's follow(): no picture of the old tileset's tiles)
        shellPost([this] { refresh(); });
        return;
    }
    const int lw = scale(80), mx = side_.right + scale(24);
    const auto label = [&](draw::Rect r, const std::wstring& t, draw::Color col = kDim) { c.text(r, t, col, draw::kLeft | draw::kMiddle | draw::kEllipsis); };
    updateMenus();
    c.fill({0, bar_.frame().bottom, width(), bar_.frame().bottom + 1}, kBorder);
    {  // The status bar: the last message at the left; the tileset and the file at the right.
        const int pad = scale(8);
        c.fill(top_, kPanel), c.fill({0, top_.top, width(), top_.top + 1}, kBorder);
        std::wstring right = ed_ ? isomrules::tilesetTitle(era_) + L", " + std::to_wstring(paletteRows()) + L" tile groups  \x00b7  " : std::wstring();
        right += file_.empty() ? std::wstring(L"Untitled") : file_.wstring();
        const int fw = std::min(width() / 2, c.measure(right) + 2 * pad);
        c.text({top_.right - fw, top_.top, top_.right - pad, top_.bottom}, right, kText, draw::kMiddle | draw::kRight | draw::kEllipsis);
        const std::wstring left = !ed_ ? std::wstring(L"No CMBW map open")
                                  : sel_ < 0 ? std::wstring(L"No mapping") : status_;
        c.text({top_.left + pad, top_.top, top_.right - fw - pad, top_.bottom}, left, kDim, draw::kMiddle | draw::kLeft | draw::kEllipsis);
    }
    if (!empty_.empty()) {  // (no mapping: what the window is for, and the two buttons that start one)
        c.text({empty_.left, empty_.top, empty_.right, empty_.top + scale(30)}, isomrules::tilesetTitle(era_) + L": ramps & bridges", kText,
               draw::kCenter | draw::kMiddle);

    }
    if (sel_ < 0) return;
    const bool isBridge = rows_[size_t(sel_)].bridge;
    if (guideShown_) {  // The guide: the steps, the step's text, a frame round what it is about.
        const Guide g = guide();
        syncGuide(g);
        {  // (its buttons as wide as their texts, right to left from the hide button)
            const draw::Rect h = guideHide_.frame();
            const int aw = guideA_.preferredWidth(c), bw = guideB_.preferredWidth(c);
            guideA_.setFrame({h.left - scale(6) - aw, h.top, h.left - scale(6), h.bottom});
            guideB_.setFrame({guideA_.frame().left - scale(6) - bw, h.top, guideA_.frame().left - scale(6), h.bottom});
        }
        c.fill(guide_, kCtl), c.frame(guide_, kAccent, 1);
        int x = guide_.left + scale(12);
        const int dy = guide_.top + guide_.height() / 2, d = scale(8);
        for (size_t i = 0; i < g.steps.size(); ++i) {  // (a dot a step: done and now filled; then the step's name)
            const draw::Rect r{x, dy - d / 2, x + d, dy + d / 2};
            if (int(i) <= g.at) c.fill(r, int(i) == g.at ? kAccentBright : kAccentLine);
            else c.frame(r, kBorder, 1);
            x = r.right + scale(4);
        }
        if (g.at < int(g.steps.size())) {
            const std::wstring now = g.steps[size_t(g.at)];
            c.text({x + scale(6), guide_.top, x + scale(6) + c.measure(now), guide_.bottom}, now, kText, draw::kLeft | draw::kMiddle);
            x += scale(6) + c.measure(now) + scale(6);
        }
        const int textR = (guideB_.visible() ? guideB_.frame().left : guideA_.frame().left) - scale(12);
        c.text({x + scale(8), guide_.top, textR, guide_.bottom}, g.text, kDim, draw::kLeft | draw::kMiddle | draw::kEllipsis);
        if (!g.target.empty()) {
            const draw::Rect t = g.target;
            c.frame({t.left - scale(4), t.top - scale(4), t.right + scale(4), t.bottom + scale(4)}, kAccentBright, scale(2));
        }
    }
    label({mx, name_.frame().top, mx + lw, name_.frame().bottom}, L"Name");
    label({shapeBox_.left - scale(50), shapeBox_.top, shapeBox_.left, shapeBox_.bottom}, templatePick() ? L"Template" : L"Shape");
    if (!templatePick()) label(shapeBox_, shape_, kText);
    {
        const int y = name_.frame().bottom + scale(8);
        const wchar_t* heads[3] = {isBridge ? L"Coast A" : L"Ground", isBridge ? L"Water" : L"Cliff", L"Coast B"};
        for (int k = 0; k < terrainCols(); ++k) label({terr_[0][k].frame().left, y, terr_[0][k].frame().right, y + scale(18)}, heads[k]);
        label({mx, y + scale(18), mx + lw, y + scale(18) + scale(stardraft::kit::kButtonH)}, L"Terrain");
    }
    label({mx, view_.frame().top, mx + lw, view_.frame().bottom}, isBridge ? L"Deck" : L"Facing");
    // The template and the result: each tile with its piece's color.
    const std::vector<Piece> ps = pieces();
    const std::map<uint16_t, draw::Color> colors = pieceColors(ps);
    const Picture p = templatePicture();
    const int era = templateEra();
    const spass::TileMap* m = tiles();
    {  // (the template's caption: each color and its piece; the result's: what an empty tile is)
        const int cy = templatePic_.top - scale(18), sw = scale(10);
        int x = templatePic_.left + c.measure(L"Template") + scale(14);
        label({templatePic_.left, cy, x, templatePic_.top}, L"Template");
        if (overlays_)
            for (const Piece& q : ps) {
                const std::wstring n = q.name.substr(q.name.find(L' ') + 1);  // (a ramp's: the kind, not the facing; a bridge's: its groups)
                const int w = sw + scale(4) + c.measure(n) + scale(12);
                if (x + w > templatePic_.right) {
                    label({x, cy, templatePic_.right, templatePic_.top}, L"\x2026");
                    break;
                }
                c.fill({x, cy + scale(4), x + sw, cy + scale(4) + sw}, q.color);
                label({x + sw + scale(4), cy, x + w, templatePic_.top}, n);
                x += w;
            }
        label({resultPic_.left, cy, resultPic_.left + scale(60), resultPic_.top}, L"Result");
        const int ex = resultPic_.left + scale(60);
        c.frame({ex, cy + scale(4), ex + sw, cy + scale(4) + sw}, kDim, 1);
        label({ex + sw + scale(4), cy, resultPic_.right, resultPic_.top}, L"unmapped");
    }
    c.fill(templatePic_, kPanel), c.fill(resultPic_, kPanel);
    if (p.w > 0) {
        int s = cell();
        while (s > scale(8) && (p.w * s > templatePic_.width() || p.h * s > templatePic_.height())) s -= scale(4);
        pictureScale_ = s;
        for (int pass = 0; pass < 2; ++pass) {
            const draw::Rect box = pass ? resultPic_ : templatePic_;
            const int ox = box.left + (box.width() - p.w * s) / 2, oy = box.top + (box.height() - p.h * s) / 2;
            c.pushClip(box);
            for (int y = 0; y < p.h; ++y)
                for (int x = 0; x < p.w; ++x) {
                    const uint16_t v = p.tiles[size_t(y) * p.w + x];
                    if (v == 0xFFFF) continue;
                    const draw::Rect r{ox + x * s, oy + y * s, ox + (x + 1) * s, oy + (y + 1) * s};
                    uint16_t to = 0;
                    const bool mapped = m && spass::mapTile(*m, v, to);
                    if (draw::Bitmap* b = pass ? (mapped ? modTile(to) : nullptr) : gameTile(era, v)) c.bitmap(*b, {0, 0, b->width(), b->height()}, r);
                    if (pass && !mapped) c.fill(r, kBg), c.frame(r, kBorder, 1);
                    if (const auto col = colors.find(v); overlays_ && col != colors.end()) {
                        const bool active = activePiece_ >= 0 && activePiece_ < int(ps.size()) && ps[size_t(activePiece_)].color == col->second &&
                                            std::find(ps[size_t(activePiece_)].tiles.begin(), ps[size_t(activePiece_)].tiles.end(), v) != ps[size_t(activePiece_)].tiles.end();
                        if (!pass) c.fill(r, col->second, active ? 90 : 45);
                        c.frame(r, col->second, active ? scale(2) : 1);
                    }
                    if (v == target_) c.frame(r, kAccentBright, scale(2));
                }
            c.popClip();
        }
    }
    // The pieces: each a card, its template tiles over its mod tiles; the picked one framed in its color.
    {
        int mapped = 0, all = 0;
        for (const Piece& q : ps) {
            const auto [d, a] = pieceDone(q);
            mapped += d, all += a;
        }
        const std::wstring how = activePiece_ >= 0 && activePiece_ < int(ps.size()) ? L"  \x00b7  " + ps[size_t(activePiece_)].name + L" \x2192 tile list" : L"";
        label({grid_.left, grid_.top - scale(18), grid_.right, grid_.top}, L"Pieces  " + std::to_wstring(mapped) + L"/" + std::to_wstring(all) + how);
    }
    c.fill(grid_, kPanel);
    c.pushClip(grid_);
    const std::vector<draw::Rect> cards = pieceCards(ps);
    const int t = miniTile();
    for (size_t i = 0; i < ps.size(); ++i) {
        const Piece& q = ps[i];
        const draw::Rect r = cards[i];
        if (r.bottom < grid_.top || r.top > grid_.bottom) continue;
        const bool active = int(i) == activePiece_;
        c.fill(r, active ? kCtlHot : kCtl);
        c.frame(r, q.color, active ? scale(3) : 1);
        const auto [done, all] = pieceDone(q);
        c.fill({r.left + scale(6), r.top + scale(6), r.left + scale(16), r.top + scale(16)}, q.color);
        label({r.left + scale(20), r.top + scale(2), r.right - scale(4), r.top + scale(20)}, q.name + L"  " + std::to_wstring(done) + L"/" + std::to_wstring(all),
              done == all ? kText : kDim);
        const int x0 = r.left + scale(6), y0 = r.top + scale(24), y1 = y0 + q.h * t + scale(6);
        for (int y = 0; y < q.h; ++y)
            for (int x = 0; x < q.w; ++x) {
                const uint16_t v = q.tiles[size_t(y) * q.w + x];
                if (v == 0xFFFF) continue;
                const draw::Rect a{x0 + x * t, y0 + y * t, x0 + (x + 1) * t, y0 + (y + 1) * t}, b{x0 + x * t, y1 + y * t, x0 + (x + 1) * t, y1 + (y + 1) * t};
                if (draw::Bitmap* bm = gameTile(era, v)) c.bitmap(*bm, {0, 0, bm->width(), bm->height()}, a);
                uint16_t to = 0;
                if (m && spass::mapTile(*m, v, to)) {
                    if (draw::Bitmap* bm = modTile(to)) c.bitmap(*bm, {0, 0, bm->width(), bm->height()}, b);
                } else {
                    c.fill(b, kBg), c.frame(b, kBorder, 1);
                }
                if (v == target_) c.frame(a, kAccentBright, scale(2));
            }
    }
    c.popClip();
    // The tileset's tiles: a group a row.
    c.fill(pal_, kPanel);
    c.pushClip(pal_);
    const int first = palScroll_ / palRowH(), last = std::min(paletteRows(), (palScroll_ + pal_.height()) / palRowH() + 1);
    uint16_t targetTo = 0;
    const bool targetMapped = m && target_ >= 0 && spass::mapTile(*m, uint16_t(target_), targetTo);
    // (each mod tile of a piece: its color)
    std::map<uint16_t, std::pair<draw::Color, bool>> modColors;  // (its color; true: a piece of the shown facing)
    std::map<uint16_t, std::set<uint16_t>> usedBy;  // (the template tiles of each mod tile: two or more, a mark)
    if (m) {
        for (const auto& [v, col] : colors)
            if (uint16_t to = 0; spass::mapTile(*m, v, to)) modColors[to] = {col, true};
        for (int f = 0; f < (ramp() && others_ ? 8 : 0); ++f)
            if (f != view_.value())
                for (const auto& [v, col] : pieceColors(piecesFor(f)))
                    if (uint16_t to = 0; spass::mapTile(*m, v, to)) modColors.emplace(to, std::pair{col, false});
        for (const auto& [v, to] : *m) usedBy[to].insert(v);
    }
    for (int row = first; row < last; ++row) {
        const int y = pal_.top + row * palRowH() - palScroll_, x = pal_.left + scale(44);
        label({pal_.left + scale(4), y, x, y + palTile()}, std::to_wstring(row), targetMapped && (targetTo >> 4) == row ? kAccentBright : kDim);
        if (draw::Bitmap* b = modRow(row)) c.bitmap(*b, {0, 0, b->width(), b->height()}, {x, y, x + 16 * palTile(), y + palTile()});
        for (int k = 0; k < 16; ++k)
            if (const auto col = modColors.find(uint16_t(row << 4 | k)); overlays_ && col != modColors.end()) {
                const draw::Rect r{x + k * palTile(), y, x + (k + 1) * palTile(), y + palTile()};
                c.fill(r, col->second.first, col->second.second ? 70 : 30), c.frame(r, col->second.first, 1);
                if (const auto u = usedBy.find(uint16_t(row << 4 | k)); u != usedBy.end() && u->second.size() > 1)  // (two template tiles here: a yellow corner)
                    c.fill({r.right - scale(7), r.top, r.right, r.top + scale(7)}, RGB(250, 210, 40));
            }
        if (targetMapped && (targetTo >> 4) == row) c.frame({x + (targetTo & 15) * palTile(), y, x + ((targetTo & 15) + 1) * palTile(), y + palTile()}, kAccentBright, scale(2));
    }
    if (activePiece_ >= 0 && activePiece_ < int(ps.size()) && hoverRow_ >= 0) {  // (the picked piece where a click puts it)
        const Piece& q = ps[size_t(activePiece_)];
        const int x = pal_.left + scale(44);
        for (int yy = 0; yy < q.h; ++yy)
            for (int xx = 0; xx < q.w; ++xx)
                if (q.tiles[size_t(yy) * q.w + xx] != 0xFFFF && hoverCol_ + xx < 16) {
                    const int ty = pal_.top + (hoverRow_ + yy) * palRowH() - palScroll_;
                    const draw::Rect r{x + (hoverCol_ + xx) * palTile(), ty, x + (hoverCol_ + xx + 1) * palTile(), ty + palTile()};
                    c.fill(r, q.color, 110), c.frame(r, q.color, scale(2));
                }
    }
    if (activePiece_ >= 0 && activePiece_ < int(ps.size()) && suggesting())  // (the picked piece's best places: numbered, the shown one bold)
        for (size_t k = 0; k < suggest_.size(); ++k) {
            const Piece& q = ps[size_t(activePiece_)];
            const int x = pal_.left + scale(44), y = pal_.top + suggest_[k].row * palRowH() - palScroll_;
            const draw::Rect r{x + suggest_[k].col * palTile(), y, x + (suggest_[k].col + q.w) * palTile(), y + (q.h - 1) * palRowH() + palTile()};
            const bool shown = int(k) == suggestAt_;
            c.frame({r.left - scale(2), r.top - scale(2), r.right + scale(2), r.bottom + scale(2)}, shown ? kAccentBright : q.color, shown ? scale(3) : 1,
                    shown ? draw::LineStyle::Solid : draw::LineStyle::Dash);
            const draw::Rect tag{r.left - scale(16), r.top, r.left - scale(2), r.top + scale(14)};
            c.fill(tag, shown ? kAccentBright : q.color);
            c.text(tag, std::to_wstring(k + 1), RGB(20, 20, 20), draw::kCenter | draw::kMiddle);
        }
    if (palDrag_ && activePiece_ < 0) {  // (the block a drag covers)
        const int x = pal_.left + scale(44), r0 = std::min(dragRow0_, dragRow1_), r1 = std::max(dragRow0_, dragRow1_), c0 = std::min(dragCol0_, dragCol1_),
                  c1 = std::max(dragCol0_, dragCol1_);
        c.frame({x + c0 * palTile(), pal_.top + r0 * palRowH() - palScroll_, x + (c1 + 1) * palTile(), pal_.top + r1 * palRowH() + palTile() - palScroll_}, kAccentBright,
                scale(2));
    }
    c.popClip();
}

inline bool PassagesView::mouse(const draw::MouseEvent& e) {
    if (e.kind == draw::MouseKind::Wheel) {
        const int d = int(std::lround(-e.wheel * 3 * palRowH()));
        if (pal_.contains(e.pt)) {
            palScroll_ += d, clampPalette();
            if (hoverRow_ >= 0) hoverRow_ = std::clamp((e.pt.y - pal_.top + palScroll_) / palRowH(), 0, std::max(0, paletteRows() - 1));
            return invalidate(), true;
        }
        if (grid_.contains(e.pt)) return gridScroll_ = std::max(0, gridScroll_ + d), invalidate(), true;
        return false;
    }
    // The palette's tile under a point (clamped to the palette: a drag past its edge).
    const auto palAt = [&](draw::Point pt, int& row, int& col) {
        row = std::clamp((pt.y - pal_.top + palScroll_) / palRowH(), 0, std::max(0, paletteRows() - 1));
        col = std::clamp((pt.x - pal_.left - scale(44)) / palTile(), 0, 15);
    };
    if (e.kind == draw::MouseKind::Move && !palDrag_) {  // (the picked piece follows the mouse over the tileset's tiles)
        int row = -1, col = -1;
        if (pal_.contains(e.pt) && e.pt.x >= pal_.left + scale(44) && paletteRows() > 0) palAt(e.pt, row, col);
        if (row != hoverRow_ || col != hoverCol_) hoverRow_ = row, hoverCol_ = col, invalidate();
        return false;
    }
    if (e.kind == draw::MouseKind::Leave && hoverRow_ >= 0) return hoverRow_ = hoverCol_ = -1, invalidate(), false;
    if (palDrag_ && (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Up)) {
        palAt(e.pt, dragRow1_, dragCol1_);
        if (e.kind == draw::MouseKind::Up) {
            palDrag_ = false;
            if (host()) host()->capture(*this, false);
            if (activePiece_ >= 0) placePiece(activePiece_, dragRow0_, dragCol0_);  // (where it was pressed: the preview's place)
            else if (target_ < 0) setMessage(L"No piece or template tile picked");
            else if (dragRow0_ == dragRow1_ && dragCol0_ == dragCol1_) assign(uint16_t(dragRow0_ << 4 | dragCol0_), (palMods_ & draw::kAlt) != 0);
            else assignBlock(std::min(dragRow0_, dragRow1_), std::min(dragCol0_, dragCol1_), std::max(dragRow0_, dragRow1_), std::max(dragCol0_, dragCol1_));
        }
        return invalidate(), true;
    }
    if (e.kind != draw::MouseKind::Down || e.button != 0 || sel_ < 0) return false;
    // A tileset tile, or a block of them: the target's mod tile (core: assign, assignBlock).
    if (pal_.contains(e.pt) && e.pt.x >= pal_.left + scale(44)) {
        if (paletteRows() <= 0) return true;
        palAt(e.pt, dragRow0_, dragCol0_);
        dragRow1_ = dragRow0_, dragCol1_ = dragCol0_, palDrag_ = true, palMods_ = int(e.mods);
        if (host()) host()->capture(*this, true);
        return invalidate(), true;
    }
    // A template tile (the pictures or the grid): the target.
    const Picture p = templatePicture();
    for (const draw::Rect& box : {templatePic_, resultPic_})
        if (box.contains(e.pt) && p.w > 0) {
            const int s = pictureScale_, ox = box.left + (box.width() - p.w * s) / 2, oy = box.top + (box.height() - p.h * s) / 2;
            const int x = (e.pt.x - ox) / s, y = (e.pt.y - oy) / s;
            if (e.pt.x >= ox && e.pt.y >= oy && x < p.w && y < p.h && p.tiles[size_t(y) * p.w + x] != 0xFFFF) {
                target_ = p.tiles[size_t(y) * p.w + x];
                // (its piece is the picked one: a click in the tileset's tiles puts the whole piece; Alt: the tile only)
                activePiece_ = -1;
                if (!(e.mods & draw::kAlt)) {
                    const std::vector<Piece> ps = pieces();
                    for (int k = 0; k < int(ps.size()) && activePiece_ < 0; ++k)
                        if (std::find(ps[size_t(k)].tiles.begin(), ps[size_t(k)].tiles.end(), uint16_t(target_)) != ps[size_t(k)].tiles.end()) activePiece_ = k, target_ = -1;
                }
            }
            followTarget();
            return true;
        }
    if (grid_.contains(e.pt)) {  // (a piece's card: that piece is the picked one; a tile in it with Alt: that tile, the target)
        const std::vector<Piece> ps = pieces();
        const std::vector<draw::Rect> cards = pieceCards(ps);
        for (size_t i = 0; i < cards.size(); ++i)
            if (cards[i].contains(e.pt)) {
                const Piece& q = ps[i];
                const int t = miniTile(), x0 = cards[i].left + scale(6), y0 = cards[i].top + scale(24), y1 = y0 + q.h * t + scale(6);
                const int x = (e.pt.x - x0) / t;
                const int y = e.pt.y < y0 ? -1 : e.pt.y < y0 + q.h * t ? (e.pt.y - y0) / t : e.pt.y >= y1 ? (e.pt.y - y1) / t : -1;
                if ((e.mods & draw::kAlt) && e.pt.x >= x0 && x < q.w && y >= 0 && y < q.h && q.tiles[size_t(y) * q.w + x] != 0xFFFF)
                    activePiece_ = -1, target_ = q.tiles[size_t(y) * q.w + x];
                else activePiece_ = int(i), target_ = -1;
                suggest_.clear();
                if (activePiece_ >= 0 && suggestOn_) findPlaces(false);
                followTarget();
                return true;
            }
        return invalidate(), true;
    }
    return false;
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
