# Quality KPIs

StarDraft keeps a set of quality numbers (KPIs) for performance, the user interface, the code and the documentation.
`tools/kpi.py` measures them the same way each time; `docs/dev/kpi/targets.json` has the target of each one. Each
release must meet the targets. Between releases, a KPI is something to work on: make the number better, then make
its target the new number.

## Commands

```
python -I tools/kpi.py measure [--areas perf,ux,code,docs] [--quick] [--rebuild]
python -I tools/kpi.py record <version>
python -I tools/kpi.py gate <version>
python -I tools/kpi.py show
```

- `measure`: all the KPIs now, with each one's target. `--areas perf` (or `ux`, `code`, `docs`) measures one area.
  `--quick`: one run of each timing, no snapshots and no tests (to see a change while optimizing; not for a
  release). `--rebuild`: also the compiler warnings (a clean build of `stardraft_editor`: slow).
- `record <version>`: everything, kept in `docs/dev/kpi/<version>.json`. Commit the file with the release.
- `gate <version>`: that file against the targets. `tools/release.py check` runs it: a missed target stops the
  release, unless `targets.json` accepts it for that version (below).
- `show`: the targets and every recorded release, as a Markdown table (for this page's history).

The measurements use the testing build (`build/Release/stardraft-testing.exe`), with no window
(`SDL_VIDEODRIVER=offscreen`, `--snapshot`), and the game's own maps as the reference maps: (2)Boxer (96 x 96),
(2)Bottleneck (128 x 128) and (8)Killing Fields (256 x 256). The program's `--kpi` option prints the start's time and
each scripted input's time and the frame after it; `stats` prints the memory. Each run has a settings file of its own
(`KPI_SETTINGS` in tools/kpi.py: the default values, with the one-time notes seen), so the numbers do not depend on the
settings of the computer that measures.

## The KPIs

| Area | KPI | How it is measured |
|---|---|---|
| perf | Start to the first frame | No map; from the process's start (median of 3 runs). Not the first start after an installation or a video driver update, which also asks each video card about Direct3D 12 and makes a device to see that it can (about 220 ms more) |
| perf | First frame of a map | The 96 x 96 and the 256 x 256 map, opened from the start page (median of 3) |
| perf | Paint stroke step, slowest 5% | 40 short drags of the Brush on the 128 x 128 map: each drag and the frame after it |
| perf | Paint stroke step on a layer, slowest 5% | The same on a new map layer |
| perf | Palette scroll step, slowest 5% | 15 scrolls down and 15 up in the Units palette (the first time: its pictures load) |
| perf | Zoom step, slowest 5% | 20 zooms in and out on the 256 x 256 map |
| perf | Video memory with no map | After the start |
| perf | Working set with a 256 x 256 map | After it opens |
| perf | Heap and video memory growth | 30 opens and closes of the 128 x 128 map, from the 5th to the 30th, for Direct3D 12 and Vulkan |
| ux | Snapshot cases that fail | `tools/sdl_snapshots.py`: every workspace, mode and dialog (with the game and a map) |
| ux | UI text rule violations | `tools/ui_text_lint.py` (the rules of PRINCIPLES.md) |
| ux | UI strings missing in a language | The strings the lint finds that a `.tsv` does not have (the language with most) |
| ux | Trigger block texts missing in a language | The same for the trigger blocks' titles, docs and examples |
| ux | Symbols from a font | Arrows, shapes and private-use glyphs in UI strings (an icon of the icon set is the rule) |
| ux | Search cues that do not name what they search | A bare "Search" placeholder ("Search <things>" is the rule) |
| code | Test programs that fail | The tests that need no game data (as `build.yml` runs them) |
| code | Unused static functions | A static function that no file uses |
| code | Files of more than 3,000 lines, the largest file | `apps/`, `src/`, `include/` (not the data tables that tools make: `src/data`, `src/mods`) |
| code | Compiler warnings in our code | `--rebuild` only |
| docs | Help pages check | `tools/help_build.py --check` |
| docs | Links to files that are not there | The relative links of `docs/**/*.md` |

## Targets

- A KPI's first target is its baseline: the number when it was first measured (with a little room for the noise of
  timings), so that no release makes it worse.
