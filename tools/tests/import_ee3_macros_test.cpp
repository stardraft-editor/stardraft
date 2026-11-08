// EUD Editor 3's Lua macros (stardraft/import_ee3_extras.h): the "@Name(...)" and "<? ... ?>" expansion, the MSQC and
// chatEvent lines and PluginVariables.py, the main file's additions, and the sandbox's limits.
//
// The inputs are made here (tools/tests/nrbf_writer.h) with EUD Editor 3's class and member names. With a folder as the
// argument, the test also expands the scripts of each .e3s in it (read only) and prints what it finds; with a second folder,
// it writes there each script that has macros, before and after.
//
// Usage: import_ee3_macros_test [samples folder [output folder]]

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "nrbf_writer.h"
#include "stardraft/import_ee3_extras.h"

namespace fs = std::filesystem;
using namespace stardraft;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

void checkEq(const std::string& got, const std::string& want, const std::string& what) {
    if (got != want) {
        ++failures;
        std::printf("FAIL: %s\n  got:  [%s]\n  want: [%s]\n", what.c_str(), got.c_str(), want.c_str());
    }
}

bool hasProblem(const std::vector<std::string>& problems, const std::string& part) {
    for (const std::string& p : problems)
        if (p.find(part) != std::string::npos) return true;
    return false;
}

std::string joined(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& s : v) out += s + "\n";
    return out;
}

// A project graph: its root SaveableData with TEData (a BGM list) and Dat (two units.dat fields).
struct Project {
    nrbf::Graph g;
    const nrbf::Object* te = nullptr;
};

void makeProject(Project& p, bool useMsqc = true) {
    std::vector<N> bgms;
    for (const char* name : {"intro", "boss"}) bgms.push_back(obj("BGMData+BGMFile", {{"_BGMName", str(name)}}));
    N te = obj("TriggerEditorData", {{"UseMSQC", boolean(useMsqc)},
                                     {"UseChatEvent", boolean(true)},
                                     {"MouseLocation", null()},
                                     {"_addr", i32(0)},
                                     {"_patternAddr", i32(0x58D910)},
                                     {"_BGMData", obj("BGMData", {{"BGMList", list(bgms)}})}});
    auto param = [](const char* name, int size, int start) {
        return obj("SCDatFiles+CDatFile+CParamater",
                   {{"ParamaterName", str(name)}, {"Size", i32(size)}, {"VarStart", i32(start)}, {"VarArray", i32(1)}});
    };
    N units = obj("SCDatFiles+CDatFile", {{"FIleName", str("units")},
                                          {"Paramaters", list({param("Hit Points", 4, 0), param("Shield Amount", 2, 0),
                                                               param("Subunit 2", 2, 0), param("Infestation", 2, 106)})}});
    N dat = obj("SCDatFiles", {{"Datfile", list({units})}});
    N root = obj("SaveableData", {{"Dat", dat}, {"TEData", te}});
    Writer w;
    const std::string s = w.stream(root);
    std::string error;
    check(p.g.read(reinterpret_cast<const uint8_t*>(s.data()), s.size(), error), "the test graph reads: " + error);
    p.te = p.g.get(p.g.root(), "TEData");
}

// Expands one text; the problems in `problems`.
std::string expand(Ee3Macros& m, const std::string& text, bool isMain, std::vector<std::string>& problems, bool* ok = nullptr) {
    std::string out;
    problems.clear();
    const bool r = m.apply(text, isMain, "test", out, problems);
    if (ok) *ok = r;
    return out;
}

std::string expandOk(Ee3Macros& m, const std::string& text, bool isMain = false) {
    std::vector<std::string> problems;
    bool ok = false;
    const std::string out = expand(m, text, isMain, problems, &ok);
    check(ok && problems.empty(), "expands with no problem: " + text + "\n" + joined(problems));
    return out;
}

