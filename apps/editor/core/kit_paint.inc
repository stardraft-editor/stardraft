// The kit's painted parts on the Canvas (portable, core.inc): the field boxes and their states, the number arrows, check
// marks, glyphs and icon faces, dropdown faces, link faces, section headings, field labels, the changed dot and the title.
// The Win32 kit (ui_kit.inc) draws the same parts through its Paint, and its functions call these, so both programs make
// the same pixels. Sizes are scaled by the program; colors are the tokens of colors.inc.

namespace kitpaint {

// What a control's face shows: a tone of a button or icon, and a text field's state.
enum class Tone { Normal, Primary, Danger, Quiet };
enum class Field { Normal, Hot, Focus, Changed, ReadOnly };

// An icon: one of the UI icons (tools/icongen; apps/editor/res/ui_icons.h), or a Segoe MDL2 glyph where the icon set
// has none yet.
struct Ico {
    const wchar_t* glyph = nullptr;
    int icon = -1;
    Ico(const wchar_t* g) : glyph(g) {}
    Ico(UiIcon i) : icon(int(i)) {}
};

// The text color of a tone (a button's or a link's text).
inline draw::Color toneText(Tone t) { return t == Tone::Primary ? kAccent : t == Tone::Danger ? kBad : t == Tone::Quiet ? kDim : kText; }

// The largest square that fits in `r`, centered.
inline draw::Rect square(draw::Rect r) {
    const int w = r.width(), h = r.height(), s = std::min(w, h);
    const int x = r.left + (w - s) / 2, y = r.top + (h - s) / 2;
    return {x, y, x + s, y + s};
}

// --- Buttons ------------------------------------------------------------------------------------------------------
// A button's state: resting, under the mouse, pressed, or not available.
enum class ButtonState { Normal, Hot, Down, Disabled };
// The face of a painted button (ui_kit.inc's buttonFace draws the native push buttons with it too): its box by tone and
// state, and its text in the middle.
// A button's width for its text (at least kButtonMinW).
inline int buttonWidth(draw::Canvas& c, const std::wstring& text) {
    return std::max(scale(stardraft::kit::kButtonMinW), c.measure(text) + scale(28));
}
// A small button (an action in a row, "Add an action"): as wide as its text and 20 px more, at most 24 px high, in the
// middle of the row's height `h` from `y`. Its box; the caller draws it with drawButtonFace (a small button is never the
// main action: Primary is drawn as Normal).
inline draw::Rect smallButtonRect(draw::Canvas& c, int x, int y, int h, const std::wstring& text) {
    const int bh = std::min(h, scale(24)), w = c.measure(text) + scale(20);
    return {x, y + (h - bh) / 2, x + w, y + (h - bh) / 2 + bh};
}

// --- Fields -------------------------------------------------------------------------------------------------------
// A text field's box (its ground and its line by state).
inline void drawFieldBox(draw::Canvas& c, draw::Rect r, Field f) {
    draw::Color fill = kCtl, line = kBorder;
    if (f == Field::Hot) line = kBorderHot;
    if (f == Field::Focus) line = kAccent;
    if (f == Field::Changed) fill = kGoodFill, line = kGoodLine;
    if (f == Field::ReadOnly) fill = kReadOnly, line = kReadOnlyLine;
    c.roundBox(r, fill, line, float(scale(stardraft::kit::kRadius)));
}

// The glyph of a Segoe MDL2 face (or a UI icon), centered in `r`, at `size` (96-dpi pixels).
inline void drawGlyph(draw::Canvas& c, draw::Rect r, Ico g, draw::Color col, int size = stardraft::kit::kGlyph) {
    if (g.icon >= 0) return c.icon(r, g.icon, col, scale(16));
    const draw::Font of = c.setFont(c.font({draw::Face::Glyphs, scale(size)}));
    c.text(r, std::wstring_view(g.glyph, 1), col, draw::kMiddle | draw::kCenter);
    c.setFont(of);
}

// The two halves of a number field's arrows: the right end of the box, up above down.
inline draw::Rect numberArrowsRect(draw::Rect vb) { return {vb.right - scale(18), vb.top, vb.right, vb.bottom}; }
inline void numberArrowHalves(draw::Rect vb, draw::Rect& up, draw::Rect& down) {
    const draw::Rect ar = numberArrowsRect(vb);
    up = {ar.left, ar.top, ar.right, (ar.top + ar.bottom) / 2};
    down = {ar.left, up.bottom, ar.right, ar.bottom};
}
// A number field's arrows (chevrons) while the mouse is on the box: each lit when the mouse is on its half.
inline void drawNumberArrows(draw::Canvas& c, draw::Rect vb, bool hotBox, bool upHot, bool downHot) {
    if (!hotBox) return;
    draw::Rect up, down;
    numberArrowHalves(vb, up, down);
    drawGlyph(c, up, L"\xE70E", upHot ? kText : kDim, 7);    // chevron up
    drawGlyph(c, down, L"\xE70D", downHot ? kText : kDim, 7);  // chevron down
}

// A dropdown's face: icon, name, the number on the right, the chevron. Changed, read-only and hot take their own field
// states. `number` < 0: none.
inline void drawPickFace(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool changed, bool over, draw::Image icon = {}, int number = -1,
                     bool enabled = true) {
    drawFieldBox(c, r, !enabled ? Field::ReadOnly : changed ? Field::Changed : over ? Field::Hot : Field::Normal);
    int tx = r.left + scale(8);
    if (icon) {
        c.fitted(icon, {tx - scale(2), r.top + scale(1), tx + scale(18), r.bottom - scale(1)});
        tx += scale(24);
    }
    c.text({tx, r.top, r.right - (number >= 0 ? scale(56) : scale(22)), r.bottom}, text, !enabled ? kFaint : changed ? kGood : kText,
           draw::kMiddle | draw::kEllipsis);
    if (number >= 0) c.text({r.right - scale(64), r.top, r.right - scale(22), r.bottom}, std::to_wstring(number), kDim, draw::kMiddle | draw::kRight);
    if (enabled) drawGlyph(c, {r.right - scale(20), r.top, r.right - scale(6), r.bottom}, UiIcon::Dropdown, over ? kText : kDim, 9);  // chevron down
}

// --- Check boxes -------------------------------------------------------------------------------------------------
// The box of a check box (painted ones and the native ones alike): a mark when on.
inline void drawCheckMark(draw::Canvas& c, draw::Rect b, bool on, bool over, bool enabled) {
    c.roundBox(b, on ? kAccentFill : kCtl, on ? kAccent : over ? kBorderHot : kBorderStrong, float(scale(3)));
    if (!on) return;
    const int w = b.width(), h = b.height();
    const draw::Point mark[] = {{b.left + w * 22 / 100, b.top + h * 52 / 100}, {b.left + w * 42 / 100, b.top + h * 72 / 100}, {b.left + w * 78 / 100, b.top + h * 30 / 100}};
    c.polyline(mark, 3, enabled ? kOnText : kDim, std::max(1, scale(2)));
}

// --- Icon buttons -----------------------------------------------------------------------------------------------
// A bare icon button's face (in rows, beside fields): `on` with Tone::Quiet is a toggle that is off (amber). Returns the glyph's color.
inline draw::Color drawIconFace(draw::Canvas& c, draw::Rect r, Ico g, Tone tone, bool on, bool over, bool enabled = true) {
    r = square(r);
    if (over && enabled) c.roundBox(r, kCtlHot, draw::kNone, float(scale(stardraft::kit::kRadius)));
    const draw::Color rest = tone == Tone::Quiet && on ? kWarn : on || tone == Tone::Primary ? kAccent : kDim;
    const draw::Color lit = tone == Tone::Danger ? kBad : tone == Tone::Quiet && on ? kWarn : tone == Tone::Primary || on ? kAccentBright : kText;
    const draw::Color col = !enabled ? kFaint : over ? lit : rest;
    drawGlyph(c, r, g, col);
    return col;
}

// --- Links and marks ----------------------------------------------------------------------------------------------
// A link's face in its box `r`: its text, and a line under it while the mouse is on it (not for the quiet tone).
inline void drawLinkFace(draw::Canvas& c, draw::Rect r, const std::wstring& text, Tone tone, bool over) {
    c.text(r, text, tone == Tone::Quiet ? (over ? kText : kDim) : toneText(tone), draw::kMiddle);
    if (over && tone != Tone::Quiet) {
        const auto tm = c.metrics();
        const int base = (r.top + r.bottom + tm.height) / 2 - tm.descent + scale(1);
        c.line(r.left, base, r.right, base, toneText(tone));
    }
}

// The mark of a changed entry: a small green dot centered on (cx, cy).
inline void drawChangedDot(draw::Canvas& c, int cx, int cy) {
    const int r = scale(3);
    c.ellipse({cx - r, cy - r, cx + r + 1, cy + r + 1}, kGood, draw::kNone);
}

// --- Form parts ---------------------------------------------------------------------------------------------------
// The form's layout, as the Data forms have it, for a form `width` wide: the fields in a column on the left, the
// pictures beside them when the form is wide (above them when narrow).
struct FormLayout {
    int x0, labelW, rowH, colW, colRight, picX, picW;
    bool twoCol;
};
inline FormLayout formLayout(int width) {
    FormLayout l{};
    l.x0 = scale(18), l.labelW = scale(230), l.rowH = scale(stardraft::kit::kRowH);
    const int full = std::max(scale(300), width - 2 * l.x0);
    l.twoCol = full >= scale(1150);
    l.colW = l.twoCol ? std::min(scale(650), full * 11 / 20) : full;
    l.colRight = l.x0 + l.colW;
    l.picX = l.twoCol ? l.colRight + scale(28) : l.x0;
    l.picW = l.twoCol ? full - l.colW - scale(28) : full;
    return l;
}

// A section heading: its name and a line under it to the column's right edge. Returns the y of the next row.
inline int drawSectionHeader(draw::Canvas& c, int x0, int colRight, int y, int rowH, const std::wstring& name, bool changed = false) {
    c.text({x0, y, colRight, y + rowH}, name, changed ? kGood : kText, draw::kMiddle);
    c.fill({x0, y + rowH - 1, colRight, y + rowH}, kBorder);
    return y + rowH + scale(6);
}
// A field's label: dim; green when its value is changed; faint when it cannot be changed.
inline void drawFieldLabel(draw::Canvas& c, draw::Rect label, const std::wstring& name, bool changed, bool enabled = true) {
    c.text(label, name, !enabled ? kFaint : changed ? kGood : kDim, draw::kMiddle | draw::kEllipsis);
}

// A title: the picture (or the kind's icon when there is none) at the left, the name in the title font, and a line under
// it: the subtitle, and "changed" when the value is changed. `line` is where that line is; `hasLine` false: no line.
struct TitleLayout {
    int next = 0;           // the y of the row after the title
    draw::Rect line;        // the line's box (its text starts at line.left)
    std::wstring lineText;  // the line's text (the caller measures it to put a Reset link after it)
    bool hasLine = false;
};
inline TitleLayout drawTitle(draw::Canvas& c, int x, int y, int right, draw::Image icon, bool hasKind, UiIcon kind, const std::wstring& text,
                             const std::wstring& sub, bool changed) {
    int tx = x;
    if (icon) {
        c.fitted(icon, {x, y, x + scale(40), y + scale(40)});
        tx += scale(52);
    } else if (hasKind) {
        c.icon({x, y, x + scale(40), y + scale(40)}, int(kind), kDim, scale(32));
        tx += scale(52);
    }
    const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)}));
    c.text({tx, y, right, y + scale(26)}, text.empty() ? L"(empty)" : text, kText, draw::kMiddle | draw::kEllipsis);
    c.setFont(of);
    TitleLayout t;
    if (sub.empty() && !changed) {
        t.next = y + scale(40);
        return t;
    }
    t.line = {tx, y + scale(24), right, y + scale(42)};
    t.lineText = sub + (changed ? (sub.empty() ? L"changed  \x00B7  " : L"  \x00B7  changed  \x00B7  ") : L"");
    t.hasLine = true;
    c.text(t.line, t.lineText, kDim, draw::kMiddle);
    t.next = y + scale(56);
    return t;
}

