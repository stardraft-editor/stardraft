// The Data form's pictures beside the fields, painted on the Canvas (portable, core.inc, after data_pictures.inc): the
// image player of an images.dat entry (the stage, the transport bar, the facing dial and the animations' list), its
// timeline, and a Port data entry's portrait. They are the Windows program's widgets (main.cpp: widgetImagePlayer,
// widgetTimeline, widgetPortrait) at the same sizes, on the model of core/data_pictures.inc. The program records the
// click and drag areas, says where the mouse is and paints the form again (DataPicturesHost). The SDL program's form
// draws them (sdl_data.inc); the Windows program still draws its GDI widgets.

// The program's side of the pictures.
class DataPicturesHost {
public:
    virtual ~DataPicturesHost() = default;
    // A part the mouse uses: `click` when the mouse goes up over it after a press, `drag` while it is held (from the
    // press on, with the form's point), `tip` its tooltip.
    virtual void pictureArea(draw::Rect r, std::function<void()> click, std::function<void(draw::Point)> drag, const std::wstring& tip,
                             draw::Cursor cursor) = 0;
    // The mouse's point in the form; false when it is not over the form.
    virtual bool pictureMouse(draw::Point& p) = 0;
    // The form paints again (a value of a picture changed).
    virtual void pictureRepaint() = 0;
};

namespace datapics {

constexpr double kPi = 3.14159265358979323846;

// A picture's ground: a dark rounded box with the border's line (the Windows widgets' canvas()).
inline void box(draw::Canvas& c, draw::Rect r, draw::Color bg = draw::rgb(17, 17, 19)) { c.roundBox(r, bg, kBorder, float(scale(4))); }

// The point at `turns` (0 up, clockwise) and `radius` from (cx, cy), and the turns of a point from (cx, cy).
inline draw::Point dialPoint(int cx, int cy, double turns, double radius) {
    const double a = turns * 2 * kPi;
    return {cx + int(std::lround(std::sin(a) * radius)), cy - int(std::lround(std::cos(a) * radius))};
}
inline double dialTurns(int cx, int cy, draw::Point pt) {
    double a = std::atan2(double(pt.x - cx), double(cy - pt.y)) / (2 * kPi);
    return a < 0 ? a + 1 : a;
}

inline bool over(DataPicturesHost& host, draw::Rect r) {
    draw::Point p;
    return host.pictureMouse(p) && r.contains(p);
}

// "* Needs verification" at the bottom left of a picture whose rule is not yet checked against OpenBW; its tip says what.
inline void needsVerification(draw::Canvas& c, DataPicturesHost& host, draw::Rect picture, const wchar_t* tip) {
    const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 4 / 5)}));
    const wchar_t* text = L"* Needs verification";
    const int w = c.measure(text), h = c.metrics().height;
    const draw::Rect r{picture.left + scale(6), picture.bottom - h - scale(5), picture.left + scale(6) + w, picture.bottom - scale(5)};
    c.text(r, text, draw::rgb(180, 140, 255), draw::kLeft);
    c.setFont(of);
    host.pictureArea(r, nullptr, nullptr, tip, draw::Cursor::Arrow);
}

// A square button with an icon: on (the accent's box), or a box under the mouse.
inline void glyphButton(draw::Canvas& c, DataPicturesHost& host, draw::Rect r, UiIcon icon, bool on, std::function<void()> click, const std::wstring& tip) {
    r = kitpaint::square(r);
    const bool hot = over(host, r);
    if (on) kitpaint::drawBox(c, r, kSel, kAccentLine);
    else if (hot) kitpaint::drawBox(c, r, kCtlHot, draw::kNone);
    kitpaint::drawGlyph(c, r, kitpaint::Ico(icon), on ? kAccentBright : hot ? kText : kDim, 14);
    host.pictureArea(r, std::move(click), nullptr, tip, draw::Cursor::Hand);
}

