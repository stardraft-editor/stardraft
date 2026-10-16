// StarDraft's editor on Windows (stardraft.exe), macOS and Linux (SDL3, Dear ImGui through apps/kit_sdl; the Win32
// program, main.cpp, builds as stardraft-win32.exe, a fallback that is not in releases). It compiles the same portable
// core as the Win32 program (core/core.inc, research/ports/MAC-BUILD-PLAN.md step 6), then the SDL shell: the host seam
// (shell_sdl.inc), the root view (sdl_main_view.inc), the functions the core still needs from the Win32 program
// (sdl_pending.inc, each marked until it is in the core) and the workspaces (sdl_*.inc).
//
// usage: stardraft_editor [map] [--install <folder>] [--size <w>x<h>] [--scale <s>]
//        stardraft_editor [map] --snapshot <out.png> [--size <w>x<h>] [--scale <s>]
//
// --snapshot: draws one frame without a window (SDL_VIDEODRIVER=offscreen needs no display), writes it, exits.
// --silent: a test run that drives the window plays no sound (a snapshot plays none either).
// --log: the Log is open; --build: Build runs first (a snapshot then shows its lines in the Log).
// --theme light|dark, --accent <n>, --lang <code>: the theme, the accent and the language for this run (not saved).
// --drop <file>: the file is dropped on the window first (as a drop, or a map that Finder opens on macOS).
// Windows: --send-crash <folder> sends a crash's report (the crashed program starts it); --crash-test crashes on purpose.
// A map given while the program is open goes to the open program (not in a test run: a snapshot or --silent).
// The StarCraft install: --install, then STARDRAFT_INSTALL, the folder chosen before, then the usual folders of each
// system (core/install_search.inc; on Windows Blizzard's registry entries first); when none opens, the window asks for
// the folder.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __APPLE__
#include "mac_app_menu.h"  // the application menu's About and Program options (mac_app_menu.mm)
#endif

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
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include <climits>
#include <cwctype>
#include <thread>
#include <unordered_map>

#include "canvas.h"
#include "view.h"
#include "wintypes.h"
#include "text_codec.h"
#include "http.h"  // apps/common: one HTTPS request (core/feedback_send.inc: Send in Report feedback)
#include "canvas_imgui.h"
#include "sdl_host.h"
#include "core/tr.h"
#include "shell.h"
#include "install.h"  // apps/common: the registry's install folders (Windows)
#include "http.h"  // apps/common: the GitHub requests (core/releases.inc)
#include "update.h"  // apps/common: the self-update's file steps (core/releases.inc, sdl_updates.inc)
#include "crash_report.h"  // apps/common: the crash handler and sending reports (core/releases.inc, shell_sdl_win.inc)
#ifdef _WIN32
#include "file_types.h"  // apps/common: the files that open with StarDraft 2 (shell_sdl_win.inc)
#include <dwmapi.h>      // the dark title bar (shell_sdl_win.inc)
#include <shellapi.h>    // the elevated start (shell_sdl_win.inc)
#else
#include <cerrno>
#include <signal.h>  // kill (shell_sdl_win.inc)
#include <unistd.h>  // getpid (shell_sdl_win.inc: the processes that still run)
#endif
#if __has_include("build_info.h")
#include "build_info.h"  // STARDRAFT_COMMIT (cmake/build_info.cmake): Check for updates compares it with the release's
#endif
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
#include "stardraft/msqc.h"  // the triggers' keys and buttons (core/project_build.inc)
#include "stardraft/pathing.h"
#include "stardraft/portraits.h"  // the portraits' clips (core/data_pictures.inc)
#include "stardraft/previews.h"  // the terrain and doodad pictures (sdl_pictures.inc)
#include "stardraft/sprites.h"  // the image player (core/data_pictures.inc)
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
// A snapshot run: no message box waits for the user (errors go to the status bar and the log).
bool gNoBoxes = false;

// The portable core (the same files the Windows program includes, in the same order, core_check.cpp).
#include "core/core.inc"

