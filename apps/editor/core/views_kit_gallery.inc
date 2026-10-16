// View > Control kit (portable, core.inc): every control of the kit in each of its states, on one page; the controls work
// (they repaint under the mouse and take clicks), so a change to the kit shows here first. docs/dev/CONTROLS.md lists
// where each is used. The page is a view (KitGalleryView) painted with the kit's faces (kit_paint.inc) on a painted
// surface (kit_surface.inc); it scrolls with its window's own scroll bar and the mouse wheel. The Windows host is
// kit_gallery.inc (the window, the entries' icons, the multi-pick list).

class KitGalleryView final : public draw::View {
public:
    // The host's part: an entry's icon (a .dat's entry: "units", "weapons", "upgrades"), and the multi-pick list that
    // the first add row opens (true: values were added).
    std::function<draw::Image(const char* dat, int id)> refIcon;
    std::function<bool()> pickValues;

    int scroll = 0, pageH = 0;  // the page's scroll and its whole height (from the last paint)
    bool check1 = true, check2 = false, selected = false;
    int clicks = 0, tableSel = 1;
    float slider = 0.4f;
    std::vector<std::wstring> order = {L"generated/strings.eps", L"scripts/intro.eps", L"scripts/waves.eps", L"scripts/boss.eps", L"scripts/main.eps"};

    void setScroll(int s) {
        scroll = std::clamp(s, 0, std::max(0, pageH - height()));
        invalidate();
    }

