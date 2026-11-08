#include "stardraft/import_codegen.h"

// EUD Editor 3's block scripts (GUIScriptEditor) to epScript: a port of GUIScriptEditor.GetFileText and
// GUIScriptManager.GetItemScriptText / GetScriptText (Class/TriggerEditor/GUIScriptEditor/), without the last step
// (macro.MacroApply, the Lua macros). The text is the same as EUD Editor 3 writes, quirks included: line ends are CRLF
// (StringBuilder.AppendLine), statements at the top level get no line end, and some blocks write wrong code (see the
// problems below). Where EUD Editor 3 fails (an exception, so it writes no file), the text has a comment and `problems`
// says why.
//
// GUIPy files: EUD Editor 3 has no Python code maker for block scripts. GetFileText writes epScript for both kinds, and
// the build saves it as a .py file, which euddraft cannot run. So a GUIPy file gives false.

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <set>

namespace stardraft {

namespace {

// ScriptBlock.EBlockType (ScriptBlock/ScriptBlockMain.vb).
enum BlockType {
    none = -1, constVal, action, condition, plibfun, macrofun, funuse, externfun, fundefine, funargs, funargblock,
    funcontent, if_, ifcondition, ifthen, elseif_, ifelse, for_, foraction, while_, whilecondition, whileaction,
    switch_, switchvar, switchcase, vardefine, varuse, externvaruse, folder, folderaction, objectdefine, objectfields,
    objectmethod, rawcode, import_, or_, and_, exp_, sign, funreturn, break_,
};

// A ScriptBlock as the code maker reads it. A string that is Nothing in the file is "" here (VB: Nothing = "").
struct Block {
    int type = none;
    bool isfolder = false, flag = false;
    std::string name, value, value2;
    const Block* vchild = nullptr;
    std::vector<const Block*> child;
    // The parent, as GUIScriptEditor.LoadInit sets it: the block that has this one as a child or as its VChild. A top
    // block keeps the Parent of the file (normally Nothing).
    bool hasParent = false, parentFolder = false;
};

// The language that EUD Editor 3 writes into a function's doc comment (pgData.Setting(Language), the user's own
// setting: "ko-KR" or "en-US"). The file does not keep it; most EUD Editor 3 users have Korean.
constexpr const char* kLanguage = "ko-KR";

// tescm.SCValueNoneType (GUIScriptManager.vb): argument types that a function's parameter list writes without ":Type".
const std::vector<std::string> kValueNoneTypes = {
    "None", "Number", "WAVName", "Variable", "FormatText", "BGM", "Tag", "UnitsDat", "WeaponsDat", "FlingyDat",
    "SpritesDat", "ImagesDat", "UpgradesDat", "TechdataDat", "OrdersDat", "Weapon", "Flingy", "Sprite", "Image",
    "Upgrade", "Tech", "Order", "EUDScore", "SupplyType"};

// Tool.GetArgTypeList (Module/Tools/Tool.vb): the names of EUD Editor 3's Data/TriggerEditor/Autocmp/*.txt files (up to
// the first "."). Values of these types are written in quotes.
const std::vector<std::string> kArgTypes = {"AblityFlags+FLAG", "Button", "CUnitOffset", "DateType", "Key", "SCABanType",
                                            "StatusFlags+FLAG", "Weekend"};

bool contains(const std::vector<std::string>& list, const std::string& s) {
    for (const std::string& e : list)
        if (e == s) return true;
    return false;
}

// VB's String.Trim (white space; enough for ASCII).
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// VB's String.Split(separator) with one separator (here a UTF-8 sequence): empty parts stay.
std::vector<std::string> split(const std::string& s, const std::string& sep) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        size_t f = s.find(sep, at);
        if (f == std::string::npos) break;
        out.push_back(s.substr(at, f - at));
        at = f + sep.size();
    }
    out.push_back(s.substr(at));
    return out;
}

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// VB's IsNumeric / CDbl on a string (the forms that matter: decimal, exponent, &H hex, &O octal). False: not a number.
bool vbNumber(const std::string& text, double& out) {
    std::string s = trim(text);
    if (s.empty()) return false;
    if (s.size() > 2 && s[0] == '&' && (s[1] == 'H' || s[1] == 'h' || s[1] == 'O' || s[1] == 'o')) {
        char* end = nullptr;
        const unsigned long long v = std::strtoull(s.c_str() + 2, &end, (s[1] == 'H' || s[1] == 'h') ? 16 : 8);
        if (*end) return false;
        out = double(v);
        return true;
    }
    for (char c : s)
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '+' || c == '-' || c == 'e' || c == 'E')) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && !*end;
}

