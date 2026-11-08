// EUD Editor 2's trigger editor to TriggerEditor.eps (stardraft/import_codegen.h).
//
// usage: import_ee2_test [<install dir>] [<samples folder> [<output folder>]]
//
// Without a samples folder: small trees written here (each element type, nested if/while/for/switch, functions,
// classic triggers, raw code, forces, button sets, texts), and the exact lines that EUD Editor 2 writes for them.
// With a samples folder: each .e2s in it (with its other sections, and the game's files when the install dir opens),
// which must give code (the problems are printed; with an output folder, the code is written there as <name>.eps).

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/import_codegen.h"

using namespace stardraft;

namespace {

int failed = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failed;
        std::printf("FAILED: %s\n", what.c_str());
    }
}

const std::string kSep = "\xE0\xAE\x90";

// An element of the saved tree: "Type:<t>,<disabled>,False,<not>", its lines (act:/con: and values), its children.
struct N {
    int type;
    std::vector<std::string> head;
    std::vector<N> kids;
    bool disabled = false, notCon = false;
};

N act(const std::string& name, const std::string& values, bool disabled = false) {
    N n{8, {"act:" + name, values}, {}};
    n.disabled = disabled;
    return n;
}
N con(const std::string& name, const std::string& values, bool notCon = false, bool disabled = false) {
    N n{7, {"con:" + name, values}, {}};
    n.notCon = notCon, n.disabled = disabled;
    return n;
}
N node(int type, std::vector<std::string> head, std::vector<N> kids = {}) { return N{type, std::move(head), std::move(kids)}; }
std::string vals(std::initializer_list<std::string> v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : kSep) + x;
    return s;
}

void write(const N& n, std::vector<std::string>& out) {
    out.push_back("Type:" + std::to_string(n.type) + "," + (n.disabled ? "True" : "False") + ",False," + (n.notCon ? "True" : "False"));
    for (const std::string& h : n.head) {
        if (h.empty()) out.push_back("");  // (an empty value is still a line)
        std::istringstream in(h);           // (a value of several lines)
        for (std::string l; std::getline(in, l);) out.push_back(l);
    }
    out.push_back("ElementsCount:" + std::to_string(n.kids.size()));
    for (const N& k : n.kids) write(k, out);
    out.push_back("END");
}

struct Tree {
    N addText = node(23, {""});
    N functions = node(15, {});
    N globalVar = node(0, {});
    N rawTriggers = node(25, {});
    N start = node(0, {}), before = node(0, {}), after = node(0, {});

    std::vector<std::string> lines() const {
        std::vector<std::string> out;
        const std::pair<const char*, const N*> parts[] = {{"&AddText&", &addText}, {"&functions&", &functions}, {"&GlobalVar&", &globalVar},
                                                          {"&RawTriggers&", &rawTriggers}, {"&onPluginStart&", &start},
                                                          {"&beforeTriggerExec&", &before}, {"&afterTriggerExec&", &after}};
        for (const auto& [key, n] : parts) {
            out.push_back(key);
            write(*n, out);
            out.push_back("");
        }
        return out;
    }
};

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i; (i = text.find("\r\n", start)) != std::string::npos; start = i + 2) out.push_back(text.substr(start, i - start));
    out.push_back(text.substr(start));
    return out;
}

// The lines `want` one after another somewhere in `eps`.
void expectLines(const std::string& eps, const std::vector<std::string>& want, const std::string& what) {
    const auto got = splitLines(eps);
    for (size_t i = 0; i + want.size() <= got.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < want.size() && ok; ++j) ok = got[i + j] == want[j];
        if (ok) return;
    }
    ++failed;
    std::printf("FAILED: %s: these lines are not in the code:\n", what.c_str());
    for (const std::string& l : want) std::printf("    |%s|\n", l.c_str());
}

std::string run(const Tree& t, std::vector<std::string>& problems, std::vector<std::pair<std::string, std::string>>* modulesOut = nullptr) {
    ImportCodeContext ctx;
    std::string eps;
    std::vector<std::pair<std::string, std::string>> modules;
    problems.clear();
    check(ee2TriggerEditorCode(t.lines(), ctx, eps, modules, problems), "the generator takes the tree");
    if (modulesOut) *modulesOut = modules;
    return eps;
}

