#pragma once

// Optional Helper-only waveform impairment for standalone matched simulations.
// This file is channel/evaluator code; none of its profile or truth values is
// passed to the receiver. Apply it to an otherwise ordinary generated dwell.
// The reconstruction assumes the Generator's original composite spectrum.
// Do not combine this wrapper with independently modified low-pilot spectra
// or a noise-only input unless that reconstruction is extended accordingly.
// The same physical transient affects Helper and probe, while input thermal
// noise is not rotated, attenuated, regenerated or resampled.
#include "helper_live_probe_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <string_view>

namespace helper_transient {
namespace hp = helper_live_probe;
namespace h = datapump::helper;

struct Profile {
    unsigned dwell = 100;             // 100 disables the optional impairment
    double onset_seconds = 0;         // receiver-time offset within this dwell
    double duration_seconds = 0;
    double gain = 1;                  // signal gain during the transient only
    double phase_radians = 0;         // signal phase pulse during the transient
    double frequency_hz = 0;          // additional frequency while active
    double decay_seconds = 0;         // zero: constant; positive: exponential
    bool additive_high = false;      // independent coherent high-pilot component
};

// Fixed controls, chosen before observing any estimator output. The phase and
// gain discontinuities deliberately stress the nominal settled aperture; the
// frequency cases integrate phase continuously and retain the accumulated
// phase through the remainder of the dwell. Every subsequent hop has its own
// ordinary random RF phase and does not inherit this test-only added phase.
inline Profile named(std::string_view name, unsigned dwell) {
    if (name == "none") return {};
    if (name == "phase48") return {dwell,.100,.048,1.,std::numbers::pi/2,0.,0.};
    // Retains the old taper study's different 48 ms counterexample: an added
    // quadrature high-pilot component beginning at the nominal settled edge.
    if (name == "high-add48") return {dwell,.020,.048,1.,std::numbers::pi/2,0.,0.,true};
    if (name == "phase15") return {dwell,.075,.015,1.,-.85,0.,0.};
    if (name == "phase80") return {dwell,.190,.080,.65,1.2,0.,0.};
    if (name == "gain80") return {dwell,.065,.080,.35,0.,0.,0.};
    if (name == "frequency60") return {dwell,.150,.060,1.,0.,4.,0.};
    if (name == "frequency-decay") return {dwell,.028,.220,1.,0.,-5.,.040};
    // Reserved evaluation shapes: these differ from the tuning profiles in
    // onset, length and/or sign. Do not use them for candidate selection.
    if (name == "heldout-add23") return {dwell,.147,.023,.8,-1.1,0.,0.,true};
    if (name == "heldout-phase37") return {dwell,.225,.037,.8,-.7,0.,0.};
    if (name == "heldout-phase65") return {dwell,.055,.065,1.1,1.1,0.,0.};
    if (name == "heldout-gain55") return {dwell,.150,.055,.5,0.,0.,0.};
    if (name == "heldout-frequency85") return {dwell,.103,.085,1.,0.,2.5,0.};
    if (name == "heldout-decay") return {dwell,.061,.190,1.,0.,-3.,.055};
    // V4 held-out combinations, reserved before estimator tuning. These are
    // evaluator/channel settings and must never select receiver corrections.
    if (name == "v4held-add31") return {dwell,.182,.031,1.2,.9,0.,0.,true};
    if (name == "v4held-phase12") return {dwell,.112,.012,.9,1.,0.,0.};
    if (name == "v4held-phase53") return {dwell,.142,.053,.75,-1.15,0.,0.};
    if (name == "v4held-gain73") return {dwell,.242,.073,.42,0.,0.,0.};
    if (name == "v4held-frequency70") return {dwell,.060,.070,1.,0.,-3.5,0.};
    if (name == "v4held-decay130") return {dwell,.160,.130,1.,0.,3.5,.025};
    // V5 held-out combinations, fixed before receiver-candidate tuning.
    if (name == "v5held-add41") return {dwell,.231,.041,.65,-1.25,0.,0.,true};
    if (name == "v5held-add17") return {dwell,.054,.017,1.3,1.7,0.,0.,true};
    if (name == "v5held-phase31") return {dwell,.126,.031,.88,-1.1,0.,0.};
    if (name == "v5held-phase68") return {dwell,.203,.068,.76,.95,0.,0.};
    if (name == "v5held-gain47") return {dwell,.091,.047,.48,0.,0.,0.};
    if (name == "v5held-frequency80") return {dwell,.188,.080,1.,0.,-2.7,0.};
    if (name == "v5held-decay175") return {dwell,.045,.175,1.,0.,4.5,.032};
    // V6 holdouts, fixed before receiver-candidate tuning.  These profiles
    // deliberately vary onset, duration, polarity and disturbance type.
    if (name == "v6held-phase22") return {dwell,.165,.022,1.15,.75,0.,0.};
    if (name == "v6held-phase59") return {dwell,.082,.059,.62,-1.35,0.,0.};
    if (name == "v6held-gain64") return {dwell,.213,.064,.31,0.,0.,0.};
    if (name == "v6held-add29") return {dwell,.105,.029,.95,1.4,0.,0.,true};
    if (name == "v6held-add56") return {dwell,.265,.056,1.1,-.8,0.,0.,true};
    if (name == "v6held-frequency44") return {dwell,.122,.044,1.,0.,6.,0.};
    if (name == "v6held-decay145") return {dwell,.093,.145,1.,0.,-4.25,.047};
    if (name == "v6held-combined37") return {dwell,.284,.037,.57,.65,1.3,0.};
    throw std::invalid_argument("unknown Helper transient profile");
}

class Model {
public:
    Model(const hp::Config& config, Profile profile)
        : profile_(profile), generator_(generator(config)) {
        if (profile.dwell > 100 || !std::isfinite(profile.onset_seconds) ||
            !std::isfinite(profile.duration_seconds) || !std::isfinite(profile.gain) ||
            !std::isfinite(profile.phase_radians) || !std::isfinite(profile.frequency_hz) ||
            !std::isfinite(profile.decay_seconds) || profile.onset_seconds < 0 ||
            profile.duration_seconds < 0 || profile.onset_seconds+profile.duration_seconds > hp::dwell_seconds ||
            profile.gain <= 0 || profile.gain > 2 || profile.decay_seconds < 0)
            throw std::invalid_argument("invalid Helper transient profile");
    }
    bool applies(unsigned dwell) const noexcept { return dwell==profile_.dwell; }

