#include "stardraft/import_ee3_extras.h"

// EUD Editor 3's dot images and BGM player (a stub: the port comes next).

namespace stardraft {

bool ee3DotImages(const nrbf::Graph&, const nrbf::Object*, std::vector<ImportFile>& files, std::vector<std::string>& problems) {
    files.clear();
    problems.push_back("the dot images: not made yet");
    return false;
}
bool ee3BgmPlayer(const nrbf::Graph&, const nrbf::Object*, const std::string&, std::vector<ImportFile>& files, std::vector<std::string>& problems) {
    files.clear();
    problems.push_back("the BGM player: not made yet");
    return false;
}

}  // namespace stardraft
