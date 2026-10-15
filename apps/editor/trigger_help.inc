// Part of main.cpp: help and fixes for the trigger editor.
// - Block help (T.7): F1 on a block in the pane or in the block picker opens a page for the block: what it does,
//   its values and their types, an example, what to watch out for, its effects and cost. The page follows the
//   selected block while it is open. It is painted from the block library, so it works offline.
// - Cost fixes (T.8): a heavy trigger shows the changes that make it cheaper, each with Apply.
// - Build errors (T.1): Build keeps the block of each line of generated/triggers.eps; a euddraft error in a line
//   selects that block in the Triggers workspace.

void trigChanged();
void trigRefreshTree(std::vector<int> select);

// --- Block help --------------------------------------------------------------------------------------------------

struct BlockHelp {
    HWND wnd = nullptr;
    std::string id;
    int scroll = 0, contentH = 0;
};
BlockHelp gBlockHelp;

std::wstring blockKindText(const TriggerDef& d) { return d.condition ? L"condition" : d.value ? L"value" : L"action"; }

// The words for a block's effects.
std::vector<std::wstring> blockEffectTexts(const TriggerDef& d) {
    std::vector<std::wstring> out;
    if (d.has("local")) out.push_back(L"Only the player on this computer sees or hears it.");
    if (d.has("cp"))
        out.push_back(d.param("who") >= 0 ? L"It acts for the player that you select. The current player has a value only in For each player."
                                          : L"It acts for the current player. Put it in For each player.");
    if (d.has("blocking")) out.push_back(L"It holds up the other triggers of the player until it is complete.");
    if (d.has("memory")) out.push_back(L"It reads or writes game memory at an address.");
    if (d.has("unitloop")) out.push_back(L"It goes through units, up to 1,700 each time it runs.");
    if (d.has("theunit")) out.push_back(L"Inside it, \"the unit\" is the unit that the loop handles now.");
    if (d.has("counter")) out.push_back(L"Inside it, counter is the round number (0 is the first), the number, or the index of the item.");
    if (d.has("curplayer")) out.push_back(L"Inside it, the current player is the player that the current run is for.");
    if (d.has("differs")) out.push_back(L"Its value is not the same on each computer. Use it only for what a player sees or hears.");
    return out;
}

// The sentence with each value's name in brackets: "create [n] [unit] at [where] for [player]".
std::wstring blockHelpSentence(const TriggerDef& d) {
    std::wstring s;
    for (size_t i = 0; i < d.sentence.size();) {
        if (d.sentence[i] == '{') {
            const size_t end = d.sentence.find('}', i);
            if (end != std::string::npos) {
                s += L"[" + widen(d.sentence.substr(i + 1, end - i - 1)) + L"]";
                i = end + 1;
                continue;
            }
        }
        s += wchar_t(static_cast<unsigned char>(d.sentence[i++]));
    }
    return s;
}

// A paragraph with word wrap; the y under it.
int blockHelpText(HDC dc, int x, int y, int right, const std::wstring& text, COLORREF color) {
    RECT r{x, y, right, y};
    DrawTextW(dc, text.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    r.right = right;
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &r, DT_WORDBREAK | DT_NOPREFIX);
    return r.bottom + scale(8);
}

