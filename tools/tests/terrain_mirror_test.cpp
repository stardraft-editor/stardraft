#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom_core.h"
#include "stardraft/terrain_mirror.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <set>
#undef NDEBUG
#include <cassert>
using namespace stardraft;
namespace {
using Pixels = std::array<uint8_t, 3072>;
Pixels pixels(const Tileset& ts, uint16_t tile, bool reflected = false) {
    Pixels out{};
    const int mega = ts.megatileFor(tile);
    assert(mega >= 0 && size_t(mega) < ts.megatiles.size());
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const int sx = reflected ? 31 - x : x;
            const auto mini = ts.megatiles[size_t(mega)][size_t(y / 8 * 4 + sx / 8)];
            const auto color = ts.palette[ts.minitiles[mini >> 1][size_t(y % 8 * 8 + ((mini & 1) ? 7 - sx % 8 : sx % 8))]];
            const size_t i = size_t(y * 32 + x) * 3;
            out[i] = color.r, out[i + 1] = color.g, out[i + 2] = color.b;
        }
    return out;
}
uint64_t hash(const Pixels& p) {
    uint64_t h = 14695981039346656037ull;
    for (auto c : p) h = (h ^ c) * 1099511628211ull;
    return h;
}
} // namespace
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "terrain_mirror_test <game installation> [fixture prefix]\n");
        return 2;
    }
    GameData game;
    std::string error;
    assert(game.open(argv[1], error));
    for (int era = 0; era < 8; ++era) {
        Editor ed(game);
        assert(ed.newMap(64, 64, uint16_t(era), 1, error));
        const auto& ts = ed.tileset();
        struct Art {
            uint16_t tile;
            bool doodad;
            Pixels pixels;
        };
        std::vector<Art> art;
        std::map<uint64_t, std::vector<size_t>> exact;
        std::set<std::pair<int, bool>> seen;
        for (size_t g = 0; g < ts.groups.size() && g < 4096; ++g)
            for (int sub = 0; sub < 16; ++sub) {
                const uint16_t value = uint16_t(g * 16 + sub);
                const int mega = ts.megatileFor(value);
                const bool doodad = ts.groups[g].type == 1;
                if (value == 0xffff || mega <= 0 || !seen.insert({mega, doodad}).second) continue;
                art.push_back({value, doodad, pixels(ts, value)});
                exact[hash(art.back().pixels)].push_back(art.size() - 1);
            }
        TileMirrorMatcher matcher(ts);
        size_t exactCount = 0, approximateCount = 0;
        for (size_t i = 0; i < art.size(); ++i) {
            const auto reflected = pixels(ts, art[i].tile, true);
            bool hasExact = false;
            if (const auto it = exact.find(hash(reflected)); it != exact.end())
                for (auto c : it->second) hasExact |= (art[i].doodad || !art[c].doodad) && art[c].pixels == reflected;
            // Every available exact mirror must be found, regardless of CV5/VF4
            // metadata. Sample the remaining artwork to keep this test quick.
            if (!hasExact && i % 23) continue;
            const auto result = matcher.match(art[i].tile);
            assert(result.matched);
            assert(art[i].doodad || ts.groupFor(result.tile)->type != 1);
            if (hasExact) {
                assert(result.exact && pixels(ts, result.tile) == reflected);
                ++exactCount;
            } else ++approximateCount;
            assert(matcher.match(art[i].tile).tile == result.tile);
        }
        assert(exactCount > 0 && approximateCount > 0);
        IsomCore core;
        assert(core.load(ed.chk(), ts, error));
        const auto brushes = core.brushes();
        for (size_t i = 1; i < std::min<size_t>(brushes.size(), 5); ++i) {
            const int x = 256 + int(i % 2) * 192, y = 256 + int(i / 2) * 192;
            ed.paintBrush(x, y, brushes[i], Brush{Brush::Diamond, .5f});
            ed.paintTerrainLine(x, y + 160, x + 96, y + 112, brushes[i], Brush{Brush::Diamond, .5f});
        }
        assert(ed.copyTiles(2, 2, 21, 25));
        auto copy = ed.terrainCopies();
        copy.terrain = {};
        const auto original = copy.tiles.values;
        const int w = copy.tiles.width, h = copy.tiles.height;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) copy.tiles.values[size_t(y) * w + w - 1 - x] = matcher.match(original[size_t(y) * w + x]).tile;
        copy.tiles.values[0] = 0xffff; // a selection-mask hole remains untouched
        const auto before = ed.chk().serialize(), isom = ed.chk().effective("ISOM");
        const auto tilesBefore = ed.terrain().tiles;
        ed.setTerrainCopies(copy);
        assert(ed.pasteTiles(34, 2) > 0);
        assert(ed.chk().effective("ISOM") == isom);
        const auto expected = copy.tiles.values;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                uint16_t value = tilesBefore[size_t(y) * 64 + x];
                if (x >= 34 && x < 34 + w && y >= 2 && y < 2 + h && expected[size_t(y - 2) * w + x - 34] != 0xffff)
                    value = expected[size_t(y - 2) * w + x - 34];
                assert(ed.terrain().tiles[size_t(y) * 64 + x] == value);
            }
        const auto after = ed.chk().serialize();
        ed.undo();
        assert(ed.chk().serialize() == before);
        ed.redo();
        assert(ed.chk().serialize() == after);
        if (argc > 2) assert(ed.save(std::string(argv[2]) + "-" + std::to_string(era) + ".scx", error));
        std::printf("Tileset %d: %zu exact artwork mirrors, %zu approximate samples; raw motif paste, selection mask, untouched ISOM and "
                    "atomic undo/redo passed\n",
                    era, exactCount, approximateCount);
    }
}
