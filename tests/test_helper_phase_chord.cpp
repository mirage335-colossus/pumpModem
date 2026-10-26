#include "../src/helper_phase_chord.hpp"
#include "datapump/pattern_receiver.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace h=datapump::helper;
namespace {
constexpr long double tau=2*std::numbers::pi_v<long double>;
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
h::Reference endpoint(std::uint64_t sample,std::uint64_t origin=0,double error=0,
        double frequency=17.25,double phase_uncertainty=.02,double frequency_uncertainty=1) {
    h::Reference r;
    r.sample=sample;r.sample_rate=1000;r.frequency_sample=sample;
    r.phase_radians=static_cast<double>(std::remainder(.37L+tau*frequency*(sample-origin)/1000+error,tau));
    r.frequency_hz=frequency;r.phase_uncertainty_radians=phase_uncertainty;
    r.frequency_uncertainty_hz=frequency_uncertainty;
    r.frequency_valid=r.phase_valid=r.timing_valid=r.phase_observation_known=true;
    r.phase_observation_sample=sample;r.dwell_start_seconds=.16;r.dwell_uncertainty_seconds=.0001;
    return r;
}
h::PhaseChord seeded(std::uint64_t origin=0,double sigma=.008770170950223484) {
    h::PhaseChord chord;check(chord.configure(1000,sigma,17.25),"valid sample clock was declined");
    for(std::uint64_t delta=0;delta<=700;delta+=100) {
        (void)chord.observe(endpoint(origin+delta,origin),origin+delta);
        if(delta<400)check(!chord.prediction(),"phase chord qualified without a measured aperture");
    }
    return chord;
}
void fresh_dates_and_bounded_storage() {
    static_assert(sizeof(h::PhaseChord)<=384,"optional estimator exceeds its declared storage cap");
    for(const auto origin:std::array<std::uint64_t,2>{0,UINT64_MAX-1000}) {
        auto chord=seeded(origin);const auto p=chord.prediction();
        check(p&&p->first_sample==origin&&p->last_sample==origin+700,
            "fresh aperture lost its original sample dates");
        check(std::abs(p->frequency_hz-17.25)<1e-10,"carrier cycles corrupted endpoint slope");
        check(std::isinf(p->phase_at(p->last_sample-1))&&std::isinf(p->uncertainty_at(p->last_sample-1)),
            "past date wrapped into a future prediction");
        const auto future_phase=endpoint(origin+900,origin).phase_radians;
        check(std::abs(std::remainder(p->phase_at(origin+900)-future_phase,tau))<1e-9L,
            "large absolute sample dates erased a small future phase interval");
        check(p->uncertainty_at(p->last_sample+300)<.25,
            "endpoint prediction reused the one-Hz local fit interval as slope error");
        check(p->uncertainty_at(p->last_sample+600)>p->uncertainty_at(p->last_sample+300),
            "future phase allowance did not increase with forecast horizon");
        auto invalid=endpoint(origin+800,origin);invalid.phase_observation_sample=origin+700;
        (void)chord.observe(invalid,origin+800);
        check(chord.prediction()->last_sample==p->last_sample,"carried phase manufactured a new endpoint");
        invalid=endpoint(origin+800,origin);invalid.frequency_sample=origin+801;
        (void)chord.observe(invalid,origin+800);
        check(chord.prediction()->last_sample==p->last_sample,"future frequency evidence entered the chord");
        invalid=endpoint(origin+800,origin);(void)chord.observe(invalid,origin+799);
        check(chord.prediction()->last_sample==p->last_sample,"future capture evidence entered the chord");
    }
}
void endpoint_error_and_process_bound() {
    constexpr double frequency=123.125,epsilon=.02,sigma=.01;
    for(const auto diffusion:std::array<double,2>{0,sigma}) {
        h::PhaseChord chord;check(chord.configure(1000,diffusion,frequency),"configuration failed");
        (void)chord.observe(endpoint(0,0,epsilon,frequency,epsilon,.1),0);
        (void)chord.observe(endpoint(400,0,-epsilon,frequency,epsilon,.1),400);
        const auto p=chord.prediction();check(bool(p),"two independent endpoints failed to form a chord");
        const auto at=std::uint64_t{600};
        const auto actual=.37L+tau*frequency*.6L;
        const auto error=std::abs(std::remainder(p->phase_at(at)-actual,tau));
        check(error<=p->uncertainty_at(at),"opposite endpoint errors escaped the complete allowance");
        const auto L=.4L,horizon=.2L;
        const auto endpoint_allowance=epsilon+2*epsilon*horizon/L;
        const auto past=diffusion*h::phase_chord_detail::pair_standard_deviations(0,400)*horizon/std::sqrt(L);
        const auto future=diffusion*h::phase_chord_detail::pair_standard_deviations(400,600)*std::sqrt(horizon);
        check(p->uncertainty_at(at)>=endpoint_allowance+past+future,
            "prediction omitted an endpoint or independent Wiener-increment allowance");
        check(p->uncertainty_at(at)<endpoint_allowance+past+future+1e-10L,
            "local frequency interval was counted again after a unique cycle unwrap");
    }
}
void ambiguity_and_explicit_reset() {
    h::PhaseChord chord;check(chord.configure(1000,0,17.25),"configuration failed");
    (void)chord.observe(endpoint(0),0);(void)chord.observe(endpoint(1000),1000);
    check(!chord.prediction(),"wide prior guessed a carrier cycle over an ambiguous gap");
    for(std::uint64_t sample=1100;sample<=1500;sample+=100)
        (void)chord.observe(endpoint(sample,0,0,17.25,.02,.001),sample);
    check(bool(chord.prediction()),"fresh short increments could not recover optional estimation");
    check(!chord.configure(2000,0,17.25),"sample clock silently changed within a capture generation");
    check(chord.reset(1000,0,17.25)&&!chord.prediction(),"explicit new generation retained old anchors");
    (void)chord.observe(endpoint(0),0);(void)chord.observe(endpoint(400,0,0,17.25,.02,.001),400);
    // The first endpoint's one-Hz prior still spans less than half a cycle at400ms.
    check(bool(chord.prediction()),"sample-zero endpoint failed after explicit generation reset");
    check(!chord.reset(0,0,17.25)&&!chord.prediction(),"invalid reset preserved old phase authority");
}
void invalid_authority() {
    auto chord=seeded();const auto old=chord.prediction();
    for(unsigned kind=0;kind<12;++kind) {
        auto r=endpoint(800);
        switch(kind) {
        case 0:r.phase_valid=false;break;
        case 1:r.timing_valid=false;break;
        case 2:r.phase_observation_known=false;break;
        case 3:r.sample_rate=2000;break;
        case 4:r.phase_uncertainty_radians=-1;break;
        case 5:r.frequency_uncertainty_hz=std::numeric_limits<double>::infinity();break;
        case 6:r.phase_radians=std::numeric_limits<double>::quiet_NaN();break;
        case 7:r.phase_uncertainty_radians=.25;break;
        case 8:r.dwell_uncertainty_seconds=.00501;break;
        case 9:r.dwell_uncertainty_seconds=-1;break;
        case 10:r.dwell_uncertainty_seconds=std::numeric_limits<double>::quiet_NaN();break;
        case 11:r.dwell_start_seconds=std::numeric_limits<double>::infinity();break;
        }
        (void)chord.observe(r,800);
        check(chord.prediction()->last_sample==old->last_sample,"invalid authority mutated endpoint history");
    }
    const h::PhaseChord::Prediction empty;
    check(std::isinf(empty.phase_at(0))&&std::isinf(empty.uncertainty_at(0)),
        "empty predictor claimed a phase certificate");
}
void mean_frequency_interval() {
    struct OriginalPredictionLayout {
        std::uint64_t first_sample=0,last_sample=0;
        double phase=0,frequency_hz=0,past_frequency_error=0;
        double first_uncertainty=0,last_uncertainty=0,sigma=0;
        std::uint32_t rate=0;
    };
    static_assert(sizeof(h::PhaseChord::Prediction)<=sizeof(OriginalPredictionLayout)+sizeof(double),
        "mean query added more than one temporary rounding scalar");
    static_assert(sizeof(h::PhaseChord)<=384,"mean query enlarged retained estimator control");
    check(!h::PhaseChord::Prediction{}.mean_frequency_interval(),
        "default predictor manufactured qualified endpoint authority");
    for(const auto origin:std::array<std::uint64_t,2>{0,UINT64_MAX-1000}) {
        const auto chord=seeded(origin);
        const auto p=chord.prediction();const auto interval=p->mean_frequency_interval();
        check(bool(interval),"actual qualified endpoints did not yield a mean interval");
        check(interval->first_sample==origin && interval->last_sample==origin+700 && interval->sample_rate==1000,
            "mean interval lost actual dated aperture");
        check(interval->lower_hz<=17.25 && interval->upper_hz>=17.25,
            "actual constant carrier escaped its mean interval");
        const auto seconds=static_cast<long double>(p->last_sample-p->first_sample)/p->rate;
        const auto bound=((static_cast<long double>(p->first_uncertainty)+p->last_uncertainty)/seconds+
            p->past_frequency_error)/tau;
        check(interval->radius_hz>=bound && interval->radius_hz<bound+1e-10L,
            "mean bound omitted endpoint/process allowance or recounted local fit CI");
    }
    constexpr double frequency=123.125,epsilon=.02;
    for(const auto sigma:std::array<double,2>{0,.01}) {
        h::PhaseChord chord;check(chord.configure(1000,sigma,frequency),"mean setup failed");
        (void)chord.observe(endpoint(0,0,epsilon,frequency,epsilon,.1),0);
        (void)chord.observe(endpoint(400,0,-epsilon,frequency,epsilon,.1),400);
        const auto p=chord.prediction();const auto interval=p->mean_frequency_interval();
        check(interval && interval->lower_hz<=frequency && interval->upper_hz>=frequency,
            "opposite phase endpoint errors escaped the static mean interval");
    }
    for(const auto sign:std::array<double,2>{-1,1}) {
        constexpr double sigma=.01;
        const auto process=static_cast<double>(sigma*
            h::phase_chord_detail::pair_standard_deviations(0,400)*std::sqrt(.4L));
        h::PhaseChord chord;check(chord.configure(1000,sigma,frequency),"process-bound setup failed");
        (void)chord.observe(endpoint(0,0,-sign*epsilon,frequency,epsilon,.1),0);
        (void)chord.observe(endpoint(400,0,sign*(epsilon+process),frequency,epsilon,.1),400);
        const auto p=chord.prediction();check(bool(p),"supported Wiener increment was not uniquely unwrapped");
        const auto interval=p->mean_frequency_interval();
        check(interval && interval->lower_hz<=frequency && interval->upper_hz>=frequency,
            "signed process/endpoint boundary escaped the static mean interval");
    }
    // Different local Hz intervals resolve the SAME unique cycles. They must
    // not enter the endpoint-slope error a second time.
    std::array<double,2> radii{};
    for(std::size_t i=0;i<radii.size();++i) {
        h::PhaseChord chord;check(chord.configure(1000,0,17.25),"local-CI setup failed");
        for(std::uint64_t sample=0;sample<=700;sample+=100)
            (void)chord.observe(endpoint(sample,0,0,17.25,.02,i?1:.001),sample);
        const auto p=chord.prediction();check(bool(p),"valid unique cycle was lost");
        const auto interval=p->mean_frequency_interval();check(bool(interval),"mean interval missing");
        radii[i]=interval->radius_hz;
    }
    check(radii[0]==radii[1],"local frequency CI was double counted in mean precision");
    const auto original=seeded().prediction();
    for(unsigned kind=0;kind<11;++kind) {
        auto invalid=*original;
        switch(kind) {
        case 0:invalid.rate=0;break;
        case 1:invalid.first_sample=invalid.last_sample;break;
        case 2:invalid.last_sample=invalid.first_sample+1;break;
        case 3:invalid.frequency_hz=std::numeric_limits<double>::infinity();break;
        case 4:invalid.phase=std::numeric_limits<double>::quiet_NaN();break;
        case 5:invalid.first_uncertainty=-1;break;
        case 6:invalid.last_uncertainty=std::numeric_limits<double>::infinity();break;
        case 7:invalid.past_frequency_error=-1;break;
        case 8:invalid.sigma=std::numeric_limits<double>::quiet_NaN();break;
        case 9:invalid.first_uncertainty=std::numeric_limits<double>::max();
               invalid.last_uncertainty=std::numeric_limits<double>::max();break;
        case 10:invalid.frequency_hz=std::numeric_limits<double>::max();break;
        }
        check(!invalid.mean_frequency_interval(),"invalid mean data claimed a finite certificate");
    }
    h::PhaseChord ambiguous;check(ambiguous.configure(1000,0,17.25),"ambiguity setup failed");
    (void)ambiguous.observe(endpoint(0),0);(void)ambiguous.observe(endpoint(1000),1000);
    check(!ambiguous.prediction(),"ambiguous carrier cycles created a mean interval");
}
void mean_arithmetic_environment() {
    const auto p=seeded().prediction();check(bool(p),"environment setup failed");
    const auto rounding=std::fegetround();
    check(rounding!=-1 && std::fesetround(FE_UPWARD)==0,"rounding control unavailable");
    const auto refused_rounding=!p->mean_frequency_interval();
    check(std::fesetround(rounding)==0,"rounding mode restoration failed");
    check(refused_rounding,"unsupported directed arithmetic supplied a mean certificate");
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
    const auto old=_mm_getcsr();
    _mm_setcsr(old|(1U<<15));const auto refused_ftz=!p->mean_frequency_interval();_mm_setcsr(old);
    _mm_setcsr(old|(1U<<6));const auto refused_daz=!p->mean_frequency_interval();_mm_setcsr(old);
    check(refused_ftz&&refused_daz,"FTZ/DAZ arithmetic supplied a mean certificate");
#endif
    check(bool(p->mean_frequency_interval()),"restored arithmetic did not restore optional query");
}
void affine_residual_support() {
    namespace m=datapump::modem;
    const auto p=seeded(0,0).prediction();check(bool(p),"affine setup failed");
    m::PatternPhaseCurve curve;
    check(curve.configure(7,1000,.25,0,1),"curve setup failed");
    const m::PatternPhaseSegment initial{1000,0,static_cast<double>(tau*.1L/1000)};
    check(curve.append(std::span(&initial,1),p->last_sample,5000),"future tail setup failed");
    const h::PhaseFrequencyDomain domain{91,12,0,1000};
    const h::ConnectedPhaseQualification connected{domain,7,1000,4500,p->last_sample,.2,true};
    const h::PhaseScoredSupport support{1200,1800};
    const auto interval=p->affine_residual_interval(curve,domain,connected,support,17.);
    check(interval && interval->lower_hz<=.15 && interval->upper_hz>=.15,
        "affine correction did not enclose mean minus actual stored slope");
    check(std::abs(interval->center_hz-.15)<1e-12 && std::abs(interval->center_hz-curve.baseline())>.09,
        "affine query blindly substituted frozen B for a differently renewed mean");
    auto zero_error=connected;zero_error.maximum_phase_error_radians=0;
    const auto zero=p->affine_residual_interval(curve,domain,zero_error,support,17.);
    check(zero && zero->radius_hz==interval->radius_hz,
        "phase envelope was silently converted to a frequency CI using E/T");
    for(unsigned kind=0;kind<15;++kind) {
        auto certificate=connected;auto identity=domain;auto span=support;double expected=17.;
        switch(kind) {
        case 0:certificate.domain.capture_generation++;break;
        case 1:certificate.domain.rf_gauge++;break;
        case 2:certificate.domain.input_view++;break;
        case 3:certificate.domain.sample_rate++;break;
        case 4:certificate.view_id++;break;
        case 5:certificate.first_sample=span.first_sample+1;break; // historical qualification gap
        case 6:certificate.last_sample=span.last_sample-1;break;
        case 7:certificate.evidence_sample++;break;
        case 8:certificate.maximum_phase_error_radians=.251;break;
        case 9:certificate.stable_affine_tail=false;break; // bridge is not a stable tail
        case 10:span.last_sample=span.first_sample-1;break;
        case 11:identity.capture_generation=0;break;
        case 12:identity.rf_gauge=0;break;
        case 13:expected=std::numeric_limits<double>::quiet_NaN();break;
        case 14:certificate.first_sample=0;break; // evidence did not predate corrected samples
        }
        check(!p->affine_residual_interval(curve,identity,certificate,span,expected),
            "unknown provenance, uncovered support or bridge was accepted");
    }
    const auto beginning=curve.value_at(2000);
    const auto bridge_slope=initial.radians_per_sample+.001;
    const std::array<m::PatternPhaseSegment,2> bridge{{
        {2000,beginning,bridge_slope},{2200,beginning+200*bridge_slope,initial.radians_per_sample}}};
    check(curve.append(bridge,p->last_sample,5000),"bridge fixture setup failed");
    check(!p->affine_residual_interval(curve,domain,connected,{1900,2100},17.),
        "scored support crossing a phase bridge was accepted");
    check(!p->affine_residual_interval(curve,domain,connected,{2050,2150},17.),
        "a bridge segment was mistaken for the stable final tail");
    check(bool(p->affine_residual_interval(curve,domain,connected,{2300,2400},17.)),
        "actual qualified final affine tail was refused");
    curve.close();
    check(!p->affine_residual_interval(curve,domain,connected,{2300,2400},17.),
        "helper loss allowed fresh phase-dependent residual narrowing");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--unsupported-arithmetic-only") {
            check(!h::phase_chord_detail::interval_environment(),"negative build did not use unsupported arithmetic");
            const auto p=seeded().prediction();
            check(p && !p->mean_frequency_interval(),"unsafe arithmetic supplied a mean certificate");
            std::cout<<"unsupported mean arithmetic declined\n";return 0;
        }
        check(argc==1,"unknown private test selector");
        fresh_dates_and_bounded_storage();endpoint_error_and_process_bound();
        ambiguity_and_explicit_reset();invalid_authority();
        mean_frequency_interval();mean_arithmetic_environment();affine_residual_support();
        std::cout<<"helper phase chord passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
