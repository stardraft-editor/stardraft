#include "stardraft/import_ee3_extras.h"

// EUD Editor 3's Lua macros: a port of MacroManager.MacroApply and of the functions that its Lua calls
// (Class/MacroFunc/MacroManager.vb, MacroDefaultFunction.vb, MacroPluginManager.vb), with EUD Editor 3's own Lua files
// (Data/Lua/TriggerEditor/*.lua, unchanged: third_party/eud-editor-3/Lua/, embedded by tools/gen_ee3_lua.py). The
// text is the same as EUD Editor 3's build writes, quirks included: "@Name(a, b)" becomes "<?Name("a", "b")?>" by its
// quoting rules, each "<? ... ?>" is Lua whose echo() calls give the text in its place, and the main file gets the
// preDefine lines and the onPluginStart, beforeTriggerExec and afterTriggerExec functions.
//
// The Lua is Lua 5.1.5 (CMakeLists.txt), the version of EUD Editor 3's KopiLua. The project file comes from the
// internet, so its Lua runs in a sandbox:
//   - only the base, string, table and math libraries: no io, os, package (require, module) or debug library. From the
//     base library, dofile, loadfile, load, collectgarbage, gcinfo, getfenv, setfenv and newproxy are removed; print
//     does nothing; loadstring loads text only (a binary chunk can crash Lua 5.1); string.dump is removed and
//     string.rep checks the length first. A "<? ... ?>" block that is a binary chunk is not run.
//   - a memory limit for the project's Lua (its own allocator), an instruction count and a time limit for each script
//     (a count hook), and a limit for the C work and the C stack of Lua's pattern matcher (a patched lstrlib.c:
//     stardraft_lua_match_step, stardraft_lua_find_step). Past a limit the block stops with an error.
//   - each call into Lua is protected (lua_cpcall, lua_pcall): a Lua error is a problem line, and the block stays as
//     written. What the host functions keep (echo's text, the MSQC items, ...) has a limit too.
//
// Not made: EUD Editor 3's SC Archive (ParseSCAScript: StarDraft has no SC Archive; its MSQC lines are left out too).
// The names of weapons, flingy, sprites, images, upgrades, techs and orders that ParseWeapon and the others know are
// eudplib's names (EUD Editor 3 uses the project's stat_txt.tbl and its own lists; most are the same); a name that is
// not known stays as written, as in EUD Editor 3.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "stardraft/dat_extras.h"

namespace stardraft {
namespace {
#include "data/ee3_lua.inc"
#include "data/eud_names.inc"
}  // namespace
}  // namespace stardraft

namespace {

// --- The limits ------------------------------------------------------------------------------------------------

constexpr size_t kMemoryLimit = size_t(64) << 20;        // the project's Lua memory
constexpr uint64_t kInstructionLimit = 50'000'000;       // per script (apply: all its blocks)
constexpr int kTimeLimitSeconds = 10;                    // per script (all its blocks)
constexpr int kProjectTimeLimitSeconds = 60;             // all the scripts of a project (an Ee3Macros)
constexpr uint64_t kPatternWorkLimit = 200'000'000;      // pattern matcher steps per script
constexpr uint64_t kTimeCheckEvery = 4096;               // pattern matcher steps between two looks at the clock
constexpr uintptr_t kPatternStackLimit = 192 * 1024;     // bytes of C stack below apply() for the pattern matcher
constexpr int kHookEvery = 128;                          // instructions between two checks
constexpr size_t kHostTextLimit = size_t(32) << 20;      // what the host functions keep for the project
constexpr size_t kEchoLimit = size_t(16) << 20;          // echo's text per script
constexpr size_t kTextLimit = size_t(64) << 20;          // a script's text
constexpr uint64_t kScanLimit = uint64_t(64) << 30;      // bytes that the text search may look at per script (the clock also stops it)
constexpr size_t kProblemLines = 20;                     // the failed blocks named per script (then their count)

// The limits of the running script. Lua's pattern matcher (the patched lstrlib.c) has no lua_State at hand for them, so
// they are per thread: set while apply() or the constructor runs Lua.
struct Limits {
    size_t memory = 0;
    bool memoryRefused = false;
    uint64_t instructions = 0, patternWork = 0, nextTimeCheck = kTimeCheckEvery;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point projectDeadline = std::chrono::steady_clock::time_point::max();  // (set by the constructor)
    bool over = false;   // a limit is passed: each check stops the script
    std::string why;     // which one
    uintptr_t stackBase = 0;

    // A new script: its own instruction count, pattern work and 10 seconds (less when the project's 60 end first).
    void start(const void* stack) {
        instructions = 0, patternWork = 0, nextTimeCheck = kTimeCheckEvery, over = false, why.clear(), memoryRefused = false;
        deadline = std::min(std::chrono::steady_clock::now() + std::chrono::seconds(kTimeLimitSeconds), projectDeadline);
        stackBase = reinterpret_cast<uintptr_t>(stack);
    }
    void stop(const char* what) {
        if (!over) over = true, why = what;
    }
    bool timeOver() {
        if (std::chrono::steady_clock::now() > deadline)
            stop(deadline == projectDeadline ? "the project's macros run for more than 60 seconds" : "it runs for more than 10 seconds");
        return over;
    }
    // The pattern matcher's work: true when it may go on.
    bool patternStep(uint64_t n, const char* what) {
        if (over) return false;
        patternWork += n;
        if (patternWork > kPatternWorkLimit) {
            stop(what);
            return false;
        }
        if (patternWork >= nextTimeCheck) {
            nextTimeCheck = patternWork + kTimeCheckEvery;
            if (timeOver()) return false;
        }
        return true;
    }
};
thread_local Limits* tlsLimits = nullptr;

// Sets tlsLimits while Lua runs (and puts back what was there).
struct LimitsScope {
    Limits* old;
    explicit LimitsScope(Limits* l) : old(tlsLimits) { tlsLimits = l; }
    ~LimitsScope() { tlsLimits = old; }
    LimitsScope(const LimitsScope&) = delete;
    LimitsScope& operator=(const LimitsScope&) = delete;
};

}  // namespace

// Called by Lua's pattern matcher for each step (the patched lstrlib.c, CMakeLists.txt). 0: stop the pattern (too much
// work, too long, or too deep into the C stack).
int stardraft_lua_match_step(void) {
    Limits* l = tlsLimits;
    if (!l) return 1;
    if (!l->patternStep(1, "its patterns take more than 200 million steps")) return 0;
    char here = 0;
    const uintptr_t at = reinterpret_cast<uintptr_t>(&here);
    const uintptr_t used = l->stackBase > at ? l->stackBase - at : at - l->stackBase;
    return used < kPatternStackLimit ? 1 : 0;
}

// Called by the plain find (string.find with plain text) for each place that it compares, with the bytes it compares,
// and with 0 by the pattern matcher's own loops (a bracket class, %b, the longest repetition, each pattern item). 0:
// stop (the find or the item then finds nothing, the next match step raises the error, and a script that ends first
// fails all the same: apply() treats a passed limit as a failure).
int stardraft_lua_find_step(size_t n) {
    Limits* l = tlsLimits;
    if (!l) return 1;
    return l->patternStep(1 + n / 64, "its text searches take too much work") ? 1 : 0;
}

