// Field (overworld/town) state, movement and field event commands, mirroring the original.
// Pure logic - no SDL - so it can be driven headless by tests.
//
// Coordinates: the game keeps positions in pixels; collision, events, objects and exits all
// work on 16x16 blocks, so actors are stored in block units.
#pragma once
#include "flags.h"
#include "mapdata.h"
#include <optional>
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
    int speed = 2;                     // px per frame (wC310; wandering NPCs walk at 1)
    bool wandering = false;            // the current step is a random walk (doesn't block scripts)
    bool scripted = false;             // the current step is an event move (no step events / battles)
    uint8_t spriteTile = 0;            // OBJ tile base (wC280)
    static constexpr int kStepFrames = 8;   // 16 px at 2 px/frame
    int offY = 0, offX = 0;            // pixels moved by a cutscene glide (00:1874-18AF)
    int subY() const { return -dr * speed * moveFrames + offY; }
    int subX() const { return -dc * speed * moveFrames + offX; }
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
    void openChest(int slot);
    const Actor& player() const { return actors_[0]; }
    const std::array<std::array<uint8_t, 16>, 128>& objTiles() const { return objTiles_; }
    const std::array<std::array<uint8_t, 4>, 64>& blockDefs() const { return blockDefs_; }
    int mapId() const { return map_; }
    int rows() const { return layout_.bh; }
    int cols() const { return layout_.bw; }
    uint8_t layer() const { return layer_; }          // wCA12: $00 outside, $40 inside
    int vehicle() const { return vehicle_; }          // wC6E3
    uint8_t blockAt(int row, int col) const;
    int blocksVersion() const { return blocksVersion_; }
    const Rom& rom() const { return rom_; }   // bumped when events rewrite blocks
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
    std::function<void(int hffca)> startBattle;               // event $1B / random encounter:
                                                              // formation | $80 = fixed counts
    std::function<void(int shop)> openShop;                   // event $17 (01:6FD4)
    std::function<void(int item)> giveItem;                   // event $18
    std::function<void(int id)> playSfx;
    std::function<void(const char*)> onUnknownEvent;   // an event the port has no code for yet (logged for testers)
    bool wander = false;               // NPC random walks (01:4F66); off for headless tests
    // The Talon: the spot it is parked at (wC6E9-C6ED), flying off from it and landing.
    struct Spot { int map, row, col; };
    Spot savedSpot() const { return {saved_.map, saved_.row, saved_.col}; }
    void setSavedSpot(Spot s) { saved_ = {s.map, s.row, s.col}; }
    void takeOff(int mode);            // 01:63E6
    bool land();                       // B in the Talon: 01:4250 -> into the Talon (map $10)
    bool boardTalon();                 // A on its parking spot: 01:631A
    void setVehicle(int mode);         // wC6E3 + 00:08AD: leader sprite / animation for the mode
    std::function<void(int slot, int character)> joinParty;    // event $10 1x/2x (0B:6DE4)
    std::function<void(int slot)> leaveParty;                  // event $10 0x (0B:6E33)
    std::function<void(uint16_t addr, uint8_t v)> pokeRam;
    std::function<void()> talonConsole;                        // event $23 (01:7972)
    std::function<void()> healParty;                           // event $22 (01:7701)
    std::function<void()> hurtParty;                           // event $20 (1a34): HP -2, min 1
    std::function<void()> cureParty;                           // event $21 (1a6d -> 09:53D1)     // game RAM writes (party records, wC6xx)                       // event $1D / step sounds
    uint8_t music = 0, lastSfx = 0;                           // hCurrentMusic / hSFX
    bool noClip = false;
    bool encounters = true;                                   // random battles on (debug toggle)
    int encounterArea() const;                                // wCA2D (00:02e7 / 00:0409)
    uint8_t random8() { rng_ = rng_ * 1103515245u + 12345u; return uint8_t(rng_ >> 16); }

private:
    bool occupied(int row, int col, int selfSlot) const;
    uint8_t vehicleMask() const;
    void onStepDone();
    bool warpTo(int map, uint8_t b2, uint8_t b3);   // 00:1468 - map, row|facing, col|facing
    bool warpByExitTable();
    void startMove(int slot, Dir d, bool turn, int speed = 2);
    bool objectPassable(int row, int col) const;
    void wanderStep();
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
    // Event $12 cutscenes: objects gliding 1 px a frame (00:176B..). Each phase: frames, then
    // up to two (slot, direction) pairs.
    struct Glide { int frames; int slot[2]; Dir dir[2]; };
    std::vector<Glide> glides_;
    int flash_ = 0;
    // Events $12 $08/$09 (00:190D / 1950): the leader spins for 160 frames, facing from
    // 01:53FF (frame counter >> 3 & 3: up, right, down, left). $08 also flings them 2 px a frame
    // up the screen for 80 frames and back (wC210, the leader's screen Y; the camera stays put).
    struct Spin { int frame = 0; bool fling = false; Dir facing = Dir::Down; };
    std::optional<Spin> spin_;
    int trans_ = 0;                    // event $12 $03: 4 x (4 frames white, 4 normal)
    int spinClock_ = 0;                // free-running stand-in for hFF93 while spinning
public:
    // A map change seen on screen (00:39F3 fade out, the load, 00:3A0C fade in): 60 frames,
    // the game frozen. Level 0-3 of the fade table wCDB4 (D2/D0/90, 81/80/40, 40/40/00, 0).
    int transition() const { return trans_; }
    int fadeLevel() const {
        if (!trans_) return 0;
        const int t = kTransition - trans_;                // 0.. frames into it
        if (t < 5) return 1;
        if (t < 10) return 2;
        if (t < 50) return 3;
        return t < 55 ? 2 : 1;
    }
    static constexpr int kTransition = 60;
    // $27/$28 moves swap the LCD handlers for the walk (00:1B2D ... 1B50): no text window.
    bool scriptWalking() const { return !walks_.empty(); }
    int leaderLift() const {           // screen pixels the leader is drawn above its spot
        if (!spin_ || !spin_->fling) return 0;
        const int k = spin_->frame;
        return 2 * (k < 80 ? k : 160 - k);
    }
    bool flashWhite() const { return flash_ > 0 && ((flash_ - 1) / 4) % 2 == 1; }
private:
    std::vector<Walk> walks_;          // queued scripted moves ($27/$28)
    uint8_t layer_ = 0;
    int vehicle_ = 0;                  // wC6E3: 0 on foot in towns, 2 on foot on the world map
    uint8_t leaderFlags_ = 0x45;       // wC804 = leader slot flags wC2E0 ($45 on foot, from RAM)
    struct Return { int map, row, col; };
    std::vector<Return> returnStack_;  // wC7A0 / wC7C0
    Return saved_{-1, 0, 0};
    int blocksVersion_ = 0;
    bool hideLeaderOnLoad_ = false;   // wCA00
    int holesFilled_ = 0;              // wCA01: boulders dropped this visit (event $2B)
    bool tryPush(int row, int col, Dir d);
    void applyMapPatches();
    void boulderLanded(int slot);           // wC6E9-C6ED: saved position (event $10 4x/Dx/Ex)
    std::string lastEvent_;
    uint32_t rng_ = 0x5a6a3;
};

} // namespace ffl3
