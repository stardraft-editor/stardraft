#pragma once
// EUD Editor 3's parts that its build makes and its project file holds the data for, made here so that File > Import a
// project needs no build folder (project_import.h):
//   - its Lua macros in the scripts (@KeyDown(...), <? ... ?>): expanded as its build expands them, with the MSQC and
//     chatEvent lines and the PluginVariables module they need (src/import_ee3_macros.cpp; the Lua runs in a sandbox: no
//     files, no OS, limits on time and memory, as the project file is not trusted);
//   - its dot images (TEData._DotDatas) and its BGM player (TEData._BGMData): their code, and the sound files the player
//     needs (src/import_ee3_media.cpp).
// What a function cannot make goes to `problems` (English, one line each, for the import report).

#include <string>
#include <utility>
#include <vector>

#include "stardraft/import_codegen.h"
#include "stardraft/media.h"
#include "stardraft/nrbf.h"

namespace stardraft {

// The macros of one project: each script's text in turn, then what they need together. `te` is the project's
// TriggerEditorData (its BGM list, chat event addresses, MSQC settings), `ctx` the map's names.
class Ee3Macros {
public:
    Ee3Macros(const nrbf::Graph& g, const nrbf::Object* te, const ImportCodeContext& ctx);
    ~Ee3Macros();
    Ee3Macros(const Ee3Macros&) = delete;
    Ee3Macros& operator=(const Ee3Macros&) = delete;
    // A script's text with its macros expanded (MacroManager.MacroApply); `isMain`: the project's main file. False when a
    // macro could not run (`out` holds the text with that macro left as written).
    bool apply(const std::string& text, bool isMain, const std::string& fileName, std::string& out, std::vector<std::string>& problems);
    // After all the scripts: the .eds lines of the [MSQC] and [chatEvent] sections, and PluginVariables.py's text.
    std::vector<std::string> msqcLines() const;
    std::vector<std::string> chatEventLines() const;
    std::string pluginVariables() const;

private:
    struct State;
    State* s_;
};

// A file that an import writes into the project: its path in the project ("TriggerEditor/dotData.eps") and its bytes.
struct ImportFile {
    std::string path;
    std::string bytes;
    int width = 0, height = 0;  // > 0: `bytes` are RGBA pixels, which the import writes as a PNG file
    std::string from;           // not empty: the file to copy (UTF-8 path; `bytes` is empty), read when the import writes
};

// EUD Editor 3's dot images: dotData.eps (WriteDotData), empty when the project has none.
bool ee3DotImages(const nrbf::Graph& g, const nrbf::Object* te, std::vector<ImportFile>& files, std::vector<std::string>& problems);
// EUD Editor 3's BGM player: BGMFlexible.eps (WriteBGMFile) and its sound files, empty when the project has none.
// `projectFolder`: the folder of the .e3s (the BGM files' relative paths start there).
bool ee3BgmPlayer(const nrbf::Graph& g, const nrbf::Object* te, const std::string& projectFolder, std::vector<ImportFile>& files,
                  std::vector<std::string>& problems);

// EUD Editor 3's BGM list and dot images as the project's music tracks and dot images (media.h), for the Assets
// workspace: `media` gets the lists; `files` the sound files to copy (music/<name>.<ext>, as they are: their paths, in
// `from`) and the pictures
// (dots/<name>.png, cut to the image's size; RGBA). False when a sound or a picture cannot come over (a problem line
// each); the others are in the lists.
bool ee3Media(const nrbf::Graph& g, const nrbf::Object* te, const std::string& projectFolder, ProjectMedia& media, std::vector<ImportFile>& files,
              std::vector<std::string>& problems);

}  // namespace stardraft
