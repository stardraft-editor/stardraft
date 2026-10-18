// StarDraft's editor on Windows (stardraft.exe), macOS and Linux (SDL3, Dear ImGui through apps/kit_sdl). It compiles
// the portable core (core/core.inc, research/ports/MAC-BUILD-PLAN.md step 6), then the SDL shell: the host seam
// (shell_sdl.inc), the root view (sdl_main_view.inc), the functions the core still needs from the program
// (sdl_pending.inc, each marked until it is in the core) and the workspaces (sdl_*.inc).
//
// usage: stardraft_editor [map] [--install <folder>] [--size <w>x<h>] [--scale <s>]
//        stardraft_editor [map] --snapshot <out.png> [--size <w>x<h>] [--scale <s>]
//
// --snapshot: draws one frame without a window (SDL_VIDEODRIVER=offscreen needs no display), writes it, exits.
// --silent: a test run that drives the window plays no sound (a snapshot plays none either).
// --log: the Log is open; --build: Build runs first (a snapshot then shows its lines in the Log).
// --theme light|dark, --accent <n>, --lang <code>: the theme, the accent and the language for this run (not saved).
// --drop <file>: the file is dropped on the window first (as a drop, or a map that Finder opens on macOS), after
// --workspace (in the Assets workspace a sound imports); the status bar's text goes to the output.
// --check <name> (more than one --check can be given): what a part of the program set up, as text after the snapshot.
// Each part prints only for its own names and says nothing for the others: window (what the window and the look are;
// macOS: a hidden window of its own), menus (the menu bar), dock (the Dock and Finder parts on macOS).
// Windows: --send-crash <folder> sends a crash's report (the crashed program starts it); --crash-test crashes on purpose.
// --update-test <zip> [options]: Check for updates' install with a zip on this computer (sdl_updates.inc), for a test.
// A map given while the program is open goes to the open program (not in a test run: a snapshot or --silent).
// The StarCraft install: --install, then STARDRAFT_INSTALL, the folder chosen before, then the usual folders of each
// system (core/install_search.inc; on Windows Blizzard's registry entries first); when none opens, a banner on the start
// page asks for the folder (Browse).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __APPLE__
#include "mac_input.h"  // the trackpad's scrolls, smart zoom, swipes and scroll phases (mac_input.mm)
#include <dlfcn.h>  // (sdl_soak.inc: the memory of the process and of its Metal device)
#include <mach/mach.h>
#endif
#include "mac_window.h"  // macOS's document window and look (mac_window.mm; elsewhere its functions do nothing)
#include "mac_dock.h"  // the Dock, Finder and the Settings window (mac_dock.mm; on Windows and Linux they do nothing)
#include "mac_dist.h"  // macOS: Quick Look's tile colors, the maps in Spotlight, --check bundle (mac_dist.mm; elsewhere nothing)
#include "map_preview.h"  // apps/quicklook: a map's picture without a GPU (sdl_dist.inc: --check preview)
#if !defined(_WIN32) && !defined(__APPLE__)
#define STARDRAFT_XDG 1  // Linux and the BSDs: the free desktops' parts (desktop_xdg.h; sdl_xdg.inc, sdl_window.inc)
#include "desktop_xdg.h"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>  // (core/prefs.inc: std::from_chars; GCC 12 does not include it with the others)
#include <chrono>
#include <future>
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
#include "instance.h"  // apps/common: one program for all maps (shell_sdl_win.inc, sdl_xdg.inc)
#ifdef _WIN32
#include "file_types.h"  // apps/common: the files that open with StarDraft 2 (shell_sdl_win.inc)
#include <dwmapi.h>      // the dark title bar (shell_sdl_win.inc)
#include <shellapi.h>    // the elevated start (shell_sdl_win.inc)
#include <psapi.h>       // the working set (sdl_soak.inc: --script stats)
#include <dxgi1_4.h>     // the GPU memory (sdl_soak.inc: --script stats)
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
#include "link.h"   // apps/kit: the Classic trigger head's comment (core/views_classic_triggers.inc)
#include "progress_bar.h"  // apps/kit: the map image export's progress (core/map_image.inc)
#include "text_field.h"  // apps/kit: the search boxes (core/views_search.inc)
#include "rich_field.h"  // apps/kit: the game text editor (core/views_game_text.inc)
#include "dropdown.h"  // apps/kit: the palette's dropdown (core/views_search.inc)
#include "check_box.h"  // apps/kit: the players and forces page (core/views_players.inc)
#include "slider.h"  // apps/kit: the volume and the generator's sliders (core/views_options.inc, core/views_new_page.inc)
#include "list_view.h"  // apps/kit: the palette and the Locations list (sdl_panels.inc)
#include "button.h"  // apps/kit: the View button (sdl_panels.inc)
#include "tree_view.h"  // apps/kit: the Triggers workspace's tree (core/views_triggers_tree.inc)
#include "scroll_view.h"  // apps/kit: the release notes' scrolling (core/views_changelog.inc)
#include "core/html_view.h"  // the HTML view: the release notes (core/views_changelog.inc)
#include "res/ui_icons.h"
#include "stardraft/commands.h"
#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/dat_units.h"  // the converted values on the Data form (core/data_model.inc)
#include "stardraft/help.h"  // the help pages (core/views_help.inc)
#include "stardraft/campaign.h"
#include "stardraft/project_import.h"
#include "stardraft/editor.h"
#include "stardraft/eud_project.h"
#include "stardraft/eptrace.h"  // epTrace's report (core/project_page.inc)
#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/msqc.h"  // the triggers' keys and buttons (core/project_build.inc)
#include "stardraft/pathing.h"
#include "stardraft/pathing_regions.h"
#include "stardraft/png_writer.h"  // File > Export map image (core/map_image.inc, sdl_map_image.inc)
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
bool gFailDeviceArg = false;  // --fail-device: the graphics device is not made (a test of a computer without one)
// --noactivate: a test with real windows that never take the focus or come to the front (sdl_window_test.inc).
bool gNoActivate = false;
// --keysfromopener: a snapshot's pop-out gets its keys and typed text from the window under it, as SDL gives them on
// Windows (SdlHost::setSnapshotKeysFromOpener).
bool gKeysFromOpener = false;

