#pragma once
// The editor's UI without a window system: a View paints on a Canvas and gets mouse, key and text events; a Host
// (a Win32 window, an SDL window) shows views, sends them the events and gives them what only a window system has
// (focus, mouse capture, the clipboard, timers, where the input method shows its candidates).
//
// Coordinates in events and in paint() are the view's own: (0, 0) is its top left corner.

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
#include <string>
#include <string_view>

#include "canvas.h"

namespace stardraft::draw {

// SizeNWSE: from the top left to the bottom right (a rectangle's top left or bottom right corner); SizeNESW: the other diagonal.
enum class Cursor { Arrow, Hand, IBeam, SizeAll, SizeWE, SizeNS, No, Wait, SizeNWSE, SizeNESW };

enum Mods : unsigned { kShift = 1, kCtrl = 2, kAlt = 4, kSuper = 8 };

// Pinch: two fingers on a trackpad move apart or together (pt: the mouse, which is between them on a trackpad).
// SmartZoom: a double tap with two fingers on a trackpad (macOS's smart zoom), at the mouse. Swipe: a swipe of three
// fingers to the left or right on a trackpad (macOS's "Swipe between pages"), at the mouse; `swipe` gives its direction.
enum class MouseKind { Move, Down, Up, DoubleClick, Wheel, Leave, Pinch, SmartZoom, Swipe };
struct MouseEvent {
    MouseKind kind = MouseKind::Move;
    Point pt;
    int button = 0;     // Down, Up, DoubleClick: 0 left, 1 right, 2 middle
    float wheel = 0;    // Wheel: notches, positive away from the user (a trackpad's: fractions)
    bool horizontal = false;
    unsigned mods = 0;  // Mods
    // Wheel: how far the content moves, in pixels, on both axes (positive: right, down), in the system's scroll
    // direction (natural scrolling: it follows the fingers).
    float panX = 0, panY = 0;
    bool precise = false;  // Wheel: a trackpad's (or a precise mouse's) scroll, not a wheel's notches
    float pinch = 1;       // Pinch: the scale change since the last (more than 1: the fingers move apart)
    // Wheel: panX and panY are the system's own scroll distance, in whole pixels (macOS: a trackpad's or a Magic Mouse's
    // scroll and its momentum after the fingers lift). A list then moves by them (wheelDistance), not by notches.
    bool exact = false;
    int swipe = 0;  // Swipe: -1 the fingers moved to the left (the next tab), 1 to the right (the tab before)
};

// How far a vertical scroll moves a view's content down, in pixels (a scroll position goes up by it): a wheel's
// notches times `notchPx` (a notch away from the user: positive), or the system's exact distance (MouseEvent::exact).
inline float wheelDistance(const MouseEvent& e, float notchPx) { return e.exact ? e.panY : e.wheel * notchPx; }
// The same distance in whole steps of `stepPx` (a list that scrolls by rows); the part of a step that is left waits in
// `carry` for the next event. Only for an exact scroll (else 0).
inline int exactSteps(const MouseEvent& e, float stepPx, float& carry) {
    if (!e.exact || !(stepPx > 0)) return 0;
    carry += e.panY / stepPx;
    const int whole = int(carry);  // (toward zero)
    carry -= float(whole);
    return whole;
}

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
    // A window with a title that stays over the window that opens it (a floating palette): the other windows do not
    // wait, the user can move it (also to another screen) and change its size. `at`: its place in the coordinates of
    // the view that opens it. Its close button closes it (then `closed` runs). A snapshot shows it over its opener.
    Tool,
};
struct WindowSpec {
    WindowKind kind = WindowKind::Popup;
    Rect at;  // where, in the coordinates of the view that opens it (Dialog: only the size; the host centers it)
    std::wstring title;
    bool workArea = false;  // Dialog: centered on the work area of the screen (else on the window that opens it)
    bool resizable = false;  // Dialog: the window can be resized (its content is laid out again)
    bool placed = false;     // Dialog: at `at`'s left and top in screen points (else centered)
    // Popup that is not modal: it takes the keys and the typed text that come to the window it is opened from (its
    // search field types; Windows can give a pop-out's keys to the window under it).
    bool takesKeys = false;
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
    // `v` (or a view under it) leaves the host's views: the focus, the capture and the hover forget it.
    virtual void forget(const View& v) = 0;
    // The modifier keys down now (Mods).
    virtual unsigned modifiers() const { return 0; }
};

class View {
public:
    virtual ~View() {
        if (parent_) parent_->remove(*this);
        for (View* c : children_) c->parent_ = nullptr;
    }

