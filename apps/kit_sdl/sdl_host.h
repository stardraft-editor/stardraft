#pragma once
// SDL3 windows for the kit's views (apps/common/view.h). An SdlHost is a draw::Host: each root view it shows is in an
// SDL window of its own, painted by an ImguiCanvas (canvas_imgui.h) through Dear ImGui's SDL_GPU backend, and SDL's
// events reach the views through a draw::Router. The program makes the GPU device and the ImGui context (the host
// makes the context in its constructor; fonts are loaded into it before the first window), then calls run().
//
// Coordinates: a window's views are in its pixels (SDL's logical points times the window's pixel density). The
// display scale (SDL_GetWindowDisplayScale) sets kit::theme().dpi = 96 * scale.

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "view.h"

namespace stardraft::kit_sdl {

class SdlHost final : public draw::Host {
public:
    // `device`: the GPU device every window is claimed for. Makes the ImGui context and keeps it until destruction.
    explicit SdlHost(SDL_GPUDevice* device);
    ~SdlHost() override;
    SdlHost(const SdlHost&) = delete;
    SdlHost& operator=(const SdlHost&) = delete;

    // The display scale of every window (the program's --scale); 0 takes the display's own scale again.
    void setScale(float s);
    // Shows `root` in a window of its own (`w` x `h` pixels, titled `title`). False when SDL could not make it.
    bool show(draw::View& root, const std::wstring& title, int w, int h);
    // Handles events and paints the windows that changed, until every window is closed or quit() is called.
    void run();
    void quit() { quit_ = true; }
    // Handles events and paints, as run() does, until `done` is true or the program quits: a call that waits for a
    // system dialog that answers later (the file and folder dialogs) returns with the answer. False when it quit first.
    bool waitUntil(const std::function<bool()>& done) {
        while (!quit_ && !done()) pump(true);
        return !quit_;
    }
    // Paints `root` (laid out for w x h pixels) into `target`, a texture of `format` with color target usage. Its
    // own, without a window: for snapshots. Waits for the GPU to finish.
    void snapshot(draw::View& root, SDL_GPUTexture* target, int w, int h, SDL_GPUTextureFormat format);
    // A snapshot's input: SDL's mouse events (`events`, at points in `root`'s pixels; their window id is not read)
    // go through the same path as a window's events (dispatch: the press closes pop-outs, the router's hit test,
    // capture and hover, the pointer that mouse() gives, the modifier keys of SDL_GetModState), as if `root` were in a
    // window of its own (it is attached to the host). No hover tips open. That window stays (its focus and capture
    // stay for the next input) until snapshotInputEnd(). A dialog or pop-out that a view opens from it is a window
    // with no SDL window too (in its own pixels; snapshotTop gives it), and a runModal of one runs the modal hook
    // (setSnapshotModalHook) for its input; a menu shows over the root in the picture, as before.
    void snapshotInput(draw::View& root, int w, int h, const std::vector<SDL_Event>& events);
    // A snapshot's mouse events at points of the main window `root` (as a user's mouse on the screen): they go to the
    // window that the first point is on (a modal dialog, in its own pixels, takes them all; else the last pop-out that
    // has the point, else `root`), by snapshotInput. A press outside the pop-outs closes them, as dispatch does.
    void snapshotMouse(draw::View& root, const std::vector<SDL_Event>& events);
    // A key (a press) and typed text for such a window, the same way as a window's (dispatch: Esc closes a pop-out;
    // the router: the view with the focus first, then its parents).
    void snapshotKey(draw::View& root, int w, int h, const draw::KeyEvent& k);
    void snapshotText(draw::View& root, int w, int h, const std::string& utf8);
    // The window that takes a snapshot's keys and text now: the last dialog or pop-out opened (with no SDL window),
    // else `root`; `size` its size in pixels.
    draw::View& snapshotTop(draw::View& root, draw::Point& size);
    // The timers that are due (a snapshot has no event loop): within `ms` milliseconds, as SDL fires them.
    void snapshotTimers(int ms);
    // The snapshot's windows go (the views stay the caller's).
    void snapshotInputEnd();
    // While runModal shows a window with no SDL window (a snapshot's): `hook` gets its root view and gives it the
    // next input; false when there is none (the window then closes, as Esc closes it).
    void setSnapshotModalHook(std::function<bool(draw::View& content)> hook) { modalHook_ = std::move(hook); }
    // Called once per frame, before the windows are painted, with a command buffer the host submits after the call.
    // The program draws its own GPU textures here (the views show them with draw::Canvas::externalImage). It runs on
    // the frames that paint, and on every frame while setAnimating(true); snapshot() calls it too.
    void setFrameHook(std::function<void(SDL_GPUCommandBuffer*)> hook) { frameHook_ = std::move(hook); }
    // Called once per pump, before the frame hook: work that may open a modal (its nested loop pumps frames) runs here,
    // not in the frame hook, which holds a command buffer.
    void setBeforePump(std::function<void()> fn) { beforePump_ = std::move(fn); }
    // True: frames keep coming (no waiting for events), every window is painted each frame. For views that animate.
    void setAnimating(bool on) { animating_ = on; }
    // The program's quit: the close button of the window that show() made, and the system's quit request, call `fn`
    // instead of closing (the program asks about unsaved changes, then calls quit()). Unset: they close.
    void setQuitRequest(std::function<void()> fn) { quitRequest_ = std::move(fn); }
    // The hover tooltips (the program shows them): after each mouse move over a window (not a tooltip), `fn` gets the
    // window's root view, the mouse in its pixels and the tip there (the views' tip(); empty: none), with reset false.
    // With reset true (and no point or text), the tip of that window must go: a press, a wheel turn, a key, the mouse
    // left the window, the window lost the focus or closed.
    using HoverHook = std::function<void(draw::View& root, draw::Point pt, const std::wstring& tip, bool reset)>;
    void setHoverHook(HoverHook fn) { hover_ = std::move(fn); }
    // A file dropped on a window, or (macOS) a file that Finder opens with the program, also at its start: `fn` gets its
    // path (UTF-8). Not while a modal dialog is open (runModal): the drop then does nothing.
    void setDropHook(std::function<void(const std::string& path)> fn) { drop_ = std::move(fn); }
    // A right click (the release) in a dialog or a pop-out window (not the window show() made, not a menu) that no
    // view took: `fn` gets the window's root view, the point in its pixels and the window's title (a pop-out: empty).
    // The program shows its Report feedback menu there (the Windows program's feedbackAfterRightClick).
    using RightClickHook = std::function<void(draw::View& root, draw::Point pt, const std::wstring& title)>;
    void setRightClickHook(RightClickHook fn) { rightClick_ = std::move(fn); }
    // Whether the wheel event that SDL gives now is a trackpad's (precise) scroll, not a wheel's notches: macOS knows
    // it (NSEvent's hasPreciseScrollingDeltas; the program asks it). With no hook, a scroll with fractions is precise
    // (Windows' precision touchpads, Wayland, X11's smooth scrolling), and so are the scroll's next steps for 0.2 s.
    void setPreciseScrollHook(std::function<bool()> fn) { precise_ = std::move(fn); }
    // The SDL window that shows `v` (a view in a window that show() or openWindow made), or nullptr: the program's
    // system parts reach it (macOS: a Settings window's toolbar).
    SDL_Window* sdlWindow(const draw::View& v) const;

