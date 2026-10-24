#include <functional>
#include "stardraft/renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>

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
    SDL_GPUTexture* diffuse = nullptr;
    SDL_GPUTexture* mask = nullptr;
    float texW = 0, texH = 0;      // texels
    float boxW = 0, boxH = 0;      // frame box, map pixels
    float worldPerTexel = 1;       // map pixels per texel
    struct Frame {
        float u0, v0, u1, v1;      // texels
        float x, y, w, h;          // inside the box, map pixels
    };
    std::vector<Frame> frames;
};

uint32_t mipCount(uint32_t w, uint32_t h) {
    uint32_t n = 1;
    while ((w | h) >> n) ++n;
    return n;
}

}  // namespace

struct Renderer::Impl {
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
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
    uint16_t mapW = 0, mapH = 0;
    uint16_t tileIndexW = 0, tileIndexH = 0;  // size the tile index texture was created at
    SDL_GPUTexture* hdAtlas = nullptr;
    SDL_GPUTexture* hdDown = nullptr;  // HD atlas box-filtered to 32 px tiles, mipmapped
    float hdDownW = 0, hdDownH = 0;
    float hdAtlasW = 0, hdAtlasH = 0, hdTile = 0, hdCols = 1;
    SDL_GPUTexture* sdAtlas = nullptr;
    float sdAtlasW = 0, sdAtlasH = 0, sdCols = 1;
    std::vector<uint16_t> pendingTerrain;
    bool terrainDirty = false;
    std::vector<uint16_t> baseTerrain;                      // megatile per map tile, from updateTerrain
    std::vector<std::pair<int, uint16_t>> terrainPreview;  // (tile, megatile) shown instead
    void applyTerrainPreview();

    // Sources
    const GameData* data = nullptr;
    UnitSprites* unitSprites = nullptr;
    const Tileset* tileset = nullptr;
    uint16_t era = 0xFFFF, loadedEra = 0xFFFF;
    GraphicsQuality quality = GraphicsQuality::HD2, loadedQuality = GraphicsQuality::SD;
    bool carbot = false, loadedCarbot = false;
    bool tilesetDirty = true;

    std::map<int, ImageTex> images;
    double loadBudgetMs = 30;

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
    void ensureDepth(uint32_t w, uint32_t h);
    bool ensureBackdrop(uint32_t w, uint32_t h);
    bool ensureBuffer(SDL_GPUBuffer*& buf, uint32_t& cap, uint32_t bytes);
    bool ensureUpload(uint32_t bytes);
    ImageTex* image(int id, Clock::time_point frameStart, bool& pending);
    void loadImage(int id, ImageTex& t);
    void freeImages();
    bool draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32_t w, uint32_t h, const Scene& scene);
};

// --- Setup -------------------------------------------------------------------------------------

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() { shutdown(); }

bool Renderer::Impl::createDevice(std::string& err) {
    // Windows: Direct3D 12, or Vulkan when STARDRAFT_GPU_DRIVER is "vulkan". Elsewhere SDL picks Metal or Vulkan.
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);  // (its default is true)
#ifdef STARDRAFT_SHADERS_DXIL
    const char* asked = std::getenv("STARDRAFT_GPU_DRIVER");
    const bool vulkan = asked && std::string(asked) == "vulkan";
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, !vulkan);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, vulkan);
    if (vulkan) SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan");
#else
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
    // The map view uses none of these: GPUs without them can run it too. (Depth clamping stays: the pipelines leave
    // enable_depth_clip off, which asks for it.)
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_METAL_ALLOW_MACFAMILY1_BOOLEAN, true);  // (no sRGB targets)
#endif
    device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (!device) {
        err = std::string("SDL_CreateGPUDevice: ") + SDL_GetError();
        return false;
    }
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
    for (auto& [id, t] : images) {
        if (t.diffuse) SDL_ReleaseGPUTexture(device, t.diffuse);
        if (t.mask) SDL_ReleaseGPUTexture(device, t.mask);
    }
    images.clear();
}

