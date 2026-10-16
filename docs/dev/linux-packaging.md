# StarDraft 2 on Linux: the desktop and the packages

The editor on Linux is the same SDL program as on Windows and macOS (`stardraft`, CMake target `stardraft_editor`).
This page tells what it does to fit in a Linux desktop, how the packages are made, and what each part needs from the
desktop session.

## The application id

`io.github.stardraft_editor.StarDraft` (`apps/common/desktop_xdg.h`: `kAppId`). The id is the name of the .desktop
file, the Wayland app id and X11's WM_CLASS (SDL: `SDL_HINT_APP_ID`, set before `SDL_Init`), the Flatpak id, and the
icon's name. The dock and the task bar find the icon and the name through it. (Flathub's rule for GitHub projects: the
hyphen of `stardraft-editor` becomes an underscore.)

## The desktop's files (packaging/linux)

`cmake --install <build> --component stardraft` puts:

| File | What it is |
| --- | --- |
| `bin/stardraft`, `bin/stardraft-thumbnailer` | The editor and the map thumbnailer |
| `share/stardraft/fonts`, `share/stardraft/res` | The fonts and the icon atlases (the editor looks there when they are not beside it) |
| `share/applications/<id>.desktop` | The menu entry, "Open with StarDraft 2" for the file types |
| `share/mime/packages/<id>.xml` | The file types: `application/x-starcraft-scm`, `-scx`, `application/x-stardraft-project`, `application/x-euddraft-project` |
| `share/icons/hicolor/<n>x<n>/apps/<id>.png` | The icon, 16 to 512 px (`tools/make_linux_icons.py` makes them from `apps/editor/res/stardraft.ico`) |
| `share/icons/hicolor/<n>x<n>/mimetypes/<id>-map.png` | The map files' icon (from `stardraft-document.ico`) |
| `share/metainfo/<id>.metainfo.xml` | What GNOME Software, Discover and Flathub show |
| `share/thumbnailers/<id>.thumbnailer` | The map thumbnailer for the file managers |

The .desktop file, the file types and the metainfo are made by CMake (`cmake/linux_desktop.cmake`) from the `.in`
files and the translations in `apps/editor/res/lang/*.tsv`: a new text there gets its Korean, Spanish, Russian and
Chinese in the .tsv files, as the program's texts do.

## What the program does on the desktop

