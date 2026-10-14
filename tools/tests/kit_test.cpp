// The control kit's widgets (apps/kit) without a window: each test (tools/tests/kit/*.cpp) puts widgets in a
// FakeHost, sends events and paints them on a RecordingCanvas. Runs on every system. `kit_test [name]` runs one test.
// Prints "kit_test: passed" or each check that fails.

#include <cstring>

#include "kit/kit_check.h"

int main(int argc, char** argv) {
    int ran = 0;
    for (const auto& t : kitcheck::tests())
        if (argc < 2 || !std::strcmp(argv[1], t.name)) t.run(), ++ran;
    if (!ran) return std::printf("kit_test: no test named %s\n", argc > 1 ? argv[1] : ""), 1;
    if (kitcheck::failed()) return std::printf("kit_test: %d failed\n", kitcheck::failed()), 1;
    std::printf("kit_test: passed (%d tests)\n", ran);
    return 0;
}
