// Experimental (branch experimental-simulation, STARDRAFT_SIMULATION): Tools > Simulate mining and Simulate
// combat of the selected units. StarDraft's simulation (tools/sim/sim.h) runs a game of the open map (its unsaved edits too)
// live on the map: its units move, mine and fight in place of the map's units, at the game's speed (Fastest) times the
// bar's speed. The bar at the top of the map says what runs; Cancel (or Esc) ends it and the map shows again; at the
// end the bar has the result, Report the whole report and Close.

#ifdef STARDRAFT_SIMULATION

#include "../../tools/sim/sim.h"
#include "../../tools/sim/sim_data.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

void sfxPlayOnce(int sfx);  // (sdl_data.inc)
void simulateCombatOf(std::set<int> selected);  // (below: a fight of these units)
bool sdlSfxLoad(int sfx, SDL_AudioSpec& spec, std::vector<Uint8>& pcm);
namespace {

// The simulator (made when a simulation first starts). Only used once simReady() is true.
std::unique_ptr<sim::Simulator> gSim_;
sim::Simulator& simEngine() { return *gSim_; }
bool gSimReady = false;

struct LiveSim {
    bool shown = false;  // the bar shows: a game runs, or ended and shows its end
    bool combat = false, done = false;
    const Editor* ed = nullptr;  // the map it runs on (another tab, or the map closed: it ends)
    float speed = 1;             // the game's speed times this (kSimSpeeds)
    bool skipping = false;       // Skip to the end: the game runs as fast as it can, a few milliseconds a window frame
    bool behind = false;         // the game cannot keep up with the speed (the bar shows the spinner)
    std::chrono::steady_clock::time_point last;
    double due = 0;  // frames due (parts of a frame)
    int players[2] = {0, 1};
    std::map<int, std::wstring> unitNames;
    int bases = 0, workers = 0;
    std::wstring endText, report;
    std::wstring result;  // the bar's few words at the end (Player 2 wins, 4066 minerals a minute)
    // How the game started (Restart and the timeline start it again: the game is the same each time).
    Bytes chk;
    std::function<bool(std::string&)> start;
    bool seeking = false;  // the timeline is dragged
    int hot = -1, pressed = -1;  // the bar's button under the mouse and the one pressed (0 the right one, 1, 2; -1 none)
};
LiveSim gSim;

// The game's data files as this map plays them (tools/sim/sim_data.h): a project's edited .dat files (Data workspace),
// then the map's unit settings.
std::map<std::string, Bytes> simDataFiles() {
    if (!gState.ed || !gState.ed->isOpen()) return {};
    std::map<std::string, Bytes> project;
    if (activeProject()) {
        dataLoadForProject();
        for (const auto& t : dataState().tables)
            if (t) project["arr/" + t->schema().name + ".dat"] = t->bytes();
    }
    return sim::dataFilesForMap(gState.data, gState.ed->chk(), std::move(project));
}

// A new simulator for each game, with the data of this map (simDataFiles).
bool simReady() {
    std::string err;
    gSim_ = sim::createSimulator();
    if (!gSim_) {
        if (gRoot) msgbox::ask(*gRoot, L"Simulation", L"The simulation engine is not in this build yet.", {L"OK"}, msgbox::Icon::Info);
        return false;
    }
    // (the simulation keeps the reader and reads more files later, a map's tileset: the reader owns the files)
    auto files = std::make_shared<const std::map<std::string, Bytes>>(simDataFiles());
    const bool ok = simEngine().init(
        [files](const std::string& name, std::vector<uint8_t>& out) {
            std::string low = name;
            for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (const auto it = files->find(low); it != files->end()) return out = it->second, true;
            std::string e;
            if (gState.data.readFile(name, out, e)) return true;
            return gState.data.readFile(low, out, e);
        },
        err);
    if (!ok) {
        if (gRoot) msgbox::ask(*gRoot, L"Simulation", L"The simulation cannot start: " + widen(err), {L"OK"}, msgbox::Icon::Error);
        return false;
    }
    gSimReady = true;
    return true;
}

std::wstring simNum(double v, int decimals = 0) {
    wchar_t b[32];
    std::swprintf(b, 32, decimals ? L"%.1f" : L"%.0f", v);
    return b;
}
std::wstring simClock(int frames) {  // game time at Fastest, m:ss
    const int s = int(frames * 0.042);
    wchar_t b[32];
    std::swprintf(b, 32, L"%d:%02d", s / 60, s % 60);
    return b;
}

// The alliances of a combat (Program options' SimAlliances; the bar's gear): who fights whom.
enum SimAlliances { kComputersAllied = 0, kMapForces = 1, kHumansVsComputers = 2, kFreeForAll = 3, kUnitFreeForAll = 4 };
constexpr int kTeamComputers = 8, kTeamHumans = 9, kTeamForce = 10;  // (team numbers; a force: kTeamForce + 0 to 3)
int simAlliances() {
    int a = kComputersAllied;
    prefs::number(L"SimAlliances", a, 0, 4);
    return a;
}
// A player's team for the alliances `a`: its own number (alone), the computers' or the humans' team, or its force's.
int simTeamOf(const Editor& ed, int player, int a) {
    const Editor::Controller who = ed.player(player).controller;
    switch (a) {
    case kMapForces: return kTeamForce + std::clamp(ed.player(player).force, 0, 3);
    case kHumansVsComputers: return who == Editor::Controller::Computer ? kTeamComputers : kTeamHumans;
    case kFreeForAll: return player;
    default: return who == Editor::Controller::Computer ? kTeamComputers : player;
    }
}

std::set<int> gSimCombatSelection;  // the units that fought last (the gear starts the fight again with them)
std::vector<std::pair<int, int>> gSimCombatPlaced;  // the fight's placed units (type, player): the last unit standing

// Skip to the end runs the game on a thread of its own, as fast as the computer can: the window shows the last
// picture and a spinner meanwhile, and touches the engine only through these (simSkipFrame: the game's frame).
std::thread gSimSkipThread;
std::atomic<bool> gSimSkipStop{false}, gSimSkipEnded{false};
std::atomic<int> gSimSkipFrame{0};
bool simSkipRunning() { return gSimSkipThread.joinable(); }
bool simSkipJoin();
struct SimSkipAtExit {  // (the program ends during a skip: the thread stops first, before the engine goes)
    ~SimSkipAtExit() { simSkipJoin(); }
} gSimSkipAtExit;
// Ends the skip's thread (stopped when it still runs); true when it ran to the game's end.
bool simSkipJoin() {
    if (!gSimSkipThread.joinable()) return false;
    gSimSkipStop = true;
    gSimSkipThread.join();
    if (gSim_) simEngine().setFastForward(false);  // (the pictures and sounds again)
    gSimSkipStop = false;
    return gSimSkipEnded.exchange(false);
}

// The live game's images as the last frame before a skip had them (the engine is the skip's thread's meanwhile).
std::vector<sim::LiveImage> gSimLastImages;
const std::vector<sim::LiveImage>& simImages() {
    if (!simSkipRunning()) gSimLastImages.clear(), simEngine().images(gSimLastImages);
    return gSimLastImages;
}

// The live game's images in place of the map's units and sprites (core/map_scene.inc: gLiveScene).
void simScene(render::Scene& s) {
    if (!gState.ed) return;
    UnitSprites& sp = gState.ed->unitSprites();
    const std::vector<sim::LiveImage>& images = simImages();
    for (const sim::LiveImage& li : images) {
        render::SpriteDraw d;
        d.image = li.image, d.frame = li.frame, d.flip = li.flip;
        d.x = float(li.x), d.y = float(li.y);
        d.owner = uint8_t(std::clamp(li.owner, 0, 11));
        d.air = li.air;
        d.hasSortY = true, d.sortY = float(li.sortY);
        // The drawing order is the game's: by elevation level (0 to 19: remnants, ground units, their hits above them,
        // air units), then by place within a level. The renderer's depth (renderer.cpp: by air and sortY, less the
        // bias) gets the bias that gives depth = 0.95 - 0.045 x elevation - 0.04 x (the place down the map, 0 to 1).
        {
            const float yn = std::clamp(float(li.sortY) / float(std::max(1, gState.ed->height() * 32)), 0.0f, 1.0f);
            const float base = li.air ? 0.2f - 0.15f * yn : 0.95f - 0.7f * yn;
            const float want = 0.95f - 0.045f * float(std::clamp(li.elevation, 0, 19)) - 0.04f * yn;
            d.depthBias = base - want + li.order * 0.0001f;
        }
        d.shadow = sp.isShadow(li.image);
        d.glow = sp.drawFunction(li.image) == 9;
        // A sprite's main image with what its script puts under and over it (the shadow, lights), as the map draws its
        // own units (core/map_scene.inc: addImage); the other images of the sprite (a turret) as they come.
        if (li.order == 0 && !d.shadow) addImage(s, d);
        else s.sprites.push_back(d);
    }
}

// The minimap's dots of the live game: each sprite's main image (not shadows, overlays, or the remnants under the
// units).
// The live game's creep for the map's Creep layer (core/creep_layer.inc: gLiveCreep): a copy, kept while a skip runs
// (the engine is the skip's thread's then).
std::vector<uint8_t> gSimCreep;
uint64_t gSimCreepChanges = ~0ull;
const std::vector<uint8_t>* simCreep(uint64_t& changes) {
    if (!simSkipRunning() && gSim_) {
        const uint64_t now = simEngine().creepChanges();
        if (now != gSimCreepChanges) {
            const std::vector<uint8_t>* c = simEngine().creep();
            gSimCreep = c ? *c : std::vector<uint8_t>{};
            gSimCreepChanges = now;
        }
    }
    changes = gSimCreepChanges;
    return gSimCreep.empty() ? nullptr : &gSimCreep;
}

bool simMinimap(std::vector<MinimapDot>& dots) {
    const std::vector<sim::LiveImage>& images = simImages();
    UnitSprites& sp = gState.ed->unitSprites();
    for (const sim::LiveImage& li : images)
        if (li.order == 0 && li.sortY > li.y - 60000 && !sp.isShadow(li.image)) dots.push_back({li.x, li.y, li.owner, li.air ? 24 : 32});
    return true;
}

void simRedraw() {
    gState.dirty = true;
    if (gRoot) gRoot->map.invalidate(), gRoot->minimap.invalidate();
    invalidateMapView();
}

// The report: HTML for its window (tables with a column for each value) and the same as text for Copy (a table's
// cells with tabs between them, so that a spreadsheet takes its columns).
struct SimDoc {
    std::string html;
    std::wstring text;
    static std::string esc(const std::wstring& w) {
        std::string out;
        for (char ch : narrow(w)) {
            if (ch == '&') out += "&amp;";
            else if (ch == '<') out += "&lt;";
            else if (ch == '>') out += "&gt;";
            else out += ch;
        }
        return out;
    }
    void para(const std::wstring& p, bool dim = false) {
        html += std::string(dim ? "<p class=\"dim\">" : "<p>") + esc(p) + "</p>";
        text += p + L"\r\n";
    }
    // A heading; `player` >= 0: that player's color before it.
    void heading(const std::wstring& h, int player = -1) {
        std::string swatch;
        if (player >= 0) {
            char b[64];
            std::snprintf(b, sizeof b, "<span style=\"color: #%06x\">\xE2\x96\xA0</span> ", unsigned(playerRgba(player, 0) >> 8));
            swatch = b;
        }
        html += "<h3>" + swatch + esc(h) + "</h3>";
        text += L"\r\n" + h + L"\r\n";
    }
    // A table: its first `left` columns are text (on the left), the others numbers (on the right); a row whose first
    // cell starts with "=" is a total (bold, without the "=").
    void table(const std::vector<std::wstring>& head, const std::vector<std::vector<std::wstring>>& rows, size_t left = 1) {
        auto cls = [&](size_t k) { return k < left ? std::string() : std::string(" class=\"n\""); };
        html += "<table><tr>";
        for (size_t k = 0; k < head.size(); ++k) html += "<th" + cls(k) + ">" + esc(head[k]) + "</th>";
        html += "</tr>";
        for (size_t k = 0; k < head.size(); ++k) text += (k ? L"\t" : L"") + head[k];
        text += L"\r\n";
        for (const auto& row : rows) {
            const bool total = !row.empty() && !row[0].empty() && row[0][0] == L'=';
            html += total ? "<tr class=\"total\">" : "<tr>";
            for (size_t k = 0; k < row.size(); ++k) {
                const std::wstring cell = k == 0 && total ? row[k].substr(1) : row[k];
                html += "<td" + cls(k) + ">" + (total ? "<b>" + esc(cell) + "</b>" : esc(cell)) + "</td>";
                text += (k ? L"\t" : L"") + cell;
            }
            html += "</tr>";
            text += L"\r\n";
        }
        html += "</table>";
    }
};

// The report's window (the bar's Report): a title, a line under it, the tables, and Copy and Close. It is a window of
// its own that stays open (another simulation's end shows its report in it).
class SimReportView final : public draw::View {
public:
    SimReportView() {
        scroll_.setContent(&html_);
        add(scroll_, {});
        copy_.setText(L"Copy");
        copy_.setTip(L"Copy the report as text (its tables with tabs, for a spreadsheet)");
        copy_.onClick = [this] {
            if (host()) host()->setClipboardText(narrow(text_));
            setMessage(L"Copied the simulation's report");
        };
        close_.setText(L"Close");
        close_.setTone(stardraft::kit::Tone::Primary);
        close_.onClick = [this] { close(); };
        add(copy_, {});
        add(close_, {});
    }
    static draw::Point size() { return {scale(780), scale(640)}; }

