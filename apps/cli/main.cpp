// stardraft-cli: the map editor without a window, for scripts and agents (docs/dev/CLI.md).
//
// A map is edited in a working folder next to it (<map>.work): `open` makes it, each `apply` is one undo step in it,
// `save` writes the map, `close` removes the folder. Every command writes one JSON object on stdout.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "install.h"  // apps/common: the registry's install folders (Windows)
#include "json.h"     // src: the library's JSON reader and writer
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/map_file.h"
#include "stardraft/settings.h"
#include "text_codec.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace stardraft;

#include "../editor/core/install_search.inc"

namespace {

// --- Errors and output ---------------------------------------------------------------------------------------------

struct CliError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
[[noreturn]] void fail(const std::string& what) { throw CliError(what); }

Json jnum(double n) {
    Json j;
    j.type = Json::Type::Number, j.n = n;
    return j;
}
Json jbool(bool b) {
    Json j;
    j.type = Json::Type::Bool, j.b = b;
    return j;
}
// Map strings have no fixed encoding: UTF-8 stays as it is, other text is read as CP949 or Windows-1252, as the
// editor reads it.
Json jstr(const std::string& s) {
    Json j;
    j.type = Json::Type::String;
    j.s = text::narrow(text::widenLegacy(s));
    return j;
}
Json jarr() {
    Json j;
    j.type = Json::Type::Array;
    return j;
}
struct Obj {
    Json j;
    Obj() { j.type = Json::Type::Object; }
    explicit Obj(Json from) : j(std::move(from)) {}
    Obj& add(const std::string& k, Json v) {
        j.fields.push_back({k, std::move(v)});
        return *this;
    }
    Obj& add(const std::string& k, double n) { return add(k, jnum(n)); }
    Obj& add(const std::string& k, int n) { return add(k, jnum(n)); }
    Obj& add(const std::string& k, const std::string& s) { return add(k, jstr(s)); }
    Obj& add(const std::string& k, const char* s) { return add(k, jstr(s)); }
    Obj& add(const std::string& k, bool b) { return add(k, jbool(b)); }
    operator Json() const { return j; }
};
Json jrect(int l, int t, int r, int b) {
    Json a = jarr();
    for (int v : {l, t, r, b}) a.items.push_back(jnum(v));
    return a;
}

// --- Paths and files -----------------------------------------------------------------------------------------------

fs::path upath(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string ustr(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Bytes readBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) fail("cannot read " + ustr(p));
    return Bytes(std::istreambuf_iterator<char>(in), {});
}
void writeBytes(const fs::path& p, const Bytes& b) {
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
        if (!out) fail("cannot write " + ustr(tmp));
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) fail("cannot write " + ustr(p) + ": " + ec.message());
}
std::string readText(const fs::path& p) {
    const Bytes b = readBytes(p);
    return std::string(b.begin(), b.end());
}
Json parseJson(const std::string& text, const std::string& what) {
    JsonParser p{text};
    Json j = p.value();
    p.ws();
    if (!p.ok || p.i != text.size()) fail(what + " is not valid JSON (near character " + std::to_string(p.i) + ")");
    return j;
}

// --- Options ---------------------------------------------------------------------------------------------------

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;  // --name value (flags: "")
    bool has(const std::string& k) const { return options.count(k) != 0; }
    std::string get(const std::string& k, const std::string& def = {}) const {
        auto it = options.find(k);
        return it == options.end() ? def : it->second;
    }
};

// The options that take no value.
bool isFlag(const std::string& name) {
    for (const char* f : {"timing", "discard", "reopen", "force", "dry-run", "locations", "no-units", "no-sprites", "help"})
        if (name == f) return true;
    return false;
}

std::vector<double> numbers(const std::string& text, size_t count, const std::string& what) {
    std::vector<double> out;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, ',')) {
        char* end = nullptr;
        const double v = std::strtod(part.c_str(), &end);
        if (part.empty() || *end || !std::isfinite(v)) fail(what + ": \"" + part + "\" is not a number");
        out.push_back(v);
    }
    if (out.size() != count) fail(what + " needs " + std::to_string(count) + " numbers separated by commas");
    return out;
}

// --- The game data -------------------------------------------------------------------------------------------------

fs::path envPath(const char* name) {
#ifdef _WIN32
    const std::wstring wname(name, name + std::strlen(name));
    wchar_t buf[32768];
    const DWORD n = GetEnvironmentVariableW(wname.c_str(), buf, DWORD(std::size(buf)));
    if (n > 0 && n < std::size(buf)) return fs::path(std::wstring(buf, n));
#else
    if (const char* v = std::getenv(name); v && *v) return fs::path(v);
#endif
    return {};
}

fs::path settingsFile() {
    if (fs::path f = envPath("STARDRAFT_SETTINGS"); !f.empty()) return f;
#if defined(_WIN32)
    if (fs::path a = envPath("APPDATA"); !a.empty()) return a / "StarDraft" / "settings.json";
#elif defined(__APPLE__)
    if (fs::path h = envPath("HOME"); !h.empty()) return h / "Library" / "Application Support" / "StarDraft" / "settings.json";
#else
    if (fs::path x = envPath("XDG_CONFIG_HOME"); !x.empty()) return x / "StarDraft" / "settings.json";
    if (fs::path h = envPath("HOME"); !h.empty()) return h / ".config" / "StarDraft" / "settings.json";
#endif
    return {};
}

// The StarCraft install, found as the editor finds it: --install, STARDRAFT_INSTALL, the editor's setting, then the
// usual folders of this system.
void openGameData(GameData& data, const std::string& given) {
    std::vector<fs::path> candidates;
    if (!given.empty()) candidates.push_back(upath(given));
    if (fs::path e = envPath("STARDRAFT_INSTALL"); !e.empty()) candidates.push_back(e);
    if (fs::path f = settingsFile(); !f.empty()) {
        Settings s(f);
        if (auto t = s.text("StarCraftFolder"); t && !t->empty()) candidates.push_back(upath(*t));
    }
    for (const fs::path& p : platform::registryInstallFolders()) candidates.push_back(p);
#if defined(_WIN32)
    for (const fs::path& p : installCandidatesOf(InstallSystem::Windows, {})) candidates.push_back(p);
#elif defined(__APPLE__)
    for (const fs::path& p : installCandidatesOf(InstallSystem::Mac, envPath("HOME"))) candidates.push_back(p);
#else
    for (const fs::path& p : installCandidatesOf(InstallSystem::Linux, envPath("HOME"))) candidates.push_back(p);
#endif
    std::string firstError;
    std::error_code ec;
    for (const fs::path& c : candidates)
        for (const fs::path& t : installTries(c)) {
            if (!fs::is_directory(t, ec)) continue;
            std::string err;
            if (data.open(t, err)) return;
            if (firstError.empty()) firstError = ustr(t) + ": " + err;
        }
    fail("no StarCraft: Remastered install found (give --install <folder> or set STARDRAFT_INSTALL)" +
         (firstError.empty() ? std::string() : "; " + firstError));
}

// --- The working folder --------------------------------------------------------------------------------------------

// <map>.work/work.json and steps/NNNN.chk: the map as it was opened (step 0) and after each apply. `step` is the step
// the map is at now (undo goes back, redo forward). Each step has a change number; `apply --expect` compares it.
struct Work {
    fs::path dir, source;
    struct Step {
        uint64_t change = 0;
        std::string what;
    };
    std::vector<Step> steps;
    size_t step = 0;
    uint64_t nextChange = 1, savedChange = 0;
    std::string stamp;  // the map file's size and time at open (or the last save): save refuses when it changed

    // The map file as it was opened: save takes its other files (sounds and the like) from this copy.
    fs::path sourceCopy() const { return dir / ("source" + ustr(source.extension())); }

    uint64_t change() const { return steps[step].change; }
    bool modified() const { return change() != savedChange; }
    fs::path stepFile(size_t i) const {
        char name[32];
        std::snprintf(name, sizeof name, "%04zu.chk", i);
        return dir / "steps" / name;
    }
    fs::path current() const { return stepFile(step); }

    void load() {
        const Json j = parseJson(readText(dir / "work.json"), ustr(dir / "work.json"));
        source = upath(j.str("source"));
        steps.clear();
        if (const Json* s = j.get("steps"))
            for (const Json& e : s->items) steps.push_back({uint64_t(e.integer("change", 0)), e.str("what")});
        step = size_t(j.integer("step", 0));
        nextChange = uint64_t(j.integer("next_change", 1));
        savedChange = uint64_t(j.integer("saved_change", 0));
        stamp = j.str("stamp");
        if (steps.empty() || step >= steps.size()) fail(ustr(dir / "work.json") + " is damaged");
    }
    void save() const {
        Json list = jarr();
        for (const Step& s : steps) list.items.push_back(Obj().add("change", double(s.change)).add("what", s.what));
        const Json j = Obj()
                           .add("version", 1)
                           .add("source", ustr(source))
                           .add("step", int(step))
                           .add("next_change", double(nextChange))
                           .add("saved_change", double(savedChange))
                           .add("stamp", stamp)
                           .add("steps", list);
        const std::string t = jsonText(j) + "\n";
        writeBytes(dir / "work.json", Bytes(t.begin(), t.end()));
    }
    Json info() const {
        return Obj()
            .add("work", ustr(dir))
            .add("change", double(change()))
            .add("modified", modified())
            .add("undo_steps", int(step))
            .add("redo_steps", int(steps.size() - 1 - step));
    }
};

