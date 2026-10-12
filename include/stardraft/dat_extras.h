#pragma once
// The Data workspace's tables that are not .dat files: the game's strings (stat_txt.tbl), the requirements
// (what a unit, upgrade, tech or order needs) and the button sets (each command card). GPU-free.
//
// Each table keeps the game's values and the project's edits; only edits are saved (dat/strings.json,
// dat/requirements.json, dat/buttons.json). Build writes them as epScript that runs once at map start.
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;

// --- Strings ---------------------------------------------------------------------------------------------------

// rez\stat_txt.tbl: unit, weapon, upgrade, tech and order names, button texts (a hotkey and a type byte, then the
// text) and messages. Text is UTF-8; control bytes (colors, line ends) are kept as they are.
class StatTxt {
public:
    bool load(const GameData& data, std::string& error);
    int count() const { return int(first_.size()); }
    // An entry's text: the part up to its first zero byte (for a button text, after its hotkey and type bytes: type
    // 0 is a zero byte). Some entries hold more parts after it (a unit's
    // StarEdit groups); those stay as they are.
    const std::string& stock(int i) const { return first_[size_t(i)]; }
    const std::string& text(int i) const;
    bool changed(int i) const { return edits_.count(i) > 0; }
    void set(int i, const std::string& text);
    void reset(int i) { edits_.erase(i); }
    int changedCount() const { return int(edits_.size()); }
    const std::map<int, std::string>& edits() const { return edits_; }
    void setEdits(std::map<int, std::string> e);

    // The rebuilt file. The game copies it over its own copy, so it must not be larger than that (capacity()).
    Bytes build() const;
    size_t capacity() const { return stockSize_; }

    bool loadEdits(const std::filesystem::path& file, std::string& error);
    bool saveEdits(const std::filesystem::path& file, std::string& error) const;  // removed when nothing is edited
    // The edits as text (the file's content; empty when nothing is edited), and back (for undo).
    std::string editsText() const;
    bool setEditsText(const std::string& text, std::string& error);
    // epScript: the rebuilt file copied over the game's (0x6D5A30 points to it). Empty when nothing is edited;
    // `error` when it does not fit.
    std::string editsScript(std::string& error) const;

    // Text for an edit box: control bytes as <XX> (hex), "<" as "\<". And back.
    static std::string escape(const std::string& raw);
    static std::string unescape(const std::string& text);

private:
    std::vector<std::string> first_, rest_;  // per entry: the text, and what follows its zero byte to the next entry
    std::map<int, std::string> edits_;
    size_t stockSize_ = 0;
};

// --- Requirements ----------------------------------------------------------------------------------------------

struct RequirementOpDef {
    uint16_t op;
    const char* name;
    const char* arg;  // the .dat its value is an entry of, or nullptr
};

// One step of a requirement list: an opcode (0xFF01..0xFF26) with its value, a unit the player must have
// (op < 0x100: op is the unit), or 0xFFFF, the end of a level's list (after "Level 1 needs" and "Level 2 needs").
struct RequirementOp {
    uint16_t op = 0, arg = 0;
    bool operator==(const RequirementOp&) const = default;
};
struct RequirementList {
    bool used = false;  // false: the entry has no list (its offset is 0)
    std::vector<RequirementOp> ops;
    bool operator==(const RequirementList&) const = default;
};

class Requirements {
public:
    // What the lists are for: units (to make), upgrades (to research), techs (to research), techs (to use), orders.
    static constexpr int kKinds = 5;
    struct Kind {
        const char* name;   // "units", "upgrades", "research", "tech use", "orders"
        const char* dat;    // the .dat whose entries they are for
        int entries;
        uint32_t address;   // where its lists start in memory
        uint32_t bytes;     // the game's room for them
        uint32_t offsets;   // each entry's u16 offset (in words from `address`; 0: no list)
    };
    static const Kind& kind(int k);
    static std::span<const RequirementOpDef> ops();
    static const RequirementOpDef* op(uint16_t op);

    Requirements();
    const RequirementList& list(int k, int entry) const { return lists_[size_t(k)][size_t(entry)]; }
    const RequirementList& stock(int k, int entry) const { return stock_[size_t(k)][size_t(entry)]; }
    bool changed(int k, int entry) const { return list(k, entry) != stock(k, entry); }
    void set(int k, int entry, RequirementList l) { lists_[size_t(k)][size_t(entry)] = std::move(l); }
    void reset(int k, int entry) { set(k, entry, stock(k, entry)); }
    int changedCount() const;

    // The lists share one block of memory (all five kinds, end to end): bytes the project's lists take, and the
    // block's size. A kind's lists may run into the room of the kinds after it.
    size_t used() const;
    static size_t capacity();

    bool loadEdits(const std::filesystem::path& file, std::string& error);
    bool saveEdits(const std::filesystem::path& file, std::string& error) const;
    // The edits as text (the file's content; empty when nothing is edited), and back (for undo).
    std::string editsText() const;
    bool setEditsText(const std::string& text, std::string& error);
    // epScript: the changed words of the block and of the offsets, written once at map start. Empty when nothing
    // is edited; `error` when the lists do not fit.
    std::string editsScript(std::string& error) const;

private:
    // The block and the five offset tables as they would be in memory.
    bool layout(Bytes& block, std::vector<std::vector<uint16_t>>& offsets) const;
    std::vector<std::vector<RequirementList>> stock_, lists_;
};

