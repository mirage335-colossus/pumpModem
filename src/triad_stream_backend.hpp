#pragma once

#include "datapump/triad_stream_receiver.hpp"

#include <functional>

namespace datapump::triad::v3::receiver_detail { class ReceiverExecutor; }

namespace datapump::triad::v3::detail {

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
struct StreamReceiverFixture {
    static CompleteStreamReceiver make(PublicProfile, ReceiverConfig, Crypto, StreamReceiverOptions,
        StreamWorkspace, StreamScorer, MessageDecoder = decode_short_message,
        MessageReceiverOptions = {}, StreamFootprint = {});
    static CompleteStreamReceiver make_with_executor(PublicProfile, ReceiverConfig, Crypto, StreamReceiverOptions,
        StreamWorkspace, StreamExecutorScorer, MessageDecoder = decode_short_message,
        MessageReceiverOptions = {}, StreamFootprint = {});
};

} // namespace datapump::triad::v3::detail
