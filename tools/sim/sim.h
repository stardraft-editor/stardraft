// Experimental (branch experimental-simulation): StarDraft's own simulation of the game (Brood War 1.16.1 rules) for
// mining and combat tests: the editor runs it live on the map (apps/editor/sdl_simulation.inc), the command line runs
// it to the end (apps/cli/simulate.inc). The engine is written from behaviour descriptions
// (a clean-room process: see docs/dev/SIMULATION.md); it has no code of other projects.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sim {

// A game file by its CASC name ("arr/units.dat", "unit/terran/marine.grp"...): false when there is none.
using FileReader = std::function<bool(const std::string& name, std::vector<uint8_t>& out)>;

enum Race { Zerg = 0, Terran = 1, Protoss = 2 };

struct MiningOptions {
    std::vector<std::pair<int, int>> bases;  // more bases (map pixels), besides the start locations and placed depots
    bool startLocations = true, placedDepots = true;
    std::vector<int> workers = {9, 18, 27};  // mineral workers of each run (0: 1 for each mineral field)
    int workersPerField = 0;                 // 1 or more: that many for each mineral field of a base, in place of `workers`
    std::vector<Race> races = {Terran, Protoss, Zerg};
    int warmupFrames = 720, frames = 4286;  // 30 s, then 3 minutes measured (Fastest)
    bool gas = false;                       // a finished refinery on the nearest geyser, with 3 more workers
    bool mineralClusters = true;            // also a base at each group of mineral fields with none near (clusterBases)
    int gasWorkers = 3;                     // the workers on the refinery when `gas` is on (1 to 3)
    // The race of a start location's base: its player's race in the map (SIDE); a player whose race is picked in the
    // lobby or Random gets a random race (from `seed`). Other bases: races[0]. Off: races[0] everywhere.
    bool mapRaces = false;
    uint32_t seed = 0;                      // (mapRaces: the random races)
    bool continuous = false;                // the editor's mining: it runs until it is stopped (the command line: `frames`)
    // The bases that mine (the editor's Bases list). startPlayers: a bit for each player (0 to 7) whose start location
    // mines. placedDepotMask: a bit for each placed depot that mines, in the map's order of the placed depots that are
    // not near a start location. The bases left out keep their mineral fields (a field does not go to another base).
    unsigned startPlayers = 0xff, placedDepotMask = ~0u;
};
// No base is selected (the editor's Bases list with no check: the game has no base, and that is not an error).
inline bool miningNoBaseSelected(const MiningOptions& o) {
    return (!o.startLocations || o.startPlayers == 0) && (!o.placedDepots || o.placedDepotMask == 0) && o.bases.empty();
}
struct MiningRun {
    Race race = Terran;
    int workers = 0;
    double mineralsPerMinute = 0, gasPerMinute = 0;
    std::vector<double> tripFrames;  // each field's mean round trip (delivery to delivery), 0: no trip measured
    // Each field's minerals left: its amount in the map less the minerals mined from it (the simulation's fields do not
    // run out). Empty: not counted.
    std::vector<int> fieldMinerals;
    bool placed = true;  // false: the base has no room for its depot and workers (no numbers)
    std::vector<double> fieldIncome;    // each field's minerals a game minute after the warm-up (the field mined)
    std::vector<int> fieldCycleFrames;  // each field's last trip of a worker (delivery to delivery) in frames
};
struct MiningBase {
    int x = 0, y = 0;
    bool startLocation = false;
    int player = -1;  // a start location's player (0 to 11); -1: not a start location
    std::vector<std::pair<int, int>> fields;  // the mineral fields near it, nearest first
    std::vector<MiningRun> runs;
};

