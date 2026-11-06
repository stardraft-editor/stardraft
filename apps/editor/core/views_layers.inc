// The Layer List (Map Layers' list: in the side bar, or in a window of its own in every mode, View > Floating palettes;
// a prototype; the model: map_layers.inc): a row of icons (new, delete, help), then the layers from the top down, with
// the Background at the bottom. Each row: the eye (show or hide), the name, and under the mouse the grip and the lock (a
// locked layer always shows it). A click selects (Ctrl: one more; Shift: a run), a double click renames, a drag moves a
// layer in the list, a right click opens the menu with the rest. The Background cannot be renamed or moved.

int shellContextMenuAt(draw::View& from, draw::Point at, const std::vector<std::wstring>& items);  // (shell_sdl.inc)

class LayersPanelView final : public draw::View {
public:
    void changed() { invalidate(); }
    void paint(draw::Canvas& c) override {
        const int w = width(), h = height();
        c.fill({0, 0, w, h}, kPanel);
        const auto hot = [this](draw::Rect r) { return hasPointer_ && r.contains(pointer_); };
        const MapLayersState& s = gMapLayers;
        kitpaint::drawIconFace(c, newRect(), UiIcon::Add, kitpaint::Tone::Quiet, false, hot(newRect()));
        kitpaint::drawIconFace(c, deleteRect(), UiIcon::Delete, kitpaint::Tone::Quiet, false, hot(deleteRect()), s.on && s.active >= 0);
        kitpaint::drawIconFace(c, helpRect(), UiIcon::Help, kitpaint::Tone::Quiet, false, hot(helpRect()));
        rows_.clear();
        int y = listTop();
        const int rowH = scale(28);
        for (int r = int(s.stack.layers.size()) - 1; r >= -1; --r) {
            const draw::Rect row{0, y, w, y + rowH};
            rows_.push_back({row, r});
            const bool picked = s.picked.count(r) > 0 || (s.picked.empty() && r == s.active);
            const bool over = hasPointer_ && row.contains(pointer_);
            kitpaint::drawListRow(c, row, picked || r == s.active, over);
            const MapLayer* layer = r >= 0 ? &s.stack.layers[size_t(r)] : &s.stack.background;
            const draw::Rect eye = eyeRect(y), lock = lockRect(y);
            kitpaint::drawIconFace(c, eye, layer->visible ? UiIcon::Show : UiIcon::ShowOff, kitpaint::Tone::Quiet, false, over && eye.contains(pointer_));
            const int nameLeft = eye.right + scale(6), nameRight = lock.left - scale(4);
            const std::wstring name = r >= 0 ? widen(layer->name) : tr(L"Background");
            c.text({nameLeft, y, nameRight, y + rowH}, name, !layer->visible ? kDim : r == s.active ? kOnText : kText, draw::kMiddle | draw::kEllipsis);
            if (layer->locked || over)
                kitpaint::drawIconFace(c, lock, layer->locked ? UiIcon::Lock : UiIcon::Unlock, layer->locked ? kitpaint::Tone::Primary : kitpaint::Tone::Quiet, false, over && lock.contains(pointer_));
            // The grip: the row can be dragged. Under the mouse, or while it is dragged.
            if (r >= 0 && (over || r == dragRow_)) {
                const draw::Rect g = gripRect(y);
                const int dot = std::max(2, scale(2));
                for (int gy = 0; gy < 3; ++gy)
                    for (int gx = 0; gx < 2; ++gx) {
                        const int x0 = g.left + gx * scale(5), y0 = g.top + gy * scale(5);
                        c.fill({x0, y0, x0 + dot, y0 + dot}, kDim);
                    }
            }
            y += rowH;
        }
        // The list's drag: where the layer goes.
        if (dragRow_ >= 0 && dragTo_ != dragRow_)
            for (const Row& rw : rows_)
                if (rw.index == dragTo_) {
                    const int ly = dragTo_ > dragRow_ ? rw.r.top : rw.r.bottom - scale(2);
                    c.fill({0, ly, w, ly + scale(2)}, kAccentBright);
                }
    }
    bool mouse(const draw::MouseEvent& e) override {
        // A press in the list goes to Map Layers, after what it does (the list can move with the mode). A right click: before
        // its menu.
        using draw::MouseKind;
        if (e.kind == MouseKind::Down && e.button == 1 && appState().mode != Mode::Layers) setMode(Mode::Layers);
        const bool handled = press(e);
        if (e.kind == MouseKind::Down && e.button == 0 && appState().mode != Mode::Layers && dragRow_ < 0) setMode(Mode::Layers);
        return handled;
    }
    bool press(const draw::MouseEvent& e) {
        using draw::MouseKind;
        const auto rowAt = [&](draw::Point p, int& index, draw::Rect* rect = nullptr) {
            for (const Row& rw : rows_)
                if (rw.r.contains(p)) {
                    if (rect) *rect = rw.r;
                    return index = rw.index, true;
                }
            return false;
        };
        int index = 0;
        draw::Rect row;
        switch (e.kind) {
        case MouseKind::Down:
            pointer_ = e.pt, hasPointer_ = true;
            if (e.button == 0 && newRect().contains(e.pt)) return mapLayersNew(), true;
            if (e.button == 0 && deleteRect().contains(e.pt)) return mapLayersDelete(), true;
            if (e.button == 0 && helpRect().contains(e.pt)) return mapLayersInfo(), true;
            if (!rowAt(e.pt, index, &row)) {
                if (e.button == 1) rowMenu(-2);
                return true;
            }
            if (e.button == 1) return rowMenu(index), true;
            if (e.button != 0) return true;
            if (eyeRect(row.top).contains(e.pt)) return mapLayersToggleVisible(index), true;
            if (lockRect(row.top).contains(e.pt)) return mapLayersToggleLock(index), true;
            mapLayersPick(index, e.mods);
            if (index >= 0) {
                dragRow_ = index, dragTo_ = index;
                if (host()) host()->capture(*this, true);
            }
            return true;
        case MouseKind::Move:
            pointer_ = e.pt, hasPointer_ = true;
            if (dragRow_ >= 0 && rowAt(e.pt, index) && index >= 0) dragTo_ = index;
            invalidate();
            return true;
        case MouseKind::Leave:
            hasPointer_ = false;
            invalidate();
            return false;
        case MouseKind::Up:
            if (host()) host()->capture(*this, false);
            if (dragRow_ >= 0 && dragTo_ != dragRow_) mapLayersMoveRow(dragRow_, dragTo_);
            dragRow_ = dragTo_ = -2;
            invalidate();
            if (appState().mode != Mode::Layers) setMode(Mode::Layers);
            return true;
        case MouseKind::DoubleClick:
            dragRow_ = dragTo_ = -2;
            if (e.button != 0 || !rowAt(e.pt, index, &row)) return true;
            // (two quick clicks on the eye or the lock are two clicks)
            if (eyeRect(row.top).contains(e.pt)) mapLayersToggleVisible(index);
            else if (lockRect(row.top).contains(e.pt)) mapLayersToggleLock(index);
            else mapLayersRename(index);
            return true;
        default: return true;
        }
    }
    draw::Cursor cursor(draw::Point p) override {
        if (dragRow_ >= 0) return draw::Cursor::SizeNS;
        if (newRect().contains(p) || deleteRect().contains(p) || helpRect().contains(p)) return draw::Cursor::Hand;
        for (const Row& rw : rows_)
            if (rw.r.contains(p)) {
                if (rw.index >= 0 && gripRect(rw.r.top).contains(p)) return draw::Cursor::SizeNS;
                if (eyeRect(rw.r.top).contains(p) || lockRect(rw.r.top).contains(p)) return draw::Cursor::Hand;
            }
        return draw::Cursor::Arrow;
    }
    std::wstring tip(draw::Point p) override {
        if (newRect().contains(p)) return L"New layer (Ctrl+Alt+N)";
        if (deleteRect().contains(p)) return L"Delete the selected layers (Del in Map Layers)";
        if (helpRect().contains(p)) return L"How layers get into the map that you play";
        for (const Row& rw : rows_)
            if (rw.r.contains(p)) {
                if (eyeRect(rw.r.top).contains(p)) return L"Show or hide (Ctrl+Alt+H). With nothing showing, the map is black";
                if (lockRect(rw.r.top).contains(p)) return L"Lock or unlock: a locked layer does not change (Ctrl+Alt+L)";
                if (rw.index >= 0 && gripRect(rw.r.top).contains(p)) return L"Drag to move the layer in the list";
            }
        return {};
    }

private:
    struct Row {
        draw::Rect r;
        int index = 0;
    };
    int listTop() const { return scale(30); }
    draw::Rect newRect() const { return {scale(2), scale(2), scale(26), scale(26)}; }
    draw::Rect deleteRect() const { return {scale(28), scale(2), scale(52), scale(26)}; }
    draw::Rect helpRect() const { return {width() - scale(26), scale(2), width() - scale(2), scale(26)}; }
    draw::Rect eyeRect(int y) const { return {scale(4), y + scale(3), scale(26), y + scale(25)}; }
    draw::Rect gripRect(int y) const { return {width() - scale(16), y + scale(8), width() - scale(4), y + scale(22)}; }
    draw::Rect lockRect(int y) const { return {width() - scale(40), y + scale(3), width() - scale(18), y + scale(25)}; }
    // The rest of the commands. index -2: under the rows.
    void rowMenu(int index) {
        const MapLayersState& s = gMapLayers;
        if (index >= int(s.stack.layers.size())) return;  // (an undo took it away)
        if (index >= -1 && !s.picked.count(index)) mapLayersPick(index, 0);
        std::vector<std::wstring> items;
        std::vector<std::function<void()>> runs;
        const auto add = [&](const std::wstring& text, std::function<void()> run) {
            items.push_back(text);
            runs.push_back(std::move(run));
        };
        const auto say = [](const wchar_t* text) { return std::wstring(tr(text)); };
        if (index >= 0) {
            const MapLayer& layer = s.stack.layers[size_t(index)];
            add(say(L"Rename\x2026") + L"\tF2", [index] { mapLayersRename(index); });
            add(say(L"Duplicate") + L"\tCtrl+D", [index] { mapLayersDuplicate(index); });
            add(say(L"Delete") + L"\tDel", [] { mapLayersDelete(); });
        }
        if (index >= -1) add(say((index >= 0 ? s.stack.layers[size_t(index)] : s.stack.background).visible ? L"Hide" : L"Show") + L"\tCtrl+Alt+H", [index] { mapLayersToggleVisible(index); });
        if (index >= -1) add(say((index >= 0 ? s.stack.layers[size_t(index)] : s.stack.background).locked ? L"Unlock" : L"Lock") + L"\tCtrl+Alt+L", [index] { mapLayersToggleLock(index); });
        if (s.picked.size() > 1 && index >= -1) add(say(L"Merge the selected layers"), [] { mapLayersMerge(); });
        else if (index >= 0) add(say(L"Merge down") + L"\tCtrl+Alt+E", [index] { mapLayersMergeDown(index); });
        if (index == -2) add(say(L"New layer") + L"\tCtrl+Alt+N", [] { mapLayersNew(); });
        if (s.stack.layers.size() > 1) add(say(L"Sort by name"), [] { mapLayersSort(); });
        if (s.on) add(say(L"Flatten"), [] { mapLayersFlatten(); });
        if (items.empty()) return;
        const int pick = shellContextMenuAt(*this, pointer_, items);
        if (pick >= 1 && pick <= int(runs.size())) runs[size_t(pick - 1)]();
    }
    std::vector<Row> rows_;
    draw::Point pointer_{};
    bool hasPointer_ = false;
    int dragRow_ = -2, dragTo_ = -2;
};
LayersPanelView gLayersPanel;
void layersPanelChanged() {
    gLayersPanel.changed();
    shellRelayout();
}

// File > Export: the map as it flattens, with no layers, in another file.
bool mapLayersExport(const fs::path& path) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return false;
    std::error_code same;
    if (!app.ed->path().empty() && fs::equivalent(path, app.ed->path(), same)) {
        setMessage(L"Select another file: an export over the open map would remove its layers");
        return false;
    }
    std::string err;
    if (!app.ed->exportFlattened(path, err)) {
        setMessage(L"Could not export the map: " + widen(err));
        return false;
    }
    setMessage(L"Exported " + path.filename().wstring() + L": the layers are flattened");
    return true;
}
