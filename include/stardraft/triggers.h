#pragma once
// Triggers for an EUD project. Three categories (Definitions, On map start, On every trigger cycle) hold
// folders, triggers and (in Definitions) variables. A trigger has conditions and actions; some actions and
// conditions hold blocks of their own (If / then / else, loops, After a delay, Any of). Every block comes from
// StarDraft's typed block library and every value has a type, which decides
// how it is picked and checked. The set builds to epScript (generate) and is checked on every edit (check).
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace stardraft {

// --- Value types ---------------------------------------------------------------------------------------------

// How a value of a type is picked.
enum class TriggerPicker {
    Choice,      // one of a fixed list (comparisons, players, resources, ...)
    Number,      // a whole number or an expression (variables, arithmetic)
    Text,        // typed text
    UnitType,    // a unit type, from the game's list
    UnitFilter,  // a unit type, or Any unit / Men / Buildings / Factories
    UnitRef,     // one unit: "the unit" in For each unit, a unit variable, a unit picked on the map
    Location,    // one of the map's locations
    Switch,      // 1-256
    Sound,       // a sound file in the map
    Variable,    // a variable of a kind (an assignment's target, a flag)
    Code,        // epScript, typed
    Property,    // a property of a game structure (a unit's hit points, a weapon's cooldown): TriggerType::of
    DatEntry,    // an entry of a .dat (a weapon, an upgrade, an order, ...): TriggerType::of is the .dat
    Trigger,     // a trigger in Definitions, by name (Run a trigger)
    Button,      // a whole button of a command card: "place,icon,condition,action,conditionValue,actionValue,tooltip,tooltipNotUsable"
    Unclear,     // no picker yet: the editor shows these in purple (fix the type in the block library)
};

struct TriggerChoice {
    std::string code, label;
};

struct TriggerType {
    std::string name;   // "PlayerGroup"
    std::string label;  // "player or group", shown in the docs and on hover
    TriggerPicker picker = TriggerPicker::Unclear;
    std::vector<TriggerChoice> choices;  // Choice types
    std::string help;                    // what the values mean
    std::string of;                      // Property: the structure ("CUnit"); DatEntry: the .dat ("weapons")
    int count = 0;                       // DatEntry: entries
};
// nullptr for a type the editor does not know (shown as unclear). Besides the fixed types there are families:
// Property:<structure>, Dat:<dat>, DatValues:<dat>/<field> (a .dat field's named values), Unclear:<kind>.
const TriggerType* triggerType(const std::string& name);
bool triggerTypeClear(const std::string& name);
// A player value (Player, PlayerGroup, PlayerId): one of its choices, or a number from a variable, an expression or a
// value from the game (Player 1 is 0).
bool triggerPlayerType(const std::string& name);
// A starting value for a value of the type ("0", the first choice, ...).
std::string triggerTypeDefault(const std::string& name);

// A property the property blocks offer: eudplib's member (code), how it reads, its value type and scale.
struct TriggerProperty {
    std::string structure;  // "CUnit", "TrgUnit", "Weapon", ...
    std::string code;       // "hp", "statusFlags.Burrowed"
    std::string label;      // "hit points"
    std::string group;      // "Health" (CUnit) or the .dat field's section; "Advanced": raw ones
    std::string type;       // the value's type
    int scale = 1;          // memory holds value * scale (hit points: 256)
    long long min = 0, max = -1;
    bool advanced = false;
    std::string notes;
};
const std::vector<TriggerProperty>& triggerProperties(const std::string& structure);
const TriggerProperty* triggerProperty(const std::string& structure, const std::string& code);
std::string triggerStructureLabel(const std::string& structure);  // "unit type"

// --- Blocks --------------------------------------------------------------------------------------------------

struct TriggerParam {
    std::string name, type, def;
    long long min = 0, max = -1;  // a range for numbers (max < min: none)
};
struct TriggerListDef {
    std::string key;    // "then": how the file names it (stable)
    std::string title;  // "Then"
    bool condition = false;
};

// The values a text takes: its {} (and {:...}) places; "{{" is a brace, not a place.
int triggerPlaceholders(const std::string& text);

// A text with values in it (Show text): its words, and values in braces. "{gold}" or "{wave + 1}": a number,
// variable or sum; "{#1}": the first value from the game kept with the block (triggerTextValues); "{=val.kills}":
// a value from the game just put in (made a {#n} by triggerNormalizeText). "{{" and "}}" are braces.
struct TriggerTextPart {
    enum class Kind { Words, Expression, GameValue, NewGameValue } kind = Kind::Words;
    std::string text;    // the words, the expression, or the new value's block id
    int value = -1;      // GameValue: its index in the block's values
    std::string format;  // how a value shows (eudplib's format codes): "" a number, "n" a player's name, "c" a player's
                         // color, "x" / "X" hex, "s" the text at an address, "t" the text at an EPD; "{gold:x}"
};
// The format codes a value in a text can have, and how StarDraft names them ("" first: a number).
const std::vector<std::pair<std::string, std::string>>& triggerTextFormats();
std::vector<TriggerTextPart> triggerTextParts(const std::string& text);
// The text again from its parts (braces in words doubled).
std::string triggerTextJoin(const std::vector<TriggerTextPart>& parts);

struct TriggerDef {
    bool condition = false;
    bool value = false;  // a value from the game (a number), used in a number slot: "the unit's hit points"
    std::string id;        // "act.createunit"
    std::string category;  // "Units"
    std::string title;     // "Create units"
    std::string sentence;  // "create {n} {unit} at {where} for {player}"
    std::vector<TriggerParam> params;
    std::vector<TriggerListDef> lists;  // a container's lists of blocks
    std::vector<std::string> effects;   // local, cp, blocking, memory, unitloop
    std::string tier;                   // basic, units, advanced
    int cost = 1;
    std::string code;                   // template ({name}); empty: built by the code generator
    std::vector<std::string> eudplib;   // the eudplib calls it relies on
    std::string docs, pitfalls;
    std::string example;  // one use of the block in a map: its sentence with values, and why (the help page)
    // The sentence as a block with these values reads: a text with values shows only as many value slots as
    // the text has {} (up to two).
    std::string shownSentence(const std::vector<std::string>& args) const;
    // How many of the values `a`, `b` a text with values uses (-1: the block has none).
    int textValues(const std::vector<std::string>& args) const;

    bool has(const std::string& effect) const;
    int param(const std::string& name) const;  // index, or -1

    static const std::vector<TriggerDef>& all();
    static const TriggerDef* find(const std::string& id);
    static const std::vector<std::string>& categories(bool condition);
    static const std::vector<std::string>& valueCategories();
};

// --- The model -----------------------------------------------------------------------------------------------

struct TriggerCall {
    std::string def;                // TriggerDef::id
    std::vector<std::string> args;  // one per parameter (see value encodings below)
    bool enabled = true;
    std::vector<std::vector<TriggerCall>> lists;  // a container's blocks, one list per TriggerDef::lists
    // A number value can be a value block instead (args[i] then unused): argBlocks[i] holds it (empty: none).
    std::vector<std::vector<TriggerCall>> argBlocks;
    bool folded = false;
    // What this StarDraft does not know (from a newer one, or a block it no longer has), kept as it was so that
    // saving never loses it: values (as JSON text) and lists by their names, and other fields as JSON text.
    std::vector<std::pair<std::string, std::string>> extraArgs;
    std::vector<std::pair<std::string, std::vector<TriggerCall>>> extraLists;
    // Show text's values from the game ({#1}, {#2} in its text): kept as the list "values".
    std::vector<TriggerCall>* textValues(bool create = false);
    const std::vector<TriggerCall>* textValues() const;
    // After the text changed: new values from the game ({=id}) become {#n}, the values are numbered in the order
    // the text has them, and those the text no longer has go.
    void normalizeText();
    std::string extra;
    static TriggerCall make(const TriggerDef& d);  // default values; an If holds Always / Do nothing / Do nothing
};
// The type of a block's value i. Mostly the parameter's own type; "ValueOf:<param>" follows the property picked in
// that parameter (a unit's hit points take a number, its owner a player).
std::string triggerParamType(const TriggerDef& d, const std::vector<std::string>& args, int i);
// The property picked in a block, if it has a property parameter.
const TriggerProperty* triggerCallProperty(const TriggerDef& d, const std::vector<std::string>& args);
// Values are strings: a choice's code (AtLeast), a number (5), text, a unit type's id (0: Terran Marine; 229-232: Any
// unit, Men, Buildings, Factories), a location's number (1-255; 64: Anywhere), a switch number (1-256), a variable's
// name, an expression (gold * 2 + 5; see triggerExpression),
// a sound path, "the unit" or "@<serial>" (a unit picked on the map), a property's code (hp), a .dat entry's
// number (0; or an expression), a YesNo (1 / 0).

struct TriggerVariable {
    // (New kinds go at the end: the editor lists them in this order.)
    // Text: a text of the map's own (its value, kept as a string in the map; shown with {name:s}).
    enum class Kind { Number, Flag, PerPlayer, Array, Unit, Constant, Text, UnitGroup } kind = Kind::Number;
    static constexpr int kKinds = 8;
    int size = 8;              // arrays; unit groups: how many units they hold at most; texts: the most letters (bytes)
    static constexpr int kTextRoom = 218, kTextRoomMax = 2000;  // a text's default and largest maximum length
    std::string sizeConstant;  // arrays: the constant giving the size ("" = size)
    std::string initial;       // numbers, flags, constants, arrays' items; units: "@<serial>" to start as a picked unit
    static const char* kindLabel(Kind k);
};

struct TriggerNode {
    enum class Kind { Category, Folder, Trigger, Variable } kind = Kind::Trigger;
    std::string name;
    std::vector<TriggerNode> children;             // categories and folders
    std::vector<TriggerCall> conditions, actions;  // triggers
    TriggerVariable variable;                      // variables
    bool enabled = true;
    std::string comment;
    std::string extra;  // fields this StarDraft does not know, kept as JSON text
};

// What the map offers values from, for checks and code: its locations, sounds, placed units, players.
struct TriggerMapInfo {
    std::vector<std::string> locations;  // names of the used locations (Anywhere included)
    std::vector<std::string> sounds;     // sound files in the map archive
    struct PlacedUnit {
        uint32_t serial = 0;
        int type = 0, owner = 0, x = 0, y = 0;
    };
    std::vector<PlacedUnit> units;
    std::vector<std::string> unitNames;  // the game's unit type names, by id
    int playerForce[8] = {};             // force (0-3) of players 1-8
    bool playerActive[8] = {};           // human or computer
    bool known = false;                  // false: no map to check names against
};

struct TriggerIssue {
    enum class Level { Info, Warning, Error } level = Level::Warning;
    const TriggerNode* node = nullptr;  // the trigger or variable
    const TriggerCall* call = nullptr;  // the block (nullptr: the trigger itself)
    int param = -1;                     // the value (-1: the block)
    std::string message;
};

// How Build makes the code.
struct TriggerBuildOptions {
    // A debug build (the project's Debug build setting): every index that is calculated in the game is checked
    // before it is used. An index out of range shows a message on the screen one time ("Debug: <trigger> / <block>:
    // index 12 is not from 0 to 9"), and item 0 is used in its place. The game does not crash or desync.
    bool debug = false;
};

// The trigger (or variable) and block that made a line of the generated code. call is nullptr for a line that
// belongs to the trigger as a whole (its function), node is nullptr for the code StarDraft adds around them.
struct TriggerCodeLine {
    const TriggerNode* node = nullptr;
    const TriggerCall* call = nullptr;
};

// A change that makes a heavy trigger cost less each run (the Finding lag suggestions).
struct TriggerCostFix {
    enum class Kind {
        FilterInLoop,  // an If in For each unit that checks the unit's type or owner: the loop does the check
        EverySecond,   // the trigger's actions go into Every so often (1 second)
    } kind = Kind::EverySecond;
    std::string text;                        // what the change does
    std::vector<std::pair<int, int>> path;   // FilterInLoop: the loop, as (list, index) steps from the trigger
                                             // (list 0 conditions, 1 actions, 2 + n a block's list n)
    int before = 0, after = 0;               // the cost before and after the change
};

// The file (triggers.json) is the source of the triggers; its format is kept stable.
class TriggerSet {
public:
    static constexpr int kFormatVersion = 4;  // 4: values can be value blocks
    TriggerSet();  // the three empty categories
    // The categories: Definitions, On map start, On every trigger cycle.
    std::vector<TriggerNode> roots;

    static TriggerNode newTrigger(const std::string& name);  // condition Always, no actions
    static TriggerNode newFolder(const std::string& name);
    static TriggerNode newVariable(const std::string& name, TriggerVariable::Kind kind);

    // triggers.json; loading a missing file gives the empty set. Older files are migrated. A file from a newer
    // StarDraft loads (what is not understood is kept) but is not saved over: save() refuses.
    bool load(const std::filesystem::path& file, std::string& error);
    bool save(const std::filesystem::path& file, std::string& error) const;
    std::string text() const;  // the file's contents, as save writes them
    int fileVersion = kFormatVersion;  // of the file loaded
    bool newerFormat() const { return fileVersion > kFormatVersion; }
    bool loadFailed = false;  // the file is not valid trigger data: save() refuses, so the file stays as it is
    // Files from before version 3 name unit types and locations; this turns the names into ids and numbers
    // (with the map's names). Returns how many changed.
    int upgradeNames(const TriggerMapInfo& map);

    TriggerNode* at(const std::vector<int>& path);
    const TriggerNode* at(const std::vector<int>& path) const;
    std::vector<int> move(const std::vector<int>& from, const std::vector<int>& parent, int index);
    void remove(const std::vector<int>& path);

    // Every variable, by name.
    std::map<std::string, const TriggerNode*> variables() const;
    // Renames a variable and every use of it in values and expressions. Returns uses changed.
    int renameVariable(const std::string& from, const std::string& to);
    // Renames a trigger in Definitions and the Run a trigger blocks that run it; returns how many blocks changed.
    int renameTrigger(const std::string& from, const std::string& to);

    // Problems that would stop the build (errors) or deserve a look (warnings), and unclear types (info).
    std::vector<TriggerIssue> check(const TriggerMapInfo& map) const;
    // The epScript the triggers build to (deterministic). Issues are those of check(); with errors the code is
    // still produced (for viewing) but should not be built.
    // `lines` (when given) gets the block that made each line (lines[0] is line 1), so that an error that euddraft
    // finds in a line can select its block.
    // The helper functions the generated code imports (generated/sdlib.eps, next to triggers.eps).
    static std::string helperLibrary();
    std::string generate(const TriggerMapInfo& map, std::vector<TriggerIssue>& issues, const TriggerBuildOptions& options = {},
                         std::vector<TriggerCodeLine>* lines = nullptr) const;
    // Rough triggers per run of a trigger (loops multiply what they hold; unit loops by `units`, a loop over one
    // player's units by an eighth of them; a type filter runs the loop's blocks for a sixteenth of the units; Every
    // so often runs its blocks once in seconds x 24 runs).
    static int cost(const TriggerNode& trigger, int units = 1700);
    // A trigger is heavy above this cost.
    static constexpr int kHeavy = 500;
    // Changes that make a trigger cost less, those that keep what it does first (category: 0-2, where the trigger is). Only for
    // heavy triggers.
    static std::vector<TriggerCostFix> costFixes(const TriggerNode& trigger, int category);
    // Makes the change; false when the trigger no longer has what the fix changes.
    static bool applyCostFix(TriggerNode& trigger, const TriggerCostFix& fix);
};

// A ready-made trigger to start from (Getting started): the Triggers workspace adds it to its category.
struct TriggerTemplate {
    std::string title, about;
    int category = 2;  // 1 On map start, 2 On every trigger cycle
    TriggerNode node;
};
const std::vector<TriggerTemplate>& triggerTemplates();

// --- Expressions ---------------------------------------------------------------------------------------------
// Numbers can be expressions: whole numbers, variables, array[index], per-player[player] (the current player
// when no index is given), counter (the innermost Repeat's round), current player, + - * / % and brackets.
struct TriggerExprResult {
    bool ok = false;
    std::string code;   // epScript
    std::string error;  // why not
    bool constant = false;
    long long value = 0;  // when constant
};
TriggerExprResult triggerExpression(const std::string& text, const std::map<std::string, const TriggerNode*>& variables,
                                    int counters);

// The line of generated/triggers.eps that a line of euddraft's output names: "[Error 7041] Module "triggers" Line 146 :
// ..." (-1: none).
int triggerErrorLine(const std::string& logLine);

// A variable's name in generated code.
std::string triggerVariableCode(const std::string& name);
// How many items an array has: its size, or the value of the constant that gives it. Per player: 8.
int triggerListSize(const TriggerVariable& v, const std::map<std::string, const TriggerNode*>& variables);
// The [chatEvent] section for generated code: "__addr__ : sd_chat", then "text : number" for each chat command
// (none: the code has no chat commands).
std::vector<std::string> triggerChatLines(const std::string& code);

}  // namespace stardraft
