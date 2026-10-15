#include "form.h"

#include "button.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

// Rows: a section (32 + 6 under it), then two rows of 32 (the fields 24 high, 4 px from the row's top), the second
// marked changed. Label column 160 px; the fields start at 160 and run to the form's right edge (300 px wide).
KIT_TEST(form_rows_and_positions) {
    draw::View root;
    FakeHost host(root);
    kit::Form f;
    root.add(f, {0, 0, 300, 10});
    kit::Button a(L"A", kit::Tone::Normal, nullptr), b(L"B", kit::Tone::Normal, nullptr);
    f.addSection(L"Name");
    f.addRow(L"Name", a);
    f.addRow(L"Size", b, kit::RowMark::Changed);
    CHECK(f.contentHeight() == 102);
    CHECK(f.height() == 102);  // the form is as high as its rows
    CHECK(a.frame().left == 160 && a.frame().top == 42 && a.frame().right == 300 && a.frame().bottom == 66);
    CHECK(b.frame().left == 160 && b.frame().top == 74 && b.frame().right == 300 && b.frame().bottom == 98);
    CHECK(a.parent() == &f && b.parent() == &f);
}

KIT_TEST(form_paints_headings_labels_and_marks) {
    draw::View root;
    FakeHost host(root);
    kit::Form f;
    root.add(f, {0, 0, 300, 10});
    kit::Button a(L"A", kit::Tone::Normal, nullptr), b(L"B", kit::Tone::Normal, nullptr);
    f.addSection(L"Name");
    f.addRow(L"Name", a);
    f.addRow(L"Size", b, kit::RowMark::Changed);
    auto c = host.paint();
    CHECK(c.drew("text 0,0,300,32 'Name' 0xe0e0e0 4"));  // the heading, in the text color, centered on its row
    CHECK(c.drew("fill 0,31,300,32 0x403c3c 255"));      // a line under it, in the border color
    CHECK(c.drew("text 0,38,152,70 'Name' 0x949494 12"));  // a dim label, 8 px short of the field column
    CHECK(c.drew("text 0,70,152,102 'Size' 0x8ac97f 12"));  // a changed value: its label is green
    f.setMark(a, kit::RowMark::Disabled);
    c = host.paint();
    CHECK(c.drew("text 0,38,152,70 'Name' 0x605a5a 12"));  // cannot be changed now: faint
    f.setMark(a, kit::RowMark::Normal);
    CHECK(host.paint().drew("text 0,38,152,70 'Name' 0x949494 12"));
}

KIT_TEST(form_notes_and_space) {
    draw::View root;
    FakeHost host(root);
    kit::Form f;
    root.add(f, {0, 0, 300, 10});
    kit::Button a(L"A", kit::Tone::Normal, nullptr);
    f.addRow(L"Name", a);  // 32
    f.addNote(L"short");  // one line of 16 px and 8 px of room: 24
    CHECK(f.contentHeight() == 56);
    f.addNote(std::wstring(100, L'x'));  // 700 px in 300 px: three lines, 48 + 8
    CHECK(f.contentHeight() == 112);
    f.addSpace(10);
    CHECK(f.contentHeight() == 122);
    f.addNote(L"line one\nline two");  // two lines, 32 + 8
    CHECK(f.contentHeight() == 162);
    auto c = host.paint();
    CHECK(c.drew("text 0,36,300,52 'short' 0x949494 16"));  // the note's text, 4 px under its top
    CHECK(c.drew("text 0,60,300,108 '" + std::string(100, 'x') + "' 0x949494 16"));  // wrapped in its three lines
}

KIT_TEST(form_label_width_and_resize) {
    draw::View root;
    FakeHost host(root);
    kit::Form f;
    root.add(f, {0, 0, 300, 10});
    kit::Button a(L"A", kit::Tone::Normal, nullptr);
    f.addRow(L"Name", a);
    CHECK(a.frame().left == 160);
    f.setLabelWidth(200);
    CHECK(a.frame().left == 200);
    root.add(f, {0, 0, 400, 10});  // a wider form: the field runs to its edge
    CHECK(a.frame().right == 400);
    CHECK(f.contentHeight() == 32);
}

KIT_TEST(form_clear_and_gone_fields) {
    draw::View root;
    FakeHost host(root);
    kit::Form f;
    root.add(f, {0, 0, 300, 10});
    kit::Button a(L"A", kit::Tone::Normal, nullptr);
    f.addRow(L"Name", a);
    {
        kit::Button tmp(L"T", kit::Tone::Normal, nullptr);
        f.addRow(L"Tmp", tmp);
        CHECK(f.contentHeight() == 64);
    }  // destroyed: its row goes at the next layout
    host.paint();
    CHECK(f.contentHeight() == 32);
    f.clear();
    CHECK(f.contentHeight() == 0 && f.height() == 0);
    CHECK(a.parent() == nullptr);  // removed from the form, not deleted
    CHECK(f.children().empty());
}
