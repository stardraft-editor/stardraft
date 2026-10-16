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
    bool inputOn = false;          // a view that types text has the keyboard (textInput): the input method is on
    // SDL gives the window typed text (SDL_StartTextInput): its text events come only then. A window that takes the
    // keyboard (the main window, a dialog, a pop-out) always does: a list's or tree's type-ahead, the triggers pane's
    // typed block and a message box's letters take text too, not only a text field.
    bool typing = false;
    Rect inputArea;                // where the input method shows its text: the caret, in the window's pixels
    int pw = 0, ph = 0;            // pixels
    float density = 1;             // pixels per logical point
    Point mouse;                   // the pointer, in pixels
    std::wstring title;            // a dialog's title (openWindow's spec)
    // A snapshot's pop-out (no SDL window): the window it shows on (the main window or a dialog: the base) and its
    // place there, in the base's pixels, as the screen shows a pop-out over its window. 0: not a pop-out (a base).
    SDL_WindowID shotBase = 0;
    Rect shotAt;
    bool popup() const { return kind == WindowKind::Popup || kind == WindowKind::Menu; }
    bool transient() const { return kind != WindowKind::Dialog; }
};

namespace {

// The user event that a timer pushes; its data is the timer's id.
Uint32 gTimerEvent = 0;
// The user event of a trackpad gesture that SDL has no event for (SdlHost::pushGesture): code is the MouseKind, data1
// the swipe's direction, windowID the window (0: the one under the mouse).
Uint32 gGestureEvent = 0;

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
    case Cursor::SizeNWSE: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case Cursor::SizeNESW: return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    default: return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

// The window an event is for (0: none).
SDL_WindowID windowOf(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: return e.motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: return e.button.windowID;
    case SDL_EVENT_MOUSE_WHEEL: return e.wheel.windowID;
    case SDL_EVENT_PINCH_BEGIN: case SDL_EVENT_PINCH_UPDATE: case SDL_EVENT_PINCH_END:
        if (e.pinch.windowID) return e.pinch.windowID;
        if (SDL_Window* f = SDL_GetMouseFocus()) return SDL_GetWindowID(f);  // (macOS gives no window: the one under the mouse)
        return 0;
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: return e.key.windowID;
    case SDL_EVENT_TEXT_INPUT: return e.text.windowID;
    case SDL_EVENT_TEXT_EDITING: return e.edit.windowID;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: case SDL_EVENT_WINDOW_CLOSE_REQUESTED: case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return e.window.windowID;
    default:
        if (gGestureEvent && e.type == gGestureEvent) {
            if (e.user.windowID) return e.user.windowID;
            if (SDL_Window* f = SDL_GetMouseFocus()) return SDL_GetWindowID(f);
        }
        return 0;
    }
}

// Events the views get from the user (a modal dialog takes these and nothing else).
bool isInput(const SDL_Event& e) {
    if (gGestureEvent && e.type == gGestureEvent) return true;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_PINCH_BEGIN: case SDL_EVENT_PINCH_UPDATE: case SDL_EVENT_PINCH_END:
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
    if (!gGestureEvent) gGestureEvent = SDL_RegisterEvents(1);
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
    setTyping(*win, true);
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
    if (!shots_.empty()) {  // the menus opened in the snapshot's views, over the root at their places
        ImguiCanvas canvas(*list, device_);
        for (const Shot& s : shots_) {
            canvas.pushView(s.at);
            s.content->paintAll(canvas);
            canvas.popView();
        }
    }
    ImGui::Render();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    submit(device_, *list, w, h, cmd, target);
    SDL_WaitForGPUIdle(device_);
    if (!rootWin(root)) root.attach(nullptr);  // (a root with a snapshot's window stays in the host for its input)
    if (switched && before != SDL_GPU_TEXTUREFORMAT_INVALID) {
        ImGui_ImplSDLGPU3_Shutdown();
        rendererReady_ = false;
        ensureRenderer(before);
    }
}

