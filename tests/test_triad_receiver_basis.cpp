#include "triad_receiver_basis.hpp"
#include "triad_crypto_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <future>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace v = datapump::triad::v3;
namespace r = v::receiver_detail;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F work) {
    bool rejected = false;
    try { work(); } catch (const datapump::Error&) { rejected = true; }
    check(rejected, "invalid mapped receiver request accepted");
}
datapump::Crypto key() {
    std::array<std::uint8_t, 32> bytes{};
    for (std::size_t i = 0; i != bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(3+17*i);
    return triad_test_key(bytes);
}
v::ReceiverConfig config() {
    v::ReceiverConfig result;
    result.sample_rate_hz = 6000;
    result.data_if_hz = 1725;
    result.data_basis_gain = .71;
    return result;
}
v::ReceiverSlotPlan slot() {
    v::ReceiverSlotPlan result;
    result.format.sample_rate = 6000;
    result.format.inner_rate_hz = 36;
    result.format.added_symbols = 1;
    result.format.bit_seconds = 2;
    result.format.pulse_shape = v::PulseShape::TaperedRrc;
    return result;
}
std::vector<double> positions(std::size_t count) {
    std::vector<double> result(count);
    for (std::size_t i = 0; i != count; ++i) result[i] = i+.17+.03*std::sin(.01*i);
    return result;
}
std::vector<v::Complex> direct(const v::ReceiverConfig& settings, const v::ReceiverSlotPlan& plan,
                               const datapump::Crypto& crypto, const r::MappedBasisRow& row,
                               std::span<const v::Tap> taps, std::uint64_t first,
                               std::span<const double> offsets) {
    auto format = plan.format;
    format.epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(format.epoch)+row.epoch_offset_seconds);
    v::Candidate candidate;
    candidate.arrival_seconds = row.arrival_seconds;
    candidate.frequency_hz = row.frequency_hz;
    candidate.clock_ppm = row.clock_ppm;
    candidate.channel.taps.assign(taps.begin(), taps.end());
    std::vector<v::Complex> result(2*taps.size()*offsets.size());
    for (unsigned bit = 0; bit != 2; ++bit) {
        v::PatternSource source(format, crypto, static_cast<std::uint8_t>(bit));
        auto destination = std::span(result).subspan(bit*taps.size()*offsets.size(), taps.size()*offsets.size());
        v::shaped_basis_mapped(source, candidate, settings.sample_rate_hz, first, offsets, destination,
                               settings.data_if_hz);
    }
    for (auto& value : result) value *= settings.data_basis_gain;
    return result;
}
void close(std::span<const v::Complex> actual, std::span<const v::Complex> expected) {
    check(actual.size() == expected.size(), "basis comparison geometry changed");
    double error = 0, energy = 0, peak = 0;
    for (std::size_t i = 0; i != actual.size(); ++i) {
        error += std::norm(actual[i]-expected[i]);
        energy += std::norm(expected[i]);
        peak = std::max(peak, std::abs(actual[i]-expected[i]));
    }
    // The existing actual-native cache gate qualifies this interpolation at
    // RMS < .001 and peak < .004 (test_triad_tracking_basis.py). A direct call
    // is exact; the public metadata-selected cubic cache is an approximation.
    check(error <= 1e-6*std::max(energy, 1e-20) && peak < .004,
          "cached basis differs from direct native map");
}

void cached_maps(const datapump::Crypto& crypto) {
    const auto offsets = positions(257);
    const std::array<v::Tap, 2> taps{{{0,.2},{.007,-.4}}};
    const r::MappedBasisRow row{2,.013,1.25,127.25};
    for (const auto source_rate : {6000U, 4800U}) {
        auto plan = slot();
        plan.format.sample_rate = source_rate;
        const auto settings = config();
        r::MappedBasis basis(settings, plan, crypto, offsets.size(), 2);
        const auto end = static_cast<std::uint64_t>(std::ceil(
            static_cast<long double>(v::geometry(plan.format).source_samples)*settings.sample_rate_hz/source_rate));
        for (const auto first : {UINT64_C(0), UINT64_C(12500), UINT64_C(25000), end-200, end+1000}) {
            std::vector<v::Complex> actual(4*offsets.size()), again(actual.size());
            basis.render_pair(row, taps, first, offsets, actual);
            basis.render_pair(row, taps, first, offsets, again);
            close(actual, direct(settings, plan, crypto, row, taps, first, offsets));
            check(actual == again, "immutable cache replay changed samples");
        }
        const auto report = basis.report();
        check(report.fallback_reason.empty() && report.interpolated_rows && !report.direct_rows,
              "smooth supported waveform did not use cached interpolation");
        check(report.cache_hits && report.cache_misses && report.peak_cache_bytes <= 32*1024*1024 &&
                report.peak_pinned_tiles <= 32, "mapped cache bound or reuse failed");
        check(r::MappedBasis::workspace_bytes(settings, plan, offsets.size(), 2) >= report.peak_cache_bytes,
              "mapped workspace omits cache");
    }
}

