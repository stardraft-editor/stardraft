// The Data form's Players section (portable, core.inc): the map's own settings for each player (feedback #65): a unit's
// Can build, an upgrade's levels, a technology's Available and Researched; the default for all players and players 1-8,
// each on the default or its own. They are the map's (as a classic map's Data page), saved with it; each change is one
// undo step of the map. The model (the settings as read, a change) is here for both programs; the Canvas paint is the
// SDL form's (the Windows form paints the same rows with GDI, main.cpp: paintMapPlayers). The program's parts come in
// through MapPlayersHost.

// The Data workspace's Players edits since its last data edit: Ctrl+Z in Data undoes these first (the map's undo),
// Ctrl+Y redoes them. Each is the map's state (Editor::stateId) after the step: only while the map's last step is still
// that one (no Map edit came after it) does Data's Undo take it back; else the list goes. gDataMapRedo: the map's
// state after each of those undos, for its redo.
std::vector<uint64_t> gDataMapSteps, gDataMapRedo;
void dataMapStepsClear() { gDataMapSteps.clear(), gDataMapRedo.clear(); }
// Whether Data's Undo (redo: Redo) is a Players edit of the map now.
bool dataMapCanUndo(bool redo) {
    const Editor* ed = shellState().ed.get();
    if (!ed || !ed->isOpen()) return false;
    if (redo) return !gDataMapRedo.empty() && gDataMapRedo.back() == ed->stateId() && ed->canRedo();
    return !gDataMapSteps.empty() && gDataMapSteps.back() == ed->stateId() && ed->canUndo();
}
// Data's Undo (redo: Redo) of a Players edit: true when one was taken back (or given again). A list that is not the
// map's last step any more goes.
bool dataMapUndo(bool redo) {
    Editor* ed = shellState().ed.get();
    if (!dataMapCanUndo(redo)) {
        if (redo) gDataMapRedo.clear();
        else gDataMapSteps.clear();
        return false;
    }
    if (redo) {
        gDataMapRedo.pop_back();
        ed->redo();
        gDataMapSteps.push_back(ed->stateId());
    } else {
        gDataMapSteps.pop_back();
        ed->undo();
        gDataMapRedo.push_back(ed->stateId());
    }
    return true;
}

// The kind of settings entry `u` of `dat` has: 0 units, 1 upgrades, 2 technologies; -1 none.
int mapPlayersKind(const std::string& dat, int u) {
    if (u < 0) return -1;
    if (dat == "units" && u < kSettingUnits) return 0;
    if (dat == "upgrades" && u < kSettingUpgrades) return 1;
    if (dat == "techdata" && u < kSettingTechs) return 2;
    return -1;
}

// The map's settings, read once for each version of the map (reading them takes a few ms; a paint comes often). Only
// the kind asked for is read again.
struct MapPlayerSettings {
    UnitSettings units;
    UpgradeSettings upgrades;
    TechSettings techs;
};
const MapPlayerSettings& mapPlayerSettings(int kind) {
    static MapPlayerSettings s;
    static const Editor* forEd[3] = {};
    static uint64_t forRev[3] = {~0ull, ~0ull, ~0ull};
    const Editor* ed = shellState().ed.get();
    if (ed && kind >= 0 && kind < 3 && (forEd[kind] != ed || forRev[kind] != ed->revision())) {
        forEd[kind] = ed, forRev[kind] = ed->revision();
        if (kind == 0) s.units = ed->unitSettings();
        else if (kind == 1) s.upgrades = ed->upgradeSettings();
        else s.techs = ed->techSettings();
    }
    return s;
}

