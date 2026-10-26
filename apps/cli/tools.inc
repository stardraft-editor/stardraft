// stardraft-cli: the pathing check, the campaigns and the search in the classic triggers, as JSON (included by main.cpp
// after map_more.inc; it uses main.cpp's helpers).
//
//   get <map> pathing                      the nooks and crannies of the ground (Scenario > Check pathing)
//   get <map> distances                    the ground distances between the start locations and the bases
//   get <map> walk                         the walkable ground and its level, one character per minitile
//   get <map> triggers --find <text>       the classic triggers that have the text in a condition or an action
//   campaign-new <folder> <name>           a campaign (campaign.json in the folder)
//   campaign <folder>                      its name and maps
//   campaign-apply <folder> <ops>          add_map (file), set_name (name); saved when all of them work

const char* pathingKindName(PathingProblemKind k) {
    switch (k) {
    case PathingProblemKind::TooManyRegions: return "too_many_regions";
    case PathingProblemKind::Pocket: return "pocket";
    case PathingProblemKind::Gap: return "gap";
    case PathingProblemKind::SmallUnitsOnly: return "small_units_only";
    case PathingProblemKind::ClosedPassage: return "closed_passage";
    case PathingProblemKind::SmallRegion: return "small_region";
    }
    return "unknown";
}

// The pathing check of the map: its regions, its problems (a box in minitiles' map pixels, and the fix of a pocket when
// it has one) and the count of each kind.
Json pathingJson(const Editor& ed) {
    const PathingRegions regions = computePathingRegions(ed.terrain(), ed.tileset());
    const PathingCheck c = checkPathing(regions, ed.terrain(), ed.tileset(), ed.units());
    Json a = jarr();
    for (const PathingProblem& p : c.problems) {
        Obj o;
        o.add("kind", pathingKindName(p.kind)).add("x", p.x).add("y", p.y).add("box", jrect(p.x0, p.y0, p.x1, p.y1));
        if (p.fixTileX >= 0) o.add("fix", Obj().add("tile_x", p.fixTileX).add("tile_y", p.fixTileY));
        a.items.push_back(o);
    }
    return Obj()
        .add("regions", int(regions.regions.size()))
        .add("connected_areas", int(regions.groups))
        .add("problems", a)
        .add("more", double(c.more))
        .add("pockets", c.pockets)
        .add("gaps", c.gaps)
        .add("small_units_only", c.narrow)
        .add("closed", c.closed)
        .add("small_regions", c.smallRegions);
}

// --- Distances ------------------------------------------------------------------------------------------------------

constexpr uint16_t kStartLocationType = 214, kGeyserType = 188;
bool isMineralField(uint16_t t) { return t >= 176 && t <= 178; }

// The bases: the mineral fields and geysers that are less than 320 px from each other, each group with its center.
struct ResourceBase {
    int x = 0, y = 0, minerals = 0, geysers = 0;
    double resources = 0;
};
std::vector<ResourceBase> resourceBases(const Editor& ed) {
    std::vector<const Unit*> res;
    for (const Unit& u : ed.units())
        if (isMineralField(u.type) || u.type == kGeyserType) res.push_back(&u);
    std::vector<int> group(res.size(), -1);
    int groups = 0;
    for (size_t i = 0; i < res.size(); ++i) {
        if (group[i] >= 0) continue;
        group[i] = groups;
        std::vector<size_t> todo{i};
        while (!todo.empty()) {
            const size_t k = todo.back();
            todo.pop_back();
            for (size_t j = 0; j < res.size(); ++j)
                if (group[j] < 0 && std::hypot(double(res[j]->x) - res[k]->x, double(res[j]->y) - res[k]->y) < 320) group[j] = groups, todo.push_back(j);
        }
        ++groups;
    }
    std::vector<ResourceBase> bases(static_cast<size_t>(groups));
    std::vector<int> count(size_t(groups), 0);
    for (size_t i = 0; i < res.size(); ++i) {
        ResourceBase& b = bases[size_t(group[i])];
        b.x += res[i]->x, b.y += res[i]->y, ++count[size_t(group[i])];
        (isMineralField(res[i]->type) ? b.minerals : b.geysers)++;
        b.resources += res[i]->resources;
    }
    for (size_t g = 0; g < bases.size(); ++g) bases[g].x /= count[g], bases[g].y /= count[g];
    return bases;
}

