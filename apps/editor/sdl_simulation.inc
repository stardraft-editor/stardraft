// Experimental (branch experimental-simulation, STARDRAFT_SIMULATION): Tools > Simulate mining, Simulate rush distance
// and Simulate combat of the selected units. The simulation bar's engine (tools/map_lab/bar_sim.h: mining and rush
// distance on the Map Simulation engine, combat on StarDraft's own engine, tools/sim/sim.h) runs a game of the open map
// (its unsaved edits too) live on the map: its units move, mine and fight in place of the map's units, at the game's speed (Fastest) times the
// bar's speed. The bar at the top of the map says what runs; Cancel (or Esc) ends it and the map shows again; at the
// end the bar has the result, Report the whole report and Close.

#ifdef STARDRAFT_SIMULATION

#include "../../tools/sim/sim.h"
#include "../../tools/sim/sim_data.h"
#include "../../tools/map_lab/bar_sim.h"

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
float mapTextScale();  // (core/map_scene.inc)

namespace {

// The simulator (made when a simulation first starts). Only used once simReady() is true.
std::unique_ptr<sim::Simulator> gSim_;
sim::Simulator& simEngine() { return *gSim_; }
bool gSimReady = false;

struct LiveSim {
    bool shown = false;  // the bar shows: a game runs, or ended and shows its end
    bool playing = false;  // the game runs (Play); a new game waits at its start until Play (Space)
    bool combat = false, done = false;
    bool rush = false;  // rush distance (combat false)
    const Editor* ed = nullptr;  // the map it runs on (another tab, or the map closed: it ends)
    float speed = 1;             // the game's speed times this (kSimSpeeds)
    bool skipping = false;       // the game runs on the skip's thread, as fast as it can (simRunAhead)
    bool toEnd = false;          // ... to its end (Skip to the end); else to a frame (Restart, the timeline)
    bool behind = false;         // the game cannot keep up with the speed (the bar shows the spinner)
    std::chrono::steady_clock::time_point last;
    double due = 0;  // frames due (parts of a frame)
    int players[2] = {0, 1};
    std::map<int, std::wstring> unitNames;
    int bases = 0, workers = 0;
    bool perField = false;  // mining: `workers` for each mineral field (else for each base)
    int boostFailed = 0;    // mining: the boost searches that could not start (a bit for each kind)
    uint32_t seed = 0;  // mining: the random races' seed (a new game time keeps it)
    int fog = -1;  // rush distance: whose vision the map shows (the fog of war; the gear's Fog of war): -1 none,
                   // sim::kAllPlayers all, kFogPlayers the players of fogPlayers
    int fogPlayers = 0;  // rush distance's fog of war, Specific players: a bit for each player (0 to 11) whose vision shows
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

// A new simulator for each game, with the data of this map (simDataFiles). `classic`: StarDraft's own engine for all
// (the mining of a Cosmonarchy BW map: the Map Simulation engine reads only the game's units).
bool simReady(bool classic) {
    std::string err;
    gSim_ = classic ? sim::createSimulator() : maplab::createBarSimulator();
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
        if (gRoot) {
            wchar_t text[1024];
            std::swprintf(text, 1024, tr(L"The simulation cannot start: %ls"), widen(err).c_str());
            msgbox::ask(*gRoot, tr(L"Simulation"), text, {tr(L"OK")}, msgbox::Icon::Error);
        }
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
std::atomic<bool> gSimSkipStop{false}, gSimSkipEnded{false}, gSimSkipReached{false};  // (reached: the frame of a seek)
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
    gSimSkipStop = false, gSimSkipReached = false;
    return gSimSkipEnded.exchange(false);
}

// The live game's images as the last frame before a skip had them (the engine is the skip's thread's meanwhile).
std::vector<sim::LiveImage> gSimLastImages;
const std::vector<sim::LiveImage>& simImages() {
    if (!simSkipRunning()) gSimLastImages.clear(), simEngine().images(gSimLastImages);
    return gSimLastImages;
}

// Rush distance's unit (the gear's Unit; the setting SimRushUnit: a units.dat number, -1 the worker of the race, the
// default). Any unit that can walk or fly: not a building, a turret, a resource or a unit that cannot be hit (a beacon,
// a start location, Map Revealer).
bool simRushUnitFits(int type) {
    if (type == -1) return true;
    if (!gState.ed || type < 0 || type >= int(gState.ed->unitTypes().size())) return false;
    const uint32_t no = UnitFlag::Building | UnitFlag::Subunit | UnitFlag::ResourceContainer | UnitFlag::Invincible;
    return !(gState.ed->unitTypes()[uint16_t(type)].flags & no);
}
int simRushUnit() {
    int u = -1;
    prefs::number(L"SimRushUnit", u, -1, 227);
    return simRushUnitFits(u) ? u : -1;
}
// The gear's Unit submenu: each race's units that its players train (as units.dat numbers them), after the worker.
struct RushRaceUnits {
    const wchar_t* race;
    std::vector<int> units;
};
const RushRaceUnits kRushRaceUnits[] = {
    {L"Terran", {0, 32, 34, 1, 2, 3, 5, 8, 11, 9, 58, 12}},       // Marine, Firebat, Medic, Ghost, Vulture, Goliath, Siege Tank, Wraith, Dropship, Science Vessel, Valkyrie, Battlecruiser
    {L"Protoss", {65, 66, 67, 61, 68, 63, 83, 69, 84, 70, 60, 71, 72}},  // Zealot, Dragoon, High and Dark Templar, Archon, Dark Archon, Reaver, Shuttle, Observer, Scout, Corsair, Arbiter, Carrier
    {L"Zerg", {37, 38, 103, 39, 46, 42, 43, 47, 44, 62, 45}},       // Zergling, Hydralisk, Lurker, Ultralisk, Defiler, Overlord, Mutalisk, Scourge, Guardian, Devourer, Queen
};
// Rush distance between two points (the gear's Start and end: Two points of the map): the
// points (map pixels) on the map that they were picked on, kept while the rush starts again; on: the rush uses them.
struct RushPoints {
    bool on = false;
    const Editor* ed = nullptr;
    std::pair<int, int> from, to;
} gRushPoints;
int gRushDrag = 0;  // the point that a drag on the map moves: 1 A (the start point), 2 B (the end point); 0 none
std::pair<float, float> gRushDragGrip;  // the press's place less the point's (window pixels): the point keeps it to the mouse
std::pair<int, int> gRushDragFrom;     // the point where the drag started (a release there: no new rush)
// The two points at their first use on a map: A at the first start location and B at the next (by player), else a
// quarter of the map's width left and right of its middle.
RushPoints simRushDefaultPoints() {
    RushPoints p;
    if (!gState.ed) return p;
    const Editor& ed = *gState.ed;
    std::vector<std::pair<int, std::pair<int, int>>> starts;  // (player, place)
    for (const Unit& u : ed.units())
        if (u.type == 214) starts.push_back({u.owner, {u.x, u.y}});
    std::sort(starts.begin(), starts.end());
    const int w = ed.width() * 32, h = ed.height() * 32;
    p.on = true, p.ed = &ed;
    p.from = starts.size() >= 2 ? starts[0].second : std::pair<int, int>{w / 4, h / 2};
    p.to = starts.size() >= 2 ? starts[1].second : std::pair<int, int>{w * 3 / 4, h / 2};
    return p;
}
// The point (1 A, 2 B) under the window point (x, y), or 0: the markers' boxes (simTrails) and a little room.
constexpr float kRushPinHalf = 9, kRushPinHeight = 20;  // the pins' half width at the top, and their height (screen pixels)
// The last two points on each map's editor (the rush from the start locations does not change them).
RushPoints gRushLastPoints;
int simRushMarkerAt(int x, int y) {
    if (!gSim.shown || !gSim.rush || !gRushPoints.on || gRushPoints.ed != gState.ed.get()) return 0;
    const float zoom = std::max(0.01f, gState.zoom), u = float(scale(100)) / 100;
    for (int n : {2, 1}) {  // (B over A, as drawn): the triangle and a little room
        const std::pair<int, int>& at = n == 1 ? gRushPoints.from : gRushPoints.to;
        const float sx = (float(at.first) - gState.camX) * zoom, sy = (float(at.second) - gState.camY) * zoom;
        if (std::abs(float(x) - sx) <= (kRushPinHalf + 4) * u && float(y) >= sy - (kRushPinHeight + 4) * u && float(y) <= sy + 4 * u) return n;
    }
    return 0;
}
std::wstring simUnitName(int type);  // (below: the unit's name in the map)

// Rush distance: each worker's way so far (a place each 8 pixels), a line in its player's color (a copy, kept while a
// skip runs).
std::vector<sim::RushRoute> gSimRoutes;
void simTrails(render::Scene&) {
    if (!simSkipRunning()) gSimRoutes = simEngine().rushResult();  // (drawn over the map by simPaintRush)
}
// The ways and the two points over the map, smoothed (antialiased) as the UI's icons, on the map view's canvas (the
// map's renderer draws no smooth lines): each way a 2 pixel line in its player's color with a dark edge under it; A
// and B as pins, green (the start) and red (the end): a triangle with a flat top, its tip on the point, a white rim
// and a dark edge. In window pixels, as large at any zoom (a held pin a little larger).
bool simFogShows(int player);  // (below: the ways that the fog of war in use shows)
void simPaintRush(draw::Canvas& c, draw::Rect view) {
    if (!gSim.rush || !gState.ed) return;
    using P = draw::Canvas::PointF;
    const float zoom = std::max(0.01f, gState.zoom), u = float(scale(100)) / 100;
    auto at = [&](std::pair<int, int> w) { return P{(float(w.first) - gState.camX) * zoom, (float(w.second) - gState.camY) * zoom}; };
    auto color = [](uint32_t rgba) { return draw::rgb(int(rgba >> 24), int(rgba >> 16 & 255), int(rgba >> 8 & 255)); };
    for (const sim::RushRoute& r : gSimRoutes) {
        if (gRushDrag) break;  // (no ways while a point moves)
        if (r.path.size() < 2 || !simFogShows(r.from)) continue;
        // (a start location's route: its player's color; the routes between two points: each unit its own)
        const draw::Color line = color(playerRgba(r.from >= 0 ? std::clamp(r.from, 0, 11) : int(&r - gSimRoutes.data()) % 12, 255));
        // The way's points in the window, at least 2 pixels apart (zoomed out, the path's places each 8 map pixels are
        // closer), and only the parts in the view: a large map's 56 ways stay a few thousand lines.
        std::vector<P> pts;
        pts.reserve(r.path.size());
        for (size_t k = 0; k < r.path.size(); ++k) {
            const P q = at(r.path[k]);
            if (!pts.empty() && k + 1 < r.path.size() && std::abs(q.x - pts.back().x) < 2 * u && std::abs(q.y - pts.back().y) < 2 * u) continue;
            pts.push_back(q);
        }
        const float m = 4 * u;  // (the edge's width)
        auto shown = [&](P a, P b) {
            return std::max(a.x, b.x) >= float(view.left) - m && std::min(a.x, b.x) <= float(view.right) + m &&
                   std::max(a.y, b.y) >= float(view.top) - m && std::min(a.y, b.y) <= float(view.bottom) + m;
        };
        for (int pass = 0; pass < 2; ++pass)  // (the dark edge first, then the color over it)
            for (size_t k = 1; k < pts.size(); ++k)
                if (shown(pts[k - 1], pts[k])) c.smoothLine(pts[k - 1], pts[k], pass ? line : draw::rgb(16, 16, 20), (pass ? 2.0f : 4.0f) * u, pass ? 255 : 110);
    }
    if (!gRushPoints.on || gRushPoints.ed != gState.ed.get()) return;
    auto pin = [&](std::pair<int, int> point, draw::Color fill, bool held) {
        const P tip = at(point);
        const float g = held ? 1.15f : 1.0f, half = kRushPinHalf * u * g, height = kRushPinHeight * u * g;
        auto tri = [&](float grow, float dy) {
            return std::array<P, 3>{P{tip.x - half - grow * 1.6f, tip.y - height - grow + dy}, P{tip.x + half + grow * 1.6f, tip.y - height - grow + dy},
                                    P{tip.x, tip.y + grow * 1.8f + dy}};
        };
        const auto edge = tri(3 * u, 1 * u), rim = tri(1.6f * u, 0), body = tri(0, 0);
        c.smoothPolygon(edge.data(), 3, draw::rgb(16, 16, 20));
        c.smoothPolygon(rim.data(), 3, draw::rgb(255, 255, 255));
        c.smoothPolygon(body.data(), 3, fill);
    };
    pin(gRushPoints.from, draw::rgb(52, 199, 89), gRushDrag == 1);
    pin(gRushPoints.to, draw::rgb(255, 69, 58), gRushDrag == 2);
}

// Mining: at each base's mineral fields, two lines over the field: its mean round trip in frames (after the warm-up;
// none before its first trip) and its minerals left (the map's amount less those mined). Zoomed out (the two lines
// do not fit on a field): one label under each base's depot, its fields' mean round trip and their minerals left. A
// copy, kept while a skip runs. The map font, as the map's other labels (core/path_overlay.inc).
std::vector<sim::MiningBase> gSimFields;
void simFieldLabels(render::Scene& s) {
    if (!simSkipRunning()) gSimFields = simEngine().miningResult();
    const float unit = mapTextScale(), px = unit / std::max(0.05f, gState.zoom);  // a font unit in map pixels
    // (a field is 64 x 32 map pixels; the widest line, "1500 minerals", is about 60 font units)
    const bool perField = 60 * px <= 62 && 24 * px <= 32;
    auto lines = [&](float cx, float top, const char* first, const char* second) {
        int line = 0;
        for (const char* t : {first, second}) {
            if (!*t) continue;
            const float w = float(render::textWidth(t)) * px, y = top + float(line) * 12 * px;
            s.texts.push_back({cx - w / 2, y, t, t == first ? 0xFFFFFFFFu : 0xD8E4F0FFu, true, unit});
            ++line;
        }
    };
    std::set<std::pair<int, int>> labeled;  // the fields with a label
    for (const sim::MiningBase& b : gSimFields) {
        if (b.runs.empty() || !b.runs[0].placed) continue;
        const sim::MiningRun& r = b.runs[0];
        char cycle[48] = "", left[48] = "";
        if (!perField) {
            double sum = 0;
            int timed = 0, minerals = 0;
            for (double t : r.tripFrames) sum += t, timed += t > 0 ? 1 : 0;
            for (int n : r.fieldMinerals) minerals += n;
            if (timed) std::snprintf(cycle, sizeof cycle, "%.0f frames a trip", sum / timed);
            if (!r.fieldMinerals.empty()) std::snprintf(left, sizeof left, "%d minerals", minerals);
            lines(float(b.x), float(b.y) + 56, cycle, left);
            continue;
        }
        for (size_t f = 0; f < b.fields.size(); ++f) {
            if (!labeled.insert(b.fields[f]).second) continue;  // (a field near two depots: the first base's label)
            cycle[0] = left[0] = 0;
            if (f < r.tripFrames.size() && r.tripFrames[f] > 0) std::snprintf(cycle, sizeof cycle, "%.0f frames", r.tripFrames[f]);
            if (f < r.fieldMinerals.size()) std::snprintf(left, sizeof left, "%d minerals", r.fieldMinerals[f]);
            lines(float(b.fields[f].first), float(b.fields[f].second) - 12 * px, cycle, left);
        }
    }
}

// The boost searches' guides on the map (the pull request's guide drawing, tools/map_lab/guide_geometry.h there, MIT):
// a ring where a worker starts (position), a turn-back mark where a worker returns its cargo (cargo), and each cycle's
// two commands as numbered arrows, in the worker's color (cycle); the part of the first command's way that the second
// command cuts off is dotted. Each line has a dark line under it. A copy, kept while a skip runs.
std::vector<sim::MiningGuide> gSimGuides;
void simGuides(render::Scene& s) {
    if (!simSkipRunning()) gSimGuides = simEngine().miningGuides();
    const float zoom = std::max(0.01f, gState.zoom), k = 1 / zoom;
    constexpr uint32_t kGold = 0xFFDA65FFu, kUnder = 0x101014F0u;
    auto line = [&](float ax, float ay, float bx, float by, uint32_t color) {
        render::RectDraw d;
        d.x0 = ax, d.y0 = ay, d.x1 = bx, d.y1 = by, d.line = true;
        d.thickness = 4, d.rgba = kUnder;
        s.rects.push_back(d);
        d.thickness = 2, d.rgba = color;
        s.rects.push_back(d);
    };
    auto colorOf = [](int worker) {
        constexpr uint32_t colors[] = {0xFFB54CFFu, 0xFFEF75FFu, 0xFF91D0FFu, 0xC7F56BFFu, 0xFFA88BFFu, 0xE5ABFFFFu};
        return colors[unsigned(worker) % 6];
    };
    for (const sim::MiningGuide& g : gSimGuides) {
        const float x = float(g.x), y = float(g.y);
        const uint32_t color = colorOf(g.worker);
        if (g.kind == 0) {
            render::RectDraw ring;
            ring.x0 = x - 10 * k, ring.y0 = y - 10 * k, ring.x1 = x + 10 * k, ring.y1 = y + 10 * k;
            ring.rgba = kUnder, ring.thickness = 4;
            s.rects.push_back(ring);
            ring.rgba = kGold, ring.thickness = 2;
            s.rects.push_back(ring);
        } else if (g.kind == 1) {  // (the map's font has no arrow: the turn-back mark as lines)
            line(x + 7 * k, y - 19 * k, x + 7 * k, y - 13 * k, kGold);
            line(x + 7 * k, y - 13 * k, x - 6 * k, y - 13 * k, kGold);
            line(x - 6 * k, y - 13 * k, x - 2 * k, y - 17 * k, kGold);
            line(x - 6 * k, y - 13 * k, x - 2 * k, y - 9 * k, kGold);
        } else if (g.kind == 2) {
            float endX = float(g.targetX), endY = float(g.targetY);
            if (g.stage == 1) {
                const auto second = std::find_if(gSimGuides.begin(), gSimGuides.end(),
                                                 [&](const sim::MiningGuide& o) { return o.kind == 2 && o.stage == 2 && o.worker == g.worker; });
                if (second != gSimGuides.end()) {  // (the second command starts here: the rest of the first way dotted)
                    endX = float(second->x), endY = float(second->y);
                    const float dx = float(g.targetX) - endX, dy = float(g.targetY) - endY, length = std::hypot(dx, dy);
                    if (length * zoom > 1) {
                        const float ux = dx / length, uy = dy / length, stop = std::max(0.f, length - 9 * k);
                        for (float d = 3 * k; d < stop; d += 6 * k) {
                            const float tail = std::min(d + 1.5f * k, stop);
                            line(endX + ux * d, endY + uy * d, endX + ux * tail, endY + uy * tail, color);
                        }
                    }
                }
            }
            const float dx = endX - x, dy = endY - y, length = std::hypot(dx, dy);
            if (length * zoom > 1) {
                const float ux = dx / length, uy = dy / length, inset = std::min(9 * k, length * .25f);
                const float tx = endX - ux * inset, ty = endY - uy * inset;
                line(x, y, tx, ty, color);
                line(tx, ty, tx - ux * 7 * k - uy * 4 * k, ty - uy * 7 * k + ux * 4 * k, color);
                line(tx, ty, tx - ux * 7 * k + uy * 4 * k, ty - uy * 7 * k - ux * 4 * k, color);
            }
            s.texts.push_back({x, y - 14 * k, std::to_string(g.stage), color, true, mapTextScale()});
        }
    }
}

// The fog of war of rush distance (the gear's Fog of war): the map as the players' scouts see it, as the game draws it:
// black where they have not been, darker where they saw before, clear where they see now: off, what all players see, or
// what the checked players see (each tile: the most that one of them sees). None before the game plays (at 0:00 and
// waiting for Play: the whole map). A copy, kept while a skip runs.
constexpr int kFogPlayers = -3;  // (LiveSim::fog: the players of LiveSim::fogPlayers)
// Whether the way of `player` (a start location's) shows with the fog of war in use.
bool simFogShows(int player) {
    return gSim.fog == -1 || gSim.fog == sim::kAllPlayers || (gSim.fog == kFogPlayers && player >= 0 && player < 12 && (gSim.fogPlayers >> player & 1));
}
std::wstring simPlayer(int player);  // (below: "Player 1")
std::vector<uint8_t> gSimFog;
int gSimFogW = 0, gSimFogH = 0;
void simFog(render::Scene& s) {
    if (!simSkipRunning()) {
        if (gSim.fog != kFogPlayers) {
            if (!simEngine().vision(gSim.fog, gSimFogW, gSimFogH, gSimFog)) gSimFog.clear();
        } else {  // (the checked players together: each tile the most one of them sees)
            gSimFog.clear();
            std::vector<uint8_t> one;
            int w = 0, h = 0;
            for (int pl = 0; pl < 12; ++pl)
                if ((gSim.fogPlayers >> pl & 1) && simEngine().vision(pl, w, h, one)) {
                    if (gSimFog.empty()) gSimFog = one, gSimFogW = w, gSimFogH = h;
                    else if (one.size() == gSimFog.size())
                        for (size_t i = 0; i < one.size(); ++i) gSimFog[i] = std::max(gSimFog[i], one[i]);
                }
        }
    }
    if (gSimFog.size() != size_t(gSimFogW) * size_t(gSimFogH)) return;
    for (int y = 0; y < gSimFogH; ++y)
        for (int x = 0; x < gSimFogW;) {  // (a row's tiles of the same state: one rectangle)
            const uint8_t t = gSimFog[size_t(y) * size_t(gSimFogW) + size_t(x)];
            int end = x + 1;
            while (end < gSimFogW && gSimFog[size_t(y) * size_t(gSimFogW) + size_t(end)] == t) ++end;
            if (t != 2) {
                render::RectDraw d;
                d.x0 = float(x * 32), d.y0 = float(y * 32), d.x1 = float(end * 32), d.y1 = float(y * 32 + 32);
                d.fill = true, d.rgba = t == 0 ? 0x000000FFu : 0x000000A0u;
                s.rects.push_back(d);
            }
            x = end;
        }
}
// The players whose scouts walk (the rush's start locations), in their order.
std::vector<int> simFogPlayers() {
    std::vector<int> out;
    for (const sim::RushRoute& r : simSkipRunning() ? gSimRoutes : simEngine().rushResult())
        if (r.from >= 0 && std::find(out.begin(), out.end(), r.from) == out.end()) out.push_back(r.from);
    std::sort(out.begin(), out.end());
    return out;
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
        // The image's draw function: images.dat's (LiveImage does not give the one the game has now, as a unit's cloak).
        d.drawFunction = sp.drawFunction(li.image);
        d.shadow = d.drawFunction == 10;
        d.glow = d.drawFunction == 9;
        // A sprite's main image with what its script puts under and over it (the shadow, lights), as the map draws its
        // own units (core/map_scene.inc: addImage); the other images of the sprite (a turret) as they come.
        if (li.order == 0 && !d.shadow) addImage(s, d);
        else s.sprites.push_back(d);
    }
    if (gSim.rush && gSim.fog != -1 && (gSim.playing || (simSkipRunning() ? gSimSkipFrame.load() : simEngine().frame()) > 0)) simFog(s);  // (before Play: the whole map)
    if (gSim.rush) simTrails(s);
    else if (!gSim.combat) simGuides(s), simFieldLabels(s);
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
        td = kitpaint::titleFontDesc(), td.weight = 600;
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

// The report's texts. Each is a whole sentence or label, a printf template passed through tr (so that a translation can
// put its values in its own order); the values that are data (unit names, force names, players' numbers) are passed in.
std::wstring simOf(int part, int all) {
    wchar_t b[64];
    std::swprintf(b, 64, tr(L"%d of %d"), part, all);
    return b;
}
std::wstring simUnitName(int type) {
    const auto n = gSim.unitNames.find(type);
    if (n != gSim.unitNames.end()) return n->second;
    if (gState.ed && type >= 0 && type < 228) return widen(gState.ed->unitTypes().statText(uint32_t(type)));  // (the map's name of it)
    wchar_t b[64];
    std::swprintf(b, 64, tr(L"Unit %d"), type);
    return b;
}
// The name of a player (0-based index in the engine, shown from 1).
std::wstring simPlayer(int player) {
    wchar_t b[64];
    std::swprintf(b, 64, tr(L"Player %d"), player + 1);
    return b;
}
std::wstring simWins(const std::wstring& who) {
    wchar_t b[1024];
    std::swprintf(b, 1024, tr(L"%ls wins"), who.c_str());
    return b;
}
// The result and its time: "<result> after <seconds> game seconds".
std::wstring simAfter(const std::wstring& result, const std::wstring& secs) {
    wchar_t b[1024];
    std::swprintf(b, 1024, tr(L"%ls after %ls game seconds"), result.c_str(), secs.c_str());
    return b;
}

// The end: the result in the bar, the report for Report.
void simEnd() {
    sim::Simulator& e = simEngine();
    gSim.done = true;
    gSimDoc = SimDoc{};
    if (!e.liveError().empty()) {
        const std::wstring err = widen(e.liveError());
        wchar_t b[1024];
        std::swprintf(b, 1024, tr(L"The simulation stopped: %ls"), err.c_str());
        gSim.endText = b;
        gSimTitle = tr(L"The simulation stopped");
        gSimLine = err;
        gSimDoc.para(gSim.endText);
        gSim.result = tr(L"Stopped");
        gSim.report = gSim.endText;
        if (gSimReport.open) gSimReport.set(gSimTitle, gSimLine, gSimDoc);
        return simRedraw();
    }
    if (gSim.combat && !e.combatResult().teams.empty()) {
        const sim::CombatResult r = e.combatResult();
        auto teamName = [](const sim::CombatSide& t) -> std::wstring {
            std::wstring n;
            for (int p : t.players) n += (n.empty() ? L"" : L", ") + std::to_wstring(p + 1);
            const bool many = t.players.size() > 1;
            wchar_t b[256];
            if (t.team == kTeamComputers) {
                if (many) std::swprintf(b, 256, tr(L"Computers (Players %ls)"), n.c_str());
                else std::swprintf(b, 256, tr(L"Computer (Player %ls)"), n.c_str());
            } else if (t.team == kTeamHumans) {
                if (many) std::swprintf(b, 256, tr(L"Humans (Players %ls)"), n.c_str());
                else std::swprintf(b, 256, tr(L"Human (Player %ls)"), n.c_str());
            } else if (t.team >= kTeamForce && t.team < kTeamForce + 4) {
                const std::string fn = gState.ed ? gState.ed->force(t.team - kTeamForce).name : std::string();
                std::wstring label;
                if (fn.empty()) {
                    wchar_t f[64];
                    std::swprintf(f, 64, tr(L"Force %d"), t.team - kTeamForce + 1);
                    label = f;
                } else {
                    label = widen(fn);  // (the force's name, from the map)
                }
                if (many) std::swprintf(b, 256, tr(L"%ls (Players %ls)"), label.c_str(), n.c_str());
                else std::swprintf(b, 256, tr(L"%ls (Player %ls)"), label.c_str(), n.c_str());
            } else {
                std::swprintf(b, 256, tr(L"Player %ls"), n.c_str());
            }
            return b;
        };
        const sim::CombatSide* won = nullptr;
        for (const sim::CombatSide& t : r.teams)
            if (t.team == r.winner) won = &t;
        std::wstring winner;
        if (won) winner = simWins(teamName(*won));
        else if (r.stalemate) winner = tr(L"Draw: Units cannot reach each other");
        else if (r.timedOut) winner = tr(L"No winner: 10 game minutes went by");
        else winner = tr(L"No winner");
        if (won && r.helpless >= 0) {
            wchar_t b[1024];
            std::swprintf(b, 1024, tr(L"%ls (the others have only units that cannot attack)"), winner.c_str());
            winner = b;
        }
        if (simAlliances() == kUnitFreeForAll && r.winnerUnit >= 0 && size_t(r.winnerUnit) < gSimCombatPlaced.size()) {
            const auto [type, owner] = gSimCombatPlaced[size_t(r.winnerUnit)];
            wchar_t b[1024];
            std::swprintf(b, 1024, tr(L"%ls of Player %d is the last unit standing"), simUnitName(type).c_str(), int(owner) + 1);
            winner = b;
        }
        int unreachable = 0;
        for (const sim::CombatSide& t : r.teams) unreachable += t.unreachable;
        if (!won && !r.timedOut && !r.stalemate && unreachable) {
            wchar_t b[1024];
            if (unreachable == 1) std::swprintf(b, 1024, tr(L"No winner: %d unit is out of reach of their enemies"), unreachable);
            else std::swprintf(b, 1024, tr(L"No winner: %d units are out of reach of their enemies"), unreachable);
            winner = b;
        }
        gSim.endText = simAfter(winner, simNum(r.frames * 0.042, 1));
        gSim.result = won ? simWins(teamName(*won)) : winner;
        std::wstring lost;
        for (const sim::CombatSide& t : r.teams) {
            wchar_t b[256];
            std::swprintf(b, 256, tr(L"%ls %d"), teamName(t).c_str(), int(t.mineralsLost + t.gasLost));
            lost += (lost.empty() ? L"" : L"; ") + std::wstring(b);
        }
        {
            wchar_t b[1024];
            std::swprintf(b, 1024, tr(L"Lost (minerals + gas): %ls"), lost.c_str());
            lost = b;
        }
        gSimTitle = gSim.endText;
        gSimLine = lost;
        SimDoc& d = gSimDoc;
        for (const sim::CombatSide& s : r.teams) {
            bool sh = false;
            for (const sim::UnitTypeStats& u : s.types) sh = sh || u.maxShields;
            std::vector<std::wstring> head = {tr(L"Unit"), tr(L"Alive"), tr(L"Kills"), tr(L"Hit points")};
            if (sh) head.push_back(tr(L"Shields"));
            head.push_back(tr(L"Lost (minerals + gas)"));
            std::vector<std::vector<std::wstring>> rows;
            for (const sim::UnitTypeStats& u : s.types) {
                std::vector<std::wstring> row = {simUnitName(u.unitType), simOf(u.alive, u.started), std::to_wstring(u.kills), simOf(u.hp, u.maxHp)};
                if (sh) row.push_back(u.maxShields ? simOf(u.shields, u.maxShields) : std::wstring(L"\x2013"));
                row.push_back(std::to_wstring((u.started - u.alive) * (u.mineralCost + u.gasCost)));
                rows.push_back(row);
            }
            std::vector<std::wstring> total = {tr(L"=All"), simOf(s.alive, s.units), std::to_wstring(s.kills), simOf(s.hp, s.startHp)};
            if (sh) total.push_back(simOf(s.shields, s.startShields));
            total.push_back(std::to_wstring(s.mineralsLost + s.gasLost));
            rows.push_back(total);
            d.heading(teamName(s), s.players.empty() ? -1 : s.players.front());
            d.table(head, rows);
            if (s.unreachable) {
                wchar_t b[1024];
                if (s.unreachable == 1)
                    std::swprintf(b, 1024, tr(L"%d unit was out of reach: no enemy could get to it or attack it (an island, only air or ground weapons, or cloaked)"), int(s.unreachable));
                else
                    std::swprintf(b, 1024, tr(L"%d units were out of reach: no enemy could get to it or attack it (an island, only air or ground weapons, or cloaked)"), int(s.unreachable));
                d.para(b, true);
            }
        }
        // (the whole paragraph is a template for each alliance, so that a translation can say it in its own way)
        const int al = simAlliances();
        std::wstring who;
        if (al == kUnitFreeForAll) who = tr(L"Fastest, with the map's upgrades; every unit fights every other unit; each unit attacks the enemies it can reach until none are left, for 10 game minutes at most.");
        else if (al == kMapForces) who = tr(L"Fastest, with the map's upgrades; the map's forces fight each other; each unit attacks the enemies it can reach until none are left, for 10 game minutes at most.");
        else if (al == kHumansVsComputers) who = tr(L"Fastest, with the map's upgrades; the human players fight the computer players; each unit attacks the enemies it can reach until none are left, for 10 game minutes at most.");
        else if (al == kFreeForAll) who = tr(L"Fastest, with the map's upgrades; each player fights alone; each unit attacks the enemies it can reach until none are left, for 10 game minutes at most.");
        else who = tr(L"Fastest, with the map's upgrades; the computer players are allies, each other player fights alone; each unit attacks the enemies it can reach until none are left, for 10 game minutes at most.");
        d.para(who, true);
        gSim.report = gSimTitle + L"\r\n" + gSimLine + L"\r\n" + d.text;
    } else if (gSim.combat) {
        const sim::CombatResult r = e.combatResult();
        const std::wstring a = simPlayer(gSim.players[0]), b = simPlayer(gSim.players[1]);
        std::wstring winner = r.a.alive && !r.b.alive ? simWins(a) : r.b.alive && !r.a.alive ? simWins(b)
                              : r.timedOut                                ? tr(L"No winner: 2 game minutes went by")
                                                                          : tr(L"No winner: both sides died");
        if (r.helpless >= 0) {
            wchar_t t[1024];
            std::swprintf(t, 1024, tr(L"%ls wins (%ls has only units that cannot attack)"), (r.helpless ? a : b).c_str(), (r.helpless ? b : a).c_str());
            winner = t;
        }
        gSim.endText = simAfter(winner, simNum(r.frames * 0.042, 1));
        const int valueA = r.a.mineralsLost + r.a.gasLost, valueB = r.b.mineralsLost + r.b.gasLost;
        // (what each side lost is in its table: the line has only the trade)
        std::wstring trade;
        wchar_t t[256];
        if (!valueA && !valueB) {
            trade = tr(L"Neither side lost a unit");
        } else if (!valueA || !valueB) {
            std::swprintf(t, 256, tr(L"%ls lost nothing"), (!valueA ? a : b).c_str());
            trade = t;
        } else if (valueA <= valueB) {
            std::swprintf(t, 256, tr(L"%ls traded %ls to 1"), a.c_str(), simNum(double(valueB) / valueA, 1).c_str());
            trade = t;
        } else {
            std::swprintf(t, 256, tr(L"%ls traded %ls to 1"), b.c_str(), simNum(double(valueA) / valueB, 1).c_str());
            trade = t;
        }
        gSim.result = r.helpless >= 0 ? simWins(r.helpless ? a : b) : winner;
        gSimTitle = gSim.endText;
        gSimLine = trade;
        SimDoc& d = gSimDoc;
        // Each side: its units by type and the whole army (the All row).
        auto side = [&](const std::wstring& who, int player, const sim::CombatSide& s) {
            bool sh = false;
            for (const sim::UnitTypeStats& u : s.types) sh = sh || u.maxShields;
            std::vector<std::wstring> head = {tr(L"Unit"), tr(L"Alive"), tr(L"Kills"), tr(L"Hit points")};
            if (sh) head.push_back(tr(L"Shields"));
            head.push_back(tr(L"Lost (minerals + gas)"));
            std::vector<std::vector<std::wstring>> rows;
            for (const sim::UnitTypeStats& u : s.types) {
                std::vector<std::wstring> row = {simUnitName(u.unitType), simOf(u.alive, u.started), std::to_wstring(u.kills), simOf(u.hp, u.maxHp)};
                if (sh) row.push_back(u.maxShields ? simOf(u.shields, u.maxShields) : std::wstring(L"\x2013"));
                row.push_back(std::to_wstring((u.started - u.alive) * (u.mineralCost + u.gasCost)));
                rows.push_back(row);
            }
            std::vector<std::wstring> total = {tr(L"=All"), simOf(s.alive, s.units), std::to_wstring(s.kills), simOf(s.hp, s.startHp)};
            if (sh) total.push_back(simOf(s.shields, s.startShields));
            total.push_back(std::to_wstring(s.mineralsLost + s.gasLost));
            rows.push_back(total);
            d.heading(who, player);
            d.table(head, rows);
        };
        side(a, gSim.players[0], r.a);
        side(b, gSim.players[1], r.b);
        d.para(tr(L"Fastest, no upgrades or research; each side attack-moves to the other's middle, for 2 game minutes at most."), true);
        gSim.report = gSimTitle + L"\r\n" + gSimLine + L"\r\n" + d.text;
    } else if (gSim.rush) {
        const std::vector<sim::RushRoute> routes = e.rushResult();
        SimDoc& d = gSimDoc;
        std::vector<std::vector<std::wstring>> rows;
        double fastest = -1, slowest = -1;
        for (const sim::RushRoute& r : routes) {
            const double secs = r.frames * 0.042;
            auto end = [](int player, int x, int y) {  // (a start location's player, or a point's tile)
                return player >= 0 ? simPlayer(player) : std::to_wstring(x / 32) + L", " + std::to_wstring(y / 32);
            };
            rows.push_back({end(r.from, r.fromX, r.fromY), end(r.to, r.toX, r.toY), simUnitName(r.unit), r.reached ? tr(L"Reached") : tr(L"Not reached"),
                            simNum(secs, 1), simNum(r.distance)});
            if (!r.reached) continue;
            fastest = fastest < 0 ? secs : std::min(fastest, secs);
            slowest = std::max(slowest, secs);
        }
        wchar_t txt[1024];
        if (fastest < 0) {
            gSim.result = tr(L"No unit reached its end");
            gSimTitle = tr(L"Rush distance: no unit reached its end");
        } else {
            std::swprintf(txt, 1024, tr(L"%ls to %ls game seconds"), simNum(fastest, 1).c_str(), simNum(slowest, 1).c_str());
            gSim.result = txt;
            std::swprintf(txt, 1024, tr(L"Rush distance: %ls to %ls game seconds"), simNum(fastest, 1).c_str(), simNum(slowest, 1).c_str());
            gSimTitle = txt;
        }
        gSim.endText = gSim.result;
        gSimLine = gRushPoints.on ? tr(L"Each unit goes from A to B, at Fastest")
                                  : tr(L"Each unit goes from each start location to each other start location, at Fastest");
        d.table({tr(L"From"), tr(L"To"), tr(L"Unit"), tr(L"Result"), tr(L"Game seconds"), tr(L"Distance (pixels)")}, rows, 4);
        gSim.report = gSimTitle + L"\r\n" + gSimLine + L"\r\n" + d.text;
    } else {
        const std::vector<sim::MiningBase> bases = e.miningResult();
        std::wstring summary;
        SimDoc& d = gSimDoc;
        std::vector<std::vector<std::wstring>> rows;
        std::vector<std::wstring> tripHead = {tr(L"Field")};
        std::vector<const sim::MiningRun*> runs;
        double minerals = 0;
        bool unplaced = false;  // a base with no room for its depot and workers
        int workers = 0;
        wchar_t txt[1024];
        for (size_t k = 0; k < bases.size(); ++k) {
            const sim::MiningBase& b = bases[k];
            if (b.runs.empty()) continue;
            const sim::MiningRun& r = b.runs[0];
            std::swprintf(txt, 1024, tr(L"Base %d"), int(k) + 1);
            const std::wstring base = txt;
            std::wstring label = base;
            if (b.startLocation) {
                std::swprintf(txt, 1024, tr(L"Base %d (start)"), int(k) + 1);
                label = txt;
            }
            const wchar_t* race = r.race == sim::Zerg ? tr(L"Zerg") : r.race == sim::Protoss ? tr(L"Protoss") : tr(L"Terran");
            rows.push_back({label, race, std::to_wstring(b.x / 32) + L", " + std::to_wstring(b.y / 32),
                            r.placed ? std::to_wstring(b.fields.size()) : std::wstring(L"\x2014"),
                            r.placed ? std::to_wstring(r.workers) : std::wstring(L"\x2014"), r.placed ? simNum(r.mineralsPerMinute) : std::wstring(tr(L"No room"))});
            minerals += r.mineralsPerMinute, workers += r.workers;
            unplaced = unplaced || !r.placed;
            tripHead.push_back(base);
            runs.push_back(&r);
        }
        if (rows.size() > 1) rows.push_back({tr(L"=All"), L"", L"", L"", std::to_wstring(workers), simNum(minerals)});
        std::swprintf(txt, 1024, tr(L"%ls minerals a minute"), simNum(minerals).c_str());
        gSim.endText = gSim.result = txt;
        std::swprintf(txt, 1024, tr(L"Mining: %ls minerals a minute"), simNum(minerals).c_str());
        gSimTitle = txt;
        int minutes = 3;
        prefs::number(L"SimMinutes", minutes, 1, 60);
        if (gSim.perField && gSim.workers == 1)
            std::swprintf(txt, 1024, tr(minutes == 1 ? L"1 worker for each mineral field at each base, %d game minute at Fastest"
                                                     : L"1 worker for each mineral field at each base, %d game minutes at Fastest"), minutes);
        else if (gSim.perField)
            std::swprintf(txt, 1024, tr(minutes == 1 ? L"%d workers for each mineral field at each base, %d game minute at Fastest"
                                                     : L"%d workers for each mineral field at each base, %d game minutes at Fastest"), int(gSim.workers), minutes);
        else if (minutes == 1)
            std::swprintf(txt, 1024, tr(L"%d workers on minerals at each base, %d game minute at Fastest"), int(gSim.workers), minutes);
        else
            std::swprintf(txt, 1024, tr(L"%d workers on minerals at each base, %d game minutes at Fastest"), int(gSim.workers), minutes);
        gSimLine = txt;
        d.table({tr(L"Base"), tr(L"Race"), tr(L"Tile"), tr(L"Fields"), tr(L"Workers"), tr(L"Minerals a minute")}, rows, 3);
        if (unplaced) d.para(tr(L"No room: the base has no room for its depot and workers."), true);
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
            d.heading(tr(L"Round trip of each field, nearest first (frames)"));
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
// The mining's settings panel (the bar's gear; below: simPaintPanel): open, its part under the mouse and pressed.
struct SimPanel {
    bool open = false;
    int hot = 0, down = 0;
} gSimPanel;
void simClose() {
    simSkipJoin();
    simStopSounds();
    if (gSim_) simEngine().stop();
    gLiveScene = nullptr, gLiveMinimap = nullptr, gLiveCreep = nullptr;
    gSimCreep.clear(), gSimCreepChanges = ~0ull;
    gSimRoutes.clear(), gSimFields.clear(), gSimGuides.clear();
    gSim = LiveSim{};
    gSimPanel = SimPanel{};
    gRushDrag = 0;
    simRelease();
    simRedraw();
}

// A game starts: the map's copy (its unsaved edits too) into the simulation, then `start`.
// A Cosmonarchy BW map as the simulation reads it (it has the game's units only): each of the mod's workers, resource
// depots and resources as the game's unit of that role (cmbw::simUnits: Talos and Pilgrim as SCVs, Landlock as a Command
// Center, Vespene Ridges as Vespene Geysers, ...), its other own units left out; the simulation runs them with the
// game's values.
Bytes simModChk(const Chk& map, const UnitTypes& types) {
    Chk c = map;
    if (const Section* u = c.find("UNIT")) c.set("UNIT", stardraft::cmbw::simUnits(u->data, types));
    return c.serialize();
}

// The bar's own new games (its settings, the rush's points): the play stays as it was. A command's new game waits.
bool gSimKeepPlay = false;
void simKeepPlay(const std::function<void()>& run) {
    gSimKeepPlay = true;
    run();
    gSimKeepPlay = false;
}
bool simBegin(bool combat, const std::function<bool(std::string&)>& start, bool rush = false) {
    if (!gState.ed || !gState.ed->isOpen()) return false;
    if ((combat || rush) && modTab()) {  // (the mod's units have their own values, which the simulation does not have)
        if (gRoot)
            msgbox::ask(*gRoot, tr(L"Simulation"),
                        rush ? tr(L"The rush distance simulation is not available for Cosmonarchy BW maps. The mining simulation is.")
                             : tr(L"The combat simulation is not available for Cosmonarchy BW maps. The mining simulation is."),
                        {tr(L"OK")}, msgbox::Icon::Info);
        return false;
    }
    const bool playing = gSimKeepPlay && gSim.shown && gSim.playing;  // (a new game of the bar's own settings: as it was; else it waits)
    if (gSim.shown) simClose();
    if (!simReady(modTab())) return false;
    std::string err;
    Bytes chk = modTab() ? simModChk(gState.ed->chk(), gState.ed->unitTypes()) : gState.ed->chk().serialize();
    if (!simEngine().setMap(chk, err) || !start(err)) {
        if (gRoot) msgbox::ask(*gRoot, L"Simulation", widen(err), {L"OK"}, msgbox::Icon::Error);
        return false;
    }
    gSim.playing = playing;
    gSim.shown = true, gSim.combat = combat, gSim.rush = rush, gSim.done = false, gSim.ed = gState.ed.get();
    gSim.chk = std::move(chk), gSim.start = start;
    gSim.last = std::chrono::steady_clock::now(), gSim.due = 0;
    gLiveScene = simScene, gLiveMinimap = simMinimap, gLiveCreep = simCreep;
    gSimCreep.clear(), gSimCreepChanges = ~0ull;
    if (gRoot) gRoot->map.focus();  // (a search box of a panel lets go of the keyboard)
    simHold();
    simRedraw();
    return true;
}

// The live game's sound player: at most 12 sounds at a time, at most 2 of one sound, and one sound does not start again
// within 90 ms (a crowd's attacks are not a wall of noise). The sounds near the view's middle come first: with 12
// playing, a nearer sound stops the farthest one. Each sound plays in stereo, panned by `pan` (-1 left, 1 right) with an
// equal-power law, at `gain` times the program's volume; `dist` is how far from the view's middle it is (the farthest gives way).
// The decoded sounds are kept (as mono floats).
struct SimVoice {
    SDL_AudioStream* stream;
    int sfx;
    float dist;
};
std::vector<SimVoice> gSimVoices;
void simStopSounds() {
    for (const SimVoice& v : gSimVoices) closeStreamSdl(v.stream);
    gSimVoices.clear();
}
bool simPlaySound(int sfx, float pan, float gain, float dist) {
    using Voice = SimVoice;
    std::vector<Voice>& voices = gSimVoices;
    struct Mono {
        int freq = 0;
        std::vector<float> samples;
    };
    static std::map<int, Mono> decoded;
    static std::map<int, Uint64> lastStart;
    for (auto it = voices.begin(); it != voices.end();)
        if (SDL_GetAudioStreamQueued(it->stream) <= 0) closeStreamSdl(it->stream), it = voices.erase(it);
        else ++it;
    if (std::count_if(voices.begin(), voices.end(), [&](const Voice& v) { return v.sfx == sfx; }) >= 2) return false;
    const Uint64 now = SDL_GetTicks();
    if (auto l = lastStart.find(sfx); l != lastStart.end() && now - l->second < 90) return false;
    if (voices.size() >= 12) {  // (full: the farthest sound gives way to a nearer one)
        auto farthest = std::max_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b) { return a.dist < b.dist; });
        if (farthest->dist <= dist) return false;
        closeStreamSdl(farthest->stream);
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
    if (m.samples.empty()) return false;
    const SDL_AudioSpec stereo{SDL_AUDIO_F32, 2, m.freq};
    SDL_AudioStream* s = openPlaybackSdl(&stereo);
    if (!s) return false;
    const float a = (std::clamp(pan, -1.0f, 1.0f) + 1) * 0.785398f / 2;  // (0 to pi/2)
    const float left = std::cos(a), right = std::sin(a);
    std::vector<float> lr(m.samples.size() * 2);
    for (size_t i = 0; i < m.samples.size(); ++i) lr[2 * i] = m.samples[i] * left, lr[2 * i + 1] = m.samples[i] * right;
    // A crowd is quieter: with more than 4 sounds playing, each one at 2 / sqrt(the count) (the sum stays even).
    const float crowd = voices.size() >= 4 ? 2.0f / std::sqrt(float(voices.size() + 1)) : 1.0f;
    SDL_SetAudioStreamGain(s, float(gDock.volume) / 100.0f * std::clamp(gain, 0.0f, 1.0f) * crowd);
    SDL_PutAudioStreamData(s, lr.data(), int(lr.size() * sizeof(float)));
    SDL_FlushAudioStream(s);
    playStreamSdl(s);
    voices.push_back({s, sfx, dist});
    lastStart[sfx] = now;
    return true;
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
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = std::max(1.0f, (x1 - x0) / 2), hh = std::max(1.0f, (y1 - y0) / 2);
    // The listener is a camera above the view's middle, as high as the view is zoomed out: its height is 320 map pixels
    // at 100% (4 x that at 25%). A sound's distance is from the camera, in map pixels: full volume within 320, none
    // from 2400 on, a straight fall between. So zoomed out everything is quieter and far sounds go first; zoomed in
    // the units near the middle are loud. Left or right of the middle pans it (by the view's half width, at least
    // 400 pixels). The nearest first.
    const float height = 320.0f / std::max(0.05f, gState.zoom), nearDist = 320, farDist = 2400;
    struct Heard {
        int sfx;
        float pan, dist, screen;
    };
    // Only the sounds on the screen play, and the middle of the screen counts most: full volume within a quarter of
    // the way to the screen's edge, none at the edge (by the view's ellipse).
    std::vector<Heard> close;
    for (const sim::LiveSound& s : sounds) {
        const float dx = float(s.x) - cx, dy = float(s.y) - cy, dist = std::sqrt(dx * dx + dy * dy + height * height);
        const float onScreen = std::sqrt((dx / hw) * (dx / hw) + (dy / hh) * (dy / hh));  // 0 the middle, 1 the edge
        if (dist < farDist && onScreen < 1) close.push_back({s.sfx, std::clamp(dx / std::max(hw, 400.0f), -1.0f, 1.0f) * 0.8f, dist, onScreen});
    }
    std::stable_sort(close.begin(), close.end(), [](const Heard& a, const Heard& b) { return a.screen < b.screen; });
    std::set<int> heard;  // (one of each sound a frame: the one nearest the middle)
    int started = 0;
    for (const Heard& h : close) {
        if (started >= 4) break;  // (at most 4 new sounds a frame)
        if (!heard.insert(h.sfx).second) continue;
        const float middle = std::clamp((1.0f - h.screen) / 0.75f, 0.0f, 1.0f);
        const float gain = middle * std::clamp((farDist - h.dist) / (farDist - nearDist), 0.0f, 1.0f);
        if (gain > 0.05f) started += simPlaySound(h.sfx, h.pan, gain, h.screen) ? 1 : 0;
    }
}

// The game at `frame` (Restart: 0; the timeline): the game again from the start, run to that frame on the skip's
// thread (the game is the same each time it runs). Forward: only the frames between.
void simRunAhead(int target);
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
    if (frame > e.frame() + 8) simRunAhead(frame);  // (the skip's thread: a large map's game is slow)
    else if (frame > e.frame() && !e.step(frame - e.frame())) simEnd();
    simSounds(false);
    gSim.due = 0, gSim.last = std::chrono::steady_clock::now();
    simRedraw();
}

// The bar: a compact pill at the top middle of the map. At the left an info icon (its tooltip: what the simulation
// is), then what runs and the game time (at the end the result); at the right the buttons: the speed with - and +,
// Restart, Skip to the end and Cancel (at the end: Restart, Report and Close). Mining has a thin timeline along the
// pill's bottom edge (rush distance and combat have none: they go on until the end): how far the game is, of its longest time; a click or a drag goes to that time.
// The buttons' ids (gSim.hot, gSim.pressed): 0 Cancel/Close, 1 the speed/Report, 2 Restart, 3 the info icon, 4 -, 5 +,
// 6 Skip to the end, 7 the settings (mining's workers, rush distance's race, a combat's alliances), 8 Report feedback,
// 10 Play or Pause.
constexpr float kSimSpeeds[] = {0.25f, 0.5f, 1, 2, 4, 8, 16, 32, 64};
struct SimBarLayout {
    draw::Rect bar, chip, text, first, second, third, minus, plus, skip, play, gear, feedback, track, trackHit;
};
SimBarLayout simLayout(draw::Rect r) {
    SimBarLayout l;
    const int h = scale(40), pad = scale(5), ib = h - 2 * pad, gap = scale(2), tw = scale(gSim.done ? 64 : 46);
    const int w = std::min(int(r.right - r.left) - scale(16), scale(gSim.done ? 460 : 520)), x = (r.left + r.right - w) / 2, y = r.top + scale(10);
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
    if (!gSim.done) l.play = left(l.minus, ib);  // Play or Pause
    draw::Rect firstButton = gSim.done ? l.third : l.play;
    l.gear = left(firstButton, ib), firstButton = l.gear;  // (the settings: mining's workers, rush's race, a combat's alliances)
    // Rush distance and combat: the fog of war, Report feedback and the info are in the gear's drop-down; mining (its
    // gear opens a panel) has Report feedback and the info icon on the bar.
    const bool menuGear = gSim.combat || gSim.rush;
    if (!menuGear) {
        l.feedback = left(firstButton, ib), firstButton = l.feedback;           // Report feedback
        l.chip = {x + pad, y + pad, x + pad + ib, y + pad + ib};  // the info icon
    }
    l.text = {menuGear ? x + pad + scale(10) : l.chip.right + scale(6), y, firstButton.left - scale(8), y + h};
    if (gSim.combat || gSim.rush) return l;  // (no timeline: a combat goes on until no side can fight, a rush until all arrive)
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

// The game runs on the skip's thread (no sounds) to `target` (a frame), or to its end (INT_MAX: Skip to the end, then
// the result and its report show). The window stays usable meanwhile.
void simRunAhead(int target) {
    if (!gSim.shown || gSim.done || simSkipRunning() || simEngine().boosting()) return;
    gSim.skipping = true;  // (simFrame waits for the thread)
    gSim.toEnd = target == INT_MAX;
    simImages();  // (the picture to show meanwhile)
    if (gSim.rush) gSimRoutes = simEngine().rushResult();
    else if (!gSim.combat) gSimFields = simEngine().miningResult(), gSimGuides = simEngine().miningGuides();
    gSimSkipFrame = simEngine().frame();
    gSimSkipEnded = false, gSimSkipReached = false;
    simEngine().setFastForward(true);  // (no pictures or sounds while it skips: the play is the same)
    gSimSkipThread = std::thread([target] {
        sim::Simulator& e = simEngine();
        while (!gSimSkipStop) {
            if (e.frame() >= target) {
                gSimSkipReached = true;
                return;
            }
            const bool more = e.step(std::min(8, target - e.frame()));  // (small steps: a stop waits for one at most)
            gSimSkipFrame = e.frame();
            if (!more) {
                gSimSkipEnded = true;
                return;
            }
        }
    });
    simRedraw();
}
void simSkipToEnd() {
    gSim.playing = true;
    if (gSim.skipping && !gSim.toEnd) simSkipJoin(), gSim.skipping = false;  // (a seek runs: on from where it is)
    simRunAhead(INT_MAX);
}

}  // namespace

bool simBarShown() { return gSim.shown; }
// Over the rush's A or B (not under the bar), or while one moves: the move cursor.
bool simRushPointCursor(int x, int y, draw::Rect r) {
    const draw::Rect bar = simLayout(r).bar;
    const bool underBar = x >= bar.left && x < bar.right && y >= bar.top && y < bar.bottom;
    return gRushDrag || (!underBar && simRushMarkerAt(x, y));
}
void simRushStart(const RushPoints& p);  // (below)
bool simRushDragging() { return gRushDrag != 0; }
// The drag of A or B ends with no release on the map (the mouse let go elsewhere, the window lost the mouse): the
// point stays where it was last moved, and the rush starts again from there.
void simRushDragLost() {
    if (!gRushDrag) return;
    gRushDrag = 0;
    const RushPoints p = gRushPoints;
    shellPost([p] { simKeepPlay([p] { simRushStart(p); }); });
}

// The races of Program options > Simulate mining (SimRaces), as the bar's gear lists them.
const wchar_t* const kSimRaceNames[4] = {L"As defined in map (Random: a random race)", L"Terran", L"Protoss", L"Zerg"};

// The mining's settings (the bar's gear): a panel under the bar. The workers on minerals at each base: for each mineral
// field or in all (a choice of two), and their number (a step box); the races (a list); the measured game time and the
// boost search's time (step boxes); the three boost searches (position, cargo, cycle: the pull request's Mining Lab
// searches, each base at the same time) as check boxes, each with its state; and a link to Program options >
// Simulation, which has the same settings. A change of the workers or the races starts the mining again (its boosts
// are searched again: they were for those workers); the game time keeps them.
// The panel's parts (gSimPanel.hot, .down): 1 and 2 the workers' - and +, 13 and 14 for each field and in all, 3 the
// races, 4 and 5 the game time's - and +, 6 and 7 the search time's, 8 to 10 the position, cargo and cycle check
// boxes, 11 the link, 12 the rest of the panel.
void simulateMiningCommand();
void simMiningNewTime();
struct SimPanelLayout {
    draw::Rect box, value[4], kind[2], check[3], link;
    int rowY[4] = {}, headY = 0;
};
SimPanelLayout simPanelLayout(const SimBarLayout& l, draw::Rect r) {
    SimPanelLayout p;
    const int w = std::min(int(r.right - r.left) - scale(16), scale(420)), pad = scale(12), rowH = scale(34), boxH = scale(26);
    const int right = std::min(int(r.right) - scale(8), int(l.gear.right) + scale(60)), left = std::max(int(r.left) + scale(8), right - w);
    int y = l.bar.bottom + scale(6) + pad / 2;
    const int valueW = scale(260), stepW = scale(120);
    for (int k = 0; k < 4; ++k) {
        p.rowY[k] = y;
        const int top = y + (rowH - boxH) / 2;
        p.value[k] = {left + w - pad - (k == 0 ? stepW : valueW), top, left + w - pad, top + boxH};
        y += rowH;
    }
    // (the workers' row: the two kinds, then the number)
    const int kx = left + w - pad - valueW, kw = (valueW - stepW - scale(8)) / 2;
    p.kind[0] = {kx, p.value[0].top, kx + kw, p.value[0].bottom};
    p.kind[1] = {kx + kw, p.value[0].top, kx + 2 * kw, p.value[0].bottom};
    y += scale(6);
    p.headY = y;
    y += scale(24);
    for (int k = 0; k < 3; ++k, y += scale(28)) p.check[k] = {left + pad, y, left + w - pad, y + scale(28)};
    y += scale(6);
    p.link = {left + pad, y, left + w - pad, y + scale(24)};
    y += scale(24) + pad / 2;
    p.box = {left, l.bar.bottom + scale(6), left + w, y};
    return p;
}
int simPanelPart(const SimPanelLayout& p, draw::Point pt) {
    auto in = [&](draw::Rect b) { return pt.x >= b.left && pt.x < b.right && pt.y >= b.top && pt.y < b.bottom; };
    if (!in(p.box)) return 0;
    for (int k : {0, 2, 3}) {
        const int first = k == 0 ? 1 : k == 2 ? 4 : 6;
        if (in(optionsview::stepBoxMinus(p.value[k]))) return first;
        if (in(optionsview::stepBoxPlus(p.value[k]))) return first + 1;
    }
    if (in(p.kind[0])) return 13;
    if (in(p.kind[1])) return 14;
    if (in(p.value[1])) return 3;
    for (int k = 0; k < 3; ++k)
        if (in(p.check[k])) return 8 + k;
    const int lw = std::min(int(p.link.right - p.link.left), scale(160));
    if (in({p.link.left, p.link.top, p.link.left + lw, p.link.bottom})) return 11;
    return 12;
}
// The settings as the panel shows them (optionsview has their ranges and default values).
struct SimSettings {
    int kind = 1, perField = 1, total = 9;  // the workers: 0 for each mineral field (perField), 1 in all (total)
    int minutes = 3, races = 0, search = optionsview::kSimSearchDefault;
    bool boost[3] = {};  // the boost searches that are on (position, cargo, cycle)
    int workers() const { return kind == 0 ? perField : total; }
};
const wchar_t* const kSimBoostKeys[3] = {L"SimBoostPosition", L"SimBoostCargo", L"SimBoostCycle"};
SimSettings simSettings() {
    SimSettings s;
    prefs::number(L"SimWorkerKind", s.kind, 0, 1), prefs::number(L"SimWorkersPerField", s.perField, 1, optionsview::kSimPerFieldMax);
    prefs::number(L"SimWorkers", s.total, 1, optionsview::kSimWorkersMax), prefs::number(L"SimMinutes", s.minutes, 1, optionsview::kSimMinutesMax);
    prefs::number(L"SimRaces", s.races, 0, 3);
    prefs::number(L"SimSearchSeconds", s.search, optionsview::kSimSearchMin, optionsview::kSimSearchMax);
    for (int k = 0; k < 3; ++k) prefs::flag(kSimBoostKeys[k], s.boost[k]);
    return s;
}
const wchar_t* const kSimBoostNames[3] = {L"Position", L"Cargo", L"Cycle"};
// A boost search's state beside its check box: searching, how many workers it made faster, or nothing yet.
std::wstring simBoostState(int kind) {
    const sim::BoostStatus st = simEngine().boostStatus(kind);
    wchar_t t[128];
    if (st.running) return tr(L"Looking for boosts\x2026");
    if (!st.finished) return {};
    if (st.failed && st.found == 0) return tr(L"Could not search");  // (some bases found boosts: those show)
    if (st.found == 0) return tr(L"None found");
    if (st.found == 1) return tr(L"1 worker faster");
    std::swprintf(t, 128, tr(L"%d workers faster"), st.found);
    return t;
}
void simPaintPanel(draw::Canvas& c, const SimBarLayout& l, draw::Rect r) {
    const SimPanelLayout p = simPanelLayout(l, r);
    const SimSettings s = simSettings();
    const bool busy = simSkipRunning() || simEngine().boosting();
    c.roundBox(p.box, kPanel, kBorder, float(scale(8)));
    const int pad = scale(12);
    const wchar_t* const names[4] = {L"Workers on minerals", L"Races", L"Measured game time", L"Boost search time"};
    for (int k = 0; k < 4; ++k)
        c.text({p.box.left + pad, p.rowY[k], (k == 0 ? p.kind[0].left : p.value[k].left) - scale(8), p.rowY[k] + scale(34)}, names[k], kText,
               draw::kMiddle | draw::kEllipsis);
    auto step = [&](int k, int first, const std::wstring& text, bool atMin, bool atMax) {
        const int hot = gSimPanel.hot == first ? 1 : gSimPanel.hot == first + 1 ? 2 : 0;
        const int down = gSimPanel.down == first ? 1 : gSimPanel.down == first + 1 ? 2 : 0;
        optionsview::paintStepBox(c, p.value[k], text, hot, down, atMin, atMax);
    };
    // The workers' two kinds: the one that is on in the accent (a segmented choice).
    for (int k = 0; k < 2; ++k) {
        const bool on = s.kind == k, over = gSimPanel.hot == 13 + k;
        c.roundBox(p.kind[k], on ? kAccentFill : over ? kCtlHot : kCtl, on ? kAccent : kBorder, float(scale(stardraft::kit::kRadius)));
        c.text(p.kind[k], k == 0 ? tr(L"Each field") : tr(L"In all"), on ? kText : kDim, draw::kCenter | draw::kMiddle | draw::kEllipsis);
    }
    const int lo = 1, hi = s.kind == 0 ? optionsview::kSimPerFieldMax : optionsview::kSimWorkersMax;
    step(0, 1, std::to_wstring(s.workers()), s.workers() <= lo, s.workers() >= hi);
    kitpaint::drawPickFace(c, p.value[1], s.races == 0 ? std::wstring(tr(L"As defined in map")) : std::wstring(kSimRaceNames[s.races]), false, gSimPanel.hot == 3);
    step(2, 4, optionsview::simMinutesText(s.minutes), s.minutes <= 1, s.minutes >= optionsview::kSimMinutesMax);
    step(3, 6, optionsview::simSecondsText(s.search), s.search <= optionsview::kSimSearchMin, s.search >= optionsview::kSimSearchMax);
    // (each search that is on runs before the game starts, for up to the boost search time)
    c.text({p.box.left + pad, p.headY, p.box.right - pad, p.headY + scale(24)}, L"Boost searches: they run before the game starts", kDim,
           draw::kMiddle | draw::kEllipsis);
    for (int k = 0; k < 3; ++k) {
        const draw::Rect b = p.check[k];
        const int cs = scale(16), cy = (b.top + b.bottom - cs) / 2;
        kitpaint::drawCheckMark(c, {b.left, cy, b.left + cs, cy + cs}, s.boost[k], gSimPanel.hot == 8 + k && !busy, !busy);
        c.text({b.left + cs + scale(8), b.top, b.left + scale(140), b.bottom}, kSimBoostNames[k], busy ? kDim : kText, draw::kMiddle | draw::kEllipsis);
        c.text({b.left + scale(140), b.top, b.right, b.bottom}, simBoostState(k), kDim, draw::kMiddle | draw::kRight | draw::kEllipsis);
    }
    c.text(p.link, L"Simulation options", gSimPanel.hot == 11 ? kAccentBright : kAccent, draw::kMiddle | draw::kEllipsis);
}
// The boost searches that are on, one after another (position, cargo, cycle: each later one is found with the earlier
// ones), and the ones turned off taken out. Each window frame of the mining (simFrame) and when it starts.
void simBoostSync() {
    if (!gSim.shown || gSim.combat || gSim.rush || simSkipRunning() || modTab()) return;  // (a Cosmonarchy map: no boosts)
    sim::Simulator& e = simEngine();
    const SimSettings s = simSettings();
    for (int k = 0; k < 3; ++k) {
        const sim::BoostStatus st = e.boostStatus(k);
        if (!s.boost[k] && (st.running || st.finished)) e.clearBoost(k), simRedraw();
    }
    if (e.boosting()) return;
    for (int k = 0; k < 3; ++k) {
        if (!s.boost[k] || e.boostStatus(k).finished || (gSim.boostFailed & (1 << k))) continue;
        std::string err;
        if (!e.startBoost(k, double(s.search), err)) {  // (it cannot search: once for this game, and why)
            gSim.boostFailed |= 1 << k;
            if (gRoot) shellPost([err] { msgbox::ask(*gRoot, tr(L"Simulation"), widen(err), {tr(L"OK")}, msgbox::Icon::Info); });
            continue;
        }
        gSim.done = false, gSim.due = 0;
        simRedraw();
        return;
    }
}
// The races' list (the panel's Races): a pick saves it and starts the mining again.
void simMiningRacesMenu() {
    if (!gRoot || !gHost) return;
    const int races = simSettings().races;
    std::vector<stardraft::kit::MenuItem> items;
    for (int k = 0; k < 4; ++k) {
        items.push_back(stardraft::kit::MenuItem{});
        items.back().label = kSimRaceNames[k], items.back().checked = k == races;
        items.back().run = [k] {
            prefs::setNumber(L"SimRaces", k);
            shellPost([] { simKeepPlay(simulateMiningCommand), gSimPanel.open = gSim.shown; });
        };
    }
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    stardraft::kit::Menu::popup(*gRoot, gHost->mouse(*gRoot), items, stardraft::kit::Menu::measure(items, &measure), {});
}
// A press on the panel's part.
void simPanelPress(int part) {
    SimSettings s = simSettings();
    auto change = [](const wchar_t* key, int value, bool restart) {
        prefs::setNumber(key, value);
        if (restart) shellPost([] { simKeepPlay(simulateMiningCommand), gSimPanel.open = gSim.shown; });  // (the panel stays open)
        simRedraw();
    };
    switch (part) {
    case 1: case 2:
        if (s.kind == 0) change(L"SimWorkersPerField", std::clamp(s.perField + (part == 1 ? -1 : 1), 1, optionsview::kSimPerFieldMax), true);
        else change(L"SimWorkers", std::clamp(s.total + (part == 1 ? -1 : 1), 1, optionsview::kSimWorkersMax), true);
        break;
    case 13: case 14:
        if (s.kind != part - 13) change(L"SimWorkerKind", part - 13, true);
        break;
    case 4: case 5:
        change(L"SimMinutes", std::clamp(s.minutes + (part == 4 ? -1 : 1), 1, optionsview::kSimMinutesMax), false);
        simMiningNewTime();
        break;
    case 6: case 7: change(L"SimSearchSeconds", std::clamp(s.search + (part == 6 ? -1 : 1), optionsview::kSimSearchMin, optionsview::kSimSearchMax), false); break;
    case 3: shellPost([] { simMiningRacesMenu(); }); break;
    case 8: case 9: case 10:
        prefs::setNumber(kSimBoostKeys[part - 8], s.boost[part - 8] ? 0 : 1);
        simBoostSync();
        simRedraw();
        break;
    case 11: gSimPanel.open = false, shellPost([] { showOptionsTab(L"Simulation"); }); break;
    default: break;
    }
}
// The mouse on the open panel (true: taken). The wheel on a step box steps it.
bool simPanelMouse(const draw::MouseEvent& e, const SimBarLayout& l, draw::Rect r) {
    if (!gSimPanel.open || gSim.combat || gSim.rush) return false;
    const SimPanelLayout p = simPanelLayout(l, r);
    const int part = e.kind == draw::MouseKind::Leave ? 0 : simPanelPart(p, e.pt);
    if (part != gSimPanel.hot) gSimPanel.hot = part, simRedraw();
    if (e.kind == draw::MouseKind::Up && gSimPanel.down) {
        gSimPanel.down = 0, simRedraw();
        return part != 0;
    }
    if (!part) return false;
    // (a search or a skip runs: the workers, the races and the game time wait; the search time, the links and the
    // check boxes do not: a check box off stops its search)
    const bool busy = simSkipRunning() || simEngine().boosting();
    const bool waits = busy && (part <= 5 || part == 13 || part == 14);
    if (e.kind == draw::MouseKind::Wheel && e.wheel != 0) {
        const int up = e.wheel > 0 ? 1 : 0;
        if (waits) return true;
        if (part == 1 || part == 2) simPanelPress(up ? 2 : 1);
        else if (part == 4 || part == 5) simPanelPress(up ? 5 : 4);
        else if (part == 6 || part == 7) simPanelPress(up ? 7 : 6);
        return true;
    }
    if ((e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::DoubleClick) && e.button == 0 && part != 12) {
        if (waits || (simSkipRunning() && part >= 8 && part <= 10)) return true;
        gSimPanel.down = part, simRedraw();
        simPanelPress(part);
    }
    return true;
}
std::wstring simPanelTip(const SimBarLayout& l, draw::Rect r, draw::Point pt) {
    if (!gSimPanel.open || gSim.combat || gSim.rush) return {};
    switch (simPanelPart(simPanelLayout(l, r), pt)) {
    case 8: return L"Search for better start places of the workers at each base. The search runs before the game starts.";
    case 9: return L"Search for the places where a worker returns its cargo sooner. The search runs before the game starts.";
    case 10: return L"Search for trips that go to the next mineral field and back (mineral walking). The search runs before the game starts.";
    case 13: return L"A number of workers for each mineral field of a base";
    case 14: return L"A number of workers for each base";
    case 11: return L"Program options > Simulation";
    default: return {};
    }
}

void simPaintBar(draw::Canvas& c, draw::Rect r) {
    simPaintRush(c, r);  // (the rush's ways and points over the map, under the bar)
    const SimBarLayout l = simLayout(r);
    const float round = float((l.bar.bottom - l.bar.top) / 2);
    c.roundBox(l.bar, kPanel, kBorder, round);
    sim::Simulator& e = simEngine();
    // The info icon (its tooltip: an early preview).
    if (!l.chip.empty()) c.icon(l.chip, int(UiIcon::Info), gSim.hot == 3 ? kText : kDim, scale(16));
    // What runs and the game time; at the end the result.
    if (gRushDrag && gSim.rush) {
        c.text(l.text, gRushDrag == 1 ? tr(L"Release to start from here") : tr(L"Release to go here"), kAccent, draw::kMiddle | draw::kEllipsis);
    } else if (gSim.done) {
        c.text(l.text, gSim.result, kText, draw::kMiddle | draw::kEllipsis);
    } else {
        const bool boosting = !simSkipRunning() && !gSim.combat && !gSim.rush && e.boosting();
        const std::wstring what = gSim.combat ? L"Combat" : gSim.rush ? L"Rush distance" : boosting ? L"Looking for boosts\x2026" : L"Mining";
        const int frame = simSkipRunning() ? gSimSkipFrame.load() : e.frame();
        const std::wstring time = boosting ? std::wstring() : gSim.combat || gSim.rush ? simClock(frame)
                                                             : simClock(frame) + L" / " + simClock(e.lastFrame());
        const int ww = c.measure(what);
        c.text(l.text, what, kText, draw::kMiddle);
        const draw::Rect tr{l.text.left + ww + scale(8), l.text.top, l.text.right, l.text.bottom};
        c.text(tr, time, kDim, draw::kMiddle | draw::kEllipsis);
        const int spinX = tr.left + c.measure(time) + scale(16);
        if ((gSim.skipping || gSim.behind || boosting) && spinX + scale(6) <= tr.right) {  // a spinner: 8 dots round, one bright, turning
            const int cx = spinX, cy = (tr.top + tr.bottom) / 2, rad = scale(6), dot = std::max(2, scale(2));
            const int lead = int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 90 % 8);
            for (int k = 0; k < 8; ++k) {
                const float a = float(k) * 0.785398f;
                const int px = cx + int(std::lround(rad * std::cos(a))), py = cy + int(std::lround(rad * std::sin(a)));
                const int age = (lead - k + 8) % 8;
                c.fill({px - dot / 2, py - dot / 2, px - dot / 2 + dot, py - dot / 2 + dot}, age == 0 ? kAccent : kDim, uint8_t(age == 0 ? 255 : 230 - age * 25));
            }
            if (gSim.skipping && gSim.toEnd) c.text({cx + rad + scale(8), tr.top, tr.right, tr.bottom}, L"Skip to the end", kDim, draw::kMiddle | draw::kEllipsis);
        }
    }
    // The timeline (mining), along the bottom edge.
    if (!gSim.combat && !gSim.rush) {
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
    iconButton(2, l.third, gSim.done ? UiIcon::Play : UiIcon::ToStart);  // (at the end: Play runs it again from the start)
    if (!l.feedback.empty()) iconButton(8, l.feedback, UiIcon::Feedback);
    iconButton(7, l.gear, UiIcon::Settings);
    if (gSim.done) {
        textButton(1, l.second, L"Report", true);
    } else {
        // Play while it waits (with the keyboard's ring: Space plays it), Pause while it runs.
        if (!gSim.playing) c.roundBox(l.play, draw::kNone, kAccent, float((l.play.bottom - l.play.top) / 2));
        c.icon(l.play, int(gSim.playing ? UiIcon::Pause : UiIcon::Play), face(10, l.play, false) || !gSim.playing ? kText : kDim, scale(16));
        textButton(4, l.minus, L"\x2212", false);
        c.text(l.second, simSpeedText(gSim.speed), kText, draw::kCenter | draw::kMiddle);
        textButton(5, l.plus, L"+", false);
        iconButton(6, l.skip, UiIcon::ToEnd);
    }
    iconButton(0, l.first, UiIcon::Close);
    if (gSimPanel.open && !gSim.combat && !gSim.rush) simPaintPanel(c, l, r);
}

// The bar's Report feedback: the text box starts with short facts about the game (under a line, so the user writes
// above them): what runs, the game time and speed, the map, the units alive of each side, the result or an error.
void showFeedbackFilledSdl(const std::wstring& on, const std::wstring& text);  // (sdl_options.inc)
std::wstring simDiagnostics() {
    sim::Simulator& e = simEngine();
    const int frame = simSkipRunning() ? gSimSkipFrame.load() : e.frame();
    std::wstring t = L"\n\n---\n";
    t += gSim.combat ? L"Combat" : gSim.rush ? L"Rush distance" : L"Mining";
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
        } else if (gSim.rush) {
            int reached = 0;
            const std::vector<sim::RushRoute> routes = e.rushResult();
            for (const sim::RushRoute& r : routes) reached += r.reached ? 1 : 0;
            t += L"\n" + std::to_wstring(routes.size()) + L" routes, " + std::to_wstring(reached) + L" reached";
        } else {
            t += L"\n" + std::to_wstring(gSim.bases) + L" bases, " + std::to_wstring(gSim.workers) + (gSim.perField ? L" workers for each mineral field" : L" workers");
        }
    }
    if (!simSkipRunning() && !e.liveError().empty()) t += L"\nError: " + widen(e.liveError());
    return t;
}

