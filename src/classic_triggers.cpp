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
bool sameBatch(const std::shared_ptr<const ClassicBatch>& a, const std::shared_ptr<const ClassicBatch>& b) {
    if (!a || !b) return !a && !b;
    return a == b || *a == *b;
}
}  // namespace

bool operator==(const ClassicAction& a, const ClassicAction& b) {
    return a.location == b.location && a.text == b.text && a.wav == b.wav && a.time == b.time && a.player == b.player &&
           a.number == b.number && a.unit == b.unit && a.type == b.type && a.modifier == b.modifier && a.flags == b.flags &&
           a.padding == b.padding && a.internal == b.internal && a.depth == b.depth && a.var == b.var && sameBatch(a.batch, b.batch);
}

ClassicField classicVarField(int k) {
    switch (k) {
    case 0: return F::Location;
    case 1: return F::Player;
    case 2: return F::Unit;
    default: return F::Number;
    }
}

bool classicVarAllowed(const ClassicAction& a, int k, ClassicBatchKind kind) {
    if (k < 0 || k >= kClassicVarFields) return false;
    const ClassicDef* d = classicActionDef(a.type);
    if (!d) return false;
    const ClassicField f = classicVarField(k);
    const ClassicKind want = classicBatchValueKind(kind);
    return std::any_of(d->params.begin(), d->params.end(), [&](const ClassicParam& p) { return p.field == f && p.kind == want; });
}

int ClassicTrigger::conditionCount() const { return listCount(conditions); }
int ClassicTrigger::actionCount() const { return listCount(actions); }

int ClassicTrigger::blockEnd(int row) const {
    const int n = actionCount();
    if (row < 0 || row >= n) return row;
    int e = row + 1;
    if (actions[size_t(row)].type != kClassicBlockType) return e;
    const int d = actions[size_t(row)].depth;
    while (e < n && actions[size_t(e)].depth > d) ++e;
    return e;
}

bool ClassicTrigger::addCondition(const ClassicCondition& c, int at) { return listAdd(conditions, c, at); }

bool ClassicTrigger::addAction(const ClassicAction& a, int at) {
    if (!listAdd(actions, a, at)) return false;
    pruneBlocks();
    return true;
}

void ClassicTrigger::removeCondition(int i) { listRemove(conditions, i); }

void ClassicTrigger::removeAction(int i) {
    const int n = actionCount();
    if (i < 0 || i >= n) return;
    const int count = blockEnd(i) - i;  // a block takes its children with it
    for (int k = 0; k < count; ++k) listRemove(actions, i);
    pruneBlocks();
}

void ClassicTrigger::moveCondition(int from, int to) { listMove(conditions, from, to); }

// Moves the row at `from` with its children to place `to` of the list without them (as moveCondition does).
void ClassicTrigger::moveAction(int from, int to) {
    const int n = actionCount();
    if (from < 0 || from >= n) return;
    const int e = blockEnd(from);
    const int rest = n - (e - from);
    if (to < 0 || to > rest) return;
    std::vector<ClassicAction> part(actions.begin() + from, actions.begin() + e), left;
    for (int i = 0; i < n; ++i)
        if (i < from || i >= e) left.push_back(actions[size_t(i)]);
    // A row that goes in front of a deeper row would take that row out of its block: the move is refused.
    if (to < int(left.size()) && left[size_t(to)].depth > part[0].depth) return;
    left.insert(left.begin() + to, part.begin(), part.end());
    for (size_t i = 0; i < kClassicActions; ++i) actions[i] = i < left.size() ? left[i] : ClassicAction{};
    pruneBlocks();
}

namespace {
// Moves the rows [from, to) by `delta` levels. A variable that holds a block at depth `inside` or deeper (inside the
// rows) follows that block; one that holds the block at depth `gone` is cleared.
void shiftRows(ClassicTrigger& t, int from, int to, int delta, int inside, int gone) {
    for (int k = from; k < to; ++k) {
        ClassicAction& a = t.actions[size_t(k)];
        a.depth = uint8_t(a.depth + delta);
        for (uint8_t& v : a.var) {
            if (!v) continue;
            const int target = v - 1;
            if (target == gone) v = 0;
            else if (target >= inside) v = uint8_t(v + delta);
        }
    }
}
}  // namespace

// The row goes into the block that is before it (as its last child): the block must be the previous sibling, so
// that the rows between them are its children. Its children go one level deeper with it.
bool ClassicTrigger::indentAction(int i) {
    const int n = actionCount();
    if (i <= 0 || i >= n) return false;
    const int d = actions[size_t(i)].depth;
    int j = i - 1;
    while (j >= 0 && actions[size_t(j)].depth > d) --j;
    if (j < 0 || actions[size_t(j)].depth != d || actions[size_t(j)].type != kClassicBlockType) return false;
    shiftRows(*this, i, blockEnd(i), 1, d, -1);
    pruneBlocks();
    return true;
}