    // Evaluator-only instantaneous additive oscillator frequency on the
    // receiver time axis. Apertures crossing phase/gain discontinuities have
    // no single frequency truth described by this function.
    double frequency_hz(unsigned dwell, double local_seconds) const noexcept {
        if (dwell != profile_.dwell || local_seconds < profile_.onset_seconds ||
            local_seconds >= profile_.onset_seconds+profile_.duration_seconds) return 0;
        const double elapsed=local_seconds-profile_.onset_seconds;
        return profile_.frequency_hz * (profile_.decay_seconds > 0
            ? std::exp(-elapsed/profile_.decay_seconds) : 1.);
    }

    // Evaluator-only physical disturbance mask. An integrated frequency
    // excursion may retain a phase offset after its active frequency pulse.
    bool disturbed(unsigned dwell, double local_seconds) const noexcept {
        if (dwell != profile_.dwell || local_seconds < profile_.onset_seconds) return false;
        if (profile_.additive_high)
            return local_seconds < profile_.onset_seconds+profile_.duration_seconds;
        return std::abs(factor(dwell,local_seconds)-std::complex<double>{1.,0.}) > 1e-12;
    }

    // Conservative evaluator-only intersection with a closed time interval.
    // Frequency excursions retain their integrated phase after the pulse.
    bool disturbed_during(unsigned dwell,double first,double last) const noexcept {
        if(dwell!=profile_.dwell||last<first||profile_.duration_seconds<=0)return false;
        const double end=profile_.frequency_hz!=0&&!profile_.additive_high
            ? hp::dwell_seconds : profile_.onset_seconds+profile_.duration_seconds;
        const bool effect=profile_.additive_high||profile_.gain!=1||profile_.phase_radians!=0||profile_.frequency_hz!=0;
        return effect&&first<end&&last>=profile_.onset_seconds;
    }

