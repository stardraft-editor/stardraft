// A folder's maps by tileset: the first few of each (to pick test maps of a tileset).
// usage: cmbw_map_eras <maps folder> [how many of each, default 3]
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "stardraft/chk.h"
#include "stardraft/map_file.h"

namespace fs = std::filesystem;
using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: cmbw_map_eras <maps folder> [how many]\n"), 2;
    const int most = argc > 2 ? std::atoi(argv[2]) : 3;
    std::array<int, 8> shown{};
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(argv[1], ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".scx" && p.extension() != ".scm") continue;
        Bytes raw;
        std::string err;
        if (!readMapFile(p, raw, err)) continue;
        const Chk chk = Chk::parse(raw);
        const Section* e = chk.find("ERA ");
        if (!e || e->data.size() < 2) continue;
        const int era = e->data[0] & 7;
        if (shown[size_t(era)]++ < most) std::printf("%d %s\n", era, p.string().c_str());
    }
    return 0;
}
