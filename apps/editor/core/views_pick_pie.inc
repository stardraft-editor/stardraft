// The pie palette of the Units, Doodads and Sprites modes: hold Space on the map and a ring of the palette's groups
// comes up in the middle of the map (Terrain mode has the ring of terrains: PieView, core/views_palette.inc). The mouse
// over a group opens the group's ring outside it, round the group's direction: more groups (a race's Ground Units, Air
// Units, Buildings), or the group's items as pictures, in rows. Letting go of Space on an item picks it, as a click in
// the palette; on a group or in the middle: no change. The first group, at the top, is the mode's last picks (Recent:
// there also with no picks, so that each group keeps its direction).

// A group (its kids) or an item (`value`, no kids).
struct PickPieNode {
    std::wstring label;
    int value = -1;
    std::vector<PickPieNode> kids;
};

// The mode's groups: Recent, then the palette's sections (the units: a race, then its sections; a mod's units: its main
// categories, then their sections). More than 9 groups: the last ones are in "More".
PickPieNode pickPieTree(AppState& app) {
    PickPieNode root;
    if (!app.ed || !app.ed->isOpen() || (app.mode != Mode::Units && app.mode != Mode::Doodads && app.mode != Mode::Sprites)) return root;
    const std::vector<PaletteUnit> rows = paletteItems(app);
    auto kid = [](PickPieNode& of, const std::wstring& label) -> PickPieNode& {
        for (PickPieNode& k : of.kids)
            if (k.value < 0 && k.label == label) return k;
        of.kids.push_back({label, -1, {}});
        return of.kids.back();
    };
    for (const PaletteUnit& u : rows) {
        std::wstring first = u.section, second;
        if (!u.group.empty()) first = u.group, second = u.groupSection.empty() ? u.section : u.groupSection;
        else if (app.mode == Mode::Units)
            if (const size_t sp = u.section.find(L' '); sp != std::wstring::npos) first = u.section.substr(0, sp), second = u.section.substr(sp + 1);
        PickPieNode& g = kid(root, first);
        (second.empty() ? g : kid(g, second)).kids.push_back({u.label, u.value, {}});
    }
    for (PickPieNode& g : root.kids)  // (a group with one section: its items at once)
        if (g.kids.size() == 1 && g.kids[0].value < 0) g.kids = std::vector<PickPieNode>(std::move(g.kids[0].kids));
    if (root.kids.size() > 9) {
        PickPieNode more{tr(L"More"), -1, {}};
        more.kids.assign(std::make_move_iterator(root.kids.begin() + 8), std::make_move_iterator(root.kids.end()));
        root.kids.resize(8);
        root.kids.push_back(std::move(more));
    }
    PickPieNode recent{tr(L"Recent"), -1, {}};
    if (const auto found = gRecentPicks.of.find(int(app.mode)); found != gRecentPicks.of.end() && (app.mode != Mode::Doodads || gRecentPicks.era == app.ed->era()))
        for (const int v : found->second)
            if (const auto at = std::find_if(rows.begin(), rows.end(), [v](const PaletteUnit& u) { return u.value == v; }); at != rows.end())
                recent.kids.push_back({at->label, v, {}});
    root.kids.insert(root.kids.begin(), std::move(recent));
    return root;
}

// The rings' view: as large as the map (the host), the rings round its middle. `path` is the hot slice of each ring
// from the middle out. While it shows, it has the mouse (the host's capture).
struct PickPieView : draw::View {
    PickPieNode root;
    std::vector<int> path;
    std::function<void(draw::Canvas&, int value, draw::Rect box)> art;  // an item's picture in `box`
    // The terrain an item is on (a doodad's; 0: none) and that terrain's picture, tiles x tiles tiles: the item's
    // slice is filled with it, and a group's slice when most of its items are on one terrain.
    std::function<int(int value)> groundOf;
    std::function<draw::Bitmap*(int terrain, int tiles)> groundPicture;
    std::function<void()> onClick;   // a left click: the slice under it is picked
    std::function<void()> onCancel;  // another button, or the capture went away: the rings close with no change

