// The Classic triggers page's parts that both programs share (portable, core.inc; after views_classic_triggers.inc,
// views_classic_batch.inc, views_classic_value.inc and views_list_pick.inc): the entries' commands (add, duplicate,
// delete, turn off, move, copy and paste), the values of the selected entry and their pickers and pop-outs, the block
// dialog's program part, the right-click menus, the keys of the lists, the lists' program part (CtListHost), the side
// (the search, the count and the filter) and the editor (the selected entry's type, its flags and its values) as kit
// views, and the page's layout. The program's part is a CtPageHost: classic_triggers.inc on Windows,
// sdl_classic_triggers.inc on SDL.

// What the page's commands ask of the program, more than the model's commands (CtModelHost): the page's model, the
// map, the selected entry (the editor and the lists follow it), the editor's values again, the list's scroll, a sound's
// length, and the pickers, pop-outs, the block dialog and the menus. A picker or a menu opens at `at`, in the coordinates
// of the view `from`.
class CtPageHost : public CtModelHost {
public:
    virtual CtModel& model() = 0;
    virtual Editor& editor() = 0;
    virtual void selectEntry(int kind, int entry) = 0;  // the model's entry is set; the editor and the lists follow
    virtual void buildEditor() = 0;                     // the editor shows the selected entry's values again
    virtual void resetListScroll() = 0;                 // the trigger list goes back to its top (a new search or filter)
    virtual void entriesLaid() = 0;                     // the entries' height changed: the page places its parts again
    virtual uint32_t soundLength(const std::string& name) = 0;  // ms; 0 when the sound is not found
    // The searchable list (one pick): true with the picked value in `out`.
    virtual bool pickOne(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, const draw::View& from,
                         draw::Point at, std::string& out) = 0;
    // The multi-pick: true (OK) with the new checks in `checked`.
    virtual bool pickMany(const std::wstring& title, std::vector<ListPickItem> items, std::set<std::string>& checked, const std::wstring& okText,
                          const draw::View& from, draw::Point at) = 0;
    // The number pop-out (core/views_classic_value.inc): true (Set) with the digits in `out`.
    virtual bool editNumber(const std::wstring& title, const std::wstring& label, const std::wstring& current, std::wstring& out) = 0;
    // The text pop-out: true (Set) with the new text in `text`.
    virtual bool editText(const std::wstring& title, const std::wstring& label, std::string& text) = 0;
    // The block dialog (core/views_classic_batch.inc, runBatchDialog): the button that closed it.
    // `bh` is the page's (CtBatchProgram); the host runs the dialog with gCtBatchView as its `shown`.
    virtual int editBlock(BatchHost& bh, ClassicBatch& b, bool editing) = 0;
    // A right-click menu of `items`: `run` gets the id of the item picked (it may run after this returns).
    virtual void menu(const draw::View& from, draw::Point at, const std::vector<CtMenuItem>& items, std::function<void(int)> run) = 0;
};

// Ctrl+C and Ctrl+V: a trigger, a condition or an action. The texts and sounds are kept as text, so a paste into
// another map gets that map's string ids.
struct ClassicTrigClip {
    int kind = -1;  // 0 condition, 1 action, 2 trigger
    ClassicCondition condition;
    std::vector<ClassicAction> rows;  // an action, or a block with its children (kind 1)
    std::vector<ClassicTrigger> triggers;  // kind 2: the checked triggers (or the selected one), in the list's order
    std::vector<std::vector<std::pair<std::string, std::string>>> triggerTexts;  // per trigger, per action: the text and the sound
    std::vector<std::pair<std::string, std::string>> texts;  // per action (kind 0 and 1): the text and the sound
};
inline ClassicTrigClip gCtClip;
inline BatchView* gCtBatchView = nullptr;  // the block dialog (core/views_classic_batch.inc) while it is open

// --- The selected entry ---------------------------------------------------------------------------------------------

inline ClassicCondition* ctModelCondition(CtModel& m) {
    ClassicTrigger* t = ctModelSelectedMut(m);
    return t && m.kind == 0 && m.entry >= 0 && m.entry < t->conditionCount() ? &t->conditions[size_t(m.entry)] : nullptr;
}
inline ClassicAction* ctModelAction(CtModel& m) {
    ClassicTrigger* t = ctModelSelectedMut(m);
    return t && m.kind == 1 && m.entry >= 0 && m.entry < t->actionCount() ? &t->actions[size_t(m.entry)] : nullptr;
}
inline int ctEntryCount(const ClassicTrigger& t, int kind) { return kind ? t.actionCount() : t.conditionCount(); }
// The definition of the selected entry's type (its parameters), or nullptr.
inline const ClassicDef* ctModelDef(CtModel& m) {
    const ClassicCondition* c = ctModelCondition(m);
    const ClassicAction* a = ctModelAction(m);
    return c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
}

// The choices of a picker: value and text. Units, locations, switches, sounds and AI scripts come from the map and
// the game; a value that is not in the list (an EUD value) is added by ctChoices, so it shows and stays.
inline std::vector<std::pair<uint32_t, std::wstring>> ctKindChoices(CtPageHost& h, ClassicKind k) {
    std::vector<std::pair<uint32_t, std::wstring>> out;
    CtModel& m = h.model();
    Editor& ed = h.editor();
    const ClassicNames n = h.names();
    switch (k) {
    case ClassicKind::Unit:
        for (int u = 0; u < 228; ++u) out.emplace_back(u, unitDisplayName(u));
        for (uint32_t g : {229u, 230u, 231u, 232u, 228u}) out.emplace_back(g, widen(classicValueText(k, g, n)));
        out[228].second[0] = towupper(out[228].second[0]);
        for (size_t i = 229; i < out.size(); ++i) out[i].second[0] = towupper(out[i].second[0]);
        break;
    case ClassicKind::Location:
        for (int i = 0; i < ed.locationCount(); ++i)
            if (ed.locationUsed(i) || i == kAnywhereLocation) out.emplace_back(uint32_t(i + 1), widen(ed.locationName(i)));
        break;
    case ClassicKind::Switch:
        for (uint32_t s = 0; s < 256; ++s) {
            const std::string name = ed.switchName(int(s));
            out.emplace_back(s, name.empty() ? L"Switch " + std::to_wstring(s + 1) : widen(name) + L" (" + std::to_wstring(s + 1) + L")");
        }
        break;
    case ClassicKind::Sound:
        out.emplace_back(0, L"No sound");
        for (uint32_t id : ed.soundStrings()) out.emplace_back(id, widen(m.strings.get(id)));
        break;
    case ClassicKind::AiScript:
        for (const ClassicAiScript& s : m.ai) out.emplace_back(s.id, widen(s.name) + L" (" + widen(classicFourCC(s.id)) + L")");
        break;
    case ClassicKind::Player:
        for (const ClassicChoice& c : classicChoices(k)) out.emplace_back(c.value, widen(classicPlayerName(c.value, n)));
        break;
    default:
        for (const ClassicChoice& c : classicChoices(k)) {
            std::wstring s = widen(c.name);
            if (!s.empty()) s[0] = towupper(s[0]);
            out.emplace_back(c.value, s);
        }
    }
    return out;
}

