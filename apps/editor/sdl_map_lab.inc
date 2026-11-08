// SPDX-License-Identifier: MIT
// Compact Rush widget. Mining Lab uses a separate floating tool window.
#include "../../tools/map_lab/guide_geometry.h"
#include "../../tools/map_lab/session.h"
#include "../kit/label.h"
#include "../kit/list_view.h"
#include "lab_stepper.h"
#include <iomanip>
#include <sstream>
void showMiningLabPanel();
void miningPanelFrame();

namespace {
void labScene(render::Scene&);
class RushWidget final : public draw::View {
    const Editor* editor_=nullptr;
    uint64_t revision_=0;
    bool paused_=false, both_=false, haveStart_=false, haveEnd_=false;
    int consume_=-1, hovered_=-1;
    maplab::RushPoint start_,end_;
    maplab::Snapshot latest_;
    std::unique_ptr<maplab::Session> session_;
    stardraft::kit::Dropdown race_;
    LabStepper speed_;
    stardraft::kit::TextField interval_,retained_;
    class EndpointButton final : public stardraft::kit::Button {
    public:
      draw::Color color = draw::rgb(100, 210, 239);
      void paint(draw::Canvas &c) override {
        stardraft::kit::Button::paint(c);
        const int x = scale(10), y = height() / 2, k = scale(3);
        c.ellipse({x - k, y - k, x + k + 1, y + k + 1}, color, draw::kNone);
      }
    } pickStart_, pickEnd_;
    stardraft::kit::Button choose_, measure_, pause_, clear_, copy_, hide_;
    stardraft::kit::Label status_;
    stardraft::kit::ListView results_;
    std::vector<const maplab::RushPath*> rows_;
    static std::wstring number(double n,int digits=2) {
        std::wostringstream out;out<<std::fixed<<std::setprecision(digits)<<n;return out.str();
    }
    double interval() {
        const auto& text=interval_.text();
        double n=text.empty()?2:std::wcstod(text.c_str(),nullptr);
        n=std::isfinite(n)?std::clamp(n,.5,60.):2;
        n=std::round(n*100)/100;
        auto display=number(n,2);while(display.back()==L'0')display.pop_back();if(display.back()==L'.')display.pop_back();
        interval_.setText(display);return n;
    }
    int retained() {
        const auto& text=retained_.text();
        const int n=std::clamp(text.empty()?10:int(std::wcstol(text.c_str(),nullptr,10)),1,50);
        retained_.setText(std::to_wstring(n));return n;
    }
    int speed() const {return 1<<std::clamp(speed_.value(),0,2);}
    void configure() {
        if(session_){const double seconds=interval();const int keep=retained();session_->configureRush(seconds,keep);
            if(gNoBoxes)std::printf("rush-lab: settings interval=%g retained=%d\n",seconds,keep);}
    }
    static double distance(const maplab::RushPath& path) {
        double value=0;
        for(size_t i=1;i<path.points.size();++i)
            value+=std::hypot(double(path.points[i].x-path.points[i-1].x),double(path.points[i].y-path.points[i-1].y));
        return value;
    }
    std::wstring state(const maplab::RushPath& path) const {
        return path.reached?L"Reached":path.finished?L"Not reached":L"Moving";
    }
    std::wstring pointName(maplab::RushPoint p) const {
      if (!gState.ed)
        return L"";
      const wchar_t *names[]{L"Top left",    L"Top",    L"Top right",
                             L"Left",        L"Center", L"Right",
                             L"Bottom left", L"Bottom", L"Bottom right"};
      const int col = std::clamp(p.x * 3 / (gState.ed->width() * 32), 0, 2),
                row = std::clamp(p.y * 3 / (gState.ed->height() * 32), 0, 2);
      return names[row * 3 + col];
    }
    void controls() {
      pickStart_.setText(haveStart_ ? L"A: " + pointName(start_)
                                    : L"Set start");
      pickEnd_.setText(haveEnd_ ? L"B: " + pointName(end_) : L"Set end");
      pickStart_.setTip(haveStart_
                            ? L"Starting point: " + std::to_wstring(start_.x) +
                                  L", " + std::to_wstring(start_.y)
                            : L"Select the starting point on the map");
      pickEnd_.setTip(haveEnd_ ? L"Destination: " + std::to_wstring(end_.x) +
                                     L", " + std::to_wstring(end_.y)
                               : L"Select the destination on the map");
      measure_.setEnabled(haveStart_ && haveEnd_);
      pause_.setEnabled(session_ && latest_.ready && !latest_.done);
      pause_.setText(paused_ ? L"Resume" : L"Pause");
      clear_.setEnabled(bool(session_) || haveStart_ || haveEnd_);
    }
    void rowSelection() {
        rows_.clear();
        for(const auto& path:latest_.rush.paths) if(path.finished)rows_.push_back(&path);
        const int count=std::clamp(int(std::wcstol(retained_.text().c_str(),nullptr,10)),1,50);
        if(int(rows_.size())>count)rows_.erase(rows_.begin(),rows_.end()-count);
        if(hovered_>=0 && std::none_of(rows_.begin(),rows_.end(),[&](const auto* p){return p->id==hovered_;}))
            for(const auto& path:latest_.rush.paths)if(path.id==hovered_)rows_.push_back(&path);
        results_.setCount(int(rows_.size()));
        for(size_t i=0;i<rows_.size();++i)if(rows_[i]->id==hovered_)results_.selectOnly(int(i));
    }
    void start();
public:
    bool open=false;
    int picking=0;
    RushWidget() {
        race_.setItems({{L"Zerg Drone"},{L"Terran SCV"},{L"Protoss Probe"}});race_.setValue(1);
        speed_.format = [](int n) {
          return std::to_wstring(1 << n) + L"\u00d7";
        };
        speed_.setValue(0);
        speed_.setTip(L"Playback speed. Use minus/plus or the arrow keys.");
        speed_.onChange=[this](int){if(session_)session_->speed(paused_?0:speed());};
        race_.onChange=[this](int){if(session_)start();};
        interval_.setText(L"2");interval_.setMaxLength(5);
        interval_.setFilter([](wchar_t c){return (c>=L'0'&&c<=L'9')||c==L'.';});
        retained_.setText(L"10");retained_.setMaxLength(2);
        retained_.setFilter([](wchar_t c){return c>=L'0'&&c<=L'9';});
        interval_.onFocusLost=[this]{interval();configure();};retained_.onFocusLost=[this]{retained();configure();};
        interval_.onEnter=interval_.onFocusLost;retained_.onEnter=retained_.onFocusLost;
        pickEnd_.color = draw::rgb(240, 174, 109);
        choose_.setText(L"Set endpoints");choose_.onClick=[this]{beginPick();};
        pickStart_.onClick=[this]{stop();both_=false;picking=1;gLabScene=labScene;status_.setText(L"Click the starting point. Esc cancels.");};
        pickEnd_.onClick=[this]{stop();both_=false;picking=2;gLabScene=labScene;status_.setText(L"Click the destination. Esc cancels.");};
        measure_.setText(L"Measure");measure_.setTone(stardraft::kit::Tone::Primary);measure_.onClick=[this]{start();};
        pause_.setText(L"Pause");pause_.onClick=[this]{paused_=!paused_;if(session_)session_->speed(paused_?0:speed());controls();};
        clear_.setText(L"Clear");clear_.onClick=[this]{stop();haveStart_=haveEnd_=false;controls();status_.setText(L"Measurement cleared.");};
        copy_.setText(L"Copy report");
        copy_.setTip(L"Copy run times, traveled distances and results as "
                     L"spreadsheet-ready text");
        copy_.onClick = [this] {
          std::wstring text =
              L"Rush distance / " + race_.items()[size_t(race_.value())].label +
              L"\r\nA: " + std::to_wstring(start_.x) + L", " +
              std::to_wstring(start_.y) + L"\r\nB: " + std::to_wstring(end_.x) +
              L", " + std::to_wstring(end_.y) +
              L"\r\nRun\tSeconds\tPixels\tResult\r\n";
          for (const auto &p : latest_.rush.paths)
            text += std::to_wstring(p.id) + L'\t' + number(p.seconds()) +
                    L'\t' + number(distance(p)) + L'\t' + state(p) + L"\r\n";
          if (host())
            host()->setClipboardText(narrow(text));
        };
        hide_.setText(L"Hide");hide_.onClick=[this]{hide();};
        status_.setTone(stardraft::kit::LabelTone::Dim);
        results_.setColumns({{L"Run",48},{L"Seconds",80,stardraft::kit::ColAlign::Right},
                             {L"Pixels",86,stardraft::kit::ColAlign::Right},{L"Result",0}});
        results_.setEmptyHint(L"Waiting for completed trips. Hover a trail to inspect it.");
        results_.cell=[this](int row,size_t col){
            if(row<0||row>=int(rows_.size()))return std::wstring();
            const auto& p=*rows_[size_t(row)];
            return col==0?std::to_wstring(p.id):col==1?number(p.seconds()):col==2?number(distance(p)):state(p);
        };
        results_.onSelect=[this]{const int n=results_.cursor();if(n>=0&&n<int(rows_.size())){hovered_=rows_[size_t(n)]->id;simRedraw();}};
        for(auto* v:std::initializer_list<draw::View*>{&race_,&speed_,&interval_,&retained_,&choose_,&pickStart_,&pickEnd_,
                                                     &measure_,&pause_,&clear_,&copy_,&hide_,&status_,&results_})add(*v,{});
        controls();
    }
    bool current() const {return gState.ed&&editor_==gState.ed.get()&&editor_->revision()==revision_;}
    void setup(bool=true) {
        stop();editor_=gState.ed.get();revision_=editor_?editor_->revision():0;
        haveStart_=haveEnd_=false;
        if(editor_)for(const auto& u:editor_->units())if(u.type==214){
            if(!haveStart_){start_={u.x,u.y};haveStart_=true;}else{end_={u.x,u.y};haveEnd_=true;break;}
        }
        status_.setText(L"Select two open points. Times use Fastest speed.");controls();
    }
    void stop() {
        session_.reset();latest_={};rows_.clear();results_.setCount(0);
        picking=0;both_=false;paused_=false;hovered_=-1;
        gLabScene=nullptr;controls();simRedraw();
    }
    void hide(){picking=0;if(host())host()->closeWindow(*this);open=false;}
    bool closeRequested() override {picking=0;open=false;return true;}
    bool key(const draw::KeyEvent& e) override {
        if(e.down&&e.key==draw::Key::Escape){if(picking)picking=0;else hide();return true;}return false;
    }
    void beginPick(){stop();haveStart_=haveEnd_=false;both_=true;picking=1;gLabScene=labScene;controls();status_.setText(L"Click the starting point. Esc cancels.");}
    void point(int x,int y) {
        if(picking==1){start_={x,y};haveStart_=true;if(both_){picking=2;status_.setText(L"Click the destination.");controls();return;}}
        else {end_={x,y};haveEnd_=true;}
        picking=0;both_=false;controls();if(haveStart_&&haveEnd_)start();
    }
    void update() {
        if(!current()){if(session_||picking)stop();return;}
        if(simBarShown()){if(session_)stop();return;}
        if(!session_)return;
        const bool ready=latest_.ready;
        const bool hadResult=std::any_of(latest_.rush.paths.begin(),latest_.rush.paths.end(),[](const auto& p){return p.finished;});
        latest_=session_->snapshot();
        if(gNoBoxes&&latest_.ready&&!ready)std::printf("map-lab: ready rush\n");
        if(!latest_.error.empty()){const auto error=widen(latest_.error);stop();status_.setText(error);return;}
        if(!latest_.ready)return;
        rowSelection();double sum=0;int count=0;
        for(const auto& p:latest_.rush.paths)if(p.reached){sum+=p.seconds();++count;}
        if(gNoBoxes&&!hadResult&&count)std::printf("rush-lab: completed=%d average=%.2f\n",count,sum/count);
        status_.setText(count?L"Average: "+number(sum/count)+L" s / "+std::to_wstring(count)+L" arrivals":
                             paused_?L"Playback paused.":L"Measuring / Fastest / Hover a trail to inspect it.");
        results_.invalidate();controls();simRedraw();
    }
    void paint(draw::Canvas& c) override {
        update();c.fill({0,0,width(),height()},kPanel);
        c.text({scale(16),scale(8),scale(230),scale(32)},L"RUSH DISTANCE",kText,draw::kMiddle);
        c.text({scale(300),scale(20),scale(400),scale(44)},L"Launch (s)",kDim,draw::kMiddle);
        c.text({width()-scale(70),scale(20),width()-scale(16),scale(44)},L"Keep",kDim,draw::kMiddle);
    }
    void resized() override {
        const int p=scale(16),gap=scale(8),fh=scale(24),bh=scale(28),w=width();
        race_.setFrame({p,scale(44),scale(160),scale(44)+fh});
        speed_.setFrame({scale(168),scale(44),scale(280),scale(44)+fh});
        interval_.setFrame({scale(300),scale(44),scale(370),scale(44)+fh});
        retained_.setFrame({w-scale(70),scale(44),w-p,scale(44)+fh});
        choose_.setFrame({p,scale(80),scale(164),scale(80)+bh});
        pickStart_.setFrame({scale(172),scale(80),(w+scale(172))/2-gap/2,scale(80)+bh});
        pickEnd_.setFrame({(w+scale(172))/2+gap/2,scale(80),w-p,scale(80)+bh});
        status_.setFrame({p,scale(112),w-p,scale(138)});
        results_.setFrame({p,scale(142),w-p,height()-scale(52)});
        int x=p;for(auto* b:{&measure_,&pause_,&clear_,&copy_,&hide_}){b->setFrame({x,height()-p-bh,x+scale(88),height()-p});x+=scale(88)+gap;}
    }
    bool mapMouse(const draw::MouseEvent& e) {
        if(e.kind==draw::MouseKind::Up&&e.button==consume_){consume_=-1;return true;}
        if(picking){
            if(e.kind==draw::MouseKind::Down&&(e.button==0||e.button==1)){
                consume_=e.button;
                if(e.button==1){picking=0;both_=false;status_.setText(L"Point selection canceled.");return true;}
                float x,y;toWorld(gState,float(e.pt.x),float(e.pt.y),x,y);
                if(gState.ed&&x>=0&&y>=0&&x<gState.ed->width()*32&&y<gState.ed->height()*32)point(int(x),int(y));
                else status_.setText(L"Click a point inside the map.");
                return true;
            }
            return e.button!=2&&(e.kind==draw::MouseKind::Down||e.kind==draw::MouseKind::DoubleClick);
        }
        if(e.kind==draw::MouseKind::Move&&session_){
            float x,y;toWorld(gState,float(e.pt.x),float(e.pt.y),x,y);
            double nearest=9/std::max(.01f,gState.zoom);int hover=-1;
            const int keep=std::clamp(int(std::wcstol(retained_.text().c_str(),nullptr,10)),1,50);
            for(const auto& p:latest_.rush.paths)if(maplab::showRushTrail(p,latest_.rush,keep))
                for(size_t i=1;i<p.points.size();++i){
                    const auto a=p.points[i-1],b=p.points[i];const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
                    const double t=length?std::clamp(((x-a.x)*dx+(y-a.y)*dy)/length,0.,1.):0;
                    const double d=std::hypot(x-a.x-t*dx,y-a.y-t*dy);
                    if(d<=nearest){nearest=d;hover=p.id;}
                }
            if(hover!=hovered_){hovered_=hover;rowSelection();simRedraw();if(gNoBoxes&&hover>=0)std::printf("rush-lab: hover run=%d\n",hover);}
        }
        return false;
    }
    void scene(render::Scene& scene) {
        if(!current())return;
        auto marker=[&](maplab::RushPoint point,const char* label){
            const float half=5/std::max(.01f,gState.zoom);
            render::RectDraw box;box.x0=float(point.x)-half;box.y0=float(point.y)-half;box.x1=float(point.x)+half;box.y1=float(point.y)+half;
            box.rgba = label[0] == 'A' ? 0x64D2EFFF : 0xF0AE6DFF;
            scene.rects.push_back(box);
            scene.texts.push_back(
                {float(point.x),
                 float(point.y) - 16 / std::max(.01f, gState.zoom), label,
                 box.rgba, true, 1.5f, true});
        };
        if(haveStart_)marker(start_,"A");if(haveEnd_)marker(end_,"B");
        const int keep=std::clamp(int(std::wcstol(retained_.text().c_str(),nullptr,10)),1,50);
        for(const auto& path:latest_.rush.paths){
            for(const auto& image:path.sprites){
                render::SpriteDraw d;d.x=float(image.x);d.y=float(image.y);d.image=image.image;d.frame=image.frame;
                d.owner=uint8_t(image.owner);d.flip=image.flip;d.shadow=image.shadow;d.alpha=.45f;
                d.drawFunction=gState.ed->unitSprites().drawFunction(image.image);scene.sprites.push_back(d);
            }
            if(maplab::showRushTrail(path,latest_.rush,keep)){
                const size_t stride=std::max<size_t>(1,path.points.size()/512);
                for(size_t i=1;i<path.points.size();i+=stride){
                    const auto a=path.points[i-1],b=path.points[std::min(i+stride-1,path.points.size()-1)];
                    render::RectDraw line;line.x0=float(a.x);line.y0=float(a.y);line.x1=float(b.x);line.y1=float(b.y);
                    line.line=true;line.thickness=path.id==hovered_?3.f:1.f;line.rgba=maplab::workerGuideColor(path.id,125,path.id==hovered_?255:135);
                    scene.rects.push_back(line);
                }
            }
            if((!path.finished||path.id==hovered_)&&!path.points.empty()){
                const auto p=path.points.back();
                scene.texts.push_back(
                    {float(p.x) + 8 / std::max(.01f, gState.zoom),
                     float(p.y) - 22 / std::max(.01f, gState.zoom),
                     (path.id == hovered_
                          ? "#" + std::to_string(path.id) + " / "
                          : std::string()) +
                         narrow(number(path.seconds())) + " s",
                     0xC8F5E8FF, true, 1.5f, true});
            }
        }
    }
};
RushWidget gMapLab;
void labScene(render::Scene& scene){gMapLab.scene(scene);}
void RushWidget::start(){
    if(!current()||!haveStart_||!haveEnd_)return;
    stop();
    if(gState.data.unitDataLoader()){status_.setText(L"These measurements need the standard Brood War data.");return;}
    try{
        auto data=std::make_shared<GameData>();std::string error;
        if(!data->open(gInstallInUse,error))throw std::runtime_error(error);
        auto files=std::make_shared<const std::map<std::string,Bytes>>(simDataFiles());
        maplab::Request request;request.mode=maplab::Mode::Rush;request.scenario=editor_->chk().serialize();
        request.start=start_;request.end=end_;request.race=race_.value();request.launchInterval=interval();request.retained=retained();
        request.read=[data,files](std::string name){
            std::replace(name.begin(),name.end(),'\\','/');for(char& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
            if(auto it=files->find(name);it!=files->end())return it->second;
            Bytes bytes;std::string error;if(!data->readFile(name,bytes,error))throw std::runtime_error(error);return bytes;
        };
        session_=std::make_unique<maplab::Session>(std::move(request));session_->speed(speed());gLabScene=labScene;
        status_.setText(L"Loading the map and game data\u2026");controls();
    }catch(const std::exception& e){status_.setText(widen(e.what()));}
}
void showRush(){
    if(!gRoot||!gHost||!gState.ed||!gState.ed->isOpen())return;
    simClose();if(!gMapLab.current())gMapLab.setup();if(gMapLab.open)return;
    gMapLab.open=true;
    draw::WindowSpec spec{draw::WindowKind::Tool,{scale(40),scale(120),scale(560),scale(420)},L"Rush distance"};
    if(gNoBoxes)gHost->snapshotInput(*gRoot,gRoot->width(),gRoot->height(),{});
    gHost->openWindow(*gRoot,gMapLab,spec,[]{gMapLab.open=false;gMapLab.picking=0;});
    if(gNoBoxes)std::printf("map-lab: opened rush\n");
}
}
void miningLabCommand(){showMiningLabPanel();}
void rushMeasureCommand(){showRush();}
void mapLabFrame(){gMapLab.update();miningPanelFrame();}
bool mapLabKey(const draw::KeyEvent& e){
    if(gMapLab.picking&&e.down&&e.key==draw::Key::Escape){gMapLab.picking=0;setMessage(L"Point selection canceled");return true;}return false;
}
bool mapLabMouse(const draw::MouseEvent& e){return gMapLab.mapMouse(e);}
