// The help window (portable, core.inc): the help pages (docs/help/*.md, built into the program: stardraft/help.h) in
// the HTML view (html_view.h). Help > Documentation opens it at the index; F1 opens it at the page of what has the focus
// (a Data field: its place on its .dat's page). The host (sdl_help.inc) opens the window and runs the links that go
// outside and Send feedback about this page.
//
// The window: the side bar with the search and the list of the pages (or, with words in the search, the places that
// have them), the page at the right, and under them Back, Send feedback about this page, Open in the browser and
// Close. A link to another help page opens it in the window (Back goes to the page before); a link to another place
// opens in the browser. Each page ends with its sources and credits (the page's own section).

class HelpView final : public draw::View {
public:
    std::function<void(const std::string& url)> onOpenUrl;      // the browser
    std::function<void(const std::wstring& name)> onFeedback;   // Send feedback about this page ("Help page: <title>")
    std::function<void()> closed;                               // the view closed its window (Close, Esc)

    HelpView() {
        html_.onLink = [this](const std::string& url) {
            const stardraft::HelpLink l = stardraft::helpLinkTarget(url);
            if (!l.topic.empty()) show(l.topic.page + (l.topic.anchor.empty() ? "" : "#" + l.topic.anchor));
            else if (onOpenUrl) onOpenUrl(l.url);
        };
        html_.onScrollTo = [this](int y) { scroll_.setOffset(0, y); };
        html_.setExtraCss("body { padding: " + std::to_string(stardraft::kit::kDialogPad) + "px " + std::to_string(stardraft::kit::kDialogPad + 8) + "px; }");
        scroll_.setContent(&html_);
        search_.setPlaceholder(L"Search the help");
        search_.onChange = [this] { fill(); };
        list_.setEmptyHint(L"No pages have these words");
        list_.paintRow = [this](draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) { paintRow(c, i, r, s); };
        list_.onSelect = [this] { pick(list_.cursor()); };
        list_.onActivate = [this](int i) { pick(i); };
        back_.setText(L"Back");
        back_.setTip(L"Go back to the page before");
        back_.onClick = [this] { back(); };
        feedback_.setText(L"Send feedback about this page");
        feedback_.setTip(L"Tell the developers what is not correct or not clear on this page");
        feedback_.onClick = [this] {
            if (onFeedback) onFeedback(feedbackName());
        };
        online_.setText(L"Open in the browser");
        online_.setTip(L"Open this page on the web");
        online_.onClick = [this] {
            if (onOpenUrl) onOpenUrl(onlineUrl());
        };
        close_.setText(L"Close");
        close_.onClick = [this] { close(); };
        add(search_, {});
        add(list_, {});
        add(scroll_, {});
        for (auto* b : {&back_, &feedback_, &online_, &close_}) add(*b, {});
        fill();
    }

    // In the Help workspace (the main window), not a window of its own: no Close, and Esc does not close it.
    void setInWorkspace(bool on) {
        inWorkspace_ = on;
        relayout();
    }

    // The window's size at 100 %.
    static draw::Point size() { return {scale(1040), scale(740)}; }

    // Shows a topic ("units-dat#hit-points"; "": the index; a page that is not there: the index). `remember`: the page
    // shown now goes into Back's list.
    void show(const std::string& topic, bool remember = true) {
        stardraft::HelpTopic t = stardraft::helpSplitTopic(topic);
        if (!stardraft::helpPage(t.page)) t = {"index", ""};
        if (remember && !page_.empty() && (t.page != page_ || t.anchor != anchor_)) history_.push_back(page_ + (anchor_.empty() ? "" : "#" + anchor_));
        if (history_.size() > 50) history_.erase(history_.begin());
        const bool samePage = t.page == page_ && !html_.baseUrl().empty();
        page_ = t.page, anchor_ = t.anchor;
        if (const stardraft::HelpPage* p = stardraft::helpPage(page_); p && !samePage) html_.setHtml(p->html, stardraft::helpOnlineUrl(page_));
        pendingAnchor_ = true;
        scrollToAnchor();
        if (search_.text().empty()) selectPageRow();
        relayout();
    }
    // Back: the page before (false: none).
    bool back() {
        if (history_.empty()) return false;
        const std::string t = history_.back();
        history_.pop_back();
        show(t, false);
        return true;
    }

