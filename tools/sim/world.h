// The simulation's inside: the game data it reads, the map, the units and projectiles, and one game world (a frame
// at a time). engine.cpp has the movement, the animation and the Simulator; mining.cpp and combat.cpp the orders.
// Written only from behaviour descriptions (see docs/dev/SIMULATION.md).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "proximity.h"
#include "rules.h"
#include "sim.h"

namespace sim {

// units.dat flags (Special Ability Flags) that the engine uses.
enum : uint32_t {
    kBuilding = 0x1, kFlyer = 0x4, kWorker = 0x8, kHero = 0x40, kRegenerates = 0x80, kTwoInOneEgg = 0x400,
    kDepot = 0x1000, kResource = 0x2000, kRobotic = 0x4000, kDetector = 0x8000, kOrganic = 0x10000,
    kSpellcaster = 0x200000, kCloaked = 0x400000, kInvincible = 0x20000000, kMechanical = 0x40000000,
};
// units.dat StarEdit group flags.
enum : int {
    kGroupZerg = 0x1, kGroupTerran = 0x2, kGroupProtoss = 0x4, kGroupMen = 0x8, kGroupBuilding = 0x10,
    kGroupNeutral = 0x80,
};
enum : int { kNoWeapon = 130, kNoUnit = 228, kNeutralPlayer = 11 };
enum : int { kDirectionRandom = 32 };      // (units.dat direction: a random one)
enum : int { kDamageIgnoreArmor = 4 };     // (weapons.dat damage type)
// iscript.bin animations of a header, by their number.
enum : int {
    kAnimInit = 0, kAnimDeath = 1, kAnimGroundAttack = 5, kAnimAirAttack = 6, kAnimGroundAttackToIdle = 8,
    kAnimWalking = 11, kAnimWalkingToIdle = 12, kAnimSpecial1 = 13, kAnimSpecial2 = 14, kAnimAlmostBuilt = 15,
    kAnimWorkingToIdle = 20, kAnimBurrow = 25,
};
// weapons.dat: behaviours, explosions.
enum : int {
    kFlyStraight = 0, kFlyFollow = 1, kAppearOnTarget = 2, kPersistOnSite = 3, kAppearOnSite = 4, kAppearOnAttacker = 5,
    kSuicide = 6, kBounce = 7, kTargetArea = 8, kToMaxRange = 9,
};
enum : int { kRadialSplash = 2, kEnemySplash = 3, kAirSplash = 24 };
// images.dat draw function of a shadow.
enum : int { kDrawShadow = 10 };
// flingy.dat control: moved by its script.
enum : int { kControlBraking = 0, kControlScript = 2 };

struct UnitData {
    int direction = 0;  // (units.dat: the heading at creation / 8; 32: random)
    int sight = 0;      // (tiles: a mobile detector's range)
    int elevation = 0;  // (its sprite's elevation level)
    int armorUpgrade = 0, flingy = 0, subunit = kNoUnit, hp = 0, shields = 0, group = 0;
    int groundWeapon = kNoWeapon, airWeapon = kNoWeapon, acquire = 0;
    bool hasShields = false;
    uint32_t flags = 0;
    int size = 0, armor = 0, left = 0, up = 0, right = 0, down = 0, placeW = 0, placeH = 0;
    int minerals = 0, gas = 0, supply = 0;
    int idleOrder = 0, attackOrder = 0;  // (units.dat: its computer idle order and its attack-unit order)
};
struct WeaponData {
    int flingy = 0, targetFlags = 0, minRange = 0, maxRange = 0, damageType = 0, behaviour = 0, removeAfter = 0;
    int explosion = 0, inner = 0, medium = 0, outer = 0, damage = 0, cooldown = 0, factor = 1, angle = 0;
    int forward = 0, upward = 0, spin = 0, upgrade = 0, bonus = 0;
};
struct FlingyData {
    int sprite = 0, topSpeed = 0, accel = 0, halt = 0, turn = 0, control = 0;
};

// The game data the engine reads (data.cpp).
struct Tables {
    std::vector<UnitData> units;
    std::vector<WeaponData> weapons;
    std::vector<FlingyData> flingy;
    GameRules rules;                    // (rules.ini)
    std::vector<UnitRole> roles;        // (rules.cpp)
    std::vector<WeaponRole> weaponRoles;
    int scarabType = 0, interceptorType = 0;  // (the unit types whose weapons Reavers and Carriers use)
    int depotOf[3] = {}, workerOf[3] = {}, refineryOf[3] = {};  // (by race: Zerg, Terran, Protoss)
    std::vector<int> spriteImage, imageTurns, imageIscript, imageDraw;  // (draw function 10: a shadow)
    std::vector<bool> spriteVisible;  // (sprites.dat "is visible": the game draws the sprite)
    std::vector<int> imageFrames;     // the frames in a turning image's GRP (0: not known)
    // The workers' main images: the first offset of their special overlay list (images.dat, its .lo file), by
    // frame: where a carried resource is drawn.
    std::vector<std::pair<int, std::vector<std::pair<int, int>>>> carryOffsets;
    // Each image's attack overlay list (images.dat, its .lo file), by GRP frame, the offsets of each overlay: where
    // imgoluselo and imguluselo put an image. Not there: none.
    std::vector<std::vector<std::vector<std::pair<int, int>>>> attackOffsets;
    std::vector<uint8_t> iscript;
    std::vector<std::pair<int, int>> scripts;  // (iscript id, offset of its header)
    FileReader read;

