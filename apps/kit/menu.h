#pragma once
// Pop-up menus and the menu bar (docs/dev/CONTROLS.md, "Menus"). A menu is a rounded panel of rows: an icon or a check
// mark, the label, the shortcut in dim at the right, a chevron for a submenu. Mouse and keys work as in Windows menus.
// A menu is a window the host shows (Host::openWindow); each submenu is another window. One menu chain is open at a
// time: a new menu closes the one before it.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "view.h"

namespace stardraft::kit {

struct MenuItem {
    std::wstring label;
    std::wstring shortcut;        // at the right, in dim (e.g. L"Ctrl+Z"); empty: none
    int icon = -1;                // a UI icon (apps/editor/res/ui_icons.h); -1: none
    draw::Color iconColor = draw::kNone;  // the icon's color while enabled (the feedback item's); kNone: the text's
    bool checked = false;         // a check mark in the icon column (it takes the place of the icon)
    bool radio = false;           // one choice of a group: a checked one shows a bullet, not a check mark
    bool enabled = true;          // a disabled item is faint and cannot be picked or highlighted
    bool separator = false;       // a line between groups (the other fields are not used)
    std::vector<MenuItem> sub;    // a submenu's items; an item with items here opens them
    std::function<void()> run;    // what the item does; it runs after the menus have closed
};

// The size of a menu's panel, and the sizes of the submenus: sub[i] is the size of the submenu of item i (empty when
// the item has none). Made once with the canvas that measures the text, so opening a menu needs no canvas.
struct MenuLayout {
    draw::Point size;
    std::vector<MenuLayout> sub;
};

// What the menu tells its owner (see Menu::popup).
struct MenuOptions {
    // The menus closed, for whatever reason (an item was picked, Escape, a click outside). Runs once; outside: the
    // host closed them because of a click outside.
    std::function<void(bool outside)> closed;
    // Left or Right at the top level, when there is no submenu to open or close: the menu closes, and this runs
    // with -1 or +1 (the menu bar opens its neighbour with it).
    std::function<void(int dir)> sideways;
    // The first enabled item is highlighted when the menu opens (it was opened by the keys).
    bool byKeys = false;
    // When the menus close without an outside click, the keys go back to the view they were opened from. That view
    // must outlive the menus. false: the owner gives the keys back itself (in `closed`).
    bool focusFrom = true;
    // Alt with a letter while the menus have the keys. Returns true when it opened a menu of that name (the menu bar).
    std::function<bool(int letter)> letter;
};

class Menu {
public:
    using Options = MenuOptions;

    // The size of the menu for `items` (its text measured with `c`; without a canvas the text is taken as empty).
    static MenuLayout measure(const std::vector<MenuItem>& items, draw::Canvas* c = nullptr);

    // Opens `items` at `at` (in `from`'s coordinates). Returns false when `from` is not in a host.
    static bool popup(draw::View& from, draw::Point at, const std::vector<MenuItem>& items, const MenuLayout& layout, Options opts = {});
    static bool popup(draw::View& from, draw::Point at, const std::vector<MenuItem>& items, draw::Canvas& c, Options opts = {});

    // A test run's look at each menu before it opens (a snapshot has no windows): the hook gets the items; when it
    // returns true the menu does not open (the hook ran an item), and the closed callback runs. Empty: none.
    using TestHook = std::function<bool(const std::vector<MenuItem>& items)>;
    static void setTestHook(TestHook hook);

    // The system's own menus in place of the kit's (macOS: NSMenu at the pointer). The hook shows the items, waits
    // until the menu closes and returns true; `picked` is then the item that was picked (nullptr: none). The menu's
    // closed callback runs, then the item's run. The test hook comes first. Empty: the kit's menus.
    using NativeHook = std::function<bool(draw::View& from, draw::Point at, const std::vector<MenuItem>& items, const MenuItem*& picked)>;
    static void setNativeHook(NativeHook hook);

    // How a shortcut shows in the menus (macOS: the Mac symbols, Cmd+S for Ctrl+S). Empty: as the item has it.
    using ShortcutText = std::function<std::wstring(const std::wstring&)>;
    static void setShortcutText(ShortcutText f);
    static std::wstring shortcutText(const std::wstring& s);

    // Closes the open menus, if any (the closed callback runs).
    static void close();
    static bool open();
};

struct MenuGroup {
    std::wstring label;  // the name in the bar
    std::vector<MenuItem> items;
};

// The menu bar: the names of the menus in a row. A click on a name, or Enter, Space or Down on the focused name, opens
// its menu under it. While a menu is open, Left and Right open the neighbouring menu. Alt with the first letter of a
// name opens that menu (Alt on its own is not a key the view system has).
class MenuBar : public draw::View {
public:
    void setMenus(std::vector<MenuGroup> groups);
    const std::vector<MenuGroup>& menus() const { return menus_; }
    // The menu that is open (-1: none).
    int openIndex() const { return open_; }
    // Opens menu `i`. byKeys: its first enabled item is highlighted.
    void openMenu(int i, bool byKeys = false);

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point) override { return draw::Cursor::Hand; }
    bool focusable() const override { return true; }
    void focusChanged(bool has) override;
    void tabbedTo(bool back) override;
    std::wstring focusedItem() const override;
    ~MenuBar() override;

private:
    void layout(draw::Canvas& c);
    int nameAt(draw::Point pt) const;
    bool openByLetter(int letter);

    std::vector<MenuGroup> menus_;
    std::vector<draw::Rect> names_;    // where each name is, from the last measure
    std::vector<MenuLayout> layouts_;  // each menu's size, from the last measure
    bool measured_ = false;            // names_ and layouts_ are up to date (see setMenus and paint)
    float measuredScale_ = 0;          // the display scale they were measured at
    int open_ = -1, hot_ = -1, hover_ = -1;
    bool keys_ = false;                // the keyboard moved the focus to hot_ (its outline shows; not after a click)
    int closedAt_ = -1;                // a click outside closed the menu at this name: its press does not open it again
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // what the menus' callbacks check before they touch us
};

}  // namespace stardraft::kit
