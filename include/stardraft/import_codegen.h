#pragma once
// The code that EUD Editor 2 and 3 make from their trigger editors, made here from the project file itself, so that File >
// Import a project needs no build folder for it (project_import.h). Each function writes the text that the old editor's
// build writes for the same data, as close as its rules allow; what it cannot make goes to `problems` (English, one
// line each, for the import report), and the rest is still written.
//
//   - EUD Editor 2's trigger editor (the .e2s's S_TriggerEditorSET): TriggerEditor.eps (src/import_ee2_triggers.cpp)
//   - EUD Editor 3's classic triggers (a TEFile of type ClassicTrigger): its .eps (src/import_ee3_classic.cpp)
//   - EUD Editor 3's block scripts (a TEFile of type GUIEps or GUIPy): its .eps or .py (src/import_ee3_blocks.cpp)

#include <string>
#include <vector>

#include "stardraft/nrbf.h"

namespace stardraft {

class GameData;

// What the generators may need from the map and the game (names that the old editors put into the code).
struct ImportCodeContext {
    const GameData* data = nullptr;          // the game's data (unit names, ...); can be null in tests
    std::vector<std::string> locationNames;  // the map's locations, by index (0-based, the map's MRGN order); "" none
    std::vector<std::string> switchNames;    // the map's switches, by index; "" none
    std::vector<std::string> unitNames;      // the map's own unit names (UNIx string), by unit id; "" when the game's
};

// EUD Editor 2: the lines of S_TriggerEditorSET (without the S_/E_ lines) to TriggerEditor.eps, and the modules that
// EUD Editor 2 writes beside it for it (name, text: "punitloop.py", ...) in `modules`. False only when the lines are not
// a trigger editor tree at all.
bool ee2TriggerEditorCode(const std::vector<std::string>& lines, const ImportCodeContext& ctx, std::string& eps,
                          std::vector<std::pair<std::string, std::string>>& modules, std::vector<std::string>& problems);

// EUD Editor 3: a ClassicTriggerEditor (a TEFile's _Scripter of type ClassicTrigger) to its epScript. `fileName` is the
// TEFile's name (EUD Editor 3 passes it to GetFileText).
bool ee3ClassicTriggerCode(const nrbf::Graph& g, const nrbf::Object* scripter, const std::string& fileName, const ImportCodeContext& ctx,
                           std::string& eps, std::vector<std::string>& problems);

// EUD Editor 3: a GUIScriptEditor (a TEFile's _Scripter of type GUIEps or GUIPy) to its epScript or Python.
bool ee3BlockScriptCode(const nrbf::Graph& g, const nrbf::Object* scripter, const std::string& fileName, bool python, const ImportCodeContext& ctx,
                        std::string& code, std::vector<std::string>& problems);

}  // namespace stardraft