    // draw::Host
    void invalidate(draw::View& v) override;
    void focus(draw::View& v) override;
    bool hasFocus(const draw::View& v) const override;
    void capture(draw::View& v, bool on) override;
    draw::Point mouse(const draw::View& v) const override;
    void setCursor(draw::Cursor c) override;
    std::string clipboardText() override;
    void setClipboardText(std::string_view utf8) override;
    int timer(int ms, std::function<void()> run) override;
    void cancelTimer(int id) override;
    void textInput(draw::View& v, bool on, draw::Rect caret) override;
    float scale() const override { return scale_; }
    void openWindow(draw::View& from, draw::View& content, const draw::WindowSpec& spec, std::function<void()> closed) override;
    void closeWindow(draw::View& content) override;
    // A Dialog spec (or a Menu or Tooltip): a dialog window of its own. A Popup spec: a modal pop-out at spec.at; a
    // press outside it only closes it (the press and its release do not go to the window under the mouse).
    void runModal(draw::View& from, draw::View& content, const draw::WindowSpec& spec) override;
    // A modal dialog or pop-out is open (runModal): the other windows wait (macOS: the menu bar's commands too).
    bool modalOpen() const { return modal_ != 0; }
    void forget(const draw::View& v) override;

private:
    struct Win;
    struct Timer {
        SDL_TimerID sdl = 0;
        std::function<void()> run;
    };

