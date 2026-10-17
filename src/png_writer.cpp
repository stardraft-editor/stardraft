#include "stardraft/png_writer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace stardraft {

namespace {

uint32_t crcTable[256];
bool crcReady = false;

uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
    if (!crcReady) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crcTable[i] = c;
        }
        crcReady = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)), v.push_back(uint8_t(x >> 16)), v.push_back(uint8_t(x >> 8)), v.push_back(uint8_t(x));
}

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

}  // namespace

PngWriter::~PngWriter() {
    if (f_) {
        std::fclose(f_);
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
}

bool PngWriter::chunk(const char* type, const uint8_t* data, size_t n) {
    std::vector<uint8_t> head;
    put32(head, uint32_t(n));
    head.insert(head.end(), type, type + 4);
    uint32_t crc = crc32(0, head.data() + 4, 4);
    crc = crc32(crc, data, n);
    std::vector<uint8_t> tail;
    put32(tail, crc);
    ok_ = ok_ && std::fwrite(head.data(), 1, head.size(), f_) == head.size() && (n == 0 || std::fwrite(data, 1, n, f_) == n) &&
          std::fwrite(tail.data(), 1, 4, f_) == 4;
    return ok_;
}

bool PngWriter::open(const std::filesystem::path& path, int width, int height) {
    if (f_ || width <= 0 || height <= 0) return false;
#ifdef _WIN32
    if (_wfopen_s(&f_, path.wstring().c_str(), L"wb") != 0) f_ = nullptr;
#else
    f_ = std::fopen(path.string().c_str(), "wb");
#endif
    if (!f_) return false;
    path_ = path, w_ = width, h_ = height, rows_ = 0, ok_ = true;
    prev_.assign(size_t(width) * 4, 0);
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    ok_ = std::fwrite(sig, 1, 8, f_) == 8;
    std::vector<uint8_t> ihdr;
    put32(ihdr, uint32_t(width)), put32(ihdr, uint32_t(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8 bits, RGBA, deflate, adaptive filters, not interlaced
    chunk("IHDR", ihdr.data(), ihdr.size());
    out_.push_back(0x78), out_.push_back(0x01);  // the zlib header (deflate, 32 KB window)
    return ok_;
}

void PngWriter::putBits(uint32_t bits, int count) {
    bitBuf_ |= uint64_t(bits) << bitCount_;
    bitCount_ += count;
    while (bitCount_ >= 8) out_.push_back(uint8_t(bitBuf_)), bitBuf_ >>= 8, bitCount_ -= 8;
}

void PngWriter::putCode(uint32_t code, int len) {
    uint32_t r = 0;
    for (int i = 0; i < len; ++i) r |= ((code >> i) & 1) << (len - 1 - i);
    putBits(r, len);
}

void PngWriter::literal(int v) {
    if (v < 144) putCode(0x30 + uint32_t(v), 8);
    else if (v < 256) putCode(0x190 + uint32_t(v - 144), 9);
    else if (v < 280) putCode(uint32_t(v - 256), 7);
    else putCode(0xC0 + uint32_t(v - 280), 8);
}

void PngWriter::match(int len, int dist) {
    int i = 28;
    while (kLenBase[i] > len) --i;
    literal(257 + i);
    if (kLenExtra[i]) putBits(uint32_t(len - kLenBase[i]), kLenExtra[i]);
    int d = 29;
    while (kDistBase[d] > dist) --d;
    putCode(uint32_t(d), 5);
    if (kDistExtra[d]) putBits(uint32_t(dist - kDistBase[d]), kDistExtra[d]);
}

// One block with the fixed codes for `data` (matches only inside it), then an empty stored block: the stream is at a
// whole byte again, and the next band starts its own block.
void PngWriter::compress(const uint8_t* data, size_t n) {
    constexpr int kHashBits = 15, kWindow = 32768, kChain = 16;
    std::vector<int32_t> head(size_t(1) << kHashBits, -1), prev(kWindow, -1);
    auto hash = [&](size_t i) { return (uint32_t(data[i]) << 10 ^ uint32_t(data[i + 1]) << 5 ^ data[i + 2]) & ((1u << kHashBits) - 1); };
    auto insert = [&](size_t i) {
        if (i + 2 >= n) return;
        const uint32_t h = hash(i);
        prev[i & (kWindow - 1)] = head[h];
        head[h] = int32_t(i);
    };
    putBits(0, 1), putBits(1, 2);  // not the last block; the fixed codes
    size_t i = 0;
    while (i < n) {
        int best = 0, bestDist = 0;
        if (i + 2 < n) {
            int32_t c = head[hash(i)];
            const size_t maxLen = std::min<size_t>(258, n - i);
            for (int k = 0; k < kChain && c >= 0 && i - size_t(c) <= size_t(kWindow - 1); ++k) {
                const uint8_t* a = data + c;
                const uint8_t* b = data + i;
                if (a[best] == b[best]) {
                    size_t l = 0;
                    while (l < maxLen && a[l] == b[l]) ++l;
                    if (int(l) > best) best = int(l), bestDist = int(i - size_t(c));
                    if (l == maxLen) break;
                }
                c = prev[size_t(c) & (kWindow - 1)];
            }
        }
        if (best >= 3) {
            match(best, bestDist);
            for (int k = 0; k < best; ++k) insert(i + size_t(k));
            i += size_t(best);
        } else {
            literal(data[i]);
            insert(i);
            ++i;
        }
        if (out_.size() >= (1u << 20)) flushBytes(false);
    }
    literal(256);                  // the block's end
    putBits(0, 3);                 // an empty stored block, not the last
    if (bitCount_) putBits(0, 8 - bitCount_);
    out_.insert(out_.end(), {0x00, 0x00, 0xFF, 0xFF});
}

void PngWriter::flushBytes(bool all) {
    (void)all;
    if (!out_.empty()) chunk("IDAT", out_.data(), out_.size());
    out_.clear();
}

bool PngWriter::addRows(const uint8_t* rgba, int rows) {
    if (!f_ || !ok_ || rows <= 0 || rows_ + rows > h_) return ok_ = false;
    const size_t stride = size_t(w_) * 4;
    filtered_.resize(size_t(rows) * (stride + 1));
    std::vector<uint8_t> cand[4];
    for (auto& c : cand) c.resize(stride);
    for (int y = 0; y < rows; ++y) {
        const uint8_t* row = rgba + size_t(y) * stride;
        const uint8_t* up = prev_.data();
        // Each filter (none, sub, up, Paeth); the one with the smallest sum of the values as signed bytes.
        long best = -1;
        int bestType = 0;
        for (int t = 0; t < 4; ++t) {
            uint8_t* o = cand[t].data();
            long sum = 0;
            for (size_t x = 0; x < stride; ++x) {
                const int a = x >= 4 ? row[x - 4] : 0, b = up[x], c = x >= 4 ? up[x - 4] : 0;
                const uint8_t v = uint8_t(t == 0 ? row[x] : t == 1 ? row[x] - a : t == 2 ? row[x] - b : row[x] - paeth(a, b, c));
                o[x] = v;
                sum += v < 128 ? v : 256 - v;
            }
            if (best < 0 || sum < best) best = sum, bestType = t;
        }
        uint8_t* dst = filtered_.data() + size_t(y) * (stride + 1);
        dst[0] = uint8_t(bestType == 3 ? 4 : bestType);
        std::memcpy(dst + 1, cand[bestType].data(), stride);
        std::memcpy(prev_.data(), row, stride);
    }
    // (the Adler-32 of the stream's bytes, before the compression)
    const uint8_t* p = filtered_.data();
    size_t left = filtered_.size();
    while (left) {
        const size_t k = std::min<size_t>(left, 5552);
        for (size_t j = 0; j < k; ++j) adlerA_ += p[j], adlerB_ += adlerA_;
        adlerA_ %= 65521, adlerB_ %= 65521;
        p += k, left -= k;
    }
    compress(filtered_.data(), filtered_.size());
    flushBytes(false);
    rows_ += rows;
    return ok_;
}

bool PngWriter::finish() {
    if (!f_) return false;
    if (rows_ != h_) ok_ = false;
    putBits(1, 1), putBits(1, 2);  // the last block: empty
    literal(256);
    if (bitCount_) putBits(0, 8 - bitCount_);
    put32(out_, adlerB_ << 16 | adlerA_);
    flushBytes(true);
    chunk("IEND", nullptr, 0);
    const bool closed = std::fclose(f_) == 0;
    f_ = nullptr;
    if (!ok_ || !closed) {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        return false;
    }
    return true;
}

}  // namespace stardraft
