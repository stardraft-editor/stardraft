// SPDX-License-Identifier: MIT
// A value snapshot of the current map, simulated on a worker. No edit or undo step.
#include "../../tools/map_lab/session.h"
#include "../kit/form.h"
#include <iomanip>
#include <sstream>

namespace {
class MapLabView final : public draw::View {
public:
    bool open = false, rush = false;
    int picking = 0;
    bool consumeUp = false;
    MapLabView() {
        race_.setItems({{L"Zerg"}, {L"Terran"}, {L"Protoss"}});
        race_.setValue(1);
        workers_.setItems({{L"One per mineral field"}, {L"4"}, {L"8"}, {L"9"}, {L"12"},
                           {L"16"}, {L"18"}, {L"24"}, {L"27"}, {L"32"}, {L"60"}});
        workers_.setValue(0);
        speed_.setItems({{L"1x"}, {L"4x"}, {L"16x"}});
        speed_.setValue(0);
        speed_.onChange = [this](int) { if (session_) session_->speed(paused_ ? 0 : speed()); };
        base_.onChange = [this](int n) {
            if (n >= 0 && n < int(bases_.size())) start_ = bases_[size_t(n)];
            coordinates();
        };
        startPoint_.setText(L"Select the start on the map");
        endPoint_.setText(L"Select the end on the map");
        startPoint_.onClick = [this] { picking = 1; setMessage(L"Click the start point on the map. Esc cancels."); };
        endPoint_.onClick = [this] { picking = 2; setMessage(L"Click the end point on the map. Esc cancels."); };
        run_.setText(L"Start");
        run_.setTone(stardraft::kit::Tone::Primary);
        run_.onClick = [this] { start(); };
        pause_.setText(L"Pause");
        pause_.onClick = [this] {
            paused_ = !paused_;
            pause_.setText(paused_ ? L"Resume" : L"Pause");
            if (session_) session_->speed(paused_ ? 0 : speed());
        };
        stop_.setText(L"Stop");
        stop_.onClick = [this] { stop(); };
        copy_.setText(L"Copy");
        copy_.onClick = [this] { if (host()) host()->setClipboardText(narrow(report_.text())); };
        close_.setText(L"Close");
        close_.onClick = [this] { close(); };
        const wchar_t* names[]{L"Optimize positions", L"Optimize cargo", L"Optimize cycles"};
        for (int i = 0; i < 3; ++i) {
            search_[i].setText(names[i]);
            search_[i].setTip(L"Search for one second. The report shows the result.");
            search_[i].onClick = [this, i] { if (session_) session_->search(i, 1); };
            add(search_[i], {});
        }
        report_.setReadOnly(true);
        report_.setMultiline(true);
        report_.setCopyWhole(true);
        report_.setWrap(false);
        add(form_, {});
        add(report_, {});
        for (auto* button : {&run_, &pause_, &stop_, &copy_, &close_}) add(*button, {});
    }
    void setup(bool forRush) {
        stop();
        rush = forRush;
        editor_ = gState.ed.get();
        revision_ = editor_->revision();
        bases_.clear();
        std::vector<stardraft::kit::DropdownItem> items;
        for (const auto& u : editor_->units()) if (u.type == 214) {
            bases_.push_back({u.x, u.y});
            items.push_back({L"Player " + std::to_wstring(u.owner + 1) + L" (" +
                             std::to_wstring(u.x) + L", " + std::to_wstring(u.y) + L")"});
        }
        base_.setItems(std::move(items));
        base_.setValue(bases_.empty() ? -1 : 0);
        start_ = bases_.empty() ? maplab::RushPoint{} : bases_[0];
        end_ = bases_.size() > 1 ? bases_[1] : start_;
        form_.clear();
        form_.addSection(rush ? L"Rush distance" : L"Mining Lab");
        form_.addRow(L"Race", race_);
        form_.addRow(L"Start location", base_);
        form_.addRow(L"Start point", startPoint_);
        if (rush) form_.addRow(L"End point", endPoint_);
        else form_.addRow(L"Workers", workers_);
        form_.addRow(L"Speed", speed_);
        form_.addNote(rush ? L"Select two open points. Each worker runs in its own copy of the map. Time is game time at Fastest."
                          : L"Mineral mining only. The depot is placed at the start point. Rates include the opening travel time.");
        report_.setText(L"Set the values, then select Start. The map keeps all edits.");
        coordinates();
        buttons();
        relayout();
    }
    void paint(draw::Canvas& c) override {
        update();
        form_.layout(c);
        placeResults();
        c.fill({0, 0, width(), height()}, kPanel);
    }
    void resized() override { relayout(); }
    bool key(const draw::KeyEvent& e) override {
        if (e.down && e.key == draw::Key::Escape) {
            if (picking) picking = 0;
            else close();
            return true;
        }
        return false;
    }
    bool closeRequested() override { stop(); open = false; return true; }
    void close() {
        stop();
        if (host()) host()->closeWindow(*this);
        open = false;
    }
    void stop() {
        session_.reset();
        picking = 0;
        paused_ = false;
        pause_.setText(L"Pause");
        gLabScene = nullptr;
        buttons();
        simRedraw();
    }
    void point(int x, int y) {
        (picking == 1 ? start_ : end_) = {x, y};
        picking = 0;
        coordinates();
    }
    void update() {
        if (!open) return;
        if (editor_ != gState.ed.get() || !gState.ed || !gState.ed->isOpen() ||
            editor_->revision() != revision_ || simBarShown()) {
            if (session_ || picking) {
                stop();
                report_.setText(L"The map or simulation changed. Open the tool again to measure the current map.");
            }
            return;
        }
        if (!session_) return;
        const bool wasReady = latest_.ready;
        latest_ = session_->snapshot();
        if (gNoBoxes && latest_.ready && !wasReady)
            std::printf("map-lab: ready %s\n", rush ? "rush" : "mining");
        if (!latest_.error.empty()) {
            report_.setText(widen(latest_.error));
            stop();
            return;
        }
        if (!latest_.ready) return;
        std::wostringstream text;
        text << std::fixed << std::setprecision(2);
        text << (rush ? L"Rush distance" : L"Mining Lab") << L"\r\n";
        text << L"Race: " << race_.items()[size_t(race_.value())].label << L"\r\n";
        if (!rush) text << L"Workers: " << latest_.mining.units.size()-1 << L"\r\n";
        text << L"Start: " << start_.x << L", " << start_.y << L"\r\n";
        if (rush) {
            text << L"End: " << end_.x << L", " << end_.y << L"\r\n";
            text << L"Run\tSeconds\tPixels\tResult\r\n";
            for (const auto& path : latest_.rush.paths) {
                double length = 0;
                for (size_t i = 1; i < path.points.size(); ++i)
                    length += std::hypot(double(path.points[i].x - path.points[i-1].x),
                                         double(path.points[i].y - path.points[i-1].y));
                text << path.id << L'\t' << path.seconds() << L'\t' << length << L'\t'
                     << (path.reached ? L"Reached" : path.finished ? L"Not reached" : L"Running") << L"\r\n";
            }
        } else {
            const auto& mine = latest_.mining;
            text << L"Game seconds: " << mine.frame * .042 << L"\r\n"
                 << L"Minerals per minute: " << mine.income << L"\r\n\r\n"
                 << L"Field X\tField Y\tCycle seconds\tMinerals per minute\r\n";
            for (const auto& field : mine.patches)
                text << field.x << L'\t' << field.y << L'\t' << field.lastCycle * .042
                     << L'\t' << field.perMinute << L"\r\n";
            const wchar_t* names[]{L"Positions", L"Cargo", L"Cycles"};
            for (int i = 0; i < 3; ++i) {
                const auto& search = mine.searches[i];
                if (search.running || search.finished)
                    text << L"\r\n" << names[i] << L": " << (search.running ? L"Searching" : L"Finished")
                         << L", " << search.score << L" minerals per minute";
            }
        }
        if (latest_.done) text << L"\r\nMeasurement complete.";
        const auto value = text.str();
        if (value != report_.text()) report_.setText(value);
        buttons();
        simRedraw();
    }
    void scene(render::Scene& scene) const {
        auto drawSprite = [&](const maplab::MiningFrame::Sprite& image) {
            render::SpriteDraw sprite;
            sprite.x = float(image.x), sprite.y = float(image.y);
            sprite.image = image.image, sprite.frame = image.frame, sprite.owner = uint8_t(image.owner);
            sprite.flip = image.flip, sprite.shadow = image.shadow;
            sprite.drawFunction = gState.ed->unitSprites().drawFunction(image.image);
            scene.sprites.push_back(sprite);
        };
        if (!rush) for (const auto& sprite : latest_.mining.sprites) drawSprite(sprite);
        else for (const auto& path : latest_.rush.paths) {
            for (const auto& sprite : path.sprites) drawSprite(sprite);
            const size_t stride = std::max<size_t>(1, path.points.size()/512);
            for (size_t i = 1; i < path.points.size(); i += stride) {
                render::RectDraw line;
                line.x0 = float(path.points[i-1].x), line.y0 = float(path.points[i-1].y);
                const auto& to = path.points[std::min(i+stride-1, path.points.size()-1)];
                line.x1 = float(to.x), line.y1 = float(to.y);
                line.line = true, line.thickness = 2;
                line.rgba = path.reached ? 0x60E080FFu : 0xE0B060FFu;
                scene.rects.push_back(line);
            }
        }
    }
private:
    const Editor* editor_ = nullptr;
    uint64_t revision_ = 0;
    bool paused_ = false;
    maplab::RushPoint start_, end_;
    std::vector<maplab::RushPoint> bases_;
    maplab::Snapshot latest_;
    std::unique_ptr<maplab::Session> session_;
    stardraft::kit::Form form_;
    stardraft::kit::Dropdown race_, base_, workers_, speed_;
    stardraft::kit::Button startPoint_, endPoint_, run_, pause_, stop_, copy_, close_, search_[3];
    stardraft::kit::TextField report_;
    int speed() const { return speed_.value() == 2 ? 16 : speed_.value() == 1 ? 4 : 1; }
    void coordinates() {
        startPoint_.setText(L"Select start (" + std::to_wstring(start_.x) + L", " + std::to_wstring(start_.y) + L")");
        endPoint_.setText(L"Select end (" + std::to_wstring(end_.x) + L", " + std::to_wstring(end_.y) + L")");
    }
    void buttons() {
        const bool running = session_ && !latest_.done;
        race_.setEnabled(!session_);
        workers_.setEnabled(!session_);
        base_.setEnabled(!session_);
        startPoint_.setEnabled(!session_);
        endPoint_.setEnabled(!session_);
        pause_.setEnabled(running);
        stop_.setEnabled(bool(session_));
        for (auto& button : search_) button.setEnabled(running && latest_.ready && !rush);
    }
    void relayout() {
        const int pad = scale(stardraft::kit::kDialogPad);
        form_.setFrame({pad, pad, width()-pad, pad + scale(270)});
        placeResults();
    }
    void placeResults() {
        const int pad = scale(stardraft::kit::kDialogPad), bh = scale(stardraft::kit::kButtonH);
        const int gap = scale(stardraft::kit::kButtonGap), bw = scale(stardraft::kit::kButtonMinW);
        const int y = pad + form_.contentHeight() + gap;
        for (int i = 0; i < 3; ++i) search_[i].setFrame(rush ? draw::Rect{} : draw::Rect(pad + i*scale(175), y, pad + i*scale(175) + scale(165), y+bh));
        report_.setFrame({pad, y + (rush ? 0 : bh+gap), width()-pad, height()-pad-bh-gap});
        int x = pad;
        for (auto* b : {&run_, &pause_, &stop_, &copy_, &close_}) {
            b->setFrame({x, height()-pad-bh, x+bw, height()-pad});
            x += bw+gap;
        }
    }
    void start();
};
MapLabView gMapLab;
void labScene(render::Scene& scene) { gMapLab.scene(scene); }
void MapLabView::start() {
    if (!gState.ed || editor_ != gState.ed.get() || revision_ != editor_->revision()) {
        report_.setText(L"Open the tool again to use the current map.");
        return;
    }
    stop();
    if (gState.data.unitDataLoader()) {
        report_.setText(L"These measurements need the standard Brood War data.");
        return;
    }
    try {
        auto data = std::make_shared<GameData>();
        std::string error;
        if (!data->open(gInstallInUse, error)) throw std::runtime_error(error);
        auto files = std::make_shared<const std::map<std::string, Bytes>>(simDataFiles());
        maplab::Request request;
        request.mode = rush ? maplab::Mode::Rush : maplab::Mode::Mining;
        request.scenario = editor_->chk().serialize();
        request.start = start_, request.end = end_, request.race = race_.value();
        static const int counts[]{0, 4, 8, 9, 12, 16, 18, 24, 27, 32, 60};
        request.workers = counts[std::clamp(workers_.value(), 0, 10)];
        request.read = [data, files](std::string name) {
            std::replace(name.begin(), name.end(), '\\', '/');
            for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (auto found = files->find(name); found != files->end()) return found->second;
            Bytes bytes;
            std::string error;
            if (!data->readFile(name, bytes, error)) throw std::runtime_error(error);
            return bytes;
        };
        latest_ = {};
        session_ = std::make_unique<maplab::Session>(std::move(request));
        session_->speed(speed());
        gLabScene = labScene;
        report_.setText(L"Loading the map and game data\u2026");
        buttons();
    } catch (const std::exception& e) { report_.setText(widen(e.what())); }
}
void showMapLab(bool rush) {
    if (!gRoot || !gHost || !gState.ed || !gState.ed->isOpen()) return;
    simClose();
    gMapLab.setup(rush);
    if (gMapLab.open) return;
    gMapLab.open = true;
    draw::WindowSpec spec{draw::WindowKind::Tool, {scale(40), scale(40), scale(660), scale(690)}, L"Map measurements"};
    spec.resizable = true;
    // Register the snapshot's root before a menu opens a floating tool. This also
    // gives the tool its own input router, as a real window has.
    if (gNoBoxes) gHost->snapshotInput(*gRoot, gRoot->width(), gRoot->height(), {});
    gHost->openWindow(*gRoot, gMapLab, spec, [] { gMapLab.stop(); gMapLab.open = false; });
    if (gNoBoxes) std::printf("map-lab: opened %s\n", rush ? "rush" : "mining");
}
}
void miningLabCommand() { showMapLab(false); }
void rushMeasureCommand() { showMapLab(true); }
void mapLabFrame() { gMapLab.update(); }
bool mapLabKey(const draw::KeyEvent& e) {
    if (gMapLab.picking && e.down && e.key == draw::Key::Escape) {
        gMapLab.picking = 0;
        setMessage(L"Point selection canceled");
        return true;
    }
    return false;
}
bool mapLabMouse(const draw::MouseEvent& e) {
    if (gMapLab.consumeUp && e.kind == draw::MouseKind::Up && e.button == 0) {
        gMapLab.consumeUp = false;
        return true;
    }
    if (!gMapLab.open || !gMapLab.picking) return false;
    if (e.kind == draw::MouseKind::Down && e.button == 0 && gState.ed) {
        float x, y;
        toWorld(gState, float(e.pt.x), float(e.pt.y), x, y);
        gMapLab.point(std::clamp(int(x), 0, gState.ed->width()*32-1),
                      std::clamp(int(y), 0, gState.ed->height()*32-1));
        gMapLab.consumeUp = true;
    }
    return e.button != 2 && (e.kind == draw::MouseKind::Down ||
                            e.kind == draw::MouseKind::Up ||
                            e.kind == draw::MouseKind::DoubleClick);
}