// What a command works on: a map (and its working folder when it is open) or a working folder.
struct Target {
    fs::path map, work;
    bool open = false;
};
Target resolve(const std::string& arg) {
    Target t;
    fs::path p = fs::absolute(upath(arg));
    std::error_code ec;
    if (fs::is_directory(p, ec) && fs::exists(p / "work.json", ec)) {
        Work w;
        w.dir = p;
        w.load();
        t.map = w.source, t.work = p, t.open = true;
        return t;
    }
    t.map = p;
    t.work = fs::path(p.native() + fs::path(".work").native());
    t.open = fs::exists(t.work / "work.json", ec);
    return t;
}
Work loadWork(const Target& t) {
    if (!t.open) fail(ustr(t.map) + " is not open: run `stardraft-cli open` first");
    Work w;
    w.dir = t.work;
    w.load();
    return w;
}

// A file's size and time, to tell whether another program changed it.
std::string fileStamp(const fs::path& p) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) return "missing";
    const auto time = fs::last_write_time(p, ec).time_since_epoch().count();
    return std::to_string(size) + ":" + std::to_string(time);
}

bool samePath(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::exists(a, ec) && fs::exists(b, ec)) return fs::equivalent(a, b, ec);
    return a.lexically_normal() == b.lexically_normal();
}

bool isChk(const fs::path& p) { return lower(ustr(p.extension())) == ".chk"; }

// The scenario of a map file (.scm, .scx or .chk).
Bytes readScenario(const fs::path& map) {
    if (isChk(map)) return readBytes(map);
    Bytes raw;
    std::string err;
    if (!readMapChk(map, raw, err)) fail(err);
    return raw;
}

// --- Names ---------------------------------------------------------------------------------------------------------

const IsomTerrainTable* terrainTable(const Editor& ed) { return IsomTerrainTable::forEra(ed.era()); }

uint16_t terrainId(const Editor& ed, const Json& v, const char* what) {
    const IsomTerrainTable* t = terrainTable(ed);
    if (!t) fail("this tileset has no terrain table");
    if (v.type == Json::Type::Number) {
        if (!t->isTerrain(uint16_t(v.n))) fail(std::string(what) + ": " + std::to_string(int(v.n)) + " is not a terrain of this tileset");
        return uint16_t(v.n);
    }
    for (uint16_t i = 1; i <= t->terrainCount(); ++i)
        if (t->isTerrain(i) && lower(t->terrainName(i)) == lower(v.s)) return i;
    fail(std::string(what) + ": no terrain \"" + v.s + "\" in this tileset (see `get <map> terrains`)");
}

uint16_t unitTypeId(const Editor& ed, const Json& v, const char* what) {
    if (v.type == Json::Type::Number) {
        if (v.n < 0 || v.n >= UnitTypes::kCount) fail(std::string(what) + ": no unit type " + std::to_string(int(v.n)));
        return uint16_t(v.n);
    }
    if (auto id = ed.unitTypes().findByName(v.s)) return *id;
    fail(std::string(what) + ": no unit type \"" + v.s + "\" (see `get <map> unit-types`)");
}

std::string doodadName(const Editor& ed, const DoodadType& t) {
    return t.groupString ? ed.unitTypes().statText(t.groupString - 1) : std::string();
}

const char* controllerName(Editor::Controller c) {
    switch (c) {
    case Editor::Controller::Inactive: return "inactive";
    case Editor::Controller::Rescue: return "rescue";
    case Editor::Controller::Computer: return "computer";
    case Editor::Controller::Human: return "human";
    case Editor::Controller::Neutral: return "neutral";
    case Editor::Controller::Closed: return "closed";
    }
    return "unknown";
}
const char* raceName(Editor::Race r) {
    switch (r) {
    case Editor::Race::Zerg: return "zerg";
    case Editor::Race::Terran: return "terran";
    case Editor::Race::Protoss: return "protoss";
    case Editor::Race::UserSelect: return "user_select";
    case Editor::Race::Random: return "random";
    }
    return "unknown";
}

// --- Reading the map (get) ---------------------------------------------------------------------------------------

Json playerJson(const Editor& ed, int p) {
    const Editor::Player pl = ed.player(p);
    Obj o;
    o.add("player", p + 1).add("controller", controllerName(pl.controller)).add("race", raceName(pl.race)).add("force", pl.force + 1);
    switch (pl.color.kind) {
    case Editor::PlayerColor::Random: o.add("color", "random"); break;
    case Editor::PlayerColor::Choice: o.add("color", "choice"); break;
    case Editor::PlayerColor::Custom: {
        char b[16];
        std::snprintf(b, sizeof b, "#%06X", unsigned(pl.color.rgb));
        o.add("color", b);
        break;
    }
    case Editor::PlayerColor::Id: o.add("color", Editor::colorIdName(pl.color.id)); break;
    }
    return o;
}
Json forceJson(const Editor& ed, int f) {
    const Editor::Force fo = ed.force(f);
    return Obj()
        .add("force", f + 1)
        .add("name", fo.name)
        .add("random_start", (fo.flags & Editor::RandomStart) != 0)
        .add("allies", (fo.flags & Editor::Allies) != 0)
        .add("allied_victory", (fo.flags & Editor::AlliedVictory) != 0)
        .add("shared_vision", (fo.flags & Editor::SharedVision) != 0);
}

Json summary(const Editor& ed) {
    int used = 0;
    for (int i = 0; i < ed.locationCount(); ++i) used += ed.locationUsed(i) ? 1 : 0;
    Json players = jarr(), forces = jarr(), problems = jarr();
    for (int p = 0; p < 8; ++p) players.items.push_back(playerJson(ed, p));
    for (int f = 0; f < 4; ++f) forces.items.push_back(forceJson(ed, f));
    for (const std::string& s : ed.playabilityProblems()) problems.items.push_back(jstr(s));
    return Obj()
        .add("name", ed.scenarioName())
        .add("description", ed.scenarioDescription())
        .add("tileset", tilesetName(ed.era()))
        .add("width", int(ed.width()))
        .add("height", int(ed.height()))
        .add("pixels", jrect(0, 0, ed.width() * 32, ed.height() * 32))
        .add("units", int(ed.units().size()))
        .add("doodads", int(ed.doodads().size()))
        .add("sprites", int(ed.sprites().size()))
        .add("locations", used)
        .add("players", players)
        .add("forces", forces)
        .add("problems", problems);
}

Json unitJson(const Editor& ed, const Unit& u) {
    return Obj()
        .add("serial", double(u.serial))
        .add("type", int(u.type))
        .add("name", u.type < ed.unitTypes().size() ? ed.unitTypes()[u.type].name : std::string())
        .add("player", u.owner + 1)
        .add("x", int(u.x))
        .add("y", int(u.y))
        .add("hp", int(u.hitPoints))
        .add("shields", int(u.shields))
        .add("energy", int(u.energy))
        .add("resources", double(u.resources))
        .add("hangar", int(u.hangar));
}

