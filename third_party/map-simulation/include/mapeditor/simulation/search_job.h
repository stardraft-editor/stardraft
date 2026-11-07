// SPDX-License-Identifier: MIT
// Map Editor first-party search coordination over caller-owned value snapshots.
#pragma once
#include "search_compute.h"
namespace mapeditor::simulation {
class World;
}
namespace mapeditor::simulation::mining_compute {
// Consumers inspect published results only after done(), and stop reading them
// before extend(). All provisional state remains private to the coordinator.
template <class Plan, class Input = simulation::World> struct Job {
  std::unique_ptr<Input> world;
  const Input *input;
  std::unique_ptr<typename Plan::Output> result;
  std::unique_ptr<Plan> frontier;
  Executor executor;
  std::atomic<bool> ready{false}, validating{false}, exhausted{false},
      changed{false};
  std::atomic<double> elapsed{0}, sliceElapsed{0}, sliceLimit{0};
  std::exception_ptr failure;
  std::mutex mutex;
  std::condition_variable wake;
  double allowance = 0;
  bool requested = true;
  std::thread control;
  double seconds() const { return elapsed.load(); }
  void extend(double seconds) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!std::isfinite(seconds))
      throw std::invalid_argument("Nonfinite search time allowance");
    if (!ready.load(std::memory_order_acquire) || exhausted.load() || failure)
      return;
    allowance = seconds;
    requested = true;
    ready = false;
    wake.notify_one();
  }
  template <class Factory>
  Job(const Input &st, Factory factory, double seconds = 0,
      unsigned workers = Executor::concurrency())
      : world(std::make_unique<Input>(st)), input(world.get()),
        executor(workers), allowance(seconds) {
    if (!std::isfinite(seconds))
      throw std::invalid_argument("Nonfinite search time allowance");
    control = std::thread([this, factory = std::move(factory)] {
      try {
        frontier = factory(*input, &executor);
        std::vector<std::int64_t> lastProposal;
        for (;;) {
          double limit;
          {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock,
                      [&] { return requested || executor.cancelled.load(); });
            if (executor.cancelled.load())
              return;
            requested = false;
            limit = allowance;
          }
          changed = false;
          sliceElapsed = 0;
          sliceLimit = limit;
          executor.pause(false);
          double previous = elapsed.load();
          auto start = std::chrono::steady_clock::now();
          auto spent = [&] {
            return std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - start)
                .count();
          };
          bool empty = false;
          while (limit <= 0 || spent() < limit) {
            executor.check();
            sliceElapsed = spent();
            elapsed = previous + spent();
            if (frontier->done() && (limit <= 0 || !frontier->expand())) {
              empty = true;
              break;
            }
            frontier->advance(128);
            std::this_thread::yield();
          }
          executor.pause(true);
          sliceElapsed = spent();
          elapsed = previous + spent();
          validating = true;
          auto proposal = frontier->proposal();
          if (!result || proposal != lastProposal) {
            auto verified = frontier->checkpoint([&] {
              if (executor.cancelled.load())
                throw Cancelled();
            });
            if (!result || verified->score() > result->score() + 0.000001 ||
                (std::abs(verified->score() - result->score()) <= 0.000001 &&
                 verified->selections() > result->selections())) {
              result = std::move(verified);
              changed = true;
            }
            lastProposal = std::move(proposal);
          }
          validating = false;
          exhausted = empty;
          ready.store(true, std::memory_order_release);
        }
      } catch (...) {
        failure = std::current_exception();
        validating = false;
        ready.store(true, std::memory_order_release);
      }
    });
  }
  bool done() {
    if (!ready.load(std::memory_order_acquire))
      return false;
    if (failure)
      std::rethrow_exception(failure);
    return true;
  }
  ~Job() {
    executor.cancel();
    {
      std::lock_guard<std::mutex> lock(mutex);
      requested = true;
    }
    wake.notify_all();
    if (control.joinable())
      control.join();
    executor.stop();
  }
};

} // namespace mapeditor::simulation::mining_compute
