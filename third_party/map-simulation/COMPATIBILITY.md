# Compatibility objective

Deliver an independently implemented, openly licensed replacement for all OpenBW
functionality, including complete game simulation and replay playback. Source
inspection may inform behavioral experiments and format facts; upstream code and
lookup tables must not be copied or translated into this package.

## First acceptance gate: Map Editor

The editor now links independent mining, Rush, region and preview adapters.
Default bootstrap/build exclude OpenBW; explicit reference-tool builds preserve
the oracle for development. This integration does not establish complete-game
or arbitrary-map compatibility.

The acceptance objective is to replace the production simulation, rush-path and sprite-preview integrations.
Remove OpenBW from production include/link graphs, bootstrap requirements and
packages. Keep the reference only as an optional development oracle.

Require exact results on the same maps, spawns, races, worker counts, command
timelines and game data: regions, paths, positions, orders, cargo transitions,
deliveries, patch cycle times and measured income. Cover saturation, obstacles,
failed paths, existing depots, short/long horizons, cancellation, continuation,
cloning and split/batched stepping. A reference-backed adapter or cached trace
cannot satisfy this gate.

Continuing routes retain consumed regional history, cursors and the combined
long/short-path storage budget. Storage-boundary fixtures and paired playback
cover recomputation near a shortened regional tail. General acceptance still
needs broader checks for multi-region jumps, local-path overflow during
continuation and visited-space capacity limits. A correct fresh route alone
does not establish continuation compatibility.

## Complete engine scope

- Replay containers, decompression, embedded maps, command decoding and scheduling,
  player setup, deterministic playback, seeking through snapshots and malformed
  input handling.
- Complete map/rule loading, all relevant DAT/IScript/LO/GRP data, scenario settings,
  player state, resources, supply, upgrades, technologies, alliances and visibility.
- Region topology, terrain contours, long/short paths, footprints, dynamic
  collision, movement modes, flying/repulsion, transport and all unit lifecycles.
- All orders, queues, secondary orders, construction, training, morphing,
  harvesting, combat, projectiles, spells, status effects, damage and death.
- Animation and sprite effects, overlays, modifiers, direction/offset behavior,
  random draws and resource limits, including non-unit scenery.
- Creep, power, fog, detection, triggers, victory/defeat, AI behaviors and the
  remaining gameplay systems and interfaces exposed by upstream OpenBW.
- Rendering/frontend and headless APIs needed to replace upstream uses; do not
  equate a headless mining subset with the complete engine.

## Evidence required for completion

Use reproducible local fixtures and replay corpora with explicit engine/ruleset
versions. Compare command acceptance and frame state against the pinned reference,
including random state and important subsystem state. Report first divergence
without numerical tolerances. Exercise every implemented order/opcode and subsystem,
edge cases, long runs, repeatability and independent snapshot branches. Include
replay end-state and intermediate-frame checks, not merely successful decoding.

Track unsupported behavior explicitly and fail rather than silently approximating
it. Broaden the version/corpus matrix as functionality is completed. No percentage
claim, successful isolated fixture or mining-only parity establishes full coverage.
The overall task remains open until the complete scope and production migration
are verified.

## Replay input follow-up

The public `replay::Replay` reader now decodes bounded container sections and
indexes ordered frame blocks. Across 3,936 local legacy/modern recordings, its
header, command, map, names and opaque extension hashes match the independent
byte-format experiment; frame counts and 13,913,181 block entries match as well.
Three classic recordings also have prior public-API reference extraction
comparisons. These checks do not simulate replay commands. Parsed metadata is a
subset of the retained raw header; extension settings/nested checksums, modern
opcode decoding, command scheduling/effects, initial match construction and full deterministic
playback/state comparisons remain required. Modern 1.18-era coverage is synthetic
only; format-family detection does not establish exact game-rules compatibility.

The separate streaming classic command reader decodes 44 wire opcodes. All 36,071
commands from three classic recordings match public-reference reads in frame,
block/byte order, player/opcode and encoded payload. This does not execute native
gameplay, reproduce selection/queue policies or construct an initial match.
The separate identity bridge now maps recorded actors and cyclic 16-bit unit IDs
to player slots and live native handles. Owner mappings match all 36,071 observed
commands; all raw ID layouts and native slot-reuse cases are checked separately.
The corrected reference probe advances its base simulation after reading each
frame's commands; the prior replay-step call dispatched commands twice and is
not valid gameplay-state evidence. Complete replay behavior remains open.

The observed corpus includes embedded maps larger than 52 MB and 239 maps with
noncanonical chunk tails. Preserve the original map bytes and test simulation
loader behavior against the reference; the editor's strict parser is not itself
an engine-compatibility oracle. Keep container integrity, map interpretation and
command execution as separately evidenced requirements. No parser-only result
satisfies the full replay objective.

The public scenario reader now preserves these tails and exposes typed terrain
views, checked against all 3,936 corpus maps. Native terrain/regions/contours
match 14 accepted reference cases and eight controlled rejections, including raw
blocked-region IDs. This is static terrain setup only: full version/rule loading,
scenario unit interpretation, player initialization, starting placement and
complete deterministic replay execution remain unfinished.
