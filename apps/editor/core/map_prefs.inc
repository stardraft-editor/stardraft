// Each map's own settings in a folder of the user's, for any map (a project's map, a .scx or .scm of its own): the
// folder Maps next to the settings file (macOS: ~/Library/Application Support/StarDraft/Maps), a folder for each map
// (its file's name and a hash of its full path: a map that moves starts again), with map.json in it. It keeps the
// doodad pins of a map that is not a project's (a project keeps its own) and the map's palette panels
// (sdl_palette_panels.inc). Not in the self-test or the screenshot runs without a settings file of their own.

namespace fs = std::filesystem;

// The maps' own files are read and written: the program's start (a snapshot with --settings: next to that file).
bool gMapPrefsOn = true;

// The file of the map at `map` ("": none: a map not saved yet, or no settings).
fs::path mapPrefsFile(const fs::path& map) {
    if (map.empty() || gSelfTest || !gMapPrefsOn) return {};
    const fs::path settings = prefs::store().file();
    if (settings.empty()) return {};
    std::error_code ec;
    const fs::path full = fs::absolute(map, ec);
    const std::string key = (ec ? map : full).lexically_normal().generic_string();
    uint32_t h = 2166136261u;  // FNV-1a
    for (unsigned char c : key) h = (h ^ c) * 16777619u;
    std::string stem = narrow(map.stem().wstring());
    for (char& c : stem)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '(' || c == ')' || c == '.')) c = '_';
    if (stem.size() > 40) stem.resize(40);
    char hex[16];
    std::snprintf(hex, sizeof hex, "%08x", h);
    return settings.parent_path() / "Maps" / (stem + "-" + hex) / "map.json";
}
// The active tab's map's file ("": none).
fs::path activeMapPrefsFile() {
    AppState& app = shellState();
    return docOpen() && app.ed ? mapPrefsFile(app.ed->path()) : fs::path();
}

// The doodad pins of the active tab: its project's, else its map's file's. canPinDoodads: there is a place to keep them.
bool canPinDoodads() { return activeProject() || !activeMapPrefsFile().empty(); }
std::vector<uint16_t> doodadPinsNow() {
    if (const EudProject* pr = activeProject()) return pr->doodadPins();
    const fs::path f = activeMapPrefsFile();
    std::vector<uint16_t> out;
    if (f.empty()) return out;
    stardraft::Settings s(f);
    if (const auto l = s.list("DoodadPins"))
        for (const std::string& v : *l)
            if (const long n = std::strtol(v.c_str(), nullptr, 10); n >= 0 && n < 65536) out.push_back(uint16_t(n));
    return out;
}
// Keeps them at once; false with `error` when the file cannot be written.
bool setDoodadPinsNow(const std::vector<uint16_t>& pins, std::string& error) {
    if (EudProject* pr = activeProject()) {
        pr->setDoodadPins(pins);
        return gSelfTest || pr->save(error);
    }
    const fs::path f = activeMapPrefsFile();
    if (f.empty()) return false;
    stardraft::Settings s(f);
    std::vector<std::string> list;
    for (uint16_t p : pins) list.push_back(std::to_string(p));
    if (!s.set("DoodadPins", list)) return error = s.lastError(), false;
    return true;
}