bool hasProblem(const std::vector<std::string>& problems, const std::string& part) {
    for (const std::string& p : problems)
        if (p.find(part) != std::string::npos) return true;
    return false;
}

void testEmpty() {
    std::vector<std::string> problems;
    std::vector<std::pair<std::string, std::string>> modules;
    const std::string eps = run(Tree{}, problems, &modules);
    std::string want =
        "import punitloop as lp;\r\n"
        "import tempcustomText as tct;\r\n"
        "\r\n"
        "var txtPtr;\r\n"
        "const trgk = $T('Artanis & safhfh');\r\n"
        "\r\n"     // GlobalVar
        "\r\n\r\n"  // AddText
        "\r\n"     // the functions
        "function ClassicTriggerExec() {\r\n"
        "}\r\n"
        "function ClassicTriggerStarter() {\r\n";
    for (int i = 0; i < 8; ++i)
        want += "    //\xED\x94\x8C\xEB\xA0\x88\xEC\x9D\xB4\xEC\x96\xB4 " + std::to_string(i + 1) + "\r\n    if (playerexist(" + std::to_string(i) +
                ")){\r\n        setcurpl(" + std::to_string(i) + ");\r\n    }\r\n";
    want +=
        "}\r\n"
        "function WaitableTriggerExec() {\r\n"
        "}\r\n"
        "function onPluginStart() {\r\n"
        "    randomize();\r\n"
        "\r\n"
        "}\r\n"
        "function beforeTriggerExec() {\r\n"
        "    EUDPlayerLoop()();\r\n"
        "        WaitableTriggerExec();\r\n"
        "        ClassicTriggerExec();\r\n"
        "    EUDEndPlayerLoop();\r\n"
        "    ClassicTriggerStarter();\r\n"
        "}\r\n"
        "function afterTriggerExec() {\r\n"
        "}\r\n";
    check(eps == want, "empty tree: the whole text");
    if (eps != want) std::printf("--- got ---\n%s\n--- want ---\n%s\n", eps.c_str(), want.c_str());
    check(problems.empty(), "empty tree: no problems");
    check(modules.size() == 2 && modules[0].first == "punitloop.py" && modules[1].first == "tempcustomText.py", "modules: punitloop.py and tempcustomText.py");
    check(modules.size() == 2 && modules[0].second.rfind("from eudplib import *", 0) == 0 &&
              modules[1].second.find("def f_setTbl(") != std::string::npos,
          "modules: their texts");
}

