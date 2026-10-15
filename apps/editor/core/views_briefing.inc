// The Briefing workspace's edit model (portable, core.inc): the map's briefings as one list of actions in order, the
// rows of the briefing shown (its player list), and the regrouping of the actions into triggers (a trigger holds 64).
// The page (briefing_view.inc, Win32) keeps the selection, the clipboard and the preview; it passes the player list
// shown (`who`) to these functions.

std::vector<BriefingTrigger> gBriefData;  // the map's, as last read
uint64_t gBriefRev = ~0ull;
std::array<int, 8> playerForces() {
    std::array<int, 8> f{};
    for (int p = 0; p < 8; ++p) f[size_t(p)] = shellState().ed ? shellState().ed->player(p).force : 0;
    return f;
}
const std::vector<BriefingTrigger>& briefings() {
    if (shellState().ed && shellState().ed->revision() != gBriefRev) gBriefData = shellState().ed->briefings(), gBriefRev = shellState().ed->revision();
    return gBriefData;
}

// The briefing's rows: every action of every trigger with its player list, in order.
struct BriefRow {
    int trig, act;
};
std::vector<BriefRow> gBriefRows;
const std::vector<BriefRow>& briefRows(const std::array<bool, 27>& who) {
    gBriefRows.clear();
    const auto& b = briefings();
    for (size_t t = 0; t < b.size(); ++t)
        if (b[t].players == who)
            for (size_t a = 0; a < b[t].actions.size(); ++a) gBriefRows.push_back({int(t), int(a)});
    return gBriefRows;
}
// The players a player list is for (players 1-8: themselves, all players, their force).
std::vector<int> whoPlayers(const std::array<bool, 27>& who) {
    std::vector<int> out;
    const auto forces = playerForces();
    for (int p = 0; p < 8; ++p)
        if (who[size_t(p)] || who[17] || who[size_t(18 + forces[size_t(p)])]) out.push_back(p);
    return out;
}
std::wstring ownerName(int owner) {
    if (owner >= 0 && owner < 8) return L"Player " + std::to_wstring(owner + 1);
    if (owner == 17) return L"All players";
    if (owner >= 18 && owner <= 21) {
        std::string n = shellState().ed ? shellState().ed->force(owner - 18).name : "";
        n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }), n.end());  // color codes
        const bool blank = n.find_first_not_of(' ') == std::string::npos;
        return blank ? L"Force " + std::to_wstring(owner - 17) : widen(n);
    }
    return L"";
}
// Whom a briefing is for, in words: "Force 1, Player 3"; "Nobody" when its list is empty.
std::wstring whoName(const std::array<bool, 27>& who) {
    std::wstring out;
    for (int o : {17, 18, 19, 20, 21, 0, 1, 2, 3, 4, 5, 6, 7})
        if (who[size_t(o)]) out += (out.empty() ? L"" : L", ") + ownerName(o);
    return out.empty() ? L"Nobody" : out;
}
// The map's briefings: each player list its triggers have, in the order they first come.
std::vector<std::array<bool, 27>> briefList() {
    std::vector<std::array<bool, 27>> out;
    for (const auto& t : briefings())
        if (std::find(out.begin(), out.end(), t.players) == out.end()) out.push_back(t.players);
    return out;
}
int briefActionCount(const std::array<bool, 27>& who) {
    int n = 0;
    for (const auto& t : briefings()) n += t.players == who ? int(t.actions.size()) : 0;
    return n;
}

// The briefing's actions as one list, each with its trigger (-2: a new trigger with its player list), and back.
struct FlatAction {
    int trig;
    BriefingAction a;
};
std::vector<FlatAction> briefFlat(const std::vector<BriefingTrigger>& b, const std::array<bool, 27>& who) {
    std::vector<FlatAction> out;
    for (size_t t = 0; t < b.size(); ++t)
        if (b[t].players == who)
            for (const auto& a : b[t].actions) out.push_back({int(t), a});
    return out;
}
void briefRegroup(std::vector<BriefingTrigger>& b, std::vector<FlatAction>& flat, const std::array<bool, 27>& who) {
    if (std::any_of(flat.begin(), flat.end(), [](const FlatAction& f) { return f.trig == -2; })) {  // the briefing's first trigger
        BriefingTrigger t;
        t.players = who;
        b.push_back(t);
        for (auto& f : flat)
            if (f.trig == -2) f.trig = int(b.size()) - 1;
    }
    for (size_t t = 0; t < b.size(); ++t) {
        if (b[t].players != who) continue;
        b[t].actions.clear();
        for (const auto& f : flat)
            if (f.trig == int(t)) b[t].actions.push_back(f.a);
    }
    // A trigger holds 64 actions: the rest go on in a copy of it, right after.
    for (size_t t = 0; t < b.size(); ++t)
        if (b[t].actions.size() > 64) {
            BriefingTrigger more = b[t];
            more.actions.assign(b[t].actions.begin() + 64, b[t].actions.end());
            b[t].actions.resize(64);
            b.insert(b.begin() + long(t) + 1, more);
        }
}
// The trigger new actions at row `at` go into: that row's, the one before it, or a new one.
int briefTagAt(const std::vector<FlatAction>& flat, int at) {
    if (at >= 0 && at < int(flat.size())) return flat[size_t(at)].trig;
    if (!flat.empty()) return flat.back().trig;
    return -2;
}

