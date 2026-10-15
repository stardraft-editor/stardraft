// The Triggers workspace's help and code (portable, core.inc; main.cpp includes it before trigger_help.inc):
// - Block help (T.7): the texts of a block's help page and its view (BlockHelpView): what the block does, its values
//   and their types, an example, what to watch out for, its effects and cost. The program shows the view in a window
//   of its own beside the pane or the block picker (the Windows program paints the same page in trigger_help.inc).
// - The code pane: the epScript the triggers build to (trigGeneratedCode), the lines of the selected trigger
//   (trigCodeMarksFor) and its view (TrigCodeView: the code in epScript's colors, read only, with those lines tinted).
// - Build errors (T.1): Build keeps the block of each line of generated/triggers.eps (trigKeepCodeMap); a line names
//   its trigger and block (trigCodeLineName), and a euddraft error in a line selects that block (trigShowCodeLine).

void setWorkspace(Workspace w);
void trigRefreshTree(std::vector<int> select);
void trigPaneRepaint();
void trigInfoRepaint();
// The program's part: the pane scrolls so that block `ref` of the selected trigger shows (after the pane's layout; the
// Windows program lays the pane out at once, the SDL program after its next frame). See trigScrollToBlock.
void trigRevealBlock(const TrigBlockRef& ref);

// --- Block help ------------------------------------------------------------------------------------------------

std::wstring blockKindText(const TriggerDef& d) { return d.condition ? L"condition" : d.value ? L"value" : L"action"; }

// The words for a block's effects.
std::vector<std::wstring> blockEffectTexts(const TriggerDef& d) {
    std::vector<std::wstring> out;
    if (d.has("local")) out.push_back(L"Only the player on this computer sees or hears it.");
    if (d.has("cp"))
        out.push_back(d.param("who") >= 0 ? L"It acts for the player that you select. The current player has a value only in For each player."
                                          : L"It acts for the current player. Put it in For each player.");
    if (d.has("blocking")) out.push_back(L"It holds up the other triggers of the player until it is complete.");
    if (d.has("memory")) out.push_back(L"It reads or writes game memory at an address.");
    if (d.has("unitloop")) out.push_back(L"It goes through units, up to 1,700 each time it runs.");
    if (d.has("theunit")) out.push_back(L"Inside it, \"the unit\" is the unit that the loop handles now.");
    if (d.has("counter")) out.push_back(L"Inside it, counter is the round number (0 is the first), the number, or the index of the item.");
    if (d.has("curplayer")) out.push_back(L"Inside it, the current player is the player that the current run is for.");
    if (d.has("differs")) out.push_back(L"Its value is not the same on each computer. Use it only for what a player sees or hears.");
    return out;
}

// The sentence with each value's name in brackets: "create [n] [unit] at [where] for [player]".
std::wstring blockHelpSentence(const TriggerDef& d) {
    std::wstring s;
    for (size_t i = 0; i < d.sentence.size();) {
        if (d.sentence[i] == '{') {
            const size_t end = d.sentence.find('}', i);
            if (end != std::string::npos) {
                s += L"[" + widen(d.sentence.substr(i + 1, end - i - 1)) + L"]";
                i = end + 1;
                continue;
            }
        }
        s += wchar_t(static_cast<unsigned char>(d.sentence[i++]));
    }
    return s;
}

// The page's subtitle: the category, the kind, and "advanced" for an advanced block.
std::wstring blockHelpSubtitle(const TriggerDef& d) {
    std::wstring sub = widen(d.category) + L"  \x00B7  " + blockKindText(d);
    if (d.tier == "advanced") sub += L"  \x00B7  advanced";
    return sub;
}

// The line of a value on the page: "[name]  type, min to max  ·  default value: ...", and whether its type is clear.
std::wstring blockHelpValueLine(const TriggerParam& p, bool& clear) {
    std::wstring line = L"[" + widen(p.name) + L"]  " + trigTypeNote(p);
    if (p.max >= p.min) line += L", " + std::to_wstring(p.min) + L" to " + std::to_wstring(p.max);
    if (!p.def.empty() && p.type.rfind("ValueOf:", 0) != 0) line += L"  \x00B7  default value: " + trigValueText(p.type, p.def);
    clear = triggerTypeClear(p.type) || p.type.rfind("ValueOf:", 0) == 0;
    return line;
}

// What the block holds: "Then (actions), Else (actions)".
std::wstring blockHelpLists(const TriggerDef& d) {
    std::wstring lists;
    for (const auto& li : d.lists) lists += (lists.empty() ? L"" : L", ") + widen(li.title) + (li.condition ? L" (conditions)" : L" (actions)");
    return lists;
}

