// The start page (portable, core.inc; docs/dev/CONTROLS.md, "New page and start page"): the project kinds as cards (a
// card opens its settings), the recent projects and maps (the newest first, each with its date and a remove button),
// Open, and the alpha notice under them. When no StarCraft install was found, a banner at the top asks for its folder
// (Browse). A view: it paints on a Canvas, and its Open button and remove buttons are kit
// widgets. The host (new_page.inc on Windows) says what a card and a row do, and gives the date of a file.

const wchar_t* kKindNames[] = {L"Classic project", L"EUD project", L"Campaign project", L"Mod project"};

// The cards: two sentences on what each kind lets you make.
const wchar_t* const kKindCards[] = {
    L"Make standard StarCraft maps.",
    L"Make maps with your own units, weapons and game rules. Change the game's data, and make new triggers with blocks "
    L"or with your own code.",
    L"A project with all the maps of your campaign. Go from one map to the next, and copy data and terrain between "
    L"the maps.",
    L"Make mods with your own graphics and game files. Use your own MPQs and data files, for StarCraft 1.16.1 and "
    L"Remastered.",
};

// The banner when no StarCraft install was found (the start page's top).
const wchar_t* const kNoInstallText = L"Could not find StarCraft on your system.";

// (two lines: what to do first, then how to give feedback)
const wchar_t* const kStartNotice = L"StarDraft 2 is alpha software: back up your maps and projects before you edit them.\n"
                                    L"Feedback helps: right-click anything and select \"Report feedback on \x2026\".";

// A button of the start page that never takes the keyboard focus (the page's keys are its own: the cards).
class StartButton final : public stardraft::kit::Button {
public:
    using stardraft::kit::Button::Button;
    bool focusable() const override { return false; }
};

// The banner's Browse: a stop of the page's Tab (the page moves the focus to it and from it: it is not a stop of the
// window's Tab of its own), with the focus ring of the other buttons; Enter or Space runs it.
class StartBrowse final : public stardraft::kit::Button {
public:
    using stardraft::kit::Button::Button;
    bool focusable() const override { return false; }
    void focusChanged(bool) override { invalidate(); }
    bool key(const draw::KeyEvent& e) override { return visible() && stardraft::kit::Button::key(e); }
};

// A row's remove button (a bare icon button that is shown under the mouse, or where the file is not found).
class StartRemove final : public stardraft::kit::IconButton {
public:
    bool focusable() const override { return false; }
};

class StartView final : public draw::View {
public:
    StartView() : open_(L"Open\x2026", stardraft::kit::Tone::Normal, nullptr), browse_(L"Browse\x2026", stardraft::kit::Tone::Primary, nullptr) {
        open_.onClick = [this] {
            if (onOpen) onOpen();
        };
        open_.setTip(L"A project or a map (Ctrl+O)");
        add(open_, {0, 0, 0, 0});
        browse_.onClick = [this] {
            if (onBrowseInstall) onBrowseInstall();
        };
        browse_.setTip(L"Select the StarCraft folder");
        browse_.setVisible(false);
        add(browse_, {0, 0, 0, 0});
    }

    // What the page does. onCard: 0 a classic project, 1 an EUD project. onOpenRecent / onRemoveRecent: project (else a
    // map) and its index in the recent list. dateOf: the date a file was last saved ("" when it is not there).
    std::function<void(int)> onCard;
    std::function<void(bool, size_t)> onOpenRecent, onRemoveRecent;
    std::function<void()> onOpen;
    std::function<std::wstring(const fs::path&)> dateOf;
    // The host's scroll bar for the page after a paint that changed it: the largest position, the page size, the position.
    std::function<void(int, int, int)> scrollInfo;
    // Why the game data is not open (the host sets it; empty when it is open): the page says so under the notice.
    std::wstring dataError;
    // No StarCraft install was found (the host sets it): the banner at the top asks for its folder. Its Browse runs
    // onBrowseInstall (the host's folder dialog). installNote: the banner's second line (a picked folder with no game
    // data); empty: none.
    bool installMissing = false;
    std::wstring installNote;
    std::function<void()> onBrowseInstall;
    // Whether the last paint showed the banner (the snapshots read it).
    bool bannerShown() const { return bannerShown_; }
    // Where the page's keys go (the snapshots read it): "Browse", "card 0", "card 1", or "none" (not on the page).
    std::string keysOn() const {
        if (browse_.visible() && browse_.hasFocus()) return "Browse";
        return hasFocus() ? "card " + std::to_string(card_) : "none";
    }

