// The Data form's diagrams beside the fields, painted on the Canvas (portable, core.inc, after views_data_pictures.inc):
// a unit's footprint, direction and pathing gap and its ranges; a weapon's damage table, ranges, attack angle and start
// point; a flingy's speed and turn; a sprite's picture with its selection circle and health bar; an SFX entry's sound
// player; an image script's uses and code; and the legends that name their parts. They are the Windows program's
// widgets (main.cpp: widgetFootprint ... widgetIscriptListing) at the same sizes, on the rules of core/data_pictures.inc.
// The SDL program's form draws them through paintDataPictures (the Windows program still draws its GDI widgets). The
// parts that change a value set it while the mouse is held; the program saves it (one undo step) when it is let go.

void dataSetValue(const DatField& f, uint32_t v);  // (the program's: the form's value, saved with its undo step)

namespace datapics {

// A color made lighter (a part whose field is under the mouse).
inline draw::Color lit(draw::Color c, bool on) {
    if (!on) return c;
    return draw::rgb(std::min(255, int(c & 255) + 50), std::min(255, int(c >> 8 & 255) + 50), std::min(255, int(c >> 16 & 255) + 50));
}
// The diagrams' ground (the Windows widgets' canvas(): a dark box with rounded corners).
inline void ground(draw::Canvas& c, draw::Rect r, draw::Color bg = draw::rgb(17, 17, 19)) { c.roundBox(r, bg, kBorder, float(scale(4))); }
// A small square to drag (each one's point is kept until the form's next paint: a snapshot's state line says whether
// each handle has a drag; a handle is only for a part that the mouse drags).
inline std::vector<draw::Point> gHandles;
inline void handle(draw::Canvas& c, int x, int y, draw::Color col) {
    gHandles.push_back({x, y});
    const int h = scale(3);
    c.fill({x - h, y - h, x + h + 1, y + h + 1}, col);
}
inline void ringAt(draw::Canvas& c, int cx, int cy, int rx, int ry, draw::Color col, int width = 1, draw::LineStyle st = draw::LineStyle::Solid) {
    c.ring({cx - rx, cy - ry, cx + rx + 1, cy + ry + 1}, col, width, st);
}
// A range around a box (the game's distance shape; core/data_pictures.inc: rangeShapeAround).
inline void rangeLine(draw::Canvas& c, draw::Rect box, double range, draw::Color col, int width = 1, draw::LineStyle st = draw::LineStyle::Solid) {
    auto pts = rangeShapeAround(box, range);
    if (!pts.empty()) pts.push_back(pts.front());  // (closed: the last point is at the box's top, the first at its bottom)
    c.polyline(pts.data(), int(pts.size()), col, width, st);
}
// A polygon filled with a see-through color, row by row (even-odd), inside `clip`.
inline void fillShape(draw::Canvas& c, const std::vector<draw::Point>& pts, draw::Color col, uint8_t alpha, draw::Rect clip) {
    if (pts.size() < 3) return;
    int top = clip.bottom, bottom = clip.top;
    for (const auto& p : pts) top = std::min(top, p.y), bottom = std::max(bottom, p.y);
    top = std::max(top, clip.top), bottom = std::min(bottom, clip.bottom);
    std::vector<int> xs;
    for (int y = top; y < bottom; ++y) {
        const double sy = y + 0.5;
        xs.clear();
        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
            const draw::Point a = pts[i], b = pts[j];
            if ((a.y > sy) != (b.y > sy)) xs.push_back(int(std::lround(a.x + (sy - a.y) * double(b.x - a.x) / double(b.y - a.y))));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            const int x0 = std::max(xs[k], clip.left), x1 = std::min(xs[k + 1], clip.right);
            if (x1 > x0) c.fill({x0, y, x1, y + 1}, col, alpha);
        }
    }
}

// The game's pictures in the diagrams: each frame made once (core/data_pictures.inc: gameImageSprite) and kept as a
// bitmap (a texture on SDL) until the edits or the art change: then they all go (an edit, or another project that
// opens, makes new ones; the old ones are never drawn again).
struct ImageBitmap {
    draw::Bitmap b;
    int originX = 0, originY = 0, k = 0;
};
inline std::map<std::tuple<int, int, bool, int>, ImageBitmap> gImageBitmaps;
inline std::pair<int, bool> gImageBitmapsOf{-1, false};  // the edits and the art of the bitmaps (dataState().edits, widgetsHd())
inline void drawGameImage(draw::Canvas& c, int image, int frame, bool flip, int owner, int cx, int cy, float zoom, uint8_t alpha = 255) {
    if (!shellState().ed || image < 0) return;
    if (const std::pair<int, bool> of{dataState().edits, widgetsHd()}; of != gImageBitmapsOf || gImageBitmaps.size() > 256)
        gImageBitmaps.clear(), gImageBitmapsOf = of;
    auto [it, fresh] = gImageBitmaps.try_emplace({image, frame, flip, owner});
    ImageBitmap& ib = it->second;
    if (fresh) {
        Sprite s;
        ib.k = gameImageSprite(image, frame, flip, owner, s);
        if (ib.k) spriteToBitmap(s, ib.b), ib.originX = s.originX, ib.originY = s.originY;
    }
    if (!ib.k || ib.b.width() <= 0) return;
    const float z = zoom / float(ib.k);
    const int left = cx - int(ib.originX * z), top = cy - int(ib.originY * z);
    c.bitmap(ib.b, {0, 0, ib.b.width(), ib.b.height()}, {left, top, left + std::max(1, int(ib.b.width() * z)), top + std::max(1, int(ib.b.height() * z))},
             true, alpha);
}
// A unit as the game shows it: its image facing `dir32`, and its turret (subunit) on top.
inline void drawUnitPicture(draw::Canvas& c, int unit, int dir32, int cx, int cy, float zoom, int owner = 0, uint8_t alpha = 255) {
    if (!shellState().ed) return;
    UnitSprites& sp = dataSprites();
    const int image = imageOfUnit(unit);
    if (image < 0) return;
    bool flip = false;
    const int frame = facingFrame(image, sp.script(image).initFrame, dir32, flip);
    drawGameImage(c, image, frame, flip, owner, cx, cy, zoom, alpha);
    const int subUnit = datValue("units", "Subunit 1", unit, 228);
    if (subUnit < 228) {
        const int sub = imageOfUnit(subUnit);
        if (sub >= 0) {
            auto [ox, oy] = sp.specialOffset(image, frame);
            bool sflip = false;
            const int sf = facingFrame(sub, sp.script(sub).initFrame, dir32, sflip);
            drawGameImage(c, sub, sf, sflip, owner, cx + int((flip ? -ox : ox) * zoom), cy + int(oy * zoom), zoom, alpha);
        }
    }
}

// While a part is dragged: the value changes at once (the program saves it when the mouse is let go).
inline void setLive(DataPicturesHost& host, const std::string& dat, const std::string& name, int e, long long v) {
    DatTable* t = datTable(datTabOf(dat));
    const DatField* f = datFieldNamed(dat, name);
    if (!t || !f || !f->covers(e)) return;
    t->set(*f, e, uint32_t(std::clamp<long long>(v, 0, f->maxValue())));
    host.pictureRepaint();
}
inline bool hotAny(DataPicturesHost& host, std::initializer_list<const char*> fields) {
    for (const char* f : fields)
        if (host.fieldHot(f)) return true;
    return false;
}

// Chips that name a picture's parts in their colors; the mouse over one marks its fields and its part.
struct LegendChip {
    draw::Color color;
    const wchar_t* label;
    std::vector<std::string> fields;
    int style = 0;  // 0 a line, 1 a filled square, 2 a dashed line
};
inline int legend(draw::Canvas& c, DataPicturesHost& host, int x, int y, int w, const std::vector<LegendChip>& chips) {
    int cx = x, cy = y;
    const int h = scale(22);
    for (const auto& ch : chips) {
        const int cw = scale(22) + c.measure(ch.label) + scale(10);
        if (cx + cw > x + w && cx > x) cx = x, cy += h + scale(4);
        bool on = false;
        for (const auto& f : ch.fields) on |= host.fieldHot(f);
        const draw::Rect r{cx, cy, cx + cw, cy + h};
        if (on) ground(c, r, draw::rgb(30, 40, 52));
        const draw::Color col = lit(ch.color, on);
        const int sy = cy + h / 2;
        if (ch.style == 1) {
            const draw::Rect sq{cx + scale(6), sy - scale(5), cx + scale(16), sy + scale(5)};
            c.fill(sq, col, 160), c.frame(sq, col);
        } else if (ch.style == 2) {
            c.line(cx + scale(5), sy, cx + scale(17), sy, col, 1, draw::LineStyle::Dot);
        } else {
            c.line(cx + scale(5), sy, cx + scale(17), sy, col, scale(2));
        }
        c.text({cx + scale(22), cy, r.right, r.bottom}, ch.label, on ? kText : kDim, draw::kMiddle);
        host.pictureEdit(r, nullptr, nullptr, L"", draw::Cursor::Arrow, ch.fields);
        cx = r.right + scale(6);
    }
    return cy + h - y;
}

// --- units: footprint (sprite, unit size, placement box, addon), the direction dial, the pathing gap, the ranges -----

