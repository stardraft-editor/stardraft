#include "stardraft/import_codegen.h"

// EUD Editor 2's trigger editor to TriggerEditor.eps: the text of TriggerToEPS (TriggerEditorDataMoudle.vb) for the
// element tree of the .e2s's S_TriggerEditorSET, read as Element.LoadFile reads it and written as Element.ToCode and
// Element.GetCode write it (Trigger.vb), with the values as Element.ValueParser makes them. The actions and conditions,
// the .dat and CUnit fields and the helper modules come from EUD Editor 2's own files (data/ee2_triggers.inc, made by
// tools/gen_ee2_triggers.py).
//
// It returns false when the lines are not a trigger editor tree, and also when it could not make all of it (a part
// that cannot be read, or a value where EUD Editor 2 stops its build): `eps` then has what was made, and `problems`
// says what was not, so that the importer can take the old editor's build folder instead.
//
// The lines end in CRLF, as EUD Editor 2's StringBuilder.AppendLine writes them. The text is that of a project with
// the trigger editor ("Struct") on and the BGM player and SCDB off: their parts need data that is not in the trigger
// editor's section (the sound list) or a service that is gone (SCDB), so they are left out, and `problems` says so.
// The project's button sets and stat_txt.tbl text edits come from its other sections (ctx.ee2Sections), the map's
// forces from ctx.playerForces and ctx.playerControls, and the game's stat_txt.tbl from ctx.data. Where that data is
// missing, or an element has a name that this EUD Editor 2 (0.17.8.2) does not know, the text gets an epScript comment
// and `problems` gets a line.

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string_view>

#include "stardraft/dat_extras.h"

namespace stardraft {

namespace {

#include "data/ee2_triggers.inc"

constexpr const char* kNl = "\r\n";
constexpr const char* kSep = "\xE0\xAE\x90";  // EUD Editor 2's value separator (U+0B90)

// ElementType (Trigger.vb)
enum Type {
    Main = 0, If = 1, IfElse = 2, IfCond = 3, Actions = 4, Then = 5, Else = 6, Cond = 7, Act = 8, While = 9, For = 10,
    OldSwitch = 11, WhileCond = 12, WhileDo = 13, ForDo = 14, Functions = 15, FuncDef = 16, FuncArgs = 17, FuncCode = 18,
    FuncCall = 19, Wait = 20, Folder = 21, FolderActions = 22, RawString = 23, RawTrigger = 24, RawTriggers = 25,
    TriggerCond = 26, TriggerAct = 27, Switch = 28, SwitchCase = 29
};

// Thrown where EUD Editor 2's code throws (a text that is not a number, an index out of range).
struct VbError {};

std::string trim(std::string_view s) {
    const char* ws = " \t\r\n\v\f";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string_view::npos) return "";
    return std::string(s.substr(a, s.find_last_not_of(ws) - a + 1));
}

std::vector<std::string> split(const std::string& s, std::string_view sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i; (i = s.find(sep, start)) != std::string::npos; start = i + sep.size()) out.push_back(s.substr(start, i - start));
    out.push_back(s.substr(start));
    return out;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    for (size_t i = s.find(from); i != std::string::npos; i = s.find(from, i + to.size())) s.replace(i, from.size(), to);
    return s;
}

std::string replaceFirst(std::string s, std::string_view from, std::string_view to) {
    const size_t i = s.find(from);
    if (i != std::string::npos) s.replace(i, from.size(), to);
    return s;
}

std::string spaces(int intend) { return intend > 0 ? std::string(size_t(intend) * 4, ' ') : std::string(); }

// Visual Basic's conversion of a text to a number ("12", " 3.5 ", "&H1F"); false when it is not one.
bool vbNumber(std::string_view text, double& out) {
    const std::string s = trim(text);
    if (s.empty()) return false;
    if (s.size() > 2 && s[0] == '&' && (s[1] == 'H' || s[1] == 'h')) {
        uint64_t v = 0;
        const auto r = std::from_chars(s.data() + 2, s.data() + s.size(), v, 16);
        if (r.ec != std::errc() || r.ptr != s.data() + s.size()) return false;
        out = double(v);
        return true;
    }
    // strtod and not from_chars: Apple's libc++ has no from_chars for double before macOS 26 (as src/json.h). The
    // library never calls setlocale, so the decimal point is '.'. strtod also takes what from_chars does not (a space
    // first, "0x", "inf", "nan"): those are refused first.
    const std::string b = s.substr(s[0] == '+' ? 1 : 0);
    if (b.empty() || !(std::isdigit(static_cast<unsigned char>(b[0])) || b[0] == '-' || b[0] == '.') ||
        b.find_first_of("xXiInN") != std::string::npos)
        return false;
    char* stop = nullptr;
    errno = 0;
    out = std::strtod(b.c_str(), &stop);
    return errno != ERANGE && stop == b.c_str() + b.size() && std::isfinite(out);
}

double vbDouble(std::string_view s) {
    double d;
    if (!vbNumber(s, d)) throw VbError{};
    return d;
}

bool fitsInt(double d) { return d >= -2147483648.0 && d <= 2147483647.0; }  // (false for NaN)

// CInt: to the nearest integer, halves to even.
int vbInt(std::string_view s) {
    const double d = std::nearbyint(vbDouble(s));
    if (!fitsInt(d)) throw VbError{};
    return int(d);
}

// A Double as Visual Basic writes it ("4", "4.5").
std::string vbText(double d) {
    if (d == std::floor(d) && std::fabs(d) < 1e15) return std::to_string(int64_t(d));
    // (the shortest text that reads back as the same number, as to_chars writes it: macOS before 13.3 has no to_chars
    // for double)
    char buf[64];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof buf, "%.*g", precision, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    return buf;
}

// CBool of a saved flag ("True", "False", or a number).
bool vbBool(std::string_view text) {
    const std::string s = trim(text);
    if (s.size() == 4 && std::tolower(uint8_t(s[0])) == 't' && std::tolower(uint8_t(s[1])) == 'r' && std::tolower(uint8_t(s[2])) == 'u' &&
        std::tolower(uint8_t(s[3])) == 'e')
        return true;
    if (s.size() == 5 && std::tolower(uint8_t(s[0])) == 'f' && std::tolower(uint8_t(s[1])) == 'a' && std::tolower(uint8_t(s[2])) == 'l' &&
        std::tolower(uint8_t(s[3])) == 's' && std::tolower(uint8_t(s[4])) == 'e')
        return false;
    return vbDouble(s) != 0;
}

// `text = n` as Visual Basic compares a text with a number (it throws when the text is not a number).
bool vbIs(const std::string& text, double n) { return vbDouble(text) == n; }

// Hex(): the number in upper-case hexadecimal (a negative number as 64-bit two's complement).
std::string vbHex(std::string_view s) {
    const double d = std::nearbyint(vbDouble(s));
    if (!(d >= -9.2e18 && d <= 9.2e18)) throw VbError{};
    const int64_t v = int64_t(d);
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, uint64_t(v), 16);
    std::string out(buf, r.ptr);
    for (char& c : out) c = char(std::toupper(uint8_t(c)));
    return out;
}

// The value kinds (ValueDefsModule.LoadValueDef): the output type of each name, and the first name of each kind in
// its order (a function's argument stores the kind as that index).
enum OutType { OtCombobox, OtNumber, OtText, OtCheckList, OtUnitProperty, OtVariable, OtRawString, OtComboboxString, OtListNum, OtUnitBtn, OtBtnData, OtCText };

const char* const kKindNames[] = {
    "RawString", "Number", "Script", "Unit", "WAVName", "Switch", "Location", "DatFile", "OffsetName", "Weapon", "Flingy",
    "Sprite", "Image", "Upgrade", "Techdata", "Order", "Sfxdata", "Portdata", "stat_txt", "PlayerX", "VariableModifier",
    "VariableComparison", "DisplayOption", "AlwaysDisplay", "State", "SState", "ResourceType", "Comparison", "ScoreType",
    "Modifier", "OrderType", "Text", "CText", "ScenarioName", "Properties", "Variable", "UnitBtn", "BtnData", "Status",
    "UnitDirection", "Boolean", "ElevationLevels", "FlingyFlags", "Rank/Sublabelstat_txt", "AIInternal",
    "SpecialAbilityFlags", "UnitSize", "Rightclick", "StareditGroupFlags", "StareditAvailabilityFlags", "TargetType",
    "DamTypes", "Behaviours", "Explosions", "Icon", "FlingyControl", "CircleImage", "GRPfile", "Races", "Animations",
    "StructOffset", "orderState", "orderSignal", "userActionFlags", "UnitMovementState", "statusFlags", "SresourceType",
    "visibilityStatus", "KeyCode", "BGM", "BGMFlag", "BtnEnableTxt", "BtnUnEnableTxt", "CState", "UnitBtnlist",
    "ScoreOffset", "SupplyType", "SCDBMsgType", "SCDBConnectStatus", "DBText", "UTType"};

