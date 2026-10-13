// The Briefing workspace (included into main.cpp after data_extras.inc): the map's mission briefings (MBRF).
//  Left: which briefing: the map's briefings by whom they are for (the triggers that share a player list: players,
//  forces, all players; lists can overlap), a new one, and the players the selected one is for.
//  Middle: their actions in order, with their start times; under them, the selected action's fields. Actions are
//  selected (Ctrl, Shift), dragged, copied, cut, pasted, duplicated and moved (Alt+Up/Down) as in the trigger editor.
//  Right: the briefing window as a wireframe (objectives on the left, four portrait slots on top, the transmission
//  text in the middle), played in time with its sounds and portraits, and a timeline to seek.

struct BriefingView {
    HWND wnd = nullptr, text = nullptr, tips = nullptr;
    int tipCount = 0;
    std::vector<DataWidgetHit> hits;
    struct {
        std::function<void(POINT)> drag;
        std::function<void()> click;
        bool down = false, moved = false;
    } grab;
    std::array<bool, 27> who{};  // the briefing shown: its triggers' player list (0-7 players, 17 all, 18-21 forces)
    bool chosen = false;         // a briefing is chosen (else the first one is, when the view paints)
    int player = 0;            // whose briefing the preview plays (one it is for)
    int sel = -1;              // the focused action: index into the owner's rows (its fields show)
    std::set<int> picked;      // the selected rows
    int anchor = -1;           // where a Shift+click range starts
    int dragFrom = -1, dragTo = -1;  // the row being dragged, and the place it goes (before row dragTo)
    int textFor = -1;          // the row the text box shows
    bool textDirty = false;
    bool playing = false;
    uint32_t t = 0;            // ms
    ULONGLONG last = 0;
    int listScroll = 0;
    // The sounds playing now: by the event they belong to.
    struct Voice {
        const BriefingAction* a = nullptr;
        uint32_t at = 0;
        SDL_AudioStream* stream = nullptr;
    };
    std::vector<Voice> voices;
} gBrief;

constexpr UINT_PTR kBriefTimer = 7;

// --- Game data: portraits and the map's sounds ---------------------------------------------------------------

// The portrait of a unit (units.dat Portrait, then portdata.dat and portdata.tbl): "tmarine\tmafid".
std::string unitPortraitPath(int unit, bool talking) {
    static Bytes unitsDat, portDat, portTbl;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::string err;
        app.data.readFile("arr\\units.dat", unitsDat, err);
        app.data.readFile("arr\\portdata.dat", portDat, err);
        app.data.readFile("arr\\portdata.tbl", portTbl, err);
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
    const int quality = int(app.quality);
    auto [it, fresh] = cache.try_emplace({unit, talking, quality, app.carbot, variant});
    if (fresh) {
        const std::string path = unitPortraitPath(unit, talking);
        std::string err;
        if (path.empty() || !loadPortrait(app.data, path, variant, app.quality, app.carbot, it->second, err)) it->second = {};
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
// A frame as a GDI bitmap, drawn into `r` (kept for the last few frames).
std::pair<int, int> gPortraitDrawnSize;  // the size of the last portrait picture drawn (a self-test reads it)
void drawPortraitFrame(HDC dc, const PortraitVideo& v, int frame, RECT r) {
    gPortraitDrawnSize = {v.width, v.height};
    // The pictures by video and frame. (Not by the frame's address: a freed video's memory can hold another one, which
    // then showed the old video's pictures at the old size, GitHub feedback #86.)
    static std::map<std::pair<uint64_t, int>, HBITMAP> bitmaps;
    frame = std::clamp(frame, 0, int(v.frames.size()) - 1);
    const std::vector<uint8_t>& px = v.frames[size_t(frame)];
    if (bitmaps.size() > 3000) {  // a few videos' frames, not every video shown since the start
        for (auto& [k, b] : bitmaps)
            if (b) DeleteObject(b);
        bitmaps.clear();
    }
    auto [it, fresh] = bitmaps.try_emplace({v.serial, frame}, nullptr);
    if (fresh) {
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), v.width, -v.height, 1, 32, BI_RGB};
        void* bits = nullptr;
        it->second = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (it->second)
            for (size_t i = 0; i < size_t(v.width) * v.height; ++i) {
                uint8_t* o = static_cast<uint8_t*>(bits) + i * 4;
                o[0] = px[i * 4 + 2], o[1] = px[i * 4 + 1], o[2] = px[i * 4], o[3] = 255;
            }
    }
    if (!it->second) return;
    HDC src = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(src, it->second);
    SetStretchBltMode(dc, HALFTONE);
    StretchBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, src, 0, 0, v.width, v.height, SRCCOPY);
    SelectObject(src, old);
    DeleteDC(src);
}
// --- The briefing being edited ---------------------------------------------------------------------------------