// The variable's depth of the selected action's field `f` (a condition has none): -1 when the field holds a value.
inline int ctVarDepthOf(CtModel& m, const ClassicCondition* c, ClassicField f) {
    const ClassicTrigger* t = ctModelSelected(m);
    if (c || !t) return -1;
    return ctVarDepthAt(*t, m.entry, f);
}
// The picker's choices, and the variables of the blocks that hold the selected action (a variable of this kind).
inline std::vector<std::pair<uint32_t, std::wstring>> ctChoices(CtPageHost& h, ClassicKind k, uint32_t current, ClassicField f) {
    CtModel& m = h.model();
    std::vector<std::pair<uint32_t, std::wstring>> out;
    const int slot = ctVarSlotOfField(f);
    if (const ClassicTrigger* t = ctModelSelected(m); t && slot >= 0 && m.kind == 1 && m.entry >= 0 && m.entry < t->actionCount()) {
        const ClassicAction& a = t->actions[size_t(m.entry)];
        const std::vector<const ClassicBatch*> scope = ctScopeOf(*t, m.entry);
        for (size_t d = 0; d < scope.size(); ++d)
            if (scope[d] && classicVarAllowed(a, slot, scope[d]->kind) && classicBatchValueKind(scope[d]->kind) == k)
                out.emplace_back(kCtVariable + uint32_t(d), widen(classicBatchVariableText(*scope[d])));
    }
    for (auto& p : ctKindChoices(h, k)) out.push_back(std::move(p));
    if (std::none_of(out.begin(), out.end(), [&](const auto& p) { return p.first == current; }))
        out.emplace_back(current, widen(classicValueText(k, current, h.names())) + L" (EUD value)");
    return out;
}

// A value's text on its button in the editor: the variable's name, the list's name, the number, or the first line of a
// text (dim "None" when empty).
inline std::wstring ctEditorValueText(CtPageHost& h, const ClassicParam& p, uint32_t v, int varDepth) {
    CtModel& m = h.model();
    if (varDepth >= 0) {
        const ClassicTrigger* t = ctModelSelected(m);
        const std::vector<const ClassicBatch*> scope = t ? ctScopeOf(*t, m.entry) : std::vector<const ClassicBatch*>{};
        if (size_t(varDepth) < scope.size() && scope[size_t(varDepth)]) return widen(classicBatchVariableText(*scope[size_t(varDepth)]));
    }
    const ClassicNames n = h.names();
    if (p.kind == ClassicKind::Text) {
        std::wstring s = widen(n.string(v));  // (the names' string is the text without its codes)
        for (wchar_t& ch : s)
            if (ch < 32) ch = L' ';
        return s.empty() ? L"None" : s;
    }
    if (ctPickerIsList(p.kind)) return ctChipTextOf(p, v, n, ctIsChoiceOf(h.editor(), m.ai, p.kind, v));
    return widen(classicValueText(p.kind, v, n));
}

// The selected entry's title: "Condition 2 of 3"; "Action 3 of 3" at the top, "Action 3 of 3 in For each unit" in a
// block (its place among the block's children).
inline std::wstring ctEntryTitle(CtModel& m, bool condition) {
    const ClassicTrigger* t = ctModelSelected(m);
    if (!t || m.entry < 0) return L"";
    if (condition) return L"Condition " + std::to_wstring(m.entry + 1) + L" of " + std::to_wstring(t->conditionCount());
    const int entry = m.entry, n = t->actionCount();
    if (entry >= n) return L"";
    const int depth = t->actions[size_t(entry)].depth;
    int from = 0, to = n, parent = -1;
    for (int i = entry - 1; i >= 0; --i)  // the block that holds the row: the nearest row above that is less deep
        if (t->actions[size_t(i)].depth < depth) {
            parent = i, from = i + 1, to = t->blockEnd(i);
            break;
        }
    int siblings = 0, index = 0;
    for (int i = from; i < to; ++i)
        if (t->actions[size_t(i)].depth == depth && ++siblings && i == entry) index = siblings;
    std::wstring s = L"Action " + std::to_wstring(index) + L" of " + std::to_wstring(siblings);
    if (parent >= 0 && t->actions[size_t(parent)].batch) s += tr(L" in For each ") + widen(t->actions[size_t(parent)].batch->name);
    return s;
}

// The editor's height: a title row and the rows of values (two values in a row; a text takes a row).
inline int ctEditorHeight(CtModel& m, int step, int gap) {
    const ClassicCondition* c = ctModelCondition(m);
    const ClassicAction* a = ctModelAction(m);
    const ClassicDef* def = c ? classicConditionDef(c->type) : a && a->type != kClassicBlockType ? classicActionDef(a->type) : nullptr;
    int plain = 0, texts = 0;
    if (def)
        for (const ClassicParam& p : def->params) (p.kind == ClassicKind::Text ? texts : plain)++;
    const int rows = (plain + 1) / 2 + texts;
    return step * (1 + std::max(1, rows)) + gap;
}

// Sets value `i` of the selected entry.
inline void ctSetValue(CtPageHost& h, int i, uint32_t v) {
    CtModel& m = h.model();
    ClassicCondition* c = ctModelCondition(m);
    ClassicAction* a = ctModelAction(m);
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def || i < 0 || i >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(i)];
    if (a && ctVarSlotOfField(p.field) >= 0) {  // the variable: the field holds its block's depth; a value: none
        const int slot = ctVarSlotOfField(p.field);
        if (v >= kCtVariable) {
            a->var[size_t(slot)] = uint8_t(v - kCtVariable + 1);
            setClassicValue(*a, p.field, 0);
            h.changed();
            return;
        }
        a->var[size_t(slot)] = 0;
    }
    if (c) setClassicValue(*c, p.field, v);
    else {
        setClassicValue(*a, p.field, v);
        if (p.kind == ClassicKind::Sound) a->time = v ? h.soundLength(m.strings.get(v)) : 0;  // Play WAV and Transmission hold the sound's length
    }
    h.changed();
}

// A text value of the selected entry: the map's string with this text (an existing one, else a new one); a text this
// page made is changed in place.
inline void ctSetText(CtPageHost& h, int i, const std::string& text) {
    CtModel& m = h.model();
    const ClassicAction* a = ctModelAction(m);
    const ClassicDef* def = a ? classicActionDef(a->type) : nullptr;
    if (!def || i < 0 || i >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(i)];
    uint32_t v = 0;
    const uint32_t old = a ? classicValue(*a, p.field) : 0;
    if (text.empty()) v = 0;
    else if (old && m.ownStrings.count(old)) m.strings.set(old, text), v = old;
    else {
        for (uint32_t id = 1; id <= m.strings.size() && !v; ++id)
            if (m.strings.get(id) == text) v = id;
        if (v) m.ownStrings.erase(v);  // a string with the same text: now shared, never changed in place
        else m.ownStrings.insert(v = m.strings.add(text));
    }
    ctSetValue(h, i, v);
}

// A number value: the range the field keeps (a count up to 255, a unit up to 65535, never a variable's value).
inline uint32_t ctClampNumber(const ClassicParam& p, unsigned long long n) {
    uint32_t v = uint32_t(std::min<unsigned long long>(n, 0xFFFFFFFFull));
    if (p.kind == ClassicKind::Count) v = std::min<uint32_t>(v, 255);
    if (p.field == ClassicField::Unit) v = std::min<uint32_t>(v, 0xFFFF);
    return std::min<uint32_t>(v, kCtVariable - 1);
}

// The pop-out of a number or a text value of the selected entry (parameter i), with Set and Cancel. Set sets the value.
inline void ctValuePopout(CtPageHost& h, int i) {
    CtModel& m = h.model();
    const ClassicCondition* c = ctModelCondition(m);
    const ClassicAction* a = ctModelAction(m);
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def || i < 0 || i >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(i)];
    const uint32_t v = c ? classicValue(*c, p.field) : classicValue(*a, p.field);
    if (p.kind != ClassicKind::Text) {
        std::wstring number;
        if (!h.editNumber(ctKindLabel(p.kind, p.name), ctKindLabel(p.kind, p.name), std::to_wstring(v), number)) return;
        ctSetValue(h, i, ctClampNumber(p, std::wcstoull(number.c_str(), nullptr, 10)));
        h.buildEditor();
        return;
    }
    std::string text = m.strings.get(v);
    if (!h.editText(ctKindLabel(p.kind, p.name), ctKindLabel(p.kind, p.name), text)) return;
    ctSetText(h, i, text);
    h.buildEditor();
}

