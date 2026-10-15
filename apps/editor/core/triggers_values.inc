// The Triggers pane's values (portable, core.inc): which pop-out a value opens and with what. A click on a value of a
// block (trigEditFlow, the pane's TrigPaneHost::editValue) opens the pop-out its type has: the list picker of the game's
// values (unit types with their icons, locations, switches, players, sounds, variables, triggers, the .dat entries, the
// game's texts, a set's buttons, a structure's properties), the value pop-out (core/views_value_pop.inc) for a number,
// a text, a text with values or colors, and a code, or the button pop-out; Tab in a pop-out applies the value and opens
// the next value's (Shift+Tab: the one before). A value block in a slot has its menu (another value from the game, or a
// number); a value of one unit can be picked on the map. The name at the top of a page and the variable form's fields
// open theirs too. Each program gives the pop-outs under a point of the pane (its coordinates): trigPopList,
// trigPopValue, trigPopValueBlock, trigPopButton and trigPopAdvance (main.cpp: the Windows ones; sdl_triggers.inc: the
// SDL program's), and trigPanePaintNow paints the pane at once.

bool trigPopList(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, draw::Point at, std::string& out);
bool trigPopValue(const ValuePopSpec& spec, draw::Point at, std::string& out);
std::string trigPopValueBlock(draw::Point at, const std::string& current);  // a value from the game: its id, or ""
bool trigPopButton(std::string& text, draw::Point at);  // a whole button (buttonToText's text): true when OK
int trigPopAdvance();  // Tab (+1) or Shift+Tab (-1) closed the last pop-out; 0 none (and it is 0 after the call)
void trigPanePaintNow();
void trigEditText(TrigParamHit h, const std::string& value, bool number, bool wide);
void trigEditTextPart(TrigParamHit h);
void trigEditParam(TrigParamHit h);

// The per-player variables that hold `holds` (unit types, locations): a block takes the current player's value (GH#108).
std::vector<ListPickItem> trigPerPlayerItems(const std::string& holds) {
    std::vector<ListPickItem> items;
    for (const auto& [name, node] : gTE.set.variables())
        if (node->variable.kind == TriggerVariable::Kind::PerPlayer && node->variable.holds == holds)
            items.push_back({widen(name) + L" (the current player's)", L"Player arrays", name});
    return items;
}

// The variables a value of this type can use, as list items.
std::vector<ListPickItem> trigVariableItems(const std::string& type) {
    std::vector<ListPickItem> items;
    for (const auto& [name, node] : gTE.set.variables()) {
        const auto k = node->variable.kind;
        bool fits = false;
        if (type == "Flag" || type == "FlagTarget") fits = k == TriggerVariable::Kind::Flag;
        else if (type == "UnitTarget" || type == "UnitRef") fits = k == TriggerVariable::Kind::Unit;
        else if (type == "UnitGroupTarget") fits = k == TriggerVariable::Kind::UnitGroup;
        else if (type == "TextTarget") fits = k == TriggerVariable::Kind::Text;
        else if (k == TriggerVariable::Kind::UnitGroup) fits = false;
        else if (type == "ListTarget") fits = k == TriggerVariable::Kind::Array || k == TriggerVariable::Kind::PerPlayer;
        else fits = k == TriggerVariable::Kind::Number || k == TriggerVariable::Kind::PerPlayer || k == TriggerVariable::Kind::Array ||
                    (k == TriggerVariable::Kind::Flag && type != "NumberTarget") || (k == TriggerVariable::Kind::Constant && type == "Number");
        if (fits) items.push_back({widen(name) + L"   " + widen(TriggerVariable::kindLabel(k)), L"Variables", name});
    }
    return items;
}

// A value block in a slot: replace it, or go back to a plain value.
void trigValueBlockMenu(TrigParamHit h) {
    TriggerCall* call = trigHitCall(h);
    const int slot = trigHitParam(h);
    if (!call || slot >= int(call->argBlocks.size()) || call->argBlocks[size_t(slot)].empty()) return;
    const draw::Point at{h.r.left, h.r.bottom};
    std::string choice;
    if (!trigPopList(L"Value", {{L"Another value from the game\x2026", L"", "1"}, {L"A number or variable instead\x2026", L"", "2"}}, "", at, choice)) return;
    const int pick = std::atoi(choice.c_str());
    if (pick == 1) {
        const std::string id = trigPopValueBlock(at, call->argBlocks[size_t(slot)][0].def);
        call = trigHitCall(h);
        if (call && !id.empty()) call->argBlocks[size_t(slot)] = {TriggerCall::make(*TriggerDef::find(id))}, trigChanged();
    } else if (pick == 2) {
        const TriggerDef* d = TriggerDef::find(call->def);
        call->argBlocks[size_t(slot)].clear();
        call->args[size_t(slot)] = triggerTypeDefault(triggerParamType(*d, call->args, slot));
        trigChanged();
        trigPanePaintNow();
        h.bracket = false;
        trigEditText(h, call->args[size_t(slot)], true, false);
    }
}

