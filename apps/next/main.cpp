// stardraft-next: the spike of the new shell (research/ports/PLAN.md, phase 4). One SDL3 window on every system,
// Dear ImGui with StarDraft's kit look, and the map renderer drawing into a texture the UI shows, all with one GPU
// device. Two workspaces: the map view and a classic map's triggers. The Win32 program is not changed by it.
//
// usage: stardraft-next [map] [--install <dir>] [--light]
//        stardraft-next [map] --snapshot <out.png> [--size <w>x<h>] [--workspace map|triggers] [--type <text>] [--scale <s>]
//                       [--ime-inline [--compose <text>]]
//
// --ime-inline: the program draws the IME's text being composed at the caret (SDL_HINT_IME_IMPLEMENTED_UI), not the
// system; --compose sends that text as the system's composition events would (a test of the drawing).
// --snapshot draws a few frames without showing a window (no focus taken) and writes the last one; --type types the
// text into the triggers' search field first (as text input events). With SDL_VIDEODRIVER=offscreen it needs no
// display (GitHub runs it so).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_internal.h"  // (the text field's caret for the IME: GImGui->PlatformImeData)
#include "kit.h"
#include "stardraft/classic_triggers.h"
#include "stardraft/commands.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/renderer.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/units.h"

namespace fs = std::filesystem;
using namespace stardraft;

namespace {

// --- The game, the map, the view ----------------------------------------------------------------------------------

enum class Workspace { Map, Triggers };

struct TriggerRow {
    std::string players, first, search;  // search: every action's text, lower case
};

struct App {
    SDL_Window* window = nullptr;
    render::Renderer renderer;
    SDL_GPUDevice* device = nullptr;
    GameData data;
    bool haveData = false;
    std::string dataError;
    UnitSprites sprites;
    std::unique_ptr<Editor> ed;
    std::string mapError;
    Workspace workspace = Workspace::Map;
    // The map view: its texture (the renderer draws into it) and camera.
    SDL_GPUTexture* view = nullptr;
    uint32_t viewW = 0, viewH = 0;
    render::Camera camera{0, 0, 1};
    bool fitPending = true;
    bool showGrid = false;
    GraphicsQuality quality = GraphicsQuality::HD2;
    bool carbot = false;
    // Triggers, and the list's texts (made when a map opens) and the rows the search shows
    ClassicTriggers triggers;
    std::vector<TriggerRow> rows;
    std::vector<int> shown;
    std::string shownFor = "\x01";
    int selectedTrigger = 0;
    char search[256] = "";
    char notes[4096] = "";
    std::string status;
    std::string fonts;
    bool imeInline = false;
    std::string composition;  // the IME's text being composed (--ime-inline)
    float scale = 1;
    Commands commands;
    bool quit = false;
    // A file chosen in SDL's file dialog (its callback can come on another thread).
    std::mutex dialogMutex;
    std::optional<fs::path> chosenFile;
};
App app;

// A UTF-8 text as a path (fs::u8path is deprecated in C++20).
fs::path utf8Path(const std::string& s) { return fs::path(reinterpret_cast<const char8_t*>(s.c_str())); }

// A map text as UTF-8: valid UTF-8 stays; other bytes (old maps' Windows code page) are read as Latin-1.
std::string mapText(const std::string& s) {
    size_t i = 0;
    bool valid = true;
    while (i < s.size() && valid) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) valid = false;
        for (size_t k = 1; valid && k < n; ++k) valid = (static_cast<unsigned char>(s[i + k]) >> 6) == 2;
        i += n ? n : 1;
    }
    if (valid) return s;
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x80) out += char(c);
        else out += char(0xC0 | c >> 6), out += char(0x80 | (c & 0x3F));
    }
    return out;
}