    // What the view draws, in its own coordinates (its children draw after it, each in its frame).
    virtual void paint(Canvas&) {}
    // After the children (a focus ring, a drag's line over them).
    virtual void paintOver(Canvas&) {}
    // true: the view used the event. A mouse event goes to the deepest child under the mouse first, then to its
    // parents; a key or text event goes to the view with the focus first, then to its parents.
    virtual bool mouse(const MouseEvent&) { return false; }
    virtual bool key(const KeyEvent&) { return false; }
    virtual bool text(const TextEvent&) { return false; }
    virtual bool compose(const CompositionEvent&) { return false; }
    virtual Cursor cursor(Point) { return Cursor::Arrow; }
    // Whether two points (the view's own) are on the same item (a list's row, a grid's cell): a second quick press is
    // a double click only on the item of the first press, else it is a new press (Router). By default the whole view
    // is one item.
    virtual bool sameItem(Point, Point) const { return true; }
    // The tooltip at `pt` (empty: none).
    virtual std::wstring tip(Point) { return {}; }
    // What Report feedback names at a point (a view can name a part that has no tooltip); by default its tip.
    virtual std::wstring feedbackName(Point pt) { return tip(pt); }
    // Whether the point is over the window's size grip (the bottom right corner of a window that resizes from it): a
    // Win32 host then answers the hit test with the sizing corner, so the window's edge is dragged from there.
    virtual bool sizeGrip(Point) { return false; }
    // true: the host shows tip() as a tooltip while the mouse is over the view (the text is asked for where it is).
    virtual bool hoverTip() const { return false; }
    // Whether Tab stops on it (and a click gives it the focus).
    virtual bool focusable() const { return false; }
    // A view that takes the letters and digits it is given (a text field): while it has the focus, the window's
    // one-letter shortcuts wait (the keys are its own).
    virtual bool typesText() const { return false; }
    // The window of the view got the keyboard focus with no view of it focused (Tab came in from another control):
    // the view takes it on its first (back: its last) focusable part.
    virtual void focusEntered(bool /*back*/) {}
    // Tab (or Shift+Tab) gave the view the focus, after focusChanged: a one-line text field selects its text, so that
    // what is typed takes its place (as a dialog's edit box on Windows and a text field on macOS).
    virtual void tabbedTo() {}
    virtual void focusChanged(bool /*has*/) {}
    // The mouse capture left the view (it went to another view, the system took it, or the view let it go): the view
    // ends what the capture was for.
    virtual void captureLost() {}
    virtual void resized() {}
    // The window's own vertical scroll bar was used (a view window with WS_VSCROLL): `code` is the SB_* request, `pos` the
    // bar's place, `track` where a dragged thumb is, `page` its page size. true: the view handled it.
    virtual bool vscroll(int /*code*/, int /*pos*/, int /*track*/, int /*page*/) { return false; }
    // The same bar's state, for the host to show: its range (the last position), its page size and its position. The host
    // asks after each paint and each scroll. false: the view has no such bar.
    virtual bool windowScrollBar(int& /*range*/, int& /*page*/, int& /*pos*/) { return false; }
    // The horizontal counterpart (WS_HSCROLL on the window): the same, for the bar at the bottom.
    virtual bool hscroll(int /*code*/, int /*pos*/, int /*track*/, int /*page*/) { return false; }
    virtual bool windowHScrollBar(int& /*range*/, int& /*page*/, int& /*pos*/) { return false; }
    // true: the window's vertical bar is disabled (not hidden) when its content fits (the Data list's bar).
    virtual bool windowScrollBarDisables() const { return false; }
    // The mouse's extra buttons (1 or 2: back, forward) released over the view; true: handled.
    virtual bool extraButton(int /*button*/) { return false; }

