// The Ramp brush's checks (src/ramps.cpp).
//   ramp_test <install dir> [<example maps folder: examples_<tileset>.scm>]
// RAMP_LEARN=<file>: the ISOM corners under each piece of the example maps' ramps (on their plateaus), for
// research/bridge-ramp/gen_ramp_kits.py.
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/ramps.h"
#include "stardraft/tileset.h"

#include <algorithm>
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
        const std::string high = planRamp(ed, {}).high;
        int tested = 0, walls = 0;
        for (const FoundRamp& r : findRamps(ed)) {
            bool cliff = false;  // (on a plateau: high ground under it somewhere)
            for (const auto& c : rampTiles(era, r.facing, r.slopes, r.ax, r.ay))
                for (int dy = 0; dy <= 1 && !cliff; ++dy)
                    for (int dx = 0; dx <= 1 && !cliff; ++dx) cliff = ed.terrainName(ed.terrainAt((c.x + dx) * 32, (c.y + dy) * 32)) == high;
            if (!cliff) continue;
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
                                                                                                            rampFacingName(p.facing) + " " + std::to_string(p.slopes) + ")");
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
    for (const int era : {0, 1, 3, 4, 6})
        for (int f = 0; f < kRampFacings; ++f)
            for (const auto& c : rampTiles(era, f, 1, 0, 0)) kit[era].insert(variant(c.value));
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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ramp_test <install dir> [<example maps folder>]\n");
        return 2;
    }
    GameData data;
    std::string err;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
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
    if (argc > 2) examples(data, argv[2]);
    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
