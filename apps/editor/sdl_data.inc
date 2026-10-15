// The Data workspace on this system: the core's views, as the Windows program has them (core/views_data_workspace.inc:
// the tab strip, the search box and the Changed toggle; core/views_data_list.inc: the entry list; core/views_data_form.inc:
// the entry's form; core/views_data_refs.inc: the References panel; core/views_data_entry.inc: the number entry) on the
// core's model (core/data_model.inc: the open project's tables with its edits, the extras, the notes, the references, the
// saves and the undo steps). The views are children of one view here (the Windows program puts each in a window of its
// own), each with a kit scroll bar where the Windows window has its bar. The Strings and Buttons tabs' forms and the
// requirements are sdl_data_extras.inc's. The pins, back and forward, the clipboard and the entry's and the field's
// menus are the core's (core/data_nav.inc), the menus kit menus here; the entries' icons are the palette's pictures
// (sdl_pictures.inc), and the pictures beside the fields are the core's (core/views_data_pictures.inc). Not on this
// system yet: the other pictures beside the fields (the diagrams, the sound player), the Units display, the map's
// players and Rename. Included by
// main_sdl.cpp after sdl_briefing.inc; registerWorkspaceView(Workspace::Data, gSdlData) puts it in the window, and
// --data <tab>:<entry> opens an entry for a snapshot.

// --- The model's state (core/data_model.inc) -------------------------------------------------------------------------

DataModel gSdlDataState;
DataExtrasModel gSdlDataExtras;
DataModel& dataState() { return gSdlDataState; }
DataExtrasModel& dataExtras() { return gSdlDataExtras; }

void sdlDataNotHere(const wchar_t* what) { setMessage(std::wstring(what) + L": not on this system yet"); }

// --- A view with a kit scroll bar at its right (the Windows window's own bar: the view's windowScrollBar and vscroll) ---

class SdlBarFrame final : public draw::View {
public:
    explicit SdlBarFrame(draw::View& content) : content_(content), bar_(content) {
        bar_.onScroll = [this](int pos) {
            int range = 0, page = 0, now = 0;
            content_.windowScrollBar(range, page, now);
            content_.vscroll(5, pos, pos, page);  // (SB_THUMBTRACK: the position)
            content_.invalidate();
        };
        add(content_, {0, 0, 0, 0});
        add(bar_, {0, 0, 0, 0});
    }
    static int barW() { return scale(stardraft::kit::kScrollBarW); }
    void resized() override {
        content_.setFrame({0, 0, std::max(0, width() - barW()), height()});
        bar_.setFrame({width() - barW(), 0, width(), height()});
    }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }
    bool mouse(const draw::MouseEvent& e) override {  // the wheel over the bar scrolls the view
        return e.kind == draw::MouseKind::Wheel && content_.mouse(e);
    }

private:
    // The bar follows the view as the view's paint just measured it: the bar paints after the view (the frame's second
    // child), so it is never one frame behind.
    struct Bar final : stardraft::kit::ScrollBar {
        explicit Bar(draw::View& content) : content(content) {}
        void paint(draw::Canvas& c) override {
            int range = 0, page = 0, pos = 0;
            if (content.windowScrollBar(range, page, pos)) {
                if (range + 1 != total || page != shown) total = range + 1, shown = page, setRange(total, shown);
                setPos(pos);
            }
            stardraft::kit::ScrollBar::paint(c);
        }
        draw::View& content;
        int total = -1, shown = -1;
    };
    draw::View& content_;
    Bar bar_;
};

// --- The workspace's actions (the Windows program's dataShowTab, dataSelect, dataGoTo, dataRebuildList) ---------------

extern DataWorkspaceView gSdlDataWorkspace;
extern DataListView gSdlDataList;
extern DataFormView gSdlDataForm;
extern DataRefsPage gSdlDataRefs;
extern DataNumberEntry gSdlDataEntry;
void sdlDataRepaint();
void sdlStringsCommit();
bool gSdlDataRevealDue = false;  // the open entry's row is scrolled into the list's view at the next paint

void sdlDataRebuildList() {
    DataModel& d = gSdlDataState;
    d.shown = dataListShown(gSdlDataWorkspace.filter().search, gSdlDataWorkspace.filter().changedOnly);  // (the pins first)
    gSdlDataList.clampScroll();  // a shorter list: the scroll goes back inside it
    sdlDataRepaint();
}
// The core's calls (core/data_nav.inc): the list again, the panes painted again.
void dataRebuildList() { sdlDataRebuildList(); }
void dataPanesChanged() { sdlDataRepaint(); }

