// The simulation's core: units, animation (iscript), the images it draws, the frame loop, and the Simulator that the
// editor and the command line use. (Movement and paths: movement.cpp.)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <queue>
#include <tuple>
#include <string>
#include <utility>
#include <vector>

#include "world.h"

namespace sim {

namespace {

struct DirTable {
    int x[256], y[256];
    DirTable() {
        for (int d = 0; d < 256; ++d) {
            double a = d * 3.14159265358979323846 / 128.0;
            x[d] = int(std::lround(std::sin(a) * 256));
            y[d] = int(std::lround(-std::cos(a) * 256));
        }
    }
};
const DirTable& dirs() {
    static const DirTable table;
    return table;
}

}  // namespace

int turnToward(int from, int to, int rate) {
    int diff = int(int8_t(uint8_t(to - from)));
    if (std::abs(diff) <= rate) return to & 255;
    return (from + (diff > 0 ? rate : -rate)) & 255;
}

int dirX(int dir) { return dirs().x[dir & 255]; }
int dirY(int dir) { return dirs().y[dir & 255]; }

int direction(int dx, int dy) {
    if (dx == 0 && dy == 0) return 0;
    double a = std::atan2(double(dx), double(-dy));
    return int(std::lround(a * 128.0 / 3.14159265358979323846)) & 255;
}

int World::random() {
    seed = seed * 22695477u + 1u;
    return int((seed >> 16) & 0x7fff);
}

// A unit's box: its units.dat box (a building's too, not its placement footprint).
Box World::boxAt(int type, int x, int y) const {
    const UnitData& d = t.units[size_t(type)];
    return boxAround(x, y, d.left, d.up, d.right, d.down);
}
Box World::boxOf(const Unit& u) const { return boxAt(u.type, u.x, u.y); }

int World::gap(const Unit& a, const Unit& b) const { return boxGap(boxOf(a), boxOf(b)); }

// Whether a unit of a type at (x, y) and another unit overlap (touching counts; units walk in the gaps that a
// building's box leaves).
bool World::overlaps(int type, int x, int y, const Unit& o) const { return boxesTouch(boxAt(type, x, y), boxOf(o)); }

int World::pointGap(const Unit& v, int x, int y) const { return sim::pointGap(boxOf(v), x, y); }

void World::regrid(int unit) {
    const Unit& u = units[size_t(unit)];
    if (u.building && !combat) {  // (mining's building boxes and walks, and its places that cannot be reached: again)
        buildingMaskReady = false;
        walks.clear();
        cutCache.clear();
    }
    if (u.building && combat) buildingMaskReady = false;
    if (u.alive) unitGrid.place(unit, boxOf(u));
    else unitGrid.remove(unit);
}

int World::add(int type, int owner, int x, int y) {
    Unit u;
    const UnitData& d = t.units[type];
    u.type = type;
    u.id = int(units.size());
    u.owner = owner;
    u.x = u.moveX = u.aimX = x;
    u.y = u.moveY = u.aimY = y;
    u.ex = x << 8;
    u.ey = y << 8;
    u.hp = d.hp;
    u.shields = d.hasShields ? d.shields << 8 : 0;
    u.building = (d.flags & (kBuilding | kResource)) != 0;
    u.anim.header = t.scriptHeader(t.flingyImage(d.flingy));
    startAnim(u.anim, kAnimInit);
    if (d.subunit != kNoUnit) {
        u.turret.header = t.scriptHeader(t.flingyImage(t.units[size_t(d.subunit)].flingy));
        startAnim(u.turret, kAnimInit);
    }
    if (role(type).launchesScarabs) u.scarabs = role(type).capacity;  // (a Reaver: its Scarabs)
    if (d.flags & kSpellcaster) u.energy = t.rules.startEnergy << 8;  // (spellcasters such as the Medic)
    units.push_back(u);
    regrid(int(units.size()) - 1);
    return int(units.size()) - 1;
}

void World::setOrder(Unit& u, Order o) {
    u.order = o;
    u.state = 0;
    u.poll = 0;
}

void World::animate(int unit, int anim) {
    startAnim(units[size_t(unit)].anim, anim);
    for (Effect& e : effects)
        if (e.follow == unit && t.animOffset(e.anim.header, anim)) startAnim(e.anim, anim);
}

void World::startAnim(Anim& a, int anim) {
    int pc = t.animOffset(a.header, anim);
    if (!pc) return;
    if (a.noBreak && !a.paused && anim != kAnimDeath) {  // (a part that must not break: the animation waits)
        a.pending = anim;
        return;
    }
    a.pc = pc;
    a.wait = 0;
    a.paused = false;
    a.noBreak = false;
    a.pending = -1;
}

// The GRP frame of an image's frame set at a heading: a turning image's set holds 17 directions (the others mirrored);
// a set that the GRP has no room for (a death animation's frames after the last full set) is one frame.
int World::grpFrame(int image, int frame, int heading, bool* flip) const {
    if (flip) *flip = false;
    if (image < 0 || size_t(image) >= t.imageTurns.size()) return frame;
    int frames = size_t(image) < t.imageFrames.size() ? t.imageFrames[size_t(image)] : 0;
    if (!t.imageTurns[size_t(image)] || (frames > 0 && frame + 17 > frames)) return frame;
    int d = ((heading + 4) >> 3) & 31;
    if (flip) *flip = d > 16;
    return frame + (d > 16 ? 32 - d : d);
}

// imgoluselo and imguluselo: the place of an overlay from the parent image's attack overlay list at its frame (x
// mirrored with the frame); not there: (0, 0).
std::pair<int, int> World::loOffset(int parent, int frame, int heading, int overlay) const {
    if (parent < 0 || size_t(parent) >= t.attackOffsets.size()) return {0, 0};
    const auto& lo = t.attackOffsets[size_t(parent)];
    bool flip = false;
    int f = grpFrame(parent, frame, heading, &flip);
    if (f < 0 || f >= int(lo.size()) || overlay >= int(lo[size_t(f)].size())) return {0, 0};
    auto [x, y] = lo[size_t(f)][size_t(overlay)];
    return {flip ? -x : x, y};
}

// trgtrangecondjmp: whether the unit's target is within the range (pixels).
bool World::targetWithin(int unit, int range) const {
    if (unit < 0 || units[size_t(unit)].target < 0) return false;
    const Unit& u = units[size_t(unit)];
    const Unit& v = units[size_t(u.target)];
    return v.alive && centreDistance(u.x, u.y, v.x, v.y) <= range;
}

// nobrkcodestart and nobrkcodeend: the start and the end of a part that a new animation does not break; at the end,
// the animation that waited starts.
bool World::noBreak(Anim& a, int op) {
    if (op == 0x2E) {
        a.noBreak = true;
        return false;
    }
    a.noBreak = false;
    int anim = a.pending;
    a.pending = -1;
    if (anim < 0) return false;
    startAnim(a, anim);
    return true;
}

// One frame of an image's iscript: the instructions up to the next wait. Instructions that only draw (overlays,
// sounds) are passed over, but those that draw random numbers draw them.
void World::runAnim(Anim& a, int unit, int bullet) {
    if (a.paused || a.header < 0) return;
    if (a.wait > 0) {
        --a.wait;
        return;
    }
    const std::vector<uint8_t>& s = t.iscript;
    auto u16 = [&](int o) { return o + 1 < int(s.size()) ? s[o] | s[o + 1] << 8 : 0; };
    for (int guard = 0; guard < 256; ++guard) {
        int pc = a.pc, len = t.opLength(pc);
        if (len < 0) {
            a.paused = true;
            return;
        }
        a.pc = pc + len;
        int arg = len > 1 ? s[pc + 1] : 0;
        Unit* u = unit >= 0 ? &units[unit] : nullptr;
        switch (s[pc]) {
        case 0x00: a.frame = u16(pc + 1); break;  // playfram
        case 0x05: a.wait = arg - 1; return;       // wait
        case 0x06: {                               // waitrand
            int lo = s[pc + 1], hi = s[pc + 2];
            a.wait = lo + ((random() * (hi - lo + 1)) >> 15) - 1;
            return;
        }
        case 0x07: a.pc = u16(pc + 1); break;  // goto
        case 0x16:                              // end
            if (bullet >= 0) bullets[size_t(bullet)].alive = false;
            a.paused = true;
            return;
        case 0x18: case 0x19: case 0x1A: {  // playsnd, playsndrand, playsndbtwn (the game's draws as before)
            int op = s[size_t(pc)], r = op == 0x18 ? 0 : random();
            int sfx = op == 0x18 ? u16(pc + 1)
                      : op == 0x19 ? u16(pc + 2 + 2 * (arg ? r % arg : 0))
                                   : u16(pc + 1) + r % std::max(1, u16(pc + 3) - u16(pc + 1) + 1);
            if (u) sound(sfx, u->x, u->y, u->owner);
            else if (bullet >= 0) sound(sfx, bullets[size_t(bullet)].x, bullets[size_t(bullet)].y,
                                        bullets[size_t(bullet)].owner);
            break;
        }
        case 0x22: random(); break;  // turnrand
        case 0x1B: case 0x44:                              // domissiledmg, dogrddamage
            if (bullet >= 0) bulletHit(bullets[size_t(bullet)]);
            break;
        case 0x1C:  // attackmelee
            if (u) fire(unit, t.units[u->type].groundWeapon, true);
            break;
        case 0x1E:  // randcondjmp
            if ((random() & 0xff) <= arg) a.pc = u16(pc + 2);
            break;
        case 0x1F: if (u) u->heading = (u->heading - arg * 8) & 255; break;  // turnccwise
        case 0x20: if (u) u->heading = (u->heading + arg * 8) & 255; break;  // turncwise
        case 0x21: if (u) u->heading = (u->heading + 8) & 255; break;        // turn1cwise
        case 0x25: case 0x31:                                                // attackwith, attkshiftproj
            if (u) {
                const UnitData& d = weaponUnit(u->type);
                // (attkshiftproj n: the ground weapon from n pixels ahead)
                fire(unit, s[pc] == 0x25 && arg == 2 ? d.airWeapon : d.groundWeapon, false, s[pc] == 0x31 ? arg : -1);
            }
            break;
        case 0x26:  // attack
            if (u && u->target >= 0) fire(unit, weaponFor(*u, units[size_t(u->target)]), false);
            break;
        case 0x28:  // useweapon (a gathering worker's: the picture of its work at its resource)
            if (u && u->order == MiningMinerals) gatherPicture(unit, arg);
            else if (u) fire(unit, arg, false);
            break;
        case 0x08: case 0x09: case 0x0D: case 0x0E: case 0x3D:  // imgol, imgul, imgoluselo, imguluselo, imgulnextid
        case 0x0F: case 0x10: case 0x11: case 0x14: case 0x42:  // sprol, highsprol, lowsprul, sprul, grdsprol
            if (bullet >= 0 && s[size_t(pc)] >= 0x0F && s[size_t(pc)] != 0x3D) {  // (a projectile's sprite: the spines)
                int id = u16(pc + 1);
                const Bullet& b = bullets[size_t(bullet)];
                bool shown = id < int(t.spriteImage.size()) && t.spriteVisible[size_t(id)];
                bool under = s[size_t(pc)] == 0x11 || s[size_t(pc)] == 0x14 || s[size_t(pc)] == 0x42;
                if (shown)
                    addEffect(t.spriteImage[size_t(id)], b.x + int8_t(s[size_t(pc) + 3]),
                              b.y + int8_t(s[size_t(pc) + 4]), b.owner, b.heading,
                              spriteElevation(s[size_t(pc)], bulletElevation(b)), under, 0, -1, 0, 0);
            }
            if (u && &a == &u->anim) {  // (the main image's: overlays stay with the unit, sprites go their own way)
                int op = s[size_t(pc)];
                int id = op == 0x3D ? t.flingyImage(t.units[size_t(u->type)].flingy) + 1 : u16(pc + 1);
                bool lo = op == 0x0D || op == 0x0E;  // (offsets from the image's attack overlay list)
                auto at = lo ? loOffset(t.flingyImage(t.units[size_t(u->type)].flingy), a.frame, u->heading,
                                        s[size_t(pc) + 3])
                             : std::pair<int, int>{0, 0};
                int dx = op == 0x3D ? int8_t(s[size_t(pc) + 1]) : lo ? at.first : int8_t(s[size_t(pc) + 3]);
                int dy = op == 0x3D ? int8_t(s[size_t(pc) + 2]) : lo ? at.second : int8_t(s[size_t(pc) + 4]);
                bool sprite = op >= 0x0F && op != 0x3D;
                // (a sprite the game never draws: none)
                bool shown = !sprite || (id < int(t.spriteImage.size()) && t.spriteVisible[size_t(id)]);
                int image = !shown ? -1 : sprite ? t.spriteImage[size_t(id)] : id;
                bool under = op == 0x09 || op == 0x0E || op == 0x3D || op == 0x11 || op == 0x14 || op == 0x42;
                int level = t.units[size_t(u->type)].elevation;
                size_t before = effects.size();
                int above = 0, below = 0;  // (the unit's overlays so far)
                for (const Effect& e : effects)
                    if (e.follow == unit && e.depth == 0) (e.layer < 0 ? below : above)++;
                addEffect(image, u->x + dx, u->y + dy, u->owner, u->heading,
                          sprite ? spriteElevation(op, level) : level, under, 0, sprite ? -1 : unit, dx, dy);
                if (!sprite && effects.size() > before)  // (imgol: above the images so far; imgul: under them)
                    effects.back().layer = under ? -(below + 1) : above + 1;
            }
            break;
        case 0x24:                                         // sigorder: a projectile's launch
            if (bullet >= 0 && (arg & 1)) bullets[size_t(bullet)].launched = true;
            if (u && (arg & 4) && u->burrowing) {  // (the end of burrowing)
                u->burrowing = false;
                u->burrowed = true;
            }
            break;
        case 0x29:  // move (control 2 units)
            if (u) u->speed = speedUpgraded(*u) ? upgradedSpeed(arg << 8, false) : arg << 8;  // (no minimum)
            break;
        case 0x2A: if (u) u->repeatAttack = false; break;  // gotorepeatattk
        case 0x30:  // ignorerest (an attack script that has no gotorepeatattk, the Firebat's, ends its attack here)
            if (u) u->repeatAttack = false;
            a.paused = true;
            return;
        case 0x35: a.ret = a.pc; a.pc = u16(pc + 1); break;  // call
        case 0x36: a.pc = a.ret; break;                      // return
        case 0x2E: case 0x2F: noBreak(a, s[size_t(pc)]); break;  // nobrkcodestart, nobrkcodeend
        case 0x32: a.hidden = true; break;                       // tmprmgraphicstart
        case 0x33: a.hidden = false; break;                      // tmprmgraphicend
        case 0x3A: if (targetWithin(unit, u16(pc + 1))) a.pc = u16(pc + 3); break;  // trgtrangecondjmp
        default: break;
        }
    }
    a.paused = true;  // (a loop with no wait)
}


void World::step() {
    ++frame;
    if (frame == std::max(measureFrom, 1))  // (mining: the end of the warm-up)
        for (Player& p : players) {
            p.measuredFrom = frame;
            p.mineralsAtMeasure = p.gathered;
            p.gasAtMeasure = p.gatheredGas;
        }
    // Movement as if at the same time: a unit held by one that has not moved yet this frame tries again after all.
    std::vector<Unit*> later;
    for (Unit& u : units)
        if (move(u, false)) later.push_back(&u);
    for (Unit* u : later) move(*u, true);
    for (size_t i = 0; i < units.size(); ++i) {
        Unit& u = units[i];
        if (!u.alive) continue;
        if (u.dieAt >= 0 && frame >= u.dieAt) {
            kill(int(i), false);
            continue;
        }
        if (u.timer > 0) --u.timer;
        if (u.cooldown > 0) --u.cooldown;
        const UnitData& d = t.units[u.type];
        if ((d.flags & kRegenerates) && u.hp < d.hp) u.hp = std::min(d.hp, u.hp + t.rules.regenHp);
        if (d.hasShields && u.shields < d.shields << 8)
            u.shields = std::min(d.shields << 8, u.shields + t.rules.regenShields);
        const UnitRole& r = role(u.type);
        bool more = r.energyUpgrade >= 0 && level(u.owner, r.energyUpgrade);
        const int maxEnergy = (more ? t.rules.maxEnergyUpgraded : t.rules.maxEnergy) << 8;
        if ((d.flags & kSpellcaster) && u.energy < maxEnergy)
            u.energy = std::min(maxEnergy, u.energy + t.rules.regenEnergy);
        if (u.order >= MoveToMinerals && u.order <= ReturnGas)
            miningOrder(int(i));
        else if (combat)
            combatOrder(int(i));
        Unit& v = units[i];
        if (v.alive && !combat && !fastForward && role(v.type).mineralField)  // (a mineral field: its picture)
            runAnim(v.anim, int(i), -1);
        if (v.alive && combat && v.building && !v.neutral) runAnim(v.anim, int(i), -1);  // (a defence: it fires)
        if (v.alive && v.building) damageOverlays(int(i));
        if (!v.alive || v.building) continue;
        bool moving = v.x != v.moveX || v.y != v.moveY;
        // A script-moved unit that starts from rest: its walk script starts a frame later, and its first movement
        // frame is lost (as the reference's movement traces show).
        bool scripted = t.flingy[size_t(t.units[v.type].flingy)].control == kControlScript;
        if (moving && !v.repeatAttack && v.walkingAnim != 1 && scripted && v.startWait == 0) {
            v.startWait = 2;
        } else if (moving && !v.repeatAttack && v.walkingAnim != 1) {
            if (v.startWait == 2) v.startWait = 1;
            animate(int(i), kAnimWalking);
            v.walkingAnim = 1;
        } else if (!moving && v.startWait) {
            v.startWait = 0;
        }
        if (!moving && v.walkingAnim == 1) {
            animate(int(i), kAnimWalkingToIdle);
            v.walkingAnim = 0;
        }
        runAnim(v.anim, int(i), -1);
        runAnim(units[i].turret, int(i), -1);
    }
    for (size_t b = 0; b < bullets.size(); ++b)
        if (bullets[b].alive) bulletStep(int(b));
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return !b.alive; }),
                  bullets.end());
    if (combat && allTeams) trackProgress();
    creepStep();  // (creep.cpp)
    for (size_t k = 0; k < effects.size(); ++k) effectStep(k);
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return e.done; }),
                  effects.end());
}

