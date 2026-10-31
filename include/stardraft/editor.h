#pragma once

// Editor document: a loaded map plus every edit operation the UI exposes (terrain painting,
// units with placement correction, selection, move, copy/paste, doodads, locations) with
// undo/redo. GPU-free and UI-free, so it can be driven by any shell, tests or agents.

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <deque>
#include <memory>
#include <utility>
#include <future>
#include <vector>

#include "stardraft/briefing.h"
#include "stardraft/chk.h"
#include "stardraft/classic_settings.h"
#include "stardraft/classic_triggers.h"
#include "stardraft/doodads.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/locations.h"
#include "stardraft/map_file.h"
#include "stardraft/sprites.h"
#include "stardraft/strings.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

namespace stardraft {

class GameData;

// Snap (Options > Units, Sprites, Locations): `grid` is the grid in px (0: no snap; 32 a tile, 16 a half tile, 8 a
// quarter tile). snapToCell gives the middle of the grid cell that `v` is in (where a unit or sprite goes);
// snapRound gives the nearest grid line (a move, a location edge).
inline float snapToCell(float v, int grid) {
    if (grid <= 0) return v;
    const float g = float(grid);
    return float(int(v >= 0 ? v / g : v / g - 1)) * g + g / 2;
}
inline float snapRound(float v, int grid) {
    if (grid <= 0) return v;
    const float g = float(grid), q = v / g;
    return float(int(q >= 0 ? q + 0.5f : q - 0.5f)) * g;
}

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;  // pixels, right/bottom exclusive
    bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
    bool intersects(const Rect& o) const { return left < o.right && o.left < right && top < o.bottom && o.top < bottom; }
    Rect normalized() const;
};

// Mirror modes applied to editing operations (map-pixel positions are mirrored; isometric
// terrain stays valid because every result goes through the terrain correction engine).
enum class Symmetry { None, Horizontal, Vertical, Both, Rotate180, Diagonal, AntiDiagonal, Rotate4 };

// The Map Revealer unit (units.dat id 101; Edit > Tools > Place map revealers).
constexpr uint16_t kMapRevealerUnit = 101;
// Mirrors and turns that combine (as PUDForge's): each edit is repeated at every place the chosen ones give together
// (the group they make; at most kMaxSymmetricPlaces places). Left/right and top/bottom together are "both" (and so 180
// too). The turns (feedback #111) are a number: the copies go round the map's middle in equal steps, 1 for none.
namespace Mirror {
constexpr int LeftRight = 1, TopBottom = 2, Diagonal = 4, AntiDiagonal = 8, All = 15;
}
constexpr int kMaxTurns = 8;
constexpr size_t kMaxSymmetricPlaces = 32;
const char* symmetryName(Symmetry s);

class Editor {
public:
    explicit Editor(const GameData& data);
    // A new map with the terrain of the map in `path` (its size, tileset, terrain, tiles and doodads with their
    // overlay sprites); nothing else of it: no units, other sprites, locations, triggers or settings (feedback #48).
    bool newMapFromTerrain(const std::filesystem::path& path, std::string& error);
    ~Editor();

    // --- File -----------------------------------------------------------------------------------
    bool open(const std::filesystem::path& path, std::string& error);
    // How long the last open's steps took (ms), for measuring.
    const std::vector<std::pair<const char*, double>>& openSteps() const { return openSteps_; }
    // Called after each step of open() with its name and time (ms): the program's loading view.
    std::function<void(const char* step, double ms)> onOpenStep;
    // New map (see createNewScenario): `terrain` is the ISOM terrain id to fill with (1 = dirt
    // on badlands). The document has no path until it is saved.
    bool newMap(uint16_t width, uint16_t height, uint16_t era, uint16_t terrain, std::string& error);
    // New badlands map with generated terrain (see terrain_gen.h).
    bool generateMap(const struct TerrainGenParams& params, std::string& error);
    // Writes a .scm/.scx archive (keeping the other archive files) or a raw .chk.
    bool save(const std::filesystem::path& path, std::string& error);
    const std::filesystem::path& path() const { return path_; }
    // The map archive's files besides the scenario (sounds and the like), as loaded.
    // The archive's other files (sounds, ...): read on a thread after open; these wait for it.
    const std::vector<ArchiveFile>& archiveFiles() const { return finishFiles(), files_; }
    bool isOpen() const { return open_; }
    bool modified() const { return modified_; }
    bool isomRebuilt() const { return isomRebuilt_; }  // the map had no ISOM section; it was made from the tiles on open
    // Tools > Try to repair ISOM (a map whose ISOM is out of step with its tiles, as some of Cosmonarchy's are): the ISOM
    // made again from the tiles (IsomGrid::rebuildFromTiles: each corner takes the terrain that its tiles vote for), as
    // one undo step. The tiles stay as they are. The ISOM values that changed (0: none, no undo step); -1: it could not
    // be made (`error`; the map stays as it was).
    int repairIsomFromTiles(std::string& error);

    // --- State (read-only) ----------------------------------------------------------------------
    const Chk& chk() const { return chk_; }
    uint16_t width() const { return width_; }   // tiles
    uint16_t height() const { return height_; }
    uint16_t era() const { return era_; }
    const TerrainLayer& terrain() const { return terrain_; }  // MTXM
    const std::vector<Unit>& units() const { return units_; }
    const std::vector<Location>& locations() const { return locations_; }
    const std::vector<Doodad>& doodads() const { return doodads_; }
    const std::vector<MapSprite>& sprites() const { return sprites_; }
    const StringTable& strings() const { return strings_; }
    const Tileset& tileset() const { return tileset_; }
    const UnitTypes& unitTypes() const { return types_; }
    const DoodadTypes& doodadTypes() const { return doodadTypes_; }
    // (its units drawn in the players' colors: the map's COLR or CRGB color numbers as tunit.pcx rows)
    UnitSprites& unitSprites() {
        syncColorRows();
        return unitSprites_;
    }
    const GameData& gameData() const { return data_; }
    // Increases on every change; terrainRevision only when MTXM changes.
    uint64_t revision() const { return revision_; }
    uint64_t terrainRevision() const { return terrainRevision_; }

    // --- Terrain --------------------------------------------------------------------------------
    bool canPaintTerrain() const { return isomOk_; }
    // A terrain's name for the palette and messages: a mod terrain's, or the game terrain's (in a mod's tab "X (Y)", with
    // the terrain it stands on, as the mod terrains are named).
    std::string terrainName(uint16_t t) const {
        if (const Tileset::ModTerrain* m = tileset_.modTerrain(t)) return m->name;
        const IsomTerrainTable* table = modTable_ ? modTable_.get() : IsomTerrainTable::forEra(era_);
        return table ? table->terrainName(t) : "";
    }
    // The terrain core (IsomCore) paints the brush where it can (on unless STARDRAFT_TERRAIN_CORE=0: the old engine
    // only, to compare them while the core takes over; docs/dev/isom-terrain-plan.md).
    static bool terrainCoreOn();
    // A brush stroke: begin once (one undo step), paint any number of times, end.
    void beginTerrainStroke();
    // The terrain a brush of the given terrain paints at a map pixel: a mod terrain's variant for the ground there
    // (Tileset::ModTerrain::variantOf), the same one for the whole stroke; else the terrain itself.
    uint16_t brushTerrain(int px, int py, uint16_t terrain);
    // Whether two terrains are one for the brush: the same, or a mod's terrains of one set (a variant and its terrain).
    bool sameBrushTerrain(uint16_t a, uint16_t b) const;
    // The palette's entry of a terrain: a variant's terrain (variants are not in the list), else the terrain.
    uint16_t paletteTerrainOf(uint16_t t) const {
        const Tileset::ModTerrain* m = tileset_.modTerrain(t);
        return m && m->variantOf ? m->variantOf : t;
    }
    void paintTerrain(int px, int py, uint16_t terrain, int brushSize);
    void endTerrainStroke();
    // Any run of edits as one undo step (e.g. units placed while dragging): begin, edit, end.
    void beginStroke();
    void endStroke();