// A view that hides gives the keyboard back to the form (else the keys go to a box nobody sees: Ctrl+Z undoes its
// hidden text, typed text changes a string of another tab). The Windows program does this when it hides a window.
static bool sdlDataFocusIn(const draw::View& v) {
    if (v.hasFocus()) return true;
    for (const draw::View* c : v.children())
        if (sdlDataFocusIn(*c)) return true;
    return false;
}
void sdlDataHide(draw::View& v) {
    const bool focused = sdlDataFocusIn(v);
    v.setVisible(false);
    if (focused) gSdlDataForm.focus();
}

// The number entry's value goes in. False when the box held text that is not a number (the status says so).
bool sdlDataCommitEdit() {
    sdlStringsCommit();  // (the Strings tab's editor: its text goes in too)
    DataModel& d = gSdlDataState;
    if (d.editing < 0) return true;
    const DatField* f = curField(d.editing);
    d.editing = -1;
    const std::wstring text = gSdlDataEntry.text();
    sdlDataHide(gSdlDataEntry);
    if (!f) return true;
    uint32_t v = 0;
    const DataEntryRead r = dataReadEntry(text, f->maxValue(), v);
    if (r == DataEntryRead::Empty) return true;
    if (r == DataEntryRead::NotNumber) {
        setMessage(L"Not a number: " + text);
        return false;
    }
    gSdlDataForm.setValue(*f, v);
    return true;
}

void sdlDataSelect(int u) {
    sdlDataCommitEdit();
    DataModel& d = gSdlDataState;
    dataPlaceRecord(d.datTab, u);  // (Back goes to the entry open before)
    d.entry = std::clamp(u, tabFirstEntry(d.datTab), std::max(tabFirstEntry(d.datTab), tabEntries(d.datTab) - 1));
    d.formScroll = 0, gSdlDataRefs.resetScroll();
    gSdlDataRevealDue = true;  // keep it in view in the list (at the next paint: the list has its size then)
    sdlDataRepaint();
}

void sdlDataShowTab(int tab) {
    sdlDataCommitEdit();
    DataModel& d = gSdlDataState;
    if (tab < 0 || tab >= dataTabCount()) return;
    if (d.tabEntry.size() < size_t(dataTabCount())) d.tabEntry.resize(size_t(dataTabCount()), 0);
    if (tab != d.datTab) {
        dataPlaceRecord(tab, d.tabEntry[size_t(tab)]);
        dataSwitchTab(d.datTab, d.entry, d.tabEntry, d.listScroll, d.formScroll, tab);
        gSdlDataWorkspace.resetSearch(dataSearchCue(tabTitle(tab)));
    }
    sdlDataRebuildList();
    gSdlDataWorkspace.relayout();
    sdlDataSelect(d.entry);
}

void sdlDataGoTo(const std::string& dat, int e) {
    const int tab = datTabOf(dat);
    if (tab < 0) return;
    DataModel& d = gSdlDataState;
    if (gSdlDataWorkspace.filter().changedOnly && !tabEntryChanged(tab, e)) gSdlDataWorkspace.setChangedOn(false);
    d.tabEntry.resize(size_t(dataTabCount()), 0);
    if (tab != d.datTab) d.tabEntry[size_t(tab)] = e;
    sdlDataShowTab(tab);
    sdlDataSelect(e);
    setMessage(tabTitle(tab) + L" " + std::to_wstring(e) + L": " + tabEntryName(tab, e));
}
void dataGoTo(const std::string& dat, int e) { sdlDataGoTo(dat, e); }  // (the core's: the quick picker's Data rows)

// Back and forward (the arrows, the mouse's side buttons): core/data_nav.inc.
void sdlDataTravel(bool back) {
    if (dataTravelTo(back, [](int tab) { sdlDataShowTab(tab); }, [](int e) { sdlDataSelect(e); })) sdlDataRepaint();
}

// A value's change: saved with its undo step (core/data_model.inc), and the list's changed dots follow. False when a save
// failed (the status says why).
bool dataSaved() {
    const bool ok = dataSaveChange();
    sdlDataRebuildList();
    return ok;
}
void sdlDataSaved() { dataSaved(); }