// The tile under the mouse in a picture's grid (cells `cell` px from (ox, oy)): marked, with what a cell is.
inline void gridHover(draw::Canvas& c, DataPicturesHost& host, draw::Rect clip, float ox, float oy, float cell, const std::wstring& what) {
    draw::Point p;
    if (!host.pictureMouse(p) || !clip.contains(p) || cell <= 0) return;
    const float gx = ox + std::floor((p.x - ox) / cell) * cell, gy = oy + std::floor((p.y - oy) / cell) * cell;
    draw::Rect g{int(std::lround(gx)), int(std::lround(gy)), int(std::lround(gx + cell)), int(std::lround(gy + cell))};
    g = {std::max(g.left, clip.left), std::max(g.top, clip.top), std::min(g.right, clip.right), std::min(g.bottom, clip.bottom)};
    if (g.empty()) return;
    c.fill({g.left + 1, g.top + 1, g.right, g.bottom}, kAccent, 40);
    c.frame(g, kAccent);
    const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 4 / 5)}));
    const int pad = scale(4), w = c.measure(what) + 2 * pad, h = c.metrics().height + scale(2);
    const int mx = (g.left + g.right) / 2;
    draw::Rect t{mx - w / 2, (g.top + g.bottom) / 2 - h / 2, mx + w / 2, (g.top + g.bottom) / 2 + h / 2};
    if (w > g.width() - scale(2) || h > g.height() - scale(2)) {  // on a chip under the cell (above it at the bottom edge)
        auto move = [&t](int dx, int dy) { t = {t.left + dx, t.top + dy, t.right + dx, t.bottom + dy}; };
        move(0, g.bottom + scale(3) - t.top);
        if (t.bottom > clip.bottom) move(0, g.top - scale(3) - h - t.top);
        if (t.left < clip.left + scale(2)) move(clip.left + scale(2) - t.left, 0);
        if (t.right > clip.right - scale(2)) move(clip.right - scale(2) - t.right, 0);
        c.fill(t, draw::rgb(30, 30, 34), 230);
    }
    c.text(t, what, draw::rgb(236, 236, 240), draw::kCenter | draw::kMiddle);
    c.setFont(of);
}

// A Sprite (straight alpha) into `b` (premultiplied), the same size.
inline void spriteToBitmap(const Sprite& s, draw::Bitmap& b) {
    if (b.width() != s.width || b.height() != s.height) b.resize(s.width, s.height);
    uint32_t* px = b.pixels();
    for (size_t i = 0; i < size_t(s.width) * size_t(s.height); ++i) {
        const uint8_t* p = &s.rgba[i * 4];
        const uint32_t a = p[3];
        px[i] = uint32_t(p[0] * a / 255) | uint32_t(p[1] * a / 255) << 8 | uint32_t(p[2] * a / 255) << 16 | a << 24;
    }
    b.changed();
}

// The pictures' bitmaps (the stage, the portrait's frame): each canvas keeps its copy in them (a GPU texture on SDL).
inline draw::Bitmap gStage, gPortraitFrame;

}  // namespace datapics

// The bitmaps' copies go (before the program's GPU device does: a later release would use a freed device).
void releaseDataPictures() { datapics::gStage = draw::Bitmap(), datapics::gPortraitFrame = draw::Bitmap(); }

const std::wstring kDataTileCell = L"1 tile: 32 x 32 px";

