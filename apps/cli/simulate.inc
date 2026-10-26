// Experimental (branch experimental-simulation, STARDRAFT_SIMULATION): simulate-mining and simulate-combat, the game
// run by StarDraft's simulation (tools/sim/sim.h). Included by main.cpp.
//
//   stardraft-cli simulate-mining <map> [--workers 9,18,27] [--races tpz] [--gas] [--base x,y] [--minutes 3]
//   stardraft-cli simulate-combat <map> --a "Terran Marine:12" --b "Zerg Zergling:24" [--at x,y] [--gap 256] [--minutes 2]
//
// A unit is its name (as the game names it: "Terran Marine"; a part of it is enough: "marine") or its number. Several
// kinds: "Terran Marine:12,Terran Medic:4".

#include "../../tools/sim/sim.h"
#include "../../tools/sim/sim_data.h"

// The simulator for the map of `t`, with the game's data as the map plays it (its unit settings: sim_data.h).
sim::Simulator& simulatorOf(Session& s, const Target& t) {
    static std::unique_ptr<sim::Simulator> made;
    if (!s.dataOpen) openGameData(s.data, s.args.get("install")), s.dataOpen = true;
    if (t.open) fail("simulate works on a map file (save the open map first)");
    Bytes chk;
    std::string err;
    if (!readMapChk(t.map, chk, err)) fail(err);
    made = sim::createSimulator();
    if (!made) fail("the simulation engine is not in this build yet");
    // (the simulation keeps the reader and reads more files later, a map's tileset: the reader owns the files)
    auto files = std::make_shared<const std::map<std::string, Bytes>>(sim::dataFilesForMap(s.data, Chk::parse(chk)));
    // The simulation asks for game files by their CASC names ("arr/units.dat"...), as they are or in small letters.
    GameData& data = s.data;
    const bool ok = made->init(
        [files, &data](const std::string& name, std::vector<uint8_t>& out) {
            if (const auto it = files->find(lower(name)); it != files->end()) return out = it->second, true;
            std::string e;
            return data.readFile(name, out, e) || data.readFile(lower(name), out, e);
        },
        err);
    if (!ok) fail(err);
    s.timer.step("start the simulation");
    if (!made->setMap(chk, err)) fail(err);
    return *made;
}

int simMinutesToFrames(Session& s, double def) {
    const double m = s.args.has("minutes") ? numbers(s.args.get("minutes"), 1, "--minutes")[0] : def;
    if (!(m > 0 && m <= 60)) fail("--minutes must be more than 0 and at most 60");
    return int(m * 60 * 1000 / 42);
}

Json cmdSimulateMining(Session& s) {
    const Target t = resolve(s.args.positional.at(0));
    sim::Simulator& simulator = simulatorOf(s, t);
    sim::MiningOptions o;
    if (s.args.has("workers")) {
        o.workers.clear();
        std::stringstream in(s.args.get("workers"));
        for (std::string w; std::getline(in, w, ',');)
            if (const int n = std::atoi(w.c_str()); n > 0 && n <= 100) o.workers.push_back(n);
        if (o.workers.empty()) fail("--workers: counts from 1 to 100, for example 9,18,27");
    }
    if (s.args.has("races")) {
        o.races.clear();
        for (char c : lower(s.args.get("races")))
            if (c == 't') o.races.push_back(sim::Terran);
            else if (c == 'p') o.races.push_back(sim::Protoss);
            else if (c == 'z') o.races.push_back(sim::Zerg);
        if (o.races.empty()) fail("--races: letters of t, p and z");
    }
    if (s.args.has("base")) {
        const std::vector<double> b = numbers(s.args.get("base"), 2, "--base");
        o.bases.push_back({int(b[0]), int(b[1])});
    }
    o.gas = s.args.has("gas");
    o.frames = simMinutesToFrames(s, 3);
    std::vector<sim::MiningBase> bases;
    std::string err;
    if (!simulator.mining(o, bases, err)) fail(err);
    s.timer.step("simulate");
    static const char* const kRace[3] = {"zerg", "terran", "protoss"};
    Json list = jarr();
    for (const sim::MiningBase& b : bases) {
        Json fields = jarr(), runs = jarr();
        for (const auto& f : b.fields) {
            Json p = jarr();
            p.items.push_back(jnum(f.first)), p.items.push_back(jnum(f.second));
            fields.items.push_back(p);
        }
        for (const sim::MiningRun& r : b.runs) {
            Json trips = jarr();
            for (double f : r.tripFrames) trips.items.push_back(jnum(std::round(f * 10) / 10));
            runs.items.push_back(Obj()
                                     .add("race", kRace[r.race])
                                     .add("workers", r.workers)
                                     .add("minerals_per_minute", std::round(r.mineralsPerMinute * 10) / 10)
                                     .add("gas_per_minute", std::round(r.gasPerMinute * 10) / 10)
                                     .add("field_trip_frames", trips));
        }
        list.items.push_back(Obj().add("x", b.x).add("y", b.y).add("start_location", b.startLocation).add("fields", fields).add("runs", runs));
    }
    return Obj()
        .add("engine", "StarDraft (Brood War 1.16.1 rules)")
        .add("speed", "Fastest")
        .add("measured_minutes", o.frames * 42.0 / 60000)
        .add("bases", list);
}

