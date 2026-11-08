// Data > Strings of a classic map (portable, core.inc): the map's string table (STR or STRx) as a list. Each row has the
// string's id, its text in one line and how many fields of the map use it. The selected string shows in the game's look
// and in the game text editor (core/views_game_text.inc, as the Data workspace's Strings tab of a project); its uses are
// in the References sidebar at the right (DataRefsPage, the Data workspace's panel). The list's buttons add a string,
// delete a string that nothing uses, and move a string up or down; a drag of a row moves it too (Alt+Up, Alt+Down).
// A move gives the string another id: every use in the map follows it (Editor::moveMapString, strings.h:
// renumberMapStrings), one undo step. The uses and the renumbering read the same list of fields (mapStringFields).
// The program's part (sdl_map_strings.inc) shows the view in the Data page's Strings tab and opens a use's page.

// The tab's state while the program runs (the page's views are made again after an undo).
struct MapStringsState {
    uint32_t selected = 0;  // the string shown (0: the first one)
    std::wstring query;
    int filter = 0;  // kMapStringsAll, ...
    int scrollTop = 0;
};
inline MapStringsState gMapStrings;
enum { kMapStringsAll, kMapStringsUsed, kMapStringsUnused, kMapStringsSame };
// The program's: a link of the References sidebar is clicked (the use's page opens).
inline std::function<void(const StringUse&)> gMapStringGoTo;

// --- The model: the rows and the names of the uses ----------------------------------------------------------------

// A row of the list: a string with a text or a use.
struct MapStringRow {
    uint32_t id = 0;
    int uses = 0;
    uint32_t sameAs = 0;  // the first string with the same text (0: none)
};

// A string's text in one line, for the list and the search (its line ends as a mark, without its codes).
inline std::wstring mapStringLine(const std::string& raw) { return gameTextPlain(raw); }

// The name of a briefing action's type (briefing.h: BriefingAction::Type).
inline std::wstring mapStringBriefingType(uint8_t type) {
    static const wchar_t* names[] = {L"None", L"Wait", L"Play WAV", L"Text message", L"Mission objectives", L"Show portrait", L"Hide portrait",
                                     L"Display speaking portrait", L"Transmission", L"Skip tutorial enabled"};
    return type < 10 ? names[type] : L"Action " + std::to_wstring(type);
}

// What a use is, as the References sidebar names it: its group and its own name.
inline std::wstring mapStringUseGroup(StringUseKind k) {
    switch (k) {
    case StringUseKind::ScenarioName:
    case StringUseKind::ScenarioDescription: return L"Map properties";
    case StringUseKind::ForceName: return L"Forces";
    case StringUseKind::UnitName: return L"Unit names";
    case StringUseKind::LocationName: return L"Locations";
    case StringUseKind::SwitchName: return L"Switches";
    case StringUseKind::SoundFile: return L"Sound files";
    case StringUseKind::TriggerText:
    case StringUseKind::TriggerSound: return L"Triggers";
    case StringUseKind::BriefingText:
    case StringUseKind::BriefingSound: return L"Briefings";
    }
    return L"";
}
inline UiIcon mapStringUseIcon(StringUseKind k) {
    switch (k) {
    case StringUseKind::ScenarioName:
    case StringUseKind::ScenarioDescription:
    case StringUseKind::ForceName: return UiIcon::Tag;
    case StringUseKind::UnitName: return UiIcon::DatUnits;
    case StringUseKind::LocationName: return UiIcon::Locations;
    case StringUseKind::SoundFile: return UiIcon::Sound;
    case StringUseKind::BriefingText:
    case StringUseKind::BriefingSound: return UiIcon::Briefing;
    default: return UiIcon::Triggers;
    }
}
inline std::wstring mapStringUseName(const Editor& ed, const StringUse& u, const ClassicTriggers& trig, const std::vector<BriefingTrigger>& brief) {
    const std::wstring n = std::to_wstring(u.index + 1), a = std::to_wstring(u.action + 1);
    switch (u.kind) {
    case StringUseKind::ScenarioName: return L"Scenario name";
    case StringUseKind::ScenarioDescription: return L"Scenario description";
    case StringUseKind::ForceName: return L"Force " + n;
    case StringUseKind::UnitName: return unitDisplayName(u.index);
    case StringUseKind::LocationName: return L"Location " + n;
    case StringUseKind::SwitchName: return L"Switch " + n;
    case StringUseKind::SoundFile: return L"Sound file " + n;
    case StringUseKind::TriggerText:
    case StringUseKind::TriggerSound: {
        std::wstring type;
        if (u.index < int(trig.triggers.size()) && u.action < int(trig.triggers[size_t(u.index)].actions.size()))
            if (const ClassicDef* d = classicActionDef(trig.triggers[size_t(u.index)].actions[size_t(u.action)].type)) type = widen(d->name);
        return L"Trigger " + n + L", action " + a + (type.empty() ? L"" : L" (" + type + L")") + (u.kind == StringUseKind::TriggerSound ? L", sound" : L"");
    }
    case StringUseKind::BriefingText:
    case StringUseKind::BriefingSound: {
        (void)ed;
        std::wstring type;
        // (readBriefings leaves out the None actions: the action's type is the n-th action that is not None)
        if (u.index < int(brief.size())) {
            const Bytes m = ed.chk().effective("MBRF");
            const size_t at = size_t(u.index) * 2400 + 320 + size_t(u.action) * 32 + 26;
            if (at < m.size()) type = mapStringBriefingType(m[at]);
        }
        return L"Briefing " + n + L", action " + a + (type.empty() ? L"" : L" (" + type + L")") + (u.kind == StringUseKind::BriefingSound ? L", sound" : L"");
    }
    }
    return L"";
}

