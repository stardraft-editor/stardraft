// The menu bar as data (research/ports/MAC-BUILD-PLAN.md step 5): each menu's items in order, with their texts, ids and
// flags. main.cpp's buildMenu makes the Win32 menu from it (toHmenu); the kit's MenuBar can show the same model.

struct MenuItem {
    enum class Kind : uint8_t { Command, Separator, Popup };
    Kind kind = Kind::Command;
    std::wstring label;     // the text without the shortcut; an & marks the access key
    std::wstring shortcut;  // shown after the label (Ctrl+N): empty for none
    int id = 0;             // a command's id (0 for a separator or a popup)
    bool checked = false, grayed = false;
    bool reason = false;    // a grayed item whose text says why it cannot be changed (the host marks it)
    std::string recent;     // a popup the host fills at run time: "maps" or "projects" (the recent lists)
    std::vector<MenuItem> items;  // a popup's items
};

using MenuModel = std::vector<MenuItem>;
