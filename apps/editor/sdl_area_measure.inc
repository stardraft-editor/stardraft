// Small nonmodal controls for the read-only area mask. Closing the window retains the selection.
namespace {
class AreaMeasurePanel final : public draw::View {
public:
    AreaMeasurePanel() {
        method.setItems({{tr(L"Outline")}, {tr(L"Mark tiles")}, {tr(L"Remove tiles")}});
        metric.setItems({{tr(L"All area")}, {tr(L"Walkable")}, {tr(L"Buildable")}});
        brush.setRange(1, 16), brush.setValue(1);
        method.onChange = [this](int value) {
            areaMeasureBegin();
            gAreaMeasure.tool = value, gAreaMeasure.held = false;
            gAreaMeasure.points.clear();
            refresh();
        };
        metric.onChange = [this](int value) {
            gAreaMeasure.mode = value;
            areaMeasureChanged();
        };
        brush.onChange = [this](int value) {
            gAreaMeasure.brush = value;
            areaMeasureChanged();
        };
        edit.setText(tr(L"Resume selection"));
        edit.onClick = [this] {
            areaMeasureBegin();
            refresh();
            shellFocusView();
        };
        finish.setText(tr(L"Finish"));
        finish.onClick = [this] {
            areaMeasureClose();
            refresh();
            shellFocusView();
        };
        undo.setText(tr(L"Undo selection"));
        undo.onClick = [this] {
            areaMeasureUndo();
            refresh();
        };
        clear.setText(tr(L"Clear"));
        clear.onClick = [this] {
            areaMeasureClear();
            refresh();
        };
        for (auto* v : std::vector<draw::View*>{&method, &metric, &brush, &edit, &finish, &undo, &clear}) add(*v, {});
    }
    void refresh() {
        const bool active = areaMeasureCurrent();
        if (active) {
            areaMeasureRecount();
            method.setValue(gAreaMeasure.tool), metric.setValue(gAreaMeasure.mode), brush.setValue(gAreaMeasure.brush);
        }
        brush.setEnabled(active && gAreaMeasure.tool != 0);
        edit.setEnabled(gState.ed && gState.ed->isOpen() && (!active || !gAreaMeasure.drawing));
        finish.setEnabled(active && gAreaMeasure.drawing && (gAreaMeasure.tool != 0 || gAreaMeasure.points.size() >= 3));
        undo.setEnabled(active && (!gAreaMeasure.points.empty() || !gAreaMeasure.history.empty()));
        clear.setEnabled(active && (!gAreaMeasure.points.empty() || gAreaMeasure.closed));
        invalidate();
    }

    bool key(const draw::KeyEvent& e) override {
        // Tool windows receive their own keys, including snapshot inputs. Enter must
        // finish here too, rather than depend on the main map retaining keyboard focus.
        if (!e.down || e.repeat || e.mods) return false;
        if (!areaMeasureKey(e.key)) return false;
        refresh();
        return true;
    }

private:
    void resized() override {
        method.setFrame({scale(16), scale(36), scale(204), scale(64)});
        metric.setFrame({scale(216), scale(36), width() - scale(16), scale(64)});
        brush.setFrame({scale(16), scale(102), width() - scale(16), scale(126)});
        edit.setFrame({scale(16), scale(144), scale(204), scale(172)});
        finish.setFrame({scale(216), scale(144), width() - scale(16), scale(172)});
        undo.setFrame({scale(16), scale(184), scale(204), scale(212)});
        clear.setFrame({scale(216), scale(184), width() - scale(16), scale(212)});
    }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        auto text = [&](int y, const std::wstring& t, draw::Color color = kDim) {
            c.text({scale(16), scale(y), width() - scale(16), scale(y + 22)}, t, color);
        };
        text(8, tr(L"Selection method"));
        c.text({scale(216), scale(8), width() - scale(16), scale(30)}, tr(L"Show"), kDim);
        text(76, tr(L"Brush width ") + std::to_wstring(gAreaMeasure.brush) + (gAreaMeasure.brush == 1 ? tr(L" tile") : tr(L" tiles")));
        text(228, tr(L"Outline: click corners across separate clicks or drags."));
        text(250, tr(L"Finish closes the outline. Mark or remove tiles with strokes."));
        if (!areaMeasureCurrent()) {
            text(286, gState.ed && gState.ed->isOpen() ? tr(L"Resume selection to start a new area.")
                                                       : tr(L"Open a map to measure its terrain."));
            return;
        }
        areaMeasureRecount();
        const auto& r = gAreaMeasure.result;
        wchar_t totals[160];
        std::swprintf(totals, 160, L"All %.2f  /  Walkable %.2f  /  Buildable %.2f", r.totalNexus(), r.walkableNexus(), r.buildableNexus());
        text(286, totals, kText);
        text(310, tr(L"Nexus equivalents (12 tiles). Terrain only."));
    }
    kit::Dropdown method, metric;
    kit::Slider brush;
    kit::Button edit, finish, undo, clear;
};
AreaMeasurePanel gAreaPanel;
bool gAreaPanelOpen = false;
void showAreaMeasurePanel() {
    if (!gRoot || !gHost || gAreaPanelOpen) return;
    gAreaPanel.setSize(scale(480), scale(346));
    gAreaPanel.refresh();
    draw::WindowSpec spec{draw::WindowKind::Tool, {scale(60), scale(100), scale(540), scale(446)}, tr(L"Measure area")};
    gAreaPanelOpen = true;
    gHost->openWindow(*gRoot, gAreaPanel, spec, [] {
        gAreaPanelOpen = false;
        gAreaMeasure.drawing = false, gAreaMeasure.held = false;
        areaMeasureChanged();
    });
}
struct AreaMeasureUiHooks {
    AreaMeasureUiHooks() {
        gShowAreaMeasurePanel = showAreaMeasurePanel;
        gAreaMeasureChanged = [] { gAreaPanel.refresh(); };
    }
} gAreaMeasureUiHooks;
} // namespace

