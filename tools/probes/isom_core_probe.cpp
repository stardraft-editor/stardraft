// The terrain core (IsomCore) next to the old engine's tables (a probe).
//   isom_core_probe ids <era>: each tileset terrain: our id and name (IsomTerrainTable), the core's brush values
//   isom_core_probe same <map> <px> <py> [extent]: paint the terrain of the corner at (px, py) there with the core;
//     which ISOM slots change their terrain
//   isom_core_probe render <map or .chk> <tx> <ty> <w> <h> <out.png>: those tiles of MTXM as an image (misfits framed)
//   isom_core_probe rules <maps dir>: Blizzard's maps by the CV5 rules of countTileMisfits (none should break them)
//   isom_core_probe rebuild <maps dir>: maps without ISOM: how many corners rebuildFromTiles gets right
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"

using namespace stardraft;

int main(int argc, char** argv) {
    GameData d;
    std::string e;
    const char* inst = std::getenv("STARDRAFT_INSTALL");
    if (!d.open(inst ? inst : "/Applications/StarCraft", e)) return std::fprintf(stderr, "%s\n", e.c_str()), 2;
    const std::string what = argc > 1 ? argv[1] : "";
    if (what == "ids" && argc > 2) {
        const uint16_t era = uint16_t(std::atoi(argv[2]));
        const IsomTerrainTable* t = IsomTerrainTable::forEra(era);
        for (uint16_t i = 1; t && i <= t->terrainCount(); ++i)
            if (t->isTerrain(i)) std::printf("ours %3u %s\n", i, t->terrainName(i));
        Tileset ts;
        if (!ts.load(d, era, e)) return 2;
        Chk chk;
        std::vector<uint8_t> dim{2, 0, 2, 0}, eraB{uint8_t(era), 0}, isom(2 * 3 * 8, 0), tile(8, 0);
        chk.set("DIM ", dim), chk.set("ERA ", eraB), chk.set("ISOM", isom), chk.set("TILE", tile), chk.set("MTXM", tile);
        IsomCore core;
        if (!core.load(chk, ts, e)) return std::fprintf(stderr, "%s\n", e.c_str()), 2;
        for (uint16_t v : core.brushes()) std::printf("core brush isom value %u\n", v);
        return 0;
    }
    if (what == "same" && argc > 4) {
        Bytes raw;
        if (!readMapChk(argv[2], raw, e)) return 2;
        Chk chk = Chk::parse(raw);
        const uint16_t era = uint16_t(readEra(chk) & 7);
        Tileset ts;
        ts.load(d, era, e);
        IsomGrid g;
        g.load(chk, e);
        int cx = 0, cy = 0;
        g.cornerAtPixel(std::atoi(argv[3]), std::atoi(argv[4]), cx, cy);
        const uint16_t terrain = g.terrainAt(cx, cy);
        int x = 0, y = 0;
        diamondAtPixel(std::atoi(argv[3]), std::atoi(argv[4]), x, y);
        const Bytes before = chk.effective("ISOM");
        IsomCore c;
        c.load(chk, ts, e);
        const bool ok = c.paint(x, y, terrain, argc > 5 ? std::atoi(argv[5]) : 2);
        c.updateTiles();
        c.store(chk);
        const Bytes after = chk.effective("ISOM");
        std::printf("diamond %d,%d terrain %u (%s): painted %d\n", x, y, terrain, IsomTerrainTable::forEra(era)->terrainName(terrain), ok);
        const int cols = g.rectCols();
        for (size_t i = 0; i < before.size() / 2; ++i) {
            const uint16_t a = uint16_t(before[2 * i] | before[2 * i + 1] << 8), b = uint16_t(after[2 * i] | after[2 * i + 1] << 8);
            if ((a >> 4) != (b >> 4))
                std::printf("rect %zu,%zu slot %zu: %u -> %u\n", (i / 4) % size_t(cols), (i / 4) / size_t(cols), i % 4, a >> 4, b >> 4);
        }
        return 0;
    }
    if (what == "render" && argc > 7) {
        const std::string path = argv[2];
        Bytes raw;
        if (path.size() > 4 && path.substr(path.size() - 4) == ".chk") {
            std::ifstream f(path, std::ios::binary);
            raw.assign(std::istreambuf_iterator<char>(f), {});
        } else if (!readMapChk(path, raw, e)) {
            return std::fprintf(stderr, "%s\n", e.c_str()), 2;
        }
        const Chk chk = Chk::parse(raw);
        Tileset ts;
        if (!ts.load(d, uint16_t(readEra(chk) & 7), e)) return 2;
        const TerrainLayer all = readTerrain(chk);
        const int tx = std::atoi(argv[3]), ty = std::atoi(argv[4]), w = std::atoi(argv[5]), h = std::atoi(argv[6]);
        TerrainLayer part;
        part.width = uint16_t(w), part.height = uint16_t(h);
        for (int y = ty; y < ty + h; ++y)
            for (int x = tx; x < tx + w; ++x)
                part.tiles.push_back(x < all.width && y < all.height ? all.tiles[size_t(y) * all.width + x] : 0);
        Image img = renderTerrain(ts, part);
        std::vector<std::pair<int, int>> where;
        countTileMisfits(chk, ts, &where);
        for (auto [mx, my] : where) {  // a red frame on each misfit
            if (mx < tx || my < ty || mx >= tx + w || my >= ty + h) continue;
            for (int k = 0; k < 32; ++k)
                for (auto [px, py] : {std::pair{k, 0}, {k, 31}, {0, k}, {31, k}}) {
                    uint8_t* p = &img.rgba[(size_t((my - ty) * 32 + py) * img.width + size_t((mx - tx) * 32 + px)) * 4];
                    p[0] = 255, p[1] = 0, p[2] = 0, p[3] = 255;
                }
        }
        return writePng(argv[7], img) ? 0 : 2;
    }
    if (what == "rules" && argc > 2) {
        Tileset ts[8];
        // Blizzard maps: neighboring rects that share no diamond shape (IsomTiles::beside), and tiles that do not stack
        // (stack bottom of the one above, top of this one): the CV5 rules of countTileMisfits find none there.
        long pairs = 0, unequal = 0, vert = 0, vertBad = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[2])) {
            std::string ext = entry.path().extension().string();
            for (auto& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            bool blizzard = entry.path().filename().string().rfind("(", 0) == 0;
            for (const auto& part : entry.path().parent_path())
                if (part == "ladder") blizzard = false;
            Bytes raw;
            if (!blizzard || (ext != ".scm" && ext != ".scx") || !readMapChk(entry.path(), raw, e)) continue;
            const Chk chk = Chk::parse(raw);
            const uint16_t era = uint16_t(readEra(chk) & 7);
            if (ts[era].groups.empty() && !ts[era].load(d, era, e)) continue;
            const IsomTiles* tiles = IsomTiles::forTileset(ts[era]);
            const TerrainLayer tile = readTerrain(chk, "TILE");
            for (int ty = 0; ty < tile.height; ++ty)
                for (int tx = 0; tx < tile.width; ++tx) {
                    const uint16_t gr = uint16_t(tile.tiles[size_t(ty) * tile.width + tx] >> 4);
                    if (gr >= 1024) continue;
                    if (ty > 0) {
                        const uint16_t up = uint16_t(tile.tiles[size_t(ty - 1) * tile.width + tx] >> 4);
                        if (up < 1024) ++vert, vertBad += tiles->stackBottom(up) != tiles->stackTop(gr);
                    }
                    if ((tx & 1) && tx + 1 < tile.width) {  // a rect's right tile and the next rect's left one
                        const uint16_t nx = uint16_t(tile.tiles[size_t(ty) * tile.width + tx + 1] >> 4);
                        if (nx < 1024) ++pairs, unequal += !tiles->beside(uint16_t(gr - 1), nx);
                    }
                }
        }
        std::printf("Blizzard maps: rect pairs %ld, sharing no diamond shape %ld; stacked tiles %ld, not stacking %ld\n", pairs, unequal, vert,
                    vertBad);
        return 0;
    }
    if (what == "rebuild" && argc > 2) {  // maps without ISOM: corners right
        Tileset ts[8];
        long total[8] = {}, right[8] = {};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[2])) {
            std::string ext = entry.path().extension().string();
            for (auto& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            Bytes raw;
            if ((ext != ".scm" && ext != ".scx") || !readMapChk(entry.path(), raw, e)) continue;
            const Chk real = Chk::parse(raw);
            const uint16_t era = uint16_t(readEra(real) & 7);
            IsomGrid orig;
            if (!orig.load(real, e) || (ts[era].groups.empty() && !ts[era].load(d, era, e))) continue;
            Chk b = real;
            b.remove("ISOM");
            IsomGrid gb;
            gb.setTileset(&ts[era]);
            if (!gb.rebuildFromTiles(b, e)) continue;
            for (int y = 0; y <= orig.rectRows(); ++y)
                for (int x = 0; x <= orig.rectCols(); ++x)
                    if (orig.isCorner(x, y)) ++total[era], right[era] += orig.terrainAt(x, y) == gb.terrainAt(x, y);
        }
        long T = 0, R = 0;
        for (int era = 0; era < 8; ++era) {
            T += total[era], R += right[era];
            std::printf("%-9s corners right %.4f\n", tilesetName(uint16_t(era)), total[era] ? double(right[era]) / total[era] : 1.0);
        }
        std::printf("all: %.4f\n", T ? double(R) / T : 1.0);
        return 0;
    }
    std::fprintf(stderr, "usage: isom_core_probe ids <era> | same <map> <px> <py> [extent] | render <map> <tx> <ty> <w> <h> <out.png> | rules <dir> | rebuild <dir>\n");
    return 2;
}
