# stardraft

A Claude Code skill: make and measure 1v1 StarCraft melee maps with stardraft-cli. Claude reads
[SKILL.md](SKILL.md).

## Install

In Claude Code:

```
/plugin marketplace add https://raw.githubusercontent.com/stardraft-editor/stardraft/main/docs/skills/stardraft/.claude-plugin/marketplace.json
/plugin install stardraft@stardraft
```

`/plugin marketplace update stardraft` gets a newer version. Or copy this folder to `~/.claude/skills/stardraft/`.

You also need:
- stardraft-cli, which comes with each StarDraft release: put it on the PATH or set `STARDRAFT_CLI` to its path.
- Python 3 with numpy and scipy.