OutType outType(const std::string& kind) {
    static const std::map<std::string, OutType> kTypes = [] {
        std::map<std::string, OutType> m;
        for (const char* n : {"Number", "Count", "Percent", "NewValue", "Add", "Goal", "Amount", "Time", "Line"}) m[n] = OtNumber;
        for (const char* n : {"Script", "WAVName"}) m[n] = OtComboboxString;
        for (const char* n : {"Unit", "UnitType", "OnUnit", "Switch", "Location", "DestLocation", "StartLocation", "Where", "DatFile",
                              "OffsetName", "Weapon", "Flingy", "Sprite", "Image", "Upgrade", "Techdata", "Order", "Sfxdata", "Portdata",
                              "stat_txt", "Rank/Sublabelstat_txt", "Icon", "CircleImage", "GRPfile", "StructOffset", "KeyCode", "BGM",
                              "UnitBtnlist"})
            m[n] = OtCombobox;
        for (const char* n : {"PlayerX", "Player", "NewOwner", "Owner", "ForPlayer", "VariableModifier", "VariableComparison",
                              "DisplayOption", "AlwaysDisplay", "State", "SState", "ResourceType", "Comparison", "ScoreType", "Modifier",
                              "TimeModifier", "OrderType", "Status", "UnitDirection", "Boolean", "ElevationLevels", "UnitSize",
                              "Rightclick", "TargetType", "DamTypes", "Behaviours", "Explosions", "FlingyControl", "Races", "Animations",
                              "UnitMovementState", "SresourceType", "BGMFlag", "CState", "ScoreOffset", "SupplyType", "SCDBMsgType",
                              "SCDBConnectStatus", "UTType"})
            m[n] = OtListNum;
        for (const char* n : {"FlingyFlags", "AIInternal", "SpecialAbilityFlags", "StareditGroupFlags", "StareditAvailabilityFlags",
                              "orderState", "orderSignal", "userActionFlags", "statusFlags", "visibilityStatus"})
            m[n] = OtCheckList;
        for (const char* n : {"Text", "Label", "ScenarioName", "DBText"}) m[n] = OtText;
        m["CText"] = OtCText;
        m["Properties"] = OtUnitProperty;
        for (const char* n : {"Variable", "UnitPTR", "UnitEPD"}) m[n] = OtVariable;
        for (const char* n : {"UnitBtn", "BtnEnableTxt", "BtnUnEnableTxt"}) m[n] = OtUnitBtn;
        m["BtnData"] = OtBtnData;
        return m;
    }();
    const auto it = kTypes.find(kind);
    return it == kTypes.end() ? OtRawString : it->second;  // (GetDefValueDefs: an unknown name is the first kind, RawString)
}

const char* const kDatBases[] = {"0x65FD00", "0x6564E0", "0x6C9858", "0x665AC0", "0x666778", "0x655700", "0x656198", "0x664A40"};

std::vector<const Ee2DatField*> datFields(int dat) {
    std::vector<const Ee2DatField*> out;
    for (const Ee2DatField& f : kEe2DatFields)
        if (f.dat == dat) out.push_back(&f);
    return out;
}

const Ee2ActCon* findActCon(const Ee2ActCon* list, size_t n, const std::string& name) {
    for (size_t i = 0; i < n; ++i)
        if (name == list[i].name) return &list[i];
    return nullptr;
}

const Ee2ActCon* findAct(const std::string& name) { return findActCon(kEe2Actions, std::size(kEe2Actions), name); }
const Ee2ActCon* findCon(const std::string& name) { return findActCon(kEe2Conditions, std::size(kEe2Conditions), name); }

struct Element {
    int type = Main;
    bool disabled = false, folded = false, notCon = false;
    const Ee2ActCon* ac = nullptr;  // the action (type Act) or condition (type Cond); null: a name this list lacks
    std::string acName;
    std::vector<std::string> defs;  // the ac's value kinds
    std::vector<std::string> values;
    std::vector<std::unique_ptr<Element>> kids;
    Element* parent = nullptr;

    // GetValue: the value of the first value of that kind ("None" when there is none).
    std::string value(const std::string& kind) const {
        for (size_t i = 0; i < defs.size(); ++i)
            if (defs[i] == kind) return i < values.size() ? values[i] : throw VbError{};
        return "None";
    }
    const std::string& v(size_t i) const {
        if (i >= values.size()) throw VbError{};
        return values[i];
    }
};

struct ParseError {};

struct Gen {
    const ImportCodeContext& ctx;
    std::vector<std::string>& problems;
    std::map<std::string, int> said;  // problem -> times
    std::vector<std::string> order;   // (problems in the order they came)
    std::string where;                // the part being written ("onPluginStart", "function Foo")

    std::vector<std::string> lines;
    std::unique_ptr<Element> addText, functions, globalVar, rawTriggers, start, before, after;

    int waitCounter = 0;
    std::string variableName;
    std::string fail;  // set when a value cannot be made (the element becomes a comment)
    std::set<std::string> loading;  // (TEFunction functions being read)
    // Limits for a file that is not what EUD Editor 2 writes: the elements' depth, and the size of an element's code.
    static constexpr int kMaxDepth = 256;
    static constexpr size_t kMaxCode = 1 << 20;
    int depth = 0;
    bool tooDeep = false;
    bool complete = true;  // false: a part could not be read or made
    std::string made;  // the text made so far (triggerToEps)

    // From the project's other sections (ctx.ee2Sections): its button sets (BtnSET: the units with BtnUse, their
    // buttons) and its stat_txt.tbl text edits (FileManagerSET: 0-based index -> text).
    struct ProjectButton {
        int pos = 0, enaStr = 0, disStr = 0;
    };
    std::map<int, std::vector<ProjectButton>> projectButtons;
    std::map<int, std::string> textEdits;
    StatTxt stat;
    bool statTried = false, statLoaded = false;

    // FindSetting: the value of the first line "<key> : <value>" ("" when there is none).
    static std::string setting(const std::vector<std::string>& section, const std::string& key) {
        for (const std::string& l : section)
            if (l.compare(0, key.size() + 1, key + " ") == 0) return l.size() > key.size() + 3 ? l.substr(key.size() + 3) : "";
        return "";
    }

    void readSections() {
        if (!ctx.ee2Sections) return;
        const auto& secs = *ctx.ee2Sections;
        if (const auto it = secs.find("BtnSET"); it != secs.end()) {
            for (int i = 0; i < 250; ++i) {
                if (setting(it->second, "BtnUse" + std::to_string(i)) != "True") continue;
                const auto f = split(setting(it->second, "BtnData" + std::to_string(i)), ",");
                auto& buttons = projectButtons[i];
                for (size_t p = 0; p < (f.size() - 1) / 8; ++p) {
                    ProjectButton b;
                    double d;
                    if (vbNumber(f[p * 8], d) && fitsInt(d)) b.pos = int(d);
                    if (vbNumber(f[p * 8 + 6], d) && fitsInt(d)) b.enaStr = int(d);
                    if (vbNumber(f[p * 8 + 7], d) && fitsInt(d)) b.disStr = int(d);
                    buttons.push_back(b);
                }
            }
        }
        if (const auto it = secs.find("FileManagerSET"); it != secs.end()) {
            double n = 0;
            vbNumber(setting(it->second, "stattextdicCount"), n);
            const int count = n > 0 ? int(std::min(n, double(it->second.size()))) : 0;  // (2 lines each: no more than the lines)
            for (int i = 0; i < count; ++i) {
                double key;
                if (!vbNumber(setting(it->second, "stattextdickey" + std::to_string(i)), key) || !fitsInt(key)) continue;
                // replacetext: "<N>" (N 0-255) is the byte N.
                std::string text = setting(it->second, "stattextdicvalue" + std::to_string(i));
                for (int b = 0; b < 256; ++b) text = replaceAll(text, "<" + std::to_string(b) + ">", std::string(1, char(b)));
                textEdits[int(key)] = text;
            }
        }
    }

    // A button's enabled or disabled text (stat_txt.tbl, 1-based) for a value "<unit>:<button>": the project's button
    // when it changes that unit's buttons, else the game's.
    int buttonString(const std::string& value, bool enabled) const {
        static const ButtonSets kStock;
        const auto parts = split(value, ":");
        if (parts.size() < 2) throw VbError{};
        const int unit = vbInt(parts[0]), index = vbInt(parts[1]);
        if (const auto it = projectButtons.find(unit); it != projectButtons.end()) {
            if (index < 0 || index >= int(it->second.size())) throw VbError{};
            return enabled ? it->second[size_t(index)].enaStr : it->second[size_t(index)].disStr;
        }
        if (unit < 0 || unit >= ButtonSets::kSets || index < 0 || index >= int(kStock.stock(unit).size())) throw VbError{};
        const Button& b = kStock.stock(unit)[size_t(index)];
        return enabled ? b.enabledString : b.disabledString;
    }

