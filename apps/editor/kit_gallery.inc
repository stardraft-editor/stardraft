// View > Control kit: the Windows host of the core's gallery (core/views_kit_gallery.inc: KitGalleryView). The window is
// a view window (ui::kViewClass) with a resizable frame and its own scroll bar in the theme; the host gives the page the
// entries' icons and the multi-pick list (showListPickerMulti).

KitGalleryView gKitView;

// The gallery's window (nullptr: it is not open).
HWND kitGalleryWindow() { return ui::Win32Host::windowOf(gKitView); }

// View > Control kit: the gallery in its own window (background test runs keep it off the screen).
void showKitGallery() {
    if (HWND w = kitGalleryWindow()) return void(SetForegroundWindow(w));
    gKitView.refIcon = [](const char* dat, int id) { return ui::imageOf(datRefIcon(dat, id)); };
    gKitView.pickValues = [] {
        RECT rc{};
        GetWindowRect(kitGalleryWindow(), &rc);
        return showListPickerMulti(L"Search units", kitGalleryPickItems(), kitGalleryPicked(), L"Add", POINT{rc.left + 40, rc.top + 120});
    };
    gKitView.scroll = 0;
    int x = CW_USEDEFAULT, yy = CW_USEDEFAULT;
    if (gBackground) x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 100, yy = 0;
    HWND w = CreateWindowExW(gBackground ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0, ui::kViewClass, L"Control kit", WS_OVERLAPPEDWINDOW | WS_VSCROLL, x, yy,
                             scale(1180), scale(1000), app.main, nullptr, app.inst, &gKitView);
    if (!w) return;
    BOOL dark = !gLightTheme;
    DwmSetWindowAttribute(w, 20, &dark, sizeof dark), titleColors(w);
    cleanScroll(w);
    darkControl(w, L"DarkMode_Explorer");  // the scroll bar in the theme
    ShowWindow(w, gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
}
