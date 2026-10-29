// The unit palette's sections of the mod, in palette order: each section, then its units (id, tier, name, subtitle).
// usage: cmbw_unit_sections <Cosmonarchy install>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/units.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: cmbw_unit_sections <Cosmonarchy install>\n"), 2;
    std::string err;
    GameData data;
    UnitTypes types;
    if (!cmbw::open(data, argv[1], err) || !types.load(data, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::vector<uint16_t> ids;
    for (uint16_t i = 0; i < types.size(); ++i)
        if (!types[i].name.empty()) ids.push_back(i);
    std::stable_sort(ids.begin(), ids.end(), [&](uint16_t a, uint16_t b) { return types[a].paletteOrder < types[b].paletteOrder; });
    std::string section;
    for (const uint16_t i : ids) {
        const UnitType& t = types[i];
        if (t.paletteSection != section) std::printf("%s\n", (section = t.paletteSection).c_str());
        std::printf("  %4u  t%d  %s%s\n", unsigned(i), t.paletteTier, t.name.c_str(), t.subtitle.empty() ? "" : (" (" + t.subtitle + ")").c_str());
    }
    return 0;
}
