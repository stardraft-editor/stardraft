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
    std::vector<int> workers = {9, 18, 27};  // mineral workers of each run
    std::vector<Race> races = {Terran, Protoss, Zerg};
    int warmupFrames = 720, frames = 4286;  // 30 s, then 3 minutes measured (Fastest)
    bool gas = false;                       // a finished refinery on the nearest geyser, with 3 more workers
    bool mineralClusters = true;            // also a base at each group of mineral fields with none near (clusterBases)
    int gasWorkers = 3;                     // the workers on the refinery when `gas` is on (1 to 3)
    // The race of a start location's base: its player's race in the map (SIDE); a player whose race is picked in the
    // lobby or Random gets a random race (from `seed`). Other bases: races[0]. Off: races[0] everywhere.
    bool mapRaces = false;
    uint32_t seed = 0;                      // (mapRaces: the random races)
};
struct MiningRun {
    Race race = Terran;
    int workers = 0;
    double mineralsPerMinute = 0, gasPerMinute = 0;
    std::vector<double> tripFrames;  // each field's mean round trip (delivery to delivery), 0: no trip measured
};
struct MiningBase {
    int x = 0, y = 0;
    bool startLocation = false;
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
    int sortY = 0;        // its sprite's place (the drawing order)
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
    virtual bool live() const = 0;
    // `frames` more frames; false when the game is over (or an error: liveError).
    virtual bool step(int frames) = 0;
    virtual int frame() const = 0;
    virtual int lastFrame() const = 0;  // the frame at which it ends (when it does not end before)
    virtual bool done() const = 0;
    virtual const std::string& liveError() const = 0;
    virtual void images(std::vector<LiveImage>& out) const = 0;
    virtual std::vector<LiveSound> takeSounds() { return {}; }  // the sounds since the last call
    virtual CombatResult combatResult() const = 0;
    virtual std::vector<MiningBase> miningResult() const = 0;
    virtual std::vector<PlayerState> players() const = 0;  // (mining and Run the map)
    virtual void stop() = 0;
};

// A new simulator, or nullptr while the engine is not part of this build.
std::unique_ptr<Simulator> createSimulator();

}  // namespace sim
