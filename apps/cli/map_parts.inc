// stardraft-cli: the classic triggers, the mission briefings and the unit, upgrade and tech settings, as JSON
// (included by main.cpp, inside its anonymous namespace; it uses main.cpp's helpers).
//
// A trigger's values are given by name or number, as `stardraft-cli trigger-types` lists them: a player as 1-12 or a
// group name ("Current player", "Force 1"), a unit type by id or name, a location by number or name, a switch as
// 1-256 or by name, a text as the text itself (it becomes a map string), a choice by its name ("at least", "add").

// --- Names -------------------------------------------------------------------------------------------------------

ClassicNames classicNames(const Editor& ed) {
    ClassicNames n;
    n.unit = [&ed](uint32_t v) { return v < ed.unitTypes().size() ? ed.unitTypes()[uint16_t(v)].name : std::string(); };
    n.location = [&ed](uint32_t v) { return v >= 1 && int(v) <= ed.locationCount() ? ed.locationName(int(v) - 1) : std::string(); };
    n.string = [&ed](uint32_t v) { return ed.strings().get(v); };
    n.switchName = [&ed](uint32_t v) { return ed.switchName(int(v)); };
    n.force = [&ed](uint32_t v) { return v < 4 ? ed.force(int(v)).name : std::string(); };
    return n;
}

std::string kindName(ClassicKind k) {
    switch (k) {
    case ClassicKind::Player: return "player";
    case ClassicKind::Comparison: return "comparison";
    case ClassicKind::Number: return "number";
    case ClassicKind::Unit: return "unit";
    case ClassicKind::Location: return "location";
    case ClassicKind::Resource: return "resource";
    case ClassicKind::Score: return "score";
    case ClassicKind::Switch: return "switch";
    case ClassicKind::SwitchState: return "switch state";
    case ClassicKind::SwitchAction: return "switch action";
    case ClassicKind::Modifier: return "modifier";
    case ClassicKind::Text: return "text";
    case ClassicKind::Sound: return "sound";
    case ClassicKind::Milliseconds: return "milliseconds";
    case ClassicKind::Seconds: return "seconds";
    case ClassicKind::Count: return "count";
    case ClassicKind::AiScript: return "ai script";
    case ClassicKind::Order: return "order";
    case ClassicKind::Alliance: return "alliance";
    case ClassicKind::State: return "state";
    case ClassicKind::UnitProperties: return "unit properties";
    case ClassicKind::Percent: return "percent";
    }
    return "?";
}

// What `trigger-types` gives for one kind: how to write its values.
std::string kindHelp(ClassicKind k) {
    switch (k) {
    case ClassicKind::Player: return "1-12, or a group: Current player, Foes, Allies, Neutral players, All players, Force 1-4";
    case ClassicKind::Unit: return "a unit type id or name, or any unit, men, buildings, factories";
    case ClassicKind::Location: return "a location number or name (Anywhere: 64)";
    case ClassicKind::Switch: return "1-256, or the switch's name";
    case ClassicKind::Text: return "the text";
    case ClassicKind::Sound: return "a sound file's path in the map";
    case ClassicKind::Count: return "a number, or \"all\"";
    case ClassicKind::AiScript: return "the script's four letters (\"TMCu\") or its name";
    case ClassicKind::Milliseconds: return "milliseconds";
    case ClassicKind::Seconds: return "seconds";
    case ClassicKind::Percent: return "0-100";
    default: break;
    }
    const auto& choices = classicChoices(k);
    if (choices.empty()) return "a number";
    std::string s;
    for (const ClassicChoice& c : choices) s += (s.empty() ? "" : ", ") + std::string(c.name);
    return s;
}

// A map string as JSON gives it (UTF-8; CP949 and Windows-1252 read as the editor reads them).
std::string shown(const std::string& raw) { return text::narrow(text::widenLegacy(raw)); }

// The string id for a text: a map string that reads the same (in its own encoding), else a new one.
uint32_t textId(StringTable& strings, const std::string& text) {
    if (text.empty()) return 0;
    for (uint32_t i = 1; i <= strings.size(); ++i)
        if (strings.get(i) == text || shown(strings.get(i)) == text) return i;
    return strings.add(text);
}
// The text as the map has it when one of its strings reads the same (so writing it back makes no new string).
std::string rawText(const StringTable& strings, const std::string& text) {
    for (uint32_t i = 1; i <= strings.size(); ++i)
        if (shown(strings.get(i)) == text) return strings.get(i);
    return text;
}

const ClassicDef* classicDefByName(const std::string& name, bool condition) {
    for (const ClassicDef* d : condition ? classicConditionDefs() : classicActionDefs())
        if (lower(d->name) == lower(name)) return d;
    fail(std::string("no ") + (condition ? "condition" : "action") + " \"" + name + "\" (see `stardraft-cli trigger-types`)");
}