void testElements() {
    Tree t;
    t.start.kids = {
        act("SetDeaths", vals({"13", "1", "5", "0"})),
        act("Victory", "None", true),  // (disabled)
        act("Comment", "\"note\""),
        node(1, {}, {node(3, {"And"}, {con("Deaths", vals({"0", "1", "3", "5"})), con("Always", "None", true)}),
                     node(5, {}, {act("SetSwitch", vals({"2", "1"}))})}),
        node(2, {}, {node(3, {"And"}, {con("Deaths", vals({"0", "1", "3", "5"})), con("Never", "None", false, true)}),
                     node(5, {}, {act("Victory", "None")}), node(6, {}, {act("Defeat", "None")})}),
        node(9, {}, {node(12, {"Or"}, {con("Variable", vals({"x", "3", "4"})), con("Variable", vals({"y", "1", "0"}))}),
                     node(13, {}, {act("SetVariable", vals({"x", "1", "1"}))})}),
        node(10, {vals({"Counting", "i", "10"})}, {node(14, {}, {act("RawCode", "foo();")})}),
        node(10, {vals({"Custom", "var j = 0 ; j < 3 ; j++"})}, {node(14, {}, {})}),
        node(10, {vals({"AllUnit", "13"})}, {node(14, {}, {act("Victory", "None")})}),
        node(10, {vals({"AllUnit", "3"})}, {node(14, {}, {})}),
        node(10, {vals({"PlayerLoop", "True,False,True,False,False,False,False,False,False,False,False,False"})},
             {node(14, {}, {act("Victory", "None")})}),
        node(28, {"x"}, {node(29, {vals({"1", "True"})}, {act("Victory", "None")}), node(29, {vals({"-1", "False"})}, {act("Defeat", "None")})}),
        node(21, {vals({"//start", "//end", "False"})}, {node(22, {}, {act("Victory", "None")})}),
        act("RawCode", "a();\nb();"),
        act("DisplayCText", vals({"\"Hi {P1_D_5} {C:x}\"", "1"})),
        act("SetDatfile", vals({"0", "19", "8", "100"})),
        act("SetUpgrade", vals({"13", "5", "0", "2", "0"})),
        act("SetCUnitDataEPD", vals({"epd", "2", "100"})),
        act("SetMemory", vals({"0x58A364", "7", "0"})),
        act("Transmission", vals({"0", "1", "\"sound\\a.wav\"", "1", "2000", "\"hi\"", "1"})),
        act("NoSuchAction", "1"),
        act("BGMPlay", vals({"0", "1", "1"})),
    };
    std::vector<std::string> problems;
    const std::string eps = run(t, problems);
    expectLines(eps, {"function onPluginStart() {", "    randomize();", "    SetDeaths((13), (8), 5, (0));", "    if("}, "action, disabled and Comment");
    expectLines(eps, {"    if(", "        Deaths((0), (1), 3, (5)) && ", "        !Always()", "    ){", "        SetSwitch((2), (5));", "    }"}, "if");
    expectLines(eps, {"    if(", "        Deaths((0), (1), 3, (5))  ", "    ){", "        Victory();", "    }", "    else{", "        Defeat();", "    }"},
                "if else, and the last condition disabled");
    expectLines(eps, {"    while(", "        x > 3 || ", "        y == 1", "    ){", "        x += 1;", "    }"}, "while");
    expectLines(eps, {"    for(var i = 0 ; i < 10 ; i++){", "        foo();", "    }"}, "for counting");
    expectLines(eps, {"    for(var j = 0 ; j < 3 ; j++){", "    }"}, "for custom");
    expectLines(eps, {"    foreach(ptr, epd : lp.EUDLoopUnit2() ) {", "        Victory();", "    }"}, "foreach all units");
    expectLines(eps, {"    foreach(ptr, epd : lp.EUDLoopPUnit(2) ) {", "    }"}, "foreach a player's units");
    expectLines(eps, {"    EUDPlayerLoop()();", "        if (getcurpl() == 0 || getcurpl() == 2) {", "            Victory();", "        }", "    EUDEndPlayerLoop();"},
                "player loop");
    expectLines(eps, {"    EUDSwitch(x);", "        EUDSwitchCase()(1);", "            Victory();", "        EUDBreak();", "        EUDSwitchDefault()();",
                      "            Defeat();", "    EUDEndSwitch();"},
                "switch");
    expectLines(eps, {"    //start", "    Victory();", "    //end"}, "folder");
    expectLines(eps, {"    a();", "    b();"}, "raw code of two lines");
    expectLines(eps, {"    txtPtr = dwread_epd_safe(EPD(0x640B58));", "    tct.print(\"Hi \", dwread_epd(5 * 12 + 0), \" \", x, \"\");",
                      "    SetMemory(0x640B58, SetTo, txtPtr);"},
                "custom text");
    expectLines(eps, {"    dwwrite(0x65FD00 + 9808 + 19 * 4, 100);"}, ".dat write");
    expectLines(eps, {"    bwrite(0x58D2B0 + 0x207C * ((5) / 46) + (getcurpl()) * (46 - 31 * ((5) / 46)) + ((5) % 46), 2);"}, "upgrade");
    expectLines(eps, {"    SetMemoryEPD(epd + 0x008 / 4, SetTo, 100);"}, "CUnit write");
    expectLines(eps, {"    SetMemory(0x58A364, (7), 7);"}, "memory");
    expectLines(eps, {"    Transmission((0), (1), \"sound\\\\a.wav\", (8), 2000, \"hi\", (4));"}, "transmission");
    expectLines(eps, {"    // EUD Editor 2 action NoSuchAction(1): this EUD Editor 2 (0.17.8.2) has no action of that name;", "", "}"},
                "unknown action, and BGM left out");
    check(hasProblem(problems, "NoSuchAction"), "problem: unknown action");
    check(hasProblem(problems, "BGMPlay"), "problem: BGM");
    check(problems.size() == 2, "two problems (" + std::to_string(problems.size()) + ")");
    for (const std::string& p : problems) std::printf("  problem: %s\n", p.c_str());
}

