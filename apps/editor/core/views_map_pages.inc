// The map pages' frame (portable, core.inc): the page's title, Classic settings' tabs, the bar (Apply and Revert, when the
// page has one) and the scroll view that holds the page's content. The pages' own controls are still the program's
// (map_pages.inc): the content is native windows, placed by the program when the scroll view asks (MapPagesHost). The
// views sit in view windows: the frame in one, the scroll view in a child window with the scroll bars.

// The program's part: what the page shows and what it does (map_pages.inc: MapPagesProgram).
class MapPagesHost {
public:
    virtual ~MapPagesHost() = default;
    struct Tab {
        std::wstring name;
        UiIcon icon;
    };
    virtual std::wstring title() = 0;  // the page's title ("" none)
    virtual std::vector<Tab> tabs() = 0;  // Classic settings' tabs; none for the other pages
    virtual int tab() = 0;                // the tab that shows
    virtual void showTab(int k) = 0;
    virtual bool changed() = 0;  // the open page has changes that are not applied (the bar's dot and Apply, Revert)
    virtual int footerH() = 0;   // the bar's height (0: no bar; every change goes into the map at once)
    virtual void apply() = 0;
    virtual void revert() = 0;
    // The scroll view's content: its size, the size it fills when smaller (Classic triggers), and where it goes.
    virtual draw::Point contentSize() = 0;
    virtual void fitContent(int w, int h) = 0;
    virtual void placeContent(int x, int y) = 0;
};

// The page's frame: the title and the tabs at the top, the bar at the bottom. The scroll view below it is another window.
class MapPagesView final : public draw::View {
public:
    explicit MapPagesView(MapPagesHost& host) : host_(host) {
        apply_.setTone(stardraft::kit::Tone::Primary);
        apply_.setText(L"Apply");
        apply_.setTip(L"Set these values in the map (one undo step)");
        apply_.onClick = [this] { host_.apply(); };
        revert_.setText(L"Revert");
        revert_.setTip(L"Discard the changes that are not applied");
        revert_.onClick = [this] { host_.revert(); };
        add(apply_, {0, 0, 0, 0});
        add(revert_, {0, 0, 0, 0});
        apply_.setVisible(false), revert_.setVisible(false);
    }

    // The height of the header (the title, and the tabs under it) and of the bar.
    int headerH() {
        return scale(56) + (host_.tabs().empty() ? 0 : scale(40));
    }
    int footerH() { return host_.footerH(); }

    // Clicks the bar's button as the mouse would (a test): false when the bar has no such button, or it is off.
    bool clickBar(bool apply) {
        stardraft::kit::Button& b = apply ? apply_ : revert_;
        if (!b.visible() || !b.enabled() || !b.onClick) return false;
        b.onClick();
        return true;
    }

    void paint(draw::Canvas& c) override {
        const int pad = scale(stardraft::kit::kDialogPad), headH = headerH(), footH = footerH();
        // The header: the page's title; the tabs under it.
        const std::wstring title = host_.title();
        if (!title.empty()) {
            const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)}));
            c.text({pad, 0, width() - pad, scale(56)}, title, kText, draw::kMiddle | draw::kEllipsis);
            c.setFont(of);
        }
        tabRects_.clear();
        const std::vector<MapPagesHost::Tab> tabs = host_.tabs();
        if (!tabs.empty()) {
            int x = pad - scale(12);
            const int icon = scale(16), top = scale(56), cur = host_.tab();
            for (size_t k = 0; k < tabs.size(); ++k) {
                const draw::Rect t{x, top, x + scale(14) + icon + scale(8) + c.measure(tabs[k].name) + scale(14), headH};
                const bool on = cur == int(k), hot = hot_ == int(k);
                if (on || hot) c.fill(t, on ? kSel : kRowHot);
                if (on) c.fill({t.left, t.bottom - scale(2), t.right, t.bottom}, kAccent);
                const draw::Color col = on ? kAccent : hot ? kText : kDim;
                c.icon({t.left + scale(14), t.top, t.left + scale(14) + icon, t.bottom}, int(tabs[k].icon), col, icon);
                c.text({t.left + scale(14) + icon + scale(8), t.top, t.right, t.bottom}, tabs[k].name, col, draw::kMiddle);
                tabRects_.push_back(t);
                x = t.right + scale(4);
            }
        }
        c.fill({0, headH - 1, width(), headH}, kBorder);
        // The bar: a line over it; what the page has; Apply and Revert at the right.
        if (!footH) {
            apply_.setVisible(false), revert_.setVisible(false);
            return;
        }
        const int barTop = height() - footH;
        c.fill({0, barTop, width(), barTop + 1}, kBorder);
        const bool changed = host_.changed();
        const int by = barTop + (footH - scale(stardraft::kit::kButtonH)) / 2, bh = scale(stardraft::kit::kButtonH);
        if (changed) {
            const int r = scale(3), cx = pad + scale(4), cy = by + bh / 2;
            c.ellipse({cx - r, cy - r, cx + r + 1, cy + r + 1}, kGood, draw::kNone);
            c.text({pad + scale(16), by, width() / 2, by + bh}, L"Changes that are not applied", kDim, draw::kMiddle | draw::kEllipsis);
        }
        const int rw = revert_.preferredWidth(c), aw = apply_.preferredWidth(c);
        const draw::Rect rr{width() - pad - rw, by, width() - pad, by + bh};
        const draw::Rect ar{rr.left - scale(stardraft::kit::kButtonGap) - aw, by, rr.left - scale(stardraft::kit::kButtonGap), by + bh};
        if (apply_.frame().left != ar.left || apply_.frame().top != ar.top || apply_.width() != ar.width()) apply_.setFrame(ar);
        if (revert_.frame().left != rr.left || revert_.frame().top != rr.top || revert_.width() != rr.width()) revert_.setFrame(rr);
        if (apply_.enabled() != changed) apply_.setEnabled(changed);  // (a set invalidates: only on a change)
        if (revert_.enabled() != changed) revert_.setEnabled(changed);
        apply_.setVisible(true), revert_.setVisible(true);
    }

    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Leave) {
            if (hot_ >= 0) hot_ = -1, invalidate();
            return false;
        }
        const int k = tabAt(e.pt);
        if (e.kind == draw::MouseKind::Move) {
            if (k != hot_) hot_ = k, invalidate();
            return false;
        }
        if (e.kind == draw::MouseKind::Down && e.button == 0) focus();  // the press takes the keyboard focus off the page's edits (the old frame window took it)
        if (e.kind == draw::MouseKind::Down && e.button == 0 && k >= 0) {
            host_.showTab(k);
            return true;
        }
        return false;
    }
    draw::Cursor cursor(draw::Point pt) override { return tabAt(pt) >= 0 ? draw::Cursor::Hand : draw::Cursor::Arrow; }

