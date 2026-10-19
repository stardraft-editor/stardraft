#include "stardraft/editor.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include "byteio.h"
#include "file_write.h"

#include <atomic>
#include <chrono>
#include <stdexcept>

#include <algorithm>
#include <random>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>

#include "stardraft/game_data.h"
#include "stardraft/new_map.h"
#include "stardraft/terrain_gen.h"

namespace stardraft {

namespace {
// Undo history limits: many steps, bounded by the section data they hold (a step shares unchanged
// sections with the step before, so most cost little: a unit move stores only UNIT).
constexpr size_t kUndoSteps = 100000;
constexpr size_t kUndoBytes = size_t(2) << 30;  // 2 GB

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

const char* symmetryName(Symmetry s) {
    switch (s) {
    case Symmetry::None: return "None";
    case Symmetry::Horizontal: return "Mirror left/right";
    case Symmetry::Vertical: return "Mirror top/bottom";
    case Symmetry::Both: return "Mirror both (4-way)";
    case Symmetry::Rotate180: return "Rotate 180";
    case Symmetry::Diagonal: return "Mirror diagonal";
    case Symmetry::AntiDiagonal: return "Mirror anti-diagonal";
    case Symmetry::Rotate4: return "Rotate 4-way";
    }
    return "";
}

Rect Rect::normalized() const {
    return {std::min(left, right), std::min(top, bottom), std::max(left, right), std::max(top, bottom)};
}

Editor::Editor(const GameData& data) : data_(data) {}
Editor::~Editor() = default;

// --- File --------------------------------------------------------------------------------------

bool Editor::open(const std::filesystem::path& path, std::string& error) {
    openSteps_.clear();
    auto t0 = std::chrono::steady_clock::now();
    auto step = [&](const char* what) {
        const auto now = std::chrono::steady_clock::now();
        openSteps_.push_back({what, std::chrono::duration<double, std::milli>(now - t0).count()});
        t0 = now;
    };
    finishFiles();  // a map still being read: done with it first
    Bytes raw;
    bool archive = lower(path.extension().string()) != ".chk";
    if (archive) {
        // The scenario now; the other files (sounds can be megabytes) on a thread, ready when something needs them.
        if (!readMapFile(path, raw, error)) return false;
        step("read the scenario");
    } else {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error = "cannot open " + path.string();
            return false;
        }
        raw.assign(std::istreambuf_iterator<char>(in), {});
    }
    Chk chk = Chk::parse(raw);
    step("parse the scenario");
    uint16_t w = 0, h = 0;
    if (!readDimensions(chk, w, h) || !w || !h) {
        error = "map has no valid DIM section";
        return false;
    }
    if (!unitDataLoaded_) {
        if (!types_.load(data_, error) || !unitSprites_.load(data_, error)) return false;
        unitDataLoaded_ = true;
    }
    step("unit types and sprites");

    chk_ = std::move(chk);
    files_.clear();
    filesError_.clear();
    if (archive)
        pendingFiles_ = std::async(std::launch::async, [path] {
            std::vector<ArchiveFile> files;
            std::string err;
            if (!readMapFiles(path, files, err)) throw std::runtime_error(err);
            return files;
        });
    asArchive_ = archive;
    path_ = path;
    undo_.clear();
    redo_.clear();
    selection_.clear();
    selectedLocation_ = -1;
    reload();
    step("terrain, tileset, units");
    if (tileset_.groups.empty()) {
        error = "could not load tileset " + std::string(tilesetName(era_));
        if (!tilesetError_.empty()) error += ": " + tilesetError_;
        return false;
    }
    open_ = true;
    modified_ = false;
    state_ = savedState_ = nextRevision();
    return true;
}

void Editor::finishFiles() const {
    if (!pendingFiles_.valid()) return;
    try {
        files_ = pendingFiles_.get();
        // The archive holds the scenario too: the editor has its own.
        files_.erase(std::remove_if(files_.begin(), files_.end(), [](const ArchiveFile& f) { return lower(f.name) == lower(kChkArchivePath); }), files_.end());
    } catch (const std::exception& e) {
        filesError_ = std::string("cannot read the map's other files: ") + e.what();
    }
}

bool Editor::newMap(uint16_t width, uint16_t height, uint16_t era, uint16_t terrain, std::string& error) {
    finishFiles();
    Chk chk;
    if (!createNewScenario(chk, data_, width, height, era, terrain, error)) return false;
    if (!unitDataLoaded_) {
        if (!types_.load(data_, error) || !unitSprites_.load(data_, error)) return false;
        unitDataLoaded_ = true;
    }
    chk_ = std::move(chk);
    files_.clear();
    asArchive_ = true;
    path_.clear();
    undo_.clear();
    redo_.clear();
    selection_.clear();
    selectedLocation_ = -1;
    reload();
    open_ = !tileset_.groups.empty();
    // The terrain core makes the tiles as StarEdit's new map does (unless a rect no tile fits: then they stay).
    if (open_ && terrainCoreOn() && core_.load(chk_, tileset_, error) && core_.fill(terrain) && !core_.untileable()) {
        core_.store(chk_);
        reload();
    }
    error.clear();
    modified_ = false;
    state_ = savedState_ = nextRevision();
    if (!open_) {
        error = "could not load tileset " + std::string(tilesetName(era_));
        if (!tilesetError_.empty()) error += ": " + tilesetError_;
    }
    return open_;
}

bool Editor::newMapFromTerrain(const std::filesystem::path& path, std::string& error) {
    Editor src(data_);
    if (!src.open(path, error)) return false;
    if (!newMap(src.width(), src.height(), src.era(), 1, error)) return false;
    for (const SectionName name : {SectionName("ISOM"), SectionName("TILE"), SectionName("MTXM"), SectionName("DD2 ")})
        if (const Section* s = src.chk_.find(name)) chk_.set(name, s->data);
        else if (name == SectionName("TILE")) {  // StarEdit's copy of the tiles: the tiles
            if (const Section* m = src.chk_.find("MTXM")) chk_.set(name, m->data);
        } else if (name == SectionName("ISOM")) chk_.remove(name);  // no ISOM (a protected map): none, not the blank map's
    std::vector<MapSprite> overlays;  // the doodads' overlay sprites: part of their look
    for (const MapSprite& s : src.sprites_)
        for (const Doodad& d : src.doodads_)
            if (const DoodadType* t = src.doodadTypes_.find(d.id);
                t && (s.flags & SpriteFlag::DrawAsSprite) && t->overlaySprite == s.id && d.x == s.x && d.y == s.y) {
                overlays.push_back(s);
                break;
            }
    writeSprites(chk_, overlays);
    reload();
    modified_ = true;
    state_ = nextRevision();
    return true;
}

// Revisions are unique across all editors, so a cache keyed on one (renderer, minimap) can never
// mistake a new document for the one it replaced.
uint64_t Editor::nextRevision() {
    static std::atomic<uint64_t> counter{1};
    return ++counter;
}

bool Editor::generateMap(const TerrainGenParams& params, std::string& error) {
    // A blank map of the chosen tileset (terrain 1 exists in every tileset; the generator repaints it all).
    if (!newMap(params.width, params.height, params.era, 1, error)) return false;
    if (!generateTerrain(chk_, tileset_, params, error)) return false;
    reload();
    modified_ = true;
    state_ = nextRevision();
    return true;
}

bool Editor::save(const std::filesystem::path& path, std::string& error) {
    Bytes raw = chk_.serialize();
    bool archive = lower(path.extension().string()) != ".chk";
    if (archive) {
        finishFiles();
        if (!filesError_.empty()) {
            error = filesError_ + " (saving would lose them)";
            return false;
        }
        if (!writeMap(path, raw, files_, error)) return false;
    } else if (!writeFile(path, std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), error)) {
        return false;
    }
    path_ = path;
    modified_ = false;
    savedState_ = state_;
    return true;
}

void Editor::reloadTerrain() {
    terrain_ = readTerrain(chk_, "MTXM");
    terrainRevision_ = nextRevision();
}

void Editor::reload() {
    readDimensions(chk_, width_, height_);
    era_ = readEra(chk_);
    if (era_ != loadedEra_) {
        std::string err;
        if (tileset_.load(data_, era_, err)) {
            doodadTypes_.load(data_, tileset_, era_, err);
            tilesetError_.clear();
            loadedEra_ = era_;
        } else {
            tilesetError_ = err;
            loadedEra_ = 0xFFFF;  // the next reload tries again
        }
    }
    reloadTerrain();
    std::string err;
    isom_.setTileset(tileset_.groups.empty() ? nullptr : &tileset_);  // (its tiles: StarEdit's, through the terrain core)
    isomOk_ = isom_.load(chk_, err);
    isomRebuilt_ = false;
    if (!isomOk_ && (!chk_.find("ISOM") || chk_.find("ISOM")->data.empty()))  // no ISOM: made from the tiles, so isometric painting works
        isomOk_ = isomRebuilt_ = isom_.rebuildFromTiles(chk_, err);
    units_ = readUnits(chk_);
    locations_ = readLocations(chk_);
    if (locations_.empty()) {
        // No MRGN yet: Brood War / Remastered maps have 255 locations, originals 64.
        const Section* ver = chk_.find("VER ");
        bool bw = ver && ver->data.size() >= 2 && (ver->data[0] | (ver->data[1] << 8)) >= 63;
        locations_.resize(bw ? 255 : 64);
    }
    doodads_ = readDoodads(chk_);
    sprites_ = readSprites(chk_);
    strings_ = StringTable::read(chk_);
    std::erase_if(selection_, [&](int i) { return i < 0 || i >= int(units_.size()); });
    if (selectedLocation_ >= int(locations_.size())) selectedLocation_ = -1;
    revision_ = nextRevision();
}

Editor::Snapshot Editor::snapshot(const Chk& chk, const std::deque<Snapshot>& stack) {
    Snapshot s;
    const Snapshot* prev = stack.empty() ? nullptr : &stack.back();
    for (size_t i = 0; i < chk.sections.size(); ++i) {
        const Section& sec = chk.sections[i];
        // Share the previous snapshot's copy of a section that has not changed since.
        if (prev && i < prev->parts.size() && prev->parts[i].name.c == sec.name.c && *prev->parts[i].data == sec.data) {
            s.parts.push_back(prev->parts[i]);
        } else {
            s.parts.push_back({sec.name, std::make_shared<const Bytes>(sec.data)});
            s.ownBytes += sec.data.size();
        }
    }
    return s;
}

Chk Editor::restore(const Snapshot& s) {
    Chk c;
    for (const auto& p : s.parts) c.sections.push_back({p.name, *p.data});
    return c;
}

