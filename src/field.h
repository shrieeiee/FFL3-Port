// Field (overworld/town) state and movement, mirroring the original field code.
// Pure logic - no SDL - so it can be driven headless by tests.
//
// Coordinates: the game keeps positions in pixels; everything that matters (collision,
// events, objects, exits) works on 16x16 blocks, so movers are stored in block units.
#pragma once
#include "mapdata.h"
#include <string>

namespace ffl3 {

enum class Dir : uint8_t { Down, Up, Left, Right };

struct Mover {
    int row = 0, col = 0;          // block the 16x16 body occupies
    Dir facing = Dir::Down;
    int moveFrames = 0;            // >0 while stepping (counts down)
    int dr = 0, dc = 0;            // step direction in blocks
    int animTick = 0;
    static constexpr int kStepFrames = 8;   // 16 px at 2 px/frame
    // Pixel offset of the in-progress step (position is updated when the step starts).
    int subY() const { return -dr * 2 * moveFrames; }
    int subX() const { return -dc * 2 * moveFrames; }
};

struct Npc {
    MapObject obj;
    Mover m;
    uint8_t spriteTile = 0;        // OBJ tile base (wC280)
};

class Field {
public:
    explicit Field(const Rom& rom) : rom_(rom) {}

    // Load a map and place the party leader (block coords). row/col < 0 = pick a spot.
    bool enterMap(int map, int row = -1, int col = -1, Dir facing = Dir::Down);

    // One 60 Hz frame. dir = held d-pad direction (Dir as int), or -1.
    void tick(int dir);

    const Layout& layout() const { return layout_; }
    const MapHeader& header() const { return hdr_; }
    const GfxSet& gfx() const { return gfx_; }
    const std::vector<Npc>& npcs() const { return npcs_; }
    const Mover& player() const { return player_; }
    const std::array<std::array<uint8_t, 16>, 128>& objTiles() const { return objTiles_; }
    const std::array<std::array<uint8_t, 4>, 64>& blockDefs() const { return blockDefs_; }
    int mapId() const { return map_; }
    int rows() const { return layout_.bh; }
    int cols() const { return layout_.bw; }
    uint8_t layer() const { return layer_; }          // wCA12: $00 outside, $40 inside
    uint8_t blockAt(int row, int col) const;
    uint8_t attrAt(int row, int col) const;
    bool passable(int row, int col) const;
    // The 4 BG tiles drawn for a block (01:5ea6: other-layer blocks show block 1 / 0).
    const std::array<uint8_t, 4>& drawnTiles(uint8_t block) const;
    const std::string& lastEvent() const { return lastEvent_; }
    bool noClip = false;

private:
    bool occupied(int row, int col, const Mover* self) const;
    uint8_t vehicleMask() const;
    void onStepDone();
    void runBlockEvent(uint8_t cmd);
    bool warpByExitTable();

    const Rom& rom_;
    int map_ = -1;
    MapHeader hdr_{};
    GfxSet gfx_{};
    Layout layout_;
    std::array<uint8_t, 64> attrs_{};
    std::array<std::array<uint8_t, 4>, 64> blockDefs_{};
    std::vector<Npc> npcs_;
    std::vector<MapExit> exits_;
    std::array<std::array<uint8_t, 16>, 128> objTiles_{};
    Mover player_;
    uint8_t layer_ = 0;
    uint8_t terrainCaps_ = 0x45;   // wC804 = leader slot flags wC2E0 ($45 on foot, from RAM)
    struct Return { int map, row, col; };
    std::vector<Return> returnStack_;  // wC7A0 / wC7C0 (buildings)
    std::string lastEvent_;
};

} // namespace ffl3
