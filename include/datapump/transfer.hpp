#pragma once
#include "datapump/clock_sync.hpp"
#include "datapump/modem.hpp"
#include "datapump/stream_codec.hpp"
#include "datapump/runtime.hpp"
#include "datapump/recovery.hpp"
#include <optional>

namespace datapump::transfer {
// UI settings and result containers. The canonical triad receiver will attach
// here; this facade contains no receiver, waveform generator or receiver bank.
struct Options {
    modem::Config modem;
    std::size_t content_limit=default_memory_limit;
    std::size_t dsp_workspace_bytes=runtime::default_dsp_workspace_bytes();
    FecMode fec=FecMode::rs60;
    bool compression=true;
    std::optional<Crypto> key;
    std::uint64_t timestamp=0;
    unsigned search_seconds=6;
    std::optional<clock_sync::Policy> clock_sync;
    double audio_timing_error_seconds=clock_sync::default_audio_error_seconds;
};
struct Estimate {
    std::size_t coded_bytes=0,wire_bits=0,content_bytes=0;
    double coded_seconds=0,content_seconds=0,total_seconds=0;
    bool memory_supported=false,batch_memory_supported=false,repeatable_allowed=false;
    std::size_t waveform_samples=0;
};
inline constexpr std::size_t short_message_bytes=16;
inline constexpr std::size_t short_message_bits=13*short_message_bytes;
bool uses_raw_message(const Message&) noexcept;
struct Received {
    StreamContent content;
    modem::Diagnostics diagnostics;
    std::uint64_t timestamp=0;
    Bytes raw_bits;
    std::size_t observed_bits=0,missing_symbols=0;
    bool stream_complete=false,content_validated=false,short_text_decoded=false;
    std::string error;
    RecoveryProgress recovery_progress;
};
}
