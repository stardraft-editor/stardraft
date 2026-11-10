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
    // Windows: the drivers that createDevice does not try ("direct3d12", "vulkan", "warp", comma separated): those of a
    // start whose window did not show. When it leaves out all of them, it tries all. STARDRAFT_GPU_DRIVER goes first.
    void setSkippedDrivers(const std::string& drivers);
    // The driver that createDevice made the device with, as setSkippedDrivers names it; "" before.
    const char* driverTried() const;
    // Windows: called with each driver's name before createDevice tries it, so that a computer that stops in a try is
    // known at the next start.
    void setTryHook(void (*hook)(const char* driver));
    // Windows: createDevice asks each video card whether it has Direct3D 12 at feature level 12_0 (about 100 ms of the
    // start). d3d12Cards is then the cards and their driver versions when they all have it ("": not all, or it did not
    // ask). With that text of an earlier start that made the Direct3D 12 device, createDevice does not ask the same
    // cards again, and SDL does not make a device only to see that it can (about 170 ms more).
    void setKnownD3d12Cards(const std::string& cards);
    const std::string& d3d12Cards() const;
    // Windows: the driver that createDevice tries first ("vulkan", "direct3d12", "warp"; "": Vulkan, the default); the
    // others follow when it cannot be used.
    void setPreferredDriver(const std::string& driver);
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

    // No map is open: the tileset's atlases, the terrain and the sprites' images go (their GPU memory); the next map's
    // setSources and updateTerrain make them again.
    void releaseMapData();
    // Data sources. The tileset is (re)loaded when the era or quality changes.
    void setSources(const GameData* data, UnitSprites* sprites, const Tileset* tileset, uint16_t era);
    // The sprites' images changed (other draw functions or remappings): load each image again when it shows.
    void reloadImages();
    void setPlayerColors(const uint32_t rgb[12]);  // each owner's team color, 0xRRGGBB (the map's player colors)
    // Upload the terrain (MTXM values); call again whenever it changes.
    void updateTerrain(const TerrainLayer& mtxm);
    // The terrain on a grid larger than the map (map layers' terrain outside of it): `tiles` the grid's tiles (tile 0
    // where there is none), the map's tile (0, 0) at (originX, originY) of it, the map mapW x mapH tiles. The previews
    // (setTerrainPreview) stay in the map's tile indices.
    void updateTerrainExtended(const TerrainLayer& tiles, int originX, int originY, int mapW, int mapH);
    // Tiles shown instead of the map's (a doodad about to be placed): (tile index y * width + x, CV5
    // tile value). Replaces the previous preview; empty clears it.
    void setTerrainPreview(const std::vector<std::pair<int, uint16_t>>& cells);
    // A second terrain drawn over the terrain, at each rect's alpha, only in the rects (a map layer's opacity: the
    // terrain is the map's without the see-through layers, this one the map's with them). `tiles`: the same grid as the
    // terrain's (its size and origin: updateTerrain, updateTerrainExtended); a rect: tiles x0..x1, y0..y1 (x1, y1 past
    // it) of that grid. Empty rects: none.
    struct TerrainOverlayRect {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float alpha = 1;
    };
    void setTerrainOverlay(const TerrainLayer& tiles, std::vector<TerrainOverlayRect> rects);
    void clearTerrainOverlay();

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
