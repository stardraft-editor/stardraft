// The Properties page (included into main.cpp after the players page; map_pages.inc shows it in the main area): the
// title and description the game shows in the lobby, the tileset, the size, where the map's content goes when the
// size changes, the terrain of new ground. The title and description go into the map as they are typed (map_pages.inc);
// the size and the tileset change only with their button, "Change the size and tileset".

struct MapPropsDialog {
    HWND wnd = nullptr, name = nullptr, desc = nullptr;  // the texts are game text editors (game_text.inc); the rest is painted (map_pages.inc)
    std::string nameText, descText;                      // as bytes (the game's codes)
    int era = 0, w = 0, h = 0, anchorIndex = 0;
    uint16_t fillTerrain = 1;
    std::wstring infoText;           // what "Change the size and tileset" will do ("": the size and tileset are the map's)
    RECT changeRect{}, resetRect{};  // the two buttons (tests)
    // What the fields showed when the page first showed the map's values: a change is a difference from these.
    bool baseSet = false;
    std::string nameBase, descBase;
    int eraBase = 0, wBase = 0, hBase = 0;
};
MapPropsDialog* gMapProps = nullptr;  // the working copy of the page that shows (map_pages.inc)
void mapPageChanged();                // map_pages.inc: the change goes into the map (the size and tileset: the rail's dot)
void mapPageApplyTerrain();           // map_pages.inc: the size and tileset button
void mapPageReload();
LRESULT CALLBACK mapPropsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);  // map_pages.inc (after the control kit)

const wchar_t* kAnchorNames[] = {L"Top left", L"Top", L"Top right", L"Left", L"Center", L"Right", L"Bottom left", L"Bottom", L"Bottom right"};

// The first terrain of a tileset (the default terrain of new ground).
uint16_t mapPropsFirstTerrain(int era) {
    if (const IsomTerrainTable* t = IsomTerrainTable::forEra(uint16_t(era)))
        for (uint16_t i = 1; i <= t->terrainCount(); ++i)
            if (t->isTerrain(i)) return i;
    return 1;
}

// What the size and tileset button will do, under the fields ("" when they are the map's).
void mapPropsInfo(MapPropsDialog& d) {
    std::wstring t;
    if (d.w != app.ed->width() || d.h != app.ed->height())
        t += L"The map becomes " + std::to_wstring(d.w) + L" x " + std::to_wstring(d.h) + L" tiles. StarDraft 2 deletes the parts of the map outside the new size.\r\n";
    if (d.era != app.ed->era())
        t += L"Each terrain becomes the " + std::wstring(kTilesetNames[d.era & 7]) +
             L" terrain of the same name or of the same kind (for example water or high ground). If there is none, it becomes the new ground's terrain. StarDraft 2 deletes the doodads, because each tileset has its own doodads.\r\n";
    d.infoText = t;
    if (d.wnd) InvalidateRect(d.wnd, nullptr, FALSE);
}

// What the page shows, read back.
void mapPropsRead(MapPropsDialog& d) {  // (the rest is set by the page's pickers)
    if (d.name) d.nameText = gameTextGet(d.name);
    if (d.desc) d.descText = gameTextGet(d.desc);
}

// A map text's line ends as StarCraft has them: \r\n (Blizzard's maps; with \n alone the game shows no new line,
// GitHub feedback #124). A \r\n stays; a \n alone gets its \r.
std::string mapTextLineEnds(const std::string& s) {
    std::string out;
    for (char ch : s) {
        if (ch == '\r') continue;
        if (ch == '\n') out += '\r';
        out += ch;
    }
    return out;
}

// The working copy from the map (the fields show it when the page is made).
void mapPropsLoad(MapPropsDialog& d) {
    d = MapPropsDialog{};
    d.nameText = app.ed->scenarioName(), d.descText = app.ed->scenarioDescription();  // (the editors keep them as they are)
    d.era = app.ed->era() & 7, d.w = app.ed->width(), d.h = app.ed->height();
    d.fillTerrain = mapPropsFirstTerrain(d.era);
}

// The size or the tileset differs from the map's (they wait for their button: the rail's dot).
bool mapPropsChanged(const MapPropsDialog& d) { return d.baseSet && (d.era != d.eraBase || d.w != d.wBase || d.h != d.hBase); }
// The title or the description differs from the map's (they go into the map at once).
bool mapPropsTextChanged(const MapPropsDialog& d) { return d.baseSet && (d.nameText != d.nameBase || d.descText != d.descBase); }

// The title and the description into the map (one undo step); then they are the map's values.
// A text the page did not change keeps the map's bytes (the box can write a code in another, equal way).
std::string mapPropsNameBytes(const MapPropsDialog& d) {
    return d.nameText != d.nameBase ? mapTextLineEnds(d.nameText) : app.ed->scenarioName();
}
std::string mapPropsDescBytes(const MapPropsDialog& d) {
    return d.descText != d.descBase ? mapTextLineEnds(d.descText) : app.ed->scenarioDescription();
}
void mapPropsApplyText(MapPropsDialog& d) {
    app.ed->setScenarioText(mapPropsNameBytes(d), mapPropsDescBytes(d));
    d.nameBase = d.nameText, d.descBase = d.descText;
    updateTitle();
    app.dirty = true;
}
bool mapPropsApply(MapPropsDialog& d);
// The size and the tileset (their button), as one undo step. False: the map stays as it was.
bool mapPropsApplyTerrain(MapPropsDialog& d) {
    d.nameBase = d.nameText, d.descBase = d.descText;  // (the text is set already)
    return mapPropsApply(d);
}

// Apply: the text with \r\n (the game's line ends), and the terrain, as one undo step. False: the map stays as it was.
bool mapPropsApply(MapPropsDialog& d) {
    const bool text = d.nameText != d.nameBase || d.descText != d.descBase;  // a text the page did not change stays byte for byte
    const bool resize = d.w != app.ed->width() || d.h != app.ed->height() || d.era != app.ed->era();
    if (!text && !resize) return true;
    std::string err;
    app.ed->beginStroke();
    if (text) app.ed->setScenarioText(mapPropsNameBytes(d), mapPropsDescBytes(d));
    bool ok = true;
    if (resize) {
        // Where the old map goes in the new one: the anchor's share of the size difference.
        const int ax = d.anchorIndex % 3, ay = d.anchorIndex / 3;
        const int shiftX = (d.w - app.ed->width()) * ax / 2, shiftY = (d.h - app.ed->height()) * ay / 2;
        ok = app.ed->resizeMap(uint16_t(d.w), uint16_t(d.h), uint16_t(d.era), shiftX, shiftY, d.fillTerrain, err);
    }
    app.ed->endStroke();
    if (!ok) {
        // The title and description of the same step stay set; the size and tileset do not.
        const std::wstring why = L"The map keeps its size and tileset: " + widen(err);
        if (gBackground) setMessage(why);  // test runs: no box to wait on
        else themedMessageBox(app.main, why.c_str(), L"Map properties", MB_ICONWARNING);
        return false;
    }
    if (resize) {
        shellRelayout();  // the minimap takes the new shape
        app.renderer.setSources(&app.data, &app.ed->unitSprites(), &app.ed->tileset(), app.ed->era());
        showActive();
        fitMap();
    }
    updateTitle();
    app.dirty = true;
    setMessage(resize ? L"Set the map properties: " + std::to_wstring(d.w) + L" x " + std::to_wstring(d.h) + L" " + kTilesetNames[d.era & 7]
                      : std::wstring(L"Set the map properties"));
    return true;
}
