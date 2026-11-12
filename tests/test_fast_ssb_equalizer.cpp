#include "datapump/fast/modem.hpp"
#include "fast/ssb_equalizer.hpp"
#include "fast/ssb_soft_calibration.hpp"
#include "fast_test_key.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
namespace fast = datapump::fast;
using namespace datapump::fast::detail;
using Complex = std::complex<double>;

static_assert(ssb_demapper_variance_guard(4) == .015);
static_assert(ssb_demapper_variance_guard(16) == .015);
static_assert(ssb_demapper_variance_guard(4096) == 6. / (4095 * 16));

void require(bool condition, const char* failure) {
    if (!condition) throw std::runtime_error(failure);
}

void check_filtered_row_derivatives(const fast::Profile& p, const SsbFrontendGeometry& frontend) {
    const auto reference = frontend.radius() + 24000;
    const auto center = reference + 131 * frontend.decimation;
    const SsbAcquisitionLock lock{reference, -14762.13, 120e-6,
        {{.37, -117.9, {.76, .21}}, {876.9, 93.7, {.35, -.51}}}, {}};
    SsbChannelModel model(p, frontend, lock);
    const SsbTimingBasis timing{370.7, 1230.};
    std::array<SsbChannelTerm, SsbChannelModel::maximum_row_symbols> scratch{};
    const auto base = model.row(center, scratch, timing);
    const auto t = model.relative_ticks(center);
    const auto beta = p.symbol_rate / p.sample_rate;
    const auto tx_radius = single_carrier_pulse_radius(p);
    const SsbPulse direct(p, frontend);
    for (std::size_t k = 0; k < base.terms.size(); ++k) {
        SsbChannelTerm expected;
        for (std::size_t a = 0; a < lock.atoms.size(); ++a) {
            const auto& atom = lock.atoms[a];
            const auto u = (t - lock.epoch_from_reference_ticks - atom.delay_ticks) * beta -
                tx_radius - (base.first_symbol + k);
            const auto value = direct.evaluate(u, t, lock.clock_ratio_error, atom.frequency_hz);
            expected.value += atom.gain * value.value;
            expected.relative += atom.gain * value.value * (a == 0 ? -.5 : .5);
            expected.offset += atom.gain * value.offset_ticks_derivative;
            expected.slope += atom.gain * (value.clock_ratio_derivative -
                timing.midpoint_ticks * value.offset_ticks_derivative) / timing.half_span_ticks;
        }
        const auto& got = base.terms[k];
        require(std::abs(got.value - expected.value) < 3e-6, "Fast SSB channel row differs from direct filtered paths");
        require(std::abs(got.relative - expected.relative) < 3e-6, "Fast SSB relative path derivative is wrong");
        require(std::abs(got.offset - expected.offset) < 3e-5, "Fast SSB timing derivative is wrong");
        require(std::abs(got.slope - expected.slope) < 3e-5, "Fast SSB timing slope lost filter tap time");
    }
}

