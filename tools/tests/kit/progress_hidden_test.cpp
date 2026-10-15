#include "kit_check.h"
#include "progress_bar.h"

using namespace stardraft;
using namespace stardraft::kit::test;

KIT_TEST(progress_busy_hidden_bar_stops_its_timer) {
    draw::View root;
    FakeHost host(root);
    kit::ProgressBar p;
    root.add(p, {0, 0, 200, 16});
    p.setBusy(true);
    CHECK(host.timers.size() == 1);
    p.setVisible(false);  // hidden while busy: the next step finds it off the screen and does not go on
    host.runTimers();
    CHECK(host.timers.empty());
    host.runTimers();
    CHECK(host.timers.empty());
    // Shown again: the animation starts again with the paint.
    p.setVisible(true);
    host.paint();
    CHECK(host.timers.size() == 1);
}

KIT_TEST(progress_busy_bar_in_a_hidden_parent_stops_its_timer) {
    draw::View root;
    FakeHost host(root);
    draw::View holder;
    root.add(holder, {0, 0, 300, 40});
    kit::ProgressBar p;
    holder.add(p, {0, 0, 200, 16});
    p.setBusy(true);
    CHECK(host.timers.size() == 1);
    holder.setVisible(false);  // the parent is hidden: the bar is not shown either
    host.runTimers();
    CHECK(host.timers.empty());
    holder.setVisible(true);
    host.paint();
    CHECK(host.timers.size() == 1);
    p.setBusy(false);
    CHECK(host.timers.empty());
}