// A change of entry u's settings into the map (one undo step): `which` the kind; `player` -1 for the default.
// Units: value 0 or 1 (-1: the player's Default toggles). Upgrades and technologies: the field in value >> 16 (0 start
// level or Available, 1 max level or Researched), the value in value & 0xFFFF (0xFFFF: the player's Default toggles).
// The map is changed (dirty); the caller shows it (the title, the form).
void applyMapPlayerSetting(int u, int which, int player, int value) {
    if (!shellState().ed) return;
    Editor& ed = *shellState().ed;
    if (which == 0) {
        UnitSettings s = ed.unitSettings();
        auto& a = s.availability[size_t(u)];
        if (player < 0) a.global = value != 0;
        else if (value < 0) a.usesDefault[size_t(player)] = !a.usesDefault[size_t(player)], a.player[size_t(player)] = a.global;
        else a.player[size_t(player)] = value != 0;
        ed.setUnitSettings(s, {});
    } else if (which == 1) {
        UpgradeSettings s = ed.upgradeSettings();
        auto& l = s.levels[size_t(u)];
        const int field = value >> 16, v = value & 0xFFFF;
        if (v == 0xFFFF) {
            const size_t pp = size_t(player);
            l.usesDefault[pp] = !l.usesDefault[pp], l.max[pp] = l.defaultMax, l.start[pp] = l.defaultStart;
        } else if (player < 0) (field ? l.defaultMax : l.defaultStart) = uint8_t(std::min(v, 255));
        else (field ? l.max[size_t(player)] : l.start[size_t(player)]) = uint8_t(std::min(v, 255));
        ed.setUpgradeSettings(s);
    } else {
        TechSettings s = ed.techSettings();
        auto& t = s.players[size_t(u)];
        const int field = value >> 16, v = value & 0xFFFF;
        if (v == 0xFFFF) {
            const size_t pp = size_t(player);
            t.usesDefault[pp] = !t.usesDefault[pp], t.available[pp] = t.defaultAvailable, t.researched[pp] = t.defaultResearched;
        } else if (player < 0) (field ? t.defaultResearched : t.defaultAvailable) = v != 0;
        else (field ? t.researched[size_t(player)] : t.available[size_t(player)]) = v != 0;
        ed.setTechSettings(s);
    }
    gDataMapSteps.push_back(ed.stateId()), gDataMapRedo.clear();
    shellState().dirty = true;
}

// The section's texts: its heading, the two columns' heads (the second can be none), the tips.
const wchar_t* const kMapPlayersTitle = L"Players (the map's settings)";
inline const wchar_t* mapPlayersHead(int kind, int col) {
    if (col == 0) return kind == 0 ? L"Can build" : kind == 1 ? L"Start level" : L"Available";
    return kind == 1 ? L"Max level" : kind == 2 ? L"Researched" : nullptr;
}

// The program's parts of the Canvas paint.
class MapPlayersHost {
public:
    virtual ~MapPlayersHost() = default;
    virtual bool hot(draw::Rect r) = 0;
    // A click area (click null: a tip only) with its tip; `text`: the I-beam cursor (a number box), else the hand.
    virtual void area(draw::Rect r, std::function<void()> click, const std::wstring& tip, bool text) = 0;
    // A level typed in a box under `box`: true with the new text in `text`.
    virtual bool editNumber(const std::wstring& title, std::string& text, draw::Rect box) = 0;
    virtual void changed() = 0;  // a setting changed: the title and the form show it
};

