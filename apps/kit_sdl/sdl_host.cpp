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
    SDL_WindowID parent = 0;       // the window this one is opened from (0: a top window)
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

// Paints `root` (a window of w x h pixels) into a list of its own (ImGui's frame must be begun). The shapes and the
// text are drawn with the font atlas's texture, which the list must name: a shape records no texture of its own.
std::unique_ptr<ImDrawList> recordView(SDL_GPUDevice* device, draw::View& root, int w, int h) {
    auto list = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
    list->_ResetForNewFrame();
    list->PushClipRect(ImVec2(0, 0), ImVec2(float(w), float(h)));  // (this window only, not the frame's size)
    list->PushTexture(ImGui::GetIO().Fonts->TexRef);
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
    ImGui_ImplSDLGPU3_PrepareDrawData(&dd, cmd);
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    clearColor(ct);
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    ImGui_ImplSDLGPU3_RenderDrawData(&dd, cmd, pass);
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

void SdlHost::setScale(float s) {
    scaleOverride_ = s > 0 ? s : 0;
    const float own = wins_.empty() ? SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) : displayScale(wins_[0]->sdl);
    applyScale(own > 0 ? own : 1.0f);
}

// The scale the windows lay out at: the program's (setScale), else the display's.
void SdlHost::applyScale(float displayScaleOfWindow) {
    const float s = scaleOverride_ > 0 ? scaleOverride_ : displayScaleOfWindow;
    if (s == scale_ && kit::theme().dpi == int(96 * s + 0.5f)) return;
    scale_ = s;
    kit::theme().dpi = int(96 * s + 0.5f);
    // The views lay out again: a resize would skip it when the pixel size is the same.
    for (const auto& w : wins_) {
        std::function<void(draw::View&)> relayout = [&](draw::View& v) {
            v.resized();
            for (draw::View* c : v.children()) relayout(*c);
        };
        relayout(*w->root);
        w->dirty = true;
    }
}

// SDL's window sizes are logical points: the pixels divided by the window's pixel density (set by the window's
// display, after it is made, so the size is set once the density is known).
void SdlHost::sizeInPixels(SDL_Window* sdl, int w, int h) {
    const float d = SDL_GetWindowPixelDensity(sdl);
    const float density = d > 0 ? d : 1.0f;
    SDL_SetWindowSize(sdl, std::max(1, toLogical(w, density)), std::max(1, toLogical(h, density)));
}

bool SdlHost::show(draw::View& root, const std::wstring& title, int w, int h) {
    std::wstring shownTitle;  // (the title in the program's language: uiText)
    SDL_Window* sdl = SDL_CreateWindow(utf8Text(uiText(title, shownTitle)).c_str(), 1, 1,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!sdl) return false;
    sizeInPixels(sdl, w, h);
    applyScale(displayScale(sdl));  // (before the layout: the views are laid out at this scale)
    auto win = makeWin(sdl, root, WindowKind::Dialog, nullptr);
    if (!claim(*win)) {
        SDL_DestroyWindow(sdl);
        return false;
    }
    SDL_StartTextInput(sdl);
    SDL_ShowWindow(sdl);
    if (!mainId_) mainId_ = win->id;
    wins_.push_back(std::move(win));
    return true;
}

void SdlHost::run() {
    while (!quit_ && !wins_.empty()) pump(true);
}

