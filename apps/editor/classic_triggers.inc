// Scenario > Classic triggers (roadmap L.2; included into main.cpp after the workspaces): the map's TRIG, as StarEdit
// edits it. Only for a classic map, or a project whose map has classic triggers (L.4: they stay in the map and run with
// the project's triggers). One window: at the left the triggers (shown for a player or for all), at the right the
// selected trigger's players, conditions and actions, and under them the selected condition or action: its type and
// a picker for each value (players, units, the map's locations, switches, sounds and strings, numbers, ...).
// The page's model, its commands, the lists, the head, the side (the search and the filter), the editor, the pickers,
// the menus and the keys are the core's (core/views_classic_triggers.inc, core/views_classic_page.inc), for both
// programs; this file is the Windows program's part (CtPageHost): the windows of the parts, the native menus, the text
// pop-out (the game text editor) and the tests. Keys on a list: Delete, Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+D, Ctrl+Up,
// Ctrl+Down, Up, Down.
// A page in the main area (map_pages.inc); it edits a working copy, and Apply writes TRIG (and the new strings) as one
// undo step (Editor::setClassicTriggers).

// Control ids.
enum : int {
    kCtDim = 1,      // a dim label
    kCtHead = 3,     // a section heading (a line under it)
};
struct ClassicTrigDialog : CtModel {  // (the model: core/views_classic_triggers.inc)
    HWND wnd = nullptr, list = nullptr, note = nullptr, entries = nullptr;
    HWND side = nullptr;                   // the side's window: a view window of sideView (the search, the count, the filter)
    HWND head = nullptr;                   // the head's window: a view window of headView (the trigger, its buttons, the players, Forces)
    HWND editor = nullptr;                 // the editor's window: a view window of editorView (the selected entry's type and values)
    std::unique_ptr<CtSideView> sideView;  // (core/views_classic_page.inc; made with the page's window)
    std::unique_ptr<CtHeadView> headView;  // (core/views_classic_triggers.inc; made with the page's window)
    std::unique_ptr<CtEditorView> editorView;  // (core/views_classic_page.inc; made with the page's window)
    // The painted lists: the triggers (list) and the conditions and actions (entries). A pane is a list's view (core) and the
    // window's host of it; made when the list's window first gets a message.
    struct Pane {
        std::unique_ptr<CtListView> view;
        std::unique_ptr<ui::Win32Host> host;  // (after the view: it goes first)
    };
    Pane listPane, entryPane;
};
ClassicTrigDialog* gCt = nullptr;  // the working copy of the page that shows (map_pages.inc)
HWND gCtValueDlg = nullptr;        // the value pop-out (ctValueProc) while it is open
CtValueView* gCtValueView = nullptr;  // the number pop-out (core/views_classic_value.inc) while it is open
extern ListPickView* gListPick;       // the list picker (core/views_list_pick.inc) while it is open (main.cpp)
void mapPageChanged();

ClassicNames ctNames() {
    ClassicNames n;
    n.unit = [](uint32_t u) { return narrow(unitDisplayName(int(u))); };
    n.location = [](uint32_t l) {
        const int i = int(l) - 1;
        return i >= 0 && i < app.ed->locationCount() ? app.ed->locationName(i) : std::string();
    };
    n.string = [](uint32_t id) { return gameTextStripped(gCt ? gCt->strings.get(id) : app.ed->strings().get(id)); };  // (without the codes)
    n.switchName = [](uint32_t s) { return app.ed->switchName(int(s)); };
    n.force = [](uint32_t f) { return app.ed->force(int(f)).name; };
    n.ai = gCt ? &gCt->ai : nullptr;
    return n;
}

ClassicTrigger* ctTrigger() { return gCt ? ctModelSelectedMut(*gCt) : nullptr; }
ClassicCondition* ctCondition() { return gCt ? ctModelCondition(*gCt) : nullptr; }
ClassicAction* ctAction() { return gCt ? ctModelAction(*gCt) : nullptr; }

// After a change: the painted lists show it.
void ctUpdateHeader();  // (below)
void ctChanged() {
    if (!gCt) return;
    InvalidateRect(gCt->list, nullptr, FALSE);
    InvalidateRect(gCt->entries, nullptr, FALSE);
    ctUpdateHeader();  // the count of actions (a block, a trigger's rows) is the change's
    if (!gCt->hold) mapPageChanged();
}

void ctSelectTrigger(int index);
void ctSelectEntry(int kind, int entry);
void ctBuildEditor();
void ctLayout(HWND hwnd);  // (below)

// A point of a view (`at` in its coordinates) on the screen.
POINT ctScreenPoint(const draw::View& from, draw::Point at) {
    const draw::Point o = from.origin();
    POINT p{o.x + at.x, o.y + at.y};
    if (HWND w = ui::Win32Host::windowOf(from)) ClientToScreen(w, &p);
    return p;
}

// Runs the text pop-out (the game text editor): below.
struct CtValueDialog;
bool ctValueRun(CtValueDialog& st);
bool ctEditTextNative(const std::wstring& title, const std::wstring& label, std::string& text);

// The page's commands' host (core/views_classic_page.inc, CtPageHost): the page's windows and the program's state.
class CtWinPageHost final : public CtPageHost {
public:
    ClassicNames names() override { return ctNames(); }
    void filled() override {  // the count of the shown triggers, and the list painted again
        ClassicTrigDialog& d = *gCt;
        if (d.sideView) d.sideView->setCount(std::to_wstring(d.shown.size()) + L" of " + std::to_wstring(d.trig.triggers.size()));
        InvalidateRect(d.list, nullptr, FALSE);
    }
    void selectTrigger(int i) override { ctSelectTrigger(i); }
    void updateHeader() override { ctUpdateHeader(); }
    void changed() override { ctChanged(); }
    void invalidate() override { InvalidateRect(gCt->list, nullptr, FALSE); }
    void message(const std::wstring& text) override { setMessage(text); }
    bool ctrl() override { return GetKeyState(VK_CONTROL) < 0; }
    bool shift() override { return GetKeyState(VK_SHIFT) < 0; }

