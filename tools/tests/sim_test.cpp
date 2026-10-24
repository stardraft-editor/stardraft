// The simulation (tools/sim) against the reference results (docs/dev/SIMULATION.md): mining at a start location of
// (2)Bottleneck.scm, the 15 fights of the combat reference with their time series, two movement traces, a long path,
// and the live API. It prints each measure beside the reference number.
//
//     sim_test [<install>] [--series] [--seeds] [--traces <file>] [--trace <fight>]
//              [--check <file>]
//     --series: each fight's time series (HP and shields every 24 frames) beside the reference.
//     --seeds: also each fight with 20 other random seeds (the winners, the median length, and the mean series and
//              frame differences over the seeds).
// The CHECK section (tools/tests/sim-reference/reference-check.md: 15 other fights and the mining of
// (4)Orbital Relay.scm) is a held-out set: it is only reported, and the engine is not tuned to it.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "sim.h"
#include "stardraft/creep.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "world.h"

namespace {

const char* raceName(sim::Race r) { return r == sim::Terran ? "Terran" : r == sim::Protoss ? "Protoss" : "Zerg"; }

// A fight of the reference (tools/tests/sim-reference/reference-combat.md): the armies, the result, and HP + shields
// every 24 frames (the last value: at the end).
struct Scenario {
    const char* name;
    std::vector<sim::Army> a, b;
    char winner;
    int frames, aliveA, hpA, shA, aliveB, hpB, shB;
    std::vector<int> seriesA, seriesB;
};

const Scenario kScenarios[] = {
    {"12 Marine vs 20 Zergling", {{0, 12}}, {{37, 20}}, 'B', 204, 0, 0, 0, 8, 239, 0,
     {480, 480, 410, 355, 290, 240, 160, 85, 20, 0}, {700, 664, 552, 478, 419, 361, 303, 261, 245, 239}},
    {"6 Zealot vs 12 Marine", {{65, 6}}, {{0, 12}}, 'A', 288, 3, 266, 103, 0, 0, 0,
     {960, 912, 819, 718, 657, 594, 509, 475, 435, 412, 388, 379, 369},
     {480, 480, 432, 384, 328, 280, 224, 192, 152, 96, 64, 24, 0}},
    {"12 Marine + 4 Medic vs 20 Zergling + 4 Hydralisk", {{0, 12}, {34, 4}}, {{37, 20}, {38, 4}}, 'B', 308, 4, 236, 0,
     7, 215, 0, {720, 701, 671, 621, 601, 569, 535, 511, 478, 435, 380, 335, 278, 236},
     {1020, 978, 889, 790, 707, 605, 534, 459, 418, 343, 284, 255, 227, 215}},
    {"1 Marine vs 1 Zergling", {{0, 1}}, {{37, 1}}, 'B', 101, 0, 0, 0, 1, 0, 0, {40, 40, 30, 20, 5, 0},
     {35, 29, 23, 11, 6, 0}},
    {"8 Hydralisk vs 8 Dragoon", {{38, 8}}, {{66, 8}}, 'B', 270, 0, 0, 0, 6, 503, 293,
     {640, 640, 520, 450, 370, 340, 265, 180, 92, 80, 80, 5, 0},
     {1440, 1420, 1271, 1162, 1093, 1022, 949, 909, 862, 828, 811, 805, 796}},
    {"10 Zergling vs 4 Zealot", {{37, 10}}, {{65, 4}}, 'B', 229, 0, 0, 0, 3, 170, 3,
     {350, 350, 302, 251, 213, 162, 127, 89, 33, 3, 0}, {640, 640, 570, 476, 388, 315, 253, 215, 182, 176, 173}},
    {"4 Siege Tank vs 12 Zergling", {{5, 4}}, {{37, 12}}, 'B', 301, 0, 0, 0, 5, 160, 0,
     {600, 600, 584, 552, 488, 418, 358, 290, 206, 150, 134, 86, 34, 0},
     {420, 385, 335, 335, 285, 250, 250, 215, 210, 180, 175, 175, 160, 160}},
    {"2 Reaver vs 16 Zergling", {{83, 2}}, {{37, 16}}, 'A', 114, 2, 200, 126, 0, 0, 0, {360, 360, 360, 350, 336, 326},
     {560, 560, 35, 35, 35, 0}},
    {"2 Carrier vs 12 Hydralisk", {{72, 2}}, {{38, 12}}, 'B', 353, 0, 0, 0, 11, 800, 0,
     {900, 900, 800, 682, 630, 546, 483, 422, 378, 325, 315, 294, 246, 181, 68, 0},
     {960, 960, 960, 948, 900, 888, 880, 874, 838, 832, 804, 798, 798, 800, 800, 800}},
    {"12 Mutalisk vs 6 Goliath", {{43, 12}}, {{3, 6}}, 'A', 291, 10, 1024, 0, 0, 0, 0,
     {1440, 1410, 1340, 1280, 1251, 1201, 1161, 1121, 1102, 1062, 1043, 1034, 1024, 1024},
     {750, 750, 641, 574, 522, 433, 336, 285, 230, 158, 97, 49, 9, 0}},
    {"4 Lurker vs 12 Marine", {{103, 4}}, {{0, 12}}, 'A', 113, 4, 417, 0, 0, 0, 0, {500, 475, 415, 415, 416, 417},
     {480, 480, 400, 160, 20, 0}},
    {"8 Firebat vs 16 Zergling", {{32, 8}}, {{37, 16}}, 'A', 96, 7, 294, 0, 0, 0, 0, {400, 392, 340, 304, 294},
     {560, 560, 171, 21, 0}},
    {"6 Vulture vs 8 Zealot", {{2, 6}}, {{65, 8}}, 'B', 271, 0, 0, 0, 5, 464, 240,
     {480, 480, 464, 392, 344, 272, 256, 192, 112, 80, 64, 32, 0},
     {1280, 1240, 1123, 1042, 983, 889, 883, 806, 749, 741, 742, 723, 704}},
    {"4 Ultralisk vs 16 Marine", {{39, 4}}, {{0, 16}}, 'A', 227, 4, 1021, 0, 0, 0, 0,
     {1600, 1560, 1430, 1310, 1242, 1157, 1112, 1069, 1039, 1026, 1021},
     {640, 600, 560, 460, 400, 300, 240, 160, 100, 20, 0}},
    {"12 Scourge vs 2 Battlecruiser", {{47, 12}}, {{12, 2}}, 'B', 52, 0, 0, 0, 2, 679, 0, {300, 250, 125, 0},
     {1000, 1000, 679, 679}},
};

struct Fight {
    char winner = '-';
    int frames = 0, alive[2] = {}, hp[2] = {}, sh[2] = {}, firstShot[2] = {}, firstDamage[2] = {};
    std::vector<int> series[2];
};

// HP + shields of a side's living units (whole points, rounded down as the reference counts them).
void sums(const sim::World& w, int side, int& alive, int& hp, int& sh) {
    alive = hp = sh = 0;
    for (const sim::Unit& u : w.units)
        if (u.alive && !u.fighter && !u.neutral && u.side == side) {
            ++alive;
            hp += u.hp >> 8;
            sh += u.shields >> 8;
        }
}

Fight runFight(const sim::Tables& t, const sim::MapInfo& map, const Scenario& s, uint32_t seed,
               const std::function<void(const sim::World&)>& onFrame = {}, const int (*centres)[2] = nullptr) {
    Fight f;
    sim::World w(t, map);
    w.seed = seed;
    if (centres)
        for (int side = 0; side < 2; ++side) {
            w.fixedCentres[side][0] = centres[side][0];
            w.fixedCentres[side][1] = centres[side][1];
        }
    sim::CombatOptions o;
    o.a = s.a;
    o.b = s.b;
    o.players[1] = 1;
    o.x = 3808;  // (the first start location)
    o.y = 2384;
    std::string error;
    if (!sim::setupCombat(w, o, error)) return f;
    for (;;) {
        if (onFrame) onFrame(w);
        bool over = sim::combatOver(w, o.frames);
        if (w.frame % 24 == 0 || over)
            for (int side = 0; side < 2; ++side) {
                int alive, hp, sh;
                sums(w, side, alive, hp, sh);
                f.series[side].push_back(hp + sh);
            }
        if (over) break;
        w.step();
    }
    f.frames = w.frame;
    for (int side = 0; side < 2; ++side) {
        sums(w, side, f.alive[side], f.hp[side], f.sh[side]);
        f.firstShot[side] = w.firstShot[side];
        f.firstDamage[side] = w.firstDamage[side];
    }
    if (!w.timedOut) f.winner = w.helpless >= 0 ? (w.helpless ? 'A' : 'B') : f.alive[0] ? 'A' : 'B';
    return f;
}

// How far a time series is from the reference: the mean difference, in percent of the side's start.
double seriesError(const std::vector<int>& m, const std::vector<int>& r) {
    if (r.empty() || r[0] == 0) return 0;
    double sum = 0;
    size_t n = std::max(m.size(), r.size());
    for (size_t i = 0; i < n; ++i) {
        int a = i < m.size() ? m[i] : m.back(), b = i < r.size() ? r[i] : r.back();
        sum += std::abs(a - b);
    }
    return 100.0 * sum / double(n) / r[0];
}

// The reference traces (tools/tests/sim-reference/reference-traces.md): its Markdown tables, read as they are.
std::vector<std::string> cellsOf(const std::string& line) {
    std::vector<std::string> out;
    size_t start = line.find('|');
    while (start != std::string::npos) {
        size_t end = line.find('|', start + 1);
        if (end == std::string::npos) break;
        std::string c = line.substr(start + 1, end - start - 1);
        size_t a = c.find_first_not_of(' '), b = c.find_last_not_of(' ');
        out.push_back(a == std::string::npos ? "" : c.substr(a, b - a + 1));
        start = end;
    }
    return out;
}

struct Table {
    std::string title;  // (the heading above it)
    std::vector<std::vector<std::string>> rows;  // (without the header and the --- line)
};

std::vector<Table> readTables(const std::filesystem::path& path) {
    std::vector<Table> tables;
    std::FILE* f = nullptr;
#ifdef _WIN32
    _wfopen_s(&f, path.c_str(), L"rb");
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return tables;
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    std::string title;
    bool inTable = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = end == std::string::npos ? text.size() : end + 1;
        if (line.rfind("#", 0) == 0) {
            title = line;
            inTable = false;
        } else if (line.rfind("|", 0) == 0) {
            if (!inTable) {
                tables.push_back({title, {}});
                inTable = true;
            } else if (line.rfind("|---", 0) != 0 && !cellsOf(line).empty()) {
                tables.back().rows.push_back(cellsOf(line));
            }
        } else {
            inTable = false;
        }
    }
    return tables;
}

// A unit's place in a trace cell ("x,y hp/shields"); false: dead or empty.
bool cellPlace(const std::string& c, int& x, int& y) { return std::sscanf(c.c_str(), "%d,%d", &x, &y) == 2; }

// The traces against the engine: the placement of units created at one point, the crowding move of 16 Zerglings, and
// the per-unit positions of some fights (the mean distance where both have the unit alive).
void compareTraces(const std::vector<Table>& tables, const sim::Tables& t, const sim::MapInfo& map, int detail) {
    for (const Table& tb : tables) {
        int type = tb.title.find("Marines") != std::string::npos     ? 0
                   : tb.title.find("Zerglings:") != std::string::npos ? 37
                   : tb.title.find("Dragoons") != std::string::npos  ? 66
                                                                     : -1;
        if (type < 0 || tb.title.rfind("###", 0) != 0) continue;
        sim::World w(t, map);
        int exact = 0;
        double sum = 0;
        for (const auto& row : tb.rows) {
            int x = 3680, y = 2384, rx = 0, ry = 0;
            w.freeSpot(type, x, y);
            w.add(type, 0, x, y);
            if (row.size() < 3) continue;
            rx = std::atoi(row[1].c_str());
            ry = std::atoi(row[2].c_str());
            exact += x == rx && y == ry ? 1 : 0;
            sum += std::hypot(x - rx, y - ry);
        }
        std::printf("Placement of %zu %s: %d exact, mean distance %.1f px\n", tb.rows.size(),
                    type == 0 ? "Marines" : type == 37 ? "Zerglings" : "Dragoons", exact,
                    sum / double(std::max<size_t>(1, tb.rows.size())));
    }
    for (const Table& tb : tables) {
        if (tb.title.find("Crowding") == std::string::npos) continue;
        sim::World w(t, map);
        w.combat = true;
        for (const sim::MapInfo::Placed& p : map.units)  // (the map's own units stay, as in the fights)
            if (p.owner > 1 && p.type != 214) {
                sim::Unit& n = w.units[size_t(w.add(p.type, p.owner, p.x, p.y))];
                n.neutral = true;
                n.side = 2;
            }
        std::vector<int> ids;
        for (int n = 0; n < 16; ++n) {
            int x = 3680, y = 2384;
            w.freeSpot(37, x, y);
            ids.push_back(w.add(37, 0, x, y));
            sim::Unit& u = w.units.back();
            u.heading = u.velDir = t.units[37].direction * 8;
            u.amX = 3936;  // (all to the point, as in the reference)
            u.amY = 2384;
            w.setOrder(u, sim::AttackMove);
        }
        double all = 0;
        int rows = 0;
        std::printf("Crowding, 16 Zerglings: mean distance from the reference by frame:");
        for (const auto& row : tb.rows) {
            int frame = std::atoi(row[0].c_str());
            while (w.frame < frame) w.step();
            double sum = 0;
            int n = 0, x, y;
            const char* p = row.size() > 1 ? row[1].c_str() : "";
            for (int id : ids) {
                if (std::sscanf(p, "%d,%d", &x, &y) != 2) break;
                sum += std::hypot(w.units[size_t(id)].x - x, w.units[size_t(id)].y - y);
                if (detail == 100) std::printf(" %d,%d|%d,%d", w.units[size_t(id)].x, w.units[size_t(id)].y, x, y);
                ++n;
                p = std::strchr(p, ' ');
                if (!p) break;
                ++p;
            }
            if (detail == 100) std::printf("  <- frame %d\n", frame);
            if (!n) continue;
            all += sum / n;
            ++rows;
            if (frame % 40 == 0) std::printf(" %d: %.0f", frame, sum / n);
        }
        std::printf("; mean %.1f px\n", rows ? all / rows : 0);
    }
    for (const Table& tb : tables) {
        int n = 0;
        if (std::sscanf(tb.title.c_str(), "## %d.", &n) != 1 || tb.title.find("per unit") == std::string::npos)
            continue;
        if (n < 1 || n > int(std::size(kScenarios)) || tb.rows.empty()) continue;
        // The names: A1 is the last unit of side A that was created (the reference's roster order).
        std::vector<std::pair<std::string, int>> names;  // (column, unit index)
        double sum = 0, early = 0;
        int cells = 0, earlyCells = 0, aliveDiff = 0, total = 0;
        size_t next = 0;
        runFight(t, map, kScenarios[size_t(n - 1)], 42, [&](const sim::World& w) {
            if (names.empty()) {
                std::vector<int> side[2];
                for (const sim::Unit& u : w.units)
                    if (!u.fighter && !u.neutral) side[u.side].push_back(u.id);
                for (int s = 0; s < 2; ++s)
                    for (size_t k = 0; k < side[s].size(); ++k)
                        names.push_back({std::string(1, s ? 'B' : 'A') + std::to_string(k + 1),
                                         side[s][side[s].size() - 1 - k]});
            }
            while (next < tb.rows.size() && std::atoi(tb.rows[next][0].c_str()) < w.frame) ++next;
            if (next >= tb.rows.size() || std::atoi(tb.rows[next][0].c_str()) != w.frame) return;
            const auto& row = tb.rows[next];
            for (size_t c = 1; c < row.size() && c - 1 < names.size(); ++c) {
                const sim::Unit& u = w.units[size_t(names[c - 1].second)];
                int x, y;
                bool refAlive = cellPlace(row[c], x, y);
                ++total;
                if (refAlive != u.alive) ++aliveDiff;
                if (!refAlive || !u.alive) continue;
                double d = std::hypot(u.x - x, u.y - y);
                if (n == detail && (w.frame <= 64 || w.frame % 32 == 0))
                    std::printf("   f%d %s (%d,%d)|(%d,%d) %.0f hp %d|%s\n", w.frame, names[c - 1].first.c_str(), u.x,
                                u.y, x, y, d, u.hp >> 8, row[c].c_str());
                sum += d;
                ++cells;
                if (w.frame <= 48) {
                    early += d;
                    ++earlyCells;
                }
            }
        });
        std::printf("Trace of fight %d: mean distance %.1f px (frames 0 to 48: %.1f px), alive differs in %d of %d "
                    "cells\n",
                    n, cells ? sum / cells : 0, earlyCells ? early / earlyCells : 0, aliveDiff, total);
    }
}