// VB's CBool on a string ("True", "False", or a number). False in `ok`: VB throws.
bool vbBool(const std::string& text, bool& ok) {
    const std::string s = lowerAscii(trim(text));
    ok = true;
    if (s == "true") return true;
    if (s == "false") return false;
    double d = 0;
    if (vbNumber(s, d)) return d != 0;
    ok = false;
    return false;
}

// Limits for a damaged or hostile file: a block that the file refers to many times is loaded each time (as EUD Editor 3
// does), so a few shared blocks can make a tree of 2^n blocks. Past either limit the file is not imported.
constexpr size_t kMaxBlocks = 200000;
constexpr size_t kMaxText = size_t(16) << 20;  // 16 MB

std::string tabs(int n) { return std::string(size_t(n > 0 ? n : 0), '\t'); }

class Maker {
public:
    Maker(const ImportCodeContext& ctx, const std::string& fileName, std::vector<std::string>& problems)
        : ctx_(ctx), file_(fileName), problems_(problems) {}

    // GUIScriptManager.GetScriptText.
    void list(const std::vector<const Block*>& scr, std::string& sb, int& intend, const std::string& spliter, bool isCondition = false,
              bool isAnd = true, bool isFuncArg = false, bool isValCover = false) {
        for (size_t i = 0; i < scr.size() && !tooBig_; ++i) {
            item(*scr[i], sb, intend, spliter, isCondition, isAnd, isFuncArg, isValCover, i + 1 == scr.size(), scr.size() == 1);
            if (sb.size() > kMaxText) tooBig_ = true;
        }
    }
    // True when the text went past kMaxText (blocks that the file refers to many times): the text is not complete.
    bool tooBig() const { return tooBig_; }

    void problem(const std::string& what) {
        const std::string line = "Block script " + file_ + ": " + what;
        if (seen_.insert(line).second) problems_.push_back(line);
    }

private:
    static void line(std::string& sb, const std::string& s = {}) { sb += s, sb += "\r\n"; }

    // A place where EUD Editor 3 throws (it then writes no file): a comment in the text, and a problem.
    void fails(std::string& sb, const std::string& what) {
        sb += "/* EUD Editor 3 cannot write this: " + what + " */";
        problem(what + " (EUD Editor 3's build fails on it; the code has a comment there)");
    }

    // tescm.GetFuncArgs: the children of a fundefine's funargs block (nullptr: none, VB returns Nothing).
    static const std::vector<const Block*>* funcArgs(const Block& f) {
        for (const Block* c : f.child)
            if (c->type == funargs) return &c->child;
        return nullptr;
    }
    // ScriptBlock.GetFuncTooltip.
    static std::string funcTooltip(const Block& f) {
        for (const Block* c : f.child)
            if (c->type == funargs) return c->value;
        return {};
    }