namespace stardraft {

namespace {

// --- Small helpers ---------------------------------------------------------------------------------------------

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n' || s[a] == '\v' || s[a] == '\f')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n' || s[b - 1] == '\v' || s[b - 1] == '\f')) --b;
    return s.substr(a, b - a);
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
    return s;
}

std::string hexUpper(uint64_t v) {
    static const char* d = "0123456789ABCDEF";
    std::string out;
    do out.insert(out.begin(), d[v & 15]), v >>= 4;
    while (v);
    return out;
}

// VB's IsNumeric / CDbl on a string: decimal (with a sign, a point, an exponent), &H hex or &O octal, with spaces around.
bool vbNumber(const std::string& text, double& out) {
    const std::string s = trim(text);
    if (s.empty()) return false;
    if (s.size() > 2 && s[0] == '&' && (s[1] == 'H' || s[1] == 'h' || s[1] == 'O' || s[1] == 'o')) {
        char* end = nullptr;
        const unsigned long long v = std::strtoull(s.c_str() + 2, &end, (s[1] == 'H' || s[1] == 'h') ? 16 : 8);
        if (!end || *end) return false;
        out = double(v);
        return true;
    }
    for (char c : s)
        if (!((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == 'e' || c == 'E')) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && !*end && std::isfinite(out);
}

// VB's CInt and CUInt of a Double (round half to even; false: VB throws, out of range).
bool vbInt(double d, long long lo, long long hi, long long& out) {
    const double r = std::nearbyint(d);
    if (!(r >= double(lo) && r <= double(hi))) return false;
    out = (long long)r;
    return true;
}

// .NET's \w for one character of UTF-8 text at `i`: its length in bytes, or 0 when it is not a word character
// (letters, digits, "_" and combining marks; for text outside ASCII the common scripts).
size_t wordChar(const std::string& s, size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') ? 1 : 0;
    size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
    if (!n || i + n > s.size()) return 0;
    uint32_t cp = n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
    for (size_t k = 1; k < n; ++k) {
        const unsigned char d = static_cast<unsigned char>(s[i + k]);
        if ((d & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (d & 0x3F);
    }
    static const uint32_t kRanges[][2] = {
        {0xAA, 0xAA}, {0xB5, 0xB5}, {0xBA, 0xBA}, {0xC0, 0xD6}, {0xD8, 0xF6}, {0xF8, 0x2FF}, {0x300, 0x36F},
        {0x370, 0x3FF}, {0x400, 0x52F}, {0x531, 0x587}, {0x5D0, 0x5EA}, {0x620, 0x64A}, {0x660, 0x669}, {0xE00, 0xE7F},
        {0x1100, 0x11FF}, {0x1E00, 0x1FFF}, {0x203F, 0x2040}, {0x3005, 0x3007}, {0x3041, 0x3096}, {0x30A1, 0x30FA},
        {0x3131, 0x318E}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFF10, 0xFF19},
        {0xFF21, 0xFF3A}, {0xFF3F, 0xFF3F}, {0xFF41, 0xFF5A}, {0xFF66, 0xFF9F}, {0x20000, 0x2FA1F},
    };
    for (const auto& r : kRanges)
        if (cp >= r[0] && cp <= r[1]) return n;
    return 0;
}

// The first match of "function\s*<name>\(\s*\)\s*\{" (.NET's \s: here the ASCII white space). npos: none; `end` is
// where the match ends.
size_t findFunction(const std::string& s, const std::string& name, size_t& end) {
    auto ws = [&](size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == '\v' || s[i] == '\f')) ++i;
    };
    for (size_t at = s.find("function"); at != std::string::npos; at = s.find("function", at + 1)) {
        size_t i = at + 8;
        ws(i);
        if (s.compare(i, name.size(), name) != 0) continue;
        i += name.size();
        if (i >= s.size() || s[i] != '(') continue;
        ++i;
        ws(i);
        if (i >= s.size() || s[i] != ')') continue;
        ++i;
        ws(i);
        if (i >= s.size() || s[i] != '{') continue;
        end = i + 1;
        return at;
    }
    return std::string::npos;
}

// MacroPluginManager.CheckIsPattern: "^start.*mid.*end$" (the regex \^[^.*$]*\.\*[^.*$]*\.\*[^.*$]*\$, anywhere).
bool isChatPattern(const std::string& s) {
    auto plain = [](char c) { return c != '.' && c != '*' && c != '$'; };
    for (size_t at = s.find('^'); at != std::string::npos; at = s.find('^', at + 1)) {
        size_t i = at + 1;
        bool ok = true;
        for (int part = 0; part < 2 && ok; ++part) {
            while (i < s.size() && plain(s[i])) ++i;
            ok = i + 1 < s.size() && s[i] == '.' && s[i + 1] == '*';
            i += 2;
        }
        if (!ok) continue;
        while (i < s.size() && plain(s[i])) ++i;
        if (i < s.size() && s[i] == '$') return true;
    }
    return false;
}

// A list that keeps each text once, in the order they come (the VB lists with IndexOf before Add).
struct OrderedSet {
    std::vector<std::string> items;
    std::unordered_map<std::string, size_t> index;
    // The text's index; `added` when it is new.
    size_t add(const std::string& t, bool& added) {
        const auto [it, isNew] = index.emplace(t, items.size());
        added = isNew;
        if (isNew) items.push_back(t);
        return it->second;
    }
    bool contains(const std::string& t) const { return index.count(t) > 0; }
    void clear() { items.clear(), index.clear(); }
};

// MacroDefaultFunction's lists of EUD scores and supply types, with their addresses.
const char* const kScoreNames[] = {
    "Score Total Units Produced", "Score Units Produced", "Score Units Owned", "Score Units Lost", "Score Units Killed",
    "Score Unit Total", "Score Kill Total", "Score Structures Constructed Total", "Score Structures Constructed",
    "Score Structures Owned", "Score Structures Lost", "Score Structures Razed", "Score Buildings Total",
    "Score Razings Total", "Score Factories Constructed", "Score Factories Owned", "Score Factories Lost",
    "Score Factories Razed", "Score Custom"};
const char* const kScoreOffsets[] = {"0x581DE4", "0x581E14", "0x581E44", "0x581E74", "0x581EA4", "0x581ED4", "0x581F04",
                                     "0x581F34", "0x581F64", "0x581F94", "0x581FC4", "0x581FF4", "0x582024", "0x582054",
                                     "0x582084", "0x5820B4", "0x5820E4", "0x582114", "0x5822F4"};
const char* const kSupplyNames[] = {"Zerg Control Available",   "Zerg Control Used",   "Zerg Control Max",
                                    "Terran Supply Available",  "Terran Supply Used",  "Terran Supply Max",
                                    "Protoss Psi Available",    "Protoss Psi Used",    "Protoss Psi Max"};
const char* const kSupplyOffsets[] = {"0x582144", "0x582174", "0x5821A4", "0x5821D4", "0x582204",
                                      "0x582234", "0x582264", "0x582294", "0x5822C4"};

// The .dat files that the Dat macros know (SCDatFiles.GetDatFileE).
const char* const kDatNames[] = {"units", "weapons", "flingy", "sprites", "images", "upgrades", "techdata", "orders"};

// An error that the host function gives to the Lua (as a Lua error).
struct HostError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Lua's panic (an error with no protected call): never expected, as each call into Lua is protected; it then stops
// the import's Lua instead of the program.
struct LuaPanic : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace

struct Ee3Macros::State {
    lua_State* L = nullptr;
    Limits limits;
    bool dead = false;                 // Lua stopped (a panic): no more scripts
    std::vector<std::string> pending;  // problems for the next apply() (the Lua files' own errors, notes)

    // The project and the map.
    std::vector<std::string> bgmNames;
    uint32_t addr = 0x58D900, ptrAddr = 0x58D904, patternAddr = 0x58D908, lenAddr = 0x58D90C;
    bool useMsqc = true, useChat = true, scaUsed = false;
    std::string mouseLocation;
    struct DatParam {
        long long size = 0, start = 0, array = 1;
    };
    std::map<std::string, std::map<std::string, DatParam>> datParams;  // file -> field name -> info
    bool datRead = false;
    std::unordered_map<std::string, uint32_t> offsets;  // Offset.txt: "units_Hit Points" -> address
    std::vector<std::string> unitNames;                  // ParseUnit's list (EngStat_txt), 0..227
    std::vector<std::string> locationNames, switchNames; // 0..255

    // MacroManager's state: the build's (BuildStart clears it once) and the script's.
    std::string echo;
    size_t hostText = 0;
    OrderedSet preDefine, mainPreDefine, onPlugin, before, after;
    std::vector<std::pair<std::string, std::string>> msqc;  // key ("KeyDown(A)") -> "cond,vname", in order
    std::unordered_set<std::string> msqcKeys;
    OrderedSet chats, mouse, scaVariables;
    bool msqcNoted = false, chatNoted = false, scaNoted = false;

    void keep(size_t n) {
        hostText += n;
        if (hostText > kHostTextLimit) throw HostError("the macros keep more than 32 MB of text");
    }
};

namespace {

using State = Ee3Macros::State;

State* stateOf(lua_State* L) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    return static_cast<State*>(ud);
}

// --- The sandbox -------------------------------------------------------------------------------------------------

void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    State* s = static_cast<State*>(ud);
    if (nsize == 0) {
        std::free(ptr);
        s->limits.memory -= std::min(osize, s->limits.memory);
        return nullptr;
    }
    if (nsize > osize && s->limits.memory - std::min(osize, s->limits.memory) + nsize > kMemoryLimit) {
        s->limits.memoryRefused = true;
        return nullptr;  // (Lua: "not enough memory")
    }
    void* p = std::realloc(ptr, nsize);
    if (!p) return nsize <= osize ? ptr : nullptr;  // (Lua expects a smaller block never to fail)
    s->limits.memory = s->limits.memory - std::min(osize, s->limits.memory) + nsize;
    return p;
}

int luaPanic(lua_State* L) {
    const char* m = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "?";
    throw LuaPanic(std::string("Lua stopped: ") + m);
}

void countHook(lua_State* L, lua_Debug*) {
    State* s = stateOf(L);
    Limits& l = s->limits;
    l.instructions += kHookEvery;
    if (!l.over) {
        if (l.instructions > kInstructionLimit) l.stop("it runs more than 50 million Lua instructions");
        else l.timeOver();
    }
    if (l.over) {
        lua_sethook(L, countHook, LUA_MASKCOUNT, 1);  // (each next instruction stops too, also after a Lua pcall)
        luaL_error(L, "stopped: %s", l.why.c_str());
    }
}

// Calls a host function; a C++ exception (HostError, std::bad_alloc) becomes a Lua error.
template <int (*F)(lua_State*, State&)>
int host(lua_State* L) {
    std::string err;
    try {
        return F(L, *stateOf(L));
    } catch (const std::exception& e) {
        err = e.what();
    }
    return luaL_error(L, "%s", err.c_str());
}

// An argument as LuaInterface gives it to a String parameter: a string or a number (as text); nil is VB's Nothing
// (here ""). Other types: LuaInterface refuses the call.
std::string arg(lua_State* L, int i) {
    const int t = lua_type(L, i);
    if (t == LUA_TNONE || t == LUA_TNIL) return {};
    if (t != LUA_TSTRING && t != LUA_TNUMBER) {
        luaL_error(L, "invalid arguments to method call (argument %d is a %s)", i, lua_typename(L, t));
        return {};
    }
    size_t n = 0;
    const char* p = lua_tolstring(L, i, &n);
    return std::string(p, n);
}

void push(lua_State* L, const std::string& s) { lua_pushlstring(L, s.data(), s.size()); }

long long argInt(lua_State* L, int i, const char* what) {
    double d = 0;
    long long v = 0;
    if (!vbNumber(arg(L, i), d) || !vbInt(d, INT32_MIN, INT32_MAX, v)) throw HostError(std::string(what) + ": not a number");
    return v;
}

