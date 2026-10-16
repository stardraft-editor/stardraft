// macOS's trackpad input for the SDL program (mac_input.h). Built without ARC (CMake's default for .mm files).

#import <Cocoa/Cocoa.h>

#include <cmath>
#include <cstddef>
#include <deque>

#include "mac_input.h"

namespace {
// One scroll event as SDL gives it (SDL_cocoamouse.m's Cocoa_HandleMouseWheel: its x and y) and its phase. SDL gives
// its wheel events after it handled all the waiting events, so each event's phase goes in this queue, in order.
struct ScrollPhase {
    float x, y;
    bool continues;
};
std::deque<ScrollPhase> gScrollPhases;
constexpr size_t kMaxScrollPhases = 256;  // (the oldest go: events that SDL did not give, for example in a dialog)
bool gStartPending = false;  // a scroll began with an event that SDL did not give (no distance): the next one begins it
void (*gSmartZoom)() = nullptr;
void (*gSwipe)(int) = nullptr;
id gGestureMonitor = nil;  // (kept for the program's life)
id gPhaseMonitor = nil;
}  // namespace

void macWatchGestures(void (*smartZoom)(), void (*swipe)(int dir)) {
    gSmartZoom = smartZoom, gSwipe = swipe;
    if (gGestureMonitor) return;
    gGestureMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskSmartMagnify | NSEventMaskSwipe)
                                                              handler:^NSEvent*(NSEvent* event) {
                                                                  if ([event type] == NSEventTypeSmartMagnify) {
                                                                      if (gSmartZoom) gSmartZoom();
                                                                  } else if ([event type] == NSEventTypeSwipe) {
                                                                      // (deltaX: 1 to go back, as Safari's; -1 to go on)
                                                                      const CGFloat dx = [event deltaX];
                                                                      if (gSwipe && dx != 0) gSwipe(dx > 0 ? 1 : -1);
                                                                  }
                                                                  return event;
                                                              }] retain];
    // Each scroll's phase, before SDL's view gets it: a scroll that goes on (its fingers or its momentum) continues.
    gPhaseMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
                                                            handler:^NSEvent*(NSEvent* event) {
                                                                const NSEventPhase p = [event phase], m = [event momentumPhase];
                                                                const bool starts = (p & (NSEventPhaseBegan | NSEventPhaseMayBegin)) != 0;
                                                                // SDL's x and y (its calculation), which find the
                                                                // event again when SDL gives it.
                                                                CGFloat x, y;
                                                                if ([event hasPreciseScrollingDeltas]) {
                                                                    x = -[event scrollingDeltaX] * 0.1f;
                                                                    y = [event scrollingDeltaY] * 0.1f;
                                                                } else {
                                                                    x = -[event deltaX], y = [event deltaY];
                                                                    x = x > 0 ? std::ceil(x) : x < 0 ? std::floor(x) : x;
                                                                    y = y > 0 ? std::ceil(y) : y < 0 ? std::floor(y) : y;
                                                                }
                                                                const float fx = float(x), fy = float(y);
                                                                if (fx == 0 && fy == 0) {  // (SDL gives no event)
                                                                    if (starts) gStartPending = true;
                                                                    return event;
                                                                }
                                                                if (gScrollPhases.size() >= kMaxScrollPhases) gScrollPhases.pop_front();
                                                                const bool goesOn = p != NSEventPhaseNone || m != NSEventPhaseNone;
                                                                gScrollPhases.push_back({fx, fy, !starts && !gStartPending && goesOn});
                                                                gStartPending = false;
                                                                return event;
                                                            }] retain];
}

bool macScrollContinues(float x, float y) {
    // The first event in the queue with SDL's values; the events before it did not get to SDL.
    for (size_t i = 0; i < gScrollPhases.size(); ++i)
        if (gScrollPhases[i].x == x && gScrollPhases[i].y == y) {
            const bool continues = gScrollPhases[i].continues;
            gScrollPhases.erase(gScrollPhases.begin(), gScrollPhases.begin() + std::ptrdiff_t(i) + 1);
            return continues;
        }
    return false;  // (not an event of the system: a new scroll)
}
