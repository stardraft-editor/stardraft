// stardraft-cli: the pathing check, the campaigns and the search in the classic triggers, as JSON (included by main.cpp
// after map_more.inc; it uses main.cpp's helpers).
//
//   get <map> pathing                      the nooks and crannies of the ground (Scenario > Check pathing)
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
                if (!c.add(upath(op.str("file")))) fail("the map is in the campaign already, or not a file");
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
