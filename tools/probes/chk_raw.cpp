// A map's scenario.chk as the bytes are in the file: each locale's copy in the MPQ, then the section headers in file
// order (offset, name, size; a negative size is a jump), and what is left after the last header. For checks of how a
// map is protected (the tricks that make other editors refuse it).
//
// usage: chk_raw <map> [--save <file>]   (--save: the copy that has the most bytes, to a file)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <StormLib.h>

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: chk_raw <map> [--save <file>]\n"), 2;
    HANDLE mpq = nullptr;
    if (!SFileOpenArchive(std::filesystem::path(argv[1]).c_str(), 0, MPQ_OPEN_FORCE_MPQ_V1 | STREAM_FLAG_READ_ONLY, &mpq))
        return std::fprintf(stderr, "not an MPQ (error %u)\n", unsigned(GetLastError())), 1;
    const LCID locales[] = {0, 0x409, 0x412, 0x404, 0x804, 0x407, 0x40c, 0x410, 0x411, 0x415, 0x416, 0x419, 0x40a, 0x405};
    std::vector<uint8_t> best;
    for (LCID l : locales) {
        SFileSetLocale(l);
        HANDLE f = nullptr;
        if (!SFileOpenFileEx(mpq, "staredit\\scenario.chk", SFILE_OPEN_FROM_MPQ, &f)) continue;
        LCID got = 0;
        SFileGetFileInfo(f, SFileInfoLocale, &got, sizeof got, nullptr);
        if (got != l) {
            SFileCloseFile(f);
            continue;
        }
        const DWORD size = SFileGetFileSize(f, nullptr);
        std::vector<uint8_t> b(size);
        DWORD read = 0;
        const bool ok = size == 0 || SFileReadFile(f, b.data(), size, &read, nullptr);
        std::printf("locale %#x: %u bytes, %s (read %u, error %u)\n", unsigned(l), unsigned(size), ok ? "read" : "not read", unsigned(read),
                    ok ? 0u : unsigned(GetLastError()));
        SFileCloseFile(f);
        b.resize(read);
        if (b.size() > best.size()) best = std::move(b);
    }
    SFileCloseArchive(mpq);
    if (best.empty()) return std::fprintf(stderr, "no scenario.chk that reads\n"), 1;
    const std::vector<uint8_t>& c = best;
    std::map<std::string, int> count;
    size_t p = 0;
    int jumps = 0, shown = 0;
    while (p + 8 <= c.size()) {
        char name[5] = {};
        std::memcpy(name, &c[p], 4);
        int32_t size;
        std::memcpy(&size, &c[p + 4], 4);
        for (char& ch : name)
            if (ch && (ch < 32 || ch > 126)) ch = '?';
        ++count[name];
        const bool odd = size < 0 || size_t(p) + 8 + size_t(size) > c.size();
        if (shown < 400 || odd) std::printf("  %8zu %-4s %11d%s\n", p, name, size, size < 0 ? "  (jump)" : odd ? "  (past the end)" : ""), ++shown;
        if (size < 0) {
            ++jumps;
            if (int64_t(p) + 8 + size < 0) {
                std::printf("  jump before the start: stop\n");
                break;
            }
            if (jumps > 100000) break;
        }
        p = size_t(int64_t(p) + 8 + size);
        if (p > c.size()) break;
    }
    std::printf("%zu bytes; %zu left after the last header; jumps %d\n", c.size(), p <= c.size() ? c.size() - p : 0, jumps);
    for (const auto& [n, k] : count)
        if (k > 1) std::printf("  %s appears %d times\n", n.c_str(), k);
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--save") {
            FILE* o = std::fopen(argv[i + 1], "wb");
            if (o) std::fwrite(c.data(), 1, c.size(), o), std::fclose(o);
        }
    return 0;
}
