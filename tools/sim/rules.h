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
    bool launchesScarabs = false;       // table: Reaver, Warbringer
    bool launchesInterceptors = false;  // table: Carrier, Gantrithor
    bool scarab = false;                // table: the Scarab (its weapon is the Reaver's)
    bool interceptor = false;           // table: the Interceptor (its weapon is the Carrier's)
    bool spiderMine = false;            // table: the Spider Mine (arms, burrows, chases, explodes)
    bool attacksBurrowed = false;       // table: the Lurker (burrows at once, attacks only burrowed, no turning)
    bool healer = false;                // table: the Medic (heals instead of attacking)
    bool ordersEveryFrame = false;      // table: the Zergling (its order runs every frame while it can attack)
    bool notDetector = false;           // table: the Overlord (units.dat calls it a detector; it does not detect)
    bool startLocation = false;         // table: the start location marker
    bool mineralField = false;          // table: mineral fields (units.dat does not tell minerals from gas)
    bool geyser = false;                // table: the Vespene Geyser
    int startDelay = 0;                 // table: frames before its first order (the Archon)
    int speedUpgrade = -1;              // table: the upgrade that raises its speed (kAlways: always raised)
    int rangeUpgrade = -1, rangeBonus = 0;  // table: the upgrade that raises its weapon range, and by how much
    bool rangeAirOnly = false;              // table: only its air weapon (the Goliath's Charon Boosters)
    int cooldownUpgrade = -1;           // table: the upgrade that halves its cooldown (Adrenal Glands)
    int armorUpgradeExtra = -1, armorExtra = 0;  // table: an upgrade with extra armor (Chitinous Plating: +2)
    int capacityUpgrade = -1, capacity = 0, capacityUpgraded = 0;  // table: Scarabs or Interceptors it holds
    int energyUpgrade = -1;             // table: the upgrade that raises its maximum energy (Caduceus Reactor)
    int gasImage = -1;                  // table: a worker's image for carried gas (images.dat)
};
// A weapon's role.
struct WeaponRole {
    bool lineHits = false;  // data: weapons.dat behaviour "go to max range": it hits along its line (Lurker spines)
    int activeLimit = 0;    // table: at most this many of its projectiles at once (Halo Rockets: 80); 0: no limit
};

void deriveRoles(Tables& t);  // (rules.cpp: fills t.roles and t.weaponRoles from the data and the table)

namespace rules {

constexpr int kAlways = 1000;  // (a UnitRole upgrade that needs no upgrade)

// Upgrades (upgrades.dat numbers) with an effect on all of a player's units of a kind.
constexpr int kShieldArmorUpgrade = 15;  // Plasma Shields: shield armor

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
constexpr int kBuildingDetectRange = 7 * 32;  // a detector building sees cloaked units 7 tiles away

// Attacks.
constexpr int kCooldownMin = 5, kCooldownMax = 250;  // the cooldown's range, then a random -1 to +2
constexpr int kSuicideDeathDelay = 3;        // a suicide unit is gone 3 frames after it fires
constexpr int kMissOffset = 30;              // a missed projectile lands 30 pixels short
constexpr int kMaxRangeExtra = 20;           // "go to max range" projectiles fly 20 pixels past the range
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
constexpr int kMinDamage = 128;              // at least half a point (8 fractional bits) when no shields took it

// Regeneration, energy (8 fractional bits a frame).
constexpr int kRegenHp = 4, kRegenShields = 7, kRegenEnergy = 8;
constexpr int kStartEnergy = 50, kMaxEnergy = 200, kMaxEnergyUpgraded = 250;
constexpr int kStartEnergyUpgraded = 62;     // a Medic made with Caduceus Reactor (a quarter of 250)

// Movement.
constexpr int kSpeedUpgradeMin = 853;        // a raised speed: +1/2, but at least 3 1/3 pixels a frame
constexpr int kStopSpeed = 192;              // a braking unit stops below 0.75 pixels a frame
constexpr int kBurrowFacing = 96;            // a unit turns to this heading to burrow
constexpr int kRandomDirection = 32;         // units.dat direction 32: a random heading at creation
constexpr int kTurnFramesNear = 32;          // (pixels) the frames-to-turn test applies within 32 pixels
// The collision reaction: the block counter's limits.
constexpr int kStuckRetry = 10, kStuckRetryEnd = 20, kStuckGiveUp = 30, kStuckYield = 25, kStuckHold = 50;
constexpr int kStuckFresh = 2;               // a blocker that has been blocked at most 2 times: one waits for it
constexpr int kStraightReach = 192;          // a straight walk in a fight goes at most 192 pixels along its axis
constexpr int kPathAgeForRepath = 7;         // a new path waits until the old one is 7 frames old
// Flying repulse.
constexpr int kRepulseCell = 48, kRepulseFrames = 7, kRepulseOffset = 32, kRepulseStop = 24;

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
