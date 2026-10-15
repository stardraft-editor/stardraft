#pragma once

// The map's draw list (Scene): plain values, no GPU types, so the editor's portable code can build overlays without the
// renderer (renderer.h draws it).

#include <cstdint>
#include <string>
#include <vector>

namespace stardraft {
namespace render {

struct Camera {
    float x = 0, y = 0;  // map pixel at the top-left of the view
    float zoom = 1;      // screen pixels per map pixel
};

struct SpriteDraw {
    int image = -1;      // images.dat id
    int frame = 0;
    bool flip = false;
    float x = 0, y = 0;  // origin (unit center), map pixels
    uint8_t owner = 0;   // player color 0..11
    float alpha = 1;
    bool air = false;    // drawn above all ground sprites
    float depthBias = 0; // small tie-breaker (e.g. turrets above their base)
    // The sprite's place for the drawing order when this image sits away from it (a turret at its base's overlay
    // point, an overlay): the game orders sprites, so an image's offset inside its sprite does not count.
    bool hasSortY = false;
    float sortY = 0;
    bool shadow = false; // draw as a translucent shadow on the ground, below every unit
    bool tintRed = false; // highlight (e.g. a placement that is not allowed)
    bool ground = false;  // drawn on the ground under every unit, above shadows (selection circles)
    bool glow = false;    // additive-looking glow (images.dat draw function 9): brightness is its opacity
};

struct RectDraw {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // map pixels if `world`, else screen pixels
    uint32_t rgba = 0xFFFFFFFF;           // 0xRRGGBBAA
    bool fill = false;
    float thickness = 1;                  // outline width, screen pixels
    bool world = true;
    bool line = false;                    // a line from (x0, y0) to (x1, y1), `thickness` wide
};

// A text's width in the map font's units (TextDraw::scale pixels each); its lines are 12 units apart.
int textWidth(const std::string& text);

struct TextDraw {
    float x = 0, y = 0;
    std::string text;
    uint32_t rgba = 0xFFFFFFFF;
    bool world = true;
    float scale = 1.5f;
};

struct Scene {
    Camera camera;
    std::vector<SpriteDraw> sprites;
    std::vector<RectDraw> rects;  // drawn after sprites, in order
    std::vector<TextDraw> texts;  // drawn last
};

}  // namespace render
}  // namespace stardraft
