#include "sdl_host.h"

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "canvas_imgui.h"
#include "imgui.h"
#include "imgui_impl_sdlgpu3.h"
#include "theme.h"

namespace stardraft::kit_sdl {

using draw::Cursor;
using draw::KeyEvent;
using draw::Key;
using draw::MouseEvent;
using draw::MouseKind;
using draw::Point;
using draw::Rect;
using draw::WindowKind;

// What a host window is: a root view, its router (focus, capture, hover), and the window's pixels.
struct SdlHost::Win {
    SDL_Window* sdl = nullptr;
    SDL_WindowID id = 0;
    draw::View* root = nullptr;
    std::unique_ptr<draw::Router> router;
    WindowKind kind = WindowKind::Dialog;
    std::function<void()> closed;  // (popups: when the user dismisses them)
    bool dirty = true;             // to paint on the next turn
    bool claimed = false;          // claimed for the GPU device (a swapchain)
    bool composing = false;        // an input method's text is not committed yet
    int pw = 0, ph = 0;            // pixels
    float density = 1;             // pixels per logical point
    Point mouse;                   // the pointer, in pixels
    bool popup() const { return kind == WindowKind::Popup || kind == WindowKind::Menu; }
    bool transient() const { return kind != WindowKind::Dialog; }
};

namespace {

// The user event that a timer pushes; its data is the timer's id.
Uint32 gTimerEvent = 0;

// Runs on SDL's timer thread: it only pushes the event; the UI thread runs the timer.
Uint32 timerFired(void* userdata, SDL_TimerID, Uint32) {
    SDL_Event e{};
    e.type = gTimerEvent;
    e.user.data1 = userdata;
    SDL_PushEvent(&e);
    return 0;  // (one shot)
}

float displayScale(SDL_Window* w) {
    const float s = SDL_GetWindowDisplayScale(w);
    return s > 0 ? s : 1.0f;
}

int toPixels(float logical, float density) { return int(std::lround(logical * density)); }
int toLogical(int pixels, float density) { return int(std::lround(float(pixels) / density)); }

draw::Key keyOf(SDL_Keycode k) {
    if (k >= 'a' && k <= 'z') return Key(k - 32);  // SDL's letters are lower case; the kit's are capitals
    if (k >= '0' && k <= '9') return Key(k);
    if (k >= SDLK_F1 && k <= SDLK_F12) return Key(int(Key::F1) + int(k - SDLK_F1));
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return Key::Enter;
    case SDLK_ESCAPE: return Key::Escape;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_TAB: return Key::Tab;
    case SDLK_SPACE: return Key::Space;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_HOME: return Key::Home;
    case SDLK_END: return Key::End;
    case SDLK_PAGEUP: return Key::PageUp;
    case SDLK_PAGEDOWN: return Key::PageDown;
    case SDLK_INSERT: return Key::Insert;
    case SDLK_DELETE: return Key::Delete;
    case SDLK_PLUS: case SDLK_KP_PLUS: return Key::Plus;
    case SDLK_MINUS: case SDLK_KP_MINUS: return Key::Minus;
    case SDLK_COMMA: return Key::Comma;
    case SDLK_PERIOD: return Key::Period;
    case SDLK_SLASH: return Key::Slash;
    case SDLK_BACKSLASH: return Key::Backslash;
    case SDLK_SEMICOLON: return Key::Semicolon;
    case SDLK_APOSTROPHE: return Key::Quote;
    case SDLK_GRAVE: return Key::Grave;
    case SDLK_LEFTBRACKET: return Key::LeftBracket;
    case SDLK_RIGHTBRACKET: return Key::RightBracket;
    case SDLK_EQUALS: return Key::Equals;
    default: return Key::None;
    }
}

unsigned modsOf(SDL_Keymod m) {
    unsigned r = 0;
    if (m & SDL_KMOD_SHIFT) r |= draw::kShift;
    if (m & SDL_KMOD_CTRL) r |= draw::kCtrl;
    if (m & SDL_KMOD_ALT) r |= draw::kAlt;
    if (m & SDL_KMOD_GUI) r |= draw::kSuper;
#if defined(__APPLE__)
    if (m & SDL_KMOD_GUI) r |= draw::kCtrl;  // Command is the kit's shortcut key on macOS
#endif
    return r;
}

// SDL's mouse button to the kit's (0 left, 1 right, 2 middle); -1 for the others.
int buttonOf(Uint8 b) {
    switch (b) {
    case SDL_BUTTON_LEFT: return 0;
    case SDL_BUTTON_RIGHT: return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    default: return -1;
    }
}

SDL_SystemCursor systemCursor(Cursor c) {
    switch (c) {
    case Cursor::Hand: return SDL_SYSTEM_CURSOR_POINTER;
    case Cursor::IBeam: return SDL_SYSTEM_CURSOR_TEXT;
    case Cursor::SizeAll: return SDL_SYSTEM_CURSOR_MOVE;
    case Cursor::SizeWE: return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case Cursor::SizeNS: return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case Cursor::No: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    case Cursor::Wait: return SDL_SYSTEM_CURSOR_WAIT;
    default: return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

// The window an event is for (0: none).
SDL_WindowID windowOf(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: return e.motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: return e.button.windowID;
    case SDL_EVENT_MOUSE_WHEEL: return e.wheel.windowID;
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: return e.key.windowID;
    case SDL_EVENT_TEXT_INPUT: return e.text.windowID;
    case SDL_EVENT_TEXT_EDITING: return e.edit.windowID;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: case SDL_EVENT_WINDOW_CLOSE_REQUESTED: case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        return e.window.windowID;
    default: return 0;
    }
}

// Events the views get from the user (a modal dialog takes these and nothing else).
bool isInput(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return true;
    default: return false;
    }
}

// The byte index of the character `chars` characters into `s` (an input method's cursor).
size_t byteIndex(const std::string& s, int chars) {
    size_t i = 0;
    for (int n = 0; n < chars && i < s.size(); ++n) i = utf8Next(s, i);
    return i;
}

// The background of the windows and the snapshots: the theme's.
void clearColor(SDL_GPUColorTargetInfo& ct) {
    const draw::Color bg = kit::theme().bg;
    ct.clear_color = {float(bg & 255) / 255, float((bg >> 8) & 255) / 255, float((bg >> 16) & 255) / 255, 1};
    ct.load_op = SDL_GPU_LOADOP_CLEAR, ct.store_op = SDL_GPU_STOREOP_STORE;
}

// Paints `root` into a list of its own (ImGui's frame must be begun).
std::unique_ptr<ImDrawList> recordView(SDL_GPUDevice* device, draw::View& root) {
    auto list = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
    list->_ResetForNewFrame();
    list->PushClipRectFullScreen();
    ImguiCanvas canvas(*list, device);
    root.paintAll(canvas);
    return list;
}

// Draws a recorded list into `target` (w x h pixels) with its own command buffer, submitted.
void submit(SDL_GPUDevice* device, ImDrawList& list, int w, int h, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target) {
    ImDrawData dd;
    dd.Valid = true;
    dd.AddDrawList(&list);
    dd.DisplayPos = ImVec2(0, 0);
    dd.DisplaySize = ImVec2(float(w), float(h));
    dd.FramebufferScale = ImVec2(1, 1);  // the list is in pixels already
    dd.Textures = &ImGui::GetPlatformIO().Textures;
    std::fprintf(stderr, "TRACE s1 cmds=%d vtx=%d idx=%d\n", int(dd.CmdLists.Size), dd.TotalVtxCount, dd.TotalIdxCount);
    for (ImTextureData* t : ImGui::GetPlatformIO().Textures) std::fprintf(stderr, "TRACE tex status=%d id=%llu\n", int(t->Status), (unsigned long long)t->TexID);
    if (dd.CmdLists.Size) for (int k = 0; k < dd.CmdLists[0]->CmdBuffer.Size; ++k) std::fprintf(stderr, "TRACE cmd %d tex=%llu\n", k, (unsigned long long)dd.CmdLists[0]->CmdBuffer[k].GetTexID());
    ImGui_ImplSDLGPU3_PrepareDrawData(&dd, cmd);
    for (ImTextureData* t : ImGui::GetPlatformIO().Textures) std::fprintf(stderr, "TRACE after prepare status=%d id=%llu\n", int(t->Status), (unsigned long long)t->TexID);
    std::fprintf(stderr, "TRACE s2\n");
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    clearColor(ct);
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    std::fprintf(stderr, "TRACE s3\n");
    ImGui_ImplSDLGPU3_RenderDrawData(&dd, cmd, pass);
    std::fprintf(stderr, "TRACE s4\n");
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    (void)device;
}

}  // namespace

