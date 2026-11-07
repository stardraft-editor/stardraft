// SPDX-License-Identifier: MIT
// Map Editor first-party search execution and memoization.
// Search workers own private engine states; only immutable game/global data is
// shared.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace mapeditor::simulation::mining_compute {
struct Cancelled : std::exception {};
struct Executor {
  std::atomic<bool> cancelled{false}, paused{false};
  mutable std::mutex pauseMutex;
  mutable std::condition_variable pauseWake;
  std::atomic<std::size_t> completed{0}, running{0};
  mutable std::atomic<std::size_t> parked{0};
  std::mutex mutex;
  std::condition_variable wake;
  std::queue<std::function<void()>> queue;
  std::vector<std::thread> threads;
  bool stopping = false;
  static unsigned concurrency() {
    unsigned n = std::thread::hardware_concurrency();
    return std::min(9u, n > 1 ? n - 1 : 1u);
  }
  explicit Executor(unsigned n = concurrency()) {
    try {
      for (unsigned i = 0; i < std::max(1u, n); ++i)
        threads.emplace_back([this] {
          for (;;) {
            std::function<void()> task;
            {
              std::unique_lock<std::mutex> lock(mutex);
              wake.wait(lock, [&] { return stopping || !queue.empty(); });
              if (stopping && queue.empty())
                return;
              task = std::move(queue.front());
              queue.pop();
              ++running;
            }
            task();
            ++completed;
            {
              std::lock_guard<std::mutex> lock(pauseMutex);
              --running;
            }
            pauseWake.notify_all();
          }
        });
    } catch (...) {
      stop();
      throw;
    }
  }
  void check() const {
    if (cancelled.load())
      throw Cancelled();
    if (paused.load()) {
      std::unique_lock<std::mutex> lock(pauseMutex);
      if (paused.load()) {
        ++parked;
        pauseWake.notify_all();
        pauseWake.wait(lock,
                       [&] { return cancelled.load() || !paused.load(); });
        --parked;
      }
    }
    if (cancelled.load())
      throw Cancelled();
  }
  void pause(bool value) {
    std::unique_lock<std::mutex> lock(pauseMutex);
    paused = value;
    if (!value) {
      lock.unlock();
      pauseWake.notify_all();
      return;
    }
    pauseWake.wait(lock, [&] {
      return cancelled.load() || running.load() == parked.load();
    });
  }
  void cancel() {
    {
      std::lock_guard<std::mutex> lock(pauseMutex);
      cancelled = true;
    }
    pauseWake.notify_all();
  }
  template <class F> auto submit(F f) -> std::future<decltype(f())> {
    using R = decltype(f());
    auto task = std::make_shared<std::packaged_task<R()>>(std::move(f));
    auto future = task->get_future();
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (stopping)
        throw std::logic_error("Cannot submit to a stopped search executor");
      queue.push([task] { (*task)(); });
    }
    wake.notify_one();
    return future;
  }
  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
    }
    wake.notify_all();
    for (auto &t : threads)
      if (t.joinable())
        t.join();
  }
  ~Executor() {
    cancel();
    stop();
  }
};

// Memoize the complete lineup, not just the worker's command/position: changing
// another worker is a different phase-space case. Reuse in-flight results as
// well as completed results.
template <class T> struct Memo {
  std::map<std::vector<std::int64_t>, std::shared_future<T>> entries;
  std::size_t hits = 0;
  void remember(std::vector<std::int64_t> key, const T &value) {
    if (entries.count(key))
      return;
    std::promise<T> ready;
    ready.set_value(value);
    entries.emplace(std::move(key), ready.get_future().share());
  }
  template <class F>
  std::shared_future<T> submit(Executor &pool, std::vector<std::int64_t> key,
                               F run) {
    auto it = entries.find(key);
    if (it != entries.end()) {
      ++hits;
      return it->second;
    }
    auto future = pool.submit(std::move(run)).share();
    entries.emplace(std::move(key), future);
    return future;
  }
};

} // namespace mapeditor::simulation::mining_compute
