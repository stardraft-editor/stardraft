// The game's ground pathing regions (see pathing_regions.h for the steps and where they differ from the game).

#include "stardraft/pathing_regions.h"

#include <algorithm>
#include <chrono>
#include <climits>

namespace stardraft {

namespace {

// The values of a tile's region index before it has a region (the game's).
constexpr int kUnwalkable = 0x1ffd, kLow = 0x1ffb, kMiddle = 0x1ff9, kHigh = 0x1ffa;
constexpr int kOutside = 0x1fff;  // a neighbor outside the map
constexpr int kMaxRegions = 5000;
constexpr int kSplitBase = 0x2000;
constexpr int kConsoleGroup = 0x4000;

// Tile flags from the VF4 flags of its megatile (the game's mega tile flags), and the console's rows.
constexpr uint8_t kTileWalkable = 1, kTilePartial = 2, kTileMiddle = 4, kTileHigh = 8;

struct Region {
    uint16_t flags = kUnwalkable;
    int index = 0;
    int tx0 = 0, ty0 = 0, tx1 = 0, ty1 = 0;  // the rectangle it was made from (the growth widens it)
    int cx = 0, cy = 0;                      // the center tile
    int ax0 = INT_MAX, ay0 = INT_MAX, ax1 = INT_MIN, ay1 = INT_MIN;  // the box of its tiles
    int count = 0;
    int group = 0;
    std::vector<int> walkableNeighbors, otherNeighbors;
    bool walkable() const { return flags != kUnwalkable; }
};

class Builder {
public:
    Builder(const TerrainLayer& ter, const Tileset& ts) : ter_(ter), ts_(ts) {}

    void run(PathingRegions& out) {
        w_ = ter_.width, h_ = ter_.height, ww_ = w_ * 4, wh_ = h_ * 4;
        if (w_ <= 0 || h_ <= 0 || ter_.tiles.size() < size_t(w_) * h_) return;
        tileFlags();
        unwalkableMinitiles();
        idx_.assign(size_t(w_) * h_, 0);
        for (int i = 0; i < w_ * h_; ++i) {
            const uint8_t f = flags_[size_t(i)];
            idx_[size_t(i)] = !(f & kTileWalkable) ? kUnwalkable : (f & kTileMiddle) ? kMiddle : (f & kTileHigh) ? kHigh : kLow;
        }
        // The console's region: the last row, unwalkable, apart from every group.
        Region& bottom = create(0, h_ - 1, w_, h_);
        bottom.flags = kUnwalkable, bottom.group = kConsoleGroup;
        createRegions();
        out.firstRegions = int(rs_.size());
        out.tooMany = tooMany_;
        refresh();
        mergeSmall();
        compact();
        refresh();
        splitTiles();
        // The result.
        out.width = w_, out.height = h_;
        out.walk.assign(size_t(ww_) * wh_, 0);
        for (size_t i = 0; i < out.walk.size(); ++i) out.walk[i] = unwalk_[i] ? 0 : 1;
        out.tile.resize(idx_.size());
        // (A tile left with its kind has no region: only when step 3 stopped at 5000 regions.)
        for (size_t i = 0; i < idx_.size(); ++i)
            out.tile[i] = idx_[i] >= kSplitBase ? -1 - (idx_[i] - kSplitBase) : idx_[i] < kMaxRegions ? idx_[i] : PathingRegions::kNoRegion;
        out.splits = splits_;
        out.regions.resize(rs_.size());
        int groups = 0;
        for (size_t i = 0; i < rs_.size(); ++i) {
            const Region& r = rs_[i];
            PathingRegion& o = out.regions[i];
            o.flags = r.flags, o.tiles = r.count, o.group = r.group;
            o.x0 = r.ax0, o.y0 = r.ay0, o.x1 = r.ax1, o.y1 = r.ay1;
            o.centerX = r.cx, o.centerY = r.cy;
            o.neighbors = r.walkable() ? r.walkableNeighbors : r.otherNeighbors;
            if (r.group < kConsoleGroup) groups = std::max(groups, r.group);
        }
        out.groups = groups;
        for (int my = 0; my < wh_; ++my)
            for (int mx = 0; mx < ww_; ++mx) {
                const int r = out.regionAt(mx, my);
                if (r >= 0 && size_t(r) < out.regions.size()) ++out.regions[size_t(r)].minitiles;
            }
    }

private:
    const TerrainLayer& ter_;
    const Tileset& ts_;
    int w_ = 0, h_ = 0, ww_ = 0, wh_ = 0;
    std::vector<int> mega_;        // per tile: its megatile, -1 none
    std::vector<uint8_t> flags_;   // per tile: kTile*
    std::vector<uint8_t> unwalk_;  // per minitile: 1 unwalkable
    std::vector<int> idx_;         // per tile: the region, a kind (>= 5000) or a split (>= 0x2000)
    std::vector<Region> rs_;
    std::vector<PathingRegions::Split> splits_;
    bool tooMany_ = false;

