#include "stardraft/doodads.h"

#include <algorithm>
#include <map>

#include "byteio.h"
#include "stardraft/game_data.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace stardraft {

namespace {

// One tile of a terrain section as readTerrain() reads it: the last section of that name that holds the tile, else 0.
uint16_t terrainTileAt(const Chk& chk, SectionName name, size_t i) {
    for (auto it = chk.sections.rbegin(); it != chk.sections.rend(); ++it)
        if (it->name == name && i < it->data.size() / 2) return readU16(it->data, i * 2);
    return 0;
}

// The one section of that name, or nullptr when there is none or more than one.
Section* onlySection(Chk& chk, SectionName name) {
    Section* found = nullptr;
    for (auto& s : chk.sections)
        if (s.name == name) {
            if (found) return nullptr;
            found = &s;
        }
    return found;
}

}  // namespace

bool DoodadTypes::load(const GameData& data, const Tileset& ts, uint16_t era, std::string& error) {
    types_.clear();
    Bytes dd;
    data.readFile(std::string("TileSet\\") + tilesetName(era) + "\\dddata.bin", dd, error);
    error.clear();  // optional: without it doodads simply have no terrain requirements

    auto ground = std::make_shared<DoodadGroundInfo>();
    for (const TileGroup& tg : ts.groups) {
        uint64_t k = (uint64_t(tg.type) << 48) ^ (uint64_t(tg.groundHeight) << 40);
        for (int e = 0; e < 4; ++e) k ^= uint64_t(tg.edges[size_t(e)]) * (0x9E3779B97F4A7C15ull >> (e * 7)) + uint64_t(e);
        ground->piece.push_back(k);
    }
    // Doodad CV5 groups: index >= 1024, type 1. edges[0] = overlay sprite, edges[2] = group string,
    // extra[0] = doodad id, extra[1] = width, extra[2] = height. The first group of an id is row 0.
    std::map<uint16_t, size_t> index;
    for (size_t g = 1024; g < ts.groups.size(); ++g) {
        const TileGroup& tg = ts.groups[g];
        if (tg.type != 1) continue;
        uint16_t id = tg.extra[0];
        if (index.count(id)) continue;
        DoodadType t;
        t.id = id;
        t.firstGroup = uint16_t(g);
        t.overlaySprite = tg.edges[0];
        t.groupString = tg.edges[2];
        t.width = std::min<uint16_t>(tg.extra[1], 16);
        t.height = std::min<uint16_t>(tg.extra[2], 16);
        if (!t.width || !t.height) continue;
        t.required.assign(size_t(t.width) * t.height, 0);
        if (dd.size() >= (size_t(id) + 1) * 512)
            for (int r = 0; r < t.height; ++r)
                for (int c = 0; c < t.width; ++c) t.required[size_t(r) * t.width + c] = readU16(dd, id * 512 + (r * t.width + c) * 2);
        for (int r = 0; r < t.height && size_t(g) + r < ts.groups.size(); ++r)
            for (int c = 0; c < t.width; ++c) t.inPalette |= ts.groups[g + size_t(r)].megatiles[size_t(c)] != 0;
        t.inPalette = t.inPalette && t.groupString != 0;
        t.ground = ground;
        index[id] = types_.size();
        types_.push_back(std::move(t));
    }
    return true;
}

const DoodadType* DoodadTypes::find(uint16_t id) const {
    for (const auto& t : types_)
        if (t.id == id) return &t;
    return nullptr;
}

std::vector<Doodad> readDoodads(const Chk& chk) {
    std::vector<Doodad> out;
    const Section* s = chk.find("DD2 ");
    if (!s) return out;
    for (size_t o = 0; o + 8 <= s->data.size(); o += 8)
        out.push_back({readU16(s->data, o), readU16(s->data, o + 2), readU16(s->data, o + 4), s->data[o + 6], s->data[o + 7]});
    return out;
}

void writeDoodads(Chk& chk, const std::vector<Doodad>& doodads) {
    Bytes d;
    for (const auto& x : doodads) {
        appendU16(d, x.id);
        appendU16(d, x.x);
        appendU16(d, x.y);
        d.push_back(x.owner);
        d.push_back(x.disabled);
    }
    chk.set("DD2 ", std::move(d));
}

std::vector<MapSprite> readSprites(const Chk& chk) {
    std::vector<MapSprite> out;
    for (const auto& s : chk.sections) {
        if (s.name != SectionName("THG2") || s.data.size() % 10) continue;
        for (size_t o = 0; o + 10 <= s.data.size(); o += 10)
            out.push_back({readU16(s.data, o), readU16(s.data, o + 2), readU16(s.data, o + 4), s.data[o + 6], s.data[o + 7],
                           readU16(s.data, o + 8)});
    }
    return out;
}

void writeSprites(Chk& chk, const std::vector<MapSprite>& sprites) {
    Bytes d;
    for (const auto& s : sprites) {
        appendU16(d, s.id);
        appendU16(d, s.x);
        appendU16(d, s.y);
        d.push_back(s.owner);
        d.push_back(s.unused);
        appendU16(d, s.flags);
    }
    chk.set("THG2", std::move(d));
}

