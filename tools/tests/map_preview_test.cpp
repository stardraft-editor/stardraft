// map_preview_test: the map picture of the Quick Look extensions and Spotlight (apps/quicklook/map_preview.h), with no
// game data: a small map made here (a .scx in <folder>), its info and its picture with and without a tile color table.
//
// usage: map_preview_test <folder>    (the folder is made; the test writes test.scx, tile-colors-4.bin and two PNGs in it)

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "map_preview.h"
#include "stardraft/map_file.h"
#include "stardraft/strings.h"
#include "stardraft/units.h"

namespace fs = std::filesystem;
using namespace stardraft;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

void u16(Bytes& b, unsigned v) { b.push_back(uint8_t(v)), b.push_back(uint8_t(v >> 8)); }

uint32_t pixel(const Image& img, int x, int y) {
    const uint8_t* p = &img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
    return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: map_preview_test <folder>\n"), 2;
    const fs::path dir = argv[1];
    std::error_code ec;
    fs::create_directories(dir, ec);

    // A 64 x 32 jungle map: the left half tile group 1, the right half group 2; a start location of player 1 and a
    // unit of player 2; a name with a color code and a description in Korean (UTF-8).
    const int w = 64, h = 32;
    Chk chk;
    Bytes d;
    u16(d, w), u16(d, h);
    chk.set("DIM ", d);
    d.clear(), u16(d, 4);
    chk.set("ERA ", d);
    d.clear();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) u16(d, x < w / 2 ? 0x10 : 0x20);
    chk.set("MTXM", d);
    chk.set("OWNR", Bytes{6, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    StringTable strings;
    const uint32_t name = strings.add("\x03Jungle\x01 Test");
    const uint32_t desc = strings.add("\xEC\xA0\x95\xEA\xB8\x80 \xED\x85\x8C\xEC\x8A\xA4\xED\x8A\xB8\r\nSecond line");
    strings.write(chk);
    d.clear(), u16(d, name), u16(d, desc);
    chk.set("SPRP", d);
    std::vector<Unit> units(2);
    units[0].serial = 1, units[0].type = 214, units[0].owner = 0, units[0].x = 10 * 32, units[0].y = 10 * 32;
    units[1].serial = 2, units[1].type = 0, units[1].owner = 1, units[1].x = 50 * 32 + 16, units[1].y = 20 * 32 + 16;
    writeUnits(chk, units);
    const fs::path map = dir / "test.scx";
    std::string err;
    check(writeMap(map, chk.serialize(), {}, err), "the map is written " + err);

    // The info.
    preview::MapInfo info;
    Image img;
    bool table = true;
    check(preview::renderFile(map, dir, 256, img, info, &table, err), "the map is drawn " + err);
    check(info.width == w && info.height == h && info.era == 4, "its size and tileset");
    check(info.name == "Jungle Test", "its name without the color codes: " + info.name);
    check(info.description == "\xEC\xA0\x95\xEA\xB8\x80 \xED\x85\x8C\xEC\x8A\xA4\xED\x8A\xB8\nSecond line", "its description: " + info.description);
    check(info.players == 3 && info.startLocations == 1, "its players (3) and start locations (1)");
    check(!table, "no table in the folder: the tileset's base color");
    check(img.width == 256 && img.height == 128, "the picture is 4 pixels a tile: " + std::to_string(img.width) + " x " + std::to_string(img.height));
    check(pixel(img, 4, 120) != pixel(img, 250, 120), "two tile groups: two colors (base color)");
    writePng((dir / "base.png").string().c_str(), img);

    // A table: group 1 green, group 2 blue.
    std::vector<uint32_t> colors(16 * 3, 0);
    for (int i = 0; i < 16; ++i) colors[16 + i] = 0x208020, colors[32 + i] = 0x2040C0;
    const fs::path file = preview::tileColorsFile(dir, 4);
    check(file.filename() == "tile-colors-4.bin", "the table's file name");
    check(preview::writeTileColors(file, colors, err), "the table is written " + err);
    std::vector<uint32_t> back;
    check(preview::readTileColors(file, back) && back == colors, "the table reads back the same");
    check(preview::renderFile(map, dir, 512, img, info, &table, err) && table, "drawn with the table");
    check(img.width == 512 && img.height == 256, "8 pixels a tile");
    check(pixel(img, 4, 250) == 0x208020 && pixel(img, 508, 250) == 0x2040C0, "the table's colors");
    check(pixel(img, 50 * 8 + 4, 20 * 8 + 4) == 0x0C48CC, "player 2's unit: a blue dot");
    check(pixel(img, 10 * 8 - 16, 10 * 8) == 0xF40404, "player 1's start location: a red frame");
    writePng((dir / "table.png").string().c_str(), img);

    // No map.
    check(!preview::renderFile(dir / "none.scx", dir, 256, img, info, &table, err), "a file that is not there: no picture");

    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
