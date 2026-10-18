// Quick add in the trigger editors (roadmap R61; portable, core.inc): Q (or Edit > Quick picker) opens the quick picker
// with the blocks (the Triggers workspace) or the conditions and actions (the Classic triggers page). Type a part of
// the name, Enter: it goes in after the selected one, as the Add menu puts it.

// A row of the quick picker: its name, its group under it, and the words it is found by (lower case).
inline PaletteUnit quickRow(int value, const std::wstring& label, const std::wstring& section, const std::wstring& more = {}) {
    PaletteUnit u;
    u.value = value, u.label = label, u.section = section;
    u.hay = label + L" " + section + L" " + more;
    std::transform(u.hay.begin(), u.hay.end(), u.hay.begin(), ::towlower);
    return u;
}

// --- The Triggers workspace -------------------------------------------------------------------------------------------

// The blocks that the Add menu offers (conditions and actions; no values), by their place in TriggerDef::all().
std::vector<PaletteUnit> trigQuickAddRows() {
    std::vector<PaletteUnit> out;
    const auto& all = TriggerDef::all();
    for (size_t i = 0; i < all.size(); ++i) {
        const TriggerDef& d = all[i];
        if (d.value || d.tier == "replaced" || d.tier == "hidden") continue;
        out.push_back(quickRow(int(i), widen(d.title), std::wstring(tr(d.condition ? L"Condition" : L"Action")) + L"  \x00B7  " + tr(widen(d.category).c_str()),
                               widen(d.sentence)));
    }
    return out;
}

void trigQuickAdd() {
    if (!trigSelected()) return setMessage(tr(L"Select a trigger first"));
    gQuickCustom.cue = tr(L"Add a block after the selected one");
    gQuickCustom.icon = UiIcon::Triggers;
    gQuickCustom.rows = trigQuickAddRows;
    gQuickCustom.run = [](int v) {
        const auto& all = TriggerDef::all();
        if (v < 0 || v >= int(all.size()) || !trigSelected()) return;
        const TriggerDef& d = all[size_t(v)];
        const auto [ref, at] = trigAddTarget(d.condition);
        trigAddTo(ref, at, d);
    };
    shellShowQuickPickerCustom();
}

// --- The Classic triggers page ----------------------------------------------------------------------------------------

// A new condition (kind 0) or action (kind 1) of this type after the selected one of its kind (an action after the
// selected action's block, at its depth); else at the end of its list.
inline void ctAddAfterSelected(CtPageHost& h, int kind, uint8_t type) {
    CtModel& m = h.model();
    const ClassicTrigger* t = ctModelSelected(m);
    if (!t) return h.message(tr(L"Select a trigger first"));
    bool ok;
    if (kind) {
        int at = -1, depth = 0;
        if (m.kind == 1 && m.entry >= 0 && m.entry < t->actionCount()) at = t->blockEnd(m.entry), depth = t->actions[size_t(m.entry)].depth;
        ClassicAction a = makeClassicAction(type);
        a.depth = uint8_t(depth);
        ok = ctInsertAction(h, a, at);
    } else {
        ok = ctInsertCondition(h, makeClassicCondition(type), m.kind == 0 && m.entry >= 0 ? m.entry + 1 : -1);
    }
    if (!ok) h.message(tr(L"The list is full"));
}

// The conditions, then the actions: the value is kind * 256 + type.
inline std::vector<PaletteUnit> ctQuickAddRows() {
    std::vector<PaletteUnit> out;
    for (int kind = 0; kind < 2; ++kind)
        for (const ClassicDef* def : kind ? classicActionDefs() : classicConditionDefs())
            out.push_back(quickRow(kind * 256 + def->type, widen(def->name), tr(kind ? L"Action" : L"Condition")));
    return out;
}

inline void ctQuickAdd(CtPageHost& h) {
    if (!ctModelSelected(h.model())) return h.message(tr(L"Select a trigger first"));
    gQuickCustom.cue = tr(L"Add a condition or an action after the selected one");
    gQuickCustom.icon = UiIcon::Triggers;
    gQuickCustom.rows = ctQuickAddRows;
    gQuickCustom.run = [&h](int v) { ctAddAfterSelected(h, v / 256, uint8_t(v % 256)); };
    shellShowQuickPickerCustom();
}