// The row (with its children) goes after its block, one level out.
bool ClassicTrigger::outdentAction(int i) {
    const int n = actionCount();
    if (i < 0 || i >= n || actions[size_t(i)].depth == 0) return false;
    const int d = actions[size_t(i)].depth;
    int p = i - 1;
    while (p >= 0 && actions[size_t(p)].depth > d - 1) --p;
    if (p < 0 || actions[size_t(p)].depth != d - 1 || actions[size_t(p)].type != kClassicBlockType) return false;
    const int e = blockEnd(i), pe = blockEnd(p);  // pe: the block's end, with the row in the list
    shiftRows(*this, i, e, -1, d, d - 1);
    std::vector<ClassicAction> part(actions.begin() + i, actions.begin() + e);
    std::vector<ClassicAction> left;
    for (int k = 0; k < n; ++k)
        if (k < i || k >= e) left.push_back(actions[size_t(k)]);
    left.insert(left.begin() + (pe - (e - i)), part.begin(), part.end());
    for (size_t k = 0; k < kClassicActions; ++k) actions[k] = k < left.size() ? left[k] : ClassicAction{};
    pruneBlocks();
    return true;
}

bool ClassicTrigger::unwrapAction(int i) {
    const int n = actionCount();
    if (i < 0 || i >= n || actions[size_t(i)].type != kClassicBlockType) return false;
    const int e = blockEnd(i), d = actions[size_t(i)].depth;
    shiftRows(*this, i + 1, e, -1, d + 1, d);
    listRemove(actions, i);
    pruneBlocks();
    return true;
}

std::vector<ClassicAction> ClassicTrigger::subtreeRows(int row) const {
    std::vector<ClassicAction> out;
    if (row < 0 || row >= actionCount()) return out;
    for (int k = row; k < blockEnd(row); ++k) out.push_back(actions[size_t(k)]);
    return out;
}

bool ClassicTrigger::pasteRows(const std::vector<ClassicAction>& rows, int at, int depth) {
    const int n = actionCount();
    if (rows.empty() || at < 0 || at > n || n + int(rows.size()) > kClassicActions) return false;
    if (at < n && actions[size_t(at)].depth > depth) return false;  // that row would leave its block
    // The blocks open before `at`, by depth: open[j] is the block at depth j.
    std::vector<int> open;
    for (int i = 0; i < at; ++i) {
        const ClassicAction& a = actions[size_t(i)];
        open.resize(std::min(open.size(), size_t(a.depth)));
        if (a.type == kClassicBlockType) open.push_back(i);
    }
    if (depth < 0 || size_t(depth) > open.size()) return false;
    const int r0 = rows[0].depth;  // the depth of the subtree's root in its list
    std::vector<ClassicAction> add;
    for (ClassicAction a : rows) {
        a.depth = uint8_t(a.depth - r0 + depth);
        for (int k = 0; k < kClassicVarFields; ++k) {
            if (!a.var[size_t(k)]) continue;
            const int target = a.var[size_t(k)] - 1;
            if (target >= r0) {  // a block inside the rows: it moves with them
                a.var[size_t(k)] = uint8_t(target - r0 + depth + 1);
                continue;
            }
            // A block outside the rows: the deepest block above `at` that holds a variable of this kind.
            uint8_t found = 0;
            for (int j = depth - 1; j >= 0 && !found; --j) {
                const ClassicAction& b = actions[size_t(open[size_t(j)])];
                if (b.batch && classicVarAllowed(a, k, b.batch->kind)) found = uint8_t(j + 1);
            }
            a.var[size_t(k)] = found;
        }
        add.push_back(a);
    }
    std::vector<ClassicAction> left(actions.begin(), actions.begin() + n);
    left.insert(left.begin() + at, add.begin(), add.end());
    for (size_t i = 0; i < kClassicActions; ++i) actions[i] = i < left.size() ? left[i] : ClassicAction{};
    pruneBlocks();
    return true;
}