void testFunctions() {
    Tree t;
    t.functions.kids = {
        node(16, {vals({"Foo", "False"})},
             {node(17, {}, {act("CreateVariableWithNoini", vals({"a", "6"})), act("CreateVariableWithNoini", vals({"b", "1"}))}),
              node(18, {}, {act("RawCode", "return a + b;")})}),
        node(16, {vals({"Bar", "True"})},
             {node(17, {}, {act("CreateVariableWithNoini", vals({"p", "19"}))}),
              node(18, {}, {act("Victory", "None"), node(20, {"10"}), act("Defeat", "None")})}),
    };
    t.start.kids = {node(19, {vals({"Foo", "5", "7"})}), node(19, {vals({"Bar", "13"})})};
    t.globalVar.kids = {act("CreateVariable", vals({"x", "0"})), act("CreatePlayerVariable", "y")};
    t.addText = node(23, {"// added\nvar z;"});
    std::vector<std::string> problems;
    const std::string eps = run(t, problems);
    expectLines(eps, {"const trgk = $T('Artanis & safhfh');", "var x = 0;", "const y = [0, 0, 0, 0, 0, 0, 0, 0];", "", "// added", "var z;", "", "function Foo(a, b);"},
                "global variables and added text");
    expectLines(eps, {"function Foo(a, b);", "const Barp = [0, 0, 0, 0, 0, 0, 0, 0];", "const BarTimer = [0, 0, 0, 0, 0, 0, 0, 0];", "function Bar(p);"},
                "function prototypes");
    expectLines(eps, {"function Foo(a ,b) {", "    return a + b;", "}"}, "function");
    expectLines(eps, {"function Bar(p) {", "    if (BarTimer[getcurpl()] > 0) {", "        if (BarTimer[getcurpl()] == 1) {", "            Victory();",
                      "        } else if (BarTimer[getcurpl()] == 11) {", "            Defeat();", "            BarTimer[getcurpl()] = -1;", "        }",
                      "        BarTimer[getcurpl()] += 1;", "    }", "}"},
                "function that waits");
    expectLines(eps, {"function WaitableTriggerExec() {", "    Bar(Barp[getcurpl()]);", "}"}, "waitable functions");
    expectLines(eps, {"    randomize();", "    Foo(4,7);", "    if (BarTimer[getcurpl()] == 0) {", "        BarTimer[getcurpl()] = 1;",
                      "        Barp[getcurpl()] = getcurpl();", "        Bar(Barp[getcurpl()]);", "    }"},
                "function calls");
    check(problems.empty(), "functions: no problems");
}

