#include "dialog.h"

#include "kit_check.h"
#include "window_support.h"

using namespace stardraft;
using namespace stardraft::kit::test;
using namespace kitwin;

namespace {

// Content for a dialog: paints nothing, takes no keys.
class Body : public draw::View {};

// A content with a focusable button (a field): Enter and Space go to it, not to the dialog.
class FieldBody : public draw::View {
public:
    FieldBody() : field(L"Field", kit::Tone::Normal, [this] { ++clicks; }) { add(field, {10, 10, 100, 38}); }
    kit::Button field;
    int clicks = 0;
};

}  // namespace

// OK (primary, one step left of the last) and Cancel (the last, at the right edge) at the bottom right of a 300 x 150
// dialog: 88 px each, 8 px apart, 16 px from the edges, the content above them 24 px higher.
KIT_TEST(dialog_layout_and_paint) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"OK", 1, true, false}, {L"Cancel", 2, false, true}});
    const int r = dlg.run(root, L"Title", {300, 150});
    CHECK(host.windows.size() == 1);
    CHECK(host.windows[0].content == &dlg && host.windows[0].from == &root);
    CHECK(host.windows[0].spec.kind == draw::WindowKind::Dialog && host.windows[0].spec.title == L"Title");
    CHECK(host.windows[0].spec.at.width() == 300 && host.windows[0].spec.at.height() == 150);
    CHECK(r == 2);  // nothing chose a button: the window closed some other way, the cancel button's result
    CHECK(dlg.result() == kit::kDialogClosed && !dlg.closed());

    auto c = host.paintWindow(0);
    CHECK(c.drew("fill 0,0,300,150 0x202020 255"));  // the window's ground
    CHECK(c.drew("roundBox 100,106,188,134 0x3f3017"));  // OK: the primary face (sel)
    CHECK(c.drew("roundBox 196,106,284,134 0x2b2b2b"));  // Cancel: a normal face (ctl)
    CHECK(c.texts().size() == 2 && c.texts()[0] == "OK" && c.texts()[1] == "Cancel");
    CHECK(body.frame().left == 16 && body.frame().top == 16);
    CHECK(body.frame().right == 284 && body.frame().bottom == 82);
    CHECK(buttonNamed(dlg, L"OK")->frame().left == 100);
    CHECK(buttonNamed(dlg, L"Cancel")->frame().right == 284);
}

KIT_TEST(dialog_button_click_closes_with_its_result) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"OK", 1, true, false}, {L"Cancel", 2, false, true}});
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        clickIn(wr, 140, 120);  // OK
    };
    CHECK(dlg.run(root, L"T", {300, 150}) == 1);
    CHECK(dlg.closed() && dlg.result() == 1);
    CHECK(host.windows.empty());  // the window is closed
    CHECK(host.modals == 1);
}

KIT_TEST(dialog_cancel_button_click) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"OK", 1, true, false}, {L"Cancel", 2, false, true}});
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        clickIn(wr, 240, 120);  // Cancel
    };
    CHECK(dlg.run(root, L"T", {300, 150}) == 2);
    CHECK(dlg.result() == 2);
}

KIT_TEST(dialog_enter_and_escape) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"OK", 1, true, false}, {L"Cancel", 2, false, true}});
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        CHECK(pressIn(wr, draw::Key::Enter));  // nothing focused: the dialog takes it, the primary button
    };
    CHECK(dlg.run(root, L"T", {300, 150}) == 1);
    CHECK(host.windows.empty());

    kit::Dialog dlg2;
    dlg2.setContent(body);
    dlg2.setButtons({{L"OK", 1, true, false}, {L"Cancel", 2, false, true}});
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        CHECK(pressIn(wr, draw::Key::Escape));
    };
    CHECK(dlg2.run(root, L"T", {300, 150}) == 2);
}