private:
    int tabAt(draw::Point pt) const {
        for (size_t k = 0; k < tabRects_.size(); ++k)
            if (tabRects_[k].contains(pt)) return int(k);
        return -1;
    }

    MapPagesHost& host_;
    stardraft::kit::Button apply_, revert_;
    std::vector<draw::Rect> tabRects_;  // the tabs as the last paint drew them
    int hot_ = -1;                      // the tab under the mouse
};

// The scroll view: the content (the program's windows, placed by the host) is scrolled when the page is smaller than it.
// Its window has the scroll bars; the view keeps the position and says where the content goes.
class MapScrollView final : public draw::View {
public:
    explicit MapScrollView(MapPagesHost& host) : host_(host) {}

    int scrollX() const { return x_; }
    int scrollY() const { return y_; }

    // Scrolls to (x, y), clamped to the content (a page smaller than the view does not scroll).
    void scrollTo(int x, int y) {
        host_.fitContent(width(), height());
        const draw::Point s = host_.contentSize();
        x_ = std::clamp(x, 0, std::max(0, s.x - width()));
        y_ = std::clamp(y, 0, std::max(0, s.y - height()));
        host_.placeContent(x_, y_);
        invalidate();  // the scroll bars follow
    }

    bool vscroll(int code, int /*pos*/, int track, int page) override {
        scrollBy(code, track, page, true);
        return true;
    }
    bool hscroll(int code, int /*pos*/, int track, int page) override {
        scrollBy(code, track, page, false);
        return true;
    }
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, host_.contentSize().y - 1), page = height(), pos = y_;
        return true;
    }
    bool windowHScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, host_.contentSize().x - 1), page = width(), pos = x_;
        return true;
    }
    // The wheel scrolls down and up three lines a notch (a sideways wheel does nothing here).
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind != draw::MouseKind::Wheel || e.horizontal) return false;
        scrollTo(x_, y_ - int(e.wheel * float(scale(32)) * 3.0f));
        return true;
    }
    void resized() override { scrollTo(x_, y_); }

private:
    // The bar's request: a line is 32 px, a page is the bar's page, a dragged thumb is where it is.
    void scrollBy(int code, int track, int page, bool vertical) {
        int p = vertical ? y_ : x_;
        switch (code) {
        case 0: p -= scale(32); break;  // SB_LINEUP
        case 1: p += scale(32); break;  // SB_LINEDOWN
        case 2: p -= page; break;       // SB_PAGEUP
        case 3: p += page; break;       // SB_PAGEDOWN
        case 4:                         // SB_THUMBPOSITION
        case 5: p = track; break;       // SB_THUMBTRACK: the thumb's place, as the old page read it
        case 6: p = 0; break;           // SB_TOP
        case 7: p = 1 << 30; break;     // SB_BOTTOM
        }
        vertical ? scrollTo(x_, p) : scrollTo(p, y_);
    }

    MapPagesHost& host_;
    int x_ = 0, y_ = 0;
};
