# Mining Lab and rush distance

Use **Tools > Mining Lab** to compare mineral income at a base. Select a
race, a start location and a worker count. To measure another base, select
**Select start**, then click its depot position on the map. Select **Start**.

The table shows minerals per game minute and the last completed mining cycle at
each field. The rate includes the workers' travel at the start. This test uses a
finished depot, fixed workers and mineral fields that do not run out during the
measurement. It does not include gas or worker production.

**Optimize positions**, **Optimize cargo** and **Optimize cycles** search for a
better mining plan for one second. The report shows when a search finishes. The
workers then use its plan. The mining clock can restart when the plan changes.

Use **Tools > Measure rush distance** to measure travel between two points.
Select the race and the start location. Use **Select start** and **Select end**
to set the points on the map, then select **Start**. Each worker runs in a separate
copy of the map. The table shows elapsed game seconds, traveled pixels and
whether the worker reached the end. A blocked route shows **Not reached**.

**Speed** changes playback speed. The times in the tables are game times at
Fastest. **Pause** stops the clock; **Resume** continues. **Stop** keeps the report
and removes the overlay. **Copy** copies the report with tabs between its columns.

The tools use the current map, including edits that you have not saved. They do
not change the map or its undo history. An edit, another map tab, or closing the
window stops the measurement. Open the tool again after an edit to use the new
map. Esc cancels point selection.

These tools need the standard Brood War data from your StarCraft installation.
Mining ends after about seven game minutes. Rush ends after about seventeen game
minutes. The engine has known differences on some routes, so test close balance
decisions in the game too.

## Sources and credits

These tools use the independent MIT Map Simulation engine by Map Editor
contributors and its mining/Rush adapters. Their controls and calculations follow
the engine's documented measurement contract. Game data comes from the user's
installation. OpenBW was used as an optional behavior reference during engine
development; its code is not part of these tools.
