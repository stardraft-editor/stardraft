#pragma once
// A dropdown (docs/dev/CONTROLS.md, "Dropdown"): a field with the value's icon and name, its number at the right and a
// chevron. A click, Alt+Down, Space or Enter opens the list under it (a popup window of up to 12 rows; the wheel
// scrolls it). Up and Down, and the mouse wheel over the closed field, step the value to the next or the previous item.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "view.h"

namespace stardraft::kit {

struct DropdownItem {
    std::wstring label;
    int icon = -1;    // a UI icon (apps/editor/res/ui_icons.h); -1: none
    int number = -1;  // shown at the right in dim; -1: none
    // A color square before the label, on the closed field (a player's color); kNone: none.
    draw::Color swatch = draw::kNone;
};

class Dropdown : public draw::View {
public:
    Dropdown();  // out of line: the list it may own is incomplete here
    explicit Dropdown(std::vector<DropdownItem> items, int value = -1);
    ~Dropdown() override;

    // The value changed to index (by the list, the wheel or the keys). Not for setValue.
    std::function<void(int)> onChange;
    // The item that is already the value was picked again (the list closes, no change): a Custom color's picker opens.
    std::function<void(int)> onSame;

    // Replaces the items (the list closes; the value stays if it is still an item).
    void setItems(std::vector<DropdownItem> items);
    const std::vector<DropdownItem>& items() const { return items_; }
    // The index of the value (-1: none). Sets it without onChange.
    void setValue(int i);
    int value() const { return value_; }
    void setEnabled(bool e);
    bool enabled() const { return enabled_; }
    void setTip(std::wstring t) { tip_ = std::move(t); }
    // The green Changed look (a value that differs from the saved one, as the program's pickers show it).
    void setChanged(bool on);
    bool changed() const { return changed_; }
    bool listOpen() const { return listOpen_; }
    // Whether any dropdown's list is open (the page's keys go to its type-to-jump, as to a combo box's list).
    static bool anyListOpen();
    // Opens the list under the field (nothing when it is disabled or has no items).
    void openList();
    // Closes the list without a change.
    void closeList();

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point) override { return enabled_ ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    std::wstring tip(draw::Point) override { return tip_; }
    bool focusable() const override { return enabled_; }
    void focusChanged(bool) override { invalidate(); }

private:
    class List;
    void pick(int i);
    void step(int dir);
    void setOpen(bool on);

    std::vector<DropdownItem> items_;
    std::wstring tip_;
    int value_ = -1;
    bool enabled_ = true, listOpen_ = false, hover_ = false, justClosed_ = false, changed_ = false;
    float wheelAcc_ = 0;
    // Lets go of the list (the host forgets it) and drops the host: a closed list keeps no host, so nothing touches a
    // host that is gone (a page's window goes before its views). The list is deleted by a timer on the host: its own
    // event may be the one running.
    void retireList();

    std::unique_ptr<List> list_;  // the list made for the open list (retired when it closes)
    // The host that shows the list while it is open. A host goes when its window goes; a closed list does not use it.
    draw::Host* listHost_ = nullptr;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // what the list's callbacks check
};

}  // namespace stardraft::kit
