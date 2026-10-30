// The built-in isometric terrain rules of every tileset as a terrain rules file (.sdterrain, mods/terrain_rules.h),
// then a check: the file read back is the same, and a tileset with the file applied paints with the same pieces.
// usage: cmbw_rules_export <Cosmonarchy install> <out .sdterrain>
#include <cstdio>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/tileset.h"

using namespace stardraft;

namespace {

bool same(const Tileset::ModTerrain& a, const Tileset::ModTerrain& b) {
    return a.id == b.id && a.name == b.name && a.standIn == b.standIn && a.ground == b.ground && a.groundTemplate == b.groundTemplate &&
           a.fill == b.fill && a.fillKey == b.fillKey && a.pieces == b.pieces && a.setPieces == b.setPieces && a.gapPieces == b.gapPieces &&
           a.variantOf == b.variantOf && a.fromMaps == b.fromMaps;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_rules_export <install> <out .sdterrain>\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    terrainrules::Rules rules;
    std::vector<Tileset> sets;
    size_t pieces = 0;
    for (int era = 0; era < 8; ++era) {
        Tileset t;
        if (!t.load(data, era, err)) continue;
        terrainrules::TilesetRules r = terrainrules::rulesOf(t);
        int drafts = 0;
        for (const auto& x : r.terrains) drafts += x.t.draft ? 1 : 0, pieces += x.t.pieces.size();
        std::printf("era %d: %zu terrains (%d drafts)\n", era, r.terrains.size(), drafts);
        rules.tilesets.push_back(std::move(r));
        sets.push_back(std::move(t));
    }
    if (!terrainrules::save(argv[2], rules, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const std::vector<uint8_t> bytes = terrainrules::write(rules);
    std::printf("%zu pieces, %zu bytes\n", pieces, bytes.size());
    terrainrules::Rules back;
    if (!terrainrules::load(argv[2], back, err)) return std::fprintf(stderr, "read back: %s\n", err.c_str()), 1;
    int bad = 0;
    for (size_t s = 0; s < sets.size(); ++s) {
        Tileset t = sets[s];
        for (auto& m : t.modTerrains) m.pieces.clear();  // (the file must give them all back)
        std::string why;
        if (!terrainrules::apply(t, back, &why)) {
            std::printf("era %d: not applied: %s (WRONG)\n", int(sets[s].era), why.c_str()), ++bad;
            continue;
        }
        for (size_t i = 0; i < t.modTerrains.size(); ++i)
            if (i >= sets[s].modTerrains.size() || !same(t.modTerrains[i], sets[s].modTerrains[i]))
                std::printf("era %d: terrain %zu differs (WRONG)\n", int(sets[s].era), i), ++bad;
    }
    std::printf(bad ? "round trip: %d differences (WRONG)\n" : "round trip: the same\n", bad);
    return bad ? 1 : 0;
}
