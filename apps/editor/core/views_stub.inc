// Triggers, Assets and Project: one window that says what each holds (portable, core.inc): moved from main.cpp. Project
// already lists the real project: its folder, map, output, and what the .eds builds. The stub as a view
// (apps/common/view.h): its window is a view window (kViewClass) that shows gStubView (the instance is in main.cpp).

// Defined in main.cpp (the Win32 TU until they move into the core).
stardraft::EudProject* activeProject();

struct StubView final : stardraft::draw::View {
    void paint(stardraft::draw::Canvas& c) override {
        using stardraft::draw::Face;
        using stardraft::draw::FontDesc;
        using stardraft::draw::Rect;
        const int w = width(), h = height();
        c.fill({0, 0, w, h}, kBg);
        std::wstring title, text;
        if (gWorkspace == Workspace::Triggers) {
            title = L"Triggers";
            text = L"Triggers are in the Triggers workspace of a project.";
        } else if (gWorkspace == Workspace::Assets) {
            title = L"Assets";
            text = L"Not available yet: graphics, sounds, portraits and fonts that you import into the map.";
        } else if (activeProject()) {
            title = L"Project: " + widen(activeProject()->name());
            text.clear();  // the settings are the form's rows
        } else {
            title = L"Project";
            text = L"No project is open. File > Project > New project makes one from the open map. File > Project > Open project opens one.";
        }
        // The title: the UI font at 3/2 of its height; the text: the UI font.
        const stardraft::draw::Font ui = c.font({Face::Ui, 0});
        const stardraft::draw::Font old = c.setFont(c.font({Face::Ui, titleHeight()}));
        c.text({scale(40), scale(36), w - scale(40), scale(80)}, title, kText, stardraft::draw::kLeft);
        c.setFont(ui);
        c.text({scale(40), scale(84), w - scale(40), h}, text, kDim, stardraft::draw::kWrap | stardraft::draw::kLeft);
        c.setFont(old);
    }
    // The title's font height: the UI font's, at 3/2 (the old stub's big font).
    static int titleHeight() { return -(shellUiFontLfHeight() * 3 / 2); }
};