    // A stat_txt.tbl text (0-based): the project's edit, else the game's (null when neither can be read).
    const std::string* statText(int i) {
        if (const auto it = textEdits.find(i); it != textEdits.end()) return &it->second;
        if (!statTried) {
            statTried = true;
            std::string error;
            statLoaded = ctx.data && stat.load(*ctx.data, error);
        }
        if (!statLoaded) return nullptr;
        if (i < 0 || i >= stat.count()) throw VbError{};
        return &stat.text(i);
    }

    // GettblLen and GettblStart: the length and the byte place of the index-th run of "#" in a text (and whether the
    // bytes before it are all ASCII). False where EUD Editor 2's byte counters run over 255.
    static bool tblMarks(std::string raw, int index, int& len, int& start, bool& ascii) {
        if (raw.empty() || raw.back() != '\0') raw += '\0';
        size_t i = 0;
        int code = 0, shop = 0;
        len = 0, start = 0;
        while (i < raw.size()) {
            len = 0;
            if (raw[i] == '#') {
                start = code;
                ++i, ++len, ++code;
                while (i < raw.size() && raw[i] == '#') ++len, ++i, ++code;
                if (i > 255) return false;
                ++shop;
            }
            if (shop == index) break;
            if (uint8_t(raw[i]) >= 0x80) ascii = false;
            ++code, ++i;
            if (i > 255) return false;
        }
        return len <= 255 && start <= 255;
    }

    void problem(const std::string& p) {
        const std::string text = (where.empty() ? "" : where + ": ") + p;
        if (said[text]++ == 0) order.push_back(text);
    }

    // --- LoadFile -----------------------------------------------------------------------------------------------------

    // NextLine: the text after the line's first colon.
    std::string nextLine(size_t& index) {
        if (index >= lines.size()) throw ParseError{};
        const std::string& s = lines[index++];
        const size_t c = s.find(':');
        return c == std::string::npos ? s : s.substr(c + 1);
    }

    bool hasFunction(const std::string& name) const {
        for (const auto& f : functions->kids)
            if (!f->values.empty() && f->values[0] == name) return true;
        return false;
    }

    size_t load(Element& e, size_t index, bool first) {
        struct Deeper {
            int& d;
            explicit Deeper(int& depth) : d(++depth) {}
            ~Deeper() { --d; }
        } deeper(depth);
        if (depth > kMaxDepth) {
            tooDeep = true;
            throw ParseError{};
        }
        const std::vector<std::string> flags = split(nextLine(index), ",");
        try {
            e.type = vbInt(flags[0]);
            if (flags.size() > 1) e.disabled = vbBool(flags[1]);
            if (flags.size() > 2) e.folded = vbBool(flags[2]);
            if (flags.size() > 3) e.notCon = vbBool(flags[3]);
        } catch (const VbError&) {
            throw ParseError{};
        }
        bool readValues = false;
        switch (e.type) {
        case Act:
            e.acName = nextLine(index);
            e.ac = findAct(e.acName);
            readValues = true;
            break;
        case Cond:
            e.acName = nextLine(index);
            e.ac = findCon(e.acName);
            readValues = true;
            break;
        case For: case FuncDef: case FuncCall: case IfCond: case WhileCond: case Wait: case Folder: case RawString:
        case RawTrigger: case TriggerCond: case Switch: case SwitchCase:
            readValues = true;
            break;
        default:
            break;
        }
        if (readValues) {
            if (index >= lines.size()) throw ParseError{};
            std::string text = trim(lines[index++]);
            while (true) {
                if (index >= lines.size()) throw ParseError{};
                if (lines[index].find("ElementsCount") != std::string::npos) break;
                text += kNl + trim(lines[index++]);
            }
            e.values = split(text, kSep);
        }

        // A call of a function that the project does not define: EUD Editor 2 adds it from its TEFunction folder.
        if (e.type == FuncCall && !first && !e.values.empty() && !hasFunction(e.values[0]) && !loading.count(e.values[0])) {
            for (const Ee2TextFile& f : kEe2Functions) {
                if (e.values[0] != f.name) continue;
                std::vector<std::string> fileLines = split(f.text, "\n");
                for (std::string& l : fileLines) l = trim(l);
                std::swap(lines, fileLines);
                loading.insert(f.name);
                auto fn = std::make_unique<Element>();
                try {
                    load(*fn, 0, false);  // (its own calls add functions too)
                } catch (const std::bad_alloc&) {
                    throw;
                } catch (...) {
                    fn.reset();
                }
                loading.erase(f.name);
                std::swap(lines, fileLines);
                if (fn) {
                    fn->parent = functions.get();
                    functions->kids.insert(functions->kids.begin(), std::move(fn));
                    problem("function " + std::string(f.name) + " comes from EUD Editor 2's TEFunction folder (the project calls it but does not define it)");
                }
                break;
            }
        }

        // Compatibility (LoadFile): the old upgrade and tech actions and conditions become SetUpgrade, SetTech, Upgrade
        // and Tech.
        if (e.type == Act || e.type == Cond) {
            static const char* const kOldActs[] = {"SetUpgradeResearched", "AddUpgradeResearched", "SetUpgradeAvailable", "AddUpgradeAvailable",
                                                   "SetTechnologiesResearched", "AddTechnologiesResearched", "SetTechnologiesAvailable",
                                                   "AddTechnologiesAvailable"};
            static const char* const kOldCons[] = {"TechnologiesAvailable", "TechnologiesResearched", "UpgradesAvailable", "UpgradesResearched"};
            bool old = false;
            for (const char* n : e.type == Act ? std::vector<const char*>(std::begin(kOldActs), std::end(kOldActs))
                                               : std::vector<const char*>(std::begin(kOldCons), std::end(kOldCons)))
                old = old || e.acName == n;
            if (old && e.values.size() >= (e.type == Act ? 3u : 4u)) {
                const bool upgrade = e.acName.find("Upgrade") != std::string::npos;
                const std::vector<std::string> o = e.values;
                const std::string utType = e.acName.find("Researched") != std::string::npos ? "0" : "1";
                if (e.type == Act) {
                    e.ac = findAct(upgrade ? "SetUpgrade" : "SetTech");
                    e.values = {o[0], o[1], utType, o[2], e.acName.find("Set") != std::string::npos ? "0" : "1"};
                } else {
                    e.ac = findCon(upgrade ? "Upgrade" : "Tech");
                    e.values = {o[0], o[1], utType, o[2], o[3]};
                }
                e.acName = e.ac->name;
            }
            if (e.ac) e.defs = split(e.ac->defs, ",");
        }
        if (e.type == Folder && e.values.size() == 1) {
            e.values[0] = "//" + e.values[0];
            e.values.push_back(e.values[0]);
            e.values.push_back("False");
        }

        std::string countText = nextLine(index);
        int count;
        try {
            count = vbInt(countText);
        } catch (const VbError&) {
            throw ParseError{};
        }
        for (int i = 0; i < count; ++i) {
            auto kid = std::make_unique<Element>();
            kid->parent = &e;
            index = load(*kid, index, first);
            // A function definition whose name the project's functions have already: left out.
            if (kid->type == FuncDef && !kid->values.empty() && hasFunction(kid->values[0])) continue;
            e.kids.push_back(std::move(kid));
        }
        nextLine(index);  // END
        return index;
    }

    // --- ValueParser (isTocode = True) --------------------------------------------------------------------------------