    // --- Terrain tools (phase 1) --------------------------------------------------------------
    // All of these go through the terrain correction engine and the symmetry setting. Brush and
    // spray belong to a stroke (begin/endTerrainStroke); fill, replace and paste are one undo step.
    bool paintBrush(int px, int py, uint16_t terrain, const Brush& brush);
    // New random tile variants (subtiles) under the brush, terrain unchanged: repainting a terrain on
    // itself. Cliff stacks re-roll from their top piece down, so they stay continuous. Returns tiles
    // changed; one undo step.
    int rerollTerrain(int px, int py, const Brush& brush, uint32_t seed);
    // Is every corner under the brush (at every symmetric position) already this terrain?
    bool brushAllTerrain(int px, int py, const Brush& brush, uint16_t terrain) const;
    void sprayTerrain(int px, int py, uint16_t terrain, const Brush& brush, int dabs, uint32_t seed);
    bool fillTerrain(int px, int py, uint16_t terrain);
    int replaceTerrain(uint16_t from, uint16_t to, const Rect* area = nullptr, int areaShape = 0);  // corners replaced; areaShape: SelectShape
    // A selection's shape in its box (feedback #31, #33, #52): the whole box, the diamond in it, or the ellipse in it.
    // Rect, Diamond and Ellipse: in the box. IsoRect: the area along the map's diamonds (its sides run as the diamonds'
    // edges) whose opposite corners are the box's top left and bottom right; IsoRectFlip: its top right and bottom left
    // (the corners a drag went between: the selection starts where the drag starts).
    enum class SelectShape { Rect, Diamond, Ellipse, IsoRect, IsoRectFlip };
    static bool inSelectShape(const Rect& box, SelectShape shape, int px, int py);
    // The ISOM corners (rect-grid points) in the shape of `area` (map pixels): those copyTerrain copies.
    std::vector<std::pair<int, int>> cornersInShape(const Rect& area, SelectShape shape) const;
    // Terrain clipboard: the ISOM corners of `area` (in its shape) plus (optionally) the units and doodads in it.
    bool copyTerrain(const Rect& area, bool withUnits, bool withDoodads, SelectShape shape = SelectShape::Rect);
    bool hasTerrainClipboard() const { return !terrainClip_.corners.empty(); }
    Rect terrainClipboardBox() const { return {0, 0, terrainClip_.width, terrainClip_.height}; }
    // Turn the terrain clipboard a quarter turn clockwise as seen in the game (corner (x, y) goes to
    // (-y, x) around the origin: the diamond grid turns onto itself, so the terrain stays valid ISOM).
    // Units turn with it; doodads cannot (their tiles face one way) and are dropped. Returns how many
    // doodads were dropped.
    int rotateTerrainClipboard();
    // Corners a terrain paste at (px, py) would set (the unmirrored copy), for previews.
    std::vector<std::pair<int, int>> terrainPasteCorners(int px, int py) const;
    // Paste with the clipboard's top-left corner snapped near (px, py). The clipboard's units obey the placement
    // options as placing does: with "units anywhere" they go where they were copied to; else each unit that does
    // not fit moves to the nearest spot where it fits (`correct`) or is not pasted.
    bool pasteTerrain(int px, int py, bool correct = true);
    uint16_t terrainAt(int px, int py) const;  // terrain of the corner nearest a map pixel (0: the ISOM did not load)
    // The terrain list's value of the terrain at a map pixel: terrainAt, or the mod terrain whose piece the tile there is
    // (a mod terrain keeps its stand-in's ISOM: Tileset::modTerrains).
    uint16_t paletteTerrainAt(int px, int py) const;
    int invalidTerrain() const { return isom_.invalidDiamonds(); }  // should always be 0
    std::vector<std::pair<int, int>> brushCorners(int px, int py, const Brush& b) const { return isom_.brushCorners(px, py, b); }
    // The game rect (its left group) whose tiles the rect (rx, ry) has in the brush's template, and its mod terrain (tag:
    // its index in tileset().modTerrains + 1; 0: a game terrain's rect). False: no such rect (IsomGrid::rectTemplate).
    bool rectTemplate(int rx, int ry, uint16_t& gameLeft, uint16_t& tag) const { return isom_.templateOf(rx, ry, gameLeft, tag); }
    // A mod's terrains in place of the ones the tileset loads with (the Custom isom terrain editor's working rules): the
    // tileset loads again with them (empty: the loaded ones).
    void useModTerrains(std::vector<Tileset::ModTerrain> terrains);

    // --- Protected terrain ------------------------------------------------------------------------
    // Protected tiles (one per tile, 1: protected; empty: none) keep their tiles (MTXM and TILE) through every terrain
    // edit: the brush, spray, fill, replace, paste, the tiles of Rectangle mode, reroll and Copy terrain. An edit of the
    // ISOM keeps the ISOM corners of the protected tiles' rects as they are and fits the terrain around them. The map
    // file does not have them: the editor keeps them in the map's project (setProtectedTiles when the map opens).
    bool tileProtected(int tx, int ty) const {
        return tx >= 0 && ty >= 0 && tx < int(width_) && ty < int(height_) && protected_.size() == size_t(width_) * height_ &&
               protected_[size_t(ty) * width_ + size_t(tx)];
    }
    bool hasProtectedTiles() const { return protectedCount_ > 0; }
    // The mask (width x height, row by row; empty: none).
    const std::vector<uint8_t>& protectedTiles() const { return protected_; }
    // Protects (on) or unprotects the tiles (x, y in tiles). One undo step; returns the tiles that changed (0: no step).
    int protectTiles(const std::vector<std::pair<int, int>>& cells, bool on);
    // The mask as it is kept (a mask of another size: none). No undo step, the map is not modified.
    void setProtectedTiles(std::vector<uint8_t> mask);
    // Changes when the mask changes (also by undo and redo).
    uint64_t protectedRevision() const { return protectedRevision_; }

