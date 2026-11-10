// Paths and town areas (roadmap R24): three map overlays from the View menu, from the library's ground pathing
// (stardraft/pathing.h).
//   - Walkable ground: the ground that units cannot walk on is shaded; each separate ground area (an island) has its
//     own tint (the largest has none).
//   - Paths between bases: the start locations and the shortest ground path between each pair, with its length in
//     tiles; a dashed line where there is no ground path. The paths are for a unit of the size set in View > Path unit
//     size.
//   - Town areas: the game's resource areas (each start location's, then the others), each in its own color: its
//     resources (a line goes from the area's position to each), the buildable tiles near its town hall, and their
//     counts.
// The analysis runs on a worker thread on copies of the map's data when the map changes (the editor's revision); the
// map shows the last result until the new one is ready. The status bar tells what the colors are, and the path or
// the area under the mouse. The labels are in the UI font, painted on the map view (pathOverlayPaint), the same
// size at each zoom.

struct PathOverlay {
    bool walk = false, paths = false, towns = false;
    PathUnitSize size = PathUnitSize::Medium;
    // The last result, and the map and revision it is for.
    std::shared_ptr<const PathingAnalysis> result;
    const Editor* resultFor = nullptr;
    uint64_t resultRev = 0;
    PathUnitSize resultSize = PathUnitSize::Medium;
    // The job that runs.
    std::future<std::shared_ptr<PathingAnalysis>> job;
    const Editor* jobFor = nullptr;
    uint64_t jobRev = 0;
    PathUnitSize jobSize = PathUnitSize::Medium;
    // A copy of the tileset parts the analysis reads (groups, megatiles, VF4 flags), made again when the tileset changes.
    std::shared_ptr<const Tileset> tileset;
    const Tileset* tilesetOf = nullptr;
    int tilesetEra = -1;
    // The shapes, made again when the result or the theme changes.
    std::vector<render::RectDraw> walkRects, townRects;
    const PathingAnalysis* shapesFor = nullptr;
    int shapesTheme = -1;
    int hoverPath = -1, hoverBase = -1;
    double lastMs = 0;  // the last analysis, all steps
};
PathOverlay gPath;

bool pathOverlayOn() { return gPath.walk || gPath.paths || gPath.towns; }

// A label on the map: its place (map pixels, then dx, dy screen pixels), one or two lines of text, and a color bar at
// its left (a resource area's color). Made with the scene (addPathOverlays), painted by pathOverlayPaint.
struct PathLabel {
    int priority = 0;  // 0 first: a label that would cover one placed before it does not show
    float x = 0, y = 0;
    int dx = 0, dy = 0;
    std::wstring text, sub;
    draw::Color color = 0;
    bool striped = false;
    draw::Color stripe = 0;
};
std::vector<PathLabel> gPathLabels;

uint32_t rgbaOf(COLORREF c, uint8_t alpha) {
    return (uint32_t(GetRValue(c)) << 24) | (uint32_t(GetGValue(c)) << 16) | (uint32_t(GetBValue(c)) << 8) | alpha;
}

// The tint of ground area r (1, 2, ...): the hues around the circle, with the theme's lightness.
draw::Color canvasColor(uint32_t rgba) { return draw::rgb(int(rgba >> 24), int(rgba >> 16 & 255), int(rgba >> 8 & 255)); }

uint32_t regionTint(int r) {
    const double hue = std::fmod(30.0 + 137.5 * (r - 1), 360.0);
    return rgbaOf(theme::color(gLightTheme ? 0.62 : 0.72, 0.16, hue), 0x58);
}

// The color of a base's resource area (each area has its hue); a base with no area has the theme's green.
uint32_t areaColor(const Base& b, uint8_t alpha) {
    if (!b.area) return rgbaOf(kGood, alpha);
    return rgbaOf(theme::color(gLightTheme ? 0.55 : 0.78, 0.17, std::fmod(140.0 + 137.5 * (b.area - 1), 360.0)), alpha);
}

// Is the result for the map that shows (and for the unit size set)?
const PathingAnalysis* pathResult() {
    if (!appState().ed || gPath.resultFor != appState().ed.get() || !gPath.result) return nullptr;
    if (gPath.result->grid.width != appState().ed->width() * 4 || gPath.result->grid.height != appState().ed->height() * 4) return nullptr;
    return gPath.result.get();
}

