// The project's music tracks and dot images (stardraft/media.h).
//
// usage: media_test <dir> [<install> <map.scx>]
//
// Without game data: the lists saved and loaded; a short WAV (written here) decoded, encoded to Ogg Vorbis (the same
// bytes each time) and cut into EUD Editor 3's parts of 2.28 seconds; the dot colors of a PNG (written here) and the
// limit of 1000 pixels; the generated modules of a project (BGMFlexible.eps, the parts, sdmusic.eps, dotData.eps,
// sddots.eps, the copies for EUD Editor 3's module names), kept while the source is the same; the blocks' code (Play music
// track and the others, Draw dot image) and their checks. With an install and a Brood War map, euddraft builds a project
// with a WAV track, an OGG track, a dot image and the blocks (all the test's own data).

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/eud_project.h"
#include "stardraft/game_data.h"
#include "stardraft/media.h"
#include "stardraft/msqc.h"
#include "stardraft/project_build.h"
#include "stardraft/triggers.h"

using namespace stardraft;
namespace fs = std::filesystem;

namespace {

int failed = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  ok  " : "FAILED", what.c_str());
    if (!ok) ++failed;
}

std::string readText(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
void writeBytes(const fs::path& f, const std::string& b) {
    fs::create_directories(f.parent_path());
    std::ofstream(f, std::ios::binary) << b;
}
bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// A WAV file (16-bit PCM) of a tone: `seconds` long, `channels` channels.
std::string wav(double seconds, uint32_t rate, int channels) {
    const uint32_t frames = uint32_t(seconds * rate);
    std::string data;
    for (uint32_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) {
            const int16_t s = int16_t(8000 * std::sin(2 * 3.14159265 * (440.0 + 110 * c) * i / rate));
            data += char(s & 0xFF), data += char((s >> 8) & 0xFF);
        }
    auto u32 = [](uint32_t v) { return std::string{char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; };
    auto u16 = [](uint16_t v) { return std::string{char(v), char(v >> 8)}; };
    std::string f = "RIFF" + u32(uint32_t(36 + data.size())) + "WAVEfmt " + u32(16) + u16(1) + u16(uint16_t(channels)) + u32(rate) +
                    u32(rate * uint32_t(channels) * 2) + u16(uint16_t(channels * 2)) + u16(16) + "data" + u32(uint32_t(data.size())) + data;
    return f;
}

TriggerCall make(const char* id) { return TriggerCall::make(*TriggerDef::find(id)); }

// A project folder with no map (a .eds that names one): enough for the generated files.
bool emptyProject(const fs::path& dir, EudProject& pr) {
    fs::create_directories(dir);
    writeBytes(dir / "media.eds", "[main]\r\ninput: media.scx\r\noutput: media_EUD.scx\r\n");
    std::string err;
    return pr.open(dir / "media.eds", err);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: media_test <dir> [<install> <map.scx>]\n");
        return 2;
    }
    const fs::path dir = argv[1];
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string err;

    // 1. The lists: saved and loaded; empty lists remove the file.
    {
        ProjectMedia m;
        m.tracks.push_back({"Boss \"theme\"", "music/boss.ogg", 95000});
        m.tracks.push_back({"Calm", "music/calm.wav", 1234});
        m.images.push_back({"Logo", "dots/logo.png", 20, 10});
        m.ee3Folder = "TriggerEditor";
        const fs::path f = dir / "lists" / "media.json";
        ProjectMedia back;
        check(m.save(f, err) && back.load(f, err) && back == m, "the lists are saved and loaded the same (names, files, lengths, sizes, the EUD Editor 3 folder)");
        check(m.freeTrackName("calm") == "calm 2" && m.freeImageName("Title") == "Title", "a new name is free of the list's names");
        ProjectMedia none;
        check(none.save(f, err) && !fs::exists(f) && back.load(f, err) && back.empty(), "empty lists remove media.json, and no file loads as empty lists");
        writeBytes(f, "{\"version\": 9}");
        check(!back.load(f, err) && contains(err, "newer"), "a file of a newer version is refused");
        // Paths that leave the folders music and dots are refused (the file is not trusted).
        int refused = 0;
        const char* const badTracks[] = {"../x.ogg", "music/../../x.ogg", "C:/x.ogg", "/x.ogg", "music\\..\\x.ogg", "music/a:b.ogg", "dots/x.ogg", "music/", "music//x.ogg"};
        for (const char* p : badTracks) {
            std::string q;  // (JSON: a backslash written as two)
            for (const char* c = p; *c; ++c) q += *c == '\\' ? std::string("\\\\") : std::string(1, *c);
            writeBytes(f, std::string("{\"tracks\": [{\"name\": \"x\", \"file\": \"") + q + "\"}]}");
            refused += !back.load(f, err) && back.empty() && contains(err, "not valid");
        }
        writeBytes(f, "{\"images\": [{\"name\": \"x\", \"file\": \"music/x.png\"}]}");
        refused += !back.load(f, err);
        for (const char* folder : {"..", "../x", "a/b", "C:", "."}) {
            writeBytes(f, std::string("{\"ee3Folder\": \"") + folder + "\"}");
            refused += !back.load(f, err);
        }
        check(refused == 9 + 1 + 5, "media.json with a path outside music and dots, or a folder that is not a plain name, is refused (" + std::to_string(refused) + " of 15)");
        check(mediaFileSafe("music/\xed\x85\x8c\xeb\xa7\x88.wav", "music") && mediaFileSafe("dots/a b/c.png", "dots") && mediaFolderSafe("TriggerEditor"),
              "Korean names and subfolders are kept");
    }

    // 2. Sounds: a WAV decoded, encoded and cut.
    std::string ogg1, partsOgg;
    {
        const std::string w = wav(1.0, 22050, 1);
        Pcm pcm;
        uint32_t ms = 0;
        check(soundKind(w, ".wav") == SoundKind::Wav && decodeSound(w, ".wav", pcm, err) && pcm.channels == 1 && pcm.rate == 22050 && pcm.frames() == 22050,
              "a WAV file is decoded (dr_wav)");
        bool ok = soundLength(w, ".wav", ms, err) && ms == 1000;
        check(ok, "a WAV file's length: " + std::to_string(ms) + " ms");
        std::string again;
        ok = encodeOggVorbis(pcm, ogg1, err) && ogg1.compare(0, 4, "OggS") == 0;
        check(ok, "the sound is encoded as Ogg Vorbis (libvorbis): " + std::to_string(ogg1.size()) + " bytes");
        check(encodeOggVorbis(pcm, again, err) && again == ogg1, "the same sound gives the same bytes");
        ok = soundKind(ogg1, ".ogg") == SoundKind::Ogg && soundLength(ogg1, ".ogg", ms, err) && ms == 1000;
        check(ok, "the Ogg Vorbis file's length: " + std::to_string(ms) + " ms");
        OggParts cut;
        check(cutOggVorbis(ogg1, cut, err) && cut.parts.size() == 1 && cut.rate == 22050 && cut.channels == 1, "1 second is one part");
        const std::string w5 = wav(5.0, 44100, 2);
        std::string ogg5;
        ok = trackOggVorbis(w5, ".wav", ogg5, err) && cutOggVorbis(ogg5, cut, err) && cut.parts.size() == 3 && cut.channels == 2;
        check(ok, "5 seconds of stereo are 3 parts of 2.28 seconds (" + std::to_string(cut.parts.size()) + ")");
        bool partsOk = true;
        int64_t total = 0;
        for (const std::string& p : cut.parts) {
            uint32_t pm = 0;
            partsOk &= p.compare(0, 4, "OggS") == 0 && soundLength(p, ".ogg", pm, err);
            total += pm;
        }
        check(partsOk && total >= 4900 && total <= 5100, "each part is an Ogg Vorbis file; together " + std::to_string(total) + " ms");
        partsOgg = ogg5;
        std::string why;
        check(!decodeSound("hello", ".mp3", pcm, why) && soundKind("hello", ".mp3") == SoundKind::Unknown, "a file that is not a sound is refused");
        Pcm six;
        six.channels = 6, six.rate = 48000, six.samples.assign(6 * 4800, 1000);
        std::string ogg6;
        OggParts c6;
        check(encodeOggVorbis(six, ogg6, err) && cutOggVorbis(ogg6, c6, err) && c6.channels == 2, "six channels are encoded as two");
        check(ee3SoundIndex(5, false) == "@@@E" && ee3SoundIndex(0x1A, true) == "0x4A414040", "EUD Editor 3's sound names");
        {  // a last page with a huge position: the length is capped, with no overflow
            std::string big = ogg1;
            const size_t last = big.rfind("OggS");
            for (int k = 0; k < 8; ++k) big[last + 6 + size_t(k)] = char(k == 7 ? 0x40 : 0);
            uint32_t bms = 0;
            ok = soundLength(big, ".ogg", bms, err) && bms == 0xFFFFFFFFu;
            check(ok, "a length that does not fit is capped (" + std::to_string(bms) + ")");
        }
    }

    // 3. Dot images: the colors of a picture, and the limit.
    {
        std::vector<uint8_t> rgba;
        const uint32_t px[6] = {0xf70400, 0xffffff, 0x3a57cc, 0x202020, 0x249824, 0x7f7f7f};
        for (uint32_t c : px) rgba.insert(rgba.end(), {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c), 255});
        const fs::path f = dir / "dots" / "small.png";
        check(writePngFile(f, 3, 2, rgba), "a PNG is written");
        int w = 0, h = 0;
        std::vector<uint8_t> colors;
        check(dotPixels(readText(f), w, h, colors, err) && w == 3 && h == 2 && colors == std::vector<uint8_t>{0, 11, 1, 20, 2, 18},
              "the picture's dot colors are the nearest of the 21 (EUD Editor 3's DotPaint)");
        std::vector<uint8_t> big(40 * 25 * 4, 255);
        std::string why;
        const bool refused = writePngFile(dir / "dots" / "big.png", 40, 25, big) && !dotPixels(readText(dir / "dots" / "big.png"), w, h, colors, why) && contains(why, "1000");
        check(refused, "a picture of 1000 pixels is refused (" + why + ")");
    }

    // 4. The generated files of a project.
    {
        EudProject pr;
        check(emptyProject(dir / "gen", pr), "a project folder");
        writeBytes(pr.folder() / "music" / "calm.wav", wav(3.0, 22050, 1));
        writeBytes(pr.folder() / "music" / "boss.ogg", partsOgg);
        fs::create_directories(pr.folder() / "dots");
        fs::copy_file(dir / "dots" / "small.png", pr.folder() / "dots" / "small.png", fs::copy_options::overwrite_existing, ec);
        ProjectMedia m;
        m.tracks.push_back({"Calm", "music/calm.wav", 3000});
        m.tracks.push_back({"Boss", "music/boss.ogg", 5000});
        m.images.push_back({"Small", "dots/small.png", 3, 2});
        MediaBuildReport rep;
        const fs::path gen = pr.folder() / "generated";
        err.clear();
        const bool wrote = writeMediaCode(pr, m, rep, err) && rep.encoded == 1 && rep.parts == 2 + 3;
        check(wrote, "Build writes the parts: " + std::to_string(rep.parts) + ", 1 track encoded" + (err.empty() ? "" : " (" + err + ")"));
        const std::string bgm = readText(gen / "BGMFlexible.eps");
        check(contains(bgm, "import .sd_mpqadd as sdmpq;") && contains(bgm, "const bgmlen = [0x41404040, 0x42404040];") &&
                  contains(bgm, "sdmpq.add('@@@@@@@@', 'generated/music/0/st0.ogg');") && contains(bgm, "sdmpq.add('@@@A@@@B', 'generated/music/1/st2.ogg');"),
              "BGMFlexible.eps: the last part of each track and the parts' files");
        check(fs::exists(gen / "music" / "1" / "st2.ogg") && readText(gen / "music" / "0" / "st0.ogg").compare(0, 4, "OggS") == 0, "the parts are Ogg Vorbis files");
        check(readText(gen / "sdmusic.eps") == musicPlayerText() && readText(gen / "sddots.eps") == dotDrawText() && readText(gen / "sd_mpqadd.py") == mpqAddText(),
              "the player, the drawing and the helper");
        const std::string dots = readText(gen / "dotData.eps");
        check(contains(dots, "EUDArray(list(1, 3, 2,0,11,1,20,2,18))  // Small") && contains(dots, "function GetData(id)"), "dotData.eps: each picture's size and colors");
        check(contains(mpqAddText(), "builtins") && !contains(mpqAddText(), "duplicate"), "the MPQ helper keeps the added names on builtins (both copies see them)");
        check(writeMediaCode(pr, m, rep, err) && rep.encoded == 0, "a second build keeps the parts of a source that did not change");
        m.ee3Folder = "TriggerEditor";
        check(writeMediaCode(pr, m, rep, err) && contains(readText(pr.folder() / "TriggerEditor" / "BGMFlexible.eps"), "import sd_mpqadd as sdmpq;") &&
                  readText(pr.folder() / "TriggerEditor" / "dotData.eps") == dots && fs::exists(pr.folder() / "TriggerEditor" / "sd_mpqadd.py"),
              "EUD Editor 3's module names: BGMFlexible.eps and dotData.eps in its folder too");
        m.tracks.pop_back();
        check(writeMediaCode(pr, m, rep, err) && !fs::exists(gen / "music" / "1") && contains(readText(gen / "BGMFlexible.eps"), "const bgmlen = [0x41404040];"),
              "a removed track's parts go");
        ProjectMedia gone;
        gone.ee3Folder = "TriggerEditor";
        check(writeMediaCode(pr, gone, rep, err) && !fs::exists(gen / "BGMFlexible.eps") && !fs::exists(gen / "music") && !fs::exists(gen / "dotData.eps") &&
                  !fs::exists(gen / "sdmusic.eps") && !fs::exists(pr.folder() / "TriggerEditor" / "BGMFlexible.eps"),
              "empty lists remove the generated files");
        {  // Korean file names (UTF-8 in the lists)
            const std::string ko = "music/\xed\x85\x8c\xeb\xa7\x88.wav";  // (music/테마.wav)
            writeBytes(pr.folder() / fs::path(std::u8string(ko.begin(), ko.end())), wav(1.0, 22050, 1));
            ProjectMedia k;
            k.tracks.push_back({"\xed\x85\x8c\xeb\xa7\x88", ko, 1000});
            ProjectMedia back;
            const bool okKo = writeMediaCode(pr, k, rep, err) && rep.parts == 1 && k.save(dir / "ko" / "media.json", err) &&
                              back.load(dir / "ko" / "media.json", err) && back == k;
            check(okKo, "a track with a Korean file name builds, and is saved and loaded (" + err + ")");
        }
        {  // a path that leaves the project is refused by Build too (its files are not touched)
            ProjectMedia evil;
            evil.tracks.push_back({"x", "../outside.wav", 0});
            err.clear();
            const bool refusedPath = !writeMediaCode(pr, evil, rep, err) && contains(err, "not in the folder music");
            evil.tracks.clear(), evil.ee3Folder = "../..";
            const bool refusedFolder = !writeMediaCode(pr, evil, rep, err);
            check(refusedPath && refusedFolder, "Build refuses a file or a folder outside the project's folders");
        }
        m.tracks[0].file = "music/none.wav";
        err.clear();
        const bool stopped = !writeMediaCode(pr, m, rep, err) && contains(err, "Calm");
        check(stopped, "a missing file stops Build and names the track (" + err + ")");
    }

    // 5. The blocks: their code and checks.
    {
        TriggerMapInfo map;
        map.locations.resize(64);
        map.locations[63] = "Anywhere";
        map.unitNames = std::vector<std::string>(228);
        map.unitNames[0] = "Terran Marine";
        map.musicTracks = {"Calm", "Boss"};
        map.dotImages = {"Small"};
        TriggerSet set;
        TriggerNode t = TriggerSet::newTrigger("Music");
        TriggerCall play = make("act.trackplay");
        play.args[0] = "Boss";
        TriggerCall stop = make("act.trackstop"), pause = make("act.trackpause"), resume = make("act.trackresume");
        stop.args[0] = "P2";
        TriggerCall draw = make("act.dotdraw");
        draw.args[0] = "Small";
        t.actions = {play, stop, pause, resume, draw};
        set.roots[1].children.push_back(t);
        std::vector<TriggerIssue> issues;
        const std::string code = set.generate(map, issues);
        int errors = 0;
        for (const auto& i : issues)
            if (i.level == TriggerIssue::Level::Error) ++errors, std::printf("      error: %s\n", i.message.c_str());
        check(errors == 0, "no errors");
        check(contains(code, "import .sdmusic as sd_music;") && contains(code, "import .sddots as sd_dots;"), "the trigger code imports the player and the drawing");
        check(contains(code, "sd_music.sd_musicPlay(1, 0x41404040, 0xFF);") && contains(code, "sd_music.sd_musicStop((1 << getcurpl()));") &&
                  contains(code, "sd_music.sd_musicPause(0xFF, 0);") && contains(code, "sd_music.sd_musicPause(0xFF, 1);"),
              "Play music track: the track's place and sound name, and the players");
        check(contains(code, "sd_music.sd_musicLoad();") && contains(code, "sd_music.sd_musicCycle();"), "the parts load at the start, and the player runs each trigger cycle");
        check(contains(code, "sd_dots.sd_dotDraw(0, "), "Draw dot image: the picture's place");
        // Run local block: the music blocks are local, Draw dot image is not.
        TriggerSet loc;
        TriggerNode lt = TriggerSet::newTrigger("Local");
        TriggerCall local = make("act.local");
        local.lists[0] = {play, draw};
        lt.actions = {local};
        loc.roots[1].children.push_back(lt);
        issues.clear();
        loc.generate(map, issues);
        int localErrors = 0;
        bool drawRefused = false;
        for (const auto& i : issues)
            if (i.level == TriggerIssue::Level::Error) ++localErrors, drawRefused |= contains(i.message, "Draw dot image");
        check(localErrors == 1 && drawRefused, "in Run local block, Play music track is allowed and Draw dot image is refused (it makes units)");
        TriggerSet bad = set;
        bad.roots[1].children[0].actions[0].args[0] = "Gone";
        bad.roots[1].children[0].actions[4].args[0] = "";
        issues.clear();
        bad.generate(map, issues);
        bool noTrack = false, noImage = false;
        for (const auto& i : issues) noTrack |= contains(i.message, "no music track \"Gone\""), noImage |= contains(i.message, "Select a dot image");
        check(noTrack && noImage, "a track that the project does not have, and no dot image, are errors");
        TriggerMapInfo none = map;
        none.musicTracks.clear();
        TriggerSet stopOnly;
        TriggerNode st = TriggerSet::newTrigger("Stop");
        st.actions = {stop, pause, resume};
        stopOnly.roots[1].children.push_back(st);
        issues.clear();
        stopOnly.generate(none, issues);
        int noTracks = 0;
        for (const auto& i : issues) noTracks += i.level == TriggerIssue::Level::Error && contains(i.message, "has no music tracks");
        check(noTracks == 3, "Stop, Pause and Resume in a project with no music tracks are errors (" + std::to_string(noTracks) + ")");
    }

    // 6. With the game: euddraft builds a project with the tracks, the image and the blocks.
    if (argc >= 4) {
        GameData data;
        if (!data.open(argv[2], err)) {
            check(false, "the game data opens (" + err + ")");
        } else {
            Editor ed(data);
            EudProject pr;
            const fs::path bdir = dir / "build";
            fs::remove_all(bdir / "media", ec);
            check(ed.open(argv[3], err) && EudProject::create(bdir, "media", argv[3], pr, err), "a project with the map");
            writeBytes(pr.folder() / "music" / "calm.wav", wav(3.0, 22050, 1));
            writeBytes(pr.folder() / "music" / "boss.ogg", partsOgg);
            fs::create_directories(pr.folder() / "dots");
            fs::copy_file(dir / "dots" / "small.png", pr.folder() / "dots" / "small.png", fs::copy_options::overwrite_existing, ec);
            ProjectMedia m;
            m.tracks.push_back({"Calm", "music/calm.wav", 3000});
            m.tracks.push_back({"Boss", "music/boss.ogg", 5000});
            m.images.push_back({"Small", "dots/small.png", 3, 2});
            check(m.save(mediaFileOf(pr), err), "media.json");
            TriggerSet set;
            TriggerNode start = TriggerSet::newTrigger("Start");
            TriggerCall play = make("act.trackplay");
            play.args[0] = "Calm";
            TriggerCall draw = make("act.dotdraw");
            draw.args[0] = "Small";
            start.actions = {play, draw};
            set.roots[1].children.push_back(start);
            TriggerNode later = TriggerSet::newTrigger("Later");
            later.actions = {make("act.trackpause"), make("act.trackresume"), make("act.trackstop")};
            set.roots[2].children.push_back(later);
            check(set.save(triggersFileOf(pr), err), "triggers.json");
            BuildReport report;
            err.clear();
            const bool prepared = prepareBuild(pr, ed, data, BuildOpen{}, report, err);
            for (const std::string& e : report.triggerErrors) std::printf("      %s\n", e.c_str());
            check(prepared, "Build's steps before euddraft (" + report.failedStep + ": " + err + ")");
            check(report.media.parts == 2 + 3 && report.media.encoded == 1, "the parts of both tracks");
            std::string log;
            const auto r = compileBuild(pr, [&](const std::string& line) { log += line + "\n"; });
            if (!r.ok) std::printf("%s\n", log.c_str());
            check(r.ok, "euddraft builds the map with the music tracks, the dot image and the blocks");
#ifdef STARDRAFT_SOURCE_DIR
            // An imported EUD Editor 3 project whose script calls its BGMPlayer and dotTool by name (media.h: ee3Folder):
            // its library's files, unchanged, find BGMFlexible and dotData in the scripts' folder; with the blocks too,
            // both copies of the BGM data put the same parts into the map, one time.
            const fs::path lib = fs::path(STARDRAFT_SOURCE_DIR) / "third_party" / "eud-editor-3" / "TriggerEditor";
            for (const char* f : {"BGMPlayer.eps", "dotTool.eps"}) {
                fs::create_directories(pr.folder() / "TriggerEditor" / "TriggerEditor");
                fs::copy_file(lib / f, pr.folder() / "TriggerEditor" / "TriggerEditor" / f, fs::copy_options::overwrite_existing, ec);
            }
            writeBytes(pr.folder() / "TriggerEditor" / "main.eps",
                       "import TriggerEditor.BGMPlayer as bp;\nimport TriggerEditor.dotTool as dt;\n\nfunction onPluginStart() {\n    bp.loadSound();\n"
                       "    bp.SetBGM(1);\n    dt.drawStart(0, 0, 64);\n    dt.draw(0, 8);\n}\n\nfunction beforeTriggerExec() {\n    bp.Play();\n}\n");
            pr.addScript("TriggerEditor/main.eps");
            m.ee3Folder = "TriggerEditor";
            check(m.save(mediaFileOf(pr), err) && pr.save(err), "the project with EUD Editor 3's script");
            err.clear();
            const bool again = prepareBuild(pr, ed, data, BuildOpen{}, report, err);
            check(again && fs::exists(pr.folder() / "TriggerEditor" / "BGMFlexible.eps") && fs::exists(pr.folder() / "TriggerEditor" / "dotData.eps"),
                  "Build writes EUD Editor 3's module names into its folder");
            log.clear();
            const auto r2 = compileBuild(pr, [&](const std::string& line) { log += line + "\n"; });
            if (!r2.ok) std::printf("%s\n", log.c_str());
            check(r2.ok, "euddraft builds the map with EUD Editor 3's BGMPlayer and dotTool and with the blocks");
#endif
        }
    }

    std::printf(failed ? "%d FAILED\n" : "all passed\n", failed);
    return failed ? 1 : 0;
}
