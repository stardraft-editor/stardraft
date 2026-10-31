// The drags on the map, the tools of each mode and the layers: portable (core.inc), moved from main.cpp as they were.

const wchar_t* kModeNames[] = {L"Terrain", L"Units", L"Doodads", L"Locations", L"Sprites"};

enum class Drag { None, Pan, AutoPan, PlaceStroke, MoveUnits, BoxSelect, PendingPlace, Paint, Spray, TerrainSelect, LocCreate, LocMove, LocResize,
                  MoveDoodad, MoveSprite, Tiles, PasteStroke };

// Tools per mode (index into these lists).
const wchar_t* kTerrainTools[] = {L"Brush", L"Spray", L"Fill", L"Replace (click terrain)", L"Select / copy"};
const wchar_t* kUnitTools[] = {L"Place / select", L"Spray", L"Replace (click unit)"};
const wchar_t* kDoodadTools[] = {L"Place", L"Spray"};
enum TerrainTool { TBrush, TSpray, TFill, TReplace, TSelect };
// The Terrain tool strip's choices (core/views_toolstrip.inc: terrainKind): 0 Isometric, 1 Tile, 2 Custom, 3 Rectangle,
// and their order in the strip and Mode > Terrain tools: Isometric, Rectangle, Tile, Custom.
constexpr int kTerrainKinds = 4;
constexpr int kTerrainKindOrder[kTerrainKinds] = {0, 3, 1, 2};
int terrainKind();
void setTerrainKind(int kind);
int nextTerrainKind(int kind);
enum UnitTool { UPlace, USpray, UReplace };
enum DoodadTool { DPlace, DSpray };
enum SpriteTool { SPlace, SSpray };
// View > Ranges (and the Units mode's right-click menu): the ranges of the selected units that show, a bit each in the
// order of kRangeKinds; 0: none.
int gRangeKinds = 0;
struct RangeKind {
    const wchar_t* menu;   // its item
    const wchar_t* named;  // its name and color in the message
};
constexpr RangeKind kRangeKinds[] = {{L"&Sight", L"sight (yellow)"}, {L"&Target acquisition", L"target acquisition (orange)"},
                                     {L"&Ground weapon", L"ground weapon (red)"}, {L"&Air weapon", L"air weapon (blue)"},
                                     {L"&Detection", L"detection (green)"}, {L"&Psi field", L"psi field (purple)"}};
constexpr int kRangeKindCount = int(sizeof kRangeKinds / sizeof kRangeKinds[0]), kRangeAll = (1 << kRangeKindCount) - 1;
bool rangeShown(int kind) { return (gRangeKinds >> kind) & 1; }
// View > Layers (GitHub feedback #117): the map's layers that show. Locations: app.showLocations.
struct MapLayers {
    bool terrain = true, doodads = true, sprites = true, units = true, revealers = true, creep = true;
    bool protectedTerrain = true;  // View > Layers > Protected terrain (core/protected_layer.inc)
} gLayers;
