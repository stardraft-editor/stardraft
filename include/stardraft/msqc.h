#pragma once
// MSQC (euddraft's plugin): sends what each player does with keyboard and mouse to every player, as death counts
// (or values) the triggers read. Its setup is the [MSQC] section of the project's .eds: settings, then one line per
// input, "conditions : result". GPU-free.
#include <string>
#include <vector>

namespace stardraft {

struct MsqcEntry {
    // Conditions, all of which must hold (joined by ";" in the file): KeyDown(A), KeyUp(A), KeyPress(A), a key name
    // (key down), MouseDown(L|R|M), MouseUp(..), MousePress(..), NotTyping, mouse (the mouse position), val,<v>,
    // xy,<v>[,<v>], 0x<address>[,<comparison>],<value>, or a condition written in epScript.
    std::vector<std::string> conditions;
    // What it sends: "<unit>, <amount>" (added to that death count of the player), "<array>, <amount>", a location
    // (for mouse), or units for val / xy.
    std::string result;
    bool operator==(const MsqcEntry&) const = default;
};

struct MsqcConfig {
    // Settings; empty: the plugin's default (Valkyrie, location 0, player 11, 128,128, debug on).
    std::string unit, location, player, xy, debug;
    std::vector<MsqcEntry> entries;
    bool operator==(const MsqcConfig&) const = default;
};

MsqcConfig parseMsqc(const std::vector<std::string>& lines);
std::vector<std::string> writeMsqc(const MsqcConfig& c);

// The plugin's key names (VirtualKeyCode names), in a useful order for a picker.
const std::vector<std::string>& msqcKeys();

// Keys for trigger blocks: a token usable in code names, the plugin's name, and a label.
struct MsqcKey {
    std::string token, name, label;
};
const std::vector<MsqcKey>& msqcKeyList();
// A key's Windows virtual-key code as the plugin has it (KeyCodeDict), from its name; -1 if unknown.
int msqcVirtualKey(const std::string& name);

// The inputs trigger code reads: variables named msqc_<KeyDown|KeyUp|KeyPress|MouseDown|MouseUp|MousePress>_<token>.
// Their declarations (a PVariable each, known to the plugin by its name, and the helper that turns CurrentPlayer
// into the player), and the [MSQC] lines that fill them (a key does not count while its player types in the chat).
std::string msqcDeclarations(const std::string& code);
std::vector<std::string> msqcLinesFor(const std::string& code);
// Keys and mouse buttons on this computer only (msqc_Here_<token>(), msqc_HereMouse_<L|R|M>()): a function each
// that reads the game's input state; no [MSQC] line.
// The mouse's place: msqc_MouseXNow / msqc_MouseYNow (per player) in the code make the declarations measure it
// (beforeTriggerExec), msqc_keepMouse() keep it, and the lines send it when it moves.
// Whether an [MSQC] line is one StarDraft writes (it fills an msqc_ variable).
bool msqcGeneratedLine(const std::string& line);

}  // namespace stardraft