// The SDL shell: the root view and the map view, the functions the core needs, the other workspaces' stand-ins, and the
// host seam (shell.h).
#include "sdl_main_view.inc"
#include "sdl_pending.inc"
#include "sdl_menus.inc"
#include "sdl_popshot.inc"  // a value's pop-out in a snapshot (--trigpop, --classictrig with a value)
#include "sdl_files.inc"  // the Files workspace's page and its sounds (core/views_files.inc)
#include "sdl_start.inc"  // the start page (core/views_start.inc)
#include "sdl_map_pages.inc"  // the map pages: Properties, Players and forces, Data (core/views_map_pages.inc and its views)
#include "sdl_new_page.inc"  // the New page (core/views_new_page.inc)
#include "sdl_project.inc"  // the Project workspace and the project commands (core/views_project.inc, core/project_page.inc)
#include "sdl_options.inc"  // Program options and Report feedback (core/views_options.inc, core/views_feedback.inc)
#include "sdl_map_dialogs.inc"  // Unit and Location properties, the text prompt, the quick picker and the map's menu
#include "sdl_briefing.inc"  // the Briefing workspace's page (the edit model is core/views_briefing.inc)
#include "sdl_data.inc"  // the Data workspace (the model is core/data_list.inc and core/views_data*.inc; the tables are the stock .dat files)
#include "sdl_tips.inc"  // the hover tooltips (core/views_tip.inc)
#include "sdl_build.inc"  // Build's parts of this system (core/project_build.inc)
#include "sdl_triggers.inc"  // the Triggers workspace (the tree, the pane and the block picker are core/views_triggers_*.inc; the model core/triggers_model.inc)
#include "sdl_updates.inc"  // Help > Check for updates, the crash reports' choice (core/releases.inc)
#include "shell_sdl.inc"
#include "sdl_cursor_check.inc"  // --cursors: the window's cursors at some points, as text (a snapshot run)
#include "shell_sdl_win.inc"  // the Windows parts: the crash handler, the map files, one program for all maps

namespace {

// The StarCraft install: --install, STARDRAFT_INSTALL, the folder the user chose before (the setting StarCraftFolder),
// then the usual folders of this system (core/install_search.inc). The first folder whose game data opens is the install.
// Returns false when none opens; the window then asks for the folder (askInstallFolder). lastErr is the first error the
// game data gave for a folder that exists (CASC's text), so a damaged install says why.
bool openInstall(const std::string& given, std::string& lastErr) {
    std::vector<fs::path> candidates;
    if (!given.empty()) candidates.push_back(given);
    if (fs::path e = envPath("STARDRAFT_INSTALL"); !e.empty()) candidates.push_back(e);
    // (Windows: the Windows program's install.txt first, so both programs use one install)
    if (const fs::path f = sdlWindowsInstallFile(); !f.empty())
        if (std::ifstream in(f); in) {
            std::string dir;
            std::getline(in, dir);
            if (!dir.empty()) candidates.push_back(fs::path(widen(dir)));
        }
    std::wstring saved;
    if (prefs::text(L"StarCraftFolder", saved) && !saved.empty()) candidates.push_back(saved);
#ifdef _WIN32
    // Blizzard's registry entries, then the usual folders (as the Windows program searches).
    for (const fs::path& p : stardraft::platform::registryInstallFolders()) candidates.push_back(p);
    for (const fs::path& p : installCandidatesOf(InstallSystem::Windows, {})) candidates.push_back(p);
#else
#ifdef __APPLE__
    const InstallSystem system = InstallSystem::Mac;
#else
    const InstallSystem system = InstallSystem::Linux;
#endif
    if (const fs::path h = homeFolder(); !h.empty()) {
        candidates.push_back(h / "scmirror");  // the developer's mirror of the install
        for (const fs::path& p : installCandidatesOf(system, h)) candidates.push_back(p);
    }
#endif
    std::error_code ec;
    for (const fs::path& c : candidates)
        for (const fs::path& t : installTries(c)) {
            if (!fs::is_directory(t, ec)) continue;
            std::string err;
            if (gState.data.open(t, err)) return gInstallInUse = t, true;
            if (lastErr.empty()) lastErr = err;
        }
    return false;
}

// The folder dialog's answer (SDL's thread): the UI thread tries the folder, saves it and opens the game data from it.
void useInstallFolder(const std::string& picked);
// The StarCraft folder for the next starts: the settings' StarCraftFolder (and on Windows the Windows program's
// install.txt, so both programs use one install).
void saveInstallFolder(const fs::path& t) {
    prefs::setText(L"StarCraftFolder", t.wstring());
    if (const fs::path f = sdlWindowsInstallFile(); !f.empty()) {
        std::error_code ec;
        fs::create_directories(f.parent_path(), ec);
        std::ofstream(f) << narrow(t.wstring());
    }
}
void installFolderPicked(void*, const char* const* list, int) {
    const std::string picked = list && list[0] ? list[0] : std::string();
    shellPost([picked] { useInstallFolder(picked); });
}
// Asks for the StarCraft folder (the window is up; the game data is not open yet).
void askInstallFolder() {
    SDL_ShowOpenFolderDialog(installFolderPicked, nullptr, dialogWindow(), nullptr, false);
}
// A file dropped on the window, or (macOS) opened from Finder: the Windows program's drop (core/documents.inc), after the
// save question for the open map (SDL has one tab).
void dropFile(const std::string& utf8) {
    openDropped({fs::path(widen(utf8))}, [](const fs::path& f) { confirmReplace([f] { openAny(f); }); });
}

// The main window without --size: w x h in the system's points (on a Retina display of macOS 2 pixels each, as the
// Windows program's 1600 x 1000 at 100 %), at most 90 % of the display's free area, in its middle.
void fitMainWindow(int w, int h) {
    int count = 0;
    SDL_Window** wins = SDL_GetWindows(&count);
    SDL_Window* win = wins && count > 0 ? wins[0] : nullptr;
    SDL_free(wins);
    SDL_Rect area{};
    if (!win || !SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(win), &area) || area.w <= 0 || area.h <= 0) return;
    SDL_SetWindowSize(win, std::min(w, area.w * 9 / 10), std::min(h, area.h * 9 / 10));
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetDisplayForWindow(win)), SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetDisplayForWindow(win)));
}

