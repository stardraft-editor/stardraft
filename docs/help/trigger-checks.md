# Trigger checks

The Triggers workspace checks your triggers while you edit them. A block with a problem has a mark: red for an error
(Build stops), amber for a warning (the map builds, but it may not do what you want). Put the mouse on the mark to read
the problem. Click the mark, or press F1 with the block selected, to open its part of this page.

Each part tells the rule, gives an example, and tells what the check does not find. The checks look at the triggers
only. They do not run the map: test your map in the game too.

## Values {#values}

Each value of a block must be there and must be of the correct kind: a location that is on the map, a unit type that
exists, a number in the range of the field, a player that the block accepts.

- Example: "Create units" with the location "Base" after you deleted the location "Base". The check says: "The map has
  no location Base".
- Example: "Set switch" with switch 300. The check says: "Use a switch from 1 to 256".
- It does not find: a value that is correct but not what you want (Player 2 when you meant Player 3), or a location
  that is in the wrong place on the map.

## Where a value has a value {#scope}

Some values have a value only inside a block that gives it. "The unit" has a value only inside For each unit (or For
each new unit). "The current player" has a value only inside For each player, or in a trigger that runs for one player.
Leave the loop works only inside a loop, and Pause only inside a Sequence.

- Example: "Kill the unit" directly in a trigger, not inside For each unit. The check says: "\"the unit\" has a value
  only in For each unit".
- Example: Pause directly in a trigger. The check says to put it in a Sequence, or to use After a delay.
- It does not find: a For each block that never finds a unit (for example, a filter that no unit matches).

## Variables and names {#variables}

A variable has one kind (a number, a text, a unit, a list, a constant...). A block accepts only the kinds that it can
use. Names of variables and of triggers in Definitions must be different from each other. A constant does not change,
and a list has a fixed size.

- Example: "Set variable" on a constant. The check says that you cannot change a constant.
- Example: two variables with the name "gold". The check marks both.
- Example: item 12 of a list of 10 items, typed as a number. The check says how many items the list has.
- It does not find: an index that the game calculates while it runs. A debug build (Project page, Debug build) checks
  these indexes in the game and shows a message.

## Texts {#texts}

A text can show values from the game with {}. It can have two {} at most, one for each value. A chat text (for chat
commands) has at most 78 letters, and some letters are not permitted in it.

- Example: "Show text" with the text "Gold: {} Gas: {} Time: {}". The check says to use at most two {}.
- It does not find: a text that is too long for the screen, or colors that are difficult to read.

## Time {#time}

Triggers run on each trigger cycle, not on each second. Some blocks need their trigger to run again and again: After
a delay, Every so often, Sequence, Pan the camera, Shake the camera. "Exactly" in a time condition can miss, because
the triggers can skip that exact value.

- Example: After a delay in a trigger in On map start. The trigger runs one time, so the delay never ends. The check
  says to put it in a trigger in On every trigger cycle.
- Example: "Elapsed time is exactly 60 seconds". The check says to use At least or At most.
- It does not find: a trigger in On every trigger cycle whose conditions stop it before the delay ends.

## Desync {#desync}

All the computers in a game run the same triggers. A Run local block runs its actions on one computer only (the
player's own). Inside it, an action may change only what each computer keeps for itself: what the player sees and
hears (texts, sounds, the screen, the camera). An action that changes the game (units, resources, a location, a wait,
the game speed, a random number) makes the computers different, and the game desyncs: it ends for the players.

- Example: "Set resources" inside Run local block. The check gives an error.
- Example: "Wait" inside Run local block: it holds up the triggers on one computer only. The check gives an error.
- It does not find: game memory that Raw epScript or Set memory changes inside Run local block. The check knows the
  blocks only, not the addresses in your own code.

## Game memory {#memory}

Some blocks read or write the game's memory at an address (EUD). StarCraft: Remastered permits only some addresses.
A write to an address that it does not permit can stop the map.

- Example: "Set memory" at an address that StarDraft does not know. The check gives a note: test it in the game.
- It does not find: whether the game permits the address. Only a test in the game shows it.

## Speed {#speed}

Each block costs time in each trigger cycle. A trigger that costs much makes the game slow for all the players. For
each unit runs its actions for each unit (up to 1,700) each time.

- Example: a trigger with a long list of actions inside For each unit. The check gives a warning, and the trigger
  shows "about N actions each run" with what makes it cheaper.
- It does not find: many cheap triggers that together cost much.

## Triggers {#triggers}

A trigger needs at least one condition (Always is a condition). A trigger in Definitions runs only when a Run a trigger
block runs it, and it must not run itself. Run a trigger must name a trigger that is on.

- Example: a trigger with no conditions. The check says that it never runs.
- Example: a trigger in Definitions that runs itself. The check says that it never ends.
- It does not find: two triggers that run each other in a loop through other triggers.

## Blocks that StarDraft does not know {#unknown}

A project from a newer StarDraft can have blocks that this StarDraft does not know. It keeps them, but it cannot build
them. Some blocks are also not ready to build yet.

- Example: "Unknown action act.something". Update StarDraft, or remove the block.
- It does not find: anything about what the unknown block does.

## Sources and credits

The StarDraft 2 developers wrote this page from the checks in StarDraft 2's own code.