// A value of a parameter, from JSON.
uint32_t classicValueOf(Editor& ed, StringTable& strings, const ClassicParam& p, const Json& v, const std::vector<ClassicAiScript>& ai) {
    const std::string what = std::string("\"") + p.name + "\"";
    const std::string s = v.type == Json::Type::String ? lower(v.s) : std::string();
    auto choice = [&]() -> uint32_t {
        for (const ClassicChoice& c : classicChoices(p.kind))
            if (lower(c.name) == s) return c.value;
        fail(what + " must be one of: " + kindHelp(p.kind));
    };
    switch (p.kind) {
    case ClassicKind::Player:
        if (v.type == Json::Type::Number) {
            if (v.n < 1 || v.n > 12) fail(what + " must be 1-12 or a group name");
            return uint32_t(v.n) - 1;
        }
        return choice();
    case ClassicKind::Unit:
        if (s == "no unit") return 228;
        if (s == "any unit") return 229;
        if (s == "men") return 230;
        if (s == "buildings") return 231;
        if (s == "factories") return 232;
        return unitTypeId(ed, v, p.name);
    case ClassicKind::Location:
        if (v.type == Json::Type::Number) {
            if (v.n == 0) return 0;  // no location (an unset destination)
            if (v.n < 1 || v.n > ed.locationCount()) fail(what + ": no location " + std::to_string(int(v.n)));
            return uint32_t(v.n);
        }
        for (int i = 0; i < ed.locationCount(); ++i)
            if ((ed.locationUsed(i) || i == kAnywhereLocation) && lower(shown(ed.locationName(i))) == s) return uint32_t(i + 1);
        fail(what + ": no location \"" + v.s + "\"");
    case ClassicKind::Switch:
        if (v.type == Json::Type::Number) {
            if (v.n < 1 || v.n > 256) fail(what + " must be 1-256");
            return uint32_t(v.n) - 1;
        }
        for (int i = 0; i < 256; ++i)
            if (!ed.switchName(i).empty() && lower(shown(ed.switchName(i))) == s) return uint32_t(i);
        fail(what + ": no switch named \"" + v.s + "\"");
    case ClassicKind::Text:
    case ClassicKind::Sound:
        if (v.type != Json::Type::String) fail(what + " must be a text");
        return textId(strings, v.s);
    case ClassicKind::Count:
        if (s == "all") return 0;
        if (v.type != Json::Type::Number || v.n < 0 || v.n > 255) fail(what + " must be 1-255 or \"all\"");
        return uint32_t(v.n);
    case ClassicKind::AiScript:
        if (v.type == Json::Type::Number && v.n >= 0 && v.n <= 4294967295.0) return uint32_t(v.n);  // (0: none)
        if (v.type != Json::Type::String) fail(what + " must be a script's four letters or name");
        for (const ClassicAiScript& a : ai)
            if (lower(a.name) == s) return a.id;
        if (v.s.size() == 4) return uint32_t(uint8_t(v.s[0])) | uint32_t(uint8_t(v.s[1])) << 8 | uint32_t(uint8_t(v.s[2])) << 16 | uint32_t(uint8_t(v.s[3])) << 24;
        fail(what + ": no AI script \"" + v.s + "\"");
    default:
        if (!classicChoices(p.kind).empty()) {
            if (v.type == Json::Type::Number) {
                for (const ClassicChoice& c : classicChoices(p.kind))
                    if (c.value == uint32_t(v.n)) return c.value;
                fail(what + " must be one of: " + kindHelp(p.kind));
            }
            return choice();
        }
        if (v.type != Json::Type::Number || v.n < 0 || v.n > 4294967295.0) fail(what + " must be a number from 0 to 4294967295");
        return uint32_t(v.n);
    }
}

// A value as JSON in the form classicValueOf reads (so `get triggers` can be edited and given back).
Json classicValueJson(const Editor& ed, ClassicKind k, uint32_t v) {
    switch (k) {
    case ClassicKind::Player:
        if (v < 12) return jnum(v + 1);
        for (const ClassicChoice& c : classicChoices(k))
            if (c.value == v) return jstr(c.name);
        return jstr("group " + std::to_string(v));
    case ClassicKind::Unit:
        if (v >= 228 && v <= 232) return jstr(v == 228 ? "no unit" : v == 229 ? "any unit" : v == 230 ? "men" : v == 231 ? "buildings" : "factories");
        return jnum(v);
    case ClassicKind::Switch: return jnum(v + 1);
    case ClassicKind::Text:
    case ClassicKind::Sound: return jstr(ed.strings().get(v));
    case ClassicKind::Count: return v ? jnum(v) : jstr("all");
    case ClassicKind::AiScript: return v ? jstr(classicFourCC(v)) : jnum(0);
    default:
        for (const ClassicChoice& c : classicChoices(k))
            if (c.value == v) return jstr(c.name);
        return jnum(v);
    }
}

