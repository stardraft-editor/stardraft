// Quick add and Find in triggers (roadmap R61, R60; portable, core.inc): Q (or Edit > Quick picker) opens the quick picker
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

// --- Trigger search (roadmap R60) -------------------------------------------------------------------------------------
// Edit > Find in triggers (Ctrl+Shift+F) in the Triggers workspace and on the Classic triggers page: the quick picker
// lists every trigger, condition, action and block (inside other blocks too), each with its trigger under it, found by
// the words of its sentence and its comment. A pick goes to it: the editor shows and selects it. The list of triggers
// stays as it is.

// A block's sentence with its values, as the pane reads it (a value block: its title).
inline std::wstring trigCallSentence(const TriggerDef& d, const TriggerCall& call) {
    const std::string s = d.shownSentence(call.args);
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '{') {
            const size_t e = s.find('}', i);
            if (e != std::string::npos) {
                const int p = d.param(s.substr(i + 1, e - i - 1));
                if (p >= 0 && size_t(p) < call.argBlocks.size() && !call.argBlocks[size_t(p)].empty()) {
                    const TriggerDef* v = TriggerDef::find(call.argBlocks[size_t(p)][0].def);
                    out += v ? widen(v->title) : L"(a value)";
                } else if (p >= 0) {
                    out += trigValueText(d.params[size_t(p)].type, size_t(p) < call.args.size() ? call.args[size_t(p)] : std::string());
                }
                i = e + 1;
                continue;
            }
        }
        const size_t n = s.find('{', i + 1);
        out += widen(s.substr(i, n == std::string::npos ? std::string::npos : n - i));
        i = n == std::string::npos ? s.size() : n;
    }
    if (!out.empty()) out[0] = wchar_t(towupper(out[0]));
    return out;
}

struct TrigFindTarget {
    std::vector<int> path;  // the trigger
    TrigBlockRef ref;       // the block (index -1: the trigger itself)
};
inline std::vector<TrigFindTarget> gTrigFindTargets;

inline std::vector<PaletteUnit> trigFindRows() {
    std::vector<PaletteUnit> out;
    gTrigFindTargets.clear();
    std::function<void(const TriggerNode&, std::vector<int>&, const std::wstring&)> walk = [&](const TriggerNode& n, std::vector<int>& path,
                                                                                                 const std::wstring& where) {
        if (n.kind == TriggerNode::Kind::Trigger) {
            const std::wstring name = widen(n.name);
            gTrigFindTargets.push_back({path, {{{}, 1}, -1}});
            out.push_back(quickRow(int(gTrigFindTargets.size()) - 1, name, std::wstring(tr(L"Trigger")) + (where.empty() ? L"" : L"  \x00B7  " + where), widen(n.comment)));
            std::function<void(const std::vector<TriggerCall>&, const TrigListRef&)> calls = [&](const std::vector<TriggerCall>& list, const TrigListRef& ref) {
                for (size_t i = 0; i < list.size(); ++i) {
                    const TriggerCall& c = list[i];
                    const TriggerDef* d = TriggerDef::find(c.def);
                    if (!d) continue;
                    const TrigBlockRef b{ref, int(i)};
                    gTrigFindTargets.push_back({path, b});
                    out.push_back(quickRow(int(gTrigFindTargets.size()) - 1, trigCallSentence(*d, c),
                                           name + L"  \x00B7  " + tr(d->condition ? L"Condition" : L"Action") + L"  \x00B7  " + widen(d->title)));
                    for (size_t k = 0; k < c.lists.size(); ++k) calls(c.lists[k], trigInside(b, int(k)));
                }
            };
            calls(n.conditions, {{}, 0});
            calls(n.actions, {{}, 1});
            return;
        }
        if (n.kind == TriggerNode::Kind::Variable) return;
        const std::wstring here = where.empty() ? widen(n.name) : where + L" / " + widen(n.name);
        for (size_t i = 0; i < n.children.size(); ++i) {
            path.push_back(int(i));
            walk(n.children[i], path, here);
            path.pop_back();
        }
    };
    for (size_t r = 0; r < gTE.set.roots.size(); ++r) {
        std::vector<int> path{int(r)};
        walk(gTE.set.roots[r], path, L"");
    }
    return out;
}

