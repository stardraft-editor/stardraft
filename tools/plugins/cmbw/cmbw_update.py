#!/usr/bin/env python3
"""Pulls the latest Cosmonarchy BW into the editor's mod tables and says what changed (after a mod update).

The editor reads the mod's game data from its install (units.dat, graphics, sounds, iscripts): a new unit's pictures
come with the install. What the install does not have is in tables made from other sources:

1. The install: are its git checkouts (Prerelease, Release) at the newest commit of their repositories? The Fraud
   Launcher updates them (the install's folder is the administrators'); this script only says it.
2. src/mods/cmbw_unit_tiers.inc (the units' tech tiers by id) from the website's export and the install's unitnames.tbl:
   tools/plugins/cmbw/cmbw_units.py. The palette's other places come from the install's files (src/mods/cmbw.cpp).
3. src/mods/cmbw_crigs.inc (the trigger comment commands) from Cosmonarchy-GPTP: tools/plugins/cmbw/cmbw_crigs.py, on a
   clone in build/cmbw/Cosmonarchy-GPTP (made or brought up to date here).

Then the changes: units whose tier is new, gone or other; commands added, removed or with other arguments; and the
new commands that src/mods/cmbw_crig_docs.inc does not document (its help is written by hand).
A table whose content is the same is not written again.

usage: python -I tools/plugins/cmbw/cmbw_update.py [<Cosmonarchy folder>]
       (default: the editor's CosmonarchyFolder setting, else C:/Program Files (x86)/Cosmonarchy)
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
GPTP_URL = 'https://gitlab.com/the-no-frauds-club/tools/Cosmonarchy-GPTP.git'
UNITS = os.path.join(ROOT, 'src', 'mods', 'cmbw_unit_tiers.inc')
CRIGS = os.path.join(ROOT, 'src', 'mods', 'cmbw_crigs.inc')
DOCS = os.path.join(ROOT, 'src', 'mods', 'cmbw_crig_docs.inc')


def git(folder, *args, check=True):
    # (safe.directory for this call only: the install's checkouts belong to the administrators)
    r = subprocess.run(['git', '-c', 'safe.directory=' + folder.replace('\\', '/'), '-C', folder] + list(args),
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    if check and r.returncode:
        raise RuntimeError('git ' + ' '.join(args) + ': ' + r.stderr.strip())
    return r.stdout.strip()


def mod_folder():
    if len(sys.argv) > 1:
        return sys.argv[1]
    appdata = os.environ.get('APPDATA')
    if appdata:
        try:
            chosen = json.load(open(os.path.join(appdata, 'StarDraft', 'settings.json'), encoding='utf-8')).get('CosmonarchyFolder')
            if chosen:
                return chosen
        except (OSError, ValueError):
            pass
    return 'C:/Program Files (x86)/Cosmonarchy'


def check_install(folder):
    print('== The install:', folder)
    found = False
    for channel in ('Prerelease', 'Release'):
        d = os.path.join(folder, channel)
        if not os.path.isdir(os.path.join(d, '.git')):
            continue
        found = True
        head = git(d, 'rev-parse', 'HEAD')
        when, subject = git(d, 'log', '-1', '--format=%cs%x09%s').split('\t', 1)
        url = git(d, 'config', 'remote.origin.url', check=False)
        remote = git(d, 'ls-remote', url, 'HEAD', check=False).split('\t')[0] if url else ''
        state = ('the newest' if remote == head else
                 'not the newest (' + remote[:8] + ' is): update it with the Fraud Launcher' if remote else 'no answer from ' + url)
        print('  %s: %s %s "%s": %s' % (channel, head[:8], when, subject, state))
    if not found:
        print('  no Prerelease or Release checkout here')


def keep_newlines(path, before):
    """The table as the generator wrote it, in the line ends it had; its old bytes when its content is the same."""
    after = open(path, 'rb').read()
    if before is None:
        return
    crlf = b'\r\n' in before
    norm = after.replace(b'\r\n', b'\n')
    if norm == before.replace(b'\r\n', b'\n'):
        open(path, 'wb').write(before)
    elif crlf:
        open(path, 'wb').write(norm.replace(b'\n', b'\r\n'))


def read(path):
    try:
        return open(path, 'rb').read()
    except OSError:
        return None


def unit_rows(data):
    out = {}  # (id: (tier, name))
    for line in (data or b'').decode('utf-8').splitlines():
        m = re.match(r'\{(\d+), (\d+)\},\s*//\s*(.*)$', line)
        if m:
            out[int(m.group(1))] = (m.group(2), m.group(3))
    return out


def crig_rows(data):
    out = {}
    for line in (data or b'').decode('utf-8').splitlines():
        m = re.match(r'\{"([^"]*)", "([^"]*)", "([^"]*)", (true|false)\},', line)
        if m:
            out[m.group(1) + ' ' + m.group(2)] = (m.group(3), m.group(4))
    return out


def update_units(folder):
    print('== Unit tiers (src/mods/cmbw_unit_tiers.inc, from the website export)')
    before = read(UNITS)
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'cmbw_units.py'), folder], capture_output=True, text=True)
    if r.returncode:
        print('  failed:', r.stderr.strip().splitlines()[-1] if r.stderr.strip() else r.returncode)
        return
    for line in r.stdout.splitlines():
        print('  ' + line)
    keep_newlines(UNITS, before)
    old, new = unit_rows(before), unit_rows(read(UNITS))
    changes = 0
    for i in sorted(set(new) - set(old)):
        print('  new: %d %s (tier %s)' % (i, new[i][1], new[i][0]))
        changes += 1
    for i in sorted(set(old) - set(new)):
        print('  no tier now: %d %s' % (i, old[i][1]))
        changes += 1
    for i in sorted(set(old) & set(new)):
        if old[i][0] != new[i][0]:
            print('  %d %s: tier %s (was %s)' % (i, new[i][1], new[i][0], old[i][0]))
            changes += 1
    if not changes:
        print('  no changes')


def update_crigs():
    print('== Trigger comment commands (src/mods/cmbw_crigs.inc, from Cosmonarchy-GPTP)')
    clone = os.path.join(ROOT, 'build', 'cmbw', 'Cosmonarchy-GPTP')
    try:
        if os.path.isdir(os.path.join(clone, '.git')):
            git(clone, 'fetch', '--depth', '1', 'origin')
            git(clone, 'reset', '--hard', 'FETCH_HEAD')
        else:
            os.makedirs(os.path.dirname(clone), exist_ok=True)
            r = subprocess.run(['git', 'clone', '--depth', '1', GPTP_URL, clone], capture_output=True, text=True)
            if r.returncode:
                raise RuntimeError(r.stderr.strip())
    except RuntimeError as e:
        print('  could not get Cosmonarchy-GPTP:', e)
        return
    print('  Cosmonarchy-GPTP:', git(clone, 'log', '-1', '--format=%h %cs "%s"'))
    before = read(CRIGS)
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'cmbw_crigs.py'), clone], capture_output=True, text=True)
    if r.returncode:
        print('  failed:', r.stderr.strip().splitlines()[-1] if r.stderr.strip() else r.returncode)
        return
    keep_newlines(CRIGS, before)
    old, new = crig_rows(before), crig_rows(read(CRIGS))
    docs = open(DOCS, encoding='utf-8').read() if os.path.isfile(DOCS) else ''
    changes = 0
    for key in sorted(set(new) - set(old)):
        name = key.split(' ', 1)[1].split('|')[0]
        print('  new: %s (%s)%s' % (key, new[key][0], '' if '"' + name + '"' in docs else '; no help in cmbw_crig_docs.inc'))
        changes += 1
    for key in sorted(set(old) - set(new)):
        print('  removed:', key)
        changes += 1
    for key in sorted(set(old) & set(new)):
        if old[key] != new[key]:
            print('  %s: arguments now %s' % (key, new[key][0]))
            changes += 1
    if not changes:
        print('  no changes')


def main():
    folder = mod_folder()
    check_install(folder)
    update_units(folder)
    update_crigs()
    print('== Next: build, and look at the palette and the Classic triggers page of a map of the mod')


if __name__ == '__main__':
    main()