// The portable core (the same files the Windows program includes, in the same order, core_check.cpp).
#include "core/core.inc"

// The SDL shell: the root view and the map view, the functions the core needs, the other workspaces' stand-ins, and the
// host seam (shell.h).
#include "sdl_main_view.inc"
#include "sdl_bar_frame.inc"  // a kit scroll bar beside a core view that scrolls itself (the pages and the lists)
#include "sdl_palette_float.inc"  // the floating palettes: a mode's palette in a window of its own (core/views_palette_float.inc)
#include "sdl_palette_panels.inc"  // the palette panels: palettes of their own in windows of their own, of a map tab
#include "sdl_pending.inc"
#include "sdl_menus.inc"
#include "sdl_soak.inc"  // --script: a long session's inputs from a file, the memory and the undo steps
#include "sdl_native_menus.inc"  // macOS: the menu bar and the context menus are the system's (mac_menus.mm)
#include "sdl_popshot.inc"  // a value's pop-out in a snapshot (--trigpop, --classictrig with a value)
#include "sdl_files.inc"  // the Files workspace's page and its sounds (core/views_files.inc)
#include "sdl_start.inc"  // the start page (core/views_start.inc)
#include "sdl_map_pages.inc"  // the map pages: Properties, Players and forces, Data (core/views_map_pages.inc and its views)
#include "sdl_new_page.inc"  // the New page (core/views_new_page.inc)
#include "sdl_project.inc"  // the Project workspace and the project commands (core/views_project.inc, core/project_page.inc)
#include "sdl_window.inc"  // the main window as the system's document window, the system's look (mac_window.h)
#include "sdl_options.inc"  // Program options and Report feedback (core/views_options.inc, core/views_feedback.inc)
#include "sdl_map_dialogs.inc"  // Unit and Location properties, the text prompt, the quick picker and the map's menu
#include "sdl_map_image.inc"  // File > Export map image: the picture drawn in parts with the map's renderer (core/map_image.inc)
#include "sdl_briefing.inc"  // the Briefing workspace's page (the edit model is core/views_briefing.inc)
#include "sdl_data.inc"  // the Data workspace (the model is core/data_list.inc and core/views_data*.inc; the tables are the stock .dat files)
#include "sdl_tips.inc"  // the hover tooltips (core/views_tip.inc)
#include "sdl_build.inc"  // Build's parts of this system (core/project_build.inc)
#include "sdl_triggers.inc"  // the Triggers workspace (the tree, the pane and the block picker are core/views_triggers_*.inc; the model core/triggers_model.inc)
#include "sdl_history.inc"  // Undo, Redo and Edit > Undo history (the model is core/undo_history.inc)
#include "sdl_shot_state.inc"  // a snapshot's input lines: what the workspace holds after each input
#include "sdl_updates.inc"  // Help > Check for updates, the crash reports' choice (core/releases.inc)
#include "sdl_help.inc"  // Help > Documentation and F1: the help window (core/views_help.inc)
#include "sdl_dock.inc"  // the Dock and Finder (macOS), the sounds dropped in the Assets workspace
#include "shell_sdl.inc"
#include "sdl_cursor_check.inc"  // --cursors: the window's cursors at some points, as text (a snapshot run)
#include "sdl_dist.inc"  // what a map that opens gives the system (macOS: Quick Look, Spotlight), and --check
#include "sdl_xdg.inc"  // Linux and the BSDs: the recently used files, Build's notification and progress, --check desktop
#include "shell_sdl_win.inc"  // the Windows parts: the crash handler, the map files, one program for all maps

namespace {

// The StarCraft install: --install, STARDRAFT_INSTALL, the folder the user chose before (the setting StarCraftFolder),
// then the usual folders of this system (core/install_search.inc). The first folder whose game data opens is the install.
// Returns false when none opens; the start page's banner then asks for the folder (browseInstallFolder). lastErr is the first error the
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
// The StarCraft folder that the user picked while no game data is open (the start page's Browse, or Program options):
// the game data opens from it (or from the install in it), and it is saved for the next starts. False: that folder has
// no game data.
bool useInstallFolder(const std::string& picked);
// The start page's banner when no install was found: Browse opens the folder dialog. A folder with game data opens
// it, and the banner goes; else the banner says that the folder has none, and stays.
void installFolderPicked(const std::string& picked) {
    if (picked.empty()) return;  // (cancelled: the banner stays)
    if (!useInstallFolder(picked)) {
        gStartView.installNote = L"That folder has no StarCraft game data.";
        gStartView.invalidate();
    }
}
void browseInstallFolder() { installFolderPicked(narrow(shellPickFolder(shellTr(L"Select the StarCraft folder")))); }
// A file dropped on the window, or (macOS) opened from Finder: the Windows program's drop (core/documents.inc), after the
// save question for the open map (SDL has one tab).
// In the Assets workspace a sound imports into the map (sdl_dock.inc).
void dropFile(const std::string& utf8) {
    if (sdlAssetsDrop(fs::path(widen(utf8)))) return;
    openDropped({fs::path(widen(utf8))}, [](const fs::path& f) { confirmReplace([f] { openAny(f); }); });
}

// The main window without --size: w x h in the system's points (on a Retina display of macOS 2 pixels each, as the
// Windows program's 1600 x 1000 at 100 %), at most 90 % of the display's free area, in its middle.
void fitMainWindow(int w, int h) {
    SDL_Window* win = gHost ? gHost->mainWindow() : nullptr;  // (not SDL_GetWindows' first: that is the newest window)
    SDL_Rect area{};
    if (!win || !SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(win), &area) || area.w <= 0 || area.h <= 0) return;
    SDL_SetWindowSize(win, std::min(w, area.w * 9 / 10), std::min(h, area.h * 9 / 10));
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetDisplayForWindow(win)), SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetDisplayForWindow(win)));
}

