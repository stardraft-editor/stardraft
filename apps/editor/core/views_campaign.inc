// The Campaign window (roadmap R25; portable, core.inc): File > Campaign > New campaign, Open campaign and Show the
// campaign. A campaign is a folder with campaign.json (stardraft/campaign.h): its maps in their order. The window lists
// them; the buttons add the open map or a map file, move a map up or down, take it out of the list (the file stays),
// open it in a tab, and copy its data (unit, upgrade and technology settings) or its terrain into the open map (one
// undo step in the open map).

stardraft::Campaign gCampaign;
bool gCampaignOpen = false;  // a campaign is open (gCampaign holds it)

void shellShowCampaignWindow();  // (the host: the window, once)
std::wstring shellPickFolder(const std::wstring& title);
std::filesystem::path shellPickFile(const ShellFilePick& pick);
void openAny(const std::filesystem::path& path);

class CampaignView final : public draw::View {
public:
    CampaignView()
        : addOpen_(L"Add the open map", stardraft::kit::Tone::Normal, [this] { addOpenMap(); }),
          addFile_(L"Add a map\x2026", stardraft::kit::Tone::Normal, [this] { addMapFile(); }),
          up_(L"Move up", stardraft::kit::Tone::Normal, [this] { move(-1); }),
          down_(L"Move down", stardraft::kit::Tone::Normal, [this] { move(1); }),
          remove_(L"Take out", stardraft::kit::Tone::Normal, [this] { takeOut(); }),
          open_(L"Open", stardraft::kit::Tone::Primary, [this] { openSelected(); }),
          copyData_(L"Copy its data into the open map", stardraft::kit::Tone::Normal, [this] { copyInto(false); }),
          copyTerrain_(L"Copy its terrain into the open map", stardraft::kit::Tone::Normal, [this] { copyInto(true); }) {
        title_.setFont({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)});
        note_.setTone(stardraft::kit::LabelTone::Dim);
        note_.setWrap(true);
        list_.label = [](int i) {
            return std::to_wstring(i + 1) + L". " + gCampaign.maps[size_t(i)].filename().wstring() +
                   (gCampaign.maps[size_t(i)].has_parent_path() ? L"   (" + gCampaign.mapFile(size_t(i)).parent_path().wstring() + L")" : L"");
        };
        list_.onActivate = [this](int) { openSelected(); };
        list_.onSelect = [this] { enable(); };
        list_.setEmptyHint(L"No maps yet: add the open map, or a map file");
        add(title_, {0, 0, 0, 0});
        add(note_, {0, 0, 0, 0});
        add(list_, {0, 0, 0, 0});
        for (stardraft::kit::Button* b : buttons()) add(*b, {0, 0, 0, 0});
    }
    static draw::Point size() { return {scale(760), scale(460)}; }

    // The campaign again (another one opened, or its list changed).
    void refresh() {
        title_.setText(gCampaign.name.empty() ? std::wstring(L"Campaign") : widen(gCampaign.name));
        note_.setText(gCampaign.folder.wstring());
        list_.setCount(int(gCampaign.maps.size()));
        enable();
        invalidate();
    }

    void resized() override {
        const int pad = scale(16), bw = scale(250), bh = scale(stardraft::kit::kButtonH), gap = scale(8);
        title_.setFrame({pad, pad, width() - pad, pad + scale(30)});
        note_.setFrame({pad, pad + scale(32), width() - pad, pad + scale(52)});
        const int top = pad + scale(60);
        list_.setFrame({pad, top, width() - pad - bw - gap * 2, height() - pad});
        int y = top;
        for (stardraft::kit::Button* b : buttons()) {
            b->setFrame({width() - pad - bw, y, width() - pad, y + bh});
            y += bh + gap + (b == &addFile_ || b == &open_ ? scale(10) : 0);
        }
    }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }

private:
    std::vector<stardraft::kit::Button*> buttons() { return {&addOpen_, &addFile_, &up_, &down_, &remove_, &open_, &copyData_, &copyTerrain_}; }
    int selected() const {
        const int i = list_.cursor();
        return i >= 0 && i < int(gCampaign.maps.size()) ? i : -1;
    }
    void enable() {
        const int i = selected();
        const AppState& app = shellState();
        const bool mapOpen = app.ed && app.ed->isOpen();
        addOpen_.setEnabled(mapOpen && !app.ed->path().empty());
        up_.setEnabled(i > 0), down_.setEnabled(i >= 0 && i + 1 < int(gCampaign.maps.size()));
        remove_.setEnabled(i >= 0), open_.setEnabled(i >= 0);
        copyData_.setEnabled(i >= 0 && mapOpen), copyTerrain_.setEnabled(i >= 0 && mapOpen);
    }
    void saved() {
        std::string err;
        if (!gCampaign.save(err)) shellErrorBox(widen(err), L"Campaign");
        refresh();
    }
    void addOpenMap() {
        const AppState& app = shellState();
        if (!app.ed || app.ed->path().empty()) return;
        if (!gCampaign.add(app.ed->path())) return setMessage(L"The map is in the campaign already");
        saved();
        list_.selectOnly(int(gCampaign.maps.size()) - 1);
    }
    void addMapFile() {
        ShellFilePick p;
        p.title = L"Add a map to the campaign";
        p.filters = {{L"StarCraft maps (*.scx, *.scm)", L"*.scx;*.scm"}};
        p.initialDir = gCampaign.folder;
        const std::filesystem::path f = shellPickFile(p);
        if (f.empty()) return;
        if (!gCampaign.add(f)) return setMessage(L"The map is in the campaign already");
        saved();
        list_.selectOnly(int(gCampaign.maps.size()) - 1);
    }
    void move(int d) {
        const int i = selected();
        if (i < 0 || i + d < 0 || i + d >= int(gCampaign.maps.size())) return;
        std::swap(gCampaign.maps[size_t(i)], gCampaign.maps[size_t(i + d)]);
        saved();
        list_.selectOnly(i + d);
    }
    void takeOut() {
        const int i = selected();
        if (i < 0) return;
        gCampaign.maps.erase(gCampaign.maps.begin() + i);
        saved();
        if (!gCampaign.maps.empty()) list_.selectOnly(std::min(i, int(gCampaign.maps.size()) - 1));
    }
    void openSelected() {
        const int i = selected();
        if (i >= 0) openAny(gCampaign.mapFile(size_t(i)));
    }
    // The selected map's data or terrain into the open map (one undo step there).
    void copyInto(bool terrain) {
        const int i = selected();
        AppState& app = shellState();
        if (i < 0 || !app.ed || !app.ed->isOpen()) return;
        Editor from(app.data);
        std::string err;
        if (!from.open(gCampaign.mapFile(size_t(i)), err)) return shellErrorBox(widen(err), L"Campaign");
        const bool ok = terrain ? app.ed->copyTerrain(from.chk(), err) : app.ed->copyMapData(from.chk(), err);
        if (!ok) return shellErrorBox(widen(err), L"Campaign");
        app.dirty = true;
        setMessage((terrain ? L"Copied the terrain of " : L"Copied the data of ") + gCampaign.maps[size_t(i)].filename().wstring() + L" into the open map");
    }

    stardraft::kit::Label title_, note_;
    stardraft::kit::ListView list_;
    stardraft::kit::Button addOpen_, addFile_, up_, down_, remove_, open_, copyData_, copyTerrain_;
};
CampaignView gCampaignView;

// File > Campaign > New campaign: a folder (made or empty), named after it.
void campaignNew() {
    const std::wstring dir = shellPickFolder(L"Select or make a folder for the campaign");
    if (dir.empty()) return;
    stardraft::Campaign c;
    std::string err;
    const std::filesystem::path folder(dir);
    if (!c.create(folder, narrow(folder.filename().wstring()), err)) return shellErrorBox(widen(err), L"New campaign");
    gCampaign = c, gCampaignOpen = true;
    gCampaignView.refresh();
    shellShowCampaignWindow();
}
// File > Campaign > Open campaign: a folder with campaign.json.
void campaignOpen() {
    const std::wstring dir = shellPickFolder(L"Select the campaign's folder");
    if (dir.empty()) return;
    stardraft::Campaign c;
    std::string err;
    if (!c.load(std::filesystem::path(dir), err)) return shellErrorBox(widen(err), L"Open campaign");
    gCampaign = c, gCampaignOpen = true;
    gCampaignView.refresh();
    shellShowCampaignWindow();
}
void campaignShow() {
    if (!gCampaignOpen) return campaignOpen();
    gCampaignView.refresh();
    shellShowCampaignWindow();
}