// --- The page's model, for both programs (briefing_view.inc on Windows, views_briefing_page.inc on the SDL program) ---

// A time on the page: "0:05.0".
std::wstring briefTime(uint32_t ms) {
    wchar_t b[24];
    std::swprintf(b, 24, L"%u:%04.1f", ms / 60000, (ms % 60000) / 1000.0);
    return b;
}
const wchar_t* briefTypeName(uint8_t t) {
    static const wchar_t* n[] = {L"No action", L"Wait", L"Play sound", L"Text message", L"Mission objectives", L"Show portrait", L"Hide portrait",
                                 L"Talking portrait", L"Transmission", L"Skip tutorial enabled"};
    return t < 10 ? n[t] : L"Unknown";
}
// The bar of an action's kind at the left of its block.
COLORREF briefTypeColor(uint8_t t) {
    switch (t) {
    case BriefingAction::Wait: return RGB(220, 184, 104);
    case BriefingAction::PlayWav: return RGB(96, 200, 210);
    case BriefingAction::ShowPortrait:
    case BriefingAction::HidePortrait:
    case BriefingAction::SpeakingPortrait: return RGB(214, 140, 200);
    case BriefingAction::SkipTutorial: return RGB(170, 150, 240);
    default: return kAccent;
    }
}
// The action has a text (its editor shows under the list).
inline bool briefHasText(uint8_t t) { return t == BriefingAction::TextMessage || t == BriefingAction::Objectives || t == BriefingAction::Transmission; }
// The timeline's lane of an action: 0 sounds, 1 waits, 2 the rest.
inline int briefLane(uint8_t t) { return t == BriefingAction::Transmission || t == BriefingAction::PlayWav ? 0 : t == BriefingAction::Wait ? 1 : 2; }
// A new action of a kind, with the values it starts with.
inline BriefingAction briefNewAction(uint8_t type) {
    BriefingAction a;
    a.type = type;
    if (a.type == BriefingAction::Wait) a.time = 1000;
    if (a.type == BriefingAction::TextMessage) a.time = 5000;
    return a;
}

// The player the preview plays for a briefing: a human or a computer it is for, else the first it is for.
int briefPreviewPlayer(const std::array<bool, 27>& who) {
    const auto ps = whoPlayers(who);
    const Editor* ed = shellState().ed.get();
    for (int p : ps)
        if (ed && (ed->player(p).controller == Editor::Controller::Human || ed->player(p).controller == Editor::Controller::Computer)) return p;
    return ps.empty() ? 0 : ps[0];
}
// New briefing: for the first player who has no briefing of their own (else nobody yet: the user chooses).
std::array<bool, 27> briefNewWho() {
    const auto list = briefList();
    for (int p = 0; p < 8; ++p) {
        std::array<bool, 27> one{};
        one[size_t(p)] = true;
        if (std::find(list.begin(), list.end(), one) == list.end()) return one;
    }
    return {};
}
// The map's first briefing (none: a new one for player 1).
std::array<bool, 27> briefFirstWho() {
    const auto list = briefList();
    if (!list.empty()) return list.front();
    std::array<bool, 27> p1{};
    p1[0] = true;
    return p1;
}
// The briefing's triggers are for other players now (a check box of "For" changed).
void briefRetarget(std::vector<BriefingTrigger>& b, const std::array<bool, 27>& before, const std::array<bool, 27>& now) {
    for (auto& t : b)
        if (t.players == before) t.players = now;
}