// --- The entries' pictures: the palette's pictures (sdl_pictures.inc) as images for the core's views ------------------

draw::Image sdlDataImage(DataPicture p) {
    if (p.kind == DataPicture::None || !gDevice) return {};
    draw::Bitmap* b = paletteBitmap(Mode::Units, p.value, gState.playerOwner, p.kind == DataPicture::IconFrame);
    return b ? kit_sdl::imageOfBitmap(gDevice, *b) : draw::Image{};
}

// A sound of the image player's script (the core's gImagePlayer, when its sounds are on), as the Windows program's
// sfxPlayOnce: each sound decoded once, at most 8 at a time; a test run plays nothing (openPlaybackSdl).
void sfxPlayOnce(int sfx) {
    static std::vector<SDL_AudioStream*> playing;
    for (auto it = playing.begin(); it != playing.end();)
        if (SDL_GetAudioStreamQueued(*it) <= 0) SDL_DestroyAudioStream(*it), it = playing.erase(it);
        else ++it;
    if (playing.size() >= 8) return;
    struct Decoded {
        SDL_AudioSpec spec{};
        std::vector<Uint8> pcm;
    };
    static std::map<int, Decoded> decoded;
    auto it = decoded.find(sfx);
    if (it == decoded.end()) {
        const std::wstring path = sfxPath(sfx);
        Bytes wav;
        std::string err;
        if (path.empty() || !gState.data.readFile("sound\\" + narrow(path), wav, err)) return;
        Decoded d;
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(wav.data(), wav.size()), true, &d.spec, &buf, &len)) return;
        d.pcm.assign(buf, buf + len);
        SDL_free(buf);
        if (decoded.size() > 64) decoded.clear();
        it = decoded.emplace(sfx, std::move(d)).first;
    }
    if (SDL_AudioStream* s = openPlaybackSdl(&it->second.spec)) {
        SDL_PutAudioStreamData(s, it->second.pcm.data(), int(it->second.pcm.size()));
        SDL_FlushAudioStream(s);
        SDL_ResumeAudioStreamDevice(s);
        playing.push_back(s);
    }
}

// A menu of the core's items (core/data_nav.inc) at `at` of `from`, then "Report feedback on <on>..." as the Windows
// menus end; a pick runs `pick` with the item's id.
void sdlDataMenu(draw::View& from, draw::Point at, const MenuModel& items, std::function<void(int)> pick, const std::wstring& on) {
    std::vector<kit::MenuItem> out;
    for (const MenuItem& it : items) {
        kit::MenuItem k;
        if (it.kind == MenuItem::Kind::Separator) k.separator = true;
        else {
            k.label = accessFree(shellTr(it.label.c_str())), k.shortcut = it.shortcut, k.enabled = !it.grayed;
            const int id = it.id;
            k.run = [pick, id] { pick(id); };
        }
        out.push_back(std::move(k));
    }
    if (!out.empty()) out.push_back(kit::MenuItem{}), out.back().separator = true;
    kit::MenuItem fb;
    fb.label = shellTr(L"Report feedback on ") + on + L"\x2026";
    fb.run = [on] { showFeedbackSdl(on); };
    out.push_back(std::move(fb));
    ImDrawList list(ImGui::GetDrawListSharedData());
    kit_sdl::ImguiCanvas measure(list, nullptr);
    kit::Menu::popup(from, at, out, kit::Menu::measure(out, &measure));
}

// Undo and redo in the Data workspace (Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z while it shows): the step's table or text as it
// was, at the entry that changed.
bool sdlDataUndo(bool redo) {
    sdlDataCommitEdit();
    DataEditStep s;
    std::string err;
    if (!dataApplyUndo(redo, s, err)) {
        setMessage(redo ? L"Nothing to redo" : L"Nothing to undo");
        return false;
    }
    if (s.tab >= 0 && s.tab < dataTabCount()) {
        gSdlDataState.tabEntry.resize(size_t(dataTabCount()), 0);
        if (!s.extra) gSdlDataState.tabEntry[size_t(s.tab)] = s.entry;
        sdlDataShowTab(s.tab);
        sdlDataSelect(s.entry);
    }
    sdlDataRebuildList();
    setMessage(!err.empty() ? widen(err) : redo ? L"Redo" : L"Undo");
    return true;
}

