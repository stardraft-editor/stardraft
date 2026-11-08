#include <functional>
#include "stardraft/renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

#define STB_EASY_FONT_IMPLEMENTATION
#include <stb_easy_font.h>

// The shaders in each format: DXIL (Direct3D 12, made by the build on Windows), SPIR-V (Vulkan) and MSL (Metal), the
// last two from shaders/gen (tools/gen_shaders.py).
#ifdef STARDRAFT_SHADERS_DXIL
#include "shaders/downsample_ps.h"
#include "shaders/downsample_vs.h"
#include "shaders/solid_ps.h"
#include "shaders/solid_vs.h"
#include "shaders/sprite_ps.h"
#include "shaders/sprite_vs.h"
#include "shaders/terrain_ps.h"
#include "shaders/terrain_vs.h"
#endif
#include "gen/downsample_ps_msl.h"
#include "gen/downsample_ps_spirv.h"
#include "gen/downsample_vs_msl.h"
#include "gen/downsample_vs_spirv.h"
#include "gen/solid_ps_msl.h"
#include "gen/solid_ps_spirv.h"
#include "gen/solid_vs_msl.h"
#include "gen/solid_vs_spirv.h"
#include "gen/sprite_ps_msl.h"
#include "gen/sprite_ps_spirv.h"
#include "gen/sprite_vs_msl.h"
#include "gen/sprite_vs_spirv.h"
#include "gen/terrain_ps_msl.h"
#include "gen/terrain_ps_spirv.h"
#include "gen/terrain_vs_msl.h"
#include "gen/terrain_vs_spirv.h"

namespace stardraft::render {

int textWidth(const std::string& text) { return stb_easy_font_width(const_cast<char*>(text.c_str())); }

namespace {

struct ShaderCode {
    const unsigned char *dxil = nullptr, *spirv = nullptr, *msl = nullptr;
    size_t dxilSize = 0, spirvSize = 0, mslSize = 0;
};
#ifdef STARDRAFT_SHADERS_DXIL
#define STARDRAFT_DXIL(n) g_##n, g_##n##_spirv, g_##n##_msl, sizeof g_##n
#else
#define STARDRAFT_DXIL(n) nullptr, g_##n##_spirv, g_##n##_msl, 0
#endif
#define STARDRAFT_SHADER(n) ShaderCode{STARDRAFT_DXIL(n), g_##n##_spirv_size, g_##n##_msl_size}

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// Uniform blocks (16-byte aligned like the HLSL cbuffers).
struct TerrainUniforms {
    float camera[2], zoom, maxLod;
    float viewport[2], mapTiles[2];
    float atlasSize[2], tileTexels, atlasCols;
};
struct SpriteUniforms {
    float camera[2], zoom, firstInstance;
    float viewport[2], pad[2];
};
struct SolidUniforms {
    float viewport[2], firstRect, pad;
};
struct GpuInstance {
    float pos[2], size[2], uv[4], color[4], depth, flags, level, pad;
    float warpUv[4], warpLim[4];  // the warp texture (draw function 12): its uv at the quad's corners, its frame's uv
};
static_assert(sizeof(GpuInstance) == 96, "sprite.hlsl reads instances 96 bytes apart");
struct SpriteFragUniforms {
    float targetSize[2], zoom, sdArt;
};
struct GpuRect {
    float rect[4], color[4], line[4];  // line[0]: thickness, 0 for a rectangle
};

// Player colours (StarCraft defaults, players 1-12); setPlayerColors gives a map's own.
float kPlayerRgb[12][3] = {{0.957f, 0.016f, 0.016f}, {0.047f, 0.282f, 0.8f},   {0.173f, 0.706f, 0.58f},
                                 {0.533f, 0.251f, 0.612f}, {0.973f, 0.549f, 0.078f}, {0.439f, 0.188f, 0.078f},
                                 {0.8f, 0.878f, 0.816f},   {0.988f, 0.988f, 0.22f},  {0.031f, 0.502f, 0.031f},
                                 {0.988f, 0.988f, 0.486f}, {0.925f, 0.769f, 0.69f},  {0.251f, 0.408f, 0.831f}};

struct ImageTex {
    bool loaded = false, failed = false;
    bool queued = false;  // (its HD art is with the renderer's worker)
    bool team = false;  // (SD: it has player color pixels)
    SDL_GPUTexture* diffuse = nullptr;
    SDL_GPUTexture* mask = nullptr;
    float texW = 0, texH = 0;      // texels
    float boxW = 0, boxH = 0;      // frame box, map pixels
    float worldPerTexel = 1;       // map pixels per texel
    struct Frame {
        float u0, v0, u1, v1;      // texels
        float x, y, w, h;          // inside the box, map pixels
        int page = -1;             // (SD: its atlas page; -1 not there yet)
        bool team = false;         // (SD: it has player color pixels)
    };
    std::vector<Frame> frames;
    // SD: each frame goes to the shared atlas pages when a scene first draws it (atlasFrame), not the whole GRP at
    // once (Cosmonarchy's images have hundreds of frames: a map with all its units had 393 million pixels of them).
    bool sd = false;
    int row = -1;  // (sd) the color row of its player color pixels' own shades; -1 the grey ramp, tinted
};

// An SD atlas page: frames on shelves, each with a clear pixel around it.
struct AtlasPage {
    SDL_GPUTexture* diffuse = nullptr;
    SDL_GPUTexture* mask = nullptr;  // R8: 255 where the player color tints
    uint32_t x = 0, y = 0, shelf = 0;
};
constexpr uint32_t kAtlasPage = 4096;
constexpr size_t kAtlasPages = 12;  // (12 pages: 960 MB; past it all frames go and come again as they are drawn)

uint32_t mipCount(uint32_t w, uint32_t h) {
    uint32_t n = 1;
    while ((w | h) >> n) ++n;
    return n;
}

}  // namespace

struct Renderer::Impl {
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    std::string skipped;           // setSkippedDrivers
    const char* tried = "";        // driverTried
    std::string preferred;         // setPreferredDriver
    void (*tryHook)(const char*) = nullptr;  // setTryHook
    SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    bool pipelinesReady = false;  // made for colorFormat
    SDL_GPUTexture* offscreen = nullptr;
    int offscreenW = 0, offscreenH = 0;
    SDL_GPUTexture* depth = nullptr;
    uint32_t depthW = 0, depthH = 0;
    // The picture before the sprites that bend what is under them (cloaks, the EMP shockwave): made only for a scene
    // that has such sprites; those sprites then read it.
    SDL_GPUTexture* backdrop = nullptr;
    uint32_t backdropW = 0, backdropH = 0;
    SDL_GPUTextureFormat backdropFormat = SDL_GPU_TEXTUREFORMAT_INVALID;

    SDL_GPUGraphicsPipeline* terrainPipe = nullptr;
    SDL_GPUGraphicsPipeline* spritePipe = nullptr;
    SDL_GPUGraphicsPipeline* glowPipe = nullptr;  // sprites added onto what is drawn (glows)
    SDL_GPUGraphicsPipeline* solidPipe = nullptr;
    SDL_GPUGraphicsPipeline* downPipe = nullptr;  // RGBA8 target, no depth
    SDL_GPUSampler* pointClamp = nullptr;
    SDL_GPUSampler* linearClamp = nullptr;
    SDL_GPUSampler* linearMip = nullptr;
    SDL_GPUTexture* blackTex = nullptr;

    // Terrain
    SDL_GPUTexture* tileIndex = nullptr;
    uint16_t mapW = 0, mapH = 0;  // the tile index's grid (the map, or a larger grid: updateTerrainExtended)
    int originX = 0, originY = 0;  // ... the map's tile (0, 0) in it
    uint16_t realW = 0, realH = 0;  // ... the map's own size (the previews' indices, the sprites' depth)
    uint16_t tileIndexW = 0, tileIndexH = 0;  // size the tile index texture was created at
    SDL_GPUTexture* hdAtlas = nullptr;
    SDL_GPUTexture* hdDown = nullptr;  // HD atlas box-filtered to 32 px tiles, mipmapped
    float hdDownW = 0, hdDownH = 0;
    float hdAtlasW = 0, hdAtlasH = 0, hdTile = 0, hdCols = 1;
    SDL_GPUTexture* sdAtlas = nullptr;
    float sdAtlasW = 0, sdAtlasH = 0, sdCols = 1;
    std::vector<uint16_t> pendingTerrain;  // what the tile index shows: baseTerrain with the preview's cells
    bool terrainDirty = false;  // the tile index needs pendingTerrain: all of it (terrainAll), else terrainBox's tiles
    bool terrainAll = true;
    int terrainBox[4] = {};  // (left, top, right, bottom: tiles, right and bottom past the box)
    std::vector<uint16_t> baseTerrain;                      // megatile per map tile, from updateTerrain
    std::vector<std::pair<int, uint16_t>> terrainPreview;  // (tile, megatile) shown instead
    std::vector<std::pair<int, uint16_t>> previewNext;     // (setTerrainPreview's: its memory kept)
    void applyTerrainPreview();
    void terrainCellChanged(int i) {
        const int x = mapW ? i % mapW : 0, y = mapW ? i / mapW : 0;
        if (!terrainDirty) terrainDirty = true, terrainAll = false, terrainBox[0] = x, terrainBox[1] = y, terrainBox[2] = x + 1, terrainBox[3] = y + 1;
        else if (!terrainAll)
            terrainBox[0] = std::min(terrainBox[0], x), terrainBox[1] = std::min(terrainBox[1], y), terrainBox[2] = std::max(terrainBox[2], x + 1),
            terrainBox[3] = std::max(terrainBox[3], y + 1);
    }

    // Sources
    const GameData* data = nullptr;
    UnitSprites* unitSprites = nullptr;
    const Tileset* tileset = nullptr;
    uint16_t era = 0xFFFF, loadedEra = 0xFFFF;
    // (the tileset the atlases were made from: another one of the same era, as a mod's, or one loaded again, makes them
    // again; its address and size, since the editor loads a new tileset into the same object)
    const Tileset* loadedTileset = nullptr;
    size_t loadedTilesetSize = 0;
    GraphicsQuality quality = GraphicsQuality::HD2, loadedQuality = GraphicsQuality::SD;
    bool carbot = false, loadedCarbot = false;
    bool tilesetDirty = true;

