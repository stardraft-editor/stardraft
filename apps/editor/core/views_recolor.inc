// Tools > Cosmonarchy > Recolor preview (portable, core.inc): find the colors for the recolor CRIG (src/mods/cmbw_recolor.cpp:
// recolor, recolor_loop_add), then Copy its line into a trigger's Comment. A control for each value; the map view shows
// the recolor (the renderer's color map: the game's palette change) while the window is open, or while Keep recolor
// preview is on. A tool window beside the map (the host: sdl_options.inc shellShowRecolorWindow).

namespace recolorLayout {
constexpr int kPad = 16;
constexpr int kW = 400;                            // the controls
constexpr int kLabelW = 76, kFieldW = 60;          // a row's name and its number field
constexpr int kRowH = 24, kPitch = 32;
constexpr int kStopsY = kPad + 2 * kPitch + 16;    // 96: Stops, the stop's color, add and remove
constexpr int kBarY = kStopsY + 28, kBarH = 28;    // the stops' gradient
constexpr int kTintY = kBarY + kBarH + 12, kTintH = 96;
constexpr int kSlidersY = kTintY + kTintH + 12;    // R, G, B, Gamma, Saturation
constexpr int kKeepY = kSlidersY + 5 * kPitch + 4;
constexpr int kValueY = kKeepY + kPitch + 12;      // CRIG, then its line and Copy
constexpr int kLineY = kValueY + 24;
constexpr int kButtonH = 28;
constexpr int kWindowW = kW + 2 * kPad;
constexpr int kLineH = 48;                         // the line: two rows (it wraps)
constexpr int kWindowH = kLineY + kLineH + kPad;
}  // namespace recolorLayout

