#include "stardraft/msqc.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <set>

namespace stardraft {

const std::vector<std::string>& msqcKeys() {
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> k;
        for (char c = 'A'; c <= 'Z'; ++c) k.push_back(std::string(1, c));
        for (char c = '0'; c <= '9'; ++c) k.push_back(std::string(1, c));
        for (int f = 1; f <= 12; ++f) k.push_back("F" + std::to_string(f));
        for (const char* n : {"SPACE", "ENTER", "ESC", "TAB", "BACK", "SHIFT", "LSHIFT", "RSHIFT", "LCTRL", "RCTRL", "LALT", "RALT", "LEFT", "UP", "RIGHT",
                              "DOWN", "INSERT", "DELETE", "HOME", "END", "PGUP", "PGDN", "CAPSLOCK", "NUMLOCK", "SCROLL", "PAUSE", "NUMPAD0", "NUMPAD1",
                              "NUMPAD2", "NUMPAD3", "NUMPAD4", "NUMPAD5", "NUMPAD6", "NUMPAD7", "NUMPAD8", "NUMPAD9", "NUMPAD*", "NUMPAD+", "NUMPAD-",
                              "NUMPAD.", "NUMPAD/", "`", "-", "=", "[", "]", "|", "SEMICOLON", "'", ",", ".", "/", "LWIN", "RWIN", "APPS"})
            k.push_back(n);
        return k;
    }();
    return keys;
}

const std::vector<MsqcKey>& msqcKeyList() {
    static const std::vector<MsqcKey> keys = [] {
        static const std::pair<const char*, std::pair<const char*, const char*>> symbols[] = {
            {"NUMPAD*", {"NUMPAD_MUL", "Numpad *"}}, {"NUMPAD+", {"NUMPAD_ADD", "Numpad +"}}, {"NUMPAD-", {"NUMPAD_SUB", "Numpad -"}},
            {"NUMPAD.", {"NUMPAD_DOT", "Numpad ."}}, {"NUMPAD/", {"NUMPAD_DIV", "Numpad /"}}, {"`", {"GRAVE", "`"}}, {"-", {"MINUS", "-"}},
            {"=", {"EQUALS", "="}}, {"[", {"LBRACKET", "["}}, {"]", {"RBRACKET", "]"}}, {"|", {"BACKSLASH", "\\"}}, {"'", {"QUOTE", "'"}},
            {",", {"COMMA", ","}}, {".", {"PERIOD", "."}}, {"/", {"SLASH", "/"}}, {"SEMICOLON", {"SEMICOLON", ";"}}};
        static const std::pair<const char*, const char*> labels[] = {
            {"SPACE", "Space"}, {"ENTER", "Enter"}, {"ESC", "Esc"}, {"TAB", "Tab"}, {"BACK", "Backspace"}, {"SHIFT", "Shift"}, {"LSHIFT", "Left Shift"},
            {"RSHIFT", "Right Shift"}, {"LCTRL", "Left Ctrl"}, {"RCTRL", "Right Ctrl"}, {"LALT", "Left Alt"}, {"RALT", "Right Alt"}, {"LEFT", "Left arrow"},
            {"UP", "Up arrow"}, {"RIGHT", "Right arrow"}, {"DOWN", "Down arrow"}, {"INSERT", "Insert"}, {"DELETE", "Delete"}, {"HOME", "Home"},
            {"END", "End"}, {"PGUP", "Page Up"}, {"PGDN", "Page Down"}, {"CAPSLOCK", "Caps Lock"}, {"NUMLOCK", "Num Lock"}, {"SCROLL", "Scroll Lock"},
            {"PAUSE", "Pause"}, {"LWIN", "Left Windows"}, {"RWIN", "Right Windows"}, {"APPS", "Menu"}};
        std::vector<MsqcKey> out;
        for (const auto& name : msqcKeys()) {
            MsqcKey k{name, name, name};
            for (const auto& [n, tl] : symbols)
                if (name == n) k.token = tl.first, k.label = tl.second;
            for (const auto& [n, l] : labels)
                if (name == n) k.label = l;
            if (name.rfind("NUMPAD", 0) == 0 && name.size() == 7) k.label = "Numpad " + name.substr(6);
            out.push_back(k);
        }
        return out;
    }();
    return keys;
}

