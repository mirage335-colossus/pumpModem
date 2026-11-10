#pragma once

#include "datapump/triad_receiver.hpp"

#include <memory>

namespace datapump::triad::v3::receiver_detail {

struct MappedBasisRow {
    std::int64_t epoch_offset_seconds = 0;
    // Final native map, including the window lead and source start. Both
    // source bits use this same public map. It is independent of observed IQ.
    double arrival_seconds = 0, frequency_hz = 0, clock_ppm = 0;
};

struct MappedBasisReport {
    std::uint64_t calls = 0, interpolated_rows = 0, direct_rows = 0;
    std::uint64_t cache_hits = 0, cache_misses = 0, evictions = 0;
    std::uint64_t native_preparation_calls = 0, native_preparation_samples = 0;
    std::uint64_t large_phase_fallback_rows = 0, staging_fallback_rows = 0;
    std::size_t cache_bytes = 0, peak_cache_bytes = 0, peak_pinned_tiles = 0;
    unsigned dense_factor = 1;
    double oversampling_ratio = 0, interpolation_amplitude_proxy = 0;
    std::string fallback_reason;
};

// A slot's shared bounded cache of unwarped keyed DATA. The caller owns the
// aggregate worker budget. At most `workers` concurrent render_pair calls may
// read immutable pinned tiles; preparation alone locks a source's mutable cache.
// Output is [bit][tap][sample], has exactly 2*taps.size()*offsets.size() entries,
// and is written only by this call. The same native cubic kernel serves Python.
class MappedBasis {
public:
    MappedBasis(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                std::size_t maximum_block_rows, unsigned workers);
    ~MappedBasis();
    MappedBasis(const MappedBasis&) = delete;
    MappedBasis& operator=(const MappedBasis&) = delete;

    // Conservative allocation bound: shared cache/source state, every admitted
    // worker's pins, source preparation scratch and paired output are included.
    static std::size_t workspace_bytes(const ReceiverConfig&, const ReceiverSlotPlan&,
                                       std::size_t maximum_block_rows, unsigned workers);
    void render_pair(const MappedBasisRow&, std::span<const Tap>,
                     std::uint64_t first_window_sample, std::span<const double> offsets,
                     std::span<Complex> output, std::stop_token = {});
    MappedBasisReport report() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::triad::v3::receiver_detail
