#include <cstdio>
#include <string>
#include <vector>

#include "kit_check.h"
#include "menu.h"

using namespace stardraft;
using namespace stardraft::kit::test;
using draw::Key;
using draw::MouseEvent;
using draw::MouseKind;

namespace {

kit::MenuItem entry(std::wstring label, std::function<void()> run = nullptr) {
    kit::MenuItem m;
    m.label = std::move(label);
    m.run = std::move(run);
    return m;
}
kit::MenuItem sep() {
    kit::MenuItem m;
    m.separator = true;
    return m;
}
kit::MenuItem off(std::wstring label, std::function<void()> run = nullptr) {
    kit::MenuItem m = entry(std::move(label), std::move(run));
    m.enabled = false;
    return m;
}
kit::MenuItem sub(std::wstring label, std::vector<kit::MenuItem> items) {
    kit::MenuItem m = entry(std::move(label));
    m.sub = std::move(items);
    return m;
}
Key letter(char c) { return static_cast<Key>(c); }
MouseEvent ev(MouseKind k, int x, int y, int button = 0) {
    MouseEvent e;
    e.kind = k, e.pt = {x, y}, e.button = button;
    return e;
}
// The content view of the host's window i (a menu's panel), in its own coordinates.
draw::View& win(FakeHost& h, size_t i) { return *h.windows.at(i).content; }
// The row a menu highlights in window w (-1: none). Rows: 5 px from the top, 30 px high; a separator 9 px.
int highlighted(FakeHost& h, size_t w, const std::vector<kit::MenuItem>& items) {
    const auto c = h.paintWindow(w);
    std::vector<int> tops;
    int top = 5;
    for (const auto& it : items) tops.push_back(top), top += it.separator ? 9 : 30;
    for (const auto& op : c.ops) {
        if (op.rfind("roundBox ", 0) != 0 || op.find("0x3f3017 0x3f3017") == std::string::npos) continue;
        int y = -1;
        std::sscanf(op.c_str(), "roundBox %*d,%d,", &y);
        for (size_t i = 0; i < tops.size(); ++i)
            if (tops[i] == y) return int(i);
    }
    return -1;
}
int countOps(const RecordingCanvas& c, const std::string& prefix, const std::string& part) {
    int n = 0;
    for (const auto& op : c.ops)
        if (op.rfind(prefix, 0) == 0 && op.find(part) != std::string::npos) ++n;
    return n;
}

// Closes the menus a test left open, while its host is still alive (the next test must not find them).
struct CloseMenus {
    FakeHost& host;
    ~CloseMenus() {
        kit::Menu::close();
        host.runTimers();  // the closed chain deletes itself in a timer
    }
};

}  // namespace

KIT_TEST(menu_keys_skip_disabled_and_separators) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    const std::vector<kit::MenuItem> items = {entry(L"A"), off(L"B"), sep(), entry(L"C"), entry(L"D")};
    CHECK(kit::Menu::popup(root, {10, 10}, items, c));
    CHECK(host.windows.size() == 1);
    CHECK(host.router.focused() == &win(host, 0));
    CHECK(highlighted(host, 0, items) == -1);
    host.press(Key::Down);
    CHECK(highlighted(host, 0, items) == 0);  // the first enabled item
    host.press(Key::Down);
    CHECK(highlighted(host, 0, items) == 3);  // the disabled B and the separator are skipped
    host.press(Key::Up);
    CHECK(highlighted(host, 0, items) == 0);
    host.press(Key::Up);  // no item above: wraps to the last
    CHECK(highlighted(host, 0, items) == 4);
    host.press(Key::Down);  // and down wraps to the first
    CHECK(highlighted(host, 0, items) == 0);
    host.press(Key::Home);
    CHECK(highlighted(host, 0, items) == 0);
    host.press(Key::End);
    CHECK(highlighted(host, 0, items) == 4);
    host.runTimers();
}

