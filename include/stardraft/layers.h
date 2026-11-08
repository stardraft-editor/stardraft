#pragma once

// Map layers (a prototype): the map's own terrain and objects are the Background, and the layers over it each have
// terrain (ISOM corners; a corner the layer does not have is NULL: see-through) and units, doodads and sprites. A layer
// has no size: its contents can be at any place, also outside of the map. The map's sections (what the game and other
// editors read) always are the flattened layers (Editor::composeLayers); the layers and the Background are two files in
// the map's archive, so that they are saved with the map and each undo step has them.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/doodads.h"
#include "stardraft/units.h"

namespace stardraft {

inline constexpr const char* kLayersFile = "stardraft\\layers.bin";
inline constexpr const char* kLayersBaseFile = "stardraft\\layers-base.bin";
inline constexpr const char* kLayersCheckFile = "stardraft\\layers-map.txt";  // what the map's terrain was at the last save
inline constexpr const char* kNotesFile = "stardraft\\notes.txt";          // the map's notes (UTF-8)
inline constexpr const char* kMapSettingsFile = "stardraft\\map.json";     // the map's own settings, as at its last save
inline constexpr const char* kExportListFile = "stardraft\\export.txt";    // lines "on <path>" and "off <path>": File > Export

// An object of a layer: x, y are its place, also outside of the map (the object's own x, y cannot be negative). shown:
// the last flattening put it on the map (Editor::composeLayers); an object that is not on the map stays in the layer.
template <class T>
struct LayerObject {
    T item{};
    int x = 0, y = 0;
    bool shown = false;
};

struct MapLayer {
    uint32_t id = 0;
    std::string name;
    bool visible = true;
    bool locked = false;  // its contents do not change
    std::map<std::pair<int, int>, uint16_t> corners;  // (rect-grid x, y; x + y odd) -> terrain
    std::map<std::pair<int, int>, uint16_t> tiles;    // (tile x, y) -> a tile placed over the terrain (Rectangle, a bridge, a ramp)
    std::vector<LayerObject<Unit>> units;
    std::vector<LayerObject<Doodad>> doodads;
    std::vector<LayerObject<MapSprite>> sprites;
    // A linked duplicate: the contents of the layer whose id is linkedTo, moved by (linkDx, linkDy) rects (64 x 32 px;
    // linkDx + linkDy even), made again from it by syncLinkedLayers. Its own: its id, name, eye, lock and place. 0: none.
    uint32_t linkedTo = 0;
    int linkDx = 0, linkDy = 0;
    bool empty() const { return corners.empty() && tiles.empty() && units.empty() && doodads.empty() && sprites.empty(); }
};

struct LayerBounds {
    int left = 0, top = 0, right = 0, bottom = 0;  // map pixels
    bool any = false;
};

// Which layer has each object of the flattened map: -1 the Background, else the index in LayerStack::layers.
struct LayerOwners {
    std::vector<int> units, doodads, sprites;
};

// The objects of the flattened map. Sprites: without the doodads' own sprites (Editor::layerSprites).
struct LayerObjectLists {
    std::vector<Unit> units;
    std::vector<Doodad> doodads;
    std::vector<MapSprite> sprites;
};

struct LayerStack {
    uint32_t nextId = 1;
    MapLayer background;           // the Background's objects (its terrain: the Background file)
    std::vector<MapLayer> layers;  // from the bottom up

    Bytes serialize() const;
    static bool parse(const Bytes& data, LayerStack& out);

    MapLayer& add(std::string name, int at);  // a new layer at this index
    // Puts the layers (indices) together in the lowest one, each one over those under it. The index of the result.
    int merge(std::vector<int> indices);
    void sortByName();
    // Which layer has each object of the flattened map, in its lists' order (as composeLayers puts them).
    LayerOwners owners() const;
};

// The contents of the layer move by (dx, dy) rects (64 x 32 px). The grid of ISOM corners moves
// onto itself: dx + dy must be even.
void moveLayerContents(MapLayer& layer, int dx, int dy);
// Each linked duplicate of the stack made again from its original (its contents, moved to its place; its units get
// serials of their own). A duplicate whose original is gone (deleted, merged into another) keeps its contents and is a
// layer of its own then. A duplicate of a duplicate is one of the first original. True: a layer changed.
bool syncLinkedLayers(LayerStack& stack);
// The nearest move in rects (dx + dy even) for a drag in pixels.
void snapLayerMove(int px, int py, int& dx, int& dy);
LayerBounds layerBounds(const MapLayer& layer);
// An edit changed the flattened map's objects from `before` to `now`: each object goes on belonging to its layer,
// new ones belong to `active` (-1: the Background). Objects that are not on the map stay as they are.
void captureLayerObjects(LayerStack& stack, const LayerObjectLists& before, const LayerObjectLists& now, const LayerOwners& owners, int active);

inline SectionName layerSectionName(const std::string& name) {
    SectionName n;
    for (size_t i = 0; i < 4 && i < name.size(); ++i) n.c[i] = name[i];
    return n;
}

// The Background file: the terrain sections, without what the doodads put on it.
Bytes packLayerBase(const std::vector<std::pair<std::string, Bytes>>& sections);
bool unpackLayerBase(const Bytes& data, std::vector<std::pair<std::string, Bytes>>& sections);

}  // namespace stardraft