    // ScriptBlock.ForScriptCoder. False: VB throws (an index out of range, a bad number).
    bool forCode(const Block& scr, std::string& out) {
        const std::vector<std::string> parts = split(scr.value, "\xE1\x9A\xA2");  // ᚢ
        const std::string& forType = parts.front();
        const std::string& vvalue = parts.back();
        const std::vector<std::string> v = split(vvalue, "\xE1\x97\xA2");  // ᗢ
        out.clear();
        if (forType == "UserEdit") {
            if (v.size() < 2) return false;
            out = lowerAscii(v[0]) + "(" + v[1] + ")";
        } else if (forType == "CountRepeat") {
            if (v.size() == 3) {
                const std::string &vname = v[0], &vinit = v[1], &vcount = v[2];
                std::string lt;
                double a = 0, b = 0;
                if (vbNumber(vinit, a) && vbNumber(vcount, b)) {
                    // "tv = vinit + vcount" with two Strings: VB joins them as text, then makes an Integer of that.
                    double joined = 0;
                    if (!vbNumber(vinit + vcount, joined) || std::nearbyint(joined) < -2147483648.0 || std::nearbyint(joined) > 2147483647.0)
                        return false;
                    const long long tv = (long long)std::nearbyint(joined);  // (CInt rounds half to even, as nearbyint)
                    lt = std::to_string(tv);
                    if (double(tv) != std::nearbyint(a + b))
                        problem("the repeat loop of " + vname + " (start " + trim(vinit) + ", count " + trim(vcount) + ") ends at " + lt +
                                ", not at " + std::to_string((long long)std::nearbyint(a + b)) +
                                " (EUD Editor 3 joins the two numbers as text); the code is as EUD Editor 3 writes it");
                } else {
                    lt = vinit + " + " + vcount;
                }
                out = "for(var " + vname + " = " + vinit + "; " + vname + " < " + lt + "; " + vname + "++)";
            }
        } else if (forType == "EUDLoopNewUnit" || forType == "EUDLoopUnit" || forType == "EUDLoopUnit2" || forType == "EUDLoopSprite") {
            out = "foreach(" + vvalue + " : " + forType + "())";
        } else if (forType == "Timeline" || forType == "EUDLoopPlayerUnit") {
            if (v.size() < 2) return false;
            out = "foreach(" + v[0] + " : " + forType + "(" + v[1] + "))";
        } else if (forType == "EUDLoopPlayer") {
            if (v.size() < 4) return false;
            std::string owner = v[1], race = v[3];
            if (owner != "None") owner = "\"" + owner + "\"";
            if (race != "None") race = "\"" + race + "\"";
            out = "foreach(" + v[0] + " : " + forType + "(" + owner + "," + v[2] + "," + race + "))";
        } else if (forType == "EUDPlayerLoop") {
            out = "EUDPlayerLoop()();";
        } else if (forType == "defaultvalue") {
            problem("a loop block has no loop set (EUD Editor 3 writes an empty loop head)");
        } else {
            problem("a loop block of the unknown kind \"" + forType + "\" (EUD Editor 3 writes an empty loop head)");
        }
        return true;
    }

    // The label of a location as EUD Editor 3 lists them (CodeEditor.GetArgList("TrgLocationIndex"): pjData.CodeLabel
    // for 0..254: the map's name, or "Location n").
    std::string locationLabel(size_t i) const {
        if (i < ctx_.locationNames.size() && !ctx_.locationNames[i].empty()) return ctx_.locationNames[i];
        return "Location " + std::to_string(i + 1);
    }