// --- Small text ---------------------------------------------------------------------------------------------------
// The small font: the UI font at 5/6 of its height (captions, table headers, the side bar's groups).
inline draw::FontDesc smallFontDesc() {
    draw::FontDesc d;
    d.px = -(shellUiFontLfHeight() * 5 / 6);
    return d;
}

// --- Buttons, chips and rows (the trigger pane, the briefing; ui_kit.inc's faces call these) ----------------------
// A rounded box with the kit's radius: `fill` (kNone: none) and a 1 px `line` (kNone: none).
inline void drawBox(draw::Canvas& c, draw::Rect r, draw::Color fill, draw::Color line, int radius = stardraft::kit::kRadius) {
    c.roundBox(r, fill, line, float(scale(radius)));
}
// The face of a button: its box by tone and state, and the text (in `font` when it is set).
inline void drawButtonFace(draw::Canvas& c, draw::Rect r, const std::wstring& text, Tone tone, ButtonState s, draw::Font font = {}) {
    draw::Color fill = kCtl, line = kBorder;
    if (tone == Tone::Primary) fill = kSel, line = kAccentLine;
    if (s == ButtonState::Hot) fill = tone == Tone::Primary ? kSelHot : kCtlHot, line = tone == Tone::Primary ? kAccent : kBorderHot;
    if (s == ButtonState::Down) fill = tone == Tone::Primary ? kSel : kCtlDown;
    if (s == ButtonState::Disabled) fill = kBg, line = kBorder;
    drawBox(c, r, fill, line);
    const draw::Font of = font ? c.setFont(font) : draw::Font{};
    c.text(r, text, s == ButtonState::Disabled ? kFaint : toneText(tone == Tone::Quiet ? Tone::Normal : tone), draw::kMiddle | draw::kCenter | draw::kEllipsis);
    if (of) c.setFont(of);
}