// The image player (images.dat, beside the script's fields): the stage with the sprite as its script runs it, the
// transport bar, the facing and the animations. Returns its height.
int paintImagePlayer(draw::Canvas& c, DataPicturesHost& host, int image, int x, int y, int w) {
    using namespace datapics;
    if (!shellState().ed) return 0;
    UnitSprites& sp = dataSprites();
    static int editsSeen = -1;
    if (gImagePlayer.image != image) imagePlayerStart(image, 0), editsSeen = dataState().edits;
    else if (editsSeen != dataState().edits) imagePlayerStart(image, gImagePlayer.anim), editsSeen = dataState().edits;
    const int listW = std::min(scale(220), w / 3), stage = std::min(w - listW - scale(12), scale(400)), barH = scale(40);
    // The stage.
    const draw::Rect st{x, y, x + stage, y + stage};
    box(c, st, draw::rgb(12, 12, 14));
    // The zoom fits every image the sprite draws (overlays can sit far from the main one), at its widest so far in this
    // animation, so the stage does not jump as images come and go.
    static int extentImage = -1, extentAnim = -1, extent = 0;
    if (extentImage != image || extentAnim != gImagePlayer.anim) extentImage = image, extentAnim = gImagePlayer.anim, extent = 0;
    if (const Grp* g = sp.grp(image); g && g->width) extent = std::max<int>(extent, std::max(g->width, g->height));
    for (const auto& d : gImagePlayer.run.draws())
        if (const Grp* gd = sp.grp(d.image); gd && gd->width)
            extent = std::max(extent, 2 * std::max(std::abs(d.x) + gd->width / 2, std::abs(d.y) + gd->height / 2));
    const float zoom = extent ? std::clamp((stage - scale(40)) / float(extent), 0.5f, 4.0f * float(shellDpi()) / 96) : 1.0f;
    const int cx = x + stage / 2, cy = y + stage / 2;
    {
        const draw::Rect in{st.left + 1, st.top + 1, st.right - 1, st.bottom - 1};
        c.pushClip(in);
        // A faint ground grid of tiles.
        for (int k = -10; k <= 10; ++k) {
            const int lx = cx + int(k * 32 * zoom), ly = cy + int(k * 32 * zoom);
            if (lx > st.left && lx < st.right) c.line(lx, st.top, lx, st.bottom, draw::rgb(24, 24, 28));
            if (ly > st.top && ly < st.bottom) c.line(st.left, ly, st.right, ly, draw::rgb(24, 24, 28));
        }
        // A cloak or an EMP shockwave shows what is under it: the map's terrain then stays under the sprite.
        for (const auto& d : gImagePlayer.run.draws()) gImagePlayer.ground |= drawFunctionBends(d.drawFunction);
        static Sprite picture;
        const int k = spriteSimPicture(gImagePlayer.run, 0, gImagePlayer.ground, picture);
        if (k > 0 && picture.width > 0) {
            spriteToBitmap(picture, gStage);
            const int sw = int(picture.width * zoom / k), sh = int(picture.height * zoom / k);
            const int left = cx - int(picture.originX * zoom / k), top = cy - int(picture.originY * zoom / k);
            c.bitmap(gStage, {0, 0, picture.width, picture.height}, {left, top, left + std::max(1, sw), top + std::max(1, sh)}, k > 1);
        }
        if (gImagePlayer.target >= 0) {  // the target: a ring in its direction
            const draw::Point t = dialPoint(cx, cy, gImagePlayer.target / 256.0, stage * 0.42);
            c.ring({t.x - scale(7), t.y - scale(7), t.x + scale(7), t.y + scale(7)}, kBad, scale(2));
        }
        gridHover(c, host, in, float(cx), float(cy), 32 * zoom, kDataTileCell);
        c.popClip();
    }
    if (sp.turns(image))
        host.pictureArea(st, nullptr,
                         [cx, cy, &host](draw::Point p) {
                             gImagePlayer.target = int(std::lround(dialTurns(cx, cy, p) * 256)) & 255;
                             gImagePlayer.run.setTarget(gImagePlayer.target);
                             host.pictureRepaint();
                         },
                         L"Drag to put a target. The image turns to it at the turn rate of its unit, as a turret does.", draw::Cursor::Hand);
    needsVerification(c, host, st,
                      L"Ported from OpenBW, not yet compared with the game: images and overlays, shadows, glows, cloaks, flips, turning to the target, move steps and weapon cooldowns. Other parts of the orders do not run.");
    // The transport bar under the stage: restart, play / pause, one tick, loop; the frame; the facing.
    const int by = st.bottom + scale(8), bs = barH - scale(8);
    int bx = x;
    auto next = [&] { bx += bs + scale(6); };
    glyphButton(c, host, {bx, by, bx + bs, by + bs}, UiIcon::ToStart, false, [image, &host] { imagePlayerStart(image, gImagePlayer.anim), host.pictureRepaint(); },
                L"Start again");
    next();
    glyphButton(c, host, {bx, by, bx + bs, by + bs}, gImagePlayer.playing ? UiIcon::Pause : UiIcon::Play, false,
                [&host] { gImagePlayer.playing = !gImagePlayer.playing, gImagePlayer.last = dataTickMs(), host.pictureRepaint(); }, L"Play or pause");
    next();
    glyphButton(c, host, {bx, by, bx + bs, by + bs}, UiIcon::ToEnd, false, [&host] { gImagePlayer.playing = false, imagePlayerStep(), host.pictureRepaint(); },
                L"One game tick");
    next();
    glyphButton(c, host, {bx, by, bx + bs, by + bs}, UiIcon::Loop, gImagePlayer.loop, [&host] { gImagePlayer.loop = !gImagePlayer.loop, host.pictureRepaint(); },
                L"Play on as a unit does: attacks repeat, and a script that waits for its order starts again");
    next();
    glyphButton(c, host, {bx, by, bx + bs, by + bs}, gImagePlayer.muted ? UiIcon::Mute : UiIcon::Sound, !gImagePlayer.muted,
                [&host] { gImagePlayer.muted = !gImagePlayer.muted, host.pictureRepaint(); }, L"Sounds of the script: on or off");
    next();
    glyphButton(c, host, {bx, by, bx + bs, by + bs}, UiIcon::Code, gImagePlayer.showCode,
                [&host] { gImagePlayer.showCode = !gImagePlayer.showCode, host.pictureRepaint(); }, L"The code of the script: show or hide");
    bx += bs + scale(14);
    wchar_t b[96];
    std::swprintf(b, 96, L"frame %d  \x00B7  tick %d%ls", gImagePlayer.run.mainFrame(), gImagePlayer.ticks, gImagePlayer.run.running() ? L"" : L"  \x00B7  end");
    c.text({bx, by, st.right - bs - scale(8), by + bs}, b, kDim, draw::kMiddle | draw::kEllipsis);
    if (sp.turns(image)) {  // the facing: a small dial at the right of the bar
        const int ds = bs, dx = st.right - ds, dcx = dx + ds / 2, dcy = by + ds / 2;
        box(c, {dx, by, dx + ds, by + ds});
        const draw::Point e = dialPoint(dcx, dcy, gImagePlayer.run.direction() / 32.0, ds / 2 - scale(5));
        c.line(dcx, dcy, e.x, e.y, draw::rgb(240, 160, 60), scale(2));
        host.pictureArea({dx, by, dx + ds, by + ds}, nullptr,
                         [dcx, dcy, &host](draw::Point p) {
                             gImagePlayer.dir = int(std::lround(dialTurns(dcx, dcy, p) * 32)) % 32, gImagePlayer.target = -1;
                             gImagePlayer.run.setTarget(-1), gImagePlayer.run.setDirection(gImagePlayer.dir);
                             host.pictureRepaint();
                         },
                         L"Drag to turn the image. This removes the target.", draw::Cursor::Hand);
    }
    // The animations: a list beside the stage.
    const int lx = st.right + scale(12), rowH = scale(28);
    box(c, {lx, y, lx + listW, st.bottom}, draw::rgb(22, 22, 25));
    int ly = y + scale(4);
    for (int a = 0; a < 28; ++a) {
        if (a > 0 && !sp.animOffset(image, a)) continue;
        const draw::Rect r{lx + 1, ly, lx + listW - 1, ly + rowH};
        const bool on = a == gImagePlayer.anim;
        if (on) c.fill(r, kSel), c.fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
        else if (over(host, r)) c.fill(r, kRowHot);
        if (on) kitpaint::drawGlyph(c, {r.left + scale(6), r.top, r.left + scale(24), r.bottom}, kitpaint::Ico(gImagePlayer.playing ? UiIcon::Play : UiIcon::Pause), kAccent);
        c.text({r.left + scale(26), r.top, r.right - scale(6), r.bottom}, kAnimNames[a], on ? kAccent : kText, draw::kMiddle | draw::kEllipsis);
        host.pictureArea(r, [image, a, &host] { imagePlayerStart(image, a), gImagePlayer.playing = true, host.pictureRepaint(); }, nullptr, L"", draw::Cursor::Hand);
        ly += rowH;
    }
    return std::max(int(by + bs - y), ly - y);
}