// The held-out check set (reference-check.md), read from its text: each fight's title, result, survivors, time series
// and seeds, the choke's centres, and the mining table.
struct CheckFight {
    Scenario s;
    std::string title;
    int seedWinsA = -1, seedWinsB = -1;
    double seedMedian = 0;
    int centres[2][2] = {{-1, -1}, {-1, -1}};
};

int unitByName(std::string name) {
    static const std::pair<const char*, int> kNames[] = {
        {"Zergling", 37}, {"Zealot", 65},   {"Hydralisk", 38}, {"Siege Tank", 5},    {"Marine", 0},
        {"Medic", 34},    {"Dragoon", 66},  {"Goliath", 3},    {"Wraith", 8},        {"Mutalisk", 43},
        {"Corsair", 60},  {"Archon", 68},   {"Ultralisk", 39}, {"Firebat", 32},      {"Battlecruiser", 12},
        {"Lurker", 103},  {"Reaver", 83},   {"Carrier", 72},   {"Dark Templar", 61}, {"Vulture", 2},
        {"Scourge", 47}};
    size_t paren = name.find(" (");
    if (paren != std::string::npos) name.resize(paren);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    for (const auto& n : kNames)
        if (name == n.first) return n.second;
    return -1;
}

std::vector<sim::Army> armyOf(const std::string& text) {  // "12 Marine + 4 Medic"
    std::vector<sim::Army> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(" + ", pos);
        std::string part = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        int count = std::atoi(part.c_str());
        size_t space = part.find(' ');
        int type = space == std::string::npos ? -1 : unitByName(part.substr(space + 1));
        if (count > 0 && type >= 0) out.push_back({type, count});
        pos = end == std::string::npos ? text.size() : end + 3;
    }
    return out;
}

std::vector<CheckFight> readCheck(const std::filesystem::path& path, std::vector<std::vector<std::string>>& mining) {
    std::vector<CheckFight> fights;
    std::FILE* f = nullptr;
#ifdef _WIN32
    _wfopen_s(&f, path.c_str(), L"rb");
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return fights;
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    int choke[2][2] = {{-1, -1}, {-1, -1}}, survivorSide = -1;
    bool inMining = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = end == std::string::npos ? text.size() : end + 1;
        int x, y;
        for (int side = 0; side < 2; ++side) {  // ("Side A centre: (x, y)", both on one line)
            const char* at = std::strstr(line.c_str(), side ? "Side B centre: (" : "Side A centre: (");
            if (at && std::sscanf(at + 16, "%d, %d", &x, &y) == 2) choke[side][0] = x, choke[side][1] = y;
        }
        if (line.rfind("## ", 0) == 0) {
            inMining = line.find("Mining") != std::string::npos;
            int number = std::atoi(line.c_str() + 3);
            size_t dot = line.find(". ");
            if (number <= 0 || dot == std::string::npos) continue;
            CheckFight c;
            c.title = line.substr(dot + 2);
            std::string body = c.title;
            bool isChoke = body.rfind("Choke: ", 0) == 0;
            if (isChoke) body = body.substr(7);
            size_t comma = body.find(',');
            if (comma != std::string::npos) body.resize(comma);
            size_t vs = body.find(" vs ");
            if (vs == std::string::npos) continue;
            c.s.a = armyOf(body.substr(0, vs));
            c.s.b = armyOf(body.substr(vs + 4));
            if (isChoke) std::memcpy(c.centres, choke, sizeof choke);
            fights.push_back(c);
            survivorSide = -1;
            continue;
        }
        if (inMining) {
            if (line.rfind("| (", 0) == 0) mining.push_back(cellsOf(line));
            continue;
        }
        if (fights.empty()) continue;
        CheckFight& c = fights.back();
        char winner;
        int frames;
        if (std::sscanf(line.c_str(), "- Winner: %c. Frames: %d", &winner, &frames) == 2) {
            c.s.winner = winner;
            c.s.frames = frames;
        } else if (line.rfind("Side A survivors", 0) == 0) {
            survivorSide = 0;
        } else if (line.rfind("Side B survivors", 0) == 0) {
            survivorSide = 1;
        } else if (line.rfind("| Total |", 0) == 0 && survivorSide >= 0) {
            std::vector<std::string> cells = cellsOf(line);
            if (cells.size() >= 5) {
                int alive = std::atoi(cells[2].c_str()), hp = std::atoi(cells[3].c_str()),
                    sh = std::atoi(cells[4].c_str());
                (survivorSide ? c.s.aliveB : c.s.aliveA) = alive;
                (survivorSide ? c.s.hpB : c.s.hpA) = hp;
                (survivorSide ? c.s.shB : c.s.shA) = sh;
            }
        } else if (line.rfind("| ", 0) == 0 && line.size() > 2 && std::isdigit(static_cast<unsigned char>(line[2]))) {
            std::vector<std::string> cells = cellsOf(line);
            if (cells.size() == 6) {  // (frame, alive A, alive B, HP+shields A, HP+shields B, distance)
                c.s.seriesA.push_back(std::atoi(cells[3].c_str()));
                c.s.seriesB.push_back(std::atoi(cells[4].c_str()));
            }
        } else if (line.rfind("Seeds 1 to 20", 0) == 0) {
            const char* a = std::strstr(line.c_str(), "A won ");
            const char* b = std::strstr(line.c_str(), "B won ");
            const char* m = std::strstr(line.c_str(), "median ");
            if (a && b && m) {
                c.seedWinsA = std::atoi(a + 6);
                c.seedWinsB = std::atoi(b + 6);
                c.seedMedian = std::atof(m + 7);
            }
        }
    }
    return fights;
}

void runCheck(const sim::Tables& t, const sim::MapInfo& map, const std::filesystem::path& path, bool seeds,
              const std::filesystem::path& install, const sim::FileReader& reader) {
    std::vector<std::vector<std::string>> miningRows;
    std::vector<CheckFight> fights = readCheck(path, miningRows);
    if (fights.empty()) {
        std::printf("CHECK: %s cannot be read\n", path.string().c_str());
        return;
    }
    std::printf("\nCHECK (held out: reported only, never tuned to): measured | reference\n");
    double total = 0, seedSeries = 0, seedFrames = 0;
    int winners = 0, frameError = 0;
    for (size_t n = 0; n < fights.size(); ++n) {
        const CheckFight& c = fights[n];
        Fight f = runFight(t, map, c.s, 42, {}, c.centres[0][0] >= 0 ? c.centres : nullptr);
        double ea = seriesError(f.series[0], c.s.seriesA), eb = seriesError(f.series[1], c.s.seriesB);
        total += (ea + eb) / 2;
        winners += f.winner == c.s.winner ? 1 : 0;
        frameError += std::abs(f.frames - c.s.frames);
        std::printf("C%-2zu %-46.46s %c|%c %4d|%4d", n + 1, c.title.c_str(), f.winner, c.s.winner, f.frames,
                    c.s.frames);
        std::printf("  A %2d %4d %3d | %2d %4d %3d", f.alive[0], f.hp[0], f.sh[0], c.s.aliveA, c.s.hpA, c.s.shA);
        std::printf("  B %2d %4d %3d | %2d %4d %3d", f.alive[1], f.hp[1], f.sh[1], c.s.aliveB, c.s.hpB, c.s.shB);
        std::printf("  series A %4.1f%% B %4.1f%%\n", ea, eb);
        if (seeds) {
            std::vector<int> frames;
            int wins[2] = {};
            double series = 0, off = 0;  // (each seed: the series against seed 42's, the frames against the median)
            for (uint32_t seed = 1; seed <= 20; ++seed) {
                Fight g = runFight(t, map, c.s, seed, {}, c.centres[0][0] >= 0 ? c.centres : nullptr);
                frames.push_back(g.frames);
                if (g.winner == 'A' || g.winner == 'B') ++wins[g.winner == 'B'];
                series += (seriesError(g.series[0], c.s.seriesA) + seriesError(g.series[1], c.s.seriesB)) / 2 / 20;
                off += std::abs(g.frames - c.seedMedian) / 20;
            }
            seedSeries += series / double(fights.size());
            seedFrames += off / double(fights.size());
            std::sort(frames.begin(), frames.end());
            std::printf("    20 seeds: A %d|%d B %d|%d, median %.1f|%.1f frames; mean series difference %.1f%%, mean "
                        "frame difference %.0f\n",
                        wins[0], c.seedWinsA, wins[1], c.seedWinsB, (frames[9] + frames[10]) / 2.0, c.seedMedian,
                        series, off);
        }
    }
    std::printf("CHECK: winners as the reference %d of %zu; mean series difference %.1f%%; mean frame difference "
                "%.0f\n", winners, fights.size(), total / double(fights.size()), frameError / double(fights.size()));
    if (seeds)
        std::printf("CHECK, 20 seeds: mean series difference %.1f%% (against seed 42's series); mean frame difference "
                    "%.0f (against the reference's median)\n", seedSeries, seedFrames);

    // Mining on (4)Orbital Relay.scm: each start location, 9 workers, each race.
    auto other = sim::createSimulator();
    stardraft::Bytes chk;
    std::string error;
    std::vector<sim::MiningBase> bases;
    sim::MiningOptions mo;
    mo.workers = {9};
    mo.placedDepots = false;
    mo.mineralClusters = false;
    if (!other->init(reader, error) ||
        !stardraft::readMapChk(install / "Maps" / "(4)Orbital Relay.scm", chk, error) ||
        !other->setMap(chk, error) || !other->mining(mo, bases, error)) {
        std::printf("CHECK mining: %s\n", error.c_str());
        return;
    }
    double sum = 0;
    int count = 0;
    std::printf("CHECK mining, (4)Orbital Relay.scm, 9 workers: minerals a minute, measured | reference\n");
    for (const auto& row : miningRows) {
        if (row.size() < 3) continue;
        int x = 0, y = 0;
        std::sscanf(row[0].c_str(), "(%d, %d)", &x, &y);
        for (const sim::MiningBase& b : bases)
            for (const sim::MiningRun& run : b.runs)
                if (b.x == x && b.y == y && row[1] == raceName(run.race)) {
                    double ref = std::atof(row[2].c_str());
                    sum += std::abs(run.mineralsPerMinute - ref) / ref;
                    ++count;
                    std::printf("  (%d, %d) %-7s %6.1f | %6.1f\n", x, y, row[1].c_str(), run.mineralsPerMinute, ref);
                }
    }
    std::printf("CHECK mining: %d of %zu runs found; mean difference %.1f%%\n", count, miningRows.size(),
                count ? 100.0 * sum / count : 0.0);
}

// Ground trips over the terrain (reference-paths.md): units given a Move order alone (or 12 together), on a map with
// no units of its own. Arrival: the unit stands at its move target with no path left (or the frame limit).
struct Trip {
    int arrival = -1, unwalkable = 0;  // (unwalkable: frames with the centre on an unwalkable minitile)
    double length = 0, finalDistance = 0;
};

std::vector<Trip> runTrips(const sim::Tables& t, const sim::MapInfo& map, int type, int count, int sx, int sy, int tx,
                           int ty, int limit = 3000) {
    sim::World w(t, map);
    w.combat = true;
    std::vector<int> ids;
    for (int n = 0; n < count; ++n) {
        int x = sx, y = sy;
        w.freeSpot(type, x, y);
        ids.push_back(w.add(type, 0, x, y));
        sim::Unit& u = w.units.back();
        int d = t.units[size_t(type)].direction;
        u.heading = u.velDir = (d == 32 ? w.random() & 31 : d) * 8 & 255;
    }
    for (int id : ids) w.setMove(w.units[size_t(id)], tx, ty);
    std::vector<Trip> trips(ids.size());
    std::vector<std::pair<int, int>> last;
    for (int id : ids) last.push_back({w.units[size_t(id)].x, w.units[size_t(id)].y});
    while (w.frame < limit) {
        w.step();
        bool all = true;
        for (size_t k = 0; k < ids.size(); ++k) {
            const sim::Unit& u = w.units[size_t(ids[k])];
            Trip& trip = trips[k];
            if (trip.arrival >= 0) continue;
            trip.length += std::hypot(double(u.x - last[k].first), double(u.y - last[k].second));
            last[k] = {u.x, u.y};
            if (!map.walkable(u.x, u.y)) ++trip.unwalkable;
            if (u.x == u.moveX && u.y == u.moveY && u.path.empty()) {
                trip.arrival = w.frame;
                trip.finalDistance = std::hypot(double(u.x - tx), double(u.y - ty));
            } else {
                all = false;
            }
        }
        if (all) break;
    }
    for (size_t k = 0; k < ids.size(); ++k)
        if (trips[k].arrival < 0) {
            const sim::Unit& u = w.units[size_t(ids[k])];
            trips[k].finalDistance = std::hypot(double(u.x - tx), double(u.y - ty));
        }
    return trips;
}

