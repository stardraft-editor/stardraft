// The open project's music tracks and dot images (portable, core.inc; the Assets workspace's Music and Dot images,
// stardraft/media.h): the lists, saved to media.json at each edit, and their undo steps. A step keeps the lists before
// and after it. A file that a step removes goes into the project's trash folder (.stardraft/trash), and Undo moves it
// back; a file that a step added goes there on Undo, and Redo moves it back. The trash is emptied when the project
// opens (the steps are of one session). In the Assets workspace, Undo takes the newest step: these lists' or the map's
// (the map's sounds), by the map's state when the list step was made (mediaUndoFirst).

std::wstring historyMessage(bool redo, size_t steps, const std::wstring& name);  // (core/undo_history.inc)
// The triggers' blocks that name a music track (a dot image) by its old name get the new one (core/triggers_model.inc).
void trigRenameMedia(bool track, const std::string& from, const std::string& to);

// A text translated, then `a` put in for its %s (a file's or an item's name).
std::wstring mediaText(const wchar_t* english, const std::wstring& a) {
    std::wstring t = tr(english);
    if (const size_t at = t.find(L"%s"); at != std::wstring::npos) t.replace(at, 2, a);
    return t;
}

struct MediaStep {
    std::wstring name;  // the step's name, as the status bar says it ("Undo: Add a music track")
    stardraft::ProjectMedia before, after;
    // The files that the step removed (path in the project -> where it is in the trash while the step is done), and the
    // files that it added (-> where each goes in the trash while the step is undone).
    std::map<std::string, std::filesystem::path> removed, added;
    // A rename: the blocks that name the item get its name back on Undo, and the new one again on Redo.
    std::string renamedFrom, renamedTo;
    bool renamedTrack = false;
    uint64_t mapState = 0;  // the map's stateId when the step was made (0: no map)
};

struct MediaModel {
    const EudProject* project = nullptr;  // the project the lists are of (null: none open)
    std::filesystem::path folder;
    stardraft::ProjectMedia media;
    std::string loadError;  // media.json could not be read: the lists are empty, and an edit does not save over it
    std::vector<MediaStep> undo, redo;
    int trashCount = 0;  // (the trash files' numbers)
};
MediaModel gMedia;

std::filesystem::path mediaTrash() { return gMedia.folder / ".stardraft" / "trash"; }

// The lists of the active project (loaded when the project changes; empty when no project is open).
MediaModel& mediaModel() {
    const EudProject* pr = activeProject();
    if (pr != gMedia.project || (pr && pr->folder() != gMedia.folder)) {
        gMedia = MediaModel{};
        gMedia.project = pr;
        if (pr) {
            gMedia.folder = pr->folder();
            if (!gMedia.media.load(stardraft::mediaFileOf(*pr), gMedia.loadError)) gMedia.media = {};
            std::error_code ec;
            std::filesystem::remove_all(mediaTrash(), ec);  // (the files of the last session's steps)
        }
    }
    return gMedia;
}
const stardraft::ProjectMedia& projectMedia() { return mediaModel().media; }

// A list's file in the project folder; empty when the path is not one that a list can have (media.h: mediaFileSafe).
std::filesystem::path mediaPath(const std::string& file) {
    if (!stardraft::mediaFileSafe(file, "music") && !stardraft::mediaFileSafe(file, "dots")) return {};
    return mediaModel().folder / std::filesystem::path(std::u8string(file.begin(), file.end()));
}
bool mediaReadFile(const std::filesystem::path& f, std::string& bytes) {
    if (f.empty()) return false;
    std::ifstream in(f, std::ios::binary);
    if (!in) return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}
bool mediaWriteFile(const std::filesystem::path& f, const std::string& bytes) {
    if (f.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(f.parent_path(), ec);
    std::ofstream out(f, std::ios::binary);
    out.write(bytes.data(), std::streamsize(bytes.size()));
    out.close();
    return bool(out);
}
// Moves a file (a rename; across drives a copy, then the remove). False when it is not moved.
bool mediaMoveFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    if (from.empty() || to.empty() || !std::filesystem::exists(from, ec)) return false;
    std::filesystem::create_directories(to.parent_path(), ec);
    std::filesystem::rename(from, to, ec);
    if (!ec) return true;
    ec.clear();
    if (!std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec)) return false;
    std::filesystem::remove(from, ec);
    return true;
}
// A new place in the trash for a list's file.
std::filesystem::path mediaTrashFor(const std::string& file) {
    const std::filesystem::path name = std::filesystem::path(std::u8string(file.begin(), file.end())).filename();
    return mediaTrash() / (std::to_wstring(++mediaModel().trashCount) + L"-" + name.wstring());
}

