// The Files workspace (portable, core.inc): the map's sound files, to import (WAV, OGG), play, rename and remove; and
// the game's sounds, to listen to, which the briefings can name without the map holding them. The References panel on
// the right lists the briefings that use the selected sound.
//
// The page is one view (apps/common/view.h) in a view window; the window's vertical bar is the native one, and the
// window's host sets it from scrollBar(). The program's parts (the audio, the file dialog, the briefings) come in
// through FilesHost: the Win32 program's implementation is in files_view.inc.

struct FilesUse {
    int trig = 0, act = 0;  // the briefing's trigger and action that use the sound (the first one of its players)
    std::wstring text;      // the briefing's players, in words
};

class FilesHost {
public:
    virtual ~FilesHost() = default;
    virtual uint32_t soundLength(const std::string& name) = 0;  // ms; 0 when the sound is not found
    virtual bool previewPlaying(const std::string& name) = 0;
    virtual std::string previewedName() = 0;  // the sound that plays now ("" when none does)
    virtual void togglePreview(const std::string& name) = 0;
    virtual void previewStop() = 0;
    virtual std::vector<std::string> importSounds() = 0;  // the file dialog; the names added to the map
    virtual bool askName(const std::wstring& title, std::wstring& text) = 0;
    virtual std::vector<FilesUse> soundUses(const std::string& name) = 0;
    virtual void showUse(const FilesUse& use) = 0;  // the Briefing workspace, at the action
    virtual void soundsChanged() = 0;               // the other views that show sounds repaint
};

class FilesPage final : public draw::View {
public:
    explicit FilesPage(FilesHost& host)
        : host_(host),
          importBtn_(L"Import sounds\x2026", stardraft::kit::Tone::Normal, [this] { importSounds(); }),
          renameBtn_(L"Rename", stardraft::kit::Tone::Quiet, [this] {
              const std::string from = selected_;
              renameSound(from);
          }) {
        search_.onChange = [this] {
            filter_ = search_.text();
            scroll_ = 0;
            invalidate();
        };
        search_.setPlaceholder(L"Search the game's sounds");
        importBtn_.setTip(L"WAV or OGG files. StarDraft 2 saves them in the map (staredit\\wav\\).");
        renameBtn_.setTip(L"Rename the file in the map. The briefings that play it also use the new name.");
        add(search_, {0, 0, 0, 0});
        add(importBtn_, {0, 0, 0, 0});
        add(renameBtn_, {0, 0, 0, 0});
        importBtn_.setVisible(false);
        renameBtn_.setVisible(false);
    }

    // The program sets the search text and the selection (the self-tests' "files:" steps).
    void setSearch(std::wstring t) { search_.setText(std::move(t)); }
    void setSelected(std::string name) {
        selected_ = std::move(name);
        invalidate();
    }
    const std::string& selected() const { return selected_; }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override { return hitAny(pt) ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    std::wstring tip(draw::Point pt) override;
    bool hoverTip() const override { return true; }
    bool focusable() const override { return true; }
    bool vscroll(int code, int pos, int track, int page) override;
    // The window's vertical bar: the content's last line, the page's height and the position.
    bool scrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, contentH_ - 1), page = height(), pos = scroll_;
        return true;
    }

