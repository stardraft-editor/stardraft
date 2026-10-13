#include "stardraft/triggers.h"

#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "json.h"
#include "stardraft/msqc.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>

#include "data/eud_names.inc"

namespace stardraft {
namespace fs = std::filesystem;

namespace {

struct TriggerCategoryRow {
    int condition;
    const char* name;
};
struct TriggerBlockRow {
    int condition;
    const char *id, *category, *title, *sentence, *params, *lists, *effects, *tier;
    int cost;
    const char *code, *eudplib, *docs, *pitfalls, *example;
};
struct TriggerStructRow {
    const char *name, *label, *dat;
};
struct TriggerPropertyRow {
    const char *structure, *code, *label, *group, *type;
    int scale;
    long long min, max;
    int advanced;
    const char* notes;
};
#include "data/trigger_blocks.inc"

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == sep) out.push_back(cur), cur.clear();
        else cur += ch;
    }
    if (!cur.empty() || (!s.empty() && s.back() == sep)) out.push_back(cur);
    return out;
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// --- Types -----------------------------------------------------------------------------------------------------

std::map<std::string, TriggerType> makeTypes() {
    std::map<std::string, TriggerType> m;
    auto add = [&](const std::string& name, const std::string& label, TriggerPicker p, std::vector<TriggerChoice> choices = {}, std::string help = {}) {
        m[name] = {name, label, p, std::move(choices), std::move(help)};
    };
    std::vector<TriggerChoice> players;
    for (int i = 1; i <= 12; ++i) players.push_back({"P" + std::to_string(i), "Player " + std::to_string(i)});
    players.push_back({"CurrentPlayer", "the current player"});
    add("Player", "player", TriggerPicker::Choice, players, "One player. The current player is the one For each player is on.");
    std::vector<TriggerChoice> groups = players;
    for (TriggerChoice c : std::vector<TriggerChoice>{{"AllPlayers", "all players"},
                                                      {"Foes", "the current player's enemies"},
                                                      {"Allies", "the current player's allies"},
                                                      {"NeutralPlayers", "neutral players"},
                                                      {"Force1", "Force 1"},
                                                      {"Force2", "Force 2"},
                                                      {"Force3", "Force 3"},
                                                      {"Force4", "Force 4"},
                                                      {"NonAlliedVictoryPlayers", "non-allied-victory players"}})
        groups.push_back(c);
    add("PlayerGroup", "player or group", TriggerPicker::Choice, groups, "A player, or a group of players (all, a force, allies, enemies).");
    add("Comparison", "comparison", TriggerPicker::Choice, {{"AtLeast", "at least"}, {"AtMost", "at most"}, {"Exactly", "exactly"}});
    std::vector<TriggerChoice> keys;
    for (const auto& k : msqcKeyList()) keys.push_back({k.token, k.label});
    add("Key", "key", TriggerPicker::Choice, keys, "A key of the keyboard.");
    add("MouseButton", "mouse button", TriggerPicker::Choice, {{"L", "left"}, {"R", "right"}, {"M", "middle"}}, "A mouse button.");
    add("NumberComparison", "comparison", TriggerPicker::Choice,
        {{">=", "at least"}, {"<=", "at most"}, {"==", "exactly"}, {"!=", "not"}, {">", "more than"}, {"<", "less than"}});
    add("Modifier", "change", TriggerPicker::Choice, {{"Add", "add"}, {"Subtract", "subtract"}, {"SetTo", "set to"}});
    add("VarModifier", "change", TriggerPicker::Choice, {{"SetTo", "set to"}, {"Add", "add"}, {"Subtract", "subtract"}});
    add("Resource", "resource", TriggerPicker::Choice, {{"Ore", "minerals"}, {"Gas", "gas"}, {"OreAndGas", "minerals and gas"}});
    add("Score", "score", TriggerPicker::Choice,
        {{"Total", "total"}, {"Units", "units"}, {"Buildings", "buildings"}, {"UnitsAndBuildings", "units and buildings"},
         {"Kills", "kills"}, {"Razings", "razings"}, {"KillsAndRazings", "kills and razings"}, {"Custom", "custom"}});
    add("SwitchState", "on or off", TriggerPicker::Choice, {{"Set", "on"}, {"Cleared", "off"}});
    add("SwitchAction", "switch change", TriggerPicker::Choice, {{"Set", "turn on"}, {"Clear", "turn off"}, {"Toggle", "flip"}, {"Random", "randomize"}});
    add("AllyStatus", "alliance", TriggerPicker::Choice, {{"Ally", "ally"}, {"Enemy", "enemy"}, {"AlliedVictory", "allied victory"}});
    add("Order", "order", TriggerPicker::Choice, {{"Move", "move"}, {"Patrol", "patrol"}, {"Attack", "attack"}});
    add("OnOff", "on or off", TriggerPicker::Choice, {{"Enable", "on"}, {"Disable", "off"}, {"Toggle", "the other way"}});
    add("TextValues", "text", TriggerPicker::Text, {}, "Text, with values in it: {gold} or {wave + 1} (a variable or sum), or a value from the game (Insert value).");
    add("TextPlace", "where", TriggerPicker::Choice,
        {{"chat", "as a message"}, {"line0", "on line 1"}, {"line1", "on line 2"}, {"line2", "on line 3"}, {"line3", "on line 4"}, {"line4", "on line 5"}, {"line5", "on line 6"}, {"line6", "on line 7"}, {"line7", "on line 8"}, {"line8", "on line 9"}, {"line9", "on line 10"}, {"line10", "on line 11"}, {"middle", "on line 12 (center of screen)"}});
    add("DisplayTo", "who sees it", TriggerPicker::Choice,
        {{"all", "all players"}, {"current", "the current player"}, {"P1", "Player 1"}, {"P2", "Player 2"}, {"P3", "Player 3"}, {"P4", "Player 4"},
         {"P5", "Player 5"}, {"P6", "Player 6"}, {"P7", "Player 7"}, {"P8", "Player 8"}});
    add("UnitState", "unit state", TriggerPicker::Choice,
        {{"is_burrowed()", "burrowed"}, {"is_in_transport()", "in a transport"}, {"is_in_building()", "in a building"},
         {"is_air()", "flying"}, {"is_hallucination()", "a hallucination"}, {"is_completed()", "finished"}, {"is_dying()", "dying"}});
    add("Invincible", "invincibility", TriggerPicker::Choice, {{"set_invincible()", "invincible"}, {"clear_invincible()", "not invincible"}});
    std::vector<TriggerChoice> ai;
    for (const char* n : kAIScripts) ai.push_back({n, n});
    add("AIScript", "AI script", TriggerPicker::Choice, ai, "A built-in computer AI.");
    add("Number", "number", TriggerPicker::Number, {}, "A whole number, or an expression: variables, + - * / %, array[index].");
    add("Count", "how many (0 = all)", TriggerPicker::Number, {}, "How many units; 0 means all of them.");
    add("Percent", "percent (0-100)", TriggerPicker::Number);
    add("Seconds", "seconds", TriggerPicker::Number, {}, "Game seconds.");
    add("Milliseconds", "milliseconds", TriggerPicker::Number);
    add("Address", "memory address", TriggerPicker::Number, {}, "An address in StarCraft's memory, usually written in hex (0x58A364).");
    add("Text", "text", TriggerPicker::Text, {}, "Text shown in the game.");
    add("Code", "epScript", TriggerPicker::Code);
    add("Sound", "sound file", TriggerPicker::Sound, {}, "A sound file stored in the map.");
    add("Switch", "switch", TriggerPicker::Switch, {}, "One of the 256 switches.");
    add("Location", "location", TriggerPicker::Location, {}, "One of the map's locations.");
    add("UnitType", "unit type", TriggerPicker::UnitType, {}, "A kind of unit (Terran Marine), not one unit.");
    add("UnitFilter", "unit type or group", TriggerPicker::UnitFilter, {}, "A kind of unit, or Any unit / Men / Buildings / Factories.");
    add("UnitRef", "one unit", TriggerPicker::UnitRef, {},
        "One unit in the game: \"the unit\" inside For each unit, a unit variable, or a unit picked on the map.");
    add("Flag", "flag", TriggerPicker::Variable, {}, "A flag variable.");
    add("FlagTarget", "flag", TriggerPicker::Variable, {}, "A flag variable to change.");
    add("NumberTarget", "number variable", TriggerPicker::Variable, {},
        "A number variable to change: a number, an array value (name[index]) or a per-player value (name[player]).");
    add("UnitTarget", "unit variable", TriggerPicker::Variable, {}, "A unit variable to remember a unit in.");
    add("TextTarget", "text variable", TriggerPicker::Variable, {}, "A text variable to change.");
    add("TriggerRef", "trigger", TriggerPicker::Trigger, {}, "A trigger in Definitions.");
    add("ListTarget", "list (or number per player)", TriggerPicker::Variable, {}, "A list of numbers, or a number per player (a list of 8, one per player).");
    add("Index", "which item", TriggerPicker::Number, {},
        "Which item: counted from 0. A number, a variable or constant, the loop's counter, or the current player (Player 1 is 0).");
    add("UnitProperties", "unit properties", TriggerPicker::Unclear, {}, "Hit points, shields, energy and states of created units: no picker yet.");
    add("YesNo", "yes or no", TriggerPicker::Choice, {{"1", "yes"}, {"0", "no"}});
    add("Locale", "language", TriggerPicker::Choice,
        {{"\"enUS\"", "English"}, {"\"frFR\"", "French"}, {"\"itIT\"", "Italian"}, {"\"deDE\"", "German"}, {"\"esES\"", "Spanish (Spain)"},
         {"\"esMX\"", "Spanish (Latin America)"}, {"\"ptBR\"", "Portuguese"}, {"\"zhCN\"", "Chinese (simplified)"}, {"\"zhTW\"", "Chinese (traditional)"},
         {"\"jaJP\"", "Japanese"}, {"\"koUS\"", "Korean (transliterated)"}, {"\"koKR\"", "Korean"}, {"\"plPL\"", "Polish"}, {"\"ruRU\"", "Russian"}},
        "The language StarCraft runs in on a computer.");
    add("AnyLocale", "language", TriggerPicker::Choice,
        {{"any", "every language"}, {"\"enUS\"", "English"}, {"\"frFR\"", "French"}, {"\"itIT\"", "Italian"}, {"\"deDE\"", "German"},
         {"\"esES\"", "Spanish (Spain)"}, {"\"esMX\"", "Spanish (Latin America)"}, {"\"ptBR\"", "Portuguese"}, {"\"zhCN\"", "Chinese (simplified)"},
         {"\"zhTW\"", "Chinese (traditional)"}, {"\"jaJP\"", "Japanese"}, {"\"koUS\"", "Korean (transliterated)"}, {"\"koKR\"", "Korean"},
         {"\"plPL\"", "Polish"}, {"\"ruRU\"", "Russian"}});
    add("PlayerKind", "kind of player", TriggerPicker::Choice, {{"any", "any"}, {"6", "human"}, {"5", "computer"}, {"3", "rescuable"}, {"7", "neutral"}});
    add("PlayerRace", "race", TriggerPicker::Choice, {{"any", "any race"}, {"0", "Zerg"}, {"1", "Terran"}, {"2", "Protoss"}});
    add("PlayerForce", "force", TriggerPicker::Choice, {{"any", "any force"}, {"0", "Force 1"}, {"1", "Force 2"}, {"2", "Force 3"}, {"3", "Force 4"}});
    add("UnitGroupTarget", "unit group", TriggerPicker::Variable, {}, "A unit group variable: units added to it, to go through later.");
    add("GameString", "game text", TriggerPicker::Number, {}, "A text of the game (stat_txt.tbl), by number as on the Strings tab.");
    {  // buttons: a place on the command card, and the game's functions for when a button shows and what it does
        std::vector<TriggerChoice> places, conds, acts;
        for (int p = 1; p <= 9; ++p) places.push_back({std::to_string(p), "row " + std::to_string((p - 1) / 3 + 1) + ", column " + std::to_string((p - 1) % 3 + 1)});
        for (const auto& f : ButtonSets::conditions()) conds.push_back({std::to_string(f.address), f.name});
        for (const auto& f : ButtonSets::actions()) acts.push_back({std::to_string(f.address), f.name});
        add("ButtonPlace", "place on the card", TriggerPicker::Choice, places);
        add("ButtonIndex", "button", TriggerPicker::Number, {}, "One of the set's buttons (as the Buttons tab lists them), or a new one at the end.");
        add("Button", "button", TriggerPicker::Button, {}, "A whole button: its place, icon, when it shows, what it does, its tooltips.");
        add("ButtonCondition", "when it shows", TriggerPicker::Choice, conds, "A function of the game that decides when the button shows.");
        add("ButtonAction", "what it does", TriggerPicker::Choice, acts, "A function of the game that the button runs.");
    }
    add("Researched", "researched or not", TriggerPicker::Choice, {{"1", "researched"}, {"0", "not researched"}});
    add("VitalChange", "increase or decrease", TriggerPicker::Choice, {{"1", "increase"}, {"0", "decrease"}});
    // The orders of one unit, by what they need: nothing, a place, or a unit (the eudplib order ids).
    add("UnitOrderNone", "order", TriggerPicker::Choice, {{"1", "stop"}, {"107", "hold position"}, {"116", "burrow"}, {"118", "unburrow"}, {"98", "go into siege mode"}, {"99", "go into tank mode"}, {"109", "cloak"}, {"110", "decloak"}, {"72", "lift off"}, {"111", "unload all"}});
    add("UnitOrderPoint", "order", TriggerPicker::Choice, {{"6", "move"}, {"14", "attack move"}, {"152", "patrol"}, {"112", "move and unload"}, {"71", "land"}, {"132", "place a spider mine"}, {"142", "cast Psionic Storm"}, {"119", "cast Dark Swarm"}, {"122", "cast EMP Shockwave"}, {"144", "cast Plague"}, {"146", "cast Ensnare"}, {"147", "cast Stasis Field"}, {"138", "cast Recall"}, {"139", "cast Scanner Sweep"}, {"181", "cast Disruption Web"}, {"186", "cast Maelstrom"}});
    add("UnitOrderUnit", "order", TriggerPicker::Choice, {{"10", "attack"}, {"49", "follow"}, {"34", "repair"}, {"79", "gather"}, {"92", "load into"}, {"113", "cast Yamato Gun"}, {"115", "cast Lockdown"}, {"120", "cast Parasite"}, {"121", "cast Spawn Broodlings"}, {"141", "cast Defensive Matrix"}, {"143", "cast Irradiate"}, {"145", "cast Consume"}, {"148", "cast Hallucination"}, {"176", "heal"}, {"180", "cast Restoration"}, {"182", "cast Mind Control"}, {"184", "cast Feedback"}, {"185", "cast Optical Flare"}});
    add("UnitVital", "hit points, shields or energy", TriggerPicker::Choice, {{"0", "hit points"}, {"1", "shields"}, {"2", "energy"}});
    std::vector<TriggerChoice> ids;
    for (int i = 1; i <= 12; ++i) ids.push_back({"P" + std::to_string(i), "Player " + std::to_string(i)});
    ids.push_back({"getcurpl()", "the current player"});
    add("PlayerId", "player", TriggerPicker::Choice, ids, "One player (as stored in a unit: who owns it, who attacked it last).");
    return m;
}

std::map<std::string, TriggerType>& types() {
    static std::map<std::string, TriggerType> m = makeTypes();
    return m;
}

std::string humanize(const std::string& kind) {
    std::string out;
    for (size_t i = 0; i < kind.size(); ++i) {
        const char ch = kind[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(ch)) && std::islower(static_cast<unsigned char>(kind[i - 1]))) out += ' ';
        out += char(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

const std::map<std::string, std::string>& datNouns() {
    static const std::map<std::string, std::string> m = {
        {"units", "unit type"}, {"weapons", "weapon"},   {"flingy", "flingy"},       {"sprites", "sprite"},       {"images", "image"},
        {"upgrades", "upgrade"}, {"techdata", "technology"}, {"orders", "order"}, {"sfxdata", "sound (sfxdata)"}, {"portdata", "portrait"},
        {"icons", "icon"}, {"buttons", "button set"}, {"iscript", "iscript ID"}};
    return m;
}

// The families: Property:<structure>, Dat:<dat>, DatValues:<dat>/<field>, Unclear:<kind>.
std::optional<TriggerType> familyType(const std::string& name) {
    const size_t colon = name.find(':');
    if (colon == std::string::npos) return std::nullopt;
    const std::string family = name.substr(0, colon), arg = name.substr(colon + 1);
    TriggerType t;
    t.name = name, t.of = arg;
    if (family == "Property") {
        if (triggerProperties(arg).empty()) return std::nullopt;
        t.picker = TriggerPicker::Property;
        t.label = triggerStructureLabel(arg) + " property";
        t.help = "Which value of the " + triggerStructureLabel(arg) + " to use; the value's own type follows it.";
        return t;
    }
    if (family == "Dat") {
        auto noun = datNouns().find(arg);
        if (noun == datNouns().end()) return std::nullopt;
        t.picker = TriggerPicker::DatEntry;
        t.label = noun->second;
        const DatSchema* sc = DatSchema::find(arg);
        t.count = sc ? sc->entries : arg == "buttons" ? ButtonSets::kSets : int(DatSchema::entryNames(arg).size());
        t.help = "An entry of " + arg + ".dat, by number (as in the Data workspace); a variable works too.";
        return t;
    }
    if (family == "DatValues") {
        const size_t slash = arg.find('/');
        const DatSchema* sc = slash == std::string::npos ? nullptr : DatSchema::find(arg.substr(0, slash));
        const int field = slash == std::string::npos ? -1 : std::atoi(arg.c_str() + slash + 1);
        if (!sc || field < 0 || field >= int(sc->fields.size()) || sc->fields[size_t(field)].valueNames.empty()) return std::nullopt;
        const DatField& f = sc->fields[size_t(field)];
        t.picker = TriggerPicker::Choice;
        t.label = humanize(f.name);
        t.of = sc->name;
        for (size_t i = 0; i < f.valueNames.size(); ++i) t.choices.push_back({std::to_string(i), f.valueNames[i]});
        return t;
    }
    if (family == "Unclear") {
        t.picker = TriggerPicker::Unclear;
        t.label = humanize(arg);
        t.help = "StarDraft does not know these values yet (" + arg + "): typed as a number.";
        return t;
    }
    return std::nullopt;
}

}  // namespace

const TriggerType* triggerType(const std::string& name) {
    static std::mutex lock;
    std::lock_guard<std::mutex> hold(lock);
    auto& m = types();
    auto it = m.find(name);
    if (it != m.end()) return &it->second;
    if (auto t = familyType(name)) return &m.emplace(name, std::move(*t)).first->second;
    return nullptr;
}

std::string triggerTypeDefault(const std::string& name) {
    const TriggerType* t = triggerType(name);
    if (!t) return "0";
    switch (t->picker) {
    case TriggerPicker::Choice: return name == "YesNo" ? "1" : t->choices.empty() ? "" : t->choices[0].code;
    case TriggerPicker::UnitType:
    case TriggerPicker::UnitFilter: return "Terran Marine";
    case TriggerPicker::UnitRef: return "the unit";
    case TriggerPicker::Location: return "Anywhere";
    case TriggerPicker::Switch: return "1";
    case TriggerPicker::Property: return triggerProperties(t->of).empty() ? "" : triggerProperties(t->of)[0].code;
    case TriggerPicker::Text:
    case TriggerPicker::Code:
    case TriggerPicker::Sound:
    case TriggerPicker::Trigger:
    case TriggerPicker::Variable: return "";
    default: return "0";
    }
}

const std::vector<TriggerProperty>& triggerProperties(const std::string& structure) {
    static const std::map<std::string, std::vector<TriggerProperty>> all = [] {
        std::map<std::string, std::vector<TriggerProperty>> m;
        for (const auto& r : kTriggerProperties)
            m[r.structure].push_back({r.structure, r.code, r.label, r.group, r.type, r.scale, r.min, r.max, r.advanced != 0, r.notes});
        return m;
    }();
    static const std::vector<TriggerProperty> none;
    auto it = all.find(structure);
    return it != all.end() ? it->second : none;
}

const TriggerProperty* triggerProperty(const std::string& structure, const std::string& code) {
    for (const auto& p : triggerProperties(structure))
        if (p.code == code) return &p;
    return nullptr;
}

std::string triggerStructureLabel(const std::string& structure) {
    for (const auto& r : kTriggerStructs)
        if (structure == r.name) return r.label;
    return structure;
}

std::string triggerParamType(const TriggerDef& d, const std::vector<std::string>& args, int i) {
    if (i < 0 || i >= int(d.params.size())) return "";
    const std::string& type = d.params[size_t(i)].type;
    if (type.rfind("ValueOf:", 0) != 0) return type;
    const int of = d.param(type.substr(8));
    if (of < 0) return "Number";
    const TriggerType* pt = triggerType(d.params[size_t(of)].type);
    const std::string code = of < int(args.size()) ? args[size_t(of)] : d.params[size_t(of)].def;
    const TriggerProperty* prop = pt && pt->picker == TriggerPicker::Property ? triggerProperty(pt->of, code) : nullptr;
    return prop ? prop->type : "Number";
}

const TriggerProperty* triggerCallProperty(const TriggerDef& d, const std::vector<std::string>& args) {
    for (size_t i = 0; i < d.params.size(); ++i)
        if (const TriggerType* t = triggerType(d.params[i].type); t && t->picker == TriggerPicker::Property)
            return triggerProperty(t->of, i < args.size() ? args[i] : d.params[i].def);
    return nullptr;
}

bool triggerPlayerType(const std::string& name) { return name == "Player" || name == "PlayerGroup" || name == "PlayerId"; }

bool triggerTypeClear(const std::string& name) {
    const TriggerType* t = triggerType(name);
    return t && t->picker != TriggerPicker::Unclear;
}

// --- Blocks ------------------------------------------------------------------------------------------------------

namespace {
// A list's key in the file: its title in lower case, spaces as _ ("Any of" -> "any_of").
std::string listKey(const std::string& title) {
    std::string k;
    for (char ch : title) k += ch == ' ' ? '_' : char(std::tolower(static_cast<unsigned char>(ch)));
    return k;
}
}  // namespace

int triggerPlaceholders(const std::string& text) {
    int n = 0;
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != '{') continue;
        if (text[i + 1] == '{') {  // "{{": a brace
            ++i;
            continue;
        }
        if (text[i + 1] == '}' || text[i + 1] == ':') ++n;
    }
    return n;
}
const std::vector<std::pair<std::string, std::string>>& triggerTextFormats() {
    static const std::vector<std::pair<std::string, std::string>> f = {
        {"", "a number"}, {"n", "a player's name"}, {"c", "a player's color"}, {"x", "hex"}, {"X", "hex (capitals)"},
        {"s", "the text at an address"}, {"t", "the text at an EPD"}};
    return f;
}
namespace {
// "gold:x" -> "gold", "x" (only a format code eudplib knows splits off).
std::string splitFormat(std::string& in) {
    const size_t colon = in.rfind(':');
    if (colon == std::string::npos) return {};
    const std::string f = in.substr(colon + 1);
    for (const auto& [code, name] : triggerTextFormats())
        if (!code.empty() && f == code) {
            in.erase(colon);
            while (!in.empty() && in.back() == ' ') in.pop_back();
            return f;
        }
    return {};
}
}  // namespace

