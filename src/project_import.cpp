#include "stardraft/project_import.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/game_data.h"
#include "stardraft/nrbf.h"

namespace fs = std::filesystem;

namespace stardraft {

namespace {

#include "data/eud_editor3.inc"

std::string readAll(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
bool writeText(const fs::path& f, const std::string& text, std::string& error) {
    std::error_code ec;
    fs::create_directories(f.parent_path(), ec);
    std::ofstream out(f, std::ios::binary);
    out << text;
    if (!out) return error = "could not write " + f.string(), false;
    return true;
}
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}
std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) out.push_back(trim(l));
    return out;
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
fs::path pathOf(const std::string& s) { return fs::u8path(s); }
// A path that the other editor's file names: a local one only. A network path (\\server\share, //server) or a device
// path (\\.\, \\?\) gives "": a file that the user selects must not open a connection or a device by itself.
fs::path localPath(const std::string& s) {
    if (s.size() >= 2 && (s[0] == '\\' || s[0] == '/') && (s[1] == '\\' || s[1] == '/')) return {};
    return pathOf(s);
}
// "1 value", "2 values".
std::string count(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }
std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
bool present(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p, ec);
}
// A file or folder name from the other editor's file, made safe for a path in the project: no folder parts, no
// characters that Windows refuses, not "." or "..". The file is not trusted.
std::string safeName(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x20 || std::strchr("\\/:*?\"<>|", c)) out += '_';
        else if (c == 0xE2) out += char(c);  // (UTF-8 lead bytes stay; the zero width space of EUD Editor 3's names goes below)
        else out += char(c);
    }
    // EUD Editor 3 starts its own nodes' names with zero width spaces (U+200B: E2 80 8B).
    for (size_t at; (at = out.find("\xE2\x80\x8B")) != std::string::npos;) out.erase(at, 3);
    out = trim(out);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty() || out == "." || out == "..") out = "_";
    // Windows' device names (CON, NUL, COM1, ...), also with an extension, are not files: a "_" before them.
    std::string stem = lower(out.substr(0, out.find('.')));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    static const char* const kDevices[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
                                           "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* d : kDevices)
        if (stem == d) return "_" + out;
    return out;
}
// "key : value" (EUD Editor 2's settings).
bool keyValue(const std::string& l, std::string& k, std::string& v) {
    const size_t c = l.find(" : ");
    if (c == std::string::npos) return false;
    k = trim(l.substr(0, c)), v = trim(l.substr(c + 3));
    return true;
}
// A button text's hotkey and type bytes, then the text up to its zero byte; the parts after that stay the game's (as
// StatTxt::stock has them).
std::string firstPart(std::string out) {
    const size_t from = out.size() >= 2 && static_cast<unsigned char>(out[0]) >= 0x20 && static_cast<unsigned char>(out[1]) <= 5 ? 2 : 0;
    if (const size_t z = out.find('\0', from); z != std::string::npos) out.resize(z);
    return out;
}
// EUD Editor 2's text: <N> is the byte N (a color, 10 a line end).
std::string ee2Text(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') {
            const size_t e = s.find('>', i);
            if (e != std::string::npos && e > i + 1 && e - i <= 4 &&
                std::all_of(s.begin() + long(i) + 1, s.begin() + long(e), [](char ch) { return ch >= '0' && ch <= '9'; })) {
                const int n = std::stoi(s.substr(i + 1, e - i - 1));
                if (n < 256) {
                    out += char(n);
                    i = e;
                    continue;
                }
            }
        }
        out += s[i];
    }
    return firstPart(out);
}
// EUD Editor 3's text (its tblWriter.StrExecuter): <NAME> is the byte that the key name NAME has (<Enter>, <Ctrl+A>),
// <XX> the byte XX in hex; \\, \< and \> are \, < and >.
std::string ee3Text(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size() && (s[i + 1] == '\\' || s[i + 1] == '<' || s[i + 1] == '>')) {
            out += s[++i];
            continue;
        }
        if (c == '<') {
            const size_t e = s.find('>', i + 1);
            if (e != std::string::npos) {
                const std::string name = s.substr(i + 1, e - i - 1);
                int byte = -1;
                for (size_t k = 0; k < std::size(kEe3KeyNames) && byte < 0; ++k)
                    if (name == kEe3KeyNames[k] && name != "NUL") byte = int(k);
                if (byte < 0 && !name.empty() && name.size() <= 2 && std::all_of(name.begin(), name.end(), [](char h) { return std::isxdigit(static_cast<unsigned char>(h)); }))
                    byte = std::stoi(name, nullptr, 16);
                if (byte >= 0) {
                    out += char(byte);
                    i = e;
                    continue;
                }
            }
        }
        out += c;
    }
    return firstPart(out);
}

// The .eds sections of a text: [name] then its lines.
using Sections = std::vector<std::pair<std::string, std::vector<std::string>>>;
Sections edsSections(const std::string& text) {
    Sections out;
    for (const std::string& l : lines(text)) {
        if (l.size() >= 2 && l.front() == '[' && l.back() == ']') out.push_back({l.substr(1, l.size() - 2), {}});
        else if (!out.empty()) out.back().second.push_back(l);
    }
    return out;
}
std::string edsInput(const Sections& sections) {
    for (const auto& [sec, body] : sections)
        if (sec == "main")
            for (const std::string& l : body)
                if (l.rfind("input", 0) == 0 && l.find(':') != std::string::npos) return trim(l.substr(l.find(':') + 1));
    return {};
}

// EUD Editor 3's two status panel functions, by their index in its files (ExtraDatFiles.LoadStatusData).
constexpr uint32_t kStatusFn[] = {4343040, 4344192, 4346240, 4345616, 4344656, 4344560, 4344512, 4348160, 4343072};
constexpr uint32_t kDisplayFn[] = {4353872, 4356240, 4357264, 4355232, 4355040, 4354656, 4357424, 4353760, 4349664};
int indexOf(const uint32_t* table, size_t n, uint32_t v) {
    for (size_t k = 0; k < n; ++k)
        if (table[k] == v) return int(k);
    return -1;
}

// A requirement list from codes (EUD Editor 2's ReqData, EUD Editor 3's blocks as codes): a code below 0x100 is a unit;
// else an opcode, with a value after opcodes 2, 3, 4 and 37.
RequirementList requirementList(const std::vector<uint16_t>& codes) {
    RequirementList l;
    l.used = true;
    for (size_t i = 0; i < codes.size(); ++i) {
        RequirementOp op;
        op.op = codes[i];
        if (op.op == 0xFF02 || op.op == 0xFF03 || op.op == 0xFF04 || op.op == 0xFF25) op.arg = i + 1 < codes.size() ? codes[++i] : 0;
        l.ops.push_back(op);
    }
    return l;
}

// --- What a file holds -----------------------------------------------------------------------------------------

struct DatValue {
    std::string dat;    // "units"
    std::string field;  // the old editor's name (EUD Editor 3), or "" (EUD Editor 2: by index)
    int fieldIndex = -1;
    int entry = 0;      // the entry (EUD Editor 3), or the index from the field's first entry (EUD Editor 2)
    long long value = 0;
    bool delta = false;  // EUD Editor 2: the difference from the game's value
};
struct Script {
    std::string path;   // in the project ("TriggerEditor/func/unit.eps")
    std::string text;
    std::string from;   // where the text came from when not the file (a linked file), for the report
    int type = 1;       // EUD Editor 3's EFileType
    bool main = false;
    std::string generated;  // why only the old editor's build has this file's code ("" it builds as written)
};
struct Model {
    std::string kind, version;
    fs::path file, map, output, buildFolder;
    std::string tempLoc;  // EUD Editor 3's build folder setting
    std::vector<DatValue> dat;
    std::map<int, std::string> strings;  // entry (0-based) -> raw text
    std::map<int, std::vector<Button>> buttons;
    std::map<int, int> unitSets;
    int otherSets = 0;  // button set slots past the units that use another set (StarDraft keeps a set per unit)
    struct Req {
        int kind = 0, entry = 0;
        RequirementList list;
    };
    std::vector<Req> reqs;
    std::map<int, UnitLooks::Unit> looks;
    std::vector<Script> scripts, files;
    bool libraryUsed = false;  // a script imports EUD Editor 3's TriggerEditor library
    Sections plugins;
    std::vector<std::string> generated;  // the parts that only the old editor's build makes, in words
    std::vector<std::string> unsupported, settings;
    std::string labels;  // EUD Editor 3's notes for entries, as a text file
    int labelCount = 0;
    bool trace = false, turbo = false;
    bool e2Triggers = false;  // EUD Editor 2's trigger editor has triggers
    bool e2PluginsFromBuild = false;  // EUD Editor 2 plugins whose sections only its build has (MurakamiShiinaQC, ...)
};

