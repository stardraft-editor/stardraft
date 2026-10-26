// stardraft-cli: the switch names, the map strings, the sounds, the spray and decorate tools, the replace and map revealer
// edits, and the start locations, as JSON (included by main.cpp, inside its anonymous namespace; it uses main.cpp's
// helpers). The symmetry and the resource distance are apply options (cmdApply), not operations.

// --- Reading ---------------------------------------------------------------------------------------------------------

// `get` parts: switches (the names), strings (the map string table), sounds (the sound files the map names by path).
// known: false when `what` is not one of them.
// `files`: the map's files when the map is open (the working step is a scenario only): else the editor's own.
Json moreGet(const Editor& ed, const std::string& what, bool& known, const std::vector<ArchiveFile>* files) {
    known = true;
    Json a = jarr();
    if (what == "switches") {
        for (int i = 0; i < 256; ++i) {
            const std::string name = ed.switchName(i);
            if (!name.empty()) a.items.push_back(Obj().add("switch", i + 1).add("name", name));
        }
    } else if (what == "strings") {
        for (uint32_t id = 1; id <= ed.strings().size(); ++id) {
            const std::string& text = ed.strings().get(id);
            if (!text.empty()) a.items.push_back(Obj().add("id", int(id)).add("text", text));
        }
    } else if (what == "sounds") {
        for (const std::string& path : ed.soundReferences()) a.items.push_back(Obj().add("path", path).add("kind", "game"));
        for (const ArchiveFile& f : files ? *files : ed.archiveFiles())
            if (lower(f.name).size() > 4 && lower(f.name).compare(lower(f.name).size() - 4, 4, ".wav") == 0)
                a.items.push_back(Obj().add("path", f.name).add("kind", "file").add("bytes", double(f.data.size())));
    } else {
        known = false;
    }
    return a;
}

// --- Editing ---------------------------------------------------------------------------------------------------------

// The spray and decorate levels: a few, some or many doodads (the GUI's Edit > Decorate: spacings of 9, 6 and 3 tiles).
int decorateSpacing(const Json& op) {
    if (op.get("spacing")) return std::max(1, int(num(op, "spacing")));
    const std::string level = lower(op.str("level", "some"));
    if (level == "few") return 9;
    if (level == "some") return 6;
    if (level == "many") return 3;
    fail("level must be few, some or many (or give \"spacing\" in tiles)");
}

// The doodads that Decorate uses when none are given: the palette's doodads that take tiles (as the GUI's default).
std::vector<uint16_t> decorateDoodads(const Editor& ed) {
    std::vector<uint16_t> ids;
    for (const DoodadType& t : ed.doodadTypes().all())
        if (t.inPalette && t.needsTiles()) ids.push_back(t.id);
    return ids;
}

// A text field: it must be given and be text. (An empty text is a value only when it is written as "".)
std::string textOf(const Json& op, const char* k) {
    const Json& v = need(op, k);
    if (v.type != Json::Type::String) fail(std::string("\"") + k + "\" must be text");
    return v.s;
}
// A list field: it must be given and be a list.
const std::vector<Json>& arrayOf(const Json& op, const char* k) {
    const Json& v = need(op, k);
    if (v.type != Json::Type::Array) fail(std::string("\"") + k + "\" must be a list");
    return v.items;
}
// A sprite id (sprites.dat, the kinds of sprites the map can hold).
uint16_t spriteId(double v, const char* k) {
    const DatSchema* sc = DatSchema::find("sprites");
    if (v < 0 || !sc || v >= sc->entries) fail(std::string("\"") + k + "\": no sprite " + std::to_string(int(v)));
    return uint16_t(v);
}

std::vector<uint16_t> idList(const Editor& ed, const Json& op, const char* k) {
    std::vector<uint16_t> ids;
    for (const Json& v : arrayOf(op, k)) ids.push_back(unitTypeId(ed, v, k));
    return ids;
}

// The doodad ids of an op: "doodads" as ids (get doodad-types). Each must be one of the map's doodad types.
std::vector<uint16_t> doodadIds(const Editor& ed, const Json& op, const char* k) {
    std::vector<uint16_t> ids;
    for (double v : nums(need(op, k), k)) {
        if (!ed.doodadTypes().find(uint16_t(v))) fail(std::string("\"") + k + "\": no doodad type " + std::to_string(int(v)));
        ids.push_back(uint16_t(v));
    }
    return ids;
}

