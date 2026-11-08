#pragma once
#include <string>
#include <vector>

namespace stardraft {
struct CopyTransform {
    double a = 1, b = 0, c = 0, d = 1;
    std::string key() const;
};
// Mirrors use the Editor's flags (1,2,4,8). Direct unions deliberately do not form a group.
std::vector<CopyTransform> copyTransforms(int mirrors, int turns, bool direct = false, bool mirroredRotations = false,
                                          bool opposite = false);
struct CopyChoice {
    std::string key;
    bool enabled = true;
    int terrain = -1; // -1: inherit the default
    int rawTile = -1;
};
struct CopyOptions {
    std::vector<CopyChoice> choices;
    int terrain = -1; // -1: the source brush
    int rawTile = -1;
    bool direct = false;
    bool mirroredRotations = false;
    bool opposite = false;
};
std::string copyOptionsText(const CopyOptions&);
CopyOptions readCopyOptions(const std::string&);
} // namespace stardraft
