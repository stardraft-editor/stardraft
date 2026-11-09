// SPDX-License-Identifier: MIT
// Asset-backed adapter regression: GAME MAP [--finite-only]. No input files are written.
#include "../map_lab/bar_sim.h"
#include "../sim/sim_data.h"
#include <stardraft/game_data.h>
#include <stardraft/map_file.h>
#include <bit>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <set>
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
struct Digest {
    uint64_t value=14695981039346656037ull;
    void add(uint64_t n){value^=n;value*=1099511628211ull;}
    void real(double n){add(std::bit_cast<uint64_t>(n));}
};
uint64_t images(sim::Simulator& engine) {
    Digest hash;std::vector<sim::LiveImage> images;engine.images(images);
    for(const auto& image:images){hash.add(image.image);hash.add(image.frame);hash.add(image.x);hash.add(image.y);hash.add(image.owner);hash.add(image.flip);}
    return hash.value;
}
void scopedCounts(sim::Simulator& engine) {
    const auto bases=engine.miningResult();const auto guides=engine.miningGuides();int total[3]={};
    for(size_t base=0;base<bases.size();++base)for(int kind=0;kind<3;++kind) {
        const auto status=engine.boostStatusForBase(kind,int(base));total[kind]+=status.found;
        const int workers=bases[base].runs.empty()?0:bases[base].runs[0].workers;
        require(status.found>=0&&status.found<=workers,"per-base findings exceed its workers");
        std::set<int> shown;
        for(const auto& g:guides)if(g.base==int(base)&&g.kind==kind)shown.insert(g.worker);
        require(int(shown.size())<=status.found,"helpers exceed found workers at this base");
        if(kind==0)require(int(shown.size())==status.found,"position helpers differ from per-base findings");
    }
    for(int kind=0;kind<3;++kind)require(total[kind]==engine.boostStatus(kind).found,"map total differs from per-base findings");
    for(const auto& guide:guides)require(guide.base>=0&&guide.base<int(bases.size()),"helper has no base");
}
void income(const std::vector<sim::MiningBase>& bases) {
    for(const auto& base:bases)for(const auto& run:base.runs)if(run.placed) {
        require(run.fieldIncome.size()==base.fields.size(),"field income dimensions");
        require(run.fieldCycleFrames.size()==base.fields.size(),"latest cycle dimensions");
        require(std::all_of(run.fieldCycleFrames.begin(),run.fieldCycleFrames.end(),[](int n){return n>=0;}),"invalid latest cycle");
        require(std::all_of(run.fieldIncome.begin(),run.fieldIncome.end(),[](double n){return std::isfinite(n)&&n>=0;}),"invalid field income");
        require(std::abs(std::accumulate(run.fieldIncome.begin(),run.fieldIncome.end(),0.)-run.mineralsPerMinute)<1e-7,"field income does not sum to delivered base income");
    }
}
void selectedSpawns(sim::Simulator& engine) {
    sim::MiningOptions options;options.workers={0};options.workersPerField=1;options.races={sim::Terran};
    options.placedDepots=false;options.mineralClusters=false;options.continuous=true;
    std::string error;require(engine.startMining(options,error),error);engine.step(1800);
    const auto baseline=engine.miningResult();unsigned available=0;
    for(const auto& b:baseline)if(b.player>=0&&b.player<8)available|=1u<<b.player;
    require(available!=0,"no selectable starting players");
    const unsigned first=available&(~available+1);
    for(unsigned mask:{first,available^first,0u,available}) {
        options.startPlayers=mask;require(engine.startMining(options,error),error);engine.step(1800);
        const auto bases=engine.miningResult();unsigned actual=0;
        for(const auto& b:bases) {
            require(b.player>=0&&b.player<8,"selected base has no player");actual|=1u<<b.player;
            const auto old=std::find_if(baseline.begin(),baseline.end(),[&](const auto& a){return a.player==b.player;});
            require(old!=baseline.end()&&old->runs.size()==b.runs.size(),"selected spawn changed base shape");
            for(size_t k=0;k<b.runs.size();++k) {
                const auto& a=old->runs[k];const auto& now=b.runs[k];
                require(a.tripFrames==now.tripFrames&&a.fieldCycleFrames==now.fieldCycleFrames&&a.mineralsPerMinute==now.mineralsPerMinute,"spawn selection changed mining results");
            }
        }
        require(actual==mask,"unchecked starting locations are still simulated");
        std::vector<sim::LiveImage> pictures;engine.images(pictures);
        for(const auto& image:pictures)if(image.owner>=0&&image.owner<8)require((mask&(1u<<image.owner))!=0,"unchecked spawn has simulated units");
        if(mask==0)require(!engine.startBoost(1,.1,error),"an empty selection can start a search");
    }
    options.startPlayers=first;require(engine.startMining(options,error),error);
    require(engine.startBoost(1,.1,error),error);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(engine.boosting()&&std::chrono::steady_clock::now()<deadline){engine.step(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(!engine.boosting(),"selected spawn search did not finish");engine.step(1800);
    for(const auto& guide:engine.miningGuides())require((first&(1u<<guide.owner))!=0,"search generated a helper for an unchecked spawn");
    scopedCounts(engine);
    std::cout<<"PASS: selected spawns, empty selection, unchanged per-spawn results and restricted searches\n";
}
int main(int argc,char** argv){try {
    if(argc<3){std::cout<<"Usage: mining_workbench_test GAME MAP [--finite-only]\n";return 2;}
    bool finiteOnly=argc>3;
#ifdef MINING_BASELINE_ONLY
    finiteOnly=true;
#endif
    stardraft::GameData data;std::string error;require(data.open(argv[1],error),error);
    stardraft::Bytes chk;require(stardraft::readMapChk(argv[2],chk,error),error);
    const auto files=sim::dataFilesForMap(data,stardraft::Chk::parse(chk));
    const sim::FileReader reader=[&](const std::string& name,std::vector<uint8_t>& out) {
        auto key=name;std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(auto it=files.find(key);it!=files.end()){out=it->second;return true;}
        std::string ignored;return data.readFile(name,out,ignored)||data.readFile(key,out,ignored);
    };
    for(const auto race:{sim::Terran,sim::Protoss,sim::Zerg})for(const int count:{0,18}) {
        auto engine=maplab::createBarSimulator();require(engine->init(reader,error),error);require(engine->setMap(chk,error),error);
        sim::MiningOptions options;options.races={race};options.workers={count};options.workersPerField=count==0?1:0;
        options.mapRaces=false;options.mineralClusters=false;options.placedDepots=false;options.seed=7;
        options.warmupFrames=240;options.frames=1800;
        require(engine->startMining(options,error),error);Digest hash;
        while(!engine->done()) {
            engine->step(60);require(engine->liveError().empty(),engine->liveError());hash.add(engine->frame());
            const auto bases=engine->miningResult();
            for(const auto& base:bases)for(const auto& run:base.runs){hash.add(run.placed);hash.add(run.workers);hash.real(run.mineralsPerMinute);for(double t:run.tripFrames)hash.real(t);}
            std::vector<sim::LiveImage> images;engine->images(images);
            for(const auto& image:images){hash.add(image.image);hash.add(image.frame);hash.add(image.x);hash.add(image.y);hash.add(image.owner);hash.add(image.flip);}
            if(!finiteOnly)income(bases);
        }
        require(engine->frame()==2040,"finite/batch duration changed");
        const auto bases=engine->miningResult();int placed=0;double total=0;
        for(const auto& b:bases)for(const auto& r:b.runs){placed+=r.placed;total+=r.mineralsPerMinute;}
        require(placed>0&&total>0,"no base mined");
        std::cout<<"FINITE race="<<int(race)<<" workers="<<count<<" frame="<<engine->frame()<<" hash="<<std::hex<<hash.value<<std::dec<<" income="<<std::fixed<<std::setprecision(8)<<total<<'\n';
        if(finiteOnly)continue;
        options.continuous=true;options.frames=1;
        require(engine->startMining(options,error),error);
        require(engine->lastFrame()==0,"continuous mining exposes a finite limit");
        require(engine->step(3000)&&!engine->done()&&engine->frame()==3000,"continuous mining ended at the old limit");
        income(engine->miningResult());
        if(count==0)for(int kind=0;kind<3;++kind) {
            const int before=engine->frame();const auto picture=images(*engine);
            require(engine->startBoost(kind,.25,error),error);
            require(engine->frame()==before&&images(*engine)==picture,"starting search reset live units or game time");
            require(engine->step(1)&&engine->frame()==before+1,"mining stopped advancing during a search");
            for(int other=0;other<3;++other)if(other!=kind)require(!engine->boostStatus(other).running,"search started a different boost");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(engine->boosting()&&std::chrono::steady_clock::now()<deadline){engine->step(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            require(!engine->boosting()&&engine->boostStatus(kind).finished&&!engine->boostStatus(kind).failed,"boost did not finish cleanly");
            require(engine->lastFrame()==0,"search restored a finite mining limit");
            require(engine->step(2000)&&!engine->done(),"boosted continuous mining stopped");
            income(engine->miningResult());
            scopedCounts(*engine);
            const int workerType=race==sim::Zerg?41:race==sim::Protoss?64:7;
            for(const auto& guide:engine->miningGuides())require(guide.type==workerType&&guide.owner>=0&&guide.owner<12,"ghost identity lost");
        }
        engine->stop();
    }
    if(!finiteOnly){auto engine=maplab::createBarSimulator();require(engine->init(reader,error),error);require(engine->setMap(chk,error),error);selectedSpawns(*engine);}
    std::cout<<"PASS: mining adapter regression"<<(finiteOnly?" (finite reference records)":" (field income, continuous runs, independent searches and ghost identity)")<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
