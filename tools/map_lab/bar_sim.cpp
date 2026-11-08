// The simulation bar's engine (bar_sim.h). Mining and rush distance use the Map Simulation engine with the rules of
// its Mining Lab and Rush adapters (pull request 260): each base, and each rush route, is a world of its own, copied
// from one world of the map. Combat, and the bases of a map, come from StarDraft's own engine (tools/sim).
#include "bar_sim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <mapeditor/simulation/mining_position.h>
#include <mapeditor/simulation/mining_search.h>
#include <mapeditor/simulation/search_job.h>

#include "native_world.h"

namespace maplab {
namespace {

namespace ms = mapeditor::simulation;

// The game's unit numbers that the adapters use (the Map Simulation engine's own data has no roles).
constexpr int kZergDrone = 41, kTerranScv = 7, kProtossProbe = 64;
constexpr int kZergHatchery = 131, kTerranCommandCenter = 106, kProtossNexus = 154;
constexpr int kMineralFirst = 176, kMineralLast = 178;
// Mining: the mineral fields of a base are those within 12 tiles of its depot; they do not run out.
constexpr int kFieldRange = 384, kFieldAmount = 50000, kMineralsPerTrip = 8;
// Rush: a walk ends when the unit's move ends (its order is no longer Move) or for 480 frames it has come neither to a
// new part of the map (a 128 pixel square it was not in yet) nor nearer to the end than before (it stands, or goes back
// and forth: no way on; a way that turns back beside a wall goes nearer to the end); no
// walk is longer than 24000 frames; the unit is at the end when it stops within a tile of it (a fast unit stops a few
// pixels past the point, and a point where it cannot stand ends its way next to it); it starts within 8 pixels of its
// start.
constexpr int kRushStall = 480, kRushCell = 128, kRushMost = 24000, kRushStartSlack = 8, kRushMostGames = 64, kRushArrive = 32,
              kOrderMove = 6;
constexpr int kMaxBases = 8;

int workerOf(int race) { return race == sim::Zerg ? kZergDrone : race == sim::Terran ? kTerranScv : kProtossProbe; }
int depotOf(int race) { return race == sim::Zerg ? kZergHatchery : race == sim::Terran ? kTerranCommandCenter : kProtossNexus; }

int pixelDistance(ms::Point a, ms::Point b) { return ms::distance({a.x - b.x, a.y - b.y}); }

class BarEngine final : public sim::Simulator {
public:
    BarEngine() : classic_(sim::createSimulator()) {}

    bool init(const sim::FileReader& read, std::string& error) override {
        read_ = read;
        definitions_.reset();
        return classic_ && classic_->init(read, error);
    }
    bool setMap(std::vector<uint8_t> chk, std::string& error) override {
        if (chk != chk_) stop(), seed_.reset();  // (the same map again, as Restart has it: the same world, the same boosts)
        else stopPlay();
        chk_ = chk;
        return classic_->setMap(std::move(chk), error);
    }

    // (the command line: all the bases, not only the first 8; each race and worker count a run)
    bool mining(const sim::MiningOptions& o, std::vector<sim::MiningBase>& out, std::string& error) override {
        out.clear();
        for (int workers : o.workers) {
            for (sim::Race race : o.mapRaces ? std::vector<sim::Race>{o.races.empty() ? sim::Terran : o.races[0]} : o.races) {
                sim::MiningOptions one = o;
                one.workers = {workers}, one.races = {race};
                if (!beginMining(one, error, false)) return false;
                while (step(240)) {
                }
                if (!error_.empty()) {
                    error = error_;
                    stop();
                    return false;
                }
                const std::vector<sim::MiningBase> got = miningResult();
                if (out.empty()) out = got;
                else  // (the same bases in the same order each run: a base with no room is in the list too)
                    for (size_t k = 0; k < out.size() && k < got.size(); ++k) out[k].runs.push_back(got[k].runs[0]);
            }
        }
        stop();
        return true;
    }
    bool combat(const sim::CombatOptions& o, sim::CombatResult& out, std::string& error) override {
        stop();
        return classic_->combat(o, out, error);
    }
    bool startCombat(const sim::CombatOptions& o, std::string& error) override {
        stop();
        if (!classic_->startCombat(o, error)) return false;
        mode_ = Mode::Classic;
        return true;
    }
    bool startMap(const sim::MapRunOptions& o, sim::MapRunInfo& info, std::string& error) override {
        stop();
        return classic_->startMap(o, info, error);
    }

