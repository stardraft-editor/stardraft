// StarDraft's editor on macOS and Linux (SDL3, Dear ImGui through apps/kit_sdl). It compiles the same portable core as
// the Windows program (core/core.inc, research/ports/MAC-BUILD-PLAN.md step 6), then the SDL shell: the host seam
// (shell_sdl.inc), the root view (sdl_main_view.inc), the functions the core still needs from the Win32 program
// (sdl_pending.inc, each marked until it is in the core) and the other workspaces' entry points (sdl_standins.inc).
//
// usage: stardraft_editor [map] [--install <folder>] [--size <w>x<h>] [--scale <s>]
//        stardraft_editor [map] --snapshot <out.png> [--size <w>x<h>] [--scale <s>]
//
// --snapshot: draws one frame without a window (SDL_VIDEODRIVER=offscreen needs no display), writes it, exits.
// --silent: a test run that drives the window plays no sound (a snapshot plays none either).
// The StarCraft install: --install, then STARDRAFT_INSTALL, the folder chosen before, then the usual folders of each
// system (core/install_search.inc); when none opens, the window asks for the folder.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <cctype>
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
#include <optional>
#include <random>
#include <set>
#include <string>
#include <tuple>
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
#include "dialog.h"  // apps/kit: the map pages' pickers (sdl_map_pages.inc)
#include "label.h"  // apps/kit: the block dialog's text (core/views_classic_batch.inc)
#include "text_field.h"  // apps/kit: the search boxes (core/views_search.inc)
#include "rich_field.h"  // apps/kit: the game text editor (core/views_game_text.inc)
#include "dropdown.h"  // apps/kit: the palette's dropdown (core/views_search.inc)
#include "check_box.h"  // apps/kit: the players and forces page (core/views_players.inc)
#include "slider.h"  // apps/kit: the volume and the generator's sliders (core/views_options.inc, core/views_new_page.inc)
#include "list_view.h"  // apps/kit: the palette and the Locations list (sdl_panels.inc)
#include "button.h"  // apps/kit: the View button (sdl_panels.inc)
#include "tree_view.h"  // apps/kit: the Triggers workspace's tree (core/views_triggers_tree.inc)
#include "res/ui_icons.h"
#include "stardraft/commands.h"
#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/editor.h"
#include "stardraft/eud_project.h"
#include "stardraft/eptrace.h"  // epTrace's report (core/project_page.inc)
#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/pathing.h"
#include "stardraft/previews.h"  // the terrain and doodad pictures (sdl_pictures.inc)
#include "stardraft/renderer.h"
#include "stardraft/scene.h"
#include "stardraft/settings.h"
#include "stardraft/terrain.h"
#include "stardraft/terrain_gen.h"  // the generator's settings (core/views_new_page.inc, sdl_new_page.inc)
#include "stardraft/tileset.h"
#include "stardraft/triggers.h"  // the Triggers workspace's model (core/views_triggers_tree.inc)
#include "stardraft/units.h"
#include "stardraft/wireframes.h"  // the Display section's wireframes (core/views_data_display.inc)

using namespace stardraft;
namespace fs = std::filesystem;
namespace draw = stardraft::draw;
namespace kit_sdl = stardraft::kit_sdl;

// Test runs play no sound: the user may be at the computer (the Windows program's gSilent, main.cpp).
bool gSilent = false;

// The portable core (the same files the Windows program includes, in the same order, core_check.cpp).
#include "core/core.inc"

// The SDL shell: the root view and the map view, the functions the core needs, the other workspaces' stand-ins, and the
// host seam (shell.h).
#include "sdl_main_view.inc"
#include "sdl_standins.inc"
#include "sdl_pending.inc"
#include "sdl_menus.inc"
#include "sdl_files.inc"  // the Files workspace's page and its sounds (core/views_files.inc)
#include "sdl_start.inc"  // the start page (core/views_start.inc)
#include "sdl_map_pages.inc"  // the map pages: Properties, Players and forces, Data (core/views_map_pages.inc and its views)
#include "sdl_new_page.inc"  // the New page (core/views_new_page.inc)
#include "sdl_project.inc"  // the Project workspace and the project commands (core/views_project.inc, core/project_page.inc)
#include "sdl_options.inc"  // Program options and Report feedback (core/views_options.inc, core/views_feedback.inc)
#include "sdl_map_dialogs.inc"  // Unit and Location properties, the text prompt, the quick picker and the map's menu
#include "sdl_briefing.inc"  // the Briefing workspace's page (the edit model is core/views_briefing.inc)
#include "sdl_data.inc"  // the Data workspace (the model is core/data_list.inc and core/views_data*.inc; the tables are the stock .dat files)
#include "sdl_triggers.inc"  // the Triggers workspace (the tree, the pane and the block picker are core/views_triggers_*.inc; the model core/triggers_model.inc)
#include "shell_sdl.inc"