// scripts/<name>, or scripts/<stem> 2<ext> (3, ...) when a script of the model has that path: two sections of a .eds can
// name files with the same name in different folders.
std::string uniqueScript(const Model& m, const std::string& name) {
    const fs::path n = pathOf(name);
    std::string path = "scripts/" + name;
    auto taken = [&](const std::string& p) { return std::any_of(m.scripts.begin(), m.scripts.end(), [&](const Script& s) { return lower(s.path) == lower(p); }); };
    for (int k = 2; taken(path); ++k) path = "scripts/" + u8(n.stem()) + " " + std::to_string(k) + u8(n.extension());
    return path;
}

// --- EUD Editor 3 ----------------------------------------------------------------------------------------------

// The notes that EUD Editor 3 gives entries itself (its CodeGrouping), which a project holds too: a unit's StarEdit
// subtitle (a hero's "Tank Mode"), a weapon's units ("Terran Marine", "Goliath G, Goliath Turret A"). The other .dat
// files have none.
std::vector<std::string> ee3DefaultNotes(const std::string& dat, const GameData& data) {
    std::vector<std::string> out;
    if (dat != "units" && dat != "weapons") return out;
    StatTxt names;
    DatTable units;
    std::string err;
    const DatSchema* us = DatSchema::find("units");
    if (!us || !names.load(data, err) || !units.load(data, *us, err)) return out;
    const DatField *flags = units.field("Staredit Group Flags"), *ground = units.field("Ground Weapon"), *air = units.field("Air Weapon");
    if (!flags || !ground || !air) return out;
    constexpr int kUnits = 228, kWeapons = 130;
    if (dat == "units") {
        out.assign(kUnits, {});
        for (int u = 0; u < kUnits && u < names.count(); ++u) {
            const std::string& rest = names.rest(u);
            const std::string subtitle = rest.substr(0, rest.find('\0'));
            const uint32_t f = units.stock(*flags, u);
            if ((f & 7) || (f & 0x80))
                if (subtitle != "*") out[size_t(u)] = subtitle;
        }
        return out;
    }
    out.assign(kWeapons, {});
    auto add = [&](uint32_t w, const std::string& name) {
        if (w >= uint32_t(kWeapons)) return;
        std::string& s = out[w];
        s += (s.empty() ? "" : ", ") + name;
    };
    for (int u = 0; u < kUnits && u < names.count(); ++u) {
        const uint32_t gw = units.stock(*ground, u), aw = units.stock(*air, u);
        if (gw == aw) add(gw, names.stock(u));
        else add(gw, names.stock(u) + " G"), add(aw, names.stock(u) + " A");
    }
    return out;
}

// A note is EUD Editor 3's own when it is the same, or (a weapon's: its units, in the language of EUD Editor 3, which
// can be another than the game's) the same list: as many names, each with the same " G" or " A".
bool sameNote(const std::string& own, const std::string& note, bool list) {
    if (own == note) return true;
    if (!list || own.empty()) return false;
    auto shape = [](const std::string& s) {
        std::string out;
        size_t at = 0;
        for (;;) {
            const size_t comma = s.find(", ", at);
            const std::string item = s.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            out += item.size() > 2 && (item.compare(item.size() - 2, 2, " G") == 0 || item.compare(item.size() - 2, 2, " A") == 0) ? item.back() : '-';
            if (comma == std::string::npos) return out;
            at = comma + 2;
        }
    };
    return shape(own) == shape(note);
}