    void set(const std::wstring& title, const std::wstring& line, const SimDoc& doc) {
        title_ = title, line_ = line, text_ = title + L"\r\n" + line + L"\r\n\r\n" + doc.text;
        html_.setExtraCss(  // (here, not at the start: the theme's gray can change)
            "body { padding: " + std::to_string(stardraft::kit::kDialogPad) + "px " + std::to_string(stardraft::kit::kDialogPad + 4) +
            "px; }\n"
            "h3 { margin: 18px 0 8px 0; } h3:first-child { margin-top: 0; }\n"
            "th { text-align: left; white-space: nowrap; } td { white-space: nowrap; }\n"
            "th.n, td.n { text-align: right; }\n"
            "p.dim { color: " +
            dimCss() + "; }\n");
        html_.setHtml("<html><body>" + doc.html + "</body></html>", std::string());
        scroll_.setOffset(0, 0);
        relayout();
        invalidate();
    }
    const std::wstring& text() const { return text_; }
    stardraft::html::HtmlView& html() { return html_; }

    void paint(draw::Canvas& c) override {
        const int w = width(), h = height(), pad = scale(stardraft::kit::kDialogPad);
        c.fill({0, 0, w, h}, kPanel);
        draw::FontDesc td;
        td.px = -shellUiFontLfHeight() * 3 / 2, td.weight = 600;
        const draw::Font of = c.setFont(c.font(td));
        c.text({pad, pad, w - pad, pad + scale(30)}, title_, kText, draw::kMiddle | draw::kEllipsis);
        c.setFont(c.font({}));
        c.text({pad, pad + scale(30), w - pad, pad + scale(52)}, line_, kDim, draw::kMiddle | draw::kEllipsis);
        c.setFont(of);
        const draw::Rect sr = scroll_.frame();
        c.fill({0, sr.top - 1, w, sr.top}, kBorder);
        c.fill({0, sr.bottom, w, sr.bottom + 1}, kBorder);
        c.fill(sr, kBg);
    }
    void resized() override { relayout(); }
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || (e.mods & draw::kAlt)) return false;
        if (e.key == draw::Key::Escape) return close(), true;
        return scroll_.key(e);
    }
    void close() {
        if (host()) host()->closeWindow(*this);
        open = false;
    }
    bool open = false;

private:
    static std::string dimCss() {
        char b[16];
        std::snprintf(b, sizeof b, "#%02x%02x%02x", unsigned(kDim & 255), unsigned(kDim >> 8 & 255), unsigned(kDim >> 16 & 255));
        return b;
    }
    void relayout() {
        const int w = width(), h = height();
        if (w <= 0 || h <= 0) return;
        const int pad = scale(stardraft::kit::kDialogPad), bh = scale(stardraft::kit::kButtonH), gap = scale(stardraft::kit::kButtonGap);
        const int bw = scale(stardraft::kit::kButtonMinW);
        const int top = pad + scale(60), bottom = h - pad - bh - pad;
        scroll_.setFrame({0, top, w, std::max(top, bottom)});
        const int y = h - pad - bh;
        close_.setFrame({w - pad - bw, y, w - pad, y + bh});
        copy_.setFrame({w - pad - 2 * bw - gap, y, w - pad - bw - gap, y + bh});
    }
    std::wstring title_, line_, text_;
    stardraft::html::HtmlView html_;
    stardraft::kit::ScrollView scroll_;
    stardraft::kit::Button copy_, close_;
};
SimReportView gSimReport;
std::wstring gSimTitle, gSimLine;  // the report's title and the line under it
SimDoc gSimDoc;