// The selected entry's type changes (the values it keeps: retypeClassicCondition, retypeClassicAction).
inline void ctSetType(CtPageHost& h, uint8_t type) {
    CtModel& m = h.model();
    if (ClassicCondition* c = ctModelCondition(m)) *c = retypeClassicCondition(*c, type);
    else if (ClassicAction* a = ctModelAction(m)) *a = retypeClassicAction(*a, type);
    if (ClassicTrigger* t = ctModelSelectedMut(m)) t->pruneBlocks();  // a variable whose field the new type does not hold goes
    h.changed();
    h.buildEditor();
}
// The selected entry's flag (2: Turn off, 4: Always show) on or off.
inline void ctSetFlag(CtPageHost& h, uint8_t bit, bool on) {
    CtModel& m = h.model();
    uint8_t* flags = ctModelCondition(m) ? &ctModelCondition(m)->flags : ctModelAction(m) ? &ctModelAction(m)->flags : nullptr;
    if (flags) *flags = uint8_t(on ? *flags | bit : *flags & ~bit);
    h.changed();
}

// The list shows the triggers of a group (-1: all), or those with the words of a search. The selected trigger stays when
// it still shows, else the first one is selected.
inline void ctShowList(CtPageHost& h) {
    CtModel& m = h.model();
    h.resetListScroll();
    ctModelFill(m, h);
    h.selectTrigger(m.shown.empty() ? -1 : std::find(m.shown.begin(), m.shown.end(), m.sel) != m.shown.end() ? m.sel : m.shown[0]);
}
inline void ctSetFilter(CtPageHost& h, int group) {
    h.model().filter = group;
    ctShowList(h);
}
inline void ctSetQuery(CtPageHost& h, const std::wstring& query) {
    h.model().query = query;
    ctShowList(h);
}

// --- Conditions and actions: add, duplicate, delete, turn off, move --------------------------------------------------

inline bool ctInsertCondition(CtPageHost& h, const ClassicCondition& c, int at) {
    ClassicTrigger* t = ctModelSelectedMut(h.model());
    if (!t || !t->addCondition(c, at)) return false;
    h.selectEntry(0, at < 0 ? t->conditionCount() - 1 : at);
    h.changed();
    return true;
}
inline bool ctInsertAction(CtPageHost& h, const ClassicAction& a, int at) {
    ClassicTrigger* t = ctModelSelectedMut(h.model());
    if (!t || !t->addAction(a, at)) return false;
    h.model().collapsed.clear();  // the rows moved: the collapsed rows are not known any more
    h.selectEntry(1, at < 0 ? t->actionCount() - 1 : at);
    h.changed();
    return true;
}
// Rows (an action, or a block with its children) before place `at` (the end when -1), at `depth`; the first one is
// selected.
inline bool ctInsertRows(CtPageHost& h, const std::vector<ClassicAction>& rows, int at, int depth) {
    ClassicTrigger* t = ctModelSelectedMut(h.model());
    if (!t) return false;
    const int place = at < 0 ? t->actionCount() : at;
    if (!t->pasteRows(rows, place, depth)) return false;
    h.model().collapsed.clear();
    h.selectEntry(1, place);
    h.changed();
    return true;
}
// A new condition (action) of this type at the end of its list. A new action goes into the selected block (as its last
// child) when a block is selected.
inline bool ctAddEntry(CtPageHost& h, int kind, uint8_t type) {
    if (!kind) return ctInsertCondition(h, makeClassicCondition(type), -1);
    CtModel& m = h.model();
    ClassicTrigger* t = ctModelSelectedMut(m);
    if (t && m.kind == 1 && m.entry >= 0 && m.entry < t->actionCount() && t->actions[size_t(m.entry)].type == kClassicBlockType) {
        ClassicAction a = makeClassicAction(type);
        a.depth = uint8_t(t->actions[size_t(m.entry)].depth + 1);
        m.collapsed.erase(m.entry);
        return ctInsertAction(h, a, t->blockEnd(m.entry));
    }
    return ctInsertAction(h, makeClassicAction(type), -1);
}
inline void ctDuplicateEntry(CtPageHost& h, int kind, int i) {
    CtModel& m = h.model();
    ClassicTrigger* t = ctModelSelectedMut(m);
    if (!t || i < 0 || i >= ctEntryCount(*t, kind)) return;
    if (kind) {  // a block is copied with its children, after itself
        std::vector<ClassicAction> rows = t->subtreeRows(i);
        for (const ClassicAction& a : rows) m.ownStrings.erase(a.text);  // shared now
        ctInsertRows(h, rows, t->blockEnd(i), t->actions[size_t(i)].depth);
    } else {
        ctInsertCondition(h, t->conditions[size_t(i)], i + 1);
    }
}
inline void ctDeleteEntry(CtPageHost& h, int kind, int i) {
    CtModel& m = h.model();
    ClassicTrigger* t = ctModelSelectedMut(m);
    if (!t || i < 0 || i >= ctEntryCount(*t, kind)) return;
    if (kind) t->removeAction(i);
    else t->removeCondition(i);
    m.collapsed.clear();
    const int n = ctEntryCount(*t, kind);
    h.selectEntry(kind, n ? std::min(i, n - 1) : -1);
    h.changed();
}
inline void ctToggleEntry(CtPageHost& h, int kind, int i) {
    ClassicTrigger* t = ctModelSelectedMut(h.model());
    if (!t || i < 0 || i >= ctEntryCount(*t, kind)) return;
    if (kind && t->actions[size_t(i)].type == kClassicBlockType) return;  // a block is not turned off: its actions are
    uint8_t& f = kind ? t->actions[size_t(i)].flags : t->conditions[size_t(i)].flags;
    f ^= 2;
    h.selectEntry(kind, i);
    h.changed();
}
// Moves entry `from` to place `to` (in the list without it).
inline void ctMoveEntry(CtPageHost& h, int kind, int from, int to) {
    ClassicTrigger* t = ctModelSelectedMut(h.model());
    if (!t) return;
    const int n = ctEntryCount(*t, kind);
    if (from < 0 || from >= n || to < 0 || to >= n || to == from) return;
    if (kind) t->moveAction(from, to);
    else t->moveCondition(from, to);
    h.model().collapsed.clear();
    h.selectEntry(kind, to);
    h.changed();
}
// Can the action move into the block before it (in), or out of its block (out)? A trial on a copy.
inline bool ctCanMove(CtModel& m, int i, bool in) {
    const ClassicTrigger* t = ctModelSelected(m);
    return t && ctCanMoveRow(*t, i, in);
}
// Moves the action at row i one level in or out (Ctrl+Right and Ctrl+Left; the row's buttons).
inline void ctMoveRow(CtPageHost& h, int i, bool in) {
    ClassicTrigger* t = ctModelSelectedMut(h.model());
    if (!t) return;
    const bool moved = in ? t->indentAction(i) : t->outdentAction(i);
    if (!moved) return;
    h.model().collapsed.clear();
    h.changed();
    h.buildEditor();
}

// --- Copy and paste --------------------------------------------------------------------------------------------------

