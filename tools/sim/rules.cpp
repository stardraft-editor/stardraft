// The unit and weapon roles: from units.dat and weapons.dat where they say it, and from the table below where no file
// does (each entry with its reason).
#include "rules.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "world.h"

namespace sim {
namespace {

// units.dat ids of the table below.
enum : int {
    kMarine = 0, kVulture = 2, kGoliath = 3, kScv = 7, kZergling = 37, kHydralisk = 38, kUltralisk = 39, kDrone = 41,
    kOverlord = 42, kProbe = 64, kZealot = 65, kDragoon = 66, kArchon = 68, kShuttle = 69, kScout = 70, kObserver = 84,
    kInterceptor = 73, kScarab = 85, kLurker = 103,
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
// orders.dat ids: the game's own orders that units.dat gives some units (their code is the game's, not in a file).
enum : int {
    kOrderReaverAttack = 59, kOrderCarrierAttack = 53, kOrderInterceptor = 64, kOrderScarab = 65, kOrderMine = 20,
    kOrderHeal = 175,
};
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
    // From units.dat's orders (data): the units whose attack is the Reaver's or the Carrier's launch, the Scarab and
    // the Interceptor (their own orders), the Spider Mine (its idle order is the mine's) and the Medic (its attack is
    // the heal). How many Scarabs or Interceptors a unit holds is in no file.
    for (size_t i = 0; i < t.units.size(); ++i) {
        const UnitData& d = t.units[i];
        UnitRole& r = t.roles[i];
        if (d.attackOrder == kOrderReaverAttack) {
            r.launchesScarabs = true;
            r.capacityUpgrade = kReaverCapacity;
            r.capacity = 5;
            r.capacityUpgraded = 10;
        }
        if (d.attackOrder == kOrderCarrierAttack) {
            r.launchesInterceptors = true;
            r.capacityUpgrade = kCarrierCapacity;
            r.capacity = 4;
            r.capacityUpgraded = 8;
        }
        r.scarab = d.attackOrder == kOrderScarab;
        r.interceptor = d.attackOrder == kOrderInterceptor;
        r.spiderMine = d.idleOrder == kOrderMine;
        r.healer = d.attackOrder == kOrderHeal;
        if (r.healer) r.energyUpgrade = kCaduceusReactor;  // (table: Caduceus Reactor raises its energy)
    }
    // Resources and markers (data): a geyser is a neutral resource container of a race refinery's size (a race's
    // resource container that is not a depot); mineral fields are the other neutral resource containers that are not in
    // the buildings group; a start location is a neutral depot that holds no resources.
    auto neutral = [&](size_t i) { return t.roles[i].race < 0; };
    std::vector<std::pair<int, int>> refinerySizes;
    for (size_t i = 0; i < t.units.size(); ++i) {
        const UnitData& d = t.units[i];
        if (!neutral(i) && (d.flags & kResource) && !(d.flags & kDepot)) refinerySizes.push_back({d.placeW, d.placeH});
    }
    for (size_t i = 0; i < t.units.size(); ++i) {
        const UnitData& d = t.units[i];
        UnitRole& r = t.roles[i];
        if (!neutral(i)) continue;
        if (d.flags & kResource) {
            r.geyser = std::find(refinerySizes.begin(), refinerySizes.end(), std::make_pair(d.placeW, d.placeH)) !=
                       refinerySizes.end();
            r.mineralField = !r.geyser && !(d.group & kGroupBuilding);
        }
        r.startLocation = (d.flags & kDepot) && !(d.flags & kResource);
    }
    // (table: the units a Reaver and a Carrier make are fixed in the game's code, not in a file)
    t.scarabType = kScarab;
    t.interceptorType = kInterceptor;
    role(kLurker).attacksBurrowed = true;   // (table: no file says it has its weapon only while burrowed)
    role(kLurker).speedUpgrade = rules::kAlways;  // (table: it moves as if upgraded)
    role(kZergling).ordersEveryFrame = true;  // (table: a rule of the game's code for the Zergling)
    // Each race's depot, worker and refinery (data): the first unit of the race with the flag, no hero; a refinery is
    // a resource container building of a geyser's size.
    int geyserW = -1, geyserH = -1;
    for (size_t i = 0; i < t.units.size(); ++i)
        if (t.roles[i].geyser) geyserW = t.units[i].placeW, geyserH = t.units[i].placeH;
    for (int race = 0; race < 3; ++race) t.depotOf[race] = t.workerOf[race] = t.refineryOf[race] = 0;
    for (int i = int(t.units.size()) - 1; i >= 0; --i) {
        const UnitData& d = t.units[size_t(i)];
        int race = t.roles[size_t(i)].race;
        if (race < 0 || (d.flags & kHero)) continue;
        if ((d.flags & kDepot) && (d.flags & kBuilding)) t.depotOf[race] = i;
        if (d.flags & kWorker) t.workerOf[race] = i;
        if ((d.flags & kResource) && (d.flags & kBuilding) && d.placeW == geyserW && d.placeH == geyserH)
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