// The info icon's tip (it shows at once: the icon is there for it; sdl_tips.inc), also the last line of the gear's
// drop-down (rush distance and combat: no icon).
const wchar_t* const kSimInfoTip = L"Experimental: results can differ from the game.";

// The end of the gear's drop-down (rush distance and combat, which have no such buttons on the bar): Report feedback,
// then the info.
void simMenuEnd(std::vector<stardraft::kit::MenuItem>& items) {
    items.push_back(stardraft::kit::MenuItem{}), items.back().separator = true;
    items.push_back(stardraft::kit::MenuItem{});
    items.back().label = L"Report feedback on the simulation\x2026";
    items.back().run = [] { shellPost([] { showFeedbackFilledSdl(L"the simulation", simDiagnostics()); }); };
    items.push_back(stardraft::kit::MenuItem{}), items.back().label = kSimInfoTip, items.back().enabled = false;
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
            shellPost([] { simKeepPlay([] { simulateCombatOf(gSimCombatSelection); }); });  // (again, the same units)
        };
    }
    simMenuEnd(items);
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    stardraft::kit::Menu::popup(*gRoot, gHost->mouse(*gRoot), items, stardraft::kit::Menu::measure(items, &measure), {});
}

// The rush distance's settings (the bar's gear): the workers' race, as Program options > Simulate mining has it (Races);
// a pick saves it and starts the rush again with it.
void simulateRushCommand();
void simRushStart(const RushPoints& p);
void simRushAgain() { simKeepPlay([] { simRushStart(gRushPoints); }); }  // (the gear's changes: the same start and end)
// Whether `unit` is one of another race's units than `races` (SimRaces: 1 Terran, 2 Protoss, 3 Zerg; 0 as in the map:
// none): its worker or a unit of its list.
bool simRushOtherRaceUnit(int unit, int races) {
    static const int kWorkers[3] = {7, 64, 41};  // (SCV, Probe, Drone)
    for (int r = 0; r < 3 && races != 0; ++r)
        if (r + 1 != races && (unit == kWorkers[r] || std::find(kRushRaceUnits[r].units.begin(), kRushRaceUnits[r].units.end(), unit) !=
                                                          kRushRaceUnits[r].units.end()))
            return true;
    return false;
}
// The rush with `unit` (simRushUnit): it is kept only when the rush starts with it (a map with too many routes for
// it: the old unit again).
void simRushSetUnit(int unit) {
    const int old = simRushUnit();
    int races = 0;
    prefs::number(L"SimRaces", races, 0, 3);
    prefs::setNumber(L"SimRushUnit", unit), prefs::setNumber(L"SimRushUnitRaces", races);  // (another race's unit too: as picked)
    simRushAgain();
    if (!gSim.shown || !gSim.rush) prefs::setNumber(L"SimRushUnit", old), simRushAgain();
}
// The gear's Unit: the searchable list of the units that can walk or fly, with their icons (as the Data workspace's
// pickers): the worker, then the race's units (As defined in map: each race's), then all other units. A pick starts the
// rush again with it.
draw::Image sdlDataImage(DataPicture p);  // (sdl_data.inc)
void simRushPickUnit() {
    if (!gRoot || !gHost || !gState.ed) return;
    dataLoadForProject();
    int races = 0;
    prefs::number(L"SimRaces", races, 0, 3);
    std::map<int, ListPickItem> all;  // (the units.dat entries, by number)
    for (ListPickItem& it : entryItems("units"))
        if (const int u = std::atoi(it.value.c_str()); simRushUnitFits(u)) all[u] = std::move(it);
    std::vector<ListPickItem> items;
    items.push_back({tr(L"Worker of the race"), L"", "-1", "", -1});
    for (int r = 0; r < 3; ++r)
        if (races == 0 || r + 1 == races)
            for (int u : kRushRaceUnits[r].units)
                if (auto it = all.find(u); it != all.end()) items.push_back(it->second), items.back().section = tr(kSimRaceNames[r + 1]), all.erase(it);
    for (auto& [u, it] : all) items.push_back(std::move(it)), items.back().section = tr(L"Other units");
    ListPickView v(tr(L"Unit"), std::move(items), std::to_string(simRushUnit()));
    v.iconOf = [](const ListPickItem& it) { return it.iconDat.empty() || it.icon < 0 ? draw::Image{} : sdlDataImage(dataRefPicture(it.iconDat, uint32_t(it.icon))); };
    if (!runListPick(v, gHost->mouse(*gRoot)) || v.result().empty()) return;
    simRushSetUnit(std::atoi(v.result().c_str()));
}
void simRushSettingsMenu() {
    if (!gRoot || !gHost) return;
    int races = 0;
    prefs::number(L"SimRaces", races, 0, 3);
    const int unit = simRushUnit();
    using stardraft::kit::MenuItem;
    std::vector<MenuItem> items;
    auto header = [&](const wchar_t* t) {
        if (!items.empty()) items.push_back(MenuItem{}), items.back().separator = true;
        items.push_back(MenuItem{}), items.back().label = t, items.back().enabled = false;
    };
    // The race of the units first: the unit's list has that race's units first. A new race that does not have the
    // picked unit (another race's unit): the worker.
    header(L"Race");
    for (int k = 0; k < 4; ++k) {
        items.push_back(MenuItem{});
        items.back().label = kSimRaceNames[k], items.back().radio = true, items.back().checked = k == races;
        items.back().run = [k] {
            prefs::setNumber(L"SimRaces", k);
            shellPost([] { simRushAgain(); });  // (again with the new race; simRushStart takes its worker for another race's unit)
        };
    }
    // The unit (its name on the row): a click opens the list of units.
    items.push_back(MenuItem{}), items.back().separator = true;
    {
        static const int kWorkers[4] = {-1, 7, 64, 41};  // (SCV, Probe, Drone)
        const std::wstring name = unit >= 0 ? simUnitName(unit) : races == 0 ? std::wstring(tr(L"Worker of the race")) : simUnitName(kWorkers[races]);
        wchar_t t[256];
        std::swprintf(t, 256, tr(L"Unit: %ls"), name.c_str());
        items.push_back(MenuItem{});
        items.back().label = std::wstring(t) + L"\x2026";
        items.back().run = [] { shellPost([] { simRushPickUnit(); }); };
    }
    // Where they go: from the start locations to each other, or between two points of the map.
    header(L"Start and end");
    items.push_back(MenuItem{});
    items.back().label = L"Start locations", items.back().radio = true, items.back().checked = !gRushPoints.on;
    items.back().run = [] { shellPost([] { simulateRushCommand(); }); };
    items.push_back(MenuItem{});
    items.back().label = L"Two points of the map\x2026", items.back().radio = true, items.back().checked = gRushPoints.on;
    items.back().run = [] { shellPost([] { simulateRushPointsCommand(); }); };
    // The fog of war: off, what all players see, or what the checked players see (Specific players: a check for each
    // player; the last one unchecked: off).
    header(L"Fog of war");
    auto setFog = [](int fog, int players) {
        gSim.fog = players || fog != kFogPlayers ? fog : -1, gSim.fogPlayers = players;
        prefs::setNumber(L"SimRushFog", gSim.fog), prefs::setNumber(L"SimRushFogPlayers", players);
        simRedraw();
    };
    for (int f : {-1, int(sim::kAllPlayers)}) {
        items.push_back(MenuItem{});
        items.back().label = f == -1 ? std::wstring(tr(L"Off")) : std::wstring(tr(L"All players"));
        items.back().radio = true, items.back().checked = f == gSim.fog;
        items.back().run = [f, setFog] {  // (the checked players stay saved, also the ones with no routes on this map)
            int saved = 0;
            prefs::number(L"SimRushFogPlayers", saved, 0, 4095);
            setFog(f, saved);
        };
    }
    MenuItem specific;
    specific.label = L"Specific players", specific.radio = true, specific.checked = gSim.fog == kFogPlayers;
    for (int pl : simFogPlayers()) {
        specific.sub.push_back(MenuItem{});
        specific.sub.back().label = simPlayer(pl);
        specific.sub.back().checked = gSim.fog == kFogPlayers && (gSim.fogPlayers >> pl & 1);
        specific.sub.back().run = [pl, setFog] {
            const int was = gSim.fog == kFogPlayers ? gSim.fogPlayers : 0;
            setFog(kFogPlayers, was ^ (1 << pl));
        };
    }
    if (!specific.sub.empty()) items.push_back(std::move(specific));
    simMenuEnd(items);
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    stardraft::kit::Menu::popup(*gRoot, gHost->mouse(*gRoot), items, stardraft::kit::Menu::measure(items, &measure), {});
}

