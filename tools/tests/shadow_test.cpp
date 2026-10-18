// Render a few units with and without their iscript shadows, offscreen, to check shadow loading.
//
// usage: shadow_test <install dir> <out prefix>

#include <SDL3/SDL.h>

#include <cstdio>

#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/isom.h"
#include "stardraft/renderer.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string err;
    GameData data;
    Tileset ts;
    UnitTypes types;
    UnitSprites sprites;
    if (!data.open(argv[1], err) || !ts.load(data, 0, err) || !types.load(data, err) || !sprites.load(data, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const char* names[] = {"Terran Command Center", "Terran Marine", "Terran Battlecruiser", "Protoss Carrier", "Zerg Hatchery",
                           "Terran Siege Tank"};
    Chk chk;
    IsomGrid grid;
    grid.create(chk, 32, 16, 0, 1, ts, err);
    SDL_Init(SDL_INIT_VIDEO);
    render::Renderer r;
    if (!r.initOffscreen(1024, 512, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    r.setLoadBudget(1e9);
    r.setSources(&data, &sprites, &ts, 0);
    r.updateTerrain(readTerrain(chk));
    for (int withShadows = 0; withShadows < 2; ++withShadows) {
        render::Scene scene;
        scene.camera = {0, 0, 1};
        float x = 80;
        for (const char* n : names) {
            uint16_t t = *types.findByName(n);
            render::SpriteDraw d;
            d.image = sprites.imageFor(t);
            d.frame = sprites.defaultFrame(t, d.image, d.flip);
            d.x = x, d.y = 250;
            d.air = types[t].is(UnitFlag::Flyer);
            scene.sprites.push_back(d);
            for (const auto& o : sprites.script(d.image).under) {
                AnimImage a;
                bool hd = loadAnimImage(data, o.image, GraphicsQuality::HD2, a, err);
                if (!withShadows)
                    std::printf("%-22s image %d: shadow image %d at %d,%d (draw fn shadow: %d, HD2 anim: %s)\n", n, d.image, o.image,
                                o.x, o.y, sprites.isShadow(o.image), hd ? "ok" : err.c_str());
                if (!withShadows) continue;
                render::SpriteDraw s = d;
                s.image = o.image, s.x = d.x + o.x, s.y = d.y + o.y, s.shadow = sprites.isShadow(o.image), s.air = false;
                scene.sprites.push_back(s);
            }
            x += 165;
        }
        r.renderOffscreenAndWait(scene);
        std::vector<uint8_t> px;
        int w, h;
        r.readOffscreen(px, w, h);
        Image img{w, h, px};
        std::string path = std::string(argv[2]) + (withShadows ? "_shadows.png" : "_plain.png");
        writePng(path.c_str(), img);
    }
    r.shutdown();
    SDL_Quit();
    return 0;
}