namespace {

// The StarCraft install: --install, STARDRAFT_INSTALL, the folder the user chose before (the setting StarCraftFolder),
// then the usual folders of this system (core/install_search.inc). The first folder whose game data opens is the install.
// Returns false when none opens; the window then asks for the folder (askInstallFolder). lastErr is the first error the
// game data gave for a folder that exists (CASC's text), so a damaged install says why.
bool openInstall(const std::string& given, std::string& lastErr) {
    std::vector<fs::path> candidates;
    if (!given.empty()) candidates.push_back(given);
    if (const char* e = std::getenv("STARDRAFT_INSTALL"); e && *e) candidates.push_back(e);
    std::wstring saved;
    if (prefs::text(L"StarCraftFolder", saved) && !saved.empty()) candidates.push_back(saved);
#ifdef __APPLE__
    const InstallSystem system = InstallSystem::Mac;
#else
    const InstallSystem system = InstallSystem::Linux;
#endif
    if (const char* h = std::getenv("HOME"); h && *h) {
        candidates.push_back(fs::path(h) / "scmirror");  // the developer's mirror of the install
        for (const fs::path& p : installCandidatesOf(system, fs::path(h))) candidates.push_back(p);
    }
    std::error_code ec;
    for (const fs::path& c : candidates)
        for (const fs::path& t : installTries(c)) {
            if (!fs::is_directory(t, ec)) continue;
            std::string err;
            if (gState.data.open(t, err)) return true;
            if (lastErr.empty()) lastErr = err;
        }
    return false;
}

// The folder dialog's answer (SDL's thread): the UI thread tries the folder, saves it and opens the game data from it.
void useInstallFolder(const std::string& picked);
void installFolderPicked(void*, const char* const* list, int) {
    const std::string picked = list && list[0] ? list[0] : std::string();
    shellPost([picked] { useInstallFolder(picked); });
}
// Asks for the StarCraft folder (the window is up; the game data is not open yet).
void askInstallFolder() {
    SDL_ShowOpenFolderDialog(installFolderPicked, nullptr, dialogWindow(), nullptr, false);
}
void useInstallFolder(const std::string& picked) {
    if (picked.empty()) return;  // (cancelled: the program asks again on the next start)
    std::string err;
    for (const fs::path& t : installTries(fs::path(picked)))
        if (fs::is_directory(t) && gState.data.open(t, err)) {
            prefs::setText(L"StarCraftFolder", t.wstring());
            gDataOk = true, gDataError.clear();
            gStartView.dataError.clear();
            if (gRoot) gRoot->relayout(), gRoot->invalidate(), gRoot->map.invalidate();
            setMessage(L"The StarCraft folder is set");
            return;
        }
    kit::messageBox(*gRoot, shellTr(L"Not a StarCraft: Remastered folder"), widen(err.empty() ? picked : err), {L"OK"}, kit::MessageIcon::Warning);
    askInstallFolder();  // asks again: the folder above the install is a common pick
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
    std::string mapArg, snapshot, installArg, modeArg, workspaceArg, newPageArg, dialogArg, dataArg, ctArg, trigArg, trigSelArg;
    bool rectArg = false, previewArg = false;
    int width = 1600, height = 1000;
    float scaleArg = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--snapshot") snapshot = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &width, &height);
        else if (a == "--scale") scaleArg = float(std::atof(next().c_str()));
        else if (a == "--install") installArg = next();
        else if (a == "--mode") modeArg = next();  // a mode's name (units, terrain, doodads, sprites, locations), for a snapshot
        else if (a == "--workspace") workspaceArg = next();  // a workspace's name (map, data, triggers, project...), for a snapshot
        else if (a == "--data") dataArg = next();  // tab:entry (units:0, images:239, strings:5): the Data workspace opens it, for a snapshot
        else if (a == "--trig") trigArg = next();  // root:index[:child...]: the Triggers workspace selects that node, for a snapshot
        else if (a == "--trigsel") trigSelArg = next();  // an action of the selected trigger: it is selected, for a snapshot
        else if (a == "--classictrig") ctArg = next();  // trigger:kind:entry (2:1:0): the Classic triggers page selects it, for a snapshot
        else if (a == "--newpage") newPageArg = next();  // classic or eud: the New page's choices, for a snapshot
        else if (a == "--dialog") dialogArg = next();  // options, feedback, newproject, prompt, listpick or blockpick (--size its size), or unitprops, locprops or quick: that view is the snapshot
        else if (a == "--rect") rectArg = true;  // Terrain mode's Rectangle (the Tiles panel), for a snapshot
        else if (a == "--scriptpreview") previewArg = true;  // the Project page's script preview on (not saved), for a snapshot
        else if (a == "--silent") gSilent = true;
        else if (!a.empty() && a[0] != '-') mapArg = a;
    }
    if (!snapshot.empty()) gSilent = true;
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
        std::string dataErr;
        if (openInstall(installArg, dataErr)) gDataOk = true;
        else {
            gDataError = "No StarCraft install found: choose the StarCraft folder, or start with --install <folder>, or STARDRAFT_INSTALL";
            if (!dataErr.empty()) gDataError += ". Game data: " + dataErr;
            gStartView.dataError = widen(gDataError);  // the start page keeps the reason (the status bar's text goes)
        }
        loadDock();  // the dock layout in the settings (core/dock_prefs.inc), as the Windows program at its start
        if (previewArg) gDock.scriptPreview = true;
        registerMapCommands();  // the Map workspace's command table (core/map_commands.inc): the menus and keys read it
        registerWorkspaceView(Workspace::Files, gFilesPage);  // the Assets / Files workspace (sdl_files.inc)
        registerMapPages();  // the map pages' workspaces (sdl_map_pages.inc)
        registerWorkspaceView(Workspace::Briefing, gSdlBriefing);  // the Briefing workspace (sdl_briefing.inc)
        registerWorkspaceView(Workspace::Data, gSdlData);  // the Data workspace (sdl_data.inc)
        registerWorkspaceView(Workspace::Project, gSdlProject);  // the Project workspace (sdl_project.inc)
        registerWorkspaceView(Workspace::Triggers, gSdlTriggers);  // the Triggers workspace (sdl_triggers.inc)
        initStartPage();  // the start page's callbacks (sdl_start.inc)
        MainView root;
        gRoot = &root;
        rebuildMenus();
        host.setQuitRequest(requestQuit);  // the window's close and the system's quit ask about unsaved changes first
        host.setBeforePump(runPosted);  // the posted work runs outside the frame hook (a dialog it opens pumps frames)
        host.setFrameHook([](SDL_GPUCommandBuffer* cmd) { drawMapOfRoot(cmd); });
        if (!mapArg.empty()) openAny(widen(mapArg));  // a map, or a project (.eds, .stardraft, its folder): as File > Open does
        for (int m = 0; !modeArg.empty() && m < kModeCount; ++m) {
            std::string name = narrow(kModeNames[m]);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
            if (name == modeArg) setMode(Mode(m));
        }
        // A workspace by name (the order of the Workspace enum): its view shows, or "Not on this system yet" where none is.
        static const char* const kWorkspaceNames[] = {"map",    "data",     "triggers",        "assets",  "project",
                                                      "briefing", "files", "props", "players", "classictriggers", "classic"};
        for (int w = 0; !workspaceArg.empty() && w < int(sizeof kWorkspaceNames / sizeof kWorkspaceNames[0]); ++w)
            if (workspaceArg == kWorkspaceNames[w]) setWorkspace(Workspace(w));
        if (rectArg) setMode(Mode::Terrain), setTerrainRect(true);
        if (previewArg && !gProj.sections.empty()) triggersRefresh(gProj.sections.back());  // the last script in the preview (as scriptpreview:)
        if (!dataArg.empty()) sdlDataSelect(dataArg);
        if (!ctArg.empty()) sdlClassicTrigSelect(ctArg);
        if (!trigArg.empty() || !trigSelArg.empty()) sdlTrigSelect(trigArg, trigSelArg);
        if (newPageArg == "classic" || newPageArg == "eud") openNewPageSdl(newPageArg == "eud" ? 1 : 0);
        if (!gDataOk) setMessage(widen(gDataError));
        std::printf("stardraft_editor: GPU %s, %s\n", renderer.driver(), gDataOk ? "game data" : gDataError.c_str());

