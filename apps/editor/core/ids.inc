// The command ids, the grid presets, the modes and the dock layout: portable (core.inc), moved from main.cpp as they
// were.

// --- Ids -----------------------------------------------------------------------------------------

enum : int {
    IDC_TABS = 100, IDC_OWNER, IDC_BRUSH, IDC_FILTER, IDC_LIST, IDC_STATUS, IDC_VIEW, IDC_TOOL, IDC_SHAPE, IDC_SYM, IDC_DENSITY, IDC_UNITSEARCH, IDC_PALVIEW, IDC_PALSECTION,
    ID_EDIT_TERRAINUNITS = 350, ID_EDIT_TERRAINDOODADS, ID_OPT_LOCWRAP,
    ID_FILE_OPEN = 200, ID_FILE_NEW, ID_FILE_SAVE, ID_FILE_SAVEAS, ID_FILE_EXIT, ID_FILE_CLOSE, ID_FILE_CLOSEALL, ID_TAB_NEXT,
    ID_TAB_PREV,
    ID_EDIT_UNDO = 300, ID_EDIT_REDO, ID_EDIT_CUT, ID_EDIT_COPY, ID_EDIT_PASTE, ID_EDIT_DELETE, ID_EDIT_SELECTALL,
    ID_EDIT_CORRECTION, ID_EDIT_DOODADCHECK, ID_EDIT_SNAPLOC, ID_EDIT_RENAME, ID_EDIT_SNAPUNITS, ID_EDIT_DUPLICATE,
    ID_EDIT_HISTORY,  // Edit > Undo history (core/undo_history.inc)
    ID_VIEW_HD = 400, ID_VIEW_HD2, ID_VIEW_SD, ID_VIEW_CARBOT, ID_VIEW_LOCATIONS, ID_VIEW_ZOOMIN, ID_VIEW_ZOOMOUT, ID_VIEW_ZOOM100, ID_VIEW_FIT, ID_VIEW_GRID,
    ID_VIEW_WALK, ID_VIEW_PATHS, ID_VIEW_TOWNS, ID_PATHSIZE_SMALL, ID_PATHSIZE_MEDIUM, ID_PATHSIZE_LARGE,  // the R24 overlays (path_overlay.inc)
    ID_MODE_TERRAIN = 500, ID_MODE_UNITS, ID_MODE_DOODADS, ID_MODE_LOCATIONS, ID_MODE_SPRITES,
    ID_PLAYER_1 = 600,  // + player index, keys 1-9
    ID_QUICKPICK = 660,
    ID_QUICKOPEN = 661,
    ID_CAMPAIGN_NEW = 663, ID_CAMPAIGN_OPEN, ID_CAMPAIGN_SHOW,  // File > Campaign (roadmap R25: core/views_campaign.inc)
    ID_OPT_CMBW_MODE = 671,  // Program options > Start > Cosmonarchy BW mode: every map a Cosmonarchy map (core/cmbw.inc cmbwMode)
    ID_OPT_CMBW = 670,  // Program options > Start > Show Cosmonarchy BW (the start page's New Cosmonarchy map: core/cmbw.inc)
    ID_TRIG_FIND = 662,  // Edit > Find in triggers (Ctrl+Shift+F): the Triggers workspace and the Classic triggers page (roadmap R60)  // File > Quick open (Ctrl+Shift+O): an open tab, a recent project or map
    ID_PROJ_NEW = 700, ID_PROJ_OPEN, ID_PROJ_COMPILE, ID_PROJ_FOLDER, ID_PROJ_ADDSCRIPT, ID_PROJ_SETTINGS, ID_PROJ_FROMMAP, ID_PROJ_IMPORT,
    ID_WS_MAP = 720, ID_WS_DATA, ID_WS_TRIGGERS, ID_WS_ASSETS, ID_WS_PROJECT, ID_WS_BRIEFING, ID_WS_FILES, ID_WS_HELP,
    ID_DOCK_MINI_LEFT = 740, ID_DOCK_MINI_RIGHT, ID_DOCK_TOOLS_LEFT, ID_DOCK_TOOLS_RIGHT,
    ID_SCEN_PLAYERS = 760, ID_SCEN_CHECK, ID_OPT_INSTALL, ID_VIEW_LOG, ID_VIEW_TRIGCODE, ID_VIEW_KIT, ID_SCEN_REPLAY,
    ID_RECENT_MAP = 800,       // + index, up to kRecentMax
    ID_RECENT_PROJECT = 820,   // + index
    ID_RECENT_CLEAR_MAPS = 840, ID_RECENT_CLEAR_PROJECTS,
    ID_HELP_FEEDBACK = 860, ID_HELP_ISSUE, ID_HELP_UPDATES, ID_OPT_UPDATECHECK, ID_HELP_DISCORD, ID_HELP_ABOUT, ID_OPT_CRASHREPORTS, ID_HELP_GITHUB,
    // Help > What is new (the release notes of this version), the top bar's Update (the newer release's notes), Program
    // options > Updates > Previews (pre-releases too).
    ID_HELP_WHATSNEW = 870, ID_UPDATE_NOTES, ID_OPT_PREVIEWS,
    // Help > Documentation (the help window at its index) and F1 (the help window at the page of what has the focus).
    ID_HELP_DOCS = 875, ID_HELP_CONTEXT,
    ID_THEME_DARK = 880, ID_THEME_LIGHT, ID_SCEN_PROPS, ID_SCEN_CLASSIC, ID_SCEN_CLASSICTRIG, ID_ACCENT = 890,  // + accent index (theme::kAccents)
    ID_THEME_SYSTEM = 888,  // Program options > Theme > Automatic: the system's theme (macOS)
    ID_OPT_SNAPVALID = 640, ID_OPT_STRICT, ID_OPT_ILLEGAL, ID_OPT_DRAGBACK, ID_OPT_ANIMATE, ID_OPT_STACK, ID_OPT_LASTPROJECT,
    ID_OPT_WINDOW,  // Options > Program options (the window with the program options)
    ID_OPT_SNAPBUILDINGS,  // Options > Units > Snap buildings to tile edges (a check item)
    ID_OPT_RESDIST,  // Options > Units > Keep resources 3 tiles from town halls (a check item; Discord feedback #97)
    // The Options menu: the snap of units, sprites and locations (+ the choice in kSnapGrids), the place rule of doodads,
    // and the items that show a rule that does not change (always grayed).
    ID_SNAP_UNITS = 910, ID_SNAP_SPRITES = 914, ID_SNAP_LOCATIONS = 918, ID_DOODAD_FIT = 922, ID_DOODAD_NEAREST, ID_DOODAD_ANYWHERE,
    ID_FIXED_DOODAD_SNAP, ID_FIXED_DOODAD_STACK, ID_FIXED_SPRITE_PLACE, ID_FIXED_SPRITE_STACK, ID_FIXED_LOC_PLACE, ID_FIXED_LOC_STACK,
    ID_PAL_LIST = 620, ID_PAL_ICONS, ID_PAL_SPRITES, ID_PAL_NORMAL, ID_PAL_LARGE,
    ID_PAL_SORT_CATEGORY = 625, ID_PAL_SORT_NAME, ID_PAL_SORT_NUMBER,  // the palette's View menu: its order (PaletteSort)
    ID_PAL_NEW_PANEL = 628, ID_PAL_CLOSE_PANEL,  // ... a palette panel like it (sdl_palette_panels.inc), a panel's Close
    ID_PAL_XLARGE = 630,  // ... Extra large (Discord feedback, GitHub #150)
    ID_GRID_SIZE = 950,  // + the preset (kGridPresets); View > Grid size
    ID_GRID_CUSTOM = 958,
    ID_OPT_LANG = 960,  // + the choice of Program options > Language: 0 automatic, 1 English, 2 Korean, 3 Spanish, 4 Russian, 5 Chinese
    ID_TOOL_KEY = 970,  // + the tool of the keys V, B, A, F: select, brush or place, spray, fill (GitHub feedback #98)
    ID_VIEW_RANGES = 975,  // View > Show ranges of the selected units (GitHub feedback #109)
    ID_VIEW_MINIMAP = 976,  // View > Show minimap (GitHub feedback #127)
    ID_VIEW_FLOATPAL = 1020,  // + int(Mode): View > Floating palettes (Roadmap R40: a mode's palette in a window of its own)
    ID_LAYER = 990,         // + View > Layers: 0 terrain, 1 doodads, 2 sprites, 3 units, 4 pathing; 5, 6 pathing shows the regions, the connected areas
    ID_DECORATE = 1040,  // + Edit > Decorate the terrain: 0 a few, 1 some, 2 many doodads (roadmap R37)
    ID_LAYER_CREEP = 989,  // View > Layers > Creep (the start creep of the map's Zerg buildings: core/creep_layer.inc)
    ID_LAYER_REVEALERS = 988,  // View > Layers > Map revealers (the map's Map Revealer units: drawn and picked, or not)
    ID_REVEAL_PLACE = 1050, ID_REVEAL_REMOVE,  // Tools > Place map revealers, Remove all map revealers (Discord feedback #125)
    ID_SIM_MINING = 1070, ID_SIM_COMBAT,  // Tools > Simulate mining, Simulate combat (experimental: STARDRAFT_SIMULATION)
    ID_VIEW_UNITLIST = 1060,  // View > Units list (core/units_list.inc: the list of the Units mode's units, in its side bar)
    ID_FOG = 1035,  // View > Overlays > Fog of war of the selected player (roadmap R32)
    ID_TERRAIN_FLAGS = 1030,  // + View > Overlays > Terrain flags: 0 walkable, 1 height, 2 blocks view, 3 buildable
    ID_SCEN_PATHCHECK = 997,  // Scenario > Check pathing (core/pathing_layer.inc)
    ID_RAIL_KEY = 1000,     // + n: Ctrl+1 to Ctrl+9, the n-th item of the side bar (the tab's mode: a classic map or a project)
    ID_CRASH_MODE = 980,   // + Options > Crash reports: 0 ask, 1 send, 2 do not send
    ID_MIDDLE_MODE = 984,  // + Options > Middle mouse click and hold: 0 pan the map (the default), 1 drag the map
    ID_OPT_MAPSDEFAULT = 986,  // Options > Maps: .scm and .scx open with StarDraft 2
    ID_OPT_RESTART = 987,      // Options > Language > Restart now: StarDraft 2 starts again in the chosen language
    ID_FILE_EXPORTIMAGE = 230,  // File > Export map image (core/map_image.inc)
    ID_DATA_TABLE = 1080,  // View > Data > Table view (Ctrl+E: the table, or the entry's form again; core/views_data_table.inc)
    ID_DATA_ENTRY = 1081,  // View > Data > Entry view: the form of the open entry
};
// View > Grid size: the sizes of the map grid (GitHub feedback #87), then Custom (width, height and offsets).
struct GridPreset {
    const wchar_t* name;
    int px;
};
constexpr GridPreset kGridPresets[] = {{L"Fine", 8}, {L"Small", 16}, {L"Normal", 32}, {L"Large", 64}, {L"Huge", 128}};