inline int footprint(draw::Canvas& c, DataPicturesHost& host, int u, int x, int y, int cw, int ch) {
    const draw::Rect cr{x, y, x + cw, y + ch};
    ground(c, cr);
    const int L = datValue("units", "Unit Size Left", u), U = datValue("units", "Unit Size Up", u);
    const int R = datValue("units", "Unit Size Right", u), D = datValue("units", "Unit Size Down", u);
    const int pw = datValue("units", "StarEdit Placement Box Width", u), ph = datValue("units", "StarEdit Placement Box Height", u);
    const bool isAddon = (datValue("units", "Special Ability Flags", u) & 2) != 0;
    const int parent = isAddon ? addonParent(u) : -1;
    const int ax = datValue("units", "Addon Horizontal (X) Position", u), ay = datValue("units", "Addon Vertical (Y) Position", u);
    const int dirValue = datValue("units", "Unit Direction", u, 12), dir = dirValue >= 32 ? 12 : dirValue;
    int extX = std::max({L, R, pw / 2, 24}), extY = std::max({U, D, ph / 2, 24});
    int px = 0, py = 0, ppw = 0, pph = 0;  // an addon: its building's center, from the addon's
    if (parent >= 0) {
        ppw = datValue("units", "StarEdit Placement Box Width", parent), pph = datValue("units", "StarEdit Placement Box Height", parent);
        px = ppw / 2 - ax - pw / 2, py = pph / 2 - ay - ph / 2;
        extX = std::max(extX, (std::abs(px) + ppw / 2 + pw / 2) / 2 + 8), extY = std::max(extY, (std::abs(py) + pph / 2 + ph / 2) / 2 + 8);
    }
    const float zoom = std::clamp(std::min((cw / 2 - scale(20)) / float(extX), (ch / 2 - scale(20)) / float(extY)), 0.25f, 4.0f * float(shellDpi()) / 96);
    const int cx = (cr.left + cr.right) / 2 - (parent >= 0 ? int(px * zoom / 2) : 0), cy = (cr.top + cr.bottom) / 2 - (parent >= 0 ? int(py * zoom / 2) : 0);
    auto X = [&](double gx) { return cx + int(std::lround(gx * zoom)); };
    auto Y = [&](double gy) { return cy + int(std::lround(gy * zoom)); };
    const draw::Rect in{cr.left + 1, cr.top + 1, cr.right - 1, cr.bottom - 1};
    c.pushClip(in);
    for (int k = -40; k <= 40; ++k) {  // tiles (32 pixels), lined up with the placement box
        const int lx = X(-pw / 2.0 + k * 32), ly = Y(-ph / 2.0 + k * 32);
        if (lx > cr.left && lx < cr.right) c.line(lx, cr.top, lx, cr.bottom, draw::rgb(32, 32, 36));
        if (ly > cr.top && ly < cr.bottom) c.line(cr.left, ly, cr.right, ly, draw::rgb(32, 32, 36));
    }
    const bool hotSize = hotAny(host, {"Unit Size Left", "Unit Size Up", "Unit Size Right", "Unit Size Down"});
    const bool hotPlace = hotAny(host, {"StarEdit Placement Box Width", "StarEdit Placement Box Height"});
    const bool hotAddon = hotAny(host, {"Addon Horizontal (X) Position", "Addon Vertical (Y) Position"});
    const draw::Color place = draw::rgb(171, 242, 0), size = draw::rgb(120, 230, 140), addonC = draw::rgb(240, 120, 100);
    if (parent >= 0) {
        drawUnitPicture(c, parent, 12, X(px), Y(py), zoom, 0, 110);
        c.frame({X(px - ppw / 2.0), Y(py - pph / 2.0), X(px + ppw / 2.0), Y(py + pph / 2.0)}, draw::rgb(90, 90, 96), 1, draw::LineStyle::Dot);
    }
    const draw::Rect pr{X(-pw / 2.0), Y(-ph / 2.0), X(pw / 2.0), Y(ph / 2.0)};
    c.fill(pr, place, hotPlace ? 70 : 35);
    c.frame(pr, lit(place, hotPlace), hotPlace ? scale(2) : 1);
    drawUnitPicture(c, u, dir, cx, cy, zoom);
    const draw::Rect sr{X(-L), Y(-U), X(R + 1), Y(D + 1)};
    c.frame(sr, lit(size, hotSize), hotSize ? scale(2) + 1 : std::max(1, scale(1)));
    const draw::Color edgeHot = draw::rgb(220, 255, 220);  // the edge whose field the mouse is on: brighter and thicker
    if (host.fieldHot("Unit Size Left")) c.line(sr.left, sr.top, sr.left, sr.bottom, edgeHot, scale(3));
    if (host.fieldHot("Unit Size Right")) c.line(sr.right - 1, sr.top, sr.right - 1, sr.bottom, edgeHot, scale(3));
    if (host.fieldHot("Unit Size Up")) c.line(sr.left, sr.top, sr.right, sr.top, edgeHot, scale(3));
    if (host.fieldHot("Unit Size Down")) c.line(sr.left, sr.bottom - 1, sr.right, sr.bottom - 1, edgeHot, scale(3));
    if (parent >= 0) c.frame(pr, lit(addonC, hotAddon), scale(2));
    gridHover(c, host, in, float(cx + (-pw / 2.0) * zoom), float(cy + (-ph / 2.0) * zoom), 32 * zoom, kDataTileCell);
    c.popClip();
    // Handles: the unit size's edges, the placement box's right and bottom edges, the addon.
    const int grab = scale(4);
    const float z = zoom;
    const int ccx = cx, ccy = cy;
    DataPicturesHost* hp = &host;
    if (parent >= 0) {
        const int bx = X(px - ppw / 2.0), by = Y(py - pph / 2.0);  // the building's top left corner
        host.pictureEdit(pr,
                         [hp, u, z, bx, by, pw, ph](draw::Point p) {
                             setLive(*hp, "units", "Addon Horizontal (X) Position", u, std::lround((p.x - bx) / z - pw / 2.0));
                             setLive(*hp, "units", "Addon Vertical (Y) Position", u, std::lround((p.y - by) / z - ph / 2.0));
                         },
                         nullptr, L"Drag to move the addon", draw::Cursor::SizeAll, {"Addon Horizontal (X) Position", "Addon Vertical (Y) Position"});
    }
    auto snap = [](long long v) { return (v + 4) / 8 * 8; };
    host.pictureEdit({pr.right - grab, pr.top + grab, pr.right + grab, pr.bottom - grab},
                     [hp, u, z, ccx, snap](draw::Point p) { setLive(*hp, "units", "StarEdit Placement Box Width", u, snap(std::lround(2 * (p.x - ccx) / z))); },
                     nullptr, L"Drag to change the width", draw::Cursor::SizeWE, {"StarEdit Placement Box Width"});
    host.pictureEdit({pr.left + grab, pr.bottom - grab, pr.right - grab, pr.bottom + grab},
                     [hp, u, z, ccy, snap](draw::Point p) { setLive(*hp, "units", "StarEdit Placement Box Height", u, snap(std::lround(2 * (p.y - ccy) / z))); },
                     nullptr, L"Drag to change the height", draw::Cursor::SizeNS, {"StarEdit Placement Box Height"});
    host.pictureEdit({sr.left - grab, sr.top + grab, sr.left + grab, sr.bottom - grab},
                     [hp, u, z, ccx](draw::Point p) { setLive(*hp, "units", "Unit Size Left", u, std::lround((ccx - p.x) / z)); }, nullptr, L"",
                     draw::Cursor::SizeWE, {"Unit Size Left"});
    host.pictureEdit({sr.right - grab, sr.top + grab, sr.right + grab, sr.bottom - grab},
                     [hp, u, z, ccx](draw::Point p) { setLive(*hp, "units", "Unit Size Right", u, std::lround((p.x - ccx) / z) - 1); }, nullptr, L"",
                     draw::Cursor::SizeWE, {"Unit Size Right"});
    host.pictureEdit({sr.left + grab, sr.top - grab, sr.right - grab, sr.top + grab},
                     [hp, u, z, ccy](draw::Point p) { setLive(*hp, "units", "Unit Size Up", u, std::lround((ccy - p.y) / z)); }, nullptr, L"",
                     draw::Cursor::SizeNS, {"Unit Size Up"});
    host.pictureEdit({sr.left + grab, sr.bottom - grab, sr.right - grab, sr.bottom + grab},
                     [hp, u, z, ccy](draw::Point p) { setLive(*hp, "units", "Unit Size Down", u, std::lround((p.y - ccy) / z) - 1); }, nullptr, L"",
                     draw::Cursor::SizeNS, {"Unit Size Down"});
    return ch;
}

