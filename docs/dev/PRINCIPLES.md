# StarDraft principles

The rules that each change to StarDraft follows: the product, new features, the UI and the text. Other docs give the
details: CONTROLS.md (the control kit),
DEVELOPING.md (build and tests), FEEDBACK.md (what to do with feedback), ROADMAP.md (what comes next).

## Product

**The map editor that StarCraft never had.** Classic editing and EUD editing in one program. EUD triggers are the
normal trigger system, not a trick for experts. Map, Data and Triggers are one workflow.

**User friendliness first: a control agrees with its value.** The type of a value selects its control, its checks
and its help. A yes/no value is a toggle, a unit type is a list that you can search, a weapon is the weapon list. When
the type of a value follows a property, the control changes with the property. Never use a plain text box when a
better control exists. Show values in the units of the player: seconds, not frames; hit points, not x 256. A value
that is not clear shows purple, so that the user can correct it.

**Rules come from the game's data.** Placement, terrain transitions, names, ranges, iscript: read them from the
game's files (units.dat, CV5, iscript.bin, ...) or learn them from Blizzard maps and eudplib. Do not write them by hand
for each case. Mods replace those files, thus hand-written rules are wrong for mods.

**Each thing on the screen has a place in a file.** A text is stored (for example a variable's description in
`triggers.json`) or comes from stored data and game data. Do not show what cannot be stored.

**Clean output.** The generated epScript is clean, easy to read and deterministic: the same project gives the same
code each time.

**No desync.** In Run local block (`if (IsUserCP())`), actions change only what the player's computer shows or plays
(images, strings, sounds, wireframes, buttons, names, colors). The game sends other changes over the network or
simulates them, and they make the game desync. The checker refuses them.

**Speed.** A single edit on a 256 x 256 map takes less than 50 ms, and frames stay under 16 ms. Measure with `perf:`
and `render_bench` before and after a change that can make things slower. Load large data when it is necessary: the
map's sound files load on a thread after the scenario; portraits and HD pictures load when they show first.

**Undo and valid maps.** Each edit is one undo step, and the map stays valid when you save it.

**Stability before a release.** Near a release date, fix defects and do not start new work. FEEDBACK.md gives the
order of defects.

## New features

A new feature must be easy to find for the users who need it. For all other users, the program must stay as simple as
it was. An agent asks the user before it builds a new feature (FEEDBACK.md, "What an agent asks first").

### From a request to the roadmap

Discord feedback and GitHub issues feed ROADMAP.md. FEEDBACK.md tells how to collect and rank them.

- **A defect** (StarDraft does not do what it says, or does not agree with the game) gets a fix in the order of
  FEEDBACK.md. It needs no case.
- **A feature request or a change request** gets into the roadmap only with a case. Many requests do not. That is
  normal: the roadmap is not a list of all requests.

**The case.** Write it on the feedback item or the issue. It answers each question in one or two sentences:

1. **Need.** What does the user try to do, and what stops them now? A user's solution is a hint, not a
   specification.
2. **Who.** Which user groups have this need (ROADMAP.md, "Who StarDraft is for")? How many people asked? Count
   people, not messages.
3. **Better.** How does the experience get better for them? Say it as a result that you can see: a task that was not
   possible, fewer steps, fewer errors, less waiting, or a result that agrees with the game.
4. **Cost.** What changes for the other groups? Name each group that sees, learns or waits more. "Nothing" is the
   best answer.
5. **Smallest change.** Can an existing feature do it with a small change? A new value of an existing control is
   better than a new control, and a new control is better than a new window. If the game's data or a rule can
   decide, do not add a setting.

**What each group keeps.** A change must not take these from a group. If a change takes one, it is no, or it waits
until it can keep it.