Json getWhat(Editor& ed, const std::string& what, int step) {
    if (what == "summary") return summary(ed);
    Json a = jarr();
    if (what == "units") {
        for (size_t i = 0; i < ed.units().size(); ++i) {
            Json u = unitJson(ed, ed.units()[i]);
            u.fields.insert(u.fields.begin(), {"index", jnum(double(i))});
            a.items.push_back(u);
        }
    } else if (what == "doodads") {
        for (size_t i = 0; i < ed.doodads().size(); ++i) {
            const Doodad& d = ed.doodads()[i];
            const DoodadType* t = ed.doodadTypes().find(d.id);
            Obj o;
            o.add("index", int(i)).add("id", int(d.id)).add("name", t ? doodadName(ed, *t) : std::string()).add("x", int(d.x)).add("y", int(d.y));
            if (t) o.add("width", int(t->width)).add("height", int(t->height));
            a.items.push_back(o);
        }
    } else if (what == "sprites") {
        for (size_t i = 0; i < ed.sprites().size(); ++i) {
            const MapSprite& s = ed.sprites()[i];
            a.items.push_back(Obj()
                                  .add("index", int(i))
                                  .add("id", int(s.id))
                                  .add("x", int(s.x))
                                  .add("y", int(s.y))
                                  .add("player", s.owner + 1)
                                  .add("as_unit", !(s.flags & SpriteFlag::DrawAsSprite))
                                  .add("doodad_overlay", ed.spriteIsDoodadOverlay(int(i))));
        }
    } else if (what == "locations") {
        for (int i = 0; i < ed.locationCount(); ++i) {
            if (!ed.locationUsed(i)) continue;
            const Rect r = ed.locationEdges(i);
            a.items.push_back(Obj()
                                  .add("location", i + 1)
                                  .add("name", ed.locationName(i))
                                  .add("area", jrect(r.left, r.top, r.right, r.bottom))
                                  .add("excluded_elevations", int(ed.locationElevation(i))));
        }
    } else if (what == "players") {
        for (int p = 0; p < 8; ++p) a.items.push_back(playerJson(ed, p));
    } else if (what == "forces") {
        for (int f = 0; f < 4; ++f) a.items.push_back(forceJson(ed, f));
    } else if (what == "terrains") {
        if (const IsomTerrainTable* t = terrainTable(ed))
            for (uint16_t i = 1; i <= t->terrainCount(); ++i)
                if (t->isTerrain(i)) a.items.push_back(Obj().add("id", int(i)).add("name", t->terrainName(i)));
    } else if (what == "unit-types") {
        for (size_t i = 0; i < ed.unitTypes().size(); ++i) {
            const UnitType& t = ed.unitTypes()[uint16_t(i)];
            a.items.push_back(Obj().add("id", int(i)).add("name", t.name).add("group", t.editorGroup));
        }
    } else if (what == "doodad-types") {
        for (const DoodadType& t : ed.doodadTypes().all())
            if (t.inPalette)
                a.items.push_back(Obj().add("id", int(t.id)).add("name", doodadName(ed, t)).add("width", int(t.width)).add("height", int(t.height)));
    } else if (what == "terrain") {
        // The terrain (ISOM id) at every `step` px, row by row: a coarse picture of the ground.
        Json rows = jarr();
        for (int y = 0; y < ed.height() * 32; y += step) {
            Json row = jarr();
            for (int x = 0; x < ed.width() * 32; x += step) row.items.push_back(jnum(ed.terrainAt(x, y)));
            rows.items.push_back(row);
        }
        return Obj().add("step", step).add("rows", rows);
    } else {
        fail("get: unknown part \"" + what + "\"");
    }
    return a;
}

// --- Editing (apply) -----------------------------------------------------------------------------------------------

const Json& need(const Json& op, const char* k) {
    const Json* v = op.get(k);
    if (!v || v->type == Json::Type::Null) fail(std::string("missing \"") + k + "\"");
    return *v;
}
double num(const Json& op, const char* k) {
    const Json& v = need(op, k);
    if (v.type != Json::Type::Number) fail(std::string("\"") + k + "\" must be a number");
    return v.n;
}
double num(const Json& op, const char* k, double def) { return op.get(k) ? num(op, k) : def; }
std::vector<double> nums(const Json& v, const char* k) {
    if (v.type != Json::Type::Array) fail(std::string("\"") + k + "\" must be a list of numbers");
    std::vector<double> out;
    for (const Json& e : v.items) {
        if (e.type != Json::Type::Number) fail(std::string("\"") + k + "\" must be a list of numbers");
        out.push_back(e.n);
    }
    return out;
}
Rect area(const Json& op, const char* k) {
    const std::vector<double> v = nums(need(op, k), k);
    if (v.size() != 4) fail(std::string("\"") + k + "\" must be [left, top, right, bottom]");
    return Rect{int(v[0]), int(v[1]), int(v[2]), int(v[3])};
}
int player(const Json& op, const char* k, int maxPlayer) {
    const int p = int(num(op, k));
    if (p < 1 || p > maxPlayer) fail(std::string("\"") + k + "\" must be 1-" + std::to_string(maxPlayer));
    return p - 1;
}