// --- Life ---------------------------------------------------------------------------------------------------------

SdlHost::SdlHost(SDL_GPUDevice* device) : device_(device) {
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // (the windows' layout is the program's, not saved)
    if (!gTimerEvent) gTimerEvent = SDL_RegisterEvents(1);
}

SdlHost::~SdlHost() {
    for (auto& [id, t] : timers_) SDL_RemoveTimer(t.sdl);
    timers_.clear();
    SDL_WaitForGPUIdle(device_);
    for (auto& w : wins_) retired_.push_back(std::move(w));  // (their views are the caller's: not touched here)
    wins_.clear();
    destroyRetired();
    if (rendererReady_) ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();
    for (SDL_Cursor*& c : cursors_)
        if (c) SDL_DestroyCursor(c), c = nullptr;
}

bool SdlHost::show(draw::View& root, const std::wstring& title, int w, int h) {
    // The logical size from the primary display's scale; the window's pixels are read back below.
    const float guess = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const float s = guess > 0 ? guess : 1.0f;
    SDL_Window* sdl = SDL_CreateWindow(utf8Text(title).c_str(), std::max(1, toLogical(w, s)), std::max(1, toLogical(h, s)),
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!sdl) return false;
    auto win = makeWin(sdl, root, WindowKind::Dialog, nullptr);
    if (!claim(*win)) {
        SDL_DestroyWindow(sdl);
        return false;
    }
    scale_ = displayScale(sdl);
    kit::theme().dpi = int(96 * scale_ + 0.5f);
    SDL_StartTextInput(sdl);
    wins_.push_back(std::move(win));
    return true;
}

