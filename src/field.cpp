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
    holesFilled_ = 0;                                                  // 00:0282
    applyMapPatches();
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
        if (o.kind == 1 && o.id && o.id - 1 < 0x80 && flags_.chest.test(size_t(o.id - 1))) {   // opened chest
            a.obj.sprite = 0x78;
            a.spriteTile = a.obj.spriteTile();
        }
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
    if (hideLeaderOnLoad_) { p.id = 0; hideLeaderOnLoad_ = false; }   // 00:054D: wCA00 (event $12 $00)
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
    setVehicle(vehicle_);
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
    if (vehicle_ < 0 || vehicle_ > 8) return 0xff;              // modes 6-8: $00, flies anywhere
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
    if (map_ < 6 && vehicle_ == 2) return (a & 0x40) != 0;      // 00:1058: the Talon at sea
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
    if (!walks_.empty() || !glides_.empty() || flash_ || trans_) return true;
    for (int i = 1; i < kSlots; i++)
        if (actors_[size_t(i)].moveFrames && !actors_[size_t(i)].wandering) return true;
    return false;
}

void Field::startMove(int slot, Dir d, bool turn, int speed) {
    Actor& a = actors_[size_t(slot)];
    if (turn) a.facing = d;
    a.dr = kDr[int(d)];
    a.dc = kDc[int(d)];
    a.row = wrap(a.row + a.dr, rows());
    a.col = wrap(a.col + a.dc, cols());
    a.speed = speed;
    a.wandering = false;
    a.scripted = false;
    a.moveFrames = 16 / speed;
}

// 00:0FFD: can an object step onto this block? Flags wC2E0 = $47, $45 for ordinary
// townsfolk (00:069C clears bit 1; RAM shows $45 on them).
bool Field::objectPassable(int row, int col) const {
    const uint8_t a = attrAt(row, col);
    if (a == 0xeb) return true;
    if ((a & 0xc0) == 0xc0) return false;
    if ((a & 0xc0) == 0x80) return true;
    return (a & 0x45) == 0;
}

void Field::setVehicle(int mode) {
    vehicle_ = mode;
    Actor& p = actors_[0];
    // 00:088C: modes 1-8 draw the leader from OBJ tile $20 / $28 / $10 (the Talon)
    static const uint8_t tile[9] = {0x00, 0x20, 0x28, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10};
    p.spriteTile = (mode >= 0 && mode < 9) ? tile[mode] : 0;
    Actor& f = actors_[1];
    if (f.used) f.id = mode >= 3 ? 0 : f.savedId;              // the follower boards too
}

void Field::takeOff(int mode) {
    const Dir d = actors_[0].facing;
    trans_ = kTransition;
    enterMap(saved_.map, saved_.row, saved_.col, d);
    setVehicle(mode);
    lastEvent_ = "took off";
}

// 01:631A: A on foot on the Talon's parking spot: go aboard (map $10, scroll ($20, $B0)).
bool Field::boardTalon() {
    const Actor& p = actors_[0];
    if (vehicle_ != 0 || p.moveFrames || map_ != saved_.map || p.row != saved_.row || p.col != saved_.col) return false;
    flags_.event.set(0x09);
    trans_ = kTransition;
    enterMap(0x10, 7, 16, Dir::Up);
    lastEvent_ = "boarded the Talon";
    return true;
}

bool Field::land() {
    if (vehicle_ < 2 || actors_[0].moveFrames) return false;    // 00:0861: modes 2-8 -> 01:4250
    const Actor& p = actors_[0];
    saved_ = {map_, p.row, p.col};                             // 4250: wC6E4 -> wC6E9
    setVehicle(0);
    trans_ = kTransition;
    enterMap(0x10, 5, 16, Dir::Up);                            // 636D: scroll (0, $B0) on map $10
    lastEvent_ = "landed";
    return true;
}

// 0B:78F5 (map load, and event $1F): story changes to a map's blocks. 47 entries: map
// 0B:7982[e], flag 0B:79B1[e] ($FF = always), patch 0B:79E0[e] -> col, row, then 0B:792D's
// rectangle (width << 4 | height, then the blocks row by row).
void Field::applyMapPatches() {
    for (int e = 0; e < 0x2f; e++) {
        if (rom_.b(0x0b, uint16_t(0x7982 + e)) != map_) continue;
        const uint8_t f = rom_.b(0x0b, uint16_t(0x79b1 + e));
        if (f != 0xff && !flags_.test(f)) continue;
        uint16_t p = rom_.w(0x0b, uint16_t(0x79e0 + e * 2));
        const int col = rom_.b(0x0b, p++), row = rom_.b(0x0b, p++);
        const uint8_t hdr = rom_.b(0x0b, p++);
        if (!hdr) continue;
        const int w = hdr >> 4, h = hdr & 15;
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) {
                const size_t at = size_t((row + r) * cols() + col + c);
                const uint8_t v = rom_.b(0x0b, p++);
                if (at < layout_.blocks.size()) layout_.blocks[at] = v;
            }
    }
    blocksVersion_++;
}