// A snapshot's window: a window of the host with no SDL window (its id is one SDL does not give), kept until
// snapshotInputEnd.
SdlHost::Win* SdlHost::shotWin(draw::View& root, int w, int h) {
    if (Win* x = rootWin(root)) return root.attach(this), x;
    auto win = std::make_unique<Win>();
    win->id = ++shotSeq_;
    win->root = &root;
    win->router = std::make_unique<draw::Router>(root);
    win->kind = WindowKind::Dialog;
    win->pw = w, win->ph = h;
    win->typing = true;  // (as show(): the main window takes typed text)
    root.attach(this);
    root.setFrame(Rect(0, 0, w, h));
    wins_.push_back(std::move(win));
    return wins_.back().get();
}

void SdlHost::snapshotInput(draw::View& root, int w, int h, const std::vector<SDL_Event>& events) {
    const SDL_WindowID id = shotWin(root, w, h)->id;
    HoverHook hover = std::move(hover_);
    hover_ = nullptr;
    for (SDL_Event e : events) {
        if (e.type == SDL_EVENT_MOUSE_MOTION) e.motion.windowID = id;
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) e.button.windowID = id;
        else if (e.type == SDL_EVENT_MOUSE_WHEEL) e.wheel.windowID = id;
        else if (e.type == SDL_EVENT_PINCH_BEGIN || e.type == SDL_EVENT_PINCH_UPDATE || e.type == SDL_EVENT_PINCH_END) e.pinch.windowID = id;
        else if (gGestureEvent && e.type == gGestureEvent) e.user.windowID = id;
        else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) e.window.windowID = id;
        dispatch(e);
    }
    hover_ = std::move(hover);
    if (captureWin_ == id) SDL_CaptureMouse(false), captureWin_ = 0;
}

void SdlHost::snapshotKey(draw::View& root, int w, int h, const draw::KeyEvent& k) {
    Win* win = shotWin(root, w, h);
    if (!inModal(win->id)) return;  // (as dispatch: only the modal window and what it opened take input)
    if (k.down && k.key == Key::Escape && win->popup()) return dismiss(win->id);
    win->router->key(k);
}

// A trackpad's scroll or a wheel's notches (setPreciseScrollHook). Only the system can tell them apart: a smooth or
// high-resolution wheel also gives fractions (Windows, X11, Wayland). With no hook, each scroll is a wheel's.
bool SdlHost::preciseScroll(const SDL_MouseWheelEvent&) { return precise_ && precise_(); }

void SdlHost::snapshotText(draw::View& root, int w, int h, const std::string& utf8) {
    Win* win = shotWin(root, w, h);
    SDL_Event e{};  // (as SDL gives typed text: dispatch ends a composition first)
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.windowID = win->id;
    e.text.text = utf8.c_str();
    dispatch(e);
}

void SdlHost::snapshotCompose(draw::View& root, int w, int h, const std::string& utf8, int start) {
    Win* win = shotWin(root, w, h);
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_EDITING;
    e.edit.windowID = win->id;
    e.edit.text = utf8.c_str();
    e.edit.start = start;
    dispatch(e);
}

void SdlHost::snapshotLayout(const draw::View& v) {
    if (Win* w = rootWin(v); w && !w->sdl) paintShot(*w);
}

bool SdlHost::textInputState(const draw::View& v, Rect& area) const {
    const Win* w = rootWin(v);
    if (!w) return false;
    area = w->inputArea;
    return w->inputOn;
}

draw::View* SdlHost::focusedView(const draw::View& v) const {
    const Win* w = rootWin(v);
    return w ? w->router->focused() : nullptr;
}