// The weapon that a gathering worker's script uses (the SCV's and the Probe's harvest weapons): its projectile's
// picture at the centre of the resource the worker mines, with its script's sprites and sounds; no hit, no game random
// number.
void World::gatherPicture(int unit, int weapon) {
    const Unit& u = units[size_t(unit)];
    if (weapon < 0 || weapon >= kNoWeapon || u.field < 0 || !t.flingyVisible(t.weapons[size_t(weapon)].flingy)) return;
    const WeaponData& wd = t.weapons[size_t(weapon)];
    const int x = units[size_t(u.field)].x, y = units[size_t(u.field)].y;
    addEffect(t.flingyImage(wd.flingy), x, y, u.owner, u.heading,
              t.units[size_t(u.type)].elevation + t.rules.bulletElevationAbove, false, kAnimInit);
}

int World::effectRandom() {
    effectSeed = effectSeed * 22695477u + 1u;
    return int((effectSeed >> 16) & 0x7fff);
}

// A building's damage overlays (fire, blood) by its hit points (damage-overlays rules): with n slots in its list (frame
// 0), S = 2n steps at the hit-point fractions 1 - 2k / (3 (S + 1)), k = 1 .. S. A step down turns the first-set overlay
// into the second set on its slot, or with none adds a first-set one on a random slot (1 to n, then the first slot from
// there that holds no second-set one); a step up turns the newest second-set overlay back, or with none removes the
// newest first-set one. Shields do not count. Pictures only: not in fast forward (it catches up after).
void World::damageOverlays(int unit) {
    Unit& u = units[size_t(unit)];
    const UnitData& d = t.units[size_t(u.type)];
    int image = t.flingyImage(d.flingy);
    if (fastForward || image < 0 || size_t(image) >= t.damageSlots.size()) return;
    const std::vector<int>& used = t.damageSlots[size_t(image)];
    if (used.empty() || d.hp <= 0) return;
    const long long steps = 2 * static_cast<long long>(used.size()), denom = 3 * (steps + 1);
    int target = 0;
    for (long long k = 1; k <= steps; ++k)
        if (static_cast<long long>(u.hp) * denom < static_cast<long long>(d.hp) * (denom - 2 * k)) target = int(k);
    auto newest = [&](bool second) {
        int best = -1;
        for (size_t e = 0; e < effects.size(); ++e)
            if (effects[e].follow == unit && effects[e].damageSlot >= 0 && effects[e].damageSecond == second &&
                (best < 0 || effects[e].damageOrder > effects[size_t(best)].damageOrder))
                best = int(e);
        return best;
    };
    auto take = [&](int e) {
        int slot = effects[size_t(e)].damageSlot;
        effects.erase(effects.begin() + e);
        return slot;
    };
    while (u.damageStep < target) {  // (down)
        int f = newest(false);
        if (f >= 0) {
            placeDamageOverlay(unit, take(f), true);
        } else {
            int n = int(used.size()), r = effectRandom() % n;
            for (int j = 0; j < n; ++j) {
                int slot = used[size_t((r + j) % n)];
                bool second = false;
                for (const Effect& e : effects) second = second || (e.follow == unit && e.damageSlot == slot);
                if (!second) {
                    placeDamageOverlay(unit, slot, false);
                    break;
                }
            }
        }
        ++u.damageStep;
    }
    while (u.damageStep > target) {  // (up: repair, regeneration)
        int s = newest(true);
        if (s >= 0) placeDamageOverlay(unit, take(s), false);
        else if (int f = newest(false); f >= 0) take(f);
        --u.damageStep;
    }
}

