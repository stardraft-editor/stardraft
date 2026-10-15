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
    // Paints `root` (laid out for w x h pixels) into `target`, a texture of `format` with color target usage. Its
    // own, without a window: for snapshots. Waits for the GPU to finish.
    void snapshot(draw::View& root, SDL_GPUTexture* target, int w, int h, SDL_GPUTextureFormat format);

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
    void runModal(draw::View& from, draw::View& content, const draw::WindowSpec& spec) override;
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
    void destroyRetired();
    void dispatch(const SDL_Event& e);
    void fireTimer(int id);

    SDL_GPUDevice* device_;
    SDL_GPUTextureFormat format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    bool rendererReady_ = false;
    bool quit_ = false;
    float scale_ = 1;
    float scaleOverride_ = 0;  // setScale, or 0
    SDL_WindowID modal_ = 0;  // the dialog that takes all input (runModal), or 0
    SDL_WindowID captureWin_ = 0;  // the window whose mouse is captured (SDL_CaptureMouse), or 0
    int timerSeq_ = 0;
    std::map<int, Timer> timers_;
    std::vector<std::unique_ptr<Win>> wins_;
    std::vector<std::unique_ptr<Win>> retired_;  // closed windows, destroyed after the events of this turn
    SDL_Cursor* cursors_[8] = {};
};

}  // namespace stardraft::kit_sdl
