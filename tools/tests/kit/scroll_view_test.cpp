#include "scroll_view.h"

#include "button.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

namespace {

// Content of a size the test sets; counts its paints.
class Block : public draw::View {
public:
    void paint(draw::Canvas&) override { ++paints; }
    int paints = 0;
};

// A wheel notch sent to the host's router; returns whether it was used.
bool wheel(FakeHost& h, int x, int y, float notches, bool horizontal = false, unsigned mods = 0) {
    draw::MouseEvent e;
    e.kind = draw::MouseKind::Wheel, e.pt = {x, y}, e.wheel = notches, e.horizontal = horizontal, e.mods = mods;
    return h.router.mouse(e);
}

}  // namespace

// A 200 x 100 view with content 180 x 500: a vertical bar, a viewport 188 x 100, 400 px to scroll.
KIT_TEST(scroll_range_and_bar) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    CHECK(sv.hasVerticalBar());
    CHECK(!sv.hasHorizontalBar());
    CHECK(sv.viewportWidth() == 188 && sv.viewportHeight() == 100);
    CHECK(content.width() == 188);  // as wide as the visible part
    CHECK(sv.maxOffsetY() == 400 && sv.maxOffsetX() == 0);
    CHECK(content.frame().top == 0);

    sv.setOffset(0, 9999);
    CHECK(sv.offsetY() == 400);
    sv.setOffset(0, -50);
    CHECK(sv.offsetY() == 0);
    sv.scrollBy(0, 150);
    CHECK(sv.offsetY() == 150);
    CHECK(content.frame().top == -150);  // the content moves up under the viewport

    // The bar is beside the viewport: a click on it does not reach the content.
    host.paint();
    CHECK(host.paint().drew("view 0,0,188,100"));
    CHECK(host.paint().drew("fill 188,0,200,100") == false);  // at rest, no track
}

KIT_TEST(scroll_fits_without_bar) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 60);
    sv.setContent(&content);
    CHECK(!sv.hasVerticalBar() && !sv.hasHorizontalBar());
    CHECK(sv.viewportWidth() == 200 && sv.viewportHeight() == 100);
    CHECK(sv.maxOffsetY() == 0);
    sv.scrollBy(0, 40);
    CHECK(sv.offsetY() == 0);  // nothing to scroll
    sv.setContent(nullptr);
    CHECK(sv.content() == nullptr && !sv.hasVerticalBar());
}

KIT_TEST(scroll_wheel) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    // Three rows of 26 px a notch; positive is up, towards the top.
    CHECK(wheel(host, 10, 10, -1));
    CHECK(sv.offsetY() == 78);
    CHECK(wheel(host, 10, 10, -0.5f));
    CHECK(sv.offsetY() == 117);
    CHECK(wheel(host, 10, 10, 1));
    CHECK(sv.offsetY() == 39);
    CHECK(wheel(host, 10, 10, 1));
    CHECK(sv.offsetY() == 0);
    CHECK(!wheel(host, 10, 10, 1));  // at the top: not used, the view above may take it
    for (int i = 0; i < 10; ++i) wheel(host, 10, 10, -1);
    CHECK(sv.offsetY() == 400);  // stops at the end
    CHECK(!wheel(host, 10, 10, -1));
    CHECK(content.frame().top == -400);
}

KIT_TEST(scroll_wheel_horizontal) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(300, 500);
    sv.setHorizontal(true);
    sv.setContent(&content);
    // A horizontal bar too: the viewport is 188 x 88 and the content keeps its width.
    CHECK(sv.hasHorizontalBar() && sv.hasVerticalBar());
    CHECK(sv.viewportWidth() == 188 && sv.viewportHeight() == 88);
    CHECK(content.width() == 300);
    CHECK(sv.maxOffsetX() == 112 && sv.maxOffsetY() == 412);
    CHECK(wheel(host, 10, 10, -1, false, draw::kShift));  // shift: across
    CHECK(sv.offsetX() == 78 && sv.offsetY() == 0);
    CHECK(wheel(host, 10, 10, 1, true));  // a horizontal wheel, positive to the right
    CHECK(sv.offsetX() == 112);
    CHECK(wheel(host, 10, 10, -1));
    CHECK(sv.offsetY() == 78 && sv.offsetX() == 112);
}