// Start the analysis when the map changed, and take a finished one. `wait`: until the result is current (tests).
void pathOverlayPoll(bool wait = false) {
    if (!pathOverlayOn() || !appState().ed || !appState().ed->isOpen()) return;
    Editor& ed = *appState().ed;
    for (;;) {
        if (gPath.job.valid()) {
            if (!wait && gPath.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            gPath.result = gPath.job.get();
            gPath.resultFor = gPath.jobFor, gPath.resultRev = gPath.jobRev, gPath.resultSize = gPath.jobSize;
            gPath.lastMs = gPath.result->totalMs;
            gPath.hoverPath = gPath.hoverBase = -1;
            appState().dirty = true;
        }
        if (gPath.result && gPath.resultFor == &ed && gPath.resultRev == ed.revision() && gPath.resultSize == gPath.size) return;
        const Tileset& ts = ed.tileset();
        if (!gPath.tileset || gPath.tilesetOf != &ts || gPath.tilesetEra != ed.era()) {
            auto slim = std::make_shared<Tileset>();
            slim->groups = ts.groups, slim->megatiles = ts.megatiles, slim->minitileFlags = ts.minitileFlags, slim->era = ts.era;
            gPath.tileset = slim, gPath.tilesetOf = &ts, gPath.tilesetEra = ed.era();
        }
        gPath.jobFor = &ed, gPath.jobRev = ed.revision(), gPath.jobSize = gPath.size;
        PathingOptions opt;
        opt.unitSize = gPath.size;
        // (a mod's map sets resource areas itself: start locations of players 9 and up, resources of Player 12)
        gPath.job = std::async(std::launch::async, [terrain = ed.terrain(), units = ed.units(), sprites = ed.sprites(), types = ed.unitTypes(),
                                                     tileset = gPath.tileset, opt, mapAreas = ed.gameData().unitDataLoader() != nullptr] {
            PathingInput in{&terrain, tileset.get(), &types, &units, &sprites, mapAreas};
            return std::make_shared<PathingAnalysis>(analyzePathing(in, opt));
        });
        if (!wait) return;
    }
}

bool pathOverlayBusy() { return pathOverlayOn() && gPath.job.valid(); }

std::wstring baseName(const Base& b) {
    return b.start ? L"P" + std::to_wstring(b.owner + 1) : L"Area " + std::to_wstring(b.area);
}

std::wstring baseText(const Base& b) {
    wchar_t buf[240];
    const std::wstring name = b.start && b.area ? baseName(b) + L" (resource area " + std::to_wstring(b.area) + L")" : b.start ? baseName(b) : L"Resource area " + std::to_wstring(b.area);
    std::swprintf(buf, 240, L"%s: %d buildable tiles, %d mineral fields (%u minerals in total), %d geysers (%u gas in total)%s", name.c_str(), int(b.buildable.size()), b.minerals,
                  b.mineralAmount, b.geysers, b.gasAmount, b.hall ? L"" : L"; no place for a town hall");
    return buf;
}

std::wstring pathText(const PathingAnalysis& a, const GroundPath& p) {
    const std::wstring from = baseName(a.bases[size_t(p.from)]), to = baseName(a.bases[size_t(p.to)]);
    wchar_t buf[200];
    if (p.found)
        std::swprintf(buf, 200, L"Ground path from %s to %s: %.0f tiles (for a %hs)", from.c_str(), to.c_str(), p.lengthTiles,
                      pathUnitSizeName(a.unitCells == 2 ? PathUnitSize::Small : a.unitCells == 5 ? PathUnitSize::Large : PathUnitSize::Medium));
    else
        std::swprintf(buf, 200, L"No ground path from %s to %s: units must fly or go by transport", from.c_str(), to.c_str());
    return buf;
}

// What the colors mean (the status bar when an overlay goes on, and when the mouse leaves a path).
std::wstring pathLegendText() {
    std::wstring s;
    if (gPath.walk) s += L"Shaded: units cannot walk there. Each tint marks a ground area that units cannot walk to from the largest area (no tint). ";
    if (gPath.paths) s += L"Lines: the shortest ground paths between the start locations. ";
    if (gPath.towns) s += L"Each color is a resource area (the game's areas for the towns of computer players): its resources, with a line from the area's position, and the buildable tiles near its town hall.";
    while (!s.empty() && s.back() == L' ') s.pop_back();
    return s;
}

// The shapes that do not change while the result stays: the walk shading and the town tiles.
void makePathShapes(const PathingAnalysis& a) {
    const int themeKey = (gLightTheme ? 1 : 0) | (gAccentIndex << 1);
    if (gPath.shapesFor == &a && gPath.shapesTheme == themeKey) return;
    gPath.shapesFor = &a, gPath.shapesTheme = themeKey;
    gPath.walkRects.clear(), gPath.townRects.clear();
    // Walk shading: runs of cells of one kind on each row, joined with the same run of the row above (fewer rects).
    const int w = a.grid.width, h = a.grid.height;
    const uint32_t shade = rgbaOf(kBg, gLightTheme ? 0xB0 : 0xC8);
    struct Run {
        int x0, x1, kind, y0;
    };
    std::vector<Run> open, row, next;
    auto kindAt = [&](int x, int y) {
        const int r = a.regions.id[size_t(y) * w + x];
        return r < 0 ? -1 : r;  // -1 shaded, 0 (the largest area) nothing, else a tint
    };
    auto emit = [&](const Run& r, int y1) {
        render::RectDraw d;
        d.x0 = r.x0 * 8.0f, d.y0 = r.y0 * 8.0f, d.x1 = r.x1 * 8.0f, d.y1 = y1 * 8.0f;
        d.rgba = r.kind < 0 ? shade : regionTint(r.kind), d.fill = true;
        gPath.walkRects.push_back(d);
    };
    for (int y = 0; y <= h; ++y) {
        row.clear();
        if (y < h)
            for (int x = 0; x < w;) {
                const int k = kindAt(x, y);
                int e = x + 1;
                while (e < w && kindAt(e, y) == k) ++e;
                if (k != 0) row.push_back({x, e, k, y});
                x = e;
            }
        // Runs that go on from the row above keep their top; the others end here.
        next.clear();
        size_t i = 0;
        for (Run r : row) {
            while (i < open.size() && open[i].x0 < r.x0) emit(open[i], y), ++i;
            if (i < open.size() && open[i].x0 == r.x0 && open[i].x1 == r.x1 && open[i].kind == r.kind) r.y0 = open[i].y0, ++i;
            next.push_back(r);
        }
        for (; i < open.size(); ++i) emit(open[i], y);
        std::swap(open, next);
    }
    // Town tiles: runs of tiles on each row.
    const int mw = appState().ed ? appState().ed->width() : w / 4;
    for (const Base& b : a.bases) {
        const uint32_t fill = areaColor(b, b.start ? 0x58 : 0x40);
        for (size_t k = 0; k < b.buildable.size();) {
            const int t = b.buildable[k];
            size_t e = k + 1;
            while (e < b.buildable.size() && b.buildable[e] == b.buildable[e - 1] + 1 && b.buildable[e] / mw == t / mw) ++e;
            render::RectDraw d;
            d.x0 = (t % mw) * 32.0f, d.y0 = (t / mw) * 32.0f, d.x1 = d.x0 + 32.0f * float(e - k), d.y1 = d.y0 + 32;
            d.rgba = fill, d.fill = true;
            gPath.townRects.push_back(d);
            k = e;
        }
    }
}

void addPathOverlays(render::Scene& s) {
    gPathLabels.clear();
    pathOverlayPoll();
    const PathingAnalysis* a = pathOverlayOn() ? pathResult() : nullptr;
    if (!a) return;
    makePathShapes(*a);
    Editor& ed = *appState().ed;
    if (gPath.walk) addRectsInView(s.rects, gPath.walkRects);  // (only those the view shows: core/map_edit.inc)
    auto line = [&](float x0, float y0, float x1, float y1, uint32_t rgba, float thickness) {
        render::RectDraw r;
        r.x0 = x0, r.y0 = y0, r.x1 = x1, r.y1 = y1, r.rgba = rgba, r.thickness = thickness, r.line = true;
        s.rects.push_back(r);
    };
    const float px = 1 / appState().zoom;  // one screen pixel in map pixels
    // The labels (pathOverlayPaint places and paints them).
    auto label = [&](int priority, float x, float y, int dx, int dy, std::wstring text, draw::Color color) -> PathLabel& {
        PathLabel l;
        l.priority = priority, l.x = x, l.y = y, l.dx = dx, l.dy = dy, l.text = std::move(text), l.color = color;
        gPathLabels.push_back(std::move(l));
        return gPathLabels.back();
    };
    const draw::Color white = draw::rgb(255, 255, 255);
    if (gPath.towns) {
        addRectsInView(s.rects, gPath.townRects);
        // Each area's resources: a box in the area's color, and a line from the area's position.
        for (size_t bi = 0; bi < a->bases.size(); ++bi) {
            const Base& b = a->bases[bi];
            const bool hot = int(bi) == gPath.hoverBase;
            const uint32_t color = hot ? rgbaOf(kFeedback, 0xFF) : areaColor(b, 0xFF), spoke = hot ? rgbaOf(kFeedback, 0xD0) : areaColor(b, 0xA0);
            for (int i : b.resources) {
                if (size_t(i) >= ed.units().size()) continue;
                const Rect box = ed.unitBox(ed.units()[size_t(i)]);
                if (b.area) line(b.areaX, b.areaY, float(box.left + box.right) / 2, float(box.top + box.bottom) / 2, spoke, hot ? 2.0f : 1.5f);
                outline(s, box, color, 2);
            }
        }
    }
    if (gPath.paths) {
        const uint32_t pathColor = rgbaOf(gLightTheme ? kAccent : kAccentBright, 0xF0), hot = rgbaOf(kFeedback, 0xFF), under = rgbaOf(kBg, 0xC0);
        for (size_t k = 0; k < a->paths.size(); ++k) {
            const GroundPath& p = a->paths[k];
            const Base &from = a->bases[size_t(p.from)], &to = a->bases[size_t(p.to)];
            if (!p.found) {  // a dashed line: no ground path
                const float dx = to.x - from.x, dy = to.y - from.y, len = std::hypot(dx, dy);
                const int dashes = std::max(1, int(len / 48));
                for (int d = 0; d < dashes; ++d) {
                    const float f0 = float(d) / dashes, f1 = f0 + 0.5f / dashes;
                    line(from.x + dx * f0, from.y + dy * f0, from.x + dx * f1, from.y + dy * f1, rgbaOf(kBad, int(k) == gPath.hoverPath ? 0xFF : 0xC0), 2);
                }
                continue;
            }
            const bool isHot = int(k) == gPath.hoverPath;
            for (size_t i = 1; i < p.points.size(); ++i)
                line(p.points[i - 1].first, p.points[i - 1].second, p.points[i].first, p.points[i].second, under, isHot ? 7.0f : 5.0f);
            for (size_t i = 1; i < p.points.size(); ++i)
                line(p.points[i - 1].first, p.points[i - 1].second, p.points[i].first, p.points[i].second, isHot ? hot : pathColor, isHot ? 4.0f : 2.5f);
        }
        // The lengths at the middle of each path (a separate pass: above every line).
        for (size_t k = 0; k < a->paths.size(); ++k) {
            const GroundPath& p = a->paths[k];
            const int priority = int(k) == gPath.hoverPath ? 0 : 5;
            if (!p.found) {
                const Base &from = a->bases[size_t(p.from)], &to = a->bases[size_t(p.to)];
                label(priority, (from.x + to.x) / 2, (from.y + to.y) / 2, scale(6), -scale(6), L"No ground path", draw::rgb(255, 128, 118));
                continue;
            }
            if (p.points.empty()) continue;
            const auto [mx, my] = p.points[p.points.size() / 2];
            wchar_t buf[32];
            std::swprintf(buf, 32, L"%.0f tiles", p.lengthTiles);
            label(priority, mx, my, scale(6), -scale(6), buf, white);
        }
    }
    // The bases: the town hall's box and a label.
    if (gPath.paths || gPath.towns)
        for (size_t i = 0; i < a->bases.size(); ++i) {
            const Base& b = a->bases[i];
            if (!b.start && !gPath.towns) continue;
            const Rect hall{int(b.x) - 64, int(b.y) - 48, int(b.x) + 64, int(b.y) + 48};
            const bool hot = int(i) == gPath.hoverBase;
            const uint32_t color = hot ? rgbaOf(kFeedback, 0xFF) : gPath.towns ? areaColor(b, 0xFF) : rgbaOf(b.start ? kAccentBright : kGood, 0xFF);
            if (b.hall) outline(s, hall, color, b.start || hot ? 2.0f : 1.0f);
            // The resource area's position: a square in the area's color (6 screen pixels from its middle).
            if (gPath.towns && b.area) {
                const float m = 6 * px;
                s.rects.push_back({b.areaX - m, b.areaY - m, b.areaX + m, b.areaY + m, color, true, 1, true});
                s.rects.push_back({b.areaX - m, b.areaY - m, b.areaX + m, b.areaY + m, 0x101216FFu, false, 1.5f, true});
            }
            // The name (start locations before the other labels), then the counts below it. An area with no town hall
            // has them below its position.
            const std::wstring name = b.start && b.area && gPath.towns ? baseName(b) + L", area " + std::to_wstring(b.area) : baseName(b);
            PathLabel& l = label(hot ? 0 : b.start ? 1 : 3, b.hall ? float(hall.left) : b.x, b.hall ? float(hall.bottom) : b.y, b.hall ? 0 : -scale(6),
                                 b.hall ? scale(6) : scale(12), name, white);
            if (gPath.towns) {
                wchar_t buf[96];
                std::swprintf(buf, 96, L"%d minerals, %d gas, %d tiles", b.minerals, b.geysers, int(b.buildable.size()));
                l.sub = buf, l.striped = true, l.stripe = canvasColor(color);
            }
        }
}

// The labels on the map view `view`, in the UI font. A label is a dark box at its place on the map (a name in bold,
// then a second line); the labels go by priority, and a label that would cover one placed before it does not show
// (the status bar tells it under the mouse).
draw::Rect gameViewClip(draw::Rect view);  // (core/map_scene.inc)
void pathOverlayPaint(draw::Canvas& c, draw::Rect view) {
    if (!pathOverlayOn() || gPathLabels.empty()) return;
    const draw::Rect all = view;  // (the map's camera is at its top left corner)
    view = gameViewClip(view);  // (View > In-game view: no labels on the black bars)
    const draw::FontDesc plainDesc{draw::Face::Ui, scale(13)}, boldDesc{draw::Face::Ui, scale(13), true};
    const auto plain = c.font(plainDesc), bold = c.font(boldDesc);
    const auto old = c.setFont(plain);
    const int h = c.metrics().height, pad = scale(5);
    c.pushClip(view);
    std::vector<draw::Rect> taken;
    std::vector<size_t> order(gPathLabels.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t l, size_t r) { return gPathLabels[l].priority < gPathLabels[r].priority; });
    for (size_t i : order) {
        const PathLabel& l = gPathLabels[i];
        const bool two = !l.sub.empty();
        const int bar = l.striped ? scale(4) : 0;
        int textW = two ? c.measure(l.sub) : 0;
        c.setFont(two ? bold : plain);
        textW = std::max(textW, c.measure(l.text));
        const int w = bar + pad + textW + pad;
        draw::Rect box;
        box.left = all.left + int(std::lround((l.x - appState().camX) * appState().zoom)) + l.dx;
        box.top = all.top + int(std::lround((l.y - appState().camY) * appState().zoom)) + l.dy;
        // A label that starts in the view stays in it (its end does not go past the view's right side).
        if (box.left < view.right && box.left + w > view.right - scale(4)) box.left = std::max(view.left + scale(4), view.right - scale(4) - w);
        box.right = box.left + w, box.bottom = box.top + (two ? 2 : 1) * h + scale(4);
        bool free = box.right > view.left && box.left < view.right && box.bottom > view.top && box.top < view.bottom;
        for (const draw::Rect& o : taken) free = free && (box.left >= o.right || o.left >= box.right || box.top >= o.bottom || o.top >= box.bottom);
        if (free) {
            taken.push_back(box);
            c.roundBox(box, draw::rgb(16, 23, 27), draw::rgb(66, 80, 83), float(scale(3)));
            if (bar) c.fill({box.left + 1, box.top + 1, box.left + 1 + bar, box.bottom - 1}, l.stripe);
            const int tx = box.left + bar + pad, ty = box.top + scale(2);
            c.text({tx, ty, box.right, ty + h}, l.text, l.color, draw::kMiddle);
            c.setFont(plain);
            if (two) c.text({tx, ty + h, box.right, ty + h * 2}, l.sub, draw::rgb(176, 188, 192), draw::kMiddle);
        }
        c.setFont(plain);
    }
    c.popClip();
    c.setFont(old);
}

