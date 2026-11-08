// SPDX-License-Identifier: MIT
#include <iostream>
#include <mapeditor/simulation/definitions.h>
using namespace mapeditor::simulation;
void check(bool value) {
  if (!value)
    throw std::runtime_error("Offset table test failed");
}
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught);
}
int main() {
  try {
    // Two frames, two signed offsets each; frame payloads need not be ordered.
    std::vector<std::uint8_t> bytes{2,   0,   0, 0,   2,   0, 0, 0,
                                    20,  0,   0, 0,   16,  0, 0, 0,
                                    128, 127, 0, 255, 253, 5, 1, 2};
    OffsetTable table(bytes);
    check(table.frames() == 2 && table.at(0, 0).x == -3 &&
          table.at(0, 0).y == 5);
    check(table.at(0, 0, true).x == 3 && table.at(1, 0).x == -128 &&
          table.at(1, 1).y == -1);
    rejects([&] { table.at(2, 0); });
    rejects([&] { table.at(0, 2); });
    rejects([&] { table.at(-1, 0); });
    for (int length : {0, 4, 7, 15, 23})
      rejects([&] { OffsetTable bad(std::span(bytes).first(length)); });
    auto bad = bytes;
    bad[8] = 255;
    rejects([&] { OffsetTable table(bad); });
    bad = bytes;
    bad[4] = 255;
    rejects([&] { OffsetTable table(bad); });
    std::cout
        << "PASS: bounded graphics offset decoding and mirrored coordinates\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
