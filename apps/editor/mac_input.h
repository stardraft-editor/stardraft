#pragma once
// macOS's trackpad input that SDL does not give (mac_input.mm, only on macOS): a monitor of the application's events
// gives the program the smart zoom (a double tap with two fingers) and the swipes of "Swipe between pages" (three
// fingers, as System Settings > Trackpad sets them), and records each scroll's phase, before SDL handles the event.
// The functions run on the UI thread.

// Starts the monitor (once): `smartZoom` runs for each smart zoom; `swipe` for each swipe to the left or right (-1: the
// fingers moved to the left, the next tab; 1: to the right, the tab before), as Safari goes between its pages.
void macWatchGestures(void (*smartZoom)(), void (*swipe)(int dir));
// Whether the scroll event that SDL gives now (its SDL_MouseWheelEvent's x and y) continues a trackpad's scroll: its
// fingers moved on, or its momentum after they lifted. This is that event's NSEvent phase and momentumPhase, not the
// last event's (SDL handles all the waiting events before it gives the first). False for a new scroll and for a mouse
// wheel's notches.
bool macScrollContinues(float x, float y);
