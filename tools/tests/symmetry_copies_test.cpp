#include "stardraft/symmetry_copies.h"
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>

using namespace stardraft;
int main() {
    assert(copyTransforms(0, 16, true, true).size() == 32);
    assert(copyTransforms(15, 15, true, true, true).size() == 34);
    assert(copyTransforms(0, 1, true, false, true).size() == 2);
    assert(copyTransforms(0, 3, false, false, true).size() == 6);
    assert(copyTransforms(3, 4, true).size() == 6);
    for (int turns = 1; turns <= 16; ++turns)
        for (int mirrors = 0; mirrors < 16; ++mirrors)
            for (bool direct : {false, true})
                for (bool reflected : {false, true}) {
                    const auto copies = copyTransforms(mirrors, turns, direct, reflected, true);
                    assert(copies.front().key() == CopyTransform{}.key() && copies.size() <= 64);
                    std::set<std::string> keys;
                    for (const auto& t : copies) {
                        assert(keys.insert(t.key()).second);
                        assert(std::abs(t.a * t.a + t.c * t.c - 1) < 1e-9);
                        assert(std::abs(t.b * t.b + t.d * t.d - 1) < 1e-9);
                        assert(std::abs(t.a * t.b + t.c * t.d) < 1e-9);
                    }
                }
    CopyOptions options;
    options.direct = options.mirroredRotations = options.opposite = true;
    options.terrain = 13, options.rawTile = 64;
    const auto turns = copyTransforms(0, 16, true, true);
    for (size_t i = 1; i < turns.size(); ++i) options.choices.push_back({turns[i].key(), i % 3 != 0, int(i), int(i * 16)});
    const auto text = copyOptionsText(options);
    assert(copyOptionsText(readCopyOptions(text)) == text);
    const auto old = readCopyOptions("2;1;13;64;-1000000,0,0,1000000:0:7:32");
    assert(old.direct && old.terrain == 13 && old.rawTile == 64 && !old.mirroredRotations && !old.opposite);
    assert(old.choices.size() == 1 && !old.choices.front().enabled && old.choices.front().terrain == 7);
    assert(readCopyOptions("3;1;13;64;2;0").choices.empty());
    assert(readCopyOptions("3;1;13;64;1").choices.empty());
    std::puts("Symmetry composer: 16 rotations, reflected rotations, opposite copies, direct unions, bounds, stable keys and v2/v3 "
              "settings passed");
}
