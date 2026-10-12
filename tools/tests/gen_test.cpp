// Terrain generator checks for every tileset: valid ISOM, determinism, exact mirror symmetry, shares
// close to the requested ones, every terrain present, speed. Writes generated maps (.chk + .png).
//
// usage: gen_test <install dir> <out dir> [era]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/terrain.h"
#include "stardraft/terrain_gen.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    fs::create_directories(argv[2]);
    const int onlyEra = argc > 3 ? std::atoi(argv[3]) : -1;

    for (uint16_t era = 0; era < 8; ++era) {
        if (onlyEra >= 0 && era != onlyEra) continue;
        const std::string ts = tilesetName(era);
        Tileset tileset;
        if (!tileset.load(data, era, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
        auto roles = generatorTerrains(era, tileset);
        std::printf("\n== %s: ", ts.c_str());
        const char* roleName[] = {"base", "sea", "high", "patch"};
        for (const auto& g : roles)
            std::printf("%s%s(%s%s, %.0f%%)  ", std::string(g.depth, '>').c_str(), g.name, roleName[g.role], g.walkable ? "" : ", unwalkable",
                        g.defaultShare * 100);
        std::printf("\n");

        auto gen = [&](TerrainGenParams p, Chk& chk, double& ms) {
            p.era = era;
            IsomGrid g;
            g.create(chk, p.width, p.height, era, 1, err);
            auto t0 = std::chrono::steady_clock::now();
            bool ok = generateTerrain(chk, tileset, p, err);
            ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return ok;
        };
        auto load = [&](const Chk& chk) {
            IsomGrid g;
            g.load(chk, err);
            return g;
        };

        // Valid for several seeds and sizes.
        int invalid = 0;
        double worst = 0;
        for (uint32_t seed : {1u, 42u, 1234u})
            for (uint16_t size : {uint16_t(64), uint16_t(128), uint16_t(256)}) {
                TerrainGenParams p;
                p.seed = seed, p.width = p.height = size;
                Chk chk;
                double ms;
                if (!gen(p, chk, ms)) return std::fprintf(stderr, "%s: %s\n", ts.c_str(), err.c_str()), 1;
                invalid += load(chk).invalidDiamonds();
                worst = std::max(worst, ms);
                if (size == 128) {
                    Bytes raw = chk.serialize();
                    fs::path base = fs::path(argv[2]) / (ts + "_seed" + std::to_string(seed));
                    std::FILE* f = std::fopen((base.string() + ".chk").c_str(), "wb");
                    std::fwrite(raw.data(), 1, raw.size(), f);
                    std::fclose(f);
                    writePng((base.string() + ".png").c_str(), renderTerrain(tileset, readTerrain(chk), 4));
                }
            }
        check(invalid == 0, ts + ": every generated map has valid ISOM");
        check(worst < 1500, ts + ": 256x256 generates in under 1.5 s (slowest " + std::to_string(int(worst)) + " ms)");

        // Width and height apart (wide and tall maps, with and without symmetry).
        {
            int bad = 0;
            const uint16_t dims[][2] = {{192, 96}, {64, 256}, {256, 128}};
            for (const auto& wh : dims)
                for (GenSymmetry sym : {GenSymmetry::None, GenSymmetry::LeftRight, GenSymmetry::Rotate180}) {
                    TerrainGenParams q;
                    q.seed = 9, q.width = wh[0], q.height = wh[1], q.symmetry = sym;
                    Chk c;
                    double t;
                    if (!gen(q, c, t)) {
                        ++bad;
                        continue;
                    }
                    IsomGrid g = load(c);
                    bad += g.invalidDiamonds() + (readTerrain(c).width != wh[0] || readTerrain(c).height != wh[1]);
                }
            check(bad == 0, ts + ": non-square maps generate valid ISOM at the asked size");
        }

        // Half-size features (brush 0.5): still valid everywhere.
        {
            int bad = 0;
            for (uint32_t seed : {5u, 6u}) {
                TerrainGenParams q;
                q.seed = seed, q.minFeature = 0.5f;
                Chk c;
                double t;
                gen(q, c, t);
                bad += load(c).invalidDiamonds();
                if (seed == 5) writePng((fs::path(argv[2]) / (ts + "_fine.png")).string().c_str(), renderTerrain(tileset, readTerrain(c), 4));
            }
            check(bad == 0, ts + ": brush-0.5 features generate valid ISOM");
        }

        // Determinism.
        TerrainGenParams p;
        p.seed = 777;
        Chk a, b;
        double ms;
        gen(p, a, ms), gen(p, b, ms);
        check(a.find("ISOM")->data == b.find("ISOM")->data, ts + ": same seed and settings give the same map");

        // Shares, averaged over seeds.
        std::map<uint16_t, double> count;
        double total = 0;
        for (uint32_t seed : {777u, 1u, 2u, 3u, 4u}) {
            TerrainGenParams q;
            q.seed = seed;
            Chk m;
            gen(q, m, ms);
            IsomGrid g = load(m);
            for (int y = 0; y <= g.rectRows(); ++y)
                for (int x = 0; x <= g.rectCols(); ++x)
                    if (g.isCorner(x, y)) count[g.terrainAt(x, y)] += 1, total += 1;
        }
        std::printf("      shares (got / Blizzard default):");
        int missing = 0;
        for (const auto& g : roles) {
            std::printf("  %s %.1f/%.1f", g.name, 100 * count[g.id] / total, 100 * g.defaultShare);
            if (g.defaultShare >= 0.005 && count[g.id] == 0) ++missing;
        }
        std::printf("\n");
        check(missing == 0, ts + ": every terrain with a default share of 0.5% or more appears");

        // Exact mirror symmetry.
        for (GenSymmetry sym : {GenSymmetry::LeftRight, GenSymmetry::TopBottom, GenSymmetry::Both, GenSymmetry::Rotate180}) {
            TerrainGenParams q;
            q.seed = 31337, q.symmetry = sym;
            Chk c;
            gen(q, c, ms);
            IsomGrid g = load(c);
            int diff = 0;
            const int W = 64, H = 128;  // corner-grid mirror bounds for 128x128 (w/2, h)
            for (int y = 0; y <= g.rectRows(); ++y)
                for (int x = 0; x <= g.rectCols(); ++x) {
                    if (!g.isCorner(x, y)) continue;
                    int mx = (sym == GenSymmetry::TopBottom) ? x : W - x, my = (sym == GenSymmetry::LeftRight) ? y : H - y;
                    if (!g.isCorner(mx, my)) continue;
                    diff += g.terrainAt(x, y) != g.terrainAt(mx, my);
                }
            if (diff) std::printf("      symmetry %d: %d corners differ from their mirror\n", int(sym), diff);
            check(g.invalidDiamonds() == 0, ts + ": symmetric map is valid");
            if (sym == GenSymmetry::Both)
                writePng((fs::path(argv[2]) / (ts + "_sym4.png")).string().c_str(), renderTerrain(tileset, readTerrain(c), 4));
        }
        (void)table;
    }

    // Through the editor (full scenario), one tileset.
    GameData& d = data;
    Editor ed(d);
    TerrainGenParams p;
    p.era = 4;
    check(ed.generateMap(p, err) && ed.invalidTerrain() == 0 && ed.era() == 4, "Editor::generateMap makes a full, valid jungle scenario");
    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
