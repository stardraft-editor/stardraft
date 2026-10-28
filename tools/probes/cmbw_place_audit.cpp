// Can each mod terrain be painted on the terrains it borders, without other terrains coming in between? For each
// tileset with mod terrains, each mod terrain M and each base B (M's ground, and every terrain with the ground's name
// before " (": Higher Dirt (High Dirt) on High Dirt, High Dirt (Jungle), High Dirt (Sand Dunes)): a new map, a big patch
// of B, M painted small in its middle; then the corners near M must be M or B, and no diamond invalid.
// usage: cmbw_place_audit <Cosmonarchy install> [era] [terrain name filter] [x: list the ones that pass too]
// AUDIT_BASE=<name>: one more base to try; AUDIT_LIST=1: the tileset's terrains (id, ground, stand-in, fill);
// AUDIT_MAPS=<file of map paths>: the terrains that meet in a diamond in those maps (their ISOM), counted.
// AUDIT_PNG=<folder>: a picture of each pair painted (24 x 20 tiles around it).
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

static std::string baseName(const std::string& n) {
    const size_t p = n.find(" (");
    return p == std::string::npos ? n : n.substr(0, p);
}

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: cmbw_place_audit <install> [era] [filter]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    if (const char* list = std::getenv("AUDIT_MAPS")) {  // (the maps in the list file: the terrains that meet in a diamond, counted)
        std::map<std::pair<std::string, std::string>, int> meet;
        FILE* f = std::fopen(list, "r");
        char line[1024];
        while (f && std::fgets(line, sizeof line, f)) {
            std::string path = line;
            while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
            Editor e(data);
            if (!e.open(path, err) || (argc > 2 && e.era() != std::atoi(argv[2]))) continue;
            for (int y = 1; y < e.height(); ++y)
                for (int x = 1; x * 2 < e.width(); ++x) {
                    // the four corners of the diamond at (x, y): left, top, right, bottom
                    const uint16_t c[4] = {e.terrainAt(x * 64 - 32, y * 32), e.terrainAt(x * 64, y * 32 - 16), e.terrainAt(x * 64 + 32, y * 32),
                                           e.terrainAt(x * 64, y * 32 + 16)};
                    for (int i = 0; i < 4; ++i)
                        for (int j = i + 1; j < 4; ++j)
                            if (c[i] != c[j]) {
                                std::string a2 = e.terrainName(c[i]), b2 = e.terrainName(c[j]);
                                if (b2 < a2) std::swap(a2, b2);
                                ++meet[{a2, b2}];
                            }
                }
        }
        if (f) std::fclose(f);
        for (const auto& [k, n] : meet) std::printf("%-34s | %-34s %d\n", k.first.c_str(), k.second.c_str(), n);
        return 0;
    }
    const int onlyEra = argc > 2 ? std::atoi(argv[2]) : -1;
    const std::string filter = argc > 3 ? argv[3] : "";
    int fails = 0, runs = 0;
    for (uint16_t era = 0; era < 8; ++era) {
        if (onlyEra >= 0 && era != onlyEra) continue;
        Editor probe(data);
        const IsomTerrainTable* game = IsomTerrainTable::forEra(era);
        if (!game) continue;
        uint16_t fill = 0;
        for (uint16_t t = 1; t <= game->terrainCount() && !fill; ++t)
            if (game->isTerrain(t)) fill = t;
        if (!probe.newMap(64, 64, era, fill, err)) continue;
        const auto mods = probe.tileset().modTerrains;
        if (mods.empty()) continue;
        // every terrain of the tileset: id -> name
        std::map<uint16_t, std::string> names;
        for (uint16_t t = 1; t <= game->terrainCount(); ++t)
            if (game->isTerrain(t)) names[t] = probe.terrainName(t);
        for (const auto& m : mods) names[m.id] = m.name;
        if (std::getenv("AUDIT_LIST"))  // (the tileset's terrains: id, name, ground)
            for (const auto& [id, n] : names) {
                const Tileset::ModTerrain* o = probe.tileset().modTerrain(id);
                std::printf("era %d  %3d %-34s ground %-28s standIn %3d groundTemplate %3d fill %08x\n", era, id, n.c_str(),
                            o && names.count(o->ground) ? names[o->ground].c_str() : "-", o ? o->standIn : 0, o ? o->groundTemplate : 0,
                            o ? o->fillKey : 0);
            }
        for (const auto& m : mods) {
            if (!filter.empty() && m.name.find(filter) == std::string::npos) continue;
            if (m.variantOf) continue;  // (a variant: the brush takes it for its ground, from the terrain it is a variant of)
            const std::string want = baseName(names.count(m.ground) ? names[m.ground] : "");
            // (the bases that are the ground inside: its name, its stand-in and its fill; a game terrain: the mod terrains
            // that are it inside)
            const Tileset::ModTerrain* g = probe.tileset().modTerrain(m.ground);
            const uint16_t gStand = g ? g->standIn : m.ground;
            const uint32_t gFill = g ? g->fillKey : (0xFFFF0000u | m.ground);
            auto same = [&](uint16_t id) {
                if (id == m.ground) return true;
                const Tileset::ModTerrain* o = probe.tileset().modTerrain(id);
                if (!o) return !g ? false : (g->standIn == id && g->fillKey == (0xFFFF0000u | id));
                return o->standIn == gStand && o->fillKey == gFill && gFill != 0;
            };
            std::set<uint16_t> bases{m.ground};
            for (const auto& [id, n] : names)
                if (id != m.id && ((baseName(n) == want && same(id)) || (std::getenv("AUDIT_BASE") && n == std::getenv("AUDIT_BASE"))))
                    bases.insert(id);  // (AUDIT_BASE: one more base by its name, to try)
            for (uint16_t b : bases) {
                Editor e(data);
                if (!e.newMap(64, 64, era, fill, err)) continue;
                const int cx = 32 * 32, cy = 32 * 32;
                e.paintTerrain(cx, cy, b, 16);
                // the patch: the corners near the middle are B
                bool baseOk = true;
                for (int y = 32 - 4; y <= 32 + 4; ++y)
                    for (int x = 16 - 2; x <= 16 + 2; ++x)
                        if (((x * 2 + y) & 1) == 0 || true) {
                            const uint16_t t = e.terrainAt(x * 64, y * 32);
                            if (t != b) baseOk = false;
                        }
                if (!baseOk) {
                    std::printf("era %d  %-34s on %-30s  (the patch of the base did not take)\n", era, m.name.c_str(), names[b].c_str());
                    continue;
                }
                const int before = e.invalidTerrain();
                e.paintTerrain(cx, cy, m.id, 2);
                ++runs;
                std::map<std::string, int> others;
                bool hasM = false;
                for (int y = 32 - 8; y <= 32 + 8; ++y)
                    for (int x = 16 - 4; x <= 16 + 4; ++x) {
                        const uint16_t t = e.terrainAt(x * 64, y * 32);
                        if (t == m.id) hasM = true;
                        else if (t != b) ++others[names.count(t) ? names[t] : std::to_string(t)];
                    }
                const int invalid = e.invalidTerrain() - before;
                if (const char* dir = std::getenv("AUDIT_PNG")) {  // (a picture of the middle: 24 x 20 tiles)
                    TerrainLayer crop;
                    crop.width = 24, crop.height = 20;
                    for (int y = 0; y < crop.height; ++y)
                        for (int x = 0; x < crop.width; ++x) crop.tiles.push_back(e.terrain().tiles[size_t(22 + y) * e.terrain().width + size_t(20 + x)]);
                    std::string file = std::string(dir) + "/era" + std::to_string(era) + " " + m.name + " on " + names[b] + ".png";
                    for (char& ch : file) if (ch == '(' || ch == ')') ch = '_';
                    writePng(file.c_str(), renderTerrain(e.tileset(), crop));
                }
                if (!hasM || !others.empty() || invalid > 0) {
                    ++fails;
                    std::printf("era %d  %-34s on %-30s  FAIL%s invalid %d:", era, m.name.c_str(), names[b].c_str(), hasM ? "" : " (not painted)", invalid);
                    for (const auto& [n, k] : others) std::printf(" [%s %d]", n.c_str(), k);
                    std::printf("\n");
                } else if (argc > 4) {
                    std::printf("era %d  %-34s on %-30s  ok\n", era, m.name.c_str(), names[b].c_str());
                }
            }
        }
    }
    std::printf("%d of %d failed\n", fails, runs);
}