std::vector<BriefingTrigger> gBriefData;  // the map's, as last read
uint64_t gBriefRev = ~0ull;
std::array<int, 8> playerForces() {
    std::array<int, 8> f{};
    for (int p = 0; p < 8; ++p) f[size_t(p)] = app.ed ? app.ed->player(p).force : 0;
    return f;
}
const std::vector<BriefingTrigger>& briefings() {
    if (app.ed && app.ed->revision() != gBriefRev) gBriefData = app.ed->briefings(), gBriefRev = app.ed->revision();
    return gBriefData;
}
std::vector<BriefingEvent> gBriefTimeline;
uint32_t gBriefTotal = 0;
const std::vector<BriefingEvent>& briefTimeline() {
    gBriefTimeline = briefingTimeline(briefings(), gBrief.player, playerForces(), &gBriefTotal);
    return gBriefTimeline;
}
int briefRowOf(int trig, int act);
// A change: the map's briefings with `edit` applied (one undo step).
void briefEdit(const std::function<void(std::vector<BriefingTrigger>&)>& edit) {
    if (!app.ed) return;
    std::vector<BriefingTrigger> b = briefings();
    edit(b);
    app.ed->setBriefings(b);
    InvalidateRect(gBrief.wnd, nullptr, FALSE);
    updateTitle();
}
// The briefing's rows: every action of every trigger with its player list, in order.
struct BriefRow {
    int trig, act;
};
std::vector<BriefRow> gBriefRows;
const std::vector<BriefRow>& briefRows() {
    gBriefRows.clear();
    const auto& b = briefings();
    for (size_t t = 0; t < b.size(); ++t)
        if (b[t].players == gBrief.who)
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
        std::string n = app.ed ? app.ed->force(owner - 18).name : "";
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
const Editor* gBriefShownFor = nullptr;  // the map the list was set up for
void briefSetWho(const std::array<bool, 27>& who) {
    gBriefShownFor = app.ed.get();
    gBrief.who = who, gBrief.chosen = true;
    gBrief.sel = -1, gBrief.picked.clear(), gBrief.anchor = -1, gBrief.textFor = -1, gBrief.listScroll = 0;
    // Preview a player who plays: a human or a computer it is for, else the first it is for.
    const auto ps = whoPlayers(who);
    gBrief.player = ps.empty() ? 0 : ps[0];
    for (int p : ps)
        if (app.ed && (app.ed->player(p).controller == Editor::Controller::Human || app.ed->player(p).controller == Editor::Controller::Computer)) {
            gBrief.player = p;
            break;
        }
    gBrief.t = 0;
}
// The focused action, to change: its trigger and index.
bool briefSelected(int& trig, int& act) {
    if (gBrief.sel < 0 || gBrief.sel >= int(gBriefRows.size())) return false;
    trig = gBriefRows[size_t(gBrief.sel)].trig, act = gBriefRows[size_t(gBrief.sel)].act;
    return true;
}

// The briefing's actions as one list, each with its trigger (-2: a new trigger with its player list), and back.
struct FlatAction {
    int trig;
    BriefingAction a;
};
std::vector<FlatAction> briefFlat(const std::vector<BriefingTrigger>& b) {
    std::vector<FlatAction> out;
    for (size_t t = 0; t < b.size(); ++t)
        if (b[t].players == gBrief.who)
            for (const auto& a : b[t].actions) out.push_back({int(t), a});
    return out;
}
void briefRegroup(std::vector<BriefingTrigger>& b, std::vector<FlatAction>& flat) {
    if (std::any_of(flat.begin(), flat.end(), [](const FlatAction& f) { return f.trig == -2; })) {  // the briefing's first trigger
        BriefingTrigger t;
        t.players = gBrief.who;
        b.push_back(t);
        for (auto& f : flat)
            if (f.trig == -2) f.trig = int(b.size()) - 1;
    }
    for (size_t t = 0; t < b.size(); ++t) {
        if (b[t].players != gBrief.who) continue;
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
std::vector<int> briefPickedRows() {
    std::vector<int> rows(gBrief.picked.begin(), gBrief.picked.end());
    if (rows.empty() && gBrief.sel >= 0) rows.push_back(gBrief.sel);
    std::erase_if(rows, [](int r) { return r < 0 || r >= int(gBriefRows.size()); });
    return rows;
}
void briefSelectRange(int first, int count) {
    gBrief.picked.clear();
    for (int i = 0; i < count; ++i) gBrief.picked.insert(first + i);
    gBrief.sel = count ? first : -1, gBrief.anchor = first, gBrief.textFor = -1;
}
std::vector<BriefingAction> gBriefClip;  // copied actions
void briefCopy(bool cut) {
    const auto rows = briefPickedRows();
    if (rows.empty()) return;
    gBriefClip.clear();
    for (int r : rows) gBriefClip.push_back(briefings()[size_t(gBriefRows[size_t(r)].trig)].actions[size_t(gBriefRows[size_t(r)].act)]);
    if (!cut) return setMessage(L"Copied " + std::to_wstring(rows.size()) + L" actions");
    briefEdit([&](std::vector<BriefingTrigger>& b) {
        auto flat = briefFlat(b);
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) flat.erase(flat.begin() + *it);
        briefRegroup(b, flat);
    });
    briefSelectRange(std::min(rows.front(), std::max(0, int(gBriefRows.size()) - int(rows.size()) - 1)), 0);
    setMessage(L"Cut " + std::to_wstring(rows.size()) + L" actions");
}
// Puts actions in before row `at` (the end: after the last).
void briefInsert(int at, const std::vector<BriefingAction>& acts) {
    if (acts.empty()) return;
    if (std::none_of(gBrief.who.begin(), gBrief.who.end(), [](bool v) { return v; })) return setMessage(L"First, select the players of the briefing");
    briefEdit([&](std::vector<BriefingTrigger>& b) {
        auto flat = briefFlat(b);
        at = std::clamp(at, 0, int(flat.size()));
        const int tag = briefTagAt(flat, at == int(flat.size()) ? at - 1 : at);
        std::vector<FlatAction> add;
        for (const auto& a : acts) add.push_back({tag, a});
        flat.insert(flat.begin() + at, add.begin(), add.end());
        briefRegroup(b, flat);
    });
    briefRows();
    briefSelectRange(at, int(acts.size()));
}
void briefPaste() {
    const auto rows = briefPickedRows();
    briefInsert(rows.empty() ? int(gBriefRows.size()) : rows.back() + 1, gBriefClip);
}
void briefDuplicate() {
    const auto rows = briefPickedRows();
    if (rows.empty()) return;
    std::vector<BriefingAction> acts;
    for (int r : rows) acts.push_back(briefings()[size_t(gBriefRows[size_t(r)].trig)].actions[size_t(gBriefRows[size_t(r)].act)]);
    briefInsert(rows.back() + 1, acts);
}
void briefDelete() {
    const auto rows = briefPickedRows();
    if (rows.empty()) return;
    briefEdit([&](std::vector<BriefingTrigger>& b) {
        auto flat = briefFlat(b);
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) flat.erase(flat.begin() + *it);
        briefRegroup(b, flat);
    });
    gBrief.picked.clear(), gBrief.sel = -1, gBrief.textFor = -1;
}
// Moves the selected rows so they start before row `to` (as dropped there).
void briefMoveTo(int to) {
    const auto rows = briefPickedRows();
    if (rows.empty()) return;
    int first = 0;
    briefEdit([&](std::vector<BriefingTrigger>& b) {
        auto flat = briefFlat(b);
        to = std::clamp(to, 0, int(flat.size()));
        const int tag = briefTagAt(flat, to == int(flat.size()) ? to - 1 : to);
        std::vector<FlatAction> moved;
        for (int r : rows) moved.push_back(flat[size_t(r)]), moved.back().trig = tag;
        int before = 0;
        for (int r : rows) before += r < to;
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) flat.erase(flat.begin() + *it);
        first = to - before;
        flat.insert(flat.begin() + first, moved.begin(), moved.end());
        briefRegroup(b, flat);
    });
    briefRows();
    briefSelectRange(first, int(rows.size()));
}
void briefNudge(int delta) {
    const auto rows = briefPickedRows();
    if (rows.empty()) return;
    briefMoveTo(delta < 0 ? std::max(0, rows.front() - 1) : std::min(int(gBriefRows.size()), rows.back() + 2));
}
// Edit's commands in the Briefing workspace (the text box keeps its own copy and paste).
bool briefCommand(int id) {
    if (GetFocus() == gBrief.text) {
        const UINT m = id == ID_EDIT_COPY ? WM_COPY : id == ID_EDIT_CUT ? WM_CUT : id == ID_EDIT_PASTE ? WM_PASTE : id == ID_EDIT_DELETE ? WM_CLEAR : 0;
        if (m) return SendMessageW(gBrief.text, m, 0, 0), true;
        if (id == ID_EDIT_SELECTALL) return SendMessageW(gBrief.text, EM_SETSEL, 0, -1), true;
        return false;
    }
    switch (id) {
    case ID_EDIT_COPY: briefCopy(false); return true;
    case ID_EDIT_CUT: briefCopy(true); return true;
    case ID_EDIT_PASTE: briefPaste(); return true;
    case ID_EDIT_DUPLICATE: briefDuplicate(); return true;
    case ID_EDIT_DELETE: briefDelete(); return true;
    case ID_EDIT_SELECTALL: briefSelectRange(0, int(gBriefRows.size())); InvalidateRect(gBrief.wnd, nullptr, FALSE); return true;
    }
    return false;
}
void briefEditSelected(const std::function<void(BriefingAction&)>& f) {
    int ti, ai;
    if (!briefSelected(ti, ai)) return;
    briefEdit([&](std::vector<BriefingTrigger>& b) {
        if (size_t(ti) < b.size() && size_t(ai) < b[size_t(ti)].actions.size()) f(b[size_t(ti)].actions[size_t(ai)]);
    });
}

// --- Playing: time and sounds ----------------------------------------------------------------------------------