// A unit's starting direction (0..31; 32 random): drag the needle, click the center for random.
inline int direction(draw::Canvas& c, DataPicturesHost& host, int u, int x, int y, int size) {
    const int r = size / 2 - scale(12), cx = x + size / 2, cy = y + size / 2;
    const bool on = host.fieldHot("Unit Direction");
    ground(c, {x, y, x + size, y + size}, on ? draw::rgb(24, 30, 38) : draw::rgb(17, 17, 19));
    ringAt(c, cx, cy, r, r, draw::rgb(60, 60, 66));
    for (int k = 0; k < 32; ++k) {
        const draw::Point a = dialPoint(cx, cy, k / 32.0, r - scale(k % 8 ? 2 : 5)), b = dialPoint(cx, cy, k / 32.0, r);
        const int g = k % 8 ? 60 : 100;
        c.line(a.x, a.y, b.x, b.y, draw::rgb(g, g, g + 6));
    }
    const int v = datValue("units", "Unit Direction", u, 0);
    const draw::Color col = lit(draw::rgb(240, 160, 60), on);
    if (v >= 32) {
        ringAt(c, cx, cy, r - scale(8), r - scale(8), col, 1, draw::LineStyle::Dot);
        c.text({cx - scale(10), cy - scale(10), cx + scale(10), cy + scale(10)}, L"?", col, draw::kCenter | draw::kMiddle);
    } else {
        const draw::Point e = dialPoint(cx, cy, v / 32.0, r);
        c.line(cx, cy, e.x, e.y, col, scale(2));
        handle(c, e.x, e.y, col);
    }
    needsVerification(c, host, {x, y, x + size, y + size}, L"Not checked against OpenBW: that 0 is up and the values go clockwise.");
    DataPicturesHost* hp = &host;
    host.pictureEdit({x, y, x + size, y + size}, [hp, u, cx, cy](draw::Point p) { setLive(*hp, "units", "Unit Direction", u, std::lround(dialTurns(cx, cy, p) * 32) % 32); },
                     nullptr, L"Drag to change the direction", draw::Cursor::Hand, {"Unit Direction"});
    host.pictureEdit({cx - scale(10), cy - scale(10), cx + scale(10), cy + scale(10)}, nullptr,
                     [v] {
                         if (const DatField* f = datFieldNamed("units", "Unit Direction")) dataSetValue(*f, v >= 32 ? 12 : 32);
                     },
                     L"Click for a random direction", draw::Cursor::Hand, {"Unit Direction"});
    return size;
}

// The narrowest gap the unit passes (terrain stops its size box at walk-tile edges, 8 pixels apart).
inline int pathing(draw::Canvas& c, DataPicturesHost& host, int u, int x, int y, int size) {
    const int L = datValue("units", "Unit Size Left", u), U = datValue("units", "Unit Size Up", u);
    const int R = datValue("units", "Unit Size Right", u), D = datValue("units", "Unit Size Down", u);
    const int bw = L + R + 1, bh = U + D + 1, nw = (bw + 7) / 8, nh = (bh + 7) / 8;
    const draw::Rect cr{x, y, x + size, y + size};
    ground(c, cr);
    const int cells = std::max(nw + 4, (bh + 7) / 8 + 4);
    const float z = (size - scale(44)) / float(cells * 8);
    const int ox = cr.left + (size - int(cells * 8 * z)) / 2, oy = cr.top + scale(10);
    const int gap0 = (cells - nw) / 2;
    for (int ty = 0; ty < cells; ++ty)
        for (int tx = 0; tx < cells; ++tx) {
            const draw::Rect t{ox + int(tx * 8 * z), oy + int(ty * 8 * z), ox + int((tx + 1) * 8 * z), oy + int((ty + 1) * 8 * z)};
            if (tx < gap0 || tx >= gap0 + nw) c.fill({t.left + 1, t.top + 1, t.right, t.bottom}, draw::rgb(90, 82, 70), 200);
            else c.frame(t, draw::rgb(32, 32, 36));
        }
    const bool on = hotAny(host, {"Unit Size Left", "Unit Size Up", "Unit Size Right", "Unit Size Down"});
    const int gx = ox + int(gap0 * 8 * z), mid = oy + int(cells * 8 * z / 2);
    const draw::Rect box{gx, mid - int(bh * z / 2), gx + int(bw * z), mid + int((bh - bh / 2) * z)};
    c.fill(box, draw::rgb(120, 230, 140), on ? 90 : 50);
    c.frame(box, lit(draw::rgb(120, 230, 140), on), on ? scale(2) : 1);
    gridHover(c, host, {ox, oy, ox + int(cells * 8 * z), oy + int(cells * 8 * z)}, float(ox), float(oy), 8 * z, L"1 walk tile: 8 x 8 px");
    wchar_t b[96];
    std::swprintf(b, 96, L"gap: %d x %d walk tiles", nw, nh);
    c.text({cr.left + scale(8), cr.bottom - scale(26), cr.right - scale(8), cr.bottom - scale(6)}, b, kDim, draw::kMiddle | draw::kCenter);
    needsVerification(c, host, {cr.left, cr.top, cr.right, cr.bottom - scale(22)},
                      L"Not checked against OpenBW: that a unit needs a gap as wide as its size, rounded up to walk tiles. Pathing can be stricter.");
    wchar_t tip[220];
    std::swprintf(tip, 220,
                  L"The unit size is rounded up to walk tiles (8 pixels each). Going up or down, the unit needs a gap of %d walk tiles (%d pixels). Going left or right, it needs a gap of %d walk tiles (%d pixels).",
                  nw, nw * 8, nh, nh * 8);
    host.pictureEdit(cr, nullptr, nullptr, tip, draw::Cursor::Arrow, {"Unit Size Left", "Unit Size Up", "Unit Size Right", "Unit Size Down"});
    return size;
}

// Ranges around a unit's bounds: sight (the tiles it sees), target acquisition and its weapons.
inline int unitRanges(draw::Canvas& c, DataPicturesHost& host, int u, int x, int y, int cw, int ch) {
    const draw::Rect cr{x, y, x + cw, y + ch};
    ground(c, cr);
    const int L = datValue("units", "Unit Size Left", u), U = datValue("units", "Unit Size Up", u);
    const int R = datValue("units", "Unit Size Right", u), D = datValue("units", "Unit Size Down", u);
    const int sight = datValue("units", "Sight Range", u) * 32, acq = datValue("units", "Target Acquisition Range", u) * 32;
    const int gw = datValue("units", "Ground Weapon", u, 130), aw = datValue("units", "Air Weapon", u, 130);
    const int sub = datValue("units", "Subunit 1", u, 228);  // a turret: its weapons
    const int gwr = gw < 130 ? datValue("weapons", "Maximum Range", gw) : sub < 228 ? datValue("weapons", "Maximum Range", datValue("units", "Ground Weapon", sub, 130), 0) : 0;
    const int awr = aw < 130 ? datValue("weapons", "Maximum Range", aw) : sub < 228 ? datValue("weapons", "Maximum Range", datValue("units", "Air Weapon", sub, 130), 0) : 0;
    const int ext = std::max({sight, acq + std::max(R, D), gwr + std::max(R, D), awr + std::max(R, D), 32});
    const float zoom = (std::min(cw, ch) / 2 - scale(10)) / float(ext);
    const int cx = (cr.left + cr.right) / 2, cy = (cr.top + cr.bottom) / 2;
    auto Z = [&](int v) { return int(std::lround(v * zoom)); };
    const draw::Rect in{cr.left + 1, cr.top + 1, cr.right - 1, cr.bottom - 1};
    c.pushClip(in);
    const draw::Rect bounds{cx - Z(L), cy - Z(U), cx + Z(R + 1), cy + Z(D + 1)};
    for (int k = -20; k <= 20; ++k) {
        const int lx = cx + Z(k * 32), ly = cy + Z(k * 32);
        if (lx > cr.left && lx < cr.right) c.line(lx, cr.top, lx, cr.bottom, draw::rgb(28, 28, 32));
        if (ly > cr.top && ly < cr.bottom) c.line(cr.left, ly, cr.right, ly, draw::rgb(28, 28, 32));
    }
    const bool hs = host.fieldHot("Sight Range"), ha = host.fieldHot("Target Acquisition Range"), hg = host.fieldHot("Ground Weapon"),
               hair = host.fieldHot("Air Weapon");
    if (sight) {  // the tiles the game shows: its sight mask around the unit's tile
        int n = 0;
        const auto mask = sightMask(sight / 32, n);
        for (int ty = 0; ty < n; ++ty)
            for (int tx = 0; tx < n; ++tx)
                if (mask[size_t(ty * n + tx)]) {
                    const int lx = cx + Z((tx - n / 2) * 32 - 16), ly = cy + Z((ty - n / 2) * 32 - 16);
                    c.fill({lx + 1, ly + 1, lx + Z(32), ly + Z(32)}, draw::rgb(190, 190, 196), hs ? 60 : 26);
                }
    }
    if (acq) rangeLine(c, bounds, acq * zoom, lit(draw::rgb(240, 180, 70), ha), ha ? scale(3) : 1);
    if (gwr) rangeLine(c, bounds, gwr * zoom, lit(draw::rgb(235, 90, 80), hg), hg ? scale(3) : scale(2));
    // The air range on top of the ground range; the same range: blue dashes over the red line, as wide as it.
    if (awr)
        rangeLine(c, bounds, awr * zoom, lit(draw::rgb(90, 170, 240), hair), hair ? scale(3) : scale(2),
                  awr == gwr && !hair ? draw::LineStyle::Dash : draw::LineStyle::Solid);
    drawUnitPicture(c, u, 12, cx, cy, std::max(zoom, 0.5f));
    gridHover(c, host, in, float(cx), float(cy), 32 * zoom, kDataTileCell);
    c.popClip();
    return ch;
}

