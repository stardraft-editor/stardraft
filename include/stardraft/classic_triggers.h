#pragma once

// The classic triggers of a map (TRIG; roadmap L.2), as StarEdit edits them: each trigger has players (who runs it),
// up to 16 conditions and up to 64 actions. The layout is the staredit.net wiki's (docs/scenario.chk.wiki), checked on
// a corpus of maps:
//   a trigger is 2400 bytes: 16 conditions of 20 bytes, 64 actions of 32 bytes, the flags (u32) and 28 bytes of
//   players (27 groups and the index of the action that runs now).
// Every byte is kept: a trigger read and written back without a change gives the same bytes. The game stops at the
// first condition (action) of type 0, so the lists that show are the entries before it; entries after it stay as
// they are.
//
// The value of each condition and action parameter comes from one field; classicConditionDef and classicActionDef
// say which field, what kind of value it holds (a player, a unit, a location, ...) and how the sentence reads.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;
class StringTable;

constexpr int kClassicConditions = 16, kClassicActions = 64, kClassicGroups = 27;
constexpr size_t kClassicTriggerSize = 2400;

// The field a parameter's value is in (see ClassicParam).
enum class ClassicField : uint8_t {
    Location, Player, Amount, Unit, Comparison, Resource,  // conditions (Resource: the byte at 16)
    Text, Wav, Time, Number, Modifier,                     // actions (Location, Player and Unit as above)
};

// A batch (R23): a block in an action list, "For each <variable> in <values>". Its children are the rows after it, one
// level deeper (ClassicAction::depth). The file form (classicExpandBatches) is the block's frame of marker comments,
// its template and its expansion (one copy of the children for each value); classicCollapseBatches reads it back.
// See classic_triggers.cpp for the layout.
enum class ClassicBatchKind : uint8_t { Unit, Player, Location };
struct ClassicBatch {
    std::string name = "unit";  // the variable's name: letters, digits, spaces and underscores (classicBatchName)
    ClassicBatchKind kind = ClassicBatchKind::Unit;
    std::vector<uint32_t> values;  // one copy of the children each (a unit type, a player or a location)
    friend bool operator==(const ClassicBatch&, const ClassicBatch&) = default;
};

// The row type of a block in the working copy (never written to a map).
constexpr uint8_t kClassicBlockType = 0xFF;

// A mod's script line kept in a Comment action (a Cosmonarchy CRIG: "set_race 1 askosi"), as its own condition or action
// row in the working copy (never written to a map: the mod's module makes the rows from the Comments' lines and writes
// them back, cmbw::crigCollapse and crigExpand).
constexpr uint8_t kClassicCrigType = 0xFE;
struct ClassicCrig {
    std::string line;    // the command as the mod reads it
    int group = -1;      // the trigger's Comment it came from (its n-th that became rows; -1: a new row)
    int index = 0;       // its line in that Comment
    int at = 0;          // that Comment's place among the action rows (for a Comment of conditions only)
    uint32_t text = 0;   // that Comment's string id
    uint8_t flags = 0;   // that Comment's flags
    bool crlf = true;    // that Comment's lines end in CR LF (else LF)
    friend bool operator==(const ClassicCrig&, const ClassicCrig&) = default;
};
// (equal when the lines are: a row and its copy, or a changed row changed back)
struct ClassicCrigRef {
    std::shared_ptr<const ClassicCrig> p;
    explicit operator bool() const { return p != nullptr; }
    const ClassicCrig* operator->() const { return p.get(); }
    friend bool operator==(const ClassicCrigRef& a, const ClassicCrigRef& b) { return a.p == b.p || (a.p && b.p && *a.p == *b.p); }
};
// The fields of an action that can hold a batch's variable, in ClassicAction::var order: location, player, unit, number.
constexpr int kClassicVarFields = 4;

