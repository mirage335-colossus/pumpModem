#pragma once

#include "datapump/pattern_code.hpp"
#include "pattern_differential.hpp"
#include "search_arithmetic.hpp"
#include "search_fft.hpp"
#include <array>
#include <limits>
#include <optional>
#include <type_traits>

namespace datapump::modem::detail {

using FftComplex = std::complex<double>;
struct FftStartRange { std::size_t first=0,count=0; };

// Logical work has no CPU worker identity, owning object or function pointer.
// A backend may split/reorder these records arbitrarily; output remains indexed
// by the original job and start offset. These are host/device transfer records,
// not a portable serialized wire format.
struct FftSearchJob {
    std::uint64_t symbol = 0, phase = 0, frequency_index = 0;
    double frequency_hz = 0; // Offset from the nominal carrier in the geometry.
    std::uint64_t prepared_template = std::numeric_limits<std::uint64_t>::max();
    double clock_ratio = 1;
    // Original batch start offsets. The default preserves the complete job;
    // zero ranges skips it. Ranges are sorted, disjoint and never renumbered.
    std::size_t range_begin=0,range_count=std::numeric_limits<std::size_t>::max();
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
    unsigned dsss_factor=1;
    std::uint32_t outer_dsss_version=static_cast<std::uint32_t>(OuterDsssVersion::legacy_v1);
    bool operator==(const FftPatternParameters&) const = default;
};
enum class PrivateTemplateReuse : std::uint32_t { exact_only, bounded_interpolation };
struct FftSearchGeometry {
    FftPatternParameters pattern;
    std::uint64_t bins_per_symbol = 0, bin_samples = 0;
    double carrier_hz = 0, evidence_count = 0, noise_condition = 1;
    std::uint32_t real_rank = 0, sample_fit = 0, extended_clock_window = 0;
    std::uint32_t drift_sections = 1;
    std::uint64_t differential_window_samples = 0;
    // Low-level legacy callers retain FP64; applications explicitly propagate Config.
    std::uint32_t search_arithmetic = static_cast<std::uint32_t>(SearchArithmetic::fp64);
    // Independent of operand width. FP32 does not itself authorize approximate
    // templates; the receiver must admit this policy and its geometry separately.
    PrivateTemplateReuse private_template_reuse = PrivateTemplateReuse::exact_only;
};
// Resolve arithmetic before constructing shared input spectra or prepared rows.
// Covariance/Gram and incoherent reductions currently retain the FP64 operator.
inline SearchArithmetic effective_search_arithmetic(SearchArithmetic requested,
        bool sample_fit,unsigned drift_sections,std::uint64_t differential_window) {
    // Only the receiver has enough scheduling/native-buffer context to resolve
    // automatic acquisition to FP32. Unresolved low-level Default is conservative.
    if(requested==SearchArithmetic::default_mode)return SearchArithmetic::fp64;
    return requested==SearchArithmetic::fp32 &&
        (sample_fit || drift_sections!=1 || differential_window!=0)?SearchArithmetic::fp64:requested;
}
inline SearchArithmetic effective_search_arithmetic(const FftSearchGeometry& g) {
    return effective_search_arithmetic(static_cast<SearchArithmetic>(g.search_arithmetic),
        g.sample_fit!=0,g.drift_sections,g.differential_window_samples);
}
static_assert(std::is_trivially_copyable_v<FftSearchJob> && std::is_standard_layout_v<FftSearchJob>);
static_assert(std::is_trivially_copyable_v<FftSearchScore> && std::is_standard_layout_v<FftSearchScore>);
static_assert(std::is_trivially_copyable_v<FftSearchGeometry> && std::is_standard_layout_v<FftSearchGeometry>);

// Host upload views, deliberately separate from the transferable records.
// A device implementation packs these read-only rows and translates the table
// index in FftSearchJob; it must not copy these host pointers into a kernel.
// Rows, energies and input spectra must use effective_search_arithmetic(geometry);
// backend normalization cannot recover an already rounded upload.
struct FftPreparedTemplate {
    std::array<std::span<const FftComplex>,2> rows;
    std::array<double,2> energy{};
    std::array<FftComplex,2> square{};
};
// Batch-local packed operands. No shifted PCM matrix. Input statistics are valid
// only while the original observation span is immutable; discard at every batch
// boundary/cancellation. Private template rows are repacked for EVERY job.
struct FftQuantizedDirect {
    std::vector<search_arithmetic::Complex8> input,input_residual;
    std::array<std::vector<search_arithmetic::Complex8>,2> rows,row_residual;
    std::vector<search_arithmetic::CompensatedBlock> input_blocks;
    std::array<std::vector<search_arithmetic::CompensatedBlock>,2> row_blocks;
    const FftComplex* source = nullptr;
    std::size_t source_size = 0;
    FftQuantizedDirect(std::size_t input_size,std::size_t row_size);
    std::size_t working_bytes() const;
    static std::size_t required_bytes(std::size_t input_size,std::size_t row_size);
};
// Optional acquisition-only interpolation of one exact private bit pair. This
// is not the public nominal_reference contract and is never used by tracking
// or the outer-code presence guard. Its lifetime is one symbol/phase cohort.
class FftPrivateTemplate {
    FftPatternParameters pattern_;
    std::uint64_t bins_=0,bin_samples_=0,symbol_=0,phase_=0;
    std::vector<std::array<std::complex<float>,2>> values_;
public:
    static constexpr double maximum_displacement_samples=.001;
    static bool eligible(const FftSearchGeometry&,const FftSearchJob&);
    static std::size_t required_bytes(const FftSearchGeometry&);
    FftPrivateTemplate(const FftSearchGeometry&,const FftSearchJob&,PatternCode&,std::stop_token);
    ~FftPrivateTemplate();
    FftPrivateTemplate(const FftPrivateTemplate&)=delete;
    FftPrivateTemplate& operator=(const FftPrivateTemplate&)=delete;
    bool matches(const FftSearchGeometry&,const FftSearchJob&) const;
    std::size_t working_bytes() const;
    std::array<FftComplex,2> value(std::size_t bin,double clock_ratio) const;
};
struct FftSearchBatch {
    ~FftSearchBatch();
    FftSearchGeometry geometry;
    std::span<const FftComplex> spectrum, carrier_square;
    // Original disjoint projection bins permit a direct first-window pass.
    std::span<const FftComplex> observations;
    std::uint64_t first_bin = 0;
    std::span<const double> energy_prefix;
    std::span<const FftPreparedTemplate> prepared;
    // Optional public nominal-clock samples before carrier rotation. Coupled
    // clock alternatives still generate their own time-scaled waveform.
    std::span<const std::array<FftComplex,2>> nominal_reference;
    std::span<const FftStartRange> start_ranges;
    std::size_t starts = 0, score_stride = 0;
    // INT8 may reject only certified scores strictly below this retention floor.
    // Zero requests exact reference scores at every selected cell.
    double retain_floor = 0;
    FftQuantizedDirect* quantized_direct = nullptr;
    const FftPrivateTemplate* private_template = nullptr;
};
// Paired D10/100/1000 CPU measurements favor <=32 starts; 64 is marginal
// and 128 loses. Keep the conservative measured crossover. Direct scoring generates
// each bit's template once, then contracts only its selected original starts.
inline bool pattern_fft_direct_eligible(const FftSearchGeometry& g,bool generated,
        std::size_t selected_starts,bool observations_available) {
    return generated && observations_available && !g.sample_fit && g.drift_sections<=1 &&
        !g.differential_window_samples && selected_starts>0 && selected_starts<=32;
}
struct FftDriftAccumulator {
    FftComplex dot{};
    double explained = 0, strongest = 0;
};

// CPU-private storage scales with CPU workers, not the logical hypothesis
// count. Backends own their different execution storage behind this boundary.
// PatternCode must be constructed from the same configuration represented by
// batch.geometry.pattern. The caller guarantees this; only buffer geometry,
// template indices and basic numeric dimensions are validated by the backend.
struct FftSearchWorkspace {
    std::unique_ptr<PatternCode> code;
    std::vector<FftComplex> product;
    std::vector<FftDriftAccumulator> drift;
    std::vector<DifferentialAccumulator> differential;
    FftSearchWorkspace(const Config&,std::size_t transform,bool needs_code,std::size_t drift_starts=0,
                       std::size_t differential_starts=0);
    std::size_t working_bytes() const;
};

// Synchronous reference backend: every job writes only its disjoint output
// slice. No trial count, threshold, peak, track, admission, or reception state
// is visible here. The caller publishes results in original search order only
// after successful completion, and owns/accountably bounds all supplied spans.
void execute_fft_search_cpu(const FftSearchBatch&,std::span<const FftSearchJob>,
                            std::span<FftSearchScore>,std::span<FftSearchWorkspace>,
                            std::stop_token = {});

// A serial receiver uses its already-budgeted transform and start scratch.
// No allocation, admission or partial-symbol publication occurs here.
void execute_drift_search_job(const FftSearchBatch&,const FftSearchJob&,
                             std::span<FftSearchScore>,std::span<FftComplex> product,
                             std::span<FftDriftAccumulator>,PatternCode&,std::stop_token = {},
                             std::span<DifferentialAccumulator> = {});

// Build exactly the unmodulated public-pattern values used by nominal-clock
// jobs. The caller owns and budgets the complete supplied reference span.
void prepare_fft_nominal_reference(const FftSearchGeometry&,PatternCode&,
                                   std::span<std::array<FftComplex,2>>,std::stop_token = {});

void pattern_fft(std::span<FftComplex>,bool inverse,std::stop_token);
void pattern_fft(std::span<FftComplex>,bool inverse,std::stop_token,SearchArithmetic);
FftComplex pattern_fft_product(FftComplex,FftComplex,SearchArithmetic);
double pattern_evidence(FftComplex dot,double energy,double template_energy,double count,
                        double condition,bool real_rank,bool exact_real,
                        FftComplex template_square = {});
double pattern_explained(FftComplex dot,double template_energy,double condition,
                        bool exact_real,FftComplex template_square = {});

// Whitening uses the exact quadrature covariance of the disjoint real-sample
// projection bins. The common input-noise variance cancels in the detector.
double pattern_projection_image_ratio(std::uint64_t bin_samples,double carrier_hz,std::uint32_t sample_rate);
FftComplex pattern_differential_whiten(FftComplex dot,double template_energy,
                                      FftComplex template_square,double projection_image_ratio);

} // namespace datapump::modem::detail

