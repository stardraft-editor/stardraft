#include "text_field.h"

#include <algorithm>

namespace stardraft::kit {

namespace {

constexpr int kBlinkMs = 530;       // the caret's half period
constexpr int kTripleMs = 500;      // a click this soon after a double click, on the same spot, is a triple click
constexpr size_t kNone = size_t(-1);

// The index after the character at `e` (a surrogate pair is one character), not past `end`.
size_t nextIndex(const std::wstring& s, size_t e, size_t end) {
    size_t ne = e + 1;
    if (ne < end && isHighSurrogate(s[e]) && isLowSurrogate(s[ne])) ++ne;
    return ne;
}

}  // namespace

TextField::TextField() : alive_(std::make_shared<bool>(true)) {}

TextField::~TextField() {
    *alive_ = false;
    stopBlink();
}

// --- Settings -----------------------------------------------------------------------------------------------------

void TextField::setText(std::wstring t) {
    model_.setText(std::move(t));
    comp_.clear();
    compCur_ = 0;
    ++style_;
    invalidate();
}

void TextField::setPlaceholder(std::wstring t) {
    placeholder_ = std::move(t);
    invalidate();
}

void TextField::setMultiline(bool on) {
    model_.setMultiline(on);
    ++style_;
    invalidate();
}

void TextField::setWrap(bool on) {
    wrap_ = on;
    ++style_;
    invalidate();
}

void TextField::setReadOnly(bool on) {
    model_.setReadOnly(on);
    invalidate();
}

void TextField::setChanged(bool on) {
    changed_ = on;
    invalidate();
}

void TextField::setSpans(std::vector<Span> spans) {
    std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
    spans_ = std::move(spans);
    ++style_;
    invalidate();
}

void TextField::setFont(draw::FontDesc f) {
    fontDesc_ = f;
    ++style_;
    invalidate();
}

// --- Shown text: the buffer with the composition in place of the selection ---------------------------------------

size_t TextField::displayOf(size_t m) const {
    if (!composing()) return m;
    const size_t lo = model_.selectionStart(), hi = model_.selectionEnd(), cl = comp_.size();
    if (m <= lo) return m;
    if (m < hi) return lo + cl;
    return m - (hi - lo) + cl;
}

size_t TextField::modelOf(size_t d) const {
    if (!composing()) return d;
    const size_t lo = model_.selectionStart(), hi = model_.selectionEnd(), cl = comp_.size();
    if (d < lo) return d;
    if (d < lo + cl) return lo;
    return d - cl + (hi - lo);
}

size_t TextField::displayCaret() const {
    return composing() ? model_.selectionStart() + compCur_ : displayOf(model_.caret());
}

const Span* TextField::spanAt(size_t m) const {
    auto it = std::upper_bound(spans_.begin(), spans_.end(), m, [](size_t v, const Span& s) { return v < s.start; });
    if (it == spans_.begin()) return nullptr;
    --it;
    return m < it->start + it->length ? &*it : nullptr;
}

TextField::Attr TextField::attrAt(size_t d) const {
    Attr a;
    a.color = theme().text;
    const size_t lo = model_.selectionStart(), hi = model_.selectionEnd(), cl = comp_.size();
    if (composing() && d >= lo && d < lo + cl) {
        a.comp = true;
        return a;
    }
    const size_t m = modelOf(d);
    if (!composing() && lo < hi && m >= lo && m < hi) a.selected = true;
    if (const Span* s = spanAt(m)) {
        if (s->color != draw::kNone) a.color = s->color;
        a.bold = s->bold;
    }
    if (a.selected) a.color = theme().onText;
    return a;
}

std::vector<TextField::Run> TextField::runs(size_t from, size_t to) const {
    std::vector<Run> out;
    for (size_t d = from; d < to; ++d) {
        const Attr a = attrAt(d);
        if (!out.empty()) {
            Attr& p = out.back().attr;
            if (p.color == a.color && p.bold == a.bold && p.selected == a.selected && p.comp == a.comp) {
                out.back().to = d + 1;
                continue;
            }
        }
        out.push_back({d, d + 1, a});
    }
    return out;
}

// --- Layout -------------------------------------------------------------------------------------------------------

void TextField::relayout(draw::Canvas& c) {
    const draw::Font prev = c.setFont(c.font(fontDesc_));
    lineH_ = std::max(1, c.metrics().height);

    if (composing()) {
        const size_t lo = model_.selectionStart(), hi = model_.selectionEnd();
        shown_ = model_.text().substr(0, lo) + comp_ + model_.text().substr(hi);
    } else {
        shown_ = model_.text();
    }
    const size_t n = shown_.size();
    px_.assign(n + 1, 0);
    int x = 0;
    bool bold = fontDesc_.bold;  // the font the canvas has now
    for (size_t d = 0; d < n;) {
        const bool b = fontDesc_.bold || attrAt(d).bold;
        if (b != bold) {
            draw::FontDesc f = fontDesc_;
            f.bold = b;
            c.setFont(c.font(f));
            bold = b;
        }
        const size_t len = nextIndex(shown_, d, n) - d;
        const int w = shown_[d] == '\n' ? 0 : c.measure(std::wstring_view(shown_).substr(d, len));
        px_[d] = x;
        if (len == 2) px_[d + 1] = x;  // the second unit of a pair has no place of its own
        x += w;
        px_[d + len] = x;
        d += len;
    }
    c.setFont(prev);

    const int vw = std::max(1, width() - 2 * padX());
    buildLines(vw);
    barShown_ = multiline() && int(lines_.size()) * lineH_ > inner().height();
    if (barShown_ && wrap_) buildLines(std::max(1, vw - px(8)));  // the scroll bar's place
    contentW_ = 0;
    for (const Line& L : lines_) contentW_ = std::max(contentW_, px_[L.end] - px_[L.start]);
    layoutKey_ = LayoutKey{model_.revision(), style_, width()};
    laidOut_ = true;
}

void TextField::buildLines(int vw) {
    lines_.clear();
    const size_t n = shown_.size();
    if (!multiline()) {
        lines_.push_back({0, n, true});
        return;
    }
    size_t ps = 0;
    for (;;) {
        size_t pe = ps;
        while (pe < n && shown_[pe] != '\n') ++pe;
        if (!wrap_ || ps == pe) {
            lines_.push_back({ps, pe, true});
        } else {
            // Greedy: as many characters as fit; the break goes after the last space that fits, else inside the word.
            size_t s = ps;
            for (;;) {
                size_t e = s, space = kNone;
                while (e < pe) {
                    const size_t ne = nextIndex(shown_, e, pe);
                    if (px_[ne] - px_[s] > vw) break;
                    if (shown_[e] == ' ') space = e;
                    e = ne;
                }
                if (e >= pe) {
                    lines_.push_back({s, pe, true});
                    break;
                }
                const size_t brk = space != kNone ? space + 1 : (e > s ? e : nextIndex(shown_, s, pe));
                if (brk >= pe) {
                    lines_.push_back({s, pe, true});
                    break;
                }
                lines_.push_back({s, brk, false});
                s = brk;
            }
        }
        if (pe >= n) break;
        ps = pe + 1;
    }
}

size_t TextField::lineOf(size_t d) const {
    // The last line that starts at or before d (a wrapped line's end is the next line's start: d belongs to the next).
    size_t lo = 0, hi = lines_.size();
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (lines_[mid].start <= d) lo = mid;
        else hi = mid;
    }
    return lo;
}

