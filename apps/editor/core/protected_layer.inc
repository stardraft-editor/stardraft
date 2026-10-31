// Protected terrain (core.inc): the map's tiles that no terrain edit changes (Editor::protectTiles). The terrain
// selection's right-click menu protects and unprotects them; View > Layers > Protected terrain shows them over the map.
// The map file does not have them: they are kept in the map's project (project.stardraft: "protectedterrain"), else in
// the map's own settings (core/map_prefs.inc), as the map is saved (the map with no changes that are not saved: they
// are kept for that map file), and they come back when the map opens.

// The mask as text: "<width>,<height>:<first>+<count>,..." (runs of protected tiles, by index row by row); "": none.
std::string protectedTerrainText(const Editor& ed) {
    const std::vector<uint8_t>& m = ed.protectedTiles();
    if (m.empty()) return {};
    std::string out = std::to_string(ed.width()) + "," + std::to_string(ed.height()) + ":";
    bool first = true;
    for (size_t i = 0; i < m.size();) {
        if (!m[i]) {
            ++i;
            continue;
        }
        size_t e = i;
        while (e < m.size() && m[e]) ++e;
        out += (first ? "" : ",") + std::to_string(i) + "+" + std::to_string(e - i);
        first = false;
        i = e;
    }
    return out;
}

// The mask of a text (protectedTerrainText) for a map of this size; false: not one (another size, not a mask).
bool protectedTerrainFromText(const std::string& text, int width, int height, std::vector<uint8_t>& mask) {
    int w = 0, h = 0, used = 0;
    if (std::sscanf(text.c_str(), "%d,%d:%n", &w, &h, &used) < 2 || w != width || h != height || !used) return false;
    mask.assign(size_t(w) * size_t(h), 0);
    std::stringstream runs(text.substr(size_t(used)));
    for (std::string run; std::getline(runs, run, ',');) {
        unsigned long long at = 0, n = 0;
        if (std::sscanf(run.c_str(), "%llu+%llu", &at, &n) != 2 || at > mask.size() || n > mask.size() - at) return false;
        std::fill(mask.begin() + long(at), mask.begin() + long(at + n), uint8_t(1));
    }
    return true;
}

// Keeps the active tab's protected tiles in its place (its project, else its map's settings) when they changed and the
// map has no changes that are not saved (so the kept tiles are always those of the map file), and
// reads them from there when the tab first has a place (the map opened, or saved the first time with none protected).
// Each frame (sdl: the frame's start), when a map or project opens, and when a tab closes (after its save question).
void protectedTerrainSync() {
    AppState& app = shellState();
    if (gActive < 0 || gActive >= int(gDocs.size()) || !app.ed || !app.ed->isOpen() || gDocs[size_t(gActive)].page) return;
    Doc& d = gDocs[size_t(gActive)];
    Editor& ed = *app.ed;
    EudProject* pr = d.project.get();
    const fs::path place = pr ? pr->file() : mapPrefsFile(ed.path());
    if (place.empty()) return;
    if (!d.protectLoaded || d.protectPlace != place) {
        d.protectLoaded = true, d.protectPlace = place;
        if (!ed.hasProtectedTiles()) {  // (the kept tiles; with tiles protected already: they go to the new place)
            std::string text;
            if (pr) text = pr->protectedTerrain();
            else if (const auto t = stardraft::Settings(place).text("ProtectedTerrain")) text = *t;
            std::vector<uint8_t> mask;
            if (!text.empty() && protectedTerrainFromText(text, ed.width(), ed.height(), mask)) ed.setProtectedTiles(std::move(mask));
            d.protectRev = ed.protectedRevision();
            return;
        }
        d.protectRev = 0;
    }
    if (ed.protectedRevision() == d.protectRev || ed.modified()) return;
    d.protectRev = ed.protectedRevision();
    const std::string text = protectedTerrainText(ed);
    std::string err;
    if (pr) {
        if (pr->protectedTerrain() == text) return;
        pr->setProtectedTerrain(text);
        if (!gSelfTest && !pr->save(err)) shellLogLine(tr(L"Could not save the protected terrain: ") + widen(err));
        return;
    }
    stardraft::Settings s(place);
    if (!(text.empty() ? s.remove("ProtectedTerrain") : s.set("ProtectedTerrain", text)))
        shellLogLine(tr(L"Could not save the protected terrain: ") + widen(s.lastError()));
}

// The right-click menu of a terrain selection: Protect terrain, Unprotect terrain (its tiles).
int protectedInSelection(const Editor& ed) {
    int n = 0;
    for (auto [x, y] : terrainSelTiles()) n += ed.tileProtected(x, y);
    return n;
}
void protectSelection(bool on) {
    AppState& app = shellState();
    if (!app.ed) return;
    const int n = app.ed->protectTiles(terrainSelTiles(), on);
    if (n) {
        std::wstring t = n == 1 ? tr(on ? L"Protected 1 tile" : L"Unprotected 1 tile") : tr(on ? L"Protected %d tiles" : L"Unprotected %d tiles");
        if (const size_t at = t.find(L"%d"); at != std::wstring::npos) t.replace(at, 2, std::to_wstring(n));
        setMessage(t);
    }
    protectedTerrainSync();
    shellMenusChanged();
    app.dirty = true;
}