private:
    // A row of the table as the last paint drew it (the mouse and the tips work from these).
    struct Row {
        draw::Rect r, kindCell, last, play, add;
        std::string name;
        int kind = 0;  // 0 a file in the map, 1 a game sound the map names, 2 a game sound (the list to add from)
        size_t size = 0;
        bool on = false, missing = false, over = false, playing = false, hasCheck = false, canAdd = false;
    };
    struct Side {
        draw::Rect r;
        FilesUse use;
    };
    struct Col {
        const wchar_t* name;
        int width;  // at 100 %; 0: what the others leave
        unsigned align;
    };

    FilesHost& svc() { return host_; }
    static Editor* mapEditor() { return appState().ed.get(); }
    static std::vector<Col> columns() {
        return {{L"", 40, draw::kLeft}, {L"Name", 0, draw::kLeft}, {L"Kind", 80, draw::kLeft}, {L"Length", 80, draw::kRight}, {L"Size", 80, draw::kRight}, {L"", 30, draw::kLeft}};
    }
    static draw::Rect squareOf(draw::Rect r) {
        const int w = r.width(), h = r.height(), s = std::min(w, h);
        const int x = r.left + (w - s) / 2, y = r.top + (h - s) / 2;
        return {x, y, x + s, y + s};
    }
    // Where column k is in a row (the table's cells: 6 px in from the left, 6 px from the right).
    static draw::Rect cellOf(draw::Rect row, const std::vector<Col>& cols, size_t k) {
        int fixed = 0, flexible = 0;
        for (const auto& c : cols) {
            if (c.width) fixed += scale(c.width);
            else ++flexible;
        }
        const int rest = std::max(0, row.width() - scale(12) - fixed);
        int x = row.left + scale(6);
        for (size_t i = 0; i < cols.size(); ++i) {
            const int w = cols[i].width ? scale(cols[i].width) : rest / std::max(1, flexible);
            if (i == k) return {x, row.top, x + w, row.bottom};
            x += w;
        }
        return {x, row.top, x, row.bottom};
    }
    static draw::Font smallFont(draw::Canvas& c) { return c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}); }
    static void tableHeader(draw::Canvas& c, draw::Rect row, const std::vector<Col>& cols) {
        const draw::Font old = c.setFont(smallFont(c));
        for (size_t k = 0; k < cols.size(); ++k) {
            draw::Rect cr = cellOf(row, cols, k);
            cr.left += scale(4), cr.right -= scale(4);
            c.text(cr, cols[k].name, kDim, cols[k].align | draw::kMiddle);
        }
        c.setFont(old);
        c.fill({row.left, row.bottom - 1, row.right, row.bottom}, kBorder);
    }
    // A row: on the page's ground; selected or under the mouse a rounded pill (as the lists and pickers).
    static void tableRow(draw::Canvas& c, draw::Rect row, bool selected, bool over) {
        const draw::Rect pill{row.left + scale(2), row.top + scale(1), row.right - scale(2), row.bottom - scale(1)};
        if (selected) c.roundBox(pill, kSel, kSel, float(scale(6)));
        else if (over) c.roundBox(pill, kRowHot, kRowHot, float(scale(6)));
    }
    // A bare icon button's face (ui_kit.inc's iconFace): a fill under the mouse, the glyph in the tone's color.
    static void iconFace(draw::Canvas& c, draw::Rect r, int icon, stardraft::kit::Tone tone, bool on, bool over) {
        using stardraft::kit::Tone;
        if (over) c.roundBox(r, kCtlHot, draw::kNone, float(scale(stardraft::kit::kRadius)));
        const draw::Color rest = tone == Tone::Quiet && on ? kWarn : on || tone == Tone::Primary ? kAccent : kDim;
        const draw::Color lit = tone == Tone::Danger ? kBad : tone == Tone::Quiet && on ? kWarn : tone == Tone::Primary || on ? kAccentBright : kText;
        c.icon(r, icon, over ? lit : rest, scale(16));
    }
    static int sectionHeader(draw::Canvas& c, int x0, int colRight, int y, int rowH, const std::wstring& name) {
        c.text({x0, y, colRight, y + rowH}, name, kText, draw::kMiddle);
        c.fill({x0, y + rowH - 1, colRight, y + rowH}, kBorder);
        return y + rowH + scale(6);
    }
    // The References panel's heading: the title and the count, then a line when nothing uses the sound.
    static int sideHeader(draw::Canvas& c, int x, int y, int w, const std::wstring& title, size_t count) {
        const draw::Rect h{x, y, x + w, y + scale(22)};
        c.text(h, title, kText, draw::kMiddle);
        c.text(h, std::to_wstring(count), kDim, draw::kMiddle | draw::kRight);
        y += scale(30);
        if (!count) {
            c.text({x, y, x + w, y + scale(20)}, L"No other data uses this entry", kDim, draw::kMiddle | draw::kEllipsis);
            y += scale(22);
        }
        return y;
    }
    // Puts a child where it goes, and shows it only where it is on the page (a move that changes nothing repaints nothing).
    void place(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
        const bool on = r.bottom > 0 && r.top < height() && r.right > 0 && r.left < width();
        if (v.visible() != on) v.setVisible(on);
    }
    static void hide(draw::View& v) {
        if (v.visible()) v.setVisible(false);
    }

    const Row* rowAt(draw::Point pt) const {
        for (const auto& r : rows_)
            if (r.r.contains(pt)) return &r;
        return nullptr;
    }
    bool hitAny(draw::Point pt) const {
        for (const auto& r : rows_)
            if (r.r.contains(pt) || r.play.contains(pt) || (r.over && (r.last.contains(pt) || r.add.contains(pt)))) return true;
        for (const auto& s : side_)
            if (s.r.contains(pt)) return true;
        return false;
    }
    // What the pointer is over, for the repaint: a row's index, a briefing link's (10000 + index), or none.
    int hoverKey() const {
        if (!hasPointer_) return -1;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].r.contains(pointer_)) return int(i);
        for (size_t j = 0; j < side_.size(); ++j)
            if (side_[j].r.contains(pointer_)) return 10000 + int(j);
        return -2;
    }
    void setPointer(draw::Point p) {
        const int before = hoverKey();
        pointer_ = p, hasPointer_ = true;
        if (hoverKey() != before) invalidate();
    }
    void clearPointer() {
        if (!hasPointer_) return;
        const int before = hoverKey();
        hasPointer_ = false;
        if (hoverKey() != before) invalidate();
    }
    int maxScroll() const { return std::max(0, contentH_ - height()); }
    void setScroll(int s) {
        const int n = std::clamp(s, 0, maxScroll());
        if (n != scroll_) scroll_ = n, invalidate();
    }

    void importSounds() {
        const auto added = svc().importSounds();
        if (!added.empty()) selected_ = added.front();
        invalidate();
    }
    void togglePlay(const std::string& name) {
        selected_ = name;
        svc().togglePreview(name);
        armPreviewTimer();
        invalidate();
    }
    // The Play button's state follows the sound: the page asks every 200 ms while one plays, then it shows Play again.
    void armPreviewTimer() {
        if (!host()) return;
        host()->timer(200, [this] {
            if (svc().previewPlaying(svc().previewedName())) armPreviewTimer();
            invalidate();
        });
    }
    void renameSound(const std::string& from) {
        std::wstring name = widen(from);
        if (!svc().askName(L"Rename the sound", name) || name.empty()) return;
        const std::string to = narrow(name);
        if (to == from) return;
        // One undo step: the file and the briefings that play it get the new path.
        if (!mapEditor()->renameArchiveFile(from, to)) return setMessage(L"A file of that name is in the map already");
        svc().soundsChanged();
        updateTitle();
        selected_ = to;
        invalidate();
    }
    void removeRow(const Row& rw) {
        svc().previewStop();
        if (rw.kind == 0) mapEditor()->removeArchiveFile(rw.name);
        else mapEditor()->removeSoundReference(rw.name);
        if (selected_ == rw.name) selected_.clear();
        const size_t uses = svc().soundUses(rw.name).size();
        setMessage(uses ? L"Removed the sound. " + std::to_wstring(uses) + L" briefings still use it." : std::wstring(L"Removed the sound"));
        updateTitle();
        invalidate();
    }
    void addReference(const std::string& name) {
        mapEditor()->addSoundReference(name);
        selected_ = name;
        updateTitle();
        invalidate();
    }

    FilesHost& host_;
    SearchBox search_;
    stardraft::kit::Button importBtn_, renameBtn_;
    std::wstring filter_;   // the search text (the game's sounds it keeps)
    std::string selected_;  // a path: a map file or a game sound
    int scroll_ = 0, contentH_ = 0;
    draw::Point pointer_;
    bool hasPointer_ = false;
    std::vector<Row> rows_;   // the rows of the last paint
    std::vector<Side> side_;  // the briefing links of the last paint
};