// The selected rows in order (none picked: the focused one), each a row of the briefing shown (gBriefRows).
std::vector<int> briefPickedRowsOf(const std::set<int>& picked, int sel) {
    std::vector<int> rows(picked.begin(), picked.end());
    if (rows.empty() && sel >= 0) rows.push_back(sel);
    std::erase_if(rows, [](int r) { return r < 0 || r >= int(gBriefRows.size()); });
    return rows;
}
// The actions of rows (in order).
std::vector<BriefingAction> briefActionsOf(const std::vector<int>& rows) {
    std::vector<BriefingAction> out;
    for (int r : rows) out.push_back(briefings()[size_t(gBriefRows[size_t(r)].trig)].actions[size_t(gBriefRows[size_t(r)].act)]);
    return out;
}
// The rows (in order) go out of the briefing.
void briefEraseRows(std::vector<BriefingTrigger>& b, const std::array<bool, 27>& who, const std::vector<int>& rows) {
    auto flat = briefFlat(b, who);
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) flat.erase(flat.begin() + *it);
    briefRegroup(b, flat, who);
}
// Actions go in before row `at` (the end: after the last). Returns the row of the first one.
int briefInsertRows(std::vector<BriefingTrigger>& b, const std::array<bool, 27>& who, int at, const std::vector<BriefingAction>& acts) {
    auto flat = briefFlat(b, who);
    at = std::clamp(at, 0, int(flat.size()));
    const int tag = briefTagAt(flat, at == int(flat.size()) ? at - 1 : at);
    std::vector<FlatAction> add;
    for (const auto& a : acts) add.push_back({tag, a});
    flat.insert(flat.begin() + at, add.begin(), add.end());
    briefRegroup(b, flat, who);
    return at;
}
// The rows (in order) move so that they start before row `to` (as dropped there). Returns the row of the first one.
int briefMoveRows(std::vector<BriefingTrigger>& b, const std::array<bool, 27>& who, const std::vector<int>& rows, int to) {
    auto flat = briefFlat(b, who);
    to = std::clamp(to, 0, int(flat.size()));
    const int tag = briefTagAt(flat, to == int(flat.size()) ? to - 1 : to);
    std::vector<FlatAction> moved;
    for (int r : rows) moved.push_back(flat[size_t(r)]), moved.back().trig = tag;
    int before = 0;
    for (int r : rows) before += r < to;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) flat.erase(flat.begin() + *it);
    const int first = to - before;
    flat.insert(flat.begin() + first, moved.begin(), moved.end());
    briefRegroup(b, flat, who);
    return first;
}
// Alt+Up / Alt+Down: where the selected rows go (before that row).
inline int briefNudgeTo(const std::vector<int>& rows, int delta) {
    return delta < 0 ? std::max(0, rows.front() - 1) : std::min(int(gBriefRows.size()), rows.back() + 2);
}

// The message box's scroll: text longer than the box scrolls up in the time it shows, so its end shows when the time is
// over (`full` the text's height, `inner` the box's, `since` the ms since it appeared).
inline int briefTextScroll(int full, int inner, double since, uint32_t textFor) {
    const int rest = std::max(0, full - inner);  // all of it scrolls by in the time it shows (none when that is 0)
    return textFor ? std::clamp(int(since / textFor * rest), 0, rest) : 0;
}

