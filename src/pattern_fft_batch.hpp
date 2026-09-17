#pragma once

#include "datapump/pattern_code.hpp"
#include <array>
#include <limits>
#include <type_traits>

namespace datapump::modem::detail {

using FftComplex = std::complex<double>;

// Logical work has no CPU worker identity, owning object or function pointer.
// A backend may split/reorder these records arbitrarily; output remains indexed
// by the original job and start offset. These are host/device transfer records,
// not a portable serialized wire format.
struct FftSearchJob {
    std::uint64_t symbol = 0, phase = 0, frequency_index = 0;
    double frequency_hz = 0; // Offset from the nominal carrier in the geometry.
    std::uint64_t prepared_template = std::numeric_limits<std::uint64_t>::max();
    double clock_ratio = 1;
};
struct FftSearchScore { double zero = 0, one = 0; };

// The pattern metadata is independent of PatternCode's mutable CPU cache.
// A device backend can prepare templates from this record and each job's
// symbol/phase, or upload the already prepared templates supplied below.
struct FftPatternParameters {
    std::uint64_t epoch = 0, chip_samples = 0, chips_per_symbol = 0, symbol_samples = 0;
    std::uint32_t sample_rate = 0, spreading_mode = 0;
    std::uint32_t shaped = 0, scramble = 0, dsss = 0;
    std::array<std::uint8_t,32> spreading_seed{}, dsss_seed{};
};
struct FftSearchGeometry {
    FftPatternParameters pattern;
    std::uint64_t bins_per_symbol = 0, bin_samples = 0;
    double carrier_hz = 0, evidence_count = 0, noise_condition = 1;
    std::uint32_t real_rank = 0, sample_fit = 0, extended_clock_window = 0;
};
static_assert(std::is_trivially_copyable_v<FftSearchJob> && std::is_standard_layout_v<FftSearchJob>);
static_assert(std::is_trivially_copyable_v<FftSearchScore> && std::is_standard_layout_v<FftSearchScore>);
static_assert(std::is_trivially_copyable_v<FftSearchGeometry> && std::is_standard_layout_v<FftSearchGeometry>);

// Host upload views, deliberately separate from the transferable records.
// A device implementation packs these read-only rows and translates the table
// index in FftSearchJob; it must not copy these host pointers into a kernel.
struct FftPreparedTemplate {
    std::array<std::span<const FftComplex>,2> rows;
    std::array<double,2> energy{};
    std::array<FftComplex,2> square{};
};
struct FftSearchBatch {
    ~FftSearchBatch();
    FftSearchGeometry geometry;
    std::span<const FftComplex> spectrum, carrier_square;
    std::span<const double> energy_prefix;
    std::span<const FftPreparedTemplate> prepared;
    std::size_t starts = 0, score_stride = 0;
};

// CPU-private storage scales with CPU workers, not the logical hypothesis
// count. Backends own their different execution storage behind this boundary.
// PatternCode must be constructed from the same configuration represented by
// batch.geometry.pattern. The caller guarantees this; only buffer geometry,
// template indices and basic numeric dimensions are validated by the backend.
struct FftSearchWorkspace {
    std::unique_ptr<PatternCode> code;
    std::vector<FftComplex> product;
    FftSearchWorkspace(const Config&,std::size_t transform,bool needs_code);
    std::size_t working_bytes() const;
};

// Synchronous reference backend: every job writes only its disjoint output
// slice. No trial count, threshold, peak, track, admission, or reception state
// is visible here. The caller publishes results in original search order only
// after successful completion, and owns/accountably bounds all supplied spans.
void execute_fft_search_cpu(const FftSearchBatch&,std::span<const FftSearchJob>,
                            std::span<FftSearchScore>,std::span<FftSearchWorkspace>,
                            std::stop_token = {});

void pattern_fft(std::span<FftComplex>,bool inverse,std::stop_token);
double pattern_evidence(FftComplex dot,double energy,double template_energy,double count,
                        double condition,bool real_rank,bool exact_real,
                        FftComplex template_square = {});

} // namespace datapump::modem::detail
