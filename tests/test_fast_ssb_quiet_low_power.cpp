#include "datapump/fast/modem.hpp"
#include "fast/ssb_equalizer.hpp"
#include "fast/ssb_quiet_low_power.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

void require(bool condition, const char* failure) {
    if (!condition) throw std::runtime_error(failure);
}

std::vector<Complex> frontend(const SsbFrontendGeometry& geometry, std::span<const float> pcm) {
    std::vector<Complex> centers;
    SsbFrontend receiver(geometry, [&](const SsbFrontendOutput& block) {
        require(block.first_center_input_sample == geometry.radius() + centers.size() * geometry.decimation,
            "Fast SSB low-power fixture changed the observed center grid");
        centers.insert(centers.end(), block.samples.begin(), block.samples.end());
    });
    require(receiver.push(pcm) == pcm.size(), "Fast SSB low-power fixture lost input support");
    return centers;
}

SsbAcquisitionView view_for(const SsbFrontendGeometry& geometry, std::span<const Complex> values) {
    return {geometry.radius(), geometry.decimation, values};
}

SsbAcquisitionView crop(SsbAcquisitionView view, SsbCenterRange range) {
    const auto first = (range.first - view.first_center_input_tick) / view.stride;
    const auto count = (range.past_last - range.first) / view.stride;
    return {range.first, view.stride, view.samples.subspan(first, count)};
}

enum class Interference { white, tone, colored };

std::vector<Complex> make_noise(const SsbFrontendGeometry& geometry, std::size_t count, Interference kind) {
    std::vector<float> pcm(count);
    std::mt19937 random(0x41d7);
    std::normal_distribution<double> normal;
    double colored = 0.;
    for (std::size_t i = 0; i < count; ++i) {
        const auto tone = std::cos(2. * std::numbers::pi * 1877. * i / geometry.input_rate);
        const auto z = normal(random);
        colored = .60 * colored + .80 * z;
        switch (kind) {
            case Interference::white: pcm[i] = static_cast<float>(.00535 * z); break;
            case Interference::tone: pcm[i] = static_cast<float>(.00210 * tone); break;
            case Interference::colored: pcm[i] = static_cast<float>(.00230 * colored + .00130 * tone); break;
        }
    }
    return frontend(geometry, pcm);
}

struct Fixture {
    fast::Profile profile = fast::capacity_profile(fast::Channel::ssb);
    SsbFrontendGeometry geometry = make_ssb_frontend_geometry(profile.sample_rate, profile.carrier_hz);
    SsbAcquisitionLock lock{geometry.radius() + 4096, -double(geometry.radius() + 4096), 0.,
        {{0., 0., 1.}}, {}};
    SsbEqualizer model{profile, geometry, lock};
    std::vector<Complex> live;
    std::vector<SsbCenterRange> anchors, groups;
    std::size_t pcm_count = 0;

    Fixture() {
        const SingleCarrierSchedule schedule(profile);
        require(schedule.pilot_count == 16 && schedule.pilot_stride == 16 &&
            profile.constellation == 4, "Expected current canonical SSB quiet-control waveform");
        std::uint64_t state = 0x79c539b859e0d4f1ULL;
        unsigned generated = 0;
        fast::Transmitter tx(profile, [&](std::span<std::uint8_t> bits) {
            if (generated++ >= 3) return false;
            for (auto& bit : bits) {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                bit = state & 1U;
            }
            return true;
        });
        pcm_count = fast::transmission_samples(profile, 3);
        std::vector<float> pcm(pcm_count);
        require(tx.read(pcm) == pcm.size(), "Fast SSB low-power fixture transmitter ended early");
        live = frontend(geometry, pcm);
        const auto signal = view_for(geometry, live);
        anchors = model.quiet_groups(0);
        const auto decoded = model.estimate(signal, 0);
        require(decoded && decoded->presence.present && model.commit_present(*decoded, signal),
            "Fast SSB low-power fixture lacks a predictively verified signal");
        groups = model.quiet_groups(1);
    }