    const std::array<uint16_t, 16>* vf4(int tileIndex) const {
        const int m = mega_[size_t(tileIndex)];
        return m >= 0 && size_t(m) < ts_.minitileFlags.size() ? &ts_.minitileFlags[size_t(m)] : nullptr;
    }
    bool walkable(int mx, int my) const { return !unwalk_[size_t(my) * ww_ + mx]; }

    void tileFlags() {
        mega_.assign(size_t(w_) * h_, -1);
        flags_.assign(size_t(w_) * h_, 0);
        for (int i = 0; i < w_ * h_; ++i) {
            uint16_t v = ter_.tiles[size_t(i)];
            if (size_t(v >> 4) >= ts_.groups.size()) v = 0;  // (the game takes tile 0 for a group it has not)
            if (ts_.groups.empty()) continue;
            mega_[size_t(i)] = ts_.groups[v >> 4].megatiles[v & 15];
            const auto* f = vf4(i);
            int walk = 0, middle = 0, high = 0;
            if (f)
                for (uint16_t m : *f) walk += m & 1, middle += (m >> 1) & 1, high += (m >> 2) & 1;
            uint8_t t = 0;
            if (walk > 12) t |= kTileWalkable;
            if (walk && walk != 16) t |= kTilePartial;
            if (high < 12 && middle + high >= 12) t |= kTileMiddle;
            if (high >= 12) t |= kTileHigh;
            flags_[size_t(i)] = t;
        }
        // The console covers the bottom: 5 tiles at each end of the row above the last, and all of the last row.
        auto clear = [&](int x0, int y, int n) {
            for (int x = std::max(0, x0); x < std::min(w_, x0 + n); ++x) flags_[size_t(y) * w_ + x] &= uint8_t(~(kTileWalkable | kTilePartial));
        };
        if (h_ >= 2) clear(0, h_ - 2, 5), clear(w_ - 5, h_ - 2, 5);
        clear(0, h_ - 1, w_);
    }

    void unwalkableMinitiles() {
        unwalk_.assign(size_t(ww_) * wh_, 0);
        for (int ty = 0; ty < h_; ++ty)
            for (int tx = 0; tx < w_; ++tx) {
                const auto* f = vf4(ty * w_ + tx);
                for (int k = 0; k < 16; ++k)
                    if (!f || !((*f)[size_t(k)] & Tileset::kWalkable)) unwalk_[size_t(ty * 4 + k / 4) * ww_ + tx * 4 + k % 4] = 1;
            }
        if (wh_ >= 8)
            for (int y = wh_ - 8; y < wh_; ++y) {
                for (int x = 0; x < std::min(20, ww_); ++x) unwalk_[size_t(y) * ww_ + x] = 1;
                if (ww_ >= 20)
                    for (int x = ww_ - 20; x < ww_; ++x) unwalk_[size_t(y) * ww_ + x] = 1;
                if (y >= wh_ - 4)
                    for (int x = 0; x < ww_; ++x) unwalk_[size_t(y) * ww_ + x] = 1;
            }
    }

    int& at(int x, int y) { return idx_[size_t(y) * w_ + x]; }