    // The images by id: images.dat's, and UnitSprites::extraImage's (from kExtraImages). (A deque: an image's place
    // stays when more come, as draw keeps pointers to them while it asks for others.)
    std::deque<ImageTex> images, extraImages;
    ImageTex& imageSlot(int id) {
        std::deque<ImageTex>& list = id >= UnitSprites::kExtraImages ? extraImages : images;
        const size_t at = size_t(id >= UnitSprites::kExtraImages ? id - UnitSprites::kExtraImages : id);
        if (at >= list.size()) list.resize(at + 1);
        return list[at];
    }
    int imagesResident = 0;  // (the images with a texture or SD frames: Stats)
    // The HD images are read and decoded on a worker (a big one, the creep edges, took 58 ms of a map's first frame):
    // the frame asks for an image, it comes in a later frame (uploaded at the frame's start, takeImages). The images'
    // generation: a change of the art (freeImages) drops what the worker made before it.
    struct ImageJob {
        int id = 0;
        uint64_t generation = 0;
        const GameData* data = nullptr;  // (the job's own: the renderer's may change while it runs)
        GraphicsQuality quality = GraphicsQuality::HD2;
        bool carbot = false;
        std::string extraPath;  // (UnitSprites::extraImage: its GRP's path; its SD frames copied)
        Grp extraSd;
    };
    struct ImageMade {
        int id = 0;
        uint64_t generation = 0;
        bool hd = false;
        AnimImage art;
    };
    std::thread imageWorker;
    std::mutex imageJobsLock;
    std::condition_variable imageJobsReady;
    std::deque<ImageJob> imageJobs;
    std::vector<ImageMade> imagesMade;
    bool imageWorkerStop = false;
    uint64_t imageGeneration = 1;
    bool imagesOffThread() const { return loadBudgetMs < 1e6 && artQuality() != GraphicsQuality::SD && data; }
    void queueImage(int id, ImageTex& t);
    void takeImages(Clock::time_point frameStart);
    void finishImage(ImageTex& t, bool hd, AnimImage& a, int id, int row);
    void stopImageWorker() {
        {
            std::lock_guard<std::mutex> lock(imageJobsLock);
            imageWorkerStop = true, imageJobs.clear();
        }
        imageJobsReady.notify_all();
        if (imageWorker.joinable()) imageWorker.join();
        imageWorkerStop = false, imagesMade.clear();
    }
    ~Impl() { stopImageWorker(); }
    // (setExactTeamColors: an image in a color row's shades; the key: the image's id << 16 | the row)
    std::unordered_map<uint64_t, ImageTex> teamImages;
    // The frame's lists, kept from frame to frame (their memory too).
    struct Raw {
        SDL_GPUTexture* diffuse;
        SDL_GPUTexture* mask;
        SDL_GPUTexture* warp;  // the Warp Texture image (draw function 12), bound in the team mask's place
        int order;  // 0 the terrain's (creep edges), 1 the others, 2 glows, 3 those that bend what is under them
        GpuInstance in;
    };
    struct Batch { SDL_GPUTexture* diffuse; SDL_GPUTexture* mask; SDL_GPUTexture* warp; uint32_t first, count; int order; };
    std::vector<Raw> raw;
    std::vector<uint32_t> rawOrder;  // (raw's indexes by batch)
    std::vector<Batch> batches;
    std::vector<GpuRect> rects;
    uint16_t exactTeam = 0;  // (a bit per owner)
    std::vector<AtlasPage> pages;
    bool pagesFull = false;  // (the next frame starts them again)
    struct PageUpload {
        int page;
        uint32_t x, y, w, h;
        std::vector<uint8_t> rgba, mask;
    };
    std::vector<PageUpload> pageUploads;  // (sent in the frame's own copy pass)
    double loadBudgetMs = 30;
    double frameBudgetMs = 30;  // (this frame's: a map's first frame has less, its terrain shows at once)

    // Dynamic buffers
    SDL_GPUBuffer* instanceBuf = nullptr;
    uint32_t instanceCap = 0;
    SDL_GPUBuffer* rectBuf = nullptr;
    uint32_t rectCap = 0;
    SDL_GPUTransferBuffer* uploadBuf = nullptr;
    uint32_t uploadCap = 0;

    Stats stats;
    std::string error;

    bool createDevice(std::string& err);
    SDL_GPUShaderFormat shaderFormat = SDL_GPU_SHADERFORMAT_INVALID;  // the one the device takes
    bool bcTextures = true;  // HD and HD2 art (BC1, BC3); without them all art is SD
    GraphicsQuality artQuality() const { return bcTextures ? quality : GraphicsQuality::SD; }
    SDL_GPUShader* shader(const ShaderCode& code, SDL_GPUShaderStage stage, const char* entry, uint32_t samplers,
                          uint32_t storageBuffers, uint32_t uniforms);
    bool createPipelines(std::string& err);
    SDL_GPUTexture* createTexture(SDL_GPUTextureFormat fmt, uint32_t w, uint32_t h, uint32_t levels, SDL_GPUTextureUsageFlags usage);
    // Upload `bytes` into mip 0 of `tex` via a temporary transfer buffer in its own command buffer.
    bool uploadTexture(SDL_GPUTexture* tex, uint32_t w, uint32_t h, const void* bytes, size_t size, bool genMips);
    void reloadTileset();
    void buildSdAtlas();
    bool buildHdAtlas();
    // The HD megatiles are read on a worker (76 ms for a 256 x 256 map's tileset, in its first frame before): the SD
    // atlas draws until they are there; then the HD atlas is made from them (finishHdAtlas, at the start of a frame).
    std::future<std::shared_ptr<const HdTiles>> hdReading;
    std::string hdReadError;
    bool finishHdAtlas(const HdTiles& tiles);
    void takeHdTiles(bool wait);
    void ensureDepth(uint32_t w, uint32_t h);
    bool ensureBackdrop(uint32_t w, uint32_t h);
    bool ensureBuffer(SDL_GPUBuffer*& buf, uint32_t& cap, uint32_t bytes);
    bool ensureUpload(uint32_t bytes);
    ImageTex* image(int id, Clock::time_point frameStart, bool& pending);
    void loadImage(int id, ImageTex& t, int row = -1);
    void freeImages();
    bool atlasFrame(ImageTex& t, int id, size_t frame);
    void freePages();
    void sendPageUploads(SDL_GPUCommandBuffer* cmd);
    bool draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32_t w, uint32_t h, const Scene& scene);
};

// --- Setup -------------------------------------------------------------------------------------

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() { shutdown(); }

// The properties of each try: what the shaders ask of the device.
static SDL_PropertiesID deviceProperties() {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);  // (its default is true)
    // The map view uses none of these: GPUs without them can run it too. (Depth clamping stays: the pipelines leave
    // enable_depth_clip off, which asks for it.)
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_METAL_ALLOW_MACFAMILY1_BOOLEAN, true);  // (no sRGB targets)
    return props;
}

bool Renderer::Impl::createDevice(std::string& err) {
    // Windows: Vulkan, else Direct3D 12 on the video card, else Direct3D 12 on WARP (the software device of Windows:
    // slow, but the program starts on video cards that have neither). Vulkan first: an old video card's Direct3D 12
    // (feature level 11_0, as the GeForce GTX 770's) stopped the whole computer (0.8.5). setPreferredDriver (Program
    // options) goes first; the skipped drivers go only when no other is left. STARDRAFT_GPU_DRIVER "direct3d12",
    // "vulkan" or "warp" asks for only that one. Elsewhere SDL picks Metal or Vulkan.
#ifdef STARDRAFT_SHADERS_DXIL
    const char* asked = std::getenv("STARDRAFT_GPU_DRIVER");
    std::string only = asked ? asked : "";
    if (only != "direct3d12" && only != "vulkan" && only != "warp") only.clear();
    // SDL gives the reason a driver cannot be used only as a warning in its log: the error says which they were.
    struct Reasons {
        SDL_LogOutputFunction next = nullptr;
        void* nextData = nullptr;
        SDL_LogPriority shown = SDL_GetLogPriority(SDL_LOG_CATEGORY_GPU);  // (the log shows what it showed before)
        std::string text;
    } reasons;
    SDL_GetLogOutputFunction(&reasons.next, &reasons.nextData);
    if (reasons.shown > SDL_LOG_PRIORITY_WARN) SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_WARN);
    SDL_SetLogOutputFunction(
        [](void* data, int category, SDL_LogPriority priority, const char* message) {
            auto& r = *static_cast<Reasons*>(data);
            if (category == SDL_LOG_CATEGORY_GPU && priority >= SDL_LOG_PRIORITY_WARN) r.text += std::string(r.text.empty() ? "" : ", ") + message;
            if (category != SDL_LOG_CATEGORY_GPU || priority >= r.shown) r.next(r.nextData, category, priority, message);
        },
        &reasons);
    std::string errors;
    std::vector<const char*> order = {"vulkan", "direct3d12", "warp"};
    for (size_t i = 1; i < order.size(); ++i)
        if (preferred == order[i]) std::rotate(order.begin(), order.begin() + std::ptrdiff_t(i), order.begin() + std::ptrdiff_t(i + 1));
    std::vector<const char*> tries;
    for (const char* d : order)
        if (!only.empty() || ("," + skipped + ",").find(std::string(",") + d + ",") == std::string::npos) tries.push_back(d);
    if (tries.empty()) tries = order;
    for (const char* driver : tries) {
        if (!only.empty() && only != driver) continue;
        if (tryHook) tryHook(driver);
        const bool vulkan = std::string(driver) == "vulkan";
        SDL_SetHint("STARDRAFT_D3D12_WARP", std::string(driver) == "warp" ? "1" : "0");  // (cmake/patch_sdl.cmake)
        SDL_PropertiesID props = deviceProperties();
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, !vulkan);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, vulkan);
        SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, vulkan ? "vulkan" : "direct3d12");
        reasons.text.clear();
        device = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
        if (device) {
            tried = driver;
            break;
        }
        errors += std::string(errors.empty() ? "" : "; ") + driver + ": " + (reasons.text.empty() ? SDL_GetError() : reasons.text);
    }
    SDL_SetLogOutputFunction(reasons.next, reasons.nextData);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, reasons.shown);
    SDL_ResetHint("STARDRAFT_D3D12_WARP");
    if (!device) {
        err = "SDL_CreateGPUDevice: " + errors;
        return false;
    }
