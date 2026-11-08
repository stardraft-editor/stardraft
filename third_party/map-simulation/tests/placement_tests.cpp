// SPDX-License-Identifier: MIT
#include <iostream>
#include <mapeditor/simulation/placement.h>
using namespace mapeditor::simulation;
void check(bool ok) {
  if (!ok)
    throw std::runtime_error("Placement test failed");
}
int main() {
  try {
    std::vector<std::uint8_t> cv(104), vf(32);
    for (int n = 0; n < 16; ++n)
      vf[n * 2] = 1;
    std::vector<std::uint16_t> tiles(64 * 64, 16);
    Terrain terrain(64, 64, tiles, cv, vf);
    Regions regions(terrain);
    dat::Extents worker{11, 11, 11, 11};
    std::vector<CollisionBody> bodies{{0, {1024, 1008}, {49, 32, 49, 32}}};
    std::vector<Point> expected{{984, 1072},  {1008, 1072}, {1032, 1072},
                                {1056, 1072}, {1080, 1072}, {1088, 1042},
                                {1088, 1014}, {1088, 990},  {960, 976}};
    for (auto point : expected) {
      auto result =
          Placement(terrain, regions, bodies).find({{1024, 1024}, worker});
      check(result && result->x == point.x && result->y == point.y);
      bodies.push_back({std::uint32_t(bodies.size()), *result, worker});
    }
    auto clear = Placement(terrain, regions, bodies).find({{700, 700}, worker});
    check(clear && clear->x == 700 && clear->y == 700);
    check(
        !Placement(terrain, regions, bodies).terrainFits({2040, 100}, worker));
    check(
        !Placement(terrain, regions, bodies).terrainFits({100, 2010}, worker));
    // Flying and non-colliding occupants do not block a ground request.
    bodies = {{1, {700, 700}, worker, true},
              {2, {700, 700}, worker, false, false}};
    clear = Placement(terrain, regions, bodies).find({{700, 700}, worker});
    check(clear && clear->x == 700 && clear->y == 700);
    bodies = {{1, {700, 700}, worker}};
    clear = Placement(terrain, regions, bodies)
                .find({{700, 700}, worker, false, 1});
    check(clear && clear->x == 700 && clear->y == 700);
    std::cout << "PASS: placement rings, collision layers, extents and ignored "
                 "handles\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