KIT_TEST(scroll_wheel_on_vertical_only_view) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 60);  // fits: no vertical room
    sv.setContent(&content);
    CHECK(!wheel(host, 10, 10, -1));  // nothing to scroll: the wheel is not used
    CHECK(!wheel(host, 10, 10, -1, false, draw::kShift));
    CHECK(sv.offsetX() == 0 && sv.offsetY() == 0);
}

KIT_TEST(scroll_keeps_place_when_content_grows_and_shrinks) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    sv.setOffset(0, 200);
    content.setSize(180, 800);  // taller: the position stays
    sv.refresh();
    CHECK(sv.offsetY() == 200);
    CHECK(content.frame().top == -200);
    CHECK(sv.maxOffsetY() == 700);
    content.setSize(180, 250);  // shorter than the place: clamped to the new end
    host.paint();
    CHECK(sv.offsetY() == 150);
    CHECK(content.frame().top == -150);
    content.setSize(180, 60);  // fits: no bar, no position
    host.paint();
    CHECK(sv.offsetY() == 0 && !sv.hasVerticalBar());
}

KIT_TEST(scroll_resize_view) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    sv.setOffset(0, 400);
    root.add(sv, {0, 0, 200, 300});  // taller view: the content fits more
    CHECK(sv.viewportHeight() == 300);
    CHECK(sv.maxOffsetY() == 200);
    CHECK(sv.offsetY() == 200);  // still at the end
}

KIT_TEST(scroll_to_rect) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    sv.scrollTo(draw::Rect{0, 150, 10, 170});  // below the view: its bottom at the view's bottom
    CHECK(sv.offsetY() == 70);
    sv.scrollTo(draw::Rect{0, 0, 10, 20});  // above it: its top at the view's top
    CHECK(sv.offsetY() == 0);
    sv.scrollTo(draw::Rect{0, 300, 10, 310});
    CHECK(sv.offsetY() == 210);
    sv.scrollTo(draw::Rect{0, 250, 10, 260});  // already in view: no change
    CHECK(sv.offsetY() == 210);
    sv.scrollTo(draw::Rect{0, 100, 10, 400});  // taller than the view: its top
    CHECK(sv.offsetY() == 100);
    sv.scrollTo(draw::Rect{0, 480, 10, 600});  // past the end: clamped
    CHECK(sv.offsetY() == 400);
}

KIT_TEST(scroll_to_child_view) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    Block row;
    content.add(row, {0, 250, 50, 270});
    sv.scrollTo(row);
    CHECK(sv.offsetY() == 170);  // its bottom at the bottom of the view
    CHECK(row.origin().y == 80);  // 250 - 170 below the view's top
    Block other;  // not in the content: nothing
    sv.scrollTo(other);
    CHECK(sv.offsetY() == 170);
}

KIT_TEST(scroll_keys) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    CHECK(sv.focusable());  // nothing inside takes the focus: the view takes the keys
    host.press(draw::Key::Tab);
    CHECK(host.router.focused() == &sv);
    CHECK(host.press(draw::Key::Down));
    CHECK(sv.offsetY() == 26);
    CHECK(host.press(draw::Key::PageDown));
    CHECK(sv.offsetY() == 100);  // one page: the viewport less a row
    CHECK(host.press(draw::Key::End));
    CHECK(sv.offsetY() == 400);
    CHECK(!host.press(draw::Key::End));  // already there
    CHECK(host.press(draw::Key::PageUp));
    CHECK(sv.offsetY() == 326);
    CHECK(host.press(draw::Key::Home));
    CHECK(sv.offsetY() == 0);
    CHECK(!host.press(draw::Key::Up));
    CHECK(!host.press(draw::Key::Left));  // no horizontal bar
    CHECK(!host.press(draw::Key::Down, draw::kCtrl));
    CHECK(sv.offsetY() == 0);
}