void testClassic() {
    Tree t;
    t.rawTriggers.kids = {
        node(24, {"5"}, {node(26, {"And"}, {con("Always", "None")}), node(27, {}, {act("Victory", "None"), node(20, {"3"}), act("PreserveTrigger", "None")})}),
        node(24, {"4096"}, {node(26, {"And"}, {}), node(27, {}, {})}),
    };
    t.rawTriggers.kids[1].disabled = true;
    std::vector<std::string> problems;
    const std::string eps = run(t, problems);
    expectLines(eps, {"const ClassicTriggerExecTimer0 = [0, 0, 0, 0, 0, 0, 0, 0];", "function ClassicTriggerExec0() {",
                      "    if (ClassicTriggerExecTimer0[getcurpl()] == 1) {", "        Victory();",
                      "    } else if (ClassicTriggerExecTimer0[getcurpl()] == 4) {", "        PreserveTrigger();",
                      "        ClassicTriggerExecTimer0[getcurpl()] = 0;", "    }", "    if (ClassicTriggerExecTimer0[getcurpl()] > 0) {",
                      "        ClassicTriggerExecTimer0[getcurpl()] += 1;", "    }", "", "}"},
                "classic trigger actions");
    expectLines(eps, {"function ClassicTriggerStarter0() {", "    if(", "        Always()", "    ){", "        if (ClassicTriggerExecTimer0[getcurpl()] == 0){",
                      "            ClassicTriggerExecTimer0[getcurpl()] = 1;", "            ClassicTriggerExec0();", "        }", "    }", "}"},
                "classic trigger conditions");
    expectLines(eps, {"function ClassicTriggerExec() {", "    ClassicTriggerExec0();", "    ClassicTriggerExec1();", "}"}, "classic trigger list");
    expectLines(eps, {"    if (playerexist(0)){", "        setcurpl(0);", "        ClassicTriggerStarter0();", "    }"}, "player 1 runs trigger 1");
    expectLines(eps, {"    if (playerexist(1)){", "        setcurpl(1);", "    }"}, "player 2 runs none (trigger 2 is disabled)");
    expectLines(eps, {"    if (playerexist(2)){", "        setcurpl(2);", "        ClassicTriggerStarter0();", "    }"}, "player 3 runs trigger 1");
    check(problems.empty(), "classic: no problems");
}

// Parts that need data outside the trigger editor's section: a comment, and a problem each.
void testLeftOut() {
    Tree t;
    t.after.kids = {
        act("SetButton", vals({"0:1", "001002003004005006007008009010011012013014015016017018019020"})),
        act("ChangeStarText", vals({"5", "1", "\"x\""})),
        node(10, {vals({"PlayerLoop", "False,False,False,False,False,False,False,False,True,False,False,False"})}, {node(14, {}, {})}),
        act("SCDB:SaveData", "None"),
        act("SetDatfile", vals({"0", "19", "8", "DValue"})),
    };
    std::vector<std::string> problems;
    const std::string eps = run(t, problems);
    expectLines(eps, {"    // EUD Editor 2 action SetButton(0:1, 001002003004005006007008009010011012013014015016017018019020): it needs the project's "
                      "button set (the buttons' order), which is not read here;"},
                "button set");
    expectLines(eps, {"    EUDPlayerLoop()();", "        if () {", "        }", "    EUDEndPlayerLoop();"}, "player loop of a force");
    expectLines(eps, {"    dwwrite(0x65FD00 + 9808 + 19 * 4, DValue);"}, "a value not set");
    check(hasProblem(problems, "SetButton"), "problem: SetButton");
    check(hasProblem(problems, "ChangeStarText"), "problem: ChangeStarText");
    check(hasProblem(problems, "forces"), "problem: forces");
    check(hasProblem(problems, "SCDB"), "problem: SCDB");
    check(hasProblem(problems, "DValue is not set"), "problem: value not set");
    check(problems.size() == 5, "five problems (" + std::to_string(problems.size()) + ")");
}

// A stat_txt.tbl of a few texts, as the game's files.
struct FakeFiles : FileSource {
    Bytes tbl;
    explicit FakeFiles(const std::vector<std::string>& texts) {
        tbl.resize(2 + 2 * texts.size());
        tbl[0] = uint8_t(texts.size()), tbl[1] = uint8_t(texts.size() >> 8);
        for (size_t i = 0; i < texts.size(); ++i) {
            const size_t at = tbl.size();
            tbl[2 + 2 * i] = uint8_t(at), tbl[3 + 2 * i] = uint8_t(at >> 8);
            tbl.insert(tbl.end(), texts[i].begin(), texts[i].end());
            tbl.push_back(0);
        }
    }
    bool readFile(const std::string& name, Bytes& out, std::string& error) const override {
        if (name != "rez\\stat_txt.tbl") return error = "no " + name, false;
        out = tbl;
        return true;
    }
    bool hasFile(const std::string& name, uint64_t* size) const override {
        if (size) *size = tbl.size();
        return name == "rez\\stat_txt.tbl";
    }
    std::vector<std::string> listFiles() const override { return {"rez\\stat_txt.tbl"}; }
};

