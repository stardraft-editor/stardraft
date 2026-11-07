// SPDX-License-Identifier: MIT
#include <algorithm>
#include <iostream>
#include <mapeditor/simulation/contours.h>
#include <stdexcept>
using namespace mapeditor::simulation;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main() {
  try {
    std::vector<std::uint8_t> cv(104), vf(64);
    for (int i = 0; i < 32; ++i)
      vf[i * 2] = 1;
    cv[52 + 22] = 1;
    vf[32 + 5 * 2] = 0;
    std::vector<std::uint16_t> tiles(64 * 64, 16);
    Terrain terrain(64, 64, tiles, cv, vf);
    Contours flat(terrain);
    check(flat.edges()[0].front() == TerrainEdge{-1, -1, 2048, 7},
          "Merged upper map boundary with convex endpoints");
    auto original = flat.edges();
    tiles[10 * 64 + 10] = 17;
    Terrain hole(64, 64, tiles, cv, vf);
    Contours edges(hole);
    for (int normal = 0; normal < 4; ++normal) {
      auto expected = original[normal];
      bool horizontal = normal % 2 == 0;
      expected.push_back({normal == 0 || normal == 3 ? 335 : 328, 328, 335,
                          horizontal ? 61 : 50});
      std::sort(expected.begin(), expected.end(), [](auto a, auto b) {
        return a.line != b.line ? a.line < b.line : a.first < b.first;
      });
      check(edges.edges()[normal] == expected,
            "Isolated blocked minitile has four concave boundary runs");
    }
    dat::Extents shape{11, 11, 11, 11};
    for (auto p :
         {Point{331, 316}, Point{316, 331}, Point{347, 331}, Point{331, 347}})
      check(edges.permits(p, shape),
            "One-pixel clearance from terrain boundary");
    for (auto p :
         {Point{331, 317}, Point{317, 331}, Point{346, 331}, Point{331, 346}})
      check(!edges.permits(p, shape),
            "Inclusive footprint touches terrain boundary");
    check(edges.permits({347, 346}, shape) && edges.permits({344, 348}, shape),
          "Corner sweep endpoints are independently clear");
    check(!edges.permitsMovement({347, 346}, {344, 348}, shape),
          "A diagonal corner sweep can collide between clear endpoints");
    check(edges.permitsMovement({347, 346}, {347, 348}, shape),
          "An orthogonal tangent clears that corner");
    check(edges.permits({300, 331}, shape) &&
              edges.permits({370, 331}, shape) &&
              !edges.permitsMovement({300, 331}, {370, 331}, shape),
          "Swept clearance detects a wall crossed between separated endpoints");
    check(flat.edges() == original,
          "Contour instances retain independent values");
    std::cout << "PASS: native terrain boundary runs and corner metadata\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