    // The host that shows the view (the root's), and the view's place.
    Host* host() const { return parent_ ? parent_->host() : host_; }
    View* parent() const { return parent_; }
    const std::vector<View*>& children() const { return children_; }
    Rect frame() const { return frame_; }  // in the parent's coordinates; a root's is its window's client area
    int width() const { return frame_.width(); }
    int height() const { return frame_.height(); }
    bool visible() const { return visible_; }
    // Visible, and so are all its parents: the view is on the screen.
    bool shown() const {
        for (const View* v = this; v; v = v->parent_)
            if (!v->visible_) return false;
        return true;
    }
    void setVisible(bool v) {
        if (v != visible_) visible_ = v, invalidate();
    }
    void invalidate() {
        if (Host* h = host()) h->invalidate(*this);
    }
    void setFrame(Rect r) {
        const bool sized = r.width() != frame_.width() || r.height() != frame_.height();
        frame_ = r;
        if (sized) resized();
        invalidate();
    }
    void setSize(int w, int h) { setFrame({frame_.left, frame_.top, frame_.left + w, frame_.top + h}); }
    // Children: painted in the order added (the last on top), and found under the mouse last first.
    void add(View& c, Rect frame) {
        if (c.parent_) c.parent_->remove(c);
        c.parent_ = this, c.host_ = nullptr;
        children_.push_back(&c);
        c.setFrame(frame);
    }
    void remove(View& c) {
        for (size_t i = 0; i < children_.size(); ++i)
            if (children_[i] == &c) {
                if (Host* h = host()) h->forget(c);
                children_.erase(children_.begin() + std::ptrdiff_t(i));
                c.parent_ = nullptr;
                invalidate();
                return;
            }
    }
    // Where (0, 0) of this view is in the root's coordinates.
    Point origin() const {
        Point o{0, 0};
        for (const View* v = this; v->parent_; v = v->parent_) o.x += v->frame_.left, o.y += v->frame_.top;
        return o;
    }
    bool isAncestorOf(const View& v) const {
        for (const View* p = v.parent_; p; p = p->parent_)
            if (p == this) return true;
        return false;
    }
    void focus() {
        if (Host* h = host()) h->focus(*this);
    }
    bool hasFocus() const {
        const Host* h = host();
        return h && h->hasFocus(*this);
    }

    // (For hosts.)
    void attach(Host* h) { host_ = h; }
    void paintAll(Canvas& c) {
        paint(c);
        for (View* ch : children_)
            if (ch->visible_ && !ch->frame_.empty()) {
                c.pushView(ch->frame_);
                ch->paintAll(c);
                c.popView();
            }
        paintOver(c);
    }
    // The deepest visible view at `pt` (this view's coordinates), and `pt` in its coordinates.
    View* viewAt(Point pt, Point& local) {
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            View* ch = *it;
            if (ch->visible_ && ch->frame_.contains(pt)) return ch->viewAt({pt.x - ch->frame_.left, pt.y - ch->frame_.top}, local);
        }
        local = pt;
        return this;
    }
    // The views Tab goes through, in order (visible, focusable).
    void focusChain(std::vector<View*>& out) {
        if (!visible_) return;
        if (focusable()) out.push_back(this);
        for (View* ch : children_) ch->focusChain(out);
    }

private:
    Host* host_ = nullptr;
    View* parent_ = nullptr;
    std::vector<View*> children_;
    Rect frame_;
    bool visible_ = true;
};

// What a host does with its root view: sends each event to the right view, keeps the focus, the mouse capture and
// the view under the mouse. Every host has one; coordinates in its events are the root's.
class Router {
public:
    explicit Router(View& root) : root_(root) {}
    View& root() const { return root_; }
    View* focused() const { return focused_; }
    View* captured() const { return captured_; }
    void setFocus(View* v) {
        if (v == focused_) return;
        View* old = focused_;
        focused_ = v;
        if (old) old->focusChanged(false);
        if (v) v->focusChanged(true);
    }
    // The view that gets the mouse (null: none). The view that had it is told with captureLost.
    void setCapture(View* v) {
        View* old = captured_;
        captured_ = v;
        if (old && old != v) old->captureLost();
    }
    // A view leaves the tree: nothing keeps pointing at it.
    void forget(const View& v) {
        auto gone = [&](View* p) { return p && (p == &v || v.isAncestorOf(*p)); };
        if (gone(focused_)) focused_ = nullptr;
        if (gone(captured_)) captured_ = nullptr;
        if (gone(hovered_)) hovered_ = nullptr;
        if (gone(pressed_)) pressed_ = nullptr;
    }

