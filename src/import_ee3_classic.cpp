#include "stardraft/import_codegen.h"

// EUD Editor 3's classic triggers to epScript: the text of ClassicTriggerEditor.GetFileText (ClassicTriggerEditor.vb,
// Trigger.vb, TriggerCodeBlock.vb and ArgValue.vb in EUD Editor 3's Class/TriggerEditor/ClassicEditor), before its Lua
// macro step (MacroApply). That step also puts "import PluginVariables as msqcvar;" before the text; the importer adds
// that line itself when the code uses msqcvar. The "<? ... ?>" parts stay as EUD Editor 3 writes them (the macro step
// expands them), with one exception: "<? LuaPlayerVariable = N?>" in each player's part. It only sets a Lua variable for
// the macros, and the macro step writes nothing for it, so when the file has no other macro it goes (its indent stays).
//
// The lines end in CRLF, as EUD Editor 3's StringBuilder.AppendLine writes them. Where EUD Editor 3 writes code that
// does not build, or stops with an error, the text here gets an epScript comment and `problems` gets a line.

#include <cctype>
#include <set>
#include <string_view>

namespace stardraft {

namespace {

#include "data/ee3_classic_functions.inc"

constexpr const char* kNl = "\r\n";

// TriggerFunction.EFType
enum FType { Action = 0, Condition = 1, Plib = 2, Lua = 3, UserFunc = 4, ExternFunc = 5, Args = 6, None = 7 };

const char* typeName(int64_t t) {
    static const char* const kNames[] = {"Action", "Condition", "Plib", "Lua", "UserFunc", "ExternFunc", "Args", "None"};
    return t >= 0 && t < 8 ? kNames[t] : "unknown";
}

template <size_t N>
bool listed(const char* const (&names)[N], const std::string& name) {
    for (const char* n : names)
        if (name == n) return true;
    return false;
}

std::string spaces(int intend) { return std::string(size_t(intend) * 4, ' '); }

// ArgValue.LuaCover: the text in double quotes (once: a text in quotes keeps its quotes).
std::string luaCover(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return "\"" + s + "\"";
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    for (size_t i = s.find(from); i != std::string::npos; i = s.find(from, i + to.size())) s.replace(i, from.size(), to);
    return s;
}

struct Gen {
    const nrbf::Graph& g;
    const ImportCodeContext& ctx;
    std::vector<std::string>& problems;
    std::set<std::string> said;  // (a problem once)
    bool hasImports = false;
    // The budget of the whole file: blocks can be shared (one object, used by many arguments, or by itself), and each use
    // writes the block again, so a small file can ask for text without end. Past the budget the file is not made.
    static constexpr size_t kMaxBlocks = 200000;          // blocks written
    static constexpr size_t kMaxText = 16 * 1024 * 1024;  // bytes of code
    size_t blocks = 0;
    bool over = false;
    bool spend(size_t textSize) {
        if (!over && (blocks > kMaxBlocks || textSize > kMaxText)) {
            over = true;
            problems.push_back("the classic triggers make more code than an import allows (shared blocks?): not made");
        }
        return !over;
    }
    std::string where;  // the trigger that the problems are about ("trigger 3")

    // A problem of the trigger in `where`: "trigger 3: <p>"; `verb`: "trigger 3 <p>" ("uses the macro ...").
    void problem(const std::string& p, bool verb = false) {
        const std::string line = where.empty() ? p : where + (verb ? " " : ": ") + p;
        if (said.insert(line).second) problems.push_back(line);
    }

    // A string member: `null` true when it is Nothing (EUD Editor 3 then fails where it uses the text's length).
    std::string str(const nrbf::Object* o, std::string_view member, bool* null = nullptr) const {
        const nrbf::Value& v = g.value(o, member);
        if (null) *null = v.kind != nrbf::Value::Kind::String;
        return v.kind == nrbf::Value::Kind::String ? v.s : std::string();
    }