// --- Triggers ----------------------------------------------------------------------------------------------------

// The groups a trigger (or briefing) runs for: 1-8, or a group name.
template <size_t N>
void readGroups(const Json& v, std::array<uint8_t, N>& out) {
    if (v.type != Json::Type::Array || v.items.empty()) fail("\"players\" must be a list: 1-8, \"All players\", \"Force 1\", ...");
    out.fill(0);
    for (const Json& g : v.items) {
        if (g.type == Json::Type::Number) {
            if (g.n < 1 || g.n > 12) fail("\"players\": a player must be 1-12");
            out[size_t(g.n) - 1] = 1;
            continue;
        }
        bool ok = false;
        for (const ClassicChoice& c : classicChoices(ClassicKind::Player))
            if (lower(c.name) == lower(g.s) && c.value < N) out[c.value] = 1, ok = true;
        if (lower(g.s).rfind("group ", 0) == 0) {  // a group the names do not cover ("group 22")
            const int n = std::atoi(g.s.c_str() + 6);
            if (n >= 0 && size_t(n) < N) out[size_t(n)] = 1, ok = true;
        }
        if (!ok) fail("\"players\": no group \"" + g.s + "\"");
    }
}
ClassicTrigger triggerFromJson(Editor& ed, StringTable& strings, const Json& j, const std::vector<ClassicAiScript>& ai) {
    ClassicTrigger t = ClassicTrigger::make();
    readGroups(need(j, "players"), t.players);
    auto entries = [&](const char* key, bool condition) {
        const Json* list = j.get(key);
        if (!list) return;
        if (list->type != Json::Type::Array) fail(std::string("\"") + key + "\" must be a list");
        if (condition) t.conditions.fill(ClassicCondition{});
        int i = 0;
        for (const Json& e : list->items) {
            const std::string where = std::string(key) + " " + std::to_string(i++) + ": ";
            try {
                const ClassicDef* d = classicDefByName(e.str("type"), condition);
                if (e.get("eud")) fail("EUD conditions and actions cannot be edited with the CLI yet");
                // Play WAV and Transmission: the sound's length (ms) is in the time field, which no parameter names.
                const bool wavTime = !condition && std::any_of(d->params.begin(), d->params.end(), [](const ClassicParam& p) { return p.kind == ClassicKind::Sound; }) &&
                                     std::none_of(d->params.begin(), d->params.end(), [](const ClassicParam& p) { return p.field == ClassicField::Time; });
                for (const auto& f : e.fields) {
                    if (f.first == "type" || f.first == "disabled" || f.first == "sentence" || f.first == "flags") continue;
                    if (wavTime && f.first == "wav_time") continue;
                    if (std::none_of(d->params.begin(), d->params.end(), [&](const ClassicParam& p) { return f.first == p.name; })) {
                        std::string names;
                        for (const ClassicParam& p : d->params) names += (names.empty() ? "" : ", ") + std::string(p.name);
                        fail("\"" + std::string(d->name) + "\" has no \"" + f.first + "\" (it has: " + (names.empty() ? "nothing" : names) + ")");
                    }
                }
                if (condition) {
                    ClassicCondition c = makeClassicCondition(d->type);
                    for (const ClassicParam& p : d->params)
                        if (const Json* v = e.get(p.name)) setClassicValue(c, p.field, classicValueOf(ed, strings, p, *v, ai));
                    if (e.get("flags")) c.flags = uint8_t(int(num(e, "flags")) & ~2);
                    if (e.boolean("disabled", false)) c.flags |= 2;
                    if (!t.addCondition(c)) fail("a trigger has at most 16 conditions");
                } else {
                    ClassicAction a = makeClassicAction(d->type);
                    for (const ClassicParam& p : d->params)
                        if (const Json* v = e.get(p.name)) setClassicValue(a, p.field, classicValueOf(ed, strings, p, *v, ai));
                    if (wavTime) a.time = uint32_t(num(e, "wav_time", 0));
                    if (e.get("flags")) a.flags = uint8_t(int(num(e, "flags")) & ~2);
                    if (e.boolean("disabled", false)) a.flags |= 2;
                    if (!t.addAction(a)) fail("a trigger has at most 64 actions");
                }
            } catch (const CliError& err) {
                fail(where + err.what());
            }
        }
        if (condition && !t.conditionCount()) t.conditions[0] = makeClassicCondition(22);  // no conditions: Always
    };
    entries("conditions", true);
    entries("actions", false);
    if (j.boolean("disabled", false)) t.setDisabled(true);
    return t;
}