struct Army {
    int unitType = 0;
    int count = 0;
};
// A unit where it stands (the editor's selection): side 0 or 1 (with byPlayer: its player, 0 to 7).
struct PlacedUnit {
    int unitType = 0, side = 0, x = 0, y = 0;
    // A map unit that does not fight (not selected): it stays where it is, blocks movement, is drawn, is never a
    // target and never attacks, and is not in the result.
    bool passive = false;
};
struct CombatOptions {
    std::vector<Army> a, b;          // side 0 and side 1, at x, y (with gap)
    std::vector<PlacedUnit> placed;  // or: these units where they stand (then a, b, x, y and gap are not used)
    int players[2] = {0, 1};         // the players (0 to 7) of the two sides: their colors
    // Placed units by player: PlacedUnit::side is the player; players of the same team are allies, all others enemies.
    bool byPlayer = false;
    int team[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    // Unit free for all (byPlayer): every fighting placed unit is its own team, an enemy of every other one, also of
    // its own player's (no allies; a unit sees cloaked units only with its own detection). The images keep the
    // players' colours; the result's teams are still one by player.
    bool unitFreeForAll = false;
    int x = -1, y = -1;              // the middle of the fight (map pixels); -1: the map's middle
    int gap = 256;                   // the distance between the two armies at the start (pixels)
    int frames = 2857;               // the most game frames (2 minutes at Fastest)
};
// A side's units of one type in a fight.
struct UnitTypeStats {
    int unitType = 0;
    int started = 0, alive = 0, kills = 0;
    int hp = 0, maxHp = 0, shields = 0, maxShields = 0;  // of the units alive (max: of all that started)
    int mineralCost = 0, gasCost = 0, supply = 0;        // of one unit (supply: in halves, as the game counts it)
};
struct CombatSide {
    int units = 0, alive = 0;
    int hp = 0, maxHp = 0, shields = 0, maxShields = 0;  // of the units alive
    std::vector<std::pair<int, int>> survivors;          // (unit type, count)
    // In detail: each unit type, the kills, the damage taken (hit points and shields), and what the units that died
    // cost (of what all the units cost).
    std::vector<UnitTypeStats> types;
    int kills = 0;
    int startHp = 0, startShields = 0, damageTaken = 0;
    int mineralsLost = 0, gasLost = 0, supplyLost = 0, mineralsAll = 0, gasAll = 0, supplyAll = 0;  // (supply: in halves)
    int team = -1;             // (in teams: the team and its players)
    int unreachable = 0;       // its units alive at the end that no enemy could reach or attack
    std::vector<int> players;
};
struct CombatResult {
    int frames = 0;
    bool timedOut = false;
    bool stalemate = false;  // (placed units: no damage and no unit nearer its enemy for 30 game seconds)
    int helpless = -1;  // the side (0, 1) that has only units that cannot attack: the fight ended there; -1: none
    CombatSide a, b;    // (the two sides; byPlayer: not filled)
    std::vector<CombatSide> teams;  // each team that had units
    int winner = -1;                // the team left with units; -1: none (or the time ran out)
    int winnerUnit = -1;            // (placed units) the index in `placed` of the last unit left; -1: none
};

constexpr int kAllPlayers = -2;  // Simulator::vision: what all players see

// A boost search's state (Simulator::boostStatus), all bases together.
struct BoostStatus {
    bool running = false, finished = false, exhausted = false;  // exhausted: nothing more to search
    bool failed = false;                                        // the search stopped on an error (at a base)
    int found = 0;                                              // the workers that it made faster
    double seconds = 0, progress = 0;                           // the time it searched; this run's part done (0 to 1)
};
// A boost's guide on the map (Simulator::miningGuides). kind 0: a worker's start place (position boost; target: its
// field); 1: where a worker turns back with its cargo (cargo boost); 2: a cycle's command (cycle boost; stage 1 to the
// next field, stage 2 back to its own; target: that field). `worker`: an id of the worker (the same for its guides).
struct MiningGuide {
    int x = 0, y = 0, targetX = 0, targetY = 0, kind = 0, stage = 0, worker = 0;
    int type = 7, owner = 0;  // the worker's unit type and player (its ghost on the map)
    int base = -1;            // its base: the index in miningResult()
};

// Rush distance (startRush): a worker walks from each start location to each other start location, a game for each
// route, all at the same time.
struct RushOptions {
    std::vector<Race> races = {Terran};  // the workers' race (races[0])
    // A start location's worker: its player's race in the map (as MiningOptions::mapRaces); off: races[0].
    bool mapRaces = false;
    uint32_t seed = 0;
    // The units that go on each route (units.dat ids; -1: a worker of the race); empty: a worker.
    std::vector<int> units;
    // On: one route, from (fromX, fromY) to (toX, toY) (map pixels), in place of the start locations' routes.
    bool points = false;
    int fromX = 0, fromY = 0, toX = 0, toY = 0;
};
struct RushRoute {
    int from = 0, to = 0;  // the players of the two start locations (0 to 11); -1: a point (RushOptions::points)
    int unit = 0;          // the unit that goes (units.dat id)
    int fromX = 0, fromY = 0, toX = 0, toY = 0;
    Race race = Terran;
    int frames = 0;                             // the game frames of the walk, to its end or to where it stopped
    double distance = 0;                        // the length of the walk (map pixels)
    bool reached = false, finished = false;     // at the other start location; the walk ended
    std::vector<std::pair<int, int>> path;      // the worker's places on the way
    int currentX = 0, currentY = 0;             // the worker's place now (path has a place each 8 pixels)
};

// A sound of the live game (sfxdata.dat id) where the game would play it: from the scripts (attacks, deaths, hits).
struct LiveSound {
    int sfx;
    int x, y;
    int owner;
};

// An image of the live game as the map draws it (images.dat id, its GRP frame, its place in map pixels).
struct LiveImage {
    int image = 0, frame = 0;
    bool flip = false;
    int x = 0, y = 0;
    int owner = 0;
    int elevation = 0;    // its sprite's elevation level (the drawing layer: lower ones first)
    int sortY = 0;        // its sprite's place (the drawing order within one elevation)
    bool air = false;     // its sprite flies
    int order = 0;        // its place among its sprite's images (0: the bottom one)
};

// A run of the whole map (Run the map): its units of every player, its forces, and the triggers that the simulation runs.
struct MapRunOptions {
    int frames = 14286;  // the most game frames (10 minutes at Fastest)
};
struct MapRunInfo {
    int triggers = 0, triggersRun = 0;  // the map's triggers, and those that the simulation runs (it knows only some conditions)
    int actionsSkipped = 0;             // actions of those that the simulation does not run (Display text, Set deaths...)
    std::vector<int> players;           // the players in the game (0 to 7)
};
// A player of a live game now.
struct PlayerState {
    int player = 0, minerals = 0, gas = 0, gatheredMinerals = 0, gatheredGas = 0, workers = 0;
    double mineralsPerMinute = 0, gasPerMinute = 0;  // (mining: since the warm-up; 0 during it)
    int units = 0, buildings = 0, unitsAtStart = 0;  // alive (Run the map)
    int victory = 0;                                 // 0: playing, 2: defeat, 3: victory (the map's triggers)
};

// The simulation of a map (createSimulator).
class Simulator {
public:
    virtual ~Simulator() = default;
    // The game's data (units.dat, iscript.bin, the GRPs' frame data, the tilesets...).
    virtual bool init(const FileReader& read, std::string& error) = 0;
    // The map (its scenario.chk). Remastered maps are read as Brood War maps.
    virtual bool setMap(std::vector<uint8_t> chk, std::string& error) = 0;