void simShowReport() {
    gSimReport.set(gSimTitle, gSimLine, gSimDoc);
    if (gSimReport.open || !gRoot || !gHost) return;
    changelog::setHtmlHooks(!gSilent);  // (the HTML view's measuring canvas)
    gSimReport.open = true;
    const draw::Point sz = SimReportView::size();
    const int h = std::min(sz.y, std::max(scale(300), gRoot->height() - scale(80)));
    draw::WindowSpec spec{draw::WindowKind::Dialog, draw::Rect(0, 0, sz.x, h), L"Simulation report"};
    spec.resizable = true;
    gHost->openWindow(*gRoot, gSimReport, spec, [] { gSimReport.open = false; });
}

std::wstring simOf(int part, int all) { return std::to_wstring(part) + L" of " + std::to_wstring(all); }

// The end: the result in the bar, the report for Report.
void simEnd() {
    sim::Simulator& e = simEngine();
    gSim.done = true;
    gSimDoc = SimDoc{};
    if (!e.liveError().empty()) {
        gSim.endText = L"The simulation stopped: " + widen(e.liveError());
        gSimTitle = L"The simulation stopped", gSimLine = widen(e.liveError());
        gSimDoc.para(gSim.endText);
        gSim.result = L"Stopped";
        gSim.report = gSim.endText;
        if (gSimReport.open) gSimReport.set(gSimTitle, gSimLine, gSimDoc);
        return simRedraw();
    }
    if (gSim.combat && !e.combatResult().teams.empty()) {
        const sim::CombatResult r = e.combatResult();
        auto teamName = [](const sim::CombatSide& t) {
            std::wstring n;
            for (int p : t.players) n += (n.empty() ? L"" : L", ") + std::to_wstring(p + 1);
            const std::wstring of = (t.players.size() > 1 ? L" (players " : L" (player ") + n + L")";
            if (t.team == kTeamComputers) return (t.players.size() > 1 ? L"Computers" : L"Computer") + of;
            if (t.team == kTeamHumans) return L"Humans" + of;
            if (t.team >= kTeamForce && t.team < kTeamForce + 4) {
                std::string fn = gState.ed ? gState.ed->force(t.team - kTeamForce).name : std::string();
                return (fn.empty() ? L"Force " + std::to_wstring(t.team - kTeamForce + 1) : widen(fn)) + of;
            }
            return L"Player " + n;
        };
        const sim::CombatSide* won = nullptr;
        for (const sim::CombatSide& t : r.teams)
            if (t.team == r.winner) won = &t;
        std::wstring winner = won ? teamName(*won) + L" wins" : r.stalemate ? L"Draw: Units cannot reach each other" : r.timedOut ? L"No winner: 10 game minutes went by" : L"No winner";
        if (won && r.helpless >= 0) winner += L" (the others have only units that cannot attack)";
        if (simAlliances() == kUnitFreeForAll && r.winnerUnit >= 0 && size_t(r.winnerUnit) < gSimCombatPlaced.size()) {
            const auto [type, owner] = gSimCombatPlaced[size_t(r.winnerUnit)];
            const auto nm = gSim.unitNames.find(type);
            winner = (nm != gSim.unitNames.end() ? nm->second : L"Unit " + std::to_wstring(type)) + L" of player " + std::to_wstring(owner + 1) +
                     L" is the last unit standing";
        }
        int unreachable = 0;
        for (const sim::CombatSide& t : r.teams) unreachable += t.unreachable;
        if (!won && !r.timedOut && !r.stalemate && unreachable)
            winner = L"No winner: " + std::to_wstring(unreachable) + (unreachable == 1 ? L" unit is" : L" units are") + L" out of reach of their enemies";
        gSim.endText = winner + L" after " + simNum(r.frames * 0.042, 1) + L" game seconds";
        gSim.result = won ? teamName(*won) + L" wins" : winner;
        auto name = [&](int type) {
            const auto n = gSim.unitNames.find(type);
            return n != gSim.unitNames.end() ? n->second : L"Unit " + std::to_wstring(type);
        };
        std::wstring lost;
        for (const sim::CombatSide& t : r.teams)
            lost += (lost.empty() ? L"Lost (minerals + gas): " : L"; ") + teamName(t) + L" " + std::to_wstring(t.mineralsLost + t.gasLost);
        gSimTitle = gSim.endText;
        gSimLine = lost;
        SimDoc& d = gSimDoc;
        for (const sim::CombatSide& s : r.teams) {
            bool sh = false;
            for (const sim::UnitTypeStats& u : s.types) sh = sh || u.maxShields;
            std::vector<std::wstring> head = {L"Unit", L"Alive", L"Kills", L"Hit points"};
            if (sh) head.push_back(L"Shields");
            head.push_back(L"Lost (minerals + gas)");
            std::vector<std::vector<std::wstring>> rows;
            for (const sim::UnitTypeStats& u : s.types) {
                std::vector<std::wstring> row = {name(u.unitType), simOf(u.alive, u.started), std::to_wstring(u.kills), simOf(u.hp, u.maxHp)};
                if (sh) row.push_back(u.maxShields ? simOf(u.shields, u.maxShields) : std::wstring(L"\x2013"));
                row.push_back(std::to_wstring((u.started - u.alive) * (u.mineralCost + u.gasCost)));
                rows.push_back(row);
            }
            std::vector<std::wstring> total = {L"=All", simOf(s.alive, s.units), std::to_wstring(s.kills), simOf(s.hp, s.startHp)};
            if (sh) total.push_back(simOf(s.shields, s.startShields));
            total.push_back(std::to_wstring(s.mineralsLost + s.gasLost));
            rows.push_back(total);
            d.heading(teamName(s), s.players.empty() ? -1 : s.players.front());
            d.table(head, rows);
            if (s.unreachable)
                d.para(std::to_wstring(s.unreachable) + (s.unreachable == 1 ? L" unit was" : L" units were") +
                       L" out of reach: no enemy could get to it or attack it (an island, only air or ground weapons, or cloaked)", true);
        }
        const int al = simAlliances();
        const std::wstring who = al == kUnitFreeForAll ? L"every unit fights every other unit" : al == kMapForces ? L"the map's forces fight each other"
                               : al == kHumansVsComputers ? L"the human players fight the computer players"
                               : al == kFreeForAll ? L"each player fights alone" : L"the computer players are allies, each other player fights alone";
        d.para(L"Fastest, with the map's upgrades; " + who + L"; each unit attacks the enemies it can reach until none are left, for 10 "
               L"game minutes at most.", true);
        gSim.report = gSimTitle + L"\r\n" + gSimLine + L"\r\n" + d.text;
    } else if (gSim.combat) {
        const sim::CombatResult r = e.combatResult();
        const std::wstring a = L"Player " + std::to_wstring(gSim.players[0] + 1), b = L"Player " + std::to_wstring(gSim.players[1] + 1);
        std::wstring winner = r.a.alive && !r.b.alive ? a + L" wins" : r.b.alive && !r.a.alive ? b + L" wins"
                              : r.timedOut                                ? L"No winner: 2 game minutes went by"
                                                                          : L"No winner: both sides died";
        if (r.helpless >= 0) winner = (r.helpless ? a : b) + L" wins (" + (r.helpless ? b : a) + L" has only units that cannot attack)";
        gSim.endText = winner + L" after " + simNum(r.frames * 0.042, 1) + L" game seconds";
        auto name = [&](int type) {
            const auto n = gSim.unitNames.find(type);
            return n != gSim.unitNames.end() ? n->second : L"Unit " + std::to_wstring(type);
        };
        const int valueA = r.a.mineralsLost + r.a.gasLost, valueB = r.b.mineralsLost + r.b.gasLost;
        // (what each side lost is in its table: the line has only the trade)
        std::wstring trade = !valueA && !valueB ? L"Neither side lost a unit"
                             : !valueA          ? a + L" lost nothing"
                             : !valueB          ? b + L" lost nothing"
                             : valueA <= valueB ? a + L" traded " + simNum(double(valueB) / valueA, 1) + L" to 1"
                                                : b + L" traded " + simNum(double(valueA) / valueB, 1) + L" to 1";
        gSim.result = r.helpless >= 0 ? (r.helpless ? a : b) + L" wins" : winner;
        gSimTitle = gSim.endText;
        gSimLine = trade;
        SimDoc& d = gSimDoc;
        // Each side: its units by type and the whole army (the All row).
        auto side = [&](const std::wstring& who, int player, const sim::CombatSide& s) {
            bool sh = false;
            for (const sim::UnitTypeStats& u : s.types) sh = sh || u.maxShields;
            std::vector<std::wstring> head = {L"Unit", L"Alive", L"Kills", L"Hit points"};
            if (sh) head.push_back(L"Shields");
            head.push_back(L"Lost (minerals + gas)");
            std::vector<std::vector<std::wstring>> rows;
            for (const sim::UnitTypeStats& u : s.types) {
                std::vector<std::wstring> row = {name(u.unitType), simOf(u.alive, u.started), std::to_wstring(u.kills), simOf(u.hp, u.maxHp)};
                if (sh) row.push_back(u.maxShields ? simOf(u.shields, u.maxShields) : std::wstring(L"\x2013"));
                row.push_back(std::to_wstring((u.started - u.alive) * (u.mineralCost + u.gasCost)));
                rows.push_back(row);
            }
            std::vector<std::wstring> total = {L"=All", simOf(s.alive, s.units), std::to_wstring(s.kills), simOf(s.hp, s.startHp)};
            if (sh) total.push_back(simOf(s.shields, s.startShields));
            total.push_back(std::to_wstring(s.mineralsLost + s.gasLost));
            rows.push_back(total);
            d.heading(who, player);
            d.table(head, rows);
        };
        side(a, gSim.players[0], r.a);
        side(b, gSim.players[1], r.b);
        d.para(L"Fastest, no upgrades or research; each side attack-moves to the other's middle, for 2 game minutes at most.", true);
        gSim.report = gSimTitle + L"\r\n" + gSimLine + L"\r\n" + d.text;
    } else {
        const std::vector<sim::MiningBase> bases = e.miningResult();
        std::wstring summary;
        SimDoc& d = gSimDoc;
        std::vector<std::vector<std::wstring>> rows;
        std::vector<std::wstring> tripHead = {L"Field"};
        std::vector<const sim::MiningRun*> runs;
        double minerals = 0, gas = 0;
        for (size_t k = 0; k < bases.size(); ++k) {
            const sim::MiningBase& b = bases[k];
            if (b.runs.empty()) continue;
            const sim::MiningRun& r = b.runs[0];
            const std::wstring base = L"Base " + std::to_wstring(k + 1);
            const wchar_t* race = r.race == sim::Zerg ? L"Zerg" : r.race == sim::Protoss ? L"Protoss" : L"Terran";
            rows.push_back({base + (b.startLocation ? L" (start)" : L""), race, std::to_wstring(b.x / 32) + L", " + std::to_wstring(b.y / 32),
                            std::to_wstring(b.fields.size()), simNum(r.mineralsPerMinute), simNum(r.gasPerMinute)});
            minerals += r.mineralsPerMinute, gas += r.gasPerMinute;
            tripHead.push_back(base);
            runs.push_back(&r);
        }
        if (rows.size() > 1) rows.push_back({L"=All", L"", L"", L"", simNum(minerals), simNum(gas)});
        gSim.endText = gSim.result = simNum(minerals) + L" minerals a minute";
        gSimTitle = L"Mining: " + simNum(minerals) + L" minerals and " + simNum(gas) + L" gas a minute";
        int gasWorkers = 3, minutes = 3;
        prefs::number(L"SimGasWorkers", gasWorkers, 0, 3), prefs::number(L"SimMinutes", minutes, 1, 60);
        gSimLine = std::to_wstring(gSim.workers) + L" workers on minerals and " +
                   (gasWorkers ? std::to_wstring(gasWorkers) + L" on gas" : std::wstring(L"none on gas")) + L" at each base, " +
                   std::to_wstring(minutes) + (minutes == 1 ? L" game minute" : L" game minutes") + L" at Fastest";
        d.table({L"Base", L"Race", L"Tile", L"Fields", L"Minerals a minute", L"Gas a minute"}, rows, 3);
        size_t most = 0;
        for (const sim::MiningRun* r : runs) most = std::max(most, r->tripFrames.size());
        if (most) {
            std::vector<std::vector<std::wstring>> trips;
            for (size_t f = 0; f < most; ++f) {
                std::vector<std::wstring> row = {std::to_wstring(f + 1)};
                for (const sim::MiningRun* r : runs)
                    row.push_back(f < r->tripFrames.size() && r->tripFrames[f] > 0 ? simNum(r->tripFrames[f]) : std::wstring(L"\x2013"));
                trips.push_back(row);
            }
            d.heading(L"Round trip of each field, nearest first (frames)");
            d.table(tripHead, trips);
        }
        gSim.report = gSimTitle + L"\r\n" + gSimLine + L"\r\n" + d.text;
    }
    if (gSimReport.open) gSimReport.set(gSimTitle, gSimLine, gSimDoc);
    simRedraw();
}

