// SPDX-License-Identifier: MIT
#pragma once
#include "air.h"
#include "contours.h"
#include "creep.h"
#include "definitions.h"
#include "economy.h"
#include "local_route.h"
#include "motion.h"
#include "path_pool.h"
#include "placement.h"
#include "scene.h"
#include "spatial.h"
#include "vitals.h"
#include <array>
#include <deque>
#include <list>
#include <optional>

namespace mapeditor::simulation {
struct UnitHandle {
  int index = -1;
  std::uint32_t generation = 0;
  bool operator==(const UnitHandle &) const = default;
};
enum class HarvestOrder { Gather, Minerals, Gas };
inline constexpr int harvestOrderCode(HarvestOrder order) {
  return order == HarvestOrder::Gas ? 81
       : order == HarvestOrder::Minerals ? 85 : 79;
}
struct UnitCommand {
  int order = 6;
  Point position;
  UnitHandle target;
  int rememberedType = 228;
  bool automaticAttack = false;
};
struct UnitState {
  int rememberedType = 228;
  int groundCooldown = 0, airCooldown = 0;
  bool attackActive = false;
  bool autoAttackTargeting = false;
  bool attackPointAdmission = false;
  int lastAttackingPlayer = 8;
  UnitHandle retaliationTarget;
  std::deque<UnitCommand> queuedCommands;
  bool queuedUserOverride = false;
  UnitHandle handle;
  UnitVitals vitals;
  std::uint64_t birth = 0;
  int type = 0, owner = 0, order = 0, orderState = 0, orderTimer = 0,
      mainTimer = 0;
  int secondaryOrder = 23, secondaryState = 0, secondaryTimer = 0;
  int remainingBuildTime = 0, constructionGain = 0;
  int constructionPreviousHp = 0;
  std::vector<int> buildQueue;
  UnitHandle buildUnit;
  Point rallyPosition;
  UnitHandle rallyTarget;
  std::array<int, 4> larvaSpawnWeights{};
  Point followPosition;
  int followTimer = 0;
  int resources = 0, cargo = 0, wireframeRandomizer = 0, resourceAnimation = 0;
  Point position, targetPosition, moveTargetPosition;
  std::optional<Point> interruptedMoveGoal;
  UnitHandle target, moveTargetUnit, connected;
  UnitHandle pathTarget;
  UnitHandle mineral, gatherer, slideObstacle, collisionObstacle;
  UnitHandle resourceTarget;
  UnitHandle pendingHarvest;
  std::optional<UnitCommand> harvestContinuation;
  std::optional<UnitCommand> landingContinuation;
  HarvestOrder pendingHarvestOrder = HarvestOrder::Gather;
  std::deque<UnitHandle> mineralQueue;
  bool gathering = false;
  bool checkHarvestOverlap = false, collisionPending = false;
  bool landingArrivalPending = false;
  bool gasLanding = false;
  bool consumedByConstruction = false;
  bool collisionRouteReset = false;
  bool escapingOverlap = false;
  bool overlapStopPending = false;
  bool overlapStopRecovery = false;
  bool overlapOrderChanged = false;
  bool overlapFreshOrder = false;
  // Explicit command travel can cross an escape segment without braking.
  // A command received during a recheck latches that intent for its new path.
  bool overlapDirectRoute = true;
  bool overlapResumeDirect = false;
  bool routeContinuesAtWaypoint = false;
  std::optional<int> overlapRegion;
  Point overlapReturnTarget;
  int collisionWait = 0;
  bool creepGrowth = false, spawnLarvae = false;
  int creepTimer = 0, larvaTimer = 0;
  int cargoAmount = 0, routePlanningFrames = 1, failedRouteFrames = 0;
  // New units need two movement updates before a route can be consumed.
  int movementPreparation = 2;
  bool userMovementPending = false;
  bool initialRoutePending = false;
  std::optional<std::size_t> cargoImage;
  bool completed = false, visible = false, dead = false, walking = false;
  bool animatedDeath = false;
  bool psiActive = false;
  bool psiDisabled = false;
  int psiSprite = -1;
  bool planRequested = false, immovable = false;
  bool hallucinated = false;
  int removeTimer = 0;
  Sprite sprite;
  std::optional<Motion> motion;
  std::optional<Motion::Inertia> inactiveInertia;
  AirState air;
  std::vector<Point> pendingRoute;
  std::optional<LocalRouteResult> path;
  int pathSlot = -1;
};
struct WorldMap {
  Terrain terrain;
  Regions regions;
  Contours contours;
  int tileset = 0;
  explicit WorldMap(Terrain value, int tilesetIndex = 0)
      : terrain(std::move(value)), regions(terrain), contours(terrain),
        tileset(tilesetIndex) {
    if (tileset < 0 || tileset > 7)
      throw std::out_of_range("Invalid world tileset");
  }
};
struct SpriteEffect {
  int type = 0, owner = 0, elevation = 4;
  bool hidden = false;
  Point position;
  Sprite sprite;
};
struct ProjectileFlight {
  Point raw, goal, velocity;
  int speed = 0, travelHeading = 0, turnRate = 0, lifetime = 0;
  bool active = false, turning = true;
};
struct Projectile {
  int weapon = 0, owner = 0, elevation = 0;
  Point position;
  UnitHandle source, target;
  bool dying = false, missed = false;
  std::optional<ProjectileFlight> flight;
  Sprite sprite;
};
struct RenderedImage {
  Point position;
  int image = 0, frame = 0, owner = 0, modifier = 0;
  bool flip = false;
};
enum class WorldStart { Ready, MapImport };
class World {
public:
  World(std::shared_ptr<const Definitions>, std::shared_ptr<const WorldMap>,
        std::uint32_t seed = 42, WorldStart start = WorldStart::Ready);
  // Copying a world copies value state; definitions and immutable terrain are
  // shared. Handles remain valid within either independently advancing copy.
  UnitState *get(UnitHandle);
  const UnitState *get(UnitHandle) const;
  std::vector<RenderedImage> renderUnits(std::span<const UnitHandle>) const;
  const std::vector<std::optional<UnitState>> &units() const { return units_; }
  const std::list<int> &visibleUnits() const { return visible_; }
  const PathPool &pathPool() const { return pathPool_; }
  const std::list<int> &hiddenUnits() const { return hidden_; }
  const std::list<int> &deadUnits() const { return dead_; }
  const std::vector<std::optional<SpriteEffect>> &effects() const {
    return effects_;
  }
  const std::list<int> &activeEffects() const { return activeEffects_; }
  const std::array<std::optional<Projectile>, 100> &projectiles() const {
    return projectiles_;
  }
  const std::list<int> &activeProjectiles() const { return activeProjectiles_; }
  int addSprite(int type, Point position, int owner);
  std::uint32_t randomState() const { return random_.state(); }
  const Creep &creep() const { return creep_; }
  const Definitions &definitions() const { return *definitions_; }
  const WorldMap &map() const { return *map_; }
  int frame() const { return frame_; }
  std::uint8_t exploredAt(Point tile) const;
  std::uint8_t visibleAt(Point tile) const;
  Economy &economy() { return economy_; }
  const Economy &economy() const { return economy_; }
  Population population(int owner) const;
  // Executes a caller-planned route. Terrain-edge planning and collision
  // response are separate, unfinished components; this is not a path query.
  void followRoute(UnitHandle, std::vector<Point>);
  // Movement can replace the current intent or wait in the command queue.
  // Planning starts after the order is processed,
  // using the position accepted by that frame's previous movement.
  void move(UnitHandle, Point destination, bool queued = false);
  void rememberedRightClick(UnitHandle, Point, int type, bool queued = false);
  UnitHandle rememberedTarget(int owner, Point, int type) const;
  void follow(UnitHandle, UnitHandle target, bool queued = false);
  void attack(UnitHandle, UnitHandle target, bool queued = false);
  void attackMove(UnitHandle, Point, bool queued = false);
  void guard(UnitHandle, Point, bool queued = false);
  void processAttackMove(UnitState &);
  bool allied(int owner, int other) const;
  void setAlliance(int owner, int other, bool enabled);
  UnitHandle acquireTarget(UnitHandle) const;
  // Experimental position-search queries for completed ground workers.
  bool workerPositionBlocked(UnitHandle, Point) const;
  UnitHandle mineralDepot(UnitHandle) const;
  // Warm immutable terrain geometry before making parallel search snapshots.
  // Does not advance frames, consume randomness or change unit state.
  void prepareNavigation(UnitHandle);
  // Untouched frame-zero ground workers only; retains orders and facing.
  void relocateWorkerForSearch(UnitHandle, Point);
  void harvest(UnitHandle worker, UnitHandle mineral,
               HarvestOrder = HarvestOrder::Gather, bool queued = false);
  // Returns false when the worker has no resource cargo to return.
  bool returnCargo(UnitHandle worker, bool queued = false, UnitHandle depot = {});
  int deliveredMinerals(int owner) const {
    return deliveredMinerals_.at(owner);
  }
  void advance(int frames = 1);
  void setResources(UnitHandle, int amount);
  void setHitpoints(UnitHandle, int amount);
  void setHallucinated(UnitHandle);
  void prepareImportedMap();
  // Starts in DAT human idle; player-controller completion is not modeled yet.
  UnitHandle createFinished(int type, Point position, int owner,
                            bool relocate = true, bool initial = false);
  std::vector<UnitHandle> startingUnits(int race, Point position, int owner);
  void remove(UnitHandle);
  bool train(UnitHandle producer, int type);
  bool cancelQueue(UnitHandle producer, int slot);
  bool cancelConstruction(UnitHandle building);
  bool canRally(UnitHandle) const;
  bool rallyPoint(UnitHandle, Point);
  bool rallyUnit(UnitHandle, UnitHandle);
  bool morph(UnitHandle larva, int type);
  bool build(UnitHandle worker, int type, Point tile, int order);

private:
  std::shared_ptr<const Definitions> definitions_;
  std::shared_ptr<const WorldMap> map_;
  SpriteRows spriteRows_;
  SpatialBounds spatial_;
  PathPool pathPool_;
  AirField air_;
  struct GroundCacheEntry {
    std::array<int, 4> shape;
    std::shared_ptr<const GroundGeometry> geometry;
  };
  std::vector<std::uint8_t> groundCreep_;
  std::vector<GroundCacheEntry> groundCache_;
  const GroundGeometry &terrainGeometry(const dat::Extents &);
  std::array<std::uint16_t, 12> alliances_{};
  Random random_;
  Creep creep_;
  std::vector<std::optional<UnitState>> units_;
  std::vector<std::uint32_t> generations_;
  // A released slot can supply residual speed to its next occupant.
  std::vector<std::optional<Motion::Inertia>> releasedInertia_;
  std::deque<int> free_;
  std::list<int> visible_, hidden_, dead_;
  std::vector<std::optional<SpriteEffect>> effects_;
  std::list<int> activeEffects_, freeEffects_;
  int allocatedEffects_ = 0;
  std::uint64_t nextBirth_ = 0;
  int frame_ = 0, orderCycle_ = 8, secondaryCycle_ = 150;
  std::array<int, 12> deliveredMinerals_{};
  Economy economy_;
  std::vector<std::uint8_t> explored_, visibleTo_;
  int visionRefreshIn_ = 1;
  void advanceVitals(UnitState &);
  void refreshPowerState();
  void releasePsiField(UnitState &);
  void processDeath(UnitState &);
  void revealSight(const UnitState &);
  void revealSightAt(Point, int owner, int range, bool airborne);
  std::array<std::optional<Projectile>, 100> projectiles_;
  std::list<int> activeProjectiles_;
  void useWeapon(UnitState &, int);
  void advanceProjectiles();
  iscript::HostAction projectileEffect(int, const iscript::Instruction &,
                                       const SpriteImage &);
  void processHarvest(UnitState &);
  void processAttack(UnitState &);
  void processTower(UnitState &);
  std::vector<UnitHandle> targetCandidates(UnitHandle, bool includeRetaliation = true) const;
  UnitHandle acquireTarget(UnitHandle, bool includeRetaliation) const;
  int attackPriority(const UnitState &, const UnitState &) const;
  void reacquireAttackTarget(UnitState &);
  void beginAttack(UnitHandle, UnitHandle, bool queued, bool automatic);
  void autoAttack(UnitHandle, UnitHandle);
  void prepareOrderTransition(UnitState &);
  void brakeForCombat(UnitState &, bool initial);
  void meleeHit(UnitState &);
  void weaponHit(int weapon, UnitHandle target, int impactHeading,
                 UnitHandle attacker);
  void respondToDamage(UnitState &, UnitHandle attacker);
  void updateDamageOverlay(UnitState &);
  bool processGuardRetaliation(UnitState &);
  bool onTargetPerimeter(const UnitState &, const UnitState &) const;
  bool interruptHarvest(UnitState &, UnitCommand);
  bool activateQueuedCommand(UnitState &);
  bool deferLockedCommand(UnitState &, UnitCommand);
  void releaseMineral(UnitState &);
  void releasePathSlot(UnitState &);
  void discardPath(UnitState &);
  void recordPath(UnitState &, const LocalRouteResult &);
  void recordEscape(UnitState &);
  void moveSpatial(const UnitState &, bool initial = false);
  void moveSpatial(const UnitState &, Point previous);
  UnitHandle freeMineral(const UnitState &, bool requireFree = true,
                         int range = 256) const;
  void processLarva(UnitState &);
  void processSecondary(UnitState &);
  void processProduction(UnitState &);
  void processConstruction(UnitState &);
  void beginConstruction(UnitState &, Point center, int order);
  void processWarp(UnitState &);
  bool constructionSiteFits(const UnitState &, int type, Point center) const;
  UnitHandle createConstruction(int type, Point, int owner);
  void applyRally(UnitState &, const UnitState &);
  void processMorph(UnitState &);
  void morphType(UnitState &, int type, int image);
  void notifyOverlap(UnitState &);
  bool clearOverlap(const UnitState &, Point) const;
  bool productionSupply(int owner, int type) const;
  UnitHandle createUnit(int type, Point, int owner, bool relocate, bool initial,
                        bool finished);
  UnitHandle completeUnit(UnitHandle, bool relocate, bool initial);
  void moveForHarvest(UnitState &, UnitHandle, int planningFrames = 0);
  std::vector<CollisionBody> bodies(int ignore = -1) const;
  std::vector<std::uint8_t> occupancy(int ignore = -1) const;
  UnitHandle larva(UnitHandle depot);
  void replaceImages(UnitState &, int image);
  iscript::HostAction effect(UnitHandle, const iscript::Instruction &,
                             const SpriteImage &);
  iscript::HostAction effectSprite(int, const iscript::Instruction &,
                                   const SpriteImage &);
  int createEffect(int, Point, int, int, UnitHandle = {});
  bool spawnEffect(const iscript::Instruction &, const SpriteImage &, Point,
                   int, UnitHandle = {});
};
// Read-only search sampling keeps handle validation inline. Mutable access
// remains available through World::get.
inline const UnitState *peekUnit(const World &world, UnitHandle handle) {
  const auto &unitRecords = world.units();
  if (handle.index < 0 || handle.index >= int(unitRecords.size()))
    return nullptr;
  const auto &slot = unitRecords[handle.index];
  return slot && slot->handle == handle ? &*slot : nullptr;
}
} // namespace mapeditor::simulation
