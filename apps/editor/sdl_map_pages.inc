// The map pages on this system (research/ports/MAC-BUILD-PLAN.md step 6): Map properties (with Players and forces under
// it), Players and forces, and Map data. The page frame, the scroll view and each page's view are the core's
// (core/views_map_pages.inc, views_map_props.inc, views_players.inc, views_classic_settings.inc). This file is the SDL
// program's part, the same rules as map_pages.inc, map_properties.inc, classic_settings.inc and main.cpp's players:
// the working copies (kept with the tab, Doc::mapPages), the copy from the map and the change into it, the page's timers,
// the title and description fields, and the modal pickers (the list and the color chooser, kit dialogs).
// Included by main_sdl.cpp after sdl_start.inc, before shell_sdl.inc. registerMapPages() puts the views in the window.

// --- The working copies ----------------------------------------------------------------------------------------------

// The program's copy of one page: its values (the core's view holds them while the page shows), the map's revision when
// the copy read it, and whether it has changes that are not applied (the rail's dot).
struct SdlCopy {
    uint64_t revision = 0;
    bool changed = false;
    virtual ~SdlCopy() = default;
    virtual bool has() const = 0;
    virtual bool differs() const = 0;  // the values differ from the map's (as the page first showed them)
    virtual bool applyToMap() = 0;     // the values into the map (one undo step); false: the map did not take them
    virtual void rebase() = 0;         // the values are the map's now
};
enum SdlPageKind { kSdlProps, kSdlPlayers, kSdlClassic, kSdlClassicTrig, kSdlPageCount };

struct SdlPropsCopy final : SdlCopy {
    std::shared_ptr<MapPropsValues> d;
    bool has() const override { return d != nullptr; }
    bool differs() const override { return d && d->baseSet && (d->era != d->eraBase || d->w != d->wBase || d->h != d->hBase); }
    bool applyToMap() override;
    void rebase() override {}
};
struct SdlPlayersCopy final : SdlCopy {
    std::shared_ptr<PlayersValues> d;
    bool has() const override { return d != nullptr; }
    bool differs() const override;
    bool applyToMap() override;
    void rebase() override;
};
struct SdlClassicCopy final : SdlCopy {
    std::shared_ptr<ClassicDialog> d;
    bool has() const override { return d != nullptr; }
    bool differs() const override;
    bool applyToMap() override;
    void rebase() override;
};
// Classic triggers: the page's model (the core's CtModel, the page's collapsed rows) and its copy (the triggers as the page
// edits them, against the map's as the page read them; the map takes them with one undo step, as classic_triggers.inc does).
struct SdlClassicTrigModel : CtModel {
    std::set<int> collapsed;  // the action rows of collapsed blocks (cleared when the trigger changes)
};
struct SdlClassicTrigCopy final : SdlCopy {
    std::shared_ptr<SdlClassicTrigModel> d;
    bool has() const override { return d != nullptr; }
    bool differs() const override { return d && ctModelChanged(*d); }
    bool applyToMap() override {
        if (!d || !gState.ed) return false;
        std::wstring message;
        const int r = ctModelApply(*d, *gState.ed, message);
        if (r == kCtApplySet) updateTitle();
        setMessage(message);
        return r != kCtApplyRefused;
    }
    void rebase() override {
        if (d) d->before = d->trig.triggers;
    }
};

// The copies of the map open in a tab (Doc::mapPages): one per page.
struct MapPageCopies {
    const Editor* owner = nullptr;  // the map the copies are of
    std::unique_ptr<SdlCopy> kinds[kSdlPageCount];
    MapPageCopies() {
        kinds[kSdlProps] = std::make_unique<SdlPropsCopy>();
        kinds[kSdlPlayers] = std::make_unique<SdlPlayersCopy>();
        kinds[kSdlClassic] = std::make_unique<SdlClassicCopy>();
        kinds[kSdlClassicTrig] = std::make_unique<SdlClassicTrigCopy>();
    }
};

// The active tab's copies (`make`: made when there are none). A tab that has another map now has none.
MapPageCopies* sdlCopies(bool make) {
    if (gActive < 0 || gActive >= int(gDocs.size()) || !gState.ed) return nullptr;
    auto& sp = gDocs[size_t(gActive)].mapPages;
    if (sp && sp->owner != gState.ed.get()) sp.reset();
    if (!sp && make) sp = std::make_shared<MapPageCopies>(), sp->owner = gState.ed.get();
    return sp.get();
}

// --- The map's values (the copies' loads, changes and applies) -------------------------------------------------------

// A text as StarCraft has its line ends: \r\n (a \n alone gets its \r, as map_properties.inc does).
std::string sdlTextLineEnds(const std::string& s) {
    std::string out;
    for (char ch : s) {
        if (ch == '\r') continue;
        if (ch == '\n') out += '\r';
        out += ch;
    }
    return out;
}

// A game text as plain text for a list: the color codes (bytes below a space) go.
std::wstring sdlPlainGameText(const std::string& s) {
    std::string out;
    for (char ch : s)
        if (static_cast<unsigned char>(ch) >= 0x20) out += ch;
    return widen(out);
}

// What the size and tileset button will do, under the fields ("" when they are the map's).
void sdlPropsInfo(MapPropsValues& d);
void sdlPropsInvalidate();  // (the Map properties view is painted again: below, with the page's state)
// (the page's own flow, below: the hosts and the frame call them)
void sdlPageRelayoutParts();
void sdlMapPageChanged();
void sdlMapPageTyped();
void sdlMapPageApply();
void sdlMapPageApplyTerrain();
void sdlMapPageReload();
void sdlShowTab(int k);
void sdlMakeContent();
void sdlDropKind(MapPageCopies& all, int kind);

// The Map properties copy from the map (the fields show it when the page is made).
void sdlPropsLoad(MapPropsValues& d) {
    d = MapPropsValues{};
    const Editor& ed = *gState.ed;
    d.nameText = ed.scenarioName(), d.descText = ed.scenarioDescription();
    d.era = ed.era() & 7, d.w = ed.width(), d.h = ed.height();
    d.fillTerrain = mapPropsFirstTerrain(d.era);
    d.baseSet = true;  // (the values the page first shows are the map's: a change is a difference from them)
    d.nameBase = d.nameText, d.descBase = d.descText;
    d.eraBase = d.era, d.wBase = d.w, d.hBase = d.h;
    sdlPropsInfo(d);
}

