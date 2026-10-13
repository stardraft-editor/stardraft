// Sounds for the map (included into main.cpp before briefing_view.inc): the map's own sound files (WAV, OGG) and
// the game's (a map can name a game sound by its path, "sound\misc\buzz.wav", without holding it).

extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

bool isSoundName(const std::string& n);

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
        for (const auto& f : app.data.listFiles()) {
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
    if (app.ed)
        if (const ArchiveFile* f = app.ed->archiveFile(name)) return &f->data;
    static std::map<std::string, Bytes> game;
    auto [it, fresh] = game.try_emplace(name);
    if (fresh) {
        std::string err;
        if (!app.data.readFile(name, it->second, err)) {
            it->second.clear();
            const std::string other = gameSoundElsewhere(name);
            if (!other.empty() && !app.data.readFile(other, it->second, err)) it->second.clear();
        }
    }
    return it->second.empty() ? nullptr : &it->second;
}
bool isOgg(const Bytes& b) { return b.size() >= 4 && std::memcmp(b.data(), "OggS", 4) == 0; }

// A sound as samples the audio device takes.
struct DecodedSound {
    SDL_AudioSpec spec{};
    std::vector<uint8_t> pcm;
    uint32_t ms = 0;
};
bool decodeSound(const Bytes& b, DecodedSound& out) {
    out = {};
    if (isOgg(b)) {
        int channels = 0, rate = 0;
        short* samples = nullptr;
        const int n = stb_vorbis_decode_memory(b.data(), int(b.size()), &channels, &rate, &samples);
        if (n <= 0 || !samples) return false;
        out.spec = {SDL_AUDIO_S16, channels, rate};
        out.pcm.assign(reinterpret_cast<uint8_t*>(samples), reinterpret_cast<uint8_t*>(samples) + size_t(n) * size_t(channels) * 2);
        std::free(samples);
    } else {
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(b.data(), b.size()), true, &out.spec, &buf, &len)) return false;
        out.pcm.assign(buf, buf + len);
        SDL_free(buf);
    }
    const int frame = SDL_AUDIO_FRAMESIZE(out.spec);
    out.ms = frame && out.spec.freq ? uint32_t(uint64_t(out.pcm.size()) / uint64_t(frame) * 1000 / uint64_t(out.spec.freq)) : 0;
    return true;
}
// A sound decoded once and kept (by its path and its file's size, so a changed file decodes again); nullptr when it
// is not found or cannot be read. Decoding an OGG takes long: a briefing decodes its sounds before it plays.
const DecodedSound* decodedSound(const std::string& name) {
    static std::map<std::pair<std::string, size_t>, std::unique_ptr<DecodedSound>> cache;
    const Bytes* b = soundFileBytes(name);
    if (!b) return nullptr;
    auto [it, fresh] = cache.try_emplace({name, b->size()});
    if (fresh) {
        auto d = std::make_unique<DecodedSound>();
        if (decodeSound(*b, *d)) it->second = std::move(d);
    }
    return it->second.get();
}
// A sound's length (ms), by its path; 0 when it is not found.
uint32_t soundLength(const std::string& name) {
    const DecodedSound* d = decodedSound(name);
    return d ? d->ms : 0;
}
bool isSoundName(const std::string& n) {
    if (n.size() < 4) return false;
    std::string e = n.substr(n.size() - 4);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e == ".wav" || e == ".ogg";
}
// The map's sounds: its sound files, then the game sounds it names by reference.
std::vector<std::string> mapSounds() {
    std::vector<std::string> out;
    if (!app.ed) return out;
    for (const ArchiveFile& f : app.ed->archiveFiles())
        if (isSoundName(f.name)) out.push_back(f.name);
    for (const auto& r : app.ed->soundReferences()) out.push_back(r);
    return out;
}
// The game's sounds: every file under sound\ (their paths, as a map names them).
const std::vector<std::string>& gameSounds() {
    static std::vector<std::string> out;
    static bool listed = false;
    if (!listed) {
        listed = true;
        for (const auto& n : app.data.listFiles()) {
            if (n.size() < 7 || !isSoundName(n)) continue;
            std::string lower = n.substr(0, 6);
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "sound\\") out.push_back(n);
        }
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
    }
    return out;
}

