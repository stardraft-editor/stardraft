// The WebSocket client on macOS: NSURLSession's (NSURLSessionWebSocketTask, macOS 10.15). The session's own serial
// queue calls the handlers, one at a time; sends go out in order, the next after the last one is sent, so the UI
// thread never waits for the network. With ARC.

#include "websocket.h"

#import <Foundation/Foundation.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace stardraft::platform {
namespace {
class MacWebSocket;
}
}  // namespace stardraft::platform

// The session's delegate: the opening and the end of the connection (the HTTP status when the upgrade fails).
@interface SDWebSocketDelegate : NSObject <NSURLSessionWebSocketDelegate>
@property(nonatomic, assign) stardraft::platform::MacWebSocket* owner;  // (nil after close: on the session's queue)
@end

namespace stardraft::platform {

namespace {

class MacWebSocket final : public WebSocket {
public:
    MacWebSocket(const std::string& url, Handlers h, size_t maxMessage) : h_(std::move(h)), max_(maxMessage) {
        NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        queue_ = [[NSOperationQueue alloc] init];
        queue_.maxConcurrentOperationCount = 1;  // (one handler at a time)
        queue_.name = @"StarDraft WebSocket";
        delegate_ = [[SDWebSocketDelegate alloc] init];
        delegate_.owner = this;
        if (!u || !([u.scheme isEqualToString:@"wss"] || [u.scheme isEqualToString:@"ws"])) {
            [queue_ addOperationWithBlock:^{ finish("the server address is not valid"); }];
            return;
        }
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.HTTPShouldSetCookies = NO;
        config.URLCache = nil;
        session_ = [NSURLSession sessionWithConfiguration:config delegate:delegate_ delegateQueue:queue_];
        task_ = [session_ webSocketTaskWithURL:u];
        // (larger messages fail the receive: the connection ends, as on Windows)
        task_.maximumMessageSize = NSInteger(max_);
        [task_ resume];
    }
    ~MacWebSocket() override { close(); }

    void sendText(std::string text) override { queue(std::move(text), true); }
    void sendBinary(std::vector<uint8_t> data) override { queue(std::string(data.begin(), data.end()), false); }

    void close() override {
        const bool onQueue = [NSOperationQueue currentQueue] == queue_;
        {
            // What the queue holds goes first (a goodbye), for at most 2 seconds; not on the session's own queue,
            // where the sends' completions run.
            std::unique_lock<std::mutex> lock(m_);
            if (closed_) return;
            if (open_ && !onQueue)
                cv_.wait_for(lock, std::chrono::seconds(2), [&] { return (out_.empty() && !sending_) || failed_; });
            closed_ = true;
        }
        cv_.notify_all();
        [task_ cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
        [session_ invalidateAndCancel];  // (the session lets the delegate go)
        // No handler after this returns: the one that runs now ends first (the queue runs one at a time).
        if (!onQueue) [queue_ waitUntilAllOperationsAreFinished];
        // The completions that come later see no owner and return.
        SDWebSocketDelegate* d = delegate_;
        if (onQueue) {
            d.owner = nullptr;
        } else {
            [queue_ addOperationWithBlock:^{ d.owner = nullptr; }];
            [queue_ waitUntilAllOperationsAreFinished];
        }
    }

    // The delegate's calls, on the session's queue.
    void opened() {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (closed_) return;
            open_ = true;
        }
        if (h_.opened) h_.opened();
        receive();
        sendNext();
    }
    void ended(NSInteger status, NSURLSessionWebSocketCloseCode code, NSError* error) {
        if (status != 0 && status != 101) return finish("the server did not answer as a StarDraft relay (HTTP " + std::to_string(status) + ")");
        if (code != NSURLSessionWebSocketCloseCodeInvalid)
            return finish(code == NSURLSessionWebSocketCloseCodeNormalClosure ? "" : "the server closed the connection (" + std::to_string(long(code)) + ")");
        bool wasOpen;
        {
            std::lock_guard<std::mutex> lock(m_);
            wasOpen = open_;
        }
        if (error) return finish(wasOpen ? "the connection was lost" : "could not connect to the server");
        finish("");
    }
    void finish(const std::string& why) {
        bool call = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            call = !closed_ && !reported_;
            reported_ = true;
            failed_ = true;
        }
        cv_.notify_all();
        if (call && h_.closed) h_.closed(why);
    }

private:
    struct Out {
        std::string data;
        bool text;
    };
    bool stopping() {
        std::lock_guard<std::mutex> lock(m_);
        return closed_ || reported_;
    }

