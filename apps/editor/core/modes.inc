// The drags on the map, the tools of each mode and the layers: portable (core.inc), moved from main.cpp as they were.

const wchar_t* kModeNames[] = {L"Terrain", L"Units", L"Doodads", L"Locations", L"Sprites", L"Map layers"};

enum class Drag { None, Pan, AutoPan, PlaceStroke, MoveUnits, BoxSelect, PendingPlace, Paint, Spray, TerrainSelect, LocCreate, LocMove, LocResize,
                  MoveDoodad, MoveSprite, Tiles, PasteStroke, Shape, Jumble, Erase, SymmetryCenter, LayerTool, TerrainMove, TerrainLasso };  // Erase: Units, Doodads, Sprites, Locations  // Shape: a Line or Contour press (core/terrain_shapes.inc)

// Tools per mode (index into these lists).
const wchar_t* kTerrainTools[] = {L"Brush", L"Spray", L"Fill", L"Replace (click terrain)", L"Select", L"Line", L"Contour", L"Polygon", L"Jumble", L"Erase", L"Rectangle", L"Filled rectangle", L"Ellipse", L"Filled ellipse", L"Diamond", L"Filled diamond", L"Bridge", L"Ramp", L"Magic wand", L"Move", L"Lasso"};
const wchar_t* kLayersTools[] = {L"Move layer"};
const wchar_t* kUnitTools[] = {L"Place / select", L"Spray", L"Replace (click unit)", L"Fill", L"Erase"};
const wchar_t* kDoodadTools[] = {L"Place", L"Spray", L"Fill", L"Erase"};
enum TerrainTool { TBrush, TSpray, TFill, TReplace, TSelect, TLine, TContour, TPolygon, TJumble, TErase, TRect, TRectFill, TEllipse, TEllipseFill, TDiamond, TDiamondFill, TBridge, TRamp, TWand, TMoveSel, TLasso };
enum LayersTool { LayersMove };  // Map Layers (core/map_layers.inc)  // (Line to Polygon, Bridge, Ramp: core/terrain_shapes.inc)
// Rectangle, Ellipse and Diamond, outline or filled: a drag from one corner to the other (core/terrain_shapes.inc).
inline bool isBoxTool(int tool) { return tool == TRect || tool == TRectFill || tool == TEllipse || tool == TEllipseFill || tool == TDiamond || tool == TDiamondFill; }
// The shapes' outlines that the brush paints (Line, Rectangle, Ellipse, Diamond); the others fill.
inline bool isStrokeShape(int tool) { return tool == TLine || tool == TRect || tool == TEllipse || tool == TDiamond; }
// Bridge: a drag from the ground across water to the ground (src/bridges.cpp), as Line with the angle locked to the
// map's diagonals. Ramp: a stroke along a cliff edge (src/ramps.cpp), as Contour's.
// The Terrain tools that change the terrain there without the list's pick (it stays picked, faded in the list):
// Bridge and Ramp (their kits' tiles), Jumble (the terrains under the brush).
inline bool terrainToolIgnoresPick(int tool) { return tool == TBridge || tool == TRamp || tool == TJumble || tool == TWand || tool == TMoveSel || tool == TLasso; }
inline bool isShapeTool(int tool) { return tool == TLine || tool == TContour || tool == TPolygon || isBoxTool(tool) || tool == TBridge || tool == TRamp; }
// The Terrain tool strip's choices (core/views_toolstrip.inc: terrainKind): 0 Isometric, 1 Tile, 2 Custom, 3 Rectangle,
// and their order in the strip and Mode > Terrain tools: Isometric, Rectangle, Tile, Custom.
constexpr int kTerrainKinds = 4;
constexpr int kTerrainKindOrder[kTerrainKinds] = {0, 3, 1, 2};
int terrainKind();
void setTerrainKind(int kind);
int nextTerrainKind(int kind);
enum UnitTool { UPlace, USpray, UReplace, UFill, UErase };
enum DoodadTool { DPlace, DSpray, DFill, DErase };
enum SpriteTool { SPlace, SSpray, SErase };
enum LocationTool { LSelect, LErase };
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
// View > Modes (GitHub feedback #117): the map's layers that show. Locations: app.showLocations.
struct MapLayers {
    bool terrain = true, doodads = true, sprites = true, units = true, revealers = true, creep = true;
    bool protectedTerrain = true;  // View > Modes > Protected terrain (core/protected_layer.inc)
} gLayers;
