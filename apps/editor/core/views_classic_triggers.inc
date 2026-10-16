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

// The right-click menus of the page (classic_triggers.inc builds them with the program's menu and runs the command of the
// pick; the same commands as the rows' buttons and the keys). A text's tab is its shortcut. An item with id 0 is a separator.
enum : int { kCtmToggle = 1, kCtmDuplicate, kCtmDelete, kCtmCopy, kCtmPaste, kCtmIn, kCtmOut, kCtmBlock };
struct CtMenuItem {
    int id = 0;
    std::wstring text;
    bool enabled = true;
};
// A trigger's menu: turn on (when it is off) or turn off, Duplicate, Delete, a separator, Copy, Paste (`canPaste`: the
// clipboard holds triggers).
inline std::vector<CtMenuItem> ctTriggerMenuItems(bool off, bool canPaste) {
    return {{kCtmToggle, off ? L"Turn on" : L"Turn off"},
            {kCtmDuplicate, L"Duplicate\tCtrl+D"},
            {kCtmDelete, L"Delete\tDel"},
            {0, L""},
            {kCtmCopy, L"Copy\tCtrl+C"},
            {kCtmPaste, L"Paste\tCtrl+V", canPaste}};
}
// What an entry's menu depends on: a condition or an action, a block (its edit replaces the turn off), the turn off state,
// the moves (actions only), and whether the clipboard holds this kind of entry.
struct CtEntryMenuState {
    bool isAction = false;
    bool block = false;
    bool off = false;
    bool canMoveOut = false;
    bool canMoveIn = false;
    bool canPaste = false;
};
inline std::vector<CtMenuItem> ctEntryMenuItems(const CtEntryMenuState& s) {
    std::vector<CtMenuItem> items;
    if (s.block) items.push_back({kCtmBlock, L"Edit the block\x2026"});
    else items.push_back({kCtmToggle, s.off ? L"Turn on" : L"Turn off"});
    if (s.isAction) {
        items.push_back({kCtmOut, L"Move out\tCtrl+Left", s.canMoveOut});
        items.push_back({kCtmIn, L"Move in\tCtrl+Right", s.canMoveIn});
    }
    items.push_back({kCtmDuplicate, L"Duplicate\tCtrl+D"});
    items.push_back({kCtmDelete, L"Delete\tDel"});
    items.push_back({0, L""});
    items.push_back({kCtmCopy, L"Copy\tCtrl+C"});
    items.push_back({kCtmPaste, L"Paste\tCtrl+V", s.canPaste});
    return items;
}

// The list's model (the Triggers page on both systems): a trigger's line, its search text and the rows a search shows.
// `n` gives the names of the map (the page passes its own; the same text on Windows and SDL).

// A trigger's line in the list: its conditions, then (dim) its actions (`actions`: the actions' line).
inline std::wstring ctTriggerLineOf(const ClassicTrigger& t, bool actions, const ClassicNames& n) {
    std::string s;
    const int count = actions ? t.actionCount() : t.conditionCount();
    for (int i = 0; i < count && s.size() < 300; ++i) {
        if (actions) {
            const ClassicAction& a = t.actions[size_t(i)];
            const std::vector<const ClassicBatch*> scope = ctScopeOf(t, i);
            s += (s.empty() ? "" : " ") + (a.type == kClassicBlockType ? classicBatchText(*a.batch, n) : classicActionText(a, n, &scope));
        } else {
            s += (s.empty() ? "" : " ") + classicConditionText(t.conditions[size_t(i)], n);
        }
    }
    if (s.empty()) s = actions ? "No actions" : "No conditions";
    for (char& c : s)
        if (c == '\r' || c == '\n' || (static_cast<unsigned char>(c) < 32 && c != '\t')) c = ' ';
    return widen(s);
}

