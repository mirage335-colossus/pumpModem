#include "live_receiver_layout.hpp"
#include "datapump/channel.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/runtime.hpp"
#include "datapump/sched_hop.hpp"
#include "signal_view.hpp"
#include "live_receptions.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace datapump::live::detail {
std::size_t constellation_limit(const modem::Config& config) {
    return std::max({minimum_constellation_limit, detail::SignalWindow::constellation_capacity(config),
                     modem::StreamingTransmitter::constellation_history_capacity(config)});
}
std::size_t constellation_limit(const Settings& settings) {
    return std::max(constellation_limit(settings.transfer.modem), settings.long_message_modem ?
        constellation_limit(*settings.long_message_modem) : std::size_t{});
}
constexpr std::size_t reception_alias_bytes = detail::ReceptionHistory::capacity * sizeof(std::uint64_t);
constexpr std::size_t trace_prefix_workspace = modem::TransmitTrace::dynamic_storage_limit;
const std::size_t replay_frame_base = sizeof(ReplayFrame) +
    (replay_wave_samples + replay_bins) * sizeof(float) +
    pattern_score_limit * (sizeof(std::complex<float>) + sizeof(PatternScoreObservation)) +
    sizeof(std::optional<SignalUpdate>) + replay_text_limit + 64 + reception_alias_bytes + trace_prefix_workspace;
// One verified stream is moved into the receive-content cache at the deadline.
// Its payload uses the content quota; its diagnostics and caption use DSP space.
const std::size_t replay_result_workspace = sizeof(transfer::Received) +
    sizeof(SignalUpdate) + 2*replay_text_limit + 64 + reception_alias_bytes + minimum_constellation_limit * sizeof(std::complex<double>);
std::size_t replay_workspace(const Settings& value) {
    return value.simulation ? std::min(value.dsp_workspace_bytes / 8,
        replay_result_workspace + replay_frames * (replay_frame_base + constellation_limit(value) * sizeof(std::complex<float>))) : 0;
}
std::size_t plot_workspace(const Settings& value) { return plot_size * (sizeof(float) + sizeof(double)) +
                                       2 * (plot_size/2+1) * sizeof(std::complex<float>) +
                                       constellation_limit(value) * 3 * sizeof(std::complex<double>) +
                                       detail::SignalWindow::sample_capacity(value.transfer.modem) * 2 * sizeof(float) +
                                       pattern_score_limit * (4 * (sizeof(std::complex<double>) + sizeof(PatternScoreObservation)) + sizeof(modem::PatternEvidence)) +
                                       sizeof(detail::PatternScoreHistory) +
                                       detail::PatternScoreHistory::reserved_entries_bytes +
                                       sizeof(detail::SignalWindow) + modem::SampledSimulationChannel::workspace_bound +
                                       plot_size * (sizeof(float)+sizeof(std::complex<double>)) +
                                       sizeof(modem::TransmitTrace) + trace_prefix_workspace + replay_workspace(value) +
                                       maximum_events * reception_alias_bytes +
                                       ((value.transfer.modem.scheduled_hopping && value.transfer.modem.scheduled_hopping->enabled() &&
                                         !value.transfer.modem.scheduled_hopping->continuous()) ? value.dsp_workspace_bytes/64 : 0); }