    // --- Tiles (rectangular terrain, roadmap N.7) -----------------------------------------------
    // A tile group of the map's tileset (CV5) for the Tiles palette: the groups that have a megatile.
    struct TileGroupEntry {
        uint16_t group = 0;
        uint16_t subtiles = 0;              // bit mask of the subtiles (0-15) that have a megatile
        std::array<uint16_t, 2> terrains{};  // ISOM terrains Blizzard's maps use it with (0 = none); see tileGroupTerrains
        int doodad = -1;                     // the doodad (DD2 id) whose tiles these are, or -1
        int doodadRow = 0;                   // which row of that doodad
    };
    // Every tile group with a megatile, in CV5 order (terrain groups, then the doodads' groups).
    const std::vector<TileGroupEntry>& tileGroups() const;
    // Place one tile value (group << 4 | subtile) on every tile from (tx0, ty0) to (tx1, ty1) (inclusive, any order;
    // clipped to the map), as it is: into MTXM (what the game shows) and TILE (StarEdit's copy), without the terrain
    // correction, and the ISOM does not change. Doodads whose tiles it covers go (as when terrain is painted over
    // them); units it makes unplaceable go unless illegal placement is allowed. Symmetry does not apply (tiles do
    // not mirror). Returns the tiles changed; one undo step (none when nothing changes).
    // Later terrain edits keep these tiles unless they change the ISOM of the tiles' own rects; the brush (paintBrush)
    // makes them again from the ISOM where it paints.
    int placeTiles(int tx0, int ty0, int tx1, int ty1, uint16_t value);
    // The same for a list of tiles (a brush's footprint: x, y in tiles).
    int placeTileCells(const std::vector<std::pair<int, int>>& cells, uint16_t value);
    // Tiles with a value each (x, y in tiles), as placeTiles places them: one undo step, or part of an open stroke.
    // A value whose group the tileset does not have is skipped; null tiles (megatile 0) are tiles like the others.
    struct TileAt {
        int x = 0, y = 0;
        uint16_t value = 0;
    };
    int placeTileValues(const std::vector<TileAt>& cells);
    // The tiles of tiles (tx0, ty0)-(tx1, ty1) (inclusive) made again from the ISOM, as its terrains give them (the
    // tiles placed by hand there go: a Cosmonarchy doodad brush taken away). One undo step. False: nothing changed.
    bool retileFromIsom(int tx0, int ty0, int tx1, int ty1);
    // The tile clipboard (Rectangle mode): the tiles of a rectangle (inclusive, in tiles) as they are.
    // (shape: the tiles whose middle is in it; the others are not pasted)
    bool copyTiles(int tx0, int ty0, int tx1, int ty1, SelectShape shape = SelectShape::Rect);
    bool hasTileClipboard() const { return !tileClip_.values.empty(); }
    int tileClipboardWidth() const { return tileClip_.width; }
    int tileClipboardHeight() const { return tileClip_.height; }
    const std::vector<uint16_t>& tileClipboard() const { return tileClip_.values; }
    // Paste the copied tiles with their top-left tile at (tx, ty), as they are (no terrain correction, no symmetry).
    // Returns the tiles changed; one undo step.
    int pasteTiles(int tx, int ty);

    // --- Symmetry -------------------------------------------------------------------------------
    void setSymmetry(Symmetry s);
    Symmetry symmetry() const;
    void setMirrors(int mask) { mirrors_ = mask & Mirror::All; }
    int mirrors() const { return mirrors_; }
    // The turns: the number of positions, equally spaced round the map's middle (1: no turn, 2: a half turn, 4: quarter
    // turns, 3: a third of a turn...), 1 to kMaxTurns. With the mirrors, each turned copy is mirrored too.
    void setTurns(int n) { turns_ = std::clamp(n, 1, kMaxTurns); }
    int turns() const { return turns_; }
    // The point itself followed by its mirror images (duplicates removed).
    std::vector<std::pair<int, int>> symmetricPoints(int px, int py) const;
    // The points a terrain brush paints at (a StarEdit brush is centered on its lattice point first; see editor.cpp).
    std::vector<std::pair<int, int>> terrainSymmetricPoints(int px, int py, const Brush& brush) const;

    // --- Units ----------------------------------------------------------------------------------
    PlacementContext placementContext(const std::vector<Unit>* units = nullptr) const;
    // Nearest position to (x, y) where `unit` passes checkPlacement (searching up to `radius` px),
    // or nullopt. `units` is the list to check against (defaults to the map's units).
    std::optional<std::pair<uint16_t, uint16_t>> findValidPosition(const Unit& unit, int radius = 320,
                                                                    const std::vector<Unit>* units = nullptr) const;
    // Place a unit; with `correct`, invalid spots move to the nearest valid one. Selects the new unit.
    PlaceResult placeUnit(uint16_t type, uint8_t owner, int x, int y, bool correct = true);
    // Allow illegal unit placement: units go exactly where they are put (no checks, no correction)
    // and terrain edits leave them alone. Off: a terrain edit removes the units it makes unplaceable
    // (units that were placeable before it).
    void setAllowIllegalUnits(bool allow) { allowIllegalUnits_ = allow; }
    // The most units the map may have (0: no limit): placing, spraying and pasting stop there (GitHub feedback #103).
    void setUnitLimit(int n) { unitLimit_ = n; }
    int unitLimit() const { return unitLimit_; }
    // Unit stacking (PlacementContext::stacking) for every placement check.
    void setAllowStacking(bool allow) { allowStacking_ = allow; }
    bool allowStacking() const { return allowStacking_; }
    // Town halls and resources keep 3 tiles apart (PlacementContext::resourceDistance) for every placement check.
    void setResourceDistance(bool keep) { resourceDistance_ = keep; }
    bool resourceDistance() const { return resourceDistance_; }
    // More than one start location for each player (the default value; the editor's Program options set it, off there).
    // Off: placing a start location for a player that has one moves that one to the new spot, as StarEdit does.
    void setManyStartLocations(bool many) { manyStartLocations_ = many; }
    bool manyStartLocations() const { return manyStartLocations_; }
    bool allowIllegalUnits() const { return allowIllegalUnits_; }
    // Units removed by terrain edits since the last call.
    int takeUnitsRemovedByTerrain() { return std::exchange(removedByTerrain_, 0); }
    int unitAt(int x, int y) const;  // topmost unit whose box contains the point, or -1
    // Every unit whose box contains the point, the top one first (the one unitAt gives: the smallest box, then the unit
    // placed last): the units of a stack, in the order a click goes through them.
    std::vector<int> unitsAt(int x, int y) const;
    Rect unitBox(const Unit& u) const;

    // Replace every unit of type `from` (only selected ones if `selectionOnly`) with type `to`,
    // keeping owner and applicable properties; returns how many were replaced. One undo step.
    int replaceUnits(uint16_t from, uint16_t to, bool selectionOnly);
    // Give the selected units to player `owner` (0..11); the ones that stay neutral (staysNeutral) keep their owner; the
    // owner is valid from then on. Returns how many changed. One undo step.
    int setSelectionOwner(uint8_t owner);
    // A unit type that a player change leaves neutral: a resource (minerals, a geyser), or a critter (StarEdit's Neutral
    // group and no race), unless StarEdit's availability says Non-Neutral. (Not the flag alone: a mod's unit types often
    // lack it, and their units must take a player.)
    bool staysNeutral(uint16_t type) const;
    // Edit placed units (the properties dialog): `change` runs on a copy of each; the ones it changes are
    // written back as one undo step. Returns how many changed.
    int updateUnits(const std::vector<int>& indices, const std::function<void(Unit&)>& change);
    // Spray `count` units of the given types (random pick) within `radius` px of (x, y); only spots
    // that pass checkPlacement are used. Honors symmetry. Returns units placed. One undo step.
    int sprayUnits(const std::vector<uint16_t>& types, uint8_t owner, int x, int y, int radius, int count, uint32_t seed);
    // Spray `count` sprites of the given kinds (random pick) at random points within `radius` px of (x, y), each point
    // snapped to a grid of `grid` px (snapToCell). Only spots where the sprite can go are used. Returns sprites placed.
    int spraySprites(const std::vector<uint16_t>& ids, uint8_t owner, float x, float y, int radius, int count, uint32_t seed, int grid);

