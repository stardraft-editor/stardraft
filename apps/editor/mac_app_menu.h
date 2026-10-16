#pragma once
// The trackpad's scrolls for the SDL program on macOS (mac_app_menu.mm). The menus are in mac_menus.mm.

// Whether the last scroll event was a trackpad's (or a Magic Mouse's) precise scroll, not a wheel's notches (NSEvent's
// hasPreciseScrollingDeltas, which SDL's wheel events do not carry). A monitor of the application's scroll events
// records it, before SDL handles each one; macWatchScrolls starts it (once).
void macWatchScrolls();
bool macLastScrollPrecise();
