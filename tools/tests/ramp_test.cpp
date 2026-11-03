// The Ramp brush's checks (src/ramps.cpp).
//   ramp_test <install dir> [<example maps folder: examples_<tileset>.scm>]
// RAMP_LEARN=<file>: the ISOM corners under each piece of the example maps' ramps (on their plateaus), for
// research/bridge-ramp/gen_ramp_kits.py.
#include "stardraft/doodads.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/bridges.h"
#include "stardraft/passages.h"
#include "stardraft/ramps.h"
#include "stardraft/tileset.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <tuple>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

using namespace stardraft;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

// The example ramps: each ramp of the map (the most slopes that match at its wall A), (facing, slopes, ax, ay).
struct FoundRamp {
    int facing, slopes, ax, ay;
};
static std::vector<FoundRamp> findRamps(const Editor& ed) {
    std::vector<FoundRamp> out;
    for (int f = 0; f < kRampFacings; ++f)
        for (int ay = -6; ay < ed.height(); ++ay)
            for (int ax = -8; ax < ed.width(); ++ax) {
                int best = -1;
                for (int n = 0; n <= 8; ++n)
                    if (rampMatches(ed, f, n, ax, ay)) best = n;
                if (best >= 0) out.push_back({f, best, ax, ay});
            }
    return out;
}

// The corners (rect-grid points, x + y odd) inside a piece's tiles, with their pixel place from its top left.
static void learn(const GameData& data, const std::filesystem::path& dir, const char* out) {
    std::FILE* f = std::fopen(out, "w");
    for (const char* name : {"jungle", "badlands", "ashworld", "ice", "platform"}) {
        Editor ed(data);
        std::string err;
        if (!ed.open((dir / (std::string("examples_") + name + ".scm")).string(), err)) continue;
        const int era = ed.era() & 7;
        const std::vector<FoundRamp> ramps = findRamps(ed);
        std::printf("%s: %zu ramps\n", name, ramps.size());
        for (const FoundRamp& r : ramps) {
            // (only ramps on ground: the corners round them have terrain)
            bool onGround = true;
            for (const RampPiece& p : rampPieces(era, r.facing, r.slopes, r.ax, r.ay)) {
                const auto [w, h] = rampPieceSize(era, r.facing, p.kind);
                for (int py = p.y * 32; py <= (p.y + h) * 32 && onGround; py += 32)
                    for (int px = p.x * 32; px <= (p.x + w) * 32 && onGround; px += 32)
                        if (ed.terrainName(ed.terrainAt(px, py)).empty()) onGround = false;
            }
            std::fprintf(f, "ramp %d %s %d %d %d %d\n", era, rampFacingName(r.facing), r.slopes, r.ax, r.ay, int(onGround));
            if (!onGround) continue;
            for (const RampPiece& p : rampPieces(era, r.facing, r.slopes, r.ax, r.ay)) {
                const auto [w, h] = rampPieceSize(era, r.facing, p.kind);
                // corners whose pixel is in the piece's tiles (edges included), from the piece's top left
                for (int cy = (p.y * 32) / 32; cy <= p.y + h; ++cy)
                    for (int cx = (p.x * 32) / 64; cx <= (p.x + w) * 32 / 64; ++cx) {
                        if (!((cx + cy) & 1)) continue;
                        const int px = 64 * cx - 32 * p.x, py = 32 * cy - 32 * p.y;
                        if (px < 0 || py < 0 || px > w * 32 || py > h * 32) continue;
                        std::fprintf(f, "corner %d %s %d %d %d %s\n", era, rampFacingName(r.facing), p.kind, px, py,
                                     ed.terrainName(ed.terrainAt(64 * cx, 32 * cy)).c_str());
                    }
            }
        }
    }
    std::fclose(f);
}

// Whether a ground unit can walk from map pixel a to map pixel b (the walkable minitiles joined side by side).
static bool walks(const Editor& ed, std::pair<int, int> a, std::pair<int, int> b) {
    const Tileset& ts = ed.tileset();
    const int w = ed.width() * 4, h = ed.height() * 4;
    const auto open = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        const int m = ts.megatileFor(ed.terrain().tiles[size_t(y / 4) * ed.width() + x / 4]);
        return m >= 0 && (ts.minitileFlags[size_t(m)][size_t((y % 4) * 4 + x % 4)] & Tileset::kWalkable) != 0;
    };
    std::vector<uint8_t> seen(size_t(w) * h, 0);
    std::vector<std::pair<int, int>> todo;
    if (open(a.first / 8, a.second / 8)) todo.push_back({a.first / 8, a.second / 8}), seen[size_t(a.second / 8) * w + a.first / 8] = 1;
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        if (x == b.first / 8 && y == b.second / 8) return true;
        for (const auto& [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
            if (open(x + dx, y + dy) && !seen[size_t(y + dy) * w + x + dx]) seen[size_t(y + dy) * w + x + dx] = 1, todo.push_back({x + dx, y + dy});
    }
    return false;
}

static std::pair<int, int> middleOf(const std::vector<Editor::TileAt>& tiles) {
    double x = 0, y = 0;
    for (const auto& c : tiles) x += 32.0 * c.x + 16, y += 32.0 * c.y + 16;
    return {int(x / double(tiles.size())), int(y / double(tiles.size()))};
}

// A stroke along a ramp: from its wall A's middle to its wall B's middle (or back).
static std::vector<std::pair<int, int>> strokeAlong(int era, int facing, int slopes, int ax, int ay, bool back) {
    std::vector<std::pair<int, int>> pts;
    for (const RampPiece& p : rampPieces(era, facing, slopes, ax, ay)) {
        const auto [w, h] = rampPieceSize(era, facing, p.kind);
        pts.push_back({p.x * 32 + w * 16, p.y * 32 + h * 16});
    }
    if (back) std::reverse(pts.begin(), pts.end());
    return pts;
}

// The example maps' ramps on their plateaus: each fits, and a stroke along it (both ways) gives that ramp.
static void examples(const GameData& data, const std::filesystem::path& dir) {
    for (const char* name : {"jungle", "badlands", "ashworld", "ice", "platform"}) {
        Editor ed(data);
        std::string err;
        if (!ed.open((dir / (std::string("examples_") + name + ".scm")).string(), err)) {
            check(false, std::string("open ") + name + ": " + err);
            continue;
        }
        const int era = ed.era() & 7;
        int tested = 0, walls = 0;
        for (const FoundRamp& r : findRamps(ed)) {
            std::set<std::string> under;  // (on a plateau: two terrains under it)
            for (const auto& c : rampTiles(era, r.facing, r.slopes, r.ax, r.ay))
                for (int dy = 0; dy <= 1; ++dy)
                    for (int dx = 0; dx <= 1; ++dx) under.insert(ed.terrainName(ed.terrainAt((c.x + dx) * 32, (c.y + dy) * 32)));
            under.erase("");
            bool high = false;  // (and the high ground among them: the example plateaus are of High Dirt, High Snow, Elevated Catwalk)
            for (const std::string& n : under) high |= n.rfind("High", 0) == 0 || n == "Elevated Catwalk";
            if (under.size() < 2 || !high) continue;
            if (!rampJoins(ed.tileset(), rampTiles(era, r.facing, r.slopes, r.ax, r.ay))) {  // (the two walls only, where they join nothing)
                ++walls;
                continue;
            }
            ++tested;
            const std::string what = std::string(name) + " " + rampFacingName(r.facing) + " ramp of " + std::to_string(r.slopes) + " slopes at " +
                                     std::to_string(r.ax) + "," + std::to_string(r.ay);
            check(rampFits(ed, r.facing, r.slopes, r.ax, r.ay), what + ": fits");
            for (const bool back : {false, true}) {
                const RampPlan p = planRamp(ed, strokeAlong(era, r.facing, r.slopes, r.ax, r.ay, back));
                check(p.problem == RampPlan::Problem::None && p.facing == r.facing && p.slopes == r.slopes && p.ax == r.ax && p.ay == r.ay,
                      what + (back ? ": a stroke back along it" : ": a stroke along it") + " (got " + rampFacingName(p.facing) + " " +
                          std::to_string(p.slopes) + " at " + std::to_string(p.ax) + "," + std::to_string(p.ay) + ")");
            }
        }
        check(tested >= 28, std::string(name) + ": example ramps on plateaus (" + std::to_string(tested) + "; " + std::to_string(walls) +
                                " of walls only that join no ground)");
    }
}