    bool load(const FileReader& reader, std::string& error);
    int flingyImage(int flingyId) const;
    bool flingyVisible(int flingyId) const;  // (its sprite is drawn by the game: sprites.dat "is visible")
    int scriptHeader(int image) const;  // -1: none
    int animOffset(int header, int anim) const;  // 0: the script has no such animation
    int opLength(int pc) const;                  // the bytes of the iscript instruction at pc, -1: unknown
    bool firesFromScript(int header, int anim) const;  // its animation has an attack instruction (to its loop)
};

// The map: its size, which 8x8 minitiles can be walked on, and its placed units (data.cpp).
struct MapInfo {
    struct Placed {
        int type = 0, x = 0, y = 0, owner = 0, resources = 0;
    };
    int width = 0, height = 0, tileset = 0;  // tiles
    std::vector<uint8_t> walk;               // per minitile: 1 walkable
    // Regions (rectangles of one class, no expansion or merging): per tile its region;
    // per region its centre, class (0 unwalkable, 1 low, 2 middle, 3 high), group and walkable neighbours.
    struct Region {
        int x = 0, y = 0, cls = 0, group = 0;
        std::vector<int> neighbours;
    };
    std::vector<int> tileRegion;
    std::vector<Region> regions;
    std::vector<Placed> units;
    std::vector<uint8_t> upgrades;  // each player's start level of each upgrade (12 x 61), from PUPx or UPGR
    std::vector<uint8_t> techs;     // each player's researched techs (12 x 44), from PTEx or PTEC
    std::vector<uint8_t> sides;     // each player's race (SIDE: 0 Zerg, 1 Terran, 2 Protoss, 5 picked, 6 random)