    // GUIScriptManager.GetItemScriptText.
    void item(const Block& scr, std::string& sb, int& intend, const std::string& spliter, bool isCondition, bool isAnd, bool isFuncArg,
              bool isValCover, bool isLastValue, bool isOneValue) {
        const int stype = scr.type;
        const std::string& sname = scr.name;
        const std::string& svalue = scr.value;
        const std::string& svalue2 = scr.value2;
        const bool flag = scr.flag;
        const std::vector<const Block*>& schild = scr.child;
        const bool parentFolder = scr.hasParent && scr.parentFolder;
        auto andOr = [&] {
            if (!isLastValue) sb += isAnd ? " && " : " || ";
        };

        if (isValCover && stype != constVal) sb += "\"";
        switch (stype) {
        case import_:
            sb += tabs(intend) + "import " + svalue;
            line(sb, ";");
            break;
        case rawcode:
            sb += svalue;
            if (parentFolder) line(sb);
            break;
        case vardefine:
            sb += tabs(intend);
            sb += flag ? (svalue2 == "static" ? "static " : "const ") : "var ";
            sb += svalue;
            if (!schild.empty()) {
                sb += " = ";
                list(schild, sb, intend, ",");
            }
            line(sb, ";");
            break;
        case objectdefine:
            sb += tabs(intend) + "object " + svalue;
            line(sb, "{");
            ++intend;
            list(schild, sb, intend, "");
            --intend;
            sb += tabs(intend);
            line(sb, "};");
            break;
        case objectfields:
        case objectmethod:
            list(schild, sb, intend, "");
            break;
        case fundefine: {
            const std::vector<const Block*>* args = funcArgs(scr);
            if (spliter != "PREDEF") {
                const std::string in = tabs(intend);
                line(sb, in + "/***");
                line(sb, in + " * @Type");
                line(sb, in + " * F");
                line(sb, in + " * @Summary." + kLanguage);
                line(sb, in + " * " + funcTooltip(scr));
                line(sb, in + " * ");
                if (!args) {
                    fails(sb, "the function " + svalue + " has no parameter block");
                    line(sb);
                } else {
                    for (const Block* a : *args) {
                        line(sb, in + " * @param." + a->value + "." + kLanguage);
                        line(sb, in + " * " + a->value2);
                    }
                }
                line(sb, in + "***/");
            }
            sb += tabs(intend) + "function " + svalue;
            if (spliter == "PREDEF") {
                sb += "(";
                if (args) list(*args, sb, intend, ", ", false, false, true);
                else fails(sb, "the function " + svalue + " has no parameter block");
                sb += ")";
                line(sb, ";");
                return;  // (as in VB: no spliter after it)
            }
            list(schild, sb, intend, "", false, false, true);
            break;
        }
        case funargs:
            sb += "(";
            list(schild, sb, intend, ", ", false, true, true);
            sb += ")";
            break;
        case funcontent:
            line(sb, "{");
            ++intend;
            list(schild, sb, intend, "");
            --intend;
            sb += tabs(intend);
            line(sb, "}");
            break;
        case if_:
            sb += tabs(intend) + "if";
            list(schild, sb, intend, "");
            line(sb);
            break;
        case elseif_:
            sb += tabs(intend) + "else if";
            list(schild, sb, intend, "");
            line(sb);
            break;
        case ifcondition:
            line(sb, "(");
            ++intend;
            list(schild, sb, intend, "", true);
            --intend;
            sb += tabs(intend) + ")";
            break;
        case ifthen:
            line(sb, "{");
            ++intend;
            list(schild, sb, intend, "");
            --intend;
            sb += tabs(intend) + "}";
            break;
        case ifelse:
            line(sb, "else{");
            ++intend;
            list(schild, sb, intend, "");
            --intend;
            sb += tabs(intend) + "}";
            break;
        case for_: {
            const std::string forType = split(svalue, "\xE1\x9A\xA2").front();
            std::string head;
            sb += tabs(intend);
            if (!forCode(scr, head)) {
                fails(sb, "a loop block with the value \"" + svalue + "\" (too few parts)");
                head.clear();
            }
            sb += head;
            line(sb, forType != "EUDPlayerLoop" ? "{" : "");
            ++intend;
            if (forType == "EUDLoopPlayer") {
                const std::vector<std::string> v = split(svalue, "\xE1\x97\xA2");
                bool ok = true;
                const bool setcurpl = vbBool(v.back(), ok);
                if (!ok) {
                    sb += tabs(intend);
                    fails(sb, "a player loop block with the value \"" + svalue + "\" (its last part is not True or False)");
                    line(sb);
                } else if (setcurpl) {
                    sb += tabs(intend);
                    line(sb, "setcurpl(" + trim(split(v.front(), "\xE1\x9A\xA2").back()) + ");");
                }
            }
            list(schild, sb, intend, "");
            --intend;
            sb += tabs(intend);
            line(sb, forType != "EUDPlayerLoop" ? "}" : "EUDEndPlayerLoop();");
            break;
        }
        case foraction:
            list(schild, sb, intend, "");
            break;
        case while_:
            sb += tabs(intend) + "while";
            list(schild, sb, intend, "");
            break;
        case whilecondition:
            // EUD Editor 3 calls GetScriptText(schild, strb, intend, True, "") here: the string "" as the Boolean
            // isCondition throws (InvalidCastException), so it writes no file with a while block. This writes the
            // condition as an if block's condition, which is what the block editor shows.
            problem("a while block: EUD Editor 3's build fails on each while block (an error in its code maker), so it never "
                    "wrote this file; the condition is written as for an if block");
            line(sb, "(");
            ++intend;
            list(schild, sb, intend, "", true);
            --intend;
            sb += tabs(intend) + ")";
            break;
        case whileaction:
            line(sb, "{");
            ++intend;
            list(schild, sb, intend, "");
            --intend;
            sb += tabs(intend);
            line(sb, "}");
            break;
        case switch_:
            sb += tabs(intend) + "switch(";
            if (scr.vchild) list({scr.vchild}, sb, intend, "");
            else fails(sb, "a switch block has no value");
            sb += ")";
            line(sb, "{");
            ++intend;
            list(schild, sb, intend, "");
            --intend;
            sb += tabs(intend);
            line(sb, "}");
            break;
        case switchcase:
            sb += tabs(intend) + "case " + svalue;
            line(sb, ":");
            ++intend;
            list(schild, sb, intend, "");
            if (flag) {
                sb += tabs(intend);
                line(sb, "break;");
            }
            --intend;
            break;
        case or_:
        case and_:
            sb += tabs(intend);
            line(sb, "(");
            ++intend;
            list(schild, sb, intend, "", true, stype == and_);
            --intend;
            sb += tabs(intend) + ")";
            andOr();
            line(sb);
            break;
        case folder: {
            const std::vector<std::string> temp = split(svalue, "\xE1\x97\x8B");  // ᗋ
            sb += tabs(intend);
            line(sb, temp.front());
            ++intend;
            list(schild, sb, intend, "", flag);
            --intend;
            sb += tabs(intend);
            line(sb, temp.back());
            break;
        }
        case folderaction:
            list(schild, sb, intend, "", flag);
            break;
        case exp_:
            if (!scr.hasParent) problem("an expression block at the top of the file (EUD Editor 3's build fails on it)");
            if (parentFolder) sb += tabs(intend);
            if (isCondition && !isOneValue) sb += "(";
            list(schild, sb, intend, " ");
            if (isCondition && !isOneValue) sb += ")";
            if (isCondition) andOr();
            else if (parentFolder) sb += ";";
            if (parentFolder) line(sb);
            break;
        case sign:
            sb += svalue;
            break;
        case break_:
            sb += tabs(intend);
            line(sb, "break;");
            break;
        case varuse:
            varUse(scr, sb, intend);
            break;
        case macrofun:
            if (parentFolder) sb += tabs(intend);
            sb += "<?" + sname + "(";
            list(schild, sb, intend, ", ", false, true, false, true);
            sb += ")?>";
            if (isCondition) andOr();
            else {
                if (!scr.hasParent) problem("a macro block at the top of the file (EUD Editor 3's build fails on it)");
                if (parentFolder) sb += ";";
            }
            if (parentFolder) line(sb);
            break;
        case funreturn:
            sb += tabs(intend) + "return ";
            list(schild, sb, intend, "");
            line(sb, ";");
            break;
        default:
            valueOrCall(scr, sb, intend, isCondition, isAnd, isFuncArg, isLastValue);
            break;
        }
        if (isValCover && stype != constVal) sb += "\"";
        if (!isLastValue) sb += spliter;
    }

