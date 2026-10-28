// The palette's items (portable, core.inc): the units, terrains, doodads and sprites the palette lists, each with its
// section, label and the text the search looks at (moved from main.cpp). The Win32 palette and the SDL palette both
// list these. paletteItems(app) reads app.mode, so a second palette swaps the mode while it lists its own.

const wchar_t* kTilesetNames[8] = {L"Badlands", L"Space Platform", L"Installation", L"Ash World", L"Jungle", L"Desert", L"Ice", L"Twilight"};

// Section of a unit, as StarEdit groups them: race (units.dat group flags), then the palette group
// from the unit's stat_txt.tbl entry (Ground Units, Air Units, Buildings, Heroes, Resources, ...).
std::wstring unitSection(const UnitType& t, int& race) {
    race = (t.groupFlags & UnitGroup::Terran) ? 0 : (t.groupFlags & UnitGroup::Zerg) ? 1 : (t.groupFlags & UnitGroup::Protoss) ? 2
         : (t.groupFlags & UnitGroup::Independent) ? 4 : 3;
    static const wchar_t* races[] = {L"Terran", L"Zerg", L"Protoss", L"Neutral", L"Independent"};
    return std::wstring(races[race]) + L" " + (t.editorGroup.empty() ? L"Other" : widen(t.editorGroup));
}

std::wstring unitPaletteLabel(const UnitType& t) {
    return unitShortName(t) + (t.subtitle.empty() ? L"" : L" (" + widen(t.subtitle) + L")");
}

// How well `needle` matches `hay` (both lower case), or -1: a substring scores highest (earlier is
// better, a word start best), otherwise the needle's letters in order ("sgtk" finds "siege tank"),
// scored by how tightly they sit.
int fuzzyScore(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return 0;
    if (size_t p = hay.find(needle); p != std::wstring::npos)
        return 2000 - int(p) + ((p == 0 || hay[p - 1] == L' ' || hay[p - 1] == L'(') ? 500 : 0);
    size_t from = 0, last = std::wstring::npos;
    int gaps = 0;
    for (wchar_t c : needle) {
        if (c == L' ') continue;
        size_t f = hay.find(c, from);
        if (f == std::wstring::npos) return -1;
        if (last != std::wstring::npos) gaps += int(f - last - 1);
        last = f, from = f + 1;
    }
    return std::max(1, 1000 - gaps * 10);
}

// The palette's units: each with its section (and order), label and the text searches look at.
struct PaletteUnit {
    int value = 0, order = 0;
    int tier = 0;  // (a mod's unit: its tech tier, 0 none)
    std::wstring section, label, hay;
};

std::vector<PaletteUnit> paletteUnits(AppState& app) {
    std::vector<PaletteUnit> out;
    if (!app.ed || !app.ed->isOpen()) return out;
    const UnitTypes& types = app.ed->unitTypes();
    std::map<std::wstring, int> firstId;  // sections within a race in the order units.dat first uses them
    for (uint16_t i = 0; i < types.size(); ++i) {
        const UnitType& t = types[i];
        if (t.name.empty()) continue;  // a slot with no unit (a mod's free entries: Cosmonarchy BW has 3000)
        // Every unit, as SCMDraft lists them: also those StarEdit hides (start location, eggs, doors and
        // traps, turrets, markers, the unused ones), which have no command icon of their own.
        PaletteUnit u;
        u.value = i, u.label = withCustomName(unitPaletteLabel(t), int(i));
        int race = 0;
        if (!t.paletteSection.empty()) {  // a mod's place (src/mods)
            u.section = widen(t.paletteSection), u.order = t.paletteOrder, u.tier = t.paletteTier;
        } else {
            u.section = unitSection(t, race);
            firstId.emplace(u.section, i);
            u.order = race * 1000 + firstId[u.section];
        }
        // Label first, so a match at the start of the name scores best.
        u.hay = u.label + L" " + widen(t.name) + L" " + u.section;
        std::transform(u.hay.begin(), u.hay.end(), u.hay.begin(), ::towlower);
        out.push_back(std::move(u));
    }
    std::stable_sort(out.begin(), out.end(), [](const PaletteUnit& a, const PaletteUnit& b) {
        return a.order != b.order ? a.order < b.order : a.label < b.label;
    });
    return out;
}

