// The Files workspace (portable, core.inc): the map's sound files, to import (WAV, OGG), play, rename and remove; and
// the game's sounds, to listen to, which the briefings can name without the map holding them. The References panel on
// the right lists the briefings that use the selected sound. In an EUD project, also the project's music tracks (to add,
// play, rename and remove) and dot images (to add, rename and remove, with a preview in the dot colors): the lists of
// core/media_model.inc, which the blocks Play music track and Draw dot image name.
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
    // The project's music tracks and dot images: the file dialogs (they answer later: mediaAddTracks, mediaAddImages), and
    // a track's file to play, as togglePreview (`key`: its name for previewPlaying).
    virtual void addMusic() = 0;
    virtual void addDotImages() = 0;
    virtual void toggleFilePreview(const std::string& key, const std::filesystem::path& file) = 0;
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
          addMusicBtn_(L"Add music\x2026", stardraft::kit::Tone::Normal, [this] { svc().addMusic(); }),
          renameMusicBtn_(L"Rename", stardraft::kit::Tone::Quiet, [this] { renameMedia(selected_); }),
          addDotBtn_(L"Add dot images\x2026", stardraft::kit::Tone::Normal, [this] { svc().addDotImages(); }),
          renameDotBtn_(L"Rename", stardraft::kit::Tone::Quiet, [this] { renameMedia(selected_); }) {
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
        addMusicBtn_.setTip(L"OGG, WAV or MP3 files. They go into the project (music\\). Build makes Ogg Vorbis parts of 2.28 seconds.");
        renameMusicBtn_.setTip(L"Rename the music track (F2). The blocks that play it also use the new name.");
        addDotBtn_.setTip(L"Pictures of fewer than 1000 pixels. They go into the project (dots\\) and show in EUD Editor 3's 21 dot colors.");
        renameDotBtn_.setTip(L"Rename the dot image (F2). The blocks that draw it also use the new name.");
        for (stardraft::kit::Button* b : {&addMusicBtn_, &renameMusicBtn_, &addDotBtn_, &renameDotBtn_}) {
            add(*b, {0, 0, 0, 0});
            b->setVisible(false);
        }
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
        draw::Rect r, kindCell, last, play, add, preview;
        std::string name;
        // 0 a file in the map, 1 a game sound the map names, 2 a game sound (the list to add from); 3 a music track, 4 a
        // dot image of the project (`name`: its key, mediaKey; `index`: its place in the list)
        int kind = 0;
        size_t index = 0;
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

    // The project's music tracks and dot images (core/media_model.inc): a row's key ("music:" or "dots:" and its file).
    static std::string mediaKey(bool track, const std::string& file) { return (track ? "music:" : "dots:") + file; }
    // The item of a key: true with its list and place; false when no item has it.
    static bool mediaItem(const std::string& key, bool& track, size_t& index) {
        const stardraft::ProjectMedia& m = projectMedia();
        for (size_t i = 0; i < m.tracks.size(); ++i)
            if (key == mediaKey(true, m.tracks[i].file)) return track = true, index = i, true;
        for (size_t i = 0; i < m.images.size(); ++i)
            if (key == mediaKey(false, m.images[i].file)) return track = false, index = i, true;
        return false;
    }
    void renameMedia(const std::string& key) {
        bool track = false;
        size_t i = 0;
        if (!mediaItem(key, track, i)) return;
        const stardraft::ProjectMedia& m = projectMedia();
        std::wstring name = widen(track ? m.tracks[i].name : m.images[i].name);
        if (!svc().askName(track ? L"Rename the music track" : L"Rename the dot image", name) || name.empty()) return;
        mediaRename(track, i, narrow(name));
        invalidate();
    }
    void removeMedia(const std::string& key) {
        bool track = false;
        size_t i = 0;
        if (!mediaItem(key, track, i)) return;
        if (track && svc().previewPlaying(key)) svc().previewStop();
        if (mediaRemove(track, i) && selected_ == key) selected_.clear();
        invalidate();
    }
    void togglePlayTrack(const std::string& key) {
        bool track = false;
        size_t i = 0;
        if (!mediaItem(key, track, i) || !track) return;
        selected_ = key;
        const std::filesystem::path file = mediaPath(projectMedia().tracks[i].file);
        if (file.empty()) return;
        svc().toggleFilePreview(key, file);
        armPreviewTimer();
        invalidate();
    }

    FilesHost& host_;
    SearchBox search_, mapSearch_;  // (the game's sounds, the map's sounds)
    stardraft::kit::Button importBtn_, renameBtn_;
    stardraft::kit::Button addMusicBtn_, renameMusicBtn_, addDotBtn_, renameDotBtn_;  // (the project's music tracks and dot images)
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

    // The project's music tracks and dot images (an EUD project only).
    if (activeProject()) {
        const stardraft::ProjectMedia& media = projectMedia();
        // Music: the Play button, the name, the kind, the length, the file's size and the remove cross.
        y = kitpaint::drawSectionHeader(c, x0, colRight, y, rowH, L"Music");
        kitpaint::drawTableHeader(c, {x0, y, colRight, y + rowH}, cols);
        y += rowH;
        for (size_t i = 0; i < media.tracks.size(); ++i) {
            const stardraft::MusicTrack& t = media.tracks[i];
            Row rw;
            rw.r = {x0, y, colRight, y + rowH};
            rw.name = mediaKey(true, t.file), rw.kind = 3, rw.index = i;
            y += rowH;
            if (rw.r.bottom < 0 || rw.r.top > h) continue;
            rw.on = rw.name == selected_;
            rw.over = over(rw.r);
            rw.playing = svc().previewPlaying(rw.name);
            std::error_code ec;
            const uintmax_t size = std::filesystem::file_size(mediaPath(t.file), ec);
            rw.missing = bool(ec);
            kitpaint::drawTableRow(c, rw.r, rw.on, rw.over);
            rw.play = kitpaint::square({x0 + scale(8), rw.r.top + scale(1), x0 + scale(8 + stardraft::kit::kIconBox), rw.r.bottom - scale(1)});
            kitpaint::drawIconFace(c, rw.play, rw.playing ? UiIcon::Stop : UiIcon::Play, kitpaint::Tone::Primary, rw.playing, over(rw.play));
            draw::Rect nt = kitpaint::tableCell(rw.r, cols, 1);
            nt.left += scale(4), nt.right -= scale(4);
            c.text(nt, widen(t.name), rw.on ? kAccent : kText, draw::kEllipsis | draw::kMiddle);
            draw::Rect kt = kitpaint::tableCell(rw.r, cols, 2);
            kt.left += scale(4), kt.right -= scale(4);
            rw.kindCell = kt;
            const size_t dot = t.file.rfind('.');  // (the extension from the UTF-8 name: no narrow path conversion)
            std::string ext = dot == std::string::npos || t.file.find('/', dot) != std::string::npos ? std::string() : t.file.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
            c.text(kt, rw.missing ? std::wstring(L"Missing") : widen(ext), rw.missing ? kBad : kDim, draw::kMiddle);
            draw::Rect lt = kitpaint::tableCell(rw.r, cols, 3);
            lt.left += scale(4), lt.right -= scale(4);
            c.text(lt, lengthText(t.ms), kDim, draw::kRight | draw::kMiddle);
            draw::Rect st = kitpaint::tableCell(rw.r, cols, 4);
            st.left += scale(4), st.right -= scale(4);
            c.text(st, rw.missing ? std::wstring(L"\x2014") : sizeText(size_t(size)), kDim, draw::kRight | draw::kMiddle);
            rw.last = kitpaint::square({cEnd, rw.r.top, colRight, rw.r.bottom});
            if (rw.over) kitpaint::drawIconFace(c, rw.last, UiIcon::Close, kitpaint::Tone::Danger, false, over(rw.last));
            rows_.push_back(rw);
        }
        if (media.tracks.empty()) {
            c.text({x0 + scale(50), y, colRight, y + rowH}, L"None", kDim, draw::kMiddle);
            y += rowH;
        }
        y += scale(8);
        bool track = false;
        size_t at = 0;
        const bool selTrack = mediaItem(selected_, track, at) && track;
        {
            const int bh = std::min(rowH, scale(24)), top = y + (rowH - bh) / 2;
            const int ax = x0 + c.measure(L"Add music\x2026") + scale(20);
            place(addMusicBtn_, {x0, top, ax, top + bh});
            if (selTrack) place(renameMusicBtn_, {ax + scale(8), top, ax + scale(8) + c.measure(L"Rename") + scale(20), top + bh});
            else hide(renameMusicBtn_);
        }
        y += rowH + scale(18);

        // Dot images: the picture in the dot colors, the name, the size and the remove cross.
        const std::vector<kitpaint::Column> dotCols = {{L"", 64, draw::kLeft}, {L"Name", 0, draw::kLeft}, {L"Size", 100, draw::kRight}, {L"", 30, draw::kLeft}};
        const int dotEnd = kitpaint::tableCell({x0, 0, colRight, 1}, dotCols, 3).left;
        const int dotH = scale(44);
        y = kitpaint::drawSectionHeader(c, x0, colRight, y, rowH, L"Dot images");
        kitpaint::drawTableHeader(c, {x0, y, colRight, y + rowH}, dotCols);
        y += rowH;
        for (size_t i = 0; i < media.images.size(); ++i) {
            const stardraft::DotImage& d = media.images[i];
            Row rw;
            rw.r = {x0, y, colRight, y + dotH};
            rw.name = mediaKey(false, d.file), rw.kind = 4, rw.index = i;
            y += dotH;
            if (rw.r.bottom < 0 || rw.r.top > h) continue;
            rw.on = rw.name == selected_;
            rw.over = over(rw.r);
            kitpaint::drawTableRow(c, rw.r, rw.on, rw.over);
            const DotPreview* pv = mediaDotPreview(d.file);
            rw.missing = !pv;
            draw::Rect pc = kitpaint::tableCell(rw.r, dotCols, 0);
            pc = {pc.left + scale(8), pc.top + scale(4), pc.right - scale(4), pc.bottom - scale(4)};
            rw.preview = pc;
            if (pv && pv->width > 0 && pv->height > 0) {
                // Each dot a square of the cell's size; white is the background (the drawing has no unit there).
                const double cell = std::min(double(pc.width()) / pv->width, double(pc.height()) / pv->height);
                const int ox = pc.left + int((pc.width() - cell * pv->width) / 2), oy = pc.top + int((pc.height() - cell * pv->height) / 2);
                c.fill({ox, oy, ox + int(cell * pv->width + 0.5), oy + int(cell * pv->height + 0.5)}, kPanel);
                for (int py = 0; py < pv->height; ++py)
                    for (int px = 0; px < pv->width; ++px) {
                        const uint8_t ci = pv->colors[size_t(py) * size_t(pv->width) + size_t(px)];
                        if (ci == 11) continue;  // (white: the background)
                        const uint32_t rgb = stardraft::kDotColors[ci];
                        c.fill({ox + int(px * cell), oy + int(py * cell), ox + int((px + 1) * cell + 0.5), oy + int((py + 1) * cell + 0.5)},
                               RGB(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF));
                    }
            }
            draw::Rect nt = kitpaint::tableCell(rw.r, dotCols, 1);
            nt.left += scale(4), nt.right -= scale(4);
            c.text(nt, widen(d.name), rw.on ? kAccent : kText, draw::kEllipsis | draw::kMiddle);
            draw::Rect st = kitpaint::tableCell(rw.r, dotCols, 2);
            st.left += scale(4), st.right -= scale(4);
            rw.kindCell = st;
            c.text(st, rw.missing ? std::wstring(L"Missing") : std::to_wstring(pv->width) + L" x " + std::to_wstring(pv->height), rw.missing ? kBad : kDim,
                   draw::kRight | draw::kMiddle);
            rw.last = kitpaint::square({dotEnd, rw.r.top + (dotH - rowH) / 2, colRight, rw.r.top + (dotH + rowH) / 2});
            if (rw.over) kitpaint::drawIconFace(c, rw.last, UiIcon::Close, kitpaint::Tone::Danger, false, over(rw.last));
            rows_.push_back(rw);
        }
        if (media.images.empty()) {
            c.text({x0 + scale(50), y, colRight, y + rowH}, L"None", kDim, draw::kMiddle);
            y += rowH;
        }
        y += scale(8);
        const bool selDot = mediaItem(selected_, track, at) && !track;
        {
            const int bh = std::min(rowH, scale(24)), top = y + (rowH - bh) / 2;
            const int ax = x0 + c.measure(L"Add dot images\x2026") + scale(20);
            place(addDotBtn_, {x0, top, ax, top + bh});
            if (selDot) place(renameDotBtn_, {ax + scale(8), top, ax + scale(8) + c.measure(L"Rename") + scale(20), top + bh});
            else hide(renameDotBtn_);
        }
        y += rowH + scale(18);
    } else {
        for (stardraft::kit::Button* b : {&addMusicBtn_, &renameMusicBtn_, &addDotBtn_, &renameDotBtn_}) hide(*b);
    }

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
            if (rw.kind >= 3) {  // a music track or a dot image of the project
                if (rw.kind == 3 && rw.play.contains(e.pt)) togglePlayTrack(rw.name);
                else if (rw.over && rw.last.contains(e.pt)) removeMedia(rw.name);
                else selected_ = rw.name, invalidate();
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
        if (rw && rw->kind == 4) {  // a dot image: rename it (it has nothing to play)
            const std::string key = rw->name;
            renameMedia(key);
            return true;
        }
        if (rw && e.pt.x > rw->r.left + scale(8 + stardraft::kit::kIconBox)) {
            const std::string name = rw->name;
            if (rw->kind == 3) togglePlayTrack(name);
            else togglePlay(name);
            return true;
        }
        return false;
    }
    default: return false;
    }
}

