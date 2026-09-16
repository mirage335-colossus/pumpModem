#pragma once

#include "datapump/stream_codec.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <vector>

namespace datapump::transfer {
enum class RecoveryState { none, ready, running, incomplete, recovered, exhausted, ambiguous, cancelled, unavailable };
struct RecoveryProgress {
    RecoveryState state=RecoveryState::none;
    std::uint64_t attempts=0,total=0;
    std::chrono::milliseconds elapsed{0};
};
struct RecoveryOptions {
    bool enabled=true;
    std::chrono::milliseconds budget{300000};
    unsigned workers=0;
    std::size_t retained_bits=65536;
    unsigned extra_errors=2;
    std::size_t workspace_bytes=16*1024*1024;
};
struct RecoveryInput {
    // Immutable hard decisions at their original canonical symbol positions.
    // 2 denotes an unknown slot; bits have already undergone normal Data unmasking.
    Bytes bits;
    std::uint64_t first_symbol=0;
    std::vector<std::uint64_t> established_starts;
    FecMode fec=FecMode::rs60;
    // Factory and returned callbacks must support independent concurrent calls.
    std::function<IntervalOptions(std::uint64_t)> interval_options;
};
// Invoke only after independently established physical completion. run() is
// synchronous, internally parallel, and resumes an incomplete/cancelled search.
// Calls to progress/result/working_bytes may run concurrently with run().
// Only one run() call may execute at a time. No source interpretation occurs here.
class RecoveryJob {
public:
    RecoveryJob(RecoveryInput,RecoveryOptions={});
    ~RecoveryJob();
    RecoveryJob(const RecoveryJob&)=delete;
    RecoveryJob& operator=(const RecoveryJob&)=delete;
    RecoveryProgress progress() const;
    void run(std::stop_token={});
    std::vector<DecodedInterval> result() const;
    std::size_t working_bytes() const;
    // Conservative stable peak reservation, including worker scratch.
    std::size_t workspace_bound() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