void simPlayPause();  // (below: Play or Pause)
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
    // A and B: a press on one and a drag moves it; the release starts the rush again from A to B.
    if (gRushDrag && gState.ed && (e.kind == draw::MouseKind::Move || (e.kind == draw::MouseKind::Up && e.button == 0))) {
        {
            float wx = 0, wy = 0;
            toWorld(int(std::lround(float(e.pt.x) - gRushDragGrip.first)), int(std::lround(float(e.pt.y) - gRushDragGrip.second)), wx, wy);
            const std::pair<int, int> at{std::clamp(int(wx), 0, gState.ed->width() * 32 - 1), std::clamp(int(wy), 0, gState.ed->height() * 32 - 1)};
            (gRushDrag == 1 ? gRushPoints.from : gRushPoints.to) = at;
            simRedraw();
        }
        if (e.kind == draw::MouseKind::Up && e.button == 0) {
            const bool moved = (gRushDrag == 1 ? gRushPoints.from : gRushPoints.to) != gRushDragFrom;
            gRushDrag = 0;
            const RushPoints p = gRushPoints;
            if (moved) shellPost([p] { simKeepPlay([p] { simRushStart(p); }); });  // (a click with no move: as it was)
        }
        return true;
    }
    if ((e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::DoubleClick) && e.button == 0 && !hit(l.bar))
        if (const int n = simRushMarkerAt(e.pt.x, e.pt.y)) {
            gRushDragFrom = n == 1 ? gRushPoints.from : gRushPoints.to;
            const float zoom = std::max(0.01f, gState.zoom);
            gRushDragGrip = {float(e.pt.x) - (float(gRushDragFrom.first) - gState.camX) * zoom, float(e.pt.y) - (float(gRushDragFrom.second) - gState.camY) * zoom};
            return gRushDrag = n, simRedraw(), true;
        }
    if (simPanelMouse(e, l, r)) {
        if (gSim.hot != -1) gSim.hot = -1, simRedraw();
        return true;
    }
    const int over = e.kind == draw::MouseKind::Leave ? -1 : hit(l.first) ? 0 : (gSim.done && hit(l.second)) ? 1 : hit(l.third) ? 2 : hit(l.chip) ? 3
                     : hit(l.minus) ? 4 : hit(l.plus) ? 5 : hit(l.skip) ? 6 : hit(l.gear) ? 7 : hit(l.feedback) ? 8 : hit(l.play) ? 10 : -1;
    if (over != gSim.hot) gSim.hot = over, simRedraw();
    if (e.kind == draw::MouseKind::Up && e.button == 0 && gSim.pressed >= 0) {
        const int was = gSim.pressed;
        gSim.pressed = -1;
        simRedraw();
        if (was != over || was == 3) return true;  // (let go elsewhere, or on the info icon: nothing)
        if (was == 2) {
            const bool again = gSim.done;
            simSeek(0);
            if (again) gSim.playing = true, gSim.due = 0, gSim.last = std::chrono::steady_clock::now();  // (Play again: it plays)
        }
        else if (was == 10) simPlayPause();
        else if (was == 0) gSimPanel.open = false, simClose();
        else if (was == 1) shellPost([] { simShowReport(); });
        else if (was == 4 || was == 5) simStepSpeed(was == 4 ? -1 : 1), simRedraw();
        else if (was == 6) simSkipToEnd();
        else if (was == 7 && !gSim.combat && !gSim.rush) gSimPanel.open = !gSimPanel.open, gSimPanel.hot = gSimPanel.down = 0, simRedraw();
        else if (was == 7) shellPost([] { gSim.combat ? simCombatSettingsMenu() : simRushSettingsMenu(); });
        else if (was == 8) shellPost([] { showFeedbackFilledSdl(L"the simulation", simDiagnostics()); });
        return true;
    }
    const bool in = hit(l.bar);
    if (!in && gSimPanel.open && e.kind == draw::MouseKind::Down) {  // (a press elsewhere closes the panel)
        gSimPanel.open = false, simRedraw();
        return true;
    }
    if (!in) return false;
    // (the bar takes the mouse; a press does what it does; a quick second click is a press too, not a double click)
    if ((e.kind != draw::MouseKind::Down && e.kind != draw::MouseKind::DoubleClick) || e.button != 0) return true;
    if (!gSim.combat && !gSim.rush && hit(l.trackHit)) {
        gSim.seeking = true;
        seekAt();
        return true;
    }
    if (over >= 0) gSim.pressed = over, simRedraw();
    return true;
}

