#pragma once
// A form: a label column on the left, a field (any view) beside each label, section headings and notes between the
// rows. The look of ui_kit.inc's formLayout, sectionHeader and fieldLabel: a dim label (green when its value is
// changed, faint when it cannot be changed), a heading in the text color with a 1 px line under it, rows kFormRowH
// apart, fields kFieldH high and centered in their row, a note in dim text that wraps.
//
// The form places its rows at its own width and sets its own height to the content's, so a Dialog or a ScrollView
// can size by it. The rows are placed when the form is resized or changed, with the notes' last measured heights (an
// estimate before the first paint). paint() measures the notes with the canvas and places again.

#include <string>
#include <vector>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

constexpr int kFormLabelW = 160;    // the label column, at 100 %
constexpr int kFormSectionGap = 6;  // between a heading's line and its first row, at 100 %

enum class RowMark { Normal, Changed, Disabled };

class Form : public draw::View {
public:
    Form() = default;

    // The label column's width (at 100 %): the fields start after it.
    void setLabelWidth(int w100);
    int labelWidth() const { return labelW_; }

    // Each call adds one line to the form, below the last.
    void addSection(const std::wstring& name);
    // A label and its field. The form adds the field as a child and sets its frame; the field must outlive the form
    // or be removed (a field that is destroyed first leaves the form on its next layout).
    void addRow(const std::wstring& label, draw::View& field, RowMark mark = RowMark::Normal);
    void addNote(const std::wstring& text);
    void addSpace(int h100);  // empty space, at 100 %
    // Removes the rows (and their fields from the form; the fields are not deleted).
    void clear();
    // The row's label color (a field changed or not allowed now).
    void setMark(const draw::View& field, RowMark mark);

    // The height the rows need (what the form sets as its own height).
    int contentHeight() const { return height_; }
    // Measures the notes with `c` and places the rows again.
    void layout(draw::Canvas& c);

    void paint(draw::Canvas& c) override;
    void resized() override { place(nullptr); }

private:
    enum class Kind { Section, Row, Note, Space };
    struct Item {
        Kind kind = Kind::Space;
        std::wstring text;               // Section, Note, Row (the label)
        draw::View* field = nullptr;     // Row
        RowMark mark = RowMark::Normal;  // Row
        int top = 0, h = 0;              // in the form's coordinates, at the current scale
        int textH = -1;                  // Note: the wrapped text's height (-1: not measured yet)
        int space100 = 0;                // Space: at 100 %
    };

    // Lays the rows out (c: measures the notes; null: the last measure, or an estimate) and sets the height. Rows
    // whose field is gone are dropped first.
    void place(draw::Canvas* c);

    std::vector<Item> items_;
    int labelW_ = kFormLabelW;
    int height_ = 0;
};

}  // namespace stardraft::kit
