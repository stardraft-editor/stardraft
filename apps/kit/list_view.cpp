#include "list_view.h"

#include <algorithm>
#include <cmath>

namespace stardraft::kit {
namespace {

constexpr int kWheelRows = 3;       // rows a notch of the wheel scrolls
constexpr int kTypeAheadMs = 1000;  // a pause this long starts a new search
constexpr int kTextInset = 8;       // at 100 %: a row's text from its edge
constexpr int kCellInset = 4;       // at 100 %: a cell's text from its column's edges (as the table's)
constexpr int kHeaderPad = 6;       // at 100 %: the first column from the list's left edge
constexpr int kHeaderFont = 10;     // at 100 %: the header's letters (the list's small text)
constexpr auto kKeyA = static_cast<draw::Key>('A');

// ASCII capitals to small letters; the rest as it is.
std::wstring lowerAscii(std::wstring s) {
    for (auto& ch : s)
        if (ch >= L'A' && ch <= L'Z') ch = wchar_t(ch + 32);
    return s;
}

// UTF-8 text to wide characters. Bad bytes are skipped; characters past the BMP are dropped (no surrogates yet).
std::wstring fromUtf8(const std::string& s) {
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            w += wchar_t(b);
            ++i;
            continue;
        }
        const size_t n = (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : 0;  // continuation bytes
        if (n == 0 || i + n >= s.size()) {
            ++i;
            continue;
        }
        uint32_t u = b & (0x3Fu >> n);
        bool ok = true;
        for (size_t k = 1; k <= n && ok; ++k) {
            const unsigned char cb = static_cast<unsigned char>(s[i + k]);
            ok = (cb & 0xC0) == 0x80;
            u = (u << 6) | (cb & 0x3Fu);
        }
        if (ok && u <= 0xFFFF) w += wchar_t(u);
        i += ok ? n + 1 : 1;
    }
    return w;
}

// The alignment of a cell's text, vertically centered.
unsigned alignFlags(ColAlign a) { return (a == ColAlign::Right ? draw::kRight : a == ColAlign::Center ? draw::kCenter : draw::kLeft) | draw::kMiddle; }

}  // namespace

ListView::ListView() {
    add(bar_, draw::Rect{});
    bar_.onScroll = [this](int p) { userScroll(p); };
}

ListView::~ListView() {
    // Cancels the timer while the list has a host; without one, the alive flag (released after this) stops it.
    if (typeTimerOn_)
        if (draw::Host* h = host()) h->cancelTimer(typeTimer_);
}

// --- Geometry -----------------------------------------------------------------------------------------------------------

int ListView::headerH() const { return cols_.empty() ? 0 : px(kRowH); }
int ListView::viewH() const { return std::max(0, height() - headerH()); }
int ListView::rowPx() const { return px(rowH_); }
int ListView::maxTop() const { return std::max(0, contentH() - viewH()); }
int ListView::rowsRight() const { return width() - (contentH() > viewH() ? px(kScrollBarW) : 0); }
int ListView::rowHeight() const { return rowPx(); }

draw::Rect ListView::rowRect(int i) const {
    const int rh = rowPx(), y = headerH() + i * rh - top_;
    return {0, y, rowsRight(), y + rh};
}

int ListView::rowAt(draw::Point pt) const {
    const int rh = rowPx();
    if (rh <= 0 || pt.y < headerH() || pt.y >= height() || pt.x < 0 || pt.x >= rowsRight()) return -1;
    const int i = (pt.y - headerH() + top_) / rh;
    return i < count_ ? i : -1;
}

// The rows' edge and the bar's place follow the size and the content; the scroll position stays in range.
void ListView::layout() {
    top_ = std::clamp(top_, 0, maxTop());
    const int hh = headerH(), vh = viewH(), bw = px(kScrollBarW);
    const bool need = contentH() > vh;
    bar_.setFrame({width() - bw, hh, width(), hh + vh});
    bar_.setRange(contentH(), vh);
    bar_.setPos(top_);
    bar_.setVisible(need);
    invalidate();
}

void ListView::setScrollTop(int px_) {
    top_ = std::clamp(px_, 0, maxTop());
    bar_.setPos(top_);
    invalidate();
}

void ListView::userScroll(int p) {
    top_ = std::clamp(p, 0, maxTop());
    bar_.setPos(top_);
    invalidate();
}

void ListView::ensureVisible(int i) {
    if (i < 0 || i >= count_) return;
    const int rh = rowPx(), t = i * rh, b = t + rh, vh = viewH();
    if (t < top_) setScrollTop(t);
    else if (b > top_ + vh) setScrollTop(b - vh);
}

// --- Rows and columns ---------------------------------------------------------------------------------------------------

