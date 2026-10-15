// Compiles the editor's portable core (core.inc) with no Win32 headers off Windows (only apps/common/wintypes.h's value
// types). Nothing runs: the build is the check. CMake target editor_core_check; CI builds it on Linux and macOS.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "tr.h"
#include "text_codec.h"
#include "text_field.h"  // the kit's text field (the tool strip's number edit); outside core.inc's namespace
#include "view.h"
#include "../shell.h"
#include "wintypes.h"
#include "canvas.h"
#include "view.h"
#include "text_field.h"
#include "dropdown.h"
#include "list_view.h"
#include "check_box.h"
#include "slider.h"
#include "button.h"
#include "dialog.h"
#include "label.h"
#include "icon_button.h"
#include "../shell.h"
#include "../res/ui_icons.h"
#include "stardraft/commands.h"
#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/editor.h"
#include "stardraft/eud_project.h"
#include "stardraft/game_data.h"
#include "stardraft/pathing.h"
#include "stardraft/previews.h"  // the tile pictures (core/views_tiles.inc)
#include "stardraft/scene.h"
#include "stardraft/settings.h"
#include "stardraft/hd_assets.h"
#include "stardraft/tileset.h"
#include "stardraft/terrain_gen.h"

using namespace stardraft;
namespace fs = std::filesystem;

// (A named namespace: the core declares editor functions and globals that only main.cpp defines; this object is never
// linked, and MSVC refuses undefined declarations with internal linkage.)
namespace core_check {
#include "core.inc"
}  // namespace core_check