std::vector<TriggerTextPart> triggerTextParts(const std::string& text) {
    std::vector<TriggerTextPart> out;
    std::string words;
    auto flush = [&] {
        if (!words.empty()) out.push_back({TriggerTextPart::Kind::Words, words, -1}), words.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if ((ch == '{' || ch == '}') && i + 1 < text.size() && text[i + 1] == ch) {  // "{{", "}}": a brace
            words += ch, ++i;
            continue;
        }
        if (ch == '{') {
            const size_t end = text.find('}', i + 1);
            if (end == std::string::npos) {  // no end: words
                words += ch;
                continue;
            }
            std::string in = text.substr(i + 1, end - i - 1);
            const size_t a = in.find_first_not_of(' '), b = in.find_last_not_of(' ');
            in = a == std::string::npos ? "" : in.substr(a, b - a + 1);
            flush();
            const std::string format = splitFormat(in);
            if (in.size() > 1 && in[0] == '#' && std::all_of(in.begin() + 1, in.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
                out.push_back({TriggerTextPart::Kind::GameValue, in, std::atoi(in.c_str() + 1) - 1, format});
            else if (in.size() > 1 && in[0] == '=')
                out.push_back({TriggerTextPart::Kind::NewGameValue, in.substr(1), -1, format});
            else
                out.push_back({TriggerTextPart::Kind::Expression, in.empty() ? "0" : in, -1, format});
            i = end;
            continue;
        }
        words += ch;
    }
    flush();
    return out;
}

std::string triggerTextJoin(const std::vector<TriggerTextPart>& parts) {
    std::string out;
    for (const auto& p : parts) switch (p.kind) {
        case TriggerTextPart::Kind::Words:
            for (char ch : p.text) out += ch == '{' ? "{{" : ch == '}' ? "}}" : std::string(1, ch);
            break;
        case TriggerTextPart::Kind::Expression: out += "{" + p.text + (p.format.empty() ? "" : ":" + p.format) + "}"; break;
        case TriggerTextPart::Kind::GameValue: out += "{#" + std::to_string(p.value + 1) + (p.format.empty() ? "" : ":" + p.format) + "}"; break;
        case TriggerTextPart::Kind::NewGameValue: out += "{=" + p.text + (p.format.empty() ? "" : ":" + p.format) + "}"; break;
        }
    return out;
}

std::vector<TriggerCall>* TriggerCall::textValues(bool create) {
    for (auto& [k, v] : extraLists)
        if (k == "values") return &v;
    if (!create) return nullptr;
    extraLists.push_back({"values", {}});
    return &extraLists.back().second;
}
const std::vector<TriggerCall>* TriggerCall::textValues() const {
    for (const auto& [k, v] : extraLists)
        if (k == "values") return &v;
    return nullptr;
}

void TriggerCall::normalizeText() {
    const TriggerDef* d = TriggerDef::find(def);
    const int ti = d ? d->param("text") : -1;
    if (ti < 0 || ti >= int(args.size())) return;
    auto parts = triggerTextParts(args[size_t(ti)]);
    const std::vector<TriggerCall> before = textValues() ? *textValues() : std::vector<TriggerCall>{};
    std::vector<TriggerCall> after;
    for (auto& p : parts) {
        if (p.kind == TriggerTextPart::Kind::GameValue && p.value >= 0 && p.value < int(before.size())) {
            after.push_back(before[size_t(p.value)]);
        } else if (p.kind == TriggerTextPart::Kind::NewGameValue || p.kind == TriggerTextPart::Kind::GameValue) {
            const TriggerDef* vd = TriggerDef::find(p.text);
            if (p.kind == TriggerTextPart::Kind::GameValue || !vd || !vd->value) {  // a value that is not there: words
                p = {TriggerTextPart::Kind::Words, "", -1, ""};
                continue;
            }
            after.push_back(make(*vd));
        } else {
            continue;
        }
        p.kind = TriggerTextPart::Kind::GameValue, p.value = int(after.size()) - 1;
    }
    args[size_t(ti)] = triggerTextJoin(parts);
    if (after.empty()) {
        std::erase_if(extraLists, [](const auto& l) { return l.first == "values"; });
        return;
    }
    *textValues(true) = std::move(after);
}

int TriggerDef::textValues(const std::vector<std::string>& args) const {
    const int t = param("text"), a = param("a"), b = param("b");
    if (t < 0 || a < 0 || b < 0 || t >= int(args.size())) return -1;
    return std::min(2, triggerPlaceholders(args[size_t(t)]));
}
std::string TriggerDef::shownSentence(const std::vector<std::string>& args) const {
    const int n = textValues(args);
    if (n < 0 || n == 2) return sentence;
    std::string s = sentence;
    const std::string both = " with {a} and {b}";
    if (const size_t at = s.find(both); at != std::string::npos) s.replace(at, both.size(), n == 1 ? " with {a}" : "");
    return s;
}

bool TriggerDef::has(const std::string& effect) const { return std::find(effects.begin(), effects.end(), effect) != effects.end(); }

int TriggerDef::param(const std::string& name) const {
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].name == name) return int(i);
    return -1;
}

const std::vector<TriggerDef>& TriggerDef::all() {
    static const std::vector<TriggerDef> defs = [] {
        std::vector<TriggerDef> out;
        for (const auto& r : kTriggerBlocks) {
            TriggerDef d;
            d.condition = r.condition == 1;
            d.value = r.condition == 2;
            d.id = r.id, d.category = r.category, d.title = r.title, d.sentence = r.sentence, d.tier = r.tier, d.cost = r.cost;
            d.code = r.code, d.docs = r.docs, d.pitfalls = r.pitfalls, d.example = r.example;
            for (const auto& p : split(r.params, ';')) {
                if (p.empty()) continue;
                const auto f = split(p, '|');
                TriggerParam tp;
                tp.name = f.size() > 0 ? f[0] : "", tp.type = f.size() > 1 ? f[1] : "", tp.def = f.size() > 2 ? f[2] : "";
                if (f.size() > 3 && f[3].find("..") != std::string::npos)
                    tp.min = std::stoll(f[3].substr(0, f[3].find(".."))), tp.max = std::stoll(f[3].substr(f[3].find("..") + 2));
                d.params.push_back(tp);
            }
            for (const auto& l : split(r.lists, ';'))
                if (!l.empty()) {
                    const auto f = split(l, '|');
                    d.lists.push_back({listKey(f[0]), f[0], f.size() > 1 && f[1] == "condition"});
                }
            for (const auto& e : split(r.effects, ','))
                if (!e.empty()) d.effects.push_back(e);
            for (const auto& e : split(r.eudplib, ','))
                if (!e.empty()) d.eudplib.push_back(e);
            out.push_back(std::move(d));
        }
        return out;
    }();
    return defs;
}

const TriggerDef* TriggerDef::find(const std::string& id) {
    for (const auto& d : all())
        if (d.id == id) return &d;
    return nullptr;
}

const std::vector<std::string>& TriggerDef::categories(bool condition) {
    static const auto pick = [](bool cond) {
        std::vector<std::string> v;
        for (const auto& r : kTriggerCategories)
            if (r.condition == (cond ? 1 : 0)) v.push_back(r.name);
        return v;
    };
    static const std::vector<std::string> conds = pick(true), acts = pick(false);
    return condition ? conds : acts;
}

const std::vector<std::string>& TriggerDef::valueCategories() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> out;
        for (const auto& r : kTriggerCategories)
            if (r.condition == 2) out.push_back(r.name);
        return out;
    }();
    return v;
}

TriggerCall TriggerCall::make(const TriggerDef& d) {
    TriggerCall c;
    c.def = d.id;
    for (const auto& p : d.params) c.args.push_back(p.def);
    c.argBlocks.resize(d.params.size());
    c.lists.resize(d.lists.size());
    // An If starts with Always as its condition; the lists of actions start empty (no Do nothing placeholders).
    if (d.id == "act.if") c.lists[0].push_back(make(*TriggerDef::find("cond.always")));
    return c;
}

const char* TriggerVariable::kindLabel(Kind k) {
    switch (k) {
    case Kind::Number: return "number";
    case Kind::Flag: return "flag";
    case Kind::PerPlayer: return "number per player";
    case Kind::Array: return "list of numbers";
    case Kind::Unit: return "unit variable";
    case Kind::Constant: return "constant number";
    case Kind::Text: return "text";
    case Kind::UnitGroup: return "unit group";
    }
    return "number";
}

std::string triggerVariableCode(const std::string& name) { return "v_" + name; }

// --- Expressions -------------------------------------------------------------------------------------------------

namespace {
struct ExprParser {
    const std::string& t;
    const std::map<std::string, const TriggerNode*>& vars;
    int counters;
    // A debug build: wraps an index worked out in the game (its code, the list's size) in a check.
    const std::function<std::string(const std::string&, int)>* wrap = nullptr;
    size_t i = 0;
    std::string error;
    void ws() {
        while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) ++i;
    }
    struct Val {
        std::string code;
        bool constant = false;
        long long v = 0;
    };
    bool fail(const std::string& e) {
        if (error.empty()) error = e;
        return false;
    }
    bool expr(Val& out) { return additive(out); }
    bool additive(Val& out) {
        if (!term(out)) return false;
        for (;;) {
            ws();
            if (i >= t.size() || (t[i] != '+' && t[i] != '-')) return true;
            const char op = t[i++];
            Val r;
            if (!term(r)) return false;
            if (out.constant && r.constant) out.v = op == '+' ? out.v + r.v : out.v - r.v, out.code = std::to_string(out.v);
            else out.code = out.code + " " + op + " " + r.code, out.constant = false;
        }
    }
    bool term(Val& out) {
        if (!factor(out)) return false;
        for (;;) {
            ws();
            if (i >= t.size() || (t[i] != '*' && t[i] != '/' && t[i] != '%')) return true;
            const char op = t[i++];
            Val r;
            if (!factor(r)) return false;
            if ((op == '/' || op == '%') && r.constant && r.v == 0) return fail("Do not divide by zero");
            if (out.constant && r.constant) {
                out.v = op == '*' ? out.v * r.v : op == '/' ? out.v / r.v : out.v % r.v;
                out.code = std::to_string(out.v);
            } else {
                out.code = "(" + out.code + " " + op + " " + r.code + ")", out.constant = false;
            }
        }
    }
    bool factor(Val& out) {
        ws();
        if (i >= t.size()) return fail("A number or variable is missing");
        if (t[i] == '(') {
            ++i;
            if (!expr(out)) return false;
            ws();
            if (i >= t.size() || t[i] != ')') return fail("A ) is missing");
            ++i;
            if (!out.constant) out.code = "(" + out.code + ")";
            return true;
        }
        if (t[i] == '-') {  // a negative number, or minus a value
            ++i;
            if (!factor(out)) return false;
            if (out.constant) out.v = -out.v, out.code = std::to_string(out.v);
            else out.code = "(0 - " + out.code + ")";
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(t[i]))) {
            size_t end = i;
            long long v = 0;
            if (t.compare(i, 2, "0x") == 0 || t.compare(i, 2, "0X") == 0) {
                end = i + 2;
                while (end < t.size() && std::isxdigit(static_cast<unsigned char>(t[end]))) ++end;
                if (end == i + 2) return fail("A hex number has no digits");
                v = std::stoll(t.substr(i + 2, end - i - 2), nullptr, 16);
            } else {
                while (end < t.size() && std::isdigit(static_cast<unsigned char>(t[end]))) ++end;
                v = std::stoll(t.substr(i, end - i));
            }
            const bool hex = end > i + 1 && (t[i + 1] == 'x' || t[i + 1] == 'X');
            out = {hex ? "0x" + t.substr(i + 2, end - i - 2) : std::to_string(v), true, v};  // hex stays hex (masks, addresses)
            i = end;
            return true;
        }
        if (!identStart(t[i])) return fail(std::string("unexpected '") + t[i] + "'");
        size_t end = i;
        while (end < t.size() && identChar(t[end])) ++end;
        std::string name = t.substr(i, end - i);
        i = end;
        // name(player): where the player's name is (a text; shown with :s).
        if (name == "name") {
            ws();
            if (i >= t.size() || t[i] != '(') return fail("name needs a player: name(P1), name(current player)");
            ++i;
            Val p;
            if (!expr(p)) return false;
            ws();
            if (i >= t.size() || t[i] != ')') return fail("A ) is missing");
            ++i;
            out = {"(0x57EEEB + 36 * " + p.code + ")"};
            return true;
        }
        // tbl(n): where text n of the game (stat_txt.tbl, 1 and up) is: a text, shown with :s. n can be a number or a value.
        if (name == "tbl") {
            ws();
            if (i >= t.size() || t[i] != '(') return fail("tbl needs the number of a game text: tbl(1)");
            ++i;
            Val n;
            if (!expr(n)) return false;
            ws();
            if (i >= t.size() || t[i] != ')') return fail("A ) is missing");
            ++i;
            if (n.constant && n.v < 1) return fail("The game's texts start at 1");
            out = {"GetTBLAddr(" + n.code + ")"};
            return true;
        }
        // A player's property: P1.ore, P2.gas, P3.killScore (the names of the player's properties; ore for minerals).
        if (name.size() == 2 && (name[0] == 'P' || name[0] == 'p') && name[1] >= '1' && name[1] <= '8' && i < t.size() && t[i] == '.') {
            ++i;
            size_t e = i;
            while (e < t.size() && identChar(t[e])) ++e;
            std::string member = t.substr(i, e - i);
            i = e;
            std::transform(member.begin(), member.end(), member.begin(), ::tolower);
            if (member == "ore" || member == "minerals") member = "mineral";
            for (const auto& pr : triggerProperties("TrgPlayer")) {
                std::string code = pr.code;
                std::transform(code.begin(), code.end(), code.begin(), ::tolower);
                if (code == member) {
                    out = {"TrgPlayer(" + std::to_string(name[1] - '1') + ")." + pr.code};
                    return true;
                }
            }
            return fail("A player has no " + member + ". For example: P1.ore, P1.gas, P1.killScore");
        }
        if (name.size() == 2 && (name[0] == 'P' || name[0] == 'p') && name[1] >= '1' && name[1] <= '8') {  // P1-P8: the player's number (0-7)
            out.v = name[1] - '1', out.constant = true, out.code = std::to_string(out.v);
            return true;
        }
        if (name == "current") {  // "current player"
            ws();
            if (t.compare(i, 6, "player") == 0) i += 6;
            out = {"getcurpl()"};
            return true;
        }
        if (name == "counter") {
            if (!counters) return fail("\"counter\" has a value only in Repeat, For each number or For each item");
            out = {"rp" + std::to_string(counters)};
            return true;
        }
        auto it = vars.find(name);
        if (it == vars.end()) return fail("There is no variable with the name " + name);
        const TriggerVariable& v = it->second->variable;
        ws();
        const bool indexed = i < t.size() && t[i] == '[';
        Val idx;
        if (indexed) {
            ++i;
            if (!expr(idx)) return false;
            ws();
            if (i >= t.size() || t[i] != ']') return fail("A ] is missing");
            ++i;
        }
        const std::string code = triggerVariableCode(name);
        switch (v.kind) {
        case TriggerVariable::Kind::Number:
        case TriggerVariable::Kind::Flag:
            if (indexed) return fail(name + " is not an array");
            out = {code};
            return true;
        case TriggerVariable::Kind::Constant: {
            if (indexed) return fail(name + " is a constant, not a list");
            const long long value = std::atoll(v.initial.c_str());
            out = {code, true, value};
            return true;
        }
        case TriggerVariable::Kind::PerPlayer:
            if (idx.constant && (idx.v < 0 || idx.v > 7)) return fail("Use a player from 0 to 7");
            if (indexed && !idx.constant && wrap) idx.code = (*wrap)(idx.code, 8);
            out = {code + "[" + (indexed ? idx.code : "getcurpl()") + "]"};
            return true;
        case TriggerVariable::Kind::Array: {
            if (!indexed) return fail(name + " is a list: say which item, " + name + "[0]");
            const int size = triggerListSize(v, vars);
            if (idx.constant && (idx.v < 0 || idx.v >= size))
                return fail("Use an item of " + name + " from 0 to " + std::to_string(size - 1));
            if (!idx.constant && wrap) idx.code = (*wrap)(idx.code, size);
            out = {code + "[" + idx.code + "]"};
            return true;
        }
        case TriggerVariable::Kind::Unit: return fail(name + " is a unit, not a number");
        case TriggerVariable::Kind::UnitGroup: return fail(name + " is a unit group, not a number");
        case TriggerVariable::Kind::Text:
            if (indexed) return fail(name + " is a text, not a list");
            out = {code + ".epd"};  // the text's place in memory (a StringBuffer; shown with :t)
            return true;
        }
        return fail("?");
    }
};
}  // namespace

int triggerListSize(const TriggerVariable& v, const std::map<std::string, const TriggerNode*>& variables) {
    if (v.kind == TriggerVariable::Kind::PerPlayer) return 8;
    if (!v.sizeConstant.empty())
        if (auto it = variables.find(v.sizeConstant); it != variables.end() && it->second->variable.kind == TriggerVariable::Kind::Constant)
            return std::max(1, std::atoi(it->second->variable.initial.c_str()));
    return std::max(1, v.size);
}

TriggerExprResult triggerExpressionChecked(const std::string& text, const std::map<std::string, const TriggerNode*>& variables, int counters,
                                           const std::function<std::string(const std::string&, int)>* wrap) {
    TriggerExprResult r;
    ExprParser p{text, variables, counters, wrap};
    ExprParser::Val v;
    if (!p.expr(v)) {
        r.error = p.error;
        return r;
    }
    p.ws();
    if (p.i != text.size()) {
        r.error = "unexpected '" + text.substr(p.i, 1) + "'";
        return r;
    }
    r.ok = true, r.code = v.code, r.constant = v.constant, r.value = v.v;
    return r;
}

TriggerExprResult triggerExpression(const std::string& text, const std::map<std::string, const TriggerNode*>& variables, int counters) {
    return triggerExpressionChecked(text, variables, counters, nullptr);
}

// --- The set and its file ------------------------------------------------------------------------------------------

TriggerSet::TriggerSet() {
    for (const char* n : {"Definitions", "On map start", "On every trigger cycle"}) {
        TriggerNode c;
        c.kind = TriggerNode::Kind::Category;
        c.name = n;
        roots.push_back(std::move(c));
    }
}

TriggerNode TriggerSet::newTrigger(const std::string& name) {
    TriggerNode t;
    t.kind = TriggerNode::Kind::Trigger;
    t.name = name;
    t.conditions.push_back(TriggerCall::make(*TriggerDef::find("cond.always")));
    return t;
}

TriggerNode TriggerSet::newFolder(const std::string& name) {
    TriggerNode f;
    f.kind = TriggerNode::Kind::Folder;
    f.name = name;
    return f;
}

TriggerNode TriggerSet::newVariable(const std::string& name, TriggerVariable::Kind kind) {
    TriggerNode v;
    v.kind = TriggerNode::Kind::Variable;
    v.name = name;
    v.variable.kind = kind;
    v.variable.initial = kind == TriggerVariable::Kind::Unit || kind == TriggerVariable::Kind::Text ? "" : "0";
    if (kind == TriggerVariable::Kind::Text) v.variable.size = TriggerVariable::kTextRoom;
    return v;
}