    // Mining: at each base (at most 8), a depot of its race and its workers on the mineral fields within 12 tiles, a
    // world for each base (bases do not take each other's fields). The measures start after the warm-up.
    bool startMining(const sim::MiningOptions& o, std::string& error) override { return beginMining(o, error, true); }
    // `firstBases`: at most 8 bases (the bar). A base with no room for its depot and workers is in the list, with no
    // numbers (MiningRun::placed false).
    // The same map and settings again (Restart, the timeline): the bases as they were set up, with their boosts.
    bool beginMining(const sim::MiningOptions& o, std::string& error, bool firstBases) {
        stopPlay();
        if (o.gas) return error = "The mining simulation has no gas", false;
        std::vector<sim::Race> races;
        std::vector<sim::MiningBase> bases = classic_->miningBases(o, races);
        if (bases.empty()) return error = "The map has no base to mine at", false;
        if (firstBases && bases.size() > size_t(kMaxBases)) bases.resize(size_t(kMaxBases)), races.resize(size_t(kMaxBases));
        // (the workers: a count for each base, or -n: n for each mineral field)
        const int count = o.workersPerField > 0 ? -o.workersPerField : o.workers.empty() ? 9 : o.workers[0];
        std::string key = std::to_string(count);
        for (size_t k = 0; k < bases.size(); ++k)
            key += "," + std::to_string(bases[k].x) + ":" + std::to_string(bases[k].y) + ":" + std::to_string(int(races[k]));
        try {
            if (!loadSeed(error)) return false;
            if (key != setupKey_ || setups_.empty()) {
                stopBoosts();
                std::string first;
                bool any = false;
                for (size_t k = 0; k < bases.size(); ++k) {
                    // (a world's players are 0 to 11: past the 12th base, each base is a world of its own anyway)
                    const int owner = int(k % size_t(kMaxBases));
                    auto s = std::make_unique<Setup>();
                    s->base = bases[k], s->race = races[k], s->owner = owner;
                    try {
                        makeSetup(*s, count);
                        any = true;
                    } catch (const std::exception& e) {  // (that base has no numbers: the others still mine)
                        if (first.empty()) first = e.what();
                        s->seed.reset();
                    }
                    setups_.push_back(std::move(s));
                }
                if (!any) return stopBoosts(), error = first, false;
                setupKey_ = key;
            }
            for (const auto& s : setups_) bases_.push_back(play(*s));
        } catch (const std::exception& e) {
            bases_.clear();
            return error = e.what(), false;
        }
        warmup_ = o.warmupFrames, last_ = o.warmupFrames + o.frames;
        mode_ = Mode::Mining;
        return true;
    }

    // A boost search at each base (kind 0 the workers' start places, 1 cargo returns, 2 cycles), `seconds` long, the
    // bases at the same time: the PR's Mining Lab searches. Again after one ended: it goes on from where it stopped.
    // Meanwhile the game waits at its start; at the end the bases that a search made better start again with it.
    bool startBoost(int kind, double seconds, std::string& error) override {
        if (mode_ != Mode::Mining || kind < 0 || kind > 2) return error = "A boost search is only for mining", false;
        if (boosting()) return error = "A boost search runs", false;
        seconds = std::clamp(std::isfinite(seconds) ? seconds : 15.0, 0.05, 600.0);
        int placed = 0;
        for (const auto& s : setups_) placed += s->seed ? 1 : 0;
        const unsigned threads = std::max(1u, ms::mining_compute::Executor::concurrency() / unsigned(std::max(1, placed)));
        try {
            for (const auto& sp : setups_) {
                Setup& s = *sp;
                if (!s.seed) continue;
                std::vector<ms::MiningAssignment> assignments;
                for (const auto& [w, f] : s.assign) assignments.push_back({w, f});
                if (kind == 0) {
                    if (s.positionJob && s.positionJob->exhausted.load()) continue;
                    if (s.positionJob) s.positionJob->extend(seconds);
                    else
                        s.positionJob = std::make_unique<PositionJob>(
                            *s.seed, [assignments](const ms::World& initial, auto* pool) { return std::make_unique<PositionPlan>(initial, assignments, pool); },
                            seconds, threads);
                } else {
                    auto& job = s.jobs[size_t(kind)];
                    if (job && job->exhausted.load()) continue;
                    if (job) {
                        job->extend(seconds);
                    } else {
                        const bool cycle = kind == 2;
                        const std::vector<Control> background = cycle ? freshControls(s.cargo) : std::vector<Control>{};
                        job = std::make_unique<SearchJob>(
                            *s.seed,
                            [assignments, background, cycle](const ms::World& initial, auto* pool) {
                                return std::make_unique<SearchPlan>(initial, assignments, background, pool, cycle);
                            },
                            seconds, threads);
                    }
                }
                sim::BoostStatus& st = s.status[size_t(kind)];
                st.running = true, st.finished = false, st.progress = 0;
            }
        } catch (const std::exception& e) {
            return error = e.what(), false;
        }
        restartPlay();  // (the game waits at its start while the search runs)
        return true;
    }
    // A boost search's state, all bases together.
    sim::BoostStatus boostStatus(int kind) const override {
        sim::BoostStatus out;
        if (kind < 0 || kind > 2) return out;
        bool any = false, allDone = true, allExhausted = true;
        double progress = 1;
        for (const auto& s : setups_) {
            if (!s->seed) continue;
            const sim::BoostStatus& st = s->status[size_t(kind)];
            if (!st.running && !st.finished) continue;
            any = true;
            out.running = out.running || st.running;
            allDone = allDone && st.finished, allExhausted = allExhausted && st.exhausted;
            out.found += st.found;
            out.failed = out.failed || st.failed;
            out.seconds = std::max(out.seconds, st.seconds);
            progress = std::min(progress, st.running ? st.progress : 1.0);
        }
        if (!any) return out;
        out.finished = allDone && !out.running, out.exhausted = allExhausted && !out.running;
        out.progress = progress;
        return out;
    }
    void clearBoost(int kind) override {
        if (mode_ != Mode::Mining || kind < 0 || kind > 2) return;
        for (const auto& sp : setups_) {
            Setup& s = *sp;
            if (!s.seed) continue;
            // (a later boost was found with the earlier ones: it goes too)
            if (kind <= 2) s.jobs[2].reset(), s.cycle.clear(), s.status[2] = {};
            if (kind <= 1) s.jobs[1].reset(), s.cargo.clear(), s.status[1] = {};
            if (kind == 0) {
                s.positionJob.reset(), s.fixed.clear(), s.status[0] = {};
                if (s.origin) s.seed = std::make_unique<ms::World>(*s.origin);
            }
        }
        changed_ = false;
        restartPlay();
    }
    bool boosting() const override {
        for (const auto& s : setups_)
            for (const sim::BoostStatus& st : s->status)
                if (st.running) return true;
        return false;
    }
    // The boosts' guides: the workers' start places (position), where a worker turns back with its cargo, and each
    // cycle's two commands (the PR's map guides).
    std::vector<sim::MiningGuide> miningGuides() const override {
        std::vector<sim::MiningGuide> out;
        if (mode_ != Mode::Mining) return out;
        for (size_t k = 0; k < bases_.size() && k < setups_.size(); ++k) {
            const Base& b = bases_[k];
            const Setup& s = *setups_[k];
            out.insert(out.end(), s.fixed.begin(), s.fixed.end());
            if (!b.world) continue;
            for (const std::vector<Control>* rules : {&b.cargo, &b.cycle})
                for (const Control& r : *rules) {
                    if (!r.enabled || !r.commands) continue;
                    const ms::UnitState* w = b.world->get(r.worker);
                    if (!w || !w->visible || w->dead) continue;
                    const int id = int(k) * 1000 + r.worker.index;
                    if (!r.cycle) {
                        out.push_back({r.issuedAt.x, r.issuedAt.y, 0, 0, 1, 0, id});
                        continue;
                    }
                    const ms::UnitState *n = b.world->get(r.neighbor), *f = b.world->get(r.field);
                    if (!n || !f || !n->visible || !f->visible) continue;
                    out.push_back({r.firstAt.x, r.firstAt.y, n->position.x, n->position.y, 2, 1, id});
                    if (r.commands >= 2) out.push_back({r.secondAt.x, r.secondAt.y, f->position.x, f->position.y, 2, 2, id});
                }
        }
        return out;
    }

