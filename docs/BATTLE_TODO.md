# Battle VM port - plan / state (user chose: hand-translate everything to C++)

Done: VM interpreter (src/battlevm.cpp), 30 logic natives (src/battlenatives.cpp), replay
harness (tests/vmreplay.cpp, tests/test_vm.sh, tools/trace.c, tools/rec.py). Recordings in
/root/ffl3/rec (not in git: they contain game data). All 12 recordings pass.

Done since: step 1 below (battleSetupCombatants matches the real table on every recording;
record +4 = byte $284 of the last group's decompressed picture plane B, see pictureResidue),
natives 6B25/6B28/6B44.

Uninitialised reads (vmreplay UNINIT=1): besides the setup data, the VM reads RAM left over
from the previous battle: wC9E6, wC9F8/F9 (message state, normally $FF), wCE00-13 (last
commands), wCE8C, wD875-77, plus wC6E3 (vehicle), wCA2D (area), wCC59/5C/5E-60, wDDFF.
Zeroing wC900-C9FF at entry changes the control flow, so the port must keep one persistent
battle RAM image across battles (initial values: WRAM cleared, wC9F8/F9 = $FF, wCE8C = $FF).
Tools: vmreplay SETUP=1 (setup check), BUILD=1 (diff a built entry image), ZERO=a-b,
UNINIT=1, NATPRE=/NATDUMP=addr (print state around a native).

Now running in the game (src/battle.cpp): setup, VM, logic natives, the command phase
(Fight-Weapon / Run / Auto as "everyone attacks"), messages (bank 9 text, battle name
codes; $1E = talent name when a monster acts), waits for animations. Also translated: 1E46
(EXP table), 2499/24C7 (32-bit mul/div). wFF93 ticks every 150 VM statements (a frame).

Next, in order:
1. (done) Port the pre-VM battle setup 0B:4000 -> 0B:4444 (combatant table): fill wD000-D67F $FF;
   copy present party records (c500+i*60, +3 != FF) 0x60 bytes to D000+i*80; copy 4 bytes
   c518+i*60 -> d070+i*80 (if c518 != FF); zero 6 bytes at d06a+i*80; then per group g with
   count wCC36+g: build the monster record at wD680 (4526: clear d690-d6ff, name 0E:7533 ->
   d685; 453A: stats 0B:4A19 -> d68d/d690..; 45AB scales HP/MP x16 for monster id >= $BB;
   45CB: d680 = 0F 00 00, talents 0B:533D (ids >= $30 -> d6c0, spells via 6E74 learn bit),
   7349 + 728B (equipment/status sums: 72E1/7305/7422, table 0B:7104), drops 0B:470D ->
   d683/d68f/d6e8, d6d7 = (d69a + level) / 2 + $4B, 0B:466B only for natives) and copy it
   count times from d280 on. Then 0B:708B (spell-bit pass over every combatant: 70A4/70B9/
   70D1/70DE, table 0B:7543). Verify against rec/*_enter.mem (D000-D6FF, CC00-CCFF).
   The same builder gives natives 6B25/6B28 (0B:44F0/4502).
2. Natives 6B44 (7349+728B), 1D71/1D74 (end-of-battle field routines), 1E46, 2499, 24C7.
3. UI natives in the port's battle screen: 1D69 command phase (writes wCE00+4i, see
   RE_NOTES), 1D77 retry menu, 5FB7 messages (text VM + battle name codes), 6001 and
   6B68/6B6C/6B71/6B7F animations/sounds (timed waits).
4. Battle class: build the RAM image from game state (party c500, inventory c6ff, flags
   c760, vehicle c6e3, formation cc59, hFFCA), run 0B:4000 setup, VM from 4A2D, natives,
   then copy back (4B9F writes the party records). Replace the provisional rules.
5. More recordings: magic (needs spells - poke didn't show them in the list), items,
   status effects, a party member dying, escape success/fail, wins with drops.

## Command menus (Talent / Magic / Item) - findings so far
- wCE00+4i = command index << 4 (0 Weapon, 1 Talent, 2 Magic, 3 Item) | low-nibble flags
  (bit 0 = auto), +1 = list cursor memory (row*4+col), +2 = target, +3 = action id.
  The command index is also the action-data type for 09:5466 (1 = 09:57BC talents,
  2 = 09:5A50 spells, 3 = 09:5B70 items >= $C0, 0 = items via 09:55C4).
- Lists: Talent = record +$48-4F (0B:6557, names via 1E8F), Magic = known spells from the
  bits at +$6A (0B:6589, list via rst18 0C; MP check against wCE29 = cost from 09:5D87),
  Item = the inventory at wC6FF (0B:6527; ids >= $C0 have their count taken here).
- Usability / target: 0B:66EC takes wCE28 (from 09:5D87) -> wCE1D; 0 = can't use;
  bit 7: bit 0 = choose, bit 2 = all (wCE1C = $81) ...; bit 7 clear: bit 3 = choose,
  bit 4 = all enemies (wCE1C = $80), else no target. Which SIDE a choice starts on comes
  from elsewhere (Cure1 and Fire items both have kind $28 but Cure1 starts on the party).
  Target byte: 0-4 party member, 5+ enemy combatant, $80 | group+1 = whole group.
- Recording these in the emulator is flaky: the game drops presses that arrive while a list
  opens or a cursor blinks, so scripted key timing desyncs. Needs a closed-loop recorder
  that waits for the menu state (e.g. wCDEA / cursor RAM) before each press.
- Plan: port the 0B menu module (5BDF ... 6900) to C++ properly rather than guess.