// The project of the active tab: its tables and extras (once for each project; core/data_model.inc).
void sdlDataLoad() {
    const DataModel& d = gSdlDataState;
    const EudProject* before = d.project;
    const bool hadTables = !d.tables.empty();
    dataLoadForProject();
    if (d.project != before || d.tables.empty() != !hadTables) {
        gSdlDataWorkspace.resetSearch(dataSearchCue(tabTitle(d.datTab)));
        sdlDataRebuildList();
    }
}

#include "sdl_data_extras.inc"  // the Strings and Buttons tabs and the requirements (sdlPaintExtraTab, sdlPaintRequirements)

// --- The hosts of the core's views -----------------------------------------------------------------------------------

class SdlDataWorkspaceHost final : public DataWorkspaceHost {
public:
    int tabCount() override { return dataTabCount(); }
    std::wstring tabTitle(int tab) override { return ::tabTitle(tab); }
    int tabEntryCount(int tab) override { return tabEntries(tab) - tabFirstEntry(tab); }
    int tabChangedCount(int tab) override { return ::tabChangedCount(tab); }
    int tabIcon(int tab) override { return int(::tabIcon(tab)); }
    draw::FontDesc countFont() override {  // the counts: four fifths of the UI font's height (as the Windows strip has it)
        draw::FontDesc d;
        d.px = -(shellUiFontLfHeight() * 4 / 5);
        return d;
    }
    int currentTab() override { return gSdlDataState.datTab; }
    bool canBack() override { return !gDataBack.empty(); }
    bool canForward() override { return !gDataForward.empty(); }
    void tabClicked(int tab) override {
        sdlDataShowTab(tab);
        const int n = ::tabChangedCount(tab);
        setMessage((extraTab(tab) ? ::tabTitle(tab) : widen(DatSchema::all()[size_t(tab)].name) + L".dat") + (n ? L": " + std::to_wstring(n) + L" changed" : L""));
    }
    void travel(bool back) override { sdlDataTravel(back); }
    int listWidth() override { return scale(gDock.dataListW); }
    int dragList(int dx) override {  // the edge: the list's width (the dock's, clamped); returns the px it moved
        const int dpi = int(shellDpi());
        const int nw = std::clamp(gDock.dataListW + dx * 96 / dpi, kMinSideW, kMaxSideW);
        if (nw == gDock.dataListW) return 0;
        const int moved = (nw - gDock.dataListW) * dpi / 96;
        gDock.dataListW = nw;
        gSdlDataWorkspace.relayout();
        return moved;
    }
    void splitDone() override { saveDock(); }
    int refsWidth() override { return scale(260); }
    void placeLists(draw::Rect list, draw::Rect form, draw::Rect refs) override;
    void filterChanged() override { sdlDataRebuildList(); }
    void status(const std::wstring& text) override { setMessage(text); }
};
SdlDataWorkspaceHost gSdlDataWorkspaceHost;
DataWorkspaceView gSdlDataWorkspace(gSdlDataWorkspaceHost);

class SdlDataListHost final : public DataListViewHost {
public:
    int tab() override { return gSdlDataState.datTab; }
    const std::vector<int>& rows() override { return gSdlDataState.shown; }
    int pinnedShown() override { return gDataPinnedShown; }
    int entry() override { return gSdlDataState.entry; }
    int& scroll() override { return gSdlDataState.listScroll; }
    bool changed(int e) override { return tabEntryChanged(gSdlDataState.datTab, e); }
    std::wstring name(int e) override { return tabEntryName(gSdlDataState.datTab, e); }
    draw::Image icon(int e) override { return sdlDataImage(dataTabEntryPicture(gSdlDataState.datTab, e)); }
    void select(int e) override { sdlDataSelect(e); }
    void menu(int e, draw::Point pt) override;
    bool travel(int button) override { return sdlDataTravel(button == 1), true; }
};
SdlDataListHost gSdlDataListHost;
DataListView gSdlDataList(gSdlDataListHost);
void SdlDataListHost::menu(int e, draw::Point pt) {  // the entry's menu (core/data_nav.inc), at the mouse
    const DatTable* t = curTable();
    if (!t) return;
    sdlDataMenu(gSdlDataList, pt, dataEntryMenuModel(e), [e](int pick) { dataEntryMenuPick(e, pick); }, entryName(t->schema().name, e));
}