void sdlPropsInfo(MapPropsValues& d) {
    std::wstring t;
    const Editor& ed = *gState.ed;
    if (d.w != ed.width() || d.h != ed.height())
        t += L"The map becomes " + std::to_wstring(d.w) + L" x " + std::to_wstring(d.h) + L" tiles. StarCraft 2 deletes the parts of the map outside the new size.\r\n";
    if (d.era != int(ed.era()))
        t += L"Each terrain becomes the " + std::wstring(kTilesetNames[d.era & 7]) +
             L" terrain of the same name or of the same kind (for example water or high ground). If there is none, it becomes the new ground's terrain. StarCraft 2 deletes the doodads, because each tileset has its own doodads.\r\n";
    d.infoText = t;
    sdlPropsInvalidate();
}

// The title and the description into the map (one undo step); then they are the map's values.
// A text the page did not change keeps the map's bytes (the box can write a code in another, equal way).
bool sdlPropsTextChanged(const MapPropsValues& d) { return d.baseSet && (d.nameText != d.nameBase || d.descText != d.descBase); }
void sdlPropsApplyText(MapPropsValues& d) {
    Editor& ed = *gState.ed;
    const std::string name = d.nameText != d.nameBase ? sdlTextLineEnds(d.nameText) : ed.scenarioName();
    const std::string desc = d.descText != d.descBase ? sdlTextLineEnds(d.descText) : ed.scenarioDescription();
    ed.setScenarioText(name, desc);
    d.nameBase = d.nameText, d.descBase = d.descText;
    updateTitle();
}

// Apply: the text with \r\n (the game's line ends), and the size and tileset, as one undo step. False: the map stays as it was.
bool sdlPropsApplyAll(MapPropsValues& d) {
    Editor& ed = *gState.ed;
    const bool text = sdlPropsTextChanged(d);
    const bool resize = d.w != ed.width() || d.h != ed.height() || d.era != int(ed.era());
    if (!text && !resize) return true;
    std::string err;
    ed.beginStroke();
    if (text) {
        const std::string name = d.nameText != d.nameBase ? sdlTextLineEnds(d.nameText) : ed.scenarioName();
        const std::string desc = d.descText != d.descBase ? sdlTextLineEnds(d.descText) : ed.scenarioDescription();
        ed.setScenarioText(name, desc);
    }
    bool ok = true;
    if (resize) {
        // Where the old map goes in the new one: the anchor's share of the size difference.
        const int ax = d.anchorIndex % 3, ay = d.anchorIndex / 3;
        const int shiftX = (d.w - ed.width()) * ax / 2, shiftY = (d.h - ed.height()) * ay / 2;
        ok = ed.resizeMap(uint16_t(d.w), uint16_t(d.h), uint16_t(d.era), shiftX, shiftY, d.fillTerrain, err);
    }
    ed.endStroke();
    if (!ok) {
        // The title and description of the same step stay set; the size and tileset do not.
        shellErrorBox(L"The map keeps its size and tileset: " + widen(err), L"Map properties");
        return false;
    }
    if (text) d.nameBase = d.nameText, d.descBase = d.descText;
    if (resize) {
        shellRelayout();  // the minimap takes the new shape
        fitMap();
    }
    updateTitle();
    setMessage(resize ? L"Set the map properties: " + std::to_wstring(d.w) + L" x " + std::to_wstring(d.h) + L" " + kTilesetNames[d.era & 7]
                      : std::wstring(L"Set the map properties"));
    return true;
}

bool SdlPropsCopy::applyToMap() { return d && sdlPropsApplyAll(*d); }

// Players and forces: the problems (the rules of Editor::playabilityProblems, on the values in the copy).
std::vector<std::wstring> sdlPlayersProblems(const PlayersValues& d) {
    std::vector<std::wstring> out;
    int units[8] = {}, starts[8] = {};
    for (const Unit& u : gState.ed->units())
        if (u.owner < 8) ++(u.type == 214 ? starts : units)[u.owner];
    int playing = 0, humans = 0;
    for (int p = 0; p < 8; ++p) {
        const auto c = d.players[size_t(p)].controller;
        playing += c == Editor::Controller::Human || c == Editor::Controller::Computer;
        humans += c == Editor::Controller::Human;
        if (units[p] && !starts[p]) out.push_back(L"Player " + std::to_wstring(p + 1) + L" owns units but has no start location");
    }
    if (playing < 2) out.push_back(L"Set at least two players to Human or Computer (" + std::to_wstring(playing) + L" now)");
    if (!humans) out.push_back(L"Set at least one player to Human");
    return out;
}
void sdlPlayersRefresh(PlayersValues& d) {
    const auto problems = sdlPlayersProblems(d);
    d.problemsText.clear();
    if (problems.empty()) d.problemsText = L"\x2713  StarCraft can start this map";
    else
        for (const auto& pr : problems) d.problemsText += L"\x2717  " + pr + L"\r\n";
}
void sdlPlayersLoad(PlayersValues& d) {
    d = PlayersValues{};
    for (int p = 0; p < 8; ++p) d.players.push_back(gState.ed->player(p));
    for (int f = 0; f < 4; ++f) d.forces.push_back(gState.ed->force(f));
    sdlPlayersRefresh(d);
    d.baseSet = true, d.basePlayers = d.players, d.baseForces = d.forces;  // (the values the page first shows are the map's)
}
bool sdlPlayersSame(const std::vector<Editor::Player>& a, const std::vector<Editor::Force>& af, const std::vector<Editor::Player>& b,
                    const std::vector<Editor::Force>& bf) {
    if (a.size() != b.size() || af.size() != bf.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].controller != b[i].controller || a[i].race != b[i].race || a[i].force != b[i].force || !(a[i].color == b[i].color)) return false;
    for (size_t i = 0; i < af.size(); ++i)
        if (af[i].name != bf[i].name || af[i].flags != bf[i].flags) return false;
    return true;
}
bool SdlPlayersCopy::differs() const {
    return d && d->baseSet && !sdlPlayersSame(d->players, d->forces, d->basePlayers, d->baseForces);
}
bool SdlPlayersCopy::applyToMap() {
    if (!d) return false;
    gState.ed->setPlayersAndForces(d->players, d->forces);
    updateTitle();
    setMessage(L"Set the players and forces");
    return true;
}
void SdlPlayersCopy::rebase() {
    if (!d) return;
    d->basePlayers = d->players, d->baseForces = d->forces;
}

