#pragma once

// A WebSocket client (wss://, or ws:// to this computer), for shared maps (include/stardraft/collab.h). WinHTTP on
// Windows (websocket_win32.cpp), NSURLSession on macOS (websocket_mac.mm); not there yet on Linux (websocket_none.cpp:
// connect fails).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stardraft::platform {

class WebSocket {
public:
    // Called on the socket's own thread, one at a time, never after close() or the destructor returned.
    struct Handlers {
        std::function<void()> opened;
        std::function<void(std::string)> text;
        std::function<void(std::vector<uint8_t>)> binary;
        std::function<void(std::string)> closed;  // why (empty: closed by this program or the server, normally)
    };
    // Connects on a thread of its own. Messages larger than maxMessage close the socket.
    static std::unique_ptr<WebSocket> connect(const std::string& url, Handlers handlers, size_t maxMessage = 2 << 20);
    virtual ~WebSocket() = default;
    // Queued and sent in order on a thread of their own (any thread may call them).
    virtual void sendText(std::string text) = 0;
    virtual void sendBinary(std::vector<uint8_t> data) = 0;
    // Ends the connection; the handlers are not called after it returns.
    virtual void close() = 0;
};

// Whether this system has a WebSocket client.
bool webSocketsAvailable();

}  // namespace stardraft::platform