// 01:51FE (move type $0B): when the leader walks into the object, it is shoved one block the
// same way at 1 px per frame, if it fits there (00:0FFD; $EB holes are allowed).
bool Field::tryPush(int row, int col, Dir d) {
    row = wrap(row, rows()); col = wrap(col, cols());
    for (int i = 2; i < kSlots; i++) {
        Actor& a = actors_[size_t(i)];
        if (!a.visible() || a.row != row || a.col != col) continue;
        if (a.obj.move != 0x0b || a.moveFrames) return false;
        const int nr = row + kDr[int(d)], nc = col + kDc[int(d)];
        if (!objectPassable(nr, nc) || occupied(nr, nc, i)) return false;
        startMove(i, d, true, 1);
        return true;
    }
    return false;
}

// 01:525B -> event $2B (00:1BE4): a boulder dropped into a $EB hole. 0B:1C51 writes the block
// strip 0B:7CC7 (count/rows nibbles, then blocks) at the boulder; the fourth one on maps
// $7C / $7D / others sets flag $E4 / $E5 / $E6, and with all three the water stops (0A:7CF1).
void Field::boulderLanded(int slot) {
    Actor& a = actors_[size_t(slot)];
    if (playSfx) playSfx(0x1c);
    uint16_t t = 0x7cc7;
    const uint8_t hdr = rom_.b(0x0b, t++);
    const int w = hdr >> 4, h = hdr & 15;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            layout_.blocks[size_t(wrap(a.row + r, rows())) * cols() + wrap(a.col + c, cols())] = rom_.b(0x0b, t++);
    blocksVersion_++;
    a.obj.move = 0x04;                                      // wC2F0 := 0: done
    if (++holesFilled_ != 4) return;
    flags_.event.set(map_ == 0x7c ? 0xe4 : map_ == 0x7d ? 0xe5 : 0xe6);
    if (flags_.test(0xe4) && flags_.test(0xe5) && flags_.test(0xe6) && openText) openText(0xa, 0x7cf1);
}

// 01:4F66 / 5060: move types 0-3 and $1B-$1E take a random step about once every 64 frames
// (rnd & $FC == 0), at 1 px per frame; the facing changes even when the way is blocked.
void Field::wanderStep() {
    for (int i = 2; i < kSlots; i++) {
        Actor& a = actors_[size_t(i)];
        if (!a.visible() || a.proxy || a.moveFrames) continue;
        if (i + 1 < kSlots && actors_[size_t(i + 1)].proxy) continue;   // shop counter pair
        const int t = a.obj.move;
        if (!(t <= 3 || (t >= 0x1b && t <= 0x1e))) continue;
        const uint8_t r = random8();
        if (r & 0xfc) continue;
        static const Dir dirs[4] = {Dir::Down, Dir::Up, Dir::Left, Dir::Right};   // 50DC 5121 51AC 5166
        const Dir d = dirs[r & 3];
        a.facing = d;
        const int nr = a.row + kDr[int(d)], nc = a.col + kDc[int(d)];
        if (!objectPassable(nr, nc) || occupied(nr, nc, i)) continue;
        startMove(i, d, true, 1);
        a.wandering = true;
    }
}