size_t TextField::posAtX(size_t li, int x) const {
    const Line& L = lines_[li];
    const int target = px_[L.start] + x;
    const auto first = px_.begin() + std::ptrdiff_t(L.start), last = px_.begin() + std::ptrdiff_t(L.end) + 1;
    const auto it = std::lower_bound(first, last, target);
    size_t d = L.end;
    if (it != last) {
        d = size_t(it - px_.begin());
        if (d > L.start && target - px_[d - 1] < px_[d] - target) --d;
    }
    if (d > L.start && d < shown_.size() && isLowSurrogate(shown_[d]) && isHighSurrogate(shown_[d - 1])) --d;
    return d;
}

size_t TextField::caretFromPoint(draw::Point pt) const {
    if (lines_.empty()) return model_.caret();
    const draw::Rect in = inner();
    size_t li = 0;
    if (multiline()) {
        const int y = pt.y - in.top + scrollY_;
        li = y < 0 ? 0 : std::min(lines_.size() - 1, size_t(y / lineH_));
    }
    return modelOf(posAtX(li, pt.x - in.left + scrollX_));
}

draw::Rect TextField::inner() const { return draw::Rect{padX(), padY(), width() - padX(), height() - padY()}; }

int TextField::lineTop(size_t li) const {
    const draw::Rect in = inner();
    if (!multiline()) return in.top + (in.height() - lineH_) / 2;
    return in.top + int(li) * lineH_ - scrollY_;
}

