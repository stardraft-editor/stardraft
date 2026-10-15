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
void box(Paint p, RECT r, COLORREF fill, COLORREF line, int radius = kRadius) { p->roundBox(r, fill, line, float(scale(radius))); }

int textWidth(Paint p, const std::wstring& t) { return p->measure(t); }

// --- Buttons ----------------------------------------------------------------------------------------------------
// Normal: a command. Primary: the one a form or dialog is for (OK, Create, Build). Danger: removes something.
using kitpaint::Tone;  // (core/kit_paint.inc: the tones are the core's)
using kitpaint::State;  // (core/kit_paint.inc: Normal, Hot, Down, Disabled)

using kitpaint::toneText;

// The face of a button (painted ones and the native push buttons alike): core/kit_paint.inc's, in `font` when given.
void buttonFace(Paint p, RECT r, const std::wstring& text, Tone tone, State s, HFONT font = nullptr) {
    const Font of = font ? p->setFont(fontOf(font)) : Font{};
    kitpaint::drawButtonFace(*p, r, text, tone, s);
    if (of) p->setFont(of);
}
// A button's width for its text.
int buttonWidth(Paint p, const std::wstring& text) { return std::max(scale(kButtonMinW), textWidth(*p, text) + scale(28)); }

void button(Paint p, RECT r, const std::wstring& text, Tone tone, std::function<void()> click, const std::wstring& tip = L"", bool enabled = true) {
    buttonFace(*p, r, text, tone, !enabled ? State::Disabled : hot(r) ? State::Hot : State::Normal);
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
int buttonsRight(Paint p, int right, int y, const std::vector<ButtonSpec>& bs) {
    int x = right;
    for (auto it = bs.rbegin(); it != bs.rend(); ++it) {
        const int w = buttonWidth(*p, it->text);
        button(*p, {x - w, y, x, y + scale(kButtonH)}, it->text, it->tone, it->click, it->tip);
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
int smallButton(Paint p, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"",
                Tone tone = Tone::Normal, bool enabled = true) {
    const int bh = std::min(h, scale(24)), w = textWidth(*p, text) + scale(20);
    const RECT r{x, y + (h - bh) / 2, x + w, y + (h - bh) / 2 + bh};
    const Tone t = tone == Tone::Primary ? Tone::Normal : tone;  // a small button is never the main action
    buttonFace(*p, r, text, t, !enabled ? State::Disabled : hot(r) ? State::Hot : State::Normal);
    hitsOut().push_back({r, nullptr, enabled ? std::move(click) : nullptr, tip, enabled ? IDC_HAND : IDC_ARROW, {}});
    return r.right;
}

// "Add ..." under a list (blocks, rows, triggers): a row as wide as the list, so it is clear where the new item
// goes. A quiet outline, a + and the text in dim; under the mouse the row's ground and the accent.
void addRowFace(Paint p, RECT r, const std::wstring& text, bool over, bool enabled = true) {
    box(*p, r, over && enabled ? kRowHot : CLR_NONE, over && enabled ? kBorderHot : kBorder, 6);
    const int is = scale(16);
    const COLORREF c = !enabled ? kFaint : over ? kAccent : kDim;
    const RECT ir{r.left + scale(10), (r.top + r.bottom - is) / 2, r.left + scale(10) + is, (r.top + r.bottom + is) / 2};
    p->icon(ir, int(UiIcon::Add), c, is);
    p->text({ir.right + scale(8), r.top, r.right - scale(8), r.bottom}, text, c, kMiddle | kEllipsis);
}
// The row, with its click. Returns the bottom. Not enabled (a limit is reached): gray, no click; the tooltip says why.
int addRow(Paint p, RECT r, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"", bool enabled = true) {
    addRowFace(*p, r, text, enabled && hot(r), enabled);
    hitsOut().push_back({r, nullptr, enabled ? std::move(click) : nullptr, tip, enabled ? IDC_HAND : IDC_ARROW, {}});
    return r.bottom;
}

// A link: only to another entity (a unit, a string, a file, a web page); its text is the entity's name. Accent;
// underlined under the mouse. Returns the right edge.
int link(Paint p, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"", Tone tone = Tone::Primary) {
    const int w = textWidth(*p, text);
    RECT r{x, y, x + w, y + h};
    const bool over = hot(r);
    kitpaint::drawLinkFace(*p, r, text, tone, over);
    hitsOut().push_back({r, nullptr, std::move(click), tip, IDC_HAND, {}});
    return r.right;
}

// --- Icon buttons -----------------------------------------------------------------------------------------------
// Bare: a glyph that lights up under the mouse (in rows, beside fields). Boxed: a glyph in a button box (players,
// tool bars); `on` marks a toggle that is on.
enum class Icon { Bare, Boxed };

// The largest square that fits in `r`, centered: every icon button is square, whatever place it gets.
RECT square(RECT r) { return toRECT(kitpaint::square(r)); }

// An icon: one of the UI icons (tools/icongen), or a Segoe MDL2 glyph where the icon set has none yet (core/kit_paint.inc).
using kitpaint::Ico;
void glyph(Paint p, RECT r, Ico g, COLORREF c, int size = kGlyph) { kitpaint::drawGlyph(*p, r, g, c, size); }

// A bare icon button's face, for surfaces with their own click areas (the triggers pane). `on` with Tone::Quiet:
// a toggle that is off (amber, as a block that is turned off).
void iconFace(Paint p, RECT r, Ico g, Tone tone, bool on, bool over, bool enabled = true) { kitpaint::drawIconFace(*p, r, g, tone, on, over, enabled); }
void iconButton(Paint p, RECT r, Ico g, std::function<void()> click, const std::wstring& tip, Icon style = Icon::Bare, bool on = false,
                bool enabled = true, Tone tone = Tone::Normal) {
    r = square(r);
    const bool over = enabled && hot(r);
    if (style == Icon::Boxed) {  // flat: a fill only when on (the accent) or under the mouse
        if (on) box(*p, r, kSel, kAccentLine);
        else if (over) box(*p, r, kCtlHot, CLR_NONE);
        glyph(*p, r, g, !enabled ? kFaint : on ? kAccentBright : tone == Tone::Danger ? kBad : over ? kText : kDim, 14);
    } else {
        iconFace(*p, r, g, tone, on, over, enabled);
    }
    hitsOut().push_back({r, nullptr, enabled ? std::move(click) : nullptr, tip, enabled ? IDC_HAND : IDC_ARROW, {}});
}

// --- Rows -------------------------------------------------------------------------------------------------------
// A list row's background: selected (fill and a bar on the left), under the mouse, or none.
void listRow(Paint p, RECT r, bool selected, bool over) {
    if (selected) {
        p->fill(r, kSel);
        p->fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
    } else if (over) {
        p->fill(r, kRowHot);
    }
}
COLORREF rowText(bool selected) { return selected ? kAccent : kText; }
// A row of an owner-drawn list box (pickers, the scripts list): the list's face, then the row.
void ownerRow(Paint p, RECT r, bool selected, COLORREF face = kCtl) {
    p->fill(r, face);
    listRow(*p, r, selected, false);
    // The list's frame (paintDarkBorder) covers its outer 2 px: the accent bar goes just inside it.
    const int ring = scale(1) + 1;
    if (selected) p->fill({r.left + ring, r.top, r.left + ring + scale(2), r.bottom}, kAccent);
}
// The mark of a changed entry: a small green dot.
void changedDot(Paint p, int cx, int cy) { kitpaint::drawChangedDot(*p, cx, cy); }

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
int rowActions(Paint p, RECT row, const std::vector<RowAction>& actions, bool show) {
    hitsOut().push_back({row, nullptr, nullptr, L"", IDC_ARROW, {}});  // the mouse entering the row repaints it
    const int w = scale(kIconBox), h = std::min<int>(row.bottom - row.top, scale(kIconBox));
    int x = row.right - scale(4) - w * int(actions.size());
    const int left = x;
    if (!show && !hot(row)) return left;
    const int y = (row.top + row.bottom - h) / 2;
    for (const auto& a : actions) {
        iconButton(*p, {x, y, x + w, y + h}, a.glyph, a.click, a.tip, Icon::Bare, a.on, a.enabled, a.tone);
        x += w;
    }
    return left;
}

// A painted scroll bar (for lists that page by rows): the track, and the thumb, lighter under the mouse or while
// it is dragged.
// As Windows 11's: a thin line at rest, wider with its track under the mouse or while dragged.
void scrollFace(Paint p, RECT track, RECT thumb, bool lit) {
    if (lit) p->fill(track, kPanel);
    else thumb.left = thumb.right - std::max(2, int(thumb.right - thumb.left) / 2);
    box(*p, thumb, lit ? kThumbHot : kThumb, CLR_NONE, 3);
}

// --- Sliders --------------------------------------------------------------------------------------------------------
// A slider from x0 to x1 on the line cy: the track (kBorder line), the part up to the value in the accent, a round
// thumb in the accent (lighter under the mouse). `frac`: the value, 0 to 1.
void sliderFace(Paint p, int x0, int x1, int cy, float frac, bool over, bool enabled = true) {
    const RECT track{x0, cy - scale(2), x1, cy + scale(2)};
    box(*p, track, kCtl, kBorder, 2);
    const int kx = x0 + int(std::lround(std::clamp(frac, 0.0f, 1.0f) * float(x1 - x0)));
    p->fill({x0, cy - scale(2), kx, cy + scale(2)}, enabled ? kAccent : kFaint);
    const RECT knob{kx - scale(7), cy - scale(7), kx + scale(7), cy + scale(7)};
    p->smoothRoundOutline(knob, float(scale(7)), !enabled ? kFaint : over ? kAccentBright : kAccent, 2.0f);
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
unsigned alignFlags(UINT dt) { return (dt & DT_RIGHT ? kRight : dt & DT_CENTER ? kCenter : kLeft) | kMiddle; }
void tableHeader(Paint p, RECT row, const std::vector<Column>& cols) {
    const Font of = p->setFont(fontOf(smallFont()));
    for (size_t k = 0; k < cols.size(); ++k) {
        RECT c = tableCell(row, cols, k);
        InflateRect(&c, -scale(4), 0);
        p->text(c, cols[k].name, kDim, alignFlags(cols[k].align));
    }
    p->setFont(of);
    p->fill({row.left, row.bottom - 1, row.right, row.bottom}, kBorder);
}
// A row: on the page's ground; selected or under the mouse a rounded pill (as the lists and pickers).
void tableRow(Paint p, RECT row, bool selected, bool over) {
    const RECT pill{row.left + scale(2), row.top + scale(1), row.right - scale(2), row.bottom - scale(1)};
    if (selected) box(*p, pill, kSel, kSel, 6);
    else if (over) box(*p, pill, kRowHot, kRowHot, 6);
}
// One cell's text.
void tableText(Paint p, RECT row, const std::vector<Column>& cols, size_t k, const std::wstring& text, COLORREF color) {
    RECT c = tableCell(row, cols, k);
    InflateRect(&c, -scale(4), 0);
    p->text(c, text, color, alignFlags(cols[k].align) | kEllipsis);
}
void tableFrame(Paint, RECT) {}  // no frame: the header's line and the rows' pills are enough

// --- Lists that can be reordered ------------------------------------------------------------------------------------
// The grip (six dots) on a row that can be dragged; the line where a dragged row goes.
void grip(Paint p, int x, int y, bool on) {
    for (int gx = 0; gx < 2; ++gx)
        for (int gy = 0; gy < 3; ++gy) {
            const int dx = x + gx * scale(5), dy = y + gy * scale(5);
            p->fill({dx, dy, dx + scale(2), dy + scale(2)}, on ? kAccent : kGripDot);
        }
}
void dropLine(Paint p, int x0, int x1, int y) { p->fill({x0, y - scale(1), x1, y + scale(1)}, kAccent); }

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
void reorderRow(Paint p, const std::string& list, RECT row, int index, std::function<void(int from, int to)> moved, bool can = true) {
    auto& rows = gReorderRows[list];
    if (index == 0) rows.clear();
    rows.push_back(row);
    const bool dragging = gReorder.list == list && gReorder.from >= 0;
    if (dragging && gReorder.from == index) p->fill(row, kSel, 160);
    const RECT g{row.left, row.top, row.left + scale(18), row.bottom};
    if (can && (hot(row) || (dragging && gReorder.from == index))) grip(*p, row.left + scale(5), (row.top + row.bottom) / 2 - scale(6), dragging && gReorder.from == index);
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
void reorderDropLine(Paint p, const std::string& list) {
    if (gReorder.list != list || gReorder.from < 0) return;
    const auto& rs = gReorderRows[list];
    if (rs.empty()) return;
    const int y = gReorder.to < int(rs.size()) ? rs[size_t(gReorder.to)].top : rs.back().bottom;
    dropLine(*p, rs.front().left, rs.front().right, y);
}

// --- Check boxes --------------------------------------------------------------------------------------------------
// The box (painted check boxes and the native ones alike).
void checkMark(Paint p, RECT b, bool on, bool over, bool enabled) { kitpaint::drawCheckMark(*p, b, on, over, enabled); }
void radioMark(Paint p, RECT b, bool on, bool over) {
    p->ellipse(b, kCtl, on ? kAccent : over ? kBorderHot : kBorderStrong);
    if (!on) return;
    const int in = (b.right - b.left) * 3 / 10;
    p->ellipse({b.left + in, b.top + in, b.right - in + 1, b.bottom - in + 1}, kAccent, kNone);
}
// A check box with its label; the whole is the click area. Returns the right edge.
int check(Paint p, RECT r, const std::wstring& label, bool on, std::function<void()> click, const std::wstring& tip = L"", bool changed = false, bool enabled = true) {
    const int s = scale(14);
    RECT b{r.left, (r.top + r.bottom - s) / 2, r.left + s, (r.top + r.bottom - s) / 2 + s};
    const bool over = enabled && hot(r);
    checkMark(*p, b, on, over, enabled);
    RECT t{b.right + scale(8), r.top, r.right, r.bottom};
    p->text(t, label, !enabled ? kFaint : changed ? kGood : on || over ? kText : kDim, kMiddle | kEllipsis);
    hitsOut().push_back({r, nullptr, enabled ? std::move(click) : nullptr, tip, enabled ? IDC_HAND : IDC_ARROW, {}});
    return t.left + textWidth(*p, label);
}

// --- Fields -------------------------------------------------------------------------------------------------------
using kitpaint::Field;
// A text field's box (painted fields, and the frame drawn around native edit boxes).
void fieldBox(Paint p, RECT r, Field f) { kitpaint::drawFieldBox(*p, r, f); }

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
int title(Paint p, int x, int y, int right, const UnitPalette::Bitmap* icon, const std::wstring& text, const std::wstring& sub, bool changed = false,
          std::function<void()> reset = nullptr, std::optional<UiIcon> kind = std::nullopt) {
    const kitpaint::TitleLayout t = kitpaint::drawTitle(*p, x, y, right, imageOf(icon), bool(kind), kind ? *kind : UiIcon{}, text, sub, changed);
    if (t.hasLine && changed && reset)
        link(*p, t.line.left + textWidth(*p, t.lineText), t.line.top, t.line.bottom - t.line.top, L"Reset", std::move(reset), L"Put back the game's values");
    return t.next;
}
// A section heading: its name and a line to the column's edge.
int sectionHeader(Paint p, const FormLayout& l, int y, const std::wstring& name, bool changed = false) {
    return kitpaint::drawSectionHeader(*p, l.x0, l.colRight, y, l.rowH, name, changed);
}
// A field's label: dim; green when its value is changed; faint when it cannot be changed.
void fieldLabel(Paint p, const FormLayout& l, int y, const std::wstring& name, bool changed, bool enabled = true) {
    kitpaint::drawFieldLabel(*p, {l.x0, y, l.x0 + l.labelW, y + l.rowH}, name, changed, enabled);
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
void sidePanel(Paint p, RECT side) {
    p->fill(side, kPanel);
    p->fill({side.left, side.top, side.left + 1, side.bottom}, kBorder);
}
int sideHeader(Paint p, int x, int y, int w, const std::wstring& title, size_t count) {
    const Rect h{x, y, x + w, y + scale(22)};
    p->text(h, title, kText, kMiddle);
    p->text(h, std::to_wstring(count), kDim, kMiddle | kRight);
    y += scale(30);
    if (!count) {
        p->text({x, y, x + w, y + scale(20)}, L"No other data uses this entry", kDim, kMiddle | kEllipsis);
        y += scale(22);
    }
    return y;
}
// `icon`: the group's icon before its name (-1: none).
int sideGroup(Paint p, int x, int y, int w, const std::wstring& name, bool first, std::optional<UiIcon> icon = std::nullopt) {
    if (!first) y += scale(8);
    const std::wstring& up = name;  // sentence case, as every heading (PRINCIPLES.md, "Text")
    const Font of = p->setFont(fontOf(smallFont()));
    if (icon) {
        p->icon({x, y - scale(1), x + scale(16), y + scale(16)}, int(*icon), kDim, scale(16));
        x += scale(22), w -= scale(22);
    }
    p->text({x, y, x + w, y + scale(15)}, up, kDim, kMiddle | kEllipsis);
    p->setFont(of);
    return y + scale(18);
}
// One reference: its icon and name in the accent; returns its row (the caller's click area).
RECT sideLink(Paint p, int x, int y, int w, const std::wstring& text, const UnitPalette::Bitmap* icon, bool over) {
    const int h = scale(20);
    int tx = x;
    if (icon && icon->bmp) {
        p->fitted(imageOf(icon), {x, y + scale(1), x + scale(18), y + h - scale(1)});
        tx += scale(22);
    }
    const RECT nr{tx, y, x + w, y + h};
    p->text(nr, text, over ? kAccentBright : kAccent, kMiddle | kEllipsis);
    if (over) {
        const int tw = std::min<int>(textWidth(*p, text), int(nr.right - nr.left));
        p->line(tx, nr.bottom - scale(3), tx + tw, nr.bottom - scale(3), kAccentBright);
    }
    return {x, y, x + w, y + h};
}

// --- Pickers ------------------------------------------------------------------------------------------------------
// A value in a sentence (a trigger's block, a briefing action): a chip to click. Dim: the block is turned off;
// bad: the value must be fixed; focused: Tab moved to it.
void chipFace(Paint p, RECT r, bool over, bool dim = false, bool bad = false, bool focused = false) {
    const COLORREF fill = dim ? kChipDim : bad ? (over ? kBadFillHot : kBadFill) : over ? kSelHot : kChip;
    const COLORREF line = focused ? kAccent : dim ? kReadOnlyLine : bad ? kBad : over ? kAccentLine : kChipLine;
    if (focused) {
        box(*p, r, fill, CLR_NONE);
        p->roundBox(r, kNone, line, float(scale(kRadius)), 2);
        return;
    }
    box(*p, r, fill, line);
}

// A dropdown: icon, name, the number on the right, the arrow; and the glyph that opens the entry when `open` is
// set. Clicking the box calls `click` with it.
// The dropdown's face, for surfaces with their own click areas (the Data form).
void pickFace(Paint p, RECT r, const std::wstring& text, bool changed, bool over, const UnitPalette::Bitmap* icon = nullptr, int number = -1, bool enabled = true) {
    kitpaint::drawPickFace(*p, r, text, changed, over, imageOf(icon), number, enabled);
}
void pickBox(Paint p, RECT r, const std::wstring& text, bool changed, std::function<void(RECT)> click, const std::wstring& tip = L"",
             const UnitPalette::Bitmap* icon = nullptr, int number = -1, std::function<void()> open = nullptr, bool enabled = true) {
    const bool over = enabled && hot(r);
    pickFace(*p, r, text, changed, over, icon, number, enabled);
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
    if (open) iconButton(*p, {r.right + scale(4), r.top, r.right + scale(4 + kIconBox), r.bottom}, UiIcon::OpenExternal, std::move(open), L"Open it", Icon::Bare, false, true, Tone::Primary);
}
POINT belowBox(RECT r) {
    POINT pt{r.left, r.bottom + scale(2)};
    ClientToScreen(hitsWnd(), &pt);
    return pt;
}

// The hover tooltips (tip_win32.inc): `w` shows the tip that `textAt` gives at a point of its client area. `key` names who
// attached (detach with another key does nothing).
void hoverTipsAttach(HWND w, std::function<std::wstring(POINT)> textAt, const void* key);
void hoverTipsDetach(HWND w, const void* key);

// The tip of the painted parts at `pt`: the first part with a tip that has the point (as a tooltip control finds its
// tool), empty when none has.
inline std::wstring hitTipAt(const std::vector<DataWidgetHit>& hits, POINT pt) {
    for (const DataWidgetHit& h : hits)
        if (!h.tip.empty() && PtInRect(&h.r, pt)) return h.tip;
    return {};
}

// --- Painted surfaces ---------------------------------------------------------------------------------------------
// A window painted with the kit: its click areas, the tooltips for them (the kit's, tip_win32.inc), the part under the
// mouse (a new part repaints the window), the cursor. A page calls begin() and end() around its paint, and message()
// first in its window procedure.
struct Surface {
    std::vector<DataWidgetHit> hits;
    RECT over{};
    struct {
        std::function<void(POINT)> drag;
        std::function<void()> click, release;
        bool down = false, moved = false;
    } grab;  // a part with a drag, while the mouse is down on it
    std::vector<DataWidgetHit>* savedOut = nullptr;
    HWND savedWnd = nullptr;

    HWND tipsFor = nullptr;  // the window the tooltips are for (it asks this surface for the tip under the mouse)
    void releaseTips() {
        if (tipsFor) hoverTipsDetach(tipsFor, this);
        tipsFor = nullptr;
    }
    Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface& operator=(Surface&& o) noexcept {  // (a page's host made again: the tooltips ask the new one)
        if (this != &o) {
            releaseTips();
            hits = std::move(o.hits), over = o.over, tipsFor = std::exchange(o.tipsFor, nullptr);
            grab = std::move(o.grab), savedOut = o.savedOut, savedWnd = o.savedWnd;
            if (tipsFor) hoverTipsAttach(tipsFor, [this](POINT p) { return hitTipAt(hits, p); }, this);
        }
        return *this;
    }
    ~Surface() { releaseTips(); }
    void begin(HWND w) {
        if (tipsFor && tipsFor != w) releaseTips();
        hits.clear();
        savedOut = gHitsOut, savedWnd = gHitsWnd;
        gHitsOut = &hits, gHitsWnd = w;
        gHitsOf[w] = &hits;
    }
    void end(HWND w) {
        gHitsOut = savedOut, gHitsWnd = savedWnd;
        if (tipsFor != w) {
            tipsFor = w;
            hoverTipsAttach(w, [this](POINT p) { return hitTipAt(hits, p); }, this);
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
        case WM_RBUTTONUP: {  // a right click on a part with a menu: the part's menu (the owner's popup)
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            for (auto it = hits.rbegin(); it != hits.rend(); ++it)
                if (PtInRect(&it->r, pt) && it->context) {
                    POINT screen = pt;
                    ClientToScreen(w, &screen);
                    auto context = it->context;
                    context(screen);
                    return result = 0, true;
                }
            return false;
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
int linkButton(ui::Paint p, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"", ui::Tone tone = ui::Tone::Normal) {
    return ui::smallButton(*p, x, y, h, text, std::move(click), tip, tone);  // an action: a small button
}
// A square button with an icon glyph.
void glyphButton(ui::Paint p, RECT r, ui::Ico glyph, bool on, std::function<void()> click, std::wstring tip) {
    ui::iconButton(*p, r, glyph, std::move(click), tip, ui::Icon::Boxed, on);
}
// The remove action of a row, shown on the row under the mouse.
void removeCross(ui::Paint p, RECT row, RECT at, std::function<void()> click, const std::wstring& tip) {
    hitsOut().push_back({row, nullptr, nullptr, L"", IDC_ARROW, {}});
    if (!ui::hot(row)) return;
    ui::iconButton(*p, at, UiIcon::Close, std::move(click), tip, ui::Icon::Bare, false, true, ui::Tone::Danger);
}
