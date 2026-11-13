#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/preset.hpp"
#include "datapump/fast/session.hpp"
#include "datapump/fast/simulation.hpp"
#include "fast_test_key.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace datapump;
using namespace datapump::fast;

namespace {

void require(bool value, const char* message) {
    if (!value) throw Error(message);
}

IntervalReader intervals(std::size_t count) {
    return [count, n=std::size_t{0}](std::span<std::uint8_t> bits) mutable {
        if (n == count) return false;
        for (std::size_t i = 0; i < bits.size(); ++i) bits[i] = ((i * 3 + n) ^ (i >> 2)) & 1;
        ++n;
        return true;
    };
}

void simulation_audio_bounds() {
    auto p = resolve_snr_preset(Channel::acoustic_short, -6).profile;
    SimulationSettings config;
    config.baseband_ppm = 100;
    config.shift_ppm = 1;
    config.shift_hz = 30e6;
    config.delay_ms = 7;
    config.doppler_hz = 30;
    config.selective_slow_depth = config.selective_fast_depth = .65;
    validate(p, config); // The reported screenshot: RF uncertainty is about 30 Hz.
    config.shift_hz = 200e6;
    validate(p, config); // Modeled energy may extend beyond the nominal occupied band.

    config.shift_hz = 30e6;
    config.shift_ppm = 100;
    const auto rejected = [&](const Profile& profile, std::string_view shift) {
        try { validate(profile, config); }
        catch (const Error& error) {
            const std::string_view message = error.what();
            require(message.find("sampled audio allows 0..24000.0 Hz") != message.npos &&
                    message.find("RF oscillator +/-" + std::string(shift)) != message.npos,
                    "Fast invalid channel did not explain its actual Hz and sampled-audio bounds");
            return;
        }
        throw Error("Fast accepted oscillator bounds crossing DC or Nyquist");
    };
    rejected(p, "3000.");
    config.shared_reference = true;
    config.shift_ppm = 1;
    rejected(p, "3000."); // A shared crystal remains a 100 ppm RF reference.
    config.shift_hz = 0;
    validate(p, config); // An unused shift oscillator contributes no offset.

    p.carrier_hz = 21000;
    config.shared_reference = false;
    config.shift_ppm = 100;
    config.shift_hz = 40e6;
    validate(p); // Nominal waveform fits; only the modeled upper bound aliases.
    rejected(p, "4000.");
}

void analytic_parity(Profile p) {
    const auto count = p.acoustic_ofdm ? cycle_intervals(p) : std::size_t{1};
    Transmitter pcm(p, intervals(count));
    Transmitter analytic(p, intervals(count));
    std::array<float, 509> a{};
    std::array<std::complex<float>, 509> b{};
    std::uint64_t samples = 0;
    while (!pcm.finished()) {
        const auto n = pcm.read(a);
        require(n == analytic.read_analytic(b), "Fast analytic projection changed the sample extent");
        for (std::size_t i = 0; i < n; ++i)
            require(std::abs(a[i] - b[i].real()) < 1e-7f, "Fast analytic projection changed real PCM");
        samples += n;
    }
    require(analytic.finished() && samples == transmission_samples(p, count),
            "Fast analytic and real sources must drain their pulse tails equally");
}

std::vector<float> collect(SimulationChannel& channel, std::size_t block_size) {
    std::vector<float> result;
    std::vector<float> buffer(block_size);
    for (;;) {
        const auto n = channel.read(buffer);
        if (!n) break;
        result.insert(result.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
    }
    return result;
}

SimulationChannel::Source impulse() {
    return [done=false](std::span<std::complex<float>> output) mutable {
        if (done) return std::size_t{0};
        done = true;
        output.front() = {1, 0};
        return std::size_t{1};
    };
}

void channel_tails() {
    const auto p = profile(Channel::ssb);
    SimulationSettings config;
    config.seed = 700;
    config.noise_dbm_hz.reset();
    SimulationChannel baseline(p, config, impulse());
    const auto bypass = collect(baseline, 701);
    require(bypass.front() == 1 && std::all_of(bypass.begin() + 1, bypass.end(), [](float x) { return x == 0; }),
            "Explicit Fast effects-Off baseline changed emitted PCM");
    require(baseline.report().quiet_start_sample == 1 && baseline.report().source_samples == 1,
            "Fast effects-Off baseline moved the quiet window");

    config.delay_ms = .5;
    config.doppler_hz = 6;
    SimulationChannel spread(p, config, impulse());
    const auto delayed = collect(spread, 1023);
    const auto& spread_report = spread.report();
    require(spread_report.propagation_tail_samples == 24 && spread_report.quiet_start_sample == 25,
            "Fast channel dropped its delayed ray or started quiet scoring early");
    require(std::abs(delayed[24]) > .001f && delayed[25] == 0,
            "Fast channel did not emit the selected delayed ray");

    config.selective_slow_depth = config.selective_fast_depth = .65;
    SimulationChannel selective(p, config, impulse());
    const auto disturbed = collect(selective, 187);
    const auto& selective_report = selective.report();
    require(selective_report.selective_spacing_hz > 0 && selective_report.propagation_tail_samples > 24,
            "Selected Fast frequency selective amplitude modulation did not reserve its filter tail");
    require(disturbed.front() != delayed.front() &&
            std::any_of(disturbed.begin() + 25, disturbed.begin() + *selective_report.quiet_start_sample,
                [](float x) { return std::abs(x) > .001f; }),
            "Selected Fast amplitude modulation did not change the signal and emit its dispersive tail");
}

void gaussian_spread_continuity() {
    const auto p = profile(Channel::ssb);
    SimulationSettings config;
    config.seed = 712;
    config.noise_dbm_hz.reset();
    SimulationChannel off_fixed(p, config, impulse());
    config.spread_evolution = SpreadEvolution::gaussian;
    SimulationChannel off_gaussian(p, config, impulse());
    require(collect(off_fixed, 509) == collect(off_gaussian, 37),
        "Gaussian evolution changed the explicit spread-Off identity");

    config.delay_ms = 7;
    config.doppler_hz = 30;
    const auto source = [] {
        return [left=std::size_t{24000}](std::span<std::complex<float>> output) mutable {
            const auto count = std::min(left, output.size());
            std::fill_n(output.begin(), count, std::complex<float>{1, 0});
            left -= count;
            return count;
        };
    };
    SimulationChannel whole(p, config, source());
    SimulationChannel fragments(p, config, source());
    const auto reference = collect(whole, 4096);
    require(reference == collect(fragments, 137),
        "Gaussian diffuse channel depends on caller chunk boundaries");
    const auto& report = whole.report();
    require(report.spread_evolution == SpreadEvolution::gaussian &&
            report.gaussian_doppler_width_hz == 30 && report.propagation_frequency_allowance_hz == 90 &&
            report.propagation_tail_samples == 336 && report.quiet_start_sample == 24336,
        "Gaussian diffuse channel lost its width convention, engineering allowance or delayed tail");
    require(std::all_of(reference.begin() + *report.quiet_start_sample, reference.end(),
                [](float sample) { return sample == 0; }),
        "Gaussian channel fabricated signal after its complete finite propagation tail");
    const auto signal = std::span(reference).subspan(336, 23664);
    const auto extremes = std::minmax_element(signal.begin(), signal.end());
    require(*extremes.second - *extremes.first > .1f,
        "Gaussian diffuse gain did not evolve through physical time");

    auto different_seed = config;
    different_seed.seed = 713;
    SimulationChannel independent(p, different_seed, source());
    require(reference != collect(independent, 137), "Gaussian ray seed did not select an independent realization");
    config.spread_evolution = SpreadEvolution::fixed;
    SimulationChannel fixed(p, config, source());
    require(reference != collect(fixed, 137) && fixed.report().gaussian_doppler_width_hz == 0 &&
            fixed.report().propagation_frequency_allowance_hz == 30,
        "Fixed-ray diagnostic was replaced by or mislabeled as diffuse fading");
}

void live_playback_excludes_synthetic_link_budget() {
    const auto p = profile(Channel::ssb);
    SimulationSettings config;
    config.seed = 711;
    config.delay_ms = .5;
    config.doppler_hz = 6;

    auto varied = config;
    varied.transmit_watts = std::numeric_limits<double>::quiet_NaN();
    varied.path_loss_db = std::numeric_limits<double>::infinity();
    varied.noise_dbm_hz = std::numeric_limits<double>::infinity();
    validate(p, varied, ChannelPurpose::live_playback);

    SimulationChannel ordinary(p, config, impulse(), ChannelPurpose::live_playback);
    SimulationChannel ignored(p, varied, impulse(), ChannelPurpose::live_playback);
    const auto clean_playback = collect(ordinary, 73);
    require(clean_playback == collect(ignored, 127),
        "Live waveform depended on ignored transmit power, synthetic path loss or receiver noise");
    for (const auto* channel : {&ordinary, &ignored}) {
        const auto& report = channel->report();
        require(!report.link_budget_applied && !report.cn0_db_hz && report.noise_variance == 0 &&
                report.received_power_dbm == 0,
            "Live playback published a synthetic receiver link budget");
    }
    require(ordinary.report().propagation_tail_samples > 0 &&
            ordinary.report().quiet_start_sample == ignored.report().quiet_start_sample &&
            ordinary.report().spread_delay_ms == .5 && ordinary.report().spread_doppler_hz == 6,
        "Live playback bypassed explicit propagation effects, tails or quiet timing");

    auto off = config;
    off.delay_ms = 0;
    off.doppler_hz = 0;
    auto physical = off;
    physical.transmit_watts = 1e12;
    physical.path_loss_db = -1000;
    physical.noise_dbm_hz.reset();
    SimulationChannel baseline(p, off, impulse(), ChannelPurpose::live_playback);
    SimulationChannel no_link_budget(p, physical, impulse(), ChannelPurpose::live_playback);
    require(collect(baseline, 89) == collect(no_link_budget, 89),
        "Live effects-Off PCM changed with irrelevant physical-link fields");

    SimulationChannel software_noise(p, config, impulse());
    require(software_noise.report().link_budget_applied && software_noise.report().cn0_db_hz &&
            software_noise.report().noise_variance > 0,
        "Receiver simulation stopped applying its declared receiver noise");
}

void continuous_noise_and_oscillators() {
    const auto p = profile(Channel::ssb);
    SimulationSettings config;
    config.seed = 701;
    config.baseband_ppm = 100;
    config.baseband_phase_rad_sqrt_s = .01;
    config.shared_reference = true;
    config.shift_hz = 1e6;
    config.delay_ms = 7;
    config.doppler_hz = 30;
    config.margin = 3;
    config.selective_slow_depth = config.selective_fast_depth = .65;
    SimulationChannel first(p, config, impulse());
    SimulationChannel second(p, config, impulse());
    const auto whole = collect(first, 4096);
    const auto fragments = collect(second, 137);
    require(whole == fragments, "Fast channel state changed across callback boundaries");
    const auto& report = first.report();
    require(report.cn0_db_hz && std::abs(*report.cn0_db_hz - 90.0205999132796) < 1e-10,
            "Fast link budget changed its watts/path loss/noise density convention");
    auto higher_loss = config;
    higher_loss.path_loss_db += 10;
    SimulationChannel receiver_noise(p, higher_loss, impulse());
    require(receiver_noise.report().cn0_db_hz &&
            std::abs(*receiver_noise.report().cn0_db_hz - (*report.cn0_db_hz - 10)) < 1e-10 &&
            std::abs(receiver_noise.report().noise_variance / report.noise_variance - 10) < 1e-10,
        "Simulation path loss did not raise normalized receiver noise independently at the receive boundary");
    require(report.shift_error_hz != 0 &&
            std::abs(report.shift_error_hz - config.shift_hz * report.sample_clock_error_ppm * 1e-6) < 1e-12,
            "Fast shared oscillator must use the same fractional reference at RF and baseband");
    require(report.spread_delay_ms == 21 && report.spread_doppler_hz == 90,
            "Fast margin did not expand the selected spread model");
    require(report.quiet_start_sample && *report.quiet_start_sample > 1 + report.propagation_tail_samples,
            "Fast clock interpolation filter did not drain before quiet scoring");
    const auto quiet = std::span(whole).subspan(*report.quiet_start_sample);
    const auto energy = std::accumulate(quiet.begin(), quiet.end(), 0.,
        [](double sum, float sample) { return sum + static_cast<double>(sample) * sample; });
    require(quiet.size() == end_silence_samples(p) &&
            std::abs(energy / (quiet.size() * report.noise_variance) - 1) < .025,
            "Fast selected noise must continue at the modeled variance during physical silence");

    config.shift_ppm = 1;
    config.shift_phase_rad_sqrt_s = .5;
    config.shared_reference = false;
    config.shift_hz = 0;
    SimulationChannel off(p, config, impulse());
    const auto shift_disabled = collect(off, 1024);
    config.shift_ppm = config.shift_phase_rad_sqrt_s = 0;
    SimulationChannel unused(p, config, impulse());
    require(off.report().shift_error_hz == 0 && shift_disabled == collect(unused, 1024),
            "Zero shift frequency must disable both shift oscillator offset and phase diffusion");
}

void measured_clock_and_shift() {
    const auto p = profile(Channel::ssb);
    SimulationSettings config;
    config.noise_dbm_hz.reset();
    config.baseband_ppm = 100;
    config.shift_hz = 1e6;
    config.shared_reference = true;
    config.seed = 702;
    const double step = 2 * std::numbers::pi * p.carrier_hz / p.sample_rate;
    SimulationChannel channel(p, config, [step, index=std::uint64_t{0}](std::span<std::complex<float>> output) mutable {
        for (auto& sample : output) sample = static_cast<std::complex<float>>(std::polar(.1, step * index++));
        return output.size();
    });
    std::vector<float> pcm(p.sample_rate);
    require(channel.read(pcm) == pcm.size(), "Fast clock and shift fixture lost samples");
    double first = 0, last = 0;
    unsigned crossings = 0;
    for (std::size_t i = 100; i < pcm.size(); ++i) {
        if (pcm[i-1] <= 0 && pcm[i] > 0) {
            const double index = i - 1 - static_cast<double>(pcm[i-1]) / (pcm[i] - pcm[i-1]);
            if (!crossings++) first = index;
            last = index;
        }
    }
    require(crossings > 100, "Fast clock and shift fixture did not emit an audio tone");
    const auto measured = (crossings - 1.) * p.sample_rate / (last - first);
    const auto& report = channel.report();
    const auto expected = p.carrier_hz * (1 + report.sample_clock_error_ppm * 1e-6) + report.shift_error_hz;
    require(std::abs(measured - expected) < .01 && std::abs(measured - p.carrier_hz) > 1,
            "Fast clock/shift error was not realized at the predicted audio frequency");
}

void encrypted_session() {
    Settings settings;
    settings.profile = profile(Channel::ssb);
    settings.device.clear();
    settings.key = fast_test::key(Bytes(32, 0x64));
    settings.simulation.emplace();
    settings.simulation->seed = 702;
    Session session;
    session.configure(settings);
    const std::string message = "Encrypted Fast simulation\nExact bytes";
    session.transmit_text(message);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    Snapshot result;
    do {
        result = session.poll();
        if (result.active) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (result.active && std::chrono::steady_clock::now() < deadline);
    if (!result.complete) std::cerr << result.status << '\n';
    require(!result.active && result.simulated && result.complete && result.physical_complete &&
            result.encrypted && result.authenticated && result.file,
            "Fast simulated receive did not complete and authenticate the protected source");
    require(result.receiver_backend=="resilient-ssb" && result.simulation && result.simulation->quiet_start_sample &&
            result.physical_completion_input_tick &&
            *result.physical_completion_input_tick >= *result.simulation->quiet_start_sample+6ULL*settings.profile.sample_rate,
            "Fast simulated receive did not observe six seconds of quiet through the modeled receiver");
    require(std::ranges::equal(result.file->bytes(), message), "Fast simulation changed the authenticated text");
}

void cancel_simulation() {
    Settings settings;
    settings.profile = profile(Channel::ssb);
    settings.profile.ldpc_frame_bits = 64800;
    settings.profile.symbol_rate = 15;
    settings.profile.interleave_depth = 1;
    settings.device.clear();
    settings.simulation.emplace();
    settings.simulation->seed = 703;
    settings.simulation->noise_dbm_hz.reset();
    Session session;
    session.configure(settings);
    session.transmit_text("Cancelled Fast simulation");
    const auto start_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    Snapshot result;
    do {
        result = session.poll();
        if (!result.active || (result.simulation && result.simulation->output_samples != 0)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < start_deadline);
    require(result.active && result.simulated && result.simulation && result.simulation->output_samples != 0,
            "Fast cancellation fixture did not start producing samples");
    session.cancel();
    session.close();
    const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!session.ready_to_close() && std::chrono::steady_clock::now() < close_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    result = session.poll();
    require(session.ready_to_close() && result.cancelled && !result.physical_complete && !result.complete && !result.file,
            "Cancelling Fast simulation manufactured a physical or authenticated completion");
}

void public_receiver_dispatch() {
    auto p=profile(Channel::ssb);
    std::stop_source stop;
    std::size_t input_points=0;
    const ValidatedIntervalSink sink=[](std::span<const float>,bool) -> std::optional<ValidatedCapacityCycle> {
        throw std::runtime_error("Quiet or stopped Fast receiver emitted a coding interval");
    };
    Receiver modeled(p,sink,{},[&](std::complex<float>) {
        if(++input_points==1)throw std::runtime_error("Display observer failure");
    },stop.get_token());
    const std::array<float,4096> pcm{};
    require(modeled.uses_resilient_ssb() && modeled.consume(pcm)==pcm.size() && input_points>0,
        "Public typed SSB receiver did not consume actual PCM and report input I/Q");
    stop.request_stop();
    require(modeled.consume(pcm)==0,"Stopped modeled SSB receiver reported additional consumed input");
    modeled.finish();
    require(!modeled.progress().physical_complete && !modeled.physical_completion_input_tick(),
        "Modeled SSB EOF or cancellation fabricated an end");

    p.sample_rate=44100;
    Receiver fallback(p,sink,{},{},{});
    require(fallback.backend_name()=="standard-ssb" && !fallback.physical_completion_input_tick(),
        "Unsupported native input rate did not report the standard SSB receiver");
    p.sample_rate=48000;p.constellation=4096;p.code_rate=CodeRate::eight_ninths;p.ldpc_frame_bits=64800;
    Receiver high_rate(p,sink,{},{},{});
    require(high_rate.uses_resilient_ssb(),"The exact high-rate SSB profile lost the qualified typed receiver");
    p.pilot_spacing_symbols=64;
    Receiver custom(p,sink,{},{},{});
    require(custom.backend_name()=="standard-ssb","An unqualified high-rate variant inherited modeled dispatch");

    std::stop_source compact_stop;
    const auto compact=resolve_snr_preset(Channel::acoustic_short,-6).profile;
    Receiver compact_startup(compact,sink,{},[&](std::complex<float>) {compact_stop.request_stop();},
        compact_stop.get_token());
    require(compact_startup.backend_name()=="single-carrier-acquisition" && !compact_startup.uses_delay_doppler(),
        "Compact Fast reported an acquisition candidate as already selected");
    const auto consumed=compact_startup.consume(pcm);
    require(consumed>0 && consumed<pcm.size() && compact_startup.consume(pcm)==0,
        "Stopped competing Fast startup reported unconsumed capture samples");
    compact_startup.finish();
    require(!compact_startup.progress().acquired && !compact_startup.progress().physical_complete &&
            !compact_startup.physical_completion_input_tick(),
        "Cancelled compact startup or EOF fabricated acquisition or physical end");
}

void compact_fading_stream() {
    const auto p = resolve_snr_preset(Channel::acoustic_short, -6).profile;
    constexpr auto quota = 1024 * 1024;
    std::mt19937 generator(0x66617374);
    Bytes source(640);
    for (auto& byte : source) byte = static_cast<std::uint8_t>(' ' + generator() % 95);
    const auto prepared = prepare_xz_source(byte_source(source), quota);
    const auto expected_intervals = estimate_transmission(p, false, prepared.encoded.size()).intervals;
    require(cycle_intervals(p) == 1 && expected_intervals >= 6,
        "Compact recurring channel fixture no longer exercises multiple fixed one-interval cycles");
    auto encoder = fast::testing::deterministic_encoder(p, std::nullopt, byte_source(prepared.encoded), 417,
        SourceEncoding::xz);
    Transmitter tx(p, [&](std::span<std::uint8_t> bits) { return encoder.next_interval(bits); });
    SimulationSettings config;
    config.seed = 418;
    config.transmit_watts = 40;
    config.path_loss_db = 130;
    config.noise_dbm_hz = -174;
    config.baseband_ppm = 100;
    config.baseband_phase_rad_sqrt_s = .5 * std::numbers::pi / 180.;
    config.shift_ppm = 1;
    config.shift_phase_rad_sqrt_s = .05 * std::numbers::pi / 180.;
    config.shift_hz = 30e6;
    config.delay_ms = 7;
    config.doppler_hz = 30;
    config.selective_slow_depth = config.selective_fast_depth = .65;
    SimulationChannel channel(p, config, [&](std::span<std::complex<float>> out) { return tx.read_analytic(out); });
    StreamDecoder decoder(p, std::nullopt, quota, SourceEncoding::xz);
    std::size_t intervals = 0;
    Receiver receiver(p, [&](std::span<const float> soft, bool feedback) {
        ++intervals;
        return decoder.push_interval(soft, feedback);
    }, {}, {}, {});
    std::array<float,1201> chunk{};
    while (!receiver.progress().physical_complete) {
        const auto count = channel.read(chunk);
        if (!count) break;
        receiver.push(std::span(chunk).first(count));
        require(!decoder.result(), "Compact Fast source escaped before scored physical quiet");
    }
    receiver.finish();
    const auto quiet = channel.report().quiet_start_sample;
    const auto complete = receiver.physical_completion_input_tick();
    require(quiet && complete && *complete >= *quiet + 6ULL * p.sample_rate && receiver.uses_delay_doppler(),
        "Compact fading receiver failed its modeled acquisition or full quiet requirement");
    decoder.finish(receiver.progress().physical_complete);
    const auto stats = decoder.snapshot();
    const auto output = decoder.result();
    require(intervals == expected_intervals && stats.ldpc_frames == intervals && !stats.ldpc_failed_frames &&
        !stats.failed_cycles && output && std::ranges::equal(output->bytes(), source),
        "Compact recurring channel tracking lost exact source bytes or a fixed interval");
}

} // namespace

int main() try {
    simulation_audio_bounds();
    analytic_parity(profile(Channel::ssb));
    auto low_rate = profile(Channel::ssb);
    low_rate.ldpc_frame_bits = 64800;
    low_rate.symbol_rate = 600;
    analytic_parity(low_rate);
    analytic_parity(profile(Channel::acoustic_short));
    channel_tails();
    gaussian_spread_continuity();
    live_playback_excludes_synthetic_link_budget();
    continuous_noise_and_oscillators();
    measured_clock_and_shift();
    encrypted_session();
    cancel_simulation();
    compact_fading_stream();
    public_receiver_dispatch();
    std::cout << "Fast simulation tests passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
