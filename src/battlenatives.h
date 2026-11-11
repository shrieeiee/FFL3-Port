// Native routines the battle VM calls (VM op 03), the ones that are pure data shuffling.
// Each is a straight translation of the GB code; the UI ones (menus, messages, animations)
// live with the battle screen. Addresses are bank 0C unless noted.
#pragma once
#include "battlevm.h"

namespace ffl3 {

// Returns true if `addr` is one of the logic natives (and ran it).
bool runLogicNative(BattleVM& vm, const Rom& rom, uint16_t addr);

// 0B:4444: build the combatant table wD000 (party copies + monsters) before the VM starts.
// Reads wC500 (party), wCC36-38 (counts), wCC59-5B (monster ids); wCD37 must be 1.
void battleSetupCombatants(BattleVM& vm, const Rom& rom);

// 0B:7349 + 0B:728B on party record `member` (wCD37 = 0): recompute the equipment / status
// bits after the equipment changed.
void recomputeMember(BattleVM& vm, const Rom& rom, int member);

// 09:521A: use item / spell wCE26 (wCE25 = 0) outside battle on member wCD36 ($80 = the whole
// party). Party records at wC500, wCD37 = 0, wCE1C = the caster (spells cost wCE29 MP).
// rand8 stands in for hFF93; sfx gets the driver's SFX ids. Returns false if it can't be used.
bool fieldUse(BattleVM& vm, const Rom& rom, const std::function<uint8_t()>& rand8,
              const std::function<void(int)>& sfx);

// Small bank-0 helpers the natives use.
void gbCopy(BattleVM& vm, uint16_t& hl, uint16_t& de, int n);     // [hl+] -> [de+], n bytes
void gbFill(BattleVM& vm, uint16_t hl, uint8_t a, int n);

} // namespace ffl3