// The list's rows for the map's strings: each string with a text or a use (string 0 is none), in id order; a filter
// and a search (words of the text, or the id) leave rows out. String `keep` has a row also when it is empty and not used
// (the string being edited stays in the list when its text is deleted).
inline std::vector<MapStringRow> mapStringRows(const Editor& ed, const std::vector<StringUse>& uses, int filter, const std::wstring& query,
                                               uint32_t keep = 0) {
    const StringTable& t = ed.strings();
    std::vector<int> count(t.size() + 1, 0);
    for (const StringUse& u : uses)
        if (u.id < count.size()) ++count[u.id];
    std::map<std::string, uint32_t> first;
    std::wstring q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::towlower);
    std::vector<MapStringRow> out;
    for (uint32_t id = 1; id <= t.size(); ++id) {
        const std::string& s = t.get(id);
        if (s.empty() && !count[id] && id != keep) continue;
        MapStringRow r{id, count[id], 0};
        if (!s.empty()) {
            const auto [it, made] = first.emplace(s, id);
            if (!made) r.sameAs = it->second;
        }
        out.push_back(r);
    }
    // (the first of a text with copies is "same text" too: it shows with them)
    std::set<uint32_t> copied;
    for (const MapStringRow& r : out)
        if (r.sameAs) copied.insert(r.sameAs);
    std::vector<MapStringRow> shown;
    for (const MapStringRow& r : out) {
        if (filter == kMapStringsUsed && !r.uses) continue;
        if (r.id == keep) {  // (the string being edited: in every filter and search)
            shown.push_back(r);
            continue;
        }
        if (filter == kMapStringsUnused && r.uses) continue;
        if (filter == kMapStringsSame && !r.sameAs && !copied.count(r.id)) continue;
        if (!q.empty()) {
            std::wstring hay = mapStringLine(t.get(r.id));
            std::transform(hay.begin(), hay.end(), hay.begin(), ::towlower);
            const std::wstring num = std::to_wstring(r.id);
            if (hay.find(q) == std::wstring::npos && q != num && q != L"#" + num) continue;
        }
        shown.push_back(r);
    }
    return shown;
}

// --- The list: kit's list, with the drag of a row ------------------------------------------------------------------

class MapStringList final : public stardraft::kit::ListView {
public:
    // A row is dragged from row `from` to the place before row `slot` (slot == count(): after the last row).
    std::function<void(int from, int slot)> onDrop;
    std::function<void(int delta)> onMove;  // Alt+Up (-1), Alt+Down (1)
    std::function<void()> onDelete;         // Delete

