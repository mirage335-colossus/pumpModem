#pragma once

#include "datapump/triad_stream_receiver.hpp"

#include <functional>
#include <limits>

namespace datapump::triad::v3::receiver_detail { class ReceiverExecutor; }

namespace datapump::triad::v3::detail {

// Disjoint raw-input dependencies of two half-open logical-IQ apertures.
// Compare differences only after order, and guard doubling against overflow.
inline bool repeat_input_dependencies_disjoint(std::uint64_t first, std::uint64_t previous_stop,
                                                std::uint64_t halo) noexcept {
    return first >= previous_stop && halo <= std::numeric_limits<std::uint64_t>::max() / 2 &&
        first - previous_stop >= 2 * halo;
}

// Test-only scorer injection. Calendar, admission, sample ownership,
// backpressure, and codec processing still use the ordinary stream controller.
using StreamWorkspace = std::function<std::size_t(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned)>;
using StreamScorer = std::function<ReceiverSlotScore(const ReceiverConfig&, const ReceiverSlotPlan&,
    const Crypto&, std::span<const Complex>, std::span<const std::uint8_t>, unsigned,
    std::size_t, std::stop_token)>;
using StreamExecutorScorer = std::function<ReceiverSlotScore(const ReceiverConfig&, const ReceiverSlotPlan&,
    const Crypto&, std::span<const Complex>, std::span<const std::uint8_t>, unsigned,
    std::size_t, std::stop_token, receiver_detail::ReceiverExecutor*)>;
using StreamFootprint = std::function<std::size_t()>;
// The trailing workspace quotes the wider lease that lets a slot overlap
// Helper acquisition with coarse preparation. Omitted, no slot is offered one.
struct StreamReceiverFixture {
    static CompleteStreamReceiver make(PublicProfile, ReceiverConfig, Crypto, StreamReceiverOptions,
        StreamWorkspace, StreamScorer, MessageDecoder = decode_short_message,
        MessageReceiverOptions = {}, StreamFootprint = {}, StreamWorkspace = {});
    static CompleteStreamReceiver make_with_executor(PublicProfile, ReceiverConfig, Crypto, StreamReceiverOptions,
        StreamWorkspace, StreamExecutorScorer, MessageDecoder = decode_short_message,
        MessageReceiverOptions = {}, StreamFootprint = {}, StreamWorkspace = {});
};

} // namespace datapump::triad::v3::detail
