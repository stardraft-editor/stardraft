#pragma once
// The host seam (research/ports/MAC-BUILD-PLAN.md step 2): what the editor's code asks of the window system, as free
// functions. The portable code calls these; each host implements them. The host is shell_sdl.inc (with
// shell_sdl_win.inc for its Windows parts), included by main_sdl.cpp.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// The control kit's icon button: the start page's remove buttons (core/views_start.inc) need its declaration in every
// editor program, and this header is the one all three include before core.inc (the SDL program has no other use of it).
#include "icon_button.h"


// The panes are placed again for the window's size and the dock (computeLayout, core/layout.inc).
void shellRelayout();
// The menus and the top tool bar follow the state (enabled, checked, the Build label).
void shellMenusChanged();
// The status bar is drawn again (its texts changed).
void shellStatusChanged();
// Whether the Log panel's window is open (the Log shows the status bar's message then).
bool shellLogOpen();
// The UI font's LOGFONT height (negative: the character height), for the views' title sizes.
int shellUiFontLfHeight();
// The map view's client size, in device pixels.
void shellViewSize(int& w, int& h);
// The time the process was created, on the steady clock (the start-up marks count from it; the host's clock).
std::chrono::steady_clock::time_point shellProcessStart();
// The program's version name, as the title shows it ("0.5.0.6").
std::wstring shellVersion();
// When this .exe was built: "built Oct 9 23:48", to tell builds apart (empty when unknown).
std::wstring shellBuildStamp();
// The window's title text.
void shellSetTitle(const std::wstring& title);
// The workspace rail exists (it is made with the window), and its window is shown or hidden.
bool shellHasRail();
void shellShowRail(bool show);
// The wait cursor (the hourglass) is shown until the next message that sets the cursor.
void shellWaitCursor();
// The minimap's client size, in device pixels.
void shellMinimapSize(int& w, int& h);
// The host's renderer shows the active editor's sources (its quality, sprites and tileset).
void shellRendererSources();
// The document tab strip and the workspace rail are drawn again (their states changed).
void shellRedrawTabs();
void shellInvalidateTabs();
void shellInvalidateWorkspaceBar();
// The renderer loads the unit images again (the map's unit graphics changed: their glows and remapping).
void shellRendererReloadImages();
// The side bar's items are made now (Ctrl+1 to Ctrl+9 read them: railItemId).
void shellRailUpdateNow();
// The mouse over the map view, in its pixels; false when it is not over the map (Paste puts a location's copy in the
// middle of the view then).
bool shellViewMouse(int& x, int& y);
// The tab strip's native window is updated now (a tab moved under the mouse), for the view `v` that is in it (the
// including file has apps/common/view.h).
void shellUpdateTabs(stardraft::draw::View& v);
// The sprites that the units' animations hold for the editor are dropped (a closed editor's sprites).
void shellForgetAnims();
// Posts a WM_COMMAND with the command id to the main window (it runs after the current message).
void shellPostCommand(int id);
// The map view takes the keyboard focus.
void shellFocusView();
// The panel tabs' right-click menu: a grayed title line, then Left and Right (the side is checked). Returns 1 (Left),
// 2 (Right), or 0 when none is picked.
int shellTabSideMenu(const std::wstring& title, int side);
// Runs `fn` on the UI thread, later. Safe to call from any thread; `fn` runs once.
void shellPost(std::function<void()> fn);
// The map view's timer for a held spray (25 ms): on starts it, off stops it.
void shellSprayTimer(bool on);
// The map view asks for WM_MOUSELEAVE when the mouse enters it (the next move sets it up).
void shellTrackMouseLeave();
// Ctrl or Shift is down now (the keys as they are, not the message's): a location made by a release is inverted by it.
bool shellCtrlOrShiftDown();
// The palette's filter text is cleared (the palette shows all its entries). False when there is no filter field.
bool shellClearFilter();
// The window's dpi: scale() gives device pixels for it (96 is 100%).
unsigned shellDpi();
// Writes a line to the log (the status bar's message is logged too).
void shellLogLine(const std::wstring& line);
// Writes an error line to the log (in the error color where the log has colors).
void shellLogError(const std::wstring& line);
// Whether the status bar exists (updateStatus does nothing without it).
bool shellHasStatusBar();
// The sprites the renderer drew last frame (the status bar's last part).
int shellSpriteCount();
// The palette list: only row i is selected (-1: none), and its caret is on it.
void shellListSelect(int i);
// The palette list's selected rows (Locations: several can be selected); empty when none is.
std::vector<int> shellListSelection();
// The palette list's rows are replaced by these, and the list is drawn again.
void shellListFill(const std::vector<std::wstring>& rows);
// The palette search box's text (the Locations filter and the palette search).
std::wstring shellFilterText();
// The map view's window exists (the tool strip is laid out once it does).
bool shellViewShown();
// The tool strip shows the active mode's tools again.
void shellToolsChanged();
// A popup menu of `items` (the strip's brush shapes), with the entry `checked` marked; (x, y) is its place in the tool
// strip's coordinates. Returns the picked entry's index, or -1 when none is picked.
int shellPopupPick(const std::vector<std::wstring>& items, int checked, int x, int y);
// A context menu of `items` at the mouse (a right click's; the Win32 menu ends with Report feedback). Returns the picked
// entry's number from 1, or 0 when none is picked.
int shellContextMenu(const std::vector<std::wstring>& items);
// The owner (player) combo box shows player i.
void shellOwnerSelect(int i);
// The palette is drawn again (its selection or the player's colors changed).
void shellPaletteChanged();
// The palette's unit entries are made again (the mode or the list changed).
void shellPaletteRebuilt();
// The translation of an English text for the UI (ui_text.h's tr).
std::wstring shellTr(const wchar_t* english);
// The palette's picked values (Ctrl+click): the palette's own list, which the selection of several picks changes.
std::vector<int>& shellPaletteChosen();
// The folder of the unsaved projects (a new project nobody saved yet lives there); empty when the system has none.
std::filesystem::path shellUnsavedRoot();
// The settings file (%APPDATA%\StarDraft\settings.json; the environment variable STARDRAFT_SETTINGS gives another).
std::filesystem::path shellSettingsFile();
// A setting the core changed is saved now (the Windows host saves its options as they change, only what differs).
void shellSettingsChanged();
// The value kinds of the settings written by older builds (the registry: HKCU\Software\StarDraft).
enum class LegacyKind { Dword, Text, Texts, Bytes };
// The value `value` under the key `sub` ("" is the key itself; "Key/Sub" a subkey, with '/'): its bytes, when there is one.
bool shellLegacyValue(const std::wstring& sub, const std::wstring& value, LegacyKind kind, std::vector<uint8_t>& out);
// Appends the value names under the key `sub` that are not in `out` yet.
void shellLegacyNames(const std::wstring& sub, std::vector<std::wstring>& out);
// Deletes the value `value` under the key `sub`.
void shellLegacyRemove(const std::wstring& sub, const std::wstring& value);
// An error message box (the theme's message box) with the title.
void shellErrorBox(const std::wstring& text, const std::wstring& title);
// The same with the warning icon (a problem the user can correct: Save project's name).
void shellWarningBox(const std::wstring& text, const std::wstring& title);
// A canvas that measures text in the UI fonts outside a paint (the message box's and the tooltip's sizes); nullptr
// when the host has none (then they estimate from the font's letters). Windows: none (its boxes measure on a screen DC).
stardraft::draw::Canvas* shellMeasureCanvas();
// The question about changes that are not applied (text, title): Apply them, Do not apply them, or Cancel (stay). Returns 0
// (Apply), 1 (Do not apply) or 2 (Cancel).
int shellAskUnapplied(const std::wstring& text, const std::wstring& title);
// The File menu's recent list: its items are the labels (the first one is the newest), or `none` when there are none.
// projects: the Recent projects menu, else the Recent maps menu.
void shellRecentMenu(bool projects, const std::vector<std::wstring>& labels, const std::wstring& none);
// The quick picker (Quick open, or Replace with... for the selection).
void shellShowQuickPicker(bool replace, bool open);
// Whether the file system ignores case (Windows: a/B.scx and a/b.scx are one file), for comparing paths.
bool shellPathsFoldCase();
// A map file was opened by itself: asks once whether maps open with StarDraft 2 (Windows only).
void shellMapFileOpened(const std::filesystem::path& file);
// A web page is opened in the browser.
void shellOpenUrl(const std::wstring& url);
// A folder is opened in the file manager.
void shellOpenFolder(const std::filesystem::path& folder);
// A script is opened in the user's editor for it ("edit", else "open"; Notepad when nothing is registered for it).
void shellOpenInEditor(const std::filesystem::path& file);
// A text file is opened in Notepad.
void shellOpenWithNotepad(const std::filesystem::path& file);
// A file is shown selected in its folder.
void shellRevealFile(const std::filesystem::path& file);
// A file's last change, in local time ("2026-10-11 14:02"); empty when it is not found.
std::wstring shellFileTimeText(const std::filesystem::path& file);
// A program is started with administrator rights in `dir` (Windows: the elevation prompt), and its process is waited for.
// Returns the process's token, or 0 when it did not start: `error` then says why, naming `what` ("epTrace").
std::uintptr_t shellStartElevated(const std::filesystem::path& exe, const std::filesystem::path& dir, const std::wstring& args,
                                  const std::wstring& what, std::wstring& error);
