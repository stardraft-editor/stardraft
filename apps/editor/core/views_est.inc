// The establishing shot of a Cosmonarchy map (portable, core.inc; Map properties' last part, only for a Cosmonarchy map:
// core/cmbw.inc modTab): the picture with text that the mod shows before the map starts, when the map holds rez\est.txt
// (stardraft::cmbw::EstablishingShot). A preview of the first page, and its picture, text, place and fade time; each
// change goes into the map at once (one undo step).

class EstShotView final : public draw::View {
public:
    EstShotView() {
        add_.setText(tr(L"Add an establishing shot"));
        add_.setTip(tr(L"A picture with text that Cosmonarchy shows before the map starts"));
        add_.onClick = [this] { create(); };
        picture_.setText(tr(L"Choose picture\x2026"));
        picture_.setTip(tr(L"A PNG, JPEG or BMP picture (made an 8-bit PCX of 640 x 480), or an 8-bit PCX picture"));
        picture_.onClick = [this] { choosePicture(); };
        text_.setText(tr(L"Edit text\x2026"));
        text_.onClick = [this] { editText(); };
        fade_.onClick = [this] { editFade(); };
        remove_.setText(tr(L"Remove"));
        remove_.setTone(stardraft::kit::Tone::Danger);
        remove_.setTip(tr(L"Take the establishing shot out of the map (its picture too)"));
        remove_.onClick = [this] { removeShot(); };
        std::vector<stardraft::kit::DropdownItem> places;
        for (const auto& [tag, name] : stardraft::cmbw::establishingShotPlaces()) places.push_back({tr(widen(name).c_str())});
        place_.setItems(places);
        place_.onChange = [this](int i) { setPlace(i); };
        for (draw::View* v : std::initializer_list<draw::View*>{&add_, &picture_, &text_, &fade_, &remove_, &place_}) add(*v, {0, 0, 0, 0});
    }
    int preferredWidth() const { return scale(860); }
    int preferredHeight() const { return scale(490); }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        const int pad = scale(stardraft::kit::kDialogPad), rowH = scale(stardraft::kit::kRowH), bh = scale(stardraft::kit::kButtonH);
        c.text({pad, pad, width() - pad, pad + rowH}, tr(L"Establishing shot"), kText, draw::kMiddle);
        c.fill({pad, pad + rowH - 1, width() - pad, pad + rowH}, kBorder);
        const int noteY = pad + rowH + scale(6);
        c.text({pad, noteY, width() - pad, noteY + rowH}, tr(L"Cosmonarchy shows it before the map starts: a picture with text (rez\\est.txt in the map)."), kDim,
               draw::kMiddle | draw::kEllipsis);
        const int top = noteY + rowH + scale(10);
        const Editor* ed = appState().ed.get();
        const ArchiveFile* script = ed ? ed->archiveFile("rez\\est.txt") : nullptr;
        const bool has = script != nullptr;
        // (no shot yet: the same parts, all off but Add)
        add_.setVisible(!has), remove_.setVisible(has);
        picture_.setEnabled(has), text_.setEnabled(has), fade_.setEnabled(has), place_.setEnabled(has);
        const auto shot = has ? stardraft::cmbw::parseEstablishingShot(std::string(script->data.begin(), script->data.end()))
                              : stardraft::cmbw::EstablishingShot{};
        // The preview: the picture (4:3), the first page's text at its place.
        const draw::Rect pv{pad, top, pad + scale(480), top + scale(360)};
        c.fill(pv, RGB(0, 0, 0));
        if (draw::Bitmap* b = has ? pictureOf(*ed, shot.background) : nullptr) c.bitmap(*b, {0, 0, b->width(), b->height()}, pv, true);
        else c.text(pv, has ? tr(L"No picture") : tr(L"The map has no establishing shot"), kDim, draw::kMiddle | draw::kCenter);
        c.roundBox(pv, draw::kNone, kBorder, 0.0f, 1);
        if (!shot.pages.empty()) {
            const auto& page = shot.pages.front();
            std::wstring t;
            for (const std::string& l : page.lines) t += (t.empty() ? L"" : L"\n") + gameTextPlain(l);
            const int m = scale(16);
            const draw::Rect inner{pv.left + m, pv.top + m, pv.right - m, pv.bottom - m};
            const std::string& p = page.place;
            const unsigned h = p.find("LEFT") != std::string::npos ? draw::kLeft : p.find("RIGHT") != std::string::npos ? draw::kRight : draw::kCenter;
            const int th = c.measureBox(t, inner.width(), draw::kWrap).y;
            const int ty = p.find("UPPER") != std::string::npos || p == "SCREENTOP" ? inner.top
                           : p.find("LOWER") != std::string::npos || p == "SCREENBOTTOM" ? inner.bottom - th
                                                                                          : inner.top + (inner.height() - th) / 2;
            c.text({inner.left + 1, ty + 1, inner.right + 1, ty + 1 + th}, t, RGB(0, 0, 0), draw::kWrap | h);  // (a shadow)
            c.text({inner.left, ty, inner.right, ty + th}, t, RGB(230, 230, 230), draw::kWrap | h);
        }
        // The fields, right of the preview.
        const int x = pv.right + pad, w = scale(220);
        int y = top;
        place(picture_, {x, y, x + w, y + bh}), y += bh + scale(8);
        place(text_, {x, y, x + w, y + bh}), y += bh + scale(8);
        c.text({x, y, x + w, y + rowH}, tr(L"Place of the text"), kDim, draw::kMiddle), y += rowH;
        int placeIndex = 0;
        const auto& places = stardraft::cmbw::establishingShotPlaces();
        for (size_t i = 0; i < places.size(); ++i)
            if (!shot.pages.empty() && places[i].first == shot.pages.front().place) placeIndex = int(i);
        if (place_.value() != placeIndex) place_.setValue(placeIndex);
        place(place_, {x, y, x + w, y + bh}), y += bh + scale(8);
        wchar_t fade[64];
        std::swprintf(fade, 64, L"%ls %g s", tr(L"Fade:"), (shot.fadeMs < 0 ? 0 : shot.fadeMs) / 1000.0);
        fade_.setText(fade);
        place(fade_, {x, y, x + w, y + bh}), y += bh + scale(24);
        place(remove_, {x, y, x + w, y + bh});
        place(add_, {x, y, x + w, y + bh});
    }

