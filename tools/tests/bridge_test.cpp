// The Bridge tool's checks (src/bridges.cpp): with the example maps of the bridge research (the approved bridges over
// their rivers), each drag across a river must give the bridge of the example: the same orientation, length and
// outline. Without the example maps: a bridge over a river that the test paints.
//   bridge_test <install dir> [<example maps folder: examples_<tileset>.scm>]
#include "stardraft/bridges.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/tileset.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    check(bridgesAvailable(0) && !bridgesAvailable(2) && !bridgesAvailable(3), "bridges in Badlands; none in Installation and Ashworld");
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
    paintedRivers(data, std::getenv("BRIDGE_SHOTS"));
    allSizes(data);
    gameBridges(data, argv[1]);
    if (argc > 2) examples(data, argv[2]);
    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
