// --script <file>[:<times>] in a snapshot (main_sdl.cpp): a long session's inputs from a file, <times> times over
// (default 1), in their order with the other inputs. Each line of the file is "<verb> <value>" ('#' starts a comment
// line). The verbs of the input options: key, type, click, drag, lostdrag, move, wheel, pinch, wait (their values are
// the options' values). And the verbs of a session:
//   command <name>     a menu command by name, as --command
//   mode <name>        a mode (units, terrain, doodads, sprites, locations), as --mode
//   rect [off]         Terrain mode's Rectangle on (as --rect), or off
//   workspace <name>   a workspace (map, data, triggers...), as --workspace
//   open <path>        a map or a project opens, as File > Open: a line with its time, and one with the first frame's
//   roundtrip <path>   the open map is saved there and compared with the map that opens from it, as --saveas
//   undocheck          all the undo steps (at most 40) are undone and redone: each redo gives the map as it was
//                      before the undo of that step, and the end is the map of the start (else a WRONG line)
//   minimap            the minimap's terrain is made again: a line with its time
//   stats              a line with the memory (the working set, the GPU's, the bitmaps with a texture), the undo
//                      steps and the frame times since the last stats line
// "{n}" in a value is the number of the time over (1, 2, ...): roundtrip soak{n}.scx makes a new file each time.

// The workspaces' names, in the order of the Workspace enum (--workspace, the script's workspace verb).
const char* const kWorkspaceNames[] = {"map",    "data",     "triggers",        "assets",  "project",
                                       "briefing", "files", "props", "players", "classictriggers", "classic"};
constexpr int kWorkspaceNameCount = int(sizeof kWorkspaceNames / sizeof kWorkspaceNames[0]);

bool sdlWorkspaceByName(const std::string& name) {
    for (int w = 0; w < kWorkspaceNameCount; ++w)
        if (name == kWorkspaceNames[w]) return setWorkspace(Workspace(w)), true;
    return false;
}

bool sdlModeByName(const std::string& name) {
    for (int m = 0; m < kModeCount; ++m) {
        std::string n = narrow(kModeNames[m]);
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        if (n == name) return setMode(Mode(m)), true;
    }
    return false;
}

// Adds the script's inputs to `inputs` (main_sdl.cpp's inputArgs). False (and a line on stderr) when the file cannot
// be read or a line has no such verb.
bool sdlReadScript(const std::string& spec, std::vector<std::pair<char, std::string>>& inputs) {
    std::string file = spec;
    int times = 1;
    if (const size_t colon = spec.rfind(':'); colon != std::string::npos && colon > 1 && colon + 1 < spec.size() &&
                                              std::all_of(spec.begin() + long(colon) + 1, spec.end(), [](unsigned char c) { return std::isdigit(c); }))
        file = spec.substr(0, colon), times = std::max(1, std::atoi(spec.c_str() + colon + 1));
    std::ifstream in{fs::path(widen(file))};
    if (!in) return std::fprintf(stderr, "--script %s: the file cannot be read\n", file.c_str()), false;
    static const std::pair<const char*, char> kVerbs[] = {
        {"key", 'k'},     {"type", 't'},    {"click", 'c'},     {"drag", 'd'},      {"lostdrag", 'l'},  {"move", 'm'},
        {"wheel", 'h'},   {"pinch", 'p'},   {"wait", 'w'},      {"command", 'C'},   {"mode", 'M'},      {"rect", 'Q'},
        {"workspace", 'W'}, {"open", 'O'},  {"roundtrip", 'R'}, {"undocheck", 'U'}, {"stats", 'S'}, {"minimap", 'P'}};
    std::vector<std::pair<char, std::string>> lines;
    int lineNo = 0;
    for (std::string line; std::getline(in, line);) {
        ++lineNo;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        line = line.substr(start);
        const size_t space = line.find(' ');
        const std::string verb = line.substr(0, space), value = space == std::string::npos ? "" : line.substr(space + 1);
        char kind = 0;
        for (const auto& [v, k] : kVerbs)
            if (verb == v) kind = k;
        if (!kind) return std::fprintf(stderr, "--script %s: line %d: no verb \"%s\"\n", file.c_str(), lineNo, verb.c_str()), false;
        lines.emplace_back(kind, value);
    }
    for (int t = 1; t <= times; ++t) {
        inputs.emplace_back('N', std::to_string(t));  // (the start of a time over: its number for the "{n}" of the values)
        for (auto [kind, value] : lines) {
            for (size_t at; (at = value.find("{n}")) != std::string::npos;) value.replace(at, 3, std::to_string(t));
            inputs.emplace_back(kind, value);
        }
    }
    return true;
}

// The process's memory now, in MB: the working set (resident), its peak, and the GPU memory the process uses (-1 when
// the system does not say).
struct SdlMemory {
    double workingSet = -1, peak = -1, privateBytes = -1, gpuLocal = -1, gpuShared = -1;
};