// Depths that the rows cannot have are fixed, and a variable that no enclosing block of its kind holds is removed.
void ClassicTrigger::pruneBlocks() {
    std::vector<int> open;  // open[k]: the block row at depth k that holds the rows at depth k + 1
    const int n = actionCount();
    for (int i = 0; i < n; ++i) {
        ClassicAction& a = actions[size_t(i)];
        const int d = std::min(int(a.depth), int(open.size()));
        a.depth = uint8_t(d);
        open.resize(size_t(d));
        if (a.type == kClassicBlockType) {
            if (!a.batch) a.batch = std::make_shared<ClassicBatch>();
            open.push_back(i);
            continue;
        }
        for (int k = 0; k < kClassicVarFields; ++k) {
            if (!a.var[size_t(k)]) continue;
            const int want = a.var[size_t(k)] - 1;
            const bool ok = want < d && size_t(want) < open.size() && actions[size_t(open[size_t(want)])].batch &&
                            classicVarAllowed(a, k, actions[size_t(open[size_t(want)])].batch->kind);
            if (!ok) a.var[size_t(k)] = 0;
            else if (classicVarField(k) == F::Unit) a.unit = 0;  // a variable's field holds no value (the file holds the placeholder)
            else setClassicValue(a, classicVarField(k), 0);
        }
    }
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
// `scope[d]`: the block at depth d that the rows hold (a row's variable is the block at its depth).
std::string sentence(const ClassicCondition* c, const ClassicAction* a, const ClassicDef* d, const ClassicNames& names,
                     const std::vector<const ClassicBatch*>* scope) {
    const char* what = c ? "Condition" : "Action";
    const uint8_t type = c ? c->type : a->type;
    if (!d) return std::string(what) + " " + number(type) + " (the game does not have it; kept as it is)";
    std::string out, s = d->sentence;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '{') {
            out += s[i];
            continue;
        }
        const size_t end = s.find('}', i);
        const std::string key = s.substr(i + 1, end - i - 1);
        for (const ClassicParam& p : d->params) {
            if (key != p.name) continue;
            const uint32_t v = c ? classicValue(*c, p.field) : classicValue(*a, p.field);
            int k = -1;
            for (int q = 0; a && q < kClassicVarFields; ++q)
                if (classicVarField(q) == p.field) k = q;
            const bool bound = a && k >= 0 && a->var[size_t(k)] && scope && size_t(a->var[size_t(k)] - 1) < scope->size() &&
                               (*scope)[size_t(a->var[size_t(k)] - 1)];
            out += bound ? classicBatchVariableText(*(*scope)[size_t(a->var[size_t(k)] - 1)]) : classicValueText(p.kind, v, names);
        }
        i = end;
    }
    // A sentence that starts with a value: a capital letter.
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = char(out[0] - 'a' + 'A');
    return out;
}
}  // namespace

