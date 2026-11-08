// SPDX-License-Identifier: MIT
#pragma once
#include "dat.h"
#include "geometry.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mapeditor::simulation {
inline int turnToward(int from, int to, int limit) {
  auto delta = signedHeading(to - from);
  return signedHeading(from + std::clamp(delta, -limit, limit));
}
inline int fixedProduct(int a, int b) {
  auto product = std::int64_t(a) * b;
  // Arithmetic-floor division is explicit, including negative motion.
  return int(product >= 0 ? product / 256 : -((-product + 255) / 256));
}
inline bool samePoint(Point a, Point b) { return a.x == b.x && a.y == b.y; }

// Path-free movement dynamics. Path construction and collision resolution are
// separate callers; no reference traces or game-engine objects enter this
// state.
class Motion {
public:
  struct Inertia {
    int speed = 0, travelHeading = 0;
    Point velocity;
  };
  Inertia inertia() const { return {speed_, travelHeading_, velocity_}; }
  // Activation selects a new facing while an earlier type's speed can survive.
  void inheritInertia(const Inertia &state, bool newlyAllocated = false) {
    if (!stopped() || !route_.empty() || state.speed < 0 || state.speed > 65536)
      throw std::invalid_argument("Invalid inherited movement inertia");
    speed_ = state.speed;
    nextSpeed_ = 0;
    initialInertiaFrame_ = newlyAllocated && !flying_;
    auto vector = directionVector(facing_);
    velocity_ = {fixedProduct(speed_, vector.x),
                 fixedProduct(speed_, vector.y)};
  }
  Motion(Point origin, int initialHeading, dat::Flingy physics,
         bool flying = false)
      : raw_{}, physics_(physics), facing_(signedHeading(initialHeading)),
        travelHeading_(facing_), anchor_(origin), flying_(flying) {
    validate(origin);
    raw_ = {origin.x * 256, origin.y * 256};
    contactTravel_ = collisionTravel_ = facing_;
    if (physics.acceleration <= 0 || physics.acceleration > 65535 ||
        physics.turnRate <= 0 || physics.turnRate > 255 || !physics.speed ||
        physics.speed > 65536 || (physics.control != 0 && physics.control != 2))
      throw std::runtime_error("Unsupported movement physics");
  }
  Point position() const {
    return airPosition_.value_or(Point{raw_.x / 256, raw_.y / 256});
  }
  Point rawPosition() const { return raw_; }
  // A committed ground landing aligns the fractional anchor while retaining
  // velocity for the following movement update and type transition.
  void alignToPixel() {
    if (flying_)
      throw std::logic_error("Ground landing alignment applied to an air unit");
    auto point = position();
    raw_ = {point.x * 256, point.y * 256};
  }
  // Position-search seeds have not consumed a movement update or a route.
  void relocateInitial(Point position) {
    validate(position);
    if (flying_ || !stopped() || !route_.empty() || moving_ || speed_ ||
        velocity_.x || velocity_.y)
      throw std::runtime_error(
          "Active movement relocation is not implemented yet");
    raw_ = {position.x * 256, position.y * 256};
  }
  // Flying sprites use bounded pixel coordinates even while their fractional
  // motion continues past an edge. The world commits this position per frame.
  void setAirPixelPosition(Point position) {
    if (!flying_)
      throw std::logic_error("Air pixel position applied to a ground unit");
    validate(position);
    airPosition_ = position;
  }
  void applyAirDisplacement(Point delta) {
    if (!flying_)
      throw std::logic_error("Air displacement applied to a ground unit");
    raw_.x += delta.x;
    raw_.y += delta.y;
  }
  void acceptNearbyAirGoal() {
    if (!flying_)
      throw std::logic_error("Air goal applied to a ground unit");
    route_.assign(1, position());
    cursor_ = 0;
  }
  Point velocity() const { return velocity_; }
  int speed() const { return speed_; }
  int nextSpeed() const { return nextSpeed_ < 0 ? speed_ : nextSpeed_; }
  int facing() const { return facing_; }
  bool steering() const { return turning_; }
  int travelHeading() const { return travelHeading_; }
  int acceptedTravelHeading() const {
    // A collision wait keeps the last accepted direction while the rejected
    // step's heading remains available for collision recovery.
    if (scriptDriven() && restoreTerrainTravel_)
      return terrainAccepted_;
    if (scriptDriven() &&
        (phase_ == Phase::TerrainReplan ||
         (phase_ == Phase::CollisionTurn && firstCollisionTurn_ &&
          collisionReplanTurn_)))
      return collisionTravel_;
    return waitingForBody() || collisionPaused() ? collisionTravel_
                                                 : travelHeading_;
  }
  // A fresh rejected step exposes its pre-step direction to other bodies.
  // The next waiting update refreshes that direction without translating.
  int collisionStepHeading() const {
    return waitingForBody() ? previousTravel_ : travelHeading_;
  }
  int previousTravelHeading() const { return previousTravel_; }
  int collisionTravelHeading() const { return collisionTravel_; }
  bool collisionTurning() const { return phase_ == Phase::CollisionTurn; }
  bool firstCollisionTurn() const { return firstCollisionTurn_; }
  std::int64_t stopDistance() const {
    if (scriptDriven())
      return 0;
    const auto speed = std::int64_t(nextSpeed());
    return speed == physics_.speed ? physics_.haltDistance
                                  : speed * speed / (2 * physics_.acceleration);
  }
  Point stopTarget() const {
    if (brakeTravel_ && !samePoint(position(), waypoint()))
      return waypoint();
    if (!moving_ || scriptDriven())
      return position();
    const int direction = waitingForBody() ? previousTravel_
                          : collisionPaused() ? collisionTravel_
                                              : travelHeading_;
    const auto vector = directionVector(direction);
    const auto distance = stopDistance();
    auto project = [&](int raw, int component) {
      const auto product = std::int64_t(component) * distance;
      const auto offset = product >= 0 ? product / 256
                                     : -((-product + 255) / 256);
      const auto point = (std::int64_t(raw) + offset) / 256;
      if (point < std::numeric_limits<int>::min() ||
          point > std::numeric_limits<int>::max())
        throw std::out_of_range("Stop target exceeds supported coordinates");
      return int(point);
    };
    return {project(raw_.x, vector.x), project(raw_.y, vector.y)};
  }
  int desiredHeading() const { return desired_; }
  void faceOnArrival(std::optional<Point> target, bool admitted = false,
                     std::optional<int> acceptedDirection = {}) {
    if (target)
      validate(*target);
    if (arrivalTargetLocked_ && target && !admitted)
      return;
    arrivalTarget_ = target;
    arrivalDirection_ = acceptedDirection
                            ? std::optional<int>(signedHeading(*acceptedDirection))
                            : std::nullopt;
    arrivalTargetLocked_ = admitted && target.has_value();
  }
  int turnRate() const { return physics_.turnRate; }
  void setTopSpeed(int speed) {
    if (speed < 0 || speed > 65536)
      throw std::invalid_argument("Invalid movement speed limit");
    physics_.speed = speed;
  }
  bool scriptDriven() const { return physics_.control == 2; }
  bool moving() const { return moving_; }
  void setRouteAge(int frames) {
    if (frames < 0)
      throw std::invalid_argument("Invalid route age");
    pathAge_ = frames;
  }
  bool awaitingFailedDeparture() const { return failedMovingDeparture_; }
  void acceptMovement() { failedMovingDeparture_ = false; }
  bool turningForRoute() const {
    return phase_ == Phase::RouteTurn || phase_ == Phase::CollisionTurnSetup ||
           phase_ == Phase::CollisionTurn;
  }
  bool waitingForBody() const { return phase_ == Phase::WaitingBody; }
  bool retainsBodyRestartWalk() const { return bodyRestartWalk_; }
  bool clearanceBodyWait() const { return waitingForBody() && bodyWaitClearance_; }
  bool turningBodyWait() const {
    return waitingForBody() && (bodyWaitTurning_ || bodyWaitScriptTurn_);
  }
  bool collisionPaused() const { return phase_ == Phase::CollisionPause; }
  bool staticCollisionPaused() const {
    return collisionPaused() && collisionResume_ == Phase::CollisionTurnSetup;
  }
  bool staticCollisionWaitReady() const {
    return scriptDriven() && staticCollisionPaused() && collisionPause_ == 1;
  }
  void awaitMovingObstacle() {
    if (!staticCollisionWaitReady())
      throw std::logic_error("Moving obstacle requires static collision recovery");
    collisionResume_ = Phase::WaitingBody;
    collisionPause_ = 1;
    bodyWaitSpeed_ = nextSpeed();
    bodyWaitClearance_ = bodyWaitTurning_ = bodyWaitKeepsStep_ = false;
    bodyWaitScriptTurn_ = turning_;
    bodyWaitSegmentTurn_ = stepWaypointChanged_;
    bodyRecoveryDelay_ = false;
  }
  bool movingCollisionPaused() const {
    return collisionPaused() && collisionResume_ == Phase::RerouteMoving;
  }
  bool movingCollisionRetryReady() const {
    return movingCollisionPaused() && movingPausePrepared_;
  }
  bool preparedBodyRetryReady() const {
    return scriptDriven() && collisionPaused() && !bodyRecoveryDelay_ &&
           collisionResume_ == Phase::WaitingBody;
  }
  bool overlapBodyRetryReady() const {
    return scriptDriven() && collisionPaused() && bodyRecoveryDelay_ &&
           collisionResume_ == Phase::WaitingBody;
  }
  void retryPreparedBodyWait() {
    if (!preparedBodyRetryReady() && !overlapBodyRetryReady())
      throw std::logic_error("Body preparation is not ready for a clearance retry");
    collisionPause_ = 0;
    collisionDebt_ = std::min(255, collisionDebt_ + 1);
    phase_ = Phase::WaitingBody;
    resumeBodyWait();
  }
  void holdPreparedBodyRetry(const Motion &trial) {
    if (!scriptDriven() || (!collisionPaused() && !waitingForBody()))
      throw std::logic_error("A blocked preparation retry requires a script body wait");
    // A rejected retry has consumed the waiting frame. Its stopped body can
    // finish steering while the saved collision step remains available.
    speed_ = nextSpeed_ = 0;
    velocity_ = {};
    moving_ = false;
    desired_ = trial.desired_;
    const auto priorFacing = facing_;
    facing_ = turnToward(facing_, desired_, physics_.turnRate);
    updatesHeading_ = priorFacing != facing_;
    bodyWaitScriptTurn_ |= updatesHeading_;
    travelHeading_ = collisionTravel_ = trial.travelHeading_;
    turning_ = facing_ != desired_ || travelHeading_ != desired_;
    collisionPause_ = 0;
    phase_ = Phase::WaitingBody;
  }
  void acceptDeferredBodyWait() {
    if (!scriptDriven() || phase_ != Phase::RerouteMoving)
      throw std::logic_error("Deferred body waiting requires a scripted replan");
    phase_ = Phase::WaitingBody;
  }
  void retryCollision() {
    if (!collisionPaused())
      throw std::logic_error("Motion is not in a collision pause");
    nextSpeed_ = speed_;
    collisionPause_ = 0;
    collisionDebt_ = std::min(255, collisionDebt_ + 1);
    phase_ = Phase::Travel;
  }
  void waitForBody(Point previousRaw, int previousFacing,
                   int previousNextSpeed, bool delayForRecovery = false) {
    bodyRecoveryDelay_ = delayForRecovery;
    bodyWaitScriptTurn_ =
        scriptDriven() && (turning_ ||
                           (previousMoving_ && facing_ != previousFacing));
    bodyWaitSegmentTurn_ = scriptDriven() && stepWaypointChanged_;
    bodyWaitClearance_ = false;
    bodyWaitSpeed_ = scriptDriven() ? previousNextSpeed : speed_;
    bodyWaitTurning_ = !scriptDriven() && travelHeading_ != desired_;
    bodyWaitStep_ = {raw_.x - previousRaw.x, raw_.y - previousRaw.y};
    // A turn/braking preview retains its pending step through a body wait.
    // Ordinary acceleration resumes its earlier travel update instead.
    bodyWaitKeepsStep_ = !scriptDriven() && (speed_ < previousNextSpeed ||
                                             travelHeading_ != previousTravel_);
    if (!scriptDriven() && !bodyWaitTurning_ && previousNextSpeed > bodyWaitSpeed_) {
      bodyWaitSpeed_ = previousNextSpeed;
      auto retry = waypointStep(previousRaw, travelHeading_, bodyWaitSpeed_);
      bodyWaitStep_ = {retry.x - previousRaw.x, retry.y - previousRaw.y};
    }
    raw_ = previousRaw;
    // Rejected body steering remains pending; pixel travel alone does not
    // restart a completed body turn.
    turning_ = turning_ || facing_ != previousFacing;
    facing_ = previousFacing;
    nextSpeed_ = previousNextSpeed;
    collisionTravel_ = turning_ ? previousTravel_ : travelHeading_;
    collisionHistory_ = true;
    collisionDebt_ = std::min(255, collisionDebt_ + 1);
    moving_ = previousMoving_;
    updatesHeading_ = false;
    phase_ = Phase::WaitingBody;
    if (delayForRecovery && scriptDriven() && pathAge_ < 7) {
      collisionResume_ = Phase::WaitingBody;
      collisionPause_ = std::max(1, 6 - pathAge_);
      phase_ = Phase::CollisionPause;
    }
  }
  void waitForClearance(Point previousRaw, int previousFacing,
                        int previousNextSpeed) {
    int collisionSpeed = speed_;
    waitForBody(previousRaw, previousFacing, previousNextSpeed);
    bodyWaitSpeed_ = collisionSpeed;
    bodyWaitKeepsStep_ = false;
    bodyWaitClearance_ = true;
  }
  void resumeBodyWait() {
    if (!waitingForBody())
      throw std::logic_error("Motion is not waiting for a body");
    nextSpeed_ = bodyWaitSpeed_;
    if (bodyWaitTurning_) {
      travelHeading_ = collisionTravel_;
      phase_ = Phase::Travel;
      return;
    }
    phase_ = bodyWaitKeepsStep_ ? Phase::BodyRetry : Phase::Travel;
  }
  void acceptBlockedWaitTurn(const Motion &trial) {
    if (!waitingForBody())
      throw std::logic_error("Blocked steering requires a body wait");
    // A body turn pending before the rejected step continues while
    // translation waits. Travel stays at the accepted angle until that turn
    // finishes; a completed turn rolled back by collision stays completed.
    if (bodyWaitScriptTurn_) {
      updatesHeading_ = facing_ != trial.facing_;
      facing_ = trial.facing_;
      desired_ = trial.desired_;
      turning_ = trial.turning_;
      if (!turning_) {
        travelHeading_ = collisionTravel_ = trial.travelHeading_;
      }
      return;
    }
    if (bodyWaitTurning_) {
      updatesHeading_ = facing_ != trial.facing_;
      facing_ = trial.facing_;
      desired_ = trial.desired_;
      travelHeading_ = collisionTravel_ = trial.travelHeading_;
      turning_ = trial.turning_;
      return;
    }
    if (!turning_ || trial.travelHeading_ != travelHeading_ ||
        travelHeading_ == collisionTravel_ ||
        (scriptDriven() && travelHeading_ == trial.desired_ && !bodyWaitSegmentTurn_))
      return;
    const int acceptedFacing =
        turnToward(facing_, travelHeading_, physics_.turnRate);
    updatesHeading_ = facing_ != acceptedFacing;
    facing_ = acceptedFacing;
    if (facing_ == travelHeading_)
      bodyWaitSegmentTurn_ = false;
  }
  void prepareBodyWait(Point previousRaw, int previousFacing,
                       int previousNextSpeed) {
    waitForBody(previousRaw, previousFacing, previousNextSpeed);
    // Entering this wait already consumed the rejected movement frame.
    const int preparationAge = scriptDriven() ? 7 : 6;
    if (pathAge_ < preparationAge) {
      collisionResume_ = Phase::WaitingBody;
      collisionPause_ = preparationAge - pathAge_;
      phase_ = Phase::CollisionPause;
      if (!scriptDriven())
        moving_ = false;
    }
  }
  void waitForFailedDeparture(Point previousRaw, int previousFacing,
                            int previousNextSpeed) {
    const int attemptedSpeed = speed_;
    const Point attemptedStep{raw_.x - previousRaw.x, raw_.y - previousRaw.y};
    waitForBody(previousRaw, previousFacing, previousNextSpeed);
    bodyWaitSpeed_ = attemptedSpeed;
    bodyWaitStep_ = attemptedStep;
    bodyWaitKeepsStep_ = false;
    collisionResume_ = Phase::WaitingBody;
    collisionPause_ = 1;
    phase_ = Phase::CollisionPause;
  }
  int collisionDebt() const { return collisionDebt_; }
  void expireBodyWait(bool includeMovingObstacles) {
    if (!waitingForBody())
      throw std::logic_error("Only a blocked body wait can expire");
    speed_ = nextSpeed_ = 0;
    velocity_ = {};
    collisionTravel_ = travelHeading_;
    phase_ = includeMovingObstacles ? Phase::RerouteMoving : Phase::Reroute;
    expiredBodyWait_ = true;
  }
  void prepareRouteFrame() {
    stepWaypointChanged_ = false;
    const bool initializingInertia = std::exchange(initialInertiaFrame_, false);
    // A newly activated animation-driven body can retain its old current
    // speed while the pending speed is already zero. A stationary legality
    // update accepts that pending speed without taking a travel step.
    if (!initializingInertia && scriptDriven() && phase_ == Phase::Stopped && nextSpeed_ >= 0) {
      speed_ = nextSpeed_;
      nextSpeed_ = -1;
      auto vector = directionVector(travelHeading_);
      velocity_ = {fixedProduct(speed_, vector.x),
                   fixedProduct(speed_, vector.y)};
    }
    previousTravel_ = travelHeading_;
    previousMoving_ = moving_;
    slidingStep_ = shortenedStep_ = false;
    updatesHeading_ = false;
  }
  void acceptedGroundStep() {
    collisionDebt_ = std::max(0, std::min(collisionDebt_, 3) - 1);
  }
  bool slidingStep() const { return slidingStep_; }
  bool shortenedStep() const { return shortenedStep_; }
  Point slideVector() const { return directionVector(slideHeading_); }
  Point waypointStep(Point from, int direction, int step) const {
    auto goal = waypoint();
    Point target{goal.x * 256, goal.y * 256};
    if (distance({target.x - from.x, target.y - from.y}) <= step)
      return target;
    auto vector = directionVector(direction);
    return {from.x + fixedProduct(step, vector.x),
            from.y + fixedProduct(step, vector.y)};
  }
  Point slideStep(Point from, int step) const {
    return waypointStep(from, slideHeading_, step);
  }
  void finishSlide(Point raw, bool resume) {
    raw_ = raw;
    auto goal = waypoint();
    desired_ = heading({goal.x - position().x, goal.y - position().y});
    if (resume && pathAge_ >= 150)
      collisionTravel_ = travelHeading_;
    phase_ = resume
                 ? (pathAge_ >= 150 ? Phase::CollisionTurnSetup : Phase::Travel)
                 : Phase::SlideFree;
    turning_ = true;
  }
  void blockedSlide(Point raw) {
    raw_ = raw;
    desired_ = facing_;
    phase_ = Phase::SlideBlocked;
    collisionHistory_ = true;
  }
  bool updatesHeading() const { return updatesHeading_; }
  void contactBlocked(Point previousRaw, int previousFacing, Point target) {
    raw_ = previousRaw;
    facing_ = previousFacing;
    speed_ = nextSpeed_ = 0;
    velocity_ = {};
    holdTarget_ = target;
    contactTravel_ = previousTravel_;
    collisionHistory_ = false;
    restoreContactTravel_ = true;
    phase_ = Phase::Contact;
    updatesHeading_ = false;
  }
  void clipStep(Point raw) {
    raw_ = raw;
    if (phase_ == Phase::Arrived)
      phase_ = Phase::Travel;
  }
  void blocked(Point previousRaw, int previousFacing,
               int previousNextSpeed = 0) {
    raw_ = previousRaw;
    facing_ = previousFacing;
    updatesHeading_ = false;
    nextSpeed_ = previousNextSpeed;
    collisionTravel_ = previousTravel_;
    collisionHistory_ = true;
    moving_ = previousMoving_;
    collisionDebt_ = std::min(255, collisionDebt_ + 1);
    if (pathAge_ < 7 && collisionDebt_ > 2)
      moving_ = false;
    collisionPause_ = std::max(1, 7 - pathAge_);
    collisionResume_ = Phase::CollisionTurnSetup;
    phase_ = Phase::CollisionPause;
    if (pathAge_ >= 7) {
      speed_ = nextSpeed_ = 0;
      velocity_ = {};
      phase_ = Phase::CollisionTurnSetup;
    }
  }
  void blockedWaypoint(Point previousRaw, int previousFacing,
                       int previousNextSpeed, bool includeMoving = false) {
    // A rejected step rolls back the pose, but an attempted body turn still
    // belongs to the recovery. Completed turns stay complete on an unchanged leg.
    turning_ = turning_ || facing_ != previousFacing;
    raw_ = previousRaw;
    facing_ = previousFacing;
    nextSpeed_ = previousNextSpeed;
    collisionTravel_ = previousTravel_;
    collisionHistory_ = true;
    collisionDebt_ = std::min(255, collisionDebt_ + 1);
    moving_ = previousMoving_;
    updatesHeading_ = false;
    collisionResume_ = includeMoving ? Phase::RerouteMoving : Phase::Reroute;
    if (scriptDriven() && pathAge_ < 7 && collisionDebt_ > 2)
      moving_ = false;
    if (scriptDriven() && includeMoving) {
      bodyWaitSpeed_ = previousNextSpeed;
      bodyWaitClearance_ = bodyWaitTurning_ = bodyWaitKeepsStep_ = false;
      bodyWaitScriptTurn_ = false;
      bodyWaitSegmentTurn_ = stepWaypointChanged_;
    }
    movingPausePrepared_ = false;
    if (pathAge_ < 7) {
      collisionPause_ = 7 - pathAge_;
      phase_ = Phase::CollisionPause;
    } else
      phase_ = collisionResume_;
  }
  void terrainBlocked(Point previousRaw, int attemptedSpeed = -1) {
    raw_ = previousRaw;
    terrainRetryChanged_ = previousTravel_ != travelHeading_;
    collisionDebt_ = std::min(255, collisionDebt_ + 1);
    phase_ = Phase::TerrainFix;
    if (scriptDriven() && attemptedSpeed >= 0) {
      speed_ = attemptedSpeed;
      auto vector = directionVector(travelHeading_);
      velocity_ = {fixedProduct(speed_, vector.x),
                   fixedProduct(speed_, vector.y)};
    }
    if (collisionDebt_ > 2)
      moving_ = false;
    terrainCollisionSpeed_ = nextSpeed_ = speed_;
    if (!scriptDriven()) {
      // Ordinary physics previews acceleration; script motion retains the
      // animation speed accepted by the rejected attempt.
      auto goal = waypoint();
      int error = std::abs(signedHeading(desired_ - travelHeading_));
      int pixels = distance({goal.x - position().x, goal.y - position().y});
      int turns = (2 * error + physics_.turnRate - 1) / physics_.turnRate;
      bool accelerate = error == 0 || pixels >= 32 ||
                        (turns * speed_ / 256) * 3 / 2 <= pixels;
      auto stopping =
          speed_ == int(physics_.speed)
              ? std::int64_t(physics_.haltDistance)
              : std::int64_t(speed_) * speed_ / (2 * physics_.acceleration);
      bool terminal =
          brakesAtWaypoint() &&
          (cursor_ + 1 == route_.size() ||
           distance({route_.back().x - goal.x, route_.back().y - goal.y}) < 16);
      if (terminal &&
          distance({goal.x * 256 - raw_.x, goal.y * 256 - raw_.y}) <= stopping)
        accelerate = false;
      speed_ = std::clamp(speed_ + (accelerate ? physics_.acceleration
                                               : -physics_.acceleration),
                          0, int(physics_.speed));
      auto vector = directionVector(travelHeading_);
      velocity_ = {fixedProduct(speed_, vector.x),
                   fixedProduct(speed_, vector.y)};
    }
  }
  bool terrainFixing() const { return phase_ == Phase::TerrainFix; }
  bool terrainSlidingStep() const { return terrainStep_; }
  bool advancesTerrainCorner() const {
    if (scriptDriven() || cursor_ + 1 >= route_.size())
      return false;
    auto goal = waypoint();
    int remaining = distance({goal.x - position().x, goal.y - position().y});
    int accepted = restoreTerrainTravel_ ? terrainAccepted_ : travelHeading_;
    return remaining > 0 && remaining <= 16 &&
           std::abs(signedHeading(desired_ - accepted)) > remaining * 2;
  }
  bool terrainRetryStep() const { return terrainRetryStep_; }
  bool terrainRetryChanged() const { return terrainRetryChanged_; }
  void acceptTerrainRetry(Point acceptedRaw) {
    raw_ = acceptedRaw;
    nextSpeed_ = speed_;
    phase_ = Phase::Travel;
    terrainRetryStep_ = true;
  }
  void holdTerrainRetry(Point previousRaw) {
    raw_ = previousRaw;
    phase_ = Phase::TerrainFix;
    nextSpeed_ = scriptDriven() ? speed_ : terrainCollisionSpeed_;
    if (collisionDebt_ > 2)
      moving_ = false;
  }
  void rejectTerrainRetry(Point previousRaw, int direction,
                          bool escape = false) {
    raw_ = previousRaw;
    terrainHeading_ = signedHeading(direction);
    phase_ = Phase::TerrainSlide;
    terrainEscape_ = escape;
    nextSpeed_ = scriptDriven() ? speed_ : terrainCollisionSpeed_;
    if (collisionDebt_ > 2)
      moving_ = false;
    updatesHeading_ = false;
  }
  int terrainProbeSpeed() const {
    return scriptDriven() ? terrainCollisionSpeed_ : speed_;
  }
  Point terrainProbeVelocity() const {
    if (!scriptDriven())
      return velocity_;
    const auto vector = directionVector(travelHeading_);
    return {fixedProduct(terrainProbeSpeed(), vector.x),
            fixedProduct(terrainProbeSpeed(), vector.y)};
  }
  void continueTerrainSlide() { phase_ = Phase::TerrainSlide; }
  bool terrainEscapeStep() const { return terrainStep_ && terrainEscape_; }
  void blockedTerrainEscape(Point previousRaw) {
    raw_ = previousRaw;
    travelHeading_ = scriptDriven() ? terrainHeading_ : terrainAccepted_;
    collisionTravel_ = previousTravel_;
    auto vector = directionVector(travelHeading_);
    velocity_ = {fixedProduct(speed_, vector.x),
                 fixedProduct(speed_, vector.y)};
    restoreTerrainTravel_ = false;
    terrainStep_ = false;
    phase_ = Phase::TerrainReplan;
  }
  void slideBlocked(Point previousRaw, int previousFacing, int direction,
                    int previousNextSpeed = 0) {
    int attemptedSpeed = speed_;
    auto attemptedVelocity = velocity_;
    if (scriptDriven()) {
      collisionDebt_ = std::min(255, collisionDebt_ + 1);
      raw_ = previousRaw;
      facing_ = previousFacing;
      updatesHeading_ = false;
      attemptedSpeed = previousNextSpeed;
      auto vector = directionVector(travelHeading_);
      attemptedVelocity = {fixedProduct(attemptedSpeed, vector.x),
                           fixedProduct(attemptedSpeed, vector.y)};
    } else
      blocked(previousRaw, previousFacing, previousNextSpeed);
    speed_ = attemptedSpeed;
    velocity_ = attemptedVelocity;
    nextSpeed_ = previousNextSpeed;
    moving_ = previousNextSpeed > 0;
    slideHeading_ = signedHeading(direction);
    slideTravel_ = previousTravel_;
    phase_ = Phase::Slide;
  }
  void scriptSpeed(int speed, bool writePending = false) {
    if (!scriptDriven() || speed < 0 || speed > 65536)
      throw std::invalid_argument("Invalid animation motion speed");
    speed_ = speed;
    if (writePending) {
      nextSpeed_ = speed;
    }
    auto vector = directionVector(travelHeading_);
    velocity_ = {fixedProduct(speed_, vector.x),
                 fixedProduct(speed_, vector.y)};
  }
  bool stopped() const { return phase_ == Phase::Stopped; }
  bool planning() const { return planning_ > 0; }
  bool replanning() const {
    return phase_ == Phase::CollisionTurnSetup ||
           phase_ == Phase::SlideBlocked || phase_ == Phase::Reroute ||
           phase_ == Phase::RerouteMoving || phase_ == Phase::TerrainReplan;
  }
  bool reroutesMovingObstacles() const {
    return phase_ == Phase::RerouteMoving;
  }
  bool reroutesRecordedObstacle() const {
    return !expiredBodyWait_ &&
           (phase_ == Phase::Reroute || phase_ == Phase::RerouteMoving ||
            phase_ == Phase::Slide || phase_ == Phase::SlideFree ||
            (scriptDriven() && phase_ == Phase::CollisionTurnSetup));
  }
  void replan(std::vector<Point> points, bool stopAtEnd = true,
              std::optional<Point> failedMovingGoal = {}) {
    if (!replanning() || points.empty())
      throw std::runtime_error("Invalid collision route replacement");
    retainSetupTravel_ = scriptDriven() && phase_ == Phase::TerrainReplan;
    collisionReplanTurn_ = phase_ == Phase::Reroute ||
                           phase_ == Phase::RerouteMoving ||
                           (scriptDriven() && phase_ == Phase::TerrainReplan);
    const bool expiredMovingWait =
        expiredBodyWait_ && phase_ == Phase::RerouteMoving;
    const bool failedMoving = failedMovingGoal && phase_ == Phase::RerouteMoving;
    const int retainedTravel = travelHeading_;
    if (failedMovingGoal)
      validate(*failedMovingGoal);
    for (auto point : points)
      validate(point);
    auto next = heading(
        {points.front().x - position().x, points.front().y - position().y});
    turning_ = turning_ || next != desired_ ||
               (scriptDriven() && facing_ != next);
    preserveWaitingSpeed_ = false;
    route_ = std::move(points);
    cursor_ = 0;
    stopAtEnd_ = stopAtEnd;
    brakeArrival_ = brakeTravel_ = false;
    arrivalTarget_.reset();
    arrivalDirection_.reset();
    arrivalTargetLocked_ = false;
    failedMovingGoal_.reset();
    failedMovingDeparture_ = failedMoving;
    if (failedMoving) {
      failedMovingGoal_ = failedMovingGoal;
      travelHeading_ = retainedTravel;
      planning_ = 2;
      phase_ = Phase::CollisionTurn;
      firstCollisionTurn_ = true;
      expiredBodyWait_ = false;
      return;
    }
    if (phase_ == Phase::TerrainReplan) {
      collisionDebt_ = std::min(255, collisionDebt_ + 1);
      speed_ = nextSpeed_ = 0;
      velocity_ = {};
      turning_ = true;
      phase_ = Phase::CollisionTurnSetup;
    }
    if (phase_ == Phase::Reroute || phase_ == Phase::RerouteMoving) {
      // Even a one-step heading change needs the stationary facing update.
      // An exactly aligned route can enter travel within that same update.
      bool alignFirst = phase_ == Phase::RerouteMoving;
      travelHeading_ = collisionTravel_;
      pathAge_ = 0;
      planning_ = 0;
      phase_ = alignFirst ? Phase::CollisionTurn : Phase::Travel;
      firstCollisionTurn_ = alignFirst;
    }
    if (expiredBodyWait_) {
      expiredBodyWait_ = false;
      if (!expiredMovingWait)
        collisionDebt_ = std::min(255, collisionDebt_ + 1);
      planning_ = expiredMovingWait ? 0 : 1;
      phase_ = Phase::CollisionTurn;
      firstCollisionTurn_ = true;
    }
  }
  bool arrived() const {
    return phase_ == Phase::Arrived || (phase_ == Phase::Travel && !planning_ &&
                                        cursor_ + 1 == route_.size() &&
                                        samePoint(position(), route_.back()));
  }
  bool needsArrivalUpdate() const { return phase_ == Phase::Arrived && moving_; }
  bool hasRoute() const { return !route_.empty(); }
  bool needsWaypointContinuation() const {
    if (scriptDriven() || planning_ || pathAge_ < 0 || route_.empty() ||
        cursor_ + 1 != route_.size() ||
        (phase_ != Phase::Travel && phase_ != Phase::Arrived))
      return false;
    if (arrived())
      return true;
    auto goal = waypoint();
    int remaining = distance({goal.x - position().x, goal.y - position().y});
    return remaining <= 16 &&
           std::abs(signedHeading(desired_ - travelHeading_)) > remaining * 2;
  }
  // An escape endpoint can require braking before a separate logical target.
  void setMovementTarget(std::optional<Point> target) {
    movementTarget_ = target;
  }
  bool brakesAtRouteEnd() const { return stopAtEnd_; }
  bool brakesAtWaypoint() const {
    if (queuedMovementGoal_ && samePoint(waypoint(), *queuedMovementGoal_))
      return false;
    return stopAtEnd_ || (movementTarget_ && !route_.empty() &&
                          !samePoint(*movementTarget_, route_.back()));
  }
  void keepMomentumForQueuedMove(std::optional<Point> target) {
    if (target)
      validate(*target);
    queuedMovementGoal_ = target;
  }
  bool braking() const { return brakeArrival_; }
  // Stationary stop inertia can persist without a projected braking trip.
  bool brakingTravel() const { return brakeTravel_; }
  // Follow keeps a logical destination ahead of the bound target while its
  // route ends at contact. The lead depends on the follower's stopping travel.
  Point projectFollowTarget(Point target, int targetHeading) const {
    if (scriptDriven())
      return target;
    auto lead =
        3 * (std::int64_t(speed_) * speed_ / (2 * physics_.acceleration));
    auto direction = directionVector(targetHeading);
    auto offset = [&](int axis) {
      auto product = lead * axis;
      return int(product >= 0 ? product / 65536
                              : -((-product + 65535) / 65536));
    };
    return {target.x + offset(direction.x), target.y + offset(direction.y)};
  }
  bool settling() const {
    return phase_ == Phase::StopNext || phase_ == Phase::Holding;
  }
  bool waitingToStop() const { return phase_ == Phase::StopNext; }
  // Hold position while braking and turning toward an interaction target.
  void hold(Point target) {
    failedMovingGoal_.reset();
    failedMovingDeparture_ = false;
    recoveryHoldFrozen_ = false;
    validate(target);
    holdTarget_ = target;
    preserveWaitingSpeed_ = false;
    collisionHistory_ = false;
    collisionDebt_ = 0;
    arrivalStepPending_ =
        moving_ && !route_.empty() && samePoint(position(), waypoint());
    phase_ = Phase::Holding;
    moving_ = false;
  }
  // Collision recovery can remain parked with residual velocity state. An
  // aligned wait preserves it for the eventual retry without moving pixels.
  void waitForRecovery(Point target) {
    bool repeated = phase_ == Phase::Holding && preserveWaitingSpeed_ &&
                    samePoint(target, holdTarget_);
    hold(target);
    preserveWaitingSpeed_ = true;
    recoveryHoldFrozen_ = repeated && samePoint(target, position());
  }
  // An obstruction leaving a completed goal ends the parked recovery state.
  // Keep the admitted pose; completing a wait is not a new steering request.
  void finishRecoveryWait() {
    if (phase_ != Phase::Holding || !preserveWaitingSpeed_ ||
        !samePoint(holdTarget_, position()))
      return;
    preserveWaitingSpeed_ = recoveryHoldFrozen_ = arrivalStepPending_ = false;
    collisionHistory_ = false;
    collisionDebt_ = 0;
    speed_ = nextSpeed_ = 0;
    velocity_ = {};
    moving_ = updatesHeading_ = false;
    phase_ = Phase::Stopped;
  }
  void settle() {
    collisionHistory_ = false;
    collisionDebt_ = 0;
    planning_ = 0;
    if (phase_ != Phase::Holding)
      phase_ = Phase::StopNext;
    moving_ = false;
  }
  Point waypoint() const {
    return route_.empty() ? position() : route_[cursor_];
  }
  void route(std::vector<Point> points, int planningFrames = 2,
             bool restoreAcceptedTravel = false, bool stopAtEnd = true,
             bool retainAirRestAnchor = false) {
    if (points.empty() || planningFrames < 0)
      throw std::runtime_error("Invalid movement route");
    collisionReplanTurn_ = retainSetupTravel_ = false;
    failedMovingGoal_.reset();
    failedMovingDeparture_ = false;
    stoppedBrake_ = false;
    for (auto point : points)
      validate(point);
    bodyRestartWalk_ = phase_ == Phase::WaitingBody;
    bool collisionRestart = phase_ == Phase::CollisionTurn;
    bool contactRestart = phase_ == Phase::Contact;
    if (phase_ == Phase::SlideFree || (scriptDriven() && phase_ == Phase::Slide))
      collisionTravel_ = travelHeading_;
    bool interruptedContact =
        phase_ == Phase::CollisionPause || phase_ == Phase::Slide ||
        phase_ == Phase::SlideFree || phase_ == Phase::WaitingBody;
    int retainedSpeed = nextSpeed();
    if (scriptDriven() && bodyRestartWalk_ && retainedSpeed > 0)
      moving_ = true;
    bool atRouteGoal =
        points.size() == 1 && samePoint(position(), points.front());
    bool stationaryRestart = restoreAcceptedTravel &&
                             phase_ == Phase::Stopped && !moving_ &&
                             !atRouteGoal;
    if (collisionRestart && firstCollisionTurn_ && !atRouteGoal)
      travelHeading_ = collisionTravel_;
    bool finishingAtGoal = phase_ == Phase::StopNext && atRouteGoal;
    route_ = std::move(points);
    // Autonomous air travel can change its destination independently of
    // the resting-facing anchor; commanded travel establishes both.
    if (flying_ && !retainAirRestAnchor)
      anchor_ = route_.back();
    cursor_ = 0;
    stopAtEnd_ = stopAtEnd;
    brakeArrival_ = brakeTravel_ = false;
    arrivalTarget_.reset();
    arrivalDirection_.reset();
    arrivalTargetLocked_ = false;
    bool restart = interruptedContact && !atRouteGoal;
    // Contact keeps its attempted heading for the preparation frame, then
    // resumes the direction accepted before that blocked step.
    planning_ = restart ? 0
                        : std::max(planningFrames,
                                   int((contactRestart && !atRouteGoal) ||
                                       (interruptedContact && atRouteGoal)));
    pathAge_ = planningFrames > 0 ? -planningFrames : -1;
    // A trial acceleration can remain pending even after the turn phase ends.
    nextSpeed_ = restart || nextSpeed_ >= 0 ? retainedSpeed : -1;
    arrivalStepPending_ = false;
    restoreContactTravel_ = contactRestart || stationaryRestart ||
                            (atRouteGoal && collisionRestart);
    if (stationaryRestart)
      contactTravel_ = collisionHistory_ ? collisionTravel_ : facing_;
    if (atRouteGoal && collisionRestart)
      contactTravel_ = collisionTravel_;
    turning_ = true;
    phase_ = restart                  ? Phase::CollisionTurnSetup
             : finishingAtGoal        ? Phase::StopNext
             : atRouteGoal && !speed_ ? Phase::Stopped
                                      : Phase::Travel;
    if (phase_ == Phase::Stopped && moving_) {
      moving_ = false;
      velocity_ = {};
      travelHeading_ = desired_ = facing_;
      collisionHistory_ = false;
      restoreContactTravel_ = false;
    }
  }
  // A new order can request a facing update while collision recovery keeps
  // its current waypoint and accepted velocity.
  void refreshRouteHeading() { turning_ = true; }
  void resumeAfterLegalityCheck() {
    if (phase_ != Phase::CollisionTurnSetup)
      return;
    // A clear-space restart admits its path before the first travel trial.
    // Preserve the visible pose until that trial consumes the pending turn.
    phase_ = Phase::Travel;
    planning_ = 0;
    contactTravel_ = collisionTravel_;
    restoreContactTravel_ = true;
  }
  // A periodic stop preserves ongoing steering. A fresh order can also start
  // a turn toward its projected braking endpoint.
  void brakeTo(Point endpoint, bool faceTarget = false) {
    if (brakeTravel_ && samePoint(endpoint, waypoint()) &&
        !samePoint(position(), endpoint))
      return;
    queuedMovementGoal_.reset();
    bool wasTurning = turning_;
    const bool pendingScriptStop =
        scriptDriven() && moving_ && samePoint(endpoint, position());
    const int pendingDirection = desired_;
    const int pendingTravel = travelHeading_;
    const bool recovery =
        !scriptDriven() && (collisionPaused() || waitingForBody() ||
                            phase_ == Phase::Slide || phase_ == Phase::SlideFree);
    const bool stopped = !moving_;
    const int departure = waitingForBody() ? previousTravel_
                          : collisionPaused() ? collisionTravel_
                                              : travelHeading_;
    route({endpoint}, 0);
    if (pendingScriptStop) {
      // Animation speed and walking state are independent. A stop admitted
      // during a turn still owes its stationary arrival update.
      phase_ = Phase::Arrived;
      moving_ = true;
      desired_ = pendingDirection;
      travelHeading_ = pendingTravel;
    }
    if (recovery) {
      planning_ = 0;
      phase_ = Phase::Travel;
      restoreContactTravel_ = true;
      contactTravel_ = departure;
    }
    stoppedBrake_ = stopped;
    turning_ = faceTarget || wasTurning;
    brakeArrival_ = true;
    brakeTravel_ = !samePoint(endpoint, position());
  }
  void brakeBlocked(Point previousRaw) {
    queuedMovementGoal_.reset();
    if (!braking() || scriptDriven())
      throw std::logic_error("Invalid obstructed braking step");
    raw_ = previousRaw;
    route({position()}, 0);
    brakeArrival_ = true;
    brakeTravel_ = true;
    speed_ = nextSpeed_ = 0;
    velocity_ = {};
    // Keep this frame's walk state. The following update accepts the stop.
    moving_ = true;
  }
  void finishBlockedPoint(Point previousRaw, int previousFacing) {
    arrivalTarget_ = waypoint();
    arrivalDirection_.reset();
    raw_ = previousRaw;
    facing_ = previousFacing;
    nextSpeed_ = scriptDriven() ? -1 : 0;
    moving_ = previousMoving_;
    updatesHeading_ = false;
    collisionHistory_ = true;
    collisionDebt_ = std::min(255, collisionDebt_ + 1);
    failedMovingGoal_.reset();
    failedMovingDeparture_ = false;
    phase_ = Phase::Arrived;
  }
  void advance(bool terrainRetry = false) {
    stepWaypointChanged_ = false;
    const bool initializingInertia = std::exchange(initialInertiaFrame_, false);
    if (phase_ != Phase::CollisionTurnSetup && phase_ != Phase::CollisionTurn)
      bodyRestartWalk_ = false;
    if (restoreTerrainTravel_) {
      travelHeading_ = terrainAccepted_;
      restoreTerrainTravel_ = false;
    }
    terrainStep_ = terrainRetryStep_ = false;
    previousMoving_ = moving_;
    if (phase_ != Phase::Slide && phase_ != Phase::SlideFree)
      previousTravel_ = travelHeading_;
    slidingStep_ = false;
    shortenedStep_ = false;
    updatesHeading_ = true;
    if (phase_ == Phase::TerrainFix) {
      collisionDebt_ = std::min(255, collisionDebt_ + 1);
      auto priorTravel = travelHeading_;
      bool retryTurning = turning_;
      phase_ = Phase::Travel;
      nextSpeed_ = terrainCollisionSpeed_;
      advance(true); // Retry this waypoint before considering corner skips.
      // A correction can align its travel while retaining the pending facing
      // update for the first ordinary movement frame.
      turning_ |= retryTurning;
      terrainRetryChanged_ = priorTravel != travelHeading_;
      terrainRetryStep_ = true;
      return;
    }
    if (phase_ == Phase::TerrainSlide) {
      collisionDebt_ = std::min(255, collisionDebt_ + 1);
      if (!scriptDriven())
        speed_ = terrainCollisionSpeed_;
      nextSpeed_ = -1;
      terrainStep_ = true;
      auto goal = waypoint();
      auto tangent = directionVector(terrainHeading_);
      desired_ = terrainHeading_;
      auto angularError = std::abs(signedHeading(desired_ - travelHeading_));
      terrainAccepted_ = turnToward(
          travelHeading_, desired_,
          scriptDriven() ? physics_.turnRate : std::max(1, physics_.turnRate / 2));
      auto stopping =
          speed_ == int(physics_.speed)
              ? std::int64_t(physics_.haltDistance)
              : std::int64_t(speed_) * speed_ / (2 * physics_.acceleration);
      auto pixelDistance =
          distance({goal.x - position().x, goal.y - position().y});
      int turnSteps =
          (2 * angularError + physics_.turnRate - 1) / physics_.turnRate;
      int turnTravel = turnSteps * speed_ / 256;
      bool speedUp = angularError == 0 || pixelDistance >= 32 ||
                     turnTravel * 3 / 2 <= pixelDistance;
      bool terminal =
          brakesAtWaypoint() &&
          (cursor_ + 1 == route_.size() ||
           distance({route_.back().x - goal.x, route_.back().y - goal.y}) < 16);
      if (terminal &&
          distance({goal.x * 256 - raw_.x, goal.y * 256 - raw_.y}) <= stopping)
        speedUp = false;
      if (!scriptDriven()) {
        speed_ = std::clamp(
            speed_ + (speedUp ? physics_.acceleration : -physics_.acceleration),
            0, int(physics_.speed));
        terrainCollisionSpeed_ = speed_;
      }
      const Point corner{goal.x * 256, goal.y * 256};
      if (scriptDriven() && cursor_ + 1 < route_.size() &&
          distance({corner.x - raw_.x, corner.y - raw_.y}) <= speed_) {
        // Admit the remaining corner segment before consuming another leg.
        // Body facing stays at its pre-slide pose until the next update.
        raw_ = corner;
        travelHeading_ = terrainAccepted_ = terrainHeading_;
      } else {
        raw_.x += fixedProduct(speed_, tangent.x);
        raw_.y += fixedProduct(speed_, tangent.y);
        // Away from a corner, preview travel from the tangent endpoint.
        travelHeading_ = heading({goal.x - position().x, goal.y - position().y});
      }
      auto vector = directionVector(travelHeading_);
      velocity_ = {fixedProduct(speed_, vector.x),
                   fixedProduct(speed_, vector.y)};
      restoreTerrainTravel_ = true;
      turning_ = true;
      moving_ = true;
      phase_ = Phase::Travel;
      updatesHeading_ = false;
      return;
    }
    if (phase_ == Phase::Stopped) {
      if (!initializingInertia && nextSpeed_ >= 0) {
        speed_ = nextSpeed_;
        nextSpeed_ = -1;
        auto vector = directionVector(travelHeading_);
        velocity_ = {fixedProduct(speed_, vector.x),
                     fixedProduct(speed_, vector.y)};
      }
      if (flying_ && route_.empty())
        desired_ = facing_;
      updatesHeading_ = alignArrivalHeading();
      return;
    }
    ++pathAge_;
    if (phase_ == Phase::BodyRetry) {
      auto goal = waypoint();
      desired_ = heading({goal.x - position().x, goal.y - position().y});
      if (turning_)
        facing_ = turnToward(facing_, desired_, physics_.turnRate);
      speed_ = nextSpeed_ = bodyWaitSpeed_;
      auto vector = directionVector(travelHeading_);
      velocity_ = {fixedProduct(speed_, vector.x),
                   fixedProduct(speed_, vector.y)};
      raw_.x += bodyWaitStep_.x;
      raw_.y += bodyWaitStep_.y;
      if (facing_ == desired_ && travelHeading_ == desired_)
        turning_ = false;
      moving_ = true;
      phase_ = Phase::Travel;
      return;
    }
    if (phase_ == Phase::RouteTurn) {
      auto goal = waypoint();
      desired_ = heading({goal.x - position().x, goal.y - position().y});
      if (samePoint(goal, position()) || travelHeading_ == desired_)
        phase_ = Phase::Travel;
      else {
        facing_ = turnToward(facing_, desired_, physics_.turnRate);
        travelHeading_ = turnToward(travelHeading_, desired_,
                                    std::max(1, physics_.turnRate / 2));
        auto vector = directionVector(travelHeading_);
        velocity_ = {fixedProduct(speed_, vector.x),
                     fixedProduct(speed_, vector.y)};
        turning_ = facing_ != desired_ || travelHeading_ != desired_;
        moving_ = true;
        return;
      }
    }
    if (phase_ == Phase::Slide || phase_ == Phase::SlideFree) {
      collisionDebt_ = std::min(255, collisionDebt_ + 1);
      slidingStep_ = true;
      auto goal = waypoint();
      desired_ = heading({goal.x - position().x, goal.y - position().y});
      facing_ = turnToward(facing_, slideHeading_, physics_.turnRate);
      int entrySpeed =
          phase_ == Phase::Slide
              ? (speed_ >= 512 ? speed_ / 2 : std::min(speed_, 256))
              : speed_;
      travelHeading_ = turnToward(
          phase_ == Phase::Slide ? slideTravel_ : travelHeading_, slideHeading_,
          std::max(1, physics_.turnRate / (scriptDriven() ? 1 : 2)));
      auto stopping =
          std::int64_t(entrySpeed) * entrySpeed / (2 * physics_.acceleration);
      auto remaining = distance({goal.x * 256 - raw_.x, goal.y * 256 - raw_.y});
      bool terminal =
          cursor_ + 1 == route_.size() ||
          distance({route_.back().x - goal.x, route_.back().y - goal.y}) < 16;
      int angularError =
          std::abs(signedHeading(slideHeading_ - travelHeading_));
      int turnSteps =
          (2 * angularError + physics_.turnRate - 1) / physics_.turnRate;
      int turnTravel = turnSteps * entrySpeed / 256;
      int pixelDistance =
          distance({goal.x - position().x, goal.y - position().y});
      bool speedUp = angularError == 0 || pixelDistance >= 32 ||
                     turnTravel * 3 / 2 <= pixelDistance;
      if (terminal && remaining <= stopping)
        speedUp = false;
      int acceleration =
          speedUp ? physics_.acceleration : -physics_.acceleration;
      speed_ = scriptDriven() ? int(physics_.speed)
                              : std::clamp(entrySpeed + acceleration, 0,
                                           int(physics_.speed));
      nextSpeed_ = -1;
      auto vector = directionVector(travelHeading_);
      velocity_ = {fixedProduct(speed_, vector.x),
                   fixedProduct(speed_, vector.y)};
      int step = std::min(speed_, 256);
      shortenedStep_ = remaining < step;
      raw_ = slideStep(raw_, step);
      moving_ = true;
      phase_ = Phase::Travel;
      return;
    }
    if (phase_ == Phase::WaitingBody) {
      collisionDebt_ = std::min(255, collisionDebt_ + 1);
      speed_ = nextSpeed_ = 0;
      velocity_ = {};
      moving_ = false;
      updatesHeading_ = false;
      return;
    }
    if (phase_ == Phase::CollisionPause) {
      movingPausePrepared_ = true;
      updatesHeading_ = false;
      collisionDebt_ = std::min(255, collisionDebt_ + 1);
      if (--collisionPause_ == 0) {
        if (collisionResume_ == Phase::CollisionTurnSetup) {
          speed_ = nextSpeed_ = 0;
          velocity_ = {};
        }
        phase_ = collisionResume_;
      } else if (collisionDebt_ > 2)
        moving_ = false;
      return;
    }
    if (phase_ == Phase::SlideBlocked) {
      collisionReplanTurn_ = false;
      collisionDebt_ = std::min(255, collisionDebt_ + 1);
      speed_ = nextSpeed_ = 0;
      velocity_ = {};
      collisionTravel_ = travelHeading_;
      updatesHeading_ = false;
      pathAge_ = 0;
      phase_ = Phase::CollisionTurn;
      firstCollisionTurn_ = true;
      return;
    }
    if (phase_ == Phase::CollisionTurnSetup) {
      collisionReplanTurn_ = retainSetupTravel_;
      retainSetupTravel_ = false;
      nextSpeed_ = nextSpeed();
      updatesHeading_ = false;
      pathAge_ = 0;
      phase_ = Phase::CollisionTurn;
      firstCollisionTurn_ = true;
      return;
    }
    if (phase_ == Phase::CollisionTurn) {
      if (planning_) {
        --planning_;
        if (!planning_ && failedMovingGoal_) {
          route_ = {*failedMovingGoal_};
          cursor_ = 0;
          stopAtEnd_ = true;
        }
        updatesHeading_ = false;
        return;
      }
      auto goal = waypoint();
      if (samePoint(goal, position())) {
        phase_ = Phase::StopNext;
        moving_ = false;
        desired_ = facing_;
        updatesHeading_ = false;
        return;
      }
      desired_ = heading({goal.x - position().x, goal.y - position().y});
      if (firstCollisionTurn_ &&
          (scriptDriven() ? facing_ : collisionTravel_) == desired_) {
        phase_ = Phase::Travel;
        // The aligned travel update belongs to this same frame.
        --pathAge_;
        advance();
        return;
      }
      if (turning_)
        facing_ = turnToward(facing_, desired_, physics_.turnRate);
      travelHeading_ = scriptDriven() && !collisionReplanTurn_
                           ? facing_
                           : turnToward(
                                 firstCollisionTurn_ ? collisionTravel_ : travelHeading_,
                                 desired_, scriptDriven() ? physics_.turnRate
                                                        : std::max(1, physics_.turnRate / 2));
      firstCollisionTurn_ = false;
      const int angularError =
          std::abs(signedHeading(desired_ - travelHeading_));
      const int turnSteps =
          (2 * angularError + physics_.turnRate - 1) / physics_.turnRate;
      const int turnTravel = turnSteps * nextSpeed_ / 256;
      const int remainingPixels =
          distance({goal.x - position().x, goal.y - position().y});
      bool speedUp = angularError == 0 || remainingPixels >= 32 ||
                           turnTravel * 3 / 2 <= remainingPixels;
      if (!scriptDriven() && brakesAtWaypoint() && cursor_ + 1 == route_.size()) {
        auto stopping = nextSpeed_ == int(physics_.speed)
                            ? std::int64_t(physics_.haltDistance)
                            : std::int64_t(nextSpeed_) * nextSpeed_ /
                                  (2 * physics_.acceleration);
        if (distance({goal.x * 256 - raw_.x, goal.y * 256 - raw_.y}) <= stopping)
          speedUp = false;
      }
      speed_ = scriptDriven()
                   ? speed_
                   : std::clamp(nextSpeed_ + (speedUp ? physics_.acceleration
                                                    : -physics_.acceleration),
                                0, int(physics_.speed));
      auto vector = directionVector(travelHeading_);
      velocity_ = {fixedProduct(speed_, vector.x),
                   fixedProduct(speed_, vector.y)};
      if ((!turning_ || facing_ == desired_) && travelHeading_ == desired_) {
        phase_ = Phase::Travel;
        turning_ = false;
        failedMovingGoal_.reset();
      }
      return;
    }
    if (nextSpeed_ >= 0) {
      const bool inheritedDeparture =
          scriptDriven() && !previousMoving_ && turning_ && !route_.empty() &&
          !samePoint(position(), waypoint()) &&
          std::abs(signedHeading(facing_ -
              heading({waypoint().x - position().x,
                       waypoint().y - position().y}))) >= 32;
      if (!inheritedDeparture)
        speed_ = nextSpeed_;
      nextSpeed_ = -1;
    }
    if (planning_) {
      updatesHeading_ = false;
      --planning_;
      return;
    }
    if (phase_ == Phase::Travel && restoreContactTravel_) {
      travelHeading_ = contactTravel_;
      previousTravel_ = travelHeading_;
      restoreContactTravel_ = false;
    }
    if (phase_ == Phase::Contact) {
      phase_ = Phase::Holding;
      moving_ = false;
    }
    if (phase_ == Phase::Holding) {
      if (recoveryHoldFrozen_) {
        updatesHeading_ = false;
        return;
      }
      if (restoreContactTravel_) {
        travelHeading_ = contactTravel_;
        restoreContactTravel_ = false;
      }
      int interactionHeading = samePoint(position(), holdTarget_)
                                   ? facing_
                                   : heading({holdTarget_.x - position().x,
                                              holdTarget_.y - position().y});
      bool aligned =
          facing_ == interactionHeading && travelHeading_ == interactionHeading;
      // An already aligned interaction does not request a steering update.
      // Keep the last desired direction even when it differs from the pose.
      if (!aligned) {
        desired_ = interactionHeading;
        facing_ = turnToward(facing_, desired_, physics_.turnRate);
        travelHeading_ = turnToward(travelHeading_, desired_,
                                    std::max(1, physics_.turnRate /
                                                    (scriptDriven() ? 1 : 2)));
      }
      bool updateSpeed =
          (!preserveWaitingSpeed_ && arrivalStepPending_) || !aligned;
      arrivalStepPending_ = false;
      if (!updateSpeed) {
        if (!preserveWaitingSpeed_)
          speed_ = 0;
      } else if (scriptDriven() || speed_ < 192)
        speed_ = 0;
      else {
        int error =
            std::abs(signedHeading(interactionHeading - travelHeading_));
        bool turnAllowsAcceleration =
            error == 0 ||
            std::int64_t(2 * error + physics_.turnRate - 1) * speed_ <
                std::int64_t(physics_.turnRate) * 256;
        bool accelerate =
            !route_.empty() && samePoint(position(), waypoint()) &&
            turnAllowsAcceleration && fractionalArrivalAccelerates();
        speed_ = std::clamp(speed_ + (accelerate ? physics_.acceleration
                                                 : -physics_.acceleration),
                            0, int(physics_.speed));
      }
      auto vector = directionVector(travelHeading_);
      velocity_ = {fixedProduct(speed_, vector.x),
                   fixedProduct(speed_, vector.y)};
      if (!speed_ && facing_ == interactionHeading &&
          travelHeading_ == interactionHeading)
        phase_ = Phase::Stopped;
      return;
    }
    // Clearing logical movement does not discard residual turn inertia.
    // End that stationary transition once both directions have aligned.
    if (stoppedBrake_ &&
        (phase_ == Phase::Travel || phase_ == Phase::Arrived ||
         phase_ == Phase::StopNext) && samePoint(position(), waypoint())) {
      const int wanted = arrivalDirection_ ? *arrivalDirection_
                         : arrivalTarget_ && !samePoint(*arrivalTarget_, position())
                             ? heading({arrivalTarget_->x - position().x,
                                        arrivalTarget_->y - position().y})
                             : facing_;
      if (facing_ == wanted && travelHeading_ == wanted) {
        speed_ = nextSpeed_ = 0;
        velocity_ = {};
        phase_ = Phase::Stopped;
        moving_ = updatesHeading_ = stoppedBrake_ = false;
        arrivalTargetLocked_ = false;
        return;
      }
    }
    if (phase_ == Phase::StopNext) {
      const bool hasArrivalDirection = arrivalDirection_ ||
          (arrivalTarget_ && !samePoint(*arrivalTarget_, position()));
      const int wanted = arrivalDirection_ ? *arrivalDirection_
                         : hasArrivalDirection
                             ? heading({arrivalTarget_->x - position().x,
                                        arrivalTarget_->y - position().y})
                             : facing_;
      const bool finishingTurn = !scriptDriven() && hasArrivalDirection &&
                                 (facing_ != wanted || travelHeading_ != wanted);
      if ((brakeArrival_ || finishingTurn) && speed_ >= 192) {
        alignArrivalHeading();
        speed_ = std::clamp(speed_ + (fractionalArrivalAccelerates()
                                          ? physics_.acceleration
                                          : -physics_.acceleration),
                            0, int(physics_.speed));
        auto vector = directionVector(travelHeading_);
        velocity_ = {fixedProduct(speed_, vector.x),
                     fixedProduct(speed_, vector.y)};
        return;
      }
      speed_ = 0;
      velocity_ = {};
      phase_ = Phase::Stopped;
      moving_ = false;
      if (arrivalTarget_)
        updatesHeading_ = alignArrivalHeading();
      arrivalTargetLocked_ = false;
      return;
    }
    if (phase_ == Phase::Arrived) {
      collisionHistory_ = false;
      collisionDebt_ = 0;
      speed_ =
          fractionalArrivalAccelerates()
              ? std::min(int(physics_.speed), speed_ + physics_.acceleration)
          : scriptDriven() || speed_ < 192
              ? 0
              : std::max(0, speed_ - physics_.acceleration);
      desired_ =
          arrivalDirection_ ? *arrivalDirection_
          : arrivalTarget_ && !samePoint(*arrivalTarget_, position())
              ? heading({arrivalTarget_->x - position().x,
                         arrivalTarget_->y - position().y})
          : flying_ && !samePoint(anchor_, position())
              ? heading({anchor_.x - position().x, anchor_.y - position().y})
              : facing_;
      if (flying_ || arrivalTarget_)
        facing_ = turnToward(facing_, desired_, physics_.turnRate);
      travelHeading_ =
          turnToward(travelHeading_, desired_,
                     std::max(1, physics_.turnRate / (scriptDriven() ? 1 : 2)));
      auto vector = directionVector(travelHeading_);
      velocity_ = {fixedProduct(speed_, vector.x),
                   fixedProduct(speed_, vector.y)};
      phase_ = Phase::StopNext;
      moving_ = false;
      if (scriptDriven())
        arrivalTargetLocked_ = false;
      return;
    }
    auto final = route_.back();
    while (!terrainRetry && !scriptDriven() && cursor_ + 1 < route_.size()) {
      auto goal = route_[cursor_];
      int remaining = distance({goal.x - position().x, goal.y - position().y});
      if (!remaining || remaining > 16 ||
          std::abs(signedHeading(desired_ - travelHeading_)) <= remaining * 2)
        break;
      ++cursor_;
      stepWaypointChanged_ = true;
      turning_ = true;
    }
    while (samePoint(position(), route_[cursor_])) {
      if (cursor_ + 1 == route_.size()) {
        phase_ = Phase::Arrived;
        advance();
        return;
      }
      ++cursor_;
      stepWaypointChanged_ = true;
      turning_ = true;
    }
    const auto at = position(), goal = route_[cursor_];
    desired_ = heading({goal.x - at.x, goal.y - at.y});
    auto previousFacing = facing_;
    auto wasTurning = turning_;
    if (turning_)
      facing_ = turnToward(facing_, desired_, physics_.turnRate);
    travelHeading_ =
        turnToward(travelHeading_, desired_,
                   std::max(1, physics_.turnRate / (scriptDriven() ? 1 : 2)));
    if (travelHeading_ == desired_ && facing_ == desired_)
      turning_ = false;
    auto remaining = distance({goal.x * 256 - raw_.x, goal.y * 256 - raw_.y});
    moving_ = true;
    if (scriptDriven()) {
      int step =
          wasTurning && previousMoving_ &&
                  std::abs(signedHeading(previousFacing - desired_)) >= 32
              ? 0
              : speed_;
      auto vector = directionVector(travelHeading_);
      if (remaining <= step) {
        shortenedStep_ = remaining < step;
        raw_ = {goal.x * 256, goal.y * 256};
        if (cursor_ + 1 == route_.size())
          phase_ = Phase::Arrived;
      } else {
        raw_.x += fixedProduct(step, vector.x);
        raw_.y += fixedProduct(step, vector.y);
      }
      speed_ = 0;
      velocity_ = {};
      return;
    }
    auto stopping =
        speed_ == int(physics_.speed)
            ? std::int64_t(physics_.haltDistance)
            : std::int64_t(speed_) * speed_ / (2 * physics_.acceleration);
    bool terminal = brakesAtWaypoint() &&
                    (cursor_ + 1 == route_.size() ||
                     distance({final.x - goal.x, final.y - goal.y}) < 16);
    auto pixelDistance = distance({goal.x - at.x, goal.y - at.y});
    int angularError = std::abs(signedHeading(desired_ - travelHeading_));
    auto turnSteps =
        (2 * angularError + physics_.turnRate - 1) / physics_.turnRate;
    auto turnTravel = turnSteps * speed_ / 256;
    bool speedUp = angularError == 0 || pixelDistance >= 32 ||
                   turnTravel * 3 / 2 <= pixelDistance;
    if (terminal && remaining <= stopping)
      speedUp = false;
    speed_ = std::clamp(
        speed_ + (speedUp ? physics_.acceleration : -physics_.acceleration), 0,
        int(physics_.speed));
    auto vector = directionVector(travelHeading_);
    velocity_ = {fixedProduct(speed_, vector.x),
                 fixedProduct(speed_, vector.y)};
    if (remaining <= speed_) {
      shortenedStep_ = remaining < speed_;
      raw_ = {goal.x * 256, goal.y * 256};
      if (cursor_ + 1 == route_.size())
        phase_ = Phase::Arrived;
    } else {
      raw_.x += velocity_.x;
      raw_.y += velocity_.y;
    }
  }

private:
  bool alignArrivalHeading() {
    if (!arrivalDirection_ &&
        (!arrivalTarget_ || samePoint(*arrivalTarget_, position())))
      return false;
    desired_ = arrivalDirection_ ? *arrivalDirection_
               : heading({arrivalTarget_->x - position().x,
                          arrivalTarget_->y - position().y});
    auto previousFacing = facing_, previousTravel = travelHeading_;
    facing_ = turnToward(facing_, desired_, physics_.turnRate);
    travelHeading_ =
        turnToward(travelHeading_, desired_,
                   std::max(1, physics_.turnRate / (scriptDriven() ? 1 : 2)));
    return facing_ != previousFacing || travelHeading_ != previousTravel;
  }
  bool fractionalArrivalAccelerates() const {
    if (scriptDriven() || route_.empty())
      return false;
    if (queuedMovementGoal_ && samePoint(waypoint(), *queuedMovementGoal_))
      return true;
    if (speed_ < 192)
      return false;
    auto goal = waypoint();
    Point remaining{goal.x * 256 - raw_.x, goal.y * 256 - raw_.y};
    auto stopping =
        speed_ == int(physics_.speed)
            ? std::int64_t(physics_.haltDistance)
            : std::int64_t(speed_) * speed_ / (2 * physics_.acceleration);
    return distance(remaining) > stopping;
  }
  enum class Phase {
    Stopped,
    Travel,
    Arrived,
    StopNext,
    Holding,
    Contact,
    CollisionPause,
    CollisionTurnSetup,
    CollisionTurn,
    Slide,
    SlideFree,
    SlideBlocked,
    TerrainFix,
    TerrainSlide,
    TerrainReplan,
    Reroute,
    RerouteMoving,
    WaitingBody,
    RouteTurn,
    BodyRetry
  };
  Point raw_, velocity_{};
  Point holdTarget_;
  dat::Flingy physics_;
  int speed_ = 0, facing_, travelHeading_, desired_ = 0, planning_ = 0;
  int nextSpeed_ = -1, collisionPause_ = 0, collisionDebt_ = 0, pathAge_ = -1;
  int terrainCollisionSpeed_ = 0;
  bool firstCollisionTurn_ = false;
  int slideHeading_ = 0;
  int slideTravel_ = 0, previousTravel_ = 0, contactTravel_ = 0;
  bool restoreContactTravel_ = false;
  bool slidingStep_ = false;
  bool shortenedStep_ = false;
  bool updatesHeading_ = false, movingPausePrepared_ = false;
  bool bodyWaitClearance_ = false;
  std::vector<Point> route_;
  std::size_t cursor_ = 0;
  Phase phase_ = Phase::Stopped;
  bool terrainRetryChanged_ = false;
  bool terrainStep_ = false, terrainRetryStep_ = false,
       restoreTerrainTravel_ = false;
  int terrainHeading_ = 0, terrainAccepted_ = 0;
  bool terrainEscape_ = false;
  bool stopAtEnd_ = true;
  std::optional<Point> movementTarget_;
  std::optional<Point> queuedMovementGoal_;
  bool brakeArrival_ = false;
  bool brakeTravel_ = false;
  bool stoppedBrake_ = false;
  bool collisionReplanTurn_ = false;
  bool retainSetupTravel_ = false;
  bool arrivalTargetLocked_ = false;
  bool initialInertiaFrame_ = false;
  Phase collisionResume_ = Phase::CollisionTurnSetup;
  int bodyWaitSpeed_ = 0;
  Point bodyWaitStep_;
  bool bodyWaitKeepsStep_ = false;
  bool bodyWaitTurning_ = false;
  bool bodyWaitScriptTurn_ = false;
  bool bodyRecoveryDelay_ = false;
  bool bodyWaitSegmentTurn_ = false;
  bool stepWaypointChanged_ = false;
  bool turning_ = false;
  bool bodyRestartWalk_ = false;
  int collisionTravel_ = 0;
  bool collisionHistory_ = false;
  bool arrivalStepPending_ = false, preserveWaitingSpeed_ = false;
  bool recoveryHoldFrozen_ = false;
  bool expiredBodyWait_ = false;
  std::optional<Point> failedMovingGoal_;
  bool failedMovingDeparture_ = false;
  bool moving_ = false;
  bool previousMoving_ = false;
  Point anchor_;
  std::optional<Point> arrivalTarget_;
  std::optional<int> arrivalDirection_;
  bool flying_ = false;
  std::optional<Point> airPosition_;
  static void validate(Point p) {
    if (p.x < 0 || p.y < 0 || p.x > 8191 || p.y > 8191)
      throw std::out_of_range("Movement point outside supported map");
  }
};

} // namespace mapeditor::simulation