KIT_TEST(dialog_enter_on_a_focused_button_presses_that_button) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"OK", 1, true, false}, {L"Cancel", 2, false, true}});
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        pressIn(wr, draw::Key::Tab);  // OK
        pressIn(wr, draw::Key::Tab);  // Cancel
        CHECK(wr.focused() == buttonNamed(content, L"Cancel"));
        CHECK(pressIn(wr, draw::Key::Enter));  // the focused button's own Enter: Cancel
    };
    CHECK(dlg.run(root, L"T", {300, 150}) == 2);
}

KIT_TEST(dialog_focused_field_keeps_enter) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    FieldBody body;
    dlg.setContent(body);
    dlg.setButtons({{L"OK", 1, true, false}, {L"Cancel", 2, false, true}});
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        pressIn(wr, draw::Key::Tab);  // the field comes first in the content
        CHECK(wr.focused() == &body.field);
        CHECK(pressIn(wr, draw::Key::Enter));
        CHECK(body.clicks == 1);
    };
    dlg.run(root, L"T", {300, 150});
    CHECK(body.clicks == 1);
    CHECK(!dlg.closed());  // the field took the key: the dialog is still open
    CHECK(host.windows.size() == 1);
    dlg.close(1);  // and the window closes with the result given
    CHECK(dlg.result() == 1 && host.windows.empty());
}

KIT_TEST(dialog_keys_without_their_buttons) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"OK", 1, false, false}});  // no primary, no cancel
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        CHECK(!pressIn(wr, draw::Key::Enter));
        CHECK(!pressIn(wr, draw::Key::Escape));
        CHECK(!pressIn(wr, draw::Key::Space));
    };
    CHECK(dlg.run(root, L"T", {300, 150}) == kit::kDialogClosed);
    CHECK(!dlg.closed());
    CHECK(host.windows.size() == 1);
}

KIT_TEST(dialog_escape_with_cancel_only) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"Close", 4, false, true}});
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        CHECK(!pressIn(wr, draw::Key::Enter));  // no primary: Enter does nothing
        CHECK(pressIn(wr, draw::Key::Escape));
    };
    CHECK(dlg.run(root, L"T", {300, 150}) == 4);
}

KIT_TEST(dialog_first_close_counts) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"OK", 1, true, false}, {L"Cancel", 2, false, true}});
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        clickIn(wr, 140, 120);  // OK closes it
        dlg.close(9);           // a later close does nothing
        clickIn(wr, 240, 120);  // and the window's gone: nothing happens
    };
    CHECK(dlg.run(root, L"T", {300, 150}) == 1);
    CHECK(dlg.result() == 1);
}

KIT_TEST(dialog_close_from_code) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"OK", 1, true, false}});
    host.onOpen = [&](draw::View&) { dlg.close(7); };
    CHECK(dlg.run(root, L"T", {300, 150}) == 7);
    CHECK(host.windows.empty());
}

KIT_TEST(dialog_wide_buttons_are_measured_when_painted) {
    draw::View root;
    ScriptedHost host(root);
    kit::Dialog dlg;
    Body body;
    dlg.setContent(body);
    dlg.setButtons({{L"Save", 1, true, false}, {L"Don't save", 2, false, false}, {L"Cancel", 3, false, true}});
    dlg.run(root, L"T", {400, 150});
    // Before a paint the buttons have the minimum width; the paint measures them: "Don't save" is 98 px (70 + 28).
    CHECK(buttonNamed(dlg, L"Cancel")->frame().left == 296);
    host.paintWindow(0);
    CHECK(buttonNamed(dlg, L"Cancel")->frame().left == 296 && buttonNamed(dlg, L"Cancel")->frame().right == 384);
    CHECK(buttonNamed(dlg, L"Don't save")->frame().left == 190 && buttonNamed(dlg, L"Don't save")->frame().right == 288);
    CHECK(buttonNamed(dlg, L"Save")->frame().left == 94 && buttonNamed(dlg, L"Save")->frame().right == 182);
}
