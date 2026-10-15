// The Data workspace's entry form (portable, core.inc): where the parts of each field's row sit, from the form's
// metrics (the program scales them to the display). The form's paint and its click areas in main.cpp take their boxes
// from here; the arithmetic is the same as the paint had (the Windows look does not change).

// The form's sizes in pixels (scaled by the program).
struct DataFormScale {
    int two = 0;      // the inset of a value box from its row
    int eight = 0;    // the gap after a label's check box, a value box and a flag's name
    int four = 0;     // the room kept at the right of a flag's cell
    int seven = 0;    // the inset of a value's text from its box
    int boolW = 0;    // the width of a yes/no value's cell
    int dropW = 0;    // the width of a dropdown's box
    int flagW = 0;    // the width of one flag in the grid
    int flagH = 0;    // the height of one flag's row in the grid
    int box = 0;      // the size of a check box
};

// The parts of a field's row (its label's box; the value's box, a yes/no value's cell and check box and text; a
// dropdown's box; a number's note).
struct DataFieldRow {
    draw::Rect label, box, dropBox, boolCell, boolCheck, boolText, note;
};

DataFieldRow dataFieldRow(int x0, int labelW, int valW, int y, int rowH, int colRight, const DataFormScale& s) {
    DataFieldRow r;
    r.label = {x0, y, x0 + labelW, y + rowH};
    r.box = {x0 + labelW, y + s.two, x0 + labelW + valW, y + rowH - s.two};
    r.dropBox = {x0 + labelW, y + s.two, x0 + labelW + s.dropW, y + rowH - s.two};
    r.boolCell = {x0 + labelW, y + s.two, x0 + labelW + s.boolW, y + rowH - s.two};
    const int cb = s.box;
    r.boolCheck = {r.boolCell.left, y + (rowH - cb) / 2, r.boolCell.left + cb, y + (rowH - cb) / 2 + cb};
    r.boolText = {r.boolCheck.right + s.eight, y, r.boolCell.right, y + rowH};
    r.note = {r.box.right + s.eight, y, colRight, y + rowH};
    return r;
}

// The value's text box: the value box with its inset (the number's arrows take the right end of it, the caller's).
draw::Rect dataValueText(draw::Rect box, const DataFormScale& s) {
    return {box.left + s.seven, box.top, box.right - s.seven, box.bottom};
}

// The flags' grid: how many flags go across (the column's width less the label, in flag widths; at least one).
int dataFlagCols(int colW, int labelW, const DataFormScale& s) { return std::max(1, (colW - labelW) / s.flagW); }

// The height of a flags grid of `count` flags across `cols` columns, with the gap under it.
int dataFlagGridHeight(size_t count, int cols, const DataFormScale& s) {
    return int((count + size_t(cols) - 1) / size_t(cols)) * s.flagH + s.eight;
}

struct DataFlagCell {
    draw::Rect cell, check, text;  // the click area of the flag (its row's cell), its check box and its name
};

DataFlagCell dataFlagCell(int x0, int labelW, int y, int cols, size_t b, const DataFormScale& s) {
    const int cx = x0 + labelW + int(b % size_t(cols)) * s.flagW, cy = y + int(b / size_t(cols)) * s.flagH;
    DataFlagCell f;
    f.check = {cx, cy + (s.flagH - s.box) / 2, cx + s.box, cy + (s.flagH - s.box) / 2 + s.box};
    f.cell = {cx, cy, cx + s.flagW - s.four, cy + s.flagH};
    f.text = {f.check.right + s.eight, cy, cx + s.flagW - s.four, cy + s.flagH};
    return f;
}

// A yes/no value on the Canvas: the check box on the value's cell and "Yes", "No" or "Yes (n)" beside it (faint when
// read only, green when it differs from the game). `hot` tells whether the mouse is over a box of the form. The caller
// records the cell as a click area.
void paintDataBool(draw::Canvas& c, const DataFieldRow& fr, uint32_t v, bool ro, bool mod, const std::function<bool(draw::Rect)>& hot) {
    kitpaint::drawCheckMark(c, fr.boolCheck, v != 0, !ro && hot(fr.boolCell), !ro);
    const std::wstring label = v == 0 ? L"No" : v == 1 ? L"Yes" : L"Yes (" + std::to_wstring(v) + L")";
    c.text(fr.boolText, label, ro ? kFaint : mod ? kGood : kText, draw::kMiddle);
}

// A flag field's grid under its value, on the Canvas: each flag's check box and name (green when it differs from the
// game, lit when the mouse is over it). Each flag's click area goes to `hit(area, bit)`. Returns the grid's height.
int paintDataFlags(draw::Canvas& c, const DatField& f, const DatTable& t, int u, uint32_t v, int x0, int labelW, int y, int colW,
                   const DataFormScale& s, const std::function<bool(draw::Rect)>& hot, const std::function<void(draw::Rect, int)>& hit) {
    const int cols = dataFlagCols(colW, labelW, s);
    for (size_t b = 0; b < f.flags.size(); ++b) {
        const DataFlagCell fc = dataFlagCell(x0, labelW, y, cols, b, s);
        const bool set = (v >> b) & 1, bmod = ((t.stock(f, u) >> b) & 1) != set;
        kitpaint::drawCheckMark(c, fc.check, set, hot(fc.cell), true);
        c.text(fc.text, widen(f.flags[b]), bmod ? kGood : set || hot(fc.cell) ? kText : kDim, draw::kMiddle | draw::kEllipsis);
        hit(fc.cell, int(b));
    }
    return dataFlagGridHeight(f.flags.size(), cols, s);
}