    CtModel& model() override { return *gCt; }
    Editor& editor() override { return *app.ed; }
    void selectEntry(int kind, int entry) override { ctSelectEntry(kind, entry); }
    void buildEditor() override { ctBuildEditor(); }
    void resetListScroll() override {
        if (gCt->listPane.view) gCt->listPane.view->resetScroll();
    }
    void entriesLaid() override {  // the entries' height changed: the editor follows it
        if (gCt && gCt->wnd) PostMessageW(gCt->wnd, WM_SIZE, 0, 0);
    }
    uint32_t soundLength(const std::string& name) override { return ::soundLength(name); }
    bool pickOne(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, const draw::View& from, draw::Point at,
                 std::string& out) override {
        return showListPicker(title, std::move(items), current, ctScreenPoint(from, at), out);
    }
    bool pickMany(const std::wstring& title, std::vector<ListPickItem> items, std::set<std::string>& checked, const std::wstring& okText,
                  const draw::View& from, draw::Point at) override {
        return showListPickerMulti(title, std::move(items), checked, okText, ctScreenPoint(from, at));
    }
    // The number pop-out is a kit dialog (core/views_classic_value.inc): over the page's window, and the page waits until it closes.
    bool editNumber(const std::wstring& title, const std::wstring& label, const std::wstring& current, std::wstring& out) override {
        gMainAnchor.host.ownerWindow(gCt && gCt->wnd ? gCt->wnd : app.main);
        const int r = runValueDialog(gMainAnchor.view, title, label, current, out, gCtValueView);
        gMainAnchor.host.ownerWindow(app.main);
        return r == kValueOk;
    }
    bool editText(const std::wstring& title, const std::wstring& label, std::string& text) override { return ctEditTextNative(title, label, text); }
    // The block dialog (core/views_classic_batch.inc) is over the page's window, and the page waits until it closes.
    int editBlock(BatchHost& bh, ClassicBatch& b, bool editing) override {
        gMainAnchor.host.ownerWindow(gCt && gCt->wnd ? gCt->wnd : app.main);
        const int r = runBatchDialog(gMainAnchor.view, bh, b, editing, gCtBatchView);
        gMainAnchor.host.ownerWindow(app.main);
        return r;
    }
    // The program's menu of the items (a separator for id 0; a disabled item is grayed).
    void menu(const draw::View& from, draw::Point at, const std::vector<CtMenuItem>& items, std::function<void(int)> run) override {
        HMENU m = CreatePopupMenu();
        for (const CtMenuItem& it : items) {
            if (!it.id) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            else AppendMenuW(m, MF_STRING | (it.enabled ? 0 : MF_GRAYED), UINT_PTR(it.id), it.text.c_str());
        }
        const POINT s = ctScreenPoint(from, at);
        const int pick = popupMenu(m, TPM_LEFTALIGN, s.x, s.y, ui::Win32Host::windowOf(from));
        DestroyMenu(m);
        if (pick && run) run(pick);
    }
};
CtWinPageHost gCtModelHost;
CtPageList gCtListProgram(gCtModelHost);  // the painted lists' program part (core/views_classic_page.inc)
// The list's rows (the model's: core/views_classic_triggers.inc).
void ctFillList() { ctModelFill(*gCt, gCtModelHost); }

// The selected trigger's row in view (the list's view scrolls to it).
void ctShowSelected() {
    ClassicTrigDialog& d = *gCt;
    const auto at = std::find(d.shown.begin(), d.shown.end(), d.sel);
    if (at == d.shown.end() || !d.listPane.view) return;
    d.listPane.view->revealShown(int(at - d.shown.begin()));
}

void ctSelectEntry(int kind, int entry) {
    ClassicTrigDialog& d = *gCt;
    d.kind = kind, d.entry = entry;
    if (d.wnd) ctLayout(d.wnd);  // the editor's height follows the entry's values
    ctBuildEditor();
    InvalidateRect(d.entries, nullptr, FALSE);
}
// The editor shows the selected entry (core/views_classic_page.inc: CtEditorView).
void ctBuildEditor() {
    if (gCt && gCt->editorView) gCt->editorView->show(gCtModelHost);
}

// The header of the right side: the selected trigger's name, its action count and its buttons.
void ctUpdateHeader() { ctUpdateHead(*gCt->headView, *gCt); }

// Shows the selected trigger: its players and lists (the first condition selected).
void ctSelectTrigger(int index) {
    ClassicTrigDialog& d = *gCt;
    d.sel = index >= 0 && index < int(d.trig.triggers.size()) ? index : -1;
    d.collapsed.clear();
    const ClassicTrigger* t = ctTrigger();
    ctShowPlayers(*d.headView, d);
    ctUpdateHeader();
    if (d.entryPane.view) d.entryPane.view->resetScroll();
    ctShowSelected();
    ctSelectEntry(0, t && t->conditionCount() ? 0 : -1);
    ctChanged();
}

// The choices of a picker (core: ctKindChoices) and the commands the tests use, with this page's host.
std::vector<std::pair<uint32_t, std::wstring>> ctKindChoices(ClassicKind k) { return ctKindChoices(gCtModelHost, k); }
void ctSetValue(int i, uint32_t v) { ctSetValue(gCtModelHost, i, v); }
void ctSetText(int i, const std::string& text) { ctSetText(gCtModelHost, i, text); }
void ctValuePopout(int i) { ctValuePopout(gCtModelHost, i); }
bool ctAddEntry(int kind, uint8_t type) { return ctAddEntry(gCtModelHost, kind, type); }
void ctAddTrigger() { ctModelAddTrigger(*gCt, gCtModelHost); }
void ctToggleOne(int i) { ctModelToggleOne(*gCt, gCtModelHost, i); }
void ctBatchClick(ClassicBatchKind kind, int editRow) { ctBatchClick(gCtModelHost, kind, editRow); }
void ctForcesClick() {
    if (gCt && gCt->headView) ctForcesClick(gCtModelHost, gCt->headView->forces);
}