namespace {

const char* kindName(TriggerNode::Kind k) {
    switch (k) {
    case TriggerNode::Kind::Category: return "category";
    case TriggerNode::Kind::Folder: return "folder";
    case TriggerNode::Kind::Variable: return "variable";
    default: return "trigger";
    }
}
const char* varKindName(TriggerVariable::Kind k) {
    switch (k) {
    case TriggerVariable::Kind::Flag: return "flag";
    case TriggerVariable::Kind::PerPlayer: return "perplayer";
    case TriggerVariable::Kind::Array: return "array";
    case TriggerVariable::Kind::Unit: return "unit";
    case TriggerVariable::Kind::Constant: return "constant";
    case TriggerVariable::Kind::Text: return "text";
    case TriggerVariable::Kind::UnitGroup: return "unitgroup";
    default: return "number";
    }
}
TriggerVariable::Kind varKindOf(const std::string& s) {
    if (s == "flag") return TriggerVariable::Kind::Flag;
    if (s == "perplayer") return TriggerVariable::Kind::PerPlayer;
    if (s == "array") return TriggerVariable::Kind::Array;
    if (s == "unit") return TriggerVariable::Kind::Unit;
    if (s == "constant") return TriggerVariable::Kind::Constant;
    if (s == "text") return TriggerVariable::Kind::Text;
    if (s == "unitgroup") return TriggerVariable::Kind::UnitGroup;
    return TriggerVariable::Kind::Number;
}

// A block's values as a JSON object: by name, in the block's order; a value block as {"type": ..., "args": ...};
// then values this StarDraft does not know, as they were. "" when there are none.
std::string argsText(const TriggerCall& c) {
    const TriggerDef* d = TriggerDef::find(c.def);
    std::vector<std::pair<std::string, std::string>> args;
    for (size_t a = 0; d && a < d->params.size() && a < c.args.size(); ++a) {
        if (a < c.argBlocks.size() && !c.argBlocks[a].empty()) {
            const TriggerCall& v = c.argBlocks[a][0];
            const std::string inner = argsText(v);
            args.push_back({d->params[a].name, "{\"type\": " + quote(v.def) + (inner.empty() ? "" : ", \"args\": " + inner) + v.extra + "}"});
        } else {
            args.push_back({d->params[a].name, quote(c.args[a])});
        }
    }
    args.insert(args.end(), c.extraArgs.begin(), c.extraArgs.end());
    if (args.empty()) return "";
    std::string o = "{";
    for (size_t a = 0; a < args.size(); ++a) o += (a ? ", " : "") + quote(args[a].first) + ": " + args[a].second;
    return o + "}";
}

void writeCalls(std::ostringstream& o, const std::vector<TriggerCall>& calls, const std::string& pad) {
    o << "[";
    for (size_t i = 0; i < calls.size(); ++i) {
        const TriggerCall& c = calls[i];
        o << (i ? "," : "") << "\n" << pad << "  {\"type\": " << quote(c.def);
        if (!c.enabled) o << ", \"enabled\": false";
        if (c.folded) o << ", \"folded\": true";
        const std::string args = argsText(c);
        if (!args.empty()) o << ", \"args\": " << args;
        const TriggerDef* d = TriggerDef::find(c.def);
        std::vector<std::pair<std::string, const std::vector<TriggerCall>*>> lists;
        for (size_t l = 0; d && l < d->lists.size() && l < c.lists.size(); ++l) lists.push_back({d->lists[l].key, &c.lists[l]});
        for (const auto& [k, v] : c.extraLists) lists.push_back({k, &v});
        if (!lists.empty()) {
            o << ",\n" << pad << "   \"lists\": {";
            for (size_t l = 0; l < lists.size(); ++l) {
                o << (l ? ", " : "") << quote(lists[l].first) << ": ";
                writeCalls(o, *lists[l].second, pad + "   ");
            }
            o << "}";
        }
        o << c.extra << "}";
    }
    o << (calls.empty() ? "]" : "\n" + pad + "]");
}

const char* const kCategoryIds[3] = {"definitions", "map_start", "trigger_cycle"};

void writeNode(std::ostringstream& o, const TriggerNode& n, const std::string& pad, const char* id = nullptr) {
    o << pad << "{\"kind\": \"" << kindName(n.kind) << "\"";
    if (id) o << ", \"id\": \"" << id << "\"";
    o << ", \"name\": " << quote(n.name);
    if (!n.enabled) o << ", \"enabled\": false";
    if (!n.comment.empty()) o << ", \"comment\": " << quote(n.comment);
    if (n.kind == TriggerNode::Kind::Variable) {
        o << ", \"type\": \"" << varKindName(n.variable.kind) << "\", \"size\": " << n.variable.size;
        if (!n.variable.sizeConstant.empty()) o << ", \"sizeFrom\": " << quote(n.variable.sizeConstant);
        o << ", \"initial\": " << quote(n.variable.initial) << n.extra
          << "}";
        return;
    }
    if (n.kind == TriggerNode::Kind::Trigger) {
        o << ",\n" << pad << " \"conditions\": ";
        writeCalls(o, n.conditions, pad + " ");
        o << ",\n" << pad << " \"actions\": ";
        writeCalls(o, n.actions, pad + " ");
    } else {
        o << ", \"children\": [";
        for (size_t i = 0; i < n.children.size(); ++i) {
            o << (i ? ",\n" : "\n");
            writeNode(o, n.children[i], pad + "  ");
        }
        o << (n.children.empty() ? "]" : "\n" + pad + "]");
    }
    o << n.extra << "}";
}

// Blocks saved before the typed library (EUD Editor's names), and how their values map.
TriggerCall migrate(TriggerCall c) {
    struct Old {
        const char *from, *to;
        std::vector<int> order;  // new value i <- old value order[i] (-1: the default)
    };
    static const std::vector<Old> table = {
        {"Always", "cond.always", {}},
        {"Never", "cond.never", {}},
        {"Bring", "cond.bring", {0, 1, 2, 3, 4}},
        {"Command", "cond.command", {0, 1, 2, 3}},
        {"Deaths", "cond.deaths", {0, 1, 2, 3}},
        {"Kills", "cond.kills", {0, 1, 2, 3}},
        {"Accumulate", "cond.accumulate", {0, 1, 2, 3}},
        {"Switch", "cond.switch", {0, 1}},
        {"CreateUnit", "act.createunit", {0, 1, 2, 3}},
        {"DisplayText", "act.display", {0, -1}},
        {"SetResources", "act.setresources", {1, 2, 3, 0}},
        {"SetSwitch", "act.setswitch", {1, 0}},
        {"Victory", "act.victory", {}},
        {"Defeat", "act.defeat", {}},
        {"IfThenElse", "act.if", {}},
        {"DoNothing", "act.donothing", {}},
    };
    for (const auto& o : table)
        if (c.def == o.from) {
            TriggerCall n = TriggerCall::make(*TriggerDef::find(o.to));
            for (size_t i = 0; i < o.order.size() && i < n.args.size(); ++i)
                if (o.order[i] >= 0 && o.order[i] < int(c.args.size())) n.args[i] = c.args[size_t(o.order[i])];
            n.enabled = c.enabled, n.folded = c.folded;
            if (!c.lists.empty()) n.lists = c.lists;
            return n;
        }
    return c;
}

std::vector<TriggerCall> readCalls(const Json* a);
TriggerCall readCall(const Json& j);

// Show text, Print, Print on a line, and the middle of the screen (with or without values) became one block,
// Show text: their text, where and who, and their values put in the text.
TriggerCall migrateText(TriggerCall c) {
    static const std::map<std::string, std::string> place = {{"act.display", "chat"},    {"act.print", "chat"},   {"act.displayat", "line"},
                                                            {"act.printat", "line"},     {"act.eprint", "middle"}, {"act.eprintf", "middle"}};
    const auto it = place.find(c.def);
    const TriggerDef* d = TriggerDef::find(c.def);
    const TriggerDef* to = TriggerDef::find("act.showtext");
    if (it == place.end() || !d || !to) return c;
    auto get = [&](const char* name) {
        const int i = d->param(name);
        return i >= 0 && i < int(c.args.size()) ? c.args[size_t(i)] : std::string();
    };
    TriggerCall n = TriggerCall::make(*to);
    n.enabled = c.enabled;
    std::string where = it->second;
    if (where == "line") {
        const std::string line = get("line");
        const int k = std::all_of(line.begin(), line.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }) && !line.empty()
                          ? std::clamp(std::atoi(line.c_str()), 0, 10) : 0;
        where = "line" + std::to_string(k);
    }
    std::string text = get("text");
    const bool format = d->param("a") >= 0;  // its {} take the values a, b; the others' text is words
    std::vector<TriggerTextPart> parts;
    if (!format) {
        parts.push_back({TriggerTextPart::Kind::Words, text, -1});
    } else {
        int used = 0;
        std::string words;
        for (size_t i = 0; i < text.size(); ++i) {
            if ((text[i] == '{' || text[i] == '}') && i + 1 < text.size() && text[i + 1] == text[i]) {
                words += text[i], ++i;
                continue;
            }
            const size_t end = text[i] == '{' ? text.find('}', i) : std::string::npos;
            if (end == std::string::npos || used >= 2) {
                words += text[i];
                continue;
            }
            if (!words.empty()) parts.push_back({TriggerTextPart::Kind::Words, words, -1}), words.clear();
            const int pi = d->param(used == 0 ? "a" : "b");
            ++used;
            const std::string spec = text.substr(i + 1, end - i - 1);  // "" or ":c" (the place's format code)
            const std::string format = spec.size() > 1 && spec[0] == ':' ? spec.substr(1) : "";
            if (pi < int(c.argBlocks.size()) && !c.argBlocks[size_t(pi)].empty()) {
                n.textValues(true)->push_back(c.argBlocks[size_t(pi)][0]);
                parts.push_back({TriggerTextPart::Kind::GameValue, "", int(n.textValues()->size()) - 1, format});
            } else {
                const std::string v = pi < int(c.args.size()) ? c.args[size_t(pi)] : "0";
                parts.push_back({TriggerTextPart::Kind::Expression, v.empty() ? "0" : v, -1, format});
            }
            i = end;
        }
        if (!words.empty()) parts.push_back({TriggerTextPart::Kind::Words, words, -1});
    }
    n.args[size_t(to->param("text"))] = triggerTextJoin(parts);
    n.args[size_t(to->param("where"))] = where;
    n.args[size_t(to->param("to"))] = get("to").empty() ? "all" : get("to");
    return n;
}

std::vector<TriggerCall> readCalls(const Json* a) {
    std::vector<TriggerCall> out;
    if (!a || a->type != Json::Type::Array) return out;
    for (const Json& j : a->items) out.push_back(readCall(j));
    return out;
}

TriggerCall readCall(const Json& j) {
    {
        TriggerCall c;
        c.def = j.str("type");
        c.enabled = j.boolean("enabled", true);
        c.folded = j.boolean("folded", false);
        auto text = [](const Json& v) { return v.type == Json::Type::String ? v.s : jsonText(v); };
        const Json* args = j.get("args");
        const Json* lists = j.get("lists");
        if (args && args->type == Json::Type::Array)  // version 2: in the block's order
            for (const Json& v : args->items) c.args.push_back(text(v));
        if (lists && lists->type == Json::Type::Array)
            for (const Json& l : lists->items) c.lists.push_back(readCalls(&l));
        else if (j.get("if"))  // an If saved before lists
            c.lists = {readCalls(j.get("if")), readCalls(j.get("then")), readCalls(j.get("else"))};
        c = migrate(std::move(c));
        const TriggerDef* d = TriggerDef::find(c.def);
        if (d) {
            if (!args || args->type == Json::Type::Array) c.args.resize(d->params.size());
            if (!lists || lists->type == Json::Type::Array) c.lists.resize(d->lists.size());
        }
        if (args && args->type == Json::Type::Object) {  // version 3: by name; a value missing takes the default
            if (d)
                for (const auto& p : d->params) c.args.push_back(p.def);
            c.argBlocks.assign(c.args.size(), {});
            for (const auto& [k, v] : args->fields) {
                const int pi = d ? d->param(k) : -1;
                if (pi >= 0 && v.type == Json::Type::Object && v.get("type")) c.args[size_t(pi)].clear(), c.argBlocks[size_t(pi)] = {readCall(v)};
                else if (pi >= 0) c.args[size_t(pi)] = text(v);
                else c.extraArgs.push_back({k, jsonText(v)});
            }
        }
        if (lists && lists->type == Json::Type::Object) {
            if (d) c.lists.assign(d->lists.size(), {});
            for (const auto& [k, v] : lists->fields) {
                int li = -1;
                for (size_t l = 0; d && l < d->lists.size(); ++l)
                    if (d->lists[l].key == k) li = int(l);
                if (li >= 0) c.lists[size_t(li)] = readCalls(&v);
                else c.extraLists.push_back({k, readCalls(&v)});
            }
        }
        c.extra = extraFields(j, {"type", "enabled", "folded", "args", "lists", "if", "then", "else"});
        c.argBlocks.resize(c.args.size());
        return migrateText(std::move(c));
    }
}

TriggerNode readNode(const Json& j) {
    TriggerNode n;
    const std::string k = j.str("kind", "trigger");
    n.kind = k == "category" ? TriggerNode::Kind::Category
           : k == "folder"   ? TriggerNode::Kind::Folder
           : k == "variable" ? TriggerNode::Kind::Variable
                             : TriggerNode::Kind::Trigger;
    n.name = j.str("name");
    n.enabled = j.boolean("enabled", true);
    n.comment = j.str("comment");
    if (n.kind == TriggerNode::Kind::Variable) {
        n.variable.kind = varKindOf(j.str("type", "number"));
        n.variable.size = std::max(1, j.integer("size", 8));
        n.variable.initial = j.str("initial");
        n.variable.sizeConstant = j.str("sizeFrom");
    }
    n.conditions = readCalls(j.get("conditions"));
    n.actions = readCalls(j.get("actions"));
    if (const Json* c = j.get("children"))
        for (const Json& child : c->items) n.children.push_back(readNode(child));
    n.extra = extraFields(j, {"kind", "id", "name", "enabled", "comment", "type", "size", "sizeFrom", "initial", "conditions", "actions", "children"});
    return n;
}

}  // namespace

bool TriggerSet::load(const fs::path& file, std::string& error) {
    *this = TriggerSet();
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    JsonParser p{text};
    Json root = p.value();
    if (!p.ok || root.type != Json::Type::Object) {
        error = file.string() + ": not valid trigger data";
        return false;
    }
    fileVersion = root.integer("version", 1);
    if (const Json* cats = root.get("categories"))
        for (const Json& c : cats->items) {
            TriggerNode n = readNode(c);
            // By id (version 3), else by name (and the third's first name).
            const std::string id = c.str("id");
            int at = -1;
            for (int i = 0; i < 3; ++i)
                if (id == kCategoryIds[i] || (id.empty() && (n.name == roots[size_t(i)].name || (i == 2 && n.name == "On every frame")))) at = i;
            if (at >= 0) roots[size_t(at)].children = std::move(n.children), roots[size_t(at)].extra = n.extra;
        }
    return true;
}

std::string TriggerSet::text() const {
    std::ostringstream o;
    o << "{\"format\": \"stardraft-triggers\", \"version\": " << kFormatVersion << ",\n\"categories\": [\n";
    for (size_t i = 0; i < roots.size(); ++i) {
        if (i) o << ",\n";
        writeNode(o, roots[i], "", i < 3 ? kCategoryIds[i] : nullptr);
    }
    o << "\n]}\n";
    return o.str();
}

bool TriggerSet::save(const fs::path& file, std::string& error) const {
    if (newerFormat()) {
        error = file.string() + " was made by a newer StarDraft (format " + std::to_string(fileVersion) + "): not saved, so nothing in it is lost";
        return false;
    }
    std::ofstream out(file, std::ios::binary);
    out << text();
    if (!out) {
        error = "could not write " + file.string();
        return false;
    }
    return true;
}

TriggerNode* TriggerSet::at(const std::vector<int>& path) {
    if (path.empty() || path[0] < 0 || path[0] >= int(roots.size())) return nullptr;
    TriggerNode* n = &roots[size_t(path[0])];
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] < 0 || path[i] >= int(n->children.size())) return nullptr;
        n = &n->children[size_t(path[i])];
    }
    return n;
}

const TriggerNode* TriggerSet::at(const std::vector<int>& path) const { return const_cast<TriggerSet*>(this)->at(path); }

std::vector<int> TriggerSet::move(const std::vector<int>& from, const std::vector<int>& parent, int index) {
    if (from.size() < 2) return {};  // categories stay where they are
    if (parent.size() >= from.size() && std::equal(from.begin(), from.end(), parent.begin())) return {};  // into itself
    TriggerNode* dest = at(parent);
    if (!dest || dest->kind == TriggerNode::Kind::Trigger || dest->kind == TriggerNode::Kind::Variable || !at(from)) return {};
    std::vector<int> fromParent(from.begin(), from.end() - 1);
    TriggerNode node = std::move(*at(from));
    at(fromParent)->children.erase(at(fromParent)->children.begin() + from.back());
    std::vector<int> p = parent;
    if (p.size() > fromParent.size() && std::equal(fromParent.begin(), fromParent.end(), p.begin()) && p[fromParent.size()] > from.back())
        --p[fromParent.size()];
    if (p == fromParent && index > from.back()) --index;
    dest = at(p);
    if (index < 0 || index > int(dest->children.size())) index = int(dest->children.size());
    dest->children.insert(dest->children.begin() + index, std::move(node));
    p.push_back(index);
    return p;
}

void TriggerSet::remove(const std::vector<int>& path) {
    if (path.size() < 2) return;
    std::vector<int> parent(path.begin(), path.end() - 1);
    if (TriggerNode* p = at(parent); p && path.back() < int(p->children.size())) p->children.erase(p->children.begin() + path.back());
}

std::map<std::string, const TriggerNode*> TriggerSet::variables() const {
    std::map<std::string, const TriggerNode*> out;
    std::function<void(const TriggerNode&)> walk = [&](const TriggerNode& n) {
        if (n.kind == TriggerNode::Kind::Variable) out[n.name] = &n;
        for (const auto& c : n.children) walk(c);
    };
    for (const auto& r : roots) walk(r);
    return out;
}

namespace {
// Replaces whole identifiers `from` with `to` in a value.
int replaceIdent(std::string& s, const std::string& from, const std::string& to) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        if (!identStart(s[i]) || (i > 0 && identChar(s[i - 1]))) {
            ++i;
            continue;
        }
        size_t end = i;
        while (end < s.size() && identChar(s[end])) ++end;
        if (end - i == from.size() && s.compare(i, end - i, from) == 0) {
            s.replace(i, end - i, to);
            i += to.size();
            ++n;
        } else {
            i = end;
        }
    }
    return n;
}
bool variableValue(const std::string& type) {
    const TriggerType* t = triggerType(type);
    return type.rfind("ValueOf:", 0) == 0 || triggerPlayerType(type) ||
           (t && (t->picker == TriggerPicker::Number || t->picker == TriggerPicker::Variable || t->picker == TriggerPicker::UnitRef ||
                  t->picker == TriggerPicker::DatEntry));
}
}  // namespace

int TriggerSet::upgradeNames(const TriggerMapInfo& map) {
    if (!map.known) return 0;
    int n = 0;
    static const std::map<std::string, std::string> groups = {{"Any unit", "229"}, {"Men", "230"}, {"Buildings", "231"}, {"Factories", "232"}};
    std::function<void(std::vector<TriggerCall>&)> calls = [&](std::vector<TriggerCall>& list) {
        for (auto& c : list) {
            if (const TriggerDef* d = TriggerDef::find(c.def))
                for (size_t i = 0; i < d->params.size() && i < c.args.size(); ++i) {
                    const TriggerType* t = triggerType(d->params[i].type);
                    std::string& v = c.args[i];
                    if (!t || v.empty() || std::isdigit(static_cast<unsigned char>(v[0]))) continue;
                    if (t->picker == TriggerPicker::UnitType || t->picker == TriggerPicker::UnitFilter) {
                        if (auto g = groups.find(v); g != groups.end()) v = g->second, ++n;
                        else
                            for (size_t u = 0; u < map.unitNames.size(); ++u)
                                if (map.unitNames[u] == v) {
                                    v = std::to_string(u), ++n;
                                    break;
                                }
                    } else if (t->picker == TriggerPicker::Location) {
                        for (size_t l = 0; l < map.locations.size(); ++l)
                            if (!map.locations[l].empty() && map.locations[l] == v) {
                                v = std::to_string(l + 1), ++n;
                                break;
                            }
                    }
                }
            for (auto& l : c.lists) calls(l);
            for (auto& b : c.argBlocks) calls(b);
        }
    };
    std::function<void(TriggerNode&)> walk = [&](TriggerNode& node) {
        calls(node.conditions), calls(node.actions);
        for (auto& c : node.children) walk(c);
    };
    for (auto& r : roots) walk(r);
    return n;
}

int TriggerSet::renameVariable(const std::string& fromRef, const std::string& toRef) {
    // Copies: the caller may pass the variable's own name, which the rename changes on the way.
    const std::string from = fromRef, to = toRef;
    // In epScript (the epScript blocks) a variable is v_<name>, and a unit variable's generation v_<name>_gen.
    const std::string fromCode = triggerVariableCode(from), toCode = triggerVariableCode(to);
    int n = 0;
    std::function<void(std::vector<TriggerCall>&)> calls = [&](std::vector<TriggerCall>& list) {
        for (auto& c : list) {
            if (const TriggerDef* d = TriggerDef::find(c.def))
                for (size_t i = 0; i < d->params.size() && i < c.args.size(); ++i) {
                    if (variableValue(d->params[i].type)) n += replaceIdent(c.args[i], from, to);
                    else if (const TriggerType* t = triggerType(d->params[i].type); t && t->picker == TriggerPicker::Code)
                        n += replaceIdent(c.args[i], fromCode, toCode) + replaceIdent(c.args[i], fromCode + "_gen", toCode + "_gen");
                }
            for (auto& l : c.lists) calls(l);
            for (auto& b : c.argBlocks) calls(b);
        }
    };
    std::function<void(TriggerNode&)> walk = [&](TriggerNode& node) {
        if (node.kind == TriggerNode::Kind::Variable && node.name == from) node.name = to;
        if (node.kind == TriggerNode::Kind::Variable && node.variable.sizeConstant == from) node.variable.sizeConstant = to, ++n;
        if (node.kind == TriggerNode::Kind::Variable) n += replaceIdent(node.variable.initial, from, to);  // a default value made from a constant
        calls(node.conditions), calls(node.actions);
        for (auto& c : node.children) walk(c);
    };
    for (auto& r : roots) walk(r);
    return n;
}

int TriggerSet::renameTrigger(const std::string& fromRef, const std::string& toRef) {
    const std::string from = fromRef, to = toRef;
    int n = 0;
    std::function<void(std::vector<TriggerCall>&)> calls = [&](std::vector<TriggerCall>& list) {
        for (auto& c : list) {
            if (const TriggerDef* d = TriggerDef::find(c.def))
                for (size_t i = 0; i < d->params.size() && i < c.args.size(); ++i)
                    if (const TriggerType* t = triggerType(d->params[i].type); t && t->picker == TriggerPicker::Trigger && c.args[i] == from) c.args[i] = to, ++n;
            for (auto& l : c.lists) calls(l);
            for (auto& b : c.argBlocks) calls(b);
        }
    };
    std::function<void(TriggerNode&)> walk = [&](TriggerNode& node) {
        calls(node.conditions), calls(node.actions);
        for (auto& c : node.children) walk(c);
    };
    for (auto& r : roots) walk(r);
    if (!roots.empty()) {
        std::function<void(TriggerNode&)> rename = [&](TriggerNode& node) {
            if (node.kind == TriggerNode::Kind::Trigger && node.name == from) node.name = to;
            for (auto& c : node.children) rename(c);
        };
        rename(roots[0]);
    }
    return n;
}

// --- Checking and generating --------------------------------------------------------------------------------------

