#include "stardraft/import_codegen.h"

// EUD Editor 3's classic triggers to epScript (a stub: the port comes next).

namespace stardraft {

bool ee3ClassicTriggerCode(const nrbf::Graph& g, const nrbf::Object* scripter, const std::string& fileName, const ImportCodeContext& ctx,
                           std::string& eps, std::vector<std::string>& problems) {
    (void)g, (void)scripter, (void)fileName, (void)ctx;
    eps.clear();
    problems.push_back("classic triggers: not made yet");
    return false;
}

}  // namespace stardraft