// Plateaus painted on a new map (a square and a diamond of the high ground): on each edge, ramps of 0 to 3 slopes; a
// unit walks from the low ground onto the plateau over each, and could not before.
static void paintedPlateaus(const GameData& data, const char* shots) {
    struct Set {
        const char* name;
        uint16_t era, low;  // (low: the new map's terrain)
        const char* high;
    };
    for (const Set& set : {Set{"jungle", 4, 1, "High Dirt"}, Set{"badlands", 0, 1, "High Dirt"}, Set{"ashworld", 3, 2, "High Dirt"},
                           Set{"ice", 6, 1, "High Snow"}, Set{"platform", 1, 2, "Elevated Catwalk"}}) {
        for (const Brush::Shape shape : {Brush::Square, Brush::Diamond}) {
            for (int slopes = 0; slopes <= 3; ++slopes) {
                const std::pair<int, int> top{48 * 32, 48 * 32};
                int done = 0;
                std::set<int> facings;
                Editor all(data);  // (the picture: every edge's ramp)
                std::string err;
                for (int f = -1; f < kRampFacings; ++f) {
                    // (a map for each facing: the walk over its ramp alone)
                    const std::unique_ptr<Editor> own = f < 0 ? nullptr : std::make_unique<Editor>(data);
                    Editor& ed = f < 0 ? all : *own;
                    uint16_t high = 0;
                    if (!ed.newMap(96, 96, set.era, set.low, err)) {
                        check(false, "new map: " + err);
                        continue;
                    }
                    for (uint16_t id = 1; id < 64; ++id)
                        if (ed.terrainName(id) == set.high) high = id;
                    Brush b;
                    b.shape = shape, b.size = shape == Brush::Square ? 14 : 16;
                    ed.paintBrush(48 * 32, 48 * 32, high, b);
                    if (f < 0) continue;
                    // the first place on the plateau's edge that this facing's ramp fits, nearest the middle of the edge
                    int bx = 0, by = 0;
                    double bd = 1e18;
                    for (int ay = 0; ay < ed.height(); ++ay)
                        for (int ax = 0; ax < ed.width(); ++ax)
                            if (rampFits(ed, f, slopes, ax, ay)) {
                                const auto m = middleOf(rampTiles(set.era, f, slopes, ax, ay));
                                if (const double d = std::hypot(m.first - top.first, m.second - top.second); d < bd) bd = d, bx = ax, by = ay;
                            }
                    if (bd > 1e17) continue;
                    const std::string what = std::string(set.name) + (shape == Brush::Square ? " square" : " diamond") + " plateau " +
                                             rampFacingName(f) + " ramp of " + std::to_string(slopes) + " slopes";
                    const RampPlan p = planRamp(ed, strokeAlong(set.era, f, slopes, bx, by, false));
                    check(p.problem == RampPlan::Problem::None && p.facing == f && p.slopes == slopes, what + ": the stroke's ramp (got " +
                                                                                                            rampFacingName(p.facing) + " " + std::to_string(p.slopes) + " of " + rampKitGroup(p.kit) + ")");
                    // low ground: 3 tiles out from the ramp, the way it faces (away from the plateau's middle)
                    const auto m = middleOf(p.tiles);
                    const double dx = m.first - top.first, dy = m.second - top.second, dl = std::hypot(dx, dy);
                    const std::pair<int, int> out{int(m.first + dx / dl * 160), int(m.second + dy / dl * 160)};
                    const bool before = walks(ed, out, top);
                    if (ed.placeTileValues(p.tiles) <= 0) check(false, what + ": placed");
                    check(!before && walks(ed, out, top), what + ": a way up onto the plateau over it" + (before ? " (there was one before)" : ""));
                    all.placeTileValues(p.tiles);
                    ++done, facings.insert(f);
                }
                check(done >= (slopes ? 4 : 2), std::string(set.name) + (shape == Brush::Square ? " square" : " diamond") + " plateau, " + std::to_string(slopes) +
                                     " slopes: ramps on " + std::to_string(done) + " edges");
                if (shots && slopes == 2)
                    all.save(std::filesystem::path(shots) / (std::string("ramps_") + set.name + (shape == Brush::Square ? "_square" : "_diamond") + ".scm"), err);
            }
        }
    }
}

// The corpus (the game's maps and the example maps): every pair of tiles side by side and one above the other where one
// of the two is a ramp piece's tile (a terrain group's tile, below 1024, as all of its group's tiles), per tileset.
static void dumpNeighbors(const GameData& data, const std::filesystem::path& maps, const char* examples, const char* out) {
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::recursive_directory_iterator(maps)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".scm" || ext == ".scx") files.push_back(e.path());
    }
    for (const char* name : {"jungle", "badlands", "ashworld", "ice", "platform"})
        files.push_back(std::filesystem::path(examples) / (std::string("examples_") + name + ".scm"));
    const auto variant = [](uint32_t v) { return (v >> 4) < 1024 ? v & ~15u : v; };
    std::map<int, std::set<uint32_t>> kit;  // era: the ramp pieces' tiles
    for (int era = 0; era < 8; ++era)
        for (const int k : rampKitsOf(era, true))
            for (const auto& c : rampKitTiles(k, 1, 0, 0)) kit[era].insert(variant(c.value));
    std::set<std::tuple<int, int, uint32_t, uint32_t>> pairs;
    for (const auto& file : files) {
        Editor ed(data);
        std::string err;
        if (!ed.open(file.string(), err)) continue;
        const int era = ed.era() & 7, w = ed.width(), h = ed.height();
        if (!kit.count(era)) continue;
        const std::vector<uint16_t>& t = ed.terrain().tiles;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const uint32_t a = variant(t[size_t(y) * w + x]);
                for (const auto& [way, nx, ny] : {std::tuple{0, x + 1, y}, {1, x, y + 1}}) {
                    if (nx >= w || ny >= h) continue;
                    const uint32_t b = variant(t[size_t(ny) * w + nx]);
                    if (kit[era].count(a) || kit[era].count(b)) pairs.insert({era, way, a, b});
                }
            }
    }
    std::FILE* f = std::fopen(out, "w");
    for (const auto& [era, way, a, b] : pairs) std::fprintf(f, "%d %d %u %u\n", era, way, a, b);
    std::fclose(f);
    std::printf("%zu pairs\n", pairs.size());
}

// A long stroke on a large map is planned in a frame or so; a stroke over half of a ramp already there gives that ramp
// again (not one that ends inside it), and a stroke from it on along the edge a wider ramp over it.
static void longAndOver(const GameData& data) {
    Editor ed(data);
    std::string err;
    if (!ed.newMap(256, 256, 4, 1, err)) return check(false, "new map: " + err);
    Brush b;
    b.shape = Brush::Square, b.size = 40;
    ed.paintBrush(128 * 32, 128 * 32, 2, b);  // (High Dirt)
    // the N edge's ramps: the first that fits, and a stroke 70 tiles along that edge
    int bx = -1, by = 0;
    for (int ay = 0; ay < ed.height() && bx < 0; ++ay)
        for (int ax = 0; ax < ed.width() && bx < 0; ++ax)
            if (rampFits(ed, 0, 0, ax, ay)) bx = ax, by = ay;
    if (bx < 0) return check(false, "a big plateau: an N ramp fits");
    const std::vector<std::pair<int, int>> stroke = strokeAlong(4, 0, 0, bx, by, false);
    std::vector<std::pair<int, int>> longStroke{stroke.front(), {stroke.front().first + 70 * 32, stroke.front().second}};
    const auto t0 = std::chrono::steady_clock::now();
    const RampPlan p = planRamp(ed, longStroke);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(p.problem == RampPlan::Problem::None && p.slopes >= 15, "a 70-tile stroke along a big plateau's N edge: a ramp of " + std::to_string(p.slopes) + " slopes");
    check(ms < 50, "it takes less than 50 ms (" + std::to_string(int(ms)) + " ms)");
    // a ramp of 4 slopes there, then a stroke over its first half
    const RampPlan four = planRamp(ed, strokeAlong(4, 0, 4, bx, by, false));
    ed.placeTileValues(four.tiles);
    std::vector<std::pair<int, int>> half = strokeAlong(4, 0, 4, bx, by, false);
    half.resize(3);
    const RampPlan over = planRamp(ed, half);
    std::set<std::pair<int, int>> old, now;
    for (const auto& c : four.tiles) old.insert({c.x, c.y});
    for (const auto& c : over.tiles) now.insert({c.x, c.y});
    bool shared = false;
    for (const auto& q : now) shared |= old.count(q) != 0;
    check(over.problem == RampPlan::Problem::None && now == old,
          "a stroke over half of a ramp: that ramp again (got " + std::to_string(over.slopes) + " slopes at " + std::to_string(over.ax) + "," +
              std::to_string(over.ay) + "; the ramp: " + std::to_string(four.slopes) + " at " + std::to_string(four.ax) + "," + std::to_string(four.ay) + ")");
    (void)shared;
    // from the ramp's wall A on past its wall B by two more slopes
    std::vector<std::pair<int, int>> wider = strokeAlong(4, 0, 6, bx, by, false);
    const RampPlan grown = planRamp(ed, wider);
    check(grown.problem == RampPlan::Problem::None && grown.ax == four.ax && grown.ay == four.ay && grown.slopes == 6,
          "a stroke along a ramp and on: a wider ramp over it (got " + std::to_string(grown.slopes) + " slopes at " + std::to_string(grown.ax) + "," +
              std::to_string(grown.ay) + ")");
}

// One stroke along two edges of a plateau (round its corner): a ramp on each, facing its edge's way.
static void roundCorner(const GameData& data) {
    Editor ed(data);
    std::string err;
    if (!ed.newMap(96, 96, 4, 1, err)) return check(false, "new map: " + err);
    Brush b;
    b.shape = Brush::Square, b.size = 14;
    ed.paintBrush(48 * 32, 48 * 32, 2, b);  // (High Dirt)
    // a ramp of one slope that fits on the N edge and one on the E edge, nearest the NE corner
    const auto nearest = [&](int f) {
        std::pair<int, int> best{-1, 0};
        double bd = 1e18;
        for (int ay = 0; ay < ed.height(); ++ay)
            for (int ax = 0; ax < ed.width(); ++ax)
                if (rampFits(ed, f, 1, ax, ay))
                    if (const double d = std::hypot(ax - 70.0, ay - 20.0); d < bd) bd = d, best = {ax, ay};
        return best;
    };
    const auto n = nearest(0), e = nearest(2);
    if (n.first < 0 || e.first < 0) return check(false, "a square plateau: N and E ramps fit");
    std::vector<std::pair<int, int>> stroke = strokeAlong(4, 0, 1, n.first, n.second, false);
    for (const auto& p : strokeAlong(4, 2, 1, e.first, e.second, false)) stroke.push_back(p);
    const std::vector<RampPlan> plans = planRamps(ed, stroke);
    std::set<int> facings;
    for (const RampPlan& p : plans) facings.insert(p.facing);
    check(plans.size() == 2 && facings.count(0) && facings.count(2), "a stroke along the N edge and on down the E edge: an N ramp and an E ramp (got " +
                                                                         std::to_string(plans.size()) + ")");
}