namespace {

std::string indentLines(const std::string& code, const std::string& pad) {
    std::string out;
    std::istringstream in(code);
    for (std::string line; std::getline(in, line);) out += pad + line + "\n";
    return out;
}

std::string pickedLookup(const TriggerMapInfo::PlacedUnit& u) {
    return "        if (cu.unitType == " + std::to_string(u.type) + " && cu.owner == " + std::to_string(u.owner) + " && cu.posX == " + std::to_string(u.x) +
           " && cu.posY == " + std::to_string(u.y) + ") {\n            pu_" + std::to_string(u.serial) + " = cu;\n            pu_" + std::to_string(u.serial) +
           "_gen = cu.uniquenessIdentifier;\n        }\n";
}

struct Gen {
    const TriggerMapInfo& map;
    std::vector<TriggerIssue>& issues;
    std::map<std::string, const TriggerNode*> vars;
    const TriggerNode* trigger = nullptr;
    std::string globals;  // timers, declared at the top
    std::set<std::string> helpers;  // helper functions the code uses (sd_upgradeAddr, ...)
    // Triggers in Definitions, by name, and every trigger's function. Run a trigger records what runs what.
    std::map<std::string, const TriggerNode*> definitions;
    std::map<const TriggerNode*, std::string> fnOf;
    std::map<const TriggerNode*, std::set<const TriggerNode*>> runs;
    // The address of a player's upgrade level, technology, a location's corners (BWAPI's tables; two blocks each for
    // the original and Brood War entries).
    std::string upgradeAddr(const std::string& pl, const std::string& u) { return helpers.insert("upgrade"), "sd_upgradeAddr(" + pl + ", " + u + ")"; }
    std::string techAddr(const std::string& pl, const std::string& t) { return helpers.insert("tech"), "sd_techAddr(" + pl + ", " + t + ")"; }
    std::string locAddr(const std::string& loc) { return helpers.insert("loc"), "sd_locAddr(" + loc + ")"; }
    int timers = 0;
    int counters = 0;     // Repeat depth
    int unitLoops = 0;    // For each unit depth
    int playerLoops = 0;  // For each player depth
    int loops = 0;        // any loop's depth (Leave the loop)
    int sequenceDepth = 0;  // inside a Sequence (its Pauses belong to it)
    bool atMapStart = false;  // the trigger is in On map start (it runs one time: a timer there never goes on)
    int localDepth = 0;   // inside Run local block, or an action only this computer sees (text, sound)
    std::set<uint32_t> pickedUnits;
    std::map<std::string, int> chats;  // chat commands (euddraft's chatEvent plugin): the text, and the number it sets
    int shares = 0;                    // shared values (MSQC sends them): msqc_share<n>_...
    std::string perCycle;              // code for afterTriggerExec, every cycle
    bool debug = false;                // a debug build: indexes worked out in the game are checked
    std::vector<std::string> sites;    // debug build: where each checked index is ("trigger / block")
    std::string curTitle;              // the title of the block whose code is being made
    std::vector<TriggerCodeLine> marks;  // the blocks and nodes the code's marks name
    std::vector<bool> unitLoopOnePlayer;  // For each unit loops around the code: true for one player's units
    std::set<int> realUnitTypes;        // unit types the map has or the triggers make (deaths count for them)

    // Marks in the code, taken out at the end (generate): \x01<n>\x02 ... \x03 around a block's code, \x04<n>\x02 before
    // a trigger's or variable's lines. They give each line of the code its block.
    std::string mark(const TriggerCall& c, const std::string& code) {
        if (code.empty()) return code;
        const std::string open = "\x01" + std::to_string(marks.size()) + "\x02";
        marks.push_back({trigger, &c});
        if (code.back() == '\n') return open + code.substr(0, code.size() - 1) + "\x03\n";
        return open + code + "\x03";
    }
    std::string nodeMark(const TriggerNode& n) {
        marks.push_back({&n, nullptr});
        return "\x04" + std::to_string(marks.size() - 1) + "\x02";
    }

    // Debug build: an index worked out in the game, checked against the list's size.
    std::string checkIndex(const std::string& code, int size) {
        if (!debug || code == "getcurpl()") return code;
        helpers.insert("index");
        sites.push_back((trigger ? trigger->name : std::string("?")) + " / " + curTitle);
        return "sd_idx(" + code + ", " + std::to_string(size) + ", " + std::to_string(sites.size() - 1) + ")";
    }
    std::function<std::string(const std::string&, int)> indexCheck = [this](const std::string& code, int size) { return checkIndex(code, size); };
    TriggerExprResult gx(const std::string& text) { return triggerExpressionChecked(text, vars, counters, debug ? &indexCheck : nullptr); }

    void issue(TriggerIssue::Level l, const TriggerCall* c, int param, const std::string& m) { issues.push_back({l, trigger, c, param, m}); }

