// Paths and town areas (roadmap R24): three map overlays from the View menu, from the library's ground pathing
// (stardraft/pathing.h).
//   - Walkable ground: the ground that units cannot walk on is shaded; each separate ground area (an island) has its
//     own tint (the largest has none).
//   - Paths between bases: the start locations and the shortest ground path between each pair, with its length in
//     tiles; a dashed line where there is no ground path. The paths are for a unit of the size set in View > Path unit
//     size.
//   - Town areas: around each start location (and each expansion), its resources and the buildable tiles near its
//     town hall, with their counts.
// The analysis runs on a worker thread on copies of the map's data when the map changes (the editor's revision); the
// map shows the last result until the new one is ready. A legend in the view's bottom-left corner tells what the
// colors are; the status bar tells the length of the path under the mouse.

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

uint32_t rgbaOf(COLORREF c, uint8_t alpha) {
    return (uint32_t(GetRValue(c)) << 24) | (uint32_t(GetGValue(c)) << 16) | (uint32_t(GetBValue(c)) << 8) | alpha;
}

// The tint of ground area r (1, 2, ...): the hues around the circle, with the theme's lightness.
uint32_t regionTint(int r) {
    const double hue = std::fmod(30.0 + 137.5 * (r - 1), 360.0);
    return rgbaOf(theme::color(gLightTheme ? 0.62 : 0.72, 0.16, hue), 0x58);
}

// Is the result for the map that shows (and for the unit size set)?
const PathingAnalysis* pathResult() {
    if (!app.ed || gPath.resultFor != app.ed.get() || !gPath.result) return nullptr;
    if (gPath.result->grid.width != app.ed->width() * 4 || gPath.result->grid.height != app.ed->height() * 4) return nullptr;
    return gPath.result.get();
}

