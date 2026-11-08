#pragma once
#include "datapump/audio.hpp"
#include "datapump/transfer.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/transmit_trace.hpp"
#include <memory>

namespace datapump::live {
// Preserved GUI settings. A single selected key is held in transfer.key.
struct Settings {
    transfer::Options transfer;
    std::string device="default";
    bool mono=true,simulation=false,full_duplex=false,exclusive=false;
    audio::ChannelMode channel_mode=audio::ChannelMode::left_mono;
    double transmit_gain=1;
    helper::Options helper;
};
void validate_settings(const Settings&);
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
// Shared presentation boundary for a future canonical backend. No acquisition,
// receiver banks, calibration counters or retired receiver ownership remain.
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
    std::string status,error,receiver_error;
    bool running=false,transmitting=false,simulation=false,simulation_replay=false;
};
// This explicit unavailable boundary owns no device, worker, waveform or DSP.
// The GUI keeps its controls until the canonical triad backend is connected.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void start(const Settings&);
    void configure(const Settings&);
    void set_mono(bool);
    void set_channel_mode(audio::ChannelMode);
    void set_transmit_gain(double);
    void set_helper_callsign(std::string);
    bool try_suspend_capture();
    void resume_capture();
    void clear_received();
    void transmit(const Message&);
    void transmit_bits(std::span<const std::uint8_t>);
    void transmit_noise();
    void transmit_helper();
    void cancel_transmit();
    bool resume_recovery(std::uint64_t);
    void cancel_recovery(std::uint64_t);
    Snapshot snapshot();
    void stop();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