    // The varuse case of GetItemScriptText: two Select Cases, one on value, one on value2 (both may write).
    void varUse(const Block& scr, std::string& sb, int& intend) {
        const std::string& sname = scr.name;
        const std::string& svalue = scr.value;
        const std::vector<const Block*>& schild = scr.child;
        if (svalue == "constructor") {
            if (sname == "EUDArray") {
                if (schild.size() == 1) {
                    sb += sname + "(";
                    list(schild, sb, intend, "");
                    sb += ")";
                } else if (scr.flag) {
                    sb += "[";
                    list(schild, sb, intend, ", ");
                    sb += "]";
                } else {
                    sb += sname + "(List(";
                    list(schild, sb, intend, ", ");
                    sb += "))";
                }
            } else if (sname == "EUDVArray") {
                if (schild.size() == 1) {
                    sb += sname + "(";
                    list(schild, sb, intend, "");
                    sb += ")()";
                } else if (scr.flag) {
                    sb += "VArray(";
                    list(schild, sb, intend, ", ");
                    sb += ")";
                } else {
                    sb += sname + "(" + std::to_string(schild.size()) + ")(List(";
                    list(schild, sb, intend, ", ");
                    sb += "))";
                }
            } else if (sname == "PVariable") {
                sb += sname + "(";
                if (!schild.empty()) {
                    sb += "list(";
                    list(schild, sb, intend, ", ");
                    sb += ")";
                }
                sb += ")";
            } else {
                sb += sname + "(";
                list(schild, sb, intend, ", ");
                sb += ")";
            }
        } else if (svalue == "cast" || svalue == "alloc") {
            sb += sname + "." + svalue + "(";
            list(schild, sb, intend, "");
            sb += ")";
        } else if (svalue == "!index") {
            sb += sname + "[";
            list(schild, sb, intend, "");
            sb += "]";
        } else if (svalue == "!cp") {
            sb += sname + "[getcurpl()]";
        } else if (svalue == "!default") {
            sb += sname;
        }

        if (scr.value2 == "value") {
            sb += sname;
        } else if (scr.value2 == "method") {
            sb += sname + "." + svalue + "(";
            list(schild, sb, intend, ", ");
            sb += ")";
        } else if (scr.value2 == "fields") {
            sb += sname + "." + svalue;
        }
    }