std::string classicConditionText(const ClassicCondition& c, const ClassicNames& names) {
    return sentence(&c, nullptr, classicConditionDef(c.type), names, nullptr);
}
std::string classicActionText(const ClassicAction& a, const ClassicNames& names, const std::vector<const ClassicBatch*>* scope) {
    return sentence(nullptr, &a, classicActionDef(a.type), names, scope);
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
// A batch is a block in an action list: "For each <variable> in <values>", with child actions. Blocks nest. The working
// copy keeps a block as one row (type kClassicBlockType, with its variable and values) followed by its children, each
// one row deeper (ClassicAction::depth). A child's field of the variable's kind holds the variable (ClassicAction::var).
//
// The file (what the game and the other editors read) has, for each top-level block, a frame of Comment actions:
//   B  begin: player = the template's slots (T), time = the expansion's slots (X), number = the header's bytes (L).
//   D  data, one for each 14 bytes of the header: player, time, number, unit (the bytes in this order).
//   template: the children in template form, each disabled (flag 2). A variable's field holds a placeholder: 0x80000000
//      or 0x8000 (unit) plus the block's depth. A nested block's frame is in the template, without its expansion.
//   expansion: X slots. The children once for each value, depth-first, with the values in their fields. Enabled.
//   E  end.
// Every marker is a Comment with the text of the shared string "Batch Trigger marker - do not remove" (the map's
// string id, added only while a batch exists), flag 2 (disabled), and its numeric fields as above. The header holds
// version, kind, name, the values (u32 each) and the places of the disabled children of this block.
//
// A trigger with more than 64 slots (markers, templates and expansions) is split into parts. Each later part has the
// condition "Switch n is set" (the same players and flags) and starts with "Set switch n: Clear"; each part before the
// last ends with "Set switch n: Set". The switch n is a switch that no trigger of the map uses. The game runs the
// parts in order, and a later part waits for the one before it. The list shows one trigger.
//
// On a read, a group of triggers or a single trigger is taken as a working copy only when the working copy makes
// exactly the same triggers again (the same bytes). Otherwise the triggers stay as they are.

namespace {
constexpr uint32_t kMarkerMagic = 0x31424453;  // "SDB1" (location)
constexpr uint8_t kMarkBegin = 'B', kMarkData = 'D', kMarkEnd = 'E';
constexpr uint8_t kCommentType = 47, kSwitchSetType = 11, kSetSwitchType = 13;
constexpr size_t kDataBytes = 14;
constexpr const char* kMarkerText = "Batch Trigger marker - do not remove";
constexpr size_t kBatchNameMax = 24;
constexpr uint8_t kHeaderVersion = 1;


std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// The placeholder of a variable in a template row, and what it stands for.
uint32_t placeholder(int k, int depth) { return k == 2 ? (0x8000u | uint32_t(depth)) : (0x80000000u | uint32_t(depth)); }

ClassicAction marker(uint8_t kind, uint32_t text) {
    ClassicAction a;
    a.type = kCommentType;
    a.flags = 2;
    a.location = kMarkerMagic;
    a.modifier = kind;
    a.text = text;
    return a;
}
bool isMarker(const ClassicAction& a, uint32_t text, uint8_t kind) {
    return a.type == kCommentType && a.location == kMarkerMagic && a.modifier == kind && a.text == text;
}

// The header: version, kind, name, the values (u32), the disabled children (their places, u8).
Bytes encodeHeader(const ClassicBatch& b, const std::vector<uint8_t>& disabled) {
    Bytes out;
    out.push_back(kHeaderVersion);
    out.push_back(uint8_t(b.kind));
    out.push_back(uint8_t(b.name.size()));
    out.insert(out.end(), b.name.begin(), b.name.end());
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(uint32_t(b.values.size()) >> (8 * i)));
    for (uint32_t v : b.values)
        for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
    out.push_back(uint8_t(disabled.size()));
    out.insert(out.end(), disabled.begin(), disabled.end());
    return out;
}

struct Header {
    ClassicBatch batch;
    std::vector<uint8_t> disabled;
};
bool decodeHeader(const Bytes& b, Header& h) {
    size_t p = 0;
    auto byte = [&](uint8_t& v) {
        if (p >= b.size()) return false;
        v = b[p++];
        return true;
    };
    uint8_t version = 0, kind = 0, len = 0;
    if (!byte(version) || version != kHeaderVersion || !byte(kind) || kind >= classicBlockKinds().size() || !byte(len) || p + len > b.size()) return false;
    h.batch.kind = ClassicBatchKind(kind);
    h.batch.name.assign(b.begin() + std::ptrdiff_t(p), b.begin() + std::ptrdiff_t(p + len));
    p += len;
    if (p + 4 > b.size()) return false;
    uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= uint32_t(b[p++]) << (8 * i);
    if (n > 255 || p + 4ull * n > b.size()) return false;
    h.batch.values.clear();
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= uint32_t(b[p++]) << (8 * k);
        h.batch.values.push_back(v);
    }
    uint8_t dn = 0;
    if (!byte(dn) || p + dn != b.size()) return false;
    h.disabled.assign(b.begin() + std::ptrdiff_t(p), b.end());
    return true;
}

enum class Mode { Top, Template, Expansion };
using Scope = std::vector<std::pair<int, uint32_t>>;  // the bound variables: (depth, value)

// The slots of a block's frame for the row at `i` (its children are i + 1 to `end`).
void emitRange(const ClassicTrigger& t, int from, int to, Mode mode, Scope& scope, std::vector<ClassicAction>& out,
               std::vector<uint8_t>* disabled, uint32_t text);

void emitFrame(const ClassicTrigger& t, int i, int end, bool withExpansion, Scope& scope, std::vector<ClassicAction>& out, uint32_t text) {
    const ClassicAction& blk = t.actions[size_t(i)];
    std::vector<uint8_t> dis;
    std::vector<ClassicAction> tpl, exp;
    Scope none;
    emitRange(t, i + 1, end, Mode::Template, none, tpl, &dis, text);
    if (withExpansion) {
        for (uint32_t v : blk.batch->values) {
            scope.emplace_back(blk.depth, v);
            emitRange(t, i + 1, end, Mode::Expansion, scope, exp, nullptr, text);
            scope.pop_back();
        }
    }
    const Bytes header = encodeHeader(*blk.batch, dis);
    ClassicAction b = marker(kMarkBegin, text);
    b.player = uint32_t(tpl.size()), b.time = uint32_t(exp.size()), b.number = uint32_t(header.size());
    out.push_back(b);
    for (size_t at = 0; at < header.size(); at += kDataBytes) {
        uint8_t raw[kDataBytes] = {};
        std::copy(header.begin() + std::ptrdiff_t(at), header.begin() + std::ptrdiff_t(std::min(header.size(), at + kDataBytes)), raw);
        ClassicAction d = marker(kMarkData, text);
        auto u32 = [&](size_t k) { return uint32_t(raw[k]) | uint32_t(raw[k + 1]) << 8 | uint32_t(raw[k + 2]) << 16 | uint32_t(raw[k + 3]) << 24; };
        d.player = u32(0), d.time = u32(4), d.number = u32(8), d.unit = uint16_t(raw[12] | raw[13] << 8);
        out.push_back(d);
    }
    out.insert(out.end(), tpl.begin(), tpl.end());
    out.insert(out.end(), exp.begin(), exp.end());
    out.push_back(marker(kMarkEnd, text));
}

