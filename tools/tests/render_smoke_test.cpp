// The map view's renderer without game data: the GPU device and all its pipelines are made (every shader in the
// format of this GPU API), and an offscreen frame with rectangles, a line and a text reads back as drawn.
// GitHub runs it on Linux (Vulkan on lavapipe, SDL_VIDEODRIVER=offscreen).
//
// usage: render_smoke_test [out.png]
//
// Renders offscreen: no window, no focus. Exit code 1 if a check fails.

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

#include "stardraft/renderer.h"
#include "stardraft/terrain.h"

using namespace stardraft::render;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
    if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    constexpr int kW = 256, kH = 128;
    Renderer r;
    std::string err;
    const bool made = r.initOffscreen(kW, kH, err);
    check(made, std::string("the device and the pipelines are made (") + (made ? r.driver() : err.c_str()) + ")");
    if (!made) return 1;
    std::printf("      HD art: %s\n", r.hdArt() ? "yes" : "no (SD only)");

    Scene scene;
    scene.camera.x = 0, scene.camera.y = 0, scene.camera.zoom = 1;
    RectDraw red;
    red.x0 = 10, red.y0 = 10, red.x1 = 60, red.y1 = 50, red.rgba = 0xFF0000FF, red.fill = true, red.world = false;
    RectDraw green = red;
    green.x0 = 100, green.x1 = 150, green.rgba = 0x00FF00FF;
    RectDraw outline = red;
    outline.x0 = 180, outline.y0 = 10, outline.x1 = 240, outline.y1 = 60, outline.fill = false, outline.thickness = 2, outline.rgba = 0xFFFFFFFF;
    RectDraw line = red;
    line.x0 = 10, line.y0 = 100, line.x1 = 240, line.y1 = 100, line.line = true, line.thickness = 3, line.rgba = 0x0000FFFF;
    scene.rects = {red, green, outline, line};
    TextDraw text;
    text.x = 100, text.y = 70, text.text = "StarDraft", text.world = false, text.rgba = 0xFFFF00FF;
    scene.texts = {text};
    r.renderOffscreenAndWait(scene);

    std::vector<uint8_t> px;
    int w = 0, h = 0;
    check(r.readOffscreen(px, w, h) && w == kW && h == kH && px.size() == size_t(kW) * kH * 4, "the frame reads back");
    if (px.size() != size_t(kW) * kH * 4) return 1;
    auto at = [&](int x, int y) {
        const uint8_t* p = &px[(size_t(y) * kW + x) * 4];
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | 0xFF;
    };
    auto near = [](uint32_t a, uint32_t b) {
        for (int s = 8; s < 32; s += 8) {
            const int d = int((a >> s) & 0xFF) - int((b >> s) & 0xFF);
            if (d < -8 || d > 8) return false;
        }
        return true;
    };
    char buf[96];
    std::snprintf(buf, sizeof buf, "a filled rectangle is red (0x%08X)", at(35, 30));
    check(near(at(35, 30), 0xFF0000FF), buf);
    std::snprintf(buf, sizeof buf, "a second one is green (0x%08X)", at(125, 30));
    check(near(at(125, 30), 0x00FF00FF), buf);
    check(near(at(181, 35), 0xFFFFFFFF) && !near(at(210, 35), 0xFFFFFFFF), "an outline is drawn on its edge only");
    check(near(at(120, 100), 0x0000FFFF) && !near(at(120, 106), 0x0000FFFF), "a line is drawn where it goes");
    int yellow = 0;
    for (int y = 60; y < 100; ++y)
        for (int x = 90; x < 200; ++x) yellow += near(at(x, y), 0xFFFF00FF);
    check(yellow > 20, "a text is drawn (" + std::to_string(yellow) + " pixels)");
    check(near(at(5, 5), 0x121214FF), "the rest is the background");

    if (argc > 1) {
        stardraft::Image img;
        img.width = kW, img.height = kH, img.rgba = px;
        stardraft::writePng(argv[1], img);
    }
    r.shutdown();
    SDL_Quit();
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
