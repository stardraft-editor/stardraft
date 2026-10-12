// List (and optionally extract) files from the StarCraft: Remastered CASC storage.
//
// usage: casc_ls <install dir> [substring filter] [extract dir]

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "stardraft/game_data.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: casc_ls <install dir> [substring filter] [extract dir]\n");
        return 2;
    }
    stardraft::GameData data;
    std::string err;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::string filter = argc > 2 ? argv[2] : "";
    std::filesystem::path outDir = argc > 3 ? argv[3] : "";
    for (const auto& name : data.listFiles()) {
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;
        std::printf("%s\n", name.c_str());
        if (outDir.empty()) continue;
        stardraft::Bytes bytes;
        if (!data.readFile(name, bytes, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            continue;
        }
        std::filesystem::path out = outDir / name;
        std::filesystem::create_directories(out.parent_path());
        std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                   static_cast<std::streamsize>(bytes.size()));
    }
    return 0;
}