void FilesPage::paint(draw::Canvas& c) {
    const int w = width(), h = height();
    c.fill({0, 0, w, h}, kBg);
    const int refsW = scale(260), rowH = scale(26);
    const int x0 = scale(18);
    const int colRight = w - refsW - scale(18), colW = colRight - x0;
    const auto cols = columns();
    const int cEnd = cellOf({x0, 0, colRight, 1}, cols, 5).left;
    const bool hovering = hasPointer_;
    const draw::Point hover = pointer_;
    rows_.clear();
    side_.clear();
    int y = scale(12) - scroll_;
    auto over = [&](draw::Rect r) { return hovering && r.contains(hover); };

    // A row of the table: the Play button, the name, the kind, the length, the size and the row's own action.
    auto soundRow = [&](const std::string& name, size_t size, int kind) {
        Row rw;
        rw.r = {x0, y, colRight, y + rowH};
        rw.name = name, rw.kind = kind, rw.size = size;
        if (rw.r.bottom < 0 || rw.r.top > h) return void(y += rowH);  // off the page: nothing to draw or click
        rw.on = name == selected_;
        rw.missing = kind == 1 && !soundFileBytes(name);
        rw.over = over(rw.r);
        rw.playing = svc().previewPlaying(name);
        tableRow(c, rw.r, rw.on, rw.over);

        rw.play = squareOf({x0 + scale(8), y + scale(1), x0 + scale(8 + stardraft::kit::kIconBox), y + rowH - scale(1)});
        iconFace(c, rw.play, int(rw.playing ? UiIcon::Stop : UiIcon::Play), stardraft::kit::Tone::Primary, rw.playing, over(rw.play));

        draw::Rect nt = cellOf(rw.r, cols, 1);
        nt.left += scale(4), nt.right -= scale(4);
        c.text(nt, widen(name), rw.on ? kAccent : kText, draw::kPathEllipsis | draw::kMiddle);

        std::string ext = name.size() >= 3 ? name.substr(name.size() - 3) : name;
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
        draw::Rect kt = cellOf(rw.r, cols, 2);
        kt.left += scale(4), kt.right -= scale(4);
        rw.kindCell = kt;
        // A name the map lists that neither it nor the game holds: missing.
        std::wstring kindText = rw.missing ? std::wstring(L"Missing") : kind == 0 ? widen(ext) : std::wstring(L"Game ") + widen(ext);
        c.text(kt, kindText, rw.missing ? kBad : kDim, draw::kMiddle);

        if (!rw.missing && (kind != 2 || rw.on)) {  // lengths: the map's, and the selected game sound's (reading every game sound takes long)
            draw::Rect lt = cellOf(rw.r, cols, 3);
            lt.left += scale(4), lt.right -= scale(4);
            c.text(lt, lengthText(svc().soundLength(name)), kDim, draw::kRight | draw::kMiddle);
        }
        draw::Rect st = cellOf(rw.r, cols, 4);
        st.left += scale(4), st.right -= scale(4);
        c.text(st, kind == 0 ? sizeText(size) : std::wstring(L"\x2014"), kDim, draw::kRight | draw::kMiddle);

        const draw::Rect last{cEnd, y, colRight, y + rowH};
        rw.last = squareOf(last);
        if (kind == 0 || kind == 1) {  // the remove cross: under the mouse only
            if (rw.over) iconFace(c, rw.last, int(UiIcon::Close), stardraft::kit::Tone::Danger, false, over(rw.last));
        } else {
            const auto refs = mapEditor() ? mapEditor()->soundReferences() : std::vector<std::string>{};
            rw.hasCheck = std::find(refs.begin(), refs.end(), name) != refs.end();
            if (rw.hasCheck) {
                c.icon(last, int(UiIcon::Check), kGood, scale(16));  // in the map already
            } else if (rw.over) {  // Add: under the mouse only, at the row's right edge
                const int bh = std::min(rw.r.height(), scale(stardraft::kit::kIconBox));
                const int top = (rw.r.top + rw.r.bottom - bh) / 2;
                rw.add = squareOf({rw.r.right - scale(4) - scale(stardraft::kit::kIconBox), top, rw.r.right - scale(4), top + bh});
                rw.canAdd = true;
                iconFace(c, rw.add, int(UiIcon::Add), stardraft::kit::Tone::Primary, false, over(rw.add));
            }
        }
        rows_.push_back(rw);
        y += rowH;
    };

    // The map's sounds: its files, and the game sounds it names.
    y = sectionHeader(c, x0, colRight, y, rowH, L"Sounds in the map");
    tableHeader(c, {x0, y, colRight, y + rowH}, cols);
    y += rowH;
    const auto mine = mapSounds();
    for (const auto& n : mine) {
        const ArchiveFile* f = mapEditor() ? mapEditor()->archiveFile(n) : nullptr;
        soundRow(n, f ? f->data.size() : 0, f ? 0 : 1);
    }
    if (mine.empty()) {
        c.text({x0 + scale(50), y, colRight, y + rowH}, L"The map has no sounds of its own. Import sounds adds WAV or OGG files.", kDim, draw::kMiddle | draw::kEllipsis);
        y += rowH;
    }
    y += scale(8);  // the table's end
    {
        const int bh = std::min(rowH, scale(24)), top = y + (rowH - bh) / 2;
        const std::wstring importText = L"Import sounds\x2026";
        const int ax = x0 + c.measure(importText) + scale(20);
        place(importBtn_, {x0, top, ax, top + bh});
        if (!selected_.empty() && mapEditor() && mapEditor()->archiveFile(selected_)) {
            const int rx = ax + scale(8);
            place(renameBtn_, {rx, top, rx + c.measure(L"Rename") + scale(20), top + bh});
        } else {
            hide(renameBtn_);
        }
    }
    y += rowH + scale(18);

    // The game's sounds: the search, then the list the search keeps.
    y = sectionHeader(c, x0, colRight, y, rowH, L"Sounds of the game");
    place(search_, {x0, y, x0 + std::min(scale(320), colW), y + scale(24)});
    y += scale(32);
    tableHeader(c, {x0, y, colRight, y + rowH}, cols);
    y += rowH;
    std::wstring f = filter_;
    std::transform(f.begin(), f.end(), f.begin(), ::towlower);
    int shown = 0;
    for (const auto& g : gameSounds()) {
        if (!f.empty()) {
            std::wstring wg = widen(g);
            std::transform(wg.begin(), wg.end(), wg.begin(), ::towlower);
            if (wg.find(f) == std::wstring::npos) continue;
        }
        soundRow(g, 0, 2);
        ++shown;
    }
    if (!shown) {
        const std::wstring none = f.empty() ? std::wstring(L"StarDraft 2 found no game sounds (Options > Program options: StarCraft folder)") : std::wstring(L"No game sound name contains the search text");
        c.text({x0 + scale(50), y, colRight, y + rowH}, none, kDim, draw::kMiddle | draw::kEllipsis);
        y += rowH;
    }
    y += scale(8);
    contentH_ = y + scroll_ + scale(20);

    // References: what uses the selected sound.
    const draw::Rect side{w - refsW, 0, w, h};
    c.fill(side, kPanel);
    c.fill({side.left, side.top, side.left + 1, side.bottom}, kBorder);
    const int sx = side.left + scale(14), sw = refsW - scale(24);
    const auto uses = svc().soundUses(selected_);
    int ry = sideHeader(c, sx, scale(12), sw, L"References", uses.size());
    if (!uses.empty()) {
        const draw::Font old = c.setFont(smallFont(c));  // the group's heading: small and dim
        c.text({sx, ry, sx + sw, ry + scale(15)}, L"Briefings", kDim, draw::kMiddle | draw::kEllipsis);
        c.setFont(old);
        ry += scale(18);
        for (const auto& u : uses) {
            const draw::Rect r{sx, ry, sx + sw, ry + scale(20)};
            const bool lit = over(r);
            c.text(r, u.text, lit ? kAccentBright : kAccent, draw::kMiddle | draw::kEllipsis);
            if (lit) {
                const int tw = std::min<int>(c.measure(u.text), r.width());
                c.line(sx, r.bottom - scale(3), sx + tw, r.bottom - scale(3), kAccentBright);
            }
            side_.push_back({r, u});
            ry += scale(22);
        }
    }
    // A taller page leaves no scroll to do: the position is kept within the content.
    if (scroll_ > maxScroll()) scroll_ = maxScroll(), invalidate();
}

