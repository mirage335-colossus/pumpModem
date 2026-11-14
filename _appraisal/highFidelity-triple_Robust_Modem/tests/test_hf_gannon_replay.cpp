#include "appraisal/hf_gannon_replay.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace appraisal;
std::size_t checks = 0;
void check(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
void near(double value, double expected, double tolerance, const char* why) {
    check(std::isfinite(value) && std::abs(value - expected) <= tolerance, why);
}
template<class F> void rejects(F function, const char* why) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, why);
}

void fixture_identity_and_joint_scale() {
    const auto a = load_gannon_2024_trace(17);
    const auto b = load_gannon_2024_trace(20);
    const auto& ai = hf_gannon_2024_info(17);
    const auto& bi = hf_gannon_2024_info(20);
    check(ai.utc_first_sample_epoch_seconds == 1715360400 &&
          bi.utc_first_sample_epoch_seconds == 1715371200, "fixed observed UTC hours");
    check(ai.stream_uuid == "0e51e045ca1d42868f3c9cd483112bd7" && ai.stream_uuid == bi.stream_uuid,
          "same nine-channel stream, never mixed with grape19");
    check(ai.zero_based_column == 4 && ai.nominal_center_frequency_hz == 10000000,
          "selected nominal 10 MHz column");
    check(ai.source_member.find("2024-05-10T00-00-00/") != std::string_view::npos &&
          bi.source_member.find("2024-05-10T00-00-00/") != std::string_view::npos,
          "complete member path disambiguates repeated HDF5 basenames");
    check(a->source_sha256 == "8397dc68dcae74222252ddc93162e331a96931d12db9a9e656af031c361dffc5" &&
          b->source_sha256 == "5b9e38b1319cb172d1c6c161b28960e10607d69b951593cd1700df91517173c7",
          "verified raw HDF5 identities attached");
    check(a->serialized_iq_sha256 == "2c56dd525dde8e58e3d78add0e8e9e5c08eddb28473748358aa38c42d30af8d0" &&
          b->serialized_iq_sha256 == "c23ab7e470eaaeb1b501c397ac3552ea04e13cec13a07e497e1c1b41e7cb098b",
          "exact lossless I/Q identities attached");
    check(a->samples.size() == 36000 && b->samples.size() == 36000, "two complete one-hour records");
    near(a->sample_rate_hz, 10, 0, "recorded clock metadata sets knot rate");
    near(b->sample_rate_hz, 10, 0, "same knot rate for both hours");
    near(a->raw_mean_power, 165231037. / 36000, 0, "17 UTC raw energy is retained");
    near(b->raw_mean_power, 2473256894. / 36000, 0, "20 UTC raw energy is retained");
    near(a->normalization_gain, b->normalization_gain, 0, "one common scale for both full records");
    const double gain = a->normalization_gain;
    near(gain * gain * (a->raw_mean_power + b->raw_mean_power) / 2, 1, 4e-16,
         "native joint full-two-hour RMS normalization");
    const auto sa = hf_replay_statistics(*a), sb = hf_replay_statistics(*b);
    near(sb.mean_power / sa.mean_power, 2473256894. / 165231037, 2e-13,
         "relative measured energy survives common normalization");
    check(sa.mean_power < .13 && sb.mean_power > 1.87,
          "neither individual hour is silently normalized to unit power");

    constexpr std::array<std::complex<double>, 5> a_raw{{{0, 50}, {7, 53}, {8, 53}, {16, 52}, {35, -34}}};
    constexpr std::array<std::complex<double>, 5> b_raw{{{-53, -32}, {-36, -4}, {38, 78}, {-29, 168}, {-95, 114}}};
    for (std::size_t i = 0; i < a_raw.size(); ++i) {
        const auto k = i == 4 ? a->samples.size() - 1 : i;
        check(a->samples[k] == a_raw[i] * gain, "raw 17 UTC I/Q order and sign preserved");
        check(b->samples[k] == b_raw[i] * gain, "raw 20 UTC I/Q order and sign preserved");
    }
    std::array<std::size_t, 2> zeros{};
    for (unsigned r = 0; r < 2; ++r) {
        const auto& trace = r == 0 ? *a : *b;
        long double recovered_energy = 0;
        bool integer_samples = true;
        for (auto value : trace.samples) {
            if (value == std::complex<double>{}) ++zeros[r];
            value /= gain;
            const double real = std::round(value.real()), imag = std::round(value.imag());
            integer_samples = integer_samples && std::abs(value.real() - real) < 1e-10 &&
                              std::abs(value.imag() - imag) < 1e-10;
            recovered_energy += static_cast<long double>(real) * real +
                                static_cast<long double>(imag) * imag;
        }
        check(integer_samples, "no sample smoothing, noise subtraction, phase unwrap or detrend");
        check(recovered_energy == (r == 0 ? 165231037.L : 2473256894.L),
              "lossless all-knot integer energy after undoing common scale");
    }
    check(zeros[0] == 4 && zeros[1] == 1, "recorded exact zeros are not repaired");
}