private:
    stardraft::kit::Button add_, picture_, text_, fade_, remove_;
    stardraft::kit::Dropdown place_{{}};
    draw::Bitmap picture_bitmap_;
    const Editor* pictureEd_ = nullptr;
    uint64_t pictureKey_ = 0;

    static void place(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
    }
    // The picture of the map's file `name` (decoded again when the file changes).
    draw::Bitmap* pictureOf(const Editor& ed, const std::string& name) {
        const ArchiveFile* f = ed.archiveFile(name);
        if (!f) return nullptr;
        // (the size, the name and the file's first and last 4 KB: the header and the palette at the end)
        const size_t n = f->data.size(), part = std::min<size_t>(n, 4096);
        const std::string_view head(reinterpret_cast<const char*>(f->data.data()), part), tail(reinterpret_cast<const char*>(f->data.data()) + n - part, part);
        const uint64_t key = uint64_t(n) * 1000003u ^ std::hash<std::string_view>{}(head) * 31u ^ std::hash<std::string_view>{}(tail) ^ std::hash<std::string>{}(name);
        if (pictureEd_ != &ed || pictureKey_ != key) {
            pictureEd_ = &ed, pictureKey_ = key;
            int w = 0, h = 0;
            std::vector<uint8_t> rgba;
            if (!stardraft::cmbw::decodePcxRgba(f->data, w, h, rgba)) {
                picture_bitmap_.resize(0, 0);
                return nullptr;
            }
            picture_bitmap_.resize(w, h);
            std::memcpy(picture_bitmap_.pixels(), rgba.data(), rgba.size());
            picture_bitmap_.opaque = true;
            picture_bitmap_.changed();
        }
        return picture_bitmap_.width() ? &picture_bitmap_ : nullptr;
    }
    // The name of the map's file `name` as the map has it (its case), else `name`.
    static std::string mapName(const Editor& ed, const std::string& name) {
        const ArchiveFile* f = ed.archiveFile(name);
        return f ? f->name : name;
    }
    // The map's shot, changed by `edit`, back into the map with the files `more` (a picture), all in one undo step.
    void change(const std::function<void(stardraft::cmbw::EstablishingShot&)>& edit, const std::wstring& message, std::vector<ArchiveFile> more = {}) {
        Editor* ed = appState().ed.get();
        if (!ed) return;
        const ArchiveFile* f = ed->archiveFile("rez\\est.txt");
        auto shot = stardraft::cmbw::parseEstablishingShot(f ? std::string(f->data.begin(), f->data.end()) : std::string());
        if (shot.pages.empty()) shot.pages.push_back({});
        edit(shot);
        const std::string text = stardraft::cmbw::establishingShotText(shot);
        more.push_back({mapName(*ed, "rez\\est.txt"), Bytes(text.begin(), text.end())});
        ed->setArchiveFiles(std::move(more));
        setMessage(message);
        invalidate();
    }
    void create() {
        change(
            [](stardraft::cmbw::EstablishingShot& s) {
                s.fadeMs = 5000, s.background = "est\\est.pcx";
                if (s.pages.front().lines.empty()) s.pages.front().lines = {"MAP TITLE"};
            },
            tr(L"Added an establishing shot: choose its picture and edit its text"));
    }
    void choosePicture() {
        ShellFilePick pick;
        pick.title = tr(L"Picture of the establishing shot");
        pick.filters = {{L"Picture (*.png; *.jpg; *.jpeg; *.bmp; *.pcx)", L"*.png;*.jpg;*.jpeg;*.bmp;*.pcx"}};
        const fs::path file = shellPickFile(pick);
        if (file.empty()) return;
        std::ifstream in(file, std::ios::binary);
        Bytes data((std::istreambuf_iterator<char>(in)), {});
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
        // (an 8-bit PCX as it is; another picture: made an 8-bit PCX of the game's size, 640 x 480, its 256 colors picked)
        if (!stardraft::cmbw::decodePcxRgba(data, w, h, rgba)) {
            if (!shellDecodeImage(data, w, h, rgba))
                return shellErrorBox(tr(L"The file is not a PNG, JPEG, BMP or 8-bit PCX picture."), tr(L"Picture of the establishing shot"));
            data = stardraft::cmbw::encodePcx8(rgba, w, h);
        }
        Editor* ed = appState().ed.get();
        if (!ed) return;
        std::vector<ArchiveFile> picture;
        picture.push_back({mapName(*ed, "est\\est.pcx"), std::move(data)});
        change([](stardraft::cmbw::EstablishingShot& s) { s.background = "est\\est.pcx"; }, tr(L"The establishing shot has the picture ") + file.filename().wstring(),
               std::move(picture));
    }
    void editText() {
        const Editor* ed = appState().ed.get();
        const ArchiveFile* f = ed ? ed->archiveFile("rez\\est.txt") : nullptr;
        if (!f) return;
        auto shot = stardraft::cmbw::parseEstablishingShot(std::string(f->data.begin(), f->data.end()));
        std::string text;
        if (!shot.pages.empty())
            for (const std::string& l : shot.pages.front().lines) text += (text.empty() ? "" : "\r\n") + l;
        // (plain text, several lines: the mod reads its lines as they are, no color codes)
        if (!shellEditPlainLines(*this, text_.frame(), tr(L"Text of the establishing shot"), text)) return;
        change(
            [&](stardraft::cmbw::EstablishingShot& s) {
                std::vector<std::string> lines;
                std::string line;
                for (char ch : text)
                    if (ch == '\n') lines.push_back(line), line.clear();
                    else if (ch != '\r') line += ch;
                lines.push_back(line);
                s.pages.front().lines = lines;
            },
            tr(L"Changed the text of the establishing shot"));
    }
    void setPlace(int i) {
        const auto& places = stardraft::cmbw::establishingShotPlaces();
        if (i < 0 || i >= int(places.size())) return;
        change([&](stardraft::cmbw::EstablishingShot& s) { s.pages.front().place = places[size_t(i)].first; },
               std::wstring(tr(L"The text of the establishing shot is at ")) + tr(widen(places[size_t(i)].second).c_str()));
    }
    void editFade() {
        const Editor* ed = appState().ed.get();
        const ArchiveFile* f = ed ? ed->archiveFile("rez\\est.txt") : nullptr;
        if (!f) return;
        const auto shot = stardraft::cmbw::parseEstablishingShot(std::string(f->data.begin(), f->data.end()));
        wchar_t now[32];
        std::swprintf(now, 32, L"%g", (shot.fadeMs < 0 ? 0 : shot.fadeMs) / 1000.0);
        std::wstring typed = now;
        if (!shellPromptText(tr(L"Fade time of the establishing shot (seconds)"), typed)) return;
        const double v = std::wcstod(typed.c_str(), nullptr);
        if (!(v >= 0) || v > 600) return shellErrorBox(tr(L"Type a number of seconds from 0 to 600, for example 5"), tr(L"Fade time"));
        change([&](stardraft::cmbw::EstablishingShot& s) { s.fadeMs = int(std::lround(v * 1000)); }, tr(L"Changed the fade time of the establishing shot"));
    }
    void removeShot() {
        Editor* ed = appState().ed.get();
        if (!ed) return;
        ed->removeArchiveFiles({mapName(*ed, "rez\\est.txt"), mapName(*ed, "est\\est.pcx")});
        setMessage(tr(L"Removed the establishing shot"));
        invalidate();
    }
};