bool simTipIsQuick(const std::wstring& tip) { return tip == kSimInfoTip; }

// The bar's tooltips: the info icon (what the simulation is) and the buttons.
std::wstring simBarTip(draw::Point pt, draw::Rect r) {
    if (!gSim.shown) return {};
    const SimBarLayout l = simLayout(r);
    auto in = [&](draw::Rect b) { return pt.x >= b.left && pt.x < b.right && pt.y >= b.top && pt.y < b.bottom; };
    if (std::wstring t = simPanelTip(l, r, pt); !t.empty()) return t;
    if (in(l.chip)) return kSimInfoTip;
    if (const int n = in(l.bar) ? 0 : simRushMarkerAt(pt.x, pt.y)) return n == 1 ? L"The start point (drag it)" : L"The end point (drag it)";
    if (in(l.third)) return gSim.done ? L"Play again from the start" : L"Restart";
    if (in(l.play)) return gSim.playing ? L"Pause (Space)" : L"Play (Space)";
    if (in(l.minus)) return L"Slower";
    if (in(l.plus)) return L"Faster";
    if (in(l.skip)) return L"Skip to the end";
    if (in(l.gear)) return gSim.combat ? L"Who fights whom" : gSim.rush ? L"Units, start and end" : L"Settings and boost searches";
    if (in(l.feedback)) return L"Report feedback on the simulation";
    if (in(l.second)) return gSim.done ? L"Show the report" : L"Speed";
    if (in(l.first)) return gSim.done ? L"Close" : L"Cancel the simulation";
    return {};
}

