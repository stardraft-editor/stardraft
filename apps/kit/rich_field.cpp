#include "rich_field.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace stardraft::kit {

namespace {

// A chip's character in the text: the private use area, U+E000 + the chip's index in the table.
constexpr uint32_t kChipBase = 0xE000;

bool isChipChar(wchar_t c) { return uint32_t(c) - kChipBase < RichField::kMaxChips; }

// The text without private use characters (typed or pasted text cannot make a chip).
std::wstring withoutPrivate(std::wstring_view w) {
    std::wstring o;
    o.reserve(w.size());
    for (wchar_t c : w)
        if (!isChipChar(c)) o += c;
    return o;
}

// The index after the character at `e` (a surrogate pair is one character), not past `end`.
size_t nextIndex(const std::wstring& s, size_t e, size_t end) {
    size_t ne = e + 1;
    if (ne < end && isHighSurrogate(s[e]) && isLowSurrogate(s[ne])) ++ne;
    return ne;
}

// The private clipboard form: what the last Copy took, with its chips. A paste of the same text puts the chips back.
struct RichClipboard {
    bool valid = false;
    std::string plain;
    std::vector<RichPart> parts;
};
RichClipboard& richClipboard() {
    static RichClipboard c;
    return c;
}

}  // namespace

RichField::RichField() = default;

RichField::~RichField() {
    if (focused_)
        if (draw::Host* h = host()) h->textInput(*this, false, caretRect_);
}

// --- Content ------------------------------------------------------------------------------------------------------

std::vector<RichPart> RichField::partsOf(size_t from, size_t to) const {
    std::vector<RichPart> out;
    const std::wstring& t = model_.text();
    std::wstring run;
    for (size_t i = from; i < to && i < t.size(); ++i) {
        if (isChipChar(t[i])) {
            if (!run.empty()) out.push_back(RichPart::plain(std::exchange(run, std::wstring())));
            const size_t k = size_t(uint32_t(t[i]) - kChipBase);
            out.push_back(RichPart::of(k < chips_.size() ? chips_[k] : RichChip{}));
        } else {
            run += t[i];
        }
    }
    if (!run.empty()) out.push_back(RichPart::plain(std::move(run)));
    return out;
}

std::vector<RichPart> RichField::parts() const { return partsOf(0, model_.text().size()); }

std::wstring RichField::plainText() const {
    std::wstring o;
    for (const RichPart& p : parts()) o += p.chip ? p.value.label : p.text;
    return o;
}

void RichField::setParts(std::vector<RichPart> parts) {
    chips_.clear();
    std::wstring s;
    for (const RichPart& p : parts) {
        if (p.chip && chips_.size() < kMaxChips) {
            s += wchar_t(kChipBase + chips_.size());
            chips_.push_back(p.value);
        } else {
            s += withoutPrivate(p.chip ? std::wstring_view(p.value.label) : std::wstring_view(p.text));
        }
    }
    model_.setText(std::move(s));
    comp_.clear();
    compCur_ = 0;
    atBreak_ = false;
    ++style_;
    invalidate();
}

size_t RichField::chipCount() const {
    size_t n = 0;
    for (wchar_t c : model_.text())
        if (isChipChar(c)) ++n;
    return n;
}

RichChip RichField::chip(size_t index) const {
    for (wchar_t c : model_.text()) {
        if (!isChipChar(c)) continue;
        if (index == 0) {
            const size_t k = size_t(uint32_t(c) - kChipBase);
            return k < chips_.size() ? chips_[k] : RichChip{};
        }
        --index;
    }
    return RichChip{};
}

size_t RichField::addChip(const RichChip& c) {
    if (chips_.size() >= kMaxChips) return kNone;
    chips_.push_back(c);
    return chips_.size() - 1;
}

bool RichField::insertChip(const RichChip& c) {
    if (readOnly()) return false;
    const size_t k = addChip(c);
    if (k == kNone) return false;
    const size_t before = k;  // the table had k entries before this one
    const bool ok = edit([&] { return model_.insert(std::wstring(1, wchar_t(kChipBase + k)), false); });
    if (!ok) chips_.resize(before);  // nothing refers to the entry
    return ok;
}

