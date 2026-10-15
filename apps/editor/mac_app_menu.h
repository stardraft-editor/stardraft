#pragma once
// macOS's application menu (the menu with the program's name) for the SDL program: SDL makes it (About, Services, Hide,
// Quit); macSetupAppMenu makes its About open the program's About window and adds Program options (Cmd+,). Quit (Cmd+Q)
// stays SDL's: it is the system's quit request, which asks about unsaved changes (SdlHost::setQuitRequest). The items
// Close (Cmd+W) and Minimize (Cmd+M) of SDL's Window menu lose their keys: the program's own menus use those keys.
// The texts are UTF-8. The functions run on the UI thread, while SDL handles events. Only on macOS (mac_app_menu.mm).

#include <string>

void macSetupAppMenu(const std::string& aboutText, const std::string& settingsText, void (*about)(), void (*settings)());