KIT_TEST(menu_enter_runs_and_closes) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    std::vector<std::string> ran;
    int closed = 0;
    kit::MenuOptions o;
    o.closed = [&](bool outside) { closed += outside ? 10 : 1; };
    const std::vector<kit::MenuItem> items = {entry(L"Copy", [&] { ran.push_back("copy"); }), off(L"Paste", [&] { ran.push_back("paste"); }),
                                              entry(L"Cut", [&] { ran.push_back("cut"); })};
    CHECK(kit::Menu::popup(root, {10, 10}, items, c, o));
    host.press(Key::Down);
    host.press(Key::Down);  // Paste is disabled: the highlight goes to Cut
    host.press(Key::Enter);
    CHECK(ran.size() == 1 && ran[0] == "cut");
    CHECK(host.windows.empty());
    CHECK(closed == 1);  // once, and not as an outside click
    CHECK(!kit::Menu::open());
    host.runTimers();
    CHECK(!host.press(Key::Enter));  // nothing is open to take the key
}

KIT_TEST(menu_escape_closes) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    int closed = 0;
    kit::MenuOptions o;
    o.closed = [&](bool) { ++closed; };
    const std::vector<kit::MenuItem> items = {entry(L"A")};
    kit::Menu::popup(root, {10, 10}, items, c, o);
    host.press(Key::Escape);
    CHECK(host.windows.empty());
    CHECK(closed == 1);
    host.runTimers();
}

KIT_TEST(menu_letter_jumps_to_label) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    const std::vector<kit::MenuItem> items = {entry(L"Open"), entry(L"Save"), entry(L"Close"), off(L"Print")};
    kit::Menu::popup(root, {10, 10}, items, c);
    host.press(letter('S'));
    CHECK(highlighted(host, 0, items) == 1);
    host.press(letter('C'));
    CHECK(highlighted(host, 0, items) == 2);
    host.press(letter('P'));  // a disabled item does not take the letter
    CHECK(highlighted(host, 0, items) == 2);
    CHECK(host.windows.size() == 1);  // a letter does not run the item
    host.press(letter('O'));
    CHECK(highlighted(host, 0, items) == 0);
    host.press(letter('Q'));  // no label starts with it
    CHECK(highlighted(host, 0, items) == 0);
    host.runTimers();
}

KIT_TEST(menu_letter_cycles_through_same_letter) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    const std::vector<kit::MenuItem> items = {entry(L"Copy"), entry(L"Cut"), entry(L"Copy as")};
    kit::Menu::popup(root, {10, 10}, items, c);
    host.press(letter('C'));
    CHECK(highlighted(host, 0, items) == 0);
    host.press(letter('C'));
    CHECK(highlighted(host, 0, items) == 1);
    host.press(letter('C'));
    CHECK(highlighted(host, 0, items) == 2);
    host.press(letter('C'));
    CHECK(highlighted(host, 0, items) == 0);
    host.runTimers();
}

KIT_TEST(menu_submenu_keys) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    int opened = 0;
    const std::vector<kit::MenuItem> top = {sub(L"File", {entry(L"New"), entry(L"Open", [&] { ++opened; }), sep(), entry(L"Exit")}), entry(L"Edit")};
    const std::vector<kit::MenuItem> file = top[0].sub;
    kit::Menu::popup(root, {10, 10}, top, c);
    host.press(Key::Right);  // nothing highlighted: nothing opens
    CHECK(host.windows.size() == 1);
    host.press(Key::Down);  // File
    host.press(Key::Right);  // opens its submenu; the keys go there, to its first item
    CHECK(host.windows.size() == 2);
    CHECK(host.router.focused() == &win(host, 1));
    CHECK(highlighted(host, 1, file) == 0);
    host.press(Key::Down);
    CHECK(highlighted(host, 1, file) == 1);
    host.press(Key::Down);
    CHECK(highlighted(host, 1, file) == 3);  // the separator is skipped
    host.press(Key::Up);
    host.press(Key::Enter);  // Open runs; the whole chain closes
    CHECK(opened == 1);
    CHECK(host.windows.empty());
    host.runTimers();
}