// The install folder: --install, STARDRAFT_INSTALL, the Windows program's choice (install.txt), the usual places.
fs::path findInstall(const std::string& given) {
    std::vector<fs::path> candidates;
    if (!given.empty()) candidates.push_back(given);
    if (const char* e = std::getenv("STARDRAFT_INSTALL"); e && *e) candidates.push_back(e);
#ifdef _WIN32
    if (const char* a = std::getenv("APPDATA")) {
        std::ifstream f(fs::path(a) / "StarDraft" / "install.txt");
        std::string line;
        if (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            candidates.push_back(utf8Path(line));
        }
    }
    candidates.push_back("C:\\Program Files (x86)\\Starcraft Remastered\\StarCraft");
    candidates.push_back("C:\\Program Files (x86)\\StarCraft");
#elif defined(__APPLE__)
    candidates.push_back("/Applications/StarCraft");
    if (const char* h = std::getenv("HOME")) candidates.push_back(fs::path(h) / "Applications" / "StarCraft");
#else
    if (const char* h = std::getenv("HOME")) {
        candidates.push_back(fs::path(h) / "scmirror");
        candidates.push_back(fs::path(h) / ".wine/drive_c/Program Files (x86)/StarCraft");
        candidates.push_back(fs::path(h) / "Games/battlenet/drive_c/Program Files (x86)/StarCraft");
    }
#endif
    std::error_code ec;
    for (const fs::path& p : candidates)
        if (fs::exists(p / ".build.info", ec)) return p;
    return {};
}

void openMap(const fs::path& path) {
    if (!app.haveData) {
        app.mapError = "No StarCraft: Remastered install: start with --install <folder>";
        return;
    }
    auto ed = std::make_unique<Editor>(app.data);
    std::string err;
    if (!ed->open(path, err)) {
        app.mapError = path.filename().string() + ": " + err;
        return;
    }
    app.ed = std::move(ed);
    app.mapError.clear();
    app.renderer.setQuality(app.quality), app.renderer.setCarbot(app.carbot);
    app.renderer.setSources(&app.data, &app.sprites, &app.ed->tileset(), app.ed->era());
    app.renderer.updateTerrain(app.ed->terrain());
    app.triggers = readClassicTriggers(app.ed->chk());
    app.rows.clear(), app.shown.clear(), app.shownFor = "\x01";
    app.selectedTrigger = 0;
    app.fitPending = true;
    app.status = "Opened " + path.filename().string() + " (" + std::to_string(app.ed->width()) + " x " + std::to_string(app.ed->height()) + ")";
    SDL_SetWindowTitle(app.window, (path.filename().string() + " - StarDraft 2 (next)").c_str());
}

void fitMap() {
    if (!app.ed || !app.viewW || !app.viewH) return;
    const float mw = app.ed->width() * 32.0f, mh = app.ed->height() * 32.0f;
    app.camera.zoom = std::min(app.viewW / mw, app.viewH / mh);
    app.camera.x = (mw - app.viewW / app.camera.zoom) / 2, app.camera.y = (mh - app.viewH / app.camera.zoom) / 2;
}

void zoomAt(float factor, float px, float py) {
    const float wx = app.camera.x + px / app.camera.zoom, wy = app.camera.y + py / app.camera.zoom;
    app.camera.zoom = std::clamp(app.camera.zoom * factor, 1.0f / 64, 8.0f);
    app.camera.x = wx - px / app.camera.zoom, app.camera.y = wy - py / app.camera.zoom;
}

render::Scene buildScene() {
    render::Scene scene;
    scene.camera = app.camera;
    if (!app.ed) return scene;
    const UnitTypes& types = app.ed->unitTypes();
    for (const Unit& u : app.ed->units()) {
        if (u.type >= types.size()) continue;
        render::SpriteDraw s;
        s.image = app.sprites.imageFor(u.type);
        if (s.image < 0) continue;
        s.frame = app.sprites.defaultFrame(u.type, s.image, s.flip);
        s.x = u.x, s.y = u.y, s.owner = u.owner;
        s.air = types[u.type].is(UnitFlag::Flyer) && !types[u.type].is(UnitFlag::Building);
        scene.sprites.push_back(s);
    }
    if (app.showGrid) {
        const float mw = app.ed->width() * 32.0f, mh = app.ed->height() * 32.0f;
        for (float x = 0; x <= mw; x += 32) scene.rects.push_back({x, 0, x, mh, 0xFFFFFF30, false, 1, true, true});
        for (float y = 0; y <= mh; y += 32) scene.rects.push_back({0, y, mw, y, 0xFFFFFF30, false, 1, true, true});
    }
    return scene;
}