void SdlHost::snapshot(draw::View& root, SDL_GPUTexture* target, int w, int h, SDL_GPUTextureFormat format) {
    // The pipeline is made for one format: the snapshot's own while it is drawn, the windows' after it.
    const SDL_GPUTextureFormat before = format_;
    const bool switched = rendererReady_ && before != format;
    if (switched) ImGui_ImplSDLGPU3_Shutdown(), rendererReady_ = false;
    ensureRenderer(format);
    root.attach(this);
    root.setFrame(Rect(0, 0, w, h));
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60;
    io.DisplaySize = ImVec2(float(w), float(h));
    if (frameHook_) {  // before the views are recorded, as in pump() (a texture the hook makes or resizes is in place for them)
        SDL_GPUCommandBuffer* hookCmd = SDL_AcquireGPUCommandBuffer(device_);
        frameHook_(hookCmd);
        SDL_SubmitGPUCommandBuffer(hookCmd);
    }
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
    auto list = recordView(device_, root, w, h);
    ImGui::Render();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    submit(device_, *list, w, h, cmd, target);
    SDL_WaitForGPUIdle(device_);
    root.attach(nullptr);
    if (switched && before != SDL_GPU_TEXTUREFORMAT_INVALID) {
        ImGui_ImplSDLGPU3_Shutdown();
        rendererReady_ = false;
        ensureRenderer(before);
    }
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

// Takes one window out of the host. It is hidden now and destroyed after this turn's events: SDL destroys a window's
// children with it, so the children are retired before their parent (closeTree).
void SdlHost::retire(SDL_WindowID id) {
    for (size_t i = 0; i < wins_.size(); ++i) {
        if (wins_[i]->id != id) continue;
        Win& w = *wins_[i];
        if (w.root && w.router->focused()) w.router->setFocus(nullptr);  // (the view that had the focus is told)
        if (captureWin_ == id) SDL_CaptureMouse(false), captureWin_ = 0;
        SDL_HideWindow(w.sdl);
        if (w.id == modal_) modal_ = 0;
        if (w.root) w.root->attach(nullptr);
        retired_.push_back(std::move(wins_[i]));
        wins_.erase(wins_.begin() + std::ptrdiff_t(i));
        return;
    }
}

// The window `id` and the windows opened from it (and from those), children first.
void SdlHost::collectTree(SDL_WindowID id, std::vector<SDL_WindowID>& out) const {
    for (const auto& w : wins_)
        if (w->parent == id) collectTree(w->id, out);
    out.push_back(id);
}

void SdlHost::closeTree(SDL_WindowID id) {
    std::vector<SDL_WindowID> ids;
    collectTree(id, ids);
    std::vector<std::function<void()>> closed;
    std::vector<draw::View*> gone;  // (the hover hook forgets them)
    for (SDL_WindowID x : ids) {
        Win* w = findWin(x);
        if (!w) continue;
        if (w->closed) closed.push_back(std::move(w->closed));
        if (w->kind != WindowKind::Tooltip && w->root) gone.push_back(w->root);
        retire(x);
    }
    if (hover_)
        for (draw::View* v : gone) hover_(*v, {}, {}, true);
    for (auto& f : closed) f();  // (after the windows are gone: a callback may open others)
}

bool SdlHost::inModal(SDL_WindowID id) const {
    if (!modal_) return true;
    while (id) {
        if (id == modal_) return true;
        const Win* w = findWin(id);
        if (!w) return false;
        id = w->parent;
    }
    return false;
}

void SdlHost::closeWindow(draw::View& content) {
    for (const auto& w : wins_)
        if (w->root == &content) return closeTree(w->id);
}

void SdlHost::dismiss(SDL_WindowID id) { closeTree(id); }

// A click outside the pop-outs closes them. The window that was clicked and the windows it is opened from stay: a
// dialog opened from a menu item keeps its menu, and a submenu keeps its menu.
void SdlHost::dismissPopupsExcept(SDL_WindowID keep) {
    std::vector<SDL_WindowID> chain;
    for (SDL_WindowID id = keep; id;) {
        chain.push_back(id);
        const Win* w = findWin(id);
        id = w ? w->parent : 0;
    }
    std::vector<SDL_WindowID> ids;
    for (const auto& w : wins_)
        if (w->popup() && std::find(chain.begin(), chain.end(), w->id) == chain.end()) ids.push_back(w->id);
    for (SDL_WindowID id : ids) dismiss(id);  // (a window already closed with its parent is skipped)
}

void SdlHost::openWindow(draw::View& from, draw::View& content, const draw::WindowSpec& spec, std::function<void()> closed) {
    Win* parent = rootWin(from);
    if (!parent) return;
    const int width = std::max(1, spec.at.width()), height = std::max(1, spec.at.height());
    SDL_Window* sdl = nullptr;
    if (spec.kind == WindowKind::Dialog) {
        // A dialog: a window of its own, centered on its opener.
        std::wstring shownTitle;  // (uiText)
        sdl = SDL_CreateWindow(utf8Text(uiText(spec.title, shownTitle)).c_str(), 1, 1, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        if (!sdl) return;
        sizeInPixels(sdl, width, height);
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
    w->parent = parent->id;
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
    if (block && !animating_ && !anyDirty() && SDL_WaitEventTimeout(&e, 100)) dispatch(e);
    while (SDL_PollEvent(&e)) dispatch(e);
    destroyRetired();
    if (beforePump_) beforePump_();
    if (animating_)
        for (const auto& w : wins_) w->dirty = true;
    runFrameHook();
    renderDirty();
}

bool SdlHost::anyDirty() const {
    for (const auto& w : wins_)
        if (w->dirty) return true;
    return false;
}

void SdlHost::runFrameHook() {
    if (!frameHook_ || !(animating_ || anyDirty())) return;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    frameHook_(cmd);
    SDL_SubmitGPUCommandBuffer(cmd);
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
    for (Win* w : todo) lists.push_back(recordView(device_, *w->root, w->pw, w->ph));
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
    if (!inModal(wid) && isInput(e)) return;  // only the dialog that runModal shows, and what it opened, takes input
    switch (e.type) {
    case SDL_EVENT_QUIT:
        if (quitRequest_) return quitRequest_();
        quit_ = true;
        return;
    case SDL_EVENT_DROP_FILE:
        if (drop_ && !modal_ && e.drop.data && *e.drop.data) drop_(e.drop.data);
        return;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
        Win* w = findWin(wid);
        if (!w) return;
        if (wid == mainId_ && quitRequest_) return quitRequest_();
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
            applyScale(displayScale(w->sdl));  // (the views lay out at the new scale, even at the same pixel size)
            resize(*w);
        }
        for (const auto& w : wins_) w->dirty = true;
        return;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (Win* w = findWin(wid); w && hover_ && w->kind != WindowKind::Tooltip) hover_(*w->root, {}, {}, true);
        // Popups close when the program loses the keyboard focus (another program took it).
        if (!SDL_GetKeyboardFocus()) dismissPopupsExcept(0);
        return;
    case SDL_EVENT_WINDOW_EXPOSED:
        if (Win* w = findWin(wid)) w->dirty = true;
        return;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
        Win* w = findWin(wid);
        if (!w) return;
        if (hover_) hover_(*w->root, {}, {}, true);
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
        if (Win* now = findWin(wid)) {  // (the view may have closed the window)
            setCursor(now->router->cursor(now->mouse));
            if (hover_ && now->kind != WindowKind::Tooltip) hover_(*now->root, now->mouse, now->router->tip(now->mouse), false);
        }
        return;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) dismissPopupsExcept(wid);  // a click outside a pop-out closes it
        Win* w = findWin(wid);
        if (w && (e.button.button == SDL_BUTTON_X1 || e.button.button == SDL_BUTTON_X2)) {
            // The mouse's back and forward buttons, let go: the view under the mouse (or a view it is in) takes them.
            if (e.type != SDL_EVENT_MOUSE_BUTTON_UP) return;
            Point local;
            const Point at{toPixels(e.button.x, w->density), toPixels(e.button.y, w->density)};
            for (draw::View* v = w->router->root().viewAt(at, local); v; v = v->parent())
                if (v->extraButton(e.button.button == SDL_BUTTON_X1 ? 1 : 2)) break;
            return;
        }
        const int button = buttonOf(e.button.button);
        if (!w || button < 0) return;
        if (hover_ && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) hover_(*w->root, {}, {}, true);
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
        if (hover_) hover_(*w->root, {}, {}, true);
        MouseEvent m;
        m.kind = MouseKind::Wheel;
        m.pt = {toPixels(e.wheel.mouse_x, w->density), toPixels(e.wheel.mouse_y, w->density)};
        float dx = e.wheel.x, dy = e.wheel.y;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dx = -dx, dy = -dy;  // (natural scrolling: the system's)
        if (dy != 0) m.wheel = dy;
        else m.wheel = dx, m.horizontal = true;
        m.mods = modsOf(SDL_GetModState());
        w->router->mouse(m);
        return;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        Win* w = findWin(wid);
        if (!w) return;
        if (hover_ && e.type == SDL_EVENT_KEY_DOWN) hover_(*w->root, {}, {}, true);
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
        if (on) SDL_CaptureMouse(true), captureWin_ = w->id;
        else if (captureWin_ == w->id) SDL_CaptureMouse(false), captureWin_ = 0;
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
    // The captured view left: the system's capture ends too.
    if (captureWin_) {
        const Win* w = findWin(captureWin_);
        if (!w || !w->router->captured()) SDL_CaptureMouse(false), captureWin_ = 0;
    }
}

}  // namespace stardraft::kit_sdl
