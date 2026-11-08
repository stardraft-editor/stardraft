// SPDX-License-Identifier: MIT
#include <iostream>
#include <limits>
#include <mapeditor/simulation/motion.h>
#include <mapeditor/simulation/navigation.h>
#include <mapeditor/simulation/random.h>
using namespace mapeditor::simulation;
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught, "Invalid motion accepted");
}
int main() {
  try {
    Random random;
    check(random.state() == 42, "Default random seed");
    check(random.next() == 14544 && random.state() == 953210035U,
          "First random observation");
    check(random.next() == 24056 && random.state() == 3724055312U,
          "Second random observation");
    check(random.next() == 29925 && random.state() == 1961185873U,
          "Third random observation");
    auto copy = random;
    check(copy.next() == random.next(), "Random state clone");
    rejects([&] { random.between(1, 0); });
    Random scaled;
    check(scaled.scaled(0, 255) == 113 && scaled.state() == 953210035U,
          "Scaled random selection differs from modulo selection");
    rejects([&] { scaled.scaled(1, 0); });
    check(fixedProduct(67, -25) == -7 && fixedProduct(67, 227) == 59,
          "Signed fixed-point floor");
    dat::Flingy physics{0, 1280, 12227, 67, 40, 0};
    {
      Motion repeated({900, 900}, 0, physics);
      repeated.route({{900, 300}}, 0);
      for (int frame = 0; frame < 12; ++frame)
        repeated.advance();
      repeated.route({{1500, 900}}, 0);
      repeated.advance();
      repeated.brakeTo(repeated.stopTarget());
      repeated.advance();
      repeated.faceOnArrival(Point{950, 850});
      check(repeated.brakingTravel() &&
                !samePoint(repeated.position(), repeated.waypoint()),
            "The repeated-stop fixture retains an active braking trip");
      auto copy = repeated;
      repeated.brakeTo(repeated.stopTarget());
      for (int frame = 0; frame < 8; ++frame) {
        repeated.advance();
        copy.advance();
        check(samePoint(repeated.rawPosition(), copy.rawPosition()) &&
                  repeated.speed() == copy.speed() &&
                  repeated.facing() == copy.facing() &&
                  repeated.travelHeading() == copy.travelHeading(),
              "Repeating an active stop preserves its accepted trajectory");
      }
    }
    for (bool moving : {true, false}) {
      Motion stop({900, 900}, 64, physics);
      stop.route({{1800, 900}}, 0);
      for (int frame = 0; frame < 14; ++frame)
        stop.advance();
      auto point = stop.position();
      stop.route({{point.x + 5, point.y}}, 0);
      stop.setRouteAge(1);
      auto raw = stop.rawPosition();
      int facing = stop.facing(), reserved = stop.nextSpeed();
      stop.advance();
      stop.blockedWaypoint(raw, facing, reserved, true);
      if (!moving) {
        stop.advance();
        stop.advance();
      }
      stop.brakeTo(stop.stopTarget());
      check(stop.braking() && stop.brakingTravel() == moving,
            "A current-point stop keeps inertia without a braking trip");
      if (moving) {
        stop.advance();
        check(stop.speed() == 871 && !samePoint(stop.rawPosition(), raw),
              "A moving stop request replaces collision preparation with braking");
      } else {
        stop.faceOnArrival(Point{point.x + 15, point.y - 20});
        auto copy = stop;
        for (int expected : {871, 804, 0}) {
          stop.advance();
          check(stop.speed() == expected && samePoint(stop.rawPosition(), raw),
                "A logically stopped body turns with residual speed and clears "
                "it once aligned");
        }
        check(copy.speed() == 871 && copy.nextSpeed() == 938,
              "Stopping a copied body keeps independent pending inertia");
      }
    }

    {
      Motion slidingStop({800, 800}, 64, physics);
      slidingStop.route({{1800, 800}}, 0);
      for (int frame = 0; frame < 12; ++frame)
        slidingStop.advance();
      auto raw = slidingStop.rawPosition();
      int facing = slidingStop.facing(), reserved = slidingStop.nextSpeed();
      slidingStop.advance();
      slidingStop.slideBlocked(raw, facing, 64, reserved);
      slidingStop.advance();
      slidingStop.finishSlide(slidingStop.rawPosition(), false);
      raw = slidingStop.rawPosition();
      auto copy = slidingStop;
      check(slidingStop.speed() == 502, "The sliding stop fixture has inertia");
      slidingStop.brakeTo(slidingStop.stopTarget());
      slidingStop.advance();
      check(slidingStop.speed() == 435 &&
                !samePoint(slidingStop.rawPosition(), raw) &&
                copy.speed() == 502 && samePoint(copy.rawPosition(), raw),
            "A fresh stop cancels sliding preparation and brakes in motion "
            "without changing a copied body");
    }
    {
      Motion acceptedSlide({800, 800}, 64, physics);
      acceptedSlide.route({{1800, 800}}, 0);
      for (int step = 0; step < 20; ++step)
        acceptedSlide.advance();
      auto beforeSlide = acceptedSlide.rawPosition();
      auto beforeSlideSpeed = acceptedSlide.nextSpeed();
      acceptedSlide.advance();
      acceptedSlide.slideBlocked(beforeSlide, 64, 0, beforeSlideSpeed);
      acceptedSlide.advance();
      acceptedSlide.finishSlide(acceptedSlide.rawPosition(), false);
      auto acceptedDirection = acceptedSlide.travelHeading();
      check(acceptedDirection != 64,
            "The slide fixture has accepted a new travel direction");
      auto closeTurn = acceptedSlide;
      auto turnAnchor = acceptedSlide.rawPosition();
      auto at = acceptedSlide.position();
      acceptedSlide.route({{at.x, at.y - 400}}, 0);
      acceptedSlide.advance();
      acceptedSlide.advance();
      check(acceptedSlide.travelHeading() ==
                turnToward(acceptedDirection, 0, physics.turnRate / 2) &&
                samePoint(acceptedSlide.rawPosition(), turnAnchor),
            "A retargeted slide turns from its latest accepted travel");
      closeTurn.route({{at.x, at.y + 8}}, 0);
      closeTurn.advance();
      auto cornerBudget = closeTurn.nextSpeed();
      closeTurn.advance();
      check(closeTurn.speed() < cornerBudget &&
                closeTurn.nextSpeed() == cornerBudget &&
                samePoint(closeTurn.rawPosition(), turnAnchor),
            "A short recovery corner decelerates while retaining its next-frame budget");
    }
    Motion waitingFineTurn({500, 500}, 0, physics);
    waitingFineTurn.route({{501, 400}}, 0);
    auto fineTurnAnchor = waitingFineTurn.rawPosition();
    waitingFineTurn.advance();
    waitingFineTurn.waitForBody(fineTurnAnchor, 0, waitingFineTurn.nextSpeed());
    waitingFineTurn.advance();
    auto fineTurnCopy = waitingFineTurn;
    auto fineTurnTrial = waitingFineTurn;
    fineTurnTrial.resumeBodyWait();
    fineTurnTrial.advance();
    check(fineTurnTrial.travelHeading() == waitingFineTurn.travelHeading() &&
              waitingFineTurn.travelHeading() != waitingFineTurn.facing(),
          "A blocked retry can retain settled travel with an unfinished body turn");
    waitingFineTurn.acceptBlockedWaitTurn(fineTurnTrial);
    check(waitingFineTurn.facing() == waitingFineTurn.travelHeading() &&
              samePoint(waitingFineTurn.rawPosition(), fineTurnAnchor) &&
              waitingFineTurn.speed() == 0 && fineTurnCopy.facing() == 0,
          "Blocked steering accepts facing without travel or snapshot aliasing");
    {
      Motion alignedReplan({500, 500}, heading({1, -100}), physics);
      Point goal{501, 400};
      alignedReplan.route({goal}, 0);
      bool found = false;
      for (int frame = 0; frame < 80 && !alignedReplan.arrived(); ++frame) {
        auto previous = alignedReplan.rawPosition();
        int facing = alignedReplan.facing();
        int reserved = alignedReplan.nextSpeed();
        alignedReplan.advance();
        if (alignedReplan.facing() == alignedReplan.travelHeading() ||
            alignedReplan.arrived())
          continue;
        // Rasterized travel can adjust after the body turn has completed.
        // Replanning that same direction must preserve the admitted body pose.
        int attemptedTravel = alignedReplan.travelHeading();
        alignedReplan.blockedWaypoint(previous, facing, reserved, true);
        alignedReplan.replan({goal});
        auto copy = alignedReplan;
        alignedReplan.advance(); copy.advance();
        check(alignedReplan.facing() == facing &&
                  alignedReplan.travelHeading() == attemptedTravel &&
                  samePoint(alignedReplan.rawPosition(), previous) &&
                  copy.facing() == facing,
              "An unchanged moving-body replan retains its completed body turn");
        found = true;
        break;
      }
      check(found, "The rasterized route changes travel after body alignment");
    }
    {
      Motion completedBodyTurn({500, 500}, heading({1, -100}), physics);
      completedBodyTurn.route({{501, 400}}, 0);
      bool found = false;
      for (int frame = 0; frame < 80 && !completedBodyTurn.arrived(); ++frame) {
        auto raw = completedBodyTurn.rawPosition();
        int facing = completedBodyTurn.facing();
        int reserved = completedBodyTurn.nextSpeed();
        int priorTravel = completedBodyTurn.travelHeading();
        completedBodyTurn.advance();
        if (completedBodyTurn.arrived() || completedBodyTurn.steering() ||
            completedBodyTurn.facing() == completedBodyTurn.travelHeading())
          continue;
        int travel = completedBodyTurn.travelHeading();
        completedBodyTurn.waitForBody(raw, facing, reserved);
        check(completedBodyTurn.collisionStepHeading() == priorTravel,
              "A fresh blocked step retains its pre-step collision direction");
        check(completedBodyTurn.acceptedTravelHeading() == travel,
              "Completed body steering retains accepted rasterized travel "
              "for a new command after a body wait");
        completedBodyTurn.advance();
        check(completedBodyTurn.collisionStepHeading() == travel,
              "A waiting update refreshes collision direction without travel");
        auto copy = completedBodyTurn;
        auto trial = completedBodyTurn;
        trial.resumeBodyWait();
        trial.advance();
        completedBodyTurn.acceptBlockedWaitTurn(trial);
        check(completedBodyTurn.facing() == facing &&
                  samePoint(completedBodyTurn.rawPosition(), raw) &&
                  !completedBodyTurn.speed() && copy.facing() == facing,
              "A blocked retry preserves a completed body turn when rasterized "
              "travel changes direction");
        found = true;
        break;
      }
      check(found, "The body-wait fixture reaches a rasterized direction change");
    }
    Motion unchangedWait({500, 500}, 0, physics);
    unchangedWait.route({{500, 400}}, 0);
    for (int step = 0; step < 4; ++step)
      unchangedWait.advance();
    auto unchangedAnchor = unchangedWait.rawPosition();
    unchangedWait.waitForBody(unchangedAnchor, 1, unchangedWait.nextSpeed());
    unchangedWait.advance();
    auto unchangedTrial = unchangedWait;
    unchangedTrial.resumeBodyWait();
    unchangedTrial.advance();
    unchangedWait.acceptBlockedWaitTurn(unchangedTrial);
    check(unchangedWait.facing() == 1 && unchangedWait.travelHeading() == 0 &&
              samePoint(unchangedWait.rawPosition(), unchangedAnchor),
          "An unchanged rejected travel direction retains its prior body pose");
    Motion freshBodyPause({300, 500}, 64, physics);
    freshBodyPause.route({{700, 500}}, 0);
    for (int step = 0; step < 4; ++step)
      freshBodyPause.advance();
    auto pausedRaw = freshBodyPause.rawPosition();
    auto pausedSpeed = freshBodyPause.speed();
    freshBodyPause.prepareBodyWait(pausedRaw, freshBodyPause.facing(), pausedSpeed);
    auto freshPauseCopy = freshBodyPause;
    check(freshBodyPause.collisionPaused() && !freshBodyPause.moving(),
          "A fresh route prepares collision recovery before its body wait");
    int preparedSteps = 0;
    while (freshBodyPause.collisionPaused() && preparedSteps++ < 10) {
      freshBodyPause.advance();
      check(samePoint(freshBodyPause.rawPosition(), pausedRaw) &&
                freshBodyPause.speed() == pausedSpeed,
            "Collision preparation retains accepted inertia without displacement");
    }
    check(freshBodyPause.waitingForBody() && preparedSteps > 0 &&
              preparedSteps < 10 && freshPauseCopy.collisionPaused(),
          "Prepared body waits and copied collision delays remain independent");
    freshBodyPause.advance();
    check(freshBodyPause.speed() == 0 && samePoint(freshBodyPause.velocity(), {}),
          "The first body-wait update clears the reserved collision inertia");
    Motion expiredWait({500, 500}, 64, physics);
    expiredWait.route({{700, 500}}, 0);
    for (int step = 0; step < 4; ++step)
      expiredWait.advance();
    auto blockedAnchor = expiredWait.rawPosition();
    expiredWait.waitForBody(blockedAnchor, expiredWait.facing(), expiredWait.nextSpeed());
    expiredWait.advance();
    auto crowdedWait = expiredWait;
    expiredWait.expireBodyWait(false);
    crowdedWait.expireBodyWait(true);
    check(expiredWait.replanning() && !expiredWait.reroutesMovingObstacles() &&
              crowdedWait.replanning() && crowdedWait.reroutesMovingObstacles(),
          "Expired waits independently retain their chosen route search policy");
    expiredWait.replan({{700, 500}});
    crowdedWait.replan({{crowdedWait.position().x, 700}});
    expiredWait.advance();
    crowdedWait.advance();
    check(samePoint(expiredWait.rawPosition(), blockedAnchor) &&
              expiredWait.speed() == 0,
          "An ordinary expired wait spends a stationary route preparation frame");
    check(samePoint(crowdedWait.rawPosition(), blockedAnchor) &&
              crowdedWait.speed() == physics.acceleration &&
              crowdedWait.nextSpeed() == 0 && !crowdedWait.moving(),
          "A crowd detour previews acceleration while turning without displacement");
    expiredWait.advance();
    crowdedWait.advance();
    check(expiredWait.speed() == physics.acceleration &&
              expiredWait.rawPosition().x == blockedAnchor.x + physics.acceleration,
          "Expired wait routes restart acceleration from accepted stationary state");
    Motion angledRecovery({800, 800}, 64, physics);
    angledRecovery.route({{1000, 800}}, 0);
    for (int step = 0; step < 4; ++step)
      angledRecovery.advance();
    angledRecovery.route({{600, 800}}, 0);
    angledRecovery.advance();
    angledRecovery.advance();
    auto waitingPoint = angledRecovery.position();
    angledRecovery.waitForRecovery(waitingPoint);
    angledRecovery.advance();
    check(angledRecovery.facing() != angledRecovery.travelHeading(),
          "The recovery fixture retains distinct body and travel headings");
    auto retainedRecovery = angledRecovery.inertia();
    auto angledRecoveryCopy = angledRecovery;
    auto remoteRecovery = angledRecovery;
    Point remoteGoal{waitingPoint.x - 100, waitingPoint.y};
    remoteRecovery.waitForRecovery(remoteGoal);
    remoteRecovery.advance();
    auto remoteFacing = remoteRecovery.facing();
    remoteRecovery.waitForRecovery(remoteGoal);
    remoteRecovery.advance();
    check(remoteRecovery.facing() != remoteFacing &&
              samePoint(remoteRecovery.position(), waitingPoint),
          "Repeated recovery toward a distant goal keeps turning in place");
    angledRecovery.waitForRecovery(waitingPoint);
    angledRecovery.advance();
    check(
        angledRecovery.speed() == retainedRecovery.speed &&
            angledRecovery.travelHeading() == retainedRecovery.travelHeading &&
            samePoint(angledRecovery.velocity(), retainedRecovery.velocity) &&
            samePoint(angledRecovery.position(), waitingPoint) &&
            angledRecoveryCopy.speed() == retainedRecovery.speed,
        "Repeated recovery waits freeze accepted travel despite a heading gap");
    Motion recordedRecovery({1000, 1000}, 64, physics);
    recordedRecovery.route({{1100, 1000}}, 0);
    recordedRecovery.setRouteAge(10);
    const auto recordedOrigin = recordedRecovery.rawPosition();
    recordedRecovery.blockedWaypoint(recordedOrigin, 64, 0);
    check(recordedRecovery.reroutesRecordedObstacle(),
          "A fresh rejected leg retains its recorded collision obstacle");
    auto expiredRecovery = recordedRecovery;
    expiredRecovery.waitForBody(recordedOrigin, 64, 0);
    expiredRecovery.expireBodyWait(false);
    check(!expiredRecovery.reroutesRecordedObstacle(),
          "Expiring a blocked path releases its recorded collider before replanning");
    auto expiredMovers = recordedRecovery;
    expiredMovers.waitForBody(recordedOrigin, 64, 0);
    expiredMovers.expireBodyWait(true);
    check(expiredMovers.reroutesMovingObstacles() &&
              !expiredMovers.reroutesRecordedObstacle(),
          "An expired moving-body retry selects movers without retaining old path ownership");
    Motion queuedCarry({300, 300}, 64, physics);
    queuedCarry.keepMomentumForQueuedMove(Point{310, 300});
    queuedCarry.route({{310, 300}}, 0);
    for (int frame = 0; frame < 40 && !queuedCarry.arrived(); ++frame)
      queuedCarry.advance();
    check(queuedCarry.arrived() && queuedCarry.speed() > 192,
          "A pending move preserves travel through the current logical goal");
    auto carriedSpeed = queuedCarry.speed();
    auto carriedCopy = queuedCarry;
    queuedCarry.advance();
    carriedCopy.advance();
    check(queuedCarry.speed() == carriedSpeed + physics.acceleration &&
              queuedCarry.speed() == carriedCopy.speed() &&
              samePoint(queuedCarry.position(), {310, 300}),
          "Queued arrival retains one reserved acceleration without "
          "displacement");
    queuedCarry.advance();
    check(queuedCarry.speed() == 0,
          "The stationary frame following queued arrival clears active speed");
    dat::Flingy restingTargetPhysics{0, 512, 0, 1, 20, 2};
    Motion restingLarva({100, 100}, 64, restingTargetPhysics);
    restingLarva.faceOnArrival(Point{200, 160});
    restingLarva.advance();
    auto restingCopy = restingLarva;
    check(restingLarva.facing() == 84 && restingLarva.speed() == 0 &&
              samePoint(restingLarva.position(), {100, 100}),
          "A stationary arrival target starts a bounded turn without travel");
    restingLarva.advance();
    restingCopy.advance();
    check(restingLarva.facing() == heading({100, 60}) &&
              restingCopy.facing() == restingLarva.facing() &&
              restingLarva.travelHeading() == restingLarva.facing() &&
              samePoint(restingLarva.position(), {100, 100}),
          "A stationary arrival turn finishes across frames and value copies");
    dat::Flingy reroutePhysics{0, 512, 2048, 64, 32, 0};
    Motion brakingBodyWait({300, 300}, 64, reroutePhysics);
    brakingBodyWait.route({{350, 300}}, 0);
    bool retriedBraking = false;
    for (int n = 0; n < 100 && !retriedBraking; ++n) {
      auto previous = brakingBodyWait.rawPosition();
      auto previousHeading = brakingBodyWait.facing();
      auto reservedSpeed = brakingBodyWait.nextSpeed();
      brakingBodyWait.advance();
      if (brakingBodyWait.speed() < reservedSpeed &&
          !samePoint(previous, brakingBodyWait.rawPosition())) {
        brakingBodyWait.waitForBody(previous, previousHeading, reservedSpeed);
        brakingBodyWait.advance();
        brakingBodyWait.resumeBodyWait();
        brakingBodyWait.advance();
        check(brakingBodyWait.speed() == reservedSpeed &&
                  samePoint(brakingBodyWait.rawPosition(),
                            {previous.x + reservedSpeed, previous.y}),
              "A cleared body wait restores its pre-braking travel budget");
        retriedBraking = true;
      }
    }
    check(retriedBraking, "The original body-wait fixture reaches braking");
    {
      dat::Flingy clearancePhysics{0, 1280, 12227, 67, 40, 0};
      Motion brakingClearance({350, 300}, -64, clearancePhysics);
      brakingClearance.route({{200, 300}}, 0);
      for (int n = 0; n < 8; ++n)
        brakingClearance.advance();
      auto at = brakingClearance.position();
      brakingClearance.route({{at.x - 5, at.y}}, 0);
      auto previous = brakingClearance.rawPosition();
      auto previousHeading = brakingClearance.facing();
      auto reservedSpeed = brakingClearance.nextSpeed();
      brakingClearance.advance();
      int collisionSpeed = brakingClearance.speed();
      check(collisionSpeed < reservedSpeed, "The clearance contact brakes");
      brakingClearance.waitForClearance(previous, previousHeading, reservedSpeed);
      auto waitingCopy = brakingClearance;
      brakingClearance.advance();
      brakingClearance.resumeBodyWait();
      brakingClearance.advance();
      int resumedSpeed = collisionSpeed - clearancePhysics.acceleration;
      check(brakingClearance.speed() == resumedSpeed &&
                samePoint(brakingClearance.rawPosition(),
                          {previous.x - resumedSpeed, previous.y}) &&
                samePoint(waitingCopy.rawPosition(), previous),
            "A clearance retry decelerates from the collision speed while "
            "its waiting copy preserves the rejected position");
    }
    Motion movingReroute({300, 300}, 0, reroutePhysics);
    movingReroute.route({{300, 100}}, 0);
    for (int n = 0; n < 12; ++n)
      movingReroute.advance();
    auto beforeCollision = movingReroute.rawPosition();
    auto rerouteFacing = movingReroute.facing();
    auto beforeNextSpeed = movingReroute.nextSpeed();
    movingReroute.advance();
    movingReroute.blockedWaypoint(beforeCollision, rerouteFacing,
                                  beforeNextSpeed, true);
    check(movingReroute.reroutesMovingObstacles(),
          "The original moving-body fixture enters route recovery");
    auto turnOrigin = movingReroute.rawPosition();
    auto turnPixel = movingReroute.position();
    auto turnSpeed = movingReroute.speed();
    auto reservedTurn = movingReroute;
    reservedTurn.blockedWaypoint(turnOrigin, reservedTurn.facing(),
                                 turnSpeed - reroutePhysics.acceleration, true);
    reservedTurn.replan({{turnPixel.x - 20, turnPixel.y}});
    reservedTurn.advance();
    check(reservedTurn.speed() == turnSpeed &&
              reservedTurn.nextSpeed() ==
                  turnSpeed - reroutePhysics.acceleration &&
              samePoint(reservedTurn.rawPosition(), turnOrigin),
          "Route alignment preserves a distinct reserved travel budget");
    movingReroute.replan({{turnPixel.x - 20, turnPixel.y}});
    movingReroute.advance();
    check(samePoint(movingReroute.rawPosition(), turnOrigin) &&
              movingReroute.speed() == turnSpeed &&
              movingReroute.facing() == -32 &&
              movingReroute.travelHeading() == -16,
          "A successful moving-body reroute aligns without losing inertia");
    Motion landing({300, 300}, 32, physics);
    landing.route({{400, 400}}, 0);
    landing.advance();
    auto landingPixel = landing.position();
    auto landingInertia = landing.inertia();
    check(landing.rawPosition().x != landingPixel.x * 256,
          "Landing fixture retains a fractional coordinate");
    landing.alignToPixel();
    check(samePoint(landing.rawPosition(),
                    {landingPixel.x * 256, landingPixel.y * 256}) &&
              landing.speed() == landingInertia.speed &&
              samePoint(landing.velocity(), landingInertia.velocity) &&
              landing.travelHeading() == landingInertia.travelHeading,
          "Landing aligns the anchor without consuming residual inertia");
    Motion airLanding({300, 300}, 32, physics, true);
    rejects([&] { airLanding.alignToPixel(); });
    Motion directedAir({1024, 1024}, 64, physics, true);
    directedAir.route({{1152, 1024}}, 0);
    for (int frame = 0; frame < 128 && !directedAir.arrived(); ++frame)
      directedAir.advance();
    check(directedAir.arrived() && directedAir.facing() == 64,
          "Directed flyer reaches its destination facing along its route");
    auto airArrivalCopy = directedAir;
    directedAir.advance();
    airArrivalCopy.advance();
    check(directedAir.facing() == 64 && directedAir.travelHeading() == 64 &&
              airArrivalCopy.facing() == directedAir.facing(),
          "A flyer settling at its commanded destination retains its facing");
    Motion roamingAir({1024, 1024}, 64, physics, true);
    roamingAir.route({{1152, 1024}}, 0, false, true, true);
    for (int frame = 0; frame < 128 && !roamingAir.arrived(); ++frame)
      roamingAir.advance();
    check(roamingAir.arrived(), "Autonomous flyer reaches its travel goal");
    auto roamingCopy = roamingAir;
    roamingAir.advance();
    roamingCopy.advance();
    check(roamingAir.desiredHeading() == -64 &&
              roamingAir.facing() != directedAir.facing() &&
              roamingCopy.facing() == roamingAir.facing(),
          "Autonomous air arrival steers toward its retained rest anchor in independent copies");
    Motion waitingDirection({1024, 1024}, -64, physics);
    waitingDirection.route({{32, 1024}}, 0);
    waitingDirection.advance();
    waitingDirection.route({{1024, 32}}, 0);
    auto waitingRaw = waitingDirection.rawPosition();
    auto waitingFacing = waitingDirection.facing();
    auto waitingSpeed = waitingDirection.nextSpeed();
    waitingDirection.advance();
    auto pausedDirection = waitingDirection;
    pausedDirection.blocked(waitingRaw, waitingFacing, waitingSpeed);
    check(pausedDirection.collisionPaused() &&
              pausedDirection.travelHeading() == -44 &&
              pausedDirection.acceptedTravelHeading() == -64,
          "A collision pause retains its accepted movement direction");
    waitingDirection.waitForBody(waitingRaw, waitingFacing, waitingSpeed);
    check(waitingDirection.travelHeading() == -44 &&
              waitingDirection.acceptedTravelHeading() == -64,
          "A rejected turn does not replace the accepted movement direction");
    auto waitingCopy = waitingDirection;
    waitingDirection.advance();
    check(waitingDirection.acceptedTravelHeading() == -64,
          "Repeated body waits preserve the bound target projection direction");
    waitingCopy.resumeBodyWait();
    waitingCopy.advance();
    check(waitingCopy.acceptedTravelHeading() == waitingCopy.travelHeading() &&
              waitingDirection.acceptedTravelHeading() == -64,
          "Resuming a copied body wait accepts its new movement direction");
    Motion boundContactTurn({1024, 1024}, -97, physics);
    boundContactTurn.route({{1019, 1024}}, 0);
    check(!boundContactTurn.needsWaypointContinuation(),
          "A new contact route first establishes its travel direction");
    boundContactTurn.advance();
    check(boundContactTurn.needsWaypointContinuation() &&
              !boundContactTurn.arrived(),
          "A sharp turn can pass a terminal contact waypoint before arrival");
    Motion boundContactStraight({1024, 1024}, -64, physics);
    boundContactStraight.route({{1019, 1024}}, 0);
    boundContactStraight.advance();
    check(!boundContactStraight.needsWaypointContinuation(),
          "Aligned travel retains its terminal contact waypoint");
    for (int n = 0; n < 100 && !boundContactStraight.arrived(); ++n)
      boundContactStraight.advance();
    check(boundContactStraight.arrived() &&
              boundContactStraight.needsWaypointContinuation(),
          "Contact arrival can request the next moving-target route leg");
    Motion followProjection({100, 100}, 64, physics);
    check(samePoint(followProjection.projectFollowTarget({146, 285}, -84),
                    {146, 285}),
          "A stationary follower does not project its target");
    followProjection.route({{1000, 100}}, 0);
    for (int i = 0; i < 4; ++i)
      followProjection.advance();
    check(followProjection.speed() == 268 &&
              samePoint(followProjection.projectFollowTarget({146, 285}, -84),
                        {140, 287}) &&
              samePoint(followProjection.projectFollowTarget({146, 285}, 0),
                        {146, 278}),
          "Follow projection uses stopping travel and floors negative offsets");
    followProjection.advance();
    check(followProjection.speed() == 335 &&
              samePoint(followProjection.projectFollowTarget({146, 285}, 64),
                        {155, 285}),
          "Follow projection retains fractional stopping travel");
    for (int i = 0; i < 15; ++i)
      followProjection.advance();
    check(followProjection.speed() == 1280 &&
              samePoint(followProjection.projectFollowTarget({146, 285}, 0),
                        {146, 141}) &&
              samePoint(followProjection.projectFollowTarget({146, 285}, -64),
                        {2, 285}),
          "Full-speed Follow projection uses signed fixed-point travel");
    auto roundedFollow = followProjection;
    roundedFollow.setTopSpeed(214);
    roundedFollow.advance();
    check(roundedFollow.speed() == 214 &&
              samePoint(roundedFollow.projectFollowTarget({146, 285}, 64),
                        {149, 285}) &&
              samePoint(roundedFollow.projectFollowTarget({146, 285}, 0),
                        {146, 281}),
          "Follow divides stopping travel before scaling the projection");
    roundedFollow.setTopSpeed(7);
    roundedFollow.advance();
    check(samePoint(roundedFollow.projectFollowTarget({146, 285}, 0),
                    {146, 285}),
          "Sub-unit stopping travel produces no Follow lead");
    Motion partialLeg({256, 1024}, 64, physics);
    partialLeg.route({{320, 1024}, {384, 1024}, {448, 1024}, {512, 1024}}, 0,
                     false, false);
    Motion finalLeg({256, 1024}, 64, physics);
    finalLeg.route({{320, 1024}, {384, 1024}, {448, 1024}, {512, 1024}}, 0);
    for (int i = 0; i < 55; ++i) {
      partialLeg.advance();
      finalLeg.advance();
    }
    check(partialLeg.speed() == 1280 && finalLeg.speed() < 1280 &&
              partialLeg.rawPosition().x > finalLeg.rawPosition().x,
          "Partial routes retain speed while final routes brake");
    for (int i = 0; i < 40 && !partialLeg.arrived(); ++i)
      partialLeg.advance();
    check(partialLeg.arrived() && partialLeg.speed() == 1280,
          "A partial endpoint retains accepted travel speed");
    partialLeg.route({{576, 1024}, {640, 1024}, {704, 1024}, {768, 1024}}, 0,
                     false, false);
    partialLeg.advance();
    check(samePoint(partialLeg.position(), {517, 1024}) &&
              partialLeg.speed() == 1280,
          "Continuation consumes its first frame at the retained speed");
    Motion arrivingDebt({1000, 1000}, 64, physics);
    arrivingDebt.route({{1100, 1000}}, 0);
    auto acceptedRaw = arrivingDebt.rawPosition();
    arrivingDebt.advance();
    arrivingDebt.blocked(acceptedRaw, 64);
    check(arrivingDebt.collisionDebt() == 1,
          "Blocked movement retains collision debt");
    auto unfinishedDebt = arrivingDebt;
    arrivingDebt.route({arrivingDebt.position()}, 0);
    for (int i = 0; i < 3; ++i)
      arrivingDebt.advance();
    check(arrivingDebt.collisionDebt() == 0 &&
              unfinishedDebt.collisionDebt() == 1,
          "Arrival clears collision debt independently of an unfinished "
          "snapshot");
    Motion motion({1024, 1024}, 104, physics);
    motion.route(openGroundRoute({1024, 1024}, {1280, 1024}));
    for (int frame = 1; frame <= 80; ++frame) {
      motion.advance();
      auto p = motion.rawPosition();
      if (frame == 2)
        check(p.x == 262144 && p.y == 262144 && motion.speed() == 0,
              "Movement planning delay");
      if (frame == 3)
        check(p.x == 262203 && p.y == 262175 && motion.speed() == 67 &&
                  motion.travelHeading() == 84,
              "First accelerated step");
      if (frame == 24)
        check(p.x == 278528 && p.y == 262144, "Waypoint snap");
      if (frame == 54)
        check(p.x == 316416 && motion.speed() == 1280, "Cruise speed");
      if (frame == 55)
        check(p.x == 317629 && motion.speed() == 1213, "Braking onset");
      if (frame == 68) {
        auto held = motion;
        held.hold({1280, 1000});
        int previousSpeed = held.speed();
        for (int i = 0; i < 8 && !held.stopped(); ++i) {
          held.advance();
          check(samePoint(held.rawPosition(), p) &&
                    held.speed() <= previousSpeed,
                "Interaction braking holds position until rest");
          previousSpeed = held.speed();
        }
        check(held.stopped(), "Interaction braking finishes at rest");
      }
      if (frame == 70)
        check(p.x == 327680 && motion.speed() == 208, "Arrival speed");
      if (frame == 71)
        check(motion.speed() == 141, "Arrival settling");
      if (frame == 72)
        check(motion.speed() == 0 && motion.stopped(), "Rest transition");
    }
    Motion shortTail({1056, 1072}, 104, physics);
    shortTail.route({{1120, 1072}, {1184, 1072}, {1248, 1072}, {1256, 1082}},
                    2);
    for (int i = 0; i < 42; ++i)
      shortTail.advance();
    check(shortTail.speed() == 1213 &&
              samePoint(shortTail.rawPosition(), {309437, 274432}),
          "A short terminal segment brakes before the preceding waypoint");
    Motion nearCorner({960, 976}, 104, physics);
    nearCorner.route({{963, 964}, {1027, 964}, {1085, 964}}, 0);
    nearCorner.advance();
    check(nearCorner.desiredHeading() == 58 && nearCorner.facing() == 64 &&
              nearCorner.travelHeading() == 84 &&
              samePoint(nearCorner.rawPosition(), {245819, 249887}),
          "A sharp nearby intermediate corner advances the movement waypoint");
    auto northeast = openGroundRoute({1024, 1024}, {1280, 768});
    check(northeast.size() == 7 && samePoint(northeast[0], {1088, 1024}) &&
              samePoint(northeast[3], {1280, 960}),
          "Region-sensitive empty-terrain route");
    auto corner = openGroundRoute({1024, 1024}, {1300, 1110});
    check(corner.size() == 5 && samePoint(corner[1], {1152, 1088}),
          "Integer route-cost ordering");
    Motion a({1024, 1024}, 104, physics);
    a.route(corner);
    for (int i = 0; i < 20; ++i)
      a.advance();
    auto b = a;
    for (int i = 0; i < 10; ++i) {
      a.advance();
      b.advance();
      check(samePoint(a.rawPosition(), b.rawPosition()),
            "Motion clone repeatability");
    }
    b.route({{900, 1000}});
    b.advance();
    check(!samePoint(a.waypoint(), b.waypoint()),
          "Motion clone route ownership");
    rejects(
        [&] { Motion bad({std::numeric_limits<int>::max(), 0}, 0, physics); });
    rejects([&] { a.route({}); });
    rejects([&] { openGroundRoute({0, 0}, {-1, 0}); });
    dat::Flingy scripted{0, 1024, 1, 1, 27, 2};
    Motion animationMotion({1024, 1024}, 40, scripted);
    animationMotion.route({{1056, 1024}}, 0);
    animationMotion.advance();
    check(animationMotion.moving() && animationMotion.speed() == 0 &&
              animationMotion.facing() == 64,
          "Animation starts movement before supplying speed");
    for (int step = 0; step < 8; ++step) {
      animationMotion.scriptSpeed(1024);
      animationMotion.advance();
      check(animationMotion.position().x == 1028 + step * 4 &&
                animationMotion.speed() == 0,
            "Animation speed consumed once per physical step");
    }
    animationMotion.advance();
    check(animationMotion.settling(),
          "Animation motion settles at destination");
    animationMotion.advance();
    check(animationMotion.stopped() && !animationMotion.moving(),
          "Animation motion stops");
    rejects([&] { animationMotion.scriptSpeed(-1); });
    rejects([&] { a.scriptSpeed(1024); });
    Motion terrainRetry({1024, 1024}, 35, scripted);
    terrainRetry.route({{1056, 1024}}, 0);
    auto terrainOrigin = terrainRetry.rawPosition();
    terrainRetry.scriptSpeed(1024);
    terrainRetry.advance();
    terrainRetry.terrainBlocked(terrainOrigin, 1024);
    check(samePoint(terrainRetry.rawPosition(), terrainOrigin) &&
              terrainRetry.facing() == 62 && terrainRetry.updatesHeading() &&
              terrainRetry.moving() && !terrainRetry.replanning(),
          "Terrain rejection preserves an animation-driven turn");
    terrainRetry.scriptSpeed(1024);
    terrainRetry.advance();
    check(terrainRetry.facing() == 64 && terrainRetry.position().x == 1028 &&
              terrainRetry.position().y == 1024,
          "Animation terrain retry resumes on the next clear step");
    Motion terrainTurn({3027, 256}, -128, physics);
    terrainTurn.route({{3027, 595}}, 0, false, false);
    for (int i = 0; i < 120 && !terrainTurn.arrived(); ++i)
      terrainTurn.advance();
    check(terrainTurn.arrived() && terrainTurn.speed() == 1280,
          "Terrain approach retains speed at a partial boundary");
    terrainTurn.route({{3019, 603}, {3011, 611}, {2947, 675}, {2883, 739}}, 0,
                      false, false);
    terrainTurn.advance();
    terrainTurn.advance();
    check(samePoint(terrainTurn.rawPosition(), {773357, 154310}),
          "The terrain turn reaches the observed fractional contact");
    auto terrainContact = terrainTurn.rawPosition();
    terrainTurn.advance();
    terrainTurn.terrainBlocked(terrainContact);
    check(terrainTurn.terrainFixing() && terrainTurn.collisionDebt() == 1 &&
              terrainTurn.terrainRetryChanged() &&
              samePoint(terrainTurn.rawPosition(), terrainContact),
          "Terrain contact retains velocity without committing its step");
    terrainTurn.advance();
    terrainTurn.rejectTerrainRetry(terrainContact, -128);
    check(terrainTurn.collisionDebt() == 2 &&
              samePoint(terrainTurn.rawPosition(), terrainContact),
          "Terrain correction prepares a tangent without displacement");
    terrainTurn.advance();
    check(terrainTurn.terrainSlidingStep() &&
              terrainTurn.collisionDebt() == 3 &&
              samePoint(terrainTurn.rawPosition(), {773357, 155523}) &&
              samePoint(terrainTurn.velocity(), {-1123, 464}) &&
              terrainTurn.speed() == 1213 && terrainTurn.facing() == -94 &&
              terrainTurn.travelHeading() == -80 &&
              terrainTurn.desiredHeading() == -128,
          "A terrain tangent separates accepted travel from the route preview");
    auto continuedTangent = terrainTurn;
    continuedTangent.continueTerrainSlide();
    continuedTangent.advance();
    check(std::abs(continuedTangent.speed() - terrainTurn.speed()) == physics.acceleration &&
              continuedTangent.rawPosition().y ==
                  terrainTurn.rawPosition().y + continuedTangent.speed(),
          "Repeated terrain tangents advance from the last accepted speed");
    terrainTurn.advance();
    check(samePoint(terrainTurn.rawPosition(), {772432, 156408}) &&
              terrainTurn.speed() == 1280 && terrainTurn.facing() == -80 &&
              terrainTurn.travelHeading() == -95 &&
              terrainTurn.desiredHeading() == -80,
          "Terrain exit resumes from its accepted slide direction");
    Motion retriedCorner({1024, 1024}, 0, physics);
    retriedCorner.route({{1024, 32}}, 0, false, false);
    for (int i = 0; i < 20; ++i)
      retriedCorner.advance();
    auto cornerPosition = retriedCorner.position();
    retriedCorner.route({{cornerPosition.x - 8, cornerPosition.y - 1},
                         {cornerPosition.x - 8, cornerPosition.y - 48}},
                        0, false, false);
    auto cornerContact = retriedCorner.rawPosition();
    retriedCorner.advance();
    retriedCorner.terrainBlocked(cornerContact);
    retriedCorner.advance();
    retriedCorner.holdTerrainRetry(cornerContact);
    retriedCorner.advance();
    retriedCorner.advance();
    check(retriedCorner.facing() == -64 && retriedCorner.travelHeading() == -64,
          "An aligned terrain retry retains the next facing update");
    Motion slideCorner({1024, 1024}, 27, physics);
    slideCorner.route({{1084, 944}}, 0, false, false);
    for (int i = 0; i < 120 && !slideCorner.arrived(); ++i)
      slideCorner.advance();
    slideCorner.route({{1087, 940}, {1087, 876}}, 0, false, false);
    auto slideContact = slideCorner.rawPosition();
    slideCorner.advance();
    slideCorner.terrainBlocked(slideContact);
    slideCorner.rejectTerrainRetry(slideContact, 0);
    slideCorner.advance();
    check(slideCorner.advancesTerrainCorner(),
          "A terrain tangent can pass a sharp intermediate corner");
    slideCorner.advance();
    check(samePoint(slideCorner.waypoint(), {1087, 876}),
          "A passed terrain corner resumes the next route segment");
    for (bool movingObstacles : {false, true}) {
      Motion rerouted({1024, 1024}, 0, physics);
      rerouted.route({{1024, 700}}, 0, false, false);
      for (int i = 0; i < 120 && !rerouted.arrived(); ++i)
        rerouted.advance();
      rerouted.route({{960, 704}}, 0, false, false);
      rerouted.advance();
      auto rerouteContact = rerouted.rawPosition();
      auto rerouteFacing = rerouted.facing();
      auto rerouteSpeed = rerouted.nextSpeed();
      rerouted.advance();
      rerouted.blockedWaypoint(rerouteContact, rerouteFacing, rerouteSpeed,
                               movingObstacles);
      check(!rerouted.replanning() && rerouted.speed() == 1280 &&
                samePoint(rerouted.rawPosition(), rerouteContact),
            "A young occupied waypoint retains physics during its collision "
            "pause");
      auto clearedPause = rerouted;
      clearedPause.retryCollision();
      clearedPause.advance();
      check(rerouted.collisionPaused() && !clearedPause.collisionPaused() &&
                !clearedPause.replanning() && clearedPause.speed() == 1280 &&
                !samePoint(clearedPause.rawPosition(), rerouteContact),
            "A cleared collision pause retries its retained motion "
            "independently");
      for (int i = 0; i < 7 && !rerouted.replanning(); ++i)
        rerouted.advance();
      check(rerouted.replanning() && rerouted.speed() == 1280 &&
                samePoint(rerouted.rawPosition(), rerouteContact),
            "The collision pause requests a fresh route without braking");
      auto reroutePosition = rerouted.position();
      rerouted.replan({{reroutePosition.x, reroutePosition.y - 27}}, false);
      rerouted.advance();
      check(rerouted.facing() == 0 && rerouted.travelHeading() == 0 &&
                rerouted.speed() == 1280 &&
                samePoint(rerouted.rawPosition(),
                          {rerouteContact.x,
                           rerouteContact.y - (movingObstacles ? 0 : 1280)}),
            "Moving-obstacle replans face the new waypoint before displacement; "
            "ordinary replans retain their accepted travel");
      if (movingObstacles) {
        rerouted.advance();
        check(samePoint(rerouted.rawPosition(),
                        {rerouteContact.x, rerouteContact.y - 1280}),
              "The aligned moving-obstacle route starts on the next frame");
      }
    }
    Motion alignedTerrain({1024, 1024}, 64, physics);
    Motion escapedTerrain({1024, 1024}, -64, physics);
    escapedTerrain.route({{512, 1024}}, 0, false, false);
    for (int i = 0; i < 20; ++i)
      escapedTerrain.advance();
    auto escapeContact = escapedTerrain.rawPosition();
    escapedTerrain.terrainBlocked(escapeContact);
    escapedTerrain.advance();
    escapedTerrain.holdTerrainRetry(escapeContact);
    escapedTerrain.advance();
    escapedTerrain.rejectTerrainRetry(escapeContact, 0, true);
    escapedTerrain.advance();
    auto escapeAccepted = escapedTerrain.rawPosition();
    check(escapedTerrain.terrainEscapeStep() &&
              escapeAccepted.y == escapeContact.y - 1280,
          "An axial terrain collision permits a perpendicular escape");
    escapedTerrain.continueTerrainSlide();
    escapedTerrain.advance();
    escapedTerrain.blockedTerrainEscape(escapeAccepted);
    check(escapedTerrain.replanning() && !escapedTerrain.terrainSlidingStep() &&
              escapedTerrain.moving() &&
              escapedTerrain.travelHeading() == -24 &&
              escapedTerrain.collisionDebt() == 5 &&
              samePoint(escapedTerrain.rawPosition(), escapeAccepted),
          "A blocked escape retains its attempted direction and requests a "
          "route");
    auto escapePosition = escapedTerrain.position();
    escapedTerrain.replan({{escapePosition.x - 1, escapePosition.y + 6}},
                          false);
    escapedTerrain.advance();
    check(escapedTerrain.speed() == 0 && escapedTerrain.nextSpeed() == 0 &&
              samePoint(escapedTerrain.velocity(), {}) &&
              escapedTerrain.travelHeading() == -24 &&
              escapedTerrain.collisionDebt() == 6,
          "A terrain escape replan stops before preparing its new turn");
    alignedTerrain.route({{1800, 1024}}, 0);
    for (int i = 0; i < 20; ++i)
      alignedTerrain.advance();
    auto alignedContact = alignedTerrain.rawPosition();
    alignedTerrain.advance();
    alignedTerrain.terrainBlocked(alignedContact);
    check(
        !alignedTerrain.terrainRetryChanged() &&
            alignedTerrain.collisionDebt() == 1 &&
            alignedTerrain.speed() == 1280 &&
            samePoint(alignedTerrain.rawPosition(), alignedContact),
        "An accepted straight direction permits tangent selection on contact");
    auto repeatedTerrain = alignedTerrain;
    repeatedTerrain.advance();
    auto repeatedContact = repeatedTerrain.rawPosition();
    repeatedTerrain.advance();
    repeatedTerrain.terrainBlocked(repeatedContact);
    check(
        repeatedTerrain.collisionDebt() == 3 && !repeatedTerrain.moving() &&
            repeatedTerrain.speed() > 0 &&
            samePoint(repeatedTerrain.rawPosition(), repeatedContact),
        "Repeated terrain rejection stops walking without discarding inertia");
    auto lateralTerrain = alignedTerrain;
    lateralTerrain.route(
        {{lateralTerrain.position().x - 1, lateralTerrain.position().y + 64}},
        0, false, false);
    lateralTerrain.rejectTerrainRetry(alignedContact, -64);
    lateralTerrain.advance();
    check(samePoint(lateralTerrain.rawPosition(),
                    {alignedContact.x - 1280, alignedContact.y}),
          "A terrain tangent can pass the waypoint's axis projection");
    Motion preparing({1024, 1024}, 106, physics);
    preparing.route({{1024, 32}}, 0);
    for (int i = 0; i < 4; ++i)
      preparing.advance();
    auto preparedRaw = preparing.rawPosition();
    int preparedFacing = preparing.facing();
    preparing.advance();
    preparing.contactBlocked(preparedRaw, preparedFacing,
                             {preparedRaw.x / 256, 32});
    preparing.advance();
    check(preparing.travelHeading() == 6 && preparing.facing() == 0,
          "Contact retains an accepted direction distinct from facing");
    preparing.prepareRouteFrame();
    check(preparing.travelHeading() == 6 && preparing.facing() == 0 &&
              samePoint(preparing.rawPosition(), preparedRaw) &&
              preparing.speed() == 0 && !preparing.updatesHeading(),
          "Deferred planning freezes contact orientation and physical state");
    auto preparedPosition = preparing.position();
    preparing.route({{preparedPosition.x - 64, preparedPosition.y}}, 1);
    preparing.advance();
    preparing.advance();
    check(preparing.travelHeading() == -14 && preparing.facing() == -40 &&
              samePoint(preparing.velocity(), {-23, -64}),
          "Departure resumes from the heading retained through preparation");
    Motion collision({1024, 1024}, -64, physics);
    collision.route({{1280, 1024}}, 0);
    auto before = collision.rawPosition();
    int beforeFacing = collision.facing();
    collision.advance();
    collision.blocked(before, beforeFacing);
    check(
        samePoint(collision.rawPosition(), before) && !collision.moving() &&
            collision.facing() == beforeFacing && collision.speed() == 67 &&
            collision.nextSpeed() == 0,
        "Rejected motion preserves the attempted velocity and prior transform");
    for (int i = 0; i < 7; ++i) {
      collision.advance();
      check(samePoint(collision.rawPosition(), before),
            "Collision pause moves the unit");
    }
    check(collision.replanning() && !collision.speed(),
          "Collision retry requests a route");
    Motion shortWait({1024, 1024}, 64, physics);
    shortWait.route({{1800, 1024}}, 0);
    for (int i = 0; i < 5; ++i)
      shortWait.advance();
    auto waitOrigin = shortWait.rawPosition();
    int waitSpeed = shortWait.nextSpeed();
    shortWait.advance();
    shortWait.blocked(waitOrigin, 64, waitSpeed);
    shortWait.advance();
    shortWait.advance();
    check(shortWait.replanning() && shortWait.moving() && !shortWait.speed(),
          "A short collision wait preserves walking when its final frame "
          "replans");
    Motion debtSlide({1024, 1024}, 64, physics);
    debtSlide.route({{1800, 1024}}, 0);
    for (int i = 0; i < 3; ++i)
      debtSlide.advance();
    debtSlide.slideBlocked(debtSlide.rawPosition(), 64, 0,
                           debtSlide.nextSpeed());
    debtSlide.advance();
    debtSlide.finishSlide(debtSlide.rawPosition(), true);
    debtSlide.acceptedGroundStep();
    debtSlide.blocked(debtSlide.rawPosition(), debtSlide.facing(),
                      debtSlide.nextSpeed());
    check(debtSlide.moving(),
          "A successful step reduces accumulated collision debt");
    debtSlide.advance();
    check(
        !debtSlide.moving(),
        "Sliding history shortens the next collision's walking-to-idle delay");
    collision.replan({{1280, 1024}});
    auto collisionCopy = collision;
    for (int i = 0; i < 140 && !collision.stopped(); ++i) {
      collision.advance();
      collisionCopy.advance();
      check(samePoint(collision.rawPosition(), collisionCopy.rawPosition()) &&
                collision.speed() == collisionCopy.speed() &&
                collision.nextSpeed() == collisionCopy.nextSpeed(),
            "Collision recovery snapshot differs");
    }
    check(collision.stopped() && samePoint(collision.position(), {1280, 1024}),
          "Collision turn recovers forward motion");
    Motion alignedRetry({1024, 1024}, -54, physics);
    alignedRetry.route({{800, 1024}}, 0);
    auto alignedOrigin = alignedRetry.rawPosition();
    alignedRetry.advance();
    alignedRetry.blocked(alignedOrigin, -54);
    for (int i = 0; i < 10 && !alignedRetry.replanning(); ++i)
      alignedRetry.advance();
    alignedRetry.replan({{800, 1024}});
    alignedRetry.advance();
    alignedRetry.advance();
    check(samePoint(alignedRetry.rawPosition(), alignedOrigin),
          "A new recovery direction consumes its first turn before moving");
    alignedRetry.advance();
    check(alignedRetry.facing() == -54 && alignedRetry.travelHeading() == -64 &&
              alignedRetry.rawPosition().x < alignedOrigin.x,
          "Recovery already aligned with its waypoint moves without turning "
          "the sprite toward the path");
    Motion slide({1323, 795}, -42, physics);
    slide.route({{1259, 859}}, 0);
    auto slideOrigin = slide.rawPosition();
    int slideFacing = slide.facing();
    slide.advance();
    slide.slideBlocked(slideOrigin, slideFacing, -128);
    auto slideCopy = slide;
    slide.advance();
    slideCopy.advance();
    check(slide.slidingStep() && slide.moving() && slide.speed() == 134 &&
              slide.nextSpeed() == 134 && slide.facing() == -82 &&
              slide.travelHeading() == -62 &&
              samePoint(slide.rawPosition(), {338688, 203654}) &&
              samePoint(slide.velocity(), {-134, -7}) &&
              samePoint(slide.rawPosition(), slideCopy.rawPosition()),
          "A collision slide keeps reported inertia separate from its tangent "
          "step");
    Motion fastSlide({1024, 1024}, -64, physics);
    fastSlide.route({{128, 1024}}, 0);
    for (int i = 0; i < 20; ++i)
      fastSlide.advance();
    auto fastOrigin = fastSlide.rawPosition();
    fastSlide.slideBlocked(fastOrigin, -64, -64, 1280);
    check(fastSlide.nextSpeed() == 1280,
          "Slide preparation retains the pre-collision next speed");
    fastSlide.advance();
    check(fastSlide.speed() == 707 &&
              samePoint(fastSlide.rawPosition(),
                        {fastOrigin.x - 256, fastOrigin.y}) &&
              samePoint(fastSlide.velocity(), {-707, 0}),
          "Cruising slide reduces inertia and limits its tangent step to one "
          "pixel");
    auto sustained = fastSlide;
    sustained.finishSlide(sustained.rawPosition(), false);
    auto sustainedCopy = sustained;
    sustained.advance();
    sustainedCopy.advance();
    check(sustained.slidingStep() && sustained.speed() == 774 &&
              sustained.speed() == sustainedCopy.speed() &&
              samePoint(sustained.rawPosition(), sustainedCopy.rawPosition()),
          "A sustained slide accelerates without repeating entry braking and "
          "snapshots independently");
    dat::Flingy larvaPhysics{0, 1, 0, 1, 20, 2};
    Motion scriptedSlide({983, 1050}, 0, larvaPhysics);
    scriptedSlide.setTopSpeed(512);
    scriptedSlide.route({{983, 1040}}, 0);
    scriptedSlide.scriptSpeed(512);
    auto scriptedOrigin = scriptedSlide.rawPosition();
    scriptedSlide.advance();
    scriptedSlide.slideBlocked(scriptedOrigin, 0, 64, 512);
    auto scriptedSnapshot = scriptedSlide;
    scriptedSlide.advance();
    scriptedSnapshot.advance();
    check(scriptedSlide.slidingStep() && scriptedSlide.speed() == 512 &&
              scriptedSlide.nextSpeed() == 512 &&
              scriptedSlide.travelHeading() == 20 &&
              samePoint(scriptedSlide.rawPosition(),
                        {scriptedOrigin.x + 256, scriptedOrigin.y}) &&
              samePoint(scriptedSlide.rawPosition(),
                        scriptedSnapshot.rawPosition()),
          "Script-controlled slides use the measured walking speed and full "
          "turn rate while preserving independent snapshots");
    rejects([&] { scriptedSlide.setTopSpeed(-1); });
    Motion stationary({1024, 1024}, 64, physics);
    Motion boundedAir({16, 1024}, -54, physics, true);
    boundedAir.clipStep({3898, 1024 * 256});
    boundedAir.setAirPixelPosition({16, 1024});
    auto boundedCopy = boundedAir;
    boundedAir.route({{16, 1125}}, 0);
    boundedAir.advance();
    check(samePoint(boundedAir.position(), {16, 1024}) &&
              boundedAir.rawPosition().x < 16 * 256 &&
              boundedAir.desiredHeading() == -128 &&
              boundedCopy.rawPosition().x == 3898,
          "A flyer steers from its bounded pixel position without clamping its "
          "raw motion");
    Motion alignedInteraction({1262, 996}, 104, physics);
    alignedInteraction.hold({1280, 1024});
    alignedInteraction.advance();
    check(alignedInteraction.stopped() && alignedInteraction.facing() == 104 &&
              alignedInteraction.travelHeading() == 104 &&
              alignedInteraction.desiredHeading() == 0 &&
              samePoint(alignedInteraction.velocity(), {}),
          "An aligned interaction preserves the previous steering request");
    stationary.hold({1024, 1100});
    stationary.advance();
    const int priorTravel = stationary.travelHeading();
    const int priorDesired = stationary.desiredHeading();
    stationary.route({stationary.position()}, 0);
    stationary.advance();
    check(stationary.stopped() && stationary.travelHeading() == priorTravel &&
              stationary.desiredHeading() == priorDesired,
          "A stationary contact route preserves the existing orientation");
    Motion stationaryRestart({1024, 1024}, -76, physics);
    stationaryRestart.route({{1024, 900}}, 0);
    stationaryRestart.advance();
    stationaryRestart.blocked({1024 * 256, 1024 * 256}, -78, 0);
    for (int i = 0; i < 20 && !stationaryRestart.replanning(); ++i)
      stationaryRestart.advance();
    stationaryRestart.route({stationaryRestart.position()}, 0);
    check(stationaryRestart.stopped() && stationaryRestart.facing() == -78 &&
              stationaryRestart.travelHeading() == -56,
          "Stopped restart fixture retains a distinct accepted direction");
    auto ordinaryRestart = stationaryRestart;
    ordinaryRestart.route({{1056, 1024}}, 0);
    ordinaryRestart.advance();
    check(ordinaryRestart.travelHeading() == -36,
          "An ordinary restart retains its accepted direction");
    stationaryRestart.route({{1056, 1024}}, 1, true);
    stationaryRestart.advance();
    check(stationaryRestart.travelHeading() == -56,
          "Stopped route preparation preserves its previous direction");
    stationaryRestart.advance();
    check(
        stationaryRestart.facing() == -118 &&
            stationaryRestart.travelHeading() == -96 &&
            samePoint(stationaryRestart.velocity(), {-48, 47}) &&
            stationaryRestart.speed() == 67,
        "A failed trip resumes from its accepted direction after preparation");
    stationaryRestart.blocked({1024 * 256, 1024 * 256}, -64, 0);
    for (int i = 0; i < 20 && !stationaryRestart.replanning(); ++i)
      stationaryRestart.advance();
    check(stationaryRestart.replanning(),
          "Blocked resting restart reaches its next preparation phase");
    stationaryRestart.advance();
    stationaryRestart.advance();
    check(
        stationaryRestart.travelHeading() == -96 &&
            samePoint(stationaryRestart.velocity(), {-48, 47}),
        "Repeated collision recovery retains the restored accepted direction");
    Motion slideToRest({1024, 1024}, -128, physics);
    slideToRest.route({{1024, 1800}}, 0);
    for (int i = 0; i < 20; ++i)
      slideToRest.advance();
    auto slideToRestRaw = slideToRest.rawPosition();
    slideToRest.slideBlocked(slideToRestRaw, -128, -128, 1280);
    slideToRest.advance();
    slideToRest.finishSlide(slideToRest.rawPosition(), false);
    for (int i = 0; i < 10; ++i) {
      slideToRest.advance();
      slideToRest.finishSlide(slideToRest.rawPosition(), false);
    }
    auto slideToRestPosition = slideToRest.rawPosition();
    auto blockedTangent = slideToRest;
    blockedTangent.blockedSlide(slideToRestPosition);
    check(blockedTangent.replanning() && blockedTangent.speed() == 1280 &&
              blockedTangent.desiredHeading() == -128 &&
              samePoint(blockedTangent.rawPosition(), slideToRestPosition),
          "A blocked tangent retains its speed and accepted position for this "
          "frame");
    blockedTangent.replan(
        {{blockedTangent.position().x, blockedTangent.position().y - 64}});
    blockedTangent.advance();
    check(
        blockedTangent.speed() == 0 && blockedTangent.nextSpeed() == 0 &&
            !blockedTangent.updatesHeading(),
        "The next blocked-tangent frame prepares the new route at zero speed");
    blockedTangent.advance();
    check(blockedTangent.speed() == 67 && blockedTangent.facing() == 88 &&
              blockedTangent.travelHeading() == 108 &&
              samePoint(blockedTangent.velocity(), {31, 59}) &&
              samePoint(blockedTangent.rawPosition(), slideToRestPosition),
          "Tangent recovery turns from the last accepted sliding direction");
    auto slideToRestDesired = slideToRest.desiredHeading();
    slideToRest.route({slideToRest.position()}, 0);
    slideToRest.advance();
    check(slideToRest.speed() == 1280 && slideToRest.moving() &&
              slideToRest.desiredHeading() == slideToRestDesired &&
              samePoint(slideToRest.rawPosition(), slideToRestPosition),
          "A slide retargeted to its current point preserves one preparation "
          "frame");
    slideToRest.advance();
    check(slideToRest.speed() == 1213 && !slideToRest.moving(),
          "The frame following slide preparation begins stationary braking");
    Motion contacted({1024, 1024}, -32, physics);
    contacted.route({{900, 1024}}, 0);
    contacted.advance();
    Point contactResumeOrigin{1024 * 256 + 225, 1024 * 256 + 41};
    contacted.contactBlocked(contactResumeOrigin, -57, {900, 1024});
    int attemptedContactTravel = contacted.travelHeading();
    contacted.route({{1021, 1033}, {1021, 1097}}, 0);
    contacted.advance();
    check(samePoint(contacted.rawPosition(), contactResumeOrigin) &&
              contacted.speed() == 0 &&
              contacted.travelHeading() == attemptedContactTravel,
          "Retargeting contact retains its zero-speed preparation frame");
    contacted.advance();
    check(contacted.speed() == 67 && contacted.travelHeading() == -52 &&
              contacted.desiredHeading() == -126 &&
              contacted.waypoint().y == 1097 &&
              samePoint(contacted.rawPosition(), {contactResumeOrigin.x - 65,
                                                  contactResumeOrigin.y - 20}),
          "Contact recovery resumes from the accepted direction before "
          "skipping a nearby waypoint");
    contacted.advance();
    check(contacted.speed() == 134 && contacted.travelHeading() == -72 &&
              samePoint(contacted.rawPosition(), {contactResumeOrigin.x - 197,
                                                  contactResumeOrigin.y + 6}),
          "The second resumed contact step preserves the observed acceleration "
          "and turn");
    Motion closedGoal({1024, 1024}, 64, physics);
    closedGoal.route({{1400, 1024}}, 0);
    for (int i = 0; i < 8; ++i)
      closedGoal.advance();
    closedGoal.blocked(closedGoal.rawPosition(), 32, closedGoal.nextSpeed());
    closedGoal.replan({{1024, 1200}});
    closedGoal.advance();
    closedGoal.route({closedGoal.position()}, 0);
    closedGoal.advance();
    check(closedGoal.stopped() && !closedGoal.moving() &&
              closedGoal.travelHeading() == 32 &&
              closedGoal.desiredHeading() == 32,
          "A collapsed zero-speed route clears movement and faces its resting "
          "direction");
    closedGoal.route({{closedGoal.position().x + 100, closedGoal.position().y}},
                     0, true);
    closedGoal.advance();
    check(closedGoal.travelHeading() == 52,
          "Normalized rest cannot restore an older collision direction on "
          "restart");
    Motion pendingTurn({1024, 1024}, 64, physics);
    pendingTurn.route({{1400, 1024}}, 0);
    for (int i = 0; i < 8; ++i)
      pendingTurn.advance();
    pendingTurn.blocked(pendingTurn.rawPosition(), pendingTurn.facing(),
                        pendingTurn.nextSpeed());
    pendingTurn.replan({{1024, 1200}});
    pendingTurn.advance();
    pendingTurn.advance();
    check(pendingTurn.speed() == 67 && pendingTurn.nextSpeed() == 0,
          "Collision turning prepares an uncommitted acceleration");
    auto replacedTurn = pendingTurn;
    pendingTurn.route({{1400, 1200}}, 0);
    pendingTurn.advance();
    check(pendingTurn.speed() == 67, "A retarget uses accepted speed rather "
                                     "than accelerating a prepared turn twice");
    replacedTurn.route({{1300, 1100}}, 0);
    replacedTurn.route({{1400, 1200}}, 0);
    replacedTurn.advance();
    check(replacedTurn.speed() == 67 &&
              samePoint(replacedTurn.rawPosition(), pendingTurn.rawPosition()),
          "Replacing a route twice in one frame cannot commit a trial "
          "acceleration");
    Motion shortSlide({1024, 1024}, -38, physics);
    Point shortSlideOrigin{1024 * 256 + 40, 1024 * 256 + 179};
    shortSlide.clipStep(shortSlideOrigin);
    shortSlide.route({{1024, 1025}}, 0);
    shortSlide.advance();
    shortSlide.slideBlocked(shortSlideOrigin, 0, 0, 0);
    shortSlide.advance();
    check(samePoint(shortSlide.rawPosition(), {1024 * 256, 1025 * 256}) &&
              shortSlide.speed() == 134 &&
              samePoint(shortSlide.velocity(), {-58, -121}),
          "A short tangent step clips to its waypoint while retaining the "
          "computed velocity");
    shortSlide.finishSlide(shortSlide.rawPosition(), true);
    shortSlide.advance();
    check(shortSlide.speed() == 0 && !shortSlide.moving() &&
              shortSlide.travelHeading() == 0,
          "A clipped tangent arrival settles on its following frame");
    Motion arrivalTurn({1024, 1024}, 0, physics);
    arrivalTurn.route({{1024, 900}}, 0);
    for (int i = 0; i < 3; ++i)
      arrivalTurn.advance();
    Point arrivalTurnOrigin{1024 * 256 + 246, 1024 * 256 + 216};
    arrivalTurn.clipStep(arrivalTurnOrigin);
    arrivalTurn.route({{1024, 1024}}, 0);
    arrivalTurn.hold({1068, 1052});
    arrivalTurn.advance();
    check(arrivalTurn.speed() == 134 &&
              samePoint(arrivalTurn.velocity(), {63, -119}) &&
              samePoint(arrivalTurn.rawPosition(), arrivalTurnOrigin),
          "A fractional arrival brakes while still turning toward its "
          "interaction");
    Motion lateArrival({1024, 1024}, -64, physics);
    lateArrival.route({{900, 1024}}, 0);
    for (int i = 0; i < 4; ++i)
      lateArrival.advance();
    Point lateArrivalOrigin{1024 * 256 + 192, 1024 * 256 + 247};
    lateArrival.clipStep(lateArrivalOrigin);
    lateArrival.route({{1024, 1024}}, 0);
    lateArrival.advance();
    check(lateArrival.speed() == 201,
          "Arrival commits its first braking frame");
    auto alignedArrival = lateArrival;
    alignedArrival.hold({900, 1024});
    alignedArrival.advance();
    check(alignedArrival.speed() == 0,
          "An aligned resting interaction needs no further velocity update");
    lateArrival.hold({981, 1020});
    lateArrival.advance();
    check(
        lateArrival.speed() == 268 &&
            samePoint(lateArrival.velocity(), {-267, -20}) &&
            samePoint(lateArrival.rawPosition(), lateArrivalOrigin),
        "A late interaction turn reevaluates fractional arrival acceleration");
    lateArrival.advance();
    check(lateArrival.speed() == 0,
          "The aligned arrival settles after its turn");
    Motion fractionalBrake({1024, 1024}, -64, physics);
    fractionalBrake.route({{900, 1024}}, 0);
    for (int i = 0; i < 4; ++i)
      fractionalBrake.advance();
    fractionalBrake.clipStep(lateArrivalOrigin);
    fractionalBrake.brakeTo({1024, 1024});
    for (int expected : {201, 268, 201, 268}) {
      fractionalBrake.advance();
      check(fractionalBrake.speed() == expected &&
                samePoint(fractionalBrake.rawPosition(), lateArrivalOrigin),
            "A fractional Follow stop retains residual velocity without "
            "displacing its accepted position");
    }
    Motion steeringBrake({1024, 1024}, 0, physics);
    steeringBrake.route({{2048, 1024}}, 0, false, false);
    steeringBrake.advance();
    check(steeringBrake.facing() == 40 && steeringBrake.travelHeading() == 20,
          "A turn can remain in progress when Follow requests braking");
    steeringBrake.brakeTo({2048, 1024});
    steeringBrake.advance();
    check(steeringBrake.facing() == 64,
          "Periodic Follow braking continues an existing facing update");
    Motion straightBrake({1024, 1024}, 0, physics);
    straightBrake.route({{1024, 32}}, 0, false, false);
    straightBrake.advance();
    straightBrake.brakeTo({2048, 1024});
    straightBrake.advance();
    check(straightBrake.facing() == 0,
          "Periodic Follow braking does not start a new facing update");
    Motion finishing({1024, 1024}, 64, physics);
    finishing.route({{1800, 1024}}, 0);
    for (int i = 0; i < 20; ++i)
      finishing.advance();
    auto finishAt = finishing.position();
    finishing.route({finishAt}, 0);
    finishing.advance();
    check(finishing.settling() && finishing.speed() > 192,
          "An already reached destination enters the final braking frame");
    finishing.route({finishAt}, 0);
    finishing.advance();
    check(!finishing.speed() && samePoint(finishing.position(), finishAt),
          "Retargeting the current point preserves the pending full stop");
    Motion arrivalHold({1024, 1024}, 64, physics);
    arrivalHold.route({{1800, 1024}}, 0);
    for (int i = 0; i < 20; ++i)
      arrivalHold.advance();
    arrivalHold.route({arrivalHold.position()}, 0);
    int arrivalSpeed = arrivalHold.speed();
    arrivalHold.hold({arrivalHold.position().x + 40, arrivalHold.position().y});
    arrivalHold.advance();
    check(arrivalHold.speed() == arrivalSpeed - 67,
          "An aligned arrival retains one braking frame before holding");
    arrivalHold.advance();
    check(!arrivalHold.speed(),
          "The retained arrival frame ends in a full stop");
    Motion interruptedPause({1024, 1024}, -95, physics);
    interruptedPause.route({{1024, 800}}, 0);
    auto pauseOrigin = interruptedPause.rawPosition();
    interruptedPause.advance();
    interruptedPause.blocked(pauseOrigin, -95);
    interruptedPause.route({{1024, 600}}, 0);
    interruptedPause.advance();
    check(samePoint(interruptedPause.rawPosition(), pauseOrigin) &&
              interruptedPause.speed() == 67 && !interruptedPause.nextSpeed() &&
              interruptedPause.travelHeading() == -75,
          "A target changed during collision pause retains its attempted "
          "velocity for one recovery frame");
    interruptedPause.advance();
    check(interruptedPause.speed() == 67 && !interruptedPause.nextSpeed() &&
              interruptedPause.travelHeading() == -75 &&
              interruptedPause.facing() == -55,
          "The changed collision target turns from accepted travel inertia");
    Motion interruptedSlide({1024, 1024}, -64, physics);
    interruptedSlide.route({{100, 1024}}, 0);
    for (int i = 0; i < 6; ++i)
      interruptedSlide.advance();
    auto slideRetargetOrigin = interruptedSlide.rawPosition();
    interruptedSlide.advance();
    interruptedSlide.slideBlocked(slideRetargetOrigin, -64, -128, 402);
    interruptedSlide.route({{1800, 1024}}, 0);
    interruptedSlide.advance();
    check(
        samePoint(interruptedSlide.rawPosition(), slideRetargetOrigin) &&
            interruptedSlide.speed() == 469 &&
            interruptedSlide.nextSpeed() == 402,
        "Retargeting a pending slide preserves speed while entering recovery");
    interruptedSlide.advance();
    check(samePoint(interruptedSlide.rawPosition(), slideRetargetOrigin) &&
              interruptedSlide.speed() == 469 &&
              interruptedSlide.travelHeading() == -84,
          "A retargeted slide turns without prematurely moving");
    Motion interruptedRetry({1024, 1024}, 0, physics);
    interruptedRetry.route({{1024, 1400}}, 0);
    auto retryOrigin = interruptedRetry.rawPosition();
    interruptedRetry.advance();
    interruptedRetry.blocked(retryOrigin, 0, 0);
    for (int i = 0; i < 10 && !interruptedRetry.replanning(); ++i)
      interruptedRetry.advance();
    check(interruptedRetry.replanning(),
          "Blocked motion reaches route recovery");
    interruptedRetry.replan({{1024, 1400}});
    interruptedRetry.advance();
    interruptedRetry.route({{800, 1024}}, 0);
    interruptedRetry.advance();
    check(
        interruptedRetry.travelHeading() == -20 &&
            interruptedRetry.facing() == -40 &&
            interruptedRetry.velocity().x < 0 &&
            !samePoint(interruptedRetry.rawPosition(), retryOrigin),
        "A destination changed during recovery resumes from the last accepted "
        "travel direction and can move immediately");
    Motion fractional({1024, 1024}, 64, physics);
    fractional.route({{1800, 1024}}, 0);
    for (int i = 0; i < 3; ++i)
      fractional.advance();
    fractional.clipStep({1024 * 256 + 240, 1024 * 256 + 240});
    fractional.route({fractional.position()}, 0);
    auto heldFractional = fractional;
    heldFractional.hold({1100, 1024});
    fractional.advance();
    heldFractional.advance();
    check(
        fractional.speed() == 268 && heldFractional.speed() == 268,
        "A fractional arrival can consume one acceleration update before rest");
    {
      auto restingTurn = fractional;
      const auto origin = restingTurn.rawPosition();
      check(!restingTurn.moving() && restingTurn.speed() > 0,
            "The resting turn starts after logical arrival with residual speed");
      restingTurn.brakeTo(restingTurn.stopTarget());
      restingTurn.faceOnArrival(Point{1100, 1050});
      restingTurn.advance();
      const int wanted = heading({1100 - restingTurn.position().x,
                                  1050 - restingTurn.position().y});
      check(restingTurn.facing() == wanted &&
                restingTurn.travelHeading() == wanted && restingTurn.speed() > 0 &&
                samePoint(restingTurn.rawPosition(), origin),
            "A stationary braking turn consumes its residual velocity update");
      auto restingCopy = restingTurn;
      restingTurn.advance();
      restingCopy.advance();
      check(!restingTurn.speed() && !restingTurn.nextSpeed() &&
                samePoint(restingTurn.velocity(), {}) &&
                samePoint(restingTurn.rawPosition(), origin) &&
                restingCopy.speed() == restingTurn.speed(),
            "An aligned stationary braking turn clears inertia on the next update");
    }
    fractional.advance();
    heldFractional.advance();
    check(!fractional.speed() && !heldFractional.speed(),
          "Both ordinary and held fractional arrivals stop on the next frame");
    Motion brakingSlide({1024, 1024}, 64, physics);
    brakingSlide.route({{1800, 1024}}, 0);
    for (int i = 0; i < 4; ++i)
      brakingSlide.advance();
    auto nearSlide = brakingSlide.position();
    brakingSlide.route({{nearSlide.x - 1, nearSlide.y}}, 0);
    brakingSlide.slideBlocked(brakingSlide.rawPosition(), 64, 0, 201);
    brakingSlide.advance();
    check(brakingSlide.speed() == 189,
          "A tangent slide brakes when its waypoint lies inside the stopping "
          "distance");
    auto cappedPhysics = physics;
    cappedPhysics.speed = 945;
    cappedPhysics.haltDistance = 6664;
    Motion fastBrakingSlide({1024, 1024}, -41, cappedPhysics);
    auto approachDirection = directionVector(-41);
    fastBrakingSlide.route(
        {{1024 + 4 * approachDirection.x, 1024 + 4 * approachDirection.y}}, 0);
    for (int i = 0; i < 15; ++i)
      fastBrakingSlide.advance();
    auto fastContact = fastBrakingSlide.position();
    fastBrakingSlide.route({{fastContact.x, fastContact.y + 9}}, 0);
    fastBrakingSlide.slideBlocked(fastBrakingSlide.rawPosition(), -41, -128,
                                  945);
    fastBrakingSlide.advance();
    check(fastBrakingSlide.speed() == 405,
          "Slide-entry acceleration accounts for the remaining turn travel");
    Motion agedSlide({1024, 1024}, 64, physics);
    agedSlide.route({{7900, 1024}}, 0);
    for (int i = 0; i < 150; ++i)
      agedSlide.advance();
    auto agedOrigin = agedSlide.rawPosition();
    agedSlide.advance();
    agedSlide.slideBlocked(agedOrigin, 64, 0, 1280);
    agedSlide.advance();
    agedSlide.finishSlide(agedSlide.rawPosition(), true);
    check(agedSlide.replanning(),
          "An accepted slide retries an aged local path");
    int acceptedSlideSpeed = agedSlide.nextSpeed();
    agedSlide.replan({{7900, 1024}});
    agedSlide.advance();
    agedSlide.advance();
    check(agedSlide.nextSpeed() == acceptedSlideSpeed &&
              agedSlide.speed() == std::min(1280, acceptedSlideSpeed + 67),
          "Route recovery retains accepted slide inertia while turning");
    Motion lateCollision({1024, 1024}, 64, physics);
    lateCollision.route({{1800, 1024}}, 0);
    for (int i = 0; i < 20; ++i)
      lateCollision.advance();
    auto lateOrigin = lateCollision.rawPosition();
    lateCollision.advance();
    lateCollision.blocked(lateOrigin, 64, 1280);
    check(lateCollision.replanning() && lateCollision.moving() &&
              !lateCollision.speed() && !lateCollision.nextSpeed() &&
              !lateCollision.updatesHeading(),
          "An established route retries immediately while preserving walking "
          "state");
    Motion contact = fastSlide;
    auto contactOrigin = contact.rawPosition();
    contact.contactBlocked(contactOrigin, contact.facing(),
                           {contact.position().x + 20, contact.position().y});
    check(
        !contact.speed() && !contact.updatesHeading() && !contact.settling(),
        "Contact arrival preserves the current animation until the next frame");
    contact.advance();
    check(samePoint(contact.rawPosition(), contactOrigin) &&
              !contact.moving() && contact.settling(),
          "Contact arrival turns toward the interaction without moving");
    Motion pendingContact({1256, 1080}, 64, physics);
    pendingContact.route({{1256, 1137}}, 0);
    pendingContact.advance();
    auto pendingOrigin = pendingContact.rawPosition();
    auto pendingFacing = pendingContact.facing();
    pendingContact.advance();
    pendingContact.contactBlocked(pendingOrigin, pendingFacing,
                                  pendingContact.waypoint());
    auto acceptedContact = pendingContact;
    int attemptedHeading = acceptedContact.travelHeading();
    acceptedContact.hold({1296, 1108});
    check(acceptedContact.travelHeading() == attemptedHeading,
          "An order accepted on the contact frame defers orientation until the "
          "next movement frame");
    pendingContact.advance();
    check(pendingContact.travelHeading() == 104 &&
              pendingContact.facing() == -128 &&
              samePoint(pendingContact.rawPosition(), pendingOrigin),
          "Contact braking turns from the last accepted travel direction while "
          "an order is pending");
    Motion alignedStop({300, 300}, 64, physics);
    alignedStop.faceOnArrival(Point{400, 300});
    alignedStop.settle();
    auto turningStop = alignedStop;
    turningStop.faceOnArrival(Point{300, 200});
    alignedStop.advance();
    check(alignedStop.stopped() && !alignedStop.updatesHeading(),
          "An aligned interaction stop does not republish its heading");
    turningStop.advance();
    check(turningStop.updatesHeading(),
          "An interaction stop still publishes a changed heading");
    dat::Flingy recoveryPhysics{0, 1280, 12227, 67, 40, 0};
    Motion recovery({300, 300}, 64, recoveryPhysics);
    recovery.route({{316, 300}}, 0);
    for (int n = 0; n < 100 && !recovery.arrived(); ++n)
      recovery.advance();
    check(recovery.arrived() && recovery.speed() > 0,
          "Recovery arrival retains residual speed");
    int residual = recovery.speed();
    auto interaction = recovery;
    interaction.hold({400, 300});
    interaction.advance();
    check(interaction.speed() == 0,
          "An interaction hold still brakes the final residual speed");
    auto parked = recovery.position();
    for (int n = 0; n < 10; ++n) {
      recovery.waitForRecovery({400, 300});
      recovery.advance();
      check(recovery.speed() == residual &&
                samePoint(recovery.position(), parked),
            "An aligned recovery wait retains speed without displacement");
    }
    auto clearedRecovery = recovery;
    auto blockedRecovery = recovery;
    auto clearedRecoveryFacing = recovery.facing();
    auto clearedRecoveryTravel = recovery.travelHeading();
    clearedRecovery.waitForRecovery(parked);
    clearedRecovery.finishRecoveryWait();
    clearedRecovery.advance();
    blockedRecovery.waitForRecovery(parked);
    blockedRecovery.advance();
    check(clearedRecovery.speed() == 0 && clearedRecovery.nextSpeed() == 0 &&
              !clearedRecovery.moving() &&
              samePoint(clearedRecovery.velocity(), {}) &&
              samePoint(clearedRecovery.position(), parked) &&
              clearedRecovery.facing() == clearedRecoveryFacing &&
              clearedRecovery.travelHeading() == clearedRecoveryTravel &&
              blockedRecovery.speed() == residual,
          "A cleared goal releases recovery speed without altering its copied blocked state");
    recovery.route({{400, 300}}, 0);
    recovery.advance();
    check(recovery.speed() > residual, "Recovery resumes from retained speed");
    Motion passThrough({300, 300}, 64, recoveryPhysics);
    passThrough.route({{336, 301}}, 0, false, false);
    auto sameDestination = passThrough;
    sameDestination.setMovementTarget(Point{336, 301});
    auto separateDestination = passThrough;
    separateDestination.setMovementTarget(Point{500, 280});
    bool slowedForEscape = false;
    for (int n = 0; n < 14; ++n) {
      passThrough.advance();
      sameDestination.advance();
      separateDestination.advance();
      check(
          samePoint(passThrough.rawPosition(), sameDestination.rawPosition()) &&
              passThrough.speed() == sameDestination.speed(),
          "The unchanged movement target preserves pass-through travel");
      slowedForEscape |= separateDestination.speed() < passThrough.speed();
    }
    check(slowedForEscape,
          "A separate logical destination brakes at the escape waypoint");
    auto scriptTurnPhysics = recoveryPhysics;
    scriptTurnPhysics.control = 2;
    scriptTurnPhysics.turnRate = 27;
    {
      Motion separateTurn({1000, 1000}, -32, scriptTurnPhysics);
      separateTurn.route({{901, 900}}, 0);
      separateTurn.scriptSpeed(0, true);
      separateTurn.advance();
      separateTurn.advance();
      const auto origin = separateTurn.rawPosition();
      separateTurn.setRouteAge(8);
      separateTurn.scriptSpeed(512, true);
      separateTurn.blockedWaypoint(origin, -33, 512, true);
      check(separateTurn.facing() == -33 && separateTurn.travelHeading() == -32,
            "A rejected step retains distinct body and accepted travel angles");
      separateTurn.replan({{1000, 900}});
      auto snapshot = separateTurn;
      separateTurn.advance();
      snapshot.advance();
      separateTurn.scriptSpeed(512, true);
      snapshot.scriptSpeed(512, true);
      check(separateTurn.facing() == -6 && separateTurn.travelHeading() == -5 &&
                samePoint(separateTurn.rawPosition(), origin) &&
                samePoint(separateTurn.velocity(), {-62, -508}) &&
                snapshot.travelHeading() == separateTurn.travelHeading(),
            "A scripted recovery turns body and accepted travel independently");
    }
    {
      Motion blockedTurn({1000, 1000}, -32, scriptTurnPhysics);
      blockedTurn.route({{901, 900}}, 0);
      blockedTurn.scriptSpeed(0, true);
      blockedTurn.advance();
      blockedTurn.advance();
      const auto origin = blockedTurn.rawPosition();
      blockedTurn.setRouteAge(8);
      blockedTurn.scriptSpeed(512, true);
      blockedTurn.blockedWaypoint(origin, -33, 512, true);
      blockedTurn.replan({{900, 1000}});
      blockedTurn.advance();
      check(blockedTurn.facing() == -60 && blockedTurn.travelHeading() == -59 &&
                blockedTurn.desiredHeading() == -64,
            "A blocked recovery enters its wait with unfinished body steering");
      blockedTurn.waitForBody(origin, -33, 512);
      auto untouched = blockedTurn;
      for (int retry = 0; retry < 2; ++retry) {
        blockedTurn.advance();
        auto trial = blockedTurn;
        trial.resumeBodyWait();
        trial.advance();
        blockedTurn.acceptBlockedWaitTurn(trial);
        check(blockedTurn.facing() == (retry == 0 ? -60 : -64) &&
                  blockedTurn.travelHeading() == (retry == 0 ? -59 : -64) &&
                  samePoint(blockedTurn.rawPosition(), origin) &&
                  blockedTurn.speed() == 0 && blockedTurn.updatesHeading() &&
                  blockedTurn.turningBodyWait(),
              "A blocked scripted body keeps turning before travel realigns");
      }
      check(untouched.facing() == -33 && untouched.travelHeading() == -59,
            "Blocked steering preserves the copied waiting state");
    }
    {
      Motion finishingTurn({1000, 1000}, 64, scriptTurnPhysics);
      finishingTurn.route({{1005, 1000}, {1025, 951}}, 0, false, false);
      finishingTurn.scriptSpeed(1280, true);
      finishingTurn.advance();
      finishingTurn.scriptSpeed(512, true);
      finishingTurn.advance();
      check(finishingTurn.facing() == 37 && finishingTurn.travelHeading() == 37,
            "The next route segment starts a body turn during walking");
      const auto origin = finishingTurn.rawPosition();
      finishingTurn.advance();
      check(finishingTurn.facing() == 16 && finishingTurn.travelHeading() == 16,
            "A rejected preview can finish a previously pending body turn");
      finishingTurn.setRouteAge(20);
      finishingTurn.waitForBody(origin, 37, 512);
      finishingTurn.advance();
      auto trial = finishingTurn;
      trial.resumeBodyWait();
      trial.advance();
      finishingTurn.acceptBlockedWaitTurn(trial);
      check(finishingTurn.facing() == 16 && finishingTurn.travelHeading() == 16 &&
                finishingTurn.turningBodyWait() &&
                samePoint(finishingTurn.rawPosition(), origin),
            "A rejected completed turn retains its body-wait animation transition");
    }
    {
      dat::Flingy scriptedPhysics{0, 1280, 12227, 1, 27, 2};
      Motion stationaryTurn({1000, 1000}, 0, scriptedPhysics);
      stationaryTurn.route({{1000, 900}}, 0);
      stationaryTurn.scriptSpeed(0, true);
      stationaryTurn.advance();
      stationaryTurn.advance();
      const auto origin = stationaryTurn.rawPosition();
      stationaryTurn.waitForBody(origin, 0, 0);
      stationaryTurn.expireBodyWait(true);
      stationaryTurn.replan({{1100, 900}});
      stationaryTurn.advance();
      check(stationaryTurn.facing() == 27 && stationaryTurn.travelHeading() == 27 &&
                stationaryTurn.speed() == 0 && stationaryTurn.nextSpeed() == 0 &&
                samePoint(stationaryTurn.rawPosition(), origin) &&
                samePoint(stationaryTurn.velocity(), {}),
            "Script-controlled recovery steering does not create physical acceleration");
    }
    {
      dat::Flingy scriptedPhysics{0, 1280, 12227, 1, 27, 2};
      Motion pendingStop({1000, 1000}, -33, scriptedPhysics);
      pendingStop.route({{900, 900}}, 0);
      bool separated = false;
      for (int frame = 0; frame < 32; ++frame) {
        pendingStop.scriptSpeed(512, true);
        pendingStop.advance();
        if (pendingStop.facing() != pendingStop.travelHeading()) {
          separated = true;
          break;
        }
      }
      check(separated && pendingStop.facing() == -33 &&
                pendingStop.travelHeading() == -32 && pendingStop.speed() == 0 &&
                pendingStop.moving(),
            "Script walking can retain distinct body and accepted travel before stopping");
      const auto origin = pendingStop.rawPosition();
      const auto snapshot = pendingStop;
      pendingStop.brakeTo(pendingStop.position());
      pendingStop.scriptSpeed(2304, true);
      check(pendingStop.facing() == -33 && pendingStop.travelHeading() == -32 &&
                pendingStop.needsArrivalUpdate() && pendingStop.moving() &&
                samePoint(pendingStop.rawPosition(), origin) &&
                samePoint(pendingStop.velocity(), {-1629, -1629}) &&
                snapshot.travelHeading() == -32,
            "A pending script stop preserves accepted travel through the zero-speed animation gap");
    }
    {
      dat::Flingy scriptedPhysics{0, 1280, 12227, 1, 27, 2};
      const Point admittedDelta{-27, -11}, refreshedDelta{-26, -11};
      const int admittedHeading = heading(admittedDelta);
      const int refreshedHeading = heading(refreshedDelta);
      Motion restingRefresh({1000, 1000}, admittedHeading, scriptedPhysics);
      restingRefresh.route({{740, 900}}, 0);
      restingRefresh.scriptSpeed(512, true);
      restingRefresh.advance();
      restingRefresh.scriptSpeed(0, true);
      restingRefresh.brakeTo(restingRefresh.position());
      const auto at = restingRefresh.position(), raw = restingRefresh.rawPosition();
      const Point admitted{at.x + admittedDelta.x, at.y + admittedDelta.y};
      const Point refreshed{at.x + refreshedDelta.x, at.y + refreshedDelta.y};
      restingRefresh.faceOnArrival(admitted, true);
      restingRefresh.faceOnArrival(refreshed);
      restingRefresh.advance();
      check(restingRefresh.facing() == admittedHeading &&
                !restingRefresh.moving() && !restingRefresh.speed(),
            "A scripted arrival honors its admitted command-facing direction");
      restingRefresh.faceOnArrival(refreshed);
      restingRefresh.advance();
      check(restingRefresh.facing() == refreshedHeading &&
                restingRefresh.travelHeading() == refreshedHeading &&
                !restingRefresh.speed() && samePoint(restingRefresh.rawPosition(), raw),
            "The next resting frame refreshes the scripted arrival direction");
    }
    Motion scriptFollower({1024, 1024}, 0, scriptTurnPhysics);
    scriptFollower.scriptSpeed(512, true);
    for (int direction : {0, 64, -64, -128})
      check(samePoint(scriptFollower.projectFollowTarget({3691, 3330}, direction),
                      {3691, 3330}),
            "Script-driven follow keeps the target without a braking-distance lead");
    Motion scriptTurn({353, 244}, 19, scriptTurnPhysics);
    scriptTurn.scriptSpeed(1280);
    scriptTurn.hold({337, 276});
    auto scriptTurnCopy = scriptTurn;
    scriptTurn.advance();
    check(scriptTurn.facing() == -8 && scriptTurn.travelHeading() == -8 &&
              scriptTurn.speed() == 0 &&
              samePoint(scriptTurn.position(), {353, 244}) &&
              scriptTurnCopy.facing() == 19 &&
              scriptTurnCopy.travelHeading() == 19,
          "Animation-driven recovery turns body and travel at the same rate");
    Motion inheritedDeparture({279, 274}, 80, scriptTurnPhysics);
    inheritedDeparture.inheritInertia({512, 109, {230, 458}});
    inheritedDeparture.route({{275, 277}}, 0);
    inheritedDeparture.advance();
    check(samePoint(inheritedDeparture.rawPosition(), {71676, 70590}) &&
              inheritedDeparture.facing() == 107 &&
              inheritedDeparture.travelHeading() == 107,
          "An initial script-driven turn retains inherited departure travel");
    auto departureRaw = inheritedDeparture.rawPosition();
    inheritedDeparture.scriptSpeed(2048);
    inheritedDeparture.advance();
    check(samePoint(inheritedDeparture.rawPosition(), departureRaw) &&
              inheritedDeparture.facing() == -122,
          "An ongoing script-driven turn suppresses its next travel step");
    {
      Motion acquiredWhileTurning({1024, 1024}, 0, scriptTurnPhysics);
      acquiredWhileTurning.route({{800, 1024}}, 0);
      acquiredWhileTurning.scriptSpeed(512);
      acquiredWhileTurning.advance();
      acquiredWhileTurning.advance();
      const auto origin = acquiredWhileTurning.rawPosition();
      const int wanted = acquiredWhileTurning.desiredHeading();
      const int facing = acquiredWhileTurning.facing();
      acquiredWhileTurning.scriptSpeed(0);
      acquiredWhileTurning.brakeTo(acquiredWhileTurning.position());
      check(acquiredWhileTurning.moving() &&
                acquiredWhileTurning.needsArrivalUpdate() &&
                acquiredWhileTurning.desiredHeading() == wanted,
            "A zero-speed script write does not finish an admitted walking stop");
      auto facingCommand = acquiredWhileTurning;
      auto commandPoint = facingCommand.position();
      commandPoint.y -= 100;
      facingCommand.faceOnArrival(commandPoint, true);
      facingCommand.faceOnArrival(
          Point{commandPoint.x - 45, commandPoint.y + 182});
      facingCommand.scriptSpeed(512, true);
      facingCommand.advance();
      check(facingCommand.facing() == facing + 27 &&
                !facingCommand.speed() &&
                samePoint(facingCommand.rawPosition(), origin),
            "Attack acquisition preserves the admitted command-facing stop");
      auto target = acquiredWhileTurning.position();
      target.x -= 45;
      target.y += 82;
      acquiredWhileTurning.faceOnArrival(target);
      acquiredWhileTurning.scriptSpeed(512, true);
      acquiredWhileTurning.advance();
      check(!acquiredWhileTurning.speed() &&
                samePoint(acquiredWhileTurning.rawPosition(), origin) &&
                acquiredWhileTurning.facing() == facing - 27,
            "A pending walking stop completes its target-facing turn before departure");
    }
    Motion immediateBodyWait({300, 300}, 64, scriptTurnPhysics);
    immediateBodyWait.route({{500, 300}}, 0);
    immediateBodyWait.advance();
    immediateBodyWait.scriptSpeed(2048);
    auto immediateOrigin = immediateBodyWait.rawPosition();
    immediateBodyWait.advance();
    immediateBodyWait.setRouteAge(2);
    immediateBodyWait.waitForBody(immediateOrigin, 64, 2048);
    check(immediateBodyWait.waitingForBody() &&
              !immediateBodyWait.collisionPaused(),
          "A young route can retry a moving body without a planning pause");
    auto scriptPlanningWait = immediateBodyWait;
    scriptPlanningWait.prepareBodyWait(immediateOrigin, 64, 2048);
    check(scriptPlanningWait.collisionPaused() && scriptPlanningWait.moving(),
          "A scripted planning wait retains its initial walk state");
    int scriptPlanningFrames = 0;
    while (scriptPlanningWait.collisionPaused() && scriptPlanningFrames < 10) {
      scriptPlanningWait.advance();
      ++scriptPlanningFrames;
    }
    check(scriptPlanningFrames == 5 && scriptPlanningWait.waitingForBody(),
          "Scripted planning waits consume the complete young-route delay");
    auto earlyClearance = immediateBodyWait;
    earlyClearance.prepareBodyWait(immediateOrigin, 64, 2048);
    check(earlyClearance.preparedBodyRetryReady(),
          "The first body-wait tick can test whether the obstruction has cleared");
    earlyClearance.advance();
    auto earlyRetry = earlyClearance;
    check(earlyRetry.preparedBodyRetryReady(),
          "A prepared script body wait can test clearance before its route delay ends");
    earlyRetry.retryPreparedBodyWait();
    earlyRetry.advance();
    check(earlyRetry.collisionDebt() == earlyClearance.collisionDebt() + 1,
          "A speculative body retry counts its waiting frame exactly once");
    check(earlyClearance.collisionPaused() && !earlyRetry.collisionPaused() &&
              earlyRetry.rawPosition().x == immediateOrigin.x + 2048 &&
              earlyRetry.rawPosition().y == immediateOrigin.y,
          "A successful copied clearance retry restores collision travel without consuming the pending wait");
    Motion fineWait({3745, 3761}, 4, scriptTurnPhysics);
    fineWait.route({{3751, 3697}}, 0);
    fineWait.advance();
    fineWait.scriptSpeed(512, true);
    fineWait.advance();
    fineWait.scriptSpeed(2048, true);
    fineWait.advance();
    const auto fineOrigin = fineWait.rawPosition();
    fineWait.scriptSpeed(1280, true);
    fineWait.advance();
    fineWait.setRouteAge(2);
    fineWait.prepareBodyWait(fineOrigin, 4, 1280);
    auto fineRetry = fineWait;
    fineRetry.retryPreparedBodyWait();
    fineRetry.advance();
    fineWait.advance();
    const int fineDebt = fineWait.collisionDebt();
    fineWait.holdPreparedBodyRetry(fineRetry);
    check(fineWait.waitingForBody() && !fineWait.moving() &&
              fineWait.speed() == 0 && fineWait.nextSpeed() == 0 &&
              fineWait.facing() == fineRetry.desiredHeading() &&
              fineWait.turningBodyWait() &&
              fineWait.collisionDebt() == fineDebt &&
              samePoint(fineWait.rawPosition(), fineOrigin),
          "A rejected prepared body retry stops and steers without consuming another collision tick");
    auto overlapPreparation = immediateBodyWait;
    overlapPreparation.setRouteAge(2);
    overlapPreparation.waitForBody(immediateOrigin, 64, 2048, true);
    check(overlapPreparation.collisionPaused() &&
              !overlapPreparation.preparedBodyRetryReady(),
          "Overlap-recovery preparation retains its delay before body clearance retries");
    overlapPreparation.scriptSpeed(1280, true);
    overlapPreparation.advance();
    overlapPreparation.advance();
    check(overlapPreparation.collisionPaused() &&
              overlapPreparation.speed() == 1280 &&
              !overlapPreparation.moving(),
          "Delayed overlap planning can stop its walk while retaining the animation speed");
    auto finalPreparationTick = immediateBodyWait;
    finalPreparationTick.setRouteAge(6);
    finalPreparationTick.prepareBodyWait(immediateOrigin, 64, 2048);
    check(finalPreparationTick.collisionPaused(),
          "A scripted route at age six retains its last preparation tick");
    finalPreparationTick.advance();
    check(finalPreparationTick.waitingForBody(),
          "The final scripted preparation tick reaches body waiting");
    auto maturePreparation = immediateBodyWait;
    maturePreparation.setRouteAge(7);
    maturePreparation.prepareBodyWait(immediateOrigin, 64, 2048);
    check(maturePreparation.waitingForBody() &&
              !maturePreparation.collisionPaused(),
          "A mature scripted route does not add another preparation tick");
    auto deferredBodyWait = immediateBodyWait;
    deferredBodyWait.setRouteAge(2);
    deferredBodyWait.blockedWaypoint(immediateOrigin, 64, 2048, true);
    deferredBodyWait.scriptSpeed(1280, true);
    for (int step = 0; step < 5; ++step)
      deferredBodyWait.advance();
    check(deferredBodyWait.reroutesMovingObstacles(),
          "A deferred moving-body decision reaches its routing boundary");
    deferredBodyWait.acceptDeferredBodyWait();
    check(deferredBodyWait.waitingForBody() && deferredBodyWait.speed() == 1280,
          "A changed obstacle direction can retain body waiting without a step");
    deferredBodyWait.advance();
    auto deferredRetry = deferredBodyWait;
    deferredRetry.resumeBodyWait();
    deferredRetry.advance();
    check(deferredRetry.rawPosition().x == immediateOrigin.x + 2048 &&
              deferredRetry.rawPosition().y == immediateOrigin.y,
          "Deferred body waiting retains collision speed across animation writes");
    Motion scriptWait({300, 300}, 64, scriptTurnPhysics);
    scriptWait.route({{500, 300}}, 0);
    scriptWait.advance();
    scriptWait.scriptSpeed(2048);
    auto blockedOrigin = scriptWait.rawPosition();
    scriptWait.advance();
    scriptWait.setRouteAge(2);
    scriptWait.waitForBody(blockedOrigin, 64, 2048, true);
    scriptWait.scriptSpeed(2304, true);
    check(scriptWait.collisionPaused() && scriptWait.speed() == 2304 &&
              scriptWait.nextSpeed() == 2304,
          "A script speed write updates pending blocked departure speed");
    scriptWait.advance();
    check(scriptWait.moving() && scriptWait.speed() == 2304 &&
              samePoint(scriptWait.rawPosition(), blockedOrigin),
          "A young overlap-recovery collision preserves its initial walk state");
    scriptWait.scriptSpeed(1280, true);
    scriptWait.advance();
    check(!scriptWait.moving() && scriptWait.speed() == 1280 &&
              scriptWait.nextSpeed() == 1280 &&
              samePoint(scriptWait.rawPosition(), blockedOrigin),
          "The third blocked update stops walking while preserving script speed");
    for (int i = 0; i < 5 && scriptWait.collisionPaused(); ++i)
      scriptWait.advance();
    check(scriptWait.waitingForBody(),
          "A young overlap-recovery collision enters a retained body retry");
    scriptWait.advance();
    auto scriptRetry = scriptWait;
    scriptRetry.resumeBodyWait();
    scriptRetry.advance();
    check(scriptRetry.rawPosition().x == blockedOrigin.x + 2048 &&
              scriptRetry.rawPosition().y == blockedOrigin.y,
          "Body retry uses the rejected collision speed after later script writes");
    Motion alignedBlocked({369, 313}, 80, scriptTurnPhysics);
    alignedBlocked.route({{388, 321}}, 0);
    alignedBlocked.scriptSpeed(2304);
    auto blockedPoseOrigin = alignedBlocked.rawPosition();
    alignedBlocked.advance();
    alignedBlocked.setRouteAge(20);
    alignedBlocked.waitForBody(blockedPoseOrigin, 78, 2304);
    auto alignedTrial = alignedBlocked;
    alignedTrial.resumeBodyWait();
    alignedTrial.advance();
    alignedBlocked.acceptBlockedWaitTurn(alignedTrial);
    check(alignedBlocked.facing() == 78 &&
              alignedBlocked.travelHeading() == 79,
          "Aligned blocked travel keeps the body pose from before contact");
    Motion pendingBlocked({500, 505}, 0, scriptTurnPhysics);
    pendingBlocked.route({{500, 500}, {489, 453}}, 0, false, false);
    pendingBlocked.scriptSpeed(1280);
    pendingBlocked.advance();
    check(samePoint(pendingBlocked.position(), {500, 500}),
          "The continuing route reaches its first segment before contact");
    pendingBlocked.scriptSpeed(512);
    auto pendingBodyOrigin = pendingBlocked.rawPosition();
    pendingBlocked.advance();
    check(pendingBlocked.facing() == -10 && pendingBlocked.travelHeading() == -10,
          "The next segment proposes its new facing before contact is rejected");
    pendingBlocked.setRouteAge(20);
    pendingBlocked.waitForBody(pendingBodyOrigin, 0, 512);
    auto pendingTrial = pendingBlocked;
    pendingTrial.resumeBodyWait();
    pendingTrial.advance();
    pendingBlocked.acceptBlockedWaitTurn(pendingTrial);
    check(pendingBlocked.facing() == -10 && pendingBlocked.travelHeading() == -10 &&
              samePoint(pendingBlocked.rawPosition(), pendingBodyOrigin),
          "A blocked retry completes a pending body turn without translating");
    auto restartingWalk = pendingBlocked;
    restartingWalk.advance();
    restartingWalk.scriptSpeed(2048, true);
    check(!restartingWalk.moving(), "The blocked update has stopped pixel travel");
    restartingWalk.route({{650, 500}}, 0);
    check(restartingWalk.moving(),
          "A script-driven route restart retains its pending walk state");
    auto pendingNorth = pendingBlocked;
    pendingNorth.route({{500, 450}}, 0);
    pendingNorth.advance();
    pendingNorth.scriptSpeed(1280, true);
    pendingNorth.advance();
    check(pendingNorth.facing() == 0 && pendingNorth.travelHeading() == 0 &&
              samePoint(pendingNorth.rawPosition(), pendingBodyOrigin),
          "An accepted old direction cannot skip an unfinished body turn");
    pendingBlocked.route({{650, 500}}, 0);
    pendingBlocked.advance();
    pendingBlocked.scriptSpeed(1280, true);
    pendingBlocked.advance();
    check(pendingBlocked.facing() == 17 && pendingBlocked.travelHeading() == 17 &&
              samePoint(pendingBlocked.rawPosition(), pendingBodyOrigin),
          "A script-driven command restart turns travel with the stationary body");
    Motion occupiedGoal({581, 502}, 59, scriptTurnPhysics);
    occupiedGoal.route({{600, 500}}, 0);
    occupiedGoal.advance();
    auto occupiedOrigin = occupiedGoal.rawPosition();
    occupiedGoal.scriptSpeed(2048);
    occupiedGoal.advance();
    occupiedGoal.finishBlockedPoint(occupiedOrigin, 59);
    occupiedGoal.scriptSpeed(2304);
    check(occupiedGoal.arrived() && occupiedGoal.moving() &&
              occupiedGoal.facing() == 59 && occupiedGoal.nextSpeed() == 2304 &&
              samePoint(occupiedGoal.rawPosition(), occupiedOrigin),
          "An occupied final point ends travel while retaining the contact frame");
    auto acceptedArrival = occupiedGoal;
    acceptedArrival.faceOnArrival(Point{700, 502}, true, 43);
    acceptedArrival.faceOnArrival(Point{581, 300});
    auto acceptedCopy = acceptedArrival;
    acceptedArrival.advance();
    acceptedCopy.advance();
    check(acceptedArrival.facing() == 43 &&
              acceptedArrival.travelHeading() == 43 &&
              acceptedCopy.facing() == 43 &&
              samePoint(acceptedArrival.rawPosition(), occupiedOrigin),
          "Pending arrival consumes its admitted direction before a later target");
    acceptedArrival.faceOnArrival(Point{581, 300});
    acceptedArrival.advance();
    check(acceptedArrival.facing() == turnToward(43, 0, scriptTurnPhysics.turnRate) &&
              acceptedCopy.facing() == 43,
          "After admission clears, a fresh arrival target steers independently of its copy");
    occupiedGoal.advance();
    check(occupiedGoal.speed() == 0 && !occupiedGoal.moving() &&
              samePoint(occupiedGoal.rawPosition(), occupiedOrigin),
          "Occupied-point completion consumes its final stationary update");
    Motion scriptedWall({500, 500}, 64, scriptTurnPhysics);
    scriptedWall.route({{700, 500}}, 0);
    scriptedWall.scriptSpeed(1536, true);
    const auto wallOrigin = scriptedWall.rawPosition();
    scriptedWall.advance();
    scriptedWall.terrainBlocked(wallOrigin, 1536);
    check(scriptedWall.terrainFixing() && scriptedWall.speed() == 1536 &&
              scriptedWall.nextSpeed() == 1536 &&
              samePoint(scriptedWall.rawPosition(), wallOrigin),
          "Terrain contact enters recovery while retaining animation-supplied speed");
    scriptedWall.rejectTerrainRetry(wallOrigin, 0, true);
    scriptedWall.scriptSpeed(512, true);
    scriptedWall.advance();
    check(scriptedWall.terrainSlidingStep() && scriptedWall.speed() == 512 &&
              scriptedWall.facing() == 64 &&
              scriptedWall.acceptedTravelHeading() == 37 &&
              samePoint(scriptedWall.rawPosition(), {wallOrigin.x, wallOrigin.y - 512}),
          "Scripted wall sliding uses the new animation speed and retains body facing");
    scriptedWall.continueTerrainSlide();
    scriptedWall.scriptSpeed(2048, true);
    scriptedWall.advance();
    check(scriptedWall.speed() == 2048 && scriptedWall.facing() == 64 &&
              scriptedWall.acceptedTravelHeading() == 10 &&
              samePoint(scriptedWall.rawPosition(), {wallOrigin.x, wallOrigin.y - 2560}),
          "A later scripted slide consumes its speed write without physical acceleration");
    scriptedWall.continueTerrainSlide();
    scriptedWall.scriptSpeed(2304, true);
    const auto blockedSlideOrigin = scriptedWall.rawPosition();
    scriptedWall.advance();
    scriptedWall.blockedTerrainEscape(blockedSlideOrigin);
    check(scriptedWall.replanning() && scriptedWall.travelHeading() == 0 &&
              scriptedWall.acceptedTravelHeading() == 10 &&
              samePoint(scriptedWall.rawPosition(), blockedSlideOrigin),
          "A blocked terrain escape retains its admitted travel behind the visible axis");
    scriptedWall.replan({{400, 450}});
    scriptedWall.advance();
    scriptedWall.scriptSpeed(1536, true);
    scriptedWall.advance();
    check(scriptedWall.facing() == 37 && scriptedWall.travelHeading() == -17 &&
              samePoint(scriptedWall.rawPosition(), blockedSlideOrigin),
          "Terrain replanning turns the body and accepted travel independently");
    Motion staticCollision({1000, 1000}, -128, scriptTurnPhysics);
    staticCollision.route({{1000, 1100}}, 0);
    staticCollision.advance();
    const auto staticOrigin = staticCollision.rawPosition();
    for (int attempt = 0; attempt < 3; ++attempt) {
      staticCollision.scriptSpeed(1792, true);
      staticCollision.advance();
      if (attempt < 2) {
        staticCollision.waitForBody(staticOrigin, -128, 1792);
        staticCollision.resumeBodyWait();
      }
    }
    staticCollision.setRouteAge(3);
    auto preparedScriptContact = staticCollision;
    preparedScriptContact.prepareBodyWait(staticOrigin, -128, 1792);
    preparedScriptContact.scriptSpeed(0);
    check(preparedScriptContact.collisionPaused() &&
              preparedScriptContact.nextSpeed() == 1792,
          "Young-path body preparation retains pending travel without an animation speed write");
    for (int tick = 0; tick < 4; ++tick)
      preparedScriptContact.advance();
    preparedScriptContact.scriptSpeed(0);
    check(preparedScriptContact.waitingForBody() &&
              preparedScriptContact.nextSpeed() == 1792,
          "Entering a body wait preserves pending speed when idle animation has no speed write");
    preparedScriptContact.advance();
    check(preparedScriptContact.nextSpeed() == 0,
          "The first body-wait update clears pending travel on the following frame");
    auto blockedScriptWaypoint = staticCollision;
    blockedScriptWaypoint.blockedWaypoint(staticOrigin, -128, 1792, true);
    check(!blockedScriptWaypoint.moving() &&
              blockedScriptWaypoint.speed() == 0 &&
              blockedScriptWaypoint.nextSpeed() == 1792 &&
              blockedScriptWaypoint.collisionDebt() == 3,
          "A young scripted waypoint collision stops animation travel after repeated contact while retaining pending speed");
    staticCollision.blocked(staticOrigin, -128, 1792);
    auto movingStaticObstacle = staticCollision;
    for (int tick = 0; tick < 3; ++tick)
      movingStaticObstacle.advance();
    movingStaticObstacle.awaitMovingObstacle();
    movingStaticObstacle.advance();
    movingStaticObstacle.scriptSpeed(0);
    check(movingStaticObstacle.waitingForBody() &&
              movingStaticObstacle.collisionDebt() == 7 &&
              movingStaticObstacle.nextSpeed() == 1792,
          "A recorded obstacle starting to move changes static recovery to body waiting without clearing pending travel");
    check(staticCollision.collisionPaused() && !staticCollision.moving() &&
              staticCollision.speed() == 0 && staticCollision.nextSpeed() == 1792 &&
              staticCollision.collisionDebt() == 3 &&
              samePoint(staticCollision.rawPosition(), staticOrigin),
          "Scripted static contact stops current travel but retains pending animation speed");
    for (int tick = 0; tick < 3; ++tick) {
      staticCollision.advance();
      check(staticCollision.collisionPaused() && staticCollision.speed() == 0 &&
                staticCollision.nextSpeed() == 1792 &&
                staticCollision.collisionDebt() == tick + 4,
            "A young scripted collision retains its pending speed through planning delay");
    }
    staticCollision.advance();
    check(staticCollision.replanning() && staticCollision.speed() == 0 &&
              staticCollision.nextSpeed() == 0 && staticCollision.collisionDebt() == 7,
          "Scripted collision delay clears pending speed when its path can restart");
    check(staticCollision.reroutesRecordedObstacle(),
          "Scripted static restart retains its recorded obstacle for boundary departure planning");
    auto staticCopy = staticCollision;
    staticCollision.replan({{1014, 994}});
    staticCollision.advance();
    check(staticCollision.facing() == -128 &&
              samePoint(staticCollision.rawPosition(), staticOrigin),
          "Scripted collision replan preserves the stationary setup frame");
    staticCollision.advance();
    check(staticCollision.facing() == 101 && staticCollision.travelHeading() == 101 &&
              staticCopy.facing() == -128 &&
              samePoint(staticCollision.rawPosition(), staticOrigin),
          "A static collision restart turns both script directions before taking a step");
    Motion waypointSlide({1000, 1000}, -27, scriptTurnPhysics);
    waypointSlide.route({{998, 998}, {998, 900}}, 0);
    waypointSlide.scriptSpeed(512, true);
    const auto waypointSlideOrigin = waypointSlide.rawPosition();
    waypointSlide.terrainBlocked(waypointSlideOrigin, 512);
    waypointSlide.rejectTerrainRetry(waypointSlideOrigin, 0);
    const int waypointSlideFacing = waypointSlide.facing();
    waypointSlide.scriptSpeed(2048, true);
    waypointSlide.advance();
    check(samePoint(waypointSlide.rawPosition(), {998 * 256, 998 * 256}) &&
              samePoint(waypointSlide.waypoint(), {998, 998}) &&
              waypointSlide.facing() == waypointSlideFacing &&
              waypointSlide.travelHeading() == 0,
          "A scripted terrain slide admits a nearby corner without overshooting or consuming the next waypoint");
    auto admittedCornerCopy = waypointSlide;
    waypointSlide.scriptSpeed(2304, true);
    waypointSlide.advance();
    check(samePoint(waypointSlide.waypoint(), {998, 900}) &&
              samePoint(waypointSlide.rawPosition(), {998 * 256, 998 * 256 - 2304}) &&
              samePoint(admittedCornerCopy.rawPosition(), {998 * 256, 998 * 256}),
          "The update after corner admission advances its waypoint and consumes the pending script step independently of a copy");
    Motion savedTerrainRetry({1000, 1000}, -17, scriptTurnPhysics);
    savedTerrainRetry.route({{990, 980}, {990, 900}}, 0);
    const auto savedRetryOrigin = savedTerrainRetry.rawPosition();
    savedTerrainRetry.scriptSpeed(512, true);
    savedTerrainRetry.terrainBlocked(savedRetryOrigin, 512);
    savedTerrainRetry.scriptSpeed(2048, true);
    savedTerrainRetry.advance();
    Point savedRetryDelta{savedTerrainRetry.rawPosition().x - savedRetryOrigin.x,
                         savedTerrainRetry.rawPosition().y - savedRetryOrigin.y};
    check(savedTerrainRetry.terrainRetryStep() &&
              savedTerrainRetry.terrainProbeSpeed() == 512 &&
              !samePoint(savedTerrainRetry.terrainProbeVelocity(), {}) &&
              std::max(std::abs(savedRetryDelta.x), std::abs(savedRetryDelta.y)) > 0 &&
              std::max(std::abs(savedRetryDelta.x), std::abs(savedRetryDelta.y)) <= 512,
          "A terrain retry uses its saved collision speed despite a later animation speed write");
    {
      Motion arrivalTurn({1000, 1000}, 64, physics);
      arrivalTurn.route({{1040, 1000}}, 0);
      arrivalTurn.faceOnArrival(Point{1040, 1200});
      for (int step = 0; step != 100 && !arrivalTurn.arrived(); ++step)
        arrivalTurn.advance();
      check(arrivalTurn.arrived(), "Arrival-turn fixture must reach its point");
      arrivalTurn.advance();
      const auto raw = arrivalTurn.rawPosition();
      const int speed = arrivalTurn.speed();
      auto saved = arrivalTurn;
      check(speed >= 259 && !arrivalTurn.moving() &&
                arrivalTurn.travelHeading() != -128,
            "A stationary point arrival retains speed while still turning");
      arrivalTurn.advance();
      check(samePoint(arrivalTurn.rawPosition(), raw) &&
                arrivalTurn.speed() == speed - physics.acceleration &&
                saved.speed() == speed &&
                saved.travelHeading() != arrivalTurn.travelHeading(),
            "Turning after a non-braking arrival decelerates retained inertia without moving or mutating a copy");
    }
    {
      Motion pendingSlide({800, 800}, 64, physics);
      pendingSlide.route({{1800, 800}}, 0);
      for (int step = 0; step != 20; ++step)
        pendingSlide.advance();
      auto pixel = pendingSlide.position();
      pendingSlide.route({{pixel.x, pixel.y - 200}}, 0);
      const auto raw = pendingSlide.rawPosition();
      const int facing = pendingSlide.facing();
      const int speed = pendingSlide.nextSpeed();
      pendingSlide.advance();
      pendingSlide.slideBlocked(raw, facing, -64, speed);
      const int attempted = pendingSlide.travelHeading();
      const int reserved = pendingSlide.collisionTravelHeading();
      check(attempted != reserved,
            "The rejected-turn fixture has distinct visible and reserved travel");
      pendingSlide.route({{pixel.x - 200, pixel.y}}, 0);
      pendingSlide.advance();
      check(pendingSlide.travelHeading() == attempted,
            "A replacement route retains the rejected heading during preparation");
      auto saved = pendingSlide;
      pendingSlide.advance();
      check(pendingSlide.travelHeading() ==
                turnToward(reserved, pendingSlide.desiredHeading(), physics.turnRate / 2) &&
                samePoint(pendingSlide.rawPosition(), raw) &&
                saved.travelHeading() == attempted,
            "The first replacement turn uses reserved travel rather than turning the rejected heading twice");
    }
    physics.control = 1;
    rejects([&] { Motion unsupported({0, 0}, 0, physics); });
    std::cout << "PASS: native motion, routing and random state\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
