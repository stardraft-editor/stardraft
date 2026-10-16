// Options > Program options (Ctrl+,): the options of the program in one window (portable, core.inc; the snap and placement
// options are in the Options menu). A view: its rows are kit widgets (check boxes, dropdowns, text fields, a slider,
// buttons), its section headers, labels, notes and accent swatches are painted on the canvas, and it scrolls with its
// own bar (or the wheel) above a footer with Close. A change applies at once, through the same command as the menu (the
// host runs command(ID_OPT_...)), so the shortcuts and the tests that call the commands get the same result. Close, Esc
// and Enter close the window. The host (options_window.inc on Windows) runs the commands, the folder pickers and the
// list pickers, and tells the view what the options are now (values()).

// The accents: a name and the swatch's color (0x00BBGGRR).
struct OptionsAccent {
    std::wstring name;
    draw::Color color = draw::kNone;
};

// The options as they are now (the host fills it before each paint).
struct OptionsValues {
    bool light = false;
    int accent = 0;
    std::vector<OptionsAccent> accents;
    bool animate = false;
    bool middleDrag = true;   // the middle button drags the map (else it pans it)
    int volume = 0;           // every sound's volume, 0 to 100
    int language = 0;         // the chosen language (its choice index, ids.inc's ID_OPT_LANG + index)
    std::vector<std::pair<int, std::wstring>> languages;  // the choices offered: (choice index, name)
    bool restartDue = false;  // the chosen language is not the one in use
    int unitNames = 0;        // how lists name a unit with a custom name (0 to 3)
    int unitLimit[2] = {3400, 1700};  // standard maps, EUD projects
    bool openLast = false;
    bool checkUpdates = false;
    int crashReports = 0;     // 0 ask, 1 send, 2 do not send
    bool mapsWithSD2 = false; // .scm and .scx open with StarDraft 2
    bool mapsRow = true;      // the Maps row shows (the file association: the Windows program's)
    std::wstring projectsFolder;  // the working folder (the default one when none is set)
    std::wstring installInUse;    // the StarCraft folder in use ("": none)
    std::wstring installNext;     // the folder used from the next start ("": the same)
};

// The program's part of the options window (options_window.inc): the values, the commands and the pickers.
class OptionsHost {
public:
    virtual ~OptionsHost() = default;
    // The options as they are now.
    virtual void values(OptionsValues& v) = 0;
    // A menu command (ids.inc): the theme, the animations, the language, crash reports, Restart now, Check now, the
    // StarCraft folder and the maps' program. The same path as the menu's.
    virtual void command(int id) = 0;
    // The accent (its swatch was clicked); the view's own theme follows it.
    virtual void setAccent(int i) = 0;
    // A unit name form, a unit limit (k: 0 standard maps, 1 EUD projects) and the volume (at once; the program saves it).
    virtual void setUnitNames(int i) = 0;
    virtual void setUnitLimit(int k, int v) = 0;
    virtual void setVolume(int v) = 0;
    // The working folder: a folder chosen by the user, or the default one.
    virtual void browseWorkingFolder() = 0;
    virtual void useDefaultWorkingFolder() = 0;
    // The window's title bar and scroll bar follow the theme (the Windows host; a no-op elsewhere).
    virtual void themeShown(bool light) { (void)light; }
    // Closes the window (Close, Esc, Enter).
    virtual void close() = 0;
};

namespace optionsview {

// The names of the unit name forms (the dropdown's items).
const wchar_t* const kUnitNameForms[4] = {L"Terran Marine", L"Terran Marine (Custom Name)", L"Custom Name (Terran Marine)", L"Custom Name"};

// The small font of the notes (the labels' size less a sixth).
inline draw::Font notesFont(draw::Canvas& c) { return c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}); }

}  // namespace optionsview