// Start the analysis when the map changed, and take a finished one. `wait`: until the result is current (tests).
void pathOverlayPoll(bool wait = false) {
    if (!pathOverlayOn() || !app.ed || !app.ed->isOpen()) return;
    Editor& ed = *app.ed;
    for (;;) {
        if (gPath.job.valid()) {
            if (!wait && gPath.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            gPath.result = gPath.job.get();
            gPath.resultFor = gPath.jobFor, gPath.resultRev = gPath.jobRev, gPath.resultSize = gPath.jobSize;
            gPath.lastMs = gPath.result->totalMs;
            gPath.hoverPath = gPath.hoverBase = -1;
            app.dirty = true;
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
        gPath.job = std::async(std::launch::async, [terrain = ed.terrain(), units = ed.units(), sprites = ed.sprites(), types = ed.unitTypes(),
                                                     tileset = gPath.tileset, opt] {
            PathingInput in{&terrain, tileset.get(), &types, &units, &sprites};
            return std::make_shared<PathingAnalysis>(analyzePathing(in, opt));
        });
        if (!wait) return;
    }
}

bool pathOverlayBusy() { return pathOverlayOn() && gPath.job.valid(); }

std::wstring baseName(const Base& b) {
    return b.start ? L"P" + std::to_wstring(b.owner + 1) : std::wstring(L"Expansion");
}

std::wstring baseText(const Base& b) {
    wchar_t buf[200];
    std::swprintf(buf, 200, L"%s: %d buildable tiles, %d mineral fields (%u minerals in total), %d geysers (%u gas in total)", baseName(b).c_str(), int(b.buildable.size()), b.minerals,
                  b.mineralAmount, b.geysers, b.gasAmount);
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
    if (gPath.towns) s += L"Green tiles: buildable tiles near a base. Orange boxes: its resources.";
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
    const int mw = app.ed ? app.ed->width() : w / 4;
    for (const Base& b : a.bases) {
        const uint32_t fill = rgbaOf(kGood, b.start ? 0x50 : 0x30);
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
    pathOverlayPoll();
    const PathingAnalysis* a = pathOverlayOn() ? pathResult() : nullptr;
    if (!a) return;
    makePathShapes(*a);
    Editor& ed = *app.ed;
    const size_t firstText = s.texts.size();
    if (gPath.walk) s.rects.insert(s.rects.end(), gPath.walkRects.begin(), gPath.walkRects.end());
    auto line = [&](float x0, float y0, float x1, float y1, uint32_t rgba, float thickness) {
        render::RectDraw r;
        r.x0 = x0, r.y0 = y0, r.x1 = x1, r.y1 = y1, r.rgba = rgba, r.thickness = thickness, r.line = true;
        s.rects.push_back(r);
    };
    const float px = 1 / app.zoom;  // one screen pixel in map pixels
    // The labels, placed after the shapes by priority (0 first): a label that would cover one placed before it does
    // not show (the status bar tells it under the mouse).
    struct Label {
        int priority;
        render::TextDraw t;
        int needs;  // shows only when label `needs` (an index into labels) shows; -1: none
    };
    std::vector<Label> labels;
    auto label = [&](int priority, float x, float y, std::string text, uint32_t rgba, int needs = -1) {
        labels.push_back({priority, {x, y, std::move(text), rgba}, needs});
        return int(labels.size()) - 1;
    };
    if (gPath.towns) {
        s.rects.insert(s.rects.end(), gPath.townRects.begin(), gPath.townRects.end());
        for (const Base& b : a->bases)
            for (int i : b.resources)
                if (size_t(i) < ed.units().size()) outline(s, ed.unitBox(ed.units()[size_t(i)]), rgbaOf(kWarn, 0xE0), 2);
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
                label(priority, (from.x + to.x) / 2 + 6 * px, (from.y + to.y) / 2 - 6 * px, "No ground path", rgbaOf(kBad, 0xFF));
                continue;
            }
            if (p.points.empty()) continue;
            const auto [mx, my] = p.points[p.points.size() / 2];
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.0f tiles", p.lengthTiles);
            label(priority, mx + 6 * px, my - 6 * px, buf, priority == 0 ? rgbaOf(kFeedback, 0xFF) : 0xFFFFFFFFu);
        }
    }
    // The bases: the town hall's box and a label.
    if (gPath.paths || gPath.towns)
        for (size_t i = 0; i < a->bases.size(); ++i) {
            const Base& b = a->bases[i];
            if (!b.start && !gPath.towns) continue;
            const Rect hall{int(b.x) - 64, int(b.y) - 48, int(b.x) + 64, int(b.y) + 48};
            outline(s, hall, rgbaOf(int(i) == gPath.hoverBase ? kFeedback : (b.start ? kAccentBright : kGood), 0xFF), 2);
            // The name (start locations before the other labels), then the counts beside it where they fit.
            const std::string name = narrow(baseName(b));
            const bool hot = int(i) == gPath.hoverBase;
            const float lx = float(hall.left), ly = float(hall.bottom) + 4 * px;
            const int nameLabel = label(hot ? 0 : b.start ? 1 : 3, lx, ly, name, 0xFFFFFFFFu);
            if (gPath.towns) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "%d buildable, %d minerals, %d gas", int(b.buildable.size()), b.minerals, b.geysers);
                label(hot ? 0 : b.start ? 2 : 4, lx + float(name.size() + 1) * 9 * px, ly, buf, 0xFFFFFFFFu, nameLabel);
            }
        }
    std::vector<int> order(labels.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::stable_sort(order.begin(), order.end(), [&](int l, int r) { return labels[size_t(l)].priority < labels[size_t(r)].priority; });
    std::vector<std::array<float, 4>> taken;  // screen boxes of the placed labels
    std::vector<bool> shown(labels.size(), false);
    for (int i : order) {
        render::TextDraw& t = labels[size_t(i)].t;
        if (labels[size_t(i)].needs >= 0 && !shown[size_t(labels[size_t(i)].needs)]) continue;
        const float x0 = (t.x - app.camX) * app.zoom, y0 = (t.y - app.camY) * app.zoom;
        const std::array<float, 4> box{x0 - 2, y0 - 2, x0 + float(t.text.size()) * 6 * t.scale + 2, y0 + 10 * t.scale + 2};
        bool free = true;
        for (const auto& o : taken) free &= box[0] >= o[2] || o[0] >= box[2] || box[1] >= o[3] || o[1] >= box[3];
        if (!free) continue;
        taken.push_back(box);
        shown[size_t(i)] = true;
        s.texts.push_back(std::move(t));
    }

    // The legend: the view's bottom-left corner, in screen pixels.
    struct Entry {
        uint32_t swatch;
        bool lineSwatch;
        const char* text;
    };
    std::vector<Entry> entries;
    if (gPath.walk) {
        entries.push_back({rgbaOf(kBg, 0xFF), false, "Units cannot walk here"});
        entries.push_back({regionTint(1), false, "A separate ground area (each has a tint)"});
    }
    if (gPath.paths) {
        static char pathLine[80];
        std::snprintf(pathLine, sizeof pathLine, "Shortest ground path (%s)", pathUnitSizeName(gPath.size));
        entries.push_back({rgbaOf(gLightTheme ? kAccent : kAccentBright, 0xFF), true, pathLine});
        entries.push_back({rgbaOf(kBad, 0xFF), true, "No ground path"});
    }
    if (gPath.towns) {
        entries.push_back({rgbaOf(kGood, 0xA0), false, "Buildable tiles near a base"});
        entries.push_back({rgbaOf(kWarn, 0xFF), false, "Resources of a base"});
    }
    int vw, vh;
    viewSize(vw, vh);
    const float sc = float(app.dpi) / 96, rowH = 18 * sc, pad = 8 * sc;
    float maxW = 0;
    for (const Entry& e : entries) maxW = std::max(maxW, float(std::strlen(e.text)) * 6.0f * 1.5f * sc);
    const float boxW = pad * 3 + 14 * sc + maxW, boxH = pad * 2 + rowH * float(entries.size());
    const float x0 = 10 * sc, y0 = float(vh) - 10 * sc - boxH;
    // The map's labels do not show through the legend.
    s.texts.erase(std::remove_if(s.texts.begin() + std::ptrdiff_t(firstText), s.texts.end(),
                                 [&](const render::TextDraw& t) {
                                     const float tx = (t.x - app.camX) * app.zoom, ty = (t.y - app.camY) * app.zoom;
                                     const float tw = float(t.text.size()) * 6 * t.scale, th = 10 * t.scale;
                                     return tx < x0 + boxW && tx + tw > x0 && ty < y0 + boxH && ty + th > y0;
                                 }),
                  s.texts.end());
    s.rects.push_back({x0, y0, x0 + boxW, y0 + boxH, rgbaOf(kPanel, 0xE8), true, 1, false});
    s.rects.push_back({x0, y0, x0 + boxW, y0 + boxH, rgbaOf(kBorder, 0xFF), false, 1, false});
    for (size_t i = 0; i < entries.size(); ++i) {
        const float y = y0 + pad + rowH * float(i), sx = x0 + pad;
        if (entries[i].lineSwatch) {
            render::RectDraw r;
            r.x0 = sx, r.y0 = y + 7 * sc, r.x1 = sx + 14 * sc, r.y1 = y + 7 * sc, r.rgba = entries[i].swatch, r.thickness = 3 * sc, r.world = false, r.line = true;
            s.rects.push_back(r);
        } else {
            s.rects.push_back({sx, y + 1 * sc, sx + 14 * sc, y + 13 * sc, entries[i].swatch, true, 1, false});
            s.rects.push_back({sx, y + 1 * sc, sx + 14 * sc, y + 13 * sc, rgbaOf(kBorderStrong, 0xFF), false, 1, false});
        }
        render::TextDraw t;
        t.x = sx + 14 * sc + pad, t.y = y + 1 * sc, t.text = entries[i].text, t.rgba = rgbaOf(kText, 0xFF), t.world = false, t.scale = 1.5f * sc;
        s.texts.push_back(t);
    }
}

