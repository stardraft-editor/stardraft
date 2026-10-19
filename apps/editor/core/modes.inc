// The drags on the map, the tools of each mode and the layers: portable (core.inc), moved from main.cpp as they were.

const wchar_t* kModeNames[] = {L"Terrain", L"Units", L"Doodads", L"Locations", L"Sprites"};

enum class Drag { None, Pan, AutoPan, PlaceStroke, MoveUnits, BoxSelect, PendingPlace, Paint, Spray, TerrainSelect, LocCreate, LocMove, LocResize,
                  MoveDoodad, MoveSprite, Tiles, PasteStroke };

// Tools per mode (index into these lists).
const wchar_t* kTerrainTools[] = {L"Brush", L"Spray", L"Fill", L"Replace (click terrain)", L"Select / copy"};
const wchar_t* kUnitTools[] = {L"Place / select", L"Spray", L"Replace (click unit)"};
const wchar_t* kDoodadTools[] = {L"Place", L"Spray"};
enum TerrainTool { TBrush, TSpray, TFill, TReplace, TSelect };
enum UnitTool { UPlace, USpray, UReplace };
enum DoodadTool { DPlace, DSpray };
enum SpriteTool { SPlace, SSpray };
bool gShowRanges = false;  // View > Show ranges of the selected units
// View > Layers (GitHub feedback #117): the map's layers that show. Locations: app.showLocations.
struct MapLayers {
    bool terrain = true, doodads = true, sprites = true, units = true, revealers = true;
} gLayers;