// Opens a value's popover; Tab in it applies and opens the next value's, Shift+Tab the previous one's.
void trigEditFlow(TrigParamHit h) {
    for (int guard = 0; guard < 64; ++guard) {
        gTP.focus = h, gTP.sel = h.ref;
        trigPaneRepaint();
        trigPopAdvance();  // (none yet)
        trigEditParam(h);
        const int dir = trigPopAdvance();
        trigPaneRepaint();
        trigPanePaintNow();
        if (!dir) return;
        const auto order = trigTabOrder(h.ref);
        int at = -1;
        for (size_t i = 0; i < order.size(); ++i)
            if (trigSameHit(order[i], h) || (at < 0 && order[i].param == h.param && order[i].inner.empty())) at = int(i);
        const int next = at + dir;
        if (at < 0 || next < 0 || next >= int(order.size())) return;
        h = order[size_t(next)];
    }
}

// A value's pop-out (`h` is a copy, see trigAddWithPicker).
void trigEditParam(TrigParamHit h) {
    if (h.token != -1) return trigEditTextPart(h);
    if (h.bracket) return trigValueBlockMenu(h);
    TriggerNode* t = trigSelected();
    TriggerCall* call = trigHitCall(h);
    const int hp = trigHitParam(h);
    const TriggerDef* d = call ? TriggerDef::find(call->def) : nullptr;
    if (!d || hp >= int(d->params.size()) || hp >= int(call->args.size())) return;
    const TriggerParam& p = d->params[size_t(hp)];
    const std::string typeName = triggerParamType(*d, call->args, hp);
    const TriggerType* type = triggerType(typeName);
    const std::string value = call->args[size_t(hp)];
    const draw::Point at{h.r.left, h.r.bottom};
    std::string picked;
    bool got = false;
    auto finish = [&](const std::string& v) {
        if (TriggerCall* c = trigHitCall(h); c && hp < int(c->args.size())) c->args[size_t(hp)] = v, trigChanged();
    };
    if (!type || type->picker == TriggerPicker::Unclear) {
        setMessage(L"\"" + widen(p.name) + tr(L"\" has no clear type yet (") + widen(typeName) + tr(L"). Type it as a number."));
        return trigEditText(h, value, true, false);
    }
    if (p.type == "GameString") {  // the game's texts (stat_txt.tbl), as on the Strings tab
        dataLoadForProject();
        std::vector<ListPickItem> items = entryItems("strings", true);
        for (auto& it : trigVariableItems("Number")) items.push_back(it);
        if (trigPopList(L"Search: game text (or a variable)", items, value, at, picked) && picked != value) finish(picked);
        return;
    }
    if (p.type == "ButtonIndex") {  // the set's buttons, as the Buttons tab lists them; a new one at the end
        const int sp = d->param("set");
        const std::string set = sp >= 0 ? call->args[size_t(sp)] : "";
        std::vector<ListPickItem> items;
        size_t count = 0;
        if (trigIsNumberText(set) && std::atoi(set.c_str()) < ButtonSets::kSets) {
            dataLoadForProject();
            const auto& bs = dataExtras().buttons.set(std::atoi(set.c_str()));
            count = bs.size();
            const std::wstring group = L"Buttons of " + buttonSetName(std::atoi(set.c_str()));
            for (size_t i = 0; i < bs.size(); ++i) items.push_back({buttonRowTextFrom(bs[i], bs[i].enabledString ? statString(bs[i].enabledString) : L""), group, std::to_string(i), "icons", int(bs[i].icon)});
        }
        items.push_back({L"+ A new button", L"Buttons", std::to_string(count)});
        for (auto& it : trigVariableItems("Number")) items.push_back(it);
        if (!trigPopList(L"Search: button", items, value, at, picked) || picked == value) return;
        finish(picked);
        // A button not set up yet starts as the one it replaces.
        const int bp = d->param("button");
        TriggerCall* c = trigHitCall(h);
        if (c && bp >= 0 && trigIsNumberText(picked) && trigIsNumberText(set) && std::atoi(set.c_str()) < ButtonSets::kSets &&
            c->args[size_t(bp)] == d->params[size_t(bp)].def) {
            const auto& bs = dataExtras().buttons.set(std::atoi(set.c_str()));
            if (size_t(std::atoi(picked.c_str())) < bs.size()) c->args[size_t(bp)] = buttonToText(bs[size_t(std::atoi(picked.c_str()))]), trigChanged();
        }
        return;
    }
    switch (type->picker) {
    case TriggerPicker::Property: {
        // Grouped as the properties are (Health, State, ... or the .dat's sections), Advanced last; each says its type.
        std::vector<ListPickItem> items, advanced;
        std::vector<std::string> groups;
        for (const auto& pr : triggerProperties(type->of))
            if (!pr.advanced && std::find(groups.begin(), groups.end(), pr.group) == groups.end()) groups.push_back(pr.group);
        for (const auto& g : groups)
            for (const auto& pr : triggerProperties(type->of))
                if (!pr.advanced && pr.group == g) {
                    const TriggerType* vt = triggerType(pr.type);
                    items.push_back({widen(pr.label) + L"   \x2192 " + (vt ? widen(vt->label) : widen(pr.type)), widen(g), pr.code});
                }
        for (const auto& pr : triggerProperties(type->of))
            if (pr.advanced) advanced.push_back({widen(pr.label) + L"   \x2192 " + widen(pr.code), L"Advanced (eudplib names)", pr.code});
        items.insert(items.end(), advanced.begin(), advanced.end());
        got = trigPopList(L"Search the " + widen(triggerStructureLabel(type->of)) + L"'s properties", items, value, at, picked);
        if (got && picked != value) {
            // The values whose type follows this property start over when their type changes.
            if (TriggerCall* c = trigHitCall(h)) {
                std::vector<std::string> before = c->args;
                c->args[size_t(hp)] = picked;
                for (size_t i = 0; i < d->params.size() && i < c->args.size(); ++i)
                    if (d->params[i].type == "ValueOf:" + p.name && triggerParamType(*d, before, int(i)) != triggerParamType(*d, c->args, int(i)))
                        c->args[i] = triggerTypeDefault(triggerParamType(*d, c->args, int(i)));
                // Adding to something that is not a number makes no sense: set it.
                const int mod = d->param("mod");
                const TriggerProperty* pr = triggerCallProperty(*d, c->args);
                if (mod >= 0 && pr && pr->type != "Number") c->args[size_t(mod)] = "SetTo";
                const int cmp = d->param("cmp");  // and yes / no is compared with exactly or not
                if (cmp >= 0 && pr && pr->type == "YesNo" && c->args[size_t(cmp)] != "!=") c->args[size_t(cmp)] = "==";
                trigChanged();
            }
            return;
        }
        break;
    }
    case TriggerPicker::DatEntry: {
        std::vector<ListPickItem> items = trigVariableItems("Number");
        dataLoadForProject();  // the icons come from the project's .dat tables
        const bool icons = datHasIcons(type->of);
        for (int i = 0; i < type->count; ++i) {
            const std::wstring name = entryName(type->of, i);
            items.push_back({(name.empty() ? L"(unnamed)" : name) + L"   #" + std::to_wstring(i), L"All, by number", std::to_string(i), type->of, icons ? i : -1});
        }
        got = trigPopList(L"Search: " + widen(type->label) + L" (or a variable)", items, value, at, picked);
        break;
    }
    case TriggerPicker::Choice: {
        std::vector<ListPickItem> items;
        for (const auto& ch : type->choices) {
            if (typeName == "TextPlace" && ch.code.rfind("line", 0) == 0) {  // the message lines: a section of their own
                const int n = std::atoi(ch.code.c_str() + 4) + 1;
                items.push_back({L"line " + std::to_wstring(n), L"On a specific line", ch.code});
                continue;
            }
            if (typeName == "TextPlace" && ch.code == "middle") {  // the error line, in the middle of the screen: line 12
                items.push_back({L"line 12 (center of the screen)", L"On a specific line", ch.code});
                continue;
            }
            items.push_back({widen(ch.label), L"", ch.code});
        }
        const bool player = triggerPlayerType(typeName);
        if (player) {  // a player can also be a number: a variable, a typed value, or a value from the game (Player 1 is 0)
            for (auto& it : trigVariableItems("Number")) items.push_back(it);
            items.push_back({L"Type a number or an expression\x2026", L"Other", "\x01type"});
            items.push_back({L"A value from the game\x2026", L"Other", "\x01game"});
        }
        got = trigPopList(widen(type->label), items, value, at, picked);
        if (got && player && picked == "\x01type") return trigEditText(h, value, false, false);
        if (got && player && picked == "\x01game") {
            const std::string id = trigPopValueBlock(at, "");
            if (TriggerCall* c = trigHitCall(h); c && !id.empty() && hp < int(c->argBlocks.size()))
                c->argBlocks[size_t(hp)] = {TriggerCall::make(*TriggerDef::find(id))}, trigChanged();
            return;
        }
        break;
    }
    case TriggerPicker::UnitType:
    case TriggerPicker::UnitFilter: {
        std::vector<ListPickItem> items;
        if (type->picker == TriggerPicker::UnitFilter)
            for (int g = 229; g <= 232; ++g) items.push_back({trigUnitGroupName(g), L"Groups", std::to_string(g)});
        for (auto& it : trigPerPlayerItems("UnitType")) items.push_back(it);
        for (const auto& u : paletteUnits(appState())) items.push_back({u.label, u.section, std::to_string(u.value), "units", u.value});
        got = trigPopList(L"Search unit types", items, value, at, picked);
        break;
    }
    case TriggerPicker::Location: {
        std::vector<ListPickItem> items;
        if (const Editor* ed = appState().ed.get())
            for (int i = 0; i < ed->locationCount(); ++i)
                if (ed->locationUsed(i) || i == kAnywhereLocation) items.push_back({widen(ed->locationName(i)), L"", std::to_string(i + 1)});
        for (auto& it : trigPerPlayerItems("Location")) items.insert(items.begin(), it);
        got = trigPopList(L"Search the map's locations", items, value, at, picked);
        break;
    }
    case TriggerPicker::Switch: {
        std::vector<ListPickItem> items;
        for (int i = 1; i <= 256; ++i) items.push_back({L"Switch " + std::to_wstring(i), L"", std::to_string(i)});
        got = trigPopList(L"Switch number", items, value, at, picked);
        break;
    }
    case TriggerPicker::Sound: {
        std::vector<ListPickItem> items;
        for (const auto& s : trigMapInfo().sounds) items.push_back({widen(s), L"Sounds in the map", s});
        if (items.empty()) {
            setMessage(L"The map has no sounds. Type the path of a sound.");
            return trigEditText(h, value, false, true);
        }
        got = trigPopList(L"Search the map's sounds", items, value, at, picked);
        break;
    }
    case TriggerPicker::Variable: {
        auto items = trigVariableItems(typeName);
        if (items.empty()) {
            setMessage(L"There is no variable of this kind. Right-click Definitions, then New variable.");
            return;
        }
        got = trigPopList(L"Search variables", items, value, at, picked);
        // Arrays and per-player values: say which one (name[index]).
        if (got && p.type == "NumberTarget")
            if (auto it = gTE.set.variables().find(picked); it != gTE.set.variables().end() && it->second->variable.kind == TriggerVariable::Kind::Array) {
                finish(picked + "[0]");
                return trigEditText(h, picked + "[0]", false, false);
            }
        break;
    }
    case TriggerPicker::Trigger: {
        std::vector<ListPickItem> items;
        std::function<void(const TriggerNode&, const std::wstring&)> add = [&](const TriggerNode& n, const std::wstring& where) {
            if (n.kind == TriggerNode::Kind::Trigger) items.push_back({widen(n.name), where, n.name});
            const std::wstring inner = n.kind == TriggerNode::Kind::Folder ? widen(n.name) : where;
            for (const auto& c : n.children) add(c, inner);
        };
        if (!gTE.set.roots.empty()) add(gTE.set.roots[0], L"Definitions");
        if (items.empty()) {
            setMessage(L"There are no triggers in Definitions. Right-click Definitions, then New trigger.");
            return;
        }
        got = trigPopList(L"Search triggers", items, value, at, picked);
        break;
    }
    case TriggerPicker::UnitRef: {
        std::vector<ListPickItem> items;
        if (trigInUnitLoop(*t, h.ref.list)) items.push_back({L"the unit", L"For each unit", "the unit"});
        items.push_back({L"the next created unit", L"Created", "the next created unit"});
        for (const auto& item : trigVariableItems("UnitRef")) items.push_back({widen(item.value), L"Unit variables", item.value});
        items.push_back({L"Select on the map\x2026", L"Map", "\x01pick"});
        got = trigPopList(L"One unit", items, value, at, picked);
        if (got && picked == "\x01pick") return trigStartUnitPick(h.ref, h.param, h.inner);
        break;
    }
    case TriggerPicker::Number: return trigEditText(h, value, true, false);
    case TriggerPicker::Text:
    case TriggerPicker::Code: return trigEditText(h, value, false, true);
    case TriggerPicker::Button: {
        std::string v = value;
        if (trigPopButton(v, at)) finish(v);
        return;
    }
    case TriggerPicker::Unclear: break;
    }
    if (got) finish(picked);
}