std::vector<Editor::Snapshot::File> Editor::fileStates(const std::vector<std::string>& names) const {
    if (names.empty()) return {};  // (a map edit: no wait for the files still being read)
    finishFiles();
    std::vector<Snapshot::File> out;
    for (const std::string& n : names) {
        if (std::any_of(out.begin(), out.end(), [&](const Snapshot::File& f) { return f.name == n; })) continue;
        Snapshot::File f{n};
        for (size_t i = 0; i < files_.size(); ++i)
            if (files_[i].name == n) f.present = true, f.index = i, f.data = files_[i].data;
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<Editor::Snapshot::File> Editor::fileStates(const std::vector<Snapshot::File>& like) const {
    std::vector<std::string> names;
    for (const auto& f : like) names.push_back(f.name);
    return fileStates(names);
}

void Editor::restoreFiles(const std::vector<Snapshot::File>& files) {
    if (files.empty()) return;
    finishFiles();
    for (const auto& f : files)
        std::erase_if(files_, [&](const ArchiveFile& a) { return a.name == f.name; });
    // Back in their places, the first place first, so each index is the one it had.
    std::vector<const Snapshot::File*> back;
    for (const auto& f : files)
        if (f.present) back.push_back(&f);
    std::sort(back.begin(), back.end(), [](const Snapshot::File* a, const Snapshot::File* b) { return a->index < b->index; });
    for (const Snapshot::File* f : back) files_.insert(files_.begin() + long(std::min(f->index, files_.size())), {f->name, f->data});
}

void Editor::pushUndo(const Chk& chk, const std::vector<std::string>& files) {
    undo_.push_back(snapshot(chk, undo_));
    undo_.back().state = state_;
    undo_.back().files = fileStates(files);
    for (const auto& f : undo_.back().files) undo_.back().ownBytes += f.data.size();
    undoBytes_ += undo_.back().ownBytes;
    while (undo_.size() > 1 && (undo_.size() > kUndoSteps || undoBytes_ > kUndoBytes)) {
        undoBytes_ -= std::min(undoBytes_, undo_.front().ownBytes);
        undo_.pop_front();
    }
    lastRedo_ = std::move(redo_);
    redo_.clear();
}

void Editor::dropLastUndo() {
    if (undo_.empty()) return;
    undoBytes_ -= std::min(undoBytes_, undo_.back().ownBytes);
    undo_.pop_back();
    redo_ = std::move(lastRedo_);
    lastRedo_.clear();
}

void Editor::endStrokeStep() {
    const bool took = inStroke_ && strokeStep_;
    inStroke_ = strokeStep_ = false;
    if (!took || undo_.empty()) return;
    // The map as at the stroke's start (every section the same): the stroke placed or painted nothing.
    const Snapshot& s = undo_.back();
    bool same = s.parts.size() == chk_.sections.size();
    for (size_t i = 0; same && i < s.parts.size(); ++i)
        same = s.parts[i].name == chk_.sections[i].name && *s.parts[i].data == chk_.sections[i].data;
    if (same) dropLastUndo();
}

void Editor::beginEdit() {
    if (inStroke_) return;  // part of a stroke: its first edit took the snapshot
    pushUndo(chk_);
}

void Editor::commit(bool terrain) {
    lastRedo_.clear();  // a change: the cleared redo steps are gone for good
    modified_ = true;
    state_ = nextRevision();
    revision_ = nextRevision();
    if (terrain) reloadTerrain();
}

// After an undo or redo: the units that were selected stay selected where they are now (by their serial; a unit that
// is not on the map now is no longer selected), so a selection never points to another unit.
std::vector<int> Editor::selectionAfterRestore(const std::vector<Unit>& before) const {
    std::vector<int> out;
    for (int i : selection_) {
        if (i < 0 || size_t(i) >= before.size()) continue;
        const Unit& was = before[size_t(i)];
        int at = -1;
        if (size_t(i) < units_.size() && units_[size_t(i)].serial == was.serial) at = i;
        for (size_t j = 0; at < 0 && j < units_.size(); ++j)
            if (units_[j].serial == was.serial) at = int(j);
        if (at >= 0 && std::find(out.begin(), out.end(), at) == out.end()) out.push_back(at);
    }
    return out;
}

void Editor::undo(size_t steps) {
    if (undo_.empty() || inStroke_ || !steps) return;  // (a stroke that the mouse still holds: its end comes first)
    const std::vector<Unit> before = units_;
    for (size_t i = 0; i < steps && !undo_.empty(); ++i) undoOne();
    reload();
    selection_ = selectionAfterRestore(before);
    modified_ = state_ != savedState_;
}

void Editor::undoOne() {
    lastRedo_.clear();
    redo_.push_back(snapshot(chk_, redo_));
    redo_.back().state = state_;
    redo_.back().files = fileStates(undo_.back().files);
    for (const auto& f : redo_.back().files) redo_.back().ownBytes += f.data.size();
    restoreFiles(undo_.back().files);
    chk_ = restore(undo_.back());
    state_ = undo_.back().state;
    undoBytes_ -= std::min(undoBytes_, undo_.back().ownBytes);
    undo_.pop_back();
}

void Editor::redo(size_t steps) {
    if (redo_.empty() || inStroke_ || !steps) return;
    const std::vector<Unit> before = units_;
    for (size_t i = 0; i < steps && !redo_.empty(); ++i) redoOne();
    reload();
    selection_ = selectionAfterRestore(before);
    modified_ = state_ != savedState_;
}

void Editor::redoOne() {
    lastRedo_.clear();
    undo_.push_back(snapshot(chk_, undo_));
    undo_.back().state = state_;
    undo_.back().files = fileStates(redo_.back().files);
    for (const auto& f : undo_.back().files) undo_.back().ownBytes += f.data.size();
    undoBytes_ += undo_.back().ownBytes;
    restoreFiles(redo_.back().files);
    chk_ = restore(redo_.back());
    state_ = redo_.back().state;
    redo_.pop_back();
}

// --- Terrain -------------------------------------------------------------------------------------

void Editor::beginTerrainStroke() {
    if (!isomOk_ || inStroke_) return;
    beginEdit();
    inStroke_ = strokeStep_ = true;
}

void Editor::paintTerrain(int px, int py, uint16_t terrain, int brushSize) {
    if (!isomOk_) return;
    bool own = !inStroke_;
    if (own) beginTerrainStroke();
    const int reach = brushSize * 64 + 256;
    const std::vector<int> watch = placeableUnitsIn({px - reach, py - reach, px + reach, py + reach});
    const std::vector<uint8_t> intact = doodadsIntact();
    const TerrainLayer before = readTerrain(chk_, chk_.has("TILE") ? "TILE" : "MTXM");
    if (isom_.paintAtPixel(px, py, terrain, brushSize)) {
        // Tiles under the brush that the ISOM does not give (null tiles and other tiles placed in Rectangle mode) get
        // the terrain's tiles, also where the ISOM terrain is the painted one already (GitHub feedback #118, #143).
        // Only the tiles in the painted diamond (brushSize diamonds of 64 x 32 px), not a box around it.
        const float hw = brushSize * 32.0f, hh = brushSize * 16.0f;
        for (int ty = std::max(0, int(py - hh) / 32); ty <= std::min(int(height_) - 1, int(py + hh) / 32); ++ty)
            for (int tx = std::max(0, int(px - hw) / 32); tx <= std::min(int(width_) - 1, int(px + hw) / 32); ++tx) {
                const float dx = std::abs(tx * 32 + 16 - float(px)) / hw, dy = std::abs(ty * 32 + 16 - float(py)) / hh;
                if (dx + dy <= 1.0f && size_t(ty) * width_ + tx < before.tiles.size() && !tileFromIsom(chk_, before, tx, ty, tileset_))
                    isom_.markRectDirty(tx / 2, ty);
            }
        std::string err;
        isom_.store(chk_, err);
        commit(true);
        pruneDoodads(intact, &before);
        pruneUnits(watch);
    }
    if (own) endTerrainStroke();
}

void Editor::endTerrainStroke() { endStrokeStep(); }

void Editor::beginStroke() {
    if (inStroke_) return;
    beginEdit();
    inStroke_ = strokeStep_ = true;
}

// A stroke that placed or painted nothing leaves no undo step (issue #29).
void Editor::endStroke() { endStrokeStep(); }

// --- Symmetry ------------------------------------------------------------------------------------

std::pair<int, int> Editor::mirror(int px, int py, int which) const {
    const int W = width_ * 32, H = height_ * 32;
    const double nx = double(px) / W, ny = double(py) / H;
    switch (which) {
    case 0: return {W - px, py};                                       // left/right
    case 1: return {px, H - py};                                       // top/bottom
    case 2: return {W - px, H - py};                                   // 180
    case 3: return {int(std::lround(ny * W)), int(std::lround(nx * H))};             // diagonal
    case 4: return {int(std::lround((1 - ny) * W)), int(std::lround((1 - nx) * H))}; // anti-diagonal
    case 5: return {int(std::lround((1 - ny) * W)), int(std::lround(nx * H))};       // rotate 90
    case 6: return {int(std::lround(ny * W)), int(std::lround((1 - nx) * H))};       // rotate 270
    }
    return {px, py};
}

void Editor::setSymmetry(Symmetry s) {
    static const int masks[] = {0, Mirror::LeftRight, Mirror::TopBottom, Mirror::LeftRight | Mirror::TopBottom, Mirror::Rotate180,
                                Mirror::Diagonal, Mirror::AntiDiagonal, Mirror::Rotate90};
    mirrors_ = masks[int(s)];
}

Symmetry Editor::symmetry() const {
    switch (mirrors_) {
    case 0: return Symmetry::None;
    case Mirror::LeftRight: return Symmetry::Horizontal;
    case Mirror::TopBottom: return Symmetry::Vertical;
    case Mirror::Rotate180: return Symmetry::Rotate180;
    case Mirror::Diagonal: return Symmetry::Diagonal;
    case Mirror::AntiDiagonal: return Symmetry::AntiDiagonal;
    case Mirror::Rotate90: return Symmetry::Rotate4;
    default: return Symmetry::Both;  // a mix
    }
}

// The places a point repeats at: the point, then every place the chosen mirrors and turns reach from it and from
// each other (their group), at most 8. Each carries whether it is mirrored across a vertical or horizontal axis.
std::vector<Editor::SymmetricPlace> Editor::symmetricPlaces(int px, int py) const {
    std::vector<SymmetricPlace> out{{px, py, false, false}};
    // Mirror:: flag -> mirror(); and how it flips a pasted copy.
    static const struct {
        int flag, which;
        bool fx, fy;
    } gens[] = {{Mirror::LeftRight, 0, true, false}, {Mirror::TopBottom, 1, false, true}, {Mirror::Rotate180, 2, true, true},
                {Mirror::Diagonal, 3, false, false}, {Mirror::AntiDiagonal, 4, false, false}, {Mirror::Rotate90, 5, false, false}};
    for (size_t k = 0; k < out.size() && out.size() < 8; ++k)
        for (const auto& g : gens) {
            if (!(mirrors_ & g.flag)) continue;
            const auto [x, y] = mirror(out[k].x, out[k].y, g.which);
            const bool seen = std::any_of(out.begin(), out.end(), [&](const SymmetricPlace& p) { return std::abs(p.x - x) <= 1 && std::abs(p.y - y) <= 1; });
            if (!seen && out.size() < 8) out.push_back({x, y, out[k].flipX != g.fx, out[k].flipY != g.fy});
        }
    return out;
}

std::vector<std::pair<int, int>> Editor::symmetricPoints(int px, int py) const {
    std::vector<std::pair<int, int>> out;
    for (const SymmetricPlace& p : symmetricPlaces(px, py)) out.push_back({p.x, p.y});
    return out;
}

// The points a terrain brush paints at. StarEdit's brush is first centered on its lattice point (a diamond or a corner),
// so the mirror images are lattice points too and copy the brush exactly; the other brushes mirror the pointer itself.
std::vector<std::pair<int, int>> Editor::terrainSymmetricPoints(int px, int py, const Brush& brush) const {
    if (brush.starEdit()) {
        const auto [x, y] = starEditCenter(px, py, int(brush.size));
        px = x * 64, py = y * 32;
    }
    return symmetricPoints(px, py);
}

// --- Terrain tools -------------------------------------------------------------------------------

// Units in `area` (pixels) that are placeable now; a terrain edit there removes the ones it rules out
// (see pruneUnits). Empty when illegal placement is allowed.
std::vector<int> Editor::placeableUnitsIn(const Rect& area) const {
    std::vector<int> out;
    if (allowIllegalUnits_ || units_.empty()) return out;
    const PlacementContext ctx = placementContext();
    for (int i = 0; i < int(units_.size()); ++i)
        if (unitBox(units_[i]).intersects(area) && checkPlacement(ctx, units_[i], i).empty()) out.push_back(i);
    return out;
}

void Editor::pruneUnits(const std::vector<int>& wasPlaceable) {
    if (wasPlaceable.empty()) return;
    const PlacementContext ctx = placementContext();
    std::vector<int> gone;
    for (int i : wasPlaceable)
        if (!checkPlacement(ctx, units_[i], i).empty()) gone.push_back(i);
    if (gone.empty()) return;
    for (auto it = gone.rbegin(); it != gone.rend(); ++it) units_.erase(units_.begin() + *it);
    selection_.clear();
    fixAddonLinks();  // an add-on whose building went (or the other way): no link
    writeUnits(chk_, units_);
    removedByTerrain_ += int(gone.size());
    commit(false);
}

void Editor::removeDoodads(const std::vector<int>& indices) {
    if (indices.empty()) return;
    TerrainLayer mtxm = readTerrain(chk_, "MTXM"), tile = readTerrain(chk_, "TILE");
    std::vector<int> order = indices;
    std::sort(order.rbegin(), order.rend());
    for (int i : order) {
        if (i < 0 || i >= int(doodads_.size())) continue;
        const Doodad d = doodads_[size_t(i)];
        if (const DoodadType* t = doodadTypes_.find(d.id)) {
            const int tx = (int(d.x) - t->width * 16) / 32, ty = (int(d.y) - t->height * 16) / 32;
            // Its tiles give way to the terrain underneath (TILE keeps it).
            for (int r = 0; r < t->height; ++r)
                for (int c = 0; c < t->width; ++c) {
                    const int x = tx + c, y = ty + r;
                    if (x < 0 || y < 0 || x >= width_ || y >= height_) continue;
                    const size_t k = size_t(y) * width_ + x;
                    if ((mtxm.tiles[k] >> 4) == t->firstGroup + r && k < tile.tiles.size()) mtxm.tiles[k] = tile.tiles[k];
                }
            // Its overlay sprite: same sprite at the same spot.
            if (t->overlaySprite)
                for (size_t s = 0; s < sprites_.size(); ++s)
                    if ((sprites_[s].flags & SpriteFlag::DrawAsSprite) && sprites_[s].id == t->overlaySprite && sprites_[s].x == d.x &&
                        sprites_[s].y == d.y) {
                        sprites_.erase(sprites_.begin() + long(s));
                        break;
                    }
        }
        doodads_.erase(doodads_.begin() + i);
    }
    Bytes packed;
    for (uint16_t v : mtxm.tiles) appendU16(packed, v);
    chk_.set("MTXM", std::move(packed));
    writeDoodads(chk_, doodads_);
    writeSprites(chk_, sprites_);
}

std::vector<uint8_t> Editor::doodadsIntact() const {
    std::vector<uint8_t> out(doodads_.size(), 0);
    for (size_t i = 0; i < doodads_.size(); ++i) {
        const Doodad& d = doodads_[i];
        const DoodadType* t = doodadTypes_.find(d.id);
        if (!t) continue;
        const int tx = (int(d.x) - t->width * 16) / 32, ty = (int(d.y) - t->height * 16) / 32;
        bool intact = true;
        for (int r = 0; r < t->height && intact; ++r) {
            const size_t g = size_t(t->firstGroup) + r;
            if (g >= tileset_.groups.size()) break;
            for (int c = 0; c < t->width && intact; ++c) {
                const int x = tx + c, y = ty + r;
                if (!tileset_.groups[g].megatiles[size_t(c)] || x < 0 || y < 0 || x >= width_ || y >= height_) continue;
                intact = (terrain_.tiles[size_t(y) * width_ + x] >> 4) == g;
            }
        }
        out[i] = intact;
    }
    return out;
}

bool Editor::doodadTerrainHolds(const Doodad& d, const TerrainLayer& now, const TerrainLayer& before) const {
    const DoodadType* t = doodadTypes_.find(d.id);
    if (!t) return true;
    const DoodadGroundInfo* gi = t->ground.get();
    auto samePiece = [&](size_t a, size_t b) { return a == b || (gi && a < gi->piece.size() && b < gi->piece.size() && gi->piece[a] == gi->piece[b]); };
    const int tx = (int(d.x) - t->width * 16) / 32, ty = (int(d.y) - t->height * 16) / 32;
    for (int r = 0; r < t->height; ++r) {
        const size_t g = size_t(t->firstGroup) + r;
        if (g >= tileset_.groups.size()) break;
        for (int c = 0; c < t->width; ++c) {
            const int x = tx + c, y = ty + r;
            if (!tileset_.groups[g].megatiles[size_t(c)] || x < 0 || y < 0 || x >= width_ || y >= height_) continue;
            const size_t k = size_t(y) * width_ + x;
            if (k >= now.tiles.size() || k >= before.tiles.size()) continue;
            const size_t cell = size_t(r) * t->width + c;
            // (the top row of a taller doodad: as a cell that needs nothing, as doodadFits does not check it)
            const uint16_t need = cell < t->required.size() && (r > 0 || t->height == 1) ? t->required[cell] : 0;
            const size_t under = now.tiles[k] >> 4;
            if (need ? !samePiece(under, need) : !samePiece(under, size_t(before.tiles[k] >> 4))) return false;
        }
    }
    return true;
}

int Editor::pruneDoodads(const std::vector<uint8_t>& intactBefore, const TerrainLayer* tileBefore) {
    const std::vector<uint8_t> now = doodadsIntact();
    std::vector<int> gone, back;
    const TerrainLayer tileNow = tileBefore ? readTerrain(chk_, chk_.has("TILE") ? "TILE" : "MTXM") : TerrainLayer{};
    for (size_t i = 0; i < now.size() && i < intactBefore.size(); ++i)
        if (intactBefore[i] && !now[i]) (tileBefore && doodadTerrainHolds(doodads_[i], tileNow, *tileBefore) ? back : gone).push_back(int(i));
    if (gone.empty() && back.empty()) return 0;
    if (!back.empty()) {  // the terrain under them still fits: their tiles go back over the new terrain
        TerrainLayer mtxm = readTerrain(chk_, "MTXM");
        for (int i : back) {
            const Doodad& d = doodads_[size_t(i)];
            const DoodadType* t = doodadTypes_.find(d.id);
            const int tx = (int(d.x) - t->width * 16) / 32, ty = (int(d.y) - t->height * 16) / 32;
            for (int r = 0; r < t->height; ++r) {
                const size_t g = size_t(t->firstGroup) + r;
                if (g >= tileset_.groups.size()) break;
                for (int c = 0; c < t->width; ++c) {
                    const int x = tx + c, y = ty + r;
                    if (!tileset_.groups[g].megatiles[size_t(c)] || x < 0 || y < 0 || x >= width_ || y >= height_) continue;
                    if (const size_t k = size_t(y) * width_ + x; k < mtxm.tiles.size()) mtxm.tiles[k] = uint16_t(g << 4 | c);
                }
            }
        }
        Bytes packed;
        for (uint16_t v : mtxm.tiles) appendU16(packed, v);
        chk_.set("MTXM", std::move(packed));
    }
    removeDoodads(gone);
    doodadsRemovedByTerrain_ += int(gone.size());
    commit(true);
    return int(gone.size());
}

int Editor::doodadAt(int x, int y) const {
    for (int i = int(doodads_.size()) - 1; i >= 0; --i) {
        const Doodad& d = doodads_[size_t(i)];
        const DoodadType* t = doodadTypes_.find(d.id);
        if (!t) continue;
        const int left = int(d.x) - t->width * 16, top = int(d.y) - t->height * 16;
        if (x < left || y < top || x >= left + t->width * 32 || y >= top + t->height * 32) continue;
        // Only where the doodad has a tile (big doodads have holes).
        const size_t g = size_t(t->firstGroup) + size_t((y - top) / 32);
        if (g < tileset_.groups.size() && tileset_.groups[g].megatiles[size_t((x - left) / 32)]) return i;
    }
    return -1;
}

bool Editor::placeSprite(uint16_t id, int x, int y, uint8_t owner) {
    if (x < 0 || y < 0 || x >= int(width_) * 32 || y >= int(height_) * 32) return false;
    beginEdit();  // one undo step (or part of a stroke: a drag that places many)
    sprites_.push_back({id, uint16_t(x), uint16_t(y), owner, 0, SpriteFlag::DrawAsSprite});
    writeSprites(chk_, sprites_);
    sprites_ = readSprites(chk_);
    commit(false);
    return true;
}

std::vector<int> Editor::doodadsOfType(uint16_t id) const {
    std::vector<int> out;
    for (int i = 0; i < int(doodads_.size()); ++i)
        if (doodads_[size_t(i)].id == id) out.push_back(i);
    return out;
}

bool Editor::spriteIsDoodadOverlay(int index) const {
    if (index < 0 || index >= int(sprites_.size())) return false;
    const MapSprite& s = sprites_[size_t(index)];
    for (const Doodad& d : doodads_)
        if (const DoodadType* t = doodadTypes_.find(d.id); t && t->overlaySprite == s.id && d.x == s.x && d.y == s.y) return true;
    return false;
}

std::vector<int> Editor::spritesOfType(int index) const {
    std::vector<int> out;
    if (index < 0 || index >= int(sprites_.size())) return out;
    const MapSprite like = sprites_[size_t(index)];
    for (int i = 0; i < int(sprites_.size()); ++i) {
        const MapSprite& s = sprites_[size_t(i)];
        if (s.id == like.id && (s.flags & SpriteFlag::DrawAsSprite) == (like.flags & SpriteFlag::DrawAsSprite) && !spriteIsDoodadOverlay(i))
            out.push_back(i);
    }
    return out;
}

int Editor::spriteAt(int x, int y, int radius) const {
    int best = -1, bestD = radius * radius + 1;
    for (size_t i = 0; i < sprites_.size(); ++i) {
        const MapSprite& s = sprites_[i];
        // A doodad's overlay goes with its doodad: not a sprite of its own here.
        bool overlay = false;
        for (const Doodad& d : doodads_)
            if (const DoodadType* t = doodadTypes_.find(d.id); t && t->overlaySprite == s.id && d.x == s.x && d.y == s.y) overlay = true;
        if (overlay) continue;
        const int dx = int(s.x) - x, dy = int(s.y) - y, d2 = dx * dx + dy * dy;
        if (d2 < bestD) bestD = d2, best = int(i);
    }
    return best;
}

bool Editor::deleteSprite(int index) {
    if (index < 0 || index >= int(sprites_.size())) return false;
    const Chk before = chk_;
    sprites_.erase(sprites_.begin() + index);
    writeSprites(chk_, sprites_);
    pushUndo(before);
    sprites_ = readSprites(chk_);
    commit(false);
    return true;
}

bool Editor::moveSprite(int index, int x, int y) {
    if (index < 0 || index >= int(sprites_.size()) || x < 0 || y < 0 || x >= int(width_) * 32 || y >= int(height_) * 32) return false;
    const Chk before = chk_;
    sprites_[size_t(index)].x = uint16_t(x), sprites_[size_t(index)].y = uint16_t(y);
    writeSprites(chk_, sprites_);
    pushUndo(before);
    sprites_ = readSprites(chk_);
    commit(false);
    return true;
}

bool Editor::moveDoodad(int index, int x, int y, bool checkTerrain, std::string* reason) {
    if (index < 0 || index >= int(doodads_.size())) return false;
    const Doodad d = doodads_[size_t(index)];
    const DoodadType* t = doodadTypes_.find(d.id);
    if (!t) return false;
    const Chk before = chk_;
    const std::vector<Doodad> doodadsBefore = doodads_;
    const std::vector<MapSprite> spritesBefore = sprites_;
    removeDoodads({index});
    int tx, ty;
    doodadOrigin(*t, x, y, tx, ty);
    std::string why;
    if (doodadOverlaps(*t, tx, ty)) why = "overlaps another doodad";
    else if (!stardraft::placeDoodad(chk_, *t, tileset_, tx, ty, d.owner, checkTerrain, &why) && why.empty()) why = "does not fit here";
    if (!why.empty()) {  // back as it was
        chk_ = before, doodads_ = doodadsBefore, sprites_ = spritesBefore;
        if (reason) *reason = why;
        return false;
    }
    pushUndo(before);
    doodads_ = readDoodads(chk_);
    sprites_ = readSprites(chk_);
    commit(true);
    return true;
}

bool Editor::moveDoodads(const std::vector<int>& indices, int dx, int dy, bool checkTerrain, std::string* reason) {
    std::vector<int> idx;
    for (int i : indices)
        if (i >= 0 && i < int(doodads_.size())) idx.push_back(i);
    if (idx.empty()) return false;
    std::vector<Doodad> moving;
    for (int i : idx) moving.push_back(doodads_[size_t(i)]);
    const Chk before = chk_;
    const std::vector<Doodad> doodadsBefore = doodads_;
    const std::vector<MapSprite> spritesBefore = sprites_;
    removeDoodads(idx);
    std::string why;
    for (const Doodad& d : moving) {
        const DoodadType* t = doodadTypes_.find(d.id);
        if (!t) continue;
        int tx, ty;
        doodadOrigin(*t, int(d.x) + dx, int(d.y) + dy, tx, ty);
        if (doodadOverlaps(*t, tx, ty)) why = "overlaps another doodad";
        else if (!stardraft::placeDoodad(chk_, *t, tileset_, tx, ty, d.owner, checkTerrain, &why) && why.empty()) why = "does not fit here";
        if (!why.empty()) break;
        doodads_ = readDoodads(chk_);
    }
    if (!why.empty()) {  // all or none: back as it was
        chk_ = before, doodads_ = doodadsBefore, sprites_ = spritesBefore;
        if (reason) *reason = why;
        return false;
    }
    pushUndo(before);
    doodads_ = readDoodads(chk_);
    sprites_ = readSprites(chk_);
    commit(true);
    return true;
}

bool Editor::deleteDoodads(const std::vector<int>& indices) {
    if (indices.empty()) return false;
    beginEdit();
    removeDoodads(indices);
    commit(true);
    return true;
}

int Editor::convertDoodadsToTerrain(const std::vector<int>& indices) {
    std::vector<int> order;
    for (int i : indices)
        if (i >= 0 && i < int(doodads_.size()) && std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
    if (order.empty()) return 0;
    beginEdit();
    TerrainLayer mtxm = readTerrain(chk_, "MTXM"), tile = readTerrain(chk_, "TILE");
    if (!chk_.has("TILE") || tile.tiles.size() != mtxm.tiles.size()) tile = mtxm;  // a map without StarEdit's copy gets one
    std::sort(order.rbegin(), order.rend());
    for (int i : order) {
        const Doodad d = doodads_[size_t(i)];
        if (const DoodadType* t = doodadTypes_.find(d.id)) {
            // Its tiles (the ones that are still the doodad's) go into TILE too: placed tiles, as placeTiles does.
            const int tx = (int(d.x) - t->width * 16) / 32, ty = (int(d.y) - t->height * 16) / 32;
            for (int r = 0; r < t->height; ++r)
                for (int c = 0; c < t->width; ++c) {
                    const int x = tx + c, y = ty + r;
                    if (x < 0 || y < 0 || x >= width_ || y >= height_) continue;
                    const size_t k = size_t(y) * width_ + x;
                    if ((mtxm.tiles[k] >> 4) == t->firstGroup + r) tile.tiles[k] = mtxm.tiles[k];
                }
            // Its overlay sprite stays, as a sprite of its own (a lamp's light, a fire): Sprites can move or delete it
            // later (feedback #53).
        }
        doodads_.erase(doodads_.begin() + i);
    }
    Bytes packed;
    for (uint16_t v : tile.tiles) appendU16(packed, v);
    chk_.set("TILE", std::move(packed));
    writeDoodads(chk_, doodads_);
    writeSprites(chk_, sprites_);
    commit(true);
    return int(order.size());
}

int Editor::placeDoodads(const std::vector<ItemAt>& items, bool checkTerrain, bool nearest) {
    const Chk before = chk_;
    int n = 0;
    for (const ItemAt& it : items) {
        const DoodadType* t = doodadTypes_.find(it.id);
        if (!t) continue;
        int tx, ty;
        doodadOrigin(*t, it.x, it.y, tx, ty);
        if (nearest && (doodadOverlaps(*t, tx, ty) || !doodadFits(chk_, *t, tx, ty, checkTerrain)))
            if (const auto at = nearestDoodadFit(it.id, it.x, it.y, checkTerrain)) doodadOrigin(*t, at->first, at->second, tx, ty);
        if (doodadOverlaps(*t, tx, ty) || !stardraft::placeDoodad(chk_, *t, tileset_, tx, ty, it.owner, checkTerrain)) continue;
        doodads_ = readDoodads(chk_);
        ++n;
    }
    if (!n) return 0;
    pushUndo(before);
    doodads_ = readDoodads(chk_);
    sprites_ = readSprites(chk_);
    commit(true);
    return n;
}

int Editor::replaceDoodads(const std::vector<int>& indices, uint16_t id, bool checkTerrain) {
    const DoodadType* nt = doodadTypes_.find(id);
    if (!nt) return 0;
    std::vector<int> idx;
    for (int i : indices)
        if (i >= 0 && i < int(doodads_.size()) && doodads_[size_t(i)].id != id) idx.push_back(i);
    if (idx.empty()) return 0;
    std::vector<Doodad> old;
    for (int i : idx) old.push_back(doodads_[size_t(i)]);
    const Chk before = chk_;
    removeDoodads(idx);
    int n = 0;
    for (const Doodad& d : old) {
        int tx, ty;
        doodadOrigin(*nt, d.x, d.y, tx, ty);
        if (!doodadOverlaps(*nt, tx, ty) && stardraft::placeDoodad(chk_, *nt, tileset_, tx, ty, d.owner, checkTerrain)) {
            ++n;
        } else if (const DoodadType* ot = doodadTypes_.find(d.id)) {  // the new one does not fit: the old one again
            const int ox = (int(d.x) - ot->width * 16) / 32, oy = (int(d.y) - ot->height * 16) / 32;
            stardraft::placeDoodad(chk_, *ot, tileset_, ox, oy, d.owner, false);
        }
        doodads_ = readDoodads(chk_);
    }
    pushUndo(before);
    doodads_ = readDoodads(chk_);
    sprites_ = readSprites(chk_);
    commit(true);
    return n;
}

int Editor::replaceSprites(const std::vector<int>& indices, uint16_t id) {
    const Chk before = chk_;
    int n = 0;
    for (int i : indices)
        if (i >= 0 && i < int(sprites_.size()) && sprites_[size_t(i)].id != id) sprites_[size_t(i)].id = id, ++n;
    if (!n) return 0;
    writeSprites(chk_, sprites_);
    pushUndo(before);
    sprites_ = readSprites(chk_);
    commit(false);
    return n;
}

bool Editor::moveSprites(const std::vector<int>& indices, int dx, int dy) {
    const Chk before = chk_;
    bool any = false;
    for (int i : indices) {
        if (i < 0 || i >= int(sprites_.size())) continue;
        const int x = int(sprites_[size_t(i)].x) + dx, y = int(sprites_[size_t(i)].y) + dy;
        if (x < 0 || y < 0 || x >= int(width_) * 32 || y >= int(height_) * 32) continue;
        sprites_[size_t(i)].x = uint16_t(x), sprites_[size_t(i)].y = uint16_t(y), any = true;
    }
    if (!any) return false;
    writeSprites(chk_, sprites_);
    pushUndo(before);
    sprites_ = readSprites(chk_);
    commit(false);
    return true;
}

bool Editor::deleteSprites(const std::vector<int>& indices) {
    std::vector<int> idx;
    for (int i : indices)
        if (i >= 0 && i < int(sprites_.size())) idx.push_back(i);
    if (idx.empty()) return false;
    std::sort(idx.rbegin(), idx.rend());
    const Chk before = chk_;
    for (int i : idx) sprites_.erase(sprites_.begin() + i);
    writeSprites(chk_, sprites_);
    pushUndo(before);
    sprites_ = readSprites(chk_);
    commit(false);
    return true;
}

int Editor::placeSprites(const std::vector<ItemAt>& items) {
    const Chk before = chk_;
    int n = 0;
    for (const ItemAt& it : items)
        if (it.x >= 0 && it.y >= 0 && it.x < int(width_) * 32 && it.y < int(height_) * 32)
            sprites_.push_back({it.id, uint16_t(it.x), uint16_t(it.y), it.owner, 0, SpriteFlag::DrawAsSprite}), ++n;
    if (!n) return 0;
    writeSprites(chk_, sprites_);
    pushUndo(before);
    sprites_ = readSprites(chk_);
    commit(false);
    return n;
}

bool Editor::deleteDoodad(int index) {
    if (index < 0 || index >= int(doodads_.size())) return false;
    beginEdit();
    removeDoodads({index});
    commit(true);
    return true;
}

// The diamonds whose four corners `corners` sets, when they are all one terrain and those diamonds hold every corner
// (the terrain core paints diamonds); else empty (a corner alone, as a half-size brush sets: the old engine paints it).
static std::vector<std::pair<int, int>> wholeDiamonds(const IsomGrid& grid, const std::vector<CornerValue>& corners) {
    std::set<std::pair<int, int>> set;
    for (const auto& c : corners) {
        if (c.terrain != corners.front().terrain) return {};
        if (grid.isCorner(c.x, c.y)) set.insert({c.x, c.y});
    }
    auto whole = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < grid.rectCols() && y < grid.rectRows() && set.count({x - 1, y}) && set.count({x + 1, y}) &&
               set.count({x, y - 1}) && set.count({x, y + 1});
    };
    std::set<std::pair<int, int>> diamonds;
    for (auto [x, y] : set) {
        bool held = false;
        for (auto [dx, dy] : {std::pair{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}})
            if (whole(dx, dy)) diamonds.insert({dx, dy}), held = true;
        if (!held) return {};
    }
    return {diamonds.begin(), diamonds.end()};
}

bool Editor::applyCore(const std::function<bool()>& paint, const std::function<bool()>& repair, int x0, int y0, int x1, int y1) {
    std::string err;
    if (!core_.load(chk_, tileset_, err)) return repair();
    const std::vector<int> watch = placeableUnitsIn({x0 * 64 - 256, y0 * 32 - 256, x1 * 64 + 256, y1 * 32 + 256});
    const std::vector<uint8_t> intact = doodadsIntact();
    const TerrainLayer before = readTerrain(chk_, chk_.has("TILE") ? "TILE" : "MTXM");
    // (The core makes the tiles of each diamond it paints again, also where its terrain does not change: the tiles
    // placed in Rectangle mode there are replaced as StarEdit replaces them, GitHub #143.)
    if (!paint()) return false;
    // A rect that no tile group fits (StarEdit's rule leaves the null tile there; on maps whose ISOM StarEdit would not
    // make): the old engine makes this edit instead, as it keeps every rect tileable. The map is still as it was.
    if (core_.untileable()) return repair();
    core_.store(chk_);
    if (!isom_.load(chk_, err)) isomOk_ = false;  // (the core writes a full ISOM: this does not fail)
    commit(true);
    // The core does not check doodads: a doodad stays, with its tiles, while the terrain under it still fits it.
    pruneDoodads(intact, &before);
    pruneUnits(watch);
    return true;
}

bool Editor::applyTerrain(const std::vector<CornerValue>& corners, bool overPlaced) {
    if (!isomOk_ || corners.empty()) return false;
    // The repair around an edit stays within a few tiles of it.
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const auto& c : corners) x0 = std::min(x0, c.x), y0 = std::min(y0, c.y), x1 = std::max(x1, c.x), y1 = std::max(y1, c.y);
    // Whole diamonds of one terrain: the terrain core paints them as StarEdit's brush would.
    // The core makes the tiles of each diamond it paints again: other edits than the brush's (fill, replace) give it only
    // the diamonds whose terrain changes, so that the tiles placed in Rectangle mode stay where the terrain stays.
    if (terrainCoreOn()) {
        auto diamonds = wholeDiamonds(isom_, corners);
        const uint16_t terrain = corners.front().terrain;
        if (!overPlaced)
            std::erase_if(diamonds, [&](const std::pair<int, int>& d) {
                auto [x, y] = d;
                return isom_.terrainAt(x - 1, y) == terrain && isom_.terrainAt(x + 1, y) == terrain && isom_.terrainAt(x, y - 1) == terrain &&
                       isom_.terrainAt(x, y + 1) == terrain;
            });
        if (!diamonds.empty())
            return applyCore([&] { return core_.paintDiamonds(diamonds, terrain); },
                             [&] { return applyOld(corners, x0, y0, x1, y1, overPlaced); }, x0, y0, x1, y1);
    }
    return applyOld(corners, x0, y0, x1, y1, overPlaced);
}

bool Editor::applyOld(const std::vector<CornerValue>& corners, int x0, int y0, int x1, int y1, bool overPlaced) {
    const std::vector<int> watch = placeableUnitsIn({x0 * 64 - 256, y0 * 32 - 256, x1 * 64 + 256, y1 * 32 + 256});
    const std::vector<uint8_t> intact = doodadsIntact();
    const TerrainLayer before = readTerrain(chk_, chk_.has("TILE") ? "TILE" : "MTXM");
    if (!isom_.apply(corners)) return false;
    if (overPlaced) markPlacedTilesDirty(corners);
    std::string err;
    isom_.store(chk_, err);
    commit(true);
    pruneDoodads(intact, &before);
    pruneUnits(watch);
    return true;
}

void Editor::markPlacedTilesDirty(const std::vector<CornerValue>& corners) {
    const TerrainLayer tiles = readTerrain(chk_, chk_.has("TILE") ? "TILE" : "MTXM");
    std::set<std::pair<int, int>> rects;
    for (const auto& c : corners)  // a corner gives the ISOM of the four rects around it
        for (int ry = c.y - 1; ry <= c.y; ++ry)
            for (int rx = c.x - 1; rx <= c.x; ++rx)
                if (rx >= 0 && ry >= 0 && rx * 2 < int(width_) && ry < int(height_)) rects.insert({rx, ry});
    for (auto [rx, ry] : rects)
        for (int tx = rx * 2; tx <= rx * 2 + 1 && tx < int(width_); ++tx)
            if (!tileFromIsom(chk_, tiles, tx, ry, tileset_)) {
                isom_.markRectDirty(rx, ry);
                break;
            }
}

uint16_t Editor::terrainAt(int px, int py) const {
    int x, y;
    isom_.cornerAtPixel(px, py, x, y);
    return isom_.terrainAt(x, y);
}

bool Editor::terrainCoreOn() {
    static const bool on = [] {
        const char* v = std::getenv("STARDRAFT_TERRAIN_CORE");
        return !(v && std::string(v) == "0");
    }();
    return on;
}

bool Editor::applyCoreBrush(int px, int py, uint16_t terrain, int extent) {
    // Each symmetric point's block of diamonds (starEditBrushAt).
    std::vector<std::vector<std::pair<int, int>>> blocks;
    for (auto [x, y] : terrainSymmetricPoints(px, py, Brush{Brush::Diamond, float(extent)})) {
        auto block = starEditBrushAt(x, y, extent);
        if (block.empty()) return false;
        if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) blocks.push_back(std::move(block));
    }
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const auto& b : blocks)
        for (auto [dx, dy] : b) x0 = std::min(x0, dx), y0 = std::min(y0, dy), x1 = std::max(x1, dx), y1 = std::max(y1, dy);
    const int reach = 1;  // (in grid steps: the diamonds around the blocks)
    return applyCore(
        [&] {
            bool painted = false;
            for (const auto& b : blocks) painted = core_.paintDiamonds(b, terrain) || painted;
            return painted;
        },
        [&] {  // (the old engine: the same diamonds' corners; the brush's tiles replace the placed tiles, GitHub #143)
            std::vector<CornerValue> corners;
            for (auto [x, y] : terrainSymmetricPoints(px, py, Brush{Brush::Diamond, float(extent)}))
                for (auto [cx, cy] : isom_.brushCorners(x, y, Brush{Brush::Diamond, float(extent)})) corners.push_back({cx, cy, terrain});
            return applyOld(corners, x0 - reach, y0 - reach, x1 + reach, y1 + reach, true);
        },
        x0 - reach, y0 - reach, x1 + reach, y1 + reach);
}

