// EUD Editor 3's classic triggers to epScript (stardraft/import_codegen.h).
//
// usage: import_ee3_classic_test
//
// No real project uses classic triggers, so the inputs are made here (tools/tests/nrbf_writer.h) with EUD Editor 3's
// class and member names. Each expected text is EUD Editor 3's GetFileText for the same data, traced by hand in
// ClassicTriggerEditor.vb (GetEpsText), Trigger.vb (GetTriggerCodeText), TriggerCodeBlock.vb and ArgValue.vb (GetCodeText);
// the comments say which rule makes which line.

#include <cstdio>
#include <string>
#include <vector>

#include "nrbf_writer.h"
#include "stardraft/import_codegen.h"

using namespace stardraft;

namespace {

int failed = 0;

void check(bool ok, const std::string& what) {
    if (!ok) ++failed, std::printf("FAILED: %s\n", what.c_str());
}

// The lines, each with CRLF (StringBuilder.AppendLine); `last` false: the last line has none.
std::string crlf(const std::vector<std::string>& lines, bool last = true) {
    std::string s;
    for (size_t k = 0; k < lines.size(); ++k) s += lines[k] + (k + 1 < lines.size() || last ? "\r\n" : "");
    return s;
}

void checkText(const std::string& got, const std::string& want, const std::string& what) {
    if (got == want) return;
    ++failed;
    std::printf("FAILED: %s\n", what.c_str());
    size_t at = 0, line = 1;
    while (at < got.size() && at < want.size() && got[at] == want[at]) line += got[at++] == '\n';
    auto lineAt = [](const std::string& s, size_t i) {
        const size_t b = s.rfind('\n', i == 0 ? 0 : i - 1), e = s.find('\n', i);
        return s.substr(b == std::string::npos ? 0 : b + 1, e == std::string::npos ? std::string::npos : e - (b == std::string::npos ? 0 : b + 1));
    };
    std::printf("  line %zu\n  got:  [%s]\n  want: [%s]\n", line, lineAt(got, at).c_str(), lineAt(want, at).c_str());
}

void checkProblems(const std::vector<std::string>& got, const std::vector<std::string>& want, const std::string& what) {
    if (got == want) return;
    ++failed;
    std::printf("FAILED: %s\n  got:\n", what.c_str());
    for (const std::string& p : got) std::printf("    %s\n", p.c_str());
    std::printf("  want:\n");
    for (const std::string& p : want) std::printf("    %s\n", p.c_str());
}

// --- EUD Editor 3's classes ------------------------------------------------------------------------------------------

N efType(int v) { return obj("TriggerFunction+EFType", {{"value__", i32(v)}}); }
enum { Action = 0, Condition = 1, Plib = 2, Lua = 3, UserFunc = 4, ExternFunc = 5 };

// ArgValue: a value set in the editor (IsInit false).
N arg(const std::string& type, N valueString, long long number = 0, bool isNumber = false, N block = null(), N valueString2 = null()) {
    return obj("ArgValue", {{"DefaultType", str(type)},
                            {"ValueType", str(type)},
                            {"ValueString", std::move(valueString)},
                            {"ValueString2", std::move(valueString2)},
                            {"ValueNumber", i64(number)},
                            {"IsArgNumber", boolean(isNumber)},
                            {"IsLangageable", boolean(false)},
                            {"IsQuotation", boolean(false)},
                            {"IsInit", boolean(false)},
                            {"CodeBlock", std::move(block)}});
}
N text(const std::string& type, const std::string& v) { return arg(type, str(v)); }
N number(const std::string& type, long long n, const std::string& label = "") { return arg(type, str(label), n, true); }
N variable(const std::string& name) { return arg("Variable", str(""), 0, false, null(), str(name)); }
N function(N block) { return arg("Function", null(), 0, false, std::move(block)); }
// An argument that was never set: no text (ArgValue.New).
N unset(const std::string& type) {
    N a = arg(type, null());
    for (auto& m : a.members)
        if (m.first == "IsInit") m.second = boolean(true);
    return a;
}

N block(int type, const std::string& name, std::vector<N> args, const std::string& group = "") {
    return obj("TriggerCodeBlock", {{"CName", str("TriggerCodeBlock")}, {"FType", efType(type)}, {"FName", str(name)}, {"FGroup", str(group)}, {"Args", list(std::move(args))}});
}

struct Trig {
    std::vector<uint8_t> players = std::vector<uint8_t>(8, 0);
    std::vector<uint8_t> forces = std::vector<uint8_t>(7, 0);
    bool onlyCode = false, enabled = true, preserved = true, noForces = false;
    N code = null();
    std::string comment;
    std::vector<N> conditions, actions;
};
N trigger(const Trig& t) {
    return obj("Trigger", {{"CName", str("Trigger")},
                           {"CodeText", t.code},
                           {"IsOnlyCode", boolean(t.onlyCode)},
                           {"PlayerEnabled", bools(t.players)},
                           {"ForceEnabled", t.noForces ? null() : bools(t.forces)},
                           {"CommentString", t.comment.empty() ? null() : str(t.comment)},
                           {"IsEnabled", boolean(t.enabled)},
                           {"IsPreserved", boolean(t.preserved)},
                           {"Condition", list(t.conditions)},
                           {"Actions", list(t.actions)}});
}

// ObservableCollection<Trigger> as .NET 4 writes it: Collection`1's items (a List<Trigger>) and _monitor.
N collection(std::vector<N> items) {
    N c = obj("", {{"Collection`1+items", list(std::move(items))}, {"_monitor", null()}});
    c.s = "System.Collections.ObjectModel.ObservableCollection`1[[EUD_Editor_3.Trigger, EUD Editor 3]]";
    return c;
}

N importOf(const std::string& name, const std::string& folder) { return obj("DefineImport", {{"vname", str(name)}, {"vfolder", str(folder)}}); }
N global(const std::string& name, const std::string& type, const std::string& init) {
    return obj("DefineVariable", {{"vname", str(name)}, {"vgroup", str("")}, {"vtype", str(type)}, {"vinit", str(init)}});
}

bool readGraph(const N& root, nrbf::Graph& g) {
    Writer w;
    const std::string s = w.stream(root);
    std::string err;
    if (!g.read(reinterpret_cast<const uint8_t*>(s.data()), s.size(), err)) return std::printf("FAILED: read: %s\n", err.c_str()), ++failed, false;
    return true;
}

// The player part of ClassicTriggerExec (GetEpsText): its header, the triggers' code, its end. `expanded`: the file has
// no other macro, so "<? LuaPlayerVariable = N?>" is as the macro step leaves it (its indent only).
std::vector<std::string> player(int p, const std::vector<std::string>& code, bool expanded = false) {
    const std::string n = std::to_string(p);
    std::vector<std::string> l = {"    /*================Start Player " + std::to_string(p + 1) + "================*/",
                                  "    if (playerexist(" + n + "))", "    {", "        setcurpl(" + n + ");", "        const cp = " + n + ";",
                                  expanded ? "        " : "        <? LuaPlayerVariable = " + n + "?>"};
    l.insert(l.end(), code.begin(), code.end());
    l.push_back("    }");
    l.push_back("    /*================End Player " + std::to_string(p + 1) + "================*/");
    return l;
}

// --- A file that is not the main file: imports, variables, triggers of each kind --------------------------------------

void testTriggers() {
    std::vector<N> triggers;

    // 1: player 1, preserved, one condition, actions with text, a variable, numbers and a location.
    Trig t1;
    t1.players[0] = 1;
    t1.comment = "intro";
    t1.conditions = {block(Condition, "Deaths", {text("TrgPlayer", "CurrentPlayer"), text("TrgComparison", "AtLeast"), text("Number", "1"), number("TrgUnit", 0, "Terran Marine")})};
    t1.actions = {block(Action, "DisplayText", {text("TrgString", "Hello")}),
                  block(Action, "SetVar", {variable("a"), text("TrgModifier", "Add"), text("Number", "1")}),
                  block(Action, "CreateUnit", {text("Number", "1"), number("TrgUnit", 0), number("TrgLocation", 0, "Loc A"), text("TrgPlayer", "CurrentPlayer")})};
    triggers.push_back(trigger(t1));

    // 2: players 1 and 2, not preserved, no conditions; switches and a plib function as an argument.
    Trig t2;
    t2.players[0] = t2.players[1] = 1;
    t2.preserved = false;
    t2.comment = "doors";
    t2.actions = {block(Action, "SetSwitch", {text("TrgSwitch", "Switch 3"), text("TrgSwitchAction", "Set")}),
                  block(Action, "SetSwitch", {text("TrgSwitch", "Door"), text("TrgSwitchAction", "Clear")}),
                  block(Action, "SetSwitch", {text("TrgSwitch", "Gate"), text("TrgSwitchAction", "Toggle")}),
                  block(Action, "SetDeaths", {text("TrgPlayer", "P1"), text("TrgModifier", "SetTo"), function(block(Plib, "dwread_epd", {text("Number", "5")})), number("TrgUnit", 0)})};
    triggers.push_back(trigger(t2));

    // 3: player 2, code only.
    Trig t3;
    t3.players[1] = 1;
    t3.onlyCode = true;
    t3.code = str("    // raw\r\n    SetResources(P1, Add, 1, Ore);");
    triggers.push_back(trigger(t3));

    // 4: player 2: Var and RawCode conditions; an import's function, raw code, an unknown function, a user function and a
    // location that is not in the map.
    Trig t4;
    t4.players[1] = 1;
    t4.conditions = {block(Condition, "Var", {variable("b"), text("TrgComparison", "Exactly"), text("Number", "5")}),
                     block(Condition, "RawCode", {text("RawCode", "b > 2")})};
    t4.actions = {block(ExternFunc, "foo", {text("Number", "1")}, "TT"), block(Action, "RawCode", {text("RawCode", "b = 0")}),
                  block(Action, "NoSuch", {}), block(UserFunc, "myFunc", {}), block(Action, "CenterView", {number("TrgLocation", 7, "Far")})};
    triggers.push_back(trigger(t4));

    // 5: all players, disabled: each player gets its part, with no code.
    Trig t5;
    t5.forces[0] = 1;
    t5.enabled = false;
    t5.actions = {block(Action, "Victory", {})};
    triggers.push_back(trigger(t5));

    // 6: player 1, not preserved, with the Wait macro; an argument that was never set; a Lua text with a backslash.
    Trig t6;
    t6.players[0] = 1;
    t6.preserved = false;
    t6.conditions = {block(Condition, "Always", {})};
    t6.actions = {block(Lua, "Wait", {text("Number", "24")}), block(Action, "DisplayText", {unset("TrgString")}),
                  block(Lua, "echo", {text("TrgString", "a\\b")})};
    triggers.push_back(trigger(t6));

    // 7: Force 1 only: the map's forces are not known here.
    Trig t7;
    t7.forces[1] = 1;
    t7.actions = {block(Action, "Defeat", {})};
    triggers.push_back(trigger(t7));

    N scripter = obj("ClassicTriggerEditor", {{"ScriptEditor+ScriptType", obj("ScriptEditor+SType", {{"value__", i32(0)}})},
                                              {"TriggerListCollection", collection(triggers)},
                                              {"TriggerList", null()},
                                              {"ImportFiles", list({importOf("TT", "TriggerEditor.TETools")})},
                                              {"globalVar", list({global("a", "Default", "NULL"), global("b", "Default", "5"), global("c", "Const", "NULL"),
                                                                  global("pv", "PVariable", "1, 2"), global("arr", "Array", "3"),
                                                                  global("arr2", "Array", "1, 2"), global("va", "VArray", "NULL")})}},
                       "sc");
    // The project: its main file is another file.
    N root = obj("SaveableData", {{"TEData", obj("TriggerEditorData", {{"Scripter", std::move(scripter)}, {"MainFile", obj("TEFile", {{"_Scripter", null()}})}})}});
    nrbf::Graph g;
    if (!readGraph(root, g)) return;
    const nrbf::Object* sc = g.get(g.get(g.root(), "TEData"), "Scripter");

    ImportCodeContext ctx;
    ctx.locationNames = {"Loc A", "Loc B"};
    ctx.switchNames = {"Door", "", ""};
    std::string eps;
    std::vector<std::string> problems;
    check(ee3ClassicTriggerCode(g, sc, "Triggers", ctx, eps, problems), "triggers: made");

    // GetEpsText: the imports, then the variables by their type ("NULL": no value).
    std::vector<std::string> want = {
        "/*================Start Import================*/", "import TriggerEditor.TETools as TT;", "/*================End Import================*/",
        "/*================Start Var================*/", "var a;", "var b = 5;", "const c = 0;", "const pv = PVariable(list(1, 2));",
        "const arr = EUDArray(3);", "const arr2 = [1, 2];", "const va = EUDVArray(10)();", "/*================End Var================*/",
        "function ClassicTriggerExec(){", "    const _origcp = getcurpl();"};

    // Trigger 1 (GetTriggerCodeText, indent 2): "{", the CodeText line (Nothing: an empty line), the condition at indent 4
    // and AppendLine, the actions with ";". TrgString in quotes (LuaCover), SetVar "Add" as "+=", TrgUnit and TrgLocation
    // by number (a location + 1).
    const std::vector<std::string> trig1 = {"        {", "", "            if (", "                Deaths(CurrentPlayer, AtLeast, 1, 0)", "            ){",
                                            "                DisplayText(\"Hello\");", "                a += 1;", "                CreateUnit(1, 0, 1, CurrentPlayer);",
                                            "            }", "        }"};
    // Trigger 2: not preserved: the flag, the "if" with the flag's test only, the flag's set after the actions.
    // dwread_epd as a plib function at indent 0. TrgSwitch in quotes, TrgSwitchAction bare.
    const std::vector<std::string> trig2 = {"        {", "", "            const PreserveFlag = EUDLightBool();", "            if (",
                                            "                PreserveFlag.IsCleared()", "            ){", "                SetSwitch(\"Switch 3\", Set);",
                                            "                SetSwitch(\"Door\", Clear);", "                SetSwitch(\"Gate\", Toggle);",
                                            "                SetDeaths(P1, SetTo, dwread_epd(5), 0);", "                DoActions(PreserveFlag.Set());", "            }",
                                            "        }"};
    // Trigger 3: IsOnlyCode: only the CodeText and its line end.
    const std::vector<std::string> trig3 = {"    // raw", "    SetResources(P1, Add, 1, Ore);"};
    // Trigger 4: the second condition after " && " and a line end; Var "Exactly" as "=="; the import's function with its
    // group. EUD Editor 3 writes "" for NoSuch and stops at myFunc (no list for UserFunc): here a comment each.
    const std::vector<std::string> trig4 = {"        {", "", "            if (", "                b == 5 && ", "                b > 2", "            ){",
                                            "                TT.foo(1);", "                b = 0;", "                /* EUD Editor 3: no code for NoSuch */;",
                                            "                /* EUD Editor 3: no code for myFunc */;", "                CenterView(8);", "            }", "        }"};
    // Trigger 6: a Wait action makes the wait form: the Wait* macros around the condition (no "if (") and the actions.
    // In "<? ?>" the arguments are Lua texts: Number "24" in quotes, the backslash doubled.
    const std::vector<std::string> trig6 = {"        {", "", "            const PreserveFlag = EUDLightBool();", "            <? WaitStart()?>",
                                            "            if (PreserveFlag.IsCleared()){", "            <? WaitConditionStart()?>", "                Always()",
                                            "            <? WaitConditionEnd()?>", "            <? WaitActionStart()?>", "                <?Wait(\"24\")?>;",
                                            "                DisplayText(/* EUD Editor 3: no value */);", "                <?echo(\"a\\\\b\")?>;",
                                            "                DoActions(PreserveFlag.Set());", "            <? WaitActionEnd()?>", "            <? WaitEnd()?>", "            }",
                                            "        }"};
    // The players in order: player 1 has triggers 1, 2, 5 (disabled), 6; player 2 has 2, 3, 4, 5; players 3 to 8 have 5.
    auto add = [&](const std::vector<std::string>& l) { want.insert(want.end(), l.begin(), l.end()); };
    std::vector<std::string> p1 = trig1, p2 = trig2;
    p1.insert(p1.end(), trig2.begin(), trig2.end());
    p1.insert(p1.end(), trig6.begin(), trig6.end());
    p2.insert(p2.end(), trig3.begin(), trig3.end());
    p2.insert(p2.end(), trig4.begin(), trig4.end());
    add(player(0, p1));
    add(player(1, p2));
    for (int p = 2; p < 8; ++p) add(player(p, {}));
    want.push_back("    setcurpl(_origcp);");
    want.push_back("}");
    checkText(eps, crlf(want), "triggers: the text");

    checkProblems(problems,
                  {"trigger 7 runs for Force 1: the map's forces are not known here, so it runs only for the players that it names",
                   "trigger 2 (\"doors\"): the switch \"Gate\" is not in the map",
                   "trigger 6 uses the macro Wait: needs EUD Editor 3's build",
                   "trigger 6: an argument of type TrgString has no value",
                   "trigger 6 uses the macro echo: needs EUD Editor 3's build",
                   "trigger 4: NoSuch is not a function of EUD Editor 3 (Action): EUD Editor 3 writes no code for it",
                   "trigger 4: myFunc is a block of type UserFunc: EUD Editor 3 cannot build it",
                   "trigger 4: location 8 (Far) is not in the map"},
                  "triggers: the problems");
}

// --- The main file, in the older layout (TriggerList, no ForceEnabled) -------------------------------------------------

void testMain() {
    Trig t;
    t.players[7] = 1;
    t.noForces = true;
    t.actions = {block(Action, "Victory", {})};
    N scripter = obj("ClassicTriggerEditor", {{"TriggerList", list({trigger(t)})}, {"ImportFiles", list({})}, {"globalVar", list({})}});
    N root = obj("SaveableData", {{"TEData", obj("TriggerEditorData", {{"MainFile", obj("TEFile", {{"_Scripter", std::move(scripter)}})}})}});
    nrbf::Graph g;
    if (!readGraph(root, g)) return;
    const nrbf::Object* sc = g.get(g.get(g.get(g.root(), "TEData"), "MainFile"), "Scripter");
    std::string eps;
    std::vector<std::string> problems;
    check(ee3ClassicTriggerCode(g, sc, "Main", ImportCodeContext{}, eps, problems), "main: made");
    // No macros: the LuaPlayerVariable line is only its indent. The main file: no _origcp lines (GetEpsText(IsMainFile)), then GetFileText's beforeTriggerExec, with no line end.
    // Preserved and no conditions: no "if", the actions at indent 4.
    std::vector<std::string> want = {"/*================Start Import================*/", "/*================End Import================*/",
                                     "/*================Start Var================*/", "/*================End Var================*/", "function ClassicTriggerExec(){"};
    for (const std::string& l : player(7, {"        {", "", "                Victory();", "        }"}, true)) want.push_back(l);
    want.insert(want.end(), {"}", "function beforeTriggerExec(){", "\tClassicTriggerExec();", "}"});
    checkText(eps, crlf(want, false), "main: the text");
    checkProblems(problems, {}, "main: the problems");
}

// --- Force 1 to 4, human players and computers from the map's players --------------------------------------------------

// A trigger for one of ForceEnabled 1-6 (Force 1 to 4, humans, computers), preserved, with one action.
N forTrigger(int force, const std::string& action) {
    Trig t;
    t.forces[size_t(force)] = 1;
    t.actions = {block(Action, action, {})};
    return trigger(t);
}
std::vector<std::string> oneAction(const std::string& action) { return {"        {", "", "                " + action + "();", "        }"}; }

void testForces() {
    // 1: Force 2; 2: Force 1; 3: Force 1; 4: the human players; 5: the computers.
    N scripter = obj("ClassicTriggerEditor", {{"TriggerListCollection", collection({forTrigger(2, "Victory"), forTrigger(1, "Defeat"), forTrigger(1, "PauseGame"),
                                                                                    forTrigger(5, "UnpauseGame"), forTrigger(6, "Draw")})},
                                              {"ImportFiles", list({})},
                                              {"globalVar", list({})}});
    nrbf::Graph g;
    if (!readGraph(scripter, g)) return;
    ImportCodeContext ctx;
    ctx.playerForces = {0, 0, 1, 1, 2, 2, 3, 3};
    ctx.playerControls = {6, 6, 5, 6, 3, 7, 0, 6};  // (humans: players 1, 2, 4, 8; computers: 3, 5, 6)
    std::string eps;
    std::vector<std::string> problems;
    check(ee3ClassicTriggerCode(g, g.root(), "Forces", ctx, eps, problems), "forces: made");
    // EUD Editor 3 compares each player's force with the trigger's position (0-based) less 1, for each checked force:
    // trigger 1 (position 0): force -1, no player; trigger 2 (1): force 0, players 1 and 2 (as checked); trigger 3 (2):
    // force 1, players 3 and 4 (not Force 1's players). Humans: OWNR 6; computers: OWNR 3, 5, 7.
    std::vector<std::string> want = {"/*================Start Import================*/", "/*================End Import================*/",
                                     "/*================Start Var================*/", "/*================End Var================*/", "function ClassicTriggerExec(){",
                                     "    const _origcp = getcurpl();"};
    auto add = [&](int p, std::vector<std::vector<std::string>> trigs) {
        std::vector<std::string> code;
        for (const auto& t : trigs) code.insert(code.end(), t.begin(), t.end());
        for (const std::string& l : player(p, code, true)) want.push_back(l);
    };
    add(0, {oneAction("Defeat"), oneAction("UnpauseGame")});
    add(1, {oneAction("Defeat"), oneAction("UnpauseGame")});
    add(2, {oneAction("PauseGame"), oneAction("Draw")});
    add(3, {oneAction("PauseGame"), oneAction("UnpauseGame")});
    add(4, {oneAction("Draw")});
    add(5, {oneAction("Draw")});
    add(7, {oneAction("UnpauseGame")});
    want.push_back("    setcurpl(_origcp);");
    want.push_back("}");
    checkText(eps, crlf(want), "forces: the text");
    checkProblems(problems,
                  {"trigger 1 runs for Force 2: EUD Editor 3 gives it to the players of no force (it compares the force with the trigger's position), and so does the import",
                   "trigger 3 runs for Force 1: EUD Editor 3 gives it to the players of Force 2 (it compares the force with the trigger's position), and so does the import"},
                  "forces: the problems");
}

// --- A macro in raw code keeps the LuaPlayerVariable line ----------------------------------------------------------------

void testRawMacro() {
    Trig t;
    t.players[0] = 1;
    t.onlyCode = true;
    t.code = str("@echo(\"x\");");
    N scripter = obj("ClassicTriggerEditor", {{"TriggerListCollection", collection({trigger(t)})}, {"ImportFiles", list({})}, {"globalVar", list({})}});
    nrbf::Graph g;
    if (!readGraph(scripter, g)) return;
    std::string eps;
    std::vector<std::string> problems;
    check(ee3ClassicTriggerCode(g, g.root(), "Raw", ImportCodeContext{}, eps, problems), "raw macro: made");
    check(eps.find("        <? LuaPlayerVariable = 0?>\r\n") != std::string::npos, "raw macro: the LuaPlayerVariable line stays");
    check(eps.find("@echo(\"x\");\r\n") != std::string::npos, "raw macro: the code stays");
}

// --- A block that holds itself: each use writes it again (a hostile file) ----------------------------------------------

void testSharedBlocks() {
    // Block "x": SetDeaths with two Function arguments whose CodeBlock is "x" itself: 2^64 copies without a budget.
    N self = obj("TriggerCodeBlock", {{"CName", str("TriggerCodeBlock")},
                                      {"FType", efType(Action)},
                                      {"FName", str("SetDeaths")},
                                      {"FGroup", str("")},
                                      {"Args", list({function(ref("x")), function(ref("x"))})}},
                 "x");
    Trig t;
    t.players[0] = 1;
    t.actions = {self};
    N scripter = obj("ClassicTriggerEditor", {{"TriggerListCollection", collection({trigger(t)})}, {"ImportFiles", list({})}, {"globalVar", list({})}});
    nrbf::Graph g;
    if (!readGraph(scripter, g)) return;
    std::string eps;
    std::vector<std::string> problems;
    check(!ee3ClassicTriggerCode(g, g.root(), "Shared", ImportCodeContext{}, eps, problems), "shared blocks: not made");
    check(eps.empty(), "shared blocks: no text");
    bool said = false;
    for (const std::string& p : problems) said = said || p == "the classic triggers make more code than an import allows (shared blocks?): not made";
    check(said, "shared blocks: the problem");
}

void testNotClassic() {
    N root = obj("CUIScriptEditor", {{"_String", str("var x;")}});
    nrbf::Graph g;
    if (!readGraph(root, g)) return;
    std::string eps;
    std::vector<std::string> problems;
    check(!ee3ClassicTriggerCode(g, g.root(), "X", ImportCodeContext{}, eps, problems), "not classic: not made");
    check(!ee3ClassicTriggerCode(g, nullptr, "X", ImportCodeContext{}, eps, problems), "no scripter: not made");
}

}  // namespace

int main(int, char**) {
    testTriggers();
    testMain();
    testForces();
    testRawMacro();
    testSharedBlocks();
    testNotClassic();
    std::printf(failed ? "FAILED (%d failed)\n" : "PASSED (%d failed)\n", failed);
    return failed ? 1 : 0;
}