// An action as the sentence the page shows: words, and values (chips) the user clicks to change.
struct BriefWordSpec {
    enum class Kind { Word, Time, Slot, Text, Sound, Unit, Modifier };
    Kind kind = Kind::Word;
    std::wstring text;
    std::wstring title;  // Time: the value pop-out's title
    std::wstring tip;
};
std::vector<BriefWordSpec> briefWords(const BriefingAction& a) {
    using K = BriefWordSpec::Kind;
    std::vector<BriefWordSpec> w;
    auto say = [&](const std::wstring& t) {
        size_t p = 0;
        while (p < t.size()) {
            const size_t q = t.find(L' ', p);
            w.push_back({K::Word, t.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p), L"", L""});
            if (q == std::wstring::npos) break;
            p = q + 1;
        }
    };
    auto secs = [](uint32_t ms) {
        wchar_t b[24];
        std::swprintf(b, 24, L"%g", ms / 1000.0);
        return std::wstring(b);
    };
    // A time: typed in seconds (decimals allowed), kept in milliseconds.
    auto time = [&](const wchar_t* title) { w.push_back({K::Time, secs(a.time), title, L""}); };
    // A slot: "slot 1", or "1" after the word "slot".
    auto slot = [&](bool word) { w.push_back({K::Slot, word ? std::to_wstring(a.slot + 1) : L"slot " + std::to_wstring(a.slot + 1), L"", L""}); };
    auto text = [&] {
        std::wstring t = stringLine(a.text);
        const size_t b = t.find_first_not_of(L' '), e = t.find_last_not_of(L' ');
        t = b == std::wstring::npos ? L"" : t.substr(b, e - b + 1);
        if (t.size() > 40) t = t.substr(0, 39) + L"\x2026";
        w.push_back({K::Text, t.empty() ? L"(text)" : t, L"", L"Edit the text under the list (with its colors)"});
    };
    auto sound = [&] {
        w.push_back({K::Sound, a.wav.empty() ? L"(a WAV file)" : widen(fs::path(a.wav).filename().string()), L"",
                     a.wav.empty() ? L"A sound of the map, or of the game (by its path)" : widen(a.wav) + L"\n" + secs(a.wavTime) + L" seconds long"});
    };
    switch (a.type) {
    case BriefingAction::Wait: say(L"Pause for"), time(L"Pause for (seconds)"), say(L"seconds."); break;
    case BriefingAction::PlayWav: say(L"Play"), sound(), say(L"for current player."); break;
    case BriefingAction::TextMessage: say(L"Display the following text for"), time(L"Display the text for (seconds)"), say(L"seconds:"), text(); break;
    case BriefingAction::Objectives: say(L"Set Mission Objectives for current player to:"), text(); break;
    case BriefingAction::ShowPortrait:
        say(L"Show portrait of");
        w.push_back({K::Unit, entryName("units", a.unit), L"", L""});
        say(L"in slot"), slot(true), say(L".");
        break;
    case BriefingAction::HidePortrait: say(L"Hide portrait in slot"), slot(true), say(L"."); break;
    case BriefingAction::SpeakingPortrait:
        say(L"Show speaking animation for"), slot(false), say(L"for"), time(L"Speaking animation for (seconds)"), say(L"seconds.");
        break;
    case BriefingAction::Transmission: {
        say(L"Show speaking animation for"), slot(false), say(L". Play"), sound(), say(L". Modify transmission duration:");
        const int m = a.modifier == BriefingAction::Add ? 1 : a.modifier == BriefingAction::Subtract ? 2 : 0;
        static const wchar_t* mods[] = {L"Set", L"Add", L"Subtract"};
        w.push_back({K::Modifier, mods[m], L"",
                     L"With Set, the transmission lasts as many seconds as the next field shows. With Add, it lasts the WAV file's length plus those seconds. With Subtract, it lasts the WAV file's length minus those seconds, but not below 0."});
        time(L"Transmission duration (seconds)"), say(L"seconds. Display the following text:"), text();
        break;
    }
    case BriefingAction::SkipTutorial: say(L"Enable Skip Tutorial button."); break;
    default: say(briefTypeName(a.type));
    }
    return w;
}
// The choices of the value chips (Slot, Modifier) and of Add an action, for the list picker.
std::vector<ListPickItem> briefSlotItems() {
    std::vector<ListPickItem> items;
    for (int i = 0; i < 4; ++i) items.push_back({L"Slot " + std::to_wstring(i + 1), L"", std::to_string(i)});
    return items;
}
std::vector<ListPickItem> briefModifierItems() { return {{L"Set", L"", "7"}, {L"Add", L"", "8"}, {L"Subtract", L"", "9"}}; }
std::vector<ListPickItem> briefTypeItems() {
    std::vector<ListPickItem> items;
    for (uint8_t t = 1; t <= 9; ++t) items.push_back({briefTypeName(t), L"", std::to_string(t)});
    return items;
}
// A time typed in seconds (decimals allowed), in milliseconds.
inline uint32_t briefMsOfSeconds(const std::string& typed) { return uint32_t(std::lround(std::strtod(typed.c_str(), nullptr) * 1000)); }