    // The card the keyboard is on (0 or 1); Left, Right and Tab move it, Enter and Space open it (Tab also goes to the
    // banner's Browse, when it is shown).
    // Tab came to the page: Browse, else the first card (back: the second card).
    void tabbedTo(bool back) override {
        if (back) card_ = 1;
        else if (browse_.visible() && browse_.enabled()) browse_.focus();
        else card_ = 0;
        invalidate();
    }
    void setCard(int k) {
        if (card_ != k) card_ = k, invalidate();
    }
    int selectedCard() const { return card_; }
    // The page is shown again: it starts at the top.
    void resetScroll() {
        if (scroll_) scroll_ = 0, invalidate();
    }
    // What the last paint showed (the self-tests read these).
    bool cardsShown() const { return cardsShown_; }
    int cardsEnabled() const { return cardsEnabled_; }
    int startProjects() const { return startProjects_; }
    int startMaps() const { return startMaps_; }
    // The New page is painted: the start page's cards are not shown (the self-tests read cardsShown).
    void resetCards() { cardsShown_ = false; }

    void paint(draw::Canvas& c) override {
        using draw::Rect;
        using draw::Font;
        using draw::Cursor;
        using draw::kWrap;
        using draw::kLeft;
        using draw::kMiddle;
        using draw::kEllipsis;
        using draw::kCenter;
        using draw::kRight;
        using draw::kPathEllipsis;
        using draw::Face;
        const int w = width(), h = height();
        c.fill({0, 0, w, h}, kBg);
        hits_.clear();
        if (!installMissing && browse_.visible()) {
            if (browse_.hasFocus()) focus();  // (the keys go to the cards again)
            browse_.setVisible(false);
        }
        const int pad = scale(stardraft::kit::kDialogPad), rowH = scale(stardraft::kit::kRowH), bh = scale(stardraft::kit::kButtonH), gap = scale(16);
        const int avail = w - 2 * pad;
        const int cardW = std::clamp((avail - 3 * gap) / 4, scale(180), scale(320));
        const int cw = 4 * cardW + 3 * gap;
        const int x1 = std::max(pad, (w - cw) / 2);
        // The banner (no StarCraft install found): at the top of the page, as wide as the cards; the block is under it.
        const int bannerH = installMissing ? paintBanner(c, x1, pad - scroll_, cw) : 0;
        bannerShown_ = installMissing;
        const int top = bannerH ? pad + bannerH + gap : pad;
        const int startY = std::max(top, (h - blockH_) / 2) - scroll_;  // the block is in the middle (it has the notice under the list)
        int y = startY;

        // The cards: the kinds side by side. Click a card (or Enter on it) to go to its settings.
        const int inPad = scale(16), iconS = scale(32);
        int textH = 0;  // all cards have the height of the longest text
        for (const wchar_t* text : kKindCards) textH = std::max(textH, c.measureBox(text, cardW - 2 * inPad, kWrap).y);
        const int cardH = inPad + iconS + scale(12) + scale(26) + scale(6) + textH + inPad;
        const UiIcon icons[] = {UiIcon::Map, UiIcon::Project, UiIcon::Folder, UiIcon::Assets};
        cardsEnabled_ = 0;
        for (int k = 0; k < 4; ++k) {
            const bool enabled = k < 2;  // Campaign project (roadmap R25, not ready) and Mod project: later
            const Rect card{x1 + k * (cardW + gap), y, x1 + k * (cardW + gap) + cardW, y + cardH};
            const bool over = enabled && hot(card), focused = enabled && card_ == k && hasFocus();
            if (!enabled) c.roundBox(card, kReadOnly, kReadOnlyLine, float(scale(stardraft::kit::kRadius)));
            else c.roundBox(card, over ? kCtlHot : kCtl, focused ? kAccent : over ? kBorderHot : kBorder, float(scale(stardraft::kit::kRadius)));
            c.icon({card.left + inPad, card.top + inPad, card.left + inPad + iconS, card.top + inPad + iconS}, int(icons[k]), enabled ? kAccent : kFaint, iconS);
            int ty = card.top + inPad + iconS + scale(12);
            {
                const draw::Font of = c.setFont(titleFont(c));
                c.text({card.left + inPad, ty, card.right - inPad, ty + scale(26)}, kKindNames[k], enabled ? kText : kFaint, kMiddle | kEllipsis);
                c.setFont(of);
            }
            if (!enabled) {  // the "Coming later" mark, at the top right
                const draw::Font of = c.setFont(smallFont(c));
                const int lw = c.measure(L"Coming later") + scale(16), lh = scale(20);
                const Rect mark{card.right - inPad - lw, card.top + inPad, card.right - inPad, card.top + inPad + lh};
                c.roundBox(mark, kChipDim, kReadOnlyLine, float(scale(stardraft::kit::kRadius)));
                c.text(mark, L"Coming later", kDim, kMiddle | kCenter);
                c.setFont(of);
            }
            ty += scale(26) + scale(6);
            c.text({card.left + inPad, ty, card.right - inPad, card.bottom - inPad}, kKindCards[k], enabled ? kDim : kFaint, kWrap | kLeft);
            if (enabled) {
                ++cardsEnabled_;
                hits_.push_back({card, [this, k] {
                                     if (onCard) onCard(k);
                                 }, L"", Cursor::Hand, std::wstring(L"the ") + kKindNames[k] + L" card"});
            } else {
                hits_.push_back({card, nullptr, k == 2 ? L"Campaign projects come in a later version" : L"Mod projects come in a later version", Cursor::No});
            }
        }
        cardsShown_ = true;
        y += cardH + scale(32);

        // The recent projects and maps: a heading, then one row each (projects and maps together, the newest first).
        y = sectionHeader(c, x1, y, x1 + cw, rowH, L"Recent");
        struct Recent {
            bool project;
            size_t index;
            fs::path path;
            std::wstring date;
        };
        std::vector<Recent> items;
        for (size_t i = 0; i < gRecentProjects.size(); ++i) items.push_back({true, i, fs::path(gRecentProjects[i]), dateOf ? dateOf(gRecentProjects[i]) : L""});
        for (size_t i = 0; i < gRecentMaps.size(); ++i) items.push_back({false, i, fs::path(gRecentMaps[i]), dateOf ? dateOf(gRecentMaps[i]) : L""});
        std::stable_sort(items.begin(), items.end(), [](const Recent& a, const Recent& b) { return a.date > b.date; });
        startProjects_ = int(gRecentProjects.size()), startMaps_ = int(gRecentMaps.size());
        if (items.empty()) {
            c.text({x1 + scale(8), y, x1 + cw, y + rowH}, L"None", kDim, kMiddle);
            y += rowH;
        }
        size_t row = 0;
        for (const Recent& it : items) {
            const bool project = it.project;
            const size_t i = it.index;
            const Rect r{x1, y, x1 + cw, y + rowH};
            const bool over = hot(r);
            if (over) c.fill(r, kRowHot);  // the list row under the mouse
            hits_.push_back({r, [this, project, i] {
                                 if (onOpenRecent) onOpenRecent(project, i);
                             },
                             fs::path(it.path).make_preferred().wstring(), Cursor::Hand});
            c.icon({x1 + scale(8), r.top + (rowH - scale(16)) / 2, x1 + scale(24), r.top + (rowH - scale(16)) / 2 + scale(16)},
                   int(project ? UiIcon::Project : UiIcon::Map), kDim, scale(16));
            // Remove: under the mouse; always where the file is not found (the one thing to do with it).
            StartRemove& rm = removeAt(row++);
            const int bw = scale(stardraft::kit::kIconBox), bhh = std::min(rowH, bw);
            const Rect rmR{r.right - scale(4) - bw, (r.top + r.bottom - bhh) / 2, r.right - scale(4), (r.top + r.bottom - bhh) / 2 + bhh};
            const int actionsLeft = rmR.left;
            if (!sameRect(rm.frame(), rmR)) rm.setFrame(rmR);
            rm.setVisible(it.date.empty() || over);
            rm.onClick = [this, project, i] {
                if (onRemoveRecent) onRemoveRecent(project, i);
            };
            const int dateW = scale(96);
            const Rect dt{actionsLeft - dateW - scale(8), r.top, actionsLeft - scale(8), r.bottom};
            c.text(dt, it.date.empty() ? L"Not found" : it.date, it.date.empty() ? kWarn : kDim, kMiddle | kRight);
            const std::wstring name = it.path.filename().wstring();
            const Rect nt{x1 + scale(32), r.top, std::min<int>(x1 + scale(32) + cw * 2 / 5, dt.left - scale(16)), r.bottom};
            c.text(nt, name, kText, kMiddle | kEllipsis);
            const int nameRight = nt.left + std::min<int>(nt.width(), c.measure(name));
            const std::wstring folder = fs::path(it.path.parent_path()).make_preferred().wstring();
            c.text({nameRight + scale(16), r.top, dt.left - scale(16), r.bottom}, folder, kDim, kMiddle | kPathEllipsis);
            y += rowH;
        }
        for (size_t k = row; k < removes_.size(); ++k) removes_[k]->setVisible(false);

        // Open: a project or a map that is not in the list.
        y += scale(12);
        const int ow = std::max(scale(stardraft::kit::kButtonMinW), c.measure(L"Open\x2026") + scale(28));
        if (!sameRect(open_.frame(), {x1, y, x1 + ow, y + bh})) open_.setFrame({x1, y, x1 + ow, y + bh});
        y += bh;

        // The alpha notice: under the recent list and Open.
        const int ny = y + scale(24);
        {
            const draw::Font of = c.setFont(c.font({Face::Glyphs, scale(14)}));
            c.text({x1, ny + scale(2), x1 + scale(16), ny + scale(18)}, L"\xE946", kAccent, kMiddle | kCenter);
            c.setFont(of);
        }
        const Rect nr{x1 + scale(22), ny, x1 + cw, ny + scale(200)};
        c.text(nr, kStartNotice, kDim, kWrap | kLeft);
        y = nr.top + c.measureBox(kStartNotice, nr.width(), kWrap).y;
        if (!dataError.empty()) {  // no game data: why the maps cannot open, under the notice
            const Rect er{x1, y + scale(16), x1 + cw, y + scale(16) + scale(400)};
            c.text(er, dataError, kWarn, kWrap | kLeft);
            y = er.top + c.measureBox(dataError, er.width(), kWrap).y;
        }

        const int bh2 = y - startY;
        if (bh2 != blockH_) {  // the height changed (a row came or went): paint again, in the middle
            blockH_ = bh2;
            invalidate();
        }
        height_ = y + scroll_ + pad;
        if (scrollInfo && (height_ != shownHeight_ || h != shownPage_ || scroll_ != shownPos_)) {
            shownHeight_ = height_, shownPage_ = h, shownPos_ = scroll_;
            scrollInfo(std::max(0, height_ - 1), h, scroll_);
        }
    }

