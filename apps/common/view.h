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
    // The tooltip at `pt` (empty: none).
    virtual std::wstring tip(Point) { return {}; }
    // Whether the point is over the window's size grip (the bottom right corner of a window that resizes from it): a
    // Win32 host then answers the hit test with the sizing corner, so the window's edge is dragged from there.
    virtual bool sizeGrip(Point) { return false; }
    // true: the host shows tip() as a tooltip while the mouse is over the view (the text is asked for where it is).
    virtual bool hoverTip() const { return false; }
    // Whether Tab stops on it (and a click gives it the focus).
    virtual bool focusable() const { return false; }
    virtual void focusChanged(bool /*has*/) {}
    // The mouse capture left the view (it went to another view, the system took it, or the view let it go): the view
    // ends what the capture was for.
    virtual void captureLost() {}
    virtual void resized() {}

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
    }

    bool mouse(MouseEvent e) {
        if (e.kind == MouseKind::Leave) {
            if (View* h = std::exchange(hovered_, nullptr)) h->mouse(e);
            return true;
        }
        if (captured_) {
            const Point o = captured_->origin();
            e.pt = {e.pt.x - o.x, e.pt.y - o.y};
            return captured_->mouse(e);
        }
        Point local;
        View* v = root_.viewAt(e.pt, local);
        if (v != hovered_ && e.kind != MouseKind::Wheel) {
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
            if (t->mouse(te)) return true;
        }
        return false;
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
    // Tab: the next (with Shift: the one before) focusable view.
    bool tab(bool back) {
        std::vector<View*> chain;
        root_.focusChain(chain);
        if (chain.empty()) return false;
        size_t i = 0;
        while (i < chain.size() && chain[i] != focused_) ++i;
        if (i == chain.size()) i = back ? 0 : chain.size() - 1;
        chain[back ? (i + chain.size() - 1) % chain.size() : (i + 1) % chain.size()]->focus();
        return true;
    }

private:
    View& root_;
    View* focused_ = nullptr;
    View* captured_ = nullptr;
    View* hovered_ = nullptr;
};

}  // namespace stardraft::draw
