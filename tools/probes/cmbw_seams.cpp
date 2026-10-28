// How cleanly each mod terrain of a map's tileset joins: paints it as cmbw_terrain_sheet does, then measures the mean
// pixel difference across the edges between neighboring tiles the strokes changed; the same for its stand-in (a game
// terrain, painted the same way) as the yardstick. Prints "score stand-in-score name" for each; with --keep, the names of
// the terrains made from the maps whose score is within 1.25 x their stand-in's + 3 (one per line).
// usage: cmbw_seams <Cosmonarchy install> <map> [--keep out.txt]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

namespace {

double seamScore(const Tileset& ts, const TerrainLayer& before, const TerrainLayer& after) {
    auto pixel = [&](uint16_t value, int px, int py) -> const Rgb* {
        const int m = ts.megatileFor(value);
        if (m < 0 || size_t(m) >= ts.megatiles.size()) return nullptr;
        const uint32_t e = ts.megatiles[size_t(m)][size_t(py / 8 * 4 + px / 8)];
        if ((e >> 1) >= ts.minitiles.size()) return nullptr;
        return &ts.palette[ts.minitiles[e >> 1][size_t(py % 8 * 8 + ((e & 1) ? 7 - px % 8 : px % 8))]];
    };
    double sum = 0;
    long n = 0;
    const int w = after.width, h = after.height;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t k = size_t(y) * w + x;
            for (int d = 0; d < 2; ++d) {  // (right, down)
                const int nx = x + (d == 0), ny = y + (d == 1);
                if (nx >= w || ny >= h) continue;
                const size_t j = size_t(ny) * w + nx;
                if (after.tiles[k] == before.tiles[k] && after.tiles[j] == before.tiles[j]) continue;
                for (int i = 0; i < 32; ++i) {
                    const Rgb* p = d == 0 ? pixel(after.tiles[k], 31, i) : pixel(after.tiles[k], i, 31);
                    const Rgb* q = d == 0 ? pixel(after.tiles[j], 0, i) : pixel(after.tiles[j], i, 0);
                    if (!p || !q) continue;
                    sum += std::abs(int(p->r) - q->r) + std::abs(int(p->g) - q->g) + std::abs(int(p->b) - q->b), n += 3;
                }
            }
        }
    return n ? sum / n : 0;
}

double paintScore(const GameData& data, const char* map, uint16_t ground, uint16_t terrain) {
    std::string err;
    Editor e(data);
    if (!e.open(map, err)) return -1;
    const int cx = e.width() * 16, cy = e.height() * 16;
    e.paintBrush(cx, cy, ground, Brush{Brush::Diamond, 14.0f});
    const TerrainLayer before = readTerrain(e.chk(), "TILE");
    e.paintBrush(cx - 96, cy, terrain, Brush{Brush::Diamond, 4.0f});
    e.paintBrush(cx + 96, cy, terrain, Brush{Brush::Diamond, 4.0f});
    return seamScore(e.tileset(), before, readTerrain(e.chk(), "TILE"));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_seams <install> <map> [--keep out.txt]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor probe(data);
    if (!probe.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    FILE* keep = argc > 4 && std::string(argv[3]) == "--keep" ? std::fopen(argv[4], "ab") : nullptr;
    for (const Tileset::ModTerrain& m : probe.tileset().modTerrains) {
        const uint16_t groundGame = m.groundTemplate ? m.groundTemplate : m.ground;
        const double s = paintScore(data, argv[2], m.ground, m.id), base = paintScore(data, argv[2], groundGame, m.standIn);
        const bool ok = s <= base * 1.25 + 3;
        std::printf("%6.1f %6.1f %s %s%s\n", s, base, ok ? "ok  " : "SEAM", m.name.c_str(), m.fromMaps ? " (from the maps)" : "");
        if (keep && m.fromMaps && ok) std::fprintf(keep, "%u %s\n", unsigned(probe.era() & 7), m.name.c_str());
    }
    if (keep) std::fclose(keep);
    return 0;
}