    // Rush distance: from each start location to each other one, a worker of the start location's race (a world for
    // each route), at the same time.
    bool startRush(const sim::RushOptions& o, std::string& error) override {
        stop();
        sim::MiningOptions starts;
        starts.placedDepots = false, starts.mineralClusters = false;
        starts.races = o.races, starts.mapRaces = o.mapRaces, starts.seed = o.seed;
        // The routes: from each start location to each other one, or the one route between the two points.
        struct Way {
            int from, to, fromX, fromY, toX, toY;
            sim::Race race;
        };
        std::vector<Way> ways;
        if (o.points) {
            ways.push_back({-1, -1, o.fromX, o.fromY, o.toX, o.toY, o.races.empty() ? sim::Terran : o.races[0]});
        } else {
            std::vector<sim::Race> races;
            const std::vector<sim::MiningBase> bases = classic_->miningBases(starts, races);
            if (bases.size() < 2) return error = "The map has fewer than 2 start locations", false;
            for (size_t a = 0; a < bases.size(); ++a)
                for (size_t b = 0; b < bases.size(); ++b)
                    if (a != b) ways.push_back({bases[a].player, bases[b].player, bases[a].x, bases[a].y, bases[b].x, bases[b].y, races[a]});
        }
        const std::vector<int> units = o.units.empty() ? std::vector<int>{-1} : o.units;
        // (each route and unit is a game of the whole map: at most 64, as the window stays usable with them)
        if (ways.size() * units.size() > size_t(kRushMostGames))
            return error = "Too many games: " + std::to_string(ways.size()) + " routes with " + std::to_string(units.size()) +
                           " units each. Select fewer units (at most " + std::to_string(kRushMostGames) + " games)",
                   false;
        try {
            if (!loadSeed(error)) return false;
            for (const Way& w : ways)
                for (int unit : units) {
                    Runner r;
                    r.route.from = w.from, r.route.to = w.to;
                    r.route.fromX = w.fromX, r.route.fromY = w.fromY, r.route.toX = w.toX, r.route.toY = w.toY;
                    r.route.race = w.race;
                    r.route.unit = unit >= 0 ? unit : workerOf(w.race);
                    r.world = std::make_unique<ms::World>(*seed_);
                    r.worker = r.world->createFinished(r.route.unit, {w.fromX, w.fromY}, 0);
                    const ms::UnitState* u = r.world->get(r.worker);
                    if (!u || !u->visible || u->dead || !u->motion || std::abs(u->position.x - w.fromX) > kRushStartSlack ||
                        std::abs(u->position.y - w.fromY) > kRushStartSlack)
                        continue;  // (a blocked start: no walk from it for this unit)
                    r.route.path.push_back({u->position.x, u->position.y});
                    r.last = r.route.path.back();
                    r.world->move(r.worker, {w.toX, w.toY});
                    runners_.push_back(std::move(r));
                }
        } catch (const std::exception& e) {
            runners_.clear();
            return error = e.what(), false;
        }
        if (runners_.empty())
            return error = o.points ? "The units cannot stand at the start point" : "A unit cannot stand at any start location", false;
        last_ = kRushMost;
        mode_ = Mode::Rush;
        return true;
    }

