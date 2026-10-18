#pragma once
// Imports another editor's project into a new StarDraft project (roadmap R52):
//   - EUD Editor 2 (.e2s): its input map, its .dat edits (as StarDraft's data edits), its stat_txt texts, its euddraft
//     plugins, and TriggerEditor.eps when it is beside the .e2s (EUD Editor 2 writes it when it builds) as a script.
//   - euddraft (.eds, .edd; EUD Editor 3 builds through one too): its input map, its .eps and .py files as scripts, and
//     its plugin sections.
// The report lists what came in and what did not (button sets, requirements, ...), to do again by hand.

#include <filesystem>
#include <string>
#include <vector>

#include "eud_project.h"

namespace stardraft {

class GameData;

struct ImportReport {
    std::vector<std::string> imported;     // what came in ("152 values of units.dat", "the plugin unlimiter")
    std::vector<std::string> notImported;  // what did not, with why
};

// The kind of project the file is: "e2s", "eds" or "" (not one that StarDraft imports).
std::string importKind(const std::filesystem::path& file);
// Makes <parent>/<name>/ from `file` (an .e2s, .eds or .edd) and opens it in `out`.
bool importProject(const std::filesystem::path& file, const std::filesystem::path& parent, const std::string& name, const GameData& data,
                   EudProject& out, ImportReport& report, std::string& error);

}  // namespace stardraft