inline std::pair<std::string, std::string> ctActionTexts(CtModel& m, const ClassicAction& a) { return {m.strings.get(a.text), m.strings.get(a.wav)}; }
// The action's text and sound as this map's strings (an id that has no string stays as it is).
inline void ctResolveTexts(CtModel& m, ClassicAction& a, const std::pair<std::string, std::string>& texts) {
    if (!texts.first.empty()) a.text = classicStringId(m.strings, texts.first), m.ownStrings.erase(a.text);
    if (!texts.second.empty()) a.wav = classicStringId(m.strings, texts.second), m.ownStrings.erase(a.wav);
}
inline void ctCopy(CtPageHost& h, bool trigger) {
    CtModel& m = h.model();
    const ClassicTrigger* t = ctModelSelected(m);
    if (!t) return;
    if (trigger) {  // the checked triggers (or the selected one), in the list's order
        gCtClip = {};
        gCtClip.kind = 2;
        for (int i : ctModelTargets(m)) {
            const ClassicTrigger& c = m.trig.triggers[size_t(i)];
            gCtClip.triggers.push_back(c);
            gCtClip.triggerTexts.emplace_back();
            for (const ClassicAction& a : c.actions) gCtClip.triggerTexts.back().push_back(ctActionTexts(m, a));
        }
        return h.message(gCtClip.triggers.size() > 1 ? L"Copied the triggers" : L"Copied the trigger");
    }
    if (m.entry < 0 || m.entry >= ctEntryCount(*t, m.kind)) return;
    gCtClip = {};
    gCtClip.kind = m.kind;
    if (m.kind) {  // a block takes its children with it
        gCtClip.rows = t->subtreeRows(m.entry);
        for (const ClassicAction& a : gCtClip.rows) gCtClip.texts.push_back(ctActionTexts(m, a));
    } else {
        gCtClip.condition = t->conditions[size_t(m.entry)];
    }
    h.message(!m.kind ? L"Copied the condition" : gCtClip.rows.size() > 1 ? L"Copied the block" : L"Copied the action");
}
inline void ctPaste(CtPageHost& h) {
    CtModel& m = h.model();
    if (gCtClip.kind == 2) {  // each after the one before it: the selected trigger, then the pasted ones (one undo step)
        m.hold++;
        for (size_t k = 0; k < gCtClip.triggers.size(); ++k) {
            ClassicTrigger n = gCtClip.triggers[k];
            const auto& texts = gCtClip.triggerTexts[k];
            for (size_t i = 0; i < n.actions.size() && i < texts.size(); ++i) ctResolveTexts(m, n.actions[i], texts[i]);
            ctModelInsertTrigger(m, h, ctModelFreshCopy(m, n));
        }
        m.hold--;
        m.marked.clear();
        h.changed();
        return h.message(gCtClip.triggers.size() > 1 ? L"Pasted the triggers" : L"Pasted the trigger");
    }
    const ClassicTrigger* t = ctModelSelected(m);
    if (!t || gCtClip.kind < 0) return;
    bool ok;
    if (gCtClip.kind) {
        // After the selected action (after its block, at its depth); at the end of the trigger when none is selected.
        int at = -1, depth = 0;
        if (m.kind == 1 && m.entry >= 0 && m.entry < t->actionCount()) at = t->blockEnd(m.entry), depth = t->actions[size_t(m.entry)].depth;
        std::vector<ClassicAction> rows = gCtClip.rows;
        for (size_t i = 0; i < rows.size() && i < gCtClip.texts.size(); ++i) ctResolveTexts(m, rows[i], gCtClip.texts[i]);
        ok = ctInsertRows(h, rows, at, depth);
        return h.message(!ok ? L"The list is full" : rows.size() > 1 ? L"Pasted the block" : L"Pasted the action");
    }
    const int at = m.kind == gCtClip.kind && m.entry >= 0 ? m.entry + 1 : -1;
    ok = ctInsertCondition(h, gCtClip.condition, at);
    h.message(!ok ? L"The list is full" : L"Pasted the condition");
}

// --- The pickers, the pop-outs and the block dialog ------------------------------------------------------------------

// The searchable list of a parameter's values (a unit, a location, a switch, ...), with the variables that fit it
// first. It opens at `at` in `from`; one click selects.
inline void ctPickValue(CtPageHost& h, int pi, const draw::View& from, draw::Point at) {
    CtModel& m = h.model();
    ClassicCondition* c = ctModelCondition(m);
    ClassicAction* a = ctModelAction(m);
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def || pi < 0 || pi >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(pi)];
    const int vd = ctVarDepthOf(m, c, p.field);
    const uint32_t v = vd >= 0 ? kCtVariable + uint32_t(vd) : c ? classicValue(*c, p.field) : classicValue(*a, p.field);
    std::vector<ListPickItem> items;
    for (const auto& [value, label] : ctChoices(h, p.kind, v, p.field)) {
        ListPickItem it;
        it.label = label, it.value = std::to_string(value);
        items.push_back(std::move(it));
    }
    std::string picked;
    if (!h.pickOne(ctKindLabel(p.kind, p.name), std::move(items), std::to_string(v), from, at, picked) || picked.empty()) return;
    const uint32_t nv = uint32_t(std::stoul(picked));
    if (nv == v) return;
    ctSetValue(h, pi, nv);
    h.buildEditor();
}
// A value of the selected entry was clicked (its chip in the entries, or its field in the editor): a list of choices
// under it, or the value's pop-out (a number or a text).
inline void ctValueClick(CtPageHost& h, int pi, const draw::View& from, draw::Rect r) {
    const ClassicDef* def = ctModelDef(h.model());
    if (!def || pi < 0 || pi >= int(def->params.size())) return;
    if (!ctPickerIsList(def->params[size_t(pi)].kind)) return ctValuePopout(h, pi);  // a number or a text: its pop-out
    ctPickValue(h, pi, from, {r.left, r.bottom + scale(2)});
}
// A value chip of the entries was clicked: its entry is selected first.
inline void ctChipClick(CtPageHost& h, int kind, int i, int pi, const draw::View& from, draw::Rect chip) {
    CtModel& m = h.model();
    if (m.kind != kind || m.entry != i) h.selectEntry(kind, i);
    ctValueClick(h, pi, from, chip);
}

// "+ condition", "+ action": the game's conditions (actions) to select from.
// When new blocks are offered (kClassicBatchesOffered), they come first in the actions' list (a section "Blocks"),
// then the actions (section "Actions").
inline std::vector<ListPickItem> ctAddItems(int kind) {
    std::vector<ListPickItem> items;
    const bool blocks = kind && kClassicBatchesOffered;
    if (blocks)
        for (const ClassicBlockKind& k : classicBlockKinds()) {
            ListPickItem it;
            it.label = tr(L"For each ") + widen(k.name), it.section = L"Blocks", it.value = "block:" + std::to_string(int(k.kind));
            items.push_back(std::move(it));
        }
    for (const ClassicDef* def : kind ? classicActionDefs() : classicConditionDefs()) {
        ListPickItem it;
        it.label = widen(def->name), it.value = std::to_string(def->type);
        if (blocks) it.section = L"Actions";
        items.push_back(std::move(it));
    }
    return items;
}

// The block dialog's program part (core/views_classic_batch.inc has the dialog): the values' names, the kind's values
// for the multi-pick, and the picker, which opens under the Add values button.
class CtBatchProgram final : public BatchHost {
public:
    explicit CtBatchProgram(CtPageHost& h) : h_(h) {}
    std::wstring valueText(ClassicBatchKind kind, uint32_t v) override { return widen(classicValueText(classicBatchValueKind(kind), v, h_.names())); }
    std::vector<std::pair<uint32_t, std::wstring>> offeredValues(ClassicBatchKind kind) override { return ctKindChoices(h_, classicBatchValueKind(kind)); }
    bool pickValues(const std::vector<std::pair<uint32_t, std::wstring>>& offered, std::set<uint32_t>& checked, draw::Point at) override {
        BatchView* shown = gCtBatchView;  // (the dialog that is open: the picker opens over it)
        if (!shown) return false;
        std::vector<ListPickItem> items;
        for (const auto& [value, label] : offered) {
            ListPickItem it;
            it.label = label, it.value = std::to_string(value);
            items.push_back(std::move(it));
        }
        std::set<std::string> ch;
        for (uint32_t v : checked) ch.insert(std::to_string(v));
        const draw::Point o = shown->origin();  // (`at` is in the dialog's window: the picker takes it in the view's)
        if (!h_.pickMany(L"Search values", items, ch, L"Set", *shown, {at.x - o.x, at.y - o.y})) return false;
        checked.clear();
        for (const ListPickItem& it : items)
            if (ch.count(it.value)) checked.insert(uint32_t(std::stoul(it.value)));
        return true;
    }

private:
    CtPageHost& h_;
};

