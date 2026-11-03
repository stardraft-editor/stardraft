// The Bridge tool's checks (src/bridges.cpp): with the example maps of the bridge research (the approved bridges over
// their rivers), each drag across a river must give the bridge of the example: the same orientation, length and
// outline. Without the example maps: a bridge over a river that the test paints.
//   bridge_test <install dir> [<example maps folder: examples_<tileset>.scm>]
#include "stardraft/bridges.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/passages.h"
#include "stardraft/ramps.h"
#include "stardraft/tileset.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <cstring>
#include <cctype>
#include <climits>
#include <filesystem>
#include <map>
#include <unordered_set>
#include <set>
#include <string>

using namespace stardraft;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

static std::set<std::pair<int, int>> outline(const std::vector<Editor::TileAt>& tiles) {
    std::set<std::pair<int, int>> s;
    for (const auto& t : tiles) s.insert({t.x, t.y});
    return s;
}

static std::string sizeText(int pieces) { return pieces ? std::to_string(pieces) : "0.5"; }

// The middle of the bridge (map pixels), and a drag through it along its deck from the ground on one side to the
// ground on the other: one diamond (64, 32 px) at a time out from the middle until the corner there is the ground.
static bool dragAcross(const Editor& ed, const std::vector<Editor::TileAt>& tiles, const BridgePlan& kit, bool downRight, bool backwards,
                       int& x0, int& y0, int& x1, int& y1) {
    double cx = 0, cy = 0;
    for (const auto& t : tiles) cx += 32.0 * t.x + 16, cy += 32.0 * t.y + 16;
    cx /= double(tiles.size()), cy /= double(tiles.size());
    const int vx = 64, vy = downRight ? 32 : -32;
    const auto groundAt = [&](double x, double y) { return ed.terrainName(ed.terrainAt(int(x), int(y))) == kit.ground; };
    const auto waterAt = [&](double x, double y) { return ed.terrainName(ed.terrainAt(int(x), int(y))) == kit.water; };
    if (!waterAt(cx, cy)) return false;  // (the bridges on null tiles: no river)
    int a = 0, b = 0;
    while (a < 40 && !groundAt(cx - a * vx, cy - a * vy)) ++a;
    while (b < 40 && !groundAt(cx + b * vx, cy + b * vy)) ++b;
    if (a >= 40 || b >= 40) return false;
    x0 = int(cx - (a + 1) * vx), y0 = int(cy - (a + 1) * vy), x1 = int(cx + (b + 1) * vx), y1 = int(cy + (b + 1) * vy);
    if (backwards) std::swap(x0, x1), std::swap(y0, y1);
    return true;
}

static void examples(const GameData& data, const std::filesystem::path& dir) {
    for (const char* name : {"jungle", "platform", "desert", "ice", "twilight"}) {
        const std::filesystem::path map = dir / (std::string("examples_") + name + ".scm");
        Editor ed(data);
        std::string err;
        if (!ed.open(map.string(), err)) {
            check(false, std::string("open ") + map.string() + ": " + err);
            continue;
        }
        const int era = ed.era() & 7;
        check(bridgesAvailable(era), std::string(name) + ": has bridge kits");
        int rivers = 0;
        for (const bool u : {false, true})
            for (int length = 0; length <= 3; ++length)
                for (int width = 0; width <= 3; ++width)
                    for (int oy = -10; oy < ed.height(); ++oy)
                        for (int ox = -40; ox < ed.width(); ox += 2) {
                            if (!bridgeMatches(ed, u, length, width, ox, oy)) continue;
                            const std::vector<Editor::TileAt> want = bridgeTiles(era, u, length, width, ox, oy, 1);
                            const BridgePlan kit = planBridge(ed, 0, 0, 0, 0, width, 1);  // (the kit's terrains)
                            // The deck's diagonal: the plan's own choice for u; tried both ways along it.
                            for (const bool downRight : {true, false})
                                for (const bool backwards : {false, true}) {
                                    int x0, y0, x1, y1;
                                    if (!dragAcross(ed, want, kit, downRight, backwards, x0, y0, x1, y1)) continue;
                                    const BridgePlan p = planBridge(ed, x0, y0, x1, y1, width, 1);
                                    if (p.problem != BridgePlan::Problem::None || p.u != u) {
                                        if (std::getenv("BRIDGE_DEBUG"))
                                            std::printf("  %s %c L%d W%d at %d,%d drag %s: problem %d u %d\n", name, u ? 'u' : 'd', length, width, ox, oy,
                                                        downRight ? "down-right" : "up-right", int(p.problem), int(p.u));
                                        continue;  // (the other diagonal)
                                    }
                                    ++rivers;
                                    const std::string what = std::string(name) + (u ? " u" : " d") + " length " + sizeText(length) + " width " + sizeText(width) + " at " +
                                                             std::to_string(ox) + "," + std::to_string(oy) + (backwards ? " (dragged back)" : "");
                                    check(p.length == length, what + ": length " + sizeText(p.length));
                                    const auto got = outline(p.tiles), exp = outline(want);
                                    check(got == exp, what + ": the example's outline" +
                                                          (got == exp || got.empty() ? std::string()
                                                                                     : " (moved by " + std::to_string(got.begin()->first - exp.begin()->first) + "," +
                                                                                           std::to_string(got.begin()->second - exp.begin()->second) + ")"));
                                }
                        }
        check(rivers >= 16, std::string(name) + ": bridges over rivers found (" + std::to_string(rivers) + ")");
    }
}

