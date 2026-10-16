# The disk image's window (dmgbuild's settings file; tools/ci/mac_dist.sh runs dmgbuild with it): StarDraft.app on the
# left, a link to /Applications on the right, the background of tools/make_dmg_background.py between them.
# dmgbuild runs this file with `defines` (its -D options): app (the signed StarDraft.app), background (the TIFF) and
# icon (the volume's icon, the app's .icns).
import os.path

app = defines["app"]  # noqa: F821 (dmgbuild gives it)
background = defines["background"]  # noqa: F821
icon = defines.get("icon")  # noqa: F821

files = [app]
symlinks = {"Applications": "/Applications"}
hide_extension = [os.path.basename(app)]
format = "UDZO"  # (zlib: every macOS opens it)
filesystem = "HFS+"

window_rect = ((200, 120), (660, 400))  # (tools/make_dmg_background.py: W, H)
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
icon_size = 128
text_size = 13
icon_locations = {os.path.basename(app): (165, 190), "Applications": (495, 190)}  # (make_dmg_background.py: APP_X, APPS_X, ICON_Y)
