// --- Map pages ---------------------------------------------------------------------------------------------------
// The map's own settings as pages in the main area, opened from the rail (under the workspaces) like the workspaces:
// Properties (map_properties.inc), Players (the players page in main.cpp), Classic triggers (classic_triggers.inc)
// and Map data (the classic settings: classic_settings.inc; classic maps only). Included into main.cpp after classic_triggers.inc.
//
// Map properties (with Players and forces under it on the same page) set each change in the map at once: a list or a
// check box at once, a typed text half a second after the last key (each an undo step). Only the size and the tileset
// wait for their own button. The other pages edit a working copy of the map's values. The bar at the bottom has Apply
// (one undo step) and Revert (the map's values again). The copy stays with its tab: leave the page (another workspace, another tab) with changes that
// are not applied and they are there when you come back, with a dot on the rail item. Closing the tab (or StarDraft)
// asks to apply them. A copy without changes reads the map again when the map changed (undo, another edit).
//
// The page: a header (the title; the Units, Upgrades and Tech tabs of Classic settings), a view that scrolls (the
// content: the native controls of the old window, made children of the view), the Apply and Revert bar.

// --- Map properties' view: core/views_map_props.inc (the fields, the dropdowns, the buttons); the game text editors are the
// program's windows in its boxes (MapPropsWinView, the window's child notifications).

// The view in the page's window: the program's game text editors notify it (their changes go into the map).
class MapPropsWinView final : public MapPropsView, public ui::ChildCommands {
public:
    using MapPropsView::MapPropsView;
    void childCommand(int, int code) override {
        if (code == EN_CHANGE) mapPageChanged();
    }
};

enum MapPageKind { kMpProps, kMpPlayers, kMpClassicTrig, kMpClassicSettings, kMpCount };
const wchar_t* const kMpTitles[kMpCount] = {L"Map properties", L"Players and forces", L"Classic triggers", L"Data"};
const wchar_t* const kMpRailNames[kMpCount] = {L"Properties", L"Players", L"Classic triggers", L"Data"};

struct MapPageCopy {
    std::shared_ptr<void> data;  // MapPropsDialog, PlayersDialog, ClassicTrigDialog or ClassicDialog
    uint64_t revision = 0;       // the map's revision when the copy read it
    bool changed = false;        // changes that are not applied (the rail's dot)
};
struct MapPageCopies {
    const Editor* owner = nullptr;  // the map the copies are of
    MapPageCopy kinds[kMpCount];
};

// The program's part of the page frame (core/views_map_pages.inc: MapPagesHost); its methods are below the page's code.
class MapPagesProgram final : public MapPagesHost {
public:
    std::wstring title() override;
    std::vector<Tab> tabs() override;
    int tab() override;
    void showTab(int k) override;
    bool changed() override;
    int footerH() override;
    void apply() override;
    void revert() override;
    stardraft::draw::Point contentSize() override;
    void fitContent(int w, int h) override;
    void placeContent(int x, int y) override;
};
MapPagesProgram gMapPages;

struct MapPageHost {
    HWND wnd = nullptr, view = nullptr;  // wnd: the frame (MapPagesView); view: the scroll view's window (MapScrollView) in it
    HWND content[kClassicTabCount] = {};  // one; three for Classic settings (its tabs)
    int kind = -1;
    const Editor* ed = nullptr;
    bool ready = false;  // the content is made (until then, no reading back from its controls)
    std::unique_ptr<MapPagesView> frameView;
    std::unique_ptr<MapScrollView> scrollView;
    std::unique_ptr<MapPropsView> props;  // Map properties' view (content[0] of that page; made with its window, freed after it)
    std::unique_ptr<PlayersView> players;  // Players and forces' view (content[0] of that page, content[1] of Map properties)
    std::unique_ptr<ClassicPageView> classic[kClassicTabCount];  // Map data's views (content[k]: each tab's page)
    int reloadTimer = 0, typingTimer = 0;  // the host's timers: the check that the map changed (300 ms); the typing's stop (500 ms)
    bool typing = false;  // Map properties: a text is being typed (it goes into the map when the typing stops)
} gMp;

int mapPageKind(Workspace w) {
    switch (w) {
    case Workspace::Properties: return kMpProps;
    case Workspace::Players: return kMpPlayers;
    case Workspace::ClassicTriggers: return kMpClassicTrig;
    case Workspace::ClassicSettings: return kMpClassicSettings;
    default: return -1;
    }
}
int mapPageCommand(int kind) { return kind == kMpProps ? ID_SCEN_PROPS : kind == kMpPlayers ? ID_SCEN_PLAYERS : kind == kMpClassicTrig ? ID_SCEN_CLASSICTRIG : ID_SCEN_CLASSIC; }
int mapPageKindOfCommand(int id) {
    for (int k = 0; k < kMpCount; ++k)
        if (mapPageCommand(k) == id) return k;
    return -1;
}

// The active tab's copies (`make`: made when there are none). A tab that has another map now has none.
MapPageCopies* mapPageCopies(bool make) {
    if (gActive < 0 || gActive >= int(gDocs.size()) || !app.ed) return nullptr;
    auto& sp = gDocs[size_t(gActive)].mapPages;
    if (sp && sp->owner != app.ed.get()) sp.reset();
    if (!sp && make) sp = std::make_shared<MapPageCopies>(), sp->owner = app.ed.get();
    return sp.get();
}