void emitRange(const ClassicTrigger& t, int from, int to, Mode mode, Scope& scope, std::vector<ClassicAction>& out,
               std::vector<uint8_t>* disabled, uint32_t text) {
    int child = 0;
    for (int i = from; i < to;) {
        const ClassicAction& a = t.actions[size_t(i)];
        const int e = t.blockEnd(i);
        if (a.type == kClassicBlockType) {
            if (mode == Mode::Top) emitFrame(t, i, e, true, scope, out, text);
            else if (mode == Mode::Template) emitFrame(t, i, e, false, scope, out, text);
            else {
                for (uint32_t v : a.batch->values) {
                    scope.emplace_back(a.depth, v);
                    emitRange(t, i + 1, e, Mode::Expansion, scope, out, nullptr, text);
                    scope.pop_back();
                }
            }
        } else {
            ClassicAction r = a;
            r.depth = 0, r.var = {}, r.batch = nullptr;
            for (int k = 0; k < kClassicVarFields; ++k) {
                if (!a.var[size_t(k)]) continue;
                const int depth = a.var[size_t(k)] - 1;
                uint32_t value = placeholder(k, depth);
                if (mode == Mode::Expansion)
                    for (const auto& [d, v] : scope)
                        if (d == depth) value = v;  // the innermost binding of the depth wins
                const ClassicField f = classicVarField(k);
                if (f == F::Unit) r.unit = uint16_t(value);
                else setClassicValue(r, f, value);
            }
            if (mode == Mode::Template) {
                if (a.flags & 2) {
                    if (disabled) disabled->push_back(uint8_t(child));
                }
                r.flags = uint8_t(r.flags | 2);
            }
            out.push_back(r);
        }
        ++child;
        i = e;
    }
}

// The file's slots of the trigger's rows (the markers, the templates and the expansions).
std::vector<ClassicAction> emitTrigger(const ClassicTrigger& t, uint32_t text) {
    std::vector<ClassicAction> out;
    Scope scope;
    emitRange(t, 0, t.actionCount(), Mode::Top, scope, out, nullptr, text);
    return out;
}

// Reads a frame or a plain row from the slots [p, end). Templates are read; expansions are skipped (the caller
// compares the whole again, so a damaged expansion is found there).
bool parseSeq(const std::vector<ClassicAction>& s, size_t& p, size_t end, int depth, uint32_t text, std::vector<ClassicAction>& rows, bool tpl) {
    while (p < end) {
        const ClassicAction& a = s[p];
        if (isMarker(a, text, kMarkBegin)) {
            const size_t T = a.player, X = a.time, L = a.number;
            const size_t nd = (L + kDataBytes - 1) / kDataBytes;
            if (p + 1 + nd > end) return false;
            Bytes bytes;
            for (size_t k = 0; k < nd; ++k) {
                const ClassicAction& d = s[p + 1 + k];
                if (!isMarker(d, text, kMarkData)) return false;
                const uint32_t f[4] = {d.player, d.time, d.number, d.unit};
                for (int q = 0; q < 14; ++q) bytes.push_back(uint8_t(f[q < 4 ? 0 : q < 8 ? 1 : q < 12 ? 2 : 3] >> (8 * (q % 4))));
            }
            if (L > bytes.size()) return false;
            bytes.resize(L);
            Header h;
            if (!decodeHeader(bytes, h)) return false;
            const size_t tplStart = p + 1 + nd, tplEnd = tplStart + T, expEnd = tplEnd + X;
            if (expEnd >= end || !isMarker(s[expEnd], text, kMarkEnd)) return false;
            ClassicAction blk;
            blk.type = kClassicBlockType;
            blk.depth = uint8_t(depth);
            blk.batch = std::make_shared<ClassicBatch>(h.batch);
            rows.push_back(blk);
            std::vector<ClassicAction> kids;
            size_t q = tplStart;
            if (!parseSeq(s, q, tplEnd, depth + 1, text, kids, true) || q != tplEnd) return false;
            int child = 0;
            for (ClassicAction& k : kids) {
                if (k.depth != depth + 1) continue;
                if (k.type != kClassicBlockType) {
                    const bool dis = std::find(h.disabled.begin(), h.disabled.end(), uint8_t(child)) != h.disabled.end();
                    k.flags = uint8_t((k.flags & ~2u) | (dis ? 2u : 0u));
                }
                ++child;
            }
            rows.insert(rows.end(), kids.begin(), kids.end());
            p = expEnd + 1;
            continue;
        }
        if (a.type == kCommentType && a.location == kMarkerMagic && a.text == text) return false;  // a stray marker
        ClassicAction r = a;
        r.depth = uint8_t(depth);
        r.var = {};
        for (int k = 0; k < kClassicVarFields; ++k) {
            const ClassicField f = classicVarField(k);
            const uint32_t v = k == 2 ? a.unit : classicValue(a, f);
            // Only a template row holds placeholders, and only for a block above the row. Any other value is a value.
            const bool isPlaceholder = k == 2 ? (v & 0x8000u) != 0 : (v & 0x80000000u) != 0;
            const uint32_t holds = k == 2 ? v & 0x7FFFu : v & 0x7FFFFFFFu;
            if (!isPlaceholder || !tpl || int(holds) >= depth) continue;
            r.var[size_t(k)] = uint8_t(holds + 1);
            if (k == 2) r.unit = 0;
            else setClassicValue(r, f, 0);
        }
        rows.push_back(r);
        ++p;
    }
    return true;
}