bool Editor::paintBrush(int px, int py, uint16_t terrain, const Brush& brush) {
    if (!isomOk_) return false;
    bool own = !inStroke_;
    if (own) beginTerrainStroke();
    bool ok;
    // The brush's tiles replace the tiles placed in Rectangle mode where it paints (GitHub #143): overPlaced.
    // StarEdit's own brush (a diamond of whole diamonds, as wide as high): the terrain core paints it as StarEdit does.
    if (terrainCoreOn() && brush.starEdit()) {
        ok = applyCoreBrush(px, py, terrain, int(brush.size));
    } else {
        std::vector<CornerValue> corners;
        for (auto [x, y] : terrainSymmetricPoints(px, py, brush))
            for (auto [cx, cy] : isom_.brushCorners(x, y, brush)) corners.push_back({cx, cy, terrain});
        ok = applyTerrain(corners, true);
    }
    if (own) endTerrainStroke();
    return ok;
}

bool Editor::brushAllTerrain(int px, int py, const Brush& brush, uint16_t terrain) const {
    if (!isomOk_) return false;
    for (auto [x, y] : terrainSymmetricPoints(px, py, brush))
        for (auto [cx, cy] : isom_.brushCorners(x, y, brush))
            if (isom_.isCorner(cx, cy) && isom_.terrainAt(cx, cy) != terrain) return false;
    return true;
}

int Editor::rerollTerrain(int px, int py, const Brush& brush, uint32_t seed) {
    TerrainLayer tile = readTerrain(chk_, "TILE"), mtxm = readTerrain(chk_, "MTXM");
    if (tile.tiles.empty()) return 0;
    const int w = tile.width, h = tile.height;
    auto group = [&](int tx, int ty) -> const TileGroup* {
        if (tx < 0 || ty < 0 || tx >= w || ty >= h) return nullptr;
        return tileset_.groupFor(tile.tiles[size_t(ty) * w + tx]);
    };
    // CV5 field 8 (extra[1]) is a tile's top stack connection, field 10 (extra[3]) its bottom one.
    auto stacked = [&](int tx, int ty) {  // does (tx, ty) sit in a stack under (tx, ty - 1)?
        const TileGroup *up = group(tx, ty - 1), *me = group(tx, ty);
        return up && me && (tile.tiles[size_t(ty - 1) * w + tx] >> 4) < 1024 && up->extra[3] && me->extra[1];
    };
    auto hasSub = [&](int tx, int ty, int sub) {
        const TileGroup* g = group(tx, ty);
        return g && (sub == 0 || g->megatiles[size_t(sub)] != 0);
    };
    // Rects under the brush (a corner touches the rects around it).
    std::set<std::pair<int, int>> rects;
    for (auto [x, y] : terrainSymmetricPoints(px, py, brush))
        for (auto [cx, cy] : isom_.brushCorners(x, y, brush))
            for (int ry = cy - 1; ry <= cy; ++ry)
                for (int rx = cx - 1; rx <= cx; ++rx)
                    if (rx >= 0 && ry >= 0 && rx * 2 < w && ry < h) rects.insert({rx, ry});
    std::mt19937 rng(seed);
    std::set<std::pair<int, int>> done;  // stack tops already re-rolled
    int changed = 0;
    for (auto [rx, ry] : rects) {
        // Climb to the top of a cliff stack: its variant is what the pieces below repeat.
        int top = ry;
        while (top > 0 && stacked(rx * 2, top)) --top;
        if (!done.insert({rx, top}).second) continue;
        const TileGroup* g = group(rx * 2, top);
        // Doodads keep their tiles: a doodad's tile is in MTXM (TILE has the terrain under it).
        if (!g || (tile.tiles[size_t(top) * w + rx * 2] >> 4) >= 1024 || (mtxm.tiles[size_t(top) * w + rx * 2] >> 4) >= 1024) continue;
        if (!tileFromIsom(chk_, tile, rx * 2, top, tileset_)) continue;                       // and placed tiles theirs
        std::vector<int> subs;
        const int cur = tile.tiles[size_t(top) * w + rx * 2] & 15;
        for (int s = 0; s < 16; ++s)
            if (hasSub(rx * 2, top, s) && hasSub(rx * 2 + 1, top, s) && s != cur) subs.push_back(s);
        if (subs.empty()) continue;
        const int sub = subs[rng() % subs.size()];
        // Both tiles of the rect, then down the stack while pieces stack and have the variant.
        for (int ty = top; ty < h; ++ty) {
            if (ty > top && !(stacked(rx * 2, ty) && hasSub(rx * 2, ty, sub))) break;
            for (int tx = rx * 2; tx <= rx * 2 + 1 && tx < w; ++tx) {
                if (ty > top && !stacked(tx, ty)) continue;
                if (!hasSub(tx, ty, sub)) continue;
                if ((tx != rx * 2 || ty != top) && !tileFromIsom(chk_, tile, tx, ty, tileset_)) continue;  // placed tiles stay
                size_t i = size_t(ty) * w + tx;
                if ((mtxm.tiles[i] >> 4) >= 1024) continue;  // under a doodad: the doodad's tile stays
                uint16_t v = uint16_t((tile.tiles[i] & 0xFFF0) | sub);
                if (tile.tiles[i] != v) tile.tiles[i] = mtxm.tiles[i] = v, ++changed;
            }
        }
    }
    if (!changed) return 0;  // no undo step
    beginEdit();             // in a stroke (a brush drag): the stroke's one step
    auto pack = [](const TerrainLayer& t) {
        Bytes out;
        for (uint16_t v : t.tiles) appendU16(out, v);
        return out;
    };
    chk_.set("TILE", pack(tile));
    chk_.set("MTXM", pack(mtxm));
    commit(true);
    return changed;
}

// --- Tiles (rectangular terrain) -----------------------------------------------------------------

const std::vector<Editor::TileGroupEntry>& Editor::tileGroups() const {
    if (tileGroupsEra_ == int(era_) && !tileGroups_.empty()) return tileGroups_;
    tileGroups_.clear();
    tileGroupsEra_ = era_;
    const auto terrains = tileGroupTerrains(tileset_);
    // The doodad (and its row) that uses each group.
    std::vector<std::pair<int, int>> doodadOf(tileset_.groups.size(), {-1, 0});
    for (const auto& d : doodadTypes_.all())
        for (int r = 0; r < d.height; ++r)
            if (size_t(d.firstGroup) + r < doodadOf.size() && doodadOf[size_t(d.firstGroup) + r].first < 0)
                doodadOf[size_t(d.firstGroup) + r] = {d.id, r};
    for (size_t g = 0; g < tileset_.groups.size() && g < 4096; ++g) {
        TileGroupEntry e;
        e.group = uint16_t(g);
        for (int s = 0; s < 16; ++s) {
            const uint16_t m = tileset_.groups[g].megatiles[size_t(s)];
            if (m && m < tileset_.megatiles.size()) e.subtiles |= uint16_t(1u << s);
        }
        if (!e.subtiles) continue;  // an empty group: nothing to place
        e.terrains = terrains[g];
        e.doodad = doodadOf[g].first, e.doodadRow = doodadOf[g].second;
        tileGroups_.push_back(e);
    }
    return tileGroups_;
}

