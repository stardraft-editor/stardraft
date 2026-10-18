# Plan for the roadmap's Next items

The work that is left in the Next section of [ROADMAP.md](../project/ROADMAP.md), as it stood on 2026-10-18,
grouped by size. ROADMAP.md is the source for what each item is; this page tells where each one starts in the code,
what is missing, and what must be decided first. Update ROADMAP.md (and so the website) when an item is done.

## How to work through it

- The small and medium items are independent of each other: they can be done one by one, or by several agents at the
  same time (one item each), then reviewed, merged and tested together.
- Each item ends with its snapshot cases (tools/sdl_snapshots.py) or unit tests, the texts in the four translations
  (apps/editor/res/lang/*.tsv, then `python3 -I tools/ui_lang_gen.py`), and its line in ROADMAP.md marked done.
- Trigger blocks (src/data/trigger_blocks.inc, src/triggers.cpp) are checked by build/trigger_test with the game data:
  euddraft builds a map with every block. What a block does in the game needs a test in StarCraft.

## Done on 2026-10-18

R61, R60, R62, R59, R38, R32, R58, R23, R37, R25, R52 and R49 (first versions of R25, R52 and R49), and the chat and
music part of R51. ROADMAP.md tells what each one does.

To check in StarCraft (trigger_test builds them with euddraft, but what they do needs the game):

- R53: how smooth Pan the camera and Shake the camera look.
- R58: the tile map's addresses (0x5993C4 and 0x57F1D4) for The tile at a location and The tile at a point.
- R51: the chat conditions, The number in the chat line, Play music and Stop the music.
- R23: a Classic trigger with more than 64 actions becomes several triggers linked by a switch; does it work for a
  trigger of several players?
- R52: a project whose texts are larger than the game's stat_txt.tbl (the game reads the project's copy then).

## Left

- **R51 Advanced EUD capabilities.** Blocks for EUDQueue and EUDDeque with their methods and loops, EUDByteReader and
  EUDByteWriter, and drawing.
- **R25, R52, R49: after the first versions.** Campaigns: settings kept the same in all maps. Import: EUD Editor 2's
  button sets, requirements and trigger editor data (not only the TriggerEditor.eps it writes when it builds), and
  EUD Editor 3's own project file. Generator: ramps between high and low ground, rules for expansions and other places.
- **R42 Map import from scmscx.com.** Skipped for now. Needs: what the site offers (an API or pages to read), the
  owner's permission, then a search window that downloads the map to a cache folder and opens it.
- **R31 Replay preview.** Skipped for now. Needs: a way to make a replay without a person playing (the game's
  computer players run only in the game; a replay is the players' commands), so either a simulation of the game
  (OpenBW) or StarCraft started with the map and computer players, and its replay saved.

## Not code

- **R56 Signed program.** Needs a Windows code-signing certificate (bought, or a signing service such as Azure Trusted
  Signing); then sign in the release workflow. The macOS build has its own signing (tools/ci/mac_dist.sh).
- **R14 Exact timers.** Paused in ROADMAP.md. Note for later: "Every N seconds" (act.every in src/triggers.cpp) sets
  its next time from the current tick, so it drifts by the cycles it waits; adding the period to the last due time
  would keep it exact.

## The website

The website (stardraft-editor/stardraft-editor.github.io) is made from ROADMAP.md, so it follows the items. Its
privacy page (PRIVACY in that repository's build.py) still describes only Windows: the settings in the registry and
"the Windows version" in feedback and crash reports. Check what the macOS and Linux builds keep and send, then change
the page and its date.