// --- Commands: the same table type as the Win32 program (stardraft/commands.h) ----------------------------------------

void openFileDialog();

void registerCommands() {
    auto add = [](const char* id, const char* label, const char* menu, const char* key, std::function<void()> run, std::function<bool()> checked = {},
                  bool radio = false) {
        Command c;
        c.id = id, c.label = label, c.menu = menu, c.shortcut = key, c.run = std::move(run), c.checked = std::move(checked), c.radio = radio;
        app.commands.add(std::move(c));
    };
    add("file.open", "&Open...", "File", "Ctrl+O", [] { openFileDialog(); });
    add("file.exit", "E&xit", "File", "", [] { app.quit = true; });
    auto graphics = [](GraphicsQuality q, bool carbot) {
        return [q, carbot] {
            app.quality = q, app.carbot = carbot;
            app.renderer.setQuality(q), app.renderer.setCarbot(carbot);
        };
    };
    add("view.hd2", "&High definition (2x, default)", "View", "", graphics(GraphicsQuality::HD2, false),
        [] { return !app.carbot && app.quality == GraphicsQuality::HD2; }, true);
    add("view.hd", "High definition (&4x)", "View", "", graphics(GraphicsQuality::HD, false),
        [] { return !app.carbot && app.quality == GraphicsQuality::HD; }, true);
    add("view.sd", "&Standard definition", "View", "", graphics(GraphicsQuality::SD, false),
        [] { return !app.carbot && app.quality == GraphicsQuality::SD; }, true);
    add("view.carbot", "&Carbot (cartooned)", "View", "", graphics(GraphicsQuality::HD, true), [] { return app.carbot; }, true);
    add("view.grid", "Show &grid", "View", "G", [] { app.showGrid = !app.showGrid; }, [] { return app.showGrid; });
    add("view.zoomIn", "Zoom &in", "View", "+", [] { zoomAt(1.25f, app.viewW / 2.0f, app.viewH / 2.0f); });
    add("view.zoomOut", "Zoom &out", "View", "-", [] { zoomAt(0.8f, app.viewW / 2.0f, app.viewH / 2.0f); });
    add("view.actualSize", "&Actual size", "View", "Ctrl+0", [] { zoomAt(1 / app.camera.zoom, app.viewW / 2.0f, app.viewH / 2.0f); });
    add("view.fit", "&Fit map", "View", "Ctrl+F", [] { fitMap(); });
    add("view.light", "&Light theme", "View", "", [] { next::kit::applyTheme(!next::kit::lightTheme(), app.scale); },
        [] { return next::kit::lightTheme(); });
    add("ws.map", "&Map workspace", "View", "Ctrl+1", [] { app.workspace = Workspace::Map; }, [] { return app.workspace == Workspace::Map; }, true);
    add("ws.triggers", "&Triggers workspace", "View", "Ctrl+3", [] { app.workspace = Workspace::Triggers; },
        [] { return app.workspace == Workspace::Triggers; }, true);
}

