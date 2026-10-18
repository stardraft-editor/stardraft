// The pathing check (pathing_regions.h): the nooks and crannies of a map's ground.
//
// The ground is the walkable minitiles of the game's regions (step 1). With w the walkable minitiles:
//   - Pocket: 4-connected walkable ground with no start location in it and fewer than `pocketMinitiles` minitiles
//     (when the map has no start location: every such ground but the largest).
//   - Gap: a small unit (16 x 16 px, 2 x 2 minitiles) stands only where a walkable 2 x 2 square covers it. Two ground
//     areas of that space (each `areaMinitiles` or more) that meet only through walkable minitiles where no unit can
//     stand, while the game's regions put them in one connected area: units try to walk through and stop.
//   - SmallUnitsOnly: the same for a medium unit (32 x 32 px, 4 x 4 minitiles): two of its ground areas that a small
//     unit can walk between (a passage 2 or 3 minitiles wide).
//   - ClosedPassage: a ground area of small units' space whose minitiles are in more than one connected area of the
//     game: each connected area but the one with the most minitiles there, with `closedMinitiles` or more (units can
//     walk into it, but the game finds no path into it or out of it).
//   - SmallRegion: a walkable region with fewer than `smallRegionMinitiles` walkable minitiles (not in a Pocket).

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <deque>
#include <map>

#include "stardraft/pathing_regions.h"