// --- weapons: the damage table, the ranges with the splash, the attack angle and the start point ----------------------

inline int gDamageArmor = 0;  // the target's armor in the damage table

inline int damage(draw::Canvas& c, DataPicturesHost& host, int wpn, int x, int y, int w) {
    const int upgrade = datValue("weapons", "Damage Upgrade", wpn, 61);
    const int levels = std::clamp(upgrade < 61 ? datValue("upgrades", "Max. Repeats", upgrade, 3) : 0, 0, 6);
    const int hits = std::max(1, datValue("weapons", "Damage Factor", wpn, 1)), cooldown = std::max(1, datValue("weapons", "Weapon Cooldown", wpn, 1));
    const int rowH = scale(28), headH = scale(26), labelW = scale(70), cellW = std::max(scale(84), std::min((w - labelW) / 3, scale(130)));
    const int cw = labelW + 3 * cellW, ch = headH + (levels + 1) * rowH + scale(44);
    const draw::Rect cr{x, y, x + cw, y + ch};
    ground(c, cr);
    const wchar_t* sizes[3] = {L"Small", L"Medium", L"Large"};
    for (int s = 0; s < 3; ++s)
        c.text({x + labelW + s * cellW, y + scale(4), x + labelW + (s + 1) * cellW - scale(10), y + headH}, sizes[s], kDim, draw::kMiddle | draw::kRight);
    const double top = hitDamage(wpn, levels, 3, 0) * hits;
    const bool hd = hotAny(host, {"Damage Amount", "Damage Bonus", "Damage Factor", "Weapon Type"});
    for (int lv = 0; lv <= levels; ++lv) {
        const int ry = y + headH + lv * rowH;
        c.text({x + scale(10), ry, x + labelW, ry + rowH}, lv ? L"+" + std::to_wstring(lv) : std::wstring(L"base"), kDim, draw::kMiddle);
        for (int s = 0; s < 3; ++s) {
            const double d = hitDamage(wpn, lv, s + 1, gDamageArmor) * hits;
            const draw::Rect cell{x + labelW + s * cellW + scale(4), ry + scale(3), x + labelW + (s + 1) * cellW - scale(4), ry + rowH - scale(3)};
            c.fill({cell.left, cell.top, cell.left + int(cell.width() * std::min(1.0, d / std::max(top, 0.5))), cell.bottom}, draw::rgb(235, 90, 80), hd ? 90 : 55);
            wchar_t b[48];
            std::swprintf(b, 48, L"%g", d);
            c.text({cell.left, cell.top, cell.right - scale(6), cell.bottom}, b, kText, draw::kMiddle | draw::kRight);
            wchar_t tip[160];
            std::swprintf(tip, 160, L"%g per attack (%d hit%ls). About %.1f per second.", d, hits, hits == 1 ? L"" : L"s", d * 24.0 / cooldown);
            host.pictureEdit(cell, nullptr, nullptr, tip, draw::Cursor::Arrow, {"Damage Amount", "Damage Bonus", "Damage Factor", "Weapon Type"});
        }
    }
    // The target's armor: - and +.
    const int ay = cr.bottom - scale(36), bs = scale(26);
    c.text({x + scale(10), ay, x + labelW + scale(40), ay + bs}, L"armor " + std::to_wstring(gDamageArmor), kDim, draw::kMiddle);
    int bx = x + labelW + scale(48);
    DataPicturesHost* hp = &host;
    glyphButton(c, host, {bx, ay, bx + bs, ay + bs}, UiIcon::Remove, false, [hp] { gDamageArmor = std::max(0, gDamageArmor - 1), hp->pictureRepaint(); }, L"Less armor");
    bx += bs + scale(6);
    glyphButton(c, host, {bx, ay, bx + bs, ay + bs}, UiIcon::Add, false, [hp] { gDamageArmor = std::min(20, gDamageArmor + 1), hp->pictureRepaint(); }, L"More armor");
    return ch;
}

inline int weaponRanges(draw::Canvas& c, DataPicturesHost& host, int wpn, int x, int y, int cw, int ch, bool dragging) {
    const draw::Rect cr{x, y, x + cw, y + ch};
    ground(c, cr);
    const int minR = datValue("weapons", "Minimum Range", wpn), maxR = datValue("weapons", "Maximum Range", wpn);
    const int s1 = datValue("weapons", "Inner Splash Range", wpn), s2 = datValue("weapons", "Medium Splash Range", wpn),
              s3 = datValue("weapons", "Outer Splash Range", wpn);
    const int L = 16, U = 16, R = 15, D = 15;  // a plain 32 x 32 box: ranges are measured from its edges
    const int spanX = L + R + 2 * maxR + std::max(s3, 8), spanY = std::max(U + D + 2 * maxR, 2 * s3);
    // While a range is dragged, the scale and the unit's place stay as they were when the drag began (else the
    // picture would grow under the mouse); they fit again when the drag ends.
    static struct {
        bool on = false;
        int wpn = -1;
        float zoom = 1;
        int dx = 0, dy = 0;
    } frozen;
    if (frozen.on && (!dragging || frozen.wpn != wpn)) frozen.on = false;
    float zoom = std::min((cw - scale(24)) / float(std::max(spanX, 32)), (ch - scale(20)) / float(std::max(spanY, 32)));
    if (frozen.on) zoom = frozen.zoom;
    auto Z = [&](int v) { return int(std::lround(v * zoom)); };
    int ux = cr.left + (cw - Z(spanX)) / 2 + Z(maxR + L), cy = (cr.top + cr.bottom) / 2 + Z(U - D) / 2;
    if (frozen.on) ux = cr.left + frozen.dx, cy = cr.top + frozen.dy;
    const draw::Rect in{cr.left + 1, cr.top + 1, cr.right - 1, cr.bottom - 1};
    c.pushClip(in);
    for (int k = -20; k <= 20; ++k) {
        const int lx = ux + Z(k * 32), ly = cy + Z(k * 32);
        if (lx > cr.left && lx < cr.right) c.line(lx, cr.top, lx, cr.bottom, draw::rgb(28, 28, 32));
        if (ly > cr.top && ly < cr.bottom) c.line(cr.left, ly, cr.right, ly, draw::rgb(28, 28, 32));
    }
    const draw::Rect bounds{ux - Z(L), cy - Z(U), ux + Z(R + 1), cy + Z(D + 1)};
    const bool hmin = host.fieldHot("Minimum Range"), hmax = host.fieldHot("Maximum Range");
    if (minR) rangeLine(c, bounds, minR * zoom, lit(draw::rgb(150, 150, 158), hmin), hmin ? scale(3) : 1, hmin ? draw::LineStyle::Solid : draw::LineStyle::Dot);
    rangeLine(c, bounds, maxR * zoom, lit(draw::rgb(235, 90, 80), hmax), hmax ? scale(3) : scale(2));
    c.frame(bounds, draw::rgb(130, 130, 138), 1, draw::LineStyle::Dot);
    const int tx = bounds.right + Z(maxR);
    const draw::Color splash[3] = {draw::rgb(255, 140, 100), draw::rgb(225, 100, 80), draw::rgb(180, 75, 65)};
    const int radius[3] = {s3, s2, s1};
    const char* names[3] = {"Outer Splash Range", "Medium Splash Range", "Inner Splash Range"};
    for (int k = 0; k < 3; ++k)
        if (radius[k]) {  // splash reaches the units whose box is within the radius of the hit (OpenBW unit_distance_to)
            const bool on = host.fieldHot(names[k]);
            fillShape(c, rangeShapeAround({tx, cy, tx, cy}, radius[k] * zoom), splash[k], on ? 110 : 45, in);
            rangeLine(c, {tx, cy, tx, cy}, radius[k] * zoom, lit(splash[k], on), on ? scale(2) : 1);
        }
    c.line(tx - scale(5), cy, tx + scale(5), cy, draw::rgb(240, 240, 240));
    c.line(tx, cy - scale(5), tx, cy + scale(5), draw::rgb(240, 240, 240));
    gridHover(c, host, in, float(ux), float(cy), 32 * zoom, kDataTileCell);
    // Handles to drag: the unit's ranges straight above it, the splash above the target.
    struct Drag {
        const char* field;
        int value, x, from;  // from: the y where the range is 0
        draw::Color color;
    };
    std::vector<Drag> drags = {{"Maximum Range", maxR, ux, bounds.top, draw::rgb(235, 90, 80)},
                               {"Minimum Range", minR, ux - Z(8), bounds.top, draw::rgb(150, 150, 158)}};
    for (int k = 0; k < 3; ++k) drags.push_back({names[k], radius[k], tx + (k - 1) * Z(6), cy, splash[k]});
    const int g = scale(7);
    c.popClip();
    DataPicturesHost* hp = &host;
    for (const auto& d : drags) {
        const int hy = d.from - Z(d.value);
        handle(c, d.x, hy, lit(d.color, host.fieldHot(d.field)));
        const float z = zoom;
        const int from = d.from, dx = ux - cr.left, dy = cy - cr.top;
        const std::string field = d.field;
        host.pictureEdit({d.x - g, hy - g, d.x + g, hy + g},
                         [hp, wpn, field, from, z, dx, dy](draw::Point p) {
                             frozen.on = true, frozen.wpn = wpn, frozen.zoom = z, frozen.dx = dx, frozen.dy = dy;
                             setLive(*hp, "weapons", field, wpn, std::max(0L, std::lround((from - p.y) / z)));
                         },
                         nullptr, L"Drag to change: " + widen(d.field), draw::Cursor::SizeNS, {d.field});
    }
    return ch;
}