// --- Button sets -----------------------------------------------------------------------------------------------

struct ButtonFunctionDef {
    uint32_t address;  // in the game's code (the 1.16.1 address; StarCraft: Remastered's EUD layer maps it)
    const char* name;
    const char* arg;  // the kind of its value: "units", "upgrades", "techdata", "buttons", or nullptr
};

struct Button {
    uint16_t pos = 1;   // 1-9: left to right, top to bottom on the command card
    uint16_t icon = 0;  // cmdicons frame
    uint32_t condition = 0, action = 0;
    uint16_t conditionArg = 0, actionArg = 0;
    uint16_t enabledString = 0, disabledString = 0;  // stat_txt.tbl, 1-based (0: none)
    bool operator==(const Button&) const = default;
};

class ButtonSets {
public:
    static constexpr int kSets = 250;
    static constexpr uint32_t kTable = 0x5187E8;  // 12 bytes per set: count, pointer to the buttons, (unused)
    static std::span<const ButtonFunctionDef> conditions();
    static std::span<const ButtonFunctionDef> actions();
    static const ButtonFunctionDef* condition(uint32_t address);
    static const ButtonFunctionDef* action(uint32_t address);

    ButtonSets();
    const std::vector<Button>& set(int s) const { return sets_[size_t(s)]; }
    const std::vector<Button>& stock(int s) const { return stock_[size_t(s)]; }
    bool changed(int s) const { return set(s) != stock(s); }
    void set(int s, std::vector<Button> b) { sets_[size_t(s)] = std::move(b); }
    void reset(int s) { set(s, stock(s)); }
    int changedCount() const;

    bool loadEdits(const std::filesystem::path& file, std::string& error);
    bool saveEdits(const std::filesystem::path& file, std::string& error) const;
    // The edits as text (the file's content; empty when nothing is edited), and back (for undo).
    std::string editsText() const;
    bool setEditsText(const std::string& text, std::string& error);
    // The set a unit uses (its own: the unit's number). Another set: at map start, the unit's entry gets that set's
    // count and pointer (after the sets' own changes).
    int unitSet(int unit) const { return unitSets_[size_t(unit)]; }
    void setUnitSet(int unit, int set) { unitSets_[size_t(unit)] = set; }
    static constexpr int kUnits = 228;

    // epScript: each changed set as a new array in the map, and the set's count and pointer moved to it; then the
    // units that use another set.
    std::string editsScript() const;

private:
    std::vector<std::vector<Button>> stock_, sets_;
    std::vector<int> unitSets_;
};

// --- Units: what the status panel shows, and wireframes -----------------------------------------------------------

// A unit's status panel: two functions in the game's code (its 1.16.1 addresses), at 0x5193A0 + 12 x unit (+4 status,
// +8 display). EUD Editor 3 names them by the kind of unit they suit.
struct UnitPanelKind {
    const char* name;
    uint32_t status, display;
};

class UnitLooks {
public:
    static constexpr int kUnits = 228;
    static constexpr uint32_t kPanelTable = 0x5193A0;
    static std::span<const UnitPanelKind> kinds();
    static const UnitPanelKind* kindOf(uint32_t status, uint32_t display);  // nullptr: a pair of no kind

    struct Unit {
        uint32_t status = 0, display = 0;  // the panel's functions (0: the game's)
        int wireframe = -1, groupWireframe = -1, transportWireframe = -1;  // the unit whose wireframe shows (-1: its own)
        bool operator==(const Unit&) const = default;
    };
    UnitLooks();
    uint32_t stockStatus(int u) const { return stockStatus_[size_t(u)]; }
    uint32_t stockDisplay(int u) const { return stockDisplay_[size_t(u)]; }
    const Unit& unit(int u) const { return units_[size_t(u)]; }
    void set(int u, Unit v) { units_[size_t(u)] = v; }
    bool changed(int u) const { return units_[size_t(u)] != Unit{}; }
    int changedCount() const;
    // The status / display function a unit has, the project's or the game's.
    uint32_t status(int u) const { return unit(u).status ? unit(u).status : stockStatus(u); }
    uint32_t display(int u) const { return unit(u).display ? unit(u).display : stockDisplay(u); }

    // Wireframes there are: 228 single, 131 group, 106 transport (eudplib's 32- and 64-bit tables).
    static constexpr int kWireframes = 228, kGroupWireframes = 131, kTransportWireframes = 106;
    // units.dat Rank/Sublabel: the title under the unit's name in the status panel. Value v is stat_txt.tbl string
    // kRankString + v (1-based); 0 is no title. Values 1 to 22 are the game's titles, "Private" to "Admiral".
    static constexpr int kRanks = 23, kRankString = 1302;

    bool loadEdits(const std::filesystem::path& file, std::string& error);
    bool saveEdits(const std::filesystem::path& file, std::string& error) const;
    std::string editsText() const;
    bool setEditsText(const std::string& text, std::string& error);
    // epScript at map start: the panel functions written; the wireframes through eudplib (SetWirefram, SetGrpWire,
    // SetTranWire), which pick the 32- or 64-bit StarCraft's tables while the game runs.
    std::string editsScript() const;

private:
    std::vector<uint32_t> stockStatus_, stockDisplay_;
    std::vector<Unit> units_;
};

}  // namespace stardraft