Json triggerJson(const Editor& ed, const ClassicTrigger& t, int index, const ClassicNames& names) {
    Json players = jarr(), conditions = jarr(), actions = jarr();
    for (int g = 0; g < kClassicGroups; ++g)
        if (t.players[size_t(g)]) players.items.push_back(classicValueJson(ed, ClassicKind::Player, uint32_t(g)));
    auto entry = [&](const ClassicDef* d, auto& e, const std::string& text) {
        Obj o;
        o.add("type", d ? std::string(d->name) : std::to_string(int(e.type)));
        if (d)
            for (const ClassicParam& p : d->params) o.add(p.name, classicValueJson(ed, p.kind, classicValue(e, p.field)));
        if constexpr (std::is_same_v<std::decay_t<decltype(e)>, ClassicAction>)
            if (d && std::any_of(d->params.begin(), d->params.end(), [](const ClassicParam& p) { return p.kind == ClassicKind::Sound; }) &&
                std::none_of(d->params.begin(), d->params.end(), [](const ClassicParam& p) { return p.field == ClassicField::Time; }))
                o.add("wav_time", double(e.time));
        if (d && (e.flags & ~2) != (d->flags & ~2)) o.add("flags", int(e.flags & ~2));  // (the type's usual flags are not given)
        if (e.flags & 2) o.add("disabled", true);
        if (classicEud(e)) o.add("eud", true);
        o.add("sentence", text);  // (not "text": that is a parameter of some)
        return Json(o);
    };
    for (int i = 0; i < t.conditionCount(); ++i)
        conditions.items.push_back(entry(classicConditionDef(t.conditions[size_t(i)].type), t.conditions[size_t(i)], classicConditionText(t.conditions[size_t(i)], names)));
    for (int i = 0; i < t.actionCount(); ++i)
        actions.items.push_back(entry(classicActionDef(t.actions[size_t(i)].type), t.actions[size_t(i)], classicActionText(t.actions[size_t(i)], names)));
    Obj o;
    o.add("index", index).add("players", players).add("conditions", conditions).add("actions", actions);
    if (t.disabled()) o.add("disabled", true);
    return o;
}

Json triggersJson(const Editor& ed) {
    const ClassicNames names = classicNames(ed);
    const ClassicTriggers t = ed.classicTriggers();
    Json a = jarr();
    for (size_t i = 0; i < t.triggers.size(); ++i) a.items.push_back(triggerJson(ed, t.triggers[i], int(i), names));
    return a;
}

Json triggerTypesJson() {
    auto defs = [](const std::vector<const ClassicDef*>& list) {
        Json a = jarr();
        for (const ClassicDef* d : list) {
            Json params = jarr();
            for (const ClassicParam& p : d->params)
                params.items.push_back(Obj().add("name", p.name).add("kind", kindName(p.kind)).add("values", kindHelp(p.kind)));
            a.items.push_back(Obj().add("type", d->name).add("sentence", d->sentence).add("params", params));
        }
        return a;
    };
    return Obj().add("conditions", defs(classicConditionDefs())).add("actions", defs(classicActionDefs()));
}

int triggerIndex(const Editor& ed, const Json& op) {
    const int i = int(num(op, "index"));
    if (i < 0 || i >= int(ed.classicTriggers().triggers.size())) fail("no trigger " + std::to_string(i));
    return i;
}

// --- Briefings ---------------------------------------------------------------------------------------------------

struct BriefingType {
    uint8_t type;
    const char* name;
};
constexpr BriefingType kBriefingTypes[] = {{BriefingAction::Wait, "wait"},
                                           {BriefingAction::PlayWav, "play_wav"},
                                           {BriefingAction::TextMessage, "text"},
                                           {BriefingAction::Objectives, "objectives"},
                                           {BriefingAction::ShowPortrait, "show_portrait"},
                                           {BriefingAction::HidePortrait, "hide_portrait"},
                                           {BriefingAction::SpeakingPortrait, "speaking_portrait"},
                                           {BriefingAction::Transmission, "transmission"},
                                           {BriefingAction::SkipTutorial, "skip_tutorial"}};