// The cost line: "About 3 actions each time it runs, and the blocks in it."
std::wstring blockHelpCost(const TriggerDef& d) {
    return L"About " + std::to_wstring(d.cost) + (d.cost == 1 ? L" action" : L" actions") + L" each time it runs" + (d.lists.empty() ? L"." : L", and the blocks in it.");
}

// F1 in the pane, with the help page `open` (showing block `shown`): the selected block's id to show; "" when the page
// closes (`close`: it is open and F1 is for the block it shows, or no block is selected; then the message says so).
std::string trigPaneHelpPick(bool open, const std::string& shown, bool& close) {
    close = false;
    TriggerNode* t = trigSelected();
    const TriggerCall* c = t ? trigBlock(*t, gTP.sel) : nullptr;
    if (!c) {
        close = open;
        setMessage(L"Select a block, then press F1 for its help");
        return "";
    }
    if (open && shown == c->def) return close = true, "";
    return c->def;
}

// The block help page: painted from the block library, so it works offline. The wheel scrolls it; Esc and F1 close it
// (onClose). setBlock shows another block from the top.
class BlockHelpView final : public draw::View {
public:
    std::function<void()> onClose;

    void setBlock(const std::string& id) {
        if (id != id_) id_ = id, scroll_ = 0;
        invalidate();
    }
    const std::string& block() const { return id_; }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        kitpaint::FormLayout l = kitpaint::formLayout(width());
        l.colRight = width() - l.x0;
        const int x = l.x0, right = l.colRight;
        int y = scale(14) - scroll_;
        auto text = [&](const std::wstring& t, draw::Color color) {  // a paragraph with word wrap; the y under it
            const int h = c.measureBox(t, std::max(1, right - x), draw::kWrap).y;
            c.text({x, y, right, y + h}, t, color, draw::kLeft | draw::kWrap);
            y += h + scale(8);
        };
        auto section = [&](const wchar_t* name) { y = kitpaint::drawSectionHeader(c, l.x0, l.colRight, y, l.rowH, name); };
        if (const TriggerDef* d = TriggerDef::find(id_)) {
            y = kitpaint::drawTitle(c, x, y, right, draw::Image{}, false, UiIcon{}, widen(d->title), blockHelpSubtitle(*d), false).next;
            text(blockHelpSentence(*d), kAccent);
            section(L"What it does");
            text(widen(d->docs), kText);
            if (!d->params.empty()) {
                section(L"Values");
                for (const auto& p : d->params) {
                    bool clear = true;
                    const std::wstring line = blockHelpValueLine(p, clear);
                    text(line, clear ? kText : trigPurple());
                }
            }
            if (!d->lists.empty()) {
                section(L"Holds");
                text(blockHelpLists(*d), kText);
            }
            section(L"Example");
            text(d->example.empty() ? L"-" : widen(d->example), kText);
            if (!d->pitfalls.empty()) {
                section(L"Watch out");
                text(widen(d->pitfalls), kWarn);
            }
            section(L"Effects and cost");
            for (const auto& e : blockEffectTexts(*d)) text(e, kText);
            text(blockHelpCost(*d), kDim);
        }
        contentH_ = y + scroll_;
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind != draw::MouseKind::Wheel || e.horizontal) return false;
        const int most = std::max(0, contentH_ - height() + scale(14));
        scroll_ = std::clamp(scroll_ - int(e.wheel * float(scale(40))), 0, most);
        invalidate();
        return true;
    }
    bool focusable() const override { return true; }
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || (e.key != draw::Key::Escape && e.key != draw::Key::F1)) return false;
        if (onClose) onClose();
        return true;
    }
    std::wstring feedbackName(draw::Point) override {
        const TriggerDef* d = TriggerDef::find(id_);
        return L"the help for " + (d ? widen(d->title) : std::wstring(L"a block"));
    }

private:
    std::string id_;
    int scroll_ = 0, contentH_ = 0;
};

// --- The code pane -----------------------------------------------------------------------------------------------

// The epScript the triggers build to (as Build writes it, with the project's debug setting).
std::string trigGeneratedCode(std::vector<TriggerIssue>& issues) {
    return gTE.set.generate(trigMapInfo(), issues, TriggerBuildOptions{gTE.project && gTE.project->debugBuild()});
}

// The code the pane shows: the generated code, with a first line that counts the problems when there are errors.
std::string trigCodeShown() {
    std::vector<TriggerIssue> issues;
    std::string code = trigGeneratedCode(issues);
    int errors = 0;
    for (const auto& i : issues) errors += i.level == TriggerIssue::Level::Error;
    if (errors) code = "// " + std::to_string(errors) + " problem(s) to fix before this builds (marked red on the blocks)\n\n" + code;
    return code;
}