bool FilesPage::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    switch (e.kind) {
    case MouseKind::Move: setPointer(e.pt); return false;
    case MouseKind::Leave: {  // the pointer may only have gone into a child (a button): the page keeps its hover then
        if (host()) {
            const draw::Point p = host()->mouse(*this);
            if (p.x >= 0 && p.y >= 0 && p.x < width() && p.y < height()) return false;
        }
        clearPointer();
        return false;
    }
    case MouseKind::Wheel:
        setScroll(scroll_ - int(std::lround(e.wheel * scale(26) * 3)));  // three rows a notch
        return true;
    case MouseKind::Down: {
        setPointer(e.pt);
        if (e.button != 0) return false;
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row rw = rows_[i];
            if (!rw.r.contains(e.pt)) continue;
            if (rw.play.contains(e.pt)) {
                togglePlay(rw.name);
            } else if (rw.over && rw.canAdd && rw.add.contains(e.pt)) {
                addReference(rw.name);
            } else if (rw.over && rw.kind != 2 && rw.last.contains(e.pt)) {
                removeRow(rw);
            } else {
                selected_ = rw.name;
                invalidate();
            }
            return true;
        }
        for (size_t j = 0; j < side_.size(); ++j)
            if (side_[j].r.contains(e.pt)) {
                const FilesUse use = side_[j].use;  // (a copy: the page repaints as the briefing opens)
                svc().showUse(use);
                return true;
            }
        return false;
    }
    case MouseKind::DoubleClick: {  // a sound's row (not its Play button): the sound plays; Rename is its button
        const Row* rw = rowAt(e.pt);
        if (rw && e.pt.x > rw->r.left + scale(8 + stardraft::kit::kIconBox)) {
            const std::string name = rw->name;
            togglePlay(name);
            return true;
        }
        return false;
    }
    default: return false;
    }
}

