// Help > About StarDraft (portable, core.inc): the app icon large, the version, the thanks (with links to IsomTerrain
// and the EUD tools), the GitHub repository and the Discord server. The content is a view (AboutView) in a Dialog
// window that a host opens with Host::openWindow (one window; it does not stop the program). The Windows host is
// about.inc; the SDL program opens it from sdl_menus.inc.
//
// The links open in the browser (shellOpenUrl); the Close button and Esc or Enter close the window.

// The program's Discord server and GitHub repository (crash.inc's kRepo is the same repository).
inline const wchar_t* const kDiscordInvite = L"https://discord.gg/JBMCwSRJBm";

class AboutView final : public draw::View {
public:
    struct Link {
        draw::Rect rc;
        std::wstring url;
    };
    std::vector<Link> links;  // from the last paint: the thanks' names and projects, the repository, Discord
    draw::Rect closeRc;
    int hot = -1;  // link index, or -2 for Close
    std::function<void()> closed;  // the view closed its window (Close, Esc, Enter)

    // The window's size at 100 % (the lines about the name and the EUD tools fit).
    static draw::Point size() { return {scale(600), scale(668)}; }  // (the thanks, the lines about Blizzard and the links fit)

    void paint(draw::Canvas& c) override {
        using draw::Color;
        using draw::Font;
        using draw::FontDesc;
        using draw::Rect;
        const int w = width(), h = height();
        c.fill({0, 0, w, h}, kPanel);

        // The icon, as large as the window allows (the .ico has a 256 px image).
        const int px = scale(160);
        int y = scale(28);
        c.resourceIcon({(w - px) / 2, y, (w - px) / 2 + px, y + px}, 1, px);
        y += px + scale(16);

        auto centered = [&](const std::wstring& t, Font f, Color col, int hgt) {
            const Font of = c.setFont(f);
            c.text({0, y, w, y + hgt}, t, col, draw::kCenter | draw::kMiddle);
            c.setFont(of);
            y += hgt;
        };
        FontDesc title;  // the UI font at twice its height, semibold
        title.px = -shellUiFontLfHeight() * 2, title.weight = 600;
        centered(L"StarDraft 2", c.font(title), kText, scale(40));
        const Font smallText = c.font({});
        centered(L"v" + shellVersion() + (shellBuildStamp().empty() ? L"" : L"  (" + shellBuildStamp() + L")"), smallText, kDim, scale(22));
        centered(L"A map creation tool for EUD-powered maps for StarCraft: Remastered", smallText, kDim, scale(22));
        // The thanks: the program StarDraft 2 is named after, and the projects it is built on. A line's names are links
        // (underlined when the mouse is on them); a translation without the names shows the text only.
        links.clear();
        auto linkLine = [&](const std::wstring& english, const std::vector<std::pair<std::wstring, std::wstring>>& names) {
            const std::wstring line = tr(english);
            struct Piece {
                std::wstring text;
                int link;  // -1: text, else the link's place in the line (0 first)
            };
            // The names where the line has them (a translation may have them in another order).
            std::vector<std::pair<size_t, int>> found;
            for (size_t k = 0; k < names.size(); ++k) {
                const size_t f = line.find(names[k].first);
                if (f == std::wstring::npos) {
                    centered(line, smallText, kDim, scale(22));
                    return;
                }
                found.push_back({f, int(k)});
            }
            std::sort(found.begin(), found.end());
            std::vector<Piece> pieces;
            std::vector<std::wstring> urls;  // the line's links, left to right
            size_t at = 0;
            for (const auto& [f, k] : found) {
                if (f < at) {  // (two names overlap)
                    centered(line, smallText, kDim, scale(22));
                    return;
                }
                pieces.push_back({line.substr(at, f - at), -1});
                pieces.push_back({names[size_t(k)].first, int(urls.size())});
                urls.push_back(names[size_t(k)].second);
                at = f + names[size_t(k)].first.size();
            }
            pieces.push_back({line.substr(at), -1});
            const size_t first = links.size();
            auto pieceFont = [&](const Piece& p) {
                if (p.link < 0) return smallText;
                FontDesc d;
                d.underline = hot == int(first) + p.link;
                return c.font(d);
            };
            int total = 0;
            for (const Piece& p : pieces) {
                if (p.text.empty()) continue;
                const Font of = c.setFont(pieceFont(p));
                total += c.measure(p.text);
                c.setFont(of);
            }
            int x = (w - total) / 2;
            for (const Piece& p : pieces) {
                if (p.text.empty()) continue;
                const Font of = c.setFont(pieceFont(p));
                const int tw = c.measure(p.text);
                const Rect r{x, y, x + tw, y + scale(22)};
                c.text(r, p.text, p.link >= 0 ? kAccent : kDim, draw::kMiddle);
                c.setFont(of);
                if (p.link >= 0) links.push_back({r, urls[size_t(p.link)]});
                x += tw;
            }
            y += scale(22);
        };
        y += scale(12);
        FontDesc heading;  // the UI font, semibold
        heading.weight = 600;
        centered(L"Thanks", c.font(heading), kText, scale(26));
        centered(L"Camelot Systems, for the original StarDraft (1998), which started the StarCraft modding scene.", smallText, kDim, scale(22));
        centered(L"StarDraft 2 aims to be its spiritual successor.", smallText, kDim, scale(22));
        y += scale(10);  // (each thanks is a paragraph; a person's name links to their GitHub profile)
        linkLine(L"Justin F, for IsomTerrain, the isometric terrain",
                 {{L"Justin F", L"https://github.com/TheNitesWhoSay"}, {L"IsomTerrain", L"https://github.com/TheNitesWhoSay/IsomTerrain"}});
        centered(L"IsomTerrain: Copyright (c) 2023 Justin F (MIT License)", smallText, kDim, scale(22));
        y += scale(10);
        linkLine(L"Armoha, for euddraft and eudplib, which power the EUD maps",
                 {{L"Armoha", L"https://github.com/armoha"}, {L"euddraft", L"https://github.com/armoha/euddraft"},
                  {L"eudplib", L"https://github.com/armoha/eudplib"}});
        y += scale(10);
        linkLine(L"Buizz, for EUD Editor 3 and its EUD logic",
                 {{L"Buizz", L"https://github.com/Buizz"}, {L"EUD Editor 3", L"https://github.com/Buizz/EUD-Editor-3"}});
        // Not a Blizzard product, and it needs the game's files.
        y += scale(10);
        centered(L"StarDraft 2 is a fan-made tool. Blizzard Entertainment did not make, approve or endorse it.", smallText, kDim, scale(22));
        centered(L"StarCraft is a trademark of Blizzard Entertainment.", smallText, kDim, scale(22));
        centered(L"It needs an installation of StarCraft to work.", smallText, kDim, scale(22));
        y += scale(14);

        // Then the repository and the Discord server.
        const size_t first = links.size();
        links.push_back({{}, L"https://github.com/" + std::wstring(kIssueRepo)});
        links.push_back({{}, kDiscordInvite});
        const wchar_t* labels[2] = {L"GitHub: github.com/stardraft-editor/stardraft", L"Join the Discord server"};
        for (size_t k = 0; k < 2; ++k) {
            const size_t i = first + k;
            FontDesc ud;
            ud.underline = hot == int(i);
            const Font of = c.setFont(c.font(ud));
            const int tw = c.measure(labels[k]);
            const Rect r{(w - tw) / 2, y, (w + tw) / 2, y + scale(24)};
            c.text(r, labels[k], kAccent, draw::kCenter | draw::kMiddle);
            c.setFont(of);
            links[i].rc = r;
            y += scale(26);
        }

        // Close.
        const int bw = scale(96), bh = scale(stardraft::kit::kButtonH), pad = scale(stardraft::kit::kDialogPad);
        closeRc = {(w - bw) / 2, h - bh - pad, (w + bw) / 2, h - pad};
        const Font of = c.setFont(smallText);
        kitpaint::drawButtonFace(c, closeRc, L"Close", kitpaint::Tone::Primary, hot == -2 ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
        c.setFont(of);
    }

    int hitAt(draw::Point p) const {
        for (size_t i = 0; i < links.size(); ++i)
            if (links[i].rc.contains(p)) return int(i);
        return closeRc.contains(p) ? -2 : -1;
    }
    void setHot(int h) {
        if (h == hot) return;
        hot = h;
        invalidate();
    }
    // Closes the window the view is in.
    void close() {
        if (host()) host()->closeWindow(*this);
        if (closed) closed();
    }

    bool mouse(const draw::MouseEvent& e) override {
        switch (e.kind) {
        case draw::MouseKind::Move: setHot(hitAt(e.pt)); return true;
        case draw::MouseKind::Leave: setHot(-1); return true;
        case draw::MouseKind::Up: {
            if (e.button != 0) return false;
            const int h = hitAt(e.pt);
            if (h >= 0) shellOpenUrl(links[size_t(h)].url);
            else if (h == -2) close();
            return true;
        }
        default: return false;
        }
    }
    bool key(const draw::KeyEvent& e) override {
        if (!e.down || (e.mods & draw::kAlt)) return false;
        if (e.key != draw::Key::Escape && e.key != draw::Key::Enter) return false;
        close();
        return true;
    }
    draw::Cursor cursor(draw::Point) override { return hot != -1 ? draw::Cursor::Hand : draw::Cursor::Arrow; }
};