// Make a block in the selected trigger, or change a block (its variable, its values). `kind`: the kind of a new block
// (the picker's choice); an edited block (the action at `editRow`) keeps its kind until it has no values.
inline void ctBatchClick(CtPageHost& h, ClassicBatchKind kind, int editRow) {
    CtModel& m = h.model();
    ClassicTrigger* t = ctModelSelectedMut(m);
    if (!t) return;
    const int row = editRow;
    const bool editing = row >= 0 && row < t->actionCount() && t->actions[size_t(row)].type == kClassicBlockType;
    if (!editing && t->actionCount() >= kClassicActions) return h.message(L"A trigger has at most 64 actions");
    ClassicBatch b = editing ? *t->actions[size_t(row)].batch : ClassicBatch{};
    if (!editing) b.kind = kind, b.name = batchDefaultName(kind);
    CtBatchProgram program(h);
    const int r = h.editBlock(program, b, editing);
    t = ctModelSelectedMut(m);  // (the dialog ran: the selection is the same, but the trigger is read again)
    if (!t) return;
    if (r == kBatchRemove && editing) {
        t->unwrapAction(row);  // the block goes; its actions stay, one level out
        m.collapsed.clear();
        h.message(L"Removed the block");
        h.selectEntry(1, -1);
    } else if (r == kBatchOk && editing) {
        t->actions[size_t(row)].batch = std::make_shared<ClassicBatch>(b);
        t->pruneBlocks();
        h.message(L"Changed the block");
    } else if (r == kBatchOk) {  // a new block goes into the selected block (as its last child), else at the end
        ClassicAction blk;
        blk.type = kClassicBlockType;
        blk.batch = std::make_shared<ClassicBatch>(b);
        const int sel = m.entry;
        const bool inBlock = m.kind == 1 && sel >= 0 && sel < t->actionCount() && t->actions[size_t(sel)].type == kClassicBlockType;
        const int at = inBlock ? t->blockEnd(sel) : -1;
        if (inBlock) blk.depth = uint8_t(t->actions[size_t(sel)].depth + 1), m.collapsed.erase(sel);
        t->addAction(blk, at);
        h.message(L"Added a block");
        h.selectEntry(1, at >= 0 ? at : t->actionCount() - 1);
    } else {
        return;
    }
    ctModelFill(m, h);
    h.buildEditor();
    h.updateHeader();
    h.changed();
}

// "+ condition" or "+ action" was clicked: the list of the types under it (`link` in `from`); a block opens its dialog.
inline void ctAddClick(CtPageHost& h, int kind, const draw::View& from, draw::Rect link) {
    std::string picked;
    if (!h.pickOne(kind ? L"Search actions" : L"Search conditions", ctAddItems(kind), "", from, {link.left, link.bottom + scale(2)}, picked) || picked.empty())
        return;
    if (picked.rfind("block:", 0) == 0) return ctBatchClick(h, ClassicBatchKind(std::stoi(picked.substr(6))), -1);
    ctAddEntry(h, kind, uint8_t(std::stoi(picked)));
}

// Forces: the players of the forces (multi-pick: each force checked or not for the selected trigger), under `from`.
inline void ctForcesClick(CtPageHost& h, const draw::View& from) {
    CtModel& m = h.model();
    const ClassicTrigger* t = ctModelSelected(m);
    if (!t) return;
    std::set<std::string> checked;
    std::vector<ListPickItem> items = ctForceItems(*t, h.names(), checked);
    if (!h.pickMany(L"Search forces", std::move(items), checked, L"Set", from, {0, from.height() + scale(2)})) return;
    ctModelSetForces(m, h, checked);
}

// --- The right-click menus and the keys ------------------------------------------------------------------------------

// The right-click menus of the rows (the same commands as the rows' buttons and the keys). A right click on a row that
// is not checked selects it first (and the checks go).
inline void ctTriggerMenu(CtPageHost& h, int i, const draw::View& from, draw::Point at) {
    CtModel& m = h.model();
    if (!m.marked.count(i)) m.marked.clear(), h.selectTrigger(i);
    const ClassicTrigger* t = ctModelSelected(m);
    h.menu(from, at, ctTriggerMenuItems(t && t->disabled(), gCtClip.kind == 2), [&h](int pick) {
        CtModel& mm = h.model();
        switch (pick) {
        case kCtmToggle: return ctModelToggleTriggers(mm, h);
        case kCtmDuplicate: return ctModelDuplicateTriggers(mm, h);
        case kCtmDelete: return ctModelDeleteTriggers(mm, h);
        case kCtmCopy: return ctCopy(h, true);
        case kCtmPaste: return ctPaste(h);
        }
    });
}
// The right-click menu of an entry's row: the row's commands (a block: its edit, no turn off).
inline void ctEntryMenu(CtPageHost& h, int kind, int i, const draw::View& from, draw::Point at) {
    CtModel& m = h.model();
    h.selectEntry(kind, i);
    const ClassicTrigger* t = ctModelSelected(m);
    if (!t || i < 0 || i >= ctEntryCount(*t, kind)) return;
    const bool block = kind && t->actions[size_t(i)].type == kClassicBlockType;
    const bool off = kind ? (t->actions[size_t(i)].flags & 2) != 0 : (t->conditions[size_t(i)].flags & 2) != 0;
    CtEntryMenuState st;
    st.isAction = kind != 0, st.block = block, st.off = off;
    st.canMoveOut = kind && ctCanMove(m, i, false), st.canMoveIn = kind && ctCanMove(m, i, true);
    st.canPaste = gCtClip.kind == (kind ? 1 : 0);
    h.menu(from, at, ctEntryMenuItems(st), [&h, kind, i](int pick) {
        switch (pick) {
        case kCtmBlock: return ctBatchClick(h, ClassicBatchKind::Unit, i);
        case kCtmToggle: return ctToggleEntry(h, kind, i);
        case kCtmOut: return ctMoveRow(h, i, false);
        case kCtmIn: return ctMoveRow(h, i, true);
        case kCtmDuplicate: return ctDuplicateEntry(h, kind, i);
        case kCtmDelete: return ctDeleteEntry(h, kind, i);
        case kCtmCopy: return ctCopy(h, false);
        case kCtmPaste: return ctPaste(h);
        }
    });
}

// The keys of a painted list: Delete, Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+D, Ctrl+Up and Ctrl+Down (move), Up and Down (select),
// Ctrl+Left and Ctrl+Right (an action out of or into a block). True when the key is one of them.
inline bool ctListKey(CtPageHost& h, bool isList, const draw::KeyEvent& e) {
    using draw::Key;
    if (!e.down) return false;
    CtModel& m = h.model();
    const bool ctrl = (e.mods & draw::kCtrl) != 0;
    const Key k = e.key;
    if (ctrl && k == Key('C')) return ctCopy(h, isList), true;
    if (ctrl && k == Key('X')) {  // cut: copy, then delete (a block with its children)
        if (isList) {
            if (m.sel < 0) return true;  // (no trigger selected: nothing to cut)
            ctCopy(h, true);
            return ctModelDeleteTriggers(m, h), true;  // (the checked ones, as the copy took them)
        }
        if (m.entry < 0) return true;
        ctCopy(h, false);
        return ctDeleteEntry(h, m.kind, m.entry), true;
    }
    if (ctrl && k == Key('V')) return ctPaste(h), true;
    if (isList) {  // the checked triggers (Ctrl or Shift click) when there are some, else the selected one
        if (k == Key::Delete) return ctModelDeleteTriggers(m, h), true;
        if (ctrl && k == Key('D')) return ctModelDuplicateTriggers(m, h), true;
        if (k != Key::Up && k != Key::Down) return false;
        const auto at = std::find(m.shown.begin(), m.shown.end(), m.sel);
        if (at == m.shown.end()) return true;
        const int i = int(at - m.shown.begin());
        const int to = i + (k == Key::Up ? -1 : 1);
        if (to < 0 || to >= int(m.shown.size())) return true;
        if (ctrl) ctModelMoveShown(m, h, i, to);
        else ctModelClickTrigger(m, h, m.shown[size_t(to)]);  // (Shift: the rows from the anchor to it)
        return true;
    }
    const ClassicTrigger* t = ctModelSelected(m);
    if (k != Key::Delete && k != Key::Up && k != Key::Down && !(ctrl && (k == Key::Left || k == Key::Right))) return false;
    if (!t || m.entry < 0) return true;
    if (k == Key::Delete) return ctDeleteEntry(h, m.kind, m.entry), true;
    if (ctrl && (k == Key::Left || k == Key::Right)) {  // Move in, Move out
        if (m.kind == 1) ctMoveRow(h, m.entry, k == Key::Right);
        return true;
    }
    const int to = m.entry + (k == Key::Up ? -1 : 1);
    if (ctrl) return ctMoveEntry(h, m.kind, m.entry, to), true;
    if (to >= 0 && to < ctEntryCount(*t, m.kind)) h.selectEntry(m.kind, to);
    else if (k == Key::Down && m.kind == 0 && t->actionCount()) h.selectEntry(1, 0);
    else if (k == Key::Up && m.kind == 1 && t->conditionCount()) h.selectEntry(0, t->conditionCount() - 1);
    return true;
}