// The files that a list names.
std::set<std::string> mediaFiles(const stardraft::ProjectMedia& m) {
    std::set<std::string> s;
    for (const auto& t : m.tracks) s.insert(t.file);
    for (const auto& d : m.images) s.insert(d.file);
    return s;
}

// The lists can change now (a project is open and its media.json was read); else false with a message.
bool mediaCanEdit() {
    MediaModel& m = mediaModel();
    if (!m.project) return setMessage(L"No project is open"), false;
    if (!m.loadError.empty()) return setMessage(mediaText(L"Did not change the lists: media.json cannot be read (%s)", widen(m.loadError))), false;
    return true;
}

bool mediaSave(const stardraft::ProjectMedia& to) {
    MediaModel& m = mediaModel();
    std::string err;
    if (!m.project) return setMessage(L"No project is open"), false;
    if (!to.save(stardraft::mediaFileOf(*m.project), err)) return setMessage(mediaText(L"Could not save the lists: %s", widen(err))), false;
    m.media = to;
    return true;
}

// One edit of the lists as one undo step. `added`: the files that the edit put into the project (already written; the
// caller removes them when this refuses). The files that it removes go into the trash. A file that is not there is not
// kept (Undo then puts back only the list's entry).
bool mediaEdit(const wchar_t* stepName, const stardraft::ProjectMedia& next, const std::set<std::string>& added = {}) {
    MediaModel& m = mediaModel();
    if (!mediaCanEdit()) return false;
    MediaStep s;
    s.name = tr(stepName);
    s.before = m.media, s.after = next;
    const std::set<std::string> now = mediaFiles(next);
    for (const std::string& f : mediaFiles(m.media))
        if (!now.count(f)) {
            const std::filesystem::path t = mediaTrashFor(f);
            if (mediaMoveFile(mediaPath(f), t)) s.removed[f] = t;
        }
    for (const std::string& f : added) s.added[f] = mediaTrashFor(f);
    if (!mediaSave(s.after)) {
        for (const auto& [f, t] : s.removed) mediaMoveFile(t, mediaPath(f));  // (the files come back: nothing changed)
        return false;
    }
    const AppState& app = appState();
    s.mapState = app.ed && app.ed->isOpen() ? app.ed->stateId() : 0;
    m.undo.push_back(std::move(s));
    m.redo.clear();
    return true;
}

bool mediaCanUndo(bool redo) {
    const MediaModel& m = mediaModel();
    return !(redo ? m.redo : m.undo).empty();
}
// In the Assets workspace: the next Undo (Redo) is a step of these lists, not the map's. The map's step comes first when
// the map changed after the list step and has a step to undo.
bool mediaUndoFirst(bool redo) {
    if (!mediaCanUndo(redo)) return false;
    const MediaModel& m = mediaModel();
    const MediaStep& s = redo ? m.redo.back() : m.undo.back();
    const AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return true;
    if (!(redo ? app.ed->canRedo() : app.ed->canUndo())) return true;
    return app.ed->stateId() == s.mapState;
}
// Undoes (redoes) the newest list step, with its files and (a rename) the blocks' names; the status bar says its name.
bool mediaUndo(bool redo) {
    MediaModel& m = mediaModel();
    auto& from = redo ? m.redo : m.undo;
    if (from.empty()) return false;
    MediaStep s = std::move(from.back());
    from.pop_back();
    for (const auto& [f, t] : s.removed) redo ? mediaMoveFile(mediaPath(f), t) : mediaMoveFile(t, mediaPath(f));
    for (const auto& [f, t] : s.added) redo ? mediaMoveFile(t, mediaPath(f)) : mediaMoveFile(mediaPath(f), t);
    const bool ok = mediaSave(redo ? s.after : s.before);
    if (ok && !s.renamedFrom.empty()) trigRenameMedia(s.renamedTrack, redo ? s.renamedFrom : s.renamedTo, redo ? s.renamedTo : s.renamedFrom);
    const AppState& app = appState();
    s.mapState = app.ed && app.ed->isOpen() ? app.ed->stateId() : 0;
    const std::wstring name = s.name;
    (redo ? m.undo : m.redo).push_back(std::move(s));
    if (ok) setMessage(historyMessage(redo, 1, name));
    return ok;
}

// --- The edits of the Assets workspace ------------------------------------------------------------------------------

// A text with its number put in for %d (translated first).
std::wstring mediaCountText(const wchar_t* english, size_t n) {
    std::wstring t = tr(english);
    if (const size_t at = t.find(L"%d"); at != std::wstring::npos) t.replace(at, 2, std::to_wstring(n));
    return t;
}

