#include "stardraft/classic_triggers.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

#include "stardraft/game_data.h"
#include "stardraft/strings.h"

namespace stardraft {

namespace {

constexpr size_t kCondSize = 20, kActSize = 32, kActionsAt = kClassicConditions * kCondSize, kFlagsAt = kActionsAt + kClassicActions * kActSize;
constexpr uint16_t kMaskFlag = 0x4353;  // "SC"

uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v), p[1] = uint8_t(v >> 8); }

ClassicTrigger decode(const uint8_t* t) {
    ClassicTrigger out;
    for (size_t i = 0; i < size_t(kClassicConditions); ++i) {
        const uint8_t* p = t + i * kCondSize;
        ClassicCondition& c = out.conditions[i];
        c.location = get32(p), c.player = get32(p + 4), c.amount = get32(p + 8), c.unit = get16(p + 12);
        c.comparison = p[14], c.type = p[15], c.resource = p[16], c.flags = p[17], c.internal = get16(p + 18);
    }
    for (size_t i = 0; i < size_t(kClassicActions); ++i) {
        const uint8_t* p = t + kActionsAt + i * kActSize;
        ClassicAction& a = out.actions[i];
        a.location = get32(p), a.text = get32(p + 4), a.wav = get32(p + 8), a.time = get32(p + 12), a.player = get32(p + 16);
        a.number = get32(p + 20), a.unit = get16(p + 24), a.type = p[26], a.modifier = p[27], a.flags = p[28], a.padding = p[29];
        a.internal = get16(p + 30);
    }
    out.flags = get32(t + kFlagsAt);
    std::copy(t + kFlagsAt + 4, t + kFlagsAt + 4 + kClassicGroups, out.players.begin());
    out.currentAction = t[kFlagsAt + 4 + kClassicGroups];
    return out;
}

void encode(const ClassicTrigger& in, uint8_t* t) {
    for (size_t i = 0; i < size_t(kClassicConditions); ++i) {
        uint8_t* p = t + i * kCondSize;
        const ClassicCondition& c = in.conditions[i];
        put32(p, c.location), put32(p + 4, c.player), put32(p + 8, c.amount), put16(p + 12, c.unit);
        p[14] = c.comparison, p[15] = c.type, p[16] = c.resource, p[17] = c.flags, put16(p + 18, c.internal);
    }
    for (size_t i = 0; i < size_t(kClassicActions); ++i) {
        uint8_t* p = t + kActionsAt + i * kActSize;
        const ClassicAction& a = in.actions[i];
        put32(p, a.location), put32(p + 4, a.text), put32(p + 8, a.wav), put32(p + 12, a.time), put32(p + 16, a.player);
        put32(p + 20, a.number), put16(p + 24, a.unit), p[26] = a.type, p[27] = a.modifier, p[28] = a.flags, p[29] = a.padding;
        put16(p + 30, a.internal);
    }
    put32(t + kFlagsAt, in.flags);
    std::copy(in.players.begin(), in.players.end(), t + kFlagsAt + 4);
    t[kFlagsAt + 4 + kClassicGroups] = in.currentAction;
}

template <class T, size_t N>
int listCount(const std::array<T, N>& a) {
    for (size_t i = 0; i < N; ++i)
        if (a[i].type == 0) return int(i);
    return int(N);
}
template <class T, size_t N>
bool listAdd(std::array<T, N>& a, const T& v, int at) {
    const int n = listCount(a);
    if (n >= int(N)) return false;
    if (at < 0 || at > n) at = n;
    for (int i = n; i > at; --i) a[size_t(i)] = a[size_t(i - 1)];
    a[size_t(at)] = v;
    return true;
}
template <class T, size_t N>
void listRemove(std::array<T, N>& a, int at) {
    const int n = listCount(a);
    if (at < 0 || at >= n) return;
    for (int i = at; i + 1 < n; ++i) a[size_t(i)] = a[size_t(i + 1)];
    a[size_t(n - 1)] = T{};
}
template <class T, size_t N>
void listMove(std::array<T, N>& a, int from, int to) {
    const int n = listCount(a);
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    const T v = a[size_t(from)];
    if (from < to)
        for (int i = from; i < to; ++i) a[size_t(i)] = a[size_t(i + 1)];
    else
        for (int i = from; i > to; --i) a[size_t(i)] = a[size_t(i - 1)];
    a[size_t(to)] = v;
}

using K = ClassicKind;
using F = ClassicField;

// The parameters that come back in many entries.
constexpr ClassicParam cPlayer{K::Player, F::Player, "player"}, cCmp{K::Comparison, F::Comparison, "cmp"}, cN{K::Number, F::Amount, "n"},
    cUnit{K::Unit, F::Unit, "unit"}, cLoc{K::Location, F::Location, "location"}, cRes{K::Resource, F::Resource, "resource"},
    cScore{K::Score, F::Resource, "score"}, cSec{K::Seconds, F::Amount, "n"};
constexpr ClassicParam aPlayer{K::Player, F::Player, "player"}, aUnit{K::Unit, F::Unit, "unit"}, aLoc{K::Location, F::Location, "location"},
    aCount{K::Count, F::Modifier, "count"}, aText{K::Text, F::Text, "text"}, aSound{K::Sound, F::Wav, "sound"}, aMs{K::Milliseconds, F::Time, "ms"},
    aMod{K::Modifier, F::Modifier, "modifier"}, aN{K::Number, F::Number, "n"}, aDest{K::Location, F::Number, "dest"},
    aState{K::State, F::Modifier, "state"}, aAi{K::AiScript, F::Number, "ai"}, aResU{K::Resource, F::Unit, "resource"},
    aScoreU{K::Score, F::Unit, "score"}, aPercent{K::Percent, F::Number, "percent"};