// Play (a new game waits at its start) or Pause.
void simPlayPause() {
    if (!gSim.shown || gSim.done) return;
    gSim.playing = !gSim.playing;
    gSim.due = 0, gSim.last = std::chrono::steady_clock::now();
    simRedraw();
}
// Tools > Simulation: the bar's commands (the menu's items, with their keys): enabled only while the bar shows.
bool simCommandEnabled(int id) {
    if (id == ID_SIM_OPTIONS) return true;
    if (!gSim.shown) return false;
    if (id == ID_SIM_PLAY || id == ID_SIM_SLOWER || id == ID_SIM_FASTER || id == ID_SIM_SKIP) return !gSim.done && !gRushDrag;
    return true;
}
void simCommand(int id) {
    if (!simCommandEnabled(id)) return;
    switch (id) {
    case ID_SIM_PLAY: simPlayPause(); break;
    case ID_SIM_RESTART: simSkipJoin(), simSeek(0); break;
    case ID_SIM_SKIP: simSkipToEnd(); break;
    case ID_SIM_SLOWER: case ID_SIM_FASTER: simStepSpeed(id == ID_SIM_SLOWER ? -1 : 1), simRedraw(); break;
    case ID_SIM_SETTINGS:
        if (!gSim.combat && !gSim.rush) gSimPanel.open = true, gSimPanel.hot = gSimPanel.down = 0, simRedraw();
        else shellPost([] { gSim.combat ? simCombatSettingsMenu() : simRushSettingsMenu(); });
        break;
    case ID_SIM_OPTIONS: showOptionsTab(L"Simulation"); break;
    case ID_SIM_CLOSE: gSimPanel.open = false, simClose(); break;
    default: break;
    }
}
bool simKey(const draw::KeyEvent& e) {
    if (gSim.shown && e.down && e.key == draw::Key::Space && !e.mods && !gSim.done && !gRushDrag) return simPlayPause(), true;
    if (!gSim.shown || !e.down || e.key != draw::Key::Escape) return false;
    if (gSimPanel.open) {  // (Esc: the panel first, then the simulation)
        gSimPanel.open = false, simRedraw();
        return true;
    }
    simClose();
    return true;
}

