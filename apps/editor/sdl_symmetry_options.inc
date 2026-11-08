// Optional, nonmodal quick access to per-copy terrain and enable state.
namespace {
// Terrain previews share the editor palette's game-data thumbnails.
class SymmetryTerrainGrid final : public draw::View {
    std::vector<kit::DropdownItem> items_;
    int value_=0,hover_=-1,scroll_=0;
    bool enabled_=true;
    int columns() const {return std::max(1,(width()+scale(4))/scale(56));}
    int rows() const {return (int(items_.size())+columns()-1)/columns();}
    int maxScroll() const {return std::max(0,rows()*scale(48)-height());}
    draw::Rect cell(int index) const {
        const int cols=columns(),gap=scale(4),w=(width()-(cols-1)*gap)/cols;
        return {(index%cols)*(w+gap),(index/cols)*scale(48)-scroll_,(index%cols)*(w+gap)+w,(index/cols)*scale(48)+scale(44)-scroll_};
    }
    int hit(draw::Point p) const {
        if(p.x<0||p.y<0||p.x>=width()||p.y>=height())return -1;
        for(int i=0;i<int(items_.size());++i)if(cell(i).contains(p))return i;
        return -1;
    }
    void choose(int index) {
        if(!enabled_||index<0||index>=int(items_.size()))return;
        setValue(index);if(onChange)onChange(index);
    }
public:
    std::function<void(int)> onChange;
    void setItems(std::vector<kit::DropdownItem> items){items_=std::move(items);scroll_=0;value_=std::clamp(value_,0,std::max(0,int(items_.size())-1));invalidate();}
    const auto& items() const {return items_;}
    void setValue(int value){
        value_=std::clamp(value,0,std::max(0,int(items_.size())-1));
        auto r=cell(value_);if(r.top<0)scroll_+=r.top;else if(r.bottom>height())scroll_+=r.bottom-height();
        scroll_=std::clamp(scroll_,0,maxScroll());invalidate();
    }
    int value() const {return value_;}
    void setEnabled(bool on){if(enabled_!=on){enabled_=on;invalidate();}}
    bool enabled() const {return enabled_;}
    bool focusable() const override {return enabled_;}
    void focusChanged(bool) override {invalidate();}
    void resized() override {setValue(value_);}
    std::wstring tip(draw::Point p) override {const int i=hit(p);return i<0?std::wstring():items_[size_t(i)].label;}
    draw::Cursor cursor(draw::Point p) override {return enabled_&&hit(p)>=0?draw::Cursor::Hand:draw::Cursor::Arrow;}
    bool mouse(const draw::MouseEvent& e) override {
        if(e.kind==draw::MouseKind::Move){const int i=hit(e.pt);if(i!=hover_){hover_=i;invalidate();}return true;}
        if(e.kind==draw::MouseKind::Leave){hover_=-1;invalidate();return true;}
        if(!enabled_)return false;
        if(e.kind==draw::MouseKind::Wheel){scroll_=std::clamp(scroll_-int(draw::wheelDistance(e,float(scale(48)))),0,maxScroll());invalidate();return true;}
        if(e.kind==draw::MouseKind::Down&&!e.button){const int i=hit(e.pt);if(i>=0){focus();choose(i);}return true;}
        return false;
    }
    bool key(const draw::KeyEvent& e) override {
        if(!enabled_||!e.down||e.mods)return false;
        int next=value_;
        switch(e.key){
        case draw::Key::Left:--next;break;case draw::Key::Right:++next;break;
        case draw::Key::Up:next-=columns();break;case draw::Key::Down:next+=columns();break;
        case draw::Key::Home:next=0;break;case draw::Key::End:next=int(items_.size())-1;break;
        case draw::Key::Enter:case draw::Key::Space:break;default:return false;}
        choose(std::clamp(next,0,std::max(0,int(items_.size())-1)));return true;
    }
    void paint(draw::Canvas& c) override {
        const auto font=c.setFont(c.font(kitpaint::smallFontDesc()));
        for(int i=0;i<int(items_.size());++i){
            const auto r=cell(i);if(r.bottom<=0||r.top>=height())continue;
            const bool selected=i==value_;const auto& item=items_[size_t(i)];
            c.roundBox(r,selected?kSel:i==hover_&&enabled_?kCtlHot:kCtl,selected&&enabled_?kAccent:kBorder,float(scale(3)));
            if(item.image){
                auto& image=*item.image;const int top=r.top+scale(3),h=scale(23),left=r.left+scale(3),w=r.width()-scale(6);
                const int crop=std::max(1,std::min(image.height(),image.width()*h/std::max(1,w))),y=(image.height()-crop)/2;
                c.bitmap(image,{0,y,image.width(),y+crop},{left,top,left+w,top+h},true,enabled_?255:90);
            }else if(!i)c.text({r.left,r.top+scale(2),r.right,r.top+scale(25)},L"\u21a9",enabled_?kAccent:kDim,draw::kMiddle|draw::kCenter);
            else c.fill({r.left+scale(3),r.top+scale(3),r.right-scale(3),r.top+scale(26)},kBg);
            c.text({r.left+scale(2),r.top+scale(26),r.right-scale(2),r.bottom-scale(1)},i?item.label:tr(L"Inherit"),enabled_?kText:kDim,draw::kCenter|draw::kMiddle|draw::kEllipsis);
            if(selected&&hasFocus())c.roundBox({r.left+1,r.top+1,r.right-1,r.bottom-1},draw::kNone,kAccent,float(scale(2)));
        }
        if(maxScroll()>0){const int h=std::max(scale(12),height()*height()/(height()+maxScroll())),y=scroll_*(height()-h)/maxScroll();c.fill({width()-scale(2),y,width(),y+h},kDim);}
        c.setFont(font);
    }
};
class SymmetryOptionsPanel final : public draw::View {
    kit::Dropdown copies_;
    SymmetryTerrainGrid defaults_, terrain_;
    kit::CheckBox enabled_;
    kit::Button previous_, next_, configure_;
    std::vector<CopyTransform> transforms_;
    std::vector<int> terrains_;
    std::string signature_;
    const Editor* editor_ = nullptr;
    int era_ = -1;
    std::string selectedKey_;
    void controls(bool ready) {
        copies_.setEnabled(ready);defaults_.setEnabled(ready);terrain_.setEnabled(ready);
        for(auto* v:{&previous_,&next_,&configure_})v->setEnabled(ready);
        enabled_.setEnabled(ready);
    }
    int terrainIndex(int value) const {
        const auto at = std::find(terrains_.begin(), terrains_.end(), value);
        return at == terrains_.end() ? 0 : int(at - terrains_.begin());
    }
    std::wstring copyName(size_t index) const {
        if (!index) return tr(L"Source brush");
        const auto t = transforms_[index];
        const auto key = t.key();
        if (key == CopyTransform{-1,0,0,1}.key()) return tr(L"Left / right");
        if (key == CopyTransform{1,0,0,-1}.key()) return tr(L"Top / bottom");
        if (key == CopyTransform{0,1,1,0}.key()) return tr(L"Diagonal \\");
        if (key == CopyTransform{0,-1,-1,0}.key()) return tr(L"Diagonal /");
        const int angle = (int(std::lround(std::atan2(t.c,t.a)*180/3.141592653589793)) + 360) % 360;
        return tr(t.a*t.d-t.b*t.c < 0 ? L"Mirror + rotate " : L"Rotate ") + std::to_wstring(angle) + L"\u00b0";
    }
    void selected() {
        if (!gState.ed || !gState.ed->isOpen() || gState.ed.get()!=editor_) { refresh(); return; }
        if (copies_.value() < 0 || copies_.value() >= int(transforms_.size())) return;
        selectedKey_ = transforms_[size_t(copies_.value())].key();
        const auto& options = gState.ed->copyOptions();
        const auto at = std::find_if(options.choices.begin(),options.choices.end(),[&](const auto& c){return c.key==selectedKey_;});
        enabled_.setOn(!copies_.value() || at==options.choices.end() || at->enabled);
        terrain_.setValue(terrainIndex(at==options.choices.end() ? -1 : at->terrain));
        enabled_.setEnabled(copies_.value()>0);
        terrain_.setEnabled(copies_.value()>0 && enabled_.on());
        previous_.setEnabled(copies_.value()>0);
        next_.setEnabled(copies_.value()+1<int(transforms_.size()));
        invalidate();
    }
    void apply(bool isDefault) {
        syncTerrainCopies();
        if (!gState.ed || !gState.ed->isOpen() || gState.ed.get()!=editor_) { refresh(); return; }
        auto options=gState.ed->copyOptions();
        if (isDefault) options.terrain=terrains_[size_t(defaults_.value())];
        else {
            if (copies_.value()<=0 || copies_.value()>=int(transforms_.size())) return;
            const auto key=transforms_[size_t(copies_.value())].key();
            auto at=std::find_if(options.choices.begin(),options.choices.end(),[&](const auto& c){return c.key==key;});
            if (at==options.choices.end()) {options.choices.push_back({key});at=options.choices.end()-1;}
            at->enabled=enabled_.on();at->terrain=terrains_[size_t(terrain_.value())];
        }
        gState.ed->setCopyOptions(std::move(options));
        rememberTerrainCopies();
        if (gActivePanel) panelStore(*gActivePanel);
        gPanelsSaveAt=0;palettePanelsSave();
        gState.dirty=true;shellToolsChanged();
        signature_.clear();refresh();
        if(gNoBoxes)std::printf("symmetry-options: applied copy=%d enabled=%d terrain=%d default=%d\n",copies_.value(),enabled_.on(),terrains_[size_t(terrain_.value())],gState.ed->copyOptions().terrain);
    }
public:
    bool open=false;
    SymmetryOptionsPanel() {
        enabled_.setLabel(tr(L"Place this copy"));
        previous_.setText(L"\u2039");next_.setText(L"\u203a");configure_.setText(tr(L"Configure copies"));
        previous_.setTip(tr(L"Select the previous copy"));next_.setTip(tr(L"Select the next copy"));
        copies_.setTip(tr(L"Select a symmetry copy to change its terrain or placement"));
        configure_.setTip(tr(L"Configure mirrors, rotations and all copy settings"));
        copies_.searchable=true;
        copies_.listInWindow=false;
        copies_.onChange=[this](int){selected();};
        previous_.onClick=[this]{copies_.setValue(std::max(0,copies_.value()-1));selected();};
        next_.onClick=[this]{copies_.setValue(std::min(int(transforms_.size())-1,copies_.value()+1));selected();};
        defaults_.onChange=[this](int){apply(true);};
        terrain_.onChange=[this](int){apply(false);};enabled_.onChange=[this](bool){apply(false);};
        configure_.onClick=[this]{showSymmetryCopiesDialog();refresh();};
        for(auto* v:std::vector<draw::View*>{&copies_,&defaults_,&terrain_,&enabled_,&previous_,&next_,&configure_})add(*v,{});
    }
    void refresh() {
        syncTerrainCopies();
        auto* ed=gState.ed.get();const bool ready=ed&&ed->isOpen();
        if(!ready){
            controls(false);
            if(editor_){editor_=nullptr;signature_.clear();invalidate();}return;
        }
        const auto signature=std::to_string(gState.mode==Mode::Terrain?gState.palette:-1)+":"+std::to_string(ed->mirrors())+":"+std::to_string(ed->turns())+":"+copyOptionsText(ed->copyOptions());
        if(editor_==ed&&era_==ed->era()&&signature_==signature)return;
        controls(true);
        if(editor_!=ed||era_!=ed->era()) {
            selectedKey_.clear();terrains_={-1};std::vector<kit::DropdownItem> items{{tr(L"Same as source brush")}};
            for(int t=1;t<512;++t)if(const auto name=ed->terrainName(uint16_t(t));!name.empty()){terrains_.push_back(t);items.push_back({widen(name)});}
            for(const auto& t:ed->tileset().modTerrains)if(!t.variantOf&&std::find(terrains_.begin(),terrains_.end(),t.id)==terrains_.end()){terrains_.push_back(t.id);items.push_back({widen(t.name)});}
            for(size_t i=1;i<items.size();++i)items[i].image=isomrules::terrainPicture(ed->tileset(),uint16_t(terrains_[i]));
            defaults_.setItems(items);items[0].label=tr(L"Use default terrain");terrain_.setItems(std::move(items));
        }
        editor_=ed;era_=ed->era();signature_=signature;transforms_=ed->availableCopies();
        if(gNoBoxes&&gState.mode==Mode::Terrain&&gState.palette>0){
            const auto& profiles=dockedPanelsOf(activeDocKey()).terrainCopies.values;
            std::printf("symmetry-options: source=%d saved=%d default=%d choices=%zu\n",gState.palette,int(profiles.count({ed->era(),gState.palette})),ed->copyOptions().terrain,ed->copyOptions().choices.size());
        }
        int select=transforms_.size()>1?1:0;std::vector<kit::DropdownItem> items;
        for(size_t i=0;i<transforms_.size();++i){
            if(transforms_[i].key()==selectedKey_)select=int(i);
            const auto& options=ed->copyOptions();const auto at=std::find_if(options.choices.begin(),options.choices.end(),[&](const auto& c){return c.key==transforms_[i].key();});
            const bool on=!i||at==options.choices.end()||at->enabled;
            items.push_back({std::to_wstring(i)+L" \u00b7 "+copyName(i),-1,-1,on?kAccent:kDim});
        }
        copies_.setItems(std::move(items));copies_.setValue(select);defaults_.setValue(terrainIndex(ed->copyOptions().terrain));selected();
    }
    void resized() override {
        const int p=scale(12),right=width()-p;
        copies_.setFrame({scale(132),p,right-scale(72),p+scale(26)});
        previous_.setFrame({right-scale(64),p,right-scale(34),p+scale(26)});
        next_.setFrame({right-scale(30),p,right,p+scale(26)});
        defaults_.setFrame({p,scale(64),right,scale(160)});
        enabled_.setFrame({p,scale(170),scale(142),scale(194)});
        terrain_.setFrame({p,scale(196),right,scale(292)});
        configure_.setFrame({p,scale(304),scale(154),scale(332)});
    }
    void paint(draw::Canvas& c) override {
        refresh();
        c.fill({0,0,width(),height()},kPanel);
        c.text({scale(12),scale(12),scale(128),scale(38)},tr(L"Copy"),kDim,draw::kMiddle);
        c.text({scale(12),scale(40),scale(132),scale(62)},tr(L"Default terrain"),kDim,draw::kMiddle);
        if(editor_){
            c.text({scale(132),scale(40),width()-scale(12),scale(62)},defaults_.items()[size_t(defaults_.value())].label,kText,draw::kMiddle|draw::kRight|draw::kEllipsis);
            c.text({scale(146),scale(170),width()-scale(12),scale(194)},terrain_.items()[size_t(terrain_.value())].label,kText,draw::kMiddle|draw::kRight|draw::kEllipsis);
        }
        int enabled=0;
        if(editor_)for(const auto& t:transforms_){const auto& o=editor_->copyOptions();const auto at=std::find_if(o.choices.begin(),o.choices.end(),[&](const auto& c){return c.key==t.key();});enabled+=at==o.choices.end()||at->enabled;}
        const auto source=editor_&&gState.mode==Mode::Terrain&&gState.palette>0?widen(editor_->terrainName(uint16_t(gState.palette)))+L" \u00b7 ":std::wstring();
        c.text({scale(166),scale(304),width()-scale(12),scale(332)},editor_?source+std::to_wstring(enabled)+L" / "+std::to_wstring(transforms_.size())+tr(L" copies placed"):tr(L"Open a map"),kDim,draw::kMiddle|draw::kRight|draw::kEllipsis);
    }
    static bool check(std::string& out) {
        if(!gState.ed||!gState.ed->isOpen()){out="symmetry-options: open a map (WRONG)\n";return true;}
        auto& ed=*gState.ed;const auto options=ed.copyOptions();const int mirrors=ed.mirrors(),turns=ed.turns();const auto bytes=ed.chk().serialize();const auto revision=ed.revision();
        int checks=0,failed=0;auto test=[&](bool good){++checks;if(!good){++failed;out+="symmetry-options: assertion "+std::to_string(checks)+" failed (WRONG)\n";}};
        ed.setCopyOptions({});ed.setMirrors(1);ed.setTurns(1);SymmetryOptionsPanel p;p.setSize(scale(420),scale(344));p.refresh();test(p.copies_.value()==1);
        test(p.terrain_.items().size()==p.terrains_.size()&&std::any_of(p.terrain_.items().begin()+1,p.terrain_.items().end(),[](const auto& item){return bool(item.image);}));
        p.defaults_.setValue(1);p.defaults_.onChange(1);test(ed.copyOptions().terrain==p.terrains_[1]);
        p.terrain_.setValue(2);p.terrain_.onChange(2);test(ed.copyOptions().choices.size()==1&&ed.copyOptions().choices[0].terrain==p.terrains_[2]);
        p.enabled_.setOn(false);p.enabled_.onChange(false);test(!ed.copyOptions().choices[0].enabled&&!p.terrain_.enabled());
        draw::MouseEvent click;click.kind=draw::MouseKind::Down;click.button=0;click.pt={scale(185),scale(20)};
        p.terrain_.mouse(click);test(p.terrain_.value()==2);
        p.enabled_.setOn(true);p.enabled_.onChange(true);test(ed.copyOptions().choices[0].enabled&&p.terrain_.enabled());
        draw::KeyEvent keyEvent;keyEvent.down=true;keyEvent.key=draw::Key::Home;
        p.terrain_.key(keyEvent);test(ed.copyOptions().choices[0].terrain==-1);
        keyEvent.key=draw::Key::Right;p.terrain_.key(keyEvent);test(ed.copyOptions().choices[0].terrain==p.terrains_[1]);
        p.terrain_.mouse(click);test(ed.copyOptions().choices[0].terrain==p.terrains_[3]);
        SymmetryTerrainGrid many;many.setSize(scale(200),scale(96));many.setItems(std::vector<kit::DropdownItem>(50));
        keyEvent.key=draw::Key::End;many.key(keyEvent);test(many.value()==49);
        keyEvent.key=draw::Key::Home;many.key(keyEvent);test(many.value()==0);
        const auto key=p.selectedKey_;
        ed.setTurns(4);p.refresh();test(p.transforms_.size()>2&&p.selectedKey_==key);
        p.copies_.setValue(1);p.selected();
        p.previous_.onClick();test(p.copies_.value()==0&&!p.enabled_.enabled()&&!p.terrain_.enabled());
        p.next_.onClick();test(p.copies_.value()==1);
        ed.setCopyOptions(options);ed.setMirrors(mirrors);ed.setTurns(turns);p.refresh();test(p.transforms_.size()==ed.availableCopies().size());
        if(gActivePanel)panelStore(*gActivePanel);
        test(ed.chk().serialize()==bytes&&ed.revision()==revision);
        out+="symmetry-options: "+std::to_string(checks)+" checks, "+std::to_string(failed)+" failed"+(failed?" (WRONG)":"")+"\n";return true;
    }
};
SymmetryOptionsPanel gSymmetryOptions;
}
void toggleSymmetryOptions() {
    gSymmetryOptionsVisible=!gSymmetryOptionsVisible;
    if(!gSymmetryOptionsVisible&&gSymmetryOptions.open&&gHost){gHost->closeWindow(gSymmetryOptions);gSymmetryOptions.open=false;}
    shellMenusChanged();gState.dirty=true;
}
void symmetryOptionsFrame() {
    syncTerrainCopies();
    if(!gSymmetryOptionsVisible)return;
    gSymmetryOptions.refresh();
    if(!gSymmetryOptions.open&&gHost&&gRoot){
        gSymmetryOptions.open=true;gSymmetryOptions.setSize(scale(420),scale(344));
        draw::WindowSpec spec{draw::WindowKind::Tool,{scale(40),scale(120),scale(460),scale(464)},tr(L"Symmetry options")};
        if(gNoBoxes)gHost->snapshotInput(*gRoot,gRoot->width(),gRoot->height(),{});
        gHost->openWindow(*gRoot,gSymmetryOptions,spec,[]{gSymmetryOptions.open=false;gSymmetryOptionsVisible=false;shellMenusChanged();});
        if(gNoBoxes)std::printf("symmetry-options: opened\n");
    }
}
bool symmetryOptionsCheck(const std::string& name,std::string& out){return name=="symmetry-options"&&SymmetryOptionsPanel::check(out);}
bool symmetrySourceProfilesCheck(const std::string& name,std::string& out) {
    if(name!="symmetry-source-profiles")return false;
    if(!gState.ed||!gState.ed->isOpen()){out="symmetry-source-profiles: open a map (WRONG)\n";return true;}
    auto& ed=*gState.ed;auto& panels=dockedPanelsOf(activeDocKey());
    const auto oldProfiles=panels.terrainCopies;const int oldSource=panels.terrainCopySource,oldEra=panels.terrainCopyEra;
    const auto oldOptions=ed.copyOptions();const int oldMirrors=ed.mirrors(),oldTurns=ed.turns(),oldPalette=gState.palette;
    const auto oldMode=gState.mode;const auto oldChosen=shellPaletteChosen();
    const auto bytes=ed.chk().serialize();const auto revision=ed.revision();
    int count=0,failures=0;auto check=[&](bool yes,const char* reason){++count;if(!yes){++failures;out+=std::string("symmetry-source-profiles: ")+reason+" (WRONG)\n";}};
    panels.terrainCopies.values.clear();panels.terrainCopySource=-1;panels.terrainCopyEra=-1;
    gState.mode=Mode::Terrain;gState.palette=-1;ed.setMirrors(Mirror::Diagonal|Mirror::AntiDiagonal);ed.setTurns(1);ed.setCopyOptions({});
    selectPalette(gState,2);
    check(ed.availableCopies().size()==4,"both diagonal mirrors must retain four copies");
    check(ed.copyOptions().terrain==-1&&ed.copyOptions().choices.empty(),"first source must inherit");
    auto configured=ed.copyOptions();const auto copies=ed.availableCopies();
    configured.choices={{copies[1].key(),true,-1},{copies[2].key(),true,4},{copies[3].key(),true,8}};
    ed.setCopyOptions(configured);rememberTerrainCopies();
    check(panels.terrainCopies.values.size()==1,"source setup must be remembered");
    selectPalette(gState,3);
    check(ed.copyOptions().terrain==-1&&ed.copyOptions().rawTile==-1&&ed.copyOptions().choices.empty(),"new source must not inherit the previous overrides");
    check(ed.mirrors()==(Mirror::Diagonal|Mirror::AntiDiagonal)&&ed.availableCopies().size()==4,"source changes must retain mirror geometry");
    auto water=ed.copyOptions();water.terrain=1;water.choices={{copies[1].key(),false,5,17}};ed.setCopyOptions(water);rememberTerrainCopies();
    selectPalette(gState,2);
    check(copyOptionsText(ed.copyOptions())==copyOptionsText(configured),"source setup must restore exactly");
    selectPalette(gState,3);
    check(copyOptionsText(ed.copyOptions())==copyOptionsText(water),"second source must retain its own default, placement and raw tile");
    selectPalette(gState,1);
    check(ed.copyOptions().terrain==-1&&ed.copyOptions().choices.empty(),"third source must remain unconfigured");
    selectPalette(gState,2);
    check(copyOptionsText(ed.copyOptions())==copyOptionsText(configured),"repeated palette changes must preserve the first profile");
    const auto lines=panels.terrainCopies.text();TerrainCopyProfiles restored;
    for(const auto& line:lines){std::vector<std::string> parts;std::stringstream stream(line);for(std::string part;std::getline(stream,part,'\t');)parts.push_back(part);check(restored.read(parts),"saved record must parse");}
    check(restored.text()==lines,"profile serialization must round-trip");
    check(copyOptionsText(restored.select(ed.era(),2,{}))==copyOptionsText(configured),"restored profile must retain all copy choices");
    check(restored.select((ed.era()+1)%8,2,{}).choices.empty(),"tilesets must be isolated");
    TerrainCopyProfiles otherMap;
    check(otherMap.select(ed.era(),2,{}).choices.empty(),"maps must be isolated");
    CopyOptions geometry;geometry.direct=geometry.mirroredRotations=geometry.opposite=true;
    const auto merged=restored.select(ed.era(),2,geometry);
    check(merged.direct&&merged.mirroredRotations&&merged.opposite,"profiles must not replace mirror generators");
    const auto beforeBad=restored.text();
    restored.read({"terrain-copy","bad","2","invalid"});restored.read({"terrain-copy","8","2","invalid"});restored.read({"terrain-copy","1","-2","invalid"});restored.read({"terrain-copy","1"});
    check(restored.text()==beforeBad,"invalid records must not change valid profiles");
    restored.remember(ed.era(),2,{});check(restored.select(ed.era(),2,{}).choices.empty(),"inherit must reset a stored setup");
    check(ed.chk().serialize()==bytes&&ed.revision()==revision,"profile edits must leave map and undo revision unchanged");
    panels.terrainCopies=oldProfiles;panels.terrainCopySource=oldSource;panels.terrainCopyEra=oldEra;
    gState.mode=oldMode;gState.palette=oldPalette;shellPaletteChosen()=oldChosen;
    ed.setMirrors(oldMirrors);ed.setTurns(oldTurns);ed.setCopyOptions(oldOptions);gState.dirty=true;
    out+="symmetry-source-profiles: "+std::to_string(count)+" checks, "+std::to_string(failures)+" failed\n";return true;
}
