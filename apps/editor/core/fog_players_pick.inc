// The fog tools' players (Paint fog, Erase fog: core/fog_layer.inc; portable, core.inc): the tool strip's player box
// (core/views_toolstrip.inc) opens this list under it. Each click on a player adds it or removes it at once (one stays
// checked at least); the Players heading checks all 8, or clears them to the first one checked.

void fogPlayersPick(draw::View& from, draw::Rect box) {
    const std::wstring section = tr(L"Players");
    std::vector<ListPickItem> items;
    std::set<std::string> checked;
    for (int p = 0; p < 8; ++p) {
        wchar_t buf[48];
        std::swprintf(buf, 48, tr(L"Player %d"), p + 1);
        items.push_back({buf, section, std::to_string(p), "", -1});
        if (fogToolBits() >> p & 1) checked.insert(std::to_string(p));
    }
    ListPickView v(tr(L"Search players"), std::move(items), std::string());
    v.setLive(std::move(checked), tr(L"Close"), [](const std::string& value, bool on, std::set<std::string>& checks) {
        const uint8_t was = fogToolBits();
        if (value.rfind(kListPickGroup, 0) == 0) {  // the heading: all 8, or only the first of them
            checks.clear();
            int first = 0;
            while (first < 7 && !(was >> first & 1)) ++first;
            for (int p = 0; p < 8; ++p)
                if (on || p == first) checks.insert(std::to_string(p));
        }
        uint8_t bits = 0;
        for (const std::string& s : checks)
            if (s.size() == 1 && s[0] >= '0' && s[0] <= '7') bits |= uint8_t(1u << (s[0] - '0'));
        if (!bits) {  // (one player at least: the last one stays checked)
            bits = was;
            for (int p = 0; p < 8; ++p)
                if (was >> p & 1) checks.insert(std::to_string(p));
        }
        gFogTool.players = bits;
        setMessage(fogText(tr(L"Fog of war: %ls")));
        shellToolsChanged();
        appState().dirty = true;
    });
    v.setGroups(true);
    const draw::Point o = from.origin();
    runListPick(v, {o.x + box.left, o.y + box.bottom + scale(2)});
    shellFocusView();
}