    bool load(const std::vector<uint8_t>& chk, const Tables& t, std::string& error);
    bool walkable(int px, int py) const;
    bool boxWalkable(int x0, int y0, int x1, int y1) const;  // every minitile the box touches
};

enum Order : uint8_t {
    None, Idle, MoveToMinerals, WaitForMinerals, MiningMinerals, ReturnMinerals, MoveToGas, WaitForGas, HarvestGas,
    ReturnGas, AttackMove, AttackUnit,
};

// An image's animation (its iscript): where it runs, its wait, and the GRP frame it shows.
struct Anim {
    int header = -1, pc = 0, wait = 0, frame = 0, ret = 0;
    bool paused = true;
    bool noBreak = false;  // (between nobrkcodestart and nobrkcodeend: a new animation waits for the end)
    bool hidden = false;   // (between tmprmgraphicstart and tmprmgraphicend: not drawn)
    int pending = -1;      // (that animation)
};

struct Unit {
    int type = 0, owner = 0, side = 0;
    int x = 0, y = 0;
    int32_t ex = 0, ey = 0;  // the exact place (8 fractional bits)
    int heading = 0, velDir = 0, speed = 0;
    int moveX = 0, moveY = 0;  // the move target
    int aimX = 0, aimY = 0;    // the target waypoint: it turns to it at the move target
    int hp = 0, shields = 0;   // (8 fractional bits)
    bool alive = true, hidden = false, building = false;
    Order order = Idle, next = None;
    int state = 0, poll = 0, timer = 0;
    // Mining: a worker's field, what it carries; a resource's amount, its holder and its waiting workers.
    int field = -1, carry = 0, lastDelivery = -1, base = 0;
    bool carryGas = false;
    int resources = 0, holder = -1;
    std::vector<int> queue;
    // Combat.
    int cooldown = 0, target = -1, autoTarget = -1, lastAttacker = -1, amX = 0, amY = 0;
    bool repeatAttack = false;
    int walkingAnim = 0;  // the animation it plays: 0 idle, 1 walking, 2 attacking
    int kills = 0, damageTaken = 0;  // (damage: 8 fractional bits)
    int energy = 0, healTarget = -1, healedFrame = -1;  // (a Medic's energy: 8 fractional bits)
    int stuck = 0;                            // the block counter
    int pathFrame = -100;                     // (the frame its path was made)
    int frozenX = -1, frozenY = -1;           // (immovable: the move target it does not take again,
    int frozenUntil = 0;                      // until this frame)
    bool retry = false;                       // (a failed path: it waits, then searches again)
    bool cut = false;                         // (its path goal cannot be reached: it goes to the nearest place)
    bool partial = false;                     // (its path goes part of the way: the rest when it is walked)
    bool pending = false;                     // (its search waits for a later frame: the budget of a frame is used)
    bool unreached = false;                   // (its last search did not get to its goal: no walking progress)
    int cutGoalX = -1, cutGoalY = -1;         // (the last goal it could not reach, the place it went to instead,
    int cutX = -1, cutY = -1;                 // and when it may search for that goal again)
    int nextSearch = 0, fails = 0, parts = 0;  // (parts: the capped searches since one got to its goal)
    int progressTarget = -1, progressBest = 0;  // (its nearest enemy, and its nearest distance yet: progress)
    int walkGoalX = -1, walkGoalY = -1, walkBest = 0;  // (its move target, and the shortest way left to it yet)
    int walked = 0;  // (the way it walked that counted as progress since it last hit or was hit)
    int seekTarget = -1, seekRegion = -1;      // (search and destroy: the enemy it goes to, and that one's region)
    int scarabs = 0;                          // (a Reaver's)
    int parent = -1;                          // (an Interceptor's Carrier)
    int movedFrame = -1, burrowStep = 0, startWait = 0, dieAt = -1;
    int id = 0;
    bool fighter = false, burrowing = false, burrowed = false, neutral = false;  // (neutral: the map's own units)
    std::vector<std::pair<int, int>> path;     // the waypoints around unwalkable ground (the last: the move target)
    int pathX = -1, pathY = -1;                // the goal the path was made for
    Anim anim, turret;  // (turret: the subunit's animation, for units such as the Siege Tank)
};

struct Bullet {
    int weapon = 0, owner = 0, side = 0, source = -1, target = -1;
    int x = 0, y = 0, tx = 0, ty = 0;
    int32_t ex = 0, ey = 0;
    int heading = 0, velDir = 0, speed = 0, life = 0;
    bool alive = true, missed = false, hit = false, arrived = false, launched = false, scarab = false;
    int age = 0, bounces = 0, previous = -1;
    int hitX = -1, hitY = -1;  // (a line of hits: where the last hit was)
    int body = -1;             // (a Scarab: its own image while it flies, as the weapon's sprite is not drawn)
    Anim anim;
};

// One game: its units and projectiles, a frame at a time.
class World {
public:
    World(const Tables& tables, const MapInfo& mapInfo) : t(tables), map(mapInfo) {
        unitGrid.reset(map.width * 32, map.height * 32);
        for (Player& p : players) p.minerals = t.rules.startMinerals;
    }

