// Render ISOM editing demos: a terrain swatch and random terrain blobs on a blank dirt map.
//
// usage: isom_demo <install dir> <out dir> [seed]

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <random>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/terrain.h"

using namespace stardraft;
namespace fs = std::filesystem;

static bool save(const Chk& chk, const Tileset& ts, const fs::path& base) {
    Bytes raw = chk.serialize();
    std::ofstream(base.string() + ".chk", std::ios::binary)
        .write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size()));
    return writePng((base.string() + ".png").c_str(), renderTerrain(ts, readTerrain(chk), 2));
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: isom_demo <install dir> <out dir> [seed]\n");
        return 2;
    }
    std::string err;
    GameData data;
    Tileset ts;
    if (!data.open(argv[1], err) || !ts.load(data, 0, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    fs::path out = argv[2];
    fs::create_directories(out);
    unsigned seed = argc > 3 ? unsigned(std::atoi(argv[3])) : 1;

    // 1. Swatch: one big diamond of each terrain 1..9 on dirt, left to right.
    {
        Chk chk;
        IsomGrid grid;
        if (!grid.create(chk, 256, 48, 0, 1, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        for (int t = 1; t <= 9; ++t) {
            int x = 7 + (t - 1) * 14, y = 24;
            if ((x + y) & 1) ++x;
            grid.paint(x, y, uint16_t(t), 5);
        }
        if (!grid.store(chk, err)) std::fprintf(stderr, "%s\n", err.c_str());
        std::printf("swatch: %d invalid diamonds\n", grid.invalidDiamonds());
        save(chk, ts, out / "swatch");
    }

    // 2. Random blobs: random walks with varying brush sizes, several terrains.
    std::mt19937 rng(seed);
    const int terrainsList[][3] = {{5, 0, 0}, {4, 5, 0}, {3, 5, 8}, {2, 4, 7}};
    for (int demo = 0; demo < 4; ++demo) {
        Chk chk;
        IsomGrid grid;
        grid.create(chk, 128, 128, 0, 1, err);
        for (int blob = 0; blob < 10; ++blob) {
            int t = terrainsList[demo][rng() % 3];
            if (!t) t = terrainsList[demo][0];
            int x = 4 + int(rng() % 56), y = 4 + int(rng() % 120);
            int steps = 5 + int(rng() % 25);
            for (int s = 0; s < steps; ++s) {
                int px = x, py = y;
                if ((px + py) & 1) ++px;
                grid.paint(px, py, uint16_t(t), 1 + int(rng() % 3));
                x = std::clamp(x + int(rng() % 5) - 2, 1, 63);
                y = std::clamp(y + int(rng() % 9) - 4, 1, 127);
            }
        }
        if (!grid.store(chk, err)) std::fprintf(stderr, "%s\n", err.c_str());
        std::printf("demo %d: %d invalid diamonds\n", demo, grid.invalidDiamonds());
        save(chk, ts, out / ("blobs" + std::to_string(demo)));
    }
    return 0;
}