struct ClassicCondition {
    uint32_t location = 0;  // 1-based location number (64: Anywhere); 0: none
    uint32_t player = 0;    // a player or group (0-11 players 1-12, 13 current player, 17 all players, 18-21 forces)
    uint32_t amount = 0;
    uint16_t unit = 0;        // unit type, or 229 any unit, 230 men, 231 buildings, 232 factories
    uint8_t comparison = 0;   // 0 at least, 1 at most, 10 exactly; switches: 2 set, 3 not set
    uint8_t type = 0;         // the condition (ClassicConditionType); 0: none (the list ends)
    uint8_t resource = 0;     // resource type, score type or switch number (0-255)
    uint8_t flags = 0;        // bit 1 (2): disabled; bit 4 (16): unit type used
    uint16_t internal = 0;    // 0x4353 ("SC"): a masked memory condition (EUD)
    ClassicCrigRef crig;      // working copy only: a mod's script condition (type kClassicCrigType)
    friend bool operator==(const ClassicCondition&, const ClassicCondition&) = default;
};

struct ClassicAction {
    uint32_t location = 0;  // 1-based location number; Move unit, Order: where from
    uint32_t text = 0;      // map string id of the text
    uint32_t wav = 0;       // map string id of the sound file's path
    uint32_t time = 0;      // milliseconds (seconds for the countdown timer)
    uint32_t player = 0;    // the first player or group
    uint32_t number = 0;    // the second value: an amount, a second player, a location, a switch, an AI script
    uint16_t unit = 0;      // unit type; also resource type, score type, alliance status
    uint8_t type = 0;       // the action (ClassicActionType); 0: none (the list ends)
    uint8_t modifier = 0;   // count (0: all), switch action, state, order, modifier (7 set to, 8 add, 9 subtract)
    uint8_t flags = 0;      // bit 0: ignore a wait once; 1 (2): disabled; 2 (4): always display; 3 (8): unit properties used; 4 (16): unit type used
    uint8_t padding = 0;
    uint16_t internal = 0;  // 0x4353 ("SC"): a masked memory action (EUD)
    // Working copy only (not in the file): the nesting depth of the row (0 at the top) and, for each field that can hold
    // a batch's variable (classicVarField), the depth of the block that holds the variable plus 1 (0: none).
    uint8_t depth = 0;
    std::array<uint8_t, kClassicVarFields> var{};
    // A block's row (type kClassicBlockType): its variable and values.
    std::shared_ptr<const ClassicBatch> batch;
    ClassicCrigRef crig;  // a mod's script action's row (type kClassicCrigType)
};
bool operator==(const ClassicAction& a, const ClassicAction& b);

// The field of a variable's slot k is the one of classicVarField(k); the variable's kind must be the parameter's kind.
ClassicField classicVarField(int k);
bool classicVarAllowed(const ClassicAction& a, int k, ClassicBatchKind kind);

struct ClassicTrigger {
    std::array<ClassicCondition, kClassicConditions> conditions{};
    // The working copy's rows (blocks with their children): the game's 64 slots. A mod's command rows (one per line of its
    // Comments) can make more; written back as Comments they must fit in 64 again (cmbw::crigFits).
    std::vector<ClassicAction> actions = std::vector<ClassicAction>(size_t(kClassicActions));
    uint32_t flags = 0;  // bit 2 (4): preserve trigger; bit 3 (8): disabled (Turn off); the rest the game sets while it runs
    static constexpr uint32_t kDisabled = 8;
    bool disabled() const { return (flags & kDisabled) != 0; }
    void setDisabled(bool off) { flags = off ? (flags | kDisabled) : (flags & ~kDisabled); }
    std::array<uint8_t, kClassicGroups> players{};  // who runs it: 0-7 players 1-8, 17 all players, 18-21 forces 1-4
    uint8_t currentAction = 0;
    // Working copy only: the switch that links the parts when the trigger is split (-1: none; see classic_triggers.cpp).
    int splitSwitch = -1;
    // (the empty slots after the last row do not count)
    friend bool operator==(const ClassicTrigger& a, const ClassicTrigger& b);