    void paint(draw::Canvas& c) override {
        using kitpaint::Tone;
        using State = kitpaint::ButtonState;
        using draw::Rect;
        s_.begin();
        const int W = width();
        c.fill({0, 0, W, height()}, kBg);
        const draw::Font uiFont = c.setFont(c.font({}));
        const int x0 = scale(18), labelW = scale(150), colRight = W - scale(24), formRowH = scale(stardraft::kit::kRowH);
        int y = scale(16) - scroll;
        const int col = x0 + labelW, gap = scale(12), rowH = scale(36);
        auto label = [&](const wchar_t* t) { kitpaint::drawFieldLabel(c, {x0, y, x0 + labelW, y + rowH}, t, false); };
        auto caption = [&](int x, int yy, const wchar_t* t) {  // a state's name under its sample
            const draw::Font of = c.setFont(c.font(kitpaint::smallFontDesc()));
            c.text({x, yy, x + scale(600), yy + scale(14)}, t, kFaint, 0);
            c.setFont(of);
        };
        auto mono = [&](Rect r, const std::wstring& t, draw::Color col, unsigned flags) {
            const draw::Font of = c.setFont(c.font({draw::Face::Mono}));
            c.text(r, t, col, flags);
            c.setFont(of);
        };
        auto icon = [&](const char* dat, int id) { return refIcon ? refIcon(dat, id) : draw::Image{}; };
        auto click = [this] { ++clicks, invalidate(); };
        auto section = [&](const wchar_t* name) { y = kitpaint::drawSectionHeader(c, x0, colRight, y, formRowH, name); };

        y = kitui::title(c, s_, x0, y, colRight, {}, L"Control kit", L"apps/editor/ui_kit.inc  \x00B7  where each is used: docs/dev/CONTROLS.md");

        // Colors.
        section(L"Colors");
        {
            const std::pair<const wchar_t*, draw::Color> tokens[] = {
                {L"kBg", kBg},         {L"kPanel", kPanel},           {L"kCtl", kCtl},   {L"kCtlHot", kCtlHot}, {L"kReadOnly", kReadOnly},
                {L"kBorder", kBorder}, {L"kBorderHot", kBorderHot},   {L"kText", kText}, {L"kDim", kDim},       {L"kFaint", kFaint},
                {L"kAccent", kAccent}, {L"kAccentLine", kAccentLine}, {L"kSel", kSel},   {L"kRowHot", kRowHot}, {L"kGood", kGood},
                {L"kGoodFill", kGoodFill}, {L"kWarn", kWarn},         {L"kBad", kBad}};
            int x = x0;
            for (const auto& [name, color] : tokens) {
                if (x + scale(96) > colRight) x = x0, y += scale(54);
                c.roundBox({x, y, x + scale(88), y + scale(30)}, color, kBorderHot, float(scale(stardraft::kit::kRadius)));
                caption(x, y + scale(33), name);
                x += scale(96);
            }
            y += scale(62);
        }

        // Buttons: tones across, states down.
        section(L"Buttons");
        {
            const std::pair<const wchar_t*, State> states[] = {{L"rest", State::Normal}, {L"under the mouse", State::Hot}, {L"pressed", State::Down}, {L"not usable", State::Disabled}};
            const std::pair<const wchar_t*, Tone> tones[] = {{L"Normal", Tone::Normal}, {L"Primary", Tone::Primary}, {L"Danger", Tone::Danger}};
            for (const auto& [tname, tone] : tones) {
                label(tname);
                int x = col;
                for (const auto& [sname, st] : states) {
                    const Rect r{x, y + scale(4), x + scale(120), y + scale(4 + stardraft::kit::kButtonH)};
                    kitpaint::drawButtonFace(c, r, tname == std::wstring(L"Danger") ? L"Remove" : tname == std::wstring(L"Primary") ? L"OK" : L"Browse\x2026", tone, st);
                    caption(x, r.bottom + scale(3), sname);
                    x += scale(132);
                }
                kitui::button(c, s_, {x + scale(20), y + scale(4), x + scale(160), y + scale(4 + stardraft::kit::kButtonH)}, L"Try me", tone, click,
                              L"A live button: clicks count");
                y += rowH + scale(18);
            }
            label(L"Right-aligned row");
            kitui::buttonsRight(c, s_, col + scale(560), y + scale(4), {{L"OK", Tone::Primary, click}, {L"Cancel", Tone::Normal, click}});
            y += rowH + scale(6);
            label(L"Segmented choice");
            const wchar_t* modes[] = {L"Terrain", L"Units", L"Doodads", L"Locations"};
            for (int i = 0; i < 4; ++i)
                kitpaint::drawButtonFace(c, {col + i * scale(100), y + scale(4), col + i * scale(100) + scale(96), y + scale(4 + 26)}, modes[i],
                                         i == 1 ? Tone::Primary : Tone::Normal, State::Normal);
            y += rowH + scale(6);
        }

        // Links.
        section(L"Links");
        {
            label(L"Primary");
            int x = kitui::smallButton(c, s_, col, y, rowH, L"Add an action", click, L"A small button: an action in a row or under a list");
            x = kitui::smallButton(c, s_, x + gap, y, rowH, L"Reset", click);
            x = kitui::smallButton(c, s_, x + gap, y, rowH, L"Copy a button from a set\x2026", click);
            kitui::link(c, s_, x + gap * 2, y, rowH, L"Terran Marine", click, L"A link: only to an entity, by its name; underlined under the mouse");
            y += rowH;
            label(L"Quiet, Danger");
            x = kitui::smallButton(c, s_, col, y, rowH, L"Add a condition", click);
            x = kitui::smallButton(c, s_, x + gap, y, rowH, L"Rename", click);
            kitui::smallButton(c, s_, x + gap, y, rowH, L"Remove the list", click, L"", Tone::Danger);
            y += rowH + scale(6);
        }

        // Icon buttons.
        section(L"Icon buttons");
        {
            const int sz = scale(32), b = scale(stardraft::kit::kIconBox);
            label(L"Boxed");
            int x = col;
            const UiIcon glyphs[] = {UiIcon::ToStart, UiIcon::Play, UiIcon::ToEnd, UiIcon::Loop, UiIcon::Sound, UiIcon::Code};
            for (int i = 0; i < 6; ++i) {
                kitui::iconButton(c, s_, {x, y + scale(2), x + sz, y + scale(2) + sz}, glyphs[i], click, L"Boxed", kitui::Icon::Boxed, i == 3);
                x += sz + scale(6);
            }
            caption(col, y + sz + scale(4), L"the fourth is a toggle that is on");
            kitui::iconButton(c, s_, {x + gap, y + scale(2), x + gap + sz, y + scale(2) + sz}, UiIcon::Play, click, L"Not usable", kitui::Icon::Boxed, false, false);
            caption(x + gap, y + sz + scale(4), L"not usable");
            y += rowH + scale(16);
            label(L"Bare");
            x = col;
            struct B {
                kitpaint::Ico g;
                const wchar_t* name;
                Tone tone;
                bool on, over;
            } bare[] = {{UiIcon::OpenExternal, L"open", Tone::Primary, false, false}, {UiIcon::OpenExternal, L"open, mouse", Tone::Primary, false, true},
                        {UiIcon::Duplicate, L"duplicate", Tone::Normal, false, false}, {UiIcon::Duplicate, L"mouse", Tone::Normal, false, true},
                        {UiIcon::Delete, L"delete, mouse", Tone::Danger, false, true},  {L"\xE7E8", L"turned off", Tone::Quiet, true, false}};
            for (const auto& bb : bare) {
                kitpaint::drawIconFace(c, {x, y + scale(6), x + b, y + scale(6) + b}, bb.g, bb.tone, bb.on, bb.over);
                caption(x - scale(4), y + scale(8) + b, bb.name);
                x += scale(84);
            }
            y += rowH + scale(18);
        }

        // List rows and row actions.
        section(L"List rows and row actions");
        {
            const int w = std::min(scale(520), colRight - col);
            const std::pair<const wchar_t*, int> rows[] = {{L"Terran Marine", 0}, {L"Terran Ghost (selected)", 1}, {L"Terran Vulture (changed)", 2}, {L"Under the mouse: its actions", 3}};
            label(L"Rows");
            for (const auto& [text, kind] : rows) {
                const Rect r{col, y, col + w, y + scale(stardraft::kit::kRowH)};
                const bool on = kind == 1 || (selected && kind == 0);
                kitpaint::drawListRow(c, r, on, kind == 3 || s_.hot(r));
                c.text({r.left + scale(10), r.top, r.right - scale(100), r.bottom}, text, kitpaint::rowText(on), draw::kMiddle);
                if (kind == 2) kitpaint::drawChangedDot(c, r.right - scale(12), (r.top + r.bottom) / 2);
                s_.add({r, nullptr, [this] { selected = !selected, invalidate(); }, L"", draw::Cursor::Hand});
                if (kind != 2)
                    kitui::rowActions(c, s_, r,
                                      {{L"\xE7E8", L"Turn off", click, Tone::Quiet},
                                       {UiIcon::Duplicate, L"Duplicate", click},
                                       {UiIcon::MoveUp, L"Earlier", click, Tone::Normal, false, false},
                                       {UiIcon::Close, L"Remove", click, Tone::Danger}},
                                      kind == 3);
                y += scale(stardraft::kit::kRowH);
            }
            caption(col, y + scale(3), L"actions show on the row under the mouse (or the selected one); a move that cannot be done is faint");
            y += scale(28);
        }

        // Tables.
        section(L"Table");
        {
            label(L"Header, rows, frame");
            const std::vector<kitpaint::Column> cols = {{L"Name", 0}, {L"Kind", 90}, {L"Length", 80, draw::kRight}, {L"Size", 80, draw::kRight}, {L"", 30}};
            const int w = std::min(scale(620), colRight - col), rh = scale(stardraft::kit::kRowH);
            kitpaint::drawTableHeader(c, {col, y, col + w, y + rh}, cols);
            y += rh;
            const wchar_t* rows[][4] = {{L"staredit\\wav\\Laser Pistol.wav", L"WAV", L"0:00.5", L"78 KB"},
                                        {L"staredit\\wav\\Mission Start.wav", L"WAV", L"0:01.1", L"197 KB"},
                                        {L"sound\\misc\\Buzz.wav", L"Game WAV", L"", L"\x2014"},
                                        {L"staredit\\wav\\Gone.wav", L"Missing", L"", L"\x2014"}};
            for (int i = 0; i < 4; ++i) {
                const Rect r{col, y, col + w, y + rh};
                kitpaint::drawTableRow(c, r, i == tableSel, s_.hot(r));
                kitpaint::drawTableText(c, r, cols, 0, rows[i][0], kitpaint::rowText(i == tableSel));
                kitpaint::drawTableText(c, r, cols, 1, rows[i][1], i == 3 ? kBad : kDim);
                kitpaint::drawTableText(c, r, cols, 2, rows[i][2], kDim);
                kitpaint::drawTableText(c, r, cols, 3, rows[i][3], kDim);
                s_.add({r, nullptr, [this, i] { tableSel = i, invalidate(); }, L"", draw::Cursor::Hand});
                kitui::removeCross(c, s_, r, kitpaint::tableCell(r, cols, 4), click, L"Remove");
                y += rh;
            }
            y += scale(14);
        }

        // A list that can be reordered: drag a row by its grip.
        section(L"List that can be reordered");
        {
            label(L"Drag by the grip");
            const int w = std::min(scale(420), (colRight - col) / 2 - scale(20));
            {  // beside it, frozen: a row being dragged, and the line where it goes
                const int fx = col + w + scale(40), rh = scale(stardraft::kit::kRowH);
                const wchar_t* names[] = {L"scripts/intro.eps", L"scripts/waves.eps", L"scripts/boss.eps", L"scripts/main.eps"};
                for (int i = 0; i < 4; ++i) {
                    const Rect r{fx, y + i * rh, fx + w, y + (i + 1) * rh};
                    kitpaint::drawListRow(c, r, false, i == 1);
                    if (i == 1) c.fill(r, kSel, 160), kitpaint::drawGrip(c, r.left + scale(5), (r.top + r.bottom) / 2 - scale(6), true);
                    mono({r.left + scale(18), r.top, r.right, r.bottom}, names[i], kText, draw::kMiddle);
                }
                kitpaint::drawDropLine(c, fx, fx + w, y + 3 * rh);
                caption(fx, y + 4 * rh + scale(3), L"while a drag: the row taken, and the line where it goes");
            }
            for (size_t i = 0; i < order.size(); ++i) {
                const Rect r{col, y, col + w, y + scale(stardraft::kit::kRowH)};
                const bool fixed = i == 0;  // as generated scripts: they stay first
                kitpaint::drawListRow(c, r, false, s_.hot(r));
                mono({r.left + scale(18), r.top, r.right - scale(120), r.bottom}, order[i], fixed ? kDim : kText, draw::kMiddle);
                kitui::reorderRow(c, s_, "gallery", r, int(i), [this](int from, int to) {
                    if (to == 0) to = 1;  // nothing goes above the fixed one
                    const std::wstring moved = order[size_t(from)];
                    order.erase(order.begin() + from);
                    order.insert(order.begin() + to, moved);
                    invalidate();
                }, !fixed);
                kitui::rowActions(c, s_, r,
                                  {{UiIcon::MoveUp, L"Earlier", click, Tone::Normal, false, i > 1},
                                   {UiIcon::MoveDown, L"Later", click, Tone::Normal, false, i > 0 && i + 1 < order.size()}},
                                  false);
                y += scale(stardraft::kit::kRowH);
            }
            kitui::reorderDropLine(c, s_, "gallery");
            caption(col, y + scale(3), L"The first row cannot move. Earlier and Later (on the row under the mouse) also move a row.");
            y += scale(28);
        }

        // Check boxes.
        section(L"Check boxes");
        {
            label(L"Painted");
            int x = kitui::check(c, s_, {col, y, col + scale(200), y + rowH}, L"EUD Turbo", check1, [this] { check1 = !check1, invalidate(); }, L"A live check box");
            x = kitui::check(c, s_, {x + scale(30), y, x + scale(250), y + rowH}, L"Air units do not push apart", check2, [this] { check2 = !check2, invalidate(); });
            x = kitui::check(c, s_, {x + scale(30), y, x + scale(220), y + rowH}, L"Changed", true, click, L"", true);
            kitui::check(c, s_, {x + scale(30), y, x + scale(220), y + rowH}, L"Not usable", false, nullptr, L"", false, false);
            y += rowH;
            label(L"Radio");
            kitpaint::drawRadioMark(c, {col, y + scale(11), col + scale(14), y + scale(25)}, true, false);
            c.text({col + scale(22), y, col + scale(120), y + rowH}, L"Map", kText, draw::kMiddle);
            kitpaint::drawRadioMark(c, {col + scale(120), y + scale(11), col + scale(134), y + scale(25)}, false, false);
            c.text({col + scale(142), y, col + scale(300), y + rowH}, L"EUD project", kText, draw::kMiddle);
            y += rowH + scale(6);
        }

        // Fields and dropdowns.
        section(L"Fields and dropdowns");
        {
            using kitpaint::Field;
            label(L"Field");
            const std::pair<const wchar_t*, Field> fields[] = {{L"rest", Field::Normal}, {L"under the mouse", Field::Hot}, {L"focus", Field::Focus},
                                                               {L"changed", Field::Changed}, {L"read only", Field::ReadOnly}};
            int x = col;
            for (const auto& [name, f] : fields) {
                const Rect r{x, y + scale(4), x + scale(110), y + scale(30)};
                kitpaint::drawFieldBox(c, r, f);
                mono({r.left, r.top, r.right - scale(8), r.bottom}, L"10240", f == Field::ReadOnly ? kFaint : f == Field::Changed ? kGood : kText,
                     draw::kMiddle | draw::kRight);
                caption(x, r.bottom + scale(3), name);
                x += scale(122);
            }
            y += rowH + scale(18);
            label(L"Dropdown");
            const int w = scale(300);
            kitui::pickBox(c, s_, {col, y + scale(4), col + w, y + scale(30)}, L"Gauss Rifle (Normal)", false, [](Rect) {}, L"A dropdown", icon("weapons", 0), 0, [] {});
            kitui::pickBox(c, s_, {col + w + scale(50), y + scale(4), col + 2 * w + scale(50), y + scale(30)}, L"Small", true, [](Rect) {}, L"Changed", {}, 1);
            y += rowH + scale(4);
            label(L"");
            kitui::pickBox(c, s_, {col, y + scale(4), col + w, y + scale(30)}, L"Terran Infantry Armor", false, [](Rect) {}, L"Not usable", icon("upgrades", 0), 0,
                           nullptr, false);
            caption(col, y + scale(33), L"not usable");
            y += rowH + scale(18);
            label(L"Slider");
            for (int k = 0; k < 3; ++k) {  // works; under the mouse; not usable
                const int sx0 = col + scale(8) + k * scale(230), sx1 = sx0 + scale(180), cy = y + rowH / 2;
                const Rect area{sx0 - scale(8), y, sx1 + scale(8), y + rowH};
                kitpaint::drawSliderFace(c, sx0, sx1, cy, k == 0 ? slider : 0.6f, k == 1 || (k == 0 && s_.hot(area)), k != 2);
                if (k == 0)
                    s_.add({area,
                            [this, sx0, sx1](draw::Point p) {
                                slider = std::clamp(float(p.x - sx0) / float(std::max(1, sx1 - sx0)), 0.0f, 1.0f);
                                invalidate();
                            },
                            nullptr, L"A slider", draw::Cursor::Hand});
                caption(sx0 - scale(8), y + rowH, k == 0 ? L"rest" : k == 1 ? L"under the mouse" : L"not usable");
            }
            y += rowH + scale(18);
        }

        // Add rows: a row that cannot be used is gray, with no click (its tooltip says why). The multi-pick list keeps open
        // while values are checked; OK keeps them, Cancel leaves them.
        section(L"Add rows and multi-pick");
        {
            label(L"Add row");
            kitui::addRow(c, s_, {col, y + scale(3), col + scale(260), y + scale(3) + scale(30)}, L"Add a value\x2026", [this] {
                if (pickValues && pickValues()) ++clicks;
                invalidate();
            }, L"Add a value (the list stays open while values are checked)");
            kitui::addRow(c, s_, {col + scale(280), y + scale(3), col + scale(540), y + scale(3) + scale(30)}, L"Add a trigger", click, L"Not usable: the list is full",
                          false);
            caption(col, y + scale(36),
                    L"left: the row; right: not usable (gray, its tooltip gives the reason). Click the left one: a multi-pick list. In it: Space checks a row, Enter confirms (OK), Esc cancels, Tab does nothing");
            y += scale(36) + scale(18);
        }

        // Value chips (trigger sentences, briefing actions).
        section(L"Value chips");
        {
            label(L"In a sentence");
            int x = col;
            auto word = [&](const wchar_t* t) {
                const Rect r{x, y, x + c.measure(t), y + rowH};
                c.text(r, t, kText, draw::kMiddle);
                x = r.right + scale(6);
            };
            struct C {
                const wchar_t* t;
                bool over, dim, bad, focused;
            };
            auto chip = [&](C ch) {
                const int w = c.measure(ch.t) + scale(12);
                const Rect r{x, y + scale(7), x + w, y + rowH - scale(7)};
                kitpaint::drawChipFace(c, r, ch.over, ch.dim, ch.bad, ch.focused);
                c.text(r, ch.t, ch.bad ? kBad : ch.dim ? kFaint : kAccent, draw::kMiddle | draw::kCenter);
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
        section(L"Titles, headings, labels");
        {
            const int top = y;
            y = kitui::title(c, s_, x0, y, x0 + scale(500), icon("units", 0), L"Terran Marine", L"units.dat entry 0", true, click);
            y = kitpaint::drawSectionHeader(c, x0, x0 + scale(500), y, formRowH, L"Combat");
            const int lw = scale(200);
            kitpaint::drawFieldLabel(c, {x0, y, x0 + lw, y + formRowH}, L"Hit Points", false);
            kitpaint::drawFieldLabel(c, {x0 + scale(200), y, x0 + scale(200) + lw, y + formRowH}, L"Armor (changed)", true);
            y += formRowH;
            kitpaint::drawFieldLabel(c, {x0, y, x0 + lw, y + formRowH}, L"Infestation (not usable)", false, false);
            y += formRowH + scale(10);
            // The side bar beside them.
            const Rect side{x0 + scale(540), top, x0 + scale(800), y};
            kitpaint::drawSidePanel(c, side);
            int sy = kitpaint::drawSideHeader(c, side.left + scale(14), side.top + scale(10), scale(230), L"References", 3);
            sy = kitpaint::drawSideGroup(c, side.left + scale(14), sy, scale(230), L"Units", true);
            kitpaint::drawSideLink(c, side.left + scale(14), sy, scale(230), L"Uraj Crystal", icon("units", 128), false), sy += scale(22);
            kitpaint::drawSideLink(c, side.left + scale(14), sy, scale(230), L"Khalis Crystal (mouse)", icon("units", 129), true), sy += scale(22);
            sy = kitpaint::drawSideGroup(c, side.left + scale(14), sy, scale(230), L"Tech data", false);
            kitpaint::drawSideLink(c, side.left + scale(14), sy, scale(230), L"Stim Packs", {}, false);
            // A scroll bar.
            const int sx = side.right + scale(40);
            kitpaint::drawScrollFace(c, {sx, top, sx + scale(6), y}, {sx, top + scale(20), sx + scale(6), top + scale(80)}, false);
            kitpaint::drawScrollFace(c, {sx + scale(24), top, sx + scale(30), y}, {sx + scale(24), top + scale(40), sx + scale(30), top + scale(100)}, true);
            caption(sx - scale(10), y + scale(4), L"scroll bar, rest / held");
            y += scale(30);
        }
        c.text({x0, y, colRight, y + scale(24)}, L"Clicks on the live controls: " + std::to_wstring(clicks), kDim, draw::kMiddle);
        y += scale(40);

        pageH = y + scroll;
        c.setFont(uiFont);
    }

    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Wheel && !e.horizontal) {
            setScroll(scroll - int(std::lround(draw::wheelDistance(e, float(scale(40) * 3)))));
            return true;
        }
        return s_.mouse(*this, e);
    }
    draw::Cursor cursor(draw::Point pt) override { return s_.cursor(pt); }
    std::wstring tip(draw::Point pt) override { return s_.tip(pt); }
    bool hoverTip() const override { return true; }
    void resized() override { invalidate(); }