    // Runs to the end (the command line).
    virtual bool mining(const MiningOptions& o, std::vector<MiningBase>& out, std::string& error) = 0;
    virtual bool combat(const CombatOptions& o, CombatResult& out, std::string& error) = 0;

    // A live game, a frame at a time (the editor draws it). Mining: one game with a player at each base (at most 8),
    // races[0] and workers[0] of the options.
    virtual bool startCombat(const CombatOptions& o, std::string& error) = 0;
    virtual bool startMining(const MiningOptions& o, std::string& error) = 0;
    virtual bool startMap(const MapRunOptions& o, MapRunInfo& info, std::string& error) = 0;
    virtual bool startRush(const RushOptions& o, std::string& error) {
        (void)o;
        error = "Rush distance is not in this simulation";
        return false;
    }
    virtual bool live() const = 0;
    // `frames` more frames; false when the game is over (or an error: liveError).
    virtual bool step(int frames) = 0;
    virtual int frame() const = 0;
    virtual int lastFrame() const = 0;  // the frame at which it ends (when it does not end before)
    virtual bool done() const = 0;
    virtual const std::string& liveError() const = 0;
    virtual void images(std::vector<LiveImage>& out) const = 0;
    virtual std::vector<LiveSound> takeSounds() { return {}; }  // the sounds since the last call
    // Fast forward (Skip to the end): only what the results depend on, no pictures or sounds; the same results.
    // After it, images() gives the units at their frames; the effects of the skipped frames are not there.
    virtual void setFastForward(bool on) { (void)on; }
    // The live game's creep: per tile (the map's width x height) 1 with creep; nullptr: no game. `creepChanges`
    // changes when it does.
    virtual const std::vector<uint8_t>* creep() const { return nullptr; }
    virtual uint64_t creepChanges() const { return 0; }
    virtual CombatResult combatResult() const = 0;
    virtual std::vector<MiningBase> miningResult() const = 0;
    virtual std::vector<PlayerState> players() const = 0;  // (mining and Run the map)
    virtual std::vector<RushRoute> rushResult() const { return {}; }
    // Mining's boost searches (kind 0 position, 1 cargo, 2 cycle; `seconds` long; again: on from where it stopped).
    // The game plays on while a search runs; at its end the bases that it made better start again with it.
    virtual bool startBoost(int kind, double seconds, std::string& error) {
        (void)kind, (void)seconds;
        error = "Boost searches are not in this simulation";
        return false;
    }
    virtual BoostStatus boostStatus(int kind) const {
        (void)kind;
        return {};
    }
    virtual BoostStatus boostStatusForBase(int kind, int base) const {
        (void)base;
        return boostStatus(kind);
    }
    virtual bool boosting() const { return false; }
    // A boost turned off: the bases without it (and without the later boosts, which were found with it), from the
    // game's start.
    virtual void clearBoost(int kind) { (void)kind; }
    virtual std::vector<MiningGuide> miningGuides() const { return {}; }
    // Rush distance: what `player` (a start location's player, 0 to 11; kAllPlayers: all the routes' units) sees now,
    // as the game's fog of war: each map
    // tile (row by row, `width` x `height`) 0 not seen yet, 1 seen before, 2 seen now. False: no such game.
    virtual bool vision(int player, int& width, int& height, std::vector<uint8_t>& tiles) const {
        (void)player, (void)width, (void)height, (void)tiles;
        return false;
    }
    // The bases that mining uses (MiningOptions: which kinds of base), with each base's race (mapRaces, races[0]).
    virtual std::vector<MiningBase> miningBases(const MiningOptions& o, std::vector<Race>& races) const {
        (void)o, races.clear();
        return {};
    }
    virtual void stop() = 0;
};

// A new simulator, or nullptr while the engine is not part of this build.
std::unique_ptr<Simulator> createSimulator();

}  // namespace sim