// The animation as a timeline under the player: a block for each frame as long as it shows, the sounds and the attacks
// above, the seconds under it and where the player is; click or drag to go to a tick. Returns its height (0: none).
int paintTimeline(draw::Canvas& c, DataPicturesHost& host, int image, int x, int y, int w) {
    using namespace datapics;
    if (!shellState().ed) return 0;
    // The run to 240 ticks, again only when the image, animation, play-on or the edits change.
    static struct {
        int image = -1, anim = -1, edits = -1;
        bool loop = false, ends = false;
        std::vector<TimelineTick> ticks;
    } cache;
    if (cache.image != image || cache.anim != gImagePlayer.anim || cache.edits != dataState().edits || cache.loop != gImagePlayer.loop)
        cache.image = image, cache.anim = gImagePlayer.anim, cache.edits = dataState().edits, cache.loop = gImagePlayer.loop,
        cache.ticks = animTimeline(image, gImagePlayer.anim, 240, cache.ends);
    const bool ends = cache.ends;
    const auto& tl = cache.ticks;
    if (tl.empty()) return 0;
    const int h = scale(70), n = int(tl.size());
    const draw::Rect cr{x, y, x + w, y + h};
    box(c, cr);
    const int x0 = cr.left + scale(10), x1 = cr.right - scale(10), by = cr.top + scale(22), bh = scale(22);
    auto X = [&](int t) { return x0 + int(int64_t(x1 - x0) * t / n); };
    // A block for each frame shown, as long as it shows; its number when there is room. The shades alternate from the
    // first block on each paint.
    bool alt = false;
    for (int t = 0; t < n;) {
        int e = t;
        while (e < n && tl[size_t(e)].frame == tl[size_t(t)].frame) ++e;
        const draw::Rect r{X(t) + 1, by, X(e), by + bh};
        alt = !alt;
        c.fill(r, alt ? draw::rgb(70, 100, 130) : draw::rgb(55, 80, 105));
        if (r.width() > scale(18)) c.text(r, std::to_wstring(tl[size_t(t)].frame), draw::rgb(200, 215, 230), draw::kCenter | draw::kMiddle);
        t = e;
    }
    // Sounds and attacks above the blocks.
    for (int t = 0; t < n; ++t) {
        const draw::Rect m{X(t) - scale(8), cr.top + scale(4), X(t) + scale(8), by - scale(2)};
        if (tl[size_t(t)].attack) kitpaint::drawGlyph(c, m, kitpaint::Ico(UiIcon::Attack), draw::rgb(235, 90, 80));
        else if (tl[size_t(t)].sound) kitpaint::drawGlyph(c, m, kitpaint::Ico(UiIcon::DatSfx), draw::rgb(240, 200, 90));
    }
    // Seconds under the blocks, and where the player is.
    for (int t = 0; t < n; t += 24) {
        c.line(X(t), by + bh, X(t), by + bh + scale(4), draw::rgb(90, 90, 96));
        c.text({X(t) + scale(2), by + bh + scale(2), X(t) + scale(40), cr.bottom - scale(2)}, std::to_wstring(t / 24) + L" s", kDim, draw::kMiddle);
    }
    if (!ends) c.text({x1 - scale(60), by + bh + scale(2), x1, cr.bottom - scale(2)}, L"\x2192 loops", kDim, draw::kMiddle | draw::kRight);
    needsVerification(c, host, {cr.right - scale(140), cr.top, cr.right, cr.top + scale(20)},
                      L"Not checked against OpenBW: the waits and the order of the script, as StarDraft 2 plays it.");
    const int px = X(std::min(gImagePlayer.ticks, n - 1));
    c.line(px, cr.top + scale(4), px, by + bh + scale(2), draw::rgb(255, 255, 255), scale(2));
    host.pictureArea({x0, cr.top, x1, cr.bottom}, nullptr,
                     [x0, x1, n, &host](draw::Point p) {
                         imagePlayerSeek(std::clamp(int(int64_t(p.x - x0) * n / std::max(1, x1 - x0)), 0, n - 1));
                         host.pictureRepaint();
                     },
                     L"Frames and their waits (24 ticks a second). Yellow: a sound. Red: an attack. Click or drag to go to a tick.", draw::Cursor::Hand);
    return h;
}