bool readE3s(const fs::path& file, const GameData& data, Model& m, std::string& error) {
    const std::string bytes = readAll(file);
    nrbf::Graph g;
    if (!g.read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), error)) return false;
    const nrbf::Object* root = g.root();
    if (!root || root->shortName() != "SaveableData") return error = "the file is not an EUD Editor 3 project (its root is not SaveableData)", false;
    if (const nrbf::Object* v = g.get(root, "LastVersion"))
        m.version = std::to_string(g.number(v, "Major")) + "." + std::to_string(g.number(v, "Minor")) + "." + std::to_string(g.number(v, "Build")) + "." +
                    std::to_string(g.number(v, "Revision"));
    auto mapPath = [&](const char* abs, const char* rel) {
        const fs::path a = localPath(g.text(root, abs)), r = localPath(g.text(root, rel));
        if (present(a)) return a;
        if (!r.empty() && present(file.parent_path() / r)) return (file.parent_path() / r).lexically_normal();
        if (!a.empty() && present(file.parent_path() / a.filename())) return file.parent_path() / a.filename();  // (moved with it)
        return a.empty() && !r.empty() ? (file.parent_path() / r).lexically_normal() : a;
    };
    m.map = mapPath("OpenMapName", "RelativeOpenMapName");
    m.output = mapPath("SaveMapName", "RelativeSaveMapName");
    m.tempLoc = g.text(root, "TempFileLoc");

    // The .dat values: each value that is not the default one (its _IsDefault is false), as EUD Editor 3's build writes it.
    const nrbf::Object* dat = g.get(root, "Dat");
    std::string labels;
    for (const nrbf::Value& dv : g.items(g.get(dat, "Datfile"))) {
        const nrbf::Object* df = g.get(dv);
        const std::string name = g.text(df, "FIleName");
        const auto params = g.items(g.get(df, "Paramaters"));
        for (size_t pi = 0; pi < params.size(); ++pi) {
            const nrbf::Object* p = g.get(params[pi]);
            if (!p || !g.value(p, "Enabled").asBool()) continue;  // (EUD Editor 3's "prohibited" fields: it never writes them)
            const std::string field = g.text(p, "ParamaterName");
            const int start = int(g.number(p, "VarStart"));
            const auto vals = g.items(g.get(p, "Values"));
            for (size_t k = 0; k < vals.size(); ++k) {
                const nrbf::Object* v = g.get(vals[k]);
                if (!v || g.value(v, "IsDefault").asBool() || !g.value(v, "Enabled").asBool()) continue;
                m.dat.push_back({name, field, int(pi), start + int(k), g.number(v, "Data"), false});
            }
        }
        // The notes that the user wrote for entries (CodeToolTip): the ones that are not EUD Editor 3's own.
        const auto tips = g.items(g.get(df, "CodeToolTip"));
        const std::vector<std::string> own = ee3DefaultNotes(name, data);
        for (size_t k = 0; k < tips.size(); ++k)
            if (tips[k].kind == nrbf::Value::Kind::String && !trim(tips[k].s).empty() && (k >= own.size() || !sameNote(own[k], tips[k].s, name == "weapons")))
                labels += name + ".dat " + std::to_string(k) + ": " + tips[k].s + "\n", ++m.labelCount;
    }

    // stat_txt.tbl (ExtraDat._Stat_txt: entry i, "NULLSTRING" when not changed).
    const nrbf::Object* extra = g.get(root, "ExtraDat");
    {
        const auto texts = g.items(g.get(extra, "Stat_txt"));
        for (size_t i = 0; i < texts.size(); ++i)
            if (texts[i].kind == nrbf::Value::Kind::String && texts[i].s != "NULLSTRING") m.strings[int(i)] = ee3Text(texts[i].s);
    }
    // Wireframes and status panels: a value and a "default" flag per unit.
    auto bytesOf = [&](const char* member) {
        std::vector<int> out;
        if (const nrbf::Object* a = g.get(extra, member)) {
            if (a->values.size() == 1 && a->values[0].kind == nrbf::Value::Kind::Bytes)
                for (unsigned char c : a->values[0].s) out.push_back(c);
            else
                for (const auto& v : a->values) out.push_back(int(v.asInt()));
        }
        return out;
    };
    auto flagsOf = [&](const char* member) {
        std::vector<bool> out;
        if (const nrbf::Object* a = g.get(extra, member))
            for (const auto& v : a->values) out.push_back(v.isNull() || v.asBool());
        return out;
    };
    {
        const auto wire = bytesOf("WireFrame"), grp = bytesOf("GrpFrame"), tran = bytesOf("TranFrame");
        const auto wireD = flagsOf("DefaultWireFrame"), grpD = flagsOf("DefaultGrpFrame"), tranD = flagsOf("DefaultTranFrame");
        const auto fn1 = bytesOf("statusFn1"), fn2 = bytesOf("statusFn2");
        const auto fn1D = flagsOf("statusFn1IsDefault"), fn2D = flagsOf("statusFn2IsDefault");
        for (int u = 0; u < UnitLooks::kUnits; ++u) {
            UnitLooks::Unit look;
            if (size_t(u) < wire.size() && size_t(u) < wireD.size() && !wireD[size_t(u)] && wire[size_t(u)] != u) look.wireframe = wire[size_t(u)];
            if (size_t(u) < grp.size() && size_t(u) < grpD.size() && !grpD[size_t(u)] && grp[size_t(u)] != u && u < UnitLooks::kGroupWireframes)
                look.groupWireframe = grp[size_t(u)];
            if (size_t(u) < tran.size() && size_t(u) < tranD.size() && !tranD[size_t(u)] && tran[size_t(u)] != u && u < UnitLooks::kTransportWireframes)
                look.transportWireframe = tran[size_t(u)];
            if (size_t(u) < fn1.size() && size_t(u) < fn1D.size() && !fn1D[size_t(u)] && size_t(fn1[size_t(u)]) < std::size(kStatusFn))
                look.status = kStatusFn[size_t(fn1[size_t(u)])];
            if (size_t(u) < fn2.size() && size_t(u) < fn2D.size() && !fn2D[size_t(u)] && size_t(fn2[size_t(u)]) < std::size(kDisplayFn))
                look.display = kDisplayFn[size_t(fn2[size_t(u)])];
            if (look != UnitLooks::Unit{}) m.looks[u] = look;
        }
        // The button set that each slot uses (_ButtonSet; default: its own).
        const auto sets = bytesOf("ButtonSet");
        const auto setsD = flagsOf("DefaultButtonSet");
        for (size_t s = 0; s < sets.size() && s < setsD.size(); ++s) {
            if (setsD[s] || sets[s] == int(s)) continue;
            if (s < size_t(ButtonSets::kUnits) && sets[s] < ButtonSets::kSets) m.unitSets[int(s)] = sets[s];
            else ++m.otherSets;
        }
    }
    // Button sets (_ButtonData.ButtonSets: a set that is not the default one has its whole list).
    {
        const auto sets = g.items(g.get(g.get(extra, "ButtonData"), "ButtonSets"));
        for (size_t s = 0; s < sets.size() && s < size_t(ButtonSets::kSets); ++s) {
            const nrbf::Object* set = g.get(sets[s]);
            if (!set || g.value(set, "DefaultUse").asBool()) continue;
            std::vector<Button> list;
            for (const nrbf::Value& bv : g.items(g.get(set, "pButtonSets"))) {
                const nrbf::Object* b = g.get(bv);
                if (!b) continue;
                Button x;
                x.pos = uint16_t(g.number(b, "pos")), x.icon = uint16_t(g.number(b, "icon"));
                x.condition = uint32_t(g.number(b, "con")), x.action = uint32_t(g.number(b, "act"));
                x.conditionArg = uint16_t(g.number(b, "conval")), x.actionArg = uint16_t(g.number(b, "actval"));
                x.enabledString = uint16_t(g.number(b, "enaStr")), x.disabledString = uint16_t(g.number(b, "disStr"));
                list.push_back(x);
            }
            m.buttons[int(s)] = std::move(list);
        }
    }
    // Requirements (RequireDatas: units, upgrades, research, tech use, orders; an entry's _UseStatus: 0 the game's,
    // 1 not used, 2 always, 3 and 4 its blocks).
    {
        const nrbf::Object* reqs = g.get(extra, "RequireDatas");
        const auto kinds = reqs ? g.items(reqs) : std::vector<nrbf::Value>{};
        for (size_t k = 0; k < kinds.size() && k < size_t(Requirements::kKinds); ++k) {
            const auto entries = g.items(g.get(g.get(kinds[k]), "RequireDatas"));
            for (size_t e = 0; e < entries.size(); ++e) {
                const nrbf::Object* r = g.get(entries[e]);
                const int use = r ? int(g.number(r, "UseStatus")) : 0;
                if (use == 0) continue;
                Model::Req q;
                q.kind = int(k), q.entry = int(e);
                if (use == 1) q.list.used = false;
                else if (use == 2) q.list.used = true;
                else {
                    std::vector<uint16_t> codes;
                    for (const nrbf::Value& bv : g.items(g.get(r, "ReauireBlocks"))) {
                        const nrbf::Object* b = g.get(bv);
                        if (!b) continue;
                        const int op = int(g.number(b, "opCode")), val = int(g.number(b, "value"));
                        if (op == 0) codes.push_back(uint16_t(val));  // a unit the player must have
                        else {
                            codes.push_back(uint16_t(0xFF00 + op));
                            if (op == 2 || op == 3 || op == 4 || op == 37) codes.push_back(uint16_t(val));
                        }
                    }
                    q.list = requirementList(codes);
                }
                m.reqs.push_back(std::move(q));
            }
        }
    }
    // The other notes (stat_txt and button sets: ToolTipDic; the default is the entry's number).
    for (const auto& [key, list] : g.pairs(g.get(extra, "ToolTipDic"))) {
        const int which = int(g.number(key));
        const auto tips = g.items(g.get(list));
        for (size_t k = 0; k < tips.size(); ++k) {
            const std::string t = trim(tips[k].s);
            if (tips[k].kind != nrbf::Value::Kind::String || t.empty() || t == std::to_string(k) || t == std::to_string(k + 1)) continue;
            labels += std::string(which == 11 ? "stat_txt.tbl " + std::to_string(k + 1) : "button set " + std::to_string(k)) + ": " + t + "\n";
            ++m.labelCount;
        }
    }
    if (!labels.empty())
        m.labels = "EUD Editor 3's notes for entries, from " + u8(file.filename()) + " (StarDraft does not show them):\n\n" + labels;

    // The trigger editor's files (TEData.ProjectFile: folders and files; a module's path is its folders').
    const nrbf::Object* te = g.get(root, "TEData");
    const nrbf::Value& mainRef = g.value(te, "MainFile");
    const int32_t mainId = mainRef.kind == nrbf::Value::Kind::Object ? mainRef.ref : 0;
    std::set<std::string> macroNames(std::begin(kEe3MacroNames), std::end(kEe3MacroNames));
    std::set<std::string> taken;
    std::set<int32_t> seen;  // (a folder or file once: references can make cycles)
    std::function<void(const nrbf::Object*, const std::string&, int)> walk = [&](const nrbf::Object* folder, const std::string& at, int depth) {
        if (!folder || depth > 64 || !seen.insert(folder->id).second) return;
        for (const nrbf::Value& fv : g.items(g.get(folder, "Files"))) {
            const nrbf::Object* f = g.get(fv);
            if (!f || !seen.insert(f->id).second) continue;
            const int type = int(g.number(f, "FileType"));
            if (type == 0 || type == 5) continue;  // (a folder in the file list; the settings node)
            const nrbf::Object* sc = g.get(f, "Scripter");
            Script s;
            s.type = type;
            s.main = fv.ref == mainId || f->id == mainId;
            const std::string ext = type == 1 || type == 3 || type == 6 ? ".eps" : type == 2 || type == 4 ? ".py" : type == 7 ? ".lua" : "";
            std::string path = at + safeName(g.text(f, "FileName")) + ext;
            for (int n = 2; taken.count(lower(path)); ++n) path = at + safeName(g.text(f, "FileName")) + " " + std::to_string(n) + ext;
            taken.insert(lower(path));
            s.path = path;
            s.text = g.text(sc, "String");
            // A linked file (_ConnectFile): EUD Editor 3 reads the text from it when it is there.
            // Only a script file is read (an .eps, .py or .lua, or a text file for a text node), never a network path.
            fs::path linked = localPath(g.text(sc, "ConnectFile"));
            if (!present(linked) && !g.text(sc, "ConnectRelativeFile").empty() && !localPath(g.text(sc, "ConnectRelativeFile")).empty())
                linked = file.parent_path() / localPath(g.text(sc, "ConnectRelativeFile"));
            const std::string lext = lower(linked.extension().string());
            const bool scriptFile = lext == ".eps" || lext == ".py" || lext == ".lua" || lext == ".txt" || (type == 8 && lext.empty());
            if (scriptFile && present(linked) && fs::is_regular_file(linked)) s.text = readAll(linked), s.from = u8(linked);
            if (type == 3 || type == 4) s.generated = "a block script";
            else if (type == 6) s.generated = "classic triggers";
            else if (type == 1 || type == 2) {
                // Lua macros: "@Name(" with one of EUD Editor 3's macro functions, or "<? ... ?>".
                for (size_t i = s.text.find('@'); i != std::string::npos && s.generated.empty(); i = s.text.find('@', i + 1)) {
                    size_t e = i + 1;
                    while (e < s.text.size() && (std::isalnum(static_cast<unsigned char>(s.text[e])) || s.text[e] == '_')) ++e;
                    if (e < s.text.size() && s.text[e] == '(' && macroNames.count(s.text.substr(i + 1, e - i - 1))) s.generated = "macros";
                }
                if (s.generated.empty())
                    if (const size_t o = s.text.find("<?"); o != std::string::npos && s.text.find("?>", o + 2) != std::string::npos) s.generated = "macros";
                if (s.text.find("TriggerEditor.") != std::string::npos) m.libraryUsed = true;
            }
            if (type == 7 || type == 8) m.files.push_back(std::move(s));
            else m.scripts.push_back(std::move(s));
        }
        for (const nrbf::Value& dv : g.items(g.get(folder, "Folders"))) {
            const nrbf::Object* d = g.get(dv);
            if (!d || g.number(d, "FileType") == 5) continue;  // (EUD Editor 3's settings node)
            std::string name = safeName(g.text(d, "FileName"));
            walk(d, at + name + "/", depth + 1);
        }
    };
    walk(g.get(te, "ProjectFile"), "TriggerEditor/", 0);
    std::stable_partition(m.scripts.begin(), m.scripts.end(), [](const Script& s) { return s.main; });
    for (const Script& s : m.scripts)
        if (!s.generated.empty()) m.generated.push_back(s.path + " (" + s.generated + ")");
    // BGM and dot images: their code is made by EUD Editor 3's build.
    if (const nrbf::Object* bgm = g.get(te, "BGMData"))
        if (const size_t n = g.items(g.get(bgm, "BGMList")).size()) m.generated.push_back("the BGM player (" + std::to_string(n) + (n == 1 ? " sound)" : " sounds)"));
    if (const size_t n = g.items(g.get(te, "DotDatas")).size()) m.generated.push_back("the dot images (" + std::to_string(n) + ")");
    if (const nrbf::Object* sca = g.get(te, "SCArchive"); sca && g.value(sca, "IsUsed").asBool())
        m.unsupported.push_back("SC Archive (its online save and load): StarDraft has no online service");

    // euddraft: the user's own blocks (UserPlugin), in their order; a block's lines before its first [section] go to the
    // section before it. EUD Editor 3 makes the other blocks when it builds.
    std::string userText;
    for (const nrbf::Value& bv : g.items(g.get(g.get(root, "EdsBlocks"), "pBlocks"))) {
        const nrbf::Object* b = g.get(bv);
        if (b && g.number(b, "BType") == 6) userText += g.text(b, "pTexts") + "\n";
    }
    for (auto& [sec, body] : edsSections(userText)) {
        std::vector<std::string> keep;
        for (const std::string& l : body)
            if (!l.empty()) keep.push_back(l);
        auto it = std::find_if(m.plugins.begin(), m.plugins.end(), [&](const auto& p) { return p.first == sec; });
        if (it == m.plugins.end()) m.plugins.push_back({sec, keep});
        else it->second.insert(it->second.end(), keep.begin(), keep.end());
    }
    m.turbo = std::any_of(m.plugins.begin(), m.plugins.end(), [](const auto& p) { return p.first == "eudTurbo"; });
    // MSQC and chatEvent: EUD Editor 3 writes their lines from the macros (KeyDown, ChatEvent, ...).
    const bool macros = std::any_of(m.scripts.begin(), m.scripts.end(), [](const Script& s) { return s.generated == "macros"; });
    if (macros && g.value(te, "UseMSQC").asBool()) m.generated.push_back("the MSQC keys and mouse of the macros");
    if (macros && g.value(te, "UseChatEvent").asBool()) m.generated.push_back("the chat events of the macros");

    // Its build settings.
    if (g.value(root, "AutoBuild").asBool()) m.settings.push_back("build after save: build with Ctrl+B");
    if (const nrbf::Value& tbl = g.value(root, "UseCustomTbl"); !tbl.isNull() && !tbl.asBool() && !m.strings.empty())
        m.settings.push_back("custom stat_txt.tbl is off in EUD Editor 3: the texts come over and are used");
    return true;
}