// "Add ..." under a list: a quiet outline, a + and the text in dim; under the mouse the row's ground and the accent.
inline void drawAddRowFace(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool over, bool enabled = true) {
    drawBox(c, r, over && enabled ? kRowHot : draw::kNone, over && enabled ? kBorderHot : kBorder, 6);
    const int is = scale(16);
    const draw::Color col = !enabled ? kFaint : over ? kAccent : kDim;
    const draw::Rect ir{r.left + scale(10), (r.top + r.bottom - is) / 2, r.left + scale(10) + is, (r.top + r.bottom + is) / 2};
    c.icon(ir, int(UiIcon::Add), col, is);
    c.text({ir.right + scale(8), r.top, r.right - scale(8), r.bottom}, text, col, draw::kMiddle | draw::kEllipsis);
}

// A value in a sentence (a trigger's block, a briefing action): a chip. Dim: the block is turned off; bad: the value
// must be fixed; focused: Tab moved to it.
inline void drawChipFace(draw::Canvas& c, draw::Rect r, bool over, bool dim = false, bool bad = false, bool focused = false) {
    const draw::Color fill = dim ? kChipDim : bad ? (over ? kBadFillHot : kBadFill) : over ? kSelHot : kChip;
    const draw::Color line = focused ? kAccent : dim ? kReadOnlyLine : bad ? kBad : over ? kAccentLine : kChipLine;
    if (focused) {
        drawBox(c, r, fill, draw::kNone);
        c.roundBox(r, draw::kNone, line, float(scale(stardraft::kit::kRadius)), 2);
        return;
    }
    drawBox(c, r, fill, line);
}