// The briefings that play a sound (the Files page's uses): one entry per briefing (the triggers with that player list),
// at its first use. Names compare without case, as the game's files do.
std::vector<FilesUse> soundUsesOf(const std::string& name) {
    std::vector<FilesUse> out;
    if (!shellState().ed || name.empty()) return out;
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        return s;
    };
    const std::string want = lower(name);
    const auto& b = briefings();
    for (size_t t = 0; t < b.size(); ++t)
        for (size_t a = 0; a < b[t].actions.size(); ++a) {
            if (lower(b[t].actions[a].wav) != want) continue;
            if (std::none_of(out.begin(), out.end(), [&](const FilesUse& u) { return b[size_t(u.trig)].players == b[t].players; }))
                out.push_back({int(t), int(a), whoName(b[t].players)});
        }
    return out;
}

// --- Portraits: a unit's portrait videos (units.dat Portrait, then portdata.dat and portdata.tbl) ------------------

// The portrait's path of a unit: "tmarine\tmafid".
std::string unitPortraitPath(int unit, bool talking) {
    static Bytes unitsDat, portDat, portTbl;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::string err;
        shellState().data.readFile("arr\\units.dat", unitsDat, err);
        shellState().data.readFile("arr\\portdata.dat", portDat, err);
        shellState().data.readFile("arr\\portdata.tbl", portTbl, err);
    }
    if (unit < 0 || unit >= 228 || unitsDat.size() < 14404 + 2 * 228 || portDat.size() < 220 * 4 || portTbl.size() < 2) return {};
    int portrait = unitsDat[14404 + 2 * size_t(unit)] | unitsDat[14405 + 2 * size_t(unit)] << 8;
    if (const DatTable* t = datTable(datTabOf("units")))
        if (const DatField* f = datFieldNamed("units", "Portrait")) portrait = int(t->value(*f, unit));  // the project's, when it changes it
    if (portrait < 0 || portrait >= 110) return {};
    const int entry = portrait + (talking ? 110 : 0);
    uint32_t file = uint32_t(portDat[4 * size_t(entry)] | portDat[4 * size_t(entry) + 1] << 8);
    if (const DatTable* t = datTable(datTabOf("portdata")))
        if (const DatField* f = datFieldNamed("portdata", "Portrait File")) file = t->value(*f, entry);  // the project's, when it changes it
    const size_t n = size_t(portTbl[0] | portTbl[1] << 8);
    if (!file || file > n) return {};
    const size_t o = size_t(portTbl[2 + 2 * (file - 1)] | portTbl[3 + 2 * (file - 1)] << 8);
    std::string path;
    for (size_t i = o; i < portTbl.size() && portTbl[i]; ++i) path += char(portTbl[i]);
    return path;
}
const PortraitVideo* unitPortrait(int unit, bool talking, int variant = 0) {
    static std::map<std::tuple<int, bool, int, bool, int>, PortraitVideo> cache;
    const AppState& s = shellState();
    auto [it, fresh] = cache.try_emplace({unit, talking, int(s.quality), s.carbot, variant});
    if (fresh) {
        const std::string path = unitPortraitPath(unit, talking);
        std::string err;
        if (path.empty() || !loadPortrait(s.data, path, variant, s.quality, s.carbot, it->second, err)) it->second = {};
    }
    return it->second.frames.empty() ? nullptr : &it->second;
}
// A unit's clips (tlk00, tlk01, ...: the game plays them one after another while the unit talks).
std::vector<const PortraitVideo*> unitPortraitClips(int unit, bool talking) {
    std::vector<const PortraitVideo*> out;
    for (int v = 0; v < 8; ++v) {
        const PortraitVideo* p = unitPortrait(unit, talking, v);
        if (!p) break;
        out.push_back(p);
    }
    return out;
}
// The picture a portrait slot shows at time t: talking, the talking clips one after another; else the first idle clip,
// again and again. False when the unit has no portrait.
bool briefPortraitFrame(const BriefingState::Portrait& p, uint32_t t, const PortraitVideo*& video, int& frame) {
    std::vector<const PortraitVideo*> clips = p.speaking ? unitPortraitClips(p.unit, true) : std::vector<const PortraitVideo*>{};
    if (clips.empty())
        if (const PortraitVideo* idle = unitPortrait(p.unit, false)) clips = {idle};
    if (clips.empty()) return false;
    const uint32_t since = t - std::min(t, p.speaking ? p.talkSince : p.since);
    size_t frames = 0;
    for (const auto* c : clips) frames += c->frames.size();
    size_t at = size_t(since * clips[0]->fps / 1000.0) % std::max<size_t>(1, frames);
    for (const auto* c : clips) {
        if (at < c->frames.size()) {
            video = c, frame = int(at);
            return true;
        }
        at -= c->frames.size();
    }
    return false;
}
