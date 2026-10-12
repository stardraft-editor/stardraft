// Validate the unit API against every map under a directory:
//  1. UNIT sections re-encode byte-identically
//  2. makeUnit() defaults (valid property/state masks) vs what the map editor wrote
//  3. checkPlacement() on the maps' own units (Blizzard maps should be clean)
//
// usage: unit_test <install dir> <maps dir>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>

#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

namespace fs = std::filesystem;
using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: unit_test <install dir> <maps dir>\n");
        return 2;
    }
    std::string err;
    GameData data;
    UnitTypes types;
    if (!data.open(argv[1], err) || !types.load(data, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::map<uint16_t, Tileset> tilesets;

    int maps = 0, roundTripFail = 0;
    size_t units = 0;
    std::map<std::pair<uint16_t, std::string>, int> mismatch;  // (type, what) -> count
    std::map<uint16_t, std::map<uint32_t, int>> resources;     // type -> amount -> count
    std::map<std::pair<std::string, std::string>, int> issues; // (issue, type name) -> count
    std::map<std::string, int> issueTotals;

    for (const auto& entry : fs::recursive_directory_iterator(argv[2])) {
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".scm" && ext != ".scx") continue;
        Bytes raw;
        if (!readMapChk(entry.path(), raw, err)) continue;
        Chk chk = Chk::parse(raw);
        ++maps;

        // 1. round trip
        std::vector<Unit> list = readUnits(chk);
        Chk copy = chk;
        writeUnits(copy, list);
        Bytes orig;
        for (const auto& s : chk.sections)
            if (s.name == SectionName("UNIT") && s.data.size() % Unit::kSize == 0)
                orig.insert(orig.end(), s.data.begin(), s.data.end());
        if (copy.find("UNIT")->data != orig) ++roundTripFail;
        units += list.size();

        // 2. defaults
        for (const auto& u : list) {
            if (u.type >= types.size()) continue;
            Unit d = makeUnit(types[u.type], u.owner, u.x, u.y);
            if (d.validProps != u.validProps) ++mismatch[{u.type, "props " + std::to_string(u.validProps) + " vs ours " + std::to_string(d.validProps)}];
            if (d.validState != u.validState) ++mismatch[{u.type, "state " + std::to_string(u.validState) + " vs ours " + std::to_string(d.validState)}];
            if (types[u.type].is(UnitFlag::ResourceContainer)) ++resources[u.type][u.resources];
        }

        // 3. placement, Blizzard maps only (names start with "(n)" and not protected/odd)
        std::string name = entry.path().filename().string();
        bool blizzard = entry.path().string().find("ladder") == std::string::npos && name[0] == '(';
        if (!blizzard) continue;
        uint16_t era = readEra(chk);
        if (!tilesets.count(era) && !tilesets[era].load(data, era, err)) continue;
        TerrainLayer terrain = readTerrain(chk);
        PlacementContext ctx{terrain.width, terrain.height, &terrain, &tilesets[era], &types, &list};
        for (size_t i = 0; i < list.size(); ++i)
            for (PlacementIssue is : checkPlacement(ctx, list[i], int(i))) {
                ++issues[{placementIssueName(is), types[list[i].type].name}];
                ++issueTotals[placementIssueName(is)];
            }
    }

    std::printf("%d maps, %zu units, %d UNIT round-trip failures\n\n", maps, units, roundTripFail);
    std::printf("Default mask mismatches (type: what -> count), top 25:\n");
    std::vector<std::pair<int, std::string>> mm;
    for (const auto& [k, n] : mismatch) mm.push_back({n, types[k.first].name + " (" + std::to_string(k.first) + "): " + k.second});
    std::sort(mm.rbegin(), mm.rend());
    for (size_t i = 0; i < mm.size() && i < 25; ++i) std::printf("  %6d  %s\n", mm[i].first, mm[i].second.c_str());
    std::printf("\nResource amounts (most common):\n");
    for (const auto& [t, amounts] : resources) {
        auto best = std::max_element(amounts.begin(), amounts.end(), [](auto& a, auto& b) { return a.second < b.second; });
        std::printf("  %-20s %u (%d of %zu values)\n", types[t].name.c_str(), best->first, best->second, amounts.size());
    }
    std::printf("\nPlacement issues on Blizzard maps:\n");
    for (const auto& [k, n] : issueTotals) std::printf("  %-18s %d\n", k.c_str(), n);
    std::vector<std::pair<int, std::string>> top;
    for (const auto& [k, n] : issues) top.push_back({n, k.first + " / " + k.second});
    std::sort(top.rbegin(), top.rend());
    for (size_t i = 0; i < top.size() && i < 15; ++i) std::printf("  %6d  %s\n", top[i].first, top[i].second.c_str());

    // 4. placeUnit smoke test on Challenger (badlands): expected outcomes per placement.
    {
        Bytes raw;
        fs::path p = fs::path(argv[2]) / "(2)Challenger.scm";
        if (readMapChk(p, raw, err)) {
            Chk chk = Chk::parse(raw);
            std::vector<Unit> list = readUnits(chk);
            TerrainLayer terrain = readTerrain(chk);
            Tileset& ts = tilesets[0];
            if (ts.groups.empty()) ts.load(data, 0, err);
            PlacementContext ctx{terrain.width, terrain.height, &terrain, &ts, &types, &list};
            // Find a water tile and an open dirt area to test against.
            int waterX = -1, waterY = -1;
            for (int i = 0; i < int(terrain.tiles.size()) && waterX < 0; ++i) {
                const TileGroup* g = ts.groupFor(terrain.tiles[i]);
                if (g && g->type == 5) waterX = (i % terrain.width) * 32 + 16, waterY = (i / terrain.width) * 32 + 16;
            }
            struct Case { const char* what; uint16_t type; uint8_t owner; int x, y; };
            const Case cases[] = {
                {"marine near a start location", *types.findByName("Terran Marine"), 0, int(list[0].x) + 200, int(list[0].y)},
                {"command center on water", *types.findByName("Terran Command Center"), 0, waterX, waterY},
                {"marine on water", *types.findByName("Terran Marine"), 0, waterX, waterY},
                {"command center off the map", *types.findByName("Terran Command Center"), 0, 10, 10},
                {"nexus on top of a start location", *types.findByName("Protoss Nexus"), 1, int(list[0].x), int(list[0].y)},
            };
            size_t before = list.size();
            std::printf("\nplaceUnit on %s (%zu units):\n", p.filename().string().c_str(), before);
            for (const auto& c : cases) {
                PlaceResult r = placeUnit(list, ctx, c.type, c.owner, uint16_t(c.x), uint16_t(c.y));
                std::printf("  %-34s -> %s at %u,%u serial %u", c.what, r.placed ? "placed" : "refused", r.unit.x, r.unit.y, r.unit.serial);
                for (auto is : r.issues) std::printf(" [%s]", placementIssueName(is));
                std::printf("\n");
            }
            writeUnits(chk, list);
            std::printf("  wrote %zu units back; re-read %zu\n", list.size(), readUnits(chk).size());
        }
    }
    return roundTripFail ? 1 : 0;
}
