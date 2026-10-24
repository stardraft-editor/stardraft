// Proximity: the game's distances, and the grid of the units by place (proximity.h).
#include "proximity.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace sim {

int octagonalLength(int dx, int dy) {
    int a = std::abs(dx), b = std::abs(dy);
    if (a < b) std::swap(a, b);
    if (a / 4 < b) return a - a / 16 + b * 3 / 8 - a / 64 + b * 3 / 256;
    return a;
}

Box boxAround(int x, int y, int left, int up, int right, int down) {
    return {x - left, y - up, x + right, y + down};
}

Box grow(const Box& b, int by) { return {b.left - by, b.top - by, b.right + by, b.bottom + by}; }

// (a box's gap counts from the cell past its edge: one more)
Box within(const Box& b, int gap) { return grow(b, gap + 1); }

// (the space between the boxes along each axis: 0 when they meet or touch)
int boxGap(const Box& a, const Box& b) {
    int gx = std::max({0, a.left - (b.right + 1), b.left - (a.right + 1)});
    int gy = std::max({0, a.top - (b.bottom + 1), b.top - (a.bottom + 1)});
    return octagonalLength(gx, gy);
}

int axisGap(const Box& a, const Box& b) {
    int gx = std::max({0, a.left - (b.right + 1), b.left - (a.right + 1)});
    int gy = std::max({0, a.top - (b.bottom + 1), b.top - (a.bottom + 1)});
    return std::max(gx, gy);
}

int pointGap(const Box& b, int x, int y) {
    return octagonalLength(std::max({0, b.left - x, x - (b.right + 1)}), std::max({0, b.top - y, y - (b.bottom + 1)}));
}

bool boxesTouch(const Box& a, const Box& b) {
    return a.left <= b.right && b.left <= a.right && a.top <= b.bottom && b.top <= a.bottom;
}

int centreDistance(int ax, int ay, int bx, int by) { return octagonalLength(bx - ax, by - ay); }

double exactDistance(int ax, int ay, int bx, int by) { return std::hypot(double(ax - bx), double(ay - by)); }

bool withinSquare(int ax, int ay, int bx, int by, int reach) {
    return std::abs(ax - bx) <= reach && std::abs(ay - by) <= reach;
}

bool pointIn(const Box& b, int x, int y) { return x >= b.left && x <= b.right && y >= b.top && y <= b.bottom; }

Box square(int x, int y, int reach) { return {x - reach, y - reach, x + reach, y + reach}; }

void UnitGrid::reset(int width, int height) {
    columns_ = std::max(1, (width + kCell - 1) / kCell);
    rows_ = std::max(1, (height + kCell - 1) / kCell);
    cells_.assign(size_t(columns_) * size_t(rows_), {});
    where_.clear();
    seen_.clear();
    stamp_ = 0;
}

UnitGrid::Cells UnitGrid::cellsOf(const Box& box) const {
    auto column = [&](int v) { return std::clamp(v < 0 ? 0 : v / kCell, 0, columns_ - 1); };
    auto row = [&](int v) { return std::clamp(v < 0 ? 0 : v / kCell, 0, rows_ - 1); };
    return {column(box.left), row(box.top), column(box.right), row(box.bottom)};
}

void UnitGrid::add(const Cells& c, int unit) {
    for (int y = c.y0; y <= c.y1; ++y)
        for (int x = c.x0; x <= c.x1; ++x) {
            std::vector<int>& cell = cells_[size_t(y) * size_t(columns_) + size_t(x)];
            cell.insert(std::lower_bound(cell.begin(), cell.end(), unit), unit);
        }
}

void UnitGrid::take(const Cells& c, int unit) {
    for (int y = c.y0; y <= c.y1; ++y)
        for (int x = c.x0; x <= c.x1; ++x) {
            std::vector<int>& cell = cells_[size_t(y) * size_t(columns_) + size_t(x)];
            auto at = std::lower_bound(cell.begin(), cell.end(), unit);
            if (at != cell.end() && *at == unit) cell.erase(at);
        }
}

void UnitGrid::place(int unit, const Box& box) {
    if (unit < 0 || cells_.empty()) return;
    if (size_t(unit) >= where_.size()) where_.resize(size_t(unit) + 1);
    Cells now = cellsOf(box);
    Cells& was = where_[size_t(unit)];
    if (was.x0 == now.x0 && was.y0 == now.y0 && was.x1 == now.x1 && was.y1 == now.y1) return;  // (the same cells)
    take(was, unit);
    add(now, unit);
    was = now;
}

void UnitGrid::remove(int unit) {
    if (unit < 0 || size_t(unit) >= where_.size()) return;
    take(where_[size_t(unit)], unit);
    where_[size_t(unit)] = Cells();
}

std::vector<int> UnitGrid::query(const Box& area) const {
    std::vector<int> out;
    collect(area, out);
    return out;
}

void UnitGrid::collect(const Box& area, std::vector<int>& out) const {
    out.clear();
    if (cells_.empty()) return;
    if (seen_.size() < where_.size()) seen_.resize(where_.size(), 0);
    if (++stamp_ == 0) {  // (the stamps wrapped: all fresh)
        std::fill(seen_.begin(), seen_.end(), 0);
        stamp_ = 1;
    }
    Cells c = cellsOf(area);
    for (int y = c.y0; y <= c.y1; ++y)
        for (int x = c.x0; x <= c.x1; ++x)
            for (int j : cells_[size_t(y) * size_t(columns_) + size_t(x)])
                if (seen_[size_t(j)] != stamp_) {
                    seen_[size_t(j)] = stamp_;
                    out.push_back(j);
                }
    std::sort(out.begin(), out.end());
}

}  // namespace sim
