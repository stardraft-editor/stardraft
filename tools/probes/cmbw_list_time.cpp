// How long the mod's game data takes to list its files (the Assets page's game sounds list them on first view).
// usage: cmbw_list_time <Cosmonarchy install>
#include <chrono>
#include <cstdio>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string err;
    GameData data;
    auto t0 = std::chrono::steady_clock::now();
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    auto t1 = std::chrono::steady_clock::now();
    const auto files = data.listFiles();
    auto t2 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::printf("open %.0f ms, listFiles %.0f ms (%zu files)\n", ms(t0, t1), ms(t1, t2), files.size());
}