std::vector<int> indices(const Json& op, const char* k, size_t count) {
    std::vector<int> out;
    for (double v : nums(need(op, k), k)) {
        if (v < 0 || v >= double(count)) fail(std::string("\"") + k + "\": no index " + std::to_string(int(v)));
        out.push_back(int(v));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());  // (the editor's deletes take each index once)
    return out;
}
int location(const Editor& ed, const Json& op) {
    const int i = int(num(op, "location")) - 1;
    if (i < 0 || i >= ed.locationCount() || !ed.locationUsed(i)) fail("no location " + std::to_string(i + 1));
    return i;
}
// Units by serial ("units", as `get units` gives them) or by index ("unit_indices": for maps whose units share a
// serial): their indices now.
std::vector<int> unitIndices(const Editor& ed, const Json& op) {
    if (op.get("unit_indices")) return indices(op, "unit_indices", ed.units().size());
    std::vector<int> out;
    for (double s : nums(need(op, "units"), "units")) {
        int found = -1, count = 0;
        for (size_t i = 0; i < ed.units().size(); ++i)
            if (ed.units()[i].serial == uint32_t(s)) found = int(i), ++count;
        if (found < 0) fail("no unit with serial " + std::to_string(uint64_t(s)));
        if (count > 1) fail(std::to_string(count) + " units have serial " + std::to_string(uint64_t(s)) + ": name them by \"unit_indices\"");
        out.push_back(found);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
void selectUnits(Editor& ed, const std::vector<int>& units) {
    ed.clearSelection();
    for (int i : units) ed.select(i, true);
}

Brush brush(const Json& op) {
    Brush b;
    if (const Json* j = op.get("brush")) {
        const std::string shape = lower(j->str("shape", "diamond"));
        if (shape == "square") b.shape = Brush::Square;
        else if (shape == "circle") b.shape = Brush::Circle;
        else if (shape != "diamond") fail("brush shape must be diamond, square or circle");
        b.size = float(num(*j, "size", 1));
    }
    return b;
}

Editor::SelectShape selectShape(const Json& op) {
    const std::string s = lower(op.str("shape", "rect"));
    if (s == "diamond") return Editor::SelectShape::Diamond;
    if (s == "ellipse") return Editor::SelectShape::Ellipse;
    if (s != "rect") fail("shape must be rect, diamond or ellipse");
    return Editor::SelectShape::Rect;
}

// One operation; returns what it did (an object) or fails.
Json applyOp(Editor& ed, const Json& op) {
    const std::string name = op.str("op");
    Obj r;
    if (name == "place_unit") {
        const uint16_t type = unitTypeId(ed, need(op, "type"), "type");
        const PlaceResult p = ed.placeUnit(type, uint8_t(player(op, "player", 12)), int(num(op, "x")), int(num(op, "y")), op.boolean("correct", true));
        if (!p.placed) {
            std::string why;
            for (PlacementIssue i : p.issues) why += (why.empty() ? "" : ", ") + std::string(placementIssueName(i));
            fail("the unit does not fit there" + (why.empty() ? std::string() : " (" + why + ")"));
        }
        const Unit& u = ed.units()[size_t(p.index)];
        return r.add("serial", double(u.serial)).add("x", int(u.x)).add("y", int(u.y));
    }
    if (name == "move_units") {
        const std::vector<int> units = unitIndices(ed, op);
        int dx = int(num(op, "dx", 0)), dy = int(num(op, "dy", 0));
        if (op.get("x") || op.get("y")) {  // to a place: the first unit goes there, the others keep their offsets
            if (units.empty()) fail("no units");
        const Unit& u = ed.units()[size_t(units[0])];
            dx = int(num(op, "x", u.x)) - u.x, dy = int(num(op, "y", u.y)) - u.y;
        }
        selectUnits(ed, units);
        const int moved = ed.moveSelection(dx, dy, op.boolean("correct", true));
        if (!moved && (dx || dy)) fail("the units do not fit there");
        return r.add("moved", moved);
    }
    if (name == "delete_units") {
        selectUnits(ed, unitIndices(ed, op));
        const int n = int(ed.selection().size());
        ed.deleteSelection();
        return r.add("deleted", n);
    }
    if (name == "set_units") {
        const std::vector<int> units = unitIndices(ed, op);
        int changed = 0;
        if (op.get("player")) {
            selectUnits(ed, units);
            changed += ed.setSelectionOwner(uint8_t(player(op, "player", 12)));
        }
        auto percent = [&](const char* k) { return std::optional<int>(op.get(k) ? std::clamp(int(num(op, k)), 0, 100) : -1); };
        const int hp = *percent("hp"), shields = *percent("shields"), energy = *percent("energy");
        const double resources = num(op, "resources", -1), hangar = num(op, "hangar", -1);
        changed += ed.updateUnits(units, [&](Unit& u) {
            if (hp >= 0) u.hitPoints = uint8_t(hp), u.validProps |= UnitProp::HitPoints;
            if (shields >= 0) u.shields = uint8_t(shields), u.validProps |= UnitProp::Shields;
            if (energy >= 0) u.energy = uint8_t(energy), u.validProps |= UnitProp::Energy;
            if (resources >= 0) u.resources = uint32_t(resources), u.validProps |= UnitProp::Resources;
            if (hangar >= 0) u.hangar = uint16_t(hangar), u.validProps |= UnitProp::Hangar;
        });
        return r.add("changed", changed);
    }
    if (name == "replace_units") {
        const uint16_t from = unitTypeId(ed, need(op, "from"), "from"), to = unitTypeId(ed, need(op, "to"), "to");
        return r.add("replaced", ed.replaceUnits(from, to, false));
    }
    if (name == "place_doodad") {
        std::string why;
        if (!ed.placeDoodad(uint16_t(num(op, "doodad")), int(num(op, "x")), int(num(op, "y")), op.boolean("check_terrain", true), &why))
            fail("the doodad does not fit there" + (why.empty() ? std::string() : ": " + why));
        return r.add("index", int(ed.doodads().size()) - 1);
    }
    if (name == "move_doodad") {
        const int index = int(num(op, "doodad"));
        if (index < 0 || index >= int(ed.doodads().size())) fail("no doodad " + std::to_string(index));
        std::string why;
        if (!ed.moveDoodad(index, int(num(op, "x")), int(num(op, "y")), op.boolean("check_terrain", true), &why))
            fail("the doodad does not fit there" + (why.empty() ? std::string() : ": " + why));
        return r.add("index", int(ed.doodads().size()) - 1);
    }
    if (name == "delete_doodads") {
        const std::vector<int> d = indices(op, "doodads", ed.doodads().size());
        ed.deleteDoodads(d);
        return r.add("deleted", int(d.size()));
    }
    if (name == "place_sprite") {
        if (!ed.placeSprite(uint16_t(num(op, "sprite")), int(num(op, "x")), int(num(op, "y")), uint8_t(player(op, "player", 12))))
            fail("the sprite cannot go there");
        return r.add("index", int(ed.sprites().size()) - 1);
    }
    if (name == "move_sprite") {
        const int index = int(num(op, "sprite"));
        if (index < 0 || index >= int(ed.sprites().size())) fail("no sprite " + std::to_string(index));
        if (!ed.moveSprite(index, int(num(op, "x")), int(num(op, "y")))) fail("the sprite cannot go there");
        return r;
    }
    if (name == "delete_sprites") {
        const std::vector<int> s = indices(op, "sprites", ed.sprites().size());
        ed.deleteSprites(s);
        return r.add("deleted", int(s.size()));
    }
    if (name == "paint_terrain") {
        if (!ed.canPaintTerrain()) fail("the terrain of this map cannot be painted (it has no valid ISOM)");
        const uint16_t t = terrainId(ed, need(op, "terrain"), "terrain");
        const Brush b = brush(op);
        const Json& points = need(op, "points");
        if (points.type != Json::Type::Array) fail("\"points\" must be a list of [x, y]");
        ed.beginTerrainStroke();
        for (const Json& p : points.items) {
            const std::vector<double> xy = nums(p, "points");
            if (xy.size() != 2) fail("each point must be [x, y]");
            ed.paintBrush(int(xy[0]), int(xy[1]), t, b);
        }
        ed.endTerrainStroke();
        return r.add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "paint_area") {
        // The area painted over with the smallest square brush, as one stroke (each corner in it gets the terrain; the
        // terrain correction makes the edges). Replacing each other terrain in the area leaves holes where a terrain
        // cannot border the new one.
        if (!ed.canPaintTerrain()) fail("the terrain of this map cannot be painted (it has no valid ISOM)");
        const uint16_t t = terrainId(ed, need(op, "terrain"), "terrain");
        const Rect a = area(op, "area").normalized();
        const Editor::SelectShape shape = selectShape(op);
        Brush b;
        b.shape = Brush::Square, b.size = 1;
        ed.beginTerrainStroke();
        for (int y = a.top; y <= a.bottom; y += 32)
            for (int x = a.left; x <= a.right; x += 64)
                if (Editor::inSelectShape(a, shape, x, y)) ed.paintBrush(x, y, t, b);
        ed.endTerrainStroke();
        return r.add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "fill_terrain") {
        if (!ed.canPaintTerrain()) fail("the terrain of this map cannot be painted (it has no valid ISOM)");
        const uint16_t t = terrainId(ed, need(op, "terrain"), "terrain");
        const bool changed = ed.fillTerrain(int(num(op, "x")), int(num(op, "y")), t);
        return r.add("changed", changed).add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "replace_terrain") {
        if (!ed.canPaintTerrain()) fail("the terrain of this map cannot be painted (it has no valid ISOM)");
        const uint16_t from = terrainId(ed, need(op, "from"), "from"), to = terrainId(ed, need(op, "to"), "to");
        std::optional<Rect> a;
        if (op.get("area")) a = area(op, "area").normalized();
        const int n = ed.replaceTerrain(from, to, a ? &*a : nullptr, int(selectShape(op)));
        return r.add("corners", n).add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "create_location") {
        const Rect a = area(op, "area").normalized();
        if (a.right <= a.left || a.bottom <= a.top) fail("the area is empty");
        const int i = ed.createLocation(a, false, 3, op.str("name"));
        if (i < 0) fail("the map has no free location");
        return r.add("location", i + 1);
    }
    if (name == "set_location") {
        const int i = location(ed, op);
        Editor::LocationEdit e;
        e.name = op.str("name");
        e.rect = op.get("area") ? area(op, "area") : ed.locationRect(i);
        e.excluded = uint16_t(num(op, "excluded_elevations", ed.locationElevation(i)));
        std::string why;
        if (!ed.editLocation(i, e, &why) && !why.empty()) fail(why);
        return r;
    }
    if (name == "delete_location") {
        ed.deleteLocation(location(ed, op));
        return r;
    }
    if (name == "set_player") {
        const int p = player(op, "player", 8);
        Editor::Player pl = ed.player(p);
        if (const Json* c = op.get("controller")) {
            const std::string s = lower(c->s);
            bool ok = false;
            for (auto v : {Editor::Controller::Inactive, Editor::Controller::Rescue, Editor::Controller::Computer, Editor::Controller::Human,
                           Editor::Controller::Neutral, Editor::Controller::Closed})
                if (s == controllerName(v)) pl.controller = v, ok = true;
            if (!ok) fail("controller must be human, computer, rescue, neutral, closed or inactive");
        }
        if (const Json* c = op.get("race")) {
            const std::string s = lower(c->s);
            bool ok = false;
            for (auto v : {Editor::Race::Zerg, Editor::Race::Terran, Editor::Race::Protoss, Editor::Race::UserSelect, Editor::Race::Random})
                if (s == raceName(v)) pl.race = v, ok = true;
            if (!ok) fail("race must be zerg, terran, protoss, user_select or random");
        }
        if (op.get("force")) pl.force = player(op, "force", 4);
        if (const Json* c = op.get("color")) {
            const std::string s = lower(c->s);
            if (c->type == Json::Type::Number) pl.color = {Editor::PlayerColor::Id, uint8_t(std::clamp(int(c->n), 0, Editor::kColorIds - 1)), 0};
            else if (s == "random") pl.color.kind = Editor::PlayerColor::Random;
            else if (s == "choice") pl.color.kind = Editor::PlayerColor::Choice;
            else if (s.size() == 7 && s[0] == '#') pl.color = {Editor::PlayerColor::Custom, 0, uint32_t(std::strtoul(s.c_str() + 1, nullptr, 16))};
            else {
                bool ok = false;
                for (int id = 0; id < Editor::kColorIds; ++id)
                    if (s == lower(Editor::colorIdName(id))) pl.color = {Editor::PlayerColor::Id, uint8_t(id), 0}, ok = true;
                if (!ok) fail("no color \"" + c->s + "\"");
            }
        }
        ed.setPlayer(p, pl);
        return playerJson(ed, p);
    }
    if (name == "set_force") {
        const int f = player(op, "force", 4);
        Editor::Force fo = ed.force(f);
        if (op.get("name")) fo.name = op.str("name");
        auto flag = [&](const char* k, uint8_t bit) {
            if (const Json* v = op.get(k)) fo.flags = v->b ? uint8_t(fo.flags | bit) : uint8_t(fo.flags & ~bit);
        };
        flag("random_start", Editor::RandomStart), flag("allies", Editor::Allies), flag("allied_victory", Editor::AlliedVictory),
            flag("shared_vision", Editor::SharedVision);
        ed.setForce(f, fo);
        return forceJson(ed, f);
    }
    if (name == "set_scenario") {
        ed.setScenarioText(op.get("name") ? op.str("name") : ed.scenarioName(), op.get("description") ? op.str("description") : ed.scenarioDescription());
        return r;
    }
    if (name == "resize_map") {
        uint16_t era = ed.era();
        if (const Json* t = op.get("tileset")) {
            bool ok = false;
            for (uint16_t e = 0; e < 8; ++e)
                if (lower(t->s) == tilesetName(e)) era = e, ok = true;
            if (!ok) fail("tileset must be badlands, platform, install, ashworld, jungle, desert, ice or twilight");
        }
        uint16_t fill = 0;
        if (const IsomTerrainTable* t = IsomTerrainTable::forEra(era)) {
            for (uint16_t i = t->terrainCount(); i >= 1; --i)
                if (t->isTerrain(i)) fill = i;  // the first terrain
            if (const Json* f = op.get("fill")) {
                if (f->type == Json::Type::Number) fill = uint16_t(f->n);
                else
                    for (uint16_t i = 1; i <= t->terrainCount(); ++i)
                        if (t->isTerrain(i) && lower(t->terrainName(i)) == lower(f->s)) fill = i;
            }
        }
        std::string err;
        if (!ed.resizeMap(uint16_t(num(op, "width", ed.width())), uint16_t(num(op, "height", ed.height())), era, int(num(op, "shift_x", 0)),
                          int(num(op, "shift_y", 0)), fill, err))
            fail(err);
        return r.add("width", int(ed.width())).add("height", int(ed.height()));
    }
    if (name == "place_tiles") {
        const Rect a = area(op, "tiles");
        return r.add("changed", ed.placeTiles(a.left, a.top, a.right, a.bottom, uint16_t(num(op, "value"))));
    }
    fail("unknown op \"" + name + "\" (see `stardraft-cli ops`)");
}

// --- Rendering -----------------------------------------------------------------------------------------------------

// A 3x5 font for the labels (digits, '-' and a few letters).
const char* glyph(char c) {
    switch (c) {
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "111001111100111";
    case '3': return "111001111001111";
    case '4': return "101101111001001";
    case '5': return "111100111001111";
    case '6': return "111100111101111";
    case '7': return "111001010010010";
    case '8': return "111101111101111";
    case '9': return "111101111001111";
    case '-': return "000000111000000";
    case 'L': return "100100100100111";
    }
    return "000000000000000";
}

struct Canvas {
    Image img;
    void blend(int x, int y, uint32_t rgb, int alpha) {
        if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
        uint8_t* d = &img.rgba[(size_t(y) * img.width + x) * 4];
        const uint8_t c[3] = {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)};
        for (int k = 0; k < 3; ++k) d[k] = uint8_t((c[k] * alpha + d[k] * (255 - alpha)) / 255);
    }
    void rect(int l, int t, int r, int b, uint32_t rgb, int alpha, int width) {
        for (int w = 0; w < width; ++w) {
            for (int x = l; x <= r; ++x) blend(x, t + w, rgb, alpha), blend(x, b - w, rgb, alpha);
            for (int y = t + width; y <= b - width; ++y) blend(l + w, y, rgb, alpha), blend(r - w, y, rgb, alpha);
        }
    }
    void text(int x, int y, const std::string& s, uint32_t rgb, int scale = 2) {
        // A dark box behind it, so it reads on any ground.
        const int w = int(s.size()) * 4 * scale + scale, h = 7 * scale;
        for (int yy = y - scale; yy < y - scale + h; ++yy)
            for (int xx = x - scale; xx < x - scale + w; ++xx) blend(xx, yy, 0x000000, 160);
        for (size_t i = 0; i < s.size(); ++i) {
            const char* g = glyph(s[i]);
            for (int gy = 0; gy < 5; ++gy)
                for (int gx = 0; gx < 3; ++gx)
                    if (g[gy * 3 + gx] == '1')
                        for (int sy = 0; sy < scale; ++sy)
                            for (int sx = 0; sx < scale; ++sx) blend(x + int(i) * 4 * scale + gx * scale + sx, y + gy * scale + sy, rgb, 255);
        }
    }
};

struct RenderOptions {
    double left = 0, top = 0, right = 0, bottom = 0;  // map pixels
    double zoom = 1;
    bool units = true, sprites = true, locations = false;
    int grid = 0;  // map pixels between grid lines (0: none)
};

// The area at 1/`scale` (a power of 2): the terrain box-filtered, then the sprites and units drawn on it.
Image renderBase(Editor& ed, const RenderOptions& o, int scale) {
    const int left = int(std::floor(o.left)), top = int(std::floor(o.top));
    Image base;
    base.width = std::max(1, int(std::ceil((o.right - left) / scale)));
    base.height = std::max(1, int(std::ceil((o.bottom - top) / scale)));
    std::vector<uint32_t> sum(size_t(base.width) * base.height * 3, 0);
    const Tileset& ts = ed.tileset();
    const TerrainLayer& terrain = ed.terrain();
    std::map<int, std::vector<uint8_t>> tiles;  // megatile -> 32x32 RGB
    auto tilePixels = [&](int mega) -> const std::vector<uint8_t>& {
        auto it = tiles.find(mega);
        if (it != tiles.end()) return it->second;
        std::vector<uint8_t> px(32 * 32 * 3, 0);
        if (mega < 0 || size_t(mega) >= ts.megatiles.size()) {
            for (size_t p = 0; p < px.size(); p += 3) px[p] = 255, px[p + 2] = 255;
        } else {
            const auto& refs = ts.megatiles[size_t(mega)];
            for (int m = 0; m < 16; ++m) {
                const bool flip = refs[m] & 1;
                const size_t mini = refs[m] >> 1;
                for (int py = 0; py < 8; ++py)
                    for (int pxl = 0; pxl < 8; ++pxl) {
                        const uint8_t idx = mini < ts.minitiles.size() ? ts.minitiles[mini][py * 8 + (flip ? 7 - pxl : pxl)] : 0;
                        uint8_t* d = &px[(size_t((m / 4) * 8 + py) * 32 + (m % 4) * 8 + pxl) * 3];
                        d[0] = ts.palette[idx].r, d[1] = ts.palette[idx].g, d[2] = ts.palette[idx].b;
                    }
            }
        }
        return tiles.emplace(mega, std::move(px)).first->second;
    };
    const int tx0 = std::max(0, left / 32), ty0 = std::max(0, top / 32);
    const int tx1 = std::min(int(terrain.width), int(std::ceil(o.right / 32))), ty1 = std::min(int(terrain.height), int(std::ceil(o.bottom / 32)));
    for (int ty = ty0; ty < ty1; ++ty)
        for (int tx = tx0; tx < tx1; ++tx) {
            const std::vector<uint8_t>& px = tilePixels(ts.megatileFor(terrain.tiles[size_t(ty) * terrain.width + tx]));
            for (int y = 0; y < 32; ++y) {
                const int my = ty * 32 + y - top;
                if (my < 0 || my / scale >= base.height) continue;
                for (int x = 0; x < 32; ++x) {
                    const int mx = tx * 32 + x - left;
                    if (mx < 0 || mx / scale >= base.width) continue;
                    uint32_t* s = &sum[(size_t(my / scale) * base.width + mx / scale) * 3];
                    const uint8_t* p = &px[(size_t(y) * 32 + x) * 3];
                    s[0] += p[0], s[1] += p[1], s[2] += p[2];
                }
            }
        }
    base.rgba.assign(size_t(base.width) * base.height * 4, 255);
    const uint32_t area = uint32_t(scale * scale);
    for (size_t i = 0; i < size_t(base.width) * base.height; ++i)
        for (int k = 0; k < 3; ++k) base.rgba[i * 4 + k] = uint8_t(sum[i * 3 + k] / area);  // (outside the map: black)

    // A picture with its origin at map pixel (x, y), alpha-blended and point-sampled at the base's scale.
    auto blit = [&](const Sprite& s, int x, int y) {
        const int l = x - s.originX - left, t = y - s.originY - top;
        for (int sy = 0; sy < s.height; sy += 1) {
            const int by = t + sy;
            if (by < 0 || by % scale || by / scale >= base.height) continue;
            for (int sx = 0; sx < s.width; ++sx) {
                const int bx = l + sx;
                if (bx < 0 || bx % scale || bx / scale >= base.width) continue;
                const uint8_t* src = &s.rgba[(size_t(sy) * s.width + sx) * 4];
                if (!src[3]) continue;
                uint8_t* d = &base.rgba[(size_t(by / scale) * base.width + bx / scale) * 4];
                for (int k = 0; k < 3; ++k) d[k] = uint8_t((src[k] * src[3] + d[k] * (255 - src[3])) / 255);
            }
        }
    };
    UnitSprites& sp = ed.unitSprites();
    struct Item {
        int y;
        bool unit;
        size_t index;
    };
    std::vector<Item> items;
    const int margin = 256;  // pictures are at most this far from their origin
    auto inView = [&](int x, int y) { return x >= o.left - margin && x <= o.right + margin && y >= o.top - margin && y <= o.bottom + margin; };
    if (o.sprites)
        for (size_t i = 0; i < ed.sprites().size(); ++i)
            if (inView(ed.sprites()[i].x, ed.sprites()[i].y)) items.push_back({ed.sprites()[i].y, false, i});
    if (o.units)
        for (size_t i = 0; i < ed.units().size(); ++i)
            if (inView(ed.units()[i].x, ed.units()[i].y)) items.push_back({ed.units()[i].y, true, i});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.y < b.y; });
    for (const Item& it : items) {
        if (it.unit) {
            const Unit& u = ed.units()[it.index];
            if (const Sprite* s = sp.unitSprite(u.type, u.owner, ts)) blit(*s, u.x, u.y);
            continue;
        }
        const MapSprite& m = ed.sprites()[it.index];
        if (!(m.flags & SpriteFlag::DrawAsSprite)) {  // a unit drawn as a sprite
            if (const Sprite* s = sp.unitSprite(m.id, uint8_t(std::min<int>(m.owner, 11)), ts)) blit(*s, m.x, m.y);
            continue;
        }
        const int image = sp.imageForSprite(m.id);
        const Grp* g = sp.grp(image);
        if (!g) continue;
        Sprite s;
        s.width = g->width, s.height = g->height, s.originX = g->width / 2, s.originY = g->height / 2;
        s.rgba.assign(size_t(s.width) * s.height * 4, 0);
        const int fn = sp.drawFunction(image);
        sp.drawImage(s, image, sp.script(image).startFrameOn(ed.era()), false, uint8_t(std::min<int>(m.owner, 11)), ts, 0, 0,
                     fn == 10 ? UnitSprites::Blend::Shadow : fn == 9 ? UnitSprites::Blend::Glow : UnitSprites::Blend::Normal);
        blit(s, m.x, m.y);
    }
    return base;
}