void testMacros() {
    Project p;
    makeProject(p);
    ImportCodeContext ctx;
    Ee3Macros m(p.g, p.te, ctx);

    // The short form and its quoting: names in quotes, a quoted text as it is, "@" for code.
    checkEq(expandOk(m, "if (@KeyDown(P1, \"A\")) {}"), "if (MemoryEPD(EPD(msqcvar.VKeyDown_A) + 0, Exactly, 1)) {}", "KeyDown");
    checkEq(expandOk(m, "@KeyPress(CurrentPlayer, `)"), "MemoryEPD(EPD(msqcvar.VKeyPress_SP13) + getcurpl(), Exactly, 1)", "KeyPress with a special key");
    checkEq(expandOk(m, "@MouseDown(P2, L);"), "MemoryEPD(EPD(msqcvar.VMouseDown_L) + 1, Exactly, 1);", "MouseDown");
    checkEq(expandOk(m, "@ChatEvent(P1, \"hello\")"), "MemoryEPD(EPD(msqcvar.VChatIndex) + 0, Exactly, 1)", "ChatEvent");
    checkEq(expandOk(m, "@ChatEvent(P3, hello)"), "MemoryEPD(EPD(msqcvar.VChatIndex) + 2, Exactly, 1)", "the same chat again");
    checkEq(expandOk(m, "@ChatEventPattern(P1, a, b, c)"), "MemoryEPD(EPD(msqcvar.VChatIndex) + 0, Exactly, 2)", "ChatEventPattern");
    checkEq(expandOk(m, "@MouseClickLine(P1, 2, 10, 20)"), "MemoryEPD(EPD(msqcvar.VMouseClickIndex0) + 0, Exactly, 1)", "MouseClickLine");
    checkEq(expandOk(m, "<? echo(1 + 2) ?>-<?echo(\"x\")?>"), "3-x", "<? ... ?> blocks");
    checkEq(expandOk(m, "@SetVariable(v, SetTo, @1 + 2)"), "v = 3", "an @ argument is Lua code, not quoted");
    checkEq(expandOk(m, "@PlayerID(P1, \"My (name)\")"), "IsPName(0, \"My (name)\")", "a bracket in a quoted text");
    checkEq(expandOk(m, "a@b @x"), "a@b @x", "no brackets: no macro");
    {
        std::vector<std::string> problems;
        bool ok = true;
        const std::string out = expand(m, "<??>?>", false, problems, &ok);
        check(!ok && out == "<??>?>" && problems.size() == 1, "a block needs a character between <? and ?>: \"<??>?>\" is the Lua \"?>\"");
    }

    // The Dat macros: the project's field sizes and EUD Editor 3's addresses.
    checkEq(expandOk(m, "@GetUnitsDat(Hit_Points, \"Terran Marine\")"), "dwread(0x662350 + 0 * 4)", "GetUnitsDat");
    checkEq(expandOk(m, "@SetUnitsDat(Shield_Amount, 1, 100, SetTo)"), "SetMemoryXEPD(EPD(0x660E00 + 1 * 2), 7, 6553600, 0xFFFF0000)",
            "SetUnitsDat at a constant index");
    checkEq(expandOk(m, "@SetUnitsDat(Shield_Amount, i, 5, Add)"), "wwrite(0x660E00 + i * 2 ,wread(0x660E00 + i * 2) + 5)",
            "SetUnitsDat at a variable index");
    checkEq(expandOk(m, "@UnitsDat(Infestation, 107, 3, Exactly)"), "MemoryXEPD(EPD(0x664980 + 107 - 106 * 2), 10, 196608, 0xFFFF0000)",
            "UnitsDat (a field that starts at 106)");
    {
        std::vector<std::string> problems;
        bool ok = true;
        expand(m, "@GetUnitsDat(No_Such_Field, 0)", false, problems, &ok);
        check(!ok && hasProblem(problems, "no field"), "a .dat field that the project does not have: " + joined(problems));
    }

    // GetBGMIndex: the index in the project's BGM list (0 when it is not there).
    checkEq(expandOk(m, "<? GetBGMIndex(\"boss\") ?>,<? GetBGMIndex(\"none\") ?>"), "1,0", "GetBGMIndex");
    checkEq(expandOk(m, "@SetBGM(boss);"), "import TriggerEditor.BGMPlayerWrapper as bg;\r\nbg.SetBGM(1);", "SetBGM's preDefine");

    // LuaPlayerVariable: EUD Editor 3 sets it again for each file.
    checkEq(expandOk(m, "<? LuaPlayerVariable = \"cp\" ?><? echo(ParsePlayer(CurrentPlayer)) ?>"), "cp", "LuaPlayerVariable in the file");
    checkEq(expandOk(m, "<? echo(ParsePlayer(CurrentPlayer)) ?>"), "getcurpl()", "LuaPlayerVariable in the next file");

    // A Lua error: the block stays as written, the others are made.
    {
        std::vector<std::string> problems;
        bool ok = true;
        const std::string out = expand(m, "a\r\n<? error(\"boom\") ?> <? echo(\"b\") ?>", false, problems, &ok);
        check(!ok, "a Lua error gives false");
        checkEq(out, "a\r\n<? error(\"boom\") ?> b", "a Lua error leaves its block");
        check(problems.size() == 1 && hasProblem(problems, "boom") && hasProblem(problems, "line 2"), "a Lua error's problem: " + joined(problems));
    }

    // The main file: the preDefine lines (this file's, then the build's mainPreDefine), and the three functions.
    {
        Project q;
        makeProject(q);
        Ee3Macros mm(q.g, q.te, ctx);
        const std::string a = expandOk(mm, "x<? preDefine(\"const a = 1;\") mainPreDefine(\"import X;\") onPluginText(\"init();\") "
                                           "beforeText(\"b();\") afterText(\"c();\") ?>y");
        checkEq(a, "const a = 1;\r\nxy", "preDefine in a file");
        const std::string main = expandOk(mm, "function onPluginStart() {\r\n}\r\n<? preDefine(\"import Y;\") mainPreDefine(\"import Y;\") ?>", true);
        checkEq(main,
                "import X;\r\nimport Y;\r\nfunction onPluginStart() {\r\n\tinit();\r\n\r\n}\r\n\r\n"
                "function beforeTriggerExec(){\r\n\tb();\r\n\r\n}\r\n\r\nfunction afterTriggerExec(){\r\n\tc();\r\n\r\n}\r\n",
                "the main file's additions");
    }

    // The .eds lines and PluginVariables.py.
    checkEq(joined(m.msqcLines()),
            "NotTyping ; KeyDown(A) : VKeyDown_A, 1\n"
            "NotTyping ; KeyPress(`) : VKeyPress_SP13, 1\n"
            "MouseDown(L) : VMouseDown_L, 1\n"
            "0x58D900, Exactly, 2 : VChatIndex, 1\n"
            "0x58D910, Exactly, 3 : VChatIndex, 2\n"
            "MouseUp(L) ; Memory(0x6CDDC8, AtLeast, 126) ; Memory(0x6CDDC8, AtMost, 141) ; Memory(0x6CDDC4, AtLeast, 10) ; "
            "Memory(0x6CDDC4, AtMost, 20) : VMouseClickIndex0, 1\n",
            "the MSQC lines");
    checkEq(joined(m.chatEventLines()),
            "__addr__ : 0x58D900\n__ptrAddr__ : 0x58D904\n__patternAddr__ : 0x58D910\n__lenAddr__ : 0x58D90C\nhello : 2\n^a.*b.*c$ : 3\n",
            "the chatEvent lines");
    checkEq(m.pluginVariables(),
            "from eudplib import *\r\n\r\n\r\nVChatIndex = EUDArray(8)\r\nVMouseClickIndex0 = EUDArray(8)\r\nVKeyDown_A = EUDArray(8)\r\n"
            "VKeyPress_SP13 = EUDArray(8)\r\nVMouseDown_L = EUDArray(8)\r\n\r\ndef Reg():\r\n    print('... TERegVar ...')\r\n"
            "    EUDRegisterObjectToNamespace(\"VKeyDown_A\", VKeyDown_A)\r\n"
            "    EUDRegisterObjectToNamespace(\"VKeyPress_SP13\", VKeyPress_SP13)\r\n"
            "    EUDRegisterObjectToNamespace(\"VMouseDown_L\", VMouseDown_L)\r\n"
            "    EUDRegisterObjectToNamespace(\"VChatIndex\", VChatIndex)\r\n"
            "    EUDRegisterObjectToNamespace(\"VMouseClickIndex0\", VMouseClickIndex0)\r\nEUDOnStart(Reg)\r\n",
            "PluginVariables.py");

    // MSQC off: no [MSQC] lines, and a note.
    {
        Project q;
        makeProject(q, false);
        Ee3Macros off(q.g, q.te, ctx);
        std::vector<std::string> problems;
        expand(off, "@KeyDown(P1, A)", false, problems);
        check(hasProblem(problems, "MSQC is off"), "a note when MSQC is off: " + joined(problems));
        check(off.msqcLines().empty(), "no MSQC lines when MSQC is off");
    }
}

