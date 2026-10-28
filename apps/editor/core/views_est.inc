// The establishing shot of a Cosmonarchy map (portable, core.inc; Map properties' last part, only for a Cosmonarchy map:
// core/cmbw.inc modTab): the pages with a picture and text that the mod shows before the map starts, when the map holds
// rez\est.txt (stardraft::cmbw::EstablishingShot). A page list, a preview of the page, and its picture (with its size
// against the game's 640 x 480), text, place and display time; the shot's fade time. Each change goes into the map at
// once (one undo step).

class EstShotView final : public draw::View {
public:
    // The game's screen: a picture fills it (the mod's example, its screenshot: the 1.16.1 game at 640 x 480).
    static constexpr int kShotW = 640, kShotH = 480;

    EstShotView() {
        add_.setText(tr(L"Add an establishing shot"));
        add_.setTip(tr(L"Pages with a picture and text that Cosmonarchy shows before the map starts"));
        add_.onClick = [this] { create(); };
        picture_.setText(tr(L"Choose picture\x2026"));
        picture_.setTip(tr(L"640 x 480 pixels, the game's screen. A PNG, JPEG or BMP is cut to 4:3 and made an 8-bit PCX with its own 256 colors; "
                           L"an 8-bit PCX goes in as it is."));
        picture_.onClick = [this] { choosePicture(); };
        text_.setText(tr(L"Edit text\x2026"));
        text_.setTip(tr(L"The page's lines, as the game shows them (plain text: no color codes)"));
        text_.onClick = [this] { editText(); };
        display_.setTip(tr(L"How long the game shows the page"));
        display_.onClick = [this] { editDisplay(); };
        fade_.setTip(tr(L"How long each page fades in and out"));
        fade_.onClick = [this] { editFade(); };
        addPage_.setText(tr(L"Add page"));
        addPage_.setTip(tr(L"A new page after this one, with the same picture"));
        addPage_.onClick = [this] { addPage(); };
        deletePage_.setText(tr(L"Delete page"));
        deletePage_.setTone(stardraft::kit::Tone::Danger);
        deletePage_.onClick = [this] { deletePage(); };
        remove_.setText(tr(L"Remove shot"));
        remove_.setTone(stardraft::kit::Tone::Danger);
        remove_.setTip(tr(L"Take the establishing shot out of the map (its pictures too)"));
        remove_.onClick = [this] { removeShot(); };
        std::vector<stardraft::kit::DropdownItem> places;
        for (const auto& [tag, name] : stardraft::cmbw::establishingShotPlaces()) places.push_back({tr(widen(name).c_str())});
        place_.setItems(places);
        place_.onChange = [this](int i) { setPlace(i); };
        page_.onChange = [this](int i) { page = i, invalidate(); };
        for (draw::View* v : std::initializer_list<draw::View*>{&add_, &picture_, &text_, &display_, &fade_, &addPage_, &deletePage_, &remove_, &place_, &page_})
            add(*v, {0, 0, 0, 0});
    }
    int preferredWidth() const { return scale(860); }
    int preferredHeight() const { return scale(540); }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        const int pad = scale(stardraft::kit::kDialogPad), rowH = scale(stardraft::kit::kRowH), bh = scale(stardraft::kit::kButtonH);
        c.text({pad, pad, width() - pad, pad + rowH}, tr(L"Establishing shot"), kText, draw::kMiddle);
        c.fill({pad, pad + rowH - 1, width() - pad, pad + rowH}, kBorder);
        const int noteY = pad + rowH + scale(6);
        c.text({pad, noteY, width() - pad, noteY + rowH},
               tr(L"Pages that Cosmonarchy shows before the map starts: each a 640 x 480 picture with text (rez\\est.txt in the map)."), kDim,
               draw::kMiddle | draw::kEllipsis);
        const int top = noteY + rowH + scale(10);
        const Editor* ed = appState().ed.get();
        const ArchiveFile* script = ed ? ed->archiveFile("rez\\est.txt") : nullptr;
        const bool has = script != nullptr;
        const auto shot = has ? stardraft::cmbw::parseEstablishingShot(std::string(script->data.begin(), script->data.end()))
                              : stardraft::cmbw::EstablishingShot{};
        const int pages = int(shot.pages.size());
        page = std::clamp(page, 0, std::max(0, pages - 1));
        const stardraft::cmbw::EstablishingShot::Page* pg = pages ? &shot.pages[size_t(page)] : nullptr;
        // (no shot yet: the same parts, all off but Add)
        add_.setVisible(!has), remove_.setVisible(has);
        picture_.setEnabled(pg), text_.setEnabled(pg), display_.setEnabled(pg), place_.setEnabled(pg), page_.setEnabled(pages > 1);
        fade_.setEnabled(has), addPage_.setEnabled(has), deletePage_.setEnabled(pages > 1);

