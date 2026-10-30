// The mod's tile groups that nothing uses yet: not a piece of any terrain row (the built-in rows, the repaired rules, the
// mod's file), not a group the maps assign to a terrain (Tileset::groupTerrains), not a map-learned piece. They come in
// runs of consecutive groups; a run of 14 to 44 groups with full rows of 16 subtiles is likely a border set (a terrain
// with no row yet), a run of odd groups doodads or fills. For each tileset: the runs with their size and how many
// subtiles each row has, and a sheet (PNG, 16 tiles a row, the run's first rows) to look at.
// With an .isom path, the runs that look like border sets become draft terrains in that file (loaded in the Custom
// isom terrain editor with Load…): the template is the tileset's first game border of the family the run's size says
// (14 patch, 24 blend, 36 wall, 44 cliff), the run's rows go to the template's rects in order (a generated set lays
// them out in the game's order), each row's subtile pairs as variants, the rects past the run NULL, the name
// "Set <first>-<last> (<ground>)"; all drafts, for a person to check, rename and complete.
// usage: cmbw_unused_groups <Cosmonarchy install> <out folder> [tileset | -] [out .isom]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/png_writer.h"
#include "stardraft/previews.h"
#include "stardraft/sprites.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_unused_groups <install> <out folder> [tileset]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const fs::path out = argv[2];
    fs::create_directories(out);
    const int only = argc > 3 && std::string(argv[3]) != "-" ? std::atoi(argv[3]) : -1;
    const std::string rulesPath = argc > 4 ? argv[4] : "";
    namespace tr = stardraft::terrainrules;
    tr::Rules candidates;
    int made = 0;
    for (int era = 0; era < 8; ++era) {
        if (only >= 0 && era != only) continue;
        Tileset ts;
        if (!ts.load(data, uint16_t(era), err)) continue;
        std::set<uint16_t> used;
        for (const Tileset::ModTerrain& m : ts.modTerrains)
            for (const auto& p : m.pieces) used.insert(uint16_t(p[1] >> 4)), used.insert(uint16_t(p[2] >> 4));
        for (const auto& gt : ts.groupTerrains) used.insert(gt[0]);
        for (const auto& gp : ts.groupPieces) used.insert(uint16_t(gp[0] >> 4)), used.insert(uint16_t(gp[1] >> 4));
        const size_t firstMod = 1024;  // (the game's CV5 has 1024 groups at most; the mod's come after)
        auto subtiles = [&](size_t g) {
            int n = 0;
            for (uint16_t k = 0; k < 16; ++k) n += ts.megatileFor(uint16_t(g << 4 | k)) > 0 ? 1 : 0;
            return n;
        };
        struct Run {
            size_t first, last;
            int tiles, full;  // (subtiles in all; rows with 16)
        };
        std::vector<Run> runs;
        for (size_t g = firstMod; g < ts.groups.size(); ++g) {
            if (used.count(uint16_t(g)) || subtiles(g) == 0) continue;
            if (!runs.empty() && runs.back().last + 1 == g) runs.back().last = g, runs.back().tiles += subtiles(g), runs.back().full += subtiles(g) == 16;
            else runs.push_back({g, g, subtiles(g), subtiles(g) == 16});
        }
        size_t unusedGroups = 0;
        for (const Run& r : runs) unusedGroups += r.last - r.first + 1;
        std::printf("== tileset %d: %zu groups, %zu used by rows and maps, %zu unused in %zu runs\n", era, ts.groups.size(), used.size(), unusedGroups, runs.size());
        std::vector<const Run*> big;
        for (const Run& r : runs) {
            const size_t n = r.last - r.first + 1;
            if (n >= 4) big.push_back(&r);
            std::printf("  groups %4zu-%4zu  %3zu rows  %4d tiles  %3d full rows%s\n", r.first, r.last, n, r.tiles, r.full,
                        n >= 14 && n <= 48 && r.full * 2 >= int(n) ? "  <- border set?" : n >= 4 && r.full * 2 >= int(n) ? "  <- tiles" : "");
        }
        // The draft terrains: a run that looks like a border set, on the tileset's first game border of its family.
        if (!rulesPath.empty()) {
            const IsomTerrainTable* table = IsomTerrainTable::forEra(ts.era);
            tr::TilesetRules tsr = tr::rulesOf(ts);
            tsr.terrains.clear();  // (the file holds the new ones only: the editor adds them to the rules in use)
            std::set<uint16_t> ids;
            for (const Tileset::ModTerrain& m : ts.modTerrains) ids.insert(m.id);
            auto templateOfFamily = [&](tr::Family f) -> std::pair<uint16_t, uint16_t> {
                if (!table) return {0, 0};
                for (const auto& pr : table->pairs())
                    if (table->isTerrain(pr.a) && table->isTerrain(pr.b) && tr::familyOf(tr::templateRects(ts, pr.a, pr.b), ts) == f) return {pr.a, pr.b};
                return {0, 0};
            };
            for (const Run& r : runs) {
                const size_t n = r.last - r.first + 1;
                if (n < 14 || n > 48 || r.full * 2 < int(n)) continue;
                const tr::Family f = n <= 16 ? tr::Family::Patch : n <= 26 ? tr::Family::Blend : n <= 40 ? tr::Family::Wall : tr::Family::Cliff;
                const auto [a, b] = templateOfFamily(f);
                if (!a) continue;
                const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, a, b);
                uint16_t id = Tileset::kModTerrainIds;
                while (ids.count(id) && id < Tileset::kModVariantIds) ++id;
                if (id >= Tileset::kModVariantIds) break;
                ids.insert(id);
                tr::Terrain x;
                x.t.id = id, x.t.standIn = b, x.t.ground = a, x.t.groundTemplate = 0, x.t.fill = false, x.t.fillKey = 0, x.t.draft = true;
                x.t.name = "Set " + std::to_string(r.first) + "-" + std::to_string(r.last) + " (" + table->terrainName(a) + ")";
                for (size_t i = 0; i < rects.size() && i < n; ++i) {
                    const size_t g = r.first + i;
                    for (uint16_t k = 0; k + 1 < 16; k += 2)
                        if (ts.megatileFor(uint16_t(g << 4 | k)) > 0 && ts.megatileFor(uint16_t(g << 4 | (k + 1))) > 0) x.t.pieces.push_back({rects[i].group, uint16_t(g << 4 | k), uint16_t(g << 4 | (k + 1))});
                }
                x.t.setPieces = x.t.pieces.size(), x.t.gapPieces = 0;
                tr::normalize(ts, x.t);
                std::printf("  draft %s: %s on %s (%s, %zu rects, %zu rows of the run used)\n", x.t.name.c_str(), table->terrainName(b), table->terrainName(a), tr::familyName(f),
                            rects.size(), std::min(n, rects.size()));
                tsr.terrains.push_back(std::move(x));
                ++made;
            }
            if (!tsr.terrains.empty()) candidates.tilesets.push_back(std::move(tsr));
        }
        // The sheet: each run of 4 or more, its first 6 rows (16 tiles each), one after the other, a dark line between.
        if (big.empty()) continue;
        std::vector<uint16_t> tiles;
        std::vector<int> breaks;
        for (const Run* r : big) {
            for (size_t g = r->first; g <= std::min(r->last, r->first + 5); ++g)
                for (uint16_t k = 0; k < 16; ++k) tiles.push_back(ts.megatileFor(uint16_t(g << 4 | k)) > 0 ? uint16_t(g << 4 | k) : 0);
            for (uint16_t k = 0; k < 16; ++k) tiles.push_back(0);  // (a black row between runs)
            breaks.push_back(int(tiles.size() / 16));
        }
        const int rows = int(tiles.size() / 16);
        const Sprite s = mapTilesPicture(ts, tiles, 16, 0, 0, 16, rows);
        if (s.width > 0) {
            std::vector<uint8_t> rgba = s.rgba;
            for (int b : breaks)  // (the black rows: a gray line in the middle)
                if (b > 0)
                    for (int x = 0; x < s.width; ++x) {
                        const size_t i = (size_t(b - 1) * 32 + 16) * size_t(s.width) * 4 + size_t(x) * 4;
                        if (i + 3 < rgba.size()) rgba[i] = rgba[i + 1] = rgba[i + 2] = 90, rgba[i + 3] = 255;
                    }
            const fs::path png = out / ("unused_era" + std::to_string(era) + ".png");
            PngWriter w;
            if (!w.open(png, s.width, s.height) || !w.addRows(rgba.data(), s.height) || !w.finish()) std::printf("  (no sheet: could not write %s)\n", png.string().c_str());
            else std::printf("  sheet %s (%d runs of 4 or more, the first 6 rows of each)\n", png.string().c_str(), int(big.size()));
        }
    }
    if (!rulesPath.empty()) {
        if (!tr::save(rulesPath, candidates, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        std::printf("== %d draft terrains in %s\n", made, rulesPath.c_str());
    }
    return 0;
}
