#include "stardraft/portraits.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "stardraft/game_data.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

namespace stardraft {

namespace {

// --- WebM (Matroska): just enough of it to find the video track and its frames ---------------------------------

struct Ebml {
    std::span<const uint8_t> d;
    size_t p = 0;
    bool ok = true;
    // A variable-length number; `keepMarker`: element IDs keep their length bits.
    uint64_t vint(bool keepMarker) {
        if (p >= d.size()) return ok = false, 0;
        const uint8_t first = d[p];
        int len = 1;
        while (len <= 8 && !(first & (0x80 >> (len - 1)))) ++len;
        if (len > 8 || p + size_t(len) > d.size()) return ok = false, 0;
        uint64_t v = keepMarker ? first : first & (0xFF >> len);
        for (int i = 1; i < len; ++i) v = v << 8 | d[p + size_t(i)];
        p += size_t(len);
        if (!keepMarker && v == (uint64_t(1) << (7 * len)) - 1) v = UINT64_MAX;  // unknown size
        return v;
    }
};
uint64_t readUint(std::span<const uint8_t> d) {
    uint64_t v = 0;
    for (uint8_t b : d) v = v << 8 | b;
    return v;
}

struct WebmTrack {
    uint64_t number = 0;
    std::string codec;
    int width = 0, height = 0;
    bool video = false;
};
struct WebmFrames {
    std::vector<WebmTrack> tracks;
    std::vector<std::pair<uint64_t, std::span<const uint8_t>>> blocks;  // track number, frame data
};

constexpr uint32_t kSegment = 0x18538067, kCluster = 0x1F43B675, kTracks = 0x1654AE6B, kTrackEntry = 0xAE, kTrackNumber = 0xD7,
                   kCodecId = 0x86, kVideo = 0xE0, kPixelWidth = 0xB0, kPixelHeight = 0xBA, kTrackType = 0x83, kSimpleBlock = 0xA3,
                   kBlockGroup = 0xA0, kBlock = 0xA1;

void parseLevel(std::span<const uint8_t> d, WebmFrames& out, WebmTrack* track) {
    Ebml e{d};
    while (e.ok && e.p < d.size()) {
        const uint64_t id = e.vint(true), size = e.vint(false);
        if (!e.ok) break;
        const size_t len = size == UINT64_MAX ? d.size() - e.p : size_t(std::min<uint64_t>(size, d.size() - e.p));
        const auto body = d.subspan(e.p, len);
        switch (id) {
        case kSegment:
        case kCluster:
        case kTracks:
        case kBlockGroup: parseLevel(body, out, track); break;
        case kTrackEntry:
            out.tracks.push_back({});
            parseLevel(body, out, &out.tracks.back());
            break;
        case kVideo:
            if (track) track->video = true, parseLevel(body, out, track);
            break;
        case kTrackNumber:
            if (track) track->number = readUint(body);
            break;
        case kTrackType:
            if (track && readUint(body) == 1) track->video = true;
            break;
        case kCodecId:
            if (track) track->codec.assign(body.begin(), body.end());
            break;
        case kPixelWidth:
            if (track) track->width = int(readUint(body));
            break;
        case kPixelHeight:
            if (track) track->height = int(readUint(body));
            break;
        case kSimpleBlock:
        case kBlock: {
            Ebml b{body};
            const uint64_t tn = b.vint(false);
            if (b.ok && b.p + 3 <= body.size() && !(body[b.p + 2] & 0x06)) out.blocks.push_back({tn, body.subspan(b.p + 3)});  // no lacing
            break;
        }
        }
        e.p += len;
    }
}

uint8_t clamp8(int v) { return uint8_t(std::clamp(v, 0, 255)); }

}  // namespace

bool decodeWebm(std::span<const uint8_t> file, PortraitVideo& out, std::string& error) {
    WebmFrames w;
    parseLevel(file, w, nullptr);
    const WebmTrack* video = nullptr;
    for (const auto& t : w.tracks)
        if (t.codec == "V_VP9" || t.codec == "V_VP8") video = &t;
    if (!video) return error = "no VP8 or VP9 video in the file", false;
    vpx_codec_ctx_t ctx;
    if (vpx_codec_dec_init(&ctx, video->codec == "V_VP9" ? vpx_codec_vp9_dx() : vpx_codec_vp8_dx(), nullptr, 0) != VPX_CODEC_OK)
        return error = "cannot start the video decoder", false;
    out = {};
    for (const auto& [track, data] : w.blocks) {
        if (track != video->number) continue;
        if (vpx_codec_decode(&ctx, data.data(), unsigned(data.size()), nullptr, 0) != VPX_CODEC_OK) {
            error = std::string("video: ") + vpx_codec_error(&ctx);
            continue;
        }
        vpx_codec_iter_t it = nullptr;
        while (const vpx_image_t* img = vpx_codec_get_frame(&ctx, &it)) {
            const int W = int(img->d_w), H = int(img->d_h);
            out.width = W, out.height = H;
            std::vector<uint8_t> rgba(size_t(W) * H * 4);
            const bool rgb = img->cs == VPX_CS_SRGB;  // 4:4:4 RGB: the planes are G, B, R
            const int xs = int(img->x_chroma_shift), ys = int(img->y_chroma_shift);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const int a = img->planes[0][y * img->stride[0] + x];
                    const int b = img->planes[1][(y >> ys) * img->stride[1] + (x >> xs)];
                    const int c = img->planes[2][(y >> ys) * img->stride[2] + (x >> xs)];
                    uint8_t* o = &rgba[(size_t(y) * W + x) * 4];
                    if (rgb) {
                        o[0] = uint8_t(c), o[1] = uint8_t(a), o[2] = uint8_t(b);
                    } else {  // BT.601, limited range
                        const int Y = (a - 16) * 298, U = b - 128, V = c - 128;
                        o[0] = clamp8((Y + 409 * V + 128) >> 8), o[1] = clamp8((Y - 100 * U - 208 * V + 128) >> 8), o[2] = clamp8((Y + 516 * U + 128) >> 8);
                    }
                    o[3] = 255;
                }
            out.frames.push_back(std::move(rgba));
        }
    }
    vpx_codec_destroy(&ctx);
    if (out.frames.empty()) return error = error.empty() ? "no frames" : error, false;
    static std::atomic<uint64_t> next{1};
    out.serial = next++;
    error.clear();
    return true;
}