        if (!snapshot.empty()) {
            // A map's dialog (--dialog unitprops|locprops|prompt|quick, sdl_map_dialogs.inc) is the picture at its own size.
            MapDialogShot mapShot;
            if (mapDialogShot(dialogArg, mapShot)) width = mapShot.size.x, height = mapShot.size.y;
            // A few frames: the first fills the font atlas with the glyphs the text needs; the last is the picture.
            SDL_GPUTextureCreateInfo ti{};
            ti.type = SDL_GPU_TEXTURETYPE_2D;
            ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            ti.width = uint32_t(width), ti.height = uint32_t(height), ti.layer_count_or_depth = 1, ti.num_levels = 1;
            SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &ti);
            if (!tex) return std::fprintf(stderr, "snapshot texture: %s\n", SDL_GetError()), 1;
            // The picture is the window's root, or the dialog's view (--dialog options|feedback|listpick) at the --size given.
            FeedbackView feedbackShot(gFeedbackProgram, L"StarDraft 2");
            NewProjectStart projectStart;  // (newproject: File > Project > New EUD project's fields, as sdl_project.inc fills them)
            projectStart.name = gState.ed && !gState.ed->path().empty() ? gState.ed->path().stem().wstring() : L"MyMap";
            projectStart.location = defaultProjectsFolder().make_preferred().wstring();
            if (docOpen()) projectStart.openMap = docTitle(gActive, false);
            NewProjectView projectShot(projectStart);
            TextPromptView promptShot(L"0x", false);
            // listpick: the list picker as a multi-pick of unit types (classictrig:values on Windows; --size 378x494).
            std::vector<ListPickItem> pickItems;
            for (int u = 0; u <= 227; ++u) pickItems.push_back(ListPickItem{unitDisplayName(u), L"", std::to_string(u), "", -1});
            ListPickView pickShot(L"Search values", pickItems, "");
            // blockpick: Add an action's block picker (as the Windows picker's window; --size 1266x861 its client area at 100 %).
            BlockPickView blockShot(gSdlBlockPickHost, false, {}, {});
            pickShot.setMulti({"0", "1", "37"}, L"Set");
            draw::View* shown = &root;
            if (dialogArg == "options") shown = &gOptionsView;
            else if (dialogArg == "feedback") shown = &feedbackShot;
            else if (dialogArg == "newproject") shown = &projectShot;
            else if (dialogArg == "prompt") shown = &promptShot;
            else if (dialogArg == "listpick") shown = &pickShot;
            else if (dialogArg == "blockpick") shown = &blockShot;
            else if (mapShot.view) shown = mapShot.view;
            host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            if (!trigSelArg.empty()) sdlTrigFocusFirstValue();  // (the pane's hit areas are the first frame's)
            host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            const bool ok = writeSnapshot(device, tex, width, height, snapshot);
            std::printf("%s %s\n", ok ? "wrote" : "could not write", snapshot.c_str());
            if (!ok) status = 1;
            SDL_WaitForGPUIdle(device);
            SDL_ReleaseGPUTexture(device, tex);
        } else {
            if (!host.show(root, L"StarDraft", width, height)) return std::fprintf(stderr, "could not open a window\n"), 1;
            updateTitle();  // (the window exists now: its title is the open map's)
            if (!gDataOk) shellPost(askInstallFolder);  // no install was found: the window asks for its folder
            host.setAnimating(true);  // (the map and the posted work need frames; see shellPost)
            host.run();
        }
        // The map view's texture and the scene's state go before the device; the views before the host.
        releaseMapTexture();
        gRoot = nullptr;
        gHost = nullptr;
    }
    releasePaletteArt();  // its pictures' textures: before the device goes
    kit_sdl::unloadIconAtlases();
    gRenderer = nullptr;
    gDevice = nullptr;
    renderer.shutdown();
    SDL_Quit();
    return status;
}