void TextField::clampScroll() {
    const draw::Rect in = inner();
    const int content = int(lines_.size()) * lineH_;
    const int maxY = multiline() ? std::max(0, content - in.height()) : 0;
    scrollY_ = std::clamp(scrollY_, 0, maxY);
    const int maxX = (multiline() && wrap_) ? 0 : std::max(0, contentW_ + 2 - in.width());  // room for the caret at the end
    scrollX_ = std::clamp(scrollX_, 0, maxX);
}

void TextField::ensureCaretVisible() {
    const EnsureKey k{model_.revision(), model_.caret(), model_.anchor(), displayCaret()};
    if (k == ensured_) return;  // only a move of the caret scrolls to it; the wheel may take it away
    ensured_ = k;
    const draw::Rect in = inner();
    const size_t dc = displayCaret();
    const size_t li = lineOf(dc);
    const int cx = px_[dc] - px_[lines_[li].start];
    if (!(multiline() && wrap_)) {
        if (cx < scrollX_) scrollX_ = cx;
        else if (cx + 2 > scrollX_ + in.width()) scrollX_ = cx + 2 - in.width();
    }
    if (multiline()) {
        const int top = int(li) * lineH_;
        if (top < scrollY_) scrollY_ = top;
        else if (top + lineH_ > scrollY_ + in.height()) scrollY_ = top + lineH_ - in.height();
    }
    clampScroll();
}

void TextField::updateCaretRect() {
    if (!fresh()) return;
    const size_t dc = displayCaret();
    const size_t li = lineOf(dc);
    const int x = inner().left - scrollX_ + (px_[dc] - px_[lines_[li].start]);
    const int top = lineTop(li);
    caretRect_ = draw::Rect{x, top, x + 1, top + lineH_};
}

// --- Painting -----------------------------------------------------------------------------------------------------

void TextField::paint(draw::Canvas& c) {
    const Theme& th = theme();
    // The field box, as ui_kit.inc's fieldBox: Hot, ReadOnly, Changed, then Focus over them.
    draw::Color fill = th.ctl, line = th.border;
    if (hot_) line = th.borderHot;
    if (model_.readOnly()) fill = th.readOnly, line = th.readOnlyLine;
    if (changed_) fill = th.goodFill, line = th.goodLine;
    if (hasFocus()) line = th.accent;
    c.roundBox(draw::Rect{0, 0, width(), height()}, fill, line, px(kRadius));

    const draw::Font prev = c.setFont(c.font(fontDesc_));
    if (!fresh()) relayout(c);
    clampScroll();
    ensureCaretVisible();

    const draw::Rect in = inner();
    c.pushClip(in);
    if (!composing() && model_.text().empty()) {
        if (!placeholder_.empty()) {
            unsigned flags = draw::kLeft;  // several lines: at the top; one line: in the middle
            if (!multiline()) flags |= draw::kMiddle;
            else if (wrap_) flags |= draw::kWrap;
            c.text(in, placeholder_, th.faint, flags);
        }
    } else {
        const int baseX = in.left - scrollX_;
        const int spaceW = c.measure(L" ");
        size_t first = 0, last = lines_.size();
        if (multiline()) {
            first = std::min(lines_.size(), size_t(scrollY_ / lineH_));
            last = std::min(lines_.size(), size_t((scrollY_ + in.height()) / lineH_) + 1);
        }
        for (size_t li = first; li < last; ++li) {
            const Line& L = lines_[li];
            const int top = lineTop(li);
            const bool newlineAfter = L.last && L.end < shown_.size();  // a line that ends in a newline shows it selected
            for (const Run& run : runs(L.start, L.end)) {
                const int x0 = baseX + (px_[run.from] - px_[L.start]);
                const int w = px_[run.to] - px_[run.from];
                if (run.attr.selected) c.fill(draw::Rect{x0, top, x0 + w + (newlineAfter && run.to == L.end ? spaceW : 0), top + lineH_}, th.sel);
                draw::FontDesc d = fontDesc_;
                d.bold = fontDesc_.bold || run.attr.bold;
                d.underline = run.attr.comp;
                c.setFont(c.font(d));
                c.text(draw::Rect{x0, top, x0 + w + lineH_, top + lineH_}, std::wstring_view(shown_).substr(run.from, run.to - run.from),
                       run.attr.color, draw::kLeft | draw::kMiddle);
            }
            c.setFont(c.font(fontDesc_));
        }
        if (hasFocus() && caretOn_) {
            const size_t dc = displayCaret();
            const size_t li = lineOf(dc);
            const int x = baseX + (px_[dc] - px_[lines_[li].start]);
            const int top = lineTop(li);
            c.fill(draw::Rect{x, top, x + std::max(1, px(1)), top + lineH_}, th.text);
        }
    }
    c.popClip();
    updateCaretRect();
    drawBar(c);
    c.setFont(prev);
    if (hasFocus()) syncInput();
}

