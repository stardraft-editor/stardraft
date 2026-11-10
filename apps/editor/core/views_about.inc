// Help > About StarDraft (portable, core.inc): the app icon large, the version, the thanks (with links to IsomTerrain
// and the EUD tools), the GitHub repository and the Discord server. The content is a view (AboutView) in a Dialog
// window that a host opens with Host::openWindow (one window; it does not stop the program). The Windows host is
// about.inc; the SDL program opens it from sdl_menus.inc.
//
// The links open in the browser (shellOpenUrl); the Close button and Esc or Enter close the window.
//
// A hidden extra: a click on the logo turns Craft mode on or off (core/fun_mode.inc: edits on the map make sounds,
// shards and pops). The logo squashes and springs back, bits fly out of it with a sound, and "Craft mode on" (or off)
// shows under it for 2 seconds.

// The program's Discord server and GitHub repository (crash.inc's kRepo is the same repository).
inline const wchar_t* const kDiscordInvite = L"https://discord.gg/JBMCwSRJBm";

// The kind of the program's build, after the build time: a debug build is much slower (its numbers are not the
// release's).
inline std::wstring buildKind() {
#ifdef NDEBUG
    return tr(L"release build");
#else
    return tr(L"debug build");
#endif
}

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
    draw::Rect logoRc;  // from the last paint

    // The logo's click (Craft mode): when it started (animNowMs, 0: none), the bits that fly, the text and until when.
    struct Bit {
        float x, y, vx, vy, size;
        COLORREF color;
    };
    uint64_t popStart = 0, toggled = 0, textUntil = 0;
    std::vector<Bit> bits;
    uint64_t lastTick = 0;
    std::wstring craftText;
    static constexpr uint64_t kPopMs = 450, kTextMs = 2000;

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
        logoRc = {(w - px) / 2, y, (w - px) / 2 + px, y + px};
        {  // (after a click: squashed, then it springs up and settles, about its foot)
            const uint64_t now = animNowMs();
            float sx = 1, sy = 1;
            if (popStart && now - popStart < kPopMs) {
                const float t = float(now - popStart), a = 0.3f * std::exp(-t / 110.0f) * std::cos(t * 6.2831853f / 240.0f);
                sx = 1 + a * 0.6f, sy = 1 - a;
            }
            const int dw = int(float(px) * sx), dh = int(float(px) * sy);
            c.resourceIcon({(w - dw) / 2, y + px - dh, (w + dw) / 2, y + px}, 1, std::max(dw, dh));
            for (const Bit& b : bits) {
                const int s = std::max(1, int(b.size));
                c.fill({int(b.x) - s, int(b.y) - s, int(b.x) + s, int(b.y) + s}, b.color);
            }
            if (!craftText.empty() && now < textUntil) {
                FontDesc d;
                d.weight = 600;
                const Font of = c.setFont(c.font(d));
                c.text({0, y + px - scale(4), w, y + px + scale(16)}, tr(craftText), kAccent, draw::kCenter | draw::kMiddle);
                c.setFont(of);
            }
        }
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
        centered(L"v" + shellVersion() + L"  (" + (shellBuildStamp().empty() ? L"" : shellBuildStamp() + L", ") + buildKind() + L")", smallText, kDim, scale(22));
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

    // A click on the logo: the pop, the bits and a sound now; Craft mode changes when the pop is at its lowest.
    void logoClick() {
        const uint64_t now = animNowMs();
        popStart = lastTick = now, toggled = 0;
        std::mt19937 rng{uint32_t(now)};
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        const COLORREF colors[] = {RGB(120, 230, 170), RGB(60, 170, 120), RGB(200, 255, 225), RGB(30, 110, 80)};  // (the logo's greens)
        bits.clear();
        const float cx = float(logoRc.left + logoRc.right) / 2, cy = float(logoRc.top + logoRc.bottom) / 2, r = float(logoRc.width()) * 0.4f;
        for (int i = 0; i < 48; ++i) {
            const float a = 6.2831853f * u(rng), d = r * (0.3f + 0.7f * u(rng)), speed = float(scale(160)) * (0.5f + u(rng));
            bits.push_back({cx + std::cos(a) * d, cy + std::sin(a) * d, std::cos(a) * speed, std::sin(a) * speed - float(scale(120)),
                            float(scale(2)) * (0.6f + u(rng)), colors[i % 4]});
        }
        fun::play(fun::Kind::Thunk, 1, 1, 0);
        tick();
    }
    void tick() {
        const uint64_t now = animNowMs();
        const float dt = std::min(0.05f, float(now - lastTick) / 1000);
        lastTick = now;
        for (Bit& b : bits) b.vy += float(scale(700)) * dt, b.x += b.vx * dt, b.y += b.vy * dt, b.size *= 1 - 1.6f * dt;
        std::erase_if(bits, [&](const Bit& b) { return b.size < 0.4f || b.y > float(height()); });
        if (popStart && !toggled && now - popStart >= 120) {  // (the mode changes as the logo is down: it says so)
            toggled = now;
            command(ID_OPT_FUNMODE);
            craftText = shellState().funMode ? L"Craft mode on" : L"Craft mode off";
            textUntil = now + kTextMs;
        }
        invalidate();
        if (host() && (!bits.empty() || now - popStart < kPopMs || now < textUntil)) host()->timer(16, [this] { tick(); });
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
            else if (logoRc.contains(e.pt)) logoClick();
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