// Flags of a new entry: as StarEdit writes them (the most common flags of each type in the map corpus).
std::vector<ClassicDef> makeConditions() {
    return {
        {1, "Countdown timer", "The countdown timer is {cmp} {n}.", {cCmp, cSec}, 0},
        {2, "Command", "{player} commands {cmp} {n} {unit}.", {cPlayer, cCmp, cN, cUnit}, 16},
        {3, "Bring", "{player} brings {cmp} {n} {unit} to {location}.", {cPlayer, cCmp, cN, cUnit, cLoc}, 16},
        {4, "Accumulate", "{player} accumulates {cmp} {n} {resource}.", {cPlayer, cCmp, cN, cRes}, 0},
        {5, "Kill", "{player} kills {cmp} {n} {unit}.", {cPlayer, cCmp, cN, cUnit}, 16},
        {6, "Command the most", "The current player commands the most {unit}.", {cUnit}, 16},
        {7, "Command the most at", "The current player commands the most {unit} at {location}.", {cUnit, cLoc}, 16},
        {8, "Most kills", "The current player has the most kills of {unit}.", {cUnit}, 16},
        {9, "Highest score", "The current player has the highest score: {score}.", {cScore}, 0},
        {10, "Most resources", "The current player has the most {resource}.", {cRes}, 0},
        {11, "Switch", "{switch} is {state}.", {{K::Switch, F::Resource, "switch"}, {K::SwitchState, F::Comparison, "state"}}, 0},
        {12, "Elapsed time", "The elapsed game time is {cmp} {n}.", {cCmp, cSec}, 0},
        {13, "Mission briefing", "Mission briefing.", {}, 0},
        {14, "Opponents", "{player} has {cmp} {n} opponents in the game.", {cPlayer, cCmp, cN}, 0},
        {15, "Deaths", "{player} has {cmp} {n} deaths of {unit}.", {cPlayer, cCmp, cN, cUnit}, 16},
        {16, "Command the least", "The current player commands the least {unit}.", {cUnit}, 16},
        {17, "Command the least at", "The current player commands the least {unit} at {location}.", {cUnit, cLoc}, 16},
        {18, "Least kills", "The current player has the least kills of {unit}.", {cUnit}, 16},
        {19, "Lowest score", "The current player has the lowest score: {score}.", {cScore}, 0},
        {20, "Least resources", "The current player has the least {resource}.", {cRes}, 0},
        {21, "Score", "{player} has a score of {cmp} {n} ({score}).", {cPlayer, cScore, cCmp, cN}, 0},
        {22, "Always", "Always.", {}, 0},
        {23, "Never", "Never.", {}, 0},
    };
}