// The lines of trigger `t` in `code` (first to last, from 0): its function (from the line above it, its path
// "// Category / Folder / Name", to its closing brace) and the lines that call it. `top`: the line to scroll to.
struct TrigCodeMarks {
    int top = 0;
    std::vector<std::pair<int, int>> marks;
};
TrigCodeMarks trigCodeMarksFor(const std::string& code, const TriggerNode* t) {
    TrigCodeMarks out;
    if (!t) return out;
    const size_t found = code.find(" / " + t->name + "\nfunction ");
    const size_t at = found == std::string::npos ? 0 : code.rfind('\n', found) + 1;
    const int line = found == std::string::npos ? 0 : int(std::count(code.begin(), code.begin() + std::ptrdiff_t(at), '\n'));
    out.top = line;
    if (found == std::string::npos) return out;
    // To the function's closing brace (at the line's start), and the lines that call it.
    const size_t fn = code.find("function ", found), open = code.find('(', fn);
    const std::string name = fn == std::string::npos || open == std::string::npos ? "" : code.substr(fn + 9, open - fn - 9);
    const size_t end = code.find("\n}\n", found);
    const int last = end == std::string::npos ? line : int(std::count(code.begin(), code.begin() + std::ptrdiff_t(end), '\n')) + 1;
    out.marks.push_back({line, last});
    if (!name.empty())
        for (size_t c = code.find("    " + name + "();"); c != std::string::npos; c = code.find("    " + name + "();", c + 1)) {
            const int cl = int(std::count(code.begin(), code.begin() + std::ptrdiff_t(c), '\n'));
            if (cl < line || cl > last) out.marks.push_back({cl, cl});
        }
    return out;
}

// The code pane: the code in a read-only text field in epScript's colors (the script preview's), inset from the
// pane's edges as the Windows pane's text is (12 px and its 10 px margin at the left, 10 px at the top, 6 px at the
// right), no wrapping; the selected trigger's lines tinted in the accent with a bar at the left.
class TrigCodeView final : public draw::View {
public:
    TrigCodeView() {
        field_.setPlain(true);
        field_.setMultiline(true);
        field_.setWrap(false);
        field_.setReadOnly(true);
        field_.setCopyWhole(true);
        field_.setFont({draw::Face::Mono, 0});
        add(field_, {0, 0, 0, 0});
    }
    stardraft::kit::TextField& field() { return field_; }

    // Shows the set's code now (left alone, with its scroll, when it did not change and !force), scrolled to the
    // selected trigger's function.
    void refresh(bool force) {
        const std::string code = trigCodeShown();
        if (code != shown_ || force) {
            shown_ = code;
            field_.setText(widen(code));
            field_.model().setCaret(0);
            field_.setSpans(epScriptSpans(field_.text(), gLightTheme));
        }
        const TrigCodeMarks m = trigCodeMarksFor(shown_, trigSelected());
        field_.scrollToLine(size_t(m.top));
        if (m.marks != marks_) marks_ = m.marks;
        invalidate();
    }

    void resized() override {
        const int pad = stardraft::kit::px(2);  // the field's own padding
        field_.setFrame({scale(22) - pad, scale(10), std::max(scale(22) - pad + 1, width() - scale(6)), height()});
    }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kPanel); }
    void paintOver(draw::Canvas& c) override {
        const int lineH = std::max(1, field_.lineHeight()), top = field_.frame().top - field_.scrollY();
        for (const auto& [l0, l1] : marks_) {
            const draw::Rect r{0, top + l0 * lineH, width(), top + (l1 + 1) * lineH};
            if (r.bottom < 0 || r.top > height()) continue;
            c.fill(r, kAccent, 30);
            c.fill({0, r.top, scale(3), r.bottom}, kAccent);  // and a bar at the left
        }
    }

private:
    stardraft::kit::TextField field_;
    std::string shown_;
    std::vector<std::pair<int, int>> marks_;
};

// --- Build errors ------------------------------------------------------------------------------------------------

// The block of each line of the last generated/triggers.eps: the node's path and the block (index -1: the trigger).
struct TrigCodeMap {
    fs::path project;
    std::vector<std::pair<std::vector<int>, TrigBlockRef>> lines;
};
TrigCodeMap gTrigCodeMap;