bool FilesPage::key(const draw::KeyEvent& e) {
    if (!e.down || e.key != draw::Key::Space || selected_.empty()) return false;
    togglePlay(selected_);  // Space: the selected sound plays, or stops
    return true;
}

std::wstring FilesPage::tip(draw::Point pt) {
    for (const auto& rw : rows_) {
        if (rw.missing && rw.kindCell.contains(pt)) return L"The map names this sound, but neither the map nor the game has the file";
        if (rw.r.contains(pt) && rw.play.contains(pt)) return rw.playing ? L"Stop" : L"Play";
        if (rw.over && rw.last.contains(pt)) {
            if (rw.kind == 2) return rw.hasCheck ? L"The map has it" : L"";
            return rw.kind == 0 ? L"Remove the file from the map" : L"Remove the reference from the map";
        }
        if (rw.over && rw.canAdd && rw.add.contains(pt))
            return L"Add to the map's sounds, by reference: the game plays its own file, the map does not hold it";
    }
    for (const auto& s : side_)
        if (s.r.contains(pt)) return L"Open it in the Briefing workspace";
    return {};
}

bool FilesPage::vscroll(int code, int pos, int track, int page) {
    // The window's bar (the SB_* requests): a line is a row, a page is the visible height.
    int s = scroll_;
    switch (code) {
    case 0: s -= scale(26); break;        // SB_LINEUP
    case 1: s += scale(26); break;        // SB_LINEDOWN
    case 2: s -= page; break;             // SB_PAGEUP
    case 3: s += page; break;             // SB_PAGEDOWN
    case 4:                               // SB_THUMBPOSITION
    case 5: s = track; break;             // SB_THUMBTRACK
    case 6: s = 0; break;                 // SB_TOP
    case 7: s = 1 << 30; break;           // SB_BOTTOM
    default: (void)pos; return false;
    }
    setScroll(s);
    return true;
}