// Map data: the copy from the map, and its changes (as classic_settings.inc).
bool sdlClassicLoad(ClassicDialog& d) {
    const SettingDefaults* def = gState.ed->settingDefaults();
    if (!def) return false;
    d.def = def;
    d.bw = settingsBroodWar(gState.ed->chk());
    d.units = gState.ed->unitSettings(), d.upgrades = gState.ed->upgradeSettings(), d.techs = gState.ed->techSettings();
    for (int u = 0; u < kSettingUnits; ++u) d.names[size_t(u)] = gState.ed->unitSettingName(u);
    d.namesBefore = d.names, d.unitsBefore = d.units, d.upgradesBefore = d.upgrades, d.techsBefore = d.techs;
    return true;
}
bool SdlClassicCopy::differs() const {
    return d && d->def && (d->names != d->namesBefore || d->units != d->unitsBefore || d->upgrades != d->upgradesBefore || d->techs != d->techsBefore);
}
bool SdlClassicCopy::applyToMap() {
    if (!d) return false;
    Editor& ed = *gState.ed;
    std::vector<std::pair<int, std::string>> names;
    for (int u = 0; u < kSettingUnits; ++u)
        if (d->names[size_t(u)] != d->namesBefore[size_t(u)]) names.emplace_back(u, d->names[size_t(u)]);
    const bool units = d->units != ed.unitSettings() || !names.empty(), upgrades = d->upgrades != ed.upgradeSettings(),
               techs = d->techs != ed.techSettings();
    const int kinds = int(units) + int(upgrades) + int(techs);
    if (!kinds) {
        setMessage(L"Map data: no change");
        return true;
    }
    if (kinds > 1) ed.beginStroke();
    if (units) ed.setUnitSettings(d->units, names);
    if (upgrades) ed.setUpgradeSettings(d->upgrades);
    if (techs) ed.setTechSettings(d->techs);
    if (kinds > 1) ed.endStroke();
    updateTitle();
    if (units) shellPaletteRebuilt();  // the custom names in the palette
    setMessage(L"Set the map data");
    return true;
}
void SdlClassicCopy::rebase() {
    if (!d) return;
    d->namesBefore = d->names, d->unitsBefore = d->units, d->upgradesBefore = d->upgrades, d->techsBefore = d->techs;
}

// The unit's entry in the list and the title: the game name and its custom name (Program options > Unit names).
std::wstring sdlClassicEntryName(int tab, int e) {
    if (tab == kClassicUnits && gState.ed && gClassic && e >= 0 && e < int(gState.ed->unitTypes().size())) {
        const UnitType& t = gState.ed->unitTypes()[uint16_t(e)];
        const std::wstring game = widen(t.name) + (t.subtitle.empty() ? L"" : L" (" + widen(t.subtitle) + L")");
        const bool own = !gClassic->units.units[size_t(e)].useDefault;
        return formatUnitName(game, own ? sdlPlainGameText(gClassic->names[size_t(e)]) : L"");
    }
    std::wstring n = entryName(tab == kClassicUpgrades ? "upgrades" : "techdata", e);
    return n.empty() ? L"(" + std::to_wstring(e) + L")" : n;
}

// --- Measuring and drawing text for the views (the canvas of the frame's paint) --------------------------------------

draw::Canvas* gSdlPageCanvas = nullptr;  // the frame's canvas while it paints (its views measure with it)
int sdlTextWidth(const std::wstring& t) {
    if (gSdlPageCanvas) return gSdlPageCanvas->measure(t);
    return int(t.size()) * scale(7);  // (before the first paint: the views measure again when they paint)
}

// --- The pickers: a modal kit dialog with a list of rows, or a grid of swatches (SDL's, for the seam calls) -----------

// A list to pick one row from: a click picks it and closes the dialog; the rows are the caller's.
class SdlPickList final : public draw::View {
public:
    SdlPickList(kit::Dialog& dlg, std::vector<std::wstring> rows, int checked) : dlg_(dlg), rows_(std::move(rows)), checked_(checked) {}
    int picked = -1;
    static int rowH() { return scale(28); }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const draw::Rect r{0, int(i) * rowH(), width(), int(i + 1) * rowH()};
            const bool hot = int(i) == hot_;
            if (hot) c.fill(r, kRowHot);
            const std::wstring label = (int(i) == checked_ ? L"✓  " : L"    ") + rows_[i];
            c.text({r.left + scale(8), r.top, r.right - scale(8), r.bottom}, label, hot ? kText : kDim, draw::kMiddle | draw::kEllipsis);
        }
    }
    bool mouse(const draw::MouseEvent& e) override {
        const int i = e.pt.y / rowH();
        if (e.kind == draw::MouseKind::Leave || e.kind == draw::MouseKind::Move) {
            const int hot = i >= 0 && i < int(rows_.size()) ? i : -1;
            if (hot != hot_) hot_ = hot, invalidate();
            return true;
        }
        if (e.kind == draw::MouseKind::Down && e.button == 0 && i >= 0 && i < int(rows_.size())) {
            picked = i;
            dlg_.close(0);
            return true;
        }
        return false;
    }
    draw::Cursor cursor(draw::Point pt) override { return pt.y / rowH() < int(rows_.size()) ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    int preferredHeight() const { return int(rows_.size()) * rowH(); }

private:
    kit::Dialog& dlg_;
    std::vector<std::wstring> rows_;
    int checked_ = -1;
    int hot_ = -1;
};

