// The selection bar: a row of buttons over the selected units in Units mode (View > Show selection bar): Duplicate,
// Delete, Arrange (two units or more), Give to player, Snap to the grid, Properties (one unit). The commands are the
// menus' (no new function is here): the bar puts them where the work is. It shows with a selection and no unit to place,
// and not during a drag. The view's host paints it over the map (paintSelBar) and gives it the clicks (the buttons'
// places of the last paint: gSelBar).
enum { kSelBarArrange = -1, kSelBarGive = -2, kSelBarProps = -3 };  // (a button's id: these, or a command's)
struct SelBarButton {
    int icon = 0;
    const wchar_t* name = L"";
    int id = 0;
    draw::Rect r{};
};
struct SelBar {
    std::vector<SelBarButton> buttons;  // (none: no bar shows)
    draw::Rect box{};
    int hot = -1, pressed = -1;
};
inline SelBar gSelBar;

bool selBarWanted(const AppState& app) {
    return app.selBar && app.ed && app.ed->isOpen() && app.mode == Mode::Units && app.palette < 0 && !app.pasting && !app.pastingItems && app.drag == Drag::None &&
           !modeLayerHidden() && !mapPeekOn(app) && !app.ed->selection().empty();
}

// The bar's place: over the middle of the selection's box in the view; under it where the view has no room above;
// at the top of the view for a selection that fills the view. No bar for a selection out of the view.
void selBarLayout(const AppState& app, int viewW, int viewH, bool show) {
    SelBar& b = gSelBar;
    b.buttons.clear();
    b.box = {};
    if (!show || !selBarWanted(app) || app.zoom <= 0) return void(b.hot = b.pressed = -1);
    const auto& units = app.ed->units();
    const auto& sel = app.ed->selection();
    Rect all{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
    for (int i : sel) {
        if (i < 0 || size_t(i) >= units.size()) continue;
        const Rect r = app.ed->unitBox(units[size_t(i)]);
        all.left = std::min(all.left, r.left), all.top = std::min(all.top, r.top), all.right = std::max(all.right, r.right), all.bottom = std::max(all.bottom, r.bottom);
    }
    if (all.left > all.right) return void(b.hot = b.pressed = -1);
    const int sl = int((all.left - app.camX) * app.zoom), st = int((all.top - app.camY) * app.zoom), sr = int((all.right - app.camX) * app.zoom), sb = int((all.bottom - app.camY) * app.zoom);
    if (sr < 0 || sb < 0 || sl > viewW || st > viewH) return void(b.hot = b.pressed = -1);
    b.buttons.push_back({int(UiIcon::Duplicate), L"Duplicate", ID_EDIT_DUPLICATE});
    b.buttons.push_back({int(UiIcon::Delete), L"Delete", ID_EDIT_DELETE});
    if (sel.size() >= 2) b.buttons.push_back({int(UiIcon::AlignLeft), L"Arrange", kSelBarArrange});
    b.buttons.push_back({int(UiIcon::Owner), L"Give to player", kSelBarGive});
    if (canSnapToGrid(app)) b.buttons.push_back({int(UiIcon::Grid), L"Snap to the grid", ID_EDIT_SNAPGRID});
    if (sel.size() == 1) b.buttons.push_back({int(UiIcon::Settings), L"Properties", kSelBarProps});
    const int btn = scale(28), pad = scale(3), margin = scale(6), away = scale(12);
    const int w = int(b.buttons.size()) * btn + 2 * pad, h = btn + 2 * pad;
    if (w + 2 * margin > viewW || h + 2 * margin > viewH) return b.buttons.clear(), void(b.hot = b.pressed = -1);
    const int x = std::clamp((sl + sr) / 2 - w / 2, margin, viewW - margin - w);
    int y = st - away - h;
    if (y < margin) y = sb + away;
    if (y + h > viewH - scale(44)) y = margin;  // (the key hints are at the bottom)
    b.box = {x, y, x + w, y + h};
    for (size_t i = 0; i < b.buttons.size(); ++i) b.buttons[i].r = {x + pad + int(i) * btn, y + pad, x + pad + int(i + 1) * btn, y + pad + btn};
    if (b.hot >= int(b.buttons.size())) b.hot = -1;
    if (b.pressed >= int(b.buttons.size())) b.pressed = -1;
}

// The button at a point of the view (-1: none; -2: the bar, between its buttons).
int selBarHit(draw::Point pt) {
    const SelBar& b = gSelBar;
    if (b.buttons.empty() || !b.box.contains(pt)) return -1;
    for (size_t i = 0; i < b.buttons.size(); ++i)
        if (b.buttons[i].r.contains(pt)) return int(i);
    return -2;
}

// `show`: the host shows the bar now (no live game, no pie palette, no pan).
void paintSelBar(draw::Canvas& c, const AppState& app, int viewW, int viewH, bool show) {
    selBarLayout(app, viewW, viewH, show);
    const SelBar& b = gSelBar;
    if (b.buttons.empty()) return;
    c.roundBox(b.box, kPanel, kBorder, float(scale(6)));
    for (size_t i = 0; i < b.buttons.size(); ++i) {
        const SelBarButton& k = b.buttons[i];
        if (int(i) == b.hot) c.roundBox(k.r, int(i) == b.pressed ? kSel : kCtlHot, int(i) == b.pressed ? kAccentLine : kBorderStrong, float(scale(4)));
        c.icon(k.r, k.icon, int(i) == b.hot ? kText : kDim, scale(16));
    }
    if (b.hot < 0) return;
    // The hot button's name: a label under the bar (over it at the bottom of the view).
    const SelBarButton& k = b.buttons[size_t(b.hot)];
    const std::wstring name = tr(k.name);
    const int w = c.measure(name) + 2 * scale(7), h = scale(20);
    const int x = std::clamp((k.r.left + k.r.right) / 2 - w / 2, scale(2), std::max(scale(2), viewW - scale(2) - w));
    const int y = b.box.bottom + scale(4) + h <= viewH ? b.box.bottom + scale(4) : b.box.top - scale(4) - h;
    c.roundBox({x, y, x + w, y + h}, kPanel, kBorderStrong, float(scale(4)));
    c.text({x, y, x + w, y + h}, name, kText, draw::kCenter | draw::kMiddle);
}