    // --- Selection ------------------------------------------------------------------------------
    const std::vector<int>& selection() const { return selection_; }
    bool isSelected(int unit) const;
    void clearSelection();
    void select(int unit, bool add);
    void toggleSelected(int unit);
    void selectRect(const Rect& r, bool add);
    void selectAll();
    // Select every unit of `type` on the map (only player `owner`'s when owner >= 0), in place of the selection.
    // Returns how many units are selected.
    int selectUnitsOfType(uint16_t type, int owner = -1);
    void deleteSelection();
    // Move the selected units by (dx, dy). They go as a group when they all fit there (or with "units anywhere").
    // Else, with `correct`, each unit goes to the nearest spot where it fits (a unit with no spot stays); without
    // it, the move is refused and no unit moves. Returns how many units moved (0: refused, no undo step).
    int moveSelection(int dx, int dy, bool correct = true);
    // Terran add-ons: the selected buildings' linked add-ons (they move with them), and the links that are no longer
    // true (a partner deleted, or an add-on off its building's add-on spot) cleared on both sides.
    std::vector<int> withLinkedAddons(std::vector<int> sel) const;
    void fixAddonLinks();
    // Copy/paste of units; paste centers the copied group on (x, y) and selects the pasted units. Each unit obeys
    // the placement options as placing does: it goes as it is if it fits (or with "units anywhere"), else to the
    // nearest spot where it fits (`correct`), else it is not pasted. The symmetry pastes the copy at its mirror images
    // and turns too, as placing does. Returns how many units were pasted.
    void copySelection();
    bool canPaste() const { return !clipboard_.empty(); }
    int paste(int x, int y, bool correct = true);
    // The units a paste at (x, y) puts down, before the placement checks: the copy, then its symmetric copies.
    std::vector<Unit> pasteUnits(int x, int y) const;
    const std::vector<Unit>& clipboard() const { return clipboard_; }  // positions relative to group center
    // The copied units go to `owner` when pasted (those that can have an owner: not resources or critters). Returns how
    // many of them changed; the map does not change.
    int setClipboardOwner(uint8_t owner);

    // --- Doodads --------------------------------------------------------------------------------
    // Place doodad `id` centered near (x, y) (snapped to tiles). `checkTerrain` enforces dddata.bin.
    bool placeDoodad(uint16_t id, int x, int y, bool checkTerrain, std::string* reason = nullptr);
    // Spray `count` doodads (random pick from `ids`) within `radius` px of (x, y): each lands only
    // where it fits the terrain and does not overlap another doodad. Returns doodads placed.
    // With `checkTerrain` off (Options > Doodads > Place anywhere), the terrain under it is not checked; on, it must be
    // the doodad's own terrain (doodadScatterFits).
    int sprayDoodads(const std::vector<uint16_t>& ids, int x, int y, int radius, int count, uint32_t seed, bool checkTerrain = true);
    // A campaign's maps (roadmap R25): the data of map `from` into this one (the unit, upgrade and technology settings,
    // with the players' upgrades and technologies; the custom unit names stay this map's), or its terrain (the size, the
    // tileset, the tiles and the doodads; the maps must have the same size). One undo step each; false with `error`.
    bool copyMapData(const Chk& from, std::string& error);
    bool copyTerrain(const Chk& from, std::string& error);
    // Decorate the terrain (roadmap R37): doodads (a random pick from `ids`) over `area` (px), one in each square of
    // `spacingTiles` tiles where one fits its tiles and its terrain (doodadScatterFits) and overlaps no doodad. One undo
    // step; returns how many.
    int decorate(const std::vector<uint16_t>& ids, Rect area, int spacingTiles, uint32_t seed, const std::vector<Rect>& avoid = {});
    // Map Revealers (Discord feedback #125; unit kMapRevealerUnit): one for each player in `owners` (0 to 7) at the middle
    // of each square of `spacingTiles` tiles over the whole map (the squares' centers, clamped to the map). A place where
    // that player has a revealer already is skipped. Revealers have no placement rules: they go where the grid puts them
    // (unit limit aside). Does not change the selection. One undo step; returns how many were placed.
    int placeMapRevealers(const std::vector<uint8_t>& owners, int spacingTiles);
    // Removes every Map Revealer, of every player (the selection keeps the other units). One undo step; returns how many.
    int removeMapRevealers();
    // Links: two Nydus canals (the Nydus link), or an add-on and its building (the add-on link), as the placing of an
    // add-on links them. canLinkUnits: both are free of links and can be linked. linkUnits: one undo step. linkedUnit:
    // the index of the unit that `unit` is linked to, -1 for none.
    bool canLinkUnits(int a, int b) const;
    bool linkUnits(int a, int b);
    int linkedUnit(int unit) const;
    // Moves units to another place in the map's UNIT order (the index, which the game and the triggers use): the units
    // `indices` keep their order among themselves and go before unit `target` (the unit there now; units().size(): to the
    // end). The selection stays on the same units. One undo step; 0 (nothing changed, no step) when the order stays.
    int moveUnits(const std::vector<int>& indices, int target);
    // View > Layers > Map revealers off: the revealers are not picked (unitAt, unitsAt, selectRect, selectAll) while hidden.
    // (Hiding them drops the revealers from the selection; a change is a new revision, so the lists are made again.)
    void setMapRevealersHidden(bool hidden);
    // Whether unit `i` can be picked: false for a hidden map revealer. The map keeps it either way.
    bool pickable(int unit) const { return !(revealersHidden_ && units_[size_t(unit)].type == kMapRevealerUnit); }
    // Options > Doodads > Move to the nearest place where it fits: the center (px) nearest (x, y), within `radiusTiles`
    // tiles, where doodad `id` fits (on the map, not on another doodad, dddata.bin when `checkTerrain`), or nullopt.
    std::optional<std::pair<int, int>> nearestDoodadFit(uint16_t id, int x, int y, bool checkTerrain, int radiusTiles = 8) const;
    // The doodad whose tiles cover pixel (x, y) (index into doodads()), or -1.
    int doodadAt(int x, int y) const;
    // Every doodad with this id (indices into doodads()), for "Select all of this type".
    std::vector<int> doodadsOfType(uint16_t id) const;
    // --- Sprites (THG2, drawn as sprites: sprites.dat ids) -----------------------------------------
    // Place sprite `id` at pixel (x, y) for `owner`, as StarEdit does (THG2 with the draw-as-sprite flag). One undo step.
    bool placeSprite(uint16_t id, int x, int y, uint8_t owner);
    // The sprite nearest (x, y) within `radius` px that is not a doodad's own overlay (index into sprites()), or -1.
    int spriteAt(int x, int y, int radius = 24) const;
    // Is sprite `index` a doodad's own overlay (it goes with its doodad: not a sprite of its own)?
    bool spriteIsDoodadOverlay(int index) const;
    // Every sprite of the same type as sprite `index` (the same id, drawn the same way: as a sprite or as a unit),
    // without the doodads' overlays (indices into sprites()). Empty when `index` is not a sprite.
    std::vector<int> spritesOfType(int index) const;
    // Remove a sprite. One undo step.
    bool deleteSprite(int index);
    // Move a sprite to (x, y). One undo step.
    bool moveSprite(int index, int x, int y);
    // Move a doodad (its tiles and its overlay) so it is centered near (x, y), with the rules placing has (dddata.bin
    // when `checkTerrain`, no overlap). One undo step; false (and nothing changed) when it does not fit there. The
    // doodad is the last one of doodads() after the move.
    bool moveDoodad(int index, int x, int y, bool checkTerrain, std::string* reason = nullptr);
    // Several at once (a selection), each one undo step. Doodads: all move by (dx, dy) or none does (the rules as
    // placing); after the move they are the last doodads(). Placing: each that fits; returns how many.
    bool moveDoodads(const std::vector<int>& indices, int dx, int dy, bool checkTerrain, std::string* reason = nullptr);
    bool deleteDoodads(const std::vector<int>& indices);
    // Convert to terrain: the doodads' tiles stay where they are, as placed tiles (MTXM and TILE, as placeTiles puts
    // them; the ISOM does not change), and their doodad entries (DD2) go; their overlay sprites (THG2) stay, as
    // sprites of their own. The tiles are
    // then plain terrain: no doodad to select, move or delete. Returns how many doodads were converted; one undo step
    // (none when 0).
    int convertDoodadsToTerrain(const std::vector<int>& indices);
    struct ItemAt {
        uint16_t id = 0;
        int x = 0, y = 0;
        uint8_t owner = 0;
    };
    // With `nearest`, one that does not fit where it is goes to the nearest place where it fits (nearestDoodadFit).
    int placeDoodads(const std::vector<ItemAt>& items, bool checkTerrain, bool nearest = false);
    // Sprites: move by (dx, dy) (those that stay on the map), delete, place.
    bool moveSprites(const std::vector<int>& indices, int dx, int dy);
    bool deleteSprites(const std::vector<int>& indices);
    // Replace with another type (Replace with...), one undo step each. Doodads: each where it was, when the new one
    // fits there (else it stays as it was); returns how many changed. Sprites: the same places.
    int replaceDoodads(const std::vector<int>& indices, uint16_t id, bool checkTerrain);
    int replaceSprites(const std::vector<int>& indices, uint16_t id);
    int placeSprites(const std::vector<ItemAt>& items);
    // Remove a doodad: its tiles give way to the terrain under it (TILE), its overlay sprite goes too.
    // One undo step.
    bool deleteDoodad(int index);
    // Doodads removed because terrain was painted over them, since the last call.
    int takeDoodadsRemovedByTerrain() { return std::exchange(doodadsRemovedByTerrain_, 0); }
    // Does the terrain under doodad `d` (TILE, else MTXM: `now`) still fit it? Where it needs a tile group (dddata.bin),
    // that group or another variant of the same piece; where it needs none, the piece that was there `before`. A
    // terrain edit removes a doodad only when this is false after it (as StarEdit does).
    bool doodadTerrainHolds(const Doodad& d, const TerrainLayer& now, const TerrainLayer& before) const;
    // Does a doodad at this origin overlap an existing doodad's footprint?
    bool doodadOverlaps(const DoodadType& t, int tileX, int tileY) const;
    // Scattered doodads (Spray, Decorate) only where they belong: besides doodadFits, the cells that need no tile group
    // have the doodad's own terrain under them (the terrains of the groups its other cells need, or an edge between
    // them); a doodad that needs none lies on one terrain (no edge): the one its palette group is named after, when
    // there is one. `terrains`: tileGroupTerrains of the tileset.
    // `under`: the terrain without doodads (TILE, else MTXM).
    bool doodadScatterFits(const DoodadType& t, int tileX, int tileY, const std::vector<std::array<uint16_t, 2>>& terrains,
                           const TerrainLayer& under) const;
    // Top-left tile a doodad would occupy when centered near (x, y).
    void doodadOrigin(const DoodadType& t, int x, int y, int& tileX, int& tileY) const;

