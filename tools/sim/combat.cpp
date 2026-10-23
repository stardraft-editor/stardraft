// Combat: the armies, Attack Move and Attack Unit, targets, weapons and projectiles, damage, death, and the result.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "world.h"

namespace sim {

namespace {

const int kStalemateFrames = 714;  // (our own rule: 30 seconds at Fastest)
const int kSeekIdleEvery = 3, kSeekEvery = 24;  // (our own search and destroy: how often a unit looks again)

}  // namespace

// The map's upgrades: a player's level of an upgrade, a weapon's damage
// with its upgrade, and the range bonus of the four units whose range an upgrade raises.
int World::level(int owner, int upgrade) const {
    if (owner < 0 || owner >= 12 || upgrade < 0 || upgrade >= 61 || map.upgrades.empty()) return 0;
    return map.upgrades[size_t(owner * 61 + upgrade)];
}

int World::weaponDamage(int weapon, int owner) const {
    const WeaponData& wd = t.weapons[size_t(weapon)];
    return (wd.damage + wd.bonus * std::min(level(owner, wd.upgrade), 100)) << 8;
}

// The range a unit's upgrade adds to a weapon of its (rules.ini rangeUpgrade).
int World::rangeBonus(const Unit& u, int weapon) const {
    const UnitRole& r = role(u.type);
    if (r.rangeUpgrade < 0 || !level(u.owner, r.rangeUpgrade)) return 0;
    if (r.rangeAirOnly && weapon >= 0 && weapon != weaponUnit(u.type).airWeapon) return 0;
    return r.rangeBonus;
}

const UnitData& World::weaponUnit(int type) const {
    const UnitData& d = t.units[size_t(type)];
    if (d.groundWeapon != kNoWeapon || d.airWeapon != kNoWeapon) return d;
    if (d.subunit != kNoUnit) return t.units[size_t(d.subunit)];
    if (role(type).launchesScarabs) return t.units[size_t(t.scarabType)];
    if (role(type).launchesInterceptors) return t.units[size_t(t.interceptorType)];
    return d;
}

bool World::hasWeapon(const Unit& u) const {
    if (role(u.type).launchesScarabs && u.scarabs <= 0) return false;
    if (role(u.type).launchesInterceptors) {  // (its Interceptors)
        int self = int(&u - units.data());
        return std::any_of(units.begin(), units.end(), [&](const Unit& f) { return f.alive && f.parent == self; });
    }
    const UnitData& d = weaponUnit(u.type);
    return d.groundWeapon != kNoWeapon || d.airWeapon != kNoWeapon;
}

int World::weaponFor(const Unit& a, const Unit& b) const {
    if (role(a.type).launchesScarabs && a.scarabs <= 0) return kNoWeapon;
    if ((role(a.type).attacksBurrowed && !a.burrowed) || (role(a.type).launchesInterceptors && !hasWeapon(a)))
        return kNoWeapon;
    const UnitData& d = weaponUnit(a.type);
    bool air = (t.units[size_t(b.type)].flags & kFlyer) != 0;
    int w = air ? d.airWeapon : d.groundWeapon;
    if (w >= kNoWeapon || !(t.weapons[size_t(w)].targetFlags & (air ? 1 : 2))) return kNoWeapon;
    return w;
}

bool World::canAttack(const Unit& a, const Unit& b) const {
    return b.alive && !b.hidden && !b.neutral && a.side != b.side && !(t.units[size_t(b.type)].flags & kInvincible) &&
           detected(b, a.side) && weaponFor(a, b) != kNoWeapon;
}

// Cloaking: a Dark Templar, an Observer or a burrowed unit is seen by a side only while one of
// its finished detectors is in range of it (box gap): its sight range.
bool World::detected(const Unit& v, int side) const {
    if (!v.burrowed && !(t.units[size_t(v.type)].flags & kCloaked)) return true;
    for (const Unit& d : units) {
        const UnitData& dd = t.units[size_t(d.type)];
        if (!d.alive || d.hidden || d.side != side || !(dd.flags & kDetector) || &d == &v) continue;
        if (gap(d, v) <= dd.sight * 32) return true;
    }
    return false;
}

// The weapon's range (a Reaver: its acquisition range).
bool World::inRange(const Unit& a, const Unit& b) const {
    int w = weaponFor(a, b);
    if (w == kNoWeapon) return false;
    const WeaponData& wd = t.weapons[size_t(w)];
    int g = gap(a, b);
    bool launcher = role(a.type).launchesScarabs || role(a.type).launchesInterceptors;
    int range = launcher ? t.units[size_t(a.type)].acquire * 32 : wd.maxRange + rangeBonus(a, w);
    // The moving-target margin: a moving attacker with braking movement (type 0) adds its halt
    // distance, unless the target moves the same way.
    const FlingyData& f = t.flingy[size_t(t.units[size_t(a.type)].flingy)];
    bool moving = a.x != a.moveX || a.y != a.moveY, targetMoving = b.x != b.moveX || b.y != b.moveY;
    if (moving && f.control == kControlBraking && a.speed > 0 &&
        !(targetMoving && std::abs(int(int8_t(uint8_t(a.velDir - b.velDir)))) <= t.rules.sameWayTolerance))
        range += haltDistance(a) >> 8;
    return g <= range && (wd.minRange == 0 || g > wd.minRange);
}

// The target priority: 0 is the best; workers 2; a unit that cannot attack the attacker 2, 3 or 4; larvae and eggs 5.
int World::priority(int attacker, int candidate) const {
    const Unit &a = units[size_t(attacker)], &v = units[size_t(candidate)];
    const UnitData& d = t.units[size_t(v.type)];
    if (role(v.type).lowPriority) return t.rules.priorityLow;
    if (d.flags & kWorker) return t.rules.priorityWorker;
    if (!canAttack(v, a))
        return hasWeapon(v)  ? t.rules.priorityNoAttackArmed
               : v.building  ? t.rules.priorityBuilding
                             : t.rules.priorityMover;
    return 0;
}

// The best target in acquisition range: the lowest priority, then the nearest centre. The range is units.dat's
// Target Acquisition Range, but at least the weapon's range (some units, such as the Marine, have 0 there).
int World::acquire(int i, bool draw) {
    const Unit& u = units[size_t(i)];
    const UnitData& d = weaponUnit(u.type);
    int range = t.units[size_t(u.type)].acquire * 32, minRange = 0;
    for (int w : {d.groundWeapon, d.airWeapon})
        if (w != kNoWeapon) {
            range = std::max(range, t.weapons[size_t(w)].maxRange);
            int m = t.weapons[size_t(w)].minRange;
            if (m > 0 && (minRange == 0 || m < minRange)) minRange = m;
        }
    // (a range upgrade adds its pixels after the larger of the two)
    range += std::max(rangeBonus(u, d.groundWeapon), rangeBonus(u, d.airWeapon));
    int best = -1, bestPriority = 99, bestDist = 0;
    std::vector<int> bucket;  // (all of the best priority)
    forNear(u.x - range - 160, u.y - range - 160, u.x + range + 160, u.y + range + 160, [&](int j) {
        const Unit& v = units[size_t(j)];
        if (!canAttack(u, v)) return;
        int g = gap(u, v);
        if (g > range || g < minRange) return;
        int p = priority(i, j), dist = octagonalLength(v.x - u.x, v.y - u.y);
        if (p < bestPriority) bucket.clear();
        if (p <= bestPriority) bucket.push_back(j);
        // (the nearest; of equal ones, the first in the list)
        if (p < bestPriority || (p == bestPriority && (dist < bestDist || (dist == bestDist && j < best)))) {
            best = j;
            bestPriority = p;
            bestDist = dist;
        }
    });
    std::sort(bucket.begin(), bucket.end());
    // A Carrier or a Reaver takes a random one of them (a random draw); the others the nearest.
    if (draw && (role(u.type).launchesScarabs || role(u.type).launchesInterceptors) && bucket.size() > 1)
        best = bucket[size_t(random()) % bucket.size()];
    return best;
}

// A Medic's order: on its order tick it heals its target by at most 200 raw within a
// 30-pixel gap (half that in energy), or follows it there; with no target it walks its attack move and every 15 frames
// looks for the nearest hurt organic ground unit of its owner within 160 pixels. Medics never attack or flee.
void World::medic(int i) {
    Unit& m = units[size_t(i)];
    if (!tick(m)) return;
    auto ok = [&](int j) {
        if (j < 0 || j == i) return false;
        const Unit& v = units[size_t(j)];
        uint32_t f = t.units[size_t(v.type)].flags;
        return v.alive && !v.hidden && v.owner == m.owner && (f & kOrganic) && !(f & (kBuilding | kFlyer)) &&
               v.hp < t.units[size_t(v.type)].hp;
    };
    if (!ok(m.healTarget)) m.healTarget = -1;
    if (m.healTarget < 0 && m.timer == 0) {
        m.timer = t.rules.targetSearchFrames;
        double best = 0;
        for (size_t j = 0; j < units.size(); ++j) {
            const Unit& v = units[j];
            if (!ok(int(j)) || v.healedFrame == frame || std::abs(v.x - m.x) > t.rules.healSearch ||
                std::abs(v.y - m.y) > t.rules.healSearch)
                continue;
            double d = std::hypot(double(v.x - m.x), double(v.y - m.y));
            if (m.healTarget < 0 || d < best) {
                m.healTarget = int(j);
                best = d;
            }
        }
    }
    if (m.healTarget < 0) {  // (its attack move: it walks with the group)
        if (m.order == AttackMove && (m.moveX != m.amX || m.moveY != m.amY) && (m.x != m.amX || m.y != m.amY))
            setMove(m, m.amX, m.amY);
        return;
    }
    Unit& v = units[size_t(m.healTarget)];
    if (gap(m, v) > t.rules.healGap) {
        setMove(m, v.x, v.y);
        return;
    }
    stopMove(m);
    m.aimX = v.x;
    m.aimY = v.y;
    if (v.healedFrame == frame) return;
    int amount = std::min(t.units[size_t(v.type)].hp - v.hp, t.rules.healMax);
    int cost = amount / t.rules.healEnergyDivisor;
    if (m.energy < cost) return;
    v.hp += amount;
    m.energy -= cost;
    v.healedFrame = frame;
    if (m.order == AttackMove) m.healTarget = -1;  // (one heal, then on with the attack move)
}

// Where a unit goes to reach a target unit: the nearest point of the target's box grown by
// the unit's own size where no ground unit at rest stands (units that move are not in the way of the plan); none free:
// the target's place.
void World::outlineSpot(const Unit& u, const Unit& target, int& x, int& y) const {
    const UnitData &d = t.units[size_t(u.type)], &td = t.units[size_t(target.type)];
    x = target.x;
    y = target.y;
    if (d.flags & kFlyer) return;
    int l = target.x - td.left - d.right - 1, r = target.x + td.right + d.left + 1;
    int top = target.y - td.up - d.down - 1, bottom = target.y + td.down + d.up + 1;
    std::vector<int> nearby;
    forNear(l - 192, top - 192, r + 192, bottom + 192, [&](int j) { nearby.push_back(j); });
    auto taken = [&](int px, int py) {
        if (!map.boxWalkable(px - d.left, py - d.up, px + d.right, py + d.down)) return true;
        for (int jj : nearby) {
            const Unit& o = units[size_t(jj)];
            if (&o == &u || &o == &target || !o.alive || o.hidden || o.burrowed) continue;
            const UnitData& od = t.units[size_t(o.type)];
            if ((od.flags & kFlyer) || o.x != o.moveX || o.y != o.moveY) continue;
            if (overlaps(u.type, px, py, o, true))
                return true;
        }
        return false;
    };
    double best = -1;
    auto consider = [&](int px, int py) {
        double dist = std::hypot(double(px - u.x), double(py - u.y));
        if ((best < 0 || dist < best) && !taken(px, py)) {
            best = dist;
            x = px;
            y = py;
        }
    };
    for (int px = l; px <= r; px += 4) {
        consider(px, top);
        consider(px, bottom);
    }
    for (int py = top; py <= bottom; py += 4) {
        consider(l, py);
        consider(r, py);
    }
}

void World::combatOrder(int i) {
    Unit& u = units[size_t(i)];
    if (u.dieAt >= 0 || u.neutral) return;  // (the map's own units do not fight)
    if (frame < role(u.type).startDelay) return;  // (an Archon starts late)
    if (u.building) {  // (a defence looks for a target every frame)
        u.poll = 0;
        if (u.order != AttackUnit) u.timer = 0;
    }
    if (role(u.type).healer) {
        medic(i);
        return;
    }
    if (role(u.type).spiderMine) {  // A Spider Mine, every frame: its phases in u.state.
        const UnitData& d = t.units[size_t(u.type)];
        Unit* target = u.target >= 0 && canAttack(u, units[size_t(u.target)]) ? &units[size_t(u.target)] : nullptr;
        int dist = target ? octagonalLength(target->x - u.x, target->y - u.y) : 0;
        switch (u.state) {
        case 0:  // arming (60 frames, then 3), then the burrow (9 frames)
            if (u.timer == 0) {
                u.state = 1;
                u.timer = t.rules.mineBurrow;
            }
            return;
        case 1:
            if (u.timer > 0) return;
            u.burrowed = true;
            u.state = 2;
            [[fallthrough]];
        case 2: {  // watching: the nearest enemy ground unit in a square of 32 x its acquisition range
            int best = -1, bestDist = 0, reach = 32 * d.acquire;
            for (size_t j = 0; j < units.size(); ++j) {
                const Unit& v = units[j];
                uint32_t f = t.units[size_t(v.type)].flags;
                if (!v.alive || v.hidden || v.neutral || v.side == u.side || v.building ||
                    (f & (kFlyer | kInvincible)) || std::abs(v.x - u.x) > reach || std::abs(v.y - u.y) > reach ||
                    !detected(v, u.side))
                    continue;
                int n = octagonalLength(v.x - u.x, v.y - u.y);
                if (best < 0 || n < bestDist) {
                    best = int(j);
                    bestDist = n;
                }
            }
            if (best >= 0) {
                u.target = best;
                u.state = 3;
                u.timer = t.rules.mineUnburrow;  // (the unburrow: its burrowed flag goes after it)
            }
            return;
        }
        case 3:
            if (u.timer > 0) return;
            u.burrowed = false;
            if (!target) {  // (its target gone: it burrows again)
                u.state = 0;
                u.timer = t.rules.mineBurrow;
                return;
            }
            u.state = 4;
            u.burrowStep = 0;  // (whether it has moved)
            [[fallthrough]];
        case 4:  // the chase: straight at it while within reach; near it, the fuse
            if (!target || dist > t.rules.mineChase) {
                stopMove(u);
                u.state = 1;
                u.timer = t.rules.mineBurrow;
            } else if (dist <= t.rules.mineFuseRange) {
                stopMove(u);
                u.state = 5;
                u.timer = u.burrowStep ? t.rules.mineFuse : t.rules.mineFuseStill;
            } else {
                setMove(u, target->x, target->y);
                u.burrowStep = 1;
            }
            return;
        default:  // the fuse: then it dies, and its splash lands a little later (friendly units too)
            if (u.timer > 0) return;
            Bullet b;
            b.weapon = d.groundWeapon;
            b.owner = u.owner;
            b.side = u.side;
            b.source = i;
            b.target = u.target;
            b.x = b.tx = u.x;
            b.y = b.ty = u.y;
            b.ex = b.x << 8;
            b.ey = b.y << 8;
            b.scarab = b.arrived = b.launched = true;  // (the delayed blast of the Scarab's kind)
            b.life = t.rules.mineBlastDelay;
            bullets.push_back(b);
            kill(i, false);
            return;
        }
    }
    if (u.burrowing) {  // Burrow, on the order tick: stop, then its script
        if (!tick(u)) return;
        if (u.burrowStep == 0) {
            stopMove(u);
            u.burrowStep = 1;
        } else if (u.burrowStep == 1) {
            u.burrowStep = 2;
        } else if (u.burrowStep == 2) {
            animate(i, kAnimBurrow);
            u.burrowStep = 3;
        }
        return;
    }
    if (u.fighter) {  // An Interceptor, on its order tick.
        const Unit& c = units[size_t(u.parent)];
        if (!c.alive) {
            kill(i, false);
            return;
        }
        if (u.hidden) return;
        if (!tick(u)) return;
        bool targetOk = u.target >= 0 && canAttack(u, units[size_t(u.target)]);
        if (!targetOk && c.target >= 0 && canAttack(u, units[size_t(c.target)])) {
            u.target = c.target;
            targetOk = true;
        }
        bool low = u.shields * t.rules.interceptorLowShields < t.units[size_t(u.type)].shields * 256;
        if (u.state != 9 && (low || !targetOk ||
                             gap(c, units[size_t(u.target)]) >
                                 t.units[size_t(c.type)].acquire * 32 + t.rules.interceptorLeash))
            u.state = 9;  // (back to the Carrier)
        auto near = [&](const Unit& g, int scale) {
            const int spread = t.rules.interceptorSpreadRange;
            int x = g.x + random(1 - spread, spread) * scale, y = g.y + random(1 - spread, spread) * scale;
            if (x < 0 || x >= map.width * 32) x = 2 * g.x - x;
            if (y < 0 || y >= map.height * 32) y = 2 * g.y - y;
            setMove(u, x, y);
        };
        auto reached = [&] {
            return std::hypot(double(u.moveX - u.x), double(u.moveY - u.y)) <= t.rules.interceptorNear;
        };
        if (u.state == 9) {
            setMove(u, c.x, c.y);
            if (gap(u, c) <= t.rules.interceptorDock) {  // (docked)
                u.hidden = true;
                stopMove(u);
                u.order = Idle;
                u.state = 0;
            }
            return;
        }
        const Unit& g = units[size_t(u.target)];
        if (u.state == 0) {  // launch: to a point near the target, then a wait
            near(g, t.rules.interceptorSpread[0]);
            u.timer = t.rules.interceptorWait;
            u.state = 1;
        } else if (u.state == 1) {
            if (u.timer == 0) u.state = 3;
        } else {
            int w = weaponFor(u, g);
            // (it fires only in its attack state, facing the target: the normal weapon rules)
            int error = std::abs(int(int8_t(uint8_t(direction(g.x - u.x, g.y - u.y) - u.heading))));
            if (u.state == 3 && inRange(u, g) && u.cooldown == 0 && !u.repeatAttack &&
                error <= t.weapons[size_t(w)].angle)
                startAttack(i, g, t.weapons[size_t(w)], false);
            if (u.state == 3) {
                if (gap(u, g) <= t.rules.interceptorNear) {
                    near(g, t.rules.interceptorSpread[1]);
                    u.state = 4;
                } else {
                    setMove(u, g.x, g.y);
                }
            } else if (u.state == 4 && reached()) {
                near(g, t.rules.interceptorSpread[2]);
                u.state = 5;
            } else if (u.state == 5 && reached()) {
                setMove(u, g.x, g.y);
                u.state = 3;
            }
        }
        return;
    }
    // (it cannot attack: it stands, a target only)
    if (!hasWeapon(u) && !role(u.type).launchesInterceptors && !role(u.type).launchesScarabs) {
        if (u.x != u.moveX || u.y != u.moveY) stopMove(u);
        return;
    }
    auto valid = [&](int j) { return j >= 0 && canAttack(u, units[size_t(j)]); };
    auto attack = [&](int target, Order after) {
        u.target = target;
        u.next = after;
        setOrder(u, AttackUnit);
    };
    // Attack Move takes an auto-target at once, every frame.
    if (u.order == AttackMove && valid(u.autoTarget)) {
        stopMove(u);
        attack(u.autoTarget, AttackMove);
        return;
    }
    // The order tick: every 9 frames; a Zergling's every frame while it can attack.
    if (role(u.type).ordersEveryFrame && u.cooldown == 0) u.poll = 0;
    if (!tick(u)) return;
    switch (u.order) {
    case Idle:  // (guard: a look for targets every 15 frames)
        if (!hasWeapon(u)) return;
        if (valid(u.autoTarget)) {
            attack(u.autoTarget, Idle);
        } else if (u.timer == 0) {
            u.timer = t.rules.targetSearchFrames;
            int target = acquire(i);
            if (target >= 0) attack(target, Idle);
            else if (seek && (frame + i) % kSeekIdleEvery == 0) {  // (search and destroy: the nearest enemy)
                int e = nearestEnemy(u);
                if (e >= 0) {
                    u.amX = units[size_t(e)].x;
                    u.amY = units[size_t(e)].y;
                    setOrder(u, AttackMove);
                }
            }
        }
        return;
    case AttackMove: {
        if (u.timer == 0 && hasWeapon(u)) {
            u.timer = t.rules.targetSearchFrames;
            // (attack-move acquisition takes the nearest and draws nothing, also for a Carrier or a Reaver)
            int target = acquire(i, false);
            if (target >= 0) {
                stopMove(u);
                attack(target, AttackMove);
                return;
            }
        }
        if (u.x == u.amX && u.y == u.amY) {
            setOrder(u, Idle);
            return;
        }
        if (seek && (frame + i) % kSeekEvery == 0) {  // (search and destroy: every 24 frames, the nearest enemy again)
            int e = nearestEnemy(u);
            if (e >= 0) {
                // (the same enemy in the same region: the walk goes on, with no new path)
                const Unit& v = units[size_t(e)];
                int tx = std::clamp(v.x / 32, 0, map.width - 1), ty = std::clamp(v.y / 32, 0, map.height - 1);
                int region = map.tileRegion.empty() ? -1 : map.tileRegion[size_t(ty * map.width + tx)];
                if (e != u.seekTarget || region != u.seekRegion) {
                    u.seekTarget = e;
                    u.seekRegion = region;
                    u.amX = v.x;
                    u.amY = v.y;
                }
            }
        }
        if (!u.burrowed && (u.moveX != u.amX || u.moveY != u.amY)) setMove(u, u.amX, u.amY);
        return;
    }
    case AttackUnit: {
        // Keep a target in range with priority 0; else only a strictly better priority (the
        // auto-target or a new one) replaces it. A target out of range counts as priority 1 at best.
        auto rank = [&](int j) {
            int p = priority(i, j);
            return p == 0 && !inRange(u, units[size_t(j)]) ? 1 : p;
        };
        int current = valid(u.target) ? u.target : -1;
        // (a Reaver whose timer runs keeps a valid target and takes no other)
        // (a target is re-chosen only when the unit is ready to attack)
        bool locked = (role(u.type).launchesScarabs && u.timer > 0) || (current >= 0 && u.cooldown > 0);
        if (locked && current < 0) return;
        if (!locked && !(current >= 0 && rank(current) == 0)) {
            int bestRank = current >= 0 ? rank(current) : 99;
            if (valid(u.autoTarget) && rank(u.autoTarget) < bestRank) {
                current = u.autoTarget;
                bestRank = rank(current);
            }
            int fresh = bestRank > 0 ? acquire(i, false) : -1;  // (a new one: by its plain priority)
            if (valid(fresh) && priority(i, fresh) < bestRank) current = acquire(i);  // (the draw only now)
        }
        if (current < 0) {
            u.target = -1;
            stopMove(u);
            setOrder(u, u.next != None ? u.next : Idle);
            u.next = None;
            u.timer = 0;
            return;
        }
        u.target = current;
        const Unit& target = units[size_t(current)];
        int weapon = weaponFor(u, target);
        const WeaponData& wd = t.weapons[size_t(weapon)];
        // Suicide units hit when they touch the target.
        if (wd.behaviour == kSuicide && !role(u.type).launchesScarabs) {
            if (gap(u, target) > wd.maxRange) {  // (in contact: 3 px)
                setMove(u, target.x, target.y);
                u.poll = 0;
            } else {
                if (firstShot[u.side] < 0) firstShot[u.side] = frame;
                fire(i, weapon, false);
            }
            return;
        }
        if (!inRange(u, target)) {
            int x, y;
            outlineSpot(u, target, x, y);
            setMove(u, x, y);
            return;
        }
        stopMove(u);
        u.aimX = target.x;
        u.aimY = target.y;
        if (u.cooldown > 0) {
            u.poll = std::min(u.poll, u.cooldown - 1);
            return;
        }
        if (u.repeatAttack) {  // (its attack animation runs: the next frame again; a Corsair fires every 8 frames)
            u.poll = 0;
            return;
        }
        if (role(u.type).launchesInterceptors) {  // (one Interceptor out when its timer is 0, then the next)
            Unit* best = nullptr;
            for (Unit& f : units) {
                if (!f.alive || f.parent != i) continue;
                if (!f.hidden && f.state != 9) f.target = current;  // (the ones out take its target)
                if (f.hidden && (!best || f.hp > best->hp)) best = &f;
            }
            if (u.timer == 0 && best && best->hp * t.rules.interceptorLaunchHp >= t.units[size_t(best->type)].hp) {
                Unit& f = *best;
                f.hidden = false;
                f.x = f.moveX = u.x;
                f.y = f.moveY = u.y;
                f.ex = f.x << 8;
                f.ey = f.y << 8;
                f.shields = t.units[size_t(f.type)].shields << 8;
                f.target = current;
                setOrder(f, AttackUnit);
                f.state = 0;
                u.timer = t.rules.interceptorLaunch;
                gridUnits = 0;  // (it moved to the Carrier: the rest of this frame searches all units)
            }
            return;
        }
        if (role(u.type).launchesScarabs) {  // (one Scarab out, a time apart, facing it)
            auto out = [&](const Bullet& o) { return o.alive && o.scarab && !o.arrived && o.source == i; };
            if (u.timer > 0 || std::any_of(bullets.begin(), bullets.end(), out)) return;
            if (((u.heading - direction(target.x - u.x, target.y - u.y)) & 255) > 1) {
                u.poll = 0;
                return;
            }
            Bullet b;
            const UnitData& scarab = t.units[size_t(t.scarabType)];
            b.weapon = scarab.groundWeapon;
            b.scarab = b.launched = true;
            b.owner = u.owner;
            b.side = u.side;
            b.source = i;
            b.target = current;
            b.x = b.tx = u.x + dirX(u.heading) * t.rules.scarabForward / 256;
            b.y = b.ty = u.y + dirY(u.heading) * t.rules.scarabForward / 256;
            b.ex = b.x << 8;
            b.ey = b.y << 8;
            b.heading = u.heading;
            b.body = t.flingyImage(scarab.flingy);
            b.anim.header = t.scriptHeader(b.body);
            startAnim(b.anim, kAnimWalking);  // (its walking, for the image)
            bullets.push_back(b);
            u.timer = t.rules.scarabLock;
            --u.scarabs;
            if (firstShot[u.side] < 0) firstShot[u.side] = frame;
            return;
        }
        if (role(u.type).attacksBurrowed) u.heading = direction(target.x - u.x, target.y - u.y);  // (no turning)
        bool turret = u.turret.header >= 0;  // (a turret turns at once, and a building: no facing test)
        int error = std::abs(int(int8_t(uint8_t(direction(target.x - u.x, target.y - u.y) - u.heading))));
        if (!turret && !u.building && error > wd.angle) {
            u.poll = 0;
            return;
        }
        startAttack(i, target, wd, turret);
        return;
    }
    default:
        return;
    }
}

// An attack starts: the cooldown with its jitter, then the attack animation (its repeat
// part: the reference's first shots and hits fit it, not Init first), which fires the weapon.
void World::startAttack(int i, const Unit& target, const WeaponData& wd, bool turret) {
    Unit& u = units[size_t(i)];
    u.repeatAttack = true;
    spinSource = -1;  // (the first bullet of an attack draws its side)
    int cooldown = wd.cooldown;
    if (role(u.type).cooldownUpgrade >= 0 && level(u.owner, role(u.type).cooldownUpgrade))
        cooldown /= t.rules.cooldownUpgradeDivisor;
    u.cooldown = std::max(0, cooldown + random() % t.rules.cooldownJitter[0] + t.rules.cooldownJitter[1]);
    if (firstShot[u.side] < 0) firstShot[u.side] = frame;
    bool air = (t.units[size_t(target.type)].flags & kFlyer) != 0;
    Anim& anim = turret ? u.turret : u.anim;
    int attackAnim = air ? kAnimAirAttack : kAnimGroundAttack;
    if (!t.animOffset(anim.header, attackAnim)) u.repeatAttack = false;  // (no script: it cannot fire)
    if (turret) startAnim(anim, attackAnim);
    else animate(i, attackAnim);
    if (!t.firesFromScript(anim.header, attackAnim)) {  // (the Archon: its hit at once)
        u.repeatAttack = false;
        fire(i, air ? weaponUnit(u.type).airWeapon : weaponUnit(u.type).groundWeapon, false);
    }
    u.walkingAnim = 2;
}

// A weapon fired by an attack animation: a melee hit now, or projectiles.
void World::fire(int i, int weapon, bool melee, int forward) {
    const Unit& u = units[size_t(i)];
    if (weapon < 0 || weapon >= kNoWeapon || u.target < 0 || !units[size_t(u.target)].alive) return;
    const WeaponData& wd = t.weapons[size_t(weapon)];
    const Unit& target = units[size_t(u.target)];
    if (melee) {
        damage(u.target, i, weapon, weaponDamage(weapon, u.owner), 1);
        return;
    }
    Bullet b;
    b.weapon = weapon;
    b.owner = u.owner;
    b.side = u.side;
    b.source = i;
    b.target = u.target;
    b.heading = u.heading;
    b.tx = target.x;
    b.ty = target.y;
    if (wd.behaviour == kSuicide) {  // (a suicide unit: it dies now; its projectile hits from its own animation)
        b.x = target.x;
        b.y = target.y;
        b.ex = b.x << 8;
        b.ey = b.y << 8;
        b.arrived = b.launched = true;
        b.anim.header = t.scriptHeader(t.flingyImage(wd.flingy));
        startAnim(b.anim, kAnimInit);
        bullets.push_back(b);
        units[size_t(i)].dieAt = frame;  // (it is gone now)
        return;
    }
    if (forward < 0) forward = wd.forward;
    // One bullet, or two when the weapon's bullet count (weapons.dat damage factor) is 2: each its own flight and hit
    // with the full damage.
    for (int n = 0; n < std::max(1, wd.factor); ++n) {
        int limit = t.weaponRoles[size_t(weapon)].activeLimit;
        if (limit > 0 && std::count_if(bullets.begin(), bullets.end(), [&](const Bullet& o) {
                return o.alive && o.weapon == weapon;
            }) >= limit)
            return;  // (at most so many of them at once)
        Bullet c = b;
        if (wd.spin) {  // the heading offset: the same unit's next bullet takes the other side
            int side = spinSource == i ? -spinSide : (random() & 1) ? 1 : -1;
            spinSource = i;
            spinSide = side;
            c.heading = (u.heading + side * wd.spin) & 255;
        }
        c.velDir = c.heading;
        bool appear =
            wd.behaviour == kAppearOnTarget || wd.behaviour == kAppearOnSite || wd.behaviour == kPersistOnSite;
        c.x = appear ? target.x : u.x + dirX(u.heading) * forward / 256;
        c.y = appear ? target.y : u.y + dirY(u.heading) * forward / 256 - wd.upward;
        bool aimed = wd.behaviour <= kAppearOnTarget || wd.behaviour == kAppearOnSite || wd.behaviour == kBounce;
        if (aimed && (random() & 0xff) <= 0) c.missed = true;  // the miss roll: the dodge chance is 0 on open ground
        c.ex = c.x << 8;
        c.ey = c.y << 8;
        if (wd.behaviour == kToMaxRange) {  // (it flies out to its maximum range)
            c.tx = c.x + dirX(c.heading) * wd.maxRange / 256;
            c.ty = c.y + dirY(c.heading) * wd.maxRange / 256;
        }
        c.life = wd.removeAfter;
        c.bounces = wd.behaviour == kBounce ? t.rules.bounces : 0;
        c.arrived = !(wd.behaviour <= kFlyFollow || wd.behaviour >= kBounce);
        c.anim.header = t.scriptHeader(t.flingyImage(wd.flingy));
        startAnim(c.anim, kAnimInit);
        bullets.push_back(c);
    }
}

// A projectile's frame: its animation first, then its state. Flying ones wait for the launch
// signal of their animation, fly (homing when they follow or bounce), and at the end play their death animation, which
// has the hit. Those that appear on the target hit from their first animation.
void World::bulletStep(int index) {
    runAnim(bullets[size_t(index)].anim, -1, index);
    Bullet& b = bullets[size_t(index)];
    if (!b.alive) return;
    const WeaponData& wd = t.weapons[size_t(b.weapon)];
    bool targetAlive = b.target >= 0 && units[size_t(b.target)].alive;
    if (b.scarab) {  // (a Scarab)
        if (b.arrived) {  // (exploded: the blast lands a few frames later)
            if (--b.life == 0) {
                bulletHit(b);
                b.alive = false;
            }
            return;
        }
        if (targetAlive) {
            b.tx = units[size_t(b.target)].x;
            b.ty = units[size_t(b.target)].y;
        }
        ++b.age;
        if (b.age >= t.rules.scarabStand) {  // (it stands after its release, then goes straight at its speed)
            int dist = octagonalLength(b.tx - b.x, b.ty - b.y);
            b.heading = direction(b.tx - b.x, b.ty - b.y);
            if (dist <= t.rules.scarabSpeed) {
                b.x = b.tx;
                b.y = b.ty;
            } else {
                double l = std::hypot(double(b.tx - b.x), double(b.ty - b.y));
                b.x += int(std::lround((b.tx - b.x) * t.rules.scarabSpeed / l));
                b.y += int(std::lround((b.ty - b.y) * t.rules.scarabSpeed / l));
            }
        }
        // (from its second step, within half its inner splash radius of the target, or at its last place, it explodes)
        bool close = targetAlive ? pointGap(units[size_t(b.target)], b.x, b.y) <= wd.inner / t.rules.scarabDetonate
                                 : b.x == b.tx && b.y == b.ty;
        if (b.age >= t.rules.scarabArmFrames && close) {
            b.arrived = true;
            b.life = t.rules.scarabBlastDelay;
        } else if (b.age >= t.rules.scarabLife) {
            b.alive = false;
        }
        return;
    }
    if (wd.behaviour == kPersistOnSite) {  // (damage over time: a hit every few frames)
        if (--b.life <= 0) {
            b.alive = false;
        } else if (b.life % t.rules.persistHitFrames == 0) {
            b.hit = false;
            bulletHit(b);
        }
        return;
    }
    if (b.arrived) {
        if (b.anim.paused && !b.hit && wd.behaviour != kBounce) {  // (its script has no hit: it hits as it ends)
            bulletHit(b);
            b.alive = false;
        }
        return;
    }
    if (!b.launched && ++b.age < t.rules.launchWaitMax) return;
    if ((wd.behaviour == kFlyFollow || wd.behaviour == kBounce) && !b.missed && targetAlive) {
        b.tx = units[size_t(b.target)].x;
        b.ty = units[size_t(b.target)].y;
    }
    // The flight with its flingy data: the velocity direction turns by half the turn rate, the picture by the full
    // rate; it speeds up when it goes the right way or is far away, and near its target while it still turns only when
    // it has room to turn (else it slows down: no circling round the target); the last step lands it on its target.
    const FlingyData& f = t.flingy[size_t(wd.flingy)];
    double fx = double((b.tx << 8) - b.ex), fy = double((b.ty << 8) - b.ey);
    int dist = int(std::sqrt(fx * fx + fy * fy));
    int desired = dist > 0 ? direction(b.tx - b.x, b.ty - b.y) : b.velDir;
    b.velDir = turnToward(b.velDir, desired, f.turn ? std::max(1, f.turn / t.rules.bulletTurnDivisor) : 256);
    b.heading = turnToward(b.heading, desired, f.turn ? f.turn : 256);
    bool accelerate = b.velDir == desired || dist >= t.rules.bulletNear * 256;
    if (!accelerate) {
        const std::vector<int>& m = t.rules.bulletTurnMargin;
        int r = std::max(1, f.turn / t.rules.bulletTurnDivisor);
        int diff = std::abs(int(int8_t(uint8_t(desired - b.velDir))));
        int turnFrames = (m[0] * diff + r - 1) / r;
        accelerate = turnFrames * m[1] / m[2] <= dist >> 8;
    }
    b.speed = std::clamp(b.speed + (accelerate ? std::max(f.accel, 1) : -f.accel), 0, f.topSpeed);
    if (b.speed >= dist || f.topSpeed <= 0) {
        b.ex = b.tx << 8;
        b.ey = b.ty << 8;
    } else if (b.speed > 0) {
        b.ex += dirX(b.velDir) * b.speed / 256;
        b.ey += dirY(b.velDir) * b.speed / 256;
    }
    b.x = (b.ex + 128) >> 8;
    b.y = (b.ey + 128) >> 8;
    bool there = b.x == b.tx && b.y == b.ty;
    bool expired = b.life > 0 && --b.life == 0;
    if (!there && !expired) return;
    if (wd.behaviour == kBounce && there) {  // the bounce: hits divided by 1, 3 and 9
        --b.bounces;
        if (!b.missed && targetAlive)
            damage(b.target, b.source, b.weapon, weaponDamage(b.weapon, b.owner),
                   t.rules.bounceDivisor[size_t(std::clamp(t.rules.bounces - 1 - b.bounces, 0,
                                                           int(t.rules.bounceDivisor.size()) - 1))]);
        if (b.bounces > 0) {
            for (size_t j = 0; j < units.size(); ++j) {
                const Unit& v = units[j];
                if (int(j) == b.target || int(j) == b.previous || !v.alive || v.hidden || v.side == b.side ||
                    std::abs(v.x - b.x) > t.rules.bounceSearch || std::abs(v.y - b.y) > t.rules.bounceSearch)
                    continue;
                bool air = (t.units[size_t(v.type)].flags & kFlyer) != 0;
                if (!(wd.targetFlags & (air ? 1 : 2))) continue;
                b.previous = b.target;
                b.target = int(j);
                b.missed = false;
                b.tx = v.x;
                b.ty = v.y;
                return;
            }
        }
        b.hit = true;  // (its death animation hits no more)
    }
    b.arrived = true;
    if (t.animOffset(b.anim.header, kAnimDeath)) {
        startAnim(b.anim, kAnimDeath);
        runAnim(b.anim, -1, index);
    } else {
        bulletHit(b);
        b.alive = false;
    }
}

void World::bulletHit(Bullet& b) {
    if (t.weaponRoles[size_t(b.weapon)].lineHits) {  // Lurker spines: a line of hits, once a pair in a time
        const WeaponData& wd = t.weapons[size_t(b.weapon)];
        // (the line from the last hit to here: each unit within the inner radius of a point of it, every 4 pixels)
        int x0 = b.hitX >= 0 ? b.hitX : b.x, y0 = b.hitX >= 0 ? b.hitY : b.y;
        int steps = std::max(std::abs(b.x - x0), std::abs(b.y - y0)) / 4 + 1;
        auto nearLine = [&](const Unit& v) {
            for (int k = 0; k <= steps; ++k)
                if (pointGap(v, x0 + (b.x - x0) * k / steps, y0 + (b.y - y0) * k / steps) <= wd.inner) return true;
            return false;
        };
        b.hitX = b.x;
        b.hitY = b.y;
        spineHits.erase(std::remove_if(spineHits.begin(), spineHits.end(),
                                       [&](const std::array<int, 3>& h) {
                                           return h[0] <= frame - t.rules.lineHitRepeat;
                                       }),
                        spineHits.end());
        for (size_t j = 0; j < units.size(); ++j) {
            const Unit& v = units[j];
            const UnitData& d = t.units[size_t(v.type)];
            if (!v.alive || v.hidden || v.side == b.side || (d.flags & kFlyer)) continue;
            if (std::abs(v.x - b.x) > std::abs(b.x - x0) + 160 || std::abs(v.y - b.y) > std::abs(b.y - y0) + 160 ||
                !nearLine(v))
                continue;
            if (std::any_of(spineHits.begin(), spineHits.end(),
                            [&](const std::array<int, 3>& h) { return h[1] == b.source && h[2] == int(j); }))
                continue;
            spineHits.push_back({frame, b.source, int(j)});
            damage(int(j), b.source, b.weapon, weaponDamage(b.weapon, b.owner), 1);
        }
        return;
    }
    if (b.hit) return;
    b.hit = true;
    int amount = weaponDamage(b.weapon, b.owner);
    if (!splash(b, amount) && !b.missed && b.target >= 0) damage(b.target, b.source, b.weapon, amount, 1);
}

// Splash: rings by the gap from the impact point. False: the weapon has no splash.
bool World::splash(Bullet& b, int amount) {
    const WeaponData& wd = t.weapons[size_t(b.weapon)];
    if (wd.explosion != kRadialSplash && wd.explosion != kEnemySplash && wd.explosion != kAirSplash) return false;
    std::vector<std::pair<int, int>> hits;  // (unit, gap)
    for (int jj : nearUnits(b.x - wd.outer - 160, b.y - wd.outer - 160, b.x + wd.outer + 160, b.y + wd.outer + 160)) {
        size_t j = size_t(jj);
        const Unit& v = units[j];
        if (!v.alive || v.hidden || v.neutral || int(j) == b.source || (b.missed && int(j) == b.target)) continue;
        if (v.side == b.side && wd.explosion != kRadialSplash && int(j) != b.target) continue;
        const UnitData& d = t.units[size_t(v.type)];
        if (!(wd.targetFlags & ((d.flags & kFlyer) ? 1 : 2))) continue;
        int g = pointGap(v, b.x, b.y);
        if (g <= wd.outer) hits.push_back({int(j), g});
    }
    const std::vector<int>& rings = t.rules.splashDivisor;
    auto divisor = [&](int g) { return g <= wd.inner ? rings[0] : g <= wd.medium ? rings[1] : rings[2]; };
    if (wd.explosion != kAirSplash) {
        for (auto [j, g] : hits)  // (a burrowed unit: the inner ring only)
            if (!units[size_t(j)].burrowed || g <= wd.inner) damage(j, b.source, b.weapon, amount, divisor(g));
        return true;
    }
    // Air splash: the target (when within the inner radius) or a random one takes a full hit; then all but the
    // target take a divided hit.
    int pick = -1;
    for (auto [j, g] : hits)
        if (j == b.target && g <= wd.inner) pick = j;
    if (pick < 0 && !hits.empty()) pick = hits[hits.size() == 1 ? 0 : size_t(random()) % hits.size()].first;
    if (pick >= 0) damage(pick, b.source, b.weapon, amount, 1);
    for (auto [j, g] : hits)  // (no Interceptor)
        if (j != b.target && !units[size_t(j)].fighter)
            damage(j, b.source, b.weapon, amount, std::max(rings[1], divisor(g)));
    return true;
}

// The damage pipeline, in fixed point (8 fractional bits). No acid spores, no matrix.
void World::damage(int target, int source, int weapon, int amount, int divisor) {
    Unit& v = units[size_t(target)];
    const UnitData& d = t.units[size_t(v.type)];
    if (!v.alive || v.neutral || (d.flags & kInvincible)) return;  // (the map's own units take no damage)
    const WeaponData& wd = t.weapons[size_t(weapon)];
    int dmg = std::max(amount / divisor, t.rules.minDamage);
    int shieldDamage = 0;
    if (d.hasShields && v.shields >= 256) {
        if (wd.damageType != kDamageIgnoreArmor) {  // (the shield armor upgrade)
            dmg -= level(v.owner, t.rules.shieldArmorUpgrade) * 256;
            if (dmg <= 0) dmg = t.rules.minDamage;
        }
        shieldDamage = std::min(dmg, v.shields);
        dmg -= shieldDamage;
    }
    const UnitRole& r = role(v.type);
    int armor = d.armor + level(v.owner, d.armorUpgrade) +
                (r.armorUpgradeExtra >= 0 && level(v.owner, r.armorUpgradeExtra) ? r.armorExtra : 0);
    if (wd.damageType != kDamageIgnoreArmor) dmg = dmg > armor * 256 ? dmg - armor * 256 : 0;
    dmg = dmg * t.rules.damage(std::min(wd.damageType, 4), std::min(d.size, 3)) / 256;
    if (shieldDamage == 0 && dmg < t.rules.minDamage) dmg = t.rules.minDamage;  // (armor never stops a hit)
    v.damageTaken += shieldDamage + std::min(dmg, v.hp);
    if (shieldDamage + dmg > 0) {  // (damage: the fight goes on, and both may walk again)
        lastProgress = lastDamage = frame;
        v.walked = 0;
        if (source >= 0) units[size_t(source)].walked = 0;
    }
    if (source >= 0 && units[size_t(source)].side != v.side) v.lastAttacker = source;
    if (source >= 0 && units[size_t(source)].side != v.side && dmg < v.hp) {  // (a unit that dies calls nobody)
        // The hit unit and its allies near it take the attacker as their auto-target.
        // (an Interceptor's hit: its Carrier)
        int attacker = units[size_t(source)].fighter ? units[size_t(source)].parent : source;
        const Unit& s = units[size_t(attacker)];
        std::vector<int> near;
        const int help = t.rules.callForHelp;
        forNear(v.x - help, v.y - help, v.x + help, v.y + help, [&](int j) { near.push_back(j); });
        for (int jj : near) {
            Unit& a = units[size_t(jj)];
            if (!a.alive || a.owner != v.owner || std::abs(a.x - v.x) > help || std::abs(a.y - v.y) > help) continue;
            if ((t.units[size_t(a.type)].flags & kWorker) || !canAttack(a, s)) continue;
            bool keep = a.autoTarget >= 0 && canAttack(a, units[size_t(a.autoTarget)]) &&
                        !(inRange(a, s) && !inRange(a, units[size_t(a.autoTarget)]));
            if (!keep) a.autoTarget = attacker;
        }
    }
    if ((shieldDamage > 0 || dmg > 0) && firstDamage[v.side] < 0) firstDamage[v.side] = frame;
    if (dmg >= v.hp) kill(target);
    else v.hp -= dmg;
    v.shields -= shieldDamage;
}

void World::kill(int i, bool credit) {
    Unit& u = units[size_t(i)];
    if (u.alive && !u.hidden && !u.neutral)  // (its death animation, a picture only)
        addEffect(t.flingyImage(t.units[size_t(u.type)].flingy), u.x, u.y, u.owner, u.heading,
                  t.units[size_t(u.type)].elevation, false, kAnimDeath);
    for (Effect& e : effects)  // (its overlays: their own death, else they go)
        if (e.follow == i) {
            e.follow = -1;
            if (t.animOffset(e.anim.header, kAnimDeath)) startAnim(e.anim, kAnimDeath);
            else e.done = true;
        }
    u.alive = false;
    u.hp = 0;
    if (credit && u.lastAttacker >= 0) ++units[size_t(u.lastAttacker)].kills;
}

bool setupCombat(World& w, const CombatOptions& o, std::string& error) {
    const Tables& t = w.t;
    const MapInfo& map = w.map;
    w.combat = true;
    w.allTeams = w.seek = !o.placed.empty();
    int centres[2][2] = {};
    auto validType = [&](int type) {
        // (the static defences fight: the buildings with a weapon; and every unit, also one that cannot attack: it is
        // a target)
        if (type >= 0 && type < int(t.units.size()) &&
            (t.roles[size_t(type)].defence || !(t.units[type].flags & (kBuilding | kResource | kInvincible))))
            return true;
        error = "Unit type " + std::to_string(type) + " cannot fight in the simulation";
        return false;
    };
    // A new unit of a side: Lurkers burrow first (the reference: Burrow at frame 0), Carriers get their Interceptors.
    // The heading at creation: 8 x units.dat's direction; its "random" value: 8 x a random 0 to 31.
    auto face = [&](int type) {
        int d = t.units[size_t(type)].direction;
        return (d == kDirectionRandom ? w.random() & 31 : d) * 8 & 255;
    };
    auto create = [&](int type, int side, int x, int y, int owner = -1) {
        int id = w.add(type, owner >= 0 ? owner : o.players[side], x, y);
        owner = w.units[size_t(id)].owner;
        const UnitRole& r = t.roles[size_t(type)];
        Unit& u = w.units[size_t(id)];
        u.side = side;
        u.heading = u.velDir = face(type);
        if (r.attacksBurrowed) u.burrowing = true;
        if (r.spiderMine) u.timer = t.rules.mineArm;  // (a mine arms first)
        bool more = r.capacityUpgrade >= 0 && w.level(owner, r.capacityUpgrade);
        if (r.launchesScarabs) u.scarabs = more ? r.capacityUpgraded : r.capacity;
        if (r.energyUpgrade >= 0 && w.level(owner, r.energyUpgrade)) u.energy = t.rules.startEnergyUpgraded << 8;
        if (r.launchesInterceptors)
            for (int n = 0; n < (more ? r.capacityUpgraded : r.capacity); ++n) {
                Unit& f = w.units[size_t(w.add(t.interceptorType, owner, x, y))];
                f.heading = f.velDir = face(t.interceptorType);
                f.side = side;
                f.fighter = f.hidden = true;
                f.parent = id;
            }
        int scarabs = w.units[size_t(id)].scarabs;
        for (int n = 0; n < scarabs; ++n) face(t.scarabType);  // (the Scarabs inside: their draws)
        return id;
    };
    // The map's other units stay (as the reference: only the two players' units are removed): its mineral fields and
    // the like stand in the way, and nobody fights them.
    for (const MapInfo::Placed& p : map.units)  // (not with placed units: those are map units themselves)
        if (o.placed.empty() && p.owner != o.players[0] && p.owner != o.players[1] &&
            !t.roles[size_t(p.type)].startLocation) {
            Unit& n = w.units[size_t(w.add(p.type, p.owner, p.x, p.y))];
            n.neutral = true;
            n.side = 99;  // (no team)
        }
    if (!o.placed.empty()) {
        for (const PlacedUnit& p : o.placed) {
            if (p.unitType < 0 || p.unitType >= int(t.units.size())) return validType(-1);
            // (no unit is removed: resources, invincible units, critters, the neutral player's units and the passive
            // ones stay where they are as the map's own units; every other building of a player is a target)
            uint32_t flags = t.units[size_t(p.unitType)].flags;
            bool critter = t.roles[size_t(p.unitType)].critter;
            if (p.passive || p.side < 0 || p.side > 7 || critter || (flags & (kResource | kInvincible))) {
                Unit& n = w.units[size_t(w.add(p.unitType, std::clamp(p.side, 0, int(kNeutralPlayer)), p.x, p.y))];
                n.neutral = true;
                n.side = 99;  // (no team)
                continue;
            }
            int player = std::clamp(p.side, 0, 7);
            int side = o.byPlayer ? std::clamp(o.team[player], 0, 7) : p.side ? 1 : 0;
            int x = p.x, y = p.y;  // (units on one another: the nearest free spot; a building stands where it is)
            if (!(t.units[size_t(p.unitType)].flags & kBuilding)) w.freeSpot(p.unitType, x, y);
            create(p.unitType, side, x, y, o.byPlayer ? player : -1);
        }
    } else {
        std::vector<int> armies[2];
        for (int s = 0; s < 2; ++s)
            for (const Army& a : s ? o.b : o.a) {
                if (!validType(a.unitType)) return false;
                for (int n = 0; n < a.count; ++n) armies[s].push_back(a.unitType);
            }
        // Each side's centre: gap / 2 left and right of the middle, moved to the nearest walkable point. All of a
        // side's units start at its centre and stand at the nearest free spots, in order.
        int cx = o.x >= 0 ? o.x : map.width * 16, cy = o.y >= 0 ? o.y : map.height * 16;
        for (int s = 0; s < 2; ++s) {
            int x = cx + (s ? o.gap / 2 : -o.gap / 2), y = cy;
            if (w.fixedCentres[s][0] >= 0) {
                x = w.fixedCentres[s][0];
                y = w.fixedCentres[s][1];
            }
            for (int ring = 0; ring <= 64 && !map.walkable(x, y); ++ring)
                for (int dy = -ring; dy <= ring; ++dy)
                    for (int dx = -ring; dx <= ring; ++dx)
                        if (std::max(std::abs(dx), std::abs(dy)) == ring && !map.walkable(x, y) &&
                            map.walkable(x + dx * 8, y + dy * 8)) {
                            x += dx * 8;
                            y += dy * 8;
                        }
            centres[s][0] = x;
            centres[s][1] = y;
        }
        for (int s = 0; s < 2; ++s)
            for (int type : armies[s]) {
                int x = centres[s][0], y = centres[s][1];
                w.freeSpot(type, x, y);
                create(type, s, x, y);
            }
    }
    // Both sides attack-move to the other's centre (placed units: to the middle of all their enemies).
    long long sum[8][3] = {};
    for (const Unit& u : w.units) {
        if (u.fighter || u.neutral) continue;
        sum[u.side][0] += u.x;
        sum[u.side][1] += u.y;
        ++sum[u.side][2];
    }
    int sides = 0;
    for (auto& s : sum) sides += s[2] ? 1 : 0;
    if (sides < 2) {
        error = "Each side needs a unit";
        return false;
    }
    for (Unit& u : w.units) {
        if (u.fighter || u.neutral) continue;
        long long ex = 0, ey = 0, en = 0;  // (its enemies' middle)
        for (int s = 0; s < 8; ++s)
            if (s != u.side) ex += sum[s][0], ey += sum[s][1], en += sum[s][2];
        u.amX = o.placed.empty() ? centres[1 - u.side][0] : int(ex / en);
        u.amY = o.placed.empty() ? centres[1 - u.side][1] : int(ey / en);
        w.setOrder(u, u.building && !w.hasWeapon(u) ? Idle : AttackMove);  // (a building that cannot attack: a target)
        u.timer = 0;
    }
    return true;
}

bool World::reaches(const Unit& a, const Unit& v) const {
    if ((t.units[size_t(a.type)].flags & kFlyer) || (t.units[size_t(v.type)].flags & kFlyer) || map.tileRegion.empty())
        return true;  // (a flyer, or one it shoots in the air: within reach of its range)
    auto group = [&](const Unit& x) {
        int tx = std::clamp(x.x / 32, 0, map.width - 1), ty = std::clamp(x.y / 32, 0, map.height - 1);
        return map.regions[size_t(map.tileRegion[size_t(ty * map.width + tx)])].group;
    };
    int ga = group(a), gv = group(v);
    return !ga || !gv || ga == gv;
}

int World::nearestEnemy(const Unit& a) const {
    int best = -1, bestDist = 0;
    for (size_t j = 0; j < units.size(); ++j) {
        const Unit& v = units[j];
        if (!v.alive || v.hidden || v.neutral || v.fighter || v.side == a.side || !couldHit(a, v) ||
            !detected(v, a.side) || !reaches(a, v))
            continue;
        int d = octagonalLength(v.x - a.x, v.y - a.y);
        if (best < 0 || d < bestDist) {
            best = int(j);
            bestDist = d;
        }
    }
    return best;
}

// Progress in a fight of placed units: a unit nearer than ever (by 8 pixels) to the nearest enemy it can attack and
// get at, or with a shorter way left than ever along its path to its move target (a long way round, first away from
// the enemy; at most the map's width and height of such walking until it hits or is hit). Each unit looks once in
// 24 frames. When that enemy dies, the distance to the next one is the new start; a new move target (more than 64
// pixels away) starts its way again.
void World::trackProgress() {
    for (size_t i = 0; i < units.size(); ++i) {
        Unit& u = units[i];
        if ((frame + int(i)) % 24 || !u.alive || u.hidden || u.neutral || u.fighter || u.building || !hasWeapon(u))
            continue;
        int e = nearestEnemy(u);
        if (e < 0) continue;
        int d = octagonalLength(units[size_t(e)].x - u.x, units[size_t(e)].y - u.y);
        if (u.progressTarget < 0 || !units[size_t(u.progressTarget)].alive) {
            u.progressBest = d;
        } else if (d + 8 <= u.progressBest) {
            u.progressBest = d;
            lastProgress = frame;
        }
        u.progressTarget = e;
        if (u.path.empty() || u.unreached) continue;
        int left = octagonalLength(u.path.front().first - u.x, u.path.front().second - u.y);
        for (size_t k = 1; k < u.path.size(); ++k)
            left += octagonalLength(u.path[k].first - u.path[k - 1].first, u.path[k].second - u.path[k - 1].second);
        left += octagonalLength(u.moveX - u.path.back().first, u.moveY - u.path.back().second);  // (a path in parts)
        if (u.walkGoalX < 0 || std::abs(u.moveX - u.walkGoalX) > 64 || std::abs(u.moveY - u.walkGoalY) > 64) {
            u.walkGoalX = u.moveX;
            u.walkGoalY = u.moveY;
            u.walkBest = left;
        } else if (left + 8 <= u.walkBest) {
            // (at most the map's width and height walked so: a unit that goes from one enemy to another for ever
            // does not keep the fight going)
            u.walked += u.walkBest - left;
            u.walkBest = left;
            if (u.walked <= (map.width + map.height) * 32) lastProgress = frame;
        }
    }
}

bool combatOver(World& w, int lastFrame) {
    int alive[8] = {}, attackers[8] = {}, teams = 0, attacking = 0;
    for (const Unit& u : w.units)
        if (u.alive && !u.fighter && !u.neutral) {
            ++alive[u.side];
            if (w.hasWeapon(u)) ++attackers[u.side];
        }
    for (int s = 0; s < 8; ++s) {
        teams += alive[s] ? 1 : 0;
        attacking += alive[s] && attackers[s] ? 1 : 0;
    }
    if (teams <= 1) return true;
    if (w.allTeams) {  // (all players: it goes on while some team can attack an enemy)
        // (no damage and no unit nearer its enemy for 30 s; or no damage at all for 2 minutes)
        if (w.frame - w.lastProgress >= kStalemateFrames || w.frame - w.lastDamage >= 4 * kStalemateFrames) {
            w.stalemate = true;
            return true;
        }
        for (const Unit& a : w.units) {
            if (!a.alive || a.fighter || a.neutral || !w.hasWeapon(a)) continue;
            for (const Unit& v : w.units)
                if (v.alive && !v.neutral && !v.fighter && v.side != a.side && w.couldHit(a, v) &&
                    (!w.seek || (w.detected(v, a.side) && w.reaches(a, v)))) {
                    if (w.frame >= lastFrame) break;
                    return false;
                }
        }
        if (w.frame >= lastFrame) w.timedOut = true;
        return true;
    }
    if (teams == 2)  // (two sides: one that cannot attack ends it)
        for (int s = 0; s < 8; ++s)
            if (alive[s] && !attackers[s]) {
                w.helpless = s;
                return true;
            }
    if (!attacking) return true;  // (no team can attack)
    if (w.frame >= lastFrame) {
        w.timedOut = true;
        return true;
    }
    return false;
}

CombatResult combatResultOf(const World& w) {
    CombatResult r;
    r.frames = w.frame;
    r.timedOut = w.timedOut;
    r.stalemate = w.stalemate;
    r.helpless = w.helpless;
    auto whole = [](int v) { return (v + 255) / 256; };
    for (const Unit& u : w.units) {
        if (u.fighter || u.neutral) continue;  // (Interceptors and the map's own units are not units of a side)
        auto team = std::find_if(r.teams.begin(), r.teams.end(), [&](const CombatSide& s) { return s.team == u.side; });
        if (team == r.teams.end()) {
            r.teams.push_back(CombatSide());
            r.teams.back().team = u.side;
            team = r.teams.end() - 1;
        }
        if (std::find(team->players.begin(), team->players.end(), u.owner) == team->players.end())
            team->players.push_back(u.owner);
        for (CombatSide* side : {&*team, u.side == 0 ? &r.a : u.side == 1 ? &r.b : nullptr}) {
            if (!side) continue;
            CombatSide& c = *side;
            const UnitData& d = w.t.units[u.type];
            bool half = w.t.roles[size_t(u.type)].halfCost;  // (two from one egg: each costs half)
            int minerals = half ? d.minerals / 2 : d.minerals, gas = half ? d.gas / 2 : d.gas;
            int maxHp = d.hp / 256, maxShields = d.hasShields ? d.shields : 0;
            auto it = std::find_if(c.types.begin(), c.types.end(), [&](auto& s) { return s.unitType == u.type; });
            if (it == c.types.end()) {
                UnitTypeStats s;
                s.unitType = u.type;
                s.mineralCost = minerals;
                s.gasCost = gas;
                s.supply = d.supply;
                c.types.push_back(s);
                it = c.types.end() - 1;
            }
            ++it->started;
            it->maxHp += maxHp;
            it->maxShields += maxShields;
            it->kills += u.kills;
            ++c.units;
            c.kills += u.kills;
            c.startHp += maxHp;
            c.startShields += maxShields;
            c.damageTaken += u.damageTaken;
            c.mineralsAll += minerals;
            c.gasAll += gas;
            c.supplyAll += d.supply;
            if (u.alive) {
                ++it->alive;
                it->hp += whole(u.hp);
                it->shields += whole(u.shields);
                ++c.alive;
                c.hp += whole(u.hp);
                c.shields += whole(u.shields);
                c.maxHp += maxHp;
                c.maxShields += maxShields;
            } else {
                c.mineralsLost += minerals;
                c.gasLost += gas;
                c.supplyLost += d.supply;
            }
        }
    }
    std::vector<CombatSide*> all = {&r.a, &r.b};
    for (CombatSide& c : r.teams) all.push_back(&c);
    int left = 0;
    for (CombatSide* c : all) {
        c->damageTaken = (c->damageTaken + 128) / 256;
        for (const UnitTypeStats& s : c->types)
            if (s.alive) c->survivors.push_back({s.unitType, s.alive});
    }
    for (const CombatSide& c : r.teams)
        if (c.alive) {
            ++left;
            r.winner = c.team;
        }
    if (left != 1 || r.timedOut || r.stalemate) r.winner = -1;
    for (const Unit& v : w.units) {  // (the units alive that no enemy could reach or attack)
        if (!v.alive || v.neutral || v.fighter) continue;
        bool got = false;
        for (const Unit& a : w.units)
            if (a.alive && !a.neutral && !a.fighter && a.side != v.side && w.hasWeapon(a) && w.couldHit(a, v) &&
                w.detected(v, a.side) && w.reaches(a, v)) {
                got = true;
                break;
            }
        if (got) continue;
        for (CombatSide& c : r.teams)
            if (c.team == v.side) ++c.unreachable;
        if (v.side == 0) ++r.a.unreachable;
        if (v.side == 1) ++r.b.unreachable;
    }
    if (w.helpless >= 0 && r.teams.size() == 2)  // (two sides: the other one wins)
        r.winner = r.teams[0].team == w.helpless ? r.teams[1].team : r.teams[0].team;
    return r;
}

}  // namespace sim