    const Tables& t;
    const MapInfo& map;
    const UnitRole& role(int type) const { return t.roles[size_t(type)]; }
    std::vector<Unit> units;
    std::vector<Bullet> bullets;
    // Pictures with no part in the game: a dead unit's death animation and what its script makes (remnants,
    // explosions), each playing its own script until its end (iscript.bin). `under`: drawn under the living units.
    struct Effect {
        Anim anim;
        int image = 0, x = 0, y = 0, owner = 0, heading = 0, elevation = 0;
        bool under = false, done = false;
        int follow = -1, dx = 0, dy = 0;  // (an overlay of a living unit: its unit and its offset)
        // (an overlay: its place among its unit's images, the main image 0, those under it below 0; an overlay's own
        // overlay just above or under it)
        double layer = 0;
        int depth = 0;
    };
    std::vector<Effect> effects;
    uint32_t effectSeed = 1;  // (their own random numbers: they never change the game's)
    bool recordSounds = false;     // (a live game: the scripts' sounds are kept for takeSounds)
    // Fast forward: no sounds, no effect sprites (overlays, remnants, death pictures), no mineral field pictures;
    // only what the play depends on. The results are the same.
    bool fastForward = false;
    void setFastForward(bool on) {
        fastForward = on;
        if (on) effects.clear();
    }
    std::vector<LiveSound> sounds;
    void sound(int sfx, int x, int y, int owner) {
        if (!recordSounds || fastForward || sfx <= 0 || sfx >= 1144) return;
        if (sounds.size() >= 4096) sounds.erase(sounds.begin(), sounds.begin() + 2048);  // (none taken: the newest)
        sounds.push_back({sfx, x, y, owner});
    }
    void addEffect(int image, int x, int y, int owner, int heading, int elevation, bool under, int anim,
                   int follow = -1, int dx = 0, int dy = 0);
    int bulletElevation(const Bullet& b) const;
    void gatherPicture(int unit, int weapon);
    int spriteElevation(int op, int parent) const;
    void animate(int unit, int anim);  // a unit's main image and its overlays start an animation
    void effectStep(size_t k);
    int frame = 0;
    bool combat = false;  // a fight: units step around each other, and idle units look for targets
    uint32_t seed = 42;
    int fixedCentres[2][2] = {{-1, -1}, {-1, -1}};  // (a fight's side centres as given, such as across a choke)

    // Mining: each player's resources (by player number).
    struct Player {
        int minerals = 0, gas = 0, gathered = 0, gatheredGas = 0, measuredFrom = -1, mineralsAtMeasure = 0,
            gasAtMeasure = 0;
    };
    Player players[12];
    struct Trip {
        double sum = 0;
        int count = 0;
    };
    std::vector<Trip> trips;                // by unit index of the field
    std::vector<std::vector<int>> baseFields;  // each base's fields (unit indices), nearest first
    int measureFrom = 0;