// The .dat field of a property that a widget can show ("" when none).
std::pair<std::string, std::string> trigPropertyDatField(const TriggerProperty& p) {
    static const std::map<std::pair<std::string, std::string>, std::pair<std::string, std::string>> fields = {
        {{"Weapon", "minRange"}, {"weapons", "Minimum Range"}},
        {{"Weapon", "maxRange"}, {"weapons", "Maximum Range"}},
        {{"Weapon", "splashInnerRadius"}, {"weapons", "Inner Splash Range"}},
        {{"Weapon", "splashMiddleRadius"}, {"weapons", "Medium Splash Range"}},
        {{"Weapon", "splashOuterRadius"}, {"weapons", "Outer Splash Range"}},
        {{"Weapon", "damage"}, {"weapons", "Damage Amount"}},
        {{"Weapon", "damageBonus"}, {"weapons", "Damage Bonus"}},
        {{"Weapon", "damageFactor"}, {"weapons", "Damage Factor"}},
        {{"Weapon", "damageType"}, {"weapons", "Weapon Type"}},
        {{"Weapon", "cooldown"}, {"weapons", "Weapon Cooldown"}},
        {{"Weapon", "attackAngle"}, {"weapons", "Attack Angle"}},
        {{"Weapon", "launchSpin"}, {"weapons", "Launch Spin"}},
        {{"Weapon", "forwardOffset"}, {"weapons", "Forward Offset"}},
        {{"Weapon", "verticalOffset"}, {"weapons", "Upward Offset"}},
        {{"TrgUnit", "unitBoundsL"}, {"units", "Unit Size Left"}},
        {{"TrgUnit", "unitBoundsT"}, {"units", "Unit Size Up"}},
        {{"TrgUnit", "unitBoundsR"}, {"units", "Unit Size Right"}},
        {{"TrgUnit", "unitBoundsB"}, {"units", "Unit Size Down"}},
        {{"TrgUnit", "startDirection"}, {"units", "Unit Direction"}},
        {{"TrgUnit", "seekRange"}, {"units", "Target Acquisition Range"}},
        {{"TrgUnit", "sightRange"}, {"units", "Sight Range"}},
        {{"Flingy", "topSpeed"}, {"flingy", "Speed"}},
        {{"Flingy", "acceleration"}, {"flingy", "Acceleration"}},
        {{"Flingy", "haltDistance"}, {"flingy", "Halt Distance"}},
        {{"Flingy", "turnSpeed"}, {"flingy", "Turn Radius"}},
    };
    const auto it = fields.find({p.structure, p.code});
    return it != fields.end() ? it->second : std::pair<std::string, std::string>{};
}

