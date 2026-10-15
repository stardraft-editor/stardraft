// Help > About StarDraft: the Windows host of the core's view (core/views_about.inc: AboutView). Included by main.cpp
// after options_window.inc: the window opens through the program's anchor (Host::openWindow, the Dialog frame of the
// program's other windows), with the program's icon in its title bar.

AboutView gAboutView;

// The About window's HWND (nullptr: it is not open).
HWND aboutWindow() { return ui::Win32Host::windowOf(gAboutView); }

// Help > About StarDraft (one window; a second click brings it to the front).
void showAbout() {
    if (HWND w = aboutWindow()) {
        SetForegroundWindow(w);
        return;
    }
    gAboutView.hot = -1;
    const stardraft::draw::Point sz = AboutView::size();
    gMainAnchor.host.ownerWindow(app.main);
    gMainAnchor.host.openWindow(gMainAnchor.view, gAboutView,
                                stardraft::draw::WindowSpec{stardraft::draw::WindowKind::Dialog, stardraft::draw::Rect{0, 0, sz.x, sz.y}, L"About StarDraft 2"},
                                nullptr);
    HWND w = aboutWindow();
    if (!w) return;
    // The program's icon in the title bar (the view class has none).
    static HICON smallIcon = nullptr;
    if (!smallIcon) smallIcon = HICON(LoadImageW(app.inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    SendMessageW(w, WM_SETICON, ICON_BIG, LPARAM(LoadIconW(app.inst, MAKEINTRESOURCEW(1))));
    SendMessageW(w, WM_SETICON, ICON_SMALL, LPARAM(smallIcon));
    UpdateWindow(w);
}
