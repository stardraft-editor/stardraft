# Copies the icons the editor uses from tools/icongen/out/ into apps/editor/res/.
# Run it after build.mjs made new icons:  python tools/import_icons.py [out folder]
# The editor embeds these files (apps/editor/res/stardraft_sdl.rc and the build's stardraft_editor_files.rc); build it
# again after the import.
import pathlib
import shutil
import sys

root = pathlib.Path(__file__).resolve().parents[1]
out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'tools' / 'icongen' / 'out'
res = root / 'apps' / 'editor' / 'res'
res.mkdir(parents=True, exist_ok=True)

files = [out / 'app' / 'stardraft.ico', out / 'app' / 'stardraft-document.ico', out / 'ui' / 'ui_icons.h']
files += [out / 'ui' / f'atlas-{size}.png' for size in (16, 20, 24, 32)]
for f in files:
    if not f.exists():
        raise SystemExit(f'missing: {f} (run node tools/icongen/build.mjs)')
    shutil.copyfile(f, res / f.name)
    print('copied', f, '->', (res / f.name).relative_to(root))
