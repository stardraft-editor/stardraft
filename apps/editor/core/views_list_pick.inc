// The searchable list picker (units, locations, switches, variables, long lists; portable, core.inc): a search row (the
// icon, a kit text field with no box, a line under it) and the list of the items that match each typed word, with the
// section headings small and dim. The row under the keys or the mouse is a rounded pill; the current value has a check
// mark at the right; the typed words are in the accent. A click or Enter picks a row; Tab picks it and asks for the next
// value (Shift+Tab: the one before); Esc and a click outside close the picker with nothing picked.
// The multi-pick: a click, Enter or Space checks a row and the list stays open; OK (its text is the caller's) keeps the
// checks, Cancel, Esc and a click outside leave them as they were. Enter in the search is OK.
// The picker runs in a pop-out window of the host (shellRunPopout); the Windows program's showListPicker and
// showListPickerMulti (main.cpp) and the SDL program run it with runListPick.

// A row's text with the typed words in the accent (as the quick search shows them): each piece of the text is drawn at
// its place, the matched ones in the accent. Draws in `r`, vertically centered.
void drawMatched(stardraft::draw::Canvas& c, stardraft::draw::Rect r, const std::wstring& label, const std::vector<std::wstring>& words, stardraft::draw::Color normal) {
    const std::wstring text = tr(label);  // (in the language in use: the letters are drawn one by one, so the text call cannot translate them)
    std::wstring low = text;
    std::transform(low.begin(), low.end(), low.begin(), ::towlower);
    std::vector<uint8_t> hit(text.size(), 0);
    for (const auto& w : words)
        if (const size_t at = w.empty() ? std::wstring::npos : low.find(w); at != std::wstring::npos)
            std::fill(hit.begin() + long(at), hit.begin() + long(at + w.size()), uint8_t(1));
    const int y = (r.top + r.bottom - c.metrics().height) / 2;  // (the GDI version's rounding: a row partly above the top)
    // Too wide: as much as fits, then an ellipsis (in the color of the last letter kept).
    std::wstring shown = text;
    if (c.measure(text) > r.width()) {
        const int room = std::max(0, r.width() - c.measure(L"\x2026"));
        size_t lo = 0, hi = text.size();  // the longest prefix that fits in `room` (widths grow with the prefix)
        while (lo < hi) {
            const size_t mid = (lo + hi + 1) / 2;
            if (c.measure(std::wstring_view(text).substr(0, mid)) <= room) lo = mid;
            else hi = mid - 1;
        }
        shown = text.substr(0, lo) + L"\x2026";
        hit.resize(shown.size());
        hit.back() = lo > 0 ? hit[lo - 1] : 0;
    }
    // Letter by letter, each at the width of the text before it: on Windows no kerning between letters, as the GDI
    // version's ExtTextOut drew them (a canvas's text kerns "Te" 1 px closer; the width does not).
    c.pushClip(r);
    const int lineH = c.metrics().height;
    for (size_t i = 0; i < shown.size();) {
        const size_t n = (shown[i] >= 0xD800 && shown[i] < 0xDC00 && i + 1 < shown.size()) ? 2 : 1;  // (a surrogate pair is one letter)
        const int x = r.left + c.measure(std::wstring_view(shown).substr(0, i));
        c.text({x, y, r.right, y + lineH}, std::wstring_view(shown).substr(i, n), hit[i] ? kAccent : normal, stardraft::draw::kLeft);
        i += n;
    }
    c.popClip();
}

// The words of a search, in lower case (a word: what is between spaces).
inline std::vector<std::wstring> listPickWords(const std::wstring& typed) {
    std::wstring low = typed;
    std::transform(low.begin(), low.end(), low.begin(), ::towlower);
    std::vector<std::wstring> words;
    std::wistringstream in(low);
    for (std::wstring w; in >> w;) words.push_back(w);
    return words;
}