    // Common (engine.cpp).
    int random();
    int random(int lo, int hi) { return lo + ((random() * (hi - lo + 1)) >> 15); }
    int add(int type, int owner, int x, int y);
    void step();
    void freeSpot(int type, int& x, int& y, const Unit* self = nullptr) const;  // (self: not in the way)
    bool freeSpotWithin(int type, int& x, int& y, const Unit* self, int limit) const;
    void setMove(Unit& u, int x, int y);
    bool fits(int type, int x, int y) const;  // a ground unit's box on walkable minitiles only
    bool lineClear(int type, int x0, int y0, int x1, int y1) const;
    void findPath(Unit& u, int x, int y);
    bool gridPath(int type, int fx, int fy, int x, int y, std::vector<std::pair<int, int>>& out) const;
    // The minitile search's buffers, kept between searches: a cell's values count only when its stamp is this
    // search's (so nothing is cleared); per cell: cost, from, place, corner, whether the box fits, and the straight
    // steps to its 8 neighbours already tested (and their results).
    struct Search {  // (left as they are when made: only the stamps start at 0, so a first search is cheap)
        struct Place {
            int first, second;
            operator std::pair<int, int>() const { return {first, second}; }
        };
        size_t cells = 0;
        std::unique_ptr<int[]> cost, from;
        std::unique_ptr<Place[]> at, via;
        std::unique_ptr<uint32_t[]> stamp;
        std::unique_ptr<uint8_t[]> fit, tested, clear;
        uint32_t now = 0;
        bool capped = false;  // (the last search stopped at its cap, not at the end of the ground it can reach)
        int spent = 0;        // (the minitiles expanded by the searches of this frame)
    };
    mutable Search search;
    struct ShortBuffers {  // (the short path's work space, kept between searches)
        std::vector<int> cost, from;
        std::vector<signed char> open;
        std::vector<std::array<int, 4>> obstacles;
        std::vector<std::pair<int, int>> heap;
    };
    ShortBuffers shortBuffers;
    // The living units by place (proximity.h): a unit's cells change when it is made, moves across a cell edge or dies.
    UnitGrid unitGrid;
    void regrid(int unit);  // (after a unit's place changed)
    Box boxOf(const Unit& u) const;
    Box boxAt(int type, int x, int y) const;
    // The units near an area (a superset of those whose box meets it), in their list order.
    std::vector<int> nearUnits(const Box& area) const { return unitGrid.query(area); }
    template <class F> void forNear(const Box& area, F f) const { unitGrid.forEach(area, f); }
    void stopMove(Unit& u);
    void setOrder(Unit& u, Order o);
    void startAnim(Anim& a, int anim);
    bool noBreak(Anim& a, int op);
    int grpFrame(int image, int frame, int heading, bool* flip = nullptr) const;
    std::pair<int, int> loOffset(int parent, int frame, int heading, int overlay) const;
    bool targetWithin(int unit, int range) const;
    void images(std::vector<LiveImage>& out) const;
    int gap(const Unit& a, const Unit& b) const;
    int pointGap(const Unit& v, int x, int y) const;
    bool overlaps(int type, int x, int y, const Unit& o) const;
    bool collides(const Unit& u, const Unit& o) const;
    bool onBuilding(const Box& box) const;  // (mining: the box touches a building's, not a mineral field's)
    mutable std::vector<uint8_t> buildingMask;  // (mining: the minitiles that buildings cover)
    mutable std::vector<Box> buildingBoxes;
    mutable bool buildingMaskReady = false;
    // The order tick: false on the frames in between; true every 9th frame. A new order runs at once; after that the
    // unit's order runs on its own phase of the 9 (fixed for its life, from its number), so that not all units act on
    // the same frame.
    bool tick(Unit& u) const {
        if (u.poll > 0) {
            --u.poll;
            return false;
        }
        // (in a fight: the unit's number mod 9, so units made one after another act on frames one after another; in
        // mining: a hash of the number, as the workers of a base are made in a row)
        const uint32_t tick = uint32_t(t.rules.orderTick);
        uint32_t phase = combat ? uint32_t(u.id) % tick : (uint32_t(u.id) * 2654435761u >> 16) % tick;
        u.poll = int(tick) - 1 - int((uint32_t(frame) + phase) % tick);  // (the next frame f: (f + phase) % tick == 0)
        return true;
    }

    // Mining (mining.cpp).
    void miningOrder(int i);
    // (mining orders, one by state, and their parts)
    void moveToResource(int i);
    void mineMinerals(int i);
    void harvestGas(int i);
    void returnCargo(int i);
    bool resourceValid(const Unit& u, int f) const;
    void goToResource(Unit& u, int f);
    int findField(const Unit& u, int range, bool freeOnly) const;
    void claimResource(int w);
    void releaseResource(int fi);
    void extract(Unit& u, int fi);
    int nearestDepot(const Unit& u) const;
    void depotBorder(const Unit& w, const Unit& b, int& x, int& y) const;