void SdlHost::snapshotMouse(draw::View& root, const std::vector<SDL_Event>& events) {
    Win* base = shotWin(root, root.width(), root.height());
    // The window that the mouse is on, as on a screen: a modal dialog (it is over the main window and takes the
    // input), else the main window; over it, the last pop-out opened that has the first point.
    for (auto it = wins_.rbegin(); it != wins_.rend() && it->get() != base; ++it)
        if (!(*it)->sdl && (*it)->kind == WindowKind::Dialog) {
            base = it->get();
            break;
        }
    Point at;
    for (const SDL_Event& e : events)
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            at = {int(e.motion.x), int(e.motion.y)};
            break;
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            at = {int(e.wheel.mouse_x), int(e.wheel.mouse_y)};
            break;
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            at = {int(e.button.x), int(e.button.y)};
            break;
        }
    Win* target = base;
    for (auto it = wins_.rbegin(); it != wins_.rend() && it->get() != base; ++it) {
        Win& w = **it;
        if (w.sdl || w.kind == WindowKind::Tooltip || w.shotBase != base->id || !w.shotAt.contains(at)) continue;
        target = &w;
        break;
    }
    const Point o = target == base ? Point{0, 0} : Point{target->shotAt.left, target->shotAt.top};
    std::vector<SDL_Event> moved = events;
    for (SDL_Event& e : moved)
        if (e.type == SDL_EVENT_MOUSE_MOTION) e.motion.x -= float(o.x), e.motion.y -= float(o.y);
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) e.button.x -= float(o.x), e.button.y -= float(o.y);
        else if (e.type == SDL_EVENT_MOUSE_WHEEL) e.wheel.mouse_x -= float(o.x), e.wheel.mouse_y -= float(o.y);
    // A pop-out that is not modal has no loop that paints it: a paint lays it out (its hit areas) as the screen shows it.
    if (target->root != &root) paintShot(*target);
    snapshotInput(*target->root, target->pw, target->ph, moved);
}

draw::View& SdlHost::snapshotTop(draw::View& root, Point& size) {
    for (auto it = wins_.rbegin(); it != wins_.rend(); ++it) {
        const Win& w = **it;
        if (w.sdl || w.kind == WindowKind::Tooltip) continue;
        if (w.root == &root) break;
        size = {w.pw, w.ph};
        return *w.root;
    }
    size = {root.width(), root.height()};
    return root;
}

void SdlHost::snapshotTimers(int ms) {
    const Uint64 end = SDL_GetTicks() + Uint64(std::max(0, ms));
    do {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (gTimerEvent && e.type == gTimerEvent) dispatch(e);  // (the snapshot has no windows: nothing else is for it)
        if (SDL_GetTicks() >= end) break;
        SDL_Delay(1);
    } while (true);
}

void SdlHost::snapshotInputEnd() {
    std::vector<SDL_WindowID> ids;
    for (const auto& w : wins_)
        if (!w->sdl && !w->parent) ids.push_back(w->id);
    for (SDL_WindowID id : ids) closeTree(id);
    destroyRetired();
}