// A damage overlay on its slot: at the slot's place in the building's frame now (x mirrored with it), over the
// building's other images, newer ones on top; it runs its own script.
void World::placeDamageOverlay(int unit, int slot, bool second) {
    const Unit& u = units[size_t(unit)];
    int image = t.flingyImage(t.units[size_t(u.type)].flingy);
    bool flip = false;
    int frame = grpFrame(image, u.anim.frame, u.heading, &flip);
    const auto& lo = t.damageOffsets[size_t(image)];
    const auto& places = lo[size_t(std::clamp(frame, 0, int(lo.size()) - 1))];
    if (size_t(slot) >= places.size()) return;
    int dx = flip ? -places[size_t(slot)].first : places[size_t(slot)].first, dy = places[size_t(slot)].second;
    size_t before = effects.size();
    addEffect((second ? t.rules.damageSecondImage : t.rules.damageFirstImage) + slot, u.x + dx, u.y + dy, u.owner,
              u.heading, t.units[size_t(u.type)].elevation, false, kAnimInit, unit, dx, dy);
    if (effects.size() == before) return;
    Effect& e = effects.back();
    e.damageSlot = slot;
    e.damageSecond = second;
    e.damageOrder = ++damageCount;
    e.layer = 1000 + damageCount;  // (over all of the building's overlays)
}