// A block's value typed in the pop-out.
void trigEditText(TrigParamHit h, const std::string& value, bool, bool) {
    TriggerNode* t = trigSelected();
    TriggerCall* call = trigHitCall(h);
    const int hp = trigHitParam(h);
    const TriggerDef* d = call ? TriggerDef::find(call->def) : nullptr;
    if (!d || hp < 0 || hp >= int(d->params.size())) return;
    const TriggerParam& p = d->params[size_t(hp)];
    const draw::Point at{h.r.left, h.r.bottom + scale(4)};  // (the pop-outs: under the value)
    ValuePopSpec spec;
    spec.type = triggerParamType(*d, call->args, hp);
    bool wantsGame = false;
    spec.wantsGame = &wantsGame;  // (also a value block's own values: values inside values)
    spec.initial = value;
    spec.inRepeat = trigInsideEffect(*t, h.ref.list, "counter");
    spec.inPlayerLoop = trigInsideEffect(*t, h.ref.list, "curplayer");
    spec.min = p.min, spec.max = p.max;
    const TriggerType* type = triggerType(spec.type);
    if (p.type.rfind("ValueOf:", 0) == 0)
        if ((spec.prop = triggerCallProperty(*d, call->args))) spec.min = spec.prop->min, spec.max = spec.prop->max;
    spec.title = spec.prop ? widen(spec.prop->label) : widen(type ? type->label : p.name);
    if (spec.title.rfind(L"ep", 0) != 0) spec.title[0] = wchar_t(towupper(spec.title[0]));  // (epScript keeps its small e)
    if (spec.prop) {  // a .dat value with a picture: which entry the block changes, and how
        auto [dat, field] = trigPropertyDatField(*spec.prop);
        if (!dat.empty())
            for (size_t k = 0; k < d->params.size() && k < call->args.size(); ++k) {
                const std::string& pt = d->params[k].type;
                if ((pt == "Dat:" + dat || (dat == "units" && pt == "UnitType")) && trigIsNumberText(call->args[k])) {
                    spec.previewDat = dat, spec.previewField = field, spec.previewEntry = std::atoi(call->args[k].c_str());
                    const int mi = d->param("mod");
                    const std::string mod = mi >= 0 && mi < int(call->args.size()) ? call->args[size_t(mi)] : "SetTo";
                    spec.previewMod = mod == "Add" ? 1 : mod == "Subtract" ? 2 : 0;
                    break;
                }
            }
    }
    spec.help = spec.prop ? widen(spec.prop->notes) : L"";
    spec.code = type && type->picker == TriggerPicker::Code;
    // A text the game shows (mission objectives, a transmission, a leaderboard's label, a player's name): its colors
    // and lines in the rich box (Preview and Raw), as Display text has them.
    if (p.type == "Text" && (d->id == "act.objectives" || d->id == "act.transmission" || d->id == "act.playername" || d->id.rfind("act.lb.", 0) == 0))
        spec.colorsOnly = true;
    if (!triggerTypeClear(spec.type)) spec.help = L"No clear type yet (" + widen(spec.type) + L"). Type it as a number.";
    if (p.type == "Index") {
        const TriggerNode* list = trigIndexList(*d, *call);
        if (!list) {
            setMessage(L"First, select the list. Then the index can be 0 to the number of items minus 1.");
            return;
        }
        const int size = triggerListSize(list->variable, gTE.set.variables());
        spec.min = 0, spec.max = size - 1;
        spec.title = L"Which item of " + widen(list->name) + L"  (0 to " + std::to_wstring(size - 1) + L")";
        spec.help.clear();
        if (list->variable.kind == TriggerVariable::Kind::PerPlayer) {
            // One per player: say which player.
            std::vector<ListPickItem> items;
            for (int pl = 0; pl < 8; ++pl) items.push_back({L"Player " + std::to_wstring(pl + 1), L"", std::to_string(pl)});
            items.push_back({L"the current player", L"", "current player"});
            items.push_back({L"Other\x2026", L"A variable or number", "\x01other"});
            std::string pick;
            if (!trigPopList(L"Which player", items, value, at, pick)) return;
            if (pick != "\x01other") {
                if (TriggerCall* c = trigHitCall(h)) c->args[size_t(hp)] = pick, trigChanged();
                return;
            }
            spec.title = L"Which player of " + widen(list->name) + L"  (0 to 7: Player 1 is 0)";
        }
    }
    if (spec.max >= spec.min && spec.title.find(L"(0 to") == std::wstring::npos)
        spec.title += L"   " + std::to_wstring(spec.min) + L"\x2013" + std::to_wstring(spec.max);
    std::string out;
    if (!trigPopValue(spec, at, out)) {
        if (!wantsGame) return;
        const std::string id = trigPopValueBlock(at, "");
        if (TriggerCall* c = trigHitCall(h); c && !id.empty() && hp < int(c->argBlocks.size()))
            c->argBlocks[size_t(hp)] = {TriggerCall::make(*TriggerDef::find(id))}, trigChanged();
        return;
    }
    if (TriggerCall* c = trigHitCall(h); c && hp < int(c->args.size()) && c->args[size_t(hp)] != out) c->args[size_t(hp)] = out, trigChanged();
}