inline int weaponDial(draw::Canvas& c, DataPicturesHost& host, int wpn, int x, int y, int size) {
    const int r = size / 2 - scale(14), cx = x + size / 2, cy = y + size / 2;
    ground(c, {x, y, x + size, y + size});
    ringAt(c, cx, cy, r, r, draw::rgb(60, 60, 66));
    const int angle = datValue("weapons", "Attack Angle", wpn), spin = datValue("weapons", "Launch Spin", wpn);
    const bool ha = host.fieldHot("Attack Angle"), hs = host.fieldHot("Launch Spin");
    const double half = angle / 256.0;
    const draw::Color cone = lit(draw::rgb(120, 220, 120), ha), spinC = lit(draw::rgb(235, 90, 80), hs);
    {
        std::vector<draw::Point> pts{{cx, cy}};
        for (int k = 0; k <= 48; ++k) pts.push_back(dialPoint(cx, cy, -half + 2 * half * k / 48, r));
        fillShape(c, pts, cone, ha ? 110 : 60, {cx - r, cy - r, cx + r, cy + r});
        const draw::Point a = dialPoint(cx, cy, -half, r), b = dialPoint(cx, cy, half, r);
        c.line(cx, cy, a.x, a.y, cone, ha ? scale(2) : 1);
        c.line(cx, cy, b.x, b.y, cone, ha ? scale(2) : 1);
        handle(c, b.x, b.y, cone);
    }
    const double s = spin / 256.0;
    const draw::Point l = dialPoint(cx, cy, -s, r * 0.72), rr = dialPoint(cx, cy, s, r * 0.72);
    c.line(cx, cy, l.x, l.y, spinC, scale(2));
    c.line(cx, cy, rr.x, rr.y, spinC, scale(2));
    handle(c, rr.x, rr.y, spinC);
    c.line(cx, cy - r - scale(6), cx, cy - r + scale(2), draw::rgb(200, 200, 205), scale(2));  // the unit faces up (the small mark)
    needsVerification(c, host, {x, y, x + size, y + size}, L"Not checked against OpenBW: that the attack angle and the launch spin go to each side of the front.");
    const int g = scale(9);
    auto toValue = [cx, cy](draw::Point p) {
        double t = dialTurns(cx, cy, p);
        if (t > 0.5) t = 1 - t;  // either side of the front
        return std::lround(t * 256);
    };
    DataPicturesHost* hp = &host;
    host.pictureEdit({x, y, x + size, y + size}, [hp, wpn, toValue](draw::Point p) { setLive(*hp, "weapons", "Attack Angle", wpn, toValue(p)); }, nullptr,
                     L"Drag to change the attack angle", draw::Cursor::Hand, {"Attack Angle"});
    host.pictureEdit({rr.x - g, rr.y - g, rr.x + g, rr.y + g}, [hp, wpn, toValue](draw::Point p) { setLive(*hp, "weapons", "Launch Spin", wpn, toValue(p)); },
                     nullptr, L"Drag to change the launch spin", draw::Cursor::Hand, {"Launch Spin"});
    return size;
}

inline int weaponOffsets(draw::Canvas& c, DataPicturesHost& host, int wpn, int x, int y, int cw, int ch) {
    const draw::Rect cr{x, y, x + cw, y + ch};
    ground(c, cr);
    const int fwd = datValue("weapons", "Forward Offset", wpn), up = datValue("weapons", "Upward Offset", wpn);
    const float zoom = 2.0f * float(shellDpi()) / 96;
    const int cx = cr.left + cw * 2 / 5, cy = cr.top + ch / 2 + scale(10);
    c.pushClip({cr.left + 1, cr.top + 1, cr.right - 1, cr.bottom - 1});
    c.line(cr.left, cy, cr.right, cy, draw::rgb(40, 40, 44));
    c.line(cx, cr.top, cx, cr.bottom, draw::rgb(40, 40, 44));
    // The center of the one that fires, facing right, in a plain box.
    c.frame({cx - int(16 * zoom), cy - int(16 * zoom), cx + int(16 * zoom), cy + int(16 * zoom)}, draw::rgb(90, 90, 98), 1, draw::LineStyle::Dot);
    ringAt(c, cx, cy, scale(2), scale(2), draw::rgb(150, 150, 158));
    c.line(cx + int(16 * zoom), cy, cx + int(16 * zoom) + scale(8), cy, draw::rgb(110, 110, 118));
    const int px = cx + int(fwd * zoom), py = cy - int(up * zoom);
    const bool on = hotAny(host, {"Forward Offset", "Upward Offset"});
    const draw::Color col = lit(draw::rgb(255, 210, 80), on);
    if (host.fieldHot("Forward Offset")) c.line(cx, py, px, py, col, scale(2));
    if (host.fieldHot("Upward Offset")) c.line(px, cy, px, py, col, scale(2));
    c.line(px - scale(8), py, px + scale(8), py, col);
    c.line(px, py - scale(8), px, py + scale(8), col);
    ringAt(c, px, py, scale(4), scale(4), col, on ? scale(2) : 1);
    c.popClip();
    DataPicturesHost* hp = &host;
    host.pictureEdit(cr,
                     [hp, wpn, cx, cy, zoom](draw::Point p) {
                         setLive(*hp, "weapons", "Forward Offset", wpn, std::lround((p.x - cx) / zoom));
                         setLive(*hp, "weapons", "Upward Offset", wpn, std::lround((cy - p.y) / zoom));
                     },
                     nullptr, L"Drag to move the point where the graphic starts", draw::Cursor::SizeAll, {"Forward Offset", "Upward Offset"});
    needsVerification(c, host, cr, L"Not checked against OpenBW: how the game uses the forward and upward offsets.");
    return ch;
}

// --- flingy: speed over time, and the path of a half turn at top speed (as OpenBW moves a flingy) ---------------------

inline int speed(draw::Canvas& c, DataPicturesHost& host, int fl, int x, int y, int cw, int ch) {
    const int speed = datValue("flingy", "Speed", fl), accel = datValue("flingy", "Acceleration", fl);
    const int halt = datValue("flingy", "Halt Distance", fl), control = datValue("flingy", "Movement Control", fl);
    const draw::Rect g{x, y, x + cw, y + ch};
    ground(c, g);
    // Each frame the speed goes up by the acceleration, to the top speed; it goes down by the same amount when what is
    // left of the move is at or below the halt distance. A move of 8 tiles (256 pixels).
    std::vector<double> v;
    double brakeFrom = -1;
    {
        double s = 0, d = 0;
        const double top = speed / 256.0, a = accel / 256.0, h = halt / 256.0, total = 256;
        for (int f = 0; f < 600 && d < total; ++f) {
            const double hd = s >= top - 1e-9 ? h : a > 0 ? s * s / (2 * a) : 0;
            const bool brake = control == 0 && s > 0 && total - d <= hd;
            if (brake && brakeFrom < 0) brakeFrom = f;
            s = control == 2 ? top : std::clamp(s + (brake ? -a : a), 0.0, top);
            if (s <= 0 && brake) s = std::min(a, total - d);
            if (s <= 0) break;
            d += s;
            v.push_back(s);
        }
    }
    const int n = int(v.size()), gx0 = g.left + scale(14), gx1 = g.right - scale(14), gy0 = g.top + scale(14), gy1 = g.bottom - scale(36);
    const double top = std::max(speed / 256.0, 0.01);
    auto X = [&](double f) { return gx0 + int((gx1 - gx0) * f / std::max(1, n - 1)); };
    auto Y = [&](double s) { return gy1 - int((gy1 - gy0) * s / top); };
    c.line(gx0, gy1, gx1, gy1, draw::rgb(50, 50, 56));
    int toTop = 0;
    while (toTop < n && v[size_t(toTop)] < top - 1e-9) ++toTop;
    const bool hs = host.fieldHot("Speed"), ha = host.fieldHot("Acceleration"), hh = host.fieldHot("Halt Distance");
    if (brakeFrom >= 0) c.fill({X(brakeFrom), gy0, gx1, gy1}, draw::rgb(240, 180, 70), hh ? 60 : 25);  // the braking part, shaded
    c.line(gx0, Y(top), gx1, Y(top), lit(draw::rgb(120, 200, 240), hs), 1, draw::LineStyle::Dot);  // the top speed
    if (n > 1) {
        std::vector<draw::Point> pts;
        for (int i = 0; i < n; ++i) pts.push_back({X(i), Y(v[size_t(i)])});
        c.polyline(pts.data(), int(pts.size()), draw::rgb(120, 200, 240), scale(2));
        if (ha && toTop > 0) c.polyline(pts.data(), std::min(toTop + 1, n), draw::rgb(160, 230, 255), scale(4));  // the rise: acceleration
    }
    wchar_t b[128];
    std::swprintf(b, 128, L"%d frames to top speed  \x00B7  %.2f px/frame  \x00B7  %d frames for 8 tiles", toTop, speed / 256.0, n);
    c.text({gx0, gy1 + scale(6), gx1, g.bottom - scale(4)}, b, kDim, draw::kMiddle | draw::kEllipsis);
    return ch;
}