KIT_TEST(scroll_keys_from_a_focused_button) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    kit::Button b(L"Edit", kit::Tone::Normal, nullptr);
    content.add(b, {0, 0, 80, 28});
    CHECK(!sv.focusable());  // a button inside takes the focus
    host.press(draw::Key::Tab);
    CHECK(host.router.focused() == &b);
    CHECK(host.press(draw::Key::Down));  // the button does not use the arrow: the view scrolls
    CHECK(sv.offsetY() == 26);
    CHECK(host.press(draw::Key::Space) == true);  // the button's own key stays the button's
}

KIT_TEST(scroll_drag_thumb) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    // The track is the view's height less the inset (2 px each end); the thumb is 24 px (the minimum).
    // Its start moves with the mouse: 40 px down the track is 40 * 400 / 72 of the content, rounded.
    host.down(195, 10);  // on the thumb (at 2..26), 8 px from its start
    host.move(195, 50);
    CHECK(sv.offsetY() == 222);
    host.move(195, 500);  // beyond the end: the end
    CHECK(sv.offsetY() == 400);
    host.up(195, 500);
    host.move(195, 20);  // released: no drag
    CHECK(sv.offsetY() == 400);
}

KIT_TEST(scroll_bar_track_pages) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    host.click(195, 90);  // the strip below the thumb: a page down
    CHECK(sv.offsetY() == 100);
    host.click(195, 5);  // the strip above the thumb (now at 20..44): a page up
    CHECK(sv.offsetY() == 0);
    host.click(195, 90);
    CHECK(sv.offsetY() == 100);
    host.click(195, 90);
    CHECK(sv.offsetY() == 200);
}

KIT_TEST(scroll_bar_paints_with_hover) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    // At rest: the thumb is half as thick (3 px) at the right edge of the strip (188..200), no track.
    CHECK(host.paint().drew("roundBox 194,2,197,26"));
    CHECK(!host.paint().drew("fill 188,0,200,100"));
    host.move(195, 10);  // the mouse on the strip: the track shows, and the thumb is wider
    const auto c = host.paint();
    CHECK(c.drew("fill 188,0,200,100"));  // lit: the track
    CHECK(c.drew("roundBox 191,2,197,26"));  // and the full thumb
}

KIT_TEST(scroll_horizontal_bar_drag) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {0, 0, 200, 100});
    Block content;
    content.setSize(300, 500);
    sv.setHorizontal(true);
    sv.setContent(&content);
    // The bar under the viewport (188 x 88): a track 188 long, less the insets (184), a thumb of 188 / 300 of it
    // (115), 112 px to scroll. The thumb starts 8 px from the grab; the mouse moves it 64 px: the start is 66 px into
    // the track, which is 64 px past the inset, so 64 * 112 / 69 = 104.
    host.down(10, 95);
    host.move(74, 95);
    CHECK(sv.offsetX() == 104);
    CHECK(sv.offsetY() == 0);
    host.up(74, 95);
    host.move(10, 95);
    CHECK(sv.offsetX() == 104);  // released
}

KIT_TEST(scroll_view_clips_its_content) {
    draw::View root;
    FakeHost host(root);
    kit::ScrollView sv;
    root.add(sv, {10, 10, 210, 110});
    Block content;
    content.setSize(180, 500);
    sv.setContent(&content);
    const auto c = host.paint();
    // The viewport is the content's clip: a view of 188 x 100 at (0, 0) in the view.
    CHECK(c.drew("view 10,10,198,110"));
    CHECK(content.paints == 1);
}