// The text pop-out (PRINCIPLES: values are edited in a pop-out with OK and Cancel): a text in the game text editor (its
// colors and align buttons). Cancel and a click outside leave the value as it was. The number pop-out is a kit dialog
// (core/views_classic_value.inc; CtWinPageHost::editNumber).
struct CtValueDialog {
    std::wstring title, label;
    std::string textValue;
    HWND textBox = nullptr;
};
constexpr int kCvText = 1102, kCvLabel = 1103;
struct CtItem {
    DWORD style;
    short x, y, w, h;
    WORD id, cls;
    std::wstring text;
};
// A dialog template: the title, the size in dialog units and the items (a class 0x80 button, 0x81 edit, 0x82 static).
std::vector<WORD> ctDialogTemplate(const std::wstring& title, short w, short h, const std::vector<CtItem>& items) {
    std::vector<WORD> t;
    auto align = [&] { if (t.size() % 2) t.push_back(0); };
    auto str = [&](const std::wstring& s) { for (wchar_t c : s) t.push_back(c); t.push_back(0); };
    auto dword = [&](DWORD d) { t.push_back(LOWORD(d)); t.push_back(HIWORD(d)); };
    dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER);
    dword(0);
    t.push_back(WORD(items.size()));
    t.push_back(0), t.push_back(0), t.push_back(w), t.push_back(h);
    t.push_back(0), t.push_back(0);
    str(title);
    t.push_back(9);
    str(L"Segoe UI");
    for (const CtItem& it : items) {
        align();
        dword(it.style | WS_CHILD | WS_VISIBLE);
        dword(0);
        t.push_back(it.x), t.push_back(it.y), t.push_back(it.w), t.push_back(it.h), t.push_back(it.id);
        t.push_back(0xFFFF), t.push_back(it.cls);
        str(it.text);
        t.push_back(0);
    }
    align();
    return t;
}

INT_PTR CALLBACK ctValueProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto state = [dlg] { return reinterpret_cast<CtValueDialog*>(GetWindowLongPtrW(dlg, DWLP_USER)); };
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        gCtValueDlg = dlg;
        CtValueDialog* st = state();
        for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SendMessageW(c, WM_SETFONT, WPARAM(app.font), TRUE);
        SetDlgItemTextW(dlg, IDOK, L"Set");
        SetDlgItemTextW(dlg, IDCANCEL, L"Cancel");
        SetDlgItemTextW(dlg, kCvLabel, st->label.c_str());
        {  // the game text editor takes the place of the placeholder
            HWND holder = GetDlgItem(dlg, kCvText);
            RECT r;
            GetWindowRect(holder, &r);
            MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&r), 2);
            DestroyWindow(holder);
            GameTextSpec spec;
            spec.palette = TextPalette::Game, spec.multiline = true, spec.align = true, spec.crlf = true;
            const int th = gameTextHeight(spec, r.right - r.left, scale(96));
            st->textBox = gameTextCreate(dlg, kCvText, {r.left, r.top, r.right, r.top + th}, spec);
            gameTextSet(st->textBox, st->textValue);
            SetFocus(gameTextFocusBox(st->textBox));
        }
        for (int id : {IDOK, IDCANCEL}) darkControl(GetDlgItem(dlg, id), L"DarkMode_Explorer");
        BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(dlg, 20, &dark, sizeof dark), titleColors(dlg);
        return FALSE;  // (the focus is set above)
    }
    if (msg == WM_CTLCOLOREDIT) {
        SetTextColor(HDC(wp), kText), SetBkColor(HDC(wp), kCtl);
        return INT_PTR(app.ctlBrush);
    }
    if (msg == WM_CTLCOLORSTATIC) {
        SetTextColor(HDC(wp), kDim), SetBkColor(HDC(wp), kBg);
        return INT_PTR(app.bgBrush);
    }
    if (msg == WM_CTLCOLORDLG) return INT_PTR(app.bgBrush);
    if (msg != WM_COMMAND) return FALSE;
    CtValueDialog* st = state();
    const int id = LOWORD(wp);
    if (id == IDOK) {
        st->textValue = gameTextGet(st->textBox);
        EndDialog(dlg, IDOK);
        return TRUE;
    }
    if (id == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// Runs a value pop-out: true when OK was clicked (st holds the value).
bool ctValueRun(CtValueDialog& st) {
    std::vector<CtItem> items;
    const short w = 330, h = 176;
    items.push_back({SS_LEFT, 8, 8, 310, 10, kCvLabel, 0x0082, L""});
    items.push_back({SS_LEFT, 8, 20, 310, 100, kCvText, 0x0082, L""});
    items.push_back({WS_TABSTOP | BS_DEFPUSHBUTTON, short(w - 116), short(h - 22), 50, 14, IDOK, 0x0080, L""});
    items.push_back({WS_TABSTOP | BS_PUSHBUTTON, short(w - 60), short(h - 22), 50, 14, IDCANCEL, 0x0080, L""});
    const std::vector<WORD> t = ctDialogTemplate(st.title, w, h, items);
    const INT_PTR r = DialogBoxIndirectParamW(app.inst, reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), app.main, ctValueProc, LPARAM(&st));
    gCtValueDlg = nullptr;
    return r == IDOK;
}

// The text pop-out of the page (CtPageHost::editText): true when Set was clicked (`text` holds the new text).
bool ctEditTextNative(const std::wstring& title, const std::wstring& label, std::string& text) {
    CtValueDialog st;
    st.title = title, st.label = label, st.textValue = text;
    if (!ctValueRun(st)) return false;
    text = st.textValue;
    return true;
}