// The ground distance (px, along walkable minitiles: 8-way, a diagonal step only past two walkable sides) from (x, y)
// to each minitile, -1 where the ground does not reach. The start is the walkable minitile nearest (x, y), within 64 px.
std::vector<float> groundDistances(const PathingRegions& p, int x, int y) {
    const int w = p.walkW(), h = p.walkH();
    std::vector<float> dist(size_t(w) * h, -1.f);
    int sx = -1, sy = -1;
    for (int r = 0; r <= 8 && sx < 0; ++r)
        for (int dy = -r; dy <= r && sx < 0; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (std::max(std::abs(dx), std::abs(dy)) == r && p.walkable(x / 8 + dx, y / 8 + dy)) {
                    sx = x / 8 + dx, sy = y / 8 + dy;
                    break;
                }
    if (sx < 0) return dist;
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q;
    dist[size_t(sy) * w + sx] = 0;
    q.push({0.f, sy * w + sx});
    while (!q.empty()) {
        const auto [d, at] = q.top();
        q.pop();
        if (d > dist[size_t(at)]) continue;
        const int ax = at % w, ay = at / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int nx = ax + dx, ny = ay + dy;
                if (!p.walkable(nx, ny) || (dx && dy && (!p.walkable(ax + dx, ay) || !p.walkable(ax, ay + dy)))) continue;
                const float nd = d + (dx && dy ? 11.31f : 8.f);
                float& old = dist[size_t(ny) * w + nx];
                if (old < 0 || nd < old) old = nd, q.push({nd, ny * w + nx});
            }
    }
    return dist;
}

// The distance at (x, y): the nearest minitile within 64 px that the ground reaches, or -1.
double distanceAt(const std::vector<float>& dist, const PathingRegions& p, int x, int y) {
    double best = -1;
    for (int dy = -8; dy <= 8; ++dy)
        for (int dx = -8; dx <= 8; ++dx) {
            const int mx = x / 8 + dx, my = y / 8 + dy;
            if (mx < 0 || my < 0 || mx >= p.walkW() || my >= p.walkH()) continue;
            const float d = dist[size_t(my) * p.walkW() + mx];
            if (d >= 0 && (best < 0 || d + std::hypot(dx, dy) * 8 < best)) best = d + std::hypot(dx, dy) * 8;
        }
    return best;
}

// get <map> distances: the start locations, the bases (resource groups) and, from each start location, the ground
// distance in px to each other start location and to each base (null: the ground does not reach it). Units and
// buildings are not obstacles; doodads and terrain are.
Json distancesJson(const Editor& ed) {
    const PathingRegions regions = computePathingRegions(ed.terrain(), ed.tileset());
    std::vector<const Unit*> starts;
    for (const Unit& u : ed.units())
        if (u.type == kStartLocationType && u.owner < 8) starts.push_back(&u);
    std::sort(starts.begin(), starts.end(), [](const Unit* a, const Unit* b) { return a->owner < b->owner; });
    const std::vector<ResourceBase> bases = resourceBases(ed);
    auto jdist = [](double d) { return d < 0 ? Json() : jnum(double(std::lround(d))); };
    Json js = jarr(), jb = jarr();
    for (size_t i = 0; i < bases.size(); ++i)
        jb.items.push_back(Obj().add("base", int(i + 1)).add("x", bases[i].x).add("y", bases[i].y).add("minerals", bases[i].minerals)
            .add("geysers", bases[i].geysers).add("resources", bases[i].resources));
    for (const Unit* s : starts) {
        const std::vector<float> dist = groundDistances(regions, s->x, s->y);
        Json toStarts = jarr(), toBases = jarr();
        for (const Unit* o : starts)
            if (o != s) toStarts.items.push_back(Obj().add("player", o->owner + 1).add("distance", jdist(distanceAt(dist, regions, o->x, o->y))));
        for (size_t i = 0; i < bases.size(); ++i)
            toBases.items.push_back(Obj().add("base", int(i + 1)).add("distance", jdist(distanceAt(dist, regions, bases[i].x, bases[i].y))));
        js.items.push_back(Obj().add("player", s->owner + 1).add("x", s->x).add("y", s->y).add("to_start_locations", toStarts).add("to_bases", toBases));
    }
    return Obj().add("bases", jb).add("start_locations", js);
}

