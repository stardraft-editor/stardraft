# Contributing to StarDraft

StarDraft is in alpha. Reports of problems and ideas help the most.

## Report a problem or an idea

- **In StarDraft:** right-click a control and select "Report feedback on ...", or use the feedback button at the top
  right. StarDraft makes a GitHub issue with the version and the workspace. A crash makes an issue by itself.
- **On GitHub:** open an issue with the bug report or the feature request template.
- **On Discord:** write in #feedback on the [StarDraft Discord](https://discord.gg/JBMCwSRJBm).

Say what you did, what you saw and what you expected. Attach the map if the problem is in a map.

## Change the code

1. Build StarDraft: see [docs/dev/DEVELOPING.md](docs/dev/DEVELOPING.md), "Build and run".
2. Read [docs/dev/PRINCIPLES.md](docs/dev/PRINCIPLES.md). Every change follows it: rules from the game's data, one
   undo step for each edit, the speed limits, the control kit, and the text rules (ASD-STE100).
3. Run the tests for what you changed (DEVELOPING.md, "Tests"). `python -I tools/ui_text_lint.py` checks the UI
   texts; the build stops on a new violation.
4. Keep each pull request to one change. Say what changes for the user.

Do not add files of the game (maps, graphics, sounds) or copies of other projects' files: link to them
(see [NOTICE.md](NOTICE.md)).

By sending a contribution, you agree that it is under the MIT license of this repository (see [LICENSE](LICENSE)).
