#pragma once

#include "datapump/execution.hpp"
#include "datapump/triad_receiver.hpp"

#include <atomic>
#include <deque>
#include <vector>

namespace datapump::triad::v3::receiver_detail {

// A stream's roots and their synchronous DSP batches share one worker pool.
// A root consumes its own batch before waiting for already-running helpers.
class ReceiverExecutor {
public:
    explicit ReceiverExecutor(unsigned capacity) : capacity_(capacity) {}
    ~ReceiverExecutor();
    ReceiverExecutor(const ReceiverExecutor&) = delete;
    ReceiverExecutor& operator=(const ReceiverExecutor&) = delete;

    using Leaf = std::function<void(unsigned, std::size_t, std::stop_token)>;
    void submit(std::function<void()> score, std::function<void()> publish);
    void parallel(std::uint64_t public_slot, std::size_t count, unsigned lane_limit, std::stop_token, const Leaf&);
    unsigned started_workers() const { return started_workers_.load(); }
    unsigned peak_workers() const { return peak_workers_.load(); }
    unsigned peak_running_roots() const { return peak_running_roots_.load(); }
    // The stream requests cancellation before closing. Running roots and
    // their leaves must exit before the caller releases their memory leases.
    void close();

private:
    struct Batch;
    struct Root { std::function<void()> score, publish; };
    void grow();
    void run();
    bool help(std::unique_lock<execution::Mutex>&);
    static void execute(Batch&, unsigned, std::size_t);

    const unsigned capacity_;
    mutable execution::Mutex mutex_;
    execution::Condition ready_;
    std::deque<Root> roots_;
    std::deque<Batch*> batches_;
    std::vector<execution::Task> workers_;
    std::atomic<unsigned> started_workers_{0}, peak_workers_{0}, peak_running_roots_{0};
    unsigned running_roots_ = 0;
    bool closing_ = false;
};

ReceiverSlotScore score_slot(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned, std::size_t, std::stop_token,
    ReceiverExecutor*);

} // namespace datapump::triad::v3::receiver_detail
