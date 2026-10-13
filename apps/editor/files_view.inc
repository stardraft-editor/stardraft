// The Files workspace (included into main.cpp after briefing_view.inc): the map's own sound files, to import
// (WAV, OGG), play, rename and remove; and the game's sounds, to listen to, which briefings can name without the
// map holding them. The References sidebar on the right lists what uses the selected file.

struct FilesView {
    HWND wnd = nullptr, search = nullptr;
    ui::Surface surface;
    std::string selected;  // a path: a map file or a game sound
    int scroll = 0, height = 0;
    std::wstring filter;
    std::vector<std::pair<RECT, std::string>> rows;  // the sound rows of the last paint (a double-click plays one)
} gFiles;
constexpr UINT_PTR kFilesTimer = 8;

// Where a sound is used: the briefings (by owner) whose actions name it.
struct SoundUse {
    int owner, trig, act;
    std::wstring text;
};
std::vector<SoundUse> soundUses(const std::string& name) {
    std::vector<SoundUse> out;
    if (!app.ed || name.empty()) return out;
    const auto& b = briefings();
    for (size_t t = 0; t < b.size(); ++t)
        for (size_t a = 0; a < b[t].actions.size(); ++a) {
            if (_stricmp(b[t].actions[a].wav.c_str(), name.c_str()) != 0) continue;
            // One entry per briefing (the triggers with that player list), at its first use.
            if (std::none_of(out.begin(), out.end(), [&](const SoundUse& u) { return b[size_t(u.trig)].players == b[t].players; }))
                out.push_back({int(t), int(t), int(a), whoName(b[t].players)});
        }
    return out;
}
std::wstring sizeText(size_t n) {
    wchar_t b[32];
    if (n < 1024) std::swprintf(b, 32, L"%zu B", n);
    else if (n < 1024 * 1024) std::swprintf(b, 32, L"%.0f KB", n / 1024.0);
    else std::swprintf(b, 32, L"%.1f MB", n / 1048576.0);
    return b;
}
std::wstring lengthText(uint32_t ms) {
    wchar_t b[32];
    std::swprintf(b, 32, L"%u:%04.1f", ms / 60000, (ms % 60000) / 1000.0);
    return b;
}

void filesRename(const std::string& from) {
    std::wstring name = widen(from);
    if (!prompt(L"Rename the sound", name) || name.empty()) return;
    const std::string to = narrow(name);
    if (to == from) return;
    // One undo step: the file and the briefings that play it get the new path.
    if (!app.ed->renameArchiveFile(from, to)) return setMessage(L"A file of that name is in the map already");
    InvalidateRect(gBrief.wnd, nullptr, FALSE);
    updateTitle();
    gFiles.selected = to;
    InvalidateRect(gFiles.wnd, nullptr, FALSE);
}

// After an undo or redo step: the sound files and the briefings that play them can be different.
void repaintSoundViews() {
    if (gFiles.wnd) InvalidateRect(gFiles.wnd, nullptr, FALSE);
    if (gBrief.wnd) InvalidateRect(gBrief.wnd, nullptr, FALSE);
}