// A shortcut text as ImGui's key chord (0: none).
ImGuiKeyChord keyChord(const std::string& text) {
    const auto k = parseShortcut(text);
    if (!k) return 0;
    ImGuiKey key = ImGuiKey_None;
    const std::string& n = k->key;
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') key = ImGuiKey(ImGuiKey_A + (n[0] - 'A'));
    else if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') key = ImGuiKey(ImGuiKey_0 + (n[0] - '0'));
    else if (n == "+") key = ImGuiKey_Equal;
    else if (n == "-") key = ImGuiKey_Minus;
    else if (n == "Del") key = ImGuiKey_Delete;
    else if (n.size() > 1 && n[0] == 'F') key = ImGuiKey(ImGuiKey_F1 + std::atoi(n.c_str() + 1) - 1);
    if (key == ImGuiKey_None) return 0;
    return key | (k->ctrl ? ImGuiMod_Ctrl : 0) | (k->shift ? ImGuiMod_Shift : 0) | (k->alt ? ImGuiMod_Alt : 0);
}

// The menu text without the '&' (ImGui has no access keys).
std::string menuLabel(const std::string& label) {
    std::string out;
    for (char c : label)
        if (c != '&') out += c;
    return out;
}

void commandMenu(const char* menu) {
    if (!ImGui::BeginMenu(menu)) return;
    for (const Command* c : app.commands.inMenu(menu))
        if (ImGui::MenuItem(menuLabel(c->label).c_str(), c->shortcut.empty() ? nullptr : c->shortcut.c_str(), c->isChecked(), c->canRun())) c->run();
    ImGui::EndMenu();
}

void commandKeys() {
    if (ImGui::GetIO().WantTextInput) return;  // typing in a field
    for (const Command& c : app.commands.all())
        if (const ImGuiKeyChord k = keyChord(c.shortcut); k && ImGui::IsKeyChordPressed(k) && c.canRun()) c.run();
}

void openFileDialog() {
    static const SDL_DialogFileFilter filters[] = {{"StarCraft maps", "scm;scx;chk"}};
    SDL_ShowOpenFileDialog(
        [](void*, const char* const* files, int) {
            if (!files || !files[0]) return;
            std::lock_guard<std::mutex> lock(app.dialogMutex);
            app.chosenFile = utf8Path(files[0]);
        },
        nullptr, app.window, filters, 1, nullptr, false);
}

// --- The workspaces -----------------------------------------------------------------------------------------------

void mapWorkspace() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    const uint32_t w = uint32_t(std::max(1.0f, avail.x * scale.x)), h = uint32_t(std::max(1.0f, avail.y * scale.y));
    if (w != app.viewW || h != app.viewH || !app.view) {
        if (app.view) SDL_ReleaseGPUTexture(app.device, app.view);
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = app.renderer.colorFormat();
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = w, ti.height = h, ti.layer_count_or_depth = 1, ti.num_levels = 1;
        app.view = SDL_CreateGPUTexture(app.device, &ti);
        app.viewW = w, app.viewH = h;
    }
    if (app.fitPending && app.ed) fitMap(), app.fitPending = false;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureRef(ImTextureID(reinterpret_cast<intptr_t>(app.view))), avail);
    if (!app.ed) {
        const char* text = app.mapError.empty() ? (app.haveData ? "File > Open a map (Ctrl+O)" : app.dataError.c_str()) : app.mapError.c_str();
        const ImVec2 ts = ImGui::CalcTextSize(text);
        ImGui::GetWindowDrawList()->AddText({origin.x + (avail.x - ts.x) / 2, origin.y + (avail.y - ts.y) / 2}, next::kit::theme().dim, text);
        return;
    }
    // Pan with the right or middle button, zoom with the wheel at the mouse. Mouse in points; the view in pixels.
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        const float mx = (io.MousePos.x - origin.x) * scale.x, my = (io.MousePos.y - origin.y) * scale.y;
        if (io.MouseWheel != 0) zoomAt(std::pow(1.2f, io.MouseWheel), mx, my);
        for (int b : {ImGuiMouseButton_Right, ImGuiMouseButton_Middle})
            if (ImGui::IsMouseDragging(b, 0)) {
                const ImVec2 d = io.MouseDelta;
                app.camera.x -= d.x * scale.x / app.camera.zoom, app.camera.y -= d.y * scale.y / app.camera.zoom;
            }
        const float wx = app.camera.x + mx / app.camera.zoom, wy = app.camera.y + my / app.camera.zoom;
        int unit = app.ed->unitAt(int(wx), int(wy));
        if (unit >= 0 && app.ed->units()[size_t(unit)].type >= app.ed->unitTypes().size()) unit = -1;  // (a type past the game's list)
        char buf[200];
        std::snprintf(buf, sizeof buf, "%.0f, %.0f (tile %d, %d)  zoom %.0f%%%s%s", wx, wy, int(wx) / 32, int(wy) / 32, app.camera.zoom * 100,
                      unit >= 0 ? "  " : "", unit >= 0 ? app.ed->unitTypes()[app.ed->units()[size_t(unit)].type].name.c_str() : "");
        app.status = buf;
    }
}