// The path or base under the mouse (map pixels): its text for the status bar, empty if none.
std::wstring pathOverlayHover(float wx, float wy) {
    const PathingAnalysis* a = pathOverlayOn() ? pathResult() : nullptr;
    const int oldPath = gPath.hoverPath, oldBase = gPath.hoverBase;
    gPath.hoverPath = gPath.hoverBase = -1;
    std::wstring text;
    if (a && gPath.paths) {
        const float reach = 6 / appState().zoom;  // 6 screen pixels
        float best = reach;
        auto segDist = [](float px, float py, float ax, float ay, float bx, float by) {
            const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
            const float t = l2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / l2, 0.0f, 1.0f) : 0.0f;
            return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
        };
        for (size_t k = 0; k < a->paths.size(); ++k) {
            const GroundPath& p = a->paths[k];
            const Base &from = a->bases[size_t(p.from)], &to = a->bases[size_t(p.to)];
            if (!p.found) {
                const float d = segDist(wx, wy, from.x, from.y, to.x, to.y);
                if (d < best) best = d, gPath.hoverPath = int(k);
                continue;
            }
            for (size_t i = 1; i < p.points.size(); ++i) {
                const float d = segDist(wx, wy, p.points[i - 1].first, p.points[i - 1].second, p.points[i].first, p.points[i].second);
                if (d < best) best = d, gPath.hoverPath = int(k);
            }
        }
        if (gPath.hoverPath >= 0) text = pathText(*a, a->paths[size_t(gPath.hoverPath)]);
    }
    if (a && text.empty() && (gPath.paths || gPath.towns))
        for (size_t i = 0; i < a->bases.size(); ++i) {
            const Base& b = a->bases[i];
            const float reach = 8 / appState().zoom;  // (8 screen pixels round the area's position)
            const bool onArea = gPath.towns && b.area && std::abs(wx - b.areaX) <= reach && std::abs(wy - b.areaY) <= reach;
            if (onArea || ((b.start || gPath.towns) && std::abs(wx - b.x) <= 64 && std::abs(wy - b.y) <= 48)) {
                gPath.hoverBase = int(i), text = baseText(b);
                break;
            }
        }
    if (gPath.hoverPath != oldPath || gPath.hoverBase != oldBase) appState().dirty = true;
    return text;
}