    std::string parse(const Element& e, const std::string& value, const std::string& kind, bool isNumber) {
        std::string r = value;
        if (kind == "PlayerX") {
            if (vbIs(value, 13)) r = "getcurpl()";
        } else if (kind == "VariableModifier") {
            static const char* const k[] = {"=", "+=", "-=", "*=", "/="};
            for (int i = 0; i < 5; ++i)
                if (vbIs(value, i)) r = k[i];
            return r;
        } else if (kind == "VariableComparison") {
            static const char* const k[] = {"==", "!=", ">=", "<=", ">", "<"};
            for (int i = 0; i < 6; ++i)
                if (vbIs(value, i)) r = k[i];
            return r;
        } else if (kind == "AlwaysDisplay") {
            if (vbIs(value, 0)) r = "0";
            else if (vbIs(value, 1)) r = "4";
        } else if (kind == "Comparison") {
            if (vbIs(value, 0)) r = "0";
            else if (vbIs(value, 1)) r = "1";
            else if (vbIs(value, 2)) r = "10";
        } else if (kind == "Modifier" || kind == "TimeModifier") {
            if (vbIs(value, 0)) r = "7";
            else if (vbIs(value, 1)) r = "8";
            else if (vbIs(value, 2)) r = "9";
        } else if (kind == "SState") {
            if (vbIs(value, 0)) r = "4";
            else if (vbIs(value, 1)) r = "5";
            else if (vbIs(value, 2)) r = "6";
            else if (vbIs(value, 3)) r = "11";
        } else if (kind == "State") {
            if (vbIs(value, 0)) r = "4";
            else if (vbIs(value, 1)) r = "5";
            else if (vbIs(value, 2)) r = "6";
        } else if (kind == "CState") {
            if (vbIs(value, 0)) r = "2";
            else if (vbIs(value, 1)) r = "3";
        } else if (kind == "ScoreOffset") {
            static const char* const k[] = {"0x581DE4", "0x581E14", "0x581E44", "0x581E74", "0x581EA4", "0x581ED4", "0x581F04",
                                            "0x581F34", "0x581F64", "0x581F94", "0x581FC4", "0x581FF4", "0x582024", "0x582054",
                                            "0x582084", "0x5820B4", "0x5820E4", "0x582114", "0x5822F4"};
            const int i = vbInt(value);
            if (i < 0 || i >= int(std::size(k))) throw VbError{};
            r = k[i];
        } else if (kind == "DatFile") {
            try {
                const int i = vbInt(value);
                if (i >= 0 && i < 8) r = kDatBases[i];
            } catch (const VbError&) {
            }
            return r;
        } else if (kind == "OffsetName") {
            try {
                const int dat = vbInt(e.value("DatFile"));
                if (dat < 0 || dat >= 8) return r;
                const auto fields = datFields(dat);
                const int i = vbInt(value);
                if (i < 0 || i >= int(fields.size())) return r;
                if (fields[size_t(i)]->offset == 0) {
                    fail = "the .dat field has no StarCraft: Remastered address";
                    return r;
                }
                return std::to_string(int64_t(fields[size_t(i)]->offset) - int64_t(std::stoul(kDatBases[dat] + 2, nullptr, 16)));
            } catch (const VbError&) {
                return r;
            }
        } else if (kind == "StructOffset") {
            try {
                const int i = vbInt(value);
                if (i >= 0 && i < int(std::size(kEe2CUnitFields))) return std::string("0x") + kEe2CUnitFields[i].offset;
            } catch (const VbError&) {
            }
        } else if (kind == "KeyCode") {
            return r;
        } else if (kind == "BtnEnableTxt" || kind == "BtnUnEnableTxt") {
            if (!ctx.ee2Sections) {
                fail = "it needs the project's button set, which is not read here";
                return r;
            }
            try {
                return std::to_string(buttonString(e.value(kind), kind == "BtnEnableTxt"));
            } catch (const VbError&) {
                return r;
            }
        } else if (kind == "UnitBtn") {
            if (!ctx.ee2Sections) {
                fail = "it needs the project's button set (the buttons' order), which is not read here";
                return r;
            }
            try {
                // The unit's button set: EUD Editor 2 takes the place of the button in the project's set sorted by
                // position (0 when the project does not change that unit's buttons).
                const auto parts = split(value, ":");
                if (parts.size() < 2) throw VbError{};
                const int unit = vbInt(parts[0]);
                const int index = vbInt(parts[1]);
                int realPos = 0;
                const auto it = projectButtons.find(unit);
                if (it != projectButtons.end()) {
                    std::vector<int> sort, pos;
                    for (size_t j = 0; j < it->second.size(); ++j) sort.push_back(int(j)), pos.push_back(it->second[j].pos);
                    for (size_t j = 0; j < sort.size(); ++j)
                        for (size_t p = j + 1; p < sort.size(); ++p)
                            if (pos[j] > pos[p]) std::swap(sort[j], sort[p]), std::swap(pos[j], pos[p]);
                    for (size_t j = 0; j < sort.size(); ++j)
                        if (sort[j] == index) {
                            realPos = int(j);
                            break;
                        }
                } else {
                    problem("action SetButton: the project does not change unit " + std::to_string(unit) +
                            "'s buttons, so EUD Editor 2 writes its first button's place (5 * 0)");
                }
                return "epdread_epd(EPD(0x5187EC) + " + std::to_string(3 * unit) + ") + 5 * " + std::to_string(realPos);
            } catch (const VbError&) {
                return r;
            }
        } else if (kind == "Location") {
            if (isNumber) {
                double d;
                if (vbNumber(r, d)) r = vbText(d - 1);
                return r;
            }
        } else if (kind == "CText") {
            return ctextEncode(r);
        } else if (kind == "DBText") {
            return "Db(u2utf8(" + r + "))";
        } else if (kind == "WAVName") {
            return replaceAll(r, "\\", "\\\\");
        }
        if (isNumber) return r;
        switch (outType(kind)) {
        case OtListNum:
        case OtCombobox:
            return "(" + r + ")";
        case OtCheckList:
            try {
                return "0x" + vbHex(r);
            } catch (const VbError&) {
                return r;
            }
        default:
            return r;
        }
    }

    // ValueParser(Valnum, True): where it throws, EUD Editor 2 writes the value's index.
    std::string parse(const Element& e, size_t i) {
        try {
            return parse(e, e.v(i), e.defs.at(i), false);
        } catch (const std::bad_alloc&) {
            throw;
        } catch (...) {
            return std::to_string(i);
        }
    }