void check_group_discriminator(const fast::Profile& p, const SsbFrontendGeometry& geometry,
        SsbAcquisitionView live, const SsbAcquisitionLock& lock, const SsbEqualizer& receiver) {
    constexpr std::uint64_t ordinal = 1;
    const auto units = receiver.quiet_groups(ordinal);
    const auto capture = units.back().past_last + geometry.radius();
    const SingleCarrierSchedule schedule(p);
    require(schedule.marker_symbols(ordinal) == 0, "Fast SSB group fixture requires a marker-free interval");
    const SsbChannelModel model(p, geometry, lock);
    auto symbol = schedule.training_count + schedule.total_interval_symbols(ordinal);
    std::vector<SsbCenterRange> groups;
    for (auto data = schedule.data_symbols; data;) {
        const auto count = std::min(data, schedule.pilot_stride);
        const auto first = model.center_at_or_after_symbol(symbol);
        symbol += count + schedule.pilot_count;
        groups.push_back({first, model.center_at_or_after_symbol(symbol)});
        data -= count;
    }
    const auto unrelated = [&](bool noise) {
        std::vector<Complex> result;
        SsbFrontend frontend(geometry, [&](const SsbFrontendOutput& output) {
            result.insert(result.end(), output.samples.begin(), output.samples.end());
        });
        std::mt19937 random(65739);
        std::normal_distribution<float> gaussian(0.F, .25F);
        std::array<float, 1024> pcm{};
        for (std::uint64_t first = 0; first < capture;) {
            const auto count = std::min<std::uint64_t>(pcm.size(), capture - first);
            for (std::size_t i = 0; i < count; ++i)
                pcm[i] = noise ? gaussian(random) : static_cast<float>(.3 *
                    std::cos(2. * std::numbers::pi * 1877. * (first + i) / p.sample_rate));
            require(frontend.push(std::span(pcm).first(count)) == count, "Fast SSB group control truncated");
            first += count;
        }
        return result;
    };
    const auto noise = unrelated(true), tone = unrelated(false);
    const auto energy = [](std::span<const Complex> samples) {
        double power = 0.;
        for (const auto x : samples) power += std::norm(x);
        return power;
    };
    enum Arm { live_waveform, lower_gain, higher_gain, equal_power_noise, equal_power_tone, zero,
        live_fragment, phase_jump, arm_count };
    std::array<std::array<std::size_t, 4>, arm_count> counts{};
    std::size_t at_group = 0;
    for (std::size_t i = 0; i < units.size(); ++i) {
        const auto range = units[i];
        require(at_group < groups.size() && groups[at_group].first == range.first,
            "Fast SSB quiet unit skipped a public group");
        const auto start = (range.first - live.first_center_input_tick) / live.stride;
        const auto count = (range.past_last - range.first) / live.stride;
        const auto signal = live.samples.subspan(start, count);
        std::array<std::vector<Complex>, arm_count> samples;
        for (auto& arm : samples) arm.resize(count);
        std::size_t members = 0;
        do {
            const auto group = groups.at(at_group++);
            const auto offset = (group.first - range.first) / live.stride;
            const auto size = (group.past_last - group.first) / live.stride;
            const auto wave = signal.subspan(offset, size);
            const auto random = std::span(noise).subspan(start + offset, size);
            const auto sinusoid = std::span(tone).subspan(start + offset, size);
            const auto power = energy(wave);
            const auto noise_gain = std::sqrt(power / energy(random));
            const auto tone_gain = std::sqrt(power / energy(sinusoid));
            for (std::size_t j = 0; j < size; ++j) {
                samples[live_waveform][offset + j] = wave[j];
                samples[lower_gain][offset + j] = .25 * wave[j];
                samples[higher_gain][offset + j] = 3.5 * wave[j];
                samples[equal_power_noise][offset + j] = noise_gain * random[j];
                samples[equal_power_tone][offset + j] = tone_gain * sinusoid[j];
                samples[live_fragment][offset + j] = members == 0 ? wave[j] : noise_gain * random[j];
                samples[phase_jump][offset + j] = members % 2 ? -wave[j] : wave[j];
            }
            ++members;
        } while (groups[at_group - 1].past_last < range.past_last);
        require(groups[at_group - 1].past_last == range.past_last && members >= ssb_quiet_minimum_groups &&
                members <= ssb_quiet_maximum_groups,
            "Fast SSB quiet unit changed its complete group bounds");
        for (std::size_t arm = 0; arm < samples.size(); ++arm) {
            const SsbAcquisitionView view{range.first, live.stride, samples[arm]};
            SsbQuietUnitDiagnostics diagnostics;
            const auto result = receiver.score_group(view, ordinal, i, {}, {},
                arm == equal_power_noise ? &diagnostics : nullptr);
            require(bool(result), "Fast SSB complete quiet unit requested absent context");
            ++counts[arm][static_cast<std::size_t>(*result)];
            if (arm == equal_power_noise) {
                std::cout << "noise unit=" << i << " ticks=" << range.first << ".." << range.past_last
                          << " classification=" << static_cast<unsigned>(*result) << " informative="
                          << diagnostics.informative << '/' << diagnostics.count
                          << " mean_squared_coherence=" << diagnostics.mean_squared_coherence
                          << " mean_amplitude=" << diagnostics.mean_amplitude << std::endl;
                if (*result == SsbPublicPresence::no_detection) {
                    for (std::size_t j = 0; j < diagnostics.count; ++j) {
                        const auto& d = diagnostics.groups[j];
                        std::cout << "  group=" << j << " centers=" << d.centers << " observable=" << d.informative
                                  << " raw_template=" << d.raw_template_energy << " projected_template="
                                  << d.projected_template_energy << " projected_input=" << d.projected_input_energy
                                  << " cross_squared=" << d.cross_squared << " squared_coherence="
                                  << d.squared_coherence << " amplitude=" << d.amplitude << " strong=" << d.strong << std::endl;
                    }
                }
            }
        }
        require(!receiver.score_group({range.first, live.stride, signal.first(signal.size() - 1)}, ordinal, i),
            "Fast SSB scored a quiet unit with unobserved native centers");
        require(receiver.score_group({range.first + live.stride, live.stride, signal.subspan(1)}, ordinal, i) ==
                SsbPublicPresence::unscored, "Fast SSB scored a quiet unit with discarded native centers");
    }
    require(at_group == groups.size(), "Fast SSB quiet units omitted the short final public group");
    constexpr std::array labels{"live", "gain .25", "gain 3.5", "equal-power noise", "equal-power tone", "zero",
        "isolated live fragment", "phase jump"};
    for (std::size_t i = 0; i < counts.size(); ++i)
        std::cout << labels[i] << " unscored=" << counts[i][0] << " present=" << counts[i][1]
                  << " no_detection=" << counts[i][2] << " absent=" << counts[i][3] << std::endl;
    for (const auto arm : {live_waveform, lower_gain, higher_gain, live_fragment, phase_jump})
        require(counts[arm][static_cast<std::size_t>(SsbPublicPresence::present)] == units.size(),
            "Fast SSB quiet unit missed a continuing waveform or a strong fragment");
    for (const auto arm : {zero, equal_power_noise, equal_power_tone})
        require(counts[arm][static_cast<std::size_t>(SsbPublicPresence::absent)] == units.size(),
            "Fast SSB normalized null did not reject all matched-power unrelated units");
}