std::vector<ClassicDef> makeActions() {
    const char* label = " Label: {text}";
    auto lb = [&](const char* s) { return std::string(s) + label; };
    static std::vector<std::string> keep;  // the leader board sentences (the defs hold their text)
    keep = {lb("Show the leader board for the most control of {unit}."),
            lb("Show the leader board for the most control of {unit} at {location}."),
            lb("Show the leader board for the most {resource}."),
            lb("Show the leader board for the most kills of {unit}."),
            lb("Show the leader board for the most points ({score})."),
            lb("Show the leader board for the player nearest to control of {n} {unit}."),
            lb("Show the leader board for the player nearest to control of {n} {unit} at {location}."),
            lb("Show the leader board for the player nearest to {n} {resource}."),
            lb("Show the leader board for the player nearest to {n} kills of {unit}."),
            lb("Show the leader board for the player nearest to {n} points ({score}).")};
    return {
        {1, "Victory", "End the game in victory for the current player.", {}, 0},
        {2, "Defeat", "End the game in defeat for the current player.", {}, 0},
        {3, "Preserve trigger", "Preserve the trigger.", {}, 0},
        {4, "Wait", "Wait {ms}.", {aMs}, 0},
        {5, "Pause game", "Pause the game.", {}, 0},
        {6, "Unpause game", "Unpause the game.", {}, 4},
        {7, "Transmission", "Send a transmission to the current player from {unit} at {location}. Play {sound}. Duration: {modifier} {duration}. Text: {text}",
         {aUnit, aLoc, aSound, aMod, {K::Milliseconds, F::Number, "duration"}, aText}, 16},
        {8, "Play WAV", "Play {sound}.", {aSound}, 4},
        {9, "Display text message", "Show this text to the current player: {text}", {aText}, 4},
        {10, "Center view", "Center the view of the current player on {location}.", {aLoc}, 0},
        {11, "Create unit with properties", "Create {count} {unit} at {location} for {player}, with the properties {props}.",
         {aCount, aUnit, aLoc, aPlayer, {K::UnitProperties, F::Number, "props"}}, 28},
        {12, "Set mission objectives", "Set the mission objectives to: {text}", {aText}, 0},
        {13, "Set switch", "{action} {switch}.", {{K::SwitchAction, F::Modifier, "action"}, {K::Switch, F::Number, "switch"}}, 0},
        {14, "Set countdown timer", "Change the countdown timer: {modifier} {n}.", {aMod, {K::Seconds, F::Time, "n"}}, 0},
        {15, "Run AI script", "Run the AI script {ai}.", {aAi}, 0},
        {16, "Run AI script at location", "Run the AI script {ai} at {location}.", {aAi, aLoc}, 0},
        {17, "Leader board (control)", keep[0].c_str(), {aUnit, aText}, 16},
        {18, "Leader board (control at location)", keep[1].c_str(), {aUnit, aLoc, aText}, 16},
        {19, "Leader board (resources)", keep[2].c_str(), {aResU, aText}, 0},
        {20, "Leader board (kills)", keep[3].c_str(), {aUnit, aText}, 0},
        {21, "Leader board (points)", keep[4].c_str(), {aScoreU, aText}, 0},
        {22, "Kill unit", "Kill all {unit} of {player}.", {aUnit, aPlayer}, 16},
        {23, "Kill unit at location", "Kill {count} {unit} of {player} at {location}.", {aCount, aUnit, aPlayer, aLoc}, 16},
        {24, "Remove unit", "Remove all {unit} of {player}.", {aUnit, aPlayer}, 20},
        {25, "Remove unit at location", "Remove {count} {unit} of {player} at {location}.", {aCount, aUnit, aPlayer, aLoc}, 20},
        {26, "Set resources", "Change the resources of {player}: {modifier} {n} {resource}.", {aPlayer, aMod, aN, aResU}, 0},
        {27, "Set score", "Change the score of {player}: {modifier} {n} points ({score}).", {aPlayer, aMod, aN, aScoreU}, 4},
        {28, "Minimap ping", "Show a minimap ping to the current player at {location}.", {aLoc}, 0},
        {29, "Talking portrait", "Show {unit} talking to the current player for {ms}.", {aUnit, aMs}, 20},
        {30, "Mute unit speech", "Mute the unit sounds for the current player.", {}, 4},
        {31, "Unmute unit speech", "Unmute the unit sounds for the current player.", {}, 4},
        {32, "Leaderboard computer players", "{state} the computer players in the leader board.", {aState}, 0},
        {33, "Leaderboard goal (control)", keep[5].c_str(), {aN, aUnit, aText}, 20},
        {34, "Leaderboard goal (control at location)", keep[6].c_str(), {aN, aUnit, aLoc, aText}, 20},
        {35, "Leaderboard goal (resources)", keep[7].c_str(), {aN, aResU, aText}, 20},
        {36, "Leaderboard goal (kills)", keep[8].c_str(), {aN, aUnit, aText}, 4},
        {37, "Leaderboard goal (points)", keep[9].c_str(), {aN, aScoreU, aText}, 4},
        {38, "Move location", "Move {dest} to the center of {unit} of {player} at {location}.", {aDest, aUnit, aPlayer, aLoc}, 16},
        {39, "Move unit", "Move {count} {unit} of {player} at {location} to {dest}.", {aCount, aUnit, aPlayer, aLoc, aDest}, 16},
        {40, "Leaderboard (greed)", "Show the greed leader board: the player nearest to {n} minerals and gas.", {aN}, 0},
        {41, "Set next scenario", "Load {text} after this game.", {aText}, 0},
        {42, "Set doodad state", "{state} the doodad state of {unit} of {player} at {location}.", {aState, aUnit, aPlayer, aLoc}, 20},
        {43, "Set invincibility", "{state} the invincibility of {unit} of {player} at {location}.", {aState, aUnit, aPlayer, aLoc}, 16},
        {44, "Create unit", "Create {count} {unit} at {location} for {player}.", {aCount, aUnit, aLoc, aPlayer}, 16},
        {45, "Set deaths", "Change the deaths of {unit} for {player}: {modifier} {n}.", {aUnit, aPlayer, aMod, aN}, 20},
        {46, "Order", "Order all {unit} of {player} at {location}: {order} to {dest}.",
         {aUnit, aPlayer, aLoc, {K::Order, F::Modifier, "order"}, aDest}, 20},
        {47, "Comment", "Comment: {text}", {aText}, 0},
        {48, "Give units to player", "Give {count} {unit} of {player} at {location} to {to}.", {aCount, aUnit, aPlayer, aLoc, {K::Player, F::Number, "to"}}, 16},
        {49, "Modify unit hit points", "Set the hit points of {count} {unit} of {player} at {location} to {percent}.", {aCount, aUnit, aPlayer, aLoc, aPercent}, 16},
        {50, "Modify unit energy", "Set the energy of {count} {unit} of {player} at {location} to {percent}.", {aCount, aUnit, aPlayer, aLoc, aPercent}, 16},
        {51, "Modify unit shield points", "Set the shields of {count} {unit} of {player} at {location} to {percent}.", {aCount, aUnit, aPlayer, aLoc, aPercent}, 16},
        {52, "Modify unit resource amount", "Set the resources of {count} resource sources of {player} at {location} to {n}.", {aCount, aPlayer, aLoc, aN}, 0},
        {53, "Modify unit hangar count", "Add at most {n} to the hangar of {count} {unit} of {player} at {location}.", {aN, aCount, aUnit, aPlayer, aLoc}, 16},
        {54, "Pause timer", "Pause the countdown timer.", {}, 4},
        {55, "Unpause timer", "Unpause the countdown timer.", {}, 20},
        {56, "Draw", "End the game in a draw for all players that are not allied.", {}, 4},
        {57, "Set alliance status", "Set {player} to {alliance} for the current player.", {aPlayer, {K::Alliance, F::Unit, "alliance"}}, 0},
        {58, "Disable debug mode", "Turn off the debug mode.", {}, 20},
        {59, "Enable debug mode", "Turn on the debug mode.", {}, 16},
    };
}

const std::vector<ClassicDef>& conditionTable() {
    static const std::vector<ClassicDef> t = makeConditions();
    return t;
}
const std::vector<ClassicDef>& actionTable() {
    static const std::vector<ClassicDef> t = makeActions();
    return t;
}