// The rail's dot: the active tab's page `kind` has changes that are not applied.
bool mapPageHasChanges(int kind) {
    const MapPageCopies* c = kind >= 0 ? mapPageCopies(false) : nullptr;
    return c && c->kinds[kind].data && c->kinds[kind].changed;
}

bool mapPageChangesFor(int id) { return mapPageHasChanges(mapPageKindOfCommand(id)); }

// The copy's struct to the globals the page's code uses (nullptr: none).
void mapPageSetGlobals(int kind, void* data, void* players = nullptr) {
    gMapProps = kind == kMpProps ? static_cast<MapPropsDialog*>(data) : nullptr;
    gPlayersDlg = kind == kMpPlayers ? static_cast<PlayersDialog*>(data) : kind == kMpProps ? static_cast<PlayersDialog*>(players) : nullptr;
    gCt = kind == kMpClassicTrig ? static_cast<ClassicTrigDialog*>(data) : nullptr;
    gClassic = kind == kMpClassicSettings ? static_cast<ClassicDialog*>(data) : nullptr;
}

// A copy from the map. False: there is none to make (Classic settings without the game's .dat files).
bool mapPageLoad(int kind, MapPageCopy& c) {
    switch (kind) {
    case kMpProps: {
        auto d = std::make_shared<MapPropsDialog>();
        mapPropsLoad(*d);
        c.data = d;
        break;
    }
    case kMpPlayers: {
        auto d = std::make_shared<PlayersDialog>();
        playersLoad(*d);
        c.data = d;
        break;
    }
    case kMpClassicTrig: {
        auto d = std::make_shared<ClassicTrigDialog>();
        ctLoad(*d);
        c.data = d;
        break;
    }
    case kMpClassicSettings: {
        auto d = std::make_shared<ClassicDialog>();
        if (!classicLoad(*d)) return false;
        c.data = d;
        break;
    }
    default: return false;
    }
    c.revision = app.ed->revision(), c.changed = false;
    return true;
}

MapPageCopy* mapPageOpenCopy() {
    MapPageCopies* c = gMp.wnd && gMp.kind >= 0 ? mapPageCopies(false) : nullptr;
    return c ? &c->kinds[gMp.kind] : nullptr;
}

// The open page's fields into its copy (the Properties and Players pages keep some values in their controls).
void mapPageReadBack() {
    if (!gMp.ready) return;
    if (gMp.kind == kMpProps && gMapProps && IsWindow(gMapProps->wnd)) mapPropsRead(*gMapProps);
}

bool mapPageCopyChanged(int kind, const MapPageCopy& c) {
    if (!c.data) return false;
    switch (kind) {
    case kMpProps: return mapPropsChanged(*static_cast<const MapPropsDialog*>(c.data.get()));
    case kMpPlayers: return playersChanged(*static_cast<const PlayersDialog*>(c.data.get()));
    case kMpClassicTrig: return ctCopyChanged(*static_cast<const ClassicTrigDialog*>(c.data.get()));
    case kMpClassicSettings: return classicChanged(*static_cast<const ClassicDialog*>(c.data.get()));
    }
    return false;
}

int mapPageHeaderH() { return gMp.frameView ? gMp.frameView->headerH() : 0; }
int mapPageFooterH() { return gMp.frameView ? gMp.frameView->footerH() : 0; }  // no Apply bar: every change goes into the map at once (GitHub feedback #102)

void mapPageAutoApply();
void mapPageCommit();
void mapPageFlush();
void mapPageStartTyping();
void mapPageStopTyping();
void mapPageArmReload();
// After an edit on the page: the change goes into the map at once, one undo step (a typed text a moment after the last
// key). GitHub feedback #102: no Apply.
void mapPageChanged() {
    MapPageCopy* c = gMp.ready ? mapPageOpenCopy() : nullptr;
    if (!c) return;
    wchar_t cls[16] = L"";
    GetClassNameW(GetFocus(), cls, 16);
    if ((_wcsicmp(cls, WC_EDITW) == 0 || _wcsicmp(cls, MSFTEDIT_CLASS) == 0) && IsChild(gMp.wnd, GetFocus())) {  // typing: when it stops
        gMp.typing = true, mapPageStartTyping();
        return;
    }
    if (gMp.kind == kMpProps) mapPageAutoApply();
    else mapPageCommit();
}

// A text typed in a kit field of the page (a force name): it goes into the map when the typing stops, as the game text
// editors' do (their window's focus is not seen here: the field is the view's).
void mapPageTyped() {
    if (!gMp.ready || !mapPageOpenCopy()) return;
    gMp.typing = true, mapPageStartTyping();
}

// --- The view: the content, scrolled when the page is smaller than it -------------------------------------------

// Map properties: its parts one under the other (the map's fields, then players and forces); the other pages: one
// part (Map data: the tab that shows).
bool mapPageStacked() { return gMp.kind == kMpProps; }
int mapPagePartTop(int k) {
    int y = 0;
    for (int i = 0; i < k && mapPageStacked(); ++i)
        if (HWND c = gMp.content[i]) {
            RECT r;
            GetWindowRect(c, &r);
            y += r.bottom - r.top;
        }
    return y;
}
SIZE mapPageContentSize() {
    SIZE s{0, 0};
    for (int k = 0; k < kClassicTabCount; ++k)
        if (HWND c = gMp.content[k]) {
            RECT r;
            GetWindowRect(c, &r);
            s.cx = std::max<LONG>(s.cx, r.right - r.left);
            s.cy = std::max<LONG>(s.cy, mapPagePartTop(k) + r.bottom - r.top);
        }
    return s;
}

