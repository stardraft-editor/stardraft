#include "kit_check.h"
#include "progress_bar.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static std::string C(draw::Color c) { return RecordingCanvas::c(c); }

// A bar 200 x 16 on the root: the track is 4 px high, centered (y 6 to 10).

KIT_TEST(progress_value_fills_the_track) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::ProgressBar p;
    root.add(p, {0, 0, 200, 16});
    CHECK(p.value() == 0.0f);
    CHECK(host.paint().drew("roundBox 0,6,200,10 " + C(th.ctl) + " " + C(th.border) + " 2 1"));
    CHECK(!host.paint().drew(C(th.accent) + " none 2 1"));  // nothing filled at 0
    p.setValue(0.5f);
    CHECK(host.paint().drew("roundBox 0,6,100,10 " + C(th.accent) + " none 2 1"));
    p.setValue(1.0f);
    CHECK(host.paint().drew("roundBox 0,6,200,10 " + C(th.accent) + " none 2 1"));
    CHECK(host.timers.empty());  // a value is not busy: no timer
}

KIT_TEST(progress_value_is_clamped_and_repaints) {
    draw::View root;
    FakeHost host(root);
    kit::ProgressBar p;
    root.add(p, {0, 0, 200, 16});
    p.setValue(2.0f);
    CHECK(p.value() == 1.0f);
    p.setValue(-1.0f);
    CHECK(p.value() == 0.0f);
    const int before = host.invalidations;
    p.setValue(0.0f);  // the same value: no repaint
    CHECK(host.invalidations == before);
    p.setValue(0.25f);
    CHECK(host.invalidations > before);
    CHECK(host.paint().drew("roundBox 0,6,50,10 "));
}

KIT_TEST(progress_busy_segment_runs_along_the_track) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::ProgressBar p;
    root.add(p, {0, 0, 200, 16});
    p.setBusy(true);
    CHECK(p.busy());
    CHECK(host.timers.size() == 1);  // one animation step is scheduled
    // At the start the segment (30 % of the track, 60 px) is off the left end; the clip keeps it on the track.
    auto c = host.paint();
    CHECK(c.drew("clip 0,6,200,10"));
    CHECK(c.drew("roundBox -60,6,0,10 " + C(th.accent) + " none 2 1"));
    CHECK(!c.drew("roundBox 0,6,100,10 " + C(th.accent)));  // a busy bar does not show a value
    // Each step moves the segment 0.02 of the way (260 px of travel: 5 px a step).
    host.runTimers();
    CHECK(host.timers.size() == 1);  // the next step
    CHECK(host.paint().drew("roundBox -55,6,5,10 " + C(th.accent) + " none 2 1"));
    for (int i = 0; i < 60; ++i) host.runTimers();  // more than a full run: it wraps around
    CHECK(host.timers.size() == 1);
    CHECK(host.paint().drew("roundBox "));
}

KIT_TEST(progress_busy_stops_and_restarts) {
    draw::View root;
    FakeHost host(root);
    kit::ProgressBar p;
    root.add(p, {0, 0, 200, 16});
    p.setBusy(true);
    p.setBusy(true);  // already busy: still one timer
    CHECK(host.timers.size() == 1);
    p.setBusy(false);
    CHECK(!p.busy() && host.timers.empty());  // the timer is cancelled
    host.runTimers();  // nothing runs
    CHECK(host.timers.empty());
    p.setValue(0.5f);
    CHECK(host.paint().drew("roundBox 0,6,100,10 "));
    p.setBusy(true);
    CHECK(host.timers.size() == 1);
    p.setBusy(false);
}

KIT_TEST(progress_destroyed_busy_cancels_timer) {
    draw::View root;
    FakeHost host(root);
    {
        kit::ProgressBar p;
        root.add(p, {0, 0, 200, 16});
        p.setBusy(true);
        CHECK(host.timers.size() == 1);
    }
    CHECK(host.timers.empty());
    host.runTimers();
}

KIT_TEST(progress_removed_while_busy_is_safe) {
    draw::View root;
    FakeHost host(root);
    auto* p = new kit::ProgressBar;
    root.add(*p, {0, 0, 200, 16});
    p->setBusy(true);
    root.remove(*p);  // out of the window: the pending step runs and finds no host, so it does not go on
    host.runTimers();
    CHECK(host.timers.empty());
    delete p;
    host.runTimers();  // the bar is gone: nothing to run
    CHECK(host.timers.empty());
}