// Show text's text: the words (the text pop-out, with Insert value), a value typed in it, or a value from the game.
void trigEditTextPart(TrigParamHit h) {
    TriggerNode* t = trigSelected();
    TriggerCall* call = trigHitCall(h);
    if (!t || !call || h.param < 0 || h.param >= int(call->args.size())) return;
    const draw::Point at{h.r.left, h.r.bottom + scale(4)};
    ValuePopSpec spec;
    spec.inRepeat = trigInsideEffect(*t, h.ref.list, "counter");
    spec.inPlayerLoop = trigInsideEffect(*t, h.ref.list, "curplayer");
    std::string out;
    if (h.token == -2) {
        spec.type = "Text", spec.insertValues = true;
        spec.title = L"Text   @ puts in a value. Click a value to change it.";
        spec.initial = call->args[size_t(h.param)];
        gTE.textValuesFor = call->textValues();
        gTE.textValuesOut.clear();
        const bool ok = trigPopValue(spec, at, out);
        gTE.textValuesFor = nullptr;
        if (!ok) return;
        if ((call = trigHitCall(h))) {
            if (gTE.textValuesOut.empty()) std::erase_if(call->extraLists, [](const auto& l) { return l.first == "values"; });
            else *call->textValues(true) = std::move(gTE.textValuesOut);
            call->args[size_t(h.param)] = out, call->normalizeText(), trigChanged();
        }
        return;
    }
    auto parts = triggerTextParts(call->args[size_t(h.param)]);
    if (h.token < 0 || h.token >= int(parts.size())) return;
    TriggerTextPart& part = parts[size_t(h.token)];
    if (part.kind == TriggerTextPart::Kind::Expression) {
        spec.type = "Number", spec.title = L"A value in the text", spec.initial = part.text;
        if (!trigPopValue(spec, at, out)) return;
        part.text = out.empty() ? "0" : out;
    } else if (part.kind == TriggerTextPart::Kind::GameValue) {
        std::string choice;
        if (!trigPopList(L"Value", {{L"Another value from the game\x2026", L"", "1"}, {L"A number or variable instead\x2026", L"", "2"}, {L"Remove", L"", "3"}},
                            "", at, choice))
            return;
        if (choice == "1") {
            const auto* values = call->textValues();
            const std::string id = trigPopValueBlock(at, values && part.value < int(values->size()) ? (*values)[size_t(part.value)].def : "");
            if (id.empty()) return;
            part = {TriggerTextPart::Kind::NewGameValue, id, -1};
        } else if (choice == "2") {
            spec.type = "Number", spec.title = L"A value in the text", spec.initial = "0";
            if (!trigPopValue(spec, at, out)) return;
            part = {TriggerTextPart::Kind::Expression, out.empty() ? "0" : out, -1};
        } else {
            part = {TriggerTextPart::Kind::Words, "", -1};
        }
    } else {
        return;
    }
    if (!(call = trigHitCall(h))) return;
    call->args[size_t(h.param)] = triggerTextJoin(parts);
    call->normalizeText();
    trigChanged();
}