std::vector<const ClassicDef*> byName(const std::vector<ClassicDef>& t) {
    std::vector<const ClassicDef*> out;
    for (const ClassicDef& d : t) out.push_back(&d);
    std::sort(out.begin(), out.end(), [](const ClassicDef* a, const ClassicDef* b) { return std::string(a->name) < std::string(b->name); });
    return out;
}

uint32_t defaultValue(ClassicKind k) {
    switch (k) {
    case K::Player: return 13;  // current player
    case K::Comparison: return 0;
    case K::Number: return 1;
    case K::Unit: return 0;
    case K::Location: return 64;  // Anywhere
    case K::SwitchState: return 2;
    case K::SwitchAction: return 4;
    case K::Modifier: return 7;
    case K::Milliseconds: return 1000;
    case K::Seconds: return 60;
    case K::Count: return 1;
    case K::AiScript: return 0;
    case K::State: return 4;
    case K::Percent: return 100;
    default: return 0;
    }
}

std::string number(uint32_t v) { return std::to_string(v); }

}  // namespace

// --- ClassicTrigger ------------------------------------------------------------------------------------------

namespace {
// The slots of a batch follow their entries: `place` gives an entry's new place (-1: removed).
template <class Place>
void followSlots(ClassicTrigger& t, bool condition, Place place) {
    if (!t.batch) return;
    auto& s = t.batch->slots;
    for (auto it = s.begin(); it != s.end();) {
        if (it->condition != condition) {
            ++it;
            continue;
        }
        const int n = place(int(it->index));
        if (n < 0) it = s.erase(it);
        else it->index = uint8_t(n), ++it;
    }
}
}  // namespace

int ClassicTrigger::conditionCount() const { return listCount(conditions); }
int ClassicTrigger::actionCount() const { return listCount(actions); }
bool ClassicTrigger::addCondition(const ClassicCondition& c, int at) {
    const int n = conditionCount(), pos = at < 0 || at > n ? n : at;
    if (!listAdd(conditions, c, at)) return false;
    followSlots(*this, true, [pos](int i) { return i >= pos ? i + 1 : i; });
    return true;
}
bool ClassicTrigger::addAction(const ClassicAction& a, int at) {
    const int n = actionCount(), pos = at < 0 || at > n ? n : at;
    if (batch && n >= kClassicActions - 1) return false;  // the comment that names the batch takes the first place
    if (!listAdd(actions, a, at)) return false;
    followSlots(*this, false, [pos](int i) { return i >= pos ? i + 1 : i; });
    return true;
}
void ClassicTrigger::removeCondition(int i) {
    if (i < 0 || i >= conditionCount()) return;
    listRemove(conditions, i);
    followSlots(*this, true, [i](int k) { return k == i ? -1 : k > i ? k - 1 : k; });
}
void ClassicTrigger::removeAction(int i) {
    if (i < 0 || i >= actionCount()) return;
    listRemove(actions, i);
    followSlots(*this, false, [i](int k) { return k == i ? -1 : k > i ? k - 1 : k; });
}
namespace {
// The place of entry `i` after the entry at `from` moved to `to` (both places in the list).
int movedPlace(int i, int from, int to) {
    if (i == from) return to;
    if (from < to) return i > from && i <= to ? i - 1 : i;
    return i >= to && i < from ? i + 1 : i;
}
}  // namespace
void ClassicTrigger::moveCondition(int from, int to) {
    const int n = conditionCount();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    listMove(conditions, from, to);
    followSlots(*this, true, [from, to](int i) { return movedPlace(i, from, to); });
}
void ClassicTrigger::moveAction(int from, int to) {
    const int n = actionCount();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    listMove(actions, from, to);
    followSlots(*this, false, [from, to](int i) { return movedPlace(i, from, to); });
}

ClassicTrigger ClassicTrigger::make() {
    ClassicTrigger t;
    t.conditions[0] = makeClassicCondition(22);
    return t;
}

// --- TRIG ----------------------------------------------------------------------------------------------------

ClassicTriggers readClassicTriggers(const Chk& chk) {
    ClassicTriggers out;
    const Bytes b = chk.effective("TRIG");
    size_t at = 0;
    for (; at + kClassicTriggerSize <= b.size(); at += kClassicTriggerSize) out.triggers.push_back(decode(&b[at]));
    out.tail.assign(b.begin() + std::ptrdiff_t(at), b.end());
    return out;
}

Bytes encodeClassicTriggers(const ClassicTriggers& t) {
    Bytes b(t.triggers.size() * kClassicTriggerSize, 0);
    for (size_t i = 0; i < t.triggers.size(); ++i) encode(t.triggers[i], &b[i * kClassicTriggerSize]);
    b.insert(b.end(), t.tail.begin(), t.tail.end());
    return b;
}

void writeClassicTriggers(Chk& chk, const ClassicTriggers& t) {
    Bytes b = encodeClassicTriggers(t);
    if (b == chk.effective("TRIG") && (chk.has("TRIG") || b.empty())) return;
    chk.set("TRIG", std::move(b));
}

// --- Definitions ---------------------------------------------------------------------------------------------

const ClassicDef* classicConditionDef(uint8_t type) {
    const auto& t = conditionTable();
    return type >= 1 && type <= t.size() ? &t[type - 1u] : nullptr;
}
const ClassicDef* classicActionDef(uint8_t type) {
    const auto& t = actionTable();
    return type >= 1 && type <= t.size() ? &t[type - 1u] : nullptr;
}
const std::vector<const ClassicDef*>& classicConditionDefs() {
    static const std::vector<const ClassicDef*> v = byName(conditionTable());
    return v;
}
const std::vector<const ClassicDef*>& classicActionDefs() {
    static const std::vector<const ClassicDef*> v = byName(actionTable());
    return v;
}