// While a game shows, the rest of the program is held: the map only shows the game (its view still moves), and the
// panels around it (the side bars, the tool bar, the tabs, the minimap, the floating palettes and the palette panels)
// are dimmed under a shield that takes their mouse. The menus stay. The shields are the last children (on top) of the
// main window's view and of each palette window's view; simFrame keeps them in place.
struct SimShield final : draw::View {
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg, 200); }
    bool mouse(const draw::MouseEvent&) override { return true; }
};
std::map<draw::View*, std::vector<std::unique_ptr<SimShield>>> gSimShields;

void simShield(draw::View& v, std::vector<draw::Rect> rects) {
    auto& list = gSimShields[&v];
    while (list.size() < rects.size()) list.push_back(std::make_unique<SimShield>());
    rects.resize(list.size(), draw::Rect(0, 0, 0, 0));  // (shields that are not needed now: no size)
    const auto& kids = v.children();
    const bool onTop = kids.size() >= list.size() && std::equal(list.begin(), list.end(), kids.end() - std::ptrdiff_t(list.size()),
                                                                 [](const std::unique_ptr<SimShield>& a, draw::View* b) { return a.get() == b; });
    for (size_t k = 0; k < list.size(); ++k) {
        if (!onTop) v.add(*list[k], rects[k]);  // (again: after a view that was added since, or a new view at a freed address)
        else if (const draw::Rect f = list[k]->frame(); f.left != rects[k].left || f.top != rects[k].top || f.right != rects[k].right || f.bottom != rects[k].bottom)
            list[k]->setFrame(rects[k]);
    }
}
void simHold() {
    if (!gRoot) return;
    const int w = gRoot->width(), h = gRoot->height();
    const draw::Rect m = gRoot->map.frame();
    const int top = gRoot->menus.shown() ? int(gRoot->menus.frame().bottom) : 0;
    // The minimap stays usable (a click moves the view): the shields go round it.
    const draw::Rect hole = gRoot->minimap.shown() ? gRoot->minimap.frame() : draw::Rect(0, 0, 0, 0);
    std::vector<draw::Rect> rects;
    for (const draw::Rect& r : {draw::Rect(0, top, w, m.top), draw::Rect(0, m.bottom, w, h), draw::Rect(0, m.top, m.left, m.bottom), draw::Rect(m.right, m.top, w, m.bottom)}) {
        const bool cut = hole.right > r.left && hole.left < r.right && hole.bottom > r.top && hole.top < r.bottom;
        if (!cut) {
            rects.push_back(r);
            continue;
        }
        rects.push_back({r.left, r.top, r.right, std::max(r.top, hole.top)});                   // above it
        rects.push_back({r.left, std::min(r.bottom, hole.bottom), r.right, r.bottom});          // under it
        rects.push_back({r.left, std::max(r.top, hole.top), std::max(r.left, hole.left), std::min(r.bottom, hole.bottom)});      // beside it
        rects.push_back({std::min(r.right, hole.right), std::max(r.top, hole.top), r.right, std::min(r.bottom, hole.bottom)});
    }
    simShield(*gRoot, rects);
    if (gFloatPalOpen) simShield(gFloatPalView, {{0, 0, gFloatPalView.width(), gFloatPalView.height()}});
    for (const auto& p : gPanels)
        if (p->open && p->view) simShield(*p->view, {{0, 0, p->view->width(), p->view->height()}});
}
void simRelease() {
    for (auto& [v, list] : gSimShields)
        for (auto& sh : list)
            if (draw::View* parent = sh->parent()) parent->remove(*sh);
    gSimShields.clear();
}

// Cancel, Close or Esc: the map's own units again.
void simStopSounds();
void simClose() {
    simSkipJoin();
    simStopSounds();
    if (gSim_) simEngine().stop();
    gLiveScene = nullptr, gLiveMinimap = nullptr, gLiveCreep = nullptr;
    gSimCreep.clear(), gSimCreepChanges = ~0ull;
    gSim = LiveSim{};
    simRelease();
    simRedraw();
}

// A game starts: the map's copy (its unsaved edits too) into the simulation, then `start`.
bool simBegin(bool combat, const std::function<bool(std::string&)>& start) {
    if (!gState.ed || !gState.ed->isOpen()) return false;
    if (gSim.shown) simClose();
    if (!simReady()) return false;
    std::string err;
    Bytes chk = gState.ed->chk().serialize();
    if (!simEngine().setMap(chk, err) || !start(err)) {
        if (gRoot) msgbox::ask(*gRoot, L"Simulation", widen(err), {L"OK"}, msgbox::Icon::Error);
        return false;
    }
    gSim.shown = true, gSim.combat = combat, gSim.done = false, gSim.ed = gState.ed.get();
    gSim.chk = std::move(chk), gSim.start = start;
    gSim.last = std::chrono::steady_clock::now(), gSim.due = 0;
    gLiveScene = simScene, gLiveMinimap = simMinimap, gLiveCreep = simCreep;
    gSimCreep.clear(), gSimCreepChanges = ~0ull;
    if (gRoot) gRoot->map.focus();  // (a search box of a panel lets go of the keyboard)
    simHold();
    simRedraw();
    return true;
}

