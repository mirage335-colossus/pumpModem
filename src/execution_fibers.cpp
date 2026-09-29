#include "datapump/execution.hpp"
#include <emscripten.h>
#include <emscripten/fiber.h>
#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace datapump::execution {
namespace detail {
namespace {
// Both stacks are bounded per task. This provisional host budget is separate
// from modem/receive quotas; the browser product must qualify their aggregate.
constexpr std::size_t c_stack_bytes = 1024U * 1024U;
constexpr std::size_t async_stack_bytes = 256U * 1024U;
constexpr std::size_t task_limit = 32;
constexpr auto quantum = std::chrono::milliseconds(2);
}
struct TaskState {
    enum class State { ready, running, waiting, finished };
    emscripten_fiber_t fiber{};
    std::vector<unsigned char> c_stack;
    std::vector<unsigned char> async_stack;
    Action action;
    std::stop_source stop;
    State state = State::ready;
    std::function<bool()> wake;
    explicit TaskState(Action entry)
        : c_stack(c_stack_bytes + 15), async_stack(async_stack_bytes), action(std::move(entry)) {}
};
struct MutexState { Context owner = 0; };
struct ConditionState { std::uint64_t generation = 0; };

namespace {
struct Scheduler {
    emscripten_fiber_t root{};
    std::vector<unsigned char> root_async_stack = std::vector<unsigned char>(async_stack_bytes);
    std::vector<std::shared_ptr<TaskState>> tasks;
    TaskState* current = nullptr;
    Clock::time_point slice_end{};
    std::size_t next = 0;
    bool pumping = false;
};
Scheduler& scheduler() {
    static Scheduler state;
    return state;
}
void suspend() {
    auto& s = scheduler();
    if (!s.current) throw std::logic_error("No running cooperative task");
    auto* task = s.current;
    emscripten_fiber_swap(&task->fiber, &s.root);
}
void park(std::function<bool()> ready) {
    if (ready()) return;
    auto& s = scheduler();
    if (s.current) {
        s.current->wake = std::move(ready);
        s.current->state = TaskState::State::waiting;
        suspend();
    } else {
        // Root joins/sleeps are asynchronous too. The host must await a
        // suspending exported operation and must not reenter its Wasm stack.
        while (!ready()) {
            pump();
            if (!ready()) emscripten_sleep(1);
        }
    }
}
void task_entry(void* opaque) {
    auto* task = static_cast<TaskState*>(opaque);
    try { task->action(task->stop.get_token()); }
    catch (...) { std::terminate(); } // Same uncaught-exception policy as std::thread.
    task->action = {};
    task->state = TaskState::State::finished;
    suspend();
    // Returning from an Emscripten fiber would terminate the whole program.
    std::terminate();
}
}

std::shared_ptr<TaskState> start(Action action) {
    auto& s = scheduler();
    std::erase_if(s.tasks, [](const auto& task) { return task->state == TaskState::State::finished; });
    if (s.tasks.size() >= task_limit)
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again), "Cooperative task limit");
    auto task = std::make_shared<TaskState>(std::move(action));
    // The fiber ABI requires 16-byte C-stack alignment. max_align_t and malloc
    // only guarantee 8 on wasm32, so align explicitly inside the allocation.
    const auto stack = (reinterpret_cast<std::uintptr_t>(task->c_stack.data()) + 15) & ~std::uintptr_t(15);
    emscripten_fiber_init(&task->fiber, task_entry, task.get(), reinterpret_cast<void*>(stack), c_stack_bytes,
                          task->async_stack.data(), task->async_stack.size());
    s.tasks.push_back(task);
    return task;
}
void join(const std::shared_ptr<TaskState>& task) {
    if (!task) throw std::system_error(std::make_error_code(std::errc::invalid_argument), "Task is not joinable");
    if (scheduler().current == task.get())
        throw std::system_error(std::make_error_code(std::errc::resource_deadlock_would_occur), "Cannot join current task");
    park([task] { return task->state == TaskState::State::finished; });
}
bool request_stop(const std::shared_ptr<TaskState>& task) noexcept { return task && task->stop.request_stop(); }
std::stop_token stop_token(const std::shared_ptr<TaskState>& task) noexcept {
    return task ? task->stop.get_token() : std::stop_token{};
}
}

Context current_context() noexcept {
    auto& s = detail::scheduler();
    return reinterpret_cast<Context>(s.current ? static_cast<void*>(s.current) : static_cast<void*>(&s));
}
unsigned concurrency() noexcept { return 1; }