bool RichField::replaceChip(size_t index, const RichChip& c) {
    if (readOnly()) return false;
    const std::wstring& t = model_.text();
    size_t pos = kNone, seen = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        if (!isChipChar(t[i])) continue;
        if (seen++ == index) {
            pos = i;
            break;
        }
    }
    if (pos == kNone) return false;
    const size_t k = addChip(c);
    if (k == kNone) return false;
    const size_t sa = model_.anchor(), sc = model_.caret();
    return edit([&] {
        model_.setSelection(pos, pos + 1);
        const bool r = model_.insert(std::wstring(1, wchar_t(kChipBase + k)), false);
        model_.setSelection(sa, sc);
        return r;
    });
}

// --- Settings -----------------------------------------------------------------------------------------------------

void RichField::setMultiline(bool on) {
    model_.setMultiline(on);
    ++style_;
    invalidate();
}

void RichField::setWrap(bool on) {
    wrap_ = on;
    ++style_;
    invalidate();
}

void RichField::setReadOnly(bool on) {
    model_.setReadOnly(on);
    invalidate();
}

void RichField::setPlaceholder(std::wstring t) {
    placeholder_ = std::move(t);
    invalidate();
}

void RichField::setSpans(std::vector<Span> spans) {
    std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
    spans_ = std::move(spans);
    ++style_;
    invalidate();
}

void RichField::setLineAligns(std::vector<uint8_t> aligns) {
    if (aligns == aligns_) return;
    aligns_ = std::move(aligns);
    ++style_;
    invalidate();
}

void RichField::setPlain(bool on) {
    plain_ = on;
    invalidate();
}

size_t RichField::caretParagraph() const {
    const std::wstring& t = model_.text();
    return size_t(std::count(t.begin(), t.begin() + std::ptrdiff_t(std::min(model_.caret(), t.size())), L'\n'));
}

void RichField::setFont(draw::FontDesc f) {
    fontDesc_ = f;
    ++style_;
    invalidate();
}

void RichField::selectAll() {
    model_.selectAll();
    goalX_ = -1;
    afterAction();
}

const Span* RichField::spanAt(size_t m) const {
    auto it = std::upper_bound(spans_.begin(), spans_.end(), m, [](size_t v, const Span& s) { return v < s.start; });
    if (it == spans_.begin()) return nullptr;
    --it;
    return m < it->start + it->length ? &*it : nullptr;
}

RichField::Attr RichField::attrOf(const Unit& u) const {
    Attr a;
    a.color = theme().text;
    a.bold = u.bold;
    if (u.kind == kComp) {
        a.comp = true;
        return a;
    }
    const size_t lo = model_.selectionStart(), hi = model_.selectionEnd();
    if (!composing() && lo < hi && u.from >= lo && u.from < hi) a.selected = true;
    if (u.kind == kChar) {
        if (const Span* s = spanAt(u.from)) {
            if (s->color != draw::kNone) a.color = s->color;
            a.strike = s->strike, a.dotted = s->dotted;
        }
    }
    if (a.selected) a.color = theme().onText;
    return a;
}

// --- Layout -------------------------------------------------------------------------------------------------------

