"""Makes src/mods/cmbw_creator_rules.inc from the mod team's terrain rules file (a .isom), as the repaired rules' inc.
usage: python -I tools/plugins/cmbw/cmbw_creator_rules.py <file.isom>  (in the repository folder)"""
import sys
data = open(sys.argv[1], 'rb').read()
assert data[:4] == b'SDTR'
ref = open('src/mods/cmbw_repaired_rules_prerelease.inc', 'rb').read()
nl = '\r\n' if b'\r\n' in ref else '\n'
lines = [
    "// The mod team's own fixes of the terrain rows (their terrains.isom, saved with the Custom isom terrain editor,",
    "// 2026-11-01: Jungle, Ice, Badlands and Space Platform), as a terrain rules file (version 2) the program applies over the repaired rows.",
    "// Do not edit by hand: tools/plugins/cmbw/cmbw_creator_rules.py makes it again from a newer file.",
    "",
]
for i in range(0, len(data), 32):
    lines.append(','.join(str(b) for b in data[i:i + 32]) + ',')
open('src/mods/cmbw_creator_rules.inc', 'wb').write((nl.join(lines) + nl).encode('ascii'))
print(len(data), 'bytes', 'CRLF' if nl == '\r\n' else 'LF')
