// apps/common/wintypes.h: on Windows these checks run against Win32 itself, elsewhere against the stand-ins, so both
// give the same results.
#include "wintypes.h"

#include "kit_check.h"

KIT_TEST(wintypes) {
    CHECK(MulDiv(10, 144, 72) == 20);
    CHECK(MulDiv(7, 3, 2) == 11);    // 10.5 rounds away from zero
    CHECK(MulDiv(-7, 3, 2) == -11);
    CHECK(MulDiv(7, -3, 2) == -11);
    CHECK(MulDiv(5, 96, 96) == 5);
    CHECK(MulDiv(26, 120, 96) == 33);  // 32.5
    CHECK(MulDiv(1, 1, 0) == -1);
    CHECK(GetRValue(RGB(1, 2, 3)) == 1 && GetGValue(RGB(1, 2, 3)) == 2 && GetBValue(RGB(1, 2, 3)) == 3);
    RECT r{10, 20, 30, 40};
    CHECK(PtInRect(&r, POINT{10, 20}) && !PtInRect(&r, POINT{30, 20}) && !PtInRect(&r, POINT{10, 40}));
    OffsetRect(&r, 5, -5);
    CHECK(r.left == 15 && r.top == 15 && r.right == 35 && r.bottom == 35);
    InflateRect(&r, 2, 3);
    CHECK(r.left == 13 && r.top == 12 && r.right == 37 && r.bottom == 38);
    RECT a{0, 0, 10, 10}, b{5, 5, 20, 20}, c{30, 30, 40, 40}, o{};
    CHECK(IntersectRect(&o, &a, &b) && o.left == 5 && o.top == 5 && o.right == 10 && o.bottom == 10);
    CHECK(!IntersectRect(&o, &a, &c) && IsRectEmpty(&o));
    CHECK(UnionRect(&o, &a, &c) && o.left == 0 && o.top == 0 && o.right == 40 && o.bottom == 40);
    CHECK(EqualRect(&a, &a) && !EqualRect(&a, &b));
}
