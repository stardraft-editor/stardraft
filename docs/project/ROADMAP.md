# Roadmap

The problems that StarDraft does not solve yet, and the solution where it is clear. The simplest items are first.
The groups in *For:* are the groups in README.md, "Who StarDraft is for".

## Next

- [ ] **R36 Build errors that open their trigger.** euddraft shows an error in the Log with a line of the triggers'
  code. Mappers must find the trigger by hand. A click on the error opens its trigger and block in the Triggers
  workspace. *For:* EUD.
- [ ] **R37 More terrain generation tools.** The generator makes only terrain. Mappers must place doodads and
  decoration by hand. Add tools such as scattering doodads that fit the terrain. *For:* melee, UMS and campaign.
- [ ] **R39 Tiles in the map's graphics.** The Tiles list shows the tiles in SD, also when the map shows HD or Carbot.
  Mappers cannot find a tile by the look that the map shows. Draw the list's tiles in the map's graphics. *For:* melee,
  UMS and campaign.
- [ ] **R40 Floating palettes.** The palettes (terrain, tiles, units, doodads, sprites) are fixed beside the map, and
  only one of each shows. Mappers with a large screen or two screens cannot put them where they want. Let each palette
  open as a floating window, with several open at once, each with its own filter. *For:* melee, UMS and campaign, EUD.
- [ ] **R38 How the checkers work.** The Triggers workspace marks EUD errors and desync risks, but mappers cannot see
  what the checkers look at and what they cannot find. Explain each check in the help (the rule, an example, what it
  does not cover), and link to it from each mark. *For:* EUD.
- [ ] **R33 Unit range overlay.** The map does not show the ranges of units: weapons, sight, detection and psi fields.
  Mappers must find the values in the data. Draw the ranges of the selected units on the map. *For:* melee, UMS and
  campaign, EUD.
- [ ] **R34 Terrain flag overlays (walkability, elevation, cover, line of sight).** The map shows terrain only as
  pictures. Mappers cannot see which terrain is walkable, how high it is, or which terrain gives cover or blocks the
  line of sight. Draw overlays from the tileset flags (CV5, VF4). *For:* melee, UMS and campaign.
- [ ] **R32 Fog of war preview.** Mappers cannot see what each player sees at the start of the game. Select a player,
  and draw that player's fog of war on the map. *For:* melee, UMS and campaign.
- [ ] **R8 Preplaced unit graphics.** The game makes preplaced units before the map's data edits run. These units keep
  the game's graphics. Make them again at map start. *For:* EUD.
- [ ] **R14 Exact timers.** Wait stops all the triggers of a player. The timer blocks drift over a long game. Add exact
  "After N seconds" and "Every N seconds" blocks. Paused. *For:* EUD.
- [ ] **R23 Batch trigger generation.** Mappers make hundreds of triggers that differ in one value. They must make and
  change each trigger by hand. Make many triggers from one pattern (for each player, location or unit type). *For:* UMS
  and campaign.
- [ ] **R25 Campaign projects.** Each map of a campaign is a separate file. The settings drift between the maps, and
  mappers cannot see the campaign in one place. Keep the maps of a campaign in order in one project. *For:* UMS and
  campaign.
- [ ] **R2 Terrain rendering accuracy.** Some terrain looks different in StarDraft and in the game. Not all tilesets are
  compared in HD, SD and Carbot. Compare each tileset with the game, and fix what differs. *For:* melee, UMS and
  campaign.
- [ ] **R35 Pathfinder region overlay.** Units find paths through regions that the game makes when it loads a map.
  Mappers cannot see these regions, so they cannot see why units take a path or get stuck. Find how the game makes the
  regions (OpenBW may have the code), and draw them. *For:* melee, UMS and campaign.
- [ ] **R31 Replay preview.** To see a map in the game, a mapper must start a game and play it. Write a replay of
  the map that StarCraft plays, with computer players. *For:* melee, UMS and campaign.

## Later

- [ ] **R26 Mod loading (MPQ, folder).** StarDraft reads only the game's own files. Mappers cannot use the files of a
  mod. Load a mod (MPQ or folder) on top of the game's files. *For:* modders (not a group in README.md yet).
- [ ] **R27 Cosmonarchy support.** The largest mod has needs of its own. These needs are not known yet. *For:* modders.
- [ ] **R41 Combat simulator.** To balance custom units, mappers must play test fights in the game. Show a fight between
  units (their counts, upgrades and the result: who wins, the hit points left, how long it takes). It needs a
  simulation of the game itself, so it waits for that. *For:* EUD, modders.
- [ ] **R28 macOS port.** StarDraft runs only on Windows.
- [ ] **R29 Linux port.** StarDraft runs only on Windows.
- [ ] **R30 Web build.** Users must install StarDraft to use it.

