#pragma once
#include "datapump/audio.hpp"
#include "datapump/transfer.hpp"
#include <chrono>
#include <complex>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace datapump::live {
struct Settings {
    transfer::Options transfer;
    // Optional transmit geometry for fixed-interval text and attachments.
    // Short text (1–16 source bytes), explicit bits and tuning noise use the
    // base modem. Both share its sample rate, carrier and bandwidth; selecting
    // a message path never replaces the independently running receiver bank.
    std::optional<modem::Config> long_message_modem;
    std::string device = "default";
    // Mono defaults to the left output. The boolean remains for existing callers.
    bool mono = true;
    audio::ChannelMode channel_mode = audio::ChannelMode::left_mono;
    bool simulation = false;
    double simulation_snr_db = 18;
    double simulation_clock_error_ppm = 100;
    double simulation_phase_noise_degrees_per_sqrt_second = .5;
    // Received content and pending event storage are independent of DSP work.
    std::size_t content_limit = default_memory_limit;
    std::size_t dsp_workspace_bytes = transfer.dsp_workspace_bytes;
    // Compatibility fields: streaming capture no longer retains a duration
    // window, and simulated media progresses as fast as bounded DSP permits.
    double receive_buffer_seconds = 30;
    std::uint64_t simulation_seed = 1;
    double simulation_speed = 1;
    std::vector<Crypto> receive_keys;
    // Selecting a transmit key restricts reception to matching keyed streams.
    // Pattern-only raw results carry no separate authentication tag.
    bool permits_plaintext() const noexcept {
        return !transfer.key && !transfer.modem.scramble && !transfer.modem.dsss;
    }
};
// Check exactly the settings accepted by Session without changing a live
// receiver, opening audio hardware, or starting a transmission.
void validate_settings(const Settings& settings);
struct SignalUpdate {
    std::uint64_t id = 0; // Stable acquisition identity for replacing pending text.
    double frequency_hz = 0;
    std::string text;
    bool validated = false;
    std::string reception_id;
    double snr_db = 0;
    std::size_t received_bytes = 0;
    std::size_t expected_bytes = 0;
    std::uint64_t sequence = 0;
    double virtual_seconds = 0;
    // Unknown until receiver evidence is available; never inferred from the
    // synthetic training prefix returned by blind bootstrap acquisition.
    std::optional<double> preamble_received_percent = std::nullopt;
    // Available only after physical completion and interval/source validation.
    std::optional<StreamBitAccuracy> pre_fec_accuracy = std::nullopt;
    // Raw bits contain decoded observations and zero placeholders for timed
    // gaps, without a source checksum/MAC or error correction.
    bool binary = false;
    bool complete = false;
    std::size_t received_bits = 0;
    std::size_t expected_bits = 0;
    std::optional<double> pattern_score = std::nullopt;
    std::size_t missing_symbols = 0; // Unobserved symbol slots.
    StreamFecStats fec_stats;
    // Exact transport bits retained when a completed short dictionary stream
    // is presented as text; absent for pending observations and interval data.
    std::string raw_bits;
    // A stronger physical hypothesis can revise an earlier interpretation,
    // including one whose shorter profile had already observed its own end.
    // Consumers replace older revisions and withdraw superseded rows/content.
    std::uint64_t revision = 0;
    std::vector<std::uint64_t> superseded_ids;
    // Physical completion is independent of the bounded, post-end search.
    transfer::RecoveryProgress recovery_progress;
};
enum class ConstellationSource { input, transmitted, received };
struct PatternScoreObservation {
    std::uint64_t id = 0;
    std::chrono::steady_clock::time_point observed_at{};
    // Receiver's single-symbol admission reference for this observation.
    // Zero denotes unavailable metadata; never a fabricated receive threshold.
    double admission_threshold = 0;
    bool operator==(const PatternScoreObservation&) const = default;
};
struct Snapshot {
    static constexpr std::size_t pattern_score_limit = 128;
    static constexpr auto pattern_score_lifetime = std::chrono::seconds(6);
    std::vector<float> waveform;
    std::vector<double> spectrum_db;
    double spectrum_bin_hz = 0;
    std::vector<std::complex<double>> constellation;
    // Retained receiver hypotheses, not probabilities: real = pattern 0
    // evidence, imaginary = pattern 1 evidence, both against noise.
    std::vector<std::complex<double>> pattern_scores;
    // Stable identities and first-observed presentation times, parallel to
    // pattern_scores. Repeated retained hypotheses do not refresh their age.
    std::vector<PatternScoreObservation> pattern_score_observations;
    // Highest observation allocated at this live/replay position, including
    // candidates from receivers not selected for the displayed plot.
    std::uint64_t pattern_score_observation_id = 0;
    ConstellationSource constellation_source = ConstellationSource::input;
    std::vector<SignalUpdate> signals;
    std::vector<transfer::Received> received;
    // Bounded capture from the actual transmitter. Replay uses its matching
    // generation frame; idle snapshots retain the last transmission capture.
    modem::TransmitTrace transmit_trace;
    std::string status;
    std::string error;
    bool running = false;
    bool transmitting = false;
    bool transmitting_noise = false; // Continuous tuning noise; fraction has no endpoint.
    bool simulation = false;
    std::uint64_t sequence = 0;
    std::uint64_t samples_received = 0;
    std::size_t buffered_samples = 0;
    double virtual_seconds = 0;
    double transmission_seconds = 0;
    // Fraction of transmitted audio generated/played, not receiver work.
    double transmission_fraction = 0;
    // Finite simulation is still scoring received samples after TX audio ends.
    // This never implies physical reception completion.
    bool simulation_receiving_tail = false;
    // Steady-clock wall time spent preparing/generating/scoring this simulation.
    // Updates on snapshot polls even during a synchronous scoring call, freezes
    // at completion/cancellation, and excludes the presentation replay.
    double simulation_compute_seconds = 0;
    bool transmission_finished = true; // CPU/audio work complete; simulation presentation may still be active.
    bool transmission_cancelled = false;
    // Completed simulations present training, measured plots and provisional
    // reception over three wall-clock seconds. Verified signals and received
    // content are delivered once at the deadline, then plots return live.
    std::uint64_t transmission_id = 0;
    bool simulation_replay = false;
    std::size_t replay_frame_index = 0;
    std::size_t replay_frame_count = 0;
    double simulation_sample_fraction = 0;
    std::uint64_t constellation_dropped = 0;
    std::uint32_t hardware_sample_rate = 0;
    double audio_passband_hz = 0;
    std::size_t dsp_buffered_bytes = 0;
    // A separate bounded post-reception search pool; it never consumes the
    // waveform/receiver workspace reserved by dsp_buffered_bytes.
    std::size_t recovery_working_bytes = 0;
};

