#include "stardraft/terrain_gen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "stardraft/isom.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace stardraft {

// --- Noise (after PUDForge's noise.cpp) ------------------------------------------------------------

namespace {
constexpr float kGrad[8][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
const float kF2 = 0.5f * (std::sqrt(3.0f) - 1.0f);
const float kG2 = (3.0f - std::sqrt(3.0f)) / 6.0f;
uint32_t lcg(uint32_t& s) { return s = s * 1664525u + 1013904223u; }
}  // namespace

SimplexNoise::SimplexNoise(uint32_t seed) {
    uint8_t p[256];
    for (int i = 0; i < 256; ++i) p[i] = uint8_t(i);
    uint32_t state = seed ? seed : 1u;
    for (int i = 255; i > 0; --i) std::swap(p[i], p[lcg(state) % uint32_t(i + 1)]);  // Fisher-Yates
    for (int i = 0; i < 512; ++i) perm_[i] = p[i & 255];
}

float SimplexNoise::at(float x, float y) const {
    const float s = (x + y) * kF2;
    const int i = int(std::floor(x + s)), j = int(std::floor(y + s));
    const float t = float(i + j) * kG2;
    const float x0 = x - (float(i) - t), y0 = y - (float(j) - t);
    const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
    const float x1 = x0 - float(i1) + kG2, y1 = y0 - float(j1) + kG2;
    const float x2 = x0 - 1.0f + 2.0f * kG2, y2 = y0 - 1.0f + 2.0f * kG2;
    const int ii = i & 255, jj = j & 255;
    const int g0 = perm_[ii + perm_[jj]] & 7, g1 = perm_[ii + i1 + perm_[jj + j1]] & 7, g2 = perm_[ii + 1 + perm_[jj + 1]] & 7;
    auto corner = [](float dx, float dy, int g) {
        float t0 = 0.5f - dx * dx - dy * dy;
        if (t0 < 0) return 0.0f;
        t0 *= t0;
        return t0 * t0 * (kGrad[g][0] * dx + kGrad[g][1] * dy);
    };
    return 70.0f * (corner(x0, y0, g0) + corner(x1, y1, g1) + corner(x2, y2, g2));
}

LayeredNoise::LayeredNoise(std::vector<NoiseLayer> layers) {
    for (const auto& l : layers) {
        if (l.weight == 0) continue;
        layers_.push_back(l);
        noises_.emplace_back(l.seed);
        total_ += std::fabs(l.weight);
    }
}

float LayeredNoise::at(float x, float y) const {
    if (total_ == 0) return 0.5f;
    float sum = 0;
    for (size_t i = 0; i < layers_.size(); ++i) sum += noises_[i].at(x * layers_[i].scale, y * layers_[i].scale) * layers_[i].weight;
    return 0.5f + 0.5f * (sum / total_);
}

// --- Terrain roles ---------------------------------------------------------------------------------

#include "data/terrain_shares.inc"

std::vector<GenTerrain> generatorTerrains(uint16_t era, const Tileset& ts) {
    std::vector<GenTerrain> out;
    const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
    if (!table) return out;
    std::vector<uint16_t> ids;
    for (uint16_t t = 1; t <= table->terrainCount(); ++t)
        if (table->isTerrain(t)) ids.push_back(t);
    if (ids.empty()) return out;
    auto degree = [&](uint16_t t) {
        int d = 0;
        for (uint16_t u : ids) d += table->linked(t, u);
        return d;
    };
    auto share = [&](uint16_t t) { return t >= 1 && t <= 16 ? kTerrainShares[era & 7][t - 1] : 0.0f; };
    // The base ground: the terrain with the most links (the hub of the tree), larger share on ties.
    uint16_t base = ids[0];
    for (uint16_t t : ids)
        if (degree(t) > degree(base) || (degree(t) == degree(base) && share(t) > share(base))) base = t;
    // Tree from the base.
    std::vector<int> depth(table->terrainCount() + 1, -1);
    std::vector<uint16_t> parent(table->terrainCount() + 1, 0), order{base};
    depth[base] = 0;
    for (size_t h = 0; h < order.size(); ++h)
        for (uint16_t u : ids)
            if (depth[u] < 0 && table->linked(order[h], u)) depth[u] = depth[order[h]] + 1, parent[u] = order[h], order.push_back(u);
    for (uint16_t t : order) {
        GenTerrain g;
        g.id = t, g.parent = t == base ? 0 : parent[t], g.depth = depth[t], g.defaultShare = share(t), g.name = table->terrainName(t);
        g.role = t == base ? GenTerrain::Base : GenTerrain::Patch;
        // Walkability and elevation of the tile group under solid terrain t (VF4 minitile flags:
        // 0x1 walkable, 0x2 mid ground, 0x4 high ground).
        int group = solidTileGroup(ts, t);
        if (group >= 0 && size_t(group) < ts.groups.size()) {
            uint16_t mega = ts.groups[size_t(group)].megatiles[0];
            if (mega < ts.minitileFlags.size()) {
                int walk = 0, mid = 0, high = 0;
                for (uint16_t f : ts.minitileFlags[mega]) walk += f & 1, mid += (f >> 1) & 1, high += (f >> 2) & 1;
                g.walkable = walk >= 8;
                g.level = high >= 8 ? 2 : mid >= 8 ? 1 : 0;
            }
        }
        out.push_back(g);
    }
    for (auto& g : out) {
        uint16_t par = g.parent ? g.parent : 0;
        for (const auto& ps : kPairStraightness)
            if (ps.era == (era & 7) && ((ps.a == g.id && ps.b == par) || (ps.b == g.id && ps.a == par))) g.blocky = ps.straight >= 0.55f;
    }
    const int baseLevel = out[0].level;
    GenTerrain *sea = nullptr, *high = nullptr;
    for (auto& g : out) {
        if (g.depth != 1) continue;
        if (!g.walkable && (!sea || g.defaultShare > sea->defaultShare)) sea = &g;
        if (g.walkable && g.level > baseLevel && (!high || g.defaultShare > high->defaultShare)) high = &g;
    }
    if (sea) sea->role = GenTerrain::Sea;
    if (high) high->role = GenTerrain::High;
    return out;
}

// --- Generator -------------------------------------------------------------------------------------

bool generateTerrain(Chk& chk, const Tileset& ts, const TerrainGenParams& p, std::string& error) {
    // STARDRAFT_GEN_TIME=1 prints how long each stage took (for speed work).
    const bool timing = std::getenv("STARDRAFT_GEN_TIME") != nullptr;
    auto tLast = std::chrono::steady_clock::now();
    auto lap = [&](const char* name) {
        if (!timing) return;
        const auto now = std::chrono::steady_clock::now();
        std::fprintf(stderr, "  %-12s %7.1f ms\n", name, std::chrono::duration<double, std::milli>(now - tLast).count());
        tLast = now;
    };
    IsomGrid grid;
    grid.setTileset(&ts);  // (the tiles: the terrain core's, from the tileset's CV5)
    if (!grid.load(chk, error)) return false;
    const uint16_t era = readEra(chk);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
    const std::vector<GenTerrain> terr = generatorTerrains(era, ts);
    if (!table || terr.empty()) {
        error = std::string("no terrain table for tileset ") + tilesetName(era);
        return false;
    }
    const int cols = grid.rectCols(), rows = grid.rectRows();  // corners live on x in [-1, cols+1], y in [-1, rows+1]
    const int w = p.width, h = p.height;
    const size_t nT = table->terrainCount() + 1;

    // Terrain facts by id.
    std::vector<int> idx(nT, -1);
    for (size_t i = 0; i < terr.size(); ++i) idx[terr[i].id] = int(i);
    auto info = [&](uint16_t t) -> const GenTerrain& { return terr[size_t(idx[t])]; };
    const uint16_t base = terr[0].id;
    uint16_t sea = 0, high = 0;
    for (const auto& g : terr) {
        if (g.role == GenTerrain::Sea) sea = g.id;
        if (g.role == GenTerrain::High) high = g.id;
    }
    std::vector<float> own(nT, 0.0f), sub(nT, 0.0f);  // share of the map: the terrain alone / with all on it
    for (const auto& g : terr) own[g.id] = g.id < p.shares.size() && p.shares[g.id] >= 0 ? p.shares[g.id] : g.defaultShare;
    for (auto it = terr.rbegin(); it != terr.rend(); ++it) {  // deepest first
        sub[it->id] += own[it->id];
        if (it->parent) sub[it->parent] += sub[it->id];
    }
    std::vector<std::vector<uint16_t>> children(nT);
    for (const auto& g : terr)
        if (g.parent) children[g.parent].push_back(g.id);
    // inSubtree(t, root): t is root or under it. The hot loops ask it millions of times: a table, made once.
    std::vector<uint8_t> subtree(nT * nT, 0);
    for (size_t t0 = 0; t0 < nT; ++t0) {
        if (t0) subtree[t0 * nT + t0] = 1;  // t is in its own subtree, as the walk found first
        if (idx[t0] < 0) continue;
        for (uint16_t t = uint16_t(t0); t; t = t == base ? 0 : info(t).parent) subtree[t0 * nT + t] = 1;
    }
    auto inSubtree = [&](uint16_t t, uint16_t root) { return t < nT && root < nT && subtree[size_t(t) * nT + root] != 0; };
    // The terrain at depth 1 a terrain belongs under (or the base itself): its "class".
    auto top = [&](uint16_t t) {
        while (t != base && info(t).parent != base) t = info(t).parent;
        return t;
    };
    auto parentOf = [&](uint16_t t) { return t == base ? base : info(t).parent; };
    const bool inHigh = high != 0;

    // Fields, sampled at each corner's position in tiles (a corner at grid (x, y) is at tile (2x, y)).
    const uint32_t s = p.seed ? p.seed : 1u;
    const float rough = std::clamp(p.roughness, 0.0f, 1.0f);
    LayeredNoise land({{p.landScale / 1000.0f, s, 1.0f},
                       {p.coastline / 1000.0f, s * 2654435761u, 0.15f + 0.45f * rough},
                       {p.detail / 1000.0f, s * 40503u + 7u, 0.05f + 0.55f * rough}});
    LayeredNoise massif({{p.landScale / 1000.0f * 1.3f, s ^ 0xA5A5A5u, 1.0f},
                         {p.coastline / 1000.0f, s * 977u + 3u, 0.15f + 0.45f * rough},
                         {p.detail / 1000.0f, s * 6007u + 17u, 0.05f + 0.55f * rough}});
    // One field per patch terrain: broad for terrains that cover much of the map (jungle, roof),
    // finer for small patches (grass, mud).
    std::vector<LayeredNoise> patchField;
    for (size_t t = 0; t < nT; ++t) {
        const uint32_t ps = s * (uint32_t(t) * 7919u + 13u) + uint32_t(t);
        if (sub[t] > 0.1f)
            patchField.push_back(LayeredNoise({{p.landScale / 1000.0f * 1.2f, ps, 1.0f}, {p.coastline / 1000.0f, ps * 31u + 1u, 0.15f + 0.3f * rough}}));
        else
            patchField.push_back(LayeredNoise({{p.detail / 1000.0f * 0.3f, ps, 1.0f}, {p.detail / 1000.0f * 0.9f, ps * 31u + 1u, 0.3f}}));
    }

    struct Corner {
        int x, y;
        float land, massif;
        std::vector<float> f;
    };
    std::vector<Corner> corners;
    auto sample = [&](int x, int y) {
        // Corners on and beyond the map edge take the value from just inside, so no transition runs
        // along the border (border tiles are where StarEdit maps are least consistent).
        x = std::clamp(x, 1, cols - 1), y = std::clamp(y, 1, rows - 1);
        float tx = float(x * 2), ty = float(y);
        Corner c{x, y, land.at(tx, ty), massif.at(tx, ty), {}};
        c.f.resize(nT);
        for (size_t t = 0; t < nT; ++t) c.f[t] = patchField[t].at(tx, ty);
        return c;
    };
    // Mirror the fields before cutting (as PUDForge does): every corner takes the values of the first
    // corner of its symmetry orbit, so the cut terrain is symmetric at every threshold.
    auto canonical = [&](int x, int y, int& cx, int& cy) {
        cx = x, cy = y;
        auto consider = [&](int ox, int oy) {
            if (oy < cy || (oy == cy && ox < cx)) cx = ox, cy = oy;
        };
        const int mx = w / 2 - x, my = h - y;  // corner-grid mirrors (W px = 64 * w/2, H px = 32 * h)
        switch (p.symmetry) {
        case GenSymmetry::None: break;
        case GenSymmetry::LeftRight: consider(mx, y); break;
        case GenSymmetry::TopBottom: consider(x, my); break;
        case GenSymmetry::Both: consider(mx, y), consider(x, my), consider(mx, my); break;
        case GenSymmetry::Rotate180: consider(mx, my); break;
        }
    };
    for (int y = -1; y <= rows + 1; ++y)
        for (int x = -1; x <= cols + 1; ++x) {
            if (!grid.isCorner(x, y)) continue;
            int cx, cy;
            canonical(x, y, cx, cy);
            Corner c = sample(cx, cy);
            c.x = x, c.y = y;
            corners.push_back(std::move(c));
        }
    const double N = double(corners.size());
    // STARDRAFT_GEN_TRACE=1 prints the share of each terrain after every stage (for tuning).
    const bool trace = std::getenv("STARDRAFT_GEN_TRACE") != nullptr;
    auto stage = [&](const char* name, const std::vector<uint16_t>& v) {
        if (!trace) return;
        std::vector<int> n(nT, 0);
        for (const auto& c : corners) ++n[v[size_t(c.y + 1) * size_t(cols + 3) + size_t(c.x + 1)]];
        std::fprintf(stderr, "%-10s", name);
        for (const auto& g : terr) std::fprintf(stderr, " %s %.1f", g.name, 100.0 * n[g.id] / N);
        std::fprintf(stderr, "\n");
    };

    // Quantile cuts so the shares come out as asked whatever shape the noise took.
    auto quantile = [](std::vector<float> v, float f) {
        if (v.empty()) return 2.0f;
        f = std::clamp(f, 0.0f, 1.0f);
        size_t at = size_t(f * float(v.size() - 1));
        std::nth_element(v.begin(), v.begin() + long(at), v.end());
        return v[at];
    };

    const int gw = cols + 3, gh = rows + 3;  // corner array covering x, y in [-1, cols+1] x [-1, rows+1]
    auto at = [&](int x, int y) { return size_t(y + 1) * gw + size_t(x + 1); };
    static const int nb[8][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {2, 0}, {-2, 0}, {0, 2}, {0, -2}};
    std::vector<uint16_t> want(size_t(gw) * gh, 0);



    // Helpers used by every stage.
    const bool fine = p.minFeature < 1.0f;  // half-size features allowed: no whole-diamond rule
    auto wholeDiamond = [&](int x, int y, auto&& member) {
        if (fine) return true;
        const int centers[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
        for (const auto& d : centers) {
            const int pts[4][2] = {{d[0] - 1, d[1]}, {d[0] + 1, d[1]}, {d[0], d[1] - 1}, {d[0], d[1] + 1}};
            bool all = true;
            for (const auto& q : pts)
                if (!grid.isCorner(q[0], q[1]) || !member(want[at(q[0], q[1])])) all = false;
            if (all) return true;
        }
        return false;
    };
    // Terrain on top of another (high grass on high dirt, structure on asphalt, the high jungle family)
    // only `steps` diamond-sharing steps inside its parent's area, so a whole diamond of the parent rims
    // it as in Blizzard maps, and only in whole diamonds of itself; re-applied after every repair that
    // can narrow the parent.
    auto fitInner = [&](int steps) {
        if (fine) steps = 1;  // a one-corner rim
        for (int pass = 0; pass < steps; ++pass) {
            std::vector<uint16_t> next = want;
            for (const auto& c : corners) {
                uint16_t t = want[at(c.x, c.y)];
                if (t == base || info(t).parent == base) continue;
                const uint16_t par = info(t).parent;
                for (const auto& d : nb) {
                    int nx = c.x + d[0], ny = c.y + d[1];
                    uint16_t u = grid.isCorner(nx, ny) ? want[at(nx, ny)] : t;
                    if (!inSubtree(u, par)) next[at(c.x, c.y)] = par;
                }
            }
            want.swap(next);
        }
        for (int pass = 0; pass < 4; ++pass) {
            std::vector<uint16_t> next = want;
            bool changed = false;
            for (const auto& c : corners) {
                uint16_t t = want[at(c.x, c.y)];
                if (t == base || info(t).parent == base) continue;
                if (wholeDiamond(c.x, c.y, [&](uint16_t u) { return inSubtree(u, t); })) continue;
                next[at(c.x, c.y)] = info(t).parent, changed = true;
            }
            want.swap(next);
            if (!changed) break;
        }
    };
    // Make the wish list valid before handing it to the engine: corners sharing a diamond must be at
    // most one step apart in the terrain tree (then every diamond is a valid shape). Where two are
    // farther apart, the lower-priority one steps toward the other, so each patch keeps its core and
    // grows its transition ring outward instead of being eroded by its neighbors. Priority: sea, the
    // high ground and what sits on it, deeper patches before shallower, the base last; ids break ties.
    // Updates are taken from a snapshot per pass, so symmetric input stays symmetric.
    std::vector<int> prio(nT, 0);
    for (const auto& g : terr) {
        if (g.id == base) prio[g.id] = 0;
        else if (g.id == sea) prio[g.id] = 100000;
        else if (inHigh && inSubtree(g.id, high)) prio[g.id] = 50000 + g.depth * 100 + g.id;
        else prio[g.id] = 10000 + g.depth * 100 + g.id;
    }
    // The step from t toward `target` in the terrain table (the terrain next to t on the path), for legalize: a table.
    std::vector<uint16_t> stepToward(nT * nT, 0);
    for (const auto& a : terr)
        for (const auto& b : terr) {
            const auto path = table->path(a.id, b.id);  // a ... b
            if (path.size() >= 2) stepToward[size_t(a.id) * nT + b.id] = path[path.size() - 2];
        }
    // Each pass reads a snapshot and writes after it (so symmetric input stays symmetric). A corner can change only when
    // it or a diamond-sharing neighbor changed in the pass before, so after the first pass only those are checked: the
    // same result as checking every corner, in a fraction of the time.
    std::vector<uint32_t> seen(want.size(), 0);
    uint32_t seenStamp = 0;
    std::vector<std::pair<int, int>> todo, nextTodo;
    std::vector<std::pair<size_t, uint16_t>> writes;
    auto legalize = [&] {
        todo.clear();
        for (const auto& c : corners) todo.push_back({c.x, c.y});
        for (int pass = 0; pass < 32 && !todo.empty(); ++pass) {
            writes.clear();
            for (const auto& [cx, cy] : todo) {
                uint16_t t = want[at(cx, cy)], target = 0;
                for (const auto& d : nb) {
                    int nx = cx + d[0], ny = cy + d[1];
                    if (!grid.isCorner(nx, ny)) continue;
                    uint16_t u = want[at(nx, ny)];
                    if (prio[u] > prio[t] && table->distance(t, u) > 1 && (!target || prio[u] > prio[target])) target = u;
                }
                if (!target) continue;
                if (const uint16_t step = target < nT && t < nT ? stepToward[size_t(target) * nT + t] : 0) writes.push_back({at(cx, cy), step});
            }
            if (writes.empty()) break;
            // The next pass: the changed corners and their neighbors, each one time.
            ++seenStamp;
            nextTodo.clear();
            auto add = [&](int x, int y) {
                if (!grid.isCorner(x, y)) return;
                const size_t i = at(x, y);
                if (seen[i] == seenStamp) return;
                seen[i] = seenStamp;
                nextTodo.push_back({x, y});
            };
            for (const auto& [i, v] : writes) {
                want[i] = v;
                const int x = int(i % size_t(gw)) - 1, y = int(i / size_t(gw)) - 1;
                add(x, y);
                for (const auto& d : nb) add(x + d[0], y + d[1]);
            }
            todo.swap(nextTodo);
        }
    };
    // Then every corner must lie in a diamond made entirely of its terrain, as when StarEdit paints.
    // Rings that legalising grew are one corner wide; a ring corner around terrain that sits on it
    // (high grass on high dirt) counts that terrain as its own; otherwise it drops to its parent.
    // Repeat with legalising until both hold.
    auto settle = [&] {
        for (int round = 0; round < 8; ++round) {
            std::vector<uint16_t> next = want;
            bool changed = false;
            for (const auto& c : corners) {
                uint16_t t = want[at(c.x, c.y)];
                if (t == base) continue;
                if (wholeDiamond(c.x, c.y, [&](uint16_t u) { return u == t; })) continue;
                if (!children[t].empty() && wholeDiamond(c.x, c.y, [&](uint16_t u) { return inSubtree(u, t); })) continue;
                next[at(c.x, c.y)] = parentOf(t);
                changed = true;
            }
            want.swap(next);
            std::vector<uint16_t> before = want;
            fitInner(2);
            legalize();
            if (!changed && want == before) break;
        }
    };

    // One pass from noise to a valid wish list: sea and high ground, opening and closing, blocks and
    // patches, rims, legalising and settling. `factor` scales how much of each terrain is cut from
    // the noise, to make up for what rims and the whole-diamond rule take; it is calibrated below.
    double partMs[5] = {0, 0, 0, 0, 0};  // STARDRAFT_GEN_TIME: in shapeAll, up to the patches, patches, inner, legalize, settle
    auto partStart = std::chrono::steady_clock::now();
    auto part = [&](int k) {
        const auto now = std::chrono::steady_clock::now();
        partMs[k] += std::chrono::duration<double, std::milli>(now - partStart).count();
        partStart = now;
    };
    auto shapeAll = [&](const std::vector<double>& factor) {
        partStart = std::chrono::steady_clock::now();
        std::fill(want.begin(), want.end(), uint16_t(0));
        // Sea and high ground from the two broad fields.
        {
            std::vector<float> lv;
            for (const auto& c : corners) lv.push_back(c.land);
            const float seaCut = sea ? quantile(lv, std::min(0.95f, float(sub[sea] * factor[sea]))) : -1.0f;
            std::vector<float> mv;
            for (const auto& c : corners)
                if (c.land >= seaCut) mv.push_back(c.massif);
            const float landShare = std::max(0.05f, 1.0f - (sea ? sub[sea] : 0.0f));
            const float highCut = high ? quantile(mv, 1.0f - std::clamp(float(sub[high] * factor[high]) / landShare, 0.0f, 0.95f)) : 2.0f;
            for (const auto& c : corners) {
                uint16_t t = base;
                if (sea && c.land < seaCut) t = sea;
                else if (high && c.massif >= highCut) t = high;
                want[at(c.x, c.y)] = t;
            }
        }
        stage("cut", want);
        // StarEdit paints whole diamonds, so in Blizzard maps nearly every raised or wet corner belongs to
        // a diamond made entirely of its terrain (measured on Blizzard maps: badlands high dirt 89%, water
        // 95%). Noise cut per corner makes one-corner strips, spurs and pillars instead, which come out as
        // cliff fragments. Remove those (an opening on the diamond lattice), then fill one-corner gaps of
        // base ground pinched between features (a closing), a few times until stable.
        for (int pass = 0; pass < 4; ++pass) {
            bool changed = false;
            std::vector<uint16_t> next = want;
            for (const auto& c : corners) {
                uint16_t t = want[at(c.x, c.y)];
                if (t == base || wholeDiamond(c.x, c.y, [&](uint16_t u) { return top(u) == top(t); })) continue;
                next[at(c.x, c.y)] = base;
                changed = true;
            }
            want.swap(next);
            next = want;
            for (const auto& c : corners) {
                if (want[at(c.x, c.y)] != base || wholeDiamond(c.x, c.y, [&](uint16_t u) { return u == base; })) continue;
                // Pinched between features: join the sea or high ground around it, whichever there is more
                // of (sea on ties, it is the stronger edge).
                int nSea = 0, nHigh = 0;
                for (const auto& d : nb) {
                    if (!grid.isCorner(c.x + d[0], c.y + d[1])) continue;
                    uint16_t u = top(want[at(c.x + d[0], c.y + d[1])]);
                    nSea += sea && u == sea, nHigh += high && u == high;
                }
                if (nSea || nHigh) {
                    next[at(c.x, c.y)] = nSea >= nHigh ? sea : high;
                    changed = true;
                }
            }
            want.swap(next);
            if (!changed) break;
        }
        stage("opened", want);
        part(0);
        // Built terrain has straight diagonal edges: snap blocky sea and high ground to aligned blocks of
        // 2x2 diamonds (in the diamond lattice u = (x+y-1)/2, v = (x-y+1)/2, as a brush paints them).
        auto fdiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        auto latticeCorner = [](int u, int v) { return std::pair{u + v, u - v + 1}; };
        for (uint16_t t : {sea, high}) {
            if (!t || !info(t).blocky) continue;
            std::map<std::pair<int, int>, int> votes;  // block -> corners of t minus others
            for (const auto& c : corners) {
                int u = (c.x + c.y - 1) / 2, v = fdiv(c.x - c.y + 1, 2);
                votes[{fdiv(u, 2), fdiv(v, 2)}] += want[at(c.x, c.y)] == t ? 1 : -1;
            }
            for (const auto& c : corners)
                if (want[at(c.x, c.y)] == t) want[at(c.x, c.y)] = base;
            for (const auto& [blk, vote] : votes) {
                if (vote <= 0) continue;
                for (int du = 0; du <= 2; ++du)
                    for (int dv = 0; dv <= 2; ++dv) {
                        auto [x, y] = latticeCorner(blk.first * 2 + du, blk.second * 2 + dv);
                        if (grid.isCorner(x, y)) want[at(x, y)] = t;
                    }
            }
        }

        // Patches, parents before children: each takes the strongest corners of its own field among the
        // corners that are purely its parent (every diamond-sharing neighbor too), so a patch never
        // touches a sibling or another feature and keeps its parent around it. Patches of the high
        // ground's family are placed under the same rule inside the plateaus.
        // Placement order: parents before children; at one depth, blocks first (they need large clear
        // areas), then natural patches smaller first, so broad ones (jungle covers a third of a jungle map)
        // do not take all the open ground.
        std::vector<const GenTerrain*> patchOrder;
        for (const auto& g : terr)
            if (g.role == GenTerrain::Patch && sub[g.id] > 0) patchOrder.push_back(&g);
        std::stable_sort(patchOrder.begin(), patchOrder.end(), [&](const GenTerrain* a, const GenTerrain* b) {
            // Blocks first at every depth: they need large clear areas, and paint their own margin of the
            // terrain they sit on (structure brings its asphalt, a temple its jungle). Then natural patches,
            // parents before children, and at one depth the small ones first on clear ground; broad ones
            // (jungle covers a third of a jungle map) then fill in around them.
            auto rank = [&](const GenTerrain* g) { return g->blocky ? 0 : 1; };
            if (rank(a) != rank(b)) return rank(a) < rank(b);
            if (a->depth != b->depth) return a->depth < b->depth;
            return rank(a) == 0 ? sub[a->id] > sub[b->id] : sub[a->id] < sub[b->id];
        });
        // Small natural siblings (grass and water on ice dirt, mud and rocky ground on jungle dirt) share
        // their parent's clear ground: each corner goes to the sibling whose field is strongest there,
        // so the first one placed does not take it all.
        std::vector<std::vector<uint16_t>> smallSiblings(nT);
        for (const auto& g : terr)
            if (g.role == GenTerrain::Patch && !g.blocky && sub[g.id] > 0 && sub[g.id] <= 0.1f) smallSiblings[g.parent].push_back(g.id);
        for (const GenTerrain* gp : patchOrder) {
            const GenTerrain& g = *gp;
            const bool broad = sub[g.id] > 0.1f;
            // A built terrain that dominates its tileset (installation roof, half the map) is a region,
            // not blocks; the rest (asphalt, plating, temples) come as blocks.
            const bool region = g.blocky && sub[g.id] > 0.25f;
            if (!g.blocky || region) {
                // Natural patch: the strongest corners of its own field among its parent's corners. Small
                // patches only where every diamond-sharing neighbor is the parent too (so they never touch
                // a sibling); broad ones anywhere on the parent, legalising adds rings where they meet
                // others. How much to take is calibrated (see below). A broad built terrain (installation
                // roof, half the map) is such a region snapped to aligned 2x2-diamond blocks afterwards.
                std::vector<std::pair<float, size_t>> cand;
                for (size_t i = 0; i < corners.size(); ++i) {
                    const Corner& c = corners[i];
                    if (want[at(c.x, c.y)] != g.parent) continue;
                    bool pure = true;
                    for (const auto& d : nb)
                        if (!broad && grid.isCorner(c.x + d[0], c.y + d[1]) && want[at(c.x + d[0], c.y + d[1])] != g.parent) pure = false;
                    if (!broad && !g.blocky)
                        for (uint16_t sib : smallSiblings[g.parent]) {
                            // Weighted by share, so each sibling's part of the ground follows its target.
                            auto score = [&](uint16_t t) { return c.f[t] + 0.25f * std::log10(std::max(sub[t], 1e-4f)); };
                            if (sib != g.id && score(sib) > score(g.id)) pure = false;
                        }
                    if (pure) cand.push_back({c.f[g.id], i});
                }
                size_t k = std::min(cand.size(), size_t(sub[g.id] * N * factor[g.id]));
                if (trace) std::fprintf(stderr, "  patch %s: %zu candidates, take %zu (factor %.2f)\n", g.name, cand.size(), k, factor[g.id]);
                if (!k) continue;
                std::nth_element(cand.begin(), cand.begin() + long(k - 1), cand.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                for (size_t j = 0; j < k; ++j) want[at(corners[cand[j].second].x, corners[cand[j].second].y)] = g.id;
                if (g.blocky) {
                    std::map<std::pair<int, int>, int> votes;  // block -> corners of g minus parent corners
                    for (const auto& c : corners) {
                        uint16_t t = want[at(c.x, c.y)];
                        if (t != g.id && t != g.parent) continue;
                        int u = (c.x + c.y - 1) / 2, v = fdiv(c.x - c.y + 1, 2);
                        votes[{fdiv(u, 2), fdiv(v, 2)}] += t == g.id ? 1 : -1;
                    }
                    for (const auto& c : corners)
                        if (want[at(c.x, c.y)] == g.id) want[at(c.x, c.y)] = g.parent;
                    for (const auto& [blk, vote] : votes) {
                        if (vote <= 0) continue;
                        for (int du = 0; du <= 2; ++du)
                            for (int dv = 0; dv <= 2; ++dv) {
                                auto [x, y] = latticeCorner(blk.first * 2 + du, blk.second * 2 + dv);
                                if (grid.isCorner(x, y) && want[at(x, y)] == g.parent) want[at(x, y)] = g.id;
                            }
                    }
                }
                continue;
            }
            std::vector<uint8_t> taken(want.size(), 0);  // inside one of this terrain's blocks or its margin
            // Blocks of K x K diamonds with a margin of M corners painted as the parent (a whole diamond of
            // it around the block), centered on the strongest corners of the terrain's field first, wherever
            // block and margin lie on the parent or the ground it sits on, until the share is placed.
            // Terrain nested deeper (a high temple on high jungle) gets smaller blocks: its parent is
            // itself a patch with little room.
            const int K = fine ? 1 : g.depth >= 2 ? 2 : 3, M = fine ? 1 : 2;
            const uint16_t par = g.parent, grand = parentOf(g.parent);
            std::vector<std::pair<float, size_t>> seeds;
            for (size_t i = 0; i < corners.size(); ++i) {
                uint16_t t = want[at(corners[i].x, corners[i].y)];
                if (t == par || t == grand) seeds.push_back({corners[i].f[g.id], i});
            }
            std::sort(seeds.begin(), seeds.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            const size_t quota = size_t(sub[g.id] * N * factor[g.id]);
            size_t placed = 0;
            for (const auto& sd : seeds) {
                if (placed >= quota) break;
                const Corner& c = corners[sd.second];
                if (taken[at(c.x, c.y)]) continue;
                int u0 = (c.x + c.y - 1) / 2 - K / 2, v0 = fdiv(c.x - c.y + 1, 2) - K / 2;
                bool fits = true;
                for (int du = -M; du <= K + M && fits; ++du)
                    for (int dv = -M; dv <= K + M && fits; ++dv) {
                        auto [x, y] = latticeCorner(u0 + du, v0 + dv);
                        if (!grid.isCorner(x, y) || taken[at(x, y)]) { fits = false; break; }
                        uint16_t t = want[at(x, y)];
                        if (t != par && t != grand) fits = false;
                    }
                if (!fits) continue;
                for (int du = -M; du <= K + M; ++du)
                    for (int dv = -M; dv <= K + M; ++dv) {
                        auto [x, y] = latticeCorner(u0 + du, v0 + dv);
                        size_t i = at(x, y);
                        if (du >= 0 && du <= K && dv >= 0 && dv <= K) want[i] = g.id, ++placed;
                        else want[i] = par;
                        taken[i] = 1;
                    }
            }
        }
        stage("patches", want);
        part(1);
        fitInner(3);
        stage("inner", want);
        part(2);
        legalize();
        part(3);
        settle();
        part(4);
        stage("settled", want);
    };
    // Calibrate: start from 1.3 (2.0 for small patches), shape, compare each terrain's share (with what
    // sits on it) with the target, rescale its factor, and shape again. A few rounds bring every
    // tileset's shares close to the request without per-tileset constants.
    lap("fields");
    std::vector<double> factor(nT, 1.3);
    for (const auto& g : terr)
        if (g.role == GenTerrain::Patch && sub[g.id] <= 0.1f) factor[g.id] = 2.0;
    for (int round = 0; round < 4; ++round) {
        shapeAll(factor);
        if (round == 3) break;
        std::vector<double> got(nT, 0.0);
        for (const auto& c : corners) {
            for (uint16_t t = want[at(c.x, c.y)]; t; t = t == base ? 0 : info(t).parent) got[t] += 1.0 / N;
        }
        for (const auto& g : terr) {
            if (g.id == base || sub[g.id] <= 0) continue;
            double ratio = got[g.id] > 0 ? sub[g.id] / got[g.id] : 2.0;
            factor[g.id] = std::clamp(factor[g.id] * std::clamp(ratio, 0.5, 2.0), 0.3, 8.0);
        }
    }

    lap("calibrate");
    if (timing)
        std::fprintf(stderr, "    (open %.1f, patches %.1f, inner %.1f, legalize %.1f, settle %.1f ms)\n", partMs[0], partMs[1], partMs[2], partMs[3], partMs[4]);
    // Apply like brush strokes. Each stroke's repairs (rects StarEdit has no tile for) push toward the
    // terrain being painted, so the order matters: the ground first (the base and the patches that lie
    // directly on it with nothing on top), then each other terrain with everything on it, parents
    // before children, the sea last (it wins at shores, its shapes are the most constrained). One
    // stroke with every corner locked would leave the repairs no painted terrain to favor.
    std::map<std::pair<int, int>, uint16_t> intended;  // terrain asked for, per corner
    auto groundLeaf = [&](uint16_t t) { return t == base || (info(t).parent == base && children[t].empty() && t != sea && t != high); };
    std::vector<uint16_t> strokeOrder;
    if (high) strokeOrder.push_back(high);
    for (const auto& g : terr)
        if (g.id != high && g.id != sea && !groundLeaf(g.id)) strokeOrder.push_back(g.id);
    if (high)  // the high family right after the high ground, as before
        std::stable_partition(strokeOrder.begin(), strokeOrder.end(), [&](uint16_t t) { return inSubtree(t, high); });
    if (sea) strokeOrder.push_back(sea);
    auto paint = [&] {
        std::vector<CornerValue> ground;
        for (const auto& c : corners) {
            uint16_t t = want[at(c.x, c.y)];
            ground.push_back({c.x, c.y, groundLeaf(t) ? t : base});
        }
        grid.apply(ground);
        for (uint16_t s0 : strokeOrder) {
            std::vector<CornerValue> layer;
            for (const auto& c : corners)
                if (inSubtree(want[at(c.x, c.y)], s0)) layer.push_back({c.x, c.y, s0});
            if (!layer.empty()) grid.apply(layer);
        }
    };
    for (const auto& c : corners) intended[{c.x, c.y}] = want[at(c.x, c.y)];
    paint();
    lap("paint");
    // The engine's repairs (rects Blizzard's maps have no tile for, e.g. a shore too close to a cliff)
    // can narrow what the rules above guarantee. Take its result back, re-apply them, paint again.
    for (int round = 0; round < 3; ++round) {
        for (const auto& c : corners) want[at(c.x, c.y)] = grid.terrainAt(c.x, c.y);
        const std::vector<uint16_t> engine = want;
        fitInner(2);
        settle();
        if (want == engine) break;
        for (const auto& c : corners) intended[{c.x, c.y}] = want[at(c.x, c.y)];
        paint();
    }
    {
        std::vector<uint16_t> fin(want.size(), 0);
        for (const auto& c : corners) fin[at(c.x, c.y)] = grid.terrainAt(c.x, c.y);
        stage("engine", fin);
    }
    lap("repairs");
    // Mirror symmetry: tile repairs (ring widths, cliff stacks; partly in store()) are not
    // mirror-symmetric because the cliff art is not, so alternate store + mirror until the halves
    // agree. The axis falls on corner points (width and height divisible by 4), so the seam stays valid.
    if (!grid.store(chk, error)) return false;
    for (int pass = 0; pass < 12 && p.symmetry != GenSymmetry::None; ++pass) {
        // For each corner that differs from its canonical twin, keep whichever side a repair changed
        // (the repair was needed there, so it is needed on both sides); otherwise the canonical value.
        std::vector<CornerValue> fix;
        for (const auto& c : corners) {
            int cx, cy;
            canonical(c.x, c.y, cx, cy);
            if ((cx == c.x && cy == c.y) || !grid.isCorner(cx, cy)) continue;
            uint16_t a = grid.terrainAt(cx, cy), b = grid.terrainAt(c.x, c.y);
            if (a == b) continue;
            bool aRepaired = a != intended[{cx, cy}], bRepaired = b != intended[{c.x, c.y}];
            uint16_t v = (bRepaired && !aRepaired) ? b : a;
            fix.push_back({c.x, c.y, v});
            fix.push_back({cx, cy, v});
        }
        if (fix.empty()) break;
        grid.apply(fix);
        if (!grid.store(chk, error)) return false;
    }
    lap("store");
    return true;
}

}  // namespace stardraft