std::vector<BriefingTrigger> briefingsFromJson(Editor& ed, const Json& list) {
    if (list.type != Json::Type::Array) fail("\"briefings\" must be a list");
    std::vector<BriefingTrigger> out;
    for (const Json& j : list.items) {
        BriefingTrigger b;
        std::array<uint8_t, 27> groups{};
        readGroups(need(j, "players"), groups);
        for (size_t g = 0; g < groups.size(); ++g) b.players[g] = groups[g] != 0;
        const Json& actions = need(j, "actions");
        if (actions.type != Json::Type::Array) fail("\"actions\" must be a list");
        for (const Json& e : actions.items) {
            BriefingAction a;
            const std::string type = lower(e.str("type"));
            for (const BriefingType& t : kBriefingTypes)
                if (type == t.name) a.type = t.type;
            if (a.type == BriefingAction::None)
                fail("briefing action \"" + e.str("type") + "\": the types are wait, play_wav, text, objectives, show_portrait, hide_portrait, "
                     "speaking_portrait, transmission, skip_tutorial");
            a.text = e.str("text");
            a.wav = e.str("wav");
            a.time = uint32_t(num(e, "time", 0));
            a.wavTime = uint32_t(num(e, "wav_time", 0));
            if (e.get("slot")) {
                const int slot = int(num(e, "slot"));
                if (slot < 1 || slot > 4) fail("\"slot\" must be 1-4");
                a.slot = slot - 1;
            }
            if (e.get("unit")) a.unit = unitTypeId(ed, need(e, "unit"), "unit");
            if (e.boolean("disabled", false)) a.flags |= 2;
            a.text = rawText(ed.strings(), a.text);
            a.wav = rawText(ed.strings(), a.wav);
            if (const Json* m = e.get("modifier")) {
                const std::string s = lower(m->s);
                a.modifier = s == "add" ? BriefingAction::Add : s == "subtract" ? BriefingAction::Subtract : BriefingAction::SetTo;
            }
            b.actions.push_back(a);
        }
        if (b.actions.size() > 64) fail("a briefing trigger has at most 64 actions");
        out.push_back(b);
    }
    return out;
}

Json briefingsJson(const Editor& ed) {
    Json list = jarr();
    for (const BriefingTrigger& b : ed.briefings()) {
        std::array<uint8_t, 27> groups{};
        for (size_t g = 0; g < groups.size(); ++g) groups[g] = b.players[g];
        Json players = jarr(), actions = jarr();
        for (size_t g = 0; g < groups.size(); ++g)
            if (groups[g]) players.items.push_back(classicValueJson(ed, ClassicKind::Player, uint32_t(g)));
        for (const BriefingAction& a : b.actions) {
            Obj o;
            const char* name = "?";
            for (const BriefingType& t : kBriefingTypes)
                if (t.type == a.type) name = t.name;
            o.add("type", name);
            switch (a.type) {
            case BriefingAction::Wait: o.add("time", double(a.time)); break;
            case BriefingAction::PlayWav: o.add("wav", a.wav).add("wav_time", double(a.wavTime)); break;
            case BriefingAction::TextMessage: o.add("text", a.text).add("time", double(a.time)); break;
            case BriefingAction::Objectives: o.add("text", a.text); break;
            case BriefingAction::ShowPortrait: o.add("unit", int(a.unit)).add("slot", a.slot + 1); break;
            case BriefingAction::HidePortrait: o.add("slot", a.slot + 1); break;
            case BriefingAction::SpeakingPortrait: o.add("slot", a.slot + 1).add("time", double(a.time)); break;
            case BriefingAction::Transmission:
                o.add("text", a.text).add("slot", a.slot + 1).add("wav", a.wav).add("wav_time", double(a.wavTime)).add("time", double(a.time));
                o.add("modifier", a.modifier == BriefingAction::Add ? "add" : a.modifier == BriefingAction::Subtract ? "subtract" : "set to");
                break;
            default: break;
            }
            if (a.disabled()) o.add("disabled", true);
            actions.items.push_back(o);
        }
        list.items.push_back(Obj().add("players", players).add("actions", actions));
    }
    return list;
}

// --- Unit, upgrade and tech settings -----------------------------------------------------------------------------

int entryId(const Json& v, const std::string& dat, int count, const char* what) {
    if (v.type == Json::Type::Number) {
        if (v.n < 0 || v.n >= count) fail(std::string(what) + ": no " + std::to_string(int(v.n)));
        return int(v.n);
    }
    const auto& names = DatSchema::entryNames(dat);
    for (int i = 0; i < count && i < int(names.size()); ++i)
        if (lower(names[size_t(i)]) == lower(v.s)) return i;
    fail(std::string(what) + ": no \"" + v.s + "\" (see `get <map> upgrade-settings` or `tech-settings`)");
}
std::string entryLabel(const std::string& dat, int i) {
    const auto& names = DatSchema::entryNames(dat);
    return i < int(names.size()) ? names[size_t(i)] : std::to_string(i);
}

