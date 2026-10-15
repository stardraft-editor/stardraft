// The Data workspace's tables that are not .dat files (included into main.cpp, before paintDataForm):
//  - a Strings tab: stat_txt.tbl, each string's text in an edit box with a colored preview; for button texts, the
//    hotkey and the button type as their own controls;
//  - a Buttons tab: each button set as the command card it makes (3 x 3), the selected button's fields beside it;
//  - a Requirements section on the Units, Upgrades, Tech Data and Orders forms;
//  - a Display section on the Units form: the button set, the status panel, the wireframes.
// Edits are saved as dat/strings.json, dat/buttons.json, dat/requirements.json and dat/unit_display.json; Build writes
// their code.

bool showListPicker(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, POINT at, std::string& out);

HWND hitsWnd() { return gHitsWnd ? gHitsWnd : gData.form; }

void (*gButtonPopHook)(HWND) = nullptr;  // self-test
struct DataExtras : DataExtrasModel {
    HWND textEdit = nullptr;
} gExtras;
DataExtrasModel& dataExtras() { return gExtras; }
// (Build's writeExtrasEdits is core/project_build.inc's.)

// --- What the tabs list ----------------------------------------------------------------------------------------

const UnitPalette::Bitmap* tabEntryIcon(int tab, int e) {
    if (tab == stringsTab()) return nullptr;
    return dataPictureBitmap(dataTabEntryPicture(tab, e));  // (core/data_nav.inc)
}

// --- Game text, drawn as the game colors it ------------------------------------------------------------------

// (The game's text colors and the Canvas paint, drawGameText: core/game_text_model.inc.)
// Draws `raw` (UTF-8 with the game's control bytes) in `r`, line by line; returns the height used. `briefing`: the
// colors of mission briefings.
int drawGameText(HDC dc, RECT r, const std::string& raw, COLORREF normal, int lineHeight = 0, bool briefing = false) {
    ui::Paint p(dc);
    return drawGameText(*p, draw::Rect(r), raw, normal, lineHeight, briefing);
}

// --- Common parts ----------------------------------------------------------------------------------------------

void extrasChanged() {
    dataSaved();
    InvalidateRect(gData.form, nullptr, FALSE);
    InvalidateRect(gData.list, nullptr, FALSE);
}

// (Copy a unit's display and button set with its values, extrasCopyUnit and extrasPasteUnit: core/data_nav.inc.)

