#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace datapump::execution {
using Clock = std::chrono::steady_clock;
using Context = std::uintptr_t;

// A context identifies one independently scheduled task, including on a host
// where several cooperative tasks share an operating-system thread.
Context current_context() noexcept;
unsigned concurrency() noexcept;

#ifndef DATAPUMP_EXECUTION_FIBERS
using Task = std::jthread;
using Thread = std::thread;
using Mutex = std::mutex;
using Condition = std::condition_variable;
using StopCondition = std::condition_variable_any;
inline void checkpoint() noexcept {}
inline void yield() noexcept { std::this_thread::yield(); }
template<class Rep, class Period>
void sleep_for(std::chrono::duration<Rep, Period> duration) {
    std::this_thread::sleep_for(duration);
}
template<class ClockType, class Duration>
void sleep_until(std::chrono::time_point<ClockType, Duration> deadline) {
    std::this_thread::sleep_until(deadline);
}
// Native tasks run independently; cooperative hosts call pump from their loop.
inline std::size_t pump(std::chrono::nanoseconds = std::chrono::milliseconds(4)) { return 0; }
#else
namespace detail {
struct TaskState;
struct MutexState;
struct ConditionState;
using Action = std::function<void(std::stop_token)>;
std::shared_ptr<TaskState> start(Action);
void join(const std::shared_ptr<TaskState>&);
bool request_stop(const std::shared_ptr<TaskState>&) noexcept;
std::stop_token stop_token(const std::shared_ptr<TaskState>&) noexcept;

template<bool WithStop = true, class Function, class... Args>
Action action(Function&& function, Args&&... args) {
    // The shared closure permits move-only user callables while the scheduler
    // owns a copyable, type-erased entry point.
    auto bound = std::make_shared<std::tuple<std::decay_t<Function>, std::decay_t<Args>...>>(
        std::forward<Function>(function), std::forward<Args>(args)...);
    return [bound](std::stop_token stop) mutable {
        std::apply([&](auto& callable, auto&... values) {
            if constexpr (WithStop && std::is_invocable_v<decltype(std::move(callable)), std::stop_token, decltype(std::move(values))...>)
                std::invoke(std::move(callable), stop, std::move(values)...);
            else
                std::invoke(std::move(callable), std::move(values)...);
        }, *bound);
    };
}
}

class Task {
public:
    Task() noexcept = default;
    template<class Function, class... Args>
        requires (!std::is_same_v<std::remove_cvref_t<Function>, Task>)
    explicit Task(Function&& function, Args&&... args)
        : state_(detail::start(detail::action(std::forward<Function>(function), std::forward<Args>(args)...))) {}
    Task(Task&&) noexcept = default;
    Task& operator=(Task&&) noexcept;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task();
    bool joinable() const noexcept { return bool(state_); }
    void join();
    bool request_stop() noexcept { return detail::request_stop(state_); }
    std::stop_token get_stop_token() const noexcept { return detail::stop_token(state_); }
private:
    std::shared_ptr<detail::TaskState> state_;
};

// Unlike Task, Thread retains std::thread's explicit-join requirement.
class Thread {
public:
    Thread() noexcept = default;
    template<class Function, class... Args>
        requires (!std::is_same_v<std::remove_cvref_t<Function>, Thread>)
    explicit Thread(Function&& function, Args&&... args)
        : state_(detail::start(detail::action<false>(std::forward<Function>(function), std::forward<Args>(args)...))) {}
    Thread(Thread&&) noexcept = default;
    Thread& operator=(Thread&&) noexcept;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    ~Thread();
    bool joinable() const noexcept { return bool(state_); }
    void join();
private:
    std::shared_ptr<detail::TaskState> state_;
};

class Mutex {
public:
    Mutex();
    ~Mutex();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void lock();
    bool try_lock();
    void unlock();
private:
    std::unique_ptr<detail::MutexState> state_;
};

class Condition {
public:
    Condition();
    ~Condition();
    Condition(const Condition&) = delete;
    Condition& operator=(const Condition&) = delete;
    void notify_one() noexcept;
    void notify_all() noexcept;
    void wait(std::unique_lock<Mutex>& lock);
    template<class Predicate>
    void wait(std::unique_lock<Mutex>& lock, Predicate predicate) {
        while (!predicate()) wait(lock);
    }
    template<class Predicate>
    bool wait(std::unique_lock<Mutex>& lock, std::stop_token stop, Predicate predicate) {
        std::stop_callback wake(stop, [this] { notify_all(); });
        while (!predicate()) {
            if (stop.stop_requested()) return false;
            wait(lock);
        }
        return true;
    }
    template<class ClockType, class Duration>
    std::cv_status wait_until(std::unique_lock<Mutex>& lock,
                              std::chrono::time_point<ClockType, Duration> deadline) {
        const auto remaining = deadline - ClockType::now();
        if (remaining <= remaining.zero()) return std::cv_status::timeout;
        return wait_until_steady(lock, Clock::now() + std::chrono::ceil<Clock::duration>(remaining));
    }
    template<class Rep, class Period>
    std::cv_status wait_for(std::unique_lock<Mutex>& lock, std::chrono::duration<Rep, Period> duration) {
        return wait_until(lock, Clock::now() + std::chrono::ceil<Clock::duration>(duration));
    }
    template<class ClockType, class Duration, class Predicate>
    bool wait_until(std::unique_lock<Mutex>& lock, std::chrono::time_point<ClockType, Duration> deadline,
                    Predicate predicate) {
        while (!predicate()) if (wait_until(lock, deadline) == std::cv_status::timeout) return predicate();
        return true;
    }
    template<class ClockType, class Duration, class Predicate>
    bool wait_until(std::unique_lock<Mutex>& lock, std::stop_token stop,
                    std::chrono::time_point<ClockType, Duration> deadline, Predicate predicate) {
        std::stop_callback wake(stop, [this] { notify_all(); });
        while (!predicate()) {
            if (stop.stop_requested()) return false;
            if (wait_until(lock, deadline) == std::cv_status::timeout) return predicate();
        }
        return true;
    }
    template<class Rep, class Period, class Predicate>
    bool wait_for(std::unique_lock<Mutex>& lock, std::chrono::duration<Rep, Period> duration,
                  Predicate predicate) {
        return wait_until(lock, Clock::now() + std::chrono::ceil<Clock::duration>(duration), std::move(predicate));
    }
    template<class Rep, class Period, class Predicate>
    bool wait_for(std::unique_lock<Mutex>& lock, std::stop_token stop,
                  std::chrono::duration<Rep, Period> duration, Predicate predicate) {
        return wait_until(lock, stop, Clock::now() + std::chrono::ceil<Clock::duration>(duration), std::move(predicate));
    }
private:
    std::cv_status wait_until_steady(std::unique_lock<Mutex>&, Clock::time_point);
    std::shared_ptr<detail::ConditionState> state_;
};
using StopCondition = Condition;

void checkpoint();
void yield();
void sleep_until(Clock::time_point);
template<class Rep, class Period>
void sleep_for(std::chrono::duration<Rep, Period> duration) {
    if (duration > duration.zero()) sleep_until(Clock::now() + std::chrono::ceil<Clock::duration>(duration));
}
std::size_t pump(std::chrono::nanoseconds budget = std::chrono::milliseconds(4));
#endif
}