void SdlHost::run() {
    while (!quit_ && !wins_.empty()) pump(true);
}

void SdlHost::snapshot(draw::View& root, SDL_GPUTexture* target, int w, int h, SDL_GPUTextureFormat format) {
    std::fprintf(stderr, "TRACE a\n");
    ensureRenderer(format);
    std::fprintf(stderr, "TRACE b\n");
    root.attach(this);
    root.setFrame(Rect(0, 0, w, h));
    std::fprintf(stderr, "TRACE c\n");
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60;
    io.DisplaySize = ImVec2(float(w), float(h));
    ImGui_ImplSDLGPU3_NewFrame();
    std::fprintf(stderr, "TRACE d\n");
    ImGui::NewFrame();
    std::fprintf(stderr, "TRACE e\n");
    auto list = recordView(device_, root);
    std::fprintf(stderr, "TRACE f\n");
    ImGui::Render();
    std::fprintf(stderr, "TRACE g\n");
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    submit(device_, *list, w, h, cmd, target);
    SDL_WaitForGPUIdle(device_);
    root.attach(nullptr);
}

// --- Windows ------------------------------------------------------------------------------------------------------

std::unique_ptr<SdlHost::Win> SdlHost::makeWin(SDL_Window* sdl, draw::View& root, WindowKind kind, std::function<void()> closed) {
    auto w = std::make_unique<Win>();
    w->sdl = sdl;
    w->id = SDL_GetWindowID(sdl);
    w->root = &root;
    w->router = std::make_unique<draw::Router>(root);
    w->kind = kind;
    w->closed = std::move(closed);
    root.attach(this);
    resize(*w);
    return w;
}