// The title block: icon, name, and a line under it ("stat_txt.tbl string 12  ·  changed  ·  Reset").
int formTitle(HDC dc, const FormLayout& l, int y, const UnitPalette::Bitmap* icon, const std::wstring& title, const std::wstring& sub, bool changed,
              std::function<void()> reset, std::optional<UiIcon> kind = std::nullopt) {
    return ui::title(dc, l.x0, y, l.colRight + l.picW, icon, title, sub, changed, std::move(reset), kind);
}
// (The pickers' entries and a value's name, entryItems and argName: core/data_model.inc.)
const UnitPalette::Bitmap* argIcon(const char* kind, int v) { return dataPictureBitmap(dataArgPicture(kind, v)); }  // (core/data_nav.inc)
// Opens entry `v` of `kind` (a .dat, "buttons" or "strings").
std::function<void()> openEntry(const char* kind, int v) {
    if (!kind) return nullptr;
    const std::string k = kind;
    return [k, v] { dataGoTo(k, v); };
}
// A thin bar, how much of the game's room is used, with the numbers beside it.
int capacityBar(HDC dc, const FormLayout& l, int y, size_t used, size_t room, const std::wstring& what) {
    const bool over = used > room;
    wchar_t num[64];
    std::swprintf(num, 64, L"%zu / %zu bytes", used, room);
    SIZE sz{};
    GetTextExtentPoint32W(dc, num, int(wcslen(num)), &sz);
    RECT r{l.x0, y + l.rowH / 2 - scale(2), l.colRight - sz.cx - scale(12), y + l.rowH / 2 + scale(2)};
    fillAlpha(dc, r, kBorder, 255);
    RECT f = r;
    f.right = r.left + int((r.right - r.left) * std::min<double>(1.0, double(used) / double(std::max<size_t>(1, room))));
    fillAlpha(dc, f, over ? kBad : kAccent, 255);
    RECT t{r.right + scale(12), y, l.colRight, y + l.rowH};
    SetTextColor(dc, over ? kBad : kDim);
    DrawTextW(dc, num, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    hitsOut().push_back({{l.x0, y, l.colRight, y + l.rowH}, nullptr, nullptr, (what.ends_with(L".") ? what : what + L".") + (over ? tr(L" The total is more than the game has room for. Build stops.") : L""), IDC_ARROW, {}});
    return y + l.rowH;
}

// --- Requirements section (Units, Upgrades, Tech Data, Orders) ------------------------------------------------
// The section's paint and clicks are core/views_data_requirements_paint.inc (paintRequirementList); this is the
// Windows program's side of it: its kit faces, the list pickers and the click areas.

struct WinRequirements final : RequirementsPaintHost {
    HDC dc = nullptr;
    std::unique_ptr<ui::Paint> paint;  // the surface being painted (its canvas over dc)
    draw::Canvas& canvas() override { return **paint; }
    Requirements& reqs() override { return gExtras.reqs; }
    void edited() override { extrasChanged(); }
    bool hot(draw::Rect r) override { return ui::hot(RECT{r.left, r.top, r.right, r.bottom}); }
    void hit(draw::Rect r, std::function<void()> click, const std::wstring& tip, bool hand) override {
        hitsOut().push_back({RECT{r.left, r.top, r.right, r.bottom}, nullptr, std::move(click), tip, hand ? IDC_HAND : IDC_ARROW, {}});
    }
    bool choose(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, draw::Rect box, std::string& out) override {
        return showListPicker(prompt, std::move(items), current, belowBox(RECT{box.left, box.top, box.right, box.bottom}), out);
    }
    std::vector<ListPickItem> valueItems(const char* kind) override { return entryItems(kind); }
    void pickBox(draw::Rect r, const std::wstring& text, bool changed, std::function<void(draw::Rect)> click) override {
        ui::pickBox(dc, RECT{r.left, r.top, r.right, r.bottom}, text, changed, [click](RECT b) { click(draw::Rect(b)); });
    }
    void valueBox(draw::Rect r, const char* kind, int v, std::function<void(draw::Rect)> click) override {
        ui::pickBox(dc, RECT{r.left, r.top, r.right, r.bottom}, argName(kind, v), false, [click](RECT b) { click(draw::Rect(b)); }, L"",
                    argIcon(kind, v), v, openEntry(kind, v));
    }
    void removeRow(draw::Rect row, draw::Rect at, std::function<void()> click, const std::wstring& tip) override {
        ::removeCross(dc, RECT{row.left, row.top, row.right, row.bottom}, RECT{at.left, at.top, at.right, at.bottom}, std::move(click), tip);
    }
    int linkButton(int x, int y, int h, const std::wstring& text, std::function<void()> click) override {
        return ::linkButton(dc, x, y, h, text, std::move(click));
    }
    void fillAlpha(draw::Rect r, draw::Color c, uint8_t alpha) override { ::fillAlpha(dc, RECT{r.left, r.top, r.right, r.bottom}, c, alpha); }
};
WinRequirements gRequirementsPaint;  // lives as long as the click areas it makes

// One list of the open entry (the core's paint; the surface is dc).
int paintRequirementList(HDC dc, const FormLayout& l, int k, int entry, int y) {
    gRequirementsPaint.dc = dc;
    gRequirementsPaint.paint = std::make_unique<ui::Paint>(dc);
    y = paintRequirementList(gRequirementsPaint, RequirementLayout{l.x0, l.labelW, l.rowH, l.colRight}, k, entry, y);
    gRequirementsPaint.paint.reset();
    return y;
}

int gWireframeTop = -1;  // where the last painted Display section's wireframe rows start (form content; the self-test)

// The Display section (core/views_data_display.inc: paintUnitDisplay): the Windows program's side of it, its kit
// faces, the list pickers, the names, the wireframe sets and the sprite blit.
struct WinUnitDisplay final : UnitDisplayHost {
    HDC dc = nullptr;
    std::unique_ptr<ui::Paint> paint;  // the surface being painted (its canvas over dc)
    draw::Canvas& canvas() override { return **paint; }
    UnitLooks& looks() override { return gExtras.looks; }
    ButtonSets& buttons() override { return gExtras.buttons; }
    void edited() override { extrasChanged(); }
    void tip(draw::Rect r, const std::wstring& tip) override {
        gWidgetHits.push_back({RECT{r.left, r.top, r.right, r.bottom}, nullptr, nullptr, tip, IDC_ARROW, {}});
    }
    bool choose(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, draw::Rect box, std::string& out) override {
        return showListPicker(prompt, std::move(items), current, belowBox(RECT{box.left, box.top, box.right, box.bottom}), out);
    }
    std::vector<ListPickItem> entryItems(const char* kind) override { return ::entryItems(kind); }
    std::wstring buttonSetName(int set) override { return ::buttonSetName(set); }
    std::wstring unitName(int unit) override { return entryName("units", unit); }
    bool hasShields(int unit) override { return datValue("units", "Shield Enable", unit) != 0; }
    void pickBox(draw::Rect r, const std::wstring& text, bool changed, std::function<void(draw::Rect)> click, const std::wstring& tip,
                 const char* iconDat, int iconEntry, int number, const char* open) override {
        ui::pickBox(dc, RECT{r.left, r.top, r.right, r.bottom}, text, changed, [click](RECT b) { click(draw::Rect(b)); }, tip,
                    iconDat ? datRefIcon(iconDat, uint32_t(iconEntry)) : nullptr, number, openEntry(open, number));
    }
    Wireframes* wireframes(WireKind kind) override { return wireframeSet(kind); }
    void sprite(const Sprite& s, int cx, int cy, float zoom) override { blitSprite(dc, s, cx, cy, zoom); }
};
WinUnitDisplay gUnitDisplayPaint;  // lives as long as the click areas it makes

// A unit's Display section (the core's paint; the surface is dc).
int paintUnitDisplay(HDC dc, int u, int y) {
    RECT rc;
    GetClientRect(gData.form, &rc);
    const FormLayout l = formLayout(rc);
    gUnitDisplayPaint.dc = dc;
    gUnitDisplayPaint.paint = std::make_unique<ui::Paint>(dc);
    int wireTop = -1;
    y = paintUnitDisplay(gUnitDisplayPaint, RequirementLayout{l.x0, l.labelW, l.rowH, l.colRight}, u, y, &wireTop);
    gUnitDisplayPaint.paint.reset();
    if (wireTop >= 0) gWireframeTop = wireTop + gData.formScroll;
    return y;
}

// The requirement lists of the open entry, for the .dat forms that have them.
int paintRequirements(HDC dc, const std::string& dat, int entry, int, int y, int) {
    RECT rc;
    GetClientRect(gData.form, &rc);
    const FormLayout l = formLayout(rc);
    for (int k = 0; k < Requirements::kKinds; ++k)
        if (Requirements::kind(k).dat == dat && entry < Requirements::kind(k).entries) y = paintRequirementList(dc, l, k, entry, y);
    return y;
}

// --- Buttons tab -----------------------------------------------------------------------------------------------

// A button's label (core views_data_buttons.inc): its tooltip's first line, or what it does.
std::wstring buttonRowText(const Button& b) { return buttonRowTextFrom(b, b.enabledString ? statString(b.enabledString) : L""); }

// The game's text with its colors, in the rich text pop-out (main.cpp); false when it was not changed.
bool editGameText(const std::wstring& title, std::string& text, POINT at);

// What a button costs (core views_data_buttons.inc: the cost line of its type's tooltip).
std::wstring buttonCost(const Button& b, int type) {
    return buttonCostText(b, type, [](const std::string& dat, const std::string& name, int e) { return datValue(dat, name, e); });
}

// The tooltip as the game shows it: the text, and the cost line its type adds (core/views_data_button_pop.inc).
int paintButtonTooltip(HDC dc, RECT box, const Button& b) {
    if (!b.enabledString) return 0;
    const std::string& raw = gExtras.strings.text(b.enabledString - 1);
    ui::Paint p(dc);
    return paintButtonTooltip(*p, draw::Rect(box), raw, buttonCost(b, buttonPrefix(raw) ? raw[1] : 0));
}

// The command card: 3 x 3 cells, each with its button's icon. Click selects (again: the next button in that
// place); drag to another cell moves it; an empty cell adds a button there.
int paintCommandCard(HDC dc, int set, int x, int y, int cell) {
    const auto& bs = gExtras.buttons.set(set);
    const int gap = scale(4);
    RECT card{x, y, x + 3 * cell + 4 * gap, y + 3 * cell + 4 * gap};
    fillAlpha(dc, card, RGB(14, 14, 16), 255);
    roundBox(dc, card, scale(6), kBorder);
    for (int p = 1; p <= 9; ++p) {
        RECT c{x + gap + (p - 1) % 3 * (cell + gap), y + gap + (p - 1) / 3 * (cell + gap), 0, 0};
        c.right = c.left + cell, c.bottom = c.top + cell;
        const std::vector<int> here = buttonsAtPlace(bs, p);
        const bool sel = std::find(here.begin(), here.end(), gExtras.button) != here.end();
        const bool hot = mouseIn(c);
        fillAlpha(dc, c, sel ? kSel : hot ? RGB(36, 39, 44) : RGB(26, 28, 32), 255);
        roundBox(dc, c, scale(4), sel ? kAccent : RGB(48, 52, 58), sel ? 2 : 1);
        if (!here.empty()) {
            const int shown = sel ? gExtras.button : here[0];
            if (const auto* b = datRefIcon("icons", bs[size_t(shown)].icon); b && b->bmp) drawFitted(dc, *b, {c.left + scale(5), c.top + scale(5), c.right - scale(5), c.bottom - scale(5)});
            if (here.size() > 1) {  // more than one button here: the game shows the first whose condition holds; a dot each, in order
                const int d = scale(5), dg = scale(3), w = int(here.size()) * (d + dg) - dg;
                for (size_t j = 0; j < here.size(); ++j) {
                    const int dx = (c.left + c.right - w) / 2 + int(j) * (d + dg), dy = c.bottom - scale(9);
                    RECT dot{dx, dy, dx + d, dy + d};
                    fillAlpha(dc, dot, here[j] == shown ? kAccent : RGB(110, 114, 120), 255);
                }
            }
        } else if (hot) {
            SetTextColor(dc, kDim);
            DrawTextW(dc, L"+", 1, &c, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
        std::wstring tip;
        if (!here.empty()) {
            const Button& b = bs[size_t(here[0])];
            tip = statString(b.enabledString);
            if (here.size() > 1) tip += L"\n" + std::to_wstring(here.size()) + L" buttons in this place. The game shows the first whose condition holds. Click again for the next one.";
        } else {
            tip = L"Add a button here";
        }
        const int p0 = p;
        hitsOut().push_back({c,
                               [set, x, y, cell, gap](POINT pt) {  // drag: the selected button to the cell under the mouse
                                   const uint16_t np = buttonPlaceAt((pt.x - x - gap) / (cell + gap), (pt.y - y - gap) / (cell + gap));
                                   if (!np) return;
                                   auto bs2 = gExtras.buttons.set(set);
                                   if (!buttonMoveTo(bs2, gExtras.button, np)) return;
                                   gExtras.buttons.set(set, bs2);
                                   InvalidateRect(gData.form, nullptr, FALSE);
                               },
                               [set, here, p0] {
                                   if (here.empty()) {  // a new button
                                       auto bs2 = gExtras.buttons.set(set);
                                       bs2.push_back(buttonNew(uint16_t(p0)));
                                       gExtras.buttons.set(set, bs2);
                                       gExtras.button = int(bs2.size()) - 1;
                                       extrasChanged();
                                       return;
                                   }
                                   gExtras.button = buttonNextInPlace(here, gExtras.button);
                                   InvalidateRect(gData.form, nullptr, FALSE);
                               },
                               tip, here.empty() ? IDC_HAND : IDC_SIZEALL, {}});
    }
    return card.bottom - y;
}

void paintButtonsForm(HDC dc, RECT rc, int& y) {
    const FormLayout l = formLayout(rc);
    const int set = std::clamp(gData.entry, 0, ButtonSets::kSets - 1);
    const auto& bs = gExtras.buttons.set(set);
    if (gExtras.button >= int(bs.size())) gExtras.button = bs.empty() ? 0 : int(bs.size()) - 1;
    y = formTitle(dc, l, y, tabEntryIcon(buttonsTab(), set), buttonSetName(set), L"Button set " + std::to_wstring(set) + L"  \x00B7  " + std::to_wstring(bs.size()) + L" buttons",
                  gExtras.buttons.changed(set), [set] {
                      gExtras.buttons.reset(set);
                      extrasChanged();
                  });
    // The card and the tooltip: pictures, in the right column (above the fields when the form is narrow).
    const int cell = scale(64), top = y;
    int picBottom = y + paintCommandCard(dc, set, l.picX, y, cell);
    // The buttons, in the order the game checks them: in one place, the first whose condition holds shows.
    y = sectionHeader(dc, l, y, L"Buttons", gExtras.buttons.changed(set));
    for (size_t i = 0; i < bs.size(); ++i) {
        const Button& b = bs[i];
        RECT r{l.x0 - scale(6), y, l.colRight, y + l.rowH};
        const bool on = int(i) == gExtras.button, hot = mouseIn(r);
        ui::listRow(dc, r, on, hot);
        int sharing = 0;  // others in the same place
        for (const Button& o : bs) sharing += o.pos == b.pos;
        const int gx = l.x0 + scale(14);  // after the grip
        SetTextColor(dc, kDim);
        RECT nr{gx, y, gx + scale(26), y + l.rowH};
        DrawTextW(dc, std::to_wstring(i).c_str(), -1, &nr, DT_SINGLELINE | DT_VCENTER);
        if (const auto* ic = datRefIcon("icons", b.icon); ic && ic->bmp) drawFitted(dc, *ic, {gx + scale(28), y + scale(3), gx + scale(48), y + l.rowH - scale(3)});
        RECT pr{gx + scale(54), y, gx + scale(120), y + l.rowH};
        SetTextColor(dc, sharing > 1 ? kWarn : kDim);
        DrawTextW(dc, buttonCardCell(b.pos).c_str(), -1, &pr, DT_SINGLELINE | DT_VCENTER);
        RECT tr{gx + scale(120), y, l.colRight - scale(80), y + l.rowH};
        SetTextColor(dc, on ? kAccent : kText);
        DrawTextW(dc, buttonRowText(b).c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        const int bi = int(i);
        hitsOut().push_back({r, nullptr, [bi] {
                                 gExtras.button = bi;
                                 InvalidateRect(gData.form, nullptr, FALSE);
                             },
                             sharing > 1 ? L"Shares its place with another button: the game shows the first whose condition holds (the higher one in this list)" : L"",
                             IDC_HAND, {}});
        ui::reorderRow(dc, "buttons", r, bi, [set](int from, int to) {
            auto bs2 = gExtras.buttons.set(set);
            gExtras.button = buttonReorder(bs2, from, to);
            gExtras.buttons.set(set, bs2);
            extrasChanged();
        });
        // On the row: earlier, later, remove.
        auto move = [set, bi](int by) {
            return [set, bi, by] {
                auto bs2 = gExtras.buttons.set(set);
                gExtras.button = buttonSwapWith(bs2, bi, by);
                gExtras.buttons.set(set, bs2);
                extrasChanged();
            };
        };
        ui::rowActions(dc, r,
                       {{UiIcon::MoveUp, L"Earlier: checked before the one above", move(-1), ui::Tone::Normal, false, i > 0},
                        {UiIcon::MoveDown, L"Later", move(1), ui::Tone::Normal, false, i + 1 < bs.size()},
                        {UiIcon::Close, L"Remove the button",
                         [set, bi] {
                             auto bs2 = gExtras.buttons.set(set);
                             gExtras.button = buttonRemoveAt(bs2, bi);
                             gExtras.buttons.set(set, bs2);
                             extrasChanged();
                         },
                         ui::Tone::Danger}},
                       false);
        y += l.rowH;
    }
    ui::reorderDropLine(dc, "buttons");
    {
        const int ay = y;
        linkButton(dc, l.x0, y, l.rowH, L"Add a button", [set, ay] {
            auto bs2 = gExtras.buttons.set(set);
            bs2.push_back(buttonNew(buttonFreePlace(bs2)));
            gExtras.buttons.set(set, bs2);
            gExtras.button = int(bs2.size()) - 1;
            extrasChanged();
            (void)ay;
        });
        y += l.rowH + scale(8);
    }
    y = sectionHeader(dc, l, y, bs.empty() ? L"Button" : L"Button " + std::to_wstring(gExtras.button), gExtras.buttons.changed(set));
    if (!bs.empty()) picBottom += scale(12) + paintButtonTooltip(dc, {l.picX, picBottom + scale(12), l.picX + 3 * cell + scale(16), 0}, bs[size_t(gExtras.button)]);
    if (!l.twoCol) y = picBottom + scale(16);
    if (bs.empty()) {
        fieldLabel(dc, l, y, L"No buttons", false);
        y += l.rowH;
    } else {
        const int bi = gExtras.button;
        const Button& b = bs[size_t(bi)];
        const auto& stock = gExtras.buttons.stock(set);
        const Button* sb = size_t(bi) < stock.size() ? &stock[size_t(bi)] : nullptr;
        auto edit = [set, bi](std::function<void(Button&)> f) {
            auto bs2 = gExtras.buttons.set(set);
            if (size_t(bi) >= bs2.size()) return;
            f(bs2[size_t(bi)]);
            gExtras.buttons.set(set, bs2);
            extrasChanged();
        };
        auto row = [&](const wchar_t* name, bool changed, const std::wstring& text, std::function<void(RECT)> click, const UnitPalette::Bitmap* icon, int number,
                       std::function<void()> open, const std::wstring& tip = L"") {
            fieldLabel(dc, l, y, name, changed);
            pickBox(dc, {l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)}, text, changed, std::move(click), tip, icon, number,
                    std::move(open));
            y += l.rowH;
        };
        row(L"Position", sb && sb->pos != b.pos, buttonRowColumn(b.pos),
            [edit, b](RECT r) {
                std::vector<ListPickItem> items;
                for (int p = 1; p <= 9; ++p) items.push_back({buttonRowColumn(p), L"", std::to_string(p)});
                std::string picked;
                if (showListPicker(L"Position", items, std::to_string(b.pos), belowBox(r), picked)) edit([&](Button& x) { x.pos = uint16_t(std::stoul(picked)); });
            },
            nullptr, b.pos, nullptr);
        const auto& iconNames = DatSchema::entryNames("icons");
        row(L"Icon", sb && sb->icon != b.icon, b.icon < iconNames.size() ? widen(iconNames[b.icon]) : L"Icon",
            [edit, b](RECT r) {
                std::string picked;
                if (showListPicker(L"Icon", entryItems("icons"), std::to_string(b.icon), belowBox(r), picked)) edit([&](Button& x) { x.icon = uint16_t(std::stoul(picked)); });
            },
            datRefIcon("icons", b.icon), b.icon, nullptr);
        // Condition and action: their functions, and a value when the function takes one.
        for (int which = 0; which < 2; ++which) {
            const bool cond = which == 0;
            const uint32_t fn = cond ? b.condition : b.action;
            const ButtonFunctionDef* d = cond ? ButtonSets::condition(fn) : ButtonSets::action(fn);
            wchar_t hex[24];
            std::swprintf(hex, 24, L"Function 0x%06X", unsigned(fn));
            row(cond ? L"Shown when" : L"Action", sb && (cond ? sb->condition != fn : sb->action != fn), d ? widen(d->name) : std::wstring(hex),
                [edit, cond, fn](RECT r) {
                    std::vector<ListPickItem> items;
                    for (const auto& f : cond ? ButtonSets::conditions() : ButtonSets::actions()) items.push_back({widen(f.name), L"", std::to_string(f.address)});
                    std::string picked;
                    if (showListPicker(cond ? L"Shown when" : L"Action", items, std::to_string(fn), belowBox(r), picked))
                        edit([&](Button& x) { (cond ? x.condition : x.action) = uint32_t(std::stoul(picked)); });
                },
                nullptr, -1, nullptr, cond ? L"The game's condition for showing the button" : L"Select the action that the button performs");
            if (d && d->arg) {
                const uint16_t v = cond ? b.conditionArg : b.actionArg;
                const char* kind = d->arg;
                row(cond ? L"Shown when: value" : L"Action: value", sb && (cond ? sb->conditionArg != v : sb->actionArg != v), argName(kind, v),
                    [edit, cond, v, kind](RECT r) {
                        std::string picked;
                        if (showListPicker(L"Value", entryItems(kind), std::to_string(v), belowBox(r), picked))
                            edit([&](Button& x) { (cond ? x.conditionArg : x.actionArg) = uint16_t(std::stoul(picked)); });
                    },
                    argIcon(kind, v), v, openEntry(kind, v));
            }
        }
        // Texts: the tooltip itself, edited here (the string in stat_txt.tbl: every button that uses it changes);
        // the arrow picks another string. Under each: a link to the string in the Strings tab, and its raw value when
        // "Display raw value" is on (the same setting as the Strings tab's).
        ui::check(dc, {l.x0 + l.labelW, y, l.x0 + l.labelW + scale(300), y + l.rowH}, L"Display raw value", gExtras.rawText, [] {
            gExtras.rawText = !gExtras.rawText;
            gExtras.textFor = -1;
            InvalidateRect(gData.form, nullptr, FALSE);
        }, L"Show each tooltip's string as the game stores it: the hotkey and type characters, and codes as <01>");
        y += l.rowH;
        for (int which = 0; which < 2; ++which) {
            const uint16_t s = which ? b.disabledString : b.enabledString;
            const bool strChanged = s && gExtras.strings.changed(s - 1);
            fieldLabel(dc, l, y, which ? L"Tooltip when grayed" : L"Tooltip", (sb && (which ? sb->disabledString != s : sb->enabledString != s)) || strChanged);
            RECT box{l.x0 + l.labelW, y + scale(2), l.colRight - scale(30), y + l.rowH - scale(2)};
            const bool hot = mouseIn(box);
            // The game's text colors read on a dark ground only: the box stays dark in the light theme.
            fillAlpha(dc, box, gLightTheme ? (hot ? RGB(46, 48, 54) : RGB(34, 36, 40)) : hot ? RGB(36, 39, 44) : kCtl, 255);
            roundBox(dc, box, scale(6), strChanged ? kGoodLine : kBorder);
            RECT tb{box.left + scale(8), box.top, box.right - scale(8), box.bottom};
            if (s) {
                const std::string& raw = gExtras.strings.text(s - 1);
                const size_t nl = raw.find('\n');
                const int saved = SaveDC(dc);
                IntersectClipRect(dc, tb.left, tb.top, tb.right, tb.bottom);
                drawGameText(dc, {tb.left, tb.top + (tb.bottom - tb.top - scale(18)) / 2, tb.right, tb.bottom}, nl == std::string::npos ? raw : raw.substr(0, nl),
                             RGB(184, 184, 232));
                RestoreDC(dc, saved);
            } else {
                SetTextColor(dc, kDim);
                DrawTextW(dc, L"None", -1, &tb, DT_SINGLELINE | DT_VCENTER);
            }
            hitsOut().push_back({box, nullptr, [edit, which, s, box] {
                                     if (!s) return;
                                     const std::string raw = gExtras.strings.text(s - 1);
                                     const size_t lead = buttonPrefix(raw) ? 2 : 0;  // the hotkey and the button type stay
                                     std::string body = raw.substr(lead);
                                     const bool crlf = body.find("\r\n") != std::string::npos;
                                     for (size_t at; (at = body.find("\r\n")) != std::string::npos;) body.erase(at, 1);
                                     POINT pt{box.left, box.bottom + scale(4)};
                                     ClientToScreen(gData.form, &pt);
                                     std::string out = body;
                                     if (!editGameText(which ? L"Tooltip when grayed" : L"Tooltip", out, pt)) return;
                                     if (crlf) {  // the string's own line ends
                                         std::string o;
                                         for (char ch : out) ch == '\n' ? o += "\r\n" : o += ch;
                                         out = o;
                                     }
                                     gExtras.strings.set(s - 1, raw.substr(0, lead) + out);
                                     extrasChanged();
                                 },
                                 s ? L"Edit the text (string " + std::to_wstring(s) + L"; every button that uses it changes)" : L"", IDC_HAND, {}});
            RECT pick{box.right + scale(6), y + scale(2), l.colRight, y + l.rowH - scale(2)};
            ui::glyph(dc, pick, UiIcon::Dropdown, mouseIn(pick) ? kAccent : kDim);  // a list: another string
            hitsOut().push_back({pick, nullptr, [edit, which, s, pick] {
                                     std::string picked;
                                     if (showListPicker(L"Text", entryItems("strings", true), std::to_string(s), belowBox(pick), picked))
                                         edit([&](Button& x) { (which ? x.disabledString : x.enabledString) = uint16_t(std::stoul(picked)); });
                                 },
                                 L"Another string", IDC_HAND, {}});
            y += l.rowH;
            if (s) {  // the string: go to it; its raw value
                const int sid = s;
                const int lx = ui::link(dc, l.x0 + l.labelW, y, scale(22), L"String " + std::to_wstring(s), [sid] { dataGoTo("strings", sid); },
                                        L"Open this string in the Strings tab", ui::Tone::Primary);
                if (gExtras.rawText) {
                    HGDIOBJ of2 = SelectObject(dc, monoFont());
                    SetTextColor(dc, kDim);
                    RECT rt{lx + scale(12), y, l.colRight, y + scale(22)};
                    DrawTextW(dc, widen(StatTxt::escape(gExtras.strings.text(s - 1))).c_str(), -1, &rt,
                              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
                    SelectObject(dc, of2);
                }
                y += scale(24);
            }
        }
    }
    if (l.twoCol) y = std::max(y, picBottom);
    (void)top;
    y += scale(14);
}

// --- A whole button, in a pop-out (a trigger's Replace a button) ------------------------------------------------

// The pop-out's view is core/views_data_button_pop.inc (ButtonPopView); this is the Windows program's side of it: the
// host (the icons, the names, the list pickers below the boxes) and the window with its own message loop.
struct ButtonPop final : ButtonPopHost {
    HWND wnd = nullptr;
    bool done = false, accepted = false, picking = false;
    draw::Image icon(const char* dat, int entry) override { return ui::imageOf(datRefIcon(dat, uint32_t(entry))); }
    draw::Image argIcon(const char* kind, int v) override { return ui::imageOf(::argIcon(kind, v)); }
    std::wstring argName(const char* kind, int v) override { return ::argName(kind, v); }
    std::wstring stringLine(int id) override { return statString(id); }
    std::string stringBytes(int id) override { return id >= 1 && id <= gExtras.strings.count() ? gExtras.strings.text(id - 1) : std::string(); }
    std::wstring cost(const Button& b, int type) override { return buttonCost(b, type); }
    std::vector<ListPickItem> entryItems(const char* kind, bool none) override { return ::entryItems(kind, none); }
    std::vector<Button> buttonSet(int set) override { return gExtras.buttons.set(set); }
    std::wstring buttonLabel(const Button& b) override { return buttonRowText(b); }
    bool choose(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, draw::Rect box, std::string& out) override {
        POINT at{box.left, box.bottom + scale(2)};  // (the pickers open below the box, as belowBox has it)
        ClientToScreen(wnd, &at);
        picking = true;  // (the picker takes the focus: the pop-out stays)
        const bool r = showListPicker(prompt, std::move(items), current, at, out);
        picking = false;
        return r;
    }
} * gButtonPop = nullptr;

// Clicking elsewhere keeps the button (as OK); a picker that it opens does not.
LRESULT CALLBACK buttonPopSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* p = reinterpret_cast<ButtonPop*>(ref);
    if (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE && !p->picking && !gBackground) p->accepted = p->done = true;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// The button pop-out: `text` as buttonToText writes it; true when OK.
bool showButtonPopup(std::string& text, POINT at) {
    if (gListWheel) return false;  // the mouse wheel does not open a pop-out
    dataLoadForProject();  // the icons and strings come from the project
    ButtonPop p;
    Button b;
    if (!buttonFromText(text, b)) b = Button{1, 0, kButtonNewCondition, kButtonNewAction, 0, 0, 0, 0};
    ButtonPopView view(p, b);
    view.onDone = [&p](bool ok) { p.accepted = ok, p.done = true; };
    view.onFit = [&p](int want) {  // the window's height for the client height the fields want
        RECT wr, rc;
        GetWindowRect(p.wnd, &wr);
        GetClientRect(p.wnd, &rc);
        SetWindowPos(p.wnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top - rc.bottom + want, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    };
    ButtonPop* outer = gButtonPop;
    gButtonPop = &p;
    const int w = scale(560), h = scale(470);
    p.wnd = CreateWindowExW(WS_EX_TOOLWINDOW | (gBackground ? WS_EX_NOACTIVATE : 0), ui::kViewClass, L"", WS_POPUP | WS_BORDER, at.x, at.y, w, h, app.main,
                            nullptr, app.inst, &view);
    SetWindowSubclass(p.wnd, buttonPopSubclass, 1, DWORD_PTR(&p));
    ui::popupFrame(p.wnd);
    ShowWindow(p.wnd, gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (gButtonPopHook) gButtonPopHook(p.wnd);
    MSG msg;
    while (!p.done && GetMessageW(&msg, nullptr, 0, 0))
        if (!popoutEatsPress(msg, p.wnd)) TranslateMessage(&msg), DispatchMessageW(&msg);
    RemoveWindowSubclass(p.wnd, buttonPopSubclass, 1);
    DestroyWindow(p.wnd);
    gButtonPop = outer;
    if (p.accepted) text = buttonToText(view.button());
    return p.accepted;
}

// --- Strings tab -----------------------------------------------------------------------------------------------

// The editor holds the text without a button's hotkey and type characters (core views_data_strings.inc).
std::string stringEditBody(int id) { return stringEditBody(gExtras.strings.text(id - 1)); }
std::string stringEditPrefix(int id) { return stringEditPrefix(gExtras.strings.text(id - 1)); }
// The editor: the core's game text view (core/views_game_text.inc) in a view window of the form; it tells the form as
// the native editor did (WM_COMMAND, id 1801: EN_CHANGE, EN_KILLFOCUS).
std::unique_ptr<GameTextView> gStringsText;
void stringsCommitEdit() {
    if (!gExtras.textEdit || gExtras.textFor < 1 || !gExtras.textDirty) return;
    gExtras.textDirty = false;
    const int id = gExtras.textFor;
    const std::string text = stringEditCommit(gExtras.strings.text(id - 1), gStringsText->bytes());
    if (text == gExtras.strings.text(id - 1)) return;
    gExtras.strings.set(id - 1, text);
    extrasChanged();
}
// The game's strings are in-game text: the game's colors (with the players' colors).
GameTextSpec stringsTextSpec() {
    GameTextSpec s;
    s.palette = TextPalette::Game, s.multiline = true, s.align = true;
    return s;
}
void stringsShowEdit(bool show) {
    if (!show) {
        if (gExtras.textEdit && IsWindowVisible(gExtras.textEdit)) stringsCommitEdit(), ShowWindow(gExtras.textEdit, SW_HIDE);
        return;
    }
    if (!gExtras.textEdit) {
        gStringsText = std::make_unique<GameTextView>(stringsTextSpec());
        auto notify = [](WORD code) { SendMessageW(gData.form, WM_COMMAND, MAKEWPARAM(1801, code), LPARAM(gExtras.textEdit)); };
        gStringsText->onChange = [notify] { notify(EN_CHANGE); };
        gStringsText->onFocusLost = [notify] { notify(EN_KILLFOCUS); };
        gExtras.textEdit = CreateWindowExW(0, ui::kViewClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, scale(400), scale(160), gData.form, HMENU(INT_PTR(1801)),
                                           app.inst, gStringsText.get());
    }
}

void paintStringsForm(HDC dc, RECT rc, int& y) {
    const FormLayout l = formLayout(rc);
    const int id = std::clamp(gData.entry, 1, gExtras.strings.count());
    const std::string& raw = gExtras.strings.text(id - 1);
    y = formTitle(dc, l, y, nullptr, stringLine(raw), L"stat_txt.tbl string " + std::to_wstring(id), gExtras.strings.changed(id - 1), [id] {
        gExtras.strings.reset(id - 1);
        gExtras.textFor = -1;
        extrasChanged();
    }, UiIcon::DatStrings);
    y = sectionHeader(dc, l, y, L"Text", gExtras.strings.changed(id - 1));
    const int top = y;
    // The preview: a picture, in the right column (above the fields when narrow); its legend is the color codes.
    std::string previewRaw = raw;
    if (gExtras.textDirty && gExtras.textEdit && gExtras.textFor == id) previewRaw = stringEditCommit(raw, gStringsText->bytes());
    int picBottom;
    {
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        const int lines = 1 + int(std::count(previewRaw.begin(), previewRaw.end(), '\n'));
        RECT pb{l.picX, y, l.picX + std::min(l.picW, scale(420)), y + std::max(scale(60), lines * (int(tm.tmHeight) + scale(2)) + scale(16))};
        fillAlpha(dc, pb, RGB(8, 8, 10), 255);
        roundBox(dc, pb, scale(4), RGB(60, 64, 70));
        drawGameText(dc, {pb.left + scale(10), pb.top + scale(8), pb.right - scale(10), pb.bottom}, previewRaw, RGB(208, 208, 208));
        {  // which box is which: this one is the game's look
            HGDIOBJ of2 = SelectObject(dc, ui::smallFont());
            SetTextColor(dc, RGB(120, 124, 130));
            RECT cap{pb.left + scale(10), pb.bottom - scale(18), pb.right - scale(8), pb.bottom - scale(3)};
            DrawTextW(dc, L"Game preview", -1, &cap, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
            SelectObject(dc, of2);
        }
        hitsOut().push_back({pb, nullptr, nullptr, L"Game preview: the text as the game shows it", IDC_ARROW, {}});
        picBottom = pb.bottom;
    }
    if (!l.twoCol) y = picBottom + scale(12);
    // A button's text (the one shown when it can be used): its hotkey and type as fields of their own.
    if (stringIsButtonText(gExtras.buttons, id, raw)) {
        const StringButtonParts bp = stringButtonParts(raw);
        const bool has = bp.has;
        const char hot = bp.hot;
        const int type = bp.type;
        const std::string& stockRaw = gExtras.strings.stock(id - 1);
        const bool hotChanged = buttonPrefix(stockRaw) != has || (has && stockRaw[0] != hot), typeChanged = buttonPrefix(stockRaw) != has || (has && stockRaw[1] != raw[1]);
        auto setPrefix = [id](char h, int t) {
            gExtras.strings.set(id - 1, stringWithButtonPrefix(gExtras.strings.text(id - 1), h, t));
            gExtras.textFor = -1;
            extrasChanged();
        };
        fieldLabel(dc, l, y, L"Hotkey", hotChanged);
        const std::wstring hotText = stringHotkeyText(bp);
        pickBox(dc, {l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)}, hotText, hotChanged, [setPrefix, type](RECT r) {
            std::vector<ListPickItem> items;
            for (char c = 'A'; c <= 'Z'; ++c) items.push_back({std::wstring(1, wchar_t(c)), L"", std::string(1, char(std::tolower(c)))});
            for (char c = '0'; c <= '9'; ++c) items.push_back({std::wstring(1, wchar_t(c)), L"", std::string(1, c)});
            items.push_back({L"Esc", L"", std::string(1, char(27))});
            std::string picked;
            if (showListPicker(L"Hotkey", items, "", belowBox(r), picked) && !picked.empty()) setPrefix(picked[0], type);
        }, L"The key that presses the button");
        y += l.rowH;
        fieldLabel(dc, l, y, L"Type", typeChanged);
        pickBox(dc, {l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)}, stringButtonTypeName(has ? type : -1), typeChanged,
                [setPrefix, hot, has](RECT r) {
                    std::vector<ListPickItem> items;
                    for (int t = 0; t < 6; ++t) items.push_back({stringButtonTypeName(t), L"", std::to_string(t)});
                    std::string picked;
                    if (showListPicker(L"Type", items, "", belowBox(r), picked)) setPrefix(has ? hot : 'a', std::stoi(picked));
                },
                L"The kind of button. For a unit, an upgrade or a technology, the game's tooltip shows the cost.", nullptr, has ? type : -1);
        y += l.rowH + scale(8);
    }
    // The text: the editor over the field column (Preview: the text in the game's colors; Raw: the codes as <XX>).
    stringsShowEdit(true);
    const RECT er{l.x0, y, l.colRight, y + gameTextHeight(stringsTextSpec(), l.colRight - l.x0, scale(120))};
    if (gExtras.textFor != id) {
        gExtras.textFor = id;
        gStringsText->setBytes(stringEditBody(id));
        gExtras.textDirty = false;
    }
    RECT now;
    GetWindowRect(gExtras.textEdit, &now);
    MapWindowPoints(nullptr, gData.form, reinterpret_cast<POINT*>(&now), 2);
    if (!EqualRect(&now, &er)) SetWindowPos(gExtras.textEdit, nullptr, er.left, er.top, er.right - er.left, er.bottom - er.top, SWP_NOZORDER | SWP_NOACTIVATE);
    gStringsText->setColors(kRichDefault, kRichBack);
    gStringsText->setChanged(gExtras.strings.changed(id - 1));
    ShowWindow(gExtras.textEdit, SW_SHOWNA);
    y = er.bottom + scale(10);
    // Room: the game copies the strings over its own.
    y = capacityBar(dc, l, y, gExtras.strings.build().size(), gExtras.strings.capacity(), L"Size of all strings together. The game has room for at most the number after the slash.");
    if (l.twoCol) y = std::max(y, picBottom);
    (void)top;
    y += scale(14);
}