    int locationIndex(const std::string& name) const {
        for (size_t i = 0; i < map.locations.size(); ++i)
            if (!name.empty() && map.locations[i] == name) return int(i);
        return -1;
    }
    static bool isNumber(const std::string& v) {
        return !v.empty() && std::all_of(v.begin(), v.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
    }
    int unitId(const std::string& name) const {
        for (size_t i = 0; i < map.unitNames.size(); ++i)
            if (map.unitNames[i] == name) return int(i);
        return -1;
    }

    // A player as a number: P1 -> 0, the current player -> getcurpl().
    std::string playerNumber(const TriggerCall& c, int i, const std::string& v) {
        if (v == "getcurpl()" || v == "CurrentPlayer") {
            if (!playerLoops) issue(TriggerIssue::Level::Warning, &c, i, "\"the current player\" has a value only in For each player");
            return "getcurpl()";
        }
        if (v.size() >= 2 && v[0] == 'P' && std::isdigit(static_cast<unsigned char>(v[1]))) return std::to_string(std::atoi(v.c_str() + 1) - 1);
        issue(TriggerIssue::Level::Error, &c, i, "Select a player");
        return "0";
    }

    // A value block: a number read from the game.
    std::string valueBlock(const TriggerCall& c) {
        const TriggerDef* d = TriggerDef::find(c.def);
        if (!d || !d->value) {
            issue(TriggerIssue::Level::Error, &c, -1, "Unknown value " + c.def);
            return "0";
        }
        blockChecks(c, *d);
        const std::string& id = d->id;
        if (id == "val.dirx" || id == "val.diry") {  // eudplib's lengthdir_256: 0 is up, as the game's directions
            helpers.insert("ldir");
            return std::string(id == "val.dirx" ? "sd_ldirX(" : "sd_ldirY(") + arg(c, *d, "d") + ", " + arg(c, *d, "a") + ")";
        }
        if (id == "val.groupsize") {
            const std::string g = raw(c, *d, "group");
            auto it = vars.find(g);
            if (it == vars.end() || it->second->variable.kind != TriggerVariable::Kind::UnitGroup) {
                issue(TriggerIssue::Level::Error, &c, d->param("group"), g.empty() ? "Select a unit group" : g + " is not a unit group");
                return "0";
            }
            return triggerVariableCode(g) + "[0]";
        }
        if (d->code.rfind("@prop ", 0) == 0) {
            const std::string structure = d->code.substr(6);
            const TriggerProperty* prop = triggerCallProperty(*d, c.args);
            if (!prop) {
                issue(TriggerIssue::Level::Error, &c, d->param("prop"), "Select a property");
                return "0";
            }
            const std::string ref = structure == "CUnit" ? unitRef(c, d->param("unit"), raw(c, *d, "unit")) : structure + "(" + value(c, *d, 0) + ")";
            const std::string member = ref + "." + prop->code;
            return prop->scale > 1 ? "(" + member + " / " + std::to_string(prop->scale) + ")" : member;
        }
        if (id == "val.resources") {
            const std::string pl = playerNumber(c, d->param("player"), raw(c, *d, "player")), res = raw(c, *d, "res");
            const std::string ore = "TrgPlayer(" + pl + ").mineral", gas = "TrgPlayer(" + pl + ").gas";  // eudplib's player structure
            return res == "Gas" ? gas : res == "OreAndGas" ? "(" + ore + " + " + gas + ")" : ore;
        }
        if (id == "val.deaths")
            return "dwread_epd(EPD(0x58A364) + 12 * " + value(c, *d, d->param("unit")) + " + " + playerNumber(c, d->param("player"), raw(c, *d, "player")) + ")";
        if (id == "val.listitem") return listItem(c, *d);
        if (id == "val.max" || id == "val.min") {
            helpers.insert(id == "val.max" ? "max" : "min");
            return std::string(id == "val.max" ? "sd_max(" : "sd_min(") + arg(c, *d, "a") + ", " + arg(c, *d, "b") + ")";
        }
        if (id == "val.upgrade") return "bread(" + upgradeAddr(playerNumber(c, d->param("player"), raw(c, *d, "player")), value(c, *d, d->param("upgrade"))) + ")";
        if (id == "val.unitcount" || id == "val.kills")
            return std::string("dwread_epd(EPD(") + (id == "val.kills" ? "0x5878A4" : "0x582324") + ") + 12 * " + value(c, *d, d->param("unit")) + " + " +
                   playerNumber(c, d->param("player"), raw(c, *d, "player")) + ")";
        if (id == "val.locx" || id == "val.locy") {
            const std::string a = locAddr(value(c, *d, d->param("where")));
            return id == "val.locx" ? "((dwread(" + a + ") + dwread(" + a + " + 8)) / 2)" : "((dwread(" + a + " + 4) + dwread(" + a + " + 12)) / 2)";
        }
        if (id == "val.distance") {
            const std::string a = value(c, *d, d->param("a")), b = value(c, *d, d->param("b"));
            const std::string dx = "(" + a + ".posX - " + b + ".posX)", dy = "(" + a + ".posY - " + b + ".posY)";
            return "sqrt(" + dx + " * " + dx + " + " + dy + " * " + dy + ")";
        }
        if (d->code.empty()) {
            issue(TriggerIssue::Level::Error, &c, -1, d->title + ": StarDraft cannot build this block yet");
            return "0";
        }
        return fill(c, *d, d->code);
    }

    // One value as epScript, checked.
    // A unit type in the code: eudplib's name for it ($U("Zerg Zergling")), which reads better than the number and is
    // the same number in any expression. The number when eudplib has no name for it, or when the map gives the name to
    // another unit type (then the name could mean that one).
    std::string unitCode(int n) const {
        if (n < 0 || n >= int(std::size(kEudUnitNames)) || !kEudUnitNames[n]) return std::to_string(n);
        const std::string name = kEudUnitNames[n];
        if (name == "None") return std::to_string(n);
        for (size_t u = 0; u < map.unitNames.size(); ++u)
            if (int(u) != n && map.unitNames[u] == name) return std::to_string(n);
        return "$U(\"" + name + "\")";
    }

    // An entry of a .dat in the code: eudplib's name for it (EncodeWeapon("Gauss Rifle")), the same number in any
    // expression; the number when eudplib has no name for it.
    static std::string datCode(const std::string& dat, int n) {
        struct Names {
            const char* dat;
            const char* const* names;
            size_t count;
            const char* encode;
        };
        static const Names all[] = {{"units", kEudUnitNames, std::size(kEudUnitNames), "$U"},
                                    {"weapons", kEudWeaponNames, std::size(kEudWeaponNames), "EncodeWeapon"},
                                    {"flingy", kEudFlingyNames, std::size(kEudFlingyNames), "EncodeFlingy"},
                                    {"sprites", kEudSpriteNames, std::size(kEudSpriteNames), "EncodeSprite"},
                                    {"images", kEudImageNames, std::size(kEudImageNames), "EncodeImage"},
                                    {"upgrades", kEudUpgradeNames, std::size(kEudUpgradeNames), "EncodeUpgrade"},
                                    {"techdata", kEudTechNames, std::size(kEudTechNames), "EncodeTech"},
                                    {"orders", kEudOrderNames, std::size(kEudOrderNames), "EncodeUnitOrder"},
                                    {"portdata", kEudPortraitNames, std::size(kEudPortraitNames), "EncodePortrait"}};
        for (const Names& t : all)
            if (dat == t.dat && n >= 0 && size_t(n) < t.count && t.names[n]) {
                std::string q;
                for (const char* ch = t.names[n]; *ch; ++ch) q += *ch == '"' || *ch == '\\' ? std::string("\\") + *ch : std::string(1, *ch);
                return std::string(t.encode) + "(\"" + q + "\")";
            }
        return std::to_string(n);
    }

    std::string value(const TriggerCall& c, const TriggerDef& d, int i) {
        if (i < 0 || i >= int(d.params.size())) return "";
        if (i < int(c.argBlocks.size()) && !c.argBlocks[size_t(i)].empty()) {  // a value block
            const TriggerType* t = triggerType(triggerParamType(d, c.args, i));
            if (!t || (t->picker != TriggerPicker::Number && t->picker != TriggerPicker::DatEntry && t->picker != TriggerPicker::Unclear &&
                       !triggerPlayerType(t->name)))
                issue(TriggerIssue::Level::Error, &c, i, "A value from the game is a number. You cannot use it here");
            return valueBlock(c.argBlocks[size_t(i)][0]);
        }
        TriggerParam p = d.params[size_t(i)];
        const std::string v = i < int(c.args.size()) ? c.args[size_t(i)] : p.def;
        if (p.type.rfind("ValueOf:", 0) == 0) {  // the property's type and range
            const TriggerProperty* prop = triggerCallProperty(d, c.args);
            p.type = triggerParamType(d, c.args, i);
            if (prop) p.min = prop->min, p.max = prop->max;
        }
        const TriggerType* t = triggerType(p.type);
        if (!t || t->picker == TriggerPicker::Unclear) {
            issue(TriggerIssue::Level::Info, &c, i, "\"" + p.name + "\" has no clear type yet (" + p.type + "). Type it as a number");
            if (p.type == "UnitProperties") return "UnitProperty()";
            const TriggerExprResult r = gx(v.empty() ? "0" : v);
            return r.ok ? r.code : "0";
        }
        switch (t->picker) {
        case TriggerPicker::Choice:
            for (const auto& ch : t->choices)
                if (ch.code == v) return p.type == "AIScript" ? quote(v) : v;
            if (triggerPlayerType(t->name)) {  // a player as a number: a variable or an expression (Player 1 is 0)
                const TriggerExprResult r = gx(v.empty() ? "0" : v);
                if (r.ok) {
                    if (r.constant && (r.value < 0 || r.value > 26)) issue(TriggerIssue::Level::Error, &c, i, "Use a player number from 0 to 26 (Player 1 is 0)");
                    return r.code;
                }
                issue(TriggerIssue::Level::Error, &c, i, "\"" + v + "\" is not a player, a variable or a number");
                return "0";
            }
            issue(TriggerIssue::Level::Error, &c, i, "\"" + v + "\" is not a " + t->label);
            return v;
        case TriggerPicker::Number: {
            const TriggerExprResult r = gx(v.empty() ? "0" : v);
            if (!r.ok) {
                issue(TriggerIssue::Level::Error, &c, i, r.error);
                return "0";
            }
            if (r.constant && p.max >= p.min && (r.value < p.min || r.value > p.max))
                issue(TriggerIssue::Level::Error, &c, i, "Use a value from " + std::to_string(p.min) + " to " + std::to_string(p.max));
            return r.code;
        }
        case TriggerPicker::Sound:
            if (v.empty()) issue(TriggerIssue::Level::Error, &c, i, "Select a sound");
            else if (map.known && std::find(map.sounds.begin(), map.sounds.end(), v) == map.sounds.end())
                issue(TriggerIssue::Level::Warning, &c, i, "The map has no sound " + v);
            return quote(v);
        case TriggerPicker::Trigger: {
            const auto it = definitions.find(v);
            if (v.empty()) issue(TriggerIssue::Level::Error, &c, i, "Select a trigger");
            else if (it == definitions.end()) issue(TriggerIssue::Level::Error, &c, i, "There is no trigger in Definitions with the name " + v);
            else if (!it->second->enabled) issue(TriggerIssue::Level::Error, &c, i, v + " is off. Turn it on, or select another trigger");
            else {
                if (trigger) runs[trigger].insert(it->second);
                return fnOf[it->second];
            }
            return "";
        }
        case TriggerPicker::Text: return quote(v);
        case TriggerPicker::Code: return v;
        case TriggerPicker::Button: {  // eight numbers, as the button's fields are in memory
            std::vector<std::string> f;
            std::stringstream ss(v);
            for (std::string x; std::getline(ss, x, ',');) f.push_back(x.empty() ? "0" : x);
            if (f.size() != 8 || !std::all_of(f.begin(), f.end(), [](const std::string& x) { return isNumber(x); })) {
                issue(TriggerIssue::Level::Error, &c, i, "Set up the button");
                return "1, 0, 0, 0, 0, 0, 0, 0";
            }
            std::string out;
            for (size_t k = 0; k < f.size(); ++k) out += (k ? ", " : "") + f[k];
            return out;
        }
        case TriggerPicker::UnitType:
        case TriggerPicker::UnitFilter: {
            static const std::map<std::string, int> filters = {{"Any unit", 229}, {"Men", 230}, {"Buildings", 231}, {"Factories", 232}};
            int group = -1;
            if (auto f = filters.find(v); f != filters.end()) group = f->second;
            if (isNumber(v) && std::atoi(v.c_str()) >= 229 && std::atoi(v.c_str()) <= 232) group = std::atoi(v.c_str());
            if (group >= 0) {
                if (t->picker == TriggerPicker::UnitType) issue(TriggerIssue::Level::Error, &c, i, "This is a group of units. Select one unit type");
                return unitCode(group);
            }
            if (isNumber(v)) {
                const int n = std::atoi(v.c_str());
                if (map.known && (n < 0 || n >= int(map.unitNames.size()))) issue(TriggerIssue::Level::Error, &c, i, "There is no unit type " + v);
                return unitCode(n);
            }
            const int id = unitId(v);  // a name: files from before version 3
            if (id < 0) {
                if (map.known) issue(TriggerIssue::Level::Error, &c, i, "There is no unit type with the name " + v);
                return quote(v);
            }
            return unitCode(id);
        }
        case TriggerPicker::Location: {
            if (isNumber(v)) {
                const int n = std::atoi(v.c_str());
                if (n < 1 || n > 255) issue(TriggerIssue::Level::Error, &c, i, "Use a location from 1 to 255");
                else if (map.known && (size_t(n) > map.locations.size() || map.locations[size_t(n - 1)].empty()))
                    issue(TriggerIssue::Level::Error, &c, i, "The map has no location " + v);
                return std::to_string(n);
            }
            const int li = locationIndex(v);  // a name: files from before version 3
            if (li < 0) {
                if (map.known) issue(TriggerIssue::Level::Error, &c, i, v.empty() ? "Select a location" : "The map has no location " + v);
                return quote(v);
            }
            return std::to_string(li + 1);
        }
        case TriggerPicker::Switch: {
            const int n = std::atoi(v.c_str());
            if (n < 1 || n > 256) issue(TriggerIssue::Level::Error, &c, i, "Use a switch from 1 to 256");
            return std::to_string(std::clamp(n, 1, 256) - 1);
        }
        case TriggerPicker::UnitRef: return unitRef(c, i, v);
        case TriggerPicker::Variable: return target(c, i, p.type, v);
        case TriggerPicker::DatEntry: {
            const TriggerExprResult r = gx(v.empty() ? "0" : v);
            if (!r.ok) {
                issue(TriggerIssue::Level::Error, &c, i, r.error);
                return "0";
            }
            if (r.constant && t->count > 0 && (r.value < 0 || r.value >= t->count))
                issue(TriggerIssue::Level::Error, &c, i, "Use a value from 0 to " + std::to_string(t->count - 1));
            if (r.constant && isNumber(v)) return datCode(t->of, int(r.value));  // a number as it is: the entry's name
            return r.code;
        }
        case TriggerPicker::Property:
            if (!triggerProperty(t->of, v)) issue(TriggerIssue::Level::Error, &c, i, "A " + triggerStructureLabel(t->of) + " has no property " + v);
            return v;
        case TriggerPicker::Unclear: break;
        }
        return v;
    }

    // A picked unit is found by type, owner and position: a second unit with all three the same cannot be told apart.
    void twinCheck(uint32_t serial, const TriggerCall* c, int i) {
        const auto it = std::find_if(map.units.begin(), map.units.end(), [&](const auto& u) { return u.serial == serial; });
        if (it == map.units.end()) return;
        for (const auto& u : map.units)
            if (u.serial != serial && u.type == it->type && u.owner == it->owner && u.x == it->x && u.y == it->y) {
                issue(TriggerIssue::Level::Warning, c, i,
                      "Unit #" + std::to_string(u.serial) + " has the same type, owner and position. Move one of them, or StarDraft can find the wrong unit");
                return;
            }
    }

    // The generation that goes with a unit value, when it has one: a variable's, or a picked unit's.
    std::string unitGen(const std::string& v) {
        if (!v.empty() && v[0] == '@') return "pu_" + std::to_string(std::strtoul(v.c_str() + 1, nullptr, 10)) + "_gen";
        auto it = vars.find(v);
        if (it != vars.end() && it->second->variable.kind == TriggerVariable::Kind::Unit) return triggerVariableCode(v) + "_gen";
        return "";
    }

    std::string unitRef(const TriggerCall& c, int i, const std::string& v) {
        if (v == "the next created unit") return "CUnit.from_next()";  // the slot the next new unit takes
        if (v == "the unit") {
            if (!unitLoops) issue(TriggerIssue::Level::Error, &c, i, "\"the unit\" has a value only in For each unit");
            return "cu" + std::to_string(std::max(1, unitLoops));
        }
        if (!v.empty() && v[0] == '@') {
            const uint32_t serial = uint32_t(std::strtoul(v.c_str() + 1, nullptr, 10));
            if (map.known && std::none_of(map.units.begin(), map.units.end(), [&](const auto& u) { return u.serial == serial; }))
                issue(TriggerIssue::Level::Error, &c, i, "The selected unit is not on the map now");
            pickedUnits.insert(serial);
            twinCheck(serial, &c, i);
            return "pu_" + std::to_string(serial);
        }
        auto it = vars.find(v);
        if (it == vars.end() || it->second->variable.kind != TriggerVariable::Kind::Unit) {
            issue(TriggerIssue::Level::Error, &c, i, v.empty() ? "Select a unit" : v + " is not a unit variable");
            return "CUnit(0)";
        }
        if (const std::string& start = it->second->variable.initial; map.known && start.size() > 1 && start[0] == '@') {
            const uint32_t serial = uint32_t(std::strtoul(start.c_str() + 1, nullptr, 10));
            if (std::none_of(map.units.begin(), map.units.end(), [&](const auto& u) { return u.serial == serial; }))
                issue(TriggerIssue::Level::Warning, &c, i, "The default value of " + v + " is a unit on the map. That unit is not on the map now");
        }
        return triggerVariableCode(v);  // declared as a CUnit
    }

    // An assignment's target: a number (or an array or per-player value), a flag, or a unit variable.
    std::string target(const TriggerCall& c, int i, const std::string& type, const std::string& v) {
        std::string name = v, index;
        if (const size_t b = v.find('['); b != std::string::npos && !v.empty() && v.back() == ']') name = v.substr(0, b), index = v.substr(b + 1, v.size() - b - 2);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        auto it = vars.find(name);
        if (it == vars.end()) {
            issue(TriggerIssue::Level::Error, &c, i, v.empty() ? "Select a variable" : "There is no variable with the name " + name);
            return "0";
        }
        const TriggerVariable& var = it->second->variable;
        const std::string code = triggerVariableCode(name);
        auto wrong = [&](const char* want) {
            issue(TriggerIssue::Level::Error, &c, i, name + " is a " + TriggerVariable::kindLabel(var.kind) + ". Use a " + std::string(want) + " here");
            return code;
        };
        if (type == "Flag" || type == "FlagTarget") return var.kind == TriggerVariable::Kind::Flag ? code : wrong("flag");
        if (type == "UnitTarget") return var.kind == TriggerVariable::Kind::Unit ? code : wrong("unit variable");
        if (var.kind == TriggerVariable::Kind::Constant) {
            issue(TriggerIssue::Level::Error, &c, i, name + " is a constant. You cannot change it");
            return code;
        }
        if (type == "ListTarget")
            return var.kind == TriggerVariable::Kind::Array || var.kind == TriggerVariable::Kind::PerPlayer ? code : wrong("list or number per player");
        if (var.kind == TriggerVariable::Kind::Number) return index.empty() ? code : wrong("number (without [ ])");
        if (var.kind == TriggerVariable::Kind::PerPlayer || var.kind == TriggerVariable::Kind::Array) {
            if (index.empty() && var.kind == TriggerVariable::Kind::Array) return wrong("value: name[index]");
            if (index.empty()) return code + "[getcurpl()]";
            const TriggerExprResult r = gx(index);
            if (!r.ok) issue(TriggerIssue::Level::Error, &c, i, r.error);
            if (r.ok && r.constant && (r.value < 0 || r.value >= triggerListSize(var, vars)))
                issue(TriggerIssue::Level::Error, &c, i, name + " has " + std::to_string(triggerListSize(var, vars)) + " items");
            return code + "[" + (!r.ok ? std::string("0") : r.constant ? r.code : checkIndex(r.code, triggerListSize(var, vars))) + "]";
        }
        return wrong("number");
    }

    // A block's own template, filled.
    std::string fill(const TriggerCall& c, const TriggerDef& d, const std::string& tmpl) {
        std::string out;
        for (size_t i = 0; i < tmpl.size();) {
            if (tmpl[i] == '{') {
                const size_t end = tmpl.find('}', i);
                const int pi = end == std::string::npos ? -1 : d.param(tmpl.substr(i + 1, end - i - 1));
                if (pi >= 0) {
                    out += value(c, d, pi);
                    i = end + 1;
                    continue;
                }
            }
            out += tmpl[i++];
        }
        return out;
    }

    std::string arg(const TriggerCall& c, const TriggerDef& d, const char* name) { return value(c, d, d.param(name)); }
    static std::string raw(const TriggerCall& c, const TriggerDef& d, const char* name) {
        const int pi = d.param(name);
        return pi >= 0 && pi < int(c.args.size()) ? c.args[size_t(pi)] : std::string();
    }

    void blockChecks(const TriggerCall& c, const TriggerDef& d) {
        if (d.has("cp") && !playerLoops) issue(TriggerIssue::Level::Warning, &c, -1, d.title + " acts for the current player. Put it in For each player");
        if (d.has("local") && raw(c, d, "to") == "current" && !playerLoops)
            issue(TriggerIssue::Level::Warning, &c, d.param("to"), "\"the current player\" has a value only in For each player");
        if (d.has("blocking"))
            issue(TriggerIssue::Level::Warning, &c, -1,
                  d.title + ": the other triggers of the player stop until it is complete" + (d.id == "act.wait" ? std::string() : ". Show text and Talking portrait do not stop them"));
        if (d.has("memory")) issue(TriggerIssue::Level::Info, &c, -1, "Reads or writes game memory at an address. Remastered permits only some addresses");
        if (d.has("differs") && !localDepth)
            issue(TriggerIssue::Level::Warning, &c, -1,
                  d.title + " is not the same on each computer. Use it in Run local block, or only for what a player sees or hears: if it changes the game, the game desyncs");
        if (d.id == "cond.pname") {
            const std::string p = raw(c, d, "player");
            if (!(p == "CurrentPlayer" || (p.size() == 2 && p[0] == 'P' && p[1] >= '1' && p[1] <= '8')))
                issue(TriggerIssue::Level::Error, &c, d.param("player"), "Use Player 1 to Player 8, or the current player");
        }
        if ((d.id == "cond.elapsed" || d.id == "cond.countdown") && raw(c, d, "cmp") == "Exactly")
            issue(TriggerIssue::Level::Warning, &c, d.param("cmp"), "Triggers do not run each second, so they can miss Exactly. Use At least or At most");
        for (size_t i = 0; i < d.params.size() && i < c.args.size(); ++i)
            if ((d.params[i].type == "Player" || d.params[i].type == "PlayerGroup") && c.args[i] == "CurrentPlayer" && !playerLoops)
                issue(TriggerIssue::Level::Warning, &c, int(i), "\"the current player\" has a value only in For each player");
    }

    std::string conditions(const std::vector<TriggerCall>& list, const char* join) {
        std::string out;
        for (const auto& c : list) {
            if (!c.enabled) continue;
            const std::string e = condition(c);
            if (!e.empty()) out += (out.empty() ? "" : std::string(" ") + join + " ") + e;
        }
        return out;
    }

    std::string condition(const TriggerCall& c) {
        const TriggerDef* d = TriggerDef::find(c.def);
        const std::string outer = curTitle;
        curTitle = d ? d->title : c.def;
        std::string out = mark(c, conditionBody(c));
        curTitle = outer;
        return out;
    }
    std::string conditionBody(const TriggerCall& c) {
        const TriggerDef* d = TriggerDef::find(c.def);
        if (!d || !d->condition) {
            issue(TriggerIssue::Level::Error, &c, -1, "Unknown condition " + c.def);
            return "Never()";
        }
        blockChecks(c, *d);
        if (d->id == "cond.anyof" || d->id == "cond.not") {
            std::string inner = c.lists.empty() ? "" : conditions(c.lists[0], "||");
            if (inner.empty()) {
                issue(TriggerIssue::Level::Warning, &c, -1, d->title + " has no conditions");
                inner = "Never()";
            }
            return d->id == "cond.not" ? "!(" + inner + ")" : "(" + inner + ")";
        }
        if (d->code.rfind("@prop ", 0) == 0) return property(c, *d, true);
        if (d->id == "cond.unitalive" || d->id == "cond.unitdead") {
            const std::string v = raw(c, *d, "unit"), u = unitRef(c, d->param("unit"), v), gen = unitGen(v);
            std::string alive = "(" + u + ".order != 0)";
            if (!gen.empty()) {
                alive = "(" + u + " != 0 && " + u + ".order != 0 && " + u + ".uniquenessIdentifier == " + gen + ")";
            }
            return d->id == "cond.unitalive" ? alive : "!" + alive;
        }
        if (d->id == "cond.listitem") return "(" + listItem(c, *d) + " " + arg(c, *d, "cmp") + " " + arg(c, *d, "value") + ")";
        if (d->id == "cond.chat") {  // euddraft's chatEvent: a chat line that is the text sets sd_chat to its number
            std::string text = raw(c, *d, "text");
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
            const auto bad = text.find_first_of(":;=[]");
            if (text.empty()) issue(TriggerIssue::Level::Error, &c, d->param("text"), "Type the chat text");
            else if (bad != std::string::npos)
                issue(TriggerIssue::Level::Error, &c, d->param("text"), std::string("A chat text cannot have \"") + text[bad] + "\" in it");
            else if (text.size() > 78) issue(TriggerIssue::Level::Error, &c, d->param("text"), "A chat text has at most 78 letters");
            auto it = chats.find(text);
            if (it == chats.end()) it = chats.insert({text, int(chats.size()) + 2}).first;  // the plugin wants 2 and up
            return "(sd_chat == " + std::to_string(it->second) + ")";
        }
        if (d->id == "cond.trains") return unitRef(c, d->param("unit"), raw(c, *d, "unit")) + ".check_buildq(" + arg(c, *d, "type") + ")";
        if (d->id == "cond.upgrade")
            return "(bread(" + upgradeAddr(playerNumber(c, d->param("player"), raw(c, *d, "player")), arg(c, *d, "upgrade")) + ") " + arg(c, *d, "cmp") + " " +
                   arg(c, *d, "n") + ")";
        if (d->id == "cond.tech") return "(bread(" + techAddr(playerNumber(c, d->param("player"), raw(c, *d, "player")), arg(c, *d, "tech")) + ") != 0)";
        if (d->id == "cond.unitin") {
            const std::string u = arg(c, *d, "unit"), a = locAddr(arg(c, *d, "where"));
            return "(" + u + ".posX >= dwread(" + a + ") && " + u + ".posX <= dwread(" + a + " + 8) && " + u + ".posY >= dwread(" + a + " + 4) && " + u +
                   ".posY <= dwread(" + a + " + 12))";
        }
        return fill(c, *d, d->code);
    }

    // An item of a list (or one player's value of a number per player): "v_list[index]", the index checked.
    std::string listItem(const TriggerCall& c, const TriggerDef& d) {
        const std::string list = arg(c, d, "list");
        const std::string name = raw(c, d, "list"), index = raw(c, d, "index");
        const int ii = d.param("index");
        const auto it = vars.find(name);
        const int listSize = it != vars.end() ? triggerListSize(it->second->variable, vars) : 8;
        if (ii >= 0 && ii < int(c.argBlocks.size()) && !c.argBlocks[size_t(ii)].empty())
            return list + "[" + checkIndex(value(c, d, ii), listSize) + "]";  // worked out in game
        const TriggerExprResult r = gx(index.empty() ? "0" : index);
        if (!r.ok) {
            issue(TriggerIssue::Level::Error, &c, ii, r.error);
            return list + "[0]";
        }
        if (it != vars.end() && r.constant) {
            const int size = triggerListSize(it->second->variable, vars);
            if (r.value < 0 || r.value >= size)
                issue(TriggerIssue::Level::Error, &c, ii,
                      it->second->variable.kind == TriggerVariable::Kind::PerPlayer ? "Use a player from 0 to 7 (Player 1 is 0)"
                                                                                     : "Use an item of " + name + " from 0 to " + std::to_string(size - 1));
        }
        return list + "[" + (r.constant ? r.code : checkIndex(r.code, listSize)) + "]";
    }

    // A property block: "<structure>.<member> = value" (actions) or "<structure>.<member> >= value" (conditions).
    std::string property(const TriggerCall& c, const TriggerDef& d, bool condition) {
        const std::string structure = d.code.substr(6);
        const TriggerProperty* prop = triggerCallProperty(d, c.args);
        if (!prop) {
            issue(TriggerIssue::Level::Error, &c, d.param("prop"), "Select a property");
            return condition ? "Never()" : "";
        }
        const std::string ref = structure == "CUnit" ? unitRef(c, d.param("unit"), raw(c, d, "unit")) : structure + "(" + value(c, d, 0) + ")";
        std::string v = value(c, d, d.param("value"));
        if (prop->scale > 1) {
            const bool literal = !v.empty() && std::all_of(v.begin(), v.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
            v = literal ? std::to_string(std::atoll(v.c_str()) * prop->scale) : "(" + v + ") * " + std::to_string(prop->scale);
        }
        const std::string member = ref + "." + prop->code;
        const bool number = prop->type == "Number" || prop->type.rfind("Unclear:", 0) == 0;
        if (condition) {
            const std::string cmp = raw(c, d, "cmp");
            if (prop->type == "YesNo") {
                if (cmp != "==" && cmp != "!=") issue(TriggerIssue::Level::Error, &c, d.param("cmp"), "This is yes or no. Use exactly or not");
                const bool wantSet = (raw(c, d, "value") != "0") == (cmp != "!=");
                return wantSet ? "(" + member + ")" : "!(" + member + ")";
            }
            if (!number && cmp != "==" && cmp != "!=") issue(TriggerIssue::Level::Warning, &c, d.param("cmp"), "Compares " + prop->label + " as a number");
            return "(" + member + " " + value(c, d, d.param("cmp")) + " " + v + ")";
        }
        const std::string mod = raw(c, d, "mod");
        if (!number && mod != "SetTo") issue(TriggerIssue::Level::Error, &c, d.param("mod"), prop->label + " is not a number. Use set to");
        return member + (mod == "Add" ? " += " : mod == "Subtract" ? " -= " : " = ") + v + ";";
    }

    std::string actions(const std::vector<TriggerCall>& list) {
        std::string out;
        for (const auto& c : list)
            if (c.enabled) out += action(c);
        return out;
    }
    std::string body(const TriggerCall& c, size_t list) { return list < c.lists.size() ? indentLines(actions(c.lists[list]), "    ") : ""; }

    // An action; one shown to one player (to: Player 1-8) is done as that player's: the current player set to
    // them for it, and back.
    int toPlayerBlocks = 0;
    std::string action(const TriggerCall& c) {
        const TriggerDef* d = TriggerDef::find(c.def);
        const std::string outer = curTitle;
        curTitle = d ? d->title : c.def;
        std::string out = mark(c, actionShown(c));
        curTitle = outer;
        return out;
    }
    std::string actionShown(const TriggerCall& c) {
        const TriggerDef* d = TriggerDef::find(c.def);
        const bool shows = d && d->has("local");  // only shows or plays something: a local value is safe in it
        localDepth += shows;
        std::string out = actionBody(c);
        localDepth -= shows;
        if (!d || d->condition || d->param("to") < 0 || out.empty()) return out;
        const std::string to = raw(c, *d, "to");
        if (to.size() < 2 || to[0] != 'P' || !std::isdigit(static_cast<unsigned char>(to[1]))) return out;
        const int pl = std::atoi(to.c_str() + 1) - 1;
        if (pl < 0 || pl > 7) return out;
        const std::string v = "cpTo" + std::to_string(++toPlayerBlocks);
        return "{\n    const " + v + " = getcurpl();\n    setcurpl(" + std::to_string(pl) + ");\n" + indentLines(out, "    ") + "    setcurpl(" + v + ");\n}\n";
    }
    std::string actionBody(const TriggerCall& c) {
        const TriggerDef* d = TriggerDef::find(c.def);
        if (!d || d->condition) {
            issue(TriggerIssue::Level::Error, &c, -1, "Unknown action " + c.def);
            return "";
        }
        blockChecks(c, *d);
        const std::string tag = "  // " + d->title;
        const std::string& id = d->id;
        loopChecks(c, *d);
        if (id == "act.donothing") return "";
        if (id == "act.unitorder" || id == "act.unitorderat" || id == "act.unitorderxy" || id == "act.unitorderunit") {  // one unit's order, as a command gives it
            helpers.insert("order");
            const std::string u = unitRef(c, d->param("unit"), raw(c, *d, "unit")), o = datCode("orders", std::atoi(raw(c, *d, "order").c_str()));
            if (id == "act.unitorderat") return "sd_orderAt(" + u + ", " + o + ", " + locAddr(arg(c, *d, "where")) + ");" + tag + "\n";
            if (id == "act.unitorderxy") return "sd_order(" + u + ", " + o + ", " + arg(c, *d, "x") + ", " + arg(c, *d, "y") + ", 0);" + tag + "\n";
            if (id == "act.unitorderunit") {
                const std::string t = unitRef(c, d->param("target"), raw(c, *d, "target"));
                return "sd_order(" + u + ", " + o + ", " + t + ".posX, " + t + ".posY, " + t + ".ptr);" + tag + "\n";
            }
            return "sd_order(" + u + ", " + o + ", " + u + ".posX, " + u + ".posY, 0);" + tag + "\n";
        }
        if (id == "act.unitvital") {  // up to the unit's most; down to 0 (no hit points: the unit dies)
            helpers.insert("vital"), helpers.insert("upgrade");
            return "sd_vital(" + unitRef(c, d->param("unit"), raw(c, *d, "unit")) + ", " + raw(c, *d, "vital") + ", " + raw(c, *d, "mod") + ", " +
                   arg(c, *d, "n") + ");" + tag + "\n";
        }
        if (id == "act.comment") return "// " + raw(c, *d, "text") + "\n";
        const bool all = raw(c, *d, "to") == "all";
        if (id == "act.display") return std::string(all ? "DisplayTextAll(" : "DisplayText(") + arg(c, *d, "text") + ");" + tag + "\n";
        if (id == "act.objectives") return std::string(all ? "SetMissionObjectivesAll(" : "SetMissionObjectives(") + arg(c, *d, "text") + ");" + tag + "\n";
        if (id == "act.playsound") return std::string(all ? "PlayWAVAll(" : "PlayWAV(") + arg(c, *d, "sound") + ");" + tag + "\n";
        if (id == "act.centerview") return std::string(all ? "CenterViewAll(" : "CenterView(") + arg(c, *d, "where") + ");" + tag + "\n";
        if (id == "act.ping") return std::string(all ? "MinimapPingAll(" : "MinimapPing(") + arg(c, *d, "where") + ");" + tag + "\n";
        // Buttons: a set's buttons are 20 bytes each (place, icon, when it shows, what it does, their values, the two
        // tooltips). The first change copies the set into the map (the game's own is not written), then writes there.
        if (id == "act.btnreplace") {
            helpers.insert("buttons");
            return "sd_setButton(" + arg(c, *d, "set") + ", " + arg(c, *d, "index") + ", " + arg(c, *d, "button") + ");" + tag + "\n";
        }
        if (id == "act.unitbuttons") {  // the unit's entry gets the set's count and buttons
            const std::string u = arg(c, *d, "unit"), s = arg(c, *d, "set");
            return "dwwrite(0x5187E8 + 12 * (" + u + "), dwread(0x5187E8 + 12 * (" + s + ")));" + tag + "\ndwwrite(0x5187EC + 12 * (" + u + "), dwread(0x5187EC + 12 * (" +
                   s + ")));\n";
        }
        if (id == "act.refreshbuttons") {  // EUD Editor 3's BtnRefresh: a unit made for an instant has the game build the card again
            return std::string("if (Memory(0x628438, AtLeast, 0x59CCA8)) {") + tag + "\n"
                   "    const checkCCMU = Memory(0x628438, Exactly, 0);\n"
                   "    const restoreBtn = SetMemoryX(0x6615AA, SetTo, 0, 0xFFFF0000);\n"
                   "    const setRemoveTimer = SetMemoryXEPD(0, SetTo, 1, 0xFFFF);\n"
                   "    maskread_epd(EPD(0x6615AA), 0xFFFF0000, ret=list(EPD(restoreBtn) + 5));\n"
                   "    cunitepdread_epd(EPD(0x628438), ret=list(EPD(checkCCMU) + 2, EPD(setRemoveTimer) + 4));\n"
                   "    DoActions(list(SetMemoryX(0x6615AA, SetTo, 0x20000, 0xFFFF0000), CreateUnit(1, 73, 64, 7)));\n"
                   "    if (!checkCCMU) {\n"
                   "        DoActions(list(restoreBtn, SetMemory(setRemoveTimer + 16, Add, 0x110 / 4), setRemoveTimer));\n"
                   "    }\n"
                   "}\n";
        }
        if (id == "act.clearmessages") {  // an empty text on each of the 11 message lines (not the center line)
            std::string out;
            for (int line = 0; line < 11; ++line)
                out += std::string(all ? "printAllAt(" : "printAt(") + std::to_string(line) + ", \"\");" + (line == 0 ? tag : "") + "\n";
            return out;
        }
        // A text with values, as eudplib's format text and its values (", a, b").
        auto formatText = [&](std::string& fmt, std::string& values) {
            const auto* gameValues = c.textValues();
            for (const auto& p : triggerTextParts(raw(c, *d, "text"))) {
                if (p.kind == TriggerTextPart::Kind::Words) {
                    for (char ch : p.text) fmt += ch == '{' ? "{{" : ch == '}' ? "}}" : std::string(1, ch);
                    continue;
                }
                // A text variable shows its text: {:t} with its place (the format typed with it does not matter).
                const auto tv = p.kind == TriggerTextPart::Kind::Expression ? vars.find(p.text) : vars.end();
                const bool textVar = tv != vars.end() && tv->second->variable.kind == TriggerVariable::Kind::Text;
                fmt += textVar ? "{:t}" : p.format.empty() ? "{}" : "{:" + p.format + "}";
                if (p.kind == TriggerTextPart::Kind::Expression) {
                    const TriggerExprResult r = gx(p.text);
                    if (!r.ok) issue(TriggerIssue::Level::Error, &c, d->param("text"), "{" + p.text + "}: " + r.error);
                    values += ", " + (r.ok ? r.code : std::string("0"));
                } else if (gameValues && p.value >= 0 && p.value < int(gameValues->size())) {
                    values += ", " + valueBlock((*gameValues)[size_t(p.value)]);
                } else {
                    issue(TriggerIssue::Level::Error, &c, d->param("text"), "The text has a value that is not there. Insert it again");
                    values += ", 0";
                }
            }
        };
        // A text variable that a block changes (each player has their own text: the block changes the current player's).
        auto textVar = [&](const char* param) -> std::string {
            const std::string name = raw(c, *d, param);
            const auto it = vars.find(name);
            if (it == vars.end() || it->second->variable.kind != TriggerVariable::Kind::Text) {
                issue(TriggerIssue::Level::Error, &c, d->param(param), name.empty() ? "Select a text variable" : name + " is not a text variable");
                return "";
            }
            return triggerVariableCode(name);
        };
        if (id == "act.text.set" || id == "act.text.add") {
            const std::string v = textVar("var");
            if (v.empty()) return "";
            std::string fmt, values;
            formatText(fmt, values);
            if (id == "act.text.add") return v + ".appendf(" + quote(fmt) + values + ");" + tag + "\n";
            return v + ".insertf(0, " + quote(fmt) + values + ");" + tag + "\n" + v + ".append(\"\");\n";
        }
        if (id == "act.text.clear") {
            const std::string v = textVar("var");
            return v.empty() ? std::string() : v + ".insert(0, \"\");" + tag + "\n" + v + ".append(\"\");\n";
        }
        if (id == "act.settbl") {  // a text of the game (stat_txt.tbl) written again, on every computer or in one language
            std::string fmt, values;
            formatText(fmt, values);
            const std::string s = arg(c, *d, "string"), lang = raw(c, *d, "lang");
            if (isNumber(s) && std::atoi(s.c_str()) < 1) issue(TriggerIssue::Level::Error, &c, d->param("string"), "Select a text of the game");
            const std::string set = "settblf(" + s + ", 0, " + quote(fmt) + values + ");";
            if (lang.empty() || lang == "any") return set + tag + "\n";
            return "if (LocalLocale == " + lang + ") {" + tag + "\n    " + set + "\n}\n";
        }
        if (id == "act.groupadd") {
            const std::string g = raw(c, *d, "group");
            auto it = vars.find(g);
            if (it == vars.end() || it->second->variable.kind != TriggerVariable::Kind::UnitGroup) {
                issue(TriggerIssue::Level::Error, &c, d->param("group"), g.empty() ? "Select a unit group" : g + " is not a unit group");
                return "";
            }
            helpers.insert("groups");
            return "sd_groupAdd(" + triggerVariableCode(g) + ", " + std::to_string(std::clamp(it->second->variable.size, 1, 1700)) + ", " +
                   unitRef(c, d->param("unit"), raw(c, *d, "unit")) + ");" + tag + "\n";
        }
        if (id == "act.foreachgroup") {  // each unit of the group; one that died (or whose slot has a new unit) leaves it
            const std::string g = raw(c, *d, "group");
            auto it = vars.find(g);
            if (it == vars.end() || it->second->variable.kind != TriggerVariable::Kind::UnitGroup) {
                issue(TriggerIssue::Level::Error, &c, d->param("group"), g.empty() ? "Select a unit group" : g + " is not a unit group");
                return "";
            }
            const std::string a = triggerVariableCode(g);
            ++unitLoops, ++loops;
            const std::string n = std::to_string(unitLoops), each = body(c, 0), dies = body(c, 1);
            --unitLoops, --loops;
            const std::string i = "gi" + n, cu = "cu" + n;
            return "{" + tag + "\n    var " + i + " = 0;\n    while (" + i + " < " + a + "[0]) {\n        const " + cu + " = CUnit.from_ptr(" + a + "[1 + 2 * " + i +
                   "]);\n        if (" + cu + ".order == 0 || " + cu + ".uniquenessIdentifier != " + a + "[2 + 2 * " + i + "]) {  // it died\n" +
                   indentLines(dies, "        ") + "            const last" + n + " = " + a + "[0] - 1;\n            " + a + "[1 + 2 * " + i + "] = " + a +
                   "[1 + 2 * last" + n + "];\n            " + a + "[2 + 2 * " + i + "] = " + a + "[2 + 2 * last" + n + "];\n            " + a + "[0] = last" + n +
                   ";\n            continue;\n        }\n" + indentLines(each, "    ") + "        " + i + " += 1;\n    }\n}\n";
        }
        if (id == "act.foreachplayerof") {  // each player in the game of a kind, race and force (as the game has them)
            const std::string kind = raw(c, *d, "kind"), race = raw(c, *d, "race"), force = raw(c, *d, "force");
            ++playerLoops, ++loops;
            const std::string n = std::to_string(playerLoops), b = body(c, 0);
            --playerLoops, --loops;
            const std::string pl = "pl" + n;
            std::string test = "playerexist(" + pl + ")";
            if (!kind.empty() && kind != "any") test += " && bread(0x57EEE8 + 36 * " + pl + ") == " + kind;
            if (!race.empty() && race != "any") test += " && bread(0x57EEE9 + 36 * " + pl + ") == " + race;
            if (!force.empty() && force != "any") test += " && bread(0x57EEEA + 36 * " + pl + ") == " + force;
            return "{" + tag + "\n    const cp" + n + " = getcurpl();\n    for (var " + pl + " = 0; " + pl + " < 8; " + pl + "++) {\n        if (" + test +
                   ") {\n            setcurpl(" + pl + ");\n" + indentLines(b, "        ") + "        }\n    }\n    setcurpl(cp" + n + ");\n}\n";
        }
        if (id == "act.share") {  // this computer's value, sent to every computer by MSQC: arrives a few frames later
            const int k = ++shares;
            const std::string s = "msqc_share" + std::to_string(k), into = raw(c, *d, "into");
            auto it = vars.find(into);
            if (it == vars.end() || it->second->variable.kind != TriggerVariable::Kind::PerPlayer) {
                issue(TriggerIssue::Level::Error, &c, d->param("into"), into.empty() ? "Select a number per player" : into + " is not a number per player");
                return "";
            }
            globals += "var " + s + "_local = 0;\nvar " + s + "_send = 0;\nconst " + s + "_recv = PVariable();\n";
            for (const char* part : {"_local", "_send", "_recv"})
                globals += "const _ns_" + s + part + " = EUDRegisterObjectToNamespace(\"" + s + part + "\", " + s + part + ");\n";
            perCycle += "    for (var p = 0; p < 8; p++) {  // " + into + ": what each computer sent\n        if (" + s + "_recv[p] != -1) " +
                        triggerVariableCode(into) + "[p] = " + s + "_recv[p];\n    }\n    " + s + "_send = 0;\n";
            return s + "_local = " + arg(c, *d, "value") + ";" + tag + "\n" + s + "_send = 1;\n";
        }
        // Commands as if a player gave them (eudplib's QueueGameCommand): sent from that player's computer, so every
        // computer gets them.
        if (id.rfind("act.qgc.", 0) == 0) {
            const std::string p = raw(c, *d, "player");
            const std::string who = p == "getcurpl()" || p == "CurrentPlayer" ? "IsUserCP()" : "getuserplayerid() == " + playerNumber(c, d->param("player"), p);
            std::string cmd;
            if (id == "act.qgc.rightclick") cmd = "QueueGameCommand_RightClick((" + arg(c, *d, "x") + ") + (" + arg(c, *d, "y") + ") * 65536);";
            else if (id == "act.qgc.train") cmd = "QueueGameCommand_TrainUnit(" + arg(c, *d, "unit") + ");";
            else if (id == "act.qgc.select") {
                helpers.insert("select");
                cmd = "sd_select(" + unitRef(c, d->param("unit"), raw(c, *d, "unit")) + ".ptr);";
            } else if (id == "act.qgc.ping") cmd = "QueueGameCommand_MinimapPing((" + arg(c, *d, "x") + ") + (" + arg(c, *d, "y") + ") * 65536);";
            else if (id == "act.qgc.pause") cmd = "QueueGameCommand_PauseGame();";
            else if (id == "act.qgc.resume") cmd = "QueueGameCommand_ResumeGame();";
            else return "";
            return "if (" + who + ") {" + tag + "\n    " + cmd + "\n}\n";
        }
        if (id == "act.showtext") {
            std::string fmt, values;
            formatText(fmt, values);
            const std::string where = raw(c, *d, "where");
            if (where.rfind("line", 0) == 0)
                return std::string(all ? "printAllAt(" : "printAt(") + where.substr(4) + ", " + quote(fmt) + values + ");" + tag + "\n";
            if (where == "middle") return std::string(all ? "eprintAll(" : "eprintf(") + quote(fmt) + values + ");" + tag + "\n";
            return std::string(all ? "printAll(" : "println(") + quote(fmt) + values + ");" + tag + "\n";
        }
        // A text with values: as many values as the text has {} (two at most).
        auto textArgs = [&] {
            std::string out;
            const int n = d->textValues(c.args);
            if (triggerPlaceholders(raw(c, *d, "text")) > 2)
                issue(TriggerIssue::Level::Error, &c, d->param("text"), "The text has more than two {}. Use at most two: one for each value");
            for (int k = 0; k < n; ++k) out += ", " + arg(c, *d, k == 0 ? "a" : "b");
            return out;
        };
        if (id == "act.eprintf") return std::string(all ? "eprintAll(" : "eprintf(") + arg(c, *d, "text") + textArgs() + ");" + tag + "\n";
        if (id == "act.print") return std::string(all ? "printAll(" : "println(") + arg(c, *d, "text") + textArgs() + ");" + tag + "\n";
        if (id == "act.setvar") {
            const std::string t = arg(c, *d, "var"), v = arg(c, *d, "value"), mod = raw(c, *d, "mod");
            return t + (mod == "Add" ? " += " : mod == "Subtract" ? " -= " : " = ") + v + ";" + tag + "\n";
        }
        if (id == "act.setflag") {
            const std::string f = arg(c, *d, "flag"), st = raw(c, *d, "state");
            return f + (st == "Disable" ? " = 0;" : st == "Toggle" ? " = 1 - " + f + ";" : " = 1;") + tag + "\n";
        }
        if (id == "act.random") {
            const std::string t = arg(c, *d, "var"), lo = arg(c, *d, "lo"), hi = arg(c, *d, "hi");
            return t + " = " + lo + " + rand() % (" + hi + " - " + lo + " + 1);" + tag + "\n";
        }
        if (id == "act.setunitvar") {
            const std::string var = arg(c, *d, "var"), from = raw(c, *d, "unit");
            std::string gen;
            if (from == "the next created unit") gen = "(CUnit.from_next().uniquenessIdentifier + 1) & 0xFF";
            else if (!unitGen(from).empty()) gen = unitGen(from);
            else gen = unitRef(c, d->param("unit"), from) + ".uniquenessIdentifier";
            return var + " = " + unitEpd(c, *d) + ";" + tag + "\n" + var + "_gen = " + gen + ";\n";
        }
        if (id == "act.if") {
            std::string cond = c.lists.empty() ? "" : conditions(c.lists[0], "&&");
            if (cond.empty()) cond = "Never()";
            std::string out = "if (" + cond + ") {" + tag + "\n" + body(c, 1) + "}";
            const std::string otherwise = body(c, 2);
            if (!otherwise.empty()) out += " else {\n" + otherwise + "}";
            return out + "\n";
        }
        if (id == "act.repeat") {
            const std::string n = arg(c, *d, "n");
            ++counters, ++loops;
            const std::string v = "rp" + std::to_string(counters), b = body(c, 0);
            --counters, --loops;
            return "for (var " + v + " = 0; " + v + " < " + n + "; " + v + "++) {" + tag + "\n" + b + "}\n";
        }
        if (id == "act.while") {
            const std::string limit = arg(c, *d, "limit"), w = "wl" + std::to_string(++timers);
            std::string cond = c.lists.empty() ? "" : conditions(c.lists[0], "&&");
            if (cond.empty()) cond = "Always()";
            ++loops;
            const std::string inner = body(c, 1);
            --loops;
            return "{" + tag + "\n    var " + w + " = 0;\n    while (" + w + " < " + limit + ") {\n        if (!(" + cond + ")) break;\n        " + w + "++;\n" +
                   indentLines(inner, "    ") + "    }\n}\n";
        }
        if (id == "act.foreachplayer") {
            const std::string group = raw(c, *d, "players");
            unsigned mask = 0;
            if (group == "AllPlayers") mask = 0xFF;
            else if (group.size() >= 2 && group[0] == 'P' && std::isdigit(static_cast<unsigned char>(group[1]))) {
                const int pn = std::atoi(group.c_str() + 1);
                if (pn >= 1 && pn <= 8) mask = 1u << (pn - 1);
            } else if (group.rfind("Force", 0) == 0) {
                const int f = std::atoi(group.c_str() + 5) - 1;
                for (int pl = 0; pl < 8; ++pl)
                    if (map.playerForce[pl] == f) mask |= 1u << pl;
            }
            if (!mask) issue(TriggerIssue::Level::Error, &c, d->param("players"), "For each player accepts all players, Player 1 to Player 8, or a force");
            ++playerLoops, ++loops;
            const std::string n = std::to_string(playerLoops), b = body(c, 0);
            --playerLoops, --loops;
            char m[16];
            std::snprintf(m, sizeof m, "0x%02X", mask);
            return "{" + tag + "\n    const cp" + n + " = getcurpl();\n    for (var pl" + n + " = 0; pl" + n + " < 8; pl" + n + "++) {\n        if (((" + m + " >> pl" + n +
                   ") & 1) != 0 && playerexist(pl" + n + ")) {\n            setcurpl(pl" + n + ");\n" + indentLines(b, "        ") + "        }\n    }\n    setcurpl(cp" +
                   n + ");\n}\n";
        }
        if (id == "act.foreachunit") {
            const std::string unit = raw(c, *d, "unit"), owner = raw(c, *d, "player");
            ++unitLoops;
            const std::string n = std::to_string(unitLoops);
            std::string filter;
            if (unit != "Any unit" && unit != "229") {
                if (unit == "Men" || unit == "Buildings" || unit == "Factories" || unit == "230" || unit == "231" || unit == "232")
                    issue(TriggerIssue::Level::Error, &c, d->param("unit"), "For each unit accepts one unit type or Any unit");
                else filter = "cu" + n + ".unitType == " + arg(c, *d, "unit");
            }
            std::string loop = "EUDLoopCUnit()";
            if (owner != "AllPlayers") {
                if (owner.size() >= 2 && owner[0] == 'P' && std::isdigit(static_cast<unsigned char>(owner[1])) && std::atoi(owner.c_str() + 1) <= 8)
                    loop = "EUDLoopPlayerCUnit(" + std::to_string(std::atoi(owner.c_str() + 1) - 1) + ")";  // only that player's units
                else if (owner.size() >= 2 && owner[0] == 'P' && std::isdigit(static_cast<unsigned char>(owner[1])))
                    filter += (filter.empty() ? "" : " && ") + std::string("cu") + n + ".owner == " + std::to_string(std::atoi(owner.c_str() + 1) - 1);
                else issue(TriggerIssue::Level::Error, &c, d->param("player"), "For each unit accepts the units of one player or of all players");
            }
            ++loops;
            unitLoopOnePlayer.push_back(loop != "EUDLoopCUnit()");
            const std::string b = body(c, 0);
            unitLoopOnePlayer.pop_back();
            --loops;
            if (cost(c.lists.empty() ? std::vector<TriggerCall>{} : c.lists[0], 1) > 20)
                issue(TriggerIssue::Level::Warning, &c, -1, "Slow: the actions in it run for each unit (up to 1,700) each time. Keep them short");
            --unitLoops;
            return "foreach (cu" + n + " : " + loop + ") {" + tag + "\n" +
                   (filter.empty() ? b : "    if (" + filter + ") {\n" + indentLines(b, "    ") + "    }\n") + "}\n";
        }
        if (id == "act.foreachnewunit") {  // the units made since this loop last ran (EUDLoopNewCUnit)
            const std::string unit = raw(c, *d, "unit"), owner = raw(c, *d, "player");
            ++unitLoops;
            const std::string n = std::to_string(unitLoops);
            std::string filter;
            if (unit != "Any unit" && unit != "229") filter = "cu" + n + ".unitType == " + arg(c, *d, "unit");
            if (owner != "AllPlayers") {
                if (owner.size() >= 2 && owner[0] == 'P' && std::isdigit(static_cast<unsigned char>(owner[1])))
                    filter += (filter.empty() ? "" : " && ") + std::string("cu") + n + ".owner == " + std::to_string(std::atoi(owner.c_str() + 1) - 1);
                else issue(TriggerIssue::Level::Error, &c, d->param("player"), "For each new unit accepts the units of one player or of all players");
            }
            ++loops;
            const std::string b = body(c, 0);
            --loops, --unitLoops;
            return "foreach (cu" + n + " : EUDLoopNewCUnit()) {" + tag + "\n" + (filter.empty() ? b : "    if (" + filter + ") {\n" + indentLines(b, "    ") + "    }\n") +
                   "}\n";
        }
        if (id == "act.forrange") {  // each number from a to b (EUDLoopRange); the counter is the number
            const std::string from = arg(c, *d, "from"), to = arg(c, *d, "to");
            ++counters, ++loops;
            const std::string v = "rp" + std::to_string(counters), b = body(c, 0);
            --counters, --loops;
            return "foreach (" + v + " : EUDLoopRange(" + from + ", (" + to + ") + 1)) {" + tag + "\n" + b + "}\n";
        }
        if (id == "act.once") return "once {" + tag + "\n" + body(c, 0) + "}\n";
        if (id == "act.local") {  // only on the current player's own computer (IsUserCP): for what only they see or hear
            // Every computer runs the same game: one that changes the game alone (units, resources, locations,
            // switches, deaths, the random numbers, the trigger list's timing) is no longer the same, and the game
            // desyncs. What only shows or plays here is safe; variables are safe only while nothing that changes
            // the game reads them.
            static const std::set<std::string> flow = {"act.if",        "act.repeat", "act.forrange", "act.foreachitem", "act.foreachplayer", "act.while",
                                                       "act.foreachunit", "act.foreachnewunit", "act.once", "act.local", "act.after", "act.every", "act.sequence"};
            // Data only this computer uses (what it draws or plays: images, strings, sounds, wireframes, buttons, names,
            // colors) is safe; what the game sends over the network or simulates is not.
            static const std::set<std::string> safe = {"act.comment", "act.donothing", "act.break",   "act.stop",
                                                       "act.unitcolor", "act.wireframe", "act.prop.image", "act.playername",
                                                       "act.btnreplace", "act.unitbuttons", "act.settbl", "act.share"};
            static const std::set<std::string> variables = {"act.setvar", "act.setflag", "act.listset", "act.setunitvar"};
            static const std::set<std::string> unknown = {"act.setmemory", "act.setmemoryx", "act.raw", "act.runtrigger"};
            // A random number moves the game's random numbers: on one computer only, they are no longer the same.
            std::function<bool(const TriggerCall&)> random = [&](const TriggerCall& x) {
                if (x.def == "val.random" || x.def == "act.random") return true;
                for (const auto& slot : x.argBlocks)
                    for (const auto& v : slot)
                        if (random(v)) return true;
                return false;
            };
            std::function<void(const std::vector<TriggerCall>&)> guard = [&](const std::vector<TriggerCall>& list) {
                for (const TriggerCall& in : list) {
                    if (!in.enabled) continue;
                    const TriggerDef* id2 = TriggerDef::find(in.def);
                    if (!id2) continue;
                    if (random(in)) {
                        issue(TriggerIssue::Level::Error, &in, -1,
                              "Run local block: a random number here changes the random numbers on this computer only, and the game desyncs. Make it outside");
                        continue;
                    }
                    if (id2->condition || id2->value) continue;
                    if (flow.count(id2->id)) {
                        for (size_t k = 0; k < in.lists.size(); ++k) guard(in.lists[k]);  // conditions too (a random number in them)
                        continue;
                    }
                    if (id2->has("blocking"))
                        issue(TriggerIssue::Level::Error, &in, -1, "Run local block: " + id2->title + " holds up the triggers on this computer only, and the game desyncs");
                    else if (id2->has("local") || safe.count(id2->id) || id2->id.rfind("act.lb.", 0) == 0)
                        continue;  // only shows or plays something
                    else if (variables.count(id2->id))
                        issue(TriggerIssue::Level::Warning, &in, -1,
                              "Run local block: the value changes on this computer only. Use it only for what this player sees: if a trigger that changes the game reads it, the game desyncs");
                    else if (unknown.count(id2->id))
                        issue(TriggerIssue::Level::Warning, &in, -1,
                              "Run local block: StarDraft cannot tell what " + id2->title + " changes. Change only what this player sees, or the game desyncs");
                    else
                        issue(TriggerIssue::Level::Error, &in, -1,
                              "Run local block: " + id2->title + " changes the game on this computer only: the other computers do not do it, and the game desyncs. Put it outside");
                }
            };
            if (!c.lists.empty()) guard(c.lists[0]);
            ++localDepth;
            const std::string b = body(c, 0);
            --localDepth;
            return "if (IsUserCP()) {" + tag + "\n" + b + "}\n";
        }
        if ((id == "act.sequence" || id == "act.after" || id == "act.every") && atMapStart)
            issue(TriggerIssue::Level::Warning, &c, -1,
                  d->title + " goes on only while its trigger runs again: in On map start it runs one time. Put it in a trigger in On every trigger cycle");
        if (id == "act.sequence") {
            // Steps: the actions between Pauses. Step k runs when the time since the start reaches the Pauses before
            // it; several steps that are due run in the same cycle. tm: the start (game tick + 1; 0: not started).
            const std::string tm = "tm" + std::to_string(++timers);
            globals += "var " + tm + " = 0;\nvar " + tm + "step = 0;\n";
            std::vector<std::vector<TriggerCall>> steps(1);
            std::vector<std::string> due{"0"};
            const TriggerDef* pause = TriggerDef::find("act.pause");
            if (!c.lists.empty())
                for (const TriggerCall& in : c.lists[0]) {
                    if (in.def == "act.pause" && pause) {
                        due.push_back(due.back() + " + (" + arg(in, *pause, "s") + ") * 24");
                        steps.emplace_back();
                    } else steps.back().push_back(in);
                }
            std::string out = "if (" + tm + "step < " + std::to_string(steps.size()) + ") {" + tag + "\n    if (" + tm + " == 0) " + tm + " = getgametick() + 1;\n";
            ++sequenceDepth;
            for (size_t k = 0; k < steps.size(); ++k)
                out += "    if (" + tm + "step == " + std::to_string(k) + " && getgametick() + 1 >= " + tm + " + " + due[k] + ") {\n        " + tm + "step = " +
                       std::to_string(k + 1) + ";\n" + indentLines(indentLines(actions(steps[k]), "    "), "    ") + "    }\n";
            --sequenceDepth;
            return out + "}\n";
        }
        if (id == "act.pause") {  // its place in a Sequence makes its code (above)
            issue(TriggerIssue::Level::Error, &c, -1, "Put Pause in a Sequence: outside one, it does nothing. After a delay waits without a Sequence");
            return std::string();
        }
        if (id == "act.after" || id == "act.every") {
            const std::string tm = "tm" + std::to_string(++timers), frames = "(" + arg(c, *d, "s") + ") * 24";
            const std::string b = body(c, 0);
            if (id == "act.every") {
                globals += "var " + tm + " = 0;\n";
                return "if (getgametick() >= " + tm + ") {" + tag + "\n    " + tm + " = getgametick() + " + frames + ";\n" + b + "}\n";
            }
            globals += "var " + tm + " = 0;\nvar " + tm + "done = 0;\n";
            return "if (" + tm + "done == 0) {" + tag + "\n    if (" + tm + " == 0) " + tm + " = getgametick() + " + frames + ";\n    else if (getgametick() >= " + tm +
                   ") {\n        " + tm + "done = 1;\n" + indentLines(b, "    ") + "    }\n}\n";
        }
        if (d->code.rfind("@prop ", 0) == 0) return property(c, *d, false) + tag + "\n";
        if (id == "act.createunitvar")  // the slot it takes, then the unit
            return arg(c, *d, "var") + " = CUnit.from_next();\nCreateUnit(1, " + arg(c, *d, "unit") + ", " + arg(c, *d, "where") + ", " + arg(c, *d, "player") +
                   ");" + tag + "\n" + arg(c, *d, "var") + "_gen = " + arg(c, *d, "var") + ".uniquenessIdentifier;\n";
        if (id == "act.printat") {
            return std::string(all ? "printAllAt(" : "printAt(") + arg(c, *d, "line") + ", " + arg(c, *d, "text") + textArgs() + ");" + tag + "\n";
        }
        if (id == "act.eprint") return std::string(all ? "eprintAll(" : "eprintln(") + arg(c, *d, "text") + ");" + tag + "\n";
        if (id == "act.runtrigger") {
            const std::string fn = arg(c, *d, "trigger");
            return fn.empty() ? std::string() : fn + "();" + tag + "\n";
        }
        if (id == "act.stop") return (playerLoops ? std::string("setcurpl(cp1);  // (as it was before For each player)\n") : std::string()) + "return;" + tag + "\n";
        if (id == "act.break") {
            if (!loops) issue(TriggerIssue::Level::Error, &c, -1, "Put Leave the loop in a loop (Repeat, While, For each)");
            return "break;" + tag + "\n";
        }
        if (id == "act.displayat")
            return std::string(all ? "DisplayTextAllAt(" : "DisplayTextAt(") + arg(c, *d, "line") + ", " + arg(c, *d, "text") + ");" + tag + "\n";
        if (id == "act.upgrade") {
            const std::string a = upgradeAddr(playerNumber(c, d->param("player"), raw(c, *d, "player")), arg(c, *d, "upgrade")), n = arg(c, *d, "n");
            const std::string mod = raw(c, *d, "mod");
            return "bwrite(" + a + ", " + (mod == "Add" ? "bread(" + a + ") + " + n : mod == "Subtract" ? "bread(" + a + ") - " + n : n) + ");" + tag + "\n";
        }
        if (id == "act.tech")
            return "bwrite(" + techAddr(playerNumber(c, d->param("player"), raw(c, *d, "player")), arg(c, *d, "tech")) + ", " + arg(c, *d, "state") + ");" + tag + "\n";
        if (id == "act.listset") {
            const std::string item = listItem(c, *d), v = arg(c, *d, "value"), mod = raw(c, *d, "mod");
            return item + (mod == "Add" ? " += " : mod == "Subtract" ? " -= " : " = ") + v + ";" + tag + "\n";
        }
        if (id == "act.foreachitem") {
            const std::string list = arg(c, *d, "list");
            const auto it = vars.find(raw(c, *d, "list"));
            const int size = it != vars.end() ? triggerListSize(it->second->variable, vars) : 1;
            ++counters, ++loops;
            const std::string v = "rp" + std::to_string(counters), b = body(c, 0);
            --counters, --loops;
            return "for (var " + v + " = 0; " + v + " < " + std::to_string(size) + "; " + v + "++) {" + tag + "\n" + b + "}\n";
        }
        if (id == "act.setmemory" || id == "act.setmemoryx") {  // a fixed address outside what StarDraft knows: say so
            const std::string a = raw(c, *d, "addr");
            char* end = nullptr;
            const unsigned long long addr = a.empty() ? 0 : std::strtoull(a.c_str(), &end, 0);
            if (end && *end == 0 && !knownMemory(uint32_t(addr))) {
                char m[200];
                std::snprintf(m, sizeof m, "0x%06llX is outside the tables StarDraft knows (.dat data, deaths, locations, units, strings). A wrong address can crash the game", addr);
                issue(TriggerIssue::Level::Warning, &c, d->param("addr"), m);
            }
        }
        if (d->code.empty()) {
            issue(TriggerIssue::Level::Error, &c, -1, d->title + ": StarDraft cannot build this block yet");
            return "";
        }
        return fill(c, *d, d->code) + ";" + tag + "\n";
    }

    // Changes to units that a For each unit loop goes through, and death counters used as numbers.
    void loopChecks(const TriggerCall& c, const TriggerDef& d) {
        const std::string& id = d.id;
        if (!unitLoopOnePlayer.empty()) {
            if (id == "act.killunitat" || id == "act.removeunitat" || id == "act.killall" || id == "act.removeall" || id == "act.giveunits")
                issue(TriggerIssue::Level::Warning, &c, -1,
                      d.title + " in For each unit changes the units that the loop goes through: the loop can miss units or stop. Do it after the loop, or "
                                "change only \"the unit\"");
            else if (unitLoopOnePlayer.back() && (id == "act.unitgive" || (id == "act.unitprop" && raw(c, d, "prop") == "owner")))
                issue(TriggerIssue::Level::Warning, &c, -1,
                      "The loop goes through the units of one player. A unit that goes to another player leaves that list: the loop can miss units or "
                      "stop. Loop through the units of all players");
        }
        if (id == "act.setdeaths" || id == "act.setdeathsx") {
            const std::string u = raw(c, d, "unit");
            if (isNumber(u) && realUnitTypes.count(std::atoi(u.c_str()))) {
                const size_t n = size_t(std::atoi(u.c_str()));
                const std::string name = n < map.unitNames.size() && !map.unitNames[n].empty() ? map.unitNames[n] : "unit type " + u;
                issue(TriggerIssue::Level::Warning, &c, d.param("unit"),
                      "The deaths of " + name + " also go up when a " + name + " dies in the game. To keep a number, use a variable");
            }
        }
    }

    // Whether a memory address is in a table StarDraft knows: the .dat data, and the game's main tables.
    static bool knownMemory(uint32_t a) {
        static const std::vector<std::pair<uint32_t, uint32_t>> ranges = [] {
            std::vector<std::pair<uint32_t, uint32_t>> r = {
                {0x58A364, 0x58A364 + 4 * 228 * 12},       // deaths
                {0x58DC60, 0x58DC60 + 20 * 255},           // locations
                {0x59CCA8, 0x59CCA8 + 336 * 1700},         // units
                {0x57EEEC, 0x57EEEC + 36 * 12},            // players
                {0x57F0F0, 0x57F0F0 + 4 * 12 * 2},         // minerals, gas
                {0x58D2B0, 0x58D2B0 + 46 * 12 + 15 * 12},  // upgrades (classic)
                {0x58CE24, 0x58CE24 + 24 * 12 * 2},        // tech (classic)
                {0x58F44A, 0x58F44A + 12 * 15 * 2},        // upgrades and tech (Brood War)
                {0x514178, 0x514178 + 4260},               // requirements
                {0x5187E8, 0x5187E8 + 12 * 250},           // button sets
                {0x6D5A30, 0x6D5A30 + 40290},              // stat_txt.tbl
            };
            for (const DatSchema& s : DatSchema::all())
                for (const DatField& f : s.fields)
                    for (const auto& range : f.memoryRanges()) r.push_back(range);
            return r;
        }();
        for (const auto& [lo, hi] : ranges)
            if (a >= lo && a < hi) return true;
        return false;
    }

    // A unit's EPD (what unit variables hold).
    std::string unitEpd(const TriggerCall& c, const TriggerDef& d) {
        const std::string v = raw(c, d, "unit");
        if (v == "the next created unit") return "CUnit.from_next()";
        if (v == "the unit") {
            if (!unitLoops) issue(TriggerIssue::Level::Error, &c, d.param("unit"), "\"the unit\" has a value only in For each unit or For each new unit");
            return "cu" + std::to_string(std::max(1, unitLoops));
        }
        const std::string ref = unitRef(c, d.param("unit"), v);  // a CUnit variable
        return ref == "CUnit(0)" ? "0" : ref;
    }

    static int cost(const std::vector<TriggerCall>& list, int units) {
        int sum = 0;
        for (const auto& c : list) {
            if (!c.enabled) continue;
            const TriggerDef* d = TriggerDef::find(c.def);
            if (!d) continue;
            int inner = 0;
            for (const auto& l : c.lists) inner += cost(l, units);
            int times = 1;
            auto argOf = [&](const char* name) {
                const int pi = d->param(name);
                return pi >= 0 && pi < int(c.args.size()) ? c.args[size_t(pi)] : std::string();
            };
            if (d->id == "act.foreachunit") {  // one player's units: about an eighth; a type filter: the blocks run for a sixteenth
                const std::string unit = argOf("unit"), owner = argOf("player");
                const bool one = owner.size() == 2 && owner[0] == 'P' && owner[1] >= '1' && owner[1] <= '8';
                const bool typed = !unit.empty() && unit != "229" && unit != "Any unit";
                const int n = one ? std::max(1, units / 8) : units;
                sum += d->cost + (typed ? n + inner * std::max(1, n / 16) : inner * n);
                continue;
            }
            if (d->id == "act.every") {  // once in seconds x 24 runs (the triggers run each frame with EUD Turbo)
                const int s = std::atoi(argOf("s").c_str());
                if (s > 0) {
                    sum += d->cost + (inner + s * 24 - 1) / (s * 24);
                    continue;
                }
            }
            if (d->id == "act.foreachnewunit") times = 4;
            else if (d->id == "act.forrange") times = 10;
            else if (d->id == "act.foreachplayer" || d->id == "act.foreachitem") times = 8;
            else if (d->id == "act.repeat" || d->id == "act.while") {
                const int pi = d->id == "act.repeat" ? d->param("n") : d->param("limit");
                times = pi >= 0 && pi < int(c.args.size()) ? std::max(1, std::atoi(c.args[size_t(pi)].c_str())) : 1;
            }
            sum += d->cost + inner * times;
        }
        return sum;
    }
};

}  // namespace

int TriggerSet::cost(const TriggerNode& t, int units) { return Gen::cost(t.conditions, units) + Gen::cost(t.actions, units); }

namespace {
// The code without its marks, and the block of each line (see Gen::mark).
std::string stripMarks(const std::string& code, const std::vector<TriggerCodeLine>& marks, std::vector<TriggerCodeLine>* lines) {
    std::string out;
    out.reserve(code.size());
    std::vector<int> open;
    const TriggerNode* fnNode = nullptr;
    size_t i = 0;
    while (i < code.size()) {
        const size_t lineStart = out.size();
        std::vector<int> openedHere;
        int firstClosed = -1;
        const TriggerNode* lineNode = nullptr;
        for (; i < code.size() && code[i] != '\n'; ++i) {
            const char ch = code[i];
            if (ch == '\x01' || ch == '\x04') {
                const size_t end = code.find('\x02', i);
                const int id = std::atoi(code.c_str() + i + 1);
                i = end;
                if (ch == '\x01') open.push_back(id), openedHere.push_back(id);
                else if (id >= 0 && id < int(marks.size())) lineNode = fnNode = marks[size_t(id)].node;
            } else if (ch == '\x03') {
                if (open.empty()) continue;
                const int id = open.back();
                open.pop_back();
                if (firstClosed < 0 && std::find(openedHere.begin(), openedHere.end(), id) != openedHere.end()) firstClosed = id;
            } else {
                out += ch;
            }
        }
        const bool newline = i < code.size();
        if (newline) out += '\n', ++i;
        if (lines) {
            const int pick = firstClosed >= 0 ? firstClosed : !openedHere.empty() ? openedHere.back() : !open.empty() ? open.back() : -1;
            TriggerCodeLine l;
            if (pick >= 0 && pick < int(marks.size())) l = marks[size_t(pick)];
            else l.node = lineNode ? lineNode : fnNode;
            lines->push_back(l);
        }
        if (out.compare(lineStart, std::string::npos, newline ? "}\n" : "}") == 0) fnNode = nullptr;  // a function's end
    }
    return out;
}
std::string stripMarks(const std::string& code) { return stripMarks(code, {}, nullptr); }
}  // namespace

int triggerErrorLine(const std::string& line) {
    for (const char* key : {"Module \"triggers\" Line ", "triggers.eps\", line "}) {
        const size_t m = line.find(key);
        if (m != std::string::npos) return std::atoi(line.c_str() + m + std::char_traits<char>::length(key));
    }
    return -1;
}

// The helper functions of the generated triggers: one file of their own (generated/sdlib.eps), which the
// triggers import from (only what they use; eudplib compiles a function only when it is called).
std::string TriggerSet::helperLibrary() {
    std::string out = "// StarDraft's helper functions for the generated triggers (triggers.eps imports them). Generated: do not edit.\n\n";
        out += "function sd_upgradeAddr(pl, u) {  // a player's level of an upgrade (0-45, then 46-60)\n    if (u < 46) return 0x58D2B0 + pl * 46 + u;\n"
               "    return 0x58F32C + pl * 15 + (u - 46);\n}\n";
    out += "function sd_techAddr(pl, t) {  // whether a player has researched a technology (0-23, then 24-43)\n    if (t < 24) return 0x58CF44 + pl * 24 + t;\n"
               "    return 0x58F140 + pl * 20 + (t - 24);\n}\n";
    out += "function sd_order(u: CUnit, order, x, y, t) {  // the unit's order now, with its place and its target unit (0: none)\n"
               "    u.orderID = order;\n    u.orderState = 0;\n    u.orderTargetX = x;\n    u.orderTargetY = y;\n    u.orderTarget = t;\n}\n"
               "function sd_orderAt(u: CUnit, order, a) {  // at the middle of a location (its address)\n"
               "    sd_order(u, order, (dwread(a) + dwread(a + 8)) / 2, (dwread(a + 4) + dwread(a + 12)) / 2, 0);\n}\n";
    out += "function sd_maxEnergy(u: CUnit) {  // as StarCraft: 250 for a hero or with the unit's energy upgrade, else 200\n"
               "    const t = u.unitType;\n    if (t.baseProperty.Hero) return 250 * 256;\n    var g = -1;\n"
               "    if (t == 1) g = 21;\n    else if (t == 8) g = 22;\n    else if (t == 9) g = 19;\n    else if (t == 12) g = 23;\n"
               "    else if (t == 34) g = 51;\n    else if (t == 45) g = 31;\n    else if (t == 46) g = 32;\n    else if (t == 67) g = 40;\n"
               "    else if (t == 71) g = 44;\n    else if (t == 60) g = 47;\n    else if (t == 63) g = 49;\n"
               "    if (g >= 0 && bread(sd_upgradeAddr(u.owner, g)) > 0) return 250 * 256;\n    return 200 * 256;\n}\n"
               "function sd_vital(u: CUnit, kind, up, n) {  // kind 0 hit points, 1 shields, 2 energy; up 1 increase, 0 decrease\n"
               "    const d = n * 256;\n    var now = 0;\n    var most = 0;\n"
               "    if (kind == 0) { now = u.hp; most = u.unitType.maxHp; }\n"
               "    else if (kind == 1) { now = u.shield; most = u.unitType.maxShield * 256; }\n"
               "    else { now = u.energy; most = sd_maxEnergy(u); }\n"
               "    if (up) {\n        if (now >= most || most - now <= d) now = most;\n        else now += d;\n"
               "    } else if (now <= d || (kind == 0 && now - d < 256)) {\n"
               "        if (kind == 0) { u.die(); return; }  // no hit points: the unit dies\n        now = 0;\n"
               "    } else now -= d;\n"
               "    if (kind == 0) u.hp = now;\n    else if (kind == 1) u.shield = now;\n    else u.energy = now;\n}\n";
    out += "function sd_max(a, b) {\n    if (a > b) return a;\n    return b;\n}\n";
    out += "function sd_ldirX(d, a) {  // the x of a point d pixels away in direction a (0-255, 0 is up)\n    const x, y = lengthdir_256(d, a);\n    return x;\n}\n"
               "function sd_ldirY(d, a) {\n    const x, y = lengthdir_256(d, a);\n    return y;\n}\n";
    out += "const sd_selection = EUDArray(1);\nfunction sd_select(ptr) {  // one unit, as if the player clicked it\n    sd_selection[0] = ptr;\n"
               "    QueueGameCommand_Select(1, sd_selection);\n}\n";
    out += "function sd_groupAdd(g, room, u: CUnit) {  // a unit group: how many, then each unit's address and generation\n"
               "    const a = EUDArray.cast(g);\n    const n = a[0];\n    if (n >= room) return;\n"
               "    for (var i = 0; i < n; i++) {\n        if (a[1 + 2 * i] == u.ptr) return;  // in it already\n    }\n"
               "    a[1 + 2 * n] = u.ptr;\n    a[2 + 2 * n] = u.uniquenessIdentifier;\n    a[0] = n + 1;\n}\n";
    out += "// A button's address in its set (0x5187E8 + 12 x set: count, address). The first change copies the set into\n"
               "// the map (room for 32 buttons), so the game's own buttons are not written.\n"
               "const sd_buttonCopied = EUDArray(250);\n"
               "function sd_button(set, index) {\n"
               "    if (sd_buttonCopied[set] == 0) {\n"
               "        const copy = Db(20 * 32);\n"
               "        memcpy(copy, dwread(0x5187EC + 12 * set), 20 * dwread(0x5187E8 + 12 * set));\n"
               "        dwwrite(0x5187EC + 12 * set, copy);\n"
               "        sd_buttonCopied[set] = 1;\n"
               "    }\n"
               "    return dwread(0x5187EC + 12 * set) + 20 * index;\n"
               "}\n"
               "// A whole button: its 20 bytes; a number past the set's last button adds it (the set grows).\n"
               "function sd_setButton(set, index, place, icon, condition, action, conditionValue, actionValue, tooltip, tooltipNotUsable) {\n"
               "    const b = sd_button(set, index);\n"
               "    if (index >= dwread(0x5187E8 + 12 * set)) dwwrite(0x5187E8 + 12 * set, index + 1);\n"
               "    wwrite(b, place);\n    wwrite(b + 2, icon);\n    dwwrite(b + 4, condition);\n    dwwrite(b + 8, action);\n"
               "    wwrite(b + 12, conditionValue);\n    wwrite(b + 14, actionValue);\n    wwrite(b + 16, tooltip);\n    wwrite(b + 18, tooltipNotUsable);\n"
               "}\n";
    out += "function sd_min(a, b) {\n    if (a < b) return a;\n    return b;\n}\n";
    out += "function sd_locAddr(loc) {  // a location's left, top, right, bottom\n    return 0x58DC60 + 20 * (loc - 1);\n}\n";
    return out;
}

std::string TriggerSet::generate(const TriggerMapInfo& map, std::vector<TriggerIssue>& issues, const TriggerBuildOptions& options,
                                 std::vector<TriggerCodeLine>* lines) const {
    Gen g{map, issues};
    g.vars = variables();
    g.debug = options.debug;
    // Unit types the map has or the triggers make: their deaths are not free to use as numbers.
    for (const auto& u : map.units) g.realUnitTypes.insert(u.type);
    std::function<void(const std::vector<TriggerCall>&)> made = [&](const std::vector<TriggerCall>& list) {
        for (const auto& c : list) {
            if (c.def == "act.createunit" || c.def == "act.createunitvar" || c.def == "act.createunitprops")
                if (const TriggerDef* d = TriggerDef::find(c.def); d && d->param("unit") >= 0 && d->param("unit") < int(c.args.size()))
                    if (const std::string& u = c.args[size_t(d->param("unit"))]; Gen::isNumber(u)) g.realUnitTypes.insert(std::atoi(u.c_str()));
            for (const auto& l : c.lists) made(l);
        }
    };
    std::function<void(const TriggerNode&)> madeIn = [&](const TriggerNode& n) {
        made(n.conditions), made(n.actions);
        for (const auto& ch : n.children) madeIn(ch);
    };
    for (const auto& r : roots) madeIn(r);
    // Variables (Definitions), in tree order.
    std::string decls, constDecls;
    std::string textStarts;  // the text variables' default texts, set at the start for every player
    std::set<std::string> seen;
    std::function<void(const TriggerNode&)> declare = [&](const TriggerNode& n) {
        if (n.kind == TriggerNode::Kind::Variable) {
            const bool valid = !n.name.empty() && identStart(n.name[0]) && std::all_of(n.name.begin(), n.name.end(), identChar) && n.name != "counter" &&
                               n.name != "current";
            if (!valid) issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1, "\"" + n.name + "\" cannot be a variable name: letters, digits and _ only"});
            if (!seen.insert(n.name).second) issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1, "two variables are called " + n.name});
            const std::string code = triggerVariableCode(n.name);
            std::string& out = n.variable.kind == TriggerVariable::Kind::Constant ? constDecls : decls;
            out += g.nodeMark(n);
            if (n.variable.kind == TriggerVariable::Kind::Unit && map.known && n.variable.initial.size() > 1 && n.variable.initial[0] == '@') {
                const uint32_t serial = uint32_t(std::strtoul(n.variable.initial.c_str() + 1, nullptr, 10));
                if (std::none_of(map.units.begin(), map.units.end(), [&](const auto& u) { return u.serial == serial; }))
                    issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1, "The unit it starts as (picked on the map) is not on the map now. Pick another unit"});
            }
            if (!n.comment.empty()) {  // the description, as the comment above the declaration
                std::istringstream lines(n.comment);
                for (std::string line; std::getline(lines, line);) out += "// " + line + "\n";
            }
            switch (n.variable.kind) {
            case TriggerVariable::Kind::Constant: {
                const TriggerExprResult r = triggerExpression(n.variable.initial.empty() ? "0" : n.variable.initial, {}, 0);
                if (!r.ok || !r.constant) issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1, "a constant's value must be a plain number"});
                out += "const " + code + " = " + (r.ok && r.constant ? r.code : "0") + ";  // " + n.name + "\n";
                break;
            }
            case TriggerVariable::Kind::Number:
            case TriggerVariable::Kind::Flag: {
                const TriggerExprResult r = triggerExpression(n.variable.initial.empty() ? "0" : n.variable.initial, {}, 0);
                if (!r.ok || !r.constant) issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1, "the starting value must be a plain number"});
                decls += "var " + code + " = " + (r.ok && r.constant ? r.code : "0") + ";  // " + n.name + "\n";
                break;
            }
            case TriggerVariable::Kind::PerPlayer: decls += "const " + code + " = PVariable();  // " + n.name + " (per player)\n"; break;
            case TriggerVariable::Kind::Array: {
                const int size = triggerListSize(n.variable, g.vars);
                std::string sizeCode = std::to_string(size);
                if (!n.variable.sizeConstant.empty()) {
                    const auto it = g.vars.find(n.variable.sizeConstant);
                    if (it == g.vars.end() || it->second->variable.kind != TriggerVariable::Kind::Constant)
                        issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1, "the size must be a number or a constant; " + n.variable.sizeConstant + " is not a constant"});
                    else sizeCode = triggerVariableCode(n.variable.sizeConstant);
                }
                if (size > 4096) issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1, "a list holds at most 4096 items"});
                const TriggerExprResult r = triggerExpression(n.variable.initial.empty() ? "0" : n.variable.initial, {}, 0);
                if (!r.ok || !r.constant) issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1, "the items' starting value must be a plain number"});
                if (r.ok && r.constant && r.value != 0) {
                    std::string items;
                    for (int k = 0; k < std::min(size, 4096); ++k) items += (k ? ", " : "") + r.code;
                    decls += "const " + code + " = EUDArray(list(" + items + "));  // " + n.name + " (" + std::to_string(size) + " items)\n";
                } else {
                    decls += "const " + code + " = EUDArray(" + sizeCode + ");  // " + n.name + "\n";
                }
                break;
            }
            case TriggerVariable::Kind::Unit: decls += "var " + code + ": CUnit = 0;  // " + n.name + "\nvar " + code + "_gen = 0;  // the slot's generation when it got the unit\n"; break;
            case TriggerVariable::Kind::Text: {  // a StringBuffer: a map string that blocks change while the game runs
                const int room = std::clamp(n.variable.size, 1, TriggerVariable::kTextRoomMax);  // its maximum length (bytes)
                if (int(n.variable.initial.size()) > room)
                    issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1,
                                      "the default text has " + std::to_string(n.variable.initial.size()) + " letters; the maximum length is " +
                                          std::to_string(room)});
                decls += "const " + code + " = StringBuffer(" + std::to_string(room) + ");  // " + n.name + " (text, at most " +
                         std::to_string(room) + " letters)\n";
                textStarts += "    " + code + ".insert(0, " + quote(n.variable.initial) + ");\n    " + code + ".append(\"\");\n";
                break;
            }
            case TriggerVariable::Kind::UnitGroup: {  // how many, then each unit's address and its slot's generation
                const int room = std::clamp(n.variable.size, 1, 1700);
                decls += "const " + code + " = EUDArray(" + std::to_string(1 + 2 * room) + ");  // " + n.name + " (unit group, room for " + std::to_string(room) +
                         ")\n";
                break;
            }
            }
        }
        for (const auto& c : n.children) declare(c);
    };
    for (const auto& r : roots) declare(r);
    // Triggers: one function each, called in tree order from the category's hook.
    std::string functions, calls[3];
    std::set<std::string> fnNames;
    // A trigger's function is named after it, so the code is easy to search: "Starting resources" -> t_Starting_resources.
    auto functionName = [&](const std::string& name) {
        std::string fn = "t_";
        for (unsigned char ch : name) {
            if (std::isalnum(ch)) fn += char(ch);
            else if (fn.back() != '_') fn += '_';
        }
        while (fn.size() > 2 && fn.back() == '_') fn.pop_back();
        if (fn == "t_") fn = "t_trigger";
        std::string unique = fn;
        for (int k = 2; fnNames.count(unique); ++k) unique = fn + "_" + std::to_string(k);
        fnNames.insert(unique);
        return unique;
    };
    std::function<void(const TriggerNode&, int)> name = [&](const TriggerNode& n, int cat) {
        if (n.kind == TriggerNode::Kind::Trigger) {
            if (cat == 0) {
                if (g.definitions.count(n.name))
                    issues.push_back({TriggerIssue::Level::Error, &n, nullptr, -1, "Another trigger in Definitions has the name " + n.name + ". Give it a different name"});
                else g.definitions[n.name] = &n;
            }
            if (n.enabled) g.fnOf[&n] = functionName(n.name);
            return;
        }
        if (n.kind == TriggerNode::Kind::Folder && !n.enabled) return;
        for (const auto& c : n.children) name(c, cat);
    };
    for (size_t i = 0; i < roots.size() && i < 3; ++i) name(roots[i], int(i));
    std::function<void(const TriggerNode&, int, const std::string&)> walk = [&](const TriggerNode& n, int cat, const std::string& where) {
        if (n.kind == TriggerNode::Kind::Trigger) {
            if (!n.enabled) return;
            g.trigger = &n;
            g.counters = g.unitLoops = g.playerLoops = 0;
            g.atMapStart = cat == 1;
            const std::string fn = g.fnOf[&n];
            std::string cond = g.conditions(n.conditions, "&&");
            const std::string acts = g.actions(n.actions);
            if (n.conditions.empty()) issues.push_back({TriggerIssue::Level::Warning, &n, nullptr, -1, "no conditions: it never runs"});
            functions += g.nodeMark(n) + "// " + where + n.name + "\nfunction " + fn + "() {\n";
            if (cond.empty()) cond = "Never()";
            if (stripMarks(cond) == "Always()") functions += indentLines(acts, "    ");
            else functions += "    if (" + cond + ") {\n" + indentLines(acts, "        ") + "    }\n";
            functions += "}\n\n";
            if (cat > 0) calls[cat] += "    " + g.nodeMark(n) + fn + "();\n";  // Definitions' triggers run only from Run a trigger
            return;
        }
        if (n.kind == TriggerNode::Kind::Folder && !n.enabled) return;
        const std::string inner = n.kind == TriggerNode::Kind::Folder ? where + n.name + " / " : where;
        for (const auto& c : n.children) walk(c, cat, inner);
    };
    for (size_t i = 0; i < roots.size() && i < 3; ++i) walk(roots[i], int(i), roots[i].name + " / ");
    // Run a trigger: a trigger that runs itself (directly or through others) never ends. Unused Definitions triggers.
    std::string forward;
    std::set<const TriggerNode*> runTargets;
    for (const auto& [from, to] : g.runs) runTargets.insert(to.begin(), to.end());
    for (const auto& [nm, node] : g.definitions) {
        if (!node->enabled) continue;
        if (!runTargets.count(node)) {
            issues.push_back({TriggerIssue::Level::Info, node, nullptr, -1, "No Run a trigger block runs this trigger"});
            continue;
        }
        forward += "function " + g.fnOf[node] + "();\n";
        std::set<const TriggerNode*> seen;
        std::vector<const TriggerNode*> todo(g.runs[node].begin(), g.runs[node].end());
        bool loop = false;
        while (!todo.empty() && !loop) {
            const TriggerNode* t = todo.back();
            todo.pop_back();
            if (t == node) loop = true;
            else if (seen.insert(t).second) todo.insert(todo.end(), g.runs[t].begin(), g.runs[t].end());
        }
        if (loop) issues.push_back({TriggerIssue::Level::Error, node, nullptr, -1, "This trigger runs itself through Run a trigger. It never ends"});
    }
    // Units picked on the map: found once at map start by type, owner and position. Unit variables that start as one.
    std::string unitStarts;
    for (const auto& [name, node] : g.vars)
        if (node->variable.kind == TriggerVariable::Kind::Unit && !node->variable.initial.empty() && node->variable.initial[0] == '@') {
            const uint32_t serial = uint32_t(std::strtoul(node->variable.initial.c_str() + 1, nullptr, 10));
            g.pickedUnits.insert(serial);
            unitStarts += "    " + triggerVariableCode(name) + " = pu_" + std::to_string(serial) + ";\n    " + triggerVariableCode(name) + "_gen = pu_" +
                          std::to_string(serial) + "_gen;\n";
            g.twinCheck(serial, nullptr, -1);
        }
    std::string pickedDecls, picked;
    for (uint32_t serial : g.pickedUnits) {
        pickedDecls += "var pu_" + std::to_string(serial) + ": CUnit = 0;\nvar pu_" + std::to_string(serial) + "_gen = 0;\n";
        const auto it = std::find_if(map.units.begin(), map.units.end(), [&](const auto& u) { return u.serial == serial; });
        if (it != map.units.end()) picked += pickedLookup(*it);
    }
    std::string out = "// Generated by StarDraft from triggers.json. Do not edit: it is rewritten on every build.\n\n";
    {  // the imports first: euddraft takes them only before the other statements
        // The helpers come from generated/sdlib.eps (TriggerSet::helperLibrary): only the ones the triggers use.
        static const std::pair<const char*, const char*> kNames[] = {
            {"upgrade", "sd_upgradeAddr"},
            {"tech", "sd_techAddr"},
            {"order", "sd_order sd_orderAt"},
            {"vital", "sd_vital"},
            {"max", "sd_max"},
            {"ldir", "sd_ldirX sd_ldirY"},
            {"select", "sd_select"},
            {"groups", "sd_groupAdd"},
            {"buttons", "sd_button sd_setButton"},
            {"min", "sd_min"},
            {"loc", "sd_locAddr"}};
        std::string imports;
        for (const auto& [key, list] : kNames)
            if (g.helpers.count(key)) {
                std::istringstream in(list);
                for (std::string n; in >> n;) imports += "import .sdlib.f_" + n + ";\n";  // a function of the file next to this one (epScript's name: f_ + its name)
            }
        if (!imports.empty()) out += "// Helpers (generated/sdlib.eps)\n" + imports + "\n";
    }
    if (!constDecls.empty()) out += "// Constants\n" + constDecls + "\n";
    if (!decls.empty()) out += "// Variables\n" + decls + "\n";
    // Keys and mouse buttons the triggers read: euddraft's MSQC plugin fills these (Build writes its [MSQC] lines).
    if (const std::string inputs = msqcDeclarations(stripMarks(functions + calls[1] + calls[2])); !inputs.empty())
        out += "// Keyboard and mouse (sent to every player by euddraft's MSQC plugin; msqc_Here: this computer only)\n" + inputs + "\n";
    if (!pickedDecls.empty() || !g.globals.empty()) out += "// Units picked on the map, timers\n" + pickedDecls + g.globals + "\n";
    if (!g.chats.empty()) {  // Build writes the [chatEvent] section from these lines
        out += "// Chat commands (euddraft's chatEvent plugin sets sd_chat when a chat line is one of them)\nvar sd_chat = 0;\n"
               "const _ns_sd_chat = EUDRegisterObjectToNamespace(\"sd_chat\", sd_chat);\n";
        for (const auto& [text, n] : g.chats) out += "// chatEvent " + std::to_string(n) + " " + text + "\n";
        out += "\n";
    }
    if (!g.helpers.empty()) {
        if (g.helpers.count("index")) {  // the debug build: where each checked index is, and whether its message was shown
            std::string sites;
            for (size_t k = 0; k < g.sites.size(); ++k) sites += (k ? ", " : "") + std::string("Db(") + quote(g.sites[k]) + ")";
            out += "// Debug build: an index out of range shows a message one time, and item 0 is used in its place.\n"
                   "const sd_idxSites = EUDArray(list(" + sites + "));\nconst sd_idxShown = EUDArray(" + std::to_string(g.sites.size()) + ");\n"
                   "function sd_idx(i, n, site) {\n    if (i < n) return i;\n    if (sd_idxShown[site] == 0) {\n        sd_idxShown[site] = 1;\n"
                   "        printAll(\"Debug: {:s}: index {} is not from 0 to {}\", sd_idxSites[site], i, n - 1);\n    }\n    return 0;\n}\n";
        }
        out += "\n";
    }
    if (!forward.empty()) out += "// Triggers in Definitions (Run a trigger runs them)\n" + forward + "\n";
    out += functions;
    out += "function onPluginStart() {\n";
    if (!picked.empty()) out += "    foreach (cu : EUDLoopCUnit()) {\n" + picked + "    }\n";
    if (!textStarts.empty())  // StringBuffer changes only the current player's copy: as each player, on their own computer
        out += "    const sd_cp = getcurpl();\n    setcurpl(getuserplayerid());\n" + textStarts + "    setcurpl(sd_cp);\n";
    out += unitStarts + calls[1] + "}\n\n";
    const bool mouse = (functions + calls[1] + calls[2]).find("msqc_Mouse") != std::string::npos;
    out += "function afterTriggerExec() {\n" + std::string(mouse ? "    msqc_keepMouse();\n" : "") + g.perCycle + calls[2] + "}\n";
    std::string code = stripMarks(out, g.marks, lines);
    // TrgUnit($U("Terran Marine")) reads as TrgUnit("Terran Marine"), Weapon(EncodeWeapon("Gauss Rifle")) as
    // Weapon("Gauss Rifle"): these take the name itself (after the line map: no line changes).
    for (const auto& [type, encode] : {std::pair{"TrgUnit", "$U"}, std::pair{"Weapon", "EncodeWeapon"}, std::pair{"Flingy", "EncodeFlingy"},
                                       std::pair{"Upgrade", "EncodeUpgrade"}, std::pair{"Tech", "EncodeTech"}}) {
        const std::string from = std::string(type) + "(" + encode + "(\"", to = std::string(type) + "(\"";
        for (size_t at = 0; (at = code.find(from, at)) != std::string::npos;) {
            if (at > 0 && (std::isalnum(static_cast<unsigned char>(code[at - 1])) || code[at - 1] == '_')) {  // (part of a longer name)
                at += from.size();
                continue;
            }
            const size_t close = code.find("\"))", at + from.size());
            if (close == std::string::npos) break;
            code.replace(close, 3, "\")"), code.replace(at, from.size(), to);
            at += to.size();
        }
    }
    return code;
}