class OptionsView final : public draw::View {
public:
    explicit OptionsView(OptionsHost& host) : host_(host) {
        lightSeg_[0].setText(L"Dark"), lightSeg_[0].setTip(L"Dark theme (default)");
        lightSeg_[1].setText(L"Light"), lightSeg_[1].setTip(L"Light theme");
        middleSeg_[0].setText(L"Drag map"), middleSeg_[0].setTip(L"The map moves with the mouse");
        middleSeg_[1].setText(L"Pan map"), middleSeg_[1].setTip(L"The farther the mouse is from where you pressed, the faster the map moves toward it (default)");
        crashSeg_[0].setText(L"Ask"), crashSeg_[0].setTip(L"After a crash, StarDraft 2 asks at the next start if it can send the report (default)");
        crashSeg_[1].setText(L"Send"), crashSeg_[1].setTip(L"Send an anonymous report after a crash: the version, Windows, the video card, where it crashed and the last log lines; no maps");
        crashSeg_[2].setText(L"Do not send"), crashSeg_[2].setTip(L"The reports stay on this computer");
        for (int i = 0; i < 2; ++i) {
            lightSeg_[i].onClick = [this, i] { host_.command(i ? ID_THEME_LIGHT : ID_THEME_DARK); };
            middleSeg_[i].onClick = [this, i] { host_.command(ID_MIDDLE_MODE + (i == 0 ? 1 : 0)); };
        }
        for (int i = 0; i < 3; ++i) crashSeg_[i].onClick = [this, i] { host_.command(ID_CRASH_MODE + i); };
        for (auto* b : allButtons()) add(*b, {0, 0, 0, 0}), b->setVisible(false);
        animate_.setLabel(L"Animate units");
        animate_.setTip(L"Units on the map play their animations");
        animate_.onChange = [this](bool) { host_.command(ID_OPT_ANIMATE); };
        lastProject_.setLabel(L"Open the last project on start");
        lastProject_.onChange = [this](bool) { host_.command(ID_OPT_LASTPROJECT); };
        updates_.setLabel(L"Look for updates on start");
        updates_.setTip(L"Release builds only");
        updates_.onChange = [this](bool) { host_.command(ID_OPT_UPDATECHECK); };
        for (auto* c : {static_cast<draw::View*>(&animate_), static_cast<draw::View*>(&lastProject_), static_cast<draw::View*>(&updates_)})
            add(*c, {0, 0, 0, 0}), c->setVisible(false);
        language_.onChange = [this](int i) {
            if (i >= 0 && i < int(langChoices_.size())) host_.command(ID_OPT_LANG + langChoices_[size_t(i)]);
        };
        unitNames_.onChange = [this](int i) { host_.setUnitNames(i); };
        add(language_, {0, 0, 0, 0}), language_.setVisible(false);
        add(unitNames_, {0, 0, 0, 0}), unitNames_.setVisible(false);
        std::vector<stardraft::kit::DropdownItem> forms;
        for (const wchar_t* n : optionsview::kUnitNameForms) forms.push_back({n});
        unitNames_.setItems(forms);
        slider_.setRange(0, 100), slider_.setStep(1);
        slider_.onChange = [this](int v) { host_.setVolume(v), invalidate(); };
        add(slider_, {0, 0, 0, 0}), slider_.setVisible(false);
        for (int k = 0; k < 2; ++k) {
            limit_[k].setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
            limit_[k].setMaxLength(5);
            limit_[k].onEnter = [this, k] { commitLimit(k); };
            limit_[k].onFocusLost = [this, k] { commitLimit(k); };
            // Escape gives back the saved value (the window stays open): the typed number is not kept.
            limit_[k].onEscape = [this, k] { limit_[k].setText(std::to_wstring(limitValue_[k])); };
            add(limit_[k], {0, 0, 0, 0}), limit_[k].setVisible(false);
        }
        folder_.setReadOnly(true), folder_.setTabStop(false);
        install_.setReadOnly(true), install_.setTabStop(false);
        add(folder_, {0, 0, 0, 0}), add(install_, {0, 0, 0, 0});
        folder_.setVisible(false), install_.setVisible(false);
        browse_.setText(L"Browse\x2026"), browse_.setTip(L"Select the folder where StarDraft 2 makes new projects");
        browse_.onClick = [this] { host_.browseWorkingFolder(); };
        defaultFolder_.setText(L"Default"), defaultFolder_.setTip(L"Documents\\StarDraft");
        defaultFolder_.onClick = [this] { host_.useDefaultWorkingFolder(); };
        installBrowse_.setText(L"Browse\x2026"), installBrowse_.setTip(L"Select the StarCraft: Remastered folder");
        installBrowse_.onClick = [this] { host_.command(ID_OPT_INSTALL); };
        check_.setText(L"Check now"), check_.setTip(L"Look for a newer release now");
        check_.onClick = [this] { host_.command(ID_HELP_UPDATES); };
        restart_.setText(L"Restart now"), restart_.setTip(L"Start StarDraft 2 again in the new language. Your open maps and projects open again.");
        restart_.onClick = [this] { host_.command(ID_OPT_RESTART); };
        maps_.setText(L"Open maps with StarDraft 2"), maps_.setTip(L"Make StarDraft 2 the program that opens .scm and .scx maps");
        maps_.onClick = [this] { host_.command(ID_OPT_MAPSDEFAULT); };
        close_.setText(L"Close"), close_.setTone(stardraft::kit::Tone::Primary);
        close_.onClick = [this] { host_.close(); };
        for (auto* b : {&browse_, &defaultFolder_, &installBrowse_, &check_, &restart_, &maps_, &close_}) b->setVisible(false);
    }

