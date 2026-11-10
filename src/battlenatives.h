// Native routines the battle VM calls (VM op 03), the ones that are pure data shuffling.
// Each is a straight translation of the GB code; the UI ones (menus, messages, animations)
// live with the battle screen. Addresses are bank 0C unless noted.
#pragma once
#include "battlevm.h"

namespace ffl3 {

// Returns true if `addr` is one of the logic natives (and ran it).
bool runLogicNative(BattleVM& vm, const Rom& rom, uint16_t addr);

// Small bank-0 helpers the natives use.
void gbCopy(BattleVM& vm, uint16_t& hl, uint16_t& de, int n);     // [hl+] -> [de+], n bytes
void gbFill(BattleVM& vm, uint16_t hl, uint8_t a, int n);

} // namespace ffl3