int Editor::placeTiles(int tx0, int ty0, int tx1, int ty1, uint16_t value) {
    if (tx0 > tx1) std::swap(tx0, tx1);
    if (ty0 > ty1) std::swap(ty0, ty1);
    std::vector<std::pair<int, int>> cells;
    for (int y = std::max(ty0, 0); y <= std::min(ty1, int(height_) - 1); ++y)
        for (int x = std::max(tx0, 0); x <= std::min(tx1, int(width_) - 1); ++x) cells.push_back({x, y});
    return placeTileCells(cells, value);
}

int Editor::placeTileCells(const std::vector<std::pair<int, int>>& cells, uint16_t value) {
    std::vector<TileAt> v;
    v.reserve(cells.size());
    for (auto [x, y] : cells) v.push_back({x, y, value});
    return placeTileValues(v);
}

int Editor::placeTileValues(const std::vector<TileAt>& cells) {
    if (!open_) return 0;
    TerrainLayer mtxm = readTerrain(chk_, "MTXM"), tile = readTerrain(chk_, "TILE");
    if (!chk_.has("TILE")) tile = mtxm;  // a map without StarEdit's copy gets one
    std::vector<std::pair<size_t, uint16_t>> todo;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const TileAt& c : cells) {
        if (c.x < 0 || c.y < 0 || c.x >= int(width_) || c.y >= int(height_)) continue;
        if (!tileset_.groupFor(c.value) || tileset_.megatileFor(c.value) < 0) continue;
        const size_t i = size_t(c.y) * width_ + c.x;
        if (mtxm.tiles[i] == c.value && tile.tiles[i] == c.value) continue;
        todo.push_back({i, c.value});
        x0 = std::min(x0, c.x), y0 = std::min(y0, c.y), x1 = std::max(x1, c.x), y1 = std::max(y1, c.y);
    }
    if (todo.empty()) return 0;
    beginEdit();
    const std::vector<int> watch = placeableUnitsIn({x0 * 32 - 64, y0 * 32 - 64, (x1 + 1) * 32 + 64, (y1 + 1) * 32 + 64});
    const std::vector<uint8_t> intact = doodadsIntact();
    std::vector<uint8_t> seen(mtxm.tiles.size(), 0);
    int changed = 0;
    for (auto [i, v] : todo) {
        mtxm.tiles[i] = tile.tiles[i] = v;
        if (!seen[i]) seen[i] = 1, ++changed;
    }
    auto pack = [](const TerrainLayer& t) {
        Bytes out;
        out.reserve(t.tiles.size() * 2);
        for (uint16_t v : t.tiles) appendU16(out, v);
        return out;
    };
    // Both sections, so StarEdit (which reads TILE) and the game (MTXM) show the same; the ISOM stays as it is.
    chk_.set("MTXM", pack(mtxm));
    chk_.set("TILE", pack(tile));
    commit(true);
    pruneDoodads(intact);
    pruneUnits(watch);
    return changed;
}

bool Editor::copyTiles(int tx0, int ty0, int tx1, int ty1, SelectShape shape) {
    if (!open_) return false;
    if (tx0 > tx1) std::swap(tx0, tx1);
    if (ty0 > ty1) std::swap(ty0, ty1);
    tx0 = std::max(tx0, 0), ty0 = std::max(ty0, 0), tx1 = std::min(tx1, int(width_) - 1), ty1 = std::min(ty1, int(height_) - 1);
    if (tx0 > tx1 || ty0 > ty1) return false;
    const TerrainLayer mtxm = readTerrain(chk_, "MTXM");
    tileClip_.width = tx1 - tx0 + 1, tileClip_.height = ty1 - ty0 + 1;
    tileClip_.values.clear();
    const Rect box{tx0 * 32, ty0 * 32, (tx1 + 1) * 32, (ty1 + 1) * 32};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)  // a tile outside the shape: 0xFFFF, not pasted
            tileClip_.values.push_back(inSelectShape(box, shape, x * 32 + 16, y * 32 + 16) ? mtxm.tiles[size_t(y) * width_ + x] : uint16_t(0xFFFF));
    return true;
}

int Editor::pasteTiles(int tx, int ty) {
    std::vector<TileAt> cells;
    cells.reserve(tileClip_.values.size());
    for (int y = 0; y < tileClip_.height; ++y)
        for (int x = 0; x < tileClip_.width; ++x)
            if (const uint16_t v = tileClip_.values[size_t(y) * tileClip_.width + x]; v != 0xFFFF) cells.push_back({tx + x, ty + y, v});
    return placeTileValues(cells);
}

void Editor::sprayTerrain(int px, int py, uint16_t terrain, const Brush& brush, int dabs, uint32_t seed) {
    if (!isomOk_) return;
    bool own = !inStroke_;
    if (own) beginTerrainStroke();
    std::mt19937 rng(seed);
    const double w = std::max(0.5, double(brush.size)) * 64, h = std::max(0.5, double(brush.height())) * 32;
    std::uniform_real_distribution<double> unit(-1, 1);
    std::vector<CornerValue> corners;
    for (int i = 0; i < dabs; ++i) {
        // Random point inside the brush shape, then a single-corner dab there.
        double dx, dy;
        do dx = unit(rng), dy = unit(rng);
        while (brush.shape == Brush::Diamond ? std::abs(dx) + std::abs(dy) > 1 : brush.shape == Brush::Circle && dx * dx + dy * dy > 1);
        for (auto [x, y] : symmetricPoints(int(px + dx * w), int(py + dy * h)))
            for (auto [cx, cy] : isom_.brushCorners(x, y, Brush{Brush::Diamond, 0.5f})) corners.push_back({cx, cy, terrain});
    }
    applyTerrain(corners);
    if (own) endTerrainStroke();
}

bool Editor::fillTerrain(int px, int py, uint16_t terrain) {
    if (!isomOk_) return false;
    std::vector<CornerValue> corners;
    for (auto [x, y] : symmetricPoints(px, py))
        for (auto [cx, cy] : isom_.floodCorners(x, y)) corners.push_back({cx, cy, terrain});
    if (corners.empty()) return false;
    beginEdit();
    if (!applyTerrain(corners)) {
        dropLastUndo();  // nothing changed
        return false;
    }
    return true;
}

int Editor::replaceTerrain(uint16_t from, uint16_t to, const Rect* area, int areaShape) {
    if (!isomOk_ || from == to) return 0;
    Rect r = area ? area->normalized() : Rect{-64, -32, width_ * 32 + 64, height_ * 32 + 64};
    std::vector<CornerValue> corners;
    for (auto [x, y] : isom_.cornersInRect(r.left, r.top, r.right, r.bottom))
        if (isom_.terrainAt(x, y) == from && (!area || inSelectShape(r, SelectShape(areaShape), x * 64, y * 32))) corners.push_back({x, y, to});
    if (corners.empty()) return 0;
    beginEdit();
    if (!applyTerrain(corners)) {
        dropLastUndo();  // nothing changed
        return 0;
    }
    return int(corners.size());
}

bool Editor::inSelectShape(const Rect& box, SelectShape shape, int px, int py) {
    const Rect r = box.normalized();
    if (shape == SelectShape::Rect) return r.contains(px, py);
    const double hw = std::max(1.0, (r.right - r.left) / 2.0), hh = std::max(1.0, (r.bottom - r.top) / 2.0);
    const double dx = (px - (r.left + r.right) / 2.0) / hw, dy = (py - (r.top + r.bottom) / 2.0) / hh;
    return shape == SelectShape::Diamond ? std::abs(dx) + std::abs(dy) <= 1.0 : dx * dx + dy * dy <= 1.0;
}

std::vector<std::pair<int, int>> Editor::cornersInShape(const Rect& area, SelectShape shape) const {
    if (!isomOk_) return {};
    const Rect r = area.normalized();
    auto cs = isom_.cornersInRect(r.left, r.top, r.right, r.bottom);
    std::erase_if(cs, [&](const auto& c) { return !inSelectShape(r, shape, c.first * 64, c.second * 32); });
    return cs;
}

bool Editor::copyTerrain(const Rect& area, bool withUnits, bool withDoodads, SelectShape shape) {
    if (!isomOk_) return false;
    Rect r = area.normalized();
    auto cs = cornersInShape(r, shape);
    if (cs.empty()) return false;
    // Origin: the top-left-most corner; offsets keep the corner lattice parity.
    int ox = cs.front().first, oy = cs.front().second;
    for (auto [x, y] : cs)
        if (y < oy || (y == oy && x < ox)) ox = x, oy = y;
    TerrainClip clip;
    for (auto [x, y] : cs) clip.corners.push_back({x - ox, y - oy, isom_.terrainAt(x, y)});
    clip.width = r.right - r.left, clip.height = r.bottom - r.top;
    const int opx = ox * 64, opy = oy * 32;
    if (withUnits)
        for (Unit u : units_)
            if (inSelectShape(r, shape, u.x, u.y)) {
                u.x = uint16_t(int(u.x) - opx), u.y = uint16_t(int(u.y) - opy);  // signed offsets
                u.relatedSerial = 0, u.links = 0;
                clip.units.push_back(u);
            }
    if (withDoodads)
        for (Doodad d : doodads_)
            if (inSelectShape(r, shape, d.x, d.y)) {
                d.x = uint16_t(int(d.x) - opx), d.y = uint16_t(int(d.y) - opy);
                clip.doodads.push_back(d);
            }
    terrainClip_ = std::move(clip);
    return true;
}

int Editor::rotateTerrainClipboard() {
    TerrainClip& c = terrainClip_;
    if (c.corners.empty()) return 0;
    // In rect-grid coordinates a quarter turn is (x, y) -> (-y, x); x + y keeps its parity, so corners
    // stay corners. Then shift so the top-left-most corner is the origin again (an even offset).
    for (auto& v : c.corners) std::tie(v.x, v.y) = std::pair{-v.y, v.x};
    int ox = c.corners.front().x, oy = c.corners.front().y;
    for (const auto& v : c.corners)
        if (v.y < oy || (v.y == oy && v.x < ox)) ox = v.x, oy = v.y;
    for (auto& v : c.corners) v.x -= ox, v.y -= oy;
    // Units: pixel offsets (px, py) are rect-grid (px / 64, py / 32); turned and shifted the same way.
    for (Unit& u : c.units) {
        const int px = int16_t(u.x), py = int16_t(u.y);
        const int nx = -py * 2 - ox * 64, ny = px / 2 - oy * 32;
        u.x = uint16_t(int16_t(nx)), u.y = uint16_t(int16_t(ny));
    }
    const int dropped = int(c.doodads.size());
    c.doodads.clear();
    std::swap(c.width, c.height);
    c.width *= 2, c.height /= 2;  // 64 x 32 px per rect-grid step: the turned box is twice as wide per height
    return dropped;
}

std::vector<std::pair<int, int>> Editor::terrainPasteCorners(int px, int py) const {
    std::vector<std::pair<int, int>> out;
    if (!isomOk_) return out;
    int x, y;
    isom_.cornerAtPixel(px, py, x, y);
    for (const auto& c : terrainClip_.corners) {
        int cx = x + c.x, cy = y + c.y;
        if ((cx + cy) & 1) out.push_back({cx, cy});
    }
    return out;
}

bool Editor::pasteTerrain(int px, int py, bool correct) {
    if (!isomOk_ || terrainClip_.corners.empty()) return false;
    beginEdit();
    std::vector<std::pair<int, int>> origins;  // corner-lattice origin of each (mirrored) copy
    std::vector<std::pair<int, int>> flips;    // (flipX, flipY) per copy
    const auto places = symmetricPlaces(px, py);
    std::vector<std::pair<int, int>> pts;
    for (const auto& p : places) pts.push_back({p.x, p.y});
    for (size_t k = 0; k < pts.size(); ++k) {
        // Mirror the clipboard for mirror images across a vertical or horizontal axis; diagonals and quarter turns
        // only move it.
        const int fx = places[k].flipX ? 1 : 0, fy = places[k].flipY ? 1 : 0;
        int x, y;
        isom_.cornerAtPixel(pts[k].first - (fx ? terrainClip_.width : 0), pts[k].second - (fy ? terrainClip_.height : 0), x, y);
        origins.push_back({x, y});
        flips.push_back({fx, fy});
    }
    std::vector<CornerValue> corners;
    int spanX = 0, spanY = 0;
    for (const auto& c : terrainClip_.corners) spanX = std::max(spanX, c.x), spanY = std::max(spanY, c.y);
    for (size_t k = 0; k < origins.size(); ++k)
        for (const auto& c : terrainClip_.corners) {
            int dx = flips[k].first ? spanX - c.x : c.x, dy = flips[k].second ? spanY - c.y : c.y;
            int x = origins[k].first + dx, y = origins[k].second + dy;
            if (!((x + y) & 1)) ++x;  // corners have x + y odd; a flip can break that
            corners.push_back({x, y, c.terrain});
        }
    if (!isom_.apply(corners)) {
        dropLastUndo();  // nothing changed
        return false;
    }
    std::string err;
    isom_.store(chk_, err);
    // Doodads and units after the terrain (tile regeneration rewrites MTXM).
    for (size_t k = 0; k < origins.size(); ++k) {
        const int opx = origins[k].first * 64, opy = origins[k].second * 32;
        for (const Doodad& d : terrainClip_.doodads)
            if (const DoodadType* t = doodadTypes_.find(d.id)) {
                int cx = opx + int16_t(d.x), cy = opy + int16_t(d.y);
                int tx, ty;
                doodadOrigin(*t, cx, cy, tx, ty);
                stardraft::placeDoodad(chk_, *t, tileset_, tx, ty, d.owner, false);
            }
    }
    reloadTerrain();  // the unit check reads terrain_; the store and the doodads changed MTXM
    for (size_t k = 0; k < origins.size(); ++k) {
        const int opx = origins[k].first * 64, opy = origins[k].second * 32;
        for (Unit u : terrainClip_.units) {
            if (unitLimit_ > 0 && int(units_.size()) >= unitLimit_) break;
            int ux = opx + int16_t(u.x), uy = opy + int16_t(u.y);
            if (ux < 0 || uy < 0 || ux >= width_ * 32 || uy >= height_ * 32) continue;
            u.x = uint16_t(ux), u.y = uint16_t(uy);
            std::optional<Unit> f = fitted(u, correct, units_);
            if (!f) continue;  // no spot where it fits: not pasted, as placing refuses
            f->serial = nextFreeSerial(units_);
            units_.push_back(*f);
        }
    }
    writeUnits(chk_, units_);
    doodads_ = readDoodads(chk_);
    sprites_ = readSprites(chk_);
    commit(true);
    return true;
}

// --- Map properties --------------------------------------------------------------------------------

namespace {
uint16_t sprpString(const Chk& chk, int which) {
    const Section* s = chk.find("SPRP");
    return s && s->data.size() >= 4 ? uint16_t(s->data[size_t(which) * 2] | s->data[size_t(which) * 2 + 1] << 8) : 0;
}
}  // namespace

std::string Editor::scenarioName() const {
    const uint16_t id = sprpString(chk_, 0);
    return id ? strings_.get(id) : std::string();
}
std::string Editor::scenarioDescription() const {
    const uint16_t id = sprpString(chk_, 1);
    return id ? strings_.get(id) : std::string();
}

void Editor::setScenarioText(const std::string& name, const std::string& description) {
    if (name == scenarioName() && description == scenarioDescription()) return;
    beginEdit();
    uint16_t ids[2] = {sprpString(chk_, 0), sprpString(chk_, 1)};
    const std::string* texts[2] = {&name, &description};
    for (int i = 0; i < 2; ++i) {
        if (ids[i]) strings_.set(ids[i], *texts[i]);
        else if (!texts[i]->empty()) ids[i] = uint16_t(strings_.add(*texts[i]));
    }
    strings_.write(chk_);
    Bytes d;
    appendU16(d, ids[0]), appendU16(d, ids[1]);
    chk_.set("SPRP", std::move(d));
    commit(false);
}

// What a terrain is, the same in every tileset, for a change of tileset (Map properties): each terrain gets a role,
// and a terrain with no terrain of the same name in the new tileset goes to the new tileset's terrain of its role.
//   L  the lowest, not walkable: water, space, magma, tar, bottomless pit
//   G  ground: dirt, snow, platform, floor          H  high ground: high dirt, high snow, high platform, roof
//   V  ground cover: grass, jungle, sand dunes      W  high cover: high grass, high jungle, ...
//   R  rough: rocky ground, crevices, broken rock   P  paved: asphalt, ruins, crags, flagstones, plating
//   S  buildings: structure, temple, compound       T  high buildings: high temple, high compound, ...
//   M  mud: mud, dried mud, ice
// A role the new tileset does not have goes on down: T > W > H, W > H, S > P > G, and V, R, M > G.
// The roles are a first pass by name and look; tune them here. Index: the tileset (era & 7), then the terrain id - 1.
static char terrainRole(uint16_t era, uint16_t t) {
    static const char* const roles[8] = {
        "GHLVPRWSM",       // badlands: Dirt, High Dirt, Water, Grass, Asphalt, Rocky Ground, High Grass, Structure, Mud
        "LG-H---SMRPWSV",  // platform: Space, Platform, -, High Platform, -, -, -, Solar Array, Low Platform, Rusty Pit, Plating,
                           // High Plating, Elevated Catwalk, Dark Platform
        "RGHPPSL",         // installation: Substructure, Floor, Roof, Substructure Plating, Plating, Substructure Panels, Bottomless Pit
        "LGVPHWWR",        // ashworld: Magma, Dirt, Lava, Shale, High Dirt, High Lava, High Shale, Broken Rock
        "GHLVVRPSWWWTM",   // jungle: Dirt, High Dirt, Water, Jungle, Raised Jungle, Rocky Ground, Ruins, Temple, High Jungle,
                           // High Ruins, High Raised Jungle, High Temple, Mud
        "GHLVVRPSWWWTM",   // desert: Dirt, High Dirt, Tar, Sand Dunes, Sandy Sunken Pit, Rocky Ground, Crags, Compound, ...
        "GHMGLRVSHWLTR",   // ice: Snow, High Snow, Ice, Dirt, Water, Rocky Snow, Grass, Outpost, High Dirt, High Grass, High Water,
                           // High Outpost, Moguls
        "GHLVVRPSWWWTM",   // twilight: Dirt, High Dirt, Water, Crushed Rock, Sunken Ground, Crevices, Flagstones, Basilica, ...
    };
    const char* r = roles[era & 7];
    return t >= 1 && t <= std::strlen(r) ? r[t - 1] : '-';
}
// The terrain of `era` for an old terrain: the same name, else the first of its role (or of the roles under it).
static uint16_t migrateTerrain(const IsomTerrainTable& from, uint16_t fromEra, uint16_t t, const IsomTerrainTable& to, uint16_t toEra, uint16_t fill) {
    for (uint16_t n = 1; n <= to.terrainCount(); ++n)
        if (to.isTerrain(n) && std::strcmp(from.terrainName(t), to.terrainName(n)) == 0) return n;
    static const std::map<char, const char*> down = {{'T', "TWH"}, {'W', "WH"}, {'S', "SPG"}, {'P', "PG"}, {'V', "VG"}, {'R', "RG"},
                                                      {'M', "MG"}, {'H', "H"},  {'G', "G"},   {'L', "L"}};
    const auto it = down.find(terrainRole(fromEra, t));
    if (it == down.end()) return fill;
    for (const char* role = it->second; *role; ++role)
        for (uint16_t n = 1; n <= to.terrainCount(); ++n)
            if (to.isTerrain(n) && terrainRole(toEra, n) == *role) return n;
    return fill;
}