std::vector<std::string> triggerChatLines(const std::string& code) {
    std::vector<std::string> out;
    std::istringstream in(code);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("// chatEvent ", 0) != 0) continue;
        const size_t sp = line.find(' ', 13);
        if (sp == std::string::npos) continue;
        if (out.empty()) out.push_back("__addr__ : sd_chat");
        out.push_back(line.substr(sp + 1) + " : " + line.substr(13, sp - 13));
    }
    return out;
}

std::vector<TriggerIssue> TriggerSet::check(const TriggerMapInfo& map) const {
    std::vector<TriggerIssue> issues;
    generate(map, issues);
    return issues;
}

// --- Finding lag: changes that make a heavy trigger cost less ----------------------------------------------------

namespace {
std::string callArg(const TriggerCall& c, const TriggerDef& d, const char* name) {
    const int pi = d.param(name);
    return pi >= 0 && pi < int(c.args.size()) ? c.args[size_t(pi)] : std::string();
}
bool hasValueBlock(const TriggerCall& c) {
    for (const auto& b : c.argBlocks)
        if (!b.empty()) return true;
    return false;
}
TriggerCall* callAt(TriggerNode& t, const std::vector<std::pair<int, int>>& path) {
    TriggerCall* c = nullptr;
    for (const auto& [l, i] : path) {
        std::vector<TriggerCall>* v = c ? (l >= 2 && l - 2 < int(c->lists.size()) ? &c->lists[size_t(l - 2)] : nullptr)
                                        : (l == 0 ? &t.conditions : l == 1 ? &t.actions : nullptr);
        if (!v || i < 0 || i >= int(v->size())) return nullptr;
        c = &(*v)[size_t(i)];
    }
    return c;
}
bool onlyNothing(const std::vector<TriggerCall>& list) {
    return std::all_of(list.begin(), list.end(), [](const TriggerCall& c) { return !c.enabled || c.def == "act.donothing"; });
}
// A For each unit whose only block is an If (without Else) with a condition on the unit's type or owner, which the
// loop can do itself: the If's place, the condition's place, the loop's value to set and its new value.
struct LoopFilter {
    size_t ifAt = 0, condAt = 0;
    const char* param = "";
    std::string value;
};
std::optional<LoopFilter> loopFilter(const TriggerCall& loop) {
    const TriggerDef* d = TriggerDef::find(loop.def);
    if (!d || d->id != "act.foreachunit" || loop.lists.empty()) return std::nullopt;
    const auto& body = loop.lists[0];
    std::optional<size_t> ifAt;
    for (size_t k = 0; k < body.size(); ++k) {
        if (!body[k].enabled || body[k].def == "act.donothing") continue;
        if (ifAt) return std::nullopt;
        ifAt = k;
    }
    if (!ifAt) return std::nullopt;
    const TriggerCall& branch = body[*ifAt];
    if (branch.def != "act.if" || branch.lists.size() < 3 || !onlyNothing(branch.lists[2])) return std::nullopt;
    const std::string unit = callArg(loop, *d, "unit"), owner = callArg(loop, *d, "player");
    for (size_t k = 0; k < branch.lists[0].size(); ++k) {
        const TriggerCall& c = branch.lists[0][k];
        const TriggerDef* cd = TriggerDef::find(c.def);
        if (!c.enabled || !cd || hasValueBlock(c) || callArg(c, *cd, "unit") != "the unit") continue;
        if (cd->id == "cond.unittype" && (unit == "229" || unit == "Any unit")) {
            const std::string type = callArg(c, *cd, "type");
            if (!type.empty() && std::all_of(type.begin(), type.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }) &&
                std::atoi(type.c_str()) < 229)
                return LoopFilter{*ifAt, k, "unit", type};
        }
        if (cd->id == "cond.unitowner" && owner == "AllPlayers") {
            const std::string pl = callArg(c, *cd, "player");
            if (pl.size() == 2 && pl[0] == 'P' && pl[1] >= '1' && pl[1] <= '8') return LoopFilter{*ifAt, k, "player", pl};
        }
    }
    return std::nullopt;
}
}  // namespace

