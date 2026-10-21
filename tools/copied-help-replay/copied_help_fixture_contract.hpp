#pragma once

#include "datapump/crypto.hpp"
#include "datapump/modem.hpp"
#include "datapump/pattern_receiver.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace datapump::diagnostic::copied_help_replay {

struct Feed {
    std::uint64_t first = 0, count = 0, pcm_offset = 0;
    double mixer_real = 0, mixer_imag = 0;
    double after_real = 0, after_imag = 0;
    bool receiver_active = false;
};

struct ExpectedBurst {
    // Absolute bank coordinates; the adapter adds input_origin to native local
    // stream_first_sample/end_sample before comparing these two fields.
    std::uint64_t feed_first = 0, physical_first = 0, physical_end = 0;
    std::uint64_t stream_first_symbol = 0, chunk_first_symbol = 0;
    std::uint64_t stream_phase_samples = 0, missing_slots = 0;
    bool complete = false;
    std::string_view wire_bits; // Every bit, with '?' for native unknown value2.
    double score = 0, support_samples = 0;
    // Only populate from an unambiguous original detector-drain trace join.
    std::optional<std::uint64_t> physical_chunk_first;
    std::optional<double> frequency_hz;
};

struct Fixture {
    std::string_view label, pcm_path, keyfile_path, pcm_sha256, key_tag_hex;
    std::uint64_t epoch = 0, receiver_id = 0, input_origin = 0;
    std::uint64_t constructor_grant = 0;
    std::span<const Feed> feeds;
    std::span<const ExpectedBurst> expected;
    modem::Config (*make_config)(const Crypto&) = nullptr;
    modem::PatternSearch (*make_search)() = nullptr;
};

// Each generated header defines a chosen namespace containing:
// const datapump::diagnostic::copied_help_replay::Fixture& fixture();
// Paths are relative to --fixture-base unless absolute. All feeds begin at
// bank sample0 and end at the feed carrying the last retained expected burst.
// Only the receiver's original admitted prefix is active. No EOF flush follows.

} // namespace datapump::diagnostic::copied_help_replay