// The preview's values and whether the map shows them (the map view's frame reads it: sdl_main_view.inc).
struct RecolorPreviewState {
    stardraft::cmbw::RecolorValue value;
    bool windowOpen = false;
    bool keep = false;  // Keep recolor preview: the map shows it also when the window is closed
    bool paused = false;  // (File > Export map image: the map's own colors in the picture)
    bool shown() const { return (windowOpen || keep) && !paused; }
};
inline RecolorPreviewState& recolorPreview() {
    static RecolorPreviewState s;
    return s;
}

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
    RecolorView() {
        using stardraft::kit::LabelTone;
        auto label = [this](stardraft::kit::Label& l, const wchar_t* t) {
            l.setText(tr(t));
            l.setTone(LabelTone::Dim);
            add(l, {0, 0, 0, 0});
        };
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
        numberField(timeField_, [this](float v) { value_.seconds = std::round(double(v) * 1000.0) / 1000.0, sync(); });

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

        keep_.setLabel(tr(L"Keep recolor preview"));
        keep_.setPlainLabel(true);
        keep_.setTip(tr(L"The map shows the recolor also when this window is closed"));
        keep_.onChange = [](bool on) { recolorPreview().keep = on, appState().dirty = true; };
        add(keep_, {0, 0, 0, 0});
        line_.setReadOnly(true);
        line_.setMultiline(true), line_.setWrap(true);  // (the whole line shows: a recolor of 3 stops is long)
        add(line_, {0, 0, 0, 0});
        reset_.setText(tr(L"Reset"));
        reset_.setTip(tr(L"One stop, every value 1, time 0 (no change of the colors)"));
        reset_.onClick = [this] { sel_ = 0, setValue({}); };
        add(reset_, {0, 0, 0, 0});
        copy_.setText(tr(L"Copy"));
        copy_.onClick = [this] { copyLine(); };
        add(copy_, {0, 0, 0, 0});
        setValue(recolorPreview().value);
    }

    static draw::Point size() { return {scale(recolorLayout::kWindowW), scale(recolorLayout::kWindowH)}; }
    // The window opens: the values and Keep as the map has them now.
    void opened() {
        keep_.setOn(recolorPreview().keep);
        setValue(recolorPreview().value);
    }
    const stardraft::cmbw::RecolorValue& value() const { return value_; }
    std::string lineText() const { return stardraft::cmbw::recolorLineText(value_); }
    size_t selectedStop() const { return sel_; }
    // A snapshot's or a test's input: a line's values, stop i chosen.
    void startWith(const std::string& line, size_t stop) {
        stardraft::cmbw::RecolorValue v;
        if (!line.empty()) stardraft::cmbw::parseRecolorLine(line, v);
        setValue(v);
        if (stop < value_.stops.size()) sel_ = stop, sync();
    }
    // Copy: the line to the clipboard (for a trigger's Comment).
    void copyLine() {
        commitFields();
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
        if (bar.contains(e.pt) || tint.contains(e.pt)) commitFields();  // (a number typed without Enter goes to the stop it was typed for)
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
    // The fields with the focus take their typed numbers (the view takes no focus, so a click in it does not end them).
    void commitFields() {
        for (auto& c : commits_)
            if (c.first->hasFocus()) c.second();
    }
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
        commits_.push_back({&f, commit});
        add(f, {0, 0, 0, 0});
    }

    stardraft::cmbw::Recolor& stop() { return value_.stops[sel_]; }
    float& part(int k) {
        stardraft::cmbw::Recolor& r = stop();
        return k == 0 ? r.r : k == 1 ? r.g : k == 2 ? r.b : k == 3 ? r.gamma : r.saturation;
    }
    // (a typed number rounded as the line writes it: the map shows what Copy gives)
    void setPart(int k, float v, bool typed) {
        part(k) = typed ? std::round(v * 1000.0f) / 1000.0f : std::round(v * 100.0f) / 100.0f;
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

    // The controls, the line and the map's preview after a change.
    void sync() {
        auto num = [](double v) {
            std::wstring s = widen(stardraft::cmbw::recolorLineText({"recolor", {{float(v), 1, 1, 1, 1}}, 0}));
            const size_t a = s.find(L'(') + 1;  // (the line's way of writing a number)
            return s.substr(a, s.find(L' ', a) - a);
        };
        for (int k = 0; k < 5; ++k) {
            slider_[k].setValue(int(std::lround(part(k) * 100.0f)));
            if (!field_[k].hasFocus() || shownSel_ != sel_) field_[k].setText(num(part(k)));
        }
        time_.setValue(int(std::lround(value_.seconds * 4)));
        if (!timeField_.hasFocus()) timeField_.setText(num(value_.seconds));
        shownSel_ = sel_;
        addStop_.setEnabled(value_.stops.size() < stardraft::cmbw::kRecolorStops);
        removeStop_.setEnabled(value_.stops.size() > 1);
        line_.setText(widen(lineText()));
        copied_ = false;
        recolorPreview().value = value_;
        appState().dirty = true;  // (the map view again, with these colors)
        invalidate();
    }

    draw::Rect place(int x, int y, int w, int h) const { return {scale(x), scale(y), scale(x + w), scale(y + h)}; }
    draw::Rect barRect() const { return place(recolorLayout::kPad, recolorLayout::kBarY, recolorLayout::kW, recolorLayout::kBarH); }
    draw::Rect tintRect() const { return place(recolorLayout::kPad, recolorLayout::kTintY, recolorLayout::kW, recolorLayout::kTintH); }
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
            const draw::Rect sw = place(kPad + kLabelW, kStopsY + 2, 20, 20);
            c.fill(sw, draw::rgb(chan(std::min(1.0f, r.r)), chan(std::min(1.0f, r.g)), chan(std::min(1.0f, r.b))));
            c.frame(sw, th.border);
            const std::wstring n = std::to_wstring(sel_ + 1) + L" / " + std::to_wstring(value_.stops.size()) +
                                   (value_.stops.size() == 1 ? L": " + std::wstring(tr(L"all colors")) : sel_ == 0 ? L": " + std::wstring(tr(L"darkest"))
                                                                : sel_ + 1 == value_.stops.size() ? L": " + std::wstring(tr(L"brightest")) : std::wstring());
            c.text(place(kPad + kLabelW + 28, kStopsY + 2, 200, 20), n, th.text);
        }
        if (copied_) c.text(place(kPad + kW - 160, kValueY, 160, 20), tr(L"Copied"), th.good, draw::kRight | draw::kMiddle);
    }

    void resized() override {
        using namespace recolorLayout;
        auto put = [this](draw::View& v, int x, int y, int w, int h) { v.setFrame(place(x, y, w, h)); };
        const int fieldX = kPad + kLabelW, wide = kW - kLabelW;
        put(commandLabel_, kPad, kPad, kLabelW, kRowH);
        put(command_, fieldX, kPad, wide, kRowH);
        put(timeLabel_, kPad, kPad + kPitch, kLabelW, kRowH);
        put(time_, fieldX, kPad + kPitch, wide - kFieldW - 8, kRowH);
        put(timeField_, kPad + kW - kFieldW, kPad + kPitch, kFieldW, kRowH);
        put(stopsLabel_, kPad, kStopsY, kLabelW, kRowH);
        put(addStop_, kPad + kW - 56, kStopsY, 24, 24);
        put(removeStop_, kPad + kW - 28, kStopsY, 24, 24);
        for (int k = 0; k < 5; ++k) {
            const int y = kSlidersY + k * kPitch;
            put(rowLabel_[k], kPad, y, kLabelW, kRowH);
            put(slider_[k], fieldX, y, wide - kFieldW - 8, kRowH);
            put(field_[k], kPad + kW - kFieldW, y, kFieldW, kRowH);
        }
        put(keep_, kPad, kKeepY, kW - 88, kRowH);
        put(reset_, kPad + kW - 72, kKeepY - 2, 72, kButtonH);
        put(valueLabel_, kPad, kValueY, kLabelW, 20);
        put(line_, kPad, kLineY, kW - 80, kLineH);
        put(copy_, kPad + kW - 72, kLineY, 72, kButtonH);
    }

    stardraft::cmbw::RecolorValue value_;
    size_t sel_ = 0, shownSel_ = 0;  // (shownSel_: the stop the fields show)
    std::vector<std::pair<stardraft::kit::TextField*, std::function<void()>>> commits_;
    int dragging_ = 0;  // 2: the tint square
    bool copied_ = false;
    std::unique_ptr<draw::Bitmap> barBmp_, tintBmp_;
    stardraft::kit::Label commandLabel_, timeLabel_, stopsLabel_, valueLabel_, rowLabel_[5];
    stardraft::kit::Dropdown command_;
    stardraft::kit::Slider slider_[5], time_;
    stardraft::kit::TextField field_[5], timeField_, line_;
    stardraft::kit::IconButton addStop_, removeStop_;
    stardraft::kit::CheckBox keep_;
    stardraft::kit::Button copy_, reset_;
};

// The one window's view (made at its first use: after the program's start, its language and settings).
inline RecolorView& recolorView() {
    static RecolorView v;
    return v;
}
void shellShowRecolorWindow();  // (sdl_options.inc: the window, once)
void recolorShow() {
    recolorView().opened();
    shellShowRecolorWindow();
}