// RAMP_WIDE=<folder>: big plateaus (a square and a diamond) with each edge painted in one stroke from end to end (the
// widest ramps), saved for pictures.
static void wideRamps(const GameData& data, const std::filesystem::path& out) {
    for (const Brush::Shape shape : {Brush::Square, Brush::Diamond}) {
        Editor ed(data);
        std::string err;
        if (!ed.newMap(128, 128, 4, 1, err)) return;
        Brush b;
        b.shape = shape, b.size = shape == Brush::Square ? 30 : 34;
        ed.paintBrush(64 * 32, 64 * 32, 2, b);  // (High Dirt)
        int ramps = 0, slopes = 0;
        for (int f = 0; f < kRampFacings; ++f) {
            // the anchors of no slope that fit this facing: the stroke from the first to the last along the slopes' step
            std::vector<std::pair<int, int>> fits;
            for (int ay = 0; ay < ed.height(); ++ay)
                for (int ax = 0; ax < ed.width(); ++ax)
                    if (rampFits(ed, f, 0, ax, ay) || rampFits(ed, f, 1, ax, ay)) fits.push_back({ax, ay});
            if (fits.empty()) continue;
            const auto step = rampPieces(4, f, 1, 0, 0)[1];  // (the first slope's place: the step's way)
            const auto key = [&](const std::pair<int, int>& p) { return p.first * step.x + p.second * step.y; };
            const auto [lo, hi] = std::minmax_element(fits.begin(), fits.end(), [&](auto& p, auto& q) { return key(p) < key(q); });
            std::vector<std::pair<int, int>> stroke = strokeAlong(4, f, 0, lo->first, lo->second, false);
            const std::vector<std::pair<int, int>> end = strokeAlong(4, f, 0, hi->first, hi->second, false);
            stroke.insert(stroke.end(), end.begin(), end.end());
            for (const RampPlan& p : planRamps(ed, stroke)) {
                ed.placeTileValues(p.tiles);
                ++ramps, slopes += p.slopes;
                std::printf("%s plateau: %s ramp of %d slopes\n", shape == Brush::Square ? "square" : "diamond", rampFacingName(p.facing), p.slopes);
            }
        }
        ed.save(out / (shape == Brush::Square ? "wide_square.scm" : "wide_diamond.scm"), err);
        std::printf("%d ramps, %d slopes\n", ramps, slopes);
    }
}

// Shift: a stroke over part of a ramp makes the whole ramp the cliff again (no ramp tile left, no way up there).
static void backToCliff(const GameData& data) {
    for (const int f : {0, 3, 4, 6}) {  // N, SE, S, W
        Editor ed(data);
        std::string err;
        if (!ed.newMap(96, 96, 4, 1, err)) return check(false, "new map: " + err);
        Brush b;
        b.shape = f == 3 ? Brush::Diamond : Brush::Square, b.size = 14;
        ed.paintBrush(48 * 32, 48 * 32, 2, b);  // (High Dirt)
        int bx = -1, by = 0;
        for (int ay = 0; ay < ed.height() && bx < 0; ++ay)
            for (int ax = 0; ax < ed.width() && bx < 0; ++ax)
                if (rampFits(ed, f, 2, ax, ay)) bx = ax, by = ay;
        const std::string what = std::string("Shift over a ") + rampFacingName(f) + " ramp";
        if (bx < 0) {
            check(false, what + ": a ramp fits");
            continue;
        }
        const std::vector<Editor::TileAt> ramp = rampTiles(4, f, 2, bx, by);
        ed.placeTileValues(ramp);
        const auto m = middleOf(ramp);
        const std::pair<int, int> top{48 * 32, 48 * 32};
        const double dx = m.first - top.first, dy = m.second - top.second, dl = std::hypot(dx, dy);
        const std::pair<int, int> out{int(m.first + dx / dl * 160), int(m.second + dy / dl * 160)};
        check(walks(ed, out, top), what + ": a way up over it");
        std::vector<std::pair<int, int>> half = strokeAlong(4, f, 2, bx, by, false);
        half.resize(2);
        const std::vector<std::pair<int, int>> under = rampTilesUnder(ed, half);
        check(under.size() == ramp.size(), what + ": all its tiles (" + std::to_string(under.size()) + " of " + std::to_string(ramp.size()) + ")");
        check(ed.remakeTilesFromIsom(under) > 0, what + ": tiles made again");
        bool left = false;
        for (const auto& c : ramp) left |= ed.terrain().tiles[size_t(c.y) * ed.width() + c.x] == c.value;
        check(!left && !walks(ed, out, top), what + ": the cliff again, no way up");
    }
}

// Terrain brushes over a ramp: high ground painted over its middle splits it round the new cliff (the ramps left fit,
// no tile of the old ramp is left where none fits); low ground painted away from it leaves it as it is. One undo step
// takes back the stroke and the refit.
static void brushOverRamp(const GameData& data) {
    struct Set {
        const char* name;
        uint16_t era, low;
        const char* high;
    };
    for (const Set& set : {Set{"jungle", 4, 1, "High Dirt"}, Set{"badlands", 0, 1, "High Dirt"}, Set{"ashworld", 3, 2, "High Dirt"},
                           Set{"ice", 6, 1, "High Snow"}, Set{"platform", 1, 2, "Elevated Catwalk"}}) {
        Editor ed(data);
        std::string err;
        if (!ed.newMap(96, 96, set.era, set.low, err)) {
            check(false, "new map: " + err);
            continue;
        }
        uint16_t high = 0;
        for (uint16_t id = 1; id < 64; ++id)
            if (ed.terrainName(id) == set.high) high = id;
        Brush b;
        b.shape = Brush::Square, b.size = 20;
        ed.paintBrush(48 * 32, 48 * 32, high, b);
        // a wide N ramp
        int bx = -1, by = 0;
        for (int ay = 0; ay < ed.height() && bx < 0; ++ay)
            for (int ax = 0; ax < ed.width() && bx < 0; ++ax)
                if (rampFits(ed, 0, 6, ax, ay)) bx = ax, by = ay;
        const std::string what = std::string(set.name) + ": a brush over a ramp";
        if (bx < 0) {
            check(false, what + ": an N ramp of 6 slopes fits");
            continue;
        }
        const std::vector<Editor::TileAt> ramp = rampTiles(set.era, 0, 6, bx, by);
        ed.placeTileValues(ramp);
        const size_t undo = ed.undoSteps();
        // high ground over the ramp's middle (a diamond, its bottom reaching below the cliff)
        const auto m = middleOf(ramp);
        Brush small;
        small.size = 2;
        ed.beginTerrainStroke();
        ed.paintBrush(m.first, m.second + 32, high, small);
        ed.endTerrainStroke();
        check(ed.undoSteps() == undo + 1, what + ": one undo step");
        // each ramp tile on the map now: part of a ramp that fits (no broken piece of the old one)
        bool broken = false;
        int pieces = 0;
        for (const auto& c : ramp) {
            const uint16_t v = ed.terrain().tiles[size_t(c.y) * ed.width() + c.x];
            if (v != c.value) continue;
            ++pieces;
        }
        const std::vector<std::pair<int, int>> left = rampTilesUnder(ed, {{m.first - 400, m.second}, {m.first + 400, m.second}});
        std::set<std::pair<int, int>> covered;
        for (int f = 0; f < kRampFacings; ++f)
            for (int ay = by - 6; ay <= by + 6; ++ay)
                for (int ax = bx - 12; ax <= bx + 30; ++ax)
                    for (int n = 0; n <= 6; ++n)
                        if (rampMatches(ed, f, n, ax, ay) && rampFits(ed, f, n, ax, ay))
                            for (const auto& c : rampTiles(set.era, f, n, ax, ay)) covered.insert({c.x, c.y});
        for (const auto& q : left) broken |= !covered.count(q);
        check(!broken, what + ": every ramp tile left is in a ramp that fits (" + std::to_string(left.size()) + " tiles, " + std::to_string(pieces) +
                           " of the old ramp's)");
        check(!left.empty(), what + ": ramps left on the edge beside the new cliff");
        // low ground far away: the ramp as it is
        const std::vector<uint16_t> keep = ed.terrain().tiles;
        ed.beginTerrainStroke();
        ed.paintBrush(10 * 32, 85 * 32, set.low, small);
        ed.endTerrainStroke();
        bool same = true;
        for (const auto& q : left) same = same && ed.terrain().tiles[size_t(q.second) * ed.width() + q.first] == keep[size_t(q.second) * ed.width() + q.first];
        check(same, what + ": a stroke away from the ramps leaves them");
        ed.undo(), ed.undo();
        bool back = true;
        for (const auto& c : ramp) back = back && ed.terrain().tiles[size_t(c.y) * ed.width() + c.x] == c.value;
        check(back, what + ": undo brings the ramp back");
    }
}

// RAMP_DOODADS: each tileset's doodads that have walkable ground of two levels (ramps and their pieces), with their
// sizes and editor groups, for the ramp kits.
static void listRampDoodads(const GameData& data) {
    for (int era = 0; era < 8; ++era) {
        Editor ed(data);
        std::string err;
        if (!ed.newMap(64, 64, uint16_t(era), 1, err)) continue;
        const Tileset& ts = ed.tileset();
        std::printf("era %d:", era);
        for (const DoodadType& t : ed.doodadTypes().all()) {
            if (!t.inPalette) continue;
            int levels = 0;
            for (int r = 0; r < t.height; ++r) {
                const size_t g = size_t(t.firstGroup) + size_t(r);
                if (g >= ts.groups.size()) continue;
                for (int c = 0; c < t.width && c < 16; ++c) {
                    const int m = int(ts.groups[g].megatiles[size_t(c)]);
                    if (!m || size_t(m) >= ts.minitileFlags.size()) continue;
                    for (uint16_t f : ts.minitileFlags[size_t(m)])
                        if (f & Tileset::kWalkable) levels |= 1 << ((f & 4) ? 2 : (f & 2) ? 1 : 0);
                }
            }
            if (levels && (levels & (levels - 1))) std::printf(" %d(%dx%d g%d L%d)", t.id, t.width, t.height, t.groupString, levels);
        }
        std::printf("\n");
        std::set<int> groups;
        for (const DoodadType& t : ed.doodadTypes().all())
            if (t.inPalette && t.groupString && groups.insert(t.groupString).second)
                std::printf("   g%d = %s\n", t.groupString, ed.unitTypes().statText(t.groupString - 1u).c_str());
    }
}