bool SdlHost::claim(Win& w) {
    if (!SDL_ClaimWindowForGPUDevice(device_, w.sdl)) return false;
    SDL_SetGPUSwapchainParameters(device_, w.sdl, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
    w.claimed = true;
    ensureRenderer(SDL_GetGPUSwapchainTextureFormat(device_, w.sdl));
    return true;
}

void SdlHost::ensureRenderer(SDL_GPUTextureFormat format) {
    if (rendererReady_) return;
    ImGui_ImplSDLGPU3_InitInfo gi;
    gi.Device = device_;
    gi.ColorTargetFormat = format;
    gi.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    rendererReady_ = ImGui_ImplSDLGPU3_Init(&gi);
    format_ = format;
    if (!rendererReady_) std::fprintf(stderr, "kit_sdl: ImGui's SDL_GPU backend did not start\n");
}

void SdlHost::resize(Win& w) {
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(w.sdl, &pw, &ph);
    const float d = SDL_GetWindowPixelDensity(w.sdl);
    w.density = d > 0 ? d : 1.0f;
    w.pw = std::max(1, pw), w.ph = std::max(1, ph);
    w.root->setFrame(Rect(0, 0, w.pw, w.ph));  // (marks the window dirty)
    w.dirty = true;
}

SdlHost::Win* SdlHost::findWin(SDL_WindowID id) const {
    for (const auto& w : wins_)
        if (w->id == id) return w.get();
    return nullptr;
}

SdlHost::Win* SdlHost::rootWin(const draw::View& v) const {
    const draw::View* top = &v;
    while (top->parent()) top = top->parent();
    for (const auto& w : wins_)
        if (w->root == top) return w.get();
    return nullptr;
}

void SdlHost::retire(size_t index) {
    Win& w = *wins_[index];
    SDL_HideWindow(w.sdl);
    if (w.id == modal_) modal_ = 0;
    if (w.root) w.root->attach(nullptr);
    retired_.push_back(std::move(wins_[index]));
    wins_.erase(wins_.begin() + std::ptrdiff_t(index));
}

void SdlHost::closeWindow(draw::View& content) {
    for (size_t i = 0; i < wins_.size(); ++i)
        if (wins_[i]->root == &content) return retire(i);
}

void SdlHost::dismiss(SDL_WindowID id) {
    for (size_t i = 0; i < wins_.size(); ++i)
        if (wins_[i]->id == id) {
            std::function<void()> closed = std::move(wins_[i]->closed);
            retire(i);
            if (closed) closed();
            return;
        }
}

void SdlHost::dismissPopupsExcept(SDL_WindowID keep) {
    std::vector<SDL_WindowID> ids;
    for (const auto& w : wins_)
        if (w->popup() && w->id != keep) ids.push_back(w->id);
    for (SDL_WindowID id : ids) dismiss(id);
}

void SdlHost::openWindow(draw::View& from, draw::View& content, const draw::WindowSpec& spec, std::function<void()> closed) {
    Win* parent = rootWin(from);
    if (!parent) return;
    const int width = std::max(1, spec.at.width()), height = std::max(1, spec.at.height());
    SDL_Window* sdl = nullptr;
    if (spec.kind == WindowKind::Dialog) {
        // A dialog: a window of its own, centered on its opener.
        sdl = SDL_CreateWindow(utf8Text(spec.title).c_str(), std::max(1, toLogical(width, parent->density)),
                               std::max(1, toLogical(height, parent->density)), SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        if (!sdl) return;
        SDL_SetWindowParent(sdl, parent->sdl);
        int px = 0, py = 0, pwl = 0, phl = 0, dw = 0, dh = 0;
        SDL_GetWindowPosition(parent->sdl, &px, &py);
        SDL_GetWindowSize(parent->sdl, &pwl, &phl);
        SDL_GetWindowSize(sdl, &dw, &dh);
        SDL_SetWindowPosition(sdl, px + (pwl - dw) / 2, py + (phl - dh) / 2);
    } else {
        // A pop-out, menu or tooltip: at `spec.at` in the opener's view, in the parent window's logical points.
        const Point o = from.origin();
        const int x = toLogical(o.x + spec.at.left, parent->density), y = toLogical(o.y + spec.at.top, parent->density);
        const SDL_WindowFlags type = spec.kind == WindowKind::Tooltip ? SDL_WINDOW_TOOLTIP : SDL_WINDOW_POPUP_MENU;
        sdl = SDL_CreatePopupWindow(parent->sdl, x, y, std::max(1, toLogical(width, parent->density)),
                                    std::max(1, toLogical(height, parent->density)), type | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!sdl) return;
    }
    auto w = makeWin(sdl, content, spec.kind, std::move(closed));
    if (!claim(*w)) {
        SDL_DestroyWindow(sdl);
        return;
    }
    SDL_ShowWindow(sdl);
    if (spec.kind == WindowKind::Dialog) SDL_RaiseWindow(sdl);
    wins_.push_back(std::move(w));
}

void SdlHost::runModal(draw::View& from, draw::View& content, const draw::WindowSpec& spec) {
    draw::WindowSpec s = spec;
    s.kind = WindowKind::Dialog;
    openWindow(from, content, s, nullptr);
    Win* w = rootWin(content);
    if (!w) return;
    const SDL_WindowID id = w->id, outer = modal_;
    modal_ = id;
    SDL_SetWindowModal(w->sdl, true);
    while (!quit_ && findWin(id)) pump(true);
    modal_ = outer;
}

// --- Events and painting ------------------------------------------------------------------------------------------

void SdlHost::pump(bool block) {
    SDL_Event e;
    if (block && !anyDirty() && SDL_WaitEventTimeout(&e, 100)) dispatch(e);
    while (SDL_PollEvent(&e)) dispatch(e);
    destroyRetired();
    renderDirty();
}

bool SdlHost::anyDirty() const {
    for (const auto& w : wins_)
        if (w->dirty) return true;
    return false;
}

void SdlHost::destroyRetired() {
    if (retired_.empty()) return;
    SDL_WaitForGPUIdle(device_);  // (a closed window's swapchain may still be in use)
    for (auto& w : retired_) {
        if (w->claimed) SDL_ReleaseWindowFromGPUDevice(device_, w->sdl);
        SDL_DestroyWindow(w->sdl);
    }
    retired_.clear();
}

void SdlHost::renderDirty() {
    std::vector<Win*> todo;
    for (const auto& w : wins_)
        if (w->dirty) todo.push_back(w.get());
    if (todo.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60;
    io.DisplaySize = ImVec2(float(todo[0]->pw), float(todo[0]->ph));
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
    std::vector<std::unique_ptr<ImDrawList>> lists;
    for (Win* w : todo) lists.push_back(recordView(device_, *w->root));
    ImGui::Render();  // (ImGui's own windows: none; the lists above are the frame)
    bool missed = false;
    for (size_t i = 0; i < todo.size(); ++i) {
        Win* w = todo[i];
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        SDL_GPUTexture* tex = nullptr;
        uint32_t sw = 0, sh = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, w->sdl, &tex, &sw, &sh) || !tex) {
            SDL_SubmitGPUCommandBuffer(cmd);  // (minimized: try again on a later turn)
            missed = true;
            continue;
        }
        w->dirty = false;
        submit(device_, *lists[i], w->pw, w->ph, cmd, tex);
        if (todo.size() > 1) SDL_WaitForGPUIdle(device_);  // (the windows share one vertex buffer)
    }
    if (missed) SDL_Delay(10);
}

void SdlHost::fireTimer(int id) {
    auto it = timers_.find(id);
    if (it == timers_.end()) return;  // cancelled after it was due
    std::function<void()> run = std::move(it->second.run);
    timers_.erase(it);
    if (run) run();
}

void SdlHost::dispatch(const SDL_Event& e) {
    if (gTimerEvent && e.type == gTimerEvent) return fireTimer(int(reinterpret_cast<intptr_t>(e.user.data1)));
    const SDL_WindowID wid = windowOf(e);
    if (modal_ && wid != modal_ && isInput(e)) return;  // only the dialog that runModal shows takes input
    switch (e.type) {
    case SDL_EVENT_QUIT:
        quit_ = true;
        return;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
        Win* w = findWin(wid);
        if (!w) return;
        if (w->transient()) dismiss(wid);
        else closeWindow(*w->root);
        return;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
        if (Win* w = findWin(wid)) resize(*w);
        return;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        if (Win* w = findWin(wid)) {
            resize(*w);
            if (w->kind == WindowKind::Dialog) {
                scale_ = displayScale(w->sdl);
                kit::theme().dpi = int(96 * scale_ + 0.5f);
            }
        }
        for (const auto& w : wins_) w->dirty = true;
        return;
    case SDL_EVENT_WINDOW_EXPOSED:
        if (Win* w = findWin(wid)) w->dirty = true;
        return;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
        Win* w = findWin(wid);
        if (!w) return;
        MouseEvent m;
        m.kind = MouseKind::Leave;
        w->router->mouse(m);
        return;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        Win* w = findWin(wid);
        if (!w) return;
        w->mouse = {toPixels(e.motion.x, w->density), toPixels(e.motion.y, w->density)};
        MouseEvent m;
        m.kind = MouseKind::Move;
        m.pt = w->mouse;
        m.mods = modsOf(SDL_GetModState());
        w->router->mouse(m);
        if (Win* now = findWin(wid)) setCursor(now->router->cursor(now->mouse));  // (the view may have closed the window)
        return;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) dismissPopupsExcept(wid);  // a click outside a pop-out closes it
        Win* w = findWin(wid);
        const int button = buttonOf(e.button.button);
        if (!w || button < 0) return;
        MouseEvent m;
        m.pt = {toPixels(e.button.x, w->density), toPixels(e.button.y, w->density)};
        w->mouse = m.pt;
        m.button = button;
        m.mods = modsOf(SDL_GetModState());
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) m.kind = MouseKind::Up;
        else m.kind = e.button.clicks >= 2 ? MouseKind::DoubleClick : MouseKind::Down;
        w->router->mouse(m);
        return;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        Win* w = findWin(wid);
        if (!w) return;
        MouseEvent m;
        m.kind = MouseKind::Wheel;
        m.pt = {toPixels(e.wheel.mouse_x, w->density), toPixels(e.wheel.mouse_y, w->density)};
        if (e.wheel.y != 0) m.wheel = e.wheel.y;
        else m.wheel = e.wheel.x, m.horizontal = true;
        m.mods = modsOf(SDL_GetModState());
        w->router->mouse(m);
        return;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        Win* w = findWin(wid);
        if (!w) return;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && w->popup()) return dismiss(wid);
        KeyEvent k;
        k.key = keyOf(e.key.key);
        if (k.key == Key::None) return;
        k.down = e.type == SDL_EVENT_KEY_DOWN;
        k.repeat = e.key.repeat;
        k.mods = modsOf(e.key.mod);
        w->router->key(k);
        return;
    }
    case SDL_EVENT_TEXT_INPUT: {
        Win* w = findWin(wid);
        if (!w) return;
        if (w->composing) {  // the text is committed: the composition ends
            w->composing = false;
            w->router->compose(draw::CompositionEvent{});
        }
        w->router->text(draw::TextEvent{e.text.text ? e.text.text : ""});
        return;
    }
    case SDL_EVENT_TEXT_EDITING: {
        Win* w = findWin(wid);
        if (!w) return;
        draw::CompositionEvent c;
        c.utf8 = e.edit.text ? e.edit.text : "";
        c.cursor = int(byteIndex(c.utf8, e.edit.start));
        w->composing = !c.utf8.empty();
        w->router->compose(c);
        return;
    }
    default:
        return;
    }
}