std::vector<uint16_t> Editor::tilesetTerrainMap(uint16_t from, uint16_t to, uint16_t fill) {
    const IsomTerrainTable *a = IsomTerrainTable::forEra(from), *b = IsomTerrainTable::forEra(to);
    std::vector<uint16_t> out;
    if (!a || !b) return out;
    out.assign(a->terrainCount() + 1u, 0);
    for (uint16_t t = 1; t <= a->terrainCount(); ++t)
        if (a->isTerrain(t)) out[t] = migrateTerrain(*a, from, t, *b, to, fill);
    return out;
}

bool Editor::resizeMap(uint16_t width, uint16_t height, uint16_t era, int shiftX, int shiftY, uint16_t fill, std::string& error) {
    if (!isomOk_) return error = "the map's terrain has no ISOM data, so StarDraft cannot rebuild it", false;
    if (width < 16 || height < 16 || width > 256 || height > 256) return error = "the size must be 16 to 256 tiles", false;
    const IsomTerrainTable* oldTable = IsomTerrainTable::forEra(era_);
    const IsomTerrainTable* newTable = IsomTerrainTable::forEra(era);
    if (!oldTable || !newTable) return error = "unknown tileset", false;
    if (!newTable->isTerrain(fill)) return error = "the fill terrain is not one of the tileset's", false;
    // The shift in corners: x in 64 px steps, y in 32 px steps, and x + y even so corners stay corners.
    int dx = shiftX >= 0 ? shiftX / 2 : -((-shiftX + 1) / 2), dy = shiftY;
    if ((dx + dy) & 1) dy -= 1;
    const int px = dx * 64, py = dy * 32;
    // Each old terrain: the new tileset's terrain of the same name, else of the same role (terrainRole), else the fill.
    std::vector<uint16_t> mapTo(oldTable->terrainCount() + 1u, fill);
    for (uint16_t t = 1; t <= oldTable->terrainCount(); ++t)
        if (oldTable->isTerrain(t)) mapTo[t] = migrateTerrain(*oldTable, era_, t, *newTable, era, fill);
    std::vector<CornerValue> corners;
    for (int y = -1; y <= isom_.rectRows() + 1; ++y)
        for (int x = -1; x <= isom_.rectCols() + 1; ++x)
            if (isom_.isCorner(x, y)) {
                const uint16_t t = isom_.terrainAt(x, y);
                corners.push_back({x + dx, y + dy, t < mapTo.size() ? mapTo[t] : fill});
            }
    const std::vector<Doodad> oldDoodads = doodads_;
    const bool keepDoodads = era == era_;  // before reload() changes era_
    std::vector<MapSprite> sprites;  // the sprites that are not doodads' (the doodads bring theirs again)
    for (const MapSprite& s : sprites_) {
        bool overlay = false;
        for (const Doodad& d : oldDoodads)
            if (const DoodadType* t = doodadTypes_.find(d.id); t && t->overlaySprite == s.id && d.x == s.x && d.y == s.y) overlay = true;
        if (!overlay) sprites.push_back(s);
    }
    const int maxX = width * 32, maxY = height * 32;
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < maxX && y < maxY; };

    beginEdit();
    std::string err;
    // The same tileset: the terrain core resizes as ScmDraft does (the terrain moved, the new ground filled, the edges
    // between them made; the tiles that stay keep theirs). MTXM is TILE again: the doodads are placed again below.
    // Else (a new tileset, or a rect no tile fits): the terrain is made again from its corners (below).
    bool viaCore = terrainCoreOn() && era == era_ && core_.load(chk_, tileset_, err) && core_.resize(width, height, dx * 2, dy, fill) &&
                   !core_.untileable();
    if (viaCore) {
        core_.store(chk_);
        chk_.set("MTXM", chk_.effective("TILE"));
    }
    IsomGrid fresh;
    Tileset otherTileset;  // (a new tileset's tiles: its CV5)
    const bool tilesetReady = viaCore || era == era_ || otherTileset.load(data_, era, err);
    if (!viaCore && (!tilesetReady || !fresh.create(chk_, width, height, era, fill, era == era_ ? tileset_ : otherTileset, err))) {
        {
            chk_ = restore(undo_.back());  // as before (inside a stroke: as before the stroke)
            if (!inStroke_) dropLastUndo();
            reload();
        }
        return error = err, false;
    }
    chk_.set("MASK", Bytes(size_t(width) * height, 0xFF));
    std::vector<Unit> units;
    for (Unit u : units_)
        if (inside(u.x + px, u.y + py)) u.x = uint16_t(u.x + px), u.y = uint16_t(u.y + py), units.push_back(u);
    writeUnits(chk_, units);
    std::vector<MapSprite> keptSprites;
    for (MapSprite s : sprites)
        if (inside(s.x + px, s.y + py)) s.x = uint16_t(s.x + px), s.y = uint16_t(s.y + py), keptSprites.push_back(s);
    writeSprites(chk_, keptSprites);
    writeDoodads(chk_, {});
    // Locations move too and stay on the map; the last one ("Anywhere") is the whole map.
    std::vector<Location> locs = locations_;
    auto clampX = [&](int v) { return uint32_t(std::clamp(v, 0, maxX)); };
    auto clampY = [&](int v) { return uint32_t(std::clamp(v, 0, maxY)); };
    for (size_t i = 0; i < locs.size(); ++i) {
        Location& l = locs[i];
        if (!l.used()) continue;
        if (i == 63) {
            l.left = l.top = 0, l.right = uint32_t(maxX), l.bottom = uint32_t(maxY);
            continue;
        }
        l.left = clampX(int(l.left) + px), l.right = clampX(int(l.right) + px), l.top = clampY(int(l.top) + py), l.bottom = clampY(int(l.bottom) + py);
    }
    writeLocations(chk_, locs);
    reload();  // the new tileset, terrain and doodad types
    std::erase_if(corners, [&](const CornerValue& c) { return !isom_.isCorner(c.x, c.y); });
    if (!viaCore && (!isom_.apply(corners) || !isom_.store(chk_, err))) {
        {
            chk_ = restore(undo_.back());  // as before (inside a stroke: as before the stroke)
            if (!inStroke_) dropLastUndo();
            reload();
        }
        return error = err.empty() ? "the terrain does not fit the new tileset" : err, false;
    }
    // The doodads again, moved with the map, only when the tileset stays: a doodad id is another doodad (or none) in
    // another tileset, so a change of tileset leaves the map without doodads.
    if (keepDoodads)
        for (const Doodad& d : oldDoodads)
            if (const DoodadType* t = doodadTypes_.find(d.id)) {
                int tx, ty;
                doodadOrigin(*t, d.x + px, d.y + py, tx, ty);
                stardraft::placeDoodad(chk_, *t, tileset_, tx, ty, d.owner, false);
            }
    selection_.clear();
    selectedLocation_ = -1;
    reload();
    commit(true);
    return true;
}

// --- Units -----------------------------------------------------------------------------------------

PlacementContext Editor::placementContext(const std::vector<Unit>* units) const {
    return PlacementContext{width_, height_, &terrain_, &tileset_, &types_, units ? units : &units_, allowStacking_, resourceDistance_};
}

Rect Editor::unitBox(const Unit& u) const {
    if (u.type >= types_.size()) return {u.x - 8, u.y - 8, u.x + 8, u.y + 8};
    const UnitType& t = types_[u.type];
    if (t.is(UnitFlag::Building))
        return {u.x - t.placementWidth / 2, u.y - t.placementHeight / 2, u.x - t.placementWidth / 2 + t.placementWidth,
                u.y - t.placementHeight / 2 + t.placementHeight};
    return {u.x - t.sizeLeft, u.y - t.sizeUp, u.x + t.sizeRight + 1, u.y + t.sizeDown + 1};
}

std::optional<std::pair<uint16_t, uint16_t>> Editor::findValidPosition(const Unit& unit, int radius,
                                                                        const std::vector<Unit>* units) const {
    if (unit.type >= types_.size()) return std::nullopt;
    const UnitType& t = types_[unit.type];
    const bool building = t.is(UnitFlag::Building);
    const int step = building ? 32 : 8;
    PlacementContext ctx = placementContext(units);
    Unit u = unit;
    uint16_t sx = unit.x, sy = unit.y;
    snapToGrid(t, sx, sy);
    const int maxX = width_ * 32 - 1, maxY = height_ * 32 - 1;
    // Rings of increasing Chebyshev radius; within a ring, nearest first.
    for (int r = 0; r * step <= radius; ++r) {
        std::vector<std::pair<int, std::pair<int, int>>> ring;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                int x = sx + dx * step, y = sy + dy * step;
                if (x < 0 || y < 0 || x > maxX || y > maxY) continue;
                ring.push_back({dx * dx + dy * dy, {x, y}});
            }
        std::sort(ring.begin(), ring.end());
        for (const auto& [d, p] : ring) {
            u.x = uint16_t(p.first);
            u.y = uint16_t(p.second);
            snapToGrid(t, u.x, u.y);  // (a building, while building snap is on)
            if (placementOk(ctx, u)) return std::make_pair(u.x, u.y);
        }
    }
    return std::nullopt;
}

uint32_t Editor::nextFreeSerial(const std::vector<Unit>& units) const { return nextSerial(units); }

Unit Editor::correctedOrSame(Unit u, bool correct, const std::vector<Unit>& against) const {
    if (!correct || allowIllegalUnits_) return u;
    if (placementOk(placementContext(&against), u)) return u;
    if (auto p = findValidPosition(u, 320, &against)) u.x = p->first, u.y = p->second;
    return u;
}

std::optional<Unit> Editor::fitted(Unit u, bool correct, const std::vector<Unit>& against) const {
    if (allowIllegalUnits_ || placementOk(placementContext(&against), u)) return u;
    if (!correct) return std::nullopt;
    auto p = findValidPosition(u, 320, &against);
    if (!p) return std::nullopt;
    u.x = p->first, u.y = p->second;
    return u;
}

int Editor::setSelectionOwner(uint8_t owner) {
    std::vector<int> targets;
    for (int i : selection_)
        if (i >= 0 && i < int(units_.size()) && units_[i].owner != owner && units_[i].type < types_.size() &&
            (types_[units_[i].type].availability & UnitAvail::NonNeutral))
            targets.push_back(i);
    if (targets.empty()) return 0;
    beginEdit();
    for (int i : targets) units_[i].owner = owner;
    writeUnits(chk_, units_);
    commit(false);
    return int(targets.size());
}

int Editor::updateUnits(const std::vector<int>& indices, const std::function<void(Unit&)>& change) {
    std::vector<std::pair<int, Unit>> changed;
    for (int i : indices) {
        if (i < 0 || i >= int(units_.size())) continue;
        Unit u = units_[i];
        change(u);
        Bytes a, b;
        u.encode(a), units_[i].encode(b);
        if (a != b) changed.push_back({i, u});
    }
    if (changed.empty()) return 0;
    beginEdit();
    for (const auto& [i, u] : changed) units_[i] = u;
    fixAddonLinks();  // a building moved or replaced by a type that takes no such add-on: no link
    writeUnits(chk_, units_);
    commit(false);
    return int(changed.size());
}

int Editor::replaceUnits(uint16_t from, uint16_t to, bool selectionOnly) {
    if (from >= types_.size() || to >= types_.size() || from == to) return 0;
    std::vector<int> targets;
    for (int i = 0; i < int(units_.size()); ++i)
        if (units_[i].type == from && (!selectionOnly || isSelected(i))) targets.push_back(i);
    if (targets.empty()) return 0;
    beginEdit();
    const UnitType& t = types_[to];
    std::vector<bool> isTarget(units_.size(), false);
    for (int i : targets) isTarget[i] = true;
    std::vector<Unit> against;
    for (size_t i = 0; i < units_.size(); ++i)
        if (!isTarget[i]) against.push_back(units_[i]);
    for (int i : targets) {
        const Unit& old = units_[i];
        Unit u = makeUnit(t, (t.availability & UnitAvail::NonNeutral) ? old.owner : uint8_t(11), old.x, old.y);
        u.serial = old.serial;
        u.hitPoints = old.hitPoints, u.shields = old.shields, u.energy = old.energy;
        u.state = old.state & u.validState;
        if (t.is(UnitFlag::ResourceContainer) && old.resources) u.resources = old.resources;
        snapToGrid(t, u.x, u.y);
        u = correctedOrSame(u, true, against);
        units_[i] = u;
        against.push_back(u);
    }
    fixAddonLinks();  // a building moved or replaced by a type that takes no such add-on: no link
    writeUnits(chk_, units_);
    commit(false);
    return int(targets.size());
}

int Editor::sprayUnits(const std::vector<uint16_t>& types, uint8_t owner, int x, int y, int radius, int count, uint32_t seed) {
    if (types.empty() || count <= 0) return 0;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> unit(-1, 1);
    std::vector<Unit> added;
    std::vector<Unit> all = units_;
    SerialCounter serials(all);
    for (int i = 0; i < count; ++i) {
        uint16_t type = types[rng() % types.size()];
        if (type >= types_.size()) continue;
        const UnitType& t = types_[type];
        double dx, dy;
        do dx = unit(rng), dy = unit(rng);
        while (dx * dx + dy * dy > 1);
        for (auto [px, py] : symmetricPoints(int(x + dx * radius), int(y + dy * radius))) {
            if (unitLimit_ > 0 && int(all.size()) >= unitLimit_) break;
            if (px < 0 || py < 0 || px >= width_ * 32 || py >= height_ * 32) continue;
            uint16_t ux = uint16_t(px), uy = uint16_t(py);
            snapToGrid(t, ux, uy);
            uint8_t who = (t.availability & UnitAvail::NonNeutral) ? owner : uint8_t(11);
            Unit u = makeUnit(t, who, ux, uy);
            if (!allowIllegalUnits_ && !placementOk(placementContext(&all), u)) continue;  // only valid spots
            u.serial = serials.take();
            all.push_back(u);
            added.push_back(u);
        }
    }
    if (added.empty()) return 0;
    beginEdit();
    selection_.clear();
    for (const Unit& u : added) {
        units_.push_back(u);
        selection_.push_back(int(units_.size()) - 1);
    }
    writeUnits(chk_, units_);
    commit(false);
    return int(added.size());
}

int Editor::placeMapRevealers(const std::vector<uint8_t>& owners, int spacingTiles) {
    if (spacingTiles < 1 || owners.empty() || kMapRevealerUnit >= types_.size()) return 0;
    const UnitType& t = types_[kMapRevealerUnit];
    // The middle of each square of the grid, the last one clamped to the map's edge (the revealer's sight is 10 tiles, so
    // squares of 8 tiles leave no gap: the farthest point of a square is 5.7 tiles from its middle).
    const int cell = spacingTiles * 32;
    auto centers = [cell](int tiles) {
        std::vector<int> out;
        for (int c = 0; c < tiles * 32; c += cell) out.push_back(std::min(c + cell / 2, tiles * 32 - 1));
        return out;
    };
    const std::vector<int> xs = centers(width_), ys = centers(height_);
    // The revealers the map has: a player's place that has one is skipped (the key: owner, x, y).
    auto key = [](int owner, int x, int y) { return (uint64_t(owner) << 32) | (uint64_t(uint16_t(x)) << 16) | uint16_t(y); };
    std::set<uint64_t> have;
    for (const Unit& u : units_)
        if (u.type == kMapRevealerUnit) have.insert(key(u.owner, u.x, u.y));
    std::vector<Unit> all = units_;
    SerialCounter serials(all);
    std::vector<Unit> added;
    for (uint8_t owner : owners) {
        if (owner > 7) continue;  // Player 1 to Player 8
        for (int y : ys)
            for (int x : xs) {
                if (unitLimit_ > 0 && int(all.size()) >= unitLimit_) break;
                if (!have.insert(key(owner, x, y)).second) continue;
                Unit u = makeUnit(t, owner, uint16_t(x), uint16_t(y));
                u.serial = serials.take();
                all.push_back(u);
                added.push_back(u);
            }
    }
    if (added.empty()) return 0;
    beginEdit();
    units_.insert(units_.end(), added.begin(), added.end());
    writeUnits(chk_, units_);
    commit(false);
    return int(added.size());
}

void Editor::setMapRevealersHidden(bool hidden) {
    if (revealersHidden_ == hidden) return;
    revealersHidden_ = hidden;
    if (hidden) selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [this](int i) { return !pickable(i); }), selection_.end());
    revision_ = nextRevision();
}

namespace {
constexpr uint16_t kNydusCanalUnit = 134;  // units.dat id of Zerg Nydus Canal
}

bool Editor::canLinkUnits(int a, int b) const {
    const int n = int(units_.size());
    if (a == b || a < 0 || b < 0 || a >= n || b >= n) return false;
    const Unit& ua = units_[size_t(a)];
    const Unit& ub = units_[size_t(b)];
    if (ua.relatedSerial || ub.relatedSerial) return false;  // linked already
    if (ua.type == kNydusCanalUnit && ub.type == kNydusCanalUnit) return true;
    auto addonOf = [&](const Unit& add, const Unit& bld) {
        const int pt = addonParent(add.type);
        return pt >= 0 && pt == int(bld.type) && bld.owner == add.owner;
    };
    return addonOf(ua, ub) || addonOf(ub, ua);
}

bool Editor::linkUnits(int a, int b) {
    if (!canLinkUnits(a, b)) return false;
    const uint16_t bit = units_[size_t(a)].type == kNydusCanalUnit ? UnitLink::Nydus : UnitLink::Addon;
    beginEdit();
    // (A serial of 0 means "no link" to the links: such a unit gets a serial first.)
    if (!units_[size_t(a)].serial) units_[size_t(a)].serial = nextFreeSerial(units_);
    if (!units_[size_t(b)].serial) units_[size_t(b)].serial = nextFreeSerial(units_);
    Unit& ua = units_[size_t(a)];
    Unit& ub = units_[size_t(b)];
    ua.links |= bit, ua.relatedSerial = ub.serial;
    ub.links |= bit, ub.relatedSerial = ua.serial;
    writeUnits(chk_, units_);
    commit(false);
    return true;
}

int Editor::linkedUnit(int unit) const {
    if (unit < 0 || unit >= int(units_.size()) || !units_[size_t(unit)].relatedSerial) return -1;
    const uint32_t serial = units_[size_t(unit)].relatedSerial;
    for (int i = 0; i < int(units_.size()); ++i)
        if (units_[size_t(i)].serial == serial) return i;
    return -1;
}

int Editor::moveUnits(const std::vector<int>& indices, int target) {
    const int n = int(units_.size());
    std::vector<bool> moved(size_t(n), false);
    int count = 0;
    for (int i : indices)
        if (i >= 0 && i < n && !moved[size_t(i)]) moved[size_t(i)] = true, ++count;
    if (!count) return 0;
    target = std::clamp(target, 0, n);
    // The new newOrder: the others in their newOrder, the moved ones (in their newOrder) where `target` was.
    std::vector<int> newOrder;
    newOrder.reserve(size_t(n));
    for (int i = 0; i <= n; ++i) {
        if (i == target)
            for (int k = 0; k < n; ++k)
                if (moved[size_t(k)]) newOrder.push_back(k);
        if (i < n && !moved[size_t(i)]) newOrder.push_back(i);
    }
    if (std::is_sorted(newOrder.begin(), newOrder.end())) return 0;  // the order stays
    std::vector<int> newIndex(size_t(n), 0);
    for (int k = 0; k < n; ++k) newIndex[size_t(newOrder[size_t(k)])] = k;
    beginEdit();
    std::vector<Unit> out;
    out.reserve(size_t(n));
    for (int k = 0; k < n; ++k) out.push_back(units_[size_t(newOrder[size_t(k)])]);
    units_ = std::move(out);
    for (int& s : selection_)
        if (s >= 0 && s < n) s = newIndex[size_t(s)];
    writeUnits(chk_, units_);
    commit(false);
    return count;
}

