// macOS's trackpad input for the SDL program (mac_input.h). Built without ARC (CMake's default for .mm files).

#import <Cocoa/Cocoa.h>

#include "mac_input.h"

namespace {
void (*gSmartZoom)() = nullptr;
void (*gSwipe)(int) = nullptr;
bool gScrollContinues = false;
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
                                                                gScrollContinues = !starts && (p != NSEventPhaseNone || m != NSEventPhaseNone);
                                                                return event;
                                                            }] retain];
}

bool macLastScrollContinues() { return gScrollContinues; }
