// The simulation bar's engine (bar_sim.h). Mining and rush distance use the Map Simulation engine with the rules of
// its Mining Lab and Rush adapters (pull request 260): each base, and each rush route, is a world of its own, copied
// from one world of the map. Combat, and the bases of a map, come from StarDraft's own engine (tools/sim).
#include "bar_sim.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
// Rush: a worker that has not moved for 480 frames stops; no walk is longer than 24000 frames; the worker is at the
// end within 1 pixel; it starts within 8 pixels of its start.
constexpr int kRushStall = 480, kRushMost = 24000, kRushStartSlack = 8;
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
        stop();
        if (chk != chk_) seed_.reset();  // (the same map again, as Restart has it: the same world)
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
    bool beginMining(const sim::MiningOptions& o, std::string& error, bool firstBases) {
        stop();
        if (o.gas) return error = "The mining simulation has no gas", false;
        std::vector<sim::Race> races;
        std::vector<sim::MiningBase> bases = classic_->miningBases(o, races);
        if (bases.empty()) return error = "The map has no base to mine at", false;
        if (firstBases && bases.size() > size_t(kMaxBases)) bases.resize(size_t(kMaxBases)), races.resize(size_t(kMaxBases));
        try {
            if (!loadSeed(error)) return false;
            const int count = o.workers.empty() ? 9 : o.workers[0];
            std::string first;
            bool any = false;
            for (size_t k = 0; k < bases.size(); ++k) {
                // (a world's players are 0 to 11: past the 12th base, each base is a world of its own anyway)
                const int owner = int(k % size_t(kMaxBases));
                try {
                    bases_.push_back(makeBase(bases[k], races[k], owner, count));
                    any = true;
                } catch (const std::exception& e) {  // (that base has no numbers: the others still mine)
                    if (first.empty()) first = e.what();
                    Base b;
                    b.base = bases[k], b.race = races[k], b.owner = owner;
                    bases_.push_back(std::move(b));
                }
            }
            if (!any) return bases_.clear(), error = first, false;
        } catch (const std::exception& e) {
            bases_.clear();
            return error = e.what(), false;
        }
        warmup_ = o.warmupFrames, last_ = o.warmupFrames + o.frames;
        mode_ = Mode::Mining;
        return true;
    }

    // Rush distance: from each start location to each other one, a worker of the start location's race (a world for
    // each route), at the same time.
    bool startRush(const sim::RushOptions& o, std::string& error) override {
        stop();
        sim::MiningOptions starts;
        starts.placedDepots = false, starts.mineralClusters = false;
        starts.races = o.races, starts.mapRaces = o.mapRaces, starts.seed = o.seed;
        std::vector<sim::Race> races;
        const std::vector<sim::MiningBase> bases = classic_->miningBases(starts, races);
        if (bases.size() < 2) return error = "The map has fewer than 2 start locations", false;
        try {
            if (!loadSeed(error)) return false;
            for (size_t a = 0; a < bases.size(); ++a)
                for (size_t b = 0; b < bases.size(); ++b) {
                    if (a == b) continue;
                    Runner r;
                    r.route.from = bases[a].player, r.route.to = bases[b].player;
                    r.route.fromX = bases[a].x, r.route.fromY = bases[a].y, r.route.toX = bases[b].x, r.route.toY = bases[b].y;
                    r.route.race = races[a];
                    r.world = std::make_unique<ms::World>(*seed_);
                    r.worker = r.world->createFinished(workerOf(races[a]), {bases[a].x, bases[a].y}, 0);
                    const ms::UnitState* u = r.world->get(r.worker);
                    if (!u || !u->visible || u->dead || std::abs(u->position.x - bases[a].x) > kRushStartSlack ||
                        std::abs(u->position.y - bases[a].y) > kRushStartSlack)
                        continue;  // (a blocked start location: no walk from it)
                    r.route.path.push_back({u->position.x, u->position.y});
                    r.last = r.route.path.back();
                    r.world->move(r.worker, {bases[b].x, bases[b].y});
                    runners_.push_back(std::move(r));
                }
        } catch (const std::exception& e) {
            runners_.clear();
            return error = e.what(), false;
        }
        if (runners_.empty()) return error = "A worker cannot stand at any start location", false;
        last_ = kRushMost;
        mode_ = Mode::Rush;
        return true;
    }

    bool live() const override { return mode_ == Mode::Classic ? classic_->live() : mode_ != Mode::None; }
    bool step(int frames) override {
        if (mode_ == Mode::Classic) return classic_->step(frames);
        if (mode_ == Mode::None || done_) return false;
        try {
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
    std::vector<sim::RushRoute> rushResult() const override {
        std::vector<sim::RushRoute> out;
        for (const Runner& r : runners_) out.push_back(r.route);
        return out;
    }
    std::vector<sim::MiningBase> miningBases(const sim::MiningOptions& o, std::vector<sim::Race>& races) const override {
        return classic_->miningBases(o, races);
    }
    void stop() override {
        if (classic_) classic_->stop();
        mode_ = Mode::None;
        bases_.clear(), runners_.clear();
        frame_ = 0, last_ = 0, warmup_ = 0, done_ = false;
        error_.clear();
        creep_.clear(), ++creepChanges_;
    }

private:
    enum class Mode { None, Classic, Mining, Rush };
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
    };
    struct Runner {
        std::unique_ptr<ms::World> world;
        ms::UnitHandle worker;
        sim::RushRoute route;
        std::pair<int, int> last;  // the worker's place in the last frame
        int stalled = 0;
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
    Base makeBase(const sim::MiningBase& at, sim::Race race, int owner, int count) {
        Base b;
        b.base = at, b.race = race, b.owner = owner;
        b.world = std::make_unique<ms::World>(*seed_);
        ms::World& world = *b.world;
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
        count = count > 0 ? count : int(b.patches.size());
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
            b.workers.push_back({workers[i], patch, patch});
        }
        world.prepareNavigation(workers.front());
        return b;
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
            if (at != before) {
                r.route.distance += std::hypot(double(at.first - before.first), double(at.second - before.second));
                r.last = at, r.stalled = 0;
            } else {
                ++r.stalled;
            }
            r.route.reached = std::hypot(double(at.first - r.route.toX), double(at.second - r.route.toY)) <= 1;
            r.route.finished = r.route.reached || r.stalled >= kRushStall || r.route.frames >= kRushMost;
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