bool FilesPage::key(const draw::KeyEvent& e) {
    if (!e.down || selected_.empty() || typing()) return false;
    bool track = false;
    size_t at = 0;
    const bool media = mediaItem(selected_, track, at);
    if (media && e.key == draw::Key::F2) {  // F2: rename the selected music track or dot image
        const std::string key = selected_;
        renameMedia(key);
        return true;
    }
    if (media && e.key == draw::Key::Delete) {  // Del: remove it (Undo puts it back)
        const std::string key = selected_;
        removeMedia(key);
        return true;
    }
    if (e.key != draw::Key::Space) return false;
    for (stardraft::kit::Button* b : {&importBtn_, &renameBtn_, &addMusicBtn_, &renameMusicBtn_, &addDotBtn_, &renameDotBtn_})
        if (b->hasFocus()) return false;  // a button takes its own press
    if (media && !track) return false;
    if (media) togglePlayTrack(selected_);
    else togglePlay(selected_);  // Space: the selected sound plays, or stops
    return true;
}

std::wstring FilesPage::tip(draw::Point pt) {
    for (const auto& rw : rows_) {
        if (rw.kind >= 3) {  // the project's music tracks and dot images
            if (rw.missing && rw.kindCell.contains(pt)) return L"The project lists this file, but the file is not in the project folder";
            if (rw.kind == 3 && rw.play.contains(pt)) return rw.playing ? L"Stop" : L"Play";
            if (rw.over && rw.last.contains(pt)) return rw.kind == 3 ? L"Remove the music track (Del)" : L"Remove the dot image (Del)";
            if (rw.kind == 4 && rw.preview.contains(pt)) return L"The picture in EUD Editor 3's dot colors. White is the background: the drawing has no unit there.";
            continue;
        }
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