std::vector<TriggerCostFix> TriggerSet::costFixes(const TriggerNode& t, int category) {
    std::vector<TriggerCostFix> out;
    const int before = cost(t);
    if (t.kind != TriggerNode::Kind::Trigger || before <= kHeavy) return out;
    auto tryFix = [&](TriggerCostFix f) {
        TriggerNode copy = t;
        f.before = before;
        if (!applyCostFix(copy, f)) return;
        f.after = cost(copy);
        if (f.after < before) out.push_back(std::move(f));
    };
    std::function<void(const std::vector<TriggerCall>&, int, std::vector<std::pair<int, int>>)> walk = [&](const std::vector<TriggerCall>& list, int key,
                                                                                                          std::vector<std::pair<int, int>> path) {
        for (size_t k = 0; k < list.size(); ++k) {
            const TriggerCall& c = list[k];
            if (!c.enabled) continue;
            auto here = path;
            here.push_back({key, int(k)});
            if (const auto lf = loopFilter(c)) {
                TriggerCostFix f;
                f.kind = TriggerCostFix::Kind::FilterInLoop;
                f.path = here;
                f.text = std::string(lf->param) == "unit"
                             ? "For each unit can check the unit type itself: then the blocks in it run only for units of that type"
                             : "For each unit can check the owner itself: then it goes through only the units of that player";
                tryFix(f);
            }
            for (size_t l = 0; l < c.lists.size(); ++l) walk(c.lists[l], 2 + int(l), here);
        }
    };
    walk(t.actions, 1, {});
    // (Paused for 0.5: the timing blocks (After, Every, Sequence) are hidden until timing is precise; see T.6.)
    const bool inEvery = t.actions.size() == 1 && t.actions[0].def == "act.every";
    if (false && category == 2 && !inEvery && !t.actions.empty()) {
        TriggerCostFix f;
        f.kind = TriggerCostFix::Kind::EverySecond;
        f.text = "Do the actions one time each second (in Every so often), not in each trigger cycle";
        tryFix(f);
    }
    // A filter in the loop first: it does the same work.
    std::stable_sort(out.begin(), out.end(), [](const TriggerCostFix& a, const TriggerCostFix& b) {
        return a.kind != b.kind ? a.kind == TriggerCostFix::Kind::FilterInLoop : a.after < b.after;
    });
    return out;
}