// A snapshot's window has no paint of its own: a record of it, dropped, lays it out (views place their hit areas as
// they paint), as a window's first frame does before the user's input.
void SdlHost::paintShot(Win& w) {
    ensureRenderer(format_ != SDL_GPU_TEXTUREFORMAT_INVALID ? format_ : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60;
    io.DisplaySize = ImVec2(float(w.pw), float(w.ph));
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
    auto list = recordView(device_, *w.root, w.pw, w.ph);
    ImGui::Render();
    w.dirty = false;
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

SDL_Window* SdlHost::sdlWindow(const draw::View& v) const {
    const Win* w = rootWin(v);
    return w ? w->sdl : nullptr;
}

// Takes one window out of the host. It is hidden now and destroyed after this turn's events: SDL destroys a window's
// children with it, so the children are retired before their parent (closeTree).
void SdlHost::retire(SDL_WindowID id) {
    for (size_t i = 0; i < wins_.size(); ++i) {
        if (wins_[i]->id != id) continue;
        Win& w = *wins_[i];
        if (w.root && w.router->focused()) w.router->setFocus(nullptr);  // (the view that had the focus is told)
        if (captureWin_ == id) SDL_CaptureMouse(false), captureWin_ = 0;
        if (w.sdl) SDL_HideWindow(w.sdl);
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
    if (!modal_) return sheets_ == 0;  // (a sheet: no window takes input; a dialog that opens over it does)
    while (id) {
        if (id == modal_) return true;
        const Win* w = findWin(id);
        if (!w) return false;
        id = w->parent;
    }
    return false;
}

void SdlHost::closeWindow(draw::View& content) {
    for (size_t i = 0; i < shots_.size(); ++i)
        if (shots_[i].content == &content) {
            content.attach(nullptr);
            shots_.erase(shots_.begin() + std::ptrdiff_t(i));
            return;
        }
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
    if (parent && !parent->sdl && spec.kind != WindowKind::Menu) {
        // A dialog or pop-out from a snapshot's window: a snapshot's window too, in its own pixels (a tooltip: none).
        if (spec.kind == WindowKind::Tooltip) return;
        auto w = std::make_unique<Win>();
        w->id = ++shotSeq_;
        w->root = &content;
        w->router = std::make_unique<draw::Router>(content);
        w->kind = spec.kind;
        w->closed = std::move(closed);
        w->parent = parent->id;
        w->title = spec.title;
        w->typing = true;  // (as a window's: setTyping in openWindow)
        w->pw = std::max(1, spec.at.width()), w->ph = std::max(1, spec.at.height());
        if (spec.kind != WindowKind::Dialog) {  // a pop-out: at spec.at in the opener's view, over the opener's base window
            const Point o = from.origin();
            const Point p = parent->shotBase ? Point{parent->shotAt.left, parent->shotAt.top} : Point{0, 0};
            w->shotBase = parent->shotBase ? parent->shotBase : parent->id;
            w->shotAt = Rect(p.x + o.x + spec.at.left, p.y + o.y + spec.at.top, p.x + o.x + spec.at.left + w->pw, p.y + o.y + spec.at.top + w->ph);
        }
        content.attach(this);
        content.setFrame(Rect(0, 0, w->pw, w->ph));
        wins_.push_back(std::move(w));
        return;
    }
    if (!parent || !parent->sdl) {
        // A snapshot's views (attached to the host with no window): a menu shows over them in the picture, at its
        // place, kept inside the root (as a menu is kept on the work area). (Not a pop-out: runModal's content goes
        // when the call returns.)
        const draw::View* top = &from;
        while (top->parent()) top = top->parent();
        if (top->host() != this || spec.kind != WindowKind::Menu) return;
        const Point o = from.origin();
        const int w = std::max(1, spec.at.width()), h = std::max(1, spec.at.height());
        const int x = std::clamp(o.x + spec.at.left, 0, std::max(0, top->width() - w)), y = std::clamp(o.y + spec.at.top, 0, std::max(0, top->height() - h));
        content.attach(this);
        content.setFrame(Rect(0, 0, w, h));
        shots_.push_back(Shot{&content, Rect(x, y, x + w, y + h)});
        return;
    }
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
    w->title = spec.title;
    if (!claim(*w)) {
        SDL_DestroyWindow(sdl);
        return;
    }
    if (spec.kind == WindowKind::Dialog || spec.kind == WindowKind::Popup) setTyping(*w, true);  // (a menu's keys are keys)
    SDL_ShowWindow(sdl);
    if (spec.kind == WindowKind::Dialog) SDL_RaiseWindow(sdl);
    wins_.push_back(std::move(w));
}

void SdlHost::runModal(draw::View& from, draw::View& content, const draw::WindowSpec& spec) {
    draw::WindowSpec s = spec;
    if (s.kind != WindowKind::Popup) s.kind = WindowKind::Dialog;  // (a modal pop-out stays a pop-out)
    openWindow(from, content, s, nullptr);
    Win* w = rootWin(content);
    if (!w) return;
    const SDL_WindowID id = w->id, outer = modal_;
    modal_ = id;
    if (!w->sdl) {  // a snapshot's: the hook gives the input, until the window closes or there is no more
        while (!quit_ && findWin(id)) {
            snapshotTimers(20);  // (a pop-out focuses its field on a timer once it shows)
            Win* now = findWin(id);
            if (!now) break;
            paintShot(*now);
            if (!modalHook_ || !modalHook_(content)) {
                if (findWin(id)) closeTree(id);
                break;
            }
        }
        modal_ = outer;
        return;
    }
    if (s.kind == WindowKind::Dialog) SDL_SetWindowModal(w->sdl, true);  // (SDL: a pop-out cannot be modal; the host keeps the input)
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
        if (w->sdl) SDL_DestroyWindow(w->sdl);  // (a snapshot's window has none)
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
    // A press outside a modal pop-out only closes it: the press and its release go to no view (as a Windows pop-out's
    // loop eats the press, popoutEatsPress), so the press cannot open a second pop-out or change what is under it.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && eatUp_ && e.button.button == eatUp_) {
        eatUp_ = 0;
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && modal_ && !inModal(wid)) {
        if (const Win* m = findWin(modal_); m && m->popup()) {
            eatUp_ = e.button.button;
            return dismiss(modal_);
        }
    }
    // (a pinch's start and end reset its scale even while a modal dialog takes the input: its next step is right)
    if (e.type == SDL_EVENT_PINCH_BEGIN || e.type == SDL_EVENT_PINCH_END) pinchLast_ = 1;
    if (!inModal(wid) && isInput(e)) return;  // only the dialog that runModal shows, and what it opened, takes input
    switch (e.type) {
    case SDL_EVENT_QUIT:
        if (quitRequest_) return quitRequest_();
        quit_ = true;
        return;
    case SDL_EVENT_DROP_FILE:
        if (drop_ && !modal_ && !sheets_ && e.drop.data && *e.drop.data) drop_(e.drop.data);
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
        // The window lost the keyboard focus during a press (Alt+Tab, another program or window took it): the view with
        // the mouse capture loses it now and ends what the press was for (a stroke on the map ends as one undo step), as
        // the Windows program did on WM_CAPTURECHANGED. The button's release may never come to the window.
        if (Win* w = findWin(wid); w && w->router->captured()) {
            if (captureWin_ == wid) SDL_CaptureMouse(false), captureWin_ = 0;
            w->router->setCapture(nullptr);  // (captureLost: the view may close windows; `w` is not used after it)
        }
        // Popups close when the program loses the keyboard focus (another program took it). An open dialog and the
        // pop-outs it is opened from stay (the Report feedback dialog of a value's pop-out keeps its typed text).
        if (!SDL_GetKeyboardFocus()) {
            SDL_WindowID keep = 0;
            for (const auto& d : wins_)
                if (d->kind == WindowKind::Dialog) keep = d->id;  // (the last one opened: its chain has the others)
            dismissPopupsExcept(keep);
        }
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
        const bool taken = w->router->mouse(m);
        // A right click that no view of a dialog or a pop-out took: the program's hook (Report feedback).
        if (rightClick_ && !taken && m.kind == MouseKind::Up && button == 1 && wid != mainId_)
            if (Win* now = findWin(wid); now && (now->kind == WindowKind::Dialog || now->kind == WindowKind::Popup))
                rightClick_(*now->root, m.pt, now->title);
        // The cursor for what the press or release changed (a drag that starts or ends), before the mouse moves.
        if (Win* now = findWin(wid); now && now->kind != WindowKind::Tooltip) setCursor(now->router->cursor(now->mouse));
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
        // Both axes as the content moves, in the system's direction (SDL's own values: a wheel away from the user, or a
        // trackpad's natural scroll down, moves the content down). macOS and Wayland give a precise scroll in tenths of
        // its points; the others in notches (a notch: 48 px at 100 %, Windows' three lines).
        const char* driver = SDL_GetCurrentVideoDriver();
        const bool cocoa = macScrollTest_ || (driver && std::strcmp(driver, "cocoa") == 0);
        const bool tenths = cocoa || (driver && std::strcmp(driver, "wayland") == 0);
        const float unit = tenths ? 10.0f * w->density : float(kit::px(48));
        m.panX = -e.wheel.x * unit, m.panY = e.wheel.y * unit;
        m.precise = preciseScroll(e.wheel);
        // A scroll that goes on (its fingers moved on, or its momentum) goes to where it began, as macOS latches it.
        if (scrollContinues_ && scrollContinues_(e.wheel.x, e.wheel.y) && latchWin_ == wid) m.pt = latchPt_;
        else if (scrollContinues_) latchWin_ = wid, latchPt_ = m.pt, exactRestX_ = exactRestY_ = 0;  // (a new scroll)
        // macOS's trackpad (and its momentum): the lists move by the system's own distance, in whole pixels (the
        // fractions add up to the next event's).
        m.exact = cocoa && m.precise;
        if (m.exact) {
            exactRestX_ += m.panX, exactRestY_ += m.panY;
            m.panX = std::trunc(exactRestX_), m.panY = std::trunc(exactRestY_);
            exactRestX_ -= m.panX, exactRestY_ -= m.panY;
        }
        m.mods = modsOf(SDL_GetModState());
        w->router->mouse(m);
        return;
    }
    case SDL_EVENT_PINCH_BEGIN:
    case SDL_EVENT_PINCH_UPDATE: {
        // macOS gives each step's change; X11 and Wayland the scale since the pinch began.
        const char* driver = SDL_GetCurrentVideoDriver();
        const bool steps = driver && std::strcmp(driver, "cocoa") == 0;
        if (e.type == SDL_EVENT_PINCH_BEGIN) {
            pinchLast_ = 1;
            return;
        }
        if (!(e.pinch.scale > 0)) return;
        const float change = steps ? e.pinch.scale : e.pinch.scale / pinchLast_;
        if (!steps) pinchLast_ = e.pinch.scale;
        Win* w = findWin(wid);
        if (!w) return;
        if (hover_) hover_(*w->root, {}, {}, true);
        MouseEvent m;
        m.kind = MouseKind::Pinch;
        m.pt = w->mouse;  // (a trackpad's pinch is around the mouse)
        m.pinch = change;
        m.mods = modsOf(SDL_GetModState());
        w->router->mouse(m);
        return;
    }
    case SDL_EVENT_PINCH_END: pinchLast_ = 1; return;
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
        if (!w || !w->typing) return;  // (SDL sends none then: a snapshot's window does the same)
        if (w->composing) {  // the text is committed: the composition ends
            w->composing = false;
            w->router->compose(draw::CompositionEvent{});
        }
        w->router->text(draw::TextEvent{e.text.text ? e.text.text : ""});
        return;
    }
    case SDL_EVENT_TEXT_EDITING: {
        Win* w = findWin(wid);
        if (!w || !w->typing) return;
        draw::CompositionEvent c;
        c.utf8 = e.edit.text ? e.edit.text : "";
        c.cursor = int(byteIndex(c.utf8, e.edit.start));
        w->composing = !c.utf8.empty();
        w->router->compose(c);
        return;
    }
    default:
        if (gGestureEvent && e.type == gGestureEvent) {  // (pushGesture: macOS's smart zoom and three-finger swipe)
            Win* w = findWin(wid);
            if (!w) return;
            if (hover_) hover_(*w->root, {}, {}, true);
            MouseEvent m;
            m.kind = MouseKind(e.user.code);
            m.pt = w->mouse;  // (a trackpad's gesture is at the mouse)
            m.swipe = int(reinterpret_cast<intptr_t>(e.user.data1));
            m.mods = modsOf(SDL_GetModState());
            w->router->mouse(m);
        }
        return;
    }
}

