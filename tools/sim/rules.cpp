// The game's rules: reading rules.ini, and each unit type's and weapon's role from the data files and the rules.
#include "rules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <utility>
#include <vector>

#include "world.h"

namespace sim {
namespace {

// rules.ini's names, and where each value goes.
struct Number {
    const char* name;
    int GameRules::* value;
};
struct List {
    const char* name;
    std::vector<int> GameRules::* values;
    size_t count;      // exactly so many values; 0: any number
    size_t group = 1;  // (count 0) groups of so many values
};
#define SIM_NUMBER(n) {#n, &GameRules::n}
const Number kNumbers[] = {
    SIM_NUMBER(orderReaverAttack), SIM_NUMBER(orderCarrierAttack), SIM_NUMBER(orderInterceptor),
    SIM_NUMBER(orderScarab),
    SIM_NUMBER(orderMine), SIM_NUMBER(orderHeal), SIM_NUMBER(scarabUnit), SIM_NUMBER(interceptorUnit),
    SIM_NUMBER(scarabsHeld), SIM_NUMBER(interceptorsHeld), SIM_NUMBER(orderTick), SIM_NUMBER(targetSearchFrames),
    SIM_NUMBER(priorityNoAttackArmed), SIM_NUMBER(priorityWorker), SIM_NUMBER(priorityMover),
    SIM_NUMBER(priorityBuilding), SIM_NUMBER(priorityLow), SIM_NUMBER(sameWayTolerance), SIM_NUMBER(callForHelp),
    SIM_NUMBER(bounces), SIM_NUMBER(bounceSearch), SIM_NUMBER(launchWaitMax), SIM_NUMBER(persistHitFrames),
    SIM_NUMBER(lineHitRepeat), SIM_NUMBER(regenHp), SIM_NUMBER(regenShields), SIM_NUMBER(regenEnergy),
    SIM_NUMBER(maxEnergy), SIM_NUMBER(stuckRetry), SIM_NUMBER(stuckRetryEnd), SIM_NUMBER(stuckGiveUp),
    SIM_NUMBER(stuckYield), SIM_NUMBER(stuckHold), SIM_NUMBER(stuckFresh), SIM_NUMBER(straightReach),
    SIM_NUMBER(frozenFrames),
    SIM_NUMBER(pathAgeForRepath), SIM_NUMBER(healSearch), SIM_NUMBER(healGap), SIM_NUMBER(healMax),
    SIM_NUMBER(scarabForward), SIM_NUMBER(scarabLock), SIM_NUMBER(scarabStand), SIM_NUMBER(scarabSpeed),
    SIM_NUMBER(scarabArmFrames), SIM_NUMBER(scarabBlastDelay), SIM_NUMBER(scarabLife), SIM_NUMBER(interceptorLaunch),
    SIM_NUMBER(interceptorWait), SIM_NUMBER(interceptorLeash), SIM_NUMBER(interceptorNear), SIM_NUMBER(interceptorDock),
    SIM_NUMBER(interceptorLowShields), SIM_NUMBER(mineArm), SIM_NUMBER(mineBurrow), SIM_NUMBER(mineUnburrow),
    SIM_NUMBER(mineFuse), SIM_NUMBER(mineFuseStill), SIM_NUMBER(mineChase), SIM_NUMBER(mineFuseRange),
    SIM_NUMBER(mineBlastDelay), SIM_NUMBER(miningFrames), SIM_NUMBER(gasFrames), SIM_NUMBER(miningReach),
    SIM_NUMBER(mineralLoad), SIM_NUMBER(mineralImage), SIM_NUMBER(mineralFull), SIM_NUMBER(mineralHalf),
    SIM_NUMBER(mineralLow), SIM_NUMBER(baseSearchTiles), SIM_NUMBER(baseResourceGap), SIM_NUMBER(fieldRange),
    SIM_NUMBER(clusterGap), SIM_NUMBER(resourceNearBase), SIM_NUMBER(depotNear), SIM_NUMBER(scarabDetonate),
    SIM_NUMBER(velocityTurnDivisor), SIM_NUMBER(waypointPass), SIM_NUMBER(healEnergyDivisor),
    SIM_NUMBER(interceptorSpreadRange), SIM_NUMBER(interceptorLaunchHp), SIM_NUMBER(frameMs), SIM_NUMBER(startMinerals),
    SIM_NUMBER(gasWorkersMax), SIM_NUMBER(minDamage), SIM_NUMBER(otherFieldRange), SIM_NUMBER(startEnergy),
    SIM_NUMBER(bulletNear), SIM_NUMBER(bulletTurnDivisor), SIM_NUMBER(bulletElevationAbove),
    SIM_NUMBER(elevationLowest), SIM_NUMBER(elevationHighest), SIM_NUMBER(damageFirstImage),
    SIM_NUMBER(damageSecondImage), SIM_NUMBER(creepGrowFrames), SIM_NUMBER(creepRecedeDelay),
    SIM_NUMBER(speedUpgradeMin), SIM_NUMBER(speedUpgradeAccel), SIM_NUMBER(speedUpgradeTurn),
    SIM_NUMBER(cooldownUpgradeDivisor), SIM_NUMBER(shieldArmorUpgrade), SIM_NUMBER(energyUpgrade),
    SIM_NUMBER(maxEnergyUpgraded), SIM_NUMBER(startEnergyUpgraded),
};
#undef SIM_NUMBER
const List kLists[] = {
    {"attacksBurrowed", &GameRules::attacksBurrowed, 0},
    {"ordersEveryFrame", &GameRules::ordersEveryFrame, 0},
    {"startDelay", &GameRules::startDelay, 0, 2},
    {"gasImage", &GameRules::gasImage, 0, 2},
    {"speedUpgrade", &GameRules::speedUpgrade, 0, 2},
    {"speedAlways", &GameRules::speedAlways, 0},
    {"speedUpgradeFactor", &GameRules::speedUpgradeFactor, 2},
    {"rangeUpgrade", &GameRules::rangeUpgrade, 0, 4},
    {"cooldownUpgrade", &GameRules::cooldownUpgrade, 0, 2},
    {"armorUpgrade", &GameRules::armorUpgrade, 0, 3},
    {"scarabsUpgrade", &GameRules::scarabsUpgrade, 2},
    {"interceptorsUpgrade", &GameRules::interceptorsUpgrade, 2},
    {"projectileLimit", &GameRules::projectileLimit, 0, 2},
    {"creepRecedeSteps", &GameRules::creepRecedeSteps, 0, 2},
    {"bounceDivisor", &GameRules::bounceDivisor, 0},     {"damageFactor", &GameRules::damageFactor, 20},
    {"interceptorSpread", &GameRules::interceptorSpread, 3}, {"splashDivisor", &GameRules::splashDivisor, 3},
    {"cooldownJitter", &GameRules::cooldownJitter, 2},
    {"bulletTurnMargin", &GameRules::bulletTurnMargin, 3},
};

const char kDefault[] =
#include "sim_rules_ini.inc"
    ;

}  // namespace

const char* defaultRules() { return kDefault; }

bool readRules(const std::string& text, GameRules& rules, std::string& error) {
    rules = GameRules();
    std::vector<bool> seen(std::size(kNumbers) + std::size(kLists), false);
    std::istringstream in(text.compare(0, 3, "\xEF\xBB\xBF") ? text : text.substr(3));  // (a UTF-8 mark)
    std::string line;
    for (int n = 1; std::getline(in, line); ++n) {
        line = line.substr(0, line.find('#'));
        size_t eq = line.find('=');
        auto trim = [](std::string s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            size_t b = 0;
            while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            return s.substr(b);
        };
        std::string key = trim(line.substr(0, eq == std::string::npos ? line.size() : eq));
        if (key.empty() || key[0] == '[') continue;  // (an empty line, a comment or a section)
        auto fail = [&](const std::string& why) {
            error = "rules.ini line " + std::to_string(n) + ": " + why;
            return false;
        };
        if (eq == std::string::npos) return fail("no \"=\"");
        std::vector<int> values;
        std::string value = line.substr(eq + 1);
        std::replace(value.begin(), value.end(), ',', ' ');
        std::istringstream vs(value);
        for (std::string word; vs >> word;) {
            char* end = nullptr;
            long v = std::strtol(word.c_str(), &end, 10);
            if (*end) return fail("\"" + word + "\" is not a number");
            values.push_back(int(v));
        }
        bool known = false;
        for (size_t k = 0; k < std::size(kNumbers) && !known; ++k)
            if (key == kNumbers[k].name) {
                if (values.size() != 1) return fail(key + " takes one number");
                rules.*kNumbers[k].value = values[0];
                known = seen[k] = true;
            }
        for (size_t k = 0; k < std::size(kLists) && !known; ++k)
            if (key == kLists[k].name) {
                if (kLists[k].count && values.size() != kLists[k].count)
                    return fail(key + " takes " + std::to_string(kLists[k].count) + " numbers");
                if (values.size() % kLists[k].group)
                    return fail(key + " takes groups of " + std::to_string(kLists[k].group) + " numbers");
                rules.*kLists[k].values = values;
                known = seen[std::size(kNumbers) + k] = true;
            }
        if (!known) return fail("unknown name \"" + key + "\"");
    }
    for (size_t k = 0; k < seen.size(); ++k)
        if (!seen[k]) {
            error = std::string("rules.ini: no value for ") +
                    (k < std::size(kNumbers) ? kNumbers[k].name : kLists[k - std::size(kNumbers)].name);
            return false;
        }
    // Values that the engine divides by, and lists that it reads by position.
    for (const char* name : {"orderTick", "persistHitFrames", "scarabDetonate", "velocityTurnDivisor",
                             "bulletTurnDivisor", "healEnergyDivisor"})
        for (const Number& k : kNumbers)
            if (std::string(name) == k.name && rules.*k.value <= 0) {
                error = std::string("rules.ini: ") + name + " must be more than 0";
                return false;
            }
    auto positive = [](const std::vector<int>& v) {
        return std::all_of(v.begin(), v.end(), [](int x) { return x > 0; });
    };
    if (rules.bounceDivisor.empty() || !positive(rules.bounceDivisor) || !positive(rules.splashDivisor) ||
        rules.cooldownJitter[0] <= 0) {
        error = "rules.ini: bounceDivisor, splashDivisor and cooldownJitter's first value must be more than 0";
        return false;
    }
    if (rules.speedUpgradeFactor[1] <= 0 || rules.cooldownUpgradeDivisor <= 0 || rules.bulletTurnMargin[2] <= 0) {
        error = "rules.ini: speedUpgradeFactor's and bulletTurnMargin's last values and cooldownUpgradeDivisor "
                "must be more than 0";
        return false;
    }
    return true;
}

void deriveRoles(Tables& t) {
    const GameRules& rules = t.rules;
    t.roles.assign(t.units.size(), UnitRole());
    auto valid = [&](int type) { return type >= 0 && type < int(t.units.size()); };
    for (size_t i = 0; i < t.units.size(); ++i) {
        const UnitData& d = t.units[i];
        UnitRole& r = t.roles[i];
        r.race = d.group & kGroupZerg ? 0 : d.group & kGroupTerran ? 1 : d.group & kGroupProtoss ? 2 : -1;
        bool weapon = d.groundWeapon != kNoWeapon || d.airWeapon != kNoWeapon;
        r.defence = (d.flags & kBuilding) && weapon;
        r.critter = (d.group & kGroupNeutral) && !(d.group & kGroupBuilding) && !(d.flags & kBuilding) && !weapon;
        r.lowPriority = r.race >= 0 && !(d.group & (kGroupMen | kGroupBuilding)) && !(d.flags & kBuilding);
        r.halfCost = (d.flags & kTwoInOneEgg) != 0;
        // (units.dat's orders: the game's own orders that rules.ini names)
        r.launchesScarabs = d.attackOrder == rules.orderReaverAttack;
        r.launchesInterceptors = d.attackOrder == rules.orderCarrierAttack;
        r.capacity = r.launchesScarabs ? rules.scarabsHeld : r.launchesInterceptors ? rules.interceptorsHeld : 0;
        r.scarab = d.attackOrder == rules.orderScarab;
        r.interceptor = d.attackOrder == rules.orderInterceptor;
        r.spiderMine = d.idleOrder == rules.orderMine;
        r.healer = d.attackOrder == rules.orderHeal;
        if (r.launchesScarabs)
            r.capacityUpgrade = rules.scarabsUpgrade[0], r.capacityUpgraded = rules.scarabsUpgrade[1];
        if (r.launchesInterceptors)
            r.capacityUpgrade = rules.interceptorsUpgrade[0], r.capacityUpgraded = rules.interceptorsUpgrade[1];
        if (r.healer) r.energyUpgrade = rules.energyUpgrade;
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
    // From rules.ini: the units a Reaver and a Carrier make, and the roles no data file says.
    t.scarabType = valid(rules.scarabUnit) ? rules.scarabUnit : 0;
    t.interceptorType = valid(rules.interceptorUnit) ? rules.interceptorUnit : 0;
    for (int type : rules.attacksBurrowed)
        if (valid(type)) t.roles[size_t(type)].attacksBurrowed = true;
    for (int type : rules.ordersEveryFrame)
        if (valid(type)) t.roles[size_t(type)].ordersEveryFrame = true;
    for (size_t k = 0; k + 1 < rules.startDelay.size(); k += 2)
        if (valid(rules.startDelay[k])) t.roles[size_t(rules.startDelay[k])].startDelay = rules.startDelay[k + 1];
    for (size_t k = 0; k + 1 < rules.gasImage.size(); k += 2)
        if (valid(rules.gasImage[k])) t.roles[size_t(rules.gasImage[k])].gasImage = rules.gasImage[k + 1];

    // Upgrades: which units each changes, and how (upgrades.dat has no effects).
    for (size_t k = 0; k + 1 < rules.speedUpgrade.size(); k += 2)
        if (valid(rules.speedUpgrade[k]))
            t.roles[size_t(rules.speedUpgrade[k])].speedUpgrade = rules.speedUpgrade[k + 1];
    for (int type : rules.speedAlways)
        if (valid(type)) t.roles[size_t(type)].speedAlways = true;
    for (size_t k = 0; k + 3 < rules.rangeUpgrade.size(); k += 4)
        if (valid(rules.rangeUpgrade[k])) {
            UnitRole& r = t.roles[size_t(rules.rangeUpgrade[k])];
            r.rangeUpgrade = rules.rangeUpgrade[k + 1];
            r.rangeBonus = rules.rangeUpgrade[k + 2];
            r.rangeAirOnly = rules.rangeUpgrade[k + 3] != 0;
        }
    for (size_t k = 0; k + 1 < rules.cooldownUpgrade.size(); k += 2)
        if (valid(rules.cooldownUpgrade[k]))
            t.roles[size_t(rules.cooldownUpgrade[k])].cooldownUpgrade = rules.cooldownUpgrade[k + 1];
    for (size_t k = 0; k + 2 < rules.armorUpgrade.size(); k += 3)
        if (valid(rules.armorUpgrade[k])) {
            t.roles[size_t(rules.armorUpgrade[k])].armorUpgradeExtra = rules.armorUpgrade[k + 1];
            t.roles[size_t(rules.armorUpgrade[k])].armorExtra = rules.armorUpgrade[k + 2];
        }

    t.weaponRoles.assign(t.weapons.size(), WeaponRole());
    for (size_t i = 0; i < t.weapons.size(); ++i) t.weaponRoles[i].lineHits = t.weapons[i].behaviour == kToMaxRange;
    for (size_t k = 0; k + 1 < rules.projectileLimit.size(); k += 2)
        if (rules.projectileLimit[k] >= 0 && rules.projectileLimit[k] < int(t.weapons.size()))
            t.weaponRoles[size_t(rules.projectileLimit[k])].activeLimit = rules.projectileLimit[k + 1];
}

}  // namespace sim