void Renderer::shutdown() {
    Impl& r = *impl_;
    if (!r.device) return;
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
    r = Impl{};
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
    if (artQuality() == GraphicsQuality::SD) return true;
    HdTiles tiles;
    std::string err;
    if (!loadHdTiles(*data, era, artQuality(), tiles, err, carbot)) {
        error = err;
        return false;
    }
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
    if (era != loadedEra) buildSdAtlas();
    if (era != loadedEra || quality != loadedQuality || carbot != loadedCarbot) buildHdAtlas();
    if (quality != loadedQuality || carbot != loadedCarbot) freeImages();
    loadedEra = era;
    loadedQuality = quality;
    loadedCarbot = carbot;
    tilesetDirty = false;
    terrainDirty = !pendingTerrain.empty();
}

void Renderer::setQuality(GraphicsQuality q) {
    if (impl_->quality != q) impl_->quality = q, impl_->tilesetDirty = true;
}
GraphicsQuality Renderer::quality() const { return impl_->quality; }
bool Renderer::hdArt() const { return impl_->bcTextures; }
const char* Renderer::driver() const { return impl_->device ? SDL_GetGPUDeviceDriver(impl_->device) : ""; }

void Renderer::setCarbot(bool carbot) {
    if (impl_->carbot != carbot) impl_->carbot = carbot, impl_->tilesetDirty = true;
}
void Renderer::setLoadBudget(double ms) { impl_->loadBudgetMs = ms; }
Stats Renderer::stats() const { return impl_->stats; }

void Renderer::setPlayerColors(const uint32_t rgb[12]) {
    for (int i = 0; i < 12; ++i)
        for (int c = 0; c < 3; ++c) kPlayerRgb[i][c] = float((rgb[i] >> (16 - 8 * c)) & 255) / 255.0f;
}

void Renderer::setSources(const GameData* data, UnitSprites* sprites, const Tileset* tileset, uint16_t era) {
    Impl& r = *impl_;
    if (r.tileset != tileset || r.era != era) r.tilesetDirty = true;
    if (r.unitSprites != sprites) r.freeImages();
    r.data = data, r.unitSprites = sprites, r.tileset = tileset, r.era = era;
}

void Renderer::reloadImages() { impl_->freeImages(); }

void Renderer::updateTerrain(const TerrainLayer& mtxm) {
    Impl& r = *impl_;
    r.mapW = mtxm.width, r.mapH = mtxm.height;
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
    terrainDirty = !pendingTerrain.empty();
}

