// SPDX-License-Identifier: MIT
// Compact floating Mining Lab: parallel base tests and three
// independent timed searches. The worlds remain owned by Session workers.
#include "../../tools/map_lab/guide_geometry.h"
#include "../kit/check_box.h"
#include "../kit/label.h"
#include "../kit/list_view.h"
#include "../kit/progress_bar.h"
#include "lab_base_preview.h"
#include "lab_stepper.h"
#include <mapeditor/simulation/geometry.h>

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
    stardraft::kit::Dropdown base_, race_;
    LabStepper workers_, speed_;
    LabBasePreview preview_;
    std::chrono::steady_clock::time_point copiedUntil_{};
    void chooseBase(int n) {
      base_.setValue(n);
      select();
      if (n >= 0 && n < int(bases_.size()))
        center(bases_[size_t(n)]);
    }
    std::wstring location(const Base &b) const {
      if (!editor_)
        return L"";
      const int col = std::clamp(b.x * 3 / (editor_->width() * 32), 0, 2),
                row = std::clamp(b.y * 3 / (editor_->height() * 32), 0, 2);
      const wchar_t *names[]{L"Top left",    L"Top",    L"Top right",
                             L"Left",        L"Center", L"Right",
                             L"Bottom left", L"Bottom", L"Bottom right"};
      return names[row * 3 + col];
    }
    int defaultWorkers() const {
      const int n = base_.value();
      if (!editor_ || n < 0 || n >= int(bases_.size()))
        return 1;
      const auto &b = bases_[size_t(n)];
      int count = 0;
      for (const auto &u : editor_->units())
        if (u.type >= 176 && u.type <= 178 &&
            mapeditor::simulation::distance({int(u.x) - b.x, int(u.y) - b.y}) <=
                384)
          ++count;
      return std::max(1, count);
    }
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
        preview_.setValue(base_.value());
        if (base_.value() >= 0 && base_.value() < int(bases_.size())) {
          const auto &b = bases_[size_t(base_.value())];
          base_.setTip(location(b) + L" / map coordinates " +
                       std::to_wstring(b.x) + L", " + std::to_wstring(b.y));
        }
        controls();
    }
    void controls() {
        auto* t=selected();
        const bool live=t && t->value.ready && !t->value.done;
        const bool busy=t && t->value.searching;
        run_.setText(t?L"Stop test":L"Start mining");
        run_.setEnabled(!bases_.empty());
        stopAll_.setEnabled(!tests_.empty());
        copy_.setEnabled(!tests_.empty());
        copy_.setText(std::chrono::steady_clock::now() < copiedUntil_
                          ? L"Copied"
                          : L"Copy report");
        race_.setEnabled(!t); workers_.setEnabled(!t);
        pause_.setEnabled(live && !busy);
        pause_.setText(t && t->paused?L"Resume":L"Pause");
        for (int k=0;k<3;++k) {
            const maplab::SearchStatus empty;
            const auto& s=t?t->value.mining.searches[k]:empty;
            search_[k].setEnabled(live && !busy && !s.exhausted);
            search_[k].setText(s.running ? (s.validating?L"Verifying\u2026":L"Searching\u2026") :
                               s.exhausted?L"Exhausted":s.finished?L"Continue":L"Search");
            const wchar_t *hints[]{L"Find a faster return starting point",
                                   L"Time the return-cargo command",
                                   L"Improve the route between mining trips"};
            status_[k].setText(
                s.running
                    ? (s.validating ? L"Verifying the full worker lineup\u2026"
                                    : L"Searching \u00b7 mining is paused")
                : s.finished ? std::to_wstring(s.found) +
                                   (s.found == 1 ? L" worker faster \u00b7 "
                                                 : L" workers faster \u00b7 ") +
                                   number(s.seconds) + L" s searched"
                             : hints[k]);
            progress_[k].setVisible(!details_ && (s.running || s.finished));
            progress_[k].setBusy(s.running && s.validating);
            progress_[k].setValue(float(s.running?s.progress:s.finished?1:0));
        }
    }
    std::wstring report() const {
        std::wostringstream out;
        out << L"Base\tRace\tWorkers\tMinerals/min\tGame seconds\tStatus\r\n";
        for (const auto& t:tests_)
          out << t.base.owner + 1 << L": " << t.base.x << L", " << t.base.y
              << L'\t' << race_.items()[size_t(t.race)].label << L'\t'
              << (t.workers
                      ? std::to_wstring(t.workers)
                      : L"1 / patch (" +
                            std::to_wstring(t.value.mining.patches.size()) +
                            L")")
              << L'\t' << number(t.value.mining.income, 2) << L'\t'
              << number(t.value.mining.frame * .042) << L'\t'
              << (t.value.searching ? L"Searching"
                  : t.paused        ? L"Paused"
                                    : L"Running")
              << L"\r\n";
        if (auto* t=const_cast<MiningLabPanel*>(this)->selected()) {
            out << L"\r\nField X\tField Y\tCycle frames\tMinerals/min\r\n";
            for (const auto& p:t->value.mining.patches)
                out << p.x << L'\t' << p.y << L'\t' << p.lastCycle << L'\t' << number(p.perMinute,2) << L"\r\n";
            const wchar_t *names[]{L"Position boost", L"Cargo boost",
                                   L"Cycle boost"};
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
        workers_.setMaximum(64);
        workers_.format = [](int n) {
          return n ? std::to_wstring(n) + L" workers \u21ba" : L"1 / patch";
        };
        workers_.setValue(0);
        workers_.setTip(L"Number of workers. Default: one per mineral patch. "
                        L"Click the value (or Home) to reset.");
        workers_.onReset = [this] {
          workers_.setValue(0);
          if (gNoBoxes)
            std::printf("mining-lab: workers=auto default=%d\n",
                        defaultWorkers());
        };
        workers_.onStep = [this](int d) {
          workers_.setValue(std::clamp(
              (workers_.value() ? workers_.value() : defaultWorkers()) + d, 1,
              64));
          if (gNoBoxes)
            std::printf("mining-lab: workers=%d default=%d\n", workers_.value(),
                        defaultWorkers());
        };
        speed_.format = [](int n) {
          return std::to_wstring(1 << n) + L"\u00d7";
        };
        speed_.setValue(0);
        speed_.setTip(L"Playback speed. Use minus/plus or the arrow keys.");
        speed_.onChange = [this](int) {
          for (auto &t : tests_)
            t.session->speed(t.paused ? 0 : speed());
          if (gNoBoxes)
            std::printf("mining-lab: playback=%d\n", speed());
        };
        base_.onChange = [this](int n) { chooseBase(n); };
        preview_.onSelect = [this](int n) { chooseBase(n); };
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
            details_=!details_; table_.setVisible(details_);
            detailsButton_.setText(details_?L"Boosts":L"Tests");
            for(int k=0;k<3;++k)for(auto* v:std::initializer_list<draw::View*>{&guides_[k],&budget_[k],&search_[k],&status_[k],&progress_[k]})v->setVisible(!details_);
            controls();
            invalidate();
        };
        copy_.setText(L"Copy report");
        copy_.setTip(L"Copy all base summaries, selected mineral cycles and "
                     L"boost results as spreadsheet-ready text");
        copy_.onClick = [this] {
          if (host()) {
            host()->setClipboardText(narrow(report()));
            copiedUntil_ =
                std::chrono::steady_clock::now() + std::chrono::seconds(2);
            controls();
          }
        };
        for (auto *v : std::initializer_list<draw::View *>{
                 &preview_, &base_, &race_, &workers_, &speed_, &run_,
                 &stopAll_, &pause_, &close_, &copy_, &detailsButton_, &stats_,
                 &table_})
          add(*v, {});
        table_.setVisible(false);
        table_.setColumns({{L"Base",150},{L"Race",70},{L"Workers",70},{L"Minerals/min",90,stardraft::kit::ColAlign::Right},
                           {L"Time",65,stardraft::kit::ColAlign::Right},{L"Status",0}});
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
        const wchar_t *names[]{L"Position boost", L"Cargo boost",
                               L"Cycle boost"};
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
        stats_.setText(L"Select a base, then start mining");
        controls();
    }
    void setup() {
        stop(); editor_=gState.ed.get(); revision_=editor_?editor_->revision():0;
        bases_.clear();
        preview_.markers.clear();
        std::vector<stardraft::kit::DropdownItem> items;
        if (editor_) for (const auto& u:editor_->units())
            if (u.type==214 || u.type==106 || u.type==131 || u.type==132 || u.type==133 || u.type==154) {
                bases_.push_back({u.x,u.y,u.owner,u.type});
                const auto rgba = playerRgba(u.owner, 255);
                const auto color =
                    draw::rgb(rgba >> 24, rgba >> 16 & 255, rgba >> 8 & 255);
                items.push_back({L"Player " + std::to_wstring(u.owner + 1) +
                                     L" \u00b7 " + location(bases_.back()) +
                                     L" \u00b7 " +
                                     (u.type == 214 ? L"Start" : L"Depot"),
                                 -1, -1, color});
                preview_.markers.push_back(
                    {float(u.x) / (editor_->width() * 32),
                     float(u.y) / (editor_->height() * 32), color});
            }
        if (editor_)
          minimapPixels(preview_.terrain,
                        minimapTileColors(editor_->tileset(),
                                          megatileColors(editor_->tileset()),
                                          editor_->terrain()),
                        editor_->width(), editor_->height());
        base_.setItems(std::move(items)); base_.setValue(bases_.empty()?-1:0); select();
    }
    void stop() {
        tests_.clear(); table_.setCount(0); controls(); simRedraw();
        stats_.setText(L"Select a base, then start mining");
    }
    void close() {
        open=false; if (host()) host()->closeWindow(*this);
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
            stats_.setText(
                number(income, 0) + L" minerals/min \u00b7 " +
                std::to_wstring(tests_.size()) +
                (tests_.size() == 1 ? L" test" : L" tests") + L" \u00b7 " +
                std::to_wstring(t ? t->value.mining.patches.size() : 0) +
                L" selected patches");
        }
        table_.invalidate(); controls();
        if (!tests_.empty()) simRedraw();
    }
    void paint(draw::Canvas& c) override {
      update();
      const auto &th = stardraft::kit::theme();
      c.fill({0, 0, width(), height()}, kPanel);
      c.text({scale(80), 0, width() - scale(16), scale(20)}, L"Base", kDim,
             draw::kMiddle);
      if (details_)
        return;
      const draw::Color colors[]{draw::rgb(232, 190, 89),
                                 draw::rgb(103, 199, 173),
                                 draw::rgb(176, 154, 232)};
      for (int k = 0; k < 3; ++k) {
        const int y = scale(168 + k * 88);
        c.roundBox({scale(16), y, width() - scale(16), y + scale(80)},
                   th.chipDim, th.rowLine, scale(5));
        c.fill({scale(16), y + scale(10), scale(19), y + scale(70)}, colors[k]);
        c.text({width() - scale(150), y + scale(8), width() - scale(104),
                y + scale(32)},
               L"Limit", kDim, draw::kMiddle);
        c.text({width() - scale(42), y + scale(8), width() - scale(22),
                y + scale(32)},
               L"sec", kDim, draw::kMiddle);
        }
    }
    void resized() override {
      const int pad = scale(16), gap = scale(8), bh = scale(28);
      int x = pad, y = scale(68);
      auto field = [&](draw::View &v, int w) {
        v.setFrame({x, y, x + scale(w), y + bh});
        x += scale(w) + gap;
      };
      preview_.setFrame({pad, scale(6), pad + scale(52), scale(58)});
      base_.setFrame({scale(80), scale(20), width() - pad, scale(48)});
      field(race_, 100);
      field(workers_, 170);
      field(run_, 110);
      field(stopAll_, 104);
      x = pad;
      y = scale(104);
      field(speed_, 128);
      field(pause_, 80);
      field(copy_, 105);
      field(detailsButton_, 72);
      field(close_, 76);
      stats_.setFrame({pad, scale(136), width() - pad, scale(160)});
      table_.setFrame({pad, scale(168), width() - pad, height() - pad});
      for (int k = 0; k < 3; ++k) {
        const int sy = scale(168 + k * 88);
        guides_[k].setFrame(
            {scale(28), sy + scale(8), width() - scale(160), sy + scale(32)});
        budget_[k].setFrame({width() - scale(98), sy + scale(8),
                             width() - scale(48), sy + scale(32)});
        search_[k].setFrame({width() - scale(124), sy + scale(38),
                             width() - pad, sy + scale(66)});
        status_[k].setFrame(
            {scale(28), sy + scale(36), width() - scale(132), sy + scale(66)});
        progress_[k].setFrame(
            {scale(28), sy + scale(72), width() - scale(28), sy + scale(75)});
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
            maplab::appendMiningGuides(
                scene, t.value.mining, mask, gState.zoom,
                [&](const auto &guide) {
                  if (guide.type < 0 ||
                      guide.type >= int(editor_->unitTypes().size()))
                    return;
                  auto unit = makeUnit(editor_->unitTypes()[size_t(guide.type)],
                                       uint8_t(t.base.owner), uint16_t(guide.x),
                                       uint16_t(guide.y));
                  addUnitSprites(scene, unit, 0, 0, .4f);
                },
                true);
        }
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
    if (!gRoot || !gHost || !gState.ed || !gState.ed->isOpen()) return;
    simClose();
    if (!gMiningPanel.current()) gMiningPanel.setup();
    if (gMiningPanel.open) return;
    gMiningPanel.open=true;
    draw::WindowSpec spec{draw::WindowKind::Tool,{scale(40),scale(120),scale(580),scale(560)},L"Mining Lab"};
    if(gNoBoxes)gHost->snapshotInput(*gRoot,gRoot->width(),gRoot->height(),{});
    gHost->openWindow(*gRoot,gMiningPanel,spec,[]{gMiningPanel.open=false;});
    if(gNoBoxes)std::printf("map-lab: opened mining\n");
}
void miningPanelFrame() { gMiningPanel.update(); }
void miningLabForUnit(int index) { showMiningLabPanel(); gMiningPanel.openForUnit(index); }