// RAMP_BLOCKS=<file>: each tileset's ramp kits from its doodads: a block of 32 doodads in the game's order (the sizes
// below); the pieces of each facing at their place in the block (Jungle's Cliff: 272 + them, the spec's ids). Each
// piece's tiles: row r of the doodad is CV5 group firstGroup + r, column c its tile c. Written as lines:
//   block <era> <base id> <group name>
//   piece <era> <base> <facing N..NW> <kind 0 wall A, 1 slope, 2 wall B> <doodad> <width> <height> <tile>...
static void dumpBlocks(const GameData& data, const char* out) {
    static const int pattern[32][2] = {{8, 5}, {4, 5}, {6, 6}, {6, 6}, {4, 5}, {6, 6}, {6, 6}, {8, 5}, {8, 3}, {8, 5}, {8, 3},
                                       {2, 5}, {4, 5}, {2, 5}, {4, 5}, {6, 5}, {6, 5}, {4, 5}, {6, 5}, {6, 5}, {2, 5}, {4, 5},
                                       {2, 5}, {4, 5}, {6, 5}, {6, 5}, {4, 5}, {6, 5}, {6, 5}, {8, 3}, {8, 5}, {8, 3}};
    // facing (N, NE, E, SE, S, SW, W, NW): the offsets of wall A, the slope, wall B in the block
    static const int place[8][3] = {{13, 12, 11}, {14, 15, 16}, {8, 9, 10}, {23, 24, 25}, {20, 21, 22}, {26, 27, 28}, {29, 30, 31}, {17, 18, 19}};
    std::FILE* f = std::fopen(out, "w");
    for (int era = 0; era < 8; ++era) {
        Editor ed(data);
        std::string err;
        if (!ed.newMap(64, 64, uint16_t(era), 1, err)) continue;
        const DoodadTypes& types = ed.doodadTypes();
        for (const DoodadType& first : types.all()) {
            bool block = true;
            for (int i = 0; i < 32 && block; ++i) {
                const DoodadType* t = types.find(uint16_t(first.id + i));
                block = t && t->inPalette && t->width == pattern[i][0] && t->height == pattern[i][1] && t->groupString == first.groupString;
            }
            if (!block) continue;
            std::fprintf(f, "block %d %d %s\n", era, first.id, ed.unitTypes().statText(first.groupString - 1u).c_str());
            for (int facing = 0; facing < 8; ++facing)
                for (int kind = 0; kind < 3; ++kind) {
                    const DoodadType* t = types.find(uint16_t(first.id + place[facing][kind]));
                    // (the doodad's cells that have a megatile: its empty cells keep the tile under them; -1 here)
                    std::fprintf(f, "piece %d %d %s %d %d %d %d", era, first.id, rampFacingName(facing), kind, t->id, t->width, t->height);
                    for (int r = 0; r < t->height; ++r)
                        for (int c = 0; c < t->width; ++c) {
                            const size_t g = size_t(t->firstGroup) + size_t(r);
                            const bool empty = g >= ed.tileset().groups.size() || !ed.tileset().groups[g].megatiles[size_t(c)];
                            std::fprintf(f, " %d", empty ? -1 : int(((t->firstGroup + r) << 4) | c));
                        }
                    std::fprintf(f, "\n");
                }
        }
    }
    std::fclose(f);
}

// RAMP_LEARN_ALL=<file>: the ramps of every kit (all blocks) in the game's maps and the example maps, and the ISOM
// corners under each of their pieces with their terrains, for research/bridge-ramp/gen_ramp_kits.py. Lines:
//   ramp <era> <block's first doodad> <facing> <slopes> <ax> <ay>
//   corner <piece> <px> <py> <terrain>   (the corner's pixel from the piece's top left)
static void learnAll(const GameData& data, const std::filesystem::path& maps, const char* examples, const char* out) {
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::recursive_directory_iterator(maps)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".scm" || ext == ".scx") files.push_back(e.path());
    }
    for (const char* name : {"jungle", "badlands", "ashworld", "ice", "platform", "desert", "twilight"})
        files.push_back(std::filesystem::path(examples) / (std::string("examples_") + name + ".scm"));
    std::FILE* f = std::fopen(out, "w");
    std::map<std::string, int> count;  // (block: ramps found)
    for (const auto& file : files) {
        Editor ed(data);
        std::string err;
        if (!ed.open(file.string(), err) || !ed.canPaintTerrain()) continue;
        const int era = ed.era() & 7;
        for (const RampOnMap& r : rampsIn(era, ed.width(), ed.height(), ed.terrain().tiles)) {
            ++count[std::to_string(era) + " " + rampKitGroup(r.kit)];
            std::fprintf(f, "ramp %d %d %s %d %d %d\n", era, rampKitBase(r.kit), rampFacingName(rampKitFacing(r.kit)), r.slopes, r.ax, r.ay);
            for (const RampPiece& p : rampKitPieces(r.kit, r.slopes, r.ax, r.ay)) {
                const auto [w, h] = rampKitPieceSize(r.kit, p.kind);
                for (int cy = p.y; cy <= p.y + h; ++cy)
                    for (int cx = (p.x * 32) / 64; cx <= (p.x + w) * 32 / 64; ++cx) {
                        if (!((cx + cy) & 1)) continue;
                        const int px = 64 * cx - 32 * p.x, py = 32 * cy - 32 * p.y;
                        if (px < 0 || py < 0 || px > w * 32 || py > h * 32) continue;
                        const std::string n = ed.terrainName(ed.terrainAt(64 * cx, 32 * cy));
                        if (!n.empty()) std::fprintf(f, "corner %d %d %d %s\n", p.kind, px, py, n.c_str());
                    }
            }
        }
    }
    std::fclose(f);
    for (const auto& [block, n] : count) std::printf("%s: %d ramps\n", block.c_str(), n);
}

// RAMP_SYNTH=<file>: for each kit, ramps where the game's own doodad rule (dddata.bin: the tiles each doodad needs
// under it) takes all its pieces: plateaus (a square and a diamond) of each pair of the tileset's terrains, on new maps.
// Written as RAMP_LEARN_ALL's lines (the corners under them), for the blocks that no map has ramps of.
static void synthAll(const GameData& data, const char* out) {
    std::FILE* f = std::fopen(out, "w");
    for (int era = 0; era < 8; ++era) {
        const std::vector<int> kits = rampKitsOf(era, true);
        if (kits.empty()) continue;
        Editor probe(data);
        std::string err;
        if (!probe.newMap(64, 64, uint16_t(era), 1, err)) continue;
        std::vector<uint16_t> terrains;
        for (uint16_t id = 1; id < 64; ++id)
            if (!probe.terrainName(id).empty()) terrains.push_back(id);
        std::map<int, int> found;  // kit: ramps
        for (const uint16_t lo : terrains)
            for (const uint16_t hi : terrains) {
                if (lo == hi) continue;
                for (const Brush::Shape shape : {Brush::Square, Brush::Diamond}) {
                    Editor ed(data);
                    if (!ed.newMap(64, 64, uint16_t(era), lo, err)) continue;
                    Brush b;
                    b.shape = shape, b.size = shape == Brush::Square ? 8 : 9;
                    if (!ed.paintBrush(32 * 32, 32 * 32, hi, b)) continue;
                    for (const int k : kits) {
                        if (found[k] >= 6) continue;
                        // each place where the rule takes all the pieces of a ramp of 1 slope (each piece's own doodad)
                        for (int ay = 0; ay < ed.height(); ++ay)
                            for (int ax = 0; ax < ed.width(); ++ax) {
                                bool all = true;
                                for (const RampPiece& p : rampKitPieces(k, 1, ax, ay)) {
                                    const DoodadType* t = ed.doodadTypes().find(uint16_t(rampKitDoodad(k, p.kind)));
                                    all = all && t && doodadFits(ed.chk(), *t, p.x, p.y, true);
                                    if (!all) break;
                                }
                                if (!all || found[k] >= 6) continue;
                                ++found[k];
                                std::fprintf(f, "ramp %d %d %s %d %d %d\n", era, rampKitBase(k), rampFacingName(rampKitFacing(k)), 1, ax, ay);
                                for (const RampPiece& p : rampKitPieces(k, 1, ax, ay)) {
                                    const auto [w, h] = rampKitPieceSize(k, p.kind);
                                    for (int cy = p.y; cy <= p.y + h; ++cy)
                                        for (int cx = (p.x * 32) / 64; cx <= (p.x + w) * 32 / 64; ++cx) {
                                            if (!((cx + cy) & 1)) continue;
                                            const int px = 64 * cx - 32 * p.x, py = 32 * cy - 32 * p.y;
                                            if (px < 0 || py < 0 || px > w * 32 || py > h * 32) continue;
                                            const std::string n = ed.terrainName(ed.terrainAt(64 * cx, 32 * cy));
                                            if (!n.empty()) std::fprintf(f, "corner %d %d %d %s\n", p.kind, px, py, n.c_str());
                                        }
                                }
                                std::printf("era %d %s %s: on %s / %s\n", era, rampKitGroup(k), rampFacingName(rampKitFacing(k)), ed.terrainName(lo).c_str(),
                                            ed.terrainName(hi).c_str());
                            }
                    }
                }
            }
        for (const int k : kits)
            if (!found[k]) std::printf("era %d %s %s: nowhere\n", era, rampKitGroup(k), rampFacingName(rampKitFacing(k)));
    }
    std::fclose(f);
}