// The trips of reference-paths.md (not the cliff rows where the reference's units do not move), and a unit sent to a
// point it cannot reach: it must stop at the reachable place nearest to it. False: a check failed.
bool runPathChecks(const sim::Tables& t, const sim::MapInfo& bottleneck, const sim::MapInfo* orbital) {
    bool ok = true;
    std::printf("\nPaths (reference-paths.md): arrival frame | reference, path length | reference, frames on an "
                "unwalkable minitile\n");
    struct Ref {
        const char* name;
        int type, arrival;
        double length;
    };
    const Ref alone[] = {{"Marine", 0, 1386, 5343.0}, {"Zergling", 37, 1016, 5337.8}, {"Dragoon", 66, 1104, 5434.9},
                         {"SCV", 7, 1131, 5373.5}};
    double lengthError = 0, arrivalError = 0;
    int n = 0;
    auto report = [&](const char* name, const Trip& trip, int arrival, double length) {
        std::printf("  %-34s %5d|%5d  %7.1f|%7.1f  %d\n", name, trip.arrival, arrival, trip.length, length,
                    trip.unwalkable);
        ok = ok && trip.unwalkable == 0 && trip.arrival >= 0;
        lengthError += std::abs(trip.length - length) / length;
        arrivalError += std::abs(trip.arrival - arrival) / double(arrival);
        ++n;
    };
    for (const Ref& ref : alone) {
        Trip trip = runTrips(t, bottleneck, ref.type, 1, 3808, 2384, 288, 2416)[0];
        report((std::string("Bottleneck start to start, ") + ref.name).c_str(), trip, ref.arrival, ref.length);
    }
    if (orbital) {
        Trip trip = runTrips(t, *orbital, 0, 1, 352, 656, 2720, 2256)[0];
        report("Orbital Relay corner to corner, Marine", trip, 840, 3286.5);
    }
    {
        Trip trip = runTrips(t, bottleneck, 37, 1, 640, 480, 800, 544)[0];
        report("Bottleneck around a cliff, Zergling", trip, 33, 158.3);
    }
    const int crowdArrival[12] = {1045, 1004, 1143, 1221, 1189, 1128, 1193, 1109, 1106, 1080, 1027, 995};
    const double crowdLength[12] = {5326.2, 5243.9, 5398.8, 5485.4, 5608.5, 5309.2,
                                    5507.0, 5332.9, 5369.2, 5310.6, 5273.4, 5254.6};
    std::vector<Trip> crowd = runTrips(t, bottleneck, 37, 12, 3808, 2384, 288, 2416);
    int crowdUnwalkable = 0, first = 99999, lastArrival = 0;
    for (size_t k = 0; k < crowd.size(); ++k) {
        crowdUnwalkable += crowd[k].unwalkable;
        first = std::min(first, crowd[k].arrival < 0 ? 99999 : crowd[k].arrival);
        lastArrival = std::max(lastArrival, crowd[k].arrival < 0 ? 3000 : crowd[k].arrival);
        lengthError += std::abs(crowd[k].length - crowdLength[k]) / crowdLength[k];
        arrivalError += std::abs(crowd[k].arrival - crowdArrival[k]) / double(crowdArrival[k]);
        ++n;
    }
    std::printf("  12 Zerglings together: arrivals %d to %d (reference 995 to 1221), %d frames on an unwalkable "
                "minitile\n", first, lastArrival, crowdUnwalkable);
    ok = ok && crowdUnwalkable == 0;
    std::printf("Paths: mean difference of the path length %.1f%%, of the arrival %.1f%%\n", 100 * lengthError / n,
                100 * arrivalError / n);
    // A point it cannot reach (inside the unwalkable ground in the map's top left corner): the nearest reachable spot.
    {
        int tx = 4, ty = 4;
        Trip trip = runTrips(t, bottleneck, 0, 1, 288, 2416, tx, ty)[0];
        // (the nearest walkable minitile centre that a Marine's box fits, reachable from the start: a flood fill)
        int w = bottleneck.width * 4, h = bottleneck.height * 4;
        const sim::UnitData& d = t.units[0];
        auto fits = [&](int c) {
            int x = c % w * 8 + 4, y = c / w * 8 + 4;
            return bottleneck.boxWalkable(x - d.left, y - d.up, x + d.right, y + d.down);
        };
        std::vector<uint8_t> seen(size_t(w) * h, 0);
        std::vector<int> queue = {(2416 / 8) * w + 288 / 8};
        seen[size_t(queue[0])] = 1;
        double best = 1e9;
        for (size_t i = 0; i < queue.size(); ++i) {
            int c = queue[i];
            best = std::min(best, std::hypot(double(c % w * 8 + 4 - tx), double(c / w * 8 + 4 - ty)));
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                int x = c % w + dx[k], y = c / w + dy[k];
                if (x < 0 || y < 0 || x >= w || y >= h || seen[size_t(y * w + x)] || !fits(y * w + x)) continue;
                seen[size_t(y * w + x)] = 1;
                queue.push_back(y * w + x);
            }
        }
        bool good = trip.unwalkable == 0 && trip.arrival >= 0 && trip.finalDistance <= best + 16;
        std::printf("Unreachable target (%d, %d): the Marine stops after %d frames %.1f px from it (the nearest "
                    "reachable "
                    "spot: %.1f px), %d frames on an unwalkable minitile: %s\n", tx, ty, trip.arrival,
                    trip.finalDistance, best, trip.unwalkable, good ? "OK" : "FAIL");
        ok = ok && good;
    }
    return ok;
}

// A corridor just as wide as a Dragoon's box (in whole minitiles), with a bend, on a map made of minitile flags: the
// Dragoon walks through it and its box never touches an unwalkable minitile.
bool runCorridorCheck(const sim::Tables& t) {
    const sim::UnitData& d = t.units[66];
    int cw = (d.left + d.right + 8) / 8, ch = (d.up + d.down + 8) / 8;  // (minitiles: the box's width and height)
    sim::MapInfo m;
    m.width = m.height = 16;
    m.walk.assign(64 * 64, 0);
    for (int y = 8; y < 8 + ch; ++y)  // (east to x = 40, then south to y = 56)
        for (int x = 4; x < 40; ++x) m.walk[size_t(y * 64 + x)] = 1;
    for (int y = 8; y < 56; ++y)
        for (int x = 40 - cw; x < 40; ++x) m.walk[size_t(y * 64 + x)] = 1;
    sim::World w(t, m);
    w.combat = true;
    int sx = 6 * 8 + d.left, sy = 8 * 8 + d.up, tx = (40 - cw) * 8 + d.left, ty = 50 * 8 + d.up;
    int id = w.add(66, 0, sx, sy);
    w.setMove(w.units[size_t(id)], tx, ty);
    int outside = 0, arrival = -1;
    while (w.frame < 2000 && arrival < 0) {
        w.step();
        const sim::Unit& u = w.units[size_t(id)];
        if (!w.fits(66, u.x, u.y)) ++outside;
        if (u.x == tx && u.y == ty) arrival = w.frame;
    }
    bool ok = w.fits(66, sx, sy) && w.fits(66, tx, ty) && outside == 0 && arrival > 0;
    std::printf("Corridor: a Dragoon through a bent corridor %d minitiles wide: arrives after %d frames, %d frames "
                "with its box on unwalkable ground: %s\n", cw, arrival, outside, ok ? "OK" : "FAIL");
    return ok;
}

// Death animations and remnants (pictures only): in fight 1, a dying Marine plays its death frames, and the dead
// units leave remnants under the living ones that end with their scripts.
bool runDeathCheck(const sim::Tables& t, const sim::MapInfo& map) {
    sim::World w(t, map);
    w.seed = 42;
    sim::CombatOptions o;
    o.a = kScenarios[0].a;
    o.b = kScenarios[0].b;
    o.players[1] = 1;
    o.x = 3808;
    o.y = 2384;
    std::string error;
    if (!sim::setupCombat(w, o, error)) return false;
    int marineImage = t.flingyImage(t.units[0].flingy);
    std::vector<bool> alive(w.units.size(), true);
    int marineDeath = -1, marineFrames = 0, under = 0, peak = 0;
    std::vector<int> seenFrames;
    while (!sim::combatOver(w, 2857)) {
        w.step();
        for (const sim::Unit& u : w.units) {
            if (alive[size_t(u.id)] && !u.alive && u.type == 0 && marineDeath < 0) marineDeath = w.frame;
            alive[size_t(u.id)] = u.alive;
        }
        for (const sim::World::Effect& e : w.effects) {
            if (e.image == marineImage && marineDeath >= 0 &&
                std::find(seenFrames.begin(), seenFrames.end(), e.anim.frame) == seenFrames.end())
                seenFrames.push_back(e.anim.frame);
            under += e.under ? 1 : 0;
        }
        peak = std::max(peak, int(w.effects.size()));
    }
    marineFrames = int(seenFrames.size());
    int end = w.frame;
    while (!w.effects.empty() && w.frame < end + 10000) w.step();
    bool ok = marineDeath >= 0 && marineFrames >= 3 && under > 0 && w.effects.empty();
    std::printf("Deaths: fight 1: the first Marine dies at frame %d and its death plays %d frames; remnants under the "
                "living units (%d image-frames, at most %d pictures); all gone %d frames after the fight: %s\n",
                marineDeath, marineFrames, under, peak, w.frame - end, ok ? "OK" : "FAIL");
    return ok;
}

// The cost of a path search to a goal that cannot be reached, on the first 256 x 256 map of the Maps folder: it must
// stay well under a frame at Fastest (42 ms).
bool runSearchTiming(const sim::Tables& t, const std::filesystem::path& install) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(install / "Maps", ec)) {
        if (entry.path().extension() != ".scm" && entry.path().extension() != ".scx") continue;
        stardraft::Bytes chk;
        std::string error;
        sim::MapInfo m;
        if (!stardraft::readMapChk(entry.path(), chk, error) || !m.load(chk, t, error) || m.width != 256 ||
            m.height != 256)
            continue;
        int sx = -1, sy = -1;
        for (const sim::MapInfo::Placed& p : m.units)
            if (p.type == 214 && sx < 0) sx = p.x, sy = p.y;
        if (sx < 0) continue;
        int tx = -1, ty = -1;  // (the unwalkable minitile farthest from the start)
        double far = 0;
        for (int y = 0; y < 256 * 4; ++y)
            for (int x = 0; x < 256 * 4; ++x)
                if (!m.walk[size_t(y * 1024 + x)] && std::hypot(double(x * 8 - sx), double(y * 8 - sy)) > far) {
                    far = std::hypot(double(x * 8 - sx), double(y * 8 - sy));
                    tx = x * 8 + 4;
                    ty = y * 8 + 4;
                }
        sim::World w(t, m);
        w.combat = true;
        int id = w.add(0, 0, sx, sy);
        double worst = 0;
        for (int n = 0; n < 3; ++n) {
            sim::Unit& u = w.units[size_t(id)];
            auto start = std::chrono::steady_clock::now();
            w.findPath(u, tx, ty);
            worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                                        .count());
        }
        bool ok = worst < 21;  // (half a frame)
        std::printf("Search time: %s, a Marine from (%d, %d) to an unwalkable place (%d, %d): at most %.1f ms a search "
                    "(3 searches): %s\n", entry.path().filename().string().c_str(), sx, sy, tx, ty, worst,
                    ok ? "OK" : "FAIL");
        return ok;
    }
    std::printf("Search time: no 256 x 256 map in the Maps folder\n");
    return true;
}

// A unit's images from its scripts: the overlays its Init animation makes, each image playing its own script. Idle
// units over 30 frames: how many images, and whether their frames change (the Archon must have overlays and change).
bool runImageCheck(const sim::Tables& t, const sim::MapInfo& map) {
    const std::pair<int, const char*> kinds[] = {{68, "Archon"},  {63, "Dark Archon"}, {47, "Scourge"},
                                                 {43, "Mutalisk"}, {60, "Corsair"},     {64, "Probe"},
                                                 {41, "Drone"},    {7, "SCV"},          {72, "Carrier"}};
    bool ok = true;
    std::printf("Images over 30 idle frames (images, frames changed):");
    for (const auto& [type, name] : kinds) {
        sim::World w(t, map);
        w.add(type, 0, 3808, 2384);
        std::vector<sim::LiveImage> first, later;
        int changed = 0;
        size_t most = 0;
        for (int f = 0; f < 31; ++f) {
            w.step();
            std::vector<sim::LiveImage> now;
            w.images(now);
            most = std::max(most, now.size());
            if (!later.empty())
                for (size_t k = 0; k < std::min(now.size(), later.size()); ++k)
                    changed += now[k].frame != later[k].frame || now[k].image != later[k].image ? 1 : 0;
            later = now;
        }
        std::printf(" %s %zu, %d;", name, most, changed);
        if (type == 68) ok = most >= 2 && changed > 0;
    }
    std::printf(" %s\n", ok ? "OK" : "FAIL: the Archon");
    return ok;
}

// No unit is removed (passive map units, buildings as targets). (1) The map's units, all passive, and 8 Marines (P0)
// vs 8 Zerglings (P1): the same winner as without them, and no passive unit hurt. (2) 4 Marines (P0) vs a Hatchery and
// 2 Drones (P1): all of player 1 dies, the Hatchery last (it cannot attack: the last priority).
bool runPassiveCheck(const sim::Tables& t, const sim::MapInfo& map) {
    auto run = [&](const std::vector<sim::PlacedUnit>& placed, sim::World& w) {
        w.seed = 42;
        sim::CombatOptions o;
        o.byPlayer = true;
        o.placed = placed;
        std::string error;
        if (!sim::setupCombat(w, o, error)) return false;
        while (!sim::combatOver(w, o.frames)) w.step();
        return true;
    };
    std::vector<sim::PlacedUnit> fight;
    for (int n = 0; n < 8; ++n) {
        fight.push_back({0, 0, 3680 + n % 4 * 20, 2360 + n / 4 * 20});
        fight.push_back({37, 1, 3920 + n % 4 * 20, 2360 + n / 4 * 20});
    }
    std::vector<sim::PlacedUnit> withMap = fight;
    for (const sim::MapInfo::Placed& p : map.units)
        if (p.type != 214) withMap.push_back({p.type, p.owner, p.x, p.y, true});  // (not the start locations)
    sim::World plain(t, map), all(t, map);
    bool ok = run(fight, plain) && run(withMap, all);
    sim::CombatResult a = sim::combatResultOf(plain), b = sim::combatResultOf(all);
    int passive = 0, hurt = 0;
    for (const sim::Unit& u : all.units)
        if (u.neutral) {
            ++passive;
            if (!u.alive || u.hp != all.t.units[size_t(u.type)].hp || u.damageTaken) ++hurt;
        }
    ok = ok && a.winner == b.winner && a.winner >= 0 && hurt == 0 && passive > 0;
    std::printf("Passive map units: 8 Marine vs 8 Zergling, winner %d (%d frames); with the map's %d units passive: "
                "winner %d (%d frames), %d hurt: %s\n",
                a.winner, a.frames, passive, b.winner, b.frames, hurt, ok ? "OK" : "FAIL");
    std::vector<sim::PlacedUnit> base = {{0, 0, 3680, 2360}, {0, 0, 3700, 2360}, {0, 0, 3680, 2380},
                                         {0, 0, 3700, 2380}, {131, 1, 3936, 2384}, {41, 1, 3880, 2440},
                                         {41, 1, 3900, 2440}};
    int hatcheryDeath = -1, lastOther = -1, left = 0;
    sim::World v(t, map);
    v.seed = 42;
    sim::CombatOptions o;
    o.byPlayer = true;
    o.placed = base;
    std::string error;
    bool ok2 = sim::setupCombat(v, o, error);
    std::vector<bool> dead(v.units.size(), false);
    while (ok2 && !sim::combatOver(v, o.frames)) {
        v.step();
        for (size_t j = 0; j < v.units.size() && j < dead.size(); ++j) {
            const sim::Unit& u = v.units[j];
            if (u.alive || u.owner != 1 || u.neutral || dead[j]) continue;
            dead[j] = true;
            if (u.type == 131) hatcheryDeath = v.frame;
            else lastOther = std::max(lastOther, v.frame);
        }
    }
    for (const sim::Unit& u : v.units) left += u.alive && u.owner == 1 && !u.neutral ? 1 : 0;
    sim::CombatResult r = sim::combatResultOf(v);
    ok2 = ok2 && left == 0 && r.winner == 0 && hatcheryDeath > lastOther && lastOther > 0;
    std::printf("Buildings: 4 Marine vs Hatchery + 2 Drone: winner %d at %d, Drones dead by %d, Hatchery at %d: %s\n",
                r.winner, r.frames, lastOther, hatcheryDeath, ok2 ? "OK" : "FAIL");
    return ok && ok2;
}

// A stalemate: a Zergling on each side of the 6-cell choke of Bottleneck (x = 832, y 3872 to 4063), the choke closed
// by passive mineral fields. The two are in one walkable group, so each goes for the other, but neither can get
// through: the fight ends as a stalemate (no damage, nobody nearer, for 30 seconds), not at the time limit. Without
// the mineral fields they fight.
bool runStalemateCheck(const sim::Tables& t, const sim::MapInfo& map) {
    auto run = [&](bool closed, sim::CombatResult& r) {
        sim::World w(t, map);
        w.seed = 42;
        sim::CombatOptions o;
        o.byPlayer = true;
        o.frames = 5000;
        o.placed = {{37, 0, 736, 3808}, {37, 1, 928, 3968}};
        if (closed)
            for (int k = 0; k < 6; ++k) o.placed.push_back({176, 11, 832, 3888 + 32 * k, true});
        std::string error;
        if (!sim::setupCombat(w, o, error)) return false;
        while (!sim::combatOver(w, o.frames)) w.step();
        r = sim::combatResultOf(w);
        return true;
    };
    sim::CombatResult open, closed;
    bool ok = run(false, open) && run(true, closed);
    int aliveClosed = 0;
    for (const sim::CombatSide& c : closed.teams) aliveClosed += c.alive;
    ok = ok && !open.stalemate && open.winner >= 0 && closed.stalemate && !closed.timedOut && aliveClosed == 2;
    std::printf("Stalemate: Zergling vs Zergling across Bottleneck's choke: open, winner %d at %d; closed by passive "
                "mineral fields, %s at %d, %d alive: %s\n",
                open.winner, open.frames, closed.stalemate ? "stalemate" : "no stalemate", closed.frames, aliveClosed,
                ok ? "OK" : "FAIL");
    return ok;
}

