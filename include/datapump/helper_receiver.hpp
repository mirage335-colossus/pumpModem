#pragma once

#include "datapump/helper_metadata.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace datapump::helper {

// A measured reference in the caller's absolute capture sample coordinates.
// Phase is the observed tone phase at sample, with a known punctuation sign
// removed after timing acquisition. It retains an unknown propagation/Tx phase.
// sample is also the latest raw observation used for this publication: no
// future input is hidden behind an earlier phase timestamp. Dwell timing is a
// modulo-400 ms measurement, not an authenticated clock.
// A metadata publication may propagate an older fitted phase to sample.
// phase_observation_sample independently dates that actual fitted phase; its
// known flag distinguishes an observation at sample zero from unknown origin.
// Propagation and frequency fusion must not manufacture a new phase anchor.
// Tone uncertainty combines an eight-standard-error local linearized LS term
// (actual finite Gram covariance, estimated white-noise residual), numerical
// floors and measured polynomial-model allowance. It is not a deterministic
// bound for interference or future oscillator drift; callers retain fallback.
// The configured process allowance separately encloses the finite raw-fit
// change caused by a uniformly bounded Wiener phase path. It does not turn
// the local AWGN linearization into a global likelihood/confidence interval.
enum class FrequencyFit { unknown, local_polynomial, conditioned_polynomial, masked_tone_profile };
// The former single-tone component fixtures retain their paired reversal.
// Production optional metadata uses the separate auxiliary, whose240ms
// reversal stays inverted until the zero-amplitude dwell boundary.
enum class PunctuationFormat { paired_legacy, auxiliary_single };

// One ACTUAL independently qualified phase-flip fit, not an extrapolated dwell
// grid. Coordinates use Reference.sample_rate. The immutable last_sample is
// also its actual raw evidence date; later tone/metadata publications may carry
// this same observation but must not renew it. (generation,sample) identifies
// the observation ONLY within one caller-owned provider/capture domain.
// Generation is local to a Receiver instance; reconstruction requires a new
// provider identity and explicit caller history reset. Receiver::reset changes
// the local generation. An actual unknown-phase RF retune also resets its gauge.
struct PunctuationObservation {
    std::uint64_t generation=0,sample=0,first_sample=0,last_sample=0;
    double fraction=0,uncertainty_samples=0;
    bool known=false;
};
static_assert(sizeof(PunctuationObservation)<=56);

struct Reference {
    std::uint64_t sample=0;
    std::uint32_t sample_rate=0;
    double frequency_hz=0,phase_radians=0;
    double frequency_uncertainty_hz=0,phase_uncertainty_radians=0;
    double dwell_start_seconds=0,dwell_uncertainty_seconds=0;
    bool frequency_valid=false,phase_valid=false,timing_valid=false,phase_observation_known=false;
    std::optional<Metadata> metadata;
    // Independent frequency component's last observation; zero means sample.
    // Phase remains evaluated at sample. A fused component is never from the
    // future and callers must apply their frequency-age limit to this field.
    std::uint64_t frequency_sample=0;
    std::uint64_t phase_observation_sample=0;
    // Actual raw support of the selected frequency component. The endpoint is
    // frequency_sample+1 (or sample+1 for a same-date legacy component).
    // The fitted kernel is a local linearization of the ordinary six-column
    // real-tone fit, not an instantaneous frequency observation.
    std::uint64_t frequency_first_sample=0;
    FrequencyFit frequency_fit=FrequencyFit::unknown;
    double frequency_phase_diffusion_gain=0; // diagnostic fitted-kernel Hz*sqrt(second)/radian
    double frequency_process_uncertainty_hz=0; // already included in the CI
    PunctuationObservation punctuation;
};

// Fixed public occupied-band geometry in the physical PCM clock. Conditioning
// is optional and refines only already accepted raw tone publications. A band
// that is invalid, too wide, or overlaps the helper search retains the raw fit.
struct OccupiedBand {double lower_hz=0,upper_hz=0;};

struct FrequencyPrecisionOptions {
    // No allocation occurs here: use set_optional_frequency_work explicitly.
    std::size_t workspace_grant=0;
    double maximum_audio_clock_ppm=1000;
    // Already combined for the configured link topology, in radians/sqrt(s).
    // The model applies to strong references even when workspace_grant is zero.
    double phase_noise_radians_per_sqrt_second=0;
};