    const std::string& page() const { return page_; }
    const std::string& anchor() const { return anchor_; }
    std::string title() const {
        const stardraft::HelpPage* p = stardraft::helpPage(page_);
        return p ? p->title : std::string();
    }
    std::string onlineUrl() const { return stardraft::helpOnlineUrl(page_.empty() ? "index" : page_, anchor_); }
    std::wstring feedbackName() const { return L"Help page: " + widen(title().empty() ? page_ : title()); }
    size_t historySize() const { return history_.size(); }

    // The search's words (as typed: the list follows).
    void setSearch(const std::wstring& t) { search_.setText(t); }
    std::wstring searchText() const { return search_.text(); }
    // The list's rows: the pages (no search), or the places that have the words.
    bool searching() const { return !hits_.empty() || !search_.text().empty(); }
    int rows() const { return list_.count(); }
    std::wstring rowTitle(int i) const {
        if (searching()) return i >= 0 && i < int(hits_.size()) ? widen(hits_[size_t(i)].title) : L"";
        const auto& pages = stardraft::helpPages();
        if (i == 0 && !pages.empty()) return L"Contents";  // (the index)
        return i >= 0 && i < int(pages.size()) ? widen(pages[size_t(i)].title) : L"";
    }
    std::string rowTopic(int i) const {
        if (searching()) return i >= 0 && i < int(hits_.size()) ? hits_[size_t(i)].page + (hits_[size_t(i)].anchor.empty() ? "" : "#" + hits_[size_t(i)].anchor) : "";
        const auto& pages = stardraft::helpPages();
        return i >= 0 && i < int(pages.size()) ? pages[size_t(i)].name : "";
    }
    int selectedRow() const { return list_.isSelected(list_.cursor()) ? list_.cursor() : -1; }
    // A click on row `i` (as the list's click: it selects the row, and the window shows its place).
    void clickRow(int i) {
        list_.selectOnly(i);
        pick(i);
    }

    stardraft::html::HtmlView& html() { return html_; }
    stardraft::kit::ScrollView& scroller() { return scroll_; }
    SearchBox& search() { return search_; }
    // The buttons that show, in their order.
    std::vector<std::wstring> buttons() const {
        std::vector<std::wstring> out;
        for (const auto* b : {&back_, &feedback_, &online_, &close_})
            if (b->visible() && b->enabled()) out.push_back(b->text());
        return out;
    }

    void paint(draw::Canvas& c) override {
        const int w = width(), h = height();
        c.fill({0, 0, w, h}, kPanel);
        const draw::Rect side = sideRect(), sr = scroll_.frame();
        c.fill({side.right, 0, side.right + 1, sr.bottom}, kBorder);
        c.fill({0, sr.bottom, w, sr.bottom + 1}, kBorder);
        c.fill(sr, kBg);
        if (stardraft::helpPages().empty()) {
            const int pad = scale(stardraft::kit::kDialogPad);
            const draw::Font of = c.setFont(c.font({}));
            c.text({sr.left + pad, sr.top + pad, sr.right - pad, sr.top + pad + scale(48)},
                   L"The help pages are not in this build. Open in the browser shows them on the web.", kDim, draw::kWrap);
            c.setFont(of);
        }
        if (pendingAnchor_) scrollToAnchor();
    }
    void resized() override {
        relayout();
        if (pendingAnchor_) scrollToAnchor();
    }

    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if (e.key == draw::Key::Escape && !(e.mods & (draw::kAlt | draw::kShift)) && !inWorkspace_) return close(), true;
        if (e.key == draw::Key::Left && (e.mods & draw::kAlt)) return back(), true;
        if (e.key == static_cast<draw::Key>('F') && (e.mods & draw::kCtrl)) {  // Ctrl+F: the search
            if (host()) host()->focus(search_.field());
            return true;
        }
        if (e.mods & draw::kAlt) return false;
        return scroll_.key(e);  // (the arrows and the page keys scroll the page)
    }
    // Closes the window the view is in.
    void close() {
        if (host()) host()->closeWindow(*this);
        if (closed) closed();
    }

