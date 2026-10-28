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
    bool askName(const std::wstring& title, std::wstring& text) override { return prompt(title.c_str(), text); }  // (sdl_map_dialogs.inc)
    std::vector<FilesUse> soundUses(const std::string& name) override { return soundUsesOf(name); }
    void showUse(const FilesUse& use) override { briefingShowUseSdl(use.trig, use.act); }
    void soundsChanged() override;
};

SdlFilesHost gFilesHost;
FilesPage gFilesPage(gFilesHost);  // (declared extern in core/views_files.inc)
SdlBarFrame gFilesFrame{gFilesPage};  // the page with its scroll bar (the workspace's view)

void SdlFilesHost::soundsChanged() {
    gFilesPage.invalidate();
    shellStatusChanged();
}
