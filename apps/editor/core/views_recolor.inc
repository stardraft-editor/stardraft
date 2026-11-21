// Tools > Cosmonarchy > Recolor preview (portable, core.inc): the recolor CRIG's values (src/mods/cmbw_recolor.cpp:
// recolor, recolor_loop_add) with a control for each, and a part of the open map (its terrain and units around the view's
// middle) recolored as the mod's exe plugin recolors the game's palette. The line's text with Copy, for a trigger's
// Comment; Load takes the values of a recolor line in the map. A modal kit Dialog (the host: sdl_map_dialogs.inc).

namespace recolorLayout {
constexpr int kPad = 16;
constexpr int kLeftW = 340;                       // the controls
constexpr int kLabelW = 76, kFieldW = 60;         // a row's name and its number field
constexpr int kRowH = 24, kPitch = 32;
constexpr int kPicX = kPad + kLeftW + kPad;       // 372: the preview
constexpr int kPicW = 512, kPicH = 384;           // 16 x 12 tiles
constexpr int kBarY = 148, kBarH = 28;            // the stops' gradient
constexpr int kTintY = kBarY + kBarH + 12, kTintH = 96;
constexpr int kSlidersY = kTintY + kTintH + 12;   // R, G, B, Gamma, Saturation
constexpr int kValueY = kSlidersY + 5 * kPitch + 12;  // the line and Copy (under the sliders and the preview's Original)
constexpr int kButtonsY = kValueY + kPitch + 12;  // Close
constexpr int kButtonH = 28;
constexpr int kWindowW = kPicX + kPicW + kPad;    // 900
constexpr int kWindowH = kButtonsY + kButtonH + kPad;
}  // namespace recolorLayout

constexpr int kRecolorClose = 1;

// The brightness that a stop's place is at: where the plugin's eased brightness is i / (n - 1) (3t^2 - 2t^3 goes up from 0
// to 1, so the place is found by halving).
inline float recolorStopPlace(size_t i, size_t n) {
    if (n < 2) return 0.5f;
    const float want = float(i) / float(n - 1);
    float lo = 0, hi = 1;
    for (int k = 0; k < 24; ++k) {
        const float m = (lo + hi) / 2;
        (m * m * (3 - 2 * m) < want ? lo : hi) = m;
    }
    return (lo + hi) / 2;
}

// Hue (0 to 1) and saturation (0 to 1) of a color, and a color of them at full value.
inline void recolorHueSat(float r, float g, float b, float& h, float& s) {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0) return void(h = 0);
    float x = mx == r ? (g - b) / d : mx == g ? 2 + (b - r) / d : 4 + (r - g) / d;
    x /= 6;
    h = x < 0 ? x + 1 : x;
}
inline void recolorFromHueSat(float h, float s, float& r, float& g, float& b) {
    const float x = h * 6;
    const int k = int(x) % 6;
    const float f = x - std::floor(x), p = 1 - s, q = 1 - s * f, t = 1 - s * (1 - f);
    const float v[6][3] = {{1, t, p}, {q, 1, p}, {p, 1, t}, {p, q, 1}, {t, p, 1}, {1, p, q}};
    r = v[k][0], g = v[k][1], b = v[k][2];
}