Image render(Editor& ed, const RenderOptions& o) {
    const int outW = std::max(1, int(std::lround((o.right - o.left) * o.zoom))), outH = std::max(1, int(std::lround((o.bottom - o.top) * o.zoom)));
    if (double(outW) * outH > 64.0 * 1024 * 1024) fail("the picture would be too large (" + std::to_string(outW) + "x" + std::to_string(outH) + "): use a smaller --zoom or --size");
    int scale = 1;
    while (scale < 32 && o.zoom * scale * 2 <= 1.0) scale *= 2;
    const Image base = renderBase(ed, o, scale);
    // The base to the output size: an average of the base pixels under each output pixel (nearest when enlarging).
    const double ratio = 1.0 / (o.zoom * scale);
    const double offX = (o.left - std::floor(o.left)) / scale, offY = (o.top - std::floor(o.top)) / scale;
    Canvas c;
    c.img.width = outW, c.img.height = outH;
    c.img.rgba.assign(size_t(outW) * outH * 4, 255);
    for (int oy = 0; oy < outH; ++oy) {
        const int y0 = std::clamp(int(offY + oy * ratio), 0, base.height - 1), y1 = std::clamp(std::max(y0 + 1, int(offY + (oy + 1) * ratio)), y0 + 1, base.height);
        for (int ox = 0; ox < outW; ++ox) {
            const int x0 = std::clamp(int(offX + ox * ratio), 0, base.width - 1), x1 = std::clamp(std::max(x0 + 1, int(offX + (ox + 1) * ratio)), x0 + 1, base.width);
            uint32_t s[3] = {0, 0, 0};
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    for (int k = 0; k < 3; ++k) s[k] += base.rgba[(size_t(y) * base.width + x) * 4 + k];
            const uint32_t n = uint32_t((y1 - y0) * (x1 - x0));
            uint8_t* d = &c.img.rgba[(size_t(oy) * outW + ox) * 4];
            for (int k = 0; k < 3; ++k) d[k] = uint8_t(s[k] / n);
        }
    }
    auto toX = [&](double mx) { return int(std::lround((mx - o.left) * o.zoom)); };
    auto toY = [&](double my) { return int(std::lround((my - o.top) * o.zoom)); };
    if (o.grid > 0) {
        for (int mx = int(std::ceil(o.left / o.grid)) * o.grid; mx <= o.right; mx += o.grid) {
            for (int y = 0; y < outH; ++y) c.blend(toX(mx), y, 0xFFFFFF, 70);
            c.text(toX(mx) + 3, 3, std::to_string(mx), 0xFFFFFF);
        }
        for (int my = int(std::ceil(o.top / o.grid)) * o.grid; my <= o.bottom; my += o.grid) {
            for (int x = 0; x < outW; ++x) c.blend(x, toY(my), 0xFFFFFF, 70);
            if (my > o.top) c.text(3, toY(my) + 3, std::to_string(my), 0xFFFFFF);
        }
    }
    if (o.locations)
        for (int i = 0; i < ed.locationCount(); ++i) {
            if (!ed.locationUsed(i) || i == kAnywhereLocation) continue;
            const Rect r = ed.locationRect(i);
            if (r.right < o.left || r.left > o.right || r.bottom < o.top || r.top > o.bottom) continue;
            c.rect(toX(r.left), toY(r.top), toX(r.right) - 1, toY(r.bottom) - 1, 0x40A0FF, 230, 2);
            c.text(toX(r.left) + 4, toY(r.top) + 4, "L" + std::to_string(i + 1), 0x80C8FF);
        }
    return c.img;
}