    bool mouse(const draw::MouseEvent& e) override {
        using draw::MouseKind;
        if (e.kind == MouseKind::Down && e.button == 0 && !e.mods) {
            const bool r = ListView::mouse(e);  // (the row is selected first)
            if (const int row = rowAt(e.pt); row >= 0) drag_.press(*this, row, e.pt);
            return r;
        }
        if (e.kind == MouseKind::Move && drag_.item >= 0) {
            if (drag_.move(*this, e.pt)) {
                if (e.pt.y < scale(12)) scrollBy(-rowHeight() / 2);
                else if (e.pt.y > height() - scale(12)) scrollBy(rowHeight() / 2);
                const int s = slotAt(e.pt.y);
                if (s != slot_) slot_ = s, invalidate();
                return true;
            }
        }
        if (e.kind == MouseKind::Up && e.button == 0 && drag_.dragging()) {
            const int slot = slot_, from = drag_.release(*this, e.pt);
            slot_ = -1, invalidate();
            if (from >= 0 && slot >= 0 && slot != from && slot != from + 1 && onDrop) onDrop(from, slot);
            return true;
        }
        if (e.kind == MouseKind::Up) drag_.cancel(*this);  // (a click: no drag)
        return ListView::mouse(e);
    }
    void captureLost() override {
        if (drag_.armed()) drag_.cancel(*this), slot_ = -1, invalidate();
    }
    bool key(const draw::KeyEvent& e) override {
        if (e.down && (e.mods & draw::kAlt) && (e.key == draw::Key::Up || e.key == draw::Key::Down)) {
            if (onMove) onMove(e.key == draw::Key::Up ? -1 : 1);
            return true;
        }
        if (e.down && !e.mods && e.key == draw::Key::Delete && onDelete) return onDelete(), true;
        return ListView::key(e);
    }
    void paint(draw::Canvas& c) override {
        ListView::paint(c);
        if (!drag_.dragging() || slot_ < 0 || !count()) return;
        // The line where the row goes: between two rows (over the first, under the last).
        const int y = slot_ < count() ? rowRect(slot_).top : rowRect(count() - 1).bottom;
        kitpaint::drawDropLine(c, scale(4), width() - scale(4), y);
    }
    bool dragging() const { return drag_.dragging(); }

private:
    int slotAt(int y) const {
        if (!count()) return -1;
        const int top = rowRect(0).top, h = std::max(1, rowRect(0).bottom - top);
        return std::clamp((y - top + h / 2) / h, 0, count());
    }
    kitui::ItemDrag drag_;  // (a row's drag: its index)
    int slot_ = -1;
};

// --- The view -------------------------------------------------------------------------------------------------------

class MapStringsView final : public draw::View, private DataRefsHost {
public:
    MapStringsView()
        : add_(L"New string", stardraft::kit::Tone::Normal, [this] { addString(); }),
          delete_(L"Delete", stardraft::kit::Tone::Normal, [this] { deleteString(); }),
          up_(L"Move up", stardraft::kit::Tone::Normal, [this] { move(-1); }),
          down_(L"Move down", stardraft::kit::Tone::Normal, [this] { move(1); }),
          filter_({{L"All strings"}, {L"Used"}, {L"Not used"}, {L"Same text as another"}}, gMapStrings.filter),
          editor_(textSpec()),
          refs_(*this) {
        search_.setPlaceholder(L"Search strings");
        search_.setText(gMapStrings.query);
        search_.onChange = [this] {
            gMapStrings.query = search_.text();
            fillList();
        };
        filter_.onChange = [this](int v) {
            gMapStrings.filter = v;
            fillList();
        };
        add_.setTip(L"Add a string to the table");
        delete_.setTip(L"Delete the string when the map does not use it (Del)");
        up_.setTip(L"Give the string the id before it and change each use of it (Alt+Up)");
        down_.setTip(L"Give the string the id after it and change each use of it (Alt+Down)");
        list_.setRowHeight(28);
        list_.label = [this](int i) { return mapStringLine(strings().get(rows_[size_t(i)].id)); };
        list_.paintRow = [this](draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) { paintRow(c, i, r, s); };
        list_.onSelect = [this] {
            const std::vector<int> sel = list_.selection();
            if (!sel.empty() && sel[0] >= 0 && sel[0] < int(rows_.size())) select(rows_[size_t(sel[0])].id);
        };
        list_.onActivate = [this](int) { editor_.focusText(); };
        list_.onDrop = [this](int from, int slot) { drop(from, slot); };
        list_.onMove = [this](int d) { move(d); };
        list_.onDelete = [this] { deleteString(); };
        list_.setEmptyHint(L"No strings");
        editor_.onChange = [this] { typed(); };
        editor_.onFocusLost = [this] { commitText(); };
        editor_.setColors(draw::rgb(184, 184, 232), draw::rgb(0x11, 0x11, 0x11));
        add(search_, {0, 0, 0, 0});
        add(filter_, {0, 0, 0, 0});
        add(list_, {0, 0, 0, 0});
        for (stardraft::kit::Button* b : {&add_, &delete_, &up_, &down_}) add(*b, {0, 0, 0, 0});
        add(editor_, {0, 0, 0, 0});
        add(refs_, {0, 0, 0, 0});
    }
    ~MapStringsView() override { commitText(); }

