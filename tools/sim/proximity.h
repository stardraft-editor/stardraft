// Proximity: the distances the game's rules use, and a grid of the units by place.
//
// Every "in range", "near" and "touching" test of the simulation goes through these functions:
// - boxGap: the gap between two boxes (units: their units.dat boxes; buildings too), by the game's octagonal length;
//   weapon ranges (melee too) and minimum ranges, acquisition, detection, healing, docking, mining reach, contact.
// - pointGap: the gap from a point to a box (splash, a Scarab's or a line hit's contact).
// - boxesTouch: two boxes overlap or touch (collision, placement).
// - centreDistance: the octagonal length between two centres (the nearest of equal targets, a trgtrangecondjmp).
// - exactDistance: the straight distance between two points (heal search order, a spot's nearness, mining spots).
// - withinSquare: both axis distances at most a reach (call for help, bounce, the heal search area).
// UnitGrid: each unit is in the 64-pixel cells that its box covers. A unit's cells change only when its box crosses a
// cell edge, when it is made or when it dies. A query gives the units whose box meets an area, in their list order.
#pragma once

#include <cstdint>
#include <vector>

namespace sim {

int octagonalLength(int dx, int dy);  // the game's approximate length

// A box, its edges inclusive.
struct Box {
    int left = 0, top = 0, right = 0, bottom = 0;
};
Box boxAround(int x, int y, int left, int up, int right, int down);  // (x - left .. x + right, y - up .. y + down)
Box grow(const Box& b, int by);
Box within(const Box& b, int gap);  // (the area that every box at most `gap` from b meets)
int boxGap(const Box& a, const Box& b);
int axisGap(const Box& a, const Box& b);  // (the larger of the two axes' gaps)
int pointGap(const Box& b, int x, int y);
bool boxesTouch(const Box& a, const Box& b);
int centreDistance(int ax, int ay, int bx, int by);
double exactDistance(int ax, int ay, int bx, int by);
bool withinSquare(int ax, int ay, int bx, int by, int reach);
bool pointIn(const Box& b, int x, int y);
Box square(int x, int y, int reach);  // (x - reach .. x + reach, y - reach .. y + reach)

class UnitGrid {
public:
    void reset(int width, int height);  // (pixels)
    void place(int unit, const Box& box);  // (a new unit, or one that moved)
    void remove(int unit);
    // The units in the cells that the area covers (all whose box meets it, and some near it), in their list order,
    // each once.
    std::vector<int> query(const Box& area) const;
    template <class F> void forEach(const Box& area, F f) const {
        collect(area, buffer_);
        std::vector<int> found;
        found.swap(buffer_);  // (the callback may query again)
        for (int j : found) f(j);
        found.swap(buffer_);
    }

private:
    struct Cells {
        int x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // (x1 < x0: in no cell)
    };
    Cells cellsOf(const Box& box) const;
    void collect(const Box& area, std::vector<int>& out) const;
    void add(const Cells& c, int unit);
    void take(const Cells& c, int unit);
    static constexpr int kCell = 64;
    int columns_ = 0, rows_ = 0;
    std::vector<std::vector<int>> cells_;  // (each in list order)
    std::vector<Cells> where_;             // (by unit)
    mutable std::vector<uint32_t> seen_;   // (a query's units: each once)
    mutable uint32_t stamp_ = 0;
    mutable std::vector<int> buffer_;  // (forEach's, kept between queries)
};

}  // namespace sim
