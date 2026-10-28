#include "dropdown.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <memory>

#include "text_model.h"
#include "theme.h"

namespace stardraft::kit {

using draw::Canvas;
using draw::Color;
using draw::KeyEvent;
using draw::Key;
using draw::MouseEvent;
using draw::MouseKind;
using draw::Point;
using draw::Rect;
using draw::View;
using draw::WindowKind;
using draw::WindowSpec;

namespace {

constexpr int kVisibleRows = 12;  // the list shows this many rows, then it scrolls
constexpr int kIconChevron = 82;  // UiIcon::Dropdown (apps/editor/res/ui_icons.h)

int gOpenLists = 0;  // the dropdowns whose list is open (Dropdown::anyListOpen)

}  // namespace

// The list under the field: one row per item, the current value highlighted. Its callbacks pick a row or cancel.
class Dropdown::List final : public View {
public:
    List(std::vector<DropdownItem> items, int current, std::function<void(int)> pick, std::function<void()> cancel)
        : items_(std::move(items)), current_(current), pick_(std::move(pick)), cancel_(std::move(cancel)) {
        hot_ = current_ >= 0 ? current_ : (items_.empty() ? -1 : 0);
        show(hot_);
    }

    void paint(Canvas& c) override {
        const auto& th = kit::theme();
        c.roundBox({0, 0, width(), height()}, th.ctl, th.border, px(kRadius));
        const int last = std::min(int(items_.size()), top_ + visible());
        for (int i = top_; i < last; ++i) {
            const Rect r = row(i);
            const DropdownItem& it = items_[size_t(i)];
            const bool sel = i == current_;
            if (sel) {
                c.fill(r, th.sel);
                c.fill({r.left, r.top, r.left + px(2), r.bottom}, th.accent);
            } else if (i == hot_) {
                c.fill(r, th.rowHot);
            }
            const Color fg = sel ? th.accent : th.text;
            if (it.icon >= 0) c.icon({r.left + px(6), r.top + px(4), r.left + px(22), r.bottom - px(4)}, it.icon, fg, px(16));
            if (it.swatch != draw::kNone) {  // (a color list: the item's square, as on the closed field)
                const int s = px(12), sx = r.left + px(10), sy = (r.top + r.bottom - s) / 2;
                c.fill({sx, sy, sx + s, sy + s}, it.swatch);
            }
            const bool hasNumber = it.number >= 0;
            c.text({r.left + px(32), r.top, r.right - (hasNumber ? px(56) : px(10)), r.bottom}, it.label, fg, draw::kMiddle | draw::kEllipsis);
            if (hasNumber) c.text({r.right - px(64), r.top, r.right - px(10), r.bottom}, std::to_wstring(it.number), th.dim, draw::kMiddle | draw::kRight);
        }
    }

    bool mouse(const MouseEvent& e) override {
        switch (e.kind) {
        case MouseKind::Move:
            if (const int i = rowAt(e.pt); i != hot_) hot_ = i, invalidate();
            return true;
        case MouseKind::Leave:
            if (hot_ != -1) hot_ = -1, invalidate();
            return true;
        case MouseKind::Down:
        case MouseKind::DoubleClick: return e.button == 0;
        case MouseKind::Up:
            if (e.button == 0)
                if (const int i = rowAt(e.pt); i >= 0) pick_(i);
            return e.button == 0;
        case MouseKind::Wheel: {  // positive: away from the user, the list scrolls up; a part of a notch waits
            int notches = 0;
            if (e.exact) notches = draw::exactSteps(e, float(rowH()), wheelAcc_);  // (macOS's trackpad: rows of its distance)
            else {
                wheelAcc_ += e.wheel;
                notches = int(wheelAcc_);  // whole notches, toward zero
                wheelAcc_ -= float(notches);
            }
            const int before = top_;
            top_ = std::clamp(top_ - notches, 0, maxTop());
            if (top_ != before) invalidate();
            return true;
        }
        default: return false;
        }
    }

    // Typed letters jump to the next item whose label starts with them, as a combo box's list does. The letters
    // count on for a second after the last one; one letter typed again (or the same one over and over) moves on to
    // the next item that starts with it.
    bool text(const draw::TextEvent& e) override {
        const int n = int(items_.size());
        const std::wstring typed = fromUtf8(e.utf8);
        if (!n || typed.empty() || typed.find_first_of(L" \t\r\n") != std::wstring::npos) return false;
        const auto now = std::chrono::steady_clock::now();
        if (now - typedAt_ > std::chrono::seconds(1)) typed_.clear();
        typedAt_ = now;
        typed_ += typed;
        const bool again = std::all_of(typed_.begin(), typed_.end(), [&](wchar_t c) { return c == typed_[0]; });
        const std::wstring prefix = again ? std::wstring(1, typed_[0]) : typed_;
        const int from = again ? hot_ + 1 : std::max(hot_, 0);
        for (int k = 0; k < n; ++k) {
            const int i = (from + k) % n;
            const std::wstring& label = items_[size_t(i)].label;
            if (label.size() >= prefix.size() &&
                std::equal(prefix.begin(), prefix.end(), label.begin(), [](wchar_t a, wchar_t b) {
                    return std::towlower(a) == std::towlower(b);
                })) {
                if (i != hot_) setHot(i);
                break;
            }
        }
        return true;
    }