    // --- Locations ------------------------------------------------------------------------------
    int locationCount() const { return int(locations_.size()); }
    bool locationUsed(int i) const;
    std::string locationName(int i) const;
    Rect locationRect(int i) const;
    // The edges as the location has them (not put in order: an inverted location's left is more than its right, or its
    // top more than its bottom).
    Rect locationEdges(int i) const;
    int locationAt(int x, int y) const;  // smallest used location containing the point, or -1 (never Anywhere)
    // Returns index or -1 if all slots are used. inverted: right < left and bottom < top (the game's trick: such a
    // location holds air units only); one undo step either way.
    // axes (with inverted): 1 left and right swapped, 2 top and bottom swapped, 3 both.
    // A new location (one undo step): `name` empty gives "Location <n>"; `excluded`: its LocationElevation bits (a copy).
    int createLocation(const Rect& r, bool inverted = false, int axes = 3, const std::string& name = "", uint16_t excluded = 0);
    bool locationInverted(int i) const;  // its right edge left of its left edge, or its bottom above its top
    int locationInvertedAxes(int i) const;  // 1 left/right, 2 top/bottom (as createLocation's axes)
    void setLocationRect(int i, const Rect& r);
    void renameLocation(int i, const std::string& name);
    // (a mod's trigger commands in Comments: the lines that use the old name get the new one, in the same undo step:
    // UnitDataLoader::renameInComment)
    void renameInComments(int location, const std::string& from, const std::string& to);
    // Elevation flags (MRGN): the LocationElevation bits that are set are the elevations the location excludes.
    uint16_t locationElevation(int i) const;
    // Sets the excluded elevations (LocationElevation bits; the unused bits stay as they are). One undo step;
    // returns false when nothing changed.
    bool setLocationElevation(int i, uint16_t excluded);
    // The location edit: name, edges and excluded elevations together, as one undo step. Edges are pixels; they must
    // have right > left and bottom > top, and they are clamped to the map. An empty name keeps the name. The Anywhere
    // location keeps its edges (it is always the whole map). Edges equal to locationRect(i) keep the location as it
    // is (an inverted location stays inverted). Returns false when nothing changed or the edges are wrong (`error`
    // then says why; nothing changed).
    // asTyped: `rect` is the edges as the location gets them (Location properties), with no limit but 0: left more
    // than right inverts it left and right, top more than bottom inverts it top and bottom; invertedAxes is not used.
    struct LocationEdit {
        std::string name;
        Rect rect;
        uint16_t excluded = 0;  // LocationElevation bits
        int invertedAxes = -1;  // 1 left/right, 2 top/bottom (as locationInvertedAxes); -1: as it is
        bool asTyped = false;
    };
    bool editLocation(int i, const LocationEdit& edit, std::string* error = nullptr);
    void deleteLocation(int i);
    // The selected location (-1: none): its edges resize, its properties open. selectLocation: it alone.
    int selectedLocation() const { return selectedLocation_; }
    void selectLocation(int i) { selectedLocation_ = i, selectedLocations_.assign(i >= 0 ? 1 : 0, i); }
    // Several selected (they move, copy and delete together): each used one, the selected location among them; none
    // while no location is selected.
    std::vector<int> selectedLocations() const;
    bool locationSelected(int i) const;
    void addLocationSelection(int i);     // i too, as the selected location
    void removeLocationSelection(int i);  // (the selected location: another of them, or none)

    // --- Players and forces ---------------------------------------------------------------------
    // Players 1-8 (index 0-7): who controls the slot (OWNR, mirrored to IOWN), the race (SIDE) and the
    // force (FORC). Forces 1-4: a name (a map string) and their properties.
    enum class Controller : uint8_t { Inactive = 0, Rescue = 3, Computer = 5, Human = 6, Neutral = 7, Closed = 8 };
    enum class Race : uint8_t { Zerg = 0, Terran = 1, Protoss = 2, UserSelect = 5, Random = 6 };
    // A player's color (COLR; Remastered's CRGB when the map has it): random, the player's choice, one of the game's
    // colors (0-21; 12 and up only through CRGB), or any RGB.
    struct PlayerColor {
        enum Kind : uint8_t { Random = 0, Choice = 1, Custom = 2, Id = 3 };
        Kind kind = Id;
        uint8_t id = 0;     // Kind::Id
        uint32_t rgb = 0;   // Kind::Custom: 0xRRGGBB (the game uses it only in multiplayer games)
        bool operator==(const PlayerColor&) const = default;
    };
    struct Player {
        Controller controller = Controller::Human;
        Race race = Race::UserSelect;
        int force = 0;  // 0-3
        PlayerColor color;
    };
    static constexpr int kColorIds = 22;
    static uint32_t colorIdRgb(int id);       // the game's color `id` as 0xRRGGBB (Red, Blue, ..., Black)
    static const char* colorIdName(int id);
    uint32_t playerRgb(int p) const;          // the color player p (0-7) has on the map (random and choice: its COLR color)
    // The colors a map's players can have by number: StarCraft's kColorIds, or a mod's (UnitDataLoader::colorNames: its
    // tunit.pcx rows, the swatch its tminimap.pcx gives in the map's tileset).
    int colorCount() const;
    std::string colorName(int id) const;
    uint32_t colorRgb(int id) const;
    enum ForceFlag : uint8_t { RandomStart = 1, Allies = 2, AlliedVictory = 4, SharedVision = 8 };
    struct Force {
        std::string name;
        uint8_t flags = 0;
    };
    Player player(int p) const;
    Force force(int f) const;
    void setPlayer(int p, const Player& player);  // one undo step
    void setForce(int f, const Force& force);     // one undo step
    void setPlayersAndForces(const std::vector<Player>& players, const std::vector<Force>& forces);  // 8 and 4; one undo step
    // --- Mission briefings (MBRF) ---------------------------------------------------------------
    std::vector<BriefingTrigger> briefings() const;
    void setBriefings(const std::vector<BriefingTrigger>& briefings);  // one undo step