KIT_TEST(menu_submenu_left_and_escape_close_one_level) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    const std::vector<kit::MenuItem> top = {sub(L"File", {entry(L"New")}), entry(L"Edit"), entry(L"View")};
    kit::Menu::popup(root, {10, 10}, top, c);
    host.press(Key::Down);
    host.press(Key::Right);
    CHECK(host.windows.size() == 2);
    host.press(Key::Left);  // Left closes the submenu only
    CHECK(host.windows.size() == 1);
    CHECK(host.router.focused() == &win(host, 0));
    CHECK(highlighted(host, 0, top) == 0);  // File is still the one highlighted
    host.press(Key::Down);
    CHECK(highlighted(host, 0, top) == 1);
    host.press(Key::Up);
    host.press(Key::Right);
    CHECK(host.windows.size() == 2);
    host.press(Key::Escape);  // Escape closes the submenu only
    CHECK(host.windows.size() == 1);
    host.press(Key::Escape);  // the next Escape closes the menu
    CHECK(host.windows.empty());
    host.runTimers();
}

KIT_TEST(menu_left_at_top_without_bar_does_nothing) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    const std::vector<kit::MenuItem> items = {entry(L"A"), entry(L"B")};
    kit::Menu::popup(root, {10, 10}, items, c);
    host.press(Key::Down);
    host.press(Key::Left);
    CHECK(host.windows.size() == 1);
    CHECK(highlighted(host, 0, items) == 0);
    host.runTimers();
}

KIT_TEST(menu_hover_opens_submenu_after_a_delay) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    const std::vector<kit::MenuItem> items = {sub(L"File", {entry(L"New")}), entry(L"Edit")};
    kit::Menu::popup(root, {10, 10}, items, c);
    win(host, 0).mouse(ev(MouseKind::Move, 20, 20));  // on File
    CHECK(host.windows.size() == 1);  // not yet: the mouse has to rest there
    host.runTimers();
    CHECK(host.windows.size() == 2);
    CHECK(host.windows[1].spec.at.left == 202);  // to the right of the menu, two pixels apart
    win(host, 0).mouse(ev(MouseKind::Move, 20, 50));  // on Edit, a command: the submenu closes at once
    CHECK(host.windows.size() == 1);
    host.runTimers();
    CHECK(host.windows.size() == 1);
}

KIT_TEST(menu_hover_leaving_before_the_delay_opens_nothing) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    const std::vector<kit::MenuItem> items = {sub(L"File", {entry(L"New")}), entry(L"Edit")};
    kit::Menu::popup(root, {10, 10}, items, c);
    win(host, 0).mouse(ev(MouseKind::Move, 20, 20));
    win(host, 0).mouse(ev(MouseKind::Move, 20, 50));  // away again before the delay ends
    host.runTimers();
    CHECK(host.windows.size() == 1);
}

KIT_TEST(menu_click_runs_item_and_closes_chain) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    std::vector<std::string> ran;
    int closed = 0;
    kit::MenuOptions o;
    o.closed = [&](bool) { ++closed; };
    const std::vector<kit::MenuItem> items = {entry(L"Copy", [&] { ran.push_back("copy"); }), off(L"Paste"),
                                              sub(L"More", {entry(L"Deep", [&] { ran.push_back("deep"); })})};
    kit::Menu::popup(root, {10, 10}, items, c, o);
    win(host, 0).mouse(ev(MouseKind::Down, 20, 50));  // Paste, a disabled item (its row is 35 to 65): nothing
    win(host, 0).mouse(ev(MouseKind::Up, 20, 50));
    CHECK(host.windows.size() == 1);
    CHECK(ran.empty());
    win(host, 0).mouse(ev(MouseKind::Down, 20, 80));  // More (its row is 65 to 95) opens its submenu on the press
    CHECK(host.windows.size() == 2);
    win(host, 0).mouse(ev(MouseKind::Up, 20, 80));  // and the release picks nothing there
    CHECK(host.windows.size() == 2);
    win(host, 1).mouse(ev(MouseKind::Down, 20, 20));
    win(host, 1).mouse(ev(MouseKind::Up, 20, 20));  // Deep runs; the chain closes
    CHECK(ran.size() == 1 && ran[0] == "deep");
    CHECK(host.windows.empty());
    CHECK(closed == 1);
    host.runTimers();
}