class SdlDataRefsHost final : public DataRefsHost {
public:
    std::vector<DataRefRow> refs(bool& shown) override {
        return dataRefRows(shown, [](int tab, int e) { return sdlDataImage(dataTabEntryPicture(tab, e)); });
    }
    void goTo(const std::string& dat, int entry) override { sdlDataGoTo(dat, entry); }
    bool travel(int button) override { return sdlDataTravel(button == 1), true; }
};
SdlDataRefsHost gSdlDataRefsHost;
DataRefsPage gSdlDataRefs(gSdlDataRefsHost);

// The form's program parts. The Strings and Buttons tabs' forms and the requirements are sdl_data_extras.inc's (their
// click areas are this host's). The pictures, the Units display and the map's players are the Windows program's GDI
// parts: here they take no room. A click runs when the mouse goes up over its area.
class SdlDataFormHost final : public DataFormViewHost {
public:
    struct Area {
        draw::Rect r;
        std::function<void()> click;
        std::wstring tip;
        draw::Cursor cursor = draw::Cursor::Arrow;
    };
    std::vector<Area> areas;
    std::function<void()> pressed;  // a link pressed: its click when the mouse goes up

    static std::string dat() { return curTable() ? curTable()->schema().name : std::string(); }
    // The topmost area under p (`clicks`: the topmost one with a click; a row's area without one does not hide the
    // boxes under it, as on Windows).
    const Area* areaAt(draw::Point p, bool clicks = false) const {
        for (auto it = areas.rbegin(); it != areas.rend(); ++it)
            if (it->r.contains(p) && (!clicks || it->click)) return &*it;
        return nullptr;
    }