#else
    SDL_PropertiesID props = deviceProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
    device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (!device) {
        err = std::string("SDL_CreateGPUDevice: ") + SDL_GetError();
        return false;
    }
    tried = SDL_GetGPUDeviceDriver(device);
#endif
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    shaderFormat = formats & SDL_GPU_SHADERFORMAT_DXIL    ? SDL_GPU_SHADERFORMAT_DXIL
                   : formats & SDL_GPU_SHADERFORMAT_SPIRV ? SDL_GPU_SHADERFORMAT_SPIRV
                                                          : SDL_GPU_SHADERFORMAT_MSL;
    bcTextures = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER) &&
                 SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    SDL_GPUSamplerCreateInfo s{};
    s.min_filter = s.mag_filter = SDL_GPU_FILTER_NEAREST;
    s.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    s.address_mode_u = s.address_mode_v = s.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    pointClamp = SDL_CreateGPUSampler(device, &s);
    s.min_filter = s.mag_filter = SDL_GPU_FILTER_LINEAR;
    linearClamp = SDL_CreateGPUSampler(device, &s);
    s.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    s.max_lod = 16;
    linearMip = SDL_CreateGPUSampler(device, &s);
    blackTex = createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    uint32_t black = 0;
    uploadTexture(blackTex, 1, 1, &black, 4, false);
    return pointClamp && linearClamp && linearMip && blackTex;
}

SDL_GPUShader* Renderer::Impl::shader(const ShaderCode& code, SDL_GPUShaderStage stage, const char* entry, uint32_t samplers,
                                      uint32_t storageBuffers, uint32_t uniforms) {
    SDL_GPUShaderCreateInfo si{};
    if (shaderFormat == SDL_GPU_SHADERFORMAT_DXIL) si.code = code.dxil, si.code_size = code.dxilSize;
    else if (shaderFormat == SDL_GPU_SHADERFORMAT_SPIRV) si.code = code.spirv, si.code_size = code.spirvSize;
    else si.code = code.msl, si.code_size = code.mslSize;
    if (!si.code) return nullptr;
    si.entrypoint = entry;  // (the same name in each format)
    si.format = shaderFormat;
    si.stage = stage;
    si.num_samplers = samplers;
    si.num_storage_buffers = storageBuffers;
    si.num_uniform_buffers = uniforms;
    return SDL_CreateGPUShader(device, &si);
}

bool Renderer::Impl::createPipelines(std::string& err) {
    SDL_GPUShader* tvs = shader(STARDRAFT_SHADER(terrain_vs), SDL_GPU_SHADERSTAGE_VERTEX, "VSMain", 0, 0, 0);
    SDL_GPUShader* tps = shader(STARDRAFT_SHADER(terrain_ps), SDL_GPU_SHADERSTAGE_FRAGMENT, "PSMain", 2, 0, 1);
    SDL_GPUShader* svs = shader(STARDRAFT_SHADER(sprite_vs), SDL_GPU_SHADERSTAGE_VERTEX, "VSMain", 0, 1, 1);
    SDL_GPUShader* sps = shader(STARDRAFT_SHADER(sprite_ps), SDL_GPU_SHADERSTAGE_FRAGMENT, "PSMain", 3, 0, 1);
    SDL_GPUShader* rvs = shader(STARDRAFT_SHADER(solid_vs), SDL_GPU_SHADERSTAGE_VERTEX, "VSMain", 0, 1, 1);
    SDL_GPUShader* rps = shader(STARDRAFT_SHADER(solid_ps), SDL_GPU_SHADERSTAGE_FRAGMENT, "PSMain", 0, 0, 0);
    if (!tvs || !tps || !svs || !sps || !rvs || !rps) {
        err = std::string("shader creation failed: ") + SDL_GetError();
        return false;
    }
    SDL_GPUColorTargetDescription color{};
    color.format = colorFormat;
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions = &color;
    pi.target_info.num_color_targets = 1;
    pi.target_info.has_depth_stencil_target = true;
    pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    pi.vertex_shader = tvs;
    pi.fragment_shader = tps;
    terrainPipe = SDL_CreateGPUGraphicsPipeline(device, &pi);

    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state = blend;

    pi.vertex_shader = svs;
    pi.fragment_shader = sps;
    pi.depth_stencil_state.enable_depth_test = true;
    pi.depth_stencil_state.enable_depth_write = true;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    spritePipe = SDL_CreateGPUGraphicsPipeline(device, &pi);
    // Glows: added to what is below (the game's remapped glow draw), depth-tested but not written.
    {
        SDL_GPUColorTargetBlendState add = blend;
        add.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        add.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        add.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        add.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color.blend_state = add;
        pi.depth_stencil_state.enable_depth_write = false;
        glowPipe = SDL_CreateGPUGraphicsPipeline(device, &pi);
        color.blend_state = blend;
        pi.depth_stencil_state.enable_depth_write = true;
    }

    pi.vertex_shader = rvs;
    pi.fragment_shader = rps;
    pi.depth_stencil_state = {};
    solidPipe = SDL_CreateGPUGraphicsPipeline(device, &pi);

    for (auto* s : {tvs, tps, svs, sps, rvs, rps}) SDL_ReleaseGPUShader(device, s);

    SDL_GPUShader* dvs = shader(STARDRAFT_SHADER(downsample_vs), SDL_GPU_SHADERSTAGE_VERTEX, "VSMain", 0, 0, 0);
    SDL_GPUShader* dps = shader(STARDRAFT_SHADER(downsample_ps), SDL_GPU_SHADERSTAGE_FRAGMENT, "PSMain", 1, 0, 1);
    SDL_GPUColorTargetDescription rgba{};
    rgba.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo di{};
    di.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    di.target_info.color_target_descriptions = &rgba;
    di.target_info.num_color_targets = 1;
    di.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    di.vertex_shader = dvs;
    di.fragment_shader = dps;
    downPipe = dvs && dps ? SDL_CreateGPUGraphicsPipeline(device, &di) : nullptr;
    if (dvs) SDL_ReleaseGPUShader(device, dvs);
    if (dps) SDL_ReleaseGPUShader(device, dps);
    if (!terrainPipe || !spritePipe || !glowPipe || !solidPipe || !downPipe) {
        err = std::string("pipeline creation failed: ") + SDL_GetError();
        return false;
    }
    return true;
}

bool Renderer::init(SDL_Window* window, std::string& error) { return createDevice(error) && attach(window, error); }

// The device and the pipelines for the usual window format (B8G8R8A8: what Windows gives an SDR window); attach
// makes the pipelines again only when the window has another.
bool Renderer::createDevice(std::string& error) {
    Impl& r = *impl_;
    if (!r.createDevice(error)) return false;
    r.colorFormat = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    r.pipelinesReady = r.createPipelines(error);
    return true;
}