// Per player (1-12): {"1": value, ...} for the players that do not use the default.
template <typename F>
Json perPlayer(const std::array<bool, kSettingPlayers>& usesDefault, F value) {
    Obj o;
    for (int p = 0; p < kSettingPlayers; ++p)
        if (!usesDefault[size_t(p)]) o.add(std::to_string(p + 1), value(p));
    return o;
}
// Reads {"1": ..., "12": ...}.
template <typename F>
void readPerPlayer(const Json& v, F apply) {
    if (v.type != Json::Type::Object) fail("\"players\" must be an object: {\"1\": ..., \"8\": ...}");
    for (const auto& f : v.fields) {
        const int p = std::atoi(f.first.c_str());
        if (p < 1 || p > kSettingPlayers) fail("\"players\": no player " + f.first);
        apply(p - 1, f.second);
    }
}

Json unitSettingsJson(const Editor& ed, bool all) {
    const UnitSettings s = ed.unitSettings();
    const SettingDefaults* d = ed.settingDefaults();
    Json a = jarr();
    for (int u = 0; u < kSettingUnits; ++u) {
        const UnitSettings::Unit& v = s.units[size_t(u)];
        const UnitSettings::Availability& av = s.availability[size_t(u)];
        const bool custom = !v.useDefault || !av.global || std::any_of(av.usesDefault.begin(), av.usesDefault.end(), [](bool b) { return !b; });
        if (!all && !custom) continue;
        Obj o;
        o.add("unit", u).add("unit_name", ed.unitTypes()[uint16_t(u)].name).add("use_default", v.useDefault);
        const UnitSettings::Unit& shown = v.useDefault && d ? d->units.units[size_t(u)] : v;
        o.add("hit_points", double(shown.hitPoints) / 256).add("shields", int(shown.shields)).add("armor", int(shown.armor));
        o.add("build_time", int(shown.buildTime)).add("minerals", int(shown.minerals)).add("gas", int(shown.gas));
        if (const std::string n = ed.unitSettingName(u); !n.empty()) o.add("name", n);  // (its name in this map)
        o.add("available", av.global).add("players", perPlayer(av.usesDefault, [&](int p) { return jbool(av.player[size_t(p)]); }));
        a.items.push_back(o);
    }
    return a;
}

Json upgradeSettingsJson(const Editor& ed, bool all) {
    const UpgradeSettings s = ed.upgradeSettings();
    const SettingDefaults* d = ed.settingDefaults();
    Json a = jarr();
    for (int u = 0; u < kSettingUpgrades; ++u) {
        const UpgradeSettings::Upgrade& v = s.upgrades[size_t(u)];
        const UpgradeSettings::Levels& lv = s.levels[size_t(u)];
        const bool levelsDefault = d && lv.defaultMax == d->upgrades.levels[size_t(u)].defaultMax && lv.defaultStart == d->upgrades.levels[size_t(u)].defaultStart;
        const bool custom = !v.useDefault || !levelsDefault || std::any_of(lv.usesDefault.begin(), lv.usesDefault.end(), [](bool b) { return !b; });
        if (!all && !custom) continue;
        const UpgradeSettings::Upgrade& shown = v.useDefault && d ? d->upgrades.upgrades[size_t(u)] : v;
        a.items.push_back(Obj()
                              .add("upgrade", u)
                              .add("name", entryLabel("upgrades", u))
                              .add("use_default", v.useDefault)
                              .add("minerals", int(shown.minerals))
                              .add("minerals_per_level", int(shown.mineralsPerLevel))
                              .add("gas", int(shown.gas))
                              .add("gas_per_level", int(shown.gasPerLevel))
                              .add("time", int(shown.time))
                              .add("time_per_level", int(shown.timePerLevel))
                              .add("max_level", int(lv.defaultMax))
                              .add("start_level", int(lv.defaultStart))
                              .add("players", perPlayer(lv.usesDefault, [&](int p) {
                                  return Json(Obj().add("max_level", int(lv.max[size_t(p)])).add("start_level", int(lv.start[size_t(p)])));
                              })));
    }
    return a;
}