// A projectile's elevation: its unit's, and rules.ini's levels above it.
int World::bulletElevation(const Bullet& b) const {
    int source = b.source >= 0 ? t.units[size_t(units[size_t(b.source)].type)].elevation : 0;
    return source + t.rules.bulletElevationAbove;
}

// The elevation of a sprite that a script makes, by its opcode: sprol and grdsprol one level above its parent,
// sprul one below, highsprol the highest level, lowsprul the lowest (rules.ini).
int World::spriteElevation(int op, int parent) const {
    switch (op) {
    case 0x10: return t.rules.elevationHighest;
    case 0x11: return t.rules.elevationLowest;
    case 0x14: return std::max(t.rules.elevationLowest, parent - 1);
    default: return std::min(t.rules.elevationHighest, parent + 1);
    }
}

void World::addEffect(int image, int x, int y, int owner, int heading, int elevation, bool under, int anim, int follow,
                      int dx, int dy) {
    if (fastForward) return;  // (pictures only)
    // (no shadows, and no image at all)
    if (image < 0 || (image < int(t.imageDraw.size()) && t.imageDraw[size_t(image)] == kDrawShadow)) return;
    Effect e;
    e.follow = follow;
    e.dx = dx;
    e.dy = dy;
    e.image = image;
    e.x = x;
    e.y = y;
    e.owner = owner;
    e.heading = heading;
    e.elevation = elevation;
    e.under = under;
    e.anim.header = t.scriptHeader(image);
    if (image < 0 || !t.animOffset(e.anim.header, anim)) return;
    startAnim(e.anim, anim);
    effects.push_back(e);
}

