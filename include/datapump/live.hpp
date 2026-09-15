#pragma once
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
    std::string device = "default";
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
};
enum class ConstellationSource { input, transmitted, received };
struct Snapshot {
    static constexpr std::size_t pattern_score_limit = 128;
    std::vector<float> waveform;
    std::vector<double> spectrum_db;
    double spectrum_bin_hz = 0;
    std::vector<std::complex<double>> constellation;
    // Retained receiver hypotheses, not probabilities: real = pattern 0
    // evidence, imaginary = pattern 1 evidence, both against noise.
    std::vector<std::complex<double>> pattern_scores;
    ConstellationSource constellation_source = ConstellationSource::input;
    std::vector<SignalUpdate> signals;
    std::vector<transfer::Received> received;
    std::string status;
    std::string error;
    bool running = false;
    bool transmitting = false;
    bool simulation = false;
    std::uint64_t sequence = 0;
    std::uint64_t samples_received = 0;
    std::size_t buffered_samples = 0;
    double virtual_seconds = 0;
    double transmission_seconds = 0;
    double transmission_fraction = 0;
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
    void transmit(const Message& message);
    // One 0/1 per element, including leading zeros. Uses streaming APSK and
    // the selected data key, with no interval coding, preamble or FEC. Raw
    // simulations pass sampled audio to ordinary blind acquisition. Without
    // raw discovery framing, no timing/length-assisted raw result is emitted.
    void transmit_bits(std::span<const std::uint8_t> bits);
    void cancel_transmit();
    Snapshot snapshot();
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