// The portrait of a Port data entry: its video at 10 frames a second, its clips one after another, as the game shows
// them (read and decoded by portraitClips); its file and clips beside it. `shown`: the box when a frame shows (the
// program's timer paints it again for the next frame). Returns its height.
int paintPortrait(draw::Canvas& c, int e, int x, int y, int w, draw::Rect* shown) {
    using namespace datapics;
    if (shown) *shown = {};
    const std::string path = portraitPath(e);
    const std::shared_ptr<PortraitClips> clips = portraitClips(path);
    const bool ready = clips->ready.load(std::memory_order_acquire);
    if (ready) {  // the next and the previous entry's portraits, before they are needed
        portraitClips(portraitPath(e + 1));
        portraitClips(portraitPath(e - 1));
    }
    static const std::vector<PortraitVideo> none;
    const std::vector<PortraitVideo>& list = ready ? clips->clips : none;
    const int side = std::min(w, scale(240)), h = side * 7 / 8;
    const draw::Rect r0{x, y, x + side, y + h};
    box(c, r0);
    int total = 0;
    for (const auto& v : list) total += int(v.frames.size());
    if (!total) {
        c.text(r0, path.empty() ? L"No portrait file" : !ready ? L"Loading the portrait\x2026" : L"The portrait's video cannot be read", kDim,
               draw::kCenter | draw::kMiddle);
        return h;
    }
    int f = int((dataTickMs() / 100) % uint64_t(total));  // 10 frames a second
    for (const auto& v : list) {
        if (f < int(v.frames.size())) {
            const float k = std::min(float(side - scale(16)) / float(v.width), float(h - scale(16)) / float(v.height));
            const int pw = int(float(v.width) * k), ph = int(float(v.height) * k);
            const draw::Rect r{x + (side - pw) / 2, y + (h - ph) / 2, x + (side - pw) / 2 + pw, y + (h - ph) / 2 + ph};
            // The frame's pixels (opaque RGBA) as a bitmap; one bitmap, changed to the frame shown.
            draw::Bitmap& frameBitmap = gPortraitFrame;
            static std::pair<uint64_t, int> frameShown{0, -1};
            if (frameShown != std::make_pair(v.serial, f) || frameBitmap.width() != v.width || frameBitmap.height() != v.height) {
                frameShown = {v.serial, f};
                if (frameBitmap.width() != v.width || frameBitmap.height() != v.height) frameBitmap.resize(v.width, v.height);
                const std::vector<uint8_t>& px = v.frames[size_t(f)];
                uint32_t* out = frameBitmap.pixels();
                for (size_t i = 0; i < size_t(v.width) * size_t(v.height) && i * 4 + 2 < px.size(); ++i)
                    out[i] = uint32_t(px[i * 4]) | uint32_t(px[i * 4 + 1]) << 8 | uint32_t(px[i * 4 + 2]) << 16 | 0xFF000000u;
                frameBitmap.opaque = true;
                frameBitmap.changed();
            }
            c.bitmap(frameBitmap, {0, 0, v.width, v.height}, r, true);
            break;
        }
        f -= int(v.frames.size());
    }
    if (shown) *shown = r0;
    c.text({r0.right + scale(12), y, x + w, y + scale(20)}, widen(path) + L"  \x00B7  " + std::to_wstring(list.size()) + (list.size() == 1 ? L" clip" : L" clips"),
           kDim, draw::kMiddle | draw::kEllipsis);
    return h;
}

// The pictures of a section of the form that the SDL program has (images.dat section 1: the player and the timeline;
// portdata.dat section 0: the portrait). Returns the bottom (y when the section has none here). `moving`: the
// pictures move (the image plays, a portrait shows): the program's timer paints them again.
int paintDataPictures(draw::Canvas& c, DataPicturesHost& host, const std::string& dat, int section, int e, int x, int y, int w, bool& moving) {
    if (dat == "portdata" && section == 0) {
        draw::Rect shown;
        const int h = paintPortrait(c, e, x, y, w, &shown);
        moving |= !shown.empty() || !portraitClips(portraitPath(e))->ready.load(std::memory_order_acquire);
        return y + h + scale(8);
    }
    if (dat == "images" && section == 1) {  // beside the script's fields (Iscript ID): the script played
        const int pw = std::min(w, scale(680)), ph = paintImagePlayer(c, host, e, x, y, pw);
        moving |= gImagePlayer.playing;
        return y + ph + scale(12) + paintTimeline(c, host, e, x, y + ph + scale(12), pw);
    }
    return y;
}
