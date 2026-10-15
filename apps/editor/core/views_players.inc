// The Players and forces page's view (portable, core.inc): the eight players (controller, race, force and color: kit
// dropdowns) and the four forces (the name: a kit text field; four flags: kit check boxes), and under them what StarCraft
// needs to start the map (the problems, or that it can start). The values are the page's working copy (PlayersValues);
// the program puts a change into the map (PlayersHost), and the page shows what the copy holds. It is the second part of
// Map properties' page, under its fields (map_pages.inc), or the whole page of Players and forces.

// The controllers, races and force flags as the page names them.
const std::pair<Editor::Controller, const wchar_t*> kControllers[] = {
    {Editor::Controller::Human, L"Human"},       {Editor::Controller::Computer, L"Computer"}, {Editor::Controller::Rescue, L"Rescuable"},
    {Editor::Controller::Neutral, L"Neutral"},   {Editor::Controller::Closed, L"Closed"},     {Editor::Controller::Inactive, L"Unused"}};
const std::pair<Editor::Race, const wchar_t*> kRaces[] = {{Editor::Race::Zerg, L"Zerg"},
                                                         {Editor::Race::Terran, L"Terran"},
                                                         {Editor::Race::Protoss, L"Protoss"},
                                                         {Editor::Race::UserSelect, L"User select"},
                                                         {Editor::Race::Random, L"Random"}};
const wchar_t* kForceFlagNames[4] = {L"Random starts", L"Allies", L"Allied victory", L"Shared vision"};

// The rows of a player's color list: the game's 22 colors, then Random, Player's choice and Custom (an RGB of your choice).
int playerColorRow(const Editor::PlayerColor& c) {
    switch (c.kind) {
    case Editor::PlayerColor::Random: return Editor::kColorIds;
    case Editor::PlayerColor::Choice: return Editor::kColorIds + 1;
    case Editor::PlayerColor::Custom: return Editor::kColorIds + 2;
    default: return std::min<int>(c.id, Editor::kColorIds - 1);
    }
}
std::wstring playerColorName(const Editor::PlayerColor& c) {
    const int row = playerColorRow(c);
    if (row < Editor::kColorIds) return widen(Editor::colorIdName(row));
    if (row == Editor::kColorIds) return L"Random";
    if (row == Editor::kColorIds + 1) return L"Player's choice";
    wchar_t hex[16];
    std::swprintf(hex, 16, L"#%06X", unsigned(c.rgb));
    return L"Custom " + std::wstring(hex) + L" (multiplayer only)";
}

// The page's values: the working copy (the program's copy is a PlayersDialog, which holds these).
struct PlayersValues {
    std::vector<Editor::Player> players;  // 8
    std::vector<Editor::Force> forces;    // 4
    std::wstring problemsText;            // the problems (or that StarCraft can start the map), one per line
    // The values as the page first showed the map's (a change is a difference from these).
    bool baseSet = false;
    std::vector<Editor::Player> basePlayers;
    std::vector<Editor::Force> baseForces;
};

// The program's part of the page (map_pages.inc: PlayersProgram): the map's changes and the color picker.
class PlayersHost {
public:
    virtual ~PlayersHost() = default;
    // A value changed by a dropdown or a check box: the program updates the problems and puts the change into the map.
    virtual void changed() = 0;
    // A force name typed: the program puts it into the map when the typing stops.
    virtual void typed() = 0;
    // The Custom color: the color picker for player p, which sets c (true) or not (false: c stays).
    virtual bool pickCustomColor(int p, Editor::PlayerColor& c) = 0;
    // The width of t in the page's font (the page's columns are measured with it).
    virtual int textWidth(const std::wstring& t) = 0;
    // The page's content is w x h (the program sizes the window it is in).
    virtual void fitSize(int w, int h) = 0;
};

