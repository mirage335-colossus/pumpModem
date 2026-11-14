#include "appraisal/hf_scenarios.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <utility>
#include <vector>

namespace appraisal {
namespace {

constexpr double hf_carrier_hz = 10e6;
constexpr double standard_reference_cn0_db_hz = 20.;

Scenario base_hf(std::string id) {
    Scenario scenario;
    scenario.id = std::move(id);
    scenario.family = "skywave-1-30MHz";
    scenario.channel.rf_hz = hf_carrier_hz;
    scenario.channel.paths.clear();
    scenario.waveform.rate_hz = 36.;
    scenario.waveform.requested_bit_seconds = 16.;
    scenario.waveform.sample_rate_hz = 1920.;
    scenario.waveform.added_symbols = 10;
    scenario.waveform.fhss = false;
    scenario.bounds.arrival_low_seconds = 0.;
    scenario.bounds.arrival_high_seconds = .010;
    scenario.bounds.frequency_bound_hz = 0.;
    scenario.bounds.spread_seconds = 0.;
    scenario.bounds.doppler_width_hz = 0.;
    scenario.bounds.varying = true;
    scenario.reference_cn0_db_hz = standard_reference_cn0_db_hz;
    return scenario;
}

Scenario nvis_disturbed() {
    auto scenario = base_hf("hf-nvis-disturbed");
    scenario.channel.paths.resize(2);
    for (auto& path : scenario.channel.paths) {
        path.power = .5;
        path.gaussian_width_2sigma_hz = 1.;
    }
    scenario.channel.paths[1].delay_seconds = .007;
    scenario.bounds.spread_seconds = .007;
    scenario.bounds.doppler_width_hz = 1.;
    scenario.bounds.atmospheric_hz = 3.;
    scenario.evidence = "standard statistical comparison from ITU-R F.1487 Annex 3 midlatitude NVIS disturbed; native finite Gaussian approximation";
    scenario.provenance = "ITU-R F.1487 Annex 3 section 3.4 NVIS; Fable-5.2/83 section 3 and Fable-5.2/33";
    scenario.assumptions =
        "10 MHz design carrier; two independent equal-power zero-mean proper-complex Gaussian skywave taps "
        "at 0 and 7 ms. The selected conditional skywave-only case has no added groundwave. "
        "Each tabulated Doppler power spectrum has Gaussian 2-sigma width D=1 Hz and zero residual centroid; "
        "this width is not an offset or deterministic frequency bound. The receiver's common search allowance "
        "is +/-3 Hz. The finite native Gaussian realization is a statistical comparator, not a field capture, "
        "conformance test, occurrence rate, or universal worst case. Conditional normalized DATA C/N0 is 20 dB-Hz "
        "before fading; no per-record signal-power normalization. The 7 ms delay and 1 Hz width distinguish this "
        "NVIS profile from the 2 ms/1 Hz midlatitude disturbed and 7 ms/30 Hz high-latitude disturbed controls.";
    return scenario;
}

Scenario drm6() {
    auto scenario = base_hf("hf-drm6");
    constexpr double unnormalized_power_sum = 1.31640625;
    constexpr double delay_ms[] = {0., 2., 4., 6.};
    constexpr double rms_gain[] = {.5, 1., .25, .0625};
    constexpr double centroid_hz[] = {0., 1.2, 2.4, 3.6};
    constexpr double width_2sigma_hz[] = {.1, 2.4, 4.8, 7.2};
    scenario.channel.paths.reserve(4);
    for (std::size_t i = 0; i < 4; ++i) {
        Path path;
        path.power = rms_gain[i] * rms_gain[i] / unnormalized_power_sum;
        path.delay_seconds = delay_ms[i] * .001;
        path.residual_doppler_hz = centroid_hz[i];
        path.gaussian_width_2sigma_hz = width_2sigma_hz[i];
        scenario.channel.paths.push_back(path);
    }
    scenario.bounds.spread_seconds = .006;
    scenario.bounds.doppler_width_hz = 7.2;
    // Keep the nonzero source-defined component centroids inside the common
    // receiver search, with the same three-width margin used for the channel
    // family. This is a declared bound, not a measured centroid distribution.
    const double largest_centroid = *std::max_element(
        std::begin(centroid_hz), std::end(centroid_hz),
        [](double left, double right) { return std::abs(left) < std::abs(right); });
    scenario.bounds.atmospheric_hz = largest_centroid + 3. * scenario.bounds.doppler_width_hz;
    scenario.evidence = "standard statistical comparison from ETSI ES 201 980 V4.2.1 Annex B DRM-6 / ITU-R BS.1615-3 Table 14; native finite Gaussian approximation";
    scenario.provenance = "ETSI ES 201 980 V4.2.1 Annex B Table B.1, channel 6; ITU-R BS.1615-3 Table 14; Fable-5.2/83 DRM-6 transcription";
    scenario.assumptions =
        "10 MHz design carrier; four independent zero-mean proper-complex Gaussian taps. Source RMS gains "
        "(.5,1,.25,.0625) are converted to normalized powers by squaring then dividing by 1.31640625; "
        "the powers sum to one without sample-based normalization. Fixed delays are (0,2,4,6) ms; "
        "residual centroids are (0,1.2,2.4,3.6) Hz, distinct from the per-component Gaussian Doppler "
        "power-spectrum 2-sigma widths (.1,2.4,4.8,7.2) Hz. The declared 25.2 Hz receiver common-search "
        "bound covers the largest absolute centroid plus three times the largest 2-sigma width; it is not "
        "a claim that every natural path occupies that support. Conditional normalized DATA C/N0 is "
        "20 dB-Hz before fading; no per-record normalization. This named DRM statistical comparator is "
        "not a field calibration and not a raw sounding replay, occurrence distribution, or conformance test. "
        "The source notes that standard profiles need not be worst possible natural channels.";
    return scenario;
}

} // namespace

std::optional<Scenario> make_additional_hf_scenario(const std::string& id) {
    if (id == "hf-nvis-disturbed") return nvis_disturbed();
    if (id == "hf-drm6") return drm6();
    return std::nullopt;
}

} // namespace appraisal