    void queue(std::string data, bool text) {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (closed_) return;
            out_.push_back({std::move(data), text});
        }
        NSOperationQueue* q = queue_;
        MacWebSocket* self = this;
        // (on the session's queue: close() waits for it, so `self` is there)
        [q addOperationWithBlock:^{ self->sendNext(); }];
    }

    // One message at a time; the next when the last one is sent. On the session's queue.
    void sendNext() {
        NSURLSessionWebSocketMessage* msg = nil;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (closed_ || !open_ || sending_ || out_.empty() || failed_) return;
            Out o = std::move(out_.front());
            out_.pop_front();
            sending_ = true;
            if (o.text) {
                NSString* s = [[NSString alloc] initWithBytes:o.data.data() length:o.data.size() encoding:NSUTF8StringEncoding];
                msg = [[NSURLSessionWebSocketMessage alloc] initWithString:s ?: @""];
            } else {
                msg = [[NSURLSessionWebSocketMessage alloc] initWithData:[NSData dataWithBytes:o.data.data() length:o.data.size()]];
            }
        }
        MacWebSocket* self = this;
        SDWebSocketDelegate* d = delegate_;
        [task_ sendMessage:msg
            completionHandler:^(NSError* error) {
                if (!d.owner) return;  // (closed: the object may be gone)
                {
                    std::lock_guard<std::mutex> lock(self->m_);
                    self->sending_ = false;
                }
                self->cv_.notify_all();
                if (error) return self->finish(self->stopping() ? "" : "could not send to the server");
                self->sendNext();
            }];
    }

    void receive() {
        MacWebSocket* self = this;
        SDWebSocketDelegate* d = delegate_;
        [task_ receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* msg, NSError* error) {
            if (!d.owner || self->stopping()) return;
            if (error) {
                // (a message over maximumMessageSize fails here; the close itself comes to the delegate)
                if (error.code == NSURLErrorCancelled) return;
                return self->finish("the connection was lost");
            }
            if (msg.type == NSURLSessionWebSocketMessageTypeString) {
                const char* s = msg.string.UTF8String;
                std::string text = s ? s : "";
                if (text.size() > self->max_) return self->finish("a message was too large");
                if (self->h_.text) self->h_.text(std::move(text));
            } else {
                NSData* data = msg.data;
                if (data.length > self->max_) return self->finish("a message was too large");
                const uint8_t* p = static_cast<const uint8_t*>(data.bytes);
                if (self->h_.binary) self->h_.binary(std::vector<uint8_t>(p, p + data.length));
            }
            if (!self->stopping()) self->receive();
        }];
    }

    Handlers h_;
    size_t max_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Out> out_;
    bool open_ = false, closed_ = false, reported_ = false, sending_ = false, failed_ = false;
    NSOperationQueue* queue_ = nil;
    SDWebSocketDelegate* delegate_ = nil;
    NSURLSession* session_ = nil;
    NSURLSessionWebSocketTask* task_ = nil;
};

}  // namespace

bool webSocketsAvailable() { return true; }

std::unique_ptr<WebSocket> WebSocket::connect(const std::string& url, Handlers handlers, size_t maxMessage) {
    return std::make_unique<MacWebSocket>(url, std::move(handlers), maxMessage);
}

}  // namespace stardraft::platform

@implementation SDWebSocketDelegate

- (void)URLSession:(NSURLSession*)session webSocketTask:(NSURLSessionWebSocketTask*)task didOpenWithProtocol:(NSString*)protocol {
    if (self.owner) self.owner->opened();
}

- (void)URLSession:(NSURLSession*)session
        webSocketTask:(NSURLSessionWebSocketTask*)task
    didCloseWithCode:(NSURLSessionWebSocketCloseCode)code
              reason:(NSData*)reason {
    if (self.owner) self.owner->ended(0, code, nil);
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
    if (!self.owner) return;
    NSInteger status = 0;
    if ([task.response isKindOfClass:[NSHTTPURLResponse class]]) status = ((NSHTTPURLResponse*)task.response).statusCode;
    self.owner->ended(status, NSURLSessionWebSocketCloseCodeInvalid, error);
}

// The relay never redirects: a redirect goes nowhere (as on Windows).
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler {
    completionHandler(nil);
}

@end
