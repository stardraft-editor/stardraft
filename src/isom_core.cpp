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
    return m.map.placeIsomTerrain(d, type, size_t(extent), *m.cache);
}

void IsomCore::updateTiles() {
    Impl& m = *impl_;
    if (m.cache) m.map.updateTilesFromIsom(*m.cache);
}

void IsomCore::store(Chk& chk) const {
    const Impl& m = *impl_;
    if (!m.cache) return;
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
