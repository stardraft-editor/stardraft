// SPDX-License-Identifier: MIT
// Static terrain regions from the same independent engine as Mining Lab/Rush.
#include "region_overlay_data.h"
#include "../kit/slider.h"
#include "../kit/check_box.h"

namespace {
struct RegionKey {
    const Editor* editor=nullptr;
    uint64_t terrain=0;
    int era=0,width=0,height=0;
    bool operator==(const RegionKey&) const = default;
};
RegionKey regionKey() {
    const auto* e=gState.ed.get();
    return e&&e->isOpen()?RegionKey{e,e->terrainRevision(),e->era(),e->width(),e->height()}:RegionKey{};
}
struct RegionCache {
    std::future<std::shared_ptr<regionoverlay::Data>> job;
    RegionKey jobKey,readyKey,failedKey;
    std::shared_ptr<regionoverlay::Data> ready;
    std::string error;
    unsigned started=0,discarded=0;
    const regionoverlay::Data* current() const {
        return ready&&readyKey.editor&&readyKey==regionKey()?ready.get():nullptr;
    }
    void poll(bool wait=false) {
        for(;;) {
            const auto key=regionKey();
            if(job.valid()) {
                if(!wait&&job.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
                try {
                    auto value=job.get();
                    if(jobKey==key&&key.editor) {ready=std::move(value);readyKey=key;error.clear();failedKey={};}
                    else ++discarded;
                } catch(const std::exception& e) {
                    if(jobKey==key){failedKey=key;error=e.what();ready.reset();}
                }
                gState.dirty=true;
            }
            if(!key.editor||!gPathing.on||gPathing.areas||current()||failedKey==key)return;
            const auto& e=*gState.ed;
            regionoverlay::Input input{e.terrain(),e.tileset().groups,e.tileset().minitileFlags};
            jobKey=key;++started;
            job=std::async(std::launch::async,[input=std::move(input)] {
                const auto start=std::chrono::steady_clock::now();
                auto value=std::make_shared<regionoverlay::Data>(regionoverlay::build(input));
                value->milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                return value;
            });
            if(!wait)return;
        }
    }
} gRegionCache;

uint32_t engineRegionColor(int id,int alpha) {
    return (pathingTint(id)&0xffffff00u)|uint32_t(std::clamp(alpha,0,255));
}
void engineRegionViewport(render::Scene& scene,int w,int h) {
    gRegionCache.poll();
    const auto* data=gRegionCache.current();
    if(!data||!regionOpacity())return;
    const auto& cam=scene.camera;
    const float x0=cam.x,y0=cam.y,x1=x0+w/cam.zoom,y1=y0+h/cam.zoom;
    const int fillAlpha=(regionOpacity()*255+50)/100;
    // Borders are clearer than the fill, but still disappear at opacity zero.
    const int edgeAlpha=std::min(255,fillAlpha*2);
    static std::vector<uint32_t> colors;
    static int colorOpacity=-1,colorTheme=-1;
    if(colors.size()!=data->regions.size()||colorOpacity!=regionOpacity()||colorTheme!=int(gLightTheme)) {
        colors.resize(data->regions.size());colorOpacity=regionOpacity();colorTheme=int(gLightTheme);
        for(size_t i=0;i<colors.size();++i)colors[i]=engineRegionColor(int(i),fillAlpha);
    }
    for(const auto& run:data->runs) {
        const float x=run.x*8.f,y=run.y*8.f,end=(run.x+run.length)*8.f;
        if(end<=x0||x>=x1||y+8<=y0||y>=y1)continue;
        render::RectDraw d;d.x0=x;d.y0=y;d.x1=end;d.y1=y+8;d.fill=true;d.rgba=colors[size_t(run.region)];
        scene.rects.push_back(d);
    }
    if(cam.zoom*8>=3)
        for(const auto& edge:data->edges) {
            const float ax=edge.x0*8.f,ay=edge.y0*8.f,bx=edge.x1*8.f,by=edge.y1*8.f;
            if(bx<x0||ax>x1||by<y0||ay>y1)continue;
            render::RectDraw d;d.x0=ax;d.y0=ay;d.x1=bx;d.y1=by;d.line=true;d.thickness=1;d.rgba=(colors[size_t(edge.region)]&0xffffff00u)|uint32_t(edgeAlpha);
            scene.rects.push_back(d);
        }
}
void engineRegionScene(render::Scene& scene) {
    int w,h;viewSize(w,h);engineRegionViewport(scene,w,h);
}
std::wstring engineRegionHover(float x,float y) {
    const auto* data=gRegionCache.current();
    if(!data)return {};
    const int id=data->at(int(std::floor(x/8)),int(std::floor(y/8)));
    if(id<0)return tr(L"Pathing: units cannot walk here");
    const auto& r=data->regions[size_t(id)];
    return tr(L"Pathing region ")+std::to_wstring(id)+L" \u00b7 "+std::to_wstring(r.tileCount)+tr(L" tiles \u00b7 Connected area ")+std::to_wstring(r.group);
}
class RegionOptions final : public draw::View {
    std::wstring status_;
public:
    bool open=false;
    kit::CheckBox visible;
    kit::Slider opacity{0,100,35};
    RegionOptions() {
        visible.setLabel(L"Show regions");
        visible.setTip(L"Show the terrain regions used by the simulation engine");
        visible.onChange=[this](bool on) {
            if(on)pathingLayerMode(false);
            else if(gPathing.on&&!gPathing.areas)pathingLayerToggle();
            shellSettingsChanged();refresh();
            if(gNoBoxes)std::printf("region-overlay: visible=%d\n",int(on));
        };
        opacity.setTip(L"Region opacity, from 0 to 100 percent");
        opacity.onChange=[this](int value){setRegionOpacity(value);shellSettingsChanged();gState.dirty=true;invalidate();
            if(gNoBoxes)std::printf("region-overlay: opacity=%d\n",value);};
        add(visible,{});add(opacity,{});
    }
    void refresh() {
        visible.setOn(gPathing.on&&!gPathing.areas);
        if(visible.enabled()!=(regionKey().editor!=nullptr))visible.setEnabled(regionKey().editor!=nullptr);
        opacity.setValue(regionOpacity());
        std::wstring status;
        if(!regionKey().editor)status=tr(L"Open a map to show its regions.");
        else if(!gPathing.on||gPathing.areas)status=tr(L"Turn on regions to show terrain boundaries.");
        else if(gRegionCache.failedKey==regionKey()&&!gRegionCache.error.empty())status=tr(L"Regions unavailable: ")+widen(gRegionCache.error);
        else if(const auto* data=gRegionCache.current())status=std::to_wstring(std::count_if(data->regions.begin(),data->regions.end(),[](const auto& r){return r.walkable();}))+tr(L" walkable regions \u00b7 Terrain only");
        else status=tr(L"Updating regions\u2026");
        if(status_!=status){status_=std::move(status);invalidate();}
    }
    void resized() override {
        visible.setFrame({scale(12),scale(10),width()-scale(12),scale(34)});
        opacity.setFrame({scale(76),scale(40),width()-scale(53),scale(64)});
    }
    void paint(draw::Canvas& c) override {
        refresh();c.fill({0,0,width(),height()},kPanel);
        c.text({scale(12),scale(40),scale(76),scale(64)},tr(L"Opacity"),kText,draw::kMiddle);
        c.text({width()-scale(49),scale(40),width()-scale(12),scale(64)},std::to_wstring(regionOpacity())+L"%",kText,draw::kRight|draw::kMiddle);
        c.text({scale(12),scale(76),width()-scale(12),scale(100)},status_,kDim,draw::kMiddle|draw::kEllipsis);
    }
    std::wstring tip(draw::Point) override {
        return gRegionCache.failedKey==regionKey()?widen(gRegionCache.error):tr(L"Static terrain regions. Buildings and units affect movement separately.");
    }
} gRegionOptions;

void openRegionOptions() {
    if(gRegionOptions.open||!gHost||!gRoot)return;
    gRegionOptions.open=true;gRegionOptions.setSize(scale(320),scale(112));gRegionOptions.refresh();
    draw::WindowSpec spec{draw::WindowKind::Tool,{scale(40),scale(120),scale(360),scale(232)},tr(L"Pathfinding regions")};
    if(gNoBoxes)gHost->snapshotInput(*gRoot,gRoot->width(),gRoot->height(),{});
    gHost->openWindow(*gRoot,gRegionOptions,spec,[]{gRegionOptions.open=false;});
    if(gNoBoxes)std::printf("region-overlay: options opened\n");
}
}
void regionOverlayInstall() {
    gEngineRegionScene=engineRegionScene;gEngineRegionHover=engineRegionHover;gShowRegionOptions=openRegionOptions;
}
void regionOverlayFrame() {
    if(gPathing.on&&!gPathing.areas)gRegionCache.poll();
    if(gRegionOptions.open)gRegionOptions.refresh();
}
bool regionOverlayBusy() {
    return gPathing.on&&!gPathing.areas&&gRegionCache.job.valid();
}
void regionOverlayTrace() {
    gRegionCache.poll(true);
    const auto* data=gRegionCache.current();uint64_t hash=14695981039346656037ull;
    if(data)for(int id:data->cells){hash^=uint32_t(id);hash*=1099511628211ull;}
    std::printf("region-overlay: visible=%d areas=%d opacity=%d ready=%d cells=%016llx builds=%u discarded=%u ms=%.3f\n",
        int(gPathing.on),int(gPathing.areas),regionOpacity(),int(data!=nullptr),
        (unsigned long long)(data?hash:0),gRegionCache.started,gRegionCache.discarded,data?data->milliseconds:0);
}

bool regionOverlayCheck(const std::string& name,std::string& out) {
    if(name!="region-overlay")return false;
    if(!regionKey().editor){out="region-overlay: open a map (WRONG)\n";return true;}
    const auto bytes=gState.ed->chk().serialize();const auto revision=gState.ed->revision();
    const auto oldOn=gPathing.on,oldAreas=gPathing.areas;const int oldOpacity=regionOpacity();
    int checks=0,failed=0;
    auto test=[&](bool ok,const char* why){++checks;if(!ok){++failed;out+=std::string("region-overlay: ")+why+" (WRONG)\n";}};
    gRegionOptions.visible.onChange(true);gRegionCache.poll(true);
    const auto* data=gRegionCache.current();
    test(data!=nullptr,"engine result missing");
    if(data) {
        test(data->width==gState.ed->width()*4&&data->height==gState.ed->height()*4,"raster dimensions");
        test(!data->runs.empty()&&!data->edges.empty(),"geometry missing");
        regionoverlay::Input input{gState.ed->terrain(),gState.ed->tileset().groups,gState.ed->tileset().minitileFlags};
        auto expected=regionoverlay::build(input);
        test(expected.cells==data->cells,"region IDs differ from shared engine");
        const auto started=gRegionCache.started;gRegionCache.poll(true);test(gRegionCache.started==started,"unchanged terrain rebuilt");
        setRegionOpacity(0);render::Scene hidden;hidden.camera={0,0,1};engineRegionViewport(hidden,960,800);test(hidden.rects.empty(),"zero opacity draws");
        setRegionOpacity(35);render::Scene shown;shown.camera={0,0,.25f};engineRegionViewport(shown,960,800);test(!shown.rects.empty(),"zoomed-out geometry missing");
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<100;++i){render::Scene frame;frame.camera=shown.camera;engineRegionViewport(frame,960,800);}
        const double drawMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100;
        out+="region-overlay-perf: generation="+std::to_string(data->milliseconds)+" ms, scene="+std::to_string(drawMs)+" ms, draws="+std::to_string(shown.rects.size())+"\n";
        const int w=gState.ed->width()*32,h=gState.ed->height()*32;
        render::Scene bottom;bottom.camera={float(w/2),float(h/2),1};engineRegionViewport(bottom,960,800);
        bool wanted=false;
        const int vw=960,vh=800;
        for(const auto& run:data->runs)wanted|=run.x*8.f<bottom.camera.x+vw&& (run.x+run.length)*8.f>bottom.camera.x&&run.y*8.f<bottom.camera.y+vh&&run.y*8.f+8>bottom.camera.y;
        test(!wanted||!bottom.rects.empty(),"bottom/right map clipping");
        const auto original=regionKey();auto changed=original;++changed.terrain;test(!(changed==original),"terrain revision key");
        changed=original;++changed.era;test(!(changed==original),"tileset key");
        changed=original;++changed.width;test(!(changed==original),"dimensions key");
        // A finished result for a superseded revision must never become current.
        const auto discarded=gRegionCache.discarded;
        auto saved=gRegionCache.ready;gRegionCache.ready.reset();gRegionCache.jobKey=original;--gRegionCache.jobKey.terrain;
        gRegionCache.job=std::async(std::launch::deferred,[saved]{return saved;});
        test(regionOverlayBusy(),"pending job does not request frames");
        gRegionCache.poll(true);test(gRegionCache.discarded==discarded+1&&gRegionCache.current(),"obsolete result published");
        test(!regionOverlayBusy(),"completed job keeps requesting frames");
        gRegionCache.jobKey=original;
        gRegionCache.job=std::async(std::launch::deferred,[]()->std::shared_ptr<regionoverlay::Data>{throw std::runtime_error("Region test failure");});
        gRegionCache.poll(true);
        test(!gRegionCache.current()&&!gRegionCache.error.empty(),"worker failure leaves stale data");
        const auto attempts=gRegionCache.started;gRegionCache.poll(true);
        test(gRegionCache.started==attempts&&!regionOverlayBusy(),"worker failure retries without a new input");
        gRegionCache.failedKey={};gRegionCache.error.clear();gRegionCache.poll(true);
        test(gRegionCache.current()!=nullptr,"cache cannot recover after failure");
    }
    setRegionOpacity(35);gRegionOptions.refresh();
    draw::KeyEvent key;key.down=true;key.key=draw::Key::End;gRegionOptions.opacity.key(key);test(regionOpacity()==100,"slider End");
    key.key=draw::Key::Home;gRegionOptions.opacity.key(key);test(regionOpacity()==0,"slider Home");
    key.key=draw::Key::Right;gRegionOptions.opacity.key(key);test(regionOpacity()==1,"slider keyboard step");
    setRegionOpacity(-99);test(regionOpacity()==0,"negative opacity");setRegionOpacity(999);test(regionOpacity()==100,"large opacity");
    const auto started=gRegionCache.started;gRegionOptions.visible.onChange(false);gRegionCache.poll(true);
    test(!gPathing.on&&gRegionCache.started==started,"disabled layer rebuilds");
    gRegionOptions.visible.onChange(true);gRegionCache.poll(true);test(gPathing.on&&!gPathing.areas,"checkbox enable");
    test(gState.ed->chk().serialize()==bytes&&gState.ed->revision()==revision,"overlay changed map/history");
    gPathing.on=oldOn;gPathing.areas=oldAreas;setRegionOpacity(oldOpacity);gRegionOptions.refresh();
    out+="region-overlay: "+std::to_string(checks)+" checks, "+std::to_string(failed)+" failed"+(failed?" (WRONG)":"")+"\n";
    return true;
}