// --- EUD Editor 2 ----------------------------------------------------------------------------------------------

// EUD Editor 2's sections: S_<name> ... E_<name>.
std::map<std::string, std::vector<std::string>> e2sSections(const std::string& text) {
    std::map<std::string, std::vector<std::string>> out;
    std::string cur;
    for (const std::string& l : lines(text)) {
        if (cur.empty()) {
            if (l.rfind("S_", 0) == 0) cur = l.substr(2), out[cur];
            continue;
        }
        if (l == "E_" + cur) {
            cur.clear();
            continue;
        }
        out[cur].push_back(l);
    }
    return out;
}

bool readE2s(const fs::path& file, const GameData& data, Model& m, std::string& error) {
    auto sec = e2sSections(readAll(file));
    if (!sec.count("ProjectSET")) return error = "the file is not an EUD Editor 2 project (it has no S_ProjectSET)", false;
    auto settings = [&](const char* s) {
        std::map<std::string, std::string> out;
        std::string k, v;
        for (const std::string& l : sec[s])
            if (keyValue(l, k, v)) out[k] = v;
        return out;
    };
    auto project = settings("ProjectSET");
    m.version = project["Version"];
    m.map = localPath(project["InputMap"]);
    if (!present(m.map) && present(file.parent_path() / m.map.filename())) m.map = file.parent_path() / m.map.filename();  // (moved with it)
    m.output = localPath(project["OutputMap"]);
    m.trace = project["epTraceDebug"] == "True";
    // A file that EUD Editor 3 saved as .e2s has fewer sections (no PluginSET) and EUD Editor 3's order of the .dat
    // files (portdata 8, sfxdata 9); EUD Editor 2's own has sfxdata 8, portdata 9.
    const bool byEe3 = !sec.count("PluginSET");
    static const char* const kDats[] = {"units", "weapons", "flingy", "sprites", "images", "upgrades", "techdata", "orders", "sfxdata", "portdata"};
    for (const std::string& l : sec["DatEditSET"]) {
        long long r[4] = {};
        if (std::sscanf(l.c_str(), "%lld,%lld,%lld,%lld", &r[0], &r[1], &r[2], &r[3]) != 4 || r[3] == 0) continue;
        int d = int(r[0]);
        if (byEe3 && (d == 8 || d == 9)) d = 17 - d;
        m.dat.push_back({d >= 0 && d < int(std::size(kDats)) ? kDats[d] : "?" + std::to_string(r[0]), "", int(r[1]), int(r[2]), r[3], true});
    }
    // FireGraft: the status panel functions, as the difference from the game's index (FireGraft<unit> : d1,d2,...).
    UnitLooks stockLooks;
    auto fg = settings("FireGraftSET");
    for (int u = 0; u < UnitLooks::kUnits; ++u) {
        const auto it = fg.find("FireGraft" + std::to_string(u));
        if (it == fg.end() || it->second == "0") continue;
        int d1 = 0, d2 = 0;
        if (std::sscanf(it->second.c_str(), "%d,%d", &d1, &d2) < 2 || (d1 == 0 && d2 == 0)) continue;
        UnitLooks::Unit look = m.looks[u];
        const int s = indexOf(kStatusFn, std::size(kStatusFn), stockLooks.stockStatus(u)) + d1;
        const int p = indexOf(kDisplayFn, std::size(kDisplayFn), stockLooks.stockDisplay(u)) + d2;
        if (d1 && s >= 0 && size_t(s) < std::size(kStatusFn)) look.status = kStatusFn[size_t(s)];
        if (d2 && p >= 0 && size_t(p) < std::size(kDisplayFn)) look.display = kDisplayFn[size_t(p)];
        m.looks[u] = look;
    }
    // Button sets: BtnUse<i> : True, BtnData<i> : 8 numbers per button.
    auto btn = settings("BtnSET");
    for (int s = 0; s < ButtonSets::kSets; ++s) {
        if (btn["BtnUse" + std::to_string(s)] != "True") continue;
        std::vector<long long> n;
        std::istringstream in(btn["BtnData" + std::to_string(s)]);
        for (std::string t; std::getline(in, t, ',');)
            if (!trim(t).empty()) n.push_back(std::atoll(t.c_str()));
        std::vector<Button> list;
        for (size_t b = 0; b + 8 <= n.size(); b += 8) {
            Button x;
            x.pos = uint16_t(n[b]), x.icon = uint16_t(n[b + 1]), x.condition = uint32_t(n[b + 2]), x.action = uint32_t(n[b + 3]);
            x.conditionArg = uint16_t(n[b + 4]), x.actionArg = uint16_t(n[b + 5]), x.enabledString = uint16_t(n[b + 6]), x.disabledString = uint16_t(n[b + 7]);
            list.push_back(x);
        }
        m.buttons[s] = std::move(list);
    }
    // Requirements: ReqUse<kind>,<entry> : 0 the game's, 1 not used, 2 always, 3 its codes (ReqCount, ReqData).
    auto req = settings("ReqSET");
    for (int k = 0; k < Requirements::kKinds; ++k)
        for (int e = 0; e < Requirements::kind(k).entries; ++e) {
            const std::string at = std::to_string(k) + "," + std::to_string(e);
            const int use = std::atoi(req["ReqUse" + at].c_str());
            if (use == 0) continue;
            Model::Req q;
            q.kind = k, q.entry = e;
            if (use == 1) q.list.used = false;
            else if (use == 2) q.list.used = true;
            else {
                std::vector<uint16_t> codes;
                const int n = std::atoi(req["ReqCount" + at].c_str());
                for (int p = 0; p < n && p < 4096; ++p) codes.push_back(uint16_t(std::atoi(req["ReqData" + at + "," + std::to_string(p)].c_str())));
                q.list = requirementList(codes);
            }
            m.reqs.push_back(std::move(q));
        }
    // stat_txt.tbl's texts and the wireframes (FileManagerSET).
    auto fm = settings("FileManagerSET");
    for (int i = 0, count = std::atoi(fm["stattextdicCount"].c_str()); i < count; ++i) {
        const auto key = fm.find("stattextdickey" + std::to_string(i)), val = fm.find("stattextdicvalue" + std::to_string(i));
        if (key != fm.end() && val != fm.end()) m.strings[std::atoi(key->second.c_str())] = ee2Text(val->second);
    }
    if (fm["wireuse"] == "True")
        for (int u = 0; u < UnitLooks::kUnits; ++u) {
            auto frame = [&](const char* key) { return std::atoi(fm[key + std::to_string(u)].c_str()) - 1; };
            UnitLooks::Unit look = m.looks.count(u) ? m.looks[u] : UnitLooks::Unit{};
            if (const int w = frame("wireframData"); w >= 0 && w != u) look.wireframe = w;
            if (const int w = frame("grpwireData"); w >= 0 && w != u && u < UnitLooks::kGroupWireframes) look.groupWireframe = w;
            if (const int w = frame("tranwireData"); w >= 0 && w != u && u < UnitLooks::kTransportWireframes) look.transportWireframe = w;
            if (look != UnitLooks::Unit{}) m.looks[u] = look;
        }
    std::erase_if(m.looks, [](const auto& l) { return l.second == UnitLooks::Unit{}; });

    // The plugins: EUD Editor 2's check boxes and its extra .eds text. MurakamiShiinaQC, grpinjector and the dataDumper's
    // files are written by its build (with names from the map and its own folder): they come from the build.
    auto plugins = settings("PluginSET");
    static const std::pair<const char*, const char*> kPlugins[] = {{"soundstopper", "soundstopper"}, {"scmloader", "scmloader"},
                                                                    {"noAirColsion", "noAirCollision"}, {"unlimiter", "unlimiter"},
                                                                    {"keepSTR", "keepSTR"}, {"eudTurbo", "eudTurbo"}};
    for (const auto& [key, section] : kPlugins)
        if (plugins[key] == "True") m.plugins.push_back({section, {}});
    if (plugins["iscriptPatcheruse"] == "True" && !plugins["iscriptPatcher"].empty()) m.plugins.push_back({"iscriptPatcher", {"iscript : " + plugins["iscriptPatcher"]}});
    if (plugins["unpatcheruse"] == "True") {
        std::string conds = plugins["unpatcher"];
        for (size_t at; (at = conds.find("##")) != std::string::npos;) conds.replace(at, 2, ", ");
        m.plugins.push_back({"unpatcher", {"resetCond : [" + conds + "]"}});
    }
    {
        std::string extra = plugins["extraedssetting"];
        for (size_t at; (at = extra.find("##")) != std::string::npos;) extra.replace(at, 2, "\n");
        for (auto& [s, body] : edsSections(extra)) {
            if (const fs::path p = pathOf(s); p.extension() == ".eps" || p.extension() == ".py") {  // (a script of the user's)
                Script sc;
                const fs::path f = p.is_absolute() ? p : file.parent_path() / p;
                sc.path = uniqueScript(m, safeName(u8(p.filename()))), sc.from = u8(f), sc.text = present(f) ? readAll(f) : std::string(), sc.main = true;
                if (!present(f)) sc.generated = "missing";
                m.scripts.push_back(std::move(sc));
                continue;
            }
            std::vector<std::string> keep;
            for (const std::string& l : body)
                if (!l.empty()) keep.push_back(l);
            m.plugins.push_back({s, keep});
        }
    }
    m.turbo = plugins["eudTurbo"] == "True";
    m.e2PluginsFromBuild = plugins["nqcuse"] == "True" || plugins["grpinjectoruse"] == "True" || plugins["dataDumperuse"] == "True";
    if (plugins["nqcuse"] == "True") m.generated.push_back("the plugin MurakamiShiinaQC (keys and mouse)");
    if (plugins["grpinjectoruse"] == "True") m.generated.push_back("the plugin grpinjector (cursor pictures)");
    if (plugins["dataDumperuse"] == "True") m.generated.push_back("the plugin dataDumper (its files)");

    // The trigger editor: its triggers are code that EUD Editor 2 writes when it builds (TriggerEditor.eps).
    const auto& trig = sec["TriggerEditorSET"];
    m.e2Triggers = std::count_if(trig.begin(), trig.end(), [](const std::string& l) { return l.rfind("Type:", 0) == 0; }) > 7;  // (the 7 empty groups)
    if (m.e2Triggers) m.generated.push_back("TriggerEditor.eps (the trigger editor's triggers)");

    // Parts that StarDraft has no editor for (only when they are used: their sections have more than the defaults).
    const auto used = [&](int k) { return project["UsedSetting" + std::to_string(k)] == "True"; };
    auto grp = settings("GRPEditorSET");
    if (used(5) && std::atoi(grp["GRPEditorDATCount"].c_str()) > 0)
        m.unsupported.push_back("the GRP editor's pictures (" + grp["GRPEditorDATCount"] + "): StarDraft does not put GRP files into the map");
    if (used(3) && sec["TileSET"].size() > 7) m.unsupported.push_back("the tileset edits: StarDraft does not change tilesets");
    if (used(2) && sec["BinEditSET"].size() > 2) m.unsupported.push_back("the dialog (.bin) edits: StarDraft does not change dialogs");
    if (sec["SoundPlayerSET"].size() > 1) m.unsupported.push_back("the sound player's sounds: add the sounds in the Files workspace");
    if (settings("SCDBSet")["SCDBUse"] == "True") m.unsupported.push_back("SCDB (its online save and load): StarDraft has no online service");
    if (m.trace) m.settings.push_back("epTrace debug: the project's Trace build is on");
    (void)data;
    return true;
}