uint32_t classicValue(const ClassicCondition& c, ClassicField f) {
    switch (f) {
    case F::Location: return c.location;
    case F::Player: return c.player;
    case F::Amount: return c.amount;
    case F::Unit: return c.unit;
    case F::Comparison: return c.comparison;
    case F::Resource: return c.resource;
    default: return 0;
    }
}
uint32_t classicValue(const ClassicAction& a, ClassicField f) {
    switch (f) {
    case F::Location: return a.location;
    case F::Player: return a.player;
    case F::Unit: return a.unit;
    case F::Text: return a.text;
    case F::Wav: return a.wav;
    case F::Time: return a.time;
    case F::Number: return a.number;
    case F::Modifier: return a.modifier;
    default: return 0;
    }
}
void setClassicValue(ClassicCondition& c, ClassicField f, uint32_t v) {
    switch (f) {
    case F::Location: c.location = v; break;
    case F::Player: c.player = v; break;
    case F::Amount: c.amount = v; break;
    case F::Unit: c.unit = uint16_t(v); break;
    case F::Comparison: c.comparison = uint8_t(v); break;
    case F::Resource: c.resource = uint8_t(v); break;
    default: break;
    }
}
void setClassicValue(ClassicAction& a, ClassicField f, uint32_t v) {
    switch (f) {
    case F::Location: a.location = v; break;
    case F::Player: a.player = v; break;
    case F::Unit: a.unit = uint16_t(v); break;
    case F::Text: a.text = v; break;
    case F::Wav: a.wav = v; break;
    case F::Time: a.time = v; break;
    case F::Number: a.number = v; break;
    case F::Modifier: a.modifier = uint8_t(v); break;
    default: break;
    }
}

ClassicCondition makeClassicCondition(uint8_t type) {
    ClassicCondition c;
    c.type = type;
    if (const ClassicDef* d = classicConditionDef(type)) {
        c.flags = d->flags;
        for (const ClassicParam& p : d->params) setClassicValue(c, p.field, defaultValue(p.kind));
    }
    return c;
}
ClassicAction makeClassicAction(uint8_t type) {
    ClassicAction a;
    a.type = type;
    if (const ClassicDef* d = classicActionDef(type)) {
        a.flags = d->flags;
        for (const ClassicParam& p : d->params) setClassicValue(a, p.field, defaultValue(p.kind));
    }
    return a;
}

namespace {
template <class T, class Make, class Def>
T retype(const T& old, uint8_t type, Make make, Def def) {
    if (old.type == type) return old;
    T n = make(type);
    const ClassicDef *from = def(old.type), *to = def(type);
    if (!from || !to) return n;
    for (const ClassicParam& p : to->params)
        for (const ClassicParam& q : from->params)
            if (p.field == q.field && p.kind == q.kind) setClassicValue(n, p.field, classicValue(old, q.field));
    n.flags = uint8_t((n.flags & ~2u) | (old.flags & 2u));  // disabled stays
    return n;
}
}  // namespace

ClassicCondition retypeClassicCondition(const ClassicCondition& c, uint8_t type) {
    return retype(c, type, makeClassicCondition, classicConditionDef);
}
ClassicAction retypeClassicAction(const ClassicAction& a, uint8_t type) { return retype(a, type, makeClassicAction, classicActionDef); }

const std::vector<ClassicChoice>& classicChoices(ClassicKind kind) {
    static const std::vector<ClassicChoice> none, cmp{{0, "at least"}, {1, "at most"}, {10, "exactly"}},
        res{{0, "minerals"}, {1, "gas"}, {2, "minerals and gas"}},
        score{{0, "total"}, {1, "units"}, {2, "buildings"}, {3, "units and buildings"}, {4, "kills"}, {5, "razings"}, {6, "kills and razings"}, {7, "custom"}},
        swState{{2, "set"}, {3, "not set"}}, swAction{{4, "Set"}, {5, "Clear"}, {6, "Toggle"}, {11, "Randomize"}},
        mod{{7, "set to"}, {8, "add"}, {9, "subtract"}}, order{{0, "move"}, {1, "patrol"}, {2, "attack"}},
        ally{{0, "enemy"}, {1, "ally"}, {2, "allied victory"}}, state{{4, "Turn on"}, {5, "Turn off"}, {6, "Toggle"}},
        players{{0, "Player 1"},       {1, "Player 2"},   {2, "Player 3"},       {3, "Player 4"},       {4, "Player 5"},     {5, "Player 6"},
                {6, "Player 7"},       {7, "Player 8"},   {8, "Player 9"},       {9, "Player 10"},      {10, "Player 11"},   {11, "Player 12"},
                {13, "Current player"}, {14, "Foes"},      {15, "Allies"},        {16, "Neutral players"}, {17, "All players"}, {18, "Force 1"},
                {19, "Force 2"},       {20, "Force 3"},   {21, "Force 4"},       {26, "Non-allied victory players"}, {12, "None"}};
    switch (kind) {
    case K::Comparison: return cmp;
    case K::Resource: return res;
    case K::Score: return score;
    case K::SwitchState: return swState;
    case K::SwitchAction: return swAction;
    case K::Modifier: return mod;
    case K::Order: return order;
    case K::Alliance: return ally;
    case K::State: return state;
    case K::Player: return players;
    default: return none;
    }
}

// --- AI scripts ----------------------------------------------------------------------------------------------

std::string classicFourCC(uint32_t id) {
    std::string s;
    for (int i = 0; i < 4; ++i) {
        const char c = char(id >> (8 * i));
        s += c >= 32 && c < 127 ? c : '?';
    }
    return s;
}