// One frame of an effect's script (the public iscript opcodes): frames, waits, jumps, and the images and sprites it
// makes (imgol, imgul, sprol, highsprol, lowsprul, sprul, grdsprol); the rest has no effect on a picture here.
void World::effectStep(size_t k) {
    if (effects[k].done) return;
    if (effects[k].follow >= 0) {  // (an overlay: with its unit)
        const Unit& u = units[size_t(effects[k].follow)];
        effects[k].x = u.x + effects[k].dx;
        effects[k].y = u.y + effects[k].dy;
        effects[k].heading = u.heading;
    }
    Anim& a0 = effects[k].anim;
    if (a0.wait > 0) {
        --a0.wait;
        return;
    }
    const std::vector<uint8_t>& s = t.iscript;
    auto u16 = [&](int o) { return o + 1 < int(s.size()) ? s[size_t(o)] | s[size_t(o + 1)] << 8 : 0; };
    auto draw = [&]() { return effectRandom(); };
    for (int guard = 0; guard < 256; ++guard) {
        Effect& e = effects[k];
        int pc = e.anim.pc, len = t.opLength(pc);
        if (len < 0) break;
        e.anim.pc = pc + len;
        int op = s[size_t(pc)], arg = len > 1 ? s[size_t(pc) + 1] : 0;
        switch (op) {
        case 0x00: e.anim.frame = u16(pc + 1); break;
        case 0x05: e.anim.wait = arg - 1; return;
        case 0x06: e.anim.wait = arg + draw() % std::max(1, s[size_t(pc) + 2] - arg + 1) - 1; return;
        case 0x07: e.anim.pc = u16(pc + 1); break;
        case 0x16: e.done = true; return;
        case 0x1E: if ((draw() & 0xff) <= arg) e.anim.pc = u16(pc + 2); break;
        case 0x18: sound(u16(pc + 1), e.x, e.y, e.owner); break;  // (sounds: their own random numbers)
        case 0x19: sound(u16(pc + 2 + 2 * (arg ? draw() % arg : 0)), e.x, e.y, e.owner); break;
        case 0x1A: sound(u16(pc + 1) + draw() % std::max(1, u16(pc + 3) - u16(pc + 1) + 1), e.x, e.y, e.owner); break;
        case 0x35: e.anim.ret = e.anim.pc; e.anim.pc = u16(pc + 1); break;
        case 0x36: e.anim.pc = e.anim.ret; break;
        case 0x2E: case 0x2F: noBreak(e.anim, op); break;
        case 0x32: e.anim.hidden = true; break;
        case 0x33: e.anim.hidden = false; break;
        case 0x3A: if (targetWithin(e.follow, u16(pc + 1))) e.anim.pc = u16(pc + 3); break;
        case 0x08: case 0x09: case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x14: case 0x42: {
            bool lo = op == 0x0D || op == 0x0E;  // (imgoluselo, imguluselo: offsets from the attack overlay list)
            auto at = lo ? loOffset(e.image, e.anim.frame, e.heading, s[size_t(pc) + 3]) : std::pair<int, int>{0, 0};
            int id = u16(pc + 1), dx = lo ? at.first : int8_t(s[size_t(pc) + 3]);
            int dy = lo ? at.second : int8_t(s[size_t(pc) + 4]);
            if (lo) op = op == 0x0D ? 0x08 : 0x09;  // (then as imgol, imgul)
            // (a sprite the game never draws: none)
            bool shown = op <= 0x09 || (id < int(t.spriteImage.size()) && t.spriteVisible[size_t(id)]);
            int image = !shown ? -1 : op <= 0x09 ? id : t.spriteImage[size_t(id)];
            bool under = op == 0x09 || op == 0x11 || op == 0x14 || op == 0x42 || e.under;
            Effect copy = e;  // (the vector may grow)
            bool attached = copy.follow >= 0 && op <= 0x09;  // (an image of the same sprite: it stays with the unit)
            int level = op <= 0x09 ? copy.elevation : spriteElevation(op, copy.elevation);  // (an image: its sprite's)
            size_t before = effects.size();
            addEffect(image, copy.x + dx, copy.y + dy, copy.owner, copy.heading, level, under, 0,
                      attached ? copy.follow : -1,
                      copy.dx + dx, copy.dy + dy);
            if (attached && effects.size() > before) {  // (just above or under its parent image)
                Effect& child = effects.back();
                child.depth = copy.depth + 1;
                child.layer = copy.layer + (op == 0x09 ? -1 : 1) * std::ldexp(1.0, -child.depth);
                child.under = child.layer < 0;
            }
            break;
        }
        default: break;
        }
    }
    effects[k].done = true;  // (a loop with no wait, or an unknown instruction)
}

