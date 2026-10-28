// Smacker video (.smk, "SMK2" and "SMK4"): the portraits of StarCraft 1.16.1 and its mods. Only the video is decoded
// (no audio): each frame's palette and its 4 x 4 blocks, as the format describes them
// (https://wiki.multimedia.cx/index.php/Smacker).

#include <array>
#include <atomic>
#include <cstring>
#include <memory>

#include "byteio.h"
#include "stardraft/portraits.h"

namespace stardraft {

namespace {

// Bits from the lowest bit of each byte up (the format's order).
class BitReader {
public:
    BitReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    int bit() {
        if (pos_ >= n_ * 8) return over_ = true, 0;
        const int b = (p_[pos_ >> 3] >> (pos_ & 7)) & 1;
        ++pos_;
        return b;
    }
    uint32_t bits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v |= uint32_t(bit()) << i;
        return v;
    }
    bool over() const { return over_; }

private:
    const uint8_t* p_;
    size_t n_, pos_ = 0;
    bool over_ = false;
};

// An 8-bit Huffman tree: nodes as (child 0, child 1) pairs; a leaf is its value + 0x10000.
struct SmallTree {
    std::vector<std::array<int, 2>> nodes;
    int root = -1;  // -1: no tree (every value is 0)
    int build(BitReader& br, int depth) {
        if (depth > 32 || br.over()) return 0x10000;
        if (!br.bit()) return 0x10000 + int(br.bits(8));
        const int at = int(nodes.size());
        nodes.push_back({0, 0});
        const int a = build(br, depth + 1);
        const int b = build(br, depth + 1);
        nodes[size_t(at)] = {a, b};
        return at;
    }
    int decode(BitReader& br) const {
        if (root < 0) return 0;
        int n = root;
        while (n < 0x10000 && !br.over()) n = nodes[size_t(n)][br.bit()];
        return n >= 0x10000 ? n - 0x10000 : 0;
    }
};

// A 16-bit tree ("big tree"): its leaves read through two small trees; three leaves (the escapes) hold the last three
// values decoded, and are reset to 0 at each frame.
class BigTree {
public:
    bool read(BitReader& br) {
        values_.assign(1, 0);
        last_ = {0, 0, 0};
        if (!br.bit()) return true;  // no tree: every value is 0
        SmallTree lo, hi;
        for (SmallTree* t : {&lo, &hi})
            if (br.bit()) {
                t->root = t->build(br, 0);
                br.bit();  // (a small tree ends with a 0 bit)
            }
        for (int& e : escapes_) e = int(br.bits(16));
        values_.clear();
        last_ = {-1, -1, -1};
        build(br, lo, hi, 0);
        br.bit();
        for (int& l : last_)
            if (l < 0) l = int(values_.size()), values_.push_back(0);
        return !br.over();
    }
    void reset() {
        for (int l : last_) values_[size_t(l)] = 0;
    }
    int decode(BitReader& br) {
        size_t i = 0;
        while (i < values_.size() && (values_[i] & kNode)) {
            if (br.bit()) i += size_t(values_[i] & ~kNode);
            ++i;
            if (br.over()) return 0;
        }
        if (i >= values_.size()) return 0;
        const int v = values_[i];
        if (v != values_[size_t(last_[0])]) {
            values_[size_t(last_[2])] = values_[size_t(last_[1])];
            values_[size_t(last_[1])] = values_[size_t(last_[0])];
            values_[size_t(last_[0])] = v;
        }
        return v;
    }

private:
    static constexpr int kNode = 0x40000000;
    std::vector<int> values_;
    std::array<int, 3> escapes_{}, last_{};
    // Returns the number of entries the subtree takes.
    int build(BitReader& br, const SmallTree& lo, const SmallTree& hi, int depth) {
        if (depth > 32 || br.over()) return values_.push_back(0), 1;
        if (!br.bit()) {
            const int low = lo.decode(br);  // (the low byte's bits come first)
            int v = low | hi.decode(br) << 8;
            for (int k = 0; k < 3; ++k)
                if (v == escapes_[size_t(k)]) {  // (the last leaf with an escape value is that escape's)
                    last_[size_t(k)] = int(values_.size()), v = 0;
                    break;
                }
            values_.push_back(v);
            return 1;
        }
        const size_t at = values_.size();
        values_.push_back(kNode);
        const int left = build(br, lo, hi, depth + 1);
        values_[at] = kNode | left;
        const int right = build(br, lo, hi, depth + 1);
        return 1 + left + right;
    }
};

// The 6-bit palette values as 8-bit.
uint8_t palValue(int v) { return uint8_t((v * 255 + 31) / 63); }

}  // namespace

