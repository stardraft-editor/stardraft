// File > Import a project (stardraft/project_import.h): the .NET BinaryFormatter reader (nrbf.h) on streams that the
// test writes, then imports of an EUD Editor 3 project (.e3s, written here with EUD Editor 3's class and member names),
// an EUD Editor 2 project (.e2s) and a euddraft project (.eds) into new projects: each part, the scripts and their
// folders, a script with macros from EUD Editor 3's build folder, and the report. With a samples folder, each .e3s and
// .e2s in it is scanned and imported too (real projects; they are not in the repository).
//
// usage: import_test <install dir> <map.scx> <scratch dir> [samples folder]

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/game_data.h"
#include "stardraft/nrbf.h"
#include "stardraft/project_import.h"

using namespace stardraft;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond, msg)                                     \
    do {                                                     \
        bool ok_ = (cond);                                   \
        std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", msg); \
        std::fflush(stdout);                                 \
        if (!ok_) ++failures;                                \
    } while (0)

// --- A small BinaryFormatter writer: classes with typed members, arrays, strings, references ([MS-NRBF]) --------------

struct N {
    enum T { I32, I64, Bool, U8, Str, Null, Obj, Arr, Bytes, Bools, Ref } t = Null;
    long long i = 0;
    std::string s;  // Str; Obj: the class; Ref: the label
    std::vector<std::pair<std::string, N>> members;  // Obj
    std::vector<N> items;                            // Arr
    std::vector<uint8_t> bytes;                      // Bytes, Bools
    std::string label;                               // Obj: the name a Ref uses
};
N i32(long long v) { N n; n.t = N::I32, n.i = v; return n; }
N i64(long long v) { N n; n.t = N::I64, n.i = v; return n; }
N boolean(bool v) { N n; n.t = N::Bool, n.i = v; return n; }
N str(std::string v) { N n; n.t = N::Str, n.s = std::move(v); return n; }
N null() { return N{}; }
N obj(std::string cls, std::vector<std::pair<std::string, N>> m, std::string label = {}) {
    N n;
    n.t = N::Obj, n.s = "EUD_Editor_3." + cls, n.members = std::move(m), n.label = std::move(label);
    return n;
}
N arr(std::vector<N> items) { N n; n.t = N::Arr, n.items = std::move(items); return n; }
N bytes(std::vector<uint8_t> b) { N n; n.t = N::Bytes, n.bytes = std::move(b); return n; }
N bools(std::vector<uint8_t> b) { N n; n.t = N::Bools, n.bytes = std::move(b); return n; }
N ref(std::string label) { N n; n.t = N::Ref, n.s = std::move(label); return n; }
N list(std::vector<N> items) {
    const int n = int(items.size());
    N l = obj("", {{"_items", arr(std::move(items))}, {"_size", i32(n)}, {"_version", i32(1)}});
    l.s = "System.Collections.Generic.List`1[[X]]";
    return l;
}
N enumOf(long long v) { return obj("SomeEnum", {{"value__", i32(v)}}); }

struct Writer {
    std::string out;
    int next = 2;  // (1 is the root)
    std::map<std::string, int> labels;
    void u8(uint8_t b) { out += char(b); }
    void i32w(int32_t v) {
        for (int k = 0; k < 4; ++k) out += char(uint32_t(v) >> (8 * k));
    }
    void strw(const std::string& s) {
        size_t n = s.size();
        do {
            uint8_t b = n & 0x7F;
            n >>= 7;
            if (n) b |= 0x80;
            u8(b);
        } while (n);
        out += s;
    }
    void prim(const N& n) {
        if (n.t == N::I32) i32w(int32_t(n.i));
        else if (n.t == N::I64) i32w(int32_t(n.i)), i32w(int32_t(n.i >> 32));
        else u8(uint8_t(n.i));
    }
    static bool isPrim(const N& n) { return n.t == N::I32 || n.t == N::I64 || n.t == N::Bool || n.t == N::U8; }
    void record(const N& n, int id = 0) {
        switch (n.t) {
        case N::Null: u8(10); return;
        case N::Ref: u8(9), i32w(labels.at(n.s)); return;
        case N::Str: u8(6), i32w(next++), strw(n.s); return;
        case N::Bytes: case N::Bools:
            u8(15), i32w(next++), i32w(int32_t(n.bytes.size())), u8(n.t == N::Bytes ? 2 : 1);
            for (uint8_t b : n.bytes) u8(b);
            return;
        case N::Arr:
            u8(16), i32w(next++), i32w(int32_t(n.items.size()));
            for (const N& e : n.items) record(e);
            return;
        case N::Obj: {
            const int me = id ? id : next++;
            if (!n.label.empty()) labels[n.label] = me;
            u8(5), i32w(me), strw(n.s), i32w(int32_t(n.members.size()));
            for (const auto& m : n.members) strw(m.first);
            for (const auto& m : n.members) u8(isPrim(m.second) ? 0 : m.second.t == N::Str ? 1 : 2);
            for (const auto& m : n.members)
                if (isPrim(m.second)) u8(m.second.t == N::I32 ? 8 : m.second.t == N::I64 ? 9 : m.second.t == N::Bool ? 1 : 2);
            i32w(2);  // the library
            for (const auto& m : n.members) isPrim(m.second) ? prim(m.second) : record(m.second);
            return;
        }
        default: prim(n);
        }
    }
    std::string stream(const N& root) {
        out.clear();
        u8(0), i32w(1), i32w(-1), i32w(1), i32w(0);
        u8(12), i32w(2), strw("EUD Editor 3, Version=0.19.6.0, Culture=neutral, PublicKeyToken=null");
        record(root, 1);
        u8(11);
        return out;
    }
};

static void writeFile(const fs::path& f, const std::string& text) {
    fs::create_directories(f.parent_path());
    std::ofstream(f, std::ios::binary) << text;
}
static std::string readFile(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
static bool has(const std::vector<std::string>& lines, const std::string& part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

// --- The reader alone ----------------------------------------------------------------------------------------------

static void nrbfTest() {
    Writer w;
    const std::string s = w.stream(obj("SaveableData", {{"mOpenMapName", str("C:\\maps\\a.scx")},
                                                       {"_Addr", i32(5)},
                                                       {"Dat", list({obj("Item", {{"_Data", i64(1LL << 40)}}, "first"), ref("first"), null()})},
                                                       {"Bytes", bytes({1, 2, 3})}}));
    nrbf::Graph g;
    std::string err;
    CHECK(g.read(reinterpret_cast<const uint8_t*>(s.data()), s.size(), err), ("nrbf: a stream reads " + err).c_str());
    const nrbf::Object* root = g.root();
    CHECK(root && root->shortName() == "SaveableData", "nrbf: the root object and its class");
    CHECK(g.text(root, "OpenMapName") == "C:\\maps\\a.scx", "nrbf: a VB field mName by its property's name");
    CHECK(g.number(root, "addr") == 5, "nrbf: a member's name is not case-sensitive (_Addr, _addr)");
    const auto items = g.items(g.get(root, "Dat"));
    CHECK(items.size() == 3 && g.get(items[0]) == g.get(items[1]) && items[2].isNull(), "nrbf: a List`1's items, a reference to an earlier object, a null");
    CHECK(g.number(g.get(items[0]), "Data") == (1LL << 40), "nrbf: an Int64 member");
    const nrbf::Object* b = g.get(root, "Bytes");
    CHECK(b && b->values.size() == 1 && b->values[0].s == std::string("\1\2\3", 3), "nrbf: a byte array");
    // A damaged stream: an error, not a crash (each cut of the stream, and a count larger than the file).
    bool allRefused = true;
    for (size_t cut = 1; cut < s.size(); cut += 3) {
        nrbf::Graph cutG;
        std::string e;
        allRefused &= !cutG.read(reinterpret_cast<const uint8_t*>(s.data()), cut, e) && !e.empty();
    }
    CHECK(allRefused, "nrbf: a stream that ends early is refused with an error");
    std::string big = s;
    big[big.find("Bytes") + 5 + 3 + 1 + 4 + 0] = '\x7F';  // (somewhere in the records: the reader must not crash)
    nrbf::Graph bg;
    std::string be;
    bg.read(reinterpret_cast<const uint8_t*>(big.data()), big.size(), be);
    CHECK(true, "nrbf: a changed byte does not crash the reader");

    // Hostile streams: each is refused, or read in a short time, with no crash.
    auto header = [](Writer& hw) {
        hw.out.clear();
        hw.u8(0), hw.i32w(1), hw.i32w(-1), hw.i32w(1), hw.i32w(0);
    };
    {  // an array of 2^31 - 1 elements, filled by one "that many nulls" record: 31 bytes
        Writer hw;
        header(hw);
        hw.u8(16), hw.i32w(1), hw.i32w(0x7FFFFFFF), hw.u8(14), hw.i32w(0x7FFFFFFF), hw.u8(11);
        nrbf::Graph hg;
        std::string e;
        CHECK(!hg.read(reinterpret_cast<const uint8_t*>(hw.out.data()), hw.out.size(), e) && !e.empty(), "nrbf: a short file that asks for 2^31 values is refused");
    }
    {  // a member of the primitive type Null (no bytes each): refused
        Writer hw;
        header(hw);
        hw.u8(5), hw.i32w(1), hw.strw("X"), hw.i32w(1), hw.strw("a"), hw.u8(0), hw.u8(17), hw.i32w(2), hw.u8(11);
        nrbf::Graph hg;
        std::string e;
        CHECK(!hg.read(reinterpret_cast<const uint8_t*>(hw.out.data()), hw.out.size(), e), "nrbf: a member of the type Null is refused");
    }
    {  // a List`1 whose _items is the list itself: its items are none, not an endless loop
        Writer hw;
        N self = obj("", {{"_items", ref("me")}, {"_size", i32(1)}}, "me");
        self.s = "System.Collections.Generic.List`1[[X]]";
        const std::string st = hw.stream(obj("Root", {{"L", self}}));
        nrbf::Graph hg;
        std::string e;
        const bool ok = hg.read(reinterpret_cast<const uint8_t*>(st.data()), st.size(), e);
        CHECK(ok && hg.items(hg.get(hg.root(), "L")).empty(), "nrbf: a list that holds itself has no items (no endless loop)");
    }
    auto nested = [&](int depth) {  // `depth` classes, each the only member of the one before
        Writer hw;
        N n = obj("Leaf", {{"v", i32(1)}});
        for (int k = 0; k < depth; ++k) n = obj("Node", {{"c", n}});
        const std::string st = hw.stream(n);
        nrbf::Graph hg;
        std::string e;
        return hg.read(reinterpret_cast<const uint8_t*>(st.data()), st.size(), e);
    };
    CHECK(nested(300), "nrbf: 300 nested objects read");
    CHECK(!nested(5000), "nrbf: 5000 nested objects are refused (not a stack overflow)");
}

// --- EUD Editor 3 ------------------------------------------------------------------------------------------------

// A TEFile (EUD Editor 3's trigger editor file or folder).
static N teFile(const std::string& name, int type, const std::string& text, std::vector<N> files = {}, std::vector<N> folders = {}, std::string label = {}) {
    std::vector<std::pair<std::string, N>> m = {{"_FileName", str(name)}, {"_FileType", enumOf(type)}, {"_Files", list(std::move(files))},
                                                 {"_Folders", list(std::move(folders))}};
    if (type != 0 && type != 5) m.push_back({"_Scripter", obj("CUIScriptEditor", {{"_String", str(text)}, {"_ConnectFile", null()}})});
    else m.push_back({"_Scripter", null()});
    return obj("TEFile", std::move(m), std::move(label));
}

static void e3sTest(const GameData& data, const char* map, const fs::path& dir) {
    const DatSchema* units = DatSchema::find("units");
    DatTable stock;
    std::string err;
    if (!units || !stock.load(data, *units, err)) {
        CHECK(false, ("e3s: units.dat loads " + err).c_str());
        return;
    }
    const DatField* shields = stock.field("Shield Amount");
    const uint32_t newShields = stock.stock(*shields, 0) + 10;
    std::vector<N> values;
    for (int e = 0; e < 3; ++e)
        values.push_back(obj("SCDatFiles+CDatFile+CParamater+Value", {{"_Data", i64(e == 0 ? newShields : stock.stock(*shields, e))}, {"_Enabled", boolean(true)},
                                                                       {"_IsDefault", boolean(e != 0)}}));
    N param = obj("SCDatFiles+CDatFile+CParamater", {{"ParamaterName", str("Shield Amount")}, {"Size", i32(2)}, {"VarStart", i32(0)},
                                                     {"Enabled", boolean(true)}, {"Values", list(values)}});
    std::vector<N> tips = {str(""), str("my note")};
    N datFile = obj("SCDatFiles+CDatFile", {{"FIleName", str("units")}, {"Paramaters", list({param})}, {"CodeToolTip", arr(tips)}});
    std::vector<N> statTxt(3, str("NULLSTRING"));
    statTxt[0] = str("<04>Hero <Enter>Marine\\<x\\>");
    std::vector<uint8_t> wire(228), wireD(228, 1);
    for (int u = 0; u < 228; ++u) wire[size_t(u)] = uint8_t(u);
    wire[0] = 7, wireD[0] = 0;
    std::vector<uint8_t> fn(228, 1), fnD(228, 1);
    fn[1] = 0, fnD[1] = 0;  // (Ghost: the first status function, a building's)
    N button = obj("CButtonData", {{"_pos", i32(1)}, {"_icon", i32(228)}, {"_con", i32(0x428E60)}, {"_act", i32(0x423FF0)}, {"_conval", i32(0)},
                                   {"_actval", i32(0)}, {"_enaStr", i32(663)}, {"_disStr", i32(0)}});
    N buttonSets = obj("CButtonSets", {{"ButtonSets", arr({obj("CButtonSet", {{"_DefaultUse", boolean(false)}, {"pButtonSets", list({button})}})})}});
    std::vector<uint8_t> sets(250), setsD(250, 1);
    for (int s = 0; s < 250; ++s) sets[size_t(s)] = uint8_t(s);
    sets[2] = 0, setsD[2] = 0;  // (unit 2 uses set 0)
    N reqObj = obj("CRequireData+RequireObject", {{"_UseStatus", enumOf(2)}, {"ReauireBlocks", list({})}});
    N req = obj("CRequireData", {{"RequireDatas", list({reqObj})}});
    N extra = obj("ExtraDatFiles", {{"_WireFrame", bytes(wire)}, {"_DefaultWireFrame", bools(wireD)}, {"_ButtonSet", bytes(sets)}, {"_DefaultButtonSet", bools(setsD)},
                                    {"RequireDatas", arr({req, null(), null(), null(), null()})}, {"_ButtonData", buttonSets},
                                    {"_statusFn1", bytes(fn)}, {"_statusFn1IsDefault", bools(fnD)}, {"_Stat_txt", arr(statTxt)}});
    // The scripts: main (it imports EUD Editor 3's library and a module), the module in a folder, a macro script, a text file.
    N mainFile = teFile("main", 1, "import lib.util as u;\r\nimport TriggerEditor.TETools as t;\r\nfunction onPluginStart() {\r\n    u.f();\r\n}\r\n", {}, {}, "main");
    N util = teFile("util", 1, "function f() {}\r\n");
    N keys = teFile("keys", 1, "function g() {\r\n    if (@KeyDown(P1, \"A\")) {}\r\n}\r\n");
    N notes = teFile("readme", 8, "plain text");
    N settings = teFile("\xE2\x80\x8B\xE2\x80\x8BSetting", 5, "");
    N bad = teFile("..", 0, "", {teFile("x", 1, "function x() {}")});  // (a folder name that would leave the folder)
    N lib = teFile("lib", 0, "", {util, keys});
    N root = teFile("\xE2\x80\x8BProjectMain", 0, "", {mainFile, notes}, {settings, lib, bad});
    N te = obj("TriggerEditorData", {{"ProjectFile", root}, {"_MainFile", ref("main")}, {"UseMSQC", boolean(true)}, {"UseChatEvent", boolean(true)}});
    N eds = obj("BuildData+EdsBlock", {{"pBlocks", list({obj("BuildData+EdsBlock+EdsBlockItem", {{"BType", enumOf(0)}, {"pTexts", null()}}),
                                                          obj("BuildData+EdsBlock+EdsBlockItem", {{"BType", enumOf(6)}, {"pTexts", str("[unlimiter]\r\n[MSQC]\r\nE : Map Revealer, 1")}})})}});
    N version = obj("System.Version", {{"_Major", i32(0)}, {"_Minor", i32(19)}, {"_Build", i32(6)}, {"_Revision", i32(0)}});
    version.s = "System.Version";
    N saveable = obj("SaveableData", {{"mOpenMapName", str(fs::absolute(map).string())}, {"mSaveMapName", str("C:\\out\\built.scx")},
                                      {"mRelativeOpenMapName", null()}, {"mRelativeSaveMapName", null()}, {"mTempFileLoc", str("2")},
                                      {"mUseCustomTbl", boolean(true)}, {"mAutoBuild", boolean(false)}, {"mLastVersion", version},
                                      {"Dat", obj("SCDatFiles", {{"Datfile", list({datFile})}})}, {"ExtraDat", extra}, {"TEData", te}, {"EdsBlocks", eds}});
    Writer w;
    writeFile(dir / "Old.e3s", w.stream(saveable));

    ImportScan scan;
    CHECK(scanImport(dir / "Old.e3s", data, scan, err), ("e3s: the scan reads the file " + err).c_str());
    CHECK(scan.kind == "e3s" && scan.version == "0.19.6.0" && scan.mapFound, "e3s: the kind, the version and the map");
    const ImportPart* gen = scan.part("generated");
    CHECK(gen && gen->needsBuild && has(gen->details, "lib/keys.eps (macros)"), "e3s: the macro script needs the build, and none is found");
    CHECK(scan.part("dat") && scan.part("dat")->count == 1 && scan.part("strings") && scan.part("buttons") && scan.part("requirements") && scan.part("looks"),
          "e3s: the data parts");
    CHECK(scan.part("labels") && scan.part("labels")->count == 1, "e3s: the user's own note (not the empty ones)");
    const ImportPart* scripts = scan.part("scripts");
    CHECK(scripts && scripts->count == 4 && scripts->details[0] == "TriggerEditor/main.eps", "e3s: the scripts, the main one first");
    CHECK(scripts && !has(scripts->details, "../") && !has(scripts->details, "Setting"), "e3s: no script outside the folder, no settings node");

    EudProject pr;
    ImportReport rep;
    ImportChoices c = defaultChoices(scan);
    c.keepOutput = true;
    CHECK(!c.scriptsFromBuild, "e3s: with no build folder, the scripts come from the file");
    const bool ok = importProject(scan, c, dir, "FromE3s", data, pr, rep, err);
    CHECK(ok, ("e3s: the import " + err).c_str());
    if (!ok) return;
    DatTable t;
    CHECK(t.load(data, *units, err) && t.loadEdits(pr.datFile("units"), err) && t.value(*shields, 0) == newShields && !t.changed(*shields, 1),
          "e3s: the .dat value that is not the default one, and only that one");
    StatTxt s;
    CHECK(s.load(data, err) && s.loadEdits(pr.folder() / "dat" / "strings.json", err) && s.text(0) == "\x04Hero \x0DMarine<x>",
          "e3s: the text, with its hex byte, a key name and \\< \\>");
    ButtonSets b;
    CHECK(b.loadEdits(pr.folder() / "dat" / "buttons.json", err) && b.set(0).size() == 1 && b.set(0)[0].enabledString == 663 && b.unitSet(2) == 0,
          "e3s: the button set and the unit that uses another unit's set");
    Requirements r;
    CHECK(r.loadEdits(pr.folder() / "dat" / "requirements.json", err) && r.list(0, 0).used && r.list(0, 0).ops.empty(), "e3s: an Always requirement");
    UnitLooks l;
    CHECK(l.loadEdits(pr.folder() / "dat" / "unit_display.json", err) && l.unit(0).wireframe == 7 && l.unit(1).status == 4343040,
          "e3s: the wireframe and the status function");
    CHECK(fs::exists(pr.folder() / "TriggerEditor" / "main.eps") && fs::exists(pr.folder() / "TriggerEditor" / "lib" / "util.eps") &&
              fs::exists(pr.folder() / "TriggerEditor" / "readme"),
          "e3s: the scripts in their module folders, and the text file");
    CHECK(fs::exists(pr.folder() / "TriggerEditor" / "TriggerEditor" / "TETools.eps"), "e3s: EUD Editor 3's library, which main imports");
    CHECK(!fs::exists(pr.folder() / "scripts" / "main.eps"), "e3s: no new main.eps beside the old project's own main");
    const auto names = pr.scripts();
    CHECK(std::find(names.begin(), names.end(), "TriggerEditor/main.eps") != names.end() && names.size() == 1, "e3s: only the main script is in the .eds");
    CHECK(pr.plugin("unlimiter") && pr.sectionLines("MSQC") == std::vector<std::string>{"E : Map Revealer, 1"} && !pr.plugin("eudTurbo"),
          "e3s: the user's plugins; no EUD Turbo that the project did not have");
    CHECK(pr.outputMap() == fs::path("C:\\out\\built.scx"), "e3s: the output map is kept when the user selects it");
    CHECK(has(rep.notImported, "lib/keys.eps: its macros need EUD Editor 3's build"), "e3s: the report says the macro script needs the build");
    CHECK(fs::exists(pr.folder() / "notes" / "EUD Editor 3 notes.txt") && readFile(pr.folder() / "notes" / "EUD Editor 3 notes.txt").find("my note") != std::string::npos,
          "e3s: the user's note in the notes file");

    // The build folder (mTempFileLoc 2: BuildData_<name> beside the .e3s): the scripts as the build made them.
    const fs::path build = dir / "BuildData_Old" / "eudplibData";
    writeFile(build / "EUDEditor.eds", "[main]\r\ninput: " + fs::absolute(map).string() + "\r\noutput: x.scx\r\n\r\n[TriggerEditor\\main.eps]\r\n\r\n"
                                       "[MSQC]\r\nKEYDOWN(A) : VKey0, 1\r\n[chatEvent]\r\n__addr__ : 0x58D900\r\n");
    writeFile(build / "TriggerEditor" / "main.eps", "import PluginVariables as msqcvar;\r\nfunction onPluginStart() {}\r\n");
    writeFile(build / "TriggerEditor" / "lib" / "keys.eps", "function g() { if (MemoryEPD(EPD(msqcvar.VKey0) + getcurpl(), Exactly, 1)) {} }\r\n");
    writeFile(build / "TriggerEditor" / "PluginVariables.py", "from eudplib import *\n");
    ImportScan scan2;
    CHECK(scanImport(dir / "Old.e3s", data, scan2, err) && scan2.buildFolder == build && scan2.buildMatches, "e3s: the build folder is found beside the file");
    ImportChoices c2 = defaultChoices(scan2);
    CHECK(c2.scriptsFromBuild && scan2.part("generated") && !scan2.part("generated")->needsBuild, "e3s: with the build, the scripts come from it");
    EudProject pb;
    ImportReport rb;
    const bool ok2 = importProject(scan2, c2, dir, "FromE3sBuild", data, pb, rb, err);
    CHECK(ok2 && readFile(pb.folder() / "TriggerEditor" / "lib" / "keys.eps").find("msqcvar.VKey0") != std::string::npos,
          "e3s: the macro script as EUD Editor 3 built it");
    CHECK(ok2 && has(pb.sectionLines("MSQC"), "KEYDOWN(A) : VKey0, 1") && has(pb.sectionLines("MSQC"), "E : Map Revealer, 1") && pb.plugin("chatEvent"),
          "e3s: the build's MSQC lines with the user's, and its chatEvent");
    CHECK(ok2 && !has(rb.notImported, "need EUD Editor 3's build"), "e3s: nothing is left for the build");

    // Parts that the user did not select.
    ImportChoices c3 = defaultChoices(scan);
    c3.skip = {"dat", "scripts"};
    EudProject ps;
    ImportReport rs;
    CHECK(importProject(scan, c3, dir, "FromE3sSkip", data, ps, rs, err) && !fs::exists(ps.datFile("units")) && !fs::exists(ps.folder() / "TriggerEditor" / "main.eps") &&
              fs::exists(ps.folder() / "scripts" / "main.eps"),
          "e3s: a part that is not selected does not come in (the new project's main.eps stays)");
}

// --- EUD Editor 2 and euddraft --------------------------------------------------------------------------------------

static void e2sTest(const GameData& data, const char* map, const fs::path& dir) {
    writeFile(dir / "Old.e2s",
              "S_ProjectSET\r\nVersion : 0.17.9.7\r\nInputMap : " + fs::absolute(map).string() + "\r\nOutputMap : C:\\o.scx\r\nE_ProjectSET\r\n"
              "S_DatEditSET\r\n0,7,0,10\r\nE_DatEditSET\r\nS_FireGraftSET\r\nE_FireGraftSET\r\nS_BtnSET\r\nBtnUse3 : True\r\nBtnData3 : 1,228,0,0,0,0,663,0,\r\nE_BtnSET\r\n"
              "S_ReqSET\r\nReqUse0,0 : 3\r\nReqCount0,0 : 2\r\nReqData0,0,0 : 65282\r\nReqData0,0,1 : 106\r\nReqUse1,0 : 1\r\nE_ReqSET\r\n"
              "S_FileManagerSET\r\nstattextdicCount : 1\r\nstattextdickey0 : 0\r\nstattextdicvalue0 : <4>Hero Marine\r\nwireuse : True\r\nwireframData0 : 6\r\nE_FileManagerSET\r\n"
              "S_TriggerEditorSET\r\nE_TriggerEditorSET\r\n"
              "S_PluginSET\r\nunlimiter : True\r\neudTurbo : True\r\nextraedssetting : [freeze]##freeze : 0\r\nE_PluginSET\r\n");
    ImportScan scan;
    std::string err;
    CHECK(scanImport(dir / "Old.e2s", data, scan, err) && scan.kind == "e2s" && scan.version == "0.17.9.7", ("e2s: the scan " + err).c_str());
    EudProject pr;
    ImportReport rep;
    const bool ok = importProject(scan, defaultChoices(scan), dir, "FromE2s", data, pr, rep, err);
    CHECK(ok, ("e2s: the import " + err).c_str());
    if (!ok) return;
    const DatSchema* units = DatSchema::find("units");
    DatTable t;
    const DatField& shields = units->fields[7];
    CHECK(t.load(data, *units, err) && t.loadEdits(pr.datFile("units"), err) && shields.name == "Shield Amount" && t.value(shields, 0) == t.stock(shields, 0) + 10,
          "e2s: the .dat value (the game's + the difference)");
    StatTxt s;
    CHECK(s.load(data, err) && s.loadEdits(pr.folder() / "dat" / "strings.json", err) && s.text(0) == "\x04Hero Marine", "e2s: the text, with its color byte");
    ButtonSets b;
    CHECK(b.loadEdits(pr.folder() / "dat" / "buttons.json", err) && b.set(3).size() == 1 && b.set(3)[0].icon == 228, "e2s: the button set");
    Requirements r;
    CHECK(r.loadEdits(pr.folder() / "dat" / "requirements.json", err) && r.list(0, 0).ops.size() == 1 && r.list(0, 0).ops[0].op == 0xFF02 &&
              r.list(0, 0).ops[0].arg == 106 && !r.list(1, 0).used,
          "e2s: the requirements (an opcode with its value, a list not used)");
    UnitLooks l;
    CHECK(l.loadEdits(pr.folder() / "dat" / "unit_display.json", err) && l.unit(0).wireframe == 5, "e2s: the wireframe (its index + 1 in the file)");
    CHECK(pr.plugin("unlimiter") && pr.sectionLines("freeze") == std::vector<std::string>{"freeze : 0"} && pr.plugin("eudTurbo"), "e2s: the plugins (EUD Turbo kept) and the extra .eds text");
    CHECK(!has(rep.notImported, "button sets"), "e2s: the button sets come in now");

    // The trigger editor's code from EUD Editor 2's build folder (TriggerEditor.eps beside the .e2s).
    writeFile(dir / "Trig" / "Old.e2s", readFile(dir / "Old.e2s").replace(readFile(dir / "Old.e2s").find("S_TriggerEditorSET\r\nE_"), 22,
                                                                          "S_TriggerEditorSET\r\nType:0\r\nType:1\r\nType:2\r\nType:3\r\nType:4\r\nType:5\r\nType:6\r\nType:7\r\nE_"));
    writeFile(dir / "Trig" / "TriggerEditor.eps", "import helper;\nfunction onPluginStart() { helper.x(); }\n");
    writeFile(dir / "Trig" / "helper.py", "def x(): pass\n");
    ImportScan st;
    CHECK(scanImport(dir / "Trig" / "Old.e2s", data, st, err) && st.part("generated") && !st.part("generated")->needsBuild, "e2s: the trigger editor's code is found");
    EudProject pt;
    ImportReport rt;
    CHECK(importProject(st, defaultChoices(st), dir, "FromE2sTrig", data, pt, rt, err) && fs::exists(pt.folder() / "scripts" / "TriggerEditor.eps") &&
              fs::exists(pt.folder() / "scripts" / "helper.py"),
          "e2s: TriggerEditor.eps and the module it imports");
}

static void edsTest(const GameData& data, const char* map, const fs::path& dir) {
    writeFile(dir / "code.eps", "function onPluginStart() {}\n");
    writeFile(dir / "game.eds", "[main]\ninput: " + fs::absolute(map).string() + "\noutput: out.scx\n\n[code.eps]\n\n[MSQC]\nE : Map Revealer, 1\n");
    EudProject pe;
    ImportReport re;
    std::string err;
    const bool ok = importProject(dir / "game.eds", dir, "FromEds", data, pe, re, err);
    CHECK(ok && fs::exists(pe.folder() / "scripts" / "code.eps") && pe.sectionLines("MSQC") == std::vector<std::string>{"E : Map Revealer, 1"},
          ("eds: its script and its plugin section " + err).c_str());
}

// A euddraft project whose script is main.eps (the new project's own main.eps must not take its place), and two
// scripts with the same file name in different folders.
static void edsMainTest(const GameData& data, const char* map, const fs::path& dir) {
    writeFile(dir / "main.eps", "function onPluginStart() { /* mine */ }\n");
    writeFile(dir / "a" / "util.eps", "// a\n");
    writeFile(dir / "b" / "util.eps", "// b\n");
    writeFile(dir / "game.eds", "[main]\ninput: " + fs::absolute(map).string() + "\noutput: out.scx\n\n[main.eps]\n[a/util.eps]\n[b/util.eps]\n");
    EudProject pe;
    ImportReport re;
    std::string err;
    const bool ok = importProject(dir / "game.eds", dir, "FromEdsMain", data, pe, re, err);
    CHECK(ok && readFile(pe.folder() / "scripts" / "main.eps").find("mine") != std::string::npos, ("eds: its main.eps is kept " + err).c_str());
    const auto names = pe.scripts();
    CHECK(ok && std::count(names.begin(), names.end(), "scripts/main.eps") == 1, "eds: main.eps is in the .eds once");
    CHECK(ok && readFile(pe.folder() / "scripts" / "util.eps") == "// a\n" && readFile(pe.folder() / "scripts" / "util 2.eps") == "// b\n",
          "eds: two scripts with the same name: both, the second as \"util 2.eps\"");
}

// Real projects (not in the repository): each scans, and imports with the test's map in place of its own.
static void samplesTest(const GameData& data, const char* map, const fs::path& samples, const fs::path& dir) {
    std::error_code ec;
    int n = 0;  // (short names: a deep script folder of a sample can pass the 260 characters of a path that the test program has)
    for (const auto& e : fs::directory_iterator(samples, ec)) {
        const std::string kind = importKind(e.path());
        if (kind != "e3s" && kind != "e2s") continue;
        ImportScan scan;
        std::string err;
        const bool scanned = scanImport(e.path(), data, scan, err);
        std::string parts;
        for (const ImportPart& p : scan.parts) parts += " " + p.id + ":" + std::to_string(p.count);
        std::printf("      %s:%s\n", e.path().filename().string().c_str(), parts.c_str());
        CHECK(scanned, ("samples: " + e.path().filename().string() + " scans " + err).c_str());
        if (!scanned) continue;
        ImportChoices c = defaultChoices(scan);
        c.map = fs::absolute(map);
        EudProject pr;
        ImportReport rep;
        const bool ok = importProject(scan, c, dir, "s" + std::to_string(++n), data, pr, rep, err);
        CHECK(ok, ("samples: " + e.path().filename().string() + " imports " + err).c_str());
        for (const std::string& l : rep.notImported) std::printf("        not imported: %s\n", l.c_str());
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: import_test <install dir> <map.scx> <scratch dir> [samples folder]\n");
        return 2;
    }
    nrbfTest();
    GameData data;
    std::string err;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "could not open the game data: %s\n", err.c_str());
        return 2;
    }
    const fs::path dir = fs::path(argv[3]) / "import_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_empty(dir, ec)) {
        std::fprintf(stderr, "%s is not empty: give the test a new folder\n", dir.string().c_str());
        return 2;
    }
    e3sTest(data, argv[2], dir / "e3s");
    e2sTest(data, argv[2], dir / "e2s");
    edsTest(data, argv[2], dir / "eds");
    edsMainTest(data, argv[2], dir / "edsmain");
    if (argc > 4) samplesTest(data, argv[2], argv[4], dir / "samples");
    std::printf("%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
