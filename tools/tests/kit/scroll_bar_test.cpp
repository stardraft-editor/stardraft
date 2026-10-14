#include <vector>

#include "scroll_bar.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

KIT_TEST(scroll_bar_thumb_follows_position) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar;
    root.add(bar, {0, 0, 12, 200});
    bar.setRange(1000, 200);  // the thumb is the visible fifth of the track: 40 px; the travel is 160 px
    CHECK(bar.scrollable());
    CHECK(bar.maxPos() == 800);
    CHECK(bar.thumb().top == 0 && bar.thumb().bottom == 40);
    CHECK(bar.thumb().left == 2 && bar.thumb().right == 10);  // inset 2 px from each edge
    bar.setPos(800);
    CHECK(bar.thumb().top == 160 && bar.thumb().bottom == 200);
    bar.setPos(5000);  // clamped
    CHECK(bar.pos() == 800);
    bar.setPos(-3);
    CHECK(bar.pos() == 0);
}

KIT_TEST(scroll_bar_thumb_minimum_length) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar;
    root.add(bar, {0, 0, 12, 200});
    bar.setRange(100000, 200);  // the visible share is under a pixel: the thumb keeps the minimum (24 px)
    CHECK(bar.thumb().bottom - bar.thumb().top == 24);
}

KIT_TEST(scroll_bar_rest_is_thin_and_lit_is_wide) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar;
    root.add(bar, {0, 0, 12, 200});
    bar.setRange(1000, 200);
    auto rest = host.paint();
    CHECK(rest.drew("roundBox 6,0,10,40 "));  // the far half of the thumb only
    CHECK(!rest.drew("fill 0,0,12,200"));     // no track at rest
    host.move(5, 50);
    CHECK(bar.lit());
    auto lit = host.paint();
    CHECK(lit.drew("roundBox 2,0,10,40 "));  // the full thumb
    CHECK(lit.drew("fill 0,0,12,200"));      // and its track
    host.move(300, 300);  // the mouse leaves: thin again
    CHECK(!bar.lit());
    CHECK(host.paint().drew("roundBox 6,0,10,40 "));
}

KIT_TEST(scroll_bar_click_track_pages) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar;
    root.add(bar, {0, 0, 12, 200});
    bar.setRange(1000, 200);
    std::vector<int> got;
    bar.onScroll = [&](int p) { got.push_back(p); };
    host.click(5, 100);  // below the thumb: one page down
    host.click(5, 10);   // above the thumb (now at 40..80): one page up... to 0
    CHECK(got.size() == 2 && got[0] == 200 && got[1] == 0);
    bar.setPos(790);  // the thumb is at 158..198: a click at 199 is on the track below it
    host.click(5, 199);
    CHECK(bar.pos() == 800);  // a page would go past the end: clamped
    CHECK(got.size() == 3 && got[2] == 800);
    host.click(5, 199);  // the thumb is at 160..200 now: a click on it is a drag that moves nothing
    CHECK(got.size() == 3);
}

KIT_TEST(scroll_bar_drag_thumb) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar;
    root.add(bar, {0, 0, 12, 200});
    bar.setRange(1000, 200);
    std::vector<int> got;
    bar.onScroll = [&](int p) { got.push_back(p); };
    host.down(5, 20);  // on the thumb (0..40): a drag, not a page
    CHECK(got.empty());
    host.move(5, 60);  // the thumb's start moves to 40: 40 * 800 / 160
    CHECK(bar.pos() == 200);
    host.move(5, 500);  // past the end: stops at the end
    CHECK(bar.pos() == 800);
    host.move(5, -100);  // past the start
    CHECK(bar.pos() == 0);
    host.move(5, 60);
    host.up(5, 60);  // let go
    CHECK(got.back() == 200);
    const size_t n = got.size();
    host.move(5, 150);  // no drag any more
    CHECK(got.size() == n);
}

KIT_TEST(scroll_bar_setpos_does_not_report) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar;
    root.add(bar, {0, 0, 12, 200});
    bar.setRange(1000, 200);
    int calls = 0;
    bar.onScroll = [&](int) { ++calls; };
    bar.setPos(400);
    CHECK(calls == 0 && bar.pos() == 400);
}

KIT_TEST(scroll_bar_not_scrollable_takes_nothing) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar;
    root.add(bar, {0, 0, 12, 200});
    bar.setRange(100, 200);  // all visible
    CHECK(!bar.scrollable());
    CHECK(bar.thumb().empty());
    int calls = 0;
    bar.onScroll = [&](int) { ++calls; };
    CHECK(!host.router.mouse({draw::MouseKind::Down, {5, 100}, 0, 0, false, 0}));
    host.click(5, 100);
    CHECK(calls == 0);
    auto c = host.paint();
    CHECK(!c.drew("roundBox") && !c.drew("fill 0,0,12,200"));
}

KIT_TEST(scroll_bar_right_button_is_ignored) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar;
    root.add(bar, {0, 0, 12, 200});
    bar.setRange(1000, 200);
    int calls = 0;
    bar.onScroll = [&](int) { ++calls; };
    host.down(5, 100, 1);
    host.up(5, 100, 1);
    CHECK(calls == 0 && bar.pos() == 0);
}

KIT_TEST(scroll_bar_horizontal) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar(kit::Axis::Horizontal);
    root.add(bar, {0, 300, 200, 312});  // 200 wide, 12 tall
    bar.setRange(1000, 200);
    CHECK(bar.thumb().left == 0 && bar.thumb().right == 40);
    CHECK(bar.thumb().top == 2 && bar.thumb().bottom == 10);
    std::vector<int> got;
    bar.onScroll = [&](int p) { got.push_back(p); };
    host.click(100, 306);  // right of the thumb: a page on
    CHECK(got.size() == 1 && got[0] == 200);
    host.down(20, 306);  // the thumb is at 40..80 now; 20 is left of it: a page back
    host.up(20, 306);
    CHECK(got.size() == 2 && got[1] == 0);
    host.down(10, 306);  // on the thumb at 0..40: a drag
    host.move(170, 306);  // the thumb's start moves 160 px: to the end
    CHECK(bar.pos() == 800);
    host.up(170, 306);
}

KIT_TEST(scroll_bar_wheel_is_not_taken) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollBar bar;
    root.add(bar, {0, 0, 12, 200});
    bar.setRange(1000, 200);
    host.wheel(5, 5, -1);
    CHECK(bar.pos() == 0);
}