    Region& create(int x0, int y0, int x1, int y1) {
        Region r;
        r.index = int(rs_.size());
        r.flags = uint16_t(at(x0, y0));
        r.tx0 = x0, r.ty0 = y0, r.tx1 = x1, r.ty1 = y1;
        r.cx = (x0 + x1) / 2, r.cy = (y0 + y1) / 2;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) at(x, y) = r.index, ++r.count;
        rs_.push_back(std::move(r));
        return rs_.back();
    }

    // From (x, y) on, in rows (around to the top-left), the first tile with no region: its kind and place.
    bool findFree(int x, int y, int& kind, int& fx, int& fy) {
        if (x >= w_) x = 0, y = y + 1 >= h_ ? 0 : y + 1;
        const int sx = x, sy = y;
        for (;;) {
            if (at(x, y) >= kMaxRegions) {
                kind = at(x, y), fx = x, fy = y;
                return true;
            }
            if (++x >= w_) x = 0, y = y + 1 >= h_ ? 0 : y + 1;
            if (x == sx && y == sy) return false;
        }
    }

    struct Rect {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        int size() const { return (x1 - x0) * (y1 - y0); }
    };

    bool columnOf(int x, int y0, int y1, int kind) {
        for (int y = y0; y < y1; ++y)
            if (at(x, y) != kind) return false;
        return true;
    }
    bool rowOf(int y, int x0, int x1, int kind) {
        for (int x = x0; x < x1; ++x)
            if (at(x, y) != kind) return false;
        return true;
    }

    // The rectangle of tiles of `kind` from (x, y), at most 8 x 7 and at most 3 times as long as wide. It starts as the
    // one tile and gets one row and one column more at each step. The next row is added while it is all of the kind.
    // The next column is added while it is all of the kind, but only when no row can be added or when each corner
    // tile was of the kind (from the first step on). The growth stops at the limits or when no row or column fits.
    Rect findArea(int x, int y, int kind) {
        const int maxW = std::min(8, w_ - x), maxH = std::min(7, h_ - y);
        int cols = 1, rows = 1;
        bool canCol = true, canRow = true, corners = true;
        while ((canCol || canRow) && cols < maxW && rows < maxH) {
            canCol = canCol && columnOf(x + cols, y, y + rows, kind);
            canRow = canRow && rowOf(y + rows, x, x + cols, kind);
            corners = corners && at(x + cols, y + rows) == kind;
            const bool addCol = canCol && (corners || !canRow);
            if (canRow) ++rows;
            if (addCol) ++cols;
        }
        cols = std::min(cols, rows * 3);
        rows = std::min(rows, cols * 3);
        return Rect{x, y, x + cols, y + rows};
    }

    // A region takes the tiles of its kind beside it (two passes over its rectangle and one tile around it).
    void grow(Region& r) {
        if (r.tx0 > 0) --r.tx0;
        if (r.ty0 > 0) --r.ty0;
        if (r.tx1 < w_) ++r.tx1;
        if (r.ty1 < h_) ++r.ty1;
        auto beside = [&](int x, int y) {
            return (x > 0 && at(x - 1, y) == r.index) || (x < w_ - 1 && at(x + 1, y) == r.index) || (y > 0 && at(x, y - 1) == r.index) ||
                   (y < h_ - 1 && at(x, y + 1) == r.index);
        };
        for (int pass = 0; pass < 2; ++pass)
            for (int y = r.ty0; y < r.ty1; ++y)
                for (int x = r.tx0; x < r.tx1; ++x)
                    if (at(x, y) == r.flags && beside(x, y)) at(x, y) = r.index;
    }