// No image the game never draws: the projectiles of the Firebat, the Reaver, the Scourge and the Lurker have sprites
// that sprites.dat marks as not visible (a white circle in the editor if drawn). Fights of these units: no image of
// such a sprite in images(), and the Scarab drawn with its own image.
bool runHiddenImageCheck(const sim::Tables& t, const sim::MapInfo& map) {
    std::vector<bool> hidden(t.imageDraw.size(), false), shown(t.imageDraw.size(), false);
    for (size_t s = 0; s < t.spriteImage.size(); ++s) {
        int image = t.spriteImage[s];
        if (image < 0 || image >= int(hidden.size())) continue;
        (t.spriteVisible[s] ? shown : hidden)[size_t(image)] = true;
    }
    int scarabImage = t.flingyImage(t.units[size_t(t.scarabType)].flingy), emitted = 0, scarabs = 0, fights = 0;
    const int kinds[][2] = {{32, 37}, {83, 37}, {47, 12}, {103, 0}};  // (Firebat, Reaver, Scourge, Lurker vs ...)
    for (const auto& k : kinds) {
        Scenario s{"hidden images", {{k[0], 4}}, {{k[1], 8}}, '-', 0, 0, 0, 0, 0, 0, 0, {}, {}};
        runFight(t, map, s, 42, [&](const sim::World& w) {
            std::vector<sim::LiveImage> images;
            w.images(images);
            for (const sim::LiveImage& li : images) {
                if (li.image >= 0 && li.image < int(hidden.size()) && hidden[size_t(li.image)] &&
                    !shown[size_t(li.image)])
                    ++emitted;
                if (li.image == scarabImage) ++scarabs;
            }
        });
        ++fights;
    }
    bool ok = emitted == 0 && scarabs > 0;
    std::printf("Hidden images: %d fights (Firebat, Reaver, Scourge, Lurker): %d images of sprites the game does not "
                "draw, the Scarab drawn %d times: %s\n",
                fights, emitted, scarabs, ok ? "OK" : "FAIL");
    return ok;
}

// Weapons with two bullets an attack (weapons.dat damage factor 2): a Scout's air attack on a Mutalisk, 300 frames
// (the Mutalisk cannot die). Each attack shows two missile images at different places, and each missile takes the
// data's damage (its type against the size, less the armor).
bool runVolleyCheck(const sim::Tables& t, const sim::MapInfo& map) {
    const int kScout = 70, kMutalisk = 43;
    sim::World w(t, map);
    w.seed = 42;
    sim::CombatOptions o;
    o.x = 3808;
    o.y = 2384;
    o.players[1] = 1;
    o.a = {{kScout, 1}};
    o.b = {{kMutalisk, 1}};
    std::string error;
    if (!sim::setupCombat(w, o, error)) {
        std::printf("Two bullets an attack: %s\n", error.c_str());
        return false;
    }
    int scout = -1, muta = -1;
    for (size_t j = 0; j < w.units.size(); ++j) {
        if (w.units[j].type == kScout) scout = int(j);
        if (w.units[j].type == kMutalisk) muta = int(j);
    }
    if (scout < 0 || muta < 0) return false;
    w.units[size_t(muta)].hp = 100000 << 8;
    const sim::WeaponData& wd = t.weapons[size_t(t.units[kScout].airWeapon)];
    const sim::UnitData& md = t.units[kMutalisk];
    static const int sizeFactor[3] = {128, 192, 256};  // (explosive against small, medium, large)
    int each = std::max(128, (wd.damage * 256 - md.armor * 256) * (wd.damageType == 1 ? sizeFactor[md.size - 1] : 256) /
                                 256);  // (8 fractional bits)
    int missile = t.flingyImage(wd.flingy);
    int attacks = 0, hits = 0, wrong = 0, most = 0, apart = 0, lastCooldown = 0;
    int before = w.units[size_t(muta)].hp + w.units[size_t(muta)].shields;
    for (int f = 0; f < 300; ++f) {
        w.step();
        const sim::Unit& s = w.units[size_t(scout)];
        if (s.cooldown > lastCooldown && f < 260) ++attacks;  // (a new attack; the last 40 frames' may not land)
        lastCooldown = s.cooldown;
        const sim::Unit& m = w.units[size_t(muta)];
        int now = m.hp + m.shields, loss = before - now;
        before = now;
        if (loss > 256) {  // (a hit: more than the regeneration)
            int n = (loss + each / 2) / each;
            hits += n;
            if (std::abs(loss - n * each) > 8) ++wrong;
        }
        std::vector<sim::LiveImage> images;
        w.images(images);
        std::vector<std::pair<int, int>> at;
        for (const sim::LiveImage& li : images)
            if (li.image == missile) at.push_back({li.x, li.y});
        most = std::max(most, int(at.size()));
        if (at.size() >= 2 && at[0] != at[1]) ++apart;
    }
    bool ok = attacks > 0 && hits >= attacks * std::max(1, wd.factor) && wrong == 0 && most >= wd.factor && apart > 0;
    std::printf("Two bullets an attack: Scout vs Mutalisk: %d attacks, %d missile hits of %.1f (data: %d x %d, %d "
                "wrong), at most %d missile images, %d frames apart: %s\n",
                attacks, hits, each / 256.0, wd.factor, wd.damage, wrong, most, apart, ok ? "OK" : "FAIL");
    return ok;
}

// Drawing layers: the hit images (a projectile's own image and the sprites its script makes above it: sprol,
// highsprol, grdsprol) have an elevation above the ground units', in fight 1 and the Siege Tank fight.
bool runElevationCheck(const sim::Tables& t, const sim::MapInfo& map) {
    // The images that a script makes as sprites above it, all its animations (gotos, calls and jumps followed).
    auto spritesAbove = [&](int image, std::set<int>& out) {
        const std::vector<uint8_t>& s = t.iscript;
        auto u16 = [&](int o) { return o + 1 < int(s.size()) ? s[size_t(o)] | s[size_t(o + 1)] << 8 : 0; };
        int header = t.scriptHeader(image);
        std::vector<int> todo;
        for (int anim = 0; anim < 30; ++anim)
            if (int pc = t.animOffset(header, anim)) todo.push_back(pc);
        std::set<int> seen;
        while (!todo.empty()) {
            int pc = todo.back();
            todo.pop_back();
            while (pc > 0 && seen.insert(pc).second) {
                int len = t.opLength(pc);
                if (len < 0) break;
                int op = s[size_t(pc)];
                if (op == 0x16 || op == 0x36) break;
                if ((op == 0x0F || op == 0x10 || op == 0x42) && u16(pc + 1) < int(t.spriteImage.size()))
                    out.insert(t.spriteImage[size_t(u16(pc + 1))]);
                if (op == 0x07) {
                    pc = u16(pc + 1);
                    continue;
                }
                if (op == 0x35) todo.push_back(u16(pc + 1));
                if (op == 0x1E) todo.push_back(u16(pc + 2));
                pc += len;
            }
        }
    };
    bool ok = true;
    for (const Scenario& s : kScenarios) {
        if (&s != &kScenarios[0] && std::string(s.name).find("Siege Tank") == std::string::npos) continue;
        std::set<int> hits;
        for (const auto* army : {&s.a, &s.b})
            for (const sim::Army& army1 : *army)
                for (int unit : {army1.unitType, t.units[size_t(army1.unitType)].subunit}) {
                    if (unit == sim::kNoUnit) continue;
                    for (int w : {t.units[size_t(unit)].groundWeapon, t.units[size_t(unit)].airWeapon}) {
                        if (w == sim::kNoWeapon) continue;
                        int image = t.flingyImage(t.weapons[size_t(w)].flingy);
                        hits.insert(image);
                        spritesAbove(image, hits);
                    }
                }
        int seen = 0, low = 0, ground = 0;
        runFight(t, map, s, 42, [&](const sim::World& w) {
            std::vector<sim::LiveImage> images;
            w.images(images);
            for (const sim::Unit& u : w.units)
                if (u.alive && !(t.units[size_t(u.type)].flags & sim::kFlyer))
                    ground = std::max(ground, t.units[size_t(u.type)].elevation);
            for (const sim::LiveImage& li : images) {
                if (!hits.count(li.image)) continue;
                ++seen;
                if (li.elevation <= ground) ++low;
            }
        });
        bool good = seen > 0 && low == 0;
        std::printf("Drawing layers: %s: %d hit images, %d at or under the ground units' elevation %d: %s\n", s.name,
                    seen, low, ground, good ? "OK" : "FAIL");
        ok = ok && good;
    }
    return ok;
}

// The Archon's images (its scripts): the Energy with the Being over it and the Swirl over both (Init's imgol 135,
// imgol 136, after its nobrkcodeend), each in its frame range: Energy 0 to 26, Being's idle and walking sets 170 to
// 221 and its attack sets 0 to 153 (+ 16 directions), Swirl 0 to 14; the attack overlay (imgoluselo 549) at its
// .lo offset while the target is farther than 48 pixels. The Dark Archon: Energy, Being, Swirl.
bool runArchonImageCheck(const sim::Tables& t, const sim::MapInfo& map) {
    struct Kind {
        int unit, energy, being, swirl, overlay;
    };
    bool ok = true;
    for (const Kind& k : {Kind{68, 134, 135, 136, 549}, Kind{63, 925, 926, 927, -1}}) {
        sim::World w(t, map);
        w.seed = 42;
        sim::CombatOptions o;
        o.x = 3808;
        o.y = 2384;
        o.gap = 700;
        o.players[1] = 1;
        o.a = {{k.unit, 1}};
        o.b = {{39, 1}};  // (an Ultralisk: large, so the Archon attacks from beyond 48 pixels)
        std::string error;
        if (!sim::setupCombat(w, o, error)) return false;
        for (sim::Unit& u : w.units) u.hp = 100000 << 8;
        int id = -1;
        for (size_t i = 0; i < w.units.size(); ++i)
            if (w.units[i].type == k.unit && id < 0) id = int(i);
        int complete = 0, wrongOrder = 0, wrongFrame = 0, walkSet = 0, attackSet = 0, overlay = 0, frames = 240;
        for (int f = 0; f < frames; ++f) {
            w.step();
            std::vector<sim::LiveImage> images;
            w.images(images);
            int at[3] = {-1, -1, -1};
            for (size_t n = 0; n < images.size(); ++n) {
                const sim::LiveImage& li = images[n];
                if (li.owner != w.units[size_t(id)].owner || std::abs(li.x - w.units[size_t(id)].x) > 64 ||
                    std::abs(li.y - w.units[size_t(id)].y) > 64)
                    continue;
                if (li.image == k.energy) {
                    at[0] = int(n);
                    wrongFrame += li.frame < 0 || li.frame > 26;
                } else if (li.image == k.being) {
                    at[1] = int(n);
                    wrongFrame += li.frame < 0 || li.frame >= t.imageFrames[size_t(k.being)];
                    if (k.overlay >= 0) (li.frame >= 170 ? walkSet : attackSet)++;
                } else if (li.image == k.swirl) {
                    at[2] = int(n);
                    wrongFrame += li.frame < 0 || li.frame > 14;
                } else if (li.image == k.overlay) {
                    overlay += li.x != w.units[size_t(id)].x || li.y != w.units[size_t(id)].y;  // (its .lo offset)
                }
            }
            if (at[0] >= 0 && at[1] >= 0 && at[2] >= 0) {
                ++complete;
                wrongOrder += !(at[0] < at[1] && at[1] < at[2]);
            }
        }
        bool good = complete >= frames - 30 && wrongOrder == 0 && wrongFrame == 0 &&
                    (k.overlay < 0 || (walkSet > 0 && attackSet > 0 && overlay > 0));
        std::printf("Archon images: unit %d: Energy, Being and Swirl in %d of %d frames (%d in the wrong order), %d "
                    "frames out of range; Being's walking sets %d, attack sets %d frames; attack overlay at its offset "
                    "%d frames: %s\n",
                    k.unit, complete, frames, wrongOrder, wrongFrame, walkSet, attackSet, overlay,
                    good ? "OK" : "FAIL");
        ok = ok && good;
    }
    return ok;
}

// Attacks on buildings: melee and ranged units reach a building's box (they collide with its units.dat box, not its
// placement footprint) and kill it, and no attacker stands idle (no attack, no change of place) longer than a unit
// that found no way waits before it tries again (rules.ini frozenFrames), an order tick and 16 frames. `groups`: (unit
// type, player, count, x, y) of placed units, 24 px apart; players 1 and 2. (8 Zerglings round a Sunken: the last ones
// wait up to 60 frames for a gap narrower than the short path's 8-pixel steps, as units do not slide: not tested.)
bool runBuildingFight(const sim::Tables& t, const sim::MapInfo& map, const char* name,
                      const std::vector<std::array<int, 5>>& groups) {
    sim::World w(t, map);
    w.seed = 42;
    sim::CombatOptions o;
    o.byPlayer = true;
    for (const auto& g : groups)
        for (int n = 0; n < g[2]; ++n) o.placed.push_back({g[0], g[1], g[3] + n % 4 * 24, g[4] + n / 4 * 24});
    std::string error;
    if (!sim::setupCombat(w, o, error)) return false;
    std::vector<int> idle(w.units.size(), 0);
    std::vector<std::pair<int, int>> was(w.units.size());
    for (size_t i = 0; i < w.units.size(); ++i) was[i] = {w.units[i].x, w.units[i].y};
    const int limit = t.rules.frozenFrames + t.rules.orderTick + 16;
    int longest = 0;
    while (!sim::combatOver(w, 3000)) {
        w.step();
        for (size_t i = 0; i < w.units.size() && i < idle.size(); ++i) {
            const sim::Unit& u = w.units[i];
            if (!u.alive || u.building || u.owner != 1) continue;
            bool moved = was[i] != std::make_pair(u.x, u.y);
            was[i] = {u.x, u.y};
            idle[i] = u.cooldown > 0 || u.repeatAttack || moved ? 0 : idle[i] + 1;
            longest = std::max(longest, idle[i]);
        }
    }
    int left = 0;
    for (const sim::Unit& u : w.units) left += u.alive && u.owner == 2;
    bool ok = left == 0 && longest <= limit;
    std::printf("Buildings: %s: %s at frame %d, longest idle %d frames (at most %d): %s\n", name,
                left ? "the building lives" : "the building dies", w.frame, longest, limit, ok ? "OK" : "FAIL");
    return ok;
}

