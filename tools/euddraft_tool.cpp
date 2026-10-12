// euddraft and EUD projects from the command line.
//
// usage: euddraft_tool list
//        euddraft_tool latest
//        euddraft_tool install                      (the latest release, into StarDraft's folder)
//        euddraft_tool build <file.eds>
//        euddraft_tool newproject <parent dir> <name> <map.scx>
//        euddraft_tool compile <project .eds or folder>

#include <cstdio>
#include <cstring>

#include "stardraft/eud_project.h"
#include "stardraft/euddraft.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: euddraft_tool list | latest | install | build | newproject | compile\n"), 2;
    std::string err;
    auto print = [](const std::string& l) { std::printf("| %s\n", l.c_str()); };
    if (!std::strcmp(argv[1], "list")) {
        std::printf("managed root: %s\n", euddraft::managedRoot().string().c_str());
        for (const auto& i : euddraft::installed()) std::printf("%-10s %-12s %s\n", i.source.c_str(), i.version.c_str(), i.exe.string().c_str());
        if (auto use = euddraft::find()) std::printf("builds with: %s %s\n", use->version.c_str(), use->exe.string().c_str());
        return 0;
    }
    if (!std::strcmp(argv[1], "latest") || !std::strcmp(argv[1], "install")) {
        euddraft::Release r;
        if (!euddraft::latestRelease(r, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        std::printf("latest: %s  %s\n", r.version.c_str(), r.zipUrl.c_str());
        if (!std::strcmp(argv[1], "latest")) return 0;
        EuddraftInstall in;
        if (!euddraft::install(r, in, err, print)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        std::printf("installed %s at %s\n", in.version.c_str(), in.exe.string().c_str());
        return 0;
    }
    if (!std::strcmp(argv[1], "build") && argc > 2) {
        auto use = euddraft::find();
        if (!use) return std::fprintf(stderr, "no euddraft found\n"), 1;
        auto r = euddraft::build(use->exe, argv[2], print);
        std::printf("%s (exit %d, %zu error lines)\n", r.ok ? "built" : "FAILED", r.exitCode, r.errors.size());
        return r.ok ? 0 : 1;
    }
    if (!std::strcmp(argv[1], "newproject") && argc > 4) {
        EudProject p;
        if (!EudProject::create(argv[2], argv[3], argv[4], p, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        std::printf("created %s\n", p.file().string().c_str());
        return 0;
    }
    if (!std::strcmp(argv[1], "compile") && argc > 2) {
        EudProject p;
        if (!p.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        auto r = p.compile({}, print);
        std::printf("%s -> %s\n", r.ok ? "built" : "FAILED", p.outputMap().string().c_str());
        return r.ok ? 0 : 1;
    }
    return 2;
}
