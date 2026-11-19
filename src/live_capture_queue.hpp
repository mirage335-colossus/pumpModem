#pragma once

#include "datapump/audio.hpp"
#include "datapump/execution.hpp"
#include <deque>
#include <mutex>
#include <span>
#include <utility>

namespace datapump::live::detail {

struct CaptureChunk {
    std::vector<float> pcm;
    std::optional<audio::TimePrediction> timestamp;
    std::uint64_t first = 0;
};

// All fields and operations are protected by mutex. The producer supplies a
// lock it already owns so waiting atomically releases it to the consumer.
struct CaptureQueue {
    execution::Mutex mutex;
    execution::StopCondition changed;
    std::deque<CaptureChunk> chunks;
    std::optional<audio::TimePrediction> timestamp;
    std::uint64_t next_sample = 0;
    std::size_t queued_samples = 0, provider_bytes = 0;
    bool done = false, capture_callback_may_block = false;
    std::string error;

    void fail(std::string text) {
        std::lock_guard lock(mutex);
        if (error.empty()) error = std::move(text);
        changed.notify_all();
    }

    bool push(std::unique_lock<execution::Mutex>& lock, std::span<const float> samples,
              std::size_t queue_limit, std::size_t buffered_chunk_limit,
              std::stop_token stop, std::stop_token capture_stop) {
        if (!error.empty() || stop.stop_requested() || capture_stop.stop_requested()) return false;
        // The receiver workspace charges one in-flight PCM/IQ chunk. A
        // buffered provider must honor both that bound and the 50 ms capture
        // contract, even if the surrounding source is malformed.
        if (capture_callback_may_block && samples.size() > buffered_chunk_limit) {
            error = "Robust buffered capture chunk exceeds its sample bound";
            changed.notify_all();
            return false;
        }
        const auto room = [&] {
            return samples.size() <= queue_limit - queued_samples && chunks.size() < 256;
        };
        if (capture_callback_may_block && samples.size() <= queue_limit)
            changed.wait(lock, capture_stop, [&] {
                return room() || !error.empty() || stop.stop_requested();
            });
        if (!error.empty() || stop.stop_requested() || capture_stop.stop_requested()) return false;
        if (!room()) {
            error = "Robust input backlog overrun; capture stopped with missing samples";
            changed.notify_all();
            return false;
        }
        if (samples.size() > UINT64_MAX - next_sample) {
            error = "Robust capture sample coordinate overflow";
            changed.notify_all();
            return false;
        }
        chunks.push_back({{samples.begin(), samples.end()}, std::exchange(timestamp, {}), next_sample});
        queued_samples += samples.size();
        next_sample += samples.size();
        changed.notify_all();
        return true;
    }

    CaptureChunk pop() {
        auto chunk = std::move(chunks.front());
        chunks.pop_front();
        queued_samples -= chunk.pcm.size();
        changed.notify_all();
        return chunk;
    }
};
}