// --- Commands ------------------------------------------------------------------------------------------------------

struct Timer {
    bool on = false;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    void step(const char* what) {
        if (!on) return;
        const auto now = std::chrono::steady_clock::now();
        std::fprintf(stderr, "%-24s %8.1f ms\n", what, std::chrono::duration<double, std::milli>(now - t0).count());
        t0 = now;
    }
};

struct Session {
    Args args;
    Timer timer;
    GameData data;
    bool dataOpen = false;
    std::unique_ptr<Editor> ed;

    // The map of a target as it is now: the working folder's current step when it is open, else the map file.
    Editor& load(const Target& t) {
        if (!dataOpen) {
            openGameData(data, args.get("install"));
            dataOpen = true;
            timer.step("open the game data");
        }
        ed = std::make_unique<Editor>(data);
        const fs::path file = t.open ? loadWork(t).current() : t.map;
        std::string err;
        if (!ed->open(file, err)) fail(err);
        timer.step("open the map");
        return *ed;
    }
};

Json fileInfo(const Target& t) {
    Obj o;
    o.add("map", ustr(t.map));
    if (t.open) {
        const Json w = loadWork(t).info();
        for (const auto& f : w.fields) o.add(f.first, f.second);
    }
    return o;
}

Json cmdOpen(Session& s) {
    const Target t = resolve(s.args.positional.at(0));
    if (t.open) {
        if (!s.args.has("reopen")) {
            const Work w = loadWork(t);
            fail(ustr(t.map) + " is open already (" + std::string(w.modified() ? "with changes that are not saved" : "no changes") +
                 "): go on with it, or `close` it, or `open --reopen` to start again from the map file");
        }
    }
    std::error_code ec;
    if (!fs::exists(t.map, ec)) fail("no file " + ustr(t.map));
    const Bytes raw = readScenario(t.map);
    // Check that the editor can open it before the folder is made.
    Target probe = t;
    probe.open = false;
    Editor& ed = s.load(probe);
    if (t.open) {
        Work old = loadWork(t);
        for (size_t i = 0; i < old.steps.size(); ++i) fs::remove(old.stepFile(i), ec);
    }
    fs::create_directories(t.work / "steps");
    Work w;
    w.dir = t.work, w.source = t.map;
    w.steps = {{1, "open"}};
    w.nextChange = 2, w.savedChange = 1;
    w.stamp = fileStamp(t.map);
    writeBytes(w.stepFile(0), raw);
    writeBytes(w.sourceCopy(), readBytes(t.map));
    w.save();
    return Obj().add("map", ustr(t.map)).add("work", ustr(w.dir)).add("change", 1).add("summary", summary(ed));
}

// A new map file: the size, the tileset and the terrain it is filled with. It does not open it.
Json cmdNew(Session& s) {
    const fs::path path = fs::absolute(upath(s.args.positional.at(0)));
    std::error_code ec;
    if (fs::exists(path, ec) && !s.args.has("force")) fail(ustr(path) + " is there already (--force writes over it)");
    const std::vector<double> size = numbers(s.args.get("size", "128,128"), 2, "--size");
    uint16_t era = 0;
    const std::string tileset = lower(s.args.get("tileset", "badlands"));
    bool found = false;
    for (uint16_t e = 0; e < 8; ++e)
        if (tileset == tilesetName(e)) era = e, found = true;
    if (!found) fail("--tileset must be badlands, platform, install, ashworld, jungle, desert, ice or twilight");
    const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
    if (!table) fail("no terrain table for " + tileset);
    uint16_t terrain = 0;
    const std::string want = lower(s.args.get("terrain"));
    for (uint16_t i = table->terrainCount(); i >= 1; --i)
        if (table->isTerrain(i) && (want.empty() || lower(table->terrainName(i)) == want)) terrain = i;
    if (!terrain) fail("no terrain \"" + s.args.get("terrain") + "\" in " + tileset);
    if (!s.dataOpen) {
        openGameData(s.data, s.args.get("install"));
        s.dataOpen = true;
    }
    Editor ed(s.data);
    std::string err;
    if (!ed.newMap(uint16_t(size[0]), uint16_t(size[1]), era, terrain, err)) fail(err);
    if (!ed.save(path, err)) fail(err);
    return Obj().add("map", ustr(path)).add("summary", summary(ed));
}

