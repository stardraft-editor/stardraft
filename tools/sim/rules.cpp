// The unit and weapon roles: from units.dat and weapons.dat where they say it, and from the table below where no file
// does (each entry with its reason).
#include "rules.h"

#include "world.h"

namespace sim {
namespace {

// units.dat ids of the table below.
enum : int {
    kMarine = 0, kGoliath = 3, kVulture = 2, kSpiderMine = 13, kMedic = 34, kZergling = 37, kHydralisk = 38,
    kUltralisk = 39, kDrone = 41, kOverlord = 42, kProbe = 64, kZealot = 65, kDragoon = 66, kArchon = 68, kShuttle = 69,
    kScout = 70, kCarrier = 72, kInterceptor = 73, kWarbringer = 81, kGantrithor = 82, kReaver = 83, kObserver = 84,
    kScarab = 85, kLurker = 103, kMineralsFirst = 176, kMineralsLast = 178, kGeyser = 188, kStartLocation = 214,
    kScv = 7,
};
// upgrades.dat ids.
enum : int {
    kU238Shells = 16, kIonThrusters = 17, kPneumatizedCarapace = 26, kMetabolicBoost = 27, kAdrenalGlands = 28,
    kMuscularAugments = 29, kGroovedSpines = 30, kSingularityCharge = 33, kLegEnhancements = 34, kReaverCapacity = 36,
    kGraviticDrive = 37, kGraviticBoosters = 39, kGraviticThrusters = 42, kCarrierCapacity = 43, kCaduceusReactor = 51,
    kChitinousPlating = 52, kAnabolicSynthesis = 53, kCharonBoosters = 54,
};
// weapons.dat ids.
enum : int { kHaloRockets = 103 };
// weapons.dat behaviour "go to max range".
enum : int { kBehaviourMaxRange = 9 };

}  // namespace

void deriveRoles(Tables& t) {
    t.roles.assign(t.units.size(), UnitRole());
    for (size_t i = 0; i < t.units.size(); ++i) {
        const UnitData& d = t.units[i];
        UnitRole& r = t.roles[i];
        r.race = d.group & kGroupZerg ? 0 : d.group & kGroupTerran ? 1 : d.group & kGroupProtoss ? 2 : -1;
        bool weapon = d.groundWeapon != kNoWeapon || d.airWeapon != kNoWeapon;
        r.defence = (d.flags & kBuilding) && weapon;
        r.critter = (d.group & kGroupNeutral) && !(d.group & kGroupBuilding) && !(d.flags & kBuilding) && !weapon;
        r.lowPriority = r.race >= 0 && !(d.group & (kGroupMen | kGroupBuilding)) && !(d.flags & kBuilding);
        r.halfCost = (d.flags & kTwoInOneEgg) != 0;
    }
    auto role = [&](int type) -> UnitRole& { return t.roles[size_t(type)]; };
    // (no file says which units hold Scarabs or Interceptors, nor how many)
    for (int type : {kReaver, kWarbringer}) {
        role(type).launchesScarabs = true;
        role(type).capacityUpgrade = kReaverCapacity;
        role(type).capacity = 5;
        role(type).capacityUpgraded = 10;
    }
    for (int type : {kCarrier, kGantrithor}) {
        role(type).launchesInterceptors = true;
        role(type).capacityUpgrade = kCarrierCapacity;
        role(type).capacity = 4;
        role(type).capacityUpgraded = 8;
    }
    role(kScarab).scarab = true;            // (the Reaver's weapon is the Scarab's)
    role(kInterceptor).interceptor = true;  // (the Carrier's weapon is the Interceptor's)
    t.scarabType = kScarab;
    t.interceptorType = kInterceptor;
    role(kSpiderMine).spiderMine = true;    // (its arming, burrow, chase and fuse are its own)
    role(kLurker).attacksBurrowed = true;   // (it has its weapon only while burrowed)
    role(kLurker).speedUpgrade = rules::kAlways;  // (it moves as if upgraded)
    role(kMedic).healer = true;             // (healing is a tech, not a weapon)
    role(kZergling).ordersEveryFrame = true;  // (a rule of the game's for the Zergling)
    role(kMedic).energyUpgrade = kCaduceusReactor;
    role(kOverlord).notDetector = true;     // (units.dat flags it as a detector; it does not detect)
    role(kStartLocation).startLocation = true;  // (a marker of the map)
    for (int type = kMineralsFirst; type <= kMineralsLast; ++type) role(type).mineralField = true;  // (minerals and
    role(kGeyser).geyser = true;                                                                    // gas look alike)
    // Each race's depot, worker and refinery (data): the first unit of the race with the flag, no hero; a refinery is
    // a resource container building of the geyser's size.
    const UnitData& geyser = t.units[kGeyser];
    for (int race = 0; race < 3; ++race) t.depotOf[race] = t.workerOf[race] = t.refineryOf[race] = 0;
    for (int i = int(t.units.size()) - 1; i >= 0; --i) {
        const UnitData& d = t.units[size_t(i)];
        int race = t.roles[size_t(i)].race;
        if (race < 0 || (d.flags & kHero)) continue;
        if ((d.flags & kDepot) && (d.flags & kBuilding)) t.depotOf[race] = i;
        if (d.flags & kWorker) t.workerOf[race] = i;
        if ((d.flags & kResource) && (d.flags & kBuilding) && d.placeW == geyser.placeW && d.placeH == geyser.placeH)
            t.refineryOf[race] = i;
    }
    role(kArchon).startDelay = 17;          // (it starts 17 frames after it is made: its merge)
    // Upgrades: which unit each changes, and how (upgrades.dat has no effects).
    const int speed[][2] = {{kVulture, kIonThrusters},       {kOverlord, kPneumatizedCarapace},
                            {kZergling, kMetabolicBoost},    {kHydralisk, kMuscularAugments},
                            {kZealot, kLegEnhancements},     {kScout, kGraviticThrusters},
                            {kShuttle, kGraviticDrive},      {kObserver, kGraviticBoosters},
                            {kUltralisk, kAnabolicSynthesis}};
    for (const auto& s : speed) role(s[0]).speedUpgrade = s[1];
    const int range[][3] = {{kMarine, kU238Shells, 32}, {kHydralisk, kGroovedSpines, 32},
                            {kDragoon, kSingularityCharge, 64}, {kGoliath, kCharonBoosters, 96}};
    for (const auto& s : range) {
        role(s[0]).rangeUpgrade = s[1];
        role(s[0]).rangeBonus = s[2];
    }
    role(kGoliath).rangeAirOnly = true;
    role(kZergling).cooldownUpgrade = kAdrenalGlands;
    role(kUltralisk).armorUpgradeExtra = kChitinousPlating;
    role(kUltralisk).armorExtra = 2;
    // (the images of carried gas are not linked to the workers in any file)
    role(kScv).gasImage = 403;
    role(kProbe).gasImage = 399;
    role(kDrone).gasImage = 401;

    t.weaponRoles.assign(t.weapons.size(), WeaponRole());
    for (size_t i = 0; i < t.weapons.size(); ++i)
        t.weaponRoles[i].lineHits = t.weapons[i].behaviour == kBehaviourMaxRange;
    t.weaponRoles[kHaloRockets].activeLimit = 80;  // (a limit of the game's, not in weapons.dat)
}

}  // namespace sim