std::vector<ClassicAiScript> loadClassicAiScripts(const GameData& data) {
    std::vector<ClassicAiScript> out;
    Bytes bin, tbl;
    std::string err;
    if (!data.readFile("scripts\\aiscript.bin", bin, err) || bin.size() < 4) return out;
    std::vector<std::string> names;
    if (data.readFile("rez\\stat_txt.tbl", tbl, err) && tbl.size() >= 2) {
        const size_t n = get16(tbl.data());
        for (size_t i = 0; i < n && 2 + 2 * i + 2 <= tbl.size(); ++i) {
            size_t at = get16(&tbl[2 + 2 * i]), end = at;
            while (end < tbl.size() && tbl[end]) ++end;
            names.emplace_back(at < tbl.size() ? std::string(tbl.begin() + std::ptrdiff_t(at), tbl.begin() + std::ptrdiff_t(end)) : std::string());
        }
    }
    // The header points to the table: id, offset, stat_txt string (1-based), flags (1: needs a location, 2: not shown
    // in StarEdit, 4: Brood War); an id of 0 ends it.
    for (size_t at = get32(bin.data()); at + 16 <= bin.size(); at += 16) {
        ClassicAiScript s;
        s.id = get32(&bin[at]);
        if (!s.id) break;
        const uint32_t str = get32(&bin[at + 8]), flags = get32(&bin[at + 12]);
        if (flags & 2) continue;
        s.location = flags & 1, s.broodWar = flags & 4;
        s.name = str >= 1 && str <= names.size() ? names[str - 1] : classicFourCC(s.id);
        out.push_back(std::move(s));
    }
    return out;
}

// --- Text ----------------------------------------------------------------------------------------------------

std::string classicPlayerName(uint32_t g, const ClassicNames& names) {
    if (g >= 18 && g <= 21 && names.force) {
        const std::string f = names.force(g - 18);
        if (!f.empty()) return f;
    }
    if (g >= 22 && g <= 25) return "Unused " + number(g - 21);
    for (const ClassicChoice& c : classicChoices(K::Player))
        if (c.value == g) return c.name;
    return "Player #" + number(g);
}

std::string classicValueText(ClassicKind kind, uint32_t v, const ClassicNames& names) {
    auto choice = [&]() -> std::string {
        for (const ClassicChoice& c : classicChoices(kind))
            if (c.value == v) return c.name;
        return "(" + number(v) + ")";
    };
    switch (kind) {
    case K::Player: return classicPlayerName(v, names);
    case K::Unit:
        if (v == 228) return "no unit";
        if (v == 229) return "any unit";
        if (v == 230) return "men";
        if (v == 231) return "buildings";
        if (v == 232) return "factories";
        if (v < 228 && names.unit) return names.unit(v);
        return "unit #" + number(v);
    case K::Location:
        if (v == 0) return "no location";
        if (names.location) {
            const std::string n = names.location(v);
            if (!n.empty()) return "'" + n + "'";
        }
        return "Location " + number(v);
    case K::Switch:
        if (names.switchName) {
            const std::string n = names.switchName(v);
            if (!n.empty()) return "'" + n + "'";
        }
        return "Switch " + number(v + 1);
    case K::Text:
    case K::Sound: {
        if (!v) return kind == K::Sound ? "no sound" : "\"\"";
        const std::string s = names.string ? names.string(v) : std::string();
        return kind == K::Sound ? "'" + s + "'" : "\"" + s + "\"";
    }
    case K::Milliseconds: return number(v) + " ms";
    case K::Seconds: return number(v) + (v == 1 ? " second" : " seconds");
    case K::Count: return v ? number(v) : "all";
    case K::Percent: return number(v) + "%";
    case K::UnitProperties: return number(v);
    case K::AiScript:
        if (names.ai)
            for (const ClassicAiScript& s : *names.ai)
                if (s.id == v) return "'" + s.name + "'";
        return "'" + classicFourCC(v) + "'";
    case K::Number: return number(v);
    default: return choice();
    }
}

namespace {
template <class T>
std::string sentence(const T& e, const ClassicDef* d, const ClassicNames& names, const char* what, const ClassicBatch* batch, int index) {
    if (!d) return std::string(what) + " " + number(e.type) + " (the game does not have it; kept as it is)";
    std::string out, s = d->sentence;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '{') {
            out += s[i];
            continue;
        }
        const size_t end = s.find('}', i);
        const std::string key = s.substr(i + 1, end - i - 1);
        for (const ClassicParam& p : d->params)
            if (key == p.name) {
                const bool variable = batch && index >= 0 && classicBatchHasSlot(*batch, std::is_same_v<T, ClassicCondition>, index, p.field);
                out += variable ? classicBatchVariableText(*batch) : classicValueText(p.kind, classicValue(e, p.field), names);
            }
        i = end;
    }
    // A sentence that starts with a value: a capital letter.
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = char(out[0] - 'a' + 'A');
    return out;
}
}  // namespace

std::string classicConditionText(const ClassicCondition& c, const ClassicNames& names, const ClassicBatch* batch, int index) {
    return sentence(c, classicConditionDef(c.type), names, "Condition", batch, index);
}
std::string classicActionText(const ClassicAction& a, const ClassicNames& names, const ClassicBatch* batch, int index) {
    return sentence(a, classicActionDef(a.type), names, "Action", batch, index);
}