void RichField::relayout(draw::Canvas& c) {
    const draw::Font prev = c.setFont(c.font(fontDesc_));
    lineH_ = std::max(1, c.metrics().height);
    const std::wstring& t = model_.text();
    const size_t n = t.size();
    const bool comp = composing();
    const size_t lo = model_.selectionStart(), hi = model_.selectionEnd();

    bool cur = fontDesc_.bold;  // the weight the canvas has now
    auto setBold = [&](bool b) {
        if (b == cur) return;
        draw::FontDesc f = fontDesc_;
        f.bold = b;
        c.setFont(c.font(f));
        cur = b;
    };
    // A character's width is measured once per layout (per character and weight).
    std::unordered_map<uint64_t, int> widths;

    units_.clear();
    int x = 0;
    bool compDone = false;
    auto pushComp = [&] {
        compDone = true;
        compStart_ = units_.size();
        setBold(false);
        for (size_t j = 0; j < comp_.size(); ++j) {
            Unit u;
            u.from = u.to = lo;
            u.kind = kComp;
            u.aux = j;
            u.x = x;
            u.w = c.measure(std::wstring_view(comp_).substr(j, 1));
            x += u.w;
            units_.push_back(u);
        }
    };
    for (size_t d = 0; d < n;) {
        if (comp && !compDone && d >= lo) pushComp();
        if (comp && d == lo && hi > lo) {  // the selection is replaced by the composition: not shown
            d = hi;
            continue;
        }
        Unit u;
        u.from = d;
        const wchar_t ch = t[d];
        if (isChipChar(ch)) {
            u.kind = kChip;
            u.to = d + 1;
            u.aux = size_t(uint32_t(ch) - kChipBase);
            setBold(false);
            const std::wstring label = u.aux < chips_.size() ? chips_[u.aux].label : std::wstring();
            u.w = c.measure(label) + px(14) + px(2);  // the label, a gap of 6 px each side in the chip, a hairline each side
        } else if (ch == L'\n') {
            u.kind = kNewline;
            u.to = d + 1;
            u.w = 0;
        } else {
            const size_t len = nextIndex(t, d, n) - d;
            u.to = d + len;
            const Span* s = spanAt(d);
            u.bold = fontDesc_.bold || (s && s->bold);
            setBold(u.bold);
            const uint64_t key = (uint64_t(ch) << 32) | (uint64_t(len == 2 ? t[d + 1] : 0) << 1) | (u.bold ? 1u : 0u);
            const auto it = widths.find(key);
            if (it != widths.end()) {
                u.w = it->second;
            } else {
                u.w = c.measure(std::wstring_view(t).substr(d, len));
                widths.emplace(key, u.w);
            }
        }
        u.x = x;
        x += u.w;
        units_.push_back(u);
        d = u.to;
    }
    if (comp && !compDone) pushComp();
    totalW_ = x;
    c.setFont(prev);

    buildLines(std::max(1, width() - 2 * padX()));
    contentW_ = 0;
    for (const Line& L : lines_) contentW_ = std::max(contentW_, L.width);
    layoutKey_ = LayoutKey{model_.revision(), style_, width()};
    laidOut_ = true;
}

void RichField::buildLines(int vw) {
    lines_.clear();
    const size_t N = units_.size();
    const bool wraps = multiline() && wrap_;
    const std::wstring& t = model_.text();
    size_t s = 0;
    bool lastHard = false;
    do {
        const int x0 = s < N ? units_[s].x : totalW_;
        size_t e = s, space = kNone;
        bool hard = false;
        while (e < N) {
            const Unit& u = units_[e];
            if (u.kind == kNewline) {
                ++e;
                hard = true;
                break;
            }
            if (wraps && e > s && u.x + u.w - x0 > vw) {  // does not fit: break after the last space, else before it
                if (space != kNone) e = space + 1;
                break;
            }
            if (u.kind == kChar && t[u.from] == L' ') space = e;
            ++e;
        }
        const int xEnd = e < N ? units_[e].x : totalW_;
        lines_.push_back(Line{s, e, hard, x0, xEnd - x0});
        s = e;
        lastHard = hard;
    } while (s < N);
    if (lastHard) lines_.push_back(Line{N, N, false, totalW_, 0});  // the caret after a last newline
    // The paragraphs' alignment (lines that wrap): a line's spaces at its end do not count.
    if (!(wraps && !aligns_.empty())) return;
    size_t para = 0;
    for (Line& ln : lines_) {
        const uint8_t a = para < aligns_.size() ? aligns_[para] : 0;
        if (a == 1 || a == 2) {
            size_t e = ln.last;
            if (e > ln.first && units_[e - 1].kind == kNewline) --e;
            while (e > ln.first && units_[e - 1].kind == kChar && t[units_[e - 1].from] == L' ') --e;
            const int w = e > ln.first ? units_[e - 1].x + units_[e - 1].w - ln.x0 : 0;
            ln.off = std::max(0, a == 1 ? (vw - w) / 2 : vw - w);
        }
        if (ln.hard) ++para;
    }
}