| Group | Keeps |
|---|---|
| Melee mappers | Classic maps with no EUDs: up to 3,400 units, save and load, replays. Fast terrain tools. No EUD controls on a melee map. |
| UMS scenario mappers | Classic triggers that are fast to edit, also hundreds of them. No EUD work to make a classic map. |
| Campaign makers | The same settings and story across the maps of a campaign. |
| New EUD mappers | No code necessary: typed blocks in plain sentences, help on each block, clear errors. No raw numbers where a name or a picker can be. |
| Experienced EUD mappers | Their own .eps files, used as written. Clean, readable and deterministic code from the blocks. Raw values one step away. |
| Modders (after 1.0) | Rules that come from the game's data, thus they also work with a mod's data. |

**The decision.** Each request gets one of these, with the reason in one sentence on the item:

- **Yes:** a new roadmap item, or a change to one. The item has the format of ROADMAP.md: the title, the need as a
  problem, the change, and `*For:*` with the groups.
- **Later:** a good case, but not now (after the release, or after 1.0). Put it in "Next" or "Later" of ROADMAP.md, or
  leave the item open with the reason.
- **No:** `wontfix`, with the reason. Typical reasons: the cost to another group is too high, the need is already
  met by a feature (name it), or it is a "no now" in FEEDBACK.md.

Only the user puts an item into the roadmap or changes its order. An agent writes the case and proposes the decision.

### Design the feature

**The basic rules.**

- Make an existing feature better before you add a new one.
- Add a setting only when users really need both ways. Do not add a setting to stop a discussion.

**Put it where the task is.** A feature goes into the workspace, panel or menu of the object that it changes: a unit
command into the unit's right-click menu and the Unit properties, a trigger value into its pop-out. Do not make a new
place for one feature.

**Three levels.** Put each feature on the lowest level that its users can find.

1. **Always visible:** only what most users of that surface use each time. Each new visible control costs the
   attention of all users. A new control on this level replaces or joins an old one, or it has a strong reason.
2. **One step away:** the menu bar, the right-click menu, a pop-out, a section of a panel that the user opens. Most
   new features go here.
3. **Shown by context:** a control shows only when the selected object, value or project needs it. EUD features show
   in an EUD project; a value's control follows its type (Product, "a control agrees with its value"). A user who
   does not need the feature does not see it.

**Make it easy to find.**

- Each command is in the menu bar, also when a button or a shortcut runs it. Thus a user who reads the menus finds it.
- Name the feature with the words of its users and the game: "Watch in StarCraft", "trigger cycle". A user who
  searches or reads looks for these words.
- An expert's way goes one step behind the simple way, not in place of it: the typed control first, then "A number
  or variable instead…"; the blocks first, then the generated epScript beside them.
- The tooltip of a button or a menu item gives the shortcut.

**Do not change what users know.**

- The default value of a new setting gives the old behavior, unless the old behavior was a defect.
- A new feature does not move existing controls, change existing shortcuts or change what existing maps and
  projects do.
- A new feature does not add a step to a task that does not use it.

**Check before it is done.**

- Look at the surface as a user who does not need the feature. If it looks more complex, move the feature one level
  down.
- Look at it as a user who needs it. Start where that user does the task. If the user cannot find it in two steps
  without help, move it one level up or change its name.
- Add the feature to CONTROLS.md (where it is) and to CHANGELOG.md (what it does for the user).

## UI

**One control kit.** The kit in `apps/editor/ui_kit.inc` draws each control, with the color tokens at the top of
`main.cpp`. CONTROLS.md lists the controls and where each is used. A new kind of control goes into the kit (and the
gallery, View > Control kit) first, then into CONTROLS.md.

**No labels that explain.** A tooltip says what a control does. Empty states stay empty: an empty list or value shows
only "None" (dim, no full stop).

**Buttons for actions, links for entities.**

- An action is always a button: add, remove, reset, rename, open, build, copy. Also a small action under a list or in
  a row is a button (a quiet button or an icon button), not a link.
- A link only goes to another entity: a unit, a weapon, an image, a string, a trigger, a file, a web page. Its text is
  the name of that entity ("Terran Marine", "stat_txt.tbl"). A click on a link opens or selects the entity; it does not
  change data.
- A button text starts with a capital letter, in sentence case ("Add an action", not "+ action" or "add action").