// Every block of every tileset (Cliff, Temple Wall, Basilica, ...), on plateaus of its own terrains (a square and a
// diamond: all 8 facings): in each facing a ramp of 1 slope goes in where a stroke along the edge says, and a unit walks
// up over it; Shift over it makes it the cliff again (no way up); high ground painted over it fits it to the new cliffs
// (each ramp tile left in a ramp that fits). shots: the maps with every block's ramps, for pictures.
static void everyBlock(const GameData& data, const char* shots) {
    for (int era = 0; era < 8; ++era) {
        std::map<int, std::vector<int>> blocks;  // first doodad: its kits
        for (const int k : rampKitsOf(era)) blocks[rampKitBase(k)].push_back(k);
        for (const auto& [base, kits] : blocks) {
            const auto [lows, high] = rampKitTerrains(kits.front());
            const std::string low = lows.substr(0, lows.find('|'));
            const std::string name = std::string("era ") + std::to_string(era) + " " + rampKitGroup(kits.front()) + " (" + low + " / " + high + ")";
            std::set<int> placed, erased, refitted, offered, notInside;
            for (const Brush::Shape shape : {Brush::Square, Brush::Diamond}) {
                Editor all(data);
                std::string err;
                uint16_t lo = 0, hi = 0;
                for (const int k : kits) {
                    const int f = rampKitFacing(k);
                    const bool straight = f % 2 == 0;
                    if (straight != (shape == Brush::Square)) continue;
                    offered.insert(f);
                    Editor ed(data);
                    if (!ed.newMap(96, 96, uint16_t(era), 1, err)) continue;
                    for (uint16_t id = 1; id < 64; ++id) {
                        if (ed.terrainName(id) == low) lo = id;
                        if (ed.terrainName(id) == high) hi = id;
                    }
                    if (!lo || !hi) continue;
                    Brush b;
                    b.shape = shape, b.size = shape == Brush::Square ? 14 : 16;
                    // the place of a ramp of 1 slope nearest the middle: of a plateau of the high ground, else (a pit: the
                    // low ground is the block's own, as Sunken Ground) of a pit of the low ground in the high ground
                    int bx = -1, by = 0;
                    bool pit = false;
                    for (const bool p : {false, true}) {
                        if (bx >= 0 || !ed.newMap(96, 96, uint16_t(era), p ? hi : lo, err)) continue;
                        ed.paintBrush(48 * 32, 48 * 32, p ? lo : hi, b);
                        pit = p;
                        double bd = 1e18;
                        for (int ay = 0; ay < ed.height(); ++ay)
                            for (int ax = 0; ax < ed.width(); ++ax)
                                if (rampKitFits(ed, k, 1, ax, ay)) {
                                    const auto m = middleOf(rampKitTiles(k, 1, ax, ay));
                                    if (const double d = std::hypot(m.first - 48 * 32, m.second - 48 * 32); d < bd) bd = d, bx = ax, by = ay;
                                }
                    }
                    if (bx < 0) continue;
                    if (all.width() == 0) all.newMap(96, 96, uint16_t(era), pit ? hi : lo, err), all.paintBrush(48 * 32, 48 * 32, pit ? lo : hi, b);
                    std::vector<std::pair<int, int>> stroke;
                    for (const RampPiece& p : rampKitPieces(k, 1, bx, by)) {
                        const auto [w, h] = rampKitPieceSize(k, p.kind);
                        stroke.push_back({p.x * 32 + w * 16, p.y * 32 + h * 16});
                    }
                    const RampPlan p = planRamp(ed, stroke);
                    if (p.problem != RampPlan::Problem::None || p.facing != f) continue;
                    const auto m = middleOf(p.tiles);
                    const std::pair<int, int> top{48 * 32, 48 * 32};
                    const double dx = m.first - top.first, dy = m.second - top.second, dl = std::hypot(dx, dy);
                    const std::pair<int, int> out{int(m.first + dx / dl * 160), int(m.second + dy / dl * 160)};
                    const bool before = walks(ed, out, top);
                    ed.placeTileValues(p.tiles);
                    all.placeTileValues(p.tiles);
                    if (!before && walks(ed, out, top)) placed.insert(f);
                    // a stroke from its slope on along the edge: the new ramp has all of the old one or none of it
                    {
                        const auto [ux, uy] = std::pair{stroke[2].first - stroke[1].first, stroke[2].second - stroke[1].second};
                        const RampPlan q = planRamp(ed, {stroke[1], stroke[2], {stroke[2].first + 3 * ux, stroke[2].second + 3 * uy}});
                        std::set<std::pair<int, int>> onNew;
                        for (const auto& c : q.tiles) onNew.insert({c.x, c.y});
                        size_t shared = 0;
                        for (const auto& c : p.tiles) shared += onNew.count({c.x, c.y});
                        if (shared == 0 || shared == p.tiles.size()) notInside.insert(f);
                    }
                    // Shift: the cliff again
                    {
                        Editor copy(data);
                        copy.openChk(ed.chk(), err);
                        copy.remakeTilesFromIsom(rampTilesUnder(copy, {stroke[1]}));
                        if (!walks(copy, out, top) && rampTilesUnder(copy, stroke).empty()) erased.insert(f);
                    }
                    // high ground over the ramp's middle: each ramp tile left in a ramp that fits
                    Brush small;
                    small.size = 1;
                    ed.beginTerrainStroke();
                    ed.paintBrush(m.first, m.second, pit ? lo : hi, small);  // (the block's own ground over it)
                    ed.endTerrainStroke();
                    std::set<std::pair<int, int>> covered;
                    for (const RampOnMap& r : rampsIn(era, ed.width(), ed.height(), ed.terrain().tiles))
                        if (rampKitFits(ed, r.kit, r.slopes, r.ax, r.ay))
                            for (const auto& c : rampKitTiles(r.kit, r.slopes, r.ax, r.ay)) covered.insert({c.x, c.y});
                    bool broken = false;
                    {
                        // (a ramp tile that the terrain does not make there: the cliff of the wall blocks has their walls' tiles)
                        Editor copy(data);
                        copy.openChk(ed.chk(), err);
                        const auto left = rampTilesUnder(ed, stroke);
                        copy.remakeTilesFromIsom(left);
                        for (const auto& q : left)
                            broken |= !covered.count(q) && copy.terrain().tiles[size_t(q.second) * ed.width() + q.first] !=
                                                               ed.terrain().tiles[size_t(q.second) * ed.width() + q.first];
                    }
                    if (!broken) refitted.insert(f);
                }
                if (shots && all.width())
                    all.save(std::filesystem::path(shots) / ("block_" + std::to_string(era) + "_" + std::to_string(base) + (shape == Brush::Square ? "_square" : "_diamond") + ".scm"), err);
            }
            const auto list = [](const std::set<int>& s) {
                std::string out;
                for (int f : s) out += std::string(out.empty() ? "" : " ") + rampFacingName(f);
                return out.empty() ? std::string("none") : out;
            };
            check(placed.size() >= offered.size() - 0 && !placed.empty(), name + ": ramps with a way up (" + list(placed) + " of " + list(offered) + ")");
            check(erased == placed, name + ": Shift makes them the cliff again (" + list(erased) + ")");
            check(refitted == placed, name + ": a brush over them refits them (" + list(refitted) + ")");
            check(std::includes(notInside.begin(), notInside.end(), placed.begin(), placed.end()), name + ": a ramp over part of one has all of it (" + list(notInside) + ")");
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ramp_test <install dir> [<example maps folder>]\n");
        return 2;
    }
    GameData data;
    std::string err;
    if (std::getenv("MOD_CMBW") ? !cmbw::open(data, argv[1], err) : !data.open(argv[1], err)) {  // (MOD_CMBW: the mod's install)
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (const char* name = std::getenv("REROLL_TERRAIN")) {  // <terrain name>: a patch of it on Jungle, then the brush's re-roll along its edge
        Editor ed(data);
        std::string e;
        if (!ed.newMap(64, 64, 4, 1, e)) return 1;
        uint16_t t = 0;
        for (uint16_t id = 1; id < 256 && !t; ++id)
            if (ed.terrainName(id) == name) t = id;
        if (!t) return std::printf("no terrain %s\n", name), 1;
        Brush b;
        b.shape = Brush::Diamond, b.size = 6;
        ed.paintBrush(32 * 32, 32 * 32, t, b);
        int changed = 0, groups1024 = 0;
        for (const uint16_t v : ed.terrain().tiles) groups1024 += (v >> 4) >= 1024;
        b.size = 2;
        const std::vector<uint16_t> before = ed.terrain().tiles;
        for (int i = 0; i < 24; ++i) changed += ed.rerollTerrain(32 * 32 + int(std::cos(i * 0.26) * 6 * 32), 32 * 32 + int(std::sin(i * 0.26) * 3 * 32), b, uint32_t(i + 1));
        int high = 0;  // (re-rolled tiles of groups from 1024)
        for (size_t i = 0; i < before.size(); ++i) high += before[i] != ed.terrain().tiles[i] && (before[i] >> 4) >= 1024;
        std::printf("reroll %s: %d tiles of groups from 1024 on the map, %d tiles re-rolled, %d of them from 1024\n", name, groups1024, changed, high);
        return 0;
    }
    if (const char* ids = std::getenv("UNIT_SIZES")) {  // id,id,...: those unit types' placement boxes (MOD_CMBW: the mod's)
        Editor ed(data);
        std::string e;
        if (!ed.newMap(64, 64, 3, 1, e)) return 1;
        for (const char* p = ids; p && *p; p = std::strchr(p, ',') ? std::strchr(p, ',') + 1 : nullptr) {
            const int id = std::atoi(p);
            if (id >= 0 && size_t(id) < ed.unitTypes().size()) {
                const UnitType& t = ed.unitTypes()[size_t(id)];
                std::printf("unit %d %s: placement %dx%d depot %d resource %d\n", id, t.name.c_str(), int(t.placementWidth), int(t.placementHeight),
                            int(t.is(UnitFlag::ResourceDepot)), int(t.is(UnitFlag::ResourceContainer)));
            }
        }
        return 0;
    }
    if (const char* synth = std::getenv("RAMP_SYNTH")) {
        synthAll(data, synth);
        return 0;
    }
    if (const char* all = std::getenv("RAMP_LEARN_ALL"); all && argc > 2) {
        learnAll(data, std::filesystem::path(argv[1]) / "Maps", argv[2], all);
        return 0;
    }
    if (const char* blocks = std::getenv("RAMP_BLOCKS")) {
        dumpBlocks(data, blocks);
        return 0;
    }
    if (const char* out = std::getenv("PASSAGES_SAMPLE")) {  // <file>: a passages file for the mapper's snapshot (Badlands: a ramp half mapped, a bridge)
        stardraft::passages::Passages ps;
        stardraft::passages::RampMapping r;
        r.name = "Cliff (sample)", r.era = 0, r.block = rampKitBase(rampKitsOf(0, true).front());
        std::set<uint16_t> all;
        for (const int k : rampKitsOf(0, true))
            if (rampKitBase(k) == r.block)
                for (const Editor::TileAt& c : rampKitTiles(k, 1, 0, 0)) all.insert(c.value);
        size_t i = 0;
        for (const uint16_t v : all)
            if (i++ < all.size() / 2) r.tiles[v] = v;
        r.terrains.push_back({"Dirt", "High Dirt"});
        if (std::getenv("MOD_CMBW")) {  // (the mod's data: a mod cliff that stands in for High Dirt, on its ground)
            Editor ed(data);
            std::string e;
            if (ed.newMap(64, 64, 0, 1, e))
                for (const Tileset::ModTerrain& m : ed.tileset().modTerrains)
                    if (ed.terrainName(m.standIn).rfind("High Dirt", 0) == 0 && m.ground && !m.variantOf) {
                        r.name = "Mod cliff (sample)", r.terrains[0] = {ed.terrainName(m.ground), ed.terrainName(m.id)};
                        std::printf("sample cliff: %s on %s\n", ed.terrainName(m.id).c_str(), ed.terrainName(m.ground).c_str());
                        break;
                    }
        }
        ps.ramps.push_back(r);
        stardraft::passages::BridgeMapping b;
        b.name = "Bridge (sample)", b.era = 0, b.templateEra = 0;
        for (const uint16_t v : bridgeKitTileValues(0)) b.tiles[v] = v;
        b.terrains.push_back({"Dirt", "Water", "Dirt"});
        ps.bridges.push_back(b);
        std::string err;
        if (!stardraft::passages::save(out, ps, err)) return std::printf("%s\n", err.c_str()), 1;
        return 0;
    }
    if (const char* out = std::getenv("RAMP_MOD_PLATEAU")) {  // <folder> (MOD_CMBW): Badlands, a plateau of a mod cliff on its ground and a
        // mapping for it with no tiles yet: for the mapper's Map all and the program's Ramp brush
        Editor probe(data);
        std::string err;
        if (!probe.newMap(64, 64, 0, 1, err)) return std::printf("%s\n", err.c_str()), 1;
        if (std::getenv("RAMP_MOD_GROUPS")) {  // (the isometric terrains of the first tile groups)
            const auto t = tileGroupTerrains(probe.tileset());
            for (size_t g = 0; g < 24 && g < t.size(); ++g) std::printf("group %zu: %d %d\n", g, t[g][0], t[g][1]);
            return 0;
        }
        for (const Tileset::ModTerrain& m : probe.tileset().modTerrains) {
            if (probe.terrainName(m.standIn).rfind("High Dirt", 0) != 0 || !m.ground || m.variantOf) continue;
            Editor ed(data);
            if (!ed.newMap(96, 96, 0, m.ground, err)) continue;
            Brush b;
            b.shape = Brush::Diamond, b.size = 16;
            ed.paintBrush(48 * 32, 48 * 32, m.id, b);
            stardraft::passages::Passages ps;
            stardraft::passages::RampMapping r;
            r.name = "Mod cliff ramp", r.era = 0, r.block = rampKitBase(rampKitsOf(0, true).front());
            r.terrains.push_back({ed.terrainName(m.ground), ed.terrainName(m.id)});
            ps.ramps.push_back(r);
            const bool ok = ed.save(std::filesystem::path(out) / "plateau.scx", err) && stardraft::passages::save(std::filesystem::path(out) / "plateau.scpass", ps, err);
            std::printf("plateau of %s on %s: %s\n", ed.terrainName(m.id).c_str(), ed.terrainName(m.ground).c_str(), ok ? "saved" : err.c_str());
            // (RAMP_MOD_FILE=<a mapped .scpass>: its ramps round the plateau's edge, as the Ramp brush makes them)
            if (const char* file = std::getenv("RAMP_MOD_FILE")) {
                stardraft::passages::Passages mapped;
                if (std::string(file) == "identity") {  // (the template's tiles as they are)
                    mapped = ps;
                    for (const int k : rampKitsOf(0, true))
                        if (rampKitBase(k) == r.block)
                            for (const Editor::TileAt& c : rampKitTiles(k, 1, 0, 0)) mapped.ramps[0].tiles[c.value] = c.value;
                } else if (!stardraft::passages::load(file, mapped, err)) return std::printf("%s\n", err.c_str()), 1;
                stardraft::passages::use(mapped);
                int rx = 0, ry = 0;
                while (rx < 40 * 32 && ed.terrainAt(48 * 32 + rx, 48 * 32) == m.id) rx += 8;
                while (ry < 40 * 32 && ed.terrainAt(48 * 32, 48 * 32 + ry) == m.id) ry += 8;
                std::vector<std::pair<int, int>> ring;
                for (int k = 0; k <= 64; ++k) {
                    const double t = k / 64.0 * 4, f = t - int(t);
                    const int side = int(t) % 4;
                    const std::pair<double, double> c[5] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                    ring.push_back({48 * 32 + int((c[side].first + (c[side + 1].first - c[side].first) * f) * rx),
                                    48 * 32 + int((c[side].second + (c[side + 1].second - c[side].second) * f) * ry)});
                }
                for (const uint16_t want : {m.ground, m.id}) {  // (the first id of each name: the one a name finds)
                    uint16_t first = 0;
                    for (uint16_t id = 1; id < 256 && !first; ++id)
                        if (ed.terrainName(id) == ed.terrainName(want)) first = id;
                    std::printf("%s: id %d, first of its name %d\n", ed.terrainName(want).c_str(), want, first);
                }
                std::printf("edge right of the middle: %d at +%d, then %d\n", ed.terrainAt(48 * 32 + rx - 8, 48 * 32), rx, ed.terrainAt(48 * 32 + rx + 8, 48 * 32));
                RampPlan why;
                const std::vector<RampPlan> plans = planRamps(ed, rampBrushStroke(ring, 2), &why);
                int passage = 0;
                for (const RampPlan& p : plans) passage += p.kit >= 0 && rampKitFromPassage(p.kit);
                std::printf("ring: %zu ramps, %d of the passage (why %d; doodads: %zu)\n", plans.size(), passage, int(why.problem),
                            rampDoodads(ed).size());
                stardraft::passages::use({});
            }
            return ok ? 0 : 1;
        }
        return std::printf("no mod cliff\n"), 1;
    }
    if (std::getenv("RAMP_PASSAGES")) {  // each tileset: a passage ramp (the first block, its tiles as they are) on each cliff that no game kit has
        int made = 0, tried = 0;
        bool savedOne = false;
        for (int era = 0; era < 8; ++era) {
            Editor probe(data);
            std::string err;
            if (!probe.newMap(64, 64, uint16_t(era), 1, err)) continue;
            int block = -1;
            std::set<std::string> kitHighs;
            for (const int k : rampKitsOf(era, true)) {
                if (block < 0) block = rampKitBase(k);
                kitHighs.insert(rampKitTerrains(k).second);
            }
            if (block < 0) continue;
            for (uint16_t high = 2; high < 64; ++high) {
                const std::string hn = probe.terrainName(high);
                if (hn.rfind("High ", 0) != 0 || kitHighs.count(hn)) continue;
                uint16_t low = 0;
                for (uint16_t id = 1; id < 64 && !low; ++id)
                    if (probe.terrainName(id) == hn.substr(5)) low = id;
                if (!low) continue;
                stardraft::passages::Passages ps;
                stardraft::passages::RampMapping m;
                m.name = "probe", m.era = era, m.block = block;
                for (const int k : rampKitsOf(era, true))
                    if (rampKitBase(k) == block)
                        for (const Editor::TileAt& c : rampKitTiles(k, 1, 0, 0)) m.tiles[c.value] = c.value;
                m.terrains.push_back({probe.terrainName(low), hn});
                ps.ramps.push_back(m);
                stardraft::passages::use(ps);
                Editor ed(data);
                ed.newMap(96, 96, uint16_t(era), low, err);
                Brush b;
                b.shape = Brush::Diamond, b.size = 16;
                ed.paintBrush(48 * 32, 48 * 32, high, b);
                int rx = 0;
                while (rx < 40 * 32 && ed.terrainAt(48 * 32 + rx, 48 * 32) == high) rx += 8;
                // (along the plateau's south-east edge)
                std::vector<std::pair<int, int>> stroke;
                for (int k = 2; k <= 6; ++k) stroke.push_back({48 * 32 + rx * (8 - k) / 8, 48 * 32 + rx / 2 * k / 8});
                ++tried;
                int passage = 0;
                // (the whole ring round the plateau, as RAMP_PLATEAUS)
                int ry = 0;
                while (ry < 40 * 32 && ed.terrainAt(48 * 32, 48 * 32 + ry) == high) ry += 8;
                stroke.clear();
                for (int k = 0; k <= 64; ++k) {
                    const double t = k / 64.0 * 4;
                    const int side = int(t) % 4;
                    const double f = t - int(t);
                    const std::pair<double, double> c[5] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                    stroke.push_back({48 * 32 + int((c[side].first + (c[side + 1].first - c[side].first) * f) * rx),
                                      48 * 32 + int((c[side].second + (c[side + 1].second - c[side].second) * f) * ry)});
                }
                // (the terrains at the plateau's edge, right of its middle: the cliff there is not the pair asked for when
                // the brush put other terrains between them)
                {
                    int x = 48 * 32;
                    while (x < 95 * 32 && ed.terrainAt(x, 48 * 32) == high) x += 16;
                    const uint16_t next = ed.terrainAt(x, 48 * 32);
                    if (next != low) {
                        std::printf("era %d cliff %-22s: skipped, the edge is %s next to %s\n", era, hn.c_str(), probe.terrainName(next).c_str(), hn.c_str());
                        --tried;
                        continue;
                    }
                }
                // (RAMP_PASSAGES_OUT=<folder>: the first plateau, before its ramps, and its mapping with no tiles yet: for the
                // mapper's Map all and the program's Ramp brush)
                if (const char* out = std::getenv("RAMP_PASSAGES_OUT"); out && *out && !savedOne) {
                    stardraft::passages::Passages empty = ps;
                    empty.ramps[0].name = "Mod cliff ramp", empty.ramps[0].tiles.clear();
                    std::string e2;
                    savedOne = ed.save(std::filesystem::path(out) / "plateau.scx", e2) && stardraft::passages::save(std::filesystem::path(out) / "plateau.scpass", empty, e2);
                    std::printf("saved plateau of %s on %s (era %d): %s\n", hn.c_str(), probe.terrainName(low).c_str(), era, savedOne ? "ok" : e2.c_str());
                }
                RampPlan why;
                const std::vector<RampPlan> plans = planRamps(ed, rampBrushStroke(stroke, 2), &why);
                if (plans.empty()) std::printf("  (why %d; kits on the map: %d)\n", int(why.problem), int(rampKitsOf(era).size()));
                for (const RampPlan& p : plans) passage += p.kit >= 0 && rampKitFromPassage(p.kit);
                made += passage > 0;
                std::printf("era %d cliff %-22s on %-16s: %zu ramps, %d of a passage\n", era, hn.c_str(), probe.terrainName(low).c_str(), plans.size(), passage);
            }
        }
        stardraft::passages::use({});
        std::printf("%d of %d cliffs got a passage ramp\n", made, tried);
        return 0;
    }
    if (const char* out = std::getenv("RAMP_PLATEAUS")) {  // <folder>: per tileset, a plateau and the brush (size 2) round its edge: the ramps, saved
        for (int era = 0; era < 8; ++era) {
            Editor ed(data);
            std::string err;
            if (!ed.newMap(96, 96, uint16_t(era), 1, err)) continue;
            uint16_t high = 0;
            for (uint16_t id = 2; id < 64 && !high; ++id)
                if (ed.terrainName(id).rfind("High ", 0) == 0) high = id;
            if (!high || !rampsAvailable(ed)) continue;
            Brush b;
            b.shape = Brush::Diamond, b.size = 16;
            ed.paintBrush(48 * 32, 48 * 32, high, b);
            // (round the plateau's edge: a diamond 16 * 64 px across, 16 * 32 px down)
            std::vector<std::pair<int, int>> ring;
            const int cx = 48 * 32, cy = 48 * 32;
            int rx = 0, ry = 0;  // (the plateau's edge: right of the middle and under it)
            while (rx < 40 * 32 && ed.terrainAt(cx + rx, cy) == high) rx += 8;
            while (ry < 40 * 32 && ed.terrainAt(cx, cy + ry) == high) ry += 8;
            for (int k = 0; k <= 64; ++k) {
                const double t = k / 64.0 * 4;
                const int side = int(t) % 4;
                const double f = t - int(t);
                const std::pair<double, double> c[5] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                ring.push_back({cx + int((c[side].first + (c[side + 1].first - c[side].first) * f) * rx),
                                cy + int((c[side].second + (c[side + 1].second - c[side].second) * f) * ry)});
            }
            int placed = 0;
            {
                RampPlan why;
                const auto raw = planRamps(ed, ring, &why);
                const auto wide = planRamps(ed, rampBrushStroke(ring, 2));
                for (int off = -64; off <= 64; off += 32) {
                    const RampPlan q = planRamp(ed, {{ring[8].first + off, ring[8].second - off}, {ring[8].first + off + 128, ring[8].second - off + 64}});
                    std::printf("  single at offset %d: problem %d kit %d, terrain %d\n", off, int(q.problem), q.kit, ed.terrainAt(ring[8].first + off, ring[8].second - off));
                }
                std::printf("era %d: raw %zu (problem %d), brush %zu, ring %d,%d to %d,%d, terrain at middle %d, at ring %d\n", era, raw.size(), int(why.problem), wide.size(),
                            ring.front().first, ring.front().second, ring[8].first, ring[8].second, ed.terrainAt(cx, cy), ed.terrainAt(ring[8].first, ring[8].second));
            }
            // (a short stroke in the middle of each edge, one after the other, as a user paints them)
            for (int side = 0; side < 4; ++side)
                for (const RampPlan& p : planRamps(ed, rampBrushStroke(std::vector<std::pair<int, int>>(ring.begin() + side * 16 + 6, ring.begin() + side * 16 + 11), 2)))
                    placed += p.doodad >= 0 ? ed.placeDoodad(uint16_t(p.doodad), p.x, p.y, true) : ed.placeTileValues(p.tiles) > 0;
            // (RAMP_CORNERS: a short stroke over each corner too, where the straight cliff pieces are)
            if (std::getenv("RAMP_CORNERS"))
                for (int side = 0; side < 4; ++side) {
                    const int k = side * 16;
                    std::vector<std::pair<int, int>> s(ring.begin() + std::max(0, k - 2), ring.begin() + std::min(64, k + 3));
                    for (const RampPlan& p : planRamps(ed, rampBrushStroke(s, 2)))
                        placed += p.doodad >= 0 ? ed.placeDoodad(uint16_t(p.doodad), p.x, p.y, true) : ed.placeTileValues(p.tiles) > 0;
                }
            std::printf("era %d: %d ramps (plateau %s)\n", era, placed, ed.terrainName(high).c_str());
            if (std::getenv("RAMP_WHY"))  // (each ramp doodad: does the game's rule let it go anywhere on the map, and what it needs under it)
                for (const uint16_t id : rampDoodads(ed)) {
                    const DoodadType* t = ed.doodadTypes().find(id);
                    int fits = 0;
                    for (int y = 0; y < ed.height() * 32; y += 32)
                        for (int x = 0; x < ed.width() * 32; x += 64) fits += ed.nearestDoodadFit(id, x, y, true, 0).has_value();
                    std::set<std::string> under;  // (the terrains of the tile groups it needs)
                    for (const uint16_t g : t->required)
                        if (g && g < ed.tileset().groups.size()) {
                            const uint16_t v = uint16_t(g << 4);
                            under.insert(std::to_string(g) + ":" + std::to_string(ed.tileset().groups[g].type));
                        }
                    std::string u;
                    for (const auto& s : under) u += s + " ";
                    std::printf("  doodad %d (%dx%d): fits at %d places; needs groups (group:type) %s\n", id, t->width, t->height, fits, u.c_str());
                }
            ed.save(std::filesystem::path(out) / ("ramps_" + std::to_string(era) + ".scm"), err);
        }
        return 0;
    }
    if (const char* maps = std::getenv("RAMP_MAPS")) {  // (a mod's maps) each ramp doodad taken away, then a stroke there: the ramp back?
        std::map<std::pair<int, int>, std::array<int, 3>> by;  // (era, doodad): cases, a ramp placed there, the same doodad at the same place
        int cases = 0;
        for (const auto& e : std::filesystem::recursive_directory_iterator(maps)) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".scm" && ext != ".scx") continue;
            Editor ed(data);
            std::string err;
            if (!ed.open(e.path().string(), err) || !ed.canPaintTerrain()) continue;
            const std::vector<uint16_t> ramps = rampDoodads(ed);
            if (ramps.empty()) continue;
            for (int i = 0; i < int(ed.doodads().size()) && cases < 4000; ++i) {
                const Doodad d = ed.doodads()[size_t(i)];
                if (std::find(ramps.begin(), ramps.end(), d.id) == ramps.end()) continue;
                Editor copy(data);
                if (!copy.openChk(ed.chk(), err) || !copy.deleteDoodad(i)) continue;
                ++cases;
                std::array<int, 3>& n = by[{ed.era() & 7, d.id}];
                ++n[0];
                const std::vector<RampPlan> plans = planRamps(copy, {{d.x, d.y}});
                if (plans.empty() || plans.front().doodad < 0) continue;
                const RampPlan& p = plans.front();
                if (!copy.placeDoodad(uint16_t(p.doodad), p.x, p.y, true)) continue;
                ++n[1];
                n[2] += p.doodad == d.id && p.x == d.x && p.y == d.y;
            }
        }
        int solved = 0;
        for (const auto& [k, n] : by) {
            std::printf("era %d doodad %d: %d cases, %d placed back, %d the same doodad at the same place\n", k.first, k.second, n[0], n[1], n[2]);
            solved += n[1] > 0;
        }
        std::printf("%d ramp doodads placed by the brush (of %zu on the maps), %d cases\n", solved, by.size(), cases);
        return 0;
    }
    if (const char* maps = std::getenv("RAMP_SURVEY")) {  // the doodads that join two ground levels (their own tiles), and how often maps place them
        std::map<std::pair<int, int>, int> used;  // (era, doodad id): placed
        for (const auto& e : std::filesystem::recursive_directory_iterator(maps)) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".scm" && ext != ".scx") continue;
            Editor ed(data);
            std::string err;
            if (!ed.open(e.path().string(), err)) continue;
            for (const Doodad& d : ed.doodads()) ++used[{ed.era() & 7, d.id}];
        }
        for (int era = 0; era < 8; ++era) {
            Editor ed(data);
            std::string err;
            if (!ed.newMap(64, 64, uint16_t(era), 1, err)) continue;
            for (const DoodadType& t : ed.doodadTypes().all()) {
                std::vector<Editor::TileAt> tiles;
                for (int r = 0; r < t.height; ++r)
                    for (int c = 0; c < t.width; ++c)
                        if (const uint16_t v = uint16_t((t.firstGroup + r) << 4 | c); ed.tileset().megatileFor(v) > 0) tiles.push_back({c, r, v});
                if (tiles.empty() || !rampJoins(ed.tileset(), tiles)) continue;
                const auto u = used.find({era, t.id});
                // (its minitiles: walkable ones per ground level, and those on the doodad's border)
                int all = 0, level[3] = {}, border[3] = {};
                for (const auto& c : tiles) {
                    const int m = ed.tileset().megatileFor(c.value);
                    for (int i = 0; i < 16; ++i, ++all) {
                        const uint16_t f = ed.tileset().minitileFlags[size_t(m)][size_t(i)];
                        if (!(f & Tileset::kWalkable)) continue;
                        const int l = (f & 4) ? 2 : (f & 2) ? 1 : 0;
                        ++level[l];
                        const int mx = c.x * 4 + i % 4, my = c.y * 4 + i / 4;
                        border[l] += mx == 0 || my == 0 || mx == t.width * 4 - 1 || my == t.height * 4 - 1;
                    }
                }
                std::printf("joins era %d doodad %d first %d size %dx%d placed %d walk %d%% (low %d mid %d high %d) border low %d mid %d high %d\n", era, t.id, t.firstGroup,
                            t.width, t.height, u == used.end() ? 0 : u->second, 100 * (level[0] + level[1] + level[2]) / std::max(1, all), level[0], level[1], level[2],
                            border[0], border[1], border[2]);
            }
        }
        return 0;
    }
    if (std::getenv("RAMP_RESHAPE")) {  // a ragged plateau: of the strokes round its edge, those with a ramp before and after the cliff smoothing
        int bad = 0;
        for (const int era : {4, 0, 1, 2, 3, 5, 6, 7}) {
            Editor ed(data);
            std::string err;
            if (!ed.newMap(128, 128, uint16_t(era), 1, err)) continue;
            if (rampKitsOf(era).empty()) continue;
            const auto [lowName, highName] = rampKitTerrains(rampKitsOf(era).front());
            uint16_t high = 0, low = 0;
            const std::string low0 = lowName.substr(0, lowName.find('|'));
            for (uint16_t id = 1; id < 128; ++id) {
                if (!high && ed.terrainName(id) == highName) high = id;
                if (!low && ed.terrainName(id) == low0) low = id;
            }
            if (!high) continue;
            Brush b;
            b.shape = Brush::Diamond;
            if (low) b.size = 24, ed.paintBrush(64 * 32, 64 * 32, low, b);  // (the kit's ground below round the plateau)
            b.size = 8;
            ed.paintBrush(64 * 32, 64 * 32, high, b);
            uint32_t seed = 12345;
            for (int i = 0; i < 40; ++i) {  // (a ragged edge: small dabs round the plateau's rim)
                seed = seed * 1103515245 + 12345;
                const double a = (seed >> 8) % 6283 / 1000.0;
                b.size = 1 + float((seed >> 20) % 2);
                ed.paintBrush(64 * 32 + int(std::cos(a) * 8 * 64), 64 * 32 + int(std::sin(a) * 8 * 32), high, b);
            }
            int before = 0, after = 0, strokes = 0;
            double worst = 0;
            // (RAMP_SHOTS: the plateau with the ramps of all the strokes, without and with the smoothing)
            Editor plain(data), smooth(data);
            if (!plain.openChk(ed.chk(), err) || !smooth.openChk(ed.chk(), err)) return 1;
            for (int i = 0; i < 48; ++i) {
                const double a = i * 6.2832 / 48;
                const std::vector<std::pair<int, int>> stroke =
                    rampBrushStroke({{64 * 32 + int(std::cos(a) * 8 * 64), 64 * 32 + int(std::sin(a) * 8 * 32)}}, 1.0f);
                const std::vector<RampPlan> r0 = planRamps(ed, stroke);
                ++strokes, before += !r0.empty();
                Editor c(data);
                if (!c.openChk(ed.chk(), err)) return 1;
                const auto t0 = std::chrono::steady_clock::now();
                const int made = reshapeForRamps(c, stroke, r0);
                const std::vector<RampPlan> r1 = made ? planRamps(c, stroke) : r0;
                worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                after += !r1.empty();
                if (c.invalidTerrain()) ++bad, std::printf("era %d stroke %d: invalid ISOM after the smoothing\n", era, i);
                if (made && r1.empty()) ++bad, std::printf("era %d stroke %d: smoothed, but no ramp\n", era, i);
                if (std::getenv("RAMP_SHOTS")) {
                    for (const RampPlan& p : planRamps(plain, stroke)) plain.placeTileValues(p.tiles);
                    std::vector<RampPlan> s = planRamps(smooth, stroke);
                    if (reshapeForRamps(smooth, stroke, s)) s = planRamps(smooth, stroke);
                    for (const RampPlan& p : s) smooth.placeTileValues(p.tiles);
                }
            }
            if (const char* shots = std::getenv("RAMP_SHOTS")) {
                if (smooth.invalidTerrain()) ++bad, std::printf("era %d: invalid ISOM after all the strokes\n", era);
                plain.save(std::filesystem::path(shots) / ("reshape_" + std::to_string(era) + "_before.scm"), err);
                smooth.save(std::filesystem::path(shots) / ("reshape_" + std::to_string(era) + "_after.scm"), err);
            }
            std::printf("era %d (%s on %s): %d strokes: a ramp at %d before, %d after the smoothing (worst %.1f ms)\n", era, highName.c_str(), lowName.c_str(), strokes, before, after, worst);
        }
        std::printf("%d failures\n", bad);
        return bad ? 1 : 0;
    }
    if (std::getenv("RAMP_SCRUB")) {  // a drag back and forth over a small plateau with no room for a ramp: each update's time
        Editor ed(data);
        std::string err;
        if (!ed.newMap(128, 128, 0, 1, err)) return 1;
        Brush b;
        b.shape = Brush::Diamond, b.size = std::getenv("RAMP_PLATEAU") ? float(std::atof(std::getenv("RAMP_PLATEAU"))) : 3.0f;
        ed.paintBrush(64 * 32, 64 * 32, 2, b);  // (Badlands High Dirt)
        const int amp = std::getenv("RAMP_AMP") ? std::atoi(std::getenv("RAMP_AMP")) : 140;
        std::vector<std::pair<int, int>> stroke;
        double worst = 0, total = 0;
        for (int i = 0; i < 3000; ++i) {
            const double t = i * 0.05;
            stroke.push_back({64 * 32 + int(std::sin(t) * amp), 64 * 32 + int(std::cos(t * 1.3) * amp / 2)});
            const auto t0 = std::chrono::steady_clock::now();
            const std::vector<std::pair<int, int>> pts = rampBrushStroke(stroke, std::getenv("RAMP_SIZE") ? float(std::atof(std::getenv("RAMP_SIZE"))) : 2.0f);
            const std::vector<RampPlan> r = planRamps(ed, pts);
            if (std::getenv("RAMP_SCRUB_SMOOTH")) {  // (and the cliff smoothing on a copy of the map, as the editor's preview)
                Editor c(data);
                if (c.openChk(ed.chk(), err) && reshapeForRamps(c, pts, r)) planRamps(c, pts);
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            worst = std::max(worst, ms), total += ms;
            if (i == 100 || i == 500 || i == 1000 || i == 2000 || i == 2999) std::printf("scrub %d points: %.1f ms\n", i + 1, ms);
            if (total > 60000) {
                std::printf("scrub: stopped at %d points, a minute in all\n", i + 1);
                break;
            }
        }
        std::printf("scrub: worst %.1f ms, all %.0f ms\n", worst, total);
        return 0;
    }
    if (std::getenv("RAMP_BENCH")) {  // a drag's preview times: the stroke so far planned again at each point (as the editor does)
        for (const int era : {4, 7, 1}) {
            Editor ed(data);
            std::string err;
            if (!ed.newMap(256, 256, uint16_t(era), 1, err)) continue;
            uint16_t high = 0;
            for (uint16_t id = 1; id < 64; ++id)
                if (ed.terrainName(id) == (era == 1 ? "Elevated Catwalk" : era == 7 ? "Basilica" : "High Dirt")) high = id;
            Brush b;
            b.shape = Brush::Diamond, b.size = 40;
            ed.paintBrush(128 * 32, 128 * 32, high, b);
            // (a stroke from open ground to the plateau's north-east edge, then along it: mouse moves 8 px apart)
            std::vector<std::pair<int, int>> stroke;
            for (int i = 0; i < 160; ++i) stroke.push_back({128 * 32 + 1600 - i * 8, 128 * 32 - 1300 + i * 4});
            for (int i = 0; i < 400; ++i) stroke.push_back({stroke.back().first + 8, stroke.back().second + 4 - (i % 2) * 0});
            const float size = std::getenv("RAMP_SIZE") ? float(std::atof(std::getenv("RAMP_SIZE"))) : 1.0f;  // (the brush's size)
            double total = 0, worst = 0;
            for (size_t n = 1; n <= stroke.size(); n += 1) {
                const auto t0 = std::chrono::steady_clock::now();
                const std::vector<RampPlan> ramps =
                    planRamps(ed, rampBrushStroke(std::vector<std::pair<int, int>>(stroke.begin(), stroke.begin() + std::ptrdiff_t(n)), size));
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                total += ms, worst = std::max(worst, ms);
                if (n == 50 || n == 160 || n == 300 || n == stroke.size())
                    std::printf("bench era %d: %zu points: %.1f ms (%zu ramps)\n", era, n, ms, ramps.size());
            }
            std::printf("bench era %d: whole drag %.0f ms, worst update %.1f ms\n", era, total, worst);
        }
        return 0;
    }
    if (std::getenv("RAMP_BLOCKS_ONLY")) {  // (only the every-block check)
        everyBlock(data, std::getenv("RAMP_SHOTS"));
        std::printf("\n%d failures\n", failures);
        return failures ? 1 : 0;
    }
    if (std::getenv("RAMP_DOODADS")) {
        listRampDoodads(data);
        return 0;
    }
    if (const char* wide = std::getenv("RAMP_WIDE")) {
        wideRamps(data, wide);
        return 0;
    }
    if (const char* out = std::getenv("RAMP_NEIGHBORS"); out && argc > 2) {  // RAMP_NEIGHBORS=<file>: the corpus's pairs
        dumpNeighbors(data, std::filesystem::path(argv[1]) / "Maps", argv[2], out);
        return 0;
    }
    if (const char* out = std::getenv("RAMP_LEARN"); out && argc > 2) {
        learn(data, argv[2], out);
        return 0;
    }
    check(rampsAvailable(4), "ramps in Jungle");
    paintedPlateaus(data, std::getenv("RAMP_SHOTS"));
    longAndOver(data);
    roundCorner(data);
    backToCliff(data);
    brushOverRamp(data);
    everyBlock(data, nullptr);
    if (argc > 2) examples(data, argv[2]);
    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