bool readPortraitFile(const GameData& data, const std::string& path, int index, GraphicsQuality quality, bool carbot, Bytes& out,
                      std::string& error) {
    // portdata.tbl writes "tmarine\TMaFid0"; the files add one digit: tmafid00.webm, tmafid01.webm, ...
    const std::string name = "portrait\\" + path + std::to_string(index) + ".webm";
    std::vector<std::string> tries;
    if (carbot) tries.push_back("Carbot\\" + name);
    tries.push_back(quality == GraphicsQuality::SD ? "SD\\" + name : quality == GraphicsQuality::HD2 ? "HD2\\" + name : name);
    tries.push_back(name);
    tries.push_back(name.substr(0, name.size() - 5) + ".smk");  // (StarCraft 1.16.1, a mod's portraits)
    for (const auto& t : tries)
        if (data.readFile(t, out, error)) return true;
    return false;
}

bool loadPortrait(const GameData& data, const std::string& path, int index, GraphicsQuality quality, bool carbot, PortraitVideo& out,
                  std::string& error) {
    Bytes file;
    if (!readPortraitFile(data, path, index, quality, carbot, file, error)) return false;
    return file.size() >= 3 && std::memcmp(file.data(), "SMK", 3) == 0 ? decodeSmacker(file, out, error) : decodeWebm(file, out, error);
}

}  // namespace stardraft
