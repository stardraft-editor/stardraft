// The Classic triggers page's model (portable, core.inc; classic maps only): the sentence of a condition or an action as
// parts in order (its words, its value chips with the parameter each shows, and the EUD tag), and the variables of the
// blocks that hold an action. The page (classic_triggers.inc) paints the parts and makes the chips clickable; the
// program passes the names and the chip texts (they come from the open map).

// A picker's value that is a block's variable: kCtVariable plus the block's depth (a field that holds it, not a value).
constexpr uint32_t kCtVariable = 0xFFFF0000u;

// One part of a sentence: a run of words, a value chip (`param`: the index of the parameter it shows), or the EUD tag.
enum class CtPart { Word, Chip, Eud };
struct CtSegment {
    CtPart part = CtPart::Word;
    std::wstring text;
    int param = -1;
};

// What the sentence needs from the program: the names of the map's units, locations, strings and switches, and the text
// of a parameter's value in its chip (a choice's name, or "(EUD value)").
struct CtSentenceEnv {
    ClassicNames names;
    std::function<std::wstring(const ClassicParam&, uint32_t)> chipText;
};

// The variable slot k (classicVarField) of the action at `index` holds a variable: its block's depth plus 1, else 0.
int ctVarOf(const ClassicTrigger& t, int index, int k) {
    return index >= 0 && index < t.actionCount() ? t.actions[size_t(index)].var[size_t(k)] : 0;
}
// The blocks that hold the row at `row`, by depth (scope[d] is the block at depth d).
std::vector<const ClassicBatch*> ctScopeOf(const ClassicTrigger& t, int row) {
    std::vector<const ClassicBatch*> chain;
    for (int i = 0; i < row && i < t.actionCount(); ++i) {
        const ClassicAction& a = t.actions[size_t(i)];
        chain.resize(std::min(chain.size(), size_t(a.depth)));
        if (a.type == kClassicBlockType) chain.push_back(a.batch.get());
    }
    if (row >= 0 && row < t.actionCount()) chain.resize(std::min(chain.size(), size_t(t.actions[size_t(row)].depth)));
    return chain;
}
// The variable slot (classicVarField) of a field, or -1.
int ctVarSlotOfField(ClassicField f) {
    for (int k = 0; k < kClassicVarFields; ++k)
        if (classicVarField(k) == f) return k;
    return -1;
}
// The depth of the block whose variable the field of the action at `index` holds, or -1 (a value).
int ctVarDepthAt(const ClassicTrigger& t, int index, ClassicField f) {
    const int slot = ctVarSlotOfField(f);
    const int v = slot < 0 ? 0 : ctVarOf(t, index, slot);
    return v ? v - 1 : -1;
}

// The sentence of a condition or action (`t` and `index` give an action's trigger and row: its blocks' variables; null
// for a condition). Each value is a chip; a sentence that starts with a value starts with a capital.
template <class E>
std::vector<CtSegment> ctSentenceParts(const E& e, int kind, const ClassicTrigger* t, int index, const CtSentenceEnv& env) {
    std::vector<CtSegment> out;
    const ClassicDef* def = kind ? classicActionDef(e.type) : classicConditionDef(e.type);
    auto word = [&out](std::wstring s) {
        if (!s.empty()) out.push_back({CtPart::Word, std::move(s), -1});
    };
    if constexpr (std::is_same_v<E, ClassicAction>) {
        if (e.type == kClassicBlockType) {  // a block: "For each unit in ..." (its chevron is painted by the list)
            word(widen(classicBatchText(*e.batch, env.names)));
            return out;
        }
    }
    std::vector<const ClassicBatch*> scope;
    if constexpr (std::is_same_v<E, ClassicAction>)
        if (kind == 1 && t) scope = ctScopeOf(*t, index);
    if (!def) {
        std::string s;
        if constexpr (std::is_same_v<E, ClassicAction>) s = classicActionText(e, env.names, &scope);
        else s = classicConditionText(e, env.names);
        for (char& ch : s)
            if (static_cast<unsigned char>(ch) < 32) ch = ' ';
        word(widen(s));
        return out;
    }
    const std::string s = def->sentence;
    std::wstring run;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '{') {
            run += wchar_t(static_cast<unsigned char>(s[i]));
            continue;
        }
        const size_t end = s.find('}', i);
        if (end == std::string::npos) break;
        const std::string key = s.substr(i + 1, end - i - 1);
        i = end;
        word(run);
        run.clear();
        for (int pi = 0; pi < int(def->params.size()); ++pi) {
            const ClassicParam& p = def->params[size_t(pi)];
            if (key != p.name) continue;
            const int vd = kind == 1 && t ? ctVarDepthAt(*t, index, p.field) : -1;
            std::wstring v = vd >= 0 && size_t(vd) < scope.size() && scope[size_t(vd)] ? widen(classicBatchVariableText(*scope[size_t(vd)]))
                                                                                       : env.chipText(p, classicValue(e, p.field));
            const bool first = std::none_of(out.begin(), out.end(), [](const CtSegment& g) { return !g.text.empty(); });
            if (first && !v.empty()) v[0] = towupper(v[0]);  // a sentence that starts with a value
            if (!v.empty()) out.push_back({CtPart::Chip, std::move(v), pi});
            break;
        }
    }
    word(run);
    if (classicEud(e)) out.push_back({CtPart::Eud, L"  EUD", -1});
    return out;
}