// --- Rows -------------------------------------------------------------------------------------------------------
// A list row's background: selected (fill and a bar on the left), under the mouse, or none.
inline void drawListRow(draw::Canvas& c, draw::Rect r, bool selected, bool over) {
    if (selected) {
        c.fill(r, kSel);
        c.fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
    } else if (over) {
        c.fill(r, kRowHot);
    }
}
inline draw::Color rowText(bool selected) { return selected ? kAccent : kText; }

// Where row action `k` of `n` goes in a row (the bare icon buttons at a row's right edge).
inline draw::Rect rowActionRect(draw::Rect row, int k, int n) {
    const int w = scale(stardraft::kit::kIconBox), h = std::min<int>(row.bottom - row.top, scale(stardraft::kit::kIconBox));
    const int x = row.right - scale(4) - w * (n - k), y = (row.top + row.bottom - h) / 2;
    return {x, y, x + w, y + h};
}

// A painted scroll bar (for lists that page by rows), as Windows 11's: a thin line at rest, wider with its track under
// the mouse or while dragged.
inline void drawScrollFace(draw::Canvas& c, draw::Rect track, draw::Rect thumb, bool lit) {
    if (lit) c.fill(track, kPanel);
    else thumb.left = thumb.right - std::max(2, int(thumb.right - thumb.left) / 2);
    c.roundBox(thumb, lit ? kThumbHot : kThumb, draw::kNone, float(scale(3)));
}