**Edit values in pop-outs.** Inline edits can cause edits that the user did not want. Change a value in a pop-out
with OK and Cancel. A click outside the pop-out cancels; it never saves. No one-click toggles in a trigger sentence.

**Clicks follow Windows.**

- In a popover (a list like a dropdown), one click selects and closes it.
- In a window (a dialog with a list, such as the block picker or a value pop-out), one click selects. A double-click
  or Enter does the action.
- Enter is the default button of a dialog, and Esc is Cancel.

**Keyboard.**

- Each command is in a menu, with an access key (`&File`). Toolbar and rail buttons are shortcuts to menu commands.
- A menu item shows its shortcut. Use the usual Windows shortcuts for the usual commands: Ctrl+Z, Ctrl+Y, Ctrl+S,
  Ctrl+C, Ctrl+V, Del, F2 to rename.
- The tab order follows the order on the screen. The control with the focus always shows it.

**Commands.**

- A command that cannot run now is gray, not hidden. Its tooltip says why, when the reason is not clear.
- Do the action and let Undo restore it. Ask first only when Undo cannot restore it (Text, "Confirmations").
- Show a result in the status bar, not in a message box. Use a message box only when the user must decide or read
  something before the work continues.
- Long work shows its progress, and the user can stop it.

**Layout.**

- Dense, on the grid of the kit (CONTROLS.md, "Scale"). Labels align, and the controls in a row have the same height.
- The surface that the user works on (the map, the trigger list, the .dat values) gets the most space.
- Put the properties of a selected object near the object, or in the panel that always shows them, not in a window far
  away.
- Panels and windows keep their size and position, also after a restart. A window that was on a monitor that is not
  there now opens on the main monitor.

**Display scale.** Each size comes from the kit's scale, never a fixed number of pixels. Icons come from the atlas of
the nearest size (`tools/icongen`), not from a stretched bitmap. Check new UI at 100 % and 200 %, in the dark and the
light theme.

**Color.** Colors come from the theme tokens, never from fixed values in a control. Color never gives a meaning
alone: a text, an icon or a tooltip gives it too.

**Show the state.** The user always sees the current workspace, tool, mode, selection and layer. Each action gives a
result that the user sees at once: the map changes, a status message shows, or the control changes.

**Prevent errors.** Make values that are not valid impossible: a picker shows only the values that the field can
have, and a number box keeps its range. The checker finds the problems that a control cannot prevent, before Build.

**Show, do not make the user remember.** The user selects from lists of names, not from numbers that they must know.
A gesture that the user cannot see (the wheel, a drag on an edge, a double-click, a modifier key) has a visible sign
too: a handle, a hover state, a cursor shape, or a menu command that does the same.

**Agree with the game.** Use the game's names, pictures and order (units.dat order, the command card, the game's
colors for players). A mapper who knows the game knows where to look.

**Help in its place.** Help shows where the user needs it: a tooltip, the help of a block, the description of a .dat
field. A user does not need a manual to do a usual task.

**Tooltips.** The first tooltip in a row of controls shows after a short delay. When a tooltip is open, the tooltip of
the next control shows at once, so that the user can read a row quickly.

**Before a UI change is done**, do a usability check on the real UI. Do not check your own work alone.

1. Name the main task of the surface, for example "give a unit a new weapon and test it in the game".
2. Capture the surface in its first state, as a new user sees it (a snapshot, `--snapshot`, with no window). Then do
   the task and capture each step. Capture at 100 % and 200 %, in the dark and the light theme.
3. Give the captures to a reviewer that did not make the change (a person or a separate agent). The reviewer checks
   them against Nielsen's 10 heuristics and the rules in this section, and gives each problem a place, a severity and
   a fix. Severity: **blocker** (the user cannot do the task, or the UI tells something wrong), **major** (slow or
   not clear), **minor** (polish).
4. Fix the blockers, then the majors. Capture again and check again. A fix is done only when the new capture shows
   it.

Nielsen's 10 heuristics, with the rule for StarDraft:

