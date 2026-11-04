#include "stardraft/doodad_coverage.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include <chrono>
#include <cstdio>
#include <random>
#undef NDEBUG
#include <cassert>
using namespace stardraft;
int main(int argc, char** argv) {
    DoodadFootprintUnion mask;
    std::mt19937 random(514);
    for (int run = 0; run < 40; ++run) {
        mask.reset(32, 24);
        std::vector<uint8_t> expected(32 * 24);
        for (int n = 0; n < 30; ++n) {
            const int x = int(random() % 46) - 7, y = int(random() % 38) - 7;
            const int w = 1 + int(random() % 12), h = 1 + int(random() % 12);
            mask.add(x, y, w, h);
            for (int yy = std::max(0, y); yy < std::min(24, y + h); ++yy)
                for (int xx = std::max(0, x); xx < std::min(32, x + w); ++xx)
                    expected[size_t(yy) * 32 + xx] = 1;
            mask.update();
            assert(mask.cells == expected);
            mask.update();
            assert(mask.cells == expected);
        }
    }
    mask.reset(256, 256);
    const auto start = std::chrono::steady_clock::now();
    for (int y = 0; y < 249; ++y)
        for (int x = 0; x < 249; ++x)
            mask.add(x, y, 8, 8);
    mask.update();
    assert(std::count(mask.cells.begin(), mask.cells.end(), uint8_t(1)) == 256 * 256);
    std::printf("1200 incremental clipped/overlapping unions passed; 62001 footprints on 256x256: %.3f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    if (argc > 2) {
        GameData data;
        std::string error;
        assert(data.open(argv[1], error));
        Editor fixture(data);
        assert(fixture.newMap(48, 32, 0, 1, error));
        const auto& types = fixture.doodadTypes().all();
        const auto first = std::find_if(types.begin(), types.end(), [](const auto& t) { return t.inPalette && t.id == 0; });
        assert(first != types.end());
        std::vector<Editor::TileAt> tiles;
        for (auto [x, y] : {std::pair{8, 8}, std::pair{20, 8}, std::pair{26, 18}})
            for (int row = 1; row < first->height; ++row)
                for (int col = 0; col < first->width; ++col) {
                    const auto group = first->required[size_t(row) * first->width + col];
                    if (group)
                        tiles.push_back({x + col, y + row, uint16_t(group << 4)});
                }
        fixture.placeTileValues(tiles);
        assert(doodadFits(fixture.chk(), *first, 8, 8, true));
        assert(fixture.save(argv[2], error));
        std::printf("Synthetic screenshot fixture saved; choose doodad %d (%dx%d), anchors 8,8 / 20,8 / 26,18\n", first->id, first->width,
                    first->height);
    }
}