size_t RichField::lineOf(size_t u) const {
    if (lines_.empty()) return 0;
    size_t lo = 0, hi = lines_.size();
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (lines_[mid].first <= u) lo = mid;
        else hi = mid;
    }
    return lo;
}

size_t RichField::lineAtY(int y) const {
    if (!multiline() || lines_.empty()) return 0;
    const int rel = y - inner().top + scrollY_;
    return std::min(lines_.size() - 1, size_t(std::max(0, rel) / lineH_));
}

size_t RichField::posInLine(size_t L, int absX) const {
    const Line& ln = lines_[L];
    for (size_t i = ln.first; i < ln.last; ++i) {
        const Unit& u = units_[i];
        if (u.kind == kNewline) return i;
        if (absX < u.x + u.w / 2) return i;
    }
    return ln.last;
}

size_t RichField::modelAt(size_t u) const { return u < units_.size() ? units_[u].from : model_.size(); }

size_t RichField::caretIndex() const {
    if (composing()) return std::min(compStart_ + compCur_, units_.size());
    const size_t m = model_.caret();
    size_t i = 0;
    while (i < units_.size() && units_[i].from < m) ++i;
    return i;
}

size_t RichField::caretLine() const {
    if (lines_.empty()) return 0;
    const size_t u = caretIndex();
    size_t L = lineOf(u);
    if (atBreak_ && L > 0 && !lines_[L - 1].hard && lines_[L - 1].last == u) --L;
    return L;
}

int RichField::caretX() const {
    const size_t u = caretIndex();
    return u < units_.size() ? units_[u].x : totalW_;
}

size_t RichField::hitCaret(draw::Point pt, bool& brk) const {
    brk = false;
    if (lines_.empty()) return model_.caret();
    const size_t L = lineAtY(pt.y);
    const Line& ln = lines_[L];
    const int absX = ln.x0 + (pt.x - inner().left - ln.off + scrollX_);
    const size_t u = posInLine(L, absX);
    brk = !ln.hard && L + 1 < lines_.size() && u == ln.last;
    return modelAt(u);
}

size_t RichField::chipAt(draw::Point pt) const {
    if (lines_.empty()) return kNone;
    const Line& ln = lines_[lineAtY(pt.y)];
    const int absX = ln.x0 + (pt.x - inner().left - ln.off + scrollX_);
    for (size_t i = ln.first; i < ln.last; ++i) {
        const Unit& u = units_[i];
        if (u.kind == kChip && absX >= u.x && absX < u.x + u.w) return i;
    }
    return kNone;
}

size_t RichField::ordinalOf(size_t unit) const {
    size_t n = 0;
    for (size_t i = 0; i < unit && i < units_.size(); ++i)
        if (units_[i].kind == kChip) ++n;
    return n;
}

draw::Rect RichField::chipRect(size_t unit) const {
    if (lines_.empty() || unit >= units_.size()) return {};
    const size_t L = lineOf(unit);
    const Unit& u = units_[unit];
    const int x = inner().left - scrollX_ + lines_[L].off + (u.x - lines_[L].x0);
    const int top = lineTop(L);
    return draw::Rect{x + px(1), top + px(2), x + u.w - px(1), top + lineH_ - px(2)};
}

draw::Rect RichField::inner() const { return draw::Rect{padX(), padY(), width() - padX(), height() - padY()}; }

int RichField::lineTop(size_t L) const {
    const draw::Rect in = inner();
    if (!multiline()) return in.top + (in.height() - lineH_) / 2;
    return in.top + int(L) * lineH_ - scrollY_;
}

void RichField::clampScroll() {
    const draw::Rect in = inner();
    const int content = int(lines_.size()) * lineH_;
    const int maxY = multiline() ? std::max(0, content - in.height()) : 0;
    scrollY_ = std::clamp(scrollY_, 0, maxY);
    const int maxX = (multiline() && wrap_) ? 0 : std::max(0, contentW_ + 2 - in.width());  // room for the caret at the end
    scrollX_ = std::clamp(scrollX_, 0, maxX);
}