        // The page list over the preview.
        const int pw = scale(480), ph = scale(360);
        std::vector<stardraft::kit::DropdownItem> names;
        for (int i = 0; i < pages; ++i) {
            std::wstring first = shot.pages[size_t(i)].lines.empty() ? L"" : gameTextPlain(shot.pages[size_t(i)].lines.front());
            names.push_back({tr(L"Page ") + std::to_wstring(i + 1) + (first.empty() ? L"" : L": " + first)});
        }
        if (names.empty()) names.push_back({tr(L"No pages")});
        page_.setItems(names);
        if (page_.value() != page) page_.setValue(pages ? page : 0);
        // (no shot: Add where the page list is; the page's parts below it, off)
        page_.setVisible(has);
        place(page_, {pad, top, pad + pw - scale(2 * 100 + 16), top + bh});
        place(add_, {pad, top, pad + pw - scale(2 * 100 + 16), top + bh});
        place(addPage_, {pad + pw - scale(200 + 8), top, pad + pw - scale(100 + 8), top + bh});
        place(deletePage_, {pad + pw - scale(100), top, pad + pw, top + bh});

        // The preview: the page's picture (the page before's when it has none of its own), its text at its place.
        const int py = top + bh + scale(8);
        const draw::Rect pv{pad, py, pad + pw, py + ph};
        c.fill(pv, RGB(0, 0, 0));
        std::string bg;
        for (int i = page; i >= 0 && pages; --i)
            if (!shot.pages[size_t(i)].background.empty()) {
                bg = shot.pages[size_t(i)].background;
                break;
            }
        int picW = 0, picH = 0;
        draw::Bitmap* b = has && !bg.empty() ? pictureOf(*ed, bg, picW, picH) : nullptr;
        if (b) c.bitmap(*b, {0, 0, b->width(), b->height()}, pv, true);
        else c.text(pv, !has ? tr(L"The map has no establishing shot") : bg.empty() ? tr(L"No picture") : tr(L"The picture is not in the map"), kDim,
                    draw::kMiddle | draw::kCenter);
        c.roundBox(pv, draw::kNone, kBorder, 0.0f, 1);
        if (pg) {
            std::wstring t;
            for (const std::string& l : pg->lines) t += (t.empty() ? L"" : L"\n") + gameTextPlain(l);
            const int m = pw * 10 / kShotW;  // (the game's 10 pixels from the screen's edge, in the preview's scale)
            const draw::Rect inner{pv.left + m, pv.top + m, pv.right - m, pv.bottom - m};
            const std::string& p = pg->place;
            const unsigned h = p.find("LEFT") != std::string::npos ? draw::kLeft : p.find("RIGHT") != std::string::npos ? draw::kRight : draw::kCenter;
            const int th = c.measureBox(t, inner.width(), draw::kWrap).y;
            const int ty = p.find("UPPER") != std::string::npos || p == "SCREENTOP" ? inner.top
                           : p.find("LOWER") != std::string::npos || p == "SCREENBOTTOM" ? inner.bottom - th
                                                                                          : inner.top + (inner.height() - th) / 2;
            c.text({inner.left + 1, ty + 1, inner.right + 1, ty + 1 + th}, t, RGB(0, 0, 0), draw::kWrap | h);  // (a shadow)
            c.text({inner.left, ty, inner.right, ty + th}, t, RGB(230, 230, 230), draw::kWrap | h);
        }
        // Under the preview: the picture's file and size against the game's screen.
        const int iy = pv.bottom + scale(6);
        std::wstring info;
        draw::Color infoColor = kDim;
        if (has && !bg.empty()) {
            info = widen(bg);
            if (pg && pg->background.empty()) info += tr(L" (the page before's)");
            if (b) {
                info += L"   " + std::to_wstring(picW) + L" x " + std::to_wstring(picH);
                if (picW == kShotW && picH == kShotH) info += tr(L": the game's screen");
                else info += tr(L": not 640 x 480 (choose the picture again to make it so)"), infoColor = kWarn;
            }
        } else if (has) {
            info = tr(L"Choose a picture of 640 x 480 pixels, the game's screen"), infoColor = kWarn;
        }
        c.text({pad, iy, pad + pw, iy + rowH}, info, infoColor, draw::kMiddle | draw::kEllipsis);