    SsbLowPowerQuiet anchored() const {
        SsbLowPowerQuiet quiet(geometry.input_rate, geometry.decimation, geometry.radius());
        require(quiet.verified_signal(view_for(geometry, live), anchors.front(), anchors.back()),
            "Fast SSB low-power fixture could not establish its actual reference");
        return quiet;
    }

    SsbQuietUnitDiagnostics measured(SsbAcquisitionView view, std::size_t index) const {
        SsbQuietUnitDiagnostics diagnostics;
        const auto scored = model.score_group(view, 1, index, {}, {}, &diagnostics);
        require(scored && *scored != SsbPublicPresence::unscored &&
            diagnostics.count >= ssb_quiet_minimum_groups, "Fast SSB low-power fixture public scoring failed");
        return diagnostics;
    }
};

void check_weak_coherent(const Fixture& fixture) {
    for (const auto gain : {.05, .02, .01}) {
        auto quiet = fixture.anchored();
        auto small = fixture.live;
        for (auto& sample : small) sample *= gain;
        const auto view = view_for(fixture.geometry, small);
        SsbQuietUnitDiagnostics diagnostics;
        const auto baseline = fixture.model.score_group(view, 1, 0, {}, {}, &diagnostics);
        require(baseline && *baseline != SsbPublicPresence::present,
            "Weak coherent control did not exercise the former amplitude boundary");
        const auto decision = quiet.observe(view, fixture.groups[0], *baseline, diagnostics,
            fixture.groups[0].first);
        require(decision.coherent_veto && decision.count == 1 &&
            decision.receipts[0].presence == SsbPublicPresence::present && !quiet.waiting() && !quiet.ceiling(),
            "Fast SSB low-power route missed a continuing weak public word");
        if (gain == .01) {
            const auto values = crop(view, fixture.groups[0]).samples;
            double sum = 0.;
            for (const auto sample : values) sum += std::norm(sample);
            require(sum / values.size() < *fixture.anchored().hard_ceiling(),
                "Weak coherent control did not cross below the independent raw power cap");
        }
        std::cout << "weak_gain=" << gain << " coherence=" << diagnostics.mean_squared_coherence
                  << " projected_amplitude=" << diagnostics.mean_amplitude << " veto=1\n";
    }
}