namespace stardraft {

namespace {

constexpr uint16_t kStartLocationType = 214;

// 4-connected components of the cells where `in` is set: the label of each cell (-1 none) and each label's size.
void components(int w, int h, const std::vector<uint8_t>& in, std::vector<int32_t>& label, std::vector<int>& size) {
    label.assign(size_t(w) * h, -1);
    size.clear();
    std::vector<int> stack;
    for (int s = 0; s < w * h; ++s) {
        if (!in[size_t(s)] || label[size_t(s)] >= 0) continue;
        const int id = int(size.size());
        int n = 0;
        label[size_t(s)] = id;
        stack.push_back(s);
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            ++n;
            const int x = c % w, y = c / w;
            const int nb[4] = {x > 0 ? c - 1 : -1, x + 1 < w ? c + 1 : -1, y > 0 ? c - w : -1, y + 1 < h ? c + w : -1};
            for (int m : nb)
                if (m >= 0 && in[size_t(m)] && label[size_t(m)] < 0) label[size_t(m)] = id, stack.push_back(m);
        }
        size.push_back(n);
    }
}

// The cells that a walkable k x k square covers.
std::vector<uint8_t> covered(int w, int h, const std::vector<uint8_t>& walk, int k) {
    // clear: the side of the largest walkable square with its top-left at the cell (at most k).
    std::vector<uint8_t> clear(size_t(w) * h, 0), out(size_t(w) * h, 0);
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            const size_t i = size_t(y) * w + x;
            if (!walk[i]) continue;
            const int r = x + 1 < w ? clear[i + 1] : 0, d = y + 1 < h ? clear[i + size_t(w)] : 0, rd = x + 1 < w && y + 1 < h ? clear[i + size_t(w) + 1] : 0;
            clear[i] = uint8_t(std::min(k, 1 + std::min({r, d, rd})));
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (clear[size_t(y) * w + x] >= k)
                for (int j = 0; j < k; ++j) std::fill_n(out.begin() + std::ptrdiff_t(size_t(y + j) * w + x), k, uint8_t(1));
    return out;
}

// Where big areas of `space` meet through the cells of `through` (and the cells of the small areas of `space`): for
// each pair of big areas, the first cell (of `through`) where they meet.
std::map<std::pair<int, int>, int> meetings(int w, int h, const std::vector<int32_t>& label, const std::vector<int>& size, int big,
                                            const std::vector<uint8_t>& through) {
    std::vector<int32_t> from(size_t(w) * h, -1);
    std::deque<int> queue;
    for (int c = 0; c < w * h; ++c)
        if (label[size_t(c)] >= 0 && size[size_t(label[size_t(c)])] >= big) from[size_t(c)] = label[size_t(c)], queue.push_back(c);
    std::map<std::pair<int, int>, int> out;
    while (!queue.empty()) {
        const int c = queue.front();
        queue.pop_front();
        const int a = from[size_t(c)];
        const int x = c % w, y = c / w;
        const int nb[4] = {x > 0 ? c - 1 : -1, x + 1 < w ? c + 1 : -1, y > 0 ? c - w : -1, y + 1 < h ? c + w : -1};
        for (int m : nb) {
            if (m < 0) continue;
            const int b = from[size_t(m)];
            if (b >= 0) {
                if (b != a) {
                    const auto key = std::minmax(a, b);
                    // The cell of the passage (not a cell of a big area).
                    const bool cBig = label[size_t(c)] >= 0 && size[size_t(label[size_t(c)])] >= big;
                    out.emplace(key, cBig ? m : c);
                }
                continue;
            }
            if (!through[size_t(m)] && label[size_t(m)] < 0) continue;
            from[size_t(m)] = a;
            queue.push_back(m);
        }
    }
    return out;
}

// The cells of `in` connected to `start` (4-connected), at most `most`.
std::vector<int> floodFrom(int w, int h, int start, const std::vector<uint8_t>& in, size_t most) {
    std::vector<int> out;
    if (start < 0 || !in[size_t(start)]) return out;
    std::vector<uint8_t> seen(size_t(w) * h, 0);
    std::deque<int> queue{start};
    seen[size_t(start)] = 1;
    while (!queue.empty() && out.size() < most) {
        const int c = queue.front();
        queue.pop_front();
        out.push_back(c);
        const int x = c % w, y = c / w;
        const int nb[4] = {x > 0 ? c - 1 : -1, x + 1 < w ? c + 1 : -1, y > 0 ? c - w : -1, y + 1 < h ? c + w : -1};
        for (int m : nb)
            if (m >= 0 && in[size_t(m)] && !seen[size_t(m)]) seen[size_t(m)] = 1, queue.push_back(m);
    }
    return out;
}

void place(PathingProblem& p, int w, std::vector<int> cells, int at = -1) {
    std::sort(cells.begin(), cells.end());
    p.x0 = p.y0 = INT_MAX, p.x1 = p.y1 = INT_MIN;
    for (int c : cells) {
        const int x = c % w, y = c / w;
        p.x0 = std::min(p.x0, x), p.y0 = std::min(p.y0, y), p.x1 = std::max(p.x1, x + 1), p.y1 = std::max(p.y1, y + 1);
    }
    if (cells.empty()) p.x0 = p.y0 = p.x1 = p.y1 = 0;
    if (at < 0 && !cells.empty()) at = cells[cells.size() / 2];
    if (at >= 0) p.x = (at % w) * 8 + 4, p.y = (at / w) * 8 + 4;
    p.cells = std::move(cells);
}

}  // namespace

const char* pathingProblemName(PathingProblemKind kind) {
    switch (kind) {
    case PathingProblemKind::TooManyRegions: return "too many regions";
    case PathingProblemKind::Pocket: return "pocket";
    case PathingProblemKind::Gap: return "gap";
    case PathingProblemKind::SmallUnitsOnly: return "small units only";
    case PathingProblemKind::ClosedPassage: return "closed passage";
    case PathingProblemKind::SmallRegion: return "small region";
    }
    return "";
}