- When work makes a KPI better, its target becomes the new number in the same change ("the ratchet").
- A count that must stay at zero (failed tests and snapshots, UI text violations, bare Search cues, broken links)
  has the target 0.
- A KPI with `"target": null` is recorded only, until its target is set.
- When a KPI's measurement changes, its target is the first number of the new measurement. UI strings missing in a
  language: 147 became 1046 on 2026-11-10, because the lint now also reads `apps/editor/core`, `apps/kit` and
  `apps/common`. The count is not only texts to translate: it has names of keys and paths too.

## A missed target at a release

`tools/release.py check <version>` stops when a KPI misses its target. The fix is the usual way. When a release must
go out with it, `targets.json`'s `"accepted"` names the KPI and the reason, for that version only:

```
"accepted": {"0.9.0": {"palette_scroll_p95_ms": "the new HD unit art loads in the palette; worked on in 0.9.1"}}
```

## History

`python -I tools/kpi.py show` makes this table from `docs/dev/kpi/*.json`.

| KPI | Target | baseline |
|---|---|---|
| Start to the first frame (no map) | ≤ 550 ms | 475.3 |
| First frame of a 96 x 96 map | ≤ 90 ms | 75.4 |
| First frame of a 256 x 256 map | ≤ 95 ms | 79.5 |
| Paint stroke step, slowest 5% | ≤ 6 ms | 4.8 |
| Paint stroke step on a layer, slowest 5% | ≤ 9 ms | 7.4 |
| Palette scroll step, slowest 5% | ≤ 9 ms | 7.4 |
| Zoom step (256 x 256), slowest 5% | ≤ 5 ms | 4.1 |
| Video memory with no map | ≤ 40 MB | 31.8 |
| Working set with a 256 x 256 map | ≤ 225 MB | 187.2 |
| Heap growth, 25 map opens and closes (Direct3D 12) | ≤ 1 MB | 0.2 |
| Video memory growth, 25 opens and closes (Direct3D 12) | ≤ 1 MB | 0.1 |
| Heap growth, 25 map opens and closes (Vulkan) | ≤ 1 MB | 0.0 |
| Video memory growth, 25 opens and closes (Vulkan) | ≤ 8 MB | 6.0 |
| Snapshot cases that fail | ≤ 0 | 12 |
| UI text rule violations | ≤ 0 | 0 |
| UI strings missing in a language (most) | ≤ 1046 | 1046 |
| Trigger block texts missing in a language (most) | ≤ 1103 | 1103 |
| Symbols from a font in the code | ≤ 30 | 30 |
| Search cues that do not name what they search | ≤ 0 | 0 |
| Test programs that fail | ≤ 0 | 1 |
| Unused static functions | ≤ 0 | 0 |
| Files of more than 3,000 lines (not data tables) | ≤ 3 | 3 |
| Lines of the largest file | ≤ 5600 | 5552 |
| Compiler warnings in our code (--rebuild) | - |  |
| Help pages check fails | ≤ 0 | 0 |
| Links in docs/ to files that are not there | ≤ 0 | 0 |

The perf targets were measured again for 0.9.0 (2026-11-08) with each run's own settings: before, the runs read the
user's settings, which change the numbers (the start mode, the graphics, the one-time notes). Start 600 ms, paint 8.5 ms,
paint on a layer 11.5 ms, palette scroll 12.5 ms, zoom 7 ms: the new numbers with a little room for noise.

2026-11-10: the start is 257 ms (475 before) and its target 325 ms: a start after the first one on the same video cards
and drivers does not ask the cards about Direct3D 12 again, and SDL does not make the device two times
(gpu-cards.txt, cmake/patch_sdl.cmake). What is left is the one Direct3D 12 device (about 200 ms, the driver's time).
The largest file is src/editor.cpp with 5,267 lines (5,732 before: the selection is in src/editor_selection.cpp) and
its target 5,300.

The baseline (2026-11-08, before 0.8.8) missed two zero targets: 12 snapshot cases (fixed in 9234c52d) and
trigger_test (its lock of the file contents is a local file, research/data: --update-lock after a change of the format).