// The sandbox: each limit stops the block with a problem, and the next block still runs.
void testSandbox() {
    Project p;
    makeProject(p);
    ImportCodeContext ctx;
    Ee3Macros m(p.g, p.te, ctx);
    auto stops = [&](const std::string& code, const std::string& part, const std::string& what) {
        std::vector<std::string> problems;
        bool ok = true;
        const auto t0 = std::chrono::steady_clock::now();
        const std::string text = "<?" + code + "?>";
        const std::string out = expand(m, text, false, problems, &ok);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(!ok && out == text && hasProblem(problems, part), what + ": " + joined(problems));
        std::printf("  %s: stopped after %.2f s\n", what.c_str(), sec);
        checkEq(expandOk(m, "<? echo(\"after\") ?>"), "after", what + ": the next block runs");
    };
    stops(" while true do end ", "50 million", "an endless loop");
    stops(" while true do pcall(function() while true do end end) end ", "50 million", "an endless loop in pcall");
    stops(" local co = coroutine.wrap(function() while true do end end) co() ", "50 million", "an endless loop in a coroutine");
    stops(" local t = {} for i = 1, 1e9 do t[i] = i end ", "memory", "a large table");
    stops(" local s = \"x\" while true do s = s .. s end ", "memory", "a long text");
    stops(" echo(string.rep(\"x\", 1e9)) ", "too long", "string.rep");
    stops(" echo(string.rep(\"\", 1e15) .. x()) ", "nil", "string.rep of an empty text (Lua 5.1 loops without end)");
    stops(" string.find(string.rep(\"a\", 3000), string.rep(\"a-\", 20) .. \"b\") ", "pattern", "a slow pattern");
    stops(" string.find(string.rep(\"a\", 200000), string.rep(\"a?\", 200000)) ", "pattern", "a deep pattern (C stack)");
    stops(" local s = string.rep(\"a\", 4000000) while true do local x = s:upper() end ", "stopped", "slow C work (the time limit)");
    stops("\x1BLuaQ\x00\x01\x04", "binary", "a binary chunk as a block");
    stops(" local f, e = loadstring(\"\\27LuaQ\\0\") assert(f, e) ", "binary chunks are not allowed", "loadstring of a binary chunk");
    stops(" xpcall(function() while true do end end, function(e) while true do end end) ", "50 million", "an endless loop in an xpcall handler");
    stops(" xpcall(error, function(e) while true do end end) ", "50 million", "an endless xpcall handler");
    stops(" string.find(string.rep(\"a\", 200000), \"[\" .. string.rep(\"b\", 200000) .. \"a]*\") ", "stopped", "a long bracket class");
    stops(" string.find(string.rep(\"(\", 8000000), \"%b()\") ", "stopped", "%b over a long text");
    stops(" local s = string.rep(\"a\", 4e6) .. \"b\" local p = string.rep(\"a\", 2e6) .. \"b\" echo(tostring(string.find(s, p, 1, true))) ", "stopped",
          "a plain find that a limit stops (it is not a result)");
    checkEq(expandOk(m, "<? local a, b, c = xpcall(function() return 1, 2 end, print) echo(tostring(a) .. b .. c) ?>,<? local ok, e = xpcall(function() error(\"x\", 0) end, "
                        "function(e) return e .. \"!\" end) echo(tostring(ok) .. e) ?>"),
            "true12,falsex!", "xpcall: the results of f, or the handler's");

    // One budget for all the blocks of a script, and a limit on the problem lines.
    {
        Project q;
        makeProject(q);
        Ee3Macros mq(q.g, q.te, ctx);
        std::string text;
        for (int i = 0; i < 100; ++i) text += "<? while true do end ?>\n";
        std::vector<std::string> problems;
        bool ok = true;
        const auto t0 = std::chrono::steady_clock::now();
        const std::string out = expand(mq, text, false, problems, &ok);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(!ok && out == text && sec < 5, "100 endless blocks share one budget: " + std::to_string(sec) + " s");
        check(problems.size() == 21 && hasProblem(problems, "80 more Lua macros fail") && hasProblem(problems, "line 20:"),
              "20 problem lines, then the count: " + std::to_string(problems.size()));
    }
    // Short blocks that fail (each under the hook's 128 instructions) use the script's time too.
    {
        Project q;
        makeProject(q);
        Ee3Macros mq(q.g, q.te, ctx);
        std::string text;
        for (int i = 0; i < 400; ++i) text += "<? local s = string.rep(\"x\", 4e6) s:upper() s:upper() s:upper() error(\"e\") ?>\n";
        std::vector<std::string> problems;
        bool ok = true;
        const auto t0 = std::chrono::steady_clock::now();
        expand(mq, text, false, problems, &ok);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(!ok && sec < 13 && hasProblem(problems, "more Lua macros fail"),"400 slow blocks that fail stop at the time limit: " + std::to_string(sec) + " s");
    }
    // Many small blocks in a large file all expand (EUD Editor 3 expands them, slowly).
    {
        Project q;
        makeProject(q);
        Ee3Macros mq(q.g, q.te, ctx);
        std::string text, want;
        const std::string pad(90, 'x');
        for (int i = 0; i < 10000; ++i) text += "<?echo(1)?>" + pad, want += "1" + pad;
        checkEq(expandOk(mq, text), want, "10,000 blocks in a file of 1 MB");
    }

    // What the Lua must not have.
    checkEq(expandOk(m, "<? echo(tostring(io) .. tostring(os) .. tostring(require) .. tostring(loadfile) .. tostring(dofile) .. "
                        "tostring(debug) .. tostring(package) .. tostring(load) .. tostring(getfenv) .. tostring(setfenv) .. "
                        "tostring(string.dump) .. tostring(collectgarbage) .. tostring(module) .. tostring(newproxy) .. tostring(gcinfo)) ?>"),
            std::string("nil").append(14 * 3, ' ').replace(3, 42, "nilnilnilnilnilnilnilnilnilnilnilnilnilnil"), "no io, os, require, ...");
    checkEq(expandOk(m, "<? local f = loadstring(\"return 1 + 1\") echo(f()) ?>"), "2", "loadstring of text");
}