    const DatTable* table() override { return curTable(); }
    DatTable* editTable() override { return curTable(); }
    int entry() override { return gSdlDataState.entry; }
    int& scroll() override { return gSdlDataState.formScroll; }
    std::set<std::string>& hotFields() override { return hot_; }
    bool hot(draw::Rect r) override {
        draw::Host* h = gSdlDataForm.host();
        return h && r.contains(h->mouse(gSdlDataForm));
    }
    bool projectOpen() override { return activeProject() != nullptr; }
    void beforePaint() override { areas.clear(); }
    bool paintExtraTab(draw::Canvas& c, int& y) override { return sdlPaintExtraTab(c, y); }
    void afterPaint(draw::Canvas&) override;
    draw::Image entryIcon() override { return sdlDataImage(dataEntryPicture(gSdlDataState.datTab, gSdlDataState.entry)); }
    UiIcon tabIcon() override { return ::tabIcon(gSdlDataState.datTab); }
    int nameString() override {
        const int sid = entryNameString(dat(), gSdlDataState.entry);
        return sid > 0 && gSdlDataState.project && sid <= gSdlDataExtras.strings.count() ? sid : 0;
    }
    void rename(int) override { sdlDataNotHere(L"Rename (the game text editor)"); }
    void openString(int sid) override { sdlDataGoTo("strings", sid); }
    std::wstring fieldNote(const DatField& f, uint32_t v) override { return ::fieldNote(f, v); }
    std::wstring readOnlyWhy(const DatField& f) override { return datFieldReadOnly(dat(), f, gSdlDataState.entry); }
    draw::Image refIcon(const std::string& d, uint32_t v) override { return sdlDataImage(dataRefPicture(d, v)); }
    int widgets(draw::Canvas&, int, int, int y, int) override { return y; }
    void soundButton(draw::Canvas&, draw::Rect, int) override {}
    int unitDisplay(draw::Canvas&, int y) override { return y; }
    int requirements(draw::Canvas& c, int, int y, int) override { return sdlPaintRequirements(c, dat(), gSdlDataState.entry, y); }
    int mapPlayers(draw::Canvas&, int, int y, int, int, int) override { return y; }
    void area(draw::Rect r, std::function<void()> click, const std::wstring& tip, draw::Cursor cursor) override {
        areas.push_back({r, std::move(click), tip, cursor});
    }
    bool widgetPress(draw::Point p) override {
        const Area* a = areaAt(p, true);
        if (!a || !a->click) return false;
        sdlDataCommitEdit();
        pressed = a->click;
        return true;
    }
    bool widgetDrag(draw::Point) override { return false; }
    void widgetRelease(bool lost) override {
        auto click = std::exchange(pressed, nullptr);
        if (click && !lost) click();
    }
    bool widgetHover(draw::Point, std::set<std::string>&) override { return true; }  // (the form repaints: the hot boxes follow the mouse)
    bool widgetCursor(draw::Point p, draw::Cursor& c) override {
        const Area* a = areaAt(p, true);
        if (!a) a = areaAt(p);
        return a ? (c = a->cursor, true) : false;
    }
    std::wstring widgetName(draw::Point p) override {
        const Area* a = areaAt(p);
        return a ? a->tip : L"";
    }
    bool widgetWheel(draw::Point, float) override { return false; }
    void commitEdit() override { sdlDataCommitEdit(); }
    void beginEdit(int field, draw::Rect) override {
        sdlDataCommitEdit();
        const DatField* f = curField(field);
        if (!f || !curTable()) return;
        gSdlDataState.editing = field;
        gSdlDataEntry.setText(fieldValueText(*f, curTable()->value(*f, gSdlDataState.entry)));
        gSdlDataEntry.setVisible(true);
        placeEntry();
        gSdlDataEntry.focus();
        gSdlDataEntry.select();
    }
    void dropdown(const DatField& f, draw::Rect) override {
        const DatTable* t = curTable();
        if (!t) return;
        const std::vector<ListPickItem> items = dataDropdownItems(f);
        const std::string cur = std::to_string(t->value(f, gSdlDataState.entry));
        std::vector<std::wstring> rows;
        int checked = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            rows.push_back(items[i].label);
            if (items[i].value == cur) checked = int(i);
        }
        const int picked = sdlPickFromList(rows, checked, datLabel(f.name));
        if (picked >= 0) gSdlDataForm.setValue(f, uint32_t(std::stoul(items[size_t(picked)].value)));
    }
    std::vector<ListPickItem> dropdownItems(const DatField& f) override { return dataDropdownItems(f); }
    void valueSet() override { sdlDataSaved(); }
    void paintNow() override {}  // (the entry follows its box at each paint: placeEntry)
    void goTo(const std::string& d, int e) override { sdlDataGoTo(d, e); }
    void fieldMenu(const DatField& f, draw::Point p) override;
    void status(const std::wstring& text) override { setMessage(text); }
    bool travel(int button) override { return sdlDataTravel(button == 1), true; }

    // The number entry sits on its field's value box, as the last paint placed it (also after a scroll).
    void placeEntry() {
        if (gSdlDataState.editing < 0) return;
        for (const DataFormHit& h : gSdlDataForm.hits())
            if (h.bit == -1 && h.field == gSdlDataState.editing) {
                const draw::Rect r{h.r.left + scale(2), h.r.top + scale(3), h.r.right - scale(2), h.r.bottom - scale(3)};
                const draw::Rect cur = gSdlDataEntry.frame();
                if (cur.left != r.left || cur.top != r.top || cur.right != r.right || cur.bottom != r.bottom) gSdlDataEntry.setFrame(r);
                return;
            }
    }

private:
    std::set<std::string> hot_;
};
SdlDataFormHost gSdlDataFormHost;
DataFormView gSdlDataForm(gSdlDataFormHost);
DataNumberEntry gSdlDataEntry;
void SdlDataFormHost::afterPaint(draw::Canvas&) { placeEntry(); }
void SdlDataFormHost::fieldMenu(const DatField& f, draw::Point p) {  // the field's menu (core/data_nav.inc), at the mouse
    if (!curTable()) return;
    const DatField* field = &f;  // (a field of the open table: it stays while the menu is open)
    sdlDataMenu(gSdlDataForm, p, dataFieldMenuModel(f), [field](int pick) { dataFieldMenuPick(*field, pick); }, widen(f.name));
}
void sdlFormArea(draw::Rect r, std::function<void()> click, const std::wstring& tip, bool hand) {
    gSdlDataFormHost.area(r, std::move(click), tip, hand ? draw::Cursor::Hand : draw::Cursor::Arrow);
}
bool sdlFormHot(draw::Rect r) { return gSdlDataFormHost.hot(r); }

// --- The workspace view: the container and the three panes, each with its bar --------------------------------------

