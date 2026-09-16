#include "../src/search_parallel.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#endif

using namespace datapump::modem::detail;
namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A broken serial executor fails in bounded time instead of hanging the test.
class Rendezvous {
public:
    explicit Rendezvous(std::size_t expected) : expected_(expected) {}
    void arrive() {
        std::unique_lock lock(mutex_);
        ++arrived_;
        changed_.notify_all();
        check(changed_.wait_for(lock, std::chrono::seconds(5),
                               [this] { return arrived_ == expected_; }),
              "the requested workers must execute concurrently");
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t arrived_ = 0;
    std::size_t expected_;
};

void concurrency_defaults() {
    auto available = std::max<std::size_t>(1, std::thread::hardware_concurrency());
#if defined(__linux__)
    cpu_set_t affinity;
    if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0 && CPU_COUNT(&affinity) > 0)
        available = static_cast<std::size_t>(CPU_COUNT(&affinity));
#endif
    check(search_concurrency() == std::max<std::size_t>(1, available - 1),
          "automatic search concurrency must leave one available CPU free");
    check(search_concurrency(1) == 1, "one requested worker must stay serial");
    check(search_concurrency(std::numeric_limits<std::size_t>::max()) == available,
          "explicit worker requests must be capped by available CPUs");
}

void growing_pool() {
    for (const auto requested : {2U, 3U, 4U, 0U, 2U}) {
        const auto concurrency = search_concurrency(requested);
        Rendezvous start(concurrency);
        std::atomic<std::size_t> visited{0};
        parallel_search(concurrency, concurrency, [&](std::size_t worker, std::size_t) {
            check(worker < concurrency, "idle pool workers cannot use slots from an earlier batch");
            start.arrive();
            ++visited;
        });
        check(visited.load() == concurrency, "growing and shrinking batches must finish every worker");
    }
}

void coverage_and_reuse() {
    const auto concurrency = search_concurrency(4);
    std::vector<std::thread::id> first(concurrency), second(concurrency);
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        Rendezvous start(concurrency);
        std::vector<std::atomic<unsigned>> visits(1031);
        std::vector<std::atomic<bool>> occupied(concurrency);
        auto& threads = repeat == 0 ? first : second;
        parallel_search(visits.size(), concurrency, [&](std::size_t worker, std::size_t index) {
            check(worker < concurrency, "worker slot must fit the requested scratch space");
            check(!occupied[worker].exchange(true), "a worker slot cannot be shared concurrently");
            if (index < concurrency) {
                threads[worker] = std::this_thread::get_id();
                start.arrive();
            }
            ++visits[index];
            occupied[worker].store(false);
        });
        for (const auto& visits_for_index : visits)
            check(visits_for_index.load() == 1, "each candidate must run exactly once");
        for (const auto thread : threads)
            check(thread != std::thread::id{}, "each requested worker must participate");
    }
    check(first == second, "successive batches must reuse their persistent worker threads");
    check(first[0] == std::this_thread::get_id(), "the caller must participate as worker zero");

    std::size_t serial_visits = 0;
    parallel_search(17, 1, [&](std::size_t worker, std::size_t index) {
        check(worker == 0 && index == serial_visits, "serial work must preserve index order");
        check(std::this_thread::get_id() == first[0], "serial work must stay on the calling thread");
        ++serial_visits;
    });
    parallel_search(0, concurrency, [](auto, auto) { throw std::runtime_error("empty batch ran work"); });
    parallel_search(1, concurrency, [](auto worker, auto index) {
        check(worker == 0 && index == 0, "a singleton batch must use slot zero");
    });
}

void exceptions_join_and_reuse() {
    const auto concurrency = search_concurrency(4);
    Rendezvous start(concurrency);
    std::vector<std::atomic<unsigned>> visits(29);
    std::atomic<unsigned> active{0};
    bool caught = false;
    try {
        parallel_search(visits.size(), concurrency, [&](std::size_t, std::size_t index) {
            ++active;
            ++visits[index];
            if (index < concurrency) start.arrive();
            // The lowest index fails last; scheduling must not select the first
            // exception to arrive as the visible outcome.
            if (index == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            --active;
            if (index < 2) throw std::runtime_error(index == 0 ? "first index" : "later index");
        });
    } catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "first index";
    }
    check(caught, "the earliest failing candidate must determine the exception");
    check(active.load() == 0, "exceptions must join every in-flight worker before returning");
    for (const auto& visited : visits)
        check(visited.load() == 1, "an exception must not abandon or duplicate candidates");

    std::atomic<unsigned> completed{0};
    parallel_search(47, concurrency, [&](auto, auto) { ++completed; });
    check(completed.load() == 47, "worker exceptions must leave the pool reusable");
}

void nested_and_concurrent_callers() {
    const auto concurrency = search_concurrency(4);
    Rendezvous start(concurrency);
    std::atomic<unsigned> nested_visits{0};
    parallel_search(concurrency, concurrency, [&](auto, auto) {
        start.arrive();
        const auto outer_thread = std::this_thread::get_id();
        parallel_search(13, concurrency, [&](std::size_t worker, std::size_t) {
            check(worker == 0 && std::this_thread::get_id() == outer_thread,
                  "nested work must execute serially on its current worker");
            ++nested_visits;
        });
    });
    check(nested_visits.load() == 13 * concurrency, "nested searches must visit every candidate");

    std::atomic<unsigned> active{0}, maximum{0}, completed{0};
    std::atomic<bool> failed{false};
    Rendezvous callers_ready(5);
    std::vector<std::thread> callers;
    for (unsigned caller = 0; caller < 5; ++caller) {
        callers.emplace_back([&] {
            try {
                callers_ready.arrive();
                parallel_search(19, concurrency, [&](auto, auto) {
                    const auto current = ++active;
                    auto previous = maximum.load();
                    while (current > previous && !maximum.compare_exchange_weak(previous, current)) {}
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    ++completed;
                    --active;
                });
            } catch (...) {
                failed.store(true);
            }
        });
    }
    for (auto& caller : callers) caller.join();
    check(!failed.load(), "concurrent callers must complete successfully");
    check(completed.load() == 5 * 19 && active.load() == 0,
          "concurrent batches must finish all their work before returning");
    check(maximum.load() <= concurrency, "concurrent callers must share the worker budget");
}

} // namespace

int main() {
    try {
        concurrency_defaults();
        growing_pool();
        coverage_and_reuse();
        exceptions_join_and_reuse();
        nested_and_concurrent_callers();
        std::cout << "search_parallel ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
