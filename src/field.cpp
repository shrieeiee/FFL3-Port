#include "field.h"
#include "battledata.h"
#include <cstdio>
#include <cstdlib>

namespace ffl3 {

namespace {
int wrap(int v, int n) { return n ? ((v % n) + n) % n : 0; }
const int kDr[] = {-1, 0, 1, 0}, kDc[] = {0, 1, 0, -1};   // indexed by Dir
Dir opposite(Dir d) { return Dir((int(d) + 2) & 3); }
}

bool Field::enterMap(int map, int row, int col, Dir facing) {
    const MapHeader h = loadMapHeader(rom_, map);
    Layout lay = loadLayout(rom_, h.layout);
    if (!lay.valid) return false;
    map_ = map;
    hdr_ = h;
    layout_ = std::move(lay);
    gfx_ = loadGfxSet(rom_, hdr_.gfxset);
    attrs_ = loadBlockAttrs(rom_, gfx_);
    blockDefs_ = loadBlockDefs(rom_, gfx_);
    objTiles_ = loadObjTiles(rom_, hdr_.unk1e);
    exits_ = loadMapExits(rom_, map);
    walks_.clear();
    if (hdr_.music != 0x1f) music = hdr_.music;

    for (Actor& a : actors_) a = Actor{};
    // 00:05bf: objects fill slots from 2 upward. Shopkeepers (kind 2) and kind-0 objects with
    // id >= $DF also get a companion in the next slot (00:0730): an invisible copy one block in
    // front of them, which is what you talk to across a counter. Flag-gated objects (byte 0
    // != 0) only appear once that event flag is set; hidden ones keep their id for "show".
    const std::vector<MapObject> objs = loadMapObjects(rom_, map);
    int slot = 2;
    for (const MapObject& o : objs) {
        if (slot >= kSlots) break;
        Actor& a = actors_[size_t(slot++)];
        a.used = true;
        a.obj = o;
        a.savedId = o.id;
        a.id = (o.flag == 0 || flags_.test(o.flag)) ? o.id : 0;
        a.row = wrap(o.row, rows());
        a.col = wrap(o.col, cols());
        a.facing = Dir(((o.flagsY >> 7) << 1) | (o.flagsX >> 7));   // wC2D0 bits 7-6 (b2 bit 7 = high bit; checked vs RAM)
        a.spriteTile = o.spriteTile();
        if ((o.kind == 2 || (o.kind == 0 && o.id >= 0xdf)) && slot < kSlots) {
            Actor& c = actors_[size_t(slot++)];
            c = a;
            c.proxy = true;
            c.row = wrap(a.row + kDr[int(a.facing)], rows());
            c.col = wrap(a.col + kDc[int(a.facing)], cols());
        }
    }

    Actor& p = actors_[0];
    p.used = true;
    p.id = p.savedId = 1;
    p.facing = facing;
    if (row >= 0 && col >= 0) {
        p.row = wrap(row, rows());
        p.col = wrap(col, cols());
    } else {
        int best = -1;
        for (int r = 0; r < rows(); r++)
            for (int c = 0; c < cols(); c++) {
                const uint8_t a = attrAt(r, c);
                if (!passable(r, c) || (a & 0xc0) == 0xc0 || occupied(r, c, 0)) continue;
                const int d = std::abs(r - rows() / 2) + std::abs(c - cols() / 2);
                if (best < 0 || d < best) { best = d; p.row = r; p.col = c; }
            }
    }
    layer_ = blockAt(p.row, p.col) & 0x40;
    char buf[96];
    std::snprintf(buf, sizeof buf, "entered map %02X at %d,%d", map, p.row, p.col);
    lastEvent_ = buf;
    return true;
}

uint8_t Field::blockAt(int row, int col) const {
    return layout_.blocks[size_t(wrap(row, rows())) * cols() + wrap(col, cols())];
}

uint8_t Field::attrAt(int row, int col) const { return attrs_[blockAt(row, col) & 0x3f]; }

const std::array<uint8_t, 4>& Field::drawnTiles(uint8_t block) const {
    uint8_t b = block & 0x3f;
    if ((block & 0x40) && !(layer_ & 0x40)) b = 1;       // inside block seen from outside
    else if (!(block & 0x40) && (layer_ & 0x40)) b = 0;  // outside block seen from inside
    return blockDefs_[b];
}

// wCA0F from the vehicle table 00:087A (mask, wC6F1 pairs). Mode 0 = on foot in towns ($FF),
// mode 2 = on foot on the world map ($40: only the sea blocks). Events $0A/$0B/$0C switch it.
uint8_t Field::vehicleMask() const {
    if (vehicle_ < 0 || vehicle_ > 5) return 0xff;
    return rom_.b(0, uint16_t(0x87a + vehicle_ * 2));
}

// 00:1024 (leader) - NZ = blocked. Event blocks ($C0+) and $80-class blocks can be entered;
// the event runs once the leader is on the block. Otherwise blocked when
// attr & wC804 (leader flags) & wCA0F (vehicle mask) != 0.
bool Field::passable(int row, int col) const {
    const uint8_t a = attrAt(row, col);
    if (a == 0xeb) return false;
    if ((a & 0xc0) == 0xc0) return true;
    if ((a & 0xc0) == 0x80) return true;
    return (a & 0x47 & leaderFlags_ & vehicleMask()) == 0;
}

bool Field::occupied(int row, int col, int selfSlot) const {
    row = wrap(row, rows());
    col = wrap(col, cols());
    for (int i = 0; i < kSlots; i++) {
        if (i == selfSlot) continue;
        const Actor& a = actors_[size_t(i)];
        if (a.visible() && a.row == row && a.col == col) return true;
    }
    return false;
}

bool Field::busy() const {
    if (!walks_.empty()) return true;
    for (int i = 1; i < kSlots; i++)
        if (actors_[size_t(i)].moveFrames) return true;
    return false;
}

void Field::startMove(int slot, Dir d, bool turn) {
    Actor& a = actors_[size_t(slot)];
    if (turn) a.facing = d;
    a.dr = kDr[int(d)];
    a.dc = kDc[int(d)];
    a.row = wrap(a.row + a.dr, rows());
    a.col = wrap(a.col + a.dc, cols());
    a.moveFrames = Actor::kStepFrames;
}

void Field::tick(int dir) {
    for (Actor& a : actors_) a.animTick++;
    // Scripted moves ($27/$28) run one at a time, a step per 8 frames.
    if (!walks_.empty()) {
        Walk& w = walks_.front();
        Actor& a = actors_[size_t(w.slot)];
        if (a.moveFrames == 0) {
            if (w.steps-- > 0) startMove(w.slot, a.facing, w.turn);
            else walks_.erase(walks_.begin());
        }
    }
    for (int i = 1; i < kSlots; i++)
        if (actors_[size_t(i)].moveFrames) actors_[size_t(i)].moveFrames--;

    Actor& p = actors_[0];
    if (p.moveFrames > 0) {
        if (--p.moveFrames == 0) onStepDone();
        return;
    }
    if (dir < 0 || busy()) return;
    const Dir d = Dir(dir);
    p.facing = d;
    const int nr = p.row + kDr[dir], nc = p.col + kDc[dir];
    if (!noClip && (!passable(nr, nc) || occupied(nr, nc, 0))) return;
    startMove(0, d, true);
}

void Field::onStepDone() {
    Actor& p = actors_[0];
    layer_ = blockAt(p.row, p.col) & 0x40;
    const uint8_t a = attrAt(p.row, p.col);     // 00:091b
    if ((a & 0xc0) == 0xc0) { runEventCommand(a & 0x3f, 0); return; }
    // 01:4617: after each step, a battle with chance 00:119A[header byte 4 >> 5] / 256.
    const int rate = encounterRate(rom_, hdr_.music_hi);
    if (encounters && rate && random8() < rate && startBattle)
        startBattle(randomFormation(rom_, encounterArea(), vehicle_, random8()));
}

int Field::encounterArea() const {
    if (!flags_.test(0x31)) return 0;                  // 00:0409
    if (map_ == 1 && flags_.test(0xce)) return 0x43;
    return hdr_.unk2d;
}

int Field::faceObjectInFront() {
    const Actor& p = actors_[0];
    const int r = wrap(p.row + kDr[int(p.facing)], rows()), c = wrap(p.col + kDc[int(p.facing)], cols());
    for (int i = 1; i < kSlots; i++) {
        Actor& a = actors_[size_t(i)];
        if (!a.visible() || a.row != r || a.col != c) continue;
        if (a.obj.kind == 0 && !a.proxy) a.facing = opposite(p.facing);
        return i;
    }
    return -1;
}

bool Field::warpTo(int map, uint8_t b2, uint8_t b3) {
    // 00:1468: wC6EE = (b3 bit7) << 7 | (b2 bit7) << 6; scroll = (b & $7F) * 16 px.
    // The leader stands $50 px (5 blocks) into the screen.
    const Dir f = Dir(((b3 >> 7) << 1) | (b2 >> 7));
    return enterMap(map, (b2 & 0x7f) + 5, (b3 & 0x7f) + 5, f);
}

bool Field::warpByExitTable() {
    const Actor& p = actors_[0];
    const int rr = p.row >> 2, rc = p.col >> 2;   // 64 px regions (00:13f1)
    for (size_t i = 0; i < exits_.size(); i++) {
        const MapExit e = exits_[i];               // copy: enterMap() replaces exits_
        if (e.regionRow != (rr & 0x0f) || e.regionCol != (rc & 0x0f)) continue;
        const int from = map_;
        if (!warpTo(e.dest, e.rawRow, e.rawCol)) return false;
        char buf[96];
        std::snprintf(buf, sizeof buf, "exit: map %02X -> %02X (%d,%d)", from, e.dest, player().row, player().col);
        lastEvent_ = buf;
        return true;
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "no exit for region %d,%d on map %02X", rr, rc, map_);
    lastEvent_ = buf;
    return false;
}

// 01:66e9 - param: low nibble = slot (0 = leader), high nibble = action.
void Field::actorCommand(int param) {
    const int slot = param & 0x0f, act = param >> 4;
    Actor& a = actors_[size_t(slot)];
    switch (act) {
    case 0: case 1: case 2: case 3: a.facing = Dir(act); break;   // face
    case 4: a.id = 0; break;                                      // hide
    case 5: a.id = a.savedId; break;                              // show
    default: break;                                               // 6/7: animation mode
    }
}

// Event command table at 00:124A (dispatcher 00:1236).
void Field::runEventCommand(int cmd, int param) {
    char buf[96];
    Actor& p = actors_[0];
    auto pushReturn = [&] {
        returnStack_.push_back({map_, wrap(p.row - p.dr, rows()), wrap(p.col - p.dc, cols())});
        if (returnStack_.size() > 8) returnStack_.erase(returnStack_.begin());
    };
    auto popReturn = [&] {
        if (returnStack_.empty()) { lastEvent_ = "return stack empty"; return; }
        const Return r = returnStack_.back();
        returnStack_.pop_back();
        enterMap(r.map, r.row, r.col, Dir::Down);
        lastEvent_ = "returned to previous map";
    };
    switch (cmd) {
    case 0x00: case 0x01: warpByExitTable(); return;                   // 13f1
    case 0x02: case 0x03: pushReturn(); if (!warpByExitTable()) returnStack_.pop_back(); return;  // 13ac
    case 0x04: case 0x05: popReturn(); return;                         // 14aa
    case 0x0a: vehicle_ = 0; pushReturn(); if (!warpByExitTable()) returnStack_.pop_back(); return;  // 13a8
    case 0x0b: case 0x0c: vehicle_ = 2; popReturn(); return;           // 14a5
    case 0x0d: case 0x0e: {                                            // 1384/1389: 06:6BA4
        const uint16_t t = uint16_t(0x6ba4 + (param - 1) * 3);
        warpTo(rom_.b(6, t), rom_.b(6, uint16_t(t + 1)), rom_.b(6, uint16_t(t + 2)));
        return;
    }
    case 0x0f: case 0x2f: {                                            // 13a0 -> 01:64b4
        const uint16_t maps = cmd == 0x0f ? 0x6503 : 0x6563, ptrs = cmd == 0x0f ? 0x6523 : 0x6569;
        for (int i = 0; i < 0x20; i++)
            if (rom_.b(1, uint16_t(maps + i)) == map_) {
                if (openText) openText(0xa, rom_.ptr(1, ptrs, i));
                return;
            }
        return;
    }
    case 0x12:                                                         // 16b9: sub-commands
        switch (param) {
        case 0x01: vehicle_ = 0; return;                               // 1745
        case 0x02: return;                                             // 174c: nothing
        case 0x04: enterMap(map_, p.row, p.col, p.facing); return;     // 174d: redraw map
        case 0x0b: runEventCommand(0x0e, 0x37); return;                // 1711
        case 0x0c: runEventCommand(0x0d, 0x02); return;                // 1728
        default:
            std::snprintf(buf, sizeof buf, "event 12 sub %02X (effect, not implemented)", param);
            lastEvent_ = buf;
            return;
        }
    case 0x17:                                                         // 19ae: shop 01:6FD4
        if (openShop) openShop(param - 1);
        std::snprintf(buf, sizeof buf, "shop %02X (not implemented)", param - 1);
        lastEvent_ = buf;
        return;
    case 0x1b:                                                         // 19d0: battle
        if (startBattle)
            startBattle(param != 0xff ? (param | 0x80) : randomFormation(rom_, 0, vehicle_, random8()));
        return;
    case 0x18: if (giveItem) giveItem(rom_.b(3, uint16_t(0x762a + param - 1))); return;  // 1a02
    case 0x1c: music = uint8_t(param); return;                          // 1a28
    case 0x1d: lastSfx = uint8_t(param); return;                        // 1a2e
    case 0x25: {                                                       // 1a95: Talon lift
        // Carries the leader one more block the way they face (every other use also shakes).
        const Dir d = p.facing;
        if (passable(p.row + kDr[int(d)], p.col + kDc[int(d)])) startMove(0, d, true);
        return;
    }
    case 0x3f:                                                         // $FF block by the Talon
        // Past the end of the 63-entry table; behaviour taken from the real game (emulator):
        // stepping onto it boards the Talon, landing at (26,7) facing up on map $0B.
        enterMap(0x0b, 26, 7, Dir::Up);
        lastEvent_ = "boarded the Talon";
        return;
    case 0x26: actorCommand(param); return;
    case 0x27: case 0x28: {                                            // 01:67f5 / 01:691e
        const int steps = (param >> 4) ? (param >> 4) : 1;
        walks_.push_back({param & 0x0f, steps, cmd == 0x27});
        return;
    }
    default:
        std::snprintf(buf, sizeof buf, "event %02X param %02X (not implemented)", cmd, param);
        lastEvent_ = buf;
        return;
    }
}

} // namespace ffl3