// The side bar's items for the current mode: units, the tileset's terrains, or its doodads (by their
// editor group, as StarEdit lists them).
// The Sprites palette's section for sprites.dat entry `i` (named `name`), and its place in the list: the map's
// tileset's doodads first, then Terran, Zerg, Protoss, Neutral, traps and doors, effects, and the doodads of the
// other tilesets last (their colors are wrong in this tileset).
std::pair<int, std::wstring> spriteSection(int i, const std::wstring& name, uint16_t era) {
    // Doodad sprites: their names end in their tileset ("4/56 Jungle", "1/6 Badlands3"); a few are "Unknown<id>".
    static const std::pair<const wchar_t*, int> kWords[] = {{L"Badlands", 0}, {L"Platform", 1}, {L"Installation", 2}, {L"Ash", 3},
                                                            {L"Jungle", 4},    {L"Desert", 5},   {L"Ice", 6},           {L"Twilight", 7}};
    const bool doodad = (i >= 0 && i <= 129) || (i >= 386 && i <= 480);
    if (doodad) {
        int tileset = i >= 402 && i <= 445 ? 6 : -1;
        std::wistringstream words(name);
        for (std::wstring w; words >> w;) {
            while (!w.empty() && iswdigit(w.back())) w.pop_back();
            for (auto [word, t] : kWords)
                if (w == word) tileset = t;
        }
        if (tileset < 0) tileset = era & 7;
        const std::wstring section = std::wstring(kTilesetNames[tileset & 7]) + L" doodads";
        return {tileset == (era & 7) ? 0 : 100 + tileset, section};
    }
    if ((i >= 225 && i <= 274) || (i >= 489 && i <= 491) || i == 496) return {1, L"Terran"};
    if ((i >= 130 && i <= 187) || (i >= 481 && i <= 485) || i == 495) return {2, L"Zerg"};
    if ((i >= 188 && i <= 224) || (i >= 486 && i <= 488) || i == 499) return {3, L"Protoss"};
    if ((i >= 275 && i <= 302) || (i >= 492 && i <= 494) || i == 497 || i == 498 || i == 515 || i == 516) return {4, L"Neutral"};
    if ((i >= 303 && i <= 308) || (i >= 381 && i <= 385)) return {5, L"Traps and doors"};
    return {6, L"Effects"};
}