void bounds_and_explicit_ablations() {
    const auto full = load_gannon_2024_trace(17);
    const auto amp = load_gannon_2024_trace(17, HfReplayPart::amplitude_only);
    const auto phase = load_gannon_2024_trace(17, HfReplayPart::phase_only);
    check(full->samples == amp->samples && full->samples == phase->samples,
          "ablations do not mutate stored raw normalized knots");
    near(hf_replay_support_seconds(*full), 3599.9, 0, "last knot excludes the unrecorded final tenth-second");
    check(hf_replay_gain(*full, 3599.9L) == full->samples.back(), "last measured knot is reachable");
    const auto midpoint = hf_replay_gain(*full, .05L);
    check(std::abs(midpoint - (full->samples[0] + full->samples[1]) * .5) < 1e-15,
          "existing Cartesian interpolation, not invented phase evolution");
    near(hf_replay_gain(*amp, .05L).real(), std::abs(midpoint), 1e-15, "amplitude ablation is explicit");
    near(std::abs(hf_replay_gain(*phase, .05L)), 1, 1e-15, "phase ablation is explicit");
    rejects([&] { (void)hf_replay_gain(*full, -.001L); }, "no negative-time extrapolation");
    rejects([&] { (void)hf_replay_gain(*full, 3599.9001L); }, "no unrecorded tail padding");
    rejects([&] { (void)hf_replay_gain(*full, 3600); }, "hour does not wrap");
    rejects([&] { (void)hf_replay_gain(*full, std::numeric_limits<long double>::infinity()); }, "infinite time rejected");
    rejects([] { (void)load_gannon_2024_trace(18); }, "unselected UTC hour rejected");
    rejects([] { (void)load_gannon_2024_trace(17, static_cast<HfReplayPart>(99)); }, "unknown replay part rejected");
}

struct Scratch {
    std::filesystem::path path;
    Scratch() {
        const auto parent = hf_gannon_2024_fixture_directory().parent_path().parent_path() / "local-evidence";
        std::filesystem::create_directories(parent);
        for (unsigned n = 0; n < 100; ++n) {
            path = parent / ("hf-gannon-unit-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(n));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("cannot create isolated Gannon test scratch");
    }
    ~Scratch() { std::error_code error; std::filesystem::remove_all(path, error); }
};

void integrity_and_missing_input() {
    Scratch scratch;
    rejects([&] { (void)load_gannon_2024_trace(17, HfReplayPart::full, scratch.path); }, "missing fixture fails closed");
    for (const unsigned hour : {17, 20}) {
        const auto filename = hf_gannon_2024_info(hour).fixture_filename;
        std::filesystem::copy_file(hf_gannon_2024_fixture_directory() / filename, scratch.path / filename);
    }
    const auto explicit_path = load_gannon_2024_trace(17, HfReplayPart::full, scratch.path);
    check(explicit_path->samples == load_gannon_2024_trace(17)->samples,
          "explicit verified fixture directory reproduces exact samples");
    const auto damaged = scratch.path / hf_gannon_2024_info(20).fixture_filename;
    {
        std::fstream file(damaged, std::ios::binary | std::ios::in | std::ios::out);
        file.put('x');
        check(bool(file), "test corruption was written");
    }
    rejects([&] { (void)load_gannon_2024_trace(17, HfReplayPart::full, scratch.path); },
            "unselected normalization partner is hash-verified too");
    std::filesystem::resize_file(damaged, 575999);
    rejects([&] { (void)load_gannon_2024_trace(20, HfReplayPart::full, scratch.path); }, "truncated record rejected");
    std::filesystem::resize_file(damaged, 576001);
    rejects([&] { (void)load_gannon_2024_trace(20, HfReplayPart::full, scratch.path); }, "extra samples rejected");
}

void measurement_noise_confound() {
    const auto absent = make_hf_measurement_noise_proxy(false);
    const auto present = make_hf_measurement_noise_proxy(true);
    near(absent->normalization_gain, 1, 0, "noise-only control keeps unity scale");
    near(present->normalization_gain, 1, 0, "carrier-plus-noise control keeps the same scale");
    near(hf_replay_statistics(*absent).mean_power, 1, 0, "nonzero record power despite zero true carrier");
    near(hf_replay_statistics(*present).mean_power, 2, 0, "carrier-plus-noise power is not independently normalized");
    const std::complex<double> modem{2, -1};
    const std::complex<double> true_zero_channel{};
    for (const long double t : {0.L, .05L, .1L, .15L, 3599.9L}) {
        const auto proxy_output = modem * hf_replay_gain(*absent, t);
        check(std::norm(proxy_output) > 0, "measurement-noise-only replay can manufacture nonzero modem response");
        check(modem * true_zero_channel == std::complex<double>{}, "known true zero channel cannot pass modem energy");
        check(std::abs(hf_replay_gain(*present, t) - hf_replay_gain(*absent, t) -
                       std::complex<double>{1, 0}) < 1e-14, "matched controls differ only by the true constant carrier");
        check(2. * proxy_output == (2. * modem) * hf_replay_gain(*absent, t),
              "recorded measurement noise becomes signal-proportional under replay");
    }
    near(std::norm(hf_replay_gain(*present, .2L)), 0, 0,
         "noise/carrier cancellation is retained instead of repaired");
    rejects([&] { (void)hf_replay_gain(*absent, 3600); }, "synthetic control uses identical bounded operator");
}
}

int main() {
    try {
        fixture_identity_and_joint_scale();
        bounds_and_explicit_ablations();
        integrity_and_missing_input();
        measurement_noise_confound();
        std::cout << checks << " Gannon observation-conditioned replay checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