bool useInstallFolder(const std::string& picked) {
    if (picked.empty()) return false;
    std::string err;
    std::error_code ec;
    for (const fs::path& t : installTries(fs::path(widen(picked))))  // (UTF-8)
        if (fs::is_directory(t, ec) && gState.data.open(t, err)) {
            gInstallInUse = t;
            if (!gSilent) saveInstallFolder(t);  // (tests never change the user's settings)
            gDataOk = true, gDataError.clear();
            gStartView.dataError.clear(), gStartView.installMissing = false, gStartView.installNote.clear();
            if (gRoot) gRoot->relayout(), gRoot->invalidate(), gRoot->map.invalidate();
            gStartView.invalidate();
            setMessage(L"The StarCraft folder is set");
            return true;
        }
    return false;
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

#include "sdl_window_test.inc"  // --noactivate, --frames, --wintest: tests of the real window

// STARDRAFT_START_TIME=1 prints how long each stage of the start took (for speed work).
void startLap(const char* stage) {
    static const bool on = std::getenv("STARDRAFT_START_TIME") != nullptr;
    static auto last = std::chrono::steady_clock::now();
    if (!on) return;
    const auto now = std::chrono::steady_clock::now();
    std::fprintf(stderr, "  start: %-22s %7.1f ms\n", stage, std::chrono::duration<double, std::milli>(now - last).count());
    last = now;
}

int main(int argc, char** argv) {
    startLap("before main");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // --update-test <zip> [options]: the update's install with a zip on this computer (sdl_updates.inc), then the end.
    if (argc > 2 && std::string(argv[1]) == "--update-test")
        return sdlUpdateTest(fs::path(widen(argv[2])), std::vector<std::string>(argv + 3, argv + argc));
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
    bool settingsArg = false;  // --settings <file>: a snapshot reads the settings in it (gSettingsFileArg, shell_sdl.inc)
    // --check <name> (one or more times): checks as text after the snapshot. Each part of the program looks only for
    // its own names (hasCheck) and prints its lines; a name that no part knows prints a WRONG line. menus: the menu
    // bar's items and keys (sdl_native_menus.inc); window: what the window and the look are (sdl_window.inc); dock: the
    // Dock and Finder parts (sdl_dock.inc); bundle, spotlight, preview:<dir>: what a map gives the system (sdl_dist.inc);
    // settings, update, crashes, filetypes: what an update keeps (sdl_updates.inc).
    std::vector<std::string> checkArgs;
    auto hasCheck = [&checkArgs](const char* name) { return std::find(checkArgs.begin(), checkArgs.end(), name) != checkArgs.end(); };
    // --key, --type, --click, --drag, --move, --wait: keys pressed, text typed, left clicks and drags and moves of the
    // mouse before the snapshot, in their order (sdl_menus.inc: sdlShotInputNext): the option's letter (k, t, c, d, m,
    // w) and its value.
    std::vector<std::pair<char, std::string>> inputArgs;
    std::vector<std::string> rclickArgs;  // --rclick: right clicks before the snapshot (sdl_menus.inc: sdlRightClickShot)
    std::vector<std::string> commandArgs;  // --command: menu commands run before the snapshot (sdl_menus.inc: sdlCommandShot)
    std::string saveAsArg;  // --saveas: the save round-trip (core/map_commands.inc: mapSaveRoundTrip)
    std::string newProjectArg;  // --newproject: a new project in that folder (core/project_page.inc: createProject)
    int width = 1600, height = 1000;
    bool sizeArg = false;
    float scaleArg = 0;
    bool noInstallArg = false;  // --noinstall
    std::string pickInstallArg;  // --pickinstall
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--snapshot") snapshot = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &width, &height), sizeArg = true;
        else if (a == "--scale") scaleArg = float(std::atof(next().c_str()));
        else if (a == "--install") installArg = next();
        else if (a == "--noinstall") noInstallArg = true;  // no StarCraft install is searched for (the start page's banner asks for it), for a snapshot
        else if (a == "--pickinstall") pickInstallArg = next();  // a folder: the start page's Browse picks it (the folder dialog's answer), for a snapshot; "found": the folder that the search finds
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
        else if (a == "--theme") themeArg = next();  // light, dark or system (macOS: the system's): the theme for this run (not saved), for a snapshot
        else if (a == "--accent") {  // the accent's number (theme::kAccents), or system (macOS: the system's accent color), for a snapshot
            const std::string n = next();
            accentArg = n == "system" ? -2 : std::atoi(n.c_str());
        }
        else if (a == "--lang") langArg = next();  // en, ko, es, ru or zh: the language for this run (not saved), for a snapshot
        else if (a == "--drop") dropArg = next();  // a file dropped on the window (as from Finder on macOS), for a snapshot
        else if (a == "--silent") gSilent = true;
        else if (a == "--fail-device") gFailDeviceArg = true;  // the graphics device is not made, as on a computer without one (a test)
        else if (a == "--settings") gSettingsFileArg = widen(next()), settingsArg = true;  // a settings file: the run reads it as a start reads the user's (a snapshot saves nothing in it), for a snapshot
        else if (a == "--legacyhive") gLegacyHiveArg = widen(next());  // Windows: a registry file in place of HKCU for the settings of the builds before the settings file (with --settings), for a snapshot
        else if (a == "--keysfromopener") gKeysFromOpener = true;  // a snapshot: a pop-out's keys come from the window under it (as on Windows)
        else if (a == "--noactivate") gNoActivate = true;  // a real window that never takes the focus (a test: sdl_window_test.inc)
        else if (a == "--frames") gFramesArg = std::atoll(next().c_str());  // with --noactivate: the program ends after that many frames
        else if (a == "--wintest") gWinTestMap = next(), gNoActivate = true;  // a map: the real window's checks (sdl_window_test.inc)
        else if (a == "--wintest-fullscreen") gWinTestFull = true;  // with --wintest: full screen on and off too (sdl_window_test.inc)
        else if (a == "--palview") palViewArg = next();  // list, icons, sprites, normal, large (comma separated): the palette's view of the mode, for a snapshot
        else if (a == "--pie") pieArg = true;  // the pie palette (hold Space in Terrain mode) open, the mouse at a slice, for a snapshot
        else if (a == "--restart") restartPid = std::uint32_t(std::strtoul(next().c_str(), nullptr, 10));  // the old program's process id (sdl_options.inc)
        else if (a == "--untitled") untitledArg = true;  // a new project that is not saved yet (core/documents.inc), for a snapshot
        else if (a == "--midpan") midPanArg = true;  // the middle button's pan and drag of the map as text, after the snapshot
        else if (a == "--cursors") cursorsArg = true;  // the map view's and a text field's cursors as text, after the snapshot
        else if (a == "--check") checkArgs.push_back(next());  // a check's name (checkArgs): its lines after the snapshot
        else if (a == "--release") sdlSetReleaseFixture(next());  // GitHub's answer for the newest release in a file, in place of GitHub's (sdl_updates.inc)
        else if (a == "--key") inputArgs.emplace_back('k', next());  // a key (Ctrl+A, 5, Ctrl+2: Ctrl is Cmd on macOS) pressed in the window, for a snapshot
        else if (a == "--click") inputArgs.emplace_back('c', next());  // x,y[:mods]: a left click there (window pixels; mods Shift+Ctrl+Alt; tile<k>: the middle of the Tiles list's k-th slot), for a snapshot
        else if (a == "--drag") inputArgs.emplace_back('d', next());  // x0,y0-x1,y1[:mods]: a left drag from the first point to the second, for a snapshot
        else if (a == "--lostdrag") inputArgs.emplace_back('l', next());  // x0,y0-x1,y1[:mods]: a left drag, then the window loses the focus before the release (Alt+Tab), for a snapshot
        else if (a == "--move") inputArgs.emplace_back('m', next());  // x,y: the mouse moves there (no button), for a snapshot
        else if (a == "--type") inputArgs.emplace_back('t', next());  // text typed into the view with the focus (a search box, a number), for a snapshot
        else if (a == "--mapimage") gShotMapImages.push_back(next());  // the first options of the next Export map image dialog (sdl_map_image.inc), for a snapshot
        else if (a == "--pick") gShotPicks.push_back(next());  // a path, or cancel: the answer of the next file or folder dialog (none shows; the output says what it asked), for a snapshot
        else if (a == "--ime") inputArgs.emplace_back('i', next());  // text[@caret]: an input method's composition (not committed; empty: it ends) in the view with the focus, for a snapshot
        else if (a == "--wheel") inputArgs.emplace_back('h', next());  // x,y,dx,dy[:precise][:mods][:Repeat<n>]: the wheel there (dy notches away from the user), or a trackpad's scroll (precise), for a snapshot
        else if (a == "--pinch") inputArgs.emplace_back('p', next());  // x,y,scale[:Alt]: a trackpad's pinch there (scale more than 1: the fingers move apart; Alt: the brush's size), for a snapshot
        else if (a == "--smartzoom") inputArgs.emplace_back('z', next());  // x,y: a trackpad's smart zoom (macOS: a double tap with two fingers) there, for a snapshot
        else if (a == "--swipe") inputArgs.emplace_back('g', next());  // x,y,left|right: a swipe of three fingers there (macOS's Swipe between pages), for a snapshot
        else if (a == "--wait") inputArgs.emplace_back('w', next());  // ms: the time passes for the program's timers (in order with the inputs), for a snapshot
        else if (a == "--script") { if (!sdlReadScript(next(), inputArgs)) return 1; }  // file[:times]: inputs from a file, times over (sdl_soak.inc), for a snapshot
        else if (a == "--rclick") rclickArgs.push_back(next());  // x,y[:n]: a right click there; the menu's items go to the output, item n runs, for a snapshot
        else if (!a.empty() && a[0] != '-') mapArg = a;
    }
    if (!snapshot.empty()) gSilent = true, gNoBoxes = true, gShotDialogs = true;  // (a snapshot shows no file dialog: --pick answers)
    if (gNoActivate) gSilent = true, gNoBoxes = true;  // (a test: no sound, the default settings, no question waits)
    // A test run (a snapshot, --silent) asks nothing, sends nothing and leaves the user's file types.
    const bool testRun = gSilent;
    // Windows: a crash's sender ends here, the crash handler goes in, and a map for the open program goes to it.
    if (const int sent = sdlWindowsFirst(argc, argv, testRun); sent >= 0) return sent;
    if (restartPid) restartWaitFor(restartPid);  // (before the pipe: the old program still listens; core/documents.inc)
    // (an update's old program file goes once the graphics device is made, below: until then it can go back)
    // (--noactivate: the test pipe, shell_sdl_win.inc's pipeName; a snapshot never sends or listens)
    if (sdlSendToOpenProgram(mapArg, testRun && !gNoActivate)) return 0;
    if (gNoActivate) SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
#if defined(STARDRAFT_XDG)
    // The Wayland app id and X11's WM_CLASS: the .desktop file's name, so that the dock and the task bar show the
    // program's icon and name (packaging/linux), and SDL's launcher progress finds its .desktop file.
    SDL_SetAppMetadata("StarDraft 2", STARDRAFT_VERSION_NAME, stardraft::xdg::kAppId);
    SDL_SetHint(SDL_HINT_APP_ID, stardraft::xdg::kAppId);
#endif
    // A second press is a double click only within 4 points of the first (SDL's default is 32, for touch screens: two
    // quick clicks on two rows of a list or two units of the map were a double click), as the desktop systems' own
    // distance (Windows: SM_CXDOUBLECLK, 4 pixels).
    SDL_SetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_RADIUS, "4");
    // The click that activates a window (a floating palette, the main window) also acts in it: one click picks a
    // palette's item (macOS used it only to activate the window).
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#ifdef __APPLE__
    SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");  // a trackpad's scroll goes on after the fingers lift (its momentum), as in macOS's own programs