class RecolorView final : public draw::View {
public:
    // `ed`: the open map (null: none; the preview is then a chart of colors).
    explicit RecolorView(Editor* ed, std::string start = {}) : ed_(ed) {
        using stardraft::kit::LabelTone;
        if (ed_) lines_ = stardraft::cmbw::recolorLines(ed_->chk());
        auto label = [this](stardraft::kit::Label& l, const wchar_t* t) {
            l.setText(tr(t));
            l.setTone(LabelTone::Dim);
            add(l, {0, 0, 0, 0});
        };
        label(loadLabel_, L"Load");
        label(commandLabel_, L"Command");
        label(timeLabel_, L"Time (s)");
        label(stopsLabel_, L"Stops");
        label(valueLabel_, L"CRIG");
        static const wchar_t* const names[5] = {L"R", L"G", L"B", L"Gamma", L"Saturation"};
        static const wchar_t* const tips[5] = {L"Red factor", L"Green factor", L"Blue factor", L"Factor on R, G and B",
                                               L"0: gray. 1: as it is. Above 1 it goes down again (the plugin's curve)."};
        for (int k = 0; k < 5; ++k) {
            label(rowLabel_[k], names[k]);
            slider_[k].setRange(0, k == 4 ? 150 : 300);
            slider_[k].setTip(tr(tips[k]));
            slider_[k].onChange = [this, k](int v) { setPart(k, float(v) / 100.0f, false); };
            add(slider_[k], {0, 0, 0, 0});
            numberField(field_[k], [this, k](float v) { setPart(k, v, true); });
        }
        time_.setRange(0, 240);  // (quarter seconds: 0 to 60 s)
        time_.setTip(tr(L"0: at once"));
        time_.onChange = [this](int v) { value_.seconds = v / 4.0, sync(); };
        add(time_, {0, 0, 0, 0});
        numberField(timeField_, [this](float v) { value_.seconds = v, sync(); });

        std::vector<stardraft::kit::DropdownItem> items;
        if (lines_.empty()) items.push_back({tr(L"No recolor CRIGs in the map")});
        else {
            items.push_back({tr(L"Default")});
            for (const auto& l : lines_) items.push_back({std::wstring(tr(L"Trigger ")) + std::to_wstring(l.trigger) + L": " + widen(l.line)});
        }
        load_.setItems(std::move(items));
        load_.setValue(0);
        load_.setEnabled(!lines_.empty());
        load_.onChange = [this](int i) {
            stardraft::cmbw::RecolorValue v;
            if (i > 0 && size_t(i - 1) < lines_.size()) stardraft::cmbw::parseRecolorLine(lines_[size_t(i - 1)].line, v);
            setValue(v);
        };
        add(load_, {0, 0, 0, 0});
        command_.setItems({{L"recolor"}, {L"recolor_loop_add"}});
        command_.setValue(0);
        command_.onChange = [this](int i) { value_.command = i == 1 ? "recolor_loop_add" : "recolor", sync(); };
        add(command_, {0, 0, 0, 0});

        addStop_.setGlyph(stardraft::kit::IconGlyph::ofIcon(int(UiIcon::Add)));
        addStop_.setStyle(stardraft::kit::IconStyle::Boxed);
        addStop_.setTip(tr(L"Add stop"));
        addStop_.onClick = [this] { addStop(); };
        add(addStop_, {0, 0, 0, 0});
        removeStop_.setGlyph(stardraft::kit::IconGlyph::ofIcon(int(UiIcon::Remove)));
        removeStop_.setStyle(stardraft::kit::IconStyle::Boxed);
        removeStop_.setTip(tr(L"Remove stop"));
        removeStop_.onClick = [this] { removeStop(); };
        add(removeStop_, {0, 0, 0, 0});

        original_.setLabel(tr(L"Original"));
        original_.setPlainLabel(true);
        original_.onChange = [this](bool) { picDirty_ = true, invalidate(); };
        add(original_, {0, 0, 0, 0});
        line_.setReadOnly(true);
        add(line_, {0, 0, 0, 0});
        copy_.setText(tr(L"Copy"));
        copy_.onClick = [this] { copyLine(); };
        add(copy_, {0, 0, 0, 0});

        makeBase();
        stardraft::cmbw::RecolorValue v;
        if (!start.empty()) stardraft::cmbw::parseRecolorLine(start, v);
        setValue(v);
    }