    // --- Map properties -------------------------------------------------------------------------
    // The scenario's title and description (SPRP; the game shows them in the lobby).
    std::string scenarioName() const;
    std::string scenarioDescription() const;
    // The terrain each terrain of tileset `from` becomes in tileset `to` (index: terrain id; 0: the fill), as a change
    // of tileset in Map properties maps it (terrainRole in editor.cpp).
    static std::vector<uint16_t> tilesetTerrainMap(uint16_t from, uint16_t to, uint16_t fill);
    void setScenarioText(const std::string& name, const std::string& description);  // one undo step
    // A new size and tileset; one undo step. Terrain, units, doodads, sprites and locations move by (shiftX, shiftY)
    // tiles; what is then outside the map goes. shiftX is made even and (shiftX / 2 + shiftY) even, as the terrain's
    // diamonds need. A new tileset takes for each terrain the one of the same name, else `fill`; it also fills new
    // ground. Doodads that the new tileset does not have go. Returns false (nothing changed) on error.
    bool resizeMap(uint16_t width, uint16_t height, uint16_t era, int shiftX, int shiftY, uint16_t fill, std::string& error);
    // The shift (tiles) that resizeMap uses for (shiftX, shiftY): shiftX even (rounded down), and shiftY one less when
    // (shiftX / 2 + shiftY) is odd.
    static void resizeShift(int& shiftX, int& shiftY);
    // The map of a scenario in memory (no file, no archive files, no undo steps): a copy of a map to try an edit on (the
    // resize preview of Map properties). False with `error` as open().
    bool openChk(const Chk& chk, std::string& error);
    // The scenario of `edited` (a copy of this map from openChk that an edit changed: the resize preview of Map
    // properties) in place of this map's, as one undo step, so the map is the copy tile for tile (the terrain core
    // selects some tiles at random). The archive's other files stay. False when `edited` is not open. The protected
    // tiles move by (shiftX, shiftY) tiles (as resizeShift gives them); a new tileset unprotects them.
    bool takeScenario(const Editor& edited, int shiftX = 0, int shiftY = 0);
    // A file of the map archive by its path (a briefing's sound), or nullptr.
    const ArchiveFile* archiveFile(const std::string& name) const;
    // Adds (or replaces) a file in the map archive, such as a briefing's sound; saved with the map. One undo step.
    void setArchiveFile(const std::string& name, Bytes data);
    void setArchiveFiles(std::vector<ArchiveFile> files);  // likewise, all in one undo step
    bool removeArchiveFile(const std::string& name);      // one undo step
    bool removeArchiveFiles(const std::vector<std::string>& names);  // the ones that are there, all in one undo step
    // A new path for a file; the briefings that play it use the new path. One undo step. False: no such file, or
    // a file of that path (in any case) is in the map already.
    bool renameArchiveFile(const std::string& from, const std::string& to);
    // Game sounds the map names by path only (in its WAV list, without the file: the game plays its own).
    std::vector<std::string> soundReferences() const;
    void addSoundReference(const std::string& path);     // one undo step
    bool removeSoundReference(const std::string& path);  // one undo step

    // Why StarCraft would refuse to start the map (empty when it starts): at least two players (human or
    // computer), at least one human, a start location for every player that owns units, and at least two start
    // locations. `startLocations` false leaves out the start location problems (startLocationProblems).
    std::vector<std::string> playabilityProblems(bool startLocations = true) const;
    // The start location problems alone: a player that owns units and has no start location; fewer than two start locations.
    std::vector<std::string> startLocationProblems() const;
    // Places the start locations that startLocationProblems names, one undo step: for each player that owns units and has
    // none, one near the middle of its units (the nearest spot where it fits); then, while the map has fewer than two, one
    // for the next player in the game without one (humans first) at the spot farthest from the start locations.
    // Returns how many were placed.
    int placeStartLocations();

    // --- Classic settings (roadmap L.3) ---------------------------------------------------------------------
    // Unit settings (UNIx and UNIS, PUNI), upgrade settings (UPGx and UPGS, PUPx and UPGR) and tech settings (TECx and
    // TECS, PTEx and PTEC), as classic_settings.h reads and writes them. The game's values (what "use default" gives),
    // or nullptr when the .dat files cannot be read.
    const SettingDefaults* settingDefaults() const;
    UnitSettings unitSettings() const;
    UpgradeSettings upgradeSettings() const;
    TechSettings techSettings() const;
    // Each is one undo step and writes only what changed; false (and no undo step) when nothing changed.
    // `names`: new names for units (unit id, text; an empty text gives the game's name), kept as map strings.
    bool setUnitSettings(const UnitSettings& settings, const std::vector<std::pair<int, std::string>>& names = {});
    bool setUpgradeSettings(const UpgradeSettings& settings);
    bool setTechSettings(const TechSettings& settings);
    // The map string of a unit's name in the unit settings ("" when it has none).
    std::string unitSettingName(int unit) const;

    // --- Classic triggers (roadmap L.2) ---------------------------------------------------------------------
    // TRIG as classic_triggers.h reads it. Each set is one undo step and returns false (no undo step) when nothing
    // changed. `strings`: the map's string table with the texts the triggers use added (classicStringId); it takes
    // the place of the map's own when it differs.
    ClassicTriggers classicTriggers() const;
    bool setClassicTriggers(const ClassicTriggers& triggers, const StringTable* strings = nullptr);
    bool setClassicTrigger(int index, const ClassicTrigger& trigger, const StringTable* strings = nullptr);
    bool addClassicTrigger(const ClassicTrigger& trigger, int at = -1, const StringTable* strings = nullptr);
    bool removeClassicTrigger(int index);
    bool moveClassicTrigger(int from, int to);
    // The map's switch names (SWNM; "" for a switch without one) and sound files (the WAV list: string ids).
    std::string switchName(int index) const;
    // Sets a switch's name (1-256 by index 0-255; "" takes it away). One undo step; false (nothing changed) when the
    // name is the same.
    bool setSwitchName(int index, const std::string& text);
    // One text of the map string table (id 1 and up; `get strings`). One undo step; false when nothing changed.
    bool setMapString(uint32_t id, const std::string& text);
    std::vector<uint32_t> soundStrings() const;