// --- draw::Host ---------------------------------------------------------------------------------------------------

void SdlHost::invalidate(draw::View& v) {
    if (Win* w = rootWin(v)) w->dirty = true;
}

void SdlHost::focus(draw::View& v) {
    if (Win* w = rootWin(v)) w->router->setFocus(&v);
}

bool SdlHost::hasFocus(const draw::View& v) const {
    Win* w = rootWin(v);
    return w && w->router->focused() == &v;
}

void SdlHost::capture(draw::View& v, bool on) {
    if (Win* w = rootWin(v)) {
        w->router->setCapture(on ? &v : nullptr);
        SDL_CaptureMouse(on);
    }
}

Point SdlHost::mouse(const draw::View& v) const {
    Win* w = rootWin(v);
    if (!w) return {0, 0};
    const Point o = v.origin();
    return {w->mouse.x - o.x, w->mouse.y - o.y};
}

void SdlHost::setCursor(Cursor c) {
    SDL_Cursor*& cur = cursors_[int(c)];
    if (!cur) cur = SDL_CreateSystemCursor(systemCursor(c));
    if (cur) SDL_SetCursor(cur);
}

std::string SdlHost::clipboardText() {
    char* t = SDL_GetClipboardText();
    std::string s = t ? t : "";
    SDL_free(t);
    return s;
}

