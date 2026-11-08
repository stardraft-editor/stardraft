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
    // The bar's room only while the view scrolls: the view's rows reach the frame's right edge, as the fields above
    // them (for views with rows of a fixed height: a view whose height follows its width could change each frame).
    void setBarOnlyWhenScrolls(bool on) { autoBar_ = on, resized(); }
    void resized() override {
        const int w = autoBar_ && !scrolls_ ? 0 : barW();
        content_.setFrame({0, 0, std::max(0, width() - w), height()});
        bar_.setFrame({width() - w, 0, width(), height()});
    }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        int range = 0, page = 0, pos = 0;  // (as the view's last paint measured it; a change shows in the next frame)
        const bool scrolls = content_.windowScrollBar(range, page, pos) && page > 0 && range + 1 > page;
        if (autoBar_ && scrolls != scrolls_) scrolls_ = scrolls, resized(), invalidate();
    }
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
    bool autoBar_ = false, scrolls_ = true;
};