    // Combat (combat.cpp).
    void combatOrder(int i);
    // (combat orders, one by order and kind of unit: orders.cpp)
    void spiderMineOrder(int i);
    void burrowOrder(int i);
    void interceptorOrder(int i);
    void guardOrder(int i);
    void attackMoveOrder(int i);
    void attackUnitOrder(int i);
    void launchInterceptor(int i, int current);
    void launchScarab(int i, int current);
    void fire(int i, int weapon, bool melee, int forward = -1);
    void bulletStep(int b);
    void bulletHit(Bullet& b);
    void trackProgress();
    bool searchBudgetLeft() const;  // (a long path search may run in this frame)
    bool canAttack(const Unit& a, const Unit& b) const;
    bool detected(const Unit& v, int side) const;
    bool couldHit(const Unit& a, const Unit& v) const {  // (its weapons hit v's kind: also a Lurker not yet down)
        const UnitData& d = weaponUnit(a.type);
        bool air = (t.units[size_t(v.type)].flags & kFlyer) != 0;
        int w = air ? d.airWeapon : d.groundWeapon;
        return (w < kNoWeapon && (t.weapons[size_t(w)].targetFlags & (air ? 1 : 2))) || weaponFor(a, v) != kNoWeapon;
    }
    bool inRange(const Unit& a, const Unit& b) const;
    int priority(int attacker, int candidate) const;
    int acquire(int i, bool draw = true);  // (draw: a Reaver's or Carrier's random pick)
    int helpless = -1;
    bool allTeams = false;  // (a fight of placed units: it ends when no team can reach and attack an enemy)
    bool seek = false;      // (placed units: search and destroy; each unit goes on to the nearest enemy)
    bool reaches(const Unit& a, const Unit& v) const;  // a can get at v: it flies, or v is in its walkable group
    int nearestEnemy(const Unit& a) const;  // the nearest enemy a can attack and reach; -1: none
    int firstShot[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // (the frames, by side or team)
    int firstDamage[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    std::vector<std::array<int, 3>> spineHits;                 // (frame, Lurker, target)
    int spinSource = -1, spinSide = 0;  // the last bullet with a heading offset: its unit and side
    bool timedOut = false;
    long long pathSearches = 0, shortSearches = 0;  // (long paths and short paths searched: a measure for the tests)
    int searchesNow = 0, searchFrame = -1;  // (the long searches of this frame: at most kSearchBudget)
    // (goals a unit type could not reach from a group: the nearest place it reached; terrain only, so it stays valid)
    std::unordered_map<long long, std::pair<int, int>> cutCache;
    int lastProgress = 0;    // (the last frame with damage, or a unit nearer than ever to its enemy or goal)
    int lastDamage = 0;      // (the last frame with damage)
    bool stalemate = false;  // (a fight of placed units ended: no progress for kStalemateFrames)
    int weaponFor(const Unit& a, const Unit& b) const;
    int level(int owner, int upgrade) const;
    int weaponDamage(int weapon, int owner) const;  // (8 fractional bits)
    int rangeBonus(const Unit& u, int weapon) const;
    bool speedUpgraded(const Unit& u) const;
    int upgradedSpeed(int speed, bool top) const;
    int haltDistance(const Unit& u) const;
    bool hasWeapon(const Unit& u) const;
    const UnitData& weaponUnit(int type) const;  // its own data, or its turret's (or the Reaver's Scarab)
    void medic(int i);
    void startAttack(int i, const Unit& target, const WeaponData& wd, bool turret);
    void outlineSpot(const Unit& u, const Unit& target, int& x, int& y) const;
    bool splash(Bullet& b, int amount);
    void damage(int target, int source, int weapon, int amount, int divisor);
    void kill(int i, bool credit = true);  // credit: a kill for the unit that hit it last

private:
    bool move(Unit& u, bool last);  // true: held by a unit that moves later this frame (try again)
    int blocker(const Unit& u, int x, int y) const;
    void react(Unit& u, const Unit& c, int tx, int ty);
    bool shortPath(Unit& u, int consider, bool movers);
    void runAnim(Anim& a, int unit, int bullet);
};

// Mining (mining.cpp): the bases of the map, a game with a player at each of them, and its measures.
std::vector<MiningBase> miningBases(const Tables& t, const MapInfo& map, const MiningOptions& o);
std::vector<Race> baseRaces(const Tables& t, const MapInfo& map, const std::vector<MiningBase>& bases,
                             const MiningOptions& o);
void setupMining(World& w, const std::vector<MiningBase>& bases, const std::vector<Race>& races, int workers,
                 const MiningOptions& o);
MiningRun miningRun(const World& w, int player, Race race, int workers, const MiningOptions& o);
PlayerState playerState(const World& w, int player);

// Combat (combat.cpp): the two armies, whether the fight is over (after a frame), and its result.
bool setupCombat(World& w, const CombatOptions& o, std::string& error);
bool combatOver(World& w, int lastFrame);
CombatResult combatResultOf(const World& w);

int direction(int dx, int dy);             // 0: up, 64: right (256 a turn)
int dirX(int dir);                         // the unit vector of a direction (8 fractional bits)
int dirY(int dir);
int turnToward(int from, int to, int rate);  // a direction turned toward another by at most rate

}  // namespace sim