class PlayersView : public draw::View {
public:
    PlayersView(PlayersHost& host, PlayersValues& v) : host_(host), v_(v) {
        std::vector<stardraft::kit::DropdownItem> ctrls, races;
        for (const auto& [e, n] : kControllers) ctrls.push_back({n, -1, -1});
        for (const auto& [e, n] : kRaces) races.push_back({n, -1, -1});
        for (int p = 0; p < 8; ++p) {
            controller_[p].setItems(ctrls), race_[p].setItems(races);
            controller_[p].onChange = [this, p](int i) { v_.players[size_t(p)].controller = kControllers[i].first, edited(); };
            race_[p].onChange = [this, p](int i) { v_.players[size_t(p)].race = kRaces[i].first, edited(); };
            force_[p].onChange = [this, p](int i) { v_.players[size_t(p)].force = i, edited(); };
            color_[p].onChange = [this, p](int row) { colorPicked(p, row); };
            // Custom again (it is the color already): its picker opens again, as the old list's Custom row did.
            color_[p].onSame = [this, p](int row) {
                if (row == Editor::kColorIds + 2) colorPicked(p, row);
            };
            for (stardraft::kit::Dropdown* d : {&controller_[p], &race_[p], &force_[p], &color_[p]}) add(*d, {0, 0, 0, 0});
        }
        for (int f = 0; f < 4; ++f) {
            name_[f].onChange = [this, f] {
                v_.forces[size_t(f)].name = narrow(name_[f].text());
                host_.typed();
                invalidate();
            };
            add(name_[f], {0, 0, 0, 0});
            for (int b = 0; b < 4; ++b) {
                flag_[f][b].setLabel(kForceFlagNames[b]);
                flag_[f][b].onChange = [this, f, b](bool on) {
                    uint8_t& fl = v_.forces[size_t(f)].flags;
                    fl = uint8_t(on ? fl | (1 << b) : fl & ~(1 << b));
                    edited();
                };
                add(flag_[f][b], {0, 0, 0, 0});
            }
        }
        syncControls();
    }

    // The Tab order of the force names (the page's Tab goes from the description to Force 1's name, as the old EDITs did).
    // The names are the page's Tab stops: true when the Tab moved to another name (false: it leaves the view).
    bool tabName(bool back) {
        int cur = -1;
        for (int f = 0; f < 4; ++f)
            if (name_[f].hasFocus()) cur = f;
        const int next = cur + (back ? -1 : 1);
        if (cur < 0 || next < 0 || next > 3) return false;
        name_[next].focus();
        return true;
    }
    // A force name has the keyboard (its typing, as the program's own typing when the text goes into the map).
    bool typingName() const {
        for (const auto& n : name_)
            if (n.hasFocus()) return true;
        return false;
    }
    void focusEntered(bool back) override { name_[back ? 3 : 0].focus(); }

