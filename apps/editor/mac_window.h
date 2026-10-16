#pragma once
// macOS's window and document look for the SDL program (mac_window.mm; sdl_window.inc calls these). On Windows and
// Linux each function is empty (or says "no"): those systems keep their own look.
//
// - macSetupWindow: full screen with the green button and Split View (the window's collection behavior).
// - macSetWindowDocument: the title bar's file (representedURL: the proxy icon; Cmd+click shows the folders) and the
//   edited dot (documentEdited) of the open map or project.
// - macSetWindowDark: the title bar and the sheets in the program's theme (the window's appearance).
// - macNoteRecent, macClearRecents: the system's recent documents (the Dock's menu, Apple menu > Recent Items).
// - macSystemDark, macSystemAccent, macSidebarColor: the system's appearance, accent color and sidebar color;
//   macWatchLook starts watching them, macLookChanged is true once after each change (then read them again).
// - macBeginSheet: an alert sheet (NSAlert) on the window; `done` gets the index of the button that closed it.
// - macWindowReport: what the window has now (the --check window option).
// The texts are UTF-8. The functions run on the UI thread.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct SDL_Window;

// An alert: the message (bold), the text under it, the style and the buttons in the alert's order (the first is the
// default one, at the right). keys: each button's key ("" none, "\r" Return, "\x1b" Escape, a letter: Cmd+letter).
struct MacAlert {
    std::string message, info;
    int style = 0;  // 0 information, 1 warning, 2 critical (an error)
    std::vector<std::string> buttons, keys;
};

#ifdef __APPLE__
void macSetupWindow(SDL_Window* w);
void macSetWindowDocument(SDL_Window* w, const std::string& path, bool edited);
void macSetWindowDark(SDL_Window* w, bool dark);
void macNoteRecent(const std::string& path);
void macClearRecents();
bool macSystemDark();
bool macSystemAccent(uint8_t& r, uint8_t& g, uint8_t& b);
bool macSidebarColor(bool dark, uint8_t& r, uint8_t& g, uint8_t& b);
void macWatchLook();
bool macLookChanged();
bool macBeginSheet(SDL_Window* w, const MacAlert& alert, std::function<void(int)> done);
std::string macWindowReport(SDL_Window* w);
#else
inline void macSetupWindow(SDL_Window*) {}
inline void macSetWindowDocument(SDL_Window*, const std::string&, bool) {}
inline void macSetWindowDark(SDL_Window*, bool) {}
inline void macNoteRecent(const std::string&) {}
inline void macClearRecents() {}
inline bool macSystemDark() { return true; }
inline bool macSystemAccent(uint8_t&, uint8_t&, uint8_t&) { return false; }
inline bool macSidebarColor(bool, uint8_t&, uint8_t&, uint8_t&) { return false; }
inline void macWatchLook() {}
inline bool macLookChanged() { return false; }
inline bool macBeginSheet(SDL_Window*, const MacAlert&, std::function<void(int)>) { return false; }
inline std::string macWindowReport(SDL_Window*) { return "not macOS"; }
#endif