// loadstring: text only (a binary chunk can crash Lua 5.1).
int sandboxLoadstring(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    const char* name = luaL_optstring(L, 2, s);
    if (n > 0 && s[0] == LUA_SIGNATURE[0]) {
        lua_pushnil(L);
        lua_pushliteral(L, "binary chunks are not allowed");
        return 2;
    }
    if (luaL_loadbuffer(L, s, n, name) == 0) return 1;
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

int sandboxPrint(lua_State*) { return 0; }

// xpcall(f, handler): Lua 5.1's own calls the handler with the hooks off (no instruction or time limit in it), so this
// one calls f protected, then the handler as an ordinary call. The same results: true and f's, or false and the
// handler's. (EUD Editor 3's macro files do not use xpcall.)
int sandboxXpcall(lua_State* L) {
    luaL_checkany(L, 2);
    lua_settop(L, 2);
    lua_pushvalue(L, 1);
    if (lua_pcall(L, 0, LUA_MULTRET, 0) == 0) {
        lua_pushboolean(L, 1);
        lua_replace(L, 2);  // (true where the handler was; f stays under it)
        return lua_gettop(L) - 1;
    }
    lua_pushvalue(L, 2);
    lua_insert(L, -2);  // the handler, then the error
    lua_call(L, 1, 1);
    lua_pushboolean(L, 0);
    lua_insert(L, -2);
    return 2;
}

// string.rep: Lua 5.1's loops without end for an empty text and a large count, and asks for any length.
int sandboxRep(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    const lua_Number count = luaL_checknumber(L, 2);
    if (!(count >= 1) || n == 0) {
        lua_pushliteral(L, "");
        return 1;
    }
    if (count * double(n) > double(kMemoryLimit)) return luaL_error(L, "string.rep: the text is too long");
    const size_t times = size_t(count);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (size_t k = 0; k < times; ++k) luaL_addlstring(&b, s, n);
    luaL_pushresult(&b);
    return 1;
}

// --- The host functions (MacroDefaultFunction.vb, MacroPluginManager.vb) ---------------------------------------

void addText(State& s, OrderedSet& set, const std::string& t) {
    bool added = false;
    if (!set.contains(t)) s.keep(t.size());
    set.add(t, added);
}

int fPreDefine(lua_State* L, State& s) { return addText(s, s.preDefine, arg(L, 1)), 0; }
int fMainPreDefine(lua_State* L, State& s) { return addText(s, s.mainPreDefine, arg(L, 1)), 0; }
int fOnPluginText(lua_State* L, State& s) { return addText(s, s.onPlugin, arg(L, 1)), 0; }
int fBeforeText(lua_State* L, State& s) { return addText(s, s.before, arg(L, 1)), 0; }
int fAfterText(lua_State* L, State& s) { return addText(s, s.after, arg(L, 1)), 0; }

void echo(State& s, const std::string& t) {
    if (s.echo.size() + t.size() > kEchoLimit) throw HostError("echo: the macros' text is longer than 16 MB");
    s.echo += t;
}
int fEcho(lua_State* L, State& s) { return echo(s, arg(L, 1)), 0; }

long long bgmIndex(const State& s, const std::string& name) {
    for (size_t i = 0; i < s.bgmNames.size(); ++i)
        if (s.bgmNames[i] == name) return (long long)i;
    return 0;
}
int fGetBGMIndex(lua_State* L, State& s) { return echo(s, std::to_string(bgmIndex(s, arg(L, 1)))), 0; }
int fGetReturnBGMIndex(lua_State* L, State& s) { return lua_pushnumber(L, lua_Number(bgmIndex(s, arg(L, 1)))), 1; }

// ParseUnit and the others: the index of the name in the list as text, else the text as given.
int parseIn(lua_State* L, const std::vector<std::string>& names) {
    const std::string name = arg(L, 1);
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return push(L, std::to_string(i)), 1;
    if (lua_isnil(L, 1) || lua_isnone(L, 1)) return lua_pushnil(L), 1;  // (Nothing back)
    return push(L, name), 1;
}
template <size_t N>
std::vector<std::string> namesOf(const char* const (&list)[N]) {
    std::vector<std::string> out;
    for (const char* n : list) out.push_back(n ? n : "");
    return out;
}
int fParseUnit(lua_State* L, State& s) { return parseIn(L, s.unitNames); }
int fParseLocation(lua_State* L, State& s) { return parseIn(L, s.locationNames); }
int fParseSwitchName(lua_State* L, State& s) { return parseIn(L, s.switchNames); }
int fParseWeapon(lua_State* L, State&) {
    static const auto n = namesOf(kEudWeaponNames);
    return parseIn(L, n);
}
int fParseFlingy(lua_State* L, State&) {
    static const auto n = namesOf(kEudFlingyNames);
    return parseIn(L, n);
}
int fParseSprites(lua_State* L, State&) {
    static const auto n = namesOf(kEudSpriteNames);
    return parseIn(L, n);
}
int fParseImages(lua_State* L, State&) {
    static const auto n = namesOf(kEudImageNames);
    return parseIn(L, n);
}
int fParseUpgrades(lua_State* L, State&) {
    static const auto n = namesOf(kEudUpgradeNames);
    return parseIn(L, n);
}
int fParseTechdata(lua_State* L, State&) {
    static const auto n = namesOf(kEudTechNames);
    return parseIn(L, n);
}
int fParseOrders(lua_State* L, State&) {
    static const auto n = namesOf(kEudOrderNames);
    (void)kEudPortraitNames;  // (eud_names.inc's one list that the macros do not use)
    return parseIn(L, n);
}
int fParseEUDScore(lua_State* L, State&) {
    static const auto n = namesOf(kScoreNames);
    return parseIn(L, n);
}
int fParseSupplyType(lua_State* L, State&) {
    static const auto n = namesOf(kSupplyNames);
    return parseIn(L, n);
}
// ParseSCAScript: the SC Archive's script list is not read (StarDraft has no SC Archive); the name without quotes,
// as EUD Editor 3 gives for a name that it does not know.
int fParseSCAScript(lua_State* L, State& s) {
    if (!s.scaNoted) s.scaNoted = true, s.pending.push_back("a macro uses SC Archive scripts, which StarDraft does not have");
    std::string name = replaceAll(arg(L, 1), "\"", "");
    return push(L, name), 1;
}
int fParseSCAScriptVariable(lua_State* L, State& s) {
    const std::string name = arg(L, 1);
    bool added = false;
    if (!s.scaVariables.contains(name)) s.keep(name.size());
    return push(L, std::to_string(s.scaVariables.add(name, added))), 1;
}

int fGetEUDScoreOffset(lua_State* L, State&) {
    const long long i = argInt(L, 1, "GetEUDScoreOffset");
    if (i < 0 || size_t(i) >= std::size(kScoreOffsets)) throw HostError("GetEUDScoreOffset: the index is out of range");
    return lua_pushstring(L, kScoreOffsets[i]), 1;
}
int fGetSupplyOffset(lua_State* L, State&) {
    const long long i = argInt(L, 1, "GetSupplyOffset");
    if (i < 0 || size_t(i) >= std::size(kSupplyOffsets)) throw HostError("GetSupplyOffset: the index is out of range");
    return lua_pushstring(L, kSupplyOffsets[i]), 1;
}

int fIsNumber(lua_State* L, State&) {
    double d = 0;
    return lua_pushboolean(L, vbNumber(arg(L, 1), d)), 1;
}

void noteMsqc(State& s) {
    if (!s.useMsqc && !s.msqcNoted)
        s.msqcNoted = true, s.pending.push_back("the macros read keys or the mouse, but MSQC is off in the project's settings: as in EUD "
                                                "Editor 3, the project gets no MSQC lines for them");
}

// AddMouseEvent(Line, MinX, MinY): a click in a text line; its index (as text).
int fAddMouseEvent(lua_State* L, State& s) {
    double d = 0;
    long long line = 0;
    if (!vbNumber(arg(L, 1), d) || !vbInt(d - 1, INT32_MIN, INT32_MAX, line)) throw HostError("AddMouseEvent: the line is not a number");
    const std::string minX = arg(L, 2), maxX = arg(L, 3);
    std::string cond = "MouseUp(L) ; ";
    cond += "Memory(0x6CDDC8, AtLeast, " + std::to_string(line * 16 + 110) + ") ; ";
    cond += "Memory(0x6CDDC8, AtMost, " + std::to_string((line + 1) * 16 + 109) + ") ; ";
    cond += "Memory(0x6CDDC4, AtLeast, " + minX + ") ; ";
    cond += "Memory(0x6CDDC4, AtMost, " + maxX + ")";
    bool added = false;
    if (!s.mouse.contains(cond)) s.keep(cond.size());
    const size_t i = s.mouse.add(cond, added);
    noteMsqc(s);
    return push(L, std::to_string(i)), 1;
}

// AddMSQCPlugin(keyboard, vname, action, cond).
int fAddMSQCPlugin(lua_State* L, State& s) {
    const std::string keyboard = arg(L, 1), vname = arg(L, 2), action = arg(L, 3), cond = arg(L, 4);
    const std::string key = action + "(" + keyboard + ")";
    if (s.msqcKeys.insert(key).second) {
        s.keep(key.size() + cond.size() + vname.size());
        s.msqc.push_back({key, cond + "," + vname});
    }
    noteMsqc(s);
    return 0;
}

// AddChatEventPlugin(chat): its index (as text).
int fAddChatEventPlugin(lua_State* L, State& s) {
    const std::string chat = arg(L, 1);
    bool added = false;
    if (!s.chats.contains(chat)) s.keep(chat.size());
    const size_t i = s.chats.add(chat, added);
    if (!s.useChat && !s.chatNoted)
        s.chatNoted = true, s.pending.push_back("the macros read chat messages, but the chat events are off in the project's settings: as "
                                                "in EUD Editor 3, the project gets no chatEvent lines for them");
    return push(L, std::to_string(i)), 1;
}

// The .dat field of a Dat macro (pjData.Dat.GetDatFile(...).GetParamInfo).
const State::DatParam& datParam(const State& s, const std::string& dat, const std::string& field) {
    if (std::find(std::begin(kDatNames), std::end(kDatNames), dat) == std::end(kDatNames))
        throw HostError("there is no .dat file \"" + dat + "\"");
    if (!s.datRead) throw HostError("the project has no .dat data");
    const auto f = s.datParams.find(dat);
    if (f != s.datParams.end())
        if (const auto p = f->second.find(field); p != f->second.end()) return p->second;
    throw HostError(dat + ".dat has no field \"" + field + "\"");
}

// MacroDefaultFunction.DatOffset: the field's address, "0x662350".
std::string datOffset(const State& s, const std::string& dat, std::string field) {
    field = trim(replaceAll(field, "_", " "));
    datParam(s, dat, field);
    const auto it = s.offsets.find(dat + "_" + field);
    if (it == s.offsets.end()) throw HostError("EUD Editor 3 has no address for " + dat + ".dat's field \"" + field + "\"");
    return "0x" + hexUpper(it->second);
}

int fDatOffset(lua_State* L, State& s) { return push(L, datOffset(s, arg(L, 1), arg(L, 2))), 1; }

// "index - start * length": the part after the address (as EUD Editor 3 writes it, without brackets).
std::string datAdd(const std::string& index, const State::DatParam& p) {
    std::string add = p.start == 0 ? index : index + " - " + std::to_string(p.start);
    return add + " * " + std::to_string((p.size * p.array) & 0xFF);  // (Length As Byte)
}

int fGetDatFile(lua_State* L, State& s) {
    const std::string dat = arg(L, 1), field = arg(L, 2), index = arg(L, 3);
    const std::string offset = datOffset(s, dat, field);
    const State::DatParam& p = datParam(s, dat, trim(replaceAll(field, "_", " ")));
    const std::string read = p.size == 1 ? "bread" : p.size == 2 ? "wread" : p.size == 4 ? "dwread" : "";
    return push(L, read + "(" + offset + " + " + datAdd(index, p) + ")"), 1;
}

// VB's Select Case on a String with a number case: the text as a number (VB throws when it is not one).
long long modifierNumber(const std::string& modifier, const char* what) {
    double d = 0;
    if (!vbNumber(modifier, d)) throw HostError(std::string(what) + ": the modifier \"" + modifier + "\" is not a number");
    return std::isfinite(d) && std::nearbyint(d) == d ? (long long)d : LLONG_MIN;
}

// The mask and the place in the dword of a 1- or 2-byte field at a constant index (SetDatFile, ConditionDatFile).
bool datByte(const std::string& index, const State::DatParam& p, long long& f, std::string& flag) {
    double d = 0;
    long long rindex = 0;
    if (!vbNumber(index, d)) return false;
    if (!vbInt(d * double((p.size * p.array) & 0xFF), INT32_MIN, INT32_MAX, rindex)) throw HostError("the .dat index is out of range");
    f = rindex % 4;
    flag = p.size == 1 ? "FF" : "FFFF";
    for (long long i = 0; i < f; ++i) flag += "00";
    return true;
}

int fSetDatFile(lua_State* L, State& s) {
    const std::string dat = arg(L, 1), field = arg(L, 2), value = arg(L, 4), modifier = arg(L, 5);
    std::string index = arg(L, 3);
    const std::string offset = datOffset(s, dat, field);
    // index = ParseUnit(index) (for each .dat, as EUD Editor 3 does).
    for (size_t i = 0; i < s.unitNames.size(); ++i)
        if (s.unitNames[i] == index) {
            index = std::to_string(i);
            break;
        }
    const State::DatParam& p = datParam(s, dat, trim(replaceAll(field, "_", " ")));
    const std::string rOffset = offset + " + " + datAdd(index, p);
    std::string action;
    if (p.size == 1 || p.size == 2) {
        long long f = 0;
        std::string flag;
        if (datByte(index, p, f, flag)) {
            double v = 0;
            if (vbNumber(value, v)) {
                long long iv = 0;
                if (!vbInt(v * std::pow(256.0, double(f)), 0, 4294967295LL, iv)) throw HostError("SetDatFile: the value is out of range");
                action = "SetMemoryXEPD(EPD(" + rOffset + "), " + modifier + ", " + std::to_string(iv) + ", 0x" + flag + ")";
            } else if (f == 0) {
                action = "SetMemoryXEPD(EPD(" + rOffset + "), " + modifier + ", " + value + ", 0x" + flag + ")";
            } else {
                const long long pw = (long long)std::pow(256.0, double(f));
                action = "SetMemoryXEPD(EPD(" + rOffset + "), " + modifier + ", " + value + " * " + std::to_string(pw) + ", 0x" + flag + ")";
            }
        } else {
            const std::string w = p.size == 1 ? "b" : "w";
            switch (modifierNumber(modifier, "SetDatFile")) {
            case 7: action = w + "write(" + rOffset + " ," + value + ")"; break;
            case 8: action = w + "write(" + rOffset + " ," + w + "read(" + rOffset + ") + " + value + ")"; break;
            case 9: action = w + "write(" + rOffset + " ," + w + "read(" + rOffset + ") - " + value + ")"; break;
            default: break;
            }
        }
    } else if (p.size == 4) {
        action = "SetMemory(" + rOffset + ", " + modifier + ", " + value + ")";
    }
    return push(L, action), 1;
}

int fConditionDatFile(lua_State* L, State& s) {
    const std::string dat = arg(L, 1), field = arg(L, 2), index = arg(L, 3), value = arg(L, 4), modifier = arg(L, 5);
    const std::string offset = datOffset(s, dat, field);
    const State::DatParam& p = datParam(s, dat, replaceAll(field, "_", " "));  // (not trimmed here)
    const std::string rOffset = offset + " + " + datAdd(index, p);
    std::string action;
    if (p.size == 1 || p.size == 2) {
        long long f = 0;
        std::string flag;
        if (datByte(index, p, f, flag)) {
            double v = 0;
            if (vbNumber(value, v)) {
                long long iv = 0;
                if (!vbInt(p.size == 1 ? v : v * 65536, 0, 4294967295LL, iv)) throw HostError("ConditionDatFile: the value is out of range");
                action = "MemoryXEPD(EPD(" + rOffset + "), " + modifier + ", " + std::to_string(iv) + ", 0x" + flag + ")";
            } else {
                action = "MemoryXEPD(EPD(" + rOffset + "), " + modifier + ", " + value + (p.size == 1 ? "" : " * 65536") + ", 0x" + flag + ")";
            }
        } else {
            const std::string r = p.size == 1 ? "bread(" : "wread(";
            switch (modifierNumber(modifier, "ConditionDatFile")) {
            case 0: action = "(" + r + rOffset + ") > " + value + ")"; break;
            case 1: action = "(" + r + rOffset + ") < " + value + ")"; break;
            case 10: action = "(" + r + rOffset + ") == " + value + ")"; break;
            default: break;
            }
        }
    } else if (p.size == 4) {
        action = "Memory(" + rOffset + ", " + modifier + ", " + value + ")";
    }
    return push(L, action), 1;
}

struct HostFunction {
    const char* name;
    lua_CFunction f;
};
const HostFunction kHostFunctions[] = {
    {"preDefine", host<fPreDefine>},
    {"mainPreDefine", host<fMainPreDefine>},
    {"onPluginText", host<fOnPluginText>},
    {"beforeText", host<fBeforeText>},
    {"afterText", host<fAfterText>},
    {"echo", host<fEcho>},
    {"GetBGMIndex", host<fGetBGMIndex>},
    {"GetReturnBGMIndex", host<fGetReturnBGMIndex>},
    {"ParseUnit", host<fParseUnit>},
    {"ParseWeapon", host<fParseWeapon>},
    {"ParseFlingy", host<fParseFlingy>},
    {"ParseSprites", host<fParseSprites>},
    {"ParseImages", host<fParseImages>},
    {"ParseUpgrades", host<fParseUpgrades>},
    {"ParseTechdata", host<fParseTechdata>},
    {"ParseOrders", host<fParseOrders>},
    {"ParseLocation", host<fParseLocation>},
    {"ParseSwitchName", host<fParseSwitchName>},
    {"ParseEUDScore", host<fParseEUDScore>},
    {"ParseSupplyType", host<fParseSupplyType>},
    {"ParseSCAScript", host<fParseSCAScript>},
    {"ParseSCAScriptVariable", host<fParseSCAScriptVariable>},
    {"GetEUDScoreOffset", host<fGetEUDScoreOffset>},
    {"GetSupplyOffset", host<fGetSupplyOffset>},
    {"AddMouseEvent", host<fAddMouseEvent>},
    {"AddMSQCPlugin", host<fAddMSQCPlugin>},
    {"AddChatEventPlugin", host<fAddChatEventPlugin>},
    {"IsNumber", host<fIsNumber>},
    {"DatOffset", host<fDatOffset>},
    {"GetDatFile", host<fGetDatFile>},
    {"SetDatFile", host<fSetDatFile>},
    {"ConditionDatFile", host<fConditionDatFile>},
};

// Opens the sandbox's libraries and the host functions (in lua_cpcall).
int openSandbox(lua_State* L) {
    const std::pair<const char*, lua_CFunction> libs[] = {
        {"", luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}};
    for (const auto& [name, open] : libs) {
        lua_pushcfunction(L, open);
        lua_pushstring(L, name);
        lua_call(L, 1, 0);
    }
    for (const char* name : {"dofile", "loadfile", "load", "loadstring", "collectgarbage", "gcinfo", "getfenv", "setfenv", "newproxy",
                             "print", "module", "require", "package", "io", "os", "debug"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_register(L, "loadstring", sandboxLoadstring);
    lua_register(L, "print", sandboxPrint);
    lua_register(L, "xpcall", sandboxXpcall);
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pushcfunction(L, sandboxRep);
    lua_setfield(L, -2, "rep");
    lua_pop(L, 1);
    for (const HostFunction& h : kHostFunctions) lua_register(L, h.name, h.f);
    return 0;
}

// One chunk of Lua text, run in lua_cpcall: its error (empty: it ran).
struct Chunk {
    const char* code = nullptr;
    size_t size = 0;
    const char* name = "=macro";
    bool binary = false;
    std::string error;
};

int runChunk(lua_State* L) {
    Chunk* c = static_cast<Chunk*>(lua_touserdata(L, 1));
    lua_pop(L, 1);
    if (c->size > 0 && c->code[0] == LUA_SIGNATURE[0]) {  // (lua_load's own test for a binary chunk)
        c->binary = true;
        return 0;
    }
    int st = luaL_loadbuffer(L, c->code, c->size, c->name);
    if (st == 0) st = lua_pcall(L, 0, 0, 0);
    if (st != 0) {
        const char* m = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "an error with no message";
        try {
            c->error = m && *m ? m : "an error with no message";
        } catch (...) {
            c->error = "?";
        }
        lua_pop(L, 1);
    }
    return 0;
}

}  // namespace

// --- Ee3Macros -----------------------------------------------------------------------------------------------------

Ee3Macros::Ee3Macros(const nrbf::Graph& g, const nrbf::Object* te, const ImportCodeContext& ctx) : s_(new State) {
    State& s = *s_;
    // The project: its BGM list, chat event addresses, MSQC settings (TriggerEditorData) and .dat fields (SCDatFiles).
    if (te) {
        for (const nrbf::Value& v : g.items(g.get(g.get(te, "BGMData"), "BGMList"))) s.bgmNames.push_back(g.text(g.get(v), "BGMName"));
        auto address = [&](const char* member, uint32_t& out) {
            const int64_t v = g.number(te, member);
            if (v != 0) out = uint32_t(v);
        };
        address("_addr", s.addr), address("_ptrAddr", s.ptrAddr), address("_patternAddr", s.patternAddr), address("_lenAddr", s.lenAddr);
        if (const nrbf::Value& v = g.value(te, "UseMSQC"); !v.isNull()) s.useMsqc = v.asBool();
        if (const nrbf::Value& v = g.value(te, "UseChatEvent"); !v.isNull()) s.useChat = v.asBool();
        s.mouseLocation = g.text(te, "MouseLocation");
        if (const nrbf::Object* sca = g.get(te, "SCArchive")) s.scaUsed = g.value(sca, "IsUsed").asBool();
    }
    if (const nrbf::Object* dat = g.get(g.root(), "Dat")) {
        for (const nrbf::Value& dv : g.items(g.get(dat, "Datfile"))) {
            const nrbf::Object* df = g.get(dv);
            auto& fields = s.datParams[g.text(df, "FIleName")];
            for (const nrbf::Value& pv : g.items(g.get(df, "Paramaters"))) {
                const nrbf::Object* p = g.get(pv);
                if (!p) continue;
                State::DatParam info;
                info.size = g.number(p, "Size"), info.start = g.number(p, "VarStart");
                const nrbf::Value& a = g.value(p, "VarArray");
                info.array = a.isNull() ? 1 : g.number(a);
                fields.emplace(g.text(p, "ParamaterName"), info);
            }
            s.datRead = true;
        }
    }
    // EUD Editor 3's Offset.txt: "name=0xHEX" lines.
    {
        const std::string text = kEe3Offsets;
        size_t at = 0;
        while (at < text.size()) {
            size_t e = text.find('\n', at);
            if (e == std::string::npos) e = text.size();
            std::string line = text.substr(at, e - at);
            at = e + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t eq = line.find('=');
            if (eq == std::string::npos || line.compare(eq + 1, 2, "0x") != 0) continue;
            s.offsets[line.substr(0, eq)] = uint32_t(std::strtoul(line.c_str() + eq + 3, nullptr, 16));
        }
    }
    // The map's names: ParseUnit's list (pjData.EngStat_txt: the map's own unit name, else stat_txt.tbl's), the locations
    // (MapData.LocationName: "" when it has none) and the switches ("Switch n" when it has none).
    {
        StatTxt stat;
        std::string error;
        const bool statLoaded = ctx.data && stat.load(*ctx.data, error);
        for (int i = 0; i < 228; ++i) {
            std::string name;
            if (size_t(i) < ctx.unitNames.size() && !ctx.unitNames[size_t(i)].empty()) name = ctx.unitNames[size_t(i)];
            else if (statLoaded && i < stat.count()) name = StatTxt::escape(stat.stock(i));
            else if (size_t(i) < std::size(kEudUnitNames) && kEudUnitNames[i]) name = kEudUnitNames[i];
            s.unitNames.push_back(name);
        }
        for (size_t i = 0; i < 256; ++i) {
            s.locationNames.push_back(i < ctx.locationNames.size() ? ctx.locationNames[i] : std::string());
            s.switchNames.push_back(i < ctx.switchNames.size() && !ctx.switchNames[i].empty() ? ctx.switchNames[i]
                                                                                              : "Switch " + std::to_string(i + 1));
        }
    }

    // The Lua: the sandbox, then EUD Editor 3's macro files.
    s.L = lua_newstate(luaAlloc, s_);
    if (!s.L) {
        s.dead = true;
        s.pending.push_back("the Lua macros cannot start (no memory)");
        return;
    }
    lua_atpanic(s.L, luaPanic);
    lua_sethook(s.L, countHook, LUA_MASKCOUNT, kHookEvery);
    s.limits.projectDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(kProjectTimeLimitSeconds);
    LimitsScope scope(&s.limits);
    try {
        char base = 0;
        s.limits.start(&base);
        if (lua_cpcall(s.L, openSandbox, nullptr) != 0) {
            s.dead = true;
            s.pending.push_back("the Lua macros cannot start");
            return;
        }
        for (const Ee3LuaFile& f : kEe3LuaFiles) {
            const std::string name = std::string("=") + f.name;
            Chunk c;
            c.code = f.text, c.size = f.size, c.name = name.c_str();
            s.limits.start(&base);
            if (lua_cpcall(s.L, runChunk, &c) != 0) c.error = "it does not run";
            if (!c.error.empty()) s.pending.push_back("EUD Editor 3's macro file " + std::string(f.name) + ": " + c.error);
        }
    } catch (const LuaPanic& e) {
        s.dead = true;
        s.pending.push_back(e.what());
    }
}

Ee3Macros::~Ee3Macros() {
    if (s_->L) {
        s_->limits.stop("closed");  // (no more Lua runs while the state closes)
        LimitsScope scope(&s_->limits);
        try {
            lua_close(s_->L);
        } catch (...) {
        }
    }
    delete s_;
}

namespace {

std::string shorten(const std::string& s, size_t n = 80) {
    std::string t = s;
    for (char& c : t)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = c == '\r' || c == '\n' || c == '\t' ? ' ' : '?';  // (one line, no control bytes)
    if (t.size() > n) {
        size_t cut = n;
        while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;  // (not inside a UTF-8 character)
        t = t.substr(0, cut) + "...";
    }
    return t;
}

}  // namespace

bool Ee3Macros::apply(const std::string& text, bool isMain, const std::string& fileName, std::string& out, std::vector<std::string>& problems) {
    State& s = *s_;
    out = text;
    for (const std::string& p : s.pending) problems.push_back(p);
    s.pending.clear();
    const std::string where = fileName.empty() ? std::string() : fileName + ": ";
    if (s.dead) {
        problems.push_back(where + "the Lua macros are not expanded (Lua stopped)");
        return false;
    }
    if (text.size() > kTextLimit) {
        problems.push_back(where + "the text is longer than 64 MB: its macros are not expanded");
        return false;
    }
    LimitsScope scope(&s.limits);
    char base = 0;
    s.limits.start(&base);  // (one budget for all the blocks of the script)
    lua_sethook(s.L, countHook, LUA_MASKCOUNT, kHookEvery);  // (once: setting it again would start its count again)
    uint64_t scanned = 0;
    auto scan = [&](size_t n) {
        scanned += n;
        if (scanned > kScanLimit) throw HostError("the text has too many macros to expand");
        if (s.limits.timeOver()) throw HostError(s.limits.why);
    };
    // Runs Lua text; its error, empty when it ran. A passed limit is an error also when the Lua ended first (a search
    // that the limit stopped found nothing: its result is wrong).
    auto run = [&](const std::string& code) {
        Chunk c;
        c.code = code.data(), c.size = code.size();
        s.echo.clear();
        if (s.limits.timeOver()) return "stopped: " + s.limits.why;  // (blocks that fail, or that are short, also use the time)
        if (lua_cpcall(s.L, runChunk, &c) != 0) c.error = s.limits.memoryRefused ? "not enough memory" : "it cannot run";
        if (c.binary) return std::string("it is a binary Lua chunk, which is not run");
        if (c.error.empty() && s.limits.over) c.error = "stopped: " + s.limits.why;
        if (!c.error.empty() && s.limits.memoryRefused) c.error += " (the macros may use 64 MB)";
        if (!c.error.empty() && s.limits.over && c.error.find("stopped: ") == std::string::npos) c.error += " (stopped: " + s.limits.why + ")";
        return c.error;
    };
    bool ok = true;
    try {
        std::string rstr = text;
        if (const std::string e = run("LuaPlayerVariable = \"getcurpl()\""); !e.empty()) {
            problems.push_back(where + "the Lua macros cannot start this file: " + e + " (EUD Editor 3's build stops on it too)");
            for (const std::string& p : s.pending) problems.push_back(p);
            s.pending.clear();
            return false;
        }

        // "@Name(args)" -> "<?Name(args)?>": the regex @[\w_]+[\w\d_]+ (two or more word characters), then the brackets.
        std::vector<std::string> shortMacro;
        for (size_t i = rstr.find('@'); i != std::string::npos;) {
            size_t e = i + 1, chars = 0;
            for (size_t n; e < rstr.size() && (n = wordChar(rstr, e)) > 0; e += n) ++chars;
            if (chars < 2) {
                i = rstr.find('@', i + 1);
                continue;
            }
            const std::string funcname = rstr.substr(i + 1, e - i - 1);
            if (e >= rstr.size()) {
                // (VB: rstr(index) past the end throws, and EUD Editor 3 writes no file.)
                problems.push_back(where + "the text ends with \"@" + funcname + "\", on which EUD Editor 3's build stops: the file is left as written");
                return false;
            }
            if (rstr[e] == '(') {
                int braces = 0;
                size_t k = e;
                for (; k < rstr.size(); ++k) {
                    if (rstr[k] == '(') ++braces;
                    if (rstr[k] == ')') --braces;
                    if (braces == 0) break;
                }
                const size_t end = std::min(k + 1, rstr.size());
                scan(end - e);
                shortMacro.push_back(funcname + rstr.substr(e, end - e));
            }
            i = rstr.find('@', e);
        }
        for (const std::string& t : shortMacro) {
            scan(t.size());
            size_t index = 0;
            std::string funcname;
            while (index < t.size() && t[index] != '(') funcname += t[index++];
            std::vector<std::string> args;
            std::string argcontent;
            int braceCount = 0;
            bool sentence = false;
            for (; index < t.size(); ++index) {
                const char c = t[index];
                if (braceCount >= 1 && c == '"') {
                    if (sentence) {
                        if (!(index > 0 && t[index - 1] == '\\')) sentence = false;
                    } else {
                        sentence = true;
                    }
                }
                if (!sentence) {
                    if (c == '(') {
                        ++braceCount;
                        if (braceCount == 1) continue;
                    }
                    if (c == ')') {
                        --braceCount;
                        if (braceCount == 0) {
                            args.push_back(trim(argcontent));
                            break;
                        }
                    }
                    if (braceCount == 1 && c == ',') {
                        args.push_back(trim(argcontent));
                        argcontent.clear();
                    }
                }
                if (sentence || braceCount != 1 || c != ',') argcontent += c;
            }
            std::string rctext;
            for (std::string a : args) {
                if (a.empty()) {
                    rctext = funcname + "(";
                    break;
                }
                bool quote = a.size() <= 2;
                if (a.front() != '"' && a.back() != '"') quote = true;
                rctext += rctext.empty() ? funcname + "(" : ", ";
                if (quote && a.front() == '@') a = a.substr(1), quote = false;
                rctext += quote ? "\"" + a + "\"" : a;
            }
            rctext += ")";
            const std::string find = "@" + t;
            const size_t at = rstr.find(find);
            scan(at == std::string::npos ? rstr.size() : at + find.size());
            if (at != std::string::npos) rstr.replace(at, find.size(), "<?" + rctext + "?>");
        }

        // Each "<? ... ?>" (the regex (<\?|<\?php)(.+?)\?>, Singleline: at least one character between) in Lua.
        std::vector<std::pair<std::string, size_t>> blocks;  // the text and its place
        for (size_t at = rstr.find("<?"); at != std::string::npos;) {
            const size_t e = rstr.find("?>", at + 3);
            if (e == std::string::npos) break;
            blocks.push_back({rstr.substr(at, e + 2 - at), at});
            at = rstr.find("<?", e + 2);
        }
        // (The line numbers: counted on from the last block, in the text before the blocks run.)
        const std::string original = rstr;
        size_t linePlace = 0, line = 1, failed = 0;
        s.preDefine.clear();
        for (const auto& [truestr, place] : blocks) {
            const std::string code = truestr.substr(2, truestr.size() - 4);
            const std::string e = run(code);
            for (const std::string& p : s.pending) problems.push_back(where + p);
            s.pending.clear();
            if (!e.empty()) {
                ok = false;
                if (++failed > kProblemLines) continue;
                line += size_t(std::count(original.begin() + std::min(linePlace, original.size()), original.begin() + std::min(place, original.size()), '\n'));
                linePlace = std::max(linePlace, place);
                problems.push_back(where + "line " + std::to_string(line) + ": the Lua macro <?" + shorten(code) + "?> fails: " + shorten(e, 300) +
                                   "; it stays as written");
                continue;
            }
            const size_t at = rstr.find(truestr);
            scan(at == std::string::npos ? rstr.size() : at + truestr.size());
            if (at != std::string::npos) rstr.replace(at, truestr.size(), s.echo);
        }
        s.echo.clear();
        s.echo.shrink_to_fit();
        if (failed > kProblemLines)
            problems.push_back(where + std::to_string(failed - kProblemLines) + " more Lua macros fail; they stay as written");

        for (const std::string& p : s.preDefine.items) rstr = p + "\r\n" + rstr;
        if (isMain) {
            for (const std::string& p : s.mainPreDefine.items)
                if (!s.preDefine.contains(p)) rstr = p + "\r\n" + rstr;
            const std::pair<const char*, const OrderedSet*> funcs[] = {
                {"onPluginStart", &s.onPlugin}, {"beforeTriggerExec", &s.before}, {"afterTriggerExec", &s.after}};
            for (const auto& [name, set] : funcs) {
                std::string temp;
                for (const std::string& t : set->items) temp += "\t" + t + "\r\n";
                size_t end = 0;
                if (findFunction(rstr, name, end) != std::string::npos) {
                    rstr.insert(end, "\r\n" + temp);
                } else {
                    rstr += "\r\nfunction " + std::string(name) + "(){\r\n";
                    rstr += temp + "\r\n";
                    rstr += "}\r\n";
                }
            }
        }
        out = std::move(rstr);
    } catch (const LuaPanic& e) {
        s.dead = true;
        problems.push_back(where + e.what());
        return false;
    } catch (const std::exception& e) {
        problems.push_back(where + "the macros are not expanded: " + e.what());
        out = text;
        return false;
    }
    return ok;
}

std::vector<std::string> Ee3Macros::msqcLines() const {
    const State& s = *s_;
    if (!s.useMsqc) return {};
    std::string r;
    for (const auto& [key, value] : s.msqc) {
        const size_t first = value.find(','), last = value.rfind(',');
        const std::string cond = value.substr(0, first), vname = value.substr(last + 1);
        r += (cond.empty() ? std::string() : cond + " ; ") + key + " : " + vname + ", 1\r\n";
    }
    for (size_t i = 0; i < s.chats.items.size(); ++i)
        r += "0x" + hexUpper(isChatPattern(s.chats.items[i]) ? s.patternAddr : s.addr) + ", Exactly, " + std::to_string(i + 2) +
             " : VChatIndex, " + std::to_string(i + 1) + "\r\n";
    for (size_t i = 0; i < s.mouse.items.size(); ++i) r += s.mouse.items[i] + " : VMouseClickIndex" + std::to_string(i) + ", 1\r\n";
    if (!s.mouseLocation.empty()) r += "mouse : " + s.mouseLocation + "\r\n";
    std::vector<std::string> lines;
    for (size_t at = 0; at < r.size();) {
        const size_t e = r.find("\r\n", at);
        lines.push_back(r.substr(at, e - at));
        at = e + 2;
    }
    return lines;
}

std::vector<std::string> Ee3Macros::chatEventLines() const {
    const State& s = *s_;
    if (!s.useChat) return {};
    std::vector<std::string> lines = {"__addr__ : 0x" + hexUpper(s.addr), "__ptrAddr__ : 0x" + hexUpper(s.ptrAddr),
                                      "__patternAddr__ : 0x" + hexUpper(s.patternAddr), "__lenAddr__ : 0x" + hexUpper(s.lenAddr)};
    for (size_t i = 0; i < s.chats.items.size(); ++i) lines.push_back(s.chats.items[i] + " : " + std::to_string(i + 2));
    return lines;
}

std::string Ee3Macros::pluginVariables() const {
    const State& s = *s_;
    std::string sb;
    auto line = [&](const std::string& t) { sb += t + "\r\n"; };
    auto vnameOf = [](const std::string& value) { return value.substr(value.rfind(',') + 1); };
    line("from eudplib import *");
    if (s.scaUsed) line("import TriggerEditor.SCArchive as sca");
    line("");
    line("");
    line("VChatIndex = EUDArray(8)");
    for (size_t i = 0; i < s.mouse.items.size(); ++i) line("VMouseClickIndex" + std::to_string(i) + " = EUDArray(8)");
    for (const auto& [key, value] : s.msqc) line(vnameOf(value) + " = EUDArray(8)");
    line("");
    line("def Reg():");
    line("    print('... TERegVar ...')");
    for (const auto& [key, value] : s.msqc) line("    EUDRegisterObjectToNamespace(\"" + vnameOf(value) + "\", " + vnameOf(value) + ")");
    line("    EUDRegisterObjectToNamespace(\"VChatIndex\", VChatIndex)");
    for (size_t i = 0; i < s.mouse.items.size(); ++i) {
        const std::string v = "VMouseClickIndex" + std::to_string(i);
        line("    EUDRegisterObjectToNamespace(\"" + v + "\", " + v + ")");
    }
    if (s.scaUsed) line("    sca.Init()");
    line("EUDOnStart(Reg)");
    return sb;
}

}  // namespace stardraft