// --- Tables ---------------------------------------------------------------------------------------------------------
// Rows with columns: a header (names in the dim color), rows with pills when selected or under the mouse.
struct Column {
    std::wstring name;
    int width = 0;        // 0: what the others leave
    unsigned align = 0;   // draw::kLeft, kCenter or kRight
};
// Where column `k` is in a row.
inline draw::Rect tableCell(draw::Rect row, const std::vector<Column>& cols, size_t k) {
    int fixed = 0, flexible = 0;
    for (const auto& col : cols) col.width ? fixed += scale(col.width) : ++flexible;
    const int rest = std::max(0, int(row.right - row.left) - scale(12) - fixed);
    int x = row.left + scale(6);
    for (size_t i = 0; i < cols.size(); ++i) {
        const int w = cols[i].width ? scale(cols[i].width) : rest / std::max(1, flexible);
        if (i == k) return {x, row.top, x + w, row.bottom};
        x += w;
    }
    return {x, row.top, x, row.bottom};
}
inline unsigned alignFlags(unsigned a) { return (a & draw::kRight ? draw::kRight : a & draw::kCenter ? draw::kCenter : draw::kLeft) | draw::kMiddle; }
// The header: the column names small and dim, one line under them (no band, no frame).
inline void drawTableHeader(draw::Canvas& c, draw::Rect row, const std::vector<Column>& cols) {
    const draw::Font of = c.setFont(c.font(smallFontDesc()));
    for (size_t k = 0; k < cols.size(); ++k) {
        draw::Rect cell = tableCell(row, cols, k);
        cell.left += scale(4), cell.right -= scale(4);
        c.text(cell, cols[k].name, kDim, alignFlags(cols[k].align));
    }
    c.setFont(of);
    c.fill({row.left, row.bottom - 1, row.right, row.bottom}, kBorder);
}
// A row: on the page's ground; selected or under the mouse a rounded pill (as the lists and pickers).
inline void drawTableRow(draw::Canvas& c, draw::Rect row, bool selected, bool over) {
    const draw::Rect pill{row.left + scale(2), row.top + scale(1), row.right - scale(2), row.bottom - scale(1)};
    if (selected) c.roundBox(pill, kSel, kSel, float(scale(6)));
    else if (over) c.roundBox(pill, kRowHot, kRowHot, float(scale(6)));
}
// One cell's text.
inline void drawTableText(draw::Canvas& c, draw::Rect row, const std::vector<Column>& cols, size_t k, const std::wstring& text, draw::Color color) {
    draw::Rect cell = tableCell(row, cols, k);
    cell.left += scale(4), cell.right -= scale(4);
    c.text(cell, text, color, alignFlags(cols[k].align) | draw::kEllipsis);
}

// --- Radio buttons and check boxes ----------------------------------------------------------------------------------
inline void drawRadioMark(draw::Canvas& c, draw::Rect b, bool on, bool over) {
    c.ellipse(b, kCtl, on ? kAccent : over ? kBorderHot : kBorderStrong);
    if (!on) return;
    const int in = (b.right - b.left) * 3 / 10;
    c.ellipse({b.left + in, b.top + in, b.right - in + 1, b.bottom - in + 1}, kAccent, draw::kNone);
}
// A check box with its label in `r` (the box in the middle of its height at the left). Returns the label's right edge.
inline int drawCheck(draw::Canvas& c, draw::Rect r, const std::wstring& label, bool on, bool over, bool changed = false, bool enabled = true) {
    const int s = scale(14);
    const draw::Rect b{r.left, (r.top + r.bottom - s) / 2, r.left + s, (r.top + r.bottom - s) / 2 + s};
    drawCheckMark(c, b, on, over, enabled);
    const draw::Rect t{b.right + scale(8), r.top, r.right, r.bottom};
    c.text(t, label, !enabled ? kFaint : changed ? kGood : on || over ? kText : kDim, draw::kMiddle | draw::kEllipsis);
    return t.left + c.measure(label);
}

