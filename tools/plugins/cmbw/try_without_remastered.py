#!/usr/bin/env python3
"""Starts StarDraft as on a computer that has Cosmonarchy BW but no StarCraft: Remastered, to try that by hand.

The program searches for no Remastered install (--noinstall: the start page asks for the folder; leave it), and it has
settings of its own in a new folder (STARDRAFT_SETTINGS, APPDATA, LOCALAPPDATA): a first start, with no recent files and
no saved folders, and the user's own settings stay as they are. Then: Options > Program options > Plugins > Cosmonarchy
BW (CMBW), and open a map of the mod (or the start page's new Cosmonarchy map).

usage: python -I tools/plugins/cmbw/try_without_remastered.py [--exe <StarDraft 2.exe>] [--dry-run] [map] [the program's options]
       (default exe: build/Release/stardraft-testing.exe)
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--exe', default=os.path.join(ROOT, 'build', 'Release', 'stardraft-testing.exe'), help='the program')
    ap.add_argument('--dry-run', action='store_true', help='print the command and the folders; start nothing')
    a, rest = ap.parse_known_args()  # (the rest: a map and the program's own options)
    folder = tempfile.mkdtemp(prefix='stardraft-no-remastered-')  # (a new folder each time: always a first start)
    env = dict(os.environ)
    env['STARDRAFT_SETTINGS'] = os.path.join(folder, 'settings.json')
    for name, sub in (('APPDATA', 'appdata'), ('LOCALAPPDATA', 'localappdata')):
        env[name] = os.path.join(folder, sub)
        os.makedirs(env[name])
    env.pop('STARDRAFT_INSTALL', None)  # (no Remastered folder from the environment either)
    cmd = [a.exe, '--noinstall'] + rest
    print('settings, recent files and caches in', folder)
    print(' '.join('"%s"' % c if ' ' in c else c for c in cmd))
    if a.dry_run:
        return 0
    return subprocess.call(cmd, env=env)


if __name__ == '__main__':
    sys.exit(main())
