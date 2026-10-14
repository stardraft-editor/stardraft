#pragma once

// The program's commands as data: each user action has an id, a label, a place in the menus, a shortcut, and
// functions that say whether it can run now, whether it is checked, and that run it. A shell (the Win32 one today)
// makes its menus and keys from this table and calls a command by its id; the action itself is in one place.

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace stardraft {

// A key with its modifiers, as a shortcut text names it: "Ctrl+Shift+S", "T", "Ctrl+0", "F2", "+", "Del".
struct KeyChord {
    bool ctrl = false, shift = false, alt = false;
    std::string key;  // one character (upper case for letters) or a name: "F1".."F24", "Del", "Tab", "Enter", ","
};
// The chord of a shortcut text; nothing when the text is not one.
std::optional<KeyChord> parseShortcut(const std::string& text);

struct Command {
    std::string id;        // "view.grid": what the shell and the tests call it by
    std::string label;     // the menu text, UTF-8, '&' before the access key: "Show &grid"
    std::string menu;      // the menu it is in, '/' between levels: "View", "View/Layers"; empty: in no menu
    std::string shortcut;  // shown after the label, and the key that runs it (see `accelerator`): "G"
    bool accelerator = true;  // false: the shortcut is only shown; the shell's own key handling gives that key
    bool radio = false;       // checked() marks it as one of a group (a dot, not a check mark)
    std::function<void()> run;
    std::function<bool()> enabled;  // empty: always
    std::function<bool()> checked;  // empty: never checked (no mark)

    bool canRun() const { return run && (!enabled || enabled()); }
    bool isChecked() const { return checked && checked(); }
};

class Commands {
public:
    // Adds a command (one with the same id replaces it). In each menu, the commands are in the order they were added.
    void add(Command c);
    const Command* find(const std::string& id) const;
    // Runs a command when it can run; false when there is no such command or it cannot run now.
    bool run(const std::string& id) const;
    const std::vector<Command>& all() const { return commands_; }
    // The commands in one menu (not its submenus), in order.
    std::vector<const Command*> inMenu(const std::string& menu) const;

private:
    std::vector<Command> commands_;
};

}  // namespace stardraft