void concurrent_pairs(const datapump::Crypto& crypto) {
    const auto offsets = positions(401);
    const std::array<v::Tap, 1> taps{{{0,0}}};
    const auto settings = config();
    const auto plan = slot();
    r::MappedBasis basis(settings, plan, crypto, offsets.size(), 4);
    std::array<std::future<std::vector<v::Complex>>, 4> work;
    for (unsigned i = 0; i != work.size(); ++i) {
        work[i] = std::async(std::launch::async, [&, i] {
            std::vector<v::Complex> result(2*offsets.size());
            basis.render_pair({static_cast<std::int64_t>(i%2), .01, .125, 17.}, taps, 25000, offsets, result);
            return result;
        });
    }
    const auto zero = work[0].get(), one = work[1].get();
    check(zero == work[2].get() && one == work[3].get(), "concurrent paired cache changed hypotheses");
    check(std::any_of(zero.begin(), zero.end(), [](auto value) { return std::norm(value) > 1e-10; }),
          "concurrent fixture missed active DATA support");
    close(zero, direct(settings, plan, crypto, {0,.01,.125,17.}, taps, 25000, offsets));
}

void dense_and_gated(const datapump::Crypto& crypto) {
    const auto settings = config();
    const auto offsets = positions(257);
    const std::array<v::Tap, 1> taps{{{0,.2}}};
    const r::MappedBasisRow row{2,.00013,1.25,127.25};
    for (const bool gated : {false, true}) {
        auto plan = slot();
        plan.format.inner_rate_hz = 3600;
        plan.format.bit_seconds = .001;
        plan.format.fhss = gated;
        plan.format.quiet_seconds = gated ? .055 : 0;
        plan.format.ramp_seconds = .02;
        plan.format.burst_wait_tiles = gated ? 1 : 0;
        r::MappedBasis basis(settings, plan, crypto, offsets.size(), 1);
        std::vector<v::Complex> output(2*offsets.size());
        const auto first = v::geometry(plan.format).calendar_origin_sample+101;
        basis.render_pair(row, taps, first, offsets, output);
        close(output, direct(settings, plan, crypto, row, taps, first, offsets));
        const auto report = basis.report();
        check(report.fallback_reason.empty() && report.interpolated_rows,
              "dense or gated source did not use qualified cache");
        if (!gated) check(report.dense_factor == 3, "high-rate cache did not prepare dense samples");
    }
}

void fallback_and_validation(const datapump::Crypto& crypto) {
    auto settings = config();
    auto plan = slot();
    const auto offsets = positions(101);
    const std::array<v::Tap, 1> taps{{{0,0}}};
    r::MappedBasisRow row{-6,.1,-.2,-100};
    std::vector<v::Complex> output(2*offsets.size());
    plan.format.pulse_shape = v::PulseShape::Rrc;
    r::MappedBasis fallback(settings, plan, crypto, offsets.size(), 1);
    fallback.render_pair(row, taps, 6000, offsets, output);
    check(output == direct(settings, plan, crypto, row, taps, 6000, offsets),
          "unsupported pulse did not preserve exact native fallback");
    check(fallback.report().direct_rows == 2 && !fallback.report().fallback_reason.empty(),
          "direct fallback was not reported");
    rejects([&] { r::MappedBasis bad(settings, plan, crypto, 0, 1); });
    rejects([&] { r::MappedBasis bad(settings, plan, crypto, offsets.size(), 0); });
    rejects([&] { fallback.render_pair({7,0,0,0}, taps, 0, offsets, output); });
    auto bad_offsets = offsets;
    bad_offsets[1] = bad_offsets[0];
    rejects([&] { fallback.render_pair(row, taps, 0, bad_offsets, output); });
    rejects([&] { fallback.render_pair(row, taps, 0, offsets, std::span(output).first(1)); });
    std::stop_source stopped;
    stopped.request_stop();
    rejects([&] { fallback.render_pair(row, taps, 0, offsets, output, stopped.get_token()); });
    // Cancellation and rejected requests must release the admitted call slot.
    fallback.render_pair(row, taps, 6000, offsets, output);
}
} // namespace

int main() {
    try {
        const auto crypto = key();
        cached_maps(crypto);
        concurrent_pairs(crypto);
        dense_and_gated(crypto);
        fallback_and_validation(crypto);
        std::cout << "native mapped receiver basis passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
