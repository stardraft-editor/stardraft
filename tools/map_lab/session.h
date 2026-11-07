// SPDX-License-Identifier: MIT
#pragma once
#include "native_mining.h"
#include "native_rush.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace maplab {
enum class Mode { Mining, Rush };
struct Request {
    Mode mode = Mode::Mining;
    Bytes scenario;
    FileReader read;
    RushPoint start, end;
    int race = 1, workers = 0;
};
struct Snapshot {
    MiningFrame mining;
    RushFrame rush;
    bool ready = false, done = false;
    std::string error;
};
// The worker owns the engine. The UI receives value snapshots and sends commands.
class Session {
public:
    explicit Session(Request);
    ~Session();
    Session(const Session&) = delete;
    Snapshot snapshot() const;
    void speed(int multiplier);
    void search(int technique, double seconds);
    void stop();
private:
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Snapshot state_;
    int search_ = -1;
    double budget_ = 1;
    std::atomic<int> speed_{1};
    std::atomic<bool> stop_{false};
    std::thread thread_;
    void run(Request);
};
}
