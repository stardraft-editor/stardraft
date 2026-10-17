#include "stardraft/isom_core.h"

#include <cstring>
#include <map>
#include <mutex>

#include "IsomApi.h"  // third_party/IsomTerrain/IsomTerrain (unchanged; its includes are StarDraft's stand-ins beside it)
#include "stardraft/terrain.h"

Logger logger;  // (IsomApi.h's; it writes nothing: third_party/IsomTerrain/CrossCutLib/Logger.h)

namespace stardraft {

namespace {

static_assert(sizeof(Sc::Isom::TileGroup) == 52, "a CV5 group is 52 bytes");

// The core's tables for a tileset, made from its CV5 groups once (Sc::Terrain_::Tiles: the isom links, the terrain
// type map, the tile groups by their links), kept for each tileset and CV5 (a mod can change the CV5).
const Sc::Terrain_::Tiles* tablesFor(const Tileset& tileset) {
    static std::mutex m;
    static std::map<std::pair<uint16_t, std::vector<uint8_t>>, std::unique_ptr<Sc::Terrain_::Tiles>> cache;
    std::vector<uint8_t> cv5(tileset.groups.size() * sizeof(Sc::Isom::TileGroup));
    for (size_t i = 0; i < tileset.groups.size(); ++i) {
        const TileGroup& g = tileset.groups[i];
        uint8_t* p = &cv5[i * sizeof(Sc::Isom::TileGroup)];
        auto put16 = [&p](uint16_t v) { std::memcpy(p, &v, 2), p += 2; };  // (little-endian, as CV5 is)
        put16(g.type);
        *p++ = g.buildability, *p++ = g.groundHeight;
        for (uint16_t v : g.edges) put16(v);
        for (uint16_t v : g.extra) put16(v);
        for (uint16_t v : g.megatiles) put16(v);
    }
    const uint16_t era = uint16_t(tileset.era & 7);
    std::lock_guard<std::mutex> lock(m);
    auto key = std::make_pair(era, std::move(cv5));
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();
    auto tiles = std::make_unique<Sc::Terrain_::Tiles>();
    const size_t n = key.second.size() / sizeof(Sc::Isom::TileGroup);
    tiles->tileGroups.resize(n);
    if (n) std::memcpy(tiles->tileGroups.data(), key.second.data(), key.second.size());
    tiles->loadIsom(era);
    return cache.emplace(std::move(key), std::move(tiles)).first->second.get();
}

// ScMap's own steps of placeIsomTerrain are private; paintDiamonds runs them for any set of diamonds. IsomApi.h stays
// unchanged: they are reached with the explicit-instantiation access idiom (a template instantiated with a pointer to
// a private member may name it; the friend function hands it out).
template <typename Tag, typename Tag::type Member>
struct Reach {
    friend typename Tag::type reach(Tag) { return Member; }
};
struct SetDiamondTag {
    using type = void (ScMap::*)(::Chk::IsomDiamond, uint16_t, bool, ::Chk::IsomCache&);
    friend type reach(SetDiamondTag);
};
struct RadialUpdateTag {
    using type = void (ScMap::*)(bool, std::deque<::Chk::IsomDiamond>&, ::Chk::IsomCache&);
    friend type reach(RadialUpdateTag);
};
template struct Reach<SetDiamondTag, &ScMap::setDiamondIsomValues>;
template struct Reach<RadialUpdateTag, &ScMap::radiallyUpdateTerrain>;

// ScMap::isInBounds and diamondNeedsUpdate (private), from the public ISOM: a diamond on the map, not set by this edit
// (its Modified flag), with a terrain.
bool inBounds(const ScMap& map, ::Chk::IsomDiamond d) { return d.x < map.getIsomWidth() && d.y < map.getIsomHeight(); }
bool needsUpdate(const ScMap& map, ::Chk::IsomDiamond d) {
    if (!inBounds(map, d)) return false;
    const ::Chk::IsomRect& r = map.isomRects[d.y * map.getIsomWidth() + d.x];
    return !r.isLeftModified() && (r.left >> 4) != 0;
}

std::vector<uint16_t> readU16s(const Chk& chk, SectionName name) {
    const Bytes b = chk.effective(name);
    std::vector<uint16_t> v(b.size() / 2);
    if (!v.empty()) std::memcpy(v.data(), b.data(), v.size() * 2);
    return v;
}

Bytes bytesOf(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    return Bytes(b, b + n);
}

}  // namespace

struct IsomCore::Impl {
    const Sc::Terrain_::Tiles* tables = nullptr;
    ScMap map{};
    std::unique_ptr<::Chk::IsomCache> cache;
    int untileable = 0;

