#include "message_box.h"

#include "kit_check.h"
#include "window_support.h"

using namespace stardraft;
using namespace stardraft::kit::test;
using namespace kitwin;

// A message box: the icon and the text, then the buttons (the first primary, the last the cancel one).
KIT_TEST(message_box_shows_text_icon_and_buttons) {
    draw::View root;
    ScriptedHost host(root);
    draw::Rect at;
    host.onOpen = [&](draw::View& content) {
        at = host.windows[0].spec.at;
        CHECK(host.windows[0].spec.kind == draw::WindowKind::Dialog);
        CHECK(host.windows[0].spec.title == L"Map");
        const auto c = host.paintWindow(0);
        CHECK(c.drew("icon 16,16,48,48 86 0x6068e8"));  // an error: the error icon in the bad color
        const auto texts = c.texts();
        CHECK(texts.size() == 4);
        if (texts.size() == 4) {
            CHECK(texts[0] == "Could not open the map");
            CHECK(texts[1] == "Save" && texts[2] == "Don't save" && texts[3] == "Cancel");
        }
        // The first button is the primary one, the last the cancel one: Escape presses Cancel.
        draw::Router& wr = host.routerFor(content);
        CHECK(pressIn(wr, draw::Key::Escape));
    };
    CHECK(kit::messageBox(root, L"Map", L"Could not open the map", {L"Save", L"Don't save", L"Cancel"}, kit::MessageIcon::Error) == 2);
    // The box is as wide as its buttons (88 + 98 + 88 + 16 px) and tall as its icon, 16 px above and below.
    CHECK(at.width() == 322 && at.height() == 116);
    CHECK(host.windows.empty());
}

KIT_TEST(message_box_button_click_gives_its_index) {
    draw::View root;
    ScriptedHost host(root);
    host.onOpen = [&](draw::View& content) {
        kit::Button* b = buttonNamed(content, L"Don't save");
        CHECK(b != nullptr);
        if (!b) return;
        const draw::Point m = middleOf(*b);
        draw::Router& wr = host.routerFor(content);
        clickIn(wr, m.x, m.y);
    };
    CHECK(kit::messageBox(root, L"Map", L"Save the changes?", {L"Save", L"Don't save", L"Cancel"}, kit::MessageIcon::Question) == 1);
}

KIT_TEST(message_box_enter_presses_the_first_button) {
    draw::View root;
    ScriptedHost host(root);
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        CHECK(pressIn(wr, draw::Key::Enter));
    };
    CHECK(kit::messageBox(root, L"Map", L"Save the changes?", {L"Save", L"Don't save", L"Cancel"}) == 0);
}

KIT_TEST(message_box_ok_is_both_primary_and_cancel) {
    draw::View root;
    ScriptedHost host(root);
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        CHECK(pressIn(wr, draw::Key::Escape));
    };
    CHECK(kit::messageBox(root, L"Note", L"Done.", {L"OK"}, kit::MessageIcon::Info) == 0);
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        CHECK(pressIn(wr, draw::Key::Enter));
    };
    CHECK(kit::messageBox(root, L"Note", L"Done.") == 0);  // OK is the default
}

KIT_TEST(message_box_yes_no_escape_is_no) {
    draw::View root;
    ScriptedHost host(root);
    host.onOpen = [&](draw::View& content) {
        draw::Router& wr = host.routerFor(content);
        CHECK(pressIn(wr, draw::Key::Escape));
    };
    CHECK(kit::messageBox(root, L"Q", L"Quit?", {L"Yes", L"No"}, kit::MessageIcon::Question) == 1);
}

KIT_TEST(message_box_info_icon_is_the_glyph) {
    draw::View root;
    ScriptedHost host(root);
    host.onOpen = [&](draw::View& content) {
        const auto c = host.paintWindow(0);
        CHECK(c.drew("text 16,16,48,48 '\xEE\xA5\x86' 0xf0b062 5"));  // Segoe MDL2 information, in the accent
        CHECK(!c.drew("icon "));  // no icon of the set
        draw::Router& wr = host.routerFor(content);
        CHECK(pressIn(wr, draw::Key::Enter));
    };
    CHECK(kit::messageBox(root, L"Info", L"Saved.", {L"OK"}, kit::MessageIcon::Info) == 0);
}

KIT_TEST(message_box_without_icon_has_no_icon_area) {
    draw::View root;
    ScriptedHost host(root);
    host.onOpen = [&](draw::View& content) {
        const auto c = host.paintWindow(0);
        CHECK(c.drew("text 16,16,"));  // the text starts at the padding
        CHECK(!c.drew("icon "));
        draw::Router& wr = host.routerFor(content);
        CHECK(pressIn(wr, draw::Key::Enter));
    };
    CHECK(kit::messageBox(root, L"Info", L"Saved.") == 0);
}