#endif
    startLap("arguments");
    if (!SDL_Init(SDL_INIT_VIDEO)) return std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()), 1;
    startLap("SDL_Init");
#ifdef __APPLE__
    // A map that the user opens from Spotlight while StarDraft is not running comes during the launch: the handler goes
    // on SDL's application delegate now (SDL_Init made it), before the first events. (The work waits for the window:
    // shellPost; not while a dialog or a sheet waits, as the Dock's and Finder's files: sdlUiWaits.)
    if (snapshot.empty()) macHandleSpotlightOpens([](const std::string& path) { shellPost([path] { if (!sdlUiWaits()) dropFile(path); }); });
#endif

    // The game data opens while the graphics device is made (the driver takes most of the start): it needs no device.
    // The settings are read once first, so the two threads only read them.
    std::string dataErr;
    std::wstring settingsRead;
    prefs::text(L"StarCraftFolder", settingsRead);
    std::future<bool> dataOpening = std::async(std::launch::async, [&] { return !noInstallArg && openInstall(installArg, dataErr); });

    // The device first (the renderer makes it with its shader formats), then the host on it.
    render::Renderer renderer;
    std::string err;
    if (gFailDeviceArg) err = "a test: no graphics device";  // (--fail-device: as a computer without Direct3D 12 or Vulkan)
    if (gFailDeviceArg || !renderer.createDevice(err)) {
        // The window cannot show: a message box says why (the program has no console). After an update, the program
        // before it goes back in its place and starts (core/releases.inc: rollBackUpdate).
        std::fprintf(stderr, "%s\n", err.c_str());
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) != "--fail-device") args.push_back(argv[i]);
        std::wstring said = releases::rollBackUpdate(stardraft::platform::programFile(), args, err);