    // TriggerCodeBlock.GetCodeFunction: is the function in EUD Editor 3's lists (TriggerManager.GetTriggerList)?
    bool known(int64_t type, const std::string& name) const {
        switch (type) {
        case Action: return listed(kEe3ClassicActions, name);
        case Condition: return listed(kEe3ClassicConditions, name);
        case Plib: return listed(kEe3ClassicPlib, name);
        case Lua: return listed(kEe3ClassicLua, name);
        // The functions of the imported files: EUD Editor 3 reads them from the files' code. That code is not here, so a
        // function of an import counts as there when the file has imports.
        case ExternFunc: return hasImports;
        default: return false;
        }
    }

    // ArgValue.GetCodeText.
    std::string argCode(const nrbf::Object* a, bool luaCover, int depth) {
        if (!a) {
            problem("an argument is missing");
            return "/* EUD Editor 3: no value */";
        }
        const std::string type = str(a, "ValueType");
        bool null = false;
        std::string v;
        if (type == "Variable") {
            v = str(a, "ValueString") + str(a, "ValueString2");
            if (luaCover) v = ::stardraft::luaCover(v);
        } else if (type == "Function") {
            const nrbf::Object* block = g.get(a, "CodeBlock");
            if (!block) {
                problem("an argument has no function selected (EUD Editor 3 writes FuncNoSelect)");
                v = "FuncNoSelect";
            } else {
                bool unknown = false;
                v = blockCode(block, 0, luaCover, depth + 1, unknown);
                if (unknown) v = "/* EUD Editor 3: no function " + str(block, "FName") + " */";
                else if (luaCover && g.number(block, "FType") != Lua) v = ::stardraft::luaCover(v);
            }
        } else if (g.value(a, "IsArgNumber").asBool()) {
            const int64_t n = g.number(a, "ValueNumber");
            if (type == "TrgLocation") {
                v = std::to_string(n + 1);
                // The location by its index in the map (the old editor wrote the number; the name is the label it showed).
                if (!ctx.locationNames.empty() && (n < 0 || size_t(n) >= ctx.locationNames.size() || ctx.locationNames[size_t(n)].empty()))
                    problem("location " + std::to_string(n + 1) + " (" + str(a, "ValueString") + ") is not in the map");
            } else {
                v = std::to_string(n);
            }
        } else {
            v = str(a, "ValueString", &null);
            if (type == "TrgProperty") v = "UnitProperty(" + v + ")", null = false;
            static const std::set<std::string> kQuoted = {"TrgString", "FormatString", "WAVName", "TrgSwitch", "TrgAIScript"};
            static const std::set<std::string> kBare = {"TrgAllyStatus", "TrgComparison", "TrgModifier", "TrgOrder", "TrgPlayer", "TrgPropState",
                                                        "TrgResource", "TrgScore", "TrgSwitchAction", "TrgSwitchState", "BGM"};
            if (null) {
                // (EUD Editor 3: an argument that was never set has no text: it writes nothing, or stops at its length)
                problem("an argument of type " + (type.empty() ? std::string("RawCode") : type) + " has no value");
                return "/* EUD Editor 3: no value */";
            }
            if (type == "TrgSwitch" && !ctx.switchNames.empty()) {
                // The switch by its name in the map (or "Switch N"), as the old editor listed them.
                bool found = false;
                for (size_t k = 0; k < ctx.switchNames.size() && !found; ++k)
                    found = v == ctx.switchNames[k] || v == "Switch " + std::to_string(k + 1);
                if (!found) problem("the switch \"" + v + "\" is not in the map");
            }
            if (kQuoted.count(type)) v = ::stardraft::luaCover(v);
            else if (luaCover && !kBare.count(type)) v = ::stardraft::luaCover(v);
        }
        if (luaCover) {
            v = replaceAll(v, "\\\"", "\x01");
            v = replaceAll(v, "\\", "\\\\");
            v = replaceAll(v, "\x01", "\\\"");
        }
        return v;
    }