    // The tiles of what changed (updateTilesFromIsom), counting first the changed rects that no tile group fits (the
    // core gives them the null tile).
    void updateTiles() {
        const Sc::BoundingBox a = cache->changedArea;
        for (size_t y = a.top; y <= a.bottom && y + 1 < cache->isomHeight; ++y)
            for (size_t x = a.left; x <= a.right && x + 1 < cache->isomWidth; ++x) {
                const ::Chk::IsomRect& r = map.isomRects[y * cache->isomWidth + x];
                if (r.isLeftOrRightModified() && !cache->hashToTileGroup->count(r.getHash(cache->isomLinks))) ++untileable;
            }
        map.updateTilesFromIsom(*cache);
    }

    // The core's terrain type (an index into its brush table) for an ISOM solid value; 0 for none.
    size_t terrainType(uint16_t isomValue) const {
        for (const auto& t : tables->terrainTypes)
            if (t.isomValue == isomValue && t.index != 0) return t.index;
        return 0;
    }
};

IsomCore::IsomCore() : impl_(std::make_unique<Impl>()) {}
IsomCore::~IsomCore() = default;

bool IsomCore::loaded() const { return impl_->cache != nullptr; }

bool IsomCore::load(const Chk& chk, const Tileset& tileset, std::string& error) {
    Impl& m = *impl_;
    m.cache.reset();
    m.untileable = 0;
    uint16_t width = 0, height = 0;
    if (!readDimensions(chk, width, height) || !width || !height) return error = "The map has no size (DIM)", false;
    if (tileset.groups.empty()) return error = "The tileset has no tile groups (CV5)", false;
    const size_t isomSize = (size_t(width) / 2 + 1) * (size_t(height) + 1);
    const Bytes isom = chk.effective("ISOM");
    if (isom.size() < isomSize * sizeof(::Chk::IsomRect)) return error = "The map's ISOM section is missing or short", false;
    m.tables = tablesFor(tileset);
    m.map = ScMap{};
    m.map.tileWidth = width, m.map.tileHeight = height;
    m.map.tileset = Sc::Terrain::Tileset(readEra(chk) & 7);
    m.map.isomRects.resize(isomSize);
    std::memcpy(m.map.isomRects.data(), isom.data(), isomSize * sizeof(::Chk::IsomRect));
    const size_t tileCount = size_t(width) * height;
    m.map.editorTiles = readU16s(chk, "TILE");
    m.map.tiles = readU16s(chk, "MTXM");
    if (m.map.editorTiles.size() < tileCount) m.map.editorTiles.resize(tileCount, 0);
    if (m.map.tiles.size() < tileCount) m.map.tiles.resize(tileCount, 0);
    m.map.editorTiles.resize(tileCount), m.map.tiles.resize(tileCount);
    m.cache = std::make_unique<::Chk::IsomCache>(m.map.tileset, width, height, *m.tables);
    return true;
}

bool IsomCore::paint(int x, int y, uint16_t terrain, int extent) {
    Impl& m = *impl_;
    if (!m.cache || x < 0 || y < 0 || extent < 1) return false;
    const size_t type = m.terrainType(terrain);
    if (!type) return false;
    const ::Chk::IsomDiamond d{size_t(x), size_t(y)};
    if (!m.map.placeIsomTerrain(d, type, size_t(extent), *m.cache)) return false;
    m.updateTiles();  // (the next paint starts a new changed area)
    return true;
}

bool IsomCore::paintDiamonds(const std::vector<std::pair<int, int>>& diamonds, uint16_t terrain) {
    Impl& m = *impl_;
    if (!m.cache || !m.terrainType(terrain)) return false;
    const uint16_t isomValue = terrain;
    if (size_t(isomValue) >= m.cache->isomLinks.size() || m.cache->isomLinks[isomValue].terrainType == 0) return false;
    // (placeIsomTerrain's loop, for any set: set each, then update the diamonds around them that are not set)
    m.cache->resetChangedArea();
    std::vector<::Chk::IsomDiamond> set;
    for (auto [x, y] : diamonds) {
        const ::Chk::IsomDiamond d{size_t(x), size_t(y)};
        if (x < 0 || y < 0 || !d.isValid() || !inBounds(m.map, d)) continue;
        (m.map.*reach(SetDiamondTag{}))(d, isomValue, true, *m.cache);
        set.push_back(d);
    }
    if (set.empty()) return false;
    std::deque<::Chk::IsomDiamond> toUpdate;
    for (const auto& d : set)
        for (auto i : ::Chk::IsomDiamond::neighbors) {
            const ::Chk::IsomDiamond n = d.getNeighbor(i);
            if (needsUpdate(m.map, n)) toUpdate.push_back(n);
        }
    (m.map.*reach(RadialUpdateTag{}))(true, toUpdate, *m.cache);
    m.updateTiles();  // (the next paint starts a new changed area)
    return true;
}

void IsomCore::updateTiles() {
    Impl& m = *impl_;
    if (m.cache) m.updateTiles();
}

int IsomCore::untileable() const { return impl_->untileable; }

bool IsomCore::fill(uint16_t fill) {
    Impl& m = *impl_;
    if (!m.cache || !m.terrainType(fill)) return false;
    // (IsomTerrain's newMap)
    const uint16_t value = uint16_t(fill << 4 | ::Chk::IsomRect::EditorFlag::Modified);
    m.map.isomRects.assign(m.map.isomRects.size(), ::Chk::IsomRect{value, value, value, value});
    m.cache->setAllChanged();
    m.updateTiles();
    return true;
}

bool IsomCore::resize(uint16_t width, uint16_t height, int xOffset, int yOffset, uint16_t fill) {
    Impl& m = *impl_;
    if (!m.cache || !width || !height || !m.terrainType(fill)) return false;
    const ScMap& src = m.map;
    ScMap dest{};
    dest.tileset = src.tileset, dest.tileWidth = width, dest.tileHeight = height;
    auto cache = std::make_unique<::Chk::IsomCache>(src.tileset, width, height, *m.tables);
    // (IsomTerrain's resizeMap: the tiles cropped or padded at the top left first; the moved ones are copied last)
    auto crop = [&](const std::vector<uint16_t>& from) {
        std::vector<uint16_t> to(size_t(width) * height, 0);
        for (size_t y = 0; y < std::min<size_t>(height, src.tileHeight); ++y)
            for (size_t x = 0; x < std::min<size_t>(width, src.tileWidth); ++x) to[y * width + x] = from[y * src.tileWidth + x];
        return to;
    };
    dest.editorTiles = crop(src.editorTiles), dest.tiles = crop(src.tiles);
    const uint16_t value = uint16_t(fill << 4 | ::Chk::IsomRect::EditorFlag::Modified);
    dest.isomRects.assign((size_t(width) / 2 + 1) * (size_t(height) + 1), ::Chk::IsomRect{value, value, value, value});
    dest.copyIsomFrom(src, xOffset, yOffset, false, *cache);
    dest.resizeIsom(xOffset, yOffset, src.tileWidth, src.tileHeight, false, *cache);
    ScMap old = std::move(m.map);
    m.map = std::move(dest);
    m.cache = std::move(cache);
    m.updateTiles();
    const Sc::BoundingBox kept{old.tileWidth, old.tileHeight, width, height, xOffset, yOffset};
    const size_t toX = xOffset < 0 ? 0 : size_t(xOffset), toY = yOffset < 0 ? 0 : size_t(yOffset);
    for (size_t y = 0; y < kept.bottom - kept.top; ++y)
        for (size_t x = 0; x < kept.right - kept.left; ++x) {
            const size_t from = (y + kept.top) * old.tileWidth + (x + kept.left), to = (y + toY) * width + (x + toX);
            m.map.editorTiles[to] = old.editorTiles[from], m.map.tiles[to] = old.tiles[from];
        }
    return true;
}

void IsomCore::store(Chk& chk) const {
    const Impl& m = *impl_;
    if (!m.cache) return;
    const uint16_t dim[2] = {m.map.tileWidth, m.map.tileHeight};
    chk.set("DIM ", bytesOf(dim, sizeof dim));
    std::vector<::Chk::IsomRect> isom = m.map.isomRects;
    for (auto& r : isom) r.clearEditorFlags();
    chk.set("ISOM", bytesOf(isom.data(), isom.size() * sizeof(::Chk::IsomRect)));
    chk.set("TILE", bytesOf(m.map.editorTiles.data(), m.map.editorTiles.size() * 2));
    chk.set("MTXM", bytesOf(m.map.tiles.data(), m.map.tiles.size() * 2));
}

std::vector<uint16_t> IsomCore::brushes() const {
    std::vector<uint16_t> out;
    if (impl_->tables)
        for (const auto& b : impl_->tables->brushes) out.push_back(b.isomValue);
    return out;
}

}  // namespace stardraft
