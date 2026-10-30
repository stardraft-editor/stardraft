// The game's own borders of every tileset, as the terrain rules' kinds see them (terrain_rules.cpp kindOf: a wall has a
// rect stacked both under one and over one; a path's rects are one row; no rects: a field): each border's tile groups
// (the ones Blizzard's maps use with its two terrains, tileGroupTerrains, that are ISOM rects), how many of them have
// which CV5 stack links (none, a top, a bottom, both; the link ids are tags: a group's top meets a group whose bottom
// has the same id) and the kind. Then the mod's terrains of the tileset with their kinds, their set's groups and gaps.
// usage: cmbw_std_kinds <Cosmonarchy install> [tileset [border name part: each group's links]]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: cmbw_std_kinds <install> [tileset [border]]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const int only = argc > 2 ? std::atoi(argv[2]) : -1;
    for (int era = 0; era < 8; ++era) {
        if (only >= 0 && era != only) continue;
        Tileset ts;
        if (!ts.load(data, uint16_t(era), err)) {
            std::printf("tileset %d: %s\n", era, err.c_str());
            continue;
        }
        const IsomTiles* tiles = IsomTiles::forTileset(ts);
        const IsomTerrainTable* table = IsomTerrainTable::forEra(ts.era);
        if (!tiles || !table) continue;
        const std::vector<std::array<uint16_t, 2>> terrains = tileGroupTerrains(ts);
        std::printf("== tileset %d (%zu groups)\n", era, ts.groups.size());
        auto summary = [&](const std::string& name, const std::vector<uint16_t>& gs, const char* kind, const std::string& more) {
            int none = 0, top = 0, bottom = 0, both = 0, rects = 0;
            std::set<unsigned> ids;
            for (uint16_t g : gs) {
                const uint16_t t = tiles->stackTop(g), b = tiles->stackBottom(g);
                rects += int(tiles->rectsOf(g).size());
                if (t) ids.insert(t);
                if (b) ids.insert(b);
                (t && b ? both : t ? top : b ? bottom : none)++;
            }
            std::string id;
            for (unsigned i : ids) id += (id.empty() ? "" : ",") + std::to_string(i);
            std::printf("  %-34s %-5s groups %3zu  none %2d top %2d bottom %2d both %2d  link ids {%s}  rects %3d%s\n", name.c_str(), kind, gs.size(), none, top,
                        bottom, both, id.c_str(), rects, more.c_str());
        };
        for (const auto& pr : table->pairs()) {
            if (!table->isTerrain(pr.a) || !table->isTerrain(pr.b)) continue;
            std::vector<uint16_t> gs;
            for (size_t g = 0; g + 1 < terrains.size(); ++g)
                if (terrains[g][1] && std::minmax(terrains[g][0], terrains[g][1]) == std::minmax(pr.a, pr.b) && !tiles->rectsOf(uint16_t(g)).empty())
                    gs.push_back(uint16_t(g));
            bool wall = false;
            for (uint16_t g : gs)
                if (tiles->stackTop(g) && tiles->stackBottom(g)) wall = true;  // (kindOf's test)
            const std::string name = std::string(table->terrainName(pr.b)) + " (" + table->terrainName(pr.a) + ")";
            summary(name, gs, gs.empty() ? "field" : wall ? "wall" : "path", "");
            if (argc > 3 && name.find(argv[3]) != std::string::npos)  // (each group: its links)
                for (uint16_t g : gs)
                    std::printf("      group %4u  top %u  bottom %u  rects %zu\n", unsigned(g), unsigned(tiles->stackTop(g)), unsigned(tiles->stackBottom(g)),
                                tiles->rectsOf(g).size());
        }
        // The mod's terrains: the template (stand-in on ground), its family, how many of its rects are done, the drafts.
        const terrainrules::TilesetRules rules = terrainrules::rulesOf(ts);
        for (const terrainrules::Terrain& x : rules.terrains) {
            if (x.t.variantOf) continue;
            const auto [a, b] = terrainrules::templateOf(ts, x.t);
            const auto rects = terrainrules::templateRects(ts, a, b);
            const auto [done, total] = terrainrules::completeness(ts, x.t);
            int own = 0, borrowed = 0, holes = 0, partial = 0;
            for (const auto& p : x.t.pieces) {
                const auto st = terrainrules::stateOf(p);
                (st == terrainrules::PieceState::Own ? own : st == terrainrules::PieceState::Borrowed ? borrowed : st == terrainrules::PieceState::Null ? holes : partial)++;
            }
            std::printf("  mod %-34s %-8s %2d of %2d  model %s (%s)  pieces own %d borrowed %d holes %d partial %d%s\n", x.t.name.c_str(),
                        terrainrules::familyName(terrainrules::familyOf(rects, ts)), done, total, table->terrainName(b), table->terrainName(a), own, borrowed,
                        holes, partial, x.t.draft ? "  draft" : "");
        }
        if (argc > 3)  // (the roles of the named border)
            for (const auto& pr : table->pairs()) {
                if (!table->isTerrain(pr.a) || !table->isTerrain(pr.b)) continue;
                const std::string name = std::string(table->terrainName(pr.b)) + " (" + table->terrainName(pr.a) + ")";
                if (name.find(argv[3]) == std::string::npos) continue;
                for (const auto& r : terrainrules::templateRects(ts, pr.a, pr.b))
                    std::printf("      group %4u  %-22s dir %2d fill %.1f row %d\n", unsigned(r.group), r.role.c_str(), r.dir, double(r.fill), r.row);
            }
    }
    return 0;
}