void briefStopSounds() {
    for (auto& v : gBrief.voices)
        if (v.stream) SDL_DestroyAudioStream(v.stream);
    gBrief.voices.clear();
}
// The sounds that play at the current time, started (or kept) at their place.
void briefSyncSounds(bool restart) {
    if (restart) briefStopSounds();
    if (!gBrief.playing || !app.ed) return briefStopSounds();
    // The sounds playing now: each for as long as its file is (the action's own length is often 0 or rounded).
    std::vector<const BriefingEvent*> sounds;
    for (const BriefingEvent& e : gBriefTimeline) {
        if (!e.a || e.a->wav.empty() || (e.a->type != BriefingAction::PlayWav && e.a->type != BriefingAction::Transmission)) continue;
        uint32_t len = soundLength(e.a->wav);
        if (!len) len = e.length;
        if (gBrief.t >= e.at && gBrief.t < e.at + len) sounds.push_back(&e);
    }
    const struct {
        const std::vector<const BriefingEvent*>& sounds;
    } s{sounds};
    for (auto it = gBrief.voices.begin(); it != gBrief.voices.end();) {
        const bool still = std::any_of(s.sounds.begin(), s.sounds.end(), [&](const BriefingEvent* e) { return e->a == it->a && e->at == it->at; });
        if (!still) SDL_DestroyAudioStream(it->stream), it = gBrief.voices.erase(it);
        else ++it;
    }
    for (const BriefingEvent* e : s.sounds) {
        if (std::any_of(gBrief.voices.begin(), gBrief.voices.end(), [&](const auto& v) { return v.a == e->a && v.at == e->at; })) continue;
        const DecodedSound* dp = decodedSound(e->a->wav);
        if (!dp) continue;
        const DecodedSound& d = *dp;
        if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
        SDL_AudioStream* st = openPlayback(&d.spec);
        if (st) {
            // A sound that comes due while playing starts from its start (the timer is a few ms late at most); after a
            // seek (restart), from where the sound is at that time, in whole frames.
            const int frame = SDL_AUDIO_FRAMESIZE(d.spec);
            const uint64_t skip = restart ? uint64_t(gBrief.t - e->at) * uint64_t(d.spec.freq) / 1000 * uint64_t(frame) : 0;
            if (skip < d.pcm.size()) SDL_PutAudioStreamData(st, d.pcm.data() + skip, int(d.pcm.size() - skip));
            SDL_FlushAudioStream(st);
            SDL_ResumeAudioStreamDevice(st);
            gBrief.voices.push_back({e->a, e->at, st});
        }
    }
}
void briefPlay(bool on) {
    if (on && gBrief.t >= gBriefTotal) gBrief.t = 0;
    if (on) {  // every sound decoded before the clock starts, so none starts late
        SetCursor(LoadCursor(nullptr, IDC_WAIT));
        for (const BriefingEvent& e : gBriefTimeline)
            if (e.a && !e.a->wav.empty() && (e.a->type == BriefingAction::PlayWav || e.a->type == BriefingAction::Transmission)) decodedSound(e.a->wav);
    }
    gBrief.playing = on;
    gBrief.last = GetTickCount64();
    if (on) SetTimer(gBrief.wnd, kBriefTimer, 30, nullptr);
    else KillTimer(gBrief.wnd, kBriefTimer);
    briefSyncSounds(true);
    InvalidateRect(gBrief.wnd, nullptr, FALSE);
}
void briefSeek(uint32_t t) {
    gBrief.t = std::min(t, gBriefTotal);
    gBrief.last = GetTickCount64();
    briefSyncSounds(true);
    InvalidateRect(gBrief.wnd, nullptr, FALSE);
}

// --- Painting ----------------------------------------------------------------------------------------------------

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
// The briefing window, as a wireframe at time t: the game's 640 x 480 layout, scaled to `r` (4:3). Portrait slots
// 1 and 4 on the sides, 2 and 3 on top; the message in the middle; Objectives at the bottom left.
void paintBriefingWindow(HDC dc, RECT r) {
    const BriefingState s = briefingStateAt(gBriefTimeline, gBrief.t);
    fillAlpha(dc, r, RGB(10, 11, 13), 255);
    roundBox(dc, r, scale(6), kBorder);
    const double k = (r.right - r.left) / 640.0;
    auto at = [&](int x0, int y0, int x1, int y1) {
        return RECT{r.left + int(x0 * k), r.top + int(y0 * k), r.left + int(x1 * k), r.top + int(y1 * k)};
    };
    // The game's font: about 14 px on its 480-line screen, lines 19 px apart.
    static std::map<int, HFONT> fonts;
    const int px = std::max(scale(10), int(14 * k));
    HFONT& font = fonts[px];
    if (!font) {
        LOGFONTW lf{};
        GetObjectW(app.font, sizeof lf, &lf);
        lf.lfHeight = -px;
        font = CreateFontIndirectW(&lf);
    }
    HGDIOBJ oldFont = SelectObject(dc, font);
    struct Restore {
        HDC dc;
        HGDIOBJ f;
        ~Restore() { SelectObject(dc, f); }
    } restore{dc, oldFont};
    const COLORREF frame = RGB(84, 58, 54), talk = RGB(230, 40, 30), textColor = RGB(196, 204, 255);  // the game's frames, quieter (the slots stand back)
    auto panel = [&](RECT b, COLORREF line, int width = 1) {
        fillAlpha(dc, b, RGB(18, 19, 22), 255);
        roundBox(dc, b, std::max(2, int(4 * k)), line, width);
    };
    // Portrait slots.
    static const RECT slots[4] = {{38, 102, 157, 209}, {189, 30, 308, 137}, {340, 30, 461, 137}, {483, 102, 603, 209}};
    for (int i = 0; i < 4; ++i) {
        const RECT f = at(slots[i].left, slots[i].top, slots[i].right, slots[i].bottom);
        const auto& p = s.portraits[size_t(i)];
        const bool talking = p.shown && p.speaking;
        panel(f, talking ? talk : frame, talking ? std::max(2, int(2 * k)) : 1);
        if (p.shown) {
            // Talking: the talking clips one after another; else the first idle clip, again and again.
            std::vector<const PortraitVideo*> clips = p.speaking ? unitPortraitClips(p.unit, true) : std::vector<const PortraitVideo*>{};
            if (clips.empty())
                if (const PortraitVideo* idle = unitPortrait(p.unit, false)) clips = {idle};
            RECT in = f;
            InflateRect(&in, -std::max(2, int(3 * k)), -std::max(2, int(3 * k)));
            if (!clips.empty()) {
                const uint32_t since = gBrief.t - std::min(gBrief.t, p.speaking ? p.talkSince : p.since);
                size_t frames = 0;
                for (const auto* c : clips) frames += c->frames.size();
                size_t at = size_t(since * clips[0]->fps / 1000.0) % std::max<size_t>(1, frames);
                for (const auto* c : clips) {
                    if (at < c->frames.size()) {
                        drawPortraitFrame(dc, *c, int(at), in);
                        break;
                    }
                    at -= c->frames.size();
                }
            }
            if (talking) roundBox(dc, f, std::max(2, int(4 * k)), talk, std::max(2, int(2 * k)));
        }
        hitsOut().push_back({f, nullptr, nullptr, p.shown ? L"Slot " + std::to_wstring(i + 1) + L": " + entryName("units", p.unit) : L"Slot " + std::to_wstring(i + 1),
                             IDC_ARROW, {}});
    }
    // Objectives.
    // As high as the game's (300-463) so it shows as many lines, moved down to clear the message box.
    const RECT obj = at(33, 336, 272, 499);
    panel(obj, frame);
    const int olh = std::max(1, int(19 * k));
    RECT oh{obj.left + int(10 * k), obj.top + int(8 * k), obj.right - int(8 * k), obj.top + int(8 * k) + olh};
    SetTextColor(dc, textColor);
    DrawTextW(dc, L"Objectives", -1, &oh, DT_SINGLELINE | DT_VCENTER);
    drawGameText(dc, {obj.left + int(10 * k), oh.bottom + int(6 * k), obj.right - int(8 * k), obj.bottom}, s.objectives, RGB(220, 220, 220), olh, true);
    // The message: 6.5 lines; longer text scrolls up, so that its end shows when its time is over.
    const RECT msg = at(183, 169, 462, 308);
    panel(msg, s.textSlot >= 0 ? RGB(150, 64, 50) : frame);
    const int lh = std::max(1, int(19 * k)), inner = lh * 13 / 2;  // the game's lines: 19 px apart
    RECT tr{msg.left + int(10 * k), msg.top + std::max(0, int(msg.bottom - msg.top - inner) / 2), msg.right - int(8 * k), 0};
    tr.bottom = tr.top + inner;
    if (s.text.empty()) {
        SetTextColor(dc, RGB(120, 124, 150));  // dim: nothing shows here yet
        RECT c = msg;
        DrawTextW(dc, L"No text at this time", -1, &c, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    } else {
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, 0, 0, 0, 0);  // measure only
        const int full = drawGameText(dc, tr, s.text, textColor, lh, true);
        RestoreDC(dc, saved);
        const double since = double(gBrief.t - std::min(gBrief.t, s.textSince));
        const int rest = std::max(0, full - inner);  // all of it scrolls by in the time it shows (none when that is 0)
        const int off = s.textFor ? std::clamp(int(since / s.textFor * rest), 0, rest) : 0;
        const int saved2 = SaveDC(dc);
        IntersectClipRect(dc, tr.left, tr.top, tr.right, tr.bottom);
        drawGameText(dc, {tr.left, tr.top - off, tr.right, tr.bottom}, s.text, textColor, lh, true);
        RestoreDC(dc, saved2);
    }
}