    // The window's own scroll bar: the page's height, the window's height, the scroll.
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, pageH - 1), page = height(), pos = scroll;
        return true;
    }
    bool vscroll(int code, int /*pos*/, int track, int page) override {
        enum { kLineUp = 0, kLineDown = 1, kPageUp = 2, kPageDown = 3, kThumbPosition = 4, kThumbTrack = 5, kTop = 6, kBottom = 7 };  // SB_*
        int s = scroll;
        switch (code) {
        case kLineUp: s -= scale(40); break;
        case kLineDown: s += scale(40); break;
        case kPageUp: s -= page; break;
        case kPageDown: s += page; break;
        case kThumbTrack:
        case kThumbPosition: s = track; break;
        case kTop: s = 0; break;
        case kBottom: s = 1 << 30; break;
        default: return true;
        }
        setScroll(s);
        return true;
    }

private:
    kitui::Surface s_;
};

// The first add row's multi-pick: its units, and the ones checked in it (both programs' hosts open it with these).
std::vector<ListPickItem> kitGalleryPickItems() {
    std::vector<ListPickItem> items;
    for (int u = 0; u < 4; ++u) {
        ListPickItem it;
        it.label = u == 0 ? L"Terran Marine" : u == 1 ? L"Terran Ghost" : u == 2 ? L"Terran Vulture" : L"Terran Goliath";
        it.value = std::to_string(u);
        items.push_back(it);
    }
    return items;
}
std::set<std::string>& kitGalleryPicked() {
    static std::set<std::string> checked;
    return checked;
}