    bool live() const override { return mode_ == Mode::Classic ? classic_->live() : mode_ != Mode::None; }
    bool step(int frames) override {
        if (mode_ == Mode::Classic) return classic_->step(frames);
        if (mode_ == Mode::None || done_) return false;
        try {
            if (mode_ == Mode::Mining && boosting()) {  // (the game waits while a boost search runs)
                pollBoosts();
                return true;
            }
            for (int i = 0; i < frames && !done_; ++i) {
                if (mode_ == Mode::Mining) stepMining();
                else stepRush();
                ++frame_;
            }
        } catch (const std::exception& e) {
            error_ = e.what(), done_ = true;
        }
        return !done_;
    }
    int frame() const override { return mode_ == Mode::Classic ? classic_->frame() : frame_; }
    int lastFrame() const override { return mode_ == Mode::Classic ? classic_->lastFrame() : last_; }
    bool done() const override { return mode_ == Mode::Classic ? classic_->done() : done_; }
    const std::string& liveError() const override { return mode_ == Mode::Classic ? classic_->liveError() : error_; }
    void images(std::vector<sim::LiveImage>& out) const override {
        out.clear();
        if (mode_ == Mode::Classic) return classic_->images(out);
        if (mode_ == Mode::Mining) {
            // Each base's own units and fields from its world; the map's other units (fields of no base, critters)
            // from the first world.
            std::set<int> own;
            for (const Base& b : bases_)
                for (const Worker& w : b.workers) own.insert(w.patch.index);
            const ms::World* first = nullptr;  // (the first base with a world)
            for (const Base& b : bases_) {
                if (!b.world) continue;
                if (!first) first = b.world.get();
                std::vector<ms::UnitHandle> list;
                for (int id : b.world->visibleUnits()) {
                    const ms::UnitState& u = *b.world->units()[size_t(id)];
                    if (u.owner == b.owner || std::any_of(b.workers.begin(), b.workers.end(), [&](const Worker& w) { return w.patch == u.handle; }))
                        list.push_back(u.handle);
                }
                addImages(*b.world, list, out);
            }
            if (first) {
                const ms::World& w = *first;
                std::vector<ms::UnitHandle> list;
                for (int id : w.visibleUnits()) {
                    const ms::UnitState& u = *w.units()[size_t(id)];
                    if (u.owner >= kMaxBases && !own.count(id)) list.push_back(u.handle);
                }
                addImages(w, list, out);
            }
        } else if (mode_ == Mode::Rush) {
            if (!seed_) return;
            std::vector<ms::UnitHandle> list;  // (the map's own units: from the first route's world, else the map's)
            const ms::World& w = runners_.empty() ? *seed_ : *runners_.front().world;
            for (int id : w.visibleUnits())
                if (w.units()[size_t(id)]->owner >= kMaxBases) list.push_back(w.units()[size_t(id)]->handle);
            addImages(w, list, out);
            for (const Runner& r : runners_)
                if (!r.route.finished) addImages(*r.world, std::vector<ms::UnitHandle>{r.worker}, out, r.route.from);
        }
    }
    const std::vector<uint8_t>* creep() const override {
        if (mode_ == Mode::Classic) return classic_->creep();
        return creep_.empty() ? nullptr : &creep_;
    }
    uint64_t creepChanges() const override { return mode_ == Mode::Classic ? classic_->creepChanges() : creepChanges_; }
    void setFastForward(bool on) override { classic_->setFastForward(on); }
    std::vector<sim::LiveSound> takeSounds() override { return mode_ == Mode::Classic ? classic_->takeSounds() : std::vector<sim::LiveSound>{}; }
    sim::CombatResult combatResult() const override { return mode_ == Mode::Classic ? classic_->combatResult() : sim::CombatResult{}; }