bool trigFindCall(const std::vector<TriggerCall>& list, const TrigListRef& ref, const TriggerCall* target, TrigBlockRef& out) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (&list[i] == target) return out = {ref, int(i)}, true;
        for (size_t n = 0; n < list[i].lists.size(); ++n) {
            TrigListRef inner = ref;
            inner.via.push_back({ref.list, int(i)});
            inner.list = 2 + int(n);
            if (trigFindCall(list[i].lists[n], inner, target, out)) return true;
        }
    }
    return false;
}

// Keeps the line map of code made from `set` (Build calls it with the code it writes).
void trigKeepCodeMap(const EudProject& pr, const TriggerSet& set, const std::vector<TriggerCodeLine>& lines) {
    std::map<const TriggerNode*, std::vector<int>> paths;
    std::function<void(const TriggerNode&, std::vector<int>)> walk = [&](const TriggerNode& n, std::vector<int> path) {
        paths[&n] = path;
        for (size_t i = 0; i < n.children.size(); ++i) {
            auto p = path;
            p.push_back(int(i));
            walk(n.children[i], p);
        }
    };
    for (size_t i = 0; i < set.roots.size(); ++i) walk(set.roots[i], {int(i)});
    gTrigCodeMap = {pr.folder(), {}};
    for (const auto& l : lines) {
        std::pair<std::vector<int>, TrigBlockRef> m;
        if (auto it = paths.find(l.node); it != paths.end()) m.first = it->second;
        if (l.node && l.call) {
            TrigListRef conds;
            conds.list = 0;
            TrigListRef acts;
            acts.list = 1;
            if (!trigFindCall(l.node->conditions, conds, l.call, m.second)) trigFindCall(l.node->actions, acts, l.call, m.second);
        }
        gTrigCodeMap.lines.push_back(std::move(m));
    }
}

// The trigger (and block) that made a line of the last generated/triggers.eps: "Name, Block title" ("" if not known).
std::wstring trigCodeLineName(int line) {
    const EudProject* pr = activeProject();
    if (line < 1 || line > int(gTrigCodeMap.lines.size()) || !pr || pr->folder() != gTrigCodeMap.project) return L"";
    const auto& [path, ref] = gTrigCodeMap.lines[size_t(line - 1)];
    TriggerNode* n = path.empty() ? nullptr : gTE.set.at(path);
    if (!n) return L"";
    const TriggerCall* c = ref.index >= 0 ? trigBlock(*n, ref) : nullptr;
    const TriggerDef* d = c ? TriggerDef::find(c->def) : nullptr;
    return widen(n->name) + (d ? L", " + widen(d->title) : L"");
}

// The pane's scroll for block `ref` as the pane last laid it out (`paneH` high): the block's top a third down when
// the block is below the pane's bottom (the program's trigRevealBlock calls it after the layout).
void trigScrollToBlock(const TrigBlockRef& ref, int paneH) {
    for (const auto& l : gTP.lines)
        if (trigSameBlock(l.ref, ref) && l.r.bottom > paneH) gTE.scroll = std::max(0, int(l.r.top) - paneH / 3);
}

// Shows the block that made a line of the last generated/triggers.eps in the Triggers workspace. True when it did.
bool trigShowCodeLine(int line) {
    const EudProject* pr = activeProject();
    if (line < 1 || line > int(gTrigCodeMap.lines.size()) || !pr || pr->folder() != gTrigCodeMap.project) return false;
    const auto [path, ref] = gTrigCodeMap.lines[size_t(line - 1)];
    if (path.empty() || !gTE.set.at(path)) return false;
    setWorkspace(Workspace::Triggers);
    trigRefreshTree(path);
    gTP.sel = ref, gTP.focus = {}, gTP.focus.ref.index = -1;
    gTE.scroll = 0;  // then the pane moves to the block, once it is laid out
    trigRevealBlock(ref);
    trigPaneRepaint();
    trigInfoRepaint();
    return true;
}

// A line of euddraft's output: when it names a line of the triggers' code, that block is selected. True when it did.
bool trigShowBuildError(const std::string& logLine) {
    const int line = triggerErrorLine(logLine);
    if (!trigShowCodeLine(line)) return false;
    const auto& [path, ref] = gTrigCodeMap.lines[size_t(line - 1)];
    const TriggerNode* n = gTE.set.at(path);
    const TriggerCall* c = trigSelected() ? trigBlock(*trigSelected(), ref) : nullptr;
    const TriggerDef* d = c ? TriggerDef::find(c->def) : nullptr;
    setMessage(L"euddraft found an error in line " + std::to_wstring(line) + L" of the triggers' code: " + widen(n->name) +
               (d ? L", " + widen(d->title) : L"") + L" (see the log)");
    return true;
}