    const stardraft::cmbw::RecolorValue& value() const { return value_; }
    std::string lineText() const { return stardraft::cmbw::recolorLineText(value_); }
    size_t selectedStop() const { return sel_; }
    // A snapshot's or a test's input: stop i chosen.
    void selectStop(size_t i) {
        if (i < value_.stops.size()) sel_ = i, sync();
    }
    // The preview's pixel at (x, y) after the recolor (0xAABBGGRR), for the checks.
    uint32_t previewPixel(int x, int y) {
        refreshPicture();
        if (!pic_ || x < 0 || y < 0 || x >= pic_->width() || y >= pic_->height()) return 0;
        return pic_->pixels()[size_t(y) * size_t(pic_->width()) + size_t(x)];
    }
    uint32_t basePixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= baseW_ || y >= baseH_) return 0;
        return base_[size_t(y) * size_t(baseW_) + size_t(x)];
    }
    int pictureW() const { return baseW_; }
    int pictureH() const { return baseH_; }
    // Copy: the line to the clipboard.
    void copyLine() {
        shellSetClipboardText(lineText());
        copied_ = true, invalidate();
    }

    bool mouse(const draw::MouseEvent& e) override {
        const draw::Rect bar = barRect(), tint = tintRect();
        if (e.kind == draw::MouseKind::Up && dragging_) {
            dragging_ = 0;
            if (host()) host()->capture(*this, false);
            return true;
        }
        if (e.kind == draw::MouseKind::Move && dragging_ == 2) return tintAt(e.pt), true;
        if (e.kind != draw::MouseKind::Down || e.button != 0) return false;
        if (bar.contains(e.pt)) {  // the stop nearest the click
            size_t best = 0;
            int bestD = 1 << 30;
            for (size_t i = 0; i < value_.stops.size(); ++i)
                if (const int d = std::abs(stopX(i) - e.pt.x); d < bestD) bestD = d, best = i;
            sel_ = best, sync();
            return true;
        }
        if (tint.contains(e.pt)) {
            dragging_ = 2;
            if (host()) host()->capture(*this, true);
            return tintAt(e.pt), true;
        }
        return false;
    }
    void captureLost() override { dragging_ = 0; }

