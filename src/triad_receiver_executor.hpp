#pragma once

#include "datapump/execution.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_detail.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace datapump::triad::v3::receiver_detail {

// A stream's roots and their DSP leaves share one worker pool. A standalone
// stage owns a private instance, with its caller as lane zero.
class ReceiverExecutor {
    struct Batch;
    struct Claim { bool background; std::size_t index; };

public:
    explicit ReceiverExecutor(unsigned capacity) : capacity_(capacity) {}
    ~ReceiverExecutor();
    ReceiverExecutor(const ReceiverExecutor&) = delete;
    ReceiverExecutor& operator=(const ReceiverExecutor&) = delete;

    using Leaf = std::function<void(unsigned, std::size_t, std::stop_token)>;
    void submit(std::function<void()> score, std::function<void()> publish);
    // The caller consumes its own batch before waiting for running helpers.
    void parallel(std::uint64_t public_slot, std::size_t count, unsigned lane_limit, std::stop_token, const Leaf&);

    // One owner's overlapped work on a single bounded lane set. Urgent leaves
    // gate the owner's next chronological step. Released background leaves
    // are claimed, in order, by lanes with no urgent leaf to run, so they
    // overlap the owner's serial work and the tail of each urgent wave.
    // Only release() may be called from another thread, including a leaf.
    class Stage {
    public:
        Stage(ReceiverExecutor&, std::uint64_t public_slot, unsigned lane_limit, std::stop_token,
              Leaf urgent, Leaf background = {});
        // Abandons unclaimed leaves and waits for running ones.
        ~Stage();
        Stage(const Stage&) = delete;
        Stage& operator=(const Stage&) = delete;

        // Leaves receive a lane below this bound. The owner is lane zero.
        unsigned lanes() const noexcept;
        // Owner-side work can observe cancellation or failure of a leaf.
        std::stop_token stop_token() const noexcept;
        // Appends urgent leaves, indexed consecutively from zero.
        void post(std::size_t count);
        // Permits background leaves [0, limit) to start.
        void release(std::size_t limit);
        // The owner runs urgent leaves, then waits for running urgent leaves.
        void wait_urgent();
        // The owner runs leaves until released background leaves
        // [first, limit) have completed. It never starts a background leaf
        // beyond them, and does not wait for a running one before them.
        void wait_background(std::size_t limit, std::size_t first = 0);

    private:
        ReceiverExecutor& executor_;
        std::unique_ptr<Batch> batch_;
    };

    unsigned started_workers() const { return started_workers_.load(); }
    unsigned peak_workers() const { return peak_workers_.load(); }
    unsigned peak_running_roots() const { return peak_running_roots_.load(); }
    // The stream requests cancellation before closing. Running roots and
    // their leaves must exit before the caller releases their memory leases.
    void close();

private:
    struct Root { std::function<void()> score, publish; };
    void grow();
    void wake(std::size_t leaves);
    void run();
    bool help(std::unique_lock<execution::Mutex>&);
    static bool claim(Batch&, std::size_t background_limit, Claim&);
    void execute(Batch&, unsigned lane, Claim);
    static void check(Batch&);

    const unsigned capacity_;
    mutable execution::Mutex mutex_;
    execution::Condition ready_;
    std::deque<Root> roots_;
    std::deque<Batch*> batches_;
    std::vector<execution::Task> workers_;
    std::atomic<unsigned> started_workers_{0}, peak_workers_{0}, peak_running_roots_{0};
    unsigned running_roots_ = 0, idle_workers_ = 0;
    bool closing_ = false;
};

// A wave whose jobs form one stage of their own on the executor's lanes. The
// caller is that stage's owner and must have no other stage open.
LaneWave lane_wave(ReceiverExecutor&, std::uint64_t public_slot, unsigned lane_limit, std::stop_token);

// A lease with room for it lets the slot acquire beside coarse preparation.
ReceiverSlotScore score_slot(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned, std::size_t, std::stop_token,
    ReceiverExecutor*);
// The matched measurement control: the same slot, never overlapped.
ReceiverSlotScore score_slot_in_sequence(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned, std::size_t, std::stop_token,
    ReceiverExecutor*);

} // namespace datapump::triad::v3::receiver_detail
