// --- The control kit ---------------------------------------------------------------------------------------------
// One look for each kind of control; docs/dev/CONTROLS.md lists them and where each is used. Painted controls draw into a DC
// and put their click areas in hitsOut() (the surface being painted: the Data form, the briefing, the files, a
// pop-out). Native controls (push buttons, check boxes, radio buttons, edit boxes) get the same look from the dark
// control subclass, which paints them with the functions here.
//
// Each control takes what it needs to fit its place (a tone, an icon, a width), never its own colors: the colors
// are the tokens at the top of main.cpp.

namespace ui {

// The kit's sizes (kRadius, kRowH, kButtonH, kDialogPad, ...) are at the top of main.cpp, after the theme, so the
// dialogs made before this file can use them too.

// Where the mouse is (screen). Datashots set a pretend place (mouse:<x>:<y>) to capture what shows under the
// mouse without moving the real one.
inline bool gPretendMouse = false;
inline POINT gPretendMousePos{};
void cursorPos(POINT& m) {
    if (gPretendMouse) m = gPretendMousePos;
    else GetCursorPos(&m);
}
// Whether the mouse is in `r` of window `w`; hot(r): on the surface being painted.
bool hotIn(HWND w, RECT r) {
    POINT m;
    cursorPos(m);
    ScreenToClient(w, &m);
    return PtInRect(&r, m) != 0;
}
bool hot(RECT r) { return hotIn(hitsWnd(), r); }

// A rounded box: `fill` (CLR_NONE: none) and a 1px `line` (CLR_NONE: none).
void box(HDC dc, RECT r, COLORREF fill, COLORREF line, int radius = kRadius) {
    HBRUSH b = fill == CLR_NONE ? nullptr : CreateSolidBrush(fill);
    HPEN p = line == CLR_NONE ? nullptr : CreatePen(PS_SOLID, 1, line);
    HGDIOBJ ob = SelectObject(dc, b ? HGDIOBJ(b) : GetStockObject(NULL_BRUSH)), op = SelectObject(dc, p ? HGDIOBJ(p) : GetStockObject(NULL_PEN));
    const int d = 2 * scale(radius);
    RoundRect(dc, r.left, r.top, r.right + (p ? 0 : 1), r.bottom + (p ? 0 : 1), d, d);
    SelectObject(dc, ob), SelectObject(dc, op);
    if (b) DeleteObject(b);
    if (p) DeleteObject(p);
}

int textWidth(HDC dc, const std::wstring& t) {
    SIZE sz{};
    GetTextExtentPoint32W(dc, t.c_str(), int(t.size()), &sz);
    return sz.cx;
}

// --- Buttons ----------------------------------------------------------------------------------------------------
// Normal: a command. Primary: the one a form or dialog is for (OK, Create, Build). Danger: removes something.
enum class Tone { Normal, Primary, Danger, Quiet };
enum class State { Normal, Hot, Down, Disabled };

COLORREF toneText(Tone t) { return t == Tone::Primary ? kAccent : t == Tone::Danger ? kBad : t == Tone::Quiet ? kDim : kText; }

// The face of a button (painted ones and the native push buttons alike).
void buttonFace(HDC dc, RECT r, const std::wstring& text, Tone tone, State s, HFONT font = nullptr) {
    COLORREF fill = kCtl, line = kBorder;
    if (tone == Tone::Primary) fill = kSel, line = kAccentLine;
    if (s == State::Hot) fill = tone == Tone::Primary ? kSelHot : kCtlHot, line = tone == Tone::Primary ? kAccent : kBorderHot;
    if (s == State::Down) fill = tone == Tone::Primary ? kSel : kCtlDown;
    if (s == State::Disabled) fill = kBg, line = kBorder;
    box(dc, r, fill, line);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, s == State::Disabled ? kFaint : toneText(tone == Tone::Quiet ? Tone::Normal : tone));
    HGDIOBJ of = font ? SelectObject(dc, font) : nullptr;
    RECT t = r;
    DrawTextW(dc, text.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (of) SelectObject(dc, of);
}
// A button's width for its text.
int buttonWidth(HDC dc, const std::wstring& text) { return std::max(scale(kButtonMinW), textWidth(dc, text) + scale(28)); }

void button(HDC dc, RECT r, const std::wstring& text, Tone tone, std::function<void()> click, const std::wstring& tip = L"", bool enabled = true) {
    buttonFace(dc, r, text, tone, !enabled ? State::Disabled : hot(r) ? State::Hot : State::Normal);
    hitsOut().push_back({r, nullptr, enabled ? std::move(click) : nullptr, tip, enabled ? IDC_HAND : IDC_ARROW, {}});
}
// A row of buttons from the right edge (OK, Cancel: the first is the rightmost but one, as Windows has them).
// Returns the left edge.
struct ButtonSpec {
    std::wstring text;
    Tone tone = Tone::Normal;
    std::function<void()> click;
    std::wstring tip;
};
int buttonsRight(HDC dc, int right, int y, const std::vector<ButtonSpec>& bs) {
    int x = right;
    for (auto it = bs.rbegin(); it != bs.rend(); ++it) {
        const int w = buttonWidth(dc, it->text);
        button(dc, {x - w, y, x, y + scale(kButtonH)}, it->text, it->tone, it->click, it->tip);
        x -= w + scale(kButtonGap);
    }
    return x + scale(kButtonGap);
}
// The same row for native push buttons (dialogs): each as wide as its text needs (at least kButtonMinW), kButtonH
// high, kButtonGap apart, the last one's right edge at `right`. Returns the left edge.
int nativeButtonWidth(HWND b) {
    wchar_t text[128] = L"";
    GetWindowTextW(b, text, 128);
    HDC dc = GetDC(b);
    HGDIOBJ of = SelectObject(dc, HFONT(SendMessageW(b, WM_GETFONT, 0, 0)));
    const int w = buttonWidth(dc, text);
    SelectObject(dc, of);
    ReleaseDC(b, dc);
    return w;
}
int nativeButtonsRight(const std::vector<HWND>& bs, int right, int y) {
    int x = right;
    for (size_t i = bs.size(); i-- > 0;) {
        if (!bs[i]) continue;
        const int w = nativeButtonWidth(bs[i]);
        MoveWindow(bs[i], x - w, y, w, scale(kButtonH), TRUE);
        x -= w + scale(kButtonGap);
    }
    return x + scale(kButtonGap);
}
// From the left edge: returns the right edge.
int nativeButtonsLeft(const std::vector<HWND>& bs, int left, int y) {
    int x = left;
    for (HWND b : bs) {
        if (!b) continue;
        const int w = nativeButtonWidth(b);
        MoveWindow(b, x, y, w, scale(kButtonH), TRUE);
        x += w + scale(kButtonGap);
    }
    return x - scale(kButtonGap);
}

// A small button: an action in a row, under a list or inside text ("Add an action", "Reset"; PRINCIPLES.md, "Buttons
// for actions, links for entities"). As wide as its text, in the middle of the row. Returns the right edge.
int smallButton(HDC dc, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"",
                Tone tone = Tone::Normal, bool enabled = true) {
    const int bh = std::min(h, scale(24)), w = textWidth(dc, text) + scale(20);
    const RECT r{x, y + (h - bh) / 2, x + w, y + (h - bh) / 2 + bh};
    const Tone t = tone == Tone::Primary ? Tone::Normal : tone;  // a small button is never the main action
    buttonFace(dc, r, text, t, !enabled ? State::Disabled : hot(r) ? State::Hot : State::Normal);
    hitsOut().push_back({r, nullptr, enabled ? std::move(click) : nullptr, tip, enabled ? IDC_HAND : IDC_ARROW, {}});
    return r.right;
}

// "Add ..." under a list (blocks, rows, triggers): a row as wide as the list, so it is clear where the new item
// goes. A quiet outline, a + and the text in dim; under the mouse the row's ground and the accent.
void addRowFace(HDC dc, RECT r, const std::wstring& text, bool over, bool enabled = true) {
    box(dc, r, over && enabled ? kRowHot : CLR_NONE, over && enabled ? kBorderHot : kBorder, 6);
    const int is = scale(16);
    const COLORREF c = !enabled ? kFaint : over ? kAccent : kDim;
    const RECT ir{r.left + scale(10), (r.top + r.bottom - is) / 2, r.left + scale(10) + is, (r.top + r.bottom + is) / 2};
    uiIcon(dc, ir, UiIcon::Add, c, is);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    RECT t{ir.right + scale(8), r.top, r.right - scale(8), r.bottom};
    DrawTextW(dc, text.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}
// The row, with its click. Returns the bottom. Not enabled (a limit is reached): gray, no click; the tooltip says why.
int addRow(HDC dc, RECT r, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"", bool enabled = true) {
    addRowFace(dc, r, text, enabled && hot(r), enabled);
    hitsOut().push_back({r, nullptr, enabled ? std::move(click) : nullptr, tip, enabled ? IDC_HAND : IDC_ARROW, {}});
    return r.bottom;
}

// A link: only to another entity (a unit, a string, a file, a web page); its text is the entity's name. Accent;
// underlined under the mouse. Returns the right edge.
int link(HDC dc, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"", Tone tone = Tone::Primary) {
    const int w = textWidth(dc, text);
    RECT r{x, y, x + w, y + h};
    const bool over = hot(r);
    SetTextColor(dc, tone == Tone::Quiet ? (over ? kText : kDim) : toneText(tone));
    DrawTextW(dc, text.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (over && tone != Tone::Quiet) {
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        const int base = (r.top + r.bottom + tm.tmHeight) / 2 - tm.tmDescent + scale(1);
        line(dc, r.left, base, r.right, base, toneText(tone));
    }
    hitsOut().push_back({r, nullptr, std::move(click), tip, IDC_HAND, {}});
    return r.right;
}

// --- Icon buttons -----------------------------------------------------------------------------------------------
// Bare: a glyph that lights up under the mouse (in rows, beside fields). Boxed: a glyph in a button box (players,
// tool bars); `on` marks a toggle that is on.
enum class Icon { Bare, Boxed };

// The largest square that fits in `r`, centered: every icon button is square, whatever place it gets.
RECT square(RECT r) {
    const int w = r.right - r.left, h = r.bottom - r.top, s = std::min(w, h);
    const int x = r.left + (w - s) / 2, y = r.top + (h - s) / 2;
    return {x, y, x + s, y + s};
}

// An icon: one of the UI icons (tools/icongen), or a Segoe MDL2 glyph where the icon set has none yet.
struct Ico {
    const wchar_t* glyph = nullptr;
    int icon = -1;
    Ico(const wchar_t* g) : glyph(g) {}
    Ico(UiIcon i) : icon(int(i)) {}
};
void glyph(HDC dc, RECT r, Ico g, COLORREF c, int size = kGlyph) {
    if (g.icon >= 0) return uiIcon(dc, r, UiIcon(g.icon), c, scale(16));
    HGDIOBJ of = SelectObject(dc, glyphFont(size));
    SetTextColor(dc, c);
    DrawTextW(dc, g.glyph, 1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    SelectObject(dc, of);
}

// A bare icon button's face, for surfaces with their own click areas (the triggers pane). `on` with Tone::Quiet:
// a toggle that is off (amber, as a block that is turned off).
void iconFace(HDC dc, RECT r, Ico g, Tone tone, bool on, bool over, bool enabled = true) {
    r = square(r);
    if (over && enabled) box(dc, r, kCtlHot, CLR_NONE);
    const COLORREF rest = tone == Tone::Quiet && on ? kWarn : on || tone == Tone::Primary ? kAccent : kDim;
    const COLORREF lit = tone == Tone::Danger ? kBad : tone == Tone::Quiet && on ? kWarn : tone == Tone::Primary || on ? kAccentBright : kText;
    glyph(dc, r, g, !enabled ? kFaint : over ? lit : rest);
}
void iconButton(HDC dc, RECT r, Ico g, std::function<void()> click, const std::wstring& tip, Icon style = Icon::Bare, bool on = false,
                bool enabled = true, Tone tone = Tone::Normal) {
    r = square(r);
    const bool over = enabled && hot(r);
    if (style == Icon::Boxed) {  // flat: a fill only when on (the accent) or under the mouse
        if (on) box(dc, r, kSel, kAccentLine);
        else if (over) box(dc, r, kCtlHot, CLR_NONE);
        glyph(dc, r, g, !enabled ? kFaint : on ? kAccentBright : tone == Tone::Danger ? kBad : over ? kText : kDim, 14);
    } else {
        iconFace(dc, r, g, tone, on, over, enabled);
    }
    hitsOut().push_back({r, nullptr, enabled ? std::move(click) : nullptr, tip, enabled ? IDC_HAND : IDC_ARROW, {}});
}

// --- Rows -------------------------------------------------------------------------------------------------------
// A list row's background: selected (fill and a bar on the left), under the mouse, or none.
void listRow(HDC dc, RECT r, bool selected, bool over) {
    if (selected) {
        fillAlpha(dc, r, kSel, 255);
        fillAlpha(dc, {r.left, r.top, r.left + scale(2), r.bottom}, kAccent, 255);
    } else if (over) {
        fillAlpha(dc, r, kRowHot, 255);
    }
}
COLORREF rowText(bool selected) { return selected ? kAccent : kText; }
// A row of an owner-drawn list box (pickers, the scripts list): the list's face, then the row.
void ownerRow(HDC dc, RECT r, bool selected, COLORREF face = kCtl) {
    fillAlpha(dc, r, face, 255);
    listRow(dc, r, selected, false);
    // The list's frame (paintDarkBorder) covers its outer 2 px: the accent bar goes just inside it.
    const int ring = scale(1) + 1;
    if (selected) fillAlpha(dc, {r.left + ring, r.top, r.left + ring + scale(2), r.bottom}, kAccent, 255);
}
// The mark of a changed entry: a small green dot.
void changedDot(HDC dc, int cx, int cy) {
    HBRUSH b = CreateSolidBrush(kGood);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
    const int r = scale(3);
    Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(dc, ob), SelectObject(dc, op);
    DeleteObject(b);
}

// The actions a row has at its right edge (on/off, duplicate, remove, earlier/later): shown on the row under the
// mouse (or the selected one), each a bare icon button.
struct RowAction {
    Ico glyph = L"";
    std::wstring tip;
    std::function<void()> click;
    Tone tone = Tone::Normal;  // Danger: removes; Primary: adds
    bool on = false;           // a toggle that is on
    bool enabled = true;
};
// Returns the left edge of the actions (the row's text should end there).
// Where row action `k` of `n` goes in a row (for surfaces with their own click areas).
RECT rowActionRect(RECT row, int k, int n) {
    const int w = scale(kIconBox), h = std::min<int>(row.bottom - row.top, scale(kIconBox));
    const int x = row.right - scale(4) - w * (n - k), y = (row.top + row.bottom - h) / 2;
    return {x, y, x + w, y + h};
}
int rowActions(HDC dc, RECT row, const std::vector<RowAction>& actions, bool show) {
    hitsOut().push_back({row, nullptr, nullptr, L"", IDC_ARROW, {}});  // the mouse entering the row repaints it
    const int w = scale(kIconBox), h = std::min<int>(row.bottom - row.top, scale(kIconBox));
    int x = row.right - scale(4) - w * int(actions.size());
    const int left = x;
    if (!show && !hot(row)) return left;
    const int y = (row.top + row.bottom - h) / 2;
    for (const auto& a : actions) {
        iconButton(dc, {x, y, x + w, y + h}, a.glyph, a.click, a.tip, Icon::Bare, a.on, a.enabled, a.tone);
        x += w;
    }
    return left;
}

// A painted scroll bar (for lists that page by rows): the track, and the thumb, lighter under the mouse or while
// it is dragged.
// As Windows 11's: a thin line at rest, wider with its track under the mouse or while dragged.
void scrollFace(HDC dc, RECT track, RECT thumb, bool lit) {
    if (lit) fillAlpha(dc, track, kPanel, 255);
    else thumb.left = thumb.right - std::max(2, int(thumb.right - thumb.left) / 2);
    box(dc, thumb, lit ? kThumbHot : kThumb, CLR_NONE, 3);
}

// --- Sliders --------------------------------------------------------------------------------------------------------
// A slider from x0 to x1 on the line cy: the track (kBorder line), the part up to the value in the accent, a round
// thumb in the accent (lighter under the mouse). `frac`: the value, 0 to 1.
void sliderFace(HDC dc, int x0, int x1, int cy, float frac, bool over, bool enabled = true) {
    const RECT track{x0, cy - scale(2), x1, cy + scale(2)};
    box(dc, track, kCtl, kBorder, 2);
    const int kx = x0 + int(std::lround(std::clamp(frac, 0.0f, 1.0f) * float(x1 - x0)));
    fillAlpha(dc, {x0, cy - scale(2), kx, cy + scale(2)}, enabled ? kAccent : kFaint, 255);
    const RECT knob{kx - scale(7), cy - scale(7), kx + scale(7), cy + scale(7)};
    smoothRoundBox(dc, knob, float(scale(7)), !enabled ? kFaint : over ? kAccentBright : kAccent, 2.0f);
}
// A native trackbar (msctls_trackbar32) drawn as the kit's slider: the parent answers NM_CUSTOMDRAW with this.
LRESULT trackbarCustomDraw(const NMCUSTOMDRAW* cd) {
    if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (cd->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    if (cd->dwItemSpec != TBCD_CHANNEL) return CDRF_SKIPDEFAULT;  // the thumb is drawn with the channel; no ticks
    HWND w = cd->hdr.hwndFrom;
    RECT rc, ch, th;
    GetClientRect(w, &rc);
    SendMessageW(w, TBM_GETCHANNELRECT, 0, LPARAM(&ch));
    SendMessageW(w, TBM_GETTHUMBRECT, 0, LPARAM(&th));
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(cd->hdc, &rc, bg);
    DeleteObject(bg);
    const int tw = th.right - th.left, x0 = ch.left + tw / 2, x1 = ch.right - tw / 2, kx = (th.left + th.right) / 2;
    const float frac = float(kx - x0) / float(std::max(1, x1 - x0));
    sliderFace(cd->hdc, x0, x1, (rc.top + rc.bottom) / 2, frac, (cd->uItemState & CDIS_HOT) || hotIn(w, rc), IsWindowEnabled(w) != 0);
    return CDRF_SKIPDEFAULT;
}

// --- Tables ---------------------------------------------------------------------------------------------------------
// Rows with columns: a header (names in the dim color), rows with hairlines between, a frame around all.
struct Column {
    std::wstring name;
    int width = 0;  // 0: what the others leave
    UINT align = DT_LEFT;
};
// Where column `k` is in a row.
RECT tableCell(RECT row, const std::vector<Column>& cols, size_t k) {
    int fixed = 0, flexible = 0;
    for (const auto& c : cols) c.width ? fixed += scale(c.width) : ++flexible;
    const int rest = std::max(0, int(row.right - row.left) - scale(12) - fixed);
    int x = row.left + scale(6);
    for (size_t i = 0; i < cols.size(); ++i) {
        const int w = cols[i].width ? scale(cols[i].width) : rest / std::max(1, flexible);
        if (i == k) return {x, row.top, x + w, row.bottom};
        x += w;
    }
    return {x, row.top, x, row.bottom};
}
HFONT smallFont();  // (below)
// The header: the column names small and dim, one line under them (no band, no frame).
void tableHeader(HDC dc, RECT row, const std::vector<Column>& cols) {
    SetTextColor(dc, kDim);
    HGDIOBJ of = SelectObject(dc, smallFont());
    for (size_t k = 0; k < cols.size(); ++k) {
        RECT c = tableCell(row, cols, k);
        InflateRect(&c, -scale(4), 0);
        DrawTextW(dc, cols[k].name.c_str(), -1, &c, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | cols[k].align);
    }
    SelectObject(dc, of);
    fillAlpha(dc, {row.left, row.bottom - 1, row.right, row.bottom}, kBorder, 255);
}
// A row: on the page's ground; selected or under the mouse a rounded pill (as the lists and pickers).
void tableRow(HDC dc, RECT row, bool selected, bool over) {
    const RECT p{row.left + scale(2), row.top + scale(1), row.right - scale(2), row.bottom - scale(1)};
    if (selected) box(dc, p, kSel, kSel, 6);
    else if (over) box(dc, p, kRowHot, kRowHot, 6);
}
// One cell's text.
void tableText(HDC dc, RECT row, const std::vector<Column>& cols, size_t k, const std::wstring& text, COLORREF color) {
    RECT c = tableCell(row, cols, k);
    InflateRect(&c, -scale(4), 0);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &c, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | cols[k].align);
}
void tableFrame(HDC, RECT) {}  // no frame: the header's line and the rows' pills are enough

// --- Lists that can be reordered ------------------------------------------------------------------------------------
// The grip (six dots) on a row that can be dragged; the line where a dragged row goes.
void grip(HDC dc, int x, int y, bool on) {
    for (int gx = 0; gx < 2; ++gx)
        for (int gy = 0; gy < 3; ++gy) {
            const int dx = x + gx * scale(5), dy = y + gy * scale(5);
            fillAlpha(dc, {dx, dy, dx + scale(2), dy + scale(2)}, on ? kAccent : kGripDot, 255);
        }
}
void dropLine(HDC dc, int x0, int x1, int y) { fillAlpha(dc, {x0, y - scale(1), x1, y + scale(1)}, kAccent, 255); }

// A list being reordered: which list, the row taken and where it goes now.
struct Reorder {
    std::string list;
    int from = -1, to = -1;
};
inline Reorder gReorder;
inline std::map<std::string, std::vector<RECT>> gReorderRows;  // this paint's rows of each list
// A row of a list that can be reordered (`index` from 0, in order): its grip at the left, shown on the row under
// the mouse; dragging the grip moves the row, and `moved(from, to)` gets where it was and where it goes (an index
// in the list without it). The caller paints the row first; the grip's drag is on top of the row's click.
void reorderRow(HDC dc, const std::string& list, RECT row, int index, std::function<void(int from, int to)> moved, bool can = true) {
    auto& rows = gReorderRows[list];
    if (index == 0) rows.clear();
    rows.push_back(row);
    const bool dragging = gReorder.list == list && gReorder.from >= 0;
    if (dragging && gReorder.from == index) fillAlpha(dc, row, kSel, 160);
    const RECT g{row.left, row.top, row.left + scale(18), row.bottom};
    if (can && (hot(row) || (dragging && gReorder.from == index))) grip(dc, row.left + scale(5), (row.top + row.bottom) / 2 - scale(6), dragging && gReorder.from == index);
    if (!can) return;
    hitsOut().push_back({g,
                         [list, index](POINT pt) {
                             const auto& rs = gReorderRows[list];
                             int to = int(rs.size());
                             for (size_t i = 0; i < rs.size(); ++i)
                                 if (pt.y < (rs[i].top + rs[i].bottom) / 2) {
                                     to = int(i);
                                     break;
                                 }
                             gReorder = {list, index, to};
                         },
                         nullptr, L"Drag to move", IDC_SIZEALL, {},
                         [moved] {
                             const Reorder r = std::exchange(gReorder, {});
                             if (r.from < 0 || r.to == r.from || r.to == r.from + 1) return;
                             moved(r.from, r.to > r.from ? r.to - 1 : r.to);
                         }});
}
// After the list's rows: the line where the dragged row goes.
void reorderDropLine(HDC dc, const std::string& list) {
    if (gReorder.list != list || gReorder.from < 0) return;
    const auto& rs = gReorderRows[list];
    if (rs.empty()) return;
    const int y = gReorder.to < int(rs.size()) ? rs[size_t(gReorder.to)].top : rs.back().bottom;
    dropLine(dc, rs.front().left, rs.front().right, y);
}

// --- Check boxes --------------------------------------------------------------------------------------------------
// The box (painted check boxes and the native ones alike).
void checkMark(HDC dc, RECT b, bool on, bool over, bool enabled) {
    box(dc, b, on ? kAccentFill : kCtl, on ? kAccent : over ? kBorderHot : kBorderStrong, 3);
    if (!on) return;
    HPEN p = CreatePen(PS_SOLID, std::max(1, scale(2)), enabled ? kOnText : kDim);
    HGDIOBJ op = SelectObject(dc, p);
    const int w = b.right - b.left, h = b.bottom - b.top;
    MoveToEx(dc, b.left + w * 22 / 100, b.top + h * 52 / 100, nullptr);
    LineTo(dc, b.left + w * 42 / 100, b.top + h * 72 / 100);
    LineTo(dc, b.left + w * 78 / 100, b.top + h * 30 / 100);
    SelectObject(dc, op);
    DeleteObject(p);
}
void radioMark(HDC dc, RECT b, bool on, bool over) {
    HBRUSH br = CreateSolidBrush(kCtl);
    HPEN p = CreatePen(PS_SOLID, 1, on ? kAccent : over ? kBorderHot : kBorderStrong);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, p);
    Ellipse(dc, b.left, b.top, b.right, b.bottom);
    SelectObject(dc, ob), SelectObject(dc, op);
    DeleteObject(br), DeleteObject(p);
    if (!on) return;
    const int in = (b.right - b.left) * 3 / 10;
    HBRUSH dot = CreateSolidBrush(kAccent);
    ob = SelectObject(dc, dot), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, b.left + in, b.top + in, b.right - in + 1, b.bottom - in + 1);
    SelectObject(dc, ob), SelectObject(dc, op);
    DeleteObject(dot);
}
// A check box with its label; the whole is the click area. Returns the right edge.
int check(HDC dc, RECT r, const std::wstring& label, bool on, std::function<void()> click, const std::wstring& tip = L"", bool changed = false, bool enabled = true) {
    const int s = scale(14);
    RECT b{r.left, (r.top + r.bottom - s) / 2, r.left + s, (r.top + r.bottom - s) / 2 + s};
    const bool over = enabled && hot(r);
    checkMark(dc, b, on, over, enabled);
    RECT t{b.right + scale(8), r.top, r.right, r.bottom};
    SetTextColor(dc, !enabled ? kFaint : changed ? kGood : on || over ? kText : kDim);
    DrawTextW(dc, label.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    hitsOut().push_back({r, nullptr, enabled ? std::move(click) : nullptr, tip, enabled ? IDC_HAND : IDC_ARROW, {}});
    return t.left + textWidth(dc, label);
}

// --- Fields -------------------------------------------------------------------------------------------------------
enum class Field { Normal, Hot, Focus, Changed, ReadOnly };
// A text field's box (painted fields, and the frame drawn around native edit boxes).
void fieldBox(HDC dc, RECT r, Field f) {
    COLORREF fill = kCtl, line = kBorder;
    if (f == Field::Hot) line = kBorderHot;
    if (f == Field::Focus) line = kAccent;
    if (f == Field::Changed) fill = kGoodFill, line = kGoodLine;
    if (f == Field::ReadOnly) fill = kReadOnly, line = kReadOnlyLine;
    box(dc, r, fill, line);
}

// --- Form parts ---------------------------------------------------------------------------------------------------
// The form's layout, as the Data forms have it: the fields in a column on the left, the pictures beside them when
// the form is wide (above them when narrow).
struct FormLayout {
    int x0, labelW, rowH, colW, colRight, picX, picW;
    bool twoCol;
};
FormLayout formLayout(RECT rc) {
    FormLayout l{};
    l.x0 = scale(18), l.labelW = scale(230), l.rowH = scale(kRowH);
    const int full = std::max(scale(300), int(rc.right) - 2 * l.x0);
    l.twoCol = full >= scale(1150);
    l.colW = l.twoCol ? std::min(scale(650), full * 11 / 20) : full;
    l.colRight = l.x0 + l.colW;
    l.picX = l.twoCol ? l.colRight + scale(28) : l.x0;
    l.picW = l.twoCol ? full - l.colW - scale(28) : full;
    return l;
}
HFONT titleFont() {
    static std::map<int, HFONT> fonts;  // by DPI
    HFONT& f = fonts[scale(100)];
    if (!f) {
        LOGFONTW lf{};
        GetObjectW(app.font, sizeof lf, &lf);
        lf.lfHeight = lf.lfHeight * 3 / 2;
        f = CreateFontIndirectW(&lf);
    }
    return f;
}
// A title: big text, and a line under it in the dim color ("stat_txt.tbl string 12  ·  changed  ·  Reset").
// `kind`: the icon when there is no picture (the .dat of the entry).
int title(HDC dc, int x, int y, int right, const UnitPalette::Bitmap* icon, const std::wstring& text, const std::wstring& sub, bool changed = false,
          std::function<void()> reset = nullptr, std::optional<UiIcon> kind = std::nullopt) {
    int tx = x;
    if (icon && icon->bmp) {
        drawFitted(dc, *icon, {x, y, x + scale(40), y + scale(40)});
        tx += scale(52);
    } else if (kind) {
        uiIcon(dc, {x, y, x + scale(40), y + scale(40)}, *kind, kDim, scale(32));
        tx += scale(52);
    }
    HGDIOBJ of = SelectObject(dc, titleFont());
    RECT tt{tx, y, right, y + scale(26)};
    SetTextColor(dc, kText);
    DrawTextW(dc, (text.empty() ? L"(empty)" : text).c_str(), -1, &tt, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);
    if (sub.empty() && !changed) return y + scale(40);
    RECT st{tx, y + scale(24), right, y + scale(42)};
    const std::wstring lineText = sub + (changed ? (sub.empty() ? L"changed  \x00B7  " : L"  \x00B7  changed  \x00B7  ") : L"");
    SetTextColor(dc, kDim);
    DrawTextW(dc, lineText.c_str(), -1, &st, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (changed && reset) link(dc, st.left + textWidth(dc, lineText), st.top, st.bottom - st.top, L"Reset", std::move(reset), L"Put back the game's values");
    return y + scale(56);
}
// A section heading: its name and a line to the column's edge.
int sectionHeader(HDC dc, const FormLayout& l, int y, const std::wstring& name, bool changed = false) {
    SetTextColor(dc, changed ? kGood : kText);
    RECT hr{l.x0, y, l.colRight, y + l.rowH};
    DrawTextW(dc, name.c_str(), -1, &hr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    fillAlpha(dc, {l.x0, y + l.rowH - 1, l.colRight, y + l.rowH}, kBorder, 255);
    return y + l.rowH + scale(6);
}
// A field's label: dim; green when its value is changed; faint when it cannot be changed.
void fieldLabel(HDC dc, const FormLayout& l, int y, const std::wstring& name, bool changed, bool enabled = true) {
    RECT lab{l.x0, y, l.x0 + l.labelW, y + l.rowH};
    SetTextColor(dc, !enabled ? kFaint : changed ? kGood : kDim);
    DrawTextW(dc, name.c_str(), -1, &lab, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}
HFONT smallFont() {
    static std::map<int, HFONT> fonts;  // by DPI
    HFONT& f = fonts[scale(100)];
    if (!f) {
        LOGFONTW lf{};
        GetObjectW(app.font, sizeof lf, &lf);
        lf.lfHeight = lf.lfHeight * 5 / 6;
        f = CreateFontIndirectW(&lf);
    }
    return f;
}

// --- The References side bar ----------------------------------------------------------------------------------------
// What uses the selected thing, by group ("UNITS", "BRIEFINGS"), each a link to it. The callers keep their own
// click areas: these paint and say where.
void sidePanel(HDC dc, RECT side) {
    fillAlpha(dc, side, kPanel, 255);
    fillAlpha(dc, {side.left, side.top, side.left + 1, side.bottom}, kBorder, 255);
}
int sideHeader(HDC dc, int x, int y, int w, const std::wstring& title, size_t count) {
    RECT h{x, y, x + w, y + scale(22)};
    SetTextColor(dc, kText);
    DrawTextW(dc, title.c_str(), -1, &h, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(dc, kDim);
    DrawTextW(dc, std::to_wstring(count).c_str(), -1, &h, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    y += scale(30);
    if (!count) {
        RECT n{x, y, x + w, y + scale(20)};
        DrawTextW(dc, L"No other data uses this entry", -1, &n, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        y += scale(22);
    }
    return y;
}
// `icon`: the group's icon before its name (-1: none).
int sideGroup(HDC dc, int x, int y, int w, const std::wstring& name, bool first, std::optional<UiIcon> icon = std::nullopt) {
    if (!first) y += scale(8);
    const std::wstring& up = name;  // sentence case, as every heading (PRINCIPLES.md, "Text")
    HGDIOBJ of = SelectObject(dc, smallFont());
    SetTextColor(dc, kDim);
    if (icon) {
        uiIcon(dc, {x, y - scale(1), x + scale(16), y + scale(16)}, *icon, kDim, scale(16));
        x += scale(22), w -= scale(22);
    }
    RECT r{x, y, x + w, y + scale(15)};
    DrawTextW(dc, up.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);
    return y + scale(18);
}
// One reference: its icon and name in the accent; returns its row (the caller's click area).
RECT sideLink(HDC dc, int x, int y, int w, const std::wstring& text, const UnitPalette::Bitmap* icon, bool over) {
    const int h = scale(20);
    int tx = x;
    if (icon && icon->bmp) {
        drawFitted(dc, *icon, {x, y + scale(1), x + scale(18), y + h - scale(1)});
        tx += scale(22);
    }
    RECT nr{tx, y, x + w, y + h};
    SetTextColor(dc, over ? kAccentBright : kAccent);
    DrawTextW(dc, text.c_str(), -1, &nr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (over) {
        const int tw = std::min<int>(textWidth(dc, text), int(nr.right - nr.left));
        line(dc, tx, nr.bottom - scale(3), tx + tw, nr.bottom - scale(3), kAccentBright);
    }
    return {x, y, x + w, y + h};
}

// --- Pickers ------------------------------------------------------------------------------------------------------
// A value in a sentence (a trigger's block, a briefing action): a chip to click. Dim: the block is turned off;
// bad: the value must be fixed; focused: Tab moved to it.
void chipFace(HDC dc, RECT r, bool over, bool dim = false, bool bad = false, bool focused = false) {
    const COLORREF fill = dim ? kChipDim : bad ? (over ? kBadFillHot : kBadFill) : over ? kSelHot : kChip;
    const COLORREF line = focused ? kAccent : dim ? kReadOnlyLine : bad ? kBad : over ? kAccentLine : kChipLine;
    if (focused) {
        box(dc, r, fill, CLR_NONE);
        HPEN p = CreatePen(PS_SOLID, 2, line);
        HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, r.left + 1, r.top + 1, r.right, r.bottom, 2 * scale(kRadius), 2 * scale(kRadius));
        SelectObject(dc, op), SelectObject(dc, ob);
        DeleteObject(p);
        return;
    }
    box(dc, r, fill, line);
}

// A dropdown: icon, name, the number on the right, the arrow; and the glyph that opens the entry when `open` is
// set. Clicking the box calls `click` with it.
// The dropdown's face, for surfaces with their own click areas (the Data form).
void pickFace(HDC dc, RECT r, const std::wstring& text, bool changed, bool over, const UnitPalette::Bitmap* icon = nullptr, int number = -1, bool enabled = true) {
    fieldBox(dc, r, !enabled ? Field::ReadOnly : changed ? Field::Changed : over ? Field::Hot : Field::Normal);
    int tx = r.left + scale(8);
    if (icon && icon->bmp) {
        drawFitted(dc, *icon, {tx - scale(2), r.top + scale(1), tx + scale(18), r.bottom - scale(1)});
        tx += scale(24);
    }
    RECT t{tx, r.top, r.right - (number >= 0 ? scale(56) : scale(22)), r.bottom};
    SetTextColor(dc, !enabled ? kFaint : changed ? kGood : kText);
    DrawTextW(dc, text.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(dc, kDim);
    if (number >= 0) {
        RECT num{r.right - scale(64), r.top, r.right - scale(22), r.bottom};
        DrawTextW(dc, std::to_wstring(number).c_str(), -1, &num, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    }
    if (enabled) glyph(dc, {r.right - scale(20), r.top, r.right - scale(6), r.bottom}, UiIcon::Dropdown, over ? kText : kDim, 9);  // chevron down
}
void pickBox(HDC dc, RECT r, const std::wstring& text, bool changed, std::function<void(RECT)> click, const std::wstring& tip = L"",
             const UnitPalette::Bitmap* icon = nullptr, int number = -1, std::function<void()> open = nullptr, bool enabled = true) {
    const bool over = enabled && hot(r);
    pickFace(dc, r, text, changed, over, icon, number, enabled);
    if (enabled) {
        DataWidgetHit h{r, nullptr, [r, click] { click(r); }, tip, IDC_HAND, {}};
        h.wheel = [r, click](int dir) {  // the mouse wheel: the next or the previous choice, without the list
            gListWheel = dir;
            click(r);
            gListWheel = 0;
        };
        hitsOut().push_back(std::move(h));
    }
    else hitsOut().push_back({r, nullptr, nullptr, tip, IDC_NO, {}});
    if (open) iconButton(dc, {r.right + scale(4), r.top, r.right + scale(4 + kIconBox), r.bottom}, UiIcon::OpenExternal, std::move(open), L"Open it", Icon::Bare, false, true, Tone::Primary);
}
POINT belowBox(RECT r) {
    POINT pt{r.left, r.bottom + scale(2)};
    ClientToScreen(hitsWnd(), &pt);
    return pt;
}

// --- Painted surfaces ---------------------------------------------------------------------------------------------
// A window painted with the kit: its click areas, the tooltips for them, the part under the mouse (a new part
// repaints the window), the cursor. A page calls begin() and end() around its paint, and message() first in
// its window procedure.
struct Surface {
    std::vector<DataWidgetHit> hits;
    HWND tips = nullptr;
    int tipCount = 0;
    RECT over{};
    struct {
        std::function<void(POINT)> drag;
        std::function<void()> click, release;
        bool down = false, moved = false;
    } grab;  // a part with a drag, while the mouse is down on it
    std::vector<DataWidgetHit>* savedOut = nullptr;
    HWND savedWnd = nullptr;

    HWND tipsFor = nullptr;  // the window the tooltips are for
    // The tooltip window is owned by the top window (a child cannot own it), so it outlives the surface's window:
    // it goes when the surface paints another window, and when the surface goes.
    void releaseTips() {
        if (tips && IsWindow(tips)) DestroyWindow(tips);
        tips = nullptr, tipCount = 0, tipsFor = nullptr;
    }
    Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface& operator=(Surface&& o) noexcept {  // (a page's host made again: the old tooltips go)
        if (this != &o) {
            releaseTips();
            hits = std::move(o.hits), tips = std::exchange(o.tips, nullptr), tipCount = o.tipCount, over = o.over, tipsFor = o.tipsFor;
            grab = std::move(o.grab), savedOut = o.savedOut, savedWnd = o.savedWnd;
        }
        return *this;
    }
    ~Surface() { releaseTips(); }
    void begin(HWND w) {
        if (tips && tipsFor != w) releaseTips();
        for (int i = 0; tips && i < tipCount; ++i) {
            TOOLINFOW ti{sizeof ti};
            ti.hwnd = w, ti.uId = UINT_PTR(i + 1);
            SendMessageW(tips, TTM_DELTOOLW, 0, LPARAM(&ti));
        }
        tipCount = 0;
        hits.clear();
        savedOut = gHitsOut, savedWnd = gHitsWnd;
        gHitsOut = &hits, gHitsWnd = w;
        gHitsOf[w] = &hits;
    }
    void end(HWND w) {
        gHitsOut = savedOut, gHitsWnd = savedWnd;
        if (!tips) {
            tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, w, nullptr, app.inst, nullptr);
            tipsFor = w;
            SendMessageW(tips, TTM_SETMAXTIPWIDTH, 0, scale(360));
            styleTips(tips);
        }
        for (auto& h : hits) {
            if (h.tip.empty()) continue;
            TOOLINFOW ti{sizeof ti};
            ti.uFlags = TTF_SUBCLASS;
            ti.hwnd = w, ti.uId = UINT_PTR(++tipCount), ti.rect = h.r;
            ti.lpszText = h.tip.data();
            SendMessageW(tips, TTM_ADDTOOLW, 0, LPARAM(&ti));
        }
    }
    const DataWidgetHit* at(POINT pt) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (PtInRect(&it->r, pt) && (it->click || it->drag)) return &*it;
        return nullptr;
    }
    // The mouse and the cursor; true when the message is handled (`result` is the answer). A click or a drag runs with
    // this window as hitsWnd(), so that a picker opens below the box in this window (belowBox).
    bool message(HWND w, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
        struct AsHitsWnd {
            HWND saved;
            explicit AsHitsWnd(HWND w) : saved(gHitsWnd) { gHitsWnd = w; }
            ~AsHitsWnd() { gHitsWnd = saved; }
        };
        switch (msg) {
        case WM_FEEDBACK_NAME: {  // feedback on a painted part: its name, else its tip
            auto& q = *reinterpret_cast<FeedbackQuery*>(lp);
            for (auto it = hits.rbegin(); it != hits.rend(); ++it)
                if (PtInRect(&it->r, q.pt) && (!it->name.empty() || !it->tip.empty())) {
                    q.name = !it->name.empty() ? it->name : it->tip;
                    result = 0;
                    return true;
                }
            return false;
        }
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: {
            AsHitsWnd as(w);
            return mouse(w, msg, wp, lp, result);
        }
        }
        return mouse(w, msg, wp, lp, result);
    }
    bool mouse(HWND w, UINT msg, WPARAM, LPARAM lp, LRESULT& result) {
        switch (msg) {
        case WM_MOUSEMOVE: {
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (grab.down && grab.drag) {
                grab.moved = true, grab.drag(pt);
                InvalidateRect(w, nullptr, FALSE);
                return result = 0, true;
            }
            RECT part{};
            for (auto it = hits.rbegin(); it != hits.rend(); ++it)
                if (PtInRect(&it->r, pt)) {
                    part = it->r;
                    break;
                }
            if (!EqualRect(&part, &over)) {
                over = part, InvalidateRect(w, nullptr, FALSE);
                TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, w, 0};
                TrackMouseEvent(&tme);
            }
            return result = 0, true;
        }
        case WM_MOUSELEAVE: over = {}, InvalidateRect(w, nullptr, FALSE); return result = 0, true;
        case WM_LBUTTONDOWN: {
            SetFocus(w);
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const DataWidgetHit* h = at(pt);
            if (h && h->drag) {  // a drag: until the mouse goes up (a click if it does not move)
                grab.drag = h->drag, grab.click = h->click, grab.release = h->release, grab.down = true, grab.moved = false;
                SetCapture(w);
                if (!grab.click) grab.drag(pt);
            } else if (h && h->click) {
                auto click = h->click;  // the click may repaint, which clears the hits
                click();
            }
            return result = 0, true;
        }
        case WM_LBUTTONUP: {
            if (!grab.down) return false;
            ReleaseCapture();
            auto g = std::move(grab);
            grab = {};
            if (g.moved && g.release) g.release();
            else if (!g.moved && g.click) g.click();
            InvalidateRect(w, nullptr, FALSE);
            return result = 0, true;
        }
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(w, &pt);
            for (auto it = hits.rbegin(); it != hits.rend(); ++it)
                if (PtInRect(&it->r, pt)) return SetCursor(LoadCursor(nullptr, it->cursor)), result = TRUE, true;
            return false;
        }
        }
        return false;
    }
};

// --- Pop-outs -------------------------------------------------------------------------------------------------------
// A pop-out window's frame: rounded corners and a shadow where Windows draws them (11), the border color.
void popupFrame(HWND w) {
    enum { kCorner = 33, kBorderColor = 34 };  // DWMWA_WINDOW_CORNER_PREFERENCE, DWMWA_BORDER_COLOR
    const int round = 3;                         // DWMWCP_ROUNDSMALL
    DwmSetWindowAttribute(w, kCorner, &round, sizeof round);
    const COLORREF line = kBorderHot;
    DwmSetWindowAttribute(w, kBorderColor, &line, sizeof line);
}

}  // namespace ui

// The kit under the names the forms have used.
using ui::FormLayout;
using ui::formLayout;
using ui::sectionHeader;
using ui::fieldLabel;
using ui::pickBox;
using ui::belowBox;
inline bool mouseIn(RECT r) { return ui::hot(r); }
// A link in the accent color (or another tone); returns its right edge.
int linkButton(HDC dc, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"", ui::Tone tone = ui::Tone::Normal) {
    return ui::smallButton(dc, x, y, h, text, std::move(click), tip, tone);  // an action: a small button
}
// A square button with an icon glyph.
void glyphButton(HDC dc, RECT r, ui::Ico glyph, bool on, std::function<void()> click, std::wstring tip) {
    ui::iconButton(dc, r, glyph, std::move(click), tip, ui::Icon::Boxed, on);
}
// The remove action of a row, shown on the row under the mouse.
void removeCross(HDC dc, RECT row, RECT at, std::function<void()> click, const std::wstring& tip) {
    hitsOut().push_back({row, nullptr, nullptr, L"", IDC_ARROW, {}});
    if (!ui::hot(row)) return;
    ui::iconButton(dc, at, UiIcon::Close, std::move(click), tip, ui::Icon::Bare, false, true, ui::Tone::Danger);
}