    // CTextEncode (CTextEncoder.vb): the codes of a custom text to the values they show. (EUD Editor 2 finds the codes
    // with .NET regular expressions; they are found here by hand, in the same places.)
    static bool ieq(char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); }
    static bool digit(char c) { return c >= '0' && c <= '9'; }

    // The first "{P<d>_<k>_<digits>}" (oneDigit: one digit only) in `s`.
    static bool findCode(const std::string& s, char k, bool oneDigit, std::string& found) {
        for (size_t i = 0; i + 6 < s.size(); ++i) {
            if (s[i] != '{' || !ieq(s[i + 1], 'P') || !digit(s[i + 2]) || s[i + 3] != '_' || !ieq(s[i + 4], k) || s[i + 5] != '_') continue;
            size_t j = i + 6;
            while (j < s.size() && digit(s[j])) ++j;
            if (j == i + 6 || (oneDigit && j != i + 7) || j >= s.size() || s[j] != '}') continue;
            found = s.substr(i, j + 1 - i);
            return true;
        }
        return false;
    }

    // The first "P(<d>)_<k>_(<digits>)" in `s`: its player digit and number.
    static bool findNumbers(const std::string& s, char k, std::string& player, std::string& number) {
        for (size_t i = 0; i + 5 < s.size(); ++i) {
            if (!ieq(s[i], 'P') || !digit(s[i + 1]) || s[i + 2] != '_' || !ieq(s[i + 3], k) || s[i + 4] != '_' || !digit(s[i + 5])) continue;
            size_t j = i + 5;
            while (j < s.size() && digit(s[j])) ++j;
            player = s.substr(i + 1, 1), number = s.substr(i + 5, j - i - 5);
            return true;
        }
        return false;
    }

    // CInt of a run of digits (VbError when it does not fit).
    static int digitsInt(const std::string& d) {
        if (d.empty() || d.size() > 9) throw VbError{};
        return std::stoi(d);
    }

    std::string ctextEncode(const std::string& text) {
        if (text.size() > 65536) {
            fail = "the custom text is longer than 64 KB";
            return text;
        }
        try {
            return ctextEncodeOr(text);
        } catch (const VbError&) {
            fail = "a code in the custom text has a number that does not fit";
        }
        return text;
    }

    std::string ctextEncodeOr(std::string s) {
        const auto player = [](const std::string& d) {
            const int p = digitsInt(d) - 1;
            return p == 8 ? std::string("getcurpl()") : std::to_string(p);
        };
        std::string found, pl, num;
        // {P<n>_D_<unit>}: deaths; {P<n>_K_<unit>}: kills. (EUD Editor 2 reads the numbers of a K code with the D
        // pattern and stops with an error; here it is the kills read that the code is for.)
        for (const char kd : {'D', 'K'}) {
            while (findCode(s, kd, false, found)) {
                if (!findNumbers(s, kd, pl, num)) throw VbError{};
                const std::string base = kd == 'D' ? "" : "EPD(0x5878A4) + ";
                s = replaceFirst(s, found, "\", dwread_epd(" + base + std::to_string(digitsInt(num)) + " * 12 + " + player(pl) + "), \"");
            }
        }
        for (int i = 0; i < 8; ++i) s = replaceAll(s, "{P" + std::to_string(i + 1) + "_N}", "\", tct.str(0x57EEEB + 36 * " + std::to_string(i) + "), \"");
        s = replaceAll(s, "{P9_N}", "\", tct.str(0x57EEEB + 36 * getcurpl()), \"");
        for (int i = 0; i < 8; ++i) s = replaceAll(s, "{P" + std::to_string(i + 1) + "_O}", "\", dwread_epd(EPD(0x57F0F0) + " + std::to_string(i) + "), \"");
        s = replaceAll(s, "{P9_O}", "\", dwread_epd(EPD(0x57F0F0) + getcurpl()), \"");
        for (int i = 0; i < 8; ++i) s = replaceAll(s, "{P" + std::to_string(i + 1) + "_G}", "\", dwread_epd(EPD(0x57F120) + " + std::to_string(i) + "), \"");
        s = replaceAll(s, "{P9_G}", "\", dwread_epd(EPD(0x57F120) + getcurpl()), \"");
        static const char* const kSupply[] = {"0x582144", "0x582174", "0x5821A4", "0x5821D4", "0x582204", "0x582234", "0x582264", "0x582294", "0x5822C4"};
        while (findCode(s, 'S', true, found)) {
            if (!findNumbers(s, 'S', pl, num)) throw VbError{};
            const int index = digitsInt(num);
            const std::string offset = index >= 0 && index < 9 ? kSupply[index] : "";
            s = replaceFirst(s, found, "\", dwread_epd(EPD(" + offset + ") + " + player(pl) + "), \"");
        }
        s = replaceAll(s, "{GT}", "\", dwread_epd(EPD(0x57F23C)) / 24, \"");
        // {C:<name>}: a variable.
        for (size_t i = 0; i + 3 < s.size();) {
            if (s[i] == '{' && ieq(s[i + 1], 'C') && s[i + 2] == ':') {
                const size_t close = s.find('}', i + 3);
                if (close == std::string::npos) break;
                if (close > i + 3) {
                    const std::string name = s.substr(i + 3, close - i - 3), with = "\", " + name + ", \"";
                    s.replace(i, close + 1 - i, with);
                    continue;  // (from the same place: the text before it has no code)
                }
            }
            ++i;
        }
        return s;
    }

    // --- GetCode ------------------------------------------------------------------------------------------------------

    const Element* findFunction(const std::string& name) const {
        for (const auto& f : functions->kids)
            if (!f->values.empty() && f->values[0] == name) return f.get();
        return nullptr;
    }

    // GetFuncDEf: the kind of a function's argument (1-based), "Error" when it has none.
    std::string argKind(const Element& call, size_t index) const {
        try {
            const Element* f = findFunction(call.v(0));
            if (!f || f->kids.empty() || index - 1 >= f->kids[0]->kids.size()) return "Error";
            const int k = vbInt(f->kids[0]->kids[index - 1]->v(1));
            return k >= 0 && k < int(std::size(kKindNames)) ? kKindNames[k] : "Error";
        } catch (const VbError&) {
            return "Error";
        }
    }

    std::string argValue(const Element& call, size_t i) {
        try {
            return parse(call, call.values[i], argKind(call, i), true);
        } catch (const VbError&) {
            return call.values[i];  // (EUD Editor 2 stops with an error here)
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const std::exception&) {
            problem("function " + call.values[0] + ": an argument could not be made; it is written as it is");
            return call.values[i];
        }
    }

    // The size of a CUnit field (from the next field's offset, or its own size), as EUD Editor 2 takes it.
    static int cunitSize(int num, bool ownSize) {
        const int n = int(std::size(kEe2CUnitFields));
        const int num1 = int(std::stoul(kEe2CUnitFields[num].offset, nullptr, 16));
        int num2 = num + 1;
        if (num + 1 < n) num2 = int(std::stoul(kEe2CUnitFields[num + 1].offset, nullptr, 16));
        int size = num2 - num1;
        if (ownSize && kEe2CUnitFields[num].size >= 0) size = kEe2CUnitFields[num].size;
        return size;
    }

    static std::string writeDef(std::string t, int size) {
        if (size == 4) t = replaceAll(t, "$writedef$", "dw");
        else if (size == 2) t = replaceAll(t, "$writedef$", "w");
        else if (size == 1) t = replaceAll(t, "$writedef$", "b");
        return t;
    }

    static std::string part(const std::string& t, size_t i) {
        const auto p = split(t, "|");
        if (i >= p.size()) throw VbError{};
        return p[i];
    }

    std::string valuesText(const Element& e) const {
        std::string t;
        for (size_t i = 0; i < e.values.size(); ++i) t += (i ? ", " : "") + replaceAll(replaceAll(e.values[i], "\r\n", " "), "*/", "* /");
        if (t.size() > 200) t = t.substr(0, 200) + "...";  // (may cut a UTF-8 letter: it is a comment)
        return t;
    }

    // An element that cannot be made: a comment, and a problem.
    std::string comment(const Element& e, const std::string& why) {
        const std::string what = std::string(e.type == Act ? "action " : "condition ") + e.acName;
        problem(what + ": " + why + "; it is a comment in TriggerEditor.eps");
        const std::string text = "EUD Editor 2 " + what + "(" + valuesText(e) + "): " + why;
        return e.type == Act ? "// " + text : "/* " + text + " */";
    }

    std::string actConCode(const Element& e) {
        if (!e.ac) return comment(e, "this EUD Editor 2 (0.17.8.2) has no " + std::string(e.type == Act ? "action" : "condition") + " of that name");
        const std::string name = e.ac->name;
        std::string t = e.ac->code;
        fail.clear();
        if (e.type == Act) {
            if (name == "SetDatfile" || name == "SetVariableDatFile" || name == "AddDatfile") {
                try {
                    const int dat = vbInt(e.value("DatFile"));
                    const int f = vbInt(e.value("OffsetName"));
                    const auto fields = dat >= 0 && dat < 8 ? datFields(dat) : std::vector<const Ee2DatField*>();
                    if (f < 0 || f >= int(fields.size())) throw VbError{};
                    t = writeDef(t, fields[size_t(f)]->size);
                    t = replaceAll(t, "&SIZE&", std::to_string(fields[size_t(f)]->size));
                } catch (const VbError&) {
                    t = replaceAll(t, "$writedef$", "dw");
                }
            } else if (name == "SetCUnitData" || name == "SetVariableCUnitData" || name == "AddCUnitData" || name == "SetCUnitDataEPD" ||
                       name == "SetVariableCUnitDataEPD" || name == "AddCUnitDataEPD") {
                try {
                    const int num = vbInt(e.value("StructOffset"));
                    if (num < 0 || num >= int(std::size(kEe2CUnitFields))) throw VbError{};
                    const int num1 = int(std::stoul(kEe2CUnitFields[num].offset, nullptr, 16));
                    const int size = cunitSize(num, true);
                    t = writeDef(t, size);
                    if (name == "SetCUnitDataEPD" || name == "AddCUnitDataEPD") t = replaceAll(t, "$Byte$", vbText(std::pow(256.0, num1 % 4)));
                    t = part(t, (name == "SetCUnitDataEPD" || name == "SetVariableCUnitDataEPD") && size == 4 ? 1 : 0);
                } catch (const VbError&) {
                    t = replaceAll(t, "$writedef$", "dw");
                }
            } else if (name == "SetButton") {
                try {
                    const std::string stream = e.value("BtnData");
                    size_t index = 0;
                    for (int i = 0; i < 5; ++i) {
                        double value = 0;
                        for (int j = 0; j < 4; ++j) {
                            value += vbDouble(index < stream.size() ? stream.substr(index, 3) : "") * std::pow(256.0, j);
                            index += 3;
                        }
                        if (value < 0 || value > 4294967295.0) throw VbError{};
                        t = replaceAll(t, "Offset" + std::to_string(i + 1), std::to_string(uint32_t(std::nearbyint(value))));
                    }
                } catch (const VbError&) {
                }
            } else if (name == "ChangeStarText" || name == "ChangeButtonEnableMsg" || name == "ChangeButtonUnEnableMsg") {
                // It writes into a stat_txt.tbl text at the place of its index-th run of "#" marks.
                const bool button = name != "ChangeStarText";
                if (button && !ctx.ee2Sections) return comment(e, "it needs the project's button set, which is not read here");
                try {
                    const int statIndex = button ? buttonString(e.value(name == "ChangeButtonEnableMsg" ? "BtnEnableTxt" : "BtnUnEnableTxt"),
                                                                name == "ChangeButtonEnableMsg") - 1
                                                 : int(std::nearbyint(vbDouble(e.v(0)) - 1));
                    const int index = vbInt(e.v(1));
                    const std::string* text = statText(statIndex);
                    if (!text)
                        return comment(e, "it writes into a stat_txt.tbl text at the place of its \"#\" marks, and that text (the game's "
                                          "stat_txt.tbl or the project's text edit) is not read here");
                    int len = 0, place = 0;
                    bool ascii = true;
                    if (index < 0 || index > 255 || !tblMarks(*text, index, len, place, ascii))
                        return comment(e, "the text's \"#\" marks are beyond the 255 bytes that EUD Editor 2 counts");
                    if (!ascii)
                        problem("action " + name + ": the text has letters other than ASCII before its \"#\" marks; the place is "
                                "counted in StarCraft: Remastered's UTF-8 bytes, where EUD Editor 2 counted 2 bytes for each such "
                                "letter (code page 949), so the place differs from EUD Editor 2's");
                    std::string dummy;
                    for (int i = 0; i < len; ++i) dummy += "\\x0D";
                    t = replaceAll(t, "Dummy", "\"" + dummy + "\"");
                    t = replaceAll(t, "Offset", std::to_string(place));
                    t = replaceAll(t, "len", std::to_string(len));
                } catch (const VbError&) {
                }
            } else if (name == "DisplayCText") {
                if (e.values.size() > 1 && e.values[1] == "1")
                    t = std::string("txtPtr = dwread_epd_safe(EPD(0x640B58));") + kNl + t + ";" + kNl + "SetMemory(0x640B58, SetTo, txtPtr);";
            } else if (name == "DisplaySavedCText") {
                if (!e.values.empty() && e.values[0] == "1")
                    t = std::string("txtPtr = dwread_epd_safe(EPD(0x640B58));") + kNl + t + ";" + kNl + "SetMemory(0x640B58, SetTo, txtPtr);";
            } else if (name == "SetUpgrade" || name == "SetTech") {
                const auto isZero = [&](size_t i) {
                    double d;
                    return i < e.values.size() && vbNumber(e.values[i], d) && d == 0;
                };
                try {
                    t = part(t, isZero(4) ? 1 : 0);
                } catch (const VbError&) {
                }
                if (name == "SetUpgrade")
                    t = replaceAll(t, "$Offset$", isZero(2) ? "0x58D2B0 + 0x207C * ($Upgrade$ / 46)" : "0x58D088 + 0x21F0 * ($Upgrade$ / 46)");
                else
                    t = replaceAll(t, "$Offset$", isZero(2) ? "0x58CF44 + 0x21FC * ($Techdata$ / 24)" : "0x58CE24 + 0x222C * ($Techdata$ / 24)");
            }
        } else {
            if (e.notCon) t = "!" + t;
            if (name == "Datfile") {
                try {
                    const int dat = vbInt(e.value("DatFile"));
                    const int f = vbInt(e.value("OffsetName"));
                    const auto fields = dat >= 0 && dat < 8 ? datFields(dat) : std::vector<const Ee2DatField*>();
                    if (f < 0 || f >= int(fields.size())) throw VbError{};
                    t = writeDef(t, fields[size_t(f)]->size);
                    t = replaceAll(t, "&SIZE&", std::to_string(fields[size_t(f)]->size));
                } catch (const VbError&) {
                    t = replaceAll(t, "$writedef$", "dw");
                    t = replaceAll(t, "&SIZE&", "4");
                }
            } else if (name == "CUnitData" || name == "CUnitDataEPD") {
                try {
                    const int num = vbInt(e.value("StructOffset"));
                    if (num < 0 || num >= int(std::size(kEe2CUnitFields))) throw VbError{};
                    const int size = cunitSize(num, false);
                    t = writeDef(t, size);
                    if (name == "CUnitDataEPD" && size == 4) {
                        t = part(t, 1);
                        const int comp = vbInt(e.value("VariableComparison"));
                        static const char* const kNot[] = {"", "!", "", "", "!", "!"};
                        static const char* const kCmp[] = {"Exactly", "Exactly", "AtLeast", "AtMost", "AtMost", "AtLeast"};
                        if (comp >= 0 && comp < 6) {
                            t = replaceAll(t, "$NotFlag$", kNot[comp]);
                            t = replaceAll(t, "$Comparison$", kCmp[comp]);
                        }
                    } else {
                        t = part(t, 0);
                    }
                } catch (const VbError&) {
                    t = replaceAll(t, "$writedef$", "dw");
                }
            } else if (name == "Upgrade" || name == "Tech") {
                double d;
                const bool zero = e.values.size() > 2 && vbNumber(e.values[2], d) && d == 0;
                if (name == "Upgrade")
                    t = replaceAll(t, "$Offset$", zero ? "0x58D2B0 + 0x207C * ($Upgrade$ / 46)" : "0x58D088 + 0x21F0 * ($Upgrade$ / 46)");
                else
                    t = replaceAll(t, "$Offset$", zero ? "0x58CF44 + 0x21FC * ($Techdata$ / 24)" : "0x58CE24 + 0x222C * ($Techdata$ / 24)");
            }
        }

        // Each "$Kind$" to its value, the first place for each value, until none is left.
        // (A value that holds its own "$Kind$" makes EUD Editor 2 loop without end; here the passes and the text's size
        // have a limit.)
        bool left = false;
        for (int pass = 0; pass < 64 && fail.empty(); ++pass) {
            for (size_t i = 0; i < e.defs.size() && fail.empty(); ++i) {
                const std::string place = "$" + e.defs[i] + "$";
                if (t.find(place) == std::string::npos) continue;
                t = replaceFirst(t, place, parse(e, i));
                if (t.size() > kMaxCode) fail = "a value holds its own place (" + place + "), so EUD Editor 2's code grows without end";
            }
            left = false;
            for (const std::string& d : e.defs) left = left || t.find("$" + d + "$") != std::string::npos;
            if (!left) break;
        }
        if (left && fail.empty()) fail = "a value holds its own place, so EUD Editor 2 does not stop making this code";
        if (!fail.empty()) {
            const std::string why = fail;
            fail.clear();
            return comment(e, why);
        }
        // Values that the editor never set (they still hold the name of their kind).
        for (size_t i = 0; i < e.defs.size() && i < e.values.size(); ++i)
            if (e.defs[i] != "None" && e.values[i] == e.defs[i])
                problem(std::string(e.type == Act ? "action " : "condition ") + name + ": its value " + e.defs[i] + " is not set; the code has a placeholder there and does not build");
        return t;
    }

    std::string getCode(Element& e) {
        if (e.type == Act || e.type == Cond) return actConCode(e);
        switch (e.type) {
        case Switch: return "EUDSwitch(" + e.v(0) + ");";
        case SwitchCase: return vbIs(e.v(0), -1) ? "EUDSwitchDefault()();" : "EUDSwitchCase()(" + e.v(0) + ");";
        case Folder: return e.v(0);
        case If: case IfElse: case While: return "";
        case IfCond: case TriggerCond: return "if(";
        case Else: return "else{";
        case Then: return "){";
        case WhileCond: return "while(";
        case WhileDo: return "){";
        case For: {
            const std::string& kind = e.v(0);
            if (kind == "Counting") return "for(var " + e.v(1) + " = 0 ; " + e.v(1) + " < " + e.v(2) + " ; " + e.v(1) + "++){";
            if (kind == "Custom") return "for(" + e.v(1) + "){";
            if (kind == "AllUnit") {
                std::string loop;
                if (vbIs(e.v(1), 0)) loop = "EUDLoopUnit()";
                else if (vbIs(e.v(1), 13)) loop = "lp.EUDLoopUnit2()";
                else loop = "lp.EUDLoopPUnit(" + vbText(vbDouble(e.v(1)) - 1) + ")";
                return "foreach(ptr, epd : " + loop + " ) {";
            }
            if (kind == "PlayerLoop") return "EUDPlayerLoop()();";
            return "For";
        }
        case ForDo: return "";
        case FuncDef: {
            std::string t = "function " + e.v(0) + "(";
            if (!e.kids.empty())
                for (size_t i = 0; i < e.kids[0]->kids.size(); ++i) t += (i ? " ," : "") + e.kids[0]->kids[i]->v(0);
            return t + ") {";
        }
        case RawTrigger: return "function ClassicTrigger(){";
        case TriggerAct: return "if (" + variableName + " == 1) {";
        case FuncCall: {
            const std::string& name = e.v(0);
            const Element* def = findFunction(name);
            if (def && vbBool(def->v(1))) {
                // A function that waits: its arguments go to its per-player variables, its timer to 1.
                std::string t = name + "Timer[getcurpl()] = 1;";
                std::vector<std::string> vars;
                for (size_t i = 1; i < e.values.size(); ++i) {
                    if (def->kids.empty() || i - 1 >= def->kids[0]->kids.size()) throw VbError{};
                    vars.push_back(name + def->kids[0]->kids[i - 1]->v(0));
                    t += kNl + vars.back() + "[getcurpl()] = " + argValue(e, i) + ";";
                }
                t += kNl + name + "(";
                for (size_t i = 0; i < vars.size(); ++i) t += (i ? "," : "") + vars[i] + "[getcurpl()]";
                return t + ")";
            }
            std::string t = name + "(";
            for (size_t i = 1; i < e.values.size(); ++i) t += (i > 1 ? "," : "") + argValue(e, i);
            return t + ")";
        }
        default: {
            static const char* const kNames[] = {"", "If (Conditions) then do (Actions)", "If (Conditions) then do (Actions) else do (Actions)",
                                                 "if : ", "actions : ", "then : ", "else : ", "Con", "Act", "While : ", "For", "Switch",
                                                 "Condition : ", "Action : ", "Action : ", "CustomFuncitons : ", "FuncitonDef : ",
                                                 "Argument : ", "Code : ", "Fuction : ", "Wait : ", "Folder : ", "actions : ", "",
                                                 "ClassciTrigger", "Triggers", "Condition : ", "Action : ", "Switch : ", "Case : "};
            return e.type >= 0 && e.type < int(std::size(kNames)) ? kNames[e.type] : "";
        }
        }
    }

    // --- ToCode -------------------------------------------------------------------------------------------------------

    static bool parentAnd(const Element& e) { return e.parent && !e.parent->values.empty() && e.parent->values[0] == "And"; }

    // The players of a PlayerLoop or a classic trigger: flags 0-7 players, 8-11 forces (12 all players).
    // CHKFORCEDATA: the players of a force (0-3) whose slot is used (OWNR: 3 rescuable, 5 computer, 6 human, 7 neutral).
    std::vector<int> forcePlayers(int force) {
        if (ctx.playerForces.size() < 8 || ctx.playerControls.size() < 8) {
            problem("a part runs for a force, but the map's forces are not read here; that force's players are left out");
            return {};
        }
        std::vector<int> out;
        for (int p = 0; p < 8; ++p) {
            const int c = ctx.playerControls[size_t(p)];
            if (ctx.playerForces[size_t(p)] == force && (c == 3 || c == 5 || c == 6 || c == 7)) out.push_back(p);
        }
        return out;
    }

    std::string toCode(Element& e, int intend, bool isLast = false) {
        std::string sb;
        if (e.disabled) return "";
        if (e.type == RawString) sb += e.values.empty() ? "" : e.values[0];
        bool abled = true;
        const std::string& name = e.acName;
        if (e.type == Act) {
            if (name == "Comment") abled = false;
            if (name == "BGMPlay" || name == "BGMResume" || name == "BGMStop") {
                abled = false;
                problem("action " + name + " left out: EUD Editor 2's BGM player needs the project's sound list and sound files");
            } else if (name.rfind("SCDB:", 0) == 0) {
                abled = false;
                problem("action " + name + " left out: SCDB (an online save service for the original StarCraft) does not work in StarCraft: Remastered");
            }
        } else if (e.type == Cond) {
            if (name == "CurrentBGM" || name == "BGMPlaying") {
                abled = false;
                problem("condition " + name + " left out: EUD Editor 2's BGM player needs the project's sound list and sound files");
            } else if (name.rfind("SCDB:", 0) == 0) {
                abled = false;
                problem("condition " + name + " left out: SCDB (an online save service for the original StarCraft) does not work in StarCraft: Remastered");
            }
        }

        static const int kNoLine[] = {Main, If, IfElse, While, ForDo, Functions, FuncCode, FuncArgs, FolderActions, RawTriggers};
        bool ownLine = true;
        for (int t : kNoLine) ownLine = ownLine && e.type != t;
        if (ownLine && abled) {
            if (e.type == Wait) {
                const Element* pp = e.parent ? e.parent->parent : nullptr;
                if (pp && pp->type == FuncDef && !vbBool(pp->v(1))) return sb;
                const double wait = std::nearbyint(waitCounter + vbDouble(e.v(0)));
                if (!fitsInt(wait)) throw VbError{};
                waitCounter = int(wait);
                sb += spaces(intend - 1) + "}";
                sb += " else if (" + variableName + " == " + std::to_string(waitCounter) + ") {" + kNl;
                return sb;
            }
            if (e.type == FuncDef && vbBool(e.v(1))) {
                waitCounter = 1;
                variableName = e.v(0) + "Timer[getcurpl()]";
            }
            if (e.type == TriggerAct) waitCounter = 1;
            if (e.type == FuncCall) {
                const Element* def = findFunction(e.v(0));
                if (def && vbBool(def->v(1))) {
                    sb += spaces(intend) + "if (" + def->v(0) + "Timer[getcurpl()] == 0) {" + kNl;
                    ++intend;
                }
            }
            if (e.type == TriggerAct) --intend;

            const std::string code = getCode(e);
            if (code.find('\r') != std::string::npos) {
                const auto parts = split(code, "\r");
                sb += spaces(intend) + trim(parts[0]);
                for (size_t i = 1; i < parts.size(); ++i) sb += kNl + spaces(intend) + trim(parts[i]);
            } else {
                sb += spaces(intend) + code;
            }

            if (e.type == FuncDef && vbBool(e.v(1))) {
                ++intend;
                sb += kNl + spaces(intend) + "if (" + variableName + " > 0) {";
                ++intend;
                sb += kNl + spaces(intend) + "if (" + variableName + " == 1) {";
            }
            if (e.type == Act && !sb.empty() && sb.back() != ';') sb += ";";
            if (e.type == Cond && !isLast) sb += parentAnd(e) ? " && " : " || ";
            if (e.type == FuncCall) {
                const int pt = e.parent ? e.parent->type : Main;
                if (pt != WhileCond && pt != IfCond && pt != TriggerCond) sb += ";";
                else if (!isLast) sb += parentAnd(e) ? " && " : " || ";
            }
            sb += kNl;
        }

        if (e.type == For && e.v(0) == "PlayerLoop") {
            ++intend;
            std::string cond;
            for (int p : loopPlayers(e.v(1), 12))
                cond += "getcurpl() == " + std::to_string(p) + " || ";
            if (!cond.empty()) cond.resize(cond.size() - 4);
            sb += spaces(intend) + "if (" + cond + ") {" + kNl;
        }

        ++intend;
        if (e.type == If || e.type == IfElse || e.type == While || e.type == For || e.type == FuncCode) --intend;
        else if (e.type == Folder) intend -= 2;

        if (e.type != FuncArgs) {
            for (size_t i = 0; i < e.kids.size(); ++i) sb += toCode(*e.kids[i], intend, i + 1 == e.kids.size());
            if (sb.size() >= 5 && !e.kids.empty() && trim(sb.substr(sb.size() - 5)) == "&&") sb.erase(sb.size() - 5, 2);
        }

        switch (e.type) {
        case SwitchCase:
            if (vbBool(e.v(1))) sb += spaces(intend - 1) + "EUDBreak();" + kNl;
            break;
        case Switch:
            sb += spaces(intend - 1) + "EUDEndSwitch();" + kNl;
            break;
        case FuncDef:
            if (vbBool(e.v(1))) {
                --intend;
                sb += spaces(intend + 1) + variableName + " = -1;" + kNl;
                sb += spaces(intend) + "}" + kNl;
                --intend;
                sb += spaces(intend + 1) + variableName + " += 1;" + kNl;
                sb += spaces(intend) + "}" + kNl;
            }
            sb += spaces(intend - 1) + "}" + kNl;
            break;
        case TriggerAct: {
            --intend;
            bool preserve = false;
            for (const auto& k : e.kids) preserve = preserve || (k->type == Act && k->acName == "PreserveTrigger");
            if (preserve) sb += spaces(intend + 1) + variableName + " = 0;" + kNl;
            sb += spaces(intend) + "}" + kNl;
            sb += spaces(intend) + "if (" + variableName + " > 0) {" + kNl;
            sb += spaces(intend + 1) + variableName + " += 1;" + kNl;
            sb += spaces(intend) + "}" + kNl;
            break;
        }
        case FuncCall: {
            const Element* def = findFunction(e.v(0));
            if (def && vbBool(def->v(1))) {
                --intend;
                sb += spaces(intend - 1) + "}" + kNl;
            }
            break;
        }
        case Then: case Else: case WhileDo: case RawTrigger:
            sb += spaces(intend - 1) + "}" + kNl;
            break;
        case ForDo:
            if (e.parent && !e.parent->values.empty() && e.parent->values[0] == "PlayerLoop") {
                if (e.parent->v(1) != "0") {
                    sb += spaces(intend - 1) + "}" + kNl;
                    --intend;
                }
                sb += spaces(intend - 1) + "EUDEndPlayerLoop();" + kNl;
            } else {
                sb += spaces(intend - 1) + "}" + kNl;
            }
            break;
        case Folder:
            sb += spaces(intend + 1) + e.v(1) + kNl;
            break;
        default:
            break;
        }
        return sb;
    }

    // The players (0-7) of a list of flags ("True,False,..." for a PlayerLoop: 8 players then 4 forces).
    std::vector<int> loopPlayers(const std::string& flagsText, size_t count) {
        const auto flags = split(flagsText, ",");
        if (flags.size() < count) throw VbError{};
        std::vector<bool> in(8, false);
        for (int i = 0; i < 8; ++i)
            if (vbBool(flags[size_t(i)])) in[size_t(i)] = true;
        for (int i = 8; i < 12; ++i)
            if (vbBool(flags[size_t(i)]))
                for (int p : forcePlayers(i - 8)) in[size_t(p)] = true;
        std::vector<int> out;
        for (int i = 0; i < 8; ++i)
            if (in[size_t(i)]) out.push_back(i);
        return out;
    }

    // --- TriggerToEPS -------------------------------------------------------------------------------------------------

    std::string classicTriggers() {
        std::vector<std::vector<size_t>> players(8);
        const auto add = [&](int p, size_t i) {
            for (size_t t : players[size_t(p)])
                if (t == i) return;
            players[size_t(p)].push_back(i);
        };
        for (size_t i = 0; i < rawTriggers->kids.size(); ++i) {
            const double f = std::nearbyint(vbDouble(rawTriggers->kids[i]->v(0)));
            if (!(f >= 0 && f <= 4294967295.0)) throw VbError{};
            const uint32_t flags = uint32_t(f);
            for (int j = 0; j <= 12; ++j) {
                if (!(flags & (1u << j))) continue;
                if (j < 8) players[size_t(j)].push_back(i);
                else if (j < 12) for (int p : forcePlayers(j - 8)) add(p, i);
                else for (int p = 0; p < 8; ++p) add(p, i);
            }
        }
        std::string sb;
        for (int i = 0; i < 8; ++i) {
            sb += spaces(1) + "//\xED\x94\x8C\xEB\xA0\x88\xEC\x9D\xB4\xEC\x96\xB4 " + std::to_string(i + 1) + kNl;  // ("//player N" in Korean)
            sb += spaces(1) + "if (playerexist(" + std::to_string(i) + ")){" + kNl;
            sb += spaces(2) + "setcurpl(" + std::to_string(i) + ");" + kNl;
            for (size_t t : players[size_t(i)])
                if (!rawTriggers->kids[t]->disabled) sb += spaces(2) + "ClassicTriggerStarter" + std::to_string(t) + "();" + kNl;
            sb += spaces(1) + "}" + kNl;
        }
        return sb;
    }

    std::string waitableTriggers() {
        std::string sb;
        for (const auto& f : functions->kids) {
            if (!vbBool(f->v(1))) continue;
            const std::string& name = f->v(0);
            sb += spaces(1) + name + "(";
            if (!f->kids.empty())
                for (size_t j = 0; j < f->kids[0]->kids.size(); ++j) sb += (j ? ", " : "") + name + f->kids[0]->kids[j]->v(0) + "[getcurpl()]";
            sb += std::string(");") + kNl;
        }
        return sb;
    }

    std::string triggerToEps() {
        made.clear();
        const auto line = [&](const std::string& s) { made += s + kNl; };
        line("import punitloop as lp;");
        line("import tempcustomText as tct;");
        line(std::string(kNl) + "var txtPtr;");
        line("const trgk = $T('Artanis & safhfh');");
        where = "GlobalVar";
        line(toCode(*globalVar, -1));
        where = "AddText";
        line(toCode(*addText, -1));

        where = "functions";
        for (const auto& f : functions->kids) {
            std::string args;
            if (!f->kids.empty())
                for (size_t i = 0; i < f->kids[0]->kids.size(); ++i) args += (i ? ", " : "") + f->kids[0]->kids[i]->v(0);
            if (f->type == FuncDef && vbBool(f->v(1))) {
                waitCounter = 1;
                if (!f->kids.empty())
                    for (const auto& a : f->kids[0]->kids) line("const " + f->v(0) + a->v(0) + " = [0, 0, 0, 0, 0, 0, 0, 0];");
                variableName = f->v(0) + "Timer[getcurpl()]";
                line("const " + f->v(0) + "Timer = [0, 0, 0, 0, 0, 0, 0, 0];");
            }
            line("function " + f->v(0) + "(" + args + ");");
        }
        line(toCode(*functions, -1));

        const size_t n = rawTriggers->kids.size();
        for (size_t i = 0; i < n; ++i) {
            const std::string is = std::to_string(i);
            where = "classic trigger " + std::to_string(i + 1);
            Element& t = *rawTriggers->kids[i];
            variableName = "ClassicTriggerExecTimer" + is + "[getcurpl()]";
            line("const ClassicTriggerExecTimer" + is + " = [0, 0, 0, 0, 0, 0, 0, 0];");
            line("function ClassicTriggerExec" + is + "() {");
            line(t.kids.size() > 1 ? toCode(*t.kids[1], 2) : "");
            line("}");
        }
        for (size_t i = 0; i < n; ++i) {
            const std::string is = std::to_string(i);
            where = "classic trigger " + std::to_string(i + 1);
            Element& t = *rawTriggers->kids[i];
            line("function ClassicTriggerStarter" + is + "() {");
            made += t.kids.empty() ? "" : toCode(*t.kids[0], 1);
            line(spaces(1) + "){");
            line(spaces(2) + "if (ClassicTriggerExecTimer" + is + "[getcurpl()] == 0){");
            line(spaces(3) + "ClassicTriggerExecTimer" + is + "[getcurpl()] = 1;");
            line(spaces(3) + "ClassicTriggerExec" + is + "();");
            line(spaces(2) + "}");
            line(spaces(1) + "}");
            line("}");
        }
        line("function ClassicTriggerExec() {");
        for (size_t i = 0; i < n; ++i) line(spaces(1) + "ClassicTriggerExec" + std::to_string(i) + "();");
        line("}");
        where = "classic triggers";
        line("function ClassicTriggerStarter() {");
        line(classicTriggers() + "}");
        where = "functions";
        line("function WaitableTriggerExec() {");
        line(waitableTriggers() + "}");

        where = "onPluginStart";
        line("function onPluginStart() {");
        line(spaces(1) + "randomize();");
        line(toCode(*start, 0));
        line("}");
        where = "beforeTriggerExec";
        line("function beforeTriggerExec() {");
        line(spaces(1) + "EUDPlayerLoop()();");
        line(spaces(2) + "WaitableTriggerExec();");
        line(spaces(2) + "ClassicTriggerExec();");
        line(spaces(1) + "EUDEndPlayerLoop();");
        line(spaces(1) + "ClassicTriggerStarter();");
        line(toCode(*before, 0) + "}");
        line("function afterTriggerExec() {");
        where = "afterTriggerExec";
        line(toCode(*after, 0) + "}");
        where.clear();
        return made;
    }

    // findSection: the line after the first line that is `key`; npos when there is none.
    size_t section(const std::string& key) const {
        for (size_t i = 0; i < lines.size(); ++i)
            if (trim(lines[i]) == key) return i + 1;
        return std::string::npos;
    }

    // A part that cannot be read (not as EUD Editor 2 writes it): left out, with a problem; the code is not complete.
    void unreadable(const std::string& what) {
        complete = false;
        problem(what + (tooDeep ? " nests elements deeper than " + std::to_string(kMaxDepth) + " levels" : " could not be read") +
                "; it is left out");
        tooDeep = false;
    }

    std::unique_ptr<Element> loadSection(const std::string& key, int emptyType, bool first) {
        const auto empty = [&] {
            auto e = std::make_unique<Element>();
            e->type = emptyType;
            if (emptyType == RawString) e->values.push_back("");
            return e;
        };
        auto e = empty();
        const size_t at = section(key);
        if (at == std::string::npos) return e;
        try {
            load(*e, at, first);
        } catch (const ParseError&) {
            unreadable("the trigger editor's part " + key);
            e = empty();
        } catch (const VbError&) {
            unreadable("the trigger editor's part " + key);
            e = empty();
        }
        return e;
    }
};

}  // namespace