// The rows a search shows: each item whose label and section have every word, a heading before each new section.
struct ListPickRows {
    std::vector<int> shown;  // indices into the items; -1 - n: the section heading n
    std::vector<std::wstring> headings;
    int sel = -1;  // the row selected first: the current value, else (with words) the first match, else the first item
};
inline ListPickRows listPickRows(const std::vector<ListPickItem>& items, const std::string& current, const std::vector<std::wstring>& words) {
    ListPickRows out;
    std::wstring section = L"\x1";
    for (size_t i = 0; i < items.size(); ++i) {
        std::wstring hay = items[i].label + L" " + items[i].section;
        std::transform(hay.begin(), hay.end(), hay.begin(), ::towlower);
        bool ok = true;
        for (const auto& w : words) ok &= hay.find(w) != std::wstring::npos;
        if (!ok) continue;
        if (items[i].section != section) {
            section = items[i].section;
            if (!section.empty()) out.headings.push_back(section), out.shown.push_back(-int(out.headings.size()));
        }
        if (items[i].value == current || (out.sel < 0 && !words.empty())) out.sel = int(out.shown.size());
        out.shown.push_back(int(i));
    }
    if (out.sel < 0)
        for (size_t i = 0; i < out.shown.size(); ++i)
            if (out.shown[i] >= 0) {
                out.sel = int(i);
                break;
            }
    return out;
}

// The mouse wheel over a dropdown: the item next to the current one (step 1) or the one before (-1), with no list. False
// when there is none (the end of the list, or no items).
inline bool listPickWheelStep(const std::vector<ListPickItem>& items, const std::string& current, int step, std::string& out) {
    if (items.empty()) return false;
    int k = -1;
    for (int i = 0; i < int(items.size()); ++i)
        if (items[size_t(i)].value == current) k = i;
    const int next = k < 0 ? 0 : std::clamp(k + step, 0, int(items.size()) - 1);
    if (next == k) return false;
    out = items[size_t(next)].value;
    return true;
}

// The layout is the native picker's, in pixels at 100 % (scaled), from the window's outer top left corner (the window
// has a 1 px border: the view's coordinates are the client area's, 1 px in, as the native child windows' were).
namespace listPickLayout {
constexpr int kW = 380, kPad = 8, kSearchH = 30;  // the window's width; the margin; the search row's height
constexpr int kRowH = 28, kIconRowH = 34;          // a row's height (with icons)
constexpr int kMaxRows = 14, kFooterH = 44;        // the rows that show at most; the multi-pick's OK and Cancel row
constexpr int kListInset = 4;                      // the list's margin at the left, the right and the bottom
constexpr int kBarW = 17;                          // the native list's scroll bar (the rows end where they ended)
constexpr int kButtonW = 96, kButtonH = 26;
}  // namespace listPickLayout

// The picker's view. `items`, `current` (the value with the check mark) and `prompt` (the search's placeholder) are the
// caller's; multi: the multi-pick, with `checked` checked at the start and `okText` on OK.
class ListPickView final : public draw::View {
public:
    ListPickView(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current) : items_(std::move(items)), current_(current) {
        field_.setPlain(true);
        field_.setPlaceholder(prompt);
        field_.onChange = [this] { fill(); };
        add(field_, {0, 0, 0, 0});
        bar_.onScroll = [this](int pos) {
            top_ = pos;
            invalidate();
        };
        add(bar_, {0, 0, 0, 0});
        fill();
    }
    // The multi-pick: a row's pick checks it; OK and Cancel under the list.
    void setMulti(std::set<std::string> checked, const std::wstring& okText) {
        multi_ = true, checked_ = std::move(checked);
        field_.setFilter([](wchar_t ch) { return ch != L' '; });  // (Space checks a row: no space in the search)
        ok_.setText(okText), ok_.setTone(stardraft::kit::Tone::Primary);
        ok_.onClick = [this] { finish(true); };
        cancel_.setText(tr(L"Cancel"));
        cancel_.onClick = [this] { finish(false); };
        add(cancel_, {0, 0, 0, 0});
        add(ok_, {0, 0, 0, 0});
    }
    // The icon of an item with one (iconDat and icon): the program's picture of that entry (empty: none).
    std::function<draw::Image(const ListPickItem&)> iconOf;

