# FFL3 port – test build guide

## Setting up
1. Unzip the folder anywhere.
2. Put your Final Fantasy Legend III (US) ROM in the same folder as `ffl3.exe`. Any file name
   ending in `.gb` will do. Nothing from the game is included in the zip.
3. Double-click `ffl3.exe`.

## Controls
| Key | Does |
|---|---|
| Arrow keys / WASD | Move, menu cursor |
| Z or Space | A |
| X or Backspace | B |
| Enter | Start (title screen, naming) |
| M or Shift | Field menu |
| F5 / F9 | Save / load (`ffl3.sav`) |
| Tab | Wide view (256 px) |
| P | Change palette |
| Esc | Quit |

## Reporting a bug
- **F12: bug report.** Press it as soon as something looks wrong. It makes a folder in
  `bug_reports\` next to the exe containing:
  - a screenshot
  - a save of that exact moment
  - your last ten minutes of key presses
  - a log of what the game was doing
  - version and position details

  Then write one line about what went wrong (a note in the folder is fine).
- **Crashes** write the same kind of folder automatically, without the save.
- **F11** takes a plain screenshot.
- **"Not done yet: ..."** across the top of the screen means the port reached a story event it
  hasn't translated. Press F12 there; those reports are the most useful ones.
- Zip up the `bug_reports` folder and send it.

**Real-game recordings help.** If you can, play the same moment in an emulator and record it
or take a save state, so the two can be compared frame by frame.

## Tester shortcuts
**F10** opens a command line at the top of the screen. Type a command and press Enter; Esc
cancels.

| Command | Does |
|---|---|
| `battle XX` | Start formation XX (hex; add 80 for the story fixed one, e.g. `battle da`) |
| `map XX` / `map XX row col` | Jump to map XX (hex) |
| `fx XX` | The next attack plays sprite script XX instead (e.g. `fx 6c` fire, `fx 6b` ripple, `fx 28` vortex) |
| `effect XX` | The next attack plays effect XX (00-17) |
| `death N` | Monsters die with death style N (0-3; 2 = boss second form) |
| `normal` | Undo fx / effect / death |
| `heal` | Full HP/MP, statuses cleared |
| `flag XX 1` / `flag XX 0` | Set / clear story flag XX |

Other keys:
- F1: shows collision
- F2: walk through walls
- F3: random battles on/off
- F4: a random battle now
- Page Up / Page Down: previous / next map

## Checklist
For each item, compare against the real game; anything different is worth an F12.

**Opening**
- [ ] Title logo, New Game / Continue, cursor
- [ ] Story crawl (text speed, the water shimmering)
- [ ] Naming screen: letters, delete, defaults, faces
- [ ] Simulator battle, then the cutscenes until you get control on the Talon

**Field**
- [ ] Walking speed, collisions, doors, stairs, map edges wrapping
- [ ] NPCs wandering, talking, facing you
- [ ] Animated tiles (water flowing, waterfalls, lights) on every map
- [ ] Chests (item, "Empty" the second time), shops, inns
- [ ] Random encounter rate in each area
- [ ] Story events in order (flags, items given, people joining or leaving)
- [ ] Talon: cockpit console, Engine / Weapon / Option, flying, landing

**Menus**
- [ ] Items, Equip (stats changing), Magic outside battle, Status, Save
- [ ] Item and spell names, counts, descriptions

**Battles**
- [ ] Fight / Run / Auto Set
  - Auto Set: "Auto" label on the member; they skip choosing and repeat last round's command
- [ ] Each command:
  - Weapon, Talent, Magic (MP cost shown), Item
  - Targets: one monster, a group, a member, everyone
- [ ] Messages and their timing ("A mighty blow!", "Missed!", "Won!", gold / items received)
- [ ] Party row on screen:
  - members walking on the spot
  - stepping forward to act
  - silhouettes when knocked out
- [ ] Attack animations over monsters, damage numbers bouncing, "Miss"
- [ ] Spell effects:
  - fire, bubbles, falling rocks, orbs, clouds
  - ripple, shimmer, vortex, converging sparks
- [ ] Monster deaths:
  - the normal dissolve
  - boss deaths (falling apart in lines; bounce, flash and melt)
  - a boss changing to its second form
- [ ] Level-ups and stat growth after battles
- [ ] Losing:
  - story fights carry on
  - in the wild, "Fight again?" Yes retries, No goes to the title

**Sound**
- [ ] Map music, battle music, victory, sound effects (menu, hits, spells, doors)

**Saving**
- [ ] Save in the menu and with F5, Continue from the title, F9

## Known not done yet
Each of these shows a "Not done yet" notice when it comes up:
- Event $10 $5 / $8 / $A / $C / $F
- Event $12 $07-$09 / $0D-$0F / $11

These cover vehicle changes, a member transforming, and the ending.

The last few steps of the prologue walk on the Talon are placed rather than walked.
