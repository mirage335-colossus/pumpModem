#pragma once
#include "datapump/audio.hpp"
#include "datapump/transfer.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/transmit_trace.hpp"
#include "datapump/wrong_key_admission.hpp"
#include <array>
#include <memory>
#include <utility>

namespace datapump::live {
// Both ends configure the same calendar independently. Its ordinal advances
// through quiet slots as well as DATA. A later capture can join this calendar.
struct Calendar {
    std::uint64_t epoch = 0, initial_ordinal = 0;
};

enum class DataCadence : std::uint8_t { scheduled, continuous };

struct Settings {
    transfer::Options transfer;
    std::string device="default";
    bool mono=true,simulation=false,full_duplex=false,exclusive=false;
    // Trusted one-message device provider: wait for scored quiet rearm and
    // publish a bounded physical capture endpoint before exposing TX samples.
    // Ordinary device and simulation sessions retain their existing behavior.
    bool finite_duplex=false;
    // Local processing choice; it does not change the on-air profile.
    bool gpu=false;
    // Reversible browser filter; the GUI receiver always preserves Current
    // baseline decisions and the original decoded content.
    std::string wrong_key_admission="current";
    audio::ChannelMode channel_mode=audio::ChannelMode::left_mono;
    double transmit_gain=1;
    helper::Options helper;
    std::optional<Calendar> calendar;
    // One-sided audio input density, in squared normalized PCM per Hz.
    // The live scorer requires a separately declared input noise model.
    std::optional<double> live_noise_dbfs_hz;
    std::optional<std::size_t> receiver_memory_limit;
    double tx_dbm=20, path_loss_db=120, noise_dbm_hz=-174, target_cn0_db_hz=32;
    double doppler_shift_hz=0;
    std::string spread_preset="none", selective_perturbation="off";
    unsigned added_symbols=1, symbol_burst_wait=0, symbol_burst_random=0;
    // Public calendar choice shared by the transmitting and receiving peers.
    DataCadence data_cadence=DataCadence::scheduled;
    bool added_interleave=false;
    std::string fhss_profile="off";
    std::vector<double> fhss_frequencies_hz;
    // Adjacent DATA carrier centers are separated by at least this many Hz.
    double minimum_carrier_spacing_hz=0;
};
void validate_settings(const Settings&);
// Compile-time capability only. Device initialization stays on the session
// manager, so GUI availability checks never open a GPU or block on a driver.
bool gpu_receiver_available() noexcept;
enum class SimulationInputResult : std::uint8_t { none, match, mismatch, incomplete };
struct SignalUpdate {
    std::uint64_t id=0;
    double frequency_hz=0;
    std::string text;
    bool validated=false;
    std::string reception_id;
    std::optional<double> preamble_received_percent;
    std::optional<StreamBitAccuracy> pre_fec_accuracy;
    bool binary=false,complete=false;
    std::size_t received_bits=0,expected_bits=0,missing_symbols=0;
    std::optional<double> pattern_score;
    StreamFecStats fec_stats;
    std::string raw_bits;
    std::uint64_t revision=0;
    bool simulation_raw_input=false;
    std::vector<std::uint64_t> superseded_ids;
    transfer::RecoveryProgress recovery_progress;
    bool helper_carrier=false;
    bool robust_reception=false;
    triad::v3::WrongKeyAdmissionEvidence wrong_key_evidence;
    // Comparison against local input for a finite raw-bit or short-text simulation.
    SimulationInputResult simulation_input_result=SimulationInputResult::none;
};
enum class ConstellationSource {input,transmitted,received};
enum class PatternDecision { ambiguous, data, quiet };
// Bounded diagnostic rows from actual receive processing in public-slot order.
struct PatternStep {
    std::uint64_t index=0, epoch=0, ordinal=0;
    PatternDecision decision=PatternDecision::ambiguous;
    std::optional<std::uint8_t> bit;
    bool adaptive_diagnostics=false;
    std::array<double,2> bit_evidence{}, adaptive_bit_evidence{}, combined_bit_evidence{};
    // Actual bit alternatives, including all operational channel-model and
    // public-coordinate priors. The affine diagnostics above can disagree.
    std::array<double,2> operational_bit_evidence{};
    double quiet_energy=0, quiet_threshold=0, operational_log_e=0, operational_threshold=0;
    double operational_bit_log_separation=0, operational_minimum_bit_log_separation=0;
    std::string wrong_key_admission="current";
    // Browser-only admission views rated from the untouched baseline score.
    // This is per-slot display evidence and never changes the detector result.
    std::optional<std::array<bool,triad::v3::wrong_key_admission_count>> browser_admission;
    double wrong_key_required_log_separation=0;
    bool wrong_key_rejected=false;
    double helper_seconds=0, coarse_seconds=0, fine_seconds=0;
    // Helper acquisition ran beside coarse preparation, inside coarse_seconds.
    bool helper_overlapped=false;
    std::size_t quiet_modes=0, fine_candidates=0;
    std::size_t coarse_workspaces=0, coarse_frequency_points=0, coarse_arrival_points=0;
    double coarse_projected_rate_hz=0;
    unsigned workers=0;
    bool evidence_complete=false;
    std::string helper_status, varying_status, drift_status, reason;
};
struct Snapshot {
    struct ReplayWaterfall {
        double gain_db=0;
        std::pair<double,double> search_band_hz;
        std::optional<std::pair<double,double>> helper_band_hz;
    };
    struct SimulationProgress {
        std::uint64_t input_samples=0,total_samples=0;
        std::uint32_t sample_rate=0;
        double elapsed_seconds=0;
        bool complete=false;
    };
    std::vector<float> waveform;
    std::uint32_t waveform_sample_rate=0;
    bool environment_monitor=false;
    std::vector<double> spectrum_db;
    std::vector<std::complex<float>> spectrum_complex;
    std::uint64_t spectrum_start_sample=0,spectrum_segment=0,spectrum_revision=0;
    std::size_t spectrum_sample_count=0;
    double spectrum_bin_hz=0;
    // Convert the simulated spectrum from unit-DATA power to modeled FFT-bin SNR.
    std::optional<double> simulation_spectrum_gain_db;
    // Original band and power calibration of the currently displayed replay.
    std::optional<ReplayWaterfall> replay_waterfall;
    // Native receiver input clock and processing wall time, excluding replay.
    std::optional<SimulationProgress> simulation_progress;
    std::vector<std::complex<double>> constellation;
    std::uint64_t constellation_dropped=0;
    ConstellationSource constellation_source=ConstellationSource::input;
    std::vector<SignalUpdate> signals;
    std::vector<transfer::Received> received;
    modem::TransmitTrace transmit_trace;
    std::vector<PatternStep> pattern_steps;
    std::optional<double> bit_seconds, slot_stride_seconds;
    std::optional<std::pair<double,double>> search_band_hz, helper_band_hz;
    std::optional<std::string> transmit_unavailable;
    std::optional<std::string> noise_transmit_unavailable;
    std::string status,error,receiver_error;
    unsigned receiver_workers=0; // Active receiver's software-worker ceiling.
    std::string gpu_device,gpu_precision;
    std::string gpu_backend,gpu_selection_reason;
    std::optional<std::array<std::uint8_t,16>> gpu_device_uuid;
    std::uint64_t gpu_score_rows=0;
    // Bounded cumulative executed-work diagnostics for this settings generation.
    std::uint64_t gpu_fp32_score_rows=0,gpu_fp16_storage_score_rows=0,gpu_packed_fp16_score_rows=0;
    // Native lag selection is recorded before DSP completion; it is not a
    // count of completed scores or evidence of GPU execution.
    std::uint64_t gpu_native_coarse_lag_sessions=0;
    // CUDA can retain native fine processing for a scoped fallback route.
    // This is stage-routing evidence, not arithmetic precision or GPU work.
    std::uint64_t gpu_cuda_native_fine_sessions=0;
    std::string gpu_cuda_native_fine_fallback_reason;
    // Shared Vulkan admission and execution-time fallback evidence. These are
    // zero on CUDA and native CPU generations.
    std::size_t gpu_vulkan_shared_workspace_limit_bytes=0,gpu_vulkan_leased_bytes=0;
    std::size_t gpu_vulkan_peak_leased_bytes=0,gpu_vulkan_peak_owner_lease_bytes=0;
    std::uint64_t gpu_vulkan_waiting_sessions=0,gpu_native_coarse_workspace_sessions=0;
    std::uint64_t gpu_native_coarse_precision_sessions=0;
    std::uint64_t gpu_vulkan_timed_submissions=0;
    double gpu_vulkan_device_command_seconds=0;
    std::uint64_t gpu_host_scored_rows=0,gpu_statistics_blocks=0;
    std::uint64_t gpu_cuda_fp16_calls=0,gpu_cuda_int8_calls=0;
    std::uint64_t gpu_cuda_nvfp4_fine_modes=0;
    std::uint64_t gpu_cuda_cufft_fine_modes=0;
    std::uint64_t gpu_opencl_nvfp4_fine_modes=0;
    // CUDA half FFT counts cover public frequency/arrival score rows only.
    // Fallback counts explain why automatic/explicit half work was skipped.
    std::uint64_t gpu_cuda_half_fft_score_rows=0;
    std::string gpu_cuda_half_fft_fallback_summary;
    bool gpu_native_lag_preference_supported=false;
    bool running=false,transmitting=false,simulation=false,simulation_replay=false;
    bool simulation_replay_available=false;
    // The last successful simulation owns this bounded evidence, even when
    // editing settings resets the current receiver's `pattern_steps`.
    std::shared_ptr<const std::vector<PatternStep>> simulation_pattern_replay_steps;
    bool simulation_pattern_replay_active=false;
    std::size_t simulation_pattern_replay_visible_steps=0;
    std::uint64_t simulation_pattern_replay_elapsed_ms=0;
    bool force_transmit_available=false;
};
// The application thread submits settings and commands. Device and DSP work
// stay in the session. Poll snapshot() to consume one-shot receive events.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void start(const Settings&,bool active=true);
    void configure(const Settings&);
    void set_mono(bool);
    void set_channel_mode(audio::ChannelMode);
    void set_transmit_gain(double);
    void set_helper_callsign(std::string);
    bool try_suspend_capture();
    void resume_capture();
    void clear_received();
    void transmit(const Message&,bool force=false);
    void transmit_bits(std::span<const std::uint8_t>,bool force=false);
    void transmit_noise();
    void transmit_helper();
    void replay_simulation();
    void cancel_transmit();
    bool resume_recovery(std::uint64_t);
    void cancel_recovery(std::uint64_t);
    Snapshot snapshot();
    void stop();
    bool ready_to_close() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
