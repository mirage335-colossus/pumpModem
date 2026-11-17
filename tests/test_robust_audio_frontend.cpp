#include "datapump/live.hpp"
#include "datapump/robust_audio_frontend.hpp"
#include "datapump/triad_stream_receiver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {
using datapump::live::detail::AudioInput;
using datapump::live::detail::real_pcm;
using datapump::triad::v3::Complex;

constexpr std::uint32_t rate = 6000;
constexpr double center = 1725.125, frequency = 700.0625, amplitude = .17;
constexpr std::uint64_t high_origin = (std::uint64_t{1} << 54) + 37;

void check(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}

Complex phase(std::uint64_t index, double hz) {
    return std::polar(1., 2 * std::numbers::pi * static_cast<double>(index) * hz / rate);
}

std::vector<float> waveform() {
    std::vector<float> result(10017);
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = real_pcm(amplitude * phase(i, frequency - center), i, center, rate);
    return result;
}

void continuity_and_passband() {
    const auto pcm = waveform();
    AudioInput whole(rate, center, 500, 2900), chunked(rate, center, 500, 2900, high_origin);
    std::vector<Complex> expected, part, observed;
    const auto first = whole.project(pcm, expected);
    check(first == whole.first_valid_sample() && expected.size() == pcm.size() - 2 * whole.halo_samples(),
          "Robust PCM projection changed its centered FIR coordinates");
    for (std::size_t at = 0; at < pcm.size(); at += 273) {
        chunked.project({}, part);
        check(part.empty(), "An empty chunk generated a synthetic audio sample");
        const auto begin = chunked.project(
            std::span(pcm).subspan(at, std::min<std::size_t>(273, pcm.size() - at)), part);
        if (!part.empty())
            check(begin == high_origin + first + observed.size(),
                  "Robust projection lost the absolute sample position across a chunk");
        observed.insert(observed.end(), part.begin(), part.end());
    }
    check(observed == expected, "Robust audio projection depends on chunk size or large absolute origin");
    double peak_error = 0;
    Complex conjugate_image{};
    for (std::size_t i = 0; i < observed.size(); ++i) {
        peak_error = std::max(peak_error, std::abs(observed[i] - amplitude * phase(first + i, frequency - center)));
        conjugate_image += observed[i] * phase(first + i, frequency + center);
    }
    check(peak_error < .001 && std::abs(conjugate_image) / static_cast<double>(observed.size()) < .0007,
          "Robust analytic projection did not preserve the wanted tone or suppress its image");
}

void erasure_and_eof() {
    auto pcm = waveform();
    constexpr std::size_t gap = 1703;
    pcm[gap] = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::uint8_t> valid(pcm.size(), 1), mask, chunk_mask, observed_mask;
    valid[gap] = 0;
    AudioInput whole(rate, center, 500, 2900, high_origin), chunked(rate, center, 500, 2900, high_origin);
    std::vector<Complex> expected, part, observed;
    const auto first = whole.project(pcm, expected, valid, &mask);
    check(mask.size() == expected.size(), "Robust projection omitted its physical validity mask");
    const auto halo = whole.halo_samples();
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const auto frame = first - high_origin + i;
        const bool affected = frame >= gap - halo && frame <= gap + halo;
        check(mask[i] == !affected && std::isfinite(expected[i].real()) && std::isfinite(expected[i].imag()),
              "A masked nonfinite audio sample did not invalidate the full FIR support");
    }
    for (std::size_t at = 0; at < pcm.size(); at += 301) {
        const auto count = std::min<std::size_t>(301, pcm.size() - at);
        chunked.project(std::span(pcm).subspan(at, count), part, std::span(valid).subspan(at, count), &chunk_mask);
        observed.insert(observed.end(), part.begin(), part.end());
        observed_mask.insert(observed_mask.end(), chunk_mask.begin(), chunk_mask.end());
    }
    check(observed == expected && observed_mask == mask, "Robust masked projection changed at a chunk edge");
    chunked.project({}, part, {}, &chunk_mask);
    check(part.empty() && chunk_mask.empty(), "Audio EOF invented observed filter-tail samples");
    AudioInput unmasked(rate, center, 500, 2900);
    try {
        unmasked.project(pcm, part);
        throw std::runtime_error("Unmasked nonfinite PCM entered Robust projection");
    } catch (const datapump::Error&) {}
}