    // Step 3. The scan goes on from the right of the last rectangle. A rectangle smaller than the last one is replaced
    // by the largest one of up to 25 more free tiles further on (the search stops at one as large as the last one).
    void createRegions() {
        const size_t first = rs_.size();
        bool grownAll = false;
        int lastSize = 7 * 8;
        int fromX = 0, fromY = 0;
        int kind = 0, fx = 0, fy = 0;
        while (findFree(fromX, fromY, kind, fx, fy)) {
            Rect best = findArea(fx, fy, kind), tried = best;
            for (int tries = 0; tries < 25 && best.size() < lastSize; ++tries) {
                if (!findFree(tried.x1, fy, kind, fx, fy)) break;
                tried = findArea(fx, fy, kind);
                if (tried.size() > best.size()) best = tried;
            }
            lastSize = best.size();
            fromX = best.x1, fromY = best.y0;
            if (rs_.size() >= size_t(kMaxRegions)) {
                tooMany_ = true;
                break;
            }
            grow(create(best.x0, best.y0, best.x1, best.y1));
            if (lastSize <= 6 && !grownAll) {
                grownAll = true;
                for (size_t i = first; i < rs_.size(); ++i) grow(rs_[i]);
            }
        }
    }

    // The 8 neighbors of a tile: up, left, right, down, up-left, up-right, down-left, down-right (kOutside: none).
    std::array<int, 8> neighbors(int x, int y) {
        auto get = [&](bool in, int nx, int ny) { return in ? at(nx, ny) : kOutside; };
        return {get(y > 0, x, y - 1),
                get(x > 0, x - 1, y),
                get(x + 1 < w_, x + 1, y),
                get(y + 1 < h_, x, y + 1),
                get(y > 0 && x > 0, x - 1, y - 1),
                get(y > 0 && x + 1 < w_, x + 1, y - 1),
                get(y + 1 < h_ && x > 0, x - 1, y + 1),
                get(y + 1 < h_ && x + 1 < w_, x + 1, y + 1)};
    }

    bool walkable2x2(int mx, int my) const {
        return walkable(mx, my) && walkable(mx + 1, my) && walkable(mx, my + 1) && walkable(mx + 1, my + 1);
    }

    // A diagonal neighbor (d: 4 up-left .. 7 down-right) of two walkable regions counts when a 16x16 unit gets
    // through the corner.
    bool cornerPasses(int x, int y, int d) const {
        const int mx = x * 4, my = y * 4;
        switch (d) {
        case 4: return walkable2x2(mx - 2, my - 2) && walkable2x2(mx, my) && (walkable2x2(mx - 2, my) || walkable2x2(mx, my - 2));
        case 5: return walkable2x2(mx + 4, my - 2) && walkable2x2(mx + 2, my) && (walkable2x2(mx + 2, my - 2) || walkable2x2(mx + 4, my));
        case 6: return walkable2x2(mx, my + 2) && walkable2x2(mx - 2, my + 4) && (walkable2x2(mx - 2, my + 2) || walkable2x2(mx, my + 4));
        default: return walkable2x2(mx + 2, my + 2) && walkable2x2(mx + 4, my + 4) && (walkable2x2(mx + 4, my + 2) || walkable2x2(mx + 2, my + 4));
        }
    }

    // The counts, boxes, neighbors and groups of the regions.
    void refresh() {
        for (Region& r : rs_) r.ax0 = r.ay0 = INT_MAX, r.ax1 = r.ay1 = INT_MIN, r.count = 0;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                const int i = at(x, y);
                if (i >= kMaxRegions) continue;
                Region& r = rs_[size_t(i)];
                ++r.count;
                r.ax0 = std::min(r.ax0, x), r.ay0 = std::min(r.ay0, y), r.ax1 = std::max(r.ax1, x + 1), r.ay1 = std::max(r.ay1, y + 1);
            }
        for (Region& r : rs_)
            if (!r.count) r.flags = kOutside;
        for (Region& r : rs_) {
            if (!r.count) continue;
            r.walkableNeighbors.clear(), r.otherNeighbors.clear();
            for (int y = r.ay0; y < r.ay1; ++y)
                for (int x = r.ax0; x < r.ax1; ++x) {
                    if (at(x, y) != r.index) continue;
                    const auto nb = neighbors(x, y);
                    for (int d = 0; d < 8; ++d) {
                        const int n = nb[size_t(d)];
                        if (n == kOutside || n == r.index || n >= kMaxRegions) continue;
                        const Region& o = rs_[size_t(n)];
                        if (!(d < 4 || !r.walkable() || !o.walkable() || cornerPasses(x, y, d))) continue;
                        auto& list = o.walkable() ? r.walkableNeighbors : r.otherNeighbors;
                        if (std::find(list.begin(), list.end(), n) == list.end()) list.push_back(n);
                    }
                }
            // (the game moves the console's region to the end of the list)
            auto& other = r.otherNeighbors;
            for (size_t k = 0; k + 1 < other.size(); ++k)
                if (other[k] == 0) std::swap(other[k], other.back());
        }
        for (Region& r : rs_)
            if (r.group < kConsoleGroup) r.group = 0;
        std::vector<int> stack;
        int next = 1;
        for (Region& r : rs_) {
            if (r.group != 0 || !r.count) continue;
            const int g = next++;
            r.group = g;
            stack.push_back(r.index);
            while (!stack.empty()) {
                const Region& c = rs_[size_t(stack.back())];
                stack.pop_back();
                for (int n : c.walkable() ? c.walkableNeighbors : c.otherNeighbors)
                    if (rs_[size_t(n)].group == 0) rs_[size_t(n)].group = g, stack.push_back(n);
            }
        }
    }

