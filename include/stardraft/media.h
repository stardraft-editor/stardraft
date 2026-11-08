#pragma once

// The project's music tracks and dot images (the Assets workspace's Music and Dot images lists). The lists are in
// media.json beside project.stardraft; each track's sound file is in the project's folder music/, each picture in
// dots/. Build makes the code that the trigger blocks use from them (writeMediaCode):
//
//   generated/BGMFlexible.eps   EUD Editor 3's BGM data (BuildData.GetBGMEps): the parts' names and the number of the
//                               last part of each track, PlayOGG and loadSound, which puts the parts into the map
//   generated/music/<n>/st<k>.ogg   the parts of track n: Ogg Vorbis, 2.28 seconds each (EUD Editor 3's cut). An OGG
//                               file is cut packet by packet; a WAV or MP3 file is encoded to Ogg Vorbis first (libvorbis)
//   generated/sd_mpqadd.py      puts a file into the map one time (two copies of the BGM data can name the same part)
//   generated/sdmusic.eps       the player of Play music track, Stop, Pause and Resume (each player hears its own)
//   generated/dotData.eps       EUD Editor 3's dot data (BuildData.GetDotEps): each picture in its 21 dot colors
//   generated/sddots.eps        Draw dot image: EUD Editor 3's dotTool.eps, and the block's function
//
// The trigger code (generated/triggers.eps) imports sdmusic and sddots when a block uses them. An imported EUD Editor 3
// project whose scripts call EUD Editor 3's BGMPlayer or dotTool by name keeps its module names: `ee3Folder` (its
// scripts' folder, "TriggerEditor") gets BGMFlexible.eps, dotData.eps and sd_mpqadd.py too, made from the same lists.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace stardraft {

class EudProject;

struct MusicTrack {
    std::string name;  // as the list and the block pickers show it
    std::string file;  // in the project folder, with "/" ("music/Theme.ogg")
    uint32_t ms = 0;   // the length (milliseconds)
    bool operator==(const MusicTrack&) const = default;
};

struct DotImage {
    std::string name;
    std::string file;  // "dots/Logo.png"
    int width = 0, height = 0;
    bool operator==(const DotImage&) const = default;
};

struct ProjectMedia {
    std::vector<MusicTrack> tracks;
    std::vector<DotImage> images;
    std::string ee3Folder;  // "": none; else the folder that also gets EUD Editor 3's module names (see above)
    bool empty() const { return tracks.empty() && images.empty(); }
    bool operator==(const ProjectMedia&) const = default;
    // No file: empty lists, and true. False with `error` when the file cannot be read.
    bool load(const std::filesystem::path& file, std::string& error);
    // Empty lists and no ee3Folder: the file goes.
    bool save(const std::filesystem::path& file, std::string& error) const;
    // A name that no track (no image) has: `base`, else "base 2", "base 3", ...
    std::string freeTrackName(const std::string& base) const;
    std::string freeImageName(const std::string& base) const;
};

std::filesystem::path mediaFileOf(const EudProject& pr);  // <project>/media.json
// A list's file as media.json may name it: a path relative to the project folder, in the folder `dir` ("music" or
// "dots"), with "/" between its parts, no part "." or "..", no root, drive or "\\". The file is not trusted: load
// refuses others, and Build and the editor check again where they use the path.
bool mediaFileSafe(const std::string& file, const char* dir);
// ee3Folder: one plain folder name in the project folder ("TriggerEditor"): no "/", "\\", ":", "." or "..".
bool mediaFolderSafe(const std::string& folder);

// --- Dot images -------------------------------------------------------------------------------------------------------

