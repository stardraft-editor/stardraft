#pragma once
// What the macOS app gives the system (mac_dist.mm): the tile colors for the Quick Look extensions
// (apps/quicklook), the maps in Spotlight, and the app bundle's report (--check bundle). On Windows and Linux the
// functions do nothing (and the reports say so), so the SDL program calls them on every system.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifdef __APPLE__

// A map that opened in the editor: its tileset's tile colors go to the Quick Look extensions' folder
// (preview::tileColorsFolder(), when they are not there or are different), then Spotlight indexes the map's name, its
// description and its picture, on a queue of its own. `tileColors`: preview::tileColors of the map's tileset (empty:
// no game data, no table).
void macMapOpened(const std::filesystem::path& map, const std::vector<uint32_t>& tileColors, uint16_t era);

// A Spotlight result that the user opens starts (or goes to) StarDraft with the map: `open` gets its path, on the UI
// thread. Once, before the window shows.
void macHandleSpotlightOpens(void (*open)(const std::string& path));

// --check spotlight: the map's Spotlight entry is made, indexed and removed again; the lines to print. Waits for
// Spotlight (at most 20 seconds).
std::string macSpotlightCheck(const std::filesystem::path& map);

// --check bundle: the app's Info.plist (document types, exported types), its plug-ins (the Quick Look extensions)
// and its signature (hardened runtime, entitlements), as lines to print.
std::string macBundleReport();

#else

inline void macMapOpened(const std::filesystem::path&, const std::vector<uint32_t>&, uint16_t) {}
inline void macHandleSpotlightOpens(void (*)(const std::string&)) {}
inline std::string macSpotlightCheck(const std::filesystem::path&) { return "spotlight: not macOS\n"; }
inline std::string macBundleReport() { return "bundle: not macOS\n"; }

#endif