KIT_TEST(menu_click_on_command_runs_it) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    int ran = 0;
    const std::vector<kit::MenuItem> items = {entry(L"Copy", [&] { ++ran; })};
    kit::Menu::popup(root, {10, 10}, items, c);
    win(host, 0).mouse(ev(MouseKind::Down, 20, 20));
    CHECK(ran == 0);  // a press only: the item runs on the release
    win(host, 0).mouse(ev(MouseKind::Up, 20, 20));
    CHECK(ran == 1);
    CHECK(host.windows.empty());
    host.runTimers();
}

KIT_TEST(menu_outside_click_closes_all_once) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    int closed = 0, outsideSeen = 0;
    kit::MenuOptions o;
    o.closed = [&](bool outside) { ++closed, outsideSeen += outside ? 1 : 0; };
    const std::vector<kit::MenuItem> items = {sub(L"File", {entry(L"New")})};
    kit::Menu::popup(root, {10, 10}, items, c, o);
    host.press(Key::Down);
    host.press(Key::Right);
    CHECK(host.windows.size() == 2);
    // The host closes the root window on a click outside (its own close, then the callback).
    auto outside = host.windows[0].closed;
    host.closeWindow(*host.windows[0].content);
    if (outside) outside();
    CHECK(host.windows.empty());
    host.runTimers();
    CHECK(closed == 1);
    CHECK(outsideSeen == 1);
    CHECK(!kit::Menu::open());
}

KIT_TEST(menu_measure_from_text) {
    RecordingCanvas c;
    const std::vector<kit::MenuItem> wide = {entry(std::wstring(40, L'M')), sep(), entry(L"Short")};
    const auto a = kit::Menu::measure(wide, &c);
    CHECK(a.size.x == 34 + 280 + 18 + 10);  // icon column, the text, the right margin, the padding
    CHECK(a.size.y == 5 + 30 + 9 + 30 + 5);
    std::vector<kit::MenuItem> keyed = wide;
    keyed[2].shortcut = L"Ctrl+Shift+Z";  // 12 characters: 84 px, after a gap of 36
    CHECK(kit::Menu::measure(keyed, &c).size.x == 34 + 280 + 36 + 84 + 18 + 10);
    const auto small = kit::Menu::measure({entry(L"A")}, &c);
    CHECK(small.size.x == 200);  // never narrower than 200 px
    const auto withSub = kit::Menu::measure({sub(L"A", {entry(L"B")})}, &c);
    CHECK(withSub.size.x == 200);
    CHECK(withSub.sub.size() == 1 && withSub.sub[0].size.x == 200);
    CHECK(kit::Menu::measure({entry(std::wstring(40, L'M')), sub(L"X", {entry(L"Y")})}, &c).size.x == 34 + 280 + 22 + 18 + 10);
    CHECK(kit::Menu::measure(wide).size.x == 200);  // without a canvas the text counts as empty
}

KIT_TEST(menu_paints_labels_icons_checks_and_chevrons) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    RecordingCanvas c;
    kit::MenuItem open = entry(L"Open");
    open.icon = 5;
    open.shortcut = L"Ctrl+O";
    kit::MenuItem save = entry(L"Save");
    save.checked = true;
    const std::vector<kit::MenuItem> items = {open, save, sep(), sub(L"Recent", {entry(L"a")}), off(L"Print")};
    kit::Menu::popup(root, {10, 10}, items, c);
    const auto p = host.paintWindow(0);
    CHECK((p.texts() == std::vector<std::string>{"Open", "Ctrl+O", "Save", "Recent", "Print"}));
    CHECK(countOps(p, "icon ", " 5 0x") == 1);   // the icon of Open
    CHECK(countOps(p, "icon ", " 76 0x") == 1);  // the check mark of Save
    CHECK(countOps(p, "polyline ", "0x") == 1);  // the chevron of Recent
    CHECK(countOps(p, "fill ", "0x") >= 1);      // the separator's line
    CHECK(countOps(p, "roundBox 0,0,", "0x202020") == 1);  // the panel's ground
    CHECK(p.drew("'Print' 0x605a5a"));           // a disabled item is faint
    CHECK(p.drew("'Open' 0xe0e0e0"));
    host.runTimers();
}