SdlMemory sdlMemory() {
    SdlMemory m;
    [[maybe_unused]] constexpr double kMb = 1024.0 * 1024.0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc))
        m.workingSet = double(pmc.WorkingSetSize) / kMb, m.peak = double(pmc.PeakWorkingSetSize) / kMb, m.privateBytes = double(pmc.PrivateUsage) / kMb;
    // The GPU memory of this process (DXGI: the video memory the system's scheduler counts for it), on every adapter.
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    static HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    static auto create = dxgi ? reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1"))) : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        double local = 0, shared = 0;
        bool any = false;
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            IDXGIAdapter3* a3 = nullptr;
            if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&a3)))) {
                DXGI_QUERY_VIDEO_MEMORY_INFO info{};
                if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) local += double(info.CurrentUsage) / kMb, any = true;
                if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info))) shared += double(info.CurrentUsage) / kMb;
                a3->Release();
            }
            adapter->Release();
        }
        factory->Release();
        if (any) m.gpuLocal = local, m.gpuShared = shared;
    }
#elif defined(__linux__)
    std::ifstream status("/proc/self/status");
    for (std::string line; std::getline(status, line);) {
        if (line.rfind("VmRSS:", 0) == 0) m.workingSet = std::atof(line.c_str() + 6) / 1024.0;
        else if (line.rfind("VmHWM:", 0) == 0) m.peak = std::atof(line.c_str() + 6) / 1024.0;
        // private: the memory of this process only (its anonymous pages, also the ones in swap)
        else if (line.rfind("RssAnon:", 0) == 0) m.privateBytes = (m.privateBytes < 0 ? 0 : m.privateBytes) + std::atof(line.c_str() + 8) / 1024.0;
        else if (line.rfind("VmSwap:", 0) == 0) m.privateBytes = (m.privateBytes < 0 ? 0 : m.privateBytes) + std::atof(line.c_str() + 7) / 1024.0;
    }
    // The GPU memory of this process: the kernel's DRM statistics of each open GPU device (/proc/self/fdinfo:
    // drm-total-<region> or drm-memory-<region>, in KiB): vram is the card's own memory, the other regions the shared.
    // None (a driver without them, as NVIDIA's or a GPU in software): -1.
    std::error_code ec;
    double local = 0, shared = 0;
    bool any = false;
    std::set<std::string> clients;  // (one device open twice counts once: its drm-client-id)
    for (const auto& e : fs::directory_iterator("/proc/self/fd", ec)) {
        const fs::path target = fs::read_symlink(e.path(), ec);
        if (ec || target.string().rfind("/dev/dri/", 0) != 0) continue;
        std::ifstream info(fs::path("/proc/self/fdinfo") / e.path().filename());
        std::string client;
        double l = 0, s = 0;
        bool has = false;
        for (std::string line; std::getline(info, line);) {
            if (line.rfind("drm-client-id:", 0) == 0) client = line.substr(14);
            const bool total = line.rfind("drm-total-", 0) == 0, memory = line.rfind("drm-memory-", 0) == 0;
            if (!total && !memory) continue;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string region = line.substr(total ? 10 : 11, colon - (total ? 10 : 11));
            double v = std::atof(line.c_str() + colon + 1);
            if (line.find("MiB", colon) != std::string::npos) v *= 1024.0;
            else if (line.find("KiB", colon) == std::string::npos) v /= 1024.0;  // (bytes)
            (region == "vram" || region == "local0" ? l : s) += v / 1024.0;
            has = true;
        }
        if (!has || !clients.insert(client.empty() ? e.path().filename().string() : client).second) continue;
        local += l, shared += s, any = true;
    }
    if (any) m.gpuLocal = local, m.gpuShared = shared;
#endif
    return m;
}

// The frames of the session (main_sdl.cpp times each frame after an input): since the last stats line.
struct SdlSoakFrames {
    int count = 0;
    double total = 0, max = 0;
    int steps = 0, loop = 0;
    bool firstFrame = false;  // a map opened: the next frame is its first
} gSoakFrames;

void sdlSoakFrame(double ms) {
    if (std::exchange(gSoakFrames.firstFrame, false))
        std::printf("first frame: %.1f ms (%d images still loading)\n", ms, gRenderer ? gRenderer->stats().imagesPending : 0);
    ++gSoakFrames.count, gSoakFrames.total += ms;
    gSoakFrames.max = std::max(gSoakFrames.max, ms);
}

uint64_t sdlMapHash(const Editor& ed) {
    const Bytes bytes = ed.chk().serialize();
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (const uint8_t b : bytes) h = (h ^ b) * 1099511628211ull;
    return h;
}

