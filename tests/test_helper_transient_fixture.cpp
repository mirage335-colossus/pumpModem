#include "helper_transient_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>

namespace {
namespace hp=helper_live_probe;
namespace ht=helper_transient;
using C=std::complex<double>;

void require(bool condition,const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

void run(hp::Probe probe) {
    hp::Config config;config.seed=17;config.probe=probe;
    auto clean_config=config;clean_config.noise=false;
    hp::Source noisy(config),noise_free(clean_config);
    hp::Dwell original,quiet;
    // Use the physical 38-second probe interval, including the actual 40 W
    // shaped APSK or tone, keyed hops and independent receiver thermal noise.
    for(unsigned dwell=0;dwell<=95;++dwell){original=noisy.generate(dwell);quiet=noise_free.generate(dwell);}
    constexpr std::size_t guard=480,aperture=8640;
    const double center_sample=static_cast<double>(guard)+(static_cast<double>(aperture)-1.)/2.;
    const double center=quiet.first_seconds+center_sample/hp::sample_rate;
    const auto center_phasor=(C(quiet.common_phasor[guard+aperture/2-1])+C(quiet.common_phasor[guard+aperture/2]))*.5;
    const double high_frequency=2400*noise_free.audio_scale_at(center)+noise_free.shift_error_hz(quiet.shift_hz,center);
    const double high_phase=std::remainder(std::arg(center_phasor)+hp::tau*std::remainder(2400*noise_free.tx_time(center),1.),hp::tau);
    for (auto name : {"phase48","high-add48","phase15","phase80","gain80","frequency60","frequency-decay",
                      "heldout-add23","heldout-phase37","heldout-phase65","heldout-gain55","heldout-frequency85","heldout-decay",
                      "v4held-add31","v4held-phase12","v4held-phase53","v4held-gain73","v4held-frequency70","v4held-decay130",
                      "v5held-add41","v5held-add17","v5held-phase31","v5held-phase68","v5held-gain47","v5held-frequency80","v5held-decay175",
                      "v6held-phase22","v6held-phase59","v6held-gain64","v6held-add29","v6held-add56","v6held-frequency44","v6held-decay145","v6held-combined37",
                      "v7held-start-phase","v7held-end-phase","v7held-start-add","v7held-end-add",
                      "v7held-start-gain","v7held-end-gain","v7held-start-freq","v7held-end-freq",
                      "v8held-start-combined","v8held-end-phase","v8held-interior-decay","v8held-interior-add",
                      "v9held-start-combined","v9held-end-settling","v9held-interior-phase","v9held-end-add",
                      "inversion-held-phase23","inversion-held-gain51","inversion-held-settle61"}) {
        const auto profile=ht::named(name,95);
        ht::Model model(config,profile);
        auto disturbed=original,clean_twin=quiet;
        model.apply(disturbed,noisy,95);model.apply(clean_twin,noise_free,95);
        double noise_error=0,signal_change=0,probe_error=0,common_error=0,reconstruction_error=0;
        for(std::size_t i=0;i<disturbed.samples.size();++i){
            const double t=static_cast<double>(i)/hp::sample_rate;
            const auto factor=model.factor(95,t);
            const C before_noise=C(original.samples[i])-C(quiet.samples[i]);
            const C after_noise=C(disturbed.samples[i])-C(clean_twin.samples[i]);
            noise_error=std::max(noise_error,std::abs(before_noise-after_noise));
            signal_change=std::max(signal_change,std::abs(C(disturbed.samples[i])-C(original.samples[i])));
            probe_error=std::max(probe_error,std::abs(C(disturbed.clean_probe[i])-factor*C(original.clean_probe[i])));
            common_error=std::max(common_error,std::abs(C(disturbed.common_phasor[i])-factor*C(original.common_phasor[i])));
            C expected=factor*C(quiet.samples[i]);
            if(std::string_view(name)=="high-add48"&&t>=.020&&t<.068){
                const double elapsed=(static_cast<double>(i)-center_sample)/hp::sample_rate;
                expected+=std::polar(1.,hp::tau*high_frequency*elapsed+high_phase+std::numbers::pi/2);
            }
            if(std::string_view(name)=="heldout-add23"&&t>=.147&&t<.170){
                const double elapsed=(static_cast<double>(i)-center_sample)/hp::sample_rate;
                expected+=std::polar(.8,hp::tau*high_frequency*elapsed+high_phase-1.1);
            }
            if(std::string_view(name)=="v4held-add31"&&t>=.182&&t<.213){
                const double elapsed=(static_cast<double>(i)-center_sample)/hp::sample_rate;
                expected+=std::polar(1.2,hp::tau*high_frequency*elapsed+high_phase+.9);
            }
            if(std::string_view(name)=="v5held-add41"&&t>=.231&&t<.231+.041){
                const double elapsed=(static_cast<double>(i)-center_sample)/hp::sample_rate;
                expected+=std::polar(.65,hp::tau*high_frequency*elapsed+high_phase-1.25);
            }
            if(std::string_view(name)=="v5held-add17"&&t>=.054&&t<.054+.017){
                const double elapsed=(static_cast<double>(i)-center_sample)/hp::sample_rate;
                expected+=std::polar(1.3,hp::tau*high_frequency*elapsed+high_phase+1.7);
            }
            if(std::string_view(name)=="v6held-add29"&&t>=.105&&t<.105+.029){
                const double elapsed=(static_cast<double>(i)-center_sample)/hp::sample_rate;
                expected+=std::polar(.95,hp::tau*high_frequency*elapsed+high_phase+1.4);
            }
            if(std::string_view(name)=="v6held-add56"&&t>=.265&&t<.265+.056){
                const double elapsed=(static_cast<double>(i)-center_sample)/hp::sample_rate;
                expected+=std::polar(1.1,hp::tau*high_frequency*elapsed+high_phase-.8);
            }
            if((std::string_view(name).starts_with("v7held-")||std::string_view(name).starts_with("v8held-")||
                std::string_view(name).starts_with("v9held-"))&&profile.additive_high&&
               t>=profile.onset_seconds&&t<profile.onset_seconds+profile.duration_seconds){
                const double elapsed=(static_cast<double>(i)-center_sample)/hp::sample_rate;
                expected+=std::polar(profile.gain,hp::tau*high_frequency*elapsed+high_phase+profile.phase_radians);
            }
            reconstruction_error=std::max(reconstruction_error,std::abs(C(clean_twin.samples[i])-expected));
        }
        // The original noise is not transformed. Float32 storage accounts for
        // the small rounding difference when adding an impaired signal.
        require(noise_error<5e-6&&signal_change>.1,"signal/noise preservation failed");
        require(probe_error<2e-6&&common_error<2e-7,"evaluator common or probe truth incorrect");
        require(reconstruction_error<2e-6,"noise-free signal reconstruction incorrect");
        require(model.factor(94,.2)==C(1,0),"transient escaped its selected dwell");
        std::cout<<"helper_transient_fixture profile="<<name<<" probe="<<(probe==hp::Probe::tone?"tone":"apsk")
                 <<" max_noise_rounding="<<noise_error<<" max_signal_change="<<signal_change
                 <<" probe_error="<<probe_error<<" common_error="<<common_error<<" reconstruction_error="<<reconstruction_error<<'\n';
    }
    const ht::Model frequency(config,ht::named("frequency60",95));
    const ht::Model decay(config,ht::named("frequency-decay",95));
    for(const auto* model : {&frequency,&decay}) for(double t : {.16,.19,.205}) {
        constexpr double step=1e-7;
        const auto ratio=model->factor(95,t+step)*std::conj(model->factor(95,t-step));
        const double slope=std::arg(ratio)/(2*step*hp::tau);
        require(std::abs(slope-model->frequency_hz(95,t))<1e-6,"frequency truth does not match integrated phase");
    }
    require(frequency.frequency_hz(95,.149)==0&&frequency.frequency_hz(95,.211)==0,
        "frequency truth escaped the declared transient");
    require(std::abs(frequency.factor(95,.2100001)-frequency.factor(95,.2099999))<1e-5,
        "frequency pulse did not retain phase continuously after settling");
    const ht::Model phase(config,ht::named("phase48",95));
    const ht::Model additive(config,ht::named("high-add48",95));
    require(!phase.disturbed(95,.099)&&phase.disturbed(95,.11)&&!phase.disturbed(95,.149)&&
            additive.disturbed(95,.04)&&!additive.disturbed(95,.07)&&!additive.disturbed(94,.04),
        "evaluator transient mask does not match physical pulse support");
    require(frequency.disturbed(95,.22)&&!frequency.disturbed(95,.14),
        "evaluator mask lost retained phase after frequency settling");
    require(phase.disturbed_during(95,.098,.102)&&phase.disturbed_during(95,.147,.150)&&
            !phase.disturbed_during(95,.090,.099)&&!phase.disturbed_during(94,.098,.102)&&
            frequency.disturbed_during(95,.25,.26),
        "evaluator interval mask failed pulse edge or retained-phase coverage");
    // Lock the predeclared V8 parameters and check distinct physical effects;
    // these profiles were reserved before evaluation or tuning against them.
    const auto start_profile=ht::named("v8held-start-combined",95);
    const auto end_profile=ht::named("v8held-end-phase",95);
    const auto decay_profile=ht::named("v8held-interior-decay",95);
    const auto add_profile=ht::named("v8held-interior-add",95);
    require(start_profile.onset_seconds==.0045&&start_profile.duration_seconds==.032&&
            start_profile.gain==.58&&start_profile.phase_radians==-.67&&
            start_profile.frequency_hz==2.2&&start_profile.decay_seconds==.018&&!start_profile.additive_high&&
            end_profile.onset_seconds==.361&&end_profile.duration_seconds==.034&&
            end_profile.gain==1.22&&end_profile.phase_radians==1.05&&end_profile.frequency_hz==0&&
            decay_profile.onset_seconds==.173&&decay_profile.duration_seconds==.087&&
            decay_profile.frequency_hz==-3.7&&decay_profile.decay_seconds==.036&&
            add_profile.onset_seconds==.287&&add_profile.duration_seconds==.049&&
            add_profile.gain==.73&&add_profile.phase_radians==-1.55&&add_profile.additive_high,
            "reserved V8 transient parameters changed");
    const ht::Model v8start(config,start_profile),v8end(config,end_profile),
                    v8decay(config,decay_profile),v8add(config,add_profile);
    require(!v8start.disturbed(95,.004)&&v8start.disturbed(95,.005)&&v8start.disturbed(95,.1)&&
            !v8end.disturbed(95,.360)&&v8end.disturbed(95,.37)&&!v8end.disturbed(95,.396)&&
            !v8decay.disturbed(95,.172)&&v8decay.disturbed(95,.2)&&v8decay.disturbed(95,.3)&&
            !v8add.disturbed(95,.286)&&v8add.disturbed(95,.3)&&!v8add.disturbed(95,.337),
            "reserved V8 transient timing or retained-phase mask failed");
    const auto v9start_profile=ht::named("v9held-start-combined",95);
    const auto v9settle_profile=ht::named("v9held-end-settling",95);
    const auto v9phase_profile=ht::named("v9held-interior-phase",95);
    const auto v9add_profile=ht::named("v9held-end-add",95);
    require(v9start_profile.onset_seconds==.011&&v9start_profile.duration_seconds==.028&&
            v9start_profile.gain==.61&&v9start_profile.phase_radians==1.08&&
            v9start_profile.frequency_hz==0&&!v9start_profile.additive_high&&
            v9settle_profile.onset_seconds==.343&&v9settle_profile.duration_seconds==.054&&
            v9settle_profile.gain==1&&v9settle_profile.frequency_hz==-5.2&&v9settle_profile.decay_seconds==.021&&
            v9phase_profile.onset_seconds==.153&&v9phase_profile.duration_seconds==.019&&
            v9phase_profile.gain==.77&&v9phase_profile.phase_radians==-1.23&&
            v9add_profile.onset_seconds==.356&&v9add_profile.duration_seconds==.037&&
            v9add_profile.gain==1.12&&v9add_profile.phase_radians==1.44&&v9add_profile.additive_high,
            "reserved V9 transient parameters changed");
    const ht::Model v9start(config,v9start_profile),v9settle(config,v9settle_profile),
                    v9phase(config,v9phase_profile),v9add(config,v9add_profile);
    require(!v9start.disturbed(95,.010)&&v9start.disturbed(95,.02)&&!v9start.disturbed(95,.040)&&
            !v9settle.disturbed(95,.342)&&v9settle.disturbed(95,.35)&&v9settle.disturbed(95,.399)&&
            !v9phase.disturbed(95,.152)&&v9phase.disturbed(95,.16)&&!v9phase.disturbed(95,.173)&&
            !v9add.disturbed(95,.355)&&v9add.disturbed(95,.37)&&!v9add.disturbed(95,.394),
            "reserved V9 transient timing or retained-phase mask failed");
    const auto inversion_phase_profile=ht::named("inversion-held-phase23",95);
    const auto inversion_gain_profile=ht::named("inversion-held-gain51",95);
    const auto inversion_settle_profile=ht::named("inversion-held-settle61",95);
    require(inversion_phase_profile.onset_seconds==.1465&&inversion_phase_profile.duration_seconds==.023&&
            inversion_phase_profile.gain==.79&&inversion_phase_profile.phase_radians==.93&&
            inversion_gain_profile.onset_seconds==.131&&inversion_gain_profile.duration_seconds==.051&&
            inversion_gain_profile.gain==.43&&inversion_gain_profile.phase_radians==0&&
            inversion_settle_profile.onset_seconds==.117&&inversion_settle_profile.duration_seconds==.061&&
            inversion_settle_profile.gain==1&&inversion_settle_profile.frequency_hz==3.8&&
            inversion_settle_profile.decay_seconds==.014,
            "reserved inversion transient parameters changed");
    const ht::Model inversion_phase(config,inversion_phase_profile),
                    inversion_gain(config,inversion_gain_profile),
                    inversion_settle(config,inversion_settle_profile);
    require(!inversion_phase.disturbed(95,.146)&&inversion_phase.disturbed(95,.15)&&
            !inversion_phase.disturbed(95,.170)&&!inversion_gain.disturbed(95,.130)&&
            inversion_gain.disturbed(95,.15)&&!inversion_gain.disturbed(95,.183)&&
            !inversion_settle.disturbed(95,.116)&&inversion_settle.disturbed(95,.13)&&
            inversion_settle.disturbed(95,.2),
            "reserved inversion transient timing or retained-phase mask failed");
    const ht::Model identity(config,ht::named("none",95));
    auto unmodified=original;identity.apply(unmodified,noisy,95);
    require(unmodified.samples==original.samples&&unmodified.clean_probe==original.clean_probe&&
        unmodified.common_phasor==original.common_phasor,"disabled profile changed samples");
    bool rejected=false;
    try { ht::Model bad(config,ht::Profile{95,.39,.04,1.,0.,0.,0.}); }
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"out-of-dwell transient accepted");
}
}

int main() try {
    run(hp::Probe::tone);run(hp::Probe::apsk64);
    std::cout<<"helper_transient_fixture passed=1\n";return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