void RichField::ensureCaretVisible() {
    if (lines_.empty()) return;
    if (model_.revision() == ensuredRev_ && model_.caret() == ensuredCaret_ && model_.anchor() == ensuredAnchor_ && style_ == ensuredStyle_)
        return;  // only a move of the caret scrolls to it; the wheel may take it away
    ensuredRev_ = model_.revision();
    ensuredCaret_ = model_.caret();
    ensuredAnchor_ = model_.anchor();
    ensuredStyle_ = style_;
    const draw::Rect in = inner();
    const size_t L = caretLine();
    const int cx = caretX() - lines_[L].x0;
    if (!(multiline() && wrap_)) {
        if (cx < scrollX_) scrollX_ = cx;
        else if (cx + 2 > scrollX_ + in.width()) scrollX_ = cx + 2 - in.width();
    }
    if (multiline()) {
        const int top = int(L) * lineH_;
        if (top < scrollY_) scrollY_ = top;
        else if (top + lineH_ > scrollY_ + in.height()) scrollY_ = top + lineH_ - in.height();
    }
    clampScroll();
}

void RichField::updateCaretRect() {
    if (!fresh() || lines_.empty()) return;
    const draw::Rect in = inner();
    const size_t L = caretLine();
    const int x = in.left - scrollX_ + lines_[L].off + (caretX() - lines_[L].x0);
    const int top = lineTop(L);
    caretRect_ = draw::Rect{x, top, x + 1, top + lineH_};
}

// --- Painting -----------------------------------------------------------------------------------------------------

void RichField::paint(draw::Canvas& c) {
    const Theme& th = theme();
    // The field box, as TextField's: Hot, ReadOnly, then Focus over them.
    draw::Color fill = th.ctl, line = th.border;
    if (hot_) line = th.borderHot;
    if (readOnly()) fill = th.readOnly, line = th.readOnlyLine;
    if (focused_ && !readOnly()) line = th.accent;
    if (!plain_) c.roundBox(draw::Rect{0, 0, width(), height()}, fill, line, float(px(kRadius)));

    const draw::Font prev = c.setFont(c.font(fontDesc_));
    if (!fresh()) relayout(c);
    clampScroll();
    ensureCaretVisible();

    const draw::Rect in = inner();
    c.pushClip(in);
    if (model_.text().empty() && !composing()) {
        if (!placeholder_.empty()) {
            unsigned flags = draw::kLeft;  // several lines: at the top; one line: in the middle
            if (!multiline()) flags |= draw::kMiddle;
            c.text(in, placeholder_, th.faint, flags);
        }
    } else {
        size_t first = 0, last = lines_.size();
        if (multiline()) {
            first = std::min(lines_.size(), size_t(std::max(0, scrollY_) / lineH_));
            last = std::min(lines_.size(), size_t((scrollY_ + in.height()) / lineH_) + 1);
        }
        for (size_t L = first; L < last; ++L) paintLine(c, L, in);
    }
    if (focused_ && !lines_.empty()) {
        const size_t L = caretLine();
        const int x = in.left - scrollX_ + lines_[L].off + (caretX() - lines_[L].x0);
        const int top = lineTop(L);
        c.fill(draw::Rect{x, top, x + std::max(1, px(1)), top + lineH_}, th.text);
    }
    c.popClip();
    updateCaretRect();
    c.setFont(prev);
    if (focused_) syncInput();
}