// EUD Editor 3's dot colors (DotPaint.ColorHex, 0xRRGGBB): the 21 colors that dotTool.eps can draw, in its order.
constexpr int kDotColorCount = 21;
extern const uint32_t kDotColors[kDotColorCount];
// The nearest dot color (DotPaint.GetColorIndex: the squared RGB distance, the first of equal ones).
int dotColorIndex(int r, int g, int b);
// EUD Editor 3 refuses a picture of 1000 pixels or more (width x height).
constexpr int kDotMaxPixels = 1000;
// A PNG, BMP, GIF or JPEG picture as RGBA. False when it cannot be read.
bool decodePicture(const std::string& bytes, int& width, int& height, std::vector<uint8_t>& rgba);
// The dot colors of a picture: one color index for each pixel, row by row. False with `why` (in English) when the
// picture cannot be read or has kDotMaxPixels or more.
bool dotPixels(const std::string& bytes, int& width, int& height, std::vector<uint8_t>& colors, std::string& why);
// Writes RGBA as a PNG file.
bool writePngFile(const std::filesystem::path& file, int width, int height, const std::vector<uint8_t>& rgba);

// --- Sounds ------------------------------------------------------------------------------------------------------------

// Sound as 16-bit samples, the channels of a frame one after the other.
struct Pcm {
    int channels = 0;
    uint32_t rate = 0;
    std::vector<int16_t> samples;
    uint64_t frames() const { return channels ? samples.size() / size_t(channels) : 0; }
};
enum class SoundKind { Unknown, Ogg, Wav, Mp3 };
// By its first bytes, else by its extension (".mp3": an MP3 file can start with its first frame).
SoundKind soundKind(const std::string& bytes, const std::string& extension);
// Decodes a WAV or MP3 file (dr_wav, dr_mp3). An OGG file needs no decoding for Build: false.
bool decodeSound(const std::string& bytes, const std::string& extension, Pcm& out, std::string& why);
// The length of an OGG (its last page's position), WAV or MP3 file.
bool soundLength(const std::string& bytes, const std::string& extension, uint32_t& ms, std::string& why);
// Encodes sound as an Ogg Vorbis file (libvorbis, variable bit rate; quality 0 to 1). Three channels or more become
// two. The same sound gives the same bytes.
bool encodeOggVorbis(const Pcm& pcm, std::string& ogg, std::string& why, float quality = 0.4f);
// An Ogg Vorbis file cut into EUD Editor 3's parts of 2.28 seconds (ffmpeg's segment muxer with "-c copy": the packets
// are copied, not encoded again). Each part is an Ogg Vorbis file with the stream's headers.
struct OggParts {
    std::vector<std::string> parts;
    uint32_t rate = 0;
    int channels = 0;
    int64_t samples = 0;        // the stream's length (its last page's position, when it has one)
    bool otherStreams = false;  // the file holds more streams than the first (they are left out)
};
bool cutOggVorbis(const std::string& ogg, OggParts& out, std::string& why);
// The Ogg Vorbis file of a track: an OGG file as it is, a WAV or MP3 file encoded.
bool trackOggVorbis(const std::string& bytes, const std::string& extension, std::string& ogg, std::string& why);
// BuildData.GetSoundIndex: a number as 4 characters, '@' + each hex digit ("@@@E" for 5); `value`: those characters as
// the dword that holds them ("0x45404040").
std::string ee3SoundIndex(int index, bool value);

// --- Build -------------------------------------------------------------------------------------------------------------

struct MediaBuildReport {
    int parts = 0;    // the sound parts of all tracks
    int encoded = 0;  // the tracks encoded on this build (WAV, MP3; an unchanged source keeps its parts)
};
// Writes (or removes, when the lists are empty) the generated files above. False with `error` (in English, with the
// track's or the picture's name) when a file cannot be read, decoded or written.
bool writeMediaCode(const EudProject& pr, const ProjectMedia& media, MediaBuildReport& report, std::string& error);
// The texts of the generated modules (for the tests).
std::string bgmModuleText(const std::vector<int>& lastPart, const std::vector<std::vector<std::string>>& partFiles, bool relativeHelper);
std::string dotModuleText(const std::vector<std::vector<uint8_t>>& colors, const std::vector<DotImage>& images);
const char* musicPlayerText();
const char* dotDrawText();
const char* mpqAddText();

}  // namespace stardraft
