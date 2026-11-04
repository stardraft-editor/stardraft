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
        mapSearch_.onChange = [this] {
            mapFilter_ = mapSearch_.text();
            invalidate();
        };
        mapSearch_.setPlaceholder(L"Search the map's sounds");
        importBtn_.setTip(L"WAV or OGG files. They go into the map (staredit\\wav\\).");
        renameBtn_.setTip(L"Rename the file in the map. The briefings that play it also use the new name.");
        add(search_, {0, 0, 0, 0});
        add(mapSearch_, {0, 0, 0, 0});
        add(importBtn_, {0, 0, 0, 0});
        add(renameBtn_, {0, 0, 0, 0});
        importBtn_.setVisible(false);
        renameBtn_.setVisible(false);
    }

    // The program sets the search text and the selection (the self-tests' "files:" steps).
    void setSearch(std::wstring t) { search_.setText(std::move(t)); }
    // The search box has the keyboard: the message loop leaves the typed keys to it (as to an edit control).
    bool typing() { return search_.field().hasFocus() || mapSearch_.field().hasFocus(); }
    // The search text, and how many of the game's sounds the list shows for it (the snapshots' input lines).
    const std::wstring& searchText() const { return filter_; }
    int gameSoundsShown() const {
        const std::wstring f = lowerText(filter_);
        return int(std::count_if(gameSounds().begin(), gameSounds().end(), [&](const std::string& g) { return soundMatches(g, f); }));
    }
    void setSelected(std::string name) {
        selected_ = std::move(name);
        invalidate();
    }
    const std::string& selected() const { return selected_; }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool sameItem(draw::Point a, draw::Point b) const override {  // (a double click: one row)
        const Row *x = rowAt(a), *y = rowAt(b);
        return x == y || (x && y && x->name == y->name);
    }
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override { return hitAny(pt) ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    std::wstring tip(draw::Point pt) override;
    bool hoverTip() const override { return true; }
    bool focusable() const override { return true; }
    bool vscroll(int code, int pos, int track, int page) override;
    // The window's vertical bar: the content's last line, the page's height and the position.
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, contentH_ - 1), page = height(), pos = scroll_;
        return true;
    }

private:
    // The search: a game sound shows when its name has the text (`lower`: lower case), in any case; no text: all.
    static std::wstring lowerText(std::wstring t) {
        std::transform(t.begin(), t.end(), t.begin(), ::towlower);
        return t;
    }
    static bool soundMatches(const std::string& name, const std::wstring& lower) {
        return lower.empty() || lowerText(widen(name)).find(lower) != std::wstring::npos;
    }
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
    FilesHost& svc() { return host_; }
    static Editor* mapEditor() { return appState().ed.get(); }
    static std::vector<kitpaint::Column> columns() {
        return {{L"", 40, draw::kLeft}, {L"Name", 0, draw::kLeft}, {L"Kind", 80, draw::kLeft}, {L"Length", 80, draw::kRight}, {L"Size", 80, draw::kRight}, {L"", 30, draw::kLeft}};
    }
    // Puts a child where it goes, and shows it only where it is on the page (a move that changes nothing repaints nothing).
    void place(draw::View& v, draw::Rect r) {
        v.setFrame(r);
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
    // One timer at a time: a new one cancels the old one, so a burst of Play clicks or key repeats starts one chain.
    void armPreviewTimer() {
        if (!host()) return;
        if (previewTimer_) host()->cancelTimer(previewTimer_);
        previewTimer_ = host()->timer(200, [this] {
            previewTimer_ = 0;
            if (svc().previewPlaying(svc().previewedName())) armPreviewTimer();
            invalidate();
        });
    }
    void renameSound(const std::string& from) {
        if (!mapEditor()) return;  // (no map is open: the sounds are the game's)
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
        if (!mapEditor()) return;  // (no map is open)
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
        if (!mapEditor()) return;  // (no map is open)
        mapEditor()->addSoundReference(name);
        selected_ = name;
        updateTitle();
        invalidate();
    }

    FilesHost& host_;
    SearchBox search_, mapSearch_;  // (the game's sounds, the map's sounds)
    stardraft::kit::Button importBtn_, renameBtn_;
    std::wstring filter_;   // the search text (the game's sounds it keeps)
    std::wstring mapFilter_;  // the map's sounds' search text
    std::string selected_;  // a path: a map file or a game sound
    int scroll_ = 0, contentH_ = 0;
    draw::Point pointer_;
    bool hasPointer_ = false;
    std::vector<Row> rows_;   // the rows of the last paint
    std::vector<Side> side_;  // the briefing links of the last paint
    int previewTimer_ = 0;    // the Play button's timer (armPreviewTimer)
};

