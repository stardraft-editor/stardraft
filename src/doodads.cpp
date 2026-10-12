#include "stardraft/doodads.h"

#include <algorithm>
#include <map>

#include "byteio.h"
#include "stardraft/game_data.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace stardraft {

bool DoodadTypes::load(const GameData& data, const Tileset& ts, uint16_t era, std::string& error) {
    types_.clear();
    Bytes dd;
    data.readFile(std::string("TileSet\\") + tilesetName(era) + "\\dddata.bin", dd, error);
    error.clear();  // optional: without it doodads simply have no terrain requirements

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
                for (int c = 0; c < t.width; ++c) t.required[size_t(r) * t.width + c] = readU16(dd, id * 512 + (r * 16 + c) * 2);
        for (int r = 0; r < t.height && size_t(g) + r < ts.groups.size(); ++r)
            for (int c = 0; c < t.width; ++c) t.inPalette |= ts.groups[g + size_t(r)].megatiles[size_t(c)] != 0;
        t.inPalette = t.inPalette && t.groupString != 0;
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
    TerrainLayer under = readTerrain(chk, chk.has("TILE") ? "TILE" : "MTXM");
    for (int r = 0; r < type.height; ++r)
        for (int c = 0; c < type.width; ++c) {
            uint16_t need = type.required[size_t(r) * type.width + c];
            if (need && uint16_t(under.tiles[size_t(tileY + r) * w + tileX + c] >> 4) != need) {
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
    TerrainLayer mtxm = readTerrain(chk, "MTXM");
    for (int r = 0; r < type.height; ++r) {
        size_t g = size_t(type.firstGroup) + r;
        if (g >= ts.groups.size()) break;
        for (int c = 0; c < type.width; ++c)
            if (ts.groups[g].megatiles[c]) mtxm.tiles[size_t(tileY + r) * w + tileX + c] = uint16_t(g << 4 | c);
    }
    Bytes packed;
    for (uint16_t v : mtxm.tiles) appendU16(packed, v);
    chk.set("MTXM", std::move(packed));

    Doodad d;
    d.id = type.id;
    d.x = uint16_t(tileX * 32 + type.width * 16);
    d.y = uint16_t(tileY * 32 + type.height * 16);
    d.owner = owner;
    auto dds = readDoodads(chk);
    dds.push_back(d);
    writeDoodads(chk, dds);
    if (type.overlaySprite) {
        auto sprites = readSprites(chk);
        sprites.push_back({type.overlaySprite, d.x, d.y, owner, 0, SpriteFlag::DrawAsSprite});
        writeSprites(chk, sprites);
    }
    return true;
}

}  // namespace stardraft
