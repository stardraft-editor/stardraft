# CMBW resource areas

Status: **on hold** (2026-11-11). No user for the tool yet. Nothing below "Today" is built.

## Today

On a CMBW map the path overlay's town areas follow the mod's rule (`src/pathing.cpp`, `PathingInput::mapAreas`;
`docs/dev/CONTROLS.md`, town areas):

- Area number of a resource: Hit points field of a Player 12 resource with the Burrowed state. 0: no area. 249 or
  more: no change.
- Area count: the largest such number, at most 249.
- Area position: a start location of player 9 and up gives its position to the area with the player's number.
- Tail areas with no resources and no start location: not areas.
- All of this only when the game's own rule made at least one area.

Editor only: an area that only the numbers made has no position in the game; the overlay shows it at the middle of
its resources.

## Proposed format (mod team, not final)

Replaces the hit-points and start-location rule.

```
magic: "TOWN"
size:  count * 4 + 2
data:
    WORD    last_prefixed
    POINT16 position[count]
```

- `count`: highest area ID with a user-defined setting; from the section size. Areas past it: 0. `count` 0: no
  section.
- `last_prefixed`: last area ID with a preset position, minus the number of start locations. A preset ("prefixed")
  area has its position before resources are assigned, as the start-location areas. Contiguous: from the first
  start-location area to the highest flagged ID.
- `position`: X, Y (WORD each). 0, 0: not user-defined.
- Resource container (mineral field, geyser, refinery, ...): area ID in `UNIT` at struct offset 0x16 (WORD). 0: no
  user assignment.

## Open questions (ask before any build)

1. `UNIT` 0x16 is the high word of the DWORD Resource amount (0x14) in the standard format. An amount of 65,536 or
   more reads as an area ID. Amount = WORD on a CMBW map? Then the editor limits it to 65,535.
2. "No area" is lost: in the engine file a user ID of 0 unlinks the resource (high byte 1 = set). In the proposal 0 =
   no assignment. Is unlink still needed, and how is it written?
3. Index base: `position[0]` = area 1 (as the engine file's `areas[1 + chktown_id]`)?
4. `last_prefixed` is relative to the start location count, which changes when a start location is deleted. Editor
   keeps the absolute ID and converts on save?
5. Positions for start-location areas: ignored, or override?

Engine file notes already reported to the mod team: the position loop writes `areas[resarea_count + 1]` (one past);
the tail loop can go below 0; source of `chkTownPrefixCount` unknown.

## Tool design (if built)

CMBW plugin only.

- **Layer** "Resource areas" (Map layers): each area's rectangle and number, a line from each resource to its area.
  Computed again after each edit with the engine's rules.
- **Position**: drag the rectangle, or X/Y in Properties. Set = pin mark; automatic = dashed. "Automatic position"
  (or Delete) = 0, 0.
- **Preset position** check box: a tick on area N ticks all below; a clear on N clears all above.
- **Area list**: number, resource count, mineral and gas totals, set/automatic; click goes to the area.
- **Unit properties** "Resource area": Automatic, existing areas, New area. Works on many resources.
- **Map**: select resources, "Assign to area" / "New area from selection" (position = middle of the selection).
- **Checks**: ID of 249 or more; set area with no resources; resource assigned to a missing area; preset area with
  no position.
- **Save**: `TOWN` only when an area has a position or a preset flag; area ID to `UNIT` 0x16. The hit-points rule is
  read once, converted, then dropped.
