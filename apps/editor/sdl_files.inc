// The Files workspace on this system (the page is core/views_files.inc; its program parts are here): a sound is decoded
// as the Windows program decodes it (sound_files.inc) and played on the default device through SDL's audio, and the
// import takes files from the system's open dialog, which answers later (the sounds come in then). The briefings that use
// a sound are not on this system yet, so no sound has uses here. Included by main_sdl.cpp after sdl_menus.inc.

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

// One sound to listen to: play, and play again to stop.
struct SdlPreview {
    std::string name;
    SDL_AudioStream* stream = nullptr;
    uint64_t started = 0;
    uint32_t ms = 0;
} gSdlPreview;

void previewStopSdl() {
    if (gSdlPreview.stream) SDL_DestroyAudioStream(gSdlPreview.stream);
    gSdlPreview = {};
}
bool previewPlayingSdl(const std::string& name) {
    return gSdlPreview.stream && gSdlPreview.name == name && SDL_GetTicks() - gSdlPreview.started < gSdlPreview.ms + 200;
}
void previewToggleSdl(const std::string& name) {
    const bool same = previewPlayingSdl(name);
    previewStopSdl();
    if (same) return;
    const SdlSound* d = sdlSound(name);
    if (!d) return setMessage(L"Could not read the sound");
    if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &d->spec, nullptr, nullptr);
    if (!s) return setMessage(L"No sound device is available");
    SDL_SetAudioStreamGain(s, float(gDock.volume) / 100.0f);  // the volume of every sound StarDraft plays
    SDL_PutAudioStreamData(s, d->pcm.data(), int(d->pcm.size()));
    SDL_FlushAudioStream(s);
    SDL_ResumeAudioStreamDevice(s);
    gSdlPreview.stream = s, gSdlPreview.name = name, gSdlPreview.started = SDL_GetTicks(), gSdlPreview.ms = d->ms;
}

// Imports the sound files of the dialog into the map (staredit\wav\<file name>), as one undo step; a file that does not
// decode is named in the status bar. The first one that came in is selected.
void importSoundFilesSdl(const std::vector<fs::path>& files) {
    if (!gState.ed || !gState.ed->isOpen()) return setMessage(L"Open a map first: the sounds go into the map");
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
void soundFilesPicked(void*, const char* const* list, int) {
    std::vector<fs::path> files;
    for (int i = 0; list && list[i]; ++i) files.push_back(fs::path(list[i]));
    if (files.empty()) return;
    shellPost([files] { importSoundFilesSdl(files); });
}

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
        SDL_ShowOpenFileDialog(soundFilesPicked, nullptr, dialogWindow(), kSounds, 1, nullptr, true);
        return {};
    }
    bool askName(const std::wstring&, std::wstring&) override {  // (the rename box is not on this system yet)
        setMessage(L"Renaming a sound is not on this system yet");
        return false;
    }
    std::vector<FilesUse> soundUses(const std::string&) override { return {}; }  // (the briefings are not on this system yet)
    void showUse(const FilesUse&) override { setMessage(L"This is not on this system yet"); }
    void soundsChanged() override;
};

SdlFilesHost gFilesHost;
FilesPage gFilesPage(gFilesHost);  // (declared extern in core/views_files.inc)

void SdlFilesHost::soundsChanged() {
    gFilesPage.invalidate();
    shellStatusChanged();
}