// The live game's sound player: at most 64 sounds at a time, at most 3 of one sound, and one sound does not start again
// within 60 ms (a crowd's attacks are not a wall of noise). The sounds near the view's middle come first: with 64
// playing, a nearer sound stops the farthest one. Each sound plays in stereo, panned by `pan` (-1 left, 1 right) with an
// equal-power law, at `gain` times the program's volume; `dist` is its distance from the listening camera (map pixels).
// The decoded sounds are kept (as mono floats).
struct SimVoice {
    SDL_AudioStream* stream;
    int sfx;
    float dist;
};
std::vector<SimVoice> gSimVoices;
void simStopSounds() {
    for (const SimVoice& v : gSimVoices) SDL_DestroyAudioStream(v.stream);
    gSimVoices.clear();
}
void simPlaySound(int sfx, float pan, float gain, float dist) {
    using Voice = SimVoice;
    std::vector<Voice>& voices = gSimVoices;
    struct Mono {
        int freq = 0;
        std::vector<float> samples;
    };
    static std::map<int, Mono> decoded;
    static std::map<int, Uint64> lastStart;
    for (auto it = voices.begin(); it != voices.end();)
        if (SDL_GetAudioStreamQueued(it->stream) <= 0) SDL_DestroyAudioStream(it->stream), it = voices.erase(it);
        else ++it;
    if (std::count_if(voices.begin(), voices.end(), [&](const Voice& v) { return v.sfx == sfx; }) >= 3) return;
    const Uint64 now = SDL_GetTicks();
    if (auto l = lastStart.find(sfx); l != lastStart.end() && now - l->second < 60) return;
    if (voices.size() >= 64) {  // (full: the farthest sound gives way to a nearer one)
        auto farthest = std::max_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b) { return a.dist < b.dist; });
        if (farthest->dist <= dist) return;
        SDL_DestroyAudioStream(farthest->stream);
        voices.erase(farthest);
    }
    auto it = decoded.find(sfx);
    if (it == decoded.end()) {
        Mono m;
        SDL_AudioSpec spec{};
        std::vector<Uint8> pcm;
        if (sdlSfxLoad(sfx, spec, pcm)) {
            const SDL_AudioSpec mono{SDL_AUDIO_F32, 1, spec.freq};
            Uint8* out = nullptr;
            int len = 0;
            if (SDL_ConvertAudioSamples(&spec, pcm.data(), int(pcm.size()), &mono, &out, &len)) {
                m.freq = spec.freq;
                m.samples.assign(reinterpret_cast<float*>(out), reinterpret_cast<float*>(out) + size_t(len) / sizeof(float));
                SDL_free(out);
            }
        }
        if (decoded.size() > 128) decoded.clear();
        it = decoded.emplace(sfx, std::move(m)).first;
    }
    const Mono& m = it->second;
    if (m.samples.empty()) return;
    const SDL_AudioSpec stereo{SDL_AUDIO_F32, 2, m.freq};
    SDL_AudioStream* s = openPlaybackSdl(&stereo);
    if (!s) return;
    const float a = (std::clamp(pan, -1.0f, 1.0f) + 1) * 0.785398f / 2;  // (0 to pi/2)
    const float left = std::cos(a), right = std::sin(a);
    std::vector<float> lr(m.samples.size() * 2);
    for (size_t i = 0; i < m.samples.size(); ++i) lr[2 * i] = m.samples[i] * left, lr[2 * i + 1] = m.samples[i] * right;
    SDL_SetAudioStreamGain(s, float(gDock.volume) / 100.0f * std::clamp(gain, 0.0f, 1.0f));
    SDL_PutAudioStreamData(s, lr.data(), int(lr.size() * sizeof(float)));
    SDL_FlushAudioStream(s);
    SDL_ResumeAudioStreamDevice(s);
    voices.push_back({s, sfx, dist});
    lastStart[sfx] = now;
}

// The game's sounds (attacks, deaths, hits) that are in the view or near it, at the program's volume (Program options;
// at 0 none play, and a test run plays nothing); at most 8 at a time (sfxPlayOnce). A seek drops the sounds it went past.
void simSounds(bool play) {
    const std::vector<sim::LiveSound> sounds = simEngine().takeSounds();
    if (gSilent && !sounds.empty()) {  // (a test run plays nothing: it says what would play)
        SDL_AudioSpec spec{};
        std::vector<Uint8> pcm;
        const bool ok = sdlSfxLoad(sounds[0].sfx, spec, pcm);
        std::printf("simsound: %zu at frame %d (play %d, volume %d); sfx %d at %d,%d: %s, %zu bytes\n", sounds.size(), simEngine().frame(), int(play),
                    gDock.volume, sounds[0].sfx, sounds[0].x, sounds[0].y, ok ? "decoded" : "NOT decoded", pcm.size());
    }
    if (!play || gDock.volume <= 0 || sounds.empty()) return;
    int vw = 0, vh = 0;
    shellViewSize(vw, vh);
    float x0, y0, x1, y1;
    toWorld(0, 0, x0, y0), toWorld(vw, vh, x1, y1);
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = std::max(1.0f, (x1 - x0) / 2);
    // The listener is a camera above the view's middle, as high as the view is zoomed out: its height is 320 map pixels
    // at 100% (4 x that at 25%). A sound's distance is from the camera, in map pixels: full volume within 320, none
    // from 2400 on, a straight fall between. So zoomed out everything is quieter and far sounds go first; zoomed in
    // the units near the middle are loud. Left or right of the middle pans it (by the view's half width, at least
    // 400 pixels). The nearest first.
    const float height = 320.0f / std::max(0.05f, gState.zoom), nearDist = 320, farDist = 2400;
    struct Heard {
        int sfx;
        float pan, dist;
    };
    std::vector<Heard> close;
    for (const sim::LiveSound& s : sounds) {
        const float dx = float(s.x) - cx, dy = float(s.y) - cy, dist = std::sqrt(dx * dx + dy * dy + height * height);
        if (dist < farDist) close.push_back({s.sfx, std::clamp(dx / std::max(hw, 400.0f), -1.0f, 1.0f) * 0.8f, dist});
    }
    std::stable_sort(close.begin(), close.end(), [](const Heard& a, const Heard& b) { return a.dist < b.dist; });
    std::set<int> heard;  // (one of each sound a frame: the nearest)
    for (const Heard& h : close) {
        if (!heard.insert(h.sfx).second) continue;
        const float gain = std::clamp((farDist - h.dist) / (farDist - nearDist), 0.0f, 1.0f);
        if (gain > 0.03f) simPlaySound(h.sfx, h.pan, gain, h.dist);
    }
}

// The game at `frame` (Restart: 0; the timeline): the game again from the start, run at once to that frame (the
// game is the same each time it runs). Forward: only the frames between.
void simSeek(int frame) {
    simSkipJoin();
    gSim.skipping = false;
    if (!gSim.shown || !gSim.start) return;
    sim::Simulator& e = simEngine();
    frame = std::max(0, frame);
    if (frame < e.frame() || gSim.done) {
        std::string err;
        if (!e.setMap(gSim.chk, err) || !gSim.start(err)) return simClose();
        gSim.done = false;
    }
    if (frame > e.frame() && !e.step(frame - e.frame())) simEnd();
    simSounds(false);
    gSim.due = 0, gSim.last = std::chrono::steady_clock::now();
    simRedraw();
}

