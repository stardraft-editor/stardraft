// A view with a kit scroll bar at its right, for the core's views that keep their own scroll (windowScrollBar and
// vscroll: the Windows window's own bar was theirs): the Data workspace's list, form and References, the Triggers pane,
// the Assets page, the Project page, the map pages and the Classic triggers' lists. Included by main_sdl.cpp after
// sdl_main_view.inc. The bar shows only while the view is taller than its place.

class SdlBarFrame final : public draw::View {
public:
    explicit SdlBarFrame(draw::View& content) : content_(content), bar_(content) {
        bar_.onScroll = [this](int pos) {
            int range = 0, page = 0, now = 0;
            content_.windowScrollBar(range, page, now);
            content_.vscroll(5, pos, pos, page);  // (SB_THUMBTRACK: the position)
            content_.invalidate();
        };
        add(content_, {0, 0, 0, 0});
        add(bar_, {0, 0, 0, 0});
    }
    static int barW() { return scale(stardraft::kit::kScrollBarW); }
    void resized() override {
        content_.setFrame({0, 0, std::max(0, width() - barW()), height()});
        bar_.setFrame({width() - barW(), 0, width(), height()});
    }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }
    bool mouse(const draw::MouseEvent& e) override {  // the wheel over the bar scrolls the view
        return e.kind == draw::MouseKind::Wheel && content_.mouse(e);
    }

private:
    // The bar follows the view as the view's paint just measured it: the bar paints after the view (the frame's second
    // child), so it is never one frame behind.
    struct Bar final : stardraft::kit::ScrollBar {
        explicit Bar(draw::View& content) : content(content) {}
        void paint(draw::Canvas& c) override {
            int range = 0, page = 0, pos = 0;
            if (content.windowScrollBar(range, page, pos)) {
                if (range + 1 != total || page != shown) total = range + 1, shown = page, setRange(total, shown);
                setPos(pos);
            }
            stardraft::kit::ScrollBar::paint(c);
        }
        draw::View& content;
        int total = -1, shown = -1;
    };
    draw::View& content_;
    Bar bar_;
};