// --- The lists' program part ------------------------------------------------------------------------------------------

// The two painted lists' host (core/views_classic_triggers.inc: CtListView): the page's state and its commands, the same
// on both systems. The page sets `list` and `entries` to its two list views (the menus and pickers open from them).
class CtPageList final : public CtListHost {
public:
    explicit CtPageList(CtPageHost& h) : h_(h) {}
    const draw::View* list = nullptr;
    const draw::View* entries = nullptr;

    const std::vector<ClassicTrigger>& triggers() const override { return h_.model().trig.triggers; }
    const std::vector<int>& shown() const override { return h_.model().shown; }
    const ClassicTrigger* selectedTrigger() const override { return ctModelSelected(h_.model()); }
    int selectedIndex() const override { return h_.model().sel; }
    bool marked(int i) const override { return h_.model().marked.count(i) != 0; }
    int entryKind() const override { return h_.model().kind; }
    int entryIndex() const override { return h_.model().entry; }
    bool collapsed(int i) const override { return h_.model().collapsed.count(i) != 0; }
    ClassicNames names() const override { return h_.names(); }
    std::wstring chipText(const ClassicParam& p, uint32_t v) override {
        return ctChipTextOf(p, v, h_.names(), !ctPickerIsList(p.kind) || ctIsChoiceOf(h_.editor(), h_.model().ai, p.kind, v));
    }
    std::wstring chipTip(const ClassicParam& p) override { return ctKindLabel(p.kind, p.name); }
    void clickTrigger(int i) override { ctModelClickTrigger(h_.model(), h_, i); }
    void toggleTrigger(int i) override { ctModelToggleOne(h_.model(), h_, i); }
    void duplicateTrigger(int i) override { ctModelDuplicateTrigger(h_.model(), h_, i); }
    void deleteTrigger(int i) override { ctModelDeleteTrigger(h_.model(), h_, i); }
    void moveShown(int from, int to) override { ctModelMoveShown(h_.model(), h_, from, to); }
    void addTrigger() override { ctModelAddTrigger(h_.model(), h_); }
    void triggerMenu(int i, draw::Point at) override {
        if (list) ctTriggerMenu(h_, i, *list, at);
    }
    void selectEntry(int kind, int i) override { h_.selectEntry(kind, i); }
    void toggleEntry(int kind, int i) override { ctToggleEntry(h_, kind, i); }
    void duplicateEntry(int kind, int i) override { ctDuplicateEntry(h_, kind, i); }
    void deleteEntry(int kind, int i) override { ctDeleteEntry(h_, kind, i); }
    void moveEntry(int kind, int from, int to) override { ctMoveEntry(h_, kind, from, to); }
    void moveRow(int i, bool in) override { ctMoveRow(h_, i, in); }
    void editBlock(int i) override {
        h_.selectEntry(1, i);
        ctBatchClick(h_, ClassicBatchKind::Unit, i);
    }
    void entryMenu(int kind, int i, draw::Point at) override {
        if (entries) ctEntryMenu(h_, kind, i, *entries, at);
    }
    void addClick(int kind, draw::Rect link) override {
        if (entries) ctAddClick(h_, kind, *entries, link);
    }
    void chipClick(int kind, int i, int pi, draw::Rect chip) override {
        if (entries) ctChipClick(h_, kind, i, pi, *entries, chip);
    }
    void toggleCollapsed(int i) override {
        CtModel& m = h_.model();
        if (m.collapsed.count(i)) m.collapsed.erase(i);
        else m.collapsed.insert(i);
        h_.changed();
    }
    void entriesLaid(int /*height*/) override { h_.entriesLaid(); }
    bool listKey(bool isList, const draw::KeyEvent& e) override { return ctListKey(h_, isList, e); }

private:
    CtPageHost& h_;
};

// --- The side and the editor (kit views) ------------------------------------------------------------------------------

// The page's sizes (96-dpi pixels; the native page's): the search's row and the filter's row at the left, the editor's
// title row and its values (a label and a field; two in a row, a text takes a row).
namespace ctLayoutPx {
constexpr int kSearchCut = 92, kCountW = 84, kCountDown = 4;  // the search is the column less 92; the count's 84 px at the right, 4 px down
constexpr int kFilterLabelW = 150;                            // "Show the triggers of"
constexpr int kEditTitleW = 360, kEditTitleH = 28;            // the editor's title ("Action 1 of 2")
constexpr int kTypeX = 370, kTypeW = 200, kTurnOffX = 580, kTurnOffW = 110, kAlwaysX = 700, kAlwaysW = 140, kTitleRowDown = 2;
constexpr int kValueLabelW = 110, kValueW = 260, kValueColGap = 24, kTextMaxW = 560, kTextCut = 16;
}  // namespace ctLayoutPx

// A value's field in the editor: the value's text in a field box with a chevron; a click (Space, Enter) opens its list or
// pop-out. (The native page's value buttons were drawn so: paintDarkButton's BS_LEFT face.)
class CtPickButton final : public draw::View {
public:
    std::function<void()> onClick;
    void setText(std::wstring t) {
        text_ = std::move(t);
        invalidate();
    }
    const std::wstring& text() const { return text_; }
    void setTip(std::wstring t) { tip_ = std::move(t); }

    void paint(draw::Canvas& c) override {
        const draw::Rect r{0, 0, width(), height()};
        c.fill(r, kBg);  // (the page's ground around the round corners)
        kitpaint::drawFieldBox(c, r, hasFocus() ? kitpaint::Field::Focus : hot_ ? kitpaint::Field::Hot : kitpaint::Field::Normal);
        c.text({r.left + scale(6), r.top, r.right - scale(24), r.bottom}, text_, kText, draw::kMiddle | draw::kEllipsis);
        kitpaint::drawGlyph(c, {r.right - scale(20), r.top, r.right - scale(6), r.bottom}, UiIcon::Dropdown, hot_ ? kText : kDim, 9);
    }
    bool mouse(const draw::MouseEvent& e) override {
        const bool inside = e.pt.x >= 0 && e.pt.y >= 0 && e.pt.x < width() && e.pt.y < height();
        switch (e.kind) {
        case draw::MouseKind::Move:
            if (inside != hot_) hot_ = inside, invalidate();
            return true;
        case draw::MouseKind::Leave:
            if (hot_ && !down_) hot_ = false, invalidate();
            return true;
        case draw::MouseKind::Down:
        case draw::MouseKind::DoubleClick:
            if (e.button != 0) return false;
            down_ = true;
            if (draw::Host* h = host()) h->capture(*this, true), h->focus(*this);
            return true;
        case draw::MouseKind::Up:
            if (e.button != 0 || !down_) return false;
            down_ = false;
            if (draw::Host* h = host()) h->capture(*this, false);
            hot_ = inside;
            invalidate();
            if (inside && onClick) onClick();
            return true;
        default: return false;
        }
    }
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || (e.key != draw::Key::Space && e.key != draw::Key::Enter)) return false;
        if (onClick) onClick();
        return true;
    }
    draw::Cursor cursor(draw::Point) override { return draw::Cursor::Hand; }
    std::wstring tip(draw::Point) override { return tip_; }
    bool focusable() const override { return true; }
    void focusChanged(bool) override { invalidate(); }

