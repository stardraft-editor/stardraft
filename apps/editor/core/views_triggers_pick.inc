// The Triggers workspace's block picker (portable, core.inc; after views_triggers_pane.inc): categories on the left,
// blocks on the right, what the selected block does below, Add and Cancel at the bottom right. A view of kit widgets
// (a SearchBox, two ListViews, two Buttons) in a modal window (Host::runModal, a Dialog window with the system's
// frame). The host (main.cpp on Windows) gives the window's size and the help page (F1).
//
// The layout is the native picker's (its window was most of the screen, up to 1280 x 900; the sizes below are at
// 100 %, from the window's client area): the search row 16 px from the top, its icon at the left and a line under the
// row across the window; the two lists (rows 28 and 46 px high); the block's description in a box 190 px high with a
// line over it; the buttons 16 px from the right and the bottom edges.

class BlockPickHost {
public:
    virtual ~BlockPickHost() = default;
    virtual void help(const std::string& id) = 0;  // F1: the block's help page, beside the picker
};

std::vector<std::string> gRecentBlocks;  // the blocks added last, newest first (the picker's Recent)
const char* const kRecentCategory = "\x01Recent";

// The category each picker (actions, conditions) last showed, while StarDraft runs.
std::string gLastBlockCategory[2];

std::wstring trigTypeNote(const TriggerParam& p) {
    if (p.type.rfind("ValueOf:", 0) == 0) return L"a value of the property's type";
    const TriggerType* t = triggerType(p.type);
    return t ? widen(t->label) : L"? " + widen(p.type);
}

// The picker's results (how its window closed).
constexpr int kBlockPickAdd = 1, kBlockPickCancel = 2;

namespace blockPickLayout {
constexpr int kCatW = 260, kInfoH = 190, kSearchIcon = 18, kSearchTextX = 32, kSearchTextH = 18;
constexpr int kCatRowH = 28, kBlockRowH = 46;
constexpr int kNativeBarW = 17;  // the native list box's scroll bar (SM_CXVSCROLL): the rows kept their width beside it
}  // namespace blockPickLayout

class BlockPickView final : public draw::View {
public:
    BlockPickView(BlockPickHost& host, bool condition, const std::string& startCategory, const std::wstring& search) : host_(host), condition_(condition) {
        categories_.push_back("");
        const bool anyRecent = std::any_of(gRecentBlocks.begin(), gRecentBlocks.end(), [&](const std::string& id) {
            const TriggerDef* d = TriggerDef::find(id);
            return d && !d->value && d->condition == condition;
        });
        if (anyRecent) categories_.push_back(kRecentCategory);
        for (const auto& c : TriggerDef::categories(condition)) categories_.push_back(c);
        for (const std::string& cn : categories_) {  // how many blocks each shows
            int count = 0;
            for (const auto& def : TriggerDef::all())
                count += !def.value && def.condition == condition && def.tier != "replaced" && def.tier != "hidden" && (cn.empty() || def.category == cn);
            counts_.push_back(count);
        }

        search_.setPlaceholder(condition ? L"Search conditions" : L"Search actions");
        search_.field().setPlain(true);  // (the native edit had no frame: the row's icon and the line under it are its look)
        search_.onChange = [this] { fill(); };
        cats_.setRowHeight(blockPickLayout::kCatRowH);
        cats_.setCount(int(categories_.size()));
        cats_.paintRow = [this](draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) { paintCategory(c, i, nativeRow(cats_, r), s); };
        cats_.onSelect = [this] {
            search_.setText(L"");  // (fills the blocks again)
            fill();
        };
        blocks_.setRowHeight(blockPickLayout::kBlockRowH);
        blocks_.paintRow = [this](draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) { paintBlock(c, i, nativeRow(blocks_, r), s); };
        blocks_.onSelect = [this] { invalidate(); };
        blocks_.onActivate = [this](int) { choose(); };
        add_.setText(condition ? L"Add condition" : L"Add action");
        add_.setTone(stardraft::kit::Tone::Primary);  // (the native default button)
        add_.onClick = [this] { choose(); };
        cancel_.setText(L"Cancel");
        cancel_.setTone(stardraft::kit::Tone::Normal);
        cancel_.onClick = [this] { cancel(); };
        add(search_, {0, 0, 0, 0});
        add(cats_, {0, 0, 0, 0});
        add(blocks_, {0, 0, 0, 0});
        add(add_, {0, 0, 0, 0});
        add(cancel_, {0, 0, 0, 0});

        int start = 0;
        const std::string& want = startCategory.empty() ? gLastBlockCategory[condition] : startCategory;  // else the last one used
        for (size_t i = 0; i < categories_.size(); ++i)
            if (!want.empty() && categories_[i] == want) start = int(i);
        cats_.selectOnly(start);
        if (!search.empty()) search_.field().setText(search);  // (the caret at the end)
        fill();
    }

