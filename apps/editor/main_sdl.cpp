// StarDraft's editor on macOS and Linux (SDL3, Dear ImGui through apps/kit_sdl). It compiles the same portable core as
// the Windows program (core/core.inc, research/ports/MAC-BUILD-PLAN.md step 6), then the SDL shell: the host seam
// (shell_sdl.inc), the root view (sdl_main_view.inc), the functions the core still needs from the Win32 program
// (sdl_pending.inc, each marked until it is in the core) and the other workspaces' entry points (sdl_standins.inc).
//
// usage: stardraft_editor [map] [--install <folder>] [--size <w>x<h>] [--scale <s>]
//        stardraft_editor [map] --snapshot <out.png> [--size <w>x<h>] [--scale <s>]
//
// --snapshot: draws one frame without a window (SDL_VIDEODRIVER=offscreen needs no display), writes it, exits.
// The StarCraft install: --install, then STARDRAFT_INSTALL, then the usual folders of each system.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "canvas.h"
#include "view.h"
#include "wintypes.h"
#include "text_codec.h"
#include "canvas_imgui.h"
#include "sdl_host.h"
#include "core/tr.h"
#include "shell.h"
#include "message_box.h"  // apps/kit: the save question (shellAskSaveChanges)
#include "res/ui_icons.h"
#include "stardraft/commands.h"
#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/editor.h"
#include "stardraft/eud_project.h"
#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/pathing.h"
#include "stardraft/renderer.h"
#include "stardraft/scene.h"
#include "stardraft/settings.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

using namespace stardraft;
namespace fs = std::filesystem;
namespace draw = stardraft::draw;
namespace kit_sdl = stardraft::kit_sdl;

// The portable core (the same files the Windows program includes, in the same order, core_check.cpp).
#include "core/core.inc"

// The SDL shell: the root view and the map view, the functions the core needs, the other workspaces' stand-ins, and the
// host seam (shell.h).
#include "sdl_main_view.inc"
#include "sdl_standins.inc"
#include "sdl_pending.inc"
#include "shell_sdl.inc"

namespace {

// The start: the install (the search order is in findInstall), the map from the command line, then the loop.
fs::path findInstall(const std::string& given) {
    std::vector<fs::path> candidates;
    if (!given.empty()) candidates.push_back(given);
    if (const char* e = std::getenv("STARDRAFT_INSTALL"); e && *e) candidates.push_back(e);
    if (const char* h = std::getenv("HOME")) {
        candidates.push_back(fs::path(h) / "scmirror");
        candidates.push_back(fs::path(h) / ".wine/drive_c/Program Files (x86)/StarCraft");
        candidates.push_back(fs::path(h) / "Games/battlenet/drive_c/Program Files (x86)/StarCraft");
    }
#ifdef __APPLE__
    candidates.push_back("/Applications/StarCraft");
#endif
    std::error_code ec;
    for (const fs::path& p : candidates)
        if (!p.empty() && fs::is_directory(p, ec)) return p;
    return {};
}

// The PNG of the window's pixels, from the snapshot's texture (RGBA8, as the snapshot makes it).
bool writeSnapshot(SDL_GPUDevice* device, SDL_GPUTexture* tex, int w, int h, const std::string& out) {
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = uint32_t(w * h * 4);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &ti);
    if (!tb) return false;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = tex, src.w = uint32_t(w), src.h = uint32_t(h), src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    Image img;
    img.width = w, img.height = h;
    const uint8_t* p = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(device, tb, false));
    if (!p) {  // (the download did not map: no picture)
        SDL_ReleaseGPUTransferBuffer(device, tb);
        return false;
    }
    img.rgba.assign(p, p + size_t(w) * size_t(h) * 4);
    SDL_UnmapGPUTransferBuffer(device, tb);
    SDL_ReleaseGPUTransferBuffer(device, tb);
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    return writePng(out.c_str(), img);
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string mapArg, snapshot, installArg;
    int width = 1600, height = 1000;
    float scaleArg = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--snapshot") snapshot = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &width, &height);
        else if (a == "--scale") scaleArg = float(std::atof(next().c_str()));
        else if (a == "--install") installArg = next();
        else if (!a.empty() && a[0] != '-') mapArg = a;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) return std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()), 1;

    // The device first (the renderer makes it with its shader formats), then the host on it.
    render::Renderer renderer;
    std::string err;
    if (!renderer.createDevice(err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    SDL_GPUDevice* device = renderer.device();
    gRenderer = &renderer;
    gDevice = device;

    float scale = scaleArg > 0 ? scaleArg : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1;
    const char* basePath = SDL_GetBasePath();
    const std::string base = basePath ? basePath : "";
    int status = 0;
    {
        // Order matters: the host (which makes the ImGui context the fonts go into) outlives the root view.
        kit_sdl::SdlHost host(device);
        gHost = &host;
        host.setScale(scaleArg);  // (0: the display's scale; the views lay out at it)
        std::printf("stardraft_editor: fonts %s, scale %.2f\n", kit_sdl::loadFonts(base + "fonts/").c_str(), double(scale));
        if (!kit_sdl::loadIconAtlases(device, base + "res/")) std::printf("stardraft_editor: no icon atlases in res/\n");

        // The game data (the map needs it), then the map from the command line.
        const fs::path install = findInstall(installArg);
        if (!install.empty() && gState.data.open(install, err)) gDataOk = true;
        else gDataError = install.empty() ? "No StarCraft install found: start with --install <folder>, or STARDRAFT_INSTALL" : "Game data: " + err;
        MainView root;
        gRoot = &root;
        host.setFrameHook([](SDL_GPUCommandBuffer* cmd) {
            runPosted();
            drawMapOfRoot(cmd);
        });
        if (!mapArg.empty()) openMap(widen(mapArg), true);
        if (!gDataOk) setMessage(widen(gDataError));
        std::printf("stardraft_editor: GPU %s, %s\n", renderer.driver(), gDataOk ? "game data" : gDataError.c_str());

        if (!snapshot.empty()) {
            // A few frames: the first fills the font atlas with the glyphs the text needs; the last is the picture.
            SDL_GPUTextureCreateInfo ti{};
            ti.type = SDL_GPU_TEXTURETYPE_2D;
            ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            ti.width = uint32_t(width), ti.height = uint32_t(height), ti.layer_count_or_depth = 1, ti.num_levels = 1;
            SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &ti);
            if (!tex) return std::fprintf(stderr, "snapshot texture: %s\n", SDL_GetError()), 1;
            host.snapshot(root, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            host.snapshot(root, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            const bool ok = writeSnapshot(device, tex, width, height, snapshot);
            std::printf("%s %s\n", ok ? "wrote" : "could not write", snapshot.c_str());
            if (!ok) status = 1;
            SDL_WaitForGPUIdle(device);
            SDL_ReleaseGPUTexture(device, tex);
        } else {
            if (!host.show(root, L"StarDraft", width, height)) return std::fprintf(stderr, "could not open a window\n"), 1;
            updateTitle();  // (the window exists now: its title is the open map's)
            host.setAnimating(true);  // (the map and the posted work need frames; see shellPost)
            host.run();
        }
        // The map view's texture and the scene's state go before the device; the views before the host.
        releaseMapTexture();
        gRoot = nullptr;
        gHost = nullptr;
    }
    kit_sdl::unloadIconAtlases();
    gRenderer = nullptr;
    gDevice = nullptr;
    renderer.shutdown();
    SDL_Quit();
    return status;
}