private:
    std::wstring text_, tip_;
    bool hot_ = false, down_ = false;
};

// The side of the page above the trigger list: the search box (the triggers whose text has its words), the count of the
// triggers shown, and the filter (the triggers of a player). The view is the left column's width and two rows high.
class CtSideView final : public draw::View {
public:
    CtSideView(const std::vector<std::wstring>& groupNames, const std::vector<int>& groups) : groups_(groups) {
        search.setPlaceholder(L"Search triggers");
        search.onChange = [this] {
            if (onSearch) onSearch(search.text());
        };
        std::vector<stardraft::kit::DropdownItem> items;
        for (const std::wstring& n : groupNames) items.push_back({n});
        filter.setItems(std::move(items));
        filter.setValue(0);
        filter.onChange = [this](int i) {
            if (onFilter && i >= 0 && i < int(groups_.size())) onFilter(groups_[size_t(i)]);
        };
        add(search, {0, 0, 0, 0});
        add(filter, {0, 0, 0, 0});
    }
    SearchBox search;
    stardraft::kit::Dropdown filter;
    std::function<void(const std::wstring&)> onSearch;
    std::function<void(int group)> onFilter;  // -1: all triggers

    void setCount(std::wstring t) {
        count_ = std::move(t);
        invalidate();
    }
    // Picks the filter's item of this group (-1: all), as a click in its list does.
    void pickGroup(int group) {
        for (size_t i = 0; i < groups_.size(); ++i)
            if (groups_[i] == group) {
                filter.setValue(int(i));
                if (onFilter) onFilter(group);
                return;
            }
    }

    void resized() override {
        using namespace ctLayoutPx;
        const int W = width(), rowH = scale(stardraft::kit::kFieldH), step = scale(stardraft::kit::kFormRowH);
        search.setFrame({0, 0, std::max(0, W - scale(kSearchCut)), rowH});
        filter.setFrame({scale(kFilterLabelW), step, W, step + rowH});
    }
    void paint(draw::Canvas& c) override {
        using namespace ctLayoutPx;
        const int W = width(), rowH = scale(stardraft::kit::kFieldH), step = scale(stardraft::kit::kFormRowH);
        c.fill({0, 0, W, height()}, kBg);
        c.text({W - scale(kCountW), scale(kCountDown), W, scale(kCountDown) + rowH}, count_, kDim, draw::kRight | draw::kMiddle | draw::kEllipsis);
        c.text({0, step, scale(kFilterLabelW), step + rowH}, L"Show the triggers of", kDim, draw::kMiddle | draw::kEllipsis);
    }

private:
    std::vector<int> groups_;
    std::wstring count_;
};

// The editor under the entries: the selected entry's title, its type, Turn off (and Always show for the actions that
// have it), and a field for each value (two in a row; a text takes the width under them). A block's row shows its title
// only. show() reads the selected entry from the page's model; the callbacks are the page's commands.
class CtEditorView final : public draw::View {
public:
    struct Callbacks {
        std::function<void(uint8_t type)> type;
        std::function<void(uint8_t bit, bool on)> flag;  // 2: Turn off, 4: Always show
        std::function<void(int param, const draw::View& field)> value;
    };
    explicit CtEditorView(Callbacks cb) : cb_(std::move(cb)) {
        title.setFont({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)});  // the page's title font (ui::titleFont)
        type.onChange = [this](int i) {
            if (cb_.type && i >= 0 && i < int(types_.size())) cb_.type(types_[size_t(i)]);
        };
        disabled.setPlainLabel(true), always.setPlainLabel(true);  // (the native check boxes' labels were in the text color)
        disabled.setTip(L"The condition or action does nothing in the game (Turn off)");
        always.setTip(L"The game's Always display flag for this action");
        disabled.onChange = [this](bool on) {
            if (cb_.flag) cb_.flag(2, on);
        };
        always.onChange = [this](bool on) {
            if (cb_.flag) cb_.flag(4, on);
        };
        add(title, {0, 0, 0, 0});
        add(type, {0, 0, 0, 0});
        add(disabled, {0, 0, 0, 0});
        add(always, {0, 0, 0, 0});
    }

    stardraft::kit::Label title;
    stardraft::kit::Dropdown type;
    stardraft::kit::CheckBox disabled{L"Turn off"}, always{L"Always show"};

    // The field of parameter `param` (nullptr: none shows).
    CtPickButton* field(int param) {
        for (Value& v : values_)
            if (v.param == param) return v.field.get();
        return nullptr;
    }
    // Picks the type's item of this type, as a click in its list does.
    void pickType(uint8_t t) {
        for (size_t i = 0; i < types_.size(); ++i)
            if (types_[i] == t) {
                type.setValue(int(i));
                if (cb_.type) cb_.type(t);
                return;
            }
    }

    // The selected entry of the page's model (ctBuildEditor of the native page).
    void show(CtPageHost& h) {
        CtModel& m = h.model();
        for (Value& v : values_) remove(*v.label), remove(*v.field);
        for (Value& v : values_) retired_.push_back(std::move(v));  // (a field's click may be the one that runs: it goes at the next paint)
        values_.clear();
        ClassicCondition* c = ctModelCondition(m);
        ClassicAction* a = ctModelAction(m);
        // A block's row has no type and no values of its own: its title says what it repeats, and its controls are not shown.
        const bool block = a && a->type == kClassicBlockType && a->batch;
        const bool any = (c || a) && !block;
        title.setVisible(any || block);
        type.setVisible(any), disabled.setVisible(any);
        always.setVisible(a && !block && (a->type == 7 || a->type == 9));
        if (block) {
            title.setText(widen(classicBatchText(*a->batch, h.names())));
            place();
            return;
        }
        if (!any) return place();
        title.setText(ctEntryTitle(m, c != nullptr));
        const uint8_t t = c ? c->type : a->type;
        const uint8_t flags = c ? c->flags : a->flags;
        std::vector<stardraft::kit::DropdownItem> items;
        types_.clear();
        int cur = -1;
        for (const ClassicDef* def : c ? classicConditionDefs() : classicActionDefs()) {
            if (def->type == t) cur = int(items.size());
            items.push_back({widen(def->name)});
            types_.push_back(def->type);
        }
        if (cur < 0) {  // a type the game does not have: shown, kept
            cur = int(items.size());
            items.push_back({L"Type " + std::to_wstring(t) + L" (not a game type)"});
            types_.push_back(t);
        }
        type.setItems(std::move(items));
        type.setValue(cur);
        disabled.setOn((flags & 2) != 0);
        always.setOn((flags & 4) != 0);
        // The fields: two columns; a text takes the whole width under them.
        const ClassicDef* def = c ? classicConditionDef(t) : classicActionDef(t);
        std::vector<int> order;  // the texts last
        if (def) {
            for (int i = 0; i < int(def->params.size()); ++i)
                if (def->params[size_t(i)].kind != ClassicKind::Text) order.push_back(i);
            for (int i = 0; i < int(def->params.size()); ++i)
                if (def->params[size_t(i)].kind == ClassicKind::Text) order.push_back(i);
        }
        for (int i : order) {
            const ClassicParam& p = def->params[size_t(i)];
            const int varDepth = ctVarDepthOf(m, c, p.field);
            const uint32_t v = varDepth >= 0 ? kCtVariable + uint32_t(varDepth) : c ? classicValue(*c, p.field) : classicValue(*a, p.field);
            Value val;
            val.param = i, val.text = p.kind == ClassicKind::Text;
            val.label = std::make_unique<stardraft::kit::Label>(ctKindLabel(p.kind, p.name), stardraft::kit::LabelTone::Dim);
            val.field = std::make_unique<CtPickButton>();
            val.field->setText(ctEditorValueText(h, p, v, varDepth));
            CtPickButton* f = val.field.get();
            val.field->onClick = [this, i, f] {
                if (cb_.value) cb_.value(i, *f);
            };
            add(*val.label, {0, 0, 0, 0});
            add(*val.field, {0, 0, 0, 0});
            values_.push_back(std::move(val));
        }
        place();
    }

    void resized() override { place(); }
    void paint(draw::Canvas& c) override {
        retired_.clear();
        c.fill({0, 0, width(), height()}, kBg);
    }