    // The block picked (after run: "" when none).
    const std::string& result() const { return result_; }
    bool picked() const { return picked_; }
    // The selected block (nullptr: none).
    const TriggerDef* selected() const {
        const int i = blocks_.cursor();
        return blocks_.isSelected(i) && i < int(shown_.size()) ? shown_[size_t(i)] : nullptr;
    }
    // Add: the selected block (nothing when no block is selected).
    void choose() {
        if (const TriggerDef* d = selected()) result_ = d->id, picked_ = true, close();
    }
    void cancel() { close(); }
    // The self-tests: a category row, the search text (as typed: the blocks follow).
    void selectCategory(int i) {
        cats_.selectOnly(i);
        if (cats_.onSelect) cats_.onSelect();
    }
    void selectCategoryNamed(const std::string& name) {
        int at = cats_.cursor();
        for (size_t i = 0; i < categories_.size(); ++i)
            if (categories_[i] == name) at = int(i);
        selectCategory(at);
    }
    void setSearch(const std::wstring& t) { search_.setText(t); }
    // The self-tests: the middle of block row `i` (the view's coordinates), the selected row (-1: none), closed.
    draw::Point blockRowCenter(int i) const {
        const draw::Rect r = blocks_.rowRect(i), f = blocks_.frame();
        return {f.left + (r.left + r.right) / 2, f.top + (r.top + r.bottom) / 2};
    }
    int selectedBlock() const { return selected() ? blocks_.cursor() : -1; }
    bool closed() const { return closed_; }

    void paint(draw::Canvas& c) override {
        using namespace blockPickLayout;
        if (!focused_ && host() && focusSearch) focused_ = true, search_.field().focus();
        armOpen();
        const int wa = add_.preferredWidth(c), wc = cancel_.preferredWidth(c);  // (the buttons' widths follow their texts)
        if (wa != buttonW_[1] || wc != buttonW_[0]) buttonW_[1] = wa, buttonW_[0] = wc, placeButtons();
        c.fill({0, 0, width(), height()}, kBg);
        const draw::Rect info = infoRect();
        c.fill({info.left, info.top - scale(8), info.right, info.top - scale(7)}, kBorder);
        const draw::Rect sr = searchRow();
        const int is = scale(kSearchIcon);
        c.icon({sr.left, (sr.top + sr.bottom - is) / 2, sr.left + is, (sr.top + sr.bottom + is) / 2}, int(UiIcon::Search), kDim, is);
        c.fill({0, sr.bottom + scale(6), width(), sr.bottom + scale(7)}, kBorder);
        drawBlockDocs(c, selected(), info);
    }

    void resized() override {
        using namespace blockPickLayout;
        const int pad = scale(stardraft::kit::kDialogPad), eh = scale(stardraft::kit::kFieldH), catW = scale(kCatW);
        const int W = width(), H = height();
        const int sy = pad + (eh - scale(kSearchTextH)) / 2 - scale(1);  // (the native edit drew its text 1 px in and 1 px higher than the kit field)
        search_.setFrame({pad + scale(kSearchTextX) + scale(1), sy, W - pad, sy + scale(kSearchTextH)});  // (so its x is 1 px higher than the native one)
        const int listTop = pad * 2 + eh, listH = H - listTop - scale(kInfoH) - pad * 3 - scale(32);
        cats_.setFrame({pad, listTop, pad + catW, listTop + listH});
        blocks_.setFrame({pad * 2 + catW, listTop, W - pad, listTop + listH});
        placeButtons();
        for (stardraft::kit::ListView* l : {&cats_, &blocks_}) {  // (before the window had a size, the lists had no room: from the top)
            l->setScrollTop(0);
            if (l->isSelected(l->cursor())) l->ensureVisible(l->cursor());
        }
        armOpen();  // (a window off the screen, in a background run, gets no paint)
    }

    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        using draw::Key;
        if (e.key == Key::Enter) return choose(), true;
        if (e.key == Key::Escape) return cancel(), true;
        if (e.key == Key::F1) {  // the selected block's help page, beside the picker
            if (const TriggerDef* d = selected()) host_.help(d->id);
            return true;
        }
        if ((e.key == Key::Down || e.key == Key::Up) && search_.field().hasFocus()) {  // the search's arrows move in the blocks
            const int n = int(shown_.size()), cur = blocks_.isSelected(blocks_.cursor()) ? blocks_.cursor() : -1;
            if (n) blocks_.selectOnly(std::clamp(cur + (e.key == Key::Down ? 1 : -1), 0, n - 1)), blocks_.ensureVisible(blocks_.cursor());
            invalidate();
            return true;
        }
        return false;
    }

    // The search field has the keyboard when the picker opens (not in a background run: the old picker did not take
    // the focus there).
    bool focusSearch = true;
    // Runs once, soon after the picker first paints (the self-tests capture and drive it from there).
    std::function<void()> onOpen;