// The bar: a compact pill at the top middle of the map. At the left an info icon (its tooltip: what the simulation
// is), then what runs and the game time (at the end the result); at the right the buttons: the speed with - and +,
// Restart, Skip to the end and Cancel (at the end: Restart, Report and Close). Mining has a thin timeline along the
// pill's bottom edge: how far the game is, of its longest time; a click or a drag goes to that time.
// The buttons' ids (gSim.hot, gSim.pressed): 0 Cancel/Close, 1 the speed/Report, 2 Restart, 3 the info icon, 4 -, 5 +,
// 6 Skip to the end, 7 the mining's settings, 8 Report feedback.
constexpr float kSimSpeeds[] = {0.25f, 0.5f, 1, 2, 4, 8, 16, 32, 64};
struct SimBarLayout {
    draw::Rect bar, chip, text, first, second, third, minus, plus, skip, gear, feedback, track, trackHit;
};
SimBarLayout simLayout(draw::Rect r) {
    SimBarLayout l;
    const int h = scale(40), pad = scale(5), ib = h - 2 * pad, gap = scale(2), tw = scale(gSim.done ? 64 : 46);
    const int w = std::min(int(r.right - r.left) - scale(16), scale(gSim.done ? 460 : 480)), x = (r.left + r.right - w) / 2, y = r.top + scale(10);
    auto left = [&](draw::Rect of, int width) { return draw::Rect{of.left - gap - width, y + pad, of.left - gap, y + pad + ib}; };
    l.bar = {x, y, x + w, y + h};
    l.first = {l.bar.right - pad - ib, y + pad, l.bar.right - pad, y + pad + ib};  // Cancel / Close
    if (gSim.done) {
        l.second = left(l.first, tw);  // Report
        l.third = left(l.second, ib);  // Restart
    } else {
        l.skip = left(l.first, ib);    // Skip to the end
        l.third = left(l.skip, ib);    // Restart
        l.plus = left(l.third, ib);    // +
        l.second = left(l.plus, tw);   // the speed
        l.minus = left(l.second, ib);  // -
    }
    draw::Rect firstButton = gSim.done ? l.third : l.minus;
    l.gear = left(firstButton, ib), firstButton = l.gear;  // (the settings: mining's workers and gas, a combat's alliances)
    l.feedback = left(firstButton, ib), firstButton = l.feedback;           // Report feedback
    l.chip = {x + pad, y + pad, x + pad + ib, y + pad + ib};  // the info icon
    l.text = {l.chip.right + scale(6), y, firstButton.left - scale(8), y + h};
    if (gSim.combat) return l;  // (a combat has no timeline: it goes on until no side can fight)
    const int inset = h / 2;
    l.track = {x + inset, l.bar.bottom - scale(4), l.bar.right - inset, l.bar.bottom - scale(2)};
    l.trackHit = {l.track.left - scale(6), l.bar.bottom - scale(9), l.track.right + scale(6), l.bar.bottom + scale(3)};
    return l;
}

// The speed as the bar shows it: 0.25x, 0.5x, 1x, 2x ...
std::wstring simSpeedText(float s) {
    std::wstring t = s == 0.25f ? L"0.25" : s == 0.5f ? L"0.5" : std::to_wstring(int(s));
    return t + L"\x00D7";
}

// The next speed down (-1) or up (+1).
void simStepSpeed(int dir) {
    const size_t n = std::size(kSimSpeeds);
    size_t at = 0;
    while (at + 1 < n && kSimSpeeds[at] < gSim.speed) ++at;
    if (dir < 0 && at > 0) --at;
    if (dir > 0 && at + 1 < n) ++at;
    gSim.speed = kSimSpeeds[at];
}

// Skip to the end: the rest of the game runs at once (no sounds), then the result and its report show.
void simSkipToEnd() {
    if (!gSim.shown || gSim.done || simSkipRunning()) return;
    gSim.skipping = true;  // (simFrame waits for the thread; at the end the report shows)
    simImages();           // (the picture to show meanwhile)
    gSimSkipFrame = simEngine().frame();
    gSimSkipEnded = false;
    simEngine().setFastForward(true);  // (no pictures or sounds while it skips: the play is the same)
    gSimSkipThread = std::thread([] {
        sim::Simulator& e = simEngine();
        while (!gSimSkipStop) {
            const bool more = e.step(240);
            gSimSkipFrame = e.frame();
            if (!more) {
                gSimSkipEnded = true;
                return;
            }
        }
    });
    simRedraw();
}

}  // namespace

bool simBarShown() { return gSim.shown; }

void simPaintBar(draw::Canvas& c, draw::Rect r) {
    const SimBarLayout l = simLayout(r);
    const float round = float((l.bar.bottom - l.bar.top) / 2);
    c.roundBox(l.bar, kPanel, kBorder, round);
    sim::Simulator& e = simEngine();
    // The info icon (its tooltip: an early preview).
    c.icon(l.chip, int(UiIcon::Info), gSim.hot == 3 ? kText : kDim, scale(16));
    // What runs and the game time; at the end the result.
    if (gSim.done) {
        c.text(l.text, gSim.result, kText, draw::kMiddle | draw::kEllipsis);
    } else {
        const std::wstring what = gSim.combat ? L"Combat" : L"Mining";
        const int frame = simSkipRunning() ? gSimSkipFrame.load() : e.frame();
        const std::wstring time = gSim.combat ? simClock(frame) : simClock(frame) + L" / " + simClock(e.lastFrame());
        const int ww = c.measure(what);
        c.text(l.text, what, kText, draw::kMiddle);
        const draw::Rect tr{l.text.left + ww + scale(8), l.text.top, l.text.right, l.text.bottom};
        c.text(tr, time, kDim, draw::kMiddle | draw::kEllipsis);
        if (gSim.skipping || gSim.behind) {  // a spinner: 8 dots round, one bright, turning
            const int cx = tr.left + c.measure(time) + scale(16), cy = (tr.top + tr.bottom) / 2, rad = scale(6), dot = std::max(2, scale(2));
            const int lead = int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 90 % 8);
            for (int k = 0; k < 8; ++k) {
                const float a = float(k) * 0.785398f;
                const int px = cx + int(std::lround(rad * std::cos(a))), py = cy + int(std::lround(rad * std::sin(a)));
                const int age = (lead - k + 8) % 8;
                c.fill({px - dot / 2, py - dot / 2, px - dot / 2 + dot, py - dot / 2 + dot}, age == 0 ? kAccent : kDim, uint8_t(age == 0 ? 255 : 230 - age * 25));
            }
            if (gSim.skipping) c.text({cx + rad + scale(8), tr.top, tr.right, tr.bottom}, L"Skip to the end", kDim, draw::kMiddle | draw::kEllipsis);
        }
    }
    // The timeline (mining), along the bottom edge.
    if (!gSim.combat) {
        const float rr = float((l.track.bottom - l.track.top) / 2);
        c.roundBox(l.track, kBorder, draw::kNone, rr);
        const int last = std::max(1, e.lastFrame());
        const int now = simSkipRunning() ? gSimSkipFrame.load() : e.frame();
        const int fill = l.track.left + int(int64_t(l.track.right - l.track.left) * std::min(now, last) / last);
        if (fill > l.track.left) c.roundBox({l.track.left, l.track.top, fill, l.track.bottom}, kAccent, draw::kNone, rr);
    }
    // The buttons: a fill under the mouse, darker while pressed (as the kit's bare buttons); Report is the primary.
    auto face = [&](int id, draw::Rect b, bool primary) {
        const float rr = float((b.bottom - b.top) / 2);
        const bool down = gSim.pressed == id && gSim.hot == id, over = gSim.hot == id && gSim.pressed < 0;
        if (primary) c.roundBox(b, over && !down ? kAccentBright : kAccent, draw::kNone, rr);
        else if (down) c.roundBox(b, kCtlDown, draw::kNone, rr);
        else if (over) c.roundBox(b, kCtlHot, draw::kNone, rr);
        return over || down;
    };
    auto iconButton = [&](int id, draw::Rect b, UiIcon icon) { c.icon(b, int(icon), face(id, b, false) ? kText : kDim, scale(16)); };
    auto textButton = [&](int id, draw::Rect b, const std::wstring& label, bool primary) {
        const bool lit = face(id, b, primary);
        c.text(b, label, primary ? draw::rgb(255, 255, 255) : lit ? kText : kDim, draw::kCenter | draw::kMiddle);
    };
    iconButton(2, l.third, UiIcon::ToStart);
    iconButton(8, l.feedback, UiIcon::Feedback);
    iconButton(7, l.gear, UiIcon::Settings);
    if (gSim.done) {
        textButton(1, l.second, L"Report", true);
    } else {
        textButton(4, l.minus, L"\x2212", false);
        c.text(l.second, simSpeedText(gSim.speed), kText, draw::kCenter | draw::kMiddle);
        textButton(5, l.plus, L"+", false);
        iconButton(6, l.skip, UiIcon::ToEnd);
    }
    iconButton(0, l.first, UiIcon::Close);
}