// Audio callbacks and modem work never run on the caller/UI thread. configure
// and transmit enqueue work; snapshot drains signal/received events while
// retaining the current raw plots. No FLTK or GUI objects are accessed here.
class Session {
public:
    // Unix epoch seconds for key admission. An optional clock makes controlled
    // time sources and deterministic clock-jump tests possible. It must be
    // thread-safe and return finite, nonnegative values within uint64 range.
    // Plot cadence and cancellation deadlines always use the steady clock.
    using EpochClock = std::function<double()>;
    // Independent monotonic presentation clock; never changes modem timing,
    // key admission, audio pacing or the CPU-bounded simulation trajectory.
    using ReplayClock = std::function<std::chrono::steady_clock::time_point()>;
    explicit Session(EpochClock epoch_clock = {}, ReplayClock replay_clock = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void start(const Settings& settings);
    void configure(const Settings& settings);
    void update(const Settings& settings) { configure(settings); }
    // Apply output routing to the next playback without interrupting reception
    // or changing audio already being transmitted.
    void set_mono(bool mono);
    void set_channel_mode(audio::ChannelMode channels);
    // Release only idle hardware capture for another local engine. Refuses
    // pending RX/TX; repeated polls acknowledge actual device closure. Does
    // not cancel recovery, complete a reception, or change saved settings.
    bool try_suspend_capture();
    void resume_capture();
    void transmit(const Message& message);
    // One 0/1 per element, including leading zeros. Uses streaming APSK and
    // the selected data key, with no interval coding, preamble or FEC. Raw
    // simulations pass sampled audio to ordinary blind acquisition. Without
    // raw discovery framing, no timing/length-assisted raw result is emitted.
    void transmit_bits(std::span<const std::uint8_t> bits);
    // Continuous noise at the selected carrier/bandwidth and normal signal
    // level, with fresh temporary private streams. Does not encode a message
    // or use/register saved keys. Stop with cancel_transmit().
    void transmit_noise();
    void cancel_transmit();
    // Resume a retained incomplete search with another configured time budget,
    // or stop one search without interrupting reception or audio capture.
    bool resume_recovery(std::uint64_t signal_id);
    void cancel_recovery(std::uint64_t signal_id);
    void clear_recoveries();
    Snapshot snapshot();
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
