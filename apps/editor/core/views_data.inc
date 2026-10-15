// The Data workspace's tab strip (portable, core.inc): the layout of the .dat tabs above the entry list. Each tab is its
// icon, its name and its entry count; the strip drops the counts, then the icons, when the tabs do not fit before the
// back and forward arrows. The program measures the text (its font) and gives the metrics in pixels; the strip's paint,
// its hot tab and the click stay in main.cpp (dataWndProc), which reads the boxes from here.

// One tab's measured text: its name's width, and its count's width (the count as the strip would show it).
struct DataTabText {
    int nameW = 0;
    int countW = 0;
};

// The strip's metrics in pixels (scaled by the program).
struct DataTabMetrics {
    int startX = 0;      // the first tab's left edge
    int reserved = 0;    // the room kept for the back and forward arrows (taken off the strip's width)
    int iconRoom = 0;    // the room one tab's icon takes in the fit test
    int iconW = 0;       // the icon and the gap after it (when the icons show)
    int pad = 0;         // a tab's padding around its name and count
    int gap = 0;         // the gap after each tab
};

// The layout: whether the counts and the icons show, and each tab's box (left and right edges, in order).
struct DataTabLayout {
    bool showCounts = true;
    bool showIcons = true;
    std::vector<std::pair<int, int>> boxes;  // (left, right) per tab
};

// What fits before the arrows: the names with their pads and gaps, the counts, and the icons; without the counts
// first, then without the icons too. A hidden count is 0 wide.
DataTabLayout dataTabLayout(const std::vector<DataTabText>& tabs, int width, const DataTabMetrics& m) {
    DataTabLayout out;
    int names = 0, counts = 0;
    for (const DataTabText& t : tabs) {
        names += t.nameW + m.pad + m.gap, counts += t.countW;
    }
    const int room = width - m.reserved, icons = int(tabs.size()) * m.iconRoom;
    if (names + counts + icons > room) out.showCounts = false;
    if (names + icons > room) out.showIcons = false;
    int x = m.startX;
    for (const DataTabText& t : tabs) {
        const int iw = out.showIcons ? m.iconW : 0;
        const int cw = out.showCounts ? t.countW : 0;
        const int right = x + iw + t.nameW + cw + m.pad;
        out.boxes.push_back({x, right});
        x = right + m.gap;
    }
    return out;
}