void check_calibration(const Fixture& fixture, Interference kind, std::string_view name) {
    const auto noise = make_noise(fixture.geometry, fixture.pcm_count, kind);
    const auto view = view_for(fixture.geometry, noise);
    // The first complete unit can straddle the physical support guard.
    // Start this calibration only at the next complete public unit.
    constexpr std::size_t first_index = 1;
    const auto guard = fixture.groups[first_index].first;
    auto quiet = fixture.anchored();
    const auto hard = *quiet.hard_ceiling();
    const auto a = fixture.measured(view, first_index);
    const auto b = fixture.measured(view, first_index + 1);
    const auto c = fixture.measured(view, first_index + 2);
    // Feed the valid native projections through the no-detection branch.
    // This isolates calibration from any separately established mixture null.
    constexpr auto pending = SsbPublicPresence::no_detection;
    const auto first = quiet.observe(view, fixture.groups[first_index], pending, a, guard);
    require(!first.count && quiet.waiting() && !quiet.ceiling(),
        "Fast SSB calibration credited its fitting unit before validation");
    const auto paired = quiet.observe(view, fixture.groups[first_index + 1], pending, b, guard);
    require(paired.count == 2 && quiet.ceiling() && !quiet.waiting() &&
        paired.receipts[0].range.first == fixture.groups[first_index].first &&
        paired.receipts[1].range.past_last == fixture.groups[first_index + 1].past_last &&
        paired.receipts[0].power_ceiling && paired.receipts[1].power_ceiling,
        "Fast SSB calibration did not independently validate both actual units");
    SsbQuietRun ledger(fixture.geometry.input_rate, fixture.geometry.decimation);
    for (const auto& receipt : std::span(paired.receipts).first(paired.count)) {
        require(!ledger.observe(view, receipt.range, receipt.presence, receipt.power_ceiling),
            "Fast SSB calibration pair claimed physical end");
    }
    require(ledger.elapsed_input_ticks() == paired.receipts[1].range.past_last - paired.receipts[0].range.first,
        "Fast SSB low-power ledger lost the exact retained calibration span");
    const auto cap = *quiet.ceiling();
    const auto accepted = quiet.observe(view, fixture.groups[first_index + 2], pending, c, guard);
    require(accepted.count == 1 && accepted.receipts[0].power_ceiling == cap,
        "Fast SSB calibrated route did not score subsequent low input");
    std::cout << name << " H=" << hard << " C=" << cap << " A_mean=" << *first.measured_mean_power
              << " B_mean=" << *paired.measured_mean_power << " subsequent_mean="
              << *accepted.measured_mean_power << " subsequent_peak10ms="
              << *accepted.measured_peak_block_mean << '\n';

    if (kind != Interference::white) return;
    auto louder = noise;
    for (auto& value : louder) value *= 8.;
    const auto loud = view_for(fixture.geometry, louder);
    const auto blocked = quiet.observe(loud, fixture.groups[first_index + 3], pending, fixture.measured(loud, first_index + 3), guard);
    require(blocked.count == 1 && !blocked.receipts[0].power_ceiling && quiet.ceiling() == cap,
        "Fast SSB low-power route raised its floor for loud unrelated input");
    const auto resumed = quiet.observe(view, fixture.groups[first_index + 4], pending, fixture.measured(view, first_index + 4), guard);
    require(resumed.count == 1 && resumed.receipts[0].power_ceiling == cap,
        "Fast SSB low-power route changed its frozen floor after interference");

    auto missing = fixture.anchored();
    require(!missing.observe(view, fixture.groups[first_index], pending, a, guard).count,
        "Fast SSB stale calibration control did not hold A");
    const auto expired = missing.observe(crop(view, fixture.groups[first_index + 1]), fixture.groups[first_index + 1], pending, b, guard);
    require(expired.count == 2 && expired.receipts[0].presence == SsbPublicPresence::unscored &&
        !expired.receipts[0].power_ceiling && !expired.receipts[1].power_ceiling && !missing.ceiling(),
        "Fast SSB low-power route credited discarded calibration samples");

    auto unscored = fixture.anchored();
    const auto invalid = unscored.observe(view, fixture.groups[first_index], SsbPublicPresence::unscored, a, guard);
    require(invalid.count == 1 && !invalid.receipts[0].power_ceiling && !unscored.waiting(),
        "Fast SSB low-power route admitted an unscored public range");

    auto before_guard = fixture.anchored();
    const auto early = before_guard.observe(view, fixture.groups[first_index], pending, a, guard + fixture.geometry.decimation);
    require(early.count == 1 && !early.receipts[0].power_ceiling && !before_guard.waiting(),
        "Fast SSB low-power route calibrated across the physical tail guard");
}

void check_relative_cap(const Fixture& fixture) {
    auto low = fixture.live;
    for (auto& sample : low) sample *= .40;
    const auto view = view_for(fixture.geometry, low);
    const auto power = [&](SsbCenterRange range) {
        const auto input = crop(view, range).samples;
        double total = 0.;
        for (const auto sample : input) total += std::norm(sample);
        return total / input.size();
    };
    SsbLowPowerQuiet quiet(fixture.geometry.input_rate, fixture.geometry.decimation, fixture.geometry.radius());
    require(quiet.verified_signal(view, fixture.anchors.front(), fixture.anchors.back()),
        "Fast SSB scaled reference was unsupported");
    const auto expected = .001 * std::min(power(fixture.anchors.front()), power(fixture.anchors.back()));
    require(expected < 2.5e-5 && std::abs(*quiet.hard_ceiling() - expected) < 1e-15,
        "Fast SSB low-power ceiling lost its independent relative cap");
}
} // namespace

int main() {
    Fixture fixture;
    check_weak_coherent(fixture);
    check_relative_cap(fixture);
    check_calibration(fixture, Interference::white, "white");
    check_calibration(fixture, Interference::tone, "tone");
    check_calibration(fixture, Interference::colored, "colored");
}
