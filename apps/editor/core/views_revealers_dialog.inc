// Place map revealers (Edit > Tools; Discord feedback #125; portable, core.inc): the players that get a map revealer, each
// (Player 1 to Player 8), or All players, in a kit Dialog (Host::runModal). OK is on while a player is chosen. The host
// (sdl_map_dialogs.inc: showMapRevealersDialog) runs it; the command places the revealers (placeMapRevealersCommand).

// The dialog's results (what its buttons return).
constexpr int kRevealersOk = 1, kRevealersCancel = 2;

// The layout in pixels at 100 %, from the window's client area: two columns of four players, All players under them.
namespace revealersLayout {
constexpr int kPad = 16;                                  // ui::kDialogPad
constexpr int kColW = 180, kContentW = 2 * kColW;
constexpr int kRowH = 24, kPitch = 32;                    // ui::kFieldH, ui::kFormRowH
constexpr int kChecksY = kPad + 40;                       // 56: the players
constexpr int kAllY = kChecksY + 4 * kPitch + 8;          // 192: All players
constexpr int kButtonsY = kAllY + kPitch + 16;            // 256: OK and Cancel (ui::kButtonH, 28 px)
constexpr int kButtonH = 28;
constexpr int kWindowH = kButtonsY + kButtonH + kPad;     // 300
}  // namespace revealersLayout

class RevealersView final : public draw::View {
public:
    RevealersView() {
        using stardraft::kit::LabelTone;
        text_.setText(tr(L"One map revealer on each square of 8 x 8 tiles."));
        text_.setTone(LabelTone::Dim);
        add(text_, {0, 0, 0, 0});
        for (int p = 0; p < 8; ++p) {
            players_[p].setLabel(tr(L"Player ") + std::to_wstring(p + 1));
            players_[p].setPlainLabel(true);  // (a native check box's label is the text color)
            players_[p].setOn(true);
            players_[p].onChange = [this](bool) { sync(); };
            add(players_[p], {0, 0, 0, 0});
        }
        all_.setLabel(tr(L"All players"));
        all_.setPlainLabel(true);
        all_.setOn(true);
        all_.onChange = [this](bool on) {  // All players sets every player
            for (auto& p : players_) p.setOn(on);
            sync();
        };
        add(all_, {0, 0, 0, 0});
    }

    // The dialog this view is in (its OK follows the choice).
    void setDialog(stardraft::kit::Dialog* d) { dlg_ = d; }
    // Whether a player is chosen (OK is on).
    bool any() const {
        for (const auto& p : players_)
            if (p.on()) return true;
        return false;
    }
    // The chosen players, 0 to 7.
    std::vector<uint8_t> owners() const {
        std::vector<uint8_t> out;
        for (int p = 0; p < 8; ++p)
            if (players_[p].on()) out.push_back(uint8_t(p));
        return out;
    }

private:
    // All players is on while every player is; OK is on while one is.
    void sync() {
        bool all = true;
        for (const auto& p : players_) all = all && p.on();
        all_.setOn(all);
        if (dlg_) dlg_->setEnabledFor(kRevealersOk, any());
    }

    void paint(draw::Canvas& c) override {
        using namespace revealersLayout;
        c.fill({0, 0, width(), height()}, kBg);
    }

    void resized() override {
        using namespace revealersLayout;
        auto put = [](draw::View& v, int x, int y, int w, int h) { v.setFrame({scale(x), scale(y), scale(x + w), scale(y + h)}); };
        put(text_, 0, kPad, kContentW, kRowH);
        for (int p = 0; p < 8; ++p) put(players_[p], (p / 4) * kColW, kChecksY + (p % 4) * kPitch, kColW - 8, kRowH);
        put(all_, 0, kAllY, kColW - 8, kRowH);
    }

    stardraft::kit::Dialog* dlg_ = nullptr;
    stardraft::kit::Label text_;
    stardraft::kit::CheckBox players_[8];
    stardraft::kit::CheckBox all_;
};

// The dialog around `view`: its buttons, the OK rule and the insets. Returns the window's size (the snapshots show it
// without a window).
inline draw::Point revealersDialogSetup(stardraft::kit::Dialog& dlg, RevealersView& view) {
    using namespace revealersLayout;
    dlg.setContent(view);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"OK"), kRevealersOk, true, false, false, 0});
    buttons.push_back({tr(L"Cancel"), kRevealersCancel, false, true, false, 0});
    dlg.setButtons(std::move(buttons));
    dlg.setAllow([&view](int r) { return r != kRevealersOk || view.any(); });
    view.setDialog(&dlg);
    dlg.setInsets({kPad, 0, kPad, kPad, -(kButtonH + kPad), 8});
    return draw::Point{scale(kContentW + 2 * kPad), scale(kWindowH)};
}

// Runs Place map revealers from `from` (the program's anchor view). OK puts the chosen players into `owners`; Cancel and
// the window's close box leave it as it was. Returns true when OK was pressed.
inline bool runRevealersDialog(draw::View& from, std::vector<uint8_t>& owners) {
    RevealersView view;
    stardraft::kit::Dialog dlg;
    const draw::Point size = revealersDialogSetup(dlg, view);
    const int r = dlg.run(from, tr(L"Place map revealers"), size);
    if (r != kRevealersOk) return false;
    owners = view.owners();
    return true;
}
