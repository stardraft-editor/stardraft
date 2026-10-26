// The Data workspace on this system: the core's views, as the Windows program has them (core/views_data_workspace.inc:
// the tab strip, the search box and the Changed toggle; core/views_data_list.inc: the entry list; core/views_data_form.inc:
// the entry's form; core/views_data_refs.inc: the References panel; core/views_data_entry.inc: the number entry) on the
// core's model (core/data_model.inc: the open project's tables with its edits, the extras, the notes, the references, the
// saves and the undo steps). The views are children of one view here (the Windows program puts each in a window of its
// own), each with a kit scroll bar where the Windows window has its bar. The Strings and Buttons tabs' forms and the
// requirements are sdl_data_extras.inc's. The pins, back and forward, the clipboard and the entry's and the field's
// menus are the core's (core/data_nav.inc), the menus kit menus here; the entries' icons are the palette's pictures
// (sdl_pictures.inc); the image player, its timeline and the portrait beside the fields are the core's
// (core/views_data_pictures.inc), and so are the diagrams, the script's uses and code and the sound player
// (core/views_data_diagrams.inc); the Units display and the map's players are the core's paint (sdl_data_extras.inc).
// Rename edits the name in the text prompt. Included by
// main_sdl.cpp after sdl_briefing.inc; registerWorkspaceView(Workspace::Data, gSdlData) puts it in the window, and
// --data <tab>:<entry> opens an entry for a snapshot.

// --- The model's state (core/data_model.inc) -------------------------------------------------------------------------

DataModel gSdlDataState;
DataExtrasModel gSdlDataExtras;
DataModel& dataState() { return gSdlDataState; }
DataExtrasModel& dataExtras() { return gSdlDataExtras; }


// --- The workspace's actions (the Windows program's dataShowTab, dataSelect, dataGoTo, dataRebuildList) ---------------

extern DataWorkspaceView gSdlDataWorkspace;
extern DataListView gSdlDataList;
extern DataFormView gSdlDataForm;
extern DataRefsPage gSdlDataRefs;
extern DataNumberEntry gSdlDataEntry;
extern DataTableView gSdlDataTable;
void sdlDataRepaint();
void sdlDataPicksChanged();
void sdlStringsCommit();
void sdlShowHelp(const std::string& topic);  // (sdl_help.inc: the help window at a topic)
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
    if (gSdlDataTable.editing()) gSdlDataTable.commitEdit(false);  // (the Table view's text box: before the rows change)
    DataModel& d = gSdlDataState;
    if (d.editing < 0) return true;
    const DatField* f = curField(d.editing);
    d.editing = -1;
    const std::wstring text = gSdlDataEntry.text();
    sdlDataHide(gSdlDataEntry);
    if (!f) return true;
    uint32_t v = 0;
    const DataEntryRead r = dataReadEntry(text, f->maxValue(), v, fieldShownScale(*f));
    if (r == DataEntryRead::Empty) return true;
    if (r == DataEntryRead::NotNumber) {
        setMessage(L"Not a number: " + text);
        return false;
    }
    gSdlDataForm.setValue(*f, v);
    return true;
}

