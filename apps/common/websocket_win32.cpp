// The WebSocket client on Windows: WinHTTP's (WinHttpWebSocketCompleteUpgrade). A thread connects and reads; another
// sends what the queue holds, so the UI thread never waits for the network.

#include "websocket.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace stardraft::platform {

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    std::wstring w(size_t(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    return w;
}

class WinWebSocket final : public WebSocket {
public:
    WinWebSocket(std::string url, Handlers h, size_t maxMessage) : url_(std::move(url)), h_(std::move(h)), max_(maxMessage) {
        reader_ = std::thread([this] { run(); });
    }
    ~WinWebSocket() override { close(); }

    void sendText(std::string text) override { queue(std::move(text), true); }
    void sendBinary(std::vector<uint8_t> data) override { queue(std::string(data.begin(), data.end()), false); }

    void close() override {
        {
            // What the queue holds goes first (a goodbye), for at most 2 seconds.
            std::unique_lock<std::mutex> lock(m_);
            if (ws_ && !closing_ && writer_.joinable() && writer_.get_id() != std::this_thread::get_id())
                cv_.wait_for(lock, std::chrono::seconds(2), [&] { return (out_.empty() && !sending_) || !ws_ || writerDone_; });
        }
        {
            std::lock_guard<std::mutex> lock(m_);
            if (closing_ && !reader_.joinable() && !writer_.joinable()) return;
            closing_ = true;
            if (ws_) WinHttpWebSocketShutdown(ws_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            // (closing the handles ends a receive or a connect that waits)
            if (ws_) WinHttpCloseHandle(ws_), ws_ = nullptr;
            if (request_) WinHttpCloseHandle(request_), request_ = nullptr;
            if (connection_) WinHttpCloseHandle(connection_), connection_ = nullptr;
            if (session_) WinHttpCloseHandle(session_), session_ = nullptr;
        }
        cv_.notify_all();
        if (reader_.joinable() && reader_.get_id() != std::this_thread::get_id()) reader_.join();
        if (writer_.joinable() && writer_.get_id() != std::this_thread::get_id()) writer_.join();
    }

private:
    struct Out {
        std::string data;
        bool text;
    };
    void queue(std::string data, bool text) {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (closing_) return;
            out_.push_back({std::move(data), text});
        }
        cv_.notify_all();
    }

    void finish(const std::string& why) {
        bool call = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            call = !closing_ && !reported_;
            reported_ = true;
        }
        if (call && h_.closed) h_.closed(why);
    }
    bool stopping() {
        std::lock_guard<std::mutex> lock(m_);
        return closing_;
    }

    void run() {
        URL_COMPONENTS uc{sizeof uc};
        // (WinHttpCrackUrl reads http and https only: ws is http, wss is https)
        std::string u = url_;
        if (u.rfind("wss://", 0) == 0) u = "https://" + u.substr(6);
        else if (u.rfind("ws://", 0) == 0) u = "http://" + u.substr(5);
        const std::wstring url = wide(u);
        std::wstring host(url.size() + 1, L'\0'), path(url.size() + 1, L'\0');
        uc.lpszHostName = host.data(), uc.dwHostNameLength = DWORD(host.size());
        uc.lpszUrlPath = path.data(), uc.dwUrlPathLength = DWORD(path.size());
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return finish("the server address is not valid");
        host.resize(uc.dwHostNameLength), path.resize(uc.dwUrlPathLength);
        const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (closing_) return;
            session_ = WinHttpOpen(L"StarDraft", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            connection_ = session_ ? WinHttpConnect(session_, host.c_str(), uc.nPort, 0) : nullptr;
            request_ = connection_ ? WinHttpOpenRequest(connection_, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        secure ? WINHTTP_FLAG_SECURE : 0)
                                   : nullptr;
            if (request_) {
                WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
                DWORD off = WINHTTP_DISABLE_REDIRECTS;  // (the relay never redirects: a redirect goes nowhere)
                WinHttpSetOption(request_, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof off);
            }
        }
        HINTERNET req = request_;
        if (!req) return finish("could not connect to the server");
        if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr))
            return finish("could not connect to the server");
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 101) return finish("the server did not answer as a StarDraft relay (HTTP " + std::to_string(status) + ")");
        {
            std::lock_guard<std::mutex> lock(m_);
            if (closing_) return;
            ws_ = WinHttpWebSocketCompleteUpgrade(request_, 0);
            WinHttpCloseHandle(request_), request_ = nullptr;
            if (ws_) writer_ = std::thread([this] { write(); });
        }
        if (!ws_) return finish("could not connect to the server");
        if (h_.opened && !stopping()) h_.opened();
        std::string msg;
        std::vector<char> buf(64 * 1024);
        for (;;) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            HINTERNET ws;
            {
                std::lock_guard<std::mutex> lock(m_);
                ws = ws_;
            }
            if (!ws) return;
            const DWORD err = WinHttpWebSocketReceive(ws, buf.data(), DWORD(buf.size()), &read, &type);
            if (err) return finish(stopping() ? "" : "the connection was lost");
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                USHORT code = 0;
                wchar_t reason[124] = L"";
                DWORD len = 0;
                {
                    std::lock_guard<std::mutex> lock(m_);
                    if (ws_) WinHttpWebSocketQueryCloseStatus(ws_, &code, reason, sizeof reason, &len);
                }
                return finish(code == 1000 || code == 0 ? "" : "the server closed the connection (" + std::to_string(code) + ")");
            }
            msg.append(buf.data(), read);
            if (msg.size() > max_) return finish("a message was too large");
            if (type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
            if (stopping()) return;
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                if (h_.text) h_.text(std::move(msg));
            } else if (h_.binary) {
                h_.binary(std::vector<uint8_t>(msg.begin(), msg.end()));
            }
            msg.clear();
        }
    }

    void write() {
        for (;;) {
            Out o;
            HINTERNET ws;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [&] { return closing_ || !out_.empty(); });
                if (closing_) return;
                o = std::move(out_.front());
                out_.pop_front();
                ws = ws_;
                sending_ = true;
            }
            if (!ws) return;
            const DWORD err = WinHttpWebSocketSend(ws, o.text ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                                   o.data.empty() ? nullptr : o.data.data(), DWORD(o.data.size()));
            {
                std::lock_guard<std::mutex> lock(m_);
                sending_ = false;
            }
            cv_.notify_all();
            if (err) {
                {
                    std::lock_guard<std::mutex> lock(m_);
                    writerDone_ = true;
                }
                cv_.notify_all();
                return finish(stopping() ? "" : "could not send to the server");
            }
        }
    }

    std::string url_;
    Handlers h_;
    size_t max_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Out> out_;
    bool closing_ = false, reported_ = false, sending_ = false, writerDone_ = false;
    HINTERNET session_ = nullptr, connection_ = nullptr, request_ = nullptr, ws_ = nullptr;
    std::thread reader_, writer_;
};

}  // namespace

bool webSocketsAvailable() { return true; }

std::unique_ptr<WebSocket> WebSocket::connect(const std::string& url, Handlers handlers, size_t maxMessage) {
    return std::make_unique<WinWebSocket>(url, std::move(handlers), maxMessage);
}

}  // namespace stardraft::platform
