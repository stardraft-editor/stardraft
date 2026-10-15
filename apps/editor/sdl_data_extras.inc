// The Data workspace's tables that are not .dat files, on this system (the Windows program's data_extras.inc): the
// Strings tab (the hotkey and type of a button's text, the core's game text editor and the game's preview), the Buttons
// tab (the command card, the set's buttons and the selected button's fields, with the button's tooltip) and the
// Requirements section of the Units, Upgrades, Tech Data and Orders forms (the core's paint, paintRequirementList, on the
// kit faces of core/kit_paint.inc). The model is the core's (core/data_model.inc: dataExtras(); views_data_strings.inc,
// views_data_buttons.inc, views_data_requirements.inc); a change is saved with its undo step as on Windows
// (sdlDataSaved). The sizes are the Windows form's (kitpaint::formLayout). Not on this system yet: the icons (the card
// shows each button's number), dragging a button to another place or row, the tooltip's own editor (a click opens the
// string in the Strings tab) and the value boxes' open buttons. Included by sdl_data.inc before the form's host, which
// calls sdlPaintExtraTab and sdlPaintRequirements and gives the click areas (sdlFormArea, sdlFormHot).

void sdlFormArea(draw::Rect r, std::function<void()> click, const std::wstring& tip, bool hand);
bool sdlFormHot(draw::Rect r);
void sdlDataSaved();
void sdlDataGoTo(const std::string& dat, int e);
extern DataFormView gSdlDataForm;

// A change to the strings, the button sets or the requirements: saved with its undo step, and the form repaints.
void sdlExtrasChanged() {
    sdlDataSaved();
    gSdlDataForm.invalidate();
}

// The list picker (sdlPickFromList, a modal kit dialog): true with the item's value in `out`.
bool sdlChoose(const std::wstring& prompt, const std::vector<ListPickItem>& items, const std::string& current, std::string& out) {
    std::vector<std::wstring> rows;
    int checked = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        rows.push_back(items[i].label);
        if (items[i].value == current) checked = int(i);
    }
    const int i = sdlPickFromList(rows, checked, prompt);
    if (i < 0 || i >= int(items.size())) return false;
    out = items[size_t(i)].value;
    return true;
}

// --- The kit faces with their click areas (the Windows program's ui_kit.inc parts) -----------------------------------