void trigFind() {
    gQuickCustom.cue = tr(L"Find a trigger, a condition, an action or a block");
    gQuickCustom.icon = UiIcon::Search;
    gQuickCustom.rows = trigFindRows;
    gQuickCustom.run = [](int v) {
        if (v < 0 || v >= int(gTrigFindTargets.size())) return;
        const TrigFindTarget t = gTrigFindTargets[size_t(v)];
        if (!gTE.set.at(t.path)) return;
        trigRefreshTree(t.path);
        gTP.sel = t.ref, gTP.focus = {}, gTP.focus.ref.index = -1;
        gTE.scroll = 0;
        if (t.ref.index >= 0) trigRevealBlock(t.ref);
        trigPaneRepaint();
        trigInfoRepaint();
    };
    shellShowQuickPickerCustom();
}

// The Classic triggers page: each trigger (its conditions' line), condition and action.
struct CtFindTarget {
    int trigger, kind, entry;  // kind -1: the trigger
};
inline std::vector<CtFindTarget> gCtFindTargets;

inline void ctFind(CtPageHost& h) {
    gQuickCustom.cue = tr(L"Find a trigger, a condition or an action");
    gQuickCustom.icon = UiIcon::Search;
    gQuickCustom.rows = [&h] {
        std::vector<PaletteUnit> out;
        gCtFindTargets.clear();
        const CtModel& m = h.model();
        const ClassicNames n = h.names();
        for (int i = 0; i < int(m.trig.triggers.size()); ++i) {
            const ClassicTrigger& t = m.trig.triggers[size_t(i)];
            const std::wstring title = std::wstring(tr(L"Trigger")) + L" " + std::to_wstring(i + 1);
            gCtFindTargets.push_back({i, -1, -1});
            const std::wstring conditions = t.conditionCount() ? ctTriggerLineOf(t, false, n) : std::wstring(tr(L"Always"));  // (as the list shows it)
            out.push_back(quickRow(int(gCtFindTargets.size()) - 1, title + L": " + conditions, ctTriggerLineOf(t, true, n), ctSearchTextOf(t, n)));
            for (int j = 0; j < t.conditionCount(); ++j) {
                gCtFindTargets.push_back({i, 0, j});
                out.push_back(quickRow(int(gCtFindTargets.size()) - 1, widen(classicConditionText(t.conditions[size_t(j)], n)), title + L"  \x00B7  " + tr(L"Condition")));
            }
            for (int j = 0; j < t.actionCount(); ++j) {
                const ClassicAction& a = t.actions[size_t(j)];
                const std::vector<const ClassicBatch*> scope = ctScopeOf(t, j);
                const std::string text = a.type == kClassicBlockType ? classicBatchText(*a.batch, n) : classicActionText(a, n, &scope);
                gCtFindTargets.push_back({i, 1, j});
                out.push_back(quickRow(int(gCtFindTargets.size()) - 1, widen(text), title + L"  \x00B7  " + tr(L"Action")));
            }
        }
        return out;
    };
    gQuickCustom.run = [&h](int v) {
        if (v < 0 || v >= int(gCtFindTargets.size())) return;
        const CtFindTarget t = gCtFindTargets[size_t(v)];
        CtModel& m = h.model();
        if (t.trigger >= int(m.trig.triggers.size())) return;
        if (std::find(m.shown.begin(), m.shown.end(), t.trigger) == m.shown.end()) h.queryCleared();  // (hidden by the filter or the search)
        if (std::find(m.shown.begin(), m.shown.end(), t.trigger) == m.shown.end()) {  // under a folded heading: it opens
            for (int j = t.trigger - 1; j >= 0; --j)
                if (ctIsHeading(m.trig.triggers[size_t(j)])) {
                    m.folded.erase(m.trig.triggers[size_t(j)].actions[0].text);
                    break;
                }
            ctModelFill(m, h);
        }
        h.selectTrigger(t.trigger);
        if (t.kind >= 0) h.selectEntry(t.kind, t.entry);
    };
    shellShowQuickPickerCustom();
}