    // The Case Else of GetItemScriptText: values (constVal), and calls (action, condition, plibfun, funuse, externfun).
    void valueOrCall(const Block& scr, std::string& sb, int& intend, bool isCondition, bool isAnd, bool isFuncArg, bool isLastValue) {
        const int stype = scr.type;
        const std::string& sname = scr.name;
        const std::string& svalue = scr.value;
        const std::vector<const Block*>& schild = scr.child;

        if (isFuncArg) {  // a function's parameter: "name" or "name:Type"
            if (contains(kValueNoneTypes, sname)) sb += sname == "WAVName" ? svalue + ":TrgString" : svalue;
            else sb += svalue + ":" + sname;
            return;
        }
        if (svalue.empty()) {
            if (stype == constVal) {
                if (trim(sname) == "TrgString") sb += "\"\"";
                return;
            }
            const bool isFolder = scr.hasParent && scr.parentFolder;
            if (isFolder) sb += tabs(intend);
            if (sname == "Var" || sname == "SetVar") {
                std::string tsb;
                list(schild, tsb, intend, "\xE2\x96\xA6");  // ▦
                const std::vector<std::string> values = split(tsb, "\xE2\x96\xA6");
                if (values.size() < 3 || schild.size() < 3) {
                    fails(sb, "the " + sname + " block has fewer than 3 values");
                } else if (sname == "Var") {
                    sb += values[0];
                    if (values[1] == "AtMost") sb += "<";
                    else if (values[1] == "AtLeast" || values[1] == "Exactly") sb += ">";
                    else sb += values[1];
                    sb += schild[2]->value;
                    if (values[1] == "AtMost" || values[1] == "AtLeast" || values[1] == "Exactly")
                        problem("the condition Var(" + values[0] + ", " + values[1] + ", " + schild[2]->value + ") is written as " + values[0] +
                                (values[1] == "AtMost" ? "<" : ">") + schild[2]->value +
                                " (EUD Editor 3 writes < and > for AtMost, AtLeast and Exactly); check it");
                } else {
                    sb += values[0];
                    if (values[1] == "SetTo") sb += "==";
                    else if (values[1] == "Add") sb += "+=";
                    else if (values[1] == "Subtract") sb += "-=";
                    else sb += values[1];
                    sb += values[2];
                    if (values[1] == "SetTo")
                        problem("the action SetVar(" + values[0] + ", SetTo, " + values[2] + ") is written as " + values[0] + "==" + values[2] +
                                " (EUD Editor 3 writes == for SetTo, a comparison that changes nothing); check it");
                }
            } else {
                sb += sname + "(";
                list(schild, sb, intend, ", ");
                sb += ")";
            }
            if (isCondition) {
                if (!isLastValue) sb += isAnd ? " && " : " || ";
            } else if (isFolder) {
                sb += ";";
            }
            if (isFolder) line(sb);
            return;
        }

        if (svalue.rfind("defaultvalue;", 0) == 0)
            problem("a value is not set (" + svalue.substr(13) + "): EUD Editor 3 writes \"" + svalue + "\" there");
        const std::string type = trim(sname);
        if (type == "TrgString" || type == "TrgAIScript" || type == "TrgUnit" || type == "TrgLocation" || type == "TrgSwitch" ||
            type == "UnitsDat" || type == "WeaponsDat" || type == "FlingyDat" || type == "SpritesDat" || type == "ImagesDat" ||
            type == "UpgradesDat" || type == "TechdataDat" || type == "OrdersDat" || type == "WAVName") {
            sb += "\"" + svalue + "\"";
        } else if (type == "TrgLocationIndex") {
            // The index of the label in EUD Editor 3's location list (0..254), or 0 when it is not there.
            int v = -1;
            for (size_t i = 0; i < 255 && v < 0; ++i)
                if (locationLabel(i) == svalue) v = int(i);
            if (v < 0) problem("the location \"" + svalue + "\" is not in the map: EUD Editor 3 writes 0 for it");
            sb += std::to_string(v < 0 ? 0 : v);
        } else if (type == "TrgProperty") {
            sb += "UnitProperty(" + svalue + ")";
        } else if (type == "BGM") {
            sb += "<?GetBGMIndex(\"" + svalue + "\")?>";
        } else if (!contains(kArgTypes, type)) {
            sb += svalue;
        } else if (svalue.front() == '"' && svalue.back() == '"') {
            sb += svalue;
        } else {
            sb += "\"" + svalue + "\"";
        }
    }