private:
    // onOpen soon after the window is there (once).
    void armOpen() {
        if (opened_ || !host()) return;
        opened_ = true;
        if (onOpen) host()->timer(0, [this] {
            if (!closed_ && onOpen) onOpen();
        });
    }
    draw::Rect searchRow() const {
        const int pad = scale(stardraft::kit::kDialogPad);
        return {pad + scale(4), pad, width() - pad, pad + scale(stardraft::kit::kFieldH)};
    }
    draw::Rect infoRect() const {
        const draw::Rect l = cats_.frame();
        const int pad = scale(stardraft::kit::kDialogPad);
        return {pad, l.bottom + pad * 2, width() - pad, l.bottom + pad * 2 + scale(blockPickLayout::kInfoH)};
    }
    // The buttons as the native ones: each as wide as its text needs, kButtonH high, kButtonGap apart, 16 px from the
    // right and the bottom edges (ui::nativeButtonsRight).
    void placeButtons() {
        const int pad = scale(stardraft::kit::kDialogPad), bh = scale(stardraft::kit::kButtonH), y = height() - pad - bh;
        int x = width() - pad;
        for (stardraft::kit::Button* b : {&cancel_, &add_}) {
            const int w = buttonW_[b == &add_];
            b->setFrame({x - w, y, x, y + bh});
            x -= w + scale(stardraft::kit::kButtonGap);
        }
    }
    // A row as the native list box had it: beside its scroll bar (17 px), not the kit's (12 px).
    draw::Rect nativeRow(const stardraft::kit::ListView& l, draw::Rect r) const {
        if (l.scrollBar().visible()) r.right -= scale(blockPickLayout::kNativeBarW) - l.scrollBar().width();
        return r;
    }
    std::vector<std::wstring> words() const {
        std::wstring filter = search_.text();
        std::transform(filter.begin(), filter.end(), filter.begin(), ::towlower);
        std::vector<std::wstring> out;
        std::wistringstream in(filter);
        for (std::wstring w; in >> w;) out.push_back(w);
        return out;
    }
    // The selected category's blocks, or the ones that have all the typed words.
    void fill() {
        const int cat = cats_.cursor();
        const std::string category = cat > 0 && cat < int(categories_.size()) ? categories_[size_t(cat)] : "";
        gLastBlockCategory[condition_] = category;  // opens there next time
        const std::vector<std::wstring> ws = words();
        const std::string selId = selected() ? selected()->id : std::string();
        shown_.clear();
        for (const auto& d : TriggerDef::all()) {
            if (d.value || d.condition != condition_ || d.tier == "replaced" || d.tier == "hidden") continue;
            const bool recent = category == kRecentCategory;
            if (recent && ws.empty() && std::find(gRecentBlocks.begin(), gRecentBlocks.end(), d.id) == gRecentBlocks.end()) continue;
            if (!recent && !category.empty() && ws.empty() && d.category != category) continue;
            std::wstring hay = widen(d.title + " " + d.sentence + " " + d.category + " " + d.docs);
            std::transform(hay.begin(), hay.end(), hay.begin(), ::towlower);
            bool ok = true;
            for (const auto& w : ws) ok &= hay.find(w) != std::wstring::npos;
            if (ok) shown_.push_back(&d);
        }
        if (!ws.empty()) {  // best first: the title starts with what was typed, then contains it, then the rest
            auto rank = [&](const TriggerDef* d) {
                std::wstring title = widen(d->title);
                std::transform(title.begin(), title.end(), title.begin(), ::towlower);
                return title.rfind(ws[0], 0) == 0 ? 0 : title.find(ws[0]) != std::wstring::npos ? 1 : 2;
            };
            std::stable_sort(shown_.begin(), shown_.end(), [&](const TriggerDef* a, const TriggerDef* b) { return rank(a) < rank(b); });
        }
        if (category == kRecentCategory && ws.empty())
            std::stable_sort(shown_.begin(), shown_.end(), [](const TriggerDef* a, const TriggerDef* b) {
                return std::find(gRecentBlocks.begin(), gRecentBlocks.end(), a->id) < std::find(gRecentBlocks.begin(), gRecentBlocks.end(), b->id);
            });
        int sel = 0;
        for (size_t i = 0; i < shown_.size(); ++i)
            if (shown_[i]->id == selId) sel = int(i);
        blocks_.setCount(0);  // (a new list: the old rows' selection goes)
        blocks_.setCount(int(shown_.size()));
        blocks_.setScrollTop(0);
        if (!shown_.empty()) blocks_.selectOnly(sel), blocks_.ensureVisible(sel);
        invalidate();
    }
    void close() {
        if (closed_) return;
        closed_ = true;
        if (draw::Host* h = host()) h->closeWindow(*this);
    }

    // On the window's ground; the selected row a rounded pill (as the quick search and the lists).
    static void rowGround(draw::Canvas& c, draw::Rect r, bool sel) {
        c.fill(r, kBg);
        if (sel) c.roundBox({r.left + scale(2), r.top + scale(1), r.right - scale(2), r.bottom - scale(1)}, kSel, kSel, float(scale(6)));
    }
    void paintCategory(draw::Canvas& c, int i, draw::Rect row, stardraft::kit::RowState s) {
        if (i < 0 || i >= int(categories_.size())) return;
        rowGround(c, row, s.selected);
        const draw::Rect r{row.left + scale(12), row.top, row.right - scale(10), row.bottom};
        const std::string& cn = categories_[size_t(i)];
        const std::wstring name = cn.empty() ? L"Everything" : cn == kRecentCategory ? L"Recent" : widen(cn);
        c.text(r, name, s.selected ? kAccent : kText, draw::kLeft | draw::kMiddle);
        if (cn != kRecentCategory) {  // how many blocks it has, small and dim at the right
            const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}));
            c.text(r, std::to_wstring(counts_[size_t(i)]), kDim, draw::kRight | draw::kMiddle);
            c.setFont(of);
        }
    }
    // A block: its title (the typed words in the accent), then the sentence with its values' types in place.
    void paintBlock(draw::Canvas& c, int i, draw::Rect row, stardraft::kit::RowState s) {
        if (i < 0 || i >= int(shown_.size())) return;
        rowGround(c, row, s.selected);
        const draw::Rect r{row.left + scale(12), row.top, row.right - scale(10), row.bottom};
        const TriggerDef& def = *shown_[size_t(i)];
        drawMatched(c, {r.left, r.top + scale(4), r.right, r.top + scale(24)}, widen(def.title), words(), kText);  // (on the pill too)
        std::wstring sentence;
        for (size_t k = 0; k < def.sentence.size();) {
            if (def.sentence[k] == '{') {
                const size_t end = def.sentence.find('}', k);
                const int pi = def.param(def.sentence.substr(k + 1, end - k - 1));
                sentence += L"(" + (pi >= 0 ? trigTypeNote(def.params[size_t(pi)]) : L"?") + L")";
                k = end + 1;
                continue;
            }
            sentence += wchar_t(static_cast<unsigned char>(def.sentence[k++]));
        }
        if (!sentence.empty() && sentence[0] != L'(') sentence[0] = wchar_t(towupper(sentence[0]));
        c.text({r.left, r.top + scale(23), r.right, r.bottom - scale(3)}, sentence, kDim, draw::kLeft | draw::kEllipsis);
    }

    BlockPickHost& host_;
    bool condition_ = false;
    std::vector<std::string> categories_;  // "" first: all
    std::vector<int> counts_;              // the blocks of each category
    std::vector<const TriggerDef*> shown_;
    SearchBox search_;
    stardraft::kit::ListView cats_, blocks_;
    stardraft::kit::Button add_, cancel_;
    int buttonW_[2] = {scale(stardraft::kit::kButtonMinW), scale(stardraft::kit::kButtonMinW)};  // Cancel, Add
    std::string result_;
    bool picked_ = false, closed_ = false, focused_ = false, opened_ = false;
};

// Runs the picker from `from` (the program's anchor view) in a window of `size` (its client area). Returns the picked
// block's id, or "" when it was canceled. `shown` is the picker while it is open (the self-tests reach it there).
inline std::string runBlockPicker(draw::View& from, BlockPickHost& host, bool condition, const std::string& startCategory, const std::wstring& search,
                                  draw::Point size, bool focusSearch, BlockPickView*& shown, std::function<void()> onOpen = {}) {
    draw::Host* h = from.host();
    if (!h) return "";
    BlockPickView view(host, condition, startCategory, search);
    view.focusSearch = focusSearch, view.onOpen = std::move(onOpen);
    shown = &view;
    h->runModal(from, view, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect{0, 0, size.x, size.y}, condition ? L"Add a condition" : L"Add an action", true});
    shown = nullptr;
    return view.picked() ? view.result() : std::string();
}