// One sound to listen to (the Files pane, the sound pickers): play, and play again to stop.
struct PreviewVoice {
    std::string name;
    SDL_AudioStream* stream = nullptr;
    ULONGLONG started = 0;
    uint32_t ms = 0;
} gPreviewVoice;
void previewStop() {
    if (gPreviewVoice.stream) SDL_DestroyAudioStream(gPreviewVoice.stream);
    gPreviewVoice = {};
}
bool previewPlaying(const std::string& name) {
    return gPreviewVoice.stream && gPreviewVoice.name == name && GetTickCount64() - gPreviewVoice.started < gPreviewVoice.ms + 200;
}
void previewToggle(const std::string& name) {
    const bool same = previewPlaying(name);
    previewStop();
    if (same) return;
    const Bytes* b = soundFileBytes(name);
    DecodedSound d;
    if (!b || !decodeSound(*b, d)) return setMessage(L"Could not read the sound");
    if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
    gPreviewVoice.stream = openPlayback(&d.spec);
    if (!gPreviewVoice.stream) return setMessage(L"No sound device is available");
    SDL_PutAudioStreamData(gPreviewVoice.stream, d.pcm.data(), int(d.pcm.size()));
    SDL_FlushAudioStream(gPreviewVoice.stream);
    SDL_ResumeAudioStreamDevice(gPreviewVoice.stream);
    gPreviewVoice.name = name, gPreviewVoice.started = GetTickCount64(), gPreviewVoice.ms = d.ms;
}

// Imports sound files from disk into the map (staredit\wav\<file name>); returns their paths in the map.
std::vector<std::string> importSounds(HWND owner) {
    std::vector<std::string> added;
    if (!app.ed) return added;
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = owner, ofn.lpstrFilter = L"Sounds (WAV, OGG)\0*.wav;*.ogg\0", ofn.lpstrFile = buf.data(), ofn.nMaxFile = DWORD(buf.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return added;
    // One file: the full path. Several: the folder, then each name.
    std::vector<fs::path> files;
    const std::wstring first = buf.data();
    const wchar_t* p = buf.data() + first.size() + 1;
    if (!*p) files.push_back(first);
    for (; *p; p += wcslen(p) + 1) files.push_back(fs::path(first) / p);
    std::vector<ArchiveFile> read;
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        DecodedSound d;
        if (!decodeSound(data, d)) {
            setMessage(L"Could not read the sound: " + f.filename().wstring());
            continue;
        }
        const std::string name = "staredit\\wav\\" + narrow(f.filename().wstring());
        read.push_back({name, std::move(data)});
        added.push_back(name);
    }
    app.ed->setArchiveFiles(std::move(read));  // one undo step
    if (!added.empty()) setMessage(L"Imported " + std::to_wstring(added.size()) + L" sounds"), updateTitle();
    return added;
}

// Picks a sound for a briefing or trigger value: the map's, a new one from disk, or one of the game's.
bool pickSound(POINT at, const std::string& current, std::string& out) {
    std::vector<ListPickItem> items{{L"None", L"", ""}};
    for (const auto& w : mapSounds()) items.push_back({widen(w), app.ed->archiveFile(w) ? L"Map" : L"Game", w});
    items.push_back({L"Import sounds\x2026", L"", "\x01"});
    items.push_back({L"A sound of the game\x2026", L"", "\x02"});
    std::string picked;
    if (!showListPicker(L"Sound", items, current, at, picked)) return false;
    if (picked == "\x01") {
        const auto added = importSounds(app.main);
        if (added.empty()) return false;
        picked = added.front();
    } else if (picked == "\x02") {
        std::vector<ListPickItem> game;
        for (const auto& g : gameSounds()) game.push_back({widen(g), L"", g});
        if (!showListPicker(L"Game sound", game, current, at, picked)) return false;
    }
    out = picked;
    return true;
}