void Renderer::setTerrainPreview(const std::vector<std::pair<int, uint16_t>>& cells) {
    Impl& r = *impl_;
    std::vector<std::pair<int, uint16_t>> megas;
    for (auto [i, tile] : cells) {
        const int mega = r.tileset ? r.tileset->megatileFor(tile) : -1;
        if (mega > 0) megas.push_back({i, uint16_t(mega)});
    }
    if (megas == r.terrainPreview) return;
    r.terrainPreview = std::move(megas);
    r.applyTerrainPreview();
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

void Renderer::Impl::loadImage(int id, ImageTex& t) {
    t.loaded = true;
    std::string err;
    AnimImage a;
    // An extra image (UnitSprites::extraImage: the creep edges): its HD art from its .dds.grp, or its GRP in SD.
    const std::string* extra = unitSprites ? unitSprites->extraPath(id) : nullptr;
    const Grp* extraSd = extra ? unitSprites->grp(id) : nullptr;
    const bool hd = artQuality() != GraphicsQuality::SD && data &&
                    (extra ? extraSd && loadHdGrp(*data, *extra, *extraSd, artQuality(), a, err, carbot)
                           : loadAnimImage(*data, id, artQuality(), a, err, carbot));
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
    // SD fallback: classic GRP frames packed into an RGBA atlas (team pixels as a grey ramp + mask).
    const Grp* g = unitSprites ? unitSprites->grp(id) : nullptr;
    if (!g || g->frames.empty() || !tileset) {
        t.failed = true;
        return;
    }
    uint32_t cols = std::min<uint32_t>(uint32_t(g->frames.size()), std::max<uint32_t>(1, 4096 / std::max<uint32_t>(g->width, 1)));
    uint32_t rows = uint32_t((g->frames.size() + cols - 1) / cols);
    uint32_t w = cols * g->width, h = rows * g->height;
    std::vector<uint8_t> diffuse(size_t(w) * h * 4, 0), mask(size_t(w) * h * 4, 0);
    for (size_t f = 0; f < g->frames.size(); ++f) {
        const Grp::Frame& fr = g->frames[f];
        uint32_t ox = uint32_t(f % cols) * g->width + fr.x, oy = uint32_t(f / cols) * g->height + fr.y;
        for (int y = 0; y < fr.height; ++y)
            for (int x = 0; x < fr.width; ++x) {
                uint8_t idx = fr.pixels[size_t(y) * fr.width + x];
                if (!idx) continue;
                bool team = !extra && idx >= 8 && idx < 16;
                // Team pixels: white player's ramp gives a neutral grey the shader can tint. Glows: their
                // colour over black from the tileset's remap table.
                Rgb glow{};
                const bool glowing = unitSprites->glowColor(id, idx, *tileset, glow);
                if (glowing) team = false;
                const Rgb& c = glowing ? glow : tileset->palette[team ? unitSprites->remap(idx, 6) : idx];
                size_t o = (size_t(oy + y) * w + ox + x) * 4;
                diffuse[o] = c.r, diffuse[o + 1] = c.g, diffuse[o + 2] = c.b, diffuse[o + 3] = 255;
                mask[o] = team ? 255 : 0, mask[o + 3] = 255;
            }
    }
    t.diffuse = createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, w, h, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    t.mask = createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, w, h, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    uploadTexture(t.diffuse, w, h, diffuse.data(), diffuse.size(), false);
    uploadTexture(t.mask, w, h, mask.data(), mask.size(), false);
    t.texW = float(w), t.texH = float(h);
    t.boxW = g->width, t.boxH = g->height;
    for (size_t f = 0; f < g->frames.size(); ++f) {
        const Grp::Frame& fr = g->frames[f];
        float u = float((f % cols) * g->width + fr.x), v = float((f / cols) * g->height + fr.y);
        t.frames.push_back({u, v, u + fr.width, v + fr.height, float(fr.x), float(fr.y), float(fr.width), float(fr.height)});
    }
}

ImageTex* Renderer::Impl::image(int id, Clock::time_point frameStart, bool& pending) {
    pending = false;
    if (id < 0) return nullptr;
    ImageTex& t = images[id];
    if (!t.loaded) {
        if (msSince(frameStart) > loadBudgetMs) {
            pending = true;
            return nullptr;
        }
        loadImage(id, t);
    }
    return t.failed || !t.diffuse ? nullptr : &t;
}

// --- Frame ---------------------------------------------------------------------------------------

bool Renderer::Impl::draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32_t w, uint32_t h, const Scene& scene) {
    auto frameStart = Clock::now();
    stats = {};
    if (tilesetDirty) reloadTileset();
    ensureDepth(w, h);
    const Camera& cam = scene.camera;
    const float mapPxH = std::max<float>(mapH * 32.0f, 1);

    // Build sprite instances bucketed by image (one draw per image; depth handles the order).
    struct Raw {
        ImageTex* tex;
        ImageTex* warp;  // the Warp Texture image (draw function 12), bound in the team mask's place
        int order;  // 0 the terrain's (creep edges), 1 the others, 2 glows, 3 those that bend what is under them
        GpuInstance in;
    };
    std::vector<Raw> raw;
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
        const ImageTex::Frame& f = t->frames[size_t(s.frame) < t->frames.size() ? s.frame : 0];
        // On the map's pixel grid, as the game draws: whole-pixel origin and integer box halves, so no
        // sprite lands between pixels (visible with nearest-neighbour sampling).
        const float ox = std::floor(s.x), oy = std::floor(s.y);
        float boxL = ox - std::floor(t->boxW / 2.0f), boxT = oy - std::floor(t->boxH / 2.0f);
        float left = std::round(s.flip ? ox + (ox - (boxL + f.x + f.w)) : boxL + f.x);
        float top = std::round(boxT + f.y);
        if (left > viewR || top > viewB || left + f.w < viewL || top + f.h < viewT) continue;  // off screen
        GpuInstance in{};
        in.pos[0] = left, in.pos[1] = top;
        in.size[0] = f.w, in.size[1] = f.h;
        float u0 = f.u0 / t->texW, u1 = f.u1 / t->texW;
        in.uv[0] = s.flip ? u1 : u0, in.uv[1] = f.v0 / t->texH, in.uv[2] = s.flip ? u0 : u1, in.uv[3] = f.v1 / t->texH;
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
                const ImageTex::Frame& wf = warp->frames[size_t(std::clamp(s.level, 0, int(warp->frames.size()) - 1))];
                const float perX = wf.w > 0 ? (wf.u1 - wf.u0) / wf.w : 1, perY = wf.h > 0 ? (wf.v1 - wf.v0) / wf.h : 1;
                in.warpUv[0] = wf.u0 / warp->texW, in.warpUv[1] = wf.v0 / warp->texH;
                in.warpUv[2] = (wf.u0 + f.w * perX) / warp->texW, in.warpUv[3] = (wf.v0 + f.h * perY) / warp->texH;
                in.warpLim[0] = wf.u0 / warp->texW, in.warpLim[1] = wf.v0 / warp->texH;
                in.warpLim[2] = wf.u1 / warp->texW, in.warpLim[3] = wf.v1 / warp->texH;
            }
        }
        if (look != 64) warp = nullptr;
        in.flags = float((shadow ? 1 : 0) | (s.tintRed ? 2 : 0) | (s.ground ? 4 : 0) | (glow ? 8 : 0) | look);
        raw.push_back({t, warp, s.terrain ? 0 : glow ? 2 : (look == 16 || look == 32) ? 3 : 1, in});
    }
    // The terrain's sprites first (else the depth that they write would hide the shadows and circles over them), then
    // the others, then the sprites that bend what is under them (cloaks, the EMP shockwave: they read the picture of the
    // others), then glows in their own batches (additive pipeline; drawn last, as they write no depth).
    std::map<std::tuple<int, ImageTex*, ImageTex*>, std::vector<GpuInstance>> buckets;
    for (auto& [t, warp, order, in] : raw) buckets[{order, t, warp}].push_back(in);
    std::vector<GpuInstance> instances;
    instances.reserve(raw.size());
    struct Batch { ImageTex* tex; ImageTex* warp; uint32_t first, count; int order; };
    std::vector<Batch> batches;
    for (auto& [key, list] : buckets) {
        batches.push_back({std::get<1>(key), std::get<2>(key), uint32_t(instances.size()), uint32_t(list.size()), std::get<0>(key)});
        instances.insert(instances.end(), list.begin(), list.end());
    }
    // Only a scene with such sprites draws into the backdrop first (and then copies it to the target).
    const bool bends = !batches.empty() && batches.back().order == 3 && ensureBackdrop(w, h);
    stats.sprites = int(instances.size());

    // Overlays -> screen-space rects (outlines become 4 thin rects, text becomes glyph quads).
    std::vector<GpuRect> rects;
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
    uint32_t instBytes = uint32_t(instances.size() * sizeof(GpuInstance));
    uint32_t rectBytes = uint32_t(rects.size() * sizeof(GpuRect));
    uint32_t terrainBytes = terrainDirty ? uint32_t(pendingTerrain.size() * 2) : 0;
    if (terrainDirty && (!tileIndex || tileIndexW != mapW || tileIndexH != mapH)) {  // the map's size changed
        if (tileIndex) SDL_ReleaseGPUTexture(device, tileIndex);
        tileIndex = createTexture(SDL_GPU_TEXTUREFORMAT_R16_UINT, mapW, mapH, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        tileIndexW = mapW, tileIndexH = mapH;
    }
    if (instBytes) ensureBuffer(instanceBuf, instanceCap, instBytes);
    if (rectBytes) ensureBuffer(rectBuf, rectCap, rectBytes);
    uint32_t total = instBytes + rectBytes + terrainBytes;
    if (total && ensureUpload(total)) {
        uint8_t* p = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device, uploadBuf, true));
        std::memcpy(p, instances.data(), instBytes);
        std::memcpy(p + instBytes, rects.data(), rectBytes);
        if (terrainBytes) std::memcpy(p + instBytes + rectBytes, pendingTerrain.data(), terrainBytes);
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
            SDL_GPUTextureRegion dst{};
            dst.texture = tileIndex;
            dst.w = mapW, dst.h = mapH, dst.d = 1;
            SDL_UploadToGPUTexture(copy, &src, &dst, false);
            terrainDirty = false;
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
        u.camera[0] = cam.x, u.camera[1] = cam.y, u.zoom = cam.zoom;
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
            SDL_GPUTexture* second = b.warp ? b.warp->diffuse : b.tex->mask ? b.tex->mask : blackTex;
            SDL_GPUTextureSamplerBinding tb[3] = {{b.tex->diffuse, spriteSampler}, {second, spriteSampler}, {under ? under : blackTex, pointClamp}};
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

    stats.imagesResident = 0;
    for (auto& [id, t] : images) stats.imagesResident += t.diffuse != nullptr;
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