bool gSimShotStepping = false;  // (--script's simstep: only it runs the game's frames, for even pictures)
void simFrame() {
    if (!gSim.shown) return;
    if (gState.ed.get() != gSim.ed) return simClose();  // (another tab, or the map closed)
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - gSim.last).count();
    gSim.last = now;
    simHold();  // (the shields follow the panels: a window resized, a panel opened)
    if (!gSim.combat && !gSim.rush && !simSkipRunning() && !gSimShotStepping) {  // (the boost searches: on or off, their state)
        simBoostSync();
        if (simEngine().boosting()) simEngine().step(0), simRedraw();
    }
    if (gSim.done || gSimShotStepping || gRushDrag || !gSim.playing) return;  // (it waits: for Play, or while a point moves)
    // The game's frames due (Fastest: 42 ms a frame, times the speed), run in small steps for at most 12 ms of each
    // window frame, so the program stays usable at any speed: the frames that do not fit are dropped (the game goes
    // slower than the speed, and the bar's spinner says so). Skip to the end: as many as fit, until the end.
    using clock = std::chrono::steady_clock;
    const auto budget = now + std::chrono::milliseconds(12);
    sim::Simulator& e = simEngine();
    bool ran = false, ended = false;
    if (gSim.skipping) {
        if (gSimSkipEnded || gSimSkipReached) {
            ended = simSkipJoin(), simSounds(false);
            if (!ended) gSim.skipping = false, gSim.due = 0;  // (a seek's frame: the game goes on from there)
        }
    } else {
        gSim.due += std::min(ms, 250.0) * gSim.speed / 42.0;
        while (gSim.due >= 1 && !ended && clock::now() < budget) {  // (a frame at a time: a large map's frame is slow)
            gSim.due -= 1, ran = true;
            ended = !e.step(1);
        }
        gSim.behind = gSim.due >= 8;
        gSim.due = std::min(gSim.due, 1.0);  // (what did not fit is dropped: no backlog)
        if (ran) simSounds(true);
    }
    if (ended) {
        const bool skipped = gSim.skipping && gSim.toEnd;
        gSim.skipping = gSim.behind = false;
        simEnd();
        if (skipped) shellPost([] { simShowReport(); });
    }
    simRedraw();  // (the next frame: the game goes on)
}

