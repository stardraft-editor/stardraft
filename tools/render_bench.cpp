// Renderer benchmark: a 256x256 badlands map with painted terrain and thousands of units,
// rendered offscreen at 1920x1080 for many frames while panning, at several zoom levels and qualities.
//
// usage: render_bench <install dir> [units=5000] [frames=300] [screenshot.png]

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/renderer.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: render_bench <install dir> [units] [frames]\n");
        return 2;
    }
    int unitCount = argc > 2 ? std::atoi(argv[2]) : 5000;
    int frames = argc > 3 ? std::atoi(argv[3]) : 300;
    std::string err;
    GameData data;
    Tileset ts;
    UnitTypes types;
    UnitSprites sprites;
    if (!data.open(argv[1], err) || !ts.load(data, 0, err) || !types.load(data, err) || !sprites.load(data, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    // Map: 256x256 dirt with random terrain blobs.
    Chk chk;
    IsomGrid grid;
    grid.create(chk, 256, 256, 0, 1, ts, err);
    std::mt19937 rng(7);
    for (int i = 0; i < 400; ++i) {
        int x = int(rng() % 128), y = int(rng() % 256);
        if ((x + y) & 1) ++x;
        grid.paint(x, y, uint16_t(2 + rng() % 8), 1 + int(rng() % 4));
    }
    grid.store(chk, err);
    TerrainLayer terrain = readTerrain(chk);

    // Units: random palette types and owners.
    std::vector<uint16_t> palette;
    for (uint16_t i = 0; i < types.size(); ++i)
        if ((types[i].availability & UnitAvail::Palette) && !types[i].is(UnitFlag::Subunit)) palette.push_back(i);
    std::vector<render::SpriteDraw> unitDraws;
    for (int i = 0; i < unitCount; ++i) {
        uint16_t t = palette[rng() % palette.size()];
        render::SpriteDraw s;
        s.image = sprites.imageFor(t);
        s.frame = sprites.defaultFrame(t, s.image, s.flip);
        s.x = float(rng() % (256 * 32)), s.y = float(rng() % (256 * 32));
        s.owner = uint8_t(rng() % 8);
        s.air = types[t].is(UnitFlag::Flyer) && !types[t].is(UnitFlag::Building);
        unitDraws.push_back(s);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    render::Renderer r;
    if (!r.initOffscreen(1920, 1080, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    r.setLoadBudget(1e9);  // load everything up front so the timing measures drawing

    std::printf("256x256 map, %d units, %d frames at 1920x1080\n", unitCount, frames);
    for (GraphicsQuality q : {GraphicsQuality::HD2, GraphicsQuality::HD, GraphicsQuality::SD}) {
        auto t0 = std::chrono::steady_clock::now();
        r.setQuality(q);
        r.setSources(&data, &sprites, &ts, 0);
        r.updateTerrain(terrain);
        render::Scene scene;
        scene.sprites = unitDraws;
        scene.camera = {0, 0, 0.125f};  // whole map visible: loads every image
        r.renderOffscreenAndWait(scene);
        double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("\n%s: setup + first frame %.0f ms, %d images resident\n", qualityName(q), loadMs, r.stats().imagesResident);
        for (float zoom : {1.0f, 0.5f, 0.125f}) {
            std::vector<double> times;
            int drawn = 0, calls = 0;
            double cpu = 0;
            for (int f = 0; f < frames; ++f) {
                scene.camera.zoom = zoom;
                scene.camera.x = float((f * 37) % std::max(1, int(256 * 32 - 1920 / zoom)));
                scene.camera.y = float((f * 23) % std::max(1, int(256 * 32 - 1080 / zoom)));
                times.push_back(r.renderOffscreenAndWait(scene));
                drawn += r.stats().sprites;
                calls += r.stats().drawCalls;
                cpu += r.stats().cpuMs;
            }
            std::sort(times.begin(), times.end());
            double avg = 0;
            for (double t : times) avg += t;
            avg /= times.size();
            std::printf("  zoom %5.3f: avg %.2f ms (%.0f fps), p99 %.2f ms, CPU %.2f ms, %d sprites/frame, %d draws/frame\n", zoom,
                        avg, 1000.0 / avg, times[times.size() * 99 / 100], cpu / frames, drawn / frames, calls / frames);
        }
    }
    // Screenshots of a busy area in each quality, for checking the output by eye.
    if (argc > 4) {
        for (GraphicsQuality q : {GraphicsQuality::HD2, GraphicsQuality::SD}) {
            r.setQuality(q);
            r.setSources(&data, &sprites, &ts, 0);
            r.updateTerrain(terrain);
            render::Scene scene;
            scene.sprites = unitDraws;
            scene.camera = {2000, 2000, 1.0f};
            scene.rects.push_back({2100, 2100, 2400, 2300, 0x40FF40FF, false, 2});
            scene.texts.push_back({2100, 2080, "Location 1", 0xFFFFFFFF});
            r.renderOffscreenAndWait(scene);
            std::vector<uint8_t> px;
            int w = 0, h = 0;
            if (r.readOffscreen(px, w, h)) {
                Image img{w, h, px};
                std::string path = std::string(argv[4]) + "_" + qualityName(q) + ".png";
                writePng(path.c_str(), img);
                std::printf("wrote %s\n", path.c_str());
            }
        }
    }
    r.shutdown();
    SDL_Quit();
    return 0;
}
