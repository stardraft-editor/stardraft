// One HTTPS request (included into main.cpp before crash.inc): the crash reports, the feedback and the update check
// all send their GitHub requests here. The request itself is apps/common (WinHTTP on Windows, curl elsewhere); a
// failed query, read or file write makes the reply incomplete, never a silent success. No app state is used:
// crash.inc calls it from the --send-crash process too.

struct HttpReply {
    DWORD status = 0;        // HTTP status; 0 when no reply came
    bool complete = false;   // whole body read: no query, read or file-write failure
    std::wstring location;   // Location of a 3xx reply (only when followRedirects is false)
};

// `token` goes to api.github.com only (as "Authorization: Bearer"), when it is not empty. `body`: sent as JSON when
// not empty. `file`: the body of a 200 reply is written there; else the body (of any status) goes into `text`, when
// it is not null. followRedirects false: a 3xx reply returns its Location, with the body not read.
HttpReply httpRequest(const std::wstring& url, const wchar_t* method, const wchar_t* accept, const std::string& token,
                      const std::string& body, std::string* text, const fs::path* file, bool followRedirects) {
    stardraft::platform::HttpRequest q;
    q.url = narrow(url), q.method = narrow(method), q.accept = narrow(accept), q.token = token, q.body = body;
    q.followRedirects = followRedirects;
    const stardraft::platform::HttpReply r = stardraft::platform::httpRequest(q, text, file);
    return {DWORD(r.status), r.complete, widen(r.location)};
}