ClassicNames triggerNames() {
    ClassicNames n;
    n.unit = [](uint32_t t) { return app.ed && t < app.ed->unitTypes().size() ? app.ed->unitTypes()[uint16_t(t)].name : std::to_string(t); };
    n.location = [](uint32_t i) { return app.ed ? mapText(app.ed->locationName(int(i) - 1)) : std::to_string(i); };
    n.string = [](uint32_t id) { return app.ed ? mapText(app.ed->strings().get(id)) : std::to_string(id); };
    n.switchName = [](uint32_t i) { return "Switch " + std::to_string(i + 1); };
    n.force = [](uint32_t) { return std::string(); };
    return n;
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

void triggersWorkspace(bool focusSearch) {
    using namespace next::kit;
    const ClassicNames names = triggerNames();
    const float listW = ImGui::GetContentRegionAvail().x * 0.38f;
    ImGui::BeginChild("list", {listW, 0}, ImGuiChildFlags_Borders);
    sectionHeader("Triggers");
    if (focusSearch) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search the triggers", app.search, sizeof app.search);
    if (!app.ed) dimText("Open a classic map to see its triggers.");
    // Each trigger's texts are made once (when the map opens); the list is filtered again only when the search changes.
    if (app.rows.size() != app.triggers.triggers.size()) {
        app.rows.clear();
        for (const ClassicTrigger& t : app.triggers.triggers) {
            TriggerRow r;
            for (int g = 0; g < kClassicGroups; ++g)
                if (t.runsFor(g)) r.players += (r.players.empty() ? "" : ", ") + classicPlayerName(uint32_t(g), names);
            if (r.players.empty()) r.players = "(no player)";
            r.first = t.actionCount() ? classicActionText(t.actions[0], names) : std::string("(no actions)");
            r.search = r.players;
            for (int a = 0; a < t.actionCount(); ++a) r.search += " " + classicActionText(t.actions[size_t(a)], names);
            r.search = lower(r.search);
            app.rows.push_back(std::move(r));
        }
        app.shownFor = "\x01";  // (filter again)
    }
    if (app.shownFor != app.search) {
        const std::string needle = lower(app.search);
        app.shown.clear();
        for (int i = 0; i < int(app.rows.size()); ++i)
            if (needle.empty() || app.rows[size_t(i)].search.find(needle) != std::string::npos) app.shown.push_back(i);
        app.shownFor = app.search;
    }
    ImGuiListClipper clip;  // (only the rows that show are drawn)
    clip.Begin(int(app.shown.size()));
    while (clip.Step())
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; ++k) {
            const int i = app.shown[size_t(k)];
            const TriggerRow& r = app.rows[size_t(i)];
            const std::string label = std::to_string(i + 1) + ". " + r.players + "##t" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), app.selectedTrigger == i)) app.selectedTrigger = i;
            ImGui::SameLine(), dimText("%s", r.first.c_str());
        }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("editor", {0, 0}, ImGuiChildFlags_Borders);
    if (app.selectedTrigger >= 0 && app.selectedTrigger < int(app.triggers.triggers.size())) {
        const ClassicTrigger& t = app.triggers.triggers[size_t(app.selectedTrigger)];
        sectionHeader("Conditions");
        for (int c = 0; c < t.conditionCount(); ++c) ImGui::BulletText("%s", classicConditionText(t.conditions[size_t(c)], names).c_str());
        if (!t.conditionCount()) dimText("(none)");
        sectionHeader("Actions");
        for (int a = 0; a < t.actionCount(); ++a) ImGui::BulletText("%s", classicActionText(t.actions[size_t(a)], names).c_str());
    }
    sectionHeader("Notes (not saved: for trying text input, Korean and Chinese included)");
    ImGui::InputTextMultiline("##notes", app.notes, sizeof app.notes, {-1, ImGui::GetFontSize() * 6});
    ImGui::EndChild();
}

