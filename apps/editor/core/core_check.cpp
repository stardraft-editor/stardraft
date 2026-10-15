// Compiles the editor's portable core (core.inc) with no Win32 headers off Windows (only apps/common/wintypes.h's value
// types). Nothing runs: the build is the check. CMake target editor_core_check; CI builds it on Linux and macOS.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>

#include "text_codec.h"
#include "wintypes.h"
#include "../res/ui_icons.h"

#include "core.inc"
