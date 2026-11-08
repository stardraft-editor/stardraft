// EUD Editor 3's block scripts to epScript and Python (stardraft/import_codegen.h).
//
// No real project has block scripts, so each test writes a GUIScriptEditor with nrbf_writer.h, reads it with
// nrbf::Graph and checks the exact text. Each expected text is traced by hand through EUD Editor 3's
// GUIScriptEditor.GetFileText and GUIScriptManager.GetItemScriptText (Class/TriggerEditor/GUIScriptEditor/), line by
// line: AppendLine is "\r\n", GetIntend is tabs, and the spliter goes after each item but the last.
//
// usage: import_ee3_blocks_test [<install dir>] [<samples folder>]

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "stardraft/import_codegen.h"
#include "stardraft/nrbf.h"

using namespace stardraft;

static int failures = 0;
#define CHECK(cond, msg)                                     \
    do {                                                     \
        bool ok_ = (cond);                                   \
        std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", msg); \
        std::fflush(stdout);                                 \
        if (!ok_) ++failures;                                \
    } while (0)

#include "nrbf_writer.h"

// ScriptBlock.EBlockType.
enum T {
    none = -1, constVal, action, condition, plibfun, macrofun, funuse, externfun, fundefine, funargs, funargblock,
    funcontent, if_, ifcondition, ifthen, elseif_, ifelse, for_, foraction, while_, whilecondition, whileaction,
    switch_, switchvar, switchcase, vardefine, varuse, externvaruse, folder, folderaction, objectdefine, objectfields,
    objectmethod, rawcode, import_, or_, and_, exp_, sign, funreturn, break_,
};

// A ScriptBlock with the fields that EUD Editor 3 serializes. isfolder as the ScriptBlock constructor sets it.
struct B {
    int type;
    std::string name, value;
    std::vector<B> child;
    bool flag = false;
    const char* value2 = nullptr;  // nullptr: Nothing
    std::vector<B> vchild;         // 0 or 1
};
static N node(const B& b) {
    static const std::vector<int> folders = {ifcondition, ifthen, ifelse, funargs, whilecondition, whileaction, switchcase, or_, and_,
                                             folderaction, funcontent, foraction, objectfields, objectmethod};
    const bool isfolder = std::find(folders.begin(), folders.end(), b.type) != folders.end();
    std::vector<N> kids;
    for (const B& c : b.child) kids.push_back(node(c));
    return obj("ScriptBlock", {{"ScriptType", enumOf(b.type)},
                               {"isfolder", boolean(isfolder)},
                               {"IsDeleteAble", boolean(true)},
                               {"name", str(b.name)},
                               {"isexpand", boolean(false)},
                               {"flag", boolean(b.flag)},
                               {"flag2", boolean(false)},
                               {"value", str(b.value)},
                               {"value2", b.value2 ? str(b.value2) : null()},
                               {"VChild", b.vchild.empty() ? null() : node(b.vchild[0])},
                               {"child", list(std::move(kids))},
                               {"Parent", null()}});
}
static B val(const std::string& type, const std::string& v) { return {constVal, type, v}; }
static B num(const std::string& v) { return val("Number", v); }
static B var(const std::string& name) { return {varuse, name, "", {}, false, "value"}; }
static B call(int type, const std::string& name, std::vector<B> args) { return {type, name, "", std::move(args)}; }
static B expr(std::vector<B> parts) { return {exp_, "expression", "", std::move(parts)}; }
static B op(const std::string& s) { return {sign, "sign", s}; }
// A function: fundefine > funargs (value: the tooltip; children: funargblock, name = type, value = name, value2 =
// the parameter's tooltip) and funcontent.
static B function(const std::string& name, const std::string& tooltip, std::vector<B> params, std::vector<B> body) {
    return {fundefine, "function", name, {B{funargs, "funargs", tooltip, std::move(params)}, B{funcontent, "funcontent", "", std::move(body)}}};
}
static B param(const std::string& type, const std::string& name, const char* tip) { return {funargblock, type, name, {}, false, tip}; }

struct Result {
    bool ok = false;
    std::string code;
    std::vector<std::string> problems;
};
static Result makeNodes(std::vector<N> nodes, bool python = false, const ImportCodeContext& ctx = {}) {
    Writer w;
    const std::string s = w.stream(obj("GUIScriptEditor", {{"items", list(std::move(nodes))},
                                                          {"_ConnectFile", null()},
                                                          {"_ConnectRelativeFile", null()},
                                                          {"ScriptEditor+ScriptType", enumOf(python ? 1 : 0)}}));
    nrbf::Graph g;
    std::string err;
    Result r;
    if (!g.read(reinterpret_cast<const uint8_t*>(s.data()), s.size(), err)) {
        r.problems.push_back("stream: " + err);
        return r;
    }
    r.ok = ee3BlockScriptCode(g, g.root(), "main", python, ctx, r.code, r.problems);
    return r;
}
static Result make(std::vector<B> items, bool python = false, const ImportCodeContext& ctx = {}) {
    std::vector<N> nodes;
    for (const B& b : items) nodes.push_back(node(b));
    return makeNodes(std::move(nodes), python, ctx);
}
static bool has(const std::vector<std::string>& lines, const std::string& part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
}
static std::string lines(std::initializer_list<const char*> ls) {
    std::string s;
    for (const char* l : ls) s += l, s += "\r\n";
    return s;
}
static void same(const Result& r, const std::string& want, const char* what) {
    const bool ok = r.ok && r.code == want;
    CHECK(ok, what);
    if (!ok) {
        std::printf("--- want:\n%s\n--- got:\n%s\n---\n", want.c_str(), r.code.c_str());
        for (const std::string& p : r.problems) std::printf("  problem: %s\n", p.c_str());
    }
}

static const char* const kHead = "import PluginVariables as msqcvar;\r\n";

// Imports, variables, and a function with parameters, a doc comment and a return.
static void functionsTest() {
    const Result r = make({
        {import_, "import", "TriggerEditor.lib as lib"},
        {vardefine, "vardefine", "count", {num("5")}, false, "var"},
        {vardefine, "vardefine", "limit", {num("10")}, true, "const"},
        {vardefine, "vardefine", "kept", {}, true, "static"},
        function("addUnits", "Adds [n] units.", {param("Number", "n", "how many"), param("TrgUnit", "unit", "the unit")},
                 {call(action, "CreateUnit", {num("n"), val("TrgUnit", "Terran Marine"), val("TrgLocation", "Anywhere"), val("TrgPlayer", "P1")}),
                  {funreturn, "funreturn", "", {num("1")}}}),
    });
    // GetFileText: the msqcvar import; "PREDEF": "function addUnits(" + the parameters with ", " (Number is in
    // SCValueNoneType: the name alone; TrgUnit is not: "unit:TrgUnit") + ");". Then the blocks: an import, a vardefine
    // ("var"/"const"/"static", " = " + the values with ","), the doc comment, "function addUnits" + funargs +
    // funcontent ("{", the action at intend 1 with ";" as its parent is a folder, the return, "}").
    same(r,
         std::string(kHead) + lines({"function addUnits(n, unit:TrgUnit);",
                                     "import TriggerEditor.lib as lib;",
                                     "var count = 5;",
                                     "const limit = 10;",
                                     "static kept;",
                                     "/***",
                                     " * @Type",
                                     " * F",
                                     " * @Summary.ko-KR",
                                     " * Adds [n] units.",
                                     " * ",
                                     " * @param.n.ko-KR",
                                     " * how many",
                                     " * @param.unit.ko-KR",
                                     " * the unit",
                                     "***/",
                                     "function addUnits(n, unit:TrgUnit){",
                                     "\tCreateUnit(n, \"Terran Marine\", \"Anywhere\", P1);",
                                     "\treturn 1;",
                                     "}"}),
         "blocks: imports, variables, a function with parameters and a return");
    CHECK(r.problems.empty(), "blocks: no problems for plain blocks");
}

// if/else, for loops of each kind, switch/case, while, nested and/or, a folder, a macro.
static void controlTest() {
    std::vector<B> body = {
        {if_, "if", "", {
            {ifcondition, "ifcondition", "", {call(condition, "Deaths", {val("TrgPlayer", "CurrentPlayer"), val("TrgComparison", "AtLeast"), num("1"),
                                                                           val("TrgUnit", "Terran Marine")}),
                                              call(condition, "Always", {})}},
            {ifthen, "ifthen", "", {call(action, "DisplayText", {val("TrgString", "hi"), num("4")})}},
            {ifelse, "ifelse", "", {{rawcode, "rawcode", "// nothing"}}}}},
        {for_, "for", "CountRepeat\xE1\x9A\xA2i\xE1\x97\xA2" "0\xE1\x97\xA2" "5", {{foraction, "foraction", "", {expr({var("total"), op("+="), var("i")})}}}},
        {switch_, "switch", "", {
            {switchcase, "switchcase", "1", {call(action, "PlayWAV", {val("WAVName", "sound\\a.wav")})}, true},
            {switchcase, "switchcase", "2", {}, false}}, false, nullptr, {var("count")}},
        {for_, "foreach", "EUDLoopPlayer\xE1\x9A\xA2p\xE1\x97\xA2Human\xE1\x97\xA2None\xE1\x97\xA2None\xE1\x97\xA2True",
         {{foraction, "foraction", "", {{break_, "break", ""}}}}},
        {for_, "foreach", "EUDPlayerLoop\xE1\x9A\xA2", {{foraction, "foraction", "", {call(action, "SetResources", {val("TrgPlayer", "CurrentPlayer"),
                                                                                                                    val("TrgModifier", "Add"), num("100"), val("TrgResource", "Ore")})}}}},
        {for_, "foreach", "EUDLoopUnit2\xE1\x9A\xA2ptr, epd", {{foraction, "foraction", "", {}}}},
        {for_, "for", "UserEdit\xE1\x9A\xA2" "For\xE1\x97\xA2var k = 0 ; k < 3 ; k++", {{foraction, "foraction", "", {}}}},
        {for_, "foreach", "Timeline\xE1\x9A\xA2t\xE1\x97\xA2" "10", {{foraction, "foraction", "", {}}}},
        {if_, "if", "", {
            {ifcondition, "ifcondition", "", {
                {or_, "or", "", {call(condition, "Switch", {val("TrgSwitch", "Switch 1"), val("TrgSwitchState", "Set")}), call(condition, "Always", {})}},
                {and_, "and", "", {call(condition, "Never", {}), expr({var("count"), op("=="), num("3")})}}}},
            {ifthen, "ifthen", "", {{macrofun, "SetBGM", "", {num("1"), var("count")}}}}}},
        {folder, "folder", "{\xE1\x97\x8B}", {{folderaction, "folderaction", "", {call(action, "PreserveTrigger", {})}}}},
    };
    const Result r = make({function("onPluginStart", "", {}, std::move(body))});
    // Traced through GetItemScriptText (intend 1 in the function):
    //  - if: "if" + ifcondition ("(", each condition at intend 2: " && " after each but the last, a line end as the
    //    parent is a folder; ")") + ifthen ("{" ... "}") + ifelse ("else{" ... "}", a raw code gets no indent) + a line end.
    //  - CountRepeat i, 0, 5: "for(var i = 0; i < 05; ...)": VB joins "0" and "5" as text, CInt("05") = 5.
    //  - switch: "switch(" + VChild + "){", the cases at intend 2, "break;" when flag is set.
    //  - EUDLoopPlayer with True at the end: setcurpl(p) first. EUDPlayerLoop: no "{", ends with EUDEndPlayerLoop().
    //  - or/and: "(" and ")" on their own lines; after ")" the separator of the list they are in (the if condition
    //    list: isAnd = True, " && "); the expression in "and" gets "(...)" as it has more than one part.
    //  - the macro: its arguments with isValCover: a value that is not a constVal in quotes.
    //  - the folder: its head and tail (split at ᗋ) around the blocks.
    const std::string want = std::string(kHead) +
                             lines({"function onPluginStart();",
                                    "/***",
                                    " * @Type",
                                    " * F",
                                    " * @Summary.ko-KR",
                                    " * ",
                                    " * ",
                                    "***/",
                                    "function onPluginStart(){",
                                    "\tif(",
                                    "\t\tDeaths(CurrentPlayer, AtLeast, 1, \"Terran Marine\") && ",
                                    "\t\tAlways()",
                                    "\t){",
                                    "\t\tDisplayText(\"hi\", 4);",
                                    "\t}else{",
                                    "// nothing",
                                    "\t}",
                                    "\tfor(var i = 0; i < 5; i++){",
                                    "\t\ttotal += i;",
                                    "\t}",
                                    "\tswitch(count){",
                                    "\t\tcase 1:",
                                    "\t\t\tPlayWAV(\"sound\\a.wav\");",
                                    "\t\t\tbreak;",
                                    "\t\tcase 2:",
                                    "\t}",
                                    "\tforeach(p : EUDLoopPlayer(\"Human\",None,None)){",
                                    "\t\tsetcurpl(p);",
                                    "\t\tbreak;",
                                    "\t}",
                                    "\tEUDPlayerLoop()();",
                                    "\t\tSetResources(CurrentPlayer, Add, 100, Ore);",
                                    "\tEUDEndPlayerLoop();",
                                    "\tforeach(ptr, epd : EUDLoopUnit2()){",
                                    "\t}",
                                    "\tfor(var k = 0 ; k < 3 ; k++){",
                                    "\t}",
                                    "\tforeach(t : Timeline(10)){",
                                    "\t}",
                                    "\tif(",
                                    "\t\t(",
                                    "\t\t\tSwitch(\"Switch 1\", Set) || ",
                                    "\t\t\tAlways()",
                                    "\t\t) && ",
                                    "\t\t(",
                                    "\t\t\tNever() && ",
                                    "\t\t\t(count == 3)",
                                    "\t\t)",
                                    "\t){",
                                    "\t\t<?SetBGM(1, \"count\")?>;",
                                    "\t}",
                                    "\t{",
                                    "\t\tPreserveTrigger();",
                                    "\t}",
                                    "}"});
    same(r, want, "blocks: if/else, loops, switch, and/or, a macro, a folder");
    CHECK(r.problems.size() == 1 && has(r.problems, "uses the macro SetBGM: needs EUD Editor 3's build"), "blocks: the macro is in the problems");
}

// A while loop: EUD Editor 3 fails on it; the text is as for an if condition, with a problem.
static void whileTest() {
    const Result r = make({function("f", "", {}, {{while_, "while", "", {
        {whilecondition, "whilecondition", "", {expr({var("count"), op(">"), num("0")})}},
        {whileaction, "whileaction", "", {expr({var("count"), op("-="), num("1")})}}}}})});
    // "while" + "(" + the expression at intend 2 (one item: no parentheses) + ")" + whileaction ("{" ... "}").
    same(r,
         std::string(kHead) + lines({"function f();", "/***", " * @Type", " * F", " * @Summary.ko-KR", " * ", " * ", "***/", "function f(){",
                                     "\twhile(", "\t\tcount > 0", "\t){", "\t\tcount -= 1;", "\t}", "}"}),
         "blocks: a while loop");
    CHECK(has(r.problems, "a while block"), "blocks: the while loop is in the problems");
}

// Objects with fields and methods, constructors and the other variable uses, and a raw code at the top.
static void objectsTest() {
    const Result r = make({
        {objectdefine, "objectdefine", "Point", {
            {objectfields, "objectfields", "", {{vardefine, "vardefine", "x", {}, false, "var"}, {vardefine, "vardefine", "y", {}, false, "var"}}},
            {objectmethod, "objectmethod", "", {function("len", "", {}, {{funreturn, "funreturn", "", {expr({
                {varuse, "this", "x", {}, false, "fields"}, op("+"), {varuse, "this", "y", {}, false, "fields"}})}}})}}}},
        {vardefine, "vardefine", "arr", {{varuse, "EUDArray", "constructor", {num("1"), num("2")}, false, nullptr}}, true, "const"},
        {vardefine, "vardefine", "one", {{varuse, "EUDArray", "constructor", {num("4")}, false, nullptr}}, true, "const"},
        {vardefine, "vardefine", "va", {{varuse, "EUDVArray", "constructor", {num("1"), num("2")}, true, nullptr}}, true, "const"},
        {vardefine, "vardefine", "vb", {{varuse, "EUDVArray", "constructor", {num("1"), num("2")}, false, nullptr}}, true, "const"},
        {vardefine, "vardefine", "pv", {{varuse, "PVariable", "constructor", {}, false, nullptr}}, false, "var"},
        {vardefine, "vardefine", "p", {{varuse, "Point", "constructor", {}, false, nullptr}}, false, "var"},
        {vardefine, "vardefine", "q", {{varuse, "Point", "cast", {num("ptr")}, false, nullptr}}, false, "var"},
        {vardefine, "vardefine", "r", {{varuse, "p", "len", {}, false, "method"}}, false, "var"},
        {vardefine, "vardefine", "s", {{varuse, "arr", "!index", {num("0")}, false, nullptr}}, false, "var"},
        {vardefine, "vardefine", "t", {{varuse, "pv", "!cp", {}, false, nullptr}}, false, "var"},
        {rawcode, "rawcode", "// end"},
    });
    // object: "object Point{", the fields and the method at intend 1 (the method with its doc comment, no PREDEF:
    // only top functions get one), "};". The return's expression: parent funreturn is not a folder, so no indent and
    // no ";". The varuse constructors: EUDArray with 2 values and flag False: "EUDArray(List(1, 2))", with 1 value
    // "EUDArray(4)"; EUDVArray with flag True: "VArray(1, 2)", False: "EUDVArray(2)(List(1, 2))"; PVariable without
    // values: "PVariable()". The raw code at the top has no parent: no line end.
    same(r,
         std::string(kHead) + lines({"object Point{", "\tvar x;", "\tvar y;", "\t/***", "\t * @Type", "\t * F", "\t * @Summary.ko-KR", "\t * ", "\t * ",
                                     "\t***/", "\tfunction len(){", "\t\treturn this.x + this.y;", "\t}", "};", "const arr = EUDArray(List(1, 2));",
                                     "const one = EUDArray(4);", "const va = VArray(1, 2);", "const vb = EUDVArray(2)(List(1, 2));", "var pv = PVariable();",
                                     "var p = Point();", "var q = Point.cast(ptr);", "var r = p.len();", "var s = arr[0];", "var t = pv[getcurpl()];"}) +
             "// end",
         "blocks: objects, constructors and variable uses");
    CHECK(r.problems.empty(), "blocks: no problems for objects");
}

// Values: locations by index (from the map), properties, BGM, quoted types, unset values; Var and SetVar.
static void valuesTest() {
    ImportCodeContext ctx;
    ctx.locationNames = {"", "Base"};
    const auto body = [] {
        return std::vector<B>{
            call(action, "A", {val("TrgLocationIndex", "Base"), val("TrgLocationIndex", "Location 1"), val("TrgLocationIndex", "Nowhere")}),
            call(action, "B", {val("TrgProperty", "hp"), val("BGM", "song"), val("Key", "F1"), val("Key", "\"F2\""), val("TrgString", ""),
                               val("TrgUnit", "defaultvalue;Unit")}),
            call(action, "SetVar", {num("count"), val("TrgModifier", "SetTo"), num("7")}),
            call(action, "SetVar", {num("count"), val("TrgModifier", "Add"), num("2")}),
        };
    };
    const Result r = make({function("g", "", {}, body())}, false, ctx);
    // TrgLocationIndex: the index in the list of labels (0: "Location 1", the map has no name; 1: "Base"); not there: 0.
    // SetVar: the three values written with "▦" and split; SetTo is "==", Add "+=".
    const std::string head = std::string(kHead) + lines({"function g();", "/***", " * @Type", " * F", " * @Summary.ko-KR", " * ", " * ", "***/", "function g(){"});
    same(r,
         head + lines({"\tA(1, 0, 0);", "\tB(UnitProperty(hp), <?GetBGMIndex(\"song\")?>, \"F1\", \"F2\", \"\", \"defaultvalue;Unit\");", "\tcount==7;",
                       "\tcount+=2;", "}"}),
         "blocks: values from the map and of each type");
    CHECK(has(r.problems, "\"Nowhere\" is not in the map"), "blocks: an unknown location is in the problems");
    CHECK(has(r.problems, "uses the macro GetBGMIndex"), "blocks: the BGM macro is in the problems");
    CHECK(has(r.problems, "a value is not set (Unit)"), "blocks: an unset value is in the problems");
    CHECK(has(r.problems, "count==7"), "blocks: SetVar's SetTo is in the problems");
    // Without the map's names: "Location n".
    const Result r2 = make({function("g", "", {}, {call(action, "A", {val("TrgLocationIndex", "Location 3")})})});
    same(r2, head + lines({"\tA(2);", "}"}), "blocks: locations without the map");
}

// Where EUD Editor 3 fails or writes other numbers: comments and problems.
static void problemsTest() {
    const Result r = make({function("h", "", {}, {
        {for_, "for", "UserEdit\xE1\x9A\xA2" "For", {{foraction, "foraction", "", {}}}},
        {for_, "for", "CountRepeat\xE1\x9A\xA2i\xE1\x97\xA2" "1\xE1\x97\xA2" "5", {{foraction, "foraction", "", {}}}},
        {for_, "for", "defaultvalue", {{foraction, "foraction", "", {}}}},
    })});
    // UserEdit without "ᗢ": v(1) is out of range (VB throws). CountRepeat 1, 5: "1" + "5" = "15". The default value:
    // no loop kind, an empty head.
    const std::string head = std::string(kHead) + lines({"function h();", "/***", " * @Type", " * F", " * @Summary.ko-KR", " * ", " * ", "***/", "function h(){"});
    same(r,
         head + lines({"\t/* EUD Editor 3 cannot write this: a loop block with the value \"UserEdit\xE1\x9A\xA2" "For\" (too few parts) */{", "\t}",
                       "\tfor(var i = 1; i < 15; i++){", "\t}", "\t{", "\t}", "}"}),
         "blocks: loops that EUD Editor 3 cannot write or writes wrong");
    CHECK(has(r.problems, "too few parts"), "blocks: the bad loop is in the problems");
    CHECK(has(r.problems, "ends at 15, not at 6"), "blocks: the repeat count is in the problems");
    CHECK(has(r.problems, "has no loop set"), "blocks: the unset loop is in the problems");

    const Result py = make({function("f", "", {}, {})}, true);
    CHECK(!py.ok && py.code.empty() && has(py.problems, "no Python code maker"), "blocks: a Python block script gives false");

    nrbf::Graph g;
    Writer w;
    const std::string s = w.stream(obj("CUIScriptEditor", {{"_String", str("x")}}));
    std::string err, code;
    std::vector<std::string> problems;
    CHECK(g.read(reinterpret_cast<const uint8_t*>(s.data()), s.size(), err) && !ee3BlockScriptCode(g, g.root(), "x", false, {}, code, problems),
          "blocks: a text script is not a block script");
}

// A hostile file: each block has the same block twice as its children (the second a reference to the first), so n
// levels are 2^n blocks. Loading stops at 200,000 blocks; the text stops at 16 MB. Both give false, fast.
static N sharedTree(int level, int levels, const std::string& name) {
    N n = node({action, name, ""});
    if (level < levels) {
        N first = sharedTree(level + 1, levels, name);
        first.label = "L" + std::to_string(level + 1);
        for (auto& m : n.members)
            if (m.first == "child") m.second = list({first, ref(first.label)});
    }
    return n;
}
static void sharedTest() {
    const Result r = makeNodes({sharedTree(0, 40, "A")});
    CHECK(!r.ok && has(r.problems, "more than 200000 blocks"), "blocks: a tree of 2^40 shared blocks stops at the block limit");
    // 2^17 - 1 = 131071 blocks (under the block limit) with names of 200 bytes: about 26 MB of text.
    const Result t = makeNodes({sharedTree(0, 16, std::string(200, 'x'))});
    CHECK(!t.ok && has(t.problems, "longer than 16 MB"), "blocks: shared blocks that make too much text stop at the text limit");
}

int main(int, char**) {
    functionsTest();
    controlTest();
    whileTest();
    objectsTest();
    valuesTest();
    problemsTest();
    sharedTest();
    std::printf("%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
