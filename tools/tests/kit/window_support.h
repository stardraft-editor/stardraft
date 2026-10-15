#pragma once
// For the tests of windows (Dialog, messageBox): a host whose runModal plays the user, so that a blocking call such as
// messageBox() returns with the button the script clicked; and events sent to a window's own views (the window has
// its own Router, as the host gives each window one).

#include <functional>
#include <memory>
#include <string>

#include "button.h"
#include "kit_check.h"
#include "test_support.h"

namespace kitwin {

using namespace stardraft;
using namespace stardraft::kit::test;

// A host for windows: each window (and the root) has a Router of its own, as a real host gives it; runModal opens
// the window and calls `onOpen` with its content, as the user would act in it, then returns. Windows are kept in
// `windows`; paintWindow paints one. (FakeHost, test_support.h, is final, so this one is a host of its own.)
class ScriptedHost final : public draw::Host {
public:
    explicit ScriptedHost(draw::View& root, int w = 800, int h = 600) {
        root.attach(this);
        root.setFrame({0, 0, w, h});
        routerFor(root);
    }
    ~ScriptedHost() override {
        // Only the root given to the constructor outlives the host in a test; a window's views (closed or not) may be
        // gone already.
        if (!routers_.empty()) routers_[0]->root().attach(nullptr);
    }

    // The router of the window (or the root) that `v` is in: the events of a window go through it.
    draw::Router& routerFor(const draw::View& v) {
        const draw::View* top = &v;
        while (top->parent()) top = top->parent();
        for (auto& r : routers_)
            if (&r->root() == top) return *r;
        routers_.push_back(std::make_unique<draw::Router>(const_cast<draw::View&>(*top)));
        closed_.push_back(false);
        return *routers_.back();
    }

    struct Window {
        draw::View* from;
        draw::View* content;
        draw::WindowSpec spec;
        std::function<void()> closed;
    };
    std::vector<Window> windows;
    std::function<void(draw::View&)> onOpen;
    int modals = 0;

    void invalidate(draw::View&) override {}
    void focus(draw::View& v) override { routerFor(v).setFocus(&v); }
    bool hasFocus(const draw::View& v) const override {
        return const_cast<ScriptedHost*>(this)->routerFor(v).focused() == &v;
    }
    void capture(draw::View& v, bool on) override { routerFor(v).setCapture(on ? &v : nullptr); }
    draw::Point mouse(const draw::View&) const override { return {}; }
    void setCursor(draw::Cursor) override {}
    std::string clipboardText() override { return clip; }
    void setClipboardText(std::string_view t) override { clip = std::string(t); }
    int timer(int, std::function<void()>) override { return 0; }
    void cancelTimer(int) override {}
    void textInput(draw::View&, bool, draw::Rect) override {}
    float scale() const override { return 1; }
    void openWindow(draw::View& from, draw::View& content, const draw::WindowSpec& spec, std::function<void()> closed) override {
        windows.push_back({&from, &content, spec, std::move(closed)});
        content.setFrame({0, 0, spec.at.width(), spec.at.height()});
        content.attach(this);
        routerFor(content);
        for (size_t i = 0; i < routers_.size(); ++i)
            if (&routers_[i]->root() == &content) closed_[i] = false;  // (a window opened again at the same address)
    }
    void closeWindow(draw::View& content) override {
        for (auto it = windows.begin(); it != windows.end(); ++it)
            if (it->content == &content) {
                windows.erase(it);
                content.attach(nullptr);  // (its router stays: the events of the call that closed it may still come)
                for (size_t i = 0; i < routers_.size(); ++i)
                    if (&routers_[i]->root() == &content) closed_[i] = true;
                return;
            }
    }
    void runModal(draw::View& from, draw::View& content, const draw::WindowSpec& spec) override {
        ++modals;
        openWindow(from, content, spec, nullptr);
        if (onOpen) onOpen(content);
    }
    void forget(const draw::View& v) override {
        for (size_t i = 0; i < routers_.size(); ++i)
            if (!closed_[i]) routers_[i]->forget(v);
    }

    RecordingCanvas paintWindow(size_t i) {
        RecordingCanvas c;
        windows.at(i).content->paintAll(c);
        return c;
    }

private:
    std::string clip;
    std::vector<std::unique_ptr<draw::Router>> routers_;
    std::vector<bool> closed_;  // per router: its window was closed (its views may be destroyed)
};

inline void clickIn(draw::Router& r, int x, int y, int button = 0) {
    draw::MouseEvent e;
    e.kind = draw::MouseKind::Down, e.pt = {x, y}, e.button = button;
    r.mouse(e);
    e.kind = draw::MouseKind::Up;
    r.mouse(e);
}

inline bool pressIn(draw::Router& r, draw::Key k, unsigned mods = 0) {
    draw::KeyEvent e;
    e.key = k, e.mods = mods;
    const bool used = r.key(e);
    e.down = false;
    r.key(e);
    return used;
}

// The button of `v`'s children with this text (null: none).
inline kit::Button* buttonNamed(const draw::View& v, const std::wstring& text) {
    for (draw::View* c : v.children())
        if (auto* b = dynamic_cast<kit::Button*>(c))
            if (b->text() == text) return b;
    return nullptr;
}

// The middle of a child's frame, in its parent's coordinates.
inline draw::Point middleOf(const draw::View& v) {
    const draw::Rect f = v.frame();
    return {f.left + f.width() / 2, f.top + f.height() / 2};
}

}  // namespace kitwin