// The window procedure of the two lists: the list's view (core) takes the messages it paints, the mouse and the keys
// (core/views_classic_page.inc: ctListKey); the rest are the page's.
LRESULT CALLBACK ctListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ClassicTrigDialog* d = gCt;
    if (!d || (hwnd != d->list && hwnd != d->entries)) return DefWindowProcW(hwnd, msg, wp, lp);
    const bool isList = hwnd == d->list;
    ClassicTrigDialog::Pane& pane = isList ? d->listPane : d->entryPane;
    if (!pane.host) {  // made with the list's window's first message
        pane.view = std::make_unique<CtListView>(gCtListProgram, isList);
        pane.host = std::make_unique<ui::Win32Host>(*pane.view);
        (isList ? gCtListProgram.list : gCtListProgram.entries) = pane.view.get();
    }
    LRESULT r = 0;
    if (pane.host->message(hwnd, msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The page's parts at the size of its window (core/views_classic_page.inc: ctPageLayout): the left column (the side,
// the list, the note) and the right one (the selected trigger's head, the entries, the editor under them). The lists
// take the height that is left.
void ctLayout(HWND hwnd) {
    ClassicTrigDialog& d = *gCt;
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int step = scale(ui::kFormRowH), gap = scale(8);
    const bool noteShown = GetWindowTextLengthW(d.note) > 0;
    const CtPageRects L = ctPageLayout(W, H, noteShown, d.entryPane.view ? d.entryPane.view->contentHeight() : 0, ctEditorHeight(d, step, gap));
    // The least height the parts take: a shorter window makes the page taller, and its view scrolls.
    if (H < L.needH) {
        SetWindowPos(hwnd, nullptr, 0, 0, W, L.needH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;  // (the size change comes back here)
    }
    auto place = [](HWND h, draw::Rect r) {
        if (h) MoveWindow(h, r.left, r.top, std::max(0, r.width()), std::max(0, r.height()), TRUE);
    };
    place(d.side, L.side);
    place(d.list, L.list);
    place(d.note, L.note);
    place(d.head, L.head);  // (its parts are in its coordinates, from 0)
    ctPlaceHead(*d.headView, L.rightW);
    place(d.entries, L.entries);
    place(d.editor, L.editor);
    ctShowSelected();  // the selected trigger in view at this size
    InvalidateRect(hwnd, nullptr, FALSE);
}

// A view window of the page that shows `view` (a kit view: the side, the head, the editor).
HWND ctViewWindow(HWND page, draw::View& view) {
    return CreateWindowExW(WS_EX_CONTROLPARENT, ui::kViewClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0, page, nullptr,
                           app.inst, &view);
}

LRESULT CALLBACK classicTrigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ClassicTrigDialog* d = gCt;
    if (!d) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        d->wnd = hwnd;
        d->query.clear(), d->filter = -1, d->marked.clear(), d->anchor = -1;  // the search and the filter start empty, as their boxes do
        // the lists' hosts go before their views (the host detaches from its view when it goes; a move assignment frees the view first)
        d->listPane.host.reset(), d->listPane.view.reset();
        d->entryPane.host.reset(), d->entryPane.view.reset();  // the lists' views and hosts of the page that went (new ones with the new windows)
        gCtListProgram.list = gCtListProgram.entries = nullptr;
        const ClassicNames n = ctNames();
        // Left: the side (the search box: the triggers whose text has its words; the count; the triggers of a player), the list.
        {
            std::vector<std::wstring> names;
            std::vector<int> groups;
            ctFilterItems(n, names, groups);
            d->sideView = std::make_unique<CtSideView>(names, groups);
            d->sideView->onSearch = [](const std::wstring& q) {
                ctSetQuery(gCtModelHost, q);
                mapPageChanged();
            };
            d->sideView->onFilter = [](int group) {
                ctSetFilter(gCtModelHost, group);
                mapPageChanged();
            };
            d->side = ctViewWindow(hwnd, *d->sideView);
        }
        d->note = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_LEFT, 0, 0, 0, 0, hwnd, HMENU(INT_PTR(kCtDim)), app.inst, nullptr);
        SendMessageW(d->note, WM_SETFONT, WPARAM(app.font), TRUE);
        auto painted = [&] {
            HWND l = CreateWindowExW(0, L"StarDraftClassicTrigList", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL, 0, 0, 0, 0, hwnd, nullptr, app.inst, nullptr);
            darkControl(l, L"DarkMode_Explorer");  // the scroll bar in the theme
            return l;
        };
        d->list = painted();
        d->entries = painted();
        // Right: the head (the trigger, its buttons, its players and Forces), the editor of the selected entry.
        {
            std::vector<std::wstring> labels;
            for (int g = 0; g < kCtBoxCount; ++g) labels.push_back(widen(classicPlayerName(uint32_t(kCtBoxGroups[g]), n)));
            CtHeadView::Callbacks cb;
            cb.player = [](int box, bool on) {
                ctModelPlayerBox(*gCt, gCtModelHost, box, on);
                mapPageChanged();
            };
            cb.turnOff = [] {
                ctModelToggleTriggers(*gCt, gCtModelHost);
                mapPageChanged();
            };
            cb.deleteTrig = [] {
                ctModelDeleteTriggers(*gCt, gCtModelHost);
                mapPageChanged();
            };
            cb.forces = [] {
                ctForcesClick();
                mapPageChanged();
            };
            d->headView = std::make_unique<CtHeadView>(labels, std::move(cb));
            d->head = ctViewWindow(hwnd, *d->headView);
        }
        {
            CtEditorView::Callbacks cb;
            cb.type = [](uint8_t type) {
                ctSetType(gCtModelHost, type);
                mapPageChanged();
            };
            cb.flag = [](uint8_t bit, bool on) {
                ctSetFlag(gCtModelHost, bit, on);
                mapPageChanged();
            };
            cb.value = [](int param, const draw::View& field) {
                ctValueClick(gCtModelHost, param, field, {0, 0, field.width(), field.height()});
                mapPageChanged();
            };
            d->editorView = std::make_unique<CtEditorView>(std::move(cb));
            d->editor = ctViewWindow(hwnd, *d->editorView);
        }
        // A note under the list: a project's map (L.4), EUD values.
        SetWindowTextW(d->note, ctNoteText(*d, projectMode()).c_str());
        ctLayout(hwnd);
        ctFillList();
        ctSelectTrigger(d->shown.empty() ? -1 : d->shown[0]);
        return 0;
    }
    case WM_SIZE:
        if (gCt && gCt->list) ctLayout(hwnd);
        return 0;
    case WM_ERASEBKGND: {  // the theme's ground (the class brush is the one of the theme at the first open)
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(HDC(wp), &rc, app.bgBrush);
        return 1;
    }
    case WM_PAINT: {  // the line under each section heading (kCtHead), as ui::sectionHeader draws it
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            if (GetDlgCtrlID(c) == kCtHead) {
                RECT r;
                GetWindowRect(c, &r);
                MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
                fillAlpha(dc, {r.left, r.bottom, r.right, r.bottom + 1}, kBorder, 255);
            }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        HDC dc = HDC(wp);
        const HWND c = HWND(lp);
        SetTextColor(dc, !IsWindowEnabled(c) ? kFaint : GetDlgCtrlID(c) == kCtDim ? kDim : kText);
        const bool field = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
        SetBkColor(dc, field ? kCtl : kBg);
        return LRESULT(field ? app.ctlBrush : app.bgBrush);
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// L.4: a project made from a classic map keeps its classic data in the map. Does the project's map have classic
// triggers (any trigger in TRIG), or classic settings (a unit, upgrade or tech that does not use the game's values, or
// players with their own values)? Read again only when the map changed.
bool classicSettingsUsed(const Editor& ed) {
    const SettingDefaults* def = ed.settingDefaults();
    if (!def) return false;
    auto own = [](const std::array<bool, kSettingPlayers>& usesDefault) {
        return std::any_of(usesDefault.begin(), usesDefault.end(), [](bool b) { return !b; });
    };
    const UnitSettings u = ed.unitSettings();
    for (int i = 0; i < kSettingUnits; ++i)
        if (!u.units[size_t(i)].useDefault || !u.availability[size_t(i)].global || own(u.availability[size_t(i)].usesDefault)) return true;
    const UpgradeSettings g = ed.upgradeSettings();
    for (int i = 0; i < kSettingUpgrades; ++i)
        if (!g.upgrades[size_t(i)].useDefault || own(g.levels[size_t(i)].usesDefault)) return true;
    const TechSettings t = ed.techSettings();
    for (int i = 0; i < kSettingTechs; ++i)
        if (!t.techs[size_t(i)].useDefault || own(t.players[size_t(i)].usesDefault)) return true;
    return false;
}

bool classicCommandInProject(int id) {
    static const Editor* ed = nullptr;
    static uint64_t revision = 0;
    static bool triggers = false, settings = false;
    if (!app.ed || !app.ed->isOpen()) return false;
    if (ed != app.ed.get() || revision != app.ed->revision()) {
        ed = app.ed.get(), revision = app.ed->revision();
        triggers = !app.ed->classicTriggers().triggers.empty();
        settings = classicSettingsUsed(*app.ed);
    }
    return id == ID_SCEN_CLASSICTRIG ? triggers : id == ID_SCEN_CLASSIC ? settings : false;
}

// The Classic triggers page's working copy from the map.
void ctLoad(ClassicTrigDialog& d) {
    d = ClassicTrigDialog{};
    ctModelLoad(d, *app.ed);  // (the strings, the triggers and their batches as the page shows them)
    d.ai = loadClassicAiScripts(app.data);
}

// Something to apply: a trigger differs from the map's when the page read them. (A text typed in an action makes a
// string and changes the action, so the triggers tell it too.)
bool ctCopyChanged(const ClassicTrigDialog& d) { return ctModelChanged(d); }

// Apply: TRIG (and the new strings) as one undo step (Editor::setClassicTriggers). The values the page does not know
// (EUD values, the bytes after the last trigger, the flags) stay as they are.
bool ctApply(ClassicTrigDialog& d) {
    std::wstring message;
    if (ctModelApply(d, *app.ed, message) == kCtApplySet) {
        updateTitle();
        app.dirty = true;
    }
    setMessage(message);
    return true;
}

// --- Tests --------------------------------------------------------------------------------------------------------
void pump();
bool capture(const fs::path& file, HWND wnd);
HWND mapPageWindow();                 // map_pages.inc: the page that shows
bool mapPageClickButton(bool apply);  // map_pages.inc: Apply (or Revert) clicked, as the mouse does

// Picks the filter's group (-1: all triggers) or the selected entry's type, as a click in its list does.
void ctPickFilter(int group) { gCt->sideView->pickGroup(group); }
void ctPickType(uint8_t type) { gCt->editorView->pickType(type); }
// The index of the selected entry's parameter with this name ("n", "unit", "text", ...), or -1.
int ctParamIndex(const char* name) {
    const ClassicCondition* c = ctCondition();
    const ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def) return -1;
    for (size_t i = 0; i < def->params.size(); ++i)
        if (std::string(def->params[i].name) == name) return int(i);
    return -1;
}
// Sets the selected entry's parameter with this name to a value (as its list or pop-out does).
void ctSetParam(const char* name, uint32_t v) {
    const int i = ctParamIndex(name);
    if (i < 0) return;
    ctSetValue(gCtModelHost, i, v);
    ctBuildEditor();
}

size_t ctTriggerCount(const Bytes& trig) {
    Chk c;
    c.sections.push_back({"TRIG", trig});
    return readClassicTriggers(c).triggers.size();
}

// Drags row `from` of a painted list (0 the triggers, 1 the conditions, 2 the actions) by its grip to place `to`, as the
// mouse does (core: CtListView::dragRow). `shot`: a capture while the row is at the place (the drop line).
bool ctDragRow(HWND wnd, CtListView* view, int list, int from, int to, const fs::path& shot = {}) {
    RedrawWindow(wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    if (!view) return false;
    const bool done = view->dragRow(list, from, to, [wnd, shot] {
        if (shot.empty()) return;
        RedrawWindow(wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        capture(shot, GetParent(wnd));
    });
    for (int i = 0; i < 2; ++i) pump();
    return done;
}

/// The modal pop-outs of the datashots: a picker, a value pop-out, the block dialog and a right-click menu are run by their
// own loops, which dispatch timers. The timer captures the pop-out and closes it (ctModalShot).
fs::path gCtShotFile;                 // where the pop-out's picture goes
enum class CtShot { None, Picker, Dialog, Menu };
CtShot gCtShotKind = CtShot::None;
VOID CALLBACK ctShotTimer(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    switch (gCtShotKind) {
    case CtShot::Picker:  // (the picker's window closes with WM_CLOSE: it is a cancel)
        if (const HWND pick = gListPick ? ui::Win32Host::windowOf(*gListPick) : nullptr) capture(gCtShotFile, pick), PostMessageW(pick, WM_CLOSE, 0, 0);
        break;
    case CtShot::Dialog: {
        if (gCtValueDlg) capture(gCtShotFile, gCtValueDlg), PostMessageW(gCtValueDlg, WM_COMMAND, IDCANCEL, 0);
        else if (gCtValueView) {
            if (HWND w = ui::Win32Host::windowOf(*gCtValueView)) capture(gCtShotFile, w);
            gCtValueView->cancel();
        } else if (gCtBatchView) {
            if (HWND w = ui::Win32Host::windowOf(*gCtBatchView)) capture(gCtShotFile, w);
            gCtBatchView->cancel();
        }
        break;
    }
    case CtShot::Menu:
        if (const HWND m = openMenuWindow()) capture(gCtShotFile, m);
        closeMenus();
        break;
    case CtShot::None: break;
    }
    gCtShotKind = CtShot::None;
}
// Runs `open` (a modal pop-out of the page) with the timer that captures it to `file`. The log says what the shot did.
void ctModalShot(CtShot kind, const fs::path& file, const std::function<void()>& open) {
    gCtShotFile = file, gCtShotKind = kind;
    const UINT_PTR timer = SetTimer(nullptr, 0, 500, ctShotTimer);  // (a timer of the thread: its id is the one returned)
    open();
    if (timer) KillTimer(nullptr, timer);
    gCtShotKind = CtShot::None;
}

// Datashots classictrig: (the page: classic_triggers.png), classictrig:set (a trigger made through the page: for
// player 1, Bring 5 men to Anywhere; Create 1 Zergling, then Show a text; Apply, saved, opened again, undone) and
// classictrig:move (triggers, then an action, moved by a drag: in the list of all triggers and in a player's list;
// Apply; the map has the new order; one undo gives back TRIG byte for byte). classictrig.txt says what holds.
void classicTrigDatashot(const std::string& spec, const fs::path& dir) {
    std::ofstream log(dir / "classictrig.txt", std::ios::app);
    static std::string mode;
    static fs::path out;
    static std::vector<ClassicTrigger> expected;  // move: the order the window must have before OK
    static std::vector<std::string> moveLog;
    mode = spec.substr(spec.find(':') + 1), out = dir, expected.clear(), moveLog.clear();
    const bool set = mode == "set", move = mode == "move", batch = mode == "batch";
    ClassicTrigger madeCopy;  // batch: the trigger that was made, as the page has it
    int madeAt = -1;          // its place in the list (ctAddTrigger puts it after the selected one)
    const Bytes before = app.ed ? app.ed->chk().effective("TRIG") : Bytes();
    int shownTriggers = -1;
    command(ID_SCEN_CLASSICTRIG);  // the page, in the main area
    if (gWorkspace == Workspace::ClassicTriggers && gCt) {
        const HWND w = mapPageWindow();
        for (int i = 0; i < 4; ++i) pump();
        shownTriggers = int(gCt->shown.size());
        const std::string light = gLightTheme ? "_light" : "";
        // The states of the page (classictrig:<state>): the page as the map has it, then the state. A pop-out is captured
        // by its own timer (classictrig_popout_<state>.png) and closed; the page is captured after it.
        if (gBackground && mode == "text")  // the native text pop-out activates its window: not in a background run
            log << mode << ": NOT captured (its dialog takes the focus; run it in the foreground)\n";
        const std::string scaleName = app.dpi >= 180 ? "_200" : "";  // (dpi:192 before the spec: 200 %)
        const fs::path popout = out / ("classictrig_popout_" + mode + scaleName + light + ".png");
        if (mode == "search") {  // a search with results: "create" in the box
            gCt->sideView->search.setText(L"create");  // (its change runs the search, as the typing does)
        } else if (mode == "off") {  // a turned-off trigger: the first one of the list
            ctSelectTrigger(0);
            ctToggleOne(0);
        } else if (mode == "add") {  // the add-action picker with the Blocks group first
            ctSelectTrigger(0);
            ctModalShot(CtShot::Picker, popout, [] {
                if (gCt->entryPane.view) ctAddClick(gCtModelHost, 1, *gCt->entryPane.view, draw::Rect{scale(10), 0, scale(200), scale(30)});
            });
        } else if (mode == "value" || (mode == "text" && !gBackground)) {  // a number (a Bring's count; a kit dialog) or a text (Display text message): OK and Cancel
            ctAddTrigger();
            if (mode == "value") ctAddEntry(0, 3);  // Bring: its count is the number
            else ctAddEntry(1, 9);                  // Display text message: its text
            const int pi = ctParamIndex(mode == "value" ? "n" : "text");
            ctModalShot(CtShot::Dialog, popout, [pi] { ctValuePopout(pi); });
        } else if (mode == "forces") {  // the forces of the trigger: a multi-pick list
            ctSelectTrigger(0);
            ctModalShot(CtShot::Picker, popout, [] { ctForcesClick(); });
        } else if (mode == "values") {  // a block's values: the searchable list in multi-pick, three checked
            std::vector<ListPickItem> items;
            for (const auto& [value, label] : ctKindChoices(ClassicKind::Unit)) {
                if (value > 227) continue;
                ListPickItem it;
                it.label = label, it.value = std::to_string(value);
                items.push_back(std::move(it));
            }
            std::set<std::string> checked{"0", "1", "37"};
            ctModalShot(CtShot::Picker, popout, [&] { showListPickerMulti(L"Search values", items, checked, L"Set", POINT{scale(400), scale(200)}); });
        } else if (mode == "blockdlg") {  // the block dialog for a new block (For each unit): a kit dialog, also in a background run
            ctSelectTrigger(0);
            ctModalShot(CtShot::Dialog, popout, [] { ctBatchClick(ClassicBatchKind::Unit, -1); });
        } else if (mode == "menu") {  // the right-click menu of the first trigger's row
            ctSelectTrigger(0);
            ctModalShot(CtShot::Menu, popout, [] {
                if (gCt->listPane.view) ctTriggerMenu(gCtModelHost, 0, *gCt->listPane.view, draw::Point{scale(60), scale(70)});
            });
        }
        if (mode == "set") {
            ctPickFilter(0);  // player 1's triggers
            ctAddTrigger();
            ctPickType(3);  // Bring
            ctSetParam("n", 5);
            ctSetParam("unit", 230);  // men
            ctAddEntry(1, 9);  // Display text message
        if (const int i = ctParamIndex("text"); i >= 0) ctSetText(i, "Hello from StarDraft 2");  // (the text's pop-out: OK in it)
            ctAddEntry(1, 9);
            ctPickType(44);  // Create unit
            ctSetParam("unit", 37);  // Zergling
            ctSetParam("player", 0);
            ctDragRow(gCt->entries, gCt->entryPane.view.get(), 2, 1, 0);  // Create unit first, by a drag
        } else if (mode == "move") {
            auto& v = gCt->trig.triggers;
            auto order = [&](const std::vector<ClassicTrigger>& want) { return v == want ? "yes" : "NO"; };
            if (v.size() < 3) {
                moveLog.push_back("move: NOT enough triggers (3 needed, " + std::to_string(v.size()) + ")");
            } else {
                // All triggers: the first one dragged to the third place.
                std::vector<ClassicTrigger> want = v;
                std::rotate(want.begin(), want.begin() + 1, want.begin() + 3);
                const bool dragged = ctDragRow(gCt->list, gCt->listPane.view.get(), 0, 0, 2, out / ("classic_triggers_drag" + light + ".png"));
                moveLog.push_back(std::string("all triggers: the first trigger dragged to the third place ") + (dragged ? "" : "(drag NOT done) ") + order(want));
                // A player's triggers: the third one in the list dragged to the top; the triggers that do not show
                // keep their places.
                int group = -1;
                for (int g : kCtGroups) {
                    std::vector<int> s;
                    for (int i = 0; i < int(v.size()); ++i)
                        if (v[size_t(i)].runsFor(g)) s.push_back(i);
                    if (s.size() >= 3 && s.size() < v.size() && (group < 0 || s[2] - s[0] > 2)) group = g;
                }
                if (group < 0) {
                    moveLog.push_back("player's triggers: NOT a player with 3 triggers and triggers that do not show");
                } else {
                    ctPickFilter(group);
                    const std::vector<int> s = gCt->shown;
                    want = v;
                    ClassicTrigger t = want[size_t(s[2])];
                    want.erase(want.begin() + s[2]);
                    want.insert(want.begin() + s[0], t);
                    const bool d2 = ctDragRow(gCt->list, gCt->listPane.view.get(), 0, 2, 0);
                    moveLog.push_back("player's triggers (group " + std::to_string(group) + ", triggers " + std::to_string(s[0] + 1) + " and " +
                                      std::to_string(s[2] + 1) + ", " + std::to_string(s[2] - s[0] - 2) + " not shown between): the third dragged to the top " +
                                      (d2 ? "" : "(drag NOT done) ") + order(want));
                }
                // An action: the first one dragged under the second.
                ctPickFilter(-1);
                int withActions = -1;
                for (int i = 0; i < int(v.size()) && withActions < 0; ++i)
                    if (v[size_t(i)].actionCount() >= 2) withActions = i;
                if (withActions < 0) {
                    moveLog.push_back("action: NOT a trigger with 2 actions");
                } else {
                    ctSelectTrigger(withActions);
                    want = v;
                    want[size_t(withActions)].moveAction(0, 1);
                    const bool d3 = ctDragRow(gCt->entries, gCt->entryPane.view.get(), 2, 0, 1);
                    moveLog.push_back("action: the first action of trigger " + std::to_string(withActions + 1) + " dragged under the second " +
                                      (d3 ? "" : "(drag NOT done) ") + order(want));
                }
                expected = v;
            }
        } else if (mode == "batch") {
            {  // New blocks are not offered while kClassicBatchesOffered is off; the map's blocks still work (below).
                const auto items = ctAddItems(1);
                const bool none = std::none_of(items.begin(), items.end(), [](const ListPickItem& it) { return it.value.rfind("block:", 0) == 0; });
                log << "batch: the action picker offers new blocks only when they are on " << (none != kClassicBatchesOffered ? "yes" : "NO") << "\n";
            }
            // A trigger for player 1 with Create unit (a Zergling). The batch dialog is made as the window makes it,
            // but not shown (no focus): it selects Terran Marine, Terran Firebat and Zergling, then reads them back.
            // Then the unit is the batch's variable.
            // A block of units (Terran Marine, Terran Firebat, Zergling) with a block of players (Player 1, Player 2)
            // in it, and Create unit in the inner block: its unit is the outer variable, its player the inner one.
            ctPickFilter(0);
            ctAddTrigger();
            ClassicTrigger* t = ctTrigger();
            ClassicAction outer;
            outer.type = kClassicBlockType, outer.depth = 0;
            outer.batch = std::make_shared<ClassicBatch>(ClassicBatch{"unit", ClassicBatchKind::Unit, {0, 1, 37}});
            ClassicAction inner;
            inner.type = kClassicBlockType, inner.depth = 1;
            inner.batch = std::make_shared<ClassicBatch>(ClassicBatch{"player", ClassicBatchKind::Player, {0, 1}});
            ClassicAction make = makeClassicAction(44);  // Create unit
            make.depth = 2, make.var[2] = 1, make.var[1] = 2;
            t->addAction(outer, -1), t->addAction(inner, -1), t->addAction(make, -1);
            madeCopy = *t, madeAt = gCt->sel;
            ctFillList();
            ctSelectEntry(1, 1);  // the inner block: the picture shows a block in a block
            log << "nested batch: " << (t->actionCount() == 3 ? "yes" : "NO") << "\n";
            ctChanged();  // the blocks are an edit of the page: Apply writes them
        } else if (!gCt->shown.empty()) {
            ctSelectEntry(1, 0);
        }
        mapPageChanged();
        RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        for (int i = 0; i < 10; ++i) pump(), Sleep(20);
        capture(out / ("classic_triggers" + (mode.empty() ? std::string() : "_" + mode) + scaleName + light + ".png"), app.main);
        log << "state " << (mode.empty() ? std::string("page") : mode) << ": list scroll " << (gCt->listPane.view ? gCt->listPane.view->scrollPos() : 0) << ", entries scroll " << (gCt->entryPane.view ? gCt->entryPane.view->scrollPos() : 0) << ", list height "
            << (gCt->listPane.view ? gCt->listPane.view->contentHeight() : 0) << ", " << gCt->shown.size() << " triggers shown\n";
        if (set || move || batch) log << "Apply " << (mapPageClickButton(true) ? "clicked" : "NOT clicked") << "\n";
    }
    command(ID_WS_MAP);  // leave the page (with no changes left, the map is as Apply left it)
    for (int i = 0; i < 4; ++i) pump();
    if (!app.ed) return;
    if (!set && !move && !batch) {  // a state of the page changed the map (a trigger made, turned off): undo it back
        int steps = 0;
        while (steps < 30 && app.ed->chk().effective("TRIG") != before) app.ed->undo(), ++steps;
        log << "the state's changes are undone " << (app.ed->chk().effective("TRIG") == before ? "yes" : "NO") << " (" << steps << " undo steps)\n";
    }
    const size_t count = ctTriggerCount(app.ed->chk().effective("TRIG")), had = ctTriggerCount(before);
    log << spec << ": the page " << (shownTriggers >= 0 ? "opened" : "did NOT open") << ", " << shownTriggers << " triggers in the list (" << had
        << " in the map)\n";
    if (batch) {
        // The map has the template and the three triggers the batch makes; read back, the batch is the same; undone.
        const ClassicTriggers saved = app.ed->classicTriggers();
        log << "the map has one trigger more (the blocks make six copies in it) " << (count == had + 1 ? "yes" : "NO") << "\n";
        // Each edit is one undo step (the page writes it into the map at once): the variable was the last one.
        const ClassicTriggers working = classicCollapseBatches(saved, app.ed->strings());
        const bool same = madeAt >= 0 && size_t(madeAt) < working.triggers.size() && working.triggers[size_t(madeAt)] == madeCopy;
        log << "read back: the blocks are the same (the outer unit block with the player block in it) " << (same ? "yes" : "NO") << "\n";
        std::string ext = app.ed->path().extension().string(), err;
        const fs::path file = dir / ("classictrig_batch" + (ext.empty() ? std::string(".scx") : ext));
        Editor back(app.data);
        const bool reopened = app.ed->save(file, err) && back.open(file, err);
        const ClassicTriggers reread = reopened ? classicCollapseBatches(back.classicTriggers(), back.strings()) : ClassicTriggers{};
        const bool again = madeAt >= 0 && size_t(madeAt) < reread.triggers.size() && reread.triggers[size_t(madeAt)] == madeCopy;
        log << "saved and opened again: the same blocks " << (reopened && again ? "yes" : "NO " + err) << "\n";
        // The page makes an undo step for each change (adding the trigger, adding the block): undo back to the start.
        int steps = 0;
        while (steps < 6 && app.ed->chk().effective("TRIG") != before) app.ed->undo(), ++steps;
        log << "undo steps back to the map before the page changed it: " << steps << (app.ed->chk().effective("TRIG") == before ? " (back at the start)" : " (NOT back at the start)")
            << "\n";
        return;
    }
    if (move) {
        for (const std::string& s : moveLog) log << s << "\n";
        if (expected.empty()) return;
        log << "Apply: the map has the new order " << (app.ed->classicTriggers().triggers == expected ? "yes" : "NO") << "\n";
        app.ed->undo();
        log << "one undo step gives back the map's TRIG byte for byte " << (app.ed->chk().effective("TRIG") == before ? "yes" : "NO") << "\n";
        return;
    }
    if (!set) {
        log << "no Apply: no change " << (app.ed->chk().effective("TRIG") == before ? "yes" : "NO") << "\n";
        return;
    }
    auto check = [&](Editor& e, const char* what) {
        const ClassicTriggers t = e.classicTriggers();
        const ClassicTrigger* made = nullptr;
        for (const ClassicTrigger& tr : t.triggers)
            if (tr.conditions[0].type == 3 && tr.conditions[0].amount == 5 && tr.conditions[0].unit == 230) made = &tr;
        const bool ok = made && made->runsFor(0) && made->actionCount() == 2 && made->actions[0].type == 44 && made->actions[0].unit == 37 &&
                        made->actions[0].player == 0 && made->actions[1].type == 9 && e.strings().get(made->actions[1].text) == "Hello from StarDraft 2";
        log << what << ": " << t.triggers.size() << " triggers; the new one (player 1, Bring 5 men, Create unit, then the text) " << (ok ? "yes" : "NO")
            << "\n";
    };
    log << "one trigger more " << (count == had + 1 ? "yes" : "NO") << "\n";
    check(*app.ed, "set");
    std::string ext = app.ed->path().extension().string(), err;
    const fs::path file = dir / ("classictrig_set" + (ext.empty() ? std::string(".scx") : ext));
    Editor back(app.data);
    if (app.ed->save(file, err) && back.open(file, err)) check(back, "saved and opened again");
    else log << "save: NO (" << err << ")\n";
    // The page makes an undo step for each change (the trigger, its player, each entry): undo back to the start.
    int steps = 0;
    while (steps < 30 && app.ed->chk().effective("TRIG") != before) app.ed->undo(), ++steps;
    log << "undo steps back to the map's TRIG byte for byte: " << steps << (app.ed->chk().effective("TRIG") == before ? " (back at the start)" : " (NOT back at the start)")
        << "\n";
}

// Datashot classicproject: (L.4) the open classic map made a project as File > Project > Make this map a project
// does (into <dir>/classicproject/). The project's map keeps TRIG, MBRF and the settings sections byte-identical;
// Scenario > Classic triggers is there in the project (Map data is not: classic maps only), with the note
// (classic_triggers_project.png). classicproject.txt says what holds. (classic_trigger_test --migrate builds one.)
void classicProjectDatashot(const fs::path& dir) {
    std::ofstream log(dir / "classicproject.txt");
    if (!app.ed || !classicMode()) {
        log << "NOT a classic map tab\n";
        return;
    }
    const Chk original = app.ed->chk();
    const bool settings = classicSettingsUsed(*app.ed);
    std::error_code ec;
    fs::remove_all(dir / "classicproject", ec);
    std::string err;
    const bool made = createProject(dir, "classicproject", true, err);
    for (int i = 0; i < 4; ++i) pump();
    log << "made a project: " << (made && projectMode() ? "yes" : "NO " + err) << "\n";
    if (!made || !activeProject()) return;
    Bytes in;
    std::string differ;
    if (readMapChk(activeProject()->inputMap(), in, err)) {
        const Chk c = Chk::parse(in);
        for (const char* n : {"TRIG", "MBRF", "UNIx", "UNIS", "PUNI", "UPGx", "UPGS", "PUPx", "UPGR", "TECx", "TECS", "PTEx", "PTEC", "MRGN", "STR ", "STRx"}) {
            SectionName s;
            std::copy(n, n + 4, s.c.begin());
            if (original.effective(s) != c.effective(s)) differ += std::string(" ") + n;
        }
    } else {
        differ = " (the project's map does not read: " + err + ")";
    }
    log << "the project's map keeps TRIG, MBRF, the settings, MRGN and the strings byte-identical " << (differ.empty() ? "yes" : "NO:" + differ) << "\n";
    const bool trig = !readClassicTriggers(original).triggers.empty();
    log << "Classic triggers in the project " << (commandInMode(ID_SCEN_CLASSICTRIG) == trig ? "yes" : "NO") << " (the map has " << (trig ? "" : "no ")
        << "classic triggers); Map data not in the project " << (!commandInMode(ID_SCEN_CLASSIC) ? "yes" : "NO") << " (the map has "
        << (settings ? "" : "no ") << "unit, upgrade or tech settings; the project's map keeps them)\n";
    if (trig) {
        command(ID_SCEN_CLASSICTRIG);
        for (int i = 0; i < 10; ++i) pump(), Sleep(20);
        const bool shown = gWorkspace == Workspace::ClassicTriggers && gCt && IsWindowVisible(gCt->note) && !gCt->trig.triggers.empty();
        capture(dir / "classic_triggers_project.png", app.main);
        command(ID_WS_MAP);
        log << "the Classic triggers page opens in the project with the note " << (shown ? "yes" : "NO") << "\n";
    }
}
