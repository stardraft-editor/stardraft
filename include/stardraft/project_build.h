#pragma once

// Build of an EUD project (File > Project > Build): the project's generated files (the triggers' code, the .dat edits and
// the other tables' edits, the code for new graphics of preplaced units) and the map that euddraft gets, then euddraft
// itself (eud_project.h, euddraft.h). The editor and stardraft-cli run these steps; the editor adds its questions, its
// log and the tables it shows (BuildOpen).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/eud_project.h"
#include "stardraft/euddraft.h"
#include "stardraft/triggers.h"

namespace stardraft {

class Editor;
class GameData;

// What the editor has open, when it differs from the files: the tables of the Data workspace (their edits are saved
// here before the build), and the strings, buttons, requirements, unit looks and triggers. A null pointer: the project's
// file is read. `tables` is in the schema's order (DatSchema::all()); a null entry is a game table that did not load.
// The extras are all given or none (the editor has them all, or the files are read).
struct BuildOpen {
    const std::vector<std::unique_ptr<DatTable>>* tables = nullptr;
    const StatTxt* strings = nullptr;
    const Requirements* reqs = nullptr;
    const ButtonSets* buttons = nullptr;
    const UnitLooks* looks = nullptr;
    const TriggerSet* triggers = nullptr;  // the triggers as the editor has them (null: triggers.json)
};

// The remake plan's counts (EudProject::RemakePlan), for the report.
struct RemakeCounts {
    int made = 0, loaded = 0, linked = 0, noProperty = 0, codeUnit = 0, edge = 0, blocked = 0;
};

// What the steps before euddraft found and made.
struct BuildReport {
    std::string failedStep;                     // "triggers", "dat", "remake", "save" or "map" when prepareBuild failed
    std::vector<std::string> triggerErrors;     // "Trigger \"name\": message" for each error of the checker
    bool codeGenerated = false;                 // the triggers' code was made: triggerSet and triggerLines are set
    TriggerSet triggerSet;                      // the triggers the code is made from
    std::vector<TriggerCodeLine> triggerLines;  // the generated code's lines and their blocks (the trigger code map)
    bool triggersFromFile = false;              // the triggers were read from triggers.json
    std::vector<std::string> datNotes;          // sounds of sfxdata.dat that the build does not apply
    bool remakeOn = false;                      // the project makes preplaced units' new graphics
    RemakeCounts remake;
    int freeProperties = 0;                     // the free unit properties the remake plan used
    std::vector<size_t> leaveOut;               // the UNIT indexes the remake code makes (not in the map euddraft gets)
};

// The map's triggers as the checker and the code need them (its locations, sounds, units and players).
TriggerMapInfo triggerMapInfo(const Editor& ed);

// The project's triggers file (triggers.json).
std::filesystem::path triggersFileOf(const EudProject& pr);

// The steps before euddraft, in the order Build runs them: the triggers' code, the .dat edits, the remake code, the map
// saved into the project (when changed or elsewhere), and the map that euddraft gets. False with `error` on the first step
// that fails (report.failedStep says which).
bool prepareBuild(EudProject& pr, Editor& ed, const GameData& data, const BuildOpen& open, BuildReport& report, std::string& error);

// The unit properties (UPRP) that the remake code can still use: the map's free ones less those of the project's code.
int freeUnitProperties(const EudProject& pr, const Editor& ed);

// The unit types that the project's .dat edits give other graphics (empty when the game's files do not load).
std::vector<uint16_t> projectRemakeUnitTypes(const GameData& data, const BuildOpen& open, const EudProject& pr);

// Runs euddraft on the project (after prepareBuild): `onLine` sees each line of its output.
euddraft::BuildResult compileBuild(const EudProject& pr, const std::function<void(const std::string&)>& onLine = {});

}  // namespace stardraft