// Splits a trigger's slots into the file's triggers. `n` is the switch that links the parts (used when the slots do not
// fit in one trigger). False when they do not fit and there is no switch.
bool expandTrigger(const ClassicTrigger& t, int n, uint32_t text, std::vector<ClassicTrigger>& out) {
    const std::vector<ClassicAction> slots = emitTrigger(t, text);
    bool preserves = false;  // an enabled "Preserve trigger" action (type 3)
    for (const ClassicAction& s : slots) preserves = preserves || (s.type == 3 && !(s.flags & 2));
    out.clear();
    if (slots.size() <= size_t(kClassicActions)) {
        ClassicTrigger one = t;
        one.splitSwitch = -1;
        for (size_t i = 0; i < kClassicActions; ++i) one.actions[i] = i < slots.size() ? slots[i] : ClassicAction{};
        out.push_back(one);
        return true;
    }
    if (n < 0 || n > 255) return false;
    size_t pos = 0;
    bool first = true;
    while (true) {
        const size_t start = first ? 0 : 1;  // a later part starts with "Set switch n: Clear"
        ClassicTrigger c;
        // A later part keeps "preserve" (also when the preserve action is in the trigger: each part may be the one that
        // runs it, and the game removes a trigger that is not preserved after it runs).
        c.flags = first ? t.flags : (t.flags & (4u | 8u)) | (preserves ? 4u : 0u);
        c.players = t.players;
        if (first) {
            c.conditions = t.conditions;
            c.currentAction = t.currentAction;
        } else {
            c.conditions[0] = makeClassicCondition(kSwitchSetType);
            c.conditions[0].resource = uint8_t(n);
            c.conditions[0].comparison = 2;  // set
        }
        std::vector<ClassicAction> part;
        if (!first) {
            ClassicAction clear = makeClassicAction(kSetSwitchType);
            clear.modifier = 5, clear.number = uint32_t(n);  // Clear
            part.push_back(clear);
        }
        const size_t rest = slots.size() - pos;
        if (rest <= kClassicActions - start) {
            part.insert(part.end(), slots.begin() + std::ptrdiff_t(pos), slots.end());
            for (size_t i = 0; i < part.size(); ++i) c.actions[i] = part[i];
            out.push_back(c);
            break;
        }
        const size_t take = kClassicActions - start - 1;
        part.insert(part.end(), slots.begin() + std::ptrdiff_t(pos), slots.begin() + std::ptrdiff_t(pos + take));
        ClassicAction set = makeClassicAction(kSetSwitchType);
        set.modifier = 4, set.number = uint32_t(n);  // Set
        part.push_back(set);
        for (size_t i = 0; i < part.size(); ++i) c.actions[i] = part[i];
        out.push_back(c);
        pos += take;
        first = false;
    }
    return true;
}

// Is the trigger a part of a split that "Set switch n" links: its first action clears the switch and it needs it.
bool isLaterPart(const ClassicTrigger& t, int n) {
    return t.conditionCount() == 1 && t.conditions[0].type == kSwitchSetType && t.conditions[0].resource == n &&
           t.conditions[0].comparison == 2 && t.actionCount() >= 1 && t.actions[0].type == kSetSwitchType &&
           t.actions[0].modifier == 5 && t.actions[0].number == uint32_t(n);
}
bool endsWithSet(const ClassicTrigger& t, int n) {
    const int c = t.actionCount();
    return c >= 1 && t.actions[size_t(c - 1)].type == kSetSwitchType && t.actions[size_t(c - 1)].modifier == 4 &&
           t.actions[size_t(c - 1)].number == uint32_t(n);
}