void RichField::paintLine(draw::Canvas& c, size_t L, const draw::Rect& in) {
    const Theme& th = theme();
    const Line& ln = lines_[L];
    const int top = lineTop(L);
    const int baseX = in.left - scrollX_ + ln.off - ln.x0;  // a unit's x is baseX + its x
    const int spaceW = c.measure(L" ");
    size_t i = ln.first;
    while (i < ln.last) {
        const Unit& u = units_[i];
        if (u.kind == kNewline) {
            if (attrOf(u).selected) c.fill(draw::Rect{baseX + u.x, top, baseX + u.x + spaceW, top + lineH_}, th.sel);
            ++i;
            continue;
        }
        if (u.kind == kChip) {
            drawChip(c, i);
            ++i;
            continue;
        }
        // A run of characters with one look (and, in a composition, the composition's own run).
        const Attr a = attrOf(u);
        auto sameLook = [](const Attr& p, const Attr& q) {
            return p.color == q.color && p.bold == q.bold && p.selected == q.selected && p.comp == q.comp && p.strike == q.strike &&
                   p.dotted == q.dotted;
        };
        size_t j = i + 1;
        while (j < ln.last) {
            const Unit& v = units_[j];
            if (v.kind != u.kind) break;
            if (u.kind == kChar && (v.from != units_[j - 1].to || !sameLook(attrOf(v), a))) break;
            ++j;
        }
        const int x0 = baseX + u.x;
        const int x1 = baseX + units_[j - 1].x + units_[j - 1].w;
        if (a.selected) c.fill(draw::Rect{x0, top, x1, top + lineH_}, th.sel);
        draw::FontDesc d = fontDesc_;
        d.bold = u.bold;
        d.underline = a.comp;
        c.setFont(c.font(d));
        const std::wstring_view s = u.kind == kComp ? std::wstring_view(comp_).substr(u.aux, j - i)
                                                    : std::wstring_view(model_.text()).substr(u.from, units_[j - 1].to - u.from);
        // Each character at its measured advance (the caret and the mouse use those widths): textAt moves no pairs.
        const draw::FontMetrics m = c.metrics();
        const int ty = top + (lineH_ - m.height) / 2;
        c.textAt({x0, ty}, s, a.color);
        const int base = ty + m.height - m.descent;  // the baseline
        if (a.strike) c.fill(draw::Rect{x0, base - (m.height - m.descent) * 3 / 10 - 1, x1, base - (m.height - m.descent) * 3 / 10}, a.color);
        if (a.dotted)
            for (int x = x0; x < x1; x += 2) c.fill(draw::Rect{x, base + 1, x + 1, base + 2}, a.color);
        c.setFont(c.font(fontDesc_));
        i = j;
    }
}

void RichField::drawChip(draw::Canvas& c, size_t i) {
    const Theme& th = theme();
    const Unit& u = units_[i];
    const draw::Rect r = chipRect(i);
    const bool sel = attrOf(u).selected;
    const bool over = i == hotChip_;
    const draw::Color fill = sel ? th.sel : over ? th.selHot : th.chip;
    const draw::Color line = sel || over ? th.accentLine : th.chipLine;
    c.roundBox(r, fill, line, float(px(kRadius)), 1);
    const std::wstring label = u.aux < chips_.size() ? chips_[u.aux].label : std::wstring();
    c.text(draw::Rect{r.left + px(6), r.top, r.right - px(6), r.bottom}, label, sel ? th.onText : th.text, draw::kLeft | draw::kMiddle);
}

// --- Moves --------------------------------------------------------------------------------------------------------

void RichField::moveTo(size_t m, bool extend) {
    model_.setCaret(m, extend);
    goalX_ = -1;
    afterAction();
}

void RichField::setCaretAt(draw::Point pt, bool extend) {
    bool brk = false;
    const size_t m = hitCaret(pt, brk);
    atBreak_ = brk;
    model_.setCaret(m, extend);
    goalX_ = -1;
    afterAction();
}

void RichField::vertical(int dir, bool extend) {
    if (lines_.empty()) return;
    const size_t L = caretLine();
    if (goalX_ < 0) goalX_ = caretX() - lines_[L].x0 + lines_[L].off;
    const bool have = dir < 0 ? L > 0 : L + 1 < lines_.size();
    if (!have) {  // Up on the first line goes to the start of the text, Down on the last to its end
        model_.setCaret(dir < 0 ? 0 : model_.size(), extend);
        afterAction();
        return;
    }
    const size_t LL = dir < 0 ? L - 1 : L + 1;
    const Line& ln = lines_[LL];
    const size_t u = posInLine(LL, ln.x0 + goalX_ - ln.off);
    atBreak_ = !ln.hard && LL + 1 < lines_.size() && u == ln.last;
    model_.setCaret(modelAt(u), extend);
    afterAction();  // keeps the column (goalX_)
}

// --- Clipboard ----------------------------------------------------------------------------------------------------