// undocheck: undo up to 40 steps and redo them all (Edit > Undo and Redo, as the keys run them).
void sdlUndoCheck() {
    if (!gState.ed || !gState.ed->isOpen()) return void(std::printf("undocheck: no map\n"));
    if (gWorkspace != Workspace::Map) setWorkspace(Workspace::Map);  // (Undo in another workspace is that workspace's)
    Editor& ed = *gState.ed;
    const size_t undoBefore = ed.undoSteps(), redoBefore = ed.redoSteps();
    std::vector<uint64_t> hashes{sdlMapHash(ed)};  // hashes[i]: the map after i undos
    while (hashes.size() <= 40 && ed.canUndo()) {
        command(ID_EDIT_UNDO);
        runPosted();
        hashes.push_back(sdlMapHash(ed));
    }
    const size_t undone = hashes.size() - 1;
    std::string problem;
    if (ed.undoSteps() != undoBefore - undone || ed.redoSteps() != redoBefore + undone)
        problem = "the undo steps after the undos are " + std::to_string(ed.undoSteps()) + " and " + std::to_string(ed.redoSteps());
    for (size_t j = 1; j <= undone && problem.empty(); ++j) {
        command(ID_EDIT_REDO);
        runPosted();
        if (sdlMapHash(ed) != hashes[undone - j]) problem = "redo " + std::to_string(j) + " of " + std::to_string(undone) + " gives another map";
    }
    if (problem.empty() && (ed.undoSteps() != undoBefore || ed.redoSteps() != redoBefore))
        problem = "the undo steps at the end are " + std::to_string(ed.undoSteps()) + " and " + std::to_string(ed.redoSteps()) + ", not " +
                  std::to_string(undoBefore) + " and " + std::to_string(redoBefore);
    std::printf("undocheck: %zu steps undone and redone, %zu undo steps: %s\n", undone, ed.undoSteps(),
                problem.empty() ? "the same map (yes)" : (problem + " (WRONG)").c_str());
}

void sdlSoakStats() {
    const SdlMemory m = sdlMemory();
    const double avg = gSoakFrames.count ? gSoakFrames.total / gSoakFrames.count : 0;
    std::string map = "no map";
    if (gState.ed && gState.ed->isOpen())
        map = std::to_string(gState.ed->width()) + "x" + std::to_string(gState.ed->height()) + ", " + std::to_string(gState.ed->units().size()) +
              " units, " + std::to_string(gState.ed->doodads().size()) + " doodads, undo " + std::to_string(gState.ed->undoSteps()) + ", redo " +
              std::to_string(gState.ed->redoSteps());
    int bitmaps = 0;
    size_t bitmapBytes = 0;
    kit_sdl::bitmapTextureStats(bitmaps, bitmapBytes);
    std::printf("stats: loop %d, step %d: working set %.1f MB (peak %.1f), private %.1f MB, GPU %.1f MB (shared %.1f), %d bitmaps %.1f MB; %zu tabs; %s; "
                "frames %d, %.1f ms on average, %.1f ms at most\n",
                gSoakFrames.loop, gSoakFrames.steps, m.workingSet, m.peak, m.privateBytes, m.gpuLocal, m.gpuShared, bitmaps,
                double(bitmapBytes) / (1024.0 * 1024.0), gDocs.size(), map.c_str(), gSoakFrames.count, avg, gSoakFrames.max);
    gSoakFrames.count = 0, gSoakFrames.total = 0, gSoakFrames.max = 0;
}

// A session verb of the script (sdlShotInputNext); false: not one of them.
bool sdlScriptStep(char kind, const std::string& value) {
    ++gSoakFrames.steps;
    switch (kind) {
    case 'N': gSoakFrames.loop = std::atoi(value.c_str()); return true;
    case 'C': sdlCommandShot(value); return true;
    case 'M':
        if (!sdlModeByName(value)) std::printf("mode %s: no such mode (WRONG)\n", value.c_str());
        sdlInputLine("mode " + value);
        return true;
    case 'Q': setMode(Mode::Terrain), setTerrainRect(value != "off"), sdlInputLine("rect " + value); return true;
    case 'W':
        if (!sdlWorkspaceByName(value)) std::printf("workspace %s: no such workspace (WRONG)\n", value.c_str());
        runPosted();
        sdlInputLine("workspace " + value);
        return true;
    case 'O': {
        const auto t0 = std::chrono::steady_clock::now();
        openAny(widen(value));
        runPosted();
        std::printf("open: %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        sdlInputLine("open " + value);
        gSoakFrames.firstFrame = true;
        return true;
    }
    case 'P': {
        if (!gState.ed || !gState.ed->isOpen()) return std::printf("minimap: no map\n"), true;
        const auto t0 = std::chrono::steady_clock::now();
        rebuildMinimapTerrain();
        std::printf("minimap: %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        return true;
    }
    case 'R': std::printf("%s\n", mapSaveRoundTrip(fs::path(widen(value))).c_str()); return true;
    case 'U': sdlUndoCheck(); return true;
    case 'S': sdlSoakStats(); return true;
    default: return false;  // (an input option's verb)
    }
}
