#include "datapump/execution.hpp"
#include <atomic>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;
namespace ex = datapump::execution;
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Predicate>
void until(Predicate predicate) {
    const auto deadline = ex::Clock::now() + 3s;
    while (!predicate()) {
        check(ex::Clock::now() < deadline, "execution test progress deadline");
        ex::pump(2ms);
        ex::sleep_for(1ms);
    }
}
void stop_wait_and_destructor() {
    ex::Mutex mutex;
    ex::StopCondition changed;
    std::atomic<bool> waiting = false, finished = false;
    {
        ex::Task task([&](std::stop_token stop) {
            std::unique_lock lock(mutex);
            waiting = true;
            const bool predicate = changed.wait(lock, stop, [] { return false; });
            check(!predicate && stop.stop_requested() && lock.owns_lock(), "stop-aware wait must reacquire and report cancellation");
            finished = true;
        });
        until([&] { return waiting.load(); });
        // Destructor must request stop and join, including a condition wait.
    }
    check(finished, "Task destructor must join stopped work");
}
void condition_and_mutex() {
    ex::Mutex mutex;
    ex::Condition changed;
    std::vector<unsigned> observed;
    unsigned available = 0;
    bool done = false;
    ex::Thread consumer([&] {
        std::unique_lock lock(mutex);
        for (;;) {
            changed.wait(lock, [&] { return available != observed.size() || done; });
            while (observed.size() < available) observed.push_back(static_cast<unsigned>(observed.size()));
            if (done) return;
        }
    });
    ex::Thread producer([&] {
        for (unsigned i = 1; i <= 12; ++i) {
            {
                std::lock_guard lock(mutex);
                available = i;
                // A cooperative yield while locked must not grant another
                // execution context access to the protected data.
                ex::yield();
                changed.notify_one();
            }
            ex::sleep_for(1ms);
        }
        { std::lock_guard lock(mutex); done = true; }
        changed.notify_all();
    });
    producer.join(); consumer.join();
    check(observed.size() == 12 && observed.front() == 0 && observed.back() == 11, "condition handoff lost ordered data");
    check(!consumer.joinable() && !producer.joinable(), "joined Thread must release its handle");
}
void timeouts_and_cancellation() {
    ex::Mutex mutex;
    ex::StopCondition changed;
    std::atomic<bool> finished = false;
    ex::Task task([&](std::stop_token stop) {
        std::unique_lock lock(mutex);
        const auto start = ex::Clock::now();
        check(!changed.wait_for(lock, stop, 15ms, [] { return false; }), "false predicate cannot satisfy timed wait");
        check(ex::Clock::now() - start >= 15ms, "timed wait completed before its deadline");
        check(!stop.stop_requested(), "timeout is not cancellation");
        const auto before = ex::Clock::now();
        check(changed.wait_for(lock, stop, 2s, [] { return true; }), "true predicate must succeed immediately");
        check(ex::Clock::now() - before < 1s, "true predicate unnecessarily waited");
        finished = true;
    });
    until([&] { return finished.load(); }); task.join();
}
void move_and_nested_join() {
    std::atomic<bool> old_started = false, old_finished = false, replacement_finished = false;
    ex::Task old([&](std::stop_token stop) {
        old_started = true;
        while (!stop.stop_requested()) ex::sleep_for(1ms);
        old_finished = true;
    });
    until([&] { return old_started.load(); });
    ex::Task replacement([owned = std::make_unique<int>(42), &replacement_finished]() mutable {
        check(*owned == 42, "move-only task capture was not preserved");
        ex::Task child([&] { *owned = 43; });
        child.join();
        check(*owned == 43, "nested task join returned before completion");
        replacement_finished = true;
    });
    old = std::move(replacement);
    check(old_finished && !replacement.joinable(), "move assignment must stop/join prior task");
    old.join();
    check(replacement_finished && !old.joinable(), "replacement task must retain its work");
}
void context_and_checkpoint() {
    const auto root = ex::current_context();
    ex::Context first = 0, second = 0;
    std::atomic<bool> entered = false, observed = false;
    ex::Task a([&] {
        first = ex::current_context();
        entered = true;
        const auto deadline = ex::Clock::now() + 2s;
        while (!observed.load() && ex::Clock::now() < deadline) ex::checkpoint();
        check(observed, "checkpoint must permit other work to progress");
        check(ex::current_context() == first, "task identity changed after resumption");
    });
    ex::Task b([&] {
        second = ex::current_context();
        while (!entered.load()) ex::yield();
        observed = true;
    });
    a.join(); b.join();
    check(first && second && first != second && first != root && second != root,
          "concurrent tasks must have distinct execution identities");
    check(ex::current_context() == root, "task completion changed host identity");
}
void suspended_cpp_frames() {
    std::atomic<unsigned> finished=0;
    std::vector<ex::Task> tasks;
    for(unsigned task=0;task<8;++task)tasks.emplace_back([task,&finished](std::stop_token stop) {
        std::array<std::uint64_t,16> retained;
        retained.fill(0xdeadbeef12340000ULL+task);
        const auto owned=std::make_unique<std::string>("resumed C++ frame");
        for(unsigned step=0;step<4;++step) {
            ex::yield();
            for(auto word:retained)check(word==0xdeadbeef12340000ULL+task,"suspension corrupted stack data");
            check(*owned=="resumed C++ frame"&&!stop.stop_requested(),"suspension corrupted object lifetime");
        }
        try {ex::yield();throw std::runtime_error("expected");}
        catch(const std::runtime_error& e) {check(std::string(e.what())=="expected","exception after suspension lost its value");}
        ++finished;
    });
    for(auto& task:tasks)task.join();
    check(finished==8,"all independent suspended C++ frames must finish");
}
}
int main() {
    try {
        // Reused heap positions exercise the fiber ABI's independent C-stack
        // alignment requirement; one isolated task can happen to be aligned.
        for(unsigned repetition=0;repetition<8;++repetition) {
        stop_wait_and_destructor();
        condition_and_mutex();
        timeouts_and_cancellation();
        move_and_nested_join();
        context_and_checkpoint();
        suspended_cpp_frames();
        }
        std::cout << "execution lifecycle, stop/wait, mutex, timing and context tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
