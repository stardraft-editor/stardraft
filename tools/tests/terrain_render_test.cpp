// The editor's terrain renderer (GPU, src/render) against the CPU reference (renderTerrain), roadmap N.6.
//
// usage: terrain_render_test <install dir> [<folder or map>...]
//
// 1. Seams: for each tileset, a map filled with one flat megatile, drawn zoomed out (mipmapped atlas, SD
//    and HD2) at many zoom levels and positions. Every pixel is an average of that megatile's own pixels, so
//    it must stay in the megatile's color range. A pixel outside it read a neighbour in the atlas: the tile
//    grid shows as lines on zoomed-out maps.
// 2. HD atlases: every megatile of each tileset, drawn at one screen pixel per HD texel (HD2, HD and the
//    Carbot skins), must be the HD tile of that index as the CPU decodes it.
// 3. Tiles: each map, drawn in SD at 100% (nearest pixels), must be the same picture as renderTerrain draws
//    (render_map): the same megatile at each tile, no magenta.
//
// Renders offscreen: no window, no focus. Exit code 1 if a check fails.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/map_file.h"
#include "stardraft/renderer.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace fs = std::filesystem;

namespace {

constexpr int kView = 1024;  // offscreen size

struct Range {
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    void add(const uint8_t* c) {
        for (int i = 0; i < 3; ++i) lo[i] = std::min<int>(lo[i], c[i]), hi[i] = std::max<int>(hi[i], c[i]);
    }
    int span() const { return hi[0] - lo[0] + hi[1] - lo[1] + hi[2] - lo[2]; }
    // How far a color is outside the range (0 inside).
    int outside(const uint8_t* c) const {
        int d = 0;
        for (int i = 0; i < 3; ++i) d = std::max({d, lo[i] - int(c[i]), int(c[i]) - hi[i]});
        return d;
    }
};

Range sdRange(const Tileset& ts, size_t mega) {
    Range r;
    for (uint32_t ref : ts.megatiles[mega]) {
        const size_t mini = ref >> 1;
        if (mini >= ts.minitiles.size()) continue;
        for (uint8_t idx : ts.minitiles[mini]) {
            const Rgb& c = ts.palette[idx];
            const uint8_t px[3] = {c.r, c.g, c.b};
            r.add(px);
        }
    }
    return r;
}

Range hdRange(const HdTiles& hd, size_t mega) {
    Range r;
    DdsTexture t;
    t.format = hd.format, t.width = t.height = hd.tileSize, t.data = hd.tiles[mega];
    std::vector<uint8_t> px;
    decodeDds(t, 0, 0, hd.tileSize, hd.tileSize, px);
    for (size_t i = 0; i < px.size(); i += 4) r.add(&px[i]);
    return r;
}

// A tile value that shows megatile `mega`, or -1.
int tileValueFor(const Tileset& ts, size_t mega) {
    for (size_t g = 0; g < ts.groups.size() && g < 4096; ++g)
        for (int s = 0; s < 16; ++s)
            if (ts.groups[g].megatiles[s] == mega) return int(g << 4 | s);
    return -1;
}

// The flattest megatile with two (else one) atlas neighbours (the atlas is a square grid of `count`
// megatiles) whose mean color is well outside its range: a read from a neighbour then shows.
template <class RangeOf, class MeanOf>
int pickMegatile(const Tileset& ts, size_t count, RangeOf rangeOf, MeanOf meanOf, int minFar = 2) {
    const size_t cols = size_t(std::ceil(std::sqrt(double(count))));
    int best = -1, bestSpan = 1 << 30;
    for (size_t m = cols + 1; m + cols < std::min(count, ts.megatiles.size()); m += 3) {
        const Range r = rangeOf(m);
        if (r.span() >= bestSpan || tileValueFor(ts, m) < 0) continue;
        int far = 0;
        for (size_t n : {m - 1, m + 1, m - cols, m + cols}) {
            uint8_t mean[3];
            meanOf(n, mean);
            far += r.outside(mean) > 24;
        }
        if (far >= minFar) best = int(m), bestSpan = r.span();
    }
    return best < 0 && minFar > 1 ? pickMegatile(ts, count, rangeOf, meanOf, minFar - 1) : best;
}

void meanSd(const Tileset& ts, size_t mega, uint8_t* out) {
    int sum[3] = {0, 0, 0};
    for (uint32_t ref : ts.megatiles[mega]) {
        const size_t mini = ref >> 1;
        if (mini >= ts.minitiles.size()) continue;
        for (uint8_t idx : ts.minitiles[mini]) sum[0] += ts.palette[idx].r, sum[1] += ts.palette[idx].g, sum[2] += ts.palette[idx].b;
    }
    for (int i = 0; i < 3; ++i) out[i] = uint8_t(sum[i] / 1024);
}

void meanHd(const HdTiles& hd, size_t mega, uint8_t* out) {
    DdsTexture t;
    t.format = hd.format, t.width = t.height = hd.tileSize, t.data = hd.tiles[mega];
    std::vector<uint8_t> px;
    decodeDds(t, 0, 0, hd.tileSize, hd.tileSize, px);
    double sum[3] = {0, 0, 0};
    for (size_t i = 0; i < px.size(); i += 4) sum[0] += px[i], sum[1] += px[i + 1], sum[2] += px[i + 2];
    const double n = double(px.size() / 4);
    for (int i = 0; i < 3; ++i) out[i] = uint8_t(sum[i] / n);
}

bool isMap(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e == ".scm" || e == ".scx" || e == ".chk";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: terrain_render_test <install dir> [<folder or map>...] [--out <dir>]\n");
        return 2;
    }
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    std::vector<fs::path> maps;
    for (int i = 2; i < argc; ++i) {
        std::error_code ec;
        if (fs::is_directory(argv[i], ec)) {
            for (auto it = fs::recursive_directory_iterator(argv[i], fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec) && isMap(it->path())) maps.push_back(it->path());
        } else {
            maps.push_back(argv[i]);
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    render::Renderer r;
    if (!r.initOffscreen(kView, kView, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    int failures = 0;
    std::vector<uint8_t> px;
    int w = 0, h = 0;

    // 1. Seams.
    std::printf("Seams (one flat megatile, zoomed out; pixels outside its color range):\n");
    Tileset sets[8];
    for (uint16_t era = 0; era < 8; ++era) {
        if (!sets[era].load(data, era, err)) {
            std::printf("  %s: %s\n", tilesetName(era), err.c_str());
            ++failures;
            continue;
        }
        const Tileset& ts = sets[era];
        HdTiles hd;
        if (!loadHdTiles(data, era, GraphicsQuality::HD2, hd, err)) {
            std::printf("  %s: %s\n", tilesetName(era), err.c_str());
            ++failures;
            continue;
        }
        for (GraphicsQuality q : {GraphicsQuality::SD, GraphicsQuality::HD2}) {
            const bool isHd = q != GraphicsQuality::SD;
            const size_t count = isHd ? hd.tiles.size() : ts.megatiles.size();
            const int mega = isHd ? pickMegatile(ts, count, [&](size_t m) { return hdRange(hd, m); },
                                                 [&](size_t m, uint8_t* o) { meanHd(hd, m, o); })
                                  : pickMegatile(ts, count, [&](size_t m) { return sdRange(ts, m); },
                                                 [&](size_t m, uint8_t* o) { meanSd(ts, m, o); });
            if (mega < 0) {
                std::printf("  %s %s: no megatile to test with\n", tilesetName(era), qualityName(q));
                continue;
            }
            const Range range = isHd ? hdRange(hd, size_t(mega)) : sdRange(ts, size_t(mega));
            TerrainLayer layer;
            layer.width = layer.height = 128;
            layer.tiles.assign(128 * 128, uint16_t(tileValueFor(ts, size_t(mega))));
            r.setQuality(q);
            r.setSources(&data, nullptr, &ts, era);
            r.updateTerrain(layer);
            int bad = 0, worst = 0;
            const int tolerance = isHd ? 6 : 2;  // GPU block decoding and rounding
            for (float zoom : {0.9f, 0.7f, 0.55f, 0.45f, 0.37f, 0.3f, 0.23f, 0.17f, 0.12f, 0.09f, 0.0625f}) {
                for (float shift : {0.0f, 7.3f, 13.9f}) {
                    render::Scene scene;
                    scene.camera = {200 + shift, 300 + shift * 1.7f, zoom};
                    r.renderOffscreenAndWait(scene);
                    if (!r.readOffscreen(px, w, h)) continue;
                    const int vis = std::min(w, int((128 * 32 - scene.camera.x) * zoom) - 1);
                    const int visH = std::min(h, int((128 * 32 - scene.camera.y) * zoom) - 1);
                    for (int y = 0; y < visH; ++y)
                        for (int x = 0; x < vis; ++x) {
                            const int d = range.outside(&px[(size_t(y) * w + x) * 4]);
                            worst = std::max(worst, d);
                            bad += d > tolerance;
                        }
                }
            }
            std::printf("  %-9s %-3s megatile %5d: %d pixels outside (largest distance %d)\n", tilesetName(era), qualityName(q),
                        mega, bad, worst);
            if (bad) {
                std::printf("    FAIL: the terrain shader reads neighbouring megatiles of the atlas\n");
                ++failures;
            }
        }
    }

    // 2. HD atlases: a layer with every megatile in order, drawn at one screen pixel per texel.
    std::printf("\nHD atlases (every megatile at one pixel per texel; pixels that differ from the CPU decoding):\n");
    for (uint16_t era = 0; era < 8; ++era) {
        const Tileset& ts = sets[era];
        if (ts.groups.empty()) continue;
        // Tile values for megatiles 0..n-1 (megatiles no tile value names stay out).
        std::vector<int> valueOf(ts.megatiles.size(), -1);
        for (size_t g = 0; g < ts.groups.size(); ++g)
            for (int k = 0; k < 16; ++k)
                if (ts.groups[g].megatiles[k] < valueOf.size() && valueOf[ts.groups[g].megatiles[k]] < 0)
                    valueOf[ts.groups[g].megatiles[k]] = int(g << 4 | k);
        TerrainLayer layer;
        layer.width = 128;
        layer.height = uint16_t((ts.megatiles.size() + 127) / 128);
        layer.tiles.assign(size_t(layer.width) * layer.height, 0);
        for (size_t m = 0; m < ts.megatiles.size(); ++m) layer.tiles[m] = uint16_t(std::max(valueOf[m], 0));
        for (int skin = 0; skin < 4; ++skin) {
            const GraphicsQuality q = skin % 2 ? GraphicsQuality::HD : GraphicsQuality::HD2;
            const bool carbot = skin >= 2;
            HdTiles hd;
            if (!loadHdTiles(data, era, q, hd, err, carbot)) {
                std::printf("  %s: %s\n", tilesetName(era), err.c_str());
                ++failures;
                continue;
            }
            r.setQuality(q);
            r.setCarbot(carbot);
            r.setSources(&data, nullptr, &ts, era);
            r.updateTerrain(layer);
            const int texels = hd.tileSize, perView = kView / texels;  // tiles across one view
            const float zoom = float(texels) / 32;
            size_t wrong = 0, tiles = 0;
            int firstMega = -1;
            std::vector<uint8_t> dec;
            for (int vy = 0; vy < layer.height; vy += perView)
                for (int vx = 0; vx < layer.width; vx += perView) {
                    render::Scene scene;
                    scene.camera = {vx * 32.0f, vy * 32.0f, zoom};
                    r.renderOffscreenAndWait(scene);
                    if (!r.readOffscreen(px, w, h)) continue;
                    for (int ty = vy; ty < std::min<int>(vy + perView, layer.height); ++ty)
                        for (int tx = vx; tx < std::min<int>(vx + perView, layer.width); ++tx) {
                            const size_t m = size_t(ty) * layer.width + tx;
                            if (m >= ts.megatiles.size() || valueOf[m] < 0 || m >= hd.tiles.size()) continue;
                            DdsTexture t;
                            t.format = hd.format, t.width = t.height = texels, t.data = hd.tiles[m];
                            decodeDds(t, 0, 0, texels, texels, dec);
                            ++tiles;
                            size_t bad = 0;
                            for (int y = 0; y < texels; ++y)
                                for (int x = 0; x < texels; ++x) {
                                    const uint8_t* g = &px[(size_t((ty - vy) * texels + y) * w + (tx - vx) * texels + x) * 4];
                                    const uint8_t* c = &dec[(size_t(y) * texels + x) * 4];
                                    bad += std::abs(g[0] - c[0]) > 8 || std::abs(g[1] - c[1]) > 8 || std::abs(g[2] - c[2]) > 8;
                                }
                            if (bad * 20 > size_t(texels * texels)) {  // more than 5% of the tile: another picture
                                if (!wrong++) firstMega = int(m);
                            }
                        }
                }
            std::printf("  %-9s %-3s%-7s %zu megatiles, %zu are not their HD tile%s", tilesetName(era), qualityName(q),
                        carbot ? " Carbot" : "", tiles, wrong, wrong ? " (first: megatile " : "\n");
            if (wrong) {
                std::printf("%d)\n    FAIL: the HD atlas puts megatiles at the wrong place\n", firstMega);
                ++failures;
            }
        }
    }
    r.setCarbot(false);

    // 3. Tiles, map by map: GPU in SD at 100% against renderTerrain.
    size_t checked = 0, differ = 0;
    if (!maps.empty()) std::printf("\nMaps (SD at 100%%, GPU against renderTerrain):\n");
    for (const fs::path& path : maps) {
        Bytes raw;
        if (path.extension() == ".chk") {
            std::ifstream in(path, std::ios::binary);
            raw.assign(std::istreambuf_iterator<char>(in), {});
        } else if (!readMapChk(path, raw, err)) {
            continue;  // protected maps do not open
        }
        const Chk chk = Chk::parse(raw);
        const uint16_t era = readEra(chk);
        const Tileset& ts = sets[era];
        if (ts.groups.empty()) continue;
        const TerrainLayer t = readTerrain(chk, "MTXM");
        if (!t.width || !t.height) continue;
        const Image ref = renderTerrain(ts, t, 1);
        r.setQuality(GraphicsQuality::SD);
        r.setSources(&data, nullptr, &ts, era);
        r.updateTerrain(t);
        size_t wrong = 0;
        int firstX = -1, firstY = -1;
        for (int vy = 0; vy < ref.height; vy += kView)
            for (int vx = 0; vx < ref.width; vx += kView) {
                render::Scene scene;
                scene.camera = {float(vx), float(vy), 1.0f};
                r.renderOffscreenAndWait(scene);
                if (!r.readOffscreen(px, w, h)) continue;
                for (int y = 0; y < h && vy + y < ref.height; ++y)
                    for (int x = 0; x < w && vx + x < ref.width; ++x)
                        if (ts.megatileFor(t.tiles[size_t((vy + y) / 32) * t.width + (vx + x) / 32]) >= 0 &&  // else terrain_check
                            std::memcmp(&px[(size_t(y) * w + x) * 4], &ref.rgba[(size_t(vy + y) * ref.width + vx + x) * 4], 3)) {
                            if (!wrong++) firstX = vx + x, firstY = vy + y;
                        }
            }
        ++checked;
        if (wrong) {
            ++differ;
            std::printf("  %s (%s): %zu pixels differ, first at tile (%d, %d)\n", path.string().c_str(), tilesetName(era), wrong,
                        firstX / 32, firstY / 32);
        }
    }
    if (!maps.empty()) std::printf("  %zu maps checked, %zu differ\n", checked, differ);
    failures += int(differ);
    r.shutdown();
    SDL_Quit();
    std::printf("\n%s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