enum class Mode { Terrain, Units, Doodads, Locations, Sprites };
constexpr int kModeCount = 5;
// The modes in the order the mode bar shows them.
constexpr Mode kModeOrder[kModeCount] = {Mode::Terrain, Mode::Units, Mode::Doodads, Mode::Sprites, Mode::Locations};

// Where the map workspace's panels sit. The minimap is always at the top of its side; each layer's toolbar
// (Terrain / Units / Doodads / Locations: the layer buttons, its options, its palette) docks left or right
// on its own. Right-click the minimap or a toolbar to move it. Kept in HKCU\Software\StarDraft\Layout.
struct Dock {
    bool minimapRight = false;
    bool minimapHidden = false;  // View > Show minimap off: its side has the panel only (GitHub feedback #127)
    bool toolsRight[kModeCount] = {false, true, false, false, false};  // units on the right, the rest on the left
    int leftW = 280, rightW = 280, dataListW = 300;    // column widths, 96-dpi pixels; drag their edges
    bool triggerCode = false;                          // the Triggers workspace's code pane is shown
    bool scriptPreview = false;                        // the Project page's preview of the selected script is shown
    int triggerZoom = 100;                             // the trigger pane's size, percent (Ctrl+mouse wheel)
    int unitWinX = 0, unitWinY = 0, unitWinW = 0, unitWinH = 0;  // the Unit properties window: its place (screen points) and size (pixels); W 0: none kept
    int volume = 100;                                  // every sound StarDraft plays, percent (Program options, an SFX entry)
    int unitNames = 2;  // how lists name a unit with a custom name: 0 game, 1 game (custom), 2 custom (game), 3 custom
    int logH = 220;     // the open Log's height, 96-dpi pixels; drag its top edge
    int minimapH = 0;   // the minimap's height, 96-dpi pixels; drag its bottom edge (0: the map's shape, at most half the side)
    int gridW = 32, gridH = 32, gridX = 0, gridY = 0;  // the map grid: cell size and offset, map pixels (View > Grid size)
};
constexpr int kMinSideW = 200, kMaxSideW = 640;
Dock gDock;
RECT gDockRects[2] = {};  // the left and right columns, for right-clicks on their empty space
RECT gSplitters[2] = {};  // the strips between the columns and the map: drag to resize
int gSplitDrag = -1, gSplitGrab = 0;  // which strip is being dragged (0 left, 1 right), and where it was grabbed

// The registry name of each mode's tool bar side (Mode order). One entry per mode: a mode without one would read past
// the end (that crashed saveDock when Sprites came).
constexpr const wchar_t* kDockSideNames[] = {L"TerrainRight", L"UnitsRight", L"DoodadsRight", L"LocationsRight", L"SpritesRight"};
static_assert(std::size(kDockSideNames) == kModeCount, "a dock side name for each mode");
enum class Workspace { Map, Data, Triggers, Assets, Project, Briefing, Files, Help, Properties, Players, ClassicTriggers, ClassicSettings };
constexpr int kRecentMax = 10;