// get <map> walk: one row of text per minitile row (8 px), one character per minitile: 0, 1, 2 walkable low, middle
// or high ground; # not walkable; x walkable ground under a building or a resource (a static obstacle).
Json walkJson(const Editor& ed) {
    const PathingInput in{&ed.terrain(), &ed.tileset(), &ed.unitTypes(), &ed.units(), &ed.sprites()};
    const WalkGrid g = buildWalkGrid(in);
    Json rows = jarr();
    for (int y = 0; y < g.height; ++y) {
        std::string row(size_t(g.width), '#');
        for (int x = 0; x < g.width; ++x) {
            const uint8_t c = g.cells[size_t(y) * g.width + x];
            if (c & WalkGrid::kTerrainWalkable) row[size_t(x)] = (c & WalkGrid::kBlocked) ? 'x' : char('0' + g.elevation(x, y));
        }
        rows.items.push_back(jstr(row));
    }
    return Obj().add("cell", 8).add("width", g.width).add("height", g.height).add("rows", rows);
}

// The classic triggers whose JSON has the text (any case). Each keeps its index, so it can be named in an op.
Json filterTriggers(const Json& all, const std::string& text) {
    const std::string want = lower(text);
    Json a = jarr();
    for (const Json& t : all.items)
        if (lower(jsonText(t)).find(want) != std::string::npos) a.items.push_back(t);
    return a;
}

// --- Campaigns (campaign.h) ------------------------------------------------------------------------------------------

Json campaignJson(const Campaign& c) {
    Json maps = jarr();
    for (size_t i = 0; i < c.maps.size(); ++i) maps.items.push_back(Obj().add("map", int(i + 1)).add("file", ustr(c.mapFile(i))));
    return Obj().add("folder", ustr(c.folder)).add("name", c.name).add("maps", maps);
}

Json cmdCampaignNew(const Args& args) {
    if (args.positional.size() < 2) fail("usage: stardraft-cli campaign-new <folder> <name>");
    Campaign c;
    std::string err;
    if (!c.create(upath(args.positional[0]), args.positional[1], err)) fail(err);
    return campaignJson(c);
}

Json cmdCampaign(const Args& args) {
    if (args.positional.empty()) fail("campaign needs a folder (stardraft-cli help)");
    Campaign c;
    std::string err;
    if (!c.load(upath(args.positional[0]), err)) fail(err);
    return campaignJson(c);
}

Json cmdCampaignApply(const Args& args) {
    if (args.positional.size() < 2) fail("usage: stardraft-cli campaign-apply <folder> <ops.json | '[...]'>");
    Campaign c;
    std::string err;
    if (!c.load(upath(args.positional[0]), err)) fail(err);
    const std::string src = args.positional[1];
    const bool inlineText = !src.empty() && (src[0] == '[' || src[0] == '{');
    if (src == "-") fail("campaign-apply takes its operations inline or from a file");
    const std::string text = inlineText ? src : readText(upath(src));
    const Json doc = parseJson(text, "the operations");
    const Json* ops = doc.type == Json::Type::Array ? &doc : doc.get("ops");
    if (!ops || ops->type != Json::Type::Array) fail("the operations must be a list, or an object with \"ops\": [...]");
    for (size_t i = 0; i < ops->items.size(); ++i) {
        const Json& op = ops->items[i];
        try {
            const std::string name = op.str("op");
            if (name == "add_map") {
                if (op.str("file").empty()) fail("missing \"file\"");
                // A relative path is relative to the working directory, as the command line gives it: the campaign keeps
                // the absolute one (and writes it relative to its folder when the map is inside it).
                const fs::path file = fs::absolute(upath(op.str("file")));
                std::error_code ec;
                if (!fs::is_regular_file(file, ec)) fail("no map file " + ustr(file));
                if (!c.add(file)) fail("the map is in the campaign already");
            } else if (name == "set_name") {
                c.name = textOf(op, "name");
            } else {
                fail("unknown campaign op \"" + name + "\": add_map, set_name");
            }
        } catch (const std::exception& e) {
            fail("op " + std::to_string(i) + " (" + op.str("op", "?") + "): " + e.what() + "; the campaign is not changed");
        }
    }
    if (!c.save(err)) fail(err);
    return campaignJson(c);
}