// View > Layers > Protected terrain: a light yellow fill over each run of protected tiles, with dashed diagonal lines
// over it (one pattern over the whole map, so the runs join), made again when the mask changes.
struct ProtectedLayer {
    const Editor* editor = nullptr;
    uint64_t revision = 0;
    bool light = false;
    float gap = 0;
    std::vector<render::RectDraw> fills;
} gProtectedLayer;

void addProtectedLayer(const Editor& ed, render::Scene& s) {
    if (!gLayers.protectedTerrain || !ed.isOpen() || !ed.hasProtectedTiles()) return;
    ProtectedLayer& l = gProtectedLayer;
    // (the lines at least 12 screen pixels apart: 16 map pixels, twice that for each halving of the zoom below 75%)
    float gap = 16;
    while (gap * appState().zoom < 12 && gap < 512) gap *= 2;
    if (l.editor != &ed || l.revision != ed.protectedRevision() || l.light != gLightTheme || l.gap != gap) {
        l.editor = &ed, l.revision = ed.protectedRevision(), l.light = gLightTheme, l.gap = gap;
        l.fills.clear();
        const uint32_t fill = gLightTheme ? 0xFFE9704Cu : 0xFFF2A040u, dash = gLightTheme ? 0xC8A000D0u : 0xFFE680C0u;
        const float kGap = gap, kDash = gap * 0.375f, kPeriod = gap * 0.625f;  // (map pixels: a dash and its period, down)
        const int w = ed.width(), h = ed.height();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w;) {
                if (!ed.tileProtected(x, y)) {
                    ++x;
                    continue;
                }
                int e = x;
                while (e < w && ed.tileProtected(e, y)) ++e;
                render::RectDraw d;
                const float left = x * 32.0f, right = e * 32.0f, top = y * 32.0f, bottom = top + 32;
                d.x0 = left, d.y0 = top, d.x1 = right, d.y1 = bottom, d.rgba = fill, d.fill = true;
                l.fills.push_back(d);
                // The lines x + y = c (up to the right), in dashes along y, cut to the run's box.
                for (float c = std::ceil((left + top) / kGap) * kGap; c < right + bottom; c += kGap)
                    for (float y0 = top; y0 < bottom; y0 += kPeriod) {
                        // (the dash's part with left <= c - y < right)
                        const float a = std::max(y0, c - right), b = std::min({y0 + kDash, bottom, c - left});
                        if (b <= a) continue;
                        render::RectDraw line;
                        line.line = true, line.rgba = dash, line.thickness = 1.5f;
                        line.x0 = c - a, line.y0 = a, line.x1 = c - b, line.y1 = b;
                        l.fills.push_back(line);
                    }
                x = e;
            }
    }
    s.rects.insert(s.rects.end(), l.fills.begin(), l.fills.end());
}

// View > Layers > Protected terrain.
void toggleProtectedLayer() {
    gLayers.protectedTerrain = !gLayers.protectedTerrain;
    setMessage(gLayers.protectedTerrain ? std::wstring(tr(L"Protected terrain shows"))
                                        : std::wstring(tr(L"Protected terrain is hidden (View > Layers shows it again)")));
    shellMenusChanged();
    shellState().dirty = true;
}

// --check protect (the snapshot test): the protected tiles, how many of them have other tiles (MTXM or TILE) than the
// map file has, how many other tiles do, and the next undo step's name.
bool protectCheck(const std::string& what, std::string& out) {
    if (what != "protect") return false;
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return out = "protect: no map open (WRONG)\n", true;
    const Editor& ed = *app.ed;
    Bytes raw;
    std::string err;
    if (!readMapFile(ed.path(), raw, err)) return out = "protect: could not read the map file: " + err + " (WRONG)\n", true;
    const Chk file = Chk::parse(raw);
    const TerrainLayer fileMtxm = readTerrain(file, "MTXM"), fileTile = readTerrain(file, file.has("TILE") ? "TILE" : "MTXM");
    const TerrainLayer mtxm = readTerrain(ed.chk(), "MTXM"), tile = readTerrain(ed.chk(), ed.chk().has("TILE") ? "TILE" : "MTXM");
    int n = 0, changed = 0, others = 0;
    for (int y = 0; y < ed.height(); ++y)
        for (int x = 0; x < ed.width(); ++x) {
            const size_t i = size_t(y) * ed.width() + size_t(x);
            if (i >= mtxm.tiles.size() || i >= fileMtxm.tiles.size() || i >= tile.tiles.size() || i >= fileTile.tiles.size()) continue;
            const bool differs = mtxm.tiles[i] != fileMtxm.tiles[i] || tile.tiles[i] != fileTile.tiles[i];
            if (ed.tileProtected(x, y)) ++n, changed += differs;
            else others += differs;
        }
    const Editor::StepName step = ed.canUndo() ? ed.undoStepName(0) : Editor::StepName{};
    std::string name = step.text;
    if (const size_t at = name.find("%d"); at != std::string::npos) name.replace(at, 2, std::to_string(step.count));
    out = "protect: " + std::to_string(n) + " protected tiles, " + std::to_string(changed) + " of them changed" +
          (n > 0 && !changed ? " (yes)" : " (WRONG)") + "; " + std::to_string(others) + " other tiles changed; " +
          std::to_string(protectedInSelection(ed)) + " protected in the selection; layer " + (gLayers.protectedTerrain ? "on" : "off") +
          "; undo: " + (name.empty() ? "none" : name) + "\n";
    return true;
}