    // The size of the page (its content: the window is as big as it, made before its first paint).
    int preferredWidth() { return rows().width; }
    int preferredHeight() { return rows().height; }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        syncControls();  // the dropdowns and fields show the copy's values
        const Rows l = rows();
        if (width() != l.width || height() != l.height) host_.fitSize(l.width, l.height);  // (its own size: a paint again)
        const int gap = scale(8);
        auto text = [&](const std::wstring& t, int x, int y, int w, draw::Color col) { c.text({x, y, x + w, y + l.rowH}, t, col, draw::kMiddle | draw::kEllipsis); };
        text(L"Players", l.c0, l.pad, l.right - l.c0, kText);  // a section heading, as the map's fields have
        c.fill({l.c0, l.pad + l.rowH, l.right, l.pad + l.rowH + 1}, kBorder);
        int y = l.tableTop;
        text(L"Player", l.c0, y, l.c1 - l.c0, kDim), text(L"Controller", l.c1, y, l.c2 - l.c1, kDim), text(L"Race", l.c2, y, l.c3 - l.c2, kDim);
        text(L"Force", l.c3, y, l.c4 - l.c3, kDim), text(L"Color", l.c4, y, l.c5 - l.c4, kDim);
        y += l.pitch;
        for (int p = 0; p < 8; ++p) {
            text(L"Player " + std::to_wstring(p + 1), l.c0, y, l.c1 - l.c0, kText);
            place(controller_[p], {l.c1, y, l.c2 - gap, y + l.rowH});
            place(race_[p], {l.c2, y, l.c3 - gap, y + l.rowH});
            place(force_[p], {l.c3, y, l.c4 - gap, y + l.rowH});
            place(color_[p], {l.c4, y, l.c5 - gap, y + l.rowH});
            y += l.pitch;
        }
        // Forces: the name (a field), then the four flags.
        y = l.forcesTop;
        text(L"Forces", l.c0, y, l.right - l.c0, kText);
        c.fill({l.c0, y + l.rowH, l.right, y + l.rowH + 1}, kBorder);
        y += l.pitch;
        for (int f = 0; f < 4; ++f) {
            place(name_[f], {l.c0, y, l.c1 + scale(110), y + l.rowH});
            int fx = l.flagX;
            for (int b = 0; b < 4; ++b) {
                place(flag_[f][b], {fx, y, fx + l.flagW[b] - gap, y + l.rowH});
                fx += l.flagW[b];
            }
            y += l.pitch;
        }
        // The problems, or that StarCraft can start the map: each line with its icon.
        y = l.problemsTop;
        const std::wstring& pt = v_.problemsText;
        for (size_t a = 0; a < pt.size();) {
            size_t e = pt.find(L"\r\n", a);
            if (e == std::wstring::npos) e = pt.size();
            std::wstring line = pt.substr(a, e - a);
            a = e + 2;
            if (line.empty()) continue;
            const bool good = line[0] == L'\x2713';
            line = line.substr(std::min(line.size(), size_t(3)));
            const draw::Color col = good ? kGood : kWarn;
            c.icon({l.c0, y, l.c0 + scale(16), y + scale(22)}, int(good ? UiIcon::Check : UiIcon::Warning), col, scale(16));
            c.text({l.c0 + scale(24), y, l.right, y + scale(22)}, line, col, draw::kMiddle | draw::kEllipsis);
            y += scale(22);
        }
    }