namespace sdlx {

// A dropdown's face; a click gets its box (ui::pickBox).
void pickBox(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool changed, std::function<void(draw::Rect)> click, const std::wstring& tip = L"",
             int number = -1) {
    kitpaint::drawPickFace(c, r, text, changed, sdlFormHot(r), {}, number);
    sdlFormArea(r, [r, click] { click(r); }, tip, true);
}
// A small button: text width + 20, at most 24 high, in the middle of the row (ui::smallButton). Its right edge.
int smallButton(draw::Canvas& c, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"") {
    const draw::Rect r = kitpaint::smallButtonRect(c, x, y, h, text);
    kitpaint::drawButtonFace(c, r, text, kitpaint::Tone::Normal, sdlFormHot(r) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
    sdlFormArea(r, std::move(click), tip, true);
    return r.right;
}
// A bare icon button (ui::iconButton, Icon::Bare).
void iconButton(draw::Canvas& c, draw::Rect r, kitpaint::Ico g, std::function<void()> click, const std::wstring& tip, kitpaint::Tone tone = kitpaint::Tone::Normal,
                bool enabled = true) {
    r = kitpaint::square(r);
    kitpaint::drawIconFace(c, r, g, tone, false, enabled && sdlFormHot(r), enabled);
    sdlFormArea(r, enabled ? std::move(click) : nullptr, tip, enabled);
}
// A link (ui::link); its right edge.
int link(draw::Canvas& c, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip,
         kitpaint::Tone tone = kitpaint::Tone::Primary) {
    const draw::Rect r{x, y, x + c.measure(text), y + h};
    kitpaint::drawLinkFace(c, r, text, tone, sdlFormHot(r));
    sdlFormArea(r, std::move(click), tip, true);
    return r.right;
}
// A check box with its label (ui::check).
void check(draw::Canvas& c, draw::Rect r, const std::wstring& label, bool on, std::function<void()> click, const std::wstring& tip) {
    const int s = scale(14);
    const draw::Rect b{r.left, (r.top + r.bottom - s) / 2, r.left + s, (r.top + r.bottom - s) / 2 + s};
    const bool over = sdlFormHot(r);
    kitpaint::drawCheckMark(c, b, on, over, true);
    c.text({b.right + scale(8), r.top, r.right, r.bottom}, label, on || over ? kText : kDim, draw::kMiddle | draw::kEllipsis);
    sdlFormArea(r, std::move(click), tip, true);
}
// The entry's title, with the Reset link after its line when it is changed (ui::title).
int title(draw::Canvas& c, const kitpaint::FormLayout& l, int y, const std::wstring& text, const std::wstring& sub, bool changed, std::function<void()> reset,
          UiIcon kind) {
    const kitpaint::TitleLayout t = kitpaint::drawTitle(c, l.x0, y, l.colRight + l.picW, {}, true, kind, text, sub, changed);
    if (t.hasLine && changed && reset)
        link(c, t.line.left + c.measure(t.lineText), t.line.top, t.line.bottom - t.line.top, L"Reset", std::move(reset), L"Put back the game's values");
    return t.next;
}
void fieldLabel(draw::Canvas& c, const kitpaint::FormLayout& l, int y, const std::wstring& name, bool changed) {
    kitpaint::drawFieldLabel(c, {l.x0, y, l.x0 + l.labelW, y + l.rowH}, name, changed);
}
// A list row's ground: selected (with the accent bar), under the mouse, or none (ui::listRow).
void listRow(draw::Canvas& c, draw::Rect r, bool selected, bool over) {
    if (selected) {
        c.fill(r, kSel);
        c.fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
    } else if (over) {
        c.fill(r, kRowHot);
    }
}

}  // namespace sdlx

kitpaint::FormLayout sdlFormLayout() { return kitpaint::formLayout(gSdlDataForm.width()); }

// --- The Requirements section: the core's paint on the kit faces -------------------------------------------------------

// The program's side of paintRequirementList (core/views_data_requirements_paint.inc), as data_extras.inc's
// WinRequirements: the kit faces, the list picker and the form's click areas. It lives as long as the click areas.
struct SdlRequirements final : RequirementsPaintHost {
    draw::Canvas* c = nullptr;
    draw::Canvas& canvas() override { return *c; }
    Requirements& reqs() override { return dataExtras().reqs; }
    void edited() override { sdlExtrasChanged(); }
    bool hot(draw::Rect r) override { return sdlFormHot(r); }
    void hit(draw::Rect r, std::function<void()> click, const std::wstring& tip, bool hand) override { sdlFormArea(r, std::move(click), tip, hand); }
    bool choose(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, draw::Rect, std::string& out) override {
        return sdlChoose(prompt, items, current, out);
    }
    std::vector<ListPickItem> valueItems(const char* kind) override { return entryItems(kind ? kind : ""); }
    void pickBox(draw::Rect r, const std::wstring& text, bool changed, std::function<void(draw::Rect)> click) override {
        sdlx::pickBox(*c, r, text, changed, std::move(click));
    }
    void valueBox(draw::Rect r, const char* kind, int v, std::function<void(draw::Rect)> click) override {
        sdlx::pickBox(*c, r, argName(kind, v), false, std::move(click), L"", v);
    }
    // The remove cross: on the row under the mouse (the row is a click area without a click, as on Windows).
    void removeRow(draw::Rect row, draw::Rect at, std::function<void()> click, const std::wstring& tip) override {
        sdlFormArea(row, nullptr, L"", false);
        if (!sdlFormHot(row)) return;
        sdlx::iconButton(*c, at, UiIcon::Close, std::move(click), tip, kitpaint::Tone::Danger);
    }
    int linkButton(int x, int y, int h, const std::wstring& text, std::function<void()> click) override {
        return sdlx::smallButton(*c, x, y, h, text, std::move(click));
    }
    void fillAlpha(draw::Rect r, draw::Color col, uint8_t alpha) override { c->fill(r, col, alpha); }
};
SdlRequirements gSdlRequirements;

// The requirement lists of the open entry, for the .dat forms that have them.
int sdlPaintRequirements(draw::Canvas& c, const std::string& dat, int entry, int y) {
    const kitpaint::FormLayout l = sdlFormLayout();
    gSdlRequirements.c = &c;
    for (int k = 0; k < Requirements::kKinds; ++k)
        if (Requirements::kind(k).dat == dat && entry < Requirements::kind(k).entries)
            y = paintRequirementList(gSdlRequirements, RequirementLayout{l.x0, l.labelW, l.rowH, l.colRight}, k, entry, y);
    return y;
}

// --- The Strings tab ---------------------------------------------------------------------------------------------------

// The editor: the core's game text view (core/views_game_text.inc), a child of the form (sdl_data.inc adds it).
GameTextSpec sdlStringsTextSpec() {
    GameTextSpec s;
    s.palette = TextPalette::Game, s.multiline = true, s.align = true;
    return s;
}
GameTextView gSdlStringsText(sdlStringsTextSpec());

// The editor's text goes in (once it changed): the string keeps its button prefix.
void sdlStringsCommit() {
    DataExtrasModel& x = dataExtras();
    if (x.textFor < 1 || !x.textDirty || x.textFor > x.strings.count()) return;
    x.textDirty = false;
    const int id = x.textFor;
    const std::string text = stringEditCommit(x.strings.text(id - 1), gSdlStringsText.bytes());
    if (text == x.strings.text(id - 1)) return;
    x.strings.set(id - 1, text);
    sdlExtrasChanged();
}

// The Strings tab's form (the Windows program's paintStringsForm), from y down.
void sdlPaintStringsForm(draw::Canvas& c, int& y) {
    DataExtrasModel& x = dataExtras();
    const kitpaint::FormLayout l = sdlFormLayout();
    const int id = std::clamp(dataState().entry, 1, x.strings.count());
    const std::string raw = x.strings.text(id - 1);
    y = sdlx::title(c, l, y, stringLine(raw), L"stat_txt.tbl string " + std::to_wstring(id), x.strings.changed(id - 1), [id] {
        dataExtras().strings.reset(id - 1);
        dataExtras().textFor = -1;
        sdlExtrasChanged();
    }, UiIcon::DatStrings);
    y = kitpaint::drawSectionHeader(c, l.x0, l.colRight, y, l.rowH, L"Text", x.strings.changed(id - 1));
    // The preview: a picture, in the right column (above the fields when narrow).
    std::string previewRaw = raw;
    if (x.textDirty && x.textFor == id) previewRaw = stringEditCommit(raw, gSdlStringsText.bytes());
    int picBottom;
    {
        const int lines = 1 + int(std::count(previewRaw.begin(), previewRaw.end(), '\n'));
        const draw::Rect pb{l.picX, y, l.picX + std::min(l.picW, scale(420)), y + std::max(scale(60), lines * (c.metrics().height + scale(2)) + scale(16))};
        c.fill(pb, draw::rgb(8, 8, 10));
        c.roundBox(pb, draw::kNone, draw::rgb(60, 64, 70), float(scale(4)));
        drawGameText(c, {pb.left + scale(10), pb.top + scale(8), pb.right - scale(10), pb.bottom}, previewRaw, draw::rgb(208, 208, 208));
        {  // which box is which: this one is the game's look (in the small font)
            const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}));
            c.text({pb.left + scale(10), pb.bottom - scale(18), pb.right - scale(8), pb.bottom - scale(3)}, L"Game preview", draw::rgb(120, 124, 130),
                   draw::kMiddle | draw::kRight);
            c.setFont(of);
        }
        sdlFormArea(pb, nullptr, L"Game preview: the text as the game shows it", false);
        picBottom = pb.bottom;
    }
    if (!l.twoCol) y = picBottom + scale(12);
    // A button's text (the one shown when it can be used): its hotkey and type as fields of their own.
    if (stringIsButtonText(x.buttons, id, raw)) {
        const StringButtonParts bp = stringButtonParts(raw);
        const std::string& stockRaw = x.strings.stock(id - 1);
        const bool hotChanged = buttonPrefix(stockRaw) != bp.has || (bp.has && stockRaw[0] != bp.hot),
                   typeChanged = buttonPrefix(stockRaw) != bp.has || (bp.has && stockRaw[1] != raw[1]);
        auto setPrefix = [id](char h, int type) {
            DataExtrasModel& e = dataExtras();
            e.strings.set(id - 1, stringWithButtonPrefix(e.strings.text(id - 1), h, type));
            e.textFor = -1;
            sdlExtrasChanged();
        };
        sdlx::fieldLabel(c, l, y, L"Hotkey", hotChanged);
        sdlx::pickBox(c, {l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)}, stringHotkeyText(bp), hotChanged,
                      [setPrefix, bp](draw::Rect) {
                          std::vector<ListPickItem> items;
                          for (char ch = 'A'; ch <= 'Z'; ++ch) items.push_back({std::wstring(1, wchar_t(ch)), L"", std::string(1, char(std::tolower(ch)))});
                          for (char ch = '0'; ch <= '9'; ++ch) items.push_back({std::wstring(1, wchar_t(ch)), L"", std::string(1, ch)});
                          items.push_back({L"Esc", L"", std::string(1, char(27))});
                          std::string picked;
                          if (sdlChoose(L"Hotkey", items, "", picked) && !picked.empty()) setPrefix(picked[0], bp.type);
                      },
                      L"The key that presses the button");
        y += l.rowH;
        sdlx::fieldLabel(c, l, y, L"Type", typeChanged);
        sdlx::pickBox(c, {l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)}, stringButtonTypeName(bp.has ? bp.type : -1),
                      typeChanged,
                      [setPrefix, bp](draw::Rect) {
                          std::vector<ListPickItem> items;
                          for (int t = 0; t < 6; ++t) items.push_back({stringButtonTypeName(t), L"", std::to_string(t)});
                          std::string picked;
                          if (sdlChoose(L"Type", items, "", picked)) setPrefix(bp.has ? bp.hot : 'a', std::stoi(picked));
                      },
                      L"The kind of button. For a unit, an upgrade or a technology, the game's tooltip shows the cost.", bp.has ? bp.type : -1);
        y += l.rowH + scale(8);
    }
    // The text: the editor over the field column (Preview: the text in the game's colors; Raw: the codes as <XX>).
    const draw::Rect er{l.x0, y, l.colRight, y + gameTextHeight(c, sdlStringsTextSpec(), l.colRight - l.x0, scale(120))};
    if (x.textFor != id) {
        x.textFor = id;
        gSdlStringsText.setBytes(stringEditBody(raw));
        x.textDirty = false;
    }
    const draw::Rect now = gSdlStringsText.frame();
    if (now.left != er.left || now.top != er.top || now.right != er.right || now.bottom != er.bottom) gSdlStringsText.setFrame(er);
    gSdlStringsText.setColors(draw::rgb(184, 184, 232), draw::rgb(0x11, 0x11, 0x11));  // (main.cpp's kRichDefault and kRichBack, the dark theme's)
    gSdlStringsText.setChanged(x.strings.changed(id - 1));
    gSdlStringsText.setVisible(true);
    y = er.bottom + scale(10);
    // Room: the game copies the strings over its own.
    gSdlRequirements.c = &c;
    y = requirementCapacityBar(gSdlRequirements, RequirementLayout{l.x0, l.labelW, l.rowH, l.colRight}, y, x.strings.build().size(), x.strings.capacity(),
                               L"Size of all strings together. The game has room for at most the number after the slash.");
    if (l.twoCol) y = std::max(y, picBottom);
    y += scale(14);
}