void ListView::setCount(int n) {
    count_ = std::max(0, n);
    sel_.resize(size_t(count_), false);
    cursor_ = std::min(cursor_, count_ - 1);
    anchor_ = std::min(anchor_, count_ - 1);
    if (hot_ >= count_) hot_ = -1;  // the row under the mouse is gone; the next Move finds the new one
    layout();
}

void ListView::setRowHeight(int h100) {
    rowH_ = std::max(1, h100);
    layout();
}

void ListView::setColumns(std::vector<Column> cols) {
    cols_ = std::move(cols);
    layout();
}

void ListView::setEmptyHint(std::wstring t) {
    hint_ = std::move(t);
    invalidate();
}

void ListView::setMode(SelectMode m) {
    if (m == mode_) return;
    mode_ = m;
    if (m == SelectMode::Multiple) return;
    // One row stays selected: the cursor's, else the first one.
    int keep = cursor_ >= 0 && isSelected(cursor_) ? cursor_ : -1;
    for (int i = 0; keep < 0 && i < count_; ++i)
        if (sel_[size_t(i)]) keep = i;
    std::fill(sel_.begin(), sel_.end(), false);
    if (keep >= 0) sel_[size_t(keep)] = true;
    invalidate();
}

// --- Selection ----------------------------------------------------------------------------------------------------------

bool ListView::isSelected(int i) const { return i >= 0 && i < count_ && sel_[size_t(i)]; }

std::vector<int> ListView::selection() const {
    std::vector<int> out;
    for (int i = 0; i < count_; ++i)
        if (sel_[size_t(i)]) out.push_back(i);
    return out;
}

void ListView::selectOnly(int i) {
    if (i < 0 || i >= count_) return;
    std::fill(sel_.begin(), sel_.end(), false);
    sel_[size_t(i)] = true;
    cursor_ = anchor_ = i;
    invalidate();
}

void ListView::toggle(int i) {
    if (i < 0 || i >= count_) return;
    if (mode_ == SelectMode::Single) return selectOnly(i);
    sel_[size_t(i)] = !sel_[size_t(i)];
    cursor_ = anchor_ = i;
    invalidate();
}

void ListView::selectAll() {
    if (mode_ != SelectMode::Multiple) return;
    std::fill(sel_.begin(), sel_.end(), true);
    invalidate();
}

void ListView::clearSelection() {
    std::fill(sel_.begin(), sel_.end(), false);
    invalidate();
}

// The rows from a to b (either order) are selected; `keep`: the others stay.
void ListView::selectSpan(int a, int b, bool keep) {
    if (!keep) std::fill(sel_.begin(), sel_.end(), false);
    for (int i = std::min(a, b); i <= std::max(a, b); ++i) sel_[size_t(i)] = true;
}

// The change is done (the rows scrolled to the cursor) before onSelect runs. onSelect may destroy the list, so it is
// copied first and nothing of the list is touched after it.
void ListView::apply(const std::function<void()>& change) {
    const auto before = sel_;
    change();
    invalidate();
    if (sel_ == before) return;
    const auto notify = onSelect;
    if (notify) notify();
}

void ListView::click(int row, unsigned mods) {
    if (row < 0) return;
    apply([&] {
        const bool multi = mode_ == SelectMode::Multiple;
        if (multi && (mods & draw::kShift)) {
            const int a = anchor_ >= 0 ? anchor_ : cursor_ >= 0 ? cursor_ : row;
            selectSpan(a, row, (mods & draw::kCtrl) != 0);
            cursor_ = row;
        } else if (multi && (mods & draw::kCtrl)) {
            toggle(row);
        } else {
            selectOnly(row);
        }
        ensureVisible(row);
    });
}

int ListView::moveTarget(draw::Key k) const {
    const int last = count_ - 1, page = std::max(1, viewH() / std::max(1, rowPx()));
    switch (k) {
    case draw::Key::Down: return cursor_ < 0 ? 0 : std::min(last, cursor_ + 1);
    case draw::Key::Up: return cursor_ < 0 ? last : std::max(0, cursor_ - 1);
    case draw::Key::Home: return 0;
    case draw::Key::End: return last;
    case draw::Key::PageDown: return cursor_ < 0 ? 0 : std::min(last, cursor_ + page);
    case draw::Key::PageUp: return cursor_ < 0 ? last : std::max(0, cursor_ - page);
    default: return cursor_;
    }
}

void ListView::navigate(int target, unsigned mods) {
    typed_.clear();
    lastLetter_ = 0;
    apply([&] {
        const bool multi = mode_ == SelectMode::Multiple;
        if (multi && (mods & draw::kShift)) {
            if (anchor_ < 0) anchor_ = cursor_ >= 0 ? cursor_ : target;
            selectSpan(anchor_, target, (mods & draw::kCtrl) != 0);
            cursor_ = target;
        } else if (multi && (mods & draw::kCtrl)) {
            cursor_ = target;  // the cursor moves; the selection stays
        } else {
            selectOnly(target);
        }
        ensureVisible(target);
    });
}