    // The window's outer size (its border is the host's).
    draw::Point outerSize() const {
        using namespace listPickLayout;
        std::set<std::wstring> sections;
        for (const auto& it : items_)
            if (!it.section.empty()) sections.insert(it.section);
        const int rows = std::min<int>(kMaxRows, int(items_.size() + sections.size()));
        const int h = 3 * scale(kPad) + scale(kSearchH) + std::max(2, rows) * rowH() + scale(6) + footerH();
        return {scale(kW), h};
    }

    const std::vector<ListPickItem>& items() const { return items_; }
    const std::string& current() const { return current_; }
    const std::set<std::string>& checked() const { return checked_; }
    // What was picked (single pick), whether the picker closed with a pick (multi: OK), and the Tab's direction (+1 Tab,
    // -1 Shift+Tab, 0 none).
    const std::string& result() const { return result_; }
    bool picked() const { return picked_; }
    bool done() const { return done_; }
    int advance() const { return advance_; }
    // The search field (the host gives it the keyboard when the picker opens).
    stardraft::kit::TextField& searchField() { return field_; }
    // The rows that the search shows (not the section headings), for a snapshot's input lines.
    std::vector<std::wstring> shownLabels() const {
        std::vector<std::wstring> out;
        for (const int idx : rows_.shown)
            if (idx >= 0 && size_t(idx) < items_.size()) out.push_back(items_[size_t(idx)].label);
        return out;
    }

    // The search's text, as typed (the self-tests type this way).
    void setSearch(const std::wstring& t) {
        field_.setText(t);
        fill();
    }
    // Picks the selected row (multi: checks it), as a click or Enter does.
    void choose() {
        if (sel_ < 0 || sel_ >= int(rows_.shown.size()) || rows_.shown[size_t(sel_)] < 0) return;
        const std::string& value = items_[size_t(rows_.shown[size_t(sel_)])].value;
        if (multi_) {  // checks the row; the list stays open
            if (!checked_.erase(value)) checked_.insert(value);
            invalidate();
            return;
        }
        result_ = value;
        finish(true);
    }
    // Closes the picker: with a pick (the result, or multi: the checks) or without.
    void finish(bool picked) {
        if (done_) return;
        picked_ = picked, done_ = true;
        if (onDone) onDone();
    }
    // Closes it with this value picked (a self-test's pick).
    void pickValue(const std::string& v) { result_ = v, finish(true); }
    // Tab (back: Shift+Tab) in a single pick: picks the row and asks for the next value.
    void tab(bool back) {
        if (multi_) return;  // a multi-pick: Tab does nothing (Enter confirms, Esc cancels)
        choose();
        advance_ = back ? -1 : 1;
        finish(picked_);
    }
    // Runs when the picker closes itself (a pick, OK, Cancel, Esc): the host closes its window.
    std::function<void()> onDone;

    void resized() override { laid_ = false; }

    void paint(draw::Canvas& c) override {
        using namespace listPickLayout;
        if (!laid_) layout(c);
        c.fill({0, 0, width(), height()}, kBg);
        // The search row: the icon, the line under it.
        const int is = scale(16);
        c.icon({search_.left, (search_.top + search_.bottom - is) / 2, search_.left + is, (search_.top + search_.bottom + is) / 2}, int(UiIcon::Search), kDim, is);
        c.fill({0, search_.bottom + scale(4), width(), search_.bottom + scale(5)}, kBorder);
        // The rows.
        c.pushClip(list_);
        const int last = std::min<int>(int(rows_.shown.size()), top_ + list_.height() / std::max(1, rowH()) + 2);
        for (int i = std::max(0, top_); i < last; ++i) paintRow(c, i, rowRect(i));
        c.popClip();
    }

    bool mouse(const draw::MouseEvent& e) override {
        using draw::MouseKind;
        if (e.kind == MouseKind::Wheel) {
            if (!list_.contains(e.pt)) return false;
            scrollTo(top_ - (e.exact ? draw::exactSteps(e, float(rowH()), wheelCarry_) : int(e.wheel * 3)));  // (3 rows a notch, as the list box)
            return true;
        }
        // (A double click's second press only selects, as the native list's did: a multi-pick checks the row once.)
        if (e.kind != MouseKind::Move && !(e.kind == MouseKind::Down && e.button == 0) && e.kind != MouseKind::DoubleClick) return false;
        const int i = rowAt(e.pt);
        if (i < 0) return e.kind != MouseKind::Move;
        if (i != sel_) setSel(i);  // the row under the mouse is the selected one
        if (e.kind == MouseKind::Down) choose();
        return true;
    }

