// The Units form's Display section (portable, core.inc): the button set a unit uses, its status panel and its
// wireframes (single, group, transport) with their previews, drawn on a draw::Canvas with the arithmetic the Windows
// form had. The program's parts come in through UnitDisplayHost: the Win32 program's implementation is in
// data_extras.inc (its dropdown faces with their click areas, the list picker, the names, the wireframe sets and the
// sprite blit). The host must live as long as the click areas it makes: their actions run after the paint.

class UnitDisplayHost {
public:
    virtual ~UnitDisplayHost() = default;
    virtual draw::Canvas& canvas() = 0;  // what the section draws on
    virtual UnitLooks& looks() = 0;      // the status panels and wireframes (the game's and the project's)
    virtual ButtonSets& buttons() = 0;   // the button sets (a unit's set)
    virtual void edited() = 0;           // a value changed
    // A click area with a tip and the arrow cursor (no click).
    virtual void tip(draw::Rect r, const std::wstring& tip) = 0;
    // A list picker of `items` under `box`: true with the value picked in `out`.
    virtual bool choose(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, draw::Rect box, std::string& out) = 0;
    // The entries of a kind ("units", "buttons") for a picker.
    virtual std::vector<ListPickItem> entryItems(const char* kind) = 0;
    virtual std::wstring buttonSetName(int set) = 0;
    virtual std::wstring unitName(int unit) = 0;
    virtual bool hasShields(int unit) = 0;
    // A dropdown face with its click area (the mouse wheel picks the next or the previous choice); its icon is entry
    // `iconEntry` of `iconDat` (nullptr: none); `open` (when set): the kind of entry `number` that the open button opens.
    virtual void pickBox(draw::Rect r, const std::wstring& text, bool changed, std::function<void(draw::Rect)> click, const std::wstring& tip,
                         const char* iconDat, int iconEntry, int number, const char* open) = 0;
    // The wireframes of the art View shows; nullptr when the game's files do not have them.
    virtual Wireframes* wireframes(WireKind kind) = 0;
    // A wireframe's picture, `zoom` screen pixels a picture pixel, its origin at (cx, cy).
    virtual void sprite(const Sprite& s, int cx, int cy, float zoom) = 0;
};

// The three wireframes a unit shows (`frames`: single, group, transport; -1 for none), as the status panel shows
// them at full health. At (x, top), to the right of the rows top..bottom; under them (from `bottom`) when the form
// is too narrow. Returns the bottom of what it drew, at least `bottom`.
int paintWireframePreviews(UnitDisplayHost& h, const RequirementLayout& l, int u, const int (&frames)[3], int x, int top, int bottom) {
    draw::Canvas& c = h.canvas();
    static const WireKind kinds[3] = {WireKind::Single, WireKind::Group, WireKind::Transport};
    static const wchar_t* captions[3] = {L"Wireframe", L"Group", L"Transport"};
    const int cap = scale(18), gap = scale(8);
    const int pic = std::max(scale(64), bottom - top);
    const bool under = x + 3 * pic + 2 * gap > l.colRight;
    if (under) x = l.x0 + l.labelW, top = bottom + scale(4);
    const bool shields = h.hasShields(u);
    const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}));  // ui::smallFont
    for (int k = 0; k < 3; ++k) {
        const draw::Rect r{x + k * (pic + gap), top, x + k * (pic + gap) + pic, top + pic};
        c.fill(r, draw::rgb(0, 0, 0));  // the status panel's black, in both themes
        c.frame(r, kBorder);
        Wireframes* set = frames[k] >= 0 ? h.wireframes(kinds[k]) : nullptr;
        const Sprite* s = set ? set->picture(frames[k], shields) : nullptr;
        if (s) {
            // One scale for the three, so that a group picture is smaller than a single one, as in the game: a single
            // wireframe's box (64 SD pixels, 128 HD2 pixels) fills the box. SD art in whole screen pixels a pixel.
            const float fit = float(pic - scale(6)) / float(set->hd() ? 128 : 64);
            const float zoom = std::min(set->hd() || fit < 1 ? fit : std::floor(fit), float(pic - scale(6)) / float(std::max(s->width, s->height)));
            h.sprite(*s, (r.left + r.right) / 2, (r.top + r.bottom) / 2, zoom);
        } else {
            c.text(r, frames[k] < 0 ? L"\x2014" : L"Not found", kDim, draw::kMiddle | draw::kCenter);
        }
        const draw::Rect cr{r.left, r.bottom, r.right, r.bottom + cap};
        c.text(cr, captions[k], kDim, draw::kMiddle | draw::kCenter | draw::kEllipsis);
        std::wstring tip = k == 0 ? L"The wireframe" : k == 1 ? L"The group wireframe" : L"The transport wireframe";
        tip += frames[k] < 0 ? L": the game has none for this unit" : L" at full health" + std::wstring(shields ? L" and full shields" : L"") +
                                                                          L", as View shows the art. Frame " + std::to_wstring(frames[k]) + L" of " +
                                                                          widen(Wireframes::fileName(kinds[k])) + (set && set->hd() ? L".dds.grp" : L".grp");
        h.tip({r.left, r.top, r.right, cr.bottom}, tip);
    }
    c.setFont(of);
    return std::max(bottom, top + pic + cap);
}