Task::~Task() { if (joinable()) { request_stop(); join(); } }
Task& Task::operator=(Task&& other) noexcept {
    if (this != &other) {
        if (joinable()) { request_stop(); join(); }
        state_ = std::move(other.state_);
    }
    return *this;
}
void Task::join() { detail::join(state_); state_.reset(); }
Thread::~Thread() { if (joinable()) std::terminate(); }
Thread& Thread::operator=(Thread&& other) noexcept {
    if (joinable()) std::terminate();
    state_ = std::move(other.state_);
    return *this;
}
void Thread::join() { detail::join(state_); state_.reset(); }

Mutex::Mutex() : state_(std::make_unique<detail::MutexState>()) {}
Mutex::~Mutex() = default;
void Mutex::lock() {
    const auto caller = current_context();
    if (state_->owner == caller)
        throw std::system_error(std::make_error_code(std::errc::resource_deadlock_would_occur), "Recursive mutex lock");
    detail::park([this] { return state_->owner == 0; });
    state_->owner = caller;
}
bool Mutex::try_lock() {
    if (state_->owner) return false;
    state_->owner = current_context();
    return true;
}
void Mutex::unlock() {
    if (state_->owner != current_context())
        throw std::system_error(std::make_error_code(std::errc::operation_not_permitted), "Mutex belongs to another task");
    state_->owner = 0;
}

Condition::Condition() : state_(std::make_shared<detail::ConditionState>()) {}
Condition::~Condition() = default;
void Condition::notify_one() noexcept { notify_all(); } // Additional wakes are permitted to be spurious.
void Condition::notify_all() noexcept { ++state_->generation; }
void Condition::wait(std::unique_lock<Mutex>& lock) { (void)wait_until_steady(lock, Clock::time_point::max()); }
std::cv_status Condition::wait_until_steady(std::unique_lock<Mutex>& lock, Clock::time_point deadline) {
    if (!lock.owns_lock()) throw std::logic_error("Condition wait requires an owned lock");
    const auto state = state_;
    const auto generation = state->generation;
    lock.unlock();
    try {
        detail::park([state, generation, deadline] {
            return state->generation != generation || Clock::now() >= deadline;
        });
    } catch (...) { lock.lock(); throw; }
    lock.lock();
    return Clock::now() >= deadline ? std::cv_status::timeout : std::cv_status::no_timeout;
}

void yield() {
    auto& s = detail::scheduler();
    if (!s.current) { pump(); emscripten_sleep(0); return; }
    s.current->state = detail::TaskState::State::ready;
    detail::suspend();
}
void checkpoint() {
    const auto& s = detail::scheduler();
    if (s.current && Clock::now() >= s.slice_end) yield();
}
void sleep_until(Clock::time_point deadline) { detail::park([deadline] { return Clock::now() >= deadline; }); }

std::size_t pump(std::chrono::nanoseconds budget) {
    auto& s = detail::scheduler();
    if (s.pumping || s.current) throw std::logic_error("Cooperative scheduler cannot be reentered");
    if (budget <= budget.zero()) return 0;
    const auto deadline = Clock::now() + std::chrono::ceil<Clock::duration>(budget);
    struct Guard { bool& value; ~Guard() { value = false; } } guard{s.pumping};
    s.pumping = true;
    emscripten_fiber_init_from_current_context(&s.root, s.root_async_stack.data(), s.root_async_stack.size());
    std::size_t switches = 0;
    while (Clock::now() < deadline) {
        std::shared_ptr<detail::TaskState> next;
        const auto count = s.tasks.size();
        for (std::size_t scanned = 0; scanned < count; ++scanned) {
            if (s.next >= s.tasks.size()) s.next = 0;
            auto task = s.tasks[s.next++];
            if (task->state == detail::TaskState::State::waiting && task->wake()) {
                task->wake = {};
                task->state = detail::TaskState::State::ready;
            }
            if (task->state == detail::TaskState::State::ready) { next = std::move(task); break; }
        }
        if (!next) break;
        s.current = next.get();
        next->state = detail::TaskState::State::running;
        s.slice_end = std::min(deadline, Clock::now() + detail::quantum);
        ++switches;
        emscripten_fiber_swap(&s.root, &next->fiber);
        s.current = nullptr;
    }
    std::erase_if(s.tasks, [](const auto& task) { return task->state == detail::TaskState::State::finished; });
    return switches;
}
}
