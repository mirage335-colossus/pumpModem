#include "triad_receiver_executor.hpp"

#include "triad_receiver_detail.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <optional>

namespace datapump::triad::v3::receiver_detail {
namespace {
constexpr auto unbounded = std::numeric_limits<std::size_t>::max();
}

struct ReceiverExecutor::Batch {
    struct Cancel {
        std::stop_source& stop;
        void operator()() const noexcept { stop.request_stop(); }
    };

    Batch(std::uint64_t public_slot, unsigned lanes, Leaf urgent, Leaf background)
        : public_slot(public_slot), urgent(std::move(urgent)), background(std::move(background)),
          occupied(lanes - 1), flight(lanes, unbounded) {}

    std::size_t claimable() const {
        return (urgent_limit - urgent_next) + (background_limit - background_next);
    }
    bool has_free_lane() const { return running != occupied.size(); }

    const std::uint64_t public_slot;
    const Leaf urgent, background;
    // Helper lanes one and above. The owner alone uses lane zero.
    std::vector<std::uint8_t> occupied;
    // The background leaf running on each lane, if any.
    std::vector<std::size_t> flight;
    std::size_t urgent_next = 0, urgent_limit = 0, background_next = 0, background_limit = 0;
    unsigned running = 0, urgent_running = 0;
    bool owner_waiting = false;
    std::stop_source stop;
    std::exception_ptr failure;
    execution::Condition done;
    std::optional<std::stop_callback<Cancel>> cancel;
};

ReceiverExecutor::~ReceiverExecutor() { close(); }

void ReceiverExecutor::grow() {
    // close() joins the pool without this lock. A stage that is still open
    // keeps its owner's lane and must not add a worker behind that join.
    if (closing_) return;
    auto demand = static_cast<std::size_t>(running_roots_) + roots_.size();
    for (const auto* batch : batches_) {
        if (demand >= capacity_) break;
        demand += std::min(batch->occupied.size(), batch->claimable() + batch->running);
    }
    demand = std::min<std::size_t>(capacity_, demand);
    while (workers_.size() < demand) {
        workers_.emplace_back([this] { run(); });
        const auto count = static_cast<unsigned>(workers_.size());
        started_workers_ = count;
        peak_workers_ = count;
    }
}

void ReceiverExecutor::wake(std::size_t leaves) {
    // Busy workers rescan after every leaf. Only sleepers need a signal, and
    // no more of them than the new work can occupy.
    if (!idle_workers_ || !leaves) return;
    if (leaves >= idle_workers_) ready_.notify_all();
    else for (; leaves; --leaves) ready_.notify_one();
}

void ReceiverExecutor::submit(std::function<void()> score, std::function<void()> publish) {
    std::lock_guard lock(mutex_);
    if (closing_ || roots_.size() >= capacity_)
        throw Error("Robust receiver executor cannot admit another root");
    roots_.push_back({std::move(score), std::move(publish)});
    try {
        grow();
    } catch (...) {
        roots_.pop_back();
        throw;
    }
    wake(1);
}

void ReceiverExecutor::close() {
    {
        std::lock_guard lock(mutex_);
        if (closing_) return;
        closing_ = true;
        roots_.clear();
    }
    ready_.notify_all();
    for (auto& worker : workers_) worker.join();
    std::lock_guard lock(mutex_);
    workers_.clear();
    started_workers_ = 0;
}

bool ReceiverExecutor::claim(Batch& batch, std::size_t background_limit, Claim& claimed) {
    if (batch.stop.stop_requested()) return false;
    if (batch.urgent_next != batch.urgent_limit) {
        claimed = {false, batch.urgent_next++};
        ++batch.urgent_running;
        return true;
    }
    if (batch.background_next < std::min(batch.background_limit, background_limit)) {
        claimed = {true, batch.background_next++};
        return true;
    }
    return false;
}

void ReceiverExecutor::execute(Batch& batch, unsigned lane, Claim claimed) {
    try {
        const auto stop = batch.stop.get_token();
        if (!stop.stop_requested()) (claimed.background ? batch.background : batch.urgent)(lane, claimed.index, stop);
    } catch (...) {
        // An earlier failure or cancellation already owns the outcome. The
        // owner reads the failure under this lock once stop is visible.
        if (batch.stop.stop_requested()) return;
        {
            std::lock_guard lock(mutex_);
            if (batch.failure) return;
            batch.failure = std::current_exception();
        }
        batch.stop.request_stop();
    }
}

void ReceiverExecutor::check(Batch& batch) {
    if (batch.failure) std::rethrow_exception(batch.failure);
    if (batch.stop.stop_requested()) throw Error("Robust reception cancelled");
}

bool ReceiverExecutor::help(std::unique_lock<execution::Mutex>& lock) {
    Batch* left = nullptr;
    for (bool worked = false;; worked = true) {
        Batch* chosen = nullptr;
        Claim claimed{};
        // The oldest eligible public slot can advance ordered commit. Younger
        // batches still use workers whenever it has no available helper work.
        if (!closing_) {
            for (auto* candidate : batches_) {
                if (candidate->has_free_lane() && claim(*candidate, unbounded, claimed)) {
                    chosen = candidate;
                    break;
                }
            }
        }
        // This worker frees a lane that a sleeper could use for remaining work.
        if (left && left != chosen && idle_workers_ && left->has_free_lane() &&
            left->claimable() && !left->stop.stop_requested())
            ready_.notify_one();
        if (!chosen) return worked;
        auto& batch = *chosen;
        const auto free = std::ranges::find(batch.occupied, 0);
        const auto lane = static_cast<unsigned>(free - batch.occupied.begin());
        *free = 1;
        ++batch.running;
        if (claimed.background) batch.flight[lane + 1] = claimed.index;
        lock.unlock();
        execute(batch, lane + 1, claimed);
        lock.lock();
        // The owner cannot retire this batch before the running count drops.
        batch.occupied[lane] = 0;
        --batch.running;
        if (claimed.background) batch.flight[lane + 1] = unbounded;
        else --batch.urgent_running;
        if (batch.owner_waiting) batch.done.notify_one();
        left = chosen;
    }
}

void ReceiverExecutor::run() {
    std::unique_lock lock(mutex_);
    while (!closing_) {
        if (help(lock)) continue;
        if (!roots_.empty()) {
            auto root = std::move(roots_.front());
            roots_.pop_front();
            ++running_roots_;
            if (running_roots_ > peak_running_roots_.load()) peak_running_roots_ = running_roots_;
            lock.unlock();
            root.score();
            lock.lock();
            --running_roots_;
            // The controller may release this root's prospective lease as
            // soon as its result is visible. Release its demand first.
            lock.unlock();
            root.publish();
            lock.lock();
        } else {
            ++idle_workers_;
            ready_.wait(lock);
            --idle_workers_;
        }
    }
}

ReceiverExecutor::Stage::Stage(ReceiverExecutor& executor, std::uint64_t public_slot, unsigned lane_limit,
                               std::stop_token stop, Leaf urgent, Leaf background)
    : executor_(executor) {
    check_cancelled(stop);
    const auto lanes = std::max(1U, std::min(lane_limit, executor.capacity_));
    batch_ = std::make_unique<Batch>(public_slot, lanes, std::move(urgent), std::move(background));
    batch_->cancel.emplace(stop, Batch::Cancel{batch_->stop});
    std::lock_guard lock(executor.mutex_);
    if (executor.closing_) throw Error("Robust receiver executor closed during scoring");
    const auto spot = std::upper_bound(executor.batches_.begin(), executor.batches_.end(), public_slot,
        [](std::uint64_t slot, const Batch* queued) { return slot < queued->public_slot; });
    executor.batches_.insert(spot, batch_.get());
}

ReceiverExecutor::Stage::~Stage() {
    auto& batch = *batch_;
    // Unclaimed leaves never start once their owner leaves the stage. Only
    // running helpers can keep the owner's borrowed state alive past here.
    batch.stop.request_stop();
    std::unique_lock lock(executor_.mutex_);
    batch.owner_waiting = true;
    batch.done.wait(lock, [&] { return !batch.running; });
    executor_.batches_.erase(std::ranges::find(executor_.batches_, &batch));
}

unsigned ReceiverExecutor::Stage::lanes() const noexcept {
    return static_cast<unsigned>(batch_->flight.size());
}

std::stop_token ReceiverExecutor::Stage::stop_token() const noexcept {
    return batch_->stop.get_token();
}

void ReceiverExecutor::Stage::post(std::size_t count) {
    auto& batch = *batch_;
    std::lock_guard lock(executor_.mutex_);
    if (count > unbounded - batch.urgent_limit) throw Error("Robust receiver stage leaf count overflow");
    batch.urgent_limit += count;
    try {
        executor_.grow();
    } catch (...) {
        // This lock was held throughout, so no lane has seen these leaves.
        // Their owner unwinds through state that they would borrow.
        batch.urgent_limit -= count;
        throw;
    }
    executor_.wake(count);
}

void ReceiverExecutor::Stage::release(std::size_t limit) {
    auto& batch = *batch_;
    std::lock_guard lock(executor_.mutex_);
    if (limit <= batch.background_limit) return;
    const auto added = limit - batch.background_limit;
    batch.background_limit = limit;
    try {
        executor_.grow();
    } catch (...) {
        batch.background_limit = limit - added;
        throw;
    }
    executor_.wake(added);
    if (batch.owner_waiting) batch.done.notify_one();
}

void ReceiverExecutor::Stage::wait_urgent() {
    auto& batch = *batch_;
    std::unique_lock lock(executor_.mutex_);
    for (Claim claimed{};;) {
        if (claim(batch, 0, claimed)) {
            lock.unlock();
            executor_.execute(batch, 0, claimed);
            lock.lock();
            --batch.urgent_running;
            continue;
        }
        // All unclaimed urgent work is either processed or cancelled.
        if (!batch.urgent_running) break;
        batch.owner_waiting = true;
        batch.done.wait(lock);
        batch.owner_waiting = false;
    }
    check(batch);
}

void ReceiverExecutor::Stage::wait_background(std::size_t limit, std::size_t first) {
    auto& batch = *batch_;
    std::unique_lock lock(executor_.mutex_);
    if (limit > batch.background_limit || first > limit)
        throw Error("Robust receiver stage waits for unreleased work");
    for (Claim claimed{};;) {
        if (claim(batch, limit, claimed)) {
            if (claimed.background) batch.flight[0] = claimed.index;
            lock.unlock();
            executor_.execute(batch, 0, claimed);
            lock.lock();
            if (claimed.background) batch.flight[0] = unbounded;
            else --batch.urgent_running;
            continue;
        }
        const bool claimed_all = batch.background_next >= limit || batch.stop.stop_requested();
        if (claimed_all && std::ranges::none_of(batch.flight,
                [&](auto index) { return index >= first && index < limit; })) break;
        batch.owner_waiting = true;
        batch.done.wait(lock);
        batch.owner_waiting = false;
    }
    check(batch);
}

LaneWave lane_wave(ReceiverExecutor& executor, std::uint64_t public_slot, unsigned lane_limit, std::stop_token stop) {
    return [&executor, public_slot, lane_limit, stop](std::size_t count, const LaneJob& job) {
        executor.parallel(public_slot, count, lane_limit, stop,
            [&job](unsigned, std::size_t index, std::stop_token job_stop) { job(index, job_stop); });
    };
}

void ReceiverExecutor::parallel(std::uint64_t public_slot, std::size_t count, unsigned lane_limit,
                                std::stop_token stop, const Leaf& leaf) {
    const auto lanes = std::min<std::size_t>(count, std::min(lane_limit, capacity_));
    if (lanes <= 1) {
        for (std::size_t i = 0; i < count; ++i) {
            check_cancelled(stop);
            leaf(0, i, stop);
        }
        check_cancelled(stop);
        return;
    }

    Stage stage(*this, public_slot, static_cast<unsigned>(lanes), stop,
        [&leaf](unsigned lane, std::size_t index, std::stop_token token) { leaf(lane, index, token); });
    stage.post(count);
    stage.wait_urgent();
}

} // namespace datapump::triad::v3::receiver_detail