    bool key(const KeyEvent& e) override {
        if (!e.down) return false;
        const int n = int(items_.size());
        switch (e.key) {
        case Key::Up: setHot(hot_ <= 0 ? 0 : hot_ - 1); return true;
        case Key::Down: setHot(hot_ < 0 ? 0 : std::min(n - 1, hot_ + 1)); return true;
        case Key::Home: setHot(0); return true;
        case Key::End: setHot(n - 1); return true;
        case Key::Enter:
        case Key::Space:
            if (hot_ >= 0) pick_(hot_);
            return true;
        case Key::Escape:
        case Key::Tab: cancel_(); return true;
        default: return false;
        }
    }

private:
    int rowH() const { return px(kRowH); }
    int visible() const { return std::min(int(items_.size()), kVisibleRows); }
    int maxTop() const { return std::max(0, int(items_.size()) - visible()); }
    Rect row(int i) const {
        const int y = px(2) + (i - top_) * rowH();
        return {px(2), y, width() - px(2), y + rowH()};
    }
    int rowAt(Point p) const {
        for (int i = top_; i < std::min(int(items_.size()), top_ + visible()); ++i)
            if (row(i).contains(p)) return i;
        return -1;
    }
    void show(int i) {
        if (i < 0) return;
        if (i < top_) top_ = i;
        else if (i >= top_ + visible()) top_ = i - visible() + 1;
    }
    void setHot(int i) {
        hot_ = i;
        show(i);
        invalidate();
    }