bool ee2TriggerEditorCode(const std::vector<std::string>& lines, const ImportCodeContext& ctx, std::string& eps,
                          std::vector<std::pair<std::string, std::string>>& modules, std::vector<std::string>& problems) {
    eps.clear();
    Gen g{ctx, problems};
    g.lines = lines;
    for (const char* key : {"&functions&", "&GlobalVar&", "&onPluginStart&", "&beforeTriggerExec&", "&afterTriggerExec&"})
        if (g.section(key) == std::string::npos) return false;
    try {
        g.readSections();

        // LoadTriggerFile(datas, True), in its order: the functions first (their calls do not add functions there).
        g.functions = std::make_unique<Element>();
        g.functions->type = Functions;
        g.addText = g.loadSection("&AddText&", RawString, false);
        try {
            g.load(*g.functions, g.section("&functions&"), true);
        } catch (const ParseError&) {
            g.functions = std::make_unique<Element>();
            g.functions->type = Functions;
            g.unreadable("the trigger editor's functions");
        } catch (const VbError&) {
            g.functions = std::make_unique<Element>();
            g.functions->type = Functions;
            g.unreadable("the trigger editor's functions");
        }
        g.globalVar = g.loadSection("&GlobalVar&", Main, false);
        g.rawTriggers = g.loadSection("&RawTriggers&", RawTriggers, false);
        g.start = g.loadSection("&onPluginStart&", Main, false);
        g.before = g.loadSection("&beforeTriggerExec&", Main, false);
        g.after = g.loadSection("&afterTriggerExec&", Main, false);

        try {
            eps = g.triggerToEps();
        } catch (const VbError&) {
            // EUD Editor 2 stops its build here: the text up to this place, and a comment.
            g.problem("a part of the trigger editor has a value that EUD Editor 2 cannot read (it stops its build there); the code stops there");
            eps = g.made + "\r\n// EUD Editor 2 stops its build here: a value that it cannot read\r\n";
            g.complete = false;
        }
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        g.problem("the trigger editor's code could not be made");
        eps = g.made;
        g.complete = false;
    }

    for (const Ee2TextFile& m : kEe2Modules) modules.emplace_back(m.name, replaceAll(m.text, "\n", "\r\n"));
    for (const std::string& p : g.order) {
        const int n = g.said[p];
        problems.push_back(n > 1 ? p + " (" + std::to_string(n) + " times)" : p);
    }
    return g.complete;
}

}  // namespace stardraft