draw::View* areaMeasureShot(draw::Point& size) {
    areaMeasureBegin();
    size = {scale(480), scale(346)};
    gAreaPanel.setSize(size.x, size.y);
    gAreaPanel.refresh();
    return &gAreaPanel;
}
bool areaMeasureControlsCheck(const std::string& name, std::string& out) {
    if (name != "area-measure-controls") return false;
    if (!gState.ed || gState.ed->width() < 20 || gState.ed->height() < 20) {
        out += "area-measure-controls: fixture needs an open map at least 20x20 (WRONG)\n";
        return true;
    }
    auto saved = std::move(gAreaMeasure);
    const auto chk = gState.ed->chk().serialize();
    const auto revision = gState.ed->revision();
    int count = 0, failures = 0;
    auto check = [&](bool yes, const char* why) {
        ++count;
        if (!yes) ++failures, out += std::string("area-measure-controls: ") + why + " (WRONG)\n";
    };
    areaMeasureBegin();
    areaMeasureDown(64, 64);
    areaMeasureUp();
    areaMeasureDown(192, 64);
    areaMeasureUp();
    check(gAreaMeasure.points.size() == 2 && gAreaMeasure.drawing, "separate clicks lost outline");
    areaMeasureDown(192, 160);
    areaMeasureMove(64, 160);
    areaMeasureUp();
    check(gAreaMeasure.drawing && !gAreaMeasure.closed, "drag release closed outline");
    // Route Enter through the floating tool's router, as the real host and the
    // snapshot driver do after opening the nonmodal panel.
    draw::Router toolKeys(gAreaPanel);
    draw::KeyEvent enter;
    enter.key = draw::Key::Enter, enter.down = false;
    check(!toolKeys.key(enter) && gAreaMeasure.drawing, "Enter key-up finished selection");
    enter.down = true;
    check(toolKeys.key(enter), "tool window did not take Enter");
    check(gAreaMeasure.result.tiles.size() == 12 && !gAreaMeasure.drawing, "outline did not measure one Nexus");
    areaMeasureBegin();
    gAreaMeasure.tool = 1;
    areaMeasureDown(336, 336);
    areaMeasureMove(464, 336);
    areaMeasureUp();
    areaMeasureRecount();
    check(gAreaMeasure.result.tiles.size() == 17, "mark stroke has gaps or lost existing area");
    check(gAreaMeasure.drawing, "mark stroke cannot continue");
    areaMeasureDown(336, 400);
    areaMeasureUp();
    areaMeasureRecount();
    check(gAreaMeasure.result.tiles.size() == 18, "second stroke was not retained");
    gAreaMeasure.tool = 2;
    areaMeasureDown(112, 112);
    areaMeasureUp();
    areaMeasureRecount();
    check(gAreaMeasure.result.tiles.size() == 17, "erase did not make a hole");
    areaMeasureUndo();
    areaMeasureRecount();
    check(gAreaMeasure.result.tiles.size() == 18, "local undo did not restore erased tile");
    areaMeasureClose();
    areaMeasureBegin();
    areaMeasureRecount();
    check(gAreaMeasure.result.tiles.size() == 18 && gAreaMeasure.drawing, "resume lost selected area");
    areaMeasureClear();
    areaMeasureRecount();
    check(gAreaMeasure.result.tiles.empty(), "clear left selected tiles");
    areaMeasureUndo();
    areaMeasureRecount();
    check(gAreaMeasure.result.tiles.size() == 18, "clear could not be undone");
    areaMeasureBegin();
    gAreaMeasure.tool = 0;
    areaMeasureDown(64, 64);
    areaMeasureUp();
    areaMeasureDown(192, 64);
    areaMeasureUp();
    areaMeasureDown(192, 160);
    areaMeasureMove(64, 160);
    areaMeasureUp();
    check(mapEnterSdl() && !gAreaMeasure.drawing && gAreaMeasure.closed, "main map Enter did not finish outline");
    check(gState.ed->revision() == revision && gState.ed->chk().serialize() == chk, "area selection changed saved map");
    gAreaMeasure = std::move(saved);
    gAreaPanel.refresh();
    out += "area-measure-controls: " + std::to_string(count) + " checks, " + std::to_string(failures) + " failed\n";
    return true;
}
