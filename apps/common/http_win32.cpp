#include "http.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <fstream>

namespace stardraft::platform {

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace

HttpReply httpRequest(const HttpRequest& request, std::string* text, const std::filesystem::path* file) {
    HttpReply r;
    const std::wstring url = wide(request.url);
    URL_COMPONENTS uc{sizeof uc};
    std::wstring host(url.size() + 1, L'\0'), path(url.size() + 1, L'\0'), extra(url.size() + 1, L'\0');
    uc.lpszHostName = host.data(), uc.dwHostNameLength = DWORD(host.size());
    uc.lpszUrlPath = path.data(), uc.dwUrlPathLength = DWORD(path.size());
    uc.lpszExtraInfo = extra.data(), uc.dwExtraInfoLength = DWORD(extra.size());
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return r;
    host.resize(uc.dwHostNameLength), path.resize(uc.dwUrlPathLength), extra.resize(uc.dwExtraInfoLength);
    const std::wstring object = path + extra, method = wide(request.method);
    HINTERNET s = WinHttpOpen(L"StarDraft", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host.c_str(), uc.nPort, 0) : nullptr;
    HINTERNET q = c ? WinHttpOpenRequest(c, method.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                    : nullptr;
    if (q) {
        std::wstring headers = L"Accept: " + wide(request.accept) + L"\r\n";
        if (_wcsicmp(host.c_str(), L"api.github.com") == 0 && !request.token.empty()) headers += L"Authorization: Bearer " + wide(request.token) + L"\r\n";
        if (!request.body.empty()) headers += L"Content-Type: application/json\r\n";
        if (!request.followRedirects) {
            DWORD off = WINHTTP_DISABLE_REDIRECTS;
            WinHttpSetOption(q, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof off);
        }
        const std::string& body = request.body;
        LPVOID data = body.empty() ? WINHTTP_NO_REQUEST_DATA : LPVOID(body.data());
        if (WinHttpSendRequest(q, headers.c_str(), DWORD(-1), data, DWORD(body.size()), DWORD(body.size()), 0) && WinHttpReceiveResponse(q, nullptr)) {
            DWORD status = 0, size = sizeof status;
            WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            r.status = int(status);
            if (!request.followRedirects && r.status >= 300 && r.status < 400) {
                wchar_t loc[4096] = L"";
                DWORD n = sizeof loc;
                if (WinHttpQueryHeaders(q, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &n, WINHTTP_NO_HEADER_INDEX)) r.location = utf8(loc);
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

}  // namespace stardraft::platform