bool Renderer::attach(SDL_Window* window, std::string& error) {
    Impl& r = *impl_;
    r.window = window;
    if (!SDL_ClaimWindowForGPUDevice(r.device, window)) {
        error = std::string("SDL_ClaimWindowForGPUDevice: ") + SDL_GetError();
        return false;
    }
    SDL_SetGPUSwapchainParameters(r.device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
    const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(r.device, window);
    if (r.pipelinesReady && format == r.colorFormat) return true;
    for (SDL_GPUGraphicsPipeline*& p : {std::ref(r.terrainPipe), std::ref(r.spritePipe), std::ref(r.glowPipe), std::ref(r.solidPipe), std::ref(r.downPipe)})
        if (p) SDL_ReleaseGPUGraphicsPipeline(r.device, p), p = nullptr;
    r.colorFormat = format;
    return r.pipelinesReady = r.createPipelines(error);
}

bool Renderer::initOffscreen(int width, int height, std::string& error) {
    Impl& r = *impl_;
    if (!r.createDevice(error)) return false;
    r.colorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    r.offscreen = r.createTexture(r.colorFormat, width, height, 1, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    r.offscreenW = width, r.offscreenH = height;
    return r.offscreen && r.createPipelines(error);
}

void Renderer::Impl::freeImages() {
    auto release = [&](ImageTex& t) {
        if (t.diffuse) SDL_ReleaseGPUTexture(device, t.diffuse);
        if (t.mask) SDL_ReleaseGPUTexture(device, t.mask);
    };
    for (ImageTex& t : images) release(t);
    for (ImageTex& t : extraImages) release(t);
    for (auto& [key, t] : teamImages) release(t);
    images.clear(), extraImages.clear(), teamImages.clear();
    imagesResident = 0;
    {  // (what the worker makes for the old art: dropped when it comes)
        std::lock_guard<std::mutex> lock(imageJobsLock);
        ++imageGeneration, imageJobs.clear(), imagesMade.clear();
    }
    freePages();
}

// The atlas pages go; the SD frames on them come again as they are drawn.
void Renderer::Impl::freePages() {
    for (AtlasPage& pg : pages) {
        if (pg.diffuse) SDL_ReleaseGPUTexture(device, pg.diffuse);
        if (pg.mask) SDL_ReleaseGPUTexture(device, pg.mask);
    }
    pages.clear(), pageUploads.clear(), pagesFull = false;
    for (auto* list : {&images, &extraImages})
        for (ImageTex& t : *list)
            for (ImageTex::Frame& f : t.frames) f.page = -1;
    for (auto& [key, t] : teamImages)
        for (ImageTex::Frame& f : t.frames) f.page = -1;
}

// Frame `frame` of an SD image on a page (its pixels to the next copy pass); false if no page has room.
bool Renderer::Impl::atlasFrame(ImageTex& t, int id, size_t frame) {
    ImageTex::Frame& out = t.frames[frame];
    if (out.page >= 0) return true;
    const Grp* g = unitSprites ? unitSprites->grp(id) : nullptr;
    if (!g || frame >= g->frames.size() || !tileset) return false;
    const Grp::Frame& fr = g->frames[frame];
    const uint32_t w = uint32_t(fr.width) + 2, h = uint32_t(fr.height) + 2;  // (a clear pixel around it)
    if (w > kAtlasPage || h > kAtlasPage) return false;
    auto fits = [&](AtlasPage& pg) {
        if (pg.x + w > kAtlasPage) pg.y += pg.shelf, pg.x = 0, pg.shelf = 0;
        return pg.y + h <= kAtlasPage;
    };
    if (pages.empty() || !fits(pages.back())) {
        if (pages.size() >= kAtlasPages) {
            pagesFull = true;
            return false;
        }
        AtlasPage pg;
        pg.diffuse = createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, kAtlasPage, kAtlasPage, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        pg.mask = createTexture(SDL_GPU_TEXTUREFORMAT_R8_UNORM, kAtlasPage, kAtlasPage, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if (!pg.diffuse || !pg.mask) {
            if (pg.diffuse) SDL_ReleaseGPUTexture(device, pg.diffuse);
            if (pg.mask) SDL_ReleaseGPUTexture(device, pg.mask);
            return false;
        }
        pages.push_back(pg);
    }
    AtlasPage& pg = pages.back();
    PageUpload up{int(pages.size() - 1), pg.x, pg.y, w, h, std::vector<uint8_t>(size_t(w) * h * 4, 0), std::vector<uint8_t>(size_t(w) * h, 0)};
    pg.x += w, pg.shelf = std::max(pg.shelf, h);
    const std::string* extra = unitSprites->extraPath(id);
    for (int y = 0; y < fr.height; ++y)
        for (int x = 0; x < fr.width; ++x) {
            const uint8_t idx = fr.pixels[size_t(y) * fr.width + x];
            if (!idx) continue;
            bool team = !extra && idx >= 8 && idx < 16;
            // Team pixels: white player's ramp gives a neutral grey the shader can tint. Glows: their
            // colour over black from the tileset's remap table.
            Rgb glow{};
            const bool glowing = unitSprites->glowColor(id, idx, *tileset, glow);
            if (glowing) team = false;
            out.team = out.team || team;
            const Rgb& c = glowing ? glow : tileset->palette[team ? unitSprites->remapRow(idx, t.row >= 0 ? t.row : 6) : idx];
            const size_t o = size_t(y + 1) * w + x + 1;
            up.rgba[o * 4] = c.r, up.rgba[o * 4 + 1] = c.g, up.rgba[o * 4 + 2] = c.b, up.rgba[o * 4 + 3] = 255;
            if (team && t.row < 0) up.mask[o] = 255;  // (a row's own shades: nothing to tint)
        }
    out.u0 = float(up.x + 1), out.v0 = float(up.y + 1), out.u1 = out.u0 + fr.width, out.v1 = out.v0 + fr.height;
    out.page = up.page;
    pageUploads.push_back(std::move(up));
    return true;
}

// The frames that atlasFrame put on the pages since the last call, in one copy pass of `cmd`.
void Renderer::Impl::sendPageUploads(SDL_GPUCommandBuffer* cmd) {
    if (pageUploads.empty()) return;
    // (each block at a multiple of 512: Vulkan takes a copy only from a multiple of the texel's size, and D3D12 copies
    // without a detour from a multiple of 512)
    auto aligned = [](size_t n) { return (n + 511) & ~size_t(511); };
    size_t total = 0;
    for (const PageUpload& u : pageUploads) total += aligned(u.rgba.size()) + aligned(u.mask.size());
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = uint32_t(total);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &ti);
    if (!tb) return;
    uint8_t* p = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device, tb, false));
    size_t at = 0;
    for (const PageUpload& u : pageUploads) {
        std::memcpy(p + at, u.rgba.data(), u.rgba.size()), at += aligned(u.rgba.size());
        std::memcpy(p + at, u.mask.data(), u.mask.size()), at += aligned(u.mask.size());
    }
    SDL_UnmapGPUTransferBuffer(device, tb);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    at = 0;
    for (const PageUpload& u : pageUploads) {
        for (int m = 0; m < 2; ++m) {
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = tb, src.offset = uint32_t(at), src.pixels_per_row = u.w, src.rows_per_layer = u.h;
            SDL_GPUTextureRegion dst{};
            dst.texture = m ? pages[size_t(u.page)].mask : pages[size_t(u.page)].diffuse;
            dst.x = u.x, dst.y = u.y, dst.w = u.w, dst.h = u.h, dst.d = 1;
            SDL_UploadToGPUTexture(copy, &src, &dst, false);
            at += aligned(m ? u.mask.size() : u.rgba.size());
        }
    }
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(device, tb);
    pageUploads.clear();
}

void Renderer::shutdown() {
    Impl& r = *impl_;
    r.stopImageWorker();
    if (!r.device) return;
    if (r.hdReading.valid()) r.hdReading.wait();
    r.hdReading = {};
    SDL_WaitForGPUIdle(r.device);
    r.freeImages();
    for (SDL_GPUTexture* t : {r.offscreen, r.depth, r.backdrop, r.blackTex, r.tileIndex, r.hdAtlas, r.hdDown, r.sdAtlas})
        if (t) SDL_ReleaseGPUTexture(r.device, t);
    for (SDL_GPUBuffer* b : {r.instanceBuf, r.rectBuf})
        if (b) SDL_ReleaseGPUBuffer(r.device, b);
    if (r.uploadBuf) SDL_ReleaseGPUTransferBuffer(r.device, r.uploadBuf);
    for (SDL_GPUSampler* s : {r.pointClamp, r.linearClamp, r.linearMip})
        if (s) SDL_ReleaseGPUSampler(r.device, s);
    for (SDL_GPUGraphicsPipeline* p : {r.terrainPipe, r.spritePipe, r.glowPipe, r.solidPipe, r.downPipe})
        if (p) SDL_ReleaseGPUGraphicsPipeline(r.device, p);
    if (r.window) SDL_ReleaseWindowFromGPUDevice(r.device, r.window);
    SDL_DestroyGPUDevice(r.device);
    impl_ = std::make_unique<Impl>();  // (all as new; a worker and its lock are not copied)
}

// --- Resources ---------------------------------------------------------------------------------

SDL_GPUTexture* Renderer::Impl::createTexture(SDL_GPUTextureFormat fmt, uint32_t w, uint32_t h, uint32_t levels,
                                              SDL_GPUTextureUsageFlags usage) {
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = fmt;
    ti.usage = usage;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = levels;
    return SDL_CreateGPUTexture(device, &ti);
}

bool Renderer::Impl::uploadTexture(SDL_GPUTexture* tex, uint32_t w, uint32_t h, const void* bytes, size_t size, bool genMips) {
    if (!tex) return false;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = uint32_t(size);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &ti);
    if (!tb) return false;
    void* p = SDL_MapGPUTransferBuffer(device, tb, false);
    std::memcpy(p, bytes, size);
    SDL_UnmapGPUTransferBuffer(device, tb);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    SDL_GPUTextureRegion dst{};
    dst.texture = tex;
    dst.w = w;
    dst.h = h;
    dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    if (genMips) SDL_GenerateMipmapsForGPUTexture(cmd, tex);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, tb);
    return true;
}

void Renderer::Impl::buildSdAtlas() {
    if (sdAtlas) SDL_ReleaseGPUTexture(device, sdAtlas), sdAtlas = nullptr;
    const Tileset& ts = *tileset;
    size_t n = ts.megatiles.size();
    uint32_t cols = uint32_t(std::ceil(std::sqrt(double(n))));
    uint32_t rows = uint32_t((n + cols - 1) / cols);
    uint32_t w = cols * 32, h = rows * 32;
    std::vector<uint8_t> px(size_t(w) * h * 4, 0);
    for (size_t m = 0; m < n; ++m) {
        uint32_t ox = uint32_t(m % cols) * 32, oy = uint32_t(m / cols) * 32;
        for (int k = 0; k < 16; ++k) {
            uint32_t ref = ts.megatiles[m][k];
            size_t mini = ref >> 1;
            bool flip = ref & 1;
            if (mini >= ts.minitiles.size()) continue;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const Rgb& c = ts.palette[ts.minitiles[mini][y * 8 + (flip ? 7 - x : x)]];
                    uint8_t* d = &px[(size_t(oy + (k / 4) * 8 + y) * w + ox + (k % 4) * 8 + x) * 4];
                    d[0] = c.r, d[1] = c.g, d[2] = c.b, d[3] = 255;
                }
        }
    }
    sdAtlas = createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, w, h, mipCount(w, h),
                            SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    uploadTexture(sdAtlas, w, h, px.data(), px.size(), true);
    sdAtlasW = float(w), sdAtlasH = float(h), sdCols = float(cols);
}

bool Renderer::Impl::buildHdAtlas() {
    if (hdAtlas) SDL_ReleaseGPUTexture(device, hdAtlas), hdAtlas = nullptr;
    if (hdDown) SDL_ReleaseGPUTexture(device, hdDown), hdDown = nullptr;
    if (hdReading.valid()) hdReading.wait();  // (another tileset's read that is still running: its result is not used)
    hdReading = {};
    if (artQuality() == GraphicsQuality::SD) return true;
    // (the game data reads under its own lock: a worker may read while the program reads its sprites)
    const GameData* d = data;
    const uint16_t e = era;
    const GraphicsQuality q = artQuality();
    const bool cb = carbot;
    hdReading = std::async(std::launch::async, [this, d, e, q, cb] { return sharedHdTiles(*d, e, q, cb, &hdReadError, nullptr, true); });
    takeHdTiles(loadBudgetMs >= 1e6);  // (a picture of the whole map, the map image: it waits for them)
    return true;
}

