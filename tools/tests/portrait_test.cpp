// Decodes unit portraits (WebM) from the game and writes frames as PNG: checks the VP8 and VP9 paths.
// usage: portrait_test <install dir> <out dir> [path, e.g. tmarine\TMaTlk0]
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/portraits.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace stardraft;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: portrait_test <install dir> <out dir> [path]\n"), 2;
    std::string err;
    if (fs::path(argv[1]).extension() == ".smk") {  // a Smacker video (StarCraft 1.16.1's portraits): each frame as PNG
        std::ifstream in(argv[1], std::ios::binary);
        const Bytes file((std::istreambuf_iterator<char>(in)), {});
        PortraitVideo v;
        const bool ok = decodeSmacker(file, v, err);
        std::printf("smk %s: %s %dx%d, %zu frames, %.1f fps%s%s\n", argv[1], ok ? "ok" : "FAILED", v.width, v.height, v.frames.size(), v.fps,
                    ok ? "" : ": ", ok ? "" : err.c_str());
        fs::create_directories(argv[2]);
        for (size_t f = 0; f < v.frames.size(); ++f)
            stbi_write_png((fs::path(argv[2]) / ("smk_" + std::to_string(f) + ".png")).string().c_str(), v.width, v.height, 4, v.frames[f].data(),
                           v.width * 4);
        return ok ? 0 : 1;
    }
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const std::string path = argc > 3 ? argv[3] : "tmarine\\TMaTlk0";
    fs::create_directories(argv[2]);
    int failures = 0;
    struct Kind {
        const char* name;
        GraphicsQuality q;
        bool carbot;
    } kinds[] = {{"sd", GraphicsQuality::SD, false}, {"hd2", GraphicsQuality::HD2, false}, {"hd", GraphicsQuality::HD, false}, {"carbot", GraphicsQuality::HD, true}};
    for (const auto& k : kinds) {
        PortraitVideo v;
        const bool ok = loadPortrait(data, path, 0, k.q, k.carbot, v, err);
        std::printf("%-6s %s: %s %dx%d, %zu frames%s%s\n", k.name, path.c_str(), ok ? "ok" : "FAILED", v.width, v.height, v.frames.size(), ok ? "" : ": ",
                    ok ? "" : err.c_str());
        if (!ok) {
            ++failures;
            continue;
        }
        for (size_t f = 0; f < v.frames.size(); f += std::max<size_t>(1, v.frames.size() / 3)) {
            const fs::path out = fs::path(argv[2]) / (std::string(k.name) + "_" + std::to_string(f) + ".png");
            stbi_write_png(out.string().c_str(), v.width, v.height, 4, v.frames[f].data(), v.width * 4);
        }
    }
    return failures ? 1 : 0;
}