    bool mouse(const draw::MouseEvent& e) override {
        switch (e.kind) {
        case draw::MouseKind::Move:
        case draw::MouseKind::Leave: {  // a row or a card under the mouse is painted again
            const int hv = e.kind == draw::MouseKind::Leave ? -1 : hitAt(e.pt);
            if (hv != hover_) hover_ = hv, invalidate();
            return false;
        }
        case draw::MouseKind::Down:
            if (e.button != 0) return false;
            if (const int hi = hitAt(e.pt); hi >= 0 && hits_[size_t(hi)].click) {
                // A copy: the click may repaint, which clears hits_ (and would destroy the function while it runs).
                const std::function<void()> run = hits_[size_t(hi)].click;
                run();
                return true;
            }
            return false;
        case draw::MouseKind::Wheel:
            scrollBy(-int(draw::wheelDistance(e, float(scale(32) * 3))));
            return true;
        default: return false;
        }
    }

    // The keys: the cards. Left and Right move the card, Enter and Space open it. Tab goes through the banner's Browse
    // (when it shows), the first card and the second card, then on to the window's next control (Shift+Tab: back);
    // Browse takes Enter and Space itself.
    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        const bool banner = browse_.visible(), onBrowse = banner && browse_.hasFocus();
        if (e.key == draw::Key::Left || e.key == draw::Key::Right) {
            card_ = e.key == draw::Key::Left ? 0 : 1;
            if (onBrowse) focus();
        } else if (e.key == draw::Key::Tab && !(e.mods & (draw::kCtrl | draw::kAlt))) {
            const bool back = (e.mods & draw::kShift) != 0;
            if (onBrowse) {  // Browse is the first stop
                focus();
                if (back) return false;  // (the window's Tab goes on from the page)
                card_ = 0;
            } else if (back ? card_ == 0 : card_ == 1) {  // past the last card (back: the first)
                if (!(back && banner)) return false;
                browse_.focus();
            } else card_ = back ? 0 : 1;
        } else if (e.key == draw::Key::Enter || e.key == draw::Key::Space) {
            if (onCard) onCard(card_);
            return true;
        } else return false;
        invalidate();
        return true;
    }

    // The scroll bar of the window (the host calls this for its vertical scroll bar).
    bool vscroll(int code, int pos, int track, int) override {
        const int page = height();
        switch (code) {
        case 0: scrollTo(scroll_ - scale(32)); break;  // SB_LINEUP
        case 1: scrollTo(scroll_ + scale(32)); break;  // SB_LINEDOWN
        case 2: scrollTo(scroll_ - page); break;       // SB_PAGEUP
        case 3: scrollTo(scroll_ + page); break;       // SB_PAGEDOWN
        case 4: scrollTo(pos); break;                  // SB_THUMBPOSITION
        case 5: scrollTo(track); break;                // SB_THUMBTRACK
        case 6: scrollTo(0); break;                    // SB_TOP
        case 7: scrollTo(height_); break;              // SB_BOTTOM
        default: return false;
        }
        return true;
    }

    bool focusable() const override { return true; }
    void focusChanged(bool) override { invalidate(); }
    // The hover tooltip: a recent row shows its path, a disabled card and Open their tips (an enabled card has none).
    bool hoverTip() const override { return true; }
    draw::Cursor cursor(draw::Point pt) override {
        const int hi = hitAt(pt);
        return hi >= 0 ? hits_[size_t(hi)].cursor : draw::Cursor::Arrow;
    }
    std::wstring tip(draw::Point pt) override {
        const int hi = hitAt(pt);
        return hi >= 0 ? hits_[size_t(hi)].tip : std::wstring();
    }
    // Report feedback names what is under the mouse: an enabled card by its name (it has no tip), else the tip.
    std::wstring feedbackName(draw::Point pt) override {
        const int hi = hitAt(pt);
        if (hi < 0) return {};
        const Hit& h = hits_[size_t(hi)];
        return h.name.empty() ? h.tip : h.name;
    }

    int scrollPos() const { return scroll_; }  // (how far the page is scrolled down, in pixels)

