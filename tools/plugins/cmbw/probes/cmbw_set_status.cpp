// Which of the mod's brush sets are terrains in the isometric list: for each tileset, each set (the brushes' name up to
// the last "_"; the doodad, swamp and tree sets left out: Custom terrain stamps), its brushes and tiles, and how many
// of its tile groups are pieces of a mod terrain ("done" when most are). For seeing what is left to do.
// usage: cmbw_set_status <Cosmonarchy install> [era] [maps folder]   (with the maps: how many of their tiles are each set's)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <filesystem>
#include <string>

#include "stardraft/chk.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain_stamp.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: cmbw_set_status <install> [era]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const int only = argc > 2 ? std::atoi(argv[2]) : -1;
    static const char* kNames[8] = {"Badlands", "Platform", "Installation", "Ash World", "Jungle", "Desert", "Ice", "Twilight"};
    int left = 0;
    std::map<int, std::map<uint16_t, long>> used;  // era -> group -> tiles on the maps
    if (argc > 3) {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(argv[3], ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            const auto p = it->path();
            if (p.extension() != ".scx" && p.extension() != ".scm") continue;
            Bytes raw;
            std::string e2;
            if (!readMapFile(p, raw, e2)) continue;
            const Chk chk = Chk::parse(raw);
            const Section* era = chk.find("ERA ");
            if (!era || era->data.size() < 2) continue;
            const TerrainLayer t = readTerrain(chk);
            auto& u = used[(era->data[0] | era->data[1] << 8) & 7];
            for (uint16_t v : t.tiles) ++u[uint16_t(v >> 4)];
        }
    }
    for (int e = 0; e < 8; ++e) {
        if (only >= 0 && e != only) continue;
        Tileset t;
        if (!t.load(data, uint16_t(e), err)) continue;
        std::set<uint16_t> pieceGroups;
        for (const auto& m : t.modTerrains)
            for (const auto& p : m.pieces) pieceGroups.insert(uint16_t(p[1] >> 4)), pieceGroups.insert(uint16_t(p[2] >> 4));
        struct Set {
            int brushes = 0, big = 0, single = 0;  // (big: bigger than an edge's block, 2 x 3: a doodad brush)
            std::set<uint16_t> groups;
        };
        std::map<std::string, Set> sets;
        for (const TerrainStamp& s : cmbw::brushStamps(argv[1], uint16_t(e))) {
            std::string lower = s.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("doodad") != std::string::npos || lower.find("_trees") != std::string::npos || lower.find("swamp") != std::string::npos) continue;
            const std::string set = s.name.substr(0, s.name.rfind('_'));
            Set& st = sets[set];
            ++st.brushes, st.big += s.width > 2 || s.height > 3, st.single += s.width == 2 && s.height == 1;
            if (s.width > 2 || s.height > 3) continue;  // (a ramp's or cave's groups: a Custom terrain stamp)
            for (size_t i = 0; i < s.tiles.size() && i < s.mask.size(); ++i)
                if (s.mask[i]) st.groups.insert(uint16_t(s.tiles[i] >> 4));
        }
        std::printf("== %s (%d): %zu mod terrains\n", kNames[e], e, t.modTerrains.size());
        for (const auto& [name, st] : sets) {
            size_t in = 0;
            long tiles = 0;
            static const unsigned kStock[8] = {1665, 1513, 1265, 1262, 1578, 1520, 1415, 1494};  // (the 1.16.1 CV5s)
            size_t game = 0;  // (the game's own groups: a set of them is the game's border, in the list already)
            for (uint16_t g : st.groups) in += pieceGroups.count(g), tiles += used[e][g], game += g < kStock[e];
            // (sets looked at and left out, the reason in docs/dev/cmbw-update.md: "out")
            static const std::set<std::string> kLeftOut = {
                                                           "Cosmonarchy_Jungle-Rjungle-cliff", "Cosmonarchy_Jungle-Rjungle-high-cliff",
                                                           "Cosmonarchy_Urban-Halfsphalt-2", "Cosmonarchy_Flagstones-dirt3",
                                                           "Cosmonarchy_RaisedJungle-Edge-Dirt-2", "Cosmonarchy_Mud-Dirt2-Edge",
                                                           "Cosmonarchy_Organ-Cliff-Flesh-1"};
            const bool out = kLeftOut.count(name) != 0;
            const bool done = in * 2 >= st.groups.size() || game * 2 > st.groups.size() || out, doodads = st.big * 2 > st.brushes || st.single == st.brushes;
            left += !done && !doodads;
            std::printf("  %-4s %-48s %3d brushes, %3zu groups, %3zu in terrains, %6ld on the maps\n", out ? "out " : done ? "done" : doodads ? "dood" : "-", name.c_str(), st.brushes, st.groups.size(), in, tiles);
            if (std::getenv("STATUS_GROUPS")) {  // (its groups, those of the game's own CV5 marked *)
                std::printf("      groups:");
                for (uint16_t g : st.groups) std::printf(" %u%s", unsigned(g), g < kStock[e] ? "*" : "");
                std::printf("\n");
            }
        }
    }
    std::printf("sets left: %d\n", left);
    return 0;
}