int Editor::removeMapRevealers() {
    std::vector<int> newIndex(units_.size(), -1);  // each unit's index after the removal (-1: removed)
    int kept = 0, removed = 0;
    for (size_t i = 0; i < units_.size(); ++i) {
        if (units_[i].type == kMapRevealerUnit) ++removed;
        else newIndex[i] = kept++;
    }
    if (!removed) return 0;
    beginEdit();
    std::vector<Unit> out;
    out.reserve(size_t(kept));
    for (size_t i = 0; i < units_.size(); ++i)
        if (newIndex[i] >= 0) out.push_back(units_[i]);
    units_ = std::move(out);
    std::vector<int> sel;  // the selection stays on the units that are left
    for (int i : selection_)
        if (i >= 0 && i < int(newIndex.size()) && newIndex[size_t(i)] >= 0) sel.push_back(newIndex[size_t(i)]);
    selection_ = std::move(sel);
    writeUnits(chk_, units_);
    commit(false);
    return removed;
}

PlaceResult Editor::placeUnit(uint16_t type, uint8_t owner, int x, int y, bool correct) {
    PlaceResult r;
    if (type >= types_.size()) {
        r.issues.push_back(PlacementIssue::BadType);
        return r;
    }
    if (unitLimit_ > 0 && int(units_.size()) >= unitLimit_) {
        r.issues.push_back(PlacementIssue::UnitLimit);
        return r;
    }
    const UnitType& t = types_[type];
    uint16_t ux = uint16_t(std::clamp(x, 0, width_ * 32 - 1)), uy = uint16_t(std::clamp(y, 0, height_ * 32 - 1));
    snapToGrid(t, ux, uy);
    // A Terran add-on near its building (same owner, no add-on yet) goes on it, linked to it (GitHub #129).
    int parent = -1;
    if (const int pt = addonParent(type); pt >= 0) {
        int best = 96 * 96 + 1;  // within 3 tiles of the click
        for (int i = 0; i < int(units_.size()); ++i) {
            const Unit& b = units_[size_t(i)];
            if (b.type != pt || b.owner != owner || (b.state & UnitState::InTransit)) continue;
            const bool taken = std::any_of(units_.begin(), units_.end(), [&](const Unit& o) {
                return o.relatedSerial == b.serial && (o.links & UnitLink::Addon) && addonParent(o.type) >= 0;
            });
            if (taken) continue;
            const auto [sx, sy] = addonSpot(types_[pt], b, t);
            const int d = (sx - x) * (sx - x) + (sy - y) * (sy - y);
            if (d < best && sx >= 0 && sy >= 0 && sx < width_ * 32 && sy < height_ * 32) best = d, parent = i, ux = uint16_t(sx), uy = uint16_t(sy);
        }
    }
    r.unit = makeUnit(t, owner, ux, uy);
    r.unit.serial = nextFreeSerial(units_);
    if (!allowIllegalUnits_) r.issues = checkPlacement(placementContext(), r.unit);
    if (!r.issues.empty()) {
        if (!correct) return r;
        auto p = findValidPosition(r.unit);
        if (!p) return r;  // nowhere valid nearby: refuse with the original issues
        r.unit.x = p->first, r.unit.y = p->second;
        parent = -1;  // moved off the building's add-on spot: not linked
    }
    beginEdit();
    if (parent >= 0) {  // both point at each other, as StarEdit writes them
        Unit& b = units_[size_t(parent)];
        r.unit.links |= UnitLink::Addon, r.unit.relatedSerial = b.serial;
        b.links |= UnitLink::Addon, b.relatedSerial = r.unit.serial;
    }
    units_.push_back(r.unit);
    r.index = int(units_.size()) - 1;
    r.placed = true;
    selection_ = {r.index};
    // Mirror images (symmetry), each corrected or skipped if there is no valid spot.
    auto pts = symmetricPoints(x, y);
    for (size_t k = 1; k < pts.size(); ++k) {
        if (unitLimit_ > 0 && int(units_.size()) >= unitLimit_) break;
        uint16_t mx = uint16_t(std::clamp(pts[k].first, 0, width_ * 32 - 1)), my = uint16_t(std::clamp(pts[k].second, 0, height_ * 32 - 1));
        snapToGrid(t, mx, my);
        Unit m = makeUnit(t, owner, mx, my);
        if (!allowIllegalUnits_ && !checkPlacement(placementContext(), m).empty()) {
            if (!correct) continue;
            auto p = findValidPosition(m);
            if (!p) continue;
            m.x = p->first, m.y = p->second;
        }
        m.serial = nextFreeSerial(units_);
        units_.push_back(m);
        selection_.push_back(int(units_.size()) - 1);
    }
    writeUnits(chk_, units_);
    commit(false);
    return r;
}

int Editor::unitAt(int x, int y) const {
    int best = -1;
    int bestArea = 1 << 30;
    for (int i = int(units_.size()) - 1; i >= 0; --i) {
        if (!pickable(i)) continue;
        Rect b = unitBox(units_[i]);
        if (!b.contains(x, y)) continue;
        int area = (b.right - b.left) * (b.bottom - b.top);  // prefer the smallest (unit on top of a building)
        if (area < bestArea) best = i, bestArea = area;
    }
    return best;
}

std::vector<int> Editor::unitsAt(int x, int y) const {
    std::vector<std::pair<int, int>> hits;  // (the box's area, the unit)
    for (int i = int(units_.size()) - 1; i >= 0; --i) {
        if (!pickable(i)) continue;
        const Rect b = unitBox(units_[size_t(i)]);
        if (b.contains(x, y)) hits.push_back({(b.right - b.left) * (b.bottom - b.top), i});
    }
    // The smallest box first (a unit on top of a building), then the unit placed last (as unitAt).
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<int> out;
    out.reserve(hits.size());
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

// --- Selection -------------------------------------------------------------------------------------

bool Editor::isSelected(int unit) const { return std::find(selection_.begin(), selection_.end(), unit) != selection_.end(); }
void Editor::clearSelection() {
    selection_.clear();
    revision_ = nextRevision();
}
void Editor::select(int unit, bool add) {
    if (!add) selection_.clear();
    if (unit >= 0 && unit < int(units_.size()) && !isSelected(unit)) selection_.push_back(unit);
    revision_ = nextRevision();
}
void Editor::toggleSelected(int unit) {
    auto it = std::find(selection_.begin(), selection_.end(), unit);
    if (it != selection_.end()) selection_.erase(it);
    else if (unit >= 0 && unit < int(units_.size())) selection_.push_back(unit);
    revision_ = nextRevision();
}
void Editor::selectRect(const Rect& rect, bool add) {
    Rect r = rect.normalized();
    if (!add) selection_.clear();
    for (int i = 0; i < int(units_.size()); ++i)
        if (pickable(i) && r.intersects(unitBox(units_[i])) && !isSelected(i)) selection_.push_back(i);
    revision_ = nextRevision();
}
void Editor::selectAll() {
    selection_.clear();
    for (int i = 0; i < int(units_.size()); ++i)
        if (pickable(i)) selection_.push_back(i);
    revision_ = nextRevision();
}

int Editor::selectUnitsOfType(uint16_t type, int owner) {
    selection_.clear();
    for (int i = 0; i < int(units_.size()); ++i)
        if (units_[size_t(i)].type == type && (owner < 0 || units_[size_t(i)].owner == owner)) selection_.push_back(i);
    revision_ = nextRevision();
    return int(selection_.size());
}

void Editor::deleteSelection() {
    if (selection_.empty()) return;
    beginEdit();
    std::vector<int> idx = selection_;
    std::sort(idx.rbegin(), idx.rend());
    for (int i : idx) units_.erase(units_.begin() + i);
    selection_.clear();
    fixAddonLinks();  // a deleted building's add-on (or a deleted add-on's building) is not linked any more
    writeUnits(chk_, units_);
    commit(false);
}

std::vector<int> Editor::withLinkedAddons(std::vector<int> sel) const {
    const size_t n = sel.size();
    for (size_t k = 0; k < n; ++k) {
        const Unit& b = units_[size_t(sel[k])];
        if (!(b.links & UnitLink::Addon) || addonParent(b.type) >= 0) continue;  // a building with an add-on
        for (int i = 0; i < int(units_.size()); ++i) {
            const Unit& a = units_[size_t(i)];
            if (a.serial == b.relatedSerial && (a.links & UnitLink::Addon) && addonParent(a.type) >= 0 &&
                std::find(sel.begin(), sel.end(), i) == sel.end())
                sel.push_back(i);
        }
    }
    return sel;
}

void Editor::fixAddonLinks() {
    auto find = [&](uint32_t serial) -> Unit* {
        for (Unit& u : units_)
            if (u.serial == serial) return &u;
        return nullptr;
    };
    for (Unit& a : units_) {
        if (!(a.links & UnitLink::Addon) || addonParent(a.type) < 0) continue;  // the add-ons
        Unit* b = find(a.relatedSerial);
        const bool ok = b && b->type == addonParent(a.type) && b->type < types_.size() && a.type < types_.size() &&
                        addonSpot(types_[b->type], *b, types_[a.type]) == std::pair<int, int>{a.x, a.y};
        if (ok) continue;
        a.links &= uint16_t(~UnitLink::Addon), a.relatedSerial = 0;
        if (b && b->relatedSerial == a.serial) b->links &= uint16_t(~UnitLink::Addon), b->relatedSerial = 0;
    }
    for (Unit& b : units_) {  // a building whose add-on is gone
        if (!(b.links & UnitLink::Addon) || addonParent(b.type) >= 0) continue;
        const Unit* a = find(b.relatedSerial);
        if (!a || !(a->links & UnitLink::Addon) || a->relatedSerial != b.serial) b.links &= uint16_t(~UnitLink::Addon), b.relatedSerial = 0;
    }
}

int Editor::moveSelection(int dx, int dy, bool correct) {
    if (selection_.empty() || (!dx && !dy)) return 0;
    std::vector<int> sel = withLinkedAddons(selection_);  // a building's add-on goes with it
    std::sort(sel.begin(), sel.end());
    std::vector<bool> selected(units_.size(), false);
    for (int i : sel) selected[i] = true;
    std::vector<Unit> others;  // the units that do not move
    for (size_t i = 0; i < units_.size(); ++i)
        if (!selected[i]) others.push_back(units_[i]);
    std::vector<Unit> moved;
    for (int i : sel) {
        Unit u = units_[i];
        u.x = uint16_t(std::clamp(int(u.x) + dx, 0, width_ * 32 - 1));
        u.y = uint16_t(std::clamp(int(u.y) + dy, 0, height_ * 32 - 1));
        if (u.type < types_.size()) snapToGrid(types_[u.type], u.x, u.y);
        moved.push_back(u);
    }
    // A linked add-on moving with its building: on the building's add-on spot (the building may have stopped at the map's
    // edge, where the add-on alone would have gone on).
    for (size_t k = 0; k < sel.size(); ++k) {
        const Unit& a = units_[size_t(sel[k])];
        if (!(a.links & UnitLink::Addon) || addonParent(a.type) < 0) continue;
        for (size_t j = 0; j < sel.size(); ++j)
            if (units_[size_t(sel[j])].serial == a.relatedSerial && moved[j].type < types_.size() && a.type < types_.size()) {
                const auto [ax, ay] = addonSpot(types_[moved[j].type], moved[j], types_[a.type]);
                moved[k].x = uint16_t(std::clamp(ax, 0, width_ * 32 - 1)), moved[k].y = uint16_t(std::clamp(ay, 0, height_ * 32 - 1));
            }
    }
    // As a group: the units keep their places to each other (as they were, also when they overlap).
    bool fits = true;
    if (!allowIllegalUnits_) {
        const PlacementContext ctx = placementContext(&others);
        for (const Unit& u : moved) fits = fits && placementOk(ctx, u);
    }
    if (!fits) {
        if (!correct) return 0;  // refused: the units stay where they were
        // Each unit to the nearest spot where it fits, checked against the others and the units already done.
        std::vector<Unit> against = others;
        for (size_t k = 0; k < sel.size(); ++k) {
            std::optional<Unit> f = fitted(moved[k], true, against);
            moved[k] = f ? *f : units_[sel[k]];  // no spot: it stays
            against.push_back(moved[k]);
        }
    }
    int count = 0;
    for (size_t k = 0; k < sel.size(); ++k) count += moved[k].x != units_[sel[k]].x || moved[k].y != units_[sel[k]].y;
    if (!count) return 0;
    beginEdit();
    for (size_t k = 0; k < sel.size(); ++k) units_[sel[k]] = moved[k];
    fixAddonLinks();  // an add-on that did not keep its place on its building is not linked any more
    writeUnits(chk_, units_);
    commit(false);
    return count;
}

void Editor::copySelection() {
    if (selection_.empty()) return;
    clipboard_.clear();
    long long cx = 0, cy = 0;
    for (int i : selection_) cx += units_[i].x, cy += units_[i].y;
    cx /= static_cast<long long>(selection_.size());
    cy /= static_cast<long long>(selection_.size());
    for (int i : selection_) {
        Unit u = units_[i];
        // Store offsets from the group center in x/y (as signed values packed into the fields).
        u.x = uint16_t(int(u.x) - int(cx));
        u.y = uint16_t(int(u.y) - int(cy));
        u.relatedSerial = 0;
        u.links = 0;
        clipboard_.push_back(u);
    }
}

int Editor::paste(int x, int y, bool correct) {
    if (clipboard_.empty()) return 0;
    std::vector<Unit> all = units_;
    SerialCounter serials(all);
    for (Unit u : clipboard_) {
        if (unitLimit_ > 0 && int(all.size()) >= unitLimit_) break;
        int nx = x + int16_t(u.x), ny = y + int16_t(u.y);
        u.x = uint16_t(std::clamp(nx, 0, width_ * 32 - 1));
        u.y = uint16_t(std::clamp(ny, 0, height_ * 32 - 1));
        if (u.type < types_.size()) snapToGrid(types_[u.type], u.x, u.y);
        std::optional<Unit> f = fitted(u, correct, all);
        if (!f) continue;  // no spot where it fits: not pasted, as placing refuses
        f->serial = serials.take();
        all.push_back(*f);
    }
    const int count = int(all.size() - units_.size());
    if (!count) {
        selection_.clear();
        revision_ = nextRevision();
        return 0;
    }
    beginEdit();
    selection_.clear();
    for (size_t i = units_.size(); i < all.size(); ++i) selection_.push_back(int(i));
    units_ = std::move(all);
    writeUnits(chk_, units_);
    commit(false);
    return count;
}

// --- Doodads ---------------------------------------------------------------------------------------

void Editor::doodadOrigin(const DoodadType& t, int x, int y, int& tileX, int& tileY) const {
    tileX = int(std::lround((x - t.width * 16) / 32.0));
    tileY = int(std::lround((y - t.height * 16) / 32.0));
}

// Two doodads overlap only where both have a tile: the empty corners of a ramp or cliff piece take the next piece (a
// test of the rectangles refused the next segment of a ramp, Discord 2026-10-14). A doodad with no tile at all (only
// its sprite) takes its whole rectangle.
bool Editor::doodadOverlaps(const DoodadType& t, int tileX, int tileY) const {
    auto tile = [&](const DoodadType& k, int c, int r) {
        const size_t g = size_t(k.firstGroup) + size_t(r);
        return g < tileset_.groups.size() && size_t(c) < tileset_.groups[g].megatiles.size() && tileset_.groups[g].megatiles[size_t(c)] != 0;
    };
    auto hasTiles = [&](const DoodadType& k) {
        for (int r = 0; r < k.height; ++r)
            for (int c = 0; c < k.width; ++c)
                if (tile(k, c, r)) return true;
        return false;
    };
    const bool tTiles = hasTiles(t);
    auto solid = [&](const DoodadType& k, bool kTiles, int c, int r) { return !kTiles || tile(k, c, r); };
    for (const Doodad& d : doodads_) {
        const DoodadType* o = doodadTypes_.find(d.id);
        if (!o) continue;
        int ox = (d.x - o->width * 16) / 32, oy = (d.y - o->height * 16) / 32;
        const int x0 = std::max(tileX, ox), x1 = std::min(tileX + int(t.width), ox + int(o->width));
        const int y0 = std::max(tileY, oy), y1 = std::min(tileY + int(t.height), oy + int(o->height));
        if (x0 >= x1 || y0 >= y1) continue;
        const bool oTiles = hasTiles(*o);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (solid(t, tTiles, x - tileX, y - tileY) && solid(*o, oTiles, x - ox, y - oy)) return true;
    }
    return false;
}

std::optional<std::pair<int, int>> Editor::nearestDoodadFit(uint16_t id, int x, int y, bool checkTerrain, int radiusTiles) const {
    const DoodadType* t = doodadTypes_.find(id);
    if (!t) return std::nullopt;
    int tx, ty;
    doodadOrigin(*t, x, y, tx, ty);
    std::vector<std::pair<int, int>> offsets;  // nearest first
    for (int dy = -radiusTiles; dy <= radiusTiles; ++dy)
        for (int dx = -radiusTiles; dx <= radiusTiles; ++dx)
            if (dx * dx + dy * dy <= radiusTiles * radiusTiles) offsets.push_back({dx, dy});
    std::stable_sort(offsets.begin(), offsets.end(),
                     [](const auto& a, const auto& b) { return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second; });
    for (auto [dx, dy] : offsets)
        if (!doodadOverlaps(*t, tx + dx, ty + dy) && doodadFits(chk_, *t, tx + dx, ty + dy, checkTerrain))
            return std::pair<int, int>{(tx + dx) * 32 + t->width * 16, (ty + dy) * 32 + t->height * 16};
    return std::nullopt;
}

bool Editor::doodadScatterFits(const DoodadType& t, int tx, int ty, const std::vector<std::array<uint16_t, 2>>& terrains,
                               const TerrainLayer& under) const {
    auto terrainsOf = [&](uint16_t g) { return g < terrains.size() ? terrains[g] : std::array<uint16_t, 2>{0, 0}; };
    // The doodad's own terrains: those of the groups its cells need.
    std::vector<uint16_t> home;
    for (uint16_t need : t.required)
        if (need)
            for (uint16_t k : terrainsOf(need))
                if (k && std::find(home.begin(), home.end(), k) == home.end()) home.push_back(k);
    const bool needsNone = std::all_of(t.required.begin(), t.required.end(), [](uint16_t g) { return g == 0; });
    if (!needsNone && home.empty()) return true;  // (groups of no known terrain: dddata.bin alone decides)
    for (int r = 0; r < t.height; ++r)
        for (int c = 0; c < t.width; ++c) {
            const size_t cell = size_t(r) * t.width + size_t(c);
            if (cell < t.required.size() && t.required[cell] && (r > 0 || t.height == 1)) continue;  // (doodadFits checks it)
            const int x = tx + c, y = ty + r;
            if (x < 0 || y < 0 || x >= under.width || y >= under.height) return false;
            const std::array<uint16_t, 2> k = terrainsOf(uint16_t(under.tiles[size_t(y) * under.width + size_t(x)] >> 4));
            if (!k[0]) return false;  // a tile of no terrain (a doodad's, or one that Isometric never places)
            if (needsNone) {  // one terrain under all of it: that of its first cell, and no edge
                if (k[1]) return false;
                if (home.empty()) home.push_back(k[0]);
                else if (k[0] != home[0]) return false;
                continue;
            }
            for (uint16_t one : k)
                if (one && std::find(home.begin(), home.end(), one) == home.end()) return false;
        }
    return true;
}

int Editor::sprayDoodads(const std::vector<uint16_t>& ids, int x, int y, int radius, int count, uint32_t seed, bool checkTerrain) {
    if (ids.empty() || count <= 0) return 0;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> unit(-1, 1);
    Chk before = chk_;
    int placed = 0;
    const auto terrains = checkTerrain ? tileGroupTerrains(tileset_) : std::vector<std::array<uint16_t, 2>>{};
    const TerrainLayer under = checkTerrain ? readTerrain(chk_, chk_.has("TILE") ? "TILE" : "MTXM") : TerrainLayer{};
    for (int i = 0; i < count; ++i) {
        const DoodadType* t = doodadTypes_.find(ids[rng() % ids.size()]);
        if (!t) continue;
        double dx, dy;
        do dx = unit(rng), dy = unit(rng);
        while (dx * dx + dy * dy > 1);
        for (auto [px, py] : symmetricPoints(int(x + dx * radius), int(y + dy * radius))) {
            int tx, ty;
            doodadOrigin(*t, px, py, tx, ty);
            if (!doodadFits(chk_, *t, tx, ty, checkTerrain) || doodadOverlaps(*t, tx, ty)) continue;
            if (checkTerrain && !doodadScatterFits(*t, tx, ty, terrains, under)) continue;
            if (stardraft::placeDoodad(chk_, *t, tileset_, tx, ty, 0, checkTerrain)) {
                doodads_ = readDoodads(chk_);
                ++placed;
            }
        }
    }
    if (!placed) return 0;
    if (!inStroke_) pushUndo(before);  // in a stroke (a held spray): its first edit took the snapshot
    sprites_ = readSprites(chk_);
    commit(true);
    return placed;
}

// The sections of `from` (all of them, or none: removed) into this map, then the state again from the map. One undo step.
static void copySections(Chk& to, const Chk& from, std::initializer_list<SectionName> names) {
    for (const SectionName& n : names) {
        if (from.has(n)) to.set(n, from.effective(n));
        else to.remove(n);
    }
}

bool Editor::copyMapData(const Chk& from, std::string& error) {
    if (!from.has("UNIx") && !from.has("UNIS") && !from.has("UPGx") && !from.has("TECx")) return error = "the map has no unit, upgrade or technology settings", false;
    const SettingDefaults* defaults = settingDefaults();
    if (!defaults) return error = "the game's unit values did not load", false;
    Chk before = chk_;
    const UnitSettings ours = readUnitSettings(chk_, *defaults);  // (the custom unit names: string ids of this map)
    copySections(chk_, from, {"UNIS", "UNIx", "UPGS", "UPGx", "TECS", "TECx", "UPGR", "PUPx", "PTEC", "PTEx", "PUNI"});
    // The custom names stay this map's, in both sections of the pair (the other map's string ids mean nothing here). A
    // named unit that the other map leaves at the game's values takes those values, without "use default" (which
    // would give the game's name too).
    const UnitSettings now = readUnitSettings(chk_, *defaults);
    UnitSettings want = now;
    for (size_t i = 0; i < want.units.size(); ++i) {
        UnitSettings::Unit& u = want.units[i];
        const UnitSettings::Unit& o = ours.units[i];
        if (o.useDefault || !o.nameString) {
            u.nameString = 0;
            continue;
        }
        if (u.useDefault) u = defaults->units.units[i], u.useDefault = false;
        u.nameString = o.nameString;
    }
    writeUnitSettings(chk_, now, want);
    reload();
    pushUndo(before);
    commit(true);
    return true;
}

bool Editor::copyTerrain(const Chk& from, std::string& error) {
    uint16_t w = 0, h = 0;
    readDimensions(from, w, h);
    if (w != width_ || h != height_) return error = "the maps have different sizes", false;
    Chk before = chk_;
    // The doodads' overlay sprites (THG2, drawn as sprites at their doodad's place) go with the doodads: this map's
    // out, the other map's in; the other sprites stay.
    auto overlay = [](const MapSprite& s, const std::vector<Doodad>& dds) {
        if (!(s.flags & SpriteFlag::DrawAsSprite)) return false;
        for (const Doodad& d : dds)
            if (d.x == s.x && d.y == s.y) return true;
        return false;
    };
    std::vector<MapSprite> sprites;
    const std::vector<Doodad> ours = readDoodads(chk_), theirs = readDoodads(from);
    for (const MapSprite& s : readSprites(chk_))
        if (!overlay(s, ours)) sprites.push_back(s);
    for (const MapSprite& s : readSprites(from))
        if (overlay(s, theirs)) sprites.push_back(s);
    writeSprites(chk_, sprites);
    copySections(chk_, from, {"ERA ", "MTXM", "TILE", "ISOM", "DD2 "});
    reload();
    pushUndo(before);
    commit(true);
    return true;
}

int Editor::decorate(const std::vector<uint16_t>& ids, Rect area, int spacingTiles, uint32_t seed, const std::vector<Rect>& avoid) {
    if (ids.empty() || spacingTiles < 1) return 0;
    area.left = std::max(0, area.left), area.top = std::max(0, area.top);
    area.right = std::min(int(width_) * 32, area.right), area.bottom = std::min(int(height_) * 32, area.bottom);
    std::mt19937 rng(seed);
    const int cell = spacingTiles * 32;
    Chk before = chk_;
    int placed = 0;
    // Tiles that no doodad may cover (the units: a Start Location's 4 x 3 tiles, the tiles round another unit; and
    // `avoid`, in pixels), and tiles that a doodad's box covers (only there the exact test of doodadOverlaps runs: it
    // looks at every doodad of the map).
    const int w = width_, h = height_;
    std::vector<uint8_t> blocked(size_t(w) * h, 0), busy(size_t(w) * h, 0);
    auto mark = [&](std::vector<uint8_t>& m, int x0, int y0, int x1, int y1) {  // tiles [x0, x1) x [y0, y1)
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x) m[size_t(y) * w + size_t(x)] = 1;
    };
    auto any = [&](const std::vector<uint8_t>& m, int x0, int y0, int x1, int y1) {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x)
                if (m[size_t(y) * w + size_t(x)]) return true;
        return false;
    };
    for (const Unit& u : units_) {
        const int hx = u.type == 214 ? 64 : 32, hy = u.type == 214 ? 48 : 32;
        mark(blocked, (u.x - hx) / 32, (u.y - hy) / 32, (u.x + hx + 31) / 32, (u.y + hy + 31) / 32);
    }
    for (const Rect& r : avoid) mark(blocked, r.left / 32, r.top / 32, (r.right + 31) / 32, (r.bottom + 31) / 32);
    auto markDoodad = [&](const Doodad& d) {
        if (const DoodadType* o = doodadTypes_.find(d.id)) {
            const int ox = (d.x - o->width * 16) / 32, oy = (d.y - o->height * 16) / 32;
            mark(busy, ox, oy, ox + o->width, oy + o->height);
        }
    };
    for (const Doodad& d : doodads_) markDoodad(d);
    const auto terrains = tileGroupTerrains(tileset_);
    const TerrainLayer under = readTerrain(chk_, chk_.has("TILE") ? "TILE" : "MTXM");  // (placeDoodad does not change TILE)
    // A cell of the area at a time: a random place in it, and up to four doodads tried there; one that fits its tiles
    // (dddata.bin) and overlaps no doodad stays.
    for (int cy = area.top; cy < area.bottom; cy += cell)
        for (int cx = area.left; cx < area.right; cx += cell) {
            const int x = cx + int(rng() % uint32_t(std::max(1, std::min(cell, area.right - cx))));
            const int y = cy + int(rng() % uint32_t(std::max(1, std::min(cell, area.bottom - cy))));
            for (int tries = 0; tries < 4; ++tries) {
                const DoodadType* t = doodadTypes_.find(ids[rng() % ids.size()]);
                if (!t) continue;
                int tx, ty;
                doodadOrigin(*t, x, y, tx, ty);
                if (any(blocked, tx, ty, tx + t->width, ty + t->height)) continue;
                if (!doodadFits(chk_, *t, tx, ty, true) || !doodadScatterFits(*t, tx, ty, terrains, under)) continue;
                if (any(busy, tx, ty, tx + t->width, ty + t->height) && doodadOverlaps(*t, tx, ty)) continue;
                if (stardraft::placeDoodad(chk_, *t, tileset_, tx, ty, 0, true)) {
                    Doodad d;  // (as placeDoodad wrote it: the map is read again once, at the end)
                    d.id = t->id, d.x = uint16_t(tx * 32 + t->width * 16), d.y = uint16_t(ty * 32 + t->height * 16), d.owner = 0;
                    doodads_.push_back(d);
                    markDoodad(d);
                    ++placed;
                    break;
                }
            }
        }
    if (!placed) return 0;
    pushUndo(before);  // (one undo step for all of them)
    doodads_ = readDoodads(chk_);
    sprites_ = readSprites(chk_);
    commit(true);
    return placed;
}

