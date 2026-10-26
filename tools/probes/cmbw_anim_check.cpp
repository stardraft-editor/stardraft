// The idle animation of each Cosmonarchy unit as the map shows it: its image, whether the image turns, and the frames
// it shows over some game ticks (to find units whose idle animation goes round their facings).
// usage: cmbw_anim_check <Cosmonarchy install> <Cosmonarchy map> [ticks]
//        cmbw_anim_check <install> <map> --frames <image> <first> <count> <out.pgm>   (those frames side by side, gray)
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/sprites.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const int ticks = argc > 3 ? std::atoi(argv[3]) : 300;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s%c", err.c_str(), 10), 1;
    Editor ed(data);
    if (!ed.open(argv[2], err)) return std::fprintf(stderr, "%s%c", err.c_str(), 10), 1;
    const UnitSprites& sp = ed.unitSprites();
    if (argc > 7 && std::string(argv[3]) == "--frames") {
        const Grp* g = const_cast<UnitSprites&>(sp).grp(std::atoi(argv[4]));
        if (!g) return 1;
        const int first = std::atoi(argv[5]), count = std::atoi(argv[6]), w = g->width, h = g->height;
        std::vector<uint8_t> out(size_t(w) * count * h, 0);
        for (int k = 0; k < count && size_t(first + k) < g->frames.size(); ++k) {
            const Grp::Frame& f = g->frames[size_t(first + k)];
            for (int y = 0; y < f.height; ++y)
                for (int x = 0; x < f.width; ++x)
                    if (const uint8_t v = f.pixels[size_t(y) * f.width + x]) out[size_t(f.y + y) * w * count + size_t(k) * w + f.x + x] = uint8_t(80 + v % 176);
        }
        std::FILE* o = std::fopen(argv[7], "wb");
        std::fprintf(o, "P5 %d %d 255%c", w * count, h, 10);
        std::fwrite(out.data(), 1, out.size(), o);
        std::fclose(o);
        return 0;
    }
    for (size_t u = 0; u < ed.unitTypes().size(); ++u) {
        const bool building = ed.unitTypes()[u].is(UnitFlag::Building);
        const int image = sp.imageFor(uint16_t(u));
        if (image < 0) continue;
        ImageAnim a(sp, image, uint32_t(u) + 1, building && sp.animOffset(image, 16) ? 16 : 0);
        std::vector<int> shown;
        std::set<int> raw;
        bool flip = false;
        for (int t = 0; t < ticks && a.running(); ++t) {
            raw.insert(a.frame());
            const int f = sp.animatedFrame(uint16_t(u), image, a.frame(), flip);
            if (shown.empty() || shown.back() != f) shown.push_back(f);
            a.tick();
        }
        std::set<int> facings;
        const int set = sp.facingSet(image);
        for (int f : shown) facings.insert(f % set);
        std::printf("%4zu %-32s image %4d set %d raw %zu shown %zu facings %zu:", u, ed.unitTypes()[u].name.c_str(), image, set, raw.size(),
                    shown.size(), facings.size());
        for (size_t i = 0; i < shown.size() && i < 16; ++i) std::printf(" %d", shown[i]);
        std::printf(" | raw");
        for (int f : raw) std::printf(" %d", f);
        std::printf("%c", 10);
    }
}
