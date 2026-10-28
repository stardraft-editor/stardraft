#pragma once
// For tests of the kit's widgets without a window: a Canvas that writes down what is drawn (text measures 7 px per
// character, lines are 16 px high), and a Host that keeps the focus, the capture, the clipboard, timers and windows in
// memory. tools/tests/kit_test.cpp uses them; so can any test of a view.

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "canvas.h"
#include "view.h"

namespace stardraft::kit::test {

using namespace stardraft::draw;

inline std::string utf8(std::wstring_view w) {
    std::string o;
    for (wchar_t c : w) {
        const uint32_t u = uint32_t(c);
        if (u < 0x80) o += char(u);
        else if (u < 0x800) o += char(0xC0 | (u >> 6)), o += char(0x80 | (u & 63));
        else o += char(0xE0 | (u >> 12)), o += char(0x80 | ((u >> 6) & 63)), o += char(0x80 | (u & 63));
    }
    return o;
}

class RecordingCanvas final : public Canvas {
public:
    std::vector<std::string> ops;  // one line per call: "text 10,4,80,20 'OK' 0xe0e0e0 5"
    Point origin{0, 0};
    int charW = 7, lineH = 16;

    std::string r(Rect x) const {
        char b[64];
        std::snprintf(b, sizeof b, "%d,%d,%d,%d", x.left + origin.x, x.top + origin.y, x.right + origin.x, x.bottom + origin.y);
        return b;
    }
    static std::string c(Color x) {
        char b[16];
        std::snprintf(b, sizeof b, "0x%06x", unsigned(x & 0xFFFFFF));
        return x == kNone ? "none" : b;
    }
    // The texts drawn, in order.
    std::vector<std::string> texts() const {
        std::vector<std::string> out;
        for (const auto& o : ops)
            if (o.rfind("text ", 0) == 0) {
                const size_t a = o.find('\''), b = o.rfind('\'');
                out.push_back(o.substr(a + 1, b - a - 1));
            }
        return out;
    }
    bool drew(const std::string& part) const {
        return std::any_of(ops.begin(), ops.end(), [&](const std::string& o) { return o.find(part) != std::string::npos; });
    }

    void fill(Rect x, Color k, uint8_t a) override { ops.push_back("fill " + r(x) + " " + c(k) + " " + std::to_string(a)); }
    void roundBox(Rect x, Color f, Color l, float rad, int w) override {
        const std::string rs = rad == float(int(rad)) ? std::to_string(int(rad)) : std::to_string(rad);
        ops.push_back("roundBox " + r(x) + " " + c(f) + " " + c(l) + " " + rs + " " + std::to_string(w));
    }
    void smoothRoundOutline(Rect x, float, Color l, float) override { ops.push_back("smoothRoundOutline " + r(x) + " " + c(l)); }
    void ellipse(Rect x, Color f, Color l) override { ops.push_back("ellipse " + r(x) + " " + c(f) + " " + c(l)); }
    void ring(Rect x, Color l, int, LineStyle) override { ops.push_back("ring " + r(x) + " " + c(l)); }
    void frame(Rect x, Color l, int, LineStyle) override { ops.push_back("frame " + r(x) + " " + c(l)); }
    void line(int x0, int y0, int x1, int y1, Color k, int, LineStyle) override {
        ops.push_back("line " + r({x0, y0, x1, y1}) + " " + c(k));
    }
    void polyline(const Point*, int n, Color k, int, LineStyle) override { ops.push_back("polyline " + std::to_string(n) + " " + c(k)); }
    void polygon(const Point*, int n, Color f, Color) override { ops.push_back("polygon " + std::to_string(n) + " " + c(f)); }
    void pushClip(Rect x) override { ops.push_back("clip " + r(x)); }
    void popClip() override { ops.push_back("unclip"); }
    void pushView(Rect x) override {
        ops.push_back("view " + r(x));
        stack_.push_back(origin);
        origin = {origin.x + x.left, origin.y + x.top};
    }
    void popView() override {
        ops.push_back("unview");
        if (!stack_.empty()) origin = stack_.back(), stack_.pop_back();
    }
    void text(Rect x, std::wstring_view t, Color k, unsigned flags) override {
        ops.push_back("text " + r(x) + " '" + utf8(t) + "' " + c(k) + " " + std::to_string(flags));
    }
    int measure(std::wstring_view t) override { return int(t.size()) * charW; }
    Point measureBox(std::wstring_view t, int width, unsigned flags) override {
        const int w = measure(t);
        if (!(flags & kWrap) || width <= 0) return {w, lineH};
        const int lines = std::max(1, (w + width - 1) / width);
        return {std::min(w, width), lines * lineH};
    }
    FontMetrics metrics() override { return {lineH, 4}; }
    Font setFont(Font f) override {
        Font old = font_;
        font_ = f;
        return old;
    }
    Font font(const FontDesc& d) override { return Font{reinterpret_cast<const void*>(uintptr_t(1 + int(d.face) * 1000 + d.px * 4 + (d.bold ? 1 : 0)))}; }
    void bitmap(Bitmap& b, Rect, Rect dst, bool, uint8_t) override { ops.push_back("bitmap " + std::to_string(b.width()) + "x" + std::to_string(b.height()) + " " + r(dst)); }
    void bitmapQuad(Bitmap& b, const PointF*, const PointF*, uint8_t) override { ops.push_back("bitmapQuad " + std::to_string(b.width()) + "x" + std::to_string(b.height())); }
    void icon(Rect x, int i, Color k, int) override { ops.push_back("icon " + r(x) + " " + std::to_string(i) + " " + c(k)); }
    void fitted(const Image&, Rect box) override { ops.push_back("fitted " + r(box)); }

private:
    std::vector<Point> stack_;
    Font font_;
};

// A host in memory. Events go in through its router: host.click(10, 10), host.type("abc"), host.press(Key::Enter).
class FakeHost final : public Host {
public:
    explicit FakeHost(View& root, int w = 800, int h = 600) : router(root) {
        root.attach(this);
        root.setFrame({0, 0, w, h});
    }
    ~FakeHost() override { router.root().attach(nullptr); }
    Router router;
    Point mousePos{0, 0};
    Cursor cursorNow = Cursor::Arrow;
    std::string clipboard;
    int invalidations = 0;
    struct Timer {
        int ms;
        std::function<void()> run;
    };
    std::map<int, Timer> timers;
    struct Window {
        View* from;
        View* content;
        WindowSpec spec;
        std::function<void()> closed;
    };
    std::vector<Window> windows;
    bool textInputOn = false;
    Rect caret;

