// SPDX-License-Identifier: MIT
#include <algorithm>
#include <iostream>
#include <mapeditor/simulation/local_route.h>
#include <stdexcept>
using namespace mapeditor::simulation;
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
int main() {
  try {
    // Compare indexed collision queries with a flat scan, including cell
    // borders, long rectangles, negative edges and partially clipped walls.
    {
      std::vector<PixelBox> boxes{{-20,-10,0,600},{250,255,258,514},{511,0,512,1000}};
      std::uint32_t state=42;
      auto next=[&]{state=state*1664525U+1013904223U;return state;};
      for(int i=0;i<180;++i){int x=int(next()%1024),y=int(next()%1024);boxes.push_back({x,y,x+int(next()%80),y+int(next()%80)});}
      LocalSpace indexed({0,0,1023,1023},boxes);
      for(int i=0;i<12000;++i) {
        int x=int(next()%1080)-20,y=int(next()%1080)-20;
        PixelBox area{x,y,x+int(next()%70),y+int(next()%70)};
        bool flat=area.left>=0&&area.top>=0&&area.right<=1023&&area.bottom<=1023;
        for(const auto& b:boxes) if(area.left<=b.right&&area.right>=b.left&&area.top<=b.bottom&&area.bottom>=b.top)flat=false;
        check(indexed.clear(area)==flat,"Spatial collision index changed clearance");
      }
      GroundGeometry geometry{{0,0,1023,1023},{8,4,7,11},boxes,{}};
      std::vector<CollisionBody> bodies{{1,{100,120},{8,4,7,11},false,true}};
      auto a=geometry.cachedWithBodies(bodies);
      check(a==geometry.cachedWithBodies(bodies),"Identical obstacle state was not reused");
      bodies[0].position.x=130;
      auto b=geometry.cachedWithBodies(bodies);
      check(a!=b,"Changed body position reused a stale navigation space");
      auto c=geometry.cachedWithBodies(bodies,Point{115,120});
      check(b!=c,"Departure state missing from the navigation cache key");
      auto direct=geometry.withBodies(bodies,Point{115,120});
      for(int i=0;i<1200;++i){int x=int(next()%1024),y=int(next()%1024);check(c->clear({x,y,x,y})==direct.clear({x,y,x,y}),"Cached clearance differs");}
    }
    LocalRouteResult stored;
    for (int i = 0; i < 21; ++i)
      stored.retained.regions.push_back(i);
    stored.retained.fullSize = 21;
    stored.retained.cursor = 16;
    stored.points.resize(14);
    limitRouteStorage(stored);
    check(stored.retained.regions.size() == 20 &&
              stored.retained.cursor == 16 && stored.itinerary.size() == 4 &&
              stored.itineraryTruncated,
          "Local points share storage with consumed regional history");
    stored.points.resize(3);
    limitRouteStorage(stored);
    check(stored.retained.regions.size() == 20 && stored.itineraryTruncated,
          "A shorter continuation does not restore discarded regional IDs");
    for (int count : {22, 23, 24, 50}) {
      LocalRouteResult packed;
      packed.retained.regions = {0, 1, 2, 3, 4};
      packed.retained.fullSize = 5;
      packed.retained.cursor = 4;
      packed.points.resize(count);
      packed.reached = true;
      limitRouteStorage(packed);
      auto expectedRegions = count == 22 ? 4U : count == 23 ? 2U : 1U;
      check(packed.retained.regions.size() == expectedRegions &&
                packed.retained.cursor == expectedRegions &&
                packed.itinerary.empty() && packed.itineraryTruncated &&
                packed.points.size() == std::size_t(std::min(count, 23)) &&
                packed.reached == (count < 24),
            "Storage boundaries retain the regional end sentinel and local "
            "prefix");
    }
    const std::vector<PixelBox> upper{{10, 10, 30, 19}},
        lower{{10, 20, 30, 40}}, joined{{10, 10, 30, 19}, {10, 20, 30, 40}},
        separated{{10, 10, 30, 18}, {10, 20, 30, 40}};
    check((unexploredLocalDirections({20, 20}, upper) & 16) &&
              (unexploredLocalDirections({20, 20}, lower) & 16),
          "Either separate sweep retains a diagonal continuation");
    check(unexploredLocalDirections({20, 20}, joined) == 0 &&
              unexploredLocalDirections({30, 20}, joined) == 50,
          "Joined sweeps close internal exits while retaining exterior ports");
    check(unexploredLocalDirections({20, 20}, separated) & 1,
          "A gap between sweeps retains its northward continuation");
    LocalSpace empty({0, 0, 2047, 2015}, {});
    auto expansion = empty.expand({1024, 1024});
    check(expansion.frontier.size() == 8 && expansion.areas.size() == 4,
          "Empty local expansion has cardinal and diagonal ports");
    auto diagonal = empty.expand({1024, 1024}, 64, 1U << 4);
    check(diagonal.areas.size() == 1 && diagonal.frontier.size() == 3,
          "Diagonal expansion exposes both axial boundary ports");
    check(diagonal.areas[0] == PixelBox{1024, 960, 1088, 1024},
          "Direction-limited expansion clips its search window");
    LocalSpace horizonObstacle({0, 0, 2047, 2015}, {{1089, 900, 1120, 1023}});
    auto horizonObstaclePorts = horizonObstacle.expand({1024, 1024});
    auto prospectiveCorner =
        std::find_if(horizonObstaclePorts.frontier.begin(),
                     horizonObstaclePorts.frontier.end(),
                     [](Point p) { return p.x == 1088 && p.y == 960; });
    check(
        prospectiveCorner != horizonObstaclePorts.frontier.end() &&
            horizonObstaclePorts.allowed[std::size_t(
                prospectiveCorner - horizonObstaclePorts.frontier.begin())] ==
                179,
        "A full-horizon corner retains prospective exits beyond visible walls");
    LocalSpace clippedCorner({0, 0, 2047, 2015},
                             {{940, 980, 959, 1010}, {960, 980, 1024, 991}},
                             std::vector<NavigationWall>{{0, 991, 960, 1024}});
    auto clippedCornerPorts = clippedCorner.expand({1024, 1024});
    auto closedCorner = std::find_if(
        clippedCornerPorts.frontier.begin(), clippedCornerPorts.frontier.end(),
        [](Point p) { return p.x == 960 && p.y == 992; });
    check(
        clippedCorner.clear({960, 992, 960, 992}) &&
            closedCorner != clippedCornerPorts.frontier.end() &&
            clippedCornerPorts.allowed[std::size_t(
                closedCorner - clippedCornerPorts.frontier.begin())] == 0,
        "A legal clipped corner can terminate a path without opening an exit");
    LocalSpace projectedCorner(
        {0, 0, 2047, 2015},
        {{940, 980, 959, 1010}, {1000, 980, 1024, 991}},
        std::vector<NavigationWall>{{0, 991, 1000, 1024}});
    auto projectedPorts = projectedCorner.expand({1024, 1024});
    auto projectedExit = std::find_if(projectedPorts.frontier.begin(),
        projectedPorts.frontier.end(),
        [](Point p) { return p.x == 960 && p.y == 992; });
    check(projectedExit != projectedPorts.frontier.end() &&
          projectedPorts.allowed[std::size_t(projectedExit -
                                             projectedPorts.frontier.begin())] == 217,
          "A projected outer window corner retains exits beyond visible contours");
    LocalSpace collapsedReturn(
        {0, 0, 511, 511}, {},
        std::vector<NavigationWall>{{0, 75, 89, 105, -1, -1},
                                    {1, 105, 75, 83, 1, 1},
                                    {1, 169, 107, 115, 1, 1}});
    auto collapsedPorts = collapsedReturn.expand({104, 108}, 64, 17);
    auto retainedExit = std::find_if(collapsedPorts.frontier.begin(),
        collapsedPorts.frontier.end(),
        [](Point p) { return p.x == 104 && p.y == 76; });
    check(retainedExit != collapsedPorts.frontier.end() &&
          collapsedPorts.allowed[std::size_t(retainedExit -
                                             collapsedPorts.frontier.begin())] == 72,
          "A collapsed strip preserves a previously discovered lateral return");
    LocalSpace splitWalls({0, 0, 2047, 2015}, {},
                          std::vector<NavigationWall>{{0, 1000, 980, 1020},
                                                      {0, 1000, 1010, 1060}});
    LocalSpace joinedWall({0, 0, 2047, 2015}, {},
                          std::vector<NavigationWall>{{0, 1000, 980, 1060}});
    auto splitExpansion = splitWalls.expand({1024, 1024});
    auto joinedExpansion = joinedWall.expand({1024, 1024});
    check(std::equal(
              splitExpansion.frontier.begin(), splitExpansion.frontier.end(),
              joinedExpansion.frontier.begin(), joinedExpansion.frontier.end(),
              [](Point a, Point b) { return a.x == b.x && a.y == b.y; }) &&
              splitExpansion.allowed == joinedExpansion.allowed &&
              splitExpansion.areas == joinedExpansion.areas,
          "Joined footprint walls expose no interior endpoint ports");
    LocalSpace cornerWalls(
        {0, 0, 2047, 2015}, {{1025, 928, 1040, 983}},
        std::vector<NavigationWall>{{0, 959, 969, 1025, -1, -1},
                                    {1, 1025, 928, 983, -1, 1}});
    auto cornerPorts = cornerWalls.expand({1024, 1024}, 64, 200);
    auto northPort =
        std::find_if(cornerPorts.frontier.begin(), cornerPorts.frontier.end(),
                     [](Point p) { return p.x == 1024 && p.y == 960; });
    check(northPort != cornerPorts.frontier.end() &&
              cornerPorts.allowed[std::size_t(
                  northPort - cornerPorts.frontier.begin())] == 34,
          "Boundary endpoint at the near sweep limit preserves the axial port");
    auto clippedReturnPorts = cornerWalls.expand({1024, 1016}, 64, 1U << 7);
    auto clippedReturn = std::find_if(
        clippedReturnPorts.frontier.begin(), clippedReturnPorts.frontier.end(),
        [](Point p) { return p.x == 1024 && p.y == 960; });
    check(clippedReturn != clippedReturnPorts.frontier.end() &&
              clippedReturnPorts.allowed[std::size_t(
                  clippedReturn - clippedReturnPorts.frontier.begin())] == 34,
          "A virtual diagonal exit preserves an already exposed axial return");
    LocalSpace clippedAxis({0, 0, 2047, 2015}, {},
                           std::vector<NavigationWall>{{1, 1034, 1000, 1100},
                                                       {2, 1034, 900, 1100}});
    auto clippedPorts = clippedAxis.expand({1024, 1024}, 64, 1U << 5);
    auto clippedExit =
        std::find_if(clippedPorts.frontier.begin(), clippedPorts.frontier.end(),
                     [](Point p) { return p.x == 1024 && p.y == 1033; });
    check(clippedExit != clippedPorts.frontier.end() &&
              clippedPorts.allowed[std::size_t(
                  clippedExit - clippedPorts.frontier.begin())] == 100,
          "A diagonal sweep retains its forward exit on a clipped unrequested "
          "axis");
    LocalSpace narrow({0, 0, 2047, 2015}, {},
                      std::vector<NavigationWall>{{0, 999, 900, 1100}});
    auto narrowPorts = narrow.expand({1024, 1064}, 64, 1);
    check(narrowPorts.frontier.size() == 1 && narrowPorts.allowed[0] == 0,
          "Degenerate quadrant corners close a terminal axial port");
    LocalSpace elbow({0, 0, 2047, 2015}, {},
                     std::vector<NavigationWall>{{0, 991, 1001, 1009, -1, -1},
                                                 {1, 1001, 897, 991, -1, 1}});
    auto elbowPorts = elbow.expand({1000, 1000}, 64, 1U << 4);
    auto elbowTip =
        std::find_if(elbowPorts.frontier.begin(), elbowPorts.frontier.end(),
                     [](Point p) { return p.x == 1000 && p.y == 936; });
    check(
        elbowTip != elbowPorts.frontier.end() &&
            elbowPorts.allowed[std::size_t(elbowTip -
                                           elbowPorts.frontier.begin())] == 201,
        "A collapsed elbow retains the existing wall beyond its sweep window");
    LocalSpace slit({0, 0, 2047, 2015}, {},
                    std::vector<NavigationWall>{{0, 999, 1001, 1065, -1, -1},
                                                {1, 1001, 967, 999, 1, 1},
                                                {1, 1001, 1001, 1063, -1, 1},
                                                {1, 1065, 999, 1031, 1, 1},
                                                {2, 1001, 1001, 1039, -1, 1},
                                                {3, 1039, 1001, 1009, -1, -1}});
    auto slitPorts = slit.expand({1000, 1000}, 64, 1U << 1);
    auto slitExit =
        std::find_if(slitPorts.frontier.begin(), slitPorts.frontier.end(),
                     [](Point p) { return p.x == 1064 && p.y == 1000; });
    check(slitExit != slitPorts.frontier.end() &&
              slitPorts.allowed[std::size_t(slitExit -
                                            slitPorts.frontier.begin())] == 68,
          "A one-pixel slit opens the free side beyond its shorter wall");
    auto gapPorts = slit.expand({944, 968}, 64, (1U << 1) | (1U << 5));
    auto gapCorner =
        std::find_if(gapPorts.frontier.begin(), gapPorts.frontier.end(),
                     [](Point p) { return p.x == 1000 && p.y == 1000; });
    check(
        gapCorner != gapPorts.frontier.end() &&
            gapPorts.allowed[std::size_t(gapCorner -
                                         gapPorts.frontier.begin())] == 102,
        "Open axis exits retain the diagonal beside a separated wall endpoint");
    LocalSpace obstacle({11, 11, 2036, 2004}, {{964, 965, 1084, 1051}});
    expansion = obstacle.expand({1056, 1072});
    check(expansion.frontier[0].x == 1056 && expansion.frontier[0].y == 1052,
          "Expanded obstacle clips north cardinal ray");
    for (auto area : expansion.areas)
      check(obstacle.clear(area), "Expansion enters obstacle");
    std::vector<std::uint8_t> cv(104), vf(32);
    for (int i = 0; i < 16; ++i)
      vf[i * 2] = 1;
    Terrain terrain(64, 64, std::vector<std::uint16_t>(4096, 16), cv, vf);
    Regions regions(terrain);
    auto closedGoal =
        planGroundRoute(clippedCorner, regions, {1024, 1024}, {960, 992});
    check(closedGoal.reached && closedGoal.points.size() == 1 &&
              closedGoal.points[0].x == 960 && closedGoal.points[0].y == 992,
          "A closed frontier corner remains an eligible destination");
    std::vector<CollisionBody> column;
    for (int i = 0; i < 4; ++i)
      column.push_back(
          {std::uint32_t(i), {1184, 880 + i * 64}, {32, 16, 31, 15}});
    column.push_back({4, {1152, 1136}, {32, 16, 31, 15}});
    auto terrainGeometry =
        groundGeometry(terrain, Contours(terrain), {11, 11, 11, 11});
    auto columnSpace = terrainGeometry.withBodies(column);
    auto noColumn = terrainGeometry.withBodies({});
    {
      auto ownedRegions = std::make_shared<const Regions>(regions);
      auto samePoint = [](Point a, Point b) {
        return a.x == b.x && a.y == b.y;
      };
      auto equal = [&](const LocalRouteResult &a, const LocalRouteResult &b) {
        check(a.itinerary == b.itinerary &&
                  a.itineraryTruncated == b.itineraryTruncated &&
                  a.retained.regions == b.retained.regions &&
                  a.retained.cursor == b.retained.cursor &&
                  a.retained.fullSize == b.retained.fullSize &&
                  a.reached == b.reached && a.stuck == b.stuck &&
                  a.nodes == b.nodes && a.peakFrontier == b.peakFrontier &&
                  samePoint(a.source, b.source) &&
                  samePoint(a.destination, b.destination) &&
                  a.points.size() == b.points.size(),
              "Cached route changed regional or local state");
        for (std::size_t i = 0; i < a.points.size(); ++i)
          check(samePoint(a.points[i], b.points[i]),
                "Cached route changed a waypoint");
      };
      auto previous =
          planGroundRoute(noColumn, regions, {500, 500}, {1100, 1100});
      for (int i = 0; i < 48; ++i) {
        Point source{500 + i, 600}, target{1100, 1100 + i};
        auto contact = i % 3 == 0
                           ? std::optional<PixelBox>{{1090, 1090, 1120, 1160}}
                           : std::nullopt;
        auto adjusted =
            i % 3 == 1 ? std::optional<Point>{{1110, 1120}} : std::nullopt;
        const auto *prior = i % 2 ? &previous : nullptr;
        auto direct = planGroundRoute(noColumn, regions, source, target,
                                      contact, adjusted, nullptr, prior);
        equal(direct, noColumn.route(ownedRegions, source, target, contact,
                                     adjusted, prior));
        equal(direct, noColumn.route(ownedRegions, source, target, contact,
                                     adjusted, prior));
      }
    }
    auto changedColumn = column;
    changedColumn[0].position = {1600, 880};
    auto movedColumn = terrainGeometry.withBodies(changedColumn);
    check(!columnSpace.clear({1184, 880, 1184, 880}) &&
              noColumn.clear({1184, 880, 1184, 880}) &&
              movedColumn.clear({1184, 880, 1184, 880}) &&
              !movedColumn.clear({1600, 880, 1600, 880}),
          "Shared terrain geometry keeps each request's bodies independent");
    auto columnRoute =
        planGroundRoute(columnSpace, regions, {1172, 916}, {1152, 1136},
                        PixelBox{1108, 1108, 1195, 1163});
    std::vector<Point> columnExpected{
        {1140, 916}, {1140, 980}, {1140, 1044}, {1140, 1108}};
    check(columnRoute.reached &&
              std::equal(
                  columnRoute.points.begin(), columnRoute.points.end(),
                  columnExpected.begin(), columnExpected.end(),
                  [](Point a, Point b) { return a.x == b.x && a.y == b.y; }),
          "A mineral column retains axial continuation through degenerate "
          "sweep junctions");

    auto horizonPorts = columnSpace.expand({1139, 980});
    auto horizonCorner =
        std::find_if(horizonPorts.frontier.begin(), horizonPorts.frontier.end(),
                     [](Point p) { return p.x == 1140 && p.y == 1044; });
    check(horizonCorner != horizonPorts.frontier.end() &&
              horizonPorts.allowed[std::size_t(
                  horizonCorner - horizonPorts.frontier.begin())] ==
                  ((1U << 1) | (1U << 2) | (1U << 4) | (1U << 5) | (1U << 6)),
          "A horizon corner retains forward directions beyond the current "
          "sweep without opening known interior barriers");

    auto currentContact =
        planGroundRoute(obstacle, regions, {1056, 1052}, {1024, 1008},
                        PixelBox{963, 964, 1085, 1052});
    check(currentContact.reached && currentContact.points.size() == 1 &&
              currentContact.destination.x == 1056 &&
              currentContact.destination.y == 1052,
          "A source already on the contact perimeter becomes the reached "
          "destination");

    Contours contours(terrain);
    dat::Extents footprint{11, 11, 11, 11};
    std::vector<OrderGroupMember> formation{
        {{500, 500}, footprint, true}, {{550, 550}, footprint, true}};
    auto at = [](Point p, int x, int y) { return p.x == x && p.y == y; };
    auto group = projectGroupOrderTargets(terrain, contours, regions, formation,
                                          {1000, 1000});
    check(group && group->keepsOffsets &&
              at(group->destinations[0], 975, 975) &&
              at(group->destinations[1], 1025, 1025),
          "A compact ground selection preserves its formation around a clear request");
    group = projectGroupOrderTargets(terrain, contours, regions, formation,
                                     {540, 540});
    check(group && group->insideSelection && !group->keepsOffsets &&
              at(group->destinations[0], 508, 508) &&
              at(group->destinations[1], 550, 550),
          "An internal request limits each unit's displacement around the request");
    formation[1].position = {900, 900};
    group = projectGroupOrderTargets(terrain, contours, regions, formation,
                                     {1200, 1200});
    check(group && !group->keepsOffsets &&
              at(group->destinations[0], 1200, 1200) &&
              at(group->destinations[1], 1200, 1200),
          "A dispersed selection converges without retaining distant offsets");
    auto ground = groundSpace(terrain, contours, footprint);
    auto segment = planGroundRoute(ground, regions, {128, 128}, {1800, 1800});
    check(!segment.itinerary.empty(),
          "Local path retains its region itinerary");
    for (int step = 0; !segment.reached && step < 40; ++step) {
      check(!segment.points.empty(),
            "Partial path has a continuation endpoint");
      auto previous = segment;
      segment =
          planGroundRoute(ground, regions, previous.points.back(),
                          previous.destination, {}, {}, nullptr, &previous);
    }
    check(segment.reached && !segment.stuck && !segment.points.empty() &&
              segment.points.back().x == 1800 &&
              segment.points.back().y == 1800,
          "Repeated local segments reach their retained destination");
    check(ground.clear({1024, 1024, 1024, 1024}) &&
              !ground.clear({10, 1024, 10, 1024}),
          "Ground-space builder preserves footprint map bounds");
    check(!ground.clear({50, 2000, 50, 2000}),
          "Ground space excludes reserved map UI terrain");
    auto holeCv = cv, holeVf = vf;
    holeCv[52 + 22] = 1;
    holeVf.resize(64);
    for (int i = 0; i < 16; ++i)
      holeVf[32 + i * 2] = 1;
    holeVf[32 + 5 * 2] = 0;
    std::vector<std::uint16_t> holeTiles(4096, 16);
    holeTiles[10 * 64 + 10] = 17;
    Terrain hole(64, 64, holeTiles, holeCv, holeVf);
    Contours holeContours(hole);
    Regions holeRegions(hole);
    auto adjusted =
        adjustGroundTarget(hole, holeRegions, footprint, {331, 331});
    check(adjusted && adjusted->x == 331 && adjusted->y == 331,
          "Target adjustment accepts partial regional tiles independently of "
          "minitile clearance");
    auto orderPoint = projectGroundOrderTarget(
        hole, holeContours, holeRegions, {0, 0, 0, 0}, {331, 331}, {300, 300});
    check(orderPoint && orderPoint->x == 336 && orderPoint->y == 336,
          "Order projection corrects a blocked minitile within a partial tile");
    orderPoint = projectGroundOrderTarget(
        hole, holeContours, holeRegions, {0, 0, 0, 0}, {340, 340}, {300, 300});
    check(orderPoint && orderPoint->x == 340 && orderPoint->y == 340,
          "An already clear order target preserves its requested pixel");
    auto blockedVf = holeVf;
    std::fill(blockedVf.begin() + 32, blockedVf.end(), 0);
    Terrain blocked(64, 64, holeTiles, holeCv, blockedVf);
    Regions blockedRegions(blocked);
    adjusted =
        adjustGroundTarget(blocked, blockedRegions, footprint, {336, 336});
    check(adjusted && adjusted->x == 304 && adjusted->y == 304,
          "Target adjustment scans clockwise square borders in footprint-safe "
          "order");
    check(
        !groundSpace(hole, holeContours, footprint).clear({331, 331, 331, 331}),
        "Blocked center rejects navigation");
    std::vector<std::uint8_t> creep(4096);
    creep[10 * 64 + 10] = 1;
    check(groundSpace(hole, holeContours, footprint, {}, creep)
              .clear({331, 331, 331, 331}),
          "Dynamic creep overrides center walkability independently of static "
          "walls");
    for (auto target :
         {Point{1280, 1024}, Point{900, 1000}, Point{1024, 800}}) {
      auto route = localGroundRoute(obstacle, regions, {1056, 1072}, target);
      check(route.back().x == target.x && route.back().y == target.y,
            "Local route reaches destination");
      Point from{1056, 1072};
      for (auto p : route) {
        check(obstacle.clear({std::min(from.x, p.x), std::min(from.y, p.y),
                              std::max(from.x, p.x), std::max(from.y, p.y)}),
              "Local route crosses obstacle");
        from = p;
      }
    }
    LocalSpace openContact({11, 11, 2036, 2004}, {});
    auto openGoal = planGroundRoute(openContact, regions, {193, 311}, {193, 289},
                                    PixelBox{171, 266, 217, 312});
    check(openGoal.reached && !openGoal.stuck &&
              openGoal.points.back().x == 193 &&
              openGoal.points.back().y == 289,
          "An unobstructed clicked point remains a goal inside a contact box");
    LocalSpace mineral({11, 11, 2036, 2004},
                       {{964, 965, 1084, 1051}, {1237, 997, 1322, 1050}});
    auto route = localGroundRoute(mineral, regions, {1056, 1072}, {1280, 1024},
                                  PixelBox{1236, 996, 1323, 1051});
    check(route.back().x == 1236 && route.back().y == 1051,
          "Unit target stops at contact perimeter");
    LocalSpace approach({11, 11, 2036, 2004}, {{981, 1253, 1066, 1306}});
    auto retry = localGroundRoute(approach, regions, {1047, 1061}, {1048, 1252},
                                  PixelBox{980, 1252, 1067, 1307});
    check(retry.back().x == 1048 && retry.back().y == 1252,
          "Replanning to an existing contact endpoint retains its exact "
          "position");
    LocalSpace northContact({11, 11, 2036, 2004}, {{981, 725, 1066, 778}});
    auto northApproach =
        localGroundRoute(northContact, regions, {1067, 964}, {1024, 752},
                         PixelBox{980, 724, 1067, 779});
    check(northApproach.back().x == 1067 && northApproach.back().y == 779,
          "A closed north sweep junction can finish a contact approach");
    LocalSpace southContact({11, 11, 2036, 2004}, {{1257, 1083, 1342, 1136}});
    auto southApproach =
        localGroundRoute(southContact, regions, {1000, 1080}, {1300, 1110},
                         PixelBox{1256, 1082, 1343, 1137});
    check(southApproach.back().x == 1256 && southApproach.back().y == 1137,
          "Contact search uses sweep ports rather than arbitrary area corners");
    std::vector<PixelBox> overlappingColumn;
    for (int i = 0; i < 8; ++i)
      overlappingColumn.push_back({1109, 933 + i * 32, 1194, 986 + i * 32});
    LocalSpace overlapping({11, 11, 2036, 2004}, overlappingColumn);
    auto junction = overlapping.expand({1195, 1028}, 64, (1U << 3) | (1U << 7));
    auto junctionPort =
        std::find_if(junction.frontier.begin(), junction.frontier.end(),
                     [](Point p) { return p.x == 1195 && p.y == 964; });
    check(junctionPort != junction.frontier.end() &&
              junction.allowed[std::size_t(junctionPort -
                                           junction.frontier.begin())] ==
                  ((1U << 0) | (1U << 1) | (1U << 4)),
          "An internal footprint junction retains only the forward exterior "
          "corner directions");
    std::vector<CollisionBody> staggeredColumn;
    LocalSpace returnCorner({0, 0, 2047, 2015}, {{960, 960, 1021, 1003}});
    LocalSpace northWall({0, 0, 2047, 2015}, {{1001, 936, 1084, 967}});
    auto northWallPorts = northWall.expand({1024, 1024}, 64, 1U << 7);
    auto axial = std::find_if(
        northWallPorts.frontier.begin(), northWallPorts.frontier.end(),
        [](Point p) { return p.x == 1024 && p.y == 968; });
    auto endpoint = std::find_if(
        northWallPorts.frontier.begin(), northWallPorts.frontier.end(),
        [](Point p) { return p.x == 1000 && p.y == 968; });
    check(axial != northWallPorts.frontier.end() &&
              endpoint != northWallPorts.frontier.end() && axial < endpoint,
          "A north-wall axial return precedes equal-distance outer endpoints");
    LocalSpace steppedRoof({0, 0, 2047, 2047},
                           {{953, 900, 975, 999}, {993, 900, 1015, 991}});
    auto roofPorts = steppedRoof.expand({1008, 1000}, 64, 129);
    check(std::none_of(roofPorts.frontier.begin(), roofPorts.frontier.end(),
                       [](Point p) { return p.x == 992 && p.y == 1000; }),
          "A roof-clipped strip does not invent a vertical-wall return");
    LocalSpace clippedSouthRoof(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{3, 975, 998, 1006, 1, 1},
                                    {2, 1049, 994, 1002, -1, -1}});
    auto clippedSouthPorts = clippedSouthRoof.expand({1000, 1000});
    auto southReturn = std::find_if(
        clippedSouthPorts.frontier.begin(), clippedSouthPorts.frontier.end(),
        [](Point p) { return p.x == 993 && p.y == 1007; });
    check(southReturn != clippedSouthPorts.frontier.end() &&
              clippedSouthPorts.allowed[std::size_t(
                  southReturn - clippedSouthPorts.frontier.begin())] == 64,
          "A positive south sweep retains a return under a clipped roof");
    auto southCorner = std::find_if(
        clippedSouthPorts.frontier.begin(), clippedSouthPorts.frontier.end(),
        [](Point p) { return p.x == 993 && p.y == 1048; });
    check(southCorner != clippedSouthPorts.frontier.end() &&
              southCorner < southReturn,
          "A narrowed southwest strip discovers its corner before its return");
    LocalSpace clippedNorthEastRoof(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{1, 1025, 994, 1002, -1, -1},
                                    {0, 951, 998, 1006, 1, 1}});
    auto clippedNorthPorts = clippedNorthEastRoof.expand({1000, 1000});
    check(std::any_of(clippedNorthPorts.frontier.begin(),
                      clippedNorthPorts.frontier.end(),
                      [](Point p) { return p.x == 1007 && p.y == 993; }),
          "A positive northeast sweep retains a return under a clipped roof");
    LocalSpace clippedNorthWestRoof(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{0, 938, 997, 1013, -1, -1},
                                    {3, 994, 989, 1042, -1, 1}});
    auto northWestPorts = clippedNorthWestRoof.expand({1000, 1000}, 64, 128);
    check(std::none_of(northWestPorts.frontier.begin(),
                       northWestPorts.frontier.end(),
                       [](Point p) { return p.x == 996 && p.y == 988; }),
          "A northwest roof alone does not replace the primary west corner");
    LocalSpace zeroDepthSouthEastWall(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{2, 1001, 1050, 1100},
                                    {1, 1048, 1020, 1100}});
    auto earlySouthEast = zeroDepthSouthEastWall.expand({1000, 1000});
    auto earlyReturn = std::find_if(
        earlySouthEast.frontier.begin(), earlySouthEast.frontier.end(),
        [](Point p) { return p.x == 1047 && p.y == 1000; });
    auto earlyEndpoint = std::find_if(
        earlySouthEast.frontier.begin(), earlySouthEast.frontier.end(),
        [](Point p) { return p.x == 1047 && p.y == 1019; });
    auto earlyCorner = std::find_if(
        earlySouthEast.frontier.begin(), earlySouthEast.frontier.end(),
        [](Point p) { return p.x == 1047 && p.y == 1064; });
    check(earlyReturn != earlySouthEast.frontier.end() &&
              earlyEndpoint != earlySouthEast.frontier.end() &&
              earlyCorner != earlySouthEast.frontier.end() &&
              earlyReturn < earlyEndpoint && earlyEndpoint < earlyCorner,
          "A southeast side-wall return precedes its endpoints and corner");
    LocalSpace clippedSouthEastRoof(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{1, 1065, 999, 1007, 1, 1},
                                    {2, 1042, 998, 1014, 1, 1}});
    auto southEastRoof = clippedSouthEastRoof.expand({1000, 1000});
    check(std::none_of(southEastRoof.frontier.begin(),
                       southEastRoof.frontier.end(),
                       [](Point p) { return p.x == 1015 && p.y == 1008; }),
          "A positive southeast roof alone does not expose a side-wall return");
    LocalSpace horizonSouthEastRoof(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{2, 1057, 1050, 1082, -1, -1},
                                    {2, 1065, 999, 1007, 1, 1},
                                    {1, 1050, 1057, 1065, -1, -1}});
    auto horizonSouthEast = horizonSouthEastRoof.expand({1000, 1000});
    check(std::none_of(horizonSouthEast.frontier.begin(),
                       horizonSouthEast.frontier.end(),
                       [](Point p) { return p.x == 1008 && p.y == 1056; }),
          "A southeast roof at the horizon still requires a side-wall return");
    LocalSpace zeroHeightStrip(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{2, 1001, 1050, 1100},
                                    {1, 1048, 1001, 1100}});
    auto zeroHeightPorts = zeroHeightStrip.expand({1000, 1000}, 64, 50);
    check(std::any_of(zeroHeightPorts.frontier.begin(),
                      zeroHeightPorts.frontier.end(),
                      [](Point p) { return p.x == 1047 && p.y == 1000; }),
          "A zero-height southeast sweep retains an actual side-wall return");
    LocalSpace clippedDiagonalEnd(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{2, 1010, 926, 942, 1, 1},
                                    {2, 1042, 998, 1006, 1, 1},
                                    {3, 998, 1034, 1042, -1, -1}});
    auto diagonalEnd = clippedDiagonalEnd.expand({1000, 1000}, 64, 64);
    auto axialEnd =
        std::find_if(diagonalEnd.frontier.begin(), diagonalEnd.frontier.end(),
                     [](Point p) { return p.x == 1000 && p.y == 1041; });
    check(axialEnd != diagonalEnd.frontier.end() &&
              (diagonalEnd.allowed[std::size_t(axialEnd -
                                               diagonalEnd.frontier.begin())] &
               100) == 100,
          "A diagonal-only secondary strip retains its prospective axial exit");
    LocalSpace zeroHeightRoofOnly(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{2, 1001, 1050, 1100},
                                    {2, 1049, 990, 1010}});
    auto roofOnlyPorts = zeroHeightRoofOnly.expand({1000, 1000});
    check(std::none_of(roofOnlyPorts.frontier.begin(),
                       roofOnlyPorts.frontier.end(),
                       [](Point p) { return p.x == 1011 && p.y == 1000; }),
          "A zero-height southeast roof alone does not expose a return");
    LocalSpace zeroHeightNorthEastRoof(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{0, 999, 1058, 1074, -1, -1},
                                    {0, 991, 999, 1015, 1, 1}});
    auto northEastRoof = zeroHeightNorthEastRoof.expand({1000, 1000}, 64, 17);
    auto northEastCorner = std::find_if(
        northEastRoof.frontier.begin(), northEastRoof.frontier.end(),
        [](Point p) { return p.x == 1016 && p.y == 992; });
    auto northEastReturn = std::find_if(
        northEastRoof.frontier.begin(), northEastRoof.frontier.end(),
        [](Point p) { return p.x == 1016 && p.y == 1000; });
    check(northEastCorner != northEastRoof.frontier.end() &&
              northEastReturn != northEastRoof.frontier.end() &&
              northEastCorner < northEastReturn,
          "A zero-depth northeast roof discovers its corner before its return");
    LocalSpace collapsedSouthEast(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{0, 999, 985, 1001, -1, -1},
                                    {1, 1001, 999, 1007, 1, 1},
                                    {2, 1042, 961, 1014, -1, 1}});
    auto southEastEnd = collapsedSouthEast.expand({1000, 1000}, 64, 179);
    auto physicalEnd =
        std::find_if(southEastEnd.frontier.begin(), southEastEnd.frontier.end(),
                     [](Point p) { return p.x == 1000 && p.y == 1041; });
    const std::vector<PixelBox> exploredRoof{{984, 1024, 1048, 1041}};
    check(physicalEnd != southEastEnd.frontier.end() &&
              (southEastEnd.allowed[std::size_t(
                   physicalEnd - southEastEnd.frontier.begin())] &
               unexploredLocalDirections({1000, 1041}, exploredRoof)) == 0,
          "A collapsed southeast corner cannot open through an explored roof");
    LocalSpace cardinalWallEnd(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{2, 1009, 999, 1015, 1, 1},
                                    {3, 999, 1001, 1009, -1, -1}});
    auto cardinalEnd = cardinalWallEnd.expand({1000, 1000}, 64, 4);
    auto lateralExit =
        std::find_if(cardinalEnd.frontier.begin(), cardinalEnd.frontier.end(),
                     [](Point p) { return p.x == 1000 && p.y == 1008; });
    check(lateralExit != cardinalEnd.frontier.end() &&
              cardinalEnd.allowed[std::size_t(
                  lateralExit - cardinalEnd.frontier.begin())] == 2,
          "Collapsed quadrants preserve the cardinal wall end's lateral exit");
    LocalSpace outsideCardinalWallEnd(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{0, 991, 985, 1015, 1, 1},
                                    {3, 999, 993, 1001, -1, 1}});
    auto outsideEnd = outsideCardinalWallEnd.expand({1000, 1000}, 64, 1);
    auto outsideExit =
        std::find_if(outsideEnd.frontier.begin(), outsideEnd.frontier.end(),
                     [](Point p) { return p.x == 1000 && p.y == 992; });
    check(outsideExit != outsideEnd.frontier.end() &&
              outsideEnd.allowed[std::size_t(outsideExit -
                                             outsideEnd.frontier.begin())] == 8,
          "A cardinal endpoint beyond its side wall keeps the forward exit");
    LocalSpace outsideEastCardinalWallEnd(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{0, 991, 985, 1015, 1, 1},
                                    {1, 1001, 993, 1001, -1, 1}});
    auto outsideEastEnd =
        outsideEastCardinalWallEnd.expand({1000, 1000}, 64, 1);
    auto outsideEastExit = std::find_if(
        outsideEastEnd.frontier.begin(), outsideEastEnd.frontier.end(),
        [](Point p) { return p.x == 1000 && p.y == 992; });
    check(outsideEastExit != outsideEastEnd.frontier.end() &&
              !(outsideEastEnd.allowed[std::size_t(
                    outsideEastExit - outsideEastEnd.frontier.begin())] &
                2),
          "A collapsed northeast endpoint retains its escape-side restriction");
    LocalSpace zeroWidthSouthWest(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{3, 991, 999, 1007, 1, 1},
                                    {3, 999, 1050, 1058, -1, -1},
                                    {2, 1058, 999, 1007, 1, 1}});
    auto southWestEnd = zeroWidthSouthWest.expand({1000, 1000}, 64, 72);
    auto southWestCorner =
        std::find_if(southWestEnd.frontier.begin(), southWestEnd.frontier.end(),
                     [](Point p) { return p.x == 1000 && p.y == 1057; });
    check(southWestCorner != southWestEnd.frontier.end() &&
              southWestEnd.allowed[std::size_t(
                  southWestCorner - southWestEnd.frontier.begin())] == 34,
          "A clipped zero-width strip opens forward escape directions only");
    LocalSpace zeroWidthNorthWest(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{0, 990, 983, 1015, 1, 1},
                                    {3, 999, 993, 1001, -1, -1},
                                    {2, 1001, 999, 1007, 1, 1}});
    auto zeroWidthEnd = zeroWidthNorthWest.expand({1000, 1000}, 64, 236);
    auto zeroWidthCorner =
        std::find_if(zeroWidthEnd.frontier.begin(), zeroWidthEnd.frontier.end(),
                     [](Point p) { return p.x == 1000 && p.y == 991; });
    check(zeroWidthCorner != zeroWidthEnd.frontier.end() &&
              (zeroWidthEnd.allowed[std::size_t(
                   zeroWidthCorner - zeroWidthEnd.frontier.begin())] &
               16),
          "A zero-width diagonal strip keeps its prospective axial exit");
    LocalSpace zeroHeightWestRoofOnly(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{2, 1001, 900, 950}, {2, 1049, 990, 1010}});
    auto westRoofPorts = zeroHeightWestRoofOnly.expand({1000, 1000});
    check(std::any_of(westRoofPorts.frontier.begin(),
                      westRoofPorts.frontier.end(),
                      [](Point p) { return p.x == 989 && p.y == 1000; }),
          "A zero-height southwest roof retains its return");
    LocalSpace zeroHeightWestStrip(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{2, 1001, 900, 950}, {3, 952, 1001, 1100}});
    auto westStripPorts = zeroHeightWestStrip.expand({1000, 1000});
    check(std::any_of(westStripPorts.frontier.begin(),
                      westStripPorts.frontier.end(),
                      [](Point p) { return p.x == 953 && p.y == 1000; }),
          "A zero-height southwest sweep retains its strip return");
    LocalSpace zeroHeightRoof(
        {0, 0, 2047, 2047}, {},
        std::vector<NavigationWall>{{0, 999, 1050, 1100}, {1, 1048, 900, 999}});
    auto zeroHeightRoofPorts = zeroHeightRoof.expand({1000, 1000});
    check(std::any_of(zeroHeightRoofPorts.frontier.begin(),
                      zeroHeightRoofPorts.frontier.end(),
                      [](Point p) { return p.x == 1047 && p.y == 1000; }),
          "A zero-height north sweep retains its roof return");
    LocalSpace horizonRoof(
        {0, 0, 2047, 2015}, {},
        std::vector<NavigationWall>{{0, 935, 944, 1015}, {1, 1066, 986, 1040}});
    auto horizonRoofPorts = horizonRoof.expand({1008, 1000}, 64, 179);
    auto roofReturn = std::find_if(
        horizonRoofPorts.frontier.begin(), horizonRoofPorts.frontier.end(),
        [](Point p) { return p.x == 1016 && p.y == 985; });
    check(roofReturn != horizonRoofPorts.frontier.end() &&
              horizonRoofPorts.allowed[std::size_t(
                  roofReturn - horizonRoofPorts.frontier.begin())] == 16,
          "A roof crossing the search horizon exposes a diagonal return");
    auto returnCornerPorts = returnCorner.expand({1024, 1024});
    auto primaryPort = std::find_if(
        returnCornerPorts.frontier.begin(), returnCornerPorts.frontier.end(),
        [](Point p) { return p.x == 1022 && p.y == 1004; });
    check(primaryPort != returnCornerPorts.frontier.end() &&
              returnCornerPorts.allowed[std::size_t(
                  primaryPort - returnCornerPorts.frontier.begin())] == 17,
          "A turn-back opportunity preserves an existing forward corner port");
    for (auto offset :
         {Point{0, 0}, Point{-32, 32}, Point{-64, 64}, Point{-64, 96},
          Point{-32, 128}, Point{-64, 160}, Point{-32, 192}})
      staggeredColumn.push_back({std::uint32_t(staggeredColumn.size()),
                                 {1184 + offset.x, 1072 + offset.y},
                                 {32, 16, 31, 15}});
    auto staggered = groundSpace(terrain, contours, footprint, staggeredColumn);
    auto returned = staggered.expand({1195, 1172}, 64, (1U << 3) | (1U << 7));
    auto returnPort =
        std::find_if(returned.frontier.begin(), returned.frontier.end(),
                     [](Point p) { return p.x == 1195 && p.y == 1131; });
    check(returnPort != returned.frontier.end() &&
              returned.allowed[std::size_t(returnPort -
                                           returned.frontier.begin())] ==
                  ((1U << 1) | (1U << 5)),
          "A north sweep junction retains its eastward turn-back directions");
    auto staggeredRoute =
        localGroundRoute(staggered, regions, {1164, 1227}, {1184, 1072},
                         PixelBox{1140, 1044, 1227, 1099});
    const std::vector<Point> expectedStaggered{
        {1195, 1227}, {1195, 1172}, {1195, 1108}, {1195, 1099}};
    check(staggeredRoute.size() == expectedStaggered.size() &&
              std::equal(
                  staggeredRoute.begin(), staggeredRoute.end(),
                  expectedStaggered.begin(),
                  [](Point a, Point b) { return a.x == b.x && a.y == b.y; }),
          "Turn-back expansion preserves the staggered column's ordered route");
    {
      LocalSpace roofShadow({0, 0, 295, 495},
                            {{0, 0, 79, 51}, {78, 78, 108, 108}}, {}, 0);
      auto exposed = roofShadow.expand({66, 84}, 64, 179);
      auto corner = std::find_if(exposed.frontier.begin(), exposed.frontier.end(),
                                [](Point p) { return p.x == 77 && p.y == 52; });
      check(corner != exposed.frontier.end() &&
                exposed.allowed[std::size_t(corner - exposed.frontier.begin())] ==
                    ((1U << 0) | (1U << 5)),
            "A clipped roof shadow retains its northward search continuation");
      auto continuation = roofShadow.expand({77, 52}, 64, 33);
      check(std::none_of(continuation.frontier.begin(), continuation.frontier.end(),
                         [](Point p) { return p.x == 80 && p.y == 52; }),
            "A blocked cardinal face does not open an adjacent empty quadrant");
    }
    {
      GroundGeometry departureGeometry{{0, 0, 1000, 1000}, {8, 4, 7, 11}, {}, {}};
      const std::array<CollisionBody, 1> body{{{1, {100, 100}, {11, 11, 11, 11}}}};
      auto original = departureGeometry.withBodies(body);
      const std::array<Point, 4> faces{{{82, 100}, {119, 100}, {100, 78}, {100, 115}}};
      for (auto origin : faces) {
        const PixelBox point{origin.x, origin.y, origin.x, origin.y};
        check(!original.clear(point), "Ordinary body geometry retains contact faces");
        auto departing = departureGeometry.withBodies(body, origin);
        check(departing.clear(point) && !departing.clear({100, 100, 100, 100}),
              "Boundary departure opens its starting face while retaining the body interior");
        departing.expand(origin, 32);
      }
      auto inside = departureGeometry.withBodies(body, Point{100, 100});
      check(!inside.clear({100, 100, 100, 100}),
            "A departure inside a body cannot open its interior");
      departureGeometry.obstacles.push_back({70, 90, 82, 110});
      auto terrainBlocked = departureGeometry.withBodies(body, Point{82, 100});
      check(!terrainBlocked.clear({82, 100, 82, 100}),
            "A body departure cannot remove an overlapping terrain obstacle");
    }
    {
      GroundGeometry contactGeometry{{0, 0, 2047, 2015}, {8, 4, 7, 11}, {}, {}};
      const std::array<CollisionBody, 3> bodies{{
          {10, {208, 792}, {49, 32, 49, 32}},
          {11, {269, 836}, {11, 11, 11, 11}},
          {12, {192, 904}, {36, 28, 40, 18}}}};
      const Point source{251, 829};
      auto touching = contactGeometry.withBodies(bodies, source);
      auto route = planGroundRoute(touching, regions, source, {289, 248});
      check(route.points.size() >= 2 &&
                route.points[0].x == 251 && route.points[0].y == 893 &&
                route.points[1].x == 289 && route.points[1].y == 852,
            "An initial body contact reaches the local horizon before its corner return");
      auto exteriorBodies = bodies;
      ++exteriorBodies[1].position.x;
      auto exterior = contactGeometry.withBodies(exteriorBodies, source);
      auto exteriorRoute = planGroundRoute(exterior, regions, source, {289, 248});
      check(!exteriorRoute.points.empty() &&
                exteriorRoute.points[0].x == 251 &&
                exteriorRoute.points[0].y == 852,
            "A one-pixel exterior departure opens the nearby corner immediately");
    }
    std::cout << "PASS: geometric local expansion and obstacle avoidance\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