    const ImportCodeContext& ctx_;
    std::string file_;
    std::vector<std::string>& problems_;
    std::set<std::string> seen_;
    bool tooBig_ = false;
};

// The blocks of the file as a tree. Cycles (a block that is its own child) and very deep trees are cut.
class Loader {
public:
    Loader(const nrbf::Graph& g, Maker& maker) : g_(g), maker_(maker) {}

    const Block* load(const nrbf::Object* o, const Block* parent, bool top, int depth) {
        if (!o || tooMany_) return nullptr;
        if (blocks_.size() >= kMaxBlocks) {
            tooMany_ = true;
            return nullptr;
        }
        if (depth > 400 || path_.count(o->id)) {
            maker_.problem("the blocks refer to themselves or are nested too deep; the rest is left out");
            return nullptr;
        }
        path_.insert(o->id);
        Block& b = blocks_.emplace_back();
        b.type = int(g_.number(o, "ScriptType"));
        b.isfolder = g_.value(o, "isfolder").asBool();
        b.flag = g_.value(o, "flag").asBool();
        b.name = g_.text(o, "name");
        b.value = g_.text(o, "value");
        b.value2 = g_.text(o, "value2");
        if (top) {
            const nrbf::Object* p = g_.get(o, "Parent");
            b.hasParent = p != nullptr;
            b.parentFolder = p && g_.value(p, "isfolder").asBool();
        } else {
            b.hasParent = true;
            b.parentFolder = parent->isfolder;
        }
        b.vchild = load(g_.get(o, "VChild"), &b, false, depth + 1);
        for (const nrbf::Value& v : g_.items(g_.get(o, "child")))
            if (const Block* c = load(g_.get(v), &b, false, depth + 1)) b.child.push_back(c);
        path_.erase(o->id);
        return &b;
    }
    // True when the file has more than kMaxBlocks blocks (each reference to a shared block counts).
    bool tooMany() const { return tooMany_; }

private:
    const nrbf::Graph& g_;
    Maker& maker_;
    std::deque<Block> blocks_;  // (stable addresses)
    std::set<int32_t> path_;
    bool tooMany_ = false;
};