bool decodeSmacker(std::span<const uint8_t> file, PortraitVideo& out, std::string& error) {
    out = {};
    if (file.size() < 104 || std::memcmp(file.data(), "SMK", 3) != 0 || (file[3] != '2' && file[3] != '4'))
        return error = "not a Smacker video", false;
    const bool v4 = file[3] == '4';
    const uint32_t w = readU32(file, 4), h = readU32(file, 8), frames = readU32(file, 12);
    const int32_t rate = int32_t(readU32(file, 16));
    const uint32_t flags = readU32(file, 20), treeSize = readU32(file, 52);
    if (!w || !h || w > 4096 || h > 4096 || !frames || frames > 100000) return error = "bad Smacker header", false;
    if (uint64_t(w) * h * frames > (64u << 20)) return error = "Smacker video too large", false;  // (all frames as RGBA: at most 256 MB)
    out.fps = rate > 0 ? 1000.0 / rate : rate < 0 ? 100000.0 / -rate : 10.0;
    const size_t count = frames + ((flags & 1) ? 1 : 0);  // a ring frame after the others
    size_t at = 104;
    if (at + count * 5 + treeSize > file.size()) return error = "Smacker file is cut short", false;
    std::vector<uint32_t> sizes(count);
    for (size_t i = 0; i < count; ++i) sizes[i] = readU32(file, at + 4 * i) & ~3u;
    at += 4 * count;
    const uint8_t* types = file.data() + at;
    at += count;
    BigTree mmap, mclr, full, type;
    {
        BitReader br(file.data() + at, treeSize);
        if (!mmap.read(br) || !mclr.read(br) || !full.read(br) || !type.read(br)) return error = "bad Smacker trees", false;
    }
    at += treeSize;
    static constexpr int kRuns[64] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,  17,  18,  19,   20,   21,   22,
                                      23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,  39,  40,  41,   42,   43,   44,
                                      45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048};
    std::vector<uint8_t> pix(size_t(w) * h, 0);
    std::array<uint32_t, 256> pal{};
    out.width = int(w), out.height = int(h);
    const size_t bw = w / 4, blocks = bw * (h / 4);
    for (uint32_t f = 0; f < frames; ++f) {
        if (!sizes[f] || at + sizes[f] > file.size()) break;
        const uint8_t* p = file.data() + at;
        const uint8_t* end = p + sizes[f];
        at += sizes[f];
        if (types[f] & 1) {  // the palette: kept entries, copies from the last palette, new colors
            if (p >= end) break;
            const size_t len = size_t(*p) * 4;
            const uint8_t* q = p + 1;
            const uint8_t* pend = p + std::min<size_t>(len, size_t(end - p));
            const std::array<uint32_t, 256> old = pal;
            size_t n = 0;
            while (n < 256 && q < pend) {
                const uint8_t t = *q++;
                if (t & 0x80) n += (t & 0x7F) + 1;
                else if (t & 0x40) {
                    if (q >= pend) break;
                    size_t from = *q++;
                    for (int k = 0; k < (t & 0x3F) + 1 && n < 256 && from < 256; ++k) pal[n++] = old[from++];
                } else {
                    if (q + 2 > pend) break;
                    pal[n++] = uint32_t(palValue(t & 0x3F)) | uint32_t(palValue(q[0] & 0x3F)) << 8 | uint32_t(palValue(q[1] & 0x3F)) << 16;
                    q += 2;
                }
            }
            p += len;
        }
        for (int a = 1; a < 8; ++a)  // the audio tracks: skipped
            if ((types[f] >> a) & 1) {
                if (p + 4 > end) break;
                p += std::max<uint32_t>(4, readU32(std::span<const uint8_t>(p, 4), 0));
            }
        if (p < end) {
            BitReader br(p, size_t(end - p));
            mmap.reset(), mclr.reset(), full.reset(), type.reset();
            size_t blk = 0;
            while (blk < blocks && !br.over()) {
                const int t = type.decode(br);
                int run = kRuns[(t >> 2) & 0x3F];
                auto out4 = [&](size_t b) { return pix.data() + (b / bw) * w * 4 + (b % bw) * 4; };
                switch (t & 3) {
                case 0:  // two colors and a 16-bit map of them
                    while (run-- && blk < blocks) {
                        const int clr = mclr.decode(br);
                        int map = mmap.decode(br);
                        const uint8_t hi = uint8_t(clr >> 8), lo = uint8_t(clr);
                        uint8_t* o = out4(blk++);
                        for (int i = 0; i < 4; ++i, map >>= 4, o += w)
                            for (int k = 0; k < 4; ++k) o[k] = (map >> k) & 1 ? hi : lo;
                    }
                    break;
                case 1: {  // every pixel (Smacker 4: also each pair of pixels or each 2 x 2 doubled)
                    int mode = 0;
                    if (v4) mode = br.bit() ? 1 : br.bit() ? 2 : 0;
                    while (run-- && blk < blocks) {
                        uint8_t* o = out4(blk++);
                        if (mode == 0)
                            for (int i = 0; i < 4; ++i, o += w) {
                                int v = full.decode(br);
                                o[2] = uint8_t(v), o[3] = uint8_t(v >> 8);
                                v = full.decode(br);
                                o[0] = uint8_t(v), o[1] = uint8_t(v >> 8);
                            }
                        else if (mode == 1)
                            for (int i = 0; i < 2; ++i) {
                                const int v = full.decode(br);
                                for (int r = 0; r < 2; ++r, o += w) o[0] = o[1] = uint8_t(v), o[2] = o[3] = uint8_t(v >> 8);
                            }
                        else
                            for (int i = 0; i < 2; ++i) {
                                const int v2 = full.decode(br), v1 = full.decode(br);
                                for (int r = 0; r < 2; ++r, o += w)
                                    o[0] = uint8_t(v1), o[1] = uint8_t(v1 >> 8), o[2] = uint8_t(v2), o[3] = uint8_t(v2 >> 8);
                            }
                    }
                    break;
                }
                case 2:  // the block stays as it was
                    blk = std::min(blocks, blk + size_t(run));
                    break;
                default:  // one color
                    while (run-- && blk < blocks) {
                        uint8_t* o = out4(blk++);
                        for (int i = 0; i < 4; ++i, o += w) std::memset(o, t >> 8, 4);
                    }
                    break;
                }
            }
        }
        std::vector<uint8_t> rgba(size_t(w) * h * 4);
        for (size_t i = 0; i < pix.size(); ++i) {
            const uint32_t c = pal[pix[i]];
            rgba[i * 4] = uint8_t(c), rgba[i * 4 + 1] = uint8_t(c >> 8), rgba[i * 4 + 2] = uint8_t(c >> 16), rgba[i * 4 + 3] = 255;
        }
        out.frames.push_back(std::move(rgba));
    }
    if (out.frames.empty()) return error = "no frames", false;
    static std::atomic<uint64_t> next{1ull << 62};  // (WebM's serials count from 1)
    out.serial = next++;
    error.clear();
    return true;
}

}  // namespace stardraft
