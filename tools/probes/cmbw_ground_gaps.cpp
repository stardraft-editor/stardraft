// Grounds a mod terrain could stand on but cannot: for each mod terrain, the other terrains with its ground's fill (the
// same tiles inside) that no terrain with its pieces stands on. Paints each such ground, then the terrain on it, and
// tells whether the terrain stays on that ground (twins) or the stroke puts its own ground back; prints the VF4 heights
// (0 low, 1 middle, 2 high: most of the tiles) of the ground, of the ground's ground and of the terrain.
// usage: cmbw_ground_gaps <Cosmonarchy install> [era]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

namespace {
constexpr int kSize = 128, kMid = kSize * 16;

uint32_t fillOf(const Editor& e, uint16_t t) {
    if (const Tileset::ModTerrain* m = e.tileset().modTerrain(t)) return m->fillKey ? m->fillKey : 0xFFFF0000u | m->standIn;
    return 0xFFFF0000u | t;
}

// The VF4 height most tiles of terrain t have near the middle (-1: none).
int heightOf(const Editor& e, uint16_t t) {
    const TerrainLayer& ter = e.terrain();
    const Tileset& ts = e.tileset();
    int n[3] = {};
    for (int ty = kSize / 2 - 30; ty < kSize / 2 + 30; ++ty)
        for (int tx = kSize / 2 - 30; tx < kSize / 2 + 30; ++tx) {
            if (e.terrainAt(tx * 32 + 16, ty * 32 + 16) != t) continue;
            const int m = ts.megatileFor(ter.tiles[size_t(ty) * ter.width + tx]);
            if (m < 0 || size_t(m) >= ts.minitileFlags.size()) continue;
            int k[3] = {};
            for (uint16_t f : ts.minitileFlags[size_t(m)])
                if (f & 1) ++k[std::min((f >> 1) & 3, 2)];
            ++n[k[2] > k[1] && k[2] > k[0] ? 2 : k[1] > k[0] ? 1 : 0];
        }
    if (!n[0] && !n[1] && !n[2]) return -1;
    return n[2] > n[1] && n[2] > n[0] ? 2 : n[1] > n[0] ? 1 : 0;
}

// (one set: the same pieces and fill; the brush takes the one of them that fits the ground: Editor::brushTerrain)
bool sameSet(const Editor& e, uint16_t a, uint16_t b) {
    const Tileset::ModTerrain *x = e.tileset().modTerrain(a), *y = e.tileset().modTerrain(b);
    return a == b || (x && y && !x->pieces.empty() && !y->pieces.empty() && x->pieces[0] == y->pieces[0] && x->fillKey == y->fillKey);
}

int count(const Editor& e, uint16_t t) {
    int c = 0;
    for (int y = kMid - 640; y < kMid + 640; y += 16)
        for (int x = kMid - 640; x < kMid + 640; x += 32) c += e.terrainAt(x, y) == t;
    return c;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: cmbw_ground_gaps <install> [era]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    for (uint16_t era = 0; era < 8; ++era) {
        if (argc > 2 && std::atoi(argv[2]) != era) continue;
        Editor e(data);
        if (!e.newMap(kSize, kSize, era, 1, err)) continue;
        const std::vector<Tileset::ModTerrain> mods = e.tileset().modTerrains;
        std::vector<uint16_t> all;
        for (uint16_t t = 1; t < 64; ++t)
            if (!e.terrainName(t).empty()) all.push_back(t);
        for (const auto& m : mods) all.push_back(m.id);
        std::printf("== %s\n", tilesetName(era));
        if (std::getenv("GAPS_PIECES")) {  // (instead: each terrain's game groups its set has no piece for, null tiles)
            for (const auto& s : mods)
                if (s.gapPieces) std::printf("  %-40s %zu of %zu\n", s.name.c_str(), s.gapPieces, s.pieces.size());
            continue;
        }
        for (const auto& s : mods) {
            if (s.pieces.empty() || s.variantOf) continue;
            const uint32_t key = fillOf(e, s.ground);
            for (uint16_t g : all) {
                if (g == s.ground || g == s.id || fillOf(e, g) != key) continue;
                bool above = false;  // (s stands on g already, a level or more above it: no gap)
                for (uint16_t a = s.ground; a && !above;) {
                    const Tileset::ModTerrain* m = e.tileset().modTerrain(a);
                    a = m ? m->ground : 0, above = a == g;
                }
                if (above) continue;
                bool has = false;  // (a hand-made row with s's pieces on g; a variant of s is tried)
                uint16_t variant = 0;
                for (const auto& o : mods)
                    if (o.ground == g && !o.pieces.empty() && o.pieces[0] == s.pieces[0]) {
                        if (o.variantOf == s.id) variant = o.id;
                        else has = true;
                    }
                if (has) continue;
                Editor p(data);
                if (!p.newMap(kSize, kSize, era, 1, err)) continue;
                p.paintTerrain(kMid, kMid, g, 24);
                if (sameSet(p, p.terrainAt(kMid, kMid), g)) g = p.terrainAt(kMid, kMid);  // (the brush took one of its set)
                if (!count(p, g)) {
                    std::printf("  %-34s on %-34s  (the ground does not paint)\n", s.name.c_str(), p.terrainName(g).c_str());
                    continue;
                }
                const Tileset::ModTerrain* gm = p.tileset().modTerrain(g);
                const uint16_t gg = gm ? gm->ground : 0;
                const int hg = heightOf(p, g), hgg = gg ? heightOf(p, gg) : -1;
                const int before = count(p, s.ground);
                const uint16_t chosen = p.brushTerrain(kMid, kMid, s.id);
                p.paintTerrain(kMid, kMid, s.id, 6);
                uint16_t painted = count(p, s.id) ? s.id : variant;  // (or another variant, one for a twin of the ground)
                if (sameSet(p, p.terrainAt(kMid, kMid), s.id)) painted = p.terrainAt(kMid, kMid);
                const bool stays = painted && count(p, painted) > 0 && count(p, s.ground) <= before && count(p, g) > 0;
                std::printf("  %-34s on %-34s  %s  ground %d, its ground %d, terrain %d%s\n", s.name.c_str(), p.terrainName(g).c_str(),
                            stays ? "stands" : "no    ", hg, hgg, heightOf(p, painted), variant ? "  (variant)" : "");
                if (!stays && std::getenv("GAPS_WHY")) std::printf("      brush took %s (%d), the variant %d, at the middle %s\n", p.terrainName(chosen).c_str(), chosen, variant, p.terrainName(p.terrainAt(kMid, kMid)).c_str());
            }
        }
    }
}