bool findUnwalkableTile(const TerrainLayer& terrain, const Tileset& ts, int tx, int ty, const std::vector<int>& cells, uint16_t& value) {
    if (tx < 0 || ty < 0 || tx >= terrain.width || ty >= terrain.height) return false;
    const int ww = terrain.width * 4;
    const uint16_t now = terrain.tiles[size_t(ty) * terrain.width + tx];
    const TileGroup* group = ts.groupFor(now);
    const int mega = ts.megatileFor(now);
    if (!group || mega < 0 || size_t(mega) >= ts.minitileFlags.size()) return false;
    uint16_t want = 0;  // bit k: minitile k is walkable after the fix
    for (int k = 0; k < 16; ++k)
        if (ts.minitileFlags[size_t(mega)][size_t(k)] & Tileset::kWalkable) want |= uint16_t(1u << k);
    for (int c : cells) {
        const int x = c % ww - tx * 4, y = c / ww - ty * 4;
        if (x < 0 || y < 0 || x > 3 || y > 3) return false;  // (a cell of another tile)
        want &= uint16_t(~(1u << (y * 4 + x)));
    }
    auto walkBits = [&](int m) {
        uint16_t b = 0;
        for (int k = 0; k < 16; ++k)
            if (ts.minitileFlags[size_t(m)][size_t(k)] & Tileset::kWalkable) b |= uint16_t(1u << k);
        return b;
    };
    auto tryGroup = [&](size_t g) {
        for (int s = 0; s < 16; ++s) {
            const int m = ts.groups[g].megatiles[size_t(s)];
            if (m <= 0 || size_t(m) >= ts.minitileFlags.size() || size_t(m) >= ts.megatiles.size() || m == mega) continue;
            if (walkBits(m) == want) return value = uint16_t((g << 4) | size_t(s)), true;
        }
        return false;
    };
    if (tryGroup(size_t(now >> 4))) return true;
    // The nearest tile of the map (at most 8 tiles away) whose minitiles are walkable so.
    for (int r = 1; r <= 8; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const int x = tx + dx, y = ty + dy;
                if (x < 0 || y < 0 || x >= terrain.width || y >= terrain.height) continue;
                const uint16_t v = terrain.tiles[size_t(y) * terrain.width + x];
                const int m = ts.megatileFor(v);
                if (m > 0 && size_t(m) < ts.minitileFlags.size() && walkBits(m) == want) return value = v, true;
            }
    for (int pass = 0; pass < 2; ++pass)  // the same terrain type and edges, then the same edges
        for (size_t g = 0; g < ts.groups.size() && g < 4096; ++g) {
            const TileGroup& o = ts.groups[g];
            if (g == size_t(now >> 4) || o.edges != group->edges || (pass == 0 && o.type != group->type)) continue;
            if (tryGroup(g)) return true;
        }
    return false;
}

