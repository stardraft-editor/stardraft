#pragma once
// The editor's UI without a window system: a View paints on a Canvas and gets mouse, key and text events; a Host
// (a Win32 window, an SDL window) shows views, sends them the events and gives them what only a window system has
// (focus, mouse capture, the clipboard, timers, where the input method shows its candidates).
//
// Coordinates in events and in paint() are the view's own: (0, 0) is its top left corner.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "canvas.h"

namespace stardraft::draw {

enum class Cursor { Arrow, Hand, IBeam, SizeAll, SizeWE, SizeNS, No, Wait };

enum Mods : unsigned { kShift = 1, kCtrl = 2, kAlt = 4, kSuper = 8 };

enum class MouseKind { Move, Down, Up, DoubleClick, Wheel, Leave };
struct MouseEvent {
    MouseKind kind = MouseKind::Move;
    Point pt;
    int button = 0;     // Down, Up, DoubleClick: 0 left, 1 right, 2 middle
    float wheel = 0;    // Wheel: notches, positive away from the user
    bool horizontal = false;
    unsigned mods = 0;  // Mods
};

// Keys by their meaning, not by the system's codes. Letters and digits are their ASCII capitals ('A', '0').
enum class Key : int {
    None = 0,
    Backspace = 8, Tab = 9, Enter = 13, Escape = 27, Space = 32,
    Left = 0x100, Right, Up, Down, Home, End, PageUp, PageDown, Insert, Delete,
    F1 = 0x200, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Plus = 0x300, Minus, Comma, Period, Slash, Backslash, Semicolon, Quote, Grave, LeftBracket, RightBracket, Equals,
};
struct KeyEvent {
    Key key = Key::None;
    bool down = true;
    bool repeat = false;
    unsigned mods = 0;
};
// Text from the keyboard or the input method, committed.
struct TextEvent {
    std::string utf8;
};
// The input method's text that is not committed yet (Korean syllables while they are typed). Empty: it ended.
struct CompositionEvent {
    std::string utf8;
    int cursor = 0;  // in UTF-8 bytes
};

class View;

// A window a view opens for another view (a pop-out, a menu, a dialog).
enum class WindowKind {
    Popup,    // closes when the user clicks outside it or presses Esc (pickers, pop-outs)
    Menu,     // as Popup; a menu and its submenus
    Tooltip,  // never takes the mouse or the keyboard
    Dialog,   // a window with a title; other windows of the program wait until it closes (runModal)
};
struct WindowSpec {
    WindowKind kind = WindowKind::Popup;
    Rect at;  // where, in the coordinates of the view that opens it (Dialog: only the size; the host centers it)
    std::wstring title;
};

class Host {
public:
    virtual ~Host() = default;
    // The view gets a paint soon.
    virtual void invalidate(View& v) = 0;
    // The keyboard goes to `v`.
    virtual void focus(View& v) = 0;
    virtual bool hasFocus(const View& v) const = 0;
    // While captured, `v` gets every mouse event, also outside it.
    virtual void capture(View& v, bool on) = 0;
    // The mouse position in `v`'s coordinates.
    virtual Point mouse(const View& v) const = 0;
    virtual void setCursor(Cursor c) = 0;
    virtual std::string clipboardText() = 0;
    virtual void setClipboardText(std::string_view utf8) = 0;
    // `run` after `ms` milliseconds, on the UI thread, once. Returns an id for cancelTimer.
    virtual int timer(int ms, std::function<void()> run) = 0;
    virtual void cancelTimer(int id) = 0;
    // Text input for `v` on or off, and where its caret is (the input method shows its candidates there).
    virtual void textInput(View& v, bool on, Rect caret) = 0;
    // Display scale: 1 at 96 DPI.
    virtual float scale() const = 0;

    // Shows `content` in a new window, opened by `from` (which stays as it is). The window closes with
    // closeWindow(content), or, for a Popup or Menu, when the user clicks outside it (then `closed` runs).
    virtual void openWindow(View& from, View& content, const WindowSpec& spec, std::function<void()> closed = nullptr) = 0;
    virtual void closeWindow(View& content) = 0;
    // Opens `content` as openWindow does and returns when it closes; the program's other windows wait.
    virtual void runModal(View& from, View& content, const WindowSpec& spec) = 0;
};

class View {
public:
    virtual ~View() = default;

    virtual void paint(Canvas&) {}
    // true: the view used the event.
    virtual bool mouse(const MouseEvent&) { return false; }
    virtual bool key(const KeyEvent&) { return false; }
    virtual bool text(const TextEvent&) { return false; }
    virtual bool compose(const CompositionEvent&) { return false; }
    virtual Cursor cursor(Point) { return Cursor::Arrow; }
    // The tooltip at `pt` (empty: none).
    virtual std::wstring tip(Point) { return {}; }
    virtual void focusChanged(bool /*has*/) {}
    virtual void resized() {}

    // Where the host shows the view, and its size.
    Host* host() const { return host_; }
    int width() const { return w_; }
    int height() const { return h_; }
    void invalidate() {
        if (host_) host_->invalidate(*this);
    }
    // (For hosts.)
    void attach(Host* h) { host_ = h; }
    void setSize(int w, int h) {
        if (w == w_ && h == h_) return;
        w_ = w, h_ = h;
        resized();
    }

private:
    Host* host_ = nullptr;
    int w_ = 0, h_ = 0;
};

}  // namespace stardraft::draw