void paintBlockHelp(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP back = CreateCompatibleBitmap(screen, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(dc, back), of = SelectObject(dc, app.font);
    FillRect(dc, &rc, app.bgBrush);
    SetBkMode(dc, TRANSPARENT);
    FormLayout l = formLayout(rc);
    l.colRight = rc.right - l.x0;
    const int x = l.x0, right = l.colRight;
    int y = scale(14) - gBlockHelp.scroll;
    if (const TriggerDef* d = TriggerDef::find(gBlockHelp.id)) {
        std::wstring sub = widen(d->category) + L"  \x00B7  " + blockKindText(*d);
        if (d->tier == "advanced") sub += L"  \x00B7  advanced";
        y = ui::title(dc, x, y, right, nullptr, widen(d->title), sub);
        y = blockHelpText(dc, x, y, right, blockHelpSentence(*d), kAccent);
        y = sectionHeader(dc, l, y, L"What it does");
        y = blockHelpText(dc, x, y, right, widen(d->docs), kText);
        if (!d->params.empty()) {
            y = sectionHeader(dc, l, y, L"Values");
            for (const auto& p : d->params) {
                std::wstring line = L"[" + widen(p.name) + L"]  " + trigTypeNote(p);
                if (p.max >= p.min) line += L", " + std::to_wstring(p.min) + L" to " + std::to_wstring(p.max);
                if (!p.def.empty() && p.type.rfind("ValueOf:", 0) != 0) line += L"  \x00B7  default value: " + trigValueText(p.type, p.def);
                y = blockHelpText(dc, x, y, right, line, triggerTypeClear(p.type) || p.type.rfind("ValueOf:", 0) == 0 ? kText : trigPurple());
            }
        }
        if (!d->lists.empty()) {
            y = sectionHeader(dc, l, y, L"Holds");
            std::wstring lists;
            for (const auto& li : d->lists) lists += (lists.empty() ? L"" : L", ") + widen(li.title) + (li.condition ? L" (conditions)" : L" (actions)");
            y = blockHelpText(dc, x, y, right, lists, kText);
        }
        y = sectionHeader(dc, l, y, L"Example");
        y = blockHelpText(dc, x, y, right, d->example.empty() ? L"-" : widen(d->example), kText);
        if (!d->pitfalls.empty()) {
            y = sectionHeader(dc, l, y, L"Watch out");
            y = blockHelpText(dc, x, y, right, widen(d->pitfalls), kWarn);
        }
        const auto effects = blockEffectTexts(*d);
        y = sectionHeader(dc, l, y, L"Effects and cost");
        for (const auto& e : effects) y = blockHelpText(dc, x, y, right, e, kText);
        y = blockHelpText(dc, x, y, right, L"About " + std::to_wstring(d->cost) + (d->cost == 1 ? L" action" : L" actions") + L" each time it runs" +
                                               (d->lists.empty() ? L"." : L", and the blocks in it."),
                          kDim);
    }
    gBlockHelp.contentH = y + gBlockHelp.scroll;
    BitBlt(screen, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, of), SelectObject(dc, ob);
    DeleteObject(back);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK blockHelpProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: return paintBlockHelp(hwnd), 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int maxScroll = std::max(0, gBlockHelp.contentH - int(rc.bottom) + scale(14));
        gBlockHelp.scroll = std::clamp(gBlockHelp.scroll - GET_WHEEL_DELTA_WPARAM(wp) * scale(40) / WHEEL_DELTA, 0, maxScroll);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_F1) return DestroyWindow(hwnd), 0;
        break;
    case WM_FEEDBACK_NAME: {
        const TriggerDef* d = TriggerDef::find(gBlockHelp.id);
        reinterpret_cast<FeedbackQuery*>(lp)->name = L"the help for " + (d ? widen(d->title) : std::wstring(L"a block"));
        return 0;
    }
    case WM_DESTROY: gBlockHelp.wnd = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Opens the help page for a block (or shows another block on it), beside `beside` (the pane, or the block picker). It
// does not take the focus: the arrow keys keep moving through the blocks, and the page follows.
void showBlockHelp(const std::string& id, HWND beside) {
    const TriggerDef* d = TriggerDef::find(id);
    if (!d) return;
    if (!gBlockHelp.wnd) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{sizeof wc};
            wc.lpfnWndProc = blockHelpProc, wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpszClassName = L"StarDraftBlockHelp";
            RegisterClassExW(&wc);
            registered = true;
        }
        RECT nr{};
        GetWindowRect(beside ? beside : app.main, &nr);
        const int w = scale(460), h = scale(600);
        int x = nr.right - w - scale(16), y = nr.top + scale(16);
        if (beside && beside != app.main && beside != gTE.pane) x = nr.right + scale(8), y = nr.top;  // the block picker: beside it
        gBlockHelp.wnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"StarDraftBlockHelp", L"Block help", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, x, y, w, h,
                                         app.main, nullptr, app.inst, nullptr);
        BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(gBlockHelp.wnd, 20, &dark, sizeof dark), titleColors(gBlockHelp.wnd);
    }
    if (gBlockHelp.id != id) gBlockHelp.id = id, gBlockHelp.scroll = 0;
    SetWindowTextW(gBlockHelp.wnd, (L"Block help: " + widen(d->title)).c_str());
    ShowWindow(gBlockHelp.wnd, SW_SHOWNOACTIVATE);
    SetWindowPos(gBlockHelp.wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(gBlockHelp.wnd, nullptr, FALSE);
}

// While the page is open, it shows the block selected in the pane.
void blockHelpFollow(const std::string& id) {
    if (gBlockHelp.wnd && !id.empty() && id != gBlockHelp.id) showBlockHelp(id, nullptr);
}

// F1 in the pane: the selected block's page (F1 again, or Esc, closes it).
void trigPaneHelp() {
    TriggerNode* t = trigSelected();
    const TriggerCall* c = t ? trigBlock(*t, gTP.sel) : nullptr;
    if (!c) {
        if (gBlockHelp.wnd) DestroyWindow(gBlockHelp.wnd);
        return setMessage(L"Select a block, then press F1 for its help");
    }
    if (gBlockHelp.wnd && gBlockHelp.id == c->def) return void(DestroyWindow(gBlockHelp.wnd));
    showBlockHelp(c->def, gTE.pane);
}

// --- Cost fixes ------------------------------------------------------------------------------------------------------

// (The fixes' Apply links, gTrigFixLinks, and their paint are in core/views_triggers_pane.inc.)

// (trigApplyCostFix is in core/triggers_model.inc.)

// --- Build errors --------------------------------------------------------------------------------------------------

