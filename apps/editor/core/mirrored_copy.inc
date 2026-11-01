// A background artwork search prepares the raw tile clipboard, never the source document.
struct MirroredCopyResult {
    Editor::TerrainCopies copy;
    int approximate = 0, unmatched = 0;
};
std::future<MirroredCopyResult> gMirroredCopyJob;
const Editor* gMirroredCopyFor = nullptr;
uint64_t gMirroredCopyRevision = 0;
bool gMirroredCopyCancelled = false;
std::shared_ptr<std::atomic<bool>> gMirroredCopyStop;
// Camera and mouse positions are deliberately absent: inspecting or panning the map may continue while searching.
auto mirroredCopyContext() {
    const auto& app = appState();
    return std::tuple{app.mode,           currentTool(app),     terrainKind(),         app.palette,
                      app.palette2,       gActivePanel,         app.hasTerrainSel,     app.terrainSel.left,
                      app.terrainSel.top, app.terrainSel.right, app.terrainSel.bottom, app.pasting,
                      app.terrainPasting, app.pastingItems};
}
std::optional<decltype(mirroredCopyContext())> gMirroredCopyContext;
void cancelMirroredCopy() {
    if (!gMirroredCopyJob.valid()) return;
    gMirroredCopyCancelled = true;
    if (gMirroredCopyStop) *gMirroredCopyStop = true;
}
bool mirroredCopyCurrent() {
    const auto& app = appState();
    return !gMirroredCopyCancelled && app.ed && app.ed.get() == gMirroredCopyFor && app.ed->revision() == gMirroredCopyRevision &&
           gMirroredCopyContext && *gMirroredCopyContext == mirroredCopyContext();
}

struct MirrorSearchAtExit {
    ~MirrorSearchAtExit() {
        if (gMirroredCopyStop) *gMirroredCopyStop = true;
        if (gMirroredCopyJob.valid()) gMirroredCopyJob.wait();
    }
} gMirrorSearchAtExit;

void createMirroredVersion() {
    auto& app = appState();
    if (!app.ed || !app.hasTerrainSel || gMirroredCopyJob.valid()) return;
    if (!copySelectedTiles(*app.ed)) return;
    auto copy = app.ed->terrainCopies();
    copy.terrain = {}; // This is deliberately a raw tile operation.
    gMirroredCopyFor = app.ed.get(), gMirroredCopyRevision = app.ed->revision(), gMirroredCopyCancelled = false;
    gMirroredCopyStop = std::make_shared<std::atomic<bool>>(false);
    gMirroredCopyContext = mirroredCopyContext();
    gMirroredCopyJob = std::async(std::launch::async, [copy = std::move(copy), ts = app.ed->tileset(), stop = gMirroredCopyStop]() mutable {
        TileMirrorMatcher matcher(ts);
        MirroredCopyResult result{std::move(copy)};
        auto& c = result.copy.tiles;
        const auto source = c.values;
        for (int y = 0; y < c.height; ++y)
            for (int x = 0; x < c.width; ++x) {
                if (*stop) return result;
                const auto original = source[size_t(y) * c.width + x];
                if (original == 0xffff) c.values[size_t(y) * c.width + c.width - 1 - x] = original;
                else {
                    const auto match = matcher.match(original);
                    c.values[size_t(y) * c.width + c.width - 1 - x] = match.tile;
                    result.approximate += match.matched && !match.exact;
                    result.unmatched += !match.matched;
                }
            }
        return result;
    });
    setMessage(L"Finding mirrored tile artwork. Escape cancels.");
    app.dirty = true;
}
void mirroredCopyPoll() {
    if (!gMirroredCopyJob.valid()) return;
    auto& app = appState();
    app.dirty = true;
    if (!mirroredCopyCurrent()) cancelMirroredCopy();
    if (gMirroredCopyJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
        auto result = gMirroredCopyJob.get();
        if (!mirroredCopyCurrent()) return;
        app.ed->setTerrainCopies(result.copy);
        shareTerrainCopy(*app.ed);
        setMode(Mode::Terrain);
        setTerrainKind(1);
        pasteMapCopy(app);
        setMessage(L"Mirrored tiles: " + std::to_wstring(result.approximate) + L" approximate, " + std::to_wstring(result.unmatched) +
                   L" unmatched. Check the preview and terrain joins. Click to paste; Escape cancels.");
    } catch (const std::exception& e) {
        setMessage(widen(e.what()));
    }
}
