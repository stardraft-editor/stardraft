// The combat orders: one function by order and kind of unit (Spider Mine, Burrow, Interceptor, Medic, guard, Attack
// Move, Attack Unit with its Interceptor and Scarab launches), and the spot a unit walks to for its target.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "world.h"

namespace sim {

namespace {

const int kSeekIdleEvery = 3, kSeekEvery = 24;  // (our own search and destroy: how often a unit looks again)

}  // namespace

void World::combatOrder(int i) {
    Unit& u = units[size_t(i)];
    if (u.dieAt >= 0 || u.neutral) return;  // (the map's own units do not fight)
    if (frame < role(u.type).startDelay) return;  // (an Archon starts late)
    if (u.building) {  // (a defence looks for a target every frame)
        u.poll = 0;
        if (u.order != AttackUnit) u.timer = 0;
    }
    if (role(u.type).healer) return medic(i);
    if (role(u.type).spiderMine) return spiderMineOrder(i);
    if (u.burrowing) return burrowOrder(i);
    if (u.fighter) return interceptorOrder(i);
    // (it cannot attack: it stands, a target only)
    if (!hasWeapon(u) && !role(u.type).launchesInterceptors && !role(u.type).launchesScarabs) {
        if (u.x != u.moveX || u.y != u.moveY) stopMove(u);
        return;
    }
    // Attack Move takes an auto-target at once, every frame.
    if (u.order == AttackMove && u.autoTarget >= 0 && canAttack(u, units[size_t(u.autoTarget)])) {
        stopMove(u);
        u.target = u.autoTarget;
        u.next = AttackMove;
        setOrder(u, AttackUnit);
        return;
    }
    // The order tick: every 9 frames; a Zergling's every frame while it can attack.
    if (role(u.type).ordersEveryFrame && u.cooldown == 0) u.poll = 0;
    if (!tick(u)) return;
    switch (u.order) {
    case Idle: return guardOrder(i);
    case AttackMove: return attackMoveOrder(i);
    case AttackUnit: return attackUnitOrder(i);
    default: return;
    }
}

// A Spider Mine, every frame: its phases in u.state.
void World::spiderMineOrder(int i) {
    Unit& u = units[size_t(i)];
    const UnitData& d = t.units[size_t(u.type)];
    Unit* target = u.target >= 0 && canAttack(u, units[size_t(u.target)]) ? &units[size_t(u.target)] : nullptr;
    int dist = target ? centreDistance(u.x, u.y, target->x, target->y) : 0;
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
                (f & (kFlyer | kInvincible)) || !withinSquare(v.x, v.y, u.x, u.y, reach) ||
                !detected(v, u.side))
                continue;
            int n = centreDistance(u.x, u.y, v.x, v.y);
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

// Burrow, on the order tick: stop, then its script.
void World::burrowOrder(int i) {
    Unit& u = units[size_t(i)];
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
}

// An Interceptor, on its order tick.
void World::interceptorOrder(int i) {
    Unit& u = units[size_t(i)];
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
        return exactDistance(u.moveX, u.moveY, u.x, u.y) <= t.rules.interceptorNear;
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
}

// Guard (Idle): a look for targets every 15 frames.
void World::guardOrder(int i) {
    Unit& u = units[size_t(i)];
    auto valid = [&](int j) { return j >= 0 && canAttack(u, units[size_t(j)]); };
    auto attack = [&](int target, Order after) {
        u.target = target;
        u.next = after;
        setOrder(u, AttackUnit);
    };
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
}

// Attack Move: the nearest target in range on its look; else on to its goal (search and destroy: the nearest enemy).
void World::attackMoveOrder(int i) {
    Unit& u = units[size_t(i)];
    auto attack = [&](int target, Order after) {
        u.target = target;
        u.next = after;
        setOrder(u, AttackUnit);
    };
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
}

// Attack Unit: keep or change the target, walk into range, then attack.
void World::attackUnitOrder(int i) {
    Unit& u = units[size_t(i)];
    auto valid = [&](int j) { return j >= 0 && canAttack(u, units[size_t(j)]); };
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
            if (u.side < 8 && firstShot[u.side] < 0) firstShot[u.side] = frame;
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
        launchInterceptor(i, current);
        return;
    }
    if (role(u.type).launchesScarabs) {  // (one Scarab out, a time apart, facing it)
        launchScarab(i, current);
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
}

// A Carrier's attack: the Interceptors out take its target; one more goes out when its timer is 0.
void World::launchInterceptor(int i, int current) {
    Unit& u = units[size_t(i)];
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
        regrid(int(&f - units.data()));
        f.shields = t.units[size_t(f.type)].shields << 8;
        f.target = current;
        setOrder(f, AttackUnit);
        f.state = 0;
        u.timer = t.rules.interceptorLaunch;
    }
}

// A Reaver's attack: one Scarab out, a time apart, facing the target.
void World::launchScarab(int i, int current) {
    Unit& u = units[size_t(i)];
    const Unit& target = units[size_t(current)];
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
    if (u.side < 8 && firstShot[u.side] < 0) firstShot[u.side] = frame;
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
        for (int j : nearUnits(square(m.x, m.y, t.rules.healSearch))) {
            const Unit& v = units[size_t(j)];
            if (!ok(j) || v.healedFrame == frame || !withinSquare(v.x, v.y, m.x, m.y, t.rules.healSearch)) continue;
            double d = exactDistance(v.x, v.y, m.x, m.y);
            if (m.healTarget < 0 || d < best) {
                m.healTarget = j;
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
    // (the units that a spot on the outline could touch)
    forNear(Box{l - d.left, top - d.up, r + d.right, bottom + d.down}, [&](int j) { nearby.push_back(j); });
    auto taken = [&](int px, int py) {
        if (!map.boxWalkable(px - d.left, py - d.up, px + d.right, py + d.down)) return true;
        for (int jj : nearby) {
            const Unit& o = units[size_t(jj)];
            if (&o == &u || &o == &target || !o.alive || o.hidden || o.burrowed) continue;
            const UnitData& od = t.units[size_t(o.type)];
            if ((od.flags & kFlyer) || o.x != o.moveX || o.y != o.moveY) continue;
            if (overlaps(u.type, px, py, o))
                return true;
        }
        return false;
    };
    double best = -1;
    auto consider = [&](int px, int py) {
        double dist = exactDistance(px, py, u.x, u.y);
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

}  // namespace sim
