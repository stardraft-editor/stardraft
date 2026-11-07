// SPDX-License-Identifier: MIT
#include "session.h"
#include <algorithm>
#include <cmath>
#include <utility>
namespace maplab {
Session::Session(Request request) : thread_([this, request = std::move(request)]() mutable {
    run(std::move(request));
}) {}
Session::~Session() { stop(); }
void Session::stop() {
    stop_ = true;
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}
Snapshot Session::snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}
void Session::speed(int value) { speed_ = std::clamp(value, 0, 16); }
void Session::configureRush(double interval,int retained) {
    if (!std::isfinite(interval) || interval < .5 || interval > 60 || retained < 1 || retained > 50)
        throw std::invalid_argument("Invalid rush settings");
    std::lock_guard lock(mutex_);
    interval_=interval; retained_=retained; configure_=true;
}
void Session::search(int technique, double seconds) {
    if (technique < 0 || technique > 2 || !std::isfinite(seconds) || seconds <= 0 || seconds > 600)
        throw std::invalid_argument("Invalid search settings");
    std::lock_guard lock(mutex_);
    if (!state_.ready || state_.done || state_.searching) return;
    search_ = technique;
    budget_ = seconds;
    state_.searching = true;
}
void Session::run(Request request) {
    try {
        std::shared_ptr<NativeMining> mining;
        std::shared_ptr<NativeRush> rush;
        if (request.mode == Mode::Mining)
            mining = NativeMining::create(request.read, request.scenario, request.start.x,
                                          request.start.y, request.race, request.workers);
        else {
            rush = NativeRush::create(request.read, request.scenario, request.start,
                                      request.end, request.race);
            rush->configure(request.launchInterval,request.retained);
        }
        while (!stop_) {
            int technique;
            double seconds;
            {
                std::lock_guard lock(mutex_);
                technique = std::exchange(search_, -1);
                seconds = budget_;
                if (rush && configure_) { rush->configure(interval_,retained_); configure_=false; }
            }
            if (mining && technique >= 0) mining->search(technique, seconds);
            const int frames = speed_.load();
            if (mining) mining->advance(frames);
            else rush->advance(frames);
            // Searches pause the mining clock; use the engine's clock in its snapshot.
            Snapshot next;
            next.ready = true;
            if (mining) {
                next.mining = mining->snapshot();
            } else {
                next.rush = rush->snapshot();
            }
            {
                std::lock_guard lock(mutex_);
                next.searching = (mining && mining->searching()) || search_ >= 0;
                state_ = std::move(next);
            }
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(42), [&] { return stop_.load(); });
        }
    } catch (const std::exception& e) {
        std::lock_guard lock(mutex_);
        state_.error = e.what();
        state_.done = true;
    }
}
}