    bool mouse(MouseEvent e) {
        if (e.kind == MouseKind::Leave) {
            if (View* h = std::exchange(hovered_, nullptr)) h->mouse(e);
            return true;
        }
        if (captured_) {
            const Point o = captured_->origin();
            e.pt = {e.pt.x - o.x, e.pt.y - o.y};
            pressedAt(*captured_, e);
            if (e.kind == MouseKind::DoubleClick && !doubleClick_) e.kind = MouseKind::Down;
            return press(*captured_, e);
        }
        Point local;
        View* v = root_.viewAt(e.pt, local);
        pressedAt(*v, MouseEvent{e.kind, local, e.button});
        if (e.kind == MouseKind::DoubleClick && !doubleClick_) e.kind = MouseKind::Down;
        if (v != hovered_ && e.kind != MouseKind::Wheel && e.kind != MouseKind::Pinch && e.kind != MouseKind::SmartZoom &&
            e.kind != MouseKind::Swipe) {
            if (hovered_) {
                MouseEvent leave;
                leave.kind = MouseKind::Leave;
                hovered_->mouse(leave);
            }
            hovered_ = v;
        }
        if (e.kind == MouseKind::Down || e.kind == MouseKind::DoubleClick)  // the focus: the first focusable view up from it
            for (View* f = v; f; f = f->parent())
                if (f->focusable()) {
                    f->focus();
                    break;
                }
        for (View* t = v; t; t = t->parent()) {  // the view, then its parents, each in its own coordinates
            const Point o = t->origin();
            MouseEvent te = e;
            te.pt = {e.pt.x - o.x, e.pt.y - o.y};
            if (press(*t, te)) return true;
        }
        return false;
    }
    // A press or a double click on view `v` at `local` (its own point): a double click is one only when the press
    // before it was on the same view, with the same button, on the same item (View::sameItem); else it is a new press.
    void pressedAt(View& v, const MouseEvent& e) {
        if (e.kind != MouseKind::Down && e.kind != MouseKind::DoubleClick) return;
        doubleClick_ = e.kind == MouseKind::DoubleClick && pressed_ == &v && pressButton_ == e.button && v.sameItem(pressPt_, e.pt);
        pressed_ = &v, pressPt_ = e.pt, pressButton_ = e.button;
    }
    // A view gets the event. A double click (the system counts quick presses: the second and each next quick press is
    // a double click) that the view does not take comes to it again as a press: a view that does not have a separate
    // double-click action (a button, a stepper, a toggle, a diagram) acts on each quick press.
    static bool press(View& v, const MouseEvent& e) {
        if (v.mouse(e)) return true;
        if (e.kind != MouseKind::DoubleClick) return false;
        MouseEvent d = e;
        d.kind = MouseKind::Down;
        return v.mouse(d);
    }
    bool key(const KeyEvent& e) {
        for (View* t = focused_ ? focused_ : &root_; t; t = t->parent())
            if (t->key(e)) return true;
        if (e.down && e.key == Key::Tab && !(e.mods & (kCtrl | kAlt))) return tab((e.mods & kShift) != 0);
        return false;
    }
    bool text(const TextEvent& e) {
        for (View* t = focused_ ? focused_ : &root_; t; t = t->parent())
            if (t->text(e)) return true;
        return false;
    }
    bool compose(const CompositionEvent& e) { return focused_ && focused_->compose(e); }
    Cursor cursor(Point pt) {
        if (captured_) {
            const Point o = captured_->origin();
            return captured_->cursor({pt.x - o.x, pt.y - o.y});
        }
        Point local;
        return root_.viewAt(pt, local)->cursor(local);
    }
    std::wstring tip(Point pt) {
        Point local;
        for (View* v = root_.viewAt(pt, local); v; v = v->parent()) {
            const Point o = v->origin();
            std::wstring t = v->tip({pt.x - o.x, pt.y - o.y});
            if (!t.empty()) return t;
        }
        return {};
    }
    // The name Report feedback gives the point: each view's feedbackName, the top one first.
    std::wstring feedbackName(Point pt) {
        Point local;
        for (View* v = root_.viewAt(pt, local); v; v = v->parent()) {
            const Point o = v->origin();
            std::wstring t = v->feedbackName({pt.x - o.x, pt.y - o.y});
            if (!t.empty()) return t;
        }
        return {};
    }
    // Tab: the next (with Shift: the one before) focusable view.
    bool tab(bool back) {
        std::vector<View*> chain;
        root_.focusChain(chain);
        if (chain.empty()) return false;
        size_t i = 0;
        while (i < chain.size() && chain[i] != focused_) ++i;
        if (i == chain.size()) i = back ? 0 : chain.size() - 1;
        View* next = chain[back ? (i + chain.size() - 1) % chain.size() : (i + 1) % chain.size()];
        next->focus();
        if (focused_ == next) next->tabbedTo();
        return true;
    }

private:
    View& root_;
    View* focused_ = nullptr;
    View* captured_ = nullptr;
    View* hovered_ = nullptr;
    View* pressed_ = nullptr;  // the view of the last press (pressedAt), its point and button
    Point pressPt_;
    int pressButton_ = 0;
    bool doubleClick_ = false;  // the last press was a double click on the item of the press before it
};

}  // namespace stardraft::draw