// Entry u opens. `keepPicks`: the list's other selected rows stay selected (else u is the only one).
void sdlDataSelect(int u, bool keepPicks = false) {
    sdlDataCommitEdit();
    DataModel& d = gSdlDataState;
    dataPlaceRecord(d.datTab, u);  // (Back goes to the entry open before)
    d.entry = std::clamp(u, tabFirstEntry(d.datTab), std::max(tabFirstEntry(d.datTab), tabEntries(d.datTab) - 1));
    if (!keepPicks) d.picked = {d.entry}, d.pickAnchor = d.entry;
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
    dataMapStepsClear();  // a data edit: Ctrl+Z goes to the data's undo (core/views_data_players.inc)
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
bool sdlSfxLoad(int sfx, SDL_AudioSpec& spec, std::vector<Uint8>& pcm);
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
        Decoded d;
        if (!sdlSfxLoad(sfx, d.spec, d.pcm)) return;
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

// An SFX data entry's sound, as the Windows program's sfxLoad: its file under sound\ in the game's data, decoded
// (Remastered's sounds are Ogg Vorbis under their .wav names; 1.16.1's are WAV).
bool sdlSfxLoad(int sfx, SDL_AudioSpec& spec, std::vector<Uint8>& pcm) {
    const std::wstring path = sfxPath(sfx);
    Bytes file;
    std::string err;
    SdlSound d;
    if (path.empty() || !gState.data.readFile("sound\\" + narrow(path), file, err) || !decodeSdlSound(file, d)) return false;
    spec = d.spec, pcm = std::move(d.pcm);
    return true;
}

// The SFX data form's sound (the player and the play buttons; the Windows program's gSound): play, pause and go on;
// another sound starts from its beginning.
struct SdlSfxSound {
    SDL_AudioStream* stream = nullptr;
    int sfx = -1, total = 0;
    bool paused = false;
} gSdlSfx;
void sdlSfxStop() {
    if (gSdlSfx.stream) SDL_DestroyAudioStream(gSdlSfx.stream);
    gSdlSfx = {};
}
void sdlSfxToggle(int sfx) {
    ++gSoundAsks, gSoundAsked = "sfx " + std::to_string(sfx);  // (a snapshot's state line: a test run plays nothing)
    if (gSdlSfx.sfx == sfx && gSdlSfx.stream) {
        gSdlSfx.paused = !gSdlSfx.paused;
        gSdlSfx.paused ? SDL_PauseAudioStreamDevice(gSdlSfx.stream) : SDL_ResumeAudioStreamDevice(gSdlSfx.stream);
        return;
    }
    sdlSfxStop();
    SDL_AudioSpec spec{};
    std::vector<Uint8> pcm;
    if (!sdlSfxLoad(sfx, spec, pcm)) return setMessage(L"Could not play the sound");
    gSdlSfx.stream = openPlaybackSdl(&spec);  // (a test run plays nothing: sdl_files.inc)
    if (!gSdlSfx.stream) {
        if (!gSilent) setMessage(L"No sound device is available");
        return;
    }
    SDL_PutAudioStreamData(gSdlSfx.stream, pcm.data(), int(pcm.size()));
    SDL_FlushAudioStream(gSdlSfx.stream);
    SDL_ResumeAudioStreamDevice(gSdlSfx.stream);
    gSdlSfx.sfx = sfx, gSdlSfx.total = int(pcm.size());
}
// The part played (0..1) and whether it plays now; a sound at its end stops (its button shows play again).
double sdlSfxPlayed(int sfx, bool& playing) {
    playing = false;
    if (gSdlSfx.sfx != sfx || !gSdlSfx.stream) return 0;
    const int queued = SDL_GetAudioStreamQueued(gSdlSfx.stream);
    if (!gSdlSfx.paused && queued <= 0) {
        sdlSfxStop();
        return 0;
    }
    playing = !gSdlSfx.paused;
    return gSdlSfx.total ? 1.0 - double(queued) / gSdlSfx.total : 0.0;
}
// The sound's loudness in 240 columns and its length, read once for each file (the Windows widgetSoundPlayer's).
bool sdlSfxWave(int sfx, std::vector<float>& peaks, double& seconds) {
    struct Wave {
        bool ok = false;
        std::vector<float> peaks;
        double seconds = 0;
    };
    static std::map<std::wstring, Wave> waves;
    const std::wstring path = sfxPath(sfx);
    if (path.empty()) return false;
    auto [it, fresh] = waves.try_emplace(path);
    Wave& w = it->second;
    if (fresh) {
        SDL_AudioSpec spec{};
        std::vector<Uint8> pcm;
        if (sdlSfxLoad(sfx, spec, pcm)) {
            const int bytes = SDL_AUDIO_BYTESIZE(spec.format), frames = int(pcm.size()) / std::max(1, bytes * spec.channels);
            w.seconds = frames / double(std::max(1, spec.freq));
            const int columns = 240;
            w.peaks.assign(columns, 0.0f);
            for (int i = 0; i < frames; ++i) {
                float v = 0;
                for (int ch = 0; ch < spec.channels; ++ch) {
                    const Uint8* q = pcm.data() + (size_t(i) * spec.channels + ch) * bytes;
                    if (spec.format == SDL_AUDIO_S16LE) v = std::max(v, std::abs(int16_t(q[0] | q[1] << 8) / 32768.0f));
                    else if (spec.format == SDL_AUDIO_U8) v = std::max(v, std::abs((q[0] - 128) / 128.0f));
                }
                float& peak = w.peaks[size_t(int64_t(i) * columns / std::max(1, frames))];
                peak = std::max(peak, v);
            }
            w.ok = true;
        }
    }
    peaks = w.peaks, seconds = w.seconds;
    return w.ok;
}

// A menu of the core's items (core/data_nav.inc) at `at` of `from`, then "Report feedback on <on>..." as the Windows
// menus end; a pick runs `pick` with the item's id.
void sdlDataMenu(draw::View& from, draw::Point at, const MenuModel& items, std::function<void(int)> pick, const std::wstring& on) {
    sdlPopupMenu(from, at, sdlModelItems(items, std::move(pick)), on);  // (sdl_menus.inc)
}

// Undo and redo in the Data workspace (Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z while it shows): the step's table or text as it
// was, at the entry that changed. `done`: the status bar's text after it ("Undo: Change Marine (Units)").
bool sdlDataUndo(bool redo, const std::wstring& done) {
    sdlDataCommitEdit();
    if (dataMapUndo(redo)) {  // the Players section's edits: the map's undo and redo (core/views_data_players.inc)
        updateTitle();
        gSdlDataForm.invalidate();
        if (!done.empty()) setMessage(done);
        return true;
    }
    DataEditStep s;
    std::string err;
    if (!dataApplyUndo(redo, s, err)) {
        setMessage(redo ? L"Nothing to redo" : L"Nothing to undo");
        return false;
    }
    for (DataEditStep more; gSdlDataState.history.nextIn(redo, s.group);)  // (the other steps of the same change)
        if (std::string e2; !dataApplyUndo(redo, more, e2)) break;
        else if (err.empty()) err = e2;
    if (s.tab >= 0 && s.tab < dataTabCount()) {
        const bool sameTab = s.tab == gSdlDataState.datTab;
        const std::set<int> picked = gSdlDataState.picked;  // (an undo in the same tab keeps the list's selection: the Table's rows)
        gSdlDataState.tabEntry.resize(size_t(dataTabCount()), 0);
        gSdlDataState.tabEntry[size_t(s.tab)] = s.entry;  // (an extra's step too: its tab and entry are where it was made)
        sdlDataShowTab(s.tab);
        const bool keep = sameTab && picked.count(s.entry);
        if (keep) gSdlDataState.picked = picked;
        sdlDataSelect(s.entry, keep);
    }
    sdlDataRebuildList();
    setMessage(!err.empty() ? widen(err) : !done.empty() ? done : redo ? L"Redo" : L"Undo");
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
    int viewKind() override { return extraTab(gSdlDataState.datTab) ? -1 : gSdlDataState.table ? 1 : 0; }
    void setViewKind(int k) override;
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
    bool picked(int e) override { return gSdlDataState.picked.count(e) != 0; }
    int pickedCount() override { return int(gSdlDataState.picked.size()); }
    void pick(int e, unsigned mods) override {
        DataModel& d = gSdlDataState;
        if (d.picked.empty()) d.picked = {d.entry};
        dataListPick(d.picked, d.pickAnchor, d.shown, e, mods);
        if (d.picked.empty()) d.picked = {e};  // (Ctrl+click on the only one: it stays)
        // (Ctrl+click took e out: the open entry is the selected row nearest to it)
        int open = e;
        if (!d.picked.count(e)) {
            auto it = d.picked.lower_bound(e);
            open = it != d.picked.end() ? *it : *d.picked.rbegin();
        }
        sdlDataSelect(open, true);
        sdlDataPicksChanged();
    }
    void pickAll() override {
        DataModel& d = gSdlDataState;
        d.picked.insert(d.shown.begin(), d.shown.end());
        sdlDataPicksChanged();
    }
    void menu(int e, draw::Point pt) override;
    bool travel(int button) override { return sdlDataTravel(button == 1), true; }
    void open(int e) override;
};
SdlDataListHost gSdlDataListHost;
DataListView gSdlDataList(gSdlDataListHost);
// A double click on an SFX data row: its sound plays (or pauses, as the form's Play button).
void SdlDataListHost::open(int e) {
    if (curTable() && curTable()->schema().name == "sfxdata") sdlSfxToggle(e), gSdlDataForm.invalidate();
}
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
// click areas are this host's), and so are the Units form's Display section and the map's Players section (the core's
// paint: core/views_data_display.inc, core/views_data_players.inc). A click runs when the mouse goes up over its area.
class SdlDataFormHost final : public DataFormViewHost, public DataPicturesHost {
public:
    struct Area {
        draw::Rect r;
        std::function<void()> click;
        std::wstring tip;
        draw::Cursor cursor = draw::Cursor::Arrow;
        std::function<void(draw::Point)> drag;  // a picture's part: while the mouse is held (core/views_data_pictures.inc)
        std::vector<std::string> fields;        // the fields a diagram's part shows (marked while the mouse is over it)
        bool edit = false;                      // its drag changes the entry: saved (one undo step) when the mouse is let go
        std::function<void()> release;          // after a drag: when the mouse is let go (a row dropped in its place)
    };
    std::vector<Area> areas;
    std::function<void()> pressed;  // a link pressed: its click when the mouse goes up
    std::function<void(draw::Point)> dragging;  // a picture's part pressed: it follows the mouse until it goes up
    bool dragMoved = false, dragEdits = false;
    std::function<void()> released;  // the pressed part's release (after a drag)

    static std::string dat() { return curTable() ? curTable()->schema().name : std::string(); }
    // The topmost area under p (`clicks`: the topmost one with a click or a drag; a row's area without one does not hide
    // the boxes under it, as on Windows).
    const Area* areaAt(draw::Point p, bool clicks = false) const {
        for (auto it = areas.rbegin(); it != areas.rend(); ++it)
            if (it->r.contains(p) && (!clicks || it->click || it->drag)) return &*it;
        return nullptr;
    }

    // The pictures beside the fields (core/views_data_pictures.inc): their areas, the mouse, the repaint.
    void pictureArea(draw::Rect r, std::function<void()> click, std::function<void(draw::Point)> drag, const std::wstring& tip,
                     draw::Cursor cursor) override {
        areas.push_back({r, std::move(click), tip, cursor, std::move(drag)});
    }
    bool pictureMouse(draw::Point& p) override {
        draw::Host* h = gSdlDataForm.host();
        if (!h) return false;
        p = h->mouse(gSdlDataForm);
        return p.x >= 0 && p.y >= 0 && p.x < gSdlDataForm.width() && p.y < gSdlDataForm.height();
    }
    void pictureRepaint() override { gSdlDataForm.invalidate(); }
    // The diagrams' parts (core/views_data_diagrams.inc): the fields they show, the drags that change the entry, the sound.
    void pictureEdit(draw::Rect r, std::function<void(draw::Point)> drag, std::function<void()> click, const std::wstring& tip, draw::Cursor cursor,
                     std::vector<std::string> fields) override {
        const bool edit = drag != nullptr;
        areas.push_back({r, std::move(click), tip, cursor, std::move(drag), std::move(fields), edit});
    }
    bool fieldHot(const std::string& field) override { return hot_.count(field) != 0; }
    bool pictureDragging() override { return dragging != nullptr; }
    bool sfxWave(int sfx, std::vector<float>& peaks, double& seconds) override { return sdlSfxWave(sfx, peaks, seconds); }
    double sfxPlayed(int sfx, bool& playing) override { return sdlSfxPlayed(sfx, playing); }
    void sfxToggle(int sfx) override { sdlSfxToggle(sfx); }

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
    void beforePaint() override { areas.clear(), datapics::gHandles.clear(); }
    bool paintExtraTab(draw::Canvas& c, int& y) override { return sdlPaintExtraTab(c, y); }
    void afterPaint(draw::Canvas&) override;
    draw::Image entryIcon() override { return sdlDataImage(dataEntryPicture(gSdlDataState.datTab, gSdlDataState.entry)); }
    UiIcon tabIcon() override { return ::tabIcon(gSdlDataState.datTab); }
    int nameString() override {
        const int sid = entryNameString(dat(), gSdlDataState.entry);
        return sid > 0 && gSdlDataState.project && sid <= gSdlDataExtras.strings.count() ? sid : 0;
    }
    void rename(int sid, draw::Rect link) override {  // the name in the game text pop-out under Rename (its colors, Preview and Raw)
        std::string text = gSdlDataExtras.strings.text(sid - 1);
        const std::wstring title = shellTr(L"Name in the game (stat_txt.tbl string ") + std::to_wstring(sid) + L")";
        if (!shellEditGameText(gSdlDataForm, link, title, text)) return;
        gSdlDataExtras.strings.set(sid - 1, text);
        sdlExtrasChanged();
        sdlDataRebuildList();
        setMessage(shellTr(L"Renamed: ") + statString(sid));
    }
    void openString(int sid) override { sdlDataGoTo("strings", sid); }
    std::wstring fieldNote(const DatField& f, uint32_t v) override { return ::fieldNote(f, v); }
    std::wstring readOnlyWhy(const DatField& f) override { return datFieldReadOnly(dat(), f, gSdlDataState.entry); }
    draw::Image refIcon(const std::string& d, uint32_t v) override { return sdlDataImage(dataRefPicture(d, v)); }
    int widgets(draw::Canvas& c, int section, int x, int y, int w) override {  // (the window paints each frame: the image plays on)
        if (dat() == "images") imagePlayerTick();
        bool moving = false;
        return paintDataPictures(c, *this, dat(), section, gSdlDataState.entry, x, y, w, moving);
    }
    void soundButton(draw::Canvas& c, draw::Rect r, int sfx) override {  // a play / pause button for a sound (main.cpp: soundButton)
        bool playing = false;
        sdlSfxPlayed(sfx, playing);
        kitpaint::drawGlyph(c, r, kitpaint::Ico(playing ? UiIcon::Pause : UiIcon::Play), kAccent);
        areas.push_back({r, [sfx] { sdlSfxToggle(sfx), gSdlDataForm.invalidate(); }, playing ? L"Pause" : L"Play", draw::Cursor::Hand});
    }
    int unitDisplay(draw::Canvas& c, int y) override { return sdlPaintUnitDisplay(c, gSdlDataState.entry, y); }  // (sdl_data_extras.inc)
    int requirements(draw::Canvas& c, int, int y, int) override { return sdlPaintRequirements(c, dat(), gSdlDataState.entry, y); }
    int mapPlayers(draw::Canvas& c, int x0, int y, int colW, int labelW, int rowH) override {  // (core/views_data_players.inc)
        return paintMapPlayers(c, gSdlMapPlayers, dat(), gSdlDataState.entry, x0, y, colW, labelW, rowH);
    }
    void area(draw::Rect r, std::function<void()> click, const std::wstring& tip, draw::Cursor cursor) override {
        areas.push_back({r, std::move(click), tip, cursor});
    }
    bool widgetPress(draw::Point p) override {
        const Area* a = areaAt(p, true);
        if (!a || (!a->click && !a->drag)) return false;
        sdlDataCommitEdit();
        pressed = a->click, dragging = a->drag, dragMoved = false, dragEdits = a->edit, released = a->release;
        if (dragging && !pressed) dragMoved = true, dragging(p);  // a press sets the value too (as on Windows)
        return true;
    }
    bool widgetDrag(draw::Point p) override {
        if (!dragging) return false;
        dragMoved = true;
        dragging(p);
        gSdlDataForm.invalidate();
        return true;
    }
    void widgetRelease(bool lost) override {
        auto click = std::exchange(pressed, nullptr);
        const bool moved = std::exchange(dragMoved, false), edits = std::exchange(dragEdits, false);
        auto release = std::exchange(released, nullptr);
        dragging = nullptr;
        if (moved && release) release(), gSdlDataForm.invalidate();
        else if (moved && edits) sdlDataSaved(), gSdlDataForm.invalidate();  // a diagram's drag: the change is one undo step
        else if (click && !lost && !moved) click();
    }
    bool widgetHover(draw::Point p, std::set<std::string>& fields) override {  // (the form repaints: the hot boxes follow the mouse)
        for (auto it = areas.rbegin(); it != areas.rend() && fields.empty(); ++it)
            if (it->r.contains(p)) fields.insert(it->fields.begin(), it->fields.end());
        return true;
    }
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
    void dropdown(const DatField& f, draw::Rect box) override {  // the list picker under the box, with the values' icons
        const DatTable* t = curTable();
        if (!t) return;
        std::string picked;
        if (sdlChoose(datLabel(f.name), dataDropdownItems(f), std::to_string(t->value(f, gSdlDataState.entry)), picked, box))
            gSdlDataForm.setValue(f, uint32_t(std::stoul(picked)));
    }
    std::vector<ListPickItem> dropdownItems(const DatField& f) override { return dataDropdownItems(f); }
    void valueSet() override { sdlDataSaved(); }
    void paintNow() override {}  // (the entry follows its box at each paint: placeEntry)
    void goTo(const std::string& d, int e) override { sdlDataGoTo(d, e); }
    void fieldMenu(const DatField& f, draw::Point p) override;
    void status(const std::wstring& text) override { setMessage(text); }
    bool travel(int button) override { return sdlDataTravel(button == 1), true; }
    void help(const DatField& f) override {
        const stardraft::HelpTopic t = stardraft::helpFieldTopic(dat(), f.name);
        sdlShowHelp(t.page + (t.anchor.empty() ? "" : "#" + t.anchor));
    }
    bool hasHelp(const DatField& f) override { return !stardraft::helpFieldTopic(dat(), f.name).anchor.empty(); }

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

// --- The Table view (core/views_data_table.inc): the open .dat with an entry a row ------------------------------------

class SdlDataTableHost final : public DataTableViewHost {
public:
    DatTable* table() override { return extraTab(gSdlDataState.datTab) ? nullptr : curTable(); }
    // The list's selected entries in its order when there are 2 or more, else all its rows.
    const std::vector<int>& rows() override {
        const DataModel& d = gSdlDataState;
        rows_.clear();
        if (!rowsPicked()) return rows_ = d.shown;
        for (int e : d.shown)
            if (d.picked.count(e)) rows_.push_back(e);
        return rows_;
    }
    bool rowsPicked() override {
        const DataModel& d = gSdlDataState;
        int n = 0;
        for (int e : d.shown) n += d.picked.count(e) != 0;
        return n >= 2;
    }
    int openEntry() override { return gSdlDataState.entry; }
    draw::Image icon(int e) override { return sdlDataImage(dataTabEntryPicture(gSdlDataState.datTab, e)); }
    draw::Image refIcon(const std::string& dat, uint32_t v) override { return sdlDataImage(dataRefPicture(dat, v)); }
    std::wstring note(const DatField& f, uint32_t v) override { return ::fieldNote(f, v); }
    std::vector<ListPickItem> dropdownItems(const DatField& f) override { return dataDropdownItems(f); }
    bool choose(const DatField& f, uint32_t current, draw::Rect box, uint32_t& out) override {
        std::string picked;
        if (!sdlChoose(datLabel(f.name), dataDropdownItems(f), std::to_string(current), picked, box, &gSdlDataTable)) return false;
        out = uint32_t(std::stoul(picked));
        return true;
    }
    void saved(const std::wstring& what) override {
        sdlDataSaved();
        setMessage(what);
        sdlDataRepaint();
    }
    void open(int e) override {  // a row's number: the entry's form (the list keeps its selection)
        sdlDataSelect(e, gSdlDataState.picked.count(e) != 0);
        gSdlDataWorkspaceHost.setViewKind(0);
    }
    void importCsv() override;
    void exportCsv() override;
    void status(const std::wstring& text) override { setMessage(text); }
    bool travel(int button) override { return sdlDataTravel(button == 1), true; }

private:
    std::vector<int> rows_;
};
SdlDataTableHost gSdlDataTableHost;
DataTableView gSdlDataTable(gSdlDataTableHost);

// The project's folder (a CSV file's place at first), else the Documents folder.
static fs::path sdlDataCsvFolder() {
    const EudProject* pr = gSdlDataState.project;
    return pr ? pr->folder() : shellDocumentsFolder();
}

void SdlDataTableHost::exportCsv() {
    const DatTable* t = table();
    if (!t) return;
    ShellFilePick d;
    d.title = shellTr(L"Export the table as a CSV file");
    d.filters = {{L"CSV file (*.csv)", L"*.csv"}};
    d.defaultExtension = L"csv", d.fileName = widen(t->schema().name) + L".csv", d.initialDir = sdlDataCsvFolder(), d.save = true;
    const fs::path file = shellPickFile(d);
    if (file.empty()) return;
    const std::vector<int>& r = rows();
    // (The byte order mark: spreadsheets then read the file as UTF-8.)
    const std::string text = "\xEF\xBB\xBF" + narrow(dataGridText(dataTableGrid(*t, dataTableColumns(t->schema()), r), L','));
    std::ofstream out(file, std::ios::binary);
    if (!out.write(text.data(), std::streamsize(text.size()))) return shellErrorBox(L"Could not export the table to " + file.wstring(), shellTr(L"Export CSV"));
    setMessage(shellTr(L"Exported ") + std::to_wstring(r.size()) + shellTr(L" rows to ") + file.filename().wstring());
}

void SdlDataTableHost::importCsv() {
    DatTable* t = table();
    if (!t) return;
    ShellFilePick d;
    d.title = shellTr(L"Import a CSV file into ") + widen(t->schema().name) + L".dat";
    d.filters = {{L"CSV file (*.csv;*.tsv;*.txt)", L"*.csv;*.tsv;*.txt"}};
    d.initialDir = sdlDataCsvFolder();
    const fs::path file = shellPickFile(d);
    if (file.empty()) return;
    std::ifstream in(file, std::ios::binary);
    if (!in) return shellErrorBox(L"Could not import the file " + file.wstring(), shellTr(L"Import CSV"));
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::wstring text = dataFileText(bytes);
    const DataImportResult res = dataTableImport(*t, dataGridRead(text, dataGridSeparator(text)));
    if (!res.error.empty()) return shellErrorBox(res.error, shellTr(L"Import CSV"));
    const std::wstring done = shellTr(L"Imported ") + std::to_wstring(res.changed) + (res.changed == 1 ? shellTr(L" value in ") : shellTr(L" values in ")) +
                              std::to_wstring(res.entries) + (res.entries == 1 ? shellTr(L" entry") : shellTr(L" entries"));
    if (res.changed) sdlDataSaved();  // (one undo step)
    setMessage(done);
    sdlDataRepaint();
    std::wstring problems;
    if (!res.unknown.empty()) {
        problems += shellTr(L"Not imported, because ") + widen(t->schema().name) + L".dat " + shellTr(L"has no such columns:") + L"\n";
        for (size_t i = 0; i < res.unknown.size() && i < 12; ++i) problems += L"  " + res.unknown[i] + L"\n";
        if (res.unknown.size() > 12) problems += L"  ... (" + std::to_wstring(res.unknown.size() - 12) + L")\n";
    }
    auto count = [](int n, const wchar_t* one, const wchar_t* more) { return std::to_wstring(n) + shellTr(n == 1 ? one : more) + L"\n"; };
    if (res.notValues) problems += count(res.notValues, L" cell is not a value of its column.", L" cells are not values of their columns.");
    if (res.locked) problems += count(res.locked, L" cell cannot change (read only, or not used by the entry).", L" cells cannot change (read only, or not used by the entry).");
    if (res.noEntry) problems += count(res.noEntry, L" row has an id that is not an entry.", L" rows have an id that is not an entry.");
    if (!res.firstProblem.empty()) problems += L"\n" + shellTr(L"First cell not imported: ") + res.firstProblem;
    if (!problems.empty()) shellWarningBox(done + L".\n\n" + problems, shellTr(L"Import CSV"));
}

// Entry | Table: the form, or the Table view of the .dat (it takes the References panel's place too).
void SdlDataWorkspaceHost::setViewKind(int k) {
    DataModel& d = gSdlDataState;
    if (extraTab(d.datTab)) return;
    if ((k == 1) == d.table) {  // (already shown: it gets the keyboard)
        if (d.table) gSdlDataTable.focus();
        return;
    }
    sdlDataCommitEdit();
    if (gSdlDataTable.editing()) gSdlDataTable.commitEdit(false);
    d.table = k == 1;
    gSdlDataWorkspace.relayout();
    gSdlDataTable.refresh();
    if (d.table) gSdlDataTable.focus();
    else static_cast<draw::View&>(gSdlDataForm).focus();
    setMessage(d.table ? shellTr(L"Opened the Table view. Select 2 or more entries in the list to show only those.")
                       : shellTr(L"Opened the entry's form"));
    sdlDataRepaint();
}
// View > Data table (Ctrl+E): the Data workspace with its Table view; in it, the entry's form again.
void sdlDataTableCommand() {
    if (gWorkspace != Workspace::Data) {
        setWorkspace(Workspace::Data);
        if (extraTab(gSdlDataState.datTab)) sdlDataShowTab(0);
        gSdlDataWorkspaceHost.setViewKind(1);
        return;
    }
    if (extraTab(gSdlDataState.datTab)) return setMessage(shellTr(L"The Table view shows only the .dat tabs"));
    gSdlDataWorkspaceHost.setViewKind(gSdlDataState.table ? 0 : 1);
}
// The list's selection changed: the table's rows follow; the status says how many are selected.
void sdlDataPicksChanged() {
    const size_t n = gSdlDataState.picked.size();
    if (n > 1) setMessage(shellTr(L"Selected ") + std::to_wstring(n) + shellTr(L" entries") + (gSdlDataState.table ? L"" : shellTr(L" (Ctrl+E shows them in a table)")));
    gSdlDataTable.refresh();
    sdlDataRepaint();
}
void SdlDataFormHost::fieldMenu(const DatField& f, draw::Point p) {  // the field's menu (core/data_nav.inc), at the mouse
    if (!curTable()) return;
    const DatField* field = &f;  // (a field of the open table: it stays while the menu is open)
    sdlDataMenu(gSdlDataForm, p, dataFieldMenuModel(f), [field](int pick) { dataFieldMenuPick(*field, pick); }, widen(f.name));
}
void sdlFormArea(draw::Rect r, std::function<void()> click, const std::wstring& tip, bool hand) {
    gSdlDataFormHost.area(r, std::move(click), tip, hand ? draw::Cursor::Hand : draw::Cursor::Arrow);
}
bool sdlFormHot(draw::Rect r) { return gSdlDataFormHost.hot(r); }
void dataSetValue(const DatField& f, uint32_t v) { gSdlDataForm.setValue(f, v); }  // (the core's diagrams: core/views_data_diagrams.inc)
void sdlFormDrag(draw::Rect r, std::function<void(draw::Point)> drag, std::function<void()> click, const std::wstring& tip, draw::Cursor cursor,
                 std::function<void()> release) {
    const bool edit = release == nullptr;  // (no release: the drag changes the entry, saved when the mouse is let go)
    gSdlDataFormHost.areas.push_back({r, std::move(click), tip, cursor, std::move(drag), {}, edit, std::move(release)});
}
void sdlFormAreaCursor(draw::Rect r, std::function<void()> click, const std::wstring& tip, draw::Cursor cursor) {
    gSdlDataFormHost.area(r, std::move(click), tip, cursor);
}

// --- The workspace view: the container and the three panes, each with its bar --------------------------------------

struct SdlDataView final : draw::View {
    SdlBarFrame listFrame{gSdlDataList}, formFrame{gSdlDataForm}, refsFrame{gSdlDataRefs};
    bool ready = false;

    SdlDataView() {
        add(gSdlDataWorkspace, draw::Rect(0, 0, 0, 0));
        add(listFrame, draw::Rect(0, 0, 0, 0));
        add(formFrame, draw::Rect(0, 0, 0, 0));
        add(refsFrame, draw::Rect(0, 0, 0, 0));
        add(gSdlDataTable, draw::Rect(0, 0, 0, 0));
        gSdlDataTable.setVisible(false);
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
    // The Table view: in the form's and the References panel's place.
    if (gSdlDataState.table && !extraTab(gSdlDataState.datTab)) {
        place(gSdlDataTable, {form.left, form.top, std::max(form.right, refs.right), form.bottom});
        gSdlDataTable.setVisible(true);
        if (gSdlData.formFrame.visible()) {
            const bool focused = sdlDataFocusIn(gSdlData.formFrame);  // (the keyboard goes to the table, not to the hidden form)
            gSdlData.formFrame.setVisible(false);
            if (focused) gSdlDataTable.focus();
        }
        gSdlData.refsFrame.setVisible(false);
        return;
    }
    if (gSdlDataTable.visible()) sdlDataHide(gSdlDataTable);
    gSdlData.formFrame.setVisible(true);
    gSdlData.refsFrame.setVisible(true);
    place(gSdlData.formFrame, form);
    place(gSdlData.refsFrame, refs);
}

void sdlDataRepaint() {
    gSdlData.invalidate();
    gSdlDataList.invalidate();
    gSdlDataForm.invalidate();
    gSdlDataRefs.invalidate();
    gSdlDataWorkspace.invalidate();
    gSdlDataTable.invalidate();
}

// A tab with this project closed (forgetProject): the number entry goes, and the model drops the project's tables.
void sdlDataForgetProject(const EudProject* pr) {
    if (!pr || gSdlDataState.project != pr) return;
    gSdlDataState.editing = -1;
    gSdlDataEntry.setVisible(false);
    dataForgetProject(pr);
    sdlDataRepaint();
}

// --- The snapshot's entry: --data <tab>:<entry>[:<scroll>] (the Data workspace opens the entry as it is shown) -----------------------

// --textpop rename|tooltip|unitname (after a frame: the form's click areas are made as it paints): the game text pop-out
// that Rename (the Data form), the first tooltip's box (the Buttons tab) or the Map data page's unit Rename opens shows in
// the picture (sdl_popshot.inc), as a click opens it.
void sdlTextPopShot(const std::string& which) {
    if (which == "unitname") {
        if (ClassicPageView* page = gSdlPage.classic[kClassicUnits].get()) popShotOpen([page] { page->renameUnit(); });
        return;
    }
    const std::wstring tip = which == "rename" ? L"Change the name that the game shows" : L"Edit the text (string";
    for (const auto& a : gSdlDataFormHost.areas)
        if (a.click && a.tip.rfind(tip, 0) == 0) {
            const std::function<void()> click = a.click;
            popShotOpen(click);
            return;
        }
}

// --check imageplayer:<anim>:<ticks> (after --data images:<n>): the image player's run of that animation, a line for
// each tick with what the stage draws (core/data_pictures.inc: imagePlayerRunText). False: not this check.
bool sdlDataCheck(const std::string& what, std::string& out) {
    if (what.rfind("imageplayer:", 0) != 0) return false;
    const int anim = std::atoi(what.c_str() + 12);
    const size_t colon = what.find(':', 12);
    const int ticks = colon == std::string::npos ? 48 : std::clamp(std::atoi(what.c_str() + colon + 1), 1, 2000);
    if (!curTable() || curTable()->schema().name != "images") return out = "image player: no images.dat entry (WRONG)\n", true;
    out = "image player: image " + std::to_string(gSdlDataState.entry) + " animation " + std::to_string(anim) + "\n" +
          imagePlayerRunText(gSdlDataState.entry, anim, ticks);
    return true;
}

// --check dataform (after the snapshot: the last paint's parts): the Data form's width and columns, its parts
// (core/views_data_form.inc: DataFormPart) and its buttons (the click areas of the pictures and the links), each
// with its rectangle in the form.
void sdlDataFormCheck() {
    const DataFormLayout lay = dataFormLayout(gSdlDataForm.width());
    std::printf("data form: %d px wide, %s\n", gSdlDataForm.width(), lay.twoCol ? "two columns" : "one column");
    for (const DataFormPart& p : gSdlDataForm.parts())
        std::printf("form part: %s %d,%d,%d,%d\n", p.what.c_str(), p.r.left, p.r.top, p.r.right, p.r.bottom);
    for (const auto& a : gSdlDataFormHost.areas)
        if (a.click && !a.drag)
            std::printf("form button: %d,%d,%d,%d %s\n", a.r.left, a.r.top, a.r.right, a.r.bottom, narrow(a.tip.substr(0, 40)).c_str());
    // The converted values after the raw values (stardraft/dat_units.h), as the paint showed them.
    const DatTable* t = curTable();
    for (const auto& [name, note] : gSdlDataForm.notes())
        if (const DatField* f = t ? datFieldNamed(t->schema().name, name) : nullptr; f && *stardraft::datFieldUnit(t->schema().name, name).unit)
            std::printf("form value: %s raw %u, shown \"%s\"\n", name.c_str(), unsigned(t->value(*f, gSdlDataState.entry)), narrow(note).c_str());
    if (const draw::Rect hi = gSdlDataForm.helpIcon(); hi.right > hi.left)
        std::printf("form help icon: %d,%d,%d,%d (the field %s)\n", hi.left, hi.top, hi.right, hi.bottom, gSdlDataForm.helpField().c_str());
}

// --check datatable (after the snapshot): the Table view as the last paint had it: its rows and columns, the selected
// cells and their text (as Ctrl+C copies it), the buttons' places, and the list's selected entries.
void sdlDataTableCheck() {
    DataModel& d = gSdlDataState;
    std::printf("data table: %s, %s\n", d.table ? "shown" : "hidden", gSdlDataTable.visible() ? "visible" : "not visible");
    std::string picked;
    for (int e : d.picked) picked += (picked.empty() ? "" : ",") + std::to_string(e);
    std::printf("data table: list selection %s (open %d)\n", picked.c_str(), d.entry);
    const auto& cols = gSdlDataTable.columns();
    std::printf("data table: %zu rows, %zu columns, frame %d,%d,%d,%d\n", gSdlDataTableHost.rows().size(), cols.size(), gSdlDataTable.frame().left,
                gSdlDataTable.frame().top, gSdlDataTable.frame().right, gSdlDataTable.frame().bottom);
    const DataTableSelection& s = gSdlDataTable.selection();
    std::printf("data table: active cell %d,%d; %zu cells selected\n", s.row, s.col, s.cells().size());
    std::string text = narrow(gSdlDataTable.selectionText());
    for (char& ch : text)
        if (ch == '\t') ch = '|';
    std::printf("data table: selection text:\n%s", text.c_str());
}

void sdlDataSelect(const std::string& spec) {
    const size_t colon = spec.find(':');
    if (colon == std::string::npos) return;
    sdlDataLoad();
    gSdlData.ready = true;
    sdlDataGoTo(spec.substr(0, colon), std::atoi(spec.c_str() + colon + 1));
    if (const size_t more = spec.find(':', colon + 1); more != std::string::npos)  // tab:entry:scroll (px; a large one: the form's end)
        gSdlDataState.formScroll = std::max(0, std::atoi(spec.c_str() + more + 1));
    if (spec.size() > 5 && spec.compare(spec.size() - 5, 5, ":code") == 0) gImagePlayer.showCode = true;  // (images:<n>:<scroll>:code: the script's code)
    if (spec.substr(0, colon) == "portdata") {  // a portrait: the picture shows it once its video is decoded (10 s at most)
        const auto clips = portraitClips(portraitPath(gSdlDataState.entry));
        for (int i = 0; i < 200 && !clips->ready.load(std::memory_order_acquire); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