// With the map's forces, the project's other sections and the game's stat_txt.tbl: forces, button sets and texts.
void testWithData() {
    Tree t;
    t.after.kids = {
        node(10, {vals({"PlayerLoop", "False,False,False,False,False,False,False,False,False,True,False,False"})}, {node(14, {}, {})}),
        act("SetButton", vals({"0:0", "001002003004005006007008009010011012013014015016017018019020"})),
        act("ChangeStarText", vals({"5", "2", "\"ab\""})),
        act("ChangeButtonEnableMsg", vals({"0:1", "1", "\"x\""})),
        act("ChangeStarText", vals({"21", "1", "\"k\""})),
        act("ChangeStarText", vals({"2", "1", "\"g\""})),
    };
    t.rawTriggers.kids = {node(24, {"512"}, {node(26, {"And"}, {}), node(27, {}, {})})};
    const std::map<std::string, std::vector<std::string>> sections = {
        {"BtnSET", {"BtnUse0 : True", "BtnData0 : 5,0,0,0,0,0,10,11,1,0,0,0,0,0,12,13,"}},
        {"FileManagerSET", {"statlang : 0", "stattextdicCount : 3", "stattextdickey0 : 4", "stattextdicvalue0 : Cost: ### and ##",
                            "stattextdickey1 : 11", "stattextdicvalue1 : <1><2>Buy ####", "stattextdickey2 : 20",
                            "stattextdicvalue2 : \xEA\xB0\x80 ##"}},
    };
    GameData data;
    data.open(std::make_unique<FakeFiles>(std::vector<std::string>{"Marine", "Gh##ost"}));
    ImportCodeContext ctx;
    ctx.data = &data;
    ctx.playerForces = {0, 0, 1, 1, 1, 0, 0, 0};
    ctx.playerControls = {6, 6, 6, 5, 0, 6, 6, 6};  // (player 5's slot is not used)
    ctx.ee2Sections = &sections;
    std::string eps;
    std::vector<std::pair<std::string, std::string>> modules;
    std::vector<std::string> problems;
    check(ee2TriggerEditorCode(t.lines(), ctx, eps, modules, problems), "with data: takes the tree");
    expectLines(eps, {"    EUDPlayerLoop()();", "        if (getcurpl() == 2 || getcurpl() == 3) {", "        }", "    EUDEndPlayerLoop();"},
                "player loop of force 2");
    expectLines(eps, {"    if (playerexist(2)){", "        setcurpl(2);", "        ClassicTriggerStarter0();", "    }"}, "force 2: player 3");
    expectLines(eps, {"    if (playerexist(3)){", "        setcurpl(3);", "        ClassicTriggerStarter0();", "    }"}, "force 2: player 4");
    expectLines(eps, {"    if (playerexist(4)){", "        setcurpl(4);", "    }"}, "force 2: not player 5 (slot not used)");
    expectLines(eps, {"    SetMemoryEPD(epdread_epd(EPD(0x5187EC) + 0) + 5 * 1, SetTo, 67305985);",
                      "    SetMemoryEPD(epdread_epd(EPD(0x5187EC) + 0) + 5 * 1 + 1, SetTo, 134678021);"},
                "button set: the button's place by position");
    expectLines(eps, {"    tct.setTbl((5), 14, 2, \"\\x0D\\x0D\");", "    tct.setTbl((5), 14, 2, \"ab\");"}, "stat_txt: the project's text");
    expectLines(eps, {"    tct.setTbl(12, 6, 4, \"\\x0D\\x0D\\x0D\\x0D\");", "    tct.setTbl(12, 6, 4, \"x\");"}, "button text: the project's button");
    expectLines(eps, {"    tct.setTbl((21), 4, 2, \"\\x0D\\x0D\");"}, "stat_txt: UTF-8 bytes");
    expectLines(eps, {"    tct.setTbl((2), 2, 2, \"\\x0D\\x0D\");", "    tct.setTbl((2), 2, 2, \"g\");"}, "stat_txt: the game's text");
    check(problems.size() == 1 && hasProblem(problems, "other than ASCII"), "with data: one problem (" + std::to_string(problems.size()) + ")");
    for (const std::string& p : problems) std::printf("  problem: %s\n", p.c_str());
}