void Field::tick(int dir) {
    if (trans_) { trans_--; return; }                    // fading: nothing moves
    for (Actor& a : actors_) a.animTick++;
    if (flash_) flash_--;
    if (!glides_.empty()) {                                // 00:18F5: one pixel a frame
        Glide& g = glides_.front();
        for (int k = 0; k < 2; k++) {
            if (g.slot[k] < 0) continue;
            Actor& a = actors_[size_t(g.slot[k])];
            a.facing = g.dir[k];
            switch (g.dir[k]) {
            case Dir::Up: a.offY--; break;
            case Dir::Down: a.offY++; break;
            case Dir::Left: a.offX--; break;
            case Dir::Right: a.offX++; break;
            }
            if (a.offY % 16 == 0 && a.offX % 16 == 0) {    // back on the block grid
                a.row = wrap(a.row + a.offY / 16, rows()); a.col = wrap(a.col + a.offX / 16, cols());
                a.offY = a.offX = 0;
            }
        }
        if (--g.frames <= 0) glides_.erase(glides_.begin());
        return;
    }
    // Scripted moves ($27/$28) run one at a time, a step per 8 frames.
    if (!walks_.empty()) {
        Walk& w = walks_.front();
        Actor& a = actors_[size_t(w.slot)];
        if (a.moveFrames == 0) {
            if (w.steps-- > 0) { startMove(w.slot, a.facing, w.turn); a.scripted = true; }
            else walks_.erase(walks_.begin());
        }
    }
    for (int i = 1; i < kSlots; i++) {
        Actor& a = actors_[size_t(i)];
        if (a.moveFrames && --a.moveFrames == 0 && a.obj.move == 0x0b && attrAt(a.row, a.col) == 0xeb)
            boulderLanded(i);
    }
    if (wander && walks_.empty()) wanderStep();

    Actor& p = actors_[0];
    if (p.moveFrames > 0) {
        if (--p.moveFrames == 0) {
            if (p.scripted) layer_ = blockAt(p.row, p.col) & 0x40;   // event moves: no step events
            else onStepDone();
        }
        return;
    }
    if (dir < 0 || busy()) return;
    const Dir d = Dir(dir);
    p.facing = d;
    const int nr = p.row + kDr[dir], nc = p.col + kDc[dir];
    if (!noClip && (!passable(nr, nc) || (occupied(nr, nc, 0) && !tryPush(nr, nc, d)))) return;
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
    trans_ = kTransition;
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
    if (std::getenv("FFL3_EVLOG")) std::fprintf(stderr, "EV %02X %02X map %02X\n", cmd, param, map_);
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
        trans_ = kTransition;
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
    case 0x10: {                                                       // 14c0: sub-commands p >> 4
        const int lo = param & 15;
        switch (param >> 4) {
        case 0x0: if (leaveParty) leaveParty(4); return;               // 1518: guest leaves
        case 0x1: {                                                    // 14F8: guest = 00:151D[p-1]
            static const uint8_t who[6] = {2, 8, 3, 9, 4, 5};
            if (lo >= 1 && lo <= 6 && joinParty) joinParty(4, who[lo - 1]);
            return;
        }
        case 0x2: if (lo == 1 && joinParty) joinParty(3, 6); return;   // 1508: 4th member
        case 0x3: case 0x9: return;                                    // 14EA: nothing
        case 0x4: {                                                    // 15DE -> 01:6A74
            if (lo < 1) return;
            const uint16_t t = uint16_t(0x6aac + (lo - 1) * 3);
            saved_ = {rom_.b(1, uint16_t(t + 2)), rom_.b(1, t) + 5, rom_.b(1, uint16_t(t + 1)) + 5};
            return;
        }
        case 0x6:                                                      // 15B3: facing -> flags $0C/$0D
            flags_.event.reset(0x0c); flags_.event.reset(0x0d);
            if (int(p.facing) & 2) flags_.event.set(0x0c);
            if (int(p.facing) & 1) flags_.event.set(0x0d);
            return;
        case 0x7: enterMap(map_, p.row, p.col, p.facing); return;      // 1524: reload objects
        case 0xb: if (flags_.test(4)) return; [[fallthrough]];         // 15A1
        case 0xe:                                                      // 15A7: back to the saved spot
            if (saved_.map >= 0) enterMap(saved_.map, saved_.row, saved_.col, p.facing);
            return;
        case 0xd: saved_ = {map_, p.row, p.col}; return;               // 15E6
        default:
            std::snprintf(buf, sizeof buf, "event 10 sub %02X (not implemented)", param);
            lastEvent_ = buf;
            if (onUnknownEvent) onUnknownEvent(buf);
            return;
        }
    }
    case 0x12:                                                         // 16b9: sub-commands
        switch (param) {
        case 0x00: hideLeaderOnLoad_ = true; return;                   // 173f: wCA00 = 1
        case 0x01: vehicle_ = 0; return;                               // 1745
        case 0x02: return;                                             // 174c: nothing
        case 0x04: enterMap(map_, p.row, p.col, p.facing); return;     // 174d: redraw map
        case 0x03:                                                     // 1AEA: the screen flashes 4 times
            if (playSfx) playSfx(0x3e);
            flash_ = 32;
            return;
        // Objects 4-6 gliding off in formation (two at a time), 1 px a frame.
        case 0x05:                                                     // 17E2
            glides_.push_back({0x40, {4, 6}, {Dir::Right, Dir::Right}});
            glides_.push_back({0x10, {4, 6}, {Dir::Up, Dir::Right}});
            glides_.push_back({0x40, {4, 6}, {Dir::Up, Dir::Up}});
            return;
        case 0x06:                                                     // 182B
            glides_.push_back({0x30, {4, 5}, {Dir::Down, Dir::Down}});
            glides_.push_back({0x10, {4, 5}, {Dir::Left, Dir::Down}});
            glides_.push_back({0x30, {4, 5}, {Dir::Left, Dir::Left}});
            return;
        case 0x0a:                                                     // 1769
            glides_.push_back({0x20, {4, 5}, {Dir::Down, Dir::Down}});
            glides_.push_back({0x10, {4, 5}, {Dir::Left, Dir::Down}});
            glides_.push_back({0x60, {4, 5}, {Dir::Left, Dir::Left}});
            glides_.push_back({0x10, {4, 5}, {Dir::Up, Dir::Left}});
            glides_.push_back({0x40, {4, 5}, {Dir::Up, Dir::Up}});
            return;
        case 0x0b: runEventCommand(0x0e, 0x37); return;                // 1711
        case 0x0c: runEventCommand(0x0d, 0x02); return;                // 1728
        case 0x10:                                                     // 16EB
            if (pokeRam) { pokeRam(0xc6f3, 0x1b); pokeRam(0xc50d, 0x02); pokeRam(0xc50e, 0xe0); }
            return;
        default:
            std::snprintf(buf, sizeof buf, "event 12 sub %02X (effect, not implemented)", param);
            lastEvent_ = buf;
            if (onUnknownEvent) onUnknownEvent(buf);
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
    case 0x18:                                                         // 1a02
        if (giveItem) giveItem(rom_.b(3, uint16_t(0x762a + param - 1)));
        if (!flags_.test(1) && param >= 0x61)                          // 0F:7C74: story flag for it
            flags_.event.set(rom_.b(0xf, uint16_t(0x7c7e + param - 0x61)));
        return;
    case 0x1c: music = uint8_t(param); return;                          // 1a28
    case 0x1d: lastSfx = uint8_t(param); if (playSfx) playSfx(param); return;                        // 1a2e
    case 0x08: case 0x09:                                              // 14b2: sound, slide, exit
        if (playSfx) playSfx(0x3f);
        p.row = wrap(p.row - 1, rows());                               // 01:661C: wC6E4 -= $10
        if (!warpByExitTable()) p.row = wrap(p.row + 1, rows());
        return;
    case 0x20:                                                         // 1a34: damage floor
        if (hurtParty) hurtParty();
        if (playSfx) playSfx(0x07);
        return;
    case 0x21:                                                         // 1a6d: status cured
        if (cureParty) cureParty();
        if (playSfx) playSfx(0x18);
        return;
    case 0x2d: case 0x2e: case 0x30: case 0x31: return;                // 12db: nothing
    case 0x23: if (talonConsole) talonConsole(); return;               // 19b8 -> 01:7972
    case 0x2a: if (openText) openText(0xa, 0x7a7d); return;            // 1bd7: rst $A0 0A:7A7D
    case 0x1f: applyMapPatches(); return;                              // 137c -> 0B:78F5
    case 0x22:                                                         // 1a84: rest (fade, heal)
        if (playSfx) playSfx(0x17);
        if (healParty) healParty();
        return;
    case 0x25: {                                                       // 1a95: Talon lift
        // Carries the leader one more block the way they face (every other use also shakes).
        const Dir d = p.facing;
        if (passable(p.row + kDr[int(d)], p.col + kDc[int(d)])) startMove(0, d, true);
        return;
    }
    case 0x3f:                                                         // $FF block by the Talon
        // Past the end of the 63-entry table; behaviour taken from the real game (emulator):
        // stepping onto it boards the Talon, landing at (26,7) facing up on map $0B.
        trans_ = kTransition;
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

void Field::openChest(int slot) {             // 00:0A08: the object's sprite becomes $78
    Actor& a = actors_[size_t(slot)];
    a.obj.sprite = 0x78;
    a.spriteTile = a.obj.spriteTile();
}

} // namespace ffl3