// Mining workers and buildings: a gathering worker collides with buildings (units.dat's building flag, mineral fields
// too), never with other workers. In a live mining run with gas, no visible worker's box overlaps a building's box in
// any frame; and the run with the walks kept (World::walks) is the same as with each walk searched again.
bool runMiningCollisionCheck(const sim::Tables& t, const std::filesystem::path& install) {
    stardraft::Bytes chk;
    std::string error;
    sim::MapInfo orbital;
    if (!stardraft::readMapChk(install / "Maps" / "(4)Orbital Relay.scm", chk, error) || !orbital.load(chk, t, error)) {
        std::printf("Mining collisions: %s: FAIL\n", error.c_str());
        return false;
    }
    sim::MiningOptions mo;
    mo.gas = true;
    mo.gasWorkers = 3;
    std::vector<sim::MiningBase> bases = sim::miningBases(t, orbital, mo);
    std::vector<sim::Race> races = sim::baseRaces(t, orbital, bases, mo);
    sim::World w(t, orbital), fresh(t, orbital);  // (fresh: each walk searched again)
    sim::setupMining(w, bases, races, 12, mo);
    sim::setupMining(fresh, bases, races, 12, mo);
    fresh.keepWalks = false;
    int frames = 0, overlaps = 0, worst = -1, differ = 0;
    for (int f = 0; f < 3000; ++f) {
        w.step();
        fresh.step();
        for (size_t i = 0; i < w.units.size() && i < fresh.units.size(); ++i)
            differ += w.units[i].x != fresh.units[i].x || w.units[i].y != fresh.units[i].y;
        bool any = false;
        for (const sim::Unit& u : w.units) {
            if (!u.alive || u.hidden || !(w.t.units[size_t(u.type)].flags & sim::kWorker)) continue;
            for (const sim::Unit& b : w.units)
                if (b.alive && (w.t.units[size_t(b.type)].flags & sim::kBuilding) &&
                    w.overlaps(u.type, u.x, u.y, b)) {
                    any = true;
                    if (worst < 0) {
                        worst = w.frame;
                        std::printf("  (frame %d: worker %d at (%d, %d) order %d in unit type %d at (%d, %d))\n",
                                    w.frame, u.type, u.x, u.y, int(u.order), b.type, b.x, b.y);
                    }
                }
        }
        ++frames;
        overlaps += any;
    }
    bool ok = overlaps == 0 && differ == 0;
    std::printf("Mining collisions: (4)Orbital Relay.scm, 12 workers and 3 on gas a base, %d frames: %d with a worker "
                "inside a building's box (first at %d); kept walks against new searches: %d places differ: %s\n",
                frames, overlaps, worst, differ, ok ? "OK" : "FAIL");
    return ok;
}

// Gathering sparks: the picture of a mining worker's harvest weapon (the SCV's Fusion Cutter, the Probe's Particle
// Beam) sits on the field it mines: within the field's box or on its edge (at most 2 pixels out), in the live images.
bool runGatherSparkCheck(const sim::Tables& t, const sim::MapInfo& map) {
    bool ok = true;
    for (sim::Race race : {sim::Terran, sim::Protoss}) {
        sim::MiningOptions mo;
        mo.races = {race};
        std::vector<sim::MiningBase> bases = sim::miningBases(t, map, mo);
        bases.resize(1);
        std::vector<sim::Race> races = sim::baseRaces(t, map, bases, mo);
        sim::World w(t, map);
        sim::setupMining(w, bases, races, 9, mo);
        const int weapon = race == sim::Terran ? 14 : 63;  // (the harvest weapons that the workers' scripts use)
        const int spark = t.flingyImage(t.weapons[size_t(weapon)].flingy);
        int sparks = 0, off = 0, worst = 0;
        for (int f = 0; f < 900; ++f) {
            w.step();
            for (const sim::World::Effect& e : w.effects) {
                if (e.image != spark || e.follow >= 0) continue;
                // (its gap to the nearest mineral field)
                int gap = -1;
                for (const sim::Unit& v : w.units)
                    if (v.alive && t.roles[size_t(v.type)].mineralField) {
                        const int g = w.pointGap(v, e.x, e.y);
                        if (gap < 0 || g < gap) gap = g;
                    }
                if (gap < 0) continue;
                ++sparks;
                worst = std::max(worst, gap);
                off += gap > 2;
            }
        }
        const bool good = sparks > 0 && off == 0;
        std::printf("Gathering sparks: %s: %d spark frames, %d off their field (the farthest %d px out): %s\n",
                    race == sim::Terran ? "SCVs" : "Probes", sparks, off, worst, good ? "OK" : "FAIL");
        ok = ok && good;
    }
    return ok;
}

// Gathering pictures and sounds: a mining worker plays its mining animation (Almost Built) while it mines; the SCV's
// and the Probe's use their harvest weapon (its picture at the field, with sparks), and all three play their sounds.
bool runGatherPictureCheck(const sim::Tables& t, const sim::MapInfo& map) {
    bool ok = true;
    for (sim::Race race : {sim::Terran, sim::Protoss, sim::Zerg}) {
        sim::MiningOptions mo;
        mo.races = {race};
        std::vector<sim::MiningBase> bases = sim::miningBases(t, map, mo);
        bases.resize(1);
        std::vector<sim::Race> races = sim::baseRaces(t, map, bases, mo);
        sim::World w(t, map);
        w.recordSounds = true;
        sim::setupMining(w, bases, races, 9, mo);
        std::vector<int> mainImages;
        for (const sim::UnitData& d : t.units) mainImages.push_back(t.flingyImage(d.flingy));
        int pictures = 0, sounds = 0;
        for (int f = 0; f < 900; ++f) {
            w.step();
            std::vector<sim::LiveImage> images;
            w.images(images);
            for (const sim::LiveImage& li : images)  // (a picture that is no unit's own)
                pictures += std::find(mainImages.begin(), mainImages.end(), li.image) == mainImages.end() &&
                            li.image != t.rules.mineralImage && li.image != t.rules.mineralImage + 1;
        }
        sounds = int(w.sounds.size());
        bool good = sounds > 0 && (race == sim::Zerg || pictures > 0);  // (the Drone's work has no picture of its own)
        const char* name = race == sim::Terran ? "SCV" : race == sim::Protoss ? "Probe" : "Drone";
        std::printf("Gathering pictures: %ss mining 900 frames: %d effect images drawn, %d sounds: %s\n", name,
                    pictures, sounds, good ? "OK" : "FAIL");
        ok = ok && good;
    }
    return ok;
}

// Damage overlays: a Barracks and a Hatchery whose hit points fall in 1/16 steps and rise again. Falling to step k
// (the edges 1 - 2k / (3 (S + 1)), S = 2 x slots): ceil(k / 2) overlays, floor(k / 2) of the second set (472 + slot,
// the others 450 + slot). Rising from the bottom: the second-set ones turn back first (max(0, k - n) left), then the
// first-set ones go (min(k, n) in all). Each at its slot's place from the .lof; none at full hit points.
bool runDamageOverlayCheck(const sim::Tables& t, const sim::MapInfo& map) {
    bool ok = true;
    for (int type : {111, 131}) {
        sim::World w(t, map);
        w.seed = 42;
        sim::CombatOptions o;
        o.byPlayer = true;
        o.placed = {{type, 1, 3808, 2384}, {0, 2, 3000, 2384}};
        std::string error;
        if (!sim::setupCombat(w, o, error)) return false;
        int id = -1;
        for (size_t i = 0; i < w.units.size(); ++i)
            if (w.units[i].type == type) id = int(i);
        const sim::UnitData& d = t.units[size_t(type)];
        int image = t.flingyImage(d.flingy);
        const auto& lo = t.damageOffsets[size_t(image)];
        int n = 0;
        for (const auto& p : lo[0]) n += p != std::make_pair(127, 127);
        const int steps = 2 * n;
        int checks = 0, wrong = 0, most = 0;
        auto check = [&](int hp, bool down) {
            w.units[size_t(id)].hp = hp;
            w.step();
            int k = 0;
            for (int e = 1; e <= steps; ++e)
                if (static_cast<long long>(hp) * 3 * (steps + 1) <
                    static_cast<long long>(d.hp) * (3 * (steps + 1) - 2 * e))
                    k = e;
            int first = 0, second = 0;
            for (const sim::World::Effect& e : w.effects) {
                if (e.follow != id || e.damageSlot < 0) continue;
                (e.damageSecond ? second : first)++;
                const auto& place = lo[0][size_t(e.damageSlot)];
                bool at = e.dx == place.first && e.dy == place.second;
                bool image = e.image == (e.damageSecond ? 472 : 450) + e.damageSlot;
                wrong += !at || !image;
            }
            int all = down ? (k + 1) / 2 : std::min(k, n), big = down ? k / 2 : std::max(0, k - n);
            wrong += first + second != all || second != big;
            most = std::max(most, first + second);
            ++checks;
        };
        for (int s = 16; s >= 1; --s) check(d.hp * s / 16, true);
        w.setFastForward(true);  // (fast forward clears the pictures: they come again after it)
        w.setFastForward(false);
        check(d.hp / 16, true);
        for (int s = 1; s <= 16; ++s) check(d.hp * s / 16, false);
        bool good = wrong == 0 && most == n;
        std::printf("Damage overlays: unit %d (%d slots): %d hit-point steps down and up, at most %d overlays, %d "
                    "wrong: %s\n", type, n, checks, most, wrong, good ? "OK" : "FAIL");
        ok = ok && good;
    }
    return ok;
}

// Unit free for all: every fighting unit is an enemy of every other one, also of its own player's. 6 Zerglings of one
// player kill each other down to one; 4 Marines and 4 Zerglings of two players end with one unit left. The result
// gives the last unit's index in `placed`, its player as the winner, and one team by player.
bool runUnitFreeForAllCheck(const sim::Tables& t, const sim::MapInfo& map) {
    bool ok = true;
    struct Case {
        const char* name;
        std::vector<std::array<int, 5>> groups;  // (type, player, count, x, y)
        int players;
    };
    for (const Case& c : {Case{"6 Zergling of one player", {{37, 1, 6, 3760, 2360}}, 1},
                          Case{"4 Marine (P0) + 4 Zergling (P1)",
                               {{0, 0, 4, 3700, 2360}, {37, 1, 4, 3820, 2360}}, 2}}) {
        sim::World w(t, map);
        w.seed = 42;
        sim::CombatOptions o;
        o.byPlayer = true;
        o.unitFreeForAll = true;
        for (const auto& g : c.groups)
            for (int n = 0; n < g[2]; ++n) o.placed.push_back({g[0], g[1], g[3] + n % 4 * 24, g[4] + n / 4 * 24});
        o.frames = 6000;
        std::string error;
        if (!sim::setupCombat(w, o, error)) {
            std::printf("Unit free for all: %s: %s: FAIL\n", c.name, error.c_str());
            return false;
        }
        while (!sim::combatOver(w, o.frames)) w.step();
        sim::CombatResult r = sim::combatResultOf(w);
        int alive = 0;
        for (const sim::Unit& u : w.units) alive += u.alive && !u.neutral && !u.fighter;
        bool good = alive == 1 && r.winnerUnit >= 0 && r.winnerUnit < int(o.placed.size()) &&
                    r.winner == o.placed[size_t(r.winnerUnit)].side && int(r.teams.size()) == c.players;
        std::printf("Unit free for all: %s: %d left at frame %d, the last unit placed %d, winner player %d, %zu teams: "
                    "%s\n", c.name, alive, r.frames, r.winnerUnit, r.winner, r.teams.size(), good ? "OK" : "FAIL");
        ok = ok && good;
    }
    return ok;
}

// Creep in the live game (creep rules): a Hatchery that finishes gets its footprint and margin (36 tiles on open
// ground), then a tile every 16 frames (frames 1, 17, 33, ...) up to its whole area; killed, its creep recedes from 9
// frames later, a tile every 10 frames (up to 256 receding tiles), until none is left.
bool runCreepCheck(const sim::Tables& t, const sim::MapInfo& map) {
    // (a place on open ground: the whole area of 200 tiles)
    int hx = -1, hy = -1;
    for (int y = 6; y < map.height - 8 && hx < 0; y += 2)
        for (int x = 10; x < map.width - 12 && hx < 0; x += 2) {
            stardraft::CreepBuilding b;
            b.x = x * 32 + 64, b.y = y * 32 + 48, b.placeW = 128, b.placeH = 96, b.maker = true;
            int n = 0;
            for (uint8_t c : stardraft::startCreep(map.width, map.height, map.creepCarriers, {b})) n += c;
            if (n == 200) hx = b.x, hy = b.y;
        }
    sim::World w(t, map);
    w.seed = 42;
    sim::CombatOptions o;
    o.byPlayer = true;
    o.placed = {{131, 1, hx, hy}, {109, 2, hx < 1000 ? hx + 1600 : 200, 200}};  // (a Supply Depot far away: no fight)
    std::string error;
    if (!sim::setupCombat(w, o, error)) return false;
    int hatchery = -1;
    for (size_t i = 0; i < w.units.size(); ++i)
        if (w.units[i].type == 131) hatchery = int(i);
    auto count = [&] {
        int n = 0;
        for (uint8_t c : w.creep) n += c;
        return n;
    };
    const int start = count();
    std::fill(w.creep.begin(), w.creep.end(), uint8_t(0));
    w.creepComplete(hatchery);
    const int margin = count(), from = w.frame;
    int added = 0, offBeat = 0, last = margin;
    for (int f = 0; f < 3200; ++f) {
        w.step();
        const int now = count();
        if (now > last) {
            added += now - last;
            offBeat += (w.frame - from - 1) % 16 != 0;
        }
        last = now;
    }
    const bool grew = margin == 36 && last == start && offBeat == 0 && added == start - margin;
    std::printf("Creep: a Hatchery's start creep %d tiles; finished: %d (footprint and margin), then %d tiles, each on "
                "frame 1 + 16k: %s\n", start, margin, added, grew ? "OK" : "FAIL");
    const int killed = w.frame;
    w.kill(hatchery, false);
    std::vector<int> removals;
    for (int f = 0; f < 2400 && count() > 0; ++f) {
        const int before = count();
        w.step();
        if (count() < before) removals.push_back(w.frame - killed);
    }
    bool receded = count() == 0 && !removals.empty() && removals[0] == 9;
    for (size_t k = 1; k < removals.size(); ++k) receded = receded && removals[k] - removals[k - 1] == 10;
    std::printf("Creep: the Hatchery killed: %zu tiles removed, the first %d frames later, one every 10 frames, the "
                "last at %d: %s\n", removals.size(), removals.empty() ? -1 : removals[0],
                removals.empty() ? -1 : removals.back(), receded ? "OK" : "FAIL");
    return grew && receded;
}