// The section on a Canvas, from y down (the Windows form's rows and sizes). Returns the y under it.
int paintMapPlayers(draw::Canvas& c, MapPlayersHost& h, const std::string& dat, int u, int x0, int y, int colW, int labelW, int rowH) {
    if (!shellState().ed || !shellState().ed->isOpen()) return y;
    const int kind = mapPlayersKind(dat, u);
    if (kind < 0) return y;
    y = kitpaint::drawSectionHeader(c, x0, x0 + colW, y, rowH, kMapPlayersTitle);
    const int colA = x0 + labelW + scale(90), colB = colA + scale(130), cb = scale(14);
    for (int k = 0; k < 2; ++k)
        if (const wchar_t* head = mapPlayersHead(kind, k)) c.text({k ? colB : colA, y, (k ? colB : colA) + scale(126), y + rowH}, head, kDim, draw::kMiddle);
    y += rowH;
    auto apply = [&h, u, kind](int player, int value) {
        applyMapPlayerSetting(u, kind, player, value);
        h.changed();
    };
    const MapPlayerSettings& s = mapPlayerSettings(kind);
    auto check = [&](int x, int rowY, bool on, bool enabled, std::function<void()> click, const std::wstring& tip) {
        const draw::Rect cell{x, rowY, x + scale(110), rowY + rowH};
        const draw::Rect b{x, rowY + (rowH - cb) / 2, x + cb, rowY + (rowH - cb) / 2 + cb};
        kitpaint::drawCheckMark(c, b, on, enabled && h.hot(cell), enabled);
        c.text({b.right + scale(8), rowY, cell.right, rowY + rowH}, on ? L"Yes" : L"No", enabled ? kText : kFaint, draw::kMiddle);
        if (enabled) h.area(cell, std::move(click), tip, false);
    };
    auto number = [&](int x, int rowY, int v, bool enabled, std::function<void(int)> set, const std::wstring& title) {
        const draw::Rect vb{x, rowY + scale(2), x + scale(90), rowY + rowH - scale(2)};
        kitpaint::drawFieldBox(c, vb, !enabled ? kitpaint::Field::ReadOnly : h.hot(vb) ? kitpaint::Field::Hot : kitpaint::Field::Normal);
        c.text({vb.left + scale(7), vb.top, vb.right - scale(7), vb.bottom}, std::to_wstring(v), enabled ? kText : kFaint, draw::kMiddle | draw::kRight);
        if (enabled)
            h.area(vb, [&h, vb, v, set, title] {
                std::string text = std::to_string(v);
                if (h.editNumber(title, text, vb)) set(std::clamp(std::atoi(text.c_str()), 0, 255));
            }, L"A level from 0 to 255", true);
    };
    for (int row = -1; row < 8; ++row) {  // -1: the default for all players
        c.text({x0, y, x0 + labelW, y + rowH}, row < 0 ? L"Default" : L"Player " + std::to_wstring(row + 1), kDim, draw::kMiddle);
        bool usesDefault = false;
        if (row >= 0) {
            usesDefault = kind == 0 ? s.units.availability[size_t(u)].usesDefault[size_t(row)]
                        : kind == 1 ? s.upgrades.levels[size_t(u)].usesDefault[size_t(row)]
                                    : s.techs.players[size_t(u)].usesDefault[size_t(row)];
            const draw::Rect db{x0 + labelW, y + (rowH - cb) / 2, x0 + labelW + cb, y + (rowH - cb) / 2 + cb};
            const draw::Rect dcell{x0 + labelW, y, colA - scale(8), y + rowH};
            kitpaint::drawCheckMark(c, db, usesDefault, h.hot(dcell), true);
            c.text({db.right + scale(6), y, colA - scale(8), y + rowH}, L"Default", kDim, draw::kMiddle);
            h.area(dcell, [apply, kind, row] { apply(row, kind == 0 ? -1 : 0xFFFF); }, L"On: this player has the default's values", false);
        }
        const bool own = row < 0 || !usesDefault;
        if (kind == 0) {
            const auto& a = s.units.availability[size_t(u)];
            const bool on = row < 0 || usesDefault ? a.global : a.player[size_t(row)];
            check(colA, y, on, own, [apply, row, on] { apply(row, on ? 0 : 1); }, L"Can the player build this unit");
        } else if (kind == 1) {
            const auto& l = s.upgrades.levels[size_t(u)];
            const int start = row < 0 || usesDefault ? l.defaultStart : l.start[size_t(row)], max = row < 0 || usesDefault ? l.defaultMax : l.max[size_t(row)];
            number(colA, y, start, own, [apply, row](int v) { apply(row, v); }, L"Start level");
            number(colB, y, max, own, [apply, row](int v) { apply(row, 1 << 16 | v); }, L"Max level");
        } else {
            const auto& t = s.techs.players[size_t(u)];
            const bool av = row < 0 || usesDefault ? t.defaultAvailable : t.available[size_t(row)];
            const bool re = row < 0 || usesDefault ? t.defaultResearched : t.researched[size_t(row)];
            check(colA, y, av, own, [apply, row, av] { apply(row, av ? 0 : 1); }, L"Is the technology available to the player");
            check(colB, y, re, own, [apply, row, re] { apply(row, 1 << 16 | (re ? 0 : 1)); }, L"Does the player start with it researched");
        }
        y += rowH;
    }
    return y + scale(8);
}