struct SdlDataView final : draw::View {
    SdlBarFrame listFrame{gSdlDataList}, formFrame{gSdlDataForm}, refsFrame{gSdlDataRefs};
    bool ready = false;

    SdlDataView() {
        add(gSdlDataWorkspace, draw::Rect(0, 0, 0, 0));
        add(listFrame, draw::Rect(0, 0, 0, 0));
        add(formFrame, draw::Rect(0, 0, 0, 0));
        add(refsFrame, draw::Rect(0, 0, 0, 0));
        gSdlDataForm.add(gSdlDataEntry, draw::Rect(0, 0, 0, 0));
        gSdlDataEntry.setVisible(false);
        gSdlDataEntry.onCommit = [] {  // Enter: the value goes in, the form has the keys again
            sdlDataCommitEdit();
            static_cast<draw::View&>(gSdlDataForm).focus();
        };
        gSdlDataEntry.onCancel = [] {  // Escape: the value stays as it was
            gSdlDataState.editing = -1;
            sdlDataHide(gSdlDataEntry);
        };
        gSdlDataEntry.onFocusLost = [] { sdlDataCommitEdit(); };
        gSdlDataEntry.onTab = [](bool back) { gSdlDataForm.editNeighbour(gSdlDataState.editing, back ? -1 : 1); };
        gSdlDataForm.add(gSdlStringsText, draw::Rect(0, 0, 0, 0));  // the Strings tab's editor (sdl_data_extras.inc)
        gSdlStringsText.setVisible(false);
        gSdlStringsText.onChange = [] {  // the preview follows; the text goes in when the editor loses the keyboard
            gSdlDataExtras.textDirty = true;
            gSdlDataForm.invalidate();
        };
        gSdlStringsText.onFocusLost = [] { sdlStringsCommit(); };
    }

    void resized() override { gSdlDataWorkspace.setFrame({0, 0, width(), height()}); }
    void paint(draw::Canvas& c) override {
        sdlDataLoad();  // (the active tab's project: its tables, once)
        if (!ready) ready = true, gSdlDataWorkspace.resetSearch(dataSearchCue(tabTitle(gSdlDataState.datTab))), sdlDataRebuildList();
        c.fill({0, 0, width(), height()}, kBg);
        if (gSdlDataRevealDue && gSdlDataList.height() > 0) gSdlDataRevealDue = false, gSdlDataList.reveal(gSdlDataState.entry);
    }
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || !(e.mods & draw::kCtrl) || (e.mods & draw::kAlt)) return false;
        if (gSdlDataWorkspace.searchFocused()) return false;  // (the search box's own undo)
        if (int(e.key) == 'Z') return sdlDataUndo((e.mods & draw::kShift) != 0), true;
        if (int(e.key) == 'Y') return sdlDataUndo(true), true;
        return false;
    }
    bool focusable() const override { return false; }
};

SdlDataView gSdlData;

void SdlDataWorkspaceHost::placeLists(draw::Rect list, draw::Rect form, draw::Rect refs) {
    auto place = [](draw::View& v, draw::Rect r) {
        const draw::Rect cur = v.frame();
        if (cur.left != r.left || cur.top != r.top || cur.right != r.right || cur.bottom != r.bottom) v.setFrame(r);
    };
    place(gSdlData.listFrame, list);
    place(gSdlData.formFrame, form);
    place(gSdlData.refsFrame, refs);
}

void sdlDataRepaint() {
    gSdlData.invalidate();
    gSdlDataList.invalidate();
    gSdlDataForm.invalidate();
    gSdlDataRefs.invalidate();
    gSdlDataWorkspace.invalidate();
}

// A tab with this project closed (forgetProject): the number entry goes, and the model drops the project's tables.
void sdlDataForgetProject(const EudProject* pr) {
    if (!pr || gSdlDataState.project != pr) return;
    gSdlDataState.editing = -1;
    gSdlDataEntry.setVisible(false);
    dataForgetProject(pr);
    sdlDataRepaint();
}

// --- The snapshot's entry: --data <tab>:<entry> (the Data workspace opens the entry as it is shown) -----------------------

void sdlDataSelect(const std::string& spec) {
    const size_t colon = spec.find(':');
    if (colon == std::string::npos) return;
    sdlDataLoad();
    gSdlData.ready = true;
    sdlDataGoTo(spec.substr(0, colon), std::atoi(spec.c_str() + colon + 1));
}