    // The map changed (an undo, another page): the rows again, and the editor's text when it has no edit.
    void refresh() {
        const Editor* ed = editor();
        if (!ed) return;
        if (ed->revision() == revision_) return;
        revision_ = ed->revision();
        wasExtended_ = ed->strings().extended();
        uses_ = ed->mapStringUses();
        trig_ = ed->classicTriggers();
        brief_ = ed->briefings();
        fillList();
    }

    void resized() override { layout(); }
    void paint(draw::Canvas& c) override {
        refresh();
        layout();
        c.fill({0, 0, width(), height()}, kBg);
        const Editor* ed = editor();
        if (!ed) return;
        const Layout l = places();
        const uint32_t id = gMapStrings.selected;
        const std::string& raw = strings().get(id);
        // The title: the string's id.
        const draw::Font of = c.setFont(c.font(kitpaint::titleFontDesc()));
        c.text({l.x0, l.pad, l.x1, l.pad + scale(30)}, id ? L"String " + std::to_wstring(id) : std::wstring(L"No string"), kText, draw::kEllipsis);
        c.setFont(of);
        if (!id) return;
        // The game's look of the text (with the editor's text while it has an edit).
        const std::string shown = dirty_ ? editor_.bytes() : raw;
        kitpaint::drawSectionHeader(c, l.x0, l.x1, l.previewTop - scale(30), scale(30), L"Game preview");
        c.fill(l.preview, draw::rgb(8, 8, 10));
        c.roundBox(l.preview, draw::kNone, draw::rgb(60, 64, 70), float(scale(4)));
        c.pushClip(l.preview);
        drawGameText(c, {l.preview.left + scale(10), l.preview.top + scale(8), l.preview.right - scale(10), l.preview.bottom}, shown, draw::rgb(208, 208, 208));
        c.popClip();
        kitpaint::drawSectionHeader(c, l.x0, l.x1, l.editorTop - scale(30), scale(30), L"Text");
        // Under the editor: the uses (the sidebar lists them), a copy's first string, the table's size.
        int ty = l.infoTop;
        auto line = [&](const std::wstring& t, draw::Color col) {
            c.text({l.x0, ty, l.x1, ty + scale(22)}, t, col, draw::kMiddle | draw::kEllipsis);
            ty += scale(24);
        };
        const MapStringRow* row = rowOf(id);
        const int uses = row ? row->uses : 0;
        line(uses ? L"Used " + std::to_wstring(uses) + (uses == 1 ? L" time" : L" times") : std::wstring(L"Not used"), uses ? kText : kDim);
        if (row && row->sameAs) line(L"Same text as string " + std::to_wstring(row->sameAs), kDim);
        const size_t bytes = strings().strBytes();
        line(strings().extended() || bytes > 0xFFFF ? L"All strings: " + std::to_wstring(bytes) + L" bytes in STRx (StarCraft: Remastered)"
                                                    : L"All strings: " + std::to_wstring(bytes) + L" of 65535 bytes in STR",
             kDim);
    }
    void paintOver(draw::Canvas& c) override {
        c.roundBox(list_.frame(), draw::kNone, kBorder, float(scale(stardraft::kit::kRadius)), scale(1) + 1);
    }
    bool mouse(const draw::MouseEvent& e) override {
        return e.kind == draw::MouseKind::Up && e.button == 1 && list_.frame().contains(e.pt);
    }

