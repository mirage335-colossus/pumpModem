#pragma once

#include "datapump/live.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <tuple>
#include <utility>

namespace datapump::live::detail {

// Numeric destination requirements only. They deliberately retain no source
// references, so a caller can measure under its lock and prepare after unlock.
struct SnapshotCopyCapacity {
    std::array<std::size_t, 5> strings{};
    std::array<std::size_t, 6> vectors{};
    std::array<std::size_t, 12> transmit_trace{};
};

namespace snapshot_copy_detail {

inline auto strings(Snapshot& value) noexcept {
    return std::tie(value.receiver_error, value.clock_timing_status,
                    value.status, value.error, value.helper_status);
}
inline auto strings(const Snapshot& value) noexcept {
    return std::tie(value.receiver_error, value.clock_timing_status,
                    value.status, value.error, value.helper_status);
}

inline auto vectors(Snapshot& value) noexcept {
    return std::tie(value.waveform, value.spectrum_db, value.spectrum_complex,
                    value.constellation, value.pattern_scores,
                    value.pattern_score_observations);
}
inline auto vectors(const Snapshot& value) noexcept {
    return std::tie(value.waveform, value.spectrum_db, value.spectrum_complex,
                    value.constellation, value.pattern_scores,
                    value.pattern_score_observations);
}

inline auto trace_vectors(modem::TransmitTrace& value) noexcept {
    return std::tie(value.source, value.compressed_bits, value.wire_plain_bits,
                    value.wire_bits, value.data_key_bits, value.pattern_input,
                    value.pattern_output, value.pattern_key, value.dsss_key,
                    value.outer_dsss_key, value.dsss_permutation_seed,
                    value.fhss_key);
}
inline auto trace_vectors(const modem::TransmitTrace& value) noexcept {
    return std::tie(value.source, value.compressed_bits, value.wire_plain_bits,
                    value.wire_bits, value.data_key_bits, value.pattern_input,
                    value.pattern_output, value.pattern_key, value.dsss_key,
                    value.outer_dsss_key, value.dsss_permutation_seed,
                    value.fhss_key);
}

template<class Fields, class Sizes, std::size_t... I>
void capture_sizes(const Fields& fields, Sizes& sizes,
                   std::index_sequence<I...>) noexcept {
    ((sizes[I] = std::get<I>(fields).size()), ...);
}

template<class Fields, class Sizes, std::size_t... I>
bool fits(const Fields& fields, const Sizes& sizes,
          std::index_sequence<I...>) noexcept {
    return ((std::get<I>(fields).capacity() >= sizes[I]) && ...);
}

template<class Fields, class Sizes, std::size_t... I>
void reserve(Fields& fields, const Sizes& sizes,
             std::index_sequence<I...>) {
    (std::get<I>(fields).reserve(sizes[I]), ...);
}

template<class Fields, class Sizes>
void capture_sizes(const Fields& fields, Sizes& sizes) noexcept {
    capture_sizes(fields, sizes,
                  std::make_index_sequence<std::tuple_size_v<Sizes>>{});
}

template<class Fields, class Sizes>
bool fits(const Fields& fields, const Sizes& sizes) noexcept {
    return fits(fields, sizes,
                std::make_index_sequence<std::tuple_size_v<Sizes>>{});
}

template<class Fields, class Sizes>
void reserve(Fields& fields, const Sizes& sizes) {
    reserve(fields, sizes,
            std::make_index_sequence<std::tuple_size_v<Sizes>>{});
}

} // namespace snapshot_copy_detail

inline SnapshotCopyCapacity snapshot_copy_requirement(
        const Snapshot& source, std::string_view transmit_error) noexcept {
    SnapshotCopyCapacity result;
    auto source_strings = snapshot_copy_detail::strings(source);
    snapshot_copy_detail::capture_sizes(source_strings, result.strings);
    result.strings[3] = std::max({source.error.size(), source.receiver_error.size(),
                                  transmit_error.size()});
    if (!transmit_error.empty())
        result.strings[2] = std::max(result.strings[2],
                                     std::string_view("Transmission failed").size());

    auto source_vectors = snapshot_copy_detail::vectors(source);
    snapshot_copy_detail::capture_sizes(source_vectors, result.vectors);
    auto source_trace = snapshot_copy_detail::trace_vectors(source.transmit_trace);
    snapshot_copy_detail::capture_sizes(source_trace, result.transmit_trace);
    return result;
}

inline bool snapshot_copy_fits(const Snapshot& destination,
                               const SnapshotCopyCapacity& required) noexcept {
    auto destination_strings = snapshot_copy_detail::strings(destination);
    if (!snapshot_copy_detail::fits(destination_strings, required.strings)) return false;
    auto destination_vectors = snapshot_copy_detail::vectors(destination);
    if (!snapshot_copy_detail::fits(destination_vectors, required.vectors)) return false;
    auto destination_trace = snapshot_copy_detail::trace_vectors(destination.transmit_trace);
    return snapshot_copy_detail::fits(destination_trace, required.transmit_trace);
}

inline void prepare_snapshot_copy(Snapshot& destination,
                                  const SnapshotCopyCapacity& required) {
    auto destination_strings = snapshot_copy_detail::strings(destination);
    snapshot_copy_detail::reserve(destination_strings, required.strings);
    auto destination_vectors = snapshot_copy_detail::vectors(destination);
    snapshot_copy_detail::reserve(destination_vectors, required.vectors);
    auto destination_trace = snapshot_copy_detail::trace_vectors(destination.transmit_trace);
    snapshot_copy_detail::reserve(destination_trace, required.transmit_trace);
}

} // namespace datapump::live::detail