PathingCheck checkPathing(const PathingRegions& R, const TerrainLayer& terrain, const Tileset& tileset, const std::vector<Unit>& units,
                          const PathingCheckOptions& opt) {
    PathingCheck out;
    const int w = R.walkW(), h = R.walkH();
    if (w <= 0 || h <= 0) return out;
    std::vector<PathingProblem> pockets, gaps, narrow, closed, small;
    if (R.tooMany) {
        PathingProblem p;
        p.kind = PathingProblemKind::TooManyRegions;
        p.x = w * 4, p.y = h * 4, p.x1 = w, p.y1 = h;
        out.problems.push_back(p);
    }

    // The ground and its parts.
    std::vector<int32_t> ground;
    std::vector<int> groundSize;
    components(w, h, R.walk, ground, groundSize);
    std::vector<uint8_t> hasStart(groundSize.size(), 0);
    bool anyStart = false;
    for (const Unit& u : units) {
        if (u.type != kStartLocationType) continue;
        const int cx = u.x / 8, cy = u.y / 8;
        // The walkable minitile nearest the start location (a town hall is 4 x 3 tiles).
        int best = -1, bestD = INT_MAX;
        for (int dy = -8; dy <= 8; ++dy)
            for (int dx = -8; dx <= 8; ++dx) {
                const int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= w || y >= h || ground[size_t(y) * w + x] < 0) continue;
                if (dx * dx + dy * dy < bestD) bestD = dx * dx + dy * dy, best = ground[size_t(y) * w + x];
            }
        if (best >= 0) hasStart[size_t(best)] = 1, anyStart = true;
    }
    int largest = -1;
    for (size_t i = 0; i < groundSize.size(); ++i)
        if (largest < 0 || groundSize[i] > groundSize[size_t(largest)]) largest = int(i);
    if (!anyStart && largest >= 0) hasStart[size_t(largest)] = 1;

    // Small units' and medium units' space.
    const std::vector<uint8_t> o2 = covered(w, h, R.walk, 2), o4 = covered(w, h, R.walk, 4);

    // Pockets.
    std::vector<uint8_t> isPocket(groundSize.size(), 0);
    {
        std::vector<std::vector<int>> cells(groundSize.size());
        for (int c = 0; c < w * h; ++c) {
            const int g = ground[size_t(c)];
            if (g >= 0 && !hasStart[size_t(g)] && groundSize[size_t(g)] < opt.pocketMinitiles) cells[size_t(g)].push_back(c);
        }
        for (size_t g = 0; g < cells.size(); ++g) {
            if (cells[g].empty()) continue;
            isPocket[g] = 1;
            PathingProblem p;
            p.kind = PathingProblemKind::Pocket;
            p.minitiles = int(cells[g].size());
            for (int c : cells[g]) p.fitsUnit |= o2[size_t(c)] != 0;
            place(p, w, cells[g]);
            // The fix: one tile with no walkable minitile where the pocket is.
            if (p.x1 - p.x0 <= 4 && p.y1 - p.y0 <= 4 && p.x0 / 4 == (p.x1 - 1) / 4 && p.y0 / 4 == (p.y1 - 1) / 4) {
                uint16_t v = 0;
                if (findUnwalkableTile(terrain, tileset, p.x0 / 4, p.y0 / 4, p.cells, v)) p.fixTileX = p.x0 / 4, p.fixTileY = p.y0 / 4, p.fixValue = v;
            }
            pockets.push_back(std::move(p));
        }
    }

    // The group (connected area) of each ground area of `label`: the game's group of its first minitile in a walkable
    // region (-1 none).
    auto groupsOf = [&](const std::vector<int32_t>& label, size_t count) {
        std::vector<int> g(count, -1);
        for (int c = 0; c < w * h; ++c) {
            const int id = label[size_t(c)];
            if (id < 0 || g[size_t(id)] >= 0) continue;
            const int r = R.regionAt(c % w, c / w);
            if (r >= 0 && R.regions[size_t(r)].walkable()) g[size_t(id)] = R.regions[size_t(r)].group;
        }
        return g;
    };

    // Gaps: small units' areas that meet through minitiles where no unit stands.
    {
        std::vector<int32_t> label;
        std::vector<int> size;
        components(w, h, o2, label, size);
        std::vector<uint8_t> sliver(size_t(w) * h, 0);
        for (size_t i = 0; i < sliver.size(); ++i) sliver[i] = R.walk[i] && !o2[i];
        const std::vector<int> group = groupsOf(label, size.size());
        for (const auto& [pair, cell] : meetings(w, h, label, size, opt.areaMinitiles, sliver)) {
            const int ga = group[size_t(pair.first)], gb = group[size_t(pair.second)];
            if (ga < 0 || ga != gb) continue;  // (the game's regions do not join them: units do not try)
            PathingProblem p;
            p.kind = PathingProblemKind::Gap;
            std::vector<int> cells = floodFrom(w, h, cell, sliver, 64);
            p.minitiles = int(cells.size());
            place(p, w, cells, cell);
            gaps.push_back(std::move(p));
        }
    }

    // Passages for small units only.
    {
        std::vector<int32_t> label;
        std::vector<int> size;
        components(w, h, o4, label, size);
        std::vector<uint8_t> passage(size_t(w) * h, 0);
        for (size_t i = 0; i < passage.size(); ++i) passage[i] = o2[i] && !o4[i];
        for (const auto& [pair, cell] : meetings(w, h, label, size, opt.areaMinitiles, passage)) {
            PathingProblem p;
            p.kind = PathingProblemKind::SmallUnitsOnly;
            std::vector<int> cells = floodFrom(w, h, cell, passage, 64);
            p.minitiles = int(cells.size());
            place(p, w, cells, cell);
            narrow.push_back(std::move(p));
        }
    }

    // Closed passages: small units' space across two connected areas of the game.
    {
        std::vector<int> groupAt(size_t(w) * h, -1);
        for (int c = 0; c < w * h; ++c) {
            if (!o2[size_t(c)]) continue;
            const int r = R.regionAt(c % w, c / w);
            if (r >= 0 && R.regions[size_t(r)].walkable()) groupAt[size_t(c)] = R.regions[size_t(r)].group;
        }
        // Each ground area of small units' space: its minitiles in each connected area of the game. The connected
        // area with the most is the area's own; each other one with enough minitiles is a closed passage.
        std::vector<int32_t> label;
        std::vector<int> size;
        components(w, h, o2, label, size);
        // (first the areas with more than one connected area: only their minitiles are listed)
        std::vector<int> firstGroup(size.size(), -1);
        std::vector<uint8_t> several(size.size(), 0);
        for (int c = 0; c < w * h; ++c) {
            const int l = label[size_t(c)], g = groupAt[size_t(c)];
            if (l < 0 || g < 0) continue;
            if (firstGroup[size_t(l)] < 0) firstGroup[size_t(l)] = g;
            else if (firstGroup[size_t(l)] != g) several[size_t(l)] = 1;
        }
        std::vector<std::map<int, std::vector<int>>> parts(size.size());
        for (int c = 0; c < w * h; ++c)
            if (label[size_t(c)] >= 0 && groupAt[size_t(c)] >= 0 && several[size_t(label[size_t(c)])])
                parts[size_t(label[size_t(c)])][groupAt[size_t(c)]].push_back(c);
        for (auto& byGroup : parts) {
            if (byGroup.size() < 2) continue;
            int own = -1;
            size_t most = 0;
            for (const auto& [g, cells] : byGroup)
                if (cells.size() > most) most = cells.size(), own = g;
            for (auto& [g, cells] : byGroup) {
                if (g == own || int(cells.size()) < opt.closedMinitiles) continue;
                // The place: a minitile of it beside the area's own connected area.
                int at = -1;
                for (int c : cells) {
                    const int x = c % w, y = c / w;
                    for (int m : {x > 0 ? c - 1 : -1, x + 1 < w ? c + 1 : -1, y > 0 ? c - w : -1, y + 1 < h ? c + w : -1})
                        if (m >= 0 && groupAt[size_t(m)] == own) at = c;
                    if (at >= 0) break;
                }
                PathingProblem p;
                p.kind = PathingProblemKind::ClosedPassage;
                p.minitiles = int(cells.size());
                place(p, w, std::move(cells), at);
                closed.push_back(std::move(p));
            }
        }
    }

    // Small regions.
    for (size_t r = 1; r < R.regions.size(); ++r) {
        const PathingRegion& reg = R.regions[r];
        if (!reg.walkable() || reg.minitiles >= opt.smallRegionMinitiles) continue;
        std::vector<int> cells;
        bool inPocket = false;
        for (int ty = reg.y0; ty < reg.y1; ++ty)
            for (int tx = reg.x0; tx < reg.x1; ++tx)
                for (int k = 0; k < 16; ++k) {
                    const int mx = tx * 4 + k % 4, my = ty * 4 + k / 4;
                    if (R.regionAt(mx, my) != int(r)) continue;
                    const int c = my * w + mx;
                    if (R.walk[size_t(c)]) {
                        cells.push_back(c);
                        if (ground[size_t(c)] >= 0 && isPocket[size_t(ground[size_t(c)])]) inPocket = true;
                    }
                }
        if (inPocket || cells.empty()) continue;
        PathingProblem p;
        p.kind = PathingProblemKind::SmallRegion;
        p.region = int(r);
        p.minitiles = int(cells.size());
        place(p, w, cells);
        small.push_back(std::move(p));
    }

    out.pockets = int(pockets.size()), out.gaps = int(gaps.size()), out.narrow = int(narrow.size()), out.closed = int(closed.size()), out.smallRegions = int(small.size());
    auto byPlace = [](const PathingProblem& a, const PathingProblem& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; };
    for (auto* list : {&pockets, &gaps, &narrow, &closed, &small}) {
        std::stable_sort(list->begin(), list->end(), byPlace);
        for (PathingProblem& p : *list) {
            if (out.problems.size() >= opt.most) {
                ++out.more;
                continue;
            }
            if (p.cells.size() > 4096) p.cells.resize(4096);
            out.problems.push_back(std::move(p));
        }
    }
    return out;
}

}  // namespace stardraft