// The swatches of a color (the Custom color's picker): a click picks it.
class SdlSwatchGrid final : public draw::View {
public:
    explicit SdlSwatchGrid(kit::Dialog& dlg) : dlg_(dlg) {}
    static constexpr int kCols = 8, kRows = 2;
    static constexpr uint32_t kColors[kCols * kRows] = {0x000000, 0x404040, 0x808080, 0xC0C0C0, 0xFFFFFF, 0xFF0000, 0xFF8000, 0xFFFF00,
                                                       0x00FF00, 0x008000, 0x00FFFF, 0x0000FF, 0x000080, 0x800080, 0xFF00FF, 0x804000};
    int picked = -1;
    static int cell() { return scale(40); }
    int preferredWidth() const { return kCols * cell(); }
    int preferredHeight() const { return kRows * cell(); }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        for (int i = 0; i < kCols * kRows; ++i) {
            const draw::Rect r = cellRect(i);
            const uint32_t v = kColors[i];
            c.fill(r, draw::rgb(int(v >> 16) & 255, int(v >> 8) & 255, int(v) & 255));
            c.fill({r.left, r.top, r.right, r.top + 1}, kBorder), c.fill({r.left, r.bottom - 1, r.right, r.bottom}, kBorder);
            c.fill({r.left, r.top, r.left + 1, r.bottom}, kBorder), c.fill({r.right - 1, r.top, r.right, r.bottom}, kBorder);
        }
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind != draw::MouseKind::Down || e.button != 0) return false;
        const int col = e.pt.x / cell(), row = e.pt.y / cell();
        if (col < 0 || col >= kCols || row < 0 || row >= kRows) return false;
        picked = row * kCols + col;
        dlg_.close(0);
        return true;
    }

private:
    draw::Rect cellRect(int i) const {
        const int x = (i % kCols) * cell(), y = (i / kCols) * cell();
        return {x + scale(3), y + scale(3), x + cell() - scale(3), y + cell() - scale(3)};
    }
    kit::Dialog& dlg_;
};

// The list picker's answer: the row picked, or -1 (Cancel, or the window closed).
int sdlPickFromList(const std::vector<std::wstring>& rows, int checked, const std::wstring& title) {
    if (!gRoot || rows.empty()) return -1;
    kit::Dialog dlg;
    SdlPickList list(dlg, rows, checked);
    dlg.setContent(list);
    dlg.setButtons({{shellTr(L"Cancel"), 1, false, true}});
    const int h = std::min(list.preferredHeight(), scale(12 * 28));
    list.setSize(scale(260), list.preferredHeight());
    dlg.run(*gRoot, title, draw::Point{scale(260) + scale(32), h + scale(96)});
    return list.picked;
}

// The Custom color's picker: the color picked from the swatches, or false (the color stays).
bool sdlPickColor(uint32_t& rgb) {
    if (!gRoot) return false;
    kit::Dialog dlg;
    SdlSwatchGrid grid(dlg);
    dlg.setContent(grid);
    dlg.setButtons({{shellTr(L"Cancel"), 1, false, true}});
    grid.setSize(grid.preferredWidth(), grid.preferredHeight());
    dlg.run(*gRoot, shellTr(L"Custom color"), draw::Point{grid.preferredWidth() + scale(32), grid.preferredHeight() + scale(96)});
    if (grid.picked < 0) return false;
    rgb = SdlSwatchGrid::kColors[grid.picked];
    return true;
}

// --- The page's views -----------------------------------------------------------------------------------------------

// The Map properties view with its title and description: kit text fields in the boxes the core's view gives them. The
// title and the description are plain text on this system (the game text editor is the Windows program's).
class SdlPropsView final : public MapPropsView {
public:
    // The title or the description has the keyboard (the program's shortcuts wait for it).
    bool typingField() const { return name_.hasFocus() || desc_.hasFocus(); }
    SdlPropsView(MapPropsHost& host, MapPropsValues& v) : MapPropsView(host, v), val_(v) {
        name_.setText(widen(v.nameText));
        name_.onChange = [this] { val_.nameText = narrow(name_.text()), sdlMapPageTyped(); };
        desc_.setMultiline(true), desc_.setWrap(true);
        desc_.setText(widen(v.descText));
        desc_.onChange = [this] { val_.descText = narrow(desc_.text()), sdlMapPageTyped(); };
        add(name_, draw::Rect(0, 0, 0, 0));
        add(desc_, draw::Rect(0, 0, 0, 0));
    }
    void paint(draw::Canvas& c) override {
        MapPropsView::paint(c);
        place(name_, nameBox());
        place(desc_, descBox());
    }

private:
    static void place(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
    }
    MapPropsValues& val_;
    kit::TextField name_, desc_;
};

// The program's part of the Map properties view (core/views_map_props.inc: MapPropsHost).
class SdlPropsHost final : public MapPropsHost {
public:
    void changed() override;
    void applyTerrain() override;
    void reset() override;
    int textHeight(bool, int, int boxH) override { return boxH; }
    void fitHeight(int w, int h) override;
};

class SdlPlayersHost final : public PlayersHost {
public:
    void changed() override;
    void typed() override;
    bool pickCustomColor(int p, Editor::PlayerColor& c) override;
    int textWidth(const std::wstring& t) override { return sdlTextWidth(t); }
    void fitSize(int w, int h) override;
};

class SdlClassicHost final : public ClassicHost {
public:
    void changed() override;
    void typed() override;
    std::wstring entryName(int tab, int e) override { return sdlClassicEntryName(tab, e); }
    std::wstring unitGameName(int u) override { return widen(gState.ed->unitTypes()[uint16_t(u)].name); }
    bool hasShields(int u) override { return gState.ed->unitTypes()[uint16_t(u)].hasShields; }
    void drawIcon(draw::Canvas& c, int tab, int e, draw::Rect box) override;
    bool editUnitName(int, std::string&, draw::Point) override {  // (the game text pop-out is the Windows program's)
        setMessage(L"Renaming a unit is not on this system yet");
        return false;
    }
    int textWidth(const std::wstring& t) override { return sdlTextWidth(t); }
    void fitSize(int tab, int w, int h) override;
};

SdlPropsHost gSdlPropsHost;
SdlPlayersHost gSdlPlayersHost;
SdlClassicHost gSdlClassicHost;

// Classic triggers: the page's view and its commands (sdl_classic_triggers.inc; the model is the core's).
#include "sdl_classic_triggers.inc"

// The page's views of the open page (the copy's values are held by the copy; the views are made when the page opens).
struct SdlPageState {
    int kind = -1;               // the open page (SdlPageKind), -1: none
    const Editor* ed = nullptr;  // the map the page shows
    bool ready = false;          // the views are made
    bool typing = false;         // a text is being typed (it goes into the map when the typing stops)
    int reloadTimer = 0, typingTimer = 0;
    std::unique_ptr<SdlPropsView> props;
    std::unique_ptr<PlayersView> players;
    std::unique_ptr<ClassicPageView> classic[kClassicTabCount];
    std::unique_ptr<SdlClassicTrigPage> classicTrig;
};
SdlPageState gSdlPage;

