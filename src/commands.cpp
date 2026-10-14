#include "stardraft/commands.h"

#include <algorithm>
#include <cctype>

namespace stardraft {

std::optional<KeyChord> parseShortcut(const std::string& text) {
    KeyChord k;
    std::string rest = text;
    // Modifiers first ("Ctrl+", "Shift+", "Alt+"); the key is what is left ("Ctrl++" is Ctrl and '+').
    for (bool more = true; more;) {
        more = false;
        for (auto [name, flag] : {std::pair{"Ctrl+", &k.ctrl}, std::pair{"Shift+", &k.shift}, std::pair{"Alt+", &k.alt}}) {
            const std::string n = name;
            if (rest.size() > n.size() && rest.compare(0, n.size(), n) == 0) rest.erase(0, n.size()), *flag = true, more = true;
        }
    }
    if (rest.empty()) return std::nullopt;
    if (rest.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(rest[0]);
        if (!std::isprint(c) || c == ' ') return std::nullopt;
        k.key = std::string(1, char(std::toupper(c)));
        return k;
    }
    static const char* const kNames[] = {"Del", "Tab", "Enter", "Esc", "Home", "End", "PgUp", "PgDn", "Left", "Right", "Up", "Down", "Ins", "Space"};
    if (std::find_if(std::begin(kNames), std::end(kNames), [&](const char* n) { return rest == n; }) != std::end(kNames)) {
        k.key = rest;
        return k;
    }
    if (rest[0] == 'F' && rest.size() <= 3 && std::all_of(rest.begin() + 1, rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        const int n = std::stoi(rest.substr(1));
        if (n < 1 || n > 24) return std::nullopt;
        k.key = rest;
        return k;
    }
    return std::nullopt;
}

void Commands::add(Command c) {
    for (Command& have : commands_)
        if (have.id == c.id) {
            have = std::move(c);
            return;
        }
    commands_.push_back(std::move(c));
}

const Command* Commands::find(const std::string& id) const {
    for (const Command& c : commands_)
        if (c.id == id) return &c;
    return nullptr;
}

bool Commands::run(const std::string& id) const {
    const Command* c = find(id);
    if (!c || !c->canRun()) return false;
    c->run();
    return true;
}

std::vector<const Command*> Commands::inMenu(const std::string& menu) const {
    std::vector<const Command*> out;
    for (const Command& c : commands_)
        if (c.menu == menu) out.push_back(&c);
    return out;
}

}  // namespace stardraft
