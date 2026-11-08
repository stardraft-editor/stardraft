// SPDX-License-Identifier: MIT
#include <iostream>
#include <mapeditor/simulation/mining_position.h>
#include <mapeditor/simulation/mining_search.h>
#include <mapeditor/simulation/search_job.h>
#include <source_location>
using namespace mapeditor::simulation::mining_compute;
void check(bool value,
           std::source_location at = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("Search test failed at line " +
                             std::to_string(at.line()));
}
struct Seed {
  int value = 37;
  std::shared_ptr<std::atomic<bool>> terminal;
  std::shared_ptr<std::promise<void>> validating;
  bool blockValidation = false;
};
struct Published {
  double value = 0;
  bool verified = true;
  double score() const { return value; }
  std::size_t selections() const { return value > 0 ? 1 : 0; }
};
struct Frontier {
  using Output = Published;
  Seed seed;
  int steps = 0;
  bool done() const {
    return seed.terminal ? seed.terminal->load() : steps >= 3;
  }
  bool expand() { return false; }
  void advance(int) { ++steps; }
  std::vector<std::int64_t> proposal() const { return {steps}; }
  template <class Check>
  std::unique_ptr<Published> checkpoint(Check check) const {
    check();
    if (seed.validating)
      seed.validating->set_value();
    if (seed.blockValidation)
      for (;;) {
        check();
        std::this_thread::yield();
      }
    return std::make_unique<Published>(Published{double(steps), true});
  }
};
template <class J> void waitReady(J &job) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!job.ready.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  check(job.ready.load(std::memory_order_acquire));
}
void jobTests() {
  Seed seed;
  std::promise<int> observed;
  auto observedValue = observed.get_future();
  auto factory = [](const Seed &input, Executor *) {
    return std::make_unique<Frontier>(Frontier{input});
  };
  {
    Job<Frontier, Seed> job(
        seed,
        [&](const Seed &input, Executor *pool) {
          observed.set_value(input.value);
          return factory(input, pool);
        },
        0, 1);
    seed.value = 99;
    waitReady(job);
    check(observedValue.get() == 37 && job.done() && job.exhausted &&
          job.result && job.result->verified && job.result->score() == 3);
    job.extend(1);
    check(job.done());
  }
  seed.terminal = std::make_shared<std::atomic<bool>>(false);
  {
    Job<Frontier, Seed> job(seed, factory, 0.002, 1);
    waitReady(job);
    check(job.done() && job.result && job.result->verified && !job.exhausted);
    auto *sameFrontier = job.frontier.get();
    double previous = job.result->score(), elapsed = job.seconds();
    job.extend(0.002);
    waitReady(job);
    check(job.done() && job.frontier.get() == sameFrontier &&
          job.result->verified && job.result->score() >= previous &&
          job.seconds() >= elapsed);
    seed.terminal->store(true);
    job.extend(0);
    waitReady(job);
    check(job.done() && job.exhausted && job.result->verified);
  }
  {
    Job<Frontier, Seed> failed(
        seed,
        [](const Seed &, Executor *) -> std::unique_ptr<Frontier> {
          throw std::runtime_error("factory failure");
        },
        0, 1);
    waitReady(failed);
    bool propagated = false;
    try {
      failed.done();
    } catch (const std::runtime_error &) {
      propagated = true;
    }
    check(propagated);
    failed.extend(1);
    check(failed.ready);
  }
  {
    Seed cancelling;
    cancelling.validating = std::make_shared<std::promise<void>>();
    auto signal = cancelling.validating->get_future();
    cancelling.blockValidation = true;
    auto job = std::make_unique<Job<Frontier, Seed>>(cancelling, factory, 0, 1);
    check(signal.wait_for(std::chrono::seconds(2)) ==
          std::future_status::ready);
    job.reset();
  }
}
int main() {
  try {
    Executor pool(2);
    Memo<int> memo;
    std::atomic<int> evaluations{0};
    auto first = memo.submit(pool, {1, 2, 3}, [&] {
      ++evaluations;
      return 42;
    });
    auto duplicate = memo.submit(pool, {1, 2, 3}, [&] {
      ++evaluations;
      return 17;
    });
    check(first.get() == 42 && duplicate.get() == 42 && evaluations == 1 &&
          memo.hits == 1);
    memo.remember({1, 2, 3}, 90);
    memo.remember({4}, 25);
    check(memo.submit(pool, {1, 2, 3}, [] { return -1; }).get() == 42);
    check(memo.submit(pool, {4}, [] { return -1; }).get() == 25);
    auto failure = pool.submit(
        []() -> int { throw std::runtime_error("candidate failure"); });
    bool propagated = false;
    try {
      failure.get();
    } catch (const std::runtime_error &) {
      propagated = true;
    }
    check(propagated && pool.submit([] { return 19; }).get() == 19);

    std::promise<void> started;
    auto signal = started.get_future();
    std::atomic<bool> finish{false};
    std::atomic<int> steps{0};
    auto running = pool.submit([&] {
      started.set_value();
      while (!finish.load()) {
        pool.check();
        ++steps;
        std::this_thread::yield();
      }
      return 71;
    });
    signal.get();
    pool.pause(true);
    int pausedSteps = steps.load();
    check(pool.parked == 1 && running.wait_for(std::chrono::milliseconds(0)) !=
                                  std::future_status::ready);
    for (int i = 0; i < 100; ++i)
      std::this_thread::yield();
    check(steps == pausedSteps);
    finish = true;
    pool.pause(false);
    check(running.get() == 71);

    std::promise<void> cancellingStarted;
    auto cancellingSignal = cancellingStarted.get_future();
    auto cancelled = pool.submit([&] {
      cancellingStarted.set_value();
      for (;;) {
        pool.check();
        std::this_thread::yield();
      }
      return 0;
    });
    cancellingSignal.get();
    pool.pause(true);
    pool.cancel();
    bool cancelledCorrectly = false;
    try {
      cancelled.get();
    } catch (const Cancelled &) {
      cancelledCorrectly = true;
    }
    check(cancelledCorrectly);
    pool.stop();
    bool stoppedCorrectly = false;
    try {
      pool.submit([] { return 0; });
    } catch (const std::logic_error &) {
      stoppedCorrectly = true;
    }
    check(stoppedCorrectly);
    jobTests();
    std::cout << "PASS: search futures, memoization, exceptions, pause/resume, "
                 "cancellation and shutdown\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
