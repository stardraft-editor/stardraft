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