#if defined(_WIN32)
        const wchar_t* needs = L"Direct3D 12 or Vulkan";
#elif defined(__APPLE__)
        const wchar_t* needs = L"Metal";
#else
        const wchar_t* needs = L"Vulkan";
#endif
        if (said.empty()) said = L"StarDraft 2 cannot start: it could not use the video card (" + widen(err) + L"). It needs " + needs + L".";
        std::printf("graphics: %s\n", narrow(said).c_str());
        if (!gNoBoxes) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "StarDraft 2", narrow(said).c_str(), nullptr);
        return 1;
    }
    startLap("graphics device");
    sdlRemoveOldProgram();  // (an update's old program file: the new program works here; sdl_updates.inc)
    SDL_GPUDevice* device = renderer.device();
    gRenderer = &renderer;
    gDevice = device;

    float scale = scaleArg > 0 ? scaleArg : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1;
    const char* basePath = SDL_GetBasePath();
    std::string base = basePath ? basePath : "";
#if defined(STARDRAFT_XDG)
    {  // (the files beside the program, as a build has them; an installed program has them in ../share/stardraft)
        std::error_code ec;
        const fs::path shared = fs::path(base) / ".." / "share" / "stardraft";
        if (!fs::exists(fs::path(base) / "res", ec) && fs::exists(shared / "res", ec)) base = (shared.lexically_normal() / "").string();
    }