    // Time-local signal gain and oscillator phase; no receiver observes this.
    std::complex<double> factor(unsigned dwell, double local_seconds) const noexcept {
        if (profile_.additive_high) return {1.,0.};
        if (dwell != profile_.dwell || local_seconds < profile_.onset_seconds) return {1.,0.};
        const double elapsed=local_seconds-profile_.onset_seconds;
        const bool active=elapsed < profile_.duration_seconds;
        const double integrated=std::min(elapsed,profile_.duration_seconds);
        const double cycles=profile_.frequency_hz*(profile_.decay_seconds > 0
            ? profile_.decay_seconds*(-std::expm1(-integrated/profile_.decay_seconds)) : integrated);
        const double phase=hp::tau*cycles+(active?profile_.phase_radians:0.);
        return std::polar(active?profile_.gain:1.,phase);
    }

    void apply(hp::Dwell& d, const hp::Source& source, unsigned dwell) const {
        if (dwell != profile_.dwell) return;
        if (d.samples.size()!=hp::dwell_samples || d.clean_probe.size()!=d.samples.size() ||
            d.common_phasor.size()!=d.samples.size())
            throw std::invalid_argument("invalid transient dwell arrays");
        // This nuisance-tone form deliberately reproduces the prior
        // taper-feasibility counterexample, whose frequency and phase are
        // defined at the midpoint of the guarded 360 ms aperture. Simulator
        // truth is used solely to synthesize this controlled impairment.
        constexpr std::size_t guard=480, aperture=8640;
        const double center_sample=static_cast<double>(guard)+(static_cast<double>(aperture)-1.)/2.;
        const double center=d.first_seconds+center_sample/hp::sample_rate;
        double high_frequency=0,high_phase=0;
        if (profile_.additive_high) {
            const auto k=guard+aperture/2;
            const auto phasor=(std::complex<double>(d.common_phasor[k-1])+std::complex<double>(d.common_phasor[k]))*.5;
            high_frequency=2400*source.audio_scale_at(center)+source.shift_error_hz(d.shift_hz,center);
            high_phase=std::remainder(std::arg(phasor)+hp::tau*std::remainder(2400*source.tx_time(center),1.),hp::tau);
        }
        for (std::size_t i=0;i<d.samples.size();++i) {
            const double local=static_cast<double>(i)/hp::sample_rate;
            if (profile_.additive_high) {
                if (local < profile_.onset_seconds || local >= profile_.onset_seconds+profile_.duration_seconds) continue;
                const double t=(static_cast<double>(i)-center_sample)/hp::sample_rate;
                const std::complex<float> added(std::polar(profile_.gain,hp::tau*high_frequency*t+high_phase+profile_.phase_radians));
                d.samples[i]+=added;
                continue; // the probe and common reference are unchanged
            }
            const auto correction=factor(dwell,local);
            if (correction==std::complex<double>{1.,0.}) continue;
            const double nominal=d.first_seconds+local;
            const double tx=source.tx_time(nominal);
            const std::complex<double> common(d.common_phasor[i]);
            const auto helper=generator_.mix_analytic({},tx,hp::sample_rate,40.L,0.L)/
                (.4*h::pilot_amplitude_fraction);
            const std::complex<double> probe(d.clean_probe[i]);
            // Add only the change to the physical signal; existing thermal
            // noise retains its identical realization up to float rounding.
            const auto received=std::complex<double>(d.samples[i])+(correction-std::complex<double>{1.,0.})*(helper*common+probe);
            const auto adjusted_probe=probe*correction;
            const auto adjusted_common=common*correction;
            d.samples[i]={static_cast<float>(received.real()),static_cast<float>(received.imag())};
            d.clean_probe[i]={static_cast<float>(adjusted_probe.real()),static_cast<float>(adjusted_probe.imag())};
            d.common_phasor[i]={static_cast<float>(adjusted_common.real()),static_cast<float>(adjusted_common.imag())};
        }
    }

private:
    static h::Generator generator(const hp::Config& c) {
        h::Options options;
        options.power_w=c.helper_w;options.data_power_w=1;options.callsign=c.callsign;
        return h::Generator(options,h::Plan{2400,.4,0,c.helper_w,950,550,c.hopping},c.epoch,true);
    }
    Profile profile_;
    h::Generator generator_;
};
} // namespace helper_transient