private:
    void numberField(stardraft::kit::TextField& f, std::function<void(float)> set) {
        f.setFilter([](wchar_t c) { return (c >= L'0' && c <= L'9') || c == L'.'; });
        f.setMaxLength(8);
        f.setAlignRight(true);
        auto commit = [this, &f, set] {
            float v = 0;
            const std::string s = narrow(f.text());
            if (!s.empty() && std::from_chars(s.data(), s.data() + s.size(), v).ec == std::errc{}) set(std::max(0.0f, v));
            else sync();  // (not a number: the value as it was)
        };
        f.onEnter = commit;
        f.onFocusLost = commit;
        add(f, {0, 0, 0, 0});
    }

    stardraft::cmbw::Recolor& stop() { return value_.stops[sel_]; }
    float& part(int k) {
        stardraft::cmbw::Recolor& r = stop();
        return k == 0 ? r.r : k == 1 ? r.g : k == 2 ? r.b : k == 3 ? r.gamma : r.saturation;
    }
    void setPart(int k, float v, bool typed) {
        part(k) = typed ? v : std::round(v * 100.0f) / 100.0f;
        sync();
    }
    void setValue(const stardraft::cmbw::RecolorValue& v) {
        value_ = v;
        if (value_.stops.empty()) value_.stops.push_back({});
        sel_ = std::min(sel_, value_.stops.size() - 1);
        command_.setValue(value_.command == "recolor_loop_add" ? 1 : 0);
        sync();
    }
    void addStop() {
        if (value_.stops.size() >= stardraft::cmbw::kRecolorStops) return;
        // (a copy of the chosen stop after it: the colors stay as they are until it is changed)
        value_.stops.insert(value_.stops.begin() + std::ptrdiff_t(sel_ + 1), value_.stops[sel_]);
        ++sel_, sync();
    }
    void removeStop() {
        if (value_.stops.size() <= 1) return;
        value_.stops.erase(value_.stops.begin() + std::ptrdiff_t(sel_));
        sel_ = std::min(sel_, value_.stops.size() - 1);
        sync();
    }
    // A click in the hue and saturation square: the stop's R, G, B get that color, at the stop's strongest factor.
    void tintAt(draw::Point p) {
        const draw::Rect t = tintRect();
        const float h = std::clamp(float(p.x - t.left) / float(std::max(1, t.width() - 1)), 0.0f, 0.9999f);
        const float s = std::clamp(1.0f - float(p.y - t.top) / float(std::max(1, t.height() - 1)), 0.0f, 1.0f);
        stardraft::cmbw::Recolor& r = stop();
        float m = std::max({r.r, r.g, r.b});
        if (m <= 0) m = 1;
        float cr = 1, cg = 1, cb = 1;
        recolorFromHueSat(h, s, cr, cg, cb);
        r.r = std::round(cr * m * 100) / 100, r.g = std::round(cg * m * 100) / 100, r.b = std::round(cb * m * 100) / 100;
        sync();
    }

    // The controls and the line after a change.
    void sync() {
        auto num = [](double v) {
            std::wstring s = widen(stardraft::cmbw::recolorLineText({"recolor", {{float(v), 1, 1, 1, 1}}, 0}));
            const size_t a = s.find(L'(') + 1;  // (the line's way of writing a number)
            return s.substr(a, s.find(L' ', a) - a);
        };
        for (int k = 0; k < 5; ++k) {
            slider_[k].setValue(int(std::lround(part(k) * 100.0f)));
            if (!field_[k].hasFocus()) field_[k].setText(num(part(k)));
        }
        time_.setValue(int(std::lround(value_.seconds * 4)));
        if (!timeField_.hasFocus()) timeField_.setText(num(value_.seconds));
        addStop_.setEnabled(value_.stops.size() < stardraft::cmbw::kRecolorStops);
        removeStop_.setEnabled(value_.stops.size() > 1);
        line_.setText(widen(lineText()));
        copied_ = false, picDirty_ = true;
        invalidate();
    }

    // The part of the map that the preview shows: 16 x 12 tiles around the view's middle (its terrain, and its units on
    // top, as the map shows them in SD); without a map, a chart of hues (across) and brightness (down) with grays at
    // the right.
    void makeBase() {
        using namespace recolorLayout;
        baseW_ = kPicW, baseH_ = kPicH;
        base_.assign(size_t(baseW_) * size_t(baseH_), 0xFF000000u);
        if (ed_ && ed_->isOpen() && ed_->width() > 0) {
            Editor& ed = *ed_;
            const AppState& app = appState();
            int vw = 0, vh = 0;
            shellViewSize(vw, vh);
            const float z = app.zoom > 0 ? app.zoom : 1;
            const int cx = int(app.camX + float(vw) / z / 2), cy = int(app.camY + float(vh) / z / 2);
            int tx = std::clamp(cx / 32 - 8, 0, std::max(0, int(ed.width()) - 16)), ty = std::clamp(cy / 32 - 6, 0, std::max(0, int(ed.height()) - 12));
            const stardraft::Sprite s = stardraft::mapTilesPicture(ed.tileset(), ed.terrain().tiles, ed.width(), tx, ty, 16, 12);
            for (int y = 0; y < std::min(s.height, baseH_); ++y)
                for (int x = 0; x < std::min(s.width, baseW_); ++x) {
                    const uint8_t* p = &s.rgba[(size_t(y) * size_t(s.width) + size_t(x)) * 4];
                    if (p[3]) base_[size_t(y) * size_t(baseW_) + size_t(x)] = p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | 0xFF000000u;
                }
            const int ox = tx * 32, oy = ty * 32;
            std::vector<const Unit*> units;
            for (const Unit& u : ed.units())
                if (int(u.x) >= ox - 128 && int(u.x) < ox + baseW_ + 128 && int(u.y) >= oy - 128 && int(u.y) < oy + baseH_ + 128) units.push_back(&u);
            std::stable_sort(units.begin(), units.end(), [](const Unit* a, const Unit* b) { return a->y < b->y; });
            UnitSprites& us = ed.unitSprites();
            for (const Unit* u : units) {
                const stardraft::Sprite* sp = us.unitSprite(u->type, u->owner, ed.tileset());
                if (!sp || sp->width <= 0) continue;
                const int x0 = int(u->x) - sp->originX - ox, y0 = int(u->y) - sp->originY - oy;
                for (int y = 0; y < sp->height; ++y) {
                    const int py = y0 + y;
                    if (py < 0 || py >= baseH_) continue;
                    for (int x = 0; x < sp->width; ++x) {
                        const int px = x0 + x;
                        if (px < 0 || px >= baseW_) continue;
                        const uint8_t* p = &sp->rgba[(size_t(y) * size_t(sp->width) + size_t(x)) * 4];
                        if (!p[3]) continue;
                        uint32_t& d = base_[size_t(py) * size_t(baseW_) + size_t(px)];
                        const int a = p[3];
                        auto mixc = [&](int src, int sh) { return uint32_t((src * a + int((d >> sh) & 255) * (255 - a)) / 255) << sh; };
                        d = mixc(p[0], 0) | mixc(p[1], 8) | mixc(p[2], 16) | 0xFF000000u;
                    }
                }
            }
            return;
        }
        for (int y = 0; y < baseH_; ++y)
            for (int x = 0; x < baseW_; ++x) {
                const float v = 1.0f - float(y) / float(baseH_ - 1);
                float r = v, g = v, b = v;
                if (x < baseW_ - 64) {
                    recolorFromHueSat(float(x) / float(baseW_ - 64), 1, r, g, b);
                    const float l = v * 2;  // (dark to the color to white)
                    auto mix = [l](float c) { return l <= 1 ? c * l : c + (1 - c) * (l - 1); };
                    r = mix(r), g = mix(g), b = mix(b);
                }
                base_[size_t(y) * size_t(baseW_) + size_t(x)] = uint32_t(r * 255) | uint32_t(g * 255) << 8 | uint32_t(b * 255) << 16 | 0xFF000000u;
            }
    }

    // The preview's bitmap of the values (each color once: a map's picture has a few hundred).
    void refreshPicture() {
        if (!picDirty_ && pic_) return;
        picDirty_ = false;
        if (!pic_) pic_ = std::make_unique<draw::Bitmap>(baseW_, baseH_), pic_->opaque = true;
        std::unordered_map<uint32_t, uint32_t> done;
        uint32_t* out = pic_->pixels();
        const bool original = original_.on();
        for (size_t i = 0; i < base_.size(); ++i) {
            const uint32_t c = base_[i];
            if (original) {
                out[i] = c;
                continue;
            }
            auto it = done.find(c);
            if (it == done.end()) {
                uint8_t r = uint8_t(c), g = uint8_t(c >> 8), b = uint8_t(c >> 16);
                stardraft::cmbw::recolorRgb(value_.stops, r, g, b);
                it = done.emplace(c, r | uint32_t(g) << 8 | uint32_t(b) << 16 | 0xFF000000u).first;
            }
            out[i] = it->second;
        }
        pic_->changed();
    }

    draw::Rect place(int x, int y, int w, int h) const { return {scale(x), scale(y), scale(x + w), scale(y + h)}; }
    draw::Rect barRect() const { return place(recolorLayout::kPad, recolorLayout::kBarY, recolorLayout::kLeftW, recolorLayout::kBarH); }
    draw::Rect tintRect() const { return place(recolorLayout::kPad, recolorLayout::kTintY, recolorLayout::kLeftW, recolorLayout::kTintH); }
    int stopX(size_t i) const {
        const draw::Rect b = barRect();
        return b.left + int(recolorStopPlace(i, value_.stops.size()) * float(b.width() - 1));
    }

    void paint(draw::Canvas& c) override {
        using namespace recolorLayout;
        const auto& th = stardraft::kit::theme();
        c.fill({0, 0, width(), height()}, kBg);
        // The stops' gradient: the grays from black to white after the recolor, a mark at each stop's place.
        const draw::Rect bar = barRect();
        if (!barBmp_ || barBmp_->width() != bar.width()) barBmp_ = std::make_unique<draw::Bitmap>(std::max(1, bar.width()), 1), barBmp_->opaque = true;
        for (int x = 0; x < barBmp_->width(); ++x) {
            uint8_t v = uint8_t(x * 255 / std::max(1, barBmp_->width() - 1)), r = v, g = v, b = v;
            stardraft::cmbw::recolorRgb(value_.stops, r, g, b);
            barBmp_->pixels()[x] = r | uint32_t(g) << 8 | uint32_t(b) << 16 | 0xFF000000u;
        }
        barBmp_->changed();
        const int markH = scale(8);
        c.bitmap(*barBmp_, {0, 0, barBmp_->width(), 1}, {bar.left, bar.top, bar.right, bar.bottom - markH});
        c.frame({bar.left, bar.top, bar.right, bar.bottom - markH}, th.border);
        for (size_t i = 0; i < value_.stops.size(); ++i) {
            const int x = stopX(i);
            const draw::Point tri[3] = {{x, bar.bottom - markH}, {x - scale(5), bar.bottom}, {x + scale(5), bar.bottom}};
            c.polygon(tri, 3, i == sel_ ? th.accentBright : th.dim);
        }
        // The hue and saturation square, with a ring at the chosen stop's tint.
        const draw::Rect t = tintRect();
        if (!tintBmp_ || tintBmp_->width() != t.width() || tintBmp_->height() != t.height()) {
            tintBmp_ = std::make_unique<draw::Bitmap>(std::max(1, t.width()), std::max(1, t.height()));
            tintBmp_->opaque = true;
            for (int y = 0; y < tintBmp_->height(); ++y)
                for (int x = 0; x < tintBmp_->width(); ++x) {
                    float r = 1, g = 1, b = 1;
                    recolorFromHueSat(std::min(0.9999f, float(x) / float(std::max(1, tintBmp_->width() - 1))), 1.0f - float(y) / float(std::max(1, tintBmp_->height() - 1)), r, g, b);
                    tintBmp_->pixels()[size_t(y) * size_t(tintBmp_->width()) + size_t(x)] = uint32_t(r * 255) | uint32_t(g * 255) << 8 | uint32_t(b * 255) << 16 | 0xFF000000u;
                }
            tintBmp_->changed();
        }
        c.bitmap(*tintBmp_, {0, 0, tintBmp_->width(), tintBmp_->height()}, t);
        c.frame(t, th.border);
        {
            const stardraft::cmbw::Recolor& r = value_.stops[sel_];
            float h = 0, s = 0;
            recolorHueSat(r.r, r.g, r.b, h, s);
            const int x = t.left + int(h * float(t.width() - 1)), y = t.top + int((1 - s) * float(t.height() - 1));
            const int rr = scale(5);
            c.ring({x - rr, y - rr, x + rr, y + rr}, 0x000000, 3);
            c.ring({x - rr, y - rr, x + rr, y + rr}, 0xFFFFFF, 1);
        }
        // Stop n of m, and the stop's tint (its R, G, B times its gamma, as far as a screen shows it).
        {
            const stardraft::cmbw::Recolor& r = value_.stops[sel_];
            auto chan = [&](float v) { return std::clamp(int(v * r.gamma * 255.0f + 0.5f), 0, 255); };
            const draw::Rect sw = place(kPad + kLabelW, kBarY - 28, 20, 20);
            c.fill(sw, draw::rgb(chan(std::min(1.0f, r.r)), chan(std::min(1.0f, r.g)), chan(std::min(1.0f, r.b))));
            c.frame(sw, th.border);
            const std::wstring n = std::to_wstring(sel_ + 1) + L" / " + std::to_wstring(value_.stops.size()) +
                                   (value_.stops.size() == 1 ? L": " + std::wstring(tr(L"all colors")) : sel_ == 0 ? L": " + std::wstring(tr(L"darkest"))
                                                                : sel_ + 1 == value_.stops.size() ? L": " + std::wstring(tr(L"brightest")) : std::wstring());
            c.text(place(kPad + kLabelW + 28, kBarY - 28, 160, 20), n, th.text);
        }
        // The preview.
        refreshPicture();
        const draw::Rect pr = place(kPicX, kPad, kPicW, kPicH);
        c.bitmap(*pic_, {0, 0, pic_->width(), pic_->height()}, pr);
        c.frame({pr.left - 1, pr.top - 1, pr.right + 1, pr.bottom + 1}, th.border);
        if (copied_) c.text(place(kPicX + kPicW - 160, kValueY - kPitch, 160, kRowH), tr(L"Copied"), th.good, draw::kRight | draw::kMiddle);
    }

    void resized() override {
        using namespace recolorLayout;
        auto put = [this](draw::View& v, int x, int y, int w, int h) { v.setFrame(place(x, y, w, h)); };
        const int fieldX = kPad + kLabelW, wide = kLeftW - kLabelW;
        put(loadLabel_, kPad, kPad, kLabelW, kRowH);
        put(load_, fieldX, kPad, wide, kRowH);
        put(commandLabel_, kPad, kPad + kPitch, kLabelW, kRowH);
        put(command_, fieldX, kPad + kPitch, wide, kRowH);
        put(timeLabel_, kPad, kPad + 2 * kPitch, kLabelW, kRowH);
        put(time_, fieldX, kPad + 2 * kPitch, wide - kFieldW - 8, kRowH);
        put(timeField_, kPad + kLeftW - kFieldW, kPad + 2 * kPitch, kFieldW, kRowH);
        put(stopsLabel_, kPad, kBarY - 28, kLabelW, kRowH - 4);
        put(addStop_, kPad + kLeftW - 56, kBarY - 30, 24, 24);
        put(removeStop_, kPad + kLeftW - 28, kBarY - 30, 24, 24);
        for (int k = 0; k < 5; ++k) {
            const int y = kSlidersY + k * kPitch;
            put(rowLabel_[k], kPad, y, kLabelW, kRowH);
            put(slider_[k], fieldX, y, wide - kFieldW - 8, kRowH);
            put(field_[k], kPad + kLeftW - kFieldW, y, kFieldW, kRowH);
        }
        put(original_, kPicX, kPad + kPicH + 8, 160, kRowH);
        put(valueLabel_, kPad, kValueY, kLabelW, kRowH);
        put(line_, fieldX, kValueY, kPicX + kPicW - fieldX - 80, kRowH);
        put(copy_, kPicX + kPicW - 72, kValueY - 2, 72, kButtonH);
    }

    Editor* ed_ = nullptr;
    std::vector<stardraft::cmbw::RecolorLineRef> lines_;
    stardraft::cmbw::RecolorValue value_;
    size_t sel_ = 0;
    int dragging_ = 0;  // 2: the tint square
    bool picDirty_ = true, copied_ = false;
    std::vector<uint32_t> base_;
    int baseW_ = 0, baseH_ = 0;
    std::unique_ptr<draw::Bitmap> pic_, barBmp_, tintBmp_;
    stardraft::kit::Label loadLabel_, commandLabel_, timeLabel_, stopsLabel_, valueLabel_, rowLabel_[5];
    stardraft::kit::Dropdown load_, command_;
    stardraft::kit::Slider slider_[5], time_;
    stardraft::kit::TextField field_[5], timeField_, line_;
    stardraft::kit::IconButton addStop_, removeStop_;
    stardraft::kit::CheckBox original_;
    stardraft::kit::Button copy_;
};

// The dialog around `view` (Close). Returns the window's size.
inline draw::Point recolorDialogSetup(stardraft::kit::Dialog& dlg, RecolorView& view) {
    using namespace recolorLayout;
    dlg.setContent(view);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"Close"), kRecolorClose, true, true, false, 0});
    dlg.setButtons(std::move(buttons));
    dlg.setInsets({0, 0, kPad, kPad, -(kButtonH + kPad), 8});
    return draw::Point{scale(kWindowW), scale(kWindowH)};
}

inline void runRecolorDialog(draw::View& from, Editor* ed) {
    RecolorView view(ed);
    stardraft::kit::Dialog dlg;
    const draw::Point size = recolorDialogSetup(dlg, view);
    dlg.run(from, tr(L"Recolor preview"), size);
}