namespace {
struct UsedInput {
    std::string kind, token;
    bool operator<(const UsedInput& o) const { return kind != o.kind ? kind < o.kind : token < o.token; }
};
// The names msqc_<kind>_<token> in the code, one of `kinds` each (the first that fits, then _ and the token's letters,
// digits and _; with `paren`, a ( right after the token), left to right, each after the one before: as a regular
// expression's matches, without one (std::regex took a large part of the check of a large trigger set).
std::set<UsedInput> usedNames(const std::string& code, std::initializer_list<const char*> kinds, bool paren) {
    std::set<UsedInput> out;
    auto tokenChar = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; };
    for (size_t at = code.find("msqc_"); at != std::string::npos; at = code.find("msqc_", at)) {
        const size_t k = at + 5;
        bool found = false;
        for (const char* kind : kinds) {
            const size_t n = std::char_traits<char>::length(kind);
            if (code.compare(k, n, kind) != 0 || k + n >= code.size() || code[k + n] != '_') continue;
            size_t end = k + n + 1;
            while (end < code.size() && tokenChar(code[end])) ++end;
            if (end == k + n + 1 || (paren && (end >= code.size() || code[end] != '('))) continue;
            out.insert({kind, code.substr(k + n + 1, end - (k + n + 1))});
            at = end + (paren ? 1 : 0), found = true;
            break;
        }
        if (!found) ++at;
    }
    return out;
}

std::set<UsedInput> usedInputs(const std::string& code) {
    return usedNames(code, {"KeyDown", "KeyUp", "KeyPress", "MouseDown", "MouseUp", "MousePress"}, false);
}
}  // namespace

namespace {
bool usesMouse(const std::string& code) { return code.find("msqc_MouseXNow") != std::string::npos || code.find("msqc_MouseYNow") != std::string::npos; }

// Keys and mouse buttons read on this computer only (msqc_Here_<token>, msqc_HereMouse_<L|R|M>): no network.
std::set<UsedInput> usedHere(const std::string& code) { return usedNames(code, {"Here", "HereMouse"}, true); }
}  // namespace

int msqcVirtualKey(const std::string& name) {
    // The plugin's KeyCodeDict (MSQC.py), for the keys msqcKeys() lists.
    static const std::pair<const char*, int> codes[] = {
        {"SPACE", 0x20}, {"ENTER", 0x0D}, {"ESC", 0x1B}, {"TAB", 0x09}, {"BACK", 0x08}, {"SHIFT", 0x10}, {"LSHIFT", 0xA0}, {"RSHIFT", 0xA1},
        {"LCTRL", 0x11}, {"RCTRL", 0x19}, {"LALT", 0x12}, {"RALT", 0x15}, {"LEFT", 0x25}, {"UP", 0x26}, {"RIGHT", 0x27}, {"DOWN", 0x28},
        {"INSERT", 0x2D}, {"DELETE", 0x2E}, {"HOME", 0x24}, {"END", 0x23}, {"PGUP", 0x21}, {"PGDN", 0x22}, {"CAPSLOCK", 0x14},
        {"NUMLOCK", 0x90}, {"SCROLL", 0x91}, {"PAUSE", 0x13}, {"NUMPAD*", 0x6A}, {"NUMPAD+", 0x6B}, {"NUMPAD-", 0x6D}, {"NUMPAD.", 0x6E},
        {"NUMPAD/", 0x6F}, {"`", 0xC0}, {"-", 0xBD}, {"=", 0xBB}, {"[", 0xDB}, {"]", 0xDD}, {"|", 0xDC}, {"SEMICOLON", 0xBA}, {"'", 0xDE},
        {",", 0xBC}, {".", 0xBE}, {"/", 0xBF}, {"LWIN", 0x5B}, {"RWIN", 0x5C}, {"APPS", 0x5D}};
    if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) return name[0];
    if (name.size() >= 2 && name[0] == 'F' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        const int f = std::atoi(name.c_str() + 1);
        if (f >= 1 && f <= 24) return 0x6F + f;
    }
    if (name.rfind("NUMPAD", 0) == 0 && name.size() == 7 && std::isdigit(static_cast<unsigned char>(name[6]))) return 0x60 + (name[6] - '0');
    for (const auto& [n, c] : codes)
        if (name == n) return c;
    return -1;
}