// Fast forward (Skip to the end): the same game with and without the pictures and sounds. Fight 1, fight 8, a
// 400-unit fight of 4 players and a mining run: the same end frame and the same HP + shields (minerals) every 24
// frames; after fast forward, images() gives each living unit its image and no effect sprite.
bool runFastForwardCheck(const sim::Tables& t, const sim::MapInfo& map, const std::filesystem::path& install,
                         const sim::FileReader& reader) {
    auto series = [&](sim::World& w, bool fast, int lastFrame) {
        w.setFastForward(fast);
        std::vector<long long> out;
        while (!sim::combatOver(w, lastFrame)) {
            if (w.frame % 24 == 0) {
                long long sum = 0;
                for (const sim::Unit& u : w.units) sum += u.alive ? u.hp + u.shields + 7 * (&u - w.units.data()) : 0;
                out.push_back(sum);
            }
            w.step();
        }
        out.push_back(w.frame);
        return out;
    };
    auto fight = [&](const Scenario& s, bool fast) {
        sim::World w(t, map);
        w.seed = 42;
        sim::CombatOptions o;
        o.a = s.a;
        o.b = s.b;
        o.players[1] = 1;
        o.x = 3808;
        o.y = 2384;
        std::string error;
        sim::setupCombat(w, o, error);
        return series(w, fast, o.frames);
    };
    auto many = [&](bool fast, bool& sane) {
        sim::World w(t, map);
        w.seed = 42;
        sim::CombatOptions o;
        o.byPlayer = true;
        const int kinds[4] = {37, 0, 65, 38};
        const int at[4][2] = {{3560, 2200}, {3950, 2200}, {3560, 2560}, {3950, 2560}};
        for (int pl = 0; pl < 4; ++pl)
            for (int n = 0; n < 100; ++n)
                o.placed.push_back({kinds[pl], pl, at[pl][0] + n % 10 * 18, at[pl][1] + n / 10 * 18});
        std::string error;
        sim::setupCombat(w, o, error);
        w.setFastForward(fast);
        for (int f = 0; f < 300; ++f) w.step();  // (then the pictures again: the units' images, no effects)
        w.setFastForward(false);
        std::vector<sim::LiveImage> images;
        w.images(images);
        int alive = 0;
        for (const sim::Unit& u : w.units) alive += u.alive && !u.hidden;
        sane = int(images.size()) >= alive && (!fast || w.effects.empty());
        return series(w, false, o.frames);
    };
    bool ok = true;
    for (int k : {0, 7}) {
        bool same = fight(kScenarios[k], false) == fight(kScenarios[k], true);
        std::printf("Fast forward: fight %d (%s): %s\n", k + 1, kScenarios[k].name, same ? "the same: OK" : "FAIL");
        ok = ok && same;
    }
    bool sane = false, saneOff = false;
    bool same = many(false, saneOff) == many(true, sane);
    std::printf("Fast forward: 400 units of 4 players (300 frames fast, then the pictures): %s; images after it %s\n",
                same ? "the same" : "different", sane ? "sane" : "not sane");
    ok = ok && same && sane;
    // A mining run: the minerals of each player every 24 frames.
    auto mining = [&](bool fast) {
        std::vector<long long> out;
        auto sim = sim::createSimulator();
        stardraft::Bytes chk;
        std::string error;
        sim::MiningOptions mo;
        mo.workers = {12};
        const std::filesystem::path orbital = install / "Maps" / "(4)Orbital Relay.scm";
        if (!sim->init(reader, error) || !stardraft::readMapChk(orbital, chk, error) ||
            !sim->setMap(chk, error) || !sim->startMining(mo, error))
            return out;
        sim->setFastForward(fast);
        for (int f = 0; f < 100; ++f) {
            sim->step(24);
            for (const sim::PlayerState& p : sim->players()) out.push_back(p.minerals * 1000 + p.gas);
        }
        return out;
    };
    std::vector<long long> slow = mining(false);
    bool sameMining = !slow.empty() && slow == mining(true);
    std::printf("Fast forward: mining, (4)Orbital Relay.scm, 12 workers a base, 2400 frames: %s\n",
                sameMining ? "the same: OK" : "FAIL");
    return ok && sameMining;
}

// Death animations: after a unit dies, its death images show for some frames, each frame in its GRP (a death
// animation's frames have no directions: Zealot, Dragoon, Marine, Zergling).
bool runDeathImageCheck(const sim::Tables& t, const sim::MapInfo& map) {
    bool ok = true;
    for (int type : {65, 66, 0, 37}) {
        sim::World w(t, map);
        w.seed = 42;
        sim::CombatOptions o;
        o.x = 3808;
        o.y = 2384;
        o.gap = 2000;  // (no fight: only the death)
        o.players[1] = 1;
        o.a = {{type, 1}};
        o.b = {{0, 1}};
        std::string error;
        if (!sim::setupCombat(w, o, error)) return false;
        int id = -1;
        for (size_t i = 0; i < w.units.size(); ++i)
            if (w.units[i].type == type) id = int(i);
        if (id < 0) return false;
        w.units[size_t(id)].heading = 128;  // (facing down: the largest direction offset)
        w.step();
        w.kill(id);
        int shown = 0, wrong = 0;
        for (int f = 0; f < 30; ++f) {
            w.step();
            std::vector<sim::LiveImage> images;
            w.images(images);
            bool any = false;
            for (const sim::LiveImage& li : images) {
                const sim::Unit& dead = w.units[size_t(id)];
                if (std::abs(li.x - dead.x) > 32 || std::abs(li.y - dead.y) > 32) continue;
                int frames = size_t(li.image) < t.imageFrames.size() ? t.imageFrames[size_t(li.image)] : 0;
                if (frames > 0 && li.frame >= frames) ++wrong;
                else any = true;
            }
            shown += any;
        }
        const int image = t.flingyImage(t.units[size_t(type)].flingy);
        bool known = size_t(image) < t.imageFrames.size() && t.imageFrames[size_t(image)] > 0;  // (its GRP was read)
        bool good = known && shown >= 10 && wrong == 0;
        std::printf("Death images: unit %d: death images in %d of 30 frames, %d frames outside their GRP: %s\n", type,
                    shown, wrong, good ? "OK" : "FAIL");
        ok = ok && good;
    }
    return ok;
}

// Missile flights: a Scout's two missiles curve out a little and home in on a Mutalisk (no circling round it). No
// missile is farther than the weapon's range + 64 pixels from the target or 48 pixels from the line Scout to target,
// and they fly at most 25 frames each on average.
bool runMissileFlightCheck(const sim::Tables& t, const sim::MapInfo& map) {
    const int kScout = 70, kMutalisk = 43;
    sim::World w(t, map);
    w.seed = 42;
    sim::CombatOptions o;
    o.x = 3808;
    o.y = 2384;
    o.players[1] = 1;
    o.a = {{kScout, 1}};
    o.b = {{kMutalisk, 1}};
    std::string error;
    if (!sim::setupCombat(w, o, error)) return false;
    for (sim::Unit& u : w.units) u.hp = 100000 << 8;
    const int weapon = t.units[kScout].airWeapon;
    const int bound = t.weapons[size_t(weapon)].maxRange + 64;
    int scout = -1;
    for (size_t j = 0; j < w.units.size(); ++j)
        if (w.units[j].type == kScout) scout = int(j);
    if (scout < 0) return false;
    int fired = 0, flightFrames = 0, far = 0, offLine = 0, lastCooldown = 0;
    double worst = 0;
    for (int f = 0; f < 300; ++f) {
        w.step();
        const int cooldown = w.units[size_t(scout)].cooldown;
        if (cooldown > lastCooldown && f < 260) fired += std::max(1, t.weapons[size_t(weapon)].factor);  // (an attack)
        lastCooldown = cooldown;
        for (const sim::Bullet& b : w.bullets) {
            if (b.weapon != weapon || !b.alive || !b.launched || b.arrived) continue;
            ++flightFrames;
            const sim::Unit& s = w.units[size_t(b.source)];
            const sim::Unit& v = w.units[size_t(b.target)];
            double dt = std::hypot(double(v.x - b.x), double(v.y - b.y));
            if (dt > bound) ++far;
            // (the distance from the segment Scout to target)
            double lx = v.x - s.x, ly = v.y - s.y, l2 = lx * lx + ly * ly;
            double u = l2 > 0 ? std::clamp(((b.x - s.x) * lx + (b.y - s.y) * ly) / l2, 0.0, 1.0) : 0;
            double off = std::hypot(s.x + u * lx - b.x, s.y + u * ly - b.y);
            worst = std::max(worst, off);
            if (off > 48) ++offLine;
        }
    }
    bool ok = fired > 0 && far == 0 && offLine == 0 && flightFrames <= fired * 25;
    std::printf("Missile flights: Scout vs Mutalisk: %d missiles, %d flight frames (at most %d), %d frames beyond %d "
                "px of the target, %d off the line (widest %.0f px): %s\n",
                fired, flightFrames, fired * 25, far, bound, offLine, worst, ok ? "OK" : "FAIL");
    return ok;
}

// The map's upgrades (rules.ini's upgrade effects): a Zergling with Metabolic Boost and Adrenal Glands reaches a
// Marine sooner and hits it more often than one without. Both live through the test (much HP).
bool runUpgradeCheck(const sim::Tables& t, const sim::MapInfo& map) {
    const int kZergling = 37, kMarine = 0, kZergPlayer = 1;
    const int upgrades[] = {t.roles[kZergling].speedUpgrade, t.roles[kZergling].cooldownUpgrade};
    struct Run {
        int firstHit = -1, hits = 0, topSpeed = 0;
    };
    auto run = [&](bool upgraded) {
        sim::MapInfo m = map;
        m.upgrades.assign(12 * 61, 0);
        if (upgraded)
            for (int u : upgrades)
                if (u >= 0 && u < 61) m.upgrades[size_t(kZergPlayer * 61 + u)] = 1;
        sim::World w(t, m);
        w.seed = 42;
        sim::CombatOptions o;
        o.x = 3808;
        o.y = 2384;
        o.gap = 320;
        o.players[1] = kZergPlayer;
        o.a = {{kMarine, 1}};
        o.b = {{kZergling, 1}};
        Run r;
        std::string error;
        if (!sim::setupCombat(w, o, error)) return r;
        for (sim::Unit& u : w.units) u.hp = 100000 << 8;
        int before = -1;
        for (int f = 0; f < 400; ++f) {
            w.step();
            for (const sim::Unit& u : w.units) {
                if (u.type == kZergling) r.topSpeed = std::max(r.topSpeed, u.speed);
                if (u.type != kMarine) continue;
                if (before >= 0 && u.hp < before) {
                    if (r.firstHit < 0) r.firstHit = f;
                    ++r.hits;
                }
                before = u.hp;
            }
        }
        return r;
    };
    Run plain = run(false), fast = run(true);
    bool ok = upgrades[0] >= 0 && upgrades[1] >= 0 && plain.firstHit >= 0 && fast.firstHit >= 0 &&
              fast.topSpeed > plain.topSpeed && fast.firstHit < plain.firstHit && fast.hits > plain.hits;
    std::printf("Upgrades: Zergling vs Marine without / with Metabolic Boost and Adrenal Glands: top speed %.2f / %.2f "
                "px a frame, first hit at frame %d / %d, %d / %d hits in 400 frames: %s\n",
                plain.topSpeed / 256.0, fast.topSpeed / 256.0, plain.firstHit, fast.firstHit, plain.hits, fast.hits,
                ok ? "OK" : "FAIL");
    return ok;
}

// Fights of several players (CombatOptions::byPlayer): allies never attack each other, and the result lists the
// teams and the one left. `groups`: (unit type, player, count, x, y) of placed units, 20 px apart.
bool runTeamFight(const sim::Tables& t, const sim::MapInfo& map, const char* name,
                  const std::vector<std::array<int, 5>>& groups, const int (&team)[8], int expected = -2) {
    sim::World w(t, map);
    w.seed = 42;
    sim::CombatOptions o;
    o.byPlayer = true;
    std::copy(team, team + 8, o.team);
    for (const auto& g : groups)
        for (int n = 0; n < g[2]; ++n) o.placed.push_back({g[0], g[1], g[3] + n % 4 * 20, g[4] + n / 4 * 20});
    std::string error;
    if (!sim::setupCombat(w, o, error)) {
        std::printf("Teams: %s: %s\n", name, error.c_str());
        return false;
    }
    int allyTargets = 0;
    while (!sim::combatOver(w, o.frames)) {
        w.step();
        for (const sim::Unit& u : w.units)
            if (u.alive && u.target >= 0 && w.units[size_t(u.target)].side == u.side) ++allyTargets;
    }
    sim::CombatResult r = sim::combatResultOf(w);
    int left = 0;
    std::printf("Teams: %s: %d frames,", name, r.frames);
    for (const sim::CombatSide& c : r.teams) {
        std::printf(" team %d (players", c.team);
        for (int p : c.players) std::printf(" %d", p);
        std::printf(") %d of %d alive;", c.alive, c.units);
        left += c.alive ? 1 : 0;
    }
    bool winnerOk = left == 1 ? std::any_of(r.teams.begin(), r.teams.end(),
                                            [&](const sim::CombatSide& c) { return c.alive && c.team == r.winner; })
                              : r.winner == -1;
    bool ok = allyTargets == 0 && winnerOk && (expected == -2 || r.winner == expected);
    std::printf(" winner %d, ally targets %d: %s\n", r.winner, allyTargets, ok ? "OK" : "FAIL");
    return ok;
}

// The static defences: three probe fights of placed units at the first start location; side A's
// defences at (3744, 2344) and (3744, 2424), its Marines at (3712, 2384), side B at (4000, 2384). HP + shields every 24
// frames beside the reference.
double runDefenceFights(const sim::Tables& t, const sim::MapInfo& map) {
    struct Def {
        const char* name;
        int defence, marines, enemy, enemies;
        char winner;
        int frames;
        std::vector<int> seriesA, seriesB;
    };
    const Def fights[] = {
        {"2 Photon Cannon vs 8 Zergling", 162, 0, 37, 8, 'A', 237,
         {400, 400, 375, 330, 292, 233, 194, 178, 164, 144, 130}, {280, 280, 210, 210, 175, 140, 105, 50, 30, 16, 0}},
        {"2 Sunken Colony vs 8 Marine", 146, 0, 0, 8, 'A', 251,
         {600, 588, 560, 540, 521, 489, 469, 447, 435, 412, 409, 405},
         {320, 280, 280, 240, 200, 160, 160, 120, 80, 40, 40, 0}},
        {"2 Missile Turret + 4 Marine vs 8 Mutalisk", 124, 4, 43, 8, 'B', 251,
         {560, 560, 499, 418, 337, 295, 203, 162, 103, 85, 29, 0},
         {960, 928, 884, 822, 784, 736, 684, 663, 631, 612, 603, 593}},
    };
    double total = 0;
    std::printf("\nDefences: measured | reference\n");
    for (const Def& d : fights) {
        sim::World w(t, map);
        w.seed = 42;
        sim::CombatOptions o;
        o.players[1] = 1;
        o.placed = {{d.defence, 0, 3744, 2344}, {d.defence, 0, 3744, 2424}};
        for (int n = 0; n < d.marines; ++n) o.placed.push_back({0, 0, 3712, 2384});
        for (int n = 0; n < d.enemies; ++n) o.placed.push_back({d.enemy, 1, 4000, 2384});
        std::string error;
        if (!sim::setupCombat(w, o, error)) {
            std::printf("  %s: %s\n", d.name, error.c_str());
            total += 100;
            continue;
        }
        Fight f;
        for (;;) {
            bool over = sim::combatOver(w, o.frames);
            if (w.frame % 24 == 0 || over)
                for (int side = 0; side < 2; ++side) {
                    int alive, hp, sh;
                    sums(w, side, alive, hp, sh);
                    f.series[side].push_back(hp + sh);
                }
            if (over) break;
            w.step();
        }
        for (int side = 0; side < 2; ++side) sums(w, side, f.alive[side], f.hp[side], f.sh[side]);
        char winner = w.timedOut ? '-' : f.alive[0] ? 'A' : 'B';
        double ea = seriesError(f.series[0], d.seriesA), eb = seriesError(f.series[1], d.seriesB);
        total += (ea + eb) / 2;
        std::printf("  %-42s %c|%c %4d|%4d  A %d alive %d HP+shields, B %d alive %d  series A %4.1f%% B %4.1f%%\n",
                    d.name, winner, d.winner, w.frame, d.frames, f.alive[0], f.hp[0] + f.sh[0], f.alive[1],
                    f.hp[1] + f.sh[1],
                    ea, eb);
    }
    std::printf("Defences: mean series difference %.1f%%\n", total / 3);
    return total / 3;
}