// Optional precision work is separately funded and may be reclaimed before any
// mandatory payload allocation. The prior is declared from the physical link
// model, centered on immutable expected_hz; it must be a subset of the original
// constructor band and is never selected from an observed peak.
struct FrequencyWorkOptions {
    std::uint32_t sample_rate=0;
    std::size_t workspace_grant=0;
    std::optional<double> physical_prior_half_width_hz;
    // This model requires common phase ACROSS dwells, as well as constant
    // gain within each clean dwell group. Fixed-frequency coherent radios and
    // the sampled Generator may provide it. Native retuning with unknown
    // per-hop phase does not provide this certificate.
    // This optional profile supports 24/48/48.96kHz physical PCM geometry.
    // Known Generator envelopes and one continuous burst are assumed. Notify
    // actual absence/reset with clear_optional_frequency_history before reuse.
    bool coherent_phase_model=false;
};
enum class FrequencyWorkState { disabled, ingested_support, frozen_work, all_cells_complete, analysis_declined, publication_qualified };
enum class FrequencyWorkDecline { none, grant_unavailable, coherence_unavailable, unsupported_geometry,
    warming_aperture, no_new_opportunity, analysis_incomplete, no_reference, aliases_remain,
    stale_support, presence_unqualified, generation_changed };
struct FrequencyWorkDescriptor {
    // actual_bytes includes heap plus separately charged optional worker scratch.
    // The base Receiver 9KiB remains charged independently during overlap.
    std::size_t required_bytes=0,actual_bytes=0,heap_bytes=0,worker_scratch_bytes=0;
    std::size_t processed_cells=0,total_cells=0,processed_work_units=0;
    std::size_t maximum_work_units_per_advance=8,maximum_shape_subsets_per_mask=326;
    std::uint32_t sample_rate=0;
    std::uint64_t generation=0,capture_revision=0,look_ordinal=0,opportunity_ordinal=0;
    // Frozen support is inclusive; capture_next_sample is the raw frontier.
    std::uint64_t first_sample=0,end_sample=0,capture_next_sample=0;
    // A separate observed-presence test, evaluated on the indicated recent
    // interval. Selected bins can be non-contiguous; exact envelope support
    // is dated separately. No support is redated to commit/capture frontier.
    std::uint64_t presence_interval_first_sample=0,presence_interval_end_sample=0;
    std::uint64_t presence_first_sample=0,presence_end_sample=0;
    double presence_snr_db=0,presence_noise_bandwidth_hz=10;
    bool recent_presence_qualified=false;
    double aperture_seconds=0,look_cadence_seconds=.4,maximum_support_age_seconds=1;
    double physical_prior_half_width_hz=0;
    FrequencyWorkState state=FrequencyWorkState::disabled;
    FrequencyWorkDecline decline=FrequencyWorkDecline::grant_unavailable;
};
// Pure planning geometry. This reports configured support and a fixed optional
// startup policy; it never predicts or qualifies a received reference. The
// ten-second cold deadline starts at the first retained sample and is never
// renewed. At expiry the caller must release every staged sample to ordinary
// broad processing; this pure descriptor does not perform staging or expiry.
struct FrequencyWorkGeometry {
    bool supported=false,eligible=false;
    std::uint32_t sample_rate=0;
    std::size_t bin_samples=0,aperture_bins=0,required_bytes=0,heap_bytes=0,worker_scratch_bytes=0;
    double aperture_seconds=0,look_cadence_seconds=.4,maximum_support_age_seconds=1;
    double maximum_supported_fit_seconds=8.0,startup_support_seconds=0,cold_staging_deadline_seconds=10;
    double physical_prior_half_width_hz=0;
    FrequencyWorkDecline decline=FrequencyWorkDecline::unsupported_geometry;
};
enum class FrequencyPresenceState { unavailable, present, absent };
struct FrequencyPresenceCertificate {
    std::uint64_t capture_revision=0,generation=0,observed_sample=0;
    FrequencyPresenceState state=FrequencyPresenceState::unavailable;
};
struct FrequencyWorkAdvance { std::size_t processed_cells=0,processed_work_units=0;bool complete=false; };

