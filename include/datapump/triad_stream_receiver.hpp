#pragma once

#include "datapump/triad_message_receiver.hpp"
#include "datapump/triad_stream.hpp"

#include <memory>
#include <optional>

namespace datapump::triad::v3 {

struct StreamReceiverOptions {
    // Omitted values admit twice the effective logical CPU count and use
    // available physical RAM with operating headroom. Explicit caps are exact.
    std::optional<unsigned> workers, slot_owners;
    std::optional<std::size_t> memory_limit_bytes, maximum_buffer_samples;
    std::uint64_t first_slot = 0;
    std::optional<std::uint64_t> first_input_sample;
    // Set when joining a calendar in progress without a known quiet boundary.
    bool require_quiet_rearm = false;
    std::size_t maximum_input_chunk_samples = 16384;
    std::size_t maximum_gap_samples = 16384;
    std::size_t maximum_pending_messages = 16;
    std::size_t recent_slot_receipts = 32;
    bool record_commit_timing = false; // Optional native measurement receipt.
};

struct StreamReceiverResources {
    unsigned hardware_threads = 0, workers = 0, slot_owners = 0;
    std::size_t memory_limit_bytes = 0;
};

StreamReceiverResources resolve_stream_receiver_resources(const StreamReceiverOptions& = {});

// Shared public geometry. Stream admission separately charges its complete
// input lease and checks the selected local buffer and workspace limits.
ReceiverSlotPlan make_receiver_slot_plan(const PublicProfile&, const ReceiverConfig&, std::uint64_t index);

enum class StreamPushResult { accepted, backpressured };
enum class StreamReceiverState { receiving, input_ended, cancelled, resource_limit, failed };

struct StreamCommitTiming {
    // Wall clock relative to controller construction. Snapshot after commit.
    double wall_seconds = 0;
    std::uint64_t input_stop = 0;
    std::size_t uncommitted_slots = 0;
    unsigned active_owners = 0;
    bool next_ready = false;
    // Cumulative process CPU at the same committed-slot boundary.
    std::optional<double> process_cpu_seconds;
};

struct StreamSlotReceipt {
    ReceiverSlotScore score;
    std::uint64_t pattern_epoch = 0, symbol_ordinal = 0;
    unsigned workers = 0;
    std::optional<StreamCommitTiming> commit_timing;
};

struct StreamReceiverReport {
    StreamReceiverState state = StreamReceiverState::receiving;
    StreamReceiverResources resources;
    std::string reason;
    std::uint64_t input_stop = 0, next_public_slot = 0;
    std::size_t pending_messages = 0, active_owners = 0, uncommitted_slots = 0;
    std::size_t peak_reserved_bytes = 0;
    std::optional<std::uint64_t> episode_first_public_slot;
    std::size_t observed_bits = 0, erasures = 0;
    bool input_closed = false, output_backpressured = false, recovering = false;
    // Measured time spent inside the controller's condition-variable wait.
    std::optional<double> controller_wait_seconds;
};

namespace detail { struct StreamReceiverFixture; }

// Owns one configured public sample calendar. The control thread calls every
// operation except request_cancel(), which is safe from a scoped stop callback.
// Never call push() from the physical audio callback.
class CompleteStreamReceiver {
public:
    CompleteStreamReceiver(PublicProfile, ReceiverConfig, Crypto,
        StreamReceiverOptions = {}, MessageDecoder = decode_short_message,
        MessageReceiverOptions = {});
    CompleteStreamReceiver(PublicProfile, ReceiverConfig, Crypto,
        StreamReceiverOptions, NativeMessageProfile);
    ~CompleteStreamReceiver();
    CompleteStreamReceiver(const CompleteStreamReceiver&) = delete;
    CompleteStreamReceiver& operator=(const CompleteStreamReceiver&) = delete;

    // Retry the unchanged chunk after backpressure. An empty mask means all
    // supplied samples are valid. A bounded coordinate gap is marked erased.
    StreamPushResult push(std::uint64_t first, std::span<const Complex>,
                          std::span<const std::uint8_t> valid = {});
    StreamReceiverReport resume(bool wait = false);
    std::vector<CompletedMessage> drain_messages();
    std::vector<StreamSlotReceipt> drain_recent_slots();
    StreamReceiverReport report() const;

    // Drains accepted complete windows, subject to output backpressure.
    // Neither EOF nor cancellation creates a quiet decision.
    StreamReceiverReport finish_input();
    StreamReceiverReport cancel();
    void request_cancel() noexcept;

private:
    friend struct detail::StreamReceiverFixture;
    struct Impl;
    explicit CompleteStreamReceiver(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::triad::v3
