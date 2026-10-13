// One HTTPS request (included into main.cpp before crash.inc): the crash reports, the feedback and the update check
// all send their GitHub requests here. A failed query, read or file write makes the reply incomplete, never a
// silent success. No app state is used: crash.inc calls it from the --send-crash process too.

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
    HttpReply r;
    URL_COMPONENTS uc{sizeof uc};
    std::wstring host(url.size() + 1, L'\0'), path(url.size() + 1, L'\0'), extra(url.size() + 1, L'\0');
    uc.lpszHostName = host.data(), uc.dwHostNameLength = DWORD(host.size());
    uc.lpszUrlPath = path.data(), uc.dwUrlPathLength = DWORD(path.size());
    uc.lpszExtraInfo = extra.data(), uc.dwExtraInfoLength = DWORD(extra.size());
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return r;
    host.resize(uc.dwHostNameLength), path.resize(uc.dwUrlPathLength), extra.resize(uc.dwExtraInfoLength);
    const std::wstring object = path + extra;
    HINTERNET s = WinHttpOpen(L"StarDraft", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host.c_str(), uc.nPort, 0) : nullptr;
    HINTERNET q = c ? WinHttpOpenRequest(c, method, object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (q) {
        std::wstring headers = L"Accept: " + std::wstring(accept) + L"\r\n";
        if (_wcsicmp(host.c_str(), L"api.github.com") == 0 && !token.empty()) headers += L"Authorization: Bearer " + widen(token) + L"\r\n";
        if (!body.empty()) headers += L"Content-Type: application/json\r\n";
        if (!followRedirects) {
            DWORD off = WINHTTP_DISABLE_REDIRECTS;
            WinHttpSetOption(q, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof off);
        }
        LPVOID data = body.empty() ? WINHTTP_NO_REQUEST_DATA : LPVOID(body.data());
        if (WinHttpSendRequest(q, headers.c_str(), DWORD(-1), data, DWORD(body.size()), DWORD(body.size()), 0) && WinHttpReceiveResponse(q, nullptr)) {
            DWORD size = sizeof r.status;
            WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &r.status, &size, WINHTTP_NO_HEADER_INDEX);
            if (!followRedirects && r.status >= 300 && r.status < 400) {
                wchar_t loc[4096] = L"";
                DWORD n = sizeof loc;
                if (WinHttpQueryHeaders(q, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &n, WINHTTP_NO_HEADER_INDEX)) r.location = loc;
            } else {
                std::ofstream out;
                bool open = true;
                if (file) {
                    if (r.status == 200) out.open(*file, std::ios::binary);
                    open = r.status == 200 && out;
                }
                for (DWORD n = 0; open;) {
                    if (!WinHttpQueryDataAvailable(q, &n)) break;
                    if (n == 0) {
                        r.complete = true;
                        break;
                    }
                    std::string chunk(n, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(q, chunk.data(), n, &read) || read == 0) break;
                    if (file) {
                        if (!out.write(chunk.data(), read)) break;
                    } else if (text) {
                        text->append(chunk.data(), read);
                    }
                }
                if (file && out.is_open()) {
                    out.close();
                    if (out.fail()) r.complete = false;
                }
            }
        }
        WinHttpCloseHandle(q);
    }
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return r;
}