// The whole window: menu bar, rail, the workspace, the status bar.
void frameUi(bool focusSearch) {
    using namespace next::kit;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos), ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (ImGui::BeginMenuBar()) {
        commandMenu("File");
        commandMenu("View");
        ImGui::EndMenuBar();
    }
    const float statusH = ImGui::GetFrameHeight();
    const float railW = ImGui::GetFontSize() * 6;
    ImGui::BeginChild("rail", {railW, -statusH}, ImGuiChildFlags_None);
    ImGui::Dummy({0, 4});
    if (railItem("Map", app.workspace == Workspace::Map, railW)) app.workspace = Workspace::Map;
    if (railItem("Triggers", app.workspace == Workspace::Triggers, railW)) app.workspace = Workspace::Triggers;
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(theme().bg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, app.workspace == Workspace::Map ? ImVec2{0, 0} : ImGui::GetStyle().WindowPadding);
    ImGui::BeginChild("workspace", {0, -statusH}, app.workspace == Workspace::Map ? ImGuiChildFlags_None : ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (app.workspace == Workspace::Map) mapWorkspace();
    else triggersWorkspace(focusSearch);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SetCursorPosX(ImGui::GetStyle().ItemSpacing.x);
    dimText("%s", app.status.c_str());
    ImGui::End();
}

// The text being composed, at the caret of the field that has the focus, underlined (--ime-inline).
void drawComposition() {
    const ImGuiPlatformImeData& ime = GImGui->PlatformImeData;
    if (!app.imeInline || app.composition.empty() || !ime.WantVisible) return;
    const next::kit::Theme& t = next::kit::theme();
    ImDrawList* d = ImGui::GetForegroundDrawList();
    const ImVec2 p = ime.InputPos;  // (the caret, less 1 px)
    const float h = ime.InputLineHeight > 0 ? ime.InputLineHeight : ImGui::GetFontSize();
    // The field's font at the field's size (here, outside the window, ImGui's current size is another).
    ImFont* font = ImGui::GetFont();
    const ImVec2 size = font->CalcTextSizeA(h, FLT_MAX, 0, app.composition.c_str());
    d->AddRectFilled({p.x + 1, p.y}, {p.x + size.x + 2, p.y + h}, t.ctl);
    d->AddText(font, h, {p.x + 1, p.y}, t.text, app.composition.c_str());
    d->AddLine({p.x + 1, p.y + h}, {p.x + size.x + 1, p.y + h}, t.accent, 2);
}

// --- Drawing ------------------------------------------------------------------------------------------------------

// The map into its texture, then the UI into `target` (the window's swapchain texture or a snapshot's texture).
void drawFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target) {
    if (app.workspace == Workspace::Map && app.view) app.renderer.drawTo(cmd, app.view, app.viewW, app.viewH, buildScene());
    ImDrawData* dd = ImGui::GetDrawData();
    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
    if (!target) return;
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    ct.clear_color = {bg.x, bg.y, bg.z, 1};
    ct.load_op = SDL_GPU_LOADOP_CLEAR, ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, pass);
    SDL_EndGPURenderPass(pass);
}

