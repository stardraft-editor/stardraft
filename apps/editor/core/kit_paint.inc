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

// --- Buttons, chips and rows (the trigger pane, the briefing; ui_kit.inc's faces call these) ----------------------
// A button's state.
enum class ButtonState { Normal, Hot, Down, Disabled };
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
// A button's width for its text.
inline int buttonWidth(draw::Canvas& c, const std::wstring& text) { return std::max(scale(stardraft::kit::kButtonMinW), c.measure(text) + scale(28)); }

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

// Where row action `k` of `n` goes in a row (the bare icon buttons at a row's right edge).
inline draw::Rect rowActionRect(draw::Rect row, int k, int n) {
    const int w = scale(stardraft::kit::kIconBox), h = std::min<int>(row.bottom - row.top, scale(stardraft::kit::kIconBox));
    const int x = row.right - scale(4) - w * (n - k), y = (row.top + row.bottom - h) / 2;
    return {x, y, x + w, y + h};
}

// The grip (six dots) on a row that can be dragged; the line where a dragged row goes.
inline void drawGrip(draw::Canvas& c, int x, int y, bool on) {
    for (int gx = 0; gx < 2; ++gx)
        for (int gy = 0; gy < 3; ++gy) {
            const int dx = x + gx * scale(5), dy = y + gy * scale(5);
            c.fill({dx, dy, dx + scale(2), dy + scale(2)}, on ? kAccent : kGripDot);
        }
}
inline void drawDropLine(draw::Canvas& c, int x0, int x1, int y) { c.fill({x0, y - scale(1), x1, y + scale(1)}, kAccent); }

}  // namespace kitpaint

// A row's text with the typed words in the accent, on a Canvas (the palette's list, the block picker): each piece of the text is drawn at its place, the matched ones in the accent.
void drawMatched(stardraft::draw::Canvas& c, stardraft::draw::Rect r, const std::wstring& text, const std::vector<std::wstring>& words, stardraft::draw::Color normal) {
    std::wstring low = text;
    std::transform(low.begin(), low.end(), low.begin(), ::towlower);
    std::vector<uint8_t> hit(text.size(), 0);
    for (const auto& w : words)
        if (const size_t at = w.empty() ? std::wstring::npos : low.find(w); at != std::wstring::npos)
            std::fill(hit.begin() + long(at), hit.begin() + long(at + w.size()), uint8_t(1));
    const int y = (r.top + r.bottom - c.metrics().height) / 2;  // (the GDI version's rounding: a row partly above the top)
    // Too wide: as much as fits, then an ellipsis (in the color of the last letter kept).
    std::wstring shown = text;
    if (c.measure(text) > r.width()) {
        const int room = std::max(0, r.width() - c.measure(L"\x2026"));
        size_t lo = 0, hi = text.size();  // the longest prefix that fits in `room` (widths grow with the prefix)
        while (lo < hi) {
            const size_t mid = (lo + hi + 1) / 2;
            if (c.measure(std::wstring_view(text).substr(0, mid)) <= room) lo = mid;
            else hi = mid - 1;
        }
        shown = text.substr(0, lo) + L"\x2026";
        hit.resize(shown.size());
        hit.back() = lo > 0 ? hit[lo - 1] : 0;
    }
    c.pushClip(r);
    int x = r.left;
    for (size_t i = 0; i < shown.size();) {
        size_t j = i;
        while (j < shown.size() && hit[j] == hit[i]) ++j;
        const std::wstring piece = shown.substr(i, j - i);
        c.text({x, y, r.right, y + c.metrics().height}, piece, hit[i] ? kAccent : normal, stardraft::draw::kLeft);
        x += c.measure(piece);
        i = j;
    }
    c.popClip();
}