// A free file name in the project's folder `dir` ("music"): the name, else "<stem> 2<ext>", ... (also free of the
// lists' files, which an undo step can put back).
std::string mediaFreeFile(const std::string& dir, const std::filesystem::path& from, const std::set<std::string>& taken) {
    const auto u8s = [](const std::filesystem::path& p) {
        const auto s = p.u8string();
        return std::string(s.begin(), s.end());
    };
    std::string stem = u8s(from.stem()), ext = u8s(from.extension());
    for (char& c : stem)  // (a name that the lists cannot hold: media.h, mediaFileSafe)
        if (uint8_t(c) < 0x20 || std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) c = '_';
    while (!stem.empty() && (stem.back() == '.' || stem.back() == ' ')) stem.pop_back();
    if (stem.empty() || stem == "." || stem == "..") stem = "file";
    std::string f = dir + "/" + stem + ext;
    std::error_code ec;
    for (int k = 2; taken.count(f) || std::filesystem::exists(mediaPath(f), ec); ++k) f = dir + "/" + stem + " " + std::to_string(k) + ext;
    return f;
}
std::string mediaStem(const std::filesystem::path& p) {
    const auto s = p.stem().u8string();
    return std::string(s.begin(), s.end());
}
std::string mediaExt(const std::filesystem::path& p) {
    const auto s = p.extension().u8string();
    return std::string(s.begin(), s.end());
}

// Sound files (OGG, WAV, MP3) as music tracks: copied into music/, as one undo step. Returns the files added.
std::vector<std::string> mediaAddTracks(const std::vector<std::filesystem::path>& files) {
    MediaModel& m = mediaModel();
    if (!m.project) return setMessage(L"Open an EUD project first: the music tracks go into the project"), std::vector<std::string>{};
    if (!mediaCanEdit()) return {};
    stardraft::ProjectMedia next = m.media;
    std::set<std::string> added;
    std::vector<std::string> out;
    std::set<std::string> taken = mediaFiles(next);
    for (const std::filesystem::path& f : files) {
        std::string bytes, why;
        uint32_t ms = 0;
        if (!mediaReadFile(f, bytes)) {
            setMessage(mediaText(L"Could not read %s", f.filename().wstring()));
            continue;
        }
        if (stardraft::soundKind(bytes, mediaExt(f)) == stardraft::SoundKind::Unknown) {
            setMessage(mediaText(L"Did not add %s: a music track is an OGG, WAV or MP3 file", f.filename().wstring()));
            continue;
        }
        if (!stardraft::soundLength(bytes, mediaExt(f), ms, why)) {
            setMessage(mediaText(L"Could not read the sound %s", f.filename().wstring()));
            continue;
        }
        bytes.clear(), bytes.shrink_to_fit();  // (the file is copied, not kept)
        const std::string file = mediaFreeFile("music", f, taken);
        std::error_code ec;
        std::filesystem::create_directories(mediaPath(file).parent_path(), ec);
        if (mediaPath(file).empty() || !std::filesystem::copy_file(f, mediaPath(file), ec)) {
            setMessage(mediaText(L"Could not write %s", widen(file)));
            continue;
        }
        taken.insert(file);
        next.tracks.push_back({next.freeTrackName(mediaStem(f)), file, ms});
        added.insert(file);
        out.push_back(file);
    }
    if (out.empty()) return out;
    if (!mediaEdit(out.size() == 1 ? L"Add a music track" : L"Add music tracks", next, added)) {
        std::error_code ec;
        for (const std::string& f : added) std::filesystem::remove(mediaPath(f), ec);  // (refused: the copies go)
        return {};
    }
    setMessage(out.size() == 1 ? mediaText(L"Added the music track %s", widen(next.tracks.back().name)) : mediaCountText(L"Added %d music tracks", out.size()));
    return out;
}