bool classicEud(const ClassicCondition& c) {
    const ClassicDef* d = classicConditionDef(c.type);
    if (!d || c.internal == kMaskFlag) return true;
    for (const ClassicParam& p : d->params) {
        const uint32_t v = classicValue(c, p.field);
        if ((p.kind == K::Player && v > 26) || (p.kind == K::Unit && v > 232)) return true;
    }
    return false;
}
bool classicEud(const ClassicAction& a) {
    const ClassicDef* d = classicActionDef(a.type);
    if (!d || a.internal == kMaskFlag) return true;
    for (const ClassicParam& p : d->params) {
        const uint32_t v = classicValue(a, p.field);
        if ((p.kind == K::Player && v > 26) || (p.kind == K::Unit && v > 232)) return true;
    }
    return false;
}

uint32_t classicStringId(StringTable& strings, const std::string& text) {
    if (text.empty()) return 0;
    for (uint32_t i = 1; i <= strings.size(); ++i)
        if (strings.get(i) == text) return i;
    return strings.add(text);
}

// --- Batches (R23) -------------------------------------------------------------------------------------------------
//
// The comment that names a batch (the template's first action, a Comment):
//   StarDraft batch of 3 triggers: name=unit; kind=unit; values=0 1 36; slots=a2.unit
// The slots are 1-based places in the template's list, without the comment: c = condition, a = action.

namespace {
constexpr const char* kBatchPrefix = "StarDraft batch of ";
constexpr uint8_t kCommentType = 47;
constexpr size_t kBatchNameMax = 24;

const char* batchKindName(ClassicBatchKind k) {
    switch (k) {
    case ClassicBatchKind::Player: return "player";
    case ClassicBatchKind::Location: return "location";
    default: return "unit";
    }
}
const char* batchFieldName(ClassicField f) {
    switch (f) {
    case F::Player: return "player";
    case F::Location: return "location";
    case F::Number: return "number";  // a second player (Give units to player: to) or a second location (Move: dest)
    default: return "unit";
    }
}
ClassicField batchFieldOf(const std::string& s) {
    return s == "player" ? F::Player : s == "location" ? F::Location : s == "number" ? F::Number : F::Unit;
}

std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out(1);
    for (char c : s) {
        if (c == sep) out.emplace_back();
        else out.back() += c;
    }
    return out;
}
std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
// A decimal number of the whole text; false when it has no digits or other characters.
bool parseNumber(const std::string& s, uint64_t& out) {
    if (s.empty() || s.size() > 18) return false;
    out = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        out = out * 10 + uint64_t(c - '0');
    }
    return true;
}

std::string batchHeader(const ClassicBatch& b) {
    std::string s = kBatchPrefix + std::to_string(b.values.size()) + (b.values.size() == 1 ? " trigger" : " triggers") + ": name=" + b.name +
                    "; kind=" + batchKindName(b.kind) + "; values=";
    for (size_t i = 0; i < b.values.size(); ++i) s += (i ? " " : "") + std::to_string(b.values[i]);
    s += "; slots=";
    for (size_t i = 0; i < b.slots.size(); ++i) {
        const ClassicBatchSlot& x = b.slots[i];
        s += (i ? " " : "") + std::string(x.condition ? "c" : "a") + std::to_string(x.index + 1) + "." + batchFieldName(x.field);
    }
    return s;
}

// The batch a comment names; false when the text is not one (or does not agree with its count).
bool parseBatchHeader(const std::string& text, ClassicBatch& out) {
    const size_t plen = std::strlen(kBatchPrefix);
    if (text.compare(0, plen, kBatchPrefix) != 0) return false;
    const size_t colon = text.find(": ", plen);
    uint64_t count = 0;
    if (colon == std::string::npos || !parseNumber(text.substr(plen, text.find(' ', plen) - plen), count)) return false;
    ClassicBatch b;
    b.values.clear();
    bool haveName = false, haveKind = false;
    for (const std::string& part : splitOn(text.substr(colon + 2), ';')) {
        const std::string tok = trimmed(part);
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) return false;
        const std::string key = tok.substr(0, eq), val = tok.substr(eq + 1);
        if (key == "name") {
            b.name = val, haveName = true;
        } else if (key == "kind") {
            if (val == "unit") b.kind = ClassicBatchKind::Unit;
            else if (val == "player") b.kind = ClassicBatchKind::Player;
            else if (val == "location") b.kind = ClassicBatchKind::Location;
            else return false;
            haveKind = true;
        } else if (key == "values") {
            for (const std::string& v : splitOn(val, ' ')) {
                uint64_t n = 0;
                if (v.empty()) continue;
                if (!parseNumber(v, n) || n > 0xFFFFFFFFull) return false;
                b.values.push_back(uint32_t(n));
            }
        } else if (key == "slots") {
            for (const std::string& v : splitOn(val, ' ')) {
                if (v.empty()) continue;
                const size_t dot = v.find('.');
                uint64_t place = 0;
                if (dot == std::string::npos || dot < 2 || (v[0] != 'c' && v[0] != 'a') || !parseNumber(v.substr(1, dot - 1), place) || place > 255) return false;
                ClassicBatchSlot x;
                x.condition = v[0] == 'c', x.index = uint8_t(place - 1), x.field = batchFieldOf(v.substr(dot + 1));
                b.slots.push_back(x);
            }
        }
    }
    if (!haveName || !haveKind || b.values.empty() || b.values.size() != count) return false;
    b.name = classicBatchName(b.name, b.kind);
    out = std::move(b);
    return true;
}

// The comment of a batch's template, one trigger for each value (the same order as the working copy's values).
ClassicTrigger batchTrigger(const ClassicTrigger& body, const ClassicBatch& b, uint32_t value) {
    ClassicTrigger g = body;
    g.flags &= 4u;  // preserve trigger stays; the others are the game's
    g.currentAction = 0;
    for (const ClassicBatchSlot& x : b.slots) {
        if (x.condition) {
            if (x.index < g.conditionCount()) setClassicValue(g.conditions[x.index], x.field, value);
        } else if (x.index < g.actionCount()) {
            setClassicValue(g.actions[x.index], x.field, value);
        }
    }
    return g;
}
}  // namespace