inline int turn(draw::Canvas& c, DataPicturesHost& host, int fl, int x, int y, int cw, int ch) {
    const int speed = datValue("flingy", "Speed", fl), turnRadius = datValue("flingy", "Turn Radius", fl), control = datValue("flingy", "Movement Control", fl);
    const draw::Rect p{x, y, x + cw, y + ch};
    ground(c, p);
    // The heading turns by the turn radius each frame (256ths of a turn), the direction of movement by half of it
    // (flingy.dat control); at top speed that draws an arc.
    double px = 0, py = 0, dirTurns = 0.25;  // starts to the right
    const double step = control == 2 ? turnRadius / 256.0 : turnRadius / 512.0, s = std::max(speed / 256.0, 0.05);
    std::vector<std::pair<double, double>> path{{0, 0}};
    for (int k = 0; k < 6; ++k) px += s, path.push_back({px, py});
    int frames = 0;
    for (; frames < 2000 && dirTurns < 0.75 && step > 0; ++frames) {
        px += std::sin(dirTurns * 2 * kPi) * s, py -= std::cos(dirTurns * 2 * kPi) * s;
        dirTurns = std::min(0.75, dirTurns + step);
        path.push_back({px, py});
    }
    for (int k = 1; k <= 6; ++k) path.push_back({px + std::sin(dirTurns * 2 * kPi) * s * k, py - std::cos(dirTurns * 2 * kPi) * s * k});
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (auto [a, b2] : path) minX = std::min(minX, a), maxX = std::max(maxX, a), minY = std::min(minY, b2), maxY = std::max(maxY, b2);
    const int ax0 = p.left + scale(14), ax1 = p.right - scale(14), ay0 = p.top + scale(30), ay1 = p.bottom - scale(36);
    const double zoom = std::min((ax1 - ax0) / std::max(1.0, maxX - minX), (ay1 - ay0) / std::max(1.0, maxY - minY));
    const int ox = ax0 + (ax1 - ax0 - int((maxX - minX) * zoom)) / 2, oy = ay0 + (ay1 - ay0 - int((maxY - minY) * zoom)) / 2;
    std::vector<draw::Point> pts;
    for (auto [a, b2] : path) pts.push_back({ox + int((a - minX) * zoom), oy + int((b2 - minY) * zoom)});
    for (int k = 0; k <= 40; ++k) {  // tiles, for scale
        const int ly = oy + int((k * 32 - minY) * zoom);
        if (ly > ay0 && ly < ay1) c.line(ax0, ly, ax1, ly, draw::rgb(28, 28, 32));
    }
    const bool ht = host.fieldHot("Turn Radius");
    c.polyline(pts.data(), int(pts.size()), lit(draw::rgb(240, 180, 70), ht), ht ? scale(3) : scale(2));
    // The end of the path: a round dot, not a handle (the path is not dragged: Turn Radius is typed in its field, as on
    // Windows).
    if (!pts.empty()) {
        const int r = scale(3), ex = pts.back().x, ey = pts.back().y;
        c.ellipse({ex - r, ey - r, ex + r + 1, ey + r + 1}, lit(draw::rgb(240, 180, 70), ht), draw::kNone);
    }
    wchar_t b[128];
    if (step > 0) std::swprintf(b, 128, L"turns back in a curve %.1f tiles wide, in %d frames", (maxY - minY) / 32, frames);
    else std::swprintf(b, 128, L"does not turn");
    c.text({ax0, ay1 + scale(6), ax1, p.bottom - scale(4)}, b, kDim, draw::kMiddle | draw::kEllipsis);
    // What the picture is: a caption at the top, and a ring where the path starts.
    c.text({p.left + scale(10), p.top + scale(6), p.right - scale(10), p.top + scale(24)}, L"The path when the unit turns back at top speed", kDim,
           draw::kLeft | draw::kEllipsis);
    if (!pts.empty()) ringAt(c, pts.front().x, pts.front().y, scale(4), scale(4), lit(draw::rgb(240, 180, 70), ht));
    host.pictureEdit(p, nullptr, nullptr,
                     L"The path of this unit when it moves right at top speed and turns back (a half turn). The ring is where it starts, the "
                     L"dot where it moves away. A larger Turn Radius turns faster: the curve is tighter.",
                     draw::Cursor::Arrow, {"Turn Radius"});
    return ch;
}

// --- sprites: the image with its selection circle and health bar -------------------------------------------------------

inline int sprite(draw::Canvas& c, DataPicturesHost& host, int s, int x, int y, int size) {
    const draw::Rect cr{x, y, x + size, y + size};
    ground(c, cr);
    const int image = datValue("sprites", "Image File", s, -1);
    const Grp* ig = shellState().ed && image >= 0 ? dataSprites().grp(image) : nullptr;
    const float zoom = ig && ig->width ? std::clamp((size * 0.55f) / std::max(ig->width, ig->height), 0.5f, 3.0f * float(shellDpi()) / 96) : 1.5f;
    const int cx = x + size / 2, cy = y + size / 2 - scale(14);
    c.pushClip({cr.left + 1, cr.top + 1, cr.right - 1, cr.bottom - 1});
    const DatField* cf = datFieldNamed("sprites", "Sel.Circle Image");
    const bool hasCircle = cf && cf->covers(s);
    const int circle = datValue("sprites", "Sel.Circle Image", s, -1), offset = datValue("sprites", "Sel.Circle Offset", s);
    const int bar = datValue("sprites", "Health Bar", s);
    const bool hc = hotAny(host, {"Sel.Circle Image", "Sel.Circle Offset"}), hb = host.fieldHot("Health Bar");
    if (hasCircle && circle >= 0) {
        drawGameImage(c, circle + 561, 0, false, 0, cx, cy + int(offset * zoom), zoom, hc ? 255 : 200);
        if (host.fieldHot("Sel.Circle Offset")) c.line(cx, cy, cx, cy + int(offset * zoom), draw::rgb(255, 210, 80), scale(2));
    }
    if (shellState().ed && image >= 0) {
        UnitSprites& sp = dataSprites();
        bool flip = false;
        const int frame = facingFrame(image, sp.script(image).initFrame, 12, flip);
        drawGameImage(c, image, frame, flip, 0, cx, cy, zoom);
        if (hasCircle && bar > 0) {  // under the circle, `bar` pixels wide in boxes of 3
            const Grp* cg = circle >= 0 ? sp.grp(circle + 561) : nullptr;
            const int top = cy + int((offset + (cg ? cg->height / 2 : 8) + 4) * zoom);
            const int boxes = std::max(1, (bar - 1) / 3), left = cx - int(boxes * 3 * zoom / 2);
            for (int k = 0; k < boxes; ++k)
                c.fill({left + int(k * 3 * zoom), top, left + int((k * 3 + 2) * zoom), top + int(4 * zoom)}, hb ? draw::rgb(120, 255, 120) : draw::rgb(36, 180, 36));
        }
    }
    c.popClip();
    needsVerification(c, host, cr, L"Not checked against OpenBW: the selection circle image (the value plus 561), its offset, and the health bar boxes of 3 pixels.");
    return size;
}

// --- sfxdata: the sound's player (play / pause, its waveform with what has played lit, the time) and the volume --------

inline int volumeRow(draw::Canvas& c, DataPicturesHost& host, int x, int y, int labelW, int right) {
    const int rowH = scale(stardraft::kit::kRowH), cy = y + rowH / 2;
    c.text({x, y, x + labelW - scale(48), y + rowH}, L"Volume", kDim, draw::kMiddle);
    c.text({x + labelW - scale(56), y, x + labelW - scale(4), y + rowH}, std::to_wstring(gDock.volume) + L"%", kText, draw::kMiddle | draw::kRight);
    const int x0 = x + labelW + scale(8), x1 = std::max(x0 + scale(60), right - scale(8));
    const draw::Rect area{x0 - scale(8), y, x1 + scale(8), y + rowH};
    kitpaint::drawSliderFace(c, x0, x1, cy, float(gDock.volume) / 100.0f, over(host, area));
    DataPicturesHost* hp = &host;
    host.pictureArea(area, [] { saveDock(); },
                     [hp, x0, x1](draw::Point pt) {
                         const int v = std::clamp(int(std::lround(float(pt.x - x0) * 100.0f / float(std::max(1, x1 - x0)))), 0, 100);
                         if (v != gDock.volume) gDock.volume = v, saveDock(), hp->pictureRepaint();
                     },
                     L"The volume of every sound StarDraft 2 plays, from the next sound on", draw::Cursor::Hand);
    return rowH;
}