    std::vector<sim::MiningBase> miningResult() const override {
        std::vector<sim::MiningBase> out;
        if (mode_ == Mode::Classic) return classic_->miningResult();
        if (mode_ != Mode::Mining) return out;
        const double minutes = std::max(0, std::min(frame_, last_) - warmup_) * 42.0 / 60000.0;
        for (const Base& b : bases_) {
            sim::MiningBase m = b.base;
            m.fields.clear();
            sim::MiningRun run;
            run.race = b.race, run.workers = int(b.workers.size());
            if (!b.world) {
                run.placed = false;
                m.runs = {run};
                out.push_back(m);
                continue;
            }
            if (minutes > 0 && b.measured >= 0) run.mineralsPerMinute = (b.world->deliveredMinerals(b.owner) - b.measured) / minutes;
            for (const ms::UnitHandle& p : b.patches) {
                const ms::UnitState* u = b.world->get(p);
                m.fields.push_back(u ? std::pair<int, int>{u->position.x, u->position.y} : std::pair<int, int>{0, 0});
                const auto t = b.trips.find(p.index);
                run.tripFrames.push_back(t != b.trips.end() && t->second.count ? double(t->second.sum) / double(t->second.count) : 0);
                const auto amount = b.amount.find(p.index), mined = b.mined.find(p.index);
                const int left = (amount != b.amount.end() ? amount->second : 0) - (mined != b.mined.end() ? mined->second : 0);
                run.fieldMinerals.push_back(std::max(0, left));
            }
            m.runs = {run};
            out.push_back(m);
        }
        return out;
    }
    std::vector<sim::PlayerState> players() const override {
        if (mode_ == Mode::Classic) return classic_->players();
        std::vector<sim::PlayerState> out;
        for (const sim::MiningBase& m : miningResult()) {
            sim::PlayerState s;
            s.player = int(out.size());
            s.workers = m.runs.empty() ? 0 : m.runs[0].workers;
            s.mineralsPerMinute = m.runs.empty() ? 0 : m.runs[0].mineralsPerMinute;
            out.push_back(s);
        }
        return out;
    }
    // What the player's scouts see: the vision of the workers that walk from the player's start location (each in its
    // own game, as the engine's fog of war has it: sight ranges, high ground), together.
    bool vision(int player, int& width, int& height, std::vector<uint8_t>& tiles) const override {
        if (mode_ != Mode::Rush) return false;
        bool any = false;
        for (const Runner& r : runners_) {
            if (player != sim::kAllPlayers && r.route.from != player) continue;
            const ms::World& w = *r.world;
            if (!any) {
                width = w.map().terrain.width(), height = w.map().terrain.height();
                tiles.assign(size_t(width) * size_t(height), 0);
                any = true;
            }
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    uint8_t& t = tiles[size_t(y) * size_t(width) + size_t(x)];
                    if (t == 2) continue;
                    if (w.visibleAt({x, y}) & 1) t = 2;  // (the scout is player 0 of its game)
                    else if (w.exploredAt({x, y}) & 1) t = 1;
                }
        }
        return any;
    }
    std::vector<sim::RushRoute> rushResult() const override {
        std::vector<sim::RushRoute> out;
        for (const Runner& r : runners_) out.push_back(r.route);
        return out;
    }
    std::vector<sim::MiningBase> miningBases(const sim::MiningOptions& o, std::vector<sim::Race>& races) const override {
        return classic_->miningBases(o, races);
    }
    void stop() override {
        stopPlay();
        stopBoosts();
    }

private:
    using Control = ms::MiningControl;
    using SearchPlan = ms::mining_search::Plan;
    using SearchJob = ms::mining_compute::Job<SearchPlan>;
    using PositionPlan = ms::position_search::Plan;
    using PositionJob = ms::mining_compute::Job<PositionPlan>;
    enum class Mode { None, Classic, Mining, Rush };

    // The game that runs ends; the bases' setups and their boost searches stay (stop: they end too).
    void stopPlay() {
        if (classic_) classic_->stop();
        mode_ = Mode::None;
        bases_.clear(), runners_.clear();
        frame_ = 0, last_ = 0, warmup_ = 0, done_ = false;
        error_.clear();
        creep_.clear(), ++creepChanges_;
    }
    void stopBoosts() {
        setups_.clear();  // (each search's thread ends with it)
        setupKey_.clear();
    }
    // The mining from its start again, with the setups as they are now (a boost search changed them).
    void restartPlay() {
        bases_.clear();
        for (const auto& s : setups_) bases_.push_back(play(*s));
        frame_ = 0, done_ = false;
        creep_.clear(), ++creepChanges_;
    }
    struct Worker {
        ms::UnitHandle unit, patch;
        ms::UnitHandle mined;  // the field it gathers from now (the engine sends a worker to a free field when its own is busy)
        bool carrying = false;
        int lastDelivery = -1;
    };
    struct Trips {
        long long sum = 0, count = 0;  // the round trips (delivery to delivery) after the warm-up
    };
    struct Base {
        sim::MiningBase base;
        sim::Race race = sim::Terran;
        int owner = 0;
        std::unique_ptr<ms::World> world;  // none: no room at the base for its depot and workers
        std::vector<Worker> workers;
        std::vector<ms::UnitHandle> patches;  // nearest first
        std::map<int, Trips> trips;           // by the field's unit index: the trips of the workers that mined it
        std::map<int, int> amount, mined;     // by the field's unit index: its minerals in the map, those mined from it
        int measured = -1;                   // the minerals delivered at the warm-up's end
        std::vector<Control> cargo, cycle;    // the boosts' commands (fresh copies of the setup's)
    };
    // A base as it starts (the same each time the mining starts again), its boosts and their searches.
    struct Setup {
        sim::MiningBase base;
        sim::Race race = sim::Terran;
        int owner = 0;
        std::unique_ptr<ms::World> seed;  // the world at the start; none: no room at the base for its depot and workers
        std::unique_ptr<ms::World> origin;  // the same before a position boost (turned off: back to it)
        std::vector<std::pair<ms::UnitHandle, ms::UnitHandle>> assign;  // each worker and its field
        std::vector<ms::UnitHandle> patches;                            // nearest first
        std::map<int, int> amount;                                      // by the field's unit index: its minerals in the map
        std::vector<Control> cargo, cycle;                              // the cargo and cycle boosts found
        std::vector<sim::MiningGuide> fixed;                            // the position boosts' guides
        std::unique_ptr<PositionJob> positionJob;
        std::array<std::unique_ptr<SearchJob>, 3> jobs;  // [1] cargo, [2] cycle
        std::array<sim::BoostStatus, 3> status{};
    };
    struct Runner {
        std::unique_ptr<ms::World> world;
        ms::UnitHandle worker;
        sim::RushRoute route;
        std::pair<int, int> last;  // the worker's place in the last frame
        int stalled = 0;            // the frames since the unit came to a part of the map that it was not in yet
        std::set<int> cells;        // those parts (kRushCell squares) of the map it was in
        double nearest = 1e18;      // the nearest it came to the end
        bool moving = false;  // the unit's Move order has started
    };

