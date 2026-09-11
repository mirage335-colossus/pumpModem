#pragma once
#include "datapump/transfer.hpp"
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
    std::size_t dsp_workspace_bytes = 64 * 1024 * 1024;
    // Compatibility fields: streaming capture no longer retains a duration
    // window, and simulated media progresses as fast as bounded DSP permits.
    double receive_buffer_seconds = 30;
    std::uint64_t simulation_seed = 1;
    double simulation_speed = 1;
    std::vector<Crypto> receive_keys;
    // Selecting a transmit key also requires authenticated reception. An
    // unkeyed channel may search loaded keys in addition to plain packets.
    bool permits_plaintext() const noexcept {
        return !transfer.key && !transfer.modem.scramble && !transfer.modem.dsss;
    }
};
struct SignalUpdate {
    std::uint64_t id = 0; // Stable acquisition identity for replacing pending text.
    double frequency_hz = 0;
    std::string text;
    bool validated = false;
    std::string packet_id;
    double snr_db = 0;
    std::size_t received_bytes = 0;
    std::size_t expected_bytes = 0;
    std::uint64_t sequence = 0;
    double virtual_seconds = 0;
};
enum class ConstellationSource { input, transmitted, received };
struct Snapshot {
    std::vector<float> waveform;
    std::vector<double> spectrum_db;
    double spectrum_bin_hz = 0;
    std::vector<std::complex<double>> constellation;
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
    bool transmission_finished = true;
    bool transmission_cancelled = false;
    // Completed simulations retain a payload-midpoint view for two wall-clock
    // seconds. All plots, including the constellation, then return to live RX.
    std::uint64_t transmission_id = 0;
    bool simulation_review = false;
    bool constellation_retained = false;
    double simulation_sample_fraction = 0;
    std::vector<std::vector<double>> simulation_waterfall;
    double simulation_waterfall_bin_hz = 0;
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
    explicit Session(EpochClock epoch_clock = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void start(const Settings& settings);
    void configure(const Settings& settings);
    void update(const Settings& settings) { configure(settings); }
    void transmit(const Message& message);
    void cancel_transmit();
    Snapshot snapshot();
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