// A unit's Display section: the button set it uses, its status panel, its wireframes (single, group, transport).
// `wireTop`: where the wireframe rows start (the self-test scrolls to them). Returns the next y.
int paintUnitDisplay(UnitDisplayHost& h, const RequirementLayout& l, int u, int y, int* wireTop = nullptr) {
    if (u < 0 || u >= UnitLooks::kUnits) return y;
    draw::Canvas& c = h.canvas();
    UnitLooks& looks = h.looks();
    const UnitLooks::Unit& v = looks.unit(u);
    const int set = h.buttons().unitSet(u);
    y = kitpaint::drawSectionHeader(c, l.x0, l.colRight, y, l.rowH, L"Display", looks.changed(u) || set != u);
    auto label = [&](int row, const wchar_t* name, bool changed) {
        kitpaint::drawFieldLabel(c, {l.x0, row, l.x0 + l.labelW, row + l.rowH}, name, changed);
    };
    auto box = [&](int row) { return draw::Rect{l.x0 + l.labelW, row + scale(2), l.x0 + l.labelW + scale(320), row + l.rowH - scale(2)}; };
    auto changeLooks = [&h, u](const std::function<void(UnitLooks::Unit&)>& f) {
        UnitLooks::Unit n = h.looks().unit(u);
        f(n);
        h.looks().set(u, n);
        h.edited();
    };
    // The button set (its own: the unit's number).
    label(y, L"Button set", set != u);
    h.pickBox(box(y), h.buttonSetName(set), set != u,
              [&h, u, set](draw::Rect b) {
                  std::string picked;
                  if (!h.choose(L"Button set", h.entryItems("buttons"), std::to_string(set), b, picked)) return;
                  h.buttons().setUnitSet(u, std::stoi(picked));
                  h.edited();
              },
              L"The command card that the unit shows. The default value is the set with the unit's number.", set < 228 ? "units" : nullptr, set, set,
              "buttons");
    y += l.rowH;
    // The status panel: one of the kinds (two functions of the game's code).
    const uint32_t st = looks.status(u), di = looks.display(u);
    const UnitPanelKind* kind = UnitLooks::kindOf(st, di);
    wchar_t custom[64];
    std::swprintf(custom, 64, L"Other (0x%06X, 0x%06X)", unsigned(st), unsigned(di));
    const bool panelChanged = v.status || v.display;
    label(y, L"Status panel", panelChanged);
    h.pickBox(box(y), kind ? widen(kind->name) : custom, panelChanged,
              [&h, u, changeLooks](draw::Rect b) {
                  std::vector<ListPickItem> items;
                  const auto kinds = UnitLooks::kinds();
                  for (size_t k = 0; k < kinds.size(); ++k) items.push_back({widen(kinds[k].name), L"", std::to_string(k)});
                  std::string picked;
                  if (!h.choose(L"Status panel", items, "", b, picked)) return;
                  const UnitPanelKind& k = kinds[size_t(std::stoi(picked))];
                  changeLooks([&](UnitLooks::Unit& n) {  // the game's own pair: no change
                      const bool own = k.status == h.looks().stockStatus(u) && k.display == h.looks().stockDisplay(u);
                      n.status = own ? 0 : k.status, n.display = own ? 0 : k.display;
                  });
              },
              L"What the panel at the bottom shows when the unit is selected, for example a building's, a unit's or a transport's (its cargo)", nullptr,
              -1, -1, nullptr);
    y += l.rowH;
    // Wireframes: the unit whose picture shows (its own by default); group and transport have fewer.
    struct Wire {
        const wchar_t* name;
        int UnitLooks::Unit::*field;
        int count;
        const wchar_t* tip;
    };
    static const Wire wires[] = {
        {L"Wireframe", &UnitLooks::Unit::wireframe, UnitLooks::kWireframes, L"The picture in the selection panel when the unit is selected alone"},
        {L"Group wireframe", &UnitLooks::Unit::groupWireframe, UnitLooks::kGroupWireframes, L"The small picture when units are selected together"},
        {L"Transport wireframe", &UnitLooks::Unit::transportWireframe, UnitLooks::kTransportWireframes, L"The picture inside a transport"},
    };
    const int top = y;
    if (wireTop) *wireTop = top;
    int shownFrame[3] = {-1, -1, -1};
    for (const Wire& w : wires) {
        const int now = v.*(w.field);
        const bool changed = now >= 0;
        label(y, w.name, changed);
        if (u >= w.count) {  // the game has no such picture for this unit
            draw::Rect t = box(y);
            t.left += scale(8);
            c.text(t, L"\x2014", kDim, draw::kMiddle);
            y += l.rowH;
            continue;
        }
        const int shown = changed ? now : u;
        shownFrame[&w - wires] = shown;
        h.pickBox(box(y), h.unitName(shown), changed,
                  [&h, u, w, changeLooks](draw::Rect b) {
                      std::vector<ListPickItem> items = h.entryItems("units");
                      items.resize(std::min(items.size(), size_t(w.count)));
                      std::string picked;
                      if (!h.choose(w.name, items, "", b, picked)) return;
                      const int p = std::stoi(picked);
                      changeLooks([&](UnitLooks::Unit& n) { n.*(w.field) = p == u ? -1 : p; });
                  },
                  w.tip, "units", shown, shown, nullptr);
        y += l.rowH;
    }
    y = paintWireframePreviews(h, l, u, shownFrame, box(top).right + scale(16), top, y);
    return y + scale(14);
}
