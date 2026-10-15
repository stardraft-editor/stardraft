// Sounds for the map (included into main.cpp before briefing_view.inc): decoding and playing a sound (the audio is SDL's),
// the import from disk, and the sound pickers. The names of the map's and the game's sounds, and the bytes by name, are in
// core/sound_lists.inc; the Files page is core/views_files.inc, with the program's side in files_view.inc.

extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

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
    platform::FileDialog dlg;
    dlg.owner = owner;
    dlg.filters = {{"Sounds (WAV, OGG)", "*.wav;*.ogg"}};
    const std::vector<fs::path> files = platform::openFilesDialog(dlg);
    if (files.empty()) return added;
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
    items.push_back({L"Select a game sound\x2026", L"", "\x02"});
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