void RichField::copySelection() {
    if (!model_.hasSelection()) return;
    std::vector<RichPart> ps = partsOf(model_.selectionStart(), model_.selectionEnd());
    std::wstring plain;
    for (const RichPart& p : ps) plain += p.chip ? p.value.label : p.text;
    richClipboard() = RichClipboard{true, toUtf8(plain), std::move(ps)};
    if (draw::Host* h = host()) h->setClipboardText(richClipboard().plain);
}

bool RichField::insertParts(const std::vector<RichPart>& parts) {
    const size_t before = chips_.size();
    std::wstring s;
    for (const RichPart& p : parts) {
        if (!p.chip) {
            s += withoutPrivate(p.text);
            continue;
        }
        const size_t k = addChip(p.value);
        if (k == kNone) s += withoutPrivate(p.value.label);  // the table is full: the label as text
        else s += wchar_t(kChipBase + k);
    }
    const bool ok = edit([&] { return model_.insert(s, false); });
    if (!ok) chips_.resize(before);  // the entries nothing refers to
    return ok;
}

void RichField::paste() {
    if (readOnly()) return;
    draw::Host* h = host();
    if (!h) return;
    const std::string s = h->clipboardText();
    const RichClipboard& k = richClipboard();
    if (k.valid && s == k.plain) {  // the same text as the last Copy: its chips come back
        insertParts(k.parts);
        return;
    }
    const std::wstring w = withoutPrivate(fromUtf8(s));
    edit([&] { return model_.insert(w, false); });
}

// --- Events -------------------------------------------------------------------------------------------------------