    // How many conditions (actions) show: those before the first one of type 0.
    int conditionCount() const;
    int actionCount() const;
    // The row after a row's children: a block's end, else row + 1.
    int blockEnd(int row) const;
    // Add (at the end of the list; false when it is full: 64 actions, more in a trigger with a mod's command rows), remove
    // (a block with its children), move a row with its children to a place of the list without them.
    bool addCondition(const ClassicCondition& c, int at = -1);
    bool addAction(const ClassicAction& a, int at = -1);
    void removeCondition(int i);
    void removeAction(int i);
    void moveCondition(int from, int to);
    void moveAction(int from, int to);
    // Moves a row (with its children) into the block before it (false when there is none), or out of its block, one
    // level each.
    bool indentAction(int i);
    bool outdentAction(int i);
    // Removes a block's row and keeps its children, one level out (the rows of the block stay in place).
    bool unwrapAction(int i);
    // The rows from `row` to its end (a block with its children; one row for another row).
    std::vector<ClassicAction> subtreeRows(int row) const;
    // Inserts rows (a subtree, its first row at its own depth) before place `at`, at depth `depth`. A variable that holds
    // a block inside the rows follows that block; one that holds a block outside them takes the deepest block above
    // `at` of its kind, or none. False when the list would be full (64 actions, more with a mod's command rows), `depth`
    // is not inside the blocks above `at`, or the row at `at` is deeper than `depth` (it would leave its block).
    bool pasteRows(const std::vector<ClassicAction>& rows, int at, int depth);
    // Fixes the depths and removes the variables that no enclosing block holds (after an edit); the empty slots after the
    // game's 64 go.
    void pruneBlocks();
    bool runsFor(int group) const { return group >= 0 && group < kClassicGroups && players[size_t(group)]; }
    static ClassicTrigger make();  // a new trigger: Always, no actions, for no player yet
};

// TRIG as the game reads it (all TRIG sections in order). `tail`: bytes after the last whole trigger (kept).
struct ClassicTriggers {
    std::vector<ClassicTrigger> triggers;
    Bytes tail;
};
ClassicTriggers readClassicTriggers(const Chk& chk);
Bytes encodeClassicTriggers(const ClassicTriggers& t);
// Writes TRIG (one section, where the first was). Writes nothing when the bytes are the same.
void writeClassicTriggers(Chk& chk, const ClassicTriggers& t);

// --- What the conditions and actions are ---------------------------------------------------------------------

// What kind of value a parameter holds (which picker it gets).
enum class ClassicKind : uint8_t {
    Player, Comparison, Number, Unit, Location, Resource, Score, Switch, SwitchState, SwitchAction, Modifier, Text,
    Sound, Milliseconds, Seconds, Count, AiScript, Order, Alliance, State, UnitProperties, Percent,
};
struct ClassicParam {
    ClassicKind kind;
    ClassicField field;
    const char* name;  // the placeholder in the sentence ("player", "n", "unit", ...)
};
struct ClassicDef {
    uint8_t type = 0;
    const char* name = "";      // StarEdit's name ("Bring")
    const char* sentence = "";  // "{player} brings {cmp} {n} {unit} to {location}."
    std::vector<ClassicParam> params;
    uint8_t flags = 0;          // the flags a new one has (as StarEdit's maps have them)
};
// nullptr for a type the game does not have (it shows as its number and stays as it is).
const ClassicDef* classicConditionDef(uint8_t type);
const ClassicDef* classicActionDef(uint8_t type);
// The types in StarEdit's order (by name), for the type picker.
const std::vector<const ClassicDef*>& classicConditionDefs();
const std::vector<const ClassicDef*>& classicActionDefs();

uint32_t classicValue(const ClassicCondition& c, ClassicField f);
uint32_t classicValue(const ClassicAction& a, ClassicField f);
void setClassicValue(ClassicCondition& c, ClassicField f, uint32_t v);
void setClassicValue(ClassicAction& a, ClassicField f, uint32_t v);

// A new condition (action) of the type, with the values StarEdit starts with (player: current player, amount 0,
// location Anywhere, ...).
ClassicCondition makeClassicCondition(uint8_t type);
ClassicAction makeClassicAction(uint8_t type);
// The same entry with another type: the values that both types have stay.
ClassicCondition retypeClassicCondition(const ClassicCondition& c, uint8_t type);
ClassicAction retypeClassicAction(const ClassicAction& a, uint8_t type);

// The fixed choices of a kind (Comparison, Resource, Score, SwitchState, SwitchAction, Modifier, Order, Alliance,
// State, Player): value and name. Empty for the other kinds.
struct ClassicChoice {
    uint32_t value;
    const char* name;
};
const std::vector<ClassicChoice>& classicChoices(ClassicKind kind);

