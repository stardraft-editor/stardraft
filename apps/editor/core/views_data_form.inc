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