std::optional<Json> applyMoreOp(Editor& ed, const std::string& name, const Json& op) {
    Obj r;
    if (name == "set_switch_name") {
        const int s = int(num(op, "switch"));
        if (s < 1 || s > 256) fail("\"switch\" must be 1-256");
        return r.add("changed", ed.setSwitchName(s - 1, textOf(op, "name")));
    }
    if (name == "set_string") {
        const uint32_t id = uint32_t(num(op, "string"));
        if (id < 1 || id > ed.strings().size()) fail("no map string " + std::to_string(id) + " (see `get strings`)");
        return r.add("changed", ed.setMapString(id, textOf(op, "text")));
    }
    if (name == "add_sound") {
        const std::string path = textOf(op, "path");
        if (path.empty()) fail("missing \"path\"");
        const size_t before = ed.soundReferences().size();
        ed.addSoundReference(path);
        return r.add("added", ed.soundReferences().size() != before);  // (a file of the map is not added: it is there already)
    }
    if (name == "remove_sound") return r.add("removed", ed.removeSoundReference(textOf(op, "path")));
    if (name == "decorate") {
        // Doodads where they fit the terrain, over the area (default: the map), with the given doodads (default: the palette's).
        const std::vector<uint16_t> ids = op.get("doodads") ? doodadIds(ed, op, "doodads") : decorateDoodads(ed);
        const Rect a = op.get("area") ? area(op, "area").normalized() : Rect{0, 0, ed.width() * 32, ed.height() * 32};
        const int n = ed.decorate(ids, a, decorateSpacing(op), uint32_t(num(op, "seed", 1)));
        return r.add("placed", n).add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "spray_units") {
        const std::vector<uint16_t> types = idList(ed, op, "types");
        const int n = ed.sprayUnits(types, uint8_t(player(op, "player", 12)), int(num(op, "x")), int(num(op, "y")), int(num(op, "radius")),
                                    int(num(op, "count")), uint32_t(num(op, "seed", 1)));
        return r.add("placed", n);
    }
    if (name == "spray_doodads") {
        const int n = ed.sprayDoodads(doodadIds(ed, op, "doodads"), int(num(op, "x")), int(num(op, "y")), int(num(op, "radius")), int(num(op, "count")),
                                      uint32_t(num(op, "seed", 1)), op.boolean("check_terrain", true));
        return r.add("placed", n);
    }
    if (name == "spray_sprites") {
        std::vector<uint16_t> ids;
        for (double v : nums(need(op, "sprites"), "sprites")) ids.push_back(spriteId(v, "sprites"));
        const int n = ed.spraySprites(ids, uint8_t(player(op, "player", 12)), float(num(op, "x")), float(num(op, "y")), int(num(op, "radius")),
                                      int(num(op, "count")), uint32_t(num(op, "seed", 1)), int(num(op, "grid", 0)));
        return r.add("placed", n);
    }
    if (name == "spray_terrain") {
        if (!ed.canPaintTerrain()) fail("the terrain of this map cannot be painted (it has no valid ISOM)");
        const uint16_t t = terrainId(ed, need(op, "terrain"), "terrain");
        ed.sprayTerrain(int(num(op, "x")), int(num(op, "y")), t, brush(op), int(num(op, "dabs", 1)), uint32_t(num(op, "seed", 1)));
        return r.add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "reroll_terrain") {
        if (!ed.canPaintTerrain()) fail("the terrain of this map cannot be painted (it has no valid ISOM)");
        const int n = ed.rerollTerrain(int(num(op, "x")), int(num(op, "y")), brush(op), uint32_t(num(op, "seed", 1)));
        return r.add("changed", n).add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "replace_doodads") {
        const std::vector<int> d = indices(op, "doodads", ed.doodads().size());
        const uint16_t to = uint16_t(num(op, "to"));
        if (!ed.doodadTypes().find(to)) fail("no doodad type " + std::to_string(int(to)));
        return r.add("replaced", ed.replaceDoodads(d, to, op.boolean("check_terrain", true)));
    }
    if (name == "replace_sprites") {
        const std::vector<int> s = indices(op, "sprites", ed.sprites().size());
        return r.add("replaced", ed.replaceSprites(s, spriteId(num(op, "to"), "to")));
    }
    if (name == "place_map_revealers") {
        std::vector<uint8_t> owners;
        for (double p : nums(need(op, "players"), "players")) {
            if (p < 1 || p > 8) fail("\"players\" must be 1-8");
            owners.push_back(uint8_t(int(p) - 1));
        }
        return r.add("placed", ed.placeMapRevealers(owners, int(num(op, "spacing", 8))));
    }
    if (name == "remove_map_revealers") return r.add("removed", ed.removeMapRevealers());
    if (name == "place_start_locations") return r.add("placed", ed.placeStartLocations());
    if (name == "copy_players") {  // the Players page's copy: the players and forces of another map (one undo step)
        const std::string from = textOf(op, "from");
        Editor other(ed.gameData());
        std::string err;
        if (!other.open(upath(from), err)) fail(err);
        std::vector<Editor::Player> players;
        std::vector<Editor::Force> forces;
        for (int p = 0; p < 8; ++p) players.push_back(other.player(p));
        for (int f = 0; f < 4; ++f) forces.push_back(other.force(f));
        ed.setPlayersAndForces(players, forces);
        return r.add("players", 8).add("forces", 4);
    }
    if (name == "link_units") {
        const std::vector<int> u = unitIndices(ed, op);
        if (u.size() != 2) fail("link_units needs two units");
        if (!ed.linkUnits(u[0], u[1])) fail("these units cannot be linked (they are linked already, or not an add-on pair)");
        return r;
    }
    return std::nullopt;
}