    bool key(const draw::KeyEvent& e) override {
        using draw::Key;
        if (!e.down) return false;
        const int n = int(rows_.shown.size());
        switch (e.key) {
        case Key::Down:
        case Key::Up:
        case Key::PageDown:
        case Key::PageUp: {
            const int step = e.key == Key::Down ? 1 : e.key == Key::Up ? -1 : e.key == Key::PageDown ? 10 : -10;
            int next = std::clamp(sel_ + step, 0, std::max(0, n - 1));
            while (next >= 0 && next < n && rows_.shown[size_t(next)] < 0) next += step > 0 ? 1 : -1;  // skip headings
            if (next >= 0 && next < n) setSel(next);
            return true;
        }
        case Key::Enter:  // a multi-pick: Enter is OK (the checks are kept)
            if (multi_) finish(true);
            else choose();
            return true;
        case Key::Escape: finish(false); return true;
        case Key::Space:  // a multi-pick: Space checks the row (in a single pick, Space is a space of the search)
            if (!multi_) return false;
            choose();
            return true;
        case Key::Tab:
            tab((e.mods & draw::kShift) != 0);
            return true;
        default: return false;
        }
    }

private:
    int rowH() const {
        const bool icons = std::any_of(items_.begin(), items_.end(), [](const ListPickItem& i) { return i.icon >= 0; });
        return scale(icons ? listPickLayout::kIconRowH : listPickLayout::kRowH);
    }
    int footerH() const { return multi_ ? scale(listPickLayout::kFooterH) : 0; }

    // The parts' places (the field's height follows the font: the canvas measures it).
    void layout(draw::Canvas& c) {
        using namespace listPickLayout;
        laid_ = true;
        const draw::Point outer = outerSize();
        const int w = outer.x, h = outer.y, pad = scale(kPad), eh = scale(kSearchH);
        search_ = {pad + scale(6), pad, w - pad, pad + eh};
        // The native edit's box was the text's height + 4 px, centered in the row, with its text at the top and 3 px in;
        // the kit field (plain: 2 px in, the text centered) is the text's height from that top, 1 px further right.
        const int editH = c.metrics().height + scale(4), editX = search_.left + scale(26), editY = pad + (eh - editH) / 2;
        field_.setFrame({editX + 1, editY, w - pad, editY + c.metrics().height});
        const int listTop = search_.bottom + scale(9);
        list_ = {scale(kListInset), listTop, w - scale(kListInset), h - scale(kListInset) - footerH()};
        if (multi_) {
            const int bw = scale(kButtonW), bh = scale(kButtonH), by = h - footerH() + (footerH() - bh) / 2 - scale(4);
            cancel_.setFrame({w - pad - 2 * bw - scale(8), by, w - pad - bw - scale(8), by + bh});
            ok_.setFrame({w - pad - bw, by, w - pad, by + bh});
        }
        setSel(sel_);
        syncBar();
    }
    // The rows that fit whole in the list.
    int pageRows() const { return std::max(1, list_.height() / std::max(1, rowH())); }
    bool barShown() const { return int(rows_.shown.size()) * rowH() > list_.height(); }
    void syncBar() {
        bar_.setVisible(barShown());
        bar_.setFrame({list_.right - scale(stardraft::kit::kScrollBarW), list_.top, list_.right, list_.bottom});
        bar_.setRange(int(rows_.shown.size()), pageRows());
        bar_.setPos(top_);
    }
    void scrollTo(int top) {
        top = std::clamp(top, 0, std::max(0, int(rows_.shown.size()) - pageRows()));
        if (top == top_) return;
        top_ = top;
        bar_.setPos(top_);
        invalidate();
    }
    // Selects row i and scrolls it into view (as the list box's selection did).
    void setSel(int i) {
        sel_ = i;
        if (i >= 0) {
            if (i < top_) scrollTo(i);
            else if (i >= top_ + pageRows()) scrollTo(i - pageRows() + 1);
        }
        invalidate();
    }
    draw::Rect rowRect(int i) const {
        const int y = list_.top + (i - top_) * rowH();
        const int right = barShown() ? list_.right - scale(listPickLayout::kBarW) : list_.right;
        return {list_.left, y, right, y + rowH()};
    }
    int rowAt(draw::Point p) const {
        if (!list_.contains(p) || (barShown() && p.x >= list_.right - scale(listPickLayout::kBarW))) return -1;
        const int i = top_ + (p.y - list_.top) / std::max(1, rowH());
        return i < int(rows_.shown.size()) ? i : -1;
    }