    // The window's size (the host asks): the content's height and what the footer takes.
    int contentHeight() const { return height_; }

    // The part of the window named `name` at its last paint (view coordinates, scrolled): the tests click them.
    bool part(const std::string& name, draw::Rect& r) const {
        const auto it = parts_.find(name);
        if (it == parts_.end()) return false;
        r = it->second;
        return true;
    }
    bool hasPart(const std::string& name) const { return parts_.count(name) != 0; }
    // A click at the middle of the part (the widget under it gets the press and the release, as a mouse does).
    bool clickPart(const std::string& name) {
        draw::Rect r;
        if (!part(name, r)) return false;
        const draw::Point c{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
        draw::Point local;
        draw::View* v = viewAt(c, local);
        if (v == this) return false;  // scrolled out of the content: no widget is there
        draw::MouseEvent d;
        d.kind = draw::MouseKind::Down, d.pt = local;
        v->mouse(d);
        d.kind = draw::MouseKind::Up;
        v->mouse(d);
        return true;
    }
    // Scrolls the content to `s` (the tests scroll a part into view; the wheel and the bar do it too).
    void scrollTo(int s) {
        s = std::clamp(s, 0, maxScroll());
        if (s != scroll_) scroll_ = s, invalidate();
    }
    int scroll() const { return scroll_; }
    // The window was closed: the next one starts at the top (no paint: the window is going).
    void resetScroll() { scroll_ = 0; }
    // The fields and check boxes the tests drive.
    stardraft::kit::CheckBox& animate() { return animate_; }
    stardraft::kit::TextField& limitField(int k) { return limit_[k]; }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if (e.key == draw::Key::Escape || e.key == draw::Key::Enter) return host_.close(), true;
        return false;
    }
    std::wstring tip(draw::Point pt) override {
        const int i = accentAt(pt);
        return i >= 0 && i < int(accents_.size()) ? accents_[size_t(i)].name + (i == 0 ? L" (default)" : L"") : std::wstring();
    }
    draw::Cursor cursor(draw::Point pt) override { return accentAt(pt) >= 0 ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    void resized() override { invalidate(); }
    bool vscroll(int code, int pos, int track, int page) override;
    bool windowScrollBar(int& range, int& pageSize, int& pos) override {
        range = std::max(0, height_ - 1), pageSize = std::max(0, contentBottom_), pos = scroll_;
        return true;
    }

private:
    // The buttons (the segmented choices, the actions) and the children that are not rows.
    std::vector<stardraft::kit::Button*> allButtons() {
        std::vector<stardraft::kit::Button*> out;
        for (int i = 0; i < 2; ++i) out.push_back(&lightSeg_[i]), out.push_back(&middleSeg_[i]);
        for (int i = 0; i < 3; ++i) out.push_back(&crashSeg_[i]);
        for (auto* b : {&browse_, &defaultFolder_, &installBrowse_, &check_, &restart_, &maps_, &close_}) out.push_back(b);
        return out;
    }
    int maxScroll() const { return std::max(0, height_ - std::max(0, contentBottom_)); }
    int accentAt(draw::Point pt) const {
        for (size_t i = 0; i < accentRects_.size(); ++i)
            if (accentRects_[i].contains(pt)) return int(i);
        return -1;
    }
    void commitLimit(int k) {
        const std::wstring t = limit_[k].text();
        const int v = t.empty() ? 1 : std::clamp(int(std::wcstol(t.c_str(), nullptr, 10)), 1, 65535);
        if (v != limitValue_[k]) host_.setUnitLimit(k, v);
        limitValue_[k] = v;
        limit_[k].setText(std::to_wstring(v));
    }
    // Places a child at `r` (view coordinates) for this paint; a child is shown when it is wholly in the content (the
    // footer is not drawn over). A child that is not placed in a paint is hidden at its end.
    void place(draw::View& v, draw::Rect r, bool show, int bottom = -1) {
        placed_.push_back(&v);
        const bool vis = show && r.top >= 0 && r.bottom <= (bottom < 0 ? contentBottom_ : bottom);
        if (v.visible() != vis) v.setVisible(vis);
        if (vis && (v.frame().left != r.left || v.frame().top != r.top || v.frame().right != r.right || v.frame().bottom != r.bottom)) v.setFrame(r);
    }
    void hideUnplaced() {
        for (draw::View* ch : children())
            if (ch->visible() && std::find(placed_.begin(), placed_.end(), ch) == placed_.end()) ch->setVisible(false);
        placed_.clear();
    }
    void sync(const OptionsValues& v);

    OptionsHost& host_;
    stardraft::kit::Button lightSeg_[2], middleSeg_[2], crashSeg_[3];
    stardraft::kit::Button browse_, defaultFolder_, installBrowse_, check_, restart_, maps_, close_;
    stardraft::kit::CheckBox animate_, lastProject_, updates_;
    stardraft::kit::Dropdown language_{{}, -1}, unitNames_{{}, -1};
    stardraft::kit::Slider slider_;
    stardraft::kit::TextField limit_[2], folder_, install_;
    std::vector<int> langChoices_;     // the choice index of each language dropdown item
    std::vector<OptionsAccent> accents_;
    std::vector<draw::Rect> accentRects_;
    int hoverAccent_ = -1;
    int lightChosen_ = -1, middleChosen_ = -1, crashChosen_ = -1;  // the segmented choices' Primary buttons (sync)
    int limitValue_[2] = {3400, 1700};
    int scroll_ = 0;        // the content's scroll position (px)
    int height_ = 0;        // the content's full height (px), at the last paint
    int contentBottom_ = 0; // the bottom of the content (above the footer), at the last paint
    bool lightShown_ = false, lightKnown_ = false;
    bool uiBuilt_ = false;
    std::map<std::string, draw::Rect> parts_;  // where each option is (view px, scrolled), for the tests
    std::vector<draw::View*> placed_;          // the children placed in this paint
};

// The rows of the options: the same places as the window had.
void OptionsView::sync(const OptionsValues& v) {
    // A segmented choice's Primary button is the chosen one (set only when the choice changes: a tone change repaints).
    const int light = v.light ? 1 : 0, middle = v.middleDrag ? 0 : 1, crash = v.crashReports;
    if (light != lightChosen_) {
        lightChosen_ = light;
        for (int i = 0; i < 2; ++i) lightSeg_[i].setTone(i == light ? stardraft::kit::Tone::Primary : stardraft::kit::Tone::Normal);
    }
    if (middle != middleChosen_) {
        middleChosen_ = middle;
        for (int i = 0; i < 2; ++i) middleSeg_[i].setTone(i == middle ? stardraft::kit::Tone::Primary : stardraft::kit::Tone::Normal);
    }
    if (crash != crashChosen_) {
        crashChosen_ = crash;
        for (int i = 0; i < 3; ++i) crashSeg_[i].setTone(i == crash ? stardraft::kit::Tone::Primary : stardraft::kit::Tone::Normal);
    }
    accents_ = v.accents;
    animate_.setOn(v.animate), lastProject_.setOn(v.openLast), updates_.setOn(v.checkUpdates);
    // The languages offered (rebuilt when they change), the chosen one.
    if (langChoices_.size() != v.languages.size() || !std::equal(langChoices_.begin(), langChoices_.end(), v.languages.begin(), [](int a, const auto& b) {
            return a == b.first;
        })) {
        std::vector<stardraft::kit::DropdownItem> items;
        langChoices_.clear();
        for (const auto& [choice, name] : v.languages) items.push_back({name}), langChoices_.push_back(choice);
        language_.setItems(items);
    }
    int lang = -1;
    for (size_t i = 0; i < langChoices_.size(); ++i)
        if (langChoices_[i] == v.language) lang = int(i);
    if (language_.value() != lang) language_.setValue(lang);
    if (unitNames_.value() != v.unitNames) unitNames_.setValue(v.unitNames);
    for (int k = 0; k < 2; ++k) {
        limitValue_[k] = v.unitLimit[k];
        if (!limit_[k].hasFocus() && limit_[k].text() != std::to_wstring(v.unitLimit[k])) limit_[k].setText(std::to_wstring(v.unitLimit[k]));
    }
    if (slider_.value() != v.volume) slider_.setValue(v.volume);
    if (folder_.text() != v.projectsFolder) folder_.setText(v.projectsFolder);
    const std::wstring inst = v.installInUse.empty() ? std::wstring(L"\x2014") : v.installInUse;
    if (install_.text() != inst) install_.setText(inst);
}

void OptionsView::paint(draw::Canvas& c) {
    OptionsValues v;
    host_.values(v);
    if (!lightKnown_ || lightShown_ != v.light) lightKnown_ = true, lightShown_ = v.light, host_.themeShown(v.light);
    sync(v);
    const int W = width(), H = height();
    c.fill({0, 0, W, H}, kBg);
    const int pad = scale(stardraft::kit::kDialogPad), rowH = scale(stardraft::kit::kFormRowH), fieldH = scale(stardraft::kit::kFieldH);
    const int btnH = scale(stardraft::kit::kButtonH), gap = scale(stardraft::kit::kButtonGap);
    const int footerH = pad + btnH + pad;
    contentBottom_ = std::max(0, H - footerH);
    const int x0 = pad, labelW = scale(210), colRight = W - pad, fx = x0 + labelW;
    int y = pad - scroll_;
    parts_.clear();
    accentRects_.clear();
    const draw::Font of = c.setFont(c.font({}));  // (the UI font; the notes use the small one)
    auto fieldTop = [&](int rowY) { return rowY + (rowH - fieldH) / 2; };
    auto buttonTop = [&](int rowY) { return rowY + (rowH - btnH) / 2; };
    auto header = [&](const wchar_t* name) {
        c.text({x0, y, colRight, y + rowH}, name, kText, draw::kMiddle);
        c.fill({x0, y + rowH - 1, colRight, y + rowH}, kBorder);
        y += rowH + scale(6);
    };
    auto label = [&](const wchar_t* name) { c.text({x0, y, x0 + labelW, y + rowH}, name, kDim, draw::kMiddle | draw::kEllipsis); };
    auto note = [&](int x, int noteY, const std::wstring& text) {
        const draw::Font o = c.setFont(optionsview::notesFont(c));
        c.text({x, noteY, colRight, noteY + scale(20)}, text, kDim, draw::kMiddle | draw::kEllipsis);
        c.setFont(o);
    };
    // A button of a row at x (its own width), its top at `top`.
    auto buttonAt = [&](stardraft::kit::Button& b, int x, int top, int h, const std::string& part) {
        const int w = b.preferredWidth(c);
        const draw::Rect r{x, top, x + w, top + h};
        place(b, r, true);
        parts_[part] = r;
        return r.right;
    };
    // A small button in a row (ui::smallButton): its text's width + 20, at most 24 high, in the row's middle.
    auto smallAt = [&](stardraft::kit::Button& b, int x, int rowY, const std::string& part) {
        const int bh = std::min(rowH, scale(24)), w = c.measure(b.text()) + scale(20), top = rowY + (rowH - bh) / 2;
        const draw::Rect r{x, top, x + w, top + bh};
        place(b, r, true);
        parts_[part] = r;
        return r.right;
    };
    // A segmented choice: one button for each choice, the chosen one Primary, 4 px apart.
    auto segmented = [&](int rowY, stardraft::kit::Button* bs, int n, const std::string& part) {
        int x = fx;
        for (int i = 0; i < n; ++i) {
            const int w = bs[i].preferredWidth(c);
            const draw::Rect r{x, fieldTop(rowY), x + w, fieldTop(rowY) + fieldH};
            place(bs[i], r, true);
            parts_[part + "." + std::to_string(i)] = r;
            x += w + scale(4);
        }
    };
    auto checkRow = [&](int rowY, stardraft::kit::CheckBox& cb, const std::string& part) {
        const draw::Rect cr{x0, rowY, x0 + scale(24) + c.measure(cb.label()), rowY + rowH};  // as ui::check: the box and the label
        place(cb, cr, true);
        parts_[part] = cr;
    };
    placed_.clear();
    c.pushClip({0, 0, W, contentBottom_});  // the content scrolls above the footer

    // Appearance.
    header(L"Appearance");
    label(L"Theme");
    segmented(y, lightSeg_, 2, "theme");
    y += rowH;
    // The language: a choice that applies at the next start (the names of the languages are in their own language).
    label(L"Language");
    {
        const draw::Rect box{fx, fieldTop(y), std::min<int>(colRight, fx + scale(260)), fieldTop(y) + fieldH};
        place(language_, box, true);
        parts_["language"] = box;
    }
    if (v.restartDue) {
        const int x = parts_["language"].right + scale(12);
        smallAt(restart_, x, y, "restart");
    }
    y += rowH;
    note(fx, y - scale(4), v.restartDue ? L"The new language shows after a restart" : L"A new language shows after a restart");
    y += scale(20);
    // The accent: the swatches (a ring around the chosen one and the one under the mouse).
    label(L"Accent color");
    {
        const int s = scale(20), top = y + (rowH - s) / 2;
        for (size_t i = 0; i < v.accents.size(); ++i) {
            const int x = fx + int(i) * (s + scale(8));
            const draw::Rect sw{x, top, x + s, top + s};
            const bool chosen = int(i) == v.accent, over = hoverAccent_ == int(i);
            if (chosen || over) {
                draw::Rect ring = sw;
                ring.left -= scale(3), ring.top -= scale(3), ring.right += scale(3), ring.bottom += scale(3);
                c.roundBox(ring, draw::kNone, chosen ? kText : kBorderHot, float(scale(stardraft::kit::kRadius)));
            }
            c.roundBox(sw, v.accents[i].color, draw::kNone, float(scale(stardraft::kit::kRadius)));
            accentRects_.push_back(sw);
            parts_["accent." + std::to_string(i)] = sw;
        }
        if (v.accent >= 0 && v.accent < int(v.accents.size()))
            note(fx + int(v.accents.size()) * (s + scale(8)) + scale(4), y + (rowH - scale(20)) / 2, v.accents[size_t(v.accent)].name);
    }
    y += rowH;
    checkRow(y, animate_, "animate");
    y += rowH;
    // The volume: its name and value, a slider.
    {
        const int rowH = scale(stardraft::kit::kRowH);  // a dense row, as the Data form's volume row
        const int right = std::min<int>(colRight, fx + scale(260));
        c.text({x0, y, x0 + labelW - scale(48), y + rowH}, L"Volume", kDim, draw::kMiddle);
        c.text({x0 + labelW - scale(56), y, x0 + labelW - scale(4), y + rowH}, std::to_wstring(v.volume) + L"%", kText, draw::kMiddle | draw::kRight);
        const int x1 = std::max(fx + scale(8) + scale(60), right - scale(8));
        const draw::Rect area{fx + scale(8) - scale(8), y, x1 + scale(8), y + rowH};
        place(slider_, area, true);
        parts_["volume"] = area;
        y += rowH;
    }
    y += pad;

    // Mouse: what holding the middle button does on the map (the middle button is the only one that moves it).
    header(L"Mouse");
    label(L"Middle mouse click and hold");
    segmented(y, middleSeg_, 2, "middle");
    y += rowH + pad;

    // Unit names: a unit with a custom name (the map's unit settings), in every list that names it.
    header(L"Unit names");
    label(L"Custom names");
    {
        const draw::Rect box{fx, fieldTop(y), std::min<int>(colRight, fx + scale(300)), fieldTop(y) + fieldH};
        place(unitNames_, box, true);
        parts_["unitnames"] = box;
        y += rowH + pad;
    }

    // Map unit limit: placing, spraying and pasting units stop there (GitHub feedback #103).
    header(L"Map unit limit");
    for (int k = 0; k < 2; ++k) {
        label(k == 0 ? L"Standard maps" : L"EUD projects");
        const draw::Rect box{fx, fieldTop(y), std::min<int>(colRight, fx + scale(120)), fieldTop(y) + fieldH};
        place(limit_[k], box, true);
        c.text({box.right + scale(6), y, box.right + scale(60), y + rowH}, L"units", kDim, draw::kMiddle);
        parts_[k == 0 ? "unitlimit.classic" : "unitlimit.eud"] = box;
        y += rowH;
    }
    note(x0, y - scale(4), L"StarCraft without mods takes at most 3400 units in a map.");
    y += scale(20) + pad;

    // Start.
    header(L"Start");
    checkRow(y, lastProject_, "lastproject");
    y += rowH;
    {
        const draw::Rect cr{x0, y, x0 + scale(24) + c.measure(updates_.label()), y + rowH};  // as ui::check
        place(updates_, cr, true);
        parts_["updatecheck"] = cr;
        smallAt(check_, cr.right + scale(16), y, "check");
    }
    y += rowH;
    // Crash reports (GitHub feedback #45): a crash always keeps its report on this computer; ask, send or not.
    label(L"Crash reports");
    segmented(y, crashSeg_, 3, "crashreports");
    y += rowH;
    // Maps (.scm, .scx): the program a double-click opens them with (where the system lets the program say it).
    if (v.mapsRow) {
        label(L"Maps");
        if (v.mapsWithSD2) note(fx, y + (rowH - scale(20)) / 2, L"Maps (.scm, .scx) open with StarDraft 2");
        else smallAt(maps_, fx, y, "maps");
        y += rowH;
    }
    y += pad;

    // The working folder: where new projects go.
    header(L"Working folder");
    {
        const int bw = browse_.preferredWidth(c), dw = defaultFolder_.preferredWidth(c);
        const draw::Rect field{x0, fieldTop(y), colRight - bw - dw - 2 * gap, fieldTop(y) + fieldH};
        place(folder_, field, true);
        const int by = buttonTop(y);
        place(defaultFolder_, {colRight - dw, by, colRight, by + btnH}, true);
        place(browse_, {colRight - dw - gap - bw, by, colRight - dw - gap, by + btnH}, true);
        parts_["workfolder"] = field;
        parts_["browse.workfolder"] = browse_.frame(), parts_["default.workfolder"] = defaultFolder_.frame();
        y += rowH;
        note(x0, y - scale(4), L"StarDraft 2 makes new projects in this folder. It does not move a project that you open from another folder.");
        y += scale(20) + pad;
    }

    // StarCraft folder.
    header(L"StarCraft folder");
    {
        const int bw = installBrowse_.preferredWidth(c);
        const draw::Rect field{x0, fieldTop(y), colRight - bw - gap, fieldTop(y) + fieldH};
        place(install_, field, true);
        const int by = buttonTop(y);
        place(installBrowse_, {colRight - bw, by, colRight, by + btnH}, true);
        parts_["install"] = {colRight - bw, by, colRight, by + btnH};
        y += rowH;
        note(x0, y - scale(4),
             !v.installNext.empty() ? L"From the next start: " + v.installNext : std::wstring(L"A new folder is used from the next start"));
        y += scale(20) + pad;
    }

    height_ = y + scroll_;
    c.popClip();
    // The footer: Close (the content scrolls under it, a line between).
    if (height_ > contentBottom_) c.fill({0, contentBottom_, W, contentBottom_ + 1}, kBorder);
    {
        const int w = close_.preferredWidth(c);
        const draw::Rect r{colRight - w, H - pad - btnH, colRight, H - pad};
        place(close_, r, true, H);
        parts_["close"] = r;
    }
    hideUnplaced();
    c.setFont(of);
    // The scroll: a bar's page is the content's height above the footer.
    if (scroll_ > maxScroll()) scroll_ = maxScroll(), invalidate();
}

bool OptionsView::mouse(const draw::MouseEvent& e) {
    if (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Leave) {
        const int i = e.kind == draw::MouseKind::Leave ? -1 : accentAt(e.pt);
        if (i != hoverAccent_) hoverAccent_ = i, invalidate();
        return false;
    }
    if (e.kind == draw::MouseKind::Wheel) {
        scrollTo(scroll_ - int(draw::wheelDistance(e, float(scale(40) * 3))));
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 0) {
        const int i = accentAt(e.pt);
        if (i >= 0) {
            host_.setAccent(i);
            return true;
        }
    }
    return false;
}

bool OptionsView::vscroll(int code, int pos, int track, int page) {
    int s = scroll_;
    switch (code) {
    case 0: s = scroll_ - scale(40); break;  // SB_LINEUP
    case 1: s = scroll_ + scale(40); break;  // SB_LINEDOWN
    case 2: s = scroll_ - page; break;       // SB_PAGEUP
    case 3: s = scroll_ + page; break;       // SB_PAGEDOWN
    case 4: s = pos; break;                  // SB_THUMBPOSITION
    case 5: s = track; break;                // SB_THUMBTRACK
    case 6: s = 0; break;                    // SB_TOP
    case 7: s = height_; break;              // SB_BOTTOM
    default: return false;
    }
    scrollTo(s);
    return true;
}