inline int soundPlayer(draw::Canvas& c, DataPicturesHost& host, int sfx, int x, int y, int w, bool& moving) {
    std::vector<float> peaks;
    double seconds = 0;
    const bool readable = host.sfxWave(sfx, peaks, seconds);
    const std::wstring path = sfxPath(sfx);
    const int h = scale(84), cw = std::min(w, scale(640));
    const draw::Rect cr{x, y, x + cw, y + h};
    ground(c, cr);
    bool playing = false;
    const double played = host.sfxPlayed(sfx, playing);
    moving |= playing;
    const int bs = scale(44);
    DataPicturesHost* hp = &host;
    glyphButton(c, host, {cr.left + scale(14), cr.top + (h - bs) / 2, cr.left + scale(14) + bs, cr.top + (h + bs) / 2}, playing ? UiIcon::Pause : UiIcon::Play, false,
                [hp, sfx] { hp->sfxToggle(sfx), hp->pictureRepaint(); }, L"Play or pause");
    const int wx0 = cr.left + scale(14) + bs + scale(16), wx1 = cr.right - scale(110), mid = (cr.top + cr.bottom) / 2, amp = h / 2 - scale(12);
    if (readable && !peaks.empty()) {
        const int n = int(peaks.size());
        for (int i = 0; i < n; ++i) {
            const int lx = wx0 + (wx1 - wx0) * i / n, a = std::max(1, int(peaks[size_t(i)] * amp));
            const bool done = i < int(played * n);
            c.line(lx, mid - a, lx, mid + a + 1, done ? kAccent : draw::rgb(70, 76, 86), std::max(1, (wx1 - wx0) / n - 1));
        }
    } else {
        c.text({wx0, cr.top, wx1, cr.bottom}, path.empty() ? L"no file" : L"Could not read the sound file", kDim, draw::kMiddle);
    }
    wchar_t b[64];
    std::swprintf(b, 64, L"%.2f / %.2f s", played * seconds, seconds);
    c.text({wx1 + scale(10), cr.top, cr.right - scale(12), cr.bottom}, b, kDim, draw::kMiddle | draw::kRight);
    return h + scale(6) + volumeRow(c, host, x, y + h + scale(6), scale(120), x + cw);  // the volume, under the player
}

}  // namespace datapics

// --- images: what the script uses, and its code ------------------------------------------------------------------------

// The name of what an argument of the script points at (a sound: its file).
std::wstring iscriptRefName(const std::string& ref, int v) {
    if (ref == "sfxdata") {
        std::wstring p = sfxPath(v);
        if (const size_t s = p.find_last_of(L"\\/"); s != std::wstring::npos) p = p.substr(s + 1);
        return p.empty() ? entryName("sfxdata", v) : p;
    }
    return entryName(ref, v);
}

void dataGoTo(const std::string& dat, int e);  // (the program's: the entry in its tab)

// What the image's script uses, in all its animations: the images (overlays, shadows), sprites, sounds and weapons it
// names, each a link to it. Returns its height (0: none).
int paintIscriptUses(draw::Canvas& c, DataPicturesHost& host, int image, int x, int y, int w) {
    using namespace datapics;
    if (!shellState().ed) return 0;
    UnitSprites& sp = dataSprites();
    static struct {
        int image = -1, edits = -1;
        std::vector<std::pair<std::string, int>> refs;  // in the order they come, each once
    } cache;
    if (cache.image != image || cache.edits != dataState().edits) {
        cache.image = image, cache.edits = dataState().edits, cache.refs.clear();
        std::set<std::pair<std::string, int>> seen;
        for (int a = 0; a < 28; ++a)
            if (const size_t start = sp.animOffset(image, a))
                for (const auto& l : disassembleIscript(sp.iscript(), start, 400))
                    for (const auto& arg : l.argv)
                        if (!arg.ref.empty() && arg.ref != "label" && seen.insert({arg.ref, arg.value}).second) cache.refs.push_back({arg.ref, arg.value});
        std::stable_sort(cache.refs.begin(), cache.refs.end(), [](const auto& p1, const auto& p2) { return p1.first < p2.first; });
    }
    if (cache.refs.empty()) return 0;
    const int rowH = scale(22);
    int cx = x, cy = y;
    std::string group;
    auto put = [&](const std::wstring& text, std::function<void()> click, const std::wstring& tip, bool isLink) {
        const int tw = c.measure(text);
        if (cx > x && cx + tw > x + w) cx = x + scale(16), cy += rowH;
        const draw::Rect r{cx, cy, cx + tw, cy + rowH};
        if (isLink) {
            kitpaint::drawLinkFace(c, r, text, kitpaint::Tone::Primary, over(host, r));
            host.pictureArea(r, std::move(click), nullptr, tip, draw::Cursor::Hand);
        } else {
            c.text(r, text, kDim, draw::kMiddle);
        }
        cx += tw + scale(10);
    };
    static const std::map<std::string, const wchar_t*> titles = {{"images", L"Images"}, {"sprites", L"Sprites"}, {"sfxdata", L"Sounds"}, {"weapons", L"Weapons"}};
    for (const auto& [dat, v] : cache.refs) {
        if (dat != group) {
            if (!group.empty()) cx = x, cy += rowH;
            group = dat;
            put(titles.count(dat) ? std::wstring(titles.at(dat)) : widen(dat), nullptr, L"", false);
        }
        std::wstring name = iscriptRefName(dat, v);
        if (name.empty()) name = L"#" + std::to_wstring(v);
        const std::string d = dat;
        const int value = v;
        put(name, [d, value] { dataGoTo(d, value); }, L"#" + std::to_wstring(v) + L": open it", true);
    }
    return cy + rowH - y;
}

// The animation's code: labels, opcodes and their arguments, with the names of what they point at (click one to open
// it; a label: the listing goes there). While the image plays, the listing follows the instruction it stopped on;
// "Follow" goes back to that after a label was clicked. Returns its height (0: none).
struct IscriptListingState {
    size_t start = 0;
    std::vector<IscriptLine> lines;
    int first = -1;  // the first line shown when it does not follow (-1: it follows)
    int shownFirst = 0, shown = 0;
};
inline IscriptListingState gIscriptListing;