// Pictures as dot images: each with fewer than 1000 pixels (as EUD Editor 3), copied into dots/ as a PNG; one undo step.
std::vector<std::string> mediaAddImages(const std::vector<std::filesystem::path>& files) {
    MediaModel& m = mediaModel();
    if (!m.project) return setMessage(L"Open an EUD project first: the dot images go into the project"), std::vector<std::string>{};
    if (!mediaCanEdit()) return {};
    stardraft::ProjectMedia next = m.media;
    std::set<std::string> added;
    std::vector<std::string> out;
    std::set<std::string> taken = mediaFiles(next);
    for (const std::filesystem::path& f : files) {
        std::string bytes;
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
        if (!mediaReadFile(f, bytes) || !stardraft::decodePicture(bytes, w, h, rgba)) {
            setMessage(mediaText(L"Could not read the picture %s", f.filename().wstring()));
            continue;
        }
        if (int64_t(w) * h >= stardraft::kDotMaxPixels) {
            wchar_t t[240];
            std::swprintf(t, 240, tr(L"Did not add %ls: a dot image has fewer than 1000 pixels (this picture is %d x %d)"), f.filename().wstring().c_str(), w, h);
            setMessage(t);
            continue;
        }
        std::filesystem::path png = f;
        png.replace_extension(".png");
        const std::string file = mediaFreeFile("dots", png, taken);
        const bool isPng = bytes.size() > 8 && bytes.compare(1, 3, "PNG") == 0;
        if (mediaPath(file).empty() || !(isPng ? mediaWriteFile(mediaPath(file), bytes) : stardraft::writePngFile(mediaPath(file), w, h, rgba))) {
            setMessage(mediaText(L"Could not write %s", widen(file)));
            continue;
        }
        taken.insert(file);
        next.images.push_back({next.freeImageName(mediaStem(f)), file, w, h});
        added.insert(file);
        out.push_back(file);
    }
    if (out.empty()) return out;
    if (!mediaEdit(out.size() == 1 ? L"Add a dot image" : L"Add dot images", next, added)) {
        std::error_code ec;
        for (const std::string& f : added) std::filesystem::remove(mediaPath(f), ec);  // (refused: the files go)
        return {};
    }
    setMessage(out.size() == 1 ? mediaText(L"Added the dot image %s", widen(next.images.back().name)) : mediaCountText(L"Added %d dot images", out.size()));
    return out;
}

// A new name, as one undo step with the blocks that name the item (Undo gives them the old name back).
bool mediaRename(bool track, size_t i, const std::string& name) {
    MediaModel& m = mediaModel();
    stardraft::ProjectMedia next = m.media;
    const size_t n = track ? next.tracks.size() : next.images.size();
    if (i >= n) return false;
    std::string& cur = track ? next.tracks[i].name : next.images[i].name;
    if (name.empty() || name == cur) return false;
    auto lower = [](const std::string& a) {
        std::wstring x = widen(a);
        std::transform(x.begin(), x.end(), x.begin(), ::towlower);
        return x;
    };
    for (size_t k = 0; k < n; ++k)
        if (k != i && lower(track ? next.tracks[k].name : next.images[k].name) == lower(name))
            return setMessage(track ? L"A music track of that name is in the project already" : L"A dot image of that name is in the project already"), false;
    const std::string old = cur;
    cur = name;
    if (!mediaEdit(track ? L"Rename the music track" : L"Rename the dot image", next)) return false;
    m.undo.back().renamedFrom = old, m.undo.back().renamedTo = name, m.undo.back().renamedTrack = track;
    trigRenameMedia(track, old, name);
    setMessage(mediaText(track ? L"Renamed the music track to %s" : L"Renamed the dot image to %s", widen(name)));
    return true;
}

bool mediaRemove(bool track, size_t i) {
    MediaModel& m = mediaModel();
    stardraft::ProjectMedia next = m.media;
    if (track ? i >= next.tracks.size() : i >= next.images.size()) return false;
    const std::string name = track ? next.tracks[i].name : next.images[i].name;
    if (track) next.tracks.erase(next.tracks.begin() + std::ptrdiff_t(i));
    else next.images.erase(next.images.begin() + std::ptrdiff_t(i));
    if (!mediaEdit(track ? L"Remove the music track" : L"Remove the dot image", next)) return false;
    setMessage(mediaText(track ? L"Removed the music track %s" : L"Removed the dot image %s", widen(name)));
    return true;
}

// A dot image's colors, read from its file once (by the file's size): for the preview.
struct DotPreview {
    int width = 0, height = 0;
    std::vector<uint8_t> colors;
};
const DotPreview* mediaDotPreview(const std::string& file) {
    static std::map<std::pair<std::wstring, uintmax_t>, DotPreview> cache;
    std::error_code ec;
    const std::filesystem::path path = mediaPath(file);
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (path.empty() || ec) return nullptr;
    if (cache.size() > 64) cache.clear();  // (small: a preview is less than 1000 colors)
    auto [it, fresh] = cache.try_emplace({path.wstring(), size});
    if (fresh) {
        std::string bytes, why;
        if (!mediaReadFile(mediaPath(file), bytes) || !stardraft::dotPixels(bytes, it->second.width, it->second.height, it->second.colors, why)) it->second = {};
    }
    return it->second.colors.empty() ? nullptr : &it->second;
}