| Heuristic | In StarDraft |
|---|---|
| Visibility of system status | Show the state (above). Progress for long work. |
| Match the real world | Agree with the game. |
| User control and freedom | Undo for each edit; Cancel and Esc leave each state. |
| Consistency and standards | One control kit; clicks and keys follow Windows. |
| Error prevention | Prevent errors (above). |
| Recognition rather than recall | Show, do not make the user remember. |
| Flexibility and efficiency | Shortcuts for experts; good default values for new users. |
| Aesthetic and minimalist design | Three levels (New features); the work surface gets the most space. |
| Recognize, diagnose and recover from errors | Text, "Errors". |
| Help and documentation | Help in its place. |

If a text must explain how to use a control, change the control.

## Text

Every text follows ASD-STE100 (Simplified Technical English), in American English: each text that the program
shows, the sentences and docs of the block library, the docs in this folder, and code comments.

`tools/ui_text_lint.py` checks most of these rules (docs/dev/DEVELOPING.md, "UI text").

**Capitalization.** Sentence case on every surface: menus, buttons, tabs, section headings, column headers, dialog and
message box titles, block titles and categories. Only proper nouns and names from the game data have capitals:
StarCraft, Terran Marine, GitHub, Discord, EUD, euddraft. The .dat field and section names come from EUD Editor's
names (`src/data/dat_schema.inc`); code and tests use them as keys, so `datLabel()` shows them in sentence case.

**Ellipsis.** Write "…" (`\x2026` in wide literals), never "...". A command has it only when it opens a dialog or a
picker that needs more input before anything happens ("Save as…", "Browse…"). No ellipsis on a command that opens a
browser, runs at once, or opens a page or a workspace. A progress status ends with it ("Building the map…").

**Punctuation.**

- No full stop on menus, buttons, labels, titles, cue banners, status texts and tooltips of one sentence, and checker
  messages.
- When a string has two or more sentences, each sentence has a full stop. Block docs, examples and pitfalls always
  have full stops.
- No colon after a form label.
- Put terms in straight double quotes: "the unit".
- Write "and", not "&".
- Write number ranges as "1 to 1700", not "1–1700" or "0-7".
- Use an em dash only as the placeholder for an empty value.
- At most one "Label: clause" in a string.

**Terms.**