// --- euddraft --------------------------------------------------------------------------------------------------

bool readEds(const fs::path& file, Model& m, std::string& error) {
    const auto sections = edsSections(readAll(file));
    m.map = pathOf(edsInput(sections));
    if (m.map.empty()) return error = "the .eds has no input map ([main] input)", false;
    if (m.map.is_relative()) m.map = file.parent_path() / m.map;
    for (const auto& [sec, body] : sections) {
        if (sec == "main") {
            for (const std::string& l : body)
                if (l.rfind("output", 0) == 0 && l.find(':') != std::string::npos) {
                    m.output = pathOf(trim(l.substr(l.find(':') + 1)));
                    if (m.output.is_relative()) m.output = file.parent_path() / m.output;
                }
            continue;
        }
        const fs::path p = pathOf(sec);
        if (p.extension() == ".eps" || p.extension() == ".py") {
            Script s;
            const fs::path f = p.is_absolute() ? p : file.parent_path() / p;
            s.path = uniqueScript(m, safeName(u8(p.filename())));
            s.from = u8(f);
            s.text = present(f) ? readAll(f) : std::string();
            s.main = true;  // (each script of a .eds is a section of its own)
            if (!present(f)) s.generated = "missing";
            m.scripts.push_back(std::move(s));
            continue;
        }
        std::vector<std::string> keep;
        for (const std::string& l : body)
            if (!l.empty()) keep.push_back(l);
        m.plugins.push_back({sec, keep});
    }
    m.turbo = std::any_of(m.plugins.begin(), m.plugins.end(), [](const auto& p) { return p.first == "eudTurbo"; });
    return true;
}