// The worker's HD megatiles, when they are there (`wait`: until they are): the HD atlas from them.
void Renderer::Impl::takeHdTiles(bool wait) {
    if (!hdReading.valid()) return;
    if (!wait && hdReading.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const std::shared_ptr<const HdTiles> held = hdReading.get();
    if (!held) error = hdReadError;
    else finishHdAtlas(*held);
}

bool Renderer::Impl::finishHdAtlas(const HdTiles& tiles) {
    // Lay megatiles out in a square grid; copy whole 4x4 blocks (no decoding).
    const uint32_t tile = uint32_t(tiles.tileSize), blocks = tile / 4, blockBytes = tiles.format == DdsTexture::BC1 ? 8 : 16;
    size_t n = tiles.tiles.size();
    uint32_t cols = uint32_t(std::ceil(std::sqrt(double(n))));
    uint32_t rows = uint32_t((n + cols - 1) / cols);
    uint32_t w = cols * tile, h = rows * tile;
    size_t rowBytes = size_t(w / 4) * blockBytes;
    std::vector<uint8_t> atlas(rowBytes * (h / 4), 0);
    for (size_t t = 0; t < n; ++t) {
        uint32_t bx = uint32_t(t % cols) * blocks, by = uint32_t(t / cols) * blocks;
        const Bytes& src = tiles.tiles[t];
        for (uint32_t r = 0; r < blocks && (r + 1) * blocks * blockBytes <= src.size(); ++r)
            std::memcpy(&atlas[(by + r) * rowBytes + bx * blockBytes], &src[r * blocks * blockBytes], blocks * blockBytes);
    }
    SDL_GPUTextureFormat fmt = tiles.format == DdsTexture::BC1 ? SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM : SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    hdAtlas = createTexture(fmt, w, h, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if (!uploadTexture(hdAtlas, w, h, atlas.data(), atlas.size(), false)) return false;
    hdAtlasW = float(w), hdAtlasH = float(h), hdTile = float(tile), hdCols = float(cols);

    // Downsample to 32 px per megatile on the GPU, then mipmap, for zoomed-out views.
    uint32_t dw = cols * 32, dh = rows * 32;
    hdDown = createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, dw, dh, mipCount(dw, dh),
                           SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    if (hdDown) {
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUColorTargetInfo ct{};
        ct.texture = hdDown;
        ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, downPipe);
        SDL_GPUTextureSamplerBinding b{hdAtlas, pointClamp};
        SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);
        float u[4] = {float(tile / 32), 0, 0, 0};
        SDL_PushGPUFragmentUniformData(cmd, 0, u, sizeof u);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        SDL_GenerateMipmapsForGPUTexture(cmd, hdDown);
        SDL_SubmitGPUCommandBuffer(cmd);
        hdDownW = float(dw), hdDownH = float(dh);
    }
    return true;
}

void Renderer::Impl::reloadTileset() {
    if (!tileset || !data || tileset->groups.empty()) return;
    const size_t size = tileset->groups.size() * 1000003u + tileset->megatiles.size() * 1009u + tileset->minitiles.size();
    const bool other = era != loadedEra || tileset != loadedTileset || size != loadedTilesetSize;
    if (other) buildSdAtlas();
    if (other || quality != loadedQuality || carbot != loadedCarbot) buildHdAtlas();
    loadedTileset = tileset, loadedTilesetSize = size;
    if (quality != loadedQuality || carbot != loadedCarbot) freeImages();
    loadedEra = era;
    loadedQuality = quality;
    loadedCarbot = carbot;
    tilesetDirty = false;
    terrainDirty = !pendingTerrain.empty(), terrainAll = true;
}

void Renderer::setQuality(GraphicsQuality q) {
    if (impl_->quality != q) impl_->quality = q, impl_->tilesetDirty = true;
}
GraphicsQuality Renderer::quality() const { return impl_->quality; }
bool Renderer::hdArt() const { return impl_->bcTextures; }
const char* Renderer::driver() const { return impl_->device ? SDL_GetGPUDeviceDriver(impl_->device) : ""; }
void Renderer::setSkippedDrivers(const std::string& drivers) { impl_->skipped = drivers; }
const char* Renderer::driverTried() const { return impl_->tried; }
void Renderer::setTryHook(void (*hook)(const char*)) { impl_->tryHook = hook; }
void Renderer::setPreferredDriver(const std::string& driver) { impl_->preferred = driver; }

void Renderer::setCarbot(bool carbot) {
    if (impl_->carbot != carbot) impl_->carbot = carbot, impl_->tilesetDirty = true;
}
void Renderer::setLoadBudget(double ms) { impl_->loadBudgetMs = ms; }
void Renderer::setExactTeamColors(uint16_t owners) { impl_->exactTeam = owners; }
Stats Renderer::stats() const { return impl_->stats; }

void Renderer::setPlayerColors(const uint32_t rgb[12]) {
    for (int i = 0; i < 12; ++i)
        for (int c = 0; c < 3; ++c) kPlayerRgb[i][c] = float((rgb[i] >> (16 - 8 * c)) & 255) / 255.0f;
}

void Renderer::setSources(const GameData* data, UnitSprites* sprites, const Tileset* tileset, uint16_t era) {
    Impl& r = *impl_;
    const size_t size = tileset ? tileset->groups.size() * 1000003u + tileset->megatiles.size() * 1009u + tileset->minitiles.size() : 0;
    if (r.tileset != tileset || r.era != era || (tileset && size != r.loadedTilesetSize)) r.tilesetDirty = true;
    if (r.unitSprites != sprites) r.freeImages();
    r.data = data, r.unitSprites = sprites, r.tileset = tileset, r.era = era;
}

void Renderer::reloadImages() { impl_->freeImages(); }

void Renderer::releaseMapData() {
    Impl& r = *impl_;
    if (!r.device) return;
    if (r.hdReading.valid()) r.hdReading.wait();
    r.hdReading = {};
    r.freeImages();
    for (SDL_GPUTexture** t : {&r.sdAtlas, &r.hdAtlas, &r.hdDown, &r.tileIndex})
        if (*t) SDL_ReleaseGPUTexture(r.device, *t), *t = nullptr;
    r.tileIndexW = r.tileIndexH = 0;
    r.data = nullptr, r.unitSprites = nullptr, r.tileset = nullptr, r.era = 0xFFFF;
    r.loadedTileset = nullptr, r.loadedEra = 0xFFFF, r.loadedTilesetSize = 0, r.tilesetDirty = true;
    r.baseTerrain.clear(), r.baseTerrain.shrink_to_fit(), r.pendingTerrain.clear(), r.pendingTerrain.shrink_to_fit();
    r.terrainPreview.clear(), r.previewNext.clear(), r.terrainDirty = false;
    r.mapW = r.mapH = 0, r.realW = r.realH = 0, r.originX = r.originY = 0;
}

void Renderer::updateTerrain(const TerrainLayer& mtxm) { updateTerrainExtended(mtxm, 0, 0, mtxm.width, mtxm.height); }

void Renderer::updateTerrainExtended(const TerrainLayer& mtxm, int originX, int originY, int mapW, int mapH) {
    Impl& r = *impl_;
    if (r.originX != originX || r.originY != originY) r.terrainPreview.clear();  // (its indices are of the old grid)
    r.mapW = mtxm.width, r.mapH = mtxm.height;
    r.originX = originX, r.originY = originY, r.realW = uint16_t(mapW), r.realH = uint16_t(mapH);
    r.baseTerrain.resize(mtxm.tiles.size());
    for (size_t i = 0; i < mtxm.tiles.size(); ++i) {
        int mega = r.tileset ? r.tileset->megatileFor(mtxm.tiles[i]) : -1;
        r.baseTerrain[i] = mega < 0 ? 0xFFFF : uint16_t(mega);
    }
    r.applyTerrainPreview();
}

void Renderer::Impl::applyTerrainPreview() {
    pendingTerrain = baseTerrain;
    for (auto [i, mega] : terrainPreview)
        if (i >= 0 && size_t(i) < pendingTerrain.size()) pendingTerrain[size_t(i)] = mega;
    terrainDirty = !pendingTerrain.empty(), terrainAll = true;
}

// A new preview: its old cells show the terrain again and its new cells the preview; only the box of these cells goes
// to the tile index (not the whole map at each move of a doodad or a shape).
void Renderer::setTerrainPreview(const std::vector<std::pair<int, uint16_t>>& cells) {
    Impl& r = *impl_;
    std::vector<std::pair<int, uint16_t>>& megas = r.previewNext;
    megas.clear();
    const bool shifted = r.originX || r.originY || r.realW != r.mapW;
    for (auto [i, tile] : cells) {
        const int mega = r.tileset ? r.tileset->megatileFor(tile) : -1;
        if (mega <= 0) continue;
        if (shifted) {  // (the map's index: the cell's index in the larger grid)
            if (!r.realW || i < 0) continue;
            i = (i / r.realW + r.originY) * r.mapW + i % r.realW + r.originX;
        }
        megas.push_back({i, uint16_t(mega)});
    }
    if (megas == r.terrainPreview) return;
    std::swap(r.terrainPreview, megas);
    if (r.pendingTerrain.size() != r.baseTerrain.size()) return r.applyTerrainPreview();
    const size_t n = r.pendingTerrain.size();
    for (auto [i, mega] : megas)  // (the old preview)
        if (i >= 0 && size_t(i) < n) r.pendingTerrain[size_t(i)] = r.baseTerrain[size_t(i)], r.terrainCellChanged(i);
    for (auto [i, mega] : r.terrainPreview)
        if (i >= 0 && size_t(i) < n) r.pendingTerrain[size_t(i)] = mega, r.terrainCellChanged(i);
}

