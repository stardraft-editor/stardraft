// The command table (stardraft/commands.h): shortcuts read, commands found and run by id, enabled and checked, the
// commands of a menu in order. Needs no game data.
//
// usage: commands_test

#include <cstdio>
#include <string>

#include "stardraft/commands.h"

using namespace stardraft;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
    if (!ok) ++failures;
}

bool chord(const char* text, bool ctrl, bool shift, bool alt, const char* key) {
    const auto k = parseShortcut(text);
    return k && k->ctrl == ctrl && k->shift == shift && k->alt == alt && k->key == key;
}

}  // namespace

int main() {
    check(chord("T", false, false, false, "T") && chord("t", false, false, false, "T"), "a letter (upper case)");
    check(chord("Ctrl+Shift+S", true, true, false, "S") && chord("Alt+F4", false, false, true, "F4"), "modifiers");
    check(chord("Ctrl+0", true, false, false, "0") && chord("+", false, false, false, "+") && chord("Ctrl++", true, false, false, "+"),
          "digits and signs (Ctrl++ is Ctrl and +)");
    check(chord("Ctrl+,", true, false, false, ",") && chord("Del", false, false, false, "Del") && chord("F12", false, false, false, "F12"), "named keys");
    check(!parseShortcut("") && !parseShortcut("Ctrl+") && !parseShortcut("F25") && !parseShortcut("Shift+Banana"), "not a shortcut");

    Commands cmds;
    int grid = 0;
    bool on = false, can = true;
    Command c;
    c.id = "view.grid", c.label = "Show &grid", c.menu = "View", c.shortcut = "G";
    c.run = [&] { ++grid, on = !on; };
    c.checked = [&] { return on; };
    c.enabled = [&] { return can; };
    cmds.add(c);
    Command z;
    z.id = "view.fit", z.label = "&Fit map", z.menu = "View", z.run = [] {};
    cmds.add(z);
    Command l;
    l.id = "view.layer.units", l.menu = "View/Layers", l.run = [] {};
    cmds.add(l);
    check(cmds.find("view.grid") && !cmds.find("view.nothing"), "found by id");
    check(cmds.run("view.grid") && grid == 1 && cmds.find("view.grid")->isChecked(), "run by id; checked");
    can = false;
    check(!cmds.run("view.grid") && grid == 1, "a command that cannot run now does not run");
    check(!cmds.run("view.nothing"), "no such command");
    const auto view = cmds.inMenu("View");
    check(view.size() == 2 && view[0]->id == "view.grid" && view[1]->id == "view.fit", "a menu's commands in order (not its submenus)");
    Command again = z;
    again.label = "Fit";
    cmds.add(again);
    check(cmds.all().size() == 3 && cmds.find("view.fit")->label == "Fit", "the same id replaces the command");
    check(!cmds.find("view.fit")->isChecked() && cmds.find("view.fit")->canRun(), "no checked() is not checked; no enabled() can run");

    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
