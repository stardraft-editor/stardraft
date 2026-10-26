# The terrain of Lagoon from organic shapes (shapes.py): writes terrain.json (P1's half; the build paints it with a
# half turn). Run it before build.py.
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'scripts'))
from shapes import blob, worm, rect, fill
ops = [{"op": "set_scenario", "name": "Lagoon", "description": "1v1. Made with stardraft-cli to the measures and the looks of the ladder maps."}]
# Low ground round the natural and under the plateau
ops += fill(blob(420, 1320, 760, 470, amp=0.15, seed=2), "Dirt")
ops += fill(blob(1408, 2600, 800, 470, amp=0.15, seed=4), "Dirt")
# The main: a wavy high ground, its core square (the base), its ramp's edge straight (y 896, x 448-896)
ops += fill(blob(430, 360, 650, 560, amp=0.18, seed=11), "High Dirt")
ops += fill(rect(0, 0, 800, 720), "High Dirt")
ops += [{"op": "paint_area", "terrain": "High Dirt", "area": [384, 704, 960, 896]},
        {"op": "paint_area", "terrain": "Dirt", "area": [384, 896, 960, 1088]}]
# Water: the pond east of the natural (it closes the main's side), the channel south of it, the lagoon in the middle
ops += fill(worm([(1170, 640), (1210, 1000), (1160, 1350), (1190, 1580)], 340, amp=0.3, seed=6), "Water")
ops += fill(worm([(-120, 1910), (250, 1870), (600, 1950)], 240, amp=0.35, seed=7), "Water")
ops += fill(blob(2048, 1792, 190, 240, amp=0.22, seed=5), "Water")
# The plateau, its ramps' edges straight (top y 2272, bottom y 2912)
ops += fill(blob(1344, 2608, 560, 320, amp=0.16, seed=9), "High Dirt")
ops += [{"op": "paint_area", "terrain": "High Dirt", "area": [1152, 2272, 1664, 2400]},
        {"op": "paint_area", "terrain": "Dirt", "area": [1152, 2144, 1664, 2272]},
        {"op": "paint_area", "terrain": "High Dirt", "area": [1152, 2784, 1664, 2912]},
        {"op": "paint_area", "terrain": "Dirt", "area": [1152, 2912, 1664, 3040]}]
# Mesas with no ramp (more cliffs, as on the ladder maps; nothing gets on them)
for i, (x, y, rx, ry) in enumerate([(560, 3260, 220, 130)]):
    ops += fill(blob(x, y, rx + 120, ry + 90, amp=0.2, seed=20 + i), "Dirt")
    ops += fill(blob(x, y, rx, ry, amp=0.25, seed=30 + i), "High Dirt")
json.dump(ops, open(os.path.join(HERE, 'terrain.json'), 'w'), indent=0)
print(len(ops), 'ops')
