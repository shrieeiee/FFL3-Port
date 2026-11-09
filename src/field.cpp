#include "field.h"
#include <cstdio>
#include <cstdlib>

namespace ffl3 {

namespace {
int wrap(int v, int n) { return n ? ((v % n) + n) % n : 0; }
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

    npcs_.clear();
    for (const MapObject& o : loadMapObjects(rom_, map)) {
        if (o.flag != 0) continue;           // event-flag gated: hidden until flags are implemented
        Npc n;
        n.obj = o;
        n.m.row = wrap(o.row, rows());
        n.m.col = wrap(o.col, cols());
        n.spriteTile = o.spriteTile();
        npcs_.push_back(n);
    }

    player_ = Mover{};
    player_.facing = facing;
    if (row >= 0 && col >= 0) {
        player_.row = wrap(row, rows());
        player_.col = wrap(col, cols());
    } else {
        // Walkable, non-event block nearest the middle of the map.
        int best = -1;
        for (int r = 0; r < rows(); r++)
            for (int c = 0; c < cols(); c++) {
                const uint8_t a = attrAt(r, c);
                if (!passable(r, c) || (a & 0xc0) == 0xc0 || occupied(r, c, nullptr)) continue;
                const int d = std::abs(r - rows() / 2) + std::abs(c - cols() / 2);
                if (best < 0 || d < best) { best = d; player_.row = r; player_.col = c; }
            }
    }
    layer_ = blockAt(player_.row, player_.col) & 0x40;
    char buf[96];
    std::snprintf(buf, sizeof buf, "entered map %02X at %d,%d", map, player_.row, player_.col);
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

// 00:1024 (leader) - NZ = blocked. Event blocks ($C0+) and $80-class blocks can be entered;
// the event runs once the leader is on the block. Otherwise a block is blocked when
// attr & wC804 (leader flags, $45) & wCA0F (vehicle mask) != 0.
// Emulator-verified on map $10: $02 walkable, $03 blocked, $E3 entered, $FF entered -> event.
bool Field::passable(int row, int col) const {
    const uint8_t a = attrAt(row, col);
    if (a == 0xeb) return false;
    if ((a & 0xc0) == 0xc0) return true;
    if ((a & 0xc0) == 0x80) return true;
    return (a & 0x47 & terrainCaps_ & vehicleMask()) == 0;
}

// wCA0F from the vehicle table 00:087A. Towns/dungeons on foot: $FF.
// World maps (id < 6): $40 (only bit 6 = sea blocks). Inferred from the table + map data;
// not yet checked in the emulator.
uint8_t Field::vehicleMask() const { return map_ < 6 ? 0x40 : 0xff; }

bool Field::occupied(int row, int col, const Mover* self) const {
    row = wrap(row, rows());
    col = wrap(col, cols());
    for (const Npc& n : npcs_)
        if (&n.m != self && n.m.row == row && n.m.col == col) return true;
    if (self != &player_ && player_.row == row && player_.col == col) return true;
    return false;
}

void Field::tick(int dir) {
    Mover& p = player_;
    p.animTick++;
    if (p.moveFrames > 0) {
        if (--p.moveFrames == 0) onStepDone();
        return;
    }
    if (dir < 0) return;
    p.facing = Dir(dir);
    static const int kDr[] = {1, -1, 0, 0}, kDc[] = {0, 0, -1, 1};
    const int nr = p.row + kDr[dir], nc = p.col + kDc[dir];
    if (!noClip && (!passable(nr, nc) || occupied(nr, nc, &p))) return;
    p.dr = kDr[dir];
    p.dc = kDc[dir];
    p.row = wrap(nr, rows());
    p.col = wrap(nc, cols());
    p.moveFrames = Mover::kStepFrames;
}

void Field::onStepDone() {
    layer_ = blockAt(player_.row, player_.col) & 0x40;
    const uint8_t a = attrAt(player_.row, player_.col);     // 00:091b
    if ((a & 0xc0) == 0xc0) runBlockEvent(a & 0x3f);
}

// Event command table at 00:124A (dispatcher 00:1236).
void Field::runBlockEvent(uint8_t cmd) {
    char buf[96];
    switch (cmd) {
    case 0: case 1:                      // 00:13f1 - warp via exit table
        if (!warpByExitTable()) {
            std::snprintf(buf, sizeof buf, "event %02X: no exit for region %d,%d", cmd,
                          player_.row >> 2, player_.col >> 2);
            lastEvent_ = buf;
        }
        return;
    case 2: case 3: {                    // 00:13ac - remember where we came from, then warp
        const Return r{map_, wrap(player_.row - player_.dr, rows()), wrap(player_.col - player_.dc, cols())};
        if (warpByExitTable()) {
            returnStack_.push_back(r);
            if (returnStack_.size() > 8) returnStack_.erase(returnStack_.begin());
        } else {
            std::snprintf(buf, sizeof buf, "event %02X: no exit for region %d,%d", cmd,
                          player_.row >> 2, player_.col >> 2);
            lastEvent_ = buf;
        }
        return;
    }
    case 4: case 5:                      // 00:14aa -> 01:6578 - go back out
        if (!returnStack_.empty()) {
            const Return r = returnStack_.back();
            returnStack_.pop_back();
            enterMap(r.map, r.row, r.col, Dir::Down);
            lastEvent_ = "event: returned to previous map";
        } else {
            lastEvent_ = "event 04: return stack empty";
        }
        return;
    default:
        std::snprintf(buf, sizeof buf, "event %02X (not implemented)", cmd);
        lastEvent_ = buf;
        return;
    }
}

bool Field::warpByExitTable() {
    const int rr = player_.row >> 2, rc = player_.col >> 2;   // 64 px regions
    for (size_t i = 0; i < exits_.size(); i++) {
        const MapExit e = exits_[i];          // copy: enterMap() replaces exits_
        if (e.regionRow != (rr & 0x0f) || e.regionCol != (rc & 0x0f)) continue;
        static const Dir kFacing[] = {Dir::Down, Dir::Up, Dir::Left, Dir::Right};
        const int from = map_;
        // Exit coords are the scroll origin; the leader stands 0x50 px (5 blocks) in.
        if (!enterMap(e.dest, e.row + 5, e.col + 5, kFacing[e.facing & 3])) return false;
        char buf[96];
        std::snprintf(buf, sizeof buf, "exit: map %02X -> %02X (%d,%d)", from, e.dest, e.row + 5, e.col + 5);
        lastEvent_ = buf;
        return true;
    }
    return false;
}

} // namespace ffl3