// The timeline: a row per kind of action, a block per action (its length), the playhead; click or drag to seek.
int paintBriefingTimeline(HDC dc, RECT r) {
    const uint32_t total = std::max<uint32_t>(gBriefTotal, 1000);
    fillAlpha(dc, r, RGB(14, 14, 16), 255);
    roundBox(dc, r, scale(4), kBorder);
    const int x0 = r.left + scale(8), x1 = r.right - scale(8);
    auto xAt = [&](uint32_t t) { return x0 + int(int64_t(t) * (x1 - x0) / total); };
    const int laneH = scale(10), ly = r.top + scale(8);
    for (size_t i = 0; i < gBriefTimeline.size(); ++i) {
        const BriefingEvent& e = gBriefTimeline[i];
        const BriefingAction& a = *e.a;
        const int lane = a.type == BriefingAction::Transmission || a.type == BriefingAction::PlayWav ? 0 : a.type == BriefingAction::Wait ? 1 : 2;
        const COLORREF c = lane == 0 ? kAccent : lane == 1 ? RGB(90, 94, 100) : kGood;
        RECT b{xAt(e.at), ly + lane * (laneH + scale(3)), std::max(xAt(e.at) + scale(3), xAt(e.at + e.length)), ly + lane * (laneH + scale(3)) + laneH};
        fillAlpha(dc, b, c, gBrief.picked.count(briefRowOf(e.trigger, e.action)) ? 255 : 130);
    }
    // Seconds.
    const int ty = ly + 3 * (laneH + scale(3)) + scale(2);
    const uint32_t step = total > 120000 ? 30000 : total > 40000 ? 10000 : total > 10000 ? 5000 : 1000;
    SetTextColor(dc, kDim);
    for (uint32_t t = 0; t <= total; t += step) {
        line(dc, xAt(t), ty, xAt(t), ty + scale(4), RGB(70, 74, 80));
        RECT tr{xAt(t) - scale(30), ty + scale(4), xAt(t) + scale(30), ty + scale(20)};
        DrawTextW(dc, briefTime(t).c_str(), -1, &tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    const int px = xAt(gBrief.t);
    line(dc, px, r.top + scale(3), px, r.bottom - scale(3), RGB(240, 240, 240), 2);
    gBrief.hits.push_back({r, [x0, x1, total](POINT pt) { briefSeek(uint32_t(std::clamp<int64_t>(int64_t(pt.x - x0) * total / std::max(1, x1 - x0), 0, total))); },
                           nullptr, L"Click or drag to go to a time", IDC_HAND, {}});
    return r.bottom - r.top;
}

void briefCommitText();
// A word of an action's sentence; a value (a chip) has a picker, which opens at `at` (screen).
struct BriefWord {
    std::wstring text;
    std::function<void(POINT)> pick;
    const UnitPalette::Bitmap* icon = nullptr;
    std::wstring tip;
};
std::vector<int> gBriefRowTops;  // the shown rows' tops (dragging finds the place from these)
bool gBriefAllShown = true;       // the last row is in view
bool gBriefFollowSel = false;     // the selected row moved by a key: keep it in view
void briefPreviewText();
int gBriefListEnd = 0;
bool briefAskMs(const std::wstring& title, uint32_t cur, POINT at, uint32_t& out);  // the value pop-out (main.cpp)

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

void briefClickRow(int i, uint32_t at) {  // at: where the preview goes (UINT32_MAX: it stays)
    briefCommitText();
    const bool chip = at == UINT32_MAX;
    if (!chip && GetKeyState(VK_SHIFT) < 0 && gBrief.anchor >= 0) {
        gBrief.picked.clear();
        for (int k = std::min(gBrief.anchor, i); k <= std::max(gBrief.anchor, i); ++k) gBrief.picked.insert(k);
    } else if (!chip && GetKeyState(VK_CONTROL) < 0) {
        if (!gBrief.picked.erase(i)) gBrief.picked.insert(i);
        gBrief.anchor = i;
    } else if (!gBrief.picked.count(i) || !chip) {
        gBrief.picked = {i}, gBrief.anchor = i;
    }
    if (gBrief.sel != i) gBrief.textFor = -1;
    gBrief.sel = i;
    if (!chip && !gBrief.playing) briefSeek(at);
    InvalidateRect(gBrief.wnd, nullptr, FALSE);
}

std::vector<BriefWord> briefSentence(const BriefingAction& a) {
    std::vector<BriefWord> w;
    auto say = [&](const std::wstring& t) {
        size_t p = 0;
        while (p < t.size()) {
            const size_t q = t.find(L' ', p);
            w.push_back({t.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p)});
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
    auto time = [&](const wchar_t* title) {
        w.push_back({secs(a.time), [title, cur = a.time](POINT at) {
                         uint32_t v;
                         if (briefAskMs(title, cur, at, v)) briefEditSelected([&](BriefingAction& x) { x.time = v; });
                     }});
    };
    // A slot: "slot 1", or "1" after the word "slot".
    auto slot = [&](bool word) {
        w.push_back({word ? std::to_wstring(a.slot + 1) : L"slot " + std::to_wstring(a.slot + 1), [cur = a.slot](POINT at) {
                         std::vector<ListPickItem> items;
                         for (int i = 0; i < 4; ++i) items.push_back({L"Slot " + std::to_wstring(i + 1), L"", std::to_string(i)});
                         std::string picked;
                         if (showListPicker(L"Slot", items, std::to_string(cur), at, picked)) briefEditSelected([&](BriefingAction& x) { x.slot = std::stoi(picked); });
                     }});
    };
    auto text = [&] {
        std::wstring t = stringLine(a.text);
        const size_t b = t.find_first_not_of(L' '), e = t.find_last_not_of(L' ');
        t = b == std::wstring::npos ? L"" : t.substr(b, e - b + 1);
        if (t.size() > 40) t = t.substr(0, 39) + L"\x2026";
        w.push_back({t.empty() ? L"(text)" : t, [](POINT) {
                         InvalidateRect(gBrief.wnd, nullptr, FALSE);
                         UpdateWindow(gBrief.wnd);  // the text box shows for this row, then gets the caret
                         SetFocus(gBrief.text);
                         SendMessageW(gBrief.text, EM_SETSEL, 0, -1);
                     },
                     nullptr, L"Edit the text under the list (with its colors)"});
    };
    auto sound = [&] {
        w.push_back({a.wav.empty() ? L"(a WAV file)" : widen(fs::path(a.wav).filename().string()), [cur = a.wav](POINT at) {
                         std::string picked;
                         if (!pickSound(at, cur, picked)) return;
                         const uint32_t len = soundLength(picked);
                         briefEditSelected([&](BriefingAction& x) { x.wav = picked, x.wavTime = len; });
                     },
                     nullptr, a.wav.empty() ? L"A sound of the map, or of the game (by its path)" : widen(a.wav) + L"\n" + secs(a.wavTime) + L" seconds long"});
    };
    switch (a.type) {
    case BriefingAction::Wait: say(L"Pause for"), time(L"Pause for (seconds)"), say(L"seconds."); break;
    case BriefingAction::PlayWav: say(L"Play"), sound(), say(L"for current player."); break;
    case BriefingAction::TextMessage: say(L"Display the following text for"), time(L"Display the text for (seconds)"), say(L"seconds:"), text(); break;
    case BriefingAction::Objectives: say(L"Set Mission Objectives for current player to:"), text(); break;
    case BriefingAction::ShowPortrait:
        say(L"Show portrait of");
        w.push_back({entryName("units", a.unit), [cur = a.unit](POINT at) {
                         std::string picked;
                         if (showListPicker(L"Search unit types", entryItems("units"), std::to_string(cur), at, picked))
                             briefEditSelected([&](BriefingAction& x) { x.unit = uint16_t(std::stoi(picked)); });
                     },
                     datRefIcon("units", a.unit)});
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
        w.push_back({mods[m], [](POINT at) {
                         std::vector<ListPickItem> items{{L"Set", L"", "7"}, {L"Add", L"", "8"}, {L"Subtract", L"", "9"}};
                         std::string picked;
                         if (showListPicker(L"Transmission duration", items, "", at, picked)) briefEditSelected([&](BriefingAction& x) { x.modifier = uint8_t(std::stoi(picked)); });
                     },
                     nullptr, L"Set: the duration is the time. Add, Subtract: the WAV file's length plus or minus the time."});
        time(L"Transmission duration (seconds)"), say(L"seconds. Display the following text:"), text();
        break;
    }
    case BriefingAction::SkipTutorial: say(L"Enable Skip Tutorial button."); break;
    default: say(briefTypeName(a.type));
    }
    return w;
}

int briefRowOf(int trig, int act) {
    for (size_t i = 0; i < gBriefRows.size(); ++i)
        if (gBriefRows[i].trig == trig && gBriefRows[i].act == act) return int(i);
    return -1;
}

// The selected action's text (when it has one): an edit box with the color code buttons. Its other values are
// chips in its row.
int paintBriefingFields(HDC dc, const FormLayout& l, int y) {
    int ti, ai;
    if (!briefSelected(ti, ai)) {
        if (gBrief.text) ShowWindow(gBrief.text, SW_HIDE);
        return y;
    }
    const BriefingAction& a = briefings()[size_t(ti)].actions[size_t(ai)];
    // Text: an edit box (the control bytes as <XX>), with the color code buttons under it.
    const bool hasText = a.type == BriefingAction::TextMessage || a.type == BriefingAction::Objectives || a.type == BriefingAction::Transmission;
    if (hasText) {
        const int h = scale(110);
        RECT frame{l.x0, y + scale(4), l.colRight, y + scale(4) + h};
        ui::fieldBox(dc, frame, GetFocus() == gBrief.text ? ui::Field::Focus : ui::Field::Normal);
        if (gBrief.textFor != gBrief.sel) {
            gBrief.textFor = gBrief.sel;
            std::wstring w = widen(StatTxt::escape(a.text)), o;
            for (wchar_t c : w) c == L'\n' ? o += L"\r\n" : o += c;
            SetWindowTextW(gBrief.text, o.c_str());
            gBrief.textDirty = false;
        }
        MoveWindow(gBrief.text, frame.left + scale(8), frame.top + scale(6), frame.right - frame.left - scale(16), h - scale(12), TRUE);
        ShowWindow(gBrief.text, SW_SHOWNA);
        y = colorCodeButtons(
            dc, l, frame.bottom + scale(6), gBrief.text,
            [] {
                gBrief.textDirty = true;
                briefPreviewText();
            },
            true);
    } else if (gBrief.text) {
        ShowWindow(gBrief.text, SW_HIDE);
    }
    return y;
}

double gBriefPaintParts[4] = {};  // perf: the last paint's parts (ms): start, list, preview, tooltips
void paintBriefing(HWND hwnd) {
    const auto pt0 = std::chrono::steady_clock::now();
    auto partDone = [last = pt0](int k) mutable {
        const auto now = std::chrono::steady_clock::now();
        gBriefPaintParts[k] = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
    };
    for (int i = 0; gBrief.tips && i < gBrief.tipCount; ++i) {
        TOOLINFOW ti{sizeof ti};
        ti.hwnd = hwnd, ti.uId = UINT_PTR(i + 1);
        SendMessageW(gBrief.tips, TTM_DELTOOLW, 0, LPARAM(&ti));
    }
    gBrief.tipCount = 0;
    gBrief.hits.clear();
    gHitsOut = &gBrief.hits, gHitsWnd = hwnd;
    gHitsOf[hwnd] = &gBrief.hits;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    // A map opened (or changed) since the last look: its first briefing (none: a new one for player 1).
    if (app.ed.get() != gBriefShownFor || !gBrief.chosen) {
        const auto list = briefList();
        std::array<bool, 27> who{};
        who[0] = true;
        briefSetWho(list.empty() ? who : list.front());
    }
    const auto& tl = briefTimeline();
    const auto& rows = briefRows();
    if (gBrief.sel >= int(rows.size())) gBrief.sel = int(rows.size()) - 1;
    std::erase_if(gBrief.picked, [&](int r) { return r >= int(rows.size()); });
    const int listW = scale(220), rowH = scale(26);
    // Left: the briefing (a dropdown of the map's briefings), a new one, and whom it is for.
    {
        RECT side{0, 0, listW, rc.bottom};
        ui::sidePanel(mem, side);
        fillAlpha(mem, {listW - 1, 0, listW, rc.bottom}, kBorder, 255);
        const int x = scale(14), w = listW - scale(28);
        int y = scale(12);
        y = ui::sideGroup(mem, x, y, w, L"Briefing", true) + scale(4);
        const int n = briefActionCount(gBrief.who);
        ui::pickBox(mem, {x, y, x + w, y + scale(28)}, whoName(gBrief.who), false,
                    [](RECT r) {
                        std::vector<ListPickItem> items;
                        const auto list = briefList();
                        for (size_t i = 0; i < list.size(); ++i)
                            items.push_back({whoName(list[i]) + L"   " + std::to_wstring(briefActionCount(list[i])) + L" actions", L"", std::to_string(i)});
                        std::string picked;
                        if (items.empty() || !showListPicker(L"Briefing", items, "", belowBox(r), picked)) return;
                        briefCommitText();
                        briefSetWho(list[size_t(std::stoi(picked))]);
                        briefPlay(false);
                    },
                    n ? std::to_wstring(n) + L" actions" : L"None");
        y += scale(34);
        const int ax = ui::smallButton(mem, x, y, scale(24), L"New briefing", [] {
            // For the first player who has no briefing of their own (else nobody yet: choose below).
            std::array<bool, 27> who{};
            const auto list = briefList();
            for (int p = 0; p < 8; ++p) {
                std::array<bool, 27> one{};
                one[size_t(p)] = true;
                if (std::find(list.begin(), list.end(), one) == list.end()) {
                    who = one;
                    break;
                }
            }
            briefCommitText();
            briefSetWho(who);
            briefPlay(false);
        }, L"A briefing for other players: its own actions");
        if (n)
            ui::smallButton(mem, ax + scale(8), y, scale(24), L"Delete", [] {
                if (themedMessageBox(app.main, (L"Delete the briefing for " + whoName(gBrief.who) + L"?").c_str(), L"Delete briefing", MB_OKCANCEL | MB_ICONWARNING, {L"&Delete", L"Cancel"}) != IDOK) return;
                const auto who = gBrief.who;
                briefEdit([&](std::vector<BriefingTrigger>& b) { std::erase_if(b, [&](const BriefingTrigger& t) { return t.players == who; }); });
                const auto list = briefList();
                std::array<bool, 27> p1{};
                p1[0] = true;
                briefSetWho(list.empty() ? p1 : list.front());
            }, L"Delete it and its actions", ui::Tone::Danger);
        y += scale(36);
        // Whom it is for: a check box for each; the briefing's triggers change with them.
        y = ui::sideGroup(mem, x, y, w, L"For", true) + scale(4);
        auto toggle = [](int k) {
            return [k] {
                auto now = gBrief.who;
                now[size_t(k)] = !now[size_t(k)];
                const auto before = gBrief.who;
                const auto list = briefList();
                if (std::find(list.begin(), list.end(), now) != list.end() && briefActionCount(before))
                    setMessage(L"A briefing for these players is there already: the two are now one");
                if (briefActionCount(before))
                    briefEdit([&](std::vector<BriefingTrigger>& b) {
                        for (auto& t : b)
                            if (t.players == before) t.players = now;
                    });
                const int sel = gBrief.sel;
                briefSetWho(now);
                gBrief.sel = sel;
                InvalidateRect(gBrief.wnd, nullptr, FALSE);
            };
        };
        const int rh = scale(26);
        for (int o : {17, 18, 19, 20, 21}) {
            ui::check(mem, {x, y, x + w, y + rh}, ownerName(o), gBrief.who[size_t(o)], toggle(o), o == 17 ? L"Every player" : L"The players of this force");
            y += rh;
        }
        y += scale(6);
        for (int p = 0; p < 8; ++p) {
            const uint32_t rgb = playerRgba(p, 0) >> 8;
            fillAlpha(mem, {x + w - scale(10), y + scale(9), x + w, y + rh - scale(9)}, RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255), 255);
            ui::check(mem, {x, y, x + w - scale(16), y + rh}, ownerName(p), gBrief.who[size_t(p)], toggle(p));
            y += rh;
        }
    }
    // Middle: the actions, then the selected one's fields.
    // The preview takes what the actions do not need (they keep 440 px), as tall as the window lets it be (with the
    // player box, the transport and the timeline under it).
    const int byHeight = (int(rc.bottom) - scale(12 + 34 + 10 + 32 + 8 + 64 + 12)) * 640 / 520;
    const int previewW = std::max(scale(420), std::min({int(rc.right) - listW - scale(440), byHeight, scale(1280)}));
    RECT midRc{listW, 0, int(rc.right) - previewW - scale(24), rc.bottom};
    FormLayout l = formLayout({0, 0, midRc.right - midRc.left, rc.bottom});
    l.x0 = listW + scale(18), l.colRight = midRc.right - scale(10), l.colW = l.colRight - l.x0, l.labelW = std::min(scale(150), l.colW / 3);
    int y = scale(14);
    // The selected action's text box sits at the bottom; the list takes the height above it.
    int selTrig, selAct;
    bool selText = false;
    if (briefSelected(selTrig, selAct)) {
        const uint8_t ty = briefings()[size_t(selTrig)].actions[size_t(selAct)].type;
        selText = ty == BriefingAction::TextMessage || ty == BriefingAction::Objectives || ty == BriefingAction::Transmission;
    }
    const int textTop = int(rc.bottom) - (selText ? scale(170) : 0);
    const int listBottom = textTop - rowH - scale(14);  // room for "+ action"
    // Each row's start time, from the preview's timeline.
    std::map<std::pair<int, int>, uint32_t> startOf;
    for (const auto& e : tl) startOf[{e.trigger, e.action}] = e.at;
    if (gBrief.sel >= 0 && gBrief.sel < gBrief.listScroll) gBrief.listScroll = gBrief.sel;
    const int first = std::clamp(gBrief.listScroll, 0, std::max(0, int(rows.size()) - 1));
    gBrief.listScroll = first;
    int lastShown = first - 1;
    const int gutter = scale(58);  // the start times, left of the blocks
    gBriefRowTops.clear();
    for (int i = first; i < int(rows.size()) && (i == first || y + scale(40) <= listBottom); ++i) {
        lastShown = i;
        const BriefRow& row = rows[size_t(i)];
        const BriefingAction& a = briefings()[size_t(row.trig)].actions[size_t(row.act)];
        if (i > first && rows[size_t(i - 1)].trig != row.trig) {  // a second trigger of the owner's: a line between
            line(mem, l.x0 + gutter, y + scale(1), l.colRight, y + scale(1), kBorder);
            y += scale(6);
        }
        gBriefRowTops.push_back(y);
        const bool on = gBrief.picked.count(i) > 0 || i == gBrief.sel, dim = a.disabled();
        const auto st = startOf.find({row.trig, row.act});
        const uint32_t at = st != startOf.end() ? st->second : 0;
        const bool playingNow = gBrief.playing && st != startOf.end() && gBrief.t >= at &&
                                (i + 1 >= int(rows.size()) || startOf[{rows[size_t(i + 1)].trig, rows[size_t(i + 1)].act}] > gBrief.t);
        // The sentence, laid out in the block (it wraps), then the block under it.
        const int bx = l.x0 + gutter, bw = l.colRight - bx, textX = bx + scale(26), textW = bw - scale(26) - scale(100);
        const int lineH = scale(22), padY = scale(9);
        const auto words = briefSentence(a);
        struct Placed {
            RECT r;
            size_t w;
        };
        std::vector<Placed> placed;
        int x = textX, ly = y + padY;
        for (size_t k = 0; k < words.size(); ++k) {
            const BriefWord& w = words[k];
            SIZE sz{};
            GetTextExtentPoint32W(mem, w.text.c_str(), int(w.text.size()), &sz);
            const int iconW = w.icon ? scale(20) : 0, padX = w.pick ? scale(5) : 0, ww = sz.cx + 2 * padX + iconW;
            if (!w.pick && !w.text.empty() && (w.text[0] == L'.' || w.text[0] == L':') && x > textX) x -= scale(5);
            if (x > textX && x + ww > textX + textW) x = textX, ly += lineH;
            placed.push_back({{x, ly, x + ww, ly + lineH}, k});
            x += ww + (w.pick ? scale(2) : 0) + scale(4);
        }
        RECT head{bx, y, bx + bw, ly + lineH + padY};
        const COLORREF blue = kAccent;
        {
            HBRUSH b = CreateSolidBrush(on ? kSel : kCtl);
            HPEN pn = CreatePen(PS_SOLID, 1, on ? blue : kBorder);
            HGDIOBJ obr = SelectObject(mem, b), op = SelectObject(mem, pn);
            RoundRect(mem, head.left, head.top, head.right, head.bottom, scale(8), scale(8));
            SelectObject(mem, obr), SelectObject(mem, op);
            DeleteObject(b), DeleteObject(pn);
        }
        if (!dim) fillAlpha(mem, {head.left + scale(2), head.top + scale(5), head.left + scale(4), head.bottom - scale(5)}, briefTypeColor(a.type), 255);
        if (playingNow) fillAlpha(mem, {head.left - scale(4), head.top + scale(5), head.left - scale(2), head.bottom - scale(5)}, kGood, 255);
        const bool hot = mouseIn(head);
        if ((on || hot) && gBrief.dragFrom < 0) ui::grip(mem, head.left + scale(7), head.top + padY + lineH / 2 - scale(6), on);  // the drag handle
        // The start time.
        SelectObject(mem, monoFont());
        SetTextColor(mem, kDim);
        RECT tt{l.x0, head.top + padY, bx - scale(6), head.top + padY + lineH};
        DrawTextW(mem, st != startOf.end() ? briefTime(at).c_str() : L"", -1, &tt, DT_SINGLELINE | DT_VCENTER);
        SelectObject(mem, app.font);
        // Click: select (Ctrl adds, Shift a range); drag: move the selected rows.
        gBrief.hits.push_back({head,
                               [first](POINT pt) {
                                   if (gBrief.dragFrom < 0) gBrief.dragFrom = gBrief.sel;
                                   int to = first + int(gBriefRowTops.size());
                                   for (size_t k = 0; k < gBriefRowTops.size(); ++k) {
                                       const int next = k + 1 < gBriefRowTops.size() ? gBriefRowTops[k + 1] : gBriefListEnd;
                                       if (pt.y < (gBriefRowTops[k] + next) / 2) {
                                           to = first + int(k);
                                           break;
                                       }
                                   }
                                   gBrief.dragTo = std::clamp(to, 0, int(gBriefRows.size()));
                                   InvalidateRect(gBrief.wnd, nullptr, FALSE);
                               },
                               [i, at] { briefClickRow(i, at); }, L"", IDC_HAND, {}});
        // The words: values are chips; a click selects the row and opens the value's picker.
        for (const Placed& pl : placed) {
            const BriefWord& w = words[pl.w];
            RECT r = pl.r;
            const int padX = w.pick ? scale(5) : 0;
            if (w.pick) {
                const bool chipHot = mouseIn(r);
                ui::chipFace(mem, {r.left, r.top + scale(2), r.right, r.bottom - scale(2)}, chipHot, dim);
                gBrief.hits.push_back({r, nullptr, [i, r, pick = w.pick] {
                                           briefClickRow(i, UINT32_MAX);
                                           POINT pt{r.left, r.bottom + scale(4)};
                                           ClientToScreen(gBrief.wnd, &pt);
                                           pick(pt);
                                       },
                                       w.tip, IDC_HAND, {}});
            }
            if (w.icon && w.icon->bmp) drawFitted(mem, *w.icon, {r.left + padX - scale(1), r.top + scale(3), r.left + padX + scale(17), r.bottom - scale(3)});
            SetTextColor(mem, dim ? kFaint : w.pick ? blue : kText);
            RECT tr{r.left + padX + (w.icon ? scale(20) : 0), r.top, r.right - padX, r.bottom};
            DrawTextW(mem, w.text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        // On the row under the mouse, or selected: on/off, duplicate, delete.
        if ((on || hot) && gBrief.dragFrom < 0) {
            auto act = [i](int k) {
                return [i, k] {
                    briefClickRow(i, UINT32_MAX);
                    if (k == 0) briefEditSelected([](BriefingAction& x) { x.flags ^= 2; });
                    else if (k == 1) briefDuplicate();
                    else briefDelete();
                };
            };
            const RECT line{head.left, head.top + padY - scale(2), head.right - scale(4), head.top + padY + scale(24)};
            ui::rowActions(mem, line,
                           {{L"\xE7E8", dim ? L"Turn on" : L"Turn off", act(0), ui::Tone::Quiet, dim},
                            {UiIcon::Duplicate, L"Duplicate (Ctrl+D)", act(1)},
                            {UiIcon::Delete, L"Delete (Del)", act(2), ui::Tone::Danger}},
                           true);
        }
        y = head.bottom + scale(5);
    }
    gBriefListEnd = y;
    if (gBrief.dragFrom >= 0 && gBrief.dragTo >= 0) {  // where the dragged rows go
        const int k = gBrief.dragTo - first;
        const int ly = k >= 0 && k < int(gBriefRowTops.size()) ? gBriefRowTops[size_t(k)] : y;
        ui::dropLine(mem, l.x0 + gutter, l.colRight, ly - scale(3));
    }
    gBriefAllShown = lastShown >= int(rows.size()) - 1;  // the wheel scrolls no further
    if (gBrief.sel > lastShown && gBrief.sel < int(rows.size()) && gBriefFollowSel) {  // the selected row below: scroll to it
        gBrief.listScroll += gBrief.sel - lastShown;
        InvalidateRect(gBrief.wnd, nullptr, FALSE);
    }
    gBriefFollowSel = false;
    // The scroll bar, when not all rows show: drag the thumb or click the track.
    if (!(first == 0 && gBriefAllShown) && !rows.empty()) {
        const int n = int(rows.size()), shown = std::max(1, lastShown - first + 1);
        const int top = scale(14), bottom = std::max(top + scale(40), listBottom), tx = l.colRight + scale(3);
        const int trackH = bottom - top, thumbH = std::max(scale(28), trackH * shown / n);
        const int span = std::max(1, n - shown);
        const int thumbTop = top + (trackH - thumbH) * std::min(first, span) / span;
        RECT track{tx, top, tx + scale(6), bottom};
        RECT thumb{tx, thumbTop, tx + scale(6), thumbTop + thumbH};
        ui::scrollFace(mem, track, thumb, mouseIn(track) || gBrief.grab.down);
        RECT hit{tx - scale(4), top, tx + scale(10), bottom};
        gBrief.hits.push_back({hit, [top, trackH, thumbH, span](POINT pt) {
                                   const int room = std::max(1, trackH - thumbH);
                                   gBrief.listScroll = std::clamp(int(std::lround(double(pt.y - top - thumbH / 2) * span / room)), 0, span);
                                   InvalidateRect(gBrief.wnd, nullptr, FALSE);
                               },
                               nullptr, L"", IDC_ARROW, {}});
    }
    if (rows.empty()) {  // no actions: what a briefing does, above the button
        const wchar_t* text = L"This briefing has no actions. Add actions to show text, portraits and sounds before the game starts.";
        RECT note{l.x0 + gutter, y + scale(4), l.colRight, y + scale(4)};
        DrawTextW(mem, text, -1, &note, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);  // as tall as the text
        note.right = l.colRight;
        SetTextColor(mem, kDim);
        DrawTextW(mem, text, -1, &note, DT_WORDBREAK | DT_NOPREFIX);
        y = note.bottom + scale(6);
    }
    // Add an action: after the selected ones, or at the end.
    ui::addRow(mem, {l.x0 + gutter, y, l.colRight, y + scale(30)}, L"Add an action", [y] {
        std::vector<ListPickItem> items;
        for (uint8_t t = 1; t <= 9; ++t) items.push_back({briefTypeName(t), L"", std::to_string(t)});
        std::string picked;
        RECT anchor{scale(240), y, scale(440), y + scale(26)};
        if (!showListPicker(L"Action", items, "", belowBox(anchor), picked)) return;
        BriefingAction a;
        a.type = uint8_t(std::stoi(picked));
        if (a.type == BriefingAction::Wait) a.time = 1000;
        if (a.type == BriefingAction::TextMessage) a.time = 5000;
        const auto sel = briefPickedRows();
        briefInsert(sel.empty() ? int(gBriefRows.size()) : sel.back() + 1, {a});
    });
    paintBriefingFields(mem, l, textTop);
    // Right: the briefing window (as a player of the owner's sees it), its transport and timeline.
    {
        const int px = int(rc.right) - previewW - scale(14);
        int top = scale(12);
        const auto players = whoPlayers(gBrief.who);
        if (players.size() > 1) {  // a force or all players: whose briefing to play
            pickBox(mem, {px, top + scale(2), px + scale(200), top + rowH - scale(2)}, ownerName(gBrief.player), false, [players](RECT b) {
                std::vector<ListPickItem> items;
                for (int p : players) items.push_back({ownerName(p), L"", std::to_string(p)});
                std::string picked;
                if (showListPicker(L"Preview as", items, std::to_string(gBrief.player), belowBox(b), picked)) gBrief.player = std::stoi(picked), briefSeek(0);
            }, L"Preview as this player: their own briefing, the group's and all players'");
            top += rowH + scale(6);
        }
        RECT pv{px, top, px + previewW, top + previewW * 520 / 640};  // the 640 x 480 screen, and a margin under it
        partDone(1);
        paintBriefingWindow(mem, pv);
        partDone(2);
        int ty = pv.bottom + scale(10);
        const int bs = scale(32);
        glyphButton(mem, {px, ty, px + bs, ty + bs}, UiIcon::ToStart, false, [] { briefSeek(0); }, L"From the start");
        glyphButton(mem, {px + bs + scale(6), ty, px + 2 * bs + scale(6), ty + bs}, gBrief.playing ? UiIcon::Pause : UiIcon::Play, false, [] { briefPlay(!gBrief.playing); },
                    gBrief.playing ? L"Pause (Space)" : L"Play (Space)");
        RECT tt{px + 2 * bs + scale(18), ty, px + previewW, ty + bs};
        SetTextColor(mem, kDim);
        DrawTextW(mem, (briefTime(gBrief.t) + L" / " + briefTime(gBriefTotal)).c_str(), -1, &tt, DT_SINGLELINE | DT_VCENTER);
        ty += bs + scale(8);
        paintBriefingTimeline(mem, {px, ty, px + previewW, ty + scale(64)});
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    gHitsOut = nullptr, gHitsWnd = nullptr;
    // Tooltips for this paint's parts.
    if (!gBrief.tips) {
        gBrief.tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, hwnd, nullptr, app.inst, nullptr);
        SendMessageW(gBrief.tips, TTM_SETMAXTIPWIDTH, 0, scale(360));
        styleTips(gBrief.tips);
    }
    for (auto& h : gBrief.hits) {
        if (h.tip.empty()) continue;
        TOOLINFOW ti{sizeof ti};
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = hwnd, ti.uId = UINT_PTR(++gBrief.tipCount), ti.rect = h.r;
        ti.lpszText = h.tip.data();
        SendMessageW(gBrief.tips, TTM_ADDTOOLW, 0, LPARAM(&ti));
    }
    partDone(3);
}

// The text box's text, as the action keeps it (with the string's own line ends).
std::string briefBoxText(const std::string& before) {
    std::string text = StatTxt::unescape(narrow(windowText(gBrief.text)));
    if (before.find("\r\n") != std::string::npos) {
        std::string crlf;
        for (char c : text) c == '\n' ? crlf += "\r\n" : crlf += c;
        text = crlf;
    }
    return text;
}
void briefCommitText() {
    if (!gBrief.text || !gBrief.textDirty) return;
    gBrief.textDirty = false;
    briefEditSelected([&](BriefingAction& x) { x.text = briefBoxText(x.text); });
}
// While typing: the preview shows the text (in the shown copy only; it becomes one undo step when the box
// loses the focus).
void briefPreviewText() {
    int ti, ai;
    if (!gBrief.textDirty || !briefSelected(ti, ai)) return;
    BriefingAction& x = gBriefData[size_t(ti)].actions[size_t(ai)];
    const std::string text = briefBoxText(x.text);
    if (text == x.text) return;
    x.text = text;
    InvalidateRect(gBrief.wnd, nullptr, FALSE);
}
LRESULT CALLBACK briefTextProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KILLFOCUS) SendMessageW(hwnd, EM_GETSEL, WPARAM(&gStringsSelStart), LPARAM(&gStringsSelEnd)), briefCommitText();
    if (msg == WM_KEYUP) {
        const LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        briefPreviewText();
        return r;
    }
    if (msg == WM_CHAR && wp == 1) return SendMessageW(hwnd, EM_SETSEL, 0, -1), 0;  // Ctrl+A
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK briefWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: paintBriefing(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_TIMER:
        if (wp == kBriefTimer && gBrief.playing) {
            const ULONGLONG now = GetTickCount64();
            // At most 100 ms a tick: when the program stops for a moment, the briefing waits rather than jumps.
            gBrief.t += uint32_t(std::min<ULONGLONG>(now - gBrief.last, 100)), gBrief.last = now;
            if (gBrief.t >= gBriefTotal) gBrief.t = gBriefTotal, briefPlay(false);
            else briefSyncSounds(false);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_COMMAND:
        if (HWND(lp) == gBrief.text && HIWORD(wp) == EN_CHANGE && GetFocus() == gBrief.text) gBrief.textDirty = true;
        return 0;
    case WM_MOUSEWHEEL:
        if (GET_WHEEL_DELTA_WPARAM(wp) < 0 && gBriefAllShown) return 0;
        gBrief.listScroll = std::max(0, gBrief.listScroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 2);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (gBrief.grab.down && gBrief.grab.drag) {
            if (!gBrief.grab.moved && gBrief.grab.click) {  // a row: select it first, then drag what is selected
                gBrief.grab.click();
            }
            gBrief.grab.moved = true, gBrief.grab.drag(pt);
            return 0;
        }
        static RECT over{};
        RECT part{};
        for (auto it = gBrief.hits.rbegin(); it != gBrief.hits.rend(); ++it)
            if (PtInRect(&it->r, pt)) {
                part = it->r;
                break;
            }
        if (!EqualRect(&part, &over)) {
            over = part, InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        SetFocus(hwnd);
        for (auto it = gBrief.hits.rbegin(); it != gBrief.hits.rend(); ++it)
            if (PtInRect(&it->r, pt) && (it->drag || it->click)) {
                gBrief.grab = {it->drag, it->click, true, false};
                gBrief.dragFrom = gBrief.dragTo = -1;
                SetCapture(hwnd);
                if (gBrief.grab.drag && !gBrief.grab.click) gBrief.grab.moved = true, gBrief.grab.drag(pt);
                return 0;
            }
        return 0;
    }
    case WM_LBUTTONUP:
        if (gBrief.grab.down) {
            ReleaseCapture();
            auto click = gBrief.grab.click;
            const bool moved = gBrief.grab.moved;
            gBrief.grab = {};
            if (gBrief.dragFrom >= 0) {  // rows dropped: the selected ones (or the one dragged) go there
                const int from = gBrief.dragFrom, to = gBrief.dragTo;
                gBrief.dragFrom = gBrief.dragTo = -1;
                if (!gBrief.picked.count(from)) gBrief.picked = {from}, gBrief.sel = from;
                if (to >= 0) briefMoveTo(to);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (!moved && click) {
                click();
            }
        }
        return 0;
    case WM_SYSKEYDOWN:
        if (wp == VK_UP || wp == VK_DOWN) return briefNudge(wp == VK_UP ? -1 : 1), 0;  // Alt+Up / Alt+Down
        break;
    case WM_KEYDOWN:
        if (wp == VK_SPACE) return briefPlay(!gBrief.playing), 0;
        if (wp == VK_DELETE) return briefDelete(), 0;
        if ((wp == VK_UP || wp == VK_DOWN) && !gBriefRows.empty()) {
            const int n = std::clamp(gBrief.sel + (wp == VK_UP ? -1 : 1), 0, int(gBriefRows.size()) - 1);
            gBrief.sel = n, gBrief.picked = {n}, gBrief.anchor = n, gBrief.textFor = -1, gBriefFollowSel = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        for (auto it = gBrief.hits.rbegin(); it != gBrief.hits.rend(); ++it)
            if (PtInRect(&it->r, pt)) return SetCursor(LoadCursor(nullptr, it->cursor)), TRUE;
        break;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor(HDC(wp), kText);
        SetBkColor(HDC(wp), kCtl);
        return LRESULT(app.ctlBrush);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void createBriefingView() {
    if (gBrief.wnd) return;
    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpfnWndProc = briefWndProc, wc.lpszClassName = L"StarDraftBriefing";
    RegisterClassExW(&wc);
    gBrief.wnd = CreateWindowExW(0, L"StarDraftBriefing", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 1, 1, app.main, nullptr, app.inst, nullptr);
    gBrief.text = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 10, 10, gBrief.wnd, nullptr, app.inst, nullptr);
    SendMessageW(gBrief.text, WM_SETFONT, WPARAM(monoFont()), TRUE);
    SetWindowSubclass(gBrief.text, briefTextProc, 1, 0);
}