// "Terran Marine:12,marine:4,0:3" (name, part of a name or number, then the count).
std::vector<sim::Army> simArmy(const std::string& text, const UnitTypes& types, const std::string& what) {
    std::vector<sim::Army> out;
    std::stringstream in(text);
    for (std::string part; std::getline(in, part, ',');) {
        const size_t colon = part.rfind(':');
        std::string name = part.substr(0, colon);
        const int count = colon == std::string::npos ? 1 : std::atoi(part.c_str() + colon + 1);
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        while (!name.empty() && name.back() == ' ') name.pop_back();
        std::replace(name.begin(), name.end(), '_', ' ');
        int id = -1;
        if (!name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; })) id = std::atoi(name.c_str());
        else if (const auto exact = types.findByName(name)) id = *exact;
        else
            for (int k = 0; k < UnitTypes::kCount && id < 0; ++k)
                if (lower(types.statText(uint32_t(k))).find(lower(name)) != std::string::npos) id = k;
        if (id < 0 || id >= UnitTypes::kCount) fail(what + ": no unit \"" + name + "\"");
        if (count < 1 || count > 200) fail(what + ": a count from 1 to 200 (\"" + part + "\")");
        out.push_back({id, count});
    }
    if (out.empty()) fail(what + " needs units, for example \"Terran Marine:12\"");
    return out;
}

Json cmdSimulateCombat(Session& s) {
    const Target t = resolve(s.args.positional.at(0));
    sim::Simulator& simulator = simulatorOf(s, t);
    UnitTypes types;
    std::string err;
    if (!types.load(s.data, err)) fail(err);
    sim::CombatOptions o;
    o.a = simArmy(s.args.get("a"), types, "--a");
    o.b = simArmy(s.args.get("b"), types, "--b");
    if (s.args.has("at")) {
        const std::vector<double> at = numbers(s.args.get("at"), 2, "--at");
        o.x = int(at[0]), o.y = int(at[1]);
    }
    if (s.args.has("gap")) o.gap = int(numbers(s.args.get("gap"), 1, "--gap")[0]);
    o.frames = simMinutesToFrames(s, 2);
    sim::CombatResult r;
    if (!simulator.combat(o, r, err)) fail(err);
    s.timer.step("simulate");
    auto side = [&](const sim::CombatSide& c) {
        Json left = jarr();
        for (const auto& [type, n] : c.survivors) left.items.push_back(Obj().add("unit", types.statText(uint32_t(type))).add("count", n));
        Json byType = jarr();
        for (const sim::UnitTypeStats& u : c.types)
            byType.items.push_back(Obj()
                                       .add("unit", types.statText(uint32_t(u.unitType)))
                                       .add("started", u.started)
                                       .add("alive", u.alive)
                                       .add("kills", u.kills)
                                       .add("hp", u.hp)
                                       .add("shields", u.shields));
        return Obj()
            .add("units", c.units)
            .add("alive", c.alive)
            .add("hp", c.hp)
            .add("max_hp", c.maxHp)
            .add("shields", c.shields)
            .add("max_shields", c.maxShields)
            .add("kills", c.kills)
            .add("damage_taken", c.damageTaken)
            .add("minerals_lost", c.mineralsLost)
            .add("gas_lost", c.gasLost)
            .add("supply_lost", c.supplyLost / 2.0)
            .add("by_type", byType)
            .add("survivors", left);
    };
    const char* winner = r.helpless >= 0                ? (r.helpless ? "a (b cannot attack)" : "b (a cannot attack)")
                         : r.a.alive && !r.b.alive ? "a"
                         : r.b.alive && !r.a.alive ? "b"
                         : r.a.alive               ? "none (time ran out)"
                                                   : "none (both died)";
    return Obj()
        .add("engine", "StarDraft (Brood War 1.16.1 rules)")
        .add("winner", winner)
        .add("frames", r.frames)
        .add("seconds", std::round(r.frames * 42.0 / 100) / 10)
        .add("timed_out", r.timedOut)
        .add("a", side(r.a))
        .add("b", side(r.b));
}