Json techSettingsJson(const Editor& ed, bool all) {
    const TechSettings s = ed.techSettings();
    const SettingDefaults* d = ed.settingDefaults();
    Json a = jarr();
    for (int t = 0; t < kSettingTechs; ++t) {
        const TechSettings::Tech& v = s.techs[size_t(t)];
        const TechSettings::Players& pl = s.players[size_t(t)];
        const bool playersDefault = d && pl.defaultAvailable == d->techs.players[size_t(t)].defaultAvailable &&
                                    pl.defaultResearched == d->techs.players[size_t(t)].defaultResearched;
        const bool custom = !v.useDefault || !playersDefault || std::any_of(pl.usesDefault.begin(), pl.usesDefault.end(), [](bool b) { return !b; });
        if (!all && !custom) continue;
        const TechSettings::Tech& shown = v.useDefault && d ? d->techs.techs[size_t(t)] : v;
        a.items.push_back(Obj()
                              .add("tech", t)
                              .add("name", entryLabel("techdata", t))
                              .add("use_default", v.useDefault)
                              .add("minerals", int(shown.minerals))
                              .add("gas", int(shown.gas))
                              .add("time", int(shown.time))
                              .add("energy", int(shown.energy))
                              .add("available", pl.defaultAvailable)
                              .add("researched", pl.defaultResearched)
                              .add("players", perPlayer(pl.usesDefault, [&](int p) {
                                  return Json(Obj().add("available", bool(pl.available[size_t(p)])).add("researched", bool(pl.researched[size_t(p)])));
                              })));
    }
    return a;
}

// Sets a number field from the op when it is there; returns whether it was.
template <typename T>
bool setField(const Json& op, const char* k, T& field, double scale = 1) {
    if (!op.get(k)) return false;
    const double v = std::round(num(op, k) * scale);
    if (!(v >= 0 && v <= double(std::numeric_limits<T>::max()))) fail(std::string("\"") + k + "\" is out of range");
    field = T(v);
    return true;
}

Json setUnitSettings(Editor& ed, const Json& op) {
    const uint16_t u = unitTypeId(ed, need(op, "unit"), "unit");
    if (u >= kSettingUnits) fail("no settings for unit " + std::to_string(u));
    UnitSettings s = ed.unitSettings();
    UnitSettings::Unit& v = s.units[u];
    const SettingDefaults* d = ed.settingDefaults();
    if (v.useDefault && d) {  // the values to change start as the game's
        const uint16_t name = v.nameString;
        v = d->units.units[u];
        v.nameString = name;
    }
    bool stats = false;
    stats |= setField(op, "hit_points", v.hitPoints, 256);
    stats |= setField(op, "shields", v.shields);
    stats |= setField(op, "armor", v.armor);
    stats |= setField(op, "build_time", v.buildTime);
    stats |= setField(op, "minerals", v.minerals);
    stats |= setField(op, "gas", v.gas);
    if (stats || op.get("name")) v.useDefault = false;
    if (op.boolean("use_default", false)) v.useDefault = true;
    UnitSettings::Availability& av = s.availability[u];
    if (const Json* a = op.get("available")) av.global = a->b;
    if (const Json* p = op.get("players"))
        readPerPlayer(*p, [&](int pl, const Json& b) {
            if (b.type == Json::Type::Null) {
                av.usesDefault[size_t(pl)] = true;
                return;
            }
            av.usesDefault[size_t(pl)] = false;
            av.player[size_t(pl)] = b.b;
        });
    std::vector<std::pair<int, std::string>> names;
    if (op.get("name")) names.push_back({u, op.str("name")});
    ed.setUnitSettings(s, names);
    return Obj().add("unit", int(u));
}

Json setUpgradeSettings(Editor& ed, const Json& op) {
    const int u = entryId(need(op, "upgrade"), "upgrades", kSettingUpgrades, "upgrade");
    UpgradeSettings s = ed.upgradeSettings();
    UpgradeSettings::Upgrade& v = s.upgrades[size_t(u)];
    const SettingDefaults* d = ed.settingDefaults();
    if (v.useDefault && d) v = d->upgrades.upgrades[size_t(u)];
    bool stats = false;
    stats |= setField(op, "minerals", v.minerals);
    stats |= setField(op, "minerals_per_level", v.mineralsPerLevel);
    stats |= setField(op, "gas", v.gas);
    stats |= setField(op, "gas_per_level", v.gasPerLevel);
    stats |= setField(op, "time", v.time);
    stats |= setField(op, "time_per_level", v.timePerLevel);
    if (stats) v.useDefault = false;
    if (op.boolean("use_default", false)) v.useDefault = true;
    UpgradeSettings::Levels& lv = s.levels[size_t(u)];
    setField(op, "max_level", lv.defaultMax);
    setField(op, "start_level", lv.defaultStart);
    if (const Json* p = op.get("players"))
        readPerPlayer(*p, [&](int pl, const Json& o) {
            if (o.type == Json::Type::Null) {
                lv.usesDefault[size_t(pl)] = true;
                return;
            }
            if (lv.usesDefault[size_t(pl)]) lv.max[size_t(pl)] = lv.defaultMax, lv.start[size_t(pl)] = lv.defaultStart;
            lv.usesDefault[size_t(pl)] = false;
            setField(o, "max_level", lv.max[size_t(pl)]);
            setField(o, "start_level", lv.start[size_t(pl)]);
        });
    ed.setUpgradeSettings(s);
    return Obj().add("upgrade", u).add("name", entryLabel("upgrades", u));
}

