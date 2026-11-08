// The Files workspace on this system (the page is core/views_files.inc; its program parts are here): a sound is decoded
// as the Windows program decodes it (sound_files.inc) and played on the default device through SDL's audio, and the
// import takes files from the system's open dialog, which answers later (the sounds come in then). The References panel
// lists the briefings that use a sound (core/views_briefing.inc: soundUsesOf), and a click on one shows it in the Briefing
// workspace (sdl_briefing.inc). Included by main_sdl.cpp after sdl_menus.inc.

extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

// A sound as the audio device takes it.
struct SdlSound {
    SDL_AudioSpec spec{};
    std::vector<uint8_t> pcm;
    uint32_t ms = 0;
};

bool decodeSdlSound(const Bytes& b, SdlSound& out) {
    out = {};
    if (b.size() >= 4 && std::memcmp(b.data(), "OggS", 4) == 0) {
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

// A sound decoded once and kept (by its name and its file's size); nullptr when it is not found or cannot be read.
const SdlSound* sdlSound(const std::string& name) {
    static std::map<std::pair<std::string, size_t>, std::unique_ptr<SdlSound>> cache;
    const Bytes* b = soundFileBytes(name);
    if (!b) return nullptr;
    auto [it, fresh] = cache.try_emplace({name, b->size()});
    if (fresh) {
        auto d = std::make_unique<SdlSound>();
        if (decodeSdlSound(*b, *d)) it->second = std::move(d);
    }
    return it->second.get();
}

// The SDL side's playback (the Windows program's openPlayback, main.cpp): a test run (gSilent) plays nothing, and the
// volume is the one of every sound StarDraft plays. Null: no sound plays (a test run, or no device).
// The sounds that the program was asked to play or stop (the Assets page's previews, the SFX data's sound) and the
// last one: a snapshot's state line says them (a test run plays nothing).
int gSoundAsks = 0;
std::string gSoundAsked;
// One playback device for every sound: opened at the first need and kept open and running while sounds play, so a
// sound starts at once (a device opened for each sound took a moment to start, and the sound began late). Each sound is
// a stream of its own on it (closeStreamSdl takes it off). The Briefing and Assets pages open it when they show and keep
// it open while they show (a Bluetooth headset's link then stays awake: its first sound starts at once too). Elsewhere,
// after 20 seconds with no sound the device closes (an open device keeps the computer from sleeping); the next sound
// opens it again. 0: none (a test run, or no device: a failed
// open is tried again after 2 seconds at the earliest, not at each of the briefing's ticks).
SDL_AudioDeviceID gPlaybackDevice = 0;
SDL_AudioSpec gPlaybackSpec{};
std::set<SDL_AudioStream*> gPlaybackStreams;  // (the streams of openPlaybackSdl not closed yet)
uint64_t gPlaybackFailedAt = 0, gPlaybackSoundAt = 0;
void playbackIdleCheckSdl() {
    if (!gHost) return;
    gHost->timer(5000, [] {
        if (!gPlaybackDevice) return;
        if (gWorkspace == Workspace::Briefing || gWorkspace == Workspace::Files) gPlaybackSoundAt = SDL_GetTicks();
        for (SDL_AudioStream* s : gPlaybackStreams)
            if (SDL_GetAudioStreamDevice(s) && SDL_GetAudioStreamQueued(s) > 0) gPlaybackSoundAt = SDL_GetTicks();
        if (SDL_GetTicks() - gPlaybackSoundAt < 20000) return playbackIdleCheckSdl();
        for (SDL_AudioStream* s : gPlaybackStreams) SDL_UnbindAudioStream(s);
        SDL_CloseAudioDevice(gPlaybackDevice);
        gPlaybackDevice = 0;
    });
}
SDL_AudioDeviceID playbackDeviceSdl() {
    if (gSilent) return 0;
    if (gPlaybackDevice) return gPlaybackSoundAt = SDL_GetTicks(), gPlaybackDevice;
    if (gPlaybackFailedAt && SDL_GetTicks() - gPlaybackFailedAt < 2000) return 0;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
    gPlaybackDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!gPlaybackDevice) return gPlaybackFailedAt = SDL_GetTicks(), 0;
    gPlaybackFailedAt = 0, gPlaybackSoundAt = SDL_GetTicks();
    if (!SDL_GetAudioDeviceFormat(gPlaybackDevice, &gPlaybackSpec, nullptr)) gPlaybackSpec = {SDL_AUDIO_F32, 2, 48000};
    playbackIdleCheckSdl();
    return gPlaybackDevice;
}
// Ends a sound of openPlaybackSdl (it stops if it plays).
void closeStreamSdl(SDL_AudioStream* s) {
    if (!s) return;
    gPlaybackStreams.erase(s);
    SDL_DestroyAudioStream(s);
}
SDL_AudioStream* openPlaybackSdl(const SDL_AudioSpec* spec) {
    if (!playbackDeviceSdl()) return nullptr;
    SDL_AudioStream* s = SDL_CreateAudioStream(spec, &gPlaybackSpec);
    if (!s) return nullptr;
    SDL_SetAudioStreamGain(s, float(gDock.volume) / 100.0f);
    gPlaybackStreams.insert(s);
    return s;
}
// Starts a stream of openPlaybackSdl with the data put in: it goes on the running device (the sound starts at once).
// False: no device (the sound does not play).
bool playStreamSdl(SDL_AudioStream* s) {
    return s && playbackDeviceSdl() && SDL_BindAudioStream(gPlaybackDevice, s);
}
// Pauses one sound (it leaves the device, its data stays) or plays it on: the other sounds go on. False: it could not
// play on (no device).
bool pauseStreamSdl(SDL_AudioStream* s, bool pause) {
    if (!s) return false;
    if (pause) return SDL_UnbindAudioStream(s), true;
    return playStreamSdl(s);
}

// One sound to listen to: play, and play again to stop.
struct SdlPreview {
    std::string name;
    SDL_AudioStream* stream = nullptr;
    uint64_t started = 0;
    uint32_t ms = 0;
} gSdlPreview;

void previewStopSdl() {
    closeStreamSdl(gSdlPreview.stream);
    gSdlPreview = {};
}
bool previewPlayingSdl(const std::string& name) {
    return gSdlPreview.stream && gSdlPreview.name == name && SDL_GetTicks() - gSdlPreview.started < gSdlPreview.ms + 200;
}
void previewToggleSdl(const std::string& name) {
    ++gSoundAsks, gSoundAsked = name;
    const bool same = previewPlayingSdl(name);
    previewStopSdl();
    if (same) return;
    const SdlSound* d = sdlSound(name);
    if (!d) return setMessage(L"Could not read the sound");
    SDL_AudioStream* s = openPlaybackSdl(&d->spec);
    if (!s) {
        if (!gSilent) setMessage(L"No sound device is available");
        return;
    }
    SDL_PutAudioStreamData(s, d->pcm.data(), int(d->pcm.size()));
    SDL_FlushAudioStream(s);
    playStreamSdl(s);
    gSdlPreview.stream = s, gSdlPreview.name = name, gSdlPreview.started = SDL_GetTicks(), gSdlPreview.ms = d->ms;
}

// A file of the project to listen to (a music track: core/media_model.inc), as previewToggleSdl: `key` is its name for
// previewPlayingSdl. An MP3 file is decoded by the library (stardraft/media.h), the others as the map's sounds.
void previewToggleFileSdl(const std::string& key, const fs::path& file) {
    ++gSoundAsks, gSoundAsked = key;
    const bool same = previewPlayingSdl(key);
    previewStopSdl();
    if (same) return;
    // The decoded sounds of the last 2 files (a decoded track is large: about 10 MB a minute).
    static std::map<std::pair<std::wstring, uintmax_t>, std::unique_ptr<SdlSound>> cache;
    static std::deque<std::pair<std::wstring, uintmax_t>> order;
    std::error_code ec;
    const uintmax_t size = fs::file_size(file, ec);
    if (ec) return setMessage(L"Could not read the sound");
    const std::pair<std::wstring, uintmax_t> fileKey{file.wstring(), size};
    auto [it, fresh] = cache.try_emplace(fileKey);
    if (fresh) {
        order.push_back(fileKey);
        while (order.size() > 2) cache.erase(order.front()), order.pop_front();
    }
    if (fresh) {
        std::ifstream in(file, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto d = std::make_unique<SdlSound>();
        stardraft::Pcm pcm;
        std::string why;
        const auto ext = file.extension().u8string();
        if (stardraft::soundKind(bytes, std::string(ext.begin(), ext.end())) == stardraft::SoundKind::Mp3) {
            if (stardraft::decodeSound(bytes, std::string(ext.begin(), ext.end()), pcm, why)) {
                d->spec = {SDL_AUDIO_S16, pcm.channels, int(pcm.rate)};
                d->pcm.assign(reinterpret_cast<const uint8_t*>(pcm.samples.data()), reinterpret_cast<const uint8_t*>(pcm.samples.data() + pcm.samples.size()));
                d->ms = uint32_t(pcm.frames() * 1000 / std::max<uint32_t>(pcm.rate, 1));
                it->second = std::move(d);
            }
        } else if (decodeSdlSound(Bytes(bytes.begin(), bytes.end()), *d)) {
            it->second = std::move(d);
        }
    }
    const SdlSound* d = it->second.get();
    if (!d) return setMessage(L"Could not read the sound");
    SDL_AudioStream* s = openPlaybackSdl(&d->spec);
    if (!s) {
        if (!gSilent) setMessage(L"No sound device is available");
        return;
    }
    SDL_PutAudioStreamData(s, d->pcm.data(), int(d->pcm.size()));
    SDL_FlushAudioStream(s);
    playStreamSdl(s);
    gSdlPreview.stream = s, gSdlPreview.name = key, gSdlPreview.started = SDL_GetTicks(), gSdlPreview.ms = d->ms;
}

// The files of the dialogs for the project's music tracks and dot images (SDL's thread): the UI thread adds them, when
// the same project is still open (core/media_model.inc).
void mediaFilesPicked(void* userdata, const char* const* list, int, bool tracks) {
    std::vector<fs::path> files;
    for (int i = 0; list && list[i]; ++i) files.push_back(fs::path(widen(list[i])));  // (UTF-8)
    if (files.empty()) return;
    const EudProject* target = static_cast<const EudProject*>(userdata);
    shellPost([files, target, tracks] {
        if (activeProject() != target) return setMessage(L"The project changed while you selected the files: nothing was added");
        const auto added = tracks ? mediaAddTracks(files) : mediaAddImages(files);
        if (!added.empty()) gFilesPage.setSelected((tracks ? "music:" : "dots:") + added.front());
    });
}
void musicFilesPicked(void* userdata, const char* const* list, int filter) { mediaFilesPicked(userdata, list, filter, true); }
void dotFilesPicked(void* userdata, const char* const* list, int filter) { mediaFilesPicked(userdata, list, filter, false); }

void gFilesHostChanged();  // (below: the other views that show the sounds)
// Imports the sound files of the dialog into the map (staredit\wav\<file name>), as one undo step; a file that does not
// decode is named in the status bar. The first one that came in is selected.
// The map the dialog was opened for (target): when another map is open now, nothing is imported into it.
void importSoundFilesSdl(const std::vector<fs::path>& files, const Editor* target) {
    if (!gState.ed || !gState.ed->isOpen()) return setMessage(L"Open a map first: the sounds go into the map");
    if (gState.ed.get() != target) return setMessage(L"The map changed while you selected the sounds: nothing was imported");
    std::vector<ArchiveFile> read;
    std::vector<std::string> added;
    for (const fs::path& f : files) {
        std::ifstream in(f, std::ios::binary);
        Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        SdlSound d;
        if (!decodeSdlSound(data, d)) {
            setMessage(L"Could not read the sound: " + f.filename().wstring());
            continue;
        }
        const std::string name = "staredit\\wav\\" + narrow(f.filename().wstring());
        read.push_back({name, std::move(data)});
        added.push_back(name);
    }
    if (read.empty()) return;
    gState.ed->setArchiveFiles(std::move(read));  // one undo step
    setMessage(L"Imported " + std::to_wstring(added.size()) + L" sounds");
    updateTitle();
    gFilesPage.setSelected(added.front());
}

// Any files into the map, as one undo step (All files): each by its name; a sound goes where the sounds are, and a file
// of that name in the map is replaced.
void addMapFilesSdl(const std::vector<fs::path>& files, const Editor* target) {
    if (!gState.ed || !gState.ed->isOpen()) return setMessage(L"Open a map first: the files go into the map");
    if (gState.ed.get() != target) return setMessage(L"The map changed while you selected the files: nothing was added");
    std::vector<ArchiveFile> read;
    int replaced = 0;
    for (const fs::path& f : files) {
        std::ifstream in(f, std::ios::binary);
        if (!in) {
            setMessage(L"Could not read the file: " + f.filename().wstring());
            continue;
        }
        Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::wstring ext = f.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        std::string name = (ext == L".wav" || ext == L".ogg" ? "staredit\\wav\\" : "") + narrow(f.filename().wstring());
        if (!archiveNameUsable(name)) {
            setMessage(L"Did not add " + f.filename().wstring() + L": the game uses that name");
            continue;
        }
        if (const ArchiveFile* same = gState.ed->archiveFile(name)) name = same->name, ++replaced;  // (the names of a map have no case)
        read.push_back({name, std::move(data)});
    }
    if (read.empty()) return;
    const std::string first = read.front().name;
    const size_t n = read.size();
    gState.ed->setArchiveFiles(std::move(read));
    setMessage(L"Added " + std::to_wstring(n) + (n == 1 ? L" file" : L" files") + (replaced ? L" (" + std::to_wstring(replaced) + L" replaced)" : L""));
    updateTitle();
    gFilesPage.setSelected(first);
    gFilesPage.invalidate();
    gFilesHostChanged();
}
void mapFilesPicked(void* userdata, const char* const* list, int) {
    std::vector<fs::path> files;
    for (int i = 0; list && list[i]; ++i) files.push_back(fs::path(widen(list[i])));
    if (files.empty()) return;
    const Editor* target = static_cast<const Editor*>(userdata);
    shellPost([files, target] { addMapFilesSdl(files, target); });
}
// Replace and Extract: the file of the map the dialog is open for.
std::string gMapFileAsked;
void replaceFilePicked(void* userdata, const char* const* list, int) {
    if (!list || !list[0]) return;
    const fs::path file(widen(list[0]));
    const Editor* target = static_cast<const Editor*>(userdata);
    shellPost([file, target] {
        if (!gState.ed || gState.ed.get() != target || !gState.ed->archiveFile(gMapFileAsked)) return setMessage(L"The map changed while you selected the file: nothing changed");
        std::ifstream in(file, std::ios::binary);
        if (!in) return setMessage(L"Could not read the file: " + file.filename().wstring());
        Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        gState.ed->setArchiveFiles({{gMapFileAsked, std::move(data)}});
        setMessage(L"Replaced the data of " + widen(gMapFileAsked));
        updateTitle();
        gFilesPage.invalidate();
        gFilesHostChanged();
    });
}
void extractFilePicked(void* userdata, const char* const* list, int) {
    if (!list || !list[0]) return;
    const fs::path file(widen(list[0]));
    const Editor* target = static_cast<const Editor*>(userdata);
    shellPost([file, target] {
        const ArchiveFile* f = gState.ed && gState.ed.get() == target ? gState.ed->archiveFile(gMapFileAsked) : nullptr;
        if (!f) return setMessage(L"The map changed while you selected the place: nothing was saved");
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(f->data.data()), std::streamsize(f->data.size()));
        setMessage(out ? L"Saved " + file.filename().wstring() : L"Could not save " + file.filename().wstring());
    });
}

// The open dialog's answer (SDL's thread): the UI thread imports the files.
void soundFilesPicked(void* userdata, const char* const* list, int) {
    std::vector<fs::path> files;
    for (int i = 0; list && list[i]; ++i) files.push_back(fs::path(widen(list[i])));  // (UTF-8)
    if (files.empty()) return;
    const Editor* target = static_cast<const Editor*>(userdata);
    shellPost([files, target] { importSoundFilesSdl(files, target); });
}

void briefingShowUseSdl(int trig, int act);  // (sdl_briefing.inc)

// The Files page's program parts (core/views_files.inc: FilesHost).
class SdlFilesHost final : public FilesHost {
public:
    uint32_t soundLength(const std::string& name) override {
        const SdlSound* d = sdlSound(name);
        return d ? d->ms : 0;
    }
    bool previewPlaying(const std::string& name) override { return previewPlayingSdl(name); }
    std::string previewedName() override { return gSdlPreview.name; }
    void togglePreview(const std::string& name) override { previewToggleSdl(name); }
    void previewStop() override { previewStopSdl(); }
    std::vector<std::string> importSounds() override {  // (the dialog answers later: no name is added now)
        static const SDL_DialogFileFilter kSounds[] = {{"Sounds (WAV, OGG)", "wav;ogg"}};
        sdlFileDialog(SDL_FILEDIALOG_OPENFILE, soundFilesPicked, gState.ed.get(), kSounds, 1, "", "", true);  // (the map it opens for)
        return {};
    }
    void addFiles() override { sdlFileDialog(SDL_FILEDIALOG_OPENFILE, mapFilesPicked, gState.ed.get(), nullptr, 0, "", "", true); }
    void extractFile(const std::string& name) override {
        gMapFileAsked = name;
        const size_t slash = name.find_last_of("\\/");
        const std::string file = slash == std::string::npos ? name : name.substr(slash + 1);
        sdlFileDialog(SDL_FILEDIALOG_SAVEFILE, extractFilePicked, gState.ed.get(), nullptr, 0, file.c_str(), "", false);
    }
    void replaceFile(const std::string& name) override {
        gMapFileAsked = name;
        sdlFileDialog(SDL_FILEDIALOG_OPENFILE, replaceFilePicked, gState.ed.get(), nullptr, 0, "", "", false);
    }
    bool askName(const std::wstring& title, std::wstring& text) override { return prompt(title.c_str(), text); }  // (sdl_map_dialogs.inc)
    std::vector<FilesUse> soundUses(const std::string& name) override { return soundUsesOf(name); }
    void showUse(const FilesUse& use) override { briefingShowUseSdl(use.trig, use.act); }
    void soundsChanged() override;
    void addMusic() override {
        static const SDL_DialogFileFilter kMusic[] = {{"Music (OGG, WAV, MP3)", "ogg;wav;mp3"}};
        sdlFileDialog(SDL_FILEDIALOG_OPENFILE, musicFilesPicked, activeProject(), kMusic, 1, "", "", true);
    }
    void addDotImages() override {
        static const SDL_DialogFileFilter kPictures[] = {{"Pictures (PNG)", "png"}, {"Other pictures (BMP, GIF, JPEG)", "bmp;gif;jpg;jpeg"}};
        sdlFileDialog(SDL_FILEDIALOG_OPENFILE, dotFilesPicked, activeProject(), kPictures, 2, "", "", true);
    }
    void toggleFilePreview(const std::string& key, const std::filesystem::path& file) override { previewToggleFileSdl(key, file); }
};

SdlFilesHost gFilesHost;
FilesPage gFilesPage(gFilesHost);  // (declared extern in core/views_files.inc)
SdlBarFrame gFilesFrame{gFilesPage};  // the page with its scroll bar (the workspace's view)

void SdlFilesHost::soundsChanged() {
    gFilesPage.invalidate();
    shellStatusChanged();
}
void gFilesHostChanged() { gFilesHost.soundsChanged(); }
