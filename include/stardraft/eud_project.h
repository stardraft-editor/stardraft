#pragma once

// An EUD project: a folder with StarDraft's project file, project.stardraft (text). Build writes euddraft's build
// file (<name>.eds) from it, so euddraft can also build the folder by itself.
//
//   <name>/
//     project.stardraft  [project] name, map and output; then one section per plugin (scripts, euddraft plugins),
//                        in the .eds syntax
//     <name>.scx         the map, edited in StarDraft
//     <name>.eds         made by Build from project.stardraft (changes to it are lost)
//     <name>_EUD.scx     the compiled map, written by Build
//     scripts/main.eps   code; every .eps or .py the project lists is built in
//
// A script can also be linked from anywhere on disk (R5): its section is its path relative to the folder
// ("[../shared/main.eps]"), or its full path on another drive. euddraft loads it from there, and the files that it
// imports from its own folder. StarDraft never writes to a linked file.
//
// Sections StarDraft does not write (euddraft plugins such as [MSQC], other people's scripts) are kept
// as they are. A folder with only a .eds (made by euddraft users or an earlier StarDraft) opens too: StarDraft
// writes its project.stardraft.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "stardraft/euddraft.h"

namespace stardraft {

struct Unit;
class UnitTypes;

class EudProject {
public:
    struct Section {
        std::string name;                 // "main", "scripts/main.eps", "MSQC", ...
        std::vector<std::string> lines;   // its lines, as written (keys unescaped on demand)
    };

    // Creates <parent>/<name>/ with the layout above. `map` is copied in as map/<name>.scx (it must be
    // a Brood War map; euddraft needs one). Fails if the folder exists and is not empty.
    static bool create(const std::filesystem::path& parent, const std::string& name, const std::filesystem::path& map,
                       EudProject& out, std::string& error);
    static constexpr const char* kFileName = "project.stardraft";
    // Opens a project from its project.stardraft, its folder, or a .eds (the project.stardraft beside it; else the
    // .eds is read and a project.stardraft written from it).
    bool open(const std::filesystem::path& fileOrFolder, std::string& error);
    bool save(std::string& error) const;
    // Writes <name>.eds for euddraft (compile() does it first).
    bool writeEds(std::string& error) const;