bool readModelOf(const fs::path& file, const GameData& data, Model& m, std::string& error) {
    m = Model{};
    m.kind = importKind(file);
    m.file = file;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return error = "could not open " + u8(file), false;
    if (m.kind == "e3s") return readE3s(file, data, m, error);
    if (m.kind == "e2s") return readE2s(file, data, m, error);
    if (m.kind == "eds") return readEds(file, m, error);
    return error = u8(file.filename()) + " is not an .e3s, .e2s, .eds or .edd project", false;
}
bool readModel(const fs::path& file, const GameData& data, Model& m, std::string& error) {
    try {
        return readModelOf(file, data, m, error);
    } catch (const std::exception& e) {  // (a damaged file that asks for too much memory)
        m = Model{};
        return error = std::string("the file is damaged: ") + e.what(), false;
    }
}

// --- The build folder ------------------------------------------------------------------------------------------

// The folder with the build file (EUDEditor.eds) in or under `dir`.
fs::path buildIn(const fs::path& dir, const std::string& stem) {
    if (dir.empty()) return {};
    const fs::path tries[] = {dir, dir / "eudplibData", dir / "eudplibdata", dir / ("BuildData_" + stem) / "eudplibData",
                              dir / "Data" / "temp" / ("BuildData_" + stem) / "eudplibData", dir / "Data" / "eudplibdata"};
    for (const fs::path& t : tries)
        if (present(t / "EUDEditor.eds")) return t;
    return {};
}
std::string projectStem(const fs::path& file) {
    // (EUD Editor 3's Tool.StripFileName: the name before its first dot, with characters a folder cannot have removed)
    std::string s = u8(file.filename());
    s = s.substr(0, s.find('.'));
    std::string out;
    for (char c : s)
        if (!std::strchr("\\/:*?\"<>|", c)) out += c;
    return out;
}
// Where the old editor builds: EUD Editor 3's BuildData_<name> (in the map's folder, the project's folder, or a folder of
// its own), EUD Editor 2's Data\eudplibdata (in its own folder, which is not known: the user selects it) or, for its
// projects (.e2p), the project's eudplibdata.
fs::path findBuild(const Model& m) {
    const std::string stem = projectStem(m.file);
    if (m.kind == "e3s") {
        const std::string t = m.tempLoc;
        std::vector<fs::path> dirs;
        if (t == "1" || t == "MapFolder") dirs.push_back(m.map.parent_path());
        else if (t == "2") dirs.push_back(m.file.parent_path());
        else if (!t.empty() && t != "0" && t != "DefaultFolder" && !localPath(t).empty()) dirs.push_back(localPath(t));
        dirs.push_back(m.file.parent_path()), dirs.push_back(m.map.parent_path());
        for (const fs::path& d : dirs)
            if (fs::path b = buildIn(d / ("BuildData_" + stem), stem); !b.empty()) return b;
        return {};
    }
    if (m.kind == "e2s") {
        for (const fs::path& d : {m.file.parent_path(), m.file.parent_path() / ".reference-files"})
            if (present(d / "TriggerEditor.eps") || present(d / "EUDEditor.eds")) return d;
        return buildIn(m.file.parent_path(), stem);
    }
    return {};
}
bool buildAgrees(const Model& m, const fs::path& build) {
    if (build.empty() || m.kind == "e2s") return !build.empty();  // (EUD Editor 2 builds from a copy of the map when it edits the tileset)
    const fs::path input = pathOf(edsInput(edsSections(readAll(build / "EUDEditor.eds"))));
    return lower(u8(input.filename())) == lower(u8(m.map.filename()));
}

// --- The scan --------------------------------------------------------------------------------------------------

void describe(const Model& m, const fs::path& build, bool buildOk, ImportScan& out) {
    out.kind = m.kind, out.version = m.version, out.file = m.file, out.map = m.map, out.mapFound = present(m.map) && !fs::is_directory(m.map);
    out.output = m.output, out.buildFolder = build, out.buildMatches = buildOk;
    auto add = [&](ImportPart p) { out.parts.push_back(std::move(p)); };
    if (!m.dat.empty()) {
        ImportPart p{"dat", int(m.dat.size())};
        std::map<std::string, int> per;
        for (const DatValue& v : m.dat) ++per[v.dat];
        for (const char* d : {"units", "weapons", "flingy", "sprites", "images", "upgrades", "techdata", "orders", "sfxdata", "portdata"})
            if (per.count(d)) p.details.push_back(std::string(d) + ".dat (" + std::to_string(per[d]) + ")");
        add(p);
    }
    if (!m.strings.empty()) add({"strings", int(m.strings.size())});
    if (!m.buttons.empty() || !m.unitSets.empty()) {
        ImportPart p{"buttons", int(m.buttons.size())};
        for (const auto& [u, s] : m.unitSets) p.details.push_back(std::to_string(u) + " " + std::to_string(s));
        if (m.otherSets) p.why = std::to_string(m.otherSets) + " button set slots past the units use another set: StarDraft keeps one set per unit";
        add(p);
    }
    if (!m.reqs.empty()) add({"requirements", int(m.reqs.size())});
    if (!m.looks.empty()) add({"looks", int(m.looks.size())});
    if (!m.scripts.empty()) {
        ImportPart p{"scripts", int(m.scripts.size())};
        for (const Script& s : m.scripts) p.details.push_back(s.path);
        add(p);
    }
    if (!m.generated.empty()) {
        ImportPart p{"generated", int(m.generated.size()), m.generated, ImportPart::Way::Build};
        p.needsBuild = build.empty() || !buildOk;
        add(p);
    }
    if (!m.files.empty()) {
        ImportPart p{"files", int(m.files.size())};
        for (const Script& s : m.files) p.details.push_back(s.path);
        add(p);
    }
    if (!m.plugins.empty()) {
        ImportPart p{"plugins", int(m.plugins.size())};
        for (const auto& s : m.plugins) p.details.push_back(s.first);
        add(p);
    }
    if (m.labelCount) add({"labels", m.labelCount});
    if (!m.settings.empty()) add({"settings", int(m.settings.size()), m.settings, ImportPart::Way::None});
    if (!m.unsupported.empty()) add({"unsupported", int(m.unsupported.size()), m.unsupported, ImportPart::Way::None});
}

// --- The import ------------------------------------------------------------------------------------------------

bool copyTree(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) return error = "could not copy " + u8(from) + ": " + ec.message(), false;
    return true;
}
// The modules that a script imports ("import name as x;" in epScript, "import name" in Python) beside it: copied with it.
void copyModules(const fs::path& from, const fs::path& toDir, ImportReport& rep) {
    std::error_code ec;
    for (std::string l : lines(readAll(from))) {
        if (l.rfind("import ", 0) != 0 && l.rfind("from ", 0) != 0) continue;
        std::string mod = trim(l.substr(l.find(' ') + 1));
        mod = mod.substr(0, mod.find_first_of(" ;,\t"));
        if (mod.empty() || mod.find('.') != std::string::npos) continue;
        for (const char* ext : {".py", ".eps"}) {
            const fs::path f = from.parent_path() / (mod + ext), t = toDir / (mod + ext);
            if (!present(f) || present(t)) continue;
            fs::copy_file(f, t, ec);
            if (!ec) rep.imported.push_back("the module " + u8(f.filename()) + " (that " + u8(from.filename()) + " imports)");
        }
    }
}

