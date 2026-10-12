// epTrace: euddraft's tool that shows where a map's scripts spend the game's time.
//
// A trace build (EudProject::traceBuild) makes euddraft write <map>.epmap: an id for each line of the scripts that
// the code records when it runs ("file|function|line"). epTrace.exe (beside euddraft.exe; it needs administrator
// rights) finds the map in a running StarCraft.exe, samples the line that runs and the calls to it, and shows them
// in its window. When the window closes, it writes <map>.epmap.prof: each script with the number of samples on
// each line.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace stardraft {
namespace eptrace {

// epTrace.exe beside euddraft.exe (empty if it is not there).
std::filesystem::path exe(const std::filesystem::path& euddraftExe);

struct Line {
    std::filesystem::path file;  // the script
    int line = 0;                // from 1
    int64_t samples = 0;
    std::string code;      // the line's text
    std::string function;  // from the .epmap ("" if it does not name the line); epScript's "f_" prefix removed
};

struct Report {
    std::vector<Line> lines;  // the lines with samples, the most samples first
    int64_t total = 0;        // the samples of all lines
    int files = 0;            // the scripts in the report
};

// Reads epTrace's report (.prof). `epmap` (optional) names the functions of the lines.
bool read(const std::filesystem::path& prof, const std::filesystem::path& epmap, Report& out, std::string& error);
// The same, from the texts (for tests).
Report parse(const std::string& prof, const std::string& epmap);
// The same file whatever the letter case or the separators (as Windows finds files).
bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b);

}  // namespace eptrace
}  // namespace stardraft
