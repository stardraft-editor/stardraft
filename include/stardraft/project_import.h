#pragma once
// Imports another editor's project into a new StarDraft project (roadmap R52; File > Import a project):
//   - EUD Editor 3 (.e3s, a .NET BinaryFormatter file: nrbf.h): its maps, its .dat edits, its stat_txt texts, its button
//     sets, requirements, status panels and wireframes (as StarDraft's data edits), its scripts (the trigger editor's
//     file tree: epScript and Python as written, in the same module folders), its euddraft plugins, and its labels.
//   - EUD Editor 2 (.e2s, text): the same data parts, its plugins, and the code of its trigger editor.
//   - euddraft (.eds, .edd; EUD Editor 3 builds through one too): its input map, its .eps and .py files as scripts, and
//     its plugin sections.
//
// Some parts are code that only the old editor makes: EUD Editor 3's Lua macros (@KeyDown(...), <? ?>), its block
// scripts and classic triggers, its BGM player and dot images; EUD Editor 2's trigger editor. These come from the old
// editor's last build (its build folder: EUD Editor 3's BuildData_<name>, EUD Editor 2's Data\eudplibdata), which holds
// that code as epScript. scanImport finds the folder when it can; the user can select it.
//
// The import has two steps: scanImport reads the file and says what each part holds and how it can come over (the
// wizard shows it, and the user's choices go into ImportChoices); importProject makes the project. The report lists
// what came in and what did not.

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "eud_project.h"

namespace stardraft {

class GameData;

struct ImportReport {
    std::vector<std::string> imported;     // what came in ("152 values of units.dat", "the plugin unlimiter")
    std::vector<std::string> notImported;  // what did not, with why
};

// One part of a project file. The ids, in the order of ImportScan::parts:
//   "dat"           .dat values (count: values; details: "units.dat 152", one per .dat)
//   "strings"       stat_txt.tbl texts
//   "buttons"       button sets (count: sets; details: the units that use another unit's set)
//   "requirements"  requirement lists
//   "looks"         status panels and wireframes (count: units)
//   "scripts"       the scripts (count: files; details: their paths in the project, the main one first)
//   "generated"     the code that only the old editor makes (count: files or parts; details: what and where)
//   "files"         other files of the trigger editor: text files and EUD Editor 3's SC Archive Lua scripts, copied but not
//                   built (count: files)
//   "plugins"       euddraft plugins (details: the section names)
//   "labels"        EUD Editor 3's own names and notes for entries (count: entries); kept in a text file of the project
//   "settings"      the old editor's build settings (details: each, with what StarDraft does with it)
//   "unsupported"   parts that StarDraft has no editor for (details: each, with why)
struct ImportPart {
    std::string id;
    int count = 0;
    std::vector<std::string> details;
    // How it comes over: Native (as StarDraft's own data, to edit), Build (from the old editor's build folder), None
    // (it cannot come over: `why`).
    enum class Way { Native, Build, None } way = Way::Native;
    bool needsBuild = false;  // Build: the build folder is necessary and was not found (or does not agree with the file)
    std::string why;          // None, or a note (English, for the CLI)
};

// What a project file holds (scanImport).
struct ImportScan {
    std::string kind;     // "e3s", "e2s" or "eds"
    std::string version;  // the old editor's version that saved it ("0.17.0.1"; empty when not known)
    std::filesystem::path file;
    std::filesystem::path map;     // the input map: as written in the file, or the file found for it
    bool mapFound = false;
    std::filesystem::path output;  // the output map as written ("" none)
    std::filesystem::path buildFolder;  // the old editor's build folder that was found ("" none)
    bool buildMatches = false;          // the build folder's build file names the same input map
    std::vector<ImportPart> parts;      // the parts that the file has (count > 0, or a note), in the order above

    const ImportPart* part(const std::string& id) const;
};

// The user's choices for importProject. The default values give the recommended import.
struct ImportChoices {
    std::filesystem::path map;          // "": the scan's map
    bool keepOutput = false;            // true: Build writes the map where the old editor wrote it; false: <name>_EUD.scx in the folder
    std::filesystem::path buildFolder;  // "": the scan's
    std::set<std::string> skip;         // the part ids not to import
    // Scripts from: false: the scripts' text in the file (to edit; the parts that only the old editor makes are not
    // built); true: the code of the old editor's last build (macros expanded, block scripts and classic triggers as
    // epScript).
    bool scriptsFromBuild = false;
};

// The kind of project the file is: "e3s", "e2s", "eds" or "" (not one that StarDraft imports).
std::string importKind(const std::filesystem::path& file);
// Reads `file` and says what it holds. `buildFolder`: a build folder that the user selected (""; else the one found
// beside the file or the map). False with `error` when the file cannot be read.
bool scanImport(const std::filesystem::path& file, const GameData& data, ImportScan& out, std::string& error,
                const std::filesystem::path& buildFolder = {});
// A folder that the user selected as the build folder: the folder that holds the build file (EUDEditor.eds), found in
// it or under it (BuildData_<name>/eudplibData, Data/eudplibdata); "" when there is none.
std::filesystem::path findBuildFolder(const std::filesystem::path& selected, const ImportScan& scan);
// The recommended choices for a scan: the parts that can come over, and scripts from the build when one is found and
// some script needs it.
ImportChoices defaultChoices(const ImportScan& scan);
// Makes <parent>/<name>/ from the scanned file with `choices` and opens it in `out`.
bool importProject(const ImportScan& scan, const ImportChoices& choices, const std::filesystem::path& parent, const std::string& name,
                   const GameData& data, EudProject& out, ImportReport& report, std::string& error);
// Scans `file` and imports it with the recommended choices.
bool importProject(const std::filesystem::path& file, const std::filesystem::path& parent, const std::string& name, const GameData& data,
                   EudProject& out, ImportReport& report, std::string& error);

}  // namespace stardraft