void sdlPropsInvalidate() {
    if (gSdlPage.props) gSdlPage.props->invalidate();
}

MapPageCopies* sdlOpenCopies() { return gSdlPage.ready ? sdlCopies(false) : nullptr; }
// A text field of the open page has the keyboard: the title or description, a force name, a Map data search box or
// number field. The program's one-letter shortcuts wait for it, as they wait for the other search boxes.
bool sdlMapPageTyping() {
    if (gSdlPage.props && gSdlPage.props->typingField()) return true;
    if (gSdlPage.players && gSdlPage.players->typingName()) return true;
    for (auto& v : gSdlPage.classic)
        if (v && v->typingField()) return true;
    return false;
}
SdlCopy* sdlOpenCopy() {
    MapPageCopies* c = sdlOpenCopies();
    return c && gSdlPage.kind >= 0 ? c->kinds[gSdlPage.kind].get() : nullptr;
}
SdlPropsCopy& sdlPropsCopy(MapPageCopies& c) { return static_cast<SdlPropsCopy&>(*c.kinds[kSdlProps]); }
SdlPlayersCopy& sdlPlayersCopy(MapPageCopies& c) { return static_cast<SdlPlayersCopy&>(*c.kinds[kSdlPlayers]); }
SdlClassicCopy& sdlClassicCopy(MapPageCopies& c) { return static_cast<SdlClassicCopy&>(*c.kinds[kSdlClassic]); }
SdlClassicTrigCopy& sdlClassicTrigCopy(MapPageCopies& c) { return static_cast<SdlClassicTrigCopy&>(*c.kinds[kSdlClassicTrig]); }

// A copy goes: the page reads the map again when it is opened next.
void sdlDropKind(MapPageCopies& all, int kind) {
    switch (kind) {
    case kSdlProps: sdlPropsCopy(all).d.reset(); break;
    case kSdlPlayers: sdlPlayersCopy(all).d.reset(); break;
    case kSdlClassic: sdlClassicCopy(all).d.reset(); break;
    case kSdlClassicTrig: sdlClassicTrigCopy(all).d.reset(); break;
    }
    all.kinds[kind]->revision = 0, all.kinds[kind]->changed = false;
}

// Loads the copy of page `kind` from the map (a Map data copy keeps its tab and entries).
bool sdlLoadKind(int kind, MapPageCopies& c) {
    const uint64_t rev = gState.ed->revision();
    switch (kind) {
    case kSdlProps: {
        auto& k = sdlPropsCopy(c);
        k.d = std::make_shared<MapPropsValues>(), sdlPropsLoad(*k.d);
        k.revision = rev, k.changed = false;
        return true;
    }
    case kSdlPlayers: {
        auto& k = sdlPlayersCopy(c);
        k.d = std::make_shared<PlayersValues>(), sdlPlayersLoad(*k.d);
        k.revision = rev, k.changed = false;
        return true;
    }
    case kSdlClassic: {
        auto& k = sdlClassicCopy(c);
        auto d = std::make_shared<ClassicDialog>();
        if (!sdlClassicLoad(*d)) return false;
        if (k.d) d->tab = k.d->tab, std::copy(std::begin(k.d->entry), std::end(k.d->entry), d->entry);  // the same tab and entries show
        k.d = d, k.revision = rev, k.changed = false;
        return true;
    }
    case kSdlClassicTrig: {
        auto& k = sdlClassicTrigCopy(c);
        auto d = std::make_shared<SdlClassicTrigModel>();
        ctModelLoad(*d, *gState.ed);  // the map's triggers (a batch is its template in the list)
        d->ai = loadClassicAiScripts(gState.data);
        k.d = d, k.revision = rev, k.changed = false;
        return true;
    }
    }
    return false;
}

// The views of the open page, made from its copies (Properties: its players and forces under the fields).
void sdlDropViews() {
    gSdlPage.props.reset();  // (each view takes itself out of the scroll view)
    gSdlPage.players.reset();
    for (auto& v : gSdlPage.classic) v.reset();
    gSdlPage.classicTrig.reset();
    gClassic = nullptr;
}
void sdlShowTab(int k);
void sdlMakeContent();

// The page frame's scroll view holds the page's parts: the Map properties page has two (its fields, then players and
// forces), the others one; Map data has its three tabs (one shows).
std::vector<draw::View*> sdlParts() {
    std::vector<draw::View*> out;
    if (gSdlPage.kind == kSdlProps) {
        if (gSdlPage.props) out.push_back(gSdlPage.props.get());
        if (gSdlPage.players) out.push_back(gSdlPage.players.get());
    } else if (gSdlPage.kind == kSdlPlayers) {
        if (gSdlPage.players) out.push_back(gSdlPage.players.get());
    } else if (gSdlPage.kind == kSdlClassic) {
        for (auto& v : gSdlPage.classic)
            if (v) out.push_back(v.get());
    } else if (gSdlPage.kind == kSdlClassicTrig) {
        if (gSdlPage.classicTrig) out.push_back(gSdlPage.classicTrig.get());
    }
    return out;
}
// Where the part k starts (the Map properties page stacks its parts; the others have one).
int sdlPartTop(size_t k) {
    int y = 0;
    const auto parts = sdlParts();
    if (gSdlPage.kind == kSdlProps)
        for (size_t i = 0; i < k && i < parts.size(); ++i) y += parts[i]->height();
    return y;
}

// --- The frame: the page's title and tabs (core/views_map_pages.inc) and the scroll view with the parts ----------------

