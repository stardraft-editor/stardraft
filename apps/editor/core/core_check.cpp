// Compiles the editor's portable core (core.inc) with no Win32 headers off Windows (only apps/common/wintypes.h's value
// types). Nothing runs: the build is the check. CMake target editor_core_check; CI builds it on Linux and macOS.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "text_codec.h"
#include "view.h"
#include "../shell.h"
#include "wintypes.h"
#include "../res/ui_icons.h"
#include "stardraft/commands.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace fs = std::filesystem;

// (A named namespace: the core declares editor functions and globals that only main.cpp defines; this object is never
// linked, and MSVC refuses undefined declarations with internal linkage.)
namespace core_check {
#include "core.inc"
}  // namespace core_check