// --- Type-ahead ---------------------------------------------------------------------------------------------------------

// The timer may outlive the list when it was taken out of its parent (no host then, so no cancel): the callback checks
// the alive flag, which the list's destruction ends, before it touches the list.
void ListView::armTypeTimer() {
    draw::Host* h = host();
    if (!h) return;
    if (typeTimerOn_) h->cancelTimer(typeTimer_);
    typeTimer_ = h->timer(kTypeAheadMs, [this, alive = std::weak_ptr<bool>(alive_)] {
        if (alive.expired()) return;
        typed_.clear();
        typeTimerOn_ = false;
    });
    typeTimerOn_ = true;
}

// Typing a letter moves to the next row whose label starts with what was typed. Repeating one letter cycles through
// the rows that start with it.
void ListView::typeAhead(wchar_t ch) {
    if (count_ == 0) return;
    typed_ += ch;
    armTypeTimer();
    const bool repeated = typed_.size() > 1 && std::all_of(typed_.begin(), typed_.end(), [&](wchar_t c) { return c == typed_[0]; });
    const std::wstring want = lowerAscii(repeated ? typed_.substr(0, 1) : typed_);
    // A repeated letter, or the same letter as the last one-letter search (pressed again after a pause), goes past the
    // cursor's row. Other searches keep the cursor's row while it still matches, so "apple" typed from Apple stays on
    // Apple (as Windows lists do).
    const bool fresh = typed_.size() == 1;
    const wchar_t lc = (ch >= L'A' && ch <= L'Z') ? wchar_t(ch + 32) : ch;
    const bool past = repeated || (fresh && lc == lastLetter_);
    if (fresh) lastLetter_ = lc;
    const int start = past ? cursor_ + 1 : std::max(cursor_, 0);
    for (int k = 0; k < count_; ++k) {
        const int i = (start + k) % count_;
        const std::wstring s = lowerAscii(cellText(i, 0));
        if (s.size() >= want.size() && s.compare(0, want.size(), want) == 0) {
            apply([&] {
                selectOnly(i);
                ensureVisible(i);
            });
            return;
        }
    }
}

// --- Events -------------------------------------------------------------------------------------------------------------

bool ListView::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    switch (e.kind) {
    case MouseKind::Move: {
        const int r = rowAt(e.pt);
        if (r != hot_) hot_ = r, invalidate();
        return true;
    }
    case MouseKind::Leave:
        if (hot_ >= 0) hot_ = -1, invalidate();
        return true;
    case MouseKind::Down:
        typed_.clear();  // a click ends a search: a Space after it toggles, it does not search
        lastLetter_ = 0;
        if (e.button == 0) {
            click(rowAt(e.pt), e.mods);
            return true;
        }
        if (e.button == 1) {  // a right click: the row is selected unless it already is
            const int r = rowAt(e.pt);
            // The callbacks are copied first: one of them may destroy the list (see apply).
            const auto context = onContext;
            if (r >= 0 && !isSelected(r)) apply([&] { selectOnly(r); });
            if (context) context(r, e.pt);
            return true;
        }
        return false;
    case MouseKind::DoubleClick: {
        if (e.button != 0) return false;
        typed_.clear();
        const int r = rowAt(e.pt);
        if (r < 0) return true;
        const auto activate = onActivate;  // copied first: the click's selection may destroy the list
        // The first click of the pair selected the row already; a Ctrl pair must not toggle it again.
        if (!(e.mods & draw::kCtrl)) click(r, e.mods);
        if (activate) activate(r);
        return true;
    }
    case MouseKind::Wheel: {
        if (e.horizontal || maxTop() <= 0) return false;
        scrollBy(int(std::lround(-e.wheel * float(kWheelRows * rowPx()))));
        hot_ = rowAt(e.pt);
        return true;
    }
    default: return false;
    }
}

bool ListView::key(const draw::KeyEvent& e) {
    if (!e.down) return false;
    using draw::Key;
    const unsigned m = e.mods;
    switch (e.key) {
    case Key::Up:
    case Key::Down:
    case Key::Home:
    case Key::End:
    case Key::PageUp:
    case Key::PageDown:
        if (count_ == 0) return false;
        navigate(moveTarget(e.key), m);
        return true;
    case Key::Enter:
        if (cursor_ < 0 || !onActivate) return false;
        onActivate(cursor_);
        return true;
    case Key::Space:
        if (!typed_.empty()) {  // a space in a search ("apple pie")
            typeAhead(L' ');
            return true;
        }
        if (cursor_ < 0) return false;
        apply([&] { toggle(cursor_); });
        return true;
    case Key::Escape:
        typed_.clear();
        return false;
    default:
        if (e.key == kKeyA && (m & draw::kCtrl) && mode_ == SelectMode::Multiple && count_ > 0) {
            apply([&] { selectAll(); });
            return true;
        }
        return false;
    }
}

