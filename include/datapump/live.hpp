#pragma once
#include "datapump/audio.hpp"
#include "datapump/transfer.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/transmit_trace.hpp"
#include <array>
#include <memory>
#include <utility>

namespace datapump::live {
// Both ends configure the same calendar independently. Its ordinal advances
// through quiet slots as well as DATA. A later capture can join this calendar.
struct Calendar {
    std::uint64_t epoch = 0, initial_ordinal = 0;
};

struct Settings {
    transfer::Options transfer;
    std::string device="default";
    bool mono=true,simulation=false,full_duplex=false,exclusive=false;
    // Local processing choice; it does not change the on-air profile.
    bool gpu=false;
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
    bool added_interleave=false;
    std::string fhss_profile="off";
    std::vector<double> fhss_frequencies_hz;
};
void validate_settings(const Settings&);
// Compile-time capability only. Device initialization stays on the session
// manager, so GUI availability checks never open a GPU or block on a driver.
bool gpu_receiver_available() noexcept;
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
    std::vector<std::uint64_t> superseded_ids;
    transfer::RecoveryProgress recovery_progress;
    bool helper_carrier=false;
};
enum class ConstellationSource {input,transmitted,received};
enum class PatternDecision { ambiguous, data, quiet };
// Bounded diagnostic rows from actual receive processing in public-slot order.
struct PatternStep {
    std::uint64_t index=0, epoch=0, ordinal=0;
    PatternDecision decision=PatternDecision::ambiguous;
    std::optional<std::uint8_t> bit;
    std::array<double,2> bit_evidence{}, adaptive_bit_evidence{}, combined_bit_evidence{};
    double quiet_energy=0, quiet_threshold=0, operational_log_e=0, operational_threshold=0;
    double helper_seconds=0, coarse_seconds=0, fine_seconds=0;
    std::size_t quiet_modes=0, fine_candidates=0;
    unsigned workers=0;
    bool evidence_complete=false;
    std::string helper_status, reason;
};
struct Snapshot {
    std::vector<float> waveform;
    std::uint32_t waveform_sample_rate=0;
    bool environment_monitor=false;
    std::vector<double> spectrum_db;
    std::vector<std::complex<float>> spectrum_complex;
    std::uint64_t spectrum_start_sample=0,spectrum_segment=0,spectrum_revision=0;
    std::size_t spectrum_sample_count=0;
    double spectrum_bin_hz=0;
    std::optional<double> simulation_spectrum_gain_db;
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
    std::string gpu_device,gpu_precision;
    std::uint64_t gpu_score_rows=0;
    // Bounded cumulative executed-work diagnostics for this settings generation.
    std::uint64_t gpu_fp32_score_rows=0,gpu_fp16_storage_score_rows=0,gpu_packed_fp16_score_rows=0;
    // Native lag selection is recorded before DSP completion; it is not a
    // count of completed scores or evidence of GPU execution.
    std::uint64_t gpu_native_coarse_lag_sessions=0;
    bool gpu_native_lag_preference_supported=false;
    bool running=false,transmitting=false,simulation=false,simulation_replay=false;
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
