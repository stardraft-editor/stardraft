// The iscript ID of every images.datex entry, as the mod's datex gives it (one line each: image, ID).
// usage: cmbw_image_scripts <Cosmonarchy install> <map>
#include <cstdio>
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/sprites.h"
using namespace stardraft;
int main(int argc, char** argv) {
    std::string err;
    GameData data;
    if (argc < 3 || !cmbw::open(data, argv[1], err)) return 1;
    Editor ed(data);
    if (!ed.open(argv[2], err)) return 1;
    for (int i = 0; i < 5000; ++i)
        if (const int s = ed.unitSprites().scriptId(i); s >= 0) std::printf("%d %d\n", i, s);
}
