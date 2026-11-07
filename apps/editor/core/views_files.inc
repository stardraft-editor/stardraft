// The Files workspace (portable, core.inc). Its Sounds tab: the map's sound files, to import (WAV, OGG), play, rename and
// remove; and the game's sounds, to listen to, which the briefings can name without the map holding them. Its All files
// tab: every file of the map, to add, extract, replace, rename and remove, and for each one if File > Export keeps it.
// The References panel on the right lists the briefings that use the selected sound.
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
    virtual void addFiles() = 0;                          // the file dialog: any files (they come in later)
    virtual void extractFile(const std::string& name) = 0;  // the save dialog: the file of the map to a file
    virtual void replaceFile(const std::string& name) = 0;  // the file dialog: the file's new data
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
          }),
          addBtn_(L"Add files\x2026", stardraft::kit::Tone::Normal, [this] { svc().addFiles(); }),
          extractBtn_(L"Extract\x2026", stardraft::kit::Tone::Quiet, [this] { svc().extractFile(selected_); }),
          replaceBtn_(L"Replace\x2026", stardraft::kit::Tone::Quiet, [this] { svc().replaceFile(selected_); }) {
        fileSearch_.onChange = [this] {
            fileFilter_ = fileSearch_.text();
            invalidate();
        };
        fileSearch_.setPlaceholder(L"Search map files");
        addBtn_.setTip(L"Any files. They go into the map with their names; a file of that name is replaced.");
        extractBtn_.setTip(L"Save a copy of the file on the computer");
        replaceBtn_.setTip(L"Use a file of the computer as this file's data. The name stays.");
        add(fileSearch_, {0, 0, 0, 0});
        add(addBtn_, {0, 0, 0, 0});
        add(extractBtn_, {0, 0, 0, 0});
        add(replaceBtn_, {0, 0, 0, 0});
        for (draw::View* v : std::initializer_list<draw::View*>{&fileSearch_, &addBtn_, &extractBtn_, &replaceBtn_}) v->setVisible(false);
        search_.onChange = [this] {
            filter_ = search_.text();
            scroll_ = 0;
            invalidate();
        };
        search_.setPlaceholder(L"Search game sounds");
        mapSearch_.onChange = [this] {
            mapFilter_ = mapSearch_.text();
            invalidate();
        };
        mapSearch_.setPlaceholder(L"Search map sounds");
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
    bool typing() { return search_.field().hasFocus() || mapSearch_.field().hasFocus() || fileSearch_.field().hasFocus(); }
    // The All files tab shows (a dropped file goes into the map whatever it is).
    bool allFiles() const { return tab_ == 1; }
    void setAllFiles(bool on) {
        if ((tab_ == 1) == on) return;
        tab_ = on ? 1 : 0, scroll_ = 0;
        invalidate();
    }
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
        int kind = 0;  // 0 a file in the map, 1 a game sound the map names, 2 a game sound (the list to add from), 3 a row of All files
        size_t size = 0;
        draw::Rect keep;  // (All files: the box of "Keep in export")
        bool canKeep = false, kept = false, own = false;
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
        return tabs_[0].contains(pt) || tabs_[1].contains(pt);
    }
    // What the pointer is over, for the repaint: a row's index, a briefing link's (10000 + index), or none.
    int hoverKey() const {
        if (!hasPointer_) return -1;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].r.contains(pointer_)) return int(i);
        for (size_t j = 0; j < side_.size(); ++j)
            if (side_[j].r.contains(pointer_)) return 10000 + int(j);
        for (int i = 0; i < 2; ++i)
            if (tabs_[i].contains(pointer_)) return 20000 + i;
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
    // StarDraft's own files have their own places (the layers, Notes): not renamed, replaced or removed here.
    static bool ownFile(const std::string& name) { return name.rfind("stardraft\\", 0) == 0; }
    static bool isSound(const std::string& name) {
        std::string ext = name.size() >= 4 ? name.substr(name.size() - 4) : name;
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        return ext == ".wav" || ext == ".ogg";
    }
    static std::wstring fileKind(const std::string& name) {
        if (name == kLayersFile || name == kLayersBaseFile || name == kLayersCheckFile) return L"Layers";
        if (name == kNotesFile) return L"Notes";
        if (name == kMapSettingsFile) return L"Map settings";
        if (name == kExportListFile) return L"Export settings";
        const size_t dot = name.rfind('.');
        std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
        return ext.empty() ? std::wstring(L"File") : widen(ext);
    }
    void toggleKeep(const Row& rw) {
        if (!mapEditor() || !rw.canKeep) return;
        mapEditor()->setKeepInExport(rw.name, !rw.kept);
        selected_ = rw.name;
        if (rw.kept && (mapEditor()->namedInMap(rw.name) || !svc().soundUses(rw.name).empty()))
            setMessage(L"The map uses this file: without it, the exported map cannot play it");
        updateTitle();
        invalidate();
    }
    void paintAllFiles(draw::Canvas& c, int x0, int colRight, int& y, int rowH, int h, const std::function<bool(draw::Rect)>& over);
    void renameSound(const std::string& from) {
        if (!mapEditor()) return;  // (no map is open: the sounds are the game's)
        std::wstring name = widen(from);
        if (!svc().askName(tab_ == 1 ? L"Rename the file" : L"Rename the sound", name) || name.empty()) return;
        const std::string to = narrow(name);
        if (to == from) return;
        std::string low = to;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        if (!archiveNameUsable(to) || low.rfind("stardraft\\", 0) == 0) return setMessage(L"The game or StarDraft uses that name: select another name");
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
        if (rw.kind == 0 || rw.kind == 3) mapEditor()->removeArchiveFile(rw.name);
        else mapEditor()->removeSoundReference(rw.name);
        if (selected_ == rw.name) selected_.clear();
        const size_t uses = svc().soundUses(rw.name).size();
        const std::wstring what = rw.kind == 3 ? L"Removed the file" : L"Removed the sound";
        setMessage(uses ? what + L". " + std::to_wstring(uses) + L" briefings still use it." : what);
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
    SearchBox search_, mapSearch_, fileSearch_;  // (the game's sounds, the map's sounds, All files)
    stardraft::kit::Button importBtn_, renameBtn_, addBtn_, extractBtn_, replaceBtn_;
    int tab_ = 0;                  // 0 Sounds, 1 All files
    draw::Rect tabs_[2];           // the tabs of the last paint
    std::wstring fileFilter_;      // All files' search text
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

    // The tabs: the sounds, and all the files of the map.
    {
        int tx = x0;
        const wchar_t* names[] = {L"Sounds", L"All files"};
        for (int i = 0; i < 2; ++i) {
            const int tw = c.measure(names[i]) + scale(24);
            tabs_[i] = {tx, y, tx + tw, y + rowH};
            c.text(tabs_[i], names[i], tab_ == i ? kAccent : over(tabs_[i]) ? kText : kDim, draw::kMiddle | draw::kCenter);
            if (tab_ == i) c.fill({tabs_[i].left, tabs_[i].bottom - scale(2), tabs_[i].right, tabs_[i].bottom}, kAccent);
            tx += tw;
        }
        c.fill({x0, y + rowH, colRight, y + rowH + 1}, kBorder);
        y += rowH + scale(10);
    }
    if (tab_ == 1) {
        for (draw::View* v : std::initializer_list<draw::View*>{&search_, &mapSearch_, &importBtn_}) hide(*v);
        paintAllFiles(c, x0, colRight, y, rowH, h, over);
        contentH_ = y + scroll_ + scale(20);
        const draw::Rect side{w - refsW, 0, w, h};
        kitpaint::drawSidePanel(c, side);
        const int sx = side.left + scale(14), sw = refsW - scale(24);
        const auto uses = selected_.empty() ? std::vector<FilesUse>{} : svc().soundUses(selected_);
        const bool named = !selected_.empty() && mapEditor() && mapEditor()->namedInMap(selected_);
        int ry = kitpaint::drawSideHeader(c, sx, scale(12), sw, L"References", uses.size() + (named && uses.empty() ? 1 : 0));
        if (!uses.empty()) {
            ry = kitpaint::drawSideGroup(c, sx, ry, sw, L"Briefings", true);
            for (const auto& u : uses) {
                const draw::Rect r{sx, ry, sx + sw, ry + scale(20)};
                kitpaint::drawSideLink(c, sx, ry, sw, u.text, {}, over(r));
                side_.push_back({r, u});
                ry += scale(22);
            }
        } else if (named) {
            c.text({sx, ry, sx + sw, ry + scale(40)}, L"The map's text names this file", kDim, draw::kWrap);
        }
        if (scroll_ > maxScroll()) scroll_ = maxScroll(), invalidate();
        return;
    }
    for (draw::View* v : std::initializer_list<draw::View*>{&fileSearch_, &addBtn_, &extractBtn_, &replaceBtn_}) hide(*v);

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
    y = kitpaint::drawSectionHeader(c, x0, colRight, y, rowH, L"Map sounds");
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
        c.text({x0 + scale(50), y, colRight, y + rowH}, L"No map sound has the search text", kDim, draw::kMiddle | draw::kEllipsis);
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
    y = kitpaint::drawSectionHeader(c, x0, colRight, y, rowH, modTab() ? L"Cosmonarchy BW and game sounds" : L"Game sounds");
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

// All files: the map's files, with the search over them, what each one is, its size and if File > Export keeps it.
void FilesPage::paintAllFiles(draw::Canvas& c, int x0, int colRight, int& y, int rowH, int h, const std::function<bool(draw::Rect)>& over) {
    const std::vector<kitpaint::Column> cols = {{L"", 40, draw::kLeft}, {L"Name", 0, draw::kLeft}, {L"Kind", 110, draw::kLeft}, {L"Size", 80, draw::kRight}, {L"Keep in export", 110, draw::kCenter}, {L"", 30, draw::kLeft}};
    const int colW = colRight - x0;
    place(fileSearch_, {x0, y, x0 + std::min(scale(320), colW), y + scale(24)});
    y += scale(32);
    kitpaint::drawTableHeader(c, {x0, y, colRight, y + rowH}, cols);
    y += rowH;
    Editor* ed = mapEditor();
    const std::wstring f = lowerText(fileFilter_);
    std::vector<const ArchiveFile*> files;
    if (ed)
        for (const ArchiveFile& a : ed->archiveFiles())
            if (soundMatches(a.name, f)) files.push_back(&a);
    std::sort(files.begin(), files.end(), [](const ArchiveFile* a, const ArchiveFile* b) { return a->name < b->name; });
    for (const ArchiveFile* a : files) {
        Row rw;
        rw.r = {x0, y, colRight, y + rowH};
        rw.name = a->name, rw.kind = 3, rw.size = a->data.size();
        if (rw.r.bottom < 0 || rw.r.top > h) {
            y += rowH;
            continue;
        }
        rw.on = a->name == selected_;
        rw.over = over(rw.r);
        rw.own = ownFile(a->name);
        kitpaint::drawTableRow(c, rw.r, rw.on, rw.over);
        if (isSound(a->name)) {
            rw.playing = svc().previewPlaying(a->name);
            rw.play = kitpaint::square({x0 + scale(8), y + scale(1), x0 + scale(8 + stardraft::kit::kIconBox), y + rowH - scale(1)});
            kitpaint::drawIconFace(c, rw.play, rw.playing ? UiIcon::Stop : UiIcon::Play, kitpaint::Tone::Primary, rw.playing, over(rw.play));
        }
        auto cell = [&](int i) {
            draw::Rect t = kitpaint::tableCell(rw.r, cols, i);
            t.left += scale(4), t.right -= scale(4);
            return t;
        };
        c.text(cell(1), widen(a->name), rw.on ? kAccent : kText, draw::kPathEllipsis | draw::kMiddle);
        c.text(cell(2), fileKind(a->name), kDim, draw::kMiddle);
        c.text(cell(3), sizeText(rw.size), kDim, draw::kRight | draw::kMiddle);
        rw.canKeep = Editor::canKeepInExport(a->name);
        rw.kept = ed->keepInExport(a->name);
        const draw::Rect kc = kitpaint::tableCell(rw.r, cols, 4);
        const int bs = scale(14), bx = (kc.left + kc.right - bs) / 2, by = (kc.top + kc.bottom - bs) / 2;
        rw.keep = {bx - scale(4), by - scale(4), bx + bs + scale(4), by + bs + scale(4)};
        if (rw.canKeep) kitpaint::drawCheckMark(c, {bx, by, bx + bs, by + bs}, rw.kept, over(rw.keep), true);
        else c.text(kc, L"\x2014", kFaint, draw::kCenter | draw::kMiddle);
        rw.last = kitpaint::square(kitpaint::tableCell(rw.r, cols, 5));
        if (rw.over && !rw.own) kitpaint::drawIconFace(c, rw.last, UiIcon::Close, kitpaint::Tone::Danger, false, over(rw.last));
        rows_.push_back(rw);
        y += rowH;
    }
    if (files.empty()) {
        c.text({x0 + scale(50), y, colRight, y + rowH}, fileFilter_.empty() ? L"The map has no files other than its terrain and triggers. Add files puts files into it." : L"No file has the search text", kDim, draw::kMiddle | draw::kEllipsis);
        y += rowH;
    }
    y += scale(8);
    const int bh = std::min(rowH, scale(24)), top = y + (rowH - bh) / 2;
    int bx = x0;
    const auto button = [&](stardraft::kit::Button& b, const wchar_t* text, bool on) {
        if (!on) return hide(b);
        const int bw = c.measure(text) + scale(20);
        place(b, {bx, top, bx + bw, top + bh});
        bx += bw + scale(8);
    };
    const bool has = !selected_.empty() && ed && ed->archiveFile(selected_);
    const bool mine = has && !ownFile(selected_);
    button(addBtn_, L"Add files\x2026", true);
    button(extractBtn_, L"Extract\x2026", has);
    button(replaceBtn_, L"Replace\x2026", mine);
    button(renameBtn_, L"Rename", mine);
    y += rowH + scale(10);
    if (ed && !ed->unreadableFiles().empty()) {
        const size_t n = ed->unreadableFiles().size();
        const std::wstring note = n == 1 ? L"1 file of the archive has no name that can be read, or could not be read. A save leaves it out."
                                         : std::to_wstring(n) + L" files of the archive have no name that can be read, or could not be read. A save leaves them out.";
        c.text({x0, y, colRight, y + scale(40)}, note, kDim, draw::kWrap);
        y += scale(40);
    }
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
        for (int i = 0; i < 2; ++i)
            if (tabs_[i].contains(e.pt)) {
                setAllFiles(i == 1);
                return true;
            }
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row rw = rows_[i];
            if (!rw.r.contains(e.pt)) continue;
            if (rw.kind == 3 && rw.keep.contains(e.pt)) {
                toggleKeep(rw);
            } else if (rw.kind == 3 && rw.own && rw.last.contains(e.pt)) {
                selected_ = rw.name;
                invalidate();
            } else if (rw.play.contains(e.pt)) {
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
        if (rw && e.pt.x > rw->r.left + scale(8 + stardraft::kit::kIconBox) && (rw->kind != 3 || (isSound(rw->name) && !rw->keep.contains(e.pt)))) {
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
    if (typing() || importBtn_.hasFocus() || renameBtn_.hasFocus() || addBtn_.hasFocus() || extractBtn_.hasFocus() || replaceBtn_.hasFocus()) return false;  // the field takes the space; a button its own press
    if (tab_ == 1 && !isSound(selected_)) return false;
    togglePlay(selected_);  // Space: the selected sound plays, or stops
    return true;
}

std::wstring FilesPage::tip(draw::Point pt) {
    for (const auto& rw : rows_) {
        if (rw.missing && rw.kindCell.contains(pt)) return L"The map names this sound, but neither the map nor the game has the file";
        if (rw.r.contains(pt) && rw.play.contains(pt)) return rw.playing ? L"Stop" : L"Play";
        if (rw.kind == 3 && rw.r.contains(pt) && rw.keep.contains(pt))
            return rw.canKeep ? L"File > Export keeps this file in the exported map" : L"File > Export never keeps this file: the exported map has no layers";
        if (rw.kind == 3 && rw.own && rw.over && rw.last.contains(pt)) return L"";
        if (rw.over && rw.last.contains(pt)) {
            if (rw.kind == 2) return rw.hasCheck ? L"The map has it" : L"";
            return rw.kind == 0 ? L"Remove the file from the map" : L"Remove the reference from the map";
        }
        if (rw.over && rw.canAdd && rw.add.contains(pt))
            return L"Add to the map's sounds, by reference: the game plays its own file, the map does not hold it";
    }
    for (const auto& s : side_)
        if (s.r.contains(pt)) return L"Open it in the Briefing workspace";
    if (tabs_[1].contains(pt)) return L"All the files of the map: add, extract, replace, rename, remove, and what File > Export keeps";
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