std::vector<PaletteUnit> paletteItems(AppState& app) {
    if (!app.ed || !app.ed->isOpen()) return {};
    Editor& ed = *app.ed;
    std::vector<PaletteUnit> out;
    auto lower = [](std::wstring t) {
        std::transform(t.begin(), t.end(), t.begin(), ::towlower);
        return t;
    };
    if (app.mode == Mode::Terrain) {
        const IsomTerrainTable* t = IsomTerrainTable::forEra(ed.era());
        if (!t || !ed.canPaintTerrain()) return out;
        // Each terrain, then the mod's terrains that stand on it (and theirs), in the mod's order: Jungle (High Dirt)
        // after High Dirt. (A mod's own terrains: Cosmonarchy tabs; a mod terrain whose ground is not in the list: at the end.)
        const auto& mods = ed.tileset().modTerrains;
        std::vector<bool> listed(mods.size(), false);
        auto add = [&](uint16_t value, const std::wstring& label) {
            PaletteUnit u;
            u.value = value, u.order = int(out.size()), u.label = label;
            u.section = std::wstring(kTilesetNames[ed.era() & 7]) + L" terrain";
            u.hay = lower(u.label);
            out.push_back(std::move(u));
        };
        std::function<void(uint16_t, int)> under = [&](uint16_t ground, int depth) {
            for (size_t k = 0; k < mods.size() && depth < 16; ++k)
                if (!listed[k] && mods[k].ground == ground) {
                    listed[k] = true;
                    if (mods[k].variantOf) continue;  // (a variant: the brush takes it for the ground under it)
                    add(mods[k].id, widen(mods[k].name));
                    under(mods[k].id, depth + 1);
                }
        };
        for (uint16_t i = 1; i <= t->terrainCount(); ++i)
            if (t->isTerrain(i)) add(i, widen(ed.terrainName(i))), under(i, 0);
        for (size_t k = 0; k < mods.size(); ++k)
            if (!listed[k] && !mods[k].variantOf) listed[k] = true, add(mods[k].id, widen(mods[k].name)), under(mods[k].id, 0);
        return out;
    }
    if (app.mode == Mode::Sprites) {  // sprites.dat: those with a picture
        UnitSprites& sp = ed.unitSprites();
        const int n = DatSchema::find("sprites") ? DatSchema::find("sprites")->entries : 0;
        for (int i = 0; i < n; ++i) {
            const int image = sp.imageForSprite(uint16_t(i));
            if (image < 0 || !sp.grp(image) || !sp.grp(image)->width) continue;
            PaletteUnit u;
            const std::wstring name = entryName("sprites", i);
            std::tie(u.order, u.section) = spriteSection(i, name, ed.era());
            u.value = i;
            u.label = (name.empty() ? L"(unnamed)" : name) + L"  #" + std::to_wstring(i);
            u.hay = lower(u.section + L" " + u.label);
            out.push_back(std::move(u));
        }
        std::stable_sort(out.begin(), out.end(), [](const PaletteUnit& a, const PaletteUnit& b) { return a.order < b.order; });
        return out;
    }
    if (app.mode == Mode::Doodads) {
        std::map<uint16_t, int> firstId;
        for (const auto& d : ed.doodadTypes().all()) {
            if (!d.inPalette) continue;
            PaletteUnit u;
            u.value = d.id;
            // (a Cosmonarchy tab: the mod's texts are not the game's, so its doodad groups' names are not the doodads';
            // the game's doodads are "Standard", the mod's brushes after them by their sets)
            u.section = modTab() ? std::wstring(tr(L"Standard")) : widen(ed.unitTypes().statText(d.groupString - 1u));
            firstId.emplace(d.groupString, d.id);
            u.order = firstId[d.groupString];
            u.label = L"#" + std::to_wstring(d.id) + L"  " + std::to_wstring(d.width) + L" x " + std::to_wstring(d.height);
            u.hay = lower(u.section + L" " + u.label);
            out.push_back(std::move(u));
        }
        const auto& brushes = brushDoodadList();  // (Cosmonarchy's doodad brushes, after the game's: a section for each set)
        std::map<std::wstring, int> setOrder;
        for (size_t i = 0; i < brushes.size(); ++i) {
            PaletteUnit u;
            u.value = brushDoodadValue(brushes[i]);
            const auto [set, own] = brushDoodadParts(*brushes[i]);
            u.section = set;
            u.order = setOrder.emplace(set, 100000 + int(setOrder.size())).first->second;
            u.label = own + L"  " + std::to_wstring(brushes[i]->width) + L" x " + std::to_wstring(brushes[i]->height);
            u.hay = lower(u.section + L" " + u.label);
            out.push_back(std::move(u));
        }
        std::stable_sort(out.begin(), out.end(), [](const PaletteUnit& a, const PaletteUnit& b) {
            return a.order != b.order ? a.order < b.order : a.value < b.value;
        });
        return out;
    }
    return app.mode == Mode::Units ? paletteUnits(app) : out;
}


// The section names of the palette's items, in their order (the section box lists them).
std::vector<std::wstring> paletteSectionNames(const std::vector<PaletteUnit>& units) {
    std::vector<std::wstring> names;
    for (const auto& u : units)
        if (std::find(names.begin(), names.end(), u.section) == names.end()) names.push_back(u.section);
    return names;
}

// Whether every word of the search (lower case) is in the item's text.
bool paletteWordsFound(const PaletteUnit& u, const std::wstring& filter) {
    std::wistringstream words(filter);
    std::wstring w;
    while (words >> w)
        if (u.hay.find(w) == std::wstring::npos) return false;
    return true;
}

// Whether some item has every word of the search: then the search is literal; otherwise the looser fuzzy match applies.
bool paletteLiteralSearch(const std::vector<PaletteUnit>& units, const std::wstring& filter) {
    return !filter.empty() && std::any_of(units.begin(), units.end(), [&](const PaletteUnit& u) { return paletteWordsFound(u, filter); });
}

// Whether the item is listed for the search and the section ("" is every section).
bool paletteUnitShown(const PaletteUnit& u, bool literal, const std::wstring& filter, const std::wstring& section) {
    return (section.empty() || u.section == section) && (literal ? paletteWordsFound(u, filter) : fuzzyScore(u.hay, filter) >= 0);
}