// Rivers painted on a new map, one for each brush size (0.5 to 3) in a row, along the map's other diagonal: a drag across
// each gives a bridge where the river is wide enough, wider rivers longer ones; both diagonals (d and u: the first two
// rows), and width 2 (the third); a drag that ends on the water or crosses none gives none. shots: the folder to save
// the maps in (with the bridges), for pictures.
static void paintedRivers(const GameData& data, const char* shots) {
    struct Set {
        const char* name;
        uint16_t era, ground, water;
    };
    for (const Set& set : {Set{"jungle", 4, 1, 3}, Set{"platform", 1, 2, 1}, Set{"desert", 5, 1, 3}, Set{"ice", 6, 4, 5}, Set{"twilight", 7, 1, 3},
                           Set{"badlands", 0, 1, 3}}) {
        Editor ed(data);
        std::string err;
        if (!ed.newMap(128, 184, set.era, set.ground, err)) {
            check(false, std::string(set.name) + ": new map: " + err);
            continue;
        }
        for (int row = 0; row < 5; ++row) {
            const bool downRight = row != 1 && row != 3;
            const int width = row < 2 ? 1 : row == 2 ? 2 : 4;
            int last = -1, bridges = 0;
            for (int col = 0; col < 4; ++col) {
                const float size = col ? float(col) : 0.5f;
                // The river across the drag: up-right for a down-right drag, else down-right.
                const int cx = (18 + 30 * col) * 32, cy = (18 + 36 * row) * 32, rx = 64 * 6, ry = downRight ? -32 * 6 : 32 * 6;
                Brush brush;
                brush.size = size;
                ed.paintTerrainLine(cx - rx, cy - ry, cx + rx, cy + ry, set.water, brush);
                const int vx = 64 * 6, vy = downRight ? 32 * 6 : -32 * 6;
                const std::string what = std::string(set.name) + (downRight ? " down-right" : " up-right") + " width " + sizeText(width) +
                                         " over a river of brush size " + (size < 1 ? "0.5" : std::to_string(col));
                const BridgePlan p = planBridge(ed, cx - vx, cy - vy, cx + vx, cy + vy, width, 7);
                if (std::getenv("BRIDGE_SHORE") && p.problem == BridgePlan::Problem::None && col == 1 && row == 0) {  // (the shore's corners: a picture)
                    std::map<std::pair<int, int>, char> mark;
                    const char* names = "grw";
                    for (size_t i = 0; i < p.shore.size(); ++i)
                        for (const auto& c : p.shore[i]) mark[{c.x, c.y}] = names[std::min<size_t>(i, 2) + (p.shore.size() == 2 && i == 1 ? 1 : 0)];
                    std::set<std::pair<int, int>> on;
                    for (const auto& c : p.tiles) on.insert({c.x / 2, c.y});
                    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
                    for (const auto& [q, ch] : mark) x0 = std::min(x0, q.first), y0 = std::min(y0, q.second), x1 = std::max(x1, q.first), y1 = std::max(y1, q.second);
                    std::printf("%s: shore lists %zu\n", set.name, p.shore.size());
                    for (int y = y0; y <= y1; ++y) {
                        for (int x = x0; x <= x1; ++x) {
                            auto m = mark.find({x, y});
                            std::printf("%c%c", m == mark.end() ? '.' : m->second, on.count({x, y}) ? '#' : ' ');
                        }
                        std::printf("\n");
                    }
                }
                if (p.problem == BridgePlan::Problem::TooNarrow) {
                    std::printf("      %s: too narrow\n", what.c_str());
                    continue;
                }
                check(p.problem == BridgePlan::Problem::None, what + ": a bridge (problem " + std::to_string(int(p.problem)) + ")");
                if (p.problem != BridgePlan::Problem::None) continue;
                ++bridges;
                check(p.length >= last, what + ": as long as over the narrower river or longer (length " + sizeText(p.length) + ")");
                last = p.length;
                check(planBridge(ed, cx - vx, cy - vy, cx, cy, width, 7).problem == BridgePlan::Problem::ReachGround, what + ": a drag that ends on the water");
                check(planBridge(ed, cx - vx, cy - vy, cx - vx / 2, cy - vy / 2, width, 7).problem == BridgePlan::Problem::Short, what + ": a drag before the water");
                check(applyBridge(ed, p), what + ": the bridge goes on the map");
                if (const char* corpus = std::getenv("BRIDGE_BLEND")) {  // (the bridge's tiles next to tiles the maps never have them by)
                    static std::set<std::tuple<int, int, int, int>> pairs;
                    const auto variant = [](int v) { return (v >> 4) < 1024 ? v & ~15 : v; };
                    if (pairs.empty())
                        if (FILE* f = std::fopen(corpus, "r")) {
                            int e, a, b;
                            char way;
                            while (std::fscanf(f, "%d %c %d %d", &e, &way, &a, &b) == 4) pairs.insert({e, way == 'r' ? 0 : 1, variant(a), variant(b)});
                            std::fclose(f);
                        }
                    const auto& t = ed.terrain().tiles;
                    const int w = ed.width();
                    std::set<std::pair<int, int>> on;
                    for (const auto& c : p.tiles) on.insert({c.x, c.y});
                    for (const auto& c : p.tiles)
                        for (const auto& [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                            const int x = c.x + dx, y = c.y + dy;
                            if (x < 0 || y < 0 || x >= w || y >= ed.height()) continue;
                            const int a = dx + dy < 0 ? t[size_t(y) * w + x] : c.value, b = dx + dy < 0 ? c.value : t[size_t(y) * w + x];
                            if (!pairs.count({set.era, dx ? 0 : 1, variant(a), variant(b)}))
                                std::printf("BLEND %s: %s %d %d (%d.%d / %d.%d) at %d,%d %s\n", what.c_str(), dx ? "right" : "below", a, b, a >> 4, a & 15, b >> 4,
                                            b & 15, c.x, c.y, on.count({x, y}) ? "in" : "edge");
                        }
                }
            }
            check(bridges >= 2, std::string(set.name) + " row " + std::to_string(row + 1) + ": bridges over 2 rivers or more (" + std::to_string(bridges) + ")");
        }
        if (shots && !ed.save(std::filesystem::path(shots) / (std::string("bridges_") + set.name + ".scm"), err)) check(false, "save: " + err);
    }
}

// Whether a ground unit can walk from map pixel a to map pixel b: the walkable minitiles (8 x 8 pixels) joined to each
// other side by side, within the box (map pixels).
static bool walks(const Editor& ed, std::pair<int, int> a, std::pair<int, int> b, int left, int top, int right, int bottom) {
    const Tileset& ts = ed.tileset();
    const int w = ed.width() * 4, h = ed.height() * 4;
    const int x0 = std::max(0, left / 8), y0 = std::max(0, top / 8), x1 = std::min(w - 1, right / 8), y1 = std::min(h - 1, bottom / 8);
    const auto open = [&](int x, int y) {
        if (x < x0 || y < y0 || x > x1 || y > y1) return false;
        const int m = ts.megatileFor(ed.terrain().tiles[size_t(y / 4) * ed.width() + x / 4]);
        return m >= 0 && (ts.minitileFlags[size_t(m)][size_t((y % 4) * 4 + x % 4)] & Tileset::kWalkable) != 0;
    };
    std::set<std::pair<int, int>> seen;
    std::vector<std::pair<int, int>> todo;
    if (open(a.first / 8, a.second / 8)) todo.push_back({a.first / 8, a.second / 8}), seen.insert(todo.back());
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        if (x == b.first / 8 && y == b.second / 8) return true;
        for (const auto& [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
            if (open(x + dx, y + dy) && seen.insert({x + dx, y + dy}).second) todo.push_back({x + dx, y + dy});
    }
    return false;
}

// The game's own Badlands bridges (Bridge Too Near: length 2, width 1, both diagonals): a drag over the river through
// the middle of one gives a bridge in its place, of its size and outline.
static void gameBridges(const GameData& data, const std::filesystem::path& install) {
    Editor ed(data);
    std::string err;
    if (!ed.open((install / "Maps" / "(4)Bridge Too Near.scm").string(), err)) return check(false, "open Bridge Too Near: " + err);
    struct Game {
        bool u;
        int ox, oy;
    };
    for (const Game& g : {Game{false, 46, 58}, Game{true, 68, 58}}) {
        const std::vector<Editor::TileAt> want = bridgeTiles(0, g.u, 2, 1, g.ox, g.oy, 1);
        double cx = 0, cy = 0;
        for (const auto& c : want) cx += 32.0 * c.x + 16, cy += 32.0 * c.y + 16;
        cx /= double(want.size()), cy /= double(want.size());
        const int vx = 64 * 5, vy = g.u ? -32 * 5 : 32 * 5;
        const BridgePlan p = planBridge(ed, int(cx) - vx, int(cy) - vy, int(cx) + vx, int(cy) + vy, 1, 1);
        const std::string what = std::string("Bridge Too Near's ") + (g.u ? "up-right" : "down-right") + " bridge";
        check(p.problem == BridgePlan::Problem::None && p.u == g.u && p.length == 2, what + ": a bridge of length 2 (problem " + std::to_string(int(p.problem)) +
                                                                                          ", length " + sizeText(p.length) + ")");
        check(outline(p.tiles) == outline(want), what + ": in the game bridge's place");
    }
}

// A drag from far away on other land (Jungle, Sand Dunes, Grass: not the kit's ground) across the river to far away on
// the other side gives the same bridge as a drag from bank to bank, and a ground unit walks from one end to the other.
static void farDrags(const GameData& data) {
    struct Set {
        const char* name;
        uint16_t era, ground, water, land;
    };
    for (const Set& set : {Set{"jungle", 4, 1, 3, 4}, Set{"desert", 5, 1, 3, 4}, Set{"badlands", 0, 1, 3, 4}})
        for (const bool downRight : {true, false}) {
            Editor ed(data);
            std::string err;
            if (!ed.newMap(96, 96, set.era, set.ground, err)) {
                check(false, std::string(set.name) + ": new map: " + err);
                continue;
            }
            const int cx = 48 * 32, cy = 48 * 32, rx = 64 * 14, ry = downRight ? -32 * 14 : 32 * 14;
            Brush river, land;
            river.size = 2, land.size = 6;
            ed.paintTerrainLine(cx - rx, cy - ry, cx + rx, cy + ry, set.water, river);
            const int fx = 64 * 12, fy = downRight ? 32 * 12 : -32 * 12, nx = 64 * 4, ny = downRight ? 32 * 4 : -32 * 4;
            ed.paintBrush(cx - fx, cy - fy, set.land, land), ed.paintBrush(cx + fx, cy + fy, set.land, land);  // (other land round both ends)
            const std::string what = std::string(set.name) + (downRight ? " down-right" : " up-right") + ": a drag from far away on " +
                                     ed.terrainName(set.land);
            const BridgePlan nearPlan = planBridge(ed, cx - nx, cy - ny, cx + nx, cy + ny, 1, 3);
            const BridgePlan p = planBridge(ed, cx - fx, cy - fy, cx + fx, cy + fy, 1, 3);
            check(p.problem == BridgePlan::Problem::None, what + ": a bridge (problem " + std::to_string(int(p.problem)) + ")");
            check(p.length == nearPlan.length && outline(p.tiles) == outline(nearPlan.tiles), what + ": the bridge of a drag from bank to bank");
            const int box[4] = {cx - fx - 64, std::min(cy - fy, cy + fy) - 64, cx + fx + 64, std::max(cy - fy, cy + fy) + 64};
            check(applyBridge(ed, p) && walks(ed, {cx - fx, cy - fy}, {cx + fx, cy + fy}, box[0], box[1], box[2], box[3]), what + ": a way from end to end");
        }
}

// Every tileset, both diagonals, rivers of brush size 0.5 to 8 and bridges of width 0.5 to 6, each on a map of its own:
// a drag across the river gives a bridge as long as the river is wide (not shorter over a wider river), and once it is on the
// map (its shore, then its tiles) a ground unit walks over it from one end of the drag to the other, where it could
// not before.
static void allSizes(const GameData& data) {
    struct Set {
        const char* name;
        uint16_t era, ground, water;
    };
    for (const Set& set : {Set{"jungle", 4, 1, 3}, Set{"platform", 1, 2, 1}, Set{"desert", 5, 1, 3}, Set{"ice", 6, 4, 5}, Set{"twilight", 7, 1, 3},
                           Set{"badlands", 0, 1, 3}}) {
        int bridges = 0, cases = 0;
        for (const bool downRight : {true, false})
            for (const int width : {0, 1, 2, 3, 4, 6}) {
                int last = -1;
                for (const float size : {0.5f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 8.f}) {
                    Editor ed(data);
                    std::string err;
                    if (!ed.newMap(80, 80, set.era, set.ground, err)) {
                        check(false, std::string(set.name) + ": new map: " + err);
                        continue;
                    }
                    const int cx = 40 * 32, cy = 40 * 32, rx = 64 * 12, ry = downRight ? -32 * 12 : 32 * 12;
                    Brush brush;
                    brush.size = size;
                    ed.paintTerrainLine(cx - rx, cy - ry, cx + rx, cy + ry, set.water, brush);
                    const int n = 4 + int(size) / 2 + 2, vx = 64 * n, vy = downRight ? 32 * n : -32 * n;  // (the drag: past the water)
                    const std::string what = std::string(set.name) + (downRight ? " down-right" : " up-right") + " width " + sizeText(width) +
                                             " over a river of brush size " + (size < 1 ? std::string("0.5") : std::to_string(int(size)));
                    const BridgePlan p = planBridge(ed, cx - vx, cy - vy, cx + vx, cy + vy, width, 11);
                    ++cases;
                    if (p.problem == BridgePlan::Problem::TooNarrow && last < 0) continue;  // (the narrow rivers of a kit with a longer shortest bridge)
                    check(p.problem == BridgePlan::Problem::None, what + ": a bridge (problem " + std::to_string(int(p.problem)) + ")");
                    if (p.problem != BridgePlan::Problem::None) continue;
                    ++bridges;
                    check(p.length >= last, what + ": as long as over the narrower river or longer (length " + sizeText(p.length) + ")");
                    last = p.length;
                    const int box[4] = {cx - vx - 64, std::min(cy - vy, cy + vy) - 64, cx + vx + 64, std::max(cy - vy, cy + vy) + 64};
                    check(!walks(ed, {cx - vx, cy - vy}, {cx + vx, cy + vy}, box[0], box[1], box[2], box[3]), what + ": no way over the river before");
                    check(applyBridge(ed, p), what + ": the bridge goes on the map");
                    check(walks(ed, {cx - vx, cy - vy}, {cx + vx, cy + vy}, box[0], box[1], box[2], box[3]), what + ": a way over the bridge");
                }
            }
        std::printf("%s: %d bridges of %d drags\n", set.name, bridges, cases);
    }
}

// The corpus (the game's maps and the example maps): the tiles side by side (a tile and the one right of it) and one
// above the other in it, per tileset. Each pair of neighbors in a bridge (sizes 0.5 to 10, both kits, 8 seeds) must be a
// pair that the corpus has: a pair it never has is a piece that does not fit its neighbor.
static void corpusCheck(const GameData& data, const std::filesystem::path& maps, const char* examples) {
    std::map<int, std::unordered_set<uint32_t>> right, below;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::recursive_directory_iterator(maps)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".scm" || ext == ".scx") files.push_back(e.path());
    }
    if (examples)
        for (const char* name : {"jungle", "platform", "desert", "ice", "twilight", "badlands"})
            files.push_back(std::filesystem::path(examples) / (std::string("examples_") + name + ".scm"));
    int opened = 0;
    for (const auto& f : files) {
        Editor ed(data);
        std::string err;
        if (!ed.open(f.string(), err)) continue;
        ++opened;
        const int era = ed.era() & 7, w = ed.width(), h = ed.height();
        if (!bridgesAvailable(era)) continue;
        const std::vector<uint16_t>& t = ed.terrain().tiles;
        if (const char* find = std::getenv("BRIDGE_FIND"))  // a tile value: the maps and places that have it
            for (int y = 0, n = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (t[size_t(y) * w + x] == std::atoi(find) && n++ < 3) std::printf("found %s at %d,%d\n", f.string().c_str(), x, y);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const uint16_t v = t[size_t(y) * w + x];
                if (x + 1 < w) right[era].insert(uint32_t(v) << 16 | t[size_t(y) * w + x + 1]);
                if (y + 1 < h) below[era].insert(uint32_t(v) << 16 | t[size_t(y + 1) * w + x]);
            }
    }
    std::printf("corpus: %d maps\n", opened);
    if (const char* out = std::getenv("BRIDGE_NEIGHBORS")) {  // the pairs, for research/bridge-ramp/gen_bridge_kits.py
        std::FILE* f = std::fopen(out, "w");
        for (const auto& [era, set] : right)
            for (const uint32_t p : set) std::fprintf(f, "%d r %u %u\n", era, p >> 16, p & 0xFFFF);
        for (const auto& [era, set] : below)
            for (const uint32_t p : set) std::fprintf(f, "%d b %u %u\n", era, p >> 16, p & 0xFFFF);
        std::fclose(f);
        return;
    }
    for (const int era : {4, 1, 5, 6, 7, 0})
        for (const bool u : {false, true}) {
            std::map<std::string, int> unseen;  // (the pair, the way and the sizes it is at: its count)
            // (a tile of a terrain group, below 1024, stands for all of its group's tiles: random variants of one piece)
            const auto variant = [](uint32_t v) { return (v >> 4) < 1024 ? v & ~15u : v; };
            std::unordered_set<uint32_t> rightG, belowG;
            for (const uint32_t p : right[era]) rightG.insert(variant(p >> 16) << 16 | variant(p & 0xFFFF));
            for (const uint32_t p : below[era]) belowG.insert(variant(p >> 16) << 16 | variant(p & 0xFFFF));
            for (int length = era == 0 ? 1 : 0; length <= 10; ++length)  // (Badlands: no bridge of length 0.5)
                for (int width = 0; width <= 10; ++width)
                    for (uint32_t seed = 1; seed <= 8; ++seed) {
                        std::map<std::pair<int, int>, uint16_t> at;
                        for (const auto& c : bridgeTiles(era, u, length, width, 0, 0, seed)) at[{c.x, c.y}] = c.value;
                        const std::string size = " at length " + sizeText(length) + ", width " + sizeText(width);
                        for (const auto& [p, v] : at) {
                            if (auto r = at.find({p.first + 1, p.second}); r != at.end() && !rightG.count(variant(v) << 16 | variant(r->second)))
                                ++unseen[std::to_string(v) + " | " + std::to_string(r->second) + " (side by side)" + size];
                            if (auto b = at.find({p.first, p.second + 1}); b != at.end() && !belowG.count(variant(v) << 16 | variant(b->second)))
                                ++unseen[std::to_string(v) + " / " + std::to_string(b->second) + " (one above the other)" + size];
                        }
                    }
            const std::string what = "era " + std::to_string(era) + (u ? " u" : " d") + ": every pair of neighbors is in the corpus";
            check(unseen.empty(), what + (unseen.empty() ? "" : " (" + std::to_string(unseen.size()) + " pairs are not)"));
            for (const auto& [pair, n] : unseen) std::printf("      %s: %d times\n", pair.c_str(), n);
        }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: bridge_test <install dir> [<example maps folder>]\n");
        return 2;
    }
    GameData data;
    std::string err;
    if (std::getenv("MOD_CMBW") ? !cmbw::open(data, argv[1], err) : !data.open(argv[1], err)) {  // (MOD_CMBW: the mod's install)
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    check(bridgesAvailable(0) && !bridgesAvailable(2) && !bridgesAvailable(3), "bridges in Badlands; none in Installation and Ashworld");
    if (std::getenv("BRIDGE_TERRAINS")) {  // each tileset's terrain ids 1 to 8 and their names
        for (int era = 0; era < 8; ++era) {
            Editor ed(data);
            std::string e;
            if (!ed.newMap(64, 64, uint16_t(era), 1, e)) continue;
            std::printf("era %d:", era);
            for (uint16_t id = 1; id <= 8; ++id) std::printf(" %d=%s", id, ed.terrainName(id).c_str());
            std::printf("\n");
        }
        return 0;
    }
    if (std::getenv("BRIDGE_DOODADS")) {  // each tileset's doodads in its "Bridge" groups: id, size, tiles (group.index, - null)
        for (const int era : {4, 1, 5, 6, 7, 0}) {
            Editor ed(data);
            std::string e;
            if (!ed.newMap(64, 64, uint16_t(era), 1, e)) continue;
            for (const DoodadType& t : ed.doodadTypes().all()) {
                if (!t.groupString || !t.inPalette) continue;
                const std::string group = ed.unitTypes().statText(t.groupString - 1u);
                if (const char* want = std::getenv("BRIDGE_DOODADS"); group.find(std::string(want) == "1" ? "ridge" : want) == std::string::npos) continue;
                std::printf("doodad era %d id %d group %s first %d size %dx%d\n", era, t.id, group.c_str(), t.firstGroup, t.width, t.height);
                for (int r = 0; r < t.height; ++r) {
                    for (int c = 0; c < t.width; ++c) {
                        const uint16_t v = uint16_t((t.firstGroup + r) << 4 | c);
                        const int m = ed.tileset().megatileFor(v);
                        if (std::getenv("BRIDGE_MEGA")) std::printf(m <= 0 ? "     -" : " %5d", m);
                        else std::printf(m <= 0 ? "  -" : " %2d", c);
                    }
                    std::printf("\n");
                }
            }
        }
        return 0;
    }
    if (const char* dump = std::getenv("BRIDGE_DUMP")) {  // a map's tiles in a box (BRIDGE_BOX=l,t,r,b): group.index
        Editor ed(data);
        std::string e;
        if (!ed.open(dump, e)) return std::printf("%s\n", e.c_str()), 1;
        int l = 0, t = 0, r = 0, b = 0;
        std::sscanf(std::getenv("BRIDGE_BOX"), "%d,%d,%d,%d", &l, &t, &r, &b);
        for (int y = t; y <= b; ++y) {
            std::printf("%3d:", y);
            for (int x = l; x <= r; ++x) {
                const int v = ed.terrain().tiles[size_t(y) * ed.width() + x];
                std::printf(" %4d.%-2d", v >> 4, v & 15);
            }
            std::printf("\n");
        }
        return 0;
    }
    if (const char* scan = std::getenv("BRIDGE_SCAN")) {  // a map: the kits' bridges in it (u, length, width, origin)
        Editor ed(data);
        std::string e;
        if (!ed.open(scan, e)) return std::printf("%s\n", e.c_str()), 1;
        // (best fit: the most tiles equal to one of the kit's bridges' tiles, any seed; 90 % or more)
        const int era = ed.era() & 7, w = ed.width();
        const std::vector<uint16_t>& t = ed.terrain().tiles;
        for (const bool u : {false, true})
            for (int length = 0; length <= 6; ++length)
                for (int width = 0; width <= 3; ++width) {
                    std::map<std::pair<int, int>, std::set<uint16_t>> can;
                    for (uint32_t seed = 1; seed <= 24; ++seed)
                        for (const auto& c : bridgeTiles(era, u, length, width, 0, 0, seed)) can[{c.x, c.y}].insert(c.value);
                    int best = -1, bx = 0, by = 0;
                    for (int oy = -10; oy < ed.height(); ++oy)
                        for (int ox = -40; ox < w; ox += 2) {
                            int hit = 0;
                            for (const auto& [p, vs] : can) {
                                const int x = ox + p.first, y = oy + p.second;
                                hit += x >= 0 && y >= 0 && x < w && y < ed.height() && vs.count(t[size_t(y) * w + x]);
                            }
                            if (hit > best) best = hit, bx = ox, by = oy;
                        }
                    std::printf("bridge %d|%d|%d|%d|%d  %d of %zu\n", int(u), length, width, bx, by, best, can.size());
                }
        return 0;
    }
    if (const char* river = std::getenv("BRIDGE_RIVER")) {  // map|u|length|width|ox|oy: the river under that bridge
        char map[512] = {};
        int u = 0, length = 0, width = 0, ox = 0, oy = 0;
        std::sscanf(river, "%511[^|]|%d|%d|%d|%d|%d", map, &u, &length, &width, &ox, &oy);
        Editor ed(data);
        std::string e;
        if (!ed.open(map, e)) return std::printf("%s\n", e.c_str()), 1;
        const auto tiles = bridgeTiles(ed.era() & 7, u != 0, length, width, ox, oy, 1);
        double cx = 0, cy = 0;
        for (const auto& c : tiles) cx += 32.0 * c.x + 16, cy += 32.0 * c.y + 16;
        cx /= double(tiles.size()), cy /= double(tiles.size());
        const auto name = [&](int x, int y) { return ed.terrainName(ed.terrainAt(64 * x, 32 * y)); };
        // Along both diagonals through the bridge's middle diamond: each diamond's four corners (left, top, right, bottom).
        int mx = int(std::lround(cx / 64)), my = int(std::lround(cy / 32));
        if ((mx + my) & 1) ++mx;
        for (const int sy : {1, -1}) {
            std::printf("diagonal (1, %d) from diamond %d,%d (tile %d,%d); origin %d,%d\n", sy, mx, my, 2 * mx, my, ox, oy);
            for (int k = -8; k <= 8; ++k) {
                const int x = mx + k, y = my + k * sy;
                std::printf("  %+d  %d,%d: %s %s %s %s\n", k, x, y, name(x - 1, y).c_str(), name(x, y - 1).c_str(), name(x + 1, y).c_str(), name(x, y + 1).c_str());
            }
        }
        return 0;
    }
    if (const char* area = std::getenv("BRIDGE_AREA")) {  // map|left|top|right|bottom (map pixels): the corners' terrains
        char map[512] = {};
        int l = 0, tp = 0, r = 0, b = 0;
        std::sscanf(area, "%511[^|]|%d|%d|%d|%d", map, &l, &tp, &r, &b);
        Editor ed(data);
        std::string e;
        if (!ed.open(map, e)) return std::printf("%s\n", e.c_str()), 1;
        for (int y = tp / 32; y <= b / 32; ++y) {
            std::printf("y %3d  ", y);
            for (int x = l / 64; x <= r / 64; ++x) {
                const std::string n = (x + y) & 1 ? ed.terrainName(ed.terrainAt(64 * x, 32 * y)) : "";
                std::printf("%c ", n.empty() ? '.' : n[0]);
            }
            std::printf("\n");
        }
        return 0;
    }
    if (const char* isom = std::getenv("BRIDGE_ISOM")) {  // map|u|length|width|ox|oy: the corners' terrains round that bridge
        char map[512] = {};
        int u = 0, length = 0, width = 0, ox = 0, oy = 0;
        std::sscanf(isom, "%511[^|]|%d|%d|%d|%d|%d", map, &u, &length, &width, &ox, &oy);
        Editor ed(data);
        std::string e;
        if (!ed.open(map, e)) return std::printf("%s\n", e.c_str()), 1;
        std::set<std::pair<int, int>> on;
        for (const auto& c : bridgeTiles(ed.era() & 7, u != 0, length, width, ox, oy, 1)) on.insert({c.x, c.y});
        int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
        for (const auto& [x, y] : on) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        // Rect-grid points (2 tiles across, 1 down): a corner's terrain's first letter, '#' where a bridge tile is.
        for (int ry = y0 - 4; ry <= y1 + 4; ++ry) {
            for (int rx = x0 / 2 - 3; rx <= x1 / 2 + 3; ++rx) {
                const bool tile = on.count({2 * rx, ry}) || on.count({2 * rx + 1, ry});
                const std::string n = (rx + ry) & 1 ? ed.terrainName(ed.terrainAt(64 * rx, 32 * ry)) : "";
                std::printf("%c%c", n.empty() ? '.' : n[0], tile ? '#' : ' ');
            }
            std::printf("   y %d\n", ry);
        }
        return 0;
    }
    if (std::getenv("BRIDGE_FIDELITY")) {  // each tileset: the tiles of the bridges made on an open map that differ from the approved bridges of their size
        for (const int era : {0, 1, 4, 5, 6, 7}) {
            Editor ed(data);
            std::string e;
            if (!ed.newMap(128, 128, uint16_t(era), 1, e)) continue;
            int differ = 0, all = 0;
            for (const bool u : {false, true})
                for (int length = 0; length <= 3; ++length)
                    for (int width = 0; width <= 3; ++width) {
                        const std::vector<Editor::TileAt> approved = bridgeTiles(era, u, length, width, 40, 40, 7, nullptr, false);
                        std::map<std::pair<int, int>, uint16_t> at;
                        for (const Editor::TileAt& c : approved) at[{c.x, c.y}] = c.value;
                        for (const Editor::TileAt& c : bridgeTiles(era, u, length, width, 40, 40, 7, &ed, false)) {
                            ++all;
                            const auto a = at.find({c.x, c.y});
                            differ += a == at.end() || (c.value >> 4 >= 1024 ? a->second != c.value : (a->second >> 4) != (c.value >> 4));
                        }
                    }
            std::printf("era %d: %d of %d tiles differ from the approved bridges (doodad tiles exact, others by group)\n", era, differ, all);
        }
        return 0;
    }
    if (const char* era0 = std::getenv("BRIDGE_DECKWALK")) {  // <era>: wide bridges' tiles that are not all walkable, each way
        const int era = std::atoi(era0);
        Editor ed(data);
        std::string e;
        if (!ed.newMap(128, 128, uint16_t(era), 1, e)) return 1;
        const Tileset& ts = ed.tileset();
        for (const bool u : {false, true})
            for (const int width : {4, 6, 8}) {
                int part = 0, all = 0;
                for (const Editor::TileAt& c : bridgeTiles(era, u, 4, width, 40, 40, 7, &ed, false)) {
                    const int m = ts.megatileFor(c.value);
                    int n = 0;
                    for (int i = 0; m >= 0 && i < 16; ++i) n += (ts.minitileFlags[size_t(m)][size_t(i)] & Tileset::kWalkable) != 0;
                    ++all, part += n > 0 && n < 16;
                }
                std::printf("era %d %s width %d: %d of %d tiles partly walkable\n", era, u ? "up" : "down", width, part, all);
            }
        return 0;
    }
    if (const char* walk = std::getenv("BRIDGE_WALK")) {  // era:tile,tile,...: the walkable minitiles of each tile (of 16)
        Tileset ts;
        std::string e;
        const int era = std::atoi(walk);
        if (ts.load(data, uint16_t(era), e))
            for (const char* p = std::strchr(walk, ':'); p && *p; p = std::strchr(p + 1, ',')) {
                const int v = std::atoi(p + 1), m = ts.megatileFor(uint16_t(v));
                int n = 0;
                for (int i = 0; m >= 0 && i < 16; ++i) n += (ts.minitileFlags[size_t(m)][size_t(i)] & Tileset::kWalkable) != 0;
                std::printf("tile %d: %d walkable\n", v, n);
            }
        return 0;
    }
    if (const char* corpus = std::getenv("BRIDGE_CORPUS")) {  // the game's Maps folder: only the corpus check
        corpusCheck(data, corpus, argc > 2 ? argv[2] : nullptr);
        std::printf("\n%d failures\n", failures);
        return failures ? 1 : 0;
    }
    if (const char* game = std::getenv("MOD_COMPARE")) {  // (with MOD_CMBW) the kits' tiles: the same pixels in the mod's tilesets as in the game's?
        GameData vanilla;
        if (!vanilla.open(game, err)) return std::printf("%s\n", err.c_str()), 1;
        for (int era = 0; era < 8; ++era) {
            Tileset a, b;
            if (!a.load(vanilla, uint16_t(era), err) || !b.load(data, uint16_t(era), err)) continue;
            const auto pixels = [](const Tileset& ts, uint16_t v) {
                std::vector<uint8_t> out;
                const int m = ts.megatileFor(v);
                if (m < 0 || size_t(m) >= ts.megatiles.size()) return out;
                for (int py = 0; py < 32; ++py)
                    for (int px = 0; px < 32; ++px) {
                        const uint32_t mini = ts.megatiles[size_t(m)][size_t((py / 8) * 4 + px / 8)];
                        const int lx = (mini & 1) ? 7 - px % 8 : px % 8;
                        const Rgb c = (mini >> 1) < ts.minitiles.size() ? ts.palette[ts.minitiles[mini >> 1][size_t((py % 8) * 8 + lx)]] : Rgb{};
                        out.push_back(c.r), out.push_back(c.g), out.push_back(c.b);
                    }
                return out;
            };
            const auto same = [&](uint16_t v) { return pixels(a, v) == pixels(b, v); };
            if (std::getenv("MOD_GROUPS")) {  // each game doodad group (1024 and up): the mod's group with the same 16 tiles' pixels
                std::map<std::vector<uint8_t>, std::vector<uint16_t>> modBy;
                const auto groupPixels = [&](const Tileset& ts, uint16_t g) {
                    std::vector<uint8_t> all;
                    bool any = false;
                    for (int i = 0; i < 16; ++i) {
                        const std::vector<uint8_t> p = pixels(ts, uint16_t(g << 4 | i));
                        any |= ts.megatileFor(uint16_t(g << 4 | i)) > 0;
                        all.insert(all.end(), p.begin(), p.end());
                        all.push_back(0xFF);
                    }
                    return any ? all : std::vector<uint8_t>{};
                };
                for (uint16_t g = 1024; g < b.groups.size(); ++g)
                    if (std::vector<uint8_t> p = groupPixels(b, g); !p.empty()) modBy[p].push_back(g);
                int found = 0, missing = 0, sameNumber = 0;
                std::map<int, int> shifts;
                for (uint16_t g = 1024; g < a.groups.size(); ++g) {
                    const std::vector<uint8_t> p = groupPixels(a, g);
                    if (p.empty()) continue;
                    const auto i = modBy.find(p);
                    if (i == modBy.end()) {
                        ++missing;
                        continue;
                    }
                    ++found;
                    sameNumber += std::find(i->second.begin(), i->second.end(), g) != i->second.end();
                    ++shifts[int(i->second.front()) - g];
                    if (const char* out = std::getenv("MOD_GROUPS_OUT"); out && *out)
                        if (FILE* f = std::fopen(out, "a")) std::fprintf(f, "%d %d %d\n", era, g, i->second.front()), std::fclose(f);
                }
                std::printf("  doodad groups: %d found in the mod (%d at the same number), %d missing; shifts:", found, sameNumber, missing);
                for (const auto& [s, n] : shifts) std::printf(" %+d x%d", s, n);
                std::printf("\n");
            }
            std::printf("era %d: groups %zu game, %zu mod\n", era, a.groups.size(), b.groups.size());
            for (const bool u : {false, true}) {
                std::set<uint16_t> vs;
                for (int length = 0; length <= 4; ++length)
                    for (int width = 0; width <= 3; ++width)
                        for (uint32_t seed = 1; seed <= 8; ++seed)
                            for (const auto& c : bridgeTiles(era, u, length, width, 0, 0, seed)) vs.insert(c.value);
                if (vs.empty()) continue;
                int bad = 0;
                for (uint16_t v : vs) bad += !same(v);
                if (std::getenv("MOD_WHICH"))  // the tiles that differ: a mod tile with the same pixels (where)
                    for (uint16_t v : vs)
                        if (!same(v)) {
                            const std::vector<uint8_t> want = pixels(a, v);
                            int at = -1;
                            long near = LONG_MAX;
                            for (uint16_t g = 0; g < b.groups.size() && at < 0; ++g)
                                for (int i = 0; i < 16 && at < 0; ++i) {
                                    const std::vector<uint8_t> p = pixels(b, uint16_t(g << 4 | i));
                                    if (p == want) at = g << 4 | i;
                                }
                            const std::vector<uint8_t> now = pixels(b, v);
                            if (now.size() == want.size()) {
                                near = 0;
                                for (size_t j = 0; j < now.size(); ++j) near += std::abs(int(now[j]) - int(want[j]));
                            }
                            std::printf("    %d.%d: same pixels in the mod at %d.%d; the mod's own tile there differs by %ld\n", v >> 4, v & 15, at < 0 ? -1 : at >> 4,
                                        at < 0 ? -1 : at & 15, near);
                        }
                std::printf("  bridge %s: %zu tiles, %d differ\n", u ? "u" : "d", vs.size(), bad);
            }
            if (std::getenv("MOD_NEAREST"))  // the first tile of each ramp block's wall A: the mod tile nearest in pixels
                for (const int k : rampKitsOf(era, true)) {
                    if (rampKitFacing(k) != 0) continue;
                    const uint16_t v = rampKitTiles(k, 0, 0, 0).front().value;
                    const std::vector<uint8_t> want = pixels(a, v);
                    long best = LONG_MAX;
                    uint16_t at = 0;
                    for (uint16_t g = 0; g < b.groups.size(); ++g)
                        for (int i = 0; i < 16; ++i) {
                            const std::vector<uint8_t> p = pixels(b, uint16_t(g << 4 | i));
                            if (p.size() != want.size()) continue;
                            long d = 0;
                            for (size_t j = 0; j < p.size() && d < best; ++j) d += std::abs(int(p[j]) - int(want[j]));
                            if (d < best) best = d, at = uint16_t(g << 4 | i);
                        }
                    std::printf("  ramp %s: game tile %d.%d, nearest mod tile %d.%d, difference %ld\n", rampKitGroup(k), v >> 4, v & 15, at >> 4, at & 15, best);
                }
            for (const int k : rampKitsOf(era, true)) {
                int bad = 0, n = 0;
                for (const auto& c : rampKitTiles(k, 1, 0, 0)) bad += !same(c.value), ++n;
                if (bad) std::printf("  ramp kit %d (%s %s): %d of %d tiles differ\n", k, rampKitGroup(k), rampFacingName(rampKitFacing(k)), bad, n);
            }
        }
        return 0;
    }
    if (std::getenv("BRIDGE_BENCH")) {  // the time of one drag's preview (plan, a copy of the map, the bridge on it), per tileset
        for (const int era : {4, 1, 5, 6, 7, 0}) {
            Editor ed(data);
            std::string err;
            uint16_t ground = era == 1 ? 2 : era == 6 ? 4 : 1, water = era == 1 ? 1 : era == 6 ? 5 : 3;
            if (!ed.newMap(256, 256, uint16_t(era), ground, err)) continue;
            Brush brush;
            brush.size = 3;
            const int cx = 128 * 32, cy = 128 * 32;
            ed.paintTerrainLine(cx - 64 * 20, cy - 32 * 20, cx + 64 * 20, cy + 32 * 20, water, brush);
            for (const int width : {1, 3}) {
                double plan = 0, open = 0, apply = 0;
                const int n = 20;
                Editor copy(data);  // (as the editor keeps one: shapeCopy)
                const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
                for (int i = 0; i < n; ++i) {
                    const auto t0 = std::chrono::steady_clock::now();
                    const BridgePlan p = planBridge(ed, cx - 64 * 8 + i * 8, cy + 32 * 8, cx + 64 * 8, cy - 32 * 8, width, 7);
                    const auto t1 = std::chrono::steady_clock::now();
                    copy.openChk(ed.chk(), err);
                    const auto t2 = std::chrono::steady_clock::now();
                    applyBridge(copy, p);
                    const auto t3 = std::chrono::steady_clock::now();
                    plan += ms(t0, t1), open += ms(t1, t2), apply += ms(t2, t3);
                }
                std::printf("bench era %d width %d: plan %.1f ms, copy %.1f ms, apply %.1f ms\n", era, width, plan / n, open / n, apply / n);
            }
            // the seams of every size (lengths 1 to 6, widths 0.5 to 3, both ways), alone (no map round them)
            double seams = 0, ms = 0;
            int bridges = 0;
            for (const bool u : {false, true})
                for (int length = 1; length <= 6; ++length)
                    for (int width = 0; width <= 3; ++width) {
                        const auto t0 = std::chrono::steady_clock::now();
                        const std::vector<Editor::TileAt> t = bridgeTiles(era, u, length, width, 40, 40, 7, &ed);
                        ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                        if (t.empty()) continue;
                        seams += bridgeSeams(ed, t, false), ++bridges;
                    }
            std::printf("seams era %d: %.0f over %d bridges (%.0f each), %.2f ms each\n", era, seams, bridges, seams / std::max(1, bridges), ms / std::max(1, bridges));
        }
        return 0;
    }
    if (const char* spaced = std::getenv("BRIDGE_SPACED")) {  // <era>,<file>: up-right bridges (south-west to north-east), far apart
        int era = 0;
        char file[512] = {};
        std::sscanf(spaced, "%d,%511s", &era, file);
        Editor ed(data);
        std::string err;
        uint16_t ground = std::getenv("BRIDGE_SPACED_GROUND") ? uint16_t(std::atoi(std::getenv("BRIDGE_SPACED_GROUND"))) : 1,
                 water = std::getenv("BRIDGE_SPACED_WATER") ? uint16_t(std::atoi(std::getenv("BRIDGE_SPACED_WATER"))) : 3;  // (the land round the rivers)
        if (!ed.newMap(256, 256, uint16_t(era), ground, err)) return std::printf("%s\n", err.c_str()), 1;
        for (int row = 0; row < 4; ++row)      // the bridge's width: 0.5, 1, 2, 3
            for (int col = 0; col < 4; ++col) {  // the river's brush size: 0.5, 1, 2, 3
                Brush brush;
                brush.size = col ? float(col) : 0.5f;
                const int cx = (32 + 60 * col) * 32, cy = (30 + 64 * row) * 32, rx = 64 * 7, ry = 32 * 7;
                ed.paintTerrainLine(cx - rx, cy - ry, cx + rx, cy + ry, water, brush);  // (the river: down-right)
                const int vx = 64 * 8, vy = -32 * 8;  // (the drag: from the south-west to the north-east)
                const BridgePlan p = planBridge(ed, cx - vx, cy - vy, cx + vx, cy + vy, row, 7);
                std::printf("width %s, river %s: %s\n", sizeText(row).c_str(), col ? std::to_string(col).c_str() : "0.5",
                            p.problem == BridgePlan::Problem::None ? ("length " + sizeText(p.length)).c_str() : ("problem " + std::to_string(int(p.problem))).c_str());
                if (p.problem == BridgePlan::Problem::None) applyBridge(ed, p);
            }
        if (!ed.save(file, err)) return std::printf("%s\n", err.c_str()), 1;
        return 0;
    }
    if (std::getenv("BRIDGE_PASSAGES")) {  // each tileset: a passage bridge (the Jungle kit, its tiles as they are) over each water-like terrain
        int made = 0, tried = 0;
        bool savedOne = false;
        for (int era = 0; era < 8; ++era) {
            Editor probe(data);
            std::string err;
            if (!probe.newMap(64, 64, uint16_t(era), 1, err)) continue;
            for (uint16_t w = 2; w < 256; ++w) {  // (a mod's terrains too: 64 and up)
                const std::string name = probe.terrainName(w);
                if (name.empty() || (name.find("Water") == std::string::npos && name.find("Lava") == std::string::npos && name.find("Magma") == std::string::npos &&
                                     name.find("Tar") == std::string::npos && name.find("Mud") == std::string::npos))
                    continue;
                stardraft::passages::Passages ps;
                stardraft::passages::BridgeMapping m;
                m.name = "probe", m.era = era, m.templateEra = bridgesAvailable(era) ? era : 4;
                for (const uint16_t v : bridgeKitTileValues(m.templateEra)) m.tiles[v] = v;
                m.terrains.push_back({probe.terrainName(1), name, probe.terrainName(1)});
                ps.bridges.push_back(m);
                stardraft::passages::use(ps);
                Editor ed(data);
                ed.newMap(128, 128, uint16_t(era), 1, err);
                Brush brush;
                brush.size = 2;
                const int cx = 64 * 32, cy = 64 * 32;
                ed.paintTerrainLine(cx - 64 * 7, cy - 32 * 7, cx + 64 * 7, cy + 32 * 7, w, brush);
                // (BRIDGE_PASSAGES_OUT=<folder>: the first river of a terrain no game kit has, before its bridge, and its passages file:
                // for a drag of the program's Bridge tool)
                if (const char* out = std::getenv("BRIDGE_PASSAGES_OUT"); out && *out && w >= 4 && !savedOne) {
                    std::string e2;
                    savedOne = ed.save(std::filesystem::path(out) / "river.scx", e2) && stardraft::passages::save(std::filesystem::path(out) / "river.scpass", ps, e2);
                    std::printf("saved river of %s: %s\n", name.c_str(), savedOne ? "ok" : e2.c_str());
                }
                const BridgePlan p = planBridge(ed, cx - 64 * 8, cy + 32 * 8, cx + 64 * 8, cy - 32 * 8, 1, 7);
                ++tried;
                const bool ok = p.problem == BridgePlan::Problem::None && p.kit >= 0 && applyBridge(ed, p);
                made += ok;
                std::printf("era %d water %-22s: problem %d kit %d tiles %zu%s\n", era, name.c_str(), int(p.problem), p.kit, p.tiles.size(), ok ? " (passage bridge placed)" : "");
            }
        }
        stardraft::passages::use({});
        std::printf("%d of %d passage bridges made\n", made, tried);
        return 0;
    }
    paintedRivers(data, std::getenv("BRIDGE_SHOTS"));
    allSizes(data);
    farDrags(data);
    gameBridges(data, argv[1]);
    if (argc > 2) examples(data, argv[2]);
    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