| Part | How | What it needs |
| --- | --- | --- |
| Folders | Settings in `$XDG_CONFIG_HOME/StarDraft` (`~/.config`), data (unsaved projects, feedback, euddraft) in `$XDG_DATA_HOME/StarDraft` (`~/.local/share`), the thumbnailer's tile colors in `$XDG_CACHE_HOME/StarDraft/tile-colors` (`~/.cache`). A relative value of a variable is not used (the specification). | Nothing |
| File dialogs | SDL's: the desktop portal (`org.freedesktop.portal.FileChooser`), else zenity. SDL has the portal only when it is built with D-Bus (`libdbus-1-dev`); `-DSTARDRAFT_REQUIRE_DBUS=ON` (CI, the packages) stops a build without it. | A session bus and `xdg-desktop-portal` with a backend (GNOME, KDE, GTK); else `zenity` |
| Theme and accent color | Program options > Automatic (the default): the portal's `org.freedesktop.appearance` `color-scheme` and `accent-color` (`desktop_xdg.cpp`, libdbus loaded at run time), with the `SettingChanged` signal for a change. Without the portal SDL's system theme, else dark. | The portal (GNOME 47 and KDE Plasma 6 give the accent color) |
| UI font | The desktop's font through fontconfig: KDE's `kdeglobals` font, GNOME's `font-name` (gsettings), else `fc-match sans-serif`; Noto Sans after it for the characters it does not have. A font that stb_truetype cannot read (CFF2, as a variable Cantarell) is not used. `STARDRAFT_FONT=noto` keeps Noto Sans. | `fc-match` (fontconfig) |
| Recently used files | A map or project that opens goes to `$XDG_DATA_HOME/recently-used.xbel` (the files that GNOME Files, the file dialogs and the dock show). Not in a test run. | Nothing |
| One program for all maps | A map or project that opens from the file manager while StarDraft is open goes to the open program (it opens there, and the window comes to the front); the second program ends. A Unix socket `StarDraft-<program file name>.sock` in `$XDG_RUNTIME_DIR` (a Flatpak: `$XDG_RUNTIME_DIR/app/<id>`), else in `/tmp/stardraft-<uid>` (mode 0700). A socket file that a crashed program left is replaced. A test with real windows (`--noactivate`) has a socket of its own. | Nothing |
| Window icon | Each window gets the app icon (`_NET_WM_ICON` on X11), so the task bar and Alt+Tab show it also when no .desktop file matches the window (an AppImage that is not installed). | Nothing |
| Crash reports | A signal handler (`apps/common/crash_posix.cpp`: SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, a C++ exception not caught; on a stack of its own, so a stack overflow is reported too) writes `report.txt` in `$XDG_STATE_HOME/StarDraft/crashes/<time>` (the place of each stack frame as `<file>+0x<offset>`; no minidump). The next start asks to send it, as on Windows. `--crash-test [stack]` crashes on purpose (the report is marked "a test run: not sent"); `--check crashes` shows the reports. | Nothing |
| Build's notification | A Build that ends while the window does not have the focus: `org.freedesktop.Notifications` (`desktop-entry` hint: the icon and the name). Build's progress on the dock's icon: SDL's launcher progress (`com.canonical.Unity.LauncherEntry`: KDE, Ubuntu's dock, Dash to Dock). | A notification server; a dock that shows the launcher progress |
| Map thumbnails | `stardraft-thumbnailer -s <size> <map> <png>`: the Quick Look picture of macOS (`apps/quicklook/map_preview.cpp`): the minimap's colors, the units, the start locations. The terrain's colors come from the tile color tables that the editor writes in the cache when a map opens; else the tileset's base colors. | A file manager that reads `.thumbnailer` files (GNOME Files, Nemo, Caja, Thunar with tumbler). GNOME runs a thumbnailer in a sandbox that cannot read the cache: the base colors there. KDE (Dolphin) reads no `.thumbnailer` files. |

`--check desktop:<folder>` (a snapshot run) prints the app id, the folders, what the desktop has (the session bus, the
portal, the notification server) and writes a recently used entry into `<folder>` (not the user's file);
`tools/sdl_snapshots.py` checks it (`map-check-desktop`). WSL and CI machines have no session bus: the portal and the
notifications are "no" there, and the program works without them.

## The AppImage

`tools/ci/linux_appimage.sh <build> <out.AppImage>`: `cmake --install` into an AppDir, an AppRun, then appimagetool.
CI (`linux-appimage`) builds it on Ubuntu 22.04 (the oldest glibc it is for) with GCC 12 and
`-DSTARDRAFT_STATIC_LIBSTDCXX=ON`, and `linux-appimage-run` starts it on Ubuntu 22.04 and on `ubuntu:rolling` with
`--snapshot`. The program links only glibc; SDL loads X11, Wayland, Vulkan, D-Bus and the sound libraries of the system.
An AppImage does not update itself: Help > Check for updates offers the new release's AppImage (the release's download
that ends in `-x86_64.AppImage`) and opens its address in the browser; with no such download, the release page opens.
In a Flatpak, Check for updates says to update in the software center. `--check release:<file>` shows what Check for
updates does with a release in GitHub's JSON in `<file>` (`tools/sdl_snapshots.py`: `update-release`). Restart starts the AppImage
(`$APPIMAGE`), not the program in its mount.

To install the desktop's files for an AppImage by hand, use a tool as Gear Lever or AppImageLauncher (they copy the
.desktop file and the icon from it).

## The Flatpak and Flathub

`packaging/flatpak/io.github.stardraft_editor.StarDraft.yml` builds in `org.freedesktop.Platform` 24.08. flatpak-builder
has no network while it builds: each source that CMake fetches (SDL, StormLib, CascLib, stb, libvpx, Dear ImGui, the
Noto fonts) is a source of the manifest at the same version, and CMake takes it from there
(`FETCHCONTENT_SOURCE_DIR_*`). A version change in `CMakeLists.txt` is a change in the manifest too. CI (`flatpak`)
builds it with flatpak-builder and keeps the bundle.

```
flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/io.github.stardraft_editor.StarDraft.yml
flatpak run io.github.stardraft_editor.StarDraft
```

For Flathub (a pull request to `flathub/flathub` with the manifest, then its own repository):

- The repository's source is `type: git` with the release's `tag` and `commit`, not `type: dir`. Each `tag` source
  gets its `commit` too (Flathub's linter wants both).
- `--filesystem=home` needs a reason in the pull request: the StarCraft install (the game data) is outside the
  sandbox, and euddraft's builds write next to the project. A later version can use the portal's folders only.
- `--share=network`: Check for updates, Report feedback and crash reports. In a Flatpak, Check for updates says to update
  in the software center (Flathub updates the program).
- The metainfo needs screenshots (`<screenshots>`, pictures at a fixed address) and a `<releases>` entry for each
  release. `flatpak run --command=flatpak-builder-lint` (`appstream`, `manifest`) checks it before the pull request.
- The thumbnailer does not work from a Flatpak (the file manager cannot run it); the AppImage and a distribution's
  package have it.