// Files that EUD Editor 2 does not write (an .e2s is untrusted): limits, and false when not all of it is made.
void testBadInput() {
    ImportCodeContext ctx;
    std::string eps;
    std::vector<std::pair<std::string, std::string>> modules;
    std::vector<std::string> problems;

    // Elements nested 30000 deep: left out (no stack overflow), and false.
    {
        std::vector<std::string> lines = Tree{}.lines();
        const auto at = std::find(lines.begin(), lines.end(), "&onPluginStart&") - lines.begin();
        std::vector<std::string> deep;
        const int n = 30000;
        for (int i = 0; i < n; ++i) deep.push_back("Type:0,False,False,False"), deep.push_back("ElementsCount:1");
        deep.back() = "ElementsCount:0";
        for (int i = 0; i < n; ++i) deep.push_back("END");
        lines.erase(lines.begin() + at + 1, lines.begin() + at + 4);
        lines.insert(lines.begin() + at + 1, deep.begin(), deep.end());
        problems.clear();
        check(!ee2TriggerEditorCode(lines, ctx, eps, modules, problems), "too deep: false");
        check(hasProblem(problems, "deeper than 256"), "too deep: problem");
        expectLines(eps, {"function onPluginStart() {", "    randomize();", "", "}"}, "too deep: the part is left out");
    }

    // A function definition with no elements (no arguments, no code).
    {
        Tree t;
        t.functions.kids = {node(16, {vals({"Foo", "True"})})};
        problems.clear();
        const std::string out = run(t, problems);
        expectLines(out, {"const FooTimer = [0, 0, 0, 0, 0, 0, 0, 0];", "function Foo();"}, "function with no elements");
    }

    // A value where EUD Editor 2 stops its build: the text up to there, a comment, and false.
    {
        Tree t;
        t.start.kids = {node(10, {vals({"AllUnit", "x"})}, {node(14, {}, {})})};
        problems.clear();
        check(!ee2TriggerEditorCode(t.lines(), ctx, eps, modules, problems), "build stops: false");
        expectLines(eps, {"function onPluginStart() {", "    randomize();", "", "// EUD Editor 2 stops its build here: a value that it cannot read", ""},
                    "build stops: the text so far");
        check(hasProblem(problems, "stops its build"), "build stops: problem");
    }

    // A custom text code with a number too long for a number: a comment and a problem.
    {
        Tree t;
        t.start.kids = {act("DisplayCText", vals({"\"{P1_D_99999999999999999999}\"", "0"}))};
        problems.clear();
        const std::string out = run(t, problems);
        check(out.find("// EUD Editor 2 action DisplayCText(") != std::string::npos, "long number: a comment");
        check(hasProblem(problems, "does not fit"), "long number: problem");
    }

    // A value that holds its own place: it grows each pass (the size capped), or stays (the passes capped).
    {
        Tree t;
        t.start.kids = {act("SetVariable", vals({"$Variable$" + std::string(20000, 'a') + "$Variable$", "1", "0"})), act("SetVariable", vals({"$Variable$", "1", "0"}))};
        problems.clear();
        const std::string out = run(t, problems);
        check(out.size() < 100000, "own place: the code stays small");
        check(hasProblem(problems, "grows without end"), "own place: problem (grows)");
        check(hasProblem(problems, "does not stop"), "own place: problem (stays)");
    }

    // A text edit count far above the section's lines: no long loop.
    {
        const std::map<std::string, std::vector<std::string>> sections = {
            {"FileManagerSET", {"stattextdicCount : 2000000000", "stattextdickey0 : 4", "stattextdicvalue0 : ##"}}};
        Tree t;
        t.start.kids = {act("ChangeStarText", vals({"5", "1", "\"ab\""}))};
        ImportCodeContext c;
        c.ee2Sections = &sections;
        problems.clear();
        check(ee2TriggerEditorCode(t.lines(), c, eps, modules, problems), "huge count: true");
        expectLines(eps, {"    tct.setTbl((5), 0, 2, \"\\x0D\\x0D\");", "    tct.setTbl((5), 0, 2, \"ab\");"}, "huge count: the edit is read");
    }
}