    // TriggerCodeBlock.GetCodeText. `unknown`: the function is not in EUD Editor 3's lists (it then writes "").
    std::string blockCode(const nrbf::Object* b, int intend, bool isLua, int depth, bool& unknown) {
        unknown = false;
        if (!spend(0) || (++blocks, !spend(0))) return unknown = true, std::string();
        const int64_t type = g.number(b, "FType");
        const std::string name = str(b, "FName");
        if (depth > 64) {
            problem("the functions in " + name + " are nested too deep");
            unknown = true;
            return "";
        }
        if (type == UserFunc || type == Args || type == None || type < 0 || type > None) {
            // (EUD Editor 3 has no list for these types: its GetFileText stops with an error)
            problem(name + " is a block of type " + typeName(type) + ": EUD Editor 3 cannot build it");
            unknown = true;
            return "";
        }
        if (!known(type, name)) {
            problem(name + " is not a function of EUD Editor 3 (" + typeName(type) + "): EUD Editor 3 writes no code for it");
            unknown = true;
            return "";
        }
        if (type == Lua) problem("uses the macro " + name + ": needs EUD Editor 3's build", true);
        const std::vector<nrbf::Value> args = g.items(g.get(b, "Args"));
        auto arg = [&](size_t k, bool lua) {
            if (k >= args.size()) {
                problem(name + " has " + std::to_string(args.size()) + " arguments, fewer than it needs");
                return std::string("/* EUD Editor 3: no value */");
            }
            return argCode(g.get(args[k]), lua, depth);
        };

        bool firstCode = false;
        std::string r;
        if (!isLua) {
            if (type == Lua) r = "<?", firstCode = true, isLua = true;
        } else if (type != Lua) {
            isLua = false;
        }
        bool system = false;
        if ((type == Action || type == Condition) && name == "RawCode") system = true, r += arg(0, isLua);
        if (type == Condition && name == "Var") {
            system = true;
            r += arg(0, isLua);
            const std::string op = arg(1, isLua);
            r += op == "Exactly" ? " == " : op == "AtLeast" ? " >= " : op == "AtMost" ? " <= " : op;
            r += arg(2, isLua);
        }
        if (type == Action && name == "SetVar") {
            system = true;
            r += arg(0, isLua);
            const std::string op = arg(1, isLua);
            r += op == "SetTo" ? " = " : op == "Add" ? " += " : op == "Subtract" ? " -= " : op;
            r += arg(2, isLua);
        }
        if (!system) {
            r += type == ExternFunc ? str(b, "FGroup") + "." + name + "(" : name + "(";
            for (size_t k = 0; k < args.size(); ++k) {
                if (k) r += ", ";
                r += argCode(g.get(args[k]), isLua, depth);
            }
            r += ")";
        }
        if (firstCode) r += "?>";
        if (!spend(r.size())) return unknown = true, std::string();
        return spaces(intend) + r;
    }

    // An action's line: the block's code and ";" (EUD Editor 3: ";" alone when it does not know the function).
    std::string actionLine(const nrbf::Object* b, int intend) {
        if (!b) {
            problem("an action is missing");
            return spaces(intend) + "/* EUD Editor 3: no action */;";
        }
        bool unknown = false;
        const std::string code = blockCode(b, intend, false, 0, unknown);
        return (unknown ? spaces(intend) + "/* EUD Editor 3: no code for " + str(b, "FName") + " */" : code) + ";";
    }

    // A condition's code ("" when EUD Editor 3 does not know the function: it then leaves the condition out).
    std::string conditionCode(const nrbf::Object* b, int intend) {
        if (!b) return problem("a condition is missing"), std::string();
        bool unknown = false;
        std::string code = blockCode(b, intend, false, 0, unknown);
        return unknown ? std::string() : code;
    }