    Win* rootWin(const draw::View& v) const;
    Win* findWin(SDL_WindowID id) const;
    std::unique_ptr<Win> makeWin(SDL_Window* sdl, draw::View& root, draw::WindowKind kind, std::function<void()> closed);
    bool claim(Win& w);
    void ensureRenderer(SDL_GPUTextureFormat format);
    void resize(Win& w);
    void applyScale(float s);
    void sizeInPixels(SDL_Window* sdl, int w, int h);
    void retire(SDL_WindowID id);  // (the window only; see closeTree)
    void closeTree(SDL_WindowID id);  // the window, the windows opened from it, and so on: children first
    void collectTree(SDL_WindowID id, std::vector<SDL_WindowID>& out) const;
    bool inModal(SDL_WindowID id) const;  // the window is the modal dialog or opened from it
    void dismiss(SDL_WindowID id);
    void dismissPopupsExcept(SDL_WindowID keep);  // (the clicked window's own chain of parents stays)
    void pump(bool block);
    bool anyDirty() const;
    void renderDirty();
    void runFrameHook();
    void destroyRetired();
    void dispatch(const SDL_Event& e);
    void fireTimer(int id);

    SDL_GPUDevice* device_;
    SDL_GPUTextureFormat format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    bool rendererReady_ = false;
    bool quit_ = false;
    float scale_ = 1;
    float scaleOverride_ = 0;  // setScale, or 0
    std::function<void(SDL_GPUCommandBuffer*)> frameHook_;
    std::function<void()> beforePump_;  // setBeforePump
    bool animating_ = false;
    SDL_WindowID modal_ = 0;  // the dialog that takes all input (runModal), or 0
    Uint8 eatUp_ = 0;  // the mouse button whose release goes to no view (its press closed a modal pop-out), or 0
    SDL_WindowID mainId_ = 0;  // the window show() made first (the program's main window), or 0
    std::function<void()> quitRequest_;  // setQuitRequest
    HoverHook hover_;                    // setHoverHook
    std::function<void(const std::string&)> drop_;  // setDropHook
    RightClickHook rightClick_;                     // setRightClickHook
    std::function<bool()> precise_;                 // setPreciseScrollHook
    Uint64 preciseUntil_ = 0;                       // a precise scroll's next steps are precise until then (ns)
    float pinchLast_ = 1;                           // a pinch's scale so far (the systems that give it from its start)
    bool preciseScroll(const SDL_MouseWheelEvent& e);
    std::function<bool(draw::View&)> modalHook_;    // setSnapshotModalHook
    SDL_WindowID shotSeq_ = 0xFFFFFF00u;            // the ids of a snapshot's windows (ids SDL does not give)
    Win* shotWin(draw::View& root, int w, int h);   // the snapshot's window of `root` (made at the first input)
    void paintShot(Win& w);                         // lays a snapshot's window out by painting it (its hit areas)
    // The menus that views attached with no window (a snapshot's) opened: snapshot() paints them over
    // the root, at their places (in the root's pixels), until they close.
    struct Shot {
        draw::View* content = nullptr;
        draw::Rect at;
    };
    std::vector<Shot> shots_;
    SDL_WindowID captureWin_ = 0;  // the window whose mouse is captured (SDL_CaptureMouse), or 0
    int timerSeq_ = 0;
    std::map<int, Timer> timers_;
    std::vector<std::unique_ptr<Win>> wins_;
    std::vector<std::unique_ptr<Win>> retired_;  // closed windows, destroyed after the events of this turn
    SDL_Cursor* cursors_[16] = {};  // (by draw::Cursor)
};

}  // namespace stardraft::kit_sdl
