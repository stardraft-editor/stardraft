// No WebSocket client on this system yet (shared maps are Windows only for now): connect reports it.

#include "websocket.h"

#include <thread>

namespace stardraft::platform {

bool webSocketsAvailable() { return false; }

std::unique_ptr<WebSocket> WebSocket::connect(const std::string&, Handlers handlers, size_t) {
    if (handlers.closed) handlers.closed("shared maps are not available on this system yet");
    return nullptr;
}

}  // namespace stardraft::platform