#endif
    int status = 0;
    {
        // Order matters: the host (which makes the ImGui context the fonts go into) outlives the root view.
        kit_sdl::SdlHost host(device);
        gHost = &host;
        host.setScale(scaleArg);  // (0: the display's scale; the views lay out at it)
        host.setNoActivate(gNoActivate);
        host.setSnapshotKeysFromOpener(gKeysFromOpener);
        host.setWindowMadeHook(sdlTitleBarOfNewWindow);  // (Windows: each window's title bar in the theme)
        kit_sdl::setEmbeddedFiles(sdlEmbeddedFiles());  // (Windows: the atlases, the icon and the Noto Sans fallback are in stardraft.exe too)
        kit_sdl::setUiFontHeight(sdlUiFontHeight());  // (Windows: the system's message font size)
        startLap("host");
        std::printf("stardraft_editor: fonts %s, scale %.2f\n", kit_sdl::loadFonts(base + "fonts/").c_str(), double(scale));
        // The text sizes at this scale (the snapshots check that the small text grows with the UI's: the rail's labels)
        std::printf("stardraft_editor: text %d px, small text %d px, titles %d px\n", kit::px(15), -(shellUiFontLfHeight() * 5 / 6),
                    -(shellUiFontLfHeight() * 3 / 2));
        startLap("fonts");
        if (!kit_sdl::loadIconAtlases(device, base + "res/")) std::printf("stardraft_editor: no icon atlases in res/\n");
        startLap("icons");
        // The atlas cells of the icons at this scale (the snapshots check them: at scale 2 a 16 px icon is 32 px from the
        // 32 px atlas, and the Start page's 32 px card icon 64 px from the 64 px atlas, sharp on a Retina display).
        std::printf("stardraft_editor: icons %d px from the %d px atlas, card icons %d px from the %d px atlas\n", kit::px(16),
                    kit_sdl::iconAtlasCell(0, kit::px(16)), kit::px(32), kit_sdl::iconAtlasCell(0, kit::px(32)));
        kit_sdl::setGlyphIcons(kitpaint::glyphIcon);  // the glyphs as UI icons (no system has to have Segoe MDL2 Assets)
        if (!kit_sdl::loadResourceIcon(1, base + "res/stardraft.ico")) std::printf("stardraft_editor: no app icon in res/\n");  // (About's)
        host.setHoverHook(sdlHover);  // the views' tooltips (sdl_tips.inc)
        host.setRightClickHook(sdlWindowRightClick);  // Report feedback in the dialogs and pop-outs (sdl_menus.inc)
#ifdef __APPLE__
        if (snapshot.empty()) {  // a trackpad's scroll moves the map, a wheel zooms it: macOS tells them apart (mac_input.mm)
            macWatchScrolls();
            host.setPreciseScrollHook(macLastScrollPrecise);
            // A scroll's fingers and its momentum stay on the view it began on; the smart zoom and the swipes go to the
            // window under the mouse (mac_input.mm).
            host.setScrollContinuesHook(macScrollContinues);
            macWatchGestures([] { kit_sdl::SdlHost::pushGesture(draw::MouseKind::SmartZoom, 0); },
                             [](int dir) { kit_sdl::SdlHost::pushGesture(draw::MouseKind::Swipe, dir); });
        }
#endif

        // The game data (the map needs it), then the map from the command line.
        startLap("hooks");
        const bool dataOpened = dataOpening.get();
        startLap("game data (the rest)");
        if (dataOpened) gDataOk = true;
        else {
            gDataError = "No StarCraft install found: choose the StarCraft folder, or start with --install <folder>, or STARDRAFT_INSTALL";
            if (!dataErr.empty()) gDataError += ". Game data: " + dataErr;
            // The start page's banner asks for the folder (Browse); under the notice, the game data's own reason when
            // a folder was there but its data did not open (a damaged install).
            gStartView.installMissing = true;
            gStartView.dataError = widen(dataErr);
        }
        gStartView.onBrowseInstall = [] { shellPost(browseInstallFolder); };  // (after the click: the dialog waits for its answer)
        if (pickInstallArg == "found") {  // (a snapshot: the folder that the search finds, as if the user picked it)
            std::string ignored;
            pickInstallArg = openInstall(installArg, ignored) ? narrow(gInstallInUse.wstring()) : std::string();
        }
        if (!pickInstallArg.empty()) installFolderPicked(pickInstallArg);  // (a snapshot: the folder as Browse's dialog gives it)
        // The dock layout (core/dock_prefs.inc), the recent lists, the working folder and the unit limits in the
        // settings, as the Win32 program at its start (snapshots: the defaults).
        // (--settings: a snapshot reads the file's, and still saves nothing)
        if (!gSilent || settingsArg) loadDock(), loadRecentLists(), loadUnitLimits();
        gSettingsOff = gSilent && !settingsArg;
        if (loadSettingsTable()) shellRendererGraphics(gState.quality, gState.carbot);  // the menus' options (core/app_settings.inc)
        gSettingsOff = gSilent;  // (snapshots and tests: the defaults, and nothing saved)
        gMapPrefsOn = !gSilent || settingsArg;  // (the maps' own files: a snapshot's next to its --settings file, core/map_prefs.inc)
        // A settings file that is not JSON (a hand edit): the Log says so; its first save keeps a copy of it.
        if (const std::string& bad = prefs::store().readError(); !bad.empty()) shellLogError(widen(bad));
        cleanUnsavedProjects();  // the unsaved projects' folders of programs that ended (core/documents.inc; not in a test run)
        loadLanguageSdl(langArg);  // the language (sdl_options.inc): the canvas translates with it from the first frame
        loadThemeSdl(themeArg, accentArg);  // the theme and the accent in the settings
        if (previewArg) gDock.scriptPreview = true;
        if (trigCodeArg) gDock.triggerCode = true;
        registerMapCommands();  // the Map workspace's command table (core/map_commands.inc): the menus and keys read it
        registerWorkspaceView(Workspace::Files, gFilesFrame);  // the Assets / Files workspace (sdl_files.inc), with its scroll bar
        registerMapPages();  // the map pages' workspaces (sdl_map_pages.inc)
        registerWorkspaceView(Workspace::Briefing, gSdlBriefing);  // the Briefing workspace (sdl_briefing.inc)
        registerWorkspaceView(Workspace::Data, gSdlData);  // the Data workspace (sdl_data.inc)
        registerWorkspaceView(Workspace::Project, gSdlProject);  // the Project workspace (sdl_project.inc)
        registerWorkspaceView(Workspace::Triggers, gSdlTriggers);  // the Triggers workspace (sdl_triggers.inc)
        registerWorkspaceView(Workspace::Help, gHelpWsView);  // the Help workspace (sdl_help.inc)
        initStartPage();  // the start page's callbacks (sdl_start.inc)
        MainView root;
        gRoot = &root;
        // The menus show the shortcuts as this system does (macOS: the Mac symbols). macOS: the menu bar is the
        // system's (in a snapshot only for --check menus) and so are the context menus (a run with a window).
        kit::Menu::setShortcutText([](const std::wstring& s) { return shortcutText(s); });
        if (snapshot.empty() || hasCheck("menus")) sdlUseNativeMenus(snapshot.empty());
        gCaretCheck = hasCheck("caret");  // (the focus lines of the inputs: sdl_menus.inc)
        rebuildMenus();
        host.setQuitRequest(requestQuit);  // the window's close and the system's quit ask about unsaved changes first
        // Files dropped on the window, and on macOS the maps that Finder opens with StarDraft: as the Windows program's
        // drop (core/documents.inc), after the save question for the open map (SDL has one tab).
        host.setDropHook([](const std::string& path) { shellPost([path] { if (!sdlUiWaits()) dropFile(path); }); });
        macDockSetup(sdlDockItems, sdlDockOpen);  // macOS: the Dock icon's menu of recent maps and projects (sdl_dock.inc)
        host.setBeforePump([] {
            runPosted();  // the posted work runs outside the frame hook (a dialog it opens pumps frames)
            sdlDockWatchBuild();  // (macOS: Build's bar and badge on the Dock icon)
            sdlXdgFrame();  // (Linux: the recently used files, Build's notification and progress: sdl_xdg.inc)
            sdlWinTestPump();  // (--frames, --wintest: sdl_window_test.inc)
            sdlFloatPaletteFrame();  // (the floating palette's place as the user moves it: sdl_palette_float.inc)
            sdlPalettePanelsFrame();  // (the palette panels of the active tab show, and their places: sdl_palette_panels.inc)
        });
        host.setFrameHook([](SDL_GPUCommandBuffer* cmd) {
            drawMapOfRoot(cmd);
            sdlWindowFrame();  // (the title bar's file and edited dot, the system's look: sdl_window.inc)
        });
        // A snapshot with input: the root is in the host's window for the snapshot from the start (SdlHost::snapshotInput),
        // as a window's views are: what opens before the first frame (a page's timer that reads the map again after an
        // undo) has the host, and the focus stays from one input to the next.
        if (!snapshot.empty() && !inputArgs.empty()) host.snapshotInput(root, width, height, {});
        if (!snapshot.empty()) sdlFloatPaletteSetup();  // (a snapshot with input: the floating palette's window shows over the root)
        if (!mapArg.empty()) openAny(widen(mapArg));  // a map, or a project (.eds, .stardraft, its folder): as File > Open does
        bool openLast = false;  // Program options > Open the last project on start (as the Win32 program)
        if (restartPid) restartReopen();  // what the old program had open (the setting Reopen: core/documents.inc)
        else if (mapArg.empty() && dropArg.empty() && !gSilent && prefs::flag(L"OpenLastProject", openLast) && openLast && !gRecentProjects.empty()) {
            std::error_code ec;
            if (fs::exists(gRecentProjects.front(), ec)) openAny(gRecentProjects.front());
        }
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
        if (!workspaceArg.empty()) sdlWorkspaceByName(workspaceArg);  // (sdl_soak.inc: kWorkspaceNames)
        if (!dropArg.empty()) {  // (after the workspace: in the Assets workspace a sound imports)
            dropFile(dropArg);
            sdlFlushAssetDrops();
            std::printf("drop: %s\n", narrow(gState.message).c_str());
        }
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
        // --check: one list for all the checks. Each part of the program takes its own values (false: not its value);
        // a value that no part takes is WRONG. (A new check: one more "||" here, not one more --check option.) The
        // window's, the menu bar's and the Dock's lines come after the snapshot (hasCheck).
        sdlApplyReleaseFixture();  // (--release: the top bar's Update)
        for (const std::string& c : checkArgs) {
            std::string out;
            const bool afterSnapshot = c == "window" || c == "menus" || c == "dock" || c == "names" || c == "caret" || c == "dataform";
            if (!(sdlDistCheck(c, out) || sdlXdgCheck(c, out) || sdlFloatPaletteCheck(c, out) || sdlUpdateCheck(c, out) || sdlDataCheck(c, out) || sdlHelpCheck(c, out) || afterSnapshot)) out = "check: no check \"" + c + "\" (WRONG)\n";
            std::printf("%s", out.c_str());
        }

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
            else if (dialogArg.rfind("options:", 0) == 0) shown = &gOptionsView, gOptionsView.setTab(std::atoi(dialogArg.c_str() + 8));  // a tab of macOS's Settings window
            else if (dialogArg == "feedback") shown = &feedbackShot;
            else if (dialogArg == "newproject") shown = &projectShot;
            else if (dialogArg == "prompt") shown = &promptShot;
            else if (dialogArg == "listpick") shown = &pickShot;
            else if (dialogArg == "blockpick") shown = &blockShot;
            else if (dialogArg.rfind("blockhelp:", 0) == 0) shown = &gSdlBlockHelp, gSdlBlockHelp.setBlock(dialogArg.substr(10));  // a block's help page (--size 460x600)
            else if (mapShot.view) shown = mapShot.view;
            else if (valueShot) shown = valueShot.get();
            else if (draw::View* notes = sdlChangelogShot(dialogArg, width, height)) shown = notes;  // html:<file>, changelog, changelog:offline (sdl_updates.inc)
            else if (draw::View* help = sdlHelpShot(dialogArg, width, height)) shown = help;  // help[:<topic>], help:search:<words> (sdl_help.inc)
            else if (dialogArg.rfind("campaign:", 0) == 0) {  // campaign:<folder>: the Campaign window of that campaign (core/views_campaign.inc)
                std::string cerr;
                gCampaignOpen = gCampaign.load(fs::path(widen(dialogArg.substr(9))), cerr);
                std::printf("campaign: %s, %zu maps%s\n", gCampaignOpen ? "opened" : "not opened", gCampaign.maps.size(), cerr.empty() ? "" : (" (" + cerr + ")").c_str());
                gCampaignView.refresh();
                const draw::Point sz = CampaignView::size();
                width = sz.x, height = sz.y;
                gCampaignView.setFrame({0, 0, width, height});
                shown = &gCampaignView;
            }
            // newpreview: the New page's larger preview of a generated map (the default choices, Generate) at 1200 x 900.
            std::unique_ptr<NewPreviewView> previewShot;
            stardraft::kit::Dialog previewDlg;
            if (dialogArg == "newpreview") {
                gNewView.reset(0);
                gNewView.generate = true;
                gNewView.regenerate();
                TerrainLayer t;
                t.width = uint16_t(gNewView.tilesW), t.height = uint16_t(gNewView.tilesH), t.tiles = gNewView.tiles;
                previewShot = std::make_unique<NewPreviewView>(gNewView.tileset, nullptr, t, L"Badlands  \x00B7  128 x 128 tiles");
                newPreviewDialogSetup(previewDlg, *previewShot, {width, height});
                shown = &previewDlg, width = 1200, height = 900;
            }
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
                tipShot.text = shellKeysText(L"Zoom in (Ctrl+Plus): the map shows larger; the tooltip wraps a text this long at 360 px");  // (macOS: the Mac symbols)
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
            startLap("to the first frame");
            host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            startLap("first frame");
            if (!trigSelArg.empty()) sdlTrigFocusFirstValue();  // (the pane's hit areas are the first frame's)
            if (trigPopArg != -1) sdlTrigPopShot(trigPopArg), host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            if (!ctArg.empty()) sdlClassicTrigPopShot(ctArg), host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            if (!textPopArg.empty()) sdlTextPopShot(textPopArg), host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            if (!inputArgs.empty()) {  // the keys and the mouse after the first frame (the rail's items are the first frame's), then a frame more
                // Each key and text goes to the window that takes input now: the main window, or a dialog or pop-out
                // that an input opened (sdl_menus.inc: sdlShotTarget); the mouse to the window under its point. While a modal one shows, its loop takes the next inputs
                // (the modal hook); with none left it closes, as Esc closes it.
                // A second frame first: a view that places its parts from the last paint's size (the New page's block)
                // is where the user sees it, as a window has painted more than once before a user's first input.
                host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
                gShotInputs.assign(inputArgs.begin(), inputArgs.end());
                host.setSnapshotModalHook([](draw::View&) { return sdlShotInputNext(); });
                while (sdlShotInputNext()) {
                    // (a frame after each: the views lay out and the map's scene takes the change, as between a user's inputs)
                    host.snapshotTimers(20);
                    const auto t0 = std::chrono::steady_clock::now();
                    host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
                    sdlSoakFrame(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());  // (--script stats)
                }
                host.snapshot(*shown, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
                host.setSnapshotModalHook(nullptr);
                sdlFloatPaletteInputEnd(host, root);  // (host.snapshotInputEnd; the floating palette's window stays in the picture)
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
            if (noInstallArg || !pickInstallArg.empty())  // the start page's banner (no install found), as the picture shows it
                std::printf("start page: banner %s; note: %s; game data %s\n", gStartView.bannerShown() ? "shown" : "not shown",
                            gStartView.installNote.empty() ? "none" : narrow(gStartView.installNote).c_str(), gDataOk ? "open" : "not open");
            sdlChangelogShotLines(dialogArg, shown == &root);  // (the HTML view's layout, the release notes, the top bar's Update)
            sdlHelpShotLines(shown == &gHelpView);  // (the help window: its page, its place, its list)
            if (gOptionsView.tab() >= 0) std::printf("options tab %d: content %d px high\n", gOptionsView.tab(), gOptionsView.contentHeight());
            if (hasCheck("dock")) sdlDockCheck("dock");
            if (midPanArg) sdlMidPanCheck();  // (after the picture: it moves the camera)
            if (hasCheck("window")) sdlWindowCheck(mapArg.empty() ? snapshot : mapArg);  // (no map: the picture is the file)
            if (cursorsArg) sdlCursorChecks();  // (after the picture: it changes the mode and the camera)
            if (hasCheck("menus")) sdlMenuCheck();
            if (hasCheck("names")) sdlNamesCheck();  // (after the inputs: a map saved and opened again)
            if (hasCheck("dataform")) sdlDataFormCheck();  // (the last paint's parts)
            if (untitledArg) {  // (the project's folder goes: a snapshot leaves nothing)
                const EudProject* pr = activeProject();
                std::printf("untitled: an unsaved project %s (%s)\n", isUnsavedProject(pr) ? "yes" : "(WRONG)", pr ? narrow(pr->folder().filename().wstring()).c_str() : "none");
                if (gActive >= 0 && gActive < int(gDocs.size())) dropUnsavedProject(gDocs[size_t(gActive)].project);
            }
            SDL_WaitForGPUIdle(device);
            SDL_ReleaseGPUTexture(device, tex);
            gPopShot.frame.reset();  // (before the root view goes)
        } else {
            // The first size is already in the display's free area (fitMainWindow sets the size in points next): macOS
            // takes a window as large as the free area as zoomed, and SDL keeps that first size as the window's own
            // (after full screen it came back that large, wider than a small display).
            int showW = width, showH = height;
            if (SDL_Rect area{}; !sizeArg && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &area) && area.w > 0 && area.h > 0)
                showW = std::min(width, area.w * 9 / 10), showH = std::min(height, area.h * 9 / 10);
            if (!host.show(root, L"StarDraft", showW, showH)) return std::fprintf(stderr, "could not open a window\n"), 1;
            updateTitle();  // (the window exists now: its title is the open map's)
            if (!sizeArg) fitMainWindow(width, height);
            if (SDL_Window* mw = host.mainWindow()) sdlMainWindowShown(mw, testRun);  // (macOS: the document window, the sheets)
            sdlFloatPaletteSetup();  // (the floating palette's window opens over it, at its place: sdl_palette_float.inc)
            sdlNativeMenusReset(), rebuildMenus();  // (macOS: the system's menu bar in place of SDL's, now that the window is there)
            sdlWindowsShown(testRun);  // (Windows: the dark title bar, an earlier crash's report, the file types)
            if (!testRun) sdlHelpOnlineStart();  // (sdl_help.inc: the help pages from the web)
            if (!testRun) sdlUpdatesAtStart();  // (a release build: a newer release says so in the status bar)
            sendPendingFeedback();  // feedback that could not go before (core/feedback_send.inc)
            host.setAnimating(true);  // (the map and the posted work need frames; see shellPost)
            host.run();
            if (gWinTestWrong) status = 1;
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