// Bounded paired-template correlation in borrowed FFT scratch.
namespace datapump::modem::detail::partitioned_paired {
inline constexpr std::size_t direct_arithmetic_stack_bytes=64*1024;
inline constexpr std::size_t integer_direct_start_limit=32;
struct Requirements {
    std::size_t tile_size=0,transform=0,length=0,chunks=0;
    std::size_t first_input_tile=0,input_tiles=0,max_job_output_tiles=0;
    std::size_t complex_count=0;
};
struct Work {
    std::uint64_t input_transforms=0,template_transforms=0,inverse_transforms=0;
    std::uint64_t complex_products=0,template_values=0,selected_starts=0;
    // Integer dots count bounded tile calls; reject/refine count bit candidates.
    std::uint64_t int8_dots=0,certified_rejects=0,exact_refines=0,fp32_dots=0;
    std::uint64_t precision_fallback_jobs=0,native_fp32_transforms=0,tabulated_fp64_transforms=0;
};
// No allocation or mutation. Returns empty for unsupported geometry or arena
// bound, throws for invalid selected ranges/incomplete observations. All jobs
// must use generated coherent non-sample-fit templates. Bounds are numeric
// scratch only; PatternCode, output row and fixed Context control are caller-owned.
std::optional<Requirements> preflight(const FftSearchBatch&,std::span<const FftSearchJob>,
                                      std::size_t tile_size,std::size_t available_complex);
// Shared runtime/planner numeric-work crossover. The private template cost is
// omitted from both sides, conservatively ignoring paired construction savings.
// Returns empty when the full FFT has lower estimated total numeric work.
long double operations(const Requirements&,std::size_t jobs);
std::optional<Requirements> choose(const FftSearchBatch&,const FftSearchJob&,
    std::size_t jobs,std::size_t original_transform,std::size_t available_complex);
// Size-only equivalent after runtime eligibility/observation validation; no dummy
// sample storage is required by the planner. Ranges are relative to segment origin.
std::optional<Requirements> choose_geometry(std::size_t length,std::size_t starts,
    std::span<const FftStartRange>,std::size_t jobs,std::size_t original_transform,
    std::size_t available_complex);
struct Context {
    const FftSearchBatch* batch=nullptr;
    Requirements geometry;
    std::span<FftComplex> first,second;
    // Caller reserves these optional allocations in addition to borrowed scratch.
    // No implicit aliasing of double objects as floats and no shifted PCM matrix.
    std::vector<std::complex<float>> native;
    std::optional<search_fft::FloatPlan> native_plan;
    std::optional<search_fft::DoublePlan> double_plan;
    SearchArithmetic execution=SearchArithmetic::fp64;
    std::size_t extra_bytes()const {
        return native.capacity()*sizeof(std::complex<float>)+
            (native_plan?native_plan->working_bytes():0)+(double_plan?double_plan->working_bytes():0);
    }
};
std::size_t native_bytes(const Requirements&,SearchArithmetic);

// Cache input tiles once for the complete preflight job set. Borrowed arena and
// immutable batch/views must outlive Context. Both spans must have P-multiple
// lengths, be disjoint, and not alias observations/energy/nominal/ranges.
// Unused spectrum/carrier_square views may alias borrowed scratch. False
// fallback leaves arena untouched. Cancellation throws and context is discarded.
std::optional<Context> prepare(const FftSearchBatch&,const Requirements&,
                               std::span<FftComplex> first,std::span<FftComplex> second,
                               Work* = nullptr,std::stop_token={},std::size_t additional_bytes=0);
inline std::optional<Context> prepare(const FftSearchBatch& batch,const Requirements& r,
        std::span<FftComplex> arena,Work* work=nullptr,std::stop_token stop={},std::size_t additional_bytes=0) {
    return prepare(batch,r,arena,{},work,stop,additional_bytes);
}
// Synchronous job-only numeric operation. Output real=zero score, imag=one score,
// at ORIGINAL selected starts; excluded cells/padding remain untouched. No
// caller publication until successful return. False means fallback before any
// output mutation. A cancellation/error may leave partial output, which caller
// discards exactly as for the existing CPU batch backend. Output must not alias
// arena or immutable batch input views. Code identity must exactly match batch geometry.
bool score_job(Context&,const FftSearchJob&,PatternCode&,std::span<FftComplex> output,
               Work* = nullptr,std::stop_token={});
// No FFT, no allocation, no arena. Generated coherent jobs with at most 32
// selected original starts. Two private candidates are generated together once
// per bin; each norm and each complex dot retains ascending sample order.
// The same output, alias, fallback and cancellation rules apply as score_job.
// A host may omit input FFT entirely when all jobs use this entry point.
bool score_direct(const FftSearchBatch&,const FftSearchJob&,PatternCode&,
                  std::span<FftComplex> output,Work* = nullptr,std::stop_token={});
// Fixed requested arithmetic. INT8 is a certified screening calculation followed
// by original FP64 refinement for every potentially retained candidate; its
// scores may be zero only when BOTH are below retain_floor; otherwise BOTH
// original scores are refined. FP32 has unquantized float operands.
// Each borrowed span holds one original paired-template row (L Complex values).
// Insufficient scratch falls back to the original FP64 direct calculation and
// increments precision_fallback_jobs. Unsafe aliases return false untouched.
// Bounded integer scratch uses a conservative 64 KiB call-stack reservation.
// Optional packed input/template storage is supplied and budgeted by the host.
bool score_direct(const FftSearchBatch&,const FftSearchJob&,PatternCode&,
                  std::span<FftComplex> output,std::span<FftComplex> first,
                  std::span<FftComplex> second,Work* = nullptr,std::stop_token={});
}