// Tools > Simulate mining: at each start location, placed resource depot and group of mineral fields (at most 8), the
// workers of Program options > Simulation (default 9) on the mineral fields within 12 tiles, 30 seconds, then the
// measured game minutes (default 3); the races as in the map (a random race for Random and a race picked in the lobby)
// or one race. Each base mines in a game of its own (bar_sim.h). At the game's speed (the bar's - and + change it).
// The mining's options from the settings; `seed`: the random races (the same seed: the same races).
sim::MiningOptions simMiningOptions(uint32_t seed) {
    const SimSettings s = simSettings();
    sim::MiningOptions o;
    o.workers = {s.kind == 0 ? 0 : s.total};
    o.workersPerField = s.kind == 0 ? s.perField : 0;
    o.frames = int(s.minutes * 60 * 1000 / 42);
    static const sim::Race kRace[4] = {sim::Terran, sim::Terran, sim::Protoss, sim::Zerg};
    o.races = {kRace[s.races]};
    o.mapRaces = s.races == 0;
    o.seed = seed;
    return o;
}
void simulateMiningCommand() {
    const uint32_t seed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());  // (Random: another race each time)
    const sim::MiningOptions o = simMiningOptions(seed);
    if (!simBegin(false, [o](std::string& err) { return simEngine().startMining(o, err); })) return;
    gState.ed->clearSelection();  // (the map's selection does not show over the game)
    const SimSettings s = simSettings();
    gSim.speed = 1, gSim.workers = s.workers(), gSim.perField = s.kind == 0, gSim.bases = int(simEngine().miningResult().size()), gSim.seed = seed;
    simBoostSync();  // (the boost searches that are on)
}
// The mining again from its start with the settings' game time, on the same engine: the bases and their boosts stay.
void simMiningNewTime() {
    if (!gSim.shown || gSim.combat || gSim.rush) return;
    simSkipJoin();
    gSim.skipping = false;
    const sim::MiningOptions o = simMiningOptions(gSim.seed);
    gSim.start = [o](std::string& err) { return simEngine().startMining(o, err); };
    std::string err;
    if (!simEngine().setMap(gSim.chk, err) || !gSim.start(err)) return simClose();
    gSim.done = false, gSim.due = 0, gSim.last = std::chrono::steady_clock::now();
    simRedraw();
}