void TextField::drawBar(draw::Canvas& c) const {
    if (!barShown_) return;
    const Theme& th = theme();
    const draw::Rect in = inner();
    const int content = int(lines_.size()) * lineH_, vh = in.height();
    const int maxY = content - vh;
    const int thumbH = std::min(vh, std::max(px(16), vh * vh / std::max(1, content)));
    const int thumbTop = in.top + (maxY > 0 ? int(static_cast<long long>(scrollY_) * (vh - thumbH) / maxY) : 0);
    const bool lit = hot_ || dragging_;  // as scrollFace: a thin line at rest, the track and a wider thumb when used
    const int tr = width() - px(2), tl = width() - px(8);
    if (lit) c.fill(draw::Rect{tl, in.top, tr, in.bottom}, th.panel);
    const draw::Rect thumb = lit ? draw::Rect{tl + px(1), thumbTop, tr, thumbTop + thumbH}
                                 : draw::Rect{tr - px(3), thumbTop, tr, thumbTop + thumbH};
    const draw::Color color = lit ? th.thumbHot : th.thumb;
    c.roundBox(thumb, color, color, px(2));
}

// --- Moves --------------------------------------------------------------------------------------------------------

void TextField::moveTo(size_t m, bool extend, bool keepGoal) {
    model_.setCaret(m, extend);
    if (!keepGoal) goalX_ = -1;
    afterAction();
}

void TextField::vertical(int dir, bool extend, int count) {
    const size_t n = model_.size();
    if (!fresh()) {  // no layout to go by: the lines of the text, the column in characters
        size_t m = model_.caret();
        const size_t col = m - model_.lineStart(m);
        for (int k = 0; k < count; ++k) {
            if (dir < 0) {
                const size_t ls = model_.lineStart(m);
                if (ls == 0) break;
                const size_t pe = ls - 1, ps = model_.lineStart(pe);
                m = ps + std::min(col, pe - ps);
            } else {
                const size_t le = model_.lineEnd(m);
                if (le >= n) break;
                const size_t ps = le + 1, pe = model_.lineEnd(ps);
                m = ps + std::min(col, pe - ps);
            }
        }
        moveTo(m, extend);
        return;
    }
    const size_t dc = displayCaret();
    size_t li = lineOf(dc);
    if (goalX_ < 0) goalX_ = px_[dc] - px_[lines_[li].start];
    size_t moved = 0;
    for (int k = 0; k < count; ++k) {
        if (dir < 0) {
            if (li == 0) break;
            --li;
        } else {
            if (li + 1 >= lines_.size()) break;
            ++li;
        }
        ++moved;
    }
    // Up on the first line goes to the start of the text, Down on the last to its end.
    const size_t target = moved ? modelOf(posAtX(li, goalX_)) : (dir < 0 ? 0 : n);
    moveTo(target, extend, true);
}

size_t TextField::visualEdge(bool end) const {
    const size_t m = model_.caret();
    if (!multiline()) return end ? model_.size() : 0;
    if (!fresh()) return end ? model_.lineEnd(m) : model_.lineStart(m);
    const size_t li = lineOf(displayCaret());
    const Line& L = lines_[li];
    size_t d = end ? L.end : L.start;
    if (end && lineOf(d) != li) --d;  // the end of a wrapped line: before the break (its space, or the last character)
    return modelOf(d);
}