// The text a search matches, in lower case: the trigger's sentences (its conditions and actions, names of units,
// locations and switches) and the names of its players.
inline std::wstring ctSearchTextOf(const ClassicTrigger& t, const ClassicNames& n) {
    std::wstring s = ctTriggerLineOf(t, false, n) + L" " + ctTriggerLineOf(t, true, n);
    for (int g = 0; g < kClassicGroups; ++g)
        if (t.players[size_t(g)]) s += L" " + widen(classicPlayerName(uint32_t(g), n));
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

// The words of a search (lower case, split at every wide space, as the stream's >> did).
inline std::vector<std::wstring> ctSearchWords(std::wstring q) {
    std::transform(q.begin(), q.end(), q.begin(), ::towlower);
    std::vector<std::wstring> words;
    std::wstring w;
    for (wchar_t c : q) {
        if (iswspace(c)) {
            if (!w.empty()) words.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) words.push_back(w);
    return words;
}

// The rows of the list: the indices of the triggers that run for `filter` (-1: every player) and have every word of
// the query, in the map's order.
inline std::vector<int> ctShownTriggers(const std::vector<ClassicTrigger>& triggers, int filter, const std::wstring& query,
                                        const ClassicNames& n) {
    const std::vector<std::wstring> words = ctSearchWords(query);
    std::vector<int> shown;
    for (int i = 0; i < int(triggers.size()); ++i) {
        const ClassicTrigger& t = triggers[size_t(i)];
        if (filter >= 0 && !t.runsFor(filter)) continue;
        if (!words.empty()) {
            const std::wstring hay = ctSearchTextOf(t, n);
            if (!std::all_of(words.begin(), words.end(), [&](const std::wstring& w) { return hay.find(w) != std::wstring::npos; })) continue;
        }
        shown.push_back(i);
    }
    return shown;
}

// The number of parts a trigger makes in the map: 1, or more when its blocks make more than 64 slots.
inline int ctPartsOf(const ClassicTrigger& t) {
    bool blocks = false;
    for (int i = 0; i < t.actionCount() && !blocks; ++i) blocks = t.actions[size_t(i)].type == kClassicBlockType;
    if (!blocks) return 1;
    ClassicTriggers one;
    one.triggers.push_back(t);
    StringTable scratch;
    return int(classicExpandBatches(one, scratch).triggers.size());
}

// Whether the action at `i` can move into the block before it (`in`) or out of its block: a trial on a copy.
inline bool ctCanMoveRow(const ClassicTrigger& t, int i, bool in) {
    if (i < 0 || i >= t.actionCount()) return false;
    ClassicTrigger trial = t;
    return in ? trial.indentAction(i) : trial.outdentAction(i);
}

// --- The page's model (both programs): the triggers as the page edits them, the list, and the trigger-level commands ---
// The groups of the filter: players 1-8, all players, forces 1-4.
constexpr int kCtGroups[] = {0, 1, 2, 3, 4, 5, 6, 7, 17, 18, 19, 20, 21};
constexpr int kCtGroupCount = int(std::size(kCtGroups));
// The players with a check box in the row (players 1-8, all players); the forces are in a multi-pick list (Forces).
constexpr int kCtBoxGroups[] = {0, 1, 2, 3, 4, 5, 6, 7, 17};
constexpr int kCtBoxCount = int(std::size(kCtBoxGroups));
constexpr int kCtForceGroups[] = {18, 19, 20, 21};

// The page's working copy of the map's triggers, the selection and the list. The Windows page and the SDL page each hold
// one; the commands below change it, and the host says what the program shows (the list, the header, the map).
struct CtModel {
    ClassicTriggers trig;
    StringTable strings;
    std::set<uint32_t> ownStrings;          // strings this page made (a text typed again changes them in place)
    std::vector<ClassicAiScript> ai;
    std::vector<ClassicTrigger> before;     // the map's triggers when the page read them (a change is a difference from these)
    std::vector<int> shown;                 // the triggers in the list
    std::wstring query;                     // the search box's words: a trigger shows when its text has them all
    int filter = -1;                        // the group whose triggers show (-1: all)
    int sel = -1;                           // the selected trigger
    int kind = 0, entry = -1;               // the selected condition (kind 0) or action (1)
    int anchor = -1;                        // the trigger a Shift click reaches from
    std::set<int> marked;                   // the triggers chosen with Ctrl or Shift click (the list's actions take them all)
    std::set<int> collapsed;                // the action rows of collapsed blocks (by row; cleared when the trigger changes)
    int hold = 0;                           // > 0: the changes of a batch (a paste of several triggers) go into the map once, at the end
};
// What the model's commands ask of the program: the names, the list's repaint, the selection (its entries and head follow),
// the header, the change into the map, the messages, and the keys.
class CtModelHost {
public:
    virtual ~CtModelHost() = default;
    virtual ClassicNames names() = 0;
    virtual void filled() = 0;               // the shown triggers changed: the count and the list's repaint
    virtual void selectTrigger(int i) = 0;   // the trigger i is selected (-1: none): the entries, the players and the header follow
    virtual void updateHeader() = 0;
    virtual void changed() = 0;              // a change: it goes into the map
    virtual void invalidate() = 0;           // the lists are painted again
    virtual void message(const std::wstring& text) = 0;
    virtual bool ctrl() = 0;
    virtual bool shift() = 0;
};

inline const ClassicTrigger* ctModelSelected(const CtModel& m) {
    return m.sel >= 0 && m.sel < int(m.trig.triggers.size()) ? &m.trig.triggers[size_t(m.sel)] : nullptr;
}
inline ClassicTrigger* ctModelSelectedMut(CtModel& m) {
    return m.sel >= 0 && m.sel < int(m.trig.triggers.size()) ? &m.trig.triggers[size_t(m.sel)] : nullptr;
}

// The map's triggers as the page reads them (a batch is its template in the list).
inline void ctModelLoad(CtModel& m, Editor& ed) {
    m.strings = ed.strings();
    m.trig = classicCollapseBatches(ed.classicTriggers(), m.strings);
    m.before = m.trig.triggers;
}

// The list's rows: the triggers that run for the player shown and have every word of the query, in the map's order.
inline void ctModelFill(CtModel& m, CtModelHost& h) {
    m.shown = ctShownTriggers(m.trig.triggers, m.filter, m.query, h.names());
    m.marked.clear();  // (the rows changed: the checked ones are not known any more)
    h.filled();
}

// A trigger goes in at the place after the selected one (or at the end), and is selected.
inline void ctModelInsertTrigger(CtModel& m, CtModelHost& h, ClassicTrigger n) {
    const int at = ctModelSelected(m) ? m.sel + 1 : int(m.trig.triggers.size());
    m.trig.triggers.insert(m.trig.triggers.begin() + at, std::move(n));
    m.sel = at;
    ctModelFill(m, h);
    h.selectTrigger(at);
}
// A new trigger (Always, no actions) after the selected one, for the player shown.
inline void ctModelAddTrigger(CtModel& m, CtModelHost& h) {
    ClassicTrigger n = ClassicTrigger::make();
    if (m.filter >= 0) n.players[size_t(m.filter)] = 1;
    ctModelInsertTrigger(m, h, n);
}
// A copy of a trigger after it. A copy has not run: the flags that the game sets while it runs are cleared (the
// copy keeps "preserve trigger" and "disabled").
inline ClassicTrigger ctModelFreshCopy(CtModel& m, ClassicTrigger t) {
    t.flags &= 4u | 8u, t.currentAction = 0;
    for (const ClassicAction& a : t.actions) m.ownStrings.erase(a.text);  // its texts are shared now
    return t;
}
inline void ctModelDuplicateTrigger(CtModel& m, CtModelHost& h, int i) {
    if (i < 0 || i >= int(m.trig.triggers.size())) return;
    m.sel = i;
    ctModelInsertTrigger(m, h, ctModelFreshCopy(m, m.trig.triggers[size_t(i)]));
}
inline void ctModelDeleteTrigger(CtModel& m, CtModelHost& h, int i) {
    if (i < 0 || i >= int(m.trig.triggers.size())) return;
    m.trig.triggers.erase(m.trig.triggers.begin() + i);
    m.sel = -1;
    ctModelFill(m, h);
    int next = -1;  // the trigger that was after it in the list, else the one before
    for (int k : m.shown)
        if (next < 0 || k < i) next = k;
    for (int k : m.shown)
        if (k >= i) {
            next = k;
            break;
        }
    h.selectTrigger(next);
}
// Moves the trigger at place `from` of the list to place `to` of the list without it. With a player's triggers
// shown, it goes just before (after) the trigger it now has after (before) it in the list; the triggers that do not
// show keep their places.
inline void ctModelMoveShown(CtModel& m, CtModelHost& h, int from, int to) {
    if (from < 0 || from >= int(m.shown.size())) return;
    const int src = m.shown[size_t(from)];
    std::vector<int> rest = m.shown;
    rest.erase(rest.begin() + from);
    if (rest.empty() || to < 0) return;
    to = std::min(to, int(rest.size()));
    const int dest = to < int(rest.size()) ? rest[size_t(to)] - (rest[size_t(to)] > src) : rest.back() - (rest.back() > src) + 1;
    auto& v = m.trig.triggers;
    ClassicTrigger t = v[size_t(src)];
    v.erase(v.begin() + src);
    v.insert(v.begin() + dest, std::move(t));
    m.sel = dest;
    ctModelFill(m, h);
    h.selectTrigger(dest);
}

// The triggers an action takes: the checked ones (Ctrl or Shift click), else the selected one. In the map's order.
inline std::vector<int> ctModelTargets(const CtModel& m) {
    std::vector<int> out(m.marked.begin(), m.marked.end());
    if (out.empty() && m.sel >= 0) out.push_back(m.sel);
    return out;
}
// A click on a trigger's row: a plain click selects it; Ctrl checks or unchecks it; Shift checks the rows of the list
// from the anchor to it.
inline void ctModelClickTrigger(CtModel& m, CtModelHost& h, int i) {
    const bool ctrl = h.ctrl(), shift = h.shift();
    const auto at = std::find(m.shown.begin(), m.shown.end(), i);
    if (shift && m.anchor >= 0 && at != m.shown.end()) {
        const auto from = std::find(m.shown.begin(), m.shown.end(), m.anchor);
        if (from != m.shown.end()) {
            m.marked.clear();
            for (auto k = std::min(from, at); k <= std::max(from, at); ++k) m.marked.insert(*k);
        }
        h.selectTrigger(i);
    } else if (ctrl) {
        if (!m.marked.erase(i)) m.marked.insert(i);
        m.anchor = i;
        h.selectTrigger(i);
    } else {
        m.marked.clear(), m.anchor = i;
        if (i != m.sel) h.selectTrigger(i);
    }
    h.invalidate();
}
// Turn off (or on) the triggers that ctModelTargets gives: all off when the first one is on, else all on.
inline void ctModelToggleTriggers(CtModel& m, CtModelHost& h) {
    const std::vector<int> targets = ctModelTargets(m);
    if (targets.empty()) return;
    // The button shows the selected trigger's state: that decides, when it is one of the targets.
    const int shown = std::find(targets.begin(), targets.end(), m.sel) != targets.end() ? m.sel : targets[0];
    const bool off = !m.trig.triggers[size_t(shown)].disabled();
    for (int i : targets) m.trig.triggers[size_t(i)].setDisabled(off);
    h.updateHeader();
    h.changed();
    h.message(off ? (targets.size() > 1 ? L"Turned off the triggers" : L"Turned off the trigger") : (targets.size() > 1 ? L"Turned on the triggers" : L"Turned on the trigger"));  // (after the change: its own message)
}
// Turn off (or on) one trigger.
inline void ctModelToggleOne(CtModel& m, CtModelHost& h, int i) {
    if (i < 0 || i >= int(m.trig.triggers.size())) return;
    ClassicTrigger& t = m.trig.triggers[size_t(i)];
    t.setDisabled(!t.disabled());
    h.updateHeader();
    h.changed();
}
// Delete the triggers that ctModelTargets gives; the trigger after the first one deleted is selected.
inline void ctModelDeleteTriggers(CtModel& m, CtModelHost& h) {
    std::vector<int> targets = ctModelTargets(m);
    if (targets.empty()) return;
    const int first = *std::min_element(targets.begin(), targets.end());
    std::sort(targets.begin(), targets.end(), std::greater<int>());
    for (int i : targets) m.trig.triggers.erase(m.trig.triggers.begin() + i);
    m.marked.clear(), m.sel = -1;
    ctModelFill(m, h);
    int next = -1;  // the first trigger shown from the deleted place on, else the last one shown
    for (int k : m.shown) {
        next = k;
        if (k >= first) break;
    }
    h.selectTrigger(next);
    h.message(targets.size() > 1 ? L"Deleted the triggers" : L"Deleted the trigger");
}
// Duplicate the triggers that ctModelTargets gives: each copy goes after the last one (in the list's order).
inline void ctModelDuplicateTriggers(CtModel& m, CtModelHost& h) {
    const std::vector<int> targets = ctModelTargets(m);
    if (targets.empty()) return;
    const int after = *std::max_element(targets.begin(), targets.end());
    int at = after + 1;
    for (int i : targets) {
        const ClassicTrigger copy = ctModelFreshCopy(m, m.trig.triggers[size_t(i)]);
        m.trig.triggers.insert(m.trig.triggers.begin() + at++, copy);
    }
    m.marked.clear();
    m.sel = at - 1;
    ctModelFill(m, h);
    h.selectTrigger(at - 1);
    h.message(targets.size() > 1 ? L"Duplicated the triggers" : L"Duplicated the trigger");
}
// A player's check box of the head: the selected trigger runs (or not) for that player; the list follows.
inline void ctModelPlayerBox(CtModel& m, CtModelHost& h, int groupIndex, bool on) {
    ClassicTrigger* t = ctModelSelectedMut(m);
    if (!t) return;
    t->players[size_t(kCtBoxGroups[groupIndex])] = on;
    ctModelFill(m, h);  // it can leave (or join) the list of the player shown
}
// Forces: the forces the selected trigger runs for are the checked ones (the groups as their numbers).
inline void ctModelSetForces(CtModel& m, CtModelHost& h, const std::set<std::string>& checked) {
    ClassicTrigger* t = ctModelSelectedMut(m);
    if (!t) return;
    for (int g : kCtForceGroups) t->players[size_t(g)] = checked.count(std::to_string(g)) ? 1 : 0;
    ctModelFill(m, h);
    h.changed();
}
// Forces' multi-pick: the forces by their names (the values are the groups' numbers), and in `checked` the ones the
// trigger runs for.
inline std::vector<ListPickItem> ctForceItems(const ClassicTrigger& t, const ClassicNames& n, std::set<std::string>& checked) {
    std::vector<ListPickItem> items;
    for (int g : kCtForceGroups) {
        ListPickItem it;
        it.label = widen(classicPlayerName(uint32_t(g), n)), it.value = std::to_string(g);
        items.push_back(std::move(it));
        if (t.runsFor(g)) checked.insert(std::to_string(g));
    }
    return items;
}
// The change's check: a trigger differs from the map's when the page read them.
inline bool ctModelChanged(const CtModel& m) { return m.trig.triggers != m.before; }

// Apply: TRIG (and the new strings) as one undo step (Editor::setClassicTriggers). The values the page does not know
// (EUD values, the bytes after the last trigger, the flags) stay as they are. The result: kCtApplyRefused (the map is not
// changed), kCtApplyUnchanged (the map has these triggers already) or kCtApplySet; `message` says what happened.
constexpr int kCtApplyRefused = 0, kCtApplyUnchanged = 1, kCtApplySet = 2;
inline int ctModelApply(CtModel& m, Editor& ed, std::wstring& message) {
    if (!classicCanExpand(m.trig)) {  // a long trigger needs a switch that no trigger uses
        message = L"Could not set the classic triggers. A long trigger needs a switch that no trigger uses.";
        return kCtApplyRefused;
    }
    // The strings this page made that no action uses now go again.
    for (uint32_t id : m.ownStrings) {
        bool used = false;
        for (const ClassicTrigger& t : m.trig.triggers)
            for (int i = 0; i < t.actionCount() && !used; ++i) used = t.actions[size_t(i)].text == id;
        if (!used) m.strings.set(id, "");
    }
    const ClassicTriggers expanded = classicExpandBatches(m.trig, m.strings);  // the batches' triggers, as the game reads them
    if (!ed.setClassicTriggers(expanded, &m.strings)) {
        message = L"Classic triggers: no change";
        return kCtApplyUnchanged;
    }
    message = L"Set the classic triggers";
    return kCtApplySet;
}

// The page's pure helpers (both programs): which parameters have a list, whether a value is one of the choices, the
// names of the kinds and the text of a chip.
inline bool ctPickerIsList(ClassicKind k) {
    switch (k) {
    case ClassicKind::Number:
    case ClassicKind::Milliseconds:
    case ClassicKind::Seconds:
    case ClassicKind::Count:
    case ClassicKind::Percent:
    case ClassicKind::UnitProperties:
    case ClassicKind::Text: return false;
    default: return true;
    }
}

inline bool ctIsChoiceOf(Editor& ed, const std::vector<ClassicAiScript>& ai, ClassicKind k, uint32_t v) {
    switch (k) {
    case ClassicKind::Unit: return v <= 232;
    case ClassicKind::Location: {
        const int i = int(v) - 1;
        return v >= 1 && i < ed.locationCount() && (ed.locationUsed(i) || i == kAnywhereLocation);
    }
    case ClassicKind::Switch: return v < 256;
    case ClassicKind::Sound: {
        if (v == 0) return true;
        const auto s = ed.soundStrings();
        return std::find(s.begin(), s.end(), v) != s.end();
    }
    case ClassicKind::AiScript: return std::any_of(ai.begin(), ai.end(), [&](const ClassicAiScript& s) { return s.id == v; });
    default: {
        const auto& cs = classicChoices(k);
        return std::any_of(cs.begin(), cs.end(), [&](const ClassicChoice& c) { return c.value == v; });
    }
    }
}

inline const wchar_t* ctKindLabel(ClassicKind k, const char* name) {
    const std::string n = name;
    if (n == "to") return L"To player";
    if (n == "dest") return L"Destination";
    if (n == "duration") return L"Duration (ms)";
    switch (k) {
    case ClassicKind::Player: return L"Player";
    case ClassicKind::Comparison: return L"Comparison";
    case ClassicKind::Number: return L"Number";
    case ClassicKind::Unit: return L"Unit";
    case ClassicKind::Location: return L"Location";
    case ClassicKind::Resource: return L"Resource";
    case ClassicKind::Score: return L"Score";
    case ClassicKind::Switch: return L"Switch";
    case ClassicKind::SwitchState: return L"State";
    case ClassicKind::SwitchAction: return L"Action";
    case ClassicKind::Modifier: return L"Modifier";
    case ClassicKind::Text: return L"Text";
    case ClassicKind::Sound: return L"Sound";
    case ClassicKind::Milliseconds: return L"Time (ms)";
    case ClassicKind::Seconds: return L"Seconds";
    case ClassicKind::Count: return L"Count (0: all)";
    case ClassicKind::AiScript: return L"AI script";
    case ClassicKind::Order: return L"Order";
    case ClassicKind::Alliance: return L"Status";
    case ClassicKind::State: return L"State";
    case ClassicKind::UnitProperties: return L"Properties";
    case ClassicKind::Percent: return L"Percent";
    }
    return L"";
}

inline std::wstring ctChipTextOf(const ClassicParam& p, uint32_t v, const ClassicNames& n, bool choice) {
    std::wstring s = widen(classicValueText(p.kind, v, n));
    for (wchar_t& ch : s)
        if (ch < 32) ch = L' ';
    if (s.size() > 48) s = s.substr(0, 47) + L"\x2026";
    if (ctPickerIsList(p.kind) && !choice) s += L" (EUD value)";
    return s;
}

// --- The painted lists -------------------------------------------------------------------------------------------------
// The two lists of the page: the triggers (the list) and the conditions and actions (the entries). The page's state and
// its commands are the host's (classic_triggers.inc); the paint, the click areas, a row dragged by its grip, the wheel and
// the scroll bar are here, on the Canvas. A list's window is a Win32Host (or any host) of its view.

// The program's part of the lists.
class CtListHost {
public:
    virtual ~CtListHost() = default;
    virtual const std::vector<ClassicTrigger>& triggers() const = 0;
    virtual const std::vector<int>& shown() const = 0;  // the triggers in the list, in order
    virtual const ClassicTrigger* selectedTrigger() const = 0;
    virtual int selectedIndex() const = 0;
    virtual bool marked(int i) const = 0;  // checked with Ctrl or Shift click
    virtual int entryKind() const = 0;     // the selected entry: 0 condition, 1 action
    virtual int entryIndex() const = 0;    // (-1: none)
    virtual bool collapsed(int i) const = 0;
    virtual ClassicNames names() const = 0;
    virtual std::wstring chipText(const ClassicParam& p, uint32_t v) = 0;
    virtual std::wstring chipTip(const ClassicParam& p) = 0;
    // Commands (the same as the rows' buttons, the keys and the menus).
    virtual void clickTrigger(int i) = 0;
    virtual void toggleTrigger(int i) = 0;
    virtual void duplicateTrigger(int i) = 0;
    virtual void deleteTrigger(int i) = 0;
    virtual void moveShown(int from, int to) = 0;
    virtual void addTrigger() = 0;
    virtual void triggerMenu(int i, draw::Point at) = 0;  // `at`: in the list's coordinates
    virtual void selectEntry(int kind, int i) = 0;
    virtual void toggleEntry(int kind, int i) = 0;
    virtual void duplicateEntry(int kind, int i) = 0;
    virtual void deleteEntry(int kind, int i) = 0;
    virtual void moveEntry(int kind, int from, int to) = 0;
    virtual void moveRow(int i, bool in) = 0;
    virtual void editBlock(int i) = 0;  // selects the block's action, then edits its block
    virtual void entryMenu(int kind, int i, draw::Point at) = 0;
    virtual void addClick(int kind, draw::Rect link) = 0;  // "Add a condition/action": `link` in the list's coordinates
    virtual void chipClick(int kind, int i, int pi, draw::Rect chip) = 0;
    virtual void toggleCollapsed(int i) = 0;
    virtual void entriesLaid(int height) = 0;  // the entries' height changed: the editor follows
    // A key on a list (`isList`: the trigger list, else the entries): true when it is one of the list's keys.
    virtual bool listKey(bool /*isList*/, const draw::KeyEvent& /*e*/) { return false; }
};

// A click area of a painted list: the mouse on it runs `click`; a `drag` (a grip) starts a drag; the right button runs
// `context` (a menu, in the list's coordinates); `tip` is the tooltip.
struct CtHit {
    draw::Rect r;
    std::function<void()> click;
    std::function<void(draw::Point)> drag;
    std::function<void(draw::Point)> context;
    std::wstring tip;
    draw::Cursor cursor = draw::Cursor::Arrow;
};

// One of a row's actions at its right edge: a bare icon, its tip, what it does, its tone and state.
struct CtAction {
    UiIcon icon = UiIcon::Power;
    std::wstring tip;
    std::function<void()> click;
    stardraft::kit::Tone tone = stardraft::kit::Tone::Normal;
    bool on = false;
    bool enabled = true;
};

class CtListView final : public draw::View {
public:
    // `triggers`: the list of triggers (else the entries: the conditions and the actions).
    CtListView(CtListHost& host, bool triggers) : host_(host), triggers_(triggers) {}

    // The scroll goes back to the top (a trigger was selected).
    void resetScroll() { scroll_ = 0; }
    int scrollPos() const { return scroll_; }
    // The content's height as the last paint made it (the editor's layout takes it for the entries).
    int contentHeight() const { return height_; }
    // The trigger at `k` in the list (its place among the shown triggers) is in view: the list scrolls to it when it is not.
    void revealShown(int k) {
        if (height() <= 0) return;
        const int top = scale(38) + k * scale(46), h = scale(46);  // (the add row is above the rows)
        if (top < scroll_) scroll_ = top;
        else if (top + h > scroll_ + height()) scroll_ = top + h - height();
        invalidate();
    }
    // Drags row `from` of a list (0 the triggers, 1 the conditions, 2 the actions) to place `to` (in the list without it),
    // through the click areas the last paint made, as the mouse does. `atPlace` runs while the row is at the place.
    bool dragRow(int list, int from, int to, const std::function<void()>& atPlace) {
        const auto& rows = rows_[list];
        const int n = int(rows.size());
        if (from < 0 || from >= n || to < 0 || to >= n) return false;
        const draw::Point grip{rows[size_t(from)].left + scale(8), (rows[size_t(from)].top + rows[size_t(from)].bottom) / 2};
        const CtHit* h = hitAt(grip, true);
        if (!h || !h->drag) return false;
        const int slot = to > from ? to + 1 : to;  // the row the dragged one goes before
        const draw::Point at{grip.x, slot < n ? rows[size_t(slot)].top + 1 : rows.back().bottom - 1};
        mouse({draw::MouseKind::Down, grip, 0, 0, false, 0});
        mouse({draw::MouseKind::Move, at, 0, 0, false, 0});
        if (atPlace) atPlace();
        mouse({draw::MouseKind::Up, at, 0, 0, false, 0});
        return true;
    }

    void paint(draw::Canvas& c) override {
        hits_.clear();
        if (triggers_) paintTriggers(c);
        else paintEntries(c);
    }
    bool key(const draw::KeyEvent& e) override { return host_.listKey(triggers_, e); }

    bool mouse(const draw::MouseEvent& e) override {
        switch (e.kind) {
        case draw::MouseKind::Move:
            if (drag_.from >= 0) {  // a row dragged: its place follows, and the list scrolls at its edges
                drag_.to = toIndex(drag_.list, e.pt.y);
                const int edge = scale(20);
                if (e.pt.y < edge) scrollTo(scroll_ - scale(12));
                else if (e.pt.y > height() - edge) scrollTo(scroll_ + scale(12));
            }
            if (drag_.from >= 0) invalidate();  // (the drag moves the rows)
            else if (const draw::Rect over = hoverRectAt(e.pt); !sameRect(over, hoverRect_)) hoverRect_ = over, invalidate();  // the hover moved to another part
            return true;
        case draw::MouseKind::Leave:
            if (hoverRect_.right > hoverRect_.left || hoverRect_.bottom > hoverRect_.top) hoverRect_ = {}, invalidate();
            return true;
        case draw::MouseKind::Down: {
            if (e.button != 0) return false;
            if (draw::Host* h = host()) h->focus(*this);
            const CtHit* hit = hitAt(e.pt, true);
            if (!hit) return true;
            if (hit->drag) {
                hit->drag(e.pt);
                if (draw::Host* h = host()) h->capture(*this, true);
            } else if (hit->click) {
                auto click = hit->click;  // (the click may repaint, which clears the hits)
                click();
            }
            return true;
        }
        case draw::MouseKind::Up: {
            if (e.button == 1) {  // the right button: the menu of the row
                const CtHit* hit = contextAt(e.pt);
                if (!hit) return false;
                auto context = hit->context;
                context(e.pt);
                return true;
            }
            if (e.button != 0 || drag_.from < 0) return true;
            const Drag d = std::exchange(drag_, Drag{});
            if (draw::Host* h = host()) h->capture(*this, false);
            if (d.to != d.from && d.to != d.from + 1 && d.moved) d.moved(d.from, d.to > d.from ? d.to - 1 : d.to);
            invalidate();
            return true;
        }
        case draw::MouseKind::Wheel:
            scrollTo(scroll_ - int(std::lround(draw::wheelDistance(e, float(scale(26)) * 3.0f))));
            return true;
        default: return false;
        }
    }
    draw::Cursor cursor(draw::Point pt) override {
        const CtHit* hit = hitAt(pt, true);
        return hit ? hit->cursor : draw::Cursor::Arrow;
    }
    std::wstring tip(draw::Point pt) override {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (it->r.contains(pt) && !it->tip.empty()) return it->tip;
        return {};
    }
    bool hoverTip() const override { return true; }

    // The bar: the list's height, its page (the visible height) and its position.
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, height_ - 1), page = height(), pos = scroll_;
        return true;
    }
    bool vscroll(int code, int /*pos*/, int track, int page) override {
        int s = scroll_;
        switch (code) {  // the SB_* requests
        case 0: s -= scale(26); break;  // SB_LINEUP: a row
        case 1: s += scale(26); break;  // SB_LINEDOWN
        case 2: s -= page; break;       // SB_PAGEUP
        case 3: s += page; break;       // SB_PAGEDOWN
        case 4:                         // SB_THUMBPOSITION
        case 5: s = track; break;       // SB_THUMBTRACK
        case 6: s = 0; break;           // SB_TOP
        case 7: s = 1 << 30; break;     // SB_BOTTOM
        default: return true;
        }
        scrollTo(s);
        return true;
    }

private:
    // The row being dragged (`from`, `to` in its list) and what it does when it is dropped.
    struct Drag {
        int list = 0, from = -1, to = -1;
        std::function<void(int, int)> moved;
    };

    bool visible(const draw::Rect& r) const { return r.bottom > 0 && r.top < height(); }
    bool hot(const draw::Rect& r) const {
        const draw::Host* h = host();
        return h && r.contains(h->mouse(*this));
    }
    CtHit& hit(draw::Rect r, std::function<void()> click, std::wstring tip = {}, draw::Cursor cur = draw::Cursor::Arrow) {
        hits_.push_back({r, std::move(click), nullptr, nullptr, std::move(tip), cur});
        return hits_.back();
    }
    const CtHit* hitAt(draw::Point pt, bool pressable) const {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (it->r.contains(pt) && (!pressable || it->click || it->drag)) return &*it;
        return nullptr;
    }
    // The rectangle of the part under the point (its hit), or an empty one: the hover paints only when this changes.
    draw::Rect hoverRectAt(draw::Point pt) const {
        const CtHit* h = hitAt(pt, false);
        return h ? h->r : draw::Rect{};
    }
    static bool sameRect(const draw::Rect& a, const draw::Rect& b) {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    }
    const CtHit* contextAt(draw::Point pt) const {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (it->r.contains(pt) && it->context) return &*it;
        return nullptr;
    }
    void scrollTo(int s) {
        s = std::clamp(s, 0, std::max(0, height_ - height()));
        if (s != scroll_) scroll_ = s, invalidate();
    }
    // The place a dragged row goes to: the first row whose middle is below the point (else the end).
    int toIndex(int list, int y) const {
        const auto& rs = rows_[list];
        for (size_t i = 0; i < rs.size(); ++i)
            if (y < (rs[i].top + rs[i].bottom) / 2) return int(i);
        return int(rs.size());
    }

    // A row that can be reordered: its grip (on the row under the mouse, or while it is dragged) and the drag by the grip.
    void reorderRow(draw::Canvas& c, int list, draw::Rect row, int index, std::function<void(int, int)> moved) {
        rows_[list].push_back(row);
        const bool dragging = drag_.from >= 0 && drag_.list == list;
        const bool me = dragging && drag_.from == index;
        if (me) c.fill(row, kSel, 160);
        if (hot(row) || me) grip(c, row.left + scale(5), (row.top + row.bottom) / 2 - scale(6), me);
        const draw::Rect g{row.left, row.top, row.left + scale(18), row.bottom};
        CtHit& h = hit(g, nullptr, L"Drag to move", draw::Cursor::SizeAll);
        h.drag = [this, list, index, moved](draw::Point pt) {
            drag_.list = list, drag_.from = index, drag_.moved = moved;
            drag_.to = toIndex(list, pt.y);
        };
    }
    static void grip(draw::Canvas& c, int x, int y, bool on) {
        for (int gx = 0; gx < 2; ++gx)
            for (int gy = 0; gy < 3; ++gy) {
                const int dx = x + gx * scale(5), dy = y + gy * scale(5);
                c.fill({dx, dy, dx + scale(2), dy + scale(2)}, on ? kAccent : kGripDot);
            }
    }
    // After a list's rows: the line where the dragged row goes.
    void dropLine(draw::Canvas& c, int list) {
        if (drag_.from < 0 || drag_.list != list) return;
        const auto& rs = rows_[list];
        if (rs.empty()) return;
        const int y = drag_.to < int(rs.size()) ? rs[size_t(drag_.to)].top : rs.back().bottom;
        c.fill({rs.front().left, y - scale(1), rs.front().right, y + scale(1)}, kAccent);
    }

    // The kit's shapes (ui_kit.inc's): a row's ground, a chip, an add row, the row's icon buttons.
    static void listRow(draw::Canvas& c, draw::Rect r, bool selected, bool over) {
        if (selected) {
            c.fill(r, kSel);
            c.fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
        } else if (over) {
            c.fill(r, kRowHot);
        }
    }
    static draw::Color textOfRow(bool selected) { return selected ? kAccent : kText; }
    static void chipFace(draw::Canvas& c, draw::Rect r, bool over, bool dim) {
        const draw::Color fill = dim ? kChipDim : over ? kSelHot : kChip;
        const draw::Color line = dim ? kReadOnlyLine : over ? kAccentLine : kChipLine;
        c.roundBox(r, fill, line, float(scale(stardraft::kit::kRadius)));
    }
    void addRow(draw::Canvas& c, draw::Rect r, const std::wstring& text, std::function<void()> click, const std::wstring& tip, bool enabled) {
        const bool over = enabled && hot(r);
        c.roundBox(r, over ? kRowHot : draw::kNone, over ? kBorderHot : kBorder, float(scale(6)));
        const int is = scale(16);
        const draw::Color col = !enabled ? kFaint : over ? kAccent : kDim;
        const draw::Rect ir{r.left + scale(10), (r.top + r.bottom - is) / 2, r.left + scale(10) + is, (r.top + r.bottom + is) / 2};
        c.icon(ir, int(UiIcon::Add), col, is);
        c.text({ir.right + scale(8), r.top, r.right - scale(8), r.bottom}, text, col, draw::kMiddle | draw::kEllipsis);
        hit(r, enabled ? std::move(click) : nullptr, tip, enabled ? draw::Cursor::Hand : draw::Cursor::Arrow);
    }
    static draw::Rect squareOf(draw::Rect r) {
        const int w = r.width(), h = r.height(), s = std::min(w, h);
        const int x = r.left + (w - s) / 2, y = r.top + (h - s) / 2;
        return {x, y, x + s, y + s};
    }
    // A row's actions at its right edge (each a square of kIconBox): shown on the row under the mouse, or when `show`.
    void rowActions(draw::Canvas& c, draw::Rect row, const std::vector<CtAction>& acts, bool show) {
        const int w = scale(stardraft::kit::kIconBox), h = std::min(row.height(), scale(stardraft::kit::kIconBox));
        int x = row.right - scale(4) - w * int(acts.size());
        if (!show && !hot(row)) return;
        const int y = (row.top + row.bottom - h) / 2;
        for (const CtAction& a : acts) {
            const draw::Rect r = squareOf({x, y, x + w, y + h});
            const bool over = a.enabled && hot(r);
            if (over) c.roundBox(r, kCtlHot, draw::kNone, float(scale(stardraft::kit::kRadius)));
            const bool quiet = a.tone == stardraft::kit::Tone::Quiet;
            const draw::Color rest = quiet && a.on ? kWarn : a.on || a.tone == stardraft::kit::Tone::Primary ? kAccent : kDim;
            draw::Color lit = kText;
            if (a.tone == stardraft::kit::Tone::Danger) lit = kBad;
            else if (quiet && a.on) lit = kWarn;
            else if (a.tone == stardraft::kit::Tone::Primary || a.on) lit = kAccentBright;
            c.icon(r, int(a.icon), !a.enabled ? kFaint : over ? lit : rest, scale(16));
            hit(r, a.enabled ? a.click : nullptr, a.tip, a.enabled ? draw::Cursor::Hand : draw::Cursor::Arrow);
            x += w;
        }
    }
    // "None" in an empty list, at x; returns the y under it.
    int noneRow(draw::Canvas& c, int x, int y, int w) {
        c.text({x, y, w, y + scale(stardraft::kit::kRowH)}, L"None", kDim, draw::kMiddle);
        return y + scale(stardraft::kit::kRowH);
    }
    // A section heading (its name and a line to the right edge); returns the y under it.
    static int sectionHeader(draw::Canvas& c, int y, int w, const wchar_t* name) {
        const int rowH = scale(stardraft::kit::kRowH);
        c.text({0, y, w, y + rowH}, name, kText, draw::kMiddle);
        c.fill({0, y + rowH - 1, w, y + rowH}, kBorder);
        return y + rowH + scale(6);
    }
    // A glyph of the Segoe MDL2 glyph font, centered in its box.
    static void glyphText(draw::Canvas& c, draw::Rect r, const wchar_t* g, draw::Color col, int px) {
        const draw::Font of = c.setFont(c.font({draw::Face::Glyphs, px}));
        c.text(r, std::wstring_view(g, 1), col, draw::kMiddle | draw::kCenter);
        c.setFont(of);
    }

    // A sentence's words and value chips (ctSentenceParts): each chip is a click area (its value's picker).
    template <class E>
    void sentence(draw::Canvas& c, const E& e, int kind, int index, draw::Rect area, bool dim, bool selected) {
        const ClassicDef* def = kind ? classicActionDef(e.type) : classicConditionDef(e.type);
        CtSentenceEnv env;
        env.names = host_.names();
        env.chipText = [this](const ClassicParam& p, uint32_t v) { return host_.chipText(p, v); };
        const draw::Color word = dim ? kFaint : selected ? kAccent : kText;
        int x = area.left;
        auto text = [&](const std::wstring& s, draw::Color col, bool chip) -> draw::Rect {
            if (x >= area.right || s.empty()) return {};
            const int pad = chip ? scale(5) : 0;
            int w = c.measure(s) + 2 * pad;
            if (x + w > area.right) w = area.right - x;
            const draw::Rect r{x, area.top, x + w, area.bottom};
            if (chip) chipFace(c, {r.left, r.top + scale(4), r.right, r.bottom - scale(4)}, hot(r), dim);
            c.text({r.left + pad, r.top, r.right - pad, r.bottom}, s, col, draw::kMiddle | draw::kEllipsis);
            x += w;
            return r;
        };
        const ClassicTrigger* t = kind == 1 ? host_.selectedTrigger() : nullptr;
        for (const CtSegment& s : ctSentenceParts(e, kind, t, index, env)) {
            if (s.part == CtPart::Eud) {
                text(s.text, kDim, false);
            } else if (s.part == CtPart::Word) {
                text(s.text, word, false);
            } else {
                const draw::Rect r = text(s.text, dim ? kFaint : kText, true);
                if (r.right > r.left && def && s.param >= 0 && s.param < int(def->params.size())) {
                    const ClassicParam& p = def->params[size_t(s.param)];
                    const int pi = s.param;
                    hit(r, [this, kind, index, pi, r] { host_.chipClick(kind, index, pi, r); }, host_.chipTip(p), draw::Cursor::Hand);
                }
            }
        }
    }

    // After a paint: the content's height (the entries' height goes to the host, which the editor follows), and the scroll
    // is kept inside it.
    void settle(int y) {
        height_ = y + scroll_;
        if (!triggers_ && height_ != laid_) laid_ = height_, host_.entriesLaid(height_);
        if (const int most = std::max(0, height_ - height()); scroll_ > most) scroll_ = most, invalidate();
    }

    // The trigger list: "Add a trigger" first, then each trigger in two lines (its first condition; its players and action
    // count), an "Off" chip on a turned-off trigger, and the row's actions on the row under the mouse (or the selected one).
    void paintTriggers(draw::Canvas& c) {
        const int w = width(), rowH = scale(46), textX = scale(22);
        rows_[0].clear();
        int y = -scroll_;
        const ClassicNames n = host_.names();
        addRow(c, {scale(10), y + scale(2), w - scale(10), y + scale(32)}, L"Add a trigger", [this] { host_.addTrigger(); }, L"Add a trigger after the selected one", true);
        y += scale(38);
        if (host_.shown().empty()) y = noneRow(c, scale(10), y, w);
        const std::vector<int> shown = host_.shown();
        for (int k = 0; k < int(shown.size()); ++k) {
            const int i = shown[size_t(k)];
            const draw::Rect r{0, y, w, y + rowH};
            y += rowH;
            const bool sel = i == host_.selectedIndex(), checked = host_.marked(i);
            const ClassicTrigger& t = host_.triggers()[size_t(i)];
            if (visible(r)) {
                listRow(c, r, sel || checked, hot(r));
                const std::wstring cond = ctTriggerLineOf(t, false, n), acts = ctTriggerLineOf(t, true, n);  // (for the rows shown only)
                CtHit& h = hit(r, [this, i] { host_.clickTrigger(i); }, (cond.empty() ? L"" : cond + L". ") + acts);
                h.context = [this, i](draw::Point p) { host_.triggerMenu(i, p); };
            }
            reorderRow(c, 0, r, k, [this](int from, int to) { host_.moveShown(from, to); });
            if (!visible(r)) continue;
            const bool lit = sel || checked;
            const int parts = ctPartsOf(t);  // a trigger too long for 64 slots is several triggers in the map
            const int actionCount = t.actionCount();
            const int right = r.right - scale(8) - scale(stardraft::kit::kIconBox) * 3;
            const draw::Rect top{textX, r.top, right, (r.top + r.bottom) / 2 + scale(1)}, bottom{textX, top.bottom - scale(2), right, r.bottom};
            // The first condition (Always when there is none), and the Off chip at the right of it.
            std::wstring first = t.conditionCount() ? widen(classicConditionText(t.conditions[0], n)) : L"Always";
            if (parts > 1) first = tr(L"Becomes ") + std::to_wstring(parts) + tr(L" triggers in the map. ") + first;
            int textRight = right;
            if (t.disabled()) {
                const int cw = c.measure(L"Off") + scale(14);
                const draw::Rect chip{right - cw, top.top + scale(5), right, top.bottom - scale(5)};
                chipFace(c, chip, false, true);
                c.text(chip, L"Off", kWarn, draw::kMiddle | draw::kCenter);
                textRight = chip.left - scale(8);
            }
            const draw::Rect head{textX, top.top, textRight, top.bottom};
            // (a turned-off trigger's line is in the warning color, as the native paint left it after the Off chip)
            c.text(head, std::to_wstring(i + 1) + L". " + first, t.disabled() ? kWarn : textOfRow(lit), draw::kBottom | draw::kEllipsis);
            // The players and the number of actions: "Player 1, Player 2 | 2 actions".
            std::wstring who;
            for (int g = 0; g < kClassicGroups; ++g)
                if (t.players[size_t(g)]) who += (who.empty() ? L"" : L", ") + widen(classicPlayerName(uint32_t(g), n));
            const std::wstring tail = (who.empty() ? L"No players" : who) + L" | " + std::to_wstring(actionCount) + (actionCount == 1 ? L" action" : L" actions");
            c.text(bottom, tail, kDim, draw::kEllipsis);
            rowActions(c, r,
                       {{UiIcon::Power, t.disabled() ? L"Turn on" : L"Turn off", [this, i] { host_.toggleTrigger(i); }, stardraft::kit::Tone::Quiet, t.disabled()},
                        {UiIcon::Duplicate, L"Duplicate", [this, i] { host_.duplicateTrigger(i); }},
                        {UiIcon::Delete, L"Delete", [this, i] { host_.deleteTrigger(i); }, stardraft::kit::Tone::Danger}},
                       lit);
        }
        dropLine(c, 0);
        settle(y);
    }

    // The entries: the conditions, then the actions. A block row has its chevron, its children below it with a rule at each
    // depth, and its child count ("2 actions", or "3 actions hidden" when collapsed).
    void paintEntries(draw::Canvas& c) {
        const int w = width(), rowH = scale(stardraft::kit::kRowH);
        int y = -scroll_;
        const ClassicTrigger* t = host_.selectedTrigger();
        for (int kind = 0; kind < 2; ++kind) {
            const int list = kind + 1;
            rows_[list].clear();
            y = sectionHeader(c, y, w, kind ? L"Actions" : L"Conditions");
            const int count = t ? (kind ? t->actionCount() : t->conditionCount()) : 0;
            if (!count && !t) y = noneRow(c, scale(10), y, w);  // (a trigger: its add row says it)
            int hiddenEnd = -1;  // the children of a collapsed block are not shown: up to this row
            for (int i = 0; i < count; ++i) {
                if (kind && i < hiddenEnd) continue;
                hiddenEnd = -1;
                const bool isBlock = kind && t->actions[size_t(i)].type == kClassicBlockType;
                if (isBlock && host_.collapsed(i)) hiddenEnd = t->blockEnd(i);
                const int depth = kind ? t->actions[size_t(i)].depth : 0;
                const int indent = depth * scale(18) + scale(6);  // the chevron's cell; the sentence starts after it
                const draw::Rect r{0, y, w, y + rowH};
                y += rowH;
                const bool sel = host_.entryKind() == kind && host_.entryIndex() == i;
                if (visible(r)) {
                    listRow(c, r, sel, hot(r));
                    CtHit& h = hit(r, [this, kind, i] { host_.selectEntry(kind, i); });
                    h.context = [this, kind, i](draw::Point p) { host_.entryMenu(kind, i, p); };
                }
                reorderRow(c, list, r, i, [this, kind](int from, int to) { host_.moveEntry(kind, from, to); });
                if (!visible(r)) continue;
                // A rule at each depth the row is inside: the children of a block read as inside it.
                for (int lv = 0; lv < depth; ++lv) {
                    const int x = lv * scale(18) + scale(13);
                    c.fill({x, r.top, x + scale(1), r.bottom}, kBorder);
                }
                const uint8_t flags = kind ? t->actions[size_t(i)].flags : t->conditions[size_t(i)].flags;
                const bool off = (flags & 2) != 0;
                if (isBlock) {  // the chevron: expanded (down) or collapsed (right); the whole indent column shows and takes a click
                    const bool shut = host_.collapsed(i);
                    const draw::Rect column{0, r.top, indent + scale(14), r.bottom};
                    const draw::Rect glyphBox{indent, r.top, indent + scale(14), r.bottom};
                    glyphText(c, glyphBox, shut ? L"\xE76C" : L"\xE70D", textOfRow(sel), scale(12));
                    hit(column, [this, i] { host_.toggleCollapsed(i); }, shut ? L"Show the actions of the block" : L"Hide the actions of the block",
                        draw::Cursor::Hand);
                }
                // The row's actions: a block (edit, turn on or off, duplicate, delete); an action (turn on or off, move out,
                // move in, duplicate, delete); a condition (turn on or off, duplicate, delete).
                std::vector<CtAction> acts;
                if (isBlock) acts.push_back({UiIcon::Edit, L"Edit the block", [this, i] { host_.editBlock(i); }});
                acts.push_back({UiIcon::Power, off ? L"Turn on" : L"Turn off", [this, kind, i] { host_.toggleEntry(kind, i); }, stardraft::kit::Tone::Quiet, off, !isBlock});
                if (kind) {
                    const bool canOut = ctCanMoveRow(*t, i, false), canIn = ctCanMoveRow(*t, i, true);
                    acts.push_back({UiIcon::Back, L"Move out (Ctrl+Left)", [this, i] { host_.moveRow(i, false); }, stardraft::kit::Tone::Normal, false, canOut});
                    acts.push_back({UiIcon::Forward, L"Move in (Ctrl+Right)", [this, i] { host_.moveRow(i, true); }, stardraft::kit::Tone::Normal, false, canIn});
                }
                acts.push_back({UiIcon::Duplicate, L"Duplicate", [this, kind, i] { host_.duplicateEntry(kind, i); }});
                acts.push_back({UiIcon::Delete, L"Delete", [this, kind, i] { host_.deleteEntry(kind, i); }, stardraft::kit::Tone::Danger});
                const int actsW = scale(stardraft::kit::kIconBox) * int(acts.size());
                const int textLeft = indent + scale(18);
                const int areaRight = r.right - scale(8) - actsW - (isBlock ? scale(96) : 0);
                const draw::Rect area{textLeft, r.top, areaRight, r.bottom};
                if (kind) sentence(c, t->actions[size_t(i)], 1, i, area, off, sel);
                else sentence(c, t->conditions[size_t(i)], 0, i, area, off, sel);
                if (isBlock) {  // the child count: "2 actions", or "3 actions hidden" when collapsed
                    const int children = t->blockEnd(i) - i - 1;
                    const std::wstring childText = std::to_wstring(children) + (children == 1 ? L" action" : L" actions") + (hiddenEnd >= 0 ? L" hidden" : L"");
                    c.text({areaRight + scale(4), r.top, areaRight + scale(96), r.bottom}, childText, kDim, draw::kMiddle | draw::kRight);
                }
                rowActions(c, r, acts, sel);
            }
            dropLine(c, list);
            y += scale(4);
            if (t) {  // the add row: an action inside the selected block says which block
                const int limit = kind ? kClassicActions : kClassicConditions;
                const bool full = count >= limit;
                const draw::Rect lr{scale(10), y, w - scale(10), y + scale(30)};
                std::wstring text = kind ? L"Add an action\x2026" : L"Add a condition\x2026";
                std::wstring tip = full ? (kind ? L"A trigger has at most 64 actions" : L"A trigger has at most 16 conditions") : L"";
                if (kind && !full && host_.entryKind() == 1 && host_.entryIndex() >= 0 && host_.entryIndex() < count &&
                    t->actions[size_t(host_.entryIndex())].type == kClassicBlockType) {
                    text = tr(L"Add an action to For each ") + widen(t->actions[size_t(host_.entryIndex())].batch->name) + L"\x2026";
                    tip = L"Add an action inside the block (the end of its actions)";
                }
                addRow(c, lr, text, [this, kind, lr] { host_.addClick(kind, lr); }, tip, !full);
                y += scale(30);
            }
            y += scale(16);
        }
        settle(y);
    }

    CtListHost& host_;
    bool triggers_ = true;
    std::vector<CtHit> hits_;
    std::vector<draw::Rect> rows_[3];  // the rows of each list as painted: the triggers, the conditions, the actions
    Drag drag_;
    draw::Rect hoverRect_;  // the part the mouse is over (its hit's rectangle at the last move)
    int scroll_ = 0, height_ = 0, laid_ = -1;
};