uint32_t markerText(const StringTable& strings) {
    for (uint32_t id = 1; id <= strings.size(); ++id)
        if (strings.get(id) == kMarkerText) return id;
    return 0;
}

bool hasBlocks(const ClassicTrigger& t) {
    for (int i = 0; i < t.actionCount(); ++i)
        if (t.actions[size_t(i)].type == kClassicBlockType) return true;
    return false;
}

// The working rows of a file's slots; false when the slots are not a batch.
bool readRows(const std::vector<ClassicAction>& slots, uint32_t text, std::vector<ClassicAction>& rows) {
    rows.clear();
    size_t p = 0;
    if (!parseSeq(slots, p, slots.size(), 0, text, rows, false) || p != slots.size() || rows.size() > size_t(kClassicActions)) return false;
    return true;
}

// Sets the working copy's rows from `rows`.
void setRows(ClassicTrigger& t, const std::vector<ClassicAction>& rows) {
    for (size_t i = 0; i < kClassicActions; ++i) t.actions[i] = i < rows.size() ? rows[i] : ClassicAction{};
    t.pruneBlocks();
}

Bytes encodeOne(const ClassicTrigger& t) {
    Bytes b(kClassicTriggerSize, 0);
    ClassicTriggers one;
    one.triggers.push_back(t);
    const Bytes all = encodeClassicTriggers(one);
    b.assign(all.begin(), all.begin() + std::ptrdiff_t(kClassicTriggerSize));
    return b;
}

}  // namespace

// The block kinds (R23). The picker, the block dialog and the file's kind byte come from this table.
const std::vector<ClassicBlockKind>& classicBlockKinds() {
    static const std::vector<ClassicBlockKind> kinds = {
        {ClassicBatchKind::Unit, "Unit type", "unit", K::Unit},
        {ClassicBatchKind::Player, "Player", "player", K::Player},
        {ClassicBatchKind::Location, "Location", "location", K::Location},
    };
    return kinds;
}

const ClassicBlockKind& classicBlockKind(ClassicBatchKind kind) {
    for (const ClassicBlockKind& k : classicBlockKinds())
        if (k.kind == kind) return k;
    return classicBlockKinds().front();
}

ClassicKind classicBatchValueKind(ClassicBatchKind kind) { return classicBlockKind(kind).values; }

std::string classicBatchName(const std::string& name, ClassicBatchKind kind) {
    std::string out;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_';
        if (ok && out.size() < kBatchNameMax) out += c;
    }
    out = trimmed(out);
    return out.empty() ? classicBlockKind(kind).name : out;
}

std::string classicBatchVariableText(const ClassicBatch& b) { return "variable \"" + b.name + "\""; }

std::string classicBatchText(const ClassicBatch& b, const ClassicNames& names) {
    std::string values;
    for (size_t i = 0; i < b.values.size(); ++i) values += (i ? ", " : "") + classicValueText(classicBatchValueKind(b.kind), b.values[i], names);
    return "For each " + b.name + " in " + (values.empty() ? std::string("no values") : values);
}

ClassicTriggers classicCollapseBatches(const ClassicTriggers& in, const StringTable& strings) {
    ClassicTriggers out;
    out.tail = in.tail;
    const uint32_t text = markerText(strings);
    const auto& v = in.triggers;
    if (!text) {
        out.triggers = v;  // no batch in the map: the triggers stay as they are
        return out;
    }
    for (size_t i = 0; i < v.size();) {
        // A group: the trigger ends with "Set switch n: Set", the next ones are later parts of it.
        const int n = v[i].actionCount() >= 1 ? int(v[i].actions[size_t(v[i].actionCount() - 1)].number) : -1;
        if (n >= 0 && n <= 255 && endsWithSet(v[i], n)) {
            size_t tails = 0;
            while (i + 1 + tails < v.size() && isLaterPart(v[i + 1 + tails], n)) ++tails;
            bool done = false;
            for (size_t len = tails + 1; len >= 2 && !done; --len) {
                std::vector<ClassicAction> slots;
                for (size_t k = 0; k < len; ++k) {
                    const ClassicTrigger& t = v[i + k];
                    const int start = k ? 1 : 0, end = t.actionCount() - (k + 1 < len ? 1 : 0);
                    for (int a = start; a < end; ++a) slots.push_back(t.actions[size_t(a)]);
                }
                std::vector<ClassicAction> rows;
                if (!readRows(slots, text, rows)) continue;
                ClassicTrigger w = v[i];
                setRows(w, rows);
                w.splitSwitch = n;
                std::vector<ClassicTrigger> again;
                if (!expandTrigger(w, n, text, again) || again.size() != len) continue;
                bool same = true;
                for (size_t k = 0; k < len && same; ++k) same = encodeOne(again[k]) == encodeOne(v[i + k]);
                if (!same) continue;
                out.triggers.push_back(w);
                i += len;
                done = true;
            }
            if (done) continue;
        }
        // A single trigger with blocks: its slots are its actions.
        ClassicTrigger single = v[i];
        std::vector<ClassicAction> rows;
        std::vector<ClassicAction> slots(v[i].actions.begin(), v[i].actions.begin() + v[i].actionCount());
        if (readRows(slots, text, rows) && std::any_of(rows.begin(), rows.end(), [](const ClassicAction& a) { return a.type == kClassicBlockType; })) {
            ClassicTrigger w = v[i];
            setRows(w, rows);
            w.splitSwitch = -1;
            std::vector<ClassicTrigger> again;
            if (expandTrigger(w, -1, text, again) && again.size() == 1 && encodeOne(again[0]) == encodeOne(v[i])) single = w;
        }
        out.triggers.push_back(single);
        ++i;
    }
    return out;
}