void Renderer::Impl::ensureDepth(uint32_t w, uint32_t h) {
    if (depth && depthW == w && depthH == h) return;
    if (depth) SDL_ReleaseGPUTexture(device, depth);
    depth = createTexture(SDL_GPU_TEXTUREFORMAT_D16_UNORM, w, h, 1, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    depthW = w, depthH = h;
}

bool Renderer::Impl::ensureBackdrop(uint32_t w, uint32_t h) {
    if (backdrop && backdropW == w && backdropH == h && backdropFormat == colorFormat) return true;
    if (backdrop) SDL_ReleaseGPUTexture(device, backdrop);
    backdrop = createTexture(colorFormat, w, h, 1, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    backdropW = w, backdropH = h, backdropFormat = colorFormat;
    return backdrop != nullptr;
}

bool Renderer::Impl::ensureBuffer(SDL_GPUBuffer*& buf, uint32_t& cap, uint32_t bytes) {
    if (buf && cap >= bytes) return true;
    if (buf) SDL_ReleaseGPUBuffer(device, buf);
    cap = std::max<uint32_t>(bytes, std::max<uint32_t>(cap * 2, 64 * 1024));
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bi.size = cap;
    buf = SDL_CreateGPUBuffer(device, &bi);
    return buf != nullptr;
}

bool Renderer::Impl::ensureUpload(uint32_t bytes) {
    if (uploadBuf && uploadCap >= bytes) return true;
    if (uploadBuf) SDL_ReleaseGPUTransferBuffer(device, uploadBuf);
    uploadCap = std::max<uint32_t>(bytes, std::max<uint32_t>(uploadCap * 2, 256 * 1024));
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = uploadCap;
    uploadBuf = SDL_CreateGPUTransferBuffer(device, &ti);
    return uploadBuf != nullptr;
}

// --- Sprite images -----------------------------------------------------------------------------

void Renderer::Impl::loadImage(int id, ImageTex& t, int row) {
    t.loaded = true;
    std::string err;
    AnimImage a;
    // An extra image (UnitSprites::extraImage: the creep edges): its HD art from its .dds.grp, or its GRP in SD.
    const std::string* extra = unitSprites ? unitSprites->extraPath(id) : nullptr;
    const Grp* extraSd = extra ? unitSprites->grp(id) : nullptr;
    const bool hd = artQuality() != GraphicsQuality::SD && data &&
                    (extra ? extraSd && loadHdGrp(*data, *extra, *extraSd, artQuality(), a, err, carbot)
                           : loadAnimImage(*data, id, artQuality(), a, err, carbot));
    finishImage(t, hd, a, id, row);
}

// An image's textures (HD) or its SD frames' places (the SD art, or no HD art for it).
void Renderer::Impl::finishImage(ImageTex& t, bool hd, AnimImage& a, int id, int row) {
    t.loaded = true;
    if (hd) {
        auto fmt = [](const DdsTexture& d) {
            return d.format == DdsTexture::BC1 ? SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM : SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
        };
        t.diffuse = createTexture(fmt(a.diffuse), a.diffuse.width, a.diffuse.height, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        uploadTexture(t.diffuse, a.diffuse.width, a.diffuse.height, a.diffuse.data.data(), a.diffuse.data.size(), false);
        if (!a.teamColor.data.empty()) {
            t.mask = createTexture(fmt(a.teamColor), a.teamColor.width, a.teamColor.height, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
            uploadTexture(t.mask, a.teamColor.width, a.teamColor.height, a.teamColor.data.data(), a.teamColor.data.size(), false);
        }
        t.texW = float(a.diffuse.width), t.texH = float(a.diffuse.height);
        // Full-HD pixels are 1/4 of a map pixel.
        t.boxW = a.boxWidth / 4.0f, t.boxH = a.boxHeight / 4.0f;
        for (const auto& f : a.frames)
            t.frames.push_back({f.x * a.texScale, f.y * a.texScale, (f.x + f.width) * a.texScale, (f.y + f.height) * a.texScale,
                                f.xOff / 4.0f, f.yOff / 4.0f, f.width / 4.0f, f.height / 4.0f});
        return;
    }
    // SD fallback: classic GRP frames on the atlas pages (team pixels as a grey ramp + mask).
    const Grp* g = unitSprites ? unitSprites->grp(id) : nullptr;
    if (!g || g->frames.empty() || !tileset) {
        t.failed = true;
        return;
    }
    // (the frames' places in the box; their pixels go to the pages as they are drawn: atlasFrame)
    t.sd = true, t.row = row;
    t.texW = t.texH = float(kAtlasPage);
    t.boxW = g->width, t.boxH = g->height;
    for (const Grp::Frame& fr : g->frames)
        t.frames.push_back({0, 0, 0, 0, float(fr.x), float(fr.y), float(fr.width), float(fr.height)});
}

// The worker's next image: the HD art read through the game data's own handle for workers.
void Renderer::Impl::queueImage(int id, ImageTex& t) {
    ImageJob job;
    job.id = id, job.data = data, job.quality = artQuality(), job.carbot = carbot;
    if (const std::string* extra = unitSprites ? unitSprites->extraPath(id) : nullptr) {
        const Grp* sd = unitSprites->grp(id);
        if (!sd) return loadImage(id, t);  // (no SD frames: nothing to read)
        job.extraPath = *extra, job.extraSd = *sd;
    }
    t.queued = true;
    {
        std::lock_guard<std::mutex> lock(imageJobsLock);
        job.generation = imageGeneration;
        imageJobs.push_back(std::move(job));
    }
    if (!imageWorker.joinable())
        imageWorker = std::thread([this] {
            for (;;) {
                ImageJob job;
                {
                    std::unique_lock<std::mutex> lock(imageJobsLock);
                    imageJobsReady.wait(lock, [&] { return imageWorkerStop || !imageJobs.empty(); });
                    if (imageWorkerStop) return;
                    job = std::move(imageJobs.front());
                    imageJobs.pop_front();
                }
                const GameData* d = job.data;
                ImageMade made;
                made.id = job.id, made.generation = job.generation;
                std::string err;
                made.hd = d && (job.extraPath.empty() ? loadAnimImage(*d, job.id, job.quality, made.art, err, job.carbot, true)
                                                      : loadHdGrp(*d, job.extraPath, job.extraSd, job.quality, made.art, err, job.carbot, true));
                std::lock_guard<std::mutex> lock(imageJobsLock);
                if (made.generation == imageGeneration) imagesMade.push_back(std::move(made));
            }
        });
    imageJobsReady.notify_one();
}

// The worker's images of this art: their textures, while the frame has time (the rest at the next frame).
void Renderer::Impl::takeImages(Clock::time_point frameStart) {
    std::vector<ImageMade> made;
    {
        std::lock_guard<std::mutex> lock(imageJobsLock);
        made.swap(imagesMade);
    }
    size_t i = 0;
    for (; i < made.size() && (i == 0 || msSince(frameStart) < frameBudgetMs); ++i) {
        if (made[i].generation != imageGeneration) continue;
        ImageTex& t = imageSlot(made[i].id);
        if (t.loaded) continue;
        finishImage(t, made[i].hd, made[i].art, made[i].id, -1);
        imagesResident += t.diffuse != nullptr || t.sd;
    }
    if (i < made.size()) {
        std::lock_guard<std::mutex> lock(imageJobsLock);
        imagesMade.insert(imagesMade.begin(), std::make_move_iterator(made.begin() + std::ptrdiff_t(i)), std::make_move_iterator(made.end()));
    }
}

ImageTex* Renderer::Impl::image(int id, Clock::time_point frameStart, bool& pending) {
    pending = false;
    if (id < 0) return nullptr;
    ImageTex& t = imageSlot(id);
    if (!t.loaded && imagesOffThread()) {  // (the worker reads it; a later frame shows it)
        if (!t.queued) queueImage(id, t);
        if (!t.loaded) return pending = true, nullptr;
    }
    if (!t.loaded) {
        if (msSince(frameStart) > frameBudgetMs) {
            pending = true;
            return nullptr;
        }
        loadImage(id, t);
        imagesResident += t.diffuse != nullptr || t.sd;
    }
    return t.failed || (!t.diffuse && !t.sd) ? nullptr : &t;
}

// --- Frame ---------------------------------------------------------------------------------------

bool Renderer::Impl::draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32_t w, uint32_t h, const Scene& scene) {
    auto frameStart = Clock::now();
    stats = {};
    if (pagesFull) freePages();
    frameBudgetMs = loadBudgetMs;
    if (tilesetDirty) {  // (a map's first frame: the terrain at once, the sprites from the next frames)
        reloadTileset();
        if (loadBudgetMs < 1e6) frameStart = Clock::now(), frameBudgetMs = std::min(loadBudgetMs, 8.0);
    }
    takeHdTiles(loadBudgetMs >= 1e6);
    takeImages(frameStart);
    if (hdReading.valid()) ++stats.imagesPending;  // (the HD tiles still reading: the program paints again, as for the sprites)
    ensureDepth(w, h);
    const Camera& cam = scene.camera;
    const float mapPxH = std::max<float>((realH ? realH : mapH) * 32.0f, 1);

    // Build sprite instances bucketed by image (one draw per image; depth handles the order).
    raw.clear();
    raw.reserve(scene.sprites.size());
    const float viewL = cam.x, viewT = cam.y, viewR = cam.x + w / cam.zoom, viewB = cam.y + h / cam.zoom;
    for (const SpriteDraw& s : scene.sprites) {
        const int df = s.drawFunction;
        if (df == 11) continue;  // the health bar: not part of the picture
        bool pending = false;
        ImageTex* t = image(s.image, frameStart, pending);
        if (pending) ++stats.imagesPending;
        if (!t || t->frames.empty()) continue;
        // The Warp Texture image (images.dat 210) for draw function 12.
        ImageTex* warp = nullptr;
        if (df == 12) {
            warp = image(210, frameStart, pending);
            if (pending) ++stats.imagesPending;
            if (!warp || warp->frames.empty()) continue;
        }
        size_t warpFrame = 0;  // (the Warp Texture's frame: the level)
        if (warp) {
            warpFrame = size_t(std::clamp(s.level, 0, int(warp->frames.size()) - 1));
            if (warp->sd && warp->frames[warpFrame].page < 0 && (msSince(frameStart) > frameBudgetMs || !atlasFrame(*warp, 210, warpFrame))) {
                ++stats.imagesPending;
                continue;
            }
        }
        const size_t fi = size_t(s.frame) < t->frames.size() ? size_t(s.frame) : 0;
        const ImageTex::Frame& f = t->frames[fi];
        // On the map's pixel grid, as the game draws: whole-pixel origin and integer box halves, so no
        // sprite lands between pixels (visible with nearest-neighbour sampling).
        const float ox = std::floor(s.x), oy = std::floor(s.y);
        float boxL = ox - std::floor(t->boxW / 2.0f), boxT = oy - std::floor(t->boxH / 2.0f);
        float left = std::round(s.flip ? ox + (ox - (boxL + f.x + f.w)) : boxL + f.x);
        float top = std::round(boxT + f.y);
        if (left > viewR || top > viewB || left + f.w < viewL || top + f.h < viewT) continue;  // off screen
        // An SD frame on its page: within the frame's load budget, as image().
        auto onPage = [&](ImageTex& im, int id, size_t k) {
            if (im.frames[k].page >= 0) return true;
            if (msSince(frameStart) > frameBudgetMs) return ++stats.imagesPending, false;
            return atlasFrame(im, id, k);
        };
        if (t->sd && !onPage(*t, s.image, fi)) continue;
        // The owner's color row's own shades (the same GRP: the same box and frames, other places on the pages).
        if ((exactTeam >> std::min<int>(s.owner, 11) & 1) && t->sd && t->frames[fi].team && unitSprites && !s.ground && !s.terrain) {
            const int row = unitSprites->colorRow(s.owner);
            ImageTex& e = teamImages[uint64_t(uint32_t(s.image)) << 16 | uint16_t(row)];
            if (!e.loaded) loadImage(s.image, e, row);
            // (the tinted picture until it is there)
            if (!e.failed && e.frames.size() == t->frames.size() && onPage(e, s.image, fi)) t = &e;
        }
        const ImageTex::Frame& uf = t->frames[fi];  // (its place in the texture drawn)
        GpuInstance in{};
        in.pos[0] = left, in.pos[1] = top;
        in.size[0] = f.w, in.size[1] = f.h;
        float u0 = uf.u0 / t->texW, u1 = uf.u1 / t->texW;
        in.uv[0] = s.flip ? u1 : u0, in.uv[1] = uf.v0 / t->texH, in.uv[2] = s.flip ? u0 : u1, in.uv[3] = uf.v1 / t->texH;
        const float* rgb = kPlayerRgb[std::min<int>(s.owner, 11)];
        in.color[0] = rgb[0], in.color[1] = rgb[1], in.color[2] = rgb[2], in.color[3] = s.alpha;
        float yn = std::clamp((s.hasSortY ? s.sortY : s.y) / mapPxH, 0.0f, 1.0f);
        in.depth = std::clamp((s.air ? 0.2f - 0.15f * yn : 0.95f - 0.7f * yn) - s.depthBias, 0.0f, 1.0f);
        const bool shadow = s.shadow || df == 10, glow = s.glow || df == 9;
        if (shadow) in.depth = 0.99f;  // on the ground: never covers a unit
        if (s.ground) {
            in.depth = 0.985f;  // over shadows, under every unit
            in.color[0] = 0.2f, in.color[1] = 1.0f, in.color[2] = 0.2f;  // the game's selection green
        }
        if (s.terrain) in.depth = 0.995f;  // on the terrain, under shadows
        // The draw functions that change how it looks (as data_pictures.inc's compositeLookHd draws them).
        const bool cloak = df >= 2 && df <= 7, emp = df == 8;
        int look = 0;
        if (!shadow && !glow && !s.ground && !s.terrain) {
            look = cloak ? 16 : emp ? 32 : df == 12 ? 64 : df == 16 ? 128 : df == 17 ? 256 : 0;
            // Cloaking and decloaking go through 0..8 (1: cloaked); 3 and 6 are cloaked.
            if (cloak) in.level = df == 3 || df == 6 || s.level < 0 ? 1.0f : std::clamp(s.level, 0, 8) / 8.0f;
            // The warp flash: white at 48, then less as it goes to 63.
            if (df == 17) in.level = 1.0f - std::clamp(((s.level < 0 ? 48 : s.level) - 48) / 15.0f, 0.0f, 1.0f);
            if (df == 12) {
                // The Warp Texture's frame (the level) at the same place as the sprite's frame: from its top-left.
                const ImageTex::Frame& wf = warp->frames[warpFrame];
                const float perX = wf.w > 0 ? (wf.u1 - wf.u0) / wf.w : 1, perY = wf.h > 0 ? (wf.v1 - wf.v0) / wf.h : 1;
                in.warpUv[0] = wf.u0 / warp->texW, in.warpUv[1] = wf.v0 / warp->texH;
                in.warpUv[2] = (wf.u0 + f.w * perX) / warp->texW, in.warpUv[3] = (wf.v0 + f.h * perY) / warp->texH;
                in.warpLim[0] = wf.u0 / warp->texW, in.warpLim[1] = wf.v0 / warp->texH;
                in.warpLim[2] = wf.u1 / warp->texW, in.warpLim[3] = wf.v1 / warp->texH;
            }
        }
        SDL_GPUTexture* warpTex = look != 64 || !warp ? nullptr : warp->sd ? pages[size_t(warp->frames[warpFrame].page)].diffuse : warp->diffuse;
        in.flags = float((shadow ? 1 : 0) | (s.tintRed ? 2 : 0) | (s.ground ? 4 : 0) | (glow ? 8 : 0) | look);
        SDL_GPUTexture* diffuse = t->sd ? pages[size_t(uf.page)].diffuse : t->diffuse;
        SDL_GPUTexture* mask = t->sd ? (t->row < 0 ? pages[size_t(uf.page)].mask : nullptr) : t->mask;
        raw.push_back({diffuse, mask, warpTex, s.terrain ? 0 : glow ? 2 : (look == 16 || look == 32) ? 3 : 1, in});
    }
    sendPageUploads(cmd);
    // The terrain's sprites first (else the depth that they write would hide the shadows and circles over them), then
    // the others, then the sprites that bend what is under them (cloaks, the EMP shockwave: they read the picture of the
    // others), then glows in their own batches (additive pipeline; drawn last, as they write no depth).
    // (raw's indexes sorted by batch, in the order the sprites came within one; the instances go to the upload in this
    // order)
    rawOrder.resize(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) rawOrder[i] = uint32_t(i);
    auto key = [&](uint32_t i) { return std::make_tuple(raw[i].order, raw[i].diffuse, raw[i].mask, raw[i].warp, i); };
    std::sort(rawOrder.begin(), rawOrder.end(), [&](uint32_t a, uint32_t b) { return key(a) < key(b); });
    batches.clear();
    for (uint32_t k = 0; k < uint32_t(rawOrder.size()); ++k) {
        const Raw& r = raw[rawOrder[k]];
        if (batches.empty() || batches.back().order != r.order || batches.back().diffuse != r.diffuse || batches.back().mask != r.mask ||
            batches.back().warp != r.warp)
            batches.push_back({r.diffuse, r.mask, r.warp, k, 0, r.order});
        ++batches.back().count;
    }
    // Only a scene with such sprites draws into the backdrop first (and then copies it to the target).
    const bool bends = !batches.empty() && batches.back().order == 3 && ensureBackdrop(w, h);
    stats.sprites = int(raw.size());

    // Overlays -> screen-space rects (outlines become 4 thin rects, text becomes glyph quads).
    rects.clear();
    auto toScreen = [&](float x, float y, bool world, float& sx, float& sy) {
        sx = world ? (x - cam.x) * cam.zoom : x;
        sy = world ? (y - cam.y) * cam.zoom : y;
    };
    auto color = [](uint32_t rgba, float* out) {
        out[0] = ((rgba >> 24) & 255) / 255.0f, out[1] = ((rgba >> 16) & 255) / 255.0f;
        out[2] = ((rgba >> 8) & 255) / 255.0f, out[3] = (rgba & 255) / 255.0f;
    };
    auto pushRect = [&](float x0, float y0, float x1, float y1, uint32_t rgba) {
        GpuRect r{};
        r.rect[0] = x0, r.rect[1] = y0, r.rect[2] = x1, r.rect[3] = y1;
        color(rgba, r.color);
        rects.push_back(r);
    };
    for (const RectDraw& rd : scene.rects) {
        if (rd.line) {
            GpuRect r{};
            toScreen(rd.x0, rd.y0, rd.world, r.rect[0], r.rect[1]);
            toScreen(rd.x1, rd.y1, rd.world, r.rect[2], r.rect[3]);
            if (std::max(r.rect[0], r.rect[2]) < 0 || std::max(r.rect[1], r.rect[3]) < 0 || std::min(r.rect[0], r.rect[2]) > w ||
                std::min(r.rect[1], r.rect[3]) > h)
                continue;
            color(rd.rgba, r.color);
            r.line[0] = std::max(rd.thickness, 0.5f);
            rects.push_back(r);
            continue;
        }
        float x0, y0, x1, y1;
        toScreen(std::min(rd.x0, rd.x1), std::min(rd.y0, rd.y1), rd.world, x0, y0);
        toScreen(std::max(rd.x0, rd.x1), std::max(rd.y0, rd.y1), rd.world, x1, y1);
        if (x1 < 0 || y1 < 0 || x0 > w || y0 > h) continue;
        if (rd.fill) {
            pushRect(x0, y0, x1, y1, rd.rgba);
        } else {
            float t = rd.thickness;
            pushRect(x0, y0, x1, y0 + t, rd.rgba);
            pushRect(x0, y1 - t, x1, y1, rd.rgba);
            pushRect(x0, y0 + t, x0 + t, y1 - t, rd.rgba);
            pushRect(x1 - t, y0 + t, x1, y1 - t, rd.rgba);
        }
    }
    static std::vector<char> glyphBuf(1 << 18);  // (textWidth: stb_easy_font_width)
    for (const TextDraw& td : scene.texts) {
        float sx, sy;
        toScreen(td.x, td.y, td.world, sx, sy);
        if (sx > w || sy > h) continue;
        char* text = const_cast<char*>(td.text.c_str());
        if (sx + (stb_easy_font_width(text) + 1) * td.scale + 1 < 0 || sy + (stb_easy_font_height(text) + 1) * td.scale + 1 < 0) continue;  // (and its shadow)
        int quads = stb_easy_font_print(0, 0, const_cast<char*>(td.text.c_str()), nullptr, glyphBuf.data(), int(glyphBuf.size()));
        // Shadow first, then the text.
        for (int pass = 0; pass < 2; ++pass)
            for (int q = 0; q < quads; ++q) {
                const float* v = reinterpret_cast<const float*>(glyphBuf.data() + q * 64);  // 4 vertices of 16 bytes
                float x0 = v[0], y0 = v[1], x1 = v[8], y1 = v[9];
                float off = pass == 0 ? 1.0f : 0.0f;
                pushRect(sx + x0 * td.scale + off, sy + y0 * td.scale + off, sx + x1 * td.scale + off, sy + y1 * td.scale + off,
                         pass == 0 ? 0x000000C0u : td.rgba);
            }
    }

    // Uploads: terrain indices (if changed), instances, rects.
    uint32_t instBytes = uint32_t(raw.size() * sizeof(GpuInstance));
    uint32_t rectBytes = uint32_t(rects.size() * sizeof(GpuRect));
    if (terrainDirty && (!tileIndex || tileIndexW != mapW || tileIndexH != mapH)) {  // the map's size changed
        if (tileIndex) SDL_ReleaseGPUTexture(device, tileIndex);
        tileIndex = createTexture(SDL_GPU_TEXTUREFORMAT_R16_UINT, mapW, mapH, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        tileIndexW = mapW, tileIndexH = mapH;
        terrainAll = true;
    }
    // (the whole map, or only the box of the tiles that changed: a preview's cells)
    const bool part = terrainDirty && !terrainAll && pendingTerrain.size() == size_t(mapW) * mapH;
    const int tx0 = part ? std::clamp(terrainBox[0], 0, int(mapW)) : 0, ty0 = part ? std::clamp(terrainBox[1], 0, int(mapH)) : 0;
    const int tw = part ? std::clamp(terrainBox[2], tx0, int(mapW)) - tx0 : mapW, th = part ? std::clamp(terrainBox[3], ty0, int(mapH)) - ty0 : mapH;
    uint32_t terrainBytes = !terrainDirty ? 0 : part ? uint32_t(tw * th * 2) : uint32_t(pendingTerrain.size() * 2);
    if (terrainDirty && terrainBytes == 0) terrainDirty = false;  // (an empty box)
    if (instBytes) ensureBuffer(instanceBuf, instanceCap, instBytes);
    if (rectBytes) ensureBuffer(rectBuf, rectCap, rectBytes);
    uint32_t total = instBytes + rectBytes + terrainBytes;
    if (total && ensureUpload(total)) {
        uint8_t* p = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device, uploadBuf, true));
        GpuInstance* inst = reinterpret_cast<GpuInstance*>(p);
        for (uint32_t i : rawOrder) std::memcpy(inst++, &raw[i].in, sizeof(GpuInstance));  // (batch by batch)
        std::memcpy(p + instBytes, rects.data(), rectBytes);
        if (terrainBytes && part)
            for (int y = 0; y < th; ++y)
                std::memcpy(p + instBytes + rectBytes + size_t(y) * tw * 2, pendingTerrain.data() + size_t(ty0 + y) * mapW + tx0, size_t(tw) * 2);
        else if (terrainBytes)
            std::memcpy(p + instBytes + rectBytes, pendingTerrain.data(), terrainBytes);
        SDL_UnmapGPUTransferBuffer(device, uploadBuf);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        if (instBytes) {
            SDL_GPUTransferBufferLocation src{uploadBuf, 0};
            SDL_GPUBufferRegion dst{instanceBuf, 0, instBytes};
            SDL_UploadToGPUBuffer(copy, &src, &dst, true);
        }
        if (rectBytes) {
            SDL_GPUTransferBufferLocation src{uploadBuf, instBytes};
            SDL_GPUBufferRegion dst{rectBuf, 0, rectBytes};
            SDL_UploadToGPUBuffer(copy, &src, &dst, true);
        }
        if (terrainBytes) {
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = uploadBuf;
            src.offset = instBytes + rectBytes;
            src.pixels_per_row = uint32_t(tw), src.rows_per_layer = uint32_t(th);
            SDL_GPUTextureRegion dst{};
            dst.texture = tileIndex;
            dst.x = uint32_t(tx0), dst.y = uint32_t(ty0), dst.w = uint32_t(tw), dst.h = uint32_t(th), dst.d = 1;
            SDL_UploadToGPUTexture(copy, &src, &dst, false);
            terrainDirty = false, terrainAll = false;
        }
        SDL_EndGPUCopyPass(copy);
    }

    SDL_GPUColorTargetInfo ct{};
    ct.texture = bends ? backdrop : target;
    ct.clear_color = {0.07f, 0.07f, 0.08f, 1};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo dt{};
    dt.texture = depth;
    dt.clear_depth = 1;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = bends ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);

    // Terrain: HD atlas when it is not minified much, else the mipmapped SD atlas.
    if (tileIndex && sdAtlas) {
        // Full-resolution HD atlas when magnified or minified up to 2:1 (bilinear); otherwise the
        // mipmapped 32 px version of the same HD art (or the SD atlas in SD mode). The game shows HD
        // tiles (128 px) at 72-96 px on 1080-1440 px screens; with the 32 px version there, details of
        // the HD art (vents, rivets, plate edges) were blurred away (R2, compared with the game).
        bool hdFull = hdAtlas && hdTile / (32.0f * cam.zoom) <= 2.0f;
        bool hdSmall = !hdFull && hdDown;
        SDL_GPUTexture* atlasTex = hdFull ? hdAtlas : hdSmall ? hdDown : sdAtlas;
        TerrainUniforms u{};
        u.camera[0] = cam.x + float(originX) * 32.0f, u.camera[1] = cam.y + float(originY) * 32.0f, u.zoom = cam.zoom;  // (a larger grid: the map at its origin)
        u.viewport[0] = float(w), u.viewport[1] = float(h);
        u.mapTiles[0] = mapW, u.mapTiles[1] = mapH;
        u.atlasSize[0] = hdFull ? hdAtlasW : hdSmall ? hdDownW : sdAtlasW;
        u.atlasSize[1] = hdFull ? hdAtlasH : hdSmall ? hdDownH : sdAtlasH;
        u.tileTexels = hdFull ? hdTile : 32.0f;
        u.atlasCols = hdFull || hdSmall ? hdCols : sdCols;
        u.maxLod = hdFull ? 0.0f : 5.0f;  // 32 px megatiles: level 5 is one texel per megatile
        SDL_BindGPUGraphicsPipeline(pass, terrainPipe);
        // Nearest-neighbour while magnified (crisp pixels); mipmapped filtering only when minified.
        const bool magnified = u.tileTexels / (32.0f * cam.zoom) <= 1.0f;
        SDL_GPUTextureSamplerBinding b[2] = {{tileIndex, pointClamp}, {atlasTex, magnified ? pointClamp : linearMip}};
        SDL_BindGPUFragmentSamplers(pass, 0, b, 2);
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        ++stats.drawCalls;
    }

    // The sprite batches of these orders; `under`: the picture under them (the backdrop), else none.
    SpriteFragUniforms fu{};
    fu.targetSize[0] = float(w), fu.targetSize[1] = float(h), fu.zoom = cam.zoom;
    fu.sdArt = artQuality() == GraphicsQuality::SD ? 1.0f : 0.0f;
    auto drawSprites = [&](int fromOrder, int toOrder, SDL_GPUTexture* under) {
        SDL_GPUGraphicsPipeline* bound = nullptr;
        for (const Batch& b : batches) {
            if (b.order < fromOrder || b.order > toOrder) continue;
            SDL_GPUGraphicsPipeline* pipe = b.order == 2 ? glowPipe : spritePipe;
            if (pipe != bound) {
                SDL_BindGPUGraphicsPipeline(pass, pipe);
                SDL_BindGPUVertexStorageBuffers(pass, 0, &instanceBuf, 1);
                SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
                bound = pipe;
            }
            SpriteUniforms u{};
            u.camera[0] = cam.x, u.camera[1] = cam.y, u.zoom = cam.zoom, u.firstInstance = float(b.first);
            u.viewport[0] = float(w), u.viewport[1] = float(h);
            SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof u);
            // Nearest-neighbour unless zoomed out (sprite textures have no mipmaps).
            SDL_GPUSampler* spriteSampler = cam.zoom >= 0.5f ? pointClamp : linearClamp;
            SDL_GPUTexture* second = b.warp ? b.warp : b.mask ? b.mask : blackTex;
            SDL_GPUTextureSamplerBinding tb[3] = {{b.diffuse, spriteSampler}, {second, spriteSampler}, {under ? under : blackTex, pointClamp}};
            SDL_BindGPUFragmentSamplers(pass, 0, tb, 3);
            SDL_DrawGPUPrimitives(pass, 6, b.count, 0, 0);
            ++stats.drawCalls;
        }
    };
    drawSprites(0, 1, nullptr);
    if (bends) {
        // The picture so far goes to the target, and the sprites that bend what is under them read it there.
        SDL_EndGPURenderPass(pass);
        SDL_GPUBlitInfo blit{};
        blit.source.texture = backdrop, blit.source.w = w, blit.source.h = h;
        blit.destination.texture = target, blit.destination.w = w, blit.destination.h = h;
        blit.load_op = SDL_GPU_LOADOP_LOAD;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &blit);
        ct.texture = target;
        ct.load_op = SDL_GPU_LOADOP_LOAD;
        dt.load_op = SDL_GPU_LOADOP_LOAD;
        dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
        pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    }
    drawSprites(3, 3, bends ? backdrop : nullptr);
    drawSprites(2, 2, nullptr);  // (the glows last: a cloak drawn after a glow in front of it would cover the glow)

    if (!rects.empty()) {
        SDL_BindGPUGraphicsPipeline(pass, solidPipe);
        SDL_BindGPUVertexStorageBuffers(pass, 0, &rectBuf, 1);
        SolidUniforms u{};
        u.viewport[0] = float(w), u.viewport[1] = float(h);
        SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof u);
        SDL_DrawGPUPrimitives(pass, 6, uint32_t(rects.size()), 0, 0);
        ++stats.drawCalls;
    }
    SDL_EndGPURenderPass(pass);

    stats.imagesResident = imagesResident;
    stats.cpuMs = msSince(frameStart);
    return true;
}

