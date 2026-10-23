// The game's rules: each unit type's role, from the game's data files where they say it, and the values that no data
// file holds, read from tools/sim/rules.ini (built into the program; a project can give its own "sim/rules.ini").
// The code holds no game values: only the names of the file's values. The engine's own numbers (fixed point, grid
// cells, search budgets) stay in the engine.
#pragma once

#include <string>
#include <vector>

namespace sim {

struct Tables;

// The values of rules.ini, by the names it uses.
struct GameRules {
    // orders.dat numbers of the game's own orders
    int orderReaverAttack = 0, orderCarrierAttack = 0, orderInterceptor = 0, orderScarab = 0, orderMine = 0;
    int orderHeal = 0;
    // units.dat numbers and unit values
    int scarabUnit = 0, interceptorUnit = 0, scarabsHeld = 0, interceptorsHeld = 0;
    std::vector<int> attacksBurrowed, ordersEveryFrame, startDelay, gasImage;  // (startDelay, gasImage: unit, value)
    // orders and targets
    int orderTick = 0, targetSearchFrames = 0, priorityNoAttackArmed = 0, priorityWorker = 0, priorityMover = 0;
    int priorityBuilding = 0, priorityLow = 0, sameWayTolerance = 0, callForHelp = 0;
    // attacks
    int bounces = 0, bounceSearch = 0, launchWaitMax = 0, persistHitFrames = 0, lineHitRepeat = 0;
    int scarabDetonate = 0, velocityTurnDivisor = 0;
    std::vector<int> bounceDivisor, damageFactor;  // (damageFactor: 5 damage types x 4 sizes)
    std::vector<int> splashDivisor, cooldownJitter;  // (splash: inner, medium, outer; jitter: values, first)
    // regeneration and energy
    int regenHp = 0, regenShields = 0, regenEnergy = 0, maxEnergy = 0;
    // movement
    int stuckRetry = 0, stuckRetryEnd = 0, stuckGiveUp = 0, stuckYield = 0, stuckHold = 0, stuckFresh = 0;
    int straightReach = 0, pathAgeForRepath = 0, waypointPass = 0;
    // Medic
    int healSearch = 0, healGap = 0, healMax = 0, healEnergyDivisor = 0;
    // Reaver and Scarab
    int scarabForward = 0, scarabLock = 0, scarabStand = 0, scarabSpeed = 0, scarabArmFrames = 0;
    int scarabBlastDelay = 0, scarabLife = 0;
    // Carrier and Interceptor
    int interceptorLaunch = 0, interceptorWait = 0, interceptorLeash = 0, interceptorNear = 0, interceptorDock = 0;
    int interceptorLowShields = 0, interceptorSpreadRange = 0, interceptorLaunchHp = 0;
    std::vector<int> interceptorSpread;
    // Spider Mine
    int mineArm = 0, mineBurrow = 0, mineUnburrow = 0, mineFuse = 0, mineFuseStill = 0, mineChase = 0;
    int mineFuseRange = 0, mineBlastDelay = 0;
    // mining
    int miningFrames = 0, gasFrames = 0, miningReach = 0, mineralLoad = 0, mineralImage = 0, mineralFull = 0;
    int mineralHalf = 0, mineralLow = 0, baseSearchTiles = 0, baseResourceGap = 0, fieldRange = 0, clusterGap = 0;
    int resourceNearBase = 0, depotNear = 0, frameMs = 0, startMinerals = 0, gasWorkersMax = 0;

    int damage(int type, int size) const { return damageFactor[size_t(type * 4 + size)]; }
};

// Reads rules.ini's text; false (with the line) when a value is missing, unknown or not a number.
bool readRules(const std::string& text, GameRules& rules, std::string& error);
const char* defaultRules();  // (rules.ini as built into the program)

// A unit type's role. "data": from units.dat; "file": from rules.ini.
struct UnitRole {
    int race = -1;              // data: 0 Zerg, 1 Terran, 2 Protoss (StarEdit group flags); -1: none
    bool defence = false;       // data: a building with a weapon of its own
    bool critter = false;       // data: a neutral unit that is not a building and has no weapon
    bool lowPriority = false;   // data: a race's unit that is neither a man nor a building (larva, egg): targeted last
    bool halfCost = false;      // data: two units from one egg (Zergling, Scourge): each costs half
    bool launchesScarabs = false;       // data: its attack order is the Reaver's
    bool launchesInterceptors = false;  // data: its attack order is the Carrier's
    bool scarab = false;                // data: the Scarab's own order
    bool interceptor = false;           // data: the Interceptor's own order
    bool spiderMine = false;            // data: its idle order is the mine's
    bool healer = false;                // data: its attack order is the heal
    bool startLocation = false;         // data: a neutral depot with no resources
    bool mineralField = false;          // data: a neutral resource container, not a geyser, not in the buildings group
    bool geyser = false;                // data: a neutral resource container of a race refinery's size
    bool attacksBurrowed = false;       // file: burrows at once, attacks only burrowed, no turning (the Lurker)
    bool ordersEveryFrame = false;      // file: its order runs every frame while it can attack (the Zergling)
    int startDelay = 0;                 // file: frames before its first order (the Archon)
    int capacity = 0;                   // file: Scarabs or Interceptors it holds
    int gasImage = -1;                  // file: a worker's image for carried gas (images.dat)
};
// A weapon's role.
struct WeaponRole {
    bool lineHits = false;  // data: weapons.dat behaviour "go to max range": it hits along its line (Lurker spines)
};

void deriveRoles(Tables& t);  // (rules.cpp: fills t.roles and t.weaponRoles from the data and t.rules)

}  // namespace sim
