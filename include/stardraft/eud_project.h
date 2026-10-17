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
    // New graphics for preplaced units ("remake: 1" in [project]; not written to the .eds): the map's code removes each
    // preplaced unit that the .dat edits give other graphics, and makes it again at the start, so that it shows them
    // (remakeUnitsScript). Call save() to keep a change.
    bool remakeUnits() const;
    void setRemakeUnits(bool on);
    // The doodads pinned to the top of the Doodads palette, in the order they were pinned ("doodadpins: 12,45" in
    // [project]; not written to the .eds). Call save() to keep a change.
    std::vector<uint16_t> doodadPins() const;
    void setDoodadPins(const std::vector<uint16_t>& ids);
    // The code for that: each unit of `types` removed and made again on its place with the same owner, hit points,
    // shields, energy, resources and invincibility. The units that a new unit cannot copy stay as they are: not
    // finished, burrowed, loaded, hallucinations, lifted buildings, cloaked units, units with an add-on, an attached
    // add-on, a linked Nydus Canal, and units with interceptors or scarabs. Empty for no types.
    struct RemakeType {
        uint16_t type = 0;
        std::string name;
        bool building = false;   // stays when it is lifted or has an add-on
        bool resources = false;  // a mineral field or a geyser: its resources go to the new unit
        bool addon = false;      // stays when it is attached
        bool nydus = false;      // a Nydus Canal: stays when it has an exit
        bool hangar = false;     // stays when it has interceptors or scarabs
        bool cloakable = false;  // stays when it is cloaked
    };
    // Whether a preplaced unit stays (see above), by what the map's UNIT section says of it.
    static bool remakeKeeps(const Unit& unit);
    static std::string remakeUnitsScript(const std::vector<RemakeType>& types);
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
    std::string mainValue(const std::string& key) const;
    bool read(const std::filesystem::path& file, bool eds, std::string& error);
    std::string text(bool eds) const;
    std::filesystem::path file_;
    std::string name_;
    std::vector<Section> sections_;
};

}  // namespace stardraft