private:
    draw::Rect sideRect() const { return {0, 0, scale(270), std::max(0, height() - footerH())}; }
    int footerH() const { return scale(stardraft::kit::kDialogPad) * 2 + scale(stardraft::kit::kButtonH); }

    // The page's top, or its heading `anchor_` at the top (once the page is laid out).
    void scrollToAnchor() {
        const int w = scroll_.viewportWidth();
        if (w <= 0 || html_.baseUrl().empty()) return;
        html_.layout(w);
        if (html_.contentHeight() <= 0) return;
        scroll_.refresh();
        const int y = anchor_.empty() ? 0 : html_.anchorY(anchor_);
        scroll_.setOffset(0, std::max(0, y - scale(8)));
        pendingAnchor_ = false;
        invalidate();
    }
    // The list: the pages, or the search's places.
    void fill() {
        hits_ = stardraft::helpSearch(narrow(search_.text()));
        list_.setCount(0);  // (a new list: the old rows' selection goes)
        list_.setRowHeight(searching() ? 46 : 30);
        list_.setCount(searching() ? int(hits_.size()) : int(stardraft::helpPages().size()));
        list_.setScrollTop(0);
        if (!searching()) selectPageRow();
        invalidate();
    }
    void selectPageRow() {
        const auto& pages = stardraft::helpPages();
        for (size_t i = 0; i < pages.size(); ++i)
            if (pages[i].name == page_) list_.selectOnly(int(i)), list_.ensureVisible(int(i));
    }
    void pick(int i) {
        const std::string t = rowTopic(i);
        if (!t.empty()) show(t);
    }
    void paintRow(draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) {
        kitpaint::drawListRow(c, r, s.selected, s.hot);
        const int x = r.left + scale(12), right = r.right - scale(8);
        if (searching() && i < int(hits_.size())) {
            const stardraft::HelpHit& hit = hits_[size_t(i)];
            const int mid = (r.top + r.bottom) / 2;
            c.text({x, r.top + scale(4), right, mid + scale(1)}, widen(hit.title), kitpaint::rowText(s.selected), draw::kMiddle | draw::kEllipsis);
            c.text({x, mid, right, r.bottom - scale(4)}, widen(hit.pageTitle), kDim, draw::kMiddle | draw::kEllipsis);
            return;
        }
        c.text({x, r.top, right, r.bottom}, rowTitle(i), kitpaint::rowText(s.selected), draw::kMiddle | draw::kEllipsis);
    }
    // The side bar at the left (the search over the list), the page at the right, the buttons under them: Back at the
    // left, Close at the right with the page's buttons before it.
    void relayout() {
        const int w = width(), h = height();
        if (w <= 0 || h <= 0) return;
        const int pad = scale(stardraft::kit::kDialogPad), bh = scale(stardraft::kit::kButtonH), gap = scale(stardraft::kit::kButtonGap);
        const draw::Rect side = sideRect();
        const int sh = scale(30);
        search_.setFrame({side.left + scale(10), side.top + scale(10), side.right - scale(10), side.top + scale(10) + sh});
        list_.setFrame({side.left, side.top + scale(10) + sh + scale(8), side.right, side.bottom});
        scroll_.setFrame({side.right + 1, 0, w, side.bottom});
        draw::Canvas* m = shellMeasureCanvas();
        auto bw = [&](const stardraft::kit::Button& b) {
            return m ? b.preferredWidth(*m) : std::max(scale(stardraft::kit::kButtonMinW), int(b.text().size()) * scale(8) + scale(32));
        };
        const int y = h - pad - bh;
        back_.setEnabled(!history_.empty());
        back_.setFrame({pad, y, pad + bw(back_), y + bh});
        const bool any = !stardraft::helpPages().empty();
        feedback_.setVisible(any);
        close_.setVisible(!inWorkspace_);
        int x = w - pad;
        for (stardraft::kit::Button* b : {&close_, &online_, &feedback_}) {
            if (!b->visible()) continue;
            const int bwid = bw(*b);
            b->setFrame({x - bwid, y, x, y + bh});
            x -= bwid + gap;
        }
        invalidate();
    }

    std::string page_, anchor_;
    std::vector<std::string> history_;  // the topics before, the newest last (Back)
    std::vector<stardraft::HelpHit> hits_;
    bool pendingAnchor_ = false;  // the page's place is to be shown once the page is laid out
    bool inWorkspace_ = false;
    SearchBox search_;
    stardraft::kit::ListView list_;
    stardraft::kit::ScrollView scroll_;
    stardraft::html::HtmlView html_;
    stardraft::kit::Button back_, feedback_, online_, close_;
};