// Independent assistance, never payload completion or presence admission.
// Strong coherent fits retain phase/metadata measurement. Optional long-profile
// frequency inversion preserves every compatible frequency/mask cell. Its
// configured CW/AWGN/Wiener model has lifetime coverage spending; unsupported
// fading, unannounced phase resets or insufficient precision yield to payload.
class Receiver {
public:
    // Receiver-owned storage plus project scratch; the pre-existing external
    // C/C++/libm/libgcc/OpenSSL runtime stack/provider allocations are separate.
    static constexpr std::size_t workspace_bound=9*1024;
    static constexpr std::size_t optional_frequency_worker_scratch_bound=8*1024;
    // Only the first receive key is selected for compact encrypted time. No
    // helper metadata is authenticated or used here to adjust a clock/grid.
    explicit Receiver(double expected_hz,double search_half_width_hz=20,
                      std::vector<Crypto> keys={},
                      std::optional<OccupiedBand> ordinary_band={},
                      FrequencyPrecisionOptions precision={},
                      PunctuationFormat punctuation=PunctuationFormat::paired_legacy);
    ~Receiver();
    Receiver(Receiver&&) noexcept;
    Receiver& operator=(Receiver&&) noexcept;
    Receiver(const Receiver&)=delete;
    Receiver& operator=(const Receiver&)=delete;
    void push(std::span<const float>,std::uint32_t rate,std::uint64_t first_sample);
    // A decoded metadata publication survives later tone-only fits in the same
    // push, and can consequently be older than the latest coherent tone fit.
    std::optional<Reference> take_update();
    void reset();
    // Owner operations push/reset/take_update must be serialized with each
    // other; that set may overlap ONE advance while object/profile lifetime
    // stays fixed. Reset invalidates generation without touching frozen work.
    // All methods below, plus move/destruction, require advance joined and
    // owner serialization (the static size query is independent). No broader
    // thread-safety is implied. Allocation happens only in the setter.
    static std::size_t optional_frequency_workspace_required() noexcept;
    static FrequencyWorkGeometry optional_frequency_geometry(double expected_hz,double constructor_half_width_hz,
        const FrequencyWorkOptions&,FrequencyPrecisionOptions = {}) noexcept;
    bool set_optional_frequency_work(const FrequencyWorkOptions&);
    void release_optional_frequency_work();
    void clear_optional_frequency_history();
    FrequencyWorkDescriptor optional_frequency_work() const;
    // One pending look, no queue. Every fixed 400ms opportunity spends its
    // lifetime ordinal even when skipped or canceled. prepare copies bounded
    // bin history; advance does no allocation and scores at most max_cells
    // complete precision cells and eight bounded work stages. A call may
    // complete zero cells: each recent-presence stage tests one fixed-domain
    // cell; each shape stage handles one prefix, one mask Schur construction,
    // or one of <=326 dirty-group deletions. No unfinished scan publishes.
    // If EVERY recent-presence cell fails, the look declines before precision
    // inversion; the pending completed look has zero processed_cells and
    // state analysis_declined until commit consumes that pending result.
    bool prepare_optional_frequency(std::uint64_t capture_revision);
    FrequencyWorkAdvance advance_optional_frequency(std::size_t max_cells);
    // The completed profile must qualify its separately tested recent1.6s
    // presence interval (6dB in10Hz, full-search noise/lifetime spending).
    // It is bounded-stale evidence at the frozen endpoint, not instantaneous
    // presence. A known newer measured absence vetoes; unknown/older external
    // observations provide no authority. No partial scan publishes.
    // Commit changes only frequency provenance;
    // phase, metadata, payload progress and completion have no authority here.
    bool commit_optional_frequency(const FrequencyPresenceCertificate&);
private:
    friend class AuxiliaryMetadataReceiver;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Optional strong/local metadata only. Discovery uses a filtered diagnostic
// copy; each complete dwell then fits independent pilot/auxiliary phasors on
// its straight flank and removes only that fitted pilot from bounded raw PCM.
// No phase continuity across dwells, decoded time, or payload authority follows.
class AuxiliaryMetadataReceiver {
public:
    static std::size_t workspace_required(std::uint32_t rate);
    AuxiliaryMetadataReceiver(std::uint32_t rate,double auxiliary_hz,double pilot_hz,
        std::vector<Crypto> keys,FrequencyPrecisionOptions,std::size_t workspace_grant,
        std::uint64_t first_sample=0);
    ~AuxiliaryMetadataReceiver();
    AuxiliaryMetadataReceiver(AuxiliaryMetadataReceiver&&) noexcept;
    AuxiliaryMetadataReceiver& operator=(AuxiliaryMetadataReceiver&&) noexcept;
    void push(std::span<const float>,std::uint64_t first_sample);
    // At most one latest complete400ms dwell, no work queue. Owners may omit
    // this call to yield optional decoding; expired raw support is discarded.
    bool advance();
    // Metadata is dated at the consumed raw frontier. All frequency, phase and
    // timing authority flags are false; frequency_hz is diagnostic auxiliary Hz.
    std::optional<Reference> take_update();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