std::size_t audio_reserve(const Settings& value) {
    return value.simulation ? 0 : std::min<std::size_t>(5 * 1024 * 1024, value.dsp_workspace_bytes / 8);
}
std::size_t available_bank_capacity(const Settings& value) {
    // Simulation feeds sampled audio directly; it never allocates
    // the asynchronous hardware capture queue.
    const auto capture_queue = value.simulation ? 0 : value.dsp_workspace_bytes / 8;
    const auto reserved = capture_queue + value.dsp_workspace_bytes / 4 + plot_workspace(value) + audio_reserve(value);
    return reserved >= value.dsp_workspace_bytes ? 0 : value.dsp_workspace_bytes - reserved;
}
std::size_t bank_capacity(const Settings& value) {
    const auto available=available_bank_capacity(value);
    if (!available) throw Error("DSP workspace cannot hold the audio and plot buffers");
    return available;
}
std::size_t source_budget(std::size_t content) {
    return transfer::source_storage_limit(content);
}
modem::ChannelConfig channel_config(const Settings& settings) {
    modem::ChannelConfig channel;
    channel.snr_db = settings.simulation_snr_db;
    channel.clock_error_ppm = settings.simulation_clock_error_ppm;
    channel.frequency_offset_hz = settings.simulation_frequency_offset_hz;
    channel.phase_noise_degrees_per_sqrt_second = settings.simulation_phase_noise_degrees_per_sqrt_second;
    channel.seed = settings.simulation_seed;
    return channel;
}
unsigned effective_epoch_search_seconds(const transfer::Options& options,bool strict_live) {
    auto seconds=options.search_seconds;
    if(options.clock_sync) {
        clock_sync::validate(*options.clock_sync);
        clock_sync::validate_audio_error(options.audio_timing_error_seconds);
        const auto radius=std::ceil(options.clock_sync->half_window_seconds()+
            2*options.audio_timing_error_seconds+2.);
        if(radius>60)throw Error("clock region requires more than the supported 60-second epoch radius");
        if(!options.timestamp)seconds=std::max(seconds,static_cast<unsigned>(radius));
    }
    if(strict_live && seconds>60)throw Error("continuous timing search exceeds 60 seconds");
    return seconds;
}
Settings normalized_layout(Settings value) {
    audio::validate_option_values({value.transmit_gain,value.exclusive});
    clock_sync::validate_audio_error(value.transfer.audio_timing_error_seconds);
    if (value.content_limit == default_memory_limit) value.content_limit = value.transfer.content_limit;
    if (value.dsp_workspace_bytes == runtime::default_dsp_workspace_bytes()) value.dsp_workspace_bytes = value.transfer.dsp_workspace_bytes;
    value.transfer.content_limit = value.content_limit;
    value.transfer.dsp_workspace_bytes = value.dsp_workspace_bytes;
    if (value.transfer.modem.spreading_mode == modem::SpreadingMode::tone) {
        value.transfer.key.reset(); value.receive_keys.clear();
        value.transfer.modem.data_key.reset();
        value.transfer.modem.scramble = value.transfer.modem.dsss = false;
        value.transfer.modem.dsss_factor=1;
        value.transfer.modem.spreading_seed.fill(0); value.transfer.modem.dsss_seed.fill(0);
        if (value.transfer.automatic_receive_profiles && value.transfer.receive_pattern_mode != tuning::PatternMode::auto_tone &&
            value.transfer.receive_pattern_mode < tuning::PatternMode::tone_1)
            value.transfer.receive_pattern_mode = tuning::PatternMode::auto_tone;
    } else if (value.transfer.key) value.transfer.modem.scramble = true;
    modem::validate(value.transfer.modem);
    if(value.transfer.modem.oscillator_search)
        modem::validate_oscillator_search(*value.transfer.modem.oscillator_search);
    if (value.long_message_modem) {
        auto& longer = *value.long_message_modem;
        const auto& base = value.transfer.modem;
        if (longer.sample_rate != base.sample_rate || longer.carrier_hz != base.carrier_hz ||
            longer.symbol_burst.enabled()!=base.symbol_burst.enabled() ||
            (base.symbol_burst.enabled()&&longer.symbol_burst!=base.symbol_burst) ||
            longer.dsss_factor != base.dsss_factor ||
            (base.dsss_factor>1 && longer.outer_dsss_version != base.outer_dsss_version) ||
            longer.bandwidth_hz != base.bandwidth_hz || longer.spreading_mode != base.spreading_mode)
            throw Error("short and long transmit profiles must share sample rate, carrier, bandwidth, spreading mode and Symbol_Burst settings");
        if (longer.spreading_mode == modem::SpreadingMode::tone) {
            longer.data_key.reset(); longer.scramble = longer.dsss = false;longer.dsss_factor=1;
            longer.spreading_seed.fill(0); longer.dsss_seed.fill(0);
        } else if (value.transfer.key) longer.scramble = true;
        modem::validate(longer);
        if ((longer.scramble || longer.dsss) && !value.transfer.key && value.receive_keys.empty())
            throw Error("encrypted spreading requires a loaded key");
    }
    if (!value.simulation && modem::waveform_bandwidth_hz(value.transfer.modem) > 192000)
        throw Error("This bandwidth requires an SDR frontend; this build supports audio hardware and simulation. Enable simulation for the selected band.");
    if (!value.content_limit) throw Error("received content cache must have a positive capacity");
    (void)source_budget(value.content_limit);
    if (value.dsp_workspace_bytes < minimum_workspace) throw Error("streaming DSP workspace must be at least 512 KiB");
    (void)bank_capacity(value);
    if (!std::isfinite(value.simulation_snr_db) || std::abs(value.simulation_snr_db) > 200)
        throw Error("simulation sample SNR must be finite and between -200 and 200 dB");
    if (value.simulation_spectrum_gain_db && !std::isfinite(*value.simulation_spectrum_gain_db))
        throw Error("simulation spectrum display gain must be finite");
    modem::validate_channel(value.transfer.modem, channel_config(value));
    if (value.receive_keys.size() > 128) throw Error("continuous receiver supports at most 128 loaded keys");
    // Explicit epochs retain their requested radius. Automatic admission and
    // pure estimates use the SAME normalized clock/propagation/audio bound.
    value.transfer.search_seconds=effective_epoch_search_seconds(value.transfer);
    if ((value.transfer.modem.scramble || value.transfer.modem.dsss) &&
        !value.transfer.key && value.receive_keys.empty()) throw Error("encrypted spreading requires a loaded key");
    return value;
}

}
namespace datapump::live {
std::size_t receiver_workspace_capacity(const Settings& settings) {
    return detail::bank_capacity(detail::normalized_layout(settings));
}
std::size_t receiver_control_margin(const modem::Config& config,bool keyed) {
    const auto compact=keyed && modem::symbol_sample_count(config)>=60ULL*config.sample_rate;
    return (compact?8192U:65536U)+sizeof(detail::ReceiverState)+
        detail::PatternScoreHistory::reserved_entries_bytes;
}
}
