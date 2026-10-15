#include <string>
#include <vector>

#include "button.h"
#include "kit_check.h"
#include "menu.h"

using namespace stardraft;
using namespace stardraft::kit::test;
using draw::Key;

namespace {

kit::MenuItem entry(std::wstring label, std::function<void()> run = nullptr) {
    kit::MenuItem m;
    m.label = std::move(label);
    m.run = std::move(run);
    return m;
}
draw::Key letter(char c) { return static_cast<draw::Key>(c); }

// Closes the menus a test left open, while its host is still alive.
struct CloseMenus {
    FakeHost& host;
    ~CloseMenus() {
        kit::Menu::close();
        host.runTimers();  // the closed chain deletes itself in a timer
    }
};

}  // namespace

// The host outlives the views in these tests, as it does in a program: the views are declared after the host.

KIT_TEST(menu_bar_outside_click_on_another_name_opens_that_one) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    kit::MenuBar bar;
    root.add(bar, {0, 0, 400, 28});
    bar.setMenus({{L"File", {entry(L"New")}}, {L"Edit", {entry(L"Undo")}}});
    host.paint();
    host.click(10, 10);  // File
    CHECK(bar.openIndex() == 0);
    // The host closes File on a click outside, and that click is on Edit.
    auto closed = host.windows[0].closed;
    host.closeWindow(*host.windows[0].content);
    if (closed) closed();
    host.click(60, 10);  // Edit (File is 48 px wide)
    CHECK(bar.openIndex() == 1);
    CHECK(host.windows.size() == 1);
    host.runTimers();
}

KIT_TEST(menu_bar_deleted_while_a_menu_is_open_closes_it) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    auto* bar = new kit::MenuBar;
    root.add(*bar, {0, 0, 400, 28});
    bar->setMenus({{L"File", {entry(L"New")}}});
    host.paint();
    host.click(10, 10);
    CHECK(host.windows.size() == 1);
    delete bar;
    CHECK(host.windows.empty());
    CHECK(!kit::Menu::open());
    CHECK(host.router.focused() == nullptr);
    host.paint();
    host.runTimers();
}

KIT_TEST(menu_bar_alt_letter_opens_another_menu_while_one_is_open) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    kit::MenuBar bar;
    root.add(bar, {0, 0, 400, 28});
    bar.setMenus({{L"File", {entry(L"New")}}, {L"Edit", {entry(L"Undo")}}});
    host.paint();
    host.click(10, 10);  // File: the keys are in its menu now
    CHECK(bar.openIndex() == 0);
    CHECK(host.press(letter('E'), draw::kAlt));
    CHECK(bar.openIndex() == 1);
    CHECK(host.windows.size() == 1);
    host.press(Key::Escape);
    CHECK(host.windows.empty());
    host.runTimers();
}

KIT_TEST(menu_popup_gives_the_keys_back_to_the_view_it_opened_from) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    kit::Button button(L"Field", kit::Tone::Normal, nullptr);
    root.add(button, {10, 10, 110, 40});
    button.focus();
    int ran = 0;
    const std::vector<kit::MenuItem> items{entry(L"Copy", [&] { ++ran; }), entry(L"Paste")};
    kit::Menu::Options byKeys;
    byKeys.byKeys = true;
    CHECK(kit::Menu::popup(button, {20, 20}, items, kit::Menu::measure(items), byKeys));
    CHECK(host.router.focused() == host.windows.at(0).content);  // the menu has the keys
    host.press(Key::Escape);
    CHECK(host.windows.empty());
    CHECK(host.router.focused() == &button);  // as before the menu
    CHECK(kit::Menu::popup(button, {20, 20}, items, kit::Menu::measure(items), byKeys));
    host.press(Key::Enter);  // picks the highlighted Copy
    CHECK(ran == 1);
    CHECK(host.windows.empty());
    CHECK(host.router.focused() == &button);
    host.runTimers();
}
