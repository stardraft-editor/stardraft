# Release screenshots

One file for each release, `<version>.json`: what `python -I tools/release.py shots <version>` captures and how it
crops each picture into `docs/user/images/<version>/`. The captures are snapshots of the testing build
(`build/Release/stardraft-testing.exe`) with no window: `tools/sdl_snapshots.py` runs each case with `--snapshot`
(docs/dev/DEVELOPING.md, "Captures and measurements"), with no sound and without the focus.

- `runs`: one run of `tools/sdl_snapshots.py` each. `cases` are the names of its cases (`python -I
  tools/sdl_snapshots.py --list`); each case gives the capture `<case>.png`. To capture something new, add a case to
  its `CASES`. `map` is relative to the StarCraft install (the environment variable `STARCRAFT_INSTALL`, else the
  usual install folder); `project` (a `project.stardraft` or `.eds` file) is relative to the repository. A case that
  needs a map or a project needs `map` or `project`. `env` sets environment variables for that run only
  (`"STARDRAFT_LANG": "zh"` for a language). `"cmbw": true` runs the Cosmonarchy BW cases (the mod's install must be
  there).
- `shots`: one picture each. `run` (default 0) and `from` name a capture of a run (`<case>.png`); `to` is the
  picture's name (small letters, digits and -); `crop` is `[x, y, width, height]` in the capture; `scale` enlarges it
  after the crop; `alt` is the text for the changelog: `![<alt>](images/<version>/<to>)`.

To set the crops: `shots <version> --measure` captures only and lists each capture with its size and file.

Pictures show sample data, not the user's files.

The files of 0.5.0.7 and 0.5.0.8 have `specs` in place of `cases`: they were made with `--datashots` of the old Win32
program, which is gone. They are a record of those releases and do not run again.