// Tools > Simulate rush distance: a worker walks from each start location to each other start location (each route
// a game of its own, all at the same time), until all arrive or stop; the races as Simulate mining has them (the bar's
// gear). The result: the shortest and longest walk in game seconds; the report: each route's time and length.
// The rush with these points (off: the start locations); they are the rush's when it starts.
void simRushStart(const RushPoints& p) {
    int races = 0;
    prefs::number(L"SimRaces", races, 0, 3);
    sim::RushOptions o;
    static const sim::Race kRace[4] = {sim::Terran, sim::Terran, sim::Protoss, sim::Zerg};
    o.races = {kRace[races]};
    o.mapRaces = races == 0;
    o.seed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());  // (Random: another race each time)
    // A race changed since the unit was picked (the gear's Race, the mining's Races): another race's unit than the new
    // one's goes, the worker comes (setting SimRushUnitRaces: the race when the unit was picked).
    int unitRaces = races;
    prefs::number(L"SimRushUnitRaces", unitRaces, 0, 3);
    if (unitRaces != races) {
        if (simRushOtherRaceUnit(simRushUnit(), races)) prefs::setNumber(L"SimRushUnit", -1);
        prefs::setNumber(L"SimRushUnitRaces", races);
    }
    o.units = {simRushUnit()};
    const bool points = p.on && gState.ed && p.ed == gState.ed.get();
    if (points) o.points = true, o.fromX = p.from.first, o.fromY = p.from.second, o.toX = p.to.first, o.toY = p.to.second;
    int fog = -1, fogPlayers = 0;  // (the gear's Fog of war: SimRushFog -1 off, -2 all players, -3 SimRushFogPlayers' players)
    prefs::number(L"SimRushFog", fog, kFogPlayers, -1), prefs::number(L"SimRushFogPlayers", fogPlayers, 0, 4095);
    if (!simBegin(false, [o](std::string& err) { return simEngine().startRush(o, err); }, true)) return;
    gRushPoints = p, gRushPoints.on = points;
    if (points) gRushLastPoints = gRushPoints;
    gState.ed->clearSelection();  // (the map's selection does not show over the game)
    gSim.speed = 1;
    int routes = 0;  // (the checked players that have no routes now are not shown; none left: off)
    for (int pl : simFogPlayers()) routes |= 1 << pl;
    gSim.fogPlayers = fogPlayers & routes;
    gSim.fog = fog == kFogPlayers && !gSim.fogPlayers ? -1 : fog;
}
// Tools > Simulate rush distance: from the start locations.
void simulateRushCommand() { simRushStart(RushPoints{}); }
// The gear's Two points of the map (the command edit.simulate.rushpoints: no menu item): the units go from A to B, two
// points on the map that a drag moves (each drag starts the rush again). The points stay while the map is open; at
// their first use, at two start locations (simRushDefaultPoints).
void simulateRushPointsCommand() {
    RushPoints p = gRushLastPoints.ed == gState.ed.get() ? gRushLastPoints : simRushDefaultPoints();
    p.on = true;
    simKeepPlay([p] { simRushStart(p); });
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

// --script's simstep <frames> (a snapshot): the game that shows runs that many frames at once; its frame after them (-1:
// no game).
int simStepForShot(int frames) {
    if (!gSim.shown || !gSim_) return -1;
    simSkipJoin();
    gSimShotStepping = true;
    sim::Simulator& e = simEngine();
    for (int n = 0; n < frames && !gSim.done; n += 60)
        if (!e.step(std::min(60, frames - n))) simEnd();
    simSounds(false);
    gSim.last = std::chrono::steady_clock::now(), gSim.due = 0;
    simRedraw();
    return e.frame();
}

// --dialog simreport:mining, simreport:rush or simreport:combat (a snapshot): that simulation of the map runs to its end at once (combat:
// the units of the map's first two players), and its report's window is the picture.
draw::View* sdlSimReportShot(const std::string& arg, int& w, int& h) {
    if (arg != "simreport:mining" && arg != "simreport:combat" && arg != "simreport:rush") return nullptr;
    changelog::setHtmlHooks(false);
    if (arg == "simreport:combat" && gState.ed && gState.ed->isOpen()) {  // 12 Marines and 4 Medics (Player 1), 6 Zealots and 4 Dragoons (Player 2)
        sim::CombatOptions o;
        o.a = {{0, 12}, {34, 4}}, o.b = {{65, 6}, {66, 4}};
        std::map<int, std::wstring> names;
        for (int t : {0, 34, 65, 66}) names[t] = widen(gState.ed->unitTypes().statText(uint32_t(t)));
        if (simBegin(true, [o](std::string& err) { return simEngine().startCombat(o, err); }))
            gSim.players[0] = 0, gSim.players[1] = 1, gSim.unitNames = names;
    } else if (arg == "simreport:rush") {
        simulateRushCommand();
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
bool simRushPointCursor(int, int, draw::Rect) { return false; }
bool simRushDragging() { return false; }
void simRushDragLost() {}
int simStepForShot(int) { return -1; }
void simPaintBar(draw::Canvas&, draw::Rect) {}
bool simBarMouse(const draw::MouseEvent&, draw::Rect) { return false; }
std::wstring simBarTip(draw::Point, draw::Rect) { return {}; }
bool simTipIsQuick(const std::wstring&) { return false; }
bool simKey(const draw::KeyEvent&) { return false; }
bool simCommandEnabled(int) { return false; }
void simCommand(int) {}
void simFrame() {}
draw::View* sdlSimReportShot(const std::string&, int&, int&) { return nullptr; }

#endif