bool Editor::placeDoodad(uint16_t id, int x, int y, bool checkTerrain, std::string* reason) {
    const DoodadType* t = doodadTypes_.find(id);
    if (!t) {
        if (reason) *reason = "unknown doodad";
        return false;
    }
    int tx, ty;
    doodadOrigin(*t, x, y, tx, ty);
    if (doodadOverlaps(*t, tx, ty)) {
        if (reason) *reason = "overlaps another doodad";
        return false;
    }
    Chk before = chk_;
    if (!stardraft::placeDoodad(chk_, *t, tileset_, tx, ty, 0, checkTerrain, reason)) {
        chk_ = std::move(before);
        return false;
    }
    doodads_ = readDoodads(chk_);
    // Mirror images where they fit.
    auto pts = symmetricPoints(x, y);
    for (size_t k = 1; k < pts.size(); ++k) {
        int mx, my;
        doodadOrigin(*t, pts[k].first, pts[k].second, mx, my);
        if (!doodadOverlaps(*t, mx, my) && stardraft::placeDoodad(chk_, *t, tileset_, mx, my, 0, checkTerrain))
            doodads_ = readDoodads(chk_);
    }
    if (!inStroke_) pushUndo(before);  // in a stroke (a drag that places many), its start took the undo step
    doodads_ = readDoodads(chk_);
    sprites_ = readSprites(chk_);
    commit(true);
    return true;
}

// --- Locations -------------------------------------------------------------------------------------

bool Editor::locationUsed(int i) const {
    return i >= 0 && i < int(locations_.size()) && i != kAnywhereLocation && locations_[i].used();
}

std::string Editor::locationName(int i) const {
    if (i < 0 || i >= int(locations_.size())) return {};
    if (locations_[i].nameString && !strings_.get(locations_[i].nameString).empty()) return strings_.get(locations_[i].nameString);
    return i == kAnywhereLocation ? "Anywhere" : "Location " + std::to_string(i + 1);
}

Rect Editor::locationRect(int i) const {
    const Location& l = locations_[i];
    return Rect{int(l.left), int(l.top), int(l.right), int(l.bottom)}.normalized();
}

Rect Editor::locationEdges(int i) const {
    if (i < 0 || i >= int(locations_.size())) return {};
    const Location& l = locations_[size_t(i)];
    return Rect{int(l.left), int(l.top), int(l.right), int(l.bottom)};
}

int Editor::locationAt(int x, int y) const {
    int best = -1;
    long long bestArea = 1LL << 62;
    for (int i = 0; i < int(locations_.size()); ++i) {
        if (!locationUsed(i)) continue;
        Rect r = locationRect(i);
        if (!r.contains(x, y)) continue;
        long long area = 1LL * (r.right - r.left) * (r.bottom - r.top);
        if (area < bestArea) best = i, bestArea = area;
    }
    return best;
}

bool Editor::locationInverted(int i) const { return locationInvertedAxes(i) != 0; }
int Editor::locationInvertedAxes(int i) const {
    if (i < 0 || i >= int(locations_.size())) return 0;
    const Location& l = locations_[size_t(i)];
    return (l.left > l.right ? 1 : 0) | (l.top > l.bottom ? 2 : 0);
}

int Editor::createLocation(const Rect& rect, bool inverted, int axes, const std::string& name, uint16_t excluded) {
    Rect r = rect.normalized();
    for (int i = 0; i < int(locations_.size()); ++i) {
        if (i == kAnywhereLocation || locations_[i].used()) continue;
        beginEdit();
        Location& l = locations_[i];
        l = Location{};
        l.left = uint32_t(std::max(r.left, 0));
        l.top = uint32_t(std::max(r.top, 0));
        l.right = uint32_t(std::max(r.right, r.left + 1));
        l.bottom = uint32_t(std::max(r.bottom, r.top + 1));
        if (inverted && (axes & 1)) std::swap(l.left, l.right);
        if (inverted && (axes & 2)) std::swap(l.top, l.bottom);
        l.nameString = uint16_t(strings_.add(name.empty() ? "Location " + std::to_string(i + 1) : name));
        l.elevation = uint16_t(excluded & LocationElevation::All);
        strings_.write(chk_);
        writeLocations(chk_, locations_);
        selectedLocation_ = i;
        commit(false);
        return i;
    }
    return -1;
}

void Editor::setLocationRect(int i, const Rect& rect) {
    if (!locationUsed(i)) return;
    Rect r = rect.normalized();
    const int inverted = locationInvertedAxes(i);  // moved or resized: it stays inverted (on the same axes)
    beginEdit();
    Location& l = locations_[i];
    l.left = uint32_t(std::max(r.left, 0));
    l.top = uint32_t(std::max(r.top, 0));
    l.right = uint32_t(std::max(r.right, int(l.left) + 1));
    l.bottom = uint32_t(std::max(r.bottom, int(l.top) + 1));
    if (inverted & 1) std::swap(l.left, l.right);
    if (inverted & 2) std::swap(l.top, l.bottom);
    writeLocations(chk_, locations_);
    commit(false);
}

// --- Players and forces -----------------------------------------------------------------------------

namespace {
constexpr uint16_t kStartLocationUnit = 214;

Bytes sectionOr(const Chk& chk, SectionName name, size_t size, uint8_t fill) {
    Bytes b;
    if (const auto* s = chk.find(name)) b = s->data;
    if (b.size() < size) b.resize(size, fill);
    return b;
}
}  // namespace

namespace {
// The game's player colors (COLR 0-11; Remastered's CRGB adds 12-21). scenario.chk wiki.
constexpr uint32_t kColorRgb[Editor::kColorIds] = {0xF40404, 0x0C48CC, 0x2CB494, 0x88409C, 0xF88C14, 0x703014, 0xCCE0D0, 0xFCFC38,
                                                   0x088008, 0xFCFC7C, 0xECC4B0, 0x4068D4, 0x74A47C, 0x7290B8, 0x00E4FC, 0xFFC4E4,
                                                   0x808000, 0xD2F53C, 0x000080, 0xF032E6, 0x808080, 0x3C3C3C};
constexpr const char* kColorNames[Editor::kColorIds] = {"Red",   "Blue",  "Teal",        "Purple",       "Orange", "Brown",
                                                        "White", "Yellow", "Green",      "Pale yellow",  "Tan",    "Azure",
                                                        "Pale green", "Blueish grey", "Cyan", "Pink", "Olive", "Lime",
                                                        "Navy",  "Magenta", "Grey",      "Black"};
// COLR, or each player's own color when the map has none.
Bytes colorSection(const Chk& chk) {
    if (const Section* s = chk.find("COLR"); s && s->data.size() >= 8) return Bytes(s->data.begin(), s->data.begin() + 8);
    return Bytes{0, 1, 2, 3, 4, 5, 6, 7};
}
}  // namespace

uint32_t Editor::colorIdRgb(int id) { return id >= 0 && id < kColorIds ? kColorRgb[id] : 0x808080; }
const char* Editor::colorIdName(int id) { return id >= 0 && id < kColorIds ? kColorNames[id] : "Random"; }

uint32_t Editor::playerRgb(int p) const {
    if (p < 0 || p >= 8) return colorIdRgb(p == 11 ? 11 : p);
    const PlayerColor c = player(p).color;
    if (c.kind == PlayerColor::Custom) return c.rgb;
    if (c.kind == PlayerColor::Id && c.id < kColorIds) return colorIdRgb(c.id);
    const uint8_t id = colorSection(chk_)[size_t(p)];
    return colorIdRgb(id < 12 ? id : p);
}

Editor::Player Editor::player(int p) const {
    Player out;
    if (p < 0 || p >= 8) return out;
    const Bytes ownr = sectionOr(chk_, "OWNR", 12, 6), side = sectionOr(chk_, "SIDE", 12, 5), forc = sectionOr(chk_, "FORC", 20, 0);
    out.controller = Controller(ownr[size_t(p)]);
    out.race = Race(side[size_t(p)]);
    out.force = std::min<int>(forc[size_t(p)], 3);
    if (const Section* crgb = chk_.find("CRGB"); crgb && crgb->data.size() >= 32) {
        const uint8_t* d = crgb->data.data();
        const uint8_t kind = d[24 + p];
        out.color.kind = kind <= 3 ? PlayerColor::Kind(kind) : PlayerColor::Random;
        out.color.rgb = uint32_t(d[3 * p]) << 16 | uint32_t(d[3 * p + 1]) << 8 | d[3 * p + 2];
        out.color.id = d[3 * p + 2];
        if (out.color.kind != PlayerColor::Custom) out.color.rgb = 0;
        if (out.color.kind != PlayerColor::Id) out.color.id = 0;
    } else {
        out.color.kind = PlayerColor::Id;
        out.color.id = colorSection(chk_)[size_t(p)];
    }
    return out;
}

Editor::Force Editor::force(int f) const {
    Force out;
    if (f < 0 || f >= 4) return out;
    const Bytes forc = sectionOr(chk_, "FORC", 20, 0);
    const uint16_t str = uint16_t(forc[8 + 2 * size_t(f)] | forc[9 + 2 * size_t(f)] << 8);
    out.name = str ? strings_.get(str) : "";
    if (out.name.empty()) out.name = "Force " + std::to_string(f + 1);
    out.flags = forc[16 + size_t(f)];
    return out;
}