void World::images(std::vector<LiveImage>& out) const {
    auto add = [&](int image, const Anim& a, int heading, int x, int y, int owner, bool air, int elevation) {
        if (image < 0 || a.hidden) return false;
        LiveImage li;
        li.image = image;
        li.elevation = elevation;
        li.frame = grpFrame(image, a.frame, heading, &li.flip);
        li.x = x;
        li.y = y;
        li.owner = owner;
        li.sortY = y;
        li.air = air;
        out.push_back(li);
        return true;
    };
    for (const Unit& u : units) {
        if (!u.alive || u.hidden) continue;
        const UnitData& d = t.units[u.type];
        int image = t.flingyImage(d.flingy);
        int self = int(&u - units.data()), order = 0;
        const int level = d.elevation;  // (its overlays, load and turret: the same sprite's level)
        // Its overlays by their place: those under it, then it, then those above (equal places: in their order).
        std::vector<const Effect*> overlays;
        for (const Effect& e : effects)
            if (e.follow == self) overlays.push_back(&e);
        std::stable_sort(overlays.begin(), overlays.end(),
                         [](const Effect* a, const Effect* b) { return a->layer < b->layer; });
        for (const Effect* e : overlays)
            if (e->layer < 0 && add(e->image, e->anim, e->heading, e->x, e->y, e->owner, false, level))
                out.back().order = order++;
        bool drawn = add(image, u.anim, u.heading, u.x, u.y, u.owner, (d.flags & kFlyer) != 0, level);
        if (drawn) out.back().order = order++;
        size_t mainAt = out.size() - 1;
        for (const Effect* e : overlays)
            if (e->layer >= 0 && add(e->image, e->anim, e->heading, e->x, e->y, e->owner, false, level))
                out.back().order = order++;
        if (u.carry > 0 && drawn) {  // A worker's load: above it, at its overlay offset.
            int gas = role(u.type).gasImage;  // (by worker: the full gas image)
            int carried = u.carryGas && gas >= 0 ? gas : t.rules.mineralImage;
            const LiveImage main = out[mainAt];
            LiveImage li = main;
            li.image = carried + (u.carry < t.rules.mineralLoad ? 1 : 0);
            if (u.carryGas) li.frame = 0;  // (gas: a fixed frame; minerals: the worker's)
            for (const auto& [img, frames] : t.carryOffsets)
                if (img == image && main.frame < int(frames.size())) {
                    li.x += (li.flip ? -1 : 1) * frames[size_t(main.frame)].first;
                    li.y += frames[size_t(main.frame)].second;
                }
            li.order = 1;
            out.push_back(li);
        }
        if (d.subunit != kNoUnit) {
            if (add(t.flingyImage(t.units[size_t(d.subunit)].flingy), u.turret, u.heading, u.x, u.y, u.owner, false,
                    level))
                out.back().order = 1;
        }
    }
    for (const Bullet& b : bullets) {  // (not a projectile whose sprite the game never draws: flame, spines, Scourge)
        const int level = bulletElevation(b);
        if (b.body >= 0 && !b.arrived) add(b.body, b.anim, b.heading, b.x, b.y, b.owner, false, level);  // (a Scarab)
        else if (b.body < 0 && t.flingyVisible(t.weapons[size_t(b.weapon)].flingy))
            add(t.flingyImage(t.weapons[size_t(b.weapon)].flingy), b.anim, b.heading, b.x, b.y, b.owner, false, level);
    }
    for (const Effect& e : effects)  // (remnants under the living units: sortY far up)
        if (e.follow < 0 && add(e.image, e.anim, e.heading, e.x, e.y, e.owner, false, e.elevation) && e.under)
            out.back().sortY = e.y - 65536;
}

