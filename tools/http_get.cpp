// One HTTPS request through apps/common (WinHTTP on Windows, curl elsewhere), to check it by hand: the status, then
// the body (or the file it went to).
//
// usage: http_get <url> [<file>]

#include <cstdio>
#include <string>

#include "http.h"

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: http_get <url> [<file>]\n"), 2;
    stardraft::platform::HttpRequest q;
    q.url = argv[1];
    std::string text;
    const std::filesystem::path file = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path();
    const auto r = stardraft::platform::httpRequest(q, &text, argc > 2 ? &file : nullptr);
    std::printf("status %d, %s\n", r.status, r.complete ? "complete" : "not complete");
    if (argc <= 2) std::printf("%s\n", text.substr(0, 2000).c_str());
    return r.complete && r.status == 200 ? 0 : 1;
}
