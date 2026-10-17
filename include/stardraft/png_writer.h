#pragma once

// A PNG file written a band of rows at a time (RGBA, 8 bits a channel): a picture larger than the memory can hold at
// once (a map at full size, 16384 x 16384 pixels) goes to the file as it is drawn. Each band is compressed on its own
// (deflate with the fixed codes); the rows of the bands make one picture.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace stardraft {

class PngWriter {
public:
    PngWriter() = default;
    PngWriter(const PngWriter&) = delete;
    PngWriter& operator=(const PngWriter&) = delete;
    ~PngWriter();

    // Starts the file: `width` x `height` pixels. False when the file cannot be made.
    bool open(const std::filesystem::path& path, int width, int height);
    // The next `rows` rows of the picture, each `width` * 4 bytes (R, G, B, A).
    bool addRows(const uint8_t* rgba, int rows);
    // Ends the file. False when a write failed or the rows are not `height`; the file is then removed.
    bool finish();

    int rowsWritten() const { return rows_; }

private:
    void putBits(uint32_t bits, int count);
    void putCode(uint32_t code, int len);  // a Huffman code, its first bit first
    void literal(int v);
    void match(int len, int dist);
    void compress(const uint8_t* data, size_t n);
    void flushBytes(bool all);
    bool chunk(const char* type, const uint8_t* data, size_t n);

    std::FILE* f_ = nullptr;
    std::filesystem::path path_;
    int w_ = 0, h_ = 0, rows_ = 0;
    bool ok_ = false;
    std::vector<uint8_t> prev_, filtered_, out_;
    uint64_t bitBuf_ = 0;
    int bitCount_ = 0;
    uint32_t adlerA_ = 1, adlerB_ = 0;
    bool headerDone_ = false;
};

}  // namespace stardraft
