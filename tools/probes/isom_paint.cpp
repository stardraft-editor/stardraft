// Paint isometric terrain on a map and save it.
//
// usage: isom_paint <in map> <out map> <x> <y> <terrain> <brush size> [<x> <y> <terrain> <size>]...
//   x, y: diamond center on the rect grid (x in 0..width/2, y in 0..height, x + y even);
//         each grid step is 64 px horizontally and 32 px vertically.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "stardraft/isom.h"
#include "stardraft/map_file.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: isom_paint <in map> <out map> <x> <y> <terrain> [size] ...\n");
        return 2;
    }
    std::string err;
    std::vector<ArchiveFile> files;
    if (!readMapFiles(argv[1], files, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    Bytes raw;
    for (const auto& f : files)
        if (f.name == kChkArchivePath) raw = f.data;
    Chk chk = Chk::parse(raw);

    IsomGrid grid;
    if (!grid.load(chk, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    for (int i = 3; i + 3 < argc; i += 4) {
        int x = std::atoi(argv[i]), y = std::atoi(argv[i + 1]);
        int terrain = std::atoi(argv[i + 2]), size = std::atoi(argv[i + 3]);
        if (!grid.paint(x, y, uint16_t(terrain), size)) {
            std::fprintf(stderr, "invalid paint at %d,%d terrain %d (x + y must be even)\n", x, y, terrain);
            return 1;
        }
        std::printf("painted terrain %d at %d,%d size %d\n", terrain, x, y, size);
    }
    if (int bad = grid.invalidDiamonds()) std::printf("warning: %d invalid diamonds\n", bad);
    if (!grid.store(chk, err) || !writeMap(argv[2], chk.serialize(), files, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::printf("wrote %s\n", argv[2]);
    return 0;
}
