#pragma once

// One HTTPS request, for the program's GitHub requests (crash reports, feedback, the update check). The system's own
// HTTP code does it: WinHTTP on Windows (http_win32.cpp), curl elsewhere (http_curl.cpp). No program state: a crash's
// sender process calls it too.

#include <filesystem>
#include <string>

namespace stardraft::platform {

struct HttpReply {
    int status = 0;          // HTTP status; 0 when no reply came
    bool complete = false;   // whole body read: no query, read or file-write failure
    std::string location;    // Location of a 3xx reply (only when followRedirects is false)
};

struct HttpRequest {
    std::string url;                       // https://...
    std::string method = "GET";
    std::string accept = "application/json";
    std::string token;                     // sent to api.github.com only (as "Authorization: Bearer"), when not empty
    std::string body;                      // sent as JSON when not empty
    bool followRedirects = true;           // false: a 3xx reply gives its Location, with the body not read
};

// `file`: the body of a 200 reply is written there; else the body (of any status) goes into `text`, when it is not
// null. A failed query, read or file write makes the reply incomplete, never a silent success.
HttpReply httpRequest(const HttpRequest& request, std::string* text, const std::filesystem::path* file = nullptr);

}  // namespace stardraft::platform