    std::vector<DropdownItem> items_;
    int current_ = -1, hot_ = -1, top_ = 0;
    float wheelAcc_ = 0;  // the wheel's notches that are not a whole one yet
    std::wstring typed_;  // the letters typed in the last second
    std::chrono::steady_clock::time_point typedAt_;
    std::function<void(int)> pick_;
    std::function<void()> cancel_;
};

Dropdown::Dropdown() = default;

Dropdown::Dropdown(std::vector<DropdownItem> items, int value) : items_(std::move(items)), value_(value) {
    if (value_ >= int(items_.size())) value_ = -1;
}

Dropdown::~Dropdown() {
    alive_.reset();  // the list's callbacks do nothing from now on
    if (listOpen_ && listHost_) listHost_->closeWindow(*list_);  // (an open list has its host: see retireList)
    setOpen(false);
    retireList();
}

bool Dropdown::anyListOpen() { return gOpenLists > 0; }

void Dropdown::setOpen(bool on) {
    if (on == listOpen_) return;
    listOpen_ = on;
    gOpenLists += on ? 1 : -1;
}

void Dropdown::retireList() {
    if (!list_) return;
    if (listHost_) {
        listHost_->forget(*list_);  // the host must not keep pointing at the list
        auto dead = std::shared_ptr<List>(std::move(list_));
        listHost_->timer(0, [dead] {});  // the list goes after the event that may be running in it
    } else {
        list_.reset();
    }
    listHost_ = nullptr;  // a closed list keeps no host: the host may go before the dropdown does
}

void Dropdown::setItems(std::vector<DropdownItem> items) {
    closeList();
    items_ = std::move(items);
    if (value_ >= int(items_.size())) value_ = -1;
    invalidate();
}

void Dropdown::setValue(int i) {
    const int v = i < 0 || i >= int(items_.size()) ? -1 : i;
    if (v == value_) return;
    value_ = v;
    invalidate();
}

void Dropdown::setChanged(bool on) {
    if (on == changed_) return;
    changed_ = on;
    invalidate();
}

void Dropdown::setEnabled(bool e) {
    if (e == enabled_) return;
    enabled_ = e;
    if (!e) closeList();
    invalidate();
}

void Dropdown::openList() {
    if (onOpen) {
        if (enabled_) onOpen();
        return;
    }
    if (listOpen_ || !enabled_ || items_.empty()) return;
    draw::Host* h = host();
    if (!h) return;
    std::weak_ptr<bool> alive = alive_;
    retireList();  // the list made before (closed) goes now
    list_ = std::make_unique<List>(
        items_, value_, [this](int i) { pick(i); }, [this] { closeList(); });
    listHost_ = h;
    const int rows = std::min(int(items_.size()), kVisibleRows);
    const int top = height() + px(2);
    const Rect at{0, top, width(), top + rows * px(kRowH) + 2 * px(2)};
    setOpen(true);
    justClosed_ = false;
    h->openWindow(*this, *list_, WindowSpec{WindowKind::Popup, at, L""}, [alive, this] {
        if (!alive.lock()) return;
        setOpen(false);  // closed by the host (a click outside, or its window going): the click's press does not open it again
        justClosed_ = true;
        retireList();
        invalidate();
        if (enabled_) focus();  // the keys are the field's again
    });
    h->focus(*list_);
    invalidate();
}

void Dropdown::closeList() {
    if (!listOpen_) return;
    setOpen(false);
    if (listHost_) listHost_->closeWindow(*list_);
    retireList();
    invalidate();
    if (enabled_) focus();  // a disabled field cannot take the focus
}

void Dropdown::pick(int i) {
    closeList();
    if (i < 0 || i >= int(items_.size())) return;
    if (i == value_) {
        if (onSame) onSame(i);
        return;
    }
    value_ = i;
    invalidate();
    if (onChange) onChange(i);
}

// Up and Down: the next or the previous item (from none: the first or the last). No wrap.
void Dropdown::step(int dir) {
    const int n = int(items_.size());
    if (!n || onOpen) return;
    const int next = value_ < 0 ? (dir > 0 ? 0 : n - 1) : std::clamp(value_ + dir, 0, n - 1);
    if (next == value_) return;
    value_ = next;
    invalidate();
    if (onChange) onChange(value_);
}

void Dropdown::paint(Canvas& c) {
    const auto& th = kit::theme();
    const Rect r{0, 0, width(), height()};
    Color fill = th.ctl, line = th.border;
    if (!enabled_) fill = th.readOnly, line = th.readOnlyLine;
    else if (changed_) fill = th.goodFill, line = th.goodLine;
    if (enabled_ && (hasFocus() || listOpen_)) line = th.accent;
    else if (enabled_ && !changed_ && hover_) line = th.borderHot;
    c.roundBox(r, fill, line, px(kRadius));
    if (value_ >= 0 && value_ < int(items_.size())) {
        const DropdownItem& it = items_[size_t(value_)];
        int tx = r.left + px(8);
        if (it.icon >= 0) {
            c.icon({tx, r.top + px(3), tx + px(18), r.bottom - px(3)}, it.icon, enabled_ ? th.text : th.faint, px(16));
            tx += px(24);
        }
        // A list with squares (a color list): the square at 10 px where the item has one, and every label after seven
        // spaces' width (as the program's pickers had it, Random and Player's choice too).
        bool squares = false;
        for (const DropdownItem& i : items_) squares = squares || i.swatch != draw::kNone;
        if (it.swatch != draw::kNone) {
            const int s = px(12), sx = r.left + px(10), sy = (r.top + r.bottom - s) / 2;
            c.fill({sx, sy, sx + s, sy + s}, it.swatch);
        }
        if (squares) tx = r.left + px(8) + c.measure(L"       ");
        const bool hasNumber = it.number >= 0;
        c.text({tx, r.top, r.right - (hasNumber ? px(56) : px(22)), r.bottom}, it.label, !enabled_ ? th.faint : changed_ ? th.good : th.text,
               draw::kMiddle | draw::kEllipsis);
        if (hasNumber) c.text({r.right - px(64), r.top, r.right - px(22), r.bottom}, std::to_wstring(it.number), th.dim, draw::kMiddle | draw::kRight);
    }
    if (enabled_) c.icon({r.right - px(20), r.top, r.right - px(6), r.bottom}, kIconChevron, hover_ || listOpen_ ? th.text : th.dim, px(16));
}

bool Dropdown::mouse(const MouseEvent& e) {
    switch (e.kind) {
    case MouseKind::Move:
        justClosed_ = false;
        if (!hover_) hover_ = true, invalidate();
        return true;
    case MouseKind::Leave:
        if (hover_) hover_ = false, invalidate();
        return true;
    case MouseKind::Down:
        if (e.button != 0 || !enabled_) return false;
        if (listOpen_) {  // the field is the list's button too: a press closes it
            closeList();
            return true;
        }
        if (justClosed_) return justClosed_ = false, true;
        openList();
        return true;
    case MouseKind::DoubleClick: return enabled_;  // the press has opened (or closed) the list already
    case MouseKind::Wheel: {
        if (!enabled_ || listOpen_ || items_.empty() || !wheelSteps) return false;
        wheelAcc_ += e.wheel;
        while (wheelAcc_ >= 1) wheelAcc_ -= 1, step(-1);
        while (wheelAcc_ <= -1) wheelAcc_ += 1, step(1);
        return true;
    }
    default: return false;
    }
}

bool Dropdown::key(const KeyEvent& e) {
    if (!e.down || !enabled_) return false;
    // The open list's keys come here where the system gives the keys to the window under the list (macOS: a pop-up
    // window does not get the keyboard): the list takes them, and a letter or a digit is its typing (type to jump).
    if (listOpen_ && list_) {
        const int k = int(e.key);
        const bool typed = !(e.mods & (draw::kCtrl | draw::kAlt)) && ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9'));
        if (typed) {
            draw::TextEvent t;
            t.utf8 = std::string(1, char(k >= 'A' && k <= 'Z' ? k - 'A' + 'a' : k));
            list_->text(t);
            return true;
        }
        if (list_->key(e)) return true;
    }
    switch (e.key) {
    case Key::Up: step(-1); return true;
    case Key::Down:
        if (e.mods & draw::kAlt) openList();
        else step(1);
        return true;
    case Key::Enter:
        if (!enterOpens) return false;
        openList();
        return true;
    case Key::Space: openList(); return true;
    default: return false;
    }
}

}  // namespace stardraft::kit
