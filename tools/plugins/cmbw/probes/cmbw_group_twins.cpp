// The mod's tile groups past the game's that the terrain engine can take for a game rect: groups of the mod's tileset
// whose CV5 type and edges are the same as a group of the game's tileset (the engine finds groups by those). Prints
// each with its game twin; a sheet of the pairs (twin, mod group) when an out folder is given.
// usage: cmbw_group_twins <Cosmonarchy install> <StarDat.mpq> <tileset name> <era> [out folder]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

#include <StormLib.h>

#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 5) return std::fprintf(stderr, "usage: cmbw_group_twins <install> <StarDat.mpq> <tileset name> <era> [out folder]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const int era = std::atoi(argv[4]);
    Tileset mod, game;
    if (!mod.load(data, era, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    HANDLE mpq = nullptr, f = nullptr;
    if (!SFileOpenArchive(std::filesystem::path(argv[2]).wstring().c_str(), 0, MPQ_OPEN_READ_ONLY, &mpq)) return std::fprintf(stderr, "no mpq\n"), 1;
    const std::string name = std::string("tileset\\") + argv[3] + ".cv5";
    if (!SFileOpenFileEx(mpq, name.c_str(), 0, &f)) return std::fprintf(stderr, "no %s\n", name.c_str()), 1;
    std::vector<uint8_t> cv5(SFileGetFileSize(f, nullptr));
    DWORD got = 0;
    SFileReadFile(f, cv5.data(), DWORD(cv5.size()), &got, nullptr);
    SFileCloseFile(f), SFileCloseArchive(mpq);
    game.groups.resize(cv5.size() / 52);
    for (size_t i = 0; i < game.groups.size(); ++i) {
        const uint8_t* p = &cv5[i * 52];
        auto u16 = [&](int o) { return uint16_t(p[o] | p[o + 1] << 8); };
        TileGroup& t = game.groups[i];
        t.type = u16(0), t.buildability = p[2], t.groundHeight = p[3];
        for (int k = 0; k < 4; ++k) t.edges[size_t(k)] = u16(4 + 2 * k), t.extra[size_t(k)] = u16(12 + 2 * k);
        for (int k = 0; k < 16; ++k) t.megatiles[size_t(k)] = u16(20 + 2 * k);
    }
    std::printf("groups: game %zu, mod %zu\n", game.groups.size(), mod.groups.size());
    int same = 0, differ = 0;
    for (size_t g = 0; g < game.groups.size() && g < mod.groups.size(); ++g)
        (std::memcmp(&game.groups[g], &mod.groups[g], sizeof(TileGroup)) == 0 ? same : differ)++;
    std::printf("the game's groups in the mod: %d the same, %d changed\n", same, differ);
    int dType = 0, dEdges = 0, dExtra = 0, dMega = 0, dFlags = 0;
    for (size_t g = 0; g < game.groups.size() && g < mod.groups.size(); ++g) {
        const TileGroup &a = game.groups[g], &b = mod.groups[g];
        dType += a.type != b.type, dEdges += a.edges != b.edges, dExtra += a.extra != b.extra, dMega += a.megatiles != b.megatiles;
        dFlags += a.buildability != b.buildability || a.groundHeight != b.groundHeight;
        if (a.type != b.type || a.edges != b.edges || a.extra != b.extra)
            std::printf("game group %zu: type %u->%u edges %u,%u,%u,%u -> %u,%u,%u,%u extra %u,%u,%u,%u -> %u,%u,%u,%u\n", g, a.type, b.type,
                        a.edges[0], a.edges[1], a.edges[2], a.edges[3], b.edges[0], b.edges[1], b.edges[2], b.edges[3], a.extra[0], a.extra[1],
                        a.extra[2], a.extra[3], b.extra[0], b.extra[1], b.extra[2], b.extra[3]);
    }
    std::printf("changed: type %d, edges %d, extra %d, megatiles %d, flags %d\n", dType, dEdges, dExtra, dMega, dFlags);
    auto key = [](const TileGroup& t) {
        std::string k(reinterpret_cast<const char*>(&t.type), 2);
        k.append(reinterpret_cast<const char*>(t.edges.data()), t.edges.size() * 2);
        k.append(reinterpret_cast<const char*>(t.extra.data()), t.extra.size() * 2);
        return k;
    };
    std::map<std::string, size_t> gameKeys;
    for (size_t g = 0; g < game.groups.size(); ++g)
        if (game.groups[g].type) gameKeys.try_emplace(key(game.groups[g]), g);
    int twins = 0;
    std::vector<std::pair<size_t, size_t>> pairs;
    for (size_t g = game.groups.size(); g < mod.groups.size(); ++g) {
        if (!mod.groups[g].type) continue;
        auto it = gameKeys.find(key(mod.groups[g]));
        if (it == gameKeys.end()) continue;
        ++twins;
        pairs.push_back({it->second, g});
        std::printf("mod group %zu: type %u, twin of game group %zu\n", g, unsigned(mod.groups[g].type), it->second);
    }
    std::printf("mod groups with a game twin: %d\n", twins);
    if (argc > 5) {  // (a sheet: rows of twin | mod group, 16 tiles each)
        TerrainLayer sheet;
        sheet.width = 33, sheet.height = uint16_t(std::min<size_t>(pairs.size(), 200));
        for (size_t r = 0; r < sheet.height; ++r) {
            for (int s = 0; s < 16; ++s) sheet.tiles.push_back(uint16_t(pairs[r].first << 4 | s));
            sheet.tiles.push_back(0);
            for (int s = 0; s < 16; ++s) sheet.tiles.push_back(uint16_t(pairs[r].second << 4 | s));
        }
        writePng((std::filesystem::path(argv[5]) / "twins.png").string(), renderTerrain(mod, sheet));
    }
    return 0;
}