// --- The Buttons tab ---------------------------------------------------------------------------------------------------

// The set takes `bs`, and `sel` is the selected button.
void sdlSetButtons(int set, std::vector<Button> bs, int sel) {
    dataExtras().buttons.set(set, std::move(bs));
    dataExtras().button = sel;
    sdlExtrasChanged();
}

// The command card: 3 x 3 cells, each with its button (its number: the icons are not on this system yet). Click selects
// (again: the next button in that place); an empty cell adds a button there. Returns its height.
int sdlPaintCommandCard(draw::Canvas& c, int set, int x, int y, int cell) {
    DataExtrasModel& xs = dataExtras();
    const auto& bs = xs.buttons.set(set);
    const int gap = scale(4);
    const draw::Rect card{x, y, x + 3 * cell + 4 * gap, y + 3 * cell + 4 * gap};
    c.fill(card, draw::rgb(14, 14, 16));
    c.roundBox(card, draw::kNone, kBorder, float(scale(6)));
    for (int p = 1; p <= 9; ++p) {
        draw::Rect cr{x + gap + (p - 1) % 3 * (cell + gap), y + gap + (p - 1) / 3 * (cell + gap), 0, 0};
        cr.right = cr.left + cell, cr.bottom = cr.top + cell;
        const std::vector<int> here = buttonsAtPlace(bs, p);
        const bool sel = std::find(here.begin(), here.end(), xs.button) != here.end();
        const bool hot = sdlFormHot(cr);
        c.fill(cr, sel ? kSel : hot ? draw::rgb(36, 39, 44) : draw::rgb(26, 28, 32));
        c.roundBox(cr, draw::kNone, sel ? kAccent : draw::rgb(48, 52, 58), float(scale(4)), sel ? 2 : 1);
        if (!here.empty()) {
            const int shown = sel ? xs.button : here[0];
            c.text(cr, std::to_wstring(shown), sel ? kAccent : kText, draw::kMiddle | draw::kCenter);
            if (here.size() > 1) {  // more than one button here: the game shows the first whose condition holds; a dot each, in order
                const int d = scale(5), dg = scale(3), w = int(here.size()) * (d + dg) - dg;
                for (size_t j = 0; j < here.size(); ++j) {
                    const int dx = (cr.left + cr.right - w) / 2 + int(j) * (d + dg), dy = cr.bottom - scale(9);
                    c.fill({dx, dy, dx + d, dy + d}, here[j] == shown ? kAccent : draw::rgb(110, 114, 120));
                }
            }
        } else if (hot) {
            c.text(cr, L"+", kDim, draw::kMiddle | draw::kCenter);
        }
        std::wstring tip;
        if (!here.empty()) {
            tip = statString(bs[size_t(here[0])].enabledString);
            if (here.size() > 1) tip += L"\n" + std::to_wstring(here.size()) + L" buttons in this place. The game shows the first whose condition holds. Click again for the next one.";
        } else {
            tip = L"Add a button here";
        }
        sdlFormArea(cr,
                    [set, here, p] {
                        if (here.empty()) {  // a new button
                            auto bs2 = dataExtras().buttons.set(set);
                            bs2.push_back(buttonNew(uint16_t(p)));
                            const int sel = int(bs2.size()) - 1;
                            return sdlSetButtons(set, std::move(bs2), sel);
                        }
                        dataExtras().button = buttonNextInPlace(here, dataExtras().button);
                        gSdlDataForm.invalidate();
                    },
                    tip, true);
    }
    return card.bottom - y;
}