    static constexpr double kTurn = 2 * 3.14159265358979323846;
    static int hole() { return scale(58); }       // the middle: no slice (letting go there: no change)
    static int textWidth() { return scale(88); }  // a ring of groups
    static inline int cellSize = 50;              // (the host's: larger for the modes whose items have no names to tell them by)
    static int cell() { return scale(cellSize); }  // an item's cell: a row of an items' ring is this wide
    static constexpr int kMaxRows = 4;

    draw::Point middle() const { return {width() / 2, height() / 2}; }
    // Other groups (the host, before it shows).
    void reset(PickPieNode tree) { root = std::move(tree), path.clear(), nameWidth_ = 0; }
    // The hot node (nullptr: none): an item, or a group.
    const PickPieNode* hotNode() const {
        const PickPieNode* n = &root;
        for (const int i : path) {
            if (i < 0 || size_t(i) >= n->kids.size()) return nullptr;
            n = &n->kids[size_t(i)];
        }
        return path.empty() ? nullptr : n;
    }

    // A ring: the kids of `of`, from `inner` px out, round the direction `mid` (the first ring: the whole turn).
    struct Ring {
        const PickPieNode* of = nullptr;
        bool items = false;  // (pictures in rows; else names)
        float inner = 0, outer = 0;
        double mid = 0, step = 0;      // names: each slice's angle
        std::vector<int> rowCount;     // items: the count of each row, from the inside
        int shownItems = 0;            // items: those that have a cell (the rest: in the palette)
        double rowStep(int j) const { return cell() / (double(inner) + cell() * (j + 0.5)); }
        int count() const { return items ? shownItems : int(of->kids.size()); }
    };
    static double turned(double a) {  // into -half a turn .. half a turn
        a = std::fmod(a, kTurn);
        return a > kTurn / 2 ? a - kTurn : a < -kTurn / 2 ? a + kTurn : a;
    }
    // The middle direction of the slice `i` of a ring (an item: of its cell), and the item's row.
    static double sliceMid(const Ring& r, int i, int* row = nullptr) {
        if (!r.items) return r.mid + (i - (r.count() - 1) / 2.0) * r.step;
        int j = 0;
        while (j + 1 < int(r.rowCount.size()) && i >= r.rowCount[size_t(j)]) i -= r.rowCount[size_t(j)], ++j;
        if (row) *row = j;
        return r.mid + (i - (r.rowCount[size_t(j)] - 1) / 2.0) * r.rowStep(j);
    }
    // The rings that show: the first one, then one for each group of the path.
    std::vector<Ring> rings() const {
        std::vector<Ring> out;
        const PickPieNode* n = &root;
        float inner = float(hole());
        double mid = 0;
        for (size_t level = 0; n && !n->kids.empty(); ++level) {
            Ring r;
            r.of = n, r.inner = inner, r.mid = mid;
            if (level == 0) r.mid = (int(n->kids.size()) - 1) * (kTurn / int(n->kids.size())) / 2;  // (the first slice: at the top)
            r.items = n->kids[0].value >= 0;
            const int count = int(n->kids.size());
            if (r.items) {
                // As few rows as hold the items (kMaxRows at most), each row with its part by its length, so that the
                // rows have one angle.
                int rows = 1, room = 0;
                auto cap = [&](int j) { return std::max(1, int(kTurn / r.rowStep(j))); };
                // (no row goes out of the view)
                const int most = std::clamp(int((std::min(width(), height()) / 2 - r.inner) / cell()), 1, kMaxRows);
                for (room = cap(0); room < count && rows < most; ++rows) room += cap(rows);
                r.shownItems = std::min(count, room);
                int left = r.shownItems;
                double radii = 0;
                for (int j = 0; j < rows; ++j) radii += r.inner + cell() * (j + 0.5);
                for (int j = 0; j < rows; ++j) {
                    const int want = j + 1 == rows ? left : int(std::lround(r.shownItems * (r.inner + cell() * (j + 0.5)) / radii));
                    const int got = std::clamp(want, 0, std::min(left, cap(j)));
                    r.rowCount.push_back(got), left -= got;
                }
                for (int j = rows - 1; j >= 0 && left > 0; --j) {  // (the rounding's rest: where there is room)
                    const int more = std::min(left, cap(j) - r.rowCount[size_t(j)]);
                    r.rowCount[size_t(j)] += more, left -= more;
                }
                r.outer = r.inner + float(rows * cell());
            } else {
                r.outer = r.inner + float(textWidth());
                r.step = kTurn / count;
                if (level > 0) r.step = std::min(r.step, double((nameWidth_ > 0 ? nameWidth_ : scale(100)) + scale(18)) / ((r.inner + r.outer) / 2));
            }
            out.push_back(r);
            if (level >= path.size() || path[level] < 0 || path[level] >= r.count()) break;
            mid = sliceMid(r, path[level]);
            inner = r.outer;
            n = &n->kids[size_t(path[level])];
        }
        return out;
    }
    // The slice of ring `r` at the distance `d` in the direction `a` (-1: not in the ring's arc).
    static int sliceAt(const Ring& r, double d, double a) {
        const double off = turned(a - r.mid);
        if (!r.items) {
            const int n = r.count();
            if (n * r.step >= kTurn - 1e-6) return wholeTurnSlice(r, a);
            const int i = int(std::floor(off / r.step + n / 2.0));
            return i >= 0 && i < n ? i : -1;
        }
        const int rows = int(r.rowCount.size());
        const int j = std::clamp(int((d - r.inner) / cell()), 0, rows - 1);
        const int n = r.rowCount[size_t(j)], i = int(std::floor(off / r.rowStep(j) + n / 2.0));
        if (i < 0 || i >= n) return -1;
        int before = 0;
        for (int k = 0; k < j; ++k) before += r.rowCount[size_t(k)];
        return before + i;
    }
    // (a ring of the whole turn: the slice whose middle is nearest to the direction)
    static int wholeTurnSlice(const Ring& r, double a) {
        const int n = r.count();
        const double first = sliceMid(r, 0);
        double t = std::fmod(a - first, kTurn);
        if (t < 0) t += kTurn;
        return int(t / r.step + 0.5) % n;
    }
    // The mouse at `p`: the ring it is in (or the outermost one, further out) takes its slice; outside that ring's arc,
    // the ring before it does.
    void track(draw::Point p) {
        const double dx = p.x - middle().x, dy = p.y - middle().y, d = std::hypot(dx, dy), a = std::atan2(dx, -dy);
        std::vector<int> was = path;
        if (d < hole()) path.clear();
        else {
            const std::vector<Ring> rs = rings();
            int level = 0;
            while (level + 1 < int(rs.size()) && d >= rs[size_t(level) + 1].inner) ++level;
            for (; level >= 0; --level)
                if (const int i = sliceAt(rs[size_t(level)], d, a); i >= 0) {
                    path.resize(size_t(level));
                    path.push_back(i);
                    break;
                }
        }
        if (was != path) invalidate();
    }
    void captureLost() override {
        if (shown() && onCancel) onCancel();
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Down && e.button == 0 && onClick) return track(e.pt), onClick(), true;
        if (e.kind == draw::MouseKind::Down && onCancel) return onCancel(), true;
        if (e.kind != draw::MouseKind::Move) return false;
        track(e.pt);
        return true;
    }
    void paint(draw::Canvas& c) override {
        using PointF = draw::Canvas::PointF;
        const float cx = float(middle().x), cy = float(middle().y);
        auto at = [&](double a, float rad) { return PointF{cx + float(std::sin(a)) * rad, cy - float(std::cos(a)) * rad}; };
        auto deg = [](double a) { return float(a * 360 / kTurn) - 90; };
        const int pad = scale(6), lineH = scale(20);
        const float gap = float(scale(4));
        c.fill({0, 0, width(), height()}, draw::rgb(0, 0, 0), 150);  // (the map goes dark behind the rings)
        // A slice: the part of the ring ra..rb between the directions a0 and a1, with half a gap off each side (thin
        // four-corner pieces along the arc).
        // `pic`: the slice is that picture's part there (one picture of 16 x 16 tiles over the rings, 32 px a tile or
        // as large as the rings, so that the slices of one terrain go on from one to the next); `inset`: the slice is
        // that much smaller on each side.
        constexpr int groundTiles = 16;
        float groundSpan = float(scale(32) * groundTiles);
        auto tile = [&](double a0, double a1, float ra, float rb, draw::Color col, draw::Bitmap* pic = nullptr, float inset = 0) {
            const float off = gap / 2 + inset, span = groundSpan;
            ra += off, rb -= off;
            if (ra <= 0) return;
            const double i0 = a0 + off / ra, i1 = a1 - off / ra, o0 = a0 + off / rb, o1 = a1 - off / rb;
            auto uv = [&](PointF p) { return PointF{0.5f + (p.x - cx) / span, 0.5f + (p.y - cy) / span}; };
            if (i1 <= i0 || o1 <= o0 || rb <= ra) return;
            const int parts = std::max(1, int(std::ceil((a1 - a0) / 0.06)));
            const double over = 0.004 / (a1 - a0);
            for (int j = 0; j < parts; ++j) {
                const double t = double(j) / parts, u = double(j + 1) / parts + (j + 1 < parts ? over : 0);
                const PointF q[4] = {at(i0 + (i1 - i0) * t, ra), at(o0 + (o1 - o0) * t, rb), at(o0 + (o1 - o0) * u, rb), at(i0 + (i1 - i0) * u, ra)};
                if (!pic) {
                    c.smoothPolygon(q, 4, col);
                    continue;
                }
                const PointF uvs[4] = {uv(q[0]), uv(q[1]), uv(q[2]), uv(q[3])};
                c.bitmapQuad(*pic, q, uvs);
            }
        };
        // A slice with its terrain's picture; the hot one has an accent rim round it.
        auto groundTile = [&](double a0, double a1, float ra, float rb, bool on, draw::Bitmap* pic) {
            tile(a0, a1, ra, rb, on ? kAccent : kBorderStrong);  // (the rim: a dark terrain's slice shows too)
            if (pic) tile(a0, a1, ra, rb, kCtlHot, pic, float(scale(on ? 3 : 1)));
        };
        // A group's terrain: the one most of its items are on, when 3 of 10 or more are (Recent and a group of groups: 6 of 10; 0: none).
        std::function<void(const PickPieNode&, std::map<int, int>&, int&)> countGround = [&](const PickPieNode& n, std::map<int, int>& by, int& all) {
            for (const PickPieNode& k : n.kids) {
                if (k.value < 0) countGround(k, by, all);
                else if (++all; const int t = groundOf(k.value)) ++by[t];
            }
        };
        auto groundOfGroup = [&](const PickPieNode& n, bool mixed) {
            if (!groundOf) return 0;
            std::map<int, int> by;
            int all = 0;
            countGround(n, by, all);
            int best = 0, most = 0;
            for (const auto& [t, count] : by)
                if (count > most) most = count, best = t;
            return most * 10 >= all * (mixed ? 6 : 3) ? best : 0;
        };
        // The names' width for the outer rings of names: the widest name, 120 px at most.
        int widest = scale(40);
        std::function<void(const PickPieNode&)> measureAll = [&](const PickPieNode& n) {
            for (const PickPieNode& k : n.kids)
                if (k.value < 0) widest = std::max(widest, c.measure(k.label)), measureAll(k);
        };
        if (nameWidth_ <= 0) measureAll(root), nameWidth_ = std::min(widest, scale(120));
        const std::vector<Ring> rs = rings();
        for (const Ring& r : rs) groundSpan = std::max(groundSpan, 2 * r.outer);
        const draw::Color kOnAccent = draw::rgb(255, 255, 255);
        float outermost = float(hole());
        double hotAngle = 0;
        for (size_t level = 0; level < rs.size(); ++level) {
            const Ring& r = rs[level];
            const int hot = level < path.size() ? path[level] : -1;
            outermost = r.outer;
            if (!r.items) {
                const int n = r.count();
                const double a0 = sliceMid(r, 0) - r.step / 2;
                const float rm = (r.inner + r.outer) / 2;
                const int most = std::max(scale(36), int(2 * rm * std::sin(std::min(r.step, kTurn / 2) / 2)) - scale(8));
                for (int k = 0; k < n; ++k) {
                    const PickPieNode& node = r.of->kids[size_t(k)];
                    const int ground = groundOfGroup(node, (level == 0 && k == 0) || (!node.kids.empty() && node.kids[0].value < 0));
                    draw::Bitmap* pic = ground && groundPicture ? groundPicture(ground, groundTiles) : nullptr;
                    if (pic) groundTile(a0 + k * r.step, a0 + (k + 1) * r.step, r.inner, r.outer, k == hot, pic);
                    else tile(a0 + k * r.step, a0 + (k + 1) * r.step, r.inner, r.outer, k == hot ? kAccentFill : kCtlHot);
                    const PointF q = at(sliceMid(r, k), rm);
                    // (a slice at the side has room for a longer name than its chord)
                    const double side = std::fabs(std::sin(sliceMid(r, k)));
                    const int room = std::max(most, int(side * (r.outer - r.inner)) - scale(10));
                    // A name longer than its room is on two lines, cut at the space nearest its middle.
                    std::wstring one = node.label, two;
                    if (c.measure(one) + 2 * pad > room) {
                        size_t cut = std::wstring::npos;
                        for (size_t i = one.find(L' '); i != std::wstring::npos; i = one.find(L' ', i + 1))
                            if (cut == std::wstring::npos || std::llabs((long long)(i) * 2 - (long long)(one.size())) < std::llabs((long long)(cut) * 2 - (long long)(one.size()))) cut = i;
                        if (cut != std::wstring::npos) two = one.substr(cut + 1), one.resize(cut);
                    }
                    const int w = std::min(std::max(c.measure(one), c.measure(two)) + 2 * pad, room), x = int(q.x) - w / 2;
                    const int y = int(q.y) - (two.empty() ? lineH : 2 * lineH) / 2;
                    const draw::Color ink = k == hot ? kOnAccent : node.kids.empty() ? kFaint : kText;  // (faint: Recent with no picks)
                    if (pic)  // (on a terrain's picture: the name is on a plate)
                        c.roundBox({x, y - scale(2), x + w, y + (two.empty() ? lineH : 2 * lineH) + scale(2)}, k == hot ? kAccentFill : kPanel, k == hot ? kAccentLine : kBorder,
                                   float(scale(stardraft::kit::kRadius)));
                    c.text({x, y, x + w, y + lineH}, one, ink, draw::kCenter | draw::kMiddle | draw::kEllipsis);
                    if (!two.empty()) c.text({x, y + lineH, x + w, y + 2 * lineH}, two, ink, draw::kCenter | draw::kMiddle | draw::kEllipsis);
                }
                if (hot >= 0 && hot < n) hotAngle = sliceMid(r, hot);
            } else {
                int first = 0;
                for (int j = 0; j < int(r.rowCount.size()); ++j) {
                    const int n = r.rowCount[size_t(j)];
                    if (n <= 0) continue;
                    const float ra = r.inner + float(j * cell()), rb = ra + float(cell());
                    const double step = r.rowStep(j), a0 = r.mid - n * step / 2;
                    for (int k = 0; k < n; ++k) {
                        const bool on = first + k == hot;
                        const int ground = groundOf ? groundOf(r.of->kids[size_t(first + k)].value) : 0;
                        draw::Bitmap* pic = ground && groundPicture ? groundPicture(ground, groundTiles) : nullptr;
                        if (pic) groundTile(a0 + k * step, a0 + (k + 1) * step, ra, rb, on, pic);
                        else tile(a0 + k * step, a0 + (k + 1) * step, ra, rb, on ? kAccentFill : kCtlHot);
                        const PointF q = at(a0 + (k + 0.5) * step, (ra + rb) / 2);
                        const int half = (cell() - scale(10)) / 2;
                        if (art) art(c, r.of->kids[size_t(first + k)].value, {int(q.x) - half, int(q.y) - half, int(q.x) + half, int(q.y) + half});
                        if (on) hotAngle = a0 + (k + 0.5) * step;
                    }
                    first += n;
                }
            }
        }
        // The middle: the hot groups' names (the last two), or what letting go there does.
        const float r0 = float(hole()) - gap / 2;
        c.smoothEllipse({cx - r0, cy - r0}, 2 * r0, 2 * r0, kPanel);
        c.smoothArc({cx - r0, cy - r0}, 2 * r0, 2 * r0, 0, 360, path.empty() ? kBorder : kAccentLine, float(scale(2)));
        std::vector<const PickPieNode*> groups;
        const PickPieNode* walk = &root;
        for (const int i : path) {
            if (i < 0 || size_t(i) >= walk->kids.size()) break;
            walk = &walk->kids[size_t(i)];
            if (walk->value < 0) groups.push_back(walk);
        }
        if (groups.size() > 2) groups.erase(groups.begin(), groups.end() - 2);
        const int tx = int(cx - r0) + pad, tw = int(2 * r0) - 2 * pad;
        // (a hot item: its picture, large)
        const PickPieNode* shown = hotNode();
        const bool preview = art && shown && shown->value >= 0;
        if (preview) {
            const int half = int(r0 * 0.66f);
            art(c, shown->value, {int(cx) - half, int(cy) - half, int(cx) + half, int(cy) + half});
            groups.clear();
        }
        if (groups.empty() && !preview) c.text({tx, int(cy) - lineH / 2, tx + tw, int(cy) + lineH / 2}, tr(L"No change"), kDim, draw::kCenter | draw::kMiddle | draw::kEllipsis);
        for (size_t i = 0; i < groups.size(); ++i) {
            const int y = int(cy) - int(groups.size()) * lineH / 2 + int(i) * lineH;
            c.text({tx, y, tx + tw, y + lineH}, groups[i]->label, i + 1 == groups.size() ? kText : kDim, draw::kCenter | draw::kMiddle | draw::kEllipsis);
        }
        // The hot item's name on a plate outside the rings, in the item's direction; a hot group with more items than
        // cells: the count of the rest.
        const PickPieNode* hot = hotNode();
        std::wstring label = hot && hot->value >= 0 ? hot->label : std::wstring();
        if (hot && hot->value < 0 && !rs.empty() && rs.back().of == hot && rs.back().items && rs.back().shownItems < int(hot->kids.size()))
            label = std::to_wstring(int(hot->kids.size()) - rs.back().shownItems) + L" " + tr(L"more in the palette");
        if (label.empty()) return;
        const int lw = std::min(width(), c.measure(label) + 4 * pad), lh = scale(28);
        const PointF q = at(hotAngle, outermost + float(scale(10)));
        const double sn = std::sin(hotAngle), cs = std::cos(hotAngle);
        int lx = std::fabs(sn) < 0.35 ? int(q.x) - lw / 2 : sn > 0 ? int(q.x) : int(q.x) - lw;
        int ly = std::fabs(cs) < 0.35 ? int(q.y) - lh / 2 : cs > 0 ? int(q.y) - lh : int(q.y);
        lx = std::clamp(lx, 0, std::max(0, width() - lw)), ly = std::clamp(ly, 0, std::max(0, height() - lh));
        c.roundBox({lx, ly, lx + lw, ly + lh}, kPanel, kAccentLine, float(scale(stardraft::kit::kRadius)));
        c.text({lx + pad, ly, lx + lw - pad, ly + lh}, label, kText, draw::kCenter | draw::kMiddle | draw::kEllipsis);
    }

private:
    int nameWidth_ = 0;  // (the first paint's: the widest group name)
};