// The name at the top of a trigger's or a variable's page.
void trigEditTitle() {
    TriggerNode* n = trigSelectedNode();
    if (!n || n->kind == TriggerNode::Kind::Category) return;
    ValuePopSpec spec;
    spec.type = "Text";
    spec.title = n->kind == TriggerNode::Kind::Variable ? L"Name   letters, digits and _" : L"Name";
    spec.initial = n->name;
    const draw::Point at{gTP.title.left, gTP.title.bottom};
    std::string out;
    if (trigPopValue(spec, at, out)) trigRenameNode(gTE.sel, out);
}

// The variable form's fields.
void trigEditVariableField(TrigVarHit h) {
    TriggerNode* v = trigSelectedNode();
    if (!v || v->kind != TriggerNode::Kind::Variable) return;
    draw::Point at{h.r.left, h.r.bottom};
    if (h.field == 0) {
        std::vector<ListPickItem> items;
        for (int k = 0; k < TriggerVariable::kKinds; ++k)
            if (TriggerVariable::Kind(k) != TriggerVariable::Kind::UnitGroup || v->variable.kind == TriggerVariable::Kind::UnitGroup)
                items.push_back({widen(TriggerVariable::kindLabel(TriggerVariable::Kind(k))), L"", std::to_string(k + 1)});
        std::string kind;
        at.y += scale(4);
        if (!trigPopList(L"Kind", items, std::to_string(int(v->variable.kind) + 1), at, kind)) return;
        const int pick = std::atoi(kind.c_str());
        if (pick > 0) {
            const bool wasText = v->variable.kind == TriggerVariable::Kind::Text;
            v->variable.kind = TriggerVariable::Kind(pick - 1);
            if (v->variable.kind == TriggerVariable::Kind::Text && !wasText)  // a text: its default length, and no number as its text
                v->variable.size = TriggerVariable::kTextRoom, v->variable.initial.clear();
            else if (wasText && v->variable.kind != TriggerVariable::Kind::Text) v->variable.size = 8, v->variable.initial = "0";
            if (v->variable.kind != TriggerVariable::Kind::PerPlayer) v->variable.holds.clear(), v->variable.playerValues.clear();
            if (v->variable.kind == TriggerVariable::Kind::Unit) v->variable.initial.clear();
            else if (!v->variable.initial.empty() && v->variable.initial[0] == '@') v->variable.initial = "0";
            trigChanged();
            trigRefreshTree(gTE.sel);
        }
        return;
    }
    if (v->variable.kind == TriggerVariable::Kind::PerPlayer && (h.field == 4 || (h.field >= 10 && h.field < 18))) {
        at.y += scale(4);
        std::string pick;
        if (h.field == 4) {  // what each player's value is
            std::vector<ListPickItem> items = {{L"Numbers", L"", "Number"}, {L"Unit types", L"", "UnitType"}, {L"Locations", L"", "Location"}};
            if (!trigPopList(L"Holds", items, v->variable.holds.empty() ? "Number" : v->variable.holds, at, pick) || pick.empty()) return;
            const std::string holds = pick == "Number" ? "" : pick;
            if (holds != v->variable.holds) v->variable.holds = holds, v->variable.playerValues.assign(8, "");  // the old values are of another kind
            trigChanged();
            return;
        }
        const int p = h.field - 10;
        v->variable.playerValues.resize(8);
        std::string& val = v->variable.playerValues[size_t(p)];
        std::vector<ListPickItem> items;
        if (v->variable.holds == "UnitType") {
            for (const auto& u : paletteUnits(appState())) items.push_back({u.label, u.section, std::to_string(u.value), "units", u.value});
            if (!trigPopList(tr(L"Player ") + std::to_wstring(p + 1) + tr(L": search unit types"), items, val, at, pick) || pick.empty()) return;
        } else if (v->variable.holds == "Location") {
            if (const Editor* ed = appState().ed.get())
                for (int i = 0; i < ed->locationCount(); ++i)
                    if (ed->locationUsed(i) || i == kAnywhereLocation) items.push_back({widen(ed->locationName(i)), L"", std::to_string(i + 1)});
            if (!trigPopList(tr(L"Player ") + std::to_wstring(p + 1) + tr(L": search the map's locations"), items, val, at, pick) || pick.empty()) return;
        } else {
            ValuePopSpec spec;
            spec.title = tr(L"Player ") + std::to_wstring(p + 1) + tr(L": default value");
            spec.type = "Number", spec.constantOnly = true, spec.initial = val.empty() ? "0" : val;
            if (!trigPopValue(spec, at, pick)) return;
        }
        val = pick;
        trigChanged();
        return;
    }
    if (h.field == 2 && v->variable.kind == TriggerVariable::Kind::Unit) {  // a unit picked on the map, or none
        if (v->variable.initial.empty()) return trigStartUnitPick({}, -1);
        std::vector<ListPickItem> items = {{L"Select another unit on the map\x2026", L"", "pick"}, {L"No unit (empty)", L"", "none"}};
        at.y += scale(4);
        std::string pick;
        if (!trigPopList(L"Default value", items, "", at, pick)) return;
        if (pick == "pick") return trigStartUnitPick({}, -1);
        if (pick == "none") v->variable.initial.clear(), trigChanged();
        return;
    }
    ValuePopSpec spec;
    if (h.field == 1 && v->variable.kind == TriggerVariable::Kind::Text) {
        spec.title = L"Maximum length (letters)   1 to " + std::to_wstring(TriggerVariable::kTextRoomMax);
        spec.help = L"Each text variable reserves space in the map for this many letters. A longer text overwrites other data in the map.";
        spec.type = "Number", spec.min = 1, spec.max = TriggerVariable::kTextRoomMax, spec.constantOnly = true;
        spec.initial = std::to_string(v->variable.size);
    } else if (h.field == 1 && v->variable.kind == TriggerVariable::Kind::UnitGroup) {
        spec.title = L"Room for (units)   1 to 1700";
        spec.type = "Number", spec.min = 1, spec.max = 1700, spec.constantOnly = true;
        spec.initial = std::to_string(v->variable.size);
    } else if (h.field == 1) {
        spec.title = L"Size   1 to 4096";
        spec.type = "Number", spec.min = 1, spec.max = 4096, spec.constantOnly = true, spec.constantsNamed = true;
        spec.initial = v->variable.sizeConstant.empty() ? std::to_string(v->variable.size) : v->variable.sizeConstant;
    } else if (h.field == 2) {
        const auto kind = v->variable.kind;
        const bool flag = kind == TriggerVariable::Kind::Flag;
        spec.title = kind == TriggerVariable::Kind::Constant ? L"Value" : kind == TriggerVariable::Kind::Array ? L"Default value of each item" : L"Default value";
        if (flag) spec.title += L"   1 on, 0 off";
        spec.type = "Number", spec.constantOnly = true, spec.initial = v->variable.initial.empty() ? "0" : v->variable.initial;
        if (flag) spec.min = 0, spec.max = 1;
        if (kind == TriggerVariable::Kind::Text)  // a text the game can show: its colors, Preview and Raw (#19)
            spec.title = L"Text", spec.type = "Text", spec.constantOnly = false, spec.colorsOnly = true, spec.initial = v->variable.initial;
    } else {
        spec.title = L"Description";
        spec.type = "Text", spec.initial = v->comment;
    }
    std::string out;
    if (!trigPopValue(spec, {h.r.left, h.r.bottom + scale(4)}, out)) return;
    v = trigSelectedNode();
    if (!v || v->kind != TriggerNode::Kind::Variable) return;
    if (h.field == 1) {
        // A constant by name keeps the link (the list follows the constant); a number is the size itself.
        const auto vars = gTE.set.variables();
        const auto it = vars.find(out);
        if (v->variable.kind == TriggerVariable::Kind::UnitGroup) v->variable.size = std::clamp(std::atoi(out.c_str()), 1, 1700);
        else if (v->variable.kind == TriggerVariable::Kind::Text) v->variable.size = std::clamp(std::atoi(out.c_str()), 1, TriggerVariable::kTextRoomMax);
        else if (it != vars.end() && it->second->variable.kind == TriggerVariable::Kind::Constant)
            v->variable.sizeConstant = out, v->variable.size = triggerListSize(v->variable, vars);
        else v->variable.sizeConstant.clear(), v->variable.size = std::clamp(std::atoi(out.c_str()), 1, 4096);
    }
    else if (h.field == 2) v->variable.initial = out;
    else v->comment = out;
    trigChanged();
}