        // The page's fields, right of the preview; then the shot's.
        const int x = pv.right + pad, w = std::max(scale(160), width() - pad - x);
        int y = py;
        c.text({x, top, x + w, top + bh}, pg ? tr(L"Page ") + std::to_wstring(page + 1) + tr(L" of ") + std::to_wstring(pages) : std::wstring(), kText,
               draw::kMiddle);
        place(picture_, {x, y, x + w, y + bh}), y += bh + scale(4);
        c.text({x, y, x + w, y + rowH}, tr(L"640 x 480, 8-bit PCX"), kDim, draw::kMiddle), y += rowH + scale(6);
        place(text_, {x, y, x + w, y + bh}), y += bh + scale(8);
        c.text({x, y, x + w, y + rowH}, tr(L"Place of the text"), kDim, draw::kMiddle), y += rowH;
        int placeIndex = 0;
        const auto& places = stardraft::cmbw::establishingShotPlaces();
        for (size_t i = 0; i < places.size(); ++i)
            if (pg && places[i].first == pg->place) placeIndex = int(i);
        if (place_.value() != placeIndex) place_.setValue(placeIndex);
        place(place_, {x, y, x + w, y + bh}), y += bh + scale(8);
        display_.setText(tr(L"Shown for: ") + seconds(pg ? pg->displayMs : -1));
        place(display_, {x, y, x + w, y + bh}), y += bh + scale(20);
        c.text({x, y, x + w, y + rowH}, tr(L"All pages"), kDim, draw::kMiddle), y += rowH;
        c.fill({x, y - 1, x + w, y}, kBorder), y += scale(6);
        fade_.setText(tr(L"Fade: ") + seconds(shot.fadeMs));
        place(fade_, {x, y, x + w, y + bh}), y += bh + scale(8);
        place(remove_, {x, y, x + w, y + bh});
    }