void TextField::copySelection() {
    if (!model_.hasSelection()) return;
    if (draw::Host* h = host()) h->setClipboardText(toUtf8(model_.selectedText()));
}

void TextField::paste() {
    if (model_.readOnly()) return;
    draw::Host* h = host();
    if (!h) return;
    const std::string s = h->clipboardText();
    edit([&] { return model_.insert(fromUtf8(s), false); });
    goalX_ = -1;
    afterAction();
}

// --- Events -------------------------------------------------------------------------------------------------------

bool TextField::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    const bool inside = draw::Rect{0, 0, width(), height()}.contains(e.pt);
    switch (e.kind) {
    case MouseKind::Move:
        if (inside != hot_) hot_ = inside, invalidate();
        if (!dragging_) return false;
        // Past the top or bottom, the text scrolls while the mouse is held.
        if (multiline()) {
            if (e.pt.y < 0) scrollY_ -= lineH_;
            else if (e.pt.y > height()) scrollY_ += lineH_;
        } else {
            if (e.pt.x < 0) scrollX_ -= px(8);
            else if (e.pt.x > width()) scrollX_ += px(8);
        }
        clampScroll();
        model_.setCaret(caretFromPoint(e.pt), true);
        afterAction();
        return true;
    case MouseKind::Leave:
        if (hot_) hot_ = false, invalidate();
        return false;
    case MouseKind::Down: {
        if (e.button != 0) return false;
        const auto now = std::chrono::steady_clock::now();
        const bool triple = clicks_ == 2 && doublePt_.x == e.pt.x && doublePt_.y == e.pt.y &&
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - doubleAt_).count() < kTripleMs;
        clicks_ = triple ? 3 : 1;
        const size_t m = caretFromPoint(e.pt);
        if (triple) model_.setSelection(model_.lineStart(m), model_.lineEnd(m));
        else model_.setCaret(m, (e.mods & draw::kShift) != 0);
        goalX_ = -1;
        dragging_ = true;
        if (draw::Host* h = host()) h->capture(*this, true);
        afterAction();
        return true;
    }
    case MouseKind::DoubleClick: {
        if (e.button != 0) return false;
        clicks_ = 2;
        doublePt_ = e.pt;
        doubleAt_ = std::chrono::steady_clock::now();
        size_t from = 0, to = 0;
        model_.wordAt(caretFromPoint(e.pt), from, to);
        model_.setSelection(from, to);
        goalX_ = -1;
        afterAction();
        return true;
    }
    case MouseKind::Up:
        if (e.button != 0 || !dragging_) return false;
        dragging_ = false;
        if (draw::Host* h = host()) h->capture(*this, false);
        invalidate();
        return true;
    case MouseKind::Wheel: {
        if (!multiline() || e.horizontal) return false;
        const int old = scrollY_;
        scrollY_ -= int(e.wheel * 3.0f * float(lineH_));  // three lines a notch; positive is away from the user (up)
        clampScroll();
        if (scrollY_ == old) return false;
        invalidate();
        return true;
    }
    default: return false;
    }
}

