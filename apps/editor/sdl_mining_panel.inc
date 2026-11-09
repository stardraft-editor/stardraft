// Mining's persistent workbench, using the simulation bar's existing panel and control kit.
void simulateMiningCommand();
unsigned gSimSelectedSpawns=0;
const Editor* gSimSpawnMap=nullptr;
std::chrono::steady_clock::time_point gSimShareUntil{};
unsigned gSimShareTicket=0;
unsigned simAvailableSpawns() {
    unsigned mask=0;if(gState.ed)for(const auto& u:gState.ed->units())if(u.type==214&&u.owner<8)mask|=1u<<u.owner;
    return mask;
}
void simResetSpawnSelection() {
    gSimSpawnMap=gState.ed.get();const unsigned available=simAvailableSpawns();
    gSimSelectedSpawns=(available&1)?1:available&(~available+1);
    gSimShareUntil={};++gSimShareTicket;
}
unsigned simActiveSpawnMask() {
    if(gSimSpawnMap!=gState.ed.get())simResetSpawnSelection();
    return gSimSelectedSpawns&simAvailableSpawns();
}
int simActiveSpawnCount(){int n=0;for(unsigned mask=simActiveSpawnMask();mask;mask&=mask-1)++n;return n;}
struct SimPanelLayout {
    draw::Rect box,value[3],kind[2],metric[2],card[3],check[3],state[3],detail[3],button[3],budget[3],progress[3],spawns[8],spawnLabel;
    int rowY[3]={},headY=0;
};
SimPanelLayout simPanelLayout(const SimBarLayout& l,draw::Rect r) {
    SimPanelLayout p;
    const int w=std::min(r.width()-scale(16),scale(480)),pad=scale(12),row=scale(34),bh=scale(26);
    const int left=std::max(r.left+scale(8),(l.bar.left+l.bar.right-w)/2);
    int y=l.bar.bottom+scale(12);
    for(int k=0;k<3;++k){p.rowY[k]=y;p.value[k]={left+w-pad-scale(k==0?120:250),y+scale(4),left+w-pad,y+scale(4)+bh};y+=row;}
    const int kx=left+w-pad-scale(250),kw=scale(65);
    for(int k=0;k<2;++k)p.kind[k]={kx+k*kw,p.value[0].top,kx+(k+1)*kw,p.value[0].bottom};
    const int half=p.value[2].width()/2;
    p.metric[0]={p.value[2].left,p.value[2].top,p.value[2].left+half,p.value[2].bottom};
    p.metric[1]={p.metric[0].right,p.value[2].top,p.value[2].right,p.value[2].bottom};
    const unsigned available=simAvailableSpawns();int n=0;
    for(int player=0;player<8;++player)if(available&(1u<<player)) {
        const int x=left+w-pad-scale(250)+scale(64*(n%4));
        const int top=y+scale(4+28*(n/4));p.spawns[player]={x,top,x+scale(58),top+scale(24)};++n;
    }
    p.spawnLabel={left+pad,y,left+w-pad-scale(258),y+scale(28)};
    p.box={left,l.bar.bottom+scale(6),left+w,y+scale(8+28*std::max(1,(n+3)/4))};
    return p;
}
// The boosts occupy the otherwise disabled right palette, leaving the map unobstructed.
SimPanelLayout simBoostLayout(draw::Rect r) {
    SimPanelLayout p;p.box=r;p.headY=r.top+scale(12);
    const int left=r.left,right=r.right;
    for(int k=0;k<3;++k) {
        const int y=r.top+scale(42+102*k);
        p.card[k]={left+scale(8),y,right-scale(8),y+scale(94)};
        p.budget[k]={right-scale(77),y+scale(8),right-scale(30),y+scale(34)};
        p.state[k]={left+scale(20),y+scale(34),right-scale(18),y+scale(54)};
        p.detail[k]={left+scale(20),y+scale(53),right-scale(18),y+scale(73)};
        p.check[k]={left+scale(20),y+scale(58),left+scale(132),y+scale(84)};
        p.button[k]={right-scale(106),y+scale(58),right-scale(18),y+scale(84)};
        p.progress[k]={left+scale(20),y+scale(89),right-scale(18),y+scale(91)};
    }
    return p;
}
void simPaintBoosts(draw::Canvas&,draw::Rect);
void simPanelPress(int);
int simBoostPart(const SimPanelLayout& p,draw::Point pt) {
    for(int k=0;k<3;++k){if(p.check[k].contains(pt))return 8+k;if(p.button[k].contains(pt))return 21+k;if(p.budget[k].contains(pt))return 31+k;}
    return p.box.contains(pt)?12:0;
}
struct SimBoostView final : draw::View {
    void paint(draw::Canvas& c) override {simPaintBoosts(c,{0,0,width(),height()});}
    bool mouse(const draw::MouseEvent& e) override {
        const int part=e.kind==draw::MouseKind::Leave?0:simBoostPart(simBoostLayout({0,0,width(),height()}),e.pt);
        if(gSimPanel.hot!=part)gSimPanel.hot=part,simRedraw();
        if(e.kind==draw::MouseKind::Up){gSimPanel.down=0;simRedraw();}
        if((e.kind==draw::MouseKind::Down||e.kind==draw::MouseKind::DoubleClick)&&e.button==0&&part!=12) {
            if(simSkipRunning()||(simEngine().boosting()&&part>=21&&part<=23))return true;
            gSimPanel.down=part;simPanelPress(part);
        }
        return true;
    }
    std::wstring tip(draw::Point pt) override {
        const auto p=simBoostLayout({0,0,width(),height()});const int part=simBoostPart(p,pt);
        if(part>=8&&part<=10)return tr(L"Show or hide this boost's helpers. Found boosts stay applied.");
        if(part>=21&&part<=23)return tr(L"Search this boost for the checked spawns. Continue preserves findings and adds another compute budget.");
        for(int k=0;k<3;++k)if(p.state[k].contains(pt)||p.detail[k].contains(pt)) {
            wchar_t text[384];std::swprintf(text,384,tr(L"%d workers faster across the active spawns. Helpers appear after their commands first execute; both cycle arrows count as one worker."),simEngine().boostStatus(k).found);return text;
        }
        return {};
    }
} gSimBoostView;
draw::View* simMiningOverlayView(){return &gSimBoostView;}
void simInvalidateMiningControls(){gSimBoostView.invalidate();}
struct SimSettings {
    int kind=0,perField=1,total=9,races=0,metric=0;
    int search[3]={};bool boost[3]={},guides[3]={true,true,true};
    int workers()const{return kind==0?perField:total;}
};
const wchar_t* const kSimBoostKeys[]={L"SimBoostPosition",L"SimBoostCargo",L"SimBoostCycle"};
const wchar_t* const kSimGuideKeys[]={L"SimShowPosition",L"SimShowCargo",L"SimShowCycle"};
const wchar_t* const kSimBoostNames[]={L"Position boost",L"Cargo boost",L"Cycle boost"};
SimSettings simSettings() {
    SimSettings s;
    prefs::number(L"SimWorkerKind",s.kind,0,1);prefs::number(L"SimWorkersPerField",s.perField,1,optionsview::kSimPerFieldMax);
    prefs::number(L"SimWorkers",s.total,1,optionsview::kSimWorkersMax);prefs::number(L"SimRaces",s.races,0,3);
    prefs::number(L"SimPatchMetric",s.metric,0,1);
    for(int k=0;k<3;++k){s.search[k]=optionsview::simBoostSeconds(k);prefs::flag(kSimBoostKeys[k],s.boost[k]);prefs::flag(kSimGuideKeys[k],s.guides[k]);}
    return s;
}
struct SimMiningControls {
    struct BudgetField final : kit::TextField {
        std::wstring tip(draw::Point) override {return tr(L"Compute budget in seconds for the next Search or Continue. Enter applies it.");}
    } budget[3];
    SimMiningControls() {
        for(int k=0;k<3;++k) {
            budget[k].setText(L"15");budget[k].setMaxLength(3);
            budget[k].setFilter([](wchar_t c){return c>=L'0'&&c<=L'9';});
            budget[k].onFocusLost=[this,k]{commit(k);};budget[k].onEnter=[this,k]{commit(k);};
        }
    }
    void commit(int k) {
        const auto& text=budget[k].text();
        const int value=text.empty()?15:int(std::clamp<long>(std::wcstol(text.c_str(),nullptr,10),optionsview::kSimSearchMin,optionsview::kSimSearchMax));
        prefs::setNumber(optionsview::kSimSearchKeys[k],value);budget[k].setText(std::to_wstring(value));
        simRedraw();
    }
} gSimMiningControls;
void simHideMiningControls() {
    gSimBoostView.setVisible(false);
    for(auto& field:gSimMiningControls.budget){if(field.hasFocus()&&gRoot)gRoot->map.focus();field.setVisible(false);}
}
void simMiningControlsFrame() {
    if(!gRoot||!gSim.shown||gSim.combat||gSim.rush||!gSimPanel.open||gRoot->map.width()<scale(180)||gRoot->map.height()<scale(100)){simHideMiningControls();return;}
    const auto map=gRoot->map.frame();
    const int right=gRoot->width();int left=std::min(map.right+scale(1),right-scale(280)),top=map.top;
    const auto mini=gRoot->minimap.frame();
    if(gRoot->minimap.shown()&&mini.left>=left&&mini.right<=right)top=std::max(top,mini.bottom+scale(8));
    const draw::Rect rail{left,top,right,map.bottom};
    if(gSimBoostView.parent()!=gRoot||gRoot->children().back()!=&gSimBoostView)gRoot->add(gSimBoostView,rail);
    else gSimBoostView.setFrame(rail);
    gSimBoostView.setVisible(true);
    const auto p=simBoostLayout({0,0,gSimBoostView.width(),gSimBoostView.height()});
    for(int k=0;k<3;++k) {
        auto& field=gSimMiningControls.budget[k];
        if(field.parent()!=&gSimBoostView)gSimBoostView.add(field,{});
        field.setFrame(p.budget[k]);field.setVisible(true);
        const auto value=std::to_wstring(optionsview::simBoostSeconds(k));
        if(!field.hasFocus()&&field.text()!=value)field.setText(value);
    }
}
int simPanelPart(const SimPanelLayout& p,draw::Point pt) {
    if(!p.box.contains(pt))return 0;
    if(optionsview::stepBoxMinus(p.value[0]).contains(pt))return 1;
    if(optionsview::stepBoxPlus(p.value[0]).contains(pt))return 2;
    if(p.value[1].contains(pt))return 3;
    for(int k=0;k<2;++k){if(p.kind[k].contains(pt))return 13+k;if(p.metric[k].contains(pt))return 41+k;}
    for(int k=0;k<3;++k){if(p.check[k].contains(pt))return 8+k;if(p.button[k].contains(pt))return 21+k;if(p.budget[k].contains(pt))return 31+k;}
    for(int player=0;player<8;++player)if(p.spawns[player].contains(pt))return 60+player;
    return 12;
}
int simDrawnBoostWorkers(int kind) {
    std::set<std::pair<int,int>> workers;
    for(const auto& guide:gSimGuides)if(guide.kind==kind)workers.insert({guide.base,guide.worker});
    return int(workers.size());
}
std::wstring simBoostState(int kind) {
    if(!simActiveSpawnMask())return tr(L"Select a spawn");
    const auto st=simEngine().boostStatus(kind);wchar_t text[128];
    if(st.running)return tr(L"Searching in background");
    if(!st.finished)return tr(L"Not searched");
    if(st.failed&&st.found==0)return tr(L"Could not search");
    const int drawn=simDrawnBoostWorkers(kind);bool show=true;prefs::flag(kSimGuideKeys[kind],show);
    if(show&&drawn<st.found)std::swprintf(text,128,tr(L"%d faster \u00b7 %d/%d helpers"),st.found,drawn,st.found);
    else std::swprintf(text,128,tr(L"%d faster \u00b7 %.1f s searched"),st.found,st.seconds);
    return text;
}
std::wstring simBoostDetail(int kind) {
    const auto st=simEngine().boostStatus(kind);wchar_t text[96];
    std::swprintf(text,96,tr(L"%d helpers \u00b7 %.1f s searched"),simDrawnBoostWorkers(kind),st.seconds);return text;
}
void simPaintPanel(draw::Canvas& c,const SimBarLayout& l,draw::Rect r) {
    const auto p=simPanelLayout(l,r);const auto s=simSettings();
    const bool busy=simSkipRunning()||simEngine().boosting();
    c.roundBox(p.box,kPanel,kBorder,float(scale(8)));
    const wchar_t* names[]={L"Workers on minerals",L"Races",L"Patch labels"};
    for(int k=0;k<3;++k)c.text({p.box.left+scale(12),p.rowY[k],(k==0?p.kind[0].left:p.value[k].left)-scale(8),p.rowY[k]+scale(34)},tr(names[k]),kText,draw::kMiddle|draw::kEllipsis);
    auto segment=[&](draw::Rect b,const wchar_t* text,bool on,int part){c.roundBox(b,on?kAccentFill:gSimPanel.hot==part?kCtlHot:kCtl,on?kAccent:kBorder,float(scale(4)));c.text(b,tr(text),on?kText:kDim,draw::kCenter|draw::kMiddle|draw::kEllipsis);};
    segment(p.kind[0],L"Each field",s.kind==0,13);segment(p.kind[1],L"In all",s.kind==1,14);
    optionsview::paintStepBox(c,p.value[0],std::to_wstring(s.workers()),gSimPanel.hot==1?1:gSimPanel.hot==2?2:0,gSimPanel.down==1?1:gSimPanel.down==2?2:0,s.workers()<=1,s.workers()>=(s.kind==0?optionsview::kSimPerFieldMax:optionsview::kSimWorkersMax));
    kitpaint::drawPickFace(c,p.value[1],s.races==0?std::wstring(tr(L"As defined in map")):std::wstring(kSimRaceNames[s.races]),false,gSimPanel.hot==3);
    segment(p.metric[0],L"Frames / cycle",s.metric==0,41);segment(p.metric[1],L"Minerals / 60 s",s.metric==1,42);
    c.text(p.spawnLabel,tr(L"Spawns"),kText,draw::kMiddle);
    const unsigned selected=simActiveSpawnMask();
    for(int player=0;player<8;++player)if(!p.spawns[player].empty()) {
        const auto b=p.spawns[player];const uint32_t rgba=playerRgba(player,255);
        const auto color=draw::rgb(int(rgba>>24),int(rgba>>16&255),int(rgba>>8&255));
        const bool on=(selected&(1u<<player))!=0;
        c.roundBox(b,gSimPanel.hot==60+player?kCtlHot:kCtl,on?color:kBorder,float(scale(4)));
        kitpaint::drawCheckMark(c,{b.left+scale(5),b.top+scale(5),b.left+scale(19),b.top+scale(19)},on,gSimPanel.hot==60+player,!busy);
        c.fill({b.right-scale(5),b.top+scale(8),b.right-scale(2),b.bottom-scale(8)},color);
        c.text({b.left+scale(24),b.top,b.right-scale(7),b.bottom},L"P"+std::to_wstring(player+1),busy?kDim:kText,draw::kMiddle);
    }
}
void simPaintBoosts(draw::Canvas& c,draw::Rect r) {
    const auto p=simBoostLayout(r);const auto s=simSettings();
    const bool busy=simSkipRunning()||simEngine().boosting();
    c.fill(r,kPanel);
    c.text({r.left+scale(16),r.top+scale(10),r.right-scale(12),r.top+scale(32)},tr(L"Boost searches"),kText,draw::kMiddle);
    const draw::Color accents[]={draw::rgb(240,201,99),draw::rgb(105,205,178),draw::rgb(185,158,235)};
    for(int k=0;k<3;++k) {
        c.roundBox(p.card[k],kCtl,kBorder,float(scale(5)));
        c.fill({p.card[k].left,p.card[k].top+scale(8),p.card[k].left+scale(3),p.card[k].bottom-scale(8)},accents[k]);
        c.text({p.card[k].left+scale(12),p.card[k].top+scale(6),p.budget[k].left-scale(6),p.card[k].top+scale(36)},tr(kSimBoostNames[k]),kText,draw::kMiddle|draw::kEllipsis);
        c.text({p.budget[k].right+scale(5),p.budget[k].top,p.card[k].right-scale(5),p.budget[k].bottom},tr(L"s"),kDim,draw::kMiddle);
        const auto b=p.check[k];const int cs=scale(14),cy=(b.top+b.bottom-cs)/2;
        kitpaint::drawCheckMark(c,{b.left,cy,b.left+cs,cy+cs},s.guides[k],gSimPanel.hot==8+k,true);
        c.text({b.left+cs+scale(6),b.top,b.right,b.bottom},tr(L"Show guides"),kText,draw::kMiddle|draw::kEllipsis);
        c.text(p.state[k],simBoostState(k),kDim,draw::kMiddle|draw::kEllipsis);
        const auto st=simEngine().boostStatus(k);const bool can=!busy&&!st.exhausted&&simActiveSpawnMask()!=0;
        c.roundBox(p.button[k],can&&gSimPanel.hot==21+k?kCtlHot:kPanel,kBorder,float(scale(4)));
        c.text(p.button[k],st.running?tr(L"Searching\u2026"):st.finished?tr(L"Continue"):tr(L"Search"),can?kText:kDim,draw::kCenter|draw::kMiddle|draw::kEllipsis);
        c.fill(p.progress[k],kBorder);auto progress=p.progress[k];progress.right=progress.left+int(progress.width()*std::clamp(st.progress,0.,1.));
        if(progress.width()>0)c.fill(progress,accents[k]);
    }
    if(std::chrono::steady_clock::now()<gSimShareUntil) {
        const draw::Rect notice{r.left+scale(8),r.top+scale(360),r.right-scale(8),r.top+scale(466)};
        c.roundBox(notice,kCtl,kAccent,float(scale(6)));
        c.text({notice.left+scale(12),notice.top+scale(10),notice.right-scale(12),notice.bottom-scale(8)},tr(L"Search CPU time is shared across active spawns. For deeper analysis, select fewer spawns."),kText,draw::kWrap);
    }
}
// Applying a saved boost and displaying its helper are independent choices.
// Searches start only from their own Search/Continue button, never as a side effect of another search.
void simBoostSync() {
    if(!gSim.shown||gSim.combat||gSim.rush||simSkipRunning()||modTab())return;
    const auto s=simSettings();
    for(int k=0;k<3;++k){const auto st=simEngine().boostStatus(k);if(!s.boost[k]&&(st.running||st.finished)){simEngine().clearBoost(k);simRedraw();}}
}
void simBoostSearchNow(int k) {
    if(!gSim.shown||gSim.combat||gSim.rush||simSkipRunning()||modTab())return;
    auto& e=simEngine();if(e.boosting()||e.boostStatus(k).exhausted||!simActiveSpawnMask())return;
    gSimMiningControls.commit(k);prefs::setNumber(kSimBoostKeys[k],1);gSim.boostFailed&=~(1<<k);
    std::string error;if(!e.startBoost(k,double(optionsview::simBoostSeconds(k)),error)) {
        gSim.boostFailed|=1<<k;if(gRoot)shellPost([error]{msgbox::ask(*gRoot,tr(L"Simulation"),widen(error),{tr(L"OK")},msgbox::Icon::Info);});return;
    }
    if(simActiveSpawnCount()>1) {
        gSimShareUntil=std::chrono::steady_clock::now()+std::chrono::seconds(6);
        const unsigned ticket=++gSimShareTicket;
        if(gHost)gHost->timer(6000,[ticket]{if(ticket==gSimShareTicket){gSimShareUntil={};simRedraw();}});
    }
    gSim.done=false;gSim.due=0;simRedraw();
}
void simMiningRacesMenu() {
    if(!gRoot||!gHost)return;std::vector<kit::MenuItem> items;
    for(int k=0;k<4;++k){kit::MenuItem item;item.label=kSimRaceNames[k];item.checked=k==simSettings().races;item.run=[k]{prefs::setNumber(L"SimRaces",k);shellPost([]{simKeepPlay(simulateMiningCommand);gSimPanel.open=gSim.shown;});};items.push_back(std::move(item));}
    ImDrawList list(ImGui::GetDrawListSharedData());kit_sdl::ImguiCanvas measure(list,nullptr);
    kit::Menu::popup(*gRoot,gHost->mouse(*gRoot),items,kit::Menu::measure(items,&measure),{});
}
void simPanelPress(int part) {
    const auto s=simSettings();
    if(part==1||part==2){const int value=std::clamp(s.workers()+(part==1?-1:1),1,s.kind==0?optionsview::kSimPerFieldMax:optionsview::kSimWorkersMax);prefs::setNumber(s.kind==0?L"SimWorkersPerField":L"SimWorkers",value);shellPost([]{simKeepPlay(simulateMiningCommand);});}
    else if(part==13||part==14){prefs::setNumber(L"SimWorkerKind",part-13);shellPost([]{simKeepPlay(simulateMiningCommand);});}
    else if(part==3)shellPost([]{simMiningRacesMenu();});
    else if(part>=8&&part<=10)prefs::setNumber(kSimGuideKeys[part-8],!s.guides[part-8]);
    else if(part>=21&&part<=23)simBoostSearchNow(part-21);
    else if(part==41||part==42)prefs::setNumber(L"SimPatchMetric",part-41);
    else if(part>=60&&part<68) {
        gSimSelectedSpawns=simActiveSpawnMask()^(1u<<(part-60));
        shellPost([]{simKeepPlay(simulateMiningCommand);});
    }
    simRedraw();
}
bool simPanelMouse(const draw::MouseEvent& e,const SimBarLayout& l,draw::Rect r) {
    if(!gSimPanel.open||gSim.combat||gSim.rush)return false;
    const int part=e.kind==draw::MouseKind::Leave?0:simPanelPart(simPanelLayout(l,r),e.pt);
    if(part!=gSimPanel.hot)gSimPanel.hot=part,simRedraw();
    if(e.kind==draw::MouseKind::Up&&gSimPanel.down){gSimPanel.down=0;simRedraw();return part!=0;}
    if(!part)return false;
    const bool busy=simSkipRunning()||simEngine().boosting(),waits=busy&&(part==1||part==2||part==3||part==13||part==14||(part>=60&&part<68));
    if(e.kind==draw::MouseKind::Wheel&&e.wheel!=0){if(!waits&&(part==1||part==2))simPanelPress(e.wheel>0?2:1);return true;}
    if((e.kind==draw::MouseKind::Down||e.kind==draw::MouseKind::DoubleClick)&&e.button==0&&part!=12){if(waits||(busy&&part>=21&&part<=23))return true;gSimPanel.down=part;simPanelPress(part);}
    return true;
}
std::wstring simPanelTip(const SimBarLayout& l,draw::Rect r,draw::Point pt) {
    const int part=gSimPanel.open&&!gSim.combat&&!gSim.rush?simPanelPart(simPanelLayout(l,r),pt):0;
    if(part>=8&&part<=10)return tr(L"Show or hide the ghost workers and command guides. Found boosts stay applied.");
    if(part>=21&&part<=23)return tr(L"Search only this boost for its compute budget. Continue adds another budget without discarding findings.");
    if(part>=60&&part<68)return tr(L"Run mining and boost searches at this starting location.");
    if(part==41)return tr(L"Show the latest completed delivery-to-delivery cycle in frames, as in Mining Lab. Zoom in to see patch values.");
    if(part==42)return tr(L"Show only each patch's measured minerals per 60 game seconds.");
    return {};
}