// Whether the process (its token from shellStartElevated) has ended.
bool shellProcessEnded(std::uintptr_t process);
// The process's token is let go (it is not used again).
void shellProcessClose(std::uintptr_t process);
// This program's process id, and whether a process with that id still runs (the unsaved projects' folders of programs
// that ended go; a restart waits for the old program).
std::uint32_t shellProcessId();
bool shellProcessRunning(std::uint32_t pid);
// File > Options > StarCraft folder: another install is chosen (used from the next start).
void shellChooseInstallFolder();
// A folder picker with the title (File > New EUD project's Browse): the folder, or empty when none is picked.
std::wstring shellPickFolder(const std::wstring& title);
// The file that Build writes (a .scx), `name` the file name to start with: the file, or empty when none is picked.
std::wstring shellPickOutputFile(const std::wstring& name);
// The user's Documents folder (new projects go into its StarDraft folder by default); empty when the system has none.
std::filesystem::path shellDocumentsFolder();
// A file from the system's open or save dialog (the Project page's Link a file, Add a script, the background music, a
// file to load into memory): the file, or empty when none is picked. The call waits for the answer.
struct ShellFilePick {
    std::wstring title;
    std::vector<std::pair<std::wstring, std::wstring>> filters;  // (name, patterns): {L"epScript (*.eps)", L"*.eps"}; the first is chosen
    std::wstring fileName;             // the name in the name box at first (a save dialog)
    std::filesystem::path initialDir;  // the system's choice when empty
    std::wstring defaultExtension;     // added to a typed name without one ("eps")
    bool save = false;                 // a save dialog: the name may be of a new file (its folder must be there)
};
std::filesystem::path shellPickFile(const ShellFilePick& pick);
// A line of text typed in a small dialog with OK and Cancel (`text` holds what the field shows first). False: Cancel.
bool shellPromptText(const std::wstring& title, std::wstring& text);
// A value typed in the pop-out under `field` (a part of the view `v`, in its coordinates), as the Data form's values.
// False: Cancel.
bool shellEditValueBelow(stardraft::draw::View& v, stardraft::draw::Rect field, const std::wstring& title, std::string& text);
// The value pop-out of `spec` under `field` (a part of the view `v`, in its coordinates): a trigger value's number, text,
// code or text with values (core/views_value_pop.inc). True and `out` when applied.
struct ValuePopSpec;
bool shellEditValue(stardraft::draw::View& v, stardraft::draw::Rect field, const ValuePopSpec& spec, std::string& out);
// A text the game shows (its bytes, with the color codes) in the value pop-out's game text mode (the colors, Preview
// and Raw) under `field`. False: Cancel, or the same text.
bool shellEditGameText(stardraft::draw::View& v, stardraft::draw::Rect field, const std::wstring& title, std::string& text);
// Writes an error line to the log (the Windows log shows it in the error color).
void shellLogError(const std::wstring& line);
// A file's bytes as text in the system's code page (Windows: the ANSI code page, as the RichEdit read a script; the SDL
// program: UTF-8, else Korean CP949, else Windows-1252: text_codec.h widenLegacy).
std::wstring shellCodePageText(const std::string& bytes);
// The StarCraft folder that StarDraft reads game data from, as saved; empty when it is found by itself.
std::wstring shellInstallFolderText();
// A pop-out (the list picker, core/views_list_pick.inc): `content` in a window w x h (with its frame) at (x, y) on the
// screen (the SDL program: in its main window), kept on the work area, until shellClosePopout(content), a click outside
// it or another program's window closes it. `focus` gets the keyboard (not in a background run); `opened` runs once
// the window shows.
void shellRunPopout(stardraft::draw::View& content, int x, int y, int w, int h, stardraft::draw::View* focus, const std::function<void()>& opened);
void shellClosePopout(stardraft::draw::View& content);
// A UI icon's coverage (0..255, `size` x `size`, rows from the top) at the atlas size `size` (16, 24 or 32). False when
// the program cannot read the atlas here (the caller then draws with Canvas::icon).
bool shellUiIconMask(int size, int icon, std::vector<uint8_t>& out);

// --- Build and the Log (core/project_build.inc, core/views_log.inc) ---
// A question with two answers, as a warning: true when the first (`yes`) is chosen. The second is the default (Enter).
bool shellAskYesNo(const std::wstring& text, const std::wstring& title, const std::wstring& yes, const std::wstring& no);
// A build starts: the Log's error count goes to 0 (the lines stay).
void shellLogNewBuild();

// --- Program options and feedback (core/feedback_send.inc) ---
// The folder where feedback waits to go and the list of the sent feedback is (Windows: %LOCALAPPDATA%\StarDraft\feedback).
std::filesystem::path shellFeedbackFolder();
