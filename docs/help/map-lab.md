# Mining Lab and rush distance

Open **Tools > Mining Lab** for the compact bottom widget. Select a start location
or a placed depot, a race, and a worker count, then select **Start mining**.
**One / field** assigns one worker to each nearby mineral field. You can also
right-click a start location or depot in Units mode and select **Mining Lab**.

Start another base to compare independent tests. **Tests** shows the table;
select a row to return to that base. **Stop test** ends the selected test and
**Stop all** ends all tests. **Hide** keeps the tests running and hides the widget.
Open Mining Lab again to show it.

## Boost searches

Each of the three boost sections has its own search time in seconds (1 to 600;
15 by default). Set the time and select **Search**. The selected test pauses
while the search runs; other tests continue. The bar shows progress, and the text
shows the number of faster workers and the total search time.

The search budget is followed by a verification step. **Continue** gives the
same search more time. **Exhausted** means that search has no more candidates.
Results are applied automatically. For combined plans, run position, cargo, then
cycle searches. Updating an earlier plan clears the later plans.

- **Position boost:** translucent workers mark the selected starting positions.
- **Return cargo:** translucent workers and return arrows mark cargo commands.
- **Cycle boost:** translucent workers, colored arrows and numbers show the two
  command stages. Each worker keeps its own arrow color.

The check boxes only show or hide guides; they do not turn off an applied plan.
Blue numbers at mineral fields show the last delivery-to-delivery cycle in game
frames. Income includes travel at the start. These tests use a completed depot,
fixed workers and mineral fields that do not run out during the measurement.
They do not include gas or worker production.

## Rush distance

Open **Tools > Measure rush distance** for the small Rush widget. Select a worker
and use **Set endpoints**, then click the start and destination on the map.
The two coordinate buttons can change one endpoint. **Measure** also runs the
displayed endpoints.

**Launch (s)** sets the interval between workers (0.5 to 60 seconds). **Keep**
sets how many completed results and recent trails to retain (1 to 50). These
settings can change while the test runs. Each worker runs in a separate world.

The map shows A/B markers, colored trails, translucent workers and moving timers.
Hover a trail to highlight it and its result. The table shows elapsed game time
and traveled pixels. Failed trips show **Not reached** and do not count in the
average. **Clear** ends the measurement. **Hide** leaves it running.

## Playback and map data

Both widgets have playback speed, pause and copy controls. Times are game times
at Fastest: one frame is 42 ms. Tests run until you stop them; a boost plan can
restart the mining clock when it is applied. Copy uses tabs between columns.

The tools use a copy of the current map, including unsaved edits. They do not
change the map or its undo history. Editing the map, changing map tabs or starting
the experimental simulator stops the tests. Reopen the tool after an edit.

These tools need standard Brood War data from your StarCraft installation.
The engine has known differences on some routes, so test close balance decisions
in the game too.

## Sources and credits

These tools use the independent MIT Map Simulation engine by Map Editor
contributors and its mining/Rush adapters. Their controls, search workflow and
guide meanings follow the original Map Editor implementation. Game data comes
from the user's installation. OpenBW was used as an optional behavior reference
during engine development; its code is not part of these tools.
