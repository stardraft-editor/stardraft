// The system parts of apps/common that need no network and no window: the self-update's unpacking and replacing (on
// files in a scratch folder, never on the running program), the program's own file, and the crash report's title.
//
// usage: platform_test <scratch dir>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "crash_report.h"
#include "update.h"

namespace fs = std::filesystem;
using namespace stardraft::platform;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
    if (!ok) ++failures;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

// A zip with one file, stardraft.exe, whose text is "new program".
const unsigned char kZip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x31, 0xad, 0x4e, 0x5d, 0x56, 0xa7, 0xde, 0x3c, 0x0d, 0x00, 0x00, 0x00, 0x0b,
    0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x73, 0x74, 0x61, 0x72, 0x64, 0x72, 0x61, 0x66, 0x74, 0x2e, 0x65, 0x78, 0x65, 0xcb, 0x4b, 0x2d,
    0x57, 0x28, 0x28, 0xca, 0x4f, 0x2f, 0x4a, 0xcc, 0x05, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x31,
    0xad, 0x4e, 0x5d, 0x56, 0xa7, 0xde, 0x3c, 0x0d, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x73, 0x74, 0x61, 0x72, 0x64, 0x72, 0x61, 0x66, 0x74, 0x2e, 0x65, 0x78, 0x65,
    0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x3b, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: platform_test <scratch dir>\n"), 2;
    const fs::path dir = fs::path(argv[1]) / "platform test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "unpacked", ec);

    const fs::path self = programFile();
    check(!self.empty() && fs::exists(self, ec) && self.filename().string().find("platform_test") == 0, "the program's own file: " + self.filename().string());
    check(std::string(releaseAssetSuffix()).size() > 4, std::string("this system's download ends in ") + releaseAssetSuffix());

    // Unpacking a release's zip.
    std::ofstream(dir / "update.zip", std::ios::binary).write(reinterpret_cast<const char*>(kZip), sizeof kZip);
    check(unpackArchive(dir / "update.zip", dir / "unpacked") && read(dir / "unpacked" / "stardraft.exe") == "new program", "a zip unpacks");

    // Replacing a program file (a stand-in in the scratch folder).
    const fs::path program = dir / "stardraft.exe";
    std::ofstream(program, std::ios::binary) << "old program";
    check(replaceProgram(program, dir / "unpacked" / "stardraft.exe") == Replace::Done && read(program) == "new program" &&
              read(oldProgramFile(program)) == "old program",
          "the new program takes the old one's place; the old one is kept beside it");
#ifdef _WIN32
    check(!startProgram(program, {"--restart", "1"}), "a file that is not a program (a damaged download) does not start");
#endif
    // The new program did not start: it goes, and the old one is in its place again.
    check(restoreProgram(program) && read(program) == "old program" && !fs::exists(oldProgramFile(program), ec),
          "a new program that did not start: the old one is in its place again");
    check(!restoreProgram(program) && read(program) == "old program", "no old program to put back: the program stays");
    check(replaceProgram(program, dir / "no such file") == Replace::NewNotPlaced && read(program) == "old program",
          "a new program that is not there: the program stays as it was");

    // A crash report's title: the place, without the address read (it differs each time).
    check(crashTitle("StarDraft\nException 0xC0000005 (access violation) at stardraft.exe+0x1234, reading 0x10\nStack:\n") ==
              "Crash: Exception 0xC0000005 (access violation) at stardraft.exe+0x1234",
          "a crash report's title");
    check(unsentCrashReports(dir / "no crashes").empty(), "no reports in a folder that is not there");

    fs::remove_all(dir, ec);
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