namespace {

class Engine : public Simulator {
public:
    bool init(const FileReader& read, std::string& error) override {
        ready_ = tables_.load(read, error);
        return ready_;
    }
    bool setMap(std::vector<uint8_t> chk, std::string& error) override {
        stop();
        if (!ready_) {
            error = "The game data is not loaded";
            return false;
        }
        hasMap_ = map_.load(chk, tables_, error);
        return hasMap_;
    }

    bool mining(const MiningOptions& o, std::vector<MiningBase>& out, std::string& error) override {
        if (!check(error)) return false;
        out = miningBases(tables_, map_, o);
        if (out.empty()) {
            error = "The map has no base to mine at";
            return false;
        }
        std::vector<Race> races = baseRaces(tables_, map_, out, o);
        for (size_t k = 0; k < out.size(); ++k)
            for (Race race : o.mapRaces ? std::vector<Race>{races[k]} : o.races)
                for (int workers : o.workers) {
                    MiningBase& base = out[k];
                    World w(tables_, map_);
                    setupMining(w, {base}, {race}, workers, o);
                    while (w.frame < o.warmupFrames + o.frames) w.step();
                    base.runs.push_back(miningRun(w, 0, race, workers, o));
                }
        return true;
    }

    bool combat(const CombatOptions& o, CombatResult& out, std::string& error) override {
        if (!startCombat(o, error)) return false;
        while (step(1)) {
        }
        out = combatResult();
        stop();
        return error_.empty();
    }

