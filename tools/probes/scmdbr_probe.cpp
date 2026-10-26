// What an SCMDraft brush file holds (.scmdbrh: an MPQ archive; Cosmonarchy BW ships one per tileset): its files (also
// the ones without a name in a listfile: StormLib names them FileNNNNNNNN.xxx), their sizes and first bytes.
// usage: scmdbr_probe <file.scmdbrh> [file inside: written to stdout in full]
#include <StormLib.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: scmdbr_probe <file.scmdbrh> [name]\n"), 2;
    HANDLE a = nullptr;
    if (!SFileOpenArchive(std::filesystem::path(argv[1]).c_str(), 0, MPQ_OPEN_READ_ONLY | MPQ_OPEN_FORCE_MPQ_V1, &a))
        return std::fprintf(stderr, "cannot open (StormLib error %lu)\n", GetLastError()), 1;
    SFILE_FIND_DATA fd;
    HANDLE find = SFileFindFirstFile(a, "*", &fd, nullptr);
    while (find) {
        HANDLE f = nullptr;
        if (SFileOpenFileEx(a, fd.cFileName, SFILE_OPEN_FROM_MPQ, &f)) {
            DWORD high = 0, size = SFileGetFileSize(f, &high), read = 0;
            std::vector<unsigned char> d(size);
            SFileReadFile(f, d.data(), size, &read, nullptr);
            SFileCloseFile(f);
            if (argc > 2) {
                if (std::string(fd.cFileName) == argv[2]) {
                    std::fwrite(d.data(), 1, read, stdout);
                    break;
                }
            } else {
                std::printf("%-40s %8lu  ", fd.cFileName, size);
                for (DWORD i = 0; i < read && i < 32; ++i) std::printf("%02x ", d[i]);
                std::printf("\n");
            }
        }
        if (!SFileFindNextFile(find, &fd)) break;
    }
    if (find) SFileFindClose(find);
    SFileCloseArchive(a);
    return 0;
}