// The bar's Report feedback: the text box starts with short facts about the game (under a line, so the user writes
// above them): what runs, the game time and speed, the map, the units alive of each side, the result or an error.
void showFeedbackFilledSdl(const std::wstring& on, const std::wstring& text);  // (sdl_options.inc)
std::wstring simDiagnostics() {
    sim::Simulator& e = simEngine();
    const int frame = simSkipRunning() ? gSimSkipFrame.load() : e.frame();
    std::wstring t = L"\n\n---\n";
    t += gSim.combat ? L"Combat" : L"Mining";
    t += L", " + simClock(frame) + L" (frame " + std::to_wstring(frame) + L"), " + simSpeedText(gSim.speed);
    if (gSim.done) t += L", ended: " + gSim.result;
    if (simSkipRunning()) t += L", skipping";
    if (const Editor* ed = gState.ed.get())
        t += L"\nMap: " + std::to_wstring(ed->width()) + L" x " + std::to_wstring(ed->height()) + L", " + kTilesetNames[ed->era() & 7] +
             L", " + std::to_wstring(ed->units().size()) + L" units";
    if (!simSkipRunning()) {
        if (gSim.combat) {
            const sim::CombatResult r = e.combatResult();
            for (const sim::CombatSide& s : r.teams) {
                std::wstring players;
                for (int pl : s.players) players += (players.empty() ? L"" : L",") + std::to_wstring(pl + 1);
                t += L"\nPlayer " + players + L": " + std::to_wstring(s.alive) + L" of " + std::to_wstring(s.units) + L" alive";
                if (s.unreachable) t += L", " + std::to_wstring(s.unreachable) + L" out of reach";
            }
            if (r.stalemate) t += L"\nDraw";
        } else {
            int workers = 9, gas = 3;
            prefs::number(L"SimWorkers", workers, 1, 60), prefs::number(L"SimGasWorkers", gas, 0, 3);
            t += L"\n" + std::to_wstring(gSim.bases) + L" bases, " + std::to_wstring(workers) + L" workers, " + std::to_wstring(gas) + L" on gas";
        }
    }
    if (!simSkipRunning() && !e.liveError().empty()) t += L"\nError: " + widen(e.liveError());
    return t;
}

// The combat's settings (the bar's gear): who fights whom; a pick saves it and starts the fight again with the same
// units.
void simCombatSettingsMenu() {
    if (!gRoot || !gHost) return;
    const int now = simAlliances();
    std::vector<stardraft::kit::MenuItem> items;
    items.push_back(stardraft::kit::MenuItem{}), items.back().label = L"Who fights whom", items.back().enabled = false;
    items.push_back(stardraft::kit::MenuItem{}), items.back().separator = true;
    const std::pair<int, const wchar_t*> kinds[] = {{kComputersAllied, L"Computers together, each human alone"},
                                                    {kHumansVsComputers, L"Humans against computers"},
                                                    {kMapForces, L"The map's forces"},
                                                    {kFreeForAll, L"Free for all"},
                                                    {kUnitFreeForAll, L"Every unit against every other"}};
    for (const auto& [value, label] : kinds) {
        items.push_back(stardraft::kit::MenuItem{});
        items.back().label = label, items.back().checked = value == now;
        const int v = value;
        items.back().run = [v] {
            prefs::setNumber(L"SimAlliances", v);
            shellPost([] { simulateCombatOf(gSimCombatSelection); });  // (again, the same units)
        };
    }
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    stardraft::kit::Menu::popup(*gRoot, gHost->mouse(*gRoot), items, stardraft::kit::Menu::measure(items, &measure), {});
}

// The mining's settings (the bar's gear): the workers on minerals and on gas at each base, as Program options has them;
// a pick saves it and starts the mining again with it.
void simulateMiningCommand();
void simMiningSettingsMenu() {
    if (!gRoot || !gHost) return;
    int workers = 9, gas = 3;
    prefs::number(L"SimWorkers", workers, 1, 60), prefs::number(L"SimGasWorkers", gas, 0, 3);
    std::vector<stardraft::kit::MenuItem> items;
    auto header = [&](const std::wstring& t) {
        if (!items.empty()) items.push_back(stardraft::kit::MenuItem{}), items.back().separator = true;
        items.push_back(stardraft::kit::MenuItem{}), items.back().label = t, items.back().enabled = false;
    };
    auto choice = [&](const std::wstring& t, bool on, const wchar_t* key, int value) {
        items.push_back(stardraft::kit::MenuItem{});
        items.back().label = t, items.back().checked = on;
        items.back().run = [key, value] {
            prefs::setNumber(key, value);
            shellPost([] { simulateMiningCommand(); });  // (again with the new settings)
        };
    };
    header(L"Workers on minerals");
    for (int n : {4, 6, 8, 9, 12, 16, 18, 20, 24, 27, 30}) choice(std::to_wstring(n), n == workers, L"SimWorkers", n);
    header(L"Gas");
    choice(L"No gas", gas == 0, L"SimGasWorkers", 0);
    for (int n = 1; n <= 3; ++n) choice(std::to_wstring(n) + (n == 1 ? L" worker" : L" workers"), n == gas, L"SimGasWorkers", n);
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    stardraft::kit::Menu::popup(*gRoot, gHost->mouse(*gRoot), items, stardraft::kit::Menu::measure(items, &measure), {});
}

bool simBarMouse(const draw::MouseEvent& e, draw::Rect r) {
    const SimBarLayout l = simLayout(r);
    auto hit = [&](draw::Rect b) { return e.pt.x >= b.left && e.pt.x < b.right && e.pt.y >= b.top && e.pt.y < b.bottom; };
    // The timeline: a press or a drag goes to the time under the mouse (also when the drag leaves the bar).
    auto seekAt = [&] {
        const int w = std::max(1, int(l.track.right - l.track.left));
        const int x = std::clamp(int(e.pt.x - l.track.left), 0, w);
        simSkipJoin();
        simSeek(int(int64_t(simEngine().lastFrame()) * x / w));
    };
    if (gSim.seeking && !(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) gSim.seeking = false;  // (let go elsewhere)
    if (gSim.seeking) {
        if (e.kind == draw::MouseKind::Move) seekAt();
        if (e.kind == draw::MouseKind::Up && e.button == 0) gSim.seeking = false;
        return true;
    }
    const int over = e.kind == draw::MouseKind::Leave ? -1 : hit(l.first) ? 0 : (gSim.done && hit(l.second)) ? 1 : hit(l.third) ? 2 : hit(l.chip) ? 3
                     : hit(l.minus) ? 4 : hit(l.plus) ? 5 : hit(l.skip) ? 6 : hit(l.gear) ? 7 : hit(l.feedback) ? 8 : -1;
    if (over != gSim.hot) gSim.hot = over, simRedraw();
    if (e.kind == draw::MouseKind::Up && e.button == 0 && gSim.pressed >= 0) {
        const int was = gSim.pressed;
        gSim.pressed = -1;
        simRedraw();
        if (was != over || was == 3) return true;  // (let go elsewhere, or on the info icon: nothing)
        if (was == 2) simSeek(0);
        else if (was == 0) simClose();
        else if (was == 1) shellPost([] { simShowReport(); });
        else if (was == 4 || was == 5) simStepSpeed(was == 4 ? -1 : 1), simRedraw();
        else if (was == 6) simSkipToEnd();
        else if (was == 7) shellPost([] { gSim.combat ? simCombatSettingsMenu() : simMiningSettingsMenu(); });
        else if (was == 8) shellPost([] { showFeedbackFilledSdl(L"the simulation", simDiagnostics()); });
        return true;
    }
    const bool in = hit(l.bar);
    if (!in) return false;
    // (the bar takes the mouse; a press does what it does; a quick second click is a press too, not a double click)
    if ((e.kind != draw::MouseKind::Down && e.kind != draw::MouseKind::DoubleClick) || e.button != 0) return true;
    if (!gSim.combat && hit(l.trackHit)) {
        gSim.seeking = true;
        seekAt();
        return true;
    }
    if (over >= 0) gSim.pressed = over, simRedraw();
    return true;
}

// The info icon's tip (it shows at once: the icon is there for it; sdl_tips.inc).
const wchar_t* const kSimInfoTip = L"Experimental simulation, might not fully match the game yet.";
bool simTipIsQuick(const std::wstring& tip) { return tip == kSimInfoTip; }

// The bar's tooltips: the info icon (what the simulation is) and the buttons.
std::wstring simBarTip(draw::Point pt, draw::Rect r) {
    if (!gSim.shown) return {};
    const SimBarLayout l = simLayout(r);
    auto in = [&](draw::Rect b) { return pt.x >= b.left && pt.x < b.right && pt.y >= b.top && pt.y < b.bottom; };
    if (in(l.chip)) return kSimInfoTip;
    if (in(l.third)) return L"Restart";
    if (in(l.minus)) return L"Slower";
    if (in(l.plus)) return L"Faster";
    if (in(l.skip)) return L"Skip to the end";
    if (in(l.gear)) return gSim.combat ? L"Who fights whom" : L"Workers and gas";
    if (in(l.feedback)) return L"Report feedback on the simulation";
    if (in(l.second)) return gSim.done ? L"Show the report" : L"Speed";
    if (in(l.first)) return gSim.done ? L"Close" : L"Cancel the simulation";
    return {};
}

bool simKey(const draw::KeyEvent& e) {
    if (!gSim.shown || !e.down || e.key != draw::Key::Escape) return false;
    simClose();
    return true;
}

void simFrame() {
    if (!gSim.shown) return;
    if (gState.ed.get() != gSim.ed) return simClose();  // (another tab, or the map closed)
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - gSim.last).count();
    gSim.last = now;
    simHold();  // (the shields follow the panels: a window resized, a panel opened)
    if (gSim.done) return;
    // The game's frames due (Fastest: 42 ms a frame, times the speed), run in small steps for at most 12 ms of each
    // window frame, so the program stays usable at any speed: the frames that do not fit are dropped (the game goes
    // slower than the speed, and the bar's spinner says so). Skip to the end: as many as fit, until the end.
    using clock = std::chrono::steady_clock;
    const auto budget = now + std::chrono::milliseconds(12);
    sim::Simulator& e = simEngine();
    bool ran = false, ended = false;
    if (gSim.skipping) {
        if (gSimSkipEnded) ended = simSkipJoin(), simSounds(false);
    } else {
        gSim.due += std::min(ms, 250.0) * gSim.speed / 42.0;
        while (gSim.due >= 1 && !ended && clock::now() < budget) {
            const int n = std::min(int(gSim.due), 8);
            gSim.due -= n, ran = true;
            ended = !e.step(n);
        }
        gSim.behind = gSim.due >= 8;
        gSim.due = std::min(gSim.due, 1.0);  // (what did not fit is dropped: no backlog)
        if (ran) simSounds(true);
    }
    if (ended) {
        const bool skipped = gSim.skipping;
        gSim.skipping = gSim.behind = false;
        simEnd();
        if (skipped) shellPost([] { simShowReport(); });
    }
    simRedraw();  // (the next frame: the game goes on)
}

