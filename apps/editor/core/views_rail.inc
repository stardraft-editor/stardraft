// The side rail (portable, core.inc): moved from main.cpp. The workspace items (icon over label, top down) for the active
// tab, and the map pages' items; a click runs the item's command. The instance (gRail) is made in main.cpp, where the
// window is. The rail's hit list is the state the menus and the keys read (railItemId).

// Defined by the program (main_sdl.cpp and its sdl_*.inc files) until they move into the core.
void command(int id);
bool mapPageChangesFor(int id);  // map_pages.inc: the page of rail item `id` has changes that are not applied
void setWorkspace(Workspace w);
stardraft::EudProject* activeProject();

// The mark of a changed entry: a small green dot at (cx, cy).
void changedDotOn(stardraft::draw::Canvas& c, int cx, int cy) {
    const int r = scale(3);
    c.ellipse({cx - r, cy - r, cx + r + 1, cy + r + 1}, kGood, stardraft::draw::kNone);
}

// The rail is for projects (workspaces, Build); a tab that is just a map has none.
int wsRailWidth() { return scale(64); }
void setWorkspace(Workspace w);

struct WsHit {
    RECT r;
    int id;
    std::wstring name;  // for Report feedback
};
std::vector<WsHit> gWsHits;
int railItemId(size_t n) { return n < gWsHits.size() ? gWsHits[n].id : 0; }
int gWsHot = -1;

// The rail: the active tab's workspaces as icon + label, top down; Build (a project) or Make project (a
// map) at the bottom. A map tab shows only Map.

bool mapPageChangesFor(int id);  // map_pages.inc: the page of rail item `id` has changes that are not applied
int mapPageCommandOf(Workspace w) {
    return w == Workspace::Properties ? ID_SCEN_PROPS : w == Workspace::Players ? ID_SCEN_PLAYERS : w == Workspace::ClassicTriggers ? ID_SCEN_CLASSICTRIG
         : w == Workspace::ClassicSettings                                                     ? ID_SCEN_CLASSIC
                                                                                                : ID_WS_MAP + int(w);  // ID_WS_* follow the Workspace order
}

// The side bar's items for the active tab, in their order (Ctrl+1 to Ctrl+9 are their places; View > Workspaces lists
// them the same way). One order for a classic map and an EUD project, so each item has the same place: Map, Data,
// Triggers (a classic map: the classic triggers), then in a project whose map has them the classic triggers, Briefing,
// Assets, Map properties, and a project's Project; last Help, in every tab and with no tab open (then it is the only
// item: the help window). An item that the tab's mode does not have is left out. Assets is the Files workspace
// (Workspace::Files). The map pages (Properties, Players, Map data, classic triggers) are on while their page shows.
struct RailItem {
    int id;
    UiIcon icon;
    const wchar_t* name;
    const wchar_t* tip;
    bool page;  // a map page (else a workspace)
    Workspace ws = Workspace::Map;
};
std::vector<RailItem> railItems() {
    const bool trigWs = commandInMode(ID_WS_TRIGGERS);
    const RailItem items[] = {
        {ID_WS_MAP, UiIcon::Map, L"Map", L"the Map workspace", false, Workspace::Map},
        {ID_SCEN_CLASSIC, UiIcon::Data, L"Data", L"Data: the map's units, upgrades and technologies", true},
        {ID_WS_DATA, UiIcon::Data, L"Data", L"the Data workspace", false, Workspace::Data},
        {trigWs ? 0 : ID_SCEN_CLASSICTRIG, UiIcon::Triggers, L"Triggers", L"Classic triggers", true},
        {ID_WS_TRIGGERS, UiIcon::Triggers, L"Triggers", L"the Triggers workspace", false, Workspace::Triggers},
        {trigWs ? ID_SCEN_CLASSICTRIG : 0, UiIcon::Log, L"Classic", L"Classic triggers (the map's own triggers)", true},
        {ID_WS_BRIEFING, UiIcon::Briefing, L"Briefing", L"the Briefing workspace", false, Workspace::Briefing},
        {ID_WS_FILES, UiIcon::Assets, L"Assets", L"the Assets workspace", false, Workspace::Files},
        {ID_SCEN_PROPS, UiIcon::Tag, L"Properties", L"Map properties: the title, size and tileset, players and forces", true},
        {ID_WS_PROJECT, UiIcon::Project, L"Project", L"the Project page", false, Workspace::Project},  // after Properties: the classic map's order, then the project's own
    };
    std::vector<RailItem> out;
    if (!gDocs.empty())
        for (const RailItem& it : items)
            if (it.id && commandInMode(it.id)) out.push_back(it);
    out.push_back({ID_WS_HELP, UiIcon::Help, L"Help", L"Help: the documentation (also F1)", false, Workspace::Help});
    return out;
}
// View > Workspaces: the side bar's items with their keys (core/menu_model.inc).
std::vector<std::pair<int, std::wstring>> railMenuItems() {
    std::vector<std::pair<int, std::wstring>> out;
    for (const RailItem& it : railItems()) out.push_back({it.id, it.name});
    return out;
}