int paintIscriptListing(draw::Canvas& c, DataPicturesHost& host, int image, int x, int y, int w) {
    using namespace datapics;
    if (!shellState().ed) return 0;
    UnitSprites& sp = dataSprites();
    IscriptListingState& g = gIscriptListing;
    const size_t start = sp.animOffset(image, gImagePlayer.anim);
    if (!start) return 0;
    if (g.start != start || g.lines.empty()) g.start = start, g.lines = disassembleIscript(sp.iscript(), start, 400), g.first = -1;
    const auto& lines = g.lines;
    const int id = sp.scriptId(image);  // the script's name over it: iscript ID and its name
    const auto names = DatSchema::entryNames("iscript");
    const std::wstring head = L"Iscript " + std::to_wstring(id) + (id >= 0 && size_t(id) < names.size() ? L"  \x00B7  " + widen(names[size_t(id)]) : L"") +
                              L"  \x00B7  " + kAnimNames[std::clamp(gImagePlayer.anim, 0, 27)];
    c.text({x, y, x + w, y + scale(22)}, head, kDim, draw::kMiddle | draw::kEllipsis);
    DataPicturesHost* hp = &host;
    if (g.first >= 0) {
        const std::wstring follow = L"Follow";
        const draw::Rect br = kitpaint::smallButtonRect(c, x + w - c.measure(follow) - scale(20), y, scale(22), follow);
        kitpaint::drawButtonFace(c, br, follow, kitpaint::Tone::Normal, over(host, br) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
        host.pictureArea(br, [hp] { gIscriptListing.first = -1, hp->pictureRepaint(); }, nullptr, L"Follow the animation's current instruction", draw::Cursor::Hand);
    }
    y += scale(24);
    const int rowH = scale(18), shown = std::min<int>(int(lines.size()), 24), h = shown * rowH + scale(16);
    const draw::Rect cr{x, y, x + w, y + h};
    g.shown = shown;
    ground(c, cr, draw::rgb(14, 14, 16));
    const draw::Font uiFont = c.setFont(c.font({draw::Face::Mono}));
    const auto& ran = gImagePlayer.run.executed();
    const size_t now = ran.empty() ? size_t(-1) : ran.back();
    int at = 0;
    for (size_t k = 0; k < lines.size(); ++k)
        if (!lines[k].label && lines[k].offset == now) at = int(k);
    const int last = std::max(0, int(lines.size()) - shown);
    const int first = g.first >= 0 ? std::min(g.first, last) : std::clamp(at - shown / 3, 0, last);
    g.shownFirst = first;
    if (int(lines.size()) > shown) {  // where the box is in the listing
        const int bar = std::max(scale(16), h * shown / int(lines.size())), by = y + (h - bar) * first / std::max(1, last);
        c.fill({cr.right - scale(5), by + scale(2), cr.right - scale(2), by + bar - scale(2)}, kAccent, 90);
    }
    const int argX = x + scale(250);
    int ly = y + scale(8);
    for (size_t k = size_t(first); k < lines.size() && k < size_t(first + shown); ++k) {
        const auto& l = lines[k];
        if (!l.label && l.offset == now) c.fill({cr.left + 1, ly, cr.right - 1, ly + rowH}, kAccent, 70);
        else if (!l.label && std::find(ran.begin(), ran.end(), l.offset) != ran.end()) c.fill({cr.left + 1, ly, cr.right - 1, ly + rowH}, kAccent, 25);
        if (!l.label) {
            wchar_t b[16];
            std::swprintf(b, 16, L"%04zX", l.offset);
            c.text({x + scale(10), ly, x + scale(70), ly + rowH}, b, draw::rgb(80, 84, 92), draw::kMiddle);
            const bool jump = l.op == "goto" || l.op == "call" || l.op == "return" || l.op == "end" || l.op.find("condjmp") != std::string::npos;
            const bool wait = l.op == "wait" || l.op == "waitrand";
            const draw::Color opC = jump ? draw::rgb(197, 134, 192) : wait ? draw::rgb(120, 200, 240) : l.op.rfind("playsnd", 0) == 0 ? draw::rgb(240, 200, 90)
                                                                                                                                   : draw::rgb(212, 212, 212);
            c.text({x + scale(94), ly, argX - scale(6), ly + rowH}, widen(l.op), opC, draw::kMiddle);
            int ax = argX;  // the arguments, one by one: a reference with its name (a link), a label (a link), a number
            for (const auto& arg : l.argv) {
                if (ax >= cr.right - scale(12)) break;
                const std::wstring num = widen(arg.text);
                const int nw = c.measure(num);
                if (arg.ref == "label") {  // to the label in the listing
                    const draw::Rect lr{ax, ly, ax + nw, ly + rowH};
                    c.text({ax, ly, cr.right - scale(10), ly + rowH}, num, over(host, lr) ? kAccentBright : draw::rgb(220, 220, 170), draw::kMiddle);
                    const int target = arg.value;
                    host.pictureArea(lr,
                                     [hp, target] {
                                         auto& gl = gIscriptListing;
                                         for (size_t i = 0; i < gl.lines.size(); ++i)
                                             if (gl.lines[i].label && gl.lines[i].offset == size_t(target)) gl.first = std::min(int(i), std::max(0, int(gl.lines.size()) - gl.shown));
                                         hp->pictureRepaint();
                                     },
                                     nullptr, L"Go to the label", draw::Cursor::Hand);
                    ax += nw + scale(10);
                    continue;
                }
                c.text({ax, ly, cr.right - scale(10), ly + rowH}, num, draw::rgb(181, 206, 168), draw::kMiddle);
                ax += nw;
                if (!arg.ref.empty()) {  // its name: a link to it (in the UI font)
                    const draw::Font mono = c.setFont(uiFont);
                    const std::wstring name = iscriptRefName(arg.ref, arg.value);
                    const std::wstring shownName = L" " + (name.empty() ? L"#" + num : name);
                    const int tw = std::min<int>(c.measure(shownName), int(cr.right - scale(12) - ax));
                    const draw::Rect lr{ax, ly, ax + tw, ly + rowH};
                    c.text(lr, shownName, over(host, lr) ? kAccentBright : kAccent, draw::kMiddle | draw::kEllipsis);
                    if (arg.ref != "sfxdata" || DatSchema::find("sfxdata")) {
                        const std::string dat = arg.ref;
                        const int v = arg.value;
                        host.pictureArea(lr, [dat, v] { dataGoTo(dat, v); }, nullptr, L"Open it", draw::Cursor::Hand);
                    }
                    c.setFont(mono);
                    ax += tw;
                }
                ax += scale(10);
            }
        } else {
            c.text({x + scale(70), ly, cr.right - scale(10), ly + rowH}, widen(l.op + ":"), draw::rgb(220, 220, 170), draw::kMiddle);
        }
        ly += rowH;
    }
    c.setFont(uiFont);
    return scale(24) + h;
}

// The diagrams beside a section's fields (main.cpp: dataWidgets). Returns the y under them.
int paintDataDiagrams(draw::Canvas& c, DataPicturesHost& host, const std::string& dat, int section, int e, int x, int y, int w, bool& moving) {
    using namespace datapics;
    using C = LegendChip;
    const int gap = scale(12);
    int bottom = y;
    if (dat == "units" && section == 3) {
        const int dial = scale(120), fw = std::max(scale(240), std::min(w - dial - gap, scale(560))), fh = std::min(scale(380), fw * 3 / 4);
        footprint(c, host, e, x, y, fw, fh);
        direction(c, host, e, x + fw + gap, y, dial);
        const int pathW = scale(170);
        if (fh >= dial + gap + pathW) pathing(c, host, e, x + fw + gap, y + dial + gap, pathW);
        else pathing(c, host, e, x + fw + gap + dial + gap, y, pathW);
        const bool isAddon = (datValue("units", "Special Ability Flags", e) & 2) != 0;
        std::vector<C> chips = {{draw::rgb(120, 230, 140), L"Unit size", {"Unit Size Left", "Unit Size Up", "Unit Size Right", "Unit Size Down"}},
                                {draw::rgb(171, 242, 0), L"Placement box", {"StarEdit Placement Box Width", "StarEdit Placement Box Height"}, 1}};
        if (isAddon) chips.push_back({draw::rgb(240, 120, 100), L"Addon position", {"Addon Horizontal (X) Position", "Addon Vertical (Y) Position"}});
        chips.push_back({draw::rgb(240, 160, 60), L"Direction", {"Unit Direction"}});
        bottom = y + fh + scale(8) + legend(c, host, x, y + fh + scale(8), fw + gap + dial, chips);
    } else if (dat == "units" && section == 0) {
        const int s = std::max(scale(240), std::min(w, scale(440)));
        unitRanges(c, host, e, x, y, s, s);
        bottom = y + s + scale(8) +
                 legend(c, host, x, y + s + scale(8), s,
                        {{draw::rgb(190, 190, 196), L"Sight", {"Sight Range"}, 1}, {draw::rgb(240, 180, 70), L"Target acquisition", {"Target Acquisition Range"}},
                         {draw::rgb(235, 90, 80), L"Ground weapon", {"Ground Weapon"}}, {draw::rgb(90, 170, 240), L"Air weapon", {"Air Weapon"}}});
    } else if (dat == "weapons" && section == 0) {
        bottom = y + damage(c, host, e, x, y, std::min(w, scale(480)));
    } else if (dat == "weapons" && section == 1) {
        const int cw = std::max(scale(260), std::min(w, scale(520))), ch = cw * 2 / 3;
        weaponRanges(c, host, e, x, y, cw, ch, host.pictureDragging());
        bottom = y + ch + scale(8) +
                 legend(c, host, x, y + ch + scale(8), cw,
                        {{draw::rgb(235, 90, 80), L"Maximum range", {"Maximum Range"}}, {draw::rgb(150, 150, 158), L"Minimum range", {"Minimum Range"}, 2},
                         {draw::rgb(255, 140, 100), L"Outer splash", {"Outer Splash Range"}, 1},
                         {draw::rgb(225, 100, 80), L"Medium splash", {"Medium Splash Range"}, 1},
                         {draw::rgb(180, 75, 65), L"Inner splash", {"Inner Splash Range"}, 1}});
    } else if (dat == "weapons" && section == 2) {
        const int d = scale(180), ow = std::max(scale(200), std::min(w - d - gap, scale(360)));
        weaponDial(c, host, e, x, y, d);
        weaponOffsets(c, host, e, x + d + gap, y, ow, d);
        bottom = y + d + scale(8) +
                 legend(c, host, x, y + d + scale(8), d + gap + ow,
                        {{draw::rgb(120, 220, 120), L"Attack angle", {"Attack Angle"}, 1}, {draw::rgb(235, 90, 80), L"Launch spin", {"Launch Spin"}},
                         {draw::rgb(255, 210, 80), L"Start point", {"Forward Offset", "Upward Offset"}}});
    } else if (dat == "flingy" && section == 0) {
        if (!datValue("flingy", "Speed", e)) {
            const draw::Rect g{x, y, x + std::min(w, scale(400)), y + scale(44)};
            ground(c, g);
            c.text(g, L"does not move", kDim, draw::kCenter | draw::kMiddle);
            return y + scale(44);
        }
        const int cw = std::max(scale(260), std::min(w, scale(560))), ch = scale(180);
        speed(c, host, e, x, y, cw, ch);
        int yy = y + ch + scale(8);
        yy += legend(c, host, x, yy, cw,
                     {{draw::rgb(120, 200, 240), L"Top speed", {"Speed"}, 2}, {draw::rgb(160, 230, 255), L"Acceleration", {"Acceleration"}},
                      {draw::rgb(240, 180, 70), L"Halt distance", {"Halt Distance"}, 1}});
        yy += scale(14);
        turn(c, host, e, x, yy, cw, ch);
        yy += ch + scale(8);
        bottom = yy + legend(c, host, x, yy, cw, {{draw::rgb(240, 180, 70), L"Turn radius", {"Turn Radius"}}});
    } else if (dat == "sprites" && section == 0) {
        const int s = std::max(scale(200), std::min(w, scale(320)));
        sprite(c, host, e, x, y, s);
        bottom = y + s + scale(8) +
                 legend(c, host, x, y + s + scale(8), s,
                        {{draw::rgb(36, 180, 36), L"Health bar", {"Health Bar"}, 1},
                         {draw::rgb(255, 210, 80), L"Selection circle", {"Sel.Circle Image", "Sel.Circle Offset"}}});
    } else if (dat == "sfxdata" && section == 0) {
        bottom = y + soundPlayer(c, host, e, x, y, w, moving);
    }
    return bottom;
}

// The diagrams' bitmaps go before the program's GPU device does.
void releaseDataDiagrams() { datapics::gImageBitmaps.clear(); }