bool applyData(const Model& m, const ImportChoices& c, const GameData& data, EudProject& pr, ImportReport& rep, std::string& error) {
    auto on = [&](const char* id) { return !c.skip.count(id); };
    if (on("dat") && !m.dat.empty()) {
        std::map<std::string, std::vector<const DatValue*>> per;
        for (const DatValue& v : m.dat) per[v.dat].push_back(&v);
        for (const auto& [dat, list] : per) {
            const DatSchema* schema = DatSchema::find(dat);
            DatTable t;
            std::string err;
            if (!schema || !t.load(data, *schema, err)) {
                rep.notImported.push_back(count(list.size(), "value", "values") + " of " + dat + ".dat: " + (schema ? err : "a .dat that StarDraft does not know"));
                continue;
            }
            int n = 0, skipped = 0;
            for (const DatValue* v : list) {
                const DatField* f = nullptr;
                if (v->fieldIndex >= 0 && size_t(v->fieldIndex) < schema->fields.size() &&
                    (v->field.empty() || lower(schema->fields[size_t(v->fieldIndex)].name) == lower(v->field)))
                    f = &schema->fields[size_t(v->fieldIndex)];
                for (size_t k = 0; !f && !v->field.empty() && k < schema->fields.size(); ++k)
                    if (lower(schema->fields[k].name) == lower(v->field)) f = &schema->fields[k];
                const int entry = f ? (v->delta ? f->first + v->entry : v->entry) : 0;
                if (!f || !f->covers(entry)) {
                    ++skipped;
                    continue;
                }
                const long long most = f->maxValue();
                const long long value = v->delta ? static_cast<long long>(t.stock(*f, entry)) + v->value : v->value;
                t.set(*f, entry, uint32_t(std::clamp<long long>(value, 0, most)));
                ++n;
            }
            if (n && !t.saveEdits(pr.datFile(dat), err)) return error = err, false;
            if (n) rep.imported.push_back(count(size_t(n), "value", "values") + " of " + dat + ".dat");
            if (skipped) rep.notImported.push_back(count(size_t(skipped), "value", "values") + " of " + dat + ".dat: fields or entries that the file does not have");
        }
    }
    std::string err;
    if (on("strings") && !m.strings.empty()) {
        StatTxt s;
        if (!s.load(data, err)) rep.notImported.push_back("the texts of stat_txt.tbl: " + err);
        else {
            int n = 0;
            for (const auto& [i, text] : m.strings)
                if (i >= 0 && i < s.count()) s.set(i, text), ++n;
            if (n && !s.saveEdits(pr.folder() / "dat" / "strings.json", err)) return error = err, false;
            if (n) rep.imported.push_back(count(size_t(n), "text", "texts") + " of stat_txt.tbl");
        }
    }
    if (on("buttons") && (!m.buttons.empty() || !m.unitSets.empty())) {
        ButtonSets b;
        for (const auto& [s, list] : m.buttons) b.set(s, list);
        for (const auto& [u, s] : m.unitSets) b.setUnitSet(u, s);
        if (!b.saveEdits(pr.folder() / "dat" / "buttons.json", err)) return error = err, false;
        if (!m.buttons.empty()) rep.imported.push_back(std::to_string(m.buttons.size()) + (m.buttons.size() == 1 ? " button set" : " button sets"));
        if (!m.unitSets.empty()) rep.imported.push_back(count(m.unitSets.size(), "unit that uses", "units that use") + " another unit's button set");
        if (m.otherSets) rep.notImported.push_back(std::to_string(m.otherSets) + " button set slots past the units that use another set: StarDraft keeps one set per unit");
    }
    if (on("requirements") && !m.reqs.empty()) {
        Requirements r;
        for (const Model::Req& q : m.reqs)
            if (q.entry < Requirements::kind(q.kind).entries) r.set(q.kind, q.entry, q.list);
        if (!r.saveEdits(pr.folder() / "dat" / "requirements.json", err)) return error = err, false;
        rep.imported.push_back(std::to_string(m.reqs.size()) + (m.reqs.size() == 1 ? " requirement list" : " requirement lists"));
    }
    if (on("looks") && !m.looks.empty()) {
        UnitLooks l;
        for (const auto& [u, look] : m.looks) l.set(u, look);
        if (!l.saveEdits(pr.folder() / "dat" / "unit_display.json", err)) return error = err, false;
        rep.imported.push_back("the status panels and wireframes of " + std::to_string(m.looks.size()) + (m.looks.size() == 1 ? " unit" : " units"));
    }
    if (on("labels") && !m.labels.empty()) {
        if (!writeText(pr.folder() / "notes" / "EUD Editor 3 notes.txt", m.labels, error)) return false;
        rep.imported.push_back(count(size_t(m.labelCount), "note", "notes") + " for entries, in notes/EUD Editor 3 notes.txt");
    }
    return true;
}

// EUD Editor 3's PluginVariables.py, for scripts that import it (msqcvar): its variables when no macro adds keys.
const char* const kPluginVariables =
    "from eudplib import *\n\n\nVChatIndex = EUDArray(8)\n\ndef Reg():\n"
    "    EUDRegisterObjectToNamespace(\"VChatIndex\", VChatIndex)\nEUDOnStart(Reg)\n";

bool applyScripts(const Model& m, const ImportChoices& c, const fs::path& build, EudProject& pr, ImportReport& rep, std::string& error) {
    auto on = [&](const char* id) { return !c.skip.count(id); };
    const fs::path dir = pr.folder();
    bool any = false;
    if (m.kind == "e3s" && on("scripts") && !m.scripts.empty()) {
        if (c.scriptsFromBuild && !build.empty() && present(build / "TriggerEditor")) {
            // The build's code: each file as EUD Editor 3 built it (macros expanded, block scripts and classic triggers as
            // epScript), with its library, PluginVariables.py, the BGM player and the dot images.
            if (!copyTree(build / "TriggerEditor", dir / "TriggerEditor", error)) return false;
            if (!on("generated")) {  // (the user left out the code that only the build has: the BGM player and the dot images)
                std::error_code ec;
                for (const char* f : {"BGMFlexible.eps", "dotData.eps"}) fs::remove(dir / "TriggerEditor" / f, ec);
            }
            if (on("generated") && present(build.parent_path() / "sound")) {  // (the BGM player's sounds; its code names them by their full path)
                if (!copyTree(build.parent_path() / "sound", dir / "sound", error)) return false;
                const fs::path bgm = dir / "TriggerEditor" / "BGMFlexible.eps";
                if (present(bgm)) {
                    std::string text = readAll(bgm);
                    std::string from = u8(build.parent_path() / "sound");
                    std::replace(from.begin(), from.end(), '\\', '/');
                    const std::string to = u8(dir / "sound");
                    std::string toFwd = to;
                    std::replace(toFwd.begin(), toFwd.end(), '\\', '/');
                    for (size_t at; (at = text.find(from)) != std::string::npos;) text.replace(at, from.size(), toFwd);
                    if (!writeText(bgm, text, error)) return false;
                }
                rep.imported.push_back("the BGM player's sounds, in the folder sound");
            }
            for (const auto& [sec, body] : edsSections(readAll(build / "EUDEditor.eds"))) {
                std::string s = sec;
                std::replace(s.begin(), s.end(), '\\', '/');
                if (lower(s).rfind("triggereditor/", 0) == 0 && pathOf(s).extension() == ".eps") pr.addScript(s);
            }
            rep.imported.push_back(count(m.scripts.size(), "script", "scripts") + ", as EUD Editor 3's last build made them (from " + u8(build) + ")");
            any = true;
        } else {
            for (const Script& s : m.scripts) {
                std::string text = s.text;
                if (text.find("msqcvar") != std::string::npos && text.find("PluginVariables") == std::string::npos)
                    text = "import PluginVariables as msqcvar;\n" + text;  // (EUD Editor 3 puts it before each file when it builds)
                if (!writeText(dir / pathOf(s.path), text, error)) return false;
                if (s.main) pr.addScript(s.path);
                if (!s.generated.empty()) rep.notImported.push_back(s.path + ": its " + s.generated + " need EUD Editor 3's build; the file holds the text as it was");
            }
            if (std::any_of(m.scripts.begin(), m.scripts.end(), [](const Script& s) { return s.text.find("msqcvar") != std::string::npos; }))
                if (!writeText(dir / "TriggerEditor" / "PluginVariables.py", kPluginVariables, error)) return false;
            if (m.libraryUsed)
                for (const Ee3LibraryFile& f : kEe3Library)
                    if (!writeText(dir / "TriggerEditor" / "TriggerEditor" / f.name, f.text, error)) return false;
            const size_t n = m.scripts.size();
            rep.imported.push_back(std::to_string(n) + (n == 1 ? " script" : " scripts") + " in the folder TriggerEditor (the main one: " +
                                   (m.scripts.front().main ? m.scripts.front().path : std::string("none")) + ")");
            if (m.libraryUsed) rep.imported.push_back("EUD Editor 3's library TriggerEditor (TETools and the others), which the scripts import");
            for (const Script& s : m.scripts)
                if (!s.from.empty()) rep.imported.push_back(s.path + " from its linked file " + s.from);
            any = true;
        }
    }
    if (m.kind == "e3s" && on("files"))
        for (const Script& s : m.files) {
            if (!writeText(dir / pathOf(s.path), s.text, error)) return false;
            rep.imported.push_back("the file " + s.path + (s.type == 7 ? " (an SC Archive Lua script: not built)" : " (not built)"));
        }
    if (m.kind != "e3s" && on("scripts"))
        for (const Script& s : m.scripts) {
            if (!s.generated.empty()) {
                rep.notImported.push_back("the script " + pathOf(s.path).filename().string() + ": the file is not there");
                continue;
            }
            if (!writeText(dir / pathOf(s.path), s.text, error)) return false;
            pr.addScript(s.path);
            copyModules(pathOf(s.from), (dir / pathOf(s.path)).parent_path(), rep);
            rep.imported.push_back("the script " + pathOf(s.path).filename().string());
            any = true;
        }
    if (m.kind == "e2s" && on("generated") && !build.empty()) {
        // EUD Editor 2 writes the trigger editor's code (TriggerEditor.eps) when it builds, and the modules it imports.
        const fs::path eps = build / "TriggerEditor.eps";
        if (m.e2Triggers && present(eps)) {
            std::error_code ec;
            fs::create_directories(dir / "scripts", ec);
            fs::copy_file(eps, dir / "scripts" / "TriggerEditor.eps", fs::copy_options::overwrite_existing, ec);
            if (ec) return error = "could not copy " + u8(eps) + ": " + ec.message(), false;
            pr.addScript("scripts/TriggerEditor.eps");
            copyModules(eps, dir / "scripts", rep);
            rep.imported.push_back("the trigger editor's code, TriggerEditor.eps (from EUD Editor 2's last build)");
            rep.notImported.push_back("TriggerEditor.eps is EUD Editor 2's code for its own euddraft: a newer euddraft can find errors in it (a name "
                                      "such as Gas is now eudplib's). Build, then correct them in the script");
            any = true;
        }
        if (m.e2PluginsFromBuild)
            for (const auto& [sec, body] : edsSections(readAll(build / "EUDEditor.eds")))
                if (sec == "MurakamiShiinaQC.py" || sec == "grpinjector" || sec == "dataDumper") {
                    std::vector<std::string> keep;
                    for (const std::string& l : body)
                        if (!l.empty() && lower(l).find("\\data\\temp\\") == std::string::npos) keep.push_back(l);  // (not EUD Editor 2's own temporary files)
                    if (sec == "MurakamiShiinaQC.py") {
                        // (a Python plugin of EUD Editor 2's folder: copied in, as a script with its settings)
                        const fs::path py = build / "MurakamiShiinaQC.py";
                        if (present(py)) {
                            std::error_code ec;
                            fs::copy_file(py, dir / "MurakamiShiinaQC.py", fs::copy_options::overwrite_existing, ec);
                        }
                    }
                    if (!keep.empty()) pr.setSectionLines(sec, keep), rep.imported.push_back("the plugin " + sec + " (from EUD Editor 2's last build)");
                }
    }
    if (m.kind == "e2s" && m.e2Triggers && (!on("generated") || build.empty() || !present(build / "TriggerEditor.eps")))
        rep.notImported.push_back("the triggers of EUD Editor 2's trigger editor: build the map once in EUD Editor 2, then import again and select its "
                                  "build folder (Data\\eudplibdata in EUD Editor 2's folder)");
    if (m.kind == "e3s" && !(c.scriptsFromBuild && !build.empty()))
        for (const std::string& g : m.generated)
            if (g.rfind("TriggerEditor/", 0) != 0)  // (the scripts: reported with their files above)
                rep.notImported.push_back(g + ": only EUD Editor 3's build makes its code. Select its build folder to bring it in");
    const bool ownMain = std::any_of(m.scripts.begin(), m.scripts.end(), [](const Script& s) { return lower(s.path) == "scripts/main.eps"; });
    if (any && !ownMain) {
        // The new project's own main.eps goes when the old project brings its scripts (not when one of them is main.eps).
        std::error_code ec;
        pr.removeSection("scripts/main.eps");
        fs::remove(dir / "scripts" / "main.eps", ec);
    }
    return true;
}

