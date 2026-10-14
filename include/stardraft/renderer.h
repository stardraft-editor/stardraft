#pragma once

// GPU map renderer on SDL3's GPU API (D3D12 / Metal / Vulkan).
//
// Draws a Scene: terrain (one draw: megatile-index texture + megatile atlas), sprites (instanced
// quads ordered by depth from y, team color in the shader) and screen-space overlays (rects, text).
// HD/HD2 graphics are uploaded still block-compressed; SD graphics are the fallback and are used
// for terrain when zoomed far out (mipmapped).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "stardraft/hd_assets.h"

struct SDL_Window;

namespace stardraft {

class GameData;
class UnitSprites;
struct Tileset;
struct TerrainLayer;

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

struct Stats {
    int drawCalls = 0;
    int sprites = 0;
    int imagesResident = 0;
    int imagesPending = 0;  // images not drawn this frame because they are still loading
    double cpuMs = 0;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Render to a window, or to an offscreen target of the given size (benchmarks, screenshots).
    bool init(SDL_Window* window, std::string& error);
    // init in two parts, so the GPU device can be made on another thread while the program loads other things:
    // createDevice (any thread), then attach (the window's thread).
    bool createDevice(std::string& error);
    bool attach(SDL_Window* window, std::string& error);
    bool initOffscreen(int width, int height, std::string& error);
    void shutdown();

    // Graphics quality for tiles and sprites (default HD2, what the game uses at 1080p).
    void setQuality(GraphicsQuality q);
    // The Cartooned (Carbot) skin for HD and HD2 art (falls back to the normal art where missing).
    void setCarbot(bool carbot);
    GraphicsQuality quality() const;
    // False when the GPU cannot sample HD and HD2 art (BC1 and BC3 textures): the art is then SD at every quality.
    bool hdArt() const;
    // The GPU API in use ("direct3d12", "vulkan", "metal"); "" before createDevice.
    const char* driver() const;

    // Data sources. The tileset is (re)loaded when the era or quality changes.
    void setSources(const GameData* data, UnitSprites* sprites, const Tileset* tileset, uint16_t era);
    // The sprites' images changed (other draw functions or remappings): load each image again when it shows.
    void reloadImages();
    void setPlayerColors(const uint32_t rgb[12]);  // each owner's team color, 0xRRGGBB (the map's player colors)
    // Upload the terrain (MTXM values); call again whenever it changes.
    void updateTerrain(const TerrainLayer& mtxm);
    // Tiles shown instead of the map's (a doodad about to be placed): (tile index y * width + x, CV5
    // tile value). Replaces the previous preview; empty clears it.
    void setTerrainPreview(const std::vector<std::pair<int, uint16_t>>& cells);

    // Draw a frame. Returns false if nothing could be drawn (e.g. minimized window).
    bool render(const Scene& scene);
    // Offscreen: render and wait for the GPU to finish; returns GPU+CPU frame time in ms.
    double renderOffscreenAndWait(const Scene& scene);
    // Offscreen: read the last frame back as RGBA (for screenshots and tests).
    bool readOffscreen(std::vector<uint8_t>& rgba, int& width, int& height);

    // Per-frame budget (ms) for loading new sprite images; the rest load on later frames.
    void setLoadBudget(double ms);
    Stats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace render
}  // namespace stardraft