class SdlPageFrameHost final : public MapPagesHost {
public:
    std::wstring title() override { return gSdlPage.kind >= 0 ? kSdlTitles[gSdlPage.kind] : L""; }
    std::vector<Tab> tabs() override {
        std::vector<Tab> out;
        if (gSdlPage.kind == kSdlClassic)
            for (int k = 0; k < kClassicTabCount; ++k) out.push_back({kClassicTabNames[k], kClassicTabIcons[k]});
        return out;
    }
    int tab() override { return gClassic ? gClassic->tab : 0; }
    void showTab(int k) override { sdlShowTab(k); }
    bool changed() override {
        const SdlCopy* c = sdlOpenCopy();
        return c && c->changed;
    }
    int footerH() override { return 0; }  // (no bar: every change goes into the map at once, GitHub feedback #102)
    void apply() override { sdlMapPageApply(); }
    void revert() override { sdlMapPageReload(); }
    draw::Point contentSize() override {
        int w = 0, h = 0;
        const auto parts = sdlParts();
        for (size_t k = 0; k < parts.size(); ++k) {
            w = std::max(w, parts[k]->width());
            h = std::max(h, sdlPartTop(k) + parts[k]->height());
        }
        return {w, h};
    }
    void fitContent(int w, int h) override {  // (Classic triggers fill the view, and are as tall as they need)
        if (gSdlPage.kind != kSdlClassicTrig || !gSdlPage.classicTrig) return;
        // (the scroll view places the parts after this: a relayout here would call this again, without end)
        gSdlPage.classicTrig->setSize(std::max(w, gSdlPage.classicTrig->preferredWidth()), std::max(h, gSdlPage.classicTrig->preferredHeight()));
    }
    void placeContent(int x, int y) override {
        const auto parts = sdlParts();
        for (size_t k = 0; k < parts.size(); ++k) {
            const int top = sdlPartTop(k) - y;
            parts[k]->setFrame({-x, top, -x + parts[k]->width(), top + parts[k]->height()});
        }
    }
    static inline const wchar_t* const kSdlTitles[kSdlPageCount] = {L"Map properties", L"Players and forces", L"Data", L"Classic triggers"};
};
SdlPageFrameHost gSdlPageFrameHost;

// The page's frame and its scroll view, as one view in the workspace area (registered for the three page workspaces).
struct SdlMapPagesView final : draw::View {
    MapPagesView frame{gSdlPageFrameHost};
    MapScrollView scroll{gSdlPageFrameHost};
    SdlMapPagesView() {
        add(frame, draw::Rect(0, 0, 0, 0));
        add(scroll, draw::Rect(0, 0, 0, 0));
    }
    void resized() override { place(); }
    void paint(draw::Canvas& c) override {
        place();
        gSdlPageCanvas = &c;
    }
    void paintOver(draw::Canvas&) override { gSdlPageCanvas = nullptr; }
    void place() {
        const int top = std::min(height(), frame.headerH());
        setRectIfChanged(frame, draw::Rect(0, 0, width(), height()));
        setRectIfChanged(scroll, draw::Rect(0, top, width(), height()));
    }
    static void setRectIfChanged(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
    }
};
SdlMapPagesView gSdlMapPages;

// The scroll view again: the parts are placed where it says (after a part changes its size).
void sdlPageRelayoutParts() {
    gSdlMapPages.scroll.scrollTo(gSdlMapPages.scroll.scrollX(), gSdlMapPages.scroll.scrollY());
}

// --- The program's hosts' methods -----------------------------------------------------------------------------------

void SdlPropsHost::changed() {
    if (MapPageCopies* c = sdlOpenCopies(); c && gSdlPage.props && c->kinds[kSdlProps]) {
        auto& pc = sdlPropsCopy(*c);
        if (pc.d) sdlPropsInfo(*pc.d);
    }
    sdlMapPageChanged();
}
void SdlPropsHost::applyTerrain() { sdlMapPageApplyTerrain(); }
void SdlPropsHost::reset() { sdlMapPageReload(); }
void SdlPropsHost::fitHeight(int w, int h) {
    if (!gSdlPage.props) return;
    gSdlPage.props->setSize(w, h);
    sdlPageRelayoutParts();  // the players part under it moves
}

void SdlPlayersHost::changed() {
    if (gSdlPage.players) {
        if (MapPageCopies* c = sdlOpenCopies()) {
            auto& k = sdlPlayersCopy(*c);
            if (k.d) sdlPlayersRefresh(*k.d);
        }
        gSdlPage.players->invalidate();
    }
    sdlMapPageChanged();
}
void SdlPlayersHost::typed() { sdlMapPageTyped(); }
bool SdlPlayersHost::pickCustomColor(int p, Editor::PlayerColor& c) {
    uint32_t rgb = 0;
    if (!sdlPickColor(rgb)) return false;
    (void)p;
    c.kind = Editor::PlayerColor::Custom;
    c.rgb = rgb;
    return true;
}
void SdlPlayersHost::fitSize(int w, int h) {
    if (!gSdlPage.players) return;
    gSdlPage.players->setSize(w, h);
    sdlPageRelayoutParts();
}

void SdlClassicHost::changed() { sdlMapPageChanged(); }
void SdlClassicHost::typed() { sdlMapPageTyped(); }
void SdlClassicHost::drawIcon(draw::Canvas& c, int tab, int e, draw::Rect box) {
    if (!gState.ed || !gClassic) return;
    uint16_t value = uint16_t(e);
    if (tab != kClassicUnits) {
        const SettingDefaults& def = *gClassic->def;
        value = tab == kClassicUpgrades ? def.upgradeIcon[size_t(e)] : def.techIcon[size_t(e)];
        if (value >= 0xFFFF) return;
    }
    if (draw::Bitmap* b = paletteBitmap(Mode::Units, int(value), 0)) drawPaletteArt(c, *b, box);
}
void SdlClassicHost::fitSize(int tab, int w, int h) {
    if (!gSdlPage.classic[tab]) return;
    gSdlPage.classic[tab]->setSize(w, h);
    sdlPageRelayoutParts();
}

// --- The page's flow: open, close, changes, the reload -------------------------------------------------------------

// The change of a page's field: the page's copy goes into the map at once (Map properties: its title, description and
// players; the size and tileset wait for their button).
void sdlPropsAutoApply() {
    MapPageCopies* all = sdlOpenCopies();
    if (!all || gSdlPage.kind != kSdlProps) return;
    auto& pc = sdlPropsCopy(*all);
    auto& pl = sdlPlayersCopy(*all);
    if (pc.d && sdlPropsTextChanged(*pc.d)) sdlPropsApplyText(*pc.d);
    if (pl.d && pl.differs()) {
        pl.applyToMap();
        pl.rebase();
    }
    const uint64_t rev = gState.ed->revision();
    pc.revision = pl.revision = rev;  // the copies have the map's values (no reload)
    pl.changed = false;
    const bool terrain = pc.differs();
    if (terrain != pc.changed) pc.changed = terrain;
}