bool applyPlugins(const Model& m, const ImportChoices& c, const fs::path& build, EudProject& pr, ImportReport& rep) {
    if (!m.turbo) pr.setPlugin("eudTurbo", false);  // (a new project has EUD Turbo; the old one did not)
    if (c.skip.count("plugins")) return true;
    for (const auto& [name, body] : m.plugins) {
        if (name == "dataDumper" && m.kind != "e2s") {
            rep.notImported.push_back("the plugin dataDumper: its files are the other editor's own; StarDraft writes the texts and requirements itself");
            continue;
        }
        if (name == "main") continue;
        if (name == "eudTurbo") {
            if (!pr.plugin("eudTurbo")) pr.setPlugin("eudTurbo", true);  // (setPlugin with no lines takes a section that is there out)
            rep.imported.push_back("the plugin eudTurbo");
            continue;
        }
        std::vector<std::string> lines = body;
        if (!pr.sectionLines(name).empty()) {  // (a section that two blocks have: one section with both)
            auto had = pr.sectionLines(name);
            had.insert(had.end(), lines.begin(), lines.end());
            lines = had;
        }
        if (lines.empty()) pr.addScript(name);  // (a plugin with no settings: the section alone)
        else pr.setSectionLines(name, lines);
        rep.imported.push_back("the plugin " + name);
    }
    // EUD Editor 3's MSQC and chatEvent of the macros: the build's sections, when the scripts come from the build.
    if (m.kind == "e3s" && c.scriptsFromBuild && !build.empty() && !c.skip.count("generated"))
        for (const auto& [sec, body] : edsSections(readAll(build / "EUDEditor.eds")))
            if (sec == "MSQC" || sec == "chatEvent") {
                std::vector<std::string> keep;
                for (const std::string& l : body)
                    if (!l.empty()) keep.push_back(l);
                auto had = pr.sectionLines(sec);
                for (const std::string& l : had)
                    if (std::find(keep.begin(), keep.end(), l) == keep.end()) keep.push_back(l);
                if (!keep.empty()) pr.setSectionLines(sec, keep), rep.imported.push_back("the " + sec + " lines of EUD Editor 3's last build");
            }
    return true;
}

}  // namespace

const ImportPart* ImportScan::part(const std::string& id) const {
    for (const ImportPart& p : parts)
        if (p.id == id) return &p;
    return nullptr;
}

std::string importKind(const fs::path& file) {
    const std::string ext = lower(file.extension().string());
    if (ext == ".e3s") return "e3s";
    if (ext == ".e2s" || ext == ".e2p") return "e2s";
    if (ext == ".eds" || ext == ".edd") return "eds";
    return "";
}

fs::path findBuildFolder(const fs::path& selected, const ImportScan& scan) { return buildIn(selected, projectStem(scan.file)); }

bool scanImport(const fs::path& file, const GameData& data, ImportScan& out, std::string& error, const fs::path& buildFolder) {
    Model m;
    if (!readModel(file, data, m, error)) return false;
    fs::path build = buildFolder.empty() ? findBuild(m) : buildIn(buildFolder, projectStem(file));
    if (m.kind == "e2s" && !buildFolder.empty() && build.empty() && present(buildFolder / "TriggerEditor.eps")) build = buildFolder;
    out = ImportScan{};
    describe(m, build, buildAgrees(m, build), out);
    return true;
}

ImportChoices defaultChoices(const ImportScan& scan) {
    ImportChoices c;
    const ImportPart* g = scan.part("generated");
    c.scriptsFromBuild = scan.kind == "e3s" && g && !scan.buildFolder.empty() && scan.buildMatches &&
                         std::any_of(g->details.begin(), g->details.end(), [](const std::string& d) { return d.find("(macros)") != std::string::npos ||
                                                                                                             d.find("(a block script)") != std::string::npos ||
                                                                                                             d.find("(classic triggers)") != std::string::npos; });
    return c;
}

bool importProject(const ImportScan& scan, const ImportChoices& c, const fs::path& parent, const std::string& name, const GameData& data,
                   EudProject& pr, ImportReport& rep, std::string& error) {
    Model m;
    if (!readModel(scan.file, data, m, error)) return false;
    const fs::path map = c.map.empty() ? scan.map : c.map;
    if (!present(map)) return error = "the map " + u8(map) + " is not there. Select the map in the first step", false;
    fs::path build = c.buildFolder.empty() ? scan.buildFolder : buildIn(c.buildFolder, projectStem(scan.file));
    if (build.empty() && !c.buildFolder.empty() && present(c.buildFolder / "TriggerEditor.eps")) build = c.buildFolder;
    if (!EudProject::create(parent, name, map, pr, error)) return false;
    rep.imported.push_back("the map " + u8(map.filename()));
    if (c.keepOutput && !scan.output.empty()) {
        pr.setOutputMap(scan.output);
        rep.imported.push_back("the output map " + u8(scan.output));
    }
    if (m.trace) pr.setTraceBuild(true);
    if (!applyData(m, c, data, pr, rep, error)) return false;
    if (!applyScripts(m, c, build, pr, rep, error)) return false;
    if (!applyPlugins(m, c, build, pr, rep)) return false;
    for (const std::string& u : m.unsupported) rep.notImported.push_back(u);
    for (const ImportPart& p : scan.parts)
        if (c.skip.count(p.id)) rep.notImported.push_back("the part \"" + p.id + "\": not selected");
    return pr.save(error) && pr.writeEds(error);
}

bool importProject(const fs::path& file, const fs::path& parent, const std::string& name, const GameData& data, EudProject& out, ImportReport& report,
                   std::string& error) {
    ImportScan scan;
    if (!scanImport(file, data, scan, error)) return false;
    return importProject(scan, defaultChoices(scan), parent, name, data, out, report, error);
}

}  // namespace stardraft
