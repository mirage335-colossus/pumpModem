#include "search_parallel.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#endif

namespace datapump::modem::detail {
namespace {

std::size_t available_cpus() {
#if defined(__linux__)
    cpu_set_t affinity;
    if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0) {
        const auto count = CPU_COUNT(&affinity);
        if (count > 0) return static_cast<std::size_t>(count);
    }
#endif
    return std::max<std::size_t>(1, std::thread::hardware_concurrency());
}

thread_local bool executing_search = false;

class ExecutionScope {
public:
    ExecutionScope() : previous_(executing_search) { executing_search = true; }
    ~ExecutionScope() { executing_search = previous_; }
private:
    bool previous_;
};

using RangeWork = std::function<void(std::size_t, std::size_t, std::size_t)>;

void serial_search_ranges(std::size_t count, std::size_t grain, const RangeWork& work) {
    ExecutionScope scope;
    std::exception_ptr error;
    for (std::size_t begin = 0; begin < count;) {
        const auto end = begin + std::min(grain, count - begin);
        try {
            work(0, begin, end);
        } catch (...) {
            if (!error) error = std::current_exception();
        }
        begin = end;
    }
    if (error) std::rethrow_exception(error);
}

class SearchPool {
public:
    ~SearchPool() {
        {
            std::lock_guard lock(state_mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        for (auto& worker : workers_) worker.join();
    }

    void run(std::size_t count, std::size_t concurrency, std::size_t grain,
             const RangeWork& work) {
        // Include serial submissions in admission so unrelated searches cannot
        // each recruit a full set of workers or compete with an admitted batch.
        std::unique_lock admission(submission_mutex_);
        if (concurrency == 1) {
            serial_search_ranges(count, grain, work);
            return;
        }
        ensure_workers(concurrency - 1);
        {
            std::lock_guard lock(state_mutex_);
            work_ = &work;
            count_ = count;
            grain_ = grain;
            concurrency_ = concurrency;
            remaining_ = concurrency - 1;
            next_.store(0, std::memory_order_relaxed);
            error_ = nullptr;
            error_index_ = std::numeric_limits<std::size_t>::max();
            ++generation_;
        }
        changed_.notify_all();
        execute(0);
        {
            std::unique_lock lock(state_mutex_);
            finished_.wait(lock, [this] { return remaining_ == 0; });
            work_ = nullptr;
        }
        if (error_) std::rethrow_exception(error_);
    }

private:
    void ensure_workers(std::size_t count) {
        workers_.reserve(count);
        // Workers added after earlier batches must observe only the next batch,
        // even if their thread starts after that batch has already been released.
        while (workers_.size() < count) {
            const auto slot = workers_.size() + 1;
            const auto generation = generation_;
            workers_.emplace_back([this, slot, generation] { worker_loop(slot, generation); });
        }
    }

    void worker_loop(std::size_t slot, std::size_t generation) {
        for (;;) {
            {
                std::unique_lock lock(state_mutex_);
                changed_.wait(lock, [this, generation] { return stopping_ || generation_ != generation; });
                if (stopping_) return;
                generation = generation_;
                if (slot >= concurrency_) continue;
            }
            execute(slot);
            {
                std::lock_guard lock(state_mutex_);
                if (--remaining_ == 0) finished_.notify_one();
            }
        }
    }

    void execute(std::size_t slot) {
        ExecutionScope scope;
        for (;;) {
            auto begin = next_.load(std::memory_order_relaxed);
            std::size_t end;
            do {
                if (begin >= count_) return;
                // Subtract before adding: neither count nor grain needs space
                // for an extra sentinel, including when either is SIZE_MAX.
                end = begin + std::min(grain_, count_ - begin);
            } while (!next_.compare_exchange_weak(begin, end, std::memory_order_relaxed));
            try {
                (*work_)(slot, begin, end);
            } catch (...) {
                std::lock_guard lock(error_mutex_);
                if (begin < error_index_) {
                    error_index_ = begin;
                    error_ = std::current_exception();
                }
            }
        }
    }

    std::mutex submission_mutex_;
    std::mutex state_mutex_;
    std::mutex error_mutex_;
    std::condition_variable changed_;
    std::condition_variable finished_;
    std::vector<std::thread> workers_;
    const RangeWork* work_ = nullptr;
    std::size_t count_ = 0;
    std::size_t grain_ = 1;
    std::size_t concurrency_ = 0;
    std::size_t remaining_ = 0;
    std::size_t generation_ = 0;
    std::atomic<std::size_t> next_{0};
    std::exception_ptr error_;
    std::size_t error_index_ = 0;
    bool stopping_ = false;
};

} // namespace

std::size_t search_concurrency(std::size_t requested) {
    const auto available = available_cpus();
    return requested == 0 ? std::max<std::size_t>(1, available - 1)
                          : std::min(requested, available);
}

void parallel_search(std::size_t count, std::size_t concurrency,
                     const std::function<void(std::size_t, std::size_t)>& work) {
    parallel_search_ranges(count, concurrency, 1,
        [&work](std::size_t worker, std::size_t begin, std::size_t) { work(worker, begin); });
}

void parallel_search_ranges(std::size_t count, std::size_t concurrency,
                            std::size_t grain, const RangeWork& work) {
    if (grain == 0) throw std::invalid_argument("search range grain must be nonzero");
    if (count == 0) return;
    if (executing_search) {
        serial_search_ranges(count, grain, work);
        return;
    }
    const auto ranges = count / grain + static_cast<std::size_t>(count % grain != 0);
    static SearchPool pool;
    pool.run(count, std::min(ranges, search_concurrency(concurrency)), grain, work);
}

} // namespace datapump::modem::detail
