#!/bin/bash
# The editor's AppImage (Linux, x86_64): `cmake --install` of a build into an AppDir (usr/bin/stardraft and
# stardraft-thumbnailer, usr/share/stardraft: the fonts and the icon atlases, the .desktop file, the icons, the file
# types, the metainfo), then appimagetool. The build should be made on the oldest system that the AppImage is for
# (glibc), with -DSTARDRAFT_STATIC_LIBSTDCXX=ON (the C++ library in the program). SDL loads the rest itself (X11,
# Wayland, Vulkan, D-Bus, the sound): the AppImage takes them from the system.
#
# usage: tools/ci/linux_appimage.sh <build folder> <output .AppImage> [<appimagetool>]
#   appimagetool: its file, else it is downloaded (the release "continuous" of AppImage/appimagetool).

set -euo pipefail
build=$1
out=$2
tool=${3:-}
id=io.github.stardraft_editor.StarDraft
work=$(mktemp -d)
app=$work/AppDir

DESTDIR=$app cmake --install "$build" --prefix /usr --component stardraft

# The AppDir's root: the program's start (AppRun), its .desktop file and its icon.
cat > "$app/AppRun" <<'EOF'
#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
exec "$here/usr/bin/stardraft" "$@"
EOF
chmod +x "$app/AppRun"
cp "$app/usr/share/applications/$id.desktop" "$app/"
cp "$app/usr/share/icons/hicolor/256x256/apps/$id.png" "$app/$id.png"
ln -sf "$id.png" "$app/.DirIcon"

echo "The program's libraries (the system gives them):"
ldd "$app/usr/bin/stardraft" | sed 's/^/  /'

if [ -z "$tool" ]; then
    tool=$work/appimagetool
    curl -fsSL -o "$tool" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$tool"
fi
# (APPIMAGE_EXTRACT_AND_RUN: appimagetool runs without FUSE, as on a CI machine)
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$tool" --no-appstream "$app" "$out"
ls -l "$out"