// The block of each line of the last generated/triggers.eps: the node's path and the block (index -1: the trigger).
struct TrigCodeMap {
    fs::path project;
    std::vector<std::pair<std::vector<int>, TrigBlockRef>> lines;
};
TrigCodeMap gTrigCodeMap;

bool trigFindCall(const std::vector<TriggerCall>& list, const TrigListRef& ref, const TriggerCall* target, TrigBlockRef& out) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (&list[i] == target) return out = {ref, int(i)}, true;
        for (size_t n = 0; n < list[i].lists.size(); ++n) {
            TrigListRef inner = ref;
            inner.via.push_back({ref.list, int(i)});
            inner.list = 2 + int(n);
            if (trigFindCall(list[i].lists[n], inner, target, out)) return true;
        }
    }
    return false;
}

// Keeps the line map of code made from `set` (Build calls it with the code it writes).
void trigKeepCodeMap(const EudProject& pr, const TriggerSet& set, const std::vector<TriggerCodeLine>& lines) {
    std::map<const TriggerNode*, std::vector<int>> paths;
    std::function<void(const TriggerNode&, std::vector<int>)> walk = [&](const TriggerNode& n, std::vector<int> path) {
        paths[&n] = path;
        for (size_t i = 0; i < n.children.size(); ++i) {
            auto p = path;
            p.push_back(int(i));
            walk(n.children[i], p);
        }
    };
    for (size_t i = 0; i < set.roots.size(); ++i) walk(set.roots[i], {int(i)});
    gTrigCodeMap = {pr.folder(), {}};
    for (const auto& l : lines) {
        std::pair<std::vector<int>, TrigBlockRef> m;
        if (auto it = paths.find(l.node); it != paths.end()) m.first = it->second;
        if (l.node && l.call) {
            TrigListRef conds;
            conds.list = 0;
            TrigListRef acts;
            acts.list = 1;
            if (!trigFindCall(l.node->conditions, conds, l.call, m.second)) trigFindCall(l.node->actions, acts, l.call, m.second);
        }
        gTrigCodeMap.lines.push_back(std::move(m));
    }
}

// The trigger (and block) that made a line of the last generated/triggers.eps: "Name, Block title" ("" if not known).
std::wstring trigCodeLineName(int line) {
    const EudProject* pr = activeProject();
    if (line < 1 || line > int(gTrigCodeMap.lines.size()) || !pr || pr->folder() != gTrigCodeMap.project) return L"";
    const auto& [path, ref] = gTrigCodeMap.lines[size_t(line - 1)];
    TriggerNode* n = path.empty() ? nullptr : gTE.set.at(path);
    if (!n) return L"";
    const TriggerCall* c = ref.index >= 0 ? trigBlock(*n, ref) : nullptr;
    const TriggerDef* d = c ? TriggerDef::find(c->def) : nullptr;
    return widen(n->name) + (d ? L", " + widen(d->title) : L"");
}

// Shows the block that made a line of the last generated/triggers.eps in the Triggers workspace. True when it did.
bool trigShowCodeLine(int line) {
    const EudProject* pr = activeProject();
    if (line < 1 || line > int(gTrigCodeMap.lines.size()) || !pr || pr->folder() != gTrigCodeMap.project) return false;
    const auto& [path, ref] = gTrigCodeMap.lines[size_t(line - 1)];
    if (path.empty() || !gTE.set.at(path)) return false;
    setWorkspace(Workspace::Triggers);
    trigRefreshTree(path);
    gTP.sel = ref, gTP.focus = {}, gTP.focus.ref.index = -1;
    // Scroll the block into view: lay out the pane, then move to it.
    gTE.scroll = 0;
    UpdateWindow(gTE.pane);
    RECT pc;
    GetClientRect(gTE.pane, &pc);
    for (const auto& l : gTP.lines)
        if (trigSameBlock(l.ref, ref) && l.r.bottom > pc.bottom) gTE.scroll = std::max(0, int(l.r.top) - int(pc.bottom) / 3);
    InvalidateRect(gTE.pane, nullptr, FALSE);
    InvalidateRect(gTE.info, nullptr, FALSE);
    return true;
}

// A line of euddraft's output: when it names a line of the triggers' code, that block is selected. True when it did.
bool trigShowBuildError(const std::string& logLine) {
    const int line = triggerErrorLine(logLine);
    if (!trigShowCodeLine(line)) return false;
    const auto& [path, ref] = gTrigCodeMap.lines[size_t(line - 1)];
    const TriggerNode* n = gTE.set.at(path);
    const TriggerCall* c = trigSelected() ? trigBlock(*trigSelected(), ref) : nullptr;
    const TriggerDef* d = c ? TriggerDef::find(c->def) : nullptr;
    setMessage(L"euddraft found an error in line " + std::to_wstring(line) + L" of the triggers' code: " + widen(n->name) +
               (d ? L", " + widen(d->title) : L"") + L" (see the log)");
    return true;
}
