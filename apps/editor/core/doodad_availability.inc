// Legal placement footprints: the same terrain and overlap rules as actual placement.
// Four difference-grid updates add each rectangle in constant time. Prefix sums make
// its union, so neighboring legal placements never darken one another with stacked alpha.
struct DoodadAvailability {
    const Editor* ed = nullptr;
    uint64_t revision = 0;
    int type = -1, width = 0, height = 0;
    bool check = true;
    size_t next = 0;
    DoodadFootprintUnion coverage;
};
DoodadAvailability gDoodadAvailability;
bool gShowDoodadAvailability = true;

void resetDoodadAvailability(DoodadAvailability& mask, const Editor& ed, const DoodadType& type, bool check) {
    mask = {};
    mask.ed = &ed, mask.revision = ed.revision(), mask.type = type.id, mask.check = check;
    mask.width = type.width ? std::max(0, ed.width() - int(type.width) + 1) : 0;
    mask.height = type.height ? std::max(0, ed.height() - int(type.height) + 1) : 0;
    mask.coverage.reset(ed.width(), ed.height());
}
void scanDoodadAvailability(DoodadAvailability& mask, const Editor& ed, const DoodadType& type, size_t count) {
    const size_t end = std::min(size_t(mask.width) * mask.height, mask.next + count);
    for (; mask.next < end; ++mask.next) {
        const int x = int(mask.next % mask.width), y = int(mask.next / mask.width);
        if (doodadFits(ed.chk(), type, x, y, mask.check) && !ed.doodadOverlaps(type, x, y))
            mask.coverage.add(x, y, type.width, type.height);
    }
}
void doodadAvailabilityScene(Editor& ed, const DoodadType& type, render::Scene& s) {
    if (!gShowDoodadAvailability || !appState().mouseIn || appState().pastingItems)
        return;
    auto& mask = gDoodadAvailability;
    const bool check = appState().doodadCheck;
    if (mask.ed != &ed || mask.revision != ed.revision() || mask.type != type.id || mask.check != check)
        resetDoodadAvailability(mask, ed, type, check);
    const size_t anchors = size_t(mask.width) * mask.height;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
    while (mask.next < anchors) {
        scanDoodadAvailability(mask, ed, type, 32);
        if (std::chrono::steady_clock::now() >= until)
            break;
    }
    if (mask.next < anchors)
        appState().dirty = true;
    mask.coverage.update();
    int vw = 0, vh = 0;
    viewSize(vw, vh);
    const auto& app = appState();
    const float zoom = std::max(.01f, app.zoom);
    const int x0 = std::clamp(int(std::floor(app.camX / 32)), 0, mask.coverage.width);
    const int x1 = std::clamp(int(std::ceil((app.camX + vw / zoom) / 32)), 0, mask.coverage.width);
    const int y0 = std::clamp(int(std::floor(app.camY / 32)), 0, mask.coverage.height);
    const int y1 = std::clamp(int(std::ceil((app.camY + vh / zoom) / 32)), 0, mask.coverage.height);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1;) {
            if (!mask.coverage.cells[size_t(y) * mask.coverage.width + x]) {
                ++x;
                continue;
            }
            const int start = x++;
            while (x < x1 && mask.coverage.cells[size_t(y) * mask.coverage.width + x])
                ++x;
            s.rects.push_back({float(start * 32), float(y * 32), float(x * 32), float((y + 1) * 32), 0x87E8A82C, true});
        }
}

bool doodadAvailabilityCheck(const std::string& name, std::string& out) {
    if (name != "doodad-footprints")
        return false;
    int checks = 0, failures = 0;
    auto check = [&](bool good, const char* reason) {
        ++checks;
        if (!good)
            ++failures, out += std::string("doodad-footprints: ") + reason + " (WRONG)\n";
    };
    DoodadFootprintUnion test;
    test.reset(12, 10);
    test.add(2, 1, 4, 3), test.update();
    check(std::count(test.cells.begin(), test.cells.end(), uint8_t(1)) == 12, "isolated footprint has the wrong area");
    check(test.cells[1 * 12 + 2] && test.cells[3 * 12 + 5] && !test.cells[4 * 12 + 5], "footprint shifted to cursor-center cells");
    test.add(4, 2, 4, 3), test.add(2, 1, 4, 3), test.update();
    check(std::count(test.cells.begin(), test.cells.end(), uint8_t(1)) == 20, "overlap did not form a single union");
    const auto once = test.cells;
    test.update();
    check(test.cells == once, "unchanged coverage was rebuilt incorrectly");
    test.reset(3, 3), test.add(-1, -1, 3, 3), test.update();
    check(std::count(test.cells.begin(), test.cells.end(), uint8_t(1)) == 4, "clipped footprint escaped the map");
    test.reset(3, 3), test.add(9, 9, 2, 2), test.update();
    check(std::count(test.cells.begin(), test.cells.end(), uint8_t(1)) == 0, "off-map rectangle was drawn");
    if (const auto* source = appState().ed.get(); source && source->isOpen()) {
        const auto original = source->chk().serialize();
        const auto revision = source->revision();
        Editor fixture(source->gameData());
        std::string why;
        check(fixture.newMap(32, 32, source->era(), 1, why), "fixture map could not be created");
        const auto& types = fixture.doodadTypes().all();
        const auto found = std::find_if(types.begin(), types.end(), [](const auto& type) {
            return type.inPalette && type.width > 1 && type.height > 1 && type.width < 16 && type.height < 16;
        });
        check(found != types.end(), "fixture tileset has no rectangular doodad");
        if (found != types.end()) {
            const auto& type = *found;
            check(fixture.placeDoodad(type.id, 8 * 32 + type.width * 16, 8 * 32 + type.height * 16, false),
                  "fixture doodad could not be placed");
            for (bool terrainCheck : {false, true}) {
                DoodadAvailability mask;
                resetDoodadAvailability(mask, fixture, type, terrainCheck);
                scanDoodadAvailability(mask, fixture, type, 1);
                check(mask.next == 1, "incremental scan did not retain its progress");
                scanDoodadAvailability(mask, fixture, type, 32 * 32);
                mask.coverage.update();
                std::vector<uint8_t> expected(32 * 32, 0);
                for (int y = 0; y < mask.height; ++y)
                    for (int x = 0; x < mask.width; ++x)
                        if (doodadFits(fixture.chk(), type, x, y, terrainCheck) && !fixture.doodadOverlaps(type, x, y))
                            for (int yy = y; yy < y + type.height; ++yy)
                                for (int xx = x; xx < x + type.width; ++xx)
                                    expected[size_t(yy) * 32 + xx] = 1;
                check(mask.coverage.cells == expected, "coverage differs from the real placement predicate");
            }
        }
        check(source->revision() == revision && source->chk().serialize() == original, "availability check changed the open map");
    } else
        out += "doodad-footprints: game-data fixture skipped (open a map)\n";
    out += "doodad-footprints: " + std::to_string(checks) + " checks, " + std::to_string(failures) + " failed\n";
    return true;
}