// Any other page: its change goes into the map now (one undo step); a copy that is old reads the map again first.
void sdlMapPageCommit() {
    MapPageCopies* all = sdlOpenCopies();
    if (!all || gSdlPage.kind == kSdlProps || gSdlPage.kind < 0) return;
    SdlCopy& c = *all->kinds[gSdlPage.kind];
    if (!c.differs()) return;
    if (c.revision != gState.ed->revision()) return sdlMapPageReload();  // the map changed (an undo): the page shows it
    if (c.applyToMap()) {
        c.rebase();
        c.revision = gState.ed->revision();
    }
    const bool now = c.differs();
    if (now != c.changed) c.changed = now;
}

// After an edit on the page: the change goes into the map at once (a typed text when the typing stops).
void sdlMapPageChanged() {
    if (!gSdlPage.ready) return;
    if (gSdlPage.kind == kSdlProps) sdlPropsAutoApply();
    else sdlMapPageCommit();
}

// A text typed on the page: it goes into the map half a second after the last key (each key starts the wait again).
void sdlMapPageStartTyping();
void sdlMapPageTyped() {
    if (!gSdlPage.ready) return;
    gSdlPage.typing = true;
    sdlMapPageStartTyping();
}

stardraft::draw::Host* sdlPageHost() { return gSdlMapPages.host(); }
void sdlMapPageStartTyping() {
    draw::Host* h = sdlPageHost();
    if (!h) return;
    if (gSdlPage.typingTimer) h->cancelTimer(gSdlPage.typingTimer);
    gSdlPage.typingTimer = h->timer(500, [] {
        gSdlPage.typingTimer = 0, gSdlPage.typing = false;
        gSdlPage.kind == kSdlProps ? sdlPropsAutoApply() : sdlMapPageCommit();
    });
}
void sdlMapPageStopTyping() {
    if (draw::Host* h = sdlPageHost(); h && gSdlPage.typingTimer) h->cancelTimer(gSdlPage.typingTimer);
    gSdlPage.typingTimer = 0;
    gSdlPage.typing = false;
}

// Apply: the size and tileset (their button), as one undo step; then the page shows the map again.
void sdlMapPageApplyTerrain() {
    if (!gSdlPage.ready || gSdlPage.kind != kSdlProps) return;
    sdlMapPageStopTyping();
    sdlPropsAutoApply();
    MapPageCopies* all = sdlOpenCopies();
    if (!all) return;
    auto& pc = sdlPropsCopy(*all);
    if (!pc.d || !pc.applyToMap()) return;
    sdlMapPageReload();
}

// Apply (the frame's bar, which this system does not show): the copy's changes into the map.
void sdlMapPageApply() {
    if (!gSdlPage.ready) return;
    if (gSdlPage.kind == kSdlProps) sdlMapPageApplyTerrain();
    else sdlMapPageCommit();
}

// The copy again from the map, and the views again (Revert, Reset, a change from outside).
void sdlMapPageReload() {
    MapPageCopies* all = sdlOpenCopies();
    if (!all || gSdlPage.kind < 0) return;
    gSdlPage.ready = false;
    sdlDropViews();
    sdlLoadKind(gSdlPage.kind, *all);
    if (gSdlPage.kind == kSdlProps) sdlLoadKind(kSdlPlayers, *all);  // (players and forces are on this page too)
    sdlMakeContent();
}

// The copy is old (the map changed from outside, an undo) and has no changes: the page reads the map again.
void sdlReloadIfStale() {
    MapPageCopies* all = sdlOpenCopies();
    if (!all || gSdlPage.ed != gState.ed.get() || gSdlPage.typing) return;
    SdlCopy& c = *all->kinds[gSdlPage.kind];
    if (c.revision != gState.ed->revision() && !c.changed) sdlMapPageReload();
}
void sdlArmReload() {
    if (draw::Host* h = sdlPageHost()) gSdlPage.reloadTimer = h->timer(300, [] { sdlReloadIfStale(), sdlArmReload(); });
}

// The views of the open page: made from the copies, placed in the scroll view (the page is as big as its content).
void sdlMakeContent() {
    gSdlPage.ready = false;
    sdlDropViews();
    MapPageCopies* all = sdlCopies(false);
    if (!all) return;
    auto& scroll = gSdlMapPages.scroll;
    switch (gSdlPage.kind) {
    case kSdlProps: {
        auto& pc = sdlPropsCopy(*all);
        gSdlPage.props = std::make_unique<SdlPropsView>(gSdlPropsHost, *pc.d);
        scroll.add(*gSdlPage.props, draw::Rect(0, 0, gSdlPage.props->preferredWidth(), gSdlPage.props->baseHeight()));
        auto& pl = sdlPlayersCopy(*all);
        gSdlPage.players = std::make_unique<PlayersView>(gSdlPlayersHost, *pl.d);
        scroll.add(*gSdlPage.players, draw::Rect(0, 0, gSdlPage.players->preferredWidth(), gSdlPage.players->preferredHeight()));
        break;
    }
    case kSdlPlayers: {
        auto& pl = sdlPlayersCopy(*all);
        gSdlPage.players = std::make_unique<PlayersView>(gSdlPlayersHost, *pl.d);
        scroll.add(*gSdlPage.players, draw::Rect(0, 0, gSdlPage.players->preferredWidth(), gSdlPage.players->preferredHeight()));
        break;
    }
    case kSdlClassicTrig: {
        auto& k = sdlClassicTrigCopy(*all);
        gSdlPage.classicTrig = std::make_unique<SdlClassicTrigPage>(*k.d);
        scroll.add(*gSdlPage.classicTrig, draw::Rect(0, 0, gSdlPage.classicTrig->preferredWidth(), gSdlPage.classicTrig->preferredHeight()));
        break;
    }
    case kSdlClassic: {
        auto& k = sdlClassicCopy(*all);
        gClassic = k.d.get();  // (the core's Map data functions read the copy that shows)
        for (int t = 0; t < kClassicTabCount; ++t) {
            gSdlPage.classic[t] = std::make_unique<ClassicPageView>(gSdlClassicHost, *k.d, t);
            scroll.add(*gSdlPage.classic[t], draw::Rect(0, 0, gSdlPage.classic[t]->preferredWidth(), gSdlPage.classic[t]->preferredHeight()));
        }
        sdlShowTab(gClassic->tab);
        break;
    }
    }
    gSdlPage.ready = true;
    scroll.scrollTo(0, 0);
    if (gSdlPage.kind != kSdlProps) sdlMapPageChanged();  // (a page's copy may differ from the map: as Windows, once)
    gSdlMapPages.invalidate();
}