ClassicKind classicBatchValueKind(ClassicBatchKind kind) {
    return kind == ClassicBatchKind::Player ? K::Player : kind == ClassicBatchKind::Location ? K::Location : K::Unit;
}

std::string classicBatchName(const std::string& name, ClassicBatchKind kind) {
    std::string out;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_';
        if (ok && out.size() < kBatchNameMax) out += c;
    }
    out = trimmed(out);
    return out.empty() ? batchKindName(kind) : out;
}

bool classicBatchHasSlot(const ClassicBatch& b, bool condition, int index, ClassicField field) {
    return std::any_of(b.slots.begin(), b.slots.end(), [&](const ClassicBatchSlot& x) {
        return x.condition == condition && int(x.index) == index && x.field == field;
    });
}

void classicBatchSetSlot(ClassicBatch& b, bool condition, int index, ClassicField field, bool on) {
    auto same = [&](const ClassicBatchSlot& x) { return x.condition == condition && int(x.index) == index && x.field == field; };
    if (!on) {
        b.slots.erase(std::remove_if(b.slots.begin(), b.slots.end(), same), b.slots.end());
        return;
    }
    if (index < 0 || index > 255 || std::any_of(b.slots.begin(), b.slots.end(), same)) return;
    ClassicBatchSlot x;
    x.condition = condition, x.index = uint8_t(index), x.field = field;
    b.slots.push_back(x);
}

void classicBatchPrune(ClassicTrigger& t) {
    if (!t.batch) return;
    const ClassicKind want = classicBatchValueKind(t.batch->kind);
    auto fits = [&](const ClassicBatchSlot& x) {
        const ClassicDef* d = nullptr;
        if (x.condition && x.index < t.conditionCount()) d = classicConditionDef(t.conditions[x.index].type);
        else if (!x.condition && x.index < t.actionCount()) d = classicActionDef(t.actions[x.index].type);
        if (!d) return false;
        return std::any_of(d->params.begin(), d->params.end(), [&](const ClassicParam& p) { return p.field == x.field && p.kind == want; });
    };
    auto& s = t.batch->slots;
    s.erase(std::remove_if(s.begin(), s.end(), [&](const ClassicBatchSlot& x) { return !fits(x); }), s.end());
}

std::string classicBatchVariableText(const ClassicBatch& b) { return "variable \"" + b.name + "\""; }

ClassicTriggers classicCollapseBatches(const ClassicTriggers& in, const StringTable& strings) {
    ClassicTriggers out;
    out.tail = in.tail;
    const auto& v = in.triggers;
    for (size_t i = 0; i < v.size();) {
        ClassicBatch b;
        const bool named = v[i].actionCount() > 0 && v[i].actions[0].type == kCommentType && parseBatchHeader(strings.get(v[i].actions[0].text), b);
        if (named && i + 1 + b.values.size() <= v.size()) {
            const size_t generated = b.values.size();
            ClassicTrigger t = v[i];
            t.removeAction(0);  // the comment is not part of the template
            t.flags &= ~8u;     // disabled: the expanded template gets it again
            t.batch = std::move(b);
            classicBatchPrune(t);
            // Only the triggers that the batch makes are taken: each one must be the copy the batch gives. Any other
            // trigger after the template (or a copy that another editor changed) is kept as it is.
            ClassicTrigger body = t;
            body.batch.reset();
            bool same = true;
            for (size_t k = 0; k < generated && same; ++k) same = v[i + 1 + k] == batchTrigger(body, *t.batch, t.batch->values[k]);
            if (same) {
                out.triggers.push_back(std::move(t));
                i += 1 + generated;  // the triggers that the batch made are not in the working copy
                continue;
            }
        }
        out.triggers.push_back(v[i]);
        ++i;
    }
    return out;
}

ClassicTriggers classicExpandBatches(const ClassicTriggers& in, StringTable& strings) {
    ClassicTriggers out;
    out.tail = in.tail;
    for (const ClassicTrigger& t : in.triggers) {
        if (!t.batch) {
            out.triggers.push_back(t);
            continue;
        }
        const ClassicBatch b = *t.batch;
        ClassicTrigger body = t;
        body.batch.reset();
        if (b.values.empty() || body.actionCount() >= kClassicActions) {  // nothing to make: the template as a trigger
            body.flags &= ~8u;
            out.triggers.push_back(body);
            continue;
        }
        ClassicTrigger head = body;  // the template: off, its comment first
        head.flags |= 8u;
        head.currentAction = 0;
        ClassicAction comment = makeClassicAction(kCommentType);
        comment.text = classicStringId(strings, batchHeader(b));
        head.addAction(comment, 0);
        out.triggers.push_back(std::move(head));
        for (uint32_t value : b.values) out.triggers.push_back(batchTrigger(body, b, value));
    }
    // The comments of batches that are gone (their values or variables changed) go blank.
    std::set<uint32_t> used;
    for (const ClassicTrigger& t : out.triggers)
        for (int i = 0; i < t.actionCount(); ++i) used.insert(t.actions[size_t(i)].text);
    for (uint32_t id = 1; id <= strings.size(); ++id)
        if (!used.count(id) && strings.get(id).compare(0, std::strlen(kBatchPrefix), kBatchPrefix) == 0) strings.set(id, "");
    return out;
}

}  // namespace stardraft