    void invalidate(View&) override { ++invalidations; }
    void focus(View& v) override { router.setFocus(&v); }
    bool hasFocus(const View& v) const override { return router.focused() == &v; }
    void capture(View& v, bool on) override { router.setCapture(on ? &v : nullptr); }
    Point mouse(const View& v) const override {
        const Point o = v.origin();
        return {mousePos.x - o.x, mousePos.y - o.y};
    }
    void setCursor(Cursor c) override { cursorNow = c; }
    std::string clipboardText() override { return clipboard; }
    void setClipboardText(std::string_view t) override { clipboard = std::string(t); }
    int timer(int ms, std::function<void()> run) override {
        timers[++timerId_] = {ms, std::move(run)};
        return timerId_;
    }
    void cancelTimer(int id) override { timers.erase(id); }
    void textInput(View&, bool on, Rect c) override { textInputOn = on, caret = c; }
    float scale() const override { return 1; }
    void openWindow(View& from, View& content, const WindowSpec& spec, std::function<void()> closed) override {
        windows.push_back({&from, &content, spec, std::move(closed)});
        content.setFrame({0, 0, spec.at.width(), spec.at.height()});
    }
    void closeWindow(View& content) override {
        windows.erase(std::remove_if(windows.begin(), windows.end(), [&](const Window& w) { return w.content == &content; }), windows.end());
    }
    void runModal(View& from, View& content, const WindowSpec& spec) override { openWindow(from, content, spec, nullptr); }
    void forget(const View& v) override { router.forget(v); }

    // Runs every timer that is due (all of them).
    void runTimers() {
        auto ts = std::move(timers);
        timers.clear();
        for (auto& [id, t] : ts) t.run();
    }
    void move(int x, int y) {
        mousePos = {x, y};
        MouseEvent e;
        e.kind = MouseKind::Move, e.pt = mousePos;
        router.mouse(e);
    }
    void down(int x, int y, int button = 0, unsigned mods = 0) {
        mousePos = {x, y};
        MouseEvent e;
        e.kind = MouseKind::Down, e.pt = mousePos, e.button = button, e.mods = mods;
        router.mouse(e);
    }
    void up(int x, int y, int button = 0, unsigned mods = 0) {
        mousePos = {x, y};
        MouseEvent e;
        e.kind = MouseKind::Up, e.pt = mousePos, e.button = button, e.mods = mods;
        router.mouse(e);
    }
    void click(int x, int y, unsigned mods = 0) { down(x, y, 0, mods), up(x, y, 0, mods); }
    void doubleClick(int x, int y) {
        click(x, y);
        MouseEvent e;
        e.kind = MouseKind::DoubleClick, e.pt = {x, y};
        router.mouse(e);
        up(x, y);
    }
    void wheel(int x, int y, float notches) {
        MouseEvent e;
        e.kind = MouseKind::Wheel, e.pt = {x, y}, e.wheel = notches;
        router.mouse(e);
    }
    bool press(Key k, unsigned mods = 0) {
        KeyEvent e;
        e.key = k, e.mods = mods;
        const bool used = router.key(e);
        e.down = false;
        router.key(e);
        return used;
    }
    void type(const std::string& utf8Text) { router.text({utf8Text}); }
    void compose(const std::string& utf8Text, int cursor) { router.compose({utf8Text, cursor}); }
    // The root and the open windows, painted.
    RecordingCanvas paint() {
        RecordingCanvas c;
        router.root().paintAll(c);
        return c;
    }
    RecordingCanvas paintWindow(size_t i) {
        RecordingCanvas c;
        windows.at(i).content->paintAll(c);
        return c;
    }

private:
    int timerId_ = 0;
};

}  // namespace stardraft::kit::test
