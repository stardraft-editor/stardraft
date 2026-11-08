#include "stardraft/import_codegen.h"

// EUD Editor 2's trigger editor to TriggerEditor.eps (a stub: the port comes next).

namespace stardraft {

bool ee2TriggerEditorCode(const std::vector<std::string>& lines, const ImportCodeContext& ctx, std::string& eps,
                          std::vector<std::pair<std::string, std::string>>& modules, std::vector<std::string>& problems) {
    (void)lines, (void)ctx, (void)modules;
    eps.clear();
    problems.push_back("the trigger editor: not made yet");
    return false;
}

}  // namespace stardraft