bool TextField::key(const draw::KeyEvent& e) {
    if (!e.down) return false;
    using draw::Key;
    const bool ctrl = (e.mods & draw::kCtrl) != 0, shift = (e.mods & draw::kShift) != 0;
    const bool ro = model_.readOnly();
    const size_t n = model_.size();
    const size_t m = model_.caret();
    const int page = std::max(1, inner().height() / lineH_);
    if (ctrl) {  // Ctrl+letter: the editing shortcuts (letters are their ASCII capitals)
        switch (int(e.key)) {
        case 'A':
            model_.selectAll();
            goalX_ = -1;
            afterAction();
            return true;
        case 'C':
            copySelection();
            return true;
        case 'X':
            copySelection();
            if (!ro) {
                edit([&] { return model_.deleteSelection(); });
                goalX_ = -1;
                afterAction();
            }
            return true;
        case 'V':
            paste();
            return true;
        case 'Y':
        case 'Z': {
            const bool redo = int(e.key) == 'Y' || shift;
            edit([&] { return redo ? model_.redo() : model_.undo(); });
            goalX_ = -1;
            afterAction();
            return true;
        }
        default: break;
        }
    }
    switch (e.key) {
    case Key::Left:
    case Key::Right: {
        const bool left = e.key == Key::Left;
        size_t t;
        if (model_.hasSelection() && !shift && !ctrl) t = left ? model_.selectionStart() : model_.selectionEnd();
        else if (left) t = ctrl ? model_.wordStartBefore(m) : model_.prevPos(m);
        else t = ctrl ? model_.wordEndAfter(m) : model_.nextPos(m);
        moveTo(t, shift);
        return true;
    }
    case Key::Up:
    case Key::Down:
        if (!multiline()) return false;
        vertical(e.key == Key::Up ? -1 : 1, shift, 1);
        return true;
    case Key::PageUp:
    case Key::PageDown:
        if (!multiline()) return false;
        vertical(e.key == Key::PageUp ? -1 : 1, shift, page);
        return true;
    case Key::Home:
    case Key::End:
        moveTo(ctrl ? (e.key == Key::Home ? 0 : n) : visualEdge(e.key == Key::End), shift);
        return true;
    case Key::Backspace:
        if (!ro) {
            edit([&] { return ctrl ? model_.backspaceWord() : model_.backspace(); });
            goalX_ = -1;
            afterAction();
        }
        return true;
    case Key::Delete:
        if (!ro) {
            edit([&] { return ctrl ? model_.deleteWordForward() : model_.deleteForward(); });
            goalX_ = -1;
            afterAction();
        }
        return true;
    case Key::Enter:
        if (multiline() && !ctrl) {
            if (!ro) {
                edit([&] { return model_.insert(L"\n", false); });
                goalX_ = -1;
                afterAction();
            }
            return true;
        }
        if (onEnter) {
            onEnter();
            return true;
        }
        return false;
    case Key::Escape:
        if (!onEscape) return false;
        onEscape();
        return true;
    default: return false;
    }
}

bool TextField::text(const draw::TextEvent& e) {
    if (!hasFocus()) return false;
    if (composing()) comp_.clear(), compCur_ = 0, ++style_;  // the input method commits what it showed
    const std::wstring w = fromUtf8(e.utf8);
    if (!model_.readOnly()) edit([&] { return model_.insert(w, true); });
    goalX_ = -1;
    afterAction();
    return true;
}

bool TextField::compose(const draw::CompositionEvent& e) {
    if (!hasFocus()) return false;
    std::wstring c = model_.readOnly() ? std::wstring() : fromUtf8(e.utf8);
    const size_t cut = e.cursor > 0 ? std::min(size_t(e.cursor), e.utf8.size()) : 0;
    const size_t cur = std::min(fromUtf8(std::string_view(e.utf8).substr(0, cut)).size(), c.size());
    if (c == comp_ && cur == compCur_) return true;
    comp_ = std::move(c);
    compCur_ = cur;
    ++style_;  // shown, not in the buffer: not in the layout's key by text, not in undo
    afterAction();
    return true;
}

void TextField::focusChanged(bool has) {
    if (has) {
        resetBlink();
        syncInput();
        return;
    }
    stopBlink();
    caretOn_ = true;
    if (composing()) comp_.clear(), compCur_ = 0, ++style_;
    dragging_ = false;
    if (draw::Host* h = host()) h->textInput(*this, false, caretRect_);
    invalidate();
    if (onFocusLost) onFocusLost();
}

// --- Helpers ------------------------------------------------------------------------------------------------------

void TextField::afterAction() {
    resetBlink();
    invalidate();
    syncInput();
}

void TextField::syncInput() {
    updateCaretRect();
    if (draw::Host* h = host(); h && hasFocus()) h->textInput(*this, true, caretRect_);
}

void TextField::armBlink() {
    draw::Host* h = host();
    if (!h) return;
    std::shared_ptr<bool> alive = alive_;
    blinkId_ = h->timer(kBlinkMs, [this, alive] {
        if (!*alive) return;
        blinkId_ = 0;
        caretOn_ = !caretOn_;
        invalidate();
        if (hasFocus()) armBlink();
    });
}

void TextField::stopBlink() {
    if (!blinkId_) return;
    if (draw::Host* h = host()) h->cancelTimer(blinkId_);
    blinkId_ = 0;
}

void TextField::resetBlink() {
    stopBlink();
    caretOn_ = true;
    if (hasFocus()) armBlink();
}

}  // namespace stardraft::kit