void useInstallFolder(const std::string& picked) {
    if (picked.empty()) return;  // (cancelled: the program asks again on the next start)
    std::string err;
    for (const fs::path& t : installTries(fs::path(widen(picked))))  // (UTF-8)
        if (fs::is_directory(t) && gState.data.open(t, err)) {
            gInstallInUse = t;
            saveInstallFolder(t);
            gDataOk = true, gDataError.clear();
            gStartView.dataError.clear();
            if (gRoot) gRoot->relayout(), gRoot->invalidate(), gRoot->map.invalidate();
            setMessage(L"The StarCraft folder is set");
            return;
        }
    msgbox::ask(*gRoot, shellTr(L"Not a StarCraft: Remastered folder"), widen(err.empty() ? picked : err), {L"OK"}, msgbox::Icon::Warning);
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
    std::string mapArg, snapshot, installArg, modeArg, workspaceArg, newPageArg, dialogArg, dataArg, ctArg, themeArg, langArg, trigArg, trigSelArg, briefArg, dropArg;
    int accentArg = -1;
    gSelfPath = argc > 0 ? argv[0] : "";  // (Restart now starts it again, sdl_options.inc)
    bool rectArg = false, previewArg = false, logArg = false, buildArg = false, trigCodeArg = false;
    int trigPopArg = -1;  // --trigpop: the value whose pop-out shows (sdl_popshot.inc); -2: a text's words; -100 - n: the variable form's field n
    std::string textPopArg;  // --textpop: a game text pop-out in the snapshot (sdl_data.inc)
    std::string palViewArg;  // --palview: the palette's view items (list, icons, sprites, normal, large), comma separated
    bool pieArg = false;  // --pie: the pie palette open (Terrain mode), the mouse to the right of its middle
    std::uint32_t restartPid = 0;  // --restart <pid> (Program options > Language): the old program, which ends first
    bool untitledArg = false;  // --untitled: a new unsaved project (as the Win32 program's screenshot run starts with), for a snapshot
    bool midPanArg = false;  // --midpan: Options > Middle mouse click and hold (Pan map, Drag map), as text after the snapshot
    bool cursorsArg = false;  // --cursors: the cursors at some points, as text after the snapshot (sdl_cursor_check.inc)
    // --key, --click, --drag: keys pressed and left clicks and drags of the mouse before the snapshot, in their order
    // (sdl_menus.inc: sdlPressKey, sdlMouseShot): the option's letter (k, c, d) and its value.
    std::vector<std::pair<char, std::string>> inputArgs;
    std::vector<std::string> rclickArgs;  // --rclick: right clicks before the snapshot (sdl_menus.inc: sdlRightClickShot)
    std::vector<std::string> commandArgs;  // --command: menu commands run before the snapshot (sdl_menus.inc: sdlCommandShot)
    std::string saveAsArg;  // --saveas: the save round-trip (core/map_commands.inc: mapSaveRoundTrip)
    std::string newProjectArg;  // --newproject: a new project in that folder (core/project_page.inc: createProject)
    int width = 1600, height = 1000;
    bool sizeArg = false;
    float scaleArg = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--snapshot") snapshot = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &width, &height), sizeArg = true;
        else if (a == "--scale") scaleArg = float(std::atof(next().c_str()));
        else if (a == "--install") installArg = next();
        else if (a == "--mode") modeArg = next();  // a mode's name (units, terrain, doodads, sprites, locations), for a snapshot
        else if (a == "--workspace") workspaceArg = next();  // a workspace's name (map, data, triggers, project...), for a snapshot
        else if (a == "--data") dataArg = next();  // tab:entry (units:0, images:239, strings:5): the Data workspace opens it, for a snapshot
        else if (a == "--trig") trigArg = next();  // root:index[:child...]: the Triggers workspace selects that node, for a snapshot
        else if (a == "--trigsel") trigSelArg = next();  // an action of the selected trigger: it is selected, for a snapshot
        else if (a == "--trigpop") {  // n or text: the pop-out of value n of the selected block (--trigsel), or of its text's words, shows, for a snapshot
            const std::string n = next();
            trigPopArg = n == "text" ? -2 : n.rfind("var:", 0) == 0 ? -100 - std::atoi(n.c_str() + 4) : std::atoi(n.c_str());  // (var:<field>: the variable form's field)
        }
        else if (a == "--textpop") textPopArg = next();  // rename, tooltip or unitname: that game text pop-out shows (sdl_data.inc), for a snapshot
        else if (a == "--trigcode") trigCodeArg = true;  // the Triggers workspace's code pane on (not saved), for a snapshot
        else if (a == "--classictrig") ctArg = next();  // trigger:kind:entry[:value] (2:1:0): the Classic triggers page selects it (and shows the value's pop-out), for a snapshot
        else if (a == "--briefing") briefArg = next();  // row[:ms]: the Briefing page selects that row of the first briefing, the preview at that time, for a snapshot
        else if (a == "--newpage") newPageArg = next();  // classic or eud: the New page's choices, for a snapshot
        else if (a == "--dialog") dialogArg = next();  // options, feedback, newproject, prompt or listpick (--size its size), or blockpick, blockhelp:<block id>, unitprops, locprops, quick, valuepop[:number|:colors|:text], about, message, message:build, message:delete, error or tip: that view is the snapshot
        else if (a == "--rect") rectArg = true;  // Terrain mode's Rectangle (the Tiles panel), for a snapshot
        else if (a == "--scriptpreview") previewArg = true;  // the Project page's script preview on (not saved), for a snapshot
        else if (a == "--log") logArg = true;  // the Log open (View > Log), for a snapshot
        else if (a == "--build") buildArg = true;  // Build (Ctrl+B) runs first: its lines go to the Log; a snapshot waits for its end
        else if (a == "--newproject") newProjectArg = next();  // a folder: File > Project > New EUD project makes the project Sample (a new map) in it, for a snapshot
        else if (a == "--saveas") saveAsArg = next();  // a file: File > Save as saves the map there, then it is compared with the map that opens from it, for a snapshot
        else if (a == "--command") commandArgs.push_back(next());  // a command by name (view.towns, view.grid...) as its menu item runs it, for a snapshot
        else if (a == "--theme") themeArg = next();  // light or dark: the theme for this run (not saved), for a snapshot
        else if (a == "--accent") accentArg = std::atoi(next().c_str());  // the accent's number (theme::kAccents), for a snapshot
        else if (a == "--lang") langArg = next();  // en, ko, es, ru or zh: the language for this run (not saved), for a snapshot
        else if (a == "--drop") dropArg = next();  // a file dropped on the window (as from Finder on macOS), for a snapshot
        else if (a == "--silent") gSilent = true;
        else if (a == "--palview") palViewArg = next();  // list, icons, sprites, normal, large (comma separated): the palette's view of the mode, for a snapshot
        else if (a == "--pie") pieArg = true;  // the pie palette (hold Space in Terrain mode) open, the mouse at a slice, for a snapshot
        else if (a == "--restart") restartPid = std::uint32_t(std::strtoul(next().c_str(), nullptr, 10));  // the old program's process id (sdl_options.inc)
        else if (a == "--untitled") untitledArg = true;  // a new project that is not saved yet (core/documents.inc), for a snapshot
        else if (a == "--midpan") midPanArg = true;  // the middle button's pan and drag of the map as text, after the snapshot
        else if (a == "--cursors") cursorsArg = true;  // the map view's and a text field's cursors as text, after the snapshot
        else if (a == "--key") inputArgs.emplace_back('k', next());  // a key (Ctrl+A, 5, Ctrl+2: Ctrl is Cmd on macOS) pressed in the window, for a snapshot
        else if (a == "--click") inputArgs.emplace_back('c', next());  // x,y[:mods]: a left click there (window pixels; mods Shift+Ctrl+Alt), for a snapshot
        else if (a == "--drag") inputArgs.emplace_back('d', next());  // x0,y0-x1,y1[:mods]: a left drag from the first point to the second, for a snapshot
        else if (a == "--rclick") rclickArgs.push_back(next());  // x,y[:n]: a right click there; the menu's items go to the output, item n runs, for a snapshot
        else if (!a.empty() && a[0] != '-') mapArg = a;
    }
    if (!snapshot.empty()) gSilent = true, gNoBoxes = true;
    // A test run (a snapshot, --silent) asks nothing, sends nothing and leaves the user's file types.
    const bool testRun = gSilent;
    // Windows: a crash's sender ends here, the crash handler goes in, and a map for the open program goes to it.
    if (const int sent = sdlWindowsFirst(argc, argv, testRun); sent >= 0) return sent;
    if (restartPid) restartWaitFor(restartPid);  // (before the pipe: the old program still listens; core/documents.inc)
    if (sdlSendToOpenProgram(mapArg, testRun)) return 0;
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
        kit_sdl::setEmbeddedFiles(sdlEmbeddedFiles());  // (Windows: the fonts, atlases and icon are in stardraft.exe too)
        std::printf("stardraft_editor: fonts %s, scale %.2f\n", kit_sdl::loadFonts(base + "fonts/").c_str(), double(scale));
        if (!kit_sdl::loadIconAtlases(device, base + "res/")) std::printf("stardraft_editor: no icon atlases in res/\n");
        kit_sdl::setGlyphIcons(kitpaint::glyphIcon);  // the glyphs as UI icons (no system has to have Segoe MDL2 Assets)
        if (!kit_sdl::loadResourceIcon(1, base + "res/stardraft.ico")) std::printf("stardraft_editor: no app icon in res/\n");  // (About's)
        host.setHoverHook(sdlHover);  // the views' tooltips (sdl_tips.inc)
        host.setRightClickHook(sdlWindowRightClick);  // Report feedback in the dialogs and pop-outs (sdl_menus.inc)

        // The game data (the map needs it), then the map from the command line.
        std::string dataErr;
        if (openInstall(installArg, dataErr)) gDataOk = true;
        else {
            gDataError = "No StarCraft install found: choose the StarCraft folder, or start with --install <folder>, or STARDRAFT_INSTALL";
            if (!dataErr.empty()) gDataError += ". Game data: " + dataErr;
            gStartView.dataError = widen(gDataError);  // the start page keeps the reason (the status bar's text goes)
        }
        // The dock layout (core/dock_prefs.inc), the recent lists, the working folder and the unit limits in the
        // settings, as the Win32 program at its start (snapshots: the defaults).
        if (!gSilent) loadDock(), loadRecentLists(), loadUnitLimits();
        gSettingsOff = gSilent;  // (snapshots and tests: the defaults, and nothing saved)
        cleanUnsavedProjects();  // the unsaved projects' folders of programs that ended (core/documents.inc; not in a test run)
        if (loadSettingsTable()) shellRendererGraphics(gState.quality, gState.carbot);  // the menus' options (core/app_settings.inc)
        loadLanguageSdl(langArg);  // the language (sdl_options.inc): the canvas translates with it from the first frame
        loadThemeSdl(themeArg, accentArg);  // the theme and the accent in the settings
        if (previewArg) gDock.scriptPreview = true;
        if (trigCodeArg) gDock.triggerCode = true;
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
        // Files dropped on the window, and on macOS the maps that Finder opens with StarDraft: as the Windows program's
        // drop (core/documents.inc), after the save question for the open map (SDL has one tab).
        host.setDropHook([](const std::string& path) { shellPost([path] { dropFile(path); }); });
        host.setBeforePump(runPosted);  // the posted work runs outside the frame hook (a dialog it opens pumps frames)
        host.setFrameHook([](SDL_GPUCommandBuffer* cmd) { drawMapOfRoot(cmd); });
        if (!mapArg.empty()) openAny(widen(mapArg));  // a map, or a project (.eds, .stardraft, its folder): as File > Open does
        bool openLast = false;  // Program options > Open the last project on start (as the Win32 program)
        if (restartPid) restartReopen();  // what the old program had open (the setting Reopen: core/documents.inc)
        else if (mapArg.empty() && dropArg.empty() && !gSilent && prefs::flag(L"OpenLastProject", openLast) && openLast && !gRecentProjects.empty()) {
            std::error_code ec;
            if (fs::exists(gRecentProjects.front(), ec)) openAny(gRecentProjects.front());
        }
        if (!dropArg.empty()) dropFile(dropArg);
        if (untitledArg) newUntitledProject();
        if (!newProjectArg.empty()) {  // (as the Windows datashot project:)
            std::string err;
            const bool was = std::exchange(gSelfTest, true);  // (the user's recent lists stay as they are)
            const bool made = gDataOk && createProject(fs::path(widen(newProjectArg)), "Sample", false, err, {});
            gSelfTest = was;
            std::printf("new project: %s %s\n", made && activeProject() ? "made (yes)" : "not made (WRONG)", err.c_str());
        }
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
        if (!palViewArg.empty()) sdlPaletteViewShot(palViewArg);  // (not saved: a snapshot saves no settings)
        if (previewArg && !gProj.sections.empty()) triggersRefresh(gProj.sections.back());  // the last script in the preview (as scriptpreview:)
        if (!dataArg.empty()) sdlDataSelect(dataArg);
        if (!ctArg.empty()) sdlClassicTrigSelect(ctArg);
        if (!trigArg.empty() || !trigSelArg.empty()) sdlTrigSelect(trigArg, trigSelArg);
        if (trigCodeArg) trigCodeRefresh(true);
        if (!briefArg.empty()) sdlBriefingSelect(briefArg);
        if (newPageArg == "classic" || newPageArg == "eud") openNewPageSdl(newPageArg == "eud" ? 1 : 0);
        if (buildArg) snapshot.empty() ? command(ID_PROJ_COMPILE) : sdlBuildShot();  // (a snapshot: until the build ends, sdl_build.inc)
        if (logArg) gLogPane.setExpanded(true);
        if (!gDataOk) setMessage(widen(gDataError));
        std::printf("stardraft_editor: GPU %s, %s\n", renderer.driver(), gDataOk ? "game data" : gDataError.c_str());

        if (!snapshot.empty()) {
            // A map's dialog (--dialog unitprops|locprops|prompt|quick, sdl_map_dialogs.inc) is the picture at its own size.
            MapDialogShot mapShot;
            if (mapDialogShot(dialogArg, mapShot)) width = mapShot.size.x, height = mapShot.size.y;
            // valuepop: the value pop-out (core/views_value_pop.inc) at its own size: a text with values (valuepop), a
            // number (valuepop:number), a text the game shows with its colors (valuepop:colors) or a plain text (valuepop:text).
            std::unique_ptr<ValuePopView> valueShot;
            if (dialogArg.rfind("valuepop", 0) == 0) {
                ValuePopSpec spec;
                const std::string kind = dialogArg.size() > 9 ? dialogArg.substr(9) : "";
                if (kind == "number") {
                    spec.type = "Number", spec.title = L"Amount   0\x2013" L"100", spec.min = 0, spec.max = 100, spec.initial = "25", spec.inRepeat = true;
                } else if (kind == "colors") {
                    spec.type = "Text", spec.colorsOnly = true, spec.title = L"Mission objectives";
                    spec.initial = "\x13\x04" "Destroy\x01 the \x06" "enemy base\x01.\nKeep {your} hero alive.";
                } else if (kind == "text") {
                    spec.type = "Text", spec.title = L"Output file", spec.initial = "build/bneck.scx";
                } else {
                    spec.type = "Text", spec.insertValues = true, spec.title = L"Text";
                    spec.initial = "\x03Welcome\x01, {name(current player):s}! You have {gold} minerals{{!}}\n\x13{P2:c}Blue team: {kills:x}";
                }
                valueShot = std::make_unique<ValuePopView>(spec);
                const draw::Point sz = valueShot->outerSize();
                width = sz.x, height = sz.y;
            }
            // The picture is the window's root, or the dialog's view (--dialog options|feedback|listpick) at the --size given.
            // --dialog about, message and tip take their own sizes: About's, the message box's for the Windows
            // program's msgbox: text, and the tooltip's for its text.
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
            else if (dialogArg.rfind("blockhelp:", 0) == 0) shown = &gSdlBlockHelp, gSdlBlockHelp.setBlock(dialogArg.substr(10));  // a block's help page (--size 460x600)
            else if (mapShot.view) shown = mapShot.view;
            else if (valueShot) shown = valueShot.get();
            msgbox::MessageView messageShot;
            TipView tipShot;
            if (dialogArg == "about") {
                shown = &gAboutView;
                const draw::Point sz = AboutView::size();
                width = sz.x, height = sz.y;
            } else if (dialogArg == "message") {
                shown = &messageShot;
                messageShot.b.text = L"Save changes to the map? This text is long, so that it wraps to a second line in the box.";
                messageShot.b.icon = msgbox::Icon::Question;
                messageShot.b.buttons = {{L"&Save", 0}, {L"Do &not save", 1}, {L"Cancel", 2}};
                messageShot.b.cancel = 2;
                const draw::Point sz = messageShot.layout(kit_sdl::measuringCanvas());
                width = sz.x, height = sz.y;
            } else if (dialogArg == "message:build" || dialogArg == "message:delete") {
                // Build's question for a map that StarCraft will not start (shellAskYesNo), or the Triggers' question
                // before Delete (trigAskDelete): the boxes those calls show, with the second button's focus.
                shown = &messageShot;
                std::wstring problems = playabilityText();
                if (problems.empty()) problems = L"\x2022 At least one player must be human\r\n";  // (a map that StarCraft starts: a sample problem)
                messageShot.b = dialogArg == "message:build" ? sdlYesNoBox(buildAnywayQuestion(problems), L"&Build", L"Do &not build")
                                                             : sdlTrigDeleteBox(L"Delete the trigger \"Spawn the waves\"?");
                const draw::Point sz = messageShot.layout(kit_sdl::measuringCanvas());
                width = sz.x, height = sz.y;
            } else if (dialogArg == "error") {  // an error box (shellErrorBox: the error icon and OK)
                shown = &messageShot;
                messageShot.b.text = L"Could not write /home/user/maps/(4)Bottleneck.scx: the folder is read-only.";
                messageShot.b.icon = msgbox::Icon::Error;
                messageShot.b.buttons = {{L"OK", 0}};
                const draw::Point sz = messageShot.layout(kit_sdl::measuringCanvas());
                width = sz.x, height = sz.y;
            } else if (dialogArg == "tip") {
                shown = &tipShot;
                tipShot.text = L"Zoom in (Ctrl+Plus): the map shows larger; the tooltip wraps a text this long at 360 px";
                const draw::Point sz = tipShot.measure(kit_sdl::measuringCanvas());
                width = sz.x, height = sz.y;
            }
            // A few frames: the first fills the font atlas with the glyphs the text needs; the last is the picture.
            SDL_GPUTextureCreateInfo ti{};
            ti.type = SDL_GPU_TEXTURETYPE_2D;
            ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            ti.width = uint32_t(width), ti.height = uint32_t(height), ti.layer_count_or_depth = 1, ti.num_levels = 1;
            SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &ti);
            if (!tex) return std::fprintf(stderr, "snapshot texture: %s\n", SDL_GetError()), 1;
            host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            if (!trigSelArg.empty()) sdlTrigFocusFirstValue();  // (the pane's hit areas are the first frame's)
            if (trigPopArg != -1) sdlTrigPopShot(trigPopArg), host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            if (!ctArg.empty()) sdlClassicTrigPopShot(ctArg), host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            if (!textPopArg.empty()) sdlTextPopShot(textPopArg), host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            if (!inputArgs.empty()) {  // the keys and the mouse after the first frame (the rail's items are the first frame's), then a frame more
                for (const auto& [kind, value] : inputArgs) {
                    if (kind == 'k') sdlPressKey(value);
                    else sdlMouseShot(value, kind == 'd', width, height);
                    // (a frame after each: the views lay out and the map's scene takes the change, as between a user's inputs)
                    host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
                }
                host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            }
            if (!commandArgs.empty()) {  // the menu commands after the first frame, then a frame more
                for (const std::string& c : commandArgs) sdlCommandShot(c);
                host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            }
            if (!saveAsArg.empty()) std::printf("%s\n", mapSaveRoundTrip(fs::path(widen(saveAsArg))).c_str());  // (the status bar: Saved ...)
            if (pieArg) {  // the pie after the first frame (the map is placed), then a frame more
                sdlPieShot();
                host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            }
            if (!rclickArgs.empty()) {  // the right clicks after the first frame (the panes are placed), then a frame more
                // (a dialog's title, as its window has it: Report feedback names it)
                const std::wstring title = dialogArg == "options" ? L"Program options" : dialogArg == "about" ? L"About StarDraft 2"
                                         : dialogArg.rfind("blockhelp:", 0) == 0 ? L"Block help" : L"";
                for (const std::string& r : rclickArgs) sdlRightClickShot(r, shown == &root ? nullptr : shown, title);
                host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            }
            // The map's sprites load over a few frames: frames until they are all there (as the Windows program paints
            // while images are pending).
            for (int k = 0; k < 100 && renderer.stats().imagesPending > 0; ++k)
                host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            const bool ok = writeSnapshot(device, tex, width, height, snapshot);
            std::printf("%s %s\n", ok ? "wrote" : "could not write", snapshot.c_str());
            if (!ok) status = 1;
            if (pieArg) sdlPieShotRelease();  // (after the picture: letting go of Space paints with the slice's terrain)
            if (midPanArg) sdlMidPanCheck();  // (after the picture: it moves the camera)
            if (cursorsArg) sdlCursorChecks();  // (after the picture: it changes the mode and the camera)
            if (untitledArg) {  // (the project's folder goes: a snapshot leaves nothing)
                const EudProject* pr = activeProject();
                std::printf("untitled: an unsaved project %s (%s)\n", isUnsavedProject(pr) ? "yes" : "(WRONG)", pr ? narrow(pr->folder().filename().wstring()).c_str() : "none");
                if (gActive >= 0 && gActive < int(gDocs.size())) dropUnsavedProject(gDocs[size_t(gActive)].project);
            }
            SDL_WaitForGPUIdle(device);
            SDL_ReleaseGPUTexture(device, tex);
            gPopShot.frame.reset();  // (before the root view goes)
        } else {
            if (!host.show(root, L"StarDraft", width, height)) return std::fprintf(stderr, "could not open a window\n"), 1;
            updateTitle();  // (the window exists now: its title is the open map's)
            if (!sizeArg) fitMainWindow(width, height);
#ifdef __APPLE__
            macSetupAppMenu(narrow(accessFree(shellTr(L"&About StarDraft 2"))), narrow(accessFree(shellTr(L"&Program options\x2026"))),
                            [] { shellPostCommand(ID_HELP_ABOUT); }, [] { shellPostCommand(ID_OPT_WINDOW); });
#endif
            sdlWindowsShown(testRun);  // (Windows: the dark title bar, an earlier crash's report, the file types)
            if (!testRun) sdlUpdatesAtStart();  // (a release build: a newer release says so in the status bar)
            if (!gDataOk) shellPost(askInstallFolder);  // no install was found: the window asks for its folder
            sendPendingFeedback();  // feedback that could not go before (core/feedback_send.inc)
            host.setAnimating(true);  // (the map and the posted work need frames; see shellPost)
            host.run();
        }
        // The map view's texture and the scene's state go before the device; the views before the host.
        releaseMapTexture();
        gRoot = nullptr;
        gHost = nullptr;
    }
    releasePaletteArt();  // its pictures' textures: before the device goes
    releaseDataPictures();  // the Data pictures' textures too (core/views_data_pictures.inc)
    sdlDataExtrasRelease();  // the Display section's wireframes (sdl_data_extras.inc)
    gSdlBriefing.releasePictures();  // the briefing's portraits (sdl_briefing.inc): their textures before the device goes
    kit_sdl::unloadIconAtlases();
    gRenderer = nullptr;
    gDevice = nullptr;
    renderer.shutdown();
    SDL_Quit();
    return status;
}