void run(bool complete_cycle, bool group_control, bool dense_calibration) {
    auto p = fast::capacity_profile(fast::Channel::ssb);
    if (dense_calibration) {
        p.ldpc_frame_bits = 64800;
        p.constellation = 4096;
        p.code_rate = fast::CodeRate::eight_ninths;
    }
    const SingleCarrierSchedule schedule(p);
    if (group_control) {
        require(p.constellation == 4 && p.code_rate == fast::CodeRate::half && p.interleave_depth == 1 &&
                p.ldpc_frame_bits == 16200 && schedule.pilot_stride == 16 && schedule.pilot_count == 16,
            "Fast SSB quiet control requires the canonical dense-pilot QPSK profile");
        std::cout << "canonical profile_id=";
        constexpr char digits[] = "0123456789abcdef";
        for (const auto byte : fast::profile_id(p)) std::cout << digits[byte >> 4] << digits[byte & 15];
        std::cout << " sample_rate=" << p.sample_rate << " baud=" << p.symbol_rate << " constellation=" << p.constellation
                  << " rate=" << fast::code_rate_name(p.code_rate) << " depth=" << p.interleave_depth
                  << " ldpc_bits=" << p.ldpc_frame_bits << " training=" << schedule.training_count
                  << " marker=" << schedule.marker_span << '/' << schedule.marker_cadence << " data/pilots="
                  << schedule.pilot_stride << '/' << schedule.pilot_count << std::endl;
    }
    const auto geometry = make_ssb_frontend_geometry(p.sample_rate, p.carrier_hz);
    check_filtered_row_derivatives(p, geometry);

    const auto key = fast_test::sequence_key();
    auto encoder = fast::testing::deterministic_encoder(p, key,
        fast::byte_source(datapump::Bytes{5, 8, 13, 21, 34}), 0x536e);
    const auto intervals = complete_cycle ? fast::cycle_intervals(p) :
        std::size_t{group_control || dense_calibration ? 2U : 1U};
    std::vector<std::array<std::uint8_t, fast::physical_interval_bits>> emitted;
    fast::Transmitter tx(p, [&](std::span<std::uint8_t> bits) {
        if (emitted.size() == intervals) return false;
        require(encoder.next_interval(bits), "Fast native encoder ended before bootstrap");
        std::array<std::uint8_t, fast::physical_interval_bits> fixture{};
        std::copy(bits.begin(), bits.end(), fixture.begin());
        emitted.push_back(fixture);
        return true;
    });
    std::vector<Complex> centers;
    SsbFrontend frontend(geometry, [&](const SsbFrontendOutput& output) {
        centers.insert(centers.end(), output.samples.begin(), output.samples.end());
    });
    std::array<float, 1024> block{};
    const auto lead = p.sample_rate * 26 / 1000;
    require(frontend.push(std::span(block).first(1024)) == 1024, "Fast equalizer fixture lead truncated");
    require(frontend.push(std::span(block).first(lead - 1024)) == lead - 1024,
        "Fast equalizer fixture lead truncated");
    for (;;) {
        const auto count = tx.read(block);
        if (!count) break;
        require(frontend.push(std::span(block).first(count)) == count, "Fast equalizer fixture PCM truncated");
    }
    if (!group_control) {
        const auto padding = fast::transmission_samples(p, intervals + 2) - fast::transmission_samples(p, intervals) +
            p.sample_rate / 10;
        block.fill(0.F);
        for (auto left = padding; left;) {
            const auto count = std::min<std::uint64_t>(left, block.size());
            require(frontend.push(std::span(block).first(count)) == count, "Fast equalizer silence truncated");
            left -= count;
        }
    }

    const SsbAcquisitionView view{geometry.radius(), geometry.decimation, centers};
    const DelayDopplerFit fit(p, geometry);
    const auto word = 64. * p.sample_rate / p.symbol_rate;
    const SsbAcquisitionRequest request{-6 * word, -3 * word, 7056, 24, 280., .022 * p.sample_rate, .0003};
    const auto acquired = fit.fit(view, request);
    require(bool(acquired), "Fast equalizer fixture did not acquire from public waveform");
    SsbEqualizer receiver(p, geometry, *acquired);
    if (group_control) {
        check_group_discriminator(p, geometry, view, *acquired, receiver);
        return;
    }
    fast::StreamDecoder decoder(p, key);
    std::optional<fast::ValidatedCapacityCycle> verified;
    std::size_t largest = 0;
    for (std::size_t i = 0; i < intervals; ++i) {
        const auto range = receiver.required_centers(i);
        largest = std::max(largest, static_cast<std::size_t>((range.past_last - range.first) / geometry.decimation));
        if (i == 0) {
            auto partial = view;
            partial.samples = partial.samples.first((range.past_last - view.first_center_input_tick) /
                view.stride - 1);
            require(!receiver.estimate(partial, i), "Fast equalizer used unobserved lookahead");
            const auto groups = receiver.quiet_groups(i);
            const auto short_group = groups.back().past_last - groups.back().first;
            const auto total_groups = (schedule.data_symbols + schedule.pilot_stride - 1) / schedule.pilot_stride + 2;
            const auto tail_groups = ssb_quiet_minimum_groups + total_groups % ssb_quiet_minimum_groups;
            const auto last_data = (schedule.data_symbols - 1) % schedule.pilot_stride + 1;
            const auto tail_symbols = last_data + schedule.pilot_count +
                (tail_groups - 1) * (schedule.pilot_stride + schedule.pilot_count);
            require(std::abs(double(short_group) - tail_symbols * p.sample_rate / p.symbol_rate) <= 2. * view.stride,
                "Fast quiet unit omitted the short final public data group or pilots");
        }
        const auto evidence = receiver.estimate(view, i);
        if (!evidence || !evidence->presence.present) {
            if (evidence) std::cerr << "ordinal=" << i << " coherence=" << evidence->presence.coherence
                << " amplitude=" << evidence->presence.amplitude << " power=" << evidence->presence.projected_power << '\n';
            throw std::runtime_error("Fast equalizer missed native present interval");
        }
        for (std::size_t bit = 0; bit < evidence->soft_bits.size(); ++bit)
            require((evidence->soft_bits[bit] > 0.F) == bool(emitted[i][bit]) && evidence->soft_bits[bit] != 0.F,
                "Fast equalizer changed a clean native bit");
        if (dense_calibration) {
            std::vector<float> weak_bits;
            for (std::size_t bit = 5; bit < evidence->soft_bits.size(); bit += 6)
                weak_bits.push_back(std::abs(evidence->soft_bits[bit]));
            const auto median = weak_bits.begin() + weak_bits.size() / 2;
            std::nth_element(weak_bits.begin(), median, weak_bits.end());
            require(i == 0 ? *median < .2F : *median > 1.F,
                "Fast sparse equalizer did not preserve startup and use prior pilots for dense QAM");
        }
        require(receiver.commit_present(*evidence, view), "Fast equalizer did not commit observed interval");
        require(!receiver.commit_present(*evidence, view), "Fast equalizer replayed a stale tracker proposal");
        if (receiver.wants_cycle_feedback(i)) verified = decoder.push_interval_with_feedback(evidence->soft_bits);
        else decoder.push_interval(evidence->soft_bits);
    }
    require(largest <= receiver.maximum_observation_centers(), "Fast equalizer exceeded declared interval storage");
    require(!decoder.result() && !decoder.snapshot().physical_end, "Fast equalizer exposed source without quiet");
    if (complete_cycle) {
        require(verified && verified->wire_ordinal == 0 &&
            verified->validation == fast::CapacityCycleValidation::authenticated,
            "Fast equalizer did not receive native authenticated protected cycle");
        const auto update = receiver.finish_coding_cycle(0, verified);
        require(update.attempted && !receiver.wants_cycle_feedback(intervals - 1),
            "Fast equalizer did not consume bounded verified history");
    }
    // Allow one whole missed public interval for the finite transmit tail.
    const auto quiet = receiver.estimate(view, intervals + 1);
    require(quiet && !quiet->presence.present && !quiet->tracker_proposed,
        "Fast equalizer treated observed silence as a present interval");
    require(!receiver.commit_present(*quiet, view), "Fast equalizer updated from absent interval");
    decoder.finish(false);
    require(!decoder.result() && !decoder.snapshot().physical_end,
        "Fast equalizer promoted transport EOF to physical completion");
    std::cout << "Fast SSB clean " << intervals << " interval(s), actual centered pulse derivatives, quiet/EOF controls\n";
}

} // namespace

int main(int argc, char** argv) {
    const auto mode = argc == 2 ? std::string_view(argv[1]) : std::string_view{};
    if (mode != "--dense-calibration") run(mode == "--cycle", mode == "--groups", false);
    if (mode.empty() || mode == "--dense-calibration") run(false, false, true);
}
