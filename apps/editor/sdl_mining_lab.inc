// SPDX-License-Identifier: MIT
// Mining Lab's dock follows the source editor: parallel base tests and three
// independent timed searches. The worlds remain owned by Session workers.
#include "../../tools/map_lab/guide_geometry.h"
#include "../kit/check_box.h"
#include "../kit/label.h"
#include "../kit/list_view.h"
#include "../kit/progress_bar.h"

namespace {
class MiningLabPanel final : public draw::View {
    struct Base { int x, y, owner, type; };
    struct Test {
        Base base;
        int race=1, workers=0;
        bool paused=false;
        std::shared_ptr<maplab::Session> session;
        maplab::Snapshot value;
    };
    Editor* editor_=nullptr;
    uint64_t revision_=0;
    std::vector<Base> bases_;
    std::vector<Test> tests_;
    stardraft::kit::Dropdown base_, race_, workers_, speed_;
    stardraft::kit::Button run_, stopAll_, pause_, close_, copy_, detailsButton_, search_[3];
    stardraft::kit::CheckBox guides_[3];
    stardraft::kit::TextField budget_[3];
    stardraft::kit::ProgressBar progress_[3];
    stardraft::kit::Label stats_, status_[3];
    stardraft::kit::ListView table_;
    bool selecting_=false;
    bool details_=false;
    Test* selected() {
        const int n=base_.value();
        if (n<0 || n>=int(bases_.size())) return nullptr;
        const auto& b=bases_[size_t(n)];
        for (auto& t:tests_) if (t.base.x==b.x && t.base.y==b.y && t.base.owner==b.owner) return &t;
        return nullptr;
    }
    int speed() const { return 1 << std::clamp(speed_.value(),0,2); }
    int allowance(int k) {
        const auto& text=budget_[k].text();
        const int n=std::clamp(text.empty()?15:int(std::wcstol(text.c_str(),nullptr,10)),1,600);
        budget_[k].setText(std::to_wstring(n));
        return n;
    }
    static std::wstring number(double n, int decimals=1) {
        std::wostringstream out;
        out << std::fixed << std::setprecision(decimals) << n;
        return out.str();
    }
    void center(const Base& b) {
        if (!gRoot) return;
        gState.zoom=1.25f;
        gState.camX=float(b.x)-gRoot->map.width()/(2*gState.zoom);
        gState.camY=float(b.y)-gRoot->map.height()/(2*gState.zoom);
        simRedraw();
    }
    void select() {
        if (auto* t=selected()) {
            race_.setValue(t->race);
            workers_.setValue(t->workers);
            const int index=int(t-tests_.data());
            selecting_=true; table_.selectOnly(index); selecting_=false;
        } else table_.clearSelection();
        controls();
    }
    void controls() {
        auto* t=selected();
        const bool live=t && t->value.ready && !t->value.done;
        const bool busy=t && t->value.searching;
        run_.setText(t?L"Stop test":L"Start mining");
        run_.setEnabled(!bases_.empty());
        stopAll_.setEnabled(!tests_.empty());
        race_.setEnabled(!t); workers_.setEnabled(!t);
        pause_.setEnabled(live && !busy);
        pause_.setText(t && t->paused?L"Resume":L"Pause");
        for (int k=0;k<3;++k) {
            const maplab::SearchStatus empty;
            const auto& s=t?t->value.mining.searches[k]:empty;
            search_[k].setEnabled(live && !busy && !s.exhausted);
            search_[k].setText(s.running ? (s.validating?L"Verifying\u2026":L"Searching\u2026") :
                               s.exhausted?L"Exhausted":s.finished?L"Continue":L"Search");
            status_[k].setText(std::to_wstring(s.found)+L" faster"+
                              (s.seconds>0?L" / "+number(s.seconds)+L" s":L""));
            progress_[k].setBusy(s.running && s.validating);
            progress_[k].setValue(float(s.running?s.progress:s.finished?1:0));
        }
    }
    std::wstring report() const {
        std::wostringstream out;
        out << L"Base\tRace\tWorkers\tMinerals/min\tGame seconds\tStatus\r\n";
        for (const auto& t:tests_)
            out << t.base.owner+1 << L": " << t.base.x << L", " << t.base.y << L'\t'
                << race_.items()[size_t(t.race)].label << L'\t' << t.workers << L'\t'
                << number(t.value.mining.income,2) << L'\t' << number(t.value.mining.frame*.042)
                << L'\t' << (t.value.searching?L"Searching":t.paused?L"Paused":L"Running") << L"\r\n";
        if (auto* t=const_cast<MiningLabPanel*>(this)->selected()) {
            out << L"\r\nField X\tField Y\tCycle frames\tMinerals/min\r\n";
            for (const auto& p:t->value.mining.patches)
                out << p.x << L'\t' << p.y << L'\t' << p.lastCycle << L'\t' << number(p.perMinute,2) << L"\r\n";
            const wchar_t* names[]{L"Position boost",L"Return cargo",L"Cycle boost"};
            for (int k=0;k<3;++k) {
                const auto& s=t->value.mining.searches[k];
                out << names[k] << L'\t' << s.found << L" faster\t" << number(s.seconds)
                    << L" seconds\t" << number(s.score,2) << L" minerals/min\r\n";
            }
        }
        return out.str();
    }
    void start();
public:
    bool open=false;
    bool current() const { return gState.ed && editor_==gState.ed.get() && editor_->revision()==revision_; }
    MiningLabPanel() {
        race_.setItems({{L"Zerg"},{L"Terran"},{L"Protoss"}}); race_.setValue(1);
        std::vector<stardraft::kit::DropdownItem> counts{{L"One / field"}};
        for (int n=1;n<=64;++n) counts.push_back({std::to_wstring(n)+L" workers"});
        workers_.setItems(std::move(counts)); workers_.setValue(0);
        speed_.setItems({{L"1x speed"},{L"2x speed"},{L"4x speed"}}); speed_.setValue(0);
        speed_.onChange=[this](int) { for (auto& t:tests_) t.session->speed(t.paused?0:speed()); };
        base_.onChange=[this](int) { select(); if (auto* t=selected()) center(t->base); };
        run_.setText(L"Start mining"); run_.setTone(stardraft::kit::Tone::Primary);
        run_.onClick=[this] {
            if (auto* t=selected()) {
                const auto index=size_t(t-tests_.data());
                tests_.erase(tests_.begin()+index);
                table_.setCount(int(tests_.size())); select(); simRedraw();
            } else start();
        };
        stopAll_.setText(L"Stop all"); stopAll_.onClick=[this] { stop(); };
        pause_.setText(L"Pause"); pause_.onClick=[this] {
            if (auto* t=selected()) { t->paused=!t->paused; t->session->speed(t->paused?0:speed()); controls(); }
        };
        close_.setText(L"Hide"); close_.onClick=[this] { close(); };
        detailsButton_.setText(L"Tests"); detailsButton_.onClick=[this] {
            details_=!details_; table_.setVisible(details_); if(gRoot)gRoot->relayout();
        };
        copy_.setText(L"Copy"); copy_.onClick=[this] { if (host()) host()->setClipboardText(narrow(report())); };
        for (auto* v : std::initializer_list<draw::View*>{&base_,&race_,&workers_,&speed_,&run_,&stopAll_,&pause_,&close_,&copy_,&detailsButton_,&stats_,&table_}) add(*v,{});
        table_.setVisible(false);
        table_.setColumns({{L"Base",230},{L"Race",100},{L"Workers",100},{L"Minerals/min",120,stardraft::kit::ColAlign::Right},
                           {L"Time",90,stardraft::kit::ColAlign::Right},{L"Status",0}});
        table_.setEmptyHint(L"Select a start location or depot, then select Start mining.");
        table_.cell=[this](int row,size_t col) {
            if (row<0 || row>=int(tests_.size())) return std::wstring();
            const auto& t=tests_[size_t(row)];
            switch(col) {
            case 0:return L"P"+std::to_wstring(t.base.owner+1)+L" / "+std::to_wstring(t.base.x)+L", "+std::to_wstring(t.base.y);
            case 1:return race_.items()[size_t(t.race)].label;
            case 2:return t.workers?std::to_wstring(t.workers):std::wstring(L"One / field");
            case 3:return number(t.value.mining.income,0);
            case 4:return number(t.value.mining.frame*.042)+L" s";
            default:return !t.value.error.empty()?widen(t.value.error):!t.value.ready?std::wstring(L"Loading\u2026"):
                        t.value.searching?std::wstring(L"Searching\u2026"):t.paused?std::wstring(L"Paused"):std::wstring(L"Running");
            }
        };
        table_.onSelect=[this] {
            if (selecting_) return;
            const int n=table_.cursor();
            if (n<0 || n>=int(tests_.size())) return;
            const auto& t=tests_[size_t(n)];
            for (size_t i=0;i<bases_.size();++i)
                if (bases_[i].x==t.base.x && bases_[i].y==t.base.y && bases_[i].owner==t.base.owner) {
                    base_.setValue(int(i)); select(); center(t.base); break;
                }
        };
        const wchar_t* names[]{L"Position boost",L"Return cargo",L"Cycle boost"};
        for (int k=0;k<3;++k) {
            guides_[k].setLabel(names[k]); guides_[k].setOn(true);
            guides_[k].setTip(L"Show the guides. Search results are applied with the guides on or off.");
            guides_[k].onChange=[](bool) { simRedraw(); };
            budget_[k].setText(L"15"); budget_[k].setMaxLength(3);
            budget_[k].setFilter([](wchar_t c){return c>=L'0' && c<=L'9';});
            budget_[k].setPlaceholder(L"Seconds");
            budget_[k].onFocusLost=[this,k] { allowance(k); };
            budget_[k].onEnter=[this,k] { beginSearch(k); };
            search_[k].setText(L"Search"); search_[k].onClick=[this,k]{beginSearch(k);};
            status_[k].setTone(stardraft::kit::LabelTone::Dim);
            for (auto* v:std::initializer_list<draw::View*>{&guides_[k],&budget_[k],&search_[k],&status_[k],&progress_[k]}) add(*v,{});
        }
        stats_.setTone(stardraft::kit::LabelTone::Dim);
        stats_.setText(L"Select a base. Each test uses a separate copy of the current map.");
        controls();
    }
    void setup() {
        stop(); editor_=gState.ed.get(); revision_=editor_?editor_->revision():0;
        bases_.clear(); std::vector<stardraft::kit::DropdownItem> items;
        if (editor_) for (const auto& u:editor_->units())
            if (u.type==214 || u.type==106 || u.type==131 || u.type==132 || u.type==133 || u.type==154) {
                bases_.push_back({u.x,u.y,u.owner,u.type});
                items.push_back({L"P"+std::to_wstring(u.owner+1)+L" / "+(u.type==214?L"Start":L"Depot")+
                                 L" / "+std::to_wstring(u.x)+L", "+std::to_wstring(u.y)});
            }
        base_.setItems(std::move(items)); base_.setValue(bases_.empty()?-1:0); select();
    }
    void stop() {
        tests_.clear(); table_.setCount(0); controls(); simRedraw();
        stats_.setText(L"Select a base. Each test uses a separate copy of the current map.");
    }
    void close() {
        open=false; setVisible(false);
        if (gRoot) gRoot->relayout();
    }
    void beginSearch(int k) {
        if (auto* t=selected();t && t->value.ready && !t->value.searching && !t->value.mining.searches[k].exhausted) {
            const int seconds=allowance(k);
            t->session->search(k,seconds); t->value=t->session->snapshot(); controls();
            if (gNoBoxes) std::printf("mining-lab: search kind=%d budget=%d\n",k,seconds);
        }
    }
    void openForUnit(int index) {
        if (!gState.ed || index<0 || index>=int(gState.ed->units().size())) return;
        const auto& unit=gState.ed->units()[size_t(index)];
        for(size_t n=0;n<bases_.size();++n)
            if(bases_[n].x==unit.x && bases_[n].y==unit.y && bases_[n].owner==unit.owner) {
                base_.setValue(int(n)); select();
                if(!selected()) {
                    if(unit.type!=214)race_.setValue(unit.type==106?1:unit.type==154?2:0);
                    start();
                }
                center(bases_[n]); return;
            }
    }
    void update() {
        if (!open && tests_.empty()) return;
        if (!gState.ed || editor_!=gState.ed.get() || editor_->revision()!=revision_ || simBarShown()) {
            if (!tests_.empty()) { stop(); stats_.setText(L"The map changed. Open Mining Lab again to use the current map."); }
            return;
        }
        double income=0;
        for (auto& t:tests_) {
            const bool ready=t.value.ready;
            const auto previous=t.value.mining.searches;
            t.value=t.session->snapshot();
            income+=t.value.mining.income;
            if (gNoBoxes && t.value.ready && !ready) std::printf("map-lab: ready mining\n");
            for(int k=0;k<3;++k)
                if(gNoBoxes && t.value.mining.searches[k].finished &&
                   (!previous[k].finished || t.value.mining.searches[k].seconds>previous[k].seconds+.01))
                    std::printf("mining-lab: finished kind=%d found=%d elapsed=%.2f guides=%zu exhausted=%d\n",k,
                                t.value.mining.searches[k].found,t.value.mining.searches[k].seconds,t.value.mining.guides.size(),
                                t.value.mining.searches[k].exhausted);
        }
        if (!tests_.empty()) {
            auto* t=selected();
            stats_.setText(std::to_wstring(tests_.size())+L" tests / "+number(income,0)+L" minerals/min total / selected: "+
                           std::to_wstring(t?t->value.mining.patches.size():0)+L" fields"+
                           (t && t->value.searching?L" / Selected test paused while searching":L""));
        }
        table_.invalidate(); controls();
        if (!tests_.empty()) simRedraw();
    }
    void paint(draw::Canvas& c) override {
        update(); c.fill({0,0,width(),height()},kPanel); c.fill({0,0,width(),1},kBorder);
        c.text({scale(16),scale(4),scale(130),scale(26)},L"MINING LAB",kText,draw::kMiddle);
        const int pad=scale(16), gap=scale(10), col=(width()-2*pad-2*gap)/3;
        const int y=height()-scale(97);
        for(int k=0;k<3;++k) c.text({pad+k*(col+gap)+col-scale(22),y,pad+k*(col+gap)+col,y+scale(24)},L"s",kDim,draw::kMiddle);
    }
    void resized() override {
        const int pad=scale(16), gap=scale(8), fh=scale(24), bh=scale(28);
        int x=pad,y=scale(32);
        auto field=[&](draw::View& v,int w) { v.setFrame({x,y,x+scale(w),y+bh});x+=scale(w)+gap; };
        field(base_,std::clamp(width()*96/int(gDpi)-474,140,260)); field(race_,100); field(workers_,115); field(run_,110); field(stopAll_,85);
        if (width()<scale(1160)) x=pad,y+=bh+gap;
        field(speed_,100); field(pause_,80); field(copy_,70); field(detailsButton_,70); field(close_,70);
        const int bottom=height()-scale(100);
        stats_.setFrame({pad,y+bh+scale(4),width()-pad,y+bh+scale(28)});
        table_.setFrame({pad,y+bh+scale(30),width()-pad,std::max(y+bh+scale(64),bottom-scale(8))});
        const int cw=(width()-2*pad-2*gap)/3, sy=height()-scale(97);
        for(int k=0;k<3;++k) {
            const int sx=pad+k*(cw+gap);
            guides_[k].setFrame({sx,sy,sx+cw-scale(90),sy+fh});
            budget_[k].setFrame({sx+cw-scale(80),sy,sx+cw-scale(26),sy+fh});
            search_[k].setFrame({sx,sy+scale(32),sx+scale(100),sy+scale(32)+bh});
            status_[k].setFrame({sx+scale(108),sy+scale(32),sx+cw,sy+scale(32)+bh});
            progress_[k].setFrame({sx,sy+scale(70),sx+cw,sy+scale(75)});
        }
    }
    void scene(render::Scene& scene) {
        if (!editor_ || editor_!=gState.ed.get()) return;
        unsigned mask=0; for(int k=0;k<3;++k) if(guides_[k].on()) mask|=1u<<k;
        for(const auto& t:tests_) {
            for(const auto& image:t.value.mining.sprites) {
                render::SpriteDraw d;
                d.x=float(image.x); d.y=float(image.y); d.image=image.image; d.frame=image.frame;
                d.owner=uint8_t(t.base.owner); d.flip=image.flip; d.shadow=image.shadow;
                d.drawFunction=editor_->unitSprites().drawFunction(image.image); scene.sprites.push_back(d);
            }
            maplab::appendMiningGuides(scene,t.value.mining,mask,gState.zoom,[&](const auto& guide) {
                if (guide.type<0 || guide.type>=int(editor_->unitTypes().size())) return;
                auto unit=makeUnit(editor_->unitTypes()[size_t(guide.type)],uint8_t(t.base.owner),uint16_t(guide.x),uint16_t(guide.y));
                addUnitSprites(scene,unit,0,0,.4f);
            });
        }
    }
    int preferredHeight(int clientWidth) const {
        return scale(210+(details_?90:0)+(clientWidth<scale(1160)?36:0));
    }
};
MiningLabPanel gMiningPanel;
void miningPanelScene(render::Scene& scene) { gMiningPanel.scene(scene); }
void MiningLabPanel::start() {
    if (!gState.ed || editor_!=gState.ed.get() || editor_->revision()!=revision_) {
        stats_.setText(L"Open Mining Lab again to use the current map."); return;
    }
    const int index=base_.value();
    if (index<0 || index>=int(bases_.size())) return;
    if (gState.data.unitDataLoader()) { stats_.setText(L"These measurements need the standard Brood War data."); return; }
    try {
        auto data=std::make_shared<GameData>(); std::string error;
        if (!data->open(gInstallInUse,error)) throw std::runtime_error(error);
        auto files=std::make_shared<const std::map<std::string,Bytes>>(simDataFiles());
        maplab::Request request;
        request.scenario=editor_->chk().serialize(); const auto b=bases_[size_t(index)];
        request.start={b.x,b.y}; request.race=race_.value(); request.workers=workers_.value();
        request.read=[data,files](std::string name) {
            std::replace(name.begin(),name.end(),'\\','/');
            for(char& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
            if(auto it=files->find(name);it!=files->end())return it->second;
            Bytes bytes;std::string error;
            if(!data->readFile(name,bytes,error))throw std::runtime_error(error);
            return bytes;
        };
        auto session=std::make_shared<maplab::Session>(std::move(request)); session->speed(speed());
        tests_.push_back({b,race_.value(),workers_.value(),false,std::move(session),{}});
        if(gNoBoxes)std::printf("mining-lab: started tests=%zu base=%d,%d owner=%d\n",tests_.size(),b.x,b.y,b.owner);
        table_.setCount(int(tests_.size())); select(); center(b); gMiningScene=miningPanelScene;
    } catch(const std::exception& e) { stats_.setText(widen(e.what())); }
}
}

void showMiningLabPanel() {
    if (!gRoot || !gState.ed || !gState.ed->isOpen()) return;
    simClose();
    if (!gMiningPanel.current()) gMiningPanel.setup();
    gMiningPanel.open=true;
    if (!gMiningPanel.parent()) gRoot->add(gMiningPanel,{});
    gRoot->relayout(); gMiningPanel.focus();
    if(gNoBoxes)std::printf("map-lab: opened mining\n");
}
void miningPanelFrame() { gMiningPanel.update(); }
void miningLabForUnit(int index) { showMiningLabPanel(); gMiningPanel.openForUnit(index); }
int miningDockHeight(int clientHeight) {
    return gMiningPanel.open?std::min(gMiningPanel.preferredHeight(gRoot?gRoot->width():1600),std::max(scale(210),clientHeight/2)):0;
}
void miningDockLayout(draw::Rect rect,bool visible) {
    gMiningPanel.setFrame(rect); gMiningPanel.setVisible(visible);
}