// The macros in the text (<?Name(...)?>), each once, in order.
std::vector<std::string> macroNames(const std::string& code) {
    std::vector<std::string> out;
    for (size_t at = code.find("<?"); at != std::string::npos; at = code.find("<?", at + 2)) {
        size_t i = at + 2;
        while (i < code.size() && (code[i] == ' ' || code[i] == '\t')) ++i;
        size_t e = i;
        while (e < code.size() && (std::isalnum(static_cast<unsigned char>(code[e])) || code[e] == '_' || code[e] == '.')) ++e;
        const std::string name = code.substr(i, e - i);
        bool have = false;
        for (const std::string& n : out) have = have || n == name;
        if (!have) out.push_back(name);
    }
    return out;
}

}  // namespace

bool ee3BlockScriptCode(const nrbf::Graph& g, const nrbf::Object* scripter, const std::string& fileName, bool python, const ImportCodeContext& ctx,
                        std::string& code, std::vector<std::string>& problems) {
    code.clear();
    if (python) {
        problems.push_back("Block script " + fileName +
                           ": a Python block script: EUD Editor 3 has no Python code maker for block scripts (it writes epScript into the "
                           ".py file, which does not run), so the file is not imported");
        return false;
    }
    if (!scripter || !scripter->member("items")) {
        problems.push_back("Block script " + fileName + ": not a block script (no block list)");
        return false;
    }

    Maker maker(ctx, fileName, problems);
    Loader loader(g, maker);
    std::vector<const Block*> items;
    for (const nrbf::Value& v : g.items(g.get(scripter, "items")))
        if (const Block* b = loader.load(g.get(v), nullptr, true, 0)) items.push_back(b);
    if (loader.tooMany()) {
        maker.problem("more than " + std::to_string(kMaxBlocks) + " blocks (the file refers to the same blocks many times); not imported");
        return false;
    }

    // GUIScriptEditor.GetFileText: the msqcvar import, a declaration of each top function, then all the blocks.
    std::string sb = "import PluginVariables as msqcvar;\r\n";
    int intend = 0;
    std::vector<const Block*> functions;
    for (const Block* b : items)
        if (b->type == fundefine) functions.push_back(b);
    maker.list(functions, sb, intend, "PREDEF");
    maker.list(items, sb, intend, "");
    if (maker.tooBig()) {
        maker.problem("the code is longer than " + std::to_string(kMaxText >> 20) + " MB (the file refers to the same blocks many times); not imported");
        return false;
    }

    for (const std::string& m : macroNames(sb))
        maker.problem(m.empty() ? "uses a macro (<? ?>): needs EUD Editor 3's build" : "uses the macro " + m + ": needs EUD Editor 3's build");
    code = std::move(sb);
    return true;
}

}  // namespace stardraft