| Use | Not |
|---|---|
| map ("scenario" only for the Scenario menu and the *.chk filter) | scenario |
| Player 1 to Player 12 (a 0-based number only where people type the raw value) | P1, player 1 |
| delete (an object); remove (from a list) | erase |
| trigger cycle ("frame" only for animation and the game's own frame counts; "tick" only in the image player) | trigger loop |
| build | compile |
| technology | tech (in prose) |
| select | pick, choose |
| cannot, could not | can't, unable, failed |
| color, center, gray (US spelling) | colour, centre, grey |
| Could not open the map ("the" every time) | Could not open map |

**Status messages.** The past-tense verb comes first, with no full stop: "Saved the project", "Cleared the
selection", "Copied the location".

**Words.**

- One word has one meaning, and one meaning has one word. Use the term in the table above every time, also when a
  different word reads better.
- Use the names of the game and its data: Terran Marine, trigger cycle, walk tile, stat_txt.tbl. Do not make new
  names for things that the game names.
- No contractions: "do not", "cannot", "could not", "is not".
- No "we", "our" or "I". The program does not talk about itself. "You" and "your" only for the user's own things:
  "your own units", "Open in your editor".
- No filler: please, just, simply, successfully, currently, note that, in order to, be able to, basically, very.
- No praise, jokes or exclamation marks. A text tells a fact or an action; it does not cheer.

**Surfaces.** Each surface has one form.

| Surface | Form | Example | Not |
|---|---|---|---|
| Menu command | Verb and object, or the name of a state. Access key; shortcut after a tab. | "Add an &action…", "&Do not snap" | "Action adder", "Snapping off" |
| Button | Verb, with an object if the verb alone is not clear. 1 to 3 words. Capital first letter. | "Play", "Remove the button", "Browse…" | "OK" when the action has a name, "Go", "Submit" |
| Dialog button | The action of the dialog. Cancel is always "Cancel". | "Save", "Do not save", "Cancel" | "Yes", "No" where the buttons can have names |
| Link | The name of the entity that it opens. Never an action. | "Terran Marine", "stat_txt.tbl" | "+ action", "Reset", "Click here" |
| Label | Noun or noun phrase. No colon, no explanation. | "Weapon", "Hotkey" | "Weapon of the unit:", "Select a weapon here" |
| Section heading | Noun phrase. | "StarCraft folder", "Scripts" | "Set up your StarCraft folder" |
| Window title | The name of the command that opened it, without "…", or the object it shows. | "Unit properties", "Add a condition" | "Unit properties…", "Properties" |
| Message box title | The command that the user did, or the problem. | "Save project", "Could not open the map" | "Error", "Warning", "StarDraft" |
| Tooltip | What the control does or shows, as a noun phrase or one sentence. Shortcut at the end in parentheses. | "Build the map of the project with euddraft (Ctrl+B)" | The label again; "Click to build" |
| Cue banner | In a value field: the default value only. In a search box: what it searches. | "0", "Search units" | "Type a number here" |
| Progress status | Present participle and "…". | "Building the map…" | "Please wait" |
| Empty value | "None", dim, no full stop. An empty table cell: "—". | "None" | "No units yet. Add one to start." |

An icon button with no text has a tooltip with the name of its command. Do not give a meaning only with a color or
an icon: a text or a tooltip says it too.

**Errors.**

- The first part says what failed: "Could not <verb> the <object>". The verb is the user's command: "Could not save
  the map", not "Write error".
- Then, if it helps, one sentence with the cause and one with the action: "Could not save the map. The file is
  read-only. Save it with Save as…". Give the cause in words of the user, not as an error code. Put a Windows error
  text after the cause, not in place of it.
- Do not blame the user ("invalid", "illegal", "wrong input"). Say what is necessary: "A chat text has at most 78
  letters".
- No "Something went wrong" and no "An error occurred". If the cause is not known, say what failed and what to do.
- A checker message tells the rule or the problem as a fact, then the fix if there is one: ""the unit" has a value
  only in For each unit".

**Confirmations.**

- Ask only before an action that Undo cannot restore. If Undo restores it, do it and show a status message.
- Ask with the verb and the object: "Delete the briefing for Player 2?". Add one sentence only if the result is not
  clear: "Delete the project folder? This also deletes the built map."
- The buttons repeat the verb: "Delete", "Cancel".

**Numbers.**

- Write numbers as digits in the UI: "3 triggers", "2.5 seconds". Block docs and examples can use words for small
  numbers ("three groups").
- A unit follows the number with a space: "128 pixels", "8 walk tiles", "1 trigger cycle". Use the units of the game.
- Sizes: "128 x 128". Ranges: "1 to 1700" (see Punctuation).
- Singular and plural must agree with the number: "1 hit", "2 hits". Do not write "hit(s)".

**Make a text shorter.** Do these steps on each new or changed text:

1. Read the text in its place, with its window title, label and section. Delete the words that these already say:
   in "Unit properties", the label is "Name", not "Unit name".
2. Delete the filler words (see Words) and the words that the reader does not need to do the task.
3. Put the important word first: "Damage of the weapon", not "The weapon's amount of damage".
4. One sentence, one fact. Split a sentence with "and" when each part is a different fact.
5. If a label needs an explanation, change the control or put the explanation in the tooltip.

**ASD-STE100.**

- A sentence has at most 20 words in a procedure and 25 words in a description.
- Use the active voice.
- Do not use: allow, enable (except where the game's classic wording has it), may, should, whether, automatically,
  approximately, "starts as" (write "default value").
- Do not repeat one docs sentence in many blocks: write it one time, and refer to that block from the others.
- A placeholder (cue banner) shows the default value only. It does not explain. A search box is the exception (see
  Surfaces).