bool doodadFits(const Chk& chk, const DoodadType& type, int tileX, int tileY, bool checkTerrain, std::string* reason) {
    uint16_t w = 0, h = 0;
    if (!readDimensions(chk, w, h)) {
        if (reason) *reason = "missing DIM";
        return false;
    }
    if (tileX < 0 || tileY < 0 || tileX + type.width > w || tileY + type.height > h) {
        if (reason) *reason = "off the map";
        return false;
    }
    if (!checkTerrain) return true;
    // Compare against the editor's terrain (TILE, without doodads), falling back to MTXM.
    // Only the footprint tiles are read (a spray or a nearest-fit search checks many places).
    const SectionName under = chk.has("TILE") ? "TILE" : "MTXM";
    const DoodadGroundInfo* gi = type.ground.get();
    // The top row of a doodad taller than one row is not checked: it draws over the ground above the doodad (a cliff's
    // high ground), and the game's own maps put it over other tiles than dddata.bin names (an Ice cliff's top row on
    // plain high snow, where it names the cliff's edge: such cliffs fit nowhere else). With it, 95 % of the doodads on
    // Blizzard's maps fit, against 73 % when it is checked.
    for (int r = type.height > 1 ? 1 : 0; r < type.height; ++r)
        for (int c = 0; c < type.width; ++c) {
            const size_t cell = size_t(r) * type.width + c;
            const uint16_t need = type.required[cell];
            const uint16_t g = uint16_t(terrainTileAt(chk, under, size_t(tileY + r) * w + tileX + c) >> 4);
            // The required group, or another variant of the same piece (a cliff piece that stacks another way: the
            // terrain tools pick among them).
            const bool ok = !need || g == need || (gi && g < gi->piece.size() && need < gi->piece.size() && gi->piece[g] == gi->piece[need]);
            if (!ok) {
                if (reason) *reason = "terrain underneath does not fit this doodad";
                return false;
            }
        }
    return true;
}

bool placeDoodad(Chk& chk, const DoodadType& type, const Tileset& ts, int tileX, int tileY, uint8_t owner,
                 bool checkTerrain, std::string* reason) {
    if (!doodadFits(chk, type, tileX, tileY, checkTerrain, reason)) return false;
    uint16_t w = 0, h = 0;
    readDimensions(chk, w, h);
    // One MTXM of the map's size: change only the footprint tiles. Else read all of it and write it back as one section.
    Section* one = onlySection(chk, "MTXM");
    const bool inPlace = one && one->data.size() == size_t(w) * h * 2;
    TerrainLayer mtxm;
    if (!inPlace) mtxm = readTerrain(chk, "MTXM");
    for (int r = 0; r < type.height; ++r) {
        size_t g = size_t(type.firstGroup) + r;
        if (g >= ts.groups.size()) break;
        for (int c = 0; c < type.width; ++c)
            if (ts.groups[g].megatiles[c]) {
                const size_t k = size_t(tileY + r) * w + tileX + c;
                const uint16_t v = uint16_t(g << 4 | c);
                if (inPlace) one->data[k * 2] = uint8_t(v), one->data[k * 2 + 1] = uint8_t(v >> 8);
                else mtxm.tiles[k] = v;
            }
    }
    if (!inPlace) {
        Bytes packed;
        for (uint16_t v : mtxm.tiles) appendU16(packed, v);
        chk.set("MTXM", std::move(packed));
    }

    Doodad d;
    d.id = type.id;
    d.x = uint16_t(tileX * 32 + type.width * 16);
    d.y = uint16_t(tileY * 32 + type.height * 16);
    d.owner = owner;
    // One DD2 (THG2) of whole records: append the record. Else read all and write them back as one section.
    if (Section* dd2 = onlySection(chk, "DD2 "); dd2 && dd2->data.size() % 8 == 0) {
        appendU16(dd2->data, d.id);
        appendU16(dd2->data, d.x);
        appendU16(dd2->data, d.y);
        dd2->data.push_back(d.owner);
        dd2->data.push_back(d.disabled);
    } else {
        auto dds = readDoodads(chk);
        dds.push_back(d);
        writeDoodads(chk, dds);
    }
    if (type.overlaySprite) {
        const MapSprite sp{type.overlaySprite, d.x, d.y, owner, 0, SpriteFlag::DrawAsSprite};
        if (Section* thg2 = onlySection(chk, "THG2"); thg2 && thg2->data.size() % 10 == 0) {
            appendU16(thg2->data, sp.id);
            appendU16(thg2->data, sp.x);
            appendU16(thg2->data, sp.y);
            thg2->data.push_back(sp.owner);
            thg2->data.push_back(sp.unused);
            appendU16(thg2->data, sp.flags);
        } else {
            auto sprites = readSprites(chk);
            sprites.push_back(sp);
            writeSprites(chk, sprites);
        }
    }
    return true;
}

}  // namespace stardraft
