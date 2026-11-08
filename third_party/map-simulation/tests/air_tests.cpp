// SPDX-License-Identifier: MIT
#include <iostream>
#include <mapeditor/simulation/air.h>
#include <stdexcept>
using namespace mapeditor::simulation;
void check(bool ok, const char *reason) {
  if (!ok)
    throw std::runtime_error(reason);
}
bool equal(Point a, Point b) { return a.x == b.x && a.y == b.y; }
int main() {
  try {
    check(equal(AirField::vector(-91, 5, 2, false), {-96, 73}) &&
              equal(AirField::vector(91, 5, 2, false), {96, 73}),
          "Sparse opposing flyers retain their measured separation vectors");
    check(equal(AirField::vector(-109, 3, 2, false), {-52, 109}) &&
              equal(AirField::vector(116, 4, 2, true), {23, 66}),
          "Resting density scaling precedes component adjustment");
    check(equal(AirField::vector(-63, 0, 4, false), {-133, 2}) &&
              equal(AirField::vector(-124, 0, 4, false), {-7, 132}),
          "Small negative components preserve the observed asymmetric bias");
    Random random;
    AirField field(4096, 4096);
    AirState first, second;
    field.insert(first, {1024, 1024}, random);
    check(
        first.heading == -48 && first.profile == 3 &&
            random.state() == 953210035U && field.strength(first) == 1,
        "Registration consumes one random sample for orientation and profile");
    check(!field.push(first, {1024, 1024}, {1200, 1200}, {16, 16, 15, 15}) &&
              first.wait == 0,
          "A solitary flyer leaves the cadence ready without applying a push");
    field.insert(second, {1025, 1024}, random);
    first.heading = -91;
    first.profile = 5;
    first.wait = 1;
    auto vector =
        field.push(first, {1024, 1024}, {1200, 1200}, {16, 16, 15, 15});
    check(vector && equal(*vector, {-96, 73}) && first.wait == 0,
          "Overlapping occupancy applies the independent per-unit state");
    auto copied = field;
    auto copiedFirst = first;
    field.move(second, {1200, 1200}, random);
    check(field.strength(first) == 1 && copied.strength(copiedFirst) == 2,
          "Moving between cells leaves a copied field independent");
    auto before = random.state();
    field.move(first, {1201, 1200}, random);
    check(random.state() == before && first.wait == 0 &&
              field.strength(first) == 2,
          "Cell migration retains an initialized profile without another draw");
    field.erase(first);
    field.erase(second);
    check(field.strength(first) == 0 && field.strength(second) == 0,
          "Removal detaches occupancy safely");
    AirField edge(4096, 4096);
    AirState near, neighbor;
    edge.insert(near, {17, 1024}, random);
    edge.insert(neighbor, {18, 1024}, random);
    near.heading = -64;
    near.profile = 5;
    auto pushed = edge.push(near, {17, 1024}, {17, 1024}, {16, 16, 15, 15});
    check(pushed && near.heading == 64 && near.profile == 0 &&
              !near.initialized && near.wait == 7,
          "An edge reflects outward separation and clears profile "
          "initialization");
    before = random.state();
    edge.move(near, {49, 1024}, random);
    check(random.state() != before && near.initialized && near.wait == 0,
          "Changing cells after an edge reset initializes a new profile");
    std::cout << "PASS: original flying separation arithmetic and value grid\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