bool RichField::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    const bool inside = draw::Rect{0, 0, width(), height()}.contains(e.pt);
    switch (e.kind) {
    case MouseKind::Move: {
        if (inside != hot_) hot_ = inside, invalidate();
        const size_t hc = inside ? chipAt(e.pt) : kNone;
        if (hc != hotChip_) hotChip_ = hc, invalidate();
        if (!dragging_) return false;
        // Past the top or bottom, the text scrolls while the mouse is held.
        if (multiline()) {
            if (e.pt.y < 0) scrollY_ -= lineH_;
            else if (e.pt.y > height()) scrollY_ += lineH_;
        }
        if (!(multiline() && wrap_)) {
            if (e.pt.x < 0) scrollX_ -= px(8);
            else if (e.pt.x > width()) scrollX_ += px(8);
        }
        clampScroll();
        setCaretAt(e.pt, true);
        return true;
    }
    case MouseKind::Leave:
        if (hot_) hot_ = false, invalidate();
        if (hotChip_ != kNone) hotChip_ = kNone, invalidate();
        return false;
    case MouseKind::Down: {
        if (e.button != 0) return false;
        atBreak_ = false;
        const size_t ci = chipAt(e.pt);
        if (ci != kNone) {  // a chip: the caret goes after it, and the owner hears the click
            model_.setCaret(units_[ci].to, false);
            goalX_ = -1;
            dragging_ = false;
            afterAction();
            if (onChipClick) onChipClick(ordinalOf(ci), chipRect(ci));  // last: the callback may destroy the field
            return true;
        }
        dragging_ = true;
        if (draw::Host* h = host()) h->capture(*this, true);
        setCaretAt(e.pt, (e.mods & draw::kShift) != 0);
        return true;
    }
    case MouseKind::DoubleClick: {
        if (e.button != 0) return false;
        if (chipAt(e.pt) != kNone) return true;
        atBreak_ = false;
        bool brk = false;
        const size_t m = hitCaret(e.pt, brk);
        size_t from = 0, to = 0;
        model_.wordAt(m, from, to);
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
        if (e.horizontal) {  // sideways (positive is right): a line that does not wrap
            if (!multiline() || wrap_) return false;
            const int oldX = scrollX_;
            scrollX_ += int(e.wheel * 3.0f * float(px(24)));
            clampScroll();
            if (scrollX_ == oldX) return false;
            invalidate();
            return true;
        }
        if (!multiline()) return false;
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

bool RichField::key(const draw::KeyEvent& e) {
    if (!e.down) return false;
    using draw::Key;
    const bool ctrl = (e.mods & draw::kCtrl) != 0, shift = (e.mods & draw::kShift) != 0;
    const bool ro = readOnly();
    const size_t n = model_.size();
    const size_t m = model_.caret();
    atBreak_ = false;
    // Ctrl+letter: the editing shortcuts (letters are their ASCII capitals). Not with Alt (AltGr is Ctrl+Alt).
    if (ctrl && !(e.mods & draw::kAlt)) {
        switch (int(e.key)) {
        case 'A':
            selectAll();
            return true;
        case 'C':
            copySelection();
            return true;
        case 'X':
            copySelection();
            if (!ro) edit([&] { return model_.deleteSelection(); });
            return true;
        case 'V':
            paste();
            return true;
        case 'Y':
        case 'Z': {
            const bool redo = int(e.key) == 'Y' || shift;
            edit([&] { return redo ? model_.redo() : model_.undo(); });
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
        vertical(e.key == Key::Up ? -1 : 1, shift);
        return true;
    case Key::Home:
    case Key::End: {
        const bool end = e.key == Key::End;
        if (ctrl) {
            model_.setCaret(end ? n : 0, shift);
            goalX_ = -1;
            afterAction();
            return true;
        }
        if (lines_.empty()) return true;
        const size_t L = caretLine();
        const Line& ln = lines_[L];
        size_t target;
        if (!end) {
            target = modelAt(ln.first);
        } else if (ln.hard) {
            target = modelAt(ln.last - 1);  // before the newline
        } else {
            atBreak_ = L + 1 < lines_.size();  // the end of a wrapped line: shown on this line
            target = modelAt(ln.last);
        }
        model_.setCaret(target, shift);
        goalX_ = -1;
        afterAction();
        return true;
    }
    case Key::Backspace:
        if (!ro) edit([&] { return ctrl ? model_.backspaceWord() : model_.backspace(); });
        return true;
    case Key::Delete:
        if (!ro) edit([&] { return ctrl ? model_.deleteWordForward() : model_.deleteForward(); });
        return true;
    case Key::Enter:
        if (multiline() && !ctrl) {
            if (!ro) edit([&] { return model_.insert(L"\n", false); });
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

bool RichField::text(const draw::TextEvent& e) {
    if (!focused_) return false;
    if (composing()) comp_.clear(), compCur_ = 0, ++style_;  // the input method commits what it showed
    const std::wstring w = withoutPrivate(fromUtf8(e.utf8));
    if (!readOnly()) edit([&] { return model_.insert(w, true); });
    else {
        goalX_ = -1;
        afterAction();
    }
    return true;
}

bool RichField::compose(const draw::CompositionEvent& e) {
    if (!focused_) return false;
    std::wstring c = readOnly() ? std::wstring() : withoutPrivate(fromUtf8(e.utf8));
    const size_t cut = e.cursor > 0 ? std::min(size_t(e.cursor), e.utf8.size()) : 0;
    const size_t cur = std::min(fromUtf8(std::string_view(e.utf8).substr(0, cut)).size(), c.size());
    if (c == comp_ && cur == compCur_) return true;
    comp_ = std::move(c);
    compCur_ = cur;
    ++style_;  // shown, not in the text: not in undo
    afterAction();
    return true;
}

void RichField::focusChanged(bool has) {
    focused_ = has;
    if (has) {
        dragging_ = false;
        syncInput();
        return;
    }
    dragging_ = false;
    if (composing()) comp_.clear(), compCur_ = 0, ++style_;
    if (draw::Host* h = host()) h->textInput(*this, false, caretRect_);
    invalidate();
    if (onFocusLost) onFocusLost();
}

void RichField::captureLost() {
    dragging_ = false;  // the selection stays; the mouse moves no longer extend it
    invalidate();
}

// --- Helpers ------------------------------------------------------------------------------------------------------

void RichField::afterAction() {
    invalidate();
    syncInput();
}

void RichField::syncInput() {
    updateCaretRect();
    if (draw::Host* h = host(); h && focused_) h->textInput(*this, true, caretRect_);
}

}  // namespace stardraft::kit
