// The game's rules in one place: what the simulation knows of the game beyond its data files. Each unit type's role
// comes from units.dat where the file says it (flags, groups, weapons, subunits), and from the table in rules.cpp
// only where no file says it. The numbers below are the game's timings, distances and amounts; the engine's own
// numbers (fixed point, grid cells, search budgets) stay in the engine.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace sim {

struct Tables;

// A unit type's role. "data": from units.dat; "table": from rules.cpp (no file says it).
struct UnitRole {
    int race = -1;              // data: 0 Zerg, 1 Terran, 2 Protoss (StarEdit group flags); -1: none
    bool defence = false;       // data: a building with a weapon of its own (Missile Turret, Spore and Sunken Colony,
                                // Photon Cannon)
    bool critter = false;       // data: a neutral unit that is not a building and has no weapon
    bool lowPriority = false;   // data: a race's unit that is neither a man nor a building (larva, egg): targeted last
    bool halfCost = false;      // data: two units from one egg (Zergling, Scourge): each costs half
    bool launchesScarabs = false;       // data: its attack order is the Reaver's (Reaver, Warbringer)
    bool launchesInterceptors = false;  // data: its attack order is the Carrier's (Carrier, Gantrithor)
    bool scarab = false;                // data: the Scarab's own order (its weapon is the Reaver's)
    bool interceptor = false;           // data: the Interceptor's own order (its weapon is the Carrier's)
    bool spiderMine = false;            // data: its idle order is the mine's (arms, burrows, chases, explodes)
    bool attacksBurrowed = false;       // table: the Lurker (burrows at once, attacks only burrowed, no turning)
    bool healer = false;                // data: its attack order is the heal (the Medic)
    bool ordersEveryFrame = false;      // table: the Zergling (its order runs every frame while it can attack)
    bool startLocation = false;         // data: a neutral depot with no resources (the start location marker)
    bool mineralField = false;          // data: a neutral resource container, not a geyser, not in the buildings group
    bool geyser = false;                // data: a neutral resource container of a race refinery's size
    int startDelay = 0;                 // table: frames before its first order (the Archon)
    int capacity = 0;                   // table: Scarabs or Interceptors it holds
    int gasImage = -1;                  // table: a worker's image for carried gas (images.dat)
};
// A weapon's role.
struct WeaponRole {
    bool lineHits = false;  // data: weapons.dat behaviour "go to max range": it hits along its line (Lurker spines)
};

void deriveRoles(Tables& t);  // (rules.cpp: fills t.roles and t.weaponRoles from the data and the table)

namespace rules {



// Orders and targets.
constexpr int kOrderTick = 9;                // an order runs every 9 frames
constexpr int kTargetSearchFrames = 15;      // attack move and guard: a look for targets every 15 frames
constexpr int kPriorityNoAttackArmed = 2;    // target priorities: 0 best; a unit that cannot hit the attacker but
constexpr int kPriorityWorker = 2;           // can hit something 2; a worker 2; one that can move 3; a building 4;
constexpr int kPriorityMover = 3;            // larvae and eggs 5
constexpr int kPriorityBuilding = 4;
constexpr int kPriorityLow = 5;
constexpr int kSameWayTolerance = 32;        // (direction steps) a target moving the same way: no moving margin
constexpr int kCallForHelp = 96;             // allies within 96 pixels of a hit unit take its attacker as target

// Attacks.
constexpr int kBounces = 3;                  // a bouncing glaive: 3 hits, damage / 1, / 3, / 9
constexpr int kBounceDivisor[3] = {1, 3, 9};
constexpr int kBounceSearch = 96;            // its next target: within 96 pixels
constexpr int kLaunchWaitMax = 10;           // a projectile that gets no launch signal flies after 10 frames
constexpr int kPersistHitFrames = 7;         // damage over time: a hit every 7 frames
constexpr int kLineHitRepeat = 32;           // a line hit hits each target once in 32 frames
// Damage factor (of 256) by damage type (independent, explosive, concussive, normal, ignore armor) and unit size
// (independent, small, medium, large).
constexpr int kDamageFactor[5][4] = {{0, 0, 0, 0}, {0, 128, 192, 256}, {0, 256, 128, 64}, {256, 256, 256, 256},
                                     {256, 256, 256, 256}};
constexpr int kDamageTypeIgnoreArmor = 4;

// Regeneration, energy (8 fractional bits a frame).
constexpr int kRegenHp = 4, kRegenShields = 7, kRegenEnergy = 8;
constexpr int kMaxEnergy = 200;

// Movement.
constexpr int kRandomDirection = 32;         // units.dat direction 32: a random heading at creation
// The collision reaction: the block counter's limits.
constexpr int kStuckRetry = 10, kStuckRetryEnd = 20, kStuckGiveUp = 30, kStuckYield = 25, kStuckHold = 50;
constexpr int kStuckFresh = 2;               // a blocker that has been blocked at most 2 times: one waits for it
constexpr int kStraightReach = 192;          // a straight walk in a fight goes at most 192 pixels along its axis
constexpr int kPathAgeForRepath = 7;         // a new path waits until the old one is 7 frames old

// Medic.
constexpr int kHealSearch = 160, kHealGap = 30, kHealMax = 200;  // (heal: 8 fractional bits; costs half in energy)

// Archon, Reaver and Scarab.
constexpr int kScarabForward = 25;           // a Scarab is let out 25 pixels ahead of its Reaver
constexpr int kScarabLock = 60;              // the next Scarab 60 frames later
constexpr int kScarabStand = 5;              // it moves from its 5th frame (after 3 frames at rest)
constexpr int kScarabSpeed = 16;             // pixels a frame
constexpr int kScarabArmFrames = 3;          // it can explode from its 3rd frame
constexpr int kScarabBlastDelay = 4;         // its blast lands 4 frames after it explodes
constexpr int kScarabLife = 90;

// Carrier and Interceptor.
constexpr int kInterceptorLaunch = 7;        // one Interceptor out every 7 frames
constexpr int kInterceptorWait = 15;         // a launched Interceptor waits 15 frames before it attacks
constexpr int kInterceptorLeash = 64;        // it returns beyond its Carrier's range + 64 pixels
constexpr int kInterceptorNear = 50, kInterceptorDock = 10;
constexpr int kInterceptorLowShields = 4;   // it returns with less than a quarter of its shields
constexpr int kInterceptorSpread[3] = {3, 1, 2};  // its random points around the target: 3, 1, then 2 x -127..128

// Spider Mine.
constexpr int kMineArm = 63;                 // it arms 60 frames, then 3
constexpr int kMineBurrow = 9, kMineUnburrow = 18, kMineFuse = 5, kMineFuseStill = 9;
constexpr int kMineChase = 576, kMineFuseRange = 30;
constexpr int kMineBlastDelay = 6;

// Mining.
constexpr int kMiningFrames = 75;            // a worker mines minerals 75 frames
constexpr int kGasFrames = 37;               // and is inside a refinery 37 frames
constexpr int kMiningReach = 10;             // it mines within 10 pixels of the field
constexpr int kMineralLoad = 8;             // minerals carried in one trip (and gas)
constexpr int kMineralImage = 397;           // images.dat: the carried mineral chunk (+1: the small one)
constexpr int kMineralFull = 750, kMineralHalf = 500, kMineralLow = 250;  // a field's picture by its amount
constexpr int kBaseSearchTiles = 14, kBaseResourceGap = 96, kFieldRange = 384, kClusterGap = 192;
constexpr int kResourceNearBase = 640, kDepotNear = 64;

}  // namespace rules
}  // namespace sim