bool TriggerSet::applyCostFix(TriggerNode& t, const TriggerCostFix& f) {
    if (f.kind == TriggerCostFix::Kind::EverySecond) {
        const TriggerDef* every = TriggerDef::find("act.every");
        if (!every || t.actions.empty()) return false;
        TriggerCall e = TriggerCall::make(*every);
        e.args[size_t(every->param("s"))] = "1";
        e.lists[0] = std::move(t.actions);
        t.actions = {std::move(e)};
        return true;
    }
    TriggerCall* loop = callAt(t, f.path);
    const auto lf = loop ? loopFilter(*loop) : std::nullopt;
    if (!lf) return false;
    const TriggerDef* d = TriggerDef::find(loop->def);
    loop->args[size_t(d->param(lf->param))] = lf->value;
    auto& body = loop->lists[0];
    TriggerCall& branch = body[lf->ifAt];
    branch.lists[0].erase(branch.lists[0].begin() + std::ptrdiff_t(lf->condAt));
    if (std::none_of(branch.lists[0].begin(), branch.lists[0].end(), [](const TriggerCall& c) { return c.enabled; })) {
        std::vector<TriggerCall> then = std::move(branch.lists[1]);  // no condition is left: its Then takes its place
        body.erase(body.begin() + std::ptrdiff_t(lf->ifAt));
        body.insert(body.begin() + std::ptrdiff_t(lf->ifAt), then.begin(), then.end());
    }
    return true;
}

// --- Templates ------------------------------------------------------------------------------------------------------

namespace {
TriggerCall tmplMake(const char* id, std::vector<std::string> args = {}) {
    TriggerCall c = TriggerCall::make(*TriggerDef::find(id));
    for (size_t i = 0; i < args.size() && i < c.args.size(); ++i)
        if (!args[i].empty()) c.args[i] = args[i];
    return c;
}
TriggerCall tmplWrap(const char* id, std::vector<std::string> args, std::vector<TriggerCall> inner) {
    TriggerCall c = tmplMake(id, std::move(args));
    if (!c.lists.empty()) c.lists[0] = std::move(inner);
    return c;
}
}  // namespace

const std::vector<TriggerTemplate>& triggerTemplates() {
    static const std::vector<TriggerTemplate> t = [] {
        std::vector<TriggerTemplate> out;
        {
            TriggerNode n = TriggerSet::newTrigger("Starting resources");
            n.actions.push_back(tmplWrap("act.foreachplayer", {"AllPlayers"},
                                         {tmplMake("act.setresources", {"Add", "500", "Ore", "CurrentPlayer"}), tmplMake("act.display", {"Good luck!", "current"})}));
            out.push_back({"Starting resources", "When the map starts, gives minerals and a message to each player.", 1, n});
        }
        {
            TriggerNode n = TriggerSet::newTrigger("Victory");
            TriggerCall branch = tmplMake("act.if");
            branch.lists[0] = {tmplMake("cond.opponents", {"CurrentPlayer", "AtMost", "0"})};
            branch.lists[1] = {tmplMake("act.victory")};
            n.actions.push_back(tmplWrap("act.foreachplayer", {"AllPlayers"}, {branch}));
            out.push_back({"Victory when the enemies are gone", "A player wins when no opponents are left.", 2, n});
        }
        {
            TriggerNode n = TriggerSet::newTrigger("Countdown");
            n.actions.push_back(tmplWrap("act.once", {}, {tmplMake("act.timer", {"SetTo", "300"})}));
            TriggerCall branch = tmplMake("act.if");
            branch.lists[0] = {tmplMake("cond.elapsed", {"AtLeast", "10"}), tmplMake("cond.countdown", {"AtMost", "0"})};
            branch.lists[1] = {tmplWrap("act.foreachplayer", {"AllPlayers"}, {tmplMake("act.defeat")})};
            n.actions.push_back(branch);
            out.push_back({"Countdown", "A countdown of five minutes. When it gets to zero, all players lose.", 2, n});
        }
        {
            TriggerNode n = TriggerSet::newTrigger("Respawn hero");
            n.conditions[0] = tmplMake("cond.command", {"P1", "AtMost", "0", "20"});  // Jim Raynor (the marine)
            n.actions.push_back(tmplMake("act.createunit", {"1", "20", "64", "P1"}));
            n.actions.push_back(tmplMake("act.display", {"Jim Raynor is back.", "all"}));
            out.push_back({"Respawn a hero", "When Player 1 has no Jim Raynor (the marine hero), a new one appears.", 2, n});
        }
        return out;
    }();
    return t;
}

}  // namespace stardraft
