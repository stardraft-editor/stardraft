#pragma once
// kit_test's checks: each file in tools/tests/kit/ adds its tests with KIT_TEST(name) { ... CHECK(...); }.

#include <cstdio>
#include <functional>
#include <vector>

#include "test_support.h"

namespace kitcheck {
struct Test {
    const char* name;
    void (*run)();
};
inline std::vector<Test>& tests() {
    static std::vector<Test> t;
    return t;
}
inline int& failed() {
    static int f = 0;
    return f;
}
struct Add {
    Add(const char* name, void (*run)()) { tests().push_back({name, run}); }
};
}  // namespace kitcheck

#define KIT_TEST(name)                                     \
    static void kitTest_##name();                          \
    static kitcheck::Add kitAdd_##name(#name, kitTest_##name); \
    static void kitTest_##name()

#define CHECK(x)                                                                    \
    do {                                                                            \
        if (!(x)) {                                                                 \
            std::printf("kit_test: FAILED %s (%s:%d)\n", #x, __FILE__, __LINE__);  \
            ++kitcheck::failed();                                                   \
        }                                                                           \
    } while (0)