Json cmdStatus(Session& s) {
    const Target t = resolve(s.args.positional.at(0));
    Obj o;
    o.add("map", ustr(t.map)).add("open", t.open);
    if (t.open) {
        const Work w = loadWork(t);
        for (const auto& f : w.info().fields) o.add(f.first, f.second);
        Json h = jarr();
        for (size_t i = 0; i < w.steps.size(); ++i)
            h.items.push_back(Obj().add("step", int(i)).add("change", double(w.steps[i].change)).add("what", w.steps[i].what).add("current", i == w.step));
        o.add("history", h);
    }
    return o;
}

Json cmdGet(Session& s) {
    const Target t = resolve(s.args.positional.at(0));
    Editor& ed = s.load(t);
    std::vector<std::string> parts(s.args.positional.begin() + 1, s.args.positional.end());
    if (parts.empty()) parts.push_back("summary");
    const int step = std::max(32, int(std::strtol(s.args.get("step", "128").c_str(), nullptr, 10)));
    Obj o = Obj(fileInfo(t));
    for (const std::string& p : parts) o.add(p, getWhat(ed, p, step));
    return o;
}

Json cmdApply(Session& s) {
    if (s.args.positional.size() < 2) fail("usage: stardraft-cli apply <map> <ops.json | - | '[...]'>");
    const Target t = resolve(s.args.positional[0]);
    Work w = loadWork(t);
    if (s.args.has("expect")) {
        const uint64_t expect = std::strtoull(s.args.get("expect").c_str(), nullptr, 10);
        if (expect != w.change()) fail("the map changed: its change is " + std::to_string(w.change()) + ", not " + std::to_string(expect) + " (run `get` again)");
    }
    const std::string src = s.args.positional[1];
    std::string text;
    if (src == "-") text.assign(std::istreambuf_iterator<char>(std::cin), {});
    else if (!src.empty() && (src[0] == '[' || src[0] == '{')) text = src;
    else text = readText(upath(src));
    const Json doc = parseJson(text, "the operations");
    const Json* ops = doc.type == Json::Type::Array ? &doc : doc.get("ops");
    if (!ops || ops->type != Json::Type::Array) fail("the operations must be a list, or an object with \"ops\": [...]");

    Editor& ed = s.load(t);
    const Json* opt = doc.type == Json::Type::Object ? doc.get("options") : nullptr;
    if (opt) {
        ed.setAllowIllegalUnits(opt->boolean("units_anywhere", false));
        ed.setAllowStacking(opt->boolean("stacking", false));
    }
    const Bytes before = ed.chk().serialize();
    Json results = jarr();
    for (size_t i = 0; i < ops->items.size(); ++i) {
        const Json& op = ops->items[i];
        try {
            if (op.type != Json::Type::Object) fail("an operation must be an object");
            results.items.push_back(applyOp(ed, op));
        } catch (const std::exception& e) {
            fail("op " + std::to_string(i) + " (" + op.str("op", "?") + "): " + e.what() + "; nothing was changed");
        }
    }
    s.timer.step("apply");
    const Bytes after = ed.chk().serialize();
    const bool changed = after != before;
    if (changed && !s.args.has("dry-run")) {
        std::error_code ec;
        for (size_t i = w.step + 1; i < w.steps.size(); ++i) fs::remove(w.stepFile(i), ec);  // the redo steps go
        w.steps.resize(w.step + 1);
        std::string what = s.args.get("label");
        if (what.empty())
            for (const Json& op : ops->items) what += (what.empty() ? "" : ", ") + op.str("op");
        w.steps.push_back({w.nextChange++, what});
        w.step = w.steps.size() - 1;
        writeBytes(w.current(), after);
        w.save();
        s.timer.step("write the step");
    }
    return Obj().add("changed", changed).add("dry_run", s.args.has("dry-run")).add("results", results).add("change", double(w.change())).add("modified", w.modified());
}

Json cmdUndoRedo(Session& s, bool undo) {
    const Target t = resolve(s.args.positional.at(0));
    Work w = loadWork(t);
    const int n = std::max(1, int(std::strtol(s.args.get("steps", "1").c_str(), nullptr, 10)));
    for (int i = 0; i < n; ++i) {
        if (undo ? w.step == 0 : w.step + 1 >= w.steps.size()) {
            if (i == 0) fail(undo ? "nothing to undo" : "nothing to redo");
            break;
        }
        w.step += undo ? -1 : 1;
    }
    w.save();
    return Obj().add("step", w.steps[w.step].what).add("change", double(w.change())).add("modified", w.modified());
}

Json cmdSave(Session& s) {
    const Target t = resolve(s.args.positional.at(0));
    Work w = loadWork(t);
    const fs::path dst = s.args.has("to") ? fs::absolute(upath(s.args.get("to"))) : w.source;
    const bool toSource = samePath(dst, w.source);
    if (toSource && fileStamp(w.source) != w.stamp && !s.args.has("force"))
        fail(ustr(w.source) + " changed after `open` (another program saved it): `save --force` writes over it, or `save --to` "
             "writes another file");
    const Bytes chk = readBytes(w.current());
    if (isChk(dst)) {
        writeBytes(dst, chk);
    } else {
        // The map's other files (sounds and the like) come from the map file as it was opened.
        std::vector<ArchiveFile> files;
        std::string err;
        if (!isChk(w.source)) {
            if (!readMapFiles(w.sourceCopy(), files, err)) fail(err);
            files.erase(std::remove_if(files.begin(), files.end(), [](const ArchiveFile& f) { return lower(f.name) == lower(kChkArchivePath); }), files.end());
        }
        if (!writeMap(dst, chk, files, err)) fail(err);
    }
    if (toSource) {
        w.savedChange = w.change();
        w.stamp = fileStamp(w.source);
        w.save();
    }
    return Obj().add("saved", ustr(dst)).add("change", double(w.change())).add("modified", w.modified());
}

Json cmdClose(Session& s) {
    const Target t = resolve(s.args.positional.at(0));
    Work w = loadWork(t);
    if (w.modified() && !s.args.has("discard")) fail("the map has changes that are not saved: `save` it, or `close --discard`");
    // work.json goes last: until then the folder is still an open map, and close can be run again.
    auto remove = [](const fs::path& p) {
        std::error_code ec;
        fs::remove(p, ec);
        if (ec) fail("could not remove " + ustr(p) + ": " + ec.message());
    };
    for (size_t i = 0; i < w.steps.size(); ++i) remove(w.stepFile(i));
    remove(w.sourceCopy());
    remove(w.dir / "steps");
    remove(w.dir / "work.json");
    remove(w.dir);
    return Obj().add("closed", ustr(t.map));
}

Json cmdRender(Session& s) {
    const Target t = resolve(s.args.positional.at(0));
    const std::string out = s.args.get("out", s.args.get("o"));
    if (out.empty()) fail("render needs -o <file.png>");
    Editor& ed = s.load(t);
    RenderOptions o;
    const double mapW = ed.width() * 32.0, mapH = ed.height() * 32.0;
    o.left = 0, o.top = 0, o.right = mapW, o.bottom = mapH;
    const bool hasZoom = s.args.has("zoom");
    if (hasZoom) o.zoom = numbers(s.args.get("zoom"), 1, "--zoom")[0];
    std::optional<std::vector<double>> size;
    if (s.args.has("size")) size = numbers(s.args.get("size"), 2, "--size");
    if (s.args.has("area")) {
        const std::vector<double> a = numbers(s.args.get("area"), 4, "--area");
        o.left = std::min(a[0], a[2]), o.top = std::min(a[1], a[3]), o.right = std::max(a[0], a[2]), o.bottom = std::max(a[1], a[3]);
        if (o.right <= o.left || o.bottom <= o.top) fail("--area is empty");
    } else if (s.args.has("center")) {
        const std::vector<double> c = numbers(s.args.get("center"), 2, "--center");
        const double w = size ? (*size)[0] : 1024, h = size ? (*size)[1] : 768;
        const double z = hasZoom ? o.zoom : 1;
        o.left = c[0] - w / z / 2, o.top = c[1] - h / z / 2, o.right = c[0] + w / z / 2, o.bottom = c[1] + h / z / 2;
        size.reset();
    }
    if (size) o.zoom = std::min((*size)[0] / (o.right - o.left), (*size)[1] / (o.bottom - o.top));
    else if (!hasZoom && !s.args.has("center")) o.zoom = std::min(1.0, 1024.0 / std::max(o.right - o.left, o.bottom - o.top));
    if (!std::isfinite(o.zoom) || o.zoom <= 0 || o.zoom > 8) fail("the zoom must be more than 0 and at most 8 (--zoom, or --size for the area)");
    o.units = !s.args.has("no-units");
    o.sprites = !s.args.has("no-sprites");
    o.locations = s.args.has("locations");
    o.grid = int(std::strtol(s.args.get("grid", "0").c_str(), nullptr, 10));
    if (o.grid > 0 && o.grid * o.zoom < 24) fail("--grid " + std::to_string(o.grid) + " is too dense at this zoom");
    const Image img = render(ed, o);
    s.timer.step("render");
    if (!writePng(fs::absolute(upath(out)), img)) fail("cannot write " + out);
    s.timer.step("write the PNG");
    return Obj()
        .add("file", ustr(fs::absolute(upath(out))))
        .add("width", img.width)
        .add("height", img.height)
        .add("area", [&] {
            Json a = jarr();
            for (double v : {o.left, o.top, o.right, o.bottom}) a.items.push_back(jnum(v));
            return a;
        }())
        .add("zoom", o.zoom)
        .add("note", "a picture pixel (px, py) is map pixel (area left + px / zoom, area top + py / zoom)");
}