bool ListView::text(const draw::TextEvent& e) {
    if (count_ == 0) return false;
    bool used = false;
    for (wchar_t ch : fromUtf8(e.utf8))
        if (ch > 32 && ch != 127) {  // a space comes as a key (see key)
            typeAhead(ch);
            used = true;
        }
    return used;
}

// --- Painting -----------------------------------------------------------------------------------------------------------

std::wstring ListView::cellText(int i, size_t k) const {
    if (k == 0 && label) return label(i);
    if (cell) return cell(i, k);
    return {};
}

draw::Rect ListView::colRect(draw::Rect row, size_t k) const {
    int fixed = 0, flexible = 0;
    for (const auto& c : cols_) {
        if (c.width) fixed += px(c.width);
        else ++flexible;
    }
    const int rest = std::max(0, row.width() - px(12) - fixed);
    int x = row.left + px(kHeaderPad);
    for (size_t i = 0; i < cols_.size(); ++i) {
        const int w = cols_[i].width ? px(cols_[i].width) : rest / std::max(1, flexible);
        if (i == k) return {x, row.top, x + w, row.bottom};
        x += w;
    }
    return {x, row.top, x, row.bottom};
}

void ListView::paintHeader(draw::Canvas& c, int rw, int hh) {
    const Theme& th = theme();
    const draw::Rect row{0, 0, rw, hh};
    const draw::Font of = c.setFont(c.font(draw::FontDesc{draw::Face::Ui, px(kHeaderFont)}));
    for (size_t k = 0; k < cols_.size(); ++k) {
        draw::Rect cr = colRect(row, k);
        cr.left += px(kCellInset), cr.right -= px(kCellInset);
        c.text(cr, cols_[k].name, th.dim, alignFlags(cols_[k].align));
    }
    c.setFont(of);
    c.fill({0, hh - 1, rw, hh}, th.border);
}

void ListView::paintRowAt(draw::Canvas& c, int i, draw::Rect row) {
    const Theme& th = theme();
    RowState s;
    s.selected = isSelected(i);
    s.hot = i == hot_;
    s.focused = i == cursor_ && hasFocus();
    if (paintRow) {
        paintRow(c, i, row, s);
        return;
    }
    const draw::Color text = s.selected ? th.accent : th.text;
    if (!cols_.empty()) {  // the table's row: a rounded pill when selected or under the mouse
        const draw::Rect pill{row.left + px(2), row.top + px(1), row.right - px(2), row.bottom - px(1)};
        if (s.selected) c.roundBox(pill, th.sel, th.sel, px(6));
        else if (s.hot) c.roundBox(pill, th.rowHot, th.rowHot, px(6));
        for (size_t k = 0; k < cols_.size(); ++k) {
            draw::Rect cr = colRect(row, k);
            cr.left += px(kCellInset), cr.right -= px(kCellInset);
            c.text(cr, cellText(i, k), text, alignFlags(cols_[k].align) | draw::kEllipsis);
        }
    } else {  // the list row: selected with a 2 px accent bar at the left, or under the mouse
        if (s.selected) {
            c.fill(row, th.sel);
            c.fill({row.left, row.top, row.left + px(2), row.bottom}, th.accent);
        } else if (s.hot) {
            c.fill(row, th.rowHot);
        }
        c.text({row.left + px(kTextInset), row.top, row.right - px(kTextInset), row.bottom}, cellText(i, 0), text,
               draw::kLeft | draw::kMiddle | draw::kEllipsis);
    }
    if (s.focused) c.frame(row, th.accent);
}

void ListView::paint(draw::Canvas& c) {
    const Theme& th = theme();
    const int hh = headerH(), rw = rowsRight(), vh = viewH();
    if (!cols_.empty()) paintHeader(c, rw, hh);
    c.pushClip({0, hh, rw, height()});
    if (count_ == 0) {
        if (!hint_.empty()) {
            const int pad = px(kDialogPad);
            const draw::Point s = c.measureBox(hint_, std::max(1, rw - 2 * pad), draw::kWrap);
            const int y = hh + (vh - s.y) / 2;
            c.text({pad, y, rw - pad, y + s.y}, hint_, th.dim, draw::kCenter | draw::kWrap);
        }
    } else if (vh > 0 && rowPx() > 0) {
        const int rh = rowPx();
        const int first = top_ / rh, last = std::min(count_ - 1, (top_ + vh - 1) / rh);
        for (int i = first; i <= last; ++i) paintRowAt(c, i, rowRect(i));
    }
    c.popClip();
}

}  // namespace stardraft::kit