    // The game data (once) and the map's world (once a map: Restart copies it again).
    bool loadSeed(std::string& error) {
        const FileReader read = [this](const std::string& name) {
            std::vector<uint8_t> out;
            if (!read_ || !read_(name, out)) throw std::runtime_error("Could not read the game file " + name);
            return out;
        };
        if (!definitions_) definitions_ = std::make_shared<ms::Definitions>([&](const std::string& n) { return read(n); });
        if (!seed_) seed_ = loadNativeWorld(definitions_, read, chk_);
        if (!seed_) return error = "Could not load the map", false;
        return true;
    }

    // A base as the Mining Lab starts it: the melee start of the race at the base, the workers that the start places,
    // more or fewer of them for `count` (0: one for each field), each worker on a field (nearest first, round).
    void makeSetup(Setup& b, int count) {
        const sim::MiningBase& at = b.base;
        const sim::Race race = b.race;
        const int owner = b.owner;
        b.seed = std::make_unique<ms::World>(*seed_);
        ms::World& world = *b.seed;
        std::set<int> existing;
        for (int id : world.visibleUnits())
            if (world.units()[size_t(id)]->owner == owner) existing.insert(id);
        std::vector<ms::UnitHandle> workers = world.startingUnits(int(race), {at.x, at.y}, owner);
        ms::UnitHandle depot;
        for (int id : world.visibleUnits()) {
            const ms::UnitState& u = *world.units()[size_t(id)];
            if (!existing.count(id) && u.owner == owner && u.type == depotOf(race)) depot = u.handle;
        }
        const ms::UnitState* d = world.get(depot);
        workers.erase(std::remove_if(workers.begin(), workers.end(),
                                     [&](ms::UnitHandle h) {
                                         const ms::UnitState* w = world.get(h);
                                         return !w || w->dead || !w->visible || !w->motion;
                                     }),
                      workers.end());
        if (!d || d->dead || !d->visible || workers.empty()) throw std::runtime_error("Cannot place the depot and the workers at a base");
        std::sort(workers.begin(), workers.end(), [&](ms::UnitHandle a, ms::UnitHandle c) { return world.get(a)->birth > world.get(c)->birth; });
        const ms::Point depotAt = d->position;
        for (int id : world.visibleUnits()) {
            const ms::UnitState& u = *world.units()[size_t(id)];
            if (u.type >= kMineralFirst && u.type <= kMineralLast && pixelDistance(u.position, depotAt) <= kFieldRange) b.patches.push_back(u.handle);
        }
        std::sort(b.patches.begin(), b.patches.end(), [&](ms::UnitHandle p, ms::UnitHandle q) {
            const int dp = pixelDistance(world.get(p)->position, depotAt), dq = pixelDistance(world.get(q)->position, depotAt);
            return dp == dq ? p.index < q.index : dp < dq;
        });
        if (b.patches.empty()) throw std::runtime_error("A base has no mineral field within 12 tiles");
        for (const ms::UnitHandle& p : b.patches) b.amount[p.index] = world.get(p)->resources;
        count = count > 0 ? count : int(b.patches.size()) * std::max(1, -count);  // (0 or -n: 1 or n for each field)
        while (int(workers.size()) < count) {
            const ms::UnitHandle h = world.createFinished(workerOf(race), depotAt, owner);
            const ms::UnitState* w = world.get(h);
            if (!w || w->dead || !w->visible || !w->motion) throw std::runtime_error("Cannot place a worker at a base");
            workers.push_back(h);
        }
        while (int(workers.size()) > count) world.remove(workers.back()), workers.pop_back();
        for (size_t i = 0; i < workers.size(); ++i) {
            const ms::UnitHandle patch = b.patches[i % b.patches.size()];
            world.setResources(patch, kFieldAmount);
            world.harvest(workers[i], patch);
            b.assign.push_back({workers[i], patch});
        }
        world.prepareNavigation(workers.front());
        b.origin = std::make_unique<ms::World>(world);
    }
    // The game of a base from its setup.
    Base play(const Setup& s) const {
        Base b;
        b.base = s.base, b.race = s.race, b.owner = s.owner;
        if (!s.seed) return b;
        b.world = std::make_unique<ms::World>(*s.seed);
        b.patches = s.patches, b.amount = s.amount;
        for (const auto& [w, f] : s.assign) b.workers.push_back({w, f, f});
        b.cargo = freshControls(s.cargo), b.cycle = freshControls(s.cycle);
        return b;
    }
    // A boost's commands as they are before the game (the PR's freshControls).
    static std::vector<Control> freshControls(const std::vector<Control>& rules) {
        std::vector<Control> out;
        for (const Control& r : rules) {
            Control next = ms::mining_search::makeRule(r.worker, r.field, r.spot, r.enabled);
            next.cycle = r.cycle, next.neighbor = r.neighbor, next.delay = r.delay, next.detour = r.detour;
            next.cargoDelay = r.cargoDelay;
            out.push_back(next);
        }
        return out;
    }