// --- Real projects (read only) -----------------------------------------------------------------------------------

void showProject(const fs::path& file, const fs::path& dumpDir) {
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    nrbf::Graph g;
    std::string error;
    if (!g.read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), error)) {
        check(false, file.string() + ": " + error);
        return;
    }
    const nrbf::Object* te = g.get(g.root(), "TEData");
    const nrbf::Value& mainRef = g.value(te, "MainFile");
    const int32_t mainId = mainRef.kind == nrbf::Value::Kind::Object ? mainRef.ref : 0;
    ImportCodeContext ctx;
    Ee3Macros m(g, te, ctx);
    size_t files = 0, problems = 0, left = 0;
    std::set<int32_t> seen;
    // EUD Editor 3's build order (WriteTriggerEditor.CreateTEFile): a folder's files, its main file last, then its folders.
    std::function<void(const nrbf::Object*, const std::string&, int)> walk = [&](const nrbf::Object* folder, const std::string& at, int depth) {
        if (!folder || depth > 64 || !seen.insert(folder->id).second) return;
        std::vector<std::pair<const nrbf::Object*, std::string>> list, mains;
        for (const nrbf::Value& fv : g.items(g.get(folder, "Files"))) {
            const nrbf::Object* f = g.get(fv);
            if (!f || !seen.insert(f->id).second) continue;
            const int type = int(g.number(f, "FileType"));
            if (type != 1 && type != 2) continue;  // (CUI scripts: epScript and Python)
            (f->id == mainId ? mains : list).push_back({f, at + g.text(f, "FileName")});
        }
        list.insert(list.end(), mains.begin(), mains.end());
        for (const auto& [f, name] : list) {
            const std::string text = "import PluginVariables as msqcvar;\r\n" + g.text(g.get(f, "Scripter"), "String");
            std::string out;
            std::vector<std::string> probs;
            const auto t0 = std::chrono::steady_clock::now();
            m.apply(text, f->id == mainId, g.text(f, "FileName"), out, probs);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            size_t blocks = 0;
            for (size_t i = text.find("<?"); i != std::string::npos; i = text.find("<?", i + 2)) ++blocks;
            size_t remain = 0;
            for (size_t i = out.find("<?"); i != std::string::npos; i = out.find("<?", i + 2)) ++remain;
            ++files, problems += probs.size(), left += remain;
            if (blocks || remain || !probs.empty() || out != text)
                std::printf("  %s%s: %zu bytes -> %zu, \"<?\" %zu -> %zu, %zu problems, %.1f ms\n", name.c_str(), f->id == mainId ? " (main)" : "",
                            text.size(), out.size(), blocks, remain, probs.size(), ms);
            for (const std::string& pl : probs) std::printf("    problem: %s\n", pl.c_str());
            if (!dumpDir.empty() && blocks) {  // (the text before and after, to compare)
                const fs::path base = dumpDir / (file.stem().string() + "_" + std::to_string(files));
                std::ofstream(fs::path(base).concat(".in.txt"), std::ios::binary) << text;
                std::ofstream(fs::path(base).concat(".out.txt"), std::ios::binary) << out;
            }
        }
        for (const nrbf::Value& dv : g.items(g.get(folder, "Folders"))) {
            const nrbf::Object* d = g.get(dv);
            if (!d || g.number(d, "FileType") == 5) continue;
            walk(d, at + g.text(d, "FileName") + "/", depth + 1);
        }
    };
    std::printf("%s:\n", file.filename().string().c_str());
    std::string bgms;
    for (const nrbf::Value& v : g.items(g.get(g.get(te, "BGMData"), "BGMList"))) bgms += " [" + g.text(g.get(v), "BGMName") + "]";
    std::printf("  BGM list:%s\n", bgms.c_str());
    walk(g.get(te, "ProjectFile"), "", 0);
    std::printf("  %zu scripts, %zu problems, %zu \"<?\" left\n", files, problems, left);
    std::printf("  [MSQC]\n");
    for (const std::string& l : m.msqcLines()) std::printf("    %s\n", l.c_str());
    std::printf("  [chatEvent]\n");
    for (const std::string& l : m.chatEventLines()) std::printf("    %s\n", l.c_str());
    std::printf("  PluginVariables.py:\n%s\n", m.pluginVariables().c_str());
    check(left == 0, file.filename().string() + ": no \"<?\" is left");
}

}  // namespace

int main(int argc, char** argv) {
    testMacros();
    testSandbox();
    if (argc > 1) {
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(fs::u8path(argv[1])))
            if (e.path().extension() == ".e3s") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files) showProject(f, argc > 2 ? fs::u8path(argv[2]) : fs::path());
    }
    std::printf(failures ? "FAILED (%d failed)\n" : "PASSED (%d failed)\n", failures);
    return failures ? 1 : 0;
}