// After a mouse move over the map: the status bar tells what is under the mouse, or the legend when the mouse leaves it.
void pathOverlayMouseMove(float wx, float wy) {
    if (!pathOverlayOn()) return;
    const bool was = gPath.hoverPath >= 0 || gPath.hoverBase >= 0;
    const std::wstring text = pathOverlayHover(wx, wy);
    if (!text.empty()) setHint(text);
    else if (was) setHint(pathLegendText());
}

void pathOverlayToggle(int id) {
    bool& flag = id == ID_VIEW_WALK ? gPath.walk : id == ID_VIEW_PATHS ? gPath.paths : gPath.towns;
    flag = !flag;
    pathOverlayPoll();
    appState().dirty = true;
    if (flag) setMessage(pathLegendText());
    else setMessage(id == ID_VIEW_WALK ? L"The walkable ground does not show" : id == ID_VIEW_PATHS ? L"The paths between bases do not show"
                                                                                                   : L"The town areas do not show");
}

void pathOverlaySize(int id) {
    gPath.size = id == ID_PATHSIZE_SMALL ? PathUnitSize::Small : id == ID_PATHSIZE_LARGE ? PathUnitSize::Large : PathUnitSize::Medium;
    pathOverlayPoll();
    appState().dirty = true;
    setMessage(std::wstring(L"Paths are for a ") + widen(pathUnitSizeName(gPath.size)));
}