SDL_Event SdlHost::gestureEvent(draw::MouseKind kind, int swipe, SDL_WindowID window) {
    if (!gGestureEvent) gGestureEvent = SDL_RegisterEvents(1);
    SDL_Event e{};
    e.type = gGestureEvent;
    e.user.code = Sint32(kind);
    e.user.data1 = reinterpret_cast<void*>(intptr_t(swipe));
    e.user.windowID = window;
    return e;
}

void SdlHost::pushGesture(draw::MouseKind kind, int swipe, SDL_WindowID window) {
    SDL_Event e = gestureEvent(kind, swipe, window);
    SDL_PushEvent(&e);
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
    const Point o = v.origin();
    w->inputOn = on;
    if (on) w->inputArea = Rect(o.x + caret.left, o.y + caret.top, o.x + caret.right, o.y + caret.bottom);
    // The field let the keyboard go: the input method's text not committed yet goes (the window still takes typed text).
    if (!on) {
        if (w->sdl && w->composing) SDL_ClearComposition(w->sdl);
        return;
    }
    setTyping(*w, true);
    if (!w->sdl) return;  // (a snapshot's window: the text comes from snapshotText and snapshotCompose)
    SDL_Rect r{};
    r.x = toLogical(o.x + caret.left, w->density), r.y = toLogical(o.y + caret.top, w->density);
    r.w = std::max(1, toLogical(caret.width(), w->density)), r.h = std::max(1, toLogical(caret.height(), w->density));
    SDL_SetTextInputArea(w->sdl, &r, 0);
}

void SdlHost::setTyping(Win& w, bool on) {
    w.typing = on;
    if (w.sdl) on ? SDL_StartTextInput(w.sdl) : SDL_StopTextInput(w.sdl);
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