bool writeSnapshot(SDL_GPUTexture* tex, SDL_GPUTextureFormat format, uint32_t w, uint32_t h, const fs::path& out) {
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, ti.size = w * h * 4;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(app.device, &ti);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(app.device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = tex, src.w = w, src.h = h, src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(app.device, true, &fence, 1);
    SDL_ReleaseGPUFence(app.device, fence);
    Image img;
    img.width = int(w), img.height = int(h);
    const uint8_t* p = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(app.device, tb, false));
    img.rgba.assign(p, p + size_t(w) * h * 4);
    SDL_UnmapGPUTransferBuffer(app.device, tb);
    SDL_ReleaseGPUTransferBuffer(app.device, tb);
    if (format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM)  // (the swapchain's usual order)
        for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) std::swap(img.rgba[i], img.rgba[i + 2]);
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    return writePng(out.string().c_str(), img);
}

}  // namespace

int main(int argc, char** argv) {
    std::string mapArg, installArg, snapshot, typeText, composeText;
    std::string workspace = "map";
    int snapW = 1280, snapH = 800;
    float scaleArg = 0;
    bool light = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--install") installArg = next();
        else if (a == "--snapshot") snapshot = next();
        else if (a == "--workspace") workspace = next();
        else if (a == "--type") typeText = next();
        else if (a == "--light") light = true;
        else if (a == "--ime-inline") app.imeInline = true;
        else if (a == "--compose") composeText = next();
        else if (a == "--scale") scaleArg = float(std::atof(next().c_str()));
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &snapW, &snapH);
        else mapArg = a;
    }
    const bool snapshotRun = !snapshot.empty();
    if (app.imeInline) SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");
    if (!SDL_Init(SDL_INIT_VIDEO)) return std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()), 1;

    // The device first (the renderer makes it with its shader formats), then the window and ImGui on it.
    std::string err;
    if (!app.renderer.createDevice(err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    app.device = app.renderer.device();
    app.scale = scaleArg > 0 ? scaleArg : snapshotRun ? 1.0f : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (app.scale <= 0) app.scale = 1;
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (snapshotRun ? SDL_WINDOW_HIDDEN : 0);
    app.window = snapshotRun ? SDL_CreateWindow("StarDraft 2 (next)", snapW, snapH, flags)
                             : SDL_CreateWindow("StarDraft 2 (next)", int(1400 * app.scale), int(860 * app.scale), flags);
    if (!app.window) return std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()), 1;
    SDL_GPUTextureFormat uiFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    if (!snapshotRun) {
        if (!SDL_ClaimWindowForGPUDevice(app.device, app.window)) return std::fprintf(stderr, "SDL_ClaimWindowForGPUDevice: %s\n", SDL_GetError()), 1;
        SDL_SetGPUSwapchainParameters(app.device, app.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
        uiFormat = SDL_GetGPUSwapchainTextureFormat(app.device, app.window);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // (the layout is the program's, not saved)
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    next::kit::applyTheme(light, app.scale);
    app.fonts = next::kit::loadFonts(app.scale);
    ImGui_ImplSDL3_InitForSDLGPU(app.window);
    ImGui_ImplSDLGPU3_InitInfo gi;
    gi.Device = app.device, gi.ColorTargetFormat = uiFormat, gi.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&gi)) return std::fprintf(stderr, "ImGui's SDL_GPU backend did not start\n"), 1;

    const fs::path install = findInstall(installArg);
    if (!install.empty() && app.data.open(install, err) && app.sprites.load(app.data, err)) app.haveData = true;
    else app.dataError = install.empty() ? "No StarCraft: Remastered install found: start with --install <folder>" : "Game data: " + err;
    registerCommands();
    app.workspace = workspace == "triggers" ? Workspace::Triggers : Workspace::Map;
    if (!mapArg.empty()) openMap(utf8Path(mapArg));
    app.status = (app.status.empty() ? std::string() : app.status + "  |  ") + "GPU: " + app.renderer.driver() + "  |  fonts: " + app.fonts;
    std::printf("stardraft-next: GPU %s, fonts %s, scale %.2f%s%s\n", app.renderer.driver(), app.fonts.c_str(), app.scale,
                app.haveData ? "" : ", no game data", app.mapError.empty() ? "" : (", " + app.mapError).c_str());

    // A snapshot: its own texture instead of the swapchain; a few frames (fonts load their glyphs as text shows).
    SDL_GPUTexture* snapTex = nullptr;
    if (snapshotRun) {
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D, ti.format = uiFormat;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        int pw = snapW, ph = snapH;
        SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
        ti.width = uint32_t(pw), ti.height = uint32_t(ph), ti.layer_count_or_depth = 1, ti.num_levels = 1;
        snapTex = SDL_CreateGPUTexture(app.device, &ti);
        if (!snapTex) return std::fprintf(stderr, "snapshot texture: %s\n", SDL_GetError()), 1;
    }
    app.renderer.setLoadBudget(snapshotRun ? 1e9 : 8);
    int frame = 0;
    bool typed = typeText.empty();
    while (!app.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(app.window))) app.quit = true;
            if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) openMap(utf8Path(e.drop.data));  // (also macOS's Finder opens)
            if (e.type == SDL_EVENT_TEXT_EDITING) app.composition = e.edit.text ? e.edit.text : "";
            if (e.type == SDL_EVENT_TEXT_INPUT) app.composition.clear();  // (committed)
        }
        {
            std::lock_guard<std::mutex> lock(app.dialogMutex);
            if (app.chosenFile) openMap(*app.chosenFile), app.chosenFile.reset();
        }
        if (!snapshotRun && (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MINIMIZED)) {
            SDL_Delay(10);
            continue;
        }
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        commandKeys();
        frameUi(snapshotRun && frame == 1 && !typed);
        drawComposition();
        ImGui::Render();
        // --type: the text arrives as the system's text input (after the search field has the focus).
        if (snapshotRun && frame == 2 && !typed) {
            SDL_Event t{};
            t.type = SDL_EVENT_TEXT_INPUT;
            t.text.windowID = SDL_GetWindowID(app.window);
            t.text.text = typeText.c_str();  // (ImGui reads it at once: the event is not queued)
            ImGui_ImplSDL3_ProcessEvent(&t);
            typed = true;
            if (!composeText.empty()) {  // then a syllable being composed, as the IME sends it
                SDL_Event c{};
                c.type = SDL_EVENT_TEXT_EDITING;
                c.edit.windowID = t.text.windowID;
                c.edit.text = composeText.c_str();
                c.edit.start = int(composeText.size()), c.edit.length = 0;
                SDL_PushEvent(&c);  // (SDL copies the text)
            }
        }
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(app.device);
        SDL_GPUTexture* target = snapTex;
        if (!snapshotRun && !SDL_WaitAndAcquireGPUSwapchainTexture(cmd, app.window, &target, nullptr, nullptr)) target = nullptr;
        drawFrame(cmd, target);
        SDL_SubmitGPUCommandBuffer(cmd);
        ++frame;
        if (snapshotRun && frame >= 6) {
            SDL_WaitForGPUIdle(app.device);
            uint32_t w = 0, h = 0;
            SDL_GetWindowSizeInPixels(app.window, reinterpret_cast<int*>(&w), reinterpret_cast<int*>(&h));
            const bool ok = writeSnapshot(snapTex, uiFormat, w, h, snapshot);
            std::printf("%s %s\n", ok ? "wrote" : "could not write", snapshot.c_str());
            if (!typeText.empty()) std::printf("search field: %s\n", app.search);
            if (!composeText.empty()) std::printf("composing: %s\n", app.composition.c_str());
            app.quit = true;
        }
    }
    SDL_WaitForGPUIdle(app.device);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();
    if (app.view) SDL_ReleaseGPUTexture(app.device, app.view);
    if (snapTex) SDL_ReleaseGPUTexture(app.device, snapTex);
    if (!snapshotRun) SDL_ReleaseWindowFromGPUDevice(app.device, app.window);
    app.ed.reset();
    app.renderer.shutdown();
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