Json setTechSettings(Editor& ed, const Json& op) {
    const int t = entryId(need(op, "tech"), "techdata", kSettingTechs, "tech");
    TechSettings s = ed.techSettings();
    TechSettings::Tech& v = s.techs[size_t(t)];
    const SettingDefaults* d = ed.settingDefaults();
    if (v.useDefault && d) v = d->techs.techs[size_t(t)];
    bool stats = false;
    stats |= setField(op, "minerals", v.minerals);
    stats |= setField(op, "gas", v.gas);
    stats |= setField(op, "time", v.time);
    stats |= setField(op, "energy", v.energy);
    if (stats) v.useDefault = false;
    if (op.boolean("use_default", false)) v.useDefault = true;
    TechSettings::Players& pl = s.players[size_t(t)];
    if (const Json* a = op.get("available")) pl.defaultAvailable = a->b;
    if (const Json* r = op.get("researched")) pl.defaultResearched = r->b;
    if (const Json* p = op.get("players"))
        readPerPlayer(*p, [&](int i, const Json& o) {
            if (o.type == Json::Type::Null) {
                pl.usesDefault[size_t(i)] = true;
                return;
            }
            if (pl.usesDefault[size_t(i)]) pl.available[size_t(i)] = pl.defaultAvailable, pl.researched[size_t(i)] = pl.defaultResearched;
            pl.usesDefault[size_t(i)] = false;
            if (const Json* a = o.get("available")) pl.available[size_t(i)] = a->b;
            if (const Json* r = o.get("researched")) pl.researched[size_t(i)] = r->b;
        });
    ed.setTechSettings(s);
    return Obj().add("tech", t).add("name", entryLabel("techdata", t));
}

// --- The operations of this file (applyOp asks here first; nullopt: not one of them) ------------------------------

std::optional<Json> applyMapPartOp(Editor& ed, const std::string& name, const Json& op, const std::vector<ClassicAiScript>& ai) {
    if (name == "add_trigger" || name == "set_trigger") {
        StringTable strings = ed.strings();
        const ClassicTrigger t = triggerFromJson(ed, strings, need(op, "trigger"), ai);
        if (name == "set_trigger") {
            const int i = triggerIndex(ed, op);
            ed.setClassicTrigger(i, t, &strings);
            return Json(Obj().add("index", i));
        }
        const int count = int(ed.classicTriggers().triggers.size());
        const int at = op.get("at") ? std::clamp(int(num(op, "at")), 0, count) : count;
        ed.addClassicTrigger(t, at, &strings);
        return Json(Obj().add("index", at));
    }
    if (name == "add_triggers") {  // many at once (one string table for all of them)
        StringTable strings = ed.strings();
        ClassicTriggers all = ed.classicTriggers();
        const Json& list = need(op, "triggers");
        if (list.type != Json::Type::Array) fail("\"triggers\" must be a list");
        const int first = int(all.triggers.size());
        for (size_t i = 0; i < list.items.size(); ++i) {
            try {
                all.triggers.push_back(triggerFromJson(ed, strings, list.items[i], ai));
            } catch (const CliError& e) {
                fail("trigger " + std::to_string(i) + ": " + e.what());
            }
        }
        ed.setClassicTriggers(all, &strings);
        return Json(Obj().add("first", first).add("count", int(list.items.size())));
    }
    if (name == "delete_trigger") {
        const int i = triggerIndex(ed, op);
        ed.removeClassicTrigger(i);
        return Json(Obj());
    }
    if (name == "move_trigger") {
        const int i = triggerIndex(ed, op);
        const int to = int(num(op, "to"));
        if (to < 0 || to >= int(ed.classicTriggers().triggers.size())) fail("\"to\": no trigger " + std::to_string(to));
        ed.moveClassicTrigger(i, to);
        return Json(Obj());
    }
    if (name == "set_briefings") {
        ed.setBriefings(briefingsFromJson(ed, need(op, "briefings")));
        return Json(Obj().add("count", int(ed.briefings().size())));
    }
    if (name == "add_briefing") {
        std::vector<BriefingTrigger> all = ed.briefings();
        Json one = jarr();
        one.items.push_back(op);
        for (const BriefingTrigger& b : briefingsFromJson(ed, one)) all.push_back(b);
        ed.setBriefings(all);
        return Json(Obj().add("index", int(all.size()) - 1));
    }
    if (name == "set_unit_settings") return setUnitSettings(ed, op);
    if (name == "set_upgrade_settings") return setUpgradeSettings(ed, op);
    if (name == "set_tech_settings") return setTechSettings(ed, op);
    return std::nullopt;
}