    // The string to show (a link from another page; a test).
    void showString(uint32_t id) {
        commitText();
        select(id);
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == id) list_.selectOnly(int(i)), list_.ensureVisible(int(i));
    }
    MapStringList& list() { return list_; }
    // The editor's text goes into the map now (a Save, Undo, a quit: core typingFlush).
    void flush() { commitText(); }

private:
    struct Layout {
        int pad = 0, listW = 0, refsW = 0, x0 = 0, x1 = 0, previewTop = 0, editorTop = 0, infoTop = 0;
        draw::Rect preview, editor, refs;
    };
    static GameTextSpec textSpec() {
        GameTextSpec s;
        s.palette = TextPalette::Game, s.multiline = true, s.align = true, s.crlf = true;  // (the map's lines end with CR LF)
        return s;
    }
    Layout places() {
        Layout l;
        l.pad = scale(stardraft::kit::kDialogPad);
        l.listW = std::clamp(width() / 3, scale(280), scale(420));
        l.refsW = scale(260);
        l.x0 = l.pad + l.listW + l.pad * 2, l.x1 = std::max(l.x0 + scale(200), width() - l.refsW - l.pad * 2);
        l.previewTop = l.pad + scale(30) + scale(36);
        const std::string& raw = strings().get(gMapStrings.selected);
        const int lines = 1 + int(std::count(raw.begin(), raw.end(), '\n'));
        l.preview = {l.x0, l.previewTop, l.x1, l.previewTop + std::clamp(lines * scale(20) + scale(16), scale(60), scale(220))};
        l.editorTop = l.preview.bottom + scale(36);
        const int eh = std::max(scale(150), std::min(scale(260), height() - l.editorTop - scale(110)));
        l.editor = {l.x0, l.editorTop, l.x1, l.editorTop + eh};
        l.infoTop = l.editor.bottom + scale(10);
        l.refs = {width() - l.refsW, 0, width(), height()};
        return l;
    }
    void layout() {
        const Layout l = places();
        const int fh = scale(stardraft::kit::kFieldH), bh = scale(stardraft::kit::kButtonH), gap = scale(8);
        const int fw = scale(150);
        search_.setFrame({l.pad, l.pad, l.pad + l.listW - fw - gap, l.pad + fh});
        filter_.setFrame({l.pad + l.listW - fw, l.pad, l.pad + l.listW, l.pad + fh});
        const int listTop = l.pad + fh + gap, buttonsTop = height() - l.pad - bh;
        list_.setFrame({l.pad, listTop, l.pad + l.listW, std::max(listTop + scale(60), buttonsTop - gap)});
        const int bw = (l.listW - gap * 3) / 4;
        int x = l.pad;
        for (stardraft::kit::Button* b : {&add_, &delete_, &up_, &down_}) {
            b->setFrame({x, buttonsTop, x + bw, buttonsTop + bh});
            x += bw + gap;
        }
        editor_.setFrame(l.editor);
        editor_.setVisible(gMapStrings.selected != 0);
        refs_.setFrame(l.refs);
        if (scrollTo_ >= 0 && list_.height() > 0) list_.ensureVisible(scrollTo_), scrollTo_ = -1;
    }

    Editor* editor() const { return shellState().ed && shellState().ed->isOpen() ? shellState().ed.get() : nullptr; }
    const StringTable& strings() const {
        static const StringTable none;
        const Editor* ed = editor();
        return ed ? ed->strings() : none;
    }
    const MapStringRow* rowOf(uint32_t id) const {
        for (const MapStringRow& r : all_)
            if (r.id == id) return &r;
        return nullptr;
    }

    // The rows again (the map, the filter or the search changed); the selection stays on its string.
    void fillList() {
        const Editor* ed = editor();
        if (!ed) return;
        // (the string in the editor keeps its row while the editor has the keyboard: an empty text or a filter that leaves it
        // out does not move the editor to another string)
        const uint32_t keep = editor_.hasFocusInside() ? gMapStrings.selected : 0;
        all_ = mapStringRows(*ed, uses_, kMapStringsAll, L"", keep);
        rows_ = mapStringRows(*ed, uses_, gMapStrings.filter, gMapStrings.query, keep);
        list_.setCount(int(rows_.size()));
        uint32_t want = gMapStrings.selected;
        if (!rowOf(want)) want = rows_.empty() ? 0 : rows_[0].id;
        list_.clearSelection();
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == want) list_.selectOnly(int(i)), scrollTo_ = int(i);
        select(want);
        enable();
        refs_.resetScroll();
        invalidate();
    }
    // The string that shows: its text in the editor (an edit of the one before goes into the map first).
    void select(uint32_t id) {
        if (id != gMapStrings.selected) commitText();
        const std::string& raw = strings().get(id);
        if (id != gMapStrings.selected || (!dirty_ && raw != loaded_)) {
            gMapStrings.selected = id;
            loaded_ = raw, dirty_ = false;
            editor_.setBytes(raw);
            refs_.resetScroll();
        }
        enable();
        invalidate();
    }
    void enable() {
        const MapStringRow* r = rowOf(gMapStrings.selected);
        int at = -1;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == gMapStrings.selected) at = int(i);
        auto set = [](stardraft::kit::Button& b, bool on) {
            if (b.enabled() != on) b.setEnabled(on);
        };
        set(delete_, r && !r->uses && !strings().get(r->id).empty());
        set(up_, at > 0), set(down_, at >= 0 && at + 1 < int(rows_.size()));
    }

    // The editor's text goes into the map half a second after the last key, when the focus leaves it, or before
    // another string shows (one undo step each).
    void typed() {
        dirty_ = true;
        invalidate();
        if (draw::Host* h = host()) {
            if (timer_) h->cancelTimer(timer_);
            timer_ = h->timer(500, [this] {
                timer_ = 0;
                commitText();
            });
        }
    }
    void commitText() {
        if (timer_ && host()) host()->cancelTimer(timer_);
        timer_ = 0;
        if (!dirty_) return;
        dirty_ = false;
        Editor* ed = editor();
        const uint32_t id = gMapStrings.selected;
        if (!ed || !id) return;
        const std::string text = editor_.bytes();
        loaded_ = text;
        if (!ed->setMapString(id, text)) return;
        if (std::any_of(uses_.begin(), uses_.end(), [id](const StringUse& u) { return u.kind == StringUseKind::UnitName && u.id == id; }))
            shellPaletteRebuilt();  // (a unit's name: the palette's and the lists' labels)
        changedMap(L"Changed string " + std::to_wstring(id));
    }
    void changedMap(const std::wstring& message) {
        updateTitle();
        const Editor* ed = editor();
        // (more than STR holds: the table is STRx now, which StarCraft 1.16 and StarEdit do not open; Undo takes it back)
        if (ed && !wasExtended_ && ed->strings().extended())
            setMessage(message + L". The strings are more than 65535 bytes: the map keeps them in STRx (StarCraft: Remastered only)");
        else
            setMessage(message);
        wasExtended_ = ed && ed->strings().extended();
        refresh();
        invalidate();
    }

    void addString() {
        commitText();
        Editor* ed = editor();
        if (!ed) return;
        const uint32_t id = ed->addMapString("New string");
        if (!id) return;
        gMapStrings.filter = kMapStringsAll, gMapStrings.query.clear();
        filter_.setValue(kMapStringsAll);
        search_.setText(L"");
        changedMap(L"Added string " + std::to_wstring(id));
        showString(id);
        editor_.selectAllText();
    }
    void deleteString() {
        commitText();
        Editor* ed = editor();
        const uint32_t id = gMapStrings.selected;
        if (!ed || !id) return;
        if (!ed->deleteMapString(id)) return setMessage(L"Could not delete string " + std::to_wstring(id) + L". The map uses it.");
        changedMap(L"Deleted string " + std::to_wstring(id));
    }
    // Moves string `from` to the id `to` (the rows between move one place); the selection follows it.
    void moveTo(uint32_t from, uint32_t to) {
        commitText();
        Editor* ed = editor();
        if (!ed || !from || !to || from == to) return;
        if (!ed->moveMapString(from, to)) return setMessage(L"Could not move string " + std::to_wstring(from));
        gMapStrings.selected = to;
        loaded_ = ed->strings().get(to), dirty_ = false;
        changedMap(L"Moved string " + std::to_wstring(from) + L" to id " + std::to_wstring(to));
        showString(to);
    }
    void move(int d) {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == gMapStrings.selected) {
                const int j = int(i) + d;
                if (j >= 0 && j < int(rows_.size())) moveTo(rows_[i].id, rows_[size_t(j)].id);
                return;
            }
    }
    // A row dropped before row `slot`: it takes that row's id (up), or the id of the row over the line (down).
    void drop(int from, int slot) {
        if (from < 0 || from >= int(rows_.size())) return;
        const uint32_t id = rows_[size_t(from)].id;
        const uint32_t to = slot <= from ? rows_[size_t(slot)].id : rows_[size_t(std::min<int>(slot, int(rows_.size())) - 1)].id;
        moveTo(id, to);
    }

    void paintRow(draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) {
        const MapStringRow& row = rows_[size_t(i)];
        c.fill(r, kCtl);
        kitpaint::drawListRow(c, r, s.selected, s.hot && !list_.dragging());
        const int idW = scale(52), useW = scale(64);
        c.text({r.left + scale(8), r.top, r.left + idW, r.bottom}, std::to_wstring(row.id), kDim, draw::kMiddle);
        const std::string& raw = strings().get(row.id);
        const std::wstring text = raw.empty() ? std::wstring(L"None") : mapStringLine(raw);
        c.text({r.left + idW, r.top, r.right - useW, r.bottom}, text, raw.empty() ? kDim : s.selected ? kAccent : kText, draw::kMiddle | draw::kEllipsis);
        const std::wstring uses = row.uses ? std::to_wstring(row.uses) + (row.uses == 1 ? L" use" : L" uses") : std::wstring(L"Not used");
        c.text({r.right - useW, r.top, r.right - scale(8), r.bottom}, uses, kDim, draw::kMiddle | draw::kRight);
    }

    // The References sidebar (DataRefsHost): the selected string's uses, grouped.
    std::vector<DataRefRow> refs(bool& shown) override {
        std::vector<DataRefRow> out;
        shown = editor() && gMapStrings.selected;
        if (!shown) return out;
        refUses_.clear();
        for (const StringUse& u : uses_)
            if (u.id == gMapStrings.selected) refUses_.push_back(u);
        // (in the fields' order: each group's uses are together)
        for (size_t k = 0; k < refUses_.size(); ++k) {
            const StringUse& u = refUses_[k];
            DataRefRow row;
            row.dat = narrow(mapStringUseGroup(u.kind));
            row.entry = int(k);
            row.group = mapStringUseGroup(u.kind);
            row.groupIcon = mapStringUseIcon(u.kind);
            row.name = mapStringUseName(*editor(), u, trig_, brief_);
            row.feedback = row.name;
            out.push_back(std::move(row));
        }
        return out;
    }
    void goTo(const std::string&, int entry) override {
        if (entry < 0 || entry >= int(refUses_.size())) return;
        commitText();
        const StringUse u = refUses_[size_t(entry)];
        if (gMapStringGoTo) gMapStringGoTo(u);
    }
    bool travel(int) override { return false; }

    SearchBox search_;
    stardraft::kit::Button add_, delete_, up_, down_;
    stardraft::kit::Dropdown filter_;
    MapStringList list_;
    GameTextView editor_;
    DataRefsPage refs_;
    std::vector<StringUse> uses_, refUses_;
    ClassicTriggers trig_;
    std::vector<BriefingTrigger> brief_;
    std::vector<MapStringRow> all_, rows_;
    uint64_t revision_ = 0;
    bool wasExtended_ = false;  // the table was STRx at the last edit
    std::string loaded_;  // the selected string's text when the editor took it
    bool dirty_ = false;  // the editor has an edit that is not in the map yet
    int timer_ = 0;
    int scrollTo_ = -1;
};
