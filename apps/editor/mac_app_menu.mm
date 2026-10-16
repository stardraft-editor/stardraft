// The trackpad's scrolls for the SDL program on macOS (mac_app_menu.h). Built without ARC (CMake's default for .mm files).

#import <Cocoa/Cocoa.h>

#include "mac_app_menu.h"

namespace {
bool gLastPrecise = false;
id gScrollMonitor = nil;
}  // namespace

void macWatchScrolls() {
    if (gScrollMonitor) return;
    gScrollMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
                                                            handler:^NSEvent*(NSEvent* event) {
                                                                gLastPrecise = [event hasPreciseScrollingDeltas];
                                                                return event;
                                                            }] retain];
}

bool macLastScrollPrecise() { return gLastPrecise; }
