#include "triad_receiver_executor.hpp"

#include "triad_receiver_detail.hpp"

#include <algorithm>
#include <exception>

namespace datapump::triad::v3::receiver_detail {

struct ReceiverExecutor::Batch {
    Batch(std::uint64_t public_slot, std::size_t count, unsigned helpers, const Leaf& leaf)
        : public_slot(public_slot), count(count), leaf(leaf), occupied(helpers) {}

    const std::uint64_t public_slot;
    const std::size_t count;
    const Leaf& leaf;
    std::vector<std::uint8_t> occupied;
    std::size_t next = 0;
    unsigned running = 0;
    std::stop_source stop;
    std::exception_ptr failure;
    execution::Condition done;
};

ReceiverExecutor::~ReceiverExecutor() { close(); }

void ReceiverExecutor::grow() {
    auto demand = static_cast<std::size_t>(running_roots_) + roots_.size();
    for (const auto* batch : batches_) {
        if (demand >= capacity_) break;
        demand += std::min(batch->occupied.size(), batch->count - batch->next + batch->running);
    }
    demand = std::min<std::size_t>(capacity_, demand);
    while (workers_.size() < demand) {
        workers_.emplace_back([this] { run(); });
        const auto count = static_cast<unsigned>(workers_.size());
        started_workers_ = count;
        peak_workers_ = count;
    }
}

void ReceiverExecutor::submit(std::function<void()> score, std::function<void()> publish) {
    {
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
    }
    ready_.notify_all();
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

void ReceiverExecutor::execute(Batch& batch, unsigned lane, std::size_t index) {
    try {
        const auto stop = batch.stop.get_token();
        if (!stop.stop_requested()) batch.leaf(lane, index, stop);
    } catch (...) {
        if (batch.stop.request_stop()) batch.failure = std::current_exception();
    }
}

bool ReceiverExecutor::help(std::unique_lock<execution::Mutex>& lock) {
    for (auto* candidate : batches_) {
        auto& batch = *candidate;
        if (batch.next == batch.count || batch.stop.stop_requested() || batch.running == batch.occupied.size())
            continue;
        const auto free = std::ranges::find(batch.occupied, 0);
        const auto lane = static_cast<unsigned>(free - batch.occupied.begin());
        *free = 1;
        ++batch.running;
        const auto index = batch.next++;
        lock.unlock();
        execute(batch, lane + 1, index);
        lock.lock();
        batch.occupied[lane] = 0;
        if (!--batch.running) batch.done.notify_one();
        return true;
    }
    return false;
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
            ready_.wait(lock);
        }
    }
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

    check_cancelled(stop);
    Batch batch(public_slot, count, static_cast<unsigned>(lanes - 1), leaf);
    std::stop_callback cancel(stop, [&] { batch.stop.request_stop(); });
    std::unique_lock lock(mutex_);
    if (closing_) throw Error("Robust receiver executor closed during scoring");
    // The oldest eligible public slot can advance ordered commit. Younger
    // batches still use workers whenever it has no available helper work.
    const auto spot = std::upper_bound(batches_.begin(), batches_.end(), public_slot,
        [](std::uint64_t slot, const Batch* queued) { return slot < queued->public_slot; });
    const auto entry = batches_.insert(spot, &batch);
    try {
        grow();
    } catch (...) {
        batches_.erase(entry);
        throw;
    }
    ready_.notify_all();
    while (batch.next != batch.count && !batch.stop.stop_requested()) {
        const auto index = batch.next++;
        lock.unlock();
        execute(batch, 0, index);
        lock.lock();
    }
    // All unclaimed work is either processed or cancelled. Only running
    // helpers can keep this stack-owned batch alive past this point.
    batch.done.wait(lock, [&] { return !batch.running; });
    batches_.erase(std::ranges::find(batches_, &batch));
    lock.unlock();
    if (batch.failure) std::rethrow_exception(batch.failure);
    check_cancelled(stop);
}

} // namespace datapump::triad::v3::receiver_detail