// Tools > Simulate mining: at each start location, placed resource depot and group of mineral fields (at most 8), the
// workers of Program options > Simulation (default 9 on minerals and 3 on gas), 30 seconds, then the measured game
// minutes (default 3); the races as in the map (a random race for Random and a race picked in the lobby) or one race.
// At the game's speed (the bar's - and + change it).
void simulateMiningCommand() {
    int workers = 9, gas = 3, minutes = 3, races = 0;
    prefs::number(L"SimWorkers", workers, 1, 60), prefs::number(L"SimGasWorkers", gas, 0, 3);
    prefs::number(L"SimMinutes", minutes, 1, 60), prefs::number(L"SimRaces", races, 0, 3);
    sim::MiningOptions o;
    o.workers = {workers};
    o.gas = gas > 0, o.gasWorkers = gas;
    o.frames = int(minutes * 60 * 1000 / 42);
    static const sim::Race kRace[4] = {sim::Terran, sim::Terran, sim::Protoss, sim::Zerg};
    o.races = {kRace[races]};
    o.mapRaces = races == 0;
    o.seed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());  // (Random: another race each time)
    if (!simBegin(false, [o](std::string& err) { return simEngine().startMining(o, err); })) return;
    gState.ed->clearSelection();  // (the map's selection does not show over the game)
    gSim.speed = 1, gSim.workers = workers, gSim.bases = int(simEngine().miningResult().size());
}

// Tools > Simulate combat of the selected units: the selected units of all players fight where they stand (each unit
// attacks the enemies it can reach until none are left: search and destroy), at most 10 game minutes. The computer
// players are one team (allies); each other player fights alone, against the computers and the other players. No unit
// of the map goes: the selected buildings are targets (only the static defense attacks), and the units that are not
// selected, resources, invincible units and the rescuable and neutral players' units stay where they are (they block
// the way; they are not attacked).
void simulateCombatCommand() {
    if (!gState.ed || !gState.ed->isOpen()) return;
    simulateCombatOf(std::set<int>(gState.ed->selection().begin(), gState.ed->selection().end()));
}
void simulateCombatOf(std::set<int> selected) {
    if (!gState.ed || !gState.ed->isOpen()) return;
    const Editor& ed = *gState.ed;
    std::vector<int> owners;
    sim::CombatOptions o;
    o.byPlayer = true;
    o.unitFreeForAll = simAlliances() == kUnitFreeForAll;
    o.frames = 10 * 60 * 1000 / 42;  // (a safety cap: the fight ends when no team can reach an enemy)
    const int alliances = simAlliances();
    std::set<int> teams;
    for (size_t i = 0; i < ed.units().size(); ++i) {
        const Unit& u = ed.units()[i];
        // (Start Locations and map revealers are not units in the game)
        if (u.type >= ed.unitTypes().size() || u.type == 214 || u.type == kMapRevealerUnit) continue;
        const Editor::Controller who = u.owner < 8 ? ed.player(u.owner).controller : Editor::Controller::Neutral;
        const bool fights = selected.count(int(i)) && u.owner < 8 && who != Editor::Controller::Rescue && who != Editor::Controller::Neutral &&
                            !ed.unitTypes()[u.type].is(UnitFlag::ResourceContainer | UnitFlag::Invincible);
        sim::PlacedUnit p{int(u.type), int(u.owner), int(u.x), int(u.y)};
        p.passive = !fights;
        o.placed.push_back(p);
        if (!fights) continue;
        if (std::find(owners.begin(), owners.end(), int(u.owner)) == owners.end()) owners.push_back(u.owner);
        o.team[u.owner] = simTeamOf(ed, u.owner, alliances);
        teams.insert(o.team[u.owner]);
        if (o.unitFreeForAll) teams.insert(1000 + int(i));  // (each unit its own side)
    }
    if (teams.size() < 2 || (o.unitFreeForAll && o.placed.size() < 2)) {
        if (gRoot)
            msgbox::ask(*gRoot, L"Simulate combat",
                        L"Select the units of two players that are enemies (in Units mode: drag a box, or Ctrl+click), then try again. The bar's settings say who are allies.",
                        {L"OK"}, msgbox::Icon::Info);
        return;
    }
    std::sort(owners.begin(), owners.end());
    if (owners.size() < 2) owners.push_back(owners.front());  // (every unit against every other: one player's units)
    o.players[0] = owners[0], o.players[1] = owners[1];
    std::map<int, std::wstring> names;
    for (const sim::PlacedUnit& p : o.placed) names[p.unitType] = widen(ed.unitTypes().statText(uint32_t(p.unitType)));
    if (!simBegin(true, [o](std::string& err) { return simEngine().startCombat(o, err); })) return;
    gSimCombatSelection = std::move(selected);
    gSimCombatPlaced.clear();
    for (const sim::PlacedUnit& pu : o.placed) gSimCombatPlaced.push_back({pu.unitType, pu.side});
    gState.ed->clearSelection();  // (the map's selection does not show over the fight)
    gSim.players[0] = owners[0], gSim.players[1] = owners[1], gSim.unitNames = names, gSim.speed = 1;
}

// --dialog simreport:mining or simreport:combat (a snapshot): that simulation of the map runs to its end at once (combat:
// the units of the map's first two players), and its report's window is the picture.
draw::View* sdlSimReportShot(const std::string& arg, int& w, int& h) {
    if (arg != "simreport:mining" && arg != "simreport:combat") return nullptr;
    changelog::setHtmlHooks(false);
    if (arg == "simreport:combat" && gState.ed && gState.ed->isOpen()) {  // 12 Marines and 4 Medics (Player 1), 6 Zealots and 4 Dragoons (Player 2)
        sim::CombatOptions o;
        o.a = {{0, 12}, {34, 4}}, o.b = {{65, 6}, {66, 4}};
        std::map<int, std::wstring> names;
        for (int t : {0, 34, 65, 66}) names[t] = widen(gState.ed->unitTypes().statText(uint32_t(t)));
        if (simBegin(true, [o](std::string& err) { return simEngine().startCombat(o, err); }))
            gSim.players[0] = 0, gSim.players[1] = 1, gSim.unitNames = names;
    } else {
        simulateMiningCommand();
    }
    if (gSim.shown) {
        for (int n = 0; n < 100000 && !gSim.done; ++n)
            if (!simEngine().step(60)) simEnd();
    }
    std::printf("simreport: %s, %s\n", gSim.done ? "ended" : "not run (WRONG)", narrow(gSim.endText).c_str());
    const draw::Point sz = SimReportView::size();
    w = sz.x, h = sz.y;
    gSimReport.setFrame({0, 0, w, h});
    gSimReport.set(gSimTitle, gSimLine, gSimDoc);
    return &gSimReport;
}

#else  // (no simulation in this build)

bool simBarShown() { return false; }
void simPaintBar(draw::Canvas&, draw::Rect) {}
bool simBarMouse(const draw::MouseEvent&, draw::Rect) { return false; }
std::wstring simBarTip(draw::Point, draw::Rect) { return {}; }
bool simTipIsQuick(const std::wstring&) { return false; }
bool simKey(const draw::KeyEvent&) { return false; }
void simFrame() {}
draw::View* sdlSimReportShot(const std::string&, int&, int&) { return nullptr; }

#endif