// The rail as a view (apps/common/view.h): its window is a view window (kViewClass) that shows gRail.
struct RailView final : stardraft::draw::View {
    void paint(stardraft::draw::Canvas& c) override {
        using stardraft::draw::Rect;
        const Rect rc{0, 0, width(), height()};
        const COLORREF acc = kAccent, accBg = kSel;
        c.fill(rc, kRail);
        c.fill({rc.right - 1, 0, rc.right, rc.bottom}, kBorder);
        gWsHits.clear();
        // One item: icon over label.
        const stardraft::draw::Font label = c.font({stardraft::draw::Face::Ui, -railLabelHeight()});
        auto item = [&](RECT r, UiIcon icon, const wchar_t* name, COLORREF color, COLORREF labelColor) {
            c.icon({r.left, r.top + scale(6), r.right, r.top + scale(30)}, int(icon), color, scale(16));
            const stardraft::draw::Font of = c.setFont(label);
            c.text({r.left, r.top + scale(30), r.right, r.bottom - scale(4)}, name, labelColor, stardraft::draw::kCenter | stardraft::draw::kMiddle);
            c.setFont(of);
        };
        const int m = scale(6), itemH = scale(54);
        int y = scale(8);
        // Help, the last item: at the bottom of the side bar. A map page's item is on while its page shows; a dot: the page
        // has changes that are not applied. Each item's tip says its key (its place: Ctrl+1 to Ctrl+9).
        for (const RailItem& it : railItems()) {
            const bool isHelp = it.id == ID_WS_HELP;
            if (isHelp) y = std::max(y, int(rc.bottom) - scale(8) - itemH);
            RECT r{m, y, rc.right - m - 1, y + itemH};
            const bool on = it.page ? gWorkspace >= Workspace::Properties && mapPageCommandOf(gWorkspace) == it.id : gWorkspace == it.ws,
                       hot = gWsHot == it.id;
            if (on || hot) c.roundBox(r, on ? accBg : kHot, on ? accBg : kHot, scale(9) / 2.0f);
            if (on) c.fill({0, r.top + scale(12), scale(3), r.bottom - scale(12)}, acc);
            item(r, it.icon, it.name, on ? acc : kText, on ? acc : kDim);
            if (it.page && mapPageChangesFor(it.id)) changedDotOn(c, r.right - scale(10), r.top + scale(10));
            const size_t n = gWsHits.size() + 1;
            gWsHits.push_back({r, it.id, std::wstring(tr(it.tip)) + (n <= 9 ? L" (Ctrl+" + std::to_wstring(n) + L")" : L"")});
            y += itemH + scale(4);
        }
    }
    bool mouse(const stardraft::draw::MouseEvent& e) override {
        using stardraft::draw::MouseKind;
        const POINT pt{e.pt.x, e.pt.y};
        switch (e.kind) {
        case MouseKind::Move: {
            int hot = -1;
            for (const auto& h : gWsHits)
                if (PtInRect(&h.r, pt)) hot = h.id;
            if (hot != gWsHot) {
                gWsHot = hot, invalidate();
                if (hot == ID_PROJ_COMPILE) setHint(L"Build the map of the project with euddraft (Ctrl+B)");
            }
            return true;
        }
        case MouseKind::Leave: gWsHot = -1, invalidate(); return true;
        case MouseKind::Up:
            if (e.button != 0) return false;
            for (const auto& h : gWsHits)
                if (PtInRect(&h.r, pt)) {
                    command(h.id);
                    break;
                }
            return true;
        default: return false;
        }
    }
    std::wstring tip(stardraft::draw::Point p) override {  // (Report feedback names the item under the mouse)
        for (const auto& h : gWsHits)
            if (PtInRect(&h.r, POINT{p.x, p.y})) return h.name;
        return {};
    }
    // The rail's labels: the UI font at 5/6 of its height.
    static int railLabelHeight() { return shellUiFontLfHeight() * 5 / 6; }
};