    // The rows for the search's words; the selection and the scroll start again.
    void fill() {
        words_ = listPickWords(field_.text());
        rows_ = listPickRows(items_, current_, words_);
        top_ = 0;
        if (laid_) setSel(rows_.sel), syncBar();
        else sel_ = rows_.sel;  // (scrolled into view once the list has its place: layout)
        invalidate();
    }

    // A row: on the window's ground; the selected row a rounded pill; the current value (multi: a checked one) with a
    // check mark at the right; the typed words in the accent. A section heading: small and dim.
    void paintRow(draw::Canvas& c, int i, draw::Rect r) {
        const int idx = rows_.shown[size_t(i)];
        // (the native list's pill: its radius was scaled twice, as ui::box scales the radius it is given)
        if (i == sel_ && idx >= 0) c.roundBox({r.left + scale(2), r.top + scale(1), r.right - scale(2), r.bottom - scale(1)}, kSel, kSel, float(scale(scale(6))));
        if (idx < 0) {
            const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}));  // (ui::smallFont)
            c.text({r.left + scale(10), r.top + scale(6), r.right - scale(10), r.bottom}, rows_.headings[size_t(-idx - 1)], kDim, draw::kMiddle | draw::kEllipsis);
            c.setFont(of);
            return;
        }
        const ListPickItem& it = items_[size_t(idx)];
        int x = r.left + scale(10);
        if (it.icon >= 0) {
            if (iconOf)
                if (const draw::Image img = iconOf(it)) c.fitted(img, {x, r.top + scale(3), x + scale(26), r.bottom - scale(3)});
            x += scale(34);
        }
        const bool mark = multi_ ? checked_.count(it.value) != 0 : it.value == current_;
        const int checkW = scale(22);
        if (mark) {
            const int cs = scale(16), cx = r.right - scale(8) - cs;
            c.icon({cx, (r.top + r.bottom - cs) / 2, cx + cs, (r.top + r.bottom + cs) / 2}, int(UiIcon::Check), kAccent, cs);
        }
        drawMatched(c, {x, r.top, r.right - scale(10) - checkW, r.bottom}, it.label, words_, kText);
    }

    std::vector<ListPickItem> items_;
    std::string current_, result_;
    std::set<std::string> checked_;
    bool multi_ = false, done_ = false, picked_ = false, laid_ = false;
    int advance_ = 0;
    std::vector<std::wstring> words_;
    ListPickRows rows_;
    int sel_ = -1, top_ = 0;
    float wheelCarry_ = 0;  // an exact scroll's part of a row (draw::exactSteps)
    draw::Rect search_, list_;
    stardraft::kit::TextField field_;
    stardraft::kit::ScrollBar bar_;
    stardraft::kit::Button ok_, cancel_;
};

// Runs the picker at `at` (the SDL program: a point in the window that takes the input now: the main window, or the dialog or pop-out it opens from) until it is picked or closed.
// True when picked (the view's result, or its checks). `opened` runs once its window shows (the self-tests).
inline ListPickView* gListPickShown = nullptr;  // the picker while it is open (a snapshot's input lines say its rows)
inline bool runListPick(ListPickView& v, draw::Point at, const std::function<void()>& opened = nullptr) {
    v.onDone = [&v] { shellClosePopout(v); };
    const draw::Point size = v.outerSize();
    ListPickView* const outer = std::exchange(gListPickShown, &v);
    shellRunPopout(v, at.x, at.y, size.x, size.y, &v.searchField(), opened);
    gListPickShown = outer;
    return v.picked();
}