// The path or base under the mouse (map pixels): its text for the status bar, empty if none.
std::wstring pathOverlayHover(float wx, float wy) {
    const PathingAnalysis* a = pathOverlayOn() ? pathResult() : nullptr;
    const int oldPath = gPath.hoverPath, oldBase = gPath.hoverBase;
    gPath.hoverPath = gPath.hoverBase = -1;
    std::wstring text;
    if (a && gPath.paths) {
        const float reach = 6 / app.zoom;  // 6 screen pixels
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
            if ((b.start || gPath.towns) && std::abs(wx - b.x) <= 64 && std::abs(wy - b.y) <= 48) {
                gPath.hoverBase = int(i), text = baseText(b);
                break;
            }
        }
    if (gPath.hoverPath != oldPath || gPath.hoverBase != oldBase) app.dirty = true;
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
    app.dirty = true;
    if (flag) setMessage(pathLegendText());
    else setMessage(id == ID_VIEW_WALK ? L"The walkable ground does not show" : id == ID_VIEW_PATHS ? L"The paths between bases do not show"
                                                                                                   : L"The town areas do not show");
}

void pathOverlaySize(int id) {
    gPath.size = id == ID_PATHSIZE_SMALL ? PathUnitSize::Small : id == ID_PATHSIZE_LARGE ? PathUnitSize::Large : PathUnitSize::Medium;
    pathOverlayPoll();
    app.dirty = true;
    setMessage(std::wstring(L"Paths are for a ") + widen(pathUnitSizeName(gPath.size)));
}

void pathOverlayMenus(HMENU m) {
    CheckMenuItem(m, ID_VIEW_WALK, gPath.walk ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, ID_VIEW_PATHS, gPath.paths ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, ID_VIEW_TOWNS, gPath.towns ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuRadioItem(m, ID_PATHSIZE_SMALL, ID_PATHSIZE_LARGE,
                       gPath.size == PathUnitSize::Small ? ID_PATHSIZE_SMALL : gPath.size == PathUnitSize::Large ? ID_PATHSIZE_LARGE : ID_PATHSIZE_MEDIUM,
                       MF_BYCOMMAND);
}