    const std::filesystem::path& file() const { return file_; }  // project.stardraft
    std::filesystem::path edsFile() const { return folder() / (name_ + ".eds"); }
    std::filesystem::path folder() const { return file_.parent_path(); }
    std::string name() const { return name_; }
    std::filesystem::path inputMap() const;   // [main] input, resolved against the folder
    std::filesystem::path outputMap() const;  // [main] output
    // Where Build writes the map: kept relative when inside the project folder, else as a full path.
    // The folder is made at build time. Call save() to keep it.
    void setOutputMap(const std::filesystem::path& file);
    std::filesystem::path defaultOutputMap() const { return folder() / (name() + "_EUD.scx"); }
    // Script plugins (.eps / .py sections), relative to the folder as written.
    std::vector<std::string> scripts() const;
    void addScript(const std::string& relativePath);
    // A script's file: the section name resolved against the folder (euddraft resolves it against the .eds's folder,
    // which is the same).
    std::filesystem::path scriptFile(const std::string& section) const;
    // Links a script file from anywhere on disk (R5): StarDraft builds it as it is and never writes to it. The section
    // name is the path relative to the folder ("scripts/a.eps", "../shared/main.eps") when there is one, else the full
    // path. Returns the section name. Call save() to keep it.
    std::string linkScript(const std::filesystem::path& file);
    // A linked script: its file is outside the project folder.
    static bool isLinked(const std::string& section);
    // The scripts whose files are not found (Build stops when there is one).
    std::vector<std::string> missingScripts() const;
    // Takes a section out of the .eds (the file stays where it is).
    bool removeSection(const std::string& name);
    // Moves a script one place earlier (-1) or later (+1) in the build order. Generated sections stay
    // first; returns false when it cannot move.
    bool moveScript(const std::string& name, int delta);
    static bool isGenerated(const std::string& section) { return section.rfind("generated/", 0) == 0; }
    const std::vector<Section>& sections() const { return sections_; }
    // A plugin's section (e.g. "MSQC"): its lines, and new ones (none: the section goes; a new one goes after the
    // generated code, before the scripts). Call save() to keep it.
    std::vector<std::string> sectionLines(const std::string& name) const;
    void setSectionLines(const std::string& name, std::vector<std::string> lines);
    // EUD Turbo: euddraft's eudTurbo plugin ([eudTurbo]), which makes the triggers run every frame. On in new
    // projects. Call save() to keep a change.
    bool turbo() const;
    // Freeze protection: euddraft's freeze, which protects the built map's file. euddraft turns it on unless [freeze] has
    // a "freeze" line ("freeze : 0"), so it is on in new projects. Call save() to keep a change.
    bool freeze() const;
    void setFreeze(bool on);
    // Freeze works on this system. Not on macOS for now: euddraft 0.11.0.1's freeze crashes there (its MPQ code casts a
    // 32-bit size's pointer to unsigned long*), so the .eds that Build writes has "freeze : 0" whatever the project says.
    static bool freezeWorks();
    // Debug build ("debug: 1" in [project]; not written to the .eds): the triggers' code checks indexes worked out in
    // the game and shows a message for one out of range (TriggerBuildOptions::debug). Call save() to keep a change.
    bool debugBuild() const;
    void setDebugBuild(bool on);
    // Trace build ("trace: 1" in [project]; written to the .eds as euddraft's "debug: 1"): the scripts' code records
    // each line it runs, and Build writes traceMap() beside the map. epTrace reads it while the map plays and writes
    // traceReport() when it closes (eptrace.h). Call save() to keep a change.
    bool traceBuild() const;
    void setTraceBuild(bool on);
    // New graphics for preplaced units ("remake: 1" in [project]; not written to the .eds): the game makes the preplaced
    // units before the .dat edits run, so they keep the old graphics. With this option, Build takes each preplaced unit
    // that the edits give other graphics out of the map that euddraft gets (writeBuildMap), and the map's code makes it
    // at the start, in the UNIT order, on its place, for its owner and with its values (planRemake). The project's map
    // does not change. Call save() to keep a change.
    bool remakeUnits() const;
    void setRemakeUnits(bool on);
    // The doodads pinned to the top of the Doodads palette, in the order they were pinned ("doodadpins: 12,45" in
    // [project]; not written to the .eds). Call save() to keep a change.
    std::vector<uint16_t> doodadPins() const;
    void setDoodadPins(const std::vector<uint16_t>& ids);
    // Whether a preplaced unit stays in the map (with the old graphics), by what the map's UNIT section says of it: a unit
    // in a transport (in transit, not a building), and a unit linked to an add-on or a Nydus Canal. `building`: its
    // type is a building (a lifted building is in transit too, and the code makes it).
    static bool remakeKeeps(const Unit& unit, bool building);
    // A unit property (UPRP) for each different set of values: a map has 64, for its triggers and for this code.
    static constexpr int kUnitProperties = 64;
    struct RemakePlan {
        std::vector<size_t> made;  // the UNIT indexes of the units that the code makes (writeBuildMap leaves them out)
        int loaded = 0;            // kept: in a transport
        int linked = 0;            // kept: linked to an add-on or a Nydus Canal (or the other unit of the link)
        int noProperty = 0;        // kept: their values need more unit properties than `freeProperties`
        int codeUnit = 0;          // kept: a unit type above 227 (CreateUnit cannot make it reliably)
        int edge = 0;              // kept: on or past the map's edge (CreateUnit moves it, fails, or the game stops)
        int properties = 0;        // the unit properties that the code uses
        std::string code;          // generated/remake.eps (empty when it makes no units)
        std::string table;         // generated/remake_units.py: the units' records, which remake.eps imports
    };
    // The plan for the units of `units` (the map's UNIT section) whose type is in `types`. The code is a table, one
    // record for each unit (its place, type, owner and property set; in a Python module, so that it is only data), and
    // one loop over it: the code does not grow with the units. Each different set of values (hit points, shields, energy, resources, hangar, and the states cloaked,
    // burrowed, lifted, hallucination, invincible) is a unit property, one case of CreateUnitWithProperties in the loop;
    // a unit with the default values gets CreateUnit. Units with values that need more than `freeProperties` different
    // properties stay in the map. So do units of a type above 227, and (with the map's size in tiles, not 0) units whose
    // box (a building's: also its placement box) is on or past the map's edge.
    static RemakePlan planRemake(const std::vector<Unit>& units, const UnitTypes& unitTypes, const std::vector<uint16_t>& types,
                                 int freeProperties = kUnitProperties, int mapWidth = 0, int mapHeight = 0);
    // Writes the plan's code (generated/remake.eps and its table, generated/remake_units.py), or removes both when it
    // makes no units.
    bool writeRemakeCode(const RemakePlan& plan, std::string& error);
    // The map that euddraft gets: the project's map without the UNIT entries `leaveOut` (UNIT indexes), written to
    // buildMap(); the .eds's input is then that map. Empty `leaveOut`: the .eds's input is the project's map, and an
    // old build map goes.
    bool writeBuildMap(const std::vector<size_t>& leaveOut, std::string& error);
    std::filesystem::path buildMap() const { return folder() / "generated" / inputMap().filename(); }
    std::filesystem::path traceMap() const { return std::filesystem::path(outputMap()) += ".epmap"; }
    std::filesystem::path traceReport() const { return traceMap() += ".prof"; }
    // A euddraft plugin with no settings or a few ([unlimiter], [noAirCollision], [bgmplayer], [dataDumper]): whether
    // the .eds has its section, and turning it on (with its lines) or off.
    bool plugin(const std::string& name) const;
    void setPlugin(const std::string& name, bool on, std::vector<std::string> lines = {});