bool Renderer::render(const Scene& scene) {
    Impl& r = *impl_;
    if (!r.device || !r.window) return false;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r.device);
    if (!cmd) return false;
    SDL_GPUTexture* sw = nullptr;
    uint32_t w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, r.window, &sw, &w, &h) || !sw) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return false;
    }
    r.draw(cmd, sw, w, h, scene);
    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

SDL_GPUDevice* Renderer::device() const { return impl_->device; }
SDL_GPUTextureFormat Renderer::colorFormat() const { return impl_->colorFormat; }

bool Renderer::drawTo(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32_t width, uint32_t height, const Scene& scene) {
    Impl& r = *impl_;
    if (!r.device || !r.pipelinesReady || !cmd || !target) return false;
    return r.draw(cmd, target, width, height, scene);
}

double Renderer::renderOffscreenAndWait(const Scene& scene) {
    Impl& r = *impl_;
    if (!r.device || !r.offscreen) return -1;
    auto t0 = Clock::now();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r.device);
    r.draw(cmd, r.offscreen, r.offscreenW, r.offscreenH, scene);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(r.device, true, &fence, 1);
        SDL_ReleaseGPUFence(r.device, fence);
    }
    return msSince(t0);
}

bool Renderer::readOffscreen(std::vector<uint8_t>& rgba, int& width, int& height) {
    Impl& r = *impl_;
    if (!r.device || !r.offscreen) return false;
    width = r.offscreenW, height = r.offscreenH;
    uint32_t bytes = uint32_t(width) * height * 4;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(r.device, &ti);
    if (!tb) return false;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r.device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = r.offscreen;
    src.w = uint32_t(width), src.h = uint32_t(height), src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(r.device, true, &fence, 1);
    SDL_ReleaseGPUFence(r.device, fence);
    const uint8_t* p = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(r.device, tb, false));
    rgba.assign(p, p + bytes);
    SDL_UnmapGPUTransferBuffer(r.device, tb);
    SDL_ReleaseGPUTransferBuffer(r.device, tb);
    return true;
}

}  // namespace stardraft::render
