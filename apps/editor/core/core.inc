// The editor's portable core: the parts of apps/editor that compile without Win32 (research/ports/MAC-BUILD-PLAN.md).
// main.cpp includes these files where they always were; core_check.cpp compiles them on every system without Win32
// headers, so a Win32 call that comes into the core breaks the build at once (the ratchet). A file moves in here when it
// has no Win32 call left; it never moves back.

#include "strings.inc"
#include "colors.inc"