    // Trigger.GetTriggerCodeText.
    std::string triggerCode(const nrbf::Object* t, int intend) {
        std::string sb;
        auto line = [&](const std::string& s) { sb += s, sb += kNl; };
        const std::string flag = "PreserveFlag";
        const std::string ispace = spaces(intend);
        std::vector<const nrbf::Object*> conditions, actions;
        for (const nrbf::Value& v : g.items(g.get(t, "Condition"))) conditions.push_back(g.get(v));
        for (const nrbf::Value& v : g.items(g.get(t, "Actions"))) actions.push_back(g.get(v));
        const bool onlyCode = g.value(t, "IsOnlyCode").asBool();
        const bool preserved = g.value(t, "IsPreserved").isNull() ? true : g.value(t, "IsPreserved").asBool();
        bool waitable = false;
        for (const nrbf::Object* a : actions)
            if (a && g.number(a, "FType") == Lua && str(a, "FName") == "Wait") waitable = true;

        int tstart = 0;
        if (!onlyCode) line(ispace + "{");
        line(str(t, "CodeText"));
        if (!onlyCode) {
            if (waitable) {
                if (!preserved) line(ispace + "    const " + flag + " = EUDLightBool();");
                line(ispace + "    <? WaitStart()?>");
                if (!preserved) line(ispace + "    if (" + flag + ".IsCleared()){");
                line(ispace + "    <? WaitConditionStart()?>");
                std::string con;
                for (const nrbf::Object* c : conditions) {
                    std::string r = conditionCode(c, intend + 2);
                    if (tstart != 0) r = std::string(" && ") + kNl + r;
                    if (!r.empty()) con += r, ++tstart;
                }
                if (con.empty()) con = ispace + "        Always()";
                line(con);
                line(ispace + "    <? WaitConditionEnd()?>");
                line(ispace + "    <? WaitActionStart()?>");
                for (const nrbf::Object* a : actions) line(actionLine(a, intend + 2));
                if (!preserved) line(ispace + "        DoActions(" + flag + ".Set());");
                line(ispace + "    <? WaitActionEnd()?>");
                line(ispace + "    <? WaitEnd()?>");
                if (!preserved) line(ispace + "    }");
            } else {
                if (!preserved) line(ispace + "    const " + flag + " = EUDLightBool();");
                const bool test = !(conditions.empty() && preserved);
                if (test) {
                    line(ispace + "    if (");
                    if (!preserved) sb += ispace + "        " + flag + ".IsCleared()", ++tstart;
                    for (const nrbf::Object* c : conditions) {
                        std::string r = conditionCode(c, intend + 2);
                        if (tstart != 0) r = std::string(" && ") + kNl + r;
                        if (!r.empty()) sb += r, ++tstart;
                    }
                    line("");
                    line(ispace + "    ){");
                }
                for (const nrbf::Object* a : actions) line(actionLine(a, intend + 2));
                if (!preserved) line(ispace + "        DoActions(" + flag + ".Set());");
                if (test) line(ispace + "    }");
            }
        }
        if (!onlyCode) line(ispace + "}");
        return sb;
    }
};

}  // namespace

bool ee3ClassicTriggerCode(const nrbf::Graph& g, const nrbf::Object* scripter, const std::string& fileName, const ImportCodeContext& ctx,
                           std::string& eps, std::vector<std::string>& problems) {
    (void)fileName;  // (EUD Editor 3 uses it only in the macro step's error texts)
    eps.clear();
    if (!scripter || scripter->shortName() != "ClassicTriggerEditor") {
        problems.push_back("classic triggers: the file has no classic trigger editor");
        return false;
    }
    Gen gen{g, ctx, problems};

    // IsMain: the project's main file (TEData.MainFile) is this file.
    bool isMain = false;
    if (const nrbf::Object* main = g.get(g.get(g.root(), "TEData"), "MainFile"))
        if (const nrbf::Object* s = g.get(main, "Scripter")) isMain = s->id == scripter->id;

    // LoadInit: the triggers are in TriggerListCollection; in older files in TriggerList.
    const nrbf::Object* list = g.get(scripter, "TriggerListCollection");
    if (!list) list = g.get(scripter, "TriggerList");
    std::vector<const nrbf::Object*> triggers;
    for (const nrbf::Value& v : g.items(list))
        if (const nrbf::Object* t = g.get(v)) triggers.push_back(t);

    std::string sb;
    auto line = [&](const std::string& s) { sb += s, sb += kNl; };

    // GetEpsText: the imports, the global variables, then ClassicTriggerExec with each player's triggers.
    line("/*================Start Import================*/");
    for (const nrbf::Value& v : g.items(g.get(scripter, "ImportFiles"))) {
        const nrbf::Object* imp = g.get(v);
        if (!imp) continue;
        gen.hasImports = true;
        line("import " + gen.str(imp, "vfolder") + " as " + gen.str(imp, "vname") + ";");
    }
    line("/*================End Import================*/");

    line("/*================Start Var================*/");
    for (const nrbf::Value& v : g.items(g.get(scripter, "globalVar"))) {
        const nrbf::Object* var = g.get(v);
        if (!var) continue;
        const std::string name = gen.str(var, "vname"), type = gen.str(var, "vtype"), init = gen.str(var, "vinit");
        const bool none = init == "NULL", many = init.find(',') != std::string::npos;
        if (type == "Default") line(none ? "var " + name + ";" : "var " + name + " = " + init + ";");
        else if (type == "Const") line("const " + name + " = " + (none ? "0" : init) + ";");
        else if (type == "PVariable") line("const " + name + " = " + (none ? "PVariable()" : "PVariable(list(" + init + "))") + ";");
        else if (type == "Array") line("const " + name + " = " + (none ? "EUDArray(10)" : many ? "[" + init + "]" : "EUDArray(" + init + ")") + ";");
        else if (type == "VArray") line("const " + name + " = " + (none ? "EUDVArray(10)()" : many ? "VArray(" + init + ")" : "EUDVArray(" + init + ")()") + ";");
        else problems.push_back("the global variable " + name + " has the type \"" + type + "\": EUD Editor 3 writes no code for it");
    }
    line("/*================End Var================*/");

    // The players that each trigger runs for.
    std::vector<std::vector<size_t>> playerTriggers(8);
    for (size_t i = 0; i < triggers.size(); ++i) {
        const nrbf::Object* t = triggers[i];
        const std::string comment = gen.str(t, "CommentString");
        const std::string label = "trigger " + std::to_string(i + 1) + (comment.empty() ? "" : " (\"" + comment + "\")");
        std::vector<nrbf::Value> players = g.items(g.get(t, "PlayerEnabled"));
        std::vector<nrbf::Value> forces = g.items(g.get(t, "ForceEnabled"));
        players.resize(8), forces.resize(7);
        bool run[8] = {};
        for (int p = 0; p < 8; ++p) run[p] = players[size_t(p)].asBool();
        if (forces[0].asBool())  // All players
            for (bool& r : run) r = true;
        // Force 1 to 4, the human players and the computers: EUD Editor 3 takes them from the map's players (FORC and
        // OWNR) at build time. Its force test compares each player's force with the trigger's position less 1, not with
        // the checked force: this does the same, so that the code is the same.
        const bool haveForces = ctx.playerForces.size() >= 8, haveControls = ctx.playerControls.size() >= 8;
        for (int f = 1; f <= 4; ++f) {
            if (!forces[size_t(f)].asBool()) continue;
            if (!haveForces) {
                problems.push_back(label + " runs for Force " + std::to_string(f) +
                                   ": the map's forces are not known here, so it runs only for the players that it names");
                continue;
            }
            const int forceNum = int(i) - 1;
            bool same = true;
            for (int p = 0; p < 8; ++p) {
                const int force = ctx.playerForces[size_t(p)];
                if (force == forceNum) run[p] = true;
                if ((force == forceNum) != (force == f - 1)) same = false;
            }
            if (!same)
                problems.push_back(label + " runs for Force " + std::to_string(f) + ": EUD Editor 3 gives it to the players of " +
                                   (forceNum >= 0 && forceNum <= 3 ? "Force " + std::to_string(forceNum + 1) : std::string("no force")) +
                                   " (it compares the force with the trigger's position), and so does the import");
        }
        // (MapData.vb: an owner of 3, 5 or 7 is a computer, 6 is a human)
        auto control = [&](int p) { return ctx.playerControls[size_t(p)]; };
        if (forces[5].asBool()) {
            if (!haveControls)
                problems.push_back(label + " runs for the human players: the map's player types are not known here, so it runs only for the players that it names");
            else
                for (int p = 0; p < 8; ++p)
                    if (control(p) == 6) run[p] = true;
        }
        if (forces[6].asBool()) {
            if (!haveControls)
                problems.push_back(label + " runs for the computer players: the map's player types are not known here, so it runs only for the players that it names");
            else
                for (int p = 0; p < 8; ++p)
                    if (control(p) == 3 || control(p) == 5 || control(p) == 7) run[p] = true;
        }
        for (int p = 0; p < 8; ++p)
            if (run[p]) playerTriggers[size_t(p)].push_back(i);
    }

    // The code of each trigger (the same for each player that it runs for: made once).
    std::vector<std::string> code(triggers.size());
    std::vector<bool> made(triggers.size(), false);
    line("function ClassicTriggerExec(){");
    if (!isMain) line("    const _origcp = getcurpl();");
    for (int player = 0; player < 8; ++player) {
        const std::vector<size_t>& tlist = playerTriggers[size_t(player)];
        if (tlist.empty()) continue;
        const std::string p = std::to_string(player);
        line("    /*================Start Player " + std::to_string(player + 1) + "================*/");
        line("    if (playerexist(" + p + "))");
        line("    {");
        line("        setcurpl(" + p + ");");
        line("        const cp = " + p + ";");
        line("        <? LuaPlayerVariable = " + p + "?>");
        for (size_t i : tlist) {
            const nrbf::Object* t = triggers[i];
            const nrbf::Value& enabled = g.value(t, "IsEnabled");
            if (!enabled.isNull() && !enabled.asBool()) continue;  // (a disabled trigger: no code)
            if (!made[i]) {
                const std::string comment = gen.str(t, "CommentString");
                gen.where = "trigger " + std::to_string(i + 1) + (comment.empty() ? "" : " (\"" + comment + "\")");
                code[i] = gen.triggerCode(t, 2);
                gen.where.clear();
                made[i] = true;
            }
            sb += code[i];
            if (!gen.spend(sb.size())) break;
        }
        line("    }");
        line("    /*================End Player " + std::to_string(player + 1) + "================*/");
    }
    if (!isMain) line("    setcurpl(_origcp);");
    line("}");

    // GetFileText: the main file also calls the triggers before the others (beforeTriggerExec).
    if (isMain) sb += std::string("function beforeTriggerExec(){") + kNl + "\tClassicTriggerExec();" + kNl + "}";

    // "<? LuaPlayerVariable = N?>": the macro step writes "" for it. When no other macro is left ("<?", or "@Name(" with
    // a macro's name, which the macro step makes into "<? ?>"), it goes here too, so the file needs no macro step.
    std::string rest = sb;
    for (int p = 0; p < 8; ++p) rest = replaceAll(rest, "<? LuaPlayerVariable = " + std::to_string(p) + "?>", "");
    bool macros = rest.find("<?") != std::string::npos;
    for (size_t at = rest.find('@'); at != std::string::npos && !macros; at = rest.find('@', at + 1)) {
        size_t e = at + 1;
        while (e < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[e])) || rest[e] == '_')) ++e;
        macros = e < rest.size() && rest[e] == '(' && listed(kEe3ClassicLua, rest.substr(at + 1, e - at - 1));
    }
    if (!macros) sb = std::move(rest);
    if (!gen.spend(sb.size())) return eps.clear(), false;
    eps = std::move(sb);
    return true;
}

}  // namespace stardraft