// The scroll position (the scroll view's); the content windows are placed where it says (MapPagesProgram::placeContent).
int mapPageScrollX() { return gMp.scrollView ? gMp.scrollView->scrollX() : 0; }
int mapPageScrollY() { return gMp.scrollView ? gMp.scrollView->scrollY() : 0; }
void mapPageScrollTo(int x, int y) {
    if (gMp.scrollView) gMp.scrollView->scrollTo(x, y);
}

// The program's part of Map properties' view (core/views_map_props.inc: MapPropsHost). The map changes go through the
// page's code (above); the height is the window's, and the players under the page move with it.
class MapPropsProgram final : public MapPropsHost {
public:
    void changed() override { mapPropsInfo(*gMapProps), mapPageChanged(); }
    void applyTerrain() override { mapPageApplyTerrain(); }
    void reset() override { mapPageReload(); }
    int textHeight(bool description, int boxW, int boxH) override {
        return gameTextHeight(description ? propsDescSpec() : propsNameSpec(), boxW, boxH);
    }
    void fitHeight(int w, int h) override {
        if (!gMp.content[0]) return;
        SetWindowPos(gMp.content[0], nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        mapPageScrollTo(mapPageScrollX(), mapPageScrollY());
    }
};
MapPropsProgram gMapPropsProgram;

// The program's part of Players and forces' view (core/views_players.inc: PlayersHost). The changes go through the page's
// code (main.cpp); the size is the window's, and the page under it moves with it.
class PlayersProgram final : public PlayersHost {
public:
    void changed() override { playersSet(*gPlayersDlg); }
    void typed() override { mapPageTyped(); }
    bool pickCustomColor(int p, Editor::PlayerColor& c) override {
        // The color picker (a test run keeps the color it has); the start is the custom color or the player's own.
        static COLORREF custom[16] = {};
        const uint32_t start = c.kind == Editor::PlayerColor::Custom ? c.rgb : app.ed->playerRgb(p);
        CHOOSECOLORW cc{sizeof cc};
        cc.hwndOwner = app.main, cc.lpCustColors = custom, cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        cc.rgbResult = RGB((start >> 16) & 255, (start >> 8) & 255, start & 255);
        if (gSelfTest || gBackground || !ChooseColorW(&cc)) return false;
        c.kind = Editor::PlayerColor::Custom;
        c.rgb = uint32_t(GetRValue(cc.rgbResult)) << 16 | uint32_t(GetGValue(cc.rgbResult)) << 8 | GetBValue(cc.rgbResult);
        return true;
    }
    int textWidth(const std::wstring& t) override {
        HDC dc = GetDC(app.main);
        HGDIOBJ of = SelectObject(dc, app.font);
        SIZE sz{};
        GetTextExtentPoint32W(dc, t.c_str(), int(t.size()), &sz);
        SelectObject(dc, of);
        ReleaseDC(app.main, dc);
        return sz.cx;
    }
    void fitSize(int w, int h) override {
        if (!gPlayersDlg || !gPlayersDlg->wnd) return;
        SetWindowPos(gPlayersDlg->wnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        mapPageScrollTo(mapPageScrollX(), mapPageScrollY());  // the parts under it, and the scroll range
    }
};
PlayersProgram gPlayersProgram;

// --- The page ------------------------------------------------------------------------------------------------------

void mapPageShowTab(int tab) {
    if (gMp.kind != kMpClassicSettings || !gClassic) return;
    gClassic->tab = std::clamp(tab, 0, kClassicTabCount - 1);
    for (int k = 0; k < kClassicTabCount; ++k) ShowWindow(gMp.content[k], k == gClassic->tab ? SW_SHOW : SW_HIDE);
    InvalidateRect(gMp.wnd, nullptr, FALSE);
}

void mapPageApply();
void mapPageRevert();

void mapPageReloadIfStale();

void mapPageRegister() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = app.inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    const std::pair<WNDPROC, const wchar_t*> classes[] = {{classicTrigProc, L"StarDraftClassicTriggers"},
                                                          {ctListProc, L"StarDraftClassicTrigList"}};  // the painted lists
    for (const auto& [proc, name] : classes) wc.lpfnWndProc = proc, wc.lpszClassName = name, RegisterClassExW(&wc);
}

// The content of the open page from its copy (made again after Apply, Revert, a reload, a theme change).
void mapPageMakeContent() {
    gMp.ready = false;
    for (HWND& c : gMp.content)
        if (c) DestroyWindow(c), c = nullptr;
    gMp.props.reset();  // (its window is gone: the view goes too)
    gMp.players.reset();
    for (auto& v : gMp.classic) v.reset();
    gClassicProgram.wnd[0] = gClassicProgram.wnd[1] = gClassicProgram.wnd[2] = nullptr;
    auto make = [](const wchar_t* cls, void* param) {
        return CreateWindowExW(WS_EX_CONTROLPARENT, cls, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, gMp.view, nullptr, app.inst, param);
    };
    // Players and forces: its view in its window, as big as its content (the page moves it).
    auto makePlayers = [] {
        gMp.players = std::make_unique<PlayersView>(gPlayersProgram, *gPlayersDlg);
        // (a Tab stop: the names are reached by Tab from the page's fields, as the old name EDITs were)
        HWND w = CreateWindowExW(WS_EX_CONTROLPARENT, ui::kViewClass, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, gMp.players->preferredWidth(),
                                 gMp.players->preferredHeight(), gMp.view, nullptr, app.inst, gMp.players.get());
        playersCreated(*gPlayersDlg, w);
        return w;
    };
    switch (gMp.kind) {
    case kMpProps:  // the map's fields (the view), then players and forces
        gMp.props = std::make_unique<MapPropsWinView>(gMapPropsProgram, *gMapProps);
        gMp.content[0] = CreateWindowExW(WS_EX_CONTROLPARENT, ui::kViewClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, gMp.props->preferredWidth(),
                                         gMp.props->baseHeight(), gMp.view, nullptr, app.inst, gMp.props.get());
        mapPropsCreated(*gMapProps, gMp.content[0], gMp.props->nameBox(), gMp.props->descBox());
        SetWindowPos(gMp.content[0], nullptr, 0, 0, gMp.props->preferredWidth(), gMp.props->baseHeight(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (gPlayersDlg) gMp.content[1] = makePlayers();
        break;
    case kMpPlayers: gMp.content[0] = makePlayers(); break;
    case kMpClassicTrig: gMp.content[0] = make(L"StarDraftClassicTriggers", nullptr); break;
    case kMpClassicSettings:  // each tab: its view in its window (the page is as big as it, made before its first paint)
        for (int k = 0; k < kClassicTabCount; ++k) {
            gMp.classic[k] = std::make_unique<ClassicPageView>(gClassicProgram, *gClassic, k);
            gMp.content[k] = CreateWindowExW(WS_EX_CONTROLPARENT, ui::kViewClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, gMp.classic[k]->preferredWidth(),
                                             gMp.classic[k]->preferredHeight(), gMp.view, nullptr, app.inst, gMp.classic[k].get());
            gClassicProgram.wnd[k] = gMp.content[k];
        }
        break;
    }
    if (gMp.kind == kMpClassicSettings) mapPageShowTab(gClassic->tab);
    else
        for (HWND c : gMp.content)
            if (c) ShowWindow(c, SW_SHOW);
    gMp.ready = true;
    mapPageScrollTo(0, 0);
    if (gMp.kind != kMpProps) mapPageChanged();
    InvalidateRect(gMp.wnd, nullptr, FALSE);
}

// Closes the open page (its window goes; its copy stays with the tab).
void mapPageClose() {
    if (!gMp.wnd) return;
    mapPageStopTyping(), gMp.typing = false;  // a text being typed goes into the map
    if (gMp.kind == kMpProps) mapPageAutoApply();
    else mapPageCommit();
    gMp.ready = false;
    DestroyWindow(gMp.wnd);  // (its timers go with it)
    gClassicProgram.wnd[0] = gClassicProgram.wnd[1] = gClassicProgram.wnd[2] = nullptr;
    mapPageSetGlobals(-1, nullptr);
    gMp = MapPageHost{};
    if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
}

// Opens page `kind` of the active tab's map (hidden: setWorkspace shows it after the layout). False: no page.
bool mapPageOpen(int kind) {
    if (!app.ed || !app.ed->isOpen() || activePage()) return false;
    if (gMp.wnd && gMp.kind == kind && gMp.ed == app.ed.get()) return true;
    mapPageClose();
    MapPageCopies* copies = mapPageCopies(true);
    if (!copies) return false;
    MapPageCopy& c = copies->kinds[kind];
    if (c.data && !c.changed && c.revision != app.ed->revision()) c.data.reset();  // the map changed: read it again
    if (!c.data && !mapPageLoad(kind, c)) {
        setMessage(L"Could not read the game data files units.dat, weapons.dat, upgrades.dat and techdata.dat. Select the StarCraft folder in Options > Program options, then restart StarDraft 2.");
        return false;
    }
    void* players = nullptr;
    if (kind == kMpProps) {  // players and forces are on this page too
        MapPageCopy& pc = copies->kinds[kMpPlayers];
        if (pc.data && pc.revision != app.ed->revision()) pc.data.reset();
        if (pc.data || mapPageLoad(kMpPlayers, pc)) players = pc.data.get();
    }
    mapPageRegister();
    mapPageSetGlobals(kind, c.data.get(), players);
    gMp.kind = kind, gMp.ed = app.ed.get();
    gMp.frameView = std::make_unique<MapPagesView>(gMapPages);
    gMp.scrollView = std::make_unique<MapScrollView>(gMapPages);
    gMp.wnd = CreateWindowExW(WS_EX_CONTROLPARENT, ui::kViewClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 1, 1, app.main, nullptr, app.inst, gMp.frameView.get());
    gMp.view = CreateWindowExW(WS_EX_CONTROLPARENT, ui::kViewClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL, 0, 0, 1, 1,
                               gMp.wnd, nullptr, app.inst, gMp.scrollView.get());
    darkControl(gMp.view, L"DarkMode_Explorer");  // the scroll bars in the theme
    mapPageMakeContent();
    mapPageArmReload();  // the map changed from outside: a copy without changes reads it again
    return true;
}

// The copy again from the map, and the content again (Revert, after Apply, a reload).
void mapPageReload() {
    MapPageCopy* c = mapPageOpenCopy();
    if (!c) return;
    MapPageCopy fresh;
    if (!mapPageLoad(gMp.kind, fresh)) return;
    if (gMp.kind == kMpClassicSettings) {  // the same tab and entries show
        auto* was = static_cast<ClassicDialog*>(c->data.get());
        auto* now = static_cast<ClassicDialog*>(fresh.data.get());
        now->tab = was->tab;
        for (int k = 0; k < kClassicTabCount; ++k) now->entry[k] = was->entry[k];
    }
    gMp.ready = false;
    for (HWND& w : gMp.content)  // the old content goes before its copy does
        if (w) DestroyWindow(w), w = nullptr;
    gMp.props.reset();
    gMp.players.reset();
    for (auto& v : gMp.classic) v.reset();
    gClassicProgram.wnd[0] = gClassicProgram.wnd[1] = gClassicProgram.wnd[2] = nullptr;
    *c = fresh;
    void* players = nullptr;
    if (gMp.kind == kMpProps)
        if (MapPageCopies* all = mapPageCopies(false); all && mapPageLoad(kMpPlayers, all->kinds[kMpPlayers])) players = all->kinds[kMpPlayers].data.get();
    mapPageSetGlobals(gMp.kind, c->data.get(), players);
    mapPageMakeContent();
    if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
}

void mapPageReloadIfStale() {
    MapPageCopy* c = gMp.ready ? mapPageOpenCopy() : nullptr;
    if (!c || !app.ed || gMp.ed != app.ed.get()) return;
    // Set at once: the map changed elsewhere (an undo) when the copy's revision is old.
    if (gMp.typing) return;  // a text being typed goes in first
    if (gMp.kind != kMpProps) {  // a change the page did not report yet (not while a text has the focus: its typing)
        wchar_t cls[16] = L"";
        GetClassNameW(GetFocus(), cls, 16);
        if (_wcsicmp(cls, WC_EDITW) != 0 || !IsChild(gMp.wnd, GetFocus())) mapPageCommit();
    }
    if (c->revision != app.ed->revision() && !c->changed) mapPageReload();
}

// Map properties: what changed goes into the map now (each an undo step): the title and the description, players and
// forces. The size and the tileset wait for their button (the copy keeps them: the rail's dot).
void mapPageAutoApply() {
    if (!gMp.ready || gMp.kind != kMpProps || !app.ed) return;
    MapPageCopies* all = mapPageCopies(false);
    if (!all) return;
    mapPageReadBack();
    MapPageCopy& pc = all->kinds[kMpProps];
    MapPageCopy& pl = all->kinds[kMpPlayers];
    if (gMapProps && mapPropsTextChanged(*gMapProps)) mapPropsApplyText(*gMapProps);
    if (gPlayersDlg && playersChanged(*gPlayersDlg)) {
        playersApply(*gPlayersDlg);
        gPlayersDlg->basePlayers = gPlayersDlg->players, gPlayersDlg->baseForces = gPlayersDlg->forces;
    }
    pc.revision = pl.revision = app.ed->revision();  // the copies have the map's values (no reload)
    pl.changed = false;
    const bool terrain = gMapProps && mapPropsChanged(*gMapProps);
    if (terrain != pc.changed) {
        pc.changed = terrain;
        if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
    }
}

// Ctrl+P (Players and forces): the Map properties page, scrolled to them.
void mapPageShowPlayers() {
    if (gMp.wnd && gMp.kind == kMpProps) mapPageScrollTo(0, mapPagePartTop(1));
}

// Map properties: the size and tileset (their button), as one undo step; then the page shows the map again.
void mapPageApplyTerrain() {
    if (!gMp.ready || gMp.kind != kMpProps || !gMapProps) return;
    mapPageStopTyping(), gMp.typing = false;
    mapPageAutoApply();
    if (!mapPropsApplyTerrain(*gMapProps)) return;
    if (gMp.kind == kMpProps) mapPageReload();
}

// Applies one copy of the active tab (the page need not show). False: the map did not take it (the copy stays).
bool mapPageApplyCopy(int kind, MapPageCopy& c) {
    switch (kind) {
    case kMpProps: return mapPropsApply(*static_cast<MapPropsDialog*>(c.data.get()));
    case kMpPlayers: return playersApply(*static_cast<PlayersDialog*>(c.data.get()));
    case kMpClassicTrig: return ctApply(*static_cast<ClassicTrigDialog*>(c.data.get()));
    case kMpClassicSettings: return classicApply(*static_cast<ClassicDialog*>(c.data.get()));
    }
    return false;
}

// The copy's values are the map's now (after they went into it): what the copy compares with for a change.
void mapPageRebase(int kind, MapPageCopy& c) {
    switch (kind) {
    case kMpPlayers: {
        auto& d = *static_cast<PlayersDialog*>(c.data.get());
        d.basePlayers = d.players, d.baseForces = d.forces;
        break;
    }
    case kMpClassicTrig: {
        auto& d = *static_cast<ClassicTrigDialog*>(c.data.get());
        d.before = d.trig.triggers;
        break;
    }
    case kMpClassicSettings: {
        auto& d = *static_cast<ClassicDialog*>(c.data.get());
        d.namesBefore = d.names, d.unitsBefore = d.units, d.upgradesBefore = d.upgrades, d.techsBefore = d.techs;
        break;
    }
    }
}

// The page's change into the map now (one undo step), without reading the page again (it keeps its place and focus).
void mapPageCommit() {
    MapPageCopy* c = gMp.ready ? mapPageOpenCopy() : nullptr;
    if (!c || !app.ed || gMp.kind == kMpProps) return;
    mapPageReadBack();
    if (!mapPageCopyChanged(gMp.kind, *c)) return;
    // The map changed elsewhere (an undo) after the copy's last commit: the copy is old. The page shows the map again
    // (the change made since is lost, so that an old copy never writes over the undo).
    if (c->revision != app.ed->revision()) return mapPageReload();
    if (mapPageApplyCopy(gMp.kind, *c)) mapPageRebase(gMp.kind, *c), c->revision = app.ed->revision();
    const bool now = mapPageCopyChanged(gMp.kind, *c);
    if (now != c->changed) {
        c->changed = now;
        if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
    }
}

// A text being typed goes into the map now (before a save, a build, or the map closes).
void mapPageFlush() {
    if (!gMp.ready || gMp.ed != app.ed.get()) return;
    mapPageStopTyping(), gMp.typing = false;
    if (gMp.kind == kMpProps) mapPageAutoApply();
    else mapPageCommit();
}

// Apply (the bar's button): the copy into the map as one undo step, then the map's values again.
void mapPageApply() {
    MapPageCopy* c = gMp.ready ? mapPageOpenCopy() : nullptr;
    if (!c) return;
    mapPageChanged();
    if (!c->changed) return;
    const int kind = gMp.kind;
    if (!mapPageApplyCopy(kind, *c)) return mapPageChanged();
    // Applying the size can lay out the window again; the page is still this one.
    if (gMp.kind == kind && mapPageOpenCopy() == c) mapPageReload();
}

// Revert (the bar's button): the changes go; the map's values again.
void mapPageRevert() {
    if (gMp.ready) mapPageReload();
}

// Leaving the map (closing its tab, or StarDraft) with changes that are not applied: apply them, or not, or stay.
bool mapPagesConfirm() {
    if (gMp.wnd && gMp.ed == app.ed.get()) mapPageFlush();
    MapPageCopies* c = mapPageCopies(false);
    if (!c) return true;
    std::wstring names;
    for (int k = 0; k < kMpCount; ++k)
        if (c->kinds[k].data && c->kinds[k].changed) names += (names.empty() ? L"" : L", ") + std::wstring(kMpRailNames[k]);
    if (names.empty()) return true;
    const std::wstring q = L"These pages have changes that are not applied: " + names + L".\n\nApply them to the map?";
    const int r = shellAskUnapplied(q, L"Changes that are not applied");  // 0: Apply, 1: Do not apply, 2: Cancel
    if (r == 2) return false;
    for (int k = 0; k < kMpCount; ++k) {
        MapPageCopy& one = c->kinds[k];
        if (!one.data || !one.changed) continue;
        if (r == 0 && !mapPageApplyCopy(k, one)) return false;  // the map did not take it: stay
        if (gMp.wnd && gMp.kind == k && gMp.ed == app.ed.get()) mapPageReload();  // the page shows the map's values
        else one = MapPageCopy{};
    }
    if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
    return true;
}

// The page's place: the main area (as the workspaces have it).
void mapPageLayout(int x, int y, int w, int h) {
    if (!gMp.wnd) return;
    MoveWindow(gMp.wnd, x, y, w, h, TRUE);
    const int top = mapPageHeaderH(), bottom = std::max(top + 1, h - mapPageFooterH());
    MoveWindow(gMp.view, 0, top, w, bottom - top, TRUE);
    mapPageScrollTo(mapPageScrollX(), mapPageScrollY());
    InvalidateRect(gMp.wnd, nullptr, FALSE);
}

// A theme change: the content again in the theme's colors (the copy keeps what the fields had).
void mapPageThemeChanged() {
    if (!gMp.ready) return;
    mapPageReadBack();
    mapPageMakeContent();
}

// The host's timers: the check that the map changed from outside (300 ms, again and again) and the typing's stop (500 ms
// after the last key; each key starts it again).
stardraft::draw::Host* mapPageHost() { return gMp.frameView ? gMp.frameView->host() : nullptr; }
void mapPageArmReload() {
    if (stardraft::draw::Host* h = mapPageHost()) gMp.reloadTimer = h->timer(300, [] { mapPageReloadIfStale(), mapPageArmReload(); });
}
void mapPageStartTyping() {
    stardraft::draw::Host* h = mapPageHost();
    if (!h) return;
    if (gMp.typingTimer) h->cancelTimer(gMp.typingTimer);
    gMp.typingTimer = h->timer(500, [] {
        gMp.typingTimer = 0, gMp.typing = false;
        gMp.kind == kMpProps ? mapPageAutoApply() : mapPageCommit();
    });
}
void mapPageStopTyping() {
    if (stardraft::draw::Host* h = mapPageHost(); h && gMp.typingTimer) h->cancelTimer(gMp.typingTimer);
    gMp.typingTimer = 0;
}

// The program's part of the page frame (core/views_map_pages.inc).
std::wstring MapPagesProgram::title() { return gMp.kind >= 0 ? kMpTitles[gMp.kind] : L""; }
std::vector<MapPagesHost::Tab> MapPagesProgram::tabs() {
    std::vector<Tab> out;
    if (gMp.kind == kMpClassicSettings)
        for (int k = 0; k < kClassicTabCount; ++k) out.push_back({kClassicTabNames[k], kClassicTabIcons[k]});
    return out;
}
int MapPagesProgram::tab() { return gClassic ? gClassic->tab : 0; }
void MapPagesProgram::showTab(int k) { mapPageShowTab(k); }
bool MapPagesProgram::changed() {
    const MapPageCopy* c = mapPageOpenCopy();
    return c && c->changed;
}
int MapPagesProgram::footerH() { return 0; }  // (no bar: see mapPageFooterH)
void MapPagesProgram::apply() { mapPageApply(); }
void MapPagesProgram::revert() { mapPageRevert(); }
stardraft::draw::Point MapPagesProgram::contentSize() {
    const SIZE s = mapPageContentSize();
    return {int(s.cx), int(s.cy)};
}
// Classic triggers fill the view's width and height (its lists take the height that is left); smaller than a minimum, the
// view scrolls.
void MapPagesProgram::fitContent(int w, int h) {
    if (gMp.kind == kMpClassicTrig && gMp.content[0])
        SetWindowPos(gMp.content[0], nullptr, 0, 0, std::max<int>(w, scale(1200)), std::max<int>(h, scale(440)), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
void MapPagesProgram::placeContent(int x, int y) {
    for (int k = 0; k < kClassicTabCount; ++k)
        if (HWND c = gMp.content[k]) SetWindowPos(c, nullptr, -x, mapPagePartTop(k) - y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Tests: the page that shows; Apply (or Revert) clicked through the click area the bar's paint made.
HWND mapPageWindow() { return gMp.wnd; }
bool mapPageClickButton(bool apply) {
    if (gMp.ready) {  // no bar: changes go in at once; "Revert" shows the map's values again
        mapPageStopTyping(), gMp.typing = false;
        if (gMp.kind == kMpProps && apply) mapPageAutoApply(), mapPageApplyTerrain();
        else if (apply) mapPageCommit();
        else mapPageReload();
        return true;
    }
    if (!gMp.wnd) return false;
    RedrawWindow(gMp.wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    return gMp.frameView && gMp.frameView->clickBar(apply);
}

// Datashot mappages: each map page of the tab's mode in the main area (mappage_<page>[_light].png), and through the
// page's fields and the bar's buttons: a change turns on Apply and the rail's dot; leaving the page keeps the change
// and the map as it was; back on the page the change shows (mappage_<page>_changed[_light].png); Revert gives the
// map's values back; Apply sets the change as one undo step; after one undo the page reads the map again.
// mappages.txt says what holds.
void mapPagesDatashot(const fs::path& dir) {
    std::ofstream log(dir / "mappages.txt", std::ios::app);
    if (!app.ed || !app.ed->isOpen()) return;
    const std::string light = gLightTheme ? "_light" : "";
    auto yes = [](bool b) { return b ? "yes" : "NO"; };
    auto settle = [] {
        RedrawWindow(app.main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        for (int i = 0; i < 15; ++i) pump(), Sleep(30);  // the new controls reach the compositor
    };
    const char* const names[kMpCount] = {"props", "players", "classic_triggers", "classic_settings"};
    {  // Map properties with players and forces: each change goes into the map at once; the size waits for its button.
        command(ID_WS_MAP);
        const uint64_t revBefore = app.ed->revision();
        const auto t0 = std::chrono::steady_clock::now();
        command(ID_SCEN_PROPS);
        const double openMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        settle();
        for (int i = 0; i < 20; ++i) pump(), Sleep(30), mapPageReloadIfStale();  // its timer (a translated name must not go in)
        const bool unchanged = app.ed->revision() == revBefore;
        const bool open = gMp.wnd && gMp.kind == kMpProps && gMapProps && gPlayersDlg && gMp.content[1];
        capture(dir / ("mappage_props" + light + ".png"), app.main);
        bool title = false, titleUndo = false, player = false, sizeWaits = false, sizeSet = false, ctrlP = false;
        if (open) {
            const std::string before = app.ed->scenarioName();
            SetWindowTextW(gameTextFocusBox(gMapProps->name), L"Changed by the test");  // (the focus is not in it: at once)
            title = app.ed->scenarioName() == "Changed by the test";
            app.ed->undo();
            mapPageReloadIfStale();
            titleUndo = app.ed->scenarioName() == before && gMapProps && gameTextGet(gMapProps->name) == before;
            int cur = 0;  // the next controller for player 2, as its picker sets it
            for (size_t i = 0; i < std::size(kControllers); ++i)
                if (kControllers[i].first == gPlayersDlg->players[1].controller) cur = int(i);
            const int next = (cur + 1) % int(std::size(kControllers));
            gPlayersDlg->players[1].controller = kControllers[next].first;
            playersSet(*gPlayersDlg);
            player = app.ed->player(1).controller == kControllers[next].first;
            app.ed->undo();
            mapPageReloadIfStale();
            const int w = app.ed->width(), other = w == 128 ? 0 : 2;  // 64 or 128
            gMapProps->w = kMapSizes[other];  // as the Width picker sets it
            mapPropsInfo(*gMapProps), mapPageChanged();
            sizeWaits = app.ed->width() == w && mapPageHasChanges(kMpProps) && !gMapProps->infoText.empty();
            capture(dir / ("mappage_props_size" + light + ".png"), app.main);
            {  // the size and the tileset: the longest text, the players part moved down under it
                const int era = gMapProps->era;
                gMapProps->era = (era + 1) % 8;
                mapPropsInfo(*gMapProps), mapPageChanged();
                for (int i = 0; i < 6; ++i) pump();
                capture(dir / ("mappage_props_tileset" + light + ".png"), app.main);
                gMapProps->era = era;
                mapPropsInfo(*gMapProps), mapPageChanged();
                for (int i = 0; i < 6; ++i) pump();
            }
            mapPageApplyTerrain();  // Change the size and tileset
            sizeSet = app.ed->width() != w && !mapPageHasChanges(kMpProps);
            app.ed->undo();
            mapPageReloadIfStale();
            command(ID_SCEN_PLAYERS);
            ctrlP = gMp.kind == kMpProps && mapPageScrollY() > 0;
        }
        log << "props: one page with players and forces " << yes(open) << " (opened in " << int(openMs) << " ms), the title goes into the map at once "
            << yes(title) << ", undo gives it back on the page " << yes(titleUndo) << ", a player's change at once " << yes(player)
            << ", the size waits for its button (the dot) " << yes(sizeWaits) << ", the button sets it " << yes(sizeSet)
            << ", Players and forces scrolls to them " << yes(ctrlP) << ", opening it leaves the map as it was " << yes(unchanged) << "\n";
        command(ID_WS_MAP);
    }
    for (int k = kMpClassicTrig; k < kMpCount; ++k) {
        const int id = mapPageCommand(k);
        if (!commandInMode(id)) {
            log << names[k] << ": not in this tab's mode\n";
            continue;
        }
        command(id);
        settle();
        const bool open = gMp.wnd && gMp.kind == k && IsWindowVisible(gMp.wnd);
        RedrawWindow(gWsBar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        bool railOn = false;
        for (const auto& h : gWsHits) railOn |= h.id == id;
        railOn &= mapPageCommandOf(gWorkspace) == id;
        capture(dir / ("mappage_" + std::string(names[k]) + light + ".png"), app.main);
        log << names[k] << ": the page shows in the main area " << yes(open) << ", its rail item on " << yes(railOn) << "\n";
        if (!open) continue;
        const uint64_t rev = app.ed->revision();
        // One change through the page's fields, and where it shows.
        int target = 0;  // players: the controller of player 2; classic settings: the marine's "Use default"
        const size_t trigCount = gCt ? gCt->trig.triggers.size() : 0;
        auto change = [&] {
            if (k == kMpClassicTrig) {
                ctAddTrigger();
            } else {
                mapPageShowTab(kClassicUnits);
                ClassicPageView& pg = *gMp.classic[kClassicUnits];
                pg.select(0);  // the marine
                target = !classicUseDefault(kClassicUnits, pg.entry());
                pg.setUseDefault(target);
            }
            mapPageChanged();
        };
        auto shows = [&] {
            if (k == kMpClassicTrig) return gCt && gCt->trig.triggers.size() == trigCount + 1;
            return gClassic && int(gClassic->units.units[0].useDefault) == target;
        };
        auto inMap = [&] {
            if (k == kMpClassicTrig) return app.ed->classicTriggers().triggers.size() == trigCount + 1;
            return int(app.ed->unitSettings().units[0].useDefault) == target;
        };
        change();
        const bool atOnce = !mapPageHasChanges(k) && shows() && inMap() && app.ed->revision() != rev;
        command(ID_WS_MAP);  // leave the page, and back: the change is the map's
        command(id);
        settle();
        const bool back = shows() && inMap();
        capture(dir / ("mappage_" + std::string(names[k]) + "_changed" + light + ".png"), app.main);
        app.ed->undo();
        mapPageReloadIfStale();  // as the page's timer does
        const bool undone = !inMap() && !shows() && !mapPageHasChanges(k);
        log << names[k] << ": a change goes into the map at once (no Apply) " << yes(atOnce) << ", there again on the page " << yes(back)
            << ", one undo gives the map's value back and the page shows it " << yes(undone) << "\n";
        if (k == kMpClassicSettings) {  // the unit's rename button (#91): the name is the game text pop-out's; the page shows it
            mapPageShowTab(kClassicUnits);
            ClassicPageView& pg = *gMp.classic[kClassicUnits];
            pg.select(3);  // the goliath
            const bool button = pg.renameButton().visible();
            capture(dir / ("mappage_rename" + light + ".png"), app.main);
            pg.setName("Walker");  // what the pop-out sets (the title shows it)
            settle();
            const bool set = app.ed->unitSettingName(3) == "Walker" && pg.title().find(L"Walker") != std::wstring::npos;
            capture(dir / ("mappage_renamed" + light + ".png"), app.main);
            app.ed->undo();
            mapPageReloadIfStale();
            log << names[k] << ": the rename button " << yes(button) << ", a name sets the map's name and the title shows it " << yes(set) << "\n";
        }
        command(ID_WS_MAP);
    }
}

// The main loop: keys while a control of the page has the focus. Tab moves between the fields; the shortcuts that
// are not the page's own (Ctrl+C, Ctrl+V and the arrow keys are) still work.
bool mapPageHasFocus() {
    const HWND f = GetFocus();
    return gMp.wnd && f && (f == gMp.wnd || IsChild(gMp.wnd, f));
}
// A force name (a kit field of the players view) has the keyboard: the typed keys and Ctrl+Z are the field's.
bool mapPageTextFocus() {
    if (gMp.kind == kMpClassicSettings && gClassic && gMp.classic[gClassic->tab]) return gMp.classic[gClassic->tab]->typingField();
    return gMp.players && gMp.players->typingName();
}
// Tab from a force name: the next (back: the previous) name; false: the Tab goes on to the page's next control.
bool mapPageTabNames(bool back) { return gMp.players && gMp.players->tabName(back); }
bool mapPageKeepsKey(const MSG& m) {
    if (m.message != WM_KEYDOWN && m.message != WM_SYSKEYDOWN) return false;
    if (!(GetKeyState(VK_CONTROL) & 0x8000)) return m.message == WM_KEYDOWN && (m.wParam < VK_F1 || m.wParam > VK_F24);
    switch (m.wParam) {
    case 'A': case 'C': case 'V': case 'X':
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_HOME: case VK_END: case VK_DELETE: case VK_INSERT: return true;
    }
    return false;
}
