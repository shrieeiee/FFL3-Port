#include "battle.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace ffl3 {

namespace {
// Battle message fragments, from the battle text at 09:79C6-7D57 (decoded with the dictionary).
const char* kFight[3] = {"Fight", "Run", "Auto"};
const char* kCmds[4] = {"Weapon", "Talent", "Magic", "Item"};
const char* kCmdShort[4] = {"Weap", "Tale", "Magi", "Item"};
// Field sprite page (04:xx00) of each character record. Read from VRAM in the real game;
// where the game derives it from is not traced yet, so it's a table for the starting party.
const int kSpritePage[10] = {0x70, 0x74, 0x72, 0x71, 0x73, 0x70, 0x70, 0x70, 0x70, 0x70};

Glyphs cat(Glyphs a, const Glyphs& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
Glyphs G(const char* s) { return asciiGlyphs(s); }
}

Battle::Battle(const Rom& rom, std::vector<CharRecord>& party, int hffca, uint32_t seed)
    : rom_(rom), party_(party), rng_(seed) {
    form_ = loadFormation(rom, hffca & 0x7f);
    layout_ = layoutEnemies(rom, form_, (hffca & 0x80) != 0, [this](int n) { return rnd(n); });
    for (int g = 0; g < 3; g++) groups_.push_back(form_.ids[g] == 0xff ? Monster{} : loadMonster(rom, form_.ids[g]));
    for (size_t i = 0; i < party_.size(); i++) {
        const CharRecord& c = party_[i];
        Fighter f;
        f.index = int(i);
        f.name = c.name();
        f.hp = c.hp(); f.maxHp = c.maxHp(); f.mp = c.mp(); f.maxMp = c.maxMp();
        f.str = c.str(); f.def = c.def(); f.mag = c.mag(); f.agi = c.agi();
        members_.push_back(f);
    }
    for (size_t s = 0; s < layout_.slots.size(); s++) {
        const Monster& m = groups_[size_t(layout_.slots[s].group)];
        Fighter f;
        f.enemy = true;
        f.index = int(s);
        f.group = layout_.slots[s].group;
        f.name = m.name;
        f.hp = f.maxHp = m.hp; f.mp = f.maxMp = m.mp;
        f.str = m.atk; f.def = m.def; f.mag = m.mag; f.agi = m.agi; f.eva = m.eva;
        enemies_.push_back(f);
    }
    timer_ = 40;
}

int Battle::memberSprite(int m) const {
    // records 0-3 are the party, the guest record follows; RAM keeps no record index, so use the
    // name order of the ROM records as a fallback.
    return kSpritePage[std::min(m, 9)];
}

int Battle::aliveInGroup(int g) const {
    int n = 0;
    for (const Fighter& e : enemies_) n += e.group == g && e.alive();
    return n;
}

std::vector<int> Battle::aliveGroups() const {
    std::vector<int> v;
    for (int g = 0; g < 3; g++) if (aliveInGroup(g)) v.push_back(g);
    return v;
}

int Battle::firstWeapon(int member) const {
    const CharRecord& c = party_[size_t(member)];
    for (int i = 0; i < 16; i++) if (c.item(i) >= 0x30 && c.item(i) != 0xff) return c.item(i);
    return 0xff;
}

void Battle::say(std::vector<Glyphs> lines, std::function<void()> effect, int pause) {
    steps_.push_back({std::move(lines), std::move(effect), pause});
}

// One action. PROVISIONAL rules (see battle.h): hit if rnd(100) >= 10 + target evade/2;
// damage = (power + 8) * (100..150)% - def/2, at least 1; 1 in 16 is a mighty blow (x2).
void Battle::act(Fighter& who, int targetGroup, int item, bool weapon) {
    if (!who.alive()) return;
    Fighter* target = nullptr;
    if (!who.enemy) {
        if (!aliveInGroup(targetGroup)) { auto g = aliveGroups(); if (g.empty()) return; targetGroup = g[0]; }
        for (Fighter& e : enemies_) if (e.group == targetGroup && e.alive()) { target = &e; break; }
    } else {
        std::vector<Fighter*> alive;
        for (int i = 0; i < 4 && i < int(members_.size()); i++) if (members_[size_t(i)].alive()) alive.push_back(&members_[size_t(i)]);
        if (alive.empty()) return;
        target = alive[size_t(rnd(int(alive.size())))];
    }
    if (!target) return;
    const Glyphs what = who.enemy ? talentName(rom_, item) : (item == 0xff ? G("Fists") : itemName(rom_, item));
    (void)weapon;
    std::vector<Glyphs> lines = {cat(who.name, G(" used")), cat(what, G(" against")), cat(target->name, G("."))};
    const bool hit = rnd(100) >= 10 + target->eva / 2;
    const bool crit = hit && rnd(16) == 0;
    int dmg = 0;
    if (hit) {
        dmg = (who.str + 8) * (100 + rnd(51)) / 100 - target->def / 2;
        if (crit) dmg *= 2;
        dmg = std::max(1, dmg);
        if (crit) lines.push_back(G("A mighty blow!"));
    }
    Fighter* t = target;
    const int flash = t->enemy ? t->index : -1;
    say(lines, [this, t, dmg, flash] {
        t->hp = std::max(0, t->hp - dmg);
        if (std::getenv("FFL3_DEBUG")) std::fprintf(stderr, "dmg %d -> hp %d\n", dmg, t->hp);
        if (dmg && flash >= 0) { flashSlot_ = flash; flashTimer_ = 16; }
        if (!t->alive()) say({cat(t->name, G(" fell."))}, nullptr, 8);
    }, 12);
}

void Battle::beginRound() {
    struct Turn { Fighter* f; int key; int cmd; };
    std::vector<Turn> order;
    for (size_t i = 0; i < members_.size(); i++) {
        Fighter& f = members_[i];
        if (!f.alive()) continue;
        if (i < 4 && !cmds_[i].set) continue;
        order.push_back({&f, f.agi + rnd(8), int(i)});
    }
    for (Fighter& e : enemies_) if (e.alive()) order.push_back({&e, e.agi + rnd(8), -1});
    std::stable_sort(order.begin(), order.end(), [](const Turn& a, const Turn& b) { return a.key > b.key; });
    for (const Turn& t : order) {
        Fighter* f = t.f;
        const int ci = t.cmd;
        // Queue the turn as a step whose effect performs the action when its turn comes, so
        // targets are chosen against the state at that moment.
        steps_.push_back({{}, [this, f, ci] {
            if (!f->alive() || aliveGroups().empty() || std::none_of(members_.begin(), members_.begin() + std::min<size_t>(4, members_.size()),
                                                                     [](const Fighter& m) { return m.alive(); })) return;
            if (f->enemy) {
                const Monster& m = groups_[size_t(f->group)];
                const int tal = m.talents.empty() ? 0 : m.talents[size_t(rnd(int(m.talents.size())))];
                act(*f, 0, tal, false);
            } else if (ci >= 0 && ci < 4) {
                act(*f, cmds_[size_t(ci)].targetGroup, cmds_[size_t(ci)].item, true);
            } else {                                     // guest: acts on its own
                const auto g = aliveGroups();
                act(*f, g[size_t(rnd(int(g.size())))], firstWeapon(f->index), true);
            }
        }, 0});
    }
    // End of round: check the outcome.
    steps_.push_back({{}, [this] {
        const bool partyAlive = std::any_of(members_.begin(), members_.begin() + std::min<size_t>(4, members_.size()),
                                            [](const Fighter& m) { return m.alive(); });
        if (aliveGroups().empty()) {
            for (const Fighter& e : enemies_) gpWon += groups_[size_t(e.group)].gp;
            char buf[32];
            std::snprintf(buf, sizeof buf, "Received %d", gpWon);
            say({G("Won!")}, nullptr, 20);
            say({cat(G(buf), G(" GP."))}, [this] { endBattle(Result::Won); }, 40);
        } else if (!partyAlive) {
            Glyphs l2 = G("was beat");
            l2.push_back(0xae); l2.push_back(0xae);
            say({cat(members_[0].name, G("'s party")), l2}, [this] { endBattle(Result::Lost); }, 60);
        } else {
            state_ = State::FightMenu;
            cursor_ = 0;
        }
    }, 0});
    for (auto& c : cmds_) c.set = false;
    state_ = State::Running;
    shown_.clear();
}

void Battle::endBattle(Result r) {
    result_ = r;
    state_ = State::Done;
    for (size_t i = 0; i < members_.size() && i < party_.size(); i++) {
        party_[i].setHp(members_[i].hp);
        party_[i].setMp(members_[i].mp);
    }
}

void Battle::tick(const BattleInput& in) {
    if (flashTimer_ > 0 && --flashTimer_ == 0) flashSlot_ = -1;
    switch (state_) {
    case State::Intro:
        if (--timer_ <= 0) { state_ = State::FightMenu; cursor_ = 0; }
        return;
    case State::FightMenu:
        if (in.up) cursor_ = (cursor_ + 2) % 3;
        if (in.down) cursor_ = (cursor_ + 1) % 3;
        if (!in.a) return;
        if (cursor_ == 0) {                                   // Fight
            member_ = 0;
            while (member_ < 4 && !members_[size_t(member_)].alive()) member_++;
            state_ = State::Command;
            cursor_ = 0;
        } else if (cursor_ == 1) {                            // Run (provisional: 1 in 2)
            state_ = State::Running;
            shown_.clear();
            if (form_.canRun() && rnd(2) == 0) {
                say({G("Ran away.")}, [this] { endBattle(Result::Ran); }, 30);
            } else {
                say({G("Can't run!")}, nullptr, 20);
                for (auto& c : cmds_) c.set = false;
                steps_.push_back({{}, [this] { beginRound(); shown_.clear(); }, 0});
                // beginRound() would clear the message; keep "Can't run!" until it's read
            }
        } else {                                              // Auto: everyone uses their weapon
            const auto g = aliveGroups();
            for (int i = 0; i < 4; i++) cmds_[size_t(i)] = {0, firstWeapon(i), g.empty() ? 0 : g[0], true};
            beginRound();
        }
        return;
    case State::Command:
        if (in.up) cursor_ = (cursor_ + 3) % 4;
        if (in.down) cursor_ = (cursor_ + 1) % 4;
        if (in.b) {
            int m = member_ - 1;
            while (m >= 0 && !members_[size_t(m)].alive()) m--;
            if (m < 0) { state_ = State::FightMenu; cursor_ = 0; }
            else { member_ = m; cmds_[size_t(m)].set = false; cursor_ = 0; }
            return;
        }
        if (in.a && cursor_ == 0) {                           // Weapon (the others come later)
            state_ = State::Target;
            const auto g = aliveGroups();
            targetCursor_ = g.empty() ? 0 : g[0];
        }
        return;
    case State::Target: {
        const auto g = aliveGroups();
        auto it = std::find(g.begin(), g.end(), targetCursor_);
        size_t k = it == g.end() ? 0 : size_t(it - g.begin());
        if (in.left || in.up) k = (k + g.size() - 1) % g.size();
        if (in.right || in.down) k = (k + 1) % g.size();
        targetCursor_ = g[k];
        if (in.b) { state_ = State::Command; return; }
        if (in.a) {
            cmds_[size_t(member_)] = {0, firstWeapon(member_), targetCursor_, true};
            int m = member_ + 1;
            while (m < 4 && !members_[size_t(m)].alive()) m++;
            if (m >= 4) beginRound();
            else { member_ = m; state_ = State::Command; cursor_ = 0; }
        }
        return;
    }
    case State::Running: {
        if (wait_ > 0) { wait_ -= in.aHeld ? 3 : 1; return; }
        if (!typing_.empty() || typed_) {
            // Typewriter: a character every 3 frames (every frame with A held), like the original.
            if (--typeDelay_ > 0) return;
            typeDelay_ = in.aHeld ? 1 : 3;
            if (typed_ < typing_.size()) { typed_++; return; }
            shown_.push_back(typing_);
            log.push_back(std::string(typing_.begin(), typing_.end()));
            typing_.clear();
            typed_ = 0;
        }
        if (steps_.empty()) return;
        Step& s = steps_.front();
        if (!s.lines.empty()) {
            typing_ = s.lines.front();
            s.lines.erase(s.lines.begin());
            typed_ = 0;
            typeDelay_ = 1;
            return;
        }
        Step done = std::move(steps_.front());
        steps_.pop_front();
        wait_ = done.pause;
        if (done.effect) {
            // An effect may queue more steps; they must run before what was already queued.
            std::deque<Step> rest;
            rest.swap(steps_);
            done.effect();
            for (auto& r : rest) steps_.push_back(std::move(r));
        }
        return;
    }
    case State::Done:
        return;
    }
}

// ---- view ------------------------------------------------------------------------------

void Battle::putText(std::array<uint8_t, 200>& w, int col, int row, const Glyphs& g) const {
    for (size_t i = 0; i < g.size() && col + int(i) < 20; i++) w[size_t(row * 20 + col + int(i))] = g[i];
}

void Battle::putNum(std::array<uint8_t, 200>& w, int col, int row, int v, int width) const {
    for (int i = width - 1; i >= 0; i--) {
        w[size_t(row * 20 + col + i)] = (v || i == width - 1) ? uint8_t(0xb0 + v % 10) : 0xff;
        v /= 10;
    }
}

// Window frame: $F7 $FC $F9 / $FB / $F8 $FC $FA (same tiles as the dialogue window).
void Battle::box(std::array<uint8_t, 200>& w, int c0, int r0, int cw, int rh) const {
    for (int r = r0; r < r0 + rh; r++)
        for (int c = c0; c < c0 + cw; c++) {
            uint8_t t = 0xff;
            const bool top = r == r0, bot = r == r0 + rh - 1, lft = c == c0, rgt = c == c0 + cw - 1;
            if (top) t = lft ? 0xf7 : rgt ? 0xf9 : 0xfc;
            else if (bot) t = lft ? 0xf8 : rgt ? 0xfa : 0xfc;
            else if (lft || rgt) t = 0xfb;
            w[size_t(r * 20 + c)] = t;
        }
}

// Status panel (from VRAM): members 0-3 in a 2x2 grid in a box on rows 0-6, the guest below.
// Per member: HP/max on the first row, MP on the second; the chosen command replaces "/max".
void Battle::statusPanel(BattleView& v, int hideCol) const {
    box(v.win, 0, 6, 20, 4);
    box(v.win, 0, 0, 20, 7);                         // its bottom edge is the guest box's top
    for (size_t i = 0; i < members_.size() && i < 5; i++) {
        const Fighter& m = members_[i];
        const int x0 = i == 4 ? 0 : (i & 1) ? 9 : 0, y0 = i == 4 ? 7 : 1 + int(i >> 1) * 3;
        putNum(v.win, x0 + 3, y0, m.hp, 3);
        putNum(v.win, x0 + 3, y0 + 1, m.mp, 3);
        if (i < 4 && cmds_[i].set) {
            putText(v.win, x0 + 6, y0 + 1, G(kCmdShort[cmds_[i].kind]));
        } else {
            v.win[size_t(y0 * 20 + x0 + 6)] = 0xf1;
            putNum(v.win, x0 + 7, y0, m.maxHp, 3);
        }
        // portrait: the member's field sprite, front frame, at window cols 1-2
        const int col = i == 4 ? 0 : int(i & 1);
        if (col == hideCol) continue;                // under the command box (OAM x off-screen)
        const int px = i == 4 ? 8 : (i & 1) ? 80 : 8, py = 64 + 8 + (i == 4 ? 48 : int(i >> 1) * 24);
        v.objs.push_back({px, py, 0, false, false, int(i), true});
    }
}

BattleView Battle::view() const {
    BattleView v;
    v.win.fill(0xff);
    for (size_t s = 0; s < enemies_.size(); s++) if (enemies_[s].alive()) v.visibleSlots.push_back(int(s));
    if (flashSlot_ >= 0 && (flashTimer_ & 4)) {
        v.visibleSlots.erase(std::remove(v.visibleSlots.begin(), v.visibleSlots.end(), flashSlot_), v.visibleSlots.end());
    }
    auto brackets = [&v](int x0, int y0, int x1, int y1) {
        v.objs.push_back({x0, y0, kBracket, false, false});
        v.objs.push_back({x1 - 8, y0, kBracket, true, false});
        v.objs.push_back({x0, y1 - 8, kBracket, false, true});
        v.objs.push_back({x1 - 8, y1 - 8, kBracket, true, true});
    };
    switch (state_) {
    case State::Intro:
    case State::Done:
        break;
    case State::FightMenu: {                       // g00_07: two 10x10 boxes, WY = 64
        v.wy = 64;
        box(v.win, 0, 0, 10, 10);
        box(v.win, 10, 0, 10, 10);
        int row = 2;
        for (int g : aliveGroups()) { putText(v.win, 1, row, groups_[size_t(g)].name); row += 2; }
        for (int i = 0; i < 3; i++) putText(v.win, 12, 2 + i * 2, G(kFight[i]));
        v.objs.push_back({88, 64 + 16 + cursor_ * 16, kCursor});
        break;
    }
    case State::Command: {                         // g00_08 / g00_10: status + command box opposite
        v.wy = 64;
        const bool left = (member_ & 1) == 0;
        statusPanel(v, left ? 1 : 0);
        const int bx = left ? 10 : 0;
        box(v.win, bx, 0, 10, 10);
        for (int i = 0; i < 4; i++) putText(v.win, bx + 2, 2 + i * 2, G(kCmds[i]));
        v.objs.push_back({bx * 8 + 8, 64 + 16 + cursor_ * 16, kCursor});
        const int px = (member_ & 1) ? 80 : 8, py = 64 + 8 + (member_ >> 1) * 24;
        brackets(px, py - 8, px + 16, py + 24);
        break;
    }
    case State::Target: {                          // g00_09: full status panel, brackets on the group
        v.wy = 64;
        statusPanel(v, -1);
        int x0 = 160, x1 = 0, y0 = 64, y1 = 0;
        for (size_t s = 0; s < layout_.slots.size(); s++) {
            const EnemySlot& e = layout_.slots[s];
            if (e.group != targetCursor_ || !enemies_[s].alive()) continue;
            const Monster& m = groups_[size_t(e.group)];
            x0 = std::min(x0, e.col * 8); x1 = std::max(x1, (e.col + m.picW) * 8);
            y0 = std::min(y0, e.row * 8); y1 = std::max(y1, (e.row + m.picH) * 8);
        }
        if (x1 > x0) brackets(x0, y0, x1, y1);
        break;
    }
    case State::Running: {                         // g00_19: WY = 88, two text lines, party below
        v.wy = 88;
        box(v.win, 0, 0, 20, 7);
        std::vector<Glyphs> lines = shown_;
        if (!typing_.empty()) lines.push_back(Glyphs(typing_.begin(), typing_.begin() + long(typed_)));
        const size_t first = lines.size() > 2 ? lines.size() - 2 : 0;
        for (size_t i = first; i < lines.size(); i++) putText(v.win, 1, 2 + int(i - first) * 2, lines[i]);
        for (size_t i = 0; i < members_.size() && i < 5; i++)
            if (members_[i].alive()) v.objs.push_back({8 + int(i) * 32, 72, 0, false, false, int(i), true});
        break;
    }
    }
    v.flashSlot = flashSlot_;
    return v;
}

} // namespace ffl3
