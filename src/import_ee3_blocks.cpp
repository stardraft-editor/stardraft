#include "stardraft/import_codegen.h"

// EUD Editor 3's block scripts to epScript or Python (a stub: the port comes next).

namespace stardraft {

bool ee3BlockScriptCode(const nrbf::Graph& g, const nrbf::Object* scripter, const std::string& fileName, bool python, const ImportCodeContext& ctx,
                        std::string& code, std::vector<std::string>& problems) {
    (void)g, (void)scripter, (void)fileName, (void)python, (void)ctx;
    code.clear();
    problems.push_back("block scripts: not made yet");
    return false;
}

}  // namespace stardraft
