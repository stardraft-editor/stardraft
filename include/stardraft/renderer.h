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

#include <SDL3/SDL_gpu.h>

#include "stardraft/hd_assets.h"
#include "stardraft/scene.h"

struct SDL_Window;

namespace stardraft {

class GameData;
class UnitSprites;
struct Tileset;
struct TerrainLayer;

namespace render {


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
    // SD art: each player's units in the shades of its color's row of tunit.pcx (a mod's colors have shades that a tint
    // of the grey ramp cannot give: Rainbow), not the grey ramp tinted by setPlayerColors.
    // (a bit for each of the 12 owners: a player of a custom RGB color stays tinted, its color has no row)
    void setExactTeamColors(uint16_t owners);
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

    // A shell that draws its own UI with the same device (stardraft-next): the map goes into `target` (made by the
    // shell with colorFormat(), COLOR_TARGET and SAMPLER usage) in the shell's command buffer. Needs createDevice.
    SDL_GPUDevice* device() const;
    SDL_GPUTextureFormat colorFormat() const;
    bool drawTo(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32_t width, uint32_t height, const Scene& scene);

    // Per-frame budget (ms) for loading new sprite images; the rest load on later frames.
    void setLoadBudget(double ms);
    Stats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace render
}  // namespace stardraft