void paintFiles(HWND hwnd) {
    gFiles.surface.begin(hwnd);
    gFiles.rows.clear();
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    const int refsW = scale(260), rowH = scale(26);
    FormLayout l = formLayout({0, 0, int(rc.right) - refsW, rc.bottom});
    l.colRight = int(rc.right) - refsW - scale(18), l.colW = l.colRight - l.x0;
    int y = scale(12) - gFiles.scroll;
    // A table of sounds: a header row (Name, Kind, Length, Size), then a row per sound with hairlines between.
    // kind: 0 a file in the map, 1 a game sound the map names, 2 a game sound (the list to add from).
    // The kit's table: play, name, kind, length, size, and the row's action.
    const std::vector<ui::Column> cols = {{L"", 40}, {L"Name", 0}, {L"Kind", 80}, {L"Length", 80, DT_RIGHT}, {L"Size", 80, DT_RIGHT}, {L"", 30}};
    const RECT sample{l.x0, 0, l.colRight, 1};
    const int cEnd = ui::tableCell(sample, cols, 5).left;
    int tableTop = 0;
    auto tableHeader = [&] {
        tableTop = y;
        ui::tableHeader(mem, {l.x0, y, l.colRight, y + rowH}, cols);
        y += rowH;
    };
    auto tableEnd = [&] {
        ui::tableFrame(mem, {l.x0, tableTop, l.colRight, y + 1});
        y += scale(8);
    };
    auto soundRow = [&](const std::string& name, size_t size, int kind) {
        RECT r{l.x0, y, l.colRight, y + rowH};
        gFiles.rows.push_back({r, name});  // (a double-click plays it)
        if (r.bottom < 0 || r.top > rc.bottom) return void(y += rowH);
        const bool on = _stricmp(name.c_str(), gFiles.selected.c_str()) == 0;
        ui::tableRow(mem, r, on, mouseIn(r));
        hitsOut().push_back({r, nullptr, [name] {
                                 gFiles.selected = name;
                                 InvalidateRect(gFiles.wnd, nullptr, FALSE);
                             },
                             L"", IDC_HAND, {}});
        const bool playing = previewPlaying(name);
        ui::iconButton(mem, {l.x0 + scale(8), y + scale(1), l.x0 + scale(8 + ui::kIconBox), y + rowH - scale(1)}, playing ? UiIcon::Stop : UiIcon::Play,
                       [name] {
                           gFiles.selected = name;
                           previewToggle(name);
                           SetTimer(gFiles.wnd, kFilesTimer, 200, nullptr);
                           InvalidateRect(gFiles.wnd, nullptr, FALSE);
                       },
                       playing ? L"Stop" : L"Play", ui::Icon::Bare, playing, true, ui::Tone::Primary);
        RECT nt = ui::tableCell(r, cols, 1);
        InflateRect(&nt, -scale(4), 0);
        SetTextColor(mem, on ? kAccent : kText);
        DrawTextW(mem, widen(name).c_str(), -1, &nt, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(mem, kDim);
        std::string ext = name.substr(name.size() - 3);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
        RECT kt = ui::tableCell(r, cols, 2);
        InflateRect(&kt, -scale(4), 0);
        // A name the map lists that neither it nor the game holds: missing.
        const bool missing = kind == 1 && !soundFileBytes(name);
        if (missing) SetTextColor(mem, kBad), hitsOut().push_back({kt, nullptr, nullptr, L"The map names this sound, but neither the map nor the game has the file", IDC_ARROW, {}});
        DrawTextW(mem, (kind == 0 ? widen(ext) : missing ? std::wstring(L"Missing") : L"Game " + widen(ext)).c_str(), -1, &kt, DT_SINGLELINE | DT_VCENTER);
        SetTextColor(mem, kDim);
        if (!missing && (kind != 2 || on)) {  // lengths: the map's, and the selected game sound's (reading every game sound takes long)
            RECT lt = ui::tableCell(r, cols, 3);
            InflateRect(&lt, -scale(4), 0);
            DrawTextW(mem, lengthText(soundLength(name)).c_str(), -1, &lt, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        }
        RECT st = ui::tableCell(r, cols, 4);
        InflateRect(&st, -scale(4), 0);
        DrawTextW(mem, kind == 0 ? sizeText(size).c_str() : L"\x2014", -1, &st, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        const RECT last{cEnd, y, l.colRight, y + rowH};
        if (kind == 0 || kind == 1) {
            removeCross(mem, r, last, [name, kind] {
                previewStop();
                if (kind == 0) app.ed->removeArchiveFile(name);
                else app.ed->removeSoundReference(name);
                if (gFiles.selected == name) gFiles.selected.clear();
                const size_t uses = soundUses(name).size();
                setMessage(uses ? L"Removed the sound. " + std::to_wstring(uses) + L" briefings still use it." : L"Removed the sound");
                updateTitle();
                InvalidateRect(gFiles.wnd, nullptr, FALSE);
            }, kind == 0 ? L"Remove the file from the map" : L"Remove the reference from the map");
        } else {
            const auto refs = app.ed ? app.ed->soundReferences() : std::vector<std::string>{};
            const bool added = std::find(refs.begin(), refs.end(), name) != refs.end();
            hitsOut().push_back({r, nullptr, nullptr, L"", IDC_HAND, {}});
            if (added) {
                ui::glyph(mem, last, UiIcon::Check, kGood);  // in the map already
                hitsOut().push_back({last, nullptr, nullptr, L"The map has it", IDC_ARROW, {}});
            } else {
                ui::rowActions(mem, r,
                               {{UiIcon::Add, L"Add to the map's sounds, by reference: the game plays its own file, the map does not hold it",
                                 [name] {
                                     app.ed->addSoundReference(name);
                                     gFiles.selected = name;
                                     updateTitle();
                                     InvalidateRect(gFiles.wnd, nullptr, FALSE);
                                 },
                                 ui::Tone::Primary}},
                               false);
            }
        }
        y += rowH;
    };
    // The map's sounds: its files, and the game sounds it names.
    y = sectionHeader(mem, l, y, L"Sounds in the map");
    tableHeader();
    const auto mine = mapSounds();
    for (const auto& n : mine) {
        const ArchiveFile* f = app.ed->archiveFile(n);
        soundRow(n, f ? f->data.size() : 0, f ? 0 : 1);
    }
    if (mine.empty()) {
        SetTextColor(mem, kDim);
        RECT t{l.x0 + scale(50), y, l.colRight, y + rowH};  // under the names
        DrawTextW(mem, L"The map has no sounds of its own. Import sounds adds WAV or OGG files.", -1, &t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        y += rowH;
    }
    tableEnd();
    const int ax = linkButton(mem, l.x0, y, rowH, L"Import sounds\x2026", [] {
        const auto added = importSounds(app.main);
        if (!added.empty()) gFiles.selected = added.front();
        InvalidateRect(gFiles.wnd, nullptr, FALSE);
    }, L"WAV or OGG files. StarDraft 2 saves them in the map (staredit\\wav\\).");
    if (!gFiles.selected.empty() && app.ed && app.ed->archiveFile(gFiles.selected)) {
        const std::string sel = gFiles.selected;
        linkButton(mem, ax + scale(8), y, rowH, L"Rename", [sel] { filesRename(sel); }, L"The briefings that use the file change with it", ui::Tone::Quiet);
    }
    y += rowH + scale(18);
    // The game's sounds.
    y = sectionHeader(mem, l, y, L"Sounds of the game");
    MoveWindow(gFiles.search, l.x0, y, std::min(scale(320), l.colW), scale(24), TRUE);
    ShowWindow(gFiles.search, SW_SHOWNA);
    y += scale(32);
    tableHeader();
    std::wstring f = gFiles.filter;
    std::transform(f.begin(), f.end(), f.begin(), ::towlower);
    int shown = 0;
    for (const auto& g : gameSounds()) {
        if (!f.empty()) {
            std::wstring w = widen(g);
            std::transform(w.begin(), w.end(), w.begin(), ::towlower);
            if (w.find(f) == std::wstring::npos) continue;
        }
        soundRow(g, 0, 2);
        ++shown;
    }
    if (!shown) {
        SetTextColor(mem, kDim);
        RECT t{l.x0 + scale(50), y, l.colRight, y + rowH};  // under the names
        DrawTextW(mem, f.empty() ? L"StarDraft 2 found no game sounds (Options > Program options: StarCraft folder)" : L"No game sound has these words", -1, &t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        y += rowH;
    }
    tableEnd();
    gFiles.height = y + gFiles.scroll + scale(20);
    // References: what uses the selected sound.
    {
        RECT side{int(rc.right) - refsW, 0, rc.right, rc.bottom};
        ui::sidePanel(mem, side);
        const int x = side.left + scale(14), w = refsW - scale(24);
        const auto uses = soundUses(gFiles.selected);
        int ry = ui::sideHeader(mem, x, scale(12), w, L"References", uses.size());
        if (!uses.empty()) {
            ry = ui::sideGroup(mem, x, ry, w, L"Briefings", true);
            for (const auto& u : uses) {
                const RECT r = ui::sideLink(mem, x, ry, w, u.text, nullptr, mouseIn({x, ry, x + w, ry + scale(20)}));
                const SoundUse use = u;
                hitsOut().push_back({r, nullptr, [use] {
                                           setWorkspace(Workspace::Briefing);
                                           briefSetWho(briefings()[size_t(use.trig)].players);
                                           briefRows();
                                           const int row = briefRowOf(use.trig, use.act);
                                           if (row >= 0) briefSelectRange(row, 1);
                                           InvalidateRect(gBrief.wnd, nullptr, FALSE);
                                       },
                                       L"Open it in the Briefing workspace", IDC_HAND, {}});
                ry += scale(22);
            }
        }
    }
    if (const int most = std::max(0, gFiles.height - int(rc.bottom)); gFiles.scroll > most)  // taller window: the bar hides, so no scroll is left
        gFiles.scroll = most, InvalidateRect(hwnd, nullptr, FALSE);
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, gFiles.height - 1), UINT(rc.bottom), gFiles.scroll};
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    gFiles.surface.end(hwnd);
}

LRESULT CALLBACK filesWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r; gFiles.surface.message(hwnd, msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_PAINT: paintFiles(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_TIMER:  // a preview playing: its button goes back to Play when it ends
        if (wp == kFilesTimer) {
            if (!previewPlaying(gPreviewVoice.name)) KillTimer(hwnd, kFilesTimer);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_COMMAND:
        if (HWND(lp) == gFiles.search && HIWORD(wp) == EN_CHANGE) gFiles.filter = windowText(gFiles.search), gFiles.scroll = 0, InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
    case WM_VSCROLL: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int s = msg == WM_MOUSEWHEEL ? gFiles.scroll - GET_WHEEL_DELTA_WPARAM(wp) * scale(26) * 3 / WHEEL_DELTA : scrollFromMessage(hwnd, wp, gFiles.scroll, scale(26));
        gFiles.scroll = std::clamp(s, 0, std::max(0, gFiles.height - int(rc.bottom)));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDBLCLK: {  // a sound's row (not its Play button): the sound plays; Rename is its button
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const auto row = std::find_if(gFiles.rows.begin(), gFiles.rows.end(), [&](const auto& r) { return PtInRect(&r.first, pt); });
        if (row != gFiles.rows.end() && pt.x > row->first.left + scale(8 + ui::kIconBox)) {
            gFiles.selected = row->second;
            previewToggle(gFiles.selected);
            SetTimer(gFiles.wnd, kFilesTimer, 200, nullptr);
            InvalidateRect(gFiles.wnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_SPACE && !gFiles.selected.empty()) return previewToggle(gFiles.selected), SetTimer(hwnd, kFilesTimer, 200, nullptr), InvalidateRect(hwnd, nullptr, FALSE), 0;
        break;

    case WM_CTLCOLOREDIT:
        SetTextColor(HDC(wp), kText);
        SetBkColor(HDC(wp), kCtl);
        return LRESULT(app.ctlBrush);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void createFilesView() {
    if (gFiles.wnd) return;
    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpfnWndProc = filesWndProc, wc.lpszClassName = L"StarDraftFiles";
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    gFiles.wnd = CreateWindowExW(0, L"StarDraftFiles", L"", WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 1, 1, app.main, nullptr, app.inst, nullptr);
    darkControl(gFiles.wnd, L"DarkMode_Explorer");  // the scroll bar in the theme
    gFiles.search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, gFiles.wnd, nullptr, app.inst, nullptr);
    SendMessageW(gFiles.search, WM_SETFONT, WPARAM(app.font), TRUE);
    SendMessageW(gFiles.search, EM_SETCUEBANNER, TRUE, LPARAM(L"Search the game's sounds")); addSearchClear(gFiles.search);  // an x clears it
    darkControl(gFiles.search, L"DarkMode_CFD");
}