void SdlHost::setClipboardText(std::string_view utf8) { SDL_SetClipboardText(std::string(utf8).c_str()); }

int SdlHost::timer(int ms, std::function<void()> run) {
    const int id = ++timerSeq_;
    const SDL_TimerID sid = SDL_AddTimer(Uint32(std::max(0, ms)), timerFired, reinterpret_cast<void*>(static_cast<intptr_t>(id)));
    timers_[id] = Timer{sid, std::move(run)};
    return id;
}

void SdlHost::cancelTimer(int id) {
    auto it = timers_.find(id);
    if (it == timers_.end()) return;
    SDL_RemoveTimer(it->second.sdl);
    timers_.erase(it);
}

void SdlHost::textInput(draw::View& v, bool on, Rect caret) {
    Win* w = rootWin(v);
    if (!w) return;
    if (!on) {
        SDL_StopTextInput(w->sdl);
        return;
    }
    const Point o = v.origin();
    SDL_Rect r{};
    r.x = toLogical(o.x + caret.left, w->density), r.y = toLogical(o.y + caret.top, w->density);
    r.w = std::max(1, toLogical(caret.width(), w->density)), r.h = std::max(1, toLogical(caret.height(), w->density));
    SDL_SetTextInputArea(w->sdl, &r, 0);
    SDL_StartTextInput(w->sdl);
}

void SdlHost::forget(const draw::View& v) {
    for (const auto& w : wins_) w->router->forget(v);
}

}  // namespace stardraft::kit_sdl
