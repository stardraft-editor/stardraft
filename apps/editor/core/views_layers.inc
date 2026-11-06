// The Layer List (in the side bar in Map Layers mode; over the map in the others with View > Floating palettes > Layer list;
// a prototype; the model: map_layers.inc): the
// layers from the top down, with the Background at the bottom. Each row: the eye (show or hide), the lock, the name, and
// under the mouse the grip. A click selects (Ctrl: one more; Shift: a run), a double click renames, a drag moves a layer
// in the list, a right click opens its menu. The Background cannot be renamed, moved or hidden.

class LayersPanelView final : public draw::View {
public:
    LayersPanelView()
        : new_(L"New", stardraft::kit::Tone::Normal, [] { mapLayersNew(); }),
          delete_(L"Delete", stardraft::kit::Tone::Quiet, [] { mapLayersDelete(); }),
          sort_(L"Sort", stardraft::kit::Tone::Quiet, [] { mapLayersSort(); }),
          merge_(L"Merge", stardraft::kit::Tone::Quiet, [] { mapLayersMerge(); }),
          flatten_(L"Flatten", stardraft::kit::Tone::Quiet, [] { mapLayersFlatten(); }) {
        new_.setTip(L"A new empty layer over the selected one");
        delete_.setTip(L"Remove the selected layers");
        sort_.setTip(L"Put the layers in the order of their names");
        merge_.setTip(L"Make the selected layers one layer. With the Background selected, they go into it");
        flatten_.setTip(L"Put all layers that show into the Background. Hidden layers go away");
        for (draw::View* v : {(draw::View*)&new_, (draw::View*)&delete_, (draw::View*)&sort_, (draw::View*)&merge_, (draw::View*)&flatten_}) add(*v, {0, 0, 0, 0});
    }
    void changed() { invalidate(); }
    int preferredHeight() const {
        const int rows = int(gMapLayers.stack.layers.size()) + 1;
        return scale(30) + rows * scale(26) + scale(66);
    }
    void paint(draw::Canvas& c) override {
        const int w = width(), h = height();
        c.fill({0, 0, w, h}, kPanel);
        c.fill({0, 0, w, 1}, kFaint), c.fill({0, h - 1, w, h}, kFaint), c.fill({0, 0, 1, h}, kFaint), c.fill({w - 1, 0, w, h}, kFaint);
        const int rowH = scale(26), pad = scale(8);
        c.text({pad, 0, w - pad, scale(28)}, tr(L"Layer List"), kText, draw::kMiddle);
        rows_.clear();
        const MapLayersState& s = gMapLayers;
        int y = scale(30);
        for (int r = int(s.stack.layers.size()) - 1; r >= -1; --r) {
            const draw::Rect row{1, y, w - 1, y + rowH};
            rows_.push_back({row, r});
            const bool picked = s.picked.count(r) > 0 || (s.picked.empty() && r == s.active);
            if (r == s.active) c.fill(row, kAccent, 110);
            else if (picked) c.fill(row, kAccent, 60);
            else if (r == hover_) c.fill(row, kFaint, 50);
            const MapLayer* layer = r >= 0 ? &s.stack.layers[size_t(r)] : &s.stack.background;
            const draw::Rect eye = eyeRect(y), lock = lockRect(y);
            if (r >= 0) {
                kitpaint::drawIconFace(c, eye, UiIcon::Show, kitpaint::Tone::Quiet, false, r == hover_ && eye.contains(pointer_), layer->visible);
                if (!layer->visible) c.line(eye.left + scale(4), eye.bottom - scale(5), eye.right - scale(4), eye.top + scale(5), kText, std::max(1, scale(2)));
            }
            drawLock(c, lock, layer->locked, r == hover_ && lock.contains(pointer_));
            const std::wstring name = r >= 0 ? widen(layer->name) : tr(L"Background");
            const int nameRight = w - pad - (r >= 0 ? scale(16) : 0);
            c.text({lock.right + scale(6), y, nameRight, y + rowH}, name, r >= 0 && !layer->visible ? kDim : kText, draw::kMiddle | draw::kEllipsis);
            // The grip: the row can be dragged. Under the mouse, or while it is dragged.
            if (r >= 0 && (r == hover_ || r == dragRow_)) {
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
                    c.fill({1, ly, w - 1, ly + scale(2)}, kAccentBright);
                }
        // The buttons: under the list, two rows.
        const int bh = scale(26), gap = scale(4);
        const int bw = (w - 2 * pad - 2 * gap) / 3;
        y += gap;
        place(new_, {pad, y, pad + bw, y + bh}), place(delete_, {pad + bw + gap, y, pad + 2 * bw + gap, y + bh}), place(sort_, {pad + 2 * (bw + gap), y, w - pad, y + bh});
        y += bh + gap;
        const int bw2 = (w - 2 * pad - gap) / 2;
        place(merge_, {pad, y, pad + bw2, y + bh}), place(flatten_, {pad + bw2 + gap, y, w - pad, y + bh});
        const bool on = s.on;
        delete_.setEnabled(on), sort_.setEnabled(on), merge_.setEnabled(on), flatten_.setEnabled(on);
    }
    bool mouse(const draw::MouseEvent& e) override {
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
            if (!rowAt(e.pt, index, &row)) return true;
            if (e.button == 1) {
                rowMenu(index);
                return true;
            }
            if (e.button != 0) return true;
            if (index >= 0 && eyeRect(row.top).contains(e.pt)) {
                mapLayersToggleVisible(index);
                return true;
            }
            if (lockRect(row.top).contains(e.pt)) {
                mapLayersToggleLock(index);
                return true;
            }
            mapLayersPick(index, e.mods);
            if (index >= 0) {
                dragRow_ = index, dragTo_ = index;
                if (host()) host()->capture(*this, true);
            }
            return true;
        case MouseKind::Move: {
            pointer_ = e.pt;
            const int was = hover_;
            hover_ = rowAt(e.pt, index) ? index : -2;
            if (dragRow_ >= 0 && hover_ >= 0 && hover_ != dragTo_) dragTo_ = hover_;
            invalidate();
            (void)was;
            return true;
        }
        case MouseKind::Leave:
            hover_ = -2;
            invalidate();
            return false;
        case MouseKind::Up:
            if (host()) host()->capture(*this, false);
            if (dragRow_ >= 0 && dragTo_ != dragRow_) mapLayersMoveRow(dragRow_, dragTo_);
            dragRow_ = dragTo_ = -2;
            invalidate();
            return true;
        case MouseKind::DoubleClick:
            if (e.button == 0 && rowAt(e.pt, index, &row) && !(index >= 0 && eyeRect(row.top).contains(e.pt)) && !lockRect(row.top).contains(e.pt)) mapLayersRename(index);
            dragRow_ = dragTo_ = -2;
            return true;
        default: return true;
        }
    }
    draw::Cursor cursor(draw::Point p) override {
        if (dragRow_ >= 0) return draw::Cursor::SizeNS;
        for (const Row& rw : rows_)
            if (rw.r.contains(p)) {
                if (rw.index >= 0 && gripRect(rw.r.top).contains(p)) return draw::Cursor::SizeNS;
                if ((rw.index >= 0 && eyeRect(rw.r.top).contains(p)) || lockRect(rw.r.top).contains(p)) return draw::Cursor::Hand;
            }
        return draw::Cursor::Arrow;
    }
    std::wstring tip(draw::Point p) override {
        for (const Row& rw : rows_)
            if (rw.r.contains(p)) {
                if (rw.index >= 0 && eyeRect(rw.r.top).contains(p)) return L"Show or hide the layer";
                if (lockRect(rw.r.top).contains(p)) return L"Lock or unlock: a locked layer does not change";
                if (rw.index >= 0 && gripRect(rw.r.top).contains(p)) return L"Drag to move the layer in the list";
            }
        return {};
    }

private:
    struct Row {
        draw::Rect r;
        int index = 0;
    };
    draw::Rect eyeRect(int y) const { return {scale(4), y + scale(2), scale(4) + scale(22), y + scale(24)}; }
    draw::Rect lockRect(int y) const { return {scale(28), y + scale(2), scale(28) + scale(22), y + scale(24)}; }
    draw::Rect gripRect(int y) const { return {width() - scale(18), y + scale(7), width() - scale(6), y + scale(21)}; }
    // A padlock: closed when locked, its bow open and the lock dim when not.
    void drawLock(draw::Canvas& c, draw::Rect r, bool locked, bool over) {
        const draw::Color col = locked ? kAccentBright : over ? kText : kFaint;
        const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, t = std::max(1, scale(2));
        c.fill({cx - scale(5), cy - scale(1), cx + scale(5), cy + scale(6)}, col);
        const int bowL = cx - scale(4), bowR = cx + scale(2), top = cy - scale(6);
        c.fill({bowL, top, bowL + t, cy - scale(1)}, col);
        c.fill({bowL, top, bowR + t, top + t}, col);
        if (locked) c.fill({bowR + scale(1), top, bowR + scale(1) + t, cy - scale(1)}, col);
        else c.fill({bowR + scale(1), top, bowR + scale(1) + t, top + scale(3)}, col);
    }
    void rowMenu(int index) {
        const MapLayersState& s = gMapLayers;
        if (index >= int(s.stack.layers.size())) return;  // (an undo took it away)
        if (!s.picked.count(index)) mapLayersPick(index, 0);
        const MapLayer* layer = index >= 0 ? &s.stack.layers[size_t(index)] : &s.stack.background;
        const bool on = s.on;
        std::vector<std::wstring> items;
        std::vector<std::function<void()>> runs;
        const auto add = [&](const wchar_t* text, std::function<void()> run) {
            items.push_back(tr(text));
            runs.push_back(std::move(run));
        };
        if (index >= 0) {
            add(L"Rename\x2026", [index] { mapLayersRename(index); });
            add(L"Duplicate", [index] { mapLayersDuplicate(index); });
            add(L"Delete", [] { mapLayersDelete(); });
            add(layer->visible ? L"Hide" : L"Show", [index] { mapLayersToggleVisible(index); });
        }
        add(layer->locked ? L"Unlock" : L"Lock", [index] { mapLayersToggleLock(index); });
        if (s.picked.size() > 1) add(L"Merge the selected layers", [] { mapLayersMerge(); });
        else if (index >= 0) add(L"Merge down", [index] { mapLayersMergeDown(index); });
        if (on) add(L"Flatten", [] { mapLayersFlatten(); });
        const int pick = shellContextMenu(items);
        if (pick >= 1 && pick <= int(runs.size())) runs[size_t(pick - 1)]();
    }
    void place(draw::View& v, draw::Rect r) { v.setFrame(r); }
    stardraft::kit::Button new_, delete_, sort_, merge_, flatten_;
    std::vector<Row> rows_;
    draw::Point pointer_{};
    int hover_ = -2, dragRow_ = -2, dragTo_ = -2;
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