    bool startCombat(const CombatOptions& o, std::string& error) override {
        if (!check(error)) return false;
        stop();
        world_ = std::make_unique<World>(tables_, map_);
        world_->recordSounds = true;
        world_->setFastForward(fastForward_);
        if (!setupCombat(*world_, o, error)) {
            world_.reset();
            return false;
        }
        combat_ = true;
        last_ = o.frames;
        return true;
    }

    bool startMining(const MiningOptions& o, std::string& error) override {
        if (!check(error)) return false;
        stop();
        liveBases_ = miningBases(tables_, map_, o);
        if (liveBases_.size() > 8) liveBases_.resize(8);
        if (liveBases_.empty()) {
            error = "The map has no base to mine at";
            return false;
        }
        miningOptions_ = o;
        world_ = std::make_unique<World>(tables_, map_);
        world_->recordSounds = true;
        world_->setFastForward(fastForward_);
        liveRaces_ = baseRaces(tables_, map_, liveBases_, o);
        setupMining(*world_, liveBases_, liveRaces_, o.workers.empty() ? 9 : o.workers[0], o);
        combat_ = false;
        last_ = o.warmupFrames + o.frames;
        return true;
    }

    bool startMap(const MapRunOptions&, MapRunInfo&, std::string& error) override {
        error = "Run the map is not in the simulation yet";
        return false;
    }

    bool live() const override { return world_ != nullptr; }
    const std::vector<uint8_t>* creep() const override { return world_ ? &world_->creep : nullptr; }
    uint64_t creepChanges() const override { return world_ ? world_->creepChanges : 0; }
    void setFastForward(bool on) override {
        fastForward_ = on;
        if (world_) world_->setFastForward(on);
    }
    bool step(int frames) override {
        if (!world_ || done_) return false;
        for (int i = 0; i < frames && !done_; ++i) {
            world_->step();
            if (combat_) {
                done_ = combatOver(*world_, last_);
            } else {
                done_ = world_->frame >= last_;
            }
        }
        return !done_;
    }
    int frame() const override { return world_ ? world_->frame : 0; }
    int lastFrame() const override { return last_; }
    bool done() const override { return done_; }
    const std::string& liveError() const override { return error_; }
    void images(std::vector<LiveImage>& out) const override {
        out.clear();
        if (world_) world_->images(out);
    }
    std::vector<LiveSound> takeSounds() override {
        std::vector<LiveSound> out;
        if (world_) out.swap(world_->sounds);
        return out;
    }
    CombatResult combatResult() const override {
        return world_ && combat_ ? combatResultOf(*world_) : CombatResult{};
    }
    std::vector<MiningBase> miningResult() const override {
        std::vector<MiningBase> out;
        if (!world_ || combat_) return out;
        const MiningOptions& o = miningOptions_;
        for (size_t i = 0; i < liveBases_.size(); ++i) {
            MiningBase b = liveBases_[i];
            b.runs.push_back(miningRun(*world_, int(i), liveRaces_[i], o.workers.empty() ? 9 : o.workers[0], o));
            out.push_back(b);
        }
        return out;
    }
    std::vector<PlayerState> players() const override {
        std::vector<PlayerState> out;
        if (!world_ || combat_) return out;
        for (size_t i = 0; i < liveBases_.size(); ++i) out.push_back(playerState(*world_, int(i)));
        return out;
    }
    void stop() override {
        world_.reset();
        done_ = false;
        error_.clear();
        last_ = 0;
    }

private:
    bool check(std::string& error) const {
        if (!ready_ || !hasMap_) error = ready_ ? "No map is open" : "The game data is not loaded";
        return ready_ && hasMap_;
    }

    Tables tables_;
    MapInfo map_;
    bool ready_ = false, hasMap_ = false, combat_ = false, done_ = false;
    std::unique_ptr<World> world_;
    bool fastForward_ = false;
    std::vector<MiningBase> liveBases_;
    std::vector<Race> liveRaces_;
    MiningOptions miningOptions_;
    int last_ = 0;
    std::string error_;
};

}  // namespace

std::unique_ptr<Simulator> createSimulator() { return std::make_unique<Engine>(); }

}  // namespace sim