// --- The References side bar ----------------------------------------------------------------------------------------
// What uses the selected thing, by group ("Units", "Briefings"), each a link to it. The callers keep their own click
// areas: these paint and say where.
inline void drawSidePanel(draw::Canvas& c, draw::Rect side) {
    c.fill(side, kPanel);
    c.fill({side.left, side.top, side.left + 1, side.bottom}, kBorder);
}
inline int drawSideHeader(draw::Canvas& c, int x, int y, int w, const std::wstring& title, size_t count) {
    const draw::Rect h{x, y, x + w, y + scale(22)};
    c.text(h, title, kText, draw::kMiddle);
    c.text(h, std::to_wstring(count), kDim, draw::kMiddle | draw::kRight);
    y += scale(30);
    if (!count) {
        c.text({x, y, x + w, y + scale(20)}, L"No other data uses this entry", kDim, draw::kMiddle | draw::kEllipsis);
        y += scale(22);
    }
    return y;
}
// A group's name (small and dim), with its icon before it when `hasIcon`. Returns the y of its first link.
inline int drawSideGroup(draw::Canvas& c, int x, int y, int w, const std::wstring& name, bool first, bool hasIcon = false, UiIcon icon = {}) {
    if (!first) y += scale(8);
    const draw::Font of = c.setFont(c.font(smallFontDesc()));
    if (hasIcon) {
        c.icon({x, y - scale(1), x + scale(16), y + scale(16)}, int(icon), kDim, scale(16));
        x += scale(22), w -= scale(22);
    }
    c.text({x, y, x + w, y + scale(15)}, name, kDim, draw::kMiddle | draw::kEllipsis);
    c.setFont(of);
    return y + scale(18);
}
// One reference: its icon and name in the accent; returns its row (the caller's click area).
inline draw::Rect drawSideLink(draw::Canvas& c, int x, int y, int w, const std::wstring& text, draw::Image icon, bool over) {
    const int h = scale(20);
    int tx = x;
    if (icon) {
        c.fitted(icon, {x, y + scale(1), x + scale(18), y + h - scale(1)});
        tx += scale(22);
    }
    const draw::Rect nr{tx, y, x + w, y + h};
    c.text(nr, text, over ? kAccentBright : kAccent, draw::kMiddle | draw::kEllipsis);
    if (over) {
        const int tw = std::min<int>(c.measure(text), int(nr.right - nr.left));
        c.line(tx, nr.bottom - scale(3), tx + tw, nr.bottom - scale(3), kAccentBright);
    }
    return {x, y, x + w, y + h};
}

// --- Lists that can be reordered ------------------------------------------------------------------------------------
// The grip (six dots) on a row that can be dragged; the line where a dragged row goes.
inline void drawGrip(draw::Canvas& c, int x, int y, bool on) {
    for (int gx = 0; gx < 2; ++gx)
        for (int gy = 0; gy < 3; ++gy) {
            const int dx = x + gx * scale(5), dy = y + gy * scale(5);
            c.fill({dx, dy, dx + scale(2), dy + scale(2)}, on ? kAccent : kGripDot);
        }
}
inline void drawDropLine(draw::Canvas& c, int x0, int x1, int y) { c.fill({x0, y - scale(1), x1, y + scale(1)}, kAccent); }

// A slider: the track from x0 to x1 at cy, lit to `frac` (0..1), and its knob (ui_kit.inc's sliderFace).
inline void drawSliderFace(draw::Canvas& c, int x0, int x1, int cy, float frac, bool over, bool enabled = true) {
    const draw::Rect track{x0, cy - scale(2), x1, cy + scale(2)};
    c.roundBox(track, kCtl, kBorder, float(scale(2)));
    const int kx = x0 + int(std::lround(std::clamp(frac, 0.0f, 1.0f) * float(x1 - x0)));
    c.fill({x0, cy - scale(2), kx, cy + scale(2)}, enabled ? kAccent : kFaint);
    c.smoothRoundOutline({kx - scale(7), cy - scale(7), kx + scale(7), cy + scale(7)}, float(scale(7)), !enabled ? kFaint : over ? kAccentBright : kAccent,
                         2.0f);
}

}  // namespace kitpaint