private:
    struct Hit {
        draw::Rect r;
        std::function<void()> click;
        std::wstring tip;
        draw::Cursor cursor = draw::Cursor::Arrow;
        std::wstring name;  // what Report feedback names here, when it is not the tip (an enabled card)
    };

    // The last hit under `pt` (the top one), or -1.
    int hitAt(draw::Point pt) const {
        for (size_t i = hits_.size(); i-- > 0;)
            if (hits_[i].r.contains(pt)) return int(i);
        return -1;
    }
    static bool sameRect(draw::Rect a, draw::Rect b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
    // Whether the mouse is in `r` (the host's pointer, in this view's coordinates).
    bool hot(draw::Rect r) const { return host() && r.contains(host()->mouse(*this)); }

    void scrollTo(int s) {
        const int s2 = std::clamp(s, 0, std::max(0, height_ - height()));
        if (s2 != scroll_) scroll_ = s2, invalidate();
    }
    void scrollBy(int d) { scrollTo(scroll_ + d); }

    // The remove button of row `k` (made when the list is longer than before).
    StartRemove& removeAt(size_t k) {
        while (removes_.size() <= k) {
            auto rm = std::make_unique<StartRemove>();
            // Set once, before the button has a host: a setter repaints the view, and a paint must not start another paint.
            rm->setTone(stardraft::kit::Tone::Danger);
            rm->setGlyph(stardraft::kit::IconGlyph::ofIcon(int(UiIcon::Close)));
            rm->setTip(L"Remove from the list");
            removes_.push_back(std::move(rm));
            add(*removes_.back(), {0, 0, 0, 0});
        }
        return *removes_[k];
    }

    // The banner at (x, y), `w` wide: a warning icon, the text (and the note under it), Browse at the right. Returns
    // its height.
    int paintBanner(draw::Canvas& c, int x, int y, int w) {
        const int inPad = scale(12), iconS = scale(16), bh = scale(stardraft::kit::kButtonH);
        const int bw = std::max(scale(stardraft::kit::kButtonMinW), c.measure(L"Browse\x2026") + scale(28));
        const int textL = x + inPad + iconS + scale(10), textR = x + w - inPad - bw - scale(16);
        const int lineH = scale(20);
        const int noteH = installNote.empty() ? 0 : c.measureBox(installNote, std::max(1, textR - textL), draw::kWrap).y;
        const int contentH = std::max(bh, lineH + (noteH ? scale(2) + noteH : 0));
        const int h = contentH + 2 * inPad;
        const draw::Rect box{x, y, x + w, y + h};
        c.roundBox(box, kCtl, kWarn, float(scale(stardraft::kit::kRadius)));
        const int ty = y + inPad + (contentH - lineH - (noteH ? scale(2) + noteH : 0)) / 2;
        c.icon({x + inPad, ty + (lineH - iconS) / 2, x + inPad + iconS, ty + (lineH - iconS) / 2 + iconS}, int(UiIcon::Warning), kWarn, iconS);
        c.text({textL, ty, textR, ty + lineH}, kNoInstallText, kText, draw::kMiddle | draw::kEllipsis);
        if (noteH) c.text({textL, ty + lineH + scale(2), textR, ty + lineH + scale(2) + noteH}, installNote, kWarn, draw::kWrap | draw::kLeft);
        const draw::Rect br{x + w - inPad - bw, y + (h - bh) / 2, x + w - inPad, y + (h - bh) / 2 + bh};
        if (!sameRect(browse_.frame(), br)) browse_.setFrame(br);
        browse_.setVisible(true);
        return h;
    }

    // The title's font (the UI font at 3/2 of its height) and the small font (at 5/6), as the Windows program has them.
    static draw::Font titleFont(draw::Canvas& c) { return c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)}); }
    static draw::Font smallFont(draw::Canvas& c) { return c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}); }

    // A section heading: its name and a line to the column's edge (the form's heading).
    static int sectionHeader(draw::Canvas& c, int x0, int y, int colRight, int rowH, const wchar_t* name) {
        c.text({x0, y, colRight, y + rowH}, name, kText, draw::kMiddle);
        c.fill({x0, y + rowH - 1, colRight, y + rowH}, kBorder);
        return y + rowH + scale(6);
    }

    StartButton open_;              // (set up in the constructor; it never takes the keyboard)
    StartBrowse browse_;            // the banner's Browse (shown with the banner; a Tab stop)
    bool bannerShown_ = false;
    std::vector<std::unique_ptr<StartRemove>> removes_;
    std::vector<Hit> hits_;         // what each paint put under the mouse (cleared at each paint)
    int hover_ = -1;                // the hit under the mouse at the last move
    int card_ = 0;
    int scroll_ = 0, height_ = 0, blockH_ = 0;
    int shownHeight_ = -1, shownPage_ = -1, shownPos_ = -1;
    bool cardsShown_ = false;
    int cardsEnabled_ = 0, startProjects_ = 0, startMaps_ = 0;
};