private:
    // The page's rows (the same at each paint).
    struct Rows {
        int pad, rowH, pitch, c0, c1, c2, c3, c4, c5, flagX, right, tableTop, forcesTop, problemsTop, width, height;
        int flagW[4];
    };
    Rows rows() {
        Rows l{};
        l.pad = scale(stardraft::kit::kDialogPad), l.rowH = scale(stardraft::kit::kFieldH), l.pitch = l.rowH + scale(8);
        l.c0 = l.pad, l.c1 = l.pad + scale(80), l.c2 = l.c1 + scale(150), l.c3 = l.c2 + scale(130), l.c4 = l.c3 + scale(170), l.c5 = l.c4 + scale(230);
        l.flagX = l.c1 + scale(120);
        int end = l.flagX;
        for (int b = 0; b < 4; ++b) l.flagW[b] = host_.textWidth(kForceFlagNames[b]) + scale(40), end += l.flagW[b];
        l.right = std::max(l.c5, end);
        l.tableTop = l.pad + l.pitch;                    // under the "Players" heading
        l.forcesTop = l.tableTop + l.pitch * 9 + l.pad;  // the column names and 8 players
        l.problemsTop = l.forcesTop + l.pitch + 4 * l.pitch + l.pad;
        l.height = l.problemsTop + scale(70) + l.pad;
        l.width = l.right + l.pad;
        return l;
    }
    // A child's frame, set only when it differs (a set repaints).
    void place(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
    }
    // A value changed by the page: the copy's change into the map (the program), then the page again.
    void edited() {
        syncControls();
        host_.changed();
        invalidate();
    }
    // A color chosen in a player's list: the copy's color (Custom: the program's picker, which may keep the old one).
    void colorPicked(int p, int row) {
        Editor::PlayerColor& c = v_.players[size_t(p)].color;
        if (row < Editor::kColorIds) c.kind = Editor::PlayerColor::Id, c.id = uint8_t(row);
        else if (row == Editor::kColorIds) c.kind = Editor::PlayerColor::Random;
        else if (row == Editor::kColorIds + 1) c.kind = Editor::PlayerColor::Choice;
        else if (!host_.pickCustomColor(p, c)) {
            invalidate();  // the list's choice is not the copy's: the field shows the copy's color again
            return;
        }
        edited();
    }
    // The dropdowns and fields show the copy's values (the program may have changed them too).
    void syncControls() {
        std::vector<stardraft::kit::DropdownItem> forces;
        for (int f = 0; f < 4; ++f) {
            const std::string& n = v_.forces[size_t(f)].name;
            forces.push_back({n.empty() ? L"Force " + std::to_wstring(f + 1) : widen(n), -1, -1});
        }
        for (int p = 0; p < 8; ++p) {
            const Editor::Player& pl = v_.players[size_t(p)];
            controller_[p].setValue(indexOf(kControllers, pl.controller));
            race_[p].setValue(indexOf(kRaces, pl.race));
            refill(force_[p], forceItems_[p], forces);
            force_[p].setValue(std::clamp(int(pl.force), 0, 3));
            refill(color_[p], colorItems_[p], colorItems(pl.color));
            color_[p].setValue(playerColorRow(pl.color));
        }
        for (int f = 0; f < 4; ++f) {
            const std::wstring want = widen(v_.forces[size_t(f)].name);
            if (name_[f].text() != want) name_[f].setText(want);
            for (int b = 0; b < 4; ++b) flag_[f][b].setOn((v_.forces[size_t(f)].flags >> b) & 1);
        }
    }
    // The index of v in a table of (value, name) pairs (-1: none).
    template <class T, size_t N>
    static int indexOf(const std::pair<T, const wchar_t*> (&table)[N], T v) {
        for (size_t i = 0; i < N; ++i)
            if (table[i].first == v) return int(i);
        return -1;
    }
    // The items of a dropdown are made again only when they differ (setItems closes a list that is open).
    static void refill(stardraft::kit::Dropdown& d, std::vector<stardraft::kit::DropdownItem>& built, const std::vector<stardraft::kit::DropdownItem>& items) {
        bool same = built.size() == items.size();
        for (size_t i = 0; same && i < items.size(); ++i) same = built[i].label == items[i].label && built[i].swatch == items[i].swatch;
        if (same) return;
        built = items;
        d.setItems(items);
    }
    // A player's color list: the 22 colors with their squares, Random, Player's choice, and Custom (its color when it is one).
    static std::vector<stardraft::kit::DropdownItem> colorItems(const Editor::PlayerColor& c) {
        auto bgr = [](uint32_t rgb) { return draw::rgb(int(rgb >> 16), int(rgb >> 8), int(rgb)); };
        std::vector<stardraft::kit::DropdownItem> items;
        for (int i = 0; i < Editor::kColorIds; ++i) items.push_back({widen(Editor::colorIdName(i)), -1, -1, bgr(Editor::colorIdRgb(i))});
        items.push_back({L"Random", -1, -1});
        items.push_back({L"Player's choice", -1, -1});
        const bool custom = c.kind == Editor::PlayerColor::Custom;
        items.push_back({custom ? playerColorName(c) : std::wstring(L"Custom color\x2026 (multiplayer only)"), -1, -1, custom ? bgr(c.rgb) : draw::kNone});
        return items;
    }

    PlayersHost& host_;
    PlayersValues& v_;
    stardraft::kit::Dropdown controller_[8], race_[8], force_[8], color_[8];
    stardraft::kit::TextField name_[4];
    stardraft::kit::CheckBox flag_[4][4];
    std::vector<stardraft::kit::DropdownItem> forceItems_[8], colorItems_[8];  // what each dropdown shows
};