// The page's head (classic_triggers.inc: the head's window is a view window with this view): the selected trigger's name
// and its count, its Turn off and Delete buttons, its players (one check box per player, in the page's order) and Forces.
// The kit widgets are the page's; the page places them (ctLayout) and the callbacks are its commands. The page's own
// changes (setOn, setEnabled, the texts) do not call back; only a click does.
class CtHeadView final : public draw::View {
public:
    struct Callbacks {
        std::function<void(int box, bool on)> player;  // the box's index in the page's order
        std::function<void()> turnOff, deleteTrig, forces;
    };
    CtHeadView(const std::vector<std::wstring>& playerLabels, Callbacks cb)
        : turnOff(L"Turn off", stardraft::kit::Tone::Normal, std::move(cb.turnOff)),
          deleteTrig(L"Delete the trigger", stardraft::kit::Tone::Normal, std::move(cb.deleteTrig)),
          forces(L"Forces\x2026", stardraft::kit::Tone::Normal, std::move(cb.forces)) {
        title.setFont({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)});  // the page's title font (ui::titleFont: 1.5 times the UI size)
        title.setText(L"None");
        count.setTone(stardraft::kit::LabelTone::Dim);  // the old count was the dim static (kCtDim)
        forces.setTip(L"Forces: the forces that run the trigger");
        for (size_t i = 0; i < playerLabels.size(); ++i)
            players.push_back(std::make_unique<stardraft::kit::CheckBox>(playerLabels[i], false, [i, f = cb.player](bool on) {
                if (f) f(int(i), on);
            }));
        for (auto& p : players) p->setPlainLabel(true);  // the native check boxes' labels were in the text color
        add(title, {0, 0, 0, 0});
        add(count, {0, 0, 0, 0});
        add(turnOff, {0, 0, 0, 0});
        add(deleteTrig, {0, 0, 0, 0});
        add(forces, {0, 0, 0, 0});
        for (auto& p : players) add(*p, {0, 0, 0, 0});
    }

    stardraft::kit::Label title, count;
    stardraft::kit::Button turnOff, deleteTrig, forces;
    std::vector<std::unique_ptr<stardraft::kit::CheckBox>> players;

    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, stardraft::kit::theme().bg); }
};