// The sounds of a live fight (from the scripts): fight 1 gives the Marines' attack sounds and the Zerglings' (and
// Marines') death sounds, all valid sfxdata ids.
bool runSoundCheck(const sim::Tables& t, const sim::MapInfo& map) {
    sim::World w(t, map);
    w.seed = 42;
    w.recordSounds = true;
    sim::CombatOptions o;
    o.a = kScenarios[0].a;
    o.b = kScenarios[0].b;
    o.players[1] = 1;
    o.x = 3808;
    o.y = 2384;
    std::string error;
    if (!sim::setupCombat(w, o, error)) return false;
    int deathsB = 0, before = 0;
    while (!sim::combatOver(w, o.frames)) w.step();
    std::vector<int> ids[2];
    for (const sim::LiveSound& snd : w.sounds)
        if (snd.owner >= 0 && snd.owner < 2 && std::find(ids[snd.owner].begin(), ids[snd.owner].end(), snd.sfx) ==
                                                    ids[snd.owner].end())
            ids[snd.owner].push_back(snd.sfx);
    for (const sim::Unit& u : w.units) deathsB += u.side == 1 && !u.alive ? 1 : 0;
    before = int(w.sounds.size());
    bool valid = std::all_of(w.sounds.begin(), w.sounds.end(), [](const sim::LiveSound& snd) {
        return snd.sfx > 0 && snd.sfx < 1144;
    });
    bool ok = valid && !ids[0].empty() && !ids[1].empty() && deathsB > 0;
    std::printf("Sounds: fight 1: %d sounds, %zu kinds from the Marines, %zu from the Zerglings (%d Zerglings died), "
                "all valid sfxdata ids: %s\n", before, ids[0].size(), ids[1].size(), deathsB, ok ? "OK" : "FAIL");
    return ok;
}

// PERF: milliseconds per game frame (targets: a 200-unit fight under 1 ms, 400 units of 4 players under 2 ms, no frame
// of an unreachable order on a 256 x 256 map over 4 ms; the fail limits are 10 times as much, for slow machines).
bool runPerf(const sim::Tables& t, const sim::MapInfo& map, const std::filesystem::path& install,
             const sim::FileReader& reader) {
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    bool ok = true;
    std::printf("\nPERF (ms per game frame, Release):\n");
    // (a) Mining: a 4-player map, 8 bases, 12 workers each.
    {
        auto sim = sim::createSimulator();
        stardraft::Bytes chk;
        std::string error;
        sim::MiningOptions mo;
        mo.workers = {12};
        if (sim->init(reader, error) && stardraft::readMapChk(install / "Maps" / "(4)Orbital Relay.scm", chk, error) &&
            sim->setMap(chk, error) && sim->startMining(mo, error)) {
            auto a = Clock::now();
            sim->step(2000);
            double per = ms(a, Clock::now()) / 2000;
            std::printf("  mining, (4)Orbital Relay.scm, %zu bases, 12 workers each: %.3f ms\n",
                        sim->players().size(), per);
            ok = ok && per < 10;
        }
    }
    // (b) A 200-unit fight on Bottleneck.
    {
        sim::World w(t, map);
        sim::CombatOptions o;
        o.a = {{37, 100}, {38, 50}};
        o.b = {{0, 60}, {34, 20}, {5, 20}};
        o.players[1] = 1;
        o.x = 3808;
        o.y = 2384;
        std::string error;
        sim::setupCombat(w, o, error);
        double total = 0, worst = 0;
        int frames = 0;
        while (!sim::combatOver(w, o.frames)) {
            auto a = Clock::now();
            w.step();
            double f = ms(a, Clock::now());
            total += f;
            worst = std::max(worst, f);
            ++frames;
        }
        std::printf("  200 units (100 Zergling + 50 Hydralisk vs 60 Marine + 20 Medic + 20 Siege Tank): %.3f ms (most "
                    "%.2f), %d frames\n", total / std::max(1, frames), worst, frames);
        ok = ok && total / std::max(1, frames) < 10;
    }
    // (c) 400 and 800 units of 4 players, each against all (with the end test).
    for (int each : {100, 200}) {
        sim::World w(t, map);
        sim::CombatOptions o;
        o.byPlayer = true;
        const int kinds[4] = {37, 0, 65, 38};
        const int at[4][2] = {{3560, 2200}, {3950, 2200}, {3560, 2560}, {3950, 2560}};
        for (int pl = 0; pl < 4; ++pl)
            for (int n = 0; n < each; ++n)
                o.placed.push_back({kinds[pl], pl, at[pl][0] + n % 10 * 18, at[pl][1] + n / 10 * 18});
        std::string error;
        sim::setupCombat(w, o, error);
        double total = 0, worst = 0;
        int frames = 0;
        for (;;) {
            auto a = Clock::now();
            bool over = sim::combatOver(w, o.frames);
            if (!over) w.step();
            double f = ms(a, Clock::now());
            if (over) break;
            total += f;
            worst = std::max(worst, f);
            ++frames;
        }
        sim::CombatResult r = sim::combatResultOf(w);
        int left = 0;
        for (const sim::CombatSide& c : r.teams) left += c.alive;
        std::printf("  %d units of 4 players: %.3f ms (most %.2f), %d frames, winner %d with %d left\n", 4 * each,
                    total / std::max(1, frames), worst, frames, r.winner, left);
        ok = ok && total / std::max(1, frames) < (each == 100 ? 20 : 40);
    }
    // (d) A ground unit ordered to an unreachable place on a 256 x 256 map: the most of one frame.
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(install / "Maps", ec)) {
            stardraft::Bytes chk;
            std::string error;
            sim::MapInfo m;
            if (entry.path().extension() != ".scm" || !stardraft::readMapChk(entry.path(), chk, error) ||
                !m.load(chk, t, error) || m.width != 256 || m.height != 256)
                continue;
            int sx = -1, sy = -1;
            for (const sim::MapInfo::Placed& pl : m.units)
                if (pl.type == 214 && sx < 0) sx = pl.x, sy = pl.y;
            sim::World w(t, m);
            w.combat = true;
            int id = w.add(0, 0, sx, sy);
            double worst = 0;
            for (int f = 0; f < 300; ++f) {
                auto a = Clock::now();
                if (f == 0) w.setMove(w.units[size_t(id)], 8, 8100);
                w.step();
                worst = std::max(worst, ms(a, Clock::now()));
            }
            std::printf("  an unreachable order, %s: the most of one frame %.2f ms\n",
                        entry.path().filename().string().c_str(), worst);
            ok = ok && worst < 40;
            break;
        }
    }
    // (e) The editor's case: all the map's units passive, and 30 units of each of 3 players at start locations plus 30
    // of a 4th player anywhere (some where they cannot be reached, or only round passive units): the most of one frame
    // (target under 4 ms) and the searches of the frame with the most.
    for (const char* name : {"(8)The Hunters.scm", "(2)Bottleneck.scm"}) {
        stardraft::Bytes chk;
        std::string error;
        sim::MapInfo m;
        if (!stardraft::readMapChk(install / "Maps" / name, chk, error) || !m.load(chk, t, error)) continue;
        sim::World w(t, m);
        sim::CombatOptions o;
        o.byPlayer = true;
        o.frames = 3000;
        std::vector<std::pair<int, int>> starts;
        for (const sim::MapInfo::Placed& p : m.units) {
            if (p.type == 214) starts.push_back({p.x, p.y});
            else o.placed.push_back({p.type, p.owner, p.x, p.y, true});
        }
        const int kinds[4] = {0, 37, 65, 38};
        for (int pl = 0; pl < 3; ++pl)
            for (int n = 0; n < 30; ++n) {
                auto [sx, sy] = starts[size_t(pl) % starts.size()];
                o.placed.push_back({kinds[(pl + n) % 4], pl, sx + 64 + n % 4 * 24, sy + n / 4 * 24});
            }
        uint32_t r = 7;
        for (int n = 0; n < 30; ++n) {
            int x, y;
            do {
                r = r * 1103515245 + 12345;
                x = int((r >> 8) % uint32_t(m.width * 32));
                r = r * 1103515245 + 12345;
                y = int((r >> 8) % uint32_t(m.height * 32));
            } while (!m.walkable(x, y));
            o.placed.push_back({kinds[n % 4], 3, x, y});
        }
        if (!sim::setupCombat(w, o, error)) continue;
        double total = 0, worst = 0;
        long long most = 0;
        int frames = 0;
        while (!sim::combatOver(w, o.frames)) {
            long long before = w.pathSearches;
            auto a = Clock::now();
            w.step();
            double f = ms(a, Clock::now());
            total += f;
            worst = std::max(worst, f);
            most = std::max(most, w.pathSearches - before);
            ++frames;
        }
        sim::CombatResult res = sim::combatResultOf(w);
        std::printf("  %s, its %zu units passive, 120 selected of 4 players: %.3f ms (most %.2f), %d frames, at most "
                    "%lld searches in a frame, %lld in all%s\n",
                    name, m.units.size(), total / std::max(1, frames), worst, frames, most, w.pathSearches,
                    res.stalemate ? ", stalemate" : res.timedOut ? ", timed out" : "");
        ok = ok && worst < 40;
    }
    std::printf("PERF: %s\n", ok ? "OK" : "FAIL (over 10 times the targets)");
    return ok;
}