    // The boost searches' state (the PR's NativeMining::poll): a search that found better takes its result into the
    // setup (a position boost: the workers' start places; it drops the cargo and cycle boosts, which were found for the
    // places before). When none runs any more and one changed a setup, the mining starts again.
    void pollBoosts() {
        bool changed = false;
        for (const auto& sp : setups_) {
            Setup& s = *sp;
            auto progress = [](auto& job, sim::BoostStatus& st) {
                st.seconds = job.seconds();
                const double budget = job.sliceLimit.load();
                st.progress = budget > 0 ? std::min(1.0, job.sliceElapsed.load() / budget) : 0;
            };
            auto finish = [](auto& job, sim::BoostStatus& st) {
                st.running = false, st.finished = true, st.exhausted = job.exhausted.load();
            };
            try {
                if (PositionJob* job = s.positionJob.get(); job && s.status[0].running) {
                    sim::BoostStatus& st = s.status[0];
                    progress(*job, st);
                    if (job->done()) {
                        finish(*job, st);
                        if (job->changed.load()) {
                            const PositionPlan& plan = *job->result;
                            auto world = std::make_unique<ms::World>(*job->input);
                            s.jobs[1].reset(), s.jobs[2].reset();
                            s.cargo.clear(), s.cycle.clear();
                            s.status[1] = {}, s.status[2] = {};
                            s.fixed.clear();
                            st.found = 0;
                            for (size_t i = 0; i < plan.positions.size(); ++i) {
                                if (!plan.positions[i].stable) continue;
                                const auto a = plan.assignments[i];
                                world->relocateWorkerForSearch(a.worker, plan.positions[i].position);
                                world->harvest(a.worker, a.field);
                                if (plan.control[i].mean() - plan.selected[i].mean() > .5 && plan.control[i].mean(1) - plan.selected[i].mean(1) > .1 &&
                                    plan.control[i].mean(2) - plan.selected[i].mean(2) > .1) {
                                    ++st.found;
                                    const ms::UnitState *w = world->get(a.worker), *m = world->get(a.field);
                                    if (w && m)
                                        s.fixed.push_back({w->position.x, w->position.y, m->position.x, m->position.y, 0, 0, s.owner * 1000 + w->handle.index});
                                }
                            }
                            s.seed = std::move(world);
                            changed = true;
                        }
                    }
                }
                for (int kind : {1, 2}) {
                    SearchJob* job = s.jobs[size_t(kind)].get();
                    sim::BoostStatus& st = s.status[size_t(kind)];
                    if (!job || !st.running) continue;
                    progress(*job, st);
                    if (!job->done()) continue;
                    finish(*job, st);
                    if (!job->changed.load()) continue;
                    s.seed = std::make_unique<ms::World>(*job->input);
                    if (kind == 1) {
                        s.cargo = freshControls(job->result->chosen);
                        s.jobs[2].reset(), s.cycle.clear(), s.status[2] = {};
                        st.found = int(std::count_if(s.cargo.begin(), s.cargo.end(), [](const Control& r) { return r.enabled; }));
                    } else {
                        s.cycle = freshControls(job->result->chosen);
                        s.cargo = freshControls(s.cargo);
                        st.found = int(std::count_if(s.cycle.begin(), s.cycle.end(), [](const Control& r) { return r.enabled; }));
                    }
                    changed = true;
                }
            } catch (const std::exception&) {  // (that base's search failed: it ends, the game and the others go on)
                for (sim::BoostStatus& st : s.status)
                    if (st.running) st.running = false, st.finished = true, st.failed = true;
            }
        }
        changed_ = changed_ || changed;
        if (!boosting() && changed_) changed_ = false, restartPlay();
    }

