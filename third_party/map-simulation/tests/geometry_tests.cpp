// SPDX-License-Identifier: MIT
#include <iostream>
#include <mapeditor/simulation/geometry.h>
#include <mapeditor/simulation/psi.h>
using namespace mapeditor::simulation;
void check(bool b) { if (!b) throw std::runtime_error("Geometry regression"); }
int main() {
  try {
    check(withinPsiField({-255, -64}) && withinPsiField({255, -64}));
    check(!withinPsiField({-256, 0}) && !withinPsiField({256, 0}));
    check(withinPsiField({223, 95}) && !withinPsiField({224, 95}) &&
          withinPsiField({-224, 95}) && !withinPsiField({-225, 95}));
    check(withinPsiField({191, -128}) && withinPsiField({-192, -128}) &&
          !withinPsiField({192, -128}));
    check(withinPsiField({95, -159}) && !withinPsiField({96, -159}) &&
          withinPsiField({-96, 128}) && !withinPsiField({96, 128}));
    check(!withinPsiField({0, -160}) && !withinPsiField({0, 160}) &&
          !withinPsiField({std::numeric_limits<int>::min(), 0}));
    check(distance({0,0}) == 0 && distance({100,100}) == 131);
    check(distance({256,64}) == 256 && distance({256,65}) == 260);
    check(distance({-100,100}) == 131 && distance({100,-100}) == 131);
    check(heading({0,0}) == 0 && heading({1,0}) == 64 &&
          heading({0,1}) == -128 && heading({-1,0}) == -64);
    check(heading({1,1}) == 95 && heading({1,-1}) == 33 &&
          heading({-1,1}) == -95 && heading({-1,-1}) == -33);
    check(directionVector(0).y == -256 && directionVector(64).x == 256 &&
          directionVector(128).y == 256 && directionVector(-64).x == -256);
    check(directionVector(32).x == 181 && directionVector(32).y == -181);
    bool rejected = false;
    try { distance({std::numeric_limits<int>::min(),0}); }
    catch (const std::out_of_range &) { rejected = true; }
    check(rejected);
    // Whole-footprint hashes from isolated public-API sight observations:
    // twelve ground ranges followed by twelve airborne ranges on flat terrain.
    const std::array<std::uint64_t, 24> expectedSight{
        3652372997285033989ULL,  13239330144813378333ULL,
        9575453479450442729ULL,  3928051409821950285ULL,
        13222017611316977105ULL, 7229913634367219993ULL,
        1149142630379745961ULL,  14960088766468802337ULL,
        15708366606514557901ULL, 9868390641721239657ULL,
        14443401106728892773ULL, 10689884365915275461ULL,
        14695981039346656037ULL, 12178034270537034877ULL,
        1200437362297581672ULL,  8920795257003805527ULL,
        15487191208898696860ULL, 4194935678454696081ULL,
        15542239126645720346ULL, 7256891094287487971ULL,
        15952322010662912252ULL, 12876877477660406504ULL,
        14958798521870652255ULL, 2848006872690640254ULL,
    };
    for (int air = 0; air < 2; ++air)
      for (int radius = 0; radius < 12; ++radius) {
        std::uint64_t digest = 14695981039346656037ULL;
        for (auto point : sightFootprint(radius, air != 0))
          for (int value : {point.x + 12, point.y + 12}) {
            digest ^= std::uint8_t(value);
            digest *= 1099511628211ULL;
          }
        check(digest == expectedSight[air * 12 + radius]);
      }
    for (int radius : {-1, 12}) {
      bool rejectedSight = false;
      try {
        sightFootprint(radius, false);
      } catch (const std::out_of_range &) {
        rejectedSight = true;
      }
      check(rejectedSight);
    }
    std::cout << "PASS: deterministic geometry boundaries\n";
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