void repeated_bit_pcm_stream() {
    using namespace datapump::triad::v3;
    constexpr std::uint32_t sample_rate = 1024;
    constexpr double carrier_hz = 256, pcm_sd = .02;
    constexpr double noise_envelope = 2 * pcm_sd * pcm_sd;
    constexpr std::size_t memory_limit = 1024ULL * 1024 * 1024;
    constexpr auto last_slot = FixedRepeatedBit::group_slots - 1;
    // Match the configured live basis calibration for every group, independent
    // of the realized signal energy, bit or noise in this fixture.
    const auto target_cn0_db_hz = datapump::live::Settings{}.target_cn0_db_hz;
    check(target_cn0_db_hz == 32, "Repeated-bit PCM fixture requires the selected 32 dB-Hz receiver target");
    const auto live_basis_scale = std::sqrt(std::pow(10., target_cn0_db_hz / 10) * noise_envelope / sample_rate);
    auto make_input = [] { return AudioInput(sample_rate, carrier_hz, 32, 480); };
    const auto halo = make_input().halo_samples();
    check(halo == 256, "Repeated-bit PCM fixture changed its native FIR support");

    PublicProfileConfig declaration;
    declaration.format.sample_rate = sample_rate;
    declaration.format.inner_rate_hz = 48;
    declaration.format.added_symbols = 1;
    declaration.format.bit_seconds = .001;
    declaration.format.pulse_shape = PulseShape::TaperedRrc;
    declaration.format.epoch = 1'793'855'000;
    declaration.format.symbol_ordinal = 7037343;
    declaration.format.ramp_seconds = .1;
    declaration.data_if_hz = carrier_hz;
    declaration.cfo_bound_hz = 2;
    declaration.capture_lead_seconds = .4;
    declaration.capture_trail_seconds = .12;
    declaration.receiver_filter_halo_samples = halo;
    ReceiverConfig config;
    config.sample_rate_hz = sample_rate;
    config.data_if_hz = carrier_hz;
    config.noise_variance = noise_envelope;
    config.time_varying_channel = true;
    config.time_varying_atmospheric_hz = config.time_varying_common_doppler_hz = declaration.cfo_bound_hz;
    config.capture_tail_bound_seconds = static_cast<double>(halo) / sample_rate;
    const auto key = datapump::Crypto::amateur();
    const PublicProfile profile(declaration, key);
    const auto first = profile.slot(0);
    const auto extent = profile.slot(last_slot).observation.stop;
    const auto raw_extent = extent + 2 * halo;
    check(first.observation.first == 9'984 && raw_extent <= 1'500'000 &&
          profile.geometry().occupied_bandwidth_hz < 2 * (carrier_hz - 32),
          "Repeated-bit PCM fixture expanded beyond the sample or audio band budget");

    struct TestCase {
        const char* name;
        std::optional<std::uint8_t> bit;
        double data_energy_over_noise;
        bool tone = false;
    };
    const std::array cases{TestCase{"zero", 0, 64}, TestCase{"one", 1, 64},
        TestCase{"noise", {}, 0}, TestCase{"coherent-line", {}, 0, true},
        TestCase{"weak-one", 1, 22}};
    for (const auto& test : cases) {
        EmissionOptions request;
        if (test.bit) request.repeated_bit = test.bit;
        else {
            request.mode = EmissionMode::Quiet;
            request.slot_count = FixedRepeatedBit::group_slots;
        }
        Emission emission(profile, key, request);
        auto receiver_config = config;
        receiver_config.data_basis_gain = live_basis_scale * emission.report().data_gain;
        StreamChannelConfig channel_config;
        channel_config.effects.sample_rate = sample_rate;
        channel_config.effects.occupied_bandwidth_hz = profile.geometry().occupied_bandwidth_hz;
        // Raw PCM starts h samples before calendar zero. Centered IQ emitted
        // at raw index h is the first public sample, with true prehistory.
        channel_config.effects.capture_lead_seconds = declaration.capture_lead_seconds +
            static_cast<double>(halo) / sample_rate;
        channel_config.effects.capture_trail_seconds = declaration.capture_trail_seconds;
        channel_config.effects.nominal_conversion_if_hz = carrier_hz;
        channel_config.effects.noise_enabled = false;
        channel_config.minimum_capture_samples = raw_extent;
        channel_config.signal_gain = std::sqrt(test.data_energy_over_noise * noise_envelope /
                                              profile.geometry().source_samples);
        StreamingChannel channel(emission.source(), channel_config);
        auto input = make_input();
        StreamReceiverOptions options;
        options.fixed_repeated_bit = true;
        options.repeat_audio_input_halo_samples = input.halo_samples();
        options.memory_limit_bytes = memory_limit;
        CompleteStreamReceiver receiver(profile, receiver_config, key, options);

        const auto quiet_rows = first.quiet_aperture.stop - first.quiet_aperture.first;
        const auto tone = test.tone ? std::sqrt(80. * noise_envelope / quiet_rows) : 0.;
        std::mt19937_64 rng(74613);
        std::normal_distribution<double> noise(0, pcm_sd);
        std::array<Complex, 4096> signal;
        std::array<float, 4096> pcm;
        std::vector<Complex> iq;
        std::vector<std::uint8_t> valid;
        std::uint64_t received = 0;
        while (channel.position() < raw_extent) {
            const auto raw_first = channel.position();
            const auto count = channel.read(std::span(signal).first(
                std::min<std::uint64_t>(signal.size(), raw_extent - raw_first)));
            check(count > 0, "Repeated-bit PCM input ended before its native filter support");
            for (std::size_t at = 0; at < count; ++at)
                pcm[at] = static_cast<float>(real_pcm(signal[at] + tone, raw_first + at, carrier_hz, sample_rate) +
                                             noise(rng));
            const auto projected_first = input.project(std::span(pcm).first(count), iq, {}, &valid);
            if (iq.empty()) continue;
            check(projected_first == halo + received && iq.size() == valid.size() &&
                  std::ranges::all_of(valid, [](std::uint8_t value) { return value == 1; }),
                  "Repeated-bit PCM projection lost a complete raw aperture");
            while (receiver.push(received, iq, valid) == StreamPushResult::backpressured)
                check(receiver.wait_for_slot_completion().state == StreamReceiverState::receiving,
                      "Repeated-bit PCM stream failed during native backpressure");
            received += iq.size();
        }
        const auto report = receiver.finish_input();
        const auto slots = receiver.drain_recent_slots();
        const auto groups = receiver.drain_repeated_bits();
        check(received == extent && report.state == StreamReceiverState::input_ended &&
              report.peak_reserved_bytes <= memory_limit && slots.size() == FixedRepeatedBit::group_slots,
              "Repeated-bit PCM group did not finish its bounded complete native windows");
        unsigned data = 0, quiet = 0, quiet_run = 0;
        bool observed_data = false, conditioned_line = false;
        double probe_seconds = 0;
        std::vector<std::size_t> physical_stops;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const auto& s = slots[i].score;
            check(s.complete && s.all_valid && s.zero_h0_verified && s.noise_statistics_valid &&
                  s.index == i && slots[i].symbol_ordinal == declaration.format.symbol_ordinal + i &&
                  s.repeat_bit_evidence.has_value() == FixedRepeatedBit::is_attempt(i),
                  "Repeated-bit PCM input did not preserve scheduled score provenance");
            if (s.decision == SlotDecision::data) {
                check(test.bit && FixedRepeatedBit::is_attempt(i) && s.bit == test.bit,
                      "Repeated-bit PCM receiver produced wrong or unscheduled ordinary DATA");
                ++data;
                observed_data = true;
            }
            quiet += s.decision == SlotDecision::quiet;
            if (observed_data && s.decision == SlotDecision::quiet) {
                if (++quiet_run == 2) {
                    physical_stops.push_back(i + 1);
                    observed_data = false;
                    quiet_run = 0;
                }
            } else quiet_run = 0;
            if (s.repeat_bit_evidence)
                conditioned_line |= s.varying_line_log_evidence > 0 ||
                    *s.repeat_bit_evidence != s.operational_bit_evidence;
            probe_seconds += s.repeat_extra_line_seconds;
        }
        const auto messages = receiver.drain_messages();
        std::cout << "repeat_pcm case=" << test.name << " events=" << groups.size() << " ordinary_data=" << data
                  << " quiet=" << quiet << " physical_completions=" << messages.size()
                  << " conditioned_line=" << conditioned_line << " raw_samples=" << raw_extent << " halo=" << halo
                  << " nominal_e=" << test.data_energy_over_noise << " live_target_db_hz=" << target_cn0_db_hz
                  << " W=" << report.resources.workers << " reserved=" << report.peak_reserved_bytes
                  << " extra_line_s=" << probe_seconds;
        if (!groups.empty()) std::cout << " attempts=" << groups.front().attempts << " log_e=" << groups.front().log_evidence;
        std::cout << std::endl;
        check(messages.size() == physical_stops.size(), "Repeated-bit PCM event changed ordinary completion count");
        for (std::size_t i = 0; i < messages.size(); ++i)
            check(messages[i].message.physical_complete && messages[i].stop_public_slot == physical_stops[i],
                  "Repeated-bit PCM event changed a physically observed message endpoint");
        check(!test.tone || conditioned_line, "Repeated-bit PCM tone control did not exercise line conditioning");
        if (test.bit) {
            check(groups.size() == 1 && groups.front().bit == test.bit && groups.front().first_public_slot == 0 &&
                  groups.front().first_symbol_ordinal == declaration.format.symbol_ordinal &&
                  groups.front().last_public_slot == (groups.front().attempts - 1) * FixedRepeatedBit::spacing_slots,
                  "Repeated-bit PCM receiver missed the agreed group");
            if (test.data_energy_over_noise == 22)
                check(!data, "Weak repeated-bit PCM fixture admitted an ordinary DATA attempt");
        } else check(groups.empty(), "Repeated-bit PCM noise or tone admitted an absent group");
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "repeat-pcm") repeated_bit_pcm_stream();
        else if (argc == 1) {
            continuity_and_passband();
            erasure_and_eof();
        } else throw std::runtime_error("Unknown Robust audio test selection");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