void Editor::setPlayersAndForces(const std::vector<Player>& players, const std::vector<Force>& forces) {
    // Colors: COLR and CRGB change only when a color changes (a map saved unchanged keeps its bytes); CRGB is made
    // only when a color needs it (random, the player's choice, RGB, or ids 12 and up).
    bool colorsChanged = false, needCrgb = chk_.find("CRGB") != nullptr;
    for (size_t p = 0; p < std::min<size_t>(players.size(), 8); ++p) {
        const PlayerColor& c = players[p].color;
        colorsChanged |= !(c == player(int(p)).color);
        needCrgb |= c.kind != PlayerColor::Id || c.id >= 12;
    }
    Bytes colr, crgb;
    if (colorsChanged) {
        colr = colorSection(chk_);
        if (const Section* s = chk_.find("CRGB"); s && s->data.size() >= 32) crgb.assign(s->data.begin(), s->data.begin() + 32);
        else if (needCrgb) {  // made from COLR: each player its COLR color by id
            crgb.assign(32, 0);
            for (size_t p = 0; p < 8; ++p) crgb[3 * p + 2] = colr[p], crgb[24 + p] = PlayerColor::Id;
        }
        for (size_t p = 0; p < std::min<size_t>(players.size(), 8); ++p) {
            const PlayerColor& c = players[p].color;
            if (c.kind == PlayerColor::Id && c.id < 12) colr[p] = c.id;  // the leaderboard's color too
            if (crgb.empty()) continue;
            crgb[24 + p] = c.kind;
            if (c.kind == PlayerColor::Custom)
                crgb[3 * p] = uint8_t(c.rgb >> 16), crgb[3 * p + 1] = uint8_t(c.rgb >> 8), crgb[3 * p + 2] = uint8_t(c.rgb);
            else if (c.kind == PlayerColor::Id)
                crgb[3 * p] = 0, crgb[3 * p + 1] = 0, crgb[3 * p + 2] = c.id;
        }
    }
    beginEdit();
    if (colorsChanged) {
        chk_.set("COLR", std::move(colr));
        if (!crgb.empty()) chk_.set("CRGB", std::move(crgb));
    }
    Bytes ownr = sectionOr(chk_, "OWNR", 12, 6), side = sectionOr(chk_, "SIDE", 12, 5), forc = sectionOr(chk_, "FORC", 20, 0);
    for (size_t p = 0; p < std::min<size_t>(players.size(), 8); ++p) {
        ownr[p] = uint8_t(players[p].controller);
        side[p] = uint8_t(players[p].race);
        forc[p] = uint8_t(std::clamp(players[p].force, 0, 3));
    }
    for (size_t f = 0; f < std::min<size_t>(forces.size(), 4); ++f) {
        uint16_t str = uint16_t(forc[8 + 2 * f] | forc[9 + 2 * f] << 8);
        if (str) strings_.set(str, forces[f].name);
        else if (!forces[f].name.empty()) str = uint16_t(strings_.add(forces[f].name));
        forc[8 + 2 * f] = uint8_t(str), forc[9 + 2 * f] = uint8_t(str >> 8);
        forc[16 + f] = forces[f].flags;
    }
    strings_.write(chk_);
    chk_.set("IOWN", ownr);  // the editor's copy of the slots; StarEdit keeps it equal to OWNR
    chk_.set("OWNR", std::move(ownr));
    chk_.set("SIDE", std::move(side));
    chk_.set("FORC", std::move(forc));
    commit(false);
}

void Editor::setPlayer(int p, const Player& pl) {
    if (p < 0 || p >= 8) return;
    std::vector<Player> players;
    std::vector<Force> forces;
    for (int i = 0; i < 8; ++i) players.push_back(i == p ? pl : player(i));
    for (int i = 0; i < 4; ++i) forces.push_back(force(i));
    setPlayersAndForces(players, forces);
}

void Editor::setForce(int f, const Force& fo) {
    if (f < 0 || f >= 4) return;
    std::vector<Player> players;
    std::vector<Force> forces;
    for (int i = 0; i < 8; ++i) players.push_back(player(i));
    for (int i = 0; i < 4; ++i) forces.push_back(i == f ? fo : force(i));
    setPlayersAndForces(players, forces);
}

// --- Classic settings -------------------------------------------------------------------------------------------

const SettingDefaults* Editor::settingDefaults() const {
    if (!settingDefaultsTried_) {
        settingDefaultsTried_ = true;
        auto d = std::make_unique<SettingDefaults>();
        std::string err;
        if (d->load(data_, err)) settingDefaults_ = std::move(d);
    }
    return settingDefaults_.get();
}

UnitSettings Editor::unitSettings() const {
    const SettingDefaults* d = settingDefaults();
    return d ? readUnitSettings(chk_, *d) : UnitSettings{};
}
UpgradeSettings Editor::upgradeSettings() const {
    const SettingDefaults* d = settingDefaults();
    return d ? readUpgradeSettings(chk_, *d) : UpgradeSettings{};
}
TechSettings Editor::techSettings() const {
    const SettingDefaults* d = settingDefaults();
    return d ? readTechSettings(chk_, *d) : TechSettings{};
}

std::string Editor::unitSettingName(int unit) const {
    if (unit < 0 || unit >= kSettingUnits) return {};
    const uint16_t id = unitSettings().units[size_t(unit)].nameString;
    return id ? strings_.get(id) : std::string();
}

bool Editor::setUnitSettings(const UnitSettings& settings, const std::vector<std::pair<int, std::string>>& names) {
    const SettingDefaults* d = settingDefaults();
    if (!d) return false;
    const UnitSettings from = readUnitSettings(chk_, *d);
    UnitSettings to = settings;
    std::vector<std::pair<int, std::string>> renamed;
    for (const auto& [u, text] : names) {
        if (u < 0 || u >= kSettingUnits) continue;
        const uint16_t id = to.units[size_t(u)].nameString;
        if (text != (id ? strings_.get(id) : std::string())) renamed.emplace_back(u, text);
    }
    if (from == to && renamed.empty()) return false;
    beginEdit();
    if (!renamed.empty()) {
        for (const auto& [u, text] : renamed) {
            uint16_t& id = to.units[size_t(u)].nameString;
            if (text.empty()) id = 0;
            else if (id) strings_.set(id, text);
            else id = uint16_t(strings_.add(text));
        }
        strings_.write(chk_);
    }
    writeUnitSettings(chk_, from, to);
    commit(false);
    return true;
}

bool Editor::setUpgradeSettings(const UpgradeSettings& settings) {
    const SettingDefaults* d = settingDefaults();
    if (!d) return false;
    const UpgradeSettings from = readUpgradeSettings(chk_, *d);
    if (from == settings) return false;
    beginEdit();
    writeUpgradeSettings(chk_, from, settings);
    commit(false);
    return true;
}

bool Editor::setTechSettings(const TechSettings& settings) {
    const SettingDefaults* d = settingDefaults();
    if (!d) return false;
    const TechSettings from = readTechSettings(chk_, *d);
    if (from == settings) return false;
    beginEdit();
    writeTechSettings(chk_, from, settings);
    commit(false);
    return true;
}

// --- Classic triggers -------------------------------------------------------------------------------------------

ClassicTriggers Editor::classicTriggers() const { return readClassicTriggers(chk_); }

namespace {
bool sameStrings(const StringTable& a, const StringTable& b) {
    if (a.size() != b.size()) return false;
    for (uint32_t i = 1; i <= a.size(); ++i)
        if (a.get(i) != b.get(i)) return false;
    return true;
}
}  // namespace

bool Editor::setClassicTriggers(const ClassicTriggers& triggers, const StringTable* strings) {
    const bool newStrings = strings && !sameStrings(*strings, strings_);
    const Bytes now = chk_.effective("TRIG"), to = encodeClassicTriggers(triggers);
    if (now == to && !newStrings) return false;
    beginEdit();
    if (newStrings) {
        strings_ = *strings;
        strings_.write(chk_);
    }
    writeClassicTriggers(chk_, triggers);
    commit(false);
    return true;
}

bool Editor::setClassicTrigger(int index, const ClassicTrigger& trigger, const StringTable* strings) {
    ClassicTriggers t = classicTriggers();
    if (index < 0 || index >= int(t.triggers.size())) return false;
    t.triggers[size_t(index)] = trigger;
    return setClassicTriggers(t, strings);
}

bool Editor::addClassicTrigger(const ClassicTrigger& trigger, int at, const StringTable* strings) {
    ClassicTriggers t = classicTriggers();
    if (at < 0 || at > int(t.triggers.size())) at = int(t.triggers.size());
    t.triggers.insert(t.triggers.begin() + at, trigger);
    return setClassicTriggers(t, strings);
}

bool Editor::removeClassicTrigger(int index) {
    ClassicTriggers t = classicTriggers();
    if (index < 0 || index >= int(t.triggers.size())) return false;
    t.triggers.erase(t.triggers.begin() + index);
    return setClassicTriggers(t);
}

bool Editor::moveClassicTrigger(int from, int to) {
    ClassicTriggers t = classicTriggers();
    const int n = int(t.triggers.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
    const ClassicTrigger v = t.triggers[size_t(from)];
    t.triggers.erase(t.triggers.begin() + from);
    t.triggers.insert(t.triggers.begin() + to, v);
    return setClassicTriggers(t);
}

std::string Editor::switchName(int index) const {
    const Bytes b = chk_.effective("SWNM");
    const size_t at = size_t(index) * 4;
    if (index < 0 || at + 4 > b.size()) return {};
    const uint32_t id = uint32_t(b[at] | b[at + 1] << 8 | b[at + 2] << 16 | uint32_t(b[at + 3]) << 24);
    return strings_.get(id);
}

std::vector<uint32_t> Editor::soundStrings() const {
    std::vector<uint32_t> out;
    const Bytes wav = chk_.effective("WAV ");
    for (size_t i = 0; i + 4 <= wav.size() && i < 512 * 4; i += 4) {
        const uint32_t id = uint32_t(wav[i] | wav[i + 1] << 8 | wav[i + 2] << 16 | uint32_t(wav[i + 3]) << 24);
        if (id && id <= strings_.size() && !strings_.get(id).empty() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    return out;
}

std::vector<BriefingTrigger> Editor::briefings() const { return readBriefings(chk_, strings_); }

void Editor::setBriefings(const std::vector<BriefingTrigger>& briefings) {
    beginEdit();
    writeBriefings(chk_, strings_, briefings);
    commit(false);
}

const ArchiveFile* Editor::archiveFile(const std::string& name) const {
    finishFiles();
    for (const ArchiveFile& f : files_)
        if (f.name.size() == name.size() && std::equal(f.name.begin(), f.name.end(), name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }))
            return &f;
    return nullptr;
}

void Editor::setArchiveFile(const std::string& name, Bytes data) {
    std::vector<ArchiveFile> one;
    one.push_back({name, std::move(data)});
    setArchiveFiles(std::move(one));
}

void Editor::setArchiveFiles(std::vector<ArchiveFile> files) {
    if (files.empty()) return;
    std::vector<std::string> paths;
    for (const auto& f : files) paths.push_back(f.name);
    pushUndo(chk_, paths);
    for (ArchiveFile& n : files) {
        auto it = std::find_if(files_.begin(), files_.end(), [&](const ArchiveFile& f) { return f.name == n.name; });
        if (it != files_.end()) it->data = std::move(n.data);
        else files_.push_back(std::move(n));
    }
    commit(false);
}

bool Editor::removeArchiveFile(const std::string& name) {
    finishFiles();
    if (std::none_of(files_.begin(), files_.end(), [&](const ArchiveFile& f) { return f.name == name; })) return false;
    pushUndo(chk_, {name});  // the undo step keeps the file
    std::erase_if(files_, [&](const ArchiveFile& f) { return f.name == name; });
    commit(false);
    return true;
}

bool Editor::renameArchiveFile(const std::string& from, const std::string& to) {
    if (to.empty() || archiveFile(to)) return false;
    auto it = std::find_if(files_.begin(), files_.end(), [&](const ArchiveFile& f) { return f.name == from; });
    if (it == files_.end()) return false;
    // One step: the file's new path, and the briefings that play it name the new path.
    pushUndo(chk_, {from, to});
    it->name = to;
    std::vector<BriefingTrigger> b = readBriefings(chk_, strings_);
    bool used = false;
    for (auto& t : b)
        for (auto& a : t.actions)
            if (a.wav == from) a.wav = to, used = true;
    if (used) writeBriefings(chk_, strings_, b);
    commit(false);
    return true;
}

std::vector<std::string> Editor::soundReferences() const {
    std::vector<std::string> out;
    const Bytes wav = chk_.effective("WAV ");
    for (size_t i = 0; i + 4 <= wav.size() && i < 512 * 4; i += 4) {
        const uint32_t id = uint32_t(wav[i] | wav[i + 1] << 8 | wav[i + 2] << 16 | uint32_t(wav[i + 3]) << 24);
        if (!id || id > strings_.size()) continue;
        const std::string& path = strings_.get(id);
        if (!path.empty() && !archiveFile(path) && std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
    }
    return out;
}

void Editor::addSoundReference(const std::string& path) {
    if (path.empty() || archiveFile(path)) return;
    for (const auto& r : soundReferences())
        if (r == path) return;
    beginEdit();
    uint32_t id = 0;
    for (uint32_t i = 1; i <= strings_.size() && !id; ++i)
        if (strings_.get(i) == path) id = i;
    if (!id) id = strings_.add(path);
    Bytes wav = chk_.effective("WAV ");
    wav.resize(512 * 4, 0);
    for (size_t i = 0; i < 512 * 4; i += 4)
        if (!(wav[i] | wav[i + 1] | wav[i + 2] | wav[i + 3])) {
            for (int b = 0; b < 4; ++b) wav[i + size_t(b)] = uint8_t(id >> (8 * b));
            break;
        }
    strings_.write(chk_);
    chk_.set("WAV ", std::move(wav));
    commit(false);
}

bool Editor::removeSoundReference(const std::string& path) {
    Bytes wav = chk_.effective("WAV ");
    bool found = false;
    for (size_t i = 0; i + 4 <= wav.size(); i += 4) {
        const uint32_t id = uint32_t(wav[i] | wav[i + 1] << 8 | wav[i + 2] << 16 | uint32_t(wav[i + 3]) << 24);
        if (id && id <= strings_.size() && strings_.get(id) == path) std::fill(wav.begin() + long(i), wav.begin() + long(i) + 4, uint8_t(0)), found = true;
    }
    if (!found) return false;
    beginEdit();
    chk_.set("WAV ", std::move(wav));
    commit(false);
    return true;
}

std::vector<std::string> Editor::playabilityProblems() const {
    std::vector<std::string> out;
    int playing = 0, humans = 0;
    bool owns[8] = {}, starts[8] = {};
    for (const Unit& u : units_) {
        if (u.owner >= 8) continue;
        (u.type == kStartLocationUnit ? starts : owns)[u.owner] = true;
    }
    for (int p = 0; p < 8; ++p) {
        const Controller c = player(p).controller;
        playing += c == Controller::Human || c == Controller::Computer;
        humans += c == Controller::Human;
        if (owns[p] && !starts[p]) out.push_back("Player " + std::to_string(p + 1) + " owns units but has no start location");
    }
    if (playing < 2) out.push_back("At least two players must be human or computer (" + std::to_string(playing) + " now)");
    if (!humans) out.push_back("At least one player must be human");
    return out;
}

void Editor::renameLocation(int i, const std::string& name) {
    if (i < 0 || i >= int(locations_.size()) || name.empty()) return;
    beginEdit();
    Location& l = locations_[i];
    if (l.nameString) strings_.set(l.nameString, name);
    else l.nameString = uint16_t(strings_.add(name));
    strings_.write(chk_);
    writeLocations(chk_, locations_);
    commit(false);
}

uint16_t Editor::locationElevation(int i) const {
    return i >= 0 && i < int(locations_.size()) ? uint16_t(locations_[i].elevation & LocationElevation::All) : 0;
}

bool Editor::setLocationElevation(int i, uint16_t excluded) {
    if (i < 0 || i >= int(locations_.size())) return false;
    Location& l = locations_[i];
    const uint16_t value = uint16_t((l.elevation & ~LocationElevation::All) | (excluded & LocationElevation::All));
    if (value == l.elevation) return false;
    beginEdit();
    l.elevation = value;
    writeLocations(chk_, locations_);
    commit(false);
    return true;
}

bool Editor::editLocation(int i, const LocationEdit& edit, std::string* error) {
    if (i < 0 || i >= int(locations_.size()) || (i != kAnywhereLocation && !locationUsed(i))) {
        if (error) *error = "There is no location " + std::to_string(i + 1);
        return false;
    }
    Location l = locations_[size_t(i)];
    const int axes = edit.invertedAxes >= 0 && i != kAnywhereLocation ? (edit.invertedAxes & 3) : locationInvertedAxes(i);
    if (edit.asTyped) {  // the edges as they are (an inverted location: left more than right, or top more than bottom)
        const Rect& r = edit.rect;
        if (i != kAnywhereLocation) {
            // An edge as the location has it stays (also one of 2^31 or more, which locationEdges gives as less than 0).
            const Rect now = locationEdges(i);
            const int typed[4] = {r.left, r.top, r.right, r.bottom}, had[4] = {now.left, now.top, now.right, now.bottom};
            uint32_t* edges[4] = {&l.left, &l.top, &l.right, &l.bottom};
            for (int k = 0; k < 4; ++k) {
                if (typed[k] == had[k]) continue;
                if (typed[k] == had[(k + 2) % 4]) {  // the other edge of its axis (the edges swapped): its raw value
                    *edges[k] = uint32_t(had[(k + 2) % 4]);
                    continue;
                }
                if (typed[k] < 0) {
                    if (error) *error = "An edge is less than 0";
                    return false;
                }
                *edges[k] = uint32_t(typed[k]);
            }
        }
    } else if (axes != locationInvertedAxes(i)) {  // another inversion: the same edges, from the lower value to the higher
        const Rect n = locationRect(i);
        l.left = uint32_t(n.left), l.top = uint32_t(n.top), l.right = uint32_t(n.right), l.bottom = uint32_t(n.bottom);
        if (axes & 1) std::swap(l.left, l.right);
        if (axes & 2) std::swap(l.top, l.bottom);
    }
    if (!edit.asTyped && i != kAnywhereLocation && !(edit.rect.left == locationRect(i).left && edit.rect.top == locationRect(i).top &&
                                    edit.rect.right == locationRect(i).right && edit.rect.bottom == locationRect(i).bottom)) {
        const Rect& r = edit.rect;
        if (r.right <= r.left || r.bottom <= r.top) {
            if (error) *error = r.right <= r.left ? "Right must be more than left" : "Bottom must be more than top";
            return false;
        }
        const int w = int(width_) * 32, h = int(height_) * 32;
        l.left = uint32_t(std::clamp(r.left, 0, w - 1));
        l.top = uint32_t(std::clamp(r.top, 0, h - 1));
        l.right = uint32_t(std::clamp(r.right, int(l.left) + 1, w));
        l.bottom = uint32_t(std::clamp(r.bottom, int(l.top) + 1, h));
        // new edges: inverted on the axes (as it was, or as the edit sets)
        if (axes & 1) std::swap(l.left, l.right);
        if (axes & 2) std::swap(l.top, l.bottom);
    }
    l.elevation = uint16_t((l.elevation & ~LocationElevation::All) | (edit.excluded & LocationElevation::All));
    const bool rename = !edit.name.empty() && edit.name != locationName(i);
    const Location& was = locations_[size_t(i)];
    if (!rename && l.left == was.left && l.top == was.top && l.right == was.right && l.bottom == was.bottom && l.elevation == was.elevation)
        return false;
    beginEdit();
    if (rename) {
        if (l.nameString) strings_.set(l.nameString, edit.name);
        else l.nameString = uint16_t(strings_.add(edit.name));
        strings_.write(chk_);
    }
    locations_[size_t(i)] = l;
    writeLocations(chk_, locations_);
    commit(false);
    return true;
}

void Editor::deleteLocation(int i) {
    if (!locationUsed(i)) return;
    beginEdit();
    if (locations_[i].nameString) strings_.set(locations_[i].nameString, "");
    locations_[i] = Location{};
    strings_.write(chk_);
    writeLocations(chk_, locations_);
    if (selectedLocation_ == i) selectedLocation_ = -1;
    commit(false);
}

}  // namespace stardraft