private:
    stardraft::kit::Button add_, picture_, text_, display_, fade_, addPage_, deletePage_, remove_;
    stardraft::kit::Dropdown place_{{}}, page_{{}};
    int page = 0;  // the page shown
    draw::Bitmap picture_bitmap_;
    const Editor* pictureEd_ = nullptr;
    uint64_t pictureKey_ = 0;
    int pictureW_ = 0, pictureH_ = 0;

    static std::wstring seconds(int ms) {
        if (ms < 0) return tr(L"the game's time");
        wchar_t s[32];
        std::swprintf(s, 32, L"%g s", ms / 1000.0);
        return s;
    }
    static void place(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
    }
    // The picture of the map's file `name` (decoded again when the file changes), and its size.
    draw::Bitmap* pictureOf(const Editor& ed, const std::string& name, int& w, int& h) {
        const ArchiveFile* f = ed.archiveFile(name);
        if (!f) return nullptr;
        // (the size, the name and the file's first and last 4 KB: the header and the palette at the end)
        const size_t n = f->data.size(), part = std::min<size_t>(n, 4096);
        const std::string_view head(reinterpret_cast<const char*>(f->data.data()), part), tail(reinterpret_cast<const char*>(f->data.data()) + n - part, part);
        const uint64_t key = uint64_t(n) * 1000003u ^ std::hash<std::string_view>{}(head) * 31u ^ std::hash<std::string_view>{}(tail) ^ std::hash<std::string>{}(name);
        if (pictureEd_ != &ed || pictureKey_ != key) {
            pictureEd_ = &ed, pictureKey_ = key;
            std::vector<uint8_t> rgba;
            if (!stardraft::cmbw::decodePcxRgba(f->data, pictureW_, pictureH_, rgba)) {
                picture_bitmap_.resize(0, 0), pictureW_ = pictureH_ = 0;
                return nullptr;
            }
            picture_bitmap_.resize(pictureW_, pictureH_);
            std::memcpy(picture_bitmap_.pixels(), rgba.data(), rgba.size());
            picture_bitmap_.opaque = true;
            picture_bitmap_.changed();
        }
        w = pictureW_, h = pictureH_;
        return picture_bitmap_.width() ? &picture_bitmap_ : nullptr;
    }
    // The name of the map's file `name` as the map has it (its case), else `name`.
    static std::string mapName(const Editor& ed, const std::string& name) {
        const ArchiveFile* f = ed.archiveFile(name);
        return f ? f->name : name;
    }
    static stardraft::cmbw::EstablishingShot shotOf(const Editor& ed) {
        const ArchiveFile* f = ed.archiveFile("rez\\est.txt");
        return stardraft::cmbw::parseEstablishingShot(f ? std::string(f->data.begin(), f->data.end()) : std::string());
    }
    // The map's shot, changed by `edit`, back into the map with the files `more` (a picture), all in one undo step.
    void change(const std::function<void(stardraft::cmbw::EstablishingShot&)>& edit, const std::wstring& message, std::vector<ArchiveFile> more = {}) {
        Editor* ed = appState().ed.get();
        if (!ed) return;
        auto shot = shotOf(*ed);
        if (shot.pages.empty()) shot.pages.push_back({});
        edit(shot);
        const std::string text = stardraft::cmbw::establishingShotText(shot);
        more.push_back({mapName(*ed, "rez\\est.txt"), Bytes(text.begin(), text.end())});
        ed->setArchiveFiles(std::move(more));
        setMessage(message);
        invalidate();
    }
    stardraft::cmbw::EstablishingShot::Page& cur(stardraft::cmbw::EstablishingShot& s) {
        page = std::clamp(page, 0, int(s.pages.size()) - 1);
        return s.pages[size_t(page)];
    }
    void create() {
        page = 0;
        change(
            [](stardraft::cmbw::EstablishingShot& s) {
                s.fadeMs = 1000;
                auto& p = s.pages.front();
                p.background = "est\\est.pcx", p.displayMs = 6500, p.place = "SCREENLOWERLEFT";
                if (p.lines.empty()) p.lines = {"MAP TITLE", "", "A place, a time"};
            },
            tr(L"Added an establishing shot: choose its picture (640 x 480) and edit its text"));
    }
    void addPage() {
        Editor* ed = appState().ed.get();
        if (!ed) return;
        const int at = page + 1;
        change(
            [&](stardraft::cmbw::EstablishingShot& s) {
                stardraft::cmbw::EstablishingShot::Page p;
                const auto& was = cur(s);
                p.background = was.background, p.font = was.font, p.displayMs = was.displayMs, p.place = was.place, p.lines = {"NEW PAGE"};
                s.pages.insert(s.pages.begin() + std::min<int>(at, int(s.pages.size())), p);
            },
            tr(L"Added page ") + std::to_wstring(at + 1) + tr(L" to the establishing shot"));
        page = at;
        invalidate();
    }
    void deletePage() {
        Editor* ed = appState().ed.get();
        if (!ed) return;
        auto shot = shotOf(*ed);
        if (shot.pages.size() < 2) return;
        const int at = std::clamp(page, 0, int(shot.pages.size()) - 1);
        const std::string pic = shot.pages[size_t(at)].background;
        change([&](stardraft::cmbw::EstablishingShot& s) { s.pages.erase(s.pages.begin() + at); },
               tr(L"Deleted page ") + std::to_wstring(at + 1) + tr(L" of the establishing shot"));
        // (its picture, when no other page shows it and the map holds it)
        const auto after = shotOf(*ed);
        if (!pic.empty() && ed->archiveFile(pic) &&
            std::none_of(after.pages.begin(), after.pages.end(), [&](const auto& p) { return p.background == pic; }))
            ed->removeArchiveFiles({mapName(*ed, pic)});
        page = std::max(0, at - 1);
        invalidate();
    }
    // A file name in the map for page i's picture: est\est.pcx for the first, est\est<n>.pcx for the others.
    static std::string pictureName(const stardraft::cmbw::EstablishingShot& s, int i) {
        const std::string own = s.pages[size_t(i)].background;
        // (its own picture of the map, when no other page shows it: replaced in place)
        int users = 0;
        for (const auto& p : s.pages) users += p.background == own;
        if (!own.empty() && users == 1 && own.rfind("est\\", 0) == 0) return own;
        for (int n = i + 1;; ++n) {
            const std::string name = n == 1 ? "est\\est.pcx" : "est\\est" + std::to_string(n) + ".pcx";
            if (std::none_of(s.pages.begin(), s.pages.end(), [&](const auto& p) { return p.background == name; })) return name;
        }
    }
    void choosePicture() {
        Editor* ed = appState().ed.get();
        if (!ed) return;
        ShellFilePick pick;
        pick.title = tr(L"Picture of the page (640 x 480)");
        pick.filters = {{L"Picture (*.png; *.jpg; *.jpeg; *.bmp; *.pcx)", L"*.png;*.jpg;*.jpeg;*.bmp;*.pcx"}};
        const fs::path file = shellPickFile(pick);
        if (file.empty()) return;
        std::ifstream in(file, std::ios::binary);
        Bytes data((std::istreambuf_iterator<char>(in)), {});
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
        std::wstring how;
        // (an 8-bit PCX as it is; another picture: its middle at 4:3, made an 8-bit PCX of the game's 640 x 480, its 256
        // colors picked)
        if (!stardraft::cmbw::decodePcxRgba(data, w, h, rgba)) {
            if (!shellDecodeImage(data, w, h, rgba))
                return shellErrorBox(tr(L"The file is not a PNG, JPEG, BMP or 8-bit PCX picture."), tr(L"Picture of the establishing shot"));
            int cw = w, ch = h;  // (the largest 4:3 part, in the middle)
            if (int64_t(w) * kShotH > int64_t(h) * kShotW) cw = int(int64_t(h) * kShotW / kShotH);
            else ch = int(int64_t(w) * kShotH / kShotW);
            std::vector<uint8_t> crop(size_t(cw) * size_t(ch) * 4);
            const int ox = (w - cw) / 2, oy = (h - ch) / 2;
            for (int y = 0; y < ch; ++y)
                std::memcpy(&crop[size_t(y) * size_t(cw) * 4], &rgba[(size_t(y + oy) * size_t(w) + size_t(ox)) * 4], size_t(cw) * 4);
            data = stardraft::cmbw::encodePcx8(crop, cw, ch, kShotW, kShotH);
            if (cw != w || ch != h) how = tr(L" (its middle, cut to 4:3)");
            if (cw != kShotW || ch != kShotH) how += tr(L", made 640 x 480");
        } else if (w != kShotW || h != kShotH) {
            how = L" (" + std::to_wstring(w) + L" x " + std::to_wstring(h) + tr(L": the game's screen is 640 x 480)");
        }
        const auto shot = shotOf(*ed);
        if (shot.pages.empty()) return;
        const int at = std::clamp(page, 0, int(shot.pages.size()) - 1);
        const std::string name = pictureName(shot, at);
        std::vector<ArchiveFile> picture;
        picture.push_back({mapName(*ed, name), std::move(data)});
        change([&](stardraft::cmbw::EstablishingShot& s) { cur(s).background = name; },
               tr(L"Page ") + std::to_wstring(at + 1) + tr(L" has the picture ") + file.filename().wstring() + how, std::move(picture));
    }
    void editText() {
        const Editor* ed = appState().ed.get();
        if (!ed) return;
        auto shot = shotOf(*ed);
        if (shot.pages.empty()) return;
        std::string text;
        for (const std::string& l : cur(shot).lines) text += (text.empty() ? "" : "\r\n") + l;
        // (plain text, several lines: the mod reads its lines as they are, no color codes)
        if (!shellEditPlainLines(*this, text_.frame(), tr(L"Text of page ") + std::to_wstring(page + 1), text)) return;
        change(
            [&](stardraft::cmbw::EstablishingShot& s) {
                std::vector<std::string> lines;
                std::string line;
                for (char ch : text)
                    if (ch == '\n') lines.push_back(line), line.clear();
                    else if (ch != '\r') line += ch;
                lines.push_back(line);
                cur(s).lines = lines;
            },
            tr(L"Changed the text of page ") + std::to_wstring(page + 1));
    }
    void setPlace(int i) {
        const auto& places = stardraft::cmbw::establishingShotPlaces();
        if (i < 0 || i >= int(places.size())) return;
        change([&](stardraft::cmbw::EstablishingShot& s) { cur(s).place = places[size_t(i)].first; },
               tr(L"The text of page ") + std::to_wstring(page + 1) + tr(L" is at ") + tr(widen(places[size_t(i)].second).c_str()));
    }
    // A time in seconds (0 to 600) typed in a prompt; -1 when nothing (or nothing valid) was typed.
    static int promptSeconds(const std::wstring& title, int ms) {
        wchar_t now[32];
        std::swprintf(now, 32, L"%g", (ms < 0 ? 0 : ms) / 1000.0);
        std::wstring typed = now;
        if (!shellPromptText(title, typed)) return -1;
        const double v = std::wcstod(typed.c_str(), nullptr);
        if (!(v >= 0) || v > 600) return shellErrorBox(tr(L"Type a number of seconds from 0 to 600, for example 5"), title), -1;
        return int(std::lround(v * 1000));
    }
    void editDisplay() {
        const Editor* ed = appState().ed.get();
        if (!ed) return;
        auto shot = shotOf(*ed);
        if (shot.pages.empty()) return;
        const int ms = promptSeconds(tr(L"How long page ") + std::to_wstring(page + 1) + tr(L" shows (seconds)"), cur(shot).displayMs);
        if (ms < 0) return;
        change([&](stardraft::cmbw::EstablishingShot& s) { cur(s).displayMs = ms; }, tr(L"Changed how long page ") + std::to_wstring(page + 1) + tr(L" shows"));
    }
    void editFade() {
        const Editor* ed = appState().ed.get();
        if (!ed || !ed->archiveFile("rez\\est.txt")) return;
        const int ms = promptSeconds(tr(L"Fade time of the establishing shot's pages (seconds)"), shotOf(*ed).fadeMs);
        if (ms < 0) return;
        change([&](stardraft::cmbw::EstablishingShot& s) { s.fadeMs = ms; }, tr(L"Changed the fade time of the establishing shot"));
    }
    void removeShot() {
        Editor* ed = appState().ed.get();
        if (!ed) return;
        // (the script, and the pictures and fonts of its pages that the map holds)
        std::vector<std::string> names = {mapName(*ed, "rez\\est.txt")};
        for (const auto& p : shotOf(*ed).pages)
            for (const std::string& f : {p.background, p.font})
                if (!f.empty() && ed->archiveFile(f) && std::find(names.begin(), names.end(), mapName(*ed, f)) == names.end()) names.push_back(mapName(*ed, f));
        ed->removeArchiveFiles(names);
        page = 0;
        setMessage(tr(L"Removed the establishing shot"));
        invalidate();
    }
};