private:
    struct Value {
        int param = -1;
        bool text = false;
        std::unique_ptr<stardraft::kit::Label> label;
        std::unique_ptr<CtPickButton> field;
    };
    // The parts at their places (the native page's: ctLayout and ctBuildEditor), from the view's top left corner.
    void place() {
        using namespace ctLayoutPx;
        const int rowH = scale(stardraft::kit::kFieldH), step = scale(stardraft::kit::kFormRowH), gap = scale(8);
        auto put = [](draw::View& v, int x, int y, int w, int hh) { v.setFrame({x, y, x + std::max(0, w), y + std::max(0, hh)}); };
        put(title, 0, 0, scale(kEditTitleW), scale(kEditTitleH));
        put(type, scale(kTypeX), scale(kTitleRowDown), scale(kTypeW), rowH);
        put(disabled, scale(kTurnOffX), scale(kTitleRowDown), scale(kTurnOffW), rowH);
        put(always, scale(kAlwaysX), scale(kTitleRowDown), scale(kAlwaysW), rowH);
        const int labelW = scale(kValueLabelW), pickW = scale(kValueW), colW = labelW + pickW + scale(kValueColGap);
        int col = 0, y = step + gap;
        for (Value& v : values_) {
            if (v.text && col) col = 0, y += step;
            const int x = col * colW;
            const int w = v.text ? std::min(width() - labelW - scale(kTextCut), scale(kTextMaxW)) : pickW;
            put(*v.label, x, y, labelW, rowH);
            put(*v.field, x + labelW, y, w, rowH);
            if (++col == 2 || v.text) col = 0, y += step;
        }
    }

    Callbacks cb_;
    std::vector<uint8_t> types_;  // the type of each item of `type`
    std::vector<Value> values_, retired_;
};

// --- The page's layout ------------------------------------------------------------------------------------------------

// The page's parts at a size W x H (the native page's ctLayout): the left column (the side, the list, the note under it)
// and the right one (the head, the entries, the editor under them). The entries take the height their rows need, up to
// the room left. `needH`: the least height the parts take (a shorter page is made this tall, and its view scrolls).
struct CtPageRects {
    draw::Rect side, list, note, head, entries, editor;
    int rightW = 0, needH = 0;
};
inline CtPageRects ctPageLayout(int W, int H, bool noteShown, int entriesContentH, int editorH) {
    CtPageRects L;
    const int pad = scale(stardraft::kit::kDialogPad), rowH = scale(stardraft::kit::kFieldH), step = scale(stardraft::kit::kFormRowH), gap = scale(8);
    L.needH = pad + scale(40) + 2 * step + gap + scale(120) + gap + step * 4 + gap + pad;
    const int leftW = std::clamp(W * 3 / 10, scale(280), scale(460));
    const int x0 = pad + leftW + scale(24), rightW = std::max(scale(600), W - x0 - pad);
    L.rightW = rightW;
    // Left: the search and the filter, the list, the note.
    int y = pad;
    L.side = {pad, y, pad + leftW, y + step + rowH};
    y += 2 * step;
    const int noteH = noteShown ? scale(44) : 0;
    const int listBottom = H - pad - (noteShown ? noteH + gap : 0);
    L.list = {pad, y, pad + leftW, std::max(y, listBottom)};
    L.note = {pad, listBottom + gap, pad + leftW, listBottom + gap + noteH};
    // Right: the head (the header, the players and Forces), the entries, the editor.
    const int headH = scale(40) + 2 * step;
    L.head = {x0, pad, x0 + rightW, pad + headH};
    y = pad + headH + gap;
    const int room = H - pad - editorH - gap - y;
    const int entriesH = std::clamp(entriesContentH, scale(120), std::max(scale(120), room));
    L.entries = {x0, y, x0 + rightW, y + entriesH};
    const int ey = y + entriesH + gap;
    L.editor = {x0, ey, x0 + rightW, ey + editorH};
    return L;
}
// The head's parts in the head (its coordinates, from 0) at the right column's width `rightW`.
inline void ctPlaceHead(CtHeadView& hv, int rightW) {
    const int rowH = scale(stardraft::kit::kFieldH), step = scale(stardraft::kit::kFormRowH), gap = scale(8);
    const int delW = scale(150), turnW = scale(96);
    const int headW = rightW - delW - turnW - 3 * gap;
    auto put = [](draw::View& v, int px, int py, int w, int hh) { v.setFrame({px, py, px + std::max(0, w), py + std::max(0, hh)}); };
    put(hv.title, 0, 0, std::min(headW, scale(150)), scale(28));
    put(hv.count, scale(160), scale(6), std::max(0, headW - scale(160)), rowH);
    put(hv.deleteTrig, rightW - delW, scale(2), delW, rowH);
    put(hv.turnOff, rightW - delW - gap - turnW, scale(2), turnW, rowH);
    // The players: check boxes in columns of a fixed width, from the left; All players and Forces in the next row.
    const int colW = scale(124);
    for (int g = 0; g < 8; ++g) put(*hv.players[size_t(g)], g * colW, scale(40), colW - gap, rowH);
    put(*hv.players[8], 0, scale(40) + step, colW - gap, rowH);
    put(hv.forces, colW, scale(40) + step, scale(120), rowH);
}
// The head's texts for the selected trigger: its name, its count, the Turn off button and the buttons' states.
inline void ctUpdateHead(CtHeadView& h, const CtModel& m) {
    const ClassicTrigger* t = ctModelSelected(m);
    h.title.setText(t ? tr(L"Trigger ") + std::to_wstring(m.sel + 1) : std::wstring(L"None"));
    h.count.setText(!t ? std::wstring() : widen(std::string(t->disabled() ? "Turned off, " : "") + std::to_string(t->actionCount()) + (t->actionCount() == 1 ? " action" : " actions")));
    h.turnOff.setText(t && t->disabled() ? L"Turn on" : L"Turn off");
    for (stardraft::kit::Button* b : {&h.turnOff, &h.deleteTrig, &h.forces}) b->setEnabled(t != nullptr);
}
// The head's player boxes for the selected trigger.
inline void ctShowPlayers(CtHeadView& h, const CtModel& m) {
    const ClassicTrigger* t = ctModelSelected(m);
    for (int g = 0; g < kCtBoxCount; ++g) {
        h.players[size_t(g)]->setOn(t && t->runsFor(kCtBoxGroups[g]));
        h.players[size_t(g)]->setEnabled(t != nullptr);
    }
}
// The filter's items: All triggers, then each group by its name.
inline void ctFilterItems(const ClassicNames& n, std::vector<std::wstring>& names, std::vector<int>& groups) {
    names = {L"All triggers"}, groups = {-1};
    for (int g : kCtGroups) names.push_back(widen(classicPlayerName(uint32_t(g), n))), groups.push_back(g);
}
// The note under the list: a project's map (L.4), the EUD values the triggers hold (they stay as they are).
inline std::wstring ctNoteText(const CtModel& m, bool project) {
    std::wstring note;
    if (project) note = L"The project's map has these classic triggers. The built map keeps them; they run with the project's triggers.";
    int eud = 0;
    for (const ClassicTrigger& t : m.trig.triggers) {
        for (int i = 0; i < t.conditionCount(); ++i) eud += classicEud(t.conditions[size_t(i)]);
        for (int i = 0; i < t.actionCount(); ++i) eud += classicEud(t.actions[size_t(i)]);
    }
    if (eud) note += (note.empty() ? L"" : L" ") + std::to_wstring(eud) + L" conditions and actions have EUD values. They stay as they are.";
    return note;
}
