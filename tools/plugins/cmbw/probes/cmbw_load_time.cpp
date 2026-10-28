// How long a map's unit pictures take to load, by part: the GRP files read and decoded, and the SD atlas's pixels
// (the renderer's loadImage loop: glow colors and the player color rows), for each image and (image, color row).
// usage: cmbw_load_time <Cosmonarchy install> <map>
#include <chrono>
#include <cstdio>
#include <set>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"

using namespace stardraft;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string err;
    GameData data;
    auto t0 = Clock::now();
    if (!cmbw::open(data, argv[1], err)) return 1;
    std::printf("mod data: %.0f ms\n", ms(t0));
    t0 = Clock::now();
    Editor ed(data);
    if (!ed.open(argv[2], err)) return 1;
    std::printf("map open: %.0f ms, %zu units\n", ms(t0), ed.units().size());
    UnitSprites& sp = ed.unitSprites();
    const Tileset& ts = ed.tileset();
    std::set<int> images;
    std::set<std::pair<int, int>> rows;
    for (const Unit& u : ed.units())
        for (int img : {sp.imageFor(u.type), sp.subunitImage(u.type)}) {
            if (img < 0) continue;
            images.insert(img);
            rows.insert({img, sp.colorRow(u.owner)});
            for (const auto& o : sp.script(img).under) images.insert(o.image);
            for (const auto& o : sp.script(img).over) images.insert(o.image);
        }
    t0 = Clock::now();
    size_t pixels = 0, frames = 0, trimmed = 0;
    for (int img : images)
        if (const Grp* g = sp.grp(img)) {
            frames += g->frames.size(), pixels += size_t(g->width) * g->height * g->frames.size();
            for (const Grp::Frame& fr : g->frames) trimmed += size_t(fr.width) * fr.height;
        }
    std::printf("%zu images (%zu frames, %.0f Mpx atlas, %.0f Mpx trimmed): GRP read and decode %.0f ms\n", images.size(), frames,
                pixels / 1e6, trimmed / 1e6, ms(t0));
    auto atlas = [&](int img, int row) {
        const Grp* g = sp.grp(img);
        if (!g) return size_t(0);
        size_t sum = 0;
        for (const Grp::Frame& fr : g->frames)
            for (int i = 0; i < fr.width * fr.height; ++i) {
                const uint8_t idx = fr.pixels[size_t(i)];
                if (!idx) continue;
                Rgb glow{};
                const bool glowing = sp.glowColor(img, idx, ts, glow);
                const bool team = !glowing && idx >= 8 && idx < 16;
                const Rgb& c = glowing ? glow : ts.palette[team ? sp.remapRow(idx, row >= 0 ? row : 6) : idx];
                sum += c.r;
            }
        return sum;
    };
    t0 = Clock::now();
    size_t s = 0;
    for (int img : images) s += atlas(img, -1);
    std::printf("atlas pixels, one per image: %.0f ms\n", ms(t0));
    t0 = Clock::now();
    for (auto [img, row] : rows) s += atlas(img, row);
    std::printf("atlas pixels, %zu (image, color row): %.0f ms (%zu)\n", rows.size(), ms(t0), s & 1);
    return 0;
}