    // Regions of fewer than n tiles (6, then 8, 10, ...) go into their smallest neighbor of the same kind, the last
    // region first, until fewer than 2500 regions are left.
    void mergeSmall() {
        for (int n = 6;; n += 2) {
            bool merged = false;
            int largest = 0;
            for (size_t k = rs_.size(); k-- > 0;) {
                Region& r = rs_[k];
                if (!r.count || r.count >= n || r.group >= kConsoleGroup) continue;
                Region* into = nullptr;
                auto consider = [&](int i) {
                    Region& o = rs_[size_t(i)];
                    if (!o.count || o.group >= kConsoleGroup || o.flags != r.flags) return;
                    if (!into || o.count < into->count) into = &o;
                };
                for (int i : r.walkableNeighbors) consider(i);
                for (int i : r.otherNeighbors) consider(i);
                if (!into) continue;
                for (int y = r.ay0; y < r.ay1; ++y)
                    for (int x = r.ax0; x < r.ax1; ++x)
                        if (at(x, y) == r.index) at(x, y) = into->index;
                into->count += r.count;
                r.count = 0, r.flags = kOutside;
                into->ax0 = std::min(into->ax0, r.ax0), into->ay0 = std::min(into->ay0, r.ay0);
                into->ax1 = std::max(into->ax1, r.ax1), into->ay1 = std::max(into->ay1, r.ay1);
                merged = true;
            }
            int left = 0;
            for (const Region& r : rs_)
                if (r.count) ++left, largest = std::max(largest, r.count);
            if (left < 2500) break;
            if (!merged && n > largest) break;  // (the game does not end here: no merge can happen any more)
        }
    }

    // The regions with tiles get new indexes in their order.
    void compact() {
        std::vector<int> to(rs_.size(), 0);
        std::vector<Region> kept;
        for (size_t i = 0; i < rs_.size(); ++i) {
            if (!rs_[i].count) continue;
            to[i] = int(kept.size());
            kept.push_back(std::move(rs_[i]));
            kept.back().index = to[i];
            kept.back().walkableNeighbors.clear(), kept.back().otherNeighbors.clear();
        }
        for (int& i : idx_)
            if (i < kMaxRegions) i = to[size_t(i)];
        rs_ = std::move(kept);
    }

    // The minitiles of a tile's edges, in the order of neighbors(): the 4 sides (4 minitiles each) and the 4 corners
    // (1 minitile each). `in`: the first minitile of the edge in the tile; `out`: the first one beside it in the
    // neighbor tile; `step`: to the next minitile of the edge.
    struct Edge {
        int inX, inY, outX, outY, stepX, stepY, length;
    };
    static constexpr Edge kEdges[8] = {
        {0, 0, 0, -1, 1, 0, 4}, {0, 0, -1, 0, 0, 1, 4}, {3, 0, 4, 0, 0, 1, 4}, {0, 3, 0, 4, 1, 0, 4},
        {0, 0, -1, -1, 0, 0, 1}, {3, 0, 4, -1, 0, 0, 1}, {0, 3, -1, 4, 0, 0, 1}, {3, 3, 4, 4, 0, 0, 1},
    };
    // The minitiles of the line from (mx, my) that are walkable (`walk`) or not.
    int countLine(int mx, int my, int stepX, int stepY, int length, bool walk) const {
        int n = 0;
        for (int k = 0; k < length; ++k) n += walkable(mx + k * stepX, my + k * stepY) == walk;
        return n;
    }