    // Where a .dat's edits are kept: dat/<name>.dat (a complete .dat; absent when nothing is edited).
    std::filesystem::path datFile(const std::string& datName) const { return folder() / "dat" / (datName + ".dat"); }
    // Writes the code for the .dat edits (generated/<name>.eps, one per edited .dat) and lists it in the
    // .eds before the other scripts, so code that reads the values sees the edited ones; drops it again
    // when a .dat has no edits. Saves the .eds when that changes it.
    bool writeGenerated(const std::vector<const class DatTable*>& tables, std::string& error);
    // Writes generated/<name> (e.g. the triggers' code) and lists it in the .eds after the .dat code and
    // before the scripts; empty code removes both. Saves the .eds when that changes it.
    bool writeGeneratedScript(const std::string& name, const std::string& code, std::string& error);

    // Where euddraft found an error in a script: the file (from the module in "[Error 8510] Module "helper" Line 3 :
    // ..." and the "[epScript] Compiling "..\shared\helper.eps"..." line before it), its line and euddraft's words.
    // Imported files count too. False when the log names no module and line.
    struct ScriptError {
        std::filesystem::path file;  // empty when the module's file is not known
        std::string module;
        int line = 0;
        std::string message;
    };
    bool scriptError(const std::string& log, ScriptError& out) const;
    // The words for a missing script: "Build stopped: the linked file ../x.eps is not found (C:\...\x.eps)...".
    std::string missingScriptText(const std::string& section) const;

    // Builds the output map with euddraft (the chosen copy, else StarDraft's own; fetched if missing). Stops before
    // euddraft when a script is missing (missingScriptText).
    euddraft::BuildResult compile(const std::filesystem::path& chosenEuddraft = {},
                                  const std::function<void(const std::string&)>& onLine = {}) const;

private:
    bool useBuildMap_ = false;  // the .eds's input is buildMap() (writeBuildMap)
    std::string mainValue(const std::string& key) const;
    bool read(const std::filesystem::path& file, bool eds, std::string& error);
    std::string text(bool eds) const;
    std::filesystem::path file_;
    std::string name_;
    std::vector<Section> sections_;
};

}  // namespace stardraft