    // --- Undo -----------------------------------------------------------------------------------
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoSteps() const { return undo_.size(); }
    // The most undo steps kept (at least 1; the default: as many as kUndoSteps and kUndoBytes allow). The off-screen maps
    // that are painted again and again (the isom terrain editor's examples) keep 1.
    void setUndoLimit(size_t steps) { undoLimit_ = std::max<size_t>(1, steps); }  // the steps that Undo can take back (and Redo give again)
    // A map for its terrain only (the isom terrain editor's off-screen maps): newMap loads no unit types and sprites (a
    // load takes most of a new map's time). Units on such a map have no types.
    void setTerrainOnly(bool on) { terrainOnly_ = on; }
    size_t redoSteps() const { return redo_.size(); }
    // Undo and Redo wait for the end of a stroke (a paint, a spray or a place that the mouse button still holds):
    // during one they do nothing. After them, the map is modified unless it is as it was at its last open or save.
    // `steps`: that many steps at once (the undo history's jump), with one reload of the map at the end; the map is
    // then as after the same number of single undos (redos). More steps than there are: all of them.
    void undo(size_t steps = 1);
    void redo(size_t steps = 1);
    // The name of an undo step, found from the map before and after it: what changed ("Add %d units", "Change the
    // terrain (%d tiles)"); `count` is the number for the text's %d (-1: the text has no %d). k = 0 is the step that
    // Undo takes back next (redoStepName: that Redo gives again next), 1 the step before it, and so on.
    struct StepName {
        std::string text;
        int count = -1;
    };
    StepName undoStepName(size_t k) const;
    StepName redoStepName(size_t k) const;
    bool inStroke() const { return inStroke_; }
    // The map's state: a number that changes with each change, undo and redo (an undo gives back the number the state
    // had). Another part of the program can tell with it whether the map's last step is still the one it made.
    uint64_t stateId() const { return state_; }
    // The map's state after the step that is k steps back in the undo (redo: the state before the step that is k steps
    // ahead in the redo); k = 0 is the state now. 0 when there is no such step.
    uint64_t undoStateId(size_t k) const { return !k ? state_ : k <= undo_.size() ? undo_[undo_.size() - k].state : 0; }
    uint64_t redoStateId(size_t k) const { return !k ? state_ : k <= redo_.size() ? redo_[redo_.size() - k].state : 0; }

    struct TerrainClip {
        std::vector<CornerValue> corners;  // relative to (0, 0) = the copied area's first corner
        std::vector<Unit> units;           // positions relative to the corner origin in pixels
        std::vector<Doodad> doodads;       // likewise
        int width = 0, height = 0;         // pixels
    };
    struct TileClip {
        int width = 0, height = 0;     // tiles
        std::vector<uint16_t> values;  // row by row
    };
    // The terrain and tile clipboards (copyTerrain, copyTiles) as one value: a copy in one map pastes in another map of
    // the same tileset (GitHub #146).
    struct TerrainCopies {
        TerrainClip terrain;
        TileClip tiles;
    };
    TerrainCopies terrainCopies() const { return {terrainClip_, tileClip_}; }
    void setTerrainCopies(const TerrainCopies& c) { terrainClip_ = c.terrain, tileClip_ = c.tiles; }

private:
    // engine + store + commit; overPlaced: the tiles placed in Rectangle mode in the rects of the corners are made again
    // (the brush: GitHub #143)
    bool applyTerrain(const std::vector<CornerValue>& corners, bool overPlaced = false);
    // StarEdit's brush through the terrain core (IsomCore: IsomTerrain): `terrain` on extent x extent diamonds at the
    // diamond nearest each symmetric point, then the tiles; the corner model (isom_) loads the result. The editor's
    // undo, doodad and unit checks as applyTerrain's; the brush's tiles replace the tiles placed in Rectangle mode.
    // False when nothing could be painted.
    bool applyCoreBrush(int px, int py, uint16_t terrain, int extent);
    // The terrain core's part of an edit: `paint` (on core_, loaded from the map), then the map takes its result as
    // applyTerrain's does (one undo step, the doodads and units it breaks; a doodad stays while its terrain fits it).
    // The core makes the tiles of each diamond it paints again (also the tiles placed in Rectangle mode there). When it
    // leaves a rect without a tile (IsomCore::untileable), `repair` makes the edit instead (the old engine).
    // (x0, y0)-(x1, y1): the edit's grid area.
    bool applyCore(const std::function<bool()>& paint, const std::function<bool()>& repair, int x0, int y0, int x1, int y1);
    void sprinkleModTiles(const TerrainLayer& before);
    // A mod's terrains (Tileset::modTerrains): the game's terrain table with them (IsomTerrainTable::withExtras), for
    // the old engine (the terrain core knows only the game's shapes); none: the game's table, and the core.
    std::unique_ptr<IsomTerrainTable> modTable_;
    bool useCore() const { return terrainCoreOn() && !modTable_; }
    // The old engine's part of an edit (IsomGrid::apply, then store): the corners and their grid area; overPlaced as
    // applyTerrain's.
    bool applyOld(const std::vector<CornerValue>& corners, int x0, int y0, int x1, int y1, bool overPlaced = false, size_t own = SIZE_MAX);
    IsomCore core_;
    std::pair<int, int> mirror(int px, int py, int which) const;
    // The point turned `k` steps of 360 / turns() degrees about the map's middle (in the map's proportions: on a square map a
    // true turn; on another the copies stay in the map's box).
    std::pair<int, int> turnPoint(int px, int py, int k) const;
    TerrainClip terrainClip_;
    TileClip tileClip_;
    int mirrors_ = 0;  // Mirror:: flags
    int turns_ = 1;    // the turns (setTurns)
    struct SymmetricPlace {
        int x, y;
        bool flipX, flipY;  // a mirror image across a vertical (horizontal) axis: a pasted copy flips that way
    };
    std::vector<SymmetricPlace> symmetricPlaces(int px, int py) const;

    // Protected terrain (protectTiles): the mask, its count of protected tiles and its revision.
    std::vector<uint8_t> protected_;
    size_t protectedCount_ = 0;
    uint64_t protectedRevision_ = 0;
    void protectedChanged();  // (the count and the revision again)
    // A rect (rx, ry) of the ISOM with a protected tile.
    bool rectProtected(int rx, int ry) const { return tileProtected(rx * 2, ry) || tileProtected(rx * 2 + 1, ry); }
    // An ISOM corner of a protected rect (one of the four rects around it).
    bool cornerProtected(int cx, int cy) const {
        return rectProtected(cx - 1, cy - 1) || rectProtected(cx, cy - 1) || rectProtected(cx - 1, cy) || rectProtected(cx, cy);
    }
    // Are protected tiles in the grid area (x0, y0)-(x1, y1) (corners) or within `margin` grid steps of it?
    bool protectedNear(int x0, int y0, int x1, int y1, int margin) const;
    // An ISOM edit's corners with protected terrain near them: the corners of protected rects go out, and the corners of
    // the protected rects near the edit (`margin` grid steps around its corners) come in with the terrain they have, so
    // the edit keeps them (IsomGrid::apply locks its corners). The edit's own corners stay first. Returns how many of
    // them stay; -1: no protected tiles near (nothing changed).
    int pinProtected(std::vector<CornerValue>& corners, int margin) const;
    static constexpr int kProtectMargin = 12;  // (grid steps: the reach of an edit's repair around its corners)
    // The protected tiles of a map of oldWidth x oldHeight moved by (dx, dy) tiles onto the map's size now (resizeMap,
    // takeScenario).
    void shiftProtected(int oldWidth, int oldHeight, int dx, int dy);
    // The tiles (MTXM, TILE) before an edit, and the protected tiles back to them after it (before commit). True: a
    // tile went back. `doodads`: also every tile of a doodad on protected tiles (doodads_), so the doodad stays whole.
    struct ProtectedBefore {
        TerrainLayer mtxm, tile;
    };
    std::optional<ProtectedBefore> protectedBefore() const;
    bool restoreProtected(const std::optional<ProtectedBefore>& before, bool doodads = true);
    // A map of oldWidth x oldHeight that moved by (dx, dy) tiles (Crop or extend): the protected tiles move with it
    // (shiftProtected) and get their tiles of `before` back.
    void keepProtectedMoved(const std::optional<ProtectedBefore>& before, int oldWidth, int oldHeight, int dx, int dy);
    // A doodad with a tile on protected tiles.
    bool doodadOnProtected(const Doodad& d) const;

