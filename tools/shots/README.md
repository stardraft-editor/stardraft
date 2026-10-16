# Release screenshots

One file for each release, `<version>.json`: what `python -I tools/release.py shots <version>` captures and how it
crops each picture into `docs/user/images/<version>/`. The captures run the Win32 testing build (`build/Release/
stardraft-testing-win32.exe`; the SDL program has no `--datashots`) off the screen with `--datashots` (docs/dev/DEVELOPING.md, "Captures and measurements"), with
no sound and without the focus.

- `runs`: one start of the program each. `map` is relative to the StarCraft install (the environment variable
  `STARCRAFT_INSTALL`, else the usual install folder). `specs` are the datashot specs, in order. `env` sets
  environment variables for that run only (`"STARDRAFT_LANG": "zh"` for a language).
- `shots`: one picture each. `run` (default 0) and `from` name a capture of a run; `to` is the picture's name (small
  letters, digits and -); `crop` is `[x, y, width, height]` in the capture; `scale` enlarges it after the crop;
  `alt` is the text for the changelog: `![<alt>](images/<version>/<to>)`.

To set the crops: `shots <version> --measure` captures only and lists each capture with its size and file.

Pictures show sample data, not the user's files: `quickopen:demo` lists sample recent files.