std::string msqcDeclarations(const std::string& code) {
    const auto used = usedInputs(code);
    const auto here = usedHere(code);
    const bool mouse = usesMouse(code);
    if (used.empty() && !mouse && here.empty()) return {};
    std::string o;
    for (const auto& u : here) {
        // The game's key states (a byte per virtual key at 0x596A18, not while the player types in the chat: 0x68C144)
        // and mouse buttons (0x6CDDC0: left 2, right 8, middle 32) on this computer.
        char cond[160] = "";
        if (u.kind == "Here") {
            std::string name = u.token;
            for (const auto& k : msqcKeyList())
                if (k.token == u.token) name = k.name;
            const int vk = msqcVirtualKey(name);
            if (vk < 0) continue;
            std::snprintf(cond, sizeof cond, "MemoryX(0x%06X, Exactly, 0x%X, 0x%X) && Memory(0x68C144, Exactly, 0)", 0x596A18 + (vk & ~3), 1u << (8 * (vk & 3)),
                          1u << (8 * (vk & 3)));
        } else {
            const int bit = u.token == "L" ? 2 : u.token == "R" ? 8 : u.token == "M" ? 32 : 0;
            if (!bit) continue;
            std::snprintf(cond, sizeof cond, "MemoryX(0x6CDDC0, Exactly, %d, %d)", bit, bit);
        }
        o += "function msqc_" + u.kind + "_" + u.token + "() {  // this computer only\n    if (" + cond + ") return 1;\n    return 0;\n}\n";
    }
    if (mouse) {
        // Each player's own mouse on the map (screen place + mouse place), and whether it moved since the last cycle;
        // MSQC sends it when it moved (as x, y), and the place each player had last is kept.
        o += "var msqc_localMouseX = 0;\nvar msqc_localMouseY = 0;\nvar msqc_moved = 0;\n"
             "const msqc_MouseX = PVariable();\nconst msqc_MouseY = PVariable();\n"
             "const msqc_MouseXNow = PVariable();\nconst msqc_MouseYNow = PVariable();\n";
        for (const char* n : {"msqc_localMouseX", "msqc_localMouseY", "msqc_moved", "msqc_MouseX", "msqc_MouseY"})
            o += std::string("const _ns_") + n + " = EUDRegisterObjectToNamespace(\"" + n + "\", " + n + ");\n";
        o += "function beforeTriggerExec() {  // before MSQC sends: this player's mouse on the map\n"
             "    const x = dwread_epd(EPD(0x62848C)) + dwread_epd(EPD(0x6CDDC4));\n"
             "    const y = dwread_epd(EPD(0x6284A8)) + dwread_epd(EPD(0x6CDDC8));\n"
             "    if (x != msqc_localMouseX || y != msqc_localMouseY) { msqc_moved = 1; } else { msqc_moved = 0; }\n"
             "    msqc_localMouseX = x;\n    msqc_localMouseY = y;\n}\n"
             "function msqc_keepMouse() {  // what MSQC received this cycle (-1: nothing): each player's last place\n"
             "    for (var p = 0; p < 8; p++) {\n"
             "        if (msqc_MouseX[p] != -1) { msqc_MouseXNow[p] = msqc_MouseX[p]; msqc_MouseYNow[p] = msqc_MouseY[p]; }\n"
             "    }\n}\n";
    }
    for (const auto& u : used) {
        const std::string v = "msqc_" + u.kind + "_" + u.token;
        o += "const " + v + " = PVariable();\nconst _ns_" + v + " = EUDRegisterObjectToNamespace(\"" + v + "\", " + v + ");\n";
    }
    o += "function msqc_player(p) {  // CurrentPlayer as the player it is\n    if (p == 13) return getcurpl();\n    return p;\n}\n";
    return o;
}

std::vector<std::string> msqcLinesFor(const std::string& code) {
    std::vector<std::string> out;
    for (const auto& u : usedInputs(code)) {
        std::string name = u.token;
        for (const auto& k : msqcKeyList())
            if (k.token == u.token) name = k.name;
        const bool key = u.kind.rfind("Key", 0) == 0;
        out.push_back(u.kind + "(" + name + ")" + (key ? "; NotTyping" : "") + " : msqc_" + u.kind + "_" + u.token + ", 1");
    }
    if (usesMouse(code)) out.push_back("msqc_moved.Exactly(1); xy, msqc_localMouseX, msqc_localMouseY : msqc_MouseX, msqc_MouseY");
    for (int k = 1; code.find("var msqc_share" + std::to_string(k) + "_local") != std::string::npos; ++k) {
        const std::string s = "msqc_share" + std::to_string(k);
        out.push_back(s + "_send.Exactly(1); xy, " + s + "_local : " + s + "_recv");
    }
    return out;
}

bool msqcGeneratedLine(const std::string& line) {
    const size_t colon = line.find(':');
    return colon != std::string::npos && line.find("msqc_", colon) != std::string::npos;
}

}  // namespace stardraft