    void beginEdit();            // snapshot for undo
    void commit(bool terrain);    // write state into chk, bump revisions
    void reload();                // rebuild state from chk_
    void reloadTerrain();
public:
    // Loads the tileset again (its mod terrains: a terrain rules file changed), then the state from the map.
    void reloadTileset() { loadedEra_ = 0xFFFF, reload(); }
    // Only the first n tile groups (a mod's tileset: the game's own groups, n of them; its own groups past them have the
    // same edge codes and the brush would pick them too). 0: every group. The tileset loads again at the next map.
    void limitGroups(size_t n) { groupLimit_ = n, loadedEra_ = 0xFFFF; }
private:
    uint32_t nextFreeSerial(const std::vector<Unit>& units) const;
    Unit correctedOrSame(Unit u, bool correct, const std::vector<Unit>& against) const;
    // Where a unit put at u's position goes by the placement options: as is (units anywhere, or it fits), else the
    // nearest spot where it fits (`correct`), else nullopt (refused).
    std::optional<Unit> fitted(Unit u, bool correct, const std::vector<Unit>& against) const;

    const GameData& data_;
    std::filesystem::path path_;
    mutable std::vector<ArchiveFile> files_;
    // The archive's files while they are read (after open); filesError_: why they could not be (save refuses then).
    mutable std::future<std::vector<ArchiveFile>> pendingFiles_;
    mutable std::string filesError_;
    void finishFiles() const;
    bool open_ = false, modified_ = false, asArchive_ = false;

    Chk chk_;
    uint16_t width_ = 0, height_ = 0, era_ = 0;
    TerrainLayer terrain_;
    IsomGrid isom_;
    bool isomOk_ = false;
    bool isomRebuilt_ = false;  // the map had no ISOM: reload() made it from the tiles
    std::vector<Unit> units_;
    std::vector<Location> locations_;
    std::vector<Doodad> doodads_;
    std::vector<MapSprite> sprites_;
    StringTable strings_;

    Tileset tileset_;
    UnitTypes types_;
    DoodadTypes doodadTypes_;
    UnitSprites unitSprites_;
    bool allowIllegalUnits_ = false;
    bool revealersHidden_ = false;
    int unitLimit_ = 0;
    bool allowStacking_ = false;
    bool resourceDistance_ = true;
    bool manyStartLocations_ = true;
    int doodadsRemovedByTerrain_ = 0;
    // Per doodad: are all its tiles still in MTXM?
    std::vector<uint8_t> doodadsIntact() const;
    // Doodads intact before an edit whose tiles it replaced are removed (as StarEdit does). With `tileBefore` (an
    // edit of the isometric terrain: TILE, else MTXM, before it): a doodad stays, with its tiles, while the terrain
    // under it still fits it (doodadTerrainHolds); only a doodad whose terrain the edit changed goes.
    int pruneDoodads(const std::vector<uint8_t>& intactBefore, const TerrainLayer* tileBefore = nullptr);
    // The tiles of rects that the corners touch and that the ISOM does not give (tiles placed in Rectangle mode, null
    // tiles) are made again from the ISOM on the next store (GitHub #143).
    void markPlacedTilesDirty(const std::vector<CornerValue>& corners);
    void removeDoodads(const std::vector<int>& indices);  // DD2, overlay sprites, tiles back to TILE
    std::vector<int> placeableUnitsIn(const Rect& area) const;
    void pruneUnits(const std::vector<int>& wasPlaceable);
    int removedByTerrain_ = 0;
    uint16_t loadedEra_ = 0xFFFF;
    std::optional<std::vector<Tileset::ModTerrain>> modTerrainsOverride_;  // (useModTerrains)
    size_t groupLimit_ = 0;  // (limitGroups; 0: every group)
    void buildModTable();
    std::string tilesetError_;     // why the last tileset load failed ("" after a good load)
    bool unitDataLoaded_ = false;  // unit types and sprites are loaded (a failed load is tried again on the next open)
    bool terrainOnly_ = false;     // (setTerrainOnly)
    mutable std::vector<TileGroupEntry> tileGroups_;  // for tileGroupsEra_ (built on first use)
    mutable int tileGroupsEra_ = -1;

    std::vector<int> selection_;
    int selectedLocation_ = -1;
    std::vector<int> selectedLocations_;  // (selectedLocations)
    std::vector<Unit> clipboard_;
    // Undo history: snapshots that share every section unchanged since the previous snapshot, so a
    // step costs only what it changed. Kept up to kUndoSteps steps or kUndoBytes of section data.
    struct Snapshot {
        struct Part {
            SectionName name;
            std::shared_ptr<const Bytes> data;
        };
        std::vector<Part> parts;
        // A step that changed archive files: those files as they were (by path; absent: not in the archive).
        struct File {
            std::string name;
            bool present = false;
            size_t index = 0;  // its place in files_
            Bytes data;
        };
        std::vector<File> files;
        std::shared_ptr<const std::vector<uint8_t>> protect;  // the protected tiles (protected_; null: none)
        size_t ownBytes = 0;  // section and file data this snapshot added (not shared with the one before)
        uint64_t state = 0;   // the map's state (state_) that the snapshot holds
    };
    static Snapshot snapshot(const Chk& chk, const std::deque<Snapshot>& stack);
    static Chk restore(const Snapshot& s);
    void pushUndo(const Chk& chk, const std::vector<std::string>& files = {});  // files: the archive files the step changes
    // The protected tiles now for a snapshot (`prev`'s copy when they are the same); and back from one.
    std::shared_ptr<const std::vector<uint8_t>> protectedSnapshot(const Snapshot* prev) const;
    void restoreProtectedSnapshot(const std::shared_ptr<const std::vector<uint8_t>>& mask);
    std::vector<Snapshot::File> fileStates(const std::vector<std::string>& names) const;
    std::vector<Snapshot::File> fileStates(const std::vector<Snapshot::File>& like) const;  // the same files as now
    void restoreFiles(const std::vector<Snapshot::File>& files);
    // Takes back the last undo step when the edit it was for changed nothing; the redo steps come back too.
    void dropLastUndo();
    void endStrokeStep();  // the end of a stroke: drops its undo step when the stroke changed nothing
    void undoOne();  // one step back (forward) without the reload: undo(n) and redo(n) reload once at the end
    void redoOne();
    std::deque<Snapshot> undo_, redo_;
    size_t undoLimit_ = SIZE_MAX;  // (setUndoLimit)
    std::deque<Snapshot> lastRedo_;  // the redo steps the last undo step cleared (until the next change)
    size_t undoBytes_ = 0;
    bool inStroke_ = false, strokeStep_ = false;  // strokeStep_: the stroke's start took an undo step
    uint16_t strokeAsked_ = 0, strokeVariant_ = 0;  // (brushTerrain: the stroke's terrain and the variant it took)
    static uint64_t nextRevision();
    uint64_t revision_ = nextRevision(), terrainRevision_ = nextRevision();
    uint64_t colorRowsRevision_ = 0;  // (the revision syncColorRows gave unitSprites_ the players' colors at)
    void syncColorRows();
    // The map's state: a new number at each change (an undo or redo gives the number of the state it restores), and the
    // state at the last open or save. Undo or Redo back to that state: the map is not modified.
    uint64_t state_ = nextRevision(), savedState_ = state_;
    std::vector<int> selectionAfterRestore(const std::vector<Unit>& before) const;  // the same units selected (by serial)
    std::vector<std::pair<const char*, double>> openSteps_;
    mutable std::unique_ptr<SettingDefaults> settingDefaults_;
    mutable bool settingDefaultsTried_ = false;
};

}  // namespace stardraft
