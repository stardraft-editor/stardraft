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

// A batch (R23): one trigger, the template, that stands for one trigger for each value of a variable. The variable
// is a unit type, a player or a location; the slots are the fields of the template that hold the variable. The
// expanded TRIG (classicExpandBatches) is what the game and the other editors read: the template, disabled and with
// a comment that names the batch, then one trigger for each value, in the template's order. A batch read from a map
// (classicCollapseBatches) is the template only, with `batch` set and the comment taken away.
enum class ClassicBatchKind : uint8_t { Unit, Player, Location };
struct ClassicBatchSlot {
    bool condition = false;  // a condition (true) or an action
    uint8_t index = 0;       // its place in the list (0-based)
    ClassicField field = ClassicField::Unit;
    friend bool operator==(const ClassicBatchSlot&, const ClassicBatchSlot&) = default;
};
struct ClassicBatch {
    std::string name = "unit";  // the variable's name: letters, digits, spaces and underscores (classicBatchName)
    ClassicBatchKind kind = ClassicBatchKind::Unit;
    std::vector<uint32_t> values;         // one trigger each (none: the template is not a batch)
    std::vector<ClassicBatchSlot> slots;  // the fields that hold the variable
    friend bool operator==(const ClassicBatch&, const ClassicBatch&) = default;
};

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
    friend bool operator==(const ClassicAction&, const ClassicAction&) = default;
};

struct ClassicTrigger {
    std::array<ClassicCondition, kClassicConditions> conditions{};
    std::array<ClassicAction, kClassicActions> actions{};
    uint32_t flags = 0;  // bit 2 (4): preserve trigger; bit 3 (8): disabled; the rest the game sets while it runs
    std::array<uint8_t, kClassicGroups> players{};  // who runs it: 0-7 players 1-8, 17 all players, 18-21 forces 1-4
    uint8_t currentAction = 0;
    std::optional<ClassicBatch> batch;  // a batch's template (see ClassicBatch); the expanded TRIG does not keep it
    friend bool operator==(const ClassicTrigger&, const ClassicTrigger&) = default;

    // How many conditions (actions) show: those before the first one of type 0.
    int conditionCount() const;
    int actionCount() const;
    // Add (at the end of the list; false when it is full), remove (the rest move up), move from one place to another.
    // The slots of a batch move with their entries. A batch's template has room for one action less (its comment).
    bool addCondition(const ClassicCondition& c, int at = -1);
    bool addAction(const ClassicAction& a, int at = -1);
    void removeCondition(int i);
    void removeAction(int i);
    void moveCondition(int from, int to);
    void moveAction(int from, int to);
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
// `batch` and `index` (the entry's place in its list): a slot of the batch shows its variable, not its value.
std::string classicConditionText(const ClassicCondition& c, const ClassicNames& names, const ClassicBatch* batch = nullptr, int index = -1);
std::string classicActionText(const ClassicAction& a, const ClassicNames& names, const ClassicBatch* batch = nullptr, int index = -1);
std::string classicPlayerName(uint32_t group, const ClassicNames& names);

// Is this a condition or action that only an EUD editor makes (a player or unit past the game's lists, a mask)?
bool classicEud(const ClassicCondition& c);
bool classicEud(const ClassicAction& a);

// The id of a map string with this text: an existing string with the same text, else a new one. 0 for "".
uint32_t classicStringId(StringTable& strings, const std::string& text);

// --- Batches (R23) -------------------------------------------------------------------------------------------------

ClassicKind classicBatchValueKind(ClassicBatchKind kind);  // the kind of the values: Unit, Player or Location
// A name that the header and the sentences can hold: letters, digits, spaces and underscores, at most 24, trimmed;
// "unit" (the kind's name) when nothing is left.
std::string classicBatchName(const std::string& name, ClassicBatchKind kind);
bool classicBatchHasSlot(const ClassicBatch& b, bool condition, int index, ClassicField field);
// Turns a slot on or off. A slot's field must hold the batch's kind of value (classicBatchValueKind).
void classicBatchSetSlot(ClassicBatch& b, bool condition, int index, ClassicField field, bool on);
// Drops the slots that point to no entry, or to a field that does not hold the batch's kind of value.
void classicBatchPrune(ClassicTrigger& t);
// The variable as a sentence shows it: variable "unit".
std::string classicBatchVariableText(const ClassicBatch& b);
// The working copy of a map's TRIG: each batch in the map becomes its template (`batch` set, the comment taken away,
// the generated triggers gone). A trigger that is not a batch stays as it is.
ClassicTriggers classicCollapseBatches(const ClassicTriggers& t, const StringTable& strings);
// The TRIG to write for a working copy: each batch becomes its template (disabled, with the comment that names the
// batch) and one trigger for each value. Adds the comments' strings, and blanks those of batches that are gone.
ClassicTriggers classicExpandBatches(const ClassicTriggers& t, StringTable& strings);

}  // namespace stardraft
