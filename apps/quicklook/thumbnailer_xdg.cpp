// stardraft-thumbnailer: the picture of a map file (.scm, .scx) for the file managers of the free desktops (Files,
// Dolphin, Thunar, Nemo...), as packaging/linux/stardraft.thumbnailer names it. The picture is the one of the macOS Quick
// Look extensions (map_preview.h): the minimap's colors, the units and the start locations. The terrain's colors come
// from the tile color tables that the editor writes when a map opens (map_preview.h: tileColorsFolder); with no table
// (or in a thumbnailer's sandbox, which cannot see the cache) the tileset's base colors.
//
// usage: stardraft-thumbnailer [-s <size>] <map file or file:// URI> <output.png>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "map_preview.h"

namespace fs = std::filesystem;

namespace {

// A file:// URI as a path (its %XX bytes decoded); any other text is a path already.
std::string pathOf(const std::string& arg) {
    if (arg.rfind("file://", 0) != 0) return arg;
    std::string p = arg.substr(7);
    if (const size_t slash = p.find('/'); slash != 0 && slash != std::string::npos) p = p.substr(slash);  // (file://host/path)
    std::string out;
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '%' && i + 2 < p.size()) {
            const std::string hex = p.substr(i + 1, 2);
            char* end = nullptr;
            const long v = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == 0) {
                out += char(v);
                i += 2;
                continue;
            }
        }
        out += p[i];
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    int size = 256;
    std::string in, out;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "-s" || a == "--size") && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (in.empty()) in = a;
        else if (out.empty()) out = a;
    }
    if (in.empty() || out.empty()) return std::fprintf(stderr, "usage: stardraft-thumbnailer [-s <size>] <map> <output.png>\n"), 2;
    size = std::clamp(size, 16, 1024);
    stardraft::Image img;
    stardraft::preview::MapInfo info;
    std::string error;
    if (!stardraft::preview::renderFile(fs::path(pathOf(in)), stardraft::preview::tileColorsFolder(), size, img, info, nullptr, error))
        return std::fprintf(stderr, "stardraft-thumbnailer: %s\n", error.c_str()), 1;
    if (!stardraft::writePng(out.c_str(), img)) return std::fprintf(stderr, "stardraft-thumbnailer: could not write %s\n", out.c_str()), 1;
    return 0;
}
