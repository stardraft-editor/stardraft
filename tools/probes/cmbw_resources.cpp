// The mod's resource containers, resource depots and workers (by units.dat flags), and how many of each a map has.
// usage: cmbw_resources <Cosmonarchy install> <map>
#include <cstdio>
#include <map>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_resources <install> <map>\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor e(data);
    if (!e.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::map<int, int> onMap;
    for (const Unit& u : e.units()) ++onMap[u.type];
    const UnitTypes& t = e.unitTypes();
    for (size_t i = 0; i < t.size(); ++i) {
        const UnitType& u = t[i];
        const char* kind = u.is(UnitFlag::ResourceContainer) ? "resource" : u.is(UnitFlag::ResourceDepot) ? "depot" : u.is(UnitFlag::Worker) ? "worker" : nullptr;
        if (!kind) continue;
        std::printf("%4zu %-9s %3dx%-3d group %02x on map %4d  %s\n", i, kind, int(u.placementWidth), int(u.placementHeight), unsigned(u.groupFlags),
                    onMap.count(int(i)) ? onMap[int(i)] : 0, u.name.c_str());
    }
    return 0;
}
