// Field (overworld/town) state, movement and field event commands, mirroring the original.
// Pure logic - no SDL - so it can be driven headless by tests.
//
// Coordinates: the game keeps positions in pixels; collision, events, objects and exits all
// work on 16x16 blocks, so actors are stored in block units.
#pragma once
#include "flags.h"
#include "mapdata.h"
#include <functional>
#include <string>

namespace ffl3 {

// The game's direction codes (wC6EE >> 6): 0 up, 1 right, 2 down, 3 left.
enum class Dir : uint8_t { Up = 0, Right = 1, Down = 2, Left = 3 };

struct Actor {
    bool used = false;                 // slot holds an object (possibly hidden)
    uint8_t id = 0;                    // wC200: 0 = hidden
    uint8_t savedId = 0;               // wC370: id restored by "show"
    MapObject obj{};
    int row = 0, col = 0;              // block the 16x16 body occupies
    Dir facing = Dir::Down;
    int moveFrames = 0;                // >0 while stepping (counts down)
    int dr = 0, dc = 0;                // step direction in blocks
    int animTick = 0;
    uint8_t spriteTile = 0;            // OBJ tile base (wC280)
    static constexpr int kStepFrames = 8;   // 16 px at 2 px/frame
    int subY() const { return -dr * 2 * moveFrames; }
    int subX() const { return -dc * 2 * moveFrames; }
    bool proxy = false;                // companion slot (00:0730): talkable, never drawn
    bool visible() const { return used && id != 0; }
    bool drawn() const { return visible() && !proxy; }
};

class Field {
public:
    static constexpr int kSlots = 16;  // object slot arrays at $C200 (slot 0 = party leader)

    Field(const Rom& rom, GameFlags& flags) : rom_(rom), flags_(flags) {}

    // Load a map and place the leader (block coords). row/col < 0 = pick a spot.
    bool enterMap(int map, int row = -1, int col = -1, Dir facing = Dir::Down);

    // One 60 Hz frame. dir = held d-pad direction (Dir as int), or -1.
    void tick(int dir);

    const Layout& layout() const { return layout_; }
    const MapHeader& header() const { return hdr_; }
    const GfxSet& gfx() const { return gfx_; }
    const std::array<Actor, 16>& actors() const { return actors_; }
    const Actor& player() const { return actors_[0]; }
    const std::array<std::array<uint8_t, 16>, 128>& objTiles() const { return objTiles_; }
    const std::array<std::array<uint8_t, 4>, 64>& blockDefs() const { return blockDefs_; }
    int mapId() const { return map_; }
    int rows() const { return layout_.bh; }
    int cols() const { return layout_.bw; }
    uint8_t layer() const { return layer_; }          // wCA12: $00 outside, $40 inside
    int vehicle() const { return vehicle_; }          // wC6E3
    void setVehicle(int v) { vehicle_ = v; }
    uint8_t blockAt(int row, int col) const;
    uint8_t attrAt(int row, int col) const;
    bool passable(int row, int col) const;
    const std::array<uint8_t, 4>& drawnTiles(uint8_t block) const;
    const std::string& lastEvent() const { return lastEvent_; }
    void setLastEvent(const std::string& s) { lastEvent_ = s; }

    // Slot of the object directly in front of the leader (01:431e), or -1. Turns it to face us.
    int faceObjectInFront();
    // Event command (00:1236) with parameter (wCA28).
    void runEventCommand(int cmd, int param);
    // True while a scripted actor move is animating (scripts wait for it).
    bool busy() const;

    // Hooks the game layer provides.
    std::function<void(int bank, uint16_t addr)> openText;   // show a text (rst $a0 etc.)
    std::function<void(int formation)> startBattle;           // event $17
    std::function<void(int item)> giveItem;                   // event $18
    uint8_t music = 0, lastSfx = 0;                           // hCurrentMusic / hSFX
    bool noClip = false;

private:
    bool occupied(int row, int col, int selfSlot) const;
    uint8_t vehicleMask() const;
    void onStepDone();
    bool warpTo(int map, uint8_t b2, uint8_t b3);   // 00:1468 - map, row|facing, col|facing
    bool warpByExitTable();
    void startMove(int slot, Dir d, bool turn);
    void actorCommand(int param);                   // event $26 (01:66e9)

    const Rom& rom_;
    GameFlags& flags_;
    int map_ = -1;
    MapHeader hdr_{};
    GfxSet gfx_{};
    Layout layout_;
    std::array<uint8_t, 64> attrs_{};
    std::array<std::array<uint8_t, 4>, 64> blockDefs_{};
    std::array<Actor, 16> actors_{};
    std::vector<MapExit> exits_;
    std::array<std::array<uint8_t, 16>, 128> objTiles_{};
    struct Walk { int slot, steps; bool turn; };
    std::vector<Walk> walks_;          // queued scripted moves ($27/$28)
    uint8_t layer_ = 0;
    int vehicle_ = 0;                  // wC6E3: 0 on foot in towns, 2 on foot on the world map
    uint8_t leaderFlags_ = 0x45;       // wC804 = leader slot flags wC2E0 ($45 on foot, from RAM)
    struct Return { int map, row, col; };
    std::vector<Return> returnStack_;  // wC7A0 / wC7C0
    std::string lastEvent_;
};

} // namespace ffl3