void testNotATree() {
    ImportCodeContext ctx;
    std::string eps;
    std::vector<std::pair<std::string, std::string>> modules;
    std::vector<std::string> problems;
    check(!ee2TriggerEditorCode({"Version : 1", "hello"}, ctx, eps, modules, problems), "not a tree: false");
}

// The lines of S_TriggerEditorSET in an .e2s, trimmed (as the importer gives them).
std::vector<std::string> e2sTriggerLines(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<std::string> out;
    bool inside = false;
    for (std::string l; std::getline(in, l);) {
        const size_t a = l.find_first_not_of(" \t\r\n"), b = l.find_last_not_of(" \t\r\n");
        l = a == std::string::npos ? "" : l.substr(a, b - a + 1);
        if (l == "S_TriggerEditorSET") inside = true;
        else if (l == "E_TriggerEditorSET") break;
        else if (inside) out.push_back(l);
    }
    return out;
}

// All the sections of an .e2s (S_<name> to E_<name>), trimmed lines.
std::map<std::string, std::vector<std::string>> e2sSections(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::map<std::string, std::vector<std::string>> out;
    std::string current;
    for (std::string l; std::getline(in, l);) {
        const size_t a = l.find_first_not_of(" \t\r\n"), b = l.find_last_not_of(" \t\r\n");
        l = a == std::string::npos ? "" : l.substr(a, b - a + 1);
        if (current.empty() && l.rfind("S_", 0) == 0) current = l.substr(2), out[current];
        else if (!current.empty() && l == "E_" + current) current.clear();
        else if (!current.empty()) out[current].push_back(l);
    }
    return out;
}

void testSamples(const std::filesystem::path& folder, const std::filesystem::path& out, const GameData* data) {
    std::vector<std::filesystem::path> files;
    for (const auto& f : std::filesystem::directory_iterator(folder))
        if (f.path().extension() == ".e2s") files.push_back(f.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        const auto lines = e2sTriggerLines(f);
        const auto sections = e2sSections(f);
        ImportCodeContext ctx;
        ctx.data = data;
        ctx.ee2Sections = &sections;
        std::string eps;
        std::vector<std::pair<std::string, std::string>> modules;
        std::vector<std::string> problems;
        const bool ok = ee2TriggerEditorCode(lines, ctx, eps, modules, problems);
        const std::string name = f.filename().string();
        check(ok, name + ": takes the tree");
        check(!eps.empty(), name + ": makes code");
        std::printf("%s: %zu lines in, %zu lines of code, %zu problems\n", name.c_str(), lines.size(), splitLines(eps).size(), problems.size());
        for (const std::string& p : problems) std::printf("    %s\n", p.c_str());
        if (!out.empty()) std::ofstream(out / (f.stem().string() + ".eps"), std::ios::binary) << eps;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path samples, out;
    if (argc > 3) out = argv[3];
    if (argc > 2) samples = argv[2];
    else if (argc > 1) {
        for (const auto& f : std::filesystem::directory_iterator(argv[1]))
            if (f.path().extension() == ".e2s") samples = argv[1];
    }
    testEmpty();
    testElements();
    testFunctions();
    testClassic();
    testLeftOut();
    testWithData();
    testBadInput();
    testNotATree();
    GameData data;
    std::string error;
    const bool game = argc > 2 && data.open(argv[1], error);
    if (!samples.empty()) testSamples(samples, out, game ? &data : nullptr);
    if (failed) std::printf("FAILED (%d failed)\n", failed);
    else std::printf("PASSED (0 failed)\n");
    return failed ? 1 : 0;
}
