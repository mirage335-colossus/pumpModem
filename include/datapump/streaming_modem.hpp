#pragma once
#include "datapump/modem.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/transmit_trace.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <random>

namespace datapump::modem {
// A uniformly timed complex baseband integration, with no framing labels.
struct SymbolObservation {
    std::complex<double> value;
    std::uint64_t sample_count = 0;
};
// Newly observed payload physical baseband chip I/Q for pattern transport.
// Points are never snapped to decisions. A slow
// consumer receives the newest bounded set and a count
// of points displaced before it could consume them.
struct ConstellationBatch {
    std::vector<std::complex<double>> points;
    std::uint64_t dropped = 0;
};
// Unframed binary input: one meaningful 0/1 bit per element, including leading
// zeros. Every symbol carries exactly one bit, including the last symbol.
struct RawBits { Bytes bits; };
// Continuous random bits through the ordinary encrypted pattern modem, with
// a private temporary key per start and no message contents or framing.
// Zero symbols is open-ended tuning; a finite fixture/request shares the
// ordinary complete burst endpoint. This is local source control, never framing.
struct Noise { std::uint64_t symbols=0; };

class StreamingTransmitter {
public:
    static constexpr std::size_t analytic_preview_limit=2112;
    static constexpr std::size_t constellation_history_limit=2048; // Minimum retained history.
    static constexpr unsigned constellation_frame_rate=60;
    // Preserve the existing narrow-band history and at least one nominal
    // bitmap frame of physical chips, including a chip already in progress.
    static std::size_t constellation_history_capacity(const Config& config);
    // Both input forms include the rounded settling prefix and an exact
    // protected suppression-noise tail (three useful seconds plus shaping support), outside all payload symbol slots.
    StreamingTransmitter(Bytes wire, Config config, std::size_t workspace_bytes = 8 * 1024 * 1024);
    StreamingTransmitter(RawBits bits, Config config, std::size_t workspace_bytes = 8 * 1024 * 1024,
                         TransmitTrace trace = {});
    StreamingTransmitter(Noise, Config config, std::size_t workspace_bytes = 8 * 1024 * 1024);
    // Fresh temporary roots must be prepared once BEFORE UTC burst scheduling.
    struct PreparedTemporaryKeys {};
    static Config prepare_noise_config(Config config);
    StreamingTransmitter(Noise, Config config, PreparedTemporaryKeys,
                         std::size_t workspace_bytes = 8 * 1024 * 1024);
    ~StreamingTransmitter();
    StreamingTransmitter(StreamingTransmitter&&) noexcept;
    StreamingTransmitter& operator=(StreamingTransmitter&&) noexcept;
    std::size_t read(std::span<float> output, std::stop_token stop = {});
    // The same sampled waveform before real projection, for physical channel
    // resampling and carrier rotation. Carries no symbol/framing metadata.
    std::size_t read_analytic(std::span<std::complex<double>> output,
                              std::stop_token stop = {});
    // Removed integrated-symbol interface: throws; use physical samples.
    std::optional<SymbolObservation> next_symbol(std::stop_token stop = {});
    // Source-thread request: retain already-emitted pulse contributions and
    // finish protected cover and finite tails. A stopped read token remains immediate abort.
    void request_finish();
    std::uint64_t original_total_samples() const;
    std::uint64_t original_source_total_samples() const;
    bool finished() const;
    std::uint64_t total_samples() const;
    std::uint64_t samples_emitted() const;
    std::uint64_t physical_output_origin() const;
    std::uint64_t physical_valid_payload_end() const;
    // Nonmutating forecast at a supplied physical PCM frontier, including
    // already-generated samples; excludes the independent suppression drain.
    std::uint64_t predicted_physical_valid_payload_end(std::uint64_t frontier) const;
    std::uint64_t predicted_physical_stop_end(std::uint64_t frontier) const;
    std::uint64_t source_total_samples() const;
    std::uint64_t source_samples_emitted() const;
    std::uint64_t source_valid_payload_end() const;
    // Transmitter-owned exact exposure accounting; no profile/calendar setup.
    std::uint64_t emitted_payload_epoch() const;
    // Retained transmitter state, independent of streamed waveform duration.
    std::size_t working_bytes() const;
    // Actual payload points whose transmission has begun, oldest first.
    // Retains physical baseband chips, including all enabled waveform masks.
    // Settling and suppression noise are excluded.
    // Each point is retained once, regardless of PCM block size or duration.
    std::vector<std::complex<double>> payload_constellation() const;
    ConstellationBatch take_payload_constellation();
    // Actual generation prefix; previews do not advance it. Source preparation
    // can be visible before read(), wire/chip rows require begun payload slots.
    TransmitTrace transmit_trace() const;
    // Reconstruct the actual recent PCM, including spreading and carrier phase.
    // At most 2048 samples; any portion before the transmission is zero-filled.
    void preview_last(std::span<float> output) const;
    // Same recent waveform before projecting its analytic carrier to real PCM;
    // up to analytic_preview_limit samples, including interpolation margins.
    void preview_last_analytic(std::span<std::complex<double>> output) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class StreamingReceiver {
public:
    StreamingReceiver(Config config, std::size_t workspace_bytes = 8 * 1024 * 1024,
                      PatternSearch pattern_search = {});
    ~StreamingReceiver();
    StreamingReceiver(StreamingReceiver&&) noexcept;
    StreamingReceiver& operator=(StreamingReceiver&&) noexcept;
    // Feed physical PCM, optionally with its shared carrier projection.
    Bytes push(std::span<const float> samples, std::stop_token stop = {});
    PatternCompactAcquisitionState compact_acquisition_state() const noexcept;
    std::span<const PatternFrequencyRateHypothesis> compact_acquisition_hypotheses() const noexcept;
    bool set_compact_acquisition_prior(double frequency_origin_hz,std::span<const std::uint8_t> mask);
    bool clear_compact_acquisition_prior() noexcept;
    bool start_compact_acquisition_audit() noexcept;
    bool cold_acquisition_only() const noexcept;
    PatternAcquisitionDispatchState helper_acquisition_dispatch() const noexcept;
    bool set_helper_acquisition_dispatch(bool permitted) noexcept;
    // Update only between joined calls, from the current qualified reference.
    // The first set builds bounded helper-only geometry paid from this loan;
    // subsequent measurement updates allocate nothing. No basis translation,
    // clock pruning or automatic broad audit.
    // Established anchors/dependencies and ordinary physical END remain active.
    bool set_helper_frequency_window(double center_hz,double half_width_hz,double clock_coefficient_hz=0) noexcept;
    // Independently measured ORIGINAL DATA LF interval. Unknown/stale clock
    // uses the three-argument setter to restore all original LF immediately.
    bool set_helper_frequency_window(double center_hz,double half_width_hz,double clock_coefficient_hz,
                                    std::optional<PatternHelperClockInterval> clock_interval) noexcept;
    void clear_helper_frequency_window() noexcept;
    PatternHelperFrequencyWindowState helper_frequency_window() const noexcept;

    bool set_compact_timing_prior(const PatternPeriodicTimingPrior&) noexcept;
    bool clear_compact_timing_prior() noexcept;
    bool compact_phase_assistance_supported() const noexcept;
    bool set_compact_phase_view(const PatternPhaseCurve*) noexcept;
    bool compact_phase_view_active() const noexcept;
    PatternAcquisitionState acquisition_state() const noexcept;
    std::optional<PatternAcquisitionWorkBound> acquisition_work_bound() const noexcept;
    std::optional<PatternAcquisitionScoredSupport> acquisition_scored_support() const noexcept;
    std::optional<PatternContinuationScoredSupport> continuation_scored_support(std::size_t incoming_samples=0) const noexcept;
    std::span<const PatternFrequencyRateHypothesis> acquisition_hypotheses() const noexcept;
    bool set_acquisition_prior(double,std::span<const std::uint8_t>);
    bool clear_acquisition_prior();
    bool set_helper_gap_protection(bool qualified) noexcept;
    std::size_t observe_acquisition(std::span<const float>,std::stop_token = {});
    std::size_t observe_acquisition(std::span<const float>,const PatternContinuationFrequencyPrior&,
        PatternContinuationFrequencyResult&,std::stop_token = {});
    bool process_prior_acquisition(std::stop_token = {});
    bool process_assisted_acquisition(const PatternAcquisitionAssistance&,std::stop_token = {});
    bool process_prior_acquisition(const PatternPeriodicTimingPrior&,std::uint64_t evidence_sample,
                                   PatternAcquisitionTimingResult&,std::stop_token = {});
    bool process_prior_acquisition(const PatternAcquisitionClockPrior&,PatternAcquisitionClockResult&,
                                   std::stop_token = {});
    bool process_prior_acquisition(const PatternAcquisitionClockPrior&,PatternAcquisitionClockResult&,
                                   const PatternPeriodicTimingPrior&,std::uint64_t timing_evidence_sample,
                                   PatternAcquisitionTimingResult&,std::stop_token = {});
    void resolve_acquisition(bool conditional_link,std::stop_token = {});
    void resolve_acquisition(bool conditional_link,const PatternContinuationFrequencyPrior&,
        PatternContinuationFrequencyResult&,std::stop_token = {});
    void process_acquisition(std::stop_token = {});
    bool set_acquisition_phase_view(const PatternPhaseCurve*) noexcept;
    bool acquisition_phase_view_active() const noexcept;
    PatternFftWork fft_work() const;
    std::optional<PatternSourceIdentity> source_identity(const PatternBurst&)const;
    // Joined native query; never feeds, confirms, reinterprets or fabricates END.
    std::optional<PatternPrefixSupport> prefix_support_before_last(
        std::uint64_t first_sample,std::uint64_t first_symbol)const noexcept;

    bool phase_assistance_supported() const noexcept;
    std::size_t phase_assistance_bin_samples() const noexcept;
    bool push_phase_assisted(std::span<const float>,std::span<const PatternPhaseKnot>,
                             std::uint64_t maximum_gap_samples,std::stop_token = {});
    bool suspend_helper_acquisition();
    Bytes push(std::span<const float> samples, std::span<const std::complex<double>> projected,
               std::stop_token stop = {});
    Bytes push(std::span<const float> samples, std::span<const std::complex<double>> projected,
               std::stop_token stop,detail::CorrelationProjectionCache*);
    // Removed integrated-symbol interface: throws; use physical samples.
    Bytes push_symbols(std::span<const SymbolObservation> observations, std::stop_token stop = {});
    // Finish a finite capture without inserting silence or extra symbols.
    // Only sufficiently supported symbols are flushed. Their chunks remain
    // incomplete unless sampled search absence already established stream end.
    // No synthetic tail samples or bits are inserted.
    Bytes finish(std::stop_token stop = {});
    // Advance only physical intervals excluded by every scheduled hypothesis;
    // rejected skips leave state untouched and must be fed as ordinary PCM.
    bool try_skip_unobserved(std::uint64_t samples, std::stop_token stop = {});
    std::optional<std::uint64_t> unobserved_end_from(std::uint64_t local_sample) const;
    std::optional<ScheduledCaptureEnvelope> scheduled_capture_envelope(std::uint64_t local_sample) const;
    void suspend_fresh_acquisition();
    bool symbol_burst_tracking_active() const;
    bool synchronized() const;
    // Complete sampled acquisition coverage of the original finite start
    // window, independent of admission and physical message completion.
    bool initial_search_complete() const;
    std::uint64_t initial_fft_input_samples() const noexcept;
    // Pattern candidates are accumulating.
    bool acquiring() const;
    // Bounded previews and drainable immutable decision chunks. Only observed
    // six-second search absence sets complete; EOF returns incomplete chunks.
    PatternBurst provisional_pattern() const;
    std::vector<PatternBurst> take_pattern_bursts();
    bool retains_pattern_stream(std::uint64_t first_sample, std::uint64_t first_symbol) const;
    // Receiver-worker only: score retained observations with cancellable bounded batching.
    // Publication polls never perform the deferred search or manufacture input.
    void process_pending(std::stop_token = {});
    bool deferred_pattern_history() const noexcept;
    std::uint64_t diagnostic_history_generation() const noexcept;
    std::uint64_t diagnostic_history_resolved_through() const noexcept;
    std::uint64_t oldest_unresolved_diagnostic_event() const noexcept;

    // Receiver-local original completion-event commit frontier. This does not
    // certify that uncompleted-symbol replay arithmetic has already executed.
    std::optional<std::uint64_t> delayed_processing_frontier() const noexcept;
    // Joined query in receiver-LOCAL input coordinates; conservative support
    // lower bound only, never a capture/frontier, timing, phase or END claim.
    // Zero can retain old support; nullopt means unsupported/unknown.
    std::optional<std::uint64_t> earliest_future_frequency_scoring_sample() const noexcept;
    std::vector<PatternEvidence> pattern_candidates() const;
    std::vector<PatternEvidence> pattern_candidates(std::size_t limit) const;
    std::vector<PatternEvidence> pattern_candidates(std::size_t limit, std::stop_token) const;
    bool clock_windowed() const;
    bool local_clock_fallback() const;
    bool candidate_limited() const;
    Diagnostics diagnostics() const;
    // Physical chip observations for provisional and synchronized pattern fits.
    // Switching tentative fits does not replay already published samples.
    // Reset discards pending points and their overflow count.
    ConstellationBatch take_payload_constellation();
    std::size_t working_bytes() const;
    std::size_t reserved_workspace_bytes() const;
    // Joined native cold decoder/configured-bit floor; no worker grant.
    // The returned actual bound includes the outer StreamingReceiver shell.
    std::optional<PatternColdWorkspaceFloor> cold_workspace_floor() const noexcept;
    // Pure pristine split-observation bound; includes this outer shell. The
    // returned execution argument must be funded in full before any scoring.
    std::optional<PatternColdObservationStorage> cold_observation_storage() const noexcept;
    std::optional<HelperReplayBudget> helper_replay_budget(std::size_t future_samples,
        const PatternSearch* reset_search=nullptr) const;
    // Additional allocation bound for one finite PCM push and its next drain.
    // SIZE_MAX disables optional shared preprocessing for unsupported geometry.
    std::size_t projection_cache_headroom(std::size_t input_samples) const;
    // No allocation: nullopt unsupported, zero eligible but cold/reselecting,
    // positive only after the original worker and Temporal choices are retained.
    std::optional<std::size_t> temporal_plan_workers() const;
    // Shared receiver banks can lend unused DSP space to an active recording.
    // The limit cannot be reduced below storage already in use.
    void set_workspace_bytes(std::size_t bytes);
    void trim_optional_workspace();
    void reset();
private:
    friend class detail::NativePeakLease;
    friend class detail::NativePointLease;
    friend struct detail::NativeDiagnosticHistoryAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

SymbolObservation add_awgn(SymbolObservation observation, double sample_snr_db,
                           std::mt19937_64& random);
}
