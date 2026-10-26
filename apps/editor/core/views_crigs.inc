// CRIGs (portable, core.inc; included by views_classic_page.inc before the editor view): a Cosmonarchy map's trigger
// comment commands, under the selected Comment action of the Classic triggers page (core/cmbw.inc: modTab). The mod reads
// each line of a trigger's comment as a command ("kill Anywhere 2 * 5 true", "condition deaths ..."); this view shows
// each line as its command and arguments (stardraft::cmbw::crigCommands), each argument a chip: units, players and
// locations from lists, the other values typed. Add a command, delete one; each change is the comment's new text (one
// undo step). Lines that are not commands stay as they are (plain text).

inline std::vector<std::string> crigSplitLines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
        if (c == '\n') out.push_back(cur), cur.clear();
        else if (c != '\r') cur += c;
    out.push_back(cur);
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

inline std::wstring ctCrigValueText(const std::string& kind, const std::string& v) {
    const Editor* ed = appState().ed.get();
    const bool number = !v.empty() && v.size() <= 6 && std::all_of(v.begin(), v.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
    const int n = number ? std::atoi(v.c_str()) : -1;  // (6 digits at most: no overflow)
    if ((kind == "unit" || kind == "units") && number && ed && n < int(ed->unitTypes().size()) && !ed->unitTypes()[uint16_t(n)].name.empty())
        return unitDisplayName(n);
    if ((kind == "player" || kind == "players") && number) {
        const int p = n;
        if (p >= 1 && p <= 8) return tr(L"Player ") + std::to_wstring(p);
        static const std::map<int, const wchar_t*> named = {{0, L"none"}, {12, L"neutral"}, {13, L"current player"}, {14, L"allies"},
                                                            {15, L"enemies"}, {17, L"all players"}, {18, L"force 1"}, {19, L"force 2"},
                                                            {20, L"force 3"}, {21, L"force 4"}};
        if (auto it = named.find(p); it != named.end()) return tr(it->second);
    }
    if ((kind == "player" || kind == "players") && v == "*") return tr(L"all players");
    if (kind == "units" && v == "*") return tr(L"all units");
    return widen(v);
}

// Argument k of a command's line, from its list (units, players, locations, the mod's words) or typed; a bool toggles.
// The pickers open at `at` under `from`. False when nothing was picked.
inline bool crigEditValue(CtPageHost& h, const draw::View& from, draw::Point at, stardraft::cmbw::CrigLine& l, int k) {
    if (!l.command || k < 0 || k >= int(l.command->args.size())) return false;
    const auto& arg = l.command->args[size_t(k)];
    while (int(l.values.size()) <= k) l.values.push_back(stardraft::cmbw::crigDefaultValue(l.command->args[l.values.size()].kind));
    std::string& v = l.values[size_t(k)];
    const std::wstring title = widen(arg.name);
    const Editor* ed = appState().ed.get();
    // (typed: the value pop-out's plain text, not the game text's dialog: the mod reads the line's bytes as they are)
    auto typed = [&] {
        const draw::Rect field{at.x, at.y - scale(2) - scale(stardraft::kit::kFieldH), at.x + scale(240), at.y - scale(2)};
        return shellEditValueBelow(const_cast<draw::View&>(from), field, title, v);
    };
    // A choice from a list, with Type a value... first (its value: "\x01").
    auto pick = [&](std::vector<ListPickItem> items) {
        items.insert(items.begin(), {tr(L"Type a value\x2026"), L"", "\x01"});
        std::string got;
        if (!h.pickOne(title, std::move(items), v, from, at, got)) return false;
        if (got == "\x01") return typed();
        v = got;
        return true;
    };
    if (arg.kind == "bool") {
        v = v == "true" || v == "1" ? "false" : "true";
        return true;
    }
    if ((arg.kind == "unit" || arg.kind == "units") && ed) {
        std::vector<ListPickItem> items;
        if (arg.kind == "units") {
            items.push_back({tr(L"All units"), tr(L"Groups"), "*"});
            for (const char* g : {"men", "buildings", "factories", "workers", "military", "combatants", "transports", "townhalls", "depots", "nodes",
                                  "terran", "protoss", "zerg", "askosi", "tech", "tech_tier", "factional"})
                items.push_back({widen(g), tr(L"Groups"), g});
        }
        for (uint16_t u = 0; u < ed->unitTypes().size(); ++u)
            if (!ed->unitTypes()[u].name.empty()) items.push_back({unitDisplayName(u) + L"   #" + std::to_wstring(u), tr(L"Units"), std::to_string(u), "units", u});
        return pick(std::move(items));
    }
    if (arg.kind == "player" || arg.kind == "players") {
        std::vector<ListPickItem> items;
        for (int p = 1; p <= 8; ++p) items.push_back({tr(L"Player ") + std::to_wstring(p), L"", std::to_string(p)});
        for (auto [p, n] : std::initializer_list<std::pair<const char*, const wchar_t*>>{{"13", L"Current player"}, {"14", L"Allies"}, {"15", L"Enemies"},
                                                                                          {"12", L"Neutral"}, {"*", L"All players"}, {"18", L"Force 1"},
                                                                                          {"19", L"Force 2"}, {"20", L"Force 3"}, {"21", L"Force 4"}})
            items.push_back({tr(n), L"", p});
        return pick(std::move(items));
    }
    if (arg.kind == "area" && ed) {
        std::vector<ListPickItem> items;
        for (int li = 0; li < ed->locationCount(); ++li)
            if (ed->locationUsed(li)) {
                std::string n = ed->locationName(li);
                if (n.find(' ') != std::string::npos) n = "\"" + n + "\"";  // (a name with spaces: quoted)
                items.push_back({widen(ed->locationName(li)), tr(L"Locations"), n});
            }
        return pick(std::move(items));
    }
    if (const auto& words = stardraft::cmbw::crigWordChoices(l.command->names.front(), arg.name); arg.kind == "word" && !words.empty()) {
        std::vector<ListPickItem> items;
        for (const std::string& w : words) items.push_back({widen(w), L"", w});
        return pick(std::move(items));
    }
    return typed();
}

inline void ctCrigArgClick(CtPageHost& h, int k, const draw::View& from, draw::Rect r) {
    const ClassicCrig* row = ctModelCrig(h.model());
    if (!row) return;
    auto l = stardraft::cmbw::parseCrigLine(row->line);
    if (!l.command) {  // a line of text: typed
        std::string v = row->line;
        if (h.editText(tr(L"Line of the comment"), tr(L"Line of the comment"), v)) ctSetCrigLine(h, v);
        return;
    }
    if (crigEditValue(h, from, {r.left, r.bottom + scale(2)}, l, k)) ctSetCrigLine(h, stardraft::cmbw::crigLineText(l));
}

class CrigView final : public draw::View {
public:
    // The selected Comment action's page (nullptr: not shown); or the selected Cosmonarchy command's row (one line).
    void show(CtPageHost* h) {
        host_ = h;
        lines_.clear();
        single_ = false;
        if (!h) return invalidate();
        CtModel& m = h->model();
        if (const ClassicCrig* row = ctModelCrig(m)) {
            lines_ = {row->line}, single_ = true;
            return invalidate();
        }
        const ClassicAction* a = ctModelAction(m);
        if (a && a->text) lines_ = crigSplitLines(m.strings.get(a->text));
        invalidate();
    }
    // The rows' height (a heading, a row per line, Add a command).
    static int heightFor(int lines) { return scale(stardraft::kit::kFormRowH) * (lines + 2) + scale(8); }
    // A command row's height: the heading, the command's name, a row for each of its values (and one for more).
    int rowHeight() const {
        int rows = 1;
        if (!lines_.empty()) {
            const auto l = stardraft::cmbw::parseCrigLine(lines_[0]);
            if (l.command) {
                for (size_t k = 0; k < l.command->args.size(); ++k) rows += k < l.values.size() || !l.command->args[k].optional;
                rows += !l.rest.empty() || l.command->readsMore;
            }
        }
        return scale(stardraft::kit::kFormRowH) * (rows + 1) + scale(8);
    }
    int lineCount() const { return int(lines_.size()); }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        hits_.clear();
        const int step = scale(stardraft::kit::kFormRowH), rowH = scale(stardraft::kit::kFieldH);
        c.text({0, 0, width(), rowH}, single_ ? tr(L"Trigger comment command") : tr(L"Trigger comment commands in this comment"), kText,
               draw::kMiddle);
        c.fill({0, rowH - 1, width(), rowH}, kBorder);
        int y = step;
        for (size_t i = 0; i < lines_.size(); ++i, y += step) {
            if (lines_[i].empty()) continue;
            const auto l = stardraft::cmbw::parseCrigLine(lines_[i]);
            int x = 0;
            const draw::Rect del{width() - rowH, y, width(), y + rowH};
            kitpaint::drawIconFace(c, del, UiIcon::Delete, kitpaint::Tone::Danger, false, del.contains(mouse_));
            hits_.push_back({del, [this, i] { deleteLine(int(i)); }, tr(L"Delete this command")});
            const int right = del.left - scale(8);
            if (!l.command) {  // plain text
                chip(c, x, y, right, tr(L"Text"), widen(lines_[i]), [this, i] { editPlain(int(i)); });
                continue;
            }
            const std::wstring name = (l.condition ? L"condition " : L"") + widen(l.command->names.front());
            if (single_) {  // (a row: its name, then each value in a row of its own, as the editor's values)
                c.text({0, y, right, y + rowH}, name, kAccent, draw::kMiddle | draw::kEllipsis);
                const int labelW = std::min(scale(150), width() / 3);
                auto row = [&](const std::wstring& label, const std::wstring& value, std::function<void()> run) {
                    y += step;
                    c.text({0, y, labelW - scale(6), y + rowH}, label, kDim, draw::kMiddle | draw::kEllipsis);
                    chip(c, labelW, y, width(), L"", value, std::move(run));
                };
                for (size_t k = 0; k < l.command->args.size(); ++k) {
                    const auto& arg = l.command->args[k];
                    const bool given = k < l.values.size();
                    if (!given && arg.optional) continue;
                    row(widen(arg.name), given ? valueText(arg.kind, l.values[k]) : L"?", [this, i, k] { editArg(int(i), int(k)); });
                }
                if (!l.rest.empty() || l.command->readsMore)
                    row(tr(L"more"), l.rest.empty() ? L"\x2026" : widen(l.rest), [this, i] { editRest(int(i)); });
                continue;
            }
            const int nw = c.measure(name) + scale(4);
            c.text({x, y, x + nw, y + rowH}, name, kAccent, draw::kMiddle);
            x += nw + scale(8);
            for (size_t k = 0; k < l.command->args.size() && x < right; ++k) {
                const auto& arg = l.command->args[k];
                const bool given = k < l.values.size();
                if (!given && arg.optional) continue;
                const std::wstring value = given ? valueText(arg.kind, l.values[k]) : L"?";
                x = chip(c, x, y, right, widen(arg.name), value, [this, i, k] { editArg(int(i), int(k)); });
            }
            if (!l.rest.empty() || l.command->readsMore)
                chip(c, x, y, right, tr(L"more"), l.rest.empty() ? L"\x2026" : widen(l.rest), [this, i] { editRest(int(i)); });
        }
        if (single_) return;  // (a row: the lists' Add adds commands)
        const draw::Rect addRow{0, y, std::min(width(), scale(320)), y + rowH};
        kitpaint::drawAddRowFace(c, addRow, tr(L"Add a command"), addRow.contains(mouse_));
        hits_.push_back({addRow, [this] { addCommand(); }, tr(L"A Cosmonarchy command or condition, as a new line of the comment")});
    }

    bool mouse(const draw::MouseEvent& e) override {
        mouse_ = e.pt;
        if (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Leave) return invalidate(), true;
        if (e.kind == draw::MouseKind::Down && e.button == 0)
            for (const Hit& h : hits_)
                if (h.r.contains(e.pt)) {
                    auto run = h.run;  // (the run may paint the view again)
                    run();
                    return true;
                }
        return false;
    }
    std::wstring tip(draw::Point p) override {
        for (const Hit& h : hits_)
            if (h.r.contains(p)) return h.tip;
        return L"";
    }

private:
    struct Hit {
        draw::Rect r;
        std::function<void()> run;
        std::wstring tip;
    };
    CtPageHost* host_ = nullptr;
    std::vector<std::string> lines_;
    bool single_ = false;  // a Cosmonarchy command's row (its one line), not a Comment's lines
    std::vector<Hit> hits_;
    draw::Point mouse_{-1, -1};
    draw::Rect lastChip_{};

    // An argument's chip at x (its name dim, its value in a box); returns the x after it.
    int chip(draw::Canvas& c, int x, int y, int right, const std::wstring& name, const std::wstring& value, std::function<void()> run) {
        const int rowH = scale(stardraft::kit::kFieldH), pad = scale(6);
        const int lw = name.empty() ? 0 : c.measure(name) + scale(4);
        if (x + lw >= right) return x;
        c.text({x, y, x + lw, y + rowH}, name, kDim, draw::kMiddle);
        x += lw;
        const int vw = std::min(c.measure(value) + 2 * pad, std::max(scale(30), std::min(scale(260), right - x)));
        const draw::Rect r{x, y + scale(2), x + vw, y + rowH - scale(2)};
        kitpaint::drawChipFace(c, r, r.contains(mouse_), false);
        c.text({r.left + pad, r.top, r.right - pad, r.bottom}, value, kAccent, draw::kMiddle | draw::kEllipsis);
        hits_.push_back({r, [this, r, run] { lastChip_ = r, run(); }, name});
        return r.right + scale(10);
    }

    // A value as the chip shows it: a unit's or a player's name, the rest as written.
    std::wstring valueText(const std::string& kind, const std::string& v) const { return ctCrigValueText(kind, v); }

    // The new lines into the comment (one undo step).
    void store() {
        if (!host_) return;
        if (single_) {  // the row's line (a deleted line: the row goes)
            CtModel& m = host_->model();
            if (lines_.empty()) ctDeleteEntry(*host_, ctModelCondition(m) ? 0 : 1, m.entry);
            else ctSetCrigLine(*host_, lines_[0]);
            host_->buildEditor();
            return;
        }
        std::string text;
        for (size_t i = 0; i < lines_.size(); ++i) text += (i ? "\r\n" : "") + lines_[i];
        CtModel& m = host_->model();
        m.hold++;
        ctSetText(*host_, 0, text);
        m.hold--;
        host_->changed();
        host_->buildEditor();
        host_->entriesLaid();  // (the editor's height follows the lines: the page places its parts again)
    }
    void deleteLine(int i) {
        if (i < 0 || i >= int(lines_.size())) return;
        lines_.erase(lines_.begin() + i);
        store();
    }
    bool typed(const std::wstring& title, std::string& value) { return host_ && host_->editText(title, title, value); }
    void editPlain(int i) {
        std::string v = lines_[size_t(i)];
        if (typed(tr(L"Line of the comment"), v)) lines_[size_t(i)] = v, store();
    }
    void editRest(int i) {
        auto l = stardraft::cmbw::parseCrigLine(lines_[size_t(i)]);
        std::string v = l.rest;
        if (!typed(tr(L"The rest of the command"), v)) return;
        l.rest = v;
        lines_[size_t(i)] = stardraft::cmbw::crigLineText(l);
        store();
    }
    // A choice from a list; `typeIt` adds Type a value... (its value: "\x01").
    bool pick(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, bool typeIt, std::string& out) {
        if (!host_) return false;
        if (typeIt) items.insert(items.begin(), {tr(L"Type a value\x2026"), L"", "\x01"});
        return host_->pickOne(title, std::move(items), current, *this, {lastChip_.left, lastChip_.bottom + scale(2)}, out);
    }
    void editArg(int i, int k) {
        auto l = stardraft::cmbw::parseCrigLine(lines_[size_t(i)]);
        if (!host_ || !crigEditValue(*host_, *this, {lastChip_.left, lastChip_.bottom + scale(2)}, l, k)) return;
        lines_[size_t(i)] = stardraft::cmbw::crigLineText(l);
        store();
    }
    void addCommand() {
        std::vector<ListPickItem> items;
        const auto& all = stardraft::cmbw::crigCommands();
        // The actions first, then each player's screen's, then the conditions (a search for "kill" finds the action kill
        // before the condition kills: the picker keeps the order of rows that match as well).
        std::vector<size_t> order;
        for (const char* kind : {"action", "local_action", "condition"})
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].kind == kind) order.push_back(i);
        for (size_t i : order) {
            const auto& c = all[i];
            std::wstring args;
            for (const auto& a : c.args) args += (args.empty() ? L"" : L", ") + widen(a.name) + (a.optional ? L"?" : L"");
            const std::wstring section = c.kind == "condition" ? tr(L"Conditions") : c.kind == "local_action" ? tr(L"Actions for each player's screen") : tr(L"Actions");
            items.push_back({widen(c.names.front()) + (args.empty() ? L"" : L"   (" + args + L")"), section, std::to_string(i)});
        }
        lastChip_ = {0, height() - scale(stardraft::kit::kFormRowH), scale(320), height()};
        std::string got;
        if (!pick(tr(L"Add a Cosmonarchy command"), std::move(items), "", false, got)) return;
        const size_t pickIndex = size_t(std::strtoul(got.c_str(), nullptr, 10));
        if (pickIndex >= all.size()) return;
        const auto& c = all[pickIndex];
        stardraft::cmbw::CrigLine l;
        l.command = &c, l.condition = c.kind == "condition";
        for (const auto& a : c.args)
            if (!a.optional) l.values.push_back(stardraft::cmbw::crigDefaultValue(a.kind));
            else break;
        lines_.push_back(stardraft::cmbw::crigLineText(l));
        store();
    }
};