// An AI script of the game (scripts\aiscript.bin, names from stat_txt.tbl).
struct ClassicAiScript {
    uint32_t id = 0;  // the four letters, as the action holds them ("TMCu")
    std::string name;
    bool location = false;  // needs a location (Run AI script at location)
    bool broodWar = false;
};
std::vector<ClassicAiScript> loadClassicAiScripts(const GameData& data);
std::string classicFourCC(uint32_t id);  // "TMCu"

// The names that the sentences use: the map's and the game's. Missing lookups give numbers.
struct ClassicNames {
    std::function<std::string(uint32_t)> unit;      // unit type id (0-227)
    std::function<std::string(uint32_t)> location;  // 1-based location number
    std::function<std::string(uint32_t)> string;    // map string id
    std::function<std::string(uint32_t)> switchName;  // 0-255
    std::function<std::string(uint32_t)> force;     // 0-3 ("" for the default name)
    const std::vector<ClassicAiScript>* ai = nullptr;
};
std::string classicValueText(ClassicKind kind, uint32_t v, const ClassicNames& names);
std::string classicConditionText(const ClassicCondition& c, const ClassicNames& names);
// `scope[d]`: the block at depth d of the row's enclosing blocks; a field that holds a variable shows its name.
std::string classicActionText(const ClassicAction& a, const ClassicNames& names, const std::vector<const ClassicBatch*>* scope = nullptr);
std::string classicPlayerName(uint32_t group, const ClassicNames& names);

// Is this a condition or action that only an EUD editor makes (a player or unit past the game's lists, a mask)?
// A mod's unit types past the trigger groups (Cosmonarchy BW: ids 233 up to `count`) are unit types, not EUD values.
// 233 (the default): the game's limit (units 0-227, no unit 228, the groups 229-232).
void classicSetUnitTypeCount(uint32_t count);
bool classicEud(const ClassicCondition& c);
bool classicEud(const ClassicAction& a);

// The id of a map string with this text: an existing string with the same text, else a new one. 0 for "".
uint32_t classicStringId(StringTable& strings, const std::string& text);

// --- Batches (R23) -------------------------------------------------------------------------------------------------

// The block kinds: one row each (classic_triggers.cpp has the table). A new kind is a ClassicBatchKind value and a row.
struct ClassicBlockKind {
    ClassicBatchKind kind;
    const char* label;  // "Unit type": the name in the picker and the dialog (sentence case)
    const char* name;   // "unit": the variable's name in a new block
    ClassicKind values; // what the values are: Unit, Player or Location
};
const std::vector<ClassicBlockKind>& classicBlockKinds();
const ClassicBlockKind& classicBlockKind(ClassicBatchKind kind);
// New blocks can be added (the action picker's "Blocks"; roadmap R23). Off again (2026-10-18): the blocks (For each
// unit, player, location) are hidden for now; a map's blocks still show and can be edited.
constexpr bool kClassicBatchesOffered = false;
ClassicKind classicBatchValueKind(ClassicBatchKind kind);  // the kind of the values: Unit, Player or Location
// A name that the header and the sentences can hold: letters, digits, spaces and underscores, at most 24, trimmed;
// "unit" (the kind's name) when nothing is left.
std::string classicBatchName(const std::string& name, ClassicBatchKind kind);
// The variable as a sentence shows it: variable "unit".
std::string classicBatchVariableText(const ClassicBatch& b);
// A block row's text: For each unit in Terran Marine, Zergling.
std::string classicBatchText(const ClassicBatch& b, const ClassicNames& names);
// The working copy of a map's TRIG: the blocks of a trigger become rows again. A trigger without blocks stays as it is.
ClassicTriggers classicCollapseBatches(const ClassicTriggers& t, const StringTable& strings);
// The TRIG to write for a working copy: each block becomes its frame, and a trigger that does not fit in 64 slots is
// split into parts (see classic_triggers.cpp). Sets the marker string while a block exists, blanks it when none does.
ClassicTriggers classicExpandBatches(const ClassicTriggers& t, StringTable& strings);
// False when a trigger with blocks needs a split and every switch is used by the map's triggers (the trigger is not
// written then; the editor says so).
bool classicCanExpand(const ClassicTriggers& t);

}  // namespace stardraft