// The Buttons tab's form (the Windows program's paintButtonsForm), from y down.
void sdlPaintButtonsForm(draw::Canvas& c, int& y) {
    DataExtrasModel& x = dataExtras();
    const kitpaint::FormLayout l = sdlFormLayout();
    const int set = std::clamp(dataState().entry, 0, ButtonSets::kSets - 1);
    const std::vector<Button> bs = x.buttons.set(set);
    if (x.button >= int(bs.size())) x.button = bs.empty() ? 0 : int(bs.size()) - 1;
    y = sdlx::title(c, l, y, buttonSetName(set), L"Button set " + std::to_wstring(set) + L"  \x00B7  " + std::to_wstring(bs.size()) + L" buttons",
                    x.buttons.changed(set), [set] {
                        dataExtras().buttons.reset(set);
                        sdlExtrasChanged();
                    },
                    UiIcon::DatButtons);
    // The card and the tooltip: pictures, in the right column (above the fields when the form is narrow).
    const int cell = scale(64), cardTop = y;
    int picBottom = y + sdlPaintCommandCard(c, set, l.picX, y, cell);
    // Narrow: the fields under the card, and the tooltip at the card's right (the Windows form, wider, has two columns).
    if (!l.twoCol) y = picBottom + scale(16);
    // The buttons, in the order the game checks them: in one place, the first whose condition holds shows.
    y = kitpaint::drawSectionHeader(c, l.x0, l.colRight, y, l.rowH, L"Buttons", x.buttons.changed(set));
    for (size_t i = 0; i < bs.size(); ++i) {
        const Button& b = bs[i];
        const draw::Rect r{l.x0 - scale(6), y, l.colRight, y + l.rowH};
        const bool on = int(i) == x.button, hot = sdlFormHot(r);
        sdlx::listRow(c, r, on, hot);
        int sharing = 0;  // others in the same place
        for (const Button& o : bs) sharing += o.pos == b.pos;
        const int gx = l.x0 + scale(14);  // after the grip
        c.text({gx, y, gx + scale(26), y + l.rowH}, std::to_wstring(i), kDim, draw::kMiddle);
        c.text({gx + scale(54), y, gx + scale(120), y + l.rowH}, buttonCardCell(b.pos), sharing > 1 ? kWarn : kDim, draw::kMiddle);
        c.text({gx + scale(120), y, l.colRight - scale(80), y + l.rowH}, buttonRowTextFrom(b, b.enabledString ? statString(b.enabledString) : L""),
               on ? kAccent : kText, draw::kMiddle | draw::kEllipsis);
        const int bi = int(i);
        sdlFormArea(r,
                    [bi] {
                        dataExtras().button = bi;
                        gSdlDataForm.invalidate();
                    },
                    sharing > 1 ? L"Shares its place with another button: the game shows the first whose condition holds (the higher one in this list)" : L"",
                    true);
        // On the row under the mouse: earlier, later, remove (ui::rowActions).
        if (hot) {
            auto move = [set, bi](int by) {
                return [set, bi, by] {
                    auto bs2 = dataExtras().buttons.set(set);
                    const int sel = buttonSwapWith(bs2, bi, by);
                    sdlSetButtons(set, std::move(bs2), sel);
                };
            };
            const int w = scale(stardraft::kit::kIconBox), h = std::min(r.height(), scale(stardraft::kit::kIconBox));
            int ax = r.right - scale(4) - w * 3;
            const int ay = (r.top + r.bottom - h) / 2;
            sdlx::iconButton(c, {ax, ay, ax + w, ay + h}, UiIcon::MoveUp, move(-1), L"Earlier: checked before the one above", kitpaint::Tone::Normal, i > 0);
            ax += w;
            sdlx::iconButton(c, {ax, ay, ax + w, ay + h}, UiIcon::MoveDown, move(1), L"Later", kitpaint::Tone::Normal, i + 1 < bs.size());
            ax += w;
            sdlx::iconButton(c, {ax, ay, ax + w, ay + h}, UiIcon::Close,
                             [set, bi] {
                                 auto bs2 = dataExtras().buttons.set(set);
                                 const int sel = buttonRemoveAt(bs2, bi);
                                 sdlSetButtons(set, std::move(bs2), sel);
                             },
                             L"Remove the button", kitpaint::Tone::Danger);
        }
        y += l.rowH;
    }
    sdlx::smallButton(c, l.x0, y, l.rowH, L"Add a button", [set] {
        auto bs2 = dataExtras().buttons.set(set);
        bs2.push_back(buttonNew(buttonFreePlace(bs2)));
        const int sel = int(bs2.size()) - 1;
        sdlSetButtons(set, std::move(bs2), sel);
    });
    y += l.rowH + scale(8);
    y = kitpaint::drawSectionHeader(c, l.x0, l.colRight, y, l.rowH, bs.empty() ? L"Button" : L"Button " + std::to_wstring(x.button), x.buttons.changed(set));
    if (!bs.empty()) {  // the tooltip as the game shows it, under the card
        const Button& b = bs[size_t(x.button)];
        if (b.enabledString && b.enabledString <= x.strings.count()) {
            const std::string& raw = x.strings.text(b.enabledString - 1);
            const std::wstring cost = buttonCostText(b, buttonPrefix(raw) ? raw[1] : 0,
                                                     [](const std::string& dat, const std::string& name, int e) { return datValue(dat, name, e, 0); });
            if (l.twoCol) picBottom += scale(12) + paintButtonTooltip(c, {l.picX, picBottom + scale(12), l.picX + 3 * cell + scale(16), 0}, raw, cost);
            else {
                const int tx = l.picX + 3 * cell + scale(16) + scale(16);
                paintButtonTooltip(c, {tx, cardTop, std::min(l.colRight, tx + 3 * cell + scale(16)), 0}, raw, cost);
            }
        }
    }
    if (bs.empty()) {
        sdlx::fieldLabel(c, l, y, L"No buttons", false);
        y += l.rowH;
    } else {
        const int bi = x.button;
        const Button b = bs[size_t(bi)];
        const auto& stock = x.buttons.stock(set);
        const Button* sb = size_t(bi) < stock.size() ? &stock[size_t(bi)] : nullptr;
        auto edit = [set, bi](std::function<void(Button&)> f) {
            auto bs2 = dataExtras().buttons.set(set);
            if (size_t(bi) >= bs2.size()) return;
            f(bs2[size_t(bi)]);
            sdlSetButtons(set, std::move(bs2), bi);
        };
        auto row = [&](const wchar_t* name, bool changed, const std::wstring& text, std::function<void(draw::Rect)> click, int number, const std::wstring& tip = L"") {
            sdlx::fieldLabel(c, l, y, name, changed);
            sdlx::pickBox(c, {l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)}, text, changed, std::move(click), tip, number);
            y += l.rowH;
        };
        row(L"Position", sb && sb->pos != b.pos, buttonRowColumn(b.pos),
            [edit, b](draw::Rect) {
                std::vector<ListPickItem> items;
                for (int p = 1; p <= 9; ++p) items.push_back({buttonRowColumn(p), L"", std::to_string(p)});
                std::string picked;
                if (sdlChoose(L"Position", items, std::to_string(b.pos), picked)) edit([&](Button& o) { o.pos = uint16_t(std::stoul(picked)); });
            },
            b.pos);
        static const std::string kIcons = "icons";
        const auto& iconNames = DatSchema::entryNames(kIcons);
        row(L"Icon", sb && sb->icon != b.icon, b.icon < iconNames.size() ? widen(iconNames[b.icon]) : L"Icon",
            [edit, b](draw::Rect) {
                std::string picked;
                if (sdlChoose(L"Icon", entryItems("icons"), std::to_string(b.icon), picked)) edit([&](Button& o) { o.icon = uint16_t(std::stoul(picked)); });
            },
            b.icon);
        // Condition and action: their functions, and a value when the function takes one.
        for (int which = 0; which < 2; ++which) {
            const bool cond = which == 0;
            const uint32_t fn = cond ? b.condition : b.action;
            const ButtonFunctionDef* d = cond ? ButtonSets::condition(fn) : ButtonSets::action(fn);
            wchar_t hex[24];
            std::swprintf(hex, 24, L"Function 0x%06X", unsigned(fn));
            row(cond ? L"Shown when" : L"Action", sb && (cond ? sb->condition != fn : sb->action != fn), d ? widen(d->name) : std::wstring(hex),
                [edit, cond, fn](draw::Rect) {
                    std::vector<ListPickItem> items;
                    for (const auto& f : cond ? ButtonSets::conditions() : ButtonSets::actions()) items.push_back({widen(f.name), L"", std::to_string(f.address)});
                    std::string picked;
                    if (sdlChoose(cond ? L"Shown when" : L"Action", items, std::to_string(fn), picked))
                        edit([&](Button& o) { (cond ? o.condition : o.action) = uint32_t(std::stoul(picked)); });
                },
                -1, cond ? L"The game's condition for showing the button" : L"Select the action that the button performs");
            if (d && d->arg) {
                const uint16_t v = cond ? b.conditionArg : b.actionArg;
                const char* kind = d->arg;
                row(cond ? L"Shown when: value" : L"Action: value", sb && (cond ? sb->conditionArg != v : sb->actionArg != v), argName(kind, v),
                    [edit, cond, v, kind](draw::Rect) {
                        std::string picked;
                        if (sdlChoose(L"Value", entryItems(kind), std::to_string(v), picked))
                            edit([&](Button& o) { (cond ? o.conditionArg : o.actionArg) = uint16_t(std::stoul(picked)); });
                    },
                    v);
            }
        }
        // Texts: the tooltip's first line in the game's colors (a click opens the string in the Strings tab; the arrow
        // picks another string). Under each: a link to the string, and its raw value when "Display raw value" is on.
        sdlx::check(c, {l.x0 + l.labelW, y, l.x0 + l.labelW + scale(300), y + l.rowH}, L"Display raw value", x.rawText,
                    [] {
                        dataExtras().rawText = !dataExtras().rawText;
                        dataExtras().textFor = -1;
                        gSdlDataForm.invalidate();
                    },
                    L"Show each tooltip's string as the game stores it: the hotkey and type characters, and codes as <01>");
        y += l.rowH;
        for (int which = 0; which < 2; ++which) {
            const uint16_t s = which ? b.disabledString : b.enabledString;
            const bool hasString = s >= 1 && s <= x.strings.count();
            const bool strChanged = hasString && x.strings.changed(s - 1);
            sdlx::fieldLabel(c, l, y, which ? L"Tooltip when grayed" : L"Tooltip", (sb && (which ? sb->disabledString != s : sb->enabledString != s)) || strChanged);
            const draw::Rect box{l.x0 + l.labelW, y + scale(2), l.colRight - scale(30), y + l.rowH - scale(2)};
            const bool hot = sdlFormHot(box);
            c.fill(box, hot ? draw::rgb(36, 39, 44) : kCtl);  // (the game's text colors read on a dark ground only)
            c.roundBox(box, draw::kNone, strChanged ? kGoodLine : kBorder, float(scale(6)));
            const draw::Rect tb{box.left + scale(8), box.top, box.right - scale(8), box.bottom};
            if (hasString) {
                const std::string& raw = x.strings.text(s - 1);
                const size_t nl = raw.find('\n');
                c.pushClip(tb);
                drawGameText(c, {tb.left, tb.top + (tb.height() - scale(18)) / 2, tb.right, tb.bottom}, nl == std::string::npos ? raw : raw.substr(0, nl),
                             draw::rgb(184, 184, 232));
                c.popClip();
                const int sid = s;
                sdlFormArea(box, [sid] { sdlDataGoTo("strings", sid); }, L"Open the text in the Strings tab (string " + std::to_wstring(s) + L")", true);
            } else {
                c.text(tb, L"None", kDim, draw::kMiddle);
            }
            const draw::Rect pick{box.right + scale(6), y + scale(2), l.colRight, y + l.rowH - scale(2)};
            kitpaint::drawGlyph(c, pick, UiIcon::Dropdown, sdlFormHot(pick) ? kAccent : kDim);  // a list: another string
            sdlFormArea(pick,
                        [edit, which, s] {
                            std::string picked;
                            if (sdlChoose(L"Text", entryItems("strings", true), std::to_string(s), picked))
                                edit([&](Button& o) { (which ? o.disabledString : o.enabledString) = uint16_t(std::stoul(picked)); });
                        },
                        L"Another string", true);
            y += l.rowH;
            if (hasString) {  // the string: go to it; its raw value
                const int sid = s;
                const int lx = sdlx::link(c, l.x0 + l.labelW, y, scale(22), L"String " + std::to_wstring(s), [sid] { sdlDataGoTo("strings", sid); },
                                          L"Open this string in the Strings tab");
                if (x.rawText) {
                    const draw::Font of = c.setFont(c.font({draw::Face::Mono}));
                    c.text({lx + scale(12), y, l.colRight, y + scale(22)}, widen(StatTxt::escape(x.strings.text(s - 1))), kDim, draw::kMiddle | draw::kEllipsis);
                    c.setFont(of);
                }
                y += scale(24);
            }
        }
    }
    if (l.twoCol) y = std::max(y, picBottom);
    y += scale(14);
}

// The Strings or Buttons tab (true: it is one of them, painted from y down). The editor shows only on the Strings tab.
bool sdlPaintExtraTab(draw::Canvas& c, int& y) {
    const bool strings = dataState().datTab == stringsTab() && dataState().project && dataExtras().strings.count() > 0;
    if (!strings) {
        sdlStringsCommit();
        sdlDataHide(gSdlStringsText);
    }
    if (!extraTab(dataState().datTab) || !dataState().project) return false;
    if (strings) sdlPaintStringsForm(c, y);
    else if (dataState().datTab == buttonsTab()) sdlPaintButtonsForm(c, y);
    return true;
}