// Map data: the tab that shows (the others are hidden).
void sdlShowTab(int k) {
    if (gSdlPage.kind != kSdlClassic || !gClassic) return;
    gClassic->tab = std::clamp(k, 0, kClassicTabCount - 1);
    for (int t = 0; t < kClassicTabCount; ++t)
        if (gSdlPage.classic[t]) gSdlPage.classic[t]->setVisible(t == gClassic->tab);
    gSdlMapPages.invalidate();
}

// Closes the open page (its views go; its copy stays with the tab, and an edit is put into the map first).
void sdlCloseOpenPage() {
    if (gSdlPage.kind < 0) return;
    sdlMapPageStopTyping();
    if (gSdlPage.kind == kSdlProps) sdlPropsAutoApply();
    else sdlMapPageCommit();
    if (draw::Host* h = sdlPageHost(); h && gSdlPage.reloadTimer) h->cancelTimer(gSdlPage.reloadTimer);
    gSdlPage.reloadTimer = 0;
    gSdlPage.ready = false;
    sdlDropViews();
    gSdlPage = SdlPageState{};
}

// Opens page `kind` of the map (the views are made from the tab's copies). False: no page (no map, or the game data).
bool sdlMapPageOpen(int kind) {
    if (!gState.ed || !gState.ed->isOpen() || activePage()) return false;
    if (gSdlPage.kind == kind && gSdlPage.ed == gState.ed.get() && gSdlPage.ready) return true;
    sdlCloseOpenPage();
    MapPageCopies* all = sdlCopies(true);
    if (!all) return false;
    SdlCopy& c = *all->kinds[kind];
    if (c.has() && !c.changed && c.revision != gState.ed->revision()) sdlDropKind(*all, kind);  // the map changed: read it again
    if (!c.has() && !sdlLoadKind(kind, *all)) {
        setMessage(L"Could not read the game data files units.dat, weapons.dat, upgrades.dat and techdata.dat. Select the StarCraft folder in Options > Program options, then restart StarDraft 2.");
        return false;
    }
    if (kind == kSdlProps) {  // players and forces are on this page too
        SdlCopy& pl = *all->kinds[kSdlPlayers];
        if (pl.has() && pl.revision != gState.ed->revision()) sdlDropKind(*all, kSdlPlayers);
        if (!pl.has()) sdlLoadKind(kSdlPlayers, *all);
    }
    gSdlPage.kind = kind, gSdlPage.ed = gState.ed.get();
    sdlMakeContent();
    sdlArmReload();  // the map changed from outside: a copy without changes reads it again
    return true;
}


// Whether the open page of the tab has changes (the rail's dot).
bool sdlPageKindChanged(int kind) {
    MapPageCopies* all = sdlCopies(false);
    return all && all->kinds[kind]->changed;
}

// The rail's dot: the page of workspace `id` has changes that are not applied.
int sdlPageKindOfCommand(int id) {
    return id == ID_SCEN_PROPS ? kSdlProps : id == ID_SCEN_PLAYERS ? kSdlPlayers : id == ID_SCEN_CLASSIC ? kSdlClassic
           : id == ID_SCEN_CLASSICTRIG ? kSdlClassicTrig : -1;
}
bool mapPageChangesFor(int id) {
    const int kind = sdlPageKindOfCommand(id);
    return kind >= 0 && sdlPageKindChanged(kind);
}

// The page kind of a workspace (-1: none on this system).
int sdlPageKindOfWorkspace(Workspace w) {
    switch (w) {
    case Workspace::Properties: return kSdlProps;
    case Workspace::Players: return kSdlPlayers;
    case Workspace::ClassicSettings: return kSdlClassic;
    case Workspace::ClassicTriggers: return kSdlClassicTrig;
    default: return -1;
    }
}

// The workspace's page is opened (its view shows) or the page goes (the copy stays with the tab).
bool sdlWorkspacePage(Workspace w) {
    const int kind = sdlPageKindOfWorkspace(w);
    if (kind < 0) {
        sdlCloseOpenPage();
        return false;
    }
    if (!sdlMapPageOpen(kind)) {
        sdlCloseOpenPage();
        return false;
    }
    return true;
}

// Leaving the map (closing its tab, or StarDraft) with changes that are not applied: apply them, or not, or stay.
bool mapPagesConfirm() {
    if (gSdlPage.kind >= 0 && gSdlPage.ed == gState.ed.get()) sdlMapPageStopTyping(), sdlCloseOpenPage();
    MapPageCopies* all = sdlCopies(false);
    if (!all) return true;
    std::wstring names;
    static const wchar_t* const kNames[kSdlPageCount] = {L"Properties", L"Players", L"Data", L"Classic triggers"};
    for (int k = 0; k < kSdlPageCount; ++k)
        if (all->kinds[k]->has() && all->kinds[k]->changed) names += (names.empty() ? L"" : L", ") + std::wstring(kNames[k]);
    if (names.empty()) return true;
    const std::wstring q = L"These pages have changes that are not applied: " + names + L".\n\nApply them to the map?";
    const int r = shellAskUnapplied(q, L"Changes that are not applied");  // 0: Apply, 1: Do not apply, 2: Cancel
    if (r == 2) return false;
    for (int k = 0; k < kSdlPageCount; ++k) {
        SdlCopy& one = *all->kinds[k];
        if (!one.has() || !one.changed) continue;
        if (r == 0 && !one.applyToMap()) return false;  // the map did not take it: stay
        sdlDropKind(*all, k);  // (the page reads the map again when it opens)
    }
    return true;
}

// The frame's close (the core's: a tab closes, a new map opens, a workspace leaves the page).
void mapPageClose() { sdlCloseOpenPage(); }

// Makes the map pages' views the window's workspace views (the rail's Properties, Players and Data workspaces).
void registerMapPages() {
    registerWorkspaceView(Workspace::Properties, gSdlMapPages);
    registerWorkspaceView(Workspace::Players, gSdlMapPages);
    registerWorkspaceView(Workspace::ClassicSettings, gSdlMapPages);
    registerWorkspaceView(Workspace::ClassicTriggers, gSdlMapPages);
}
