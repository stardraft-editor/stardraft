// View > Control kit: every control of the kit (ui_kit.inc) in each of its states, on one page; the controls work
// (they repaint under the mouse and take clicks), so a change to the kit shows here first. docs/dev/CONTROLS.md lists where
// each is used.

struct KitGallery {
    HWND wnd = nullptr;
    ui::Surface surface;
    int scroll = 0, height = 0;
    bool check1 = true, check2 = false, selected = false;
    int clicks = 0, tableSel = 1;
    float slider = 0.4f;
    std::vector<std::wstring> order = {L"generated/strings.eps", L"scripts/intro.eps", L"scripts/waves.eps", L"scripts/boss.eps", L"scripts/main.eps"};
} gKit;

void paintKitGallery(HWND hwnd) {
    gKit.surface.begin(hwnd);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    FormLayout l = formLayout(rc);
    l.labelW = scale(150), l.colRight = int(rc.right) - scale(24), l.colW = l.colRight - l.x0;
    int y = scale(16) - gKit.scroll;
    const int col = l.x0 + l.labelW, gap = scale(12), rowH = scale(36);
    auto label = [&](const wchar_t* t) {
        FormLayout rl = l;
        rl.rowH = rowH;
        ui::fieldLabel(mem, rl, y, t, false);
    };
    auto caption = [&](int x, int yy, const wchar_t* t) {  // a state's name under its sample
        HGDIOBJ f2 = SelectObject(mem, ui::smallFont());
        SetTextColor(mem, kFaint);
        RECT r{x, yy, x + scale(600), yy + scale(14)};
        DrawTextW(mem, t, -1, &r, DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(mem, f2);
    };
    auto click = [] { ++gKit.clicks, InvalidateRect(gKit.wnd, nullptr, FALSE); };

    y = ui::title(mem, l.x0, y, l.colRight, nullptr, L"Control kit", L"apps/editor/ui_kit.inc  \x00B7  where each is used: docs/dev/CONTROLS.md");

    // Colors.
    y = ui::sectionHeader(mem, l, y, L"Colors");
    {
        const std::pair<const wchar_t*, COLORREF> tokens[] = {
            {L"kBg", kBg},         {L"kPanel", kPanel},         {L"kCtl", kCtl},       {L"kCtlHot", kCtlHot},   {L"kReadOnly", kReadOnly},
            {L"kBorder", kBorder}, {L"kBorderHot", kBorderHot}, {L"kText", kText},     {L"kDim", kDim},         {L"kFaint", kFaint},
            {L"kAccent", kAccent}, {L"kAccentLine", kAccentLine}, {L"kSel", kSel},     {L"kRowHot", kRowHot},   {L"kGood", kGood},
            {L"kGoodFill", kGoodFill}, {L"kWarn", kWarn},       {L"kBad", kBad}};
        int x = l.x0;
        for (const auto& [name, c] : tokens) {
            if (x + scale(96) > l.colRight) x = l.x0, y += scale(54);
            ui::box(mem, {x, y, x + scale(88), y + scale(30)}, c, kBorderHot);
            caption(x, y + scale(33), name);
            x += scale(96);
        }
        y += scale(62);
    }

    // Buttons: tones across, states down.
    y = ui::sectionHeader(mem, l, y, L"Buttons");
    {
        const std::pair<const wchar_t*, ui::State> states[] = {{L"rest", ui::State::Normal}, {L"under the mouse", ui::State::Hot}, {L"pressed", ui::State::Down}, {L"not usable", ui::State::Disabled}};
        const std::pair<const wchar_t*, ui::Tone> tones[] = {{L"Normal", ui::Tone::Normal}, {L"Primary", ui::Tone::Primary}, {L"Danger", ui::Tone::Danger}};
        for (const auto& [tname, tone] : tones) {
            label(tname);
            int x = col;
            for (const auto& [sname, st] : states) {
                const RECT r{x, y + scale(4), x + scale(120), y + scale(4 + ui::kButtonH)};
                ui::buttonFace(mem, r, tname == std::wstring(L"Danger") ? L"Remove" : tname == std::wstring(L"Primary") ? L"OK" : L"Browse\x2026", tone, st);
                caption(x, r.bottom + scale(3), sname);
                x += scale(132);
            }
            ui::button(mem, {x + scale(20), y + scale(4), x + scale(160), y + scale(4 + ui::kButtonH)}, L"Try me", tone, click, L"A live button: clicks count");
            y += rowH + scale(18);
        }
        label(L"Right-aligned row");
        ui::buttonsRight(mem, col + scale(560), y + scale(4), {{L"OK", ui::Tone::Primary, click}, {L"Cancel", ui::Tone::Normal, click}});
        y += rowH + scale(6);
        label(L"Segmented choice");
        const wchar_t* modes[] = {L"Terrain", L"Units", L"Doodads", L"Locations"};
        for (int i = 0; i < 4; ++i)
            ui::buttonFace(mem, {col + i * scale(100), y + scale(4), col + i * scale(100) + scale(96), y + scale(4 + 26)}, modes[i], i == 1 ? ui::Tone::Primary : ui::Tone::Normal, ui::State::Normal);
        y += rowH + scale(6);
    }

    // Links.
    y = ui::sectionHeader(mem, l, y, L"Links");
    {
        label(L"Primary");
        int x = ui::smallButton(mem, col, y, rowH, L"Add an action", click, L"A small button: an action in a row or under a list");
        x = ui::smallButton(mem, x + gap, y, rowH, L"Reset", click);
        x = ui::smallButton(mem, x + gap, y, rowH, L"Copy a button from a set\x2026", click);
        ui::link(mem, x + gap * 2, y, rowH, L"Terran Marine", click, L"A link: only to an entity, by its name; underlined under the mouse");
        y += rowH;
        label(L"Quiet, Danger");
        x = ui::smallButton(mem, col, y, rowH, L"Add a condition", click);
        x = ui::smallButton(mem, x + gap, y, rowH, L"Rename", click);
        ui::smallButton(mem, x + gap, y, rowH, L"Remove the list", click, L"", ui::Tone::Danger);
        y += rowH + scale(6);
    }

    // Icon buttons.
    y = ui::sectionHeader(mem, l, y, L"Icon buttons");
    {
        const int s = scale(32), b = scale(ui::kIconBox);
        label(L"Boxed");
        int x = col;
        const UiIcon glyphs[] = {UiIcon::ToStart, UiIcon::Play, UiIcon::ToEnd, UiIcon::Loop, UiIcon::Sound, UiIcon::Code};
        for (int i = 0; i < 6; ++i) {
            ui::iconButton(mem, {x, y + scale(2), x + s, y + scale(2) + s}, glyphs[i], click, L"Boxed", ui::Icon::Boxed, i == 3);
            x += s + scale(6);
        }
        caption(col, y + s + scale(4), L"the fourth is a toggle that is on");
        ui::iconButton(mem, {x + gap, y + scale(2), x + gap + s, y + scale(2) + s}, UiIcon::Play, click, L"Not usable", ui::Icon::Boxed, false, false);
        caption(x + gap, y + s + scale(4), L"not usable");
        y += rowH + scale(16);
        label(L"Bare");
        x = col;
        struct B {
            ui::Ico g;
            const wchar_t* name;
            ui::Tone tone;
            bool on, over;
        } bare[] = {{UiIcon::OpenExternal, L"open", ui::Tone::Primary, false, false}, {UiIcon::OpenExternal, L"open, mouse", ui::Tone::Primary, false, true},
                    {UiIcon::Duplicate, L"duplicate", ui::Tone::Normal, false, false}, {UiIcon::Duplicate, L"mouse", ui::Tone::Normal, false, true},
                    {UiIcon::Delete, L"delete, mouse", ui::Tone::Danger, false, true}, {L"\xE7E8", L"turned off", ui::Tone::Quiet, true, false}};
        for (const auto& bb : bare) {
            ui::iconFace(mem, {x, y + scale(6), x + b, y + scale(6) + b}, bb.g, bb.tone, bb.on, bb.over);
            caption(x - scale(4), y + scale(8) + b, bb.name);
            x += scale(84);
        }
        y += rowH + scale(18);
    }

    // List rows and row actions.
    y = ui::sectionHeader(mem, l, y, L"List rows and row actions");
    {
        const int w = std::min(scale(520), l.colRight - col);
        const std::pair<const wchar_t*, int> rows[] = {{L"Terran Marine", 0}, {L"Terran Ghost (selected)", 1}, {L"Terran Vulture (changed)", 2}, {L"Under the mouse: its actions", 3}};
        label(L"Rows");
        for (const auto& [text, kind] : rows) {
            RECT r{col, y, col + w, y + scale(ui::kRowH)};
            const bool on = kind == 1 || (gKit.selected && kind == 0);
            ui::listRow(mem, r, on, kind == 3 || ui::hot(r));
            SetTextColor(mem, ui::rowText(on));
            RECT t{r.left + scale(10), r.top, r.right - scale(100), r.bottom};
            DrawTextW(mem, text, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            if (kind == 2) ui::changedDot(mem, r.right - scale(12), (r.top + r.bottom) / 2);
            hitsOut().push_back({r, nullptr, [] { gKit.selected = !gKit.selected, InvalidateRect(gKit.wnd, nullptr, FALSE); }, L"", IDC_HAND, {}});
            if (kind != 2)
                ui::rowActions(mem, r,
                               {{L"\xE7E8", L"Turn off", click, ui::Tone::Quiet},
                                {UiIcon::Duplicate, L"Duplicate", click},
                                {UiIcon::MoveUp, L"Earlier", click, ui::Tone::Normal, false, false},
                                {UiIcon::Close, L"Remove", click, ui::Tone::Danger}},
                               kind == 3);
            y += scale(ui::kRowH);
        }
        caption(col, y + scale(3), L"actions show on the row under the mouse (or the selected one); a move that cannot be done is faint");
        y += scale(28);
    }

    // Tables.
    y = ui::sectionHeader(mem, l, y, L"Table");
    {
        label(L"Header, rows, frame");
        const std::vector<ui::Column> cols = {{L"Name", 0}, {L"Kind", 90}, {L"Length", 80, DT_RIGHT}, {L"Size", 80, DT_RIGHT}, {L"", 30}};
        const int w = std::min(scale(620), l.colRight - col), rh = scale(ui::kRowH), top = y;
        ui::tableHeader(mem, {col, y, col + w, y + rh}, cols);
        y += rh;
        const wchar_t* rows[][4] = {{L"staredit\\wav\\Laser Pistol.wav", L"WAV", L"0:00.5", L"78 KB"},
                                    {L"staredit\\wav\\Mission Start.wav", L"WAV", L"0:01.1", L"197 KB"},
                                    {L"sound\\misc\\Buzz.wav", L"Game WAV", L"", L"\x2014"},
                                    {L"staredit\\wav\\Gone.wav", L"Missing", L"", L"\x2014"}};
        for (int i = 0; i < 4; ++i) {
            RECT r{col, y, col + w, y + rh};
            ui::tableRow(mem, r, i == gKit.tableSel, ui::hot(r));
            ui::tableText(mem, r, cols, 0, rows[i][0], ui::rowText(i == gKit.tableSel));
            ui::tableText(mem, r, cols, 1, rows[i][1], i == 3 ? kBad : kDim);
            ui::tableText(mem, r, cols, 2, rows[i][2], kDim);
            ui::tableText(mem, r, cols, 3, rows[i][3], kDim);
            hitsOut().push_back({r, nullptr, [i] { gKit.tableSel = i, InvalidateRect(gKit.wnd, nullptr, FALSE); }, L"", IDC_HAND, {}});
            removeCross(mem, r, ui::tableCell(r, cols, 4), click, L"Remove");
            y += rh;
        }
        ui::tableFrame(mem, {col, top, col + w, y + 1});
        y += scale(14);
    }

    // A list that can be reordered: drag a row by its grip.
    y = ui::sectionHeader(mem, l, y, L"List that can be reordered");
    {
        label(L"Drag by the grip");
        const int w = std::min(scale(420), (l.colRight - col) / 2 - scale(20));
        {  // beside it, frozen: a row being dragged, and the line where it goes
            const int fx = col + w + scale(40), rh = scale(ui::kRowH);
            const wchar_t* names[] = {L"scripts/intro.eps", L"scripts/waves.eps", L"scripts/boss.eps", L"scripts/main.eps"};
            for (int i = 0; i < 4; ++i) {
                RECT r{fx, y + i * rh, fx + w, y + (i + 1) * rh};
                ui::listRow(mem, r, false, i == 1);
                if (i == 1) fillAlpha(mem, r, kSel, 160), ui::grip(mem, r.left + scale(5), (r.top + r.bottom) / 2 - scale(6), true);
                HGDIOBJ f2 = SelectObject(mem, monoFont());
                SetTextColor(mem, kText);
                RECT t{r.left + scale(18), r.top, r.right, r.bottom};
                DrawTextW(mem, names[i], -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                SelectObject(mem, f2);
            }
            ui::dropLine(mem, fx, fx + w, y + 3 * rh);
            caption(fx, y + 4 * rh + scale(3), L"while a drag: the row taken, and the line where it goes");
        }
        for (size_t i = 0; i < gKit.order.size(); ++i) {
            RECT r{col, y, col + w, y + scale(ui::kRowH)};
            const bool fixed = i == 0;  // as generated scripts: they stay first
            ui::listRow(mem, r, false, ui::hot(r));
            HGDIOBJ f2 = SelectObject(mem, monoFont());
            SetTextColor(mem, fixed ? kDim : kText);
            RECT t{r.left + scale(18), r.top, r.right - scale(120), r.bottom};
            DrawTextW(mem, gKit.order[i].c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(mem, f2);
            ui::reorderRow(mem, "gallery", r, int(i), [](int from, int to) {
                if (to == 0) to = 1;  // nothing goes above the fixed one
                const std::wstring moved = gKit.order[size_t(from)];
                gKit.order.erase(gKit.order.begin() + from);
                gKit.order.insert(gKit.order.begin() + to, moved);
                InvalidateRect(gKit.wnd, nullptr, FALSE);
            }, !fixed);
            ui::rowActions(mem, r, {{UiIcon::MoveUp, L"Earlier", click, ui::Tone::Normal, false, i > 1}, {UiIcon::MoveDown, L"Later", click, ui::Tone::Normal, false, i > 0 && i + 1 < gKit.order.size()}}, false);
            y += scale(ui::kRowH);
        }
        ui::reorderDropLine(mem, "gallery");
        caption(col, y + scale(3), L"The first row cannot move. Earlier and Later (on the row under the mouse) also move a row.");
        y += scale(28);
    }

    // Check boxes.
    y = ui::sectionHeader(mem, l, y, L"Check boxes");
    {
        label(L"Painted");
        int x = ui::check(mem, {col, y, col + scale(200), y + rowH}, L"EUD Turbo", gKit.check1, [] { gKit.check1 = !gKit.check1, InvalidateRect(gKit.wnd, nullptr, FALSE); }, L"A live check box");
        x = ui::check(mem, {x + scale(30), y, x + scale(250), y + rowH}, L"Air units do not push apart", gKit.check2,
                      [] { gKit.check2 = !gKit.check2, InvalidateRect(gKit.wnd, nullptr, FALSE); });
        x = ui::check(mem, {x + scale(30), y, x + scale(220), y + rowH}, L"Changed", true, click, L"", true);
        ui::check(mem, {x + scale(30), y, x + scale(220), y + rowH}, L"Not usable", false, nullptr, L"", false, false);
        y += rowH;
        label(L"Radio");
        ui::radioMark(mem, {col, y + scale(11), col + scale(14), y + scale(25)}, true, false);
        RECT t{col + scale(22), y, col + scale(120), y + rowH};
        SetTextColor(mem, kText);
        DrawTextW(mem, L"Map", -1, &t, DT_SINGLELINE | DT_VCENTER);
        ui::radioMark(mem, {col + scale(120), y + scale(11), col + scale(134), y + scale(25)}, false, false);
        RECT t2{col + scale(142), y, col + scale(300), y + rowH};
        DrawTextW(mem, L"EUD project", -1, &t2, DT_SINGLELINE | DT_VCENTER);
        y += rowH + scale(6);
    }

    // Fields and dropdowns.
    y = ui::sectionHeader(mem, l, y, L"Fields and dropdowns");
    {
        label(L"Field");
        const std::pair<const wchar_t*, ui::Field> fields[] = {{L"rest", ui::Field::Normal}, {L"under the mouse", ui::Field::Hot}, {L"focus", ui::Field::Focus},
                                                               {L"changed", ui::Field::Changed}, {L"read only", ui::Field::ReadOnly}};
        int x = col;
        for (const auto& [name, f] : fields) {
            const RECT r{x, y + scale(4), x + scale(110), y + scale(30)};
            ui::fieldBox(mem, r, f);
            SetTextColor(mem, f == ui::Field::ReadOnly ? kFaint : f == ui::Field::Changed ? kGood : kText);
            HGDIOBJ f2 = SelectObject(mem, monoFont());
            RECT t{r.left, r.top, r.right - scale(8), r.bottom};
            DrawTextW(mem, L"10240", -1, &t, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
            SelectObject(mem, f2);
            caption(x, r.bottom + scale(3), name);
            x += scale(122);
        }
        y += rowH + scale(18);
        label(L"Dropdown");
        const int w = scale(300);
        ui::pickBox(mem, {col, y + scale(4), col + w, y + scale(30)}, L"Gauss Rifle (Normal)", false, [](RECT) {}, L"A dropdown", datRefIcon("weapons", 0), 0,
                    [] {});
        ui::pickBox(mem, {col + w + scale(50), y + scale(4), col + 2 * w + scale(50), y + scale(30)}, L"Small", true, [](RECT) {}, L"Changed", nullptr, 1);
        y += rowH + scale(4);
        label(L"");
        ui::pickBox(mem, {col, y + scale(4), col + w, y + scale(30)}, L"Terran Infantry Armor", false, [](RECT) {}, L"Not usable", datRefIcon("upgrades", 0), 0,
                    nullptr, false);
        caption(col, y + scale(33), L"not usable");
        y += rowH + scale(18);
        label(L"Slider");
        for (int k = 0; k < 3; ++k) {  // works; under the mouse; not usable
            const int x0 = col + scale(8) + k * scale(230), x1 = x0 + scale(180), cy = y + rowH / 2;
            const RECT area{x0 - scale(8), y, x1 + scale(8), y + rowH};
            ui::sliderFace(mem, x0, x1, cy, k == 0 ? gKit.slider : 0.6f, k == 1 || (k == 0 && ui::hot(area)), k != 2);
            if (k == 0)
                hitsOut().push_back({area, [x0, x1](POINT p) {
                                         gKit.slider = std::clamp(float(p.x - x0) / float(std::max(1, x1 - x0)), 0.0f, 1.0f);
                                         InvalidateRect(gKit.wnd, nullptr, FALSE);
                                     }, nullptr, L"A slider", IDC_HAND, {}});
            caption(x0 - scale(8), y + rowH, k == 0 ? L"rest" : k == 1 ? L"under the mouse" : L"not usable");
        }
        y += rowH + scale(18);
    }

    // Add rows: a row that cannot be used is gray, with no click (its tooltip says why). The multi-pick list keeps open
    // while values are checked (showListPickerMulti); OK keeps them, Cancel leaves them.
    y = ui::sectionHeader(mem, l, y, L"Add rows and multi-pick");
    {
        label(L"Add row");
        ui::addRow(mem, {col, y + scale(3), col + scale(260), y + scale(3) + scale(30)}, L"Add a value\x2026", [] {
            static std::set<std::string> checked;
            std::vector<ListPickItem> items;
            for (int u = 0; u < 4; ++u) {
                ListPickItem it;
                it.label = u == 0 ? L"Terran Marine" : u == 1 ? L"Terran Ghost" : u == 2 ? L"Terran Vulture" : L"Terran Goliath";
                it.value = std::to_string(u);
                items.push_back(it);
            }
            POINT at{0, 0};
            RECT rc{};
            GetWindowRect(gKit.wnd, &rc);
            at = {rc.left + 40, rc.top + 120};
            if (showListPickerMulti(L"Search units", items, checked, L"Add", at)) ++gKit.clicks;
            InvalidateRect(gKit.wnd, nullptr, FALSE);
        }, L"Add a value (the list stays open while values are checked)");
        ui::addRow(mem, {col + scale(280), y + scale(3), col + scale(540), y + scale(3) + scale(30)}, L"Add a trigger", click,
                   L"Not usable: the list is full", false);
        caption(col, y + scale(36), L"left: the row; right: not usable (gray, its tooltip gives the reason). Click the left one: a multi-pick list");
        y += scale(36) + scale(18);
    }

    // Value chips (trigger sentences, briefing actions).
    y = ui::sectionHeader(mem, l, y, L"Value chips");
    {
        label(L"In a sentence");
        int x = col;
        auto word = [&](const wchar_t* t) {
            RECT r{x, y, x + ui::textWidth(mem, t), y + rowH};
            SetTextColor(mem, kText);
            DrawTextW(mem, t, -1, &r, DT_SINGLELINE | DT_VCENTER);
            x = r.right + scale(6);
        };
        struct C {
            const wchar_t* t;
            bool over, dim, bad, focused;
        };
        auto chip = [&](C c) {
            const int w = ui::textWidth(mem, c.t) + scale(12);
            RECT r{x, y + scale(7), x + w, y + rowH - scale(7)};
            ui::chipFace(mem, r, c.over, c.dim, c.bad, c.focused);
            SetTextColor(mem, c.bad ? kBad : c.dim ? kFaint : kAccent);
            DrawTextW(mem, c.t, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            x = r.right + scale(6);
        };
        word(L"create");
        chip({L"5", false, false, false, false});
        chip({L"Terran Marine", true, false, false, false});
        word(L"at");
        chip({L"Anywhere", false, false, false, true});
        word(L"for");
        chip({L"(player)", false, false, true, false});
        word(L"   off:");
        chip({L"Player 1", false, true, false, false});
        y += rowH;
        caption(col, y, L"rest  \x00B7  under the mouse  \x00B7  Tab moved to it  \x00B7  must be fixed  \x00B7  the block is off");
        y += scale(24);
    }

    // Titles, headings, labels; the References side bar; a painted scroll bar.
    y = ui::sectionHeader(mem, l, y, L"Titles, headings, labels");
    {
        const int top = y;
        y = ui::title(mem, l.x0, y, l.x0 + scale(500), datRefIcon("units", 0), L"Terran Marine", L"units.dat entry 0", true, click);
        FormLayout hl = l;
        hl.colRight = l.x0 + scale(500);
        y = ui::sectionHeader(mem, hl, y, L"Combat");
        hl.labelW = scale(200);
        ui::fieldLabel(mem, hl, y, L"Hit Points", false);
        ui::fieldLabel(mem, {hl.x0 + scale(200), hl.labelW, hl.rowH, hl.colW, hl.colRight, hl.picX, hl.picW, hl.twoCol}, y, L"Armor (changed)", true);
        y += hl.rowH;
        ui::fieldLabel(mem, hl, y, L"Infestation (not usable)", false, false);
        y += hl.rowH + scale(10);
        // The side bar beside them.
        const RECT side{l.x0 + scale(540), top, l.x0 + scale(800), y};
        ui::sidePanel(mem, side);
        int sy = ui::sideHeader(mem, side.left + scale(14), side.top + scale(10), scale(230), L"References", 3);
        sy = ui::sideGroup(mem, side.left + scale(14), sy, scale(230), L"Units", true);
        ui::sideLink(mem, side.left + scale(14), sy, scale(230), L"Uraj Crystal", datRefIcon("units", 128), false), sy += scale(22);
        ui::sideLink(mem, side.left + scale(14), sy, scale(230), L"Khalis Crystal (mouse)", datRefIcon("units", 129), true), sy += scale(22);
        sy = ui::sideGroup(mem, side.left + scale(14), sy, scale(230), L"Tech data", false);
        ui::sideLink(mem, side.left + scale(14), sy, scale(230), L"Stim Packs", nullptr, false);
        // A scroll bar.
        const int sx = side.right + scale(40);
        ui::scrollFace(mem, {sx, top, sx + scale(6), y}, {sx, top + scale(20), sx + scale(6), top + scale(80)}, false);
        ui::scrollFace(mem, {sx + scale(24), top, sx + scale(30), y}, {sx + scale(24), top + scale(40), sx + scale(30), top + scale(100)}, true);
        caption(sx - scale(10), y + scale(4), L"scroll bar, rest / held");
        y += scale(30);
    }
    {
        RECT r{l.x0, y, l.colRight, y + scale(24)};
        SetTextColor(mem, kDim);
        DrawTextW(mem, (L"Clicks on the live controls: " + std::to_wstring(gKit.clicks)).c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER);
        y += scale(40);
    }

    gKit.height = y + gKit.scroll;
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, gKit.height - 1), UINT(rc.bottom), gKit.scroll};
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    gKit.surface.end(hwnd);
}

LRESULT CALLBACK kitGalleryProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r; gKit.surface.message(hwnd, msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_PAINT: paintKitGallery(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL:
    case WM_VSCROLL: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int s = msg == WM_MOUSEWHEEL ? gKit.scroll - GET_WHEEL_DELTA_WPARAM(wp) * scale(40) * 3 / WHEEL_DELTA : scrollFromMessage(hwnd, wp, gKit.scroll, scale(40));
        gKit.scroll = std::clamp(s, 0, std::max(0, gKit.height - int(rc.bottom)));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY: gKit.wnd = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// View > Control kit: the gallery in its own window (background test runs keep it off the screen).
void showKitGallery() {
    if (gKit.wnd) return void(SetForegroundWindow(gKit.wnd));
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = kitGalleryProc, wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpszClassName = L"StarDraftKitGallery";
        RegisterClassExW(&wc);
        registered = true;
    }
    int x = CW_USEDEFAULT, yy = CW_USEDEFAULT;
    if (gBackground) x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 100, yy = 0;
    gKit.wnd = CreateWindowExW(gBackground ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0, L"StarDraftKitGallery", L"Control kit", WS_OVERLAPPEDWINDOW | WS_VSCROLL, x, yy,
                               scale(1180), scale(1000), app.main, nullptr, app.inst, nullptr);
    BOOL dark = !gLightTheme;
    DwmSetWindowAttribute(gKit.wnd, 20, &dark, sizeof dark), titleColors(gKit.wnd);
    cleanScroll(gKit.wnd);
    darkControl(gKit.wnd, L"DarkMode_Explorer");  // the scroll bar in the theme
    ShowWindow(gKit.wnd, gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
}
