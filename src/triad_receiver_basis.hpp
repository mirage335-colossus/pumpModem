#pragma once

#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"

#include <memory>
#include <stop_token>
#include <vector>

namespace datapump::triad::v3::receiver_detail {

struct MappedBasisRow {
    std::int64_t epoch_offset_seconds = 0;
    // Final native map, including the window lead and source start. Both
    // source bits use this same public map. It is independent of observed IQ.
    double arrival_seconds = 0, frequency_hz = 0, clock_ppm = 0;
};

struct MappedBasisReport {
    std::uint64_t calls = 0, interpolated_rows = 0, direct_rows = 0;
    std::uint64_t prepared_calls = 0, prepared_mapped_columns = 0;
    std::uint64_t prepared_direct_columns = 0, prepared_empty_columns = 0;
    std::uint64_t cache_hits = 0, cache_misses = 0, evictions = 0;
    std::uint64_t native_preparation_calls = 0, native_preparation_samples = 0;
    // Elapsed native tile rendering, summed over the lanes that rendered.
    double native_preparation_seconds = 0;
    // Columns whose tiles a rendering consumer received as public addresses,
    // and the keyed lanes whose coefficients it requested.
    std::uint64_t addressed_calls = 0, addressed_columns = 0, keyed_lanes = 0;
    std::uint64_t large_phase_fallback_rows = 0, staging_fallback_rows = 0;
    std::size_t cache_bytes = 0, peak_cache_bytes = 0, peak_pinned_tiles = 0;
    std::size_t tile_samples = 0, cache_limit_bytes = 0;
    unsigned dense_factor = 1;
    double dense_sample_rate_hz = 0;
    double oversampling_ratio = 0, interpolation_amplitude_proxy = 0;
    std::string fallback_reason;
};

// A reusable immutable [0, rows) sequence for the receiver's block calendar.
// Arbitrary caller-provided offsets still use the checked span API below.
class ConsecutiveBasisOffsets {
public:
    explicit ConsecutiveBasisOffsets(std::size_t rows = 0);
    std::size_t size() const noexcept { return offsets_.size(); }

private:
    std::vector<double> offsets_;
    friend class MappedBasis;
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
                     std::span<Complex> output, const std::stop_token& = {});
    // Fills, ahead of need, exactly the tiles that render_pair pins for rows
    // [first, first+rows) at unit spacing. A caller overlaps this serial
    // per-epoch preparation with other lanes' rendering of earlier blocks.
    // Cache receipts equal those of the same fills made on demand.
    void prepare_pair(const MappedBasisRow&, std::span<const Tap>,
                      std::uint64_t first_window_sample, std::size_t rows, const std::stop_token& = {});
    // Prepare the very same public map and cache tiles without rendering the
    // supported basis on the CPU. Columns are indexed [bit][tap]; the consumer
    // initializes missing/empty columns to zero. Each synchronous sink call
    // owns at most 32 pinned tiles, and every tile span contains its fixed-size
    // immutable samples including zero padding beyond finite source support.
    // Gather indices outside [0, sum(tile.samples.size())) are exactly zero.
    // Host-to-device copies must finish before the sink returns. A device cache
    // may retain weak owner identities, not uncharged strong host leases. Exact
    // native fallback columns are explicitly marked and also borrowed only for
    // the sink call. Device storage/allocator bounds belong to the consumer.
    void visit_pair(const MappedBasisRow&, std::span<const Tap>,
                    std::uint64_t first_window_sample, std::span<const double> offsets,
                    const BulkMappedSink&, const std::stop_token& = {});
    void visit_pair(const MappedBasisRow&, std::span<const Tap>, std::uint64_t first_window_sample,
                    const ConsecutiveBasisOffsets&, std::size_t rows, const BulkMappedSink&,
                    const std::stop_token& = {});
    // The public description of this slot's dense source for a consumer
    // that renders its tiles, or null when every column takes the exact
    // native route. It stays valid for the lifetime of the basis.
    const FineTileSource* tile_source();
    // The same public maps as visit_pair, with each column's tile address in
    // place of native leases: no tile is rendered, pinned or cached here.
    // Exact native fallback columns are still supplied as samples.
    void address_pair(const MappedBasisRow&, std::span<const Tap>, std::uint64_t first_window_sample,
                      const ConsecutiveBasisOffsets&, std::size_t rows, const BulkMappedSink&,
                      const std::stop_token& = {});
    MappedBasisReport report() const;

    class Addresses;

private:
    // The first `rows` offsets, which must exist.
    static std::span<const double> consecutive(const ConsecutiveBasisOffsets&, std::size_t rows);
    void visit_pair_impl(const MappedBasisRow&, std::span<const Tap>, std::uint64_t,
                         std::span<const double>, const BulkMappedSink&, const std::stop_token&, bool consecutive);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// One owner's address_pair calls for a fixed set of rows, block after block,
// on its own thread. Each call supplies the same maps, sink calls and
// receipts as address_pair. The terms of a map that depend only on its row,
// or only on its block, are computed once for both taps or once per block.
// The first call after open() takes one admitted call of the basis, which is
// held until close(). close() releases it and credits the receipts of every
// call since open(), also after a call failed.
class MappedBasis::Addresses {
public:
    explicit Addresses(MappedBasis&);
    ~Addresses();
    Addresses(const Addresses&) = delete;
    Addresses& operator=(const Addresses&) = delete;

    // The rows and taps stay alive and unchanged until close().
    void open(std::span<const MappedBasisRow>, std::span<const Tap>);
    void close() noexcept;
    // address_pair(rows[row], taps, first_window_sample, offsets, count, sink, stop).
    void address(std::size_t row, std::uint64_t first_window_sample, const ConsecutiveBasisOffsets& offsets,
                 std::size_t count, const BulkMappedSink& sink, const std::stop_token& stop);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace datapump::triad::v3::receiver_detail
