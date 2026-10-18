// Compiles the editor's portable core (core.inc) with no Win32 headers off Windows (only apps/common/wintypes.h's value
// types). Nothing runs: the build is the check. CMake target editor_core_check; CI builds it on Linux and macOS.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
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
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <atomic>
#include <climits>
#include <thread>
#include <unordered_map>

#include <mutex>

#include "tr.h"
#include "text_codec.h"
#include "http.h"  // apps/common: the GitHub requests (core/releases.inc)
#include "update.h"  // apps/common: the self-update's file steps (core/releases.inc)
#include "crash_report.h"  // apps/common: sending crash reports (core/releases.inc)
#include "text_field.h"  // the kit's text field (the tool strip's number edit); outside core.inc's namespace
#include "view.h"
#include "../shell.h"
#include "wintypes.h"
#include "canvas.h"
#include "view.h"
#include "text_field.h"
#include "rich_field.h"
#include "dropdown.h"
#include "list_view.h"
#include "check_box.h"
#include "slider.h"
#include "button.h"
#include "dialog.h"
#include "label.h"
#include "link.h"
#include "progress_bar.h"
#include "icon_button.h"
#include "tree_view.h"
#include "scroll_view.h"
#include "html_view.h"
#include "../shell.h"
#include "../res/ui_icons.h"
#include "stardraft/commands.h"
#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/dat_units.h"  // the converted values on the Data form (core/data_model.inc)
#include "stardraft/help.h"  // the help pages (core/views_help.inc)
#include "stardraft/campaign.h"
#include "stardraft/project_import.h"
#include "stardraft/editor.h"
#include "stardraft/eud_project.h"
#include "stardraft/eptrace.h"  // epTrace's report (core/project_page.inc)
#include "stardraft/game_data.h"
#include "stardraft/pathing.h"
#include "stardraft/pathing_regions.h"
#include "stardraft/portraits.h"  // the briefing's portraits (core/views_briefing.inc)
#include "stardraft/previews.h"  // the tile pictures (core/views_tiles.inc)
#include "stardraft/portraits.h"  // the portraits' clips (core/data_pictures.inc)
#include "stardraft/sprites.h"  // the image player (core/data_pictures.inc)
#include "stardraft/scene.h"
#include "stardraft/settings.h"
#include "stardraft/hd_assets.h"
#include "stardraft/msqc.h"
#include "stardraft/png_writer.h"  // File > Export map image (core/map_image.inc)
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/wireframes.h"
#include "stardraft/terrain_gen.h"
#include "stardraft/triggers.h"

using namespace stardraft;
namespace fs = std::filesystem;

// (A named namespace: the core declares editor functions and globals that only the program (main_sdl.cpp) defines; this object is never
// linked, and MSVC refuses undefined declarations with internal linkage.)
namespace core_check {
#include "core.inc"
}  // namespace core_check