extern FilesPage gFilesPage;  // the Files workspace's instance (files_view.inc)

void FilesPage::paint(draw::Canvas& c) {
    const int w = width(), h = height();
    c.fill({0, 0, w, h}, kBg);
    const int refsW = scale(260), rowH = scale(26);
    const int x0 = scale(18);
    const int colRight = w - refsW - scale(18), colW = colRight - x0;
    const auto cols = columns();
    const int cEnd = kitpaint::tableCell({x0, 0, colRight, 1}, cols, 5).left;
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
        kitpaint::drawTableRow(c, rw.r, rw.on, rw.over);

        rw.play = kitpaint::square({x0 + scale(8), y + scale(1), x0 + scale(8 + stardraft::kit::kIconBox), y + rowH - scale(1)});
        kitpaint::drawIconFace(c, rw.play, rw.playing ? UiIcon::Stop : UiIcon::Play, kitpaint::Tone::Primary, rw.playing, over(rw.play));

        draw::Rect nt = kitpaint::tableCell(rw.r, cols, 1);
        nt.left += scale(4), nt.right -= scale(4);
        c.text(nt, widen(name), rw.on ? kAccent : kText, draw::kPathEllipsis | draw::kMiddle);

        std::string ext = name.size() >= 3 ? name.substr(name.size() - 3) : name;
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
        draw::Rect kt = kitpaint::tableCell(rw.r, cols, 2);
        kt.left += scale(4), kt.right -= scale(4);
        rw.kindCell = kt;
        // A name the map lists that neither it nor the game holds: missing.
        std::wstring kindText = rw.missing ? std::wstring(L"Missing") : kind == 0 ? widen(ext) : std::wstring(L"Game ") + widen(ext);
        c.text(kt, kindText, rw.missing ? kBad : kDim, draw::kMiddle);

        if (!rw.missing && (kind != 2 || rw.on)) {  // lengths: the map's, and the selected game sound's (reading every game sound takes long)
            draw::Rect lt = kitpaint::tableCell(rw.r, cols, 3);
            lt.left += scale(4), lt.right -= scale(4);
            c.text(lt, lengthText(svc().soundLength(name)), kDim, draw::kRight | draw::kMiddle);
        }
        draw::Rect st = kitpaint::tableCell(rw.r, cols, 4);
        st.left += scale(4), st.right -= scale(4);
        c.text(st, kind == 0 ? sizeText(size) : std::wstring(L"\x2014"), kDim, draw::kRight | draw::kMiddle);

        const draw::Rect last{cEnd, y, colRight, y + rowH};
        rw.last = kitpaint::square(last);
        if (kind == 0 || kind == 1) {  // the remove cross: under the mouse only
            if (rw.over) kitpaint::drawIconFace(c, rw.last, UiIcon::Close, kitpaint::Tone::Danger, false, over(rw.last));
        } else {
            const auto refs = mapEditor() ? mapEditor()->soundReferences() : std::vector<std::string>{};
            rw.hasCheck = std::find(refs.begin(), refs.end(), name) != refs.end();
            if (rw.hasCheck) {
                c.icon(last, int(UiIcon::Check), kGood, scale(16));  // in the map already
            } else if (rw.over) {  // Add: under the mouse only, at the row's right edge
                const int bh = std::min(rw.r.height(), scale(stardraft::kit::kIconBox));
                const int top = (rw.r.top + rw.r.bottom - bh) / 2;
                rw.add = kitpaint::square({rw.r.right - scale(4) - scale(stardraft::kit::kIconBox), top, rw.r.right - scale(4), top + bh});
                rw.canAdd = true;
                kitpaint::drawIconFace(c, rw.add, UiIcon::Add, kitpaint::Tone::Primary, false, over(rw.add));
            }
        }
        rows_.push_back(rw);
        y += rowH;
    };

    // The map's sounds: its files, and the game sounds it names.
    y = kitpaint::drawSectionHeader(c, x0, colRight, y, rowH, L"Sounds in the map");
    const auto mine = mapSounds();
    if (!mine.empty()) {  // (the search: when the map has sounds)
        place(mapSearch_, {x0, y, x0 + std::min(scale(320), colW), y + scale(24)});
        y += scale(32);
    } else {
        hide(mapSearch_);
    }
    kitpaint::drawTableHeader(c, {x0, y, colRight, y + rowH}, cols);
    y += rowH;
    const std::wstring mf = lowerText(mapFilter_);
    int mineShown = 0;
    for (const auto& n : mine) {
        if (!soundMatches(n, mf)) continue;
        const ArchiveFile* f = mapEditor() ? mapEditor()->archiveFile(n) : nullptr;
        soundRow(n, f ? f->data.size() : 0, f ? 0 : 1);
        ++mineShown;
    }
    if (mine.empty()) {
        c.text({x0 + scale(50), y, colRight, y + rowH}, L"The map has no sounds of its own. Import sounds adds WAV or OGG files.", kDim, draw::kMiddle | draw::kEllipsis);
        y += rowH;
    } else if (!mineShown) {
        c.text({x0 + scale(50), y, colRight, y + rowH}, L"No sound of the map has the search text", kDim, draw::kMiddle | draw::kEllipsis);
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
    y = kitpaint::drawSectionHeader(c, x0, colRight, y, rowH, modTab() ? L"Sounds of Cosmonarchy BW and the game" : L"Sounds of the game");
    place(search_, {x0, y, x0 + std::min(scale(320), colW), y + scale(24)});
    y += scale(32);
    kitpaint::drawTableHeader(c, {x0, y, colRight, y + rowH}, cols);
    y += rowH;
    const std::wstring f = lowerText(filter_);
    int shown = 0;
    for (const auto& g : gameSounds()) {
        if (!soundMatches(g, f)) continue;
        soundRow(g, 0, 2);
        ++shown;
    }
    if (!shown) {
        const std::wstring none = f.empty() ? std::wstring(L"No game sounds found. Select the StarCraft folder in Options > Program options.") : std::wstring(L"No game sound name contains the search text");
        c.text({x0 + scale(50), y, colRight, y + rowH}, none, kDim, draw::kMiddle | draw::kEllipsis);
        y += rowH;
    }
    y += scale(8);
    contentH_ = y + scroll_ + scale(20);

    // References: what uses the selected sound.
    const draw::Rect side{w - refsW, 0, w, h};
    kitpaint::drawSidePanel(c, side);
    const int sx = side.left + scale(14), sw = refsW - scale(24);
    const auto uses = svc().soundUses(selected_);
    int ry = kitpaint::drawSideHeader(c, sx, scale(12), sw, L"References", uses.size());
    if (!uses.empty()) {
        ry = kitpaint::drawSideGroup(c, sx, ry, sw, L"Briefings", true);
        for (const auto& u : uses) {
            const draw::Rect r{sx, ry, sx + sw, ry + scale(20)};
            kitpaint::drawSideLink(c, sx, ry, sw, u.text, {}, over(r));
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
        setScroll(scroll_ - int(std::lround(draw::wheelDistance(e, float(scale(26) * 3)))));  // three rows a notch
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
    if (typing() || importBtn_.hasFocus() || renameBtn_.hasFocus()) return false;  // the field takes the space; a button its own press
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
    (void)pos;
    if (!kitpaint::scrollRequest(code, s, scale(26), page, track)) return false;
    setScroll(s);
    return true;
}