    void stepMining() {
        const bool measuring = frame_ >= warmup_;
        for (Base& b : bases_) {
            if (!b.world) continue;
            if (frame_ == warmup_) b.measured = b.world->deliveredMinerals(b.owner);
            for (Worker& w : b.workers) {
                const ms::UnitState* u = b.world->get(w.unit);
                w.carrying = u->cargo & 2;
                if (b.world->get(u->mineral)) w.mined = u->mineral;
            }
            for (Control& r : b.cargo) r.apply(*b.world);  // (the boosts' commands, as the PR's playback gives them)
            for (Control& r : b.cycle) r.apply(*b.world);
            b.world->advance();
            const int now = b.world->frame();
            for (Worker& w : b.workers) {
                if (!w.carrying || (b.world->get(w.unit)->cargo & 2)) continue;
                b.mined[(b.world->get(w.mined) ? w.mined : w.patch).index] += kMineralsPerTrip;
                if (measuring && w.lastDelivery >= 0) {
                    Trips& t = b.trips[(b.world->get(w.mined) ? w.mined : w.patch).index];
                    t.sum += now - w.lastDelivery, ++t.count;
                }
                w.lastDelivery = now;
            }
        }
        if (frame_ + 1 >= last_) done_ = true;
        if ((frame_ & 15) == 0) takeCreep();
    }

    void stepRush() {
        bool all = true;
        for (Runner& r : runners_) {
            if (r.route.finished) continue;
            r.world->advance();
            ++r.route.frames;
            const ms::UnitState* u = r.world->get(r.worker);
            if (!u || !u->visible || u->dead) {
                r.route.finished = true;
                continue;
            }
            const std::pair<int, int> at{u->position.x, u->position.y}, before = r.last;
            if (at != before) r.route.distance += std::hypot(double(at.first - before.first), double(at.second - before.second)), r.last = at;
            const double away = std::hypot(double(at.first - r.route.toX), double(at.second - r.route.toY));
            const bool nearer = away < r.nearest - 8;  // (8 pixels nearer than ever: on its way)
            if (nearer) r.nearest = away;
            if (r.cells.insert(at.first / kRushCell + at.second / kRushCell * 4096).second || nearer) r.stalled = 0;
            else ++r.stalled;
            const bool moving = u->order == kOrderMove;
            const bool stopped = r.moving && !moving;  // (the move ended: the unit is where its way ends)
            r.moving = r.moving || moving;
            r.route.reached = away <= 1 || (stopped && away <= kRushArrive);
            r.route.finished = r.route.reached || stopped || r.stalled >= kRushStall || r.route.frames >= kRushMost;
            const std::pair<int, int> drawn = r.route.path.back();  // (the path keeps a place each 8 pixels, and the end)
            const int dx = at.first - drawn.first, dy = at.second - drawn.second;
            if (at != drawn && (dx * dx + dy * dy >= 64 || r.route.finished)) r.route.path.push_back(at);
            all = all && r.route.finished;
        }
        if (all) done_ = true;
    }

    // The creep of the Zerg bases, as one map of tiles.
    void takeCreep() {
        std::vector<uint8_t> c;
        for (const Base& b : bases_) {
            if (b.race != sim::Zerg || !b.world) continue;
            const std::vector<uint8_t>& cells = b.world->creep().cells();
            if (c.empty()) c.assign(cells.size(), 0);
            for (size_t i = 0; i < cells.size() && i < c.size(); ++i) c[i] |= cells[i];
        }
        if (c != creep_) creep_ = std::move(c), ++creepChanges_;
    }

    // The images of these units, as the bar draws them: each unit's images from the bottom, its first image that is
    // not a shadow as the sprite's main image (order 0). `owner` >= 0: in that player's color.
    void addImages(const ms::World& w, const std::vector<ms::UnitHandle>& units, std::vector<sim::LiveImage>& out, int owner = -1) const {
        for (const ms::UnitHandle& h : units) {
            const ms::UnitState* u = w.get(h);
            if (!u) continue;
            const int elevation = definitions_ ? definitions_->units[size_t(u->type)].elevation : 4;
            int order = 1;
            bool main = false;
            for (const ms::RenderedImage& im : w.renderUnits(std::vector<ms::UnitHandle>{h})) {
                if (im.modifier != 0 && im.modifier != 1 && im.modifier != 10) continue;
                sim::LiveImage li;
                li.image = im.image, li.frame = im.frame, li.flip = im.flip;
                li.x = im.position.x, li.y = im.position.y;
                li.owner = owner >= 0 ? owner : im.owner;
                li.elevation = elevation, li.sortY = u->position.y;
                li.order = !main && im.modifier != 10 ? 0 : order++;
                main = main || li.order == 0;
                out.push_back(li);
            }
        }
    }

    std::unique_ptr<sim::Simulator> classic_;
    sim::FileReader read_;
    std::vector<uint8_t> chk_;
    std::shared_ptr<const ms::Definitions> definitions_;
    std::unique_ptr<ms::World> seed_;
    Mode mode_ = Mode::None;
    std::vector<Base> bases_;
    std::vector<std::unique_ptr<Setup>> setups_;  // each base's setup (Restart and the timeline start from them)
    std::string setupKey_;                        // the map's bases and settings that made them
    bool changed_ = false;                        // a boost search that ended changed a setup
    std::vector<Runner> runners_;
    int frame_ = 0, last_ = 0, warmup_ = 0;
    bool done_ = false;
    std::string error_;
    std::vector<uint8_t> creep_;
    uint64_t creepChanges_ = 1;
};

}  // namespace

std::unique_ptr<sim::Simulator> createBarSimulator() { return std::make_unique<BarEngine>(); }

}  // namespace maplab