// Search and destroy (placed units): a straggler far away is hunted down; an enemy on an island ends the fight as
// unreachable (no time-out).
bool runSeekChecks(const sim::Tables& t, const sim::MapInfo& map, const std::filesystem::path& install) {
    bool ok = true;
    {
        sim::World w(t, map);
        w.seed = 42;
        sim::CombatOptions o;
        o.byPlayer = true;
        for (int n = 0; n < 8; ++n) o.placed.push_back({0, 0, 3780 + n % 4 * 18, 2370 + n / 4 * 18});
        for (int n = 0; n < 6; ++n) o.placed.push_back({37, 1, 3940 + n % 3 * 18, 2370 + n / 3 * 18});
        o.placed.push_back({37, 1, 2196, 1719});  // (far away, round the turns of the way across)
        std::string error;
        sim::setupCombat(w, o, error);
        while (!sim::combatOver(w, o.frames)) w.step();
        sim::CombatResult r = sim::combatResultOf(w);
        bool good = !r.timedOut && r.winner == 0;
        std::printf("Search and destroy: 8 Marines vs 6 Zerglings + 1 far away: winner %d after %d frames, Zerglings "
                    "left %d: %s\n", r.winner, r.frames, r.teams.size() > 1 ? r.teams[1].alive : -1,
                    good ? "OK" : "FAIL");
        ok = ok && good;
    }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(install / "Maps", ec)) {
        stardraft::Bytes chk;
        std::string error;
        sim::MapInfo m;
        if (entry.path().extension() != ".scm" || !stardraft::readMapChk(entry.path(), chk, error) ||
            !m.load(chk, t, error))
            continue;
        std::vector<std::pair<int, int>> starts;
        for (const sim::MapInfo::Placed& pl : m.units)
            if (pl.type == 214) starts.push_back({pl.x, pl.y});
        auto group = [&](int x, int y) {
            return m.regions[size_t(m.tileRegion[size_t(y / 32 * m.width + x / 32)])].group;
        };
        if (starts.size() < 2 || group(starts[0].first, starts[0].second) == group(starts[1].first, starts[1].second))
            continue;
        sim::World w(t, m);
        w.seed = 42;
        sim::CombatOptions o;
        o.byPlayer = true;
        for (int n = 0; n < 4; ++n) o.placed.push_back({0, 0, starts[0].first + n * 18, starts[0].second});
        o.placed.push_back({37, 1, starts[1].first, starts[1].second});
        sim::setupCombat(w, o, error);
        while (!sim::combatOver(w, o.frames)) w.step();
        sim::CombatResult r = sim::combatResultOf(w);
        int unreachable = 0;
        for (const sim::CombatSide& c : r.teams) unreachable += c.team == 1 ? c.unreachable : 0;
        bool good = !r.timedOut && unreachable == 1;
        std::printf("Unreachable: %s, 4 Marines vs a Zergling on another island: ends after %d frames, %d unreachable, "
                    "time-out %d: %s\n", entry.path().filename().string().c_str(), r.frames, unreachable,
                    int(r.timedOut), good ? "OK" : "FAIL");
        return ok && good;
    }
    std::printf("Unreachable: no map with islands in the Maps folder\n");
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path install = "C:\\Program Files (x86)\\Starcraft Remastered\\StarCraft";
    bool showSeries = false, seeds = false;
    int traceDetail = 0;
    std::filesystem::path tracePath = "tools/tests/sim-reference/reference-traces.md";
    std::filesystem::path checkPath = "tools/tests/sim-reference/reference-check.md";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--series")) showSeries = true;
        else if (!std::strcmp(argv[i], "--traces") && i + 1 < argc) tracePath = argv[++i];
        else if (!std::strcmp(argv[i], "--trace") && i + 1 < argc) traceDetail = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seeds")) seeds = true;
        else if (!std::strcmp(argv[i], "--check") && i + 1 < argc) checkPath = argv[++i];
        else install = argv[i];
    }
    stardraft::GameData data;
    std::string error;
    if (!data.open(install, error)) {
        std::printf("FAIL: the game data: %s\n", error.c_str());
        return 1;
    }
    auto sim = sim::createSimulator();
    auto reader = [&](const std::string& name, std::vector<uint8_t>& out) {
        std::string e;
        return data.readFile(name, out, e);
    };
    stardraft::Bytes chk;
    sim::Tables tables;
    sim::MapInfo map;
    if (!sim->init(reader, error) || !stardraft::readMapChk(install / "Maps" / "(2)Bottleneck.scm", chk, error) ||
        !sim->setMap(chk, error) || !tables.load(reader, error) || !map.load(chk, tables, error)) {
        std::printf("FAIL: %s\n", error.c_str());
        return 1;
    }
    int failures = 0;

    // Mining: 9 workers at the start location at (3808, 2384).
    sim::MiningOptions mo;
    mo.workers = {9};
    mo.mineralClusters = false;
    std::vector<sim::MiningBase> bases;
    if (!sim->mining(mo, bases, error)) {
        std::printf("FAIL: mining: %s\n", error.c_str());
        return 1;
    }
    const double refRate[3] = {541.4, 549.4, 546.8};  // (by sim::Race)
    const std::vector<double> refTrips[3] = {
        {}, {180.3, 183.7, 191.8, 193.5, 179.7, 198.3, 192.2}, {176.3, 170.4, 178.3, 205.0, 190.7, 198.0, 197.3}};
    std::printf("Mining, (2)Bottleneck.scm, 9 workers, 720 frames warm-up, 4286 frames measured\n");
    for (const sim::MiningBase& b : bases) {
        if (b.x != 3808 || b.y != 2384) continue;
        for (const sim::MiningRun& r : b.runs) {
            std::printf("  %-7s %6.1f minerals a minute (reference %.1f)\n    trips:", raceName(r.race),
                        r.mineralsPerMinute, refRate[r.race]);
            for (double f : r.tripFrames) std::printf(" %.1f", f);
            if (!refTrips[r.race].empty()) {
                std::printf("\n    ref:  ");
                for (double f : refTrips[r.race]) std::printf(" %.1f", f);
            }
            std::printf("\n");
        }
    }

    // A long run with many workers (the editor allows 1 to 60 minutes and 1 to 60 workers): 60 minutes measured, 60
    // workers, gas with 2 workers on the refinery, each start location's race from the map (SIDE).
    {
        sim::MiningOptions lo;
        lo.workers = {60};
        lo.races = {sim::Terran};
        lo.placedDepots = false;
        lo.mineralClusters = false;
        lo.frames = 60 * 60000 / 42;
        lo.gas = true;
        lo.gasWorkers = 2;
        lo.mapRaces = true;
        lo.seed = 7;
        std::vector<sim::MiningBase> long_;
        auto start = std::chrono::steady_clock::now();
        bool ok = sim->mining(lo, long_, error) && !long_.empty();
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        for (const sim::MiningBase& b : long_)
            for (const sim::MiningRun& r : b.runs) {
                std::printf("Long run: base (%d, %d), %s, 60 workers, 60 minutes: %.1f minerals and %.1f gas a "
                            "minute\n",
                            b.x, b.y, raceName(r.race), r.mineralsPerMinute, r.gasPerMinute);
                ok = ok && r.mineralsPerMinute > 0 && r.gasPerMinute > 0;
            }
        std::printf("Long run: %s in %.1f s\n", ok ? "done" : "FAIL", seconds);
        failures += ok ? 0 : 1;
    }

    // The 15 fights (reference-combat): winner, frames, survivors (alive, HP, shields), and the time series error.
    std::printf("\nCombat: measured | reference. Series: the mean difference of HP + shields every 24 frames, in %% of "
                "the side's start.\n");
    double total = 0, seedSeries = 0, seedFrames = 0;
    int winners = 0;
    for (size_t n = 0; n < std::size(kScenarios); ++n) {
        const Scenario& s = kScenarios[n];
        Fight f = runFight(tables, map, s, 42);
        double ea = seriesError(f.series[0], s.seriesA), eb = seriesError(f.series[1], s.seriesB);
        total += (ea + eb) / 2;
        winners += f.winner == s.winner ? 1 : 0;
        std::printf("%2zu %-48s %c|%c %4d|%4d", n + 1, s.name, f.winner, s.winner, f.frames, s.frames);
        std::printf("  A %2d %4d %3d | %2d %4d %3d", f.alive[0], f.hp[0], f.sh[0], s.aliveA, s.hpA, s.shA);
        std::printf("  B %2d %4d %3d | %2d %4d %3d", f.alive[1], f.hp[1], f.sh[1], s.aliveB, s.hpB, s.shB);
        std::printf("  series A %4.1f%% B %4.1f%%\n", ea, eb);
        if (n < 3) {
            static const int ref[3][4] = {{18, 24, 25, 20}, {34, 18, 20, 37}, {13, 4, 8, 15}};
            std::printf("   first shot A %d|%d B %d|%d,", f.firstShot[0], ref[n][0], f.firstShot[1], ref[n][1]);
            std::printf(" first damage taken A %d|%d B %d|%d\n", f.firstDamage[0], ref[n][2], f.firstDamage[1],
                        ref[n][3]);
        }
        if (showSeries)
            for (int side = 0; side < 2; ++side) {
                const std::vector<int>& r = side ? s.seriesB : s.seriesA;
                std::printf("   %c:", side ? 'B' : 'A');
                for (size_t i = 0; i < std::max(r.size(), f.series[side].size()); ++i)
                    std::printf(" %d|%d", i < f.series[side].size() ? f.series[side][i] : -1, i < r.size() ? r[i] : -1);
                std::printf("\n");
            }
        if (seeds) {
            std::vector<int> frames;
            int same = 0;
            double series = 0, off = 0;  // (each seed against the reference's seed 42)
            for (uint32_t seed = 1; seed <= 20; ++seed) {
                Fight g = runFight(tables, map, s, seed);
                frames.push_back(g.frames);
                same += g.winner == s.winner ? 1 : 0;
                series += (seriesError(g.series[0], s.seriesA) + seriesError(g.series[1], s.seriesB)) / 2 / 20;
                off += std::abs(g.frames - s.frames) / 20.0;
            }
            seedSeries += series / double(std::size(kScenarios));
            seedFrames += off / double(std::size(kScenarios));
            std::sort(frames.begin(), frames.end());
            std::printf("   20 seeds: the reference's winner %d of 20, median %d frames (%d to %d); mean series "
                        "difference %.1f%%, mean frame difference %.0f\n",
                        same, frames[10], frames.front(), frames.back(), series, off);
        }
    }
    std::printf("Winners as the reference: %d of %zu; mean series difference %.1f%%\n", winners,
                std::size(kScenarios), total / double(std::size(kScenarios)));
    if (seeds)
        std::printf("20 seeds: mean series difference %.1f%%, mean frame difference %.0f (against the reference's seed "
                    "42)\n", seedSeries, seedFrames);

    runCheck(tables, map, checkPath, seeds, install, reader);
    {
        stardraft::Bytes orbitalChk;
        sim::MapInfo orbital;
        bool haveOrbital = stardraft::readMapChk(install / "Maps" / "(4)Orbital Relay.scm", orbitalChk, error) &&
                           orbital.load(orbitalChk, tables, error);
        if (!runCorridorCheck(tables)) ++failures;
        if (!runSeekChecks(tables, map, install)) ++failures;
        if (!runPerf(tables, map, install, reader)) ++failures;
        if (!runFastForwardCheck(tables, map, install, reader)) ++failures;
        if (!runMiningCollisionCheck(tables, install)) ++failures;
        if (!runGatherPictureCheck(tables, map)) ++failures;
        if (!runGatherSparkCheck(tables, map)) ++failures;
        if (!runDamageOverlayCheck(tables, map)) ++failures;
        if (!runUnitFreeForAllCheck(tables, map)) ++failures;
        if (!runCreepCheck(tables, map)) ++failures;
        if (!runSoundCheck(tables, map)) ++failures;
        runDefenceFights(tables, map);
        {  // (players 0, 1 and 2: 1 and 2 allied; then each player alone)
            const int allied[8] = {0, 1, 1, 3, 4, 5, 6, 7}, alone[8] = {0, 1, 2, 3, 4, 5, 6, 7};
            if (!runTeamFight(tables, map, "8 Marine (P0) vs 6 Zergling (P1) + 6 Zergling (P2), P1 and P2 allied",
                              {{0, 0, 8, 3680, 2360}, {37, 1, 6, 3920, 2340}, {37, 2, 6, 3920, 2420}}, allied))
                ++failures;
            if (!runTeamFight(tables, map, "free for all: 6 Marine (P0), 6 Zergling (P1), 4 Zealot (P2)",
                              {{0, 0, 6, 3680, 2360}, {37, 1, 6, 3920, 2340}, {65, 2, 4, 3800, 2500}}, alone))
                ++failures;
            // (units that cannot attack are targets: the Marines win and all of player 1's units die)
            if (!runTeamFight(tables, map, "4 Marine (P0) vs 2 Overlord + 4 Drone (P1)",
                              {{0, 0, 4, 3680, 2360}, {42, 1, 2, 3900, 2340}, {41, 1, 4, 3900, 2420}}, alone, 0))
                ++failures;
        }
        if (!runImageCheck(tables, map)) ++failures;
        if (!runVolleyCheck(tables, map)) ++failures;
        if (!runMissileFlightCheck(tables, map)) ++failures;
        if (!runElevationCheck(tables, map)) ++failures;
        if (!runArchonImageCheck(tables, map)) ++failures;
        // (Zealots, Zerglings and Marines on buildings: 2 Photon Cannons, a Sunken Colony, a Nexus, a Command Center)
        if (!runBuildingFight(tables, map, "6 Zealot vs 2 Photon Cannon",
                              {{65, 1, 6, 3600, 2350}, {162, 2, 1, 3850, 2330}, {162, 2, 1, 3850, 2400}}))
            ++failures;
        if (!runBuildingFight(tables, map, "6 Zergling vs Sunken Colony",
                              {{37, 1, 6, 3600, 2350}, {146, 2, 1, 3850, 2350}}))
            ++failures;
        if (!runBuildingFight(tables, map, "12 Zealot vs Nexus", {{65, 1, 12, 3600, 2350}, {154, 2, 1, 3880, 2350}}))
            ++failures;
        if (!runBuildingFight(tables, map, "12 Zergling vs Command Center",
                              {{37, 1, 12, 3600, 2350}, {106, 2, 1, 3880, 2350}}))
            ++failures;
        if (!runBuildingFight(tables, map, "5 Zealot vs Bunker", {{65, 1, 5, 3600, 2350}, {125, 2, 1, 3850, 2350}}))
            ++failures;
        if (!runDeathImageCheck(tables, map)) ++failures;
        if (!runUpgradeCheck(tables, map)) ++failures;
        if (!runHiddenImageCheck(tables, map)) ++failures;
        if (!runPassiveCheck(tables, map)) ++failures;
        if (!runStalemateCheck(tables, map)) ++failures;
        if (!runSearchTiming(tables, install)) ++failures;
        if (!runDeathCheck(tables, map)) ++failures;
        // Spider Mines and Siege Tanks in Siege Mode: they work (no reference numbers yet).
        Scenario mines{"3 Spider Mine + 4 Marine vs 8 Zergling", {{13, 3}, {0, 4}}, {{37, 8}}, '-', 0, 0, 0, 0, 0, 0, 0,
                       {}, {}};
        Scenario marines{"4 Marine vs 8 Zergling", {{0, 4}}, {{37, 8}}, '-', 0, 0, 0, 0, 0, 0, 0, {}, {}};
        Fight fm = runFight(tables, map, mines, 42), f0 = runFight(tables, map, marines, 42);
        Scenario siege{"2 Siege Tank (Siege Mode) vs 12 Zergling", {{30, 2}}, {{37, 12}}, '-', 0, 0, 0, 0, 0, 0, 0,
                       {}, {}};
        Fight fs = runFight(tables, map, siege, 42);
        bool minesOk = fm.hp[1] + fm.alive[1] * 1000 < f0.hp[1] + f0.alive[1] * 1000 || fm.alive[0] > f0.alive[0];
        std::printf("Spider Mines: %s: side A left %d, Zerglings left %d with %d HP (without the mines: %d, %d, %d "
                    "HP): %s\n", mines.name, fm.alive[0], fm.alive[1], fm.hp[1], f0.alive[0], f0.alive[1], f0.hp[1],
                    minesOk ? "OK" : "FAIL");
        std::printf("Siege Mode: %s: winner %c after %d frames, Zerglings left %d, first shot frame %d\n", siege.name,
                    fs.winner, fs.frames, fs.alive[1], fs.firstShot[0]);
        failures += minesOk ? 0 : 1;
        if (!runPathChecks(tables, map, haveOrbital ? &orbital : nullptr)) {
            std::printf("FAIL: paths\n");
            ++failures;
        }
    }

    // The traces of the reference: placement, crowding, per-unit positions.
    std::vector<Table> traceTables = readTables(tracePath);
    if (traceTables.empty()) std::printf("Traces: %s cannot be read (run from the repository's root, or --traces)\n",
                                         tracePath.string().c_str());
    else compareTraces(traceTables, tables, map, traceDetail);

    // The API gives the same fight as the World above.
    {
        sim::CombatOptions co;
        co.a = kScenarios[0].a;
        co.b = kScenarios[0].b;
        co.players[1] = 1;
        co.x = 3808;
        co.y = 2384;
        sim::CombatResult r;
        Fight f = runFight(tables, map, kScenarios[0], 42);
        bool ok = sim->combat(co, r, error) && r.frames == f.frames && r.b.alive == f.alive[1];
        std::printf("API: fight 1 through Simulator::combat: %s\n", ok ? "the same" : "FAIL: not the same");
        failures += ok ? 0 : 1;
    }

    // The map's upgrades change a fight: the same map with a PUPx section that gives player 0 (the Marines) level 3
    // Infantry Weapons and Armor and U-238 Shells (PUPx: per player maximum and start levels of 61 upgrades, the
    // defaults, then per player whether the defaults apply).
    {
        stardraft::Chk upgraded = stardraft::Chk::parse(chk);
        stardraft::Bytes pup(2318, 0);
        for (int p = 0; p < 12; ++p)
            for (int k = 0; k < 61; ++k) {
                pup[size_t(p * 61 + k)] = 3;             // (maximum)
                pup[size_t(1586 + p * 61 + k)] = p != 0;  // (use the defaults: all but player 0)
            }
        pup[732 + 0] = 3;   // Terran Infantry Armor
        pup[732 + 7] = 3;   // Terran Infantry Weapons
        pup[732 + 16] = 1;  // U-238 Shells
        upgraded.set("PUPx", pup);
        sim::CombatOptions co;
        co.a = kScenarios[0].a;
        co.b = kScenarios[0].b;
        co.players[1] = 1;
        co.x = 3808;
        co.y = 2384;
        sim::CombatResult plain, better;
        bool ok = sim->combat(co, plain, error) && sim->setMap(upgraded.serialize(), error) &&
                  sim->combat(co, better, error) && sim->setMap(chk, error);
        ok = ok && better.b.alive < plain.b.alive && better.a.alive >= plain.a.alive;
        std::printf("Upgrades: fight 1 with the Marines at +3 weapons, +3 armor and U-238 Shells: Marines %d alive (%d "
                    "without), Zerglings %d alive (%d without): %s\n",
                    better.a.alive, plain.a.alive, better.b.alive, plain.b.alive, ok ? "the upgrades count" : "FAIL");
        failures += ok ? 0 : 1;
    }

    // Movement traces: a Marine and a Zergling walk 320 pixels to the east (the distance every 4 frames).
    struct Trace {
        int type;
        std::vector<int> ref;
    };
    const Trace traces[] = {
        {0, {0, 4, 20, 36, 52, 68, 84, 100, 116, 132, 148, 164, 180, 196, 212, 228, 244, 260, 276, 292}},
        {37, {0, 8, 35, 56, 73, 97, 114, 139, 158, 185, 201, 221, 245, 260, 288, 307}},
    };
    for (const Trace& tr : traces) {
        sim::World w(tables, map);  // (as in a fight: Attack Move, from the first order tick)
        w.combat = true;
        int id = w.add(tr.type, 0, 3680, 2384);
        sim::Unit& u = w.units[size_t(id)];
        u.heading = u.velDir = 64;
        u.amX = 4000;
        u.amY = 2384;
        w.setOrder(u, sim::AttackMove);
        std::printf("Move %s:", tr.type ? "Zergling" : "Marine");
        for (size_t i = 0; i < tr.ref.size(); ++i) {
            if (i) for (int k = 0; k < 4; ++k) w.step();
            std::printf(" %d|%d", w.units[size_t(id)].x - 3680, tr.ref[i]);
        }
        std::printf("\n");
    }

    // Paths: a Zergling at one start location and a Marine at the other meet through the map's chokes.
    if (bases.size() >= 2) {
        sim::CombatOptions po;
        po.placed = {{37, 0, bases[1].x, bases[1].y}, {0, 1, bases[0].x, bases[0].y}};
        po.frames = 4000;
        sim::CombatResult r;
        bool ok = sim->combat(po, r, error) && !r.timedOut;
        std::printf("Paths: a Zergling from (%d, %d) and a Marine from (%d, %d): %s after %d frames\n", bases[1].x,
                    bases[1].y, bases[0].x, bases[0].y, ok ? "they fought" : "FAIL: they did not meet", r.frames);
        failures += ok ? 0 : 1;
    }

    // The live API: a few frames of a mining game, and its images.
    sim::MiningOptions live;
    if (!sim->startMining(live, error) || !sim->step(100) || sim->frame() != 100) {
        std::printf("FAIL: live mining: %s\n", error.c_str());
        ++failures;
    } else {
        std::vector<sim::LiveImage> images;
        sim->images(images);
        std::printf("Live mining: frame %d of %d, %zu players, %zu images\n", sim->frame(), sim->lastFrame(),
                    sim->players().size(), images.size());
        // The workers' loads: after the first trips some workers carry, drawn above them.
        sim->step(300);
        sim->images(images);
        int carried = 0, offset = 0;
        for (size_t i = 1; i < images.size(); ++i)
            if (images[i].image >= 397 && images[i].image <= 404 && images[i].order == 1) {
                ++carried;
                if (images[i].x != images[i - 1].x || images[i].y != images[i - 1].y) ++offset;
            }
        bool ok = carried > 0 && offset == carried && tables.carryOffsets.size() == 3;
        std::printf("Live mining: frame %d, %d loads drawn (%d at an offset from the worker), offsets of %zu workers: "
                    "%s\n", sim->frame(), carried, offset, tables.carryOffsets.size(), ok ? "OK" : "FAIL");
        failures += ok ? 0 : 1;
        sim->stop();
    }
    sim::MapRunInfo info;
    if (sim->startMap({}, info, error)) ++failures;
    std::printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
