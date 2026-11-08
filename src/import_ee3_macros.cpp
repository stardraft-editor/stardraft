#include "stardraft/import_ee3_extras.h"

// EUD Editor 3's Lua macros (a stub: the port comes next).

namespace stardraft {

struct Ee3Macros::State {};

Ee3Macros::Ee3Macros(const nrbf::Graph&, const nrbf::Object*, const ImportCodeContext&) : s_(new State) {}
Ee3Macros::~Ee3Macros() { delete s_; }
bool Ee3Macros::apply(const std::string& text, bool, const std::string&, std::string& out, std::vector<std::string>& problems) {
    out = text;
    problems.push_back("the macros: not made yet");
    return false;
}
std::vector<std::string> Ee3Macros::msqcLines() const { return {}; }
std::vector<std::string> Ee3Macros::chatEventLines() const { return {}; }
std::string Ee3Macros::pluginVariables() const { return {}; }

}  // namespace stardraft
