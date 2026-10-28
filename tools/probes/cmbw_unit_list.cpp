// Each mod unit's palette section: id, name, subtitle, editor group, flags, section, order, its image and the image's
// iscript ID (tab-separated).
// usage: cmbw_unit_list <Cosmonarchy install> <map>
#include <cstdio>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/sprites.h"
#include "stardraft/units.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return 1;
    Editor ed(data);
    if (!ed.open(argv[2], err)) return 1;
    const UnitTypes& t = ed.unitTypes();
    const UnitSprites& sp = ed.unitSprites();
    for (size_t i = 0; i < t.size(); ++i) {
        const UnitType& u = t[uint16_t(i)];
        const int image = sp.imageFor(uint16_t(i));
        std::printf("%zu\t%s\t%s\t%s\t%08x\t%02x\t%s\t%d\t%d\t%d\n", i, u.name.c_str(), u.subtitle.c_str(), u.editorGroup.c_str(), u.flags,
                    u.groupFlags, u.paletteSection.c_str(), u.paletteOrder, image, sp.scriptId(image));
    }
    return 0;
}
