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
    for (const Set& set : {Set{"jungle", 4, 1, 3}, Set{"platform", 1, 2, 1}, Set{"desert", 5, 1, 3}, Set{"ice", 6, 4, 5}, Set{"twilight", 7, 1, 3}}) {
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
                check(ed.placeTileValues(p.tiles) > 0, what + ": the tiles go on the map");
            }
            check(bridges >= 2, std::string(set.name) + " row " + std::to_string(row + 1) + ": bridges over 2 rivers or more (" + std::to_string(bridges) + ")");
        }
        if (shots && !ed.save(std::filesystem::path(shots) / (std::string("bridges_") + set.name + ".scm"), err)) check(false, "save: " + err);
    }
}

// The corpus (the game's maps and the example maps): the tiles side by side (a tile and the one right of it) and one
// above the other in it, per tileset. Each pair of neighbors in a bridge (sizes 0.5 to 6, both kits, 8 seeds) must be a
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
        for (const char* name : {"jungle", "platform", "desert", "ice", "twilight"})
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
    for (const int era : {4, 1, 5, 6, 7})
        for (const bool u : {false, true}) {
            std::map<std::string, int> unseen;  // (the pair, the way and the sizes it is at: its count)
            for (int length = 0; length <= 6; ++length)
                for (int width = 0; width <= 6; ++width)
                    for (uint32_t seed = 1; seed <= 8; ++seed) {
                        std::map<std::pair<int, int>, uint16_t> at;
                        for (const auto& c : bridgeTiles(era, u, length, width, 0, 0, seed)) at[{c.x, c.y}] = c.value;
                        const std::string size = " at length " + sizeText(length) + ", width " + sizeText(width);
                        for (const auto& [p, v] : at) {
                            if (auto r = at.find({p.first + 1, p.second}); r != at.end() && !right[era].count(uint32_t(v) << 16 | r->second))
                                ++unseen[std::to_string(v) + " | " + std::to_string(r->second) + " (side by side)" + size];
                            if (auto b = at.find({p.first, p.second + 1}); b != at.end() && !below[era].count(uint32_t(v) << 16 | b->second))
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
    check(!bridgesAvailable(0) && !bridgesAvailable(2) && !bridgesAvailable(3), "no bridges in Badlands, Installation and Ashworld");
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
    if (argc > 2) examples(data, argv[2]);
    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
