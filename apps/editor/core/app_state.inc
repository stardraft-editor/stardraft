// The editor's state (what struct App held apart from its windows): portable (core.inc).
struct AppState {
    std::wstring statusText[6];
    std::vector<uint32_t> megatileColor;  // average color per megatile (0x00RRGGBB)
    const Tileset* megatileColorsFor = nullptr;
    int megatileColorsEra = -1;  // the tileset object is loaded again in place when the map's tileset changes
    std::vector<uint32_t> miniTerrain;    // one pixel per map tile
    uint64_t miniTerrainRev = 0;
    RECT miniInvalidView{};  // what renderNow last invalidated the minimap for
    uint64_t miniInvalidRev = 0, miniInvalidTerrainRev = 0;
    UINT dpi = 96;

    GameData data;
    std::unique_ptr<Editor> ed;

    Mode mode = Mode::Units;
    std::vector<int> items;  // palette entries for the current mode; -1 = "(Select)"
    int palette = -1;        // selected palette entry value
    int palette2 = -1;       // isometric terrain: the second terrain (Shift+click or Ctrl+click in the palette), painted with Shift+click or Shift+drag
    int playerOwner = 0;
    float brushSize = 2;  // diamonds, whole numbers 1 to 32
    float brushHeight = 0;  // terrain: the brush's height, 1 to 32; 0: the same as its size (GitHub #113)
    // Terrain: the brush size and height of the other terrain tools (Rectangle's while Isometric is on, and the
    // reverse). Each keeps its own (GitHub #155).
    float otherBrushSize = 2, otherBrushHeight = 0;
    Brush::Shape brushShape = Brush::Diamond;
    Brush::Shape selectShape = Brush::Square;
    bool terrainSelFlip = false;  // the diamond selection's drag went between the box's top right and bottom left (IsoRectFlip)  // the terrain Select tool's shape: its own, not the brushes' (shapeOf)
    int toolByMode[kModeCount] = {};
    int sprayDensity = 10;  // the active panel's: how many things one spray puts down (kDensityMax at most)
    uint32_t spraySeed = 1;
    int lastSprayX = -100000, lastSprayY = -100000;
    double sprayHeld = 0, sprayCredit = 0;  // a held spray: the seconds it is held, and the sprays due
    int sprayPlaced = 0;                    // the units or doodads that the press and hold placed
    bool terrainRect = false;  // the Terrain tool strip's Rectangle: the tools work on tiles as they are (tiles_view.inc)
    bool hasTerrainSel = false;
    Rect terrainSel;
    // Rectangle and Stamps: a selection of tiles that need not be one rectangle (Shift adds tiles, Ctrl takes them out):
    // which tiles of terrainSel (its box, in tiles, row by row) are in it. Empty: the tiles of terrainSel's shape.
    std::vector<uint8_t> terrainSelMask;
    int terrainSelOp = 0;  // the selection's drag under way: 0 a new selection, 1 adds its tiles (Shift), 2 takes them out (Ctrl)
    bool terrainPasting = false;
    bool copyUnitsWithTerrain = true, copyDoodadsWithTerrain = true;
    bool locationWrap = false;  // a location's name in lines that fit in it (Options > Locations)
    bool allowIllegalUnits = false;  // Options: units anywhere; terrain edits keep them
    std::vector<int> selDoodads, selSprites;  // the selected doodads and sprites (indexes into the Editor's lists)
    // Each mode's own copy buffer (units and terrain are the Editor's): Ctrl+C, Ctrl+X and Ctrl+V act on the active mode's.
    // Doodads and sprites: the copied ones, each from the middle of the group; a paste follows the mouse.
    std::vector<Editor::ItemAt> doodadClip, spriteClip;
    bool pastingItems = false;                 // Doodads or Sprites: the copy follows the mouse; a click places it
    // The copied locations: each one's edges, inverted axes (1 left/right, 2 top/bottom), name and the elevations it
    // leaves out (MRGN flags).
    struct LocationCopy {
        Rect rect{};
        int inverted = 0;
        std::string name;
        uint16_t elevation = 0;
    };
    std::vector<LocationCopy> locationClip;
    bool allowStacking = false;     // Options: units may overlap
    bool resourceDistance = true;   // Options: town halls and resources keep 3 tiles apart
    bool snapBuildings = true;      // Options > Units: buildings snap to tile edges (on every path; other units: unitGrid)
    bool animate = true;             // Options: units play their idle animations
    bool dragSnapBack = true;        // Options: dragged units go back to the last position where they fit
    bool randomFacing = true;        // Options > Units: units with a random units.dat facing each face their own way
    float lastValidDX = 0, lastValidDY = 0;  // offset of that spot from where the drag started
    bool correction = false, doodadCheck = true, showLocations = false;  // units: Place only where it fits (the default)
    bool doodadNearest = false;  // Options > Doodads > Move to the nearest place where it fits (with doodadCheck)
    bool showGrid = false;
    // Options > Units, Sprites, Locations: the snap grid in px (0: no snap; 32 a tile, 16 a half tile, 8 a quarter tile).
    int unitGrid = 0, spriteGrid = 0, locationGrid = 32;
    bool carbot = false;  // Cartooned (Carbot) graphics
    float lastPlaceX = 0, lastPlaceY = 0;  // where the last unit of a placing drag went (cursor)
    int placedInStroke = 0;
    RECT lastPlacedBox{};  // doodads and sprites in a drag: the box of the last one placed (the next goes beside it)  // 32x32 tile grid; units on tile centers, moved in whole tiles
    GraphicsQuality quality = GraphicsQuality::HD2;

    float camX = 0, camY = 0, zoom = 1;
    int mouseX = 0, mouseY = 0;  // viewport pixels
    bool mouseIn = false;
    Drag drag = Drag::None;
    float dragWX = 0, dragWY = 0;  // world point where the drag started
    int dragSX = 0, dragSY = 0;    // screen point where the drag started
    bool dragMoved = false;
    int locEdges = 0;  // 1 left, 2 top, 4 right, 8 bottom
    int locToggle = -1;  // Ctrl or Shift pressed on this location: a click takes it in or out of the selection
    bool pasting = false;  // paste mode: the clipboard follows the cursor until a click places it
    Rect locStart;
    bool dirty = true;
    uint64_t terrainRev = 0, editorRev = 0;
    std::wstring message;
    double frameMs = 0;
    std::deque<std::chrono::steady_clock::time_point> frameTimes;  // the frames of the last second (for fps)
};