const char* kOps = R"OPS([
  {"op": "place_unit", "params": {"type": "id or name", "player": "1-12 (12: neutral)", "x": "px", "y": "px", "correct": "bool, default true: move to the nearest place where it fits"}, "result": {"serial": "the new unit's id", "x": "px", "y": "px"}},
  {"op": "move_units", "params": {"units": "[serial] (or unit_indices: [index])", "dx": "px", "dy": "px", "x": "px (instead of dx: the first unit goes there)", "y": "px", "correct": "bool, default true"}},
  {"op": "delete_units", "params": {"units": "[serial]"}},
  {"op": "set_units", "params": {"units": "[serial]", "player": "1-12", "hp": "0-100 %", "shields": "0-100 %", "energy": "0-100 %", "resources": "number", "hangar": "number"}},
  {"op": "replace_units", "params": {"from": "unit type", "to": "unit type"}},
  {"op": "place_doodad", "params": {"doodad": "id (get doodad-types)", "x": "px (center)", "y": "px", "check_terrain": "bool, default true"}, "result": {"index": "the new doodad's index"}},
  {"op": "move_doodad", "params": {"doodad": "index", "x": "px", "y": "px", "check_terrain": "bool, default true"}, "result": {"index": "its new index (it goes to the end)"}},
  {"op": "delete_doodads", "params": {"doodads": "[index]"}},
  {"op": "place_sprite", "params": {"sprite": "sprites.dat id", "x": "px", "y": "px", "player": "1-12"}},
  {"op": "move_sprite", "params": {"sprite": "index", "x": "px", "y": "px"}},
  {"op": "delete_sprites", "params": {"sprites": "[index]"}},
  {"op": "paint_terrain", "params": {"terrain": "id or name (get terrains)", "points": "[[x, y], ...] px: one brush stroke", "brush": {"shape": "diamond | square | circle", "size": "in diamonds (128x64 px), default 1"}}},
  {"op": "paint_area", "params": {"terrain": "id or name", "area": "[left, top, right, bottom] px", "shape": "rect | diamond | ellipse"}},
  {"op": "fill_terrain", "params": {"terrain": "id or name", "x": "px", "y": "px"}},
  {"op": "replace_terrain", "params": {"from": "terrain", "to": "terrain", "area": "[left, top, right, bottom] px (default: the whole map)", "shape": "rect | diamond | ellipse"}},
  {"op": "place_tiles", "params": {"tiles": "[x0, y0, x1, y1] in tiles, inclusive", "value": "tile value (group << 4 | subtile)"}},
  {"op": "create_location", "params": {"area": "[left, top, right, bottom] px", "name": "text"}, "result": {"location": "its number"}},
  {"op": "set_location", "params": {"location": "number", "name": "text", "area": "[left, top, right, bottom] px", "excluded_elevations": "bits: 1 low, 2 medium, 4 high ground, 8 low, 16 medium, 32 high air"}},
  {"op": "delete_location", "params": {"location": "number"}},
  {"op": "set_player", "params": {"player": "1-8", "controller": "human | computer | rescue | neutral | closed | inactive", "race": "zerg | terran | protoss | user_select | random", "force": "1-4", "color": "a color name, 0-21, random, choice or #RRGGBB"}},
  {"op": "set_force", "params": {"force": "1-4", "name": "text", "allies": "bool", "allied_victory": "bool", "shared_vision": "bool", "random_start": "bool"}},
  {"op": "set_scenario", "params": {"name": "text", "description": "text"}},
  {"op": "resize_map", "params": {"width": "tiles", "height": "tiles", "tileset": "badlands | platform | install | ashworld | jungle | desert | ice | twilight", "shift_x": "tiles", "shift_y": "tiles", "fill": "terrain for new ground"}}
])OPS";

const char* kUsage = R"USAGE(stardraft-cli: edit StarCraft: Remastered maps without a window.

  stardraft-cli new <map> [--size w,h] [--tileset name] [--terrain name] [--force]
                                            a new map file (default 128x128 badlands, the first terrain)
  stardraft-cli open <map>                  make the working folder <map>.work (the map file does not change)
  stardraft-cli status <map>                the working folder: change number, undo and redo steps, history
  stardraft-cli get <map> [part...]         JSON: summary (default), units, doodads, sprites, locations, players,
                                            forces, terrain [--step px], terrains, unit-types, doodad-types
  stardraft-cli apply <map> <ops>           apply operations (a file, - for stdin, or JSON text) as one undo step
                    [--expect <change>] [--dry-run] [--label <text>]
  stardraft-cli undo <map> [--steps n]      go back one step (or n)
  stardraft-cli redo <map> [--steps n]
  stardraft-cli render <map> -o <file.png>  a picture of the map, or part of it:
                    [--area l,t,r,b | --center x,y [--size w,h]] [--zoom z] [--size w,h]
                    [--grid px] [--locations] [--no-units] [--no-sprites]
  stardraft-cli save <map> [--to <path>]    write the map (.scm, .scx or .chk)
  stardraft-cli close <map> [--discard]     remove the working folder
  stardraft-cli ops                         the operations that apply takes, with their parameters

Positions are map pixels (a tile is 32 px). Players are 1-12, forces 1-4, locations by their number.
<map> is the map file, or its working folder. After `open`, get, apply and render work on the working copy.
Options: --install <StarCraft folder> (else STARDRAFT_INSTALL, the editor's setting, the usual folders), --timing.
Every command writes one JSON object: {"ok": true, ...} or {"ok": false, "error": "..."} (exit code 1).
)USAGE";

}  // namespace

static int run(std::vector<std::string> argv) {
    Session s;
    for (size_t i = 1; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        if (a.size() > 1 && a[0] == '-' && a != "-") {
            std::string name = a.substr(a[1] == '-' ? 2 : 1);
            if (const size_t eq = name.find('='); eq != std::string::npos) {
                s.args.options[name.substr(0, eq)] = name.substr(eq + 1);
            } else if (isFlag(name)) {
                s.args.options[name] = "";
            } else {
                if (i + 1 >= argv.size()) {
                    std::printf("%s\n", jsonText(Obj().add("ok", false).add("error", "--" + name + " needs a value")).c_str());
                    return 2;
                }
                s.args.options[name] = argv[++i];
            }
        } else if (s.args.command.empty()) {
            s.args.command = a;
        } else {
            s.args.positional.push_back(a);
        }
    }
    const std::string& c = s.args.command;
    if (c.empty() || c == "help" || s.args.has("help")) {
        std::fputs(kUsage, stdout);
        return c.empty() ? 2 : 0;
    }
    s.timer.on = s.args.has("timing");
    Json result;
    try {
        if (c == "ops") {
            result = Obj().add("ops", parseJson(kOps, "the operation list"));
        } else {
            if (s.args.positional.empty()) fail(c + " needs a map");
            if (c == "open") result = cmdOpen(s);
            else if (c == "status") result = cmdStatus(s);
            else if (c == "new") result = cmdNew(s);
            else if (c == "get") result = cmdGet(s);
            else if (c == "apply") result = cmdApply(s);
            else if (c == "undo") result = cmdUndoRedo(s, true);
            else if (c == "redo") result = cmdUndoRedo(s, false);
            else if (c == "save") result = cmdSave(s);
            else if (c == "close") result = cmdClose(s);
            else if (c == "render") result = cmdRender(s);
            else fail("unknown command \"" + c + "\" (stardraft-cli help)");
        }
    } catch (const std::exception& e) {
        std::printf("%s\n", jsonText(Obj().add("ok", false).add("error", e.what())).c_str());
        return 1;
    }
    Obj out;
    out.add("ok", true);
    for (auto& f : result.fields) out.add(f.first, f.second);
    std::printf("%s\n", jsonText(out).c_str());
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(size_t(std::max(n, 1)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), n, nullptr, nullptr);
        s.resize(std::strlen(s.c_str()));
        args.push_back(s);
    }
    SetConsoleOutputCP(CP_UTF8);
    return run(args);
}
#else
int main(int argc, char** argv) { return run(std::vector<std::string>(argv, argv + argc)); }
#endif
