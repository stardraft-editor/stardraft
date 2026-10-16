#pragma once

// The free desktops' parts (Linux and the BSDs: the XDG specifications, desktop_xdg.cpp): the folders of the base
// directory specification, the desktop portal's appearance (dark or light, the accent color), desktop notifications, and
// the recently used files (~/.local/share/recently-used.xbel). D-Bus is libdbus-1, loaded when it is first needed
// (dlopen): a system without it, or without a session bus, gives no portal and no notifications, and nothing else changes.
// On Windows and macOS this file is not compiled.

#include <cstdint>
#include <filesystem>
#include <string>

namespace stardraft::xdg {

// The application's id (the .desktop file's name, the Wayland app id, the Flatpak id, the icon's name).
inline constexpr const char* kAppId = "io.github.stardraft_editor.StarDraft";

// $XDG_CONFIG_HOME, $XDG_DATA_HOME, $XDG_CACHE_HOME, $XDG_STATE_HOME: the variable when it is an absolute path, else
// ~/.config, ~/.local/share, ~/.cache, ~/.local/state (the specification: a relative path is not used). Empty with no
// HOME.
std::filesystem::path configHome();
std::filesystem::path dataHome();
std::filesystem::path cacheHome();
std::filesystem::path stateHome();

// The program runs as an AppImage ($APPIMAGE: its file) or in a Flatpak (/.flatpak-info). Such a program updates as
// its package does, not with the program's own update.
std::filesystem::path appImageFile();
bool inFlatpak();

// --- The desktop portal (org.freedesktop.portal.Settings, namespace org.freedesktop.appearance) ---

// color-scheme: 1 dark, 2 light, 0 no preference (or no portal).
int portalColorScheme();
// accent-color: the desktop's accent color (false: none set, or no portal).
bool portalAccent(uint8_t& r, uint8_t& g, uint8_t& b);
// Watches the portal's SettingChanged signal. lookChanged: true once after the appearance changed (polled each frame;
// it does not wait).
void watchLook();
bool lookChanged();

// --- Notifications (org.freedesktop.Notifications) ---

// A notification of this program (its .desktop file gives the icon and the name). False: no notification server.
bool notify(const std::string& summary, const std::string& body);

// --- Recently used files (the desktop bookmark specification) ---

// The file of the recently used files: $XDG_DATA_HOME/recently-used.xbel.
std::filesystem::path recentFile();
// Adds `file` (or updates its entry: the time, the count) to the recently used files in `xbel` with its MIME type and
// this program as the application that opened it. The file is written to a new file that then takes the old one's place.
bool addRecent(const std::filesystem::path& xbel, const std::filesystem::path& file, const std::string& mimeType, std::string& error);
// The MIME type of a map or project file by its extension (the types of packaging/linux/stardraft-mime.xml).
std::string mimeTypeOf(const std::filesystem::path& file);

// (The desktop's UI font is apps/kit_sdl/canvas_imgui.cpp's: fontconfig, with all of the kit's programs.)

// --- Checks ---

// What this system has (the session bus, the portal, the notification server), as lines "desktop: ...".
std::string report();

}  // namespace stardraft::xdg