KIT_TEST(menu_bar_click_opens_and_arrows_move_between_menus) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    kit::MenuBar bar;
    root.add(bar, {0, 0, 400, 28});
    bar.setMenus({{L"File", {entry(L"New")}}, {L"Edit", {entry(L"Undo")}}, {L"View", {entry(L"Zoom")}}});
    host.paint();  // the names are laid out when the bar is painted
    host.click(10, 10);  // File
    CHECK(host.windows.size() == 1);
    CHECK(bar.openIndex() == 0);
    host.press(Key::Right);  // the neighbour opens; the old menu closes
    CHECK(host.windows.size() == 1);
    CHECK(bar.openIndex() == 1);
    host.press(Key::Right);
    CHECK(bar.openIndex() == 2);
    host.press(Key::Right);  // wraps to the first
    CHECK(bar.openIndex() == 0);
    host.press(Key::Left);  // and back
    CHECK(bar.openIndex() == 2);
    host.press(Key::Escape);  // Escape closes the menu; the keys are the bar's again
    CHECK(host.windows.empty());
    CHECK(bar.openIndex() == -1);
    CHECK(host.router.focused() == &bar);
    host.runTimers();
}

KIT_TEST(menu_bar_keys_open_and_alt_letters) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    kit::MenuBar bar;
    root.add(bar, {0, 0, 400, 28});
    bar.setMenus({{L"File", {entry(L"New")}}, {L"Edit", {entry(L"Undo")}}, {L"View", {entry(L"Zoom")}}});
    host.paint();
    bar.focus();
    host.press(Key::Right);  // no name highlighted yet: the first
    host.press(Key::Right);
    CHECK(bar.openIndex() == -1);
    host.press(Key::Down);  // opens the highlighted name
    CHECK(bar.openIndex() == 1);
    CHECK(host.windows.size() == 1);
    host.press(Key::Escape);
    CHECK(bar.openIndex() == -1);
    CHECK(host.router.focused() == &bar);
    host.press(letter('V'), draw::kAlt);  // Alt and the first letter of a name
    CHECK(bar.openIndex() == 2);
    host.press(Key::Escape);
    host.press(letter('Q'), draw::kAlt);  // no name starts with it
    CHECK(bar.openIndex() == -1);
    host.press(letter('F'));  // a letter without Alt does nothing
    CHECK(bar.openIndex() == -1);
    host.runTimers();
}

KIT_TEST(menu_bar_outside_click_does_not_reopen_on_its_press) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    kit::MenuBar bar;
    root.add(bar, {0, 0, 400, 28});
    bar.setMenus({{L"File", {entry(L"New")}}, {L"Edit", {entry(L"Undo")}}});
    host.paint();
    host.click(10, 10);
    CHECK(bar.openIndex() == 0);
    // A click outside: the host closes the menu, and the press that caused it goes to the bar.
    auto closed = host.windows[0].closed;
    host.closeWindow(*host.windows[0].content);
    if (closed) closed();
    CHECK(bar.openIndex() == -1);
    host.click(10, 10);  // this press closed the menu: it does not open it again
    CHECK(bar.openIndex() == -1);
    CHECK(host.windows.empty());
    host.click(10, 10);
    CHECK(bar.openIndex() == 0);
    host.runTimers();
}

KIT_TEST(menu_bar_click_on_open_name_closes_it) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    kit::MenuBar bar;
    root.add(bar, {0, 0, 400, 28});
    bar.setMenus({{L"File", {entry(L"New")}}});
    host.paint();
    host.click(10, 10);
    CHECK(bar.openIndex() == 0);
    host.click(10, 10);  // (the host would have closed it already; here the bar does it)
    CHECK(bar.openIndex() == -1);
    CHECK(host.windows.empty());
    host.runTimers();
}

KIT_TEST(menu_bar_paints_names) {
    draw::View root;
    FakeHost host(root);
    CloseMenus closeMenus{host};
    kit::MenuBar bar;
    root.add(bar, {0, 0, 400, 28});
    bar.setMenus({{L"File", {}}, {L"Edit", {}}});
    const auto c = host.paint();
    CHECK((c.texts() == std::vector<std::string>{"File", "Edit"}));
}
