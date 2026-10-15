// The sound files of the map and of the game, by their names (portable, core.inc): the map's sound files (WAV, OGG),
// the game's sounds (a map can name a game sound by its path, "sound\misc\buzz.wav", without holding it), and the bytes
// of a sound by its name. Decoding and playing are the program's (sound_files.inc).

bool isSoundName(const std::string& n) {
    if (n.size() < 4) return false;
    std::string e = n.substr(n.size() - 4);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e == ".wav" || e == ".ogg";
}

// A sound the game keeps in another folder: campaign maps name theirs as "staredit\wav\name.wav", which the game
// has under campaign\...\staredit\wav (Remastered: often as .ogg). The same name ending, without the extension:
// the game's own folders first, then English, HD before SD.
std::string gameSoundElsewhere(const std::string& name) {
    static std::map<std::string, std::pair<int, std::string>> index;  // lower-case "dir\stem" -> (rank, path)
    auto key = [](std::string n) {
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        const size_t dot = n.rfind('.');
        if (dot != std::string::npos && n.find('\\', dot) == std::string::npos) n.resize(dot);
        const size_t s1 = n.rfind('\\');
        const size_t s2 = s1 == std::string::npos || s1 == 0 ? std::string::npos : n.rfind('\\', s1 - 1);
        return s2 == std::string::npos ? n : n.substr(s2 + 1);  // the folder and the name
    };
    static bool built = false;
    if (!built) {
        built = true;
        for (const auto& f : appState().data.listFiles()) {
            if (!isSoundName(f)) continue;
            std::string lower = f;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            const bool locale = lower.rfind("locales\\", 0) == 0, english = lower.find("enus") != std::string::npos, sd = lower.find("\\sd\\") != std::string::npos || lower.rfind("sd\\", 0) == 0;
            const int rank = (locale ? (english ? 2 : 4) : 0) + (sd ? 1 : 0);
            auto [it, fresh] = index.try_emplace(key(f), rank, f);
            if (!fresh && rank < it->second.first) it->second = {rank, f};
        }
    }
    const auto it = index.find(key(name));
    return it == index.end() ? std::string() : it->second.second;
}

// A sound's bytes by its path: the map's archive first, then the game's files.
const Bytes* soundFileBytes(const std::string& name) {
    if (name.empty()) return nullptr;
    AppState& st = appState();
    if (st.ed)
        if (const ArchiveFile* f = st.ed->archiveFile(name)) return &f->data;
    static std::map<std::string, Bytes> game;
    auto [it, fresh] = game.try_emplace(name);
    if (fresh) {
        std::string err;
        if (!st.data.readFile(name, it->second, err)) {
            it->second.clear();
            const std::string other = gameSoundElsewhere(name);
            if (!other.empty() && !st.data.readFile(other, it->second, err)) it->second.clear();
        }
    }
    return it->second.empty() ? nullptr : &it->second;
}

// The map's sounds: its sound files, then the game sounds it names by reference.
std::vector<std::string> mapSounds() {
    std::vector<std::string> out;
    AppState& st = appState();
    if (!st.ed) return out;
    for (const ArchiveFile& f : st.ed->archiveFiles())
        if (isSoundName(f.name)) out.push_back(f.name);
    for (const auto& r : st.ed->soundReferences()) out.push_back(r);
    return out;
}

// The game's sounds: every file under sound\ (their paths, as a map names them).
const std::vector<std::string>& gameSounds() {
    static std::vector<std::string> out;
    static bool listed = false;
    if (!listed) {
        listed = true;
        for (const auto& n : appState().data.listFiles()) {
            if (n.size() < 7 || !isSoundName(n)) continue;
            std::string lower = n.substr(0, 6);
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "sound\\") out.push_back(n);
        }
        // Sorted without case (as the list has always been: the names differ only in case rarely, but the order must not change).
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
        });
    }
    return out;
}

// A size in bytes, KB or MB, as the table shows it.
std::wstring sizeText(size_t n) {
    wchar_t b[32];
    if (n < 1024) std::swprintf(b, 32, L"%zu B", n);
    else if (n < 1024 * 1024) std::swprintf(b, 32, L"%.0f KB", n / 1024.0);
    else std::swprintf(b, 32, L"%.1f MB", n / 1048576.0);
    return b;
}
// A length as m:ss.t
std::wstring lengthText(uint32_t ms) {
    wchar_t b[32];
    std::swprintf(b, 32, L"%u:%04.1f", ms / 60000, (ms % 60000) / 1000.0);
    return b;
}