    // Step 6. A tile of a walkable region with unwalkable minitiles gives them to the unwalkable neighbor region that
    // has the most unwalkable minitiles on the tile's edge to it (the sides and the corners); a tile of an unwalkable
    // region gives its walkable minitiles to a walkable neighbor in the same way (the sides only). With equal counts,
    // the edge that comes last in the order of neighbors() wins. A split neighbor tile counts when its minitiles
    // beside the edge include some of that kind; its part of that kind is then the region.
    void splitTiles() {
        splits_.clear();
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                const int ti = y * w_ + x;
                if (!(flags_[size_t(ti)] & kTilePartial)) continue;
                const int ri = at(x, y);
                if (ri >= kMaxRegions) continue;  // (no region: only when there are too many)
                const Region& r = rs_[size_t(ri)];
                const bool give = !r.walkable();  // the kind of minitile that goes to the neighbor (true: walkable)
                const auto nb = neighbors(x, y);
                const int mx = x * 4, my = y * 4;
                int other = -1, most = 0;  // the region that gets the minitiles (an index of rs_), its count
                const int edges = r.walkable() ? 8 : 4;
                for (int d = 0; d < edges; ++d) {
                    const Edge& e = kEdges[d];
                    const int n = countLine(mx + e.inX, my + e.inY, e.stepX, e.stepY, e.length, give);
                    if (n == 0 || n < most) continue;
                    const int ni = nb[size_t(d)];
                    if (ni == ri) continue;
                    int to = -1;
                    if (ni >= kSplitBase) {
                        if (countLine(mx + e.outX, my + e.outY, e.stepX, e.stepY, e.length, give) > 0) {
                            const PathingRegions::Split& s = splits_[size_t(ni - kSplitBase)];
                            to = give ? s.a : s.b;
                        }
                    } else if (ni < kMaxRegions && rs_[size_t(ni)].walkable() == give) {
                        to = ni;
                    }
                    if (to >= 0) most = n, other = to;
                }
                uint16_t mask = 0;  // the unwalkable minitiles
                const auto* f = vf4(ti);
                for (int k = 0; k < 16; ++k)
                    if (!f || !((*f)[size_t(k)] & Tileset::kWalkable)) mask |= uint16_t(1u << k);
                if (other < 0 || other == ri) mask = r.walkable() ? 0 : 0xffff;
                else if (!r.walkable() && !rs_[size_t(other)].walkable()) mask = 0xffff;
                const int o = other < 0 ? ri : other;
                at(x, y) = kSplitBase + int(splits_.size());
                splits_.push_back(r.walkable() ? PathingRegions::Split{mask, ri, o} : PathingRegions::Split{mask, o, ri});
            }
    }
};

}  // namespace

int PathingRegions::regionAt(int mx, int my) const {
    if (mx < 0 || my < 0 || mx >= walkW() || my >= walkH()) return -1;
    const size_t ti = size_t(my / 4) * width + mx / 4;
    if (ti >= tile.size()) return -1;
    const int t = tile[ti];
    if (t >= 0) return size_t(t) < regions.size() ? t : -1;
    if (size_t(-1 - t) >= splits.size()) return -1;
    const Split& s = splits[size_t(-1 - t)];
    const int r = (s.mask >> ((my & 3) * 4 + (mx & 3))) & 1 ? s.b : s.a;
    return r >= 0 && size_t(r) < regions.size() ? r : -1;
}

PathingRegions computePathingRegions(const TerrainLayer& terrain, const Tileset& tileset) {
    const auto t0 = std::chrono::steady_clock::now();
    PathingRegions out;
    Builder(terrain, tileset).run(out);
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

}  // namespace stardraft