namespace {
// The switch that links the parts of each trigger that is split (-1: not split). A switch that the map's triggers use
// is not taken; a split keeps the switch it had when it was read, when that is still free. Each split takes its own.
std::vector<int> splitSwitches(const ClassicTriggers& in, uint32_t text) {
    std::set<int> used;
    for (const ClassicTrigger& t : in.triggers) {
        for (int i = 0; i < t.conditionCount(); ++i)
            if (t.conditions[size_t(i)].type == kSwitchSetType) used.insert(t.conditions[size_t(i)].resource);
        for (int i = 0; i < t.actionCount(); ++i)
            if (t.actions[size_t(i)].type == kSetSwitchType) used.insert(int(t.actions[size_t(i)].number & 0xFF));
    }
    std::vector<int> out(in.triggers.size(), -1);
    for (size_t k = 0; k < in.triggers.size(); ++k) {
        const ClassicTrigger& t = in.triggers[k];
        if (!hasBlocks(t) || emitTrigger(t, text).size() <= size_t(kClassicActions)) continue;
        int n = -1;
        if (t.splitSwitch >= 0 && t.splitSwitch <= 255 && !used.count(t.splitSwitch)) n = t.splitSwitch;
        else
            for (int s = 0; s <= 255 && n < 0; ++s)
                if (!used.count(s)) n = s;
        if (n < 0) return out;  // no switch is free for this trigger (classicCanExpand)
        used.insert(n);
        out[k] = n;
    }
    return out;
}
}  // namespace

bool classicCanExpand(const ClassicTriggers& in) {
    const std::vector<int> link = splitSwitches(in, 0);
    for (size_t k = 0; k < in.triggers.size(); ++k)
        if (hasBlocks(in.triggers[k]) && emitTrigger(in.triggers[k], 0).size() > size_t(kClassicActions) && link[k] < 0) return false;
    return true;
}

ClassicTriggers classicExpandBatches(const ClassicTriggers& in, StringTable& strings) {
    ClassicTriggers out;
    out.tail = in.tail;
    bool any = false;
    for (const ClassicTrigger& t : in.triggers)
        if (hasBlocks(t)) any = true;
    uint32_t text = 0;
    if (any) text = classicStringId(strings, kMarkerText);
    else if ((text = markerText(strings)) != 0) strings.set(text, "");  // no batch uses the marker's string any more
    const std::vector<int> link = splitSwitches(in, text);
    for (size_t k = 0; k < in.triggers.size(); ++k) {
        const ClassicTrigger& t = in.triggers[k];
        if (!hasBlocks(t)) {  // a trigger without blocks is written as it is (its bytes after the list too)
            out.triggers.push_back(t);
            continue;
        }
        std::vector<ClassicTrigger> parts;
        if (!expandTrigger(t, link[k], text, parts)) {
            // No switch is free: classicCanExpand says so, and the editor does not write it. The first 64 slots are
            // written here, so that the map stays valid.
            ClassicTrigger cut = t;
            const std::vector<ClassicAction> slots = emitTrigger(t, text);
            for (size_t i = 0; i < kClassicActions; ++i) cut.actions[i] = slots[i];
            cut.splitSwitch = -1;
            parts.assign(1, cut);
        }
        out.triggers.insert(out.triggers.end(), parts.begin(), parts.end());
    }
    for (ClassicTrigger& t : out.triggers) t.splitSwitch = -1;
    return out;
}


}  // namespace stardraft
