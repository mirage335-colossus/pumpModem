#include "../src/live_helper_pair_model.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace datapump;
using namespace datapump::helper;
using datapump::live::detail::HelperPairModel;

namespace {
void check(bool value,const char* text) { if(!value)throw std::runtime_error(text); }

HelperPairModel::Config config() {
    HelperPairModel::Config c;
    c.maximum_actual_rf_hz=54e6;c.maximum_witness_age_seconds=1;
    c.model.rf_prior_sigma_ppm=2;c.model.audio_prior_sigma_ppm=100;
    c.model.rf_drift_prior_sigma_ppm_per_second=.01;c.model.audio_drift_prior_sigma_ppm_per_second=.1;
    c.model.rf_acceleration_psd_ppm2_per_second3=1e-6;
    c.model.audio_acceleration_psd_ppm2_per_second3=.01;
    c.model.maximum_prediction_age_seconds=3;c.model.retune_variance_floor_hz2=.04;
    c.model.learn_repeat_visit_scatter=false;
    return c;
}
LinkMapping mapping(std::uint64_t hop,double rf,int side) {
    LinkMapping m;m.capture_generation=1;m.timebase_identity=4;m.reference_identity=5;
    m.reference_identity_known=true;m.sample_rate=32000;m.phase_segment=hop+1;
    m.settled_first_sample=hop*12800+384;m.settled_end_sample=(hop+1)*12800;
    m.actual_rf_shift_hz=rf;m.helper_audio_hz=2400;m.data_audio_hz=1500;m.sideband_sign=side;
    m.provenance=LinkMappingProvenance::modelled_physical_capture;return m;
}
ToneFrequencyObservation observation(const LinkMapping& m,double audio,double common,double fraction,std::uint64_t end=0) {
    ToneFrequencyObservation o;o.valid=true;o.first_sample=m.settled_first_sample;o.end_sample=end?end:m.settled_end_sample;
    o.state_endpoint_sample=o.end_sample-1;
    const auto t=static_cast<double>(o.state_endpoint_sample-o.first_sample)/m.sample_rate;
    o.mass=o.k_prime_at_end=1;o.mu_seconds=o.k_at_end_seconds=t/2;
    o.integral_k_seconds2=3*t*t/20;o.integral_b_minus_s_k_seconds3=t*t*t/30;
    o.integral_k_squared_seconds3=23*t*t*t/504;
    const auto lag_samples=o.mu_seconds*m.sample_rate;const auto lag=std::ceil(lag_samples);
    o.mean_sample=o.state_endpoint_sample-static_cast<std::uint64_t>(lag);
    o.mean_sample_fraction=lag-lag_samples;
    o.frequency_hz=audio*(1+fraction)+common;
    o.independent_variance_hz2=1e-6;o.residual_variance_hz2=2e-7;return o;
}
HelperPairModel::Observation pair(const LinkMapping& m,std::uint64_t clear=7) {
    const auto common=m.sideband_sign*m.actual_rf_shift_hz*1e-6;
    HelperPairModel::Observation p;p.mapping=m;p.high=observation(m,2400,common,100e-6);
    p.low=observation(m,550,common,100e-6);p.clock_domain={-300e-6,300e-6};p.clear_generation=clear;return p;
}
IndependentFrequencyAperture expected(const LinkMapping& m,const ToneFrequencyObservation& o) {
    FrequencyApertureProcessMoments k{o.valid,o.first_sample,o.end_sample,o.state_endpoint_sample,o.mass,
        o.mu_seconds,o.integral_k_seconds2,o.integral_b_minus_s_k_seconds3,o.integral_k_squared_seconds3,
        o.k_at_end_seconds,o.k_prime_at_end};
    return {m,o.first_sample,o.end_sample,o.mean_sample,o.mean_sample_fraction,o.frequency_hz,
        2*(o.independent_variance_hz2+o.residual_variance_hz2),
        static_cast<double>(o.end_sample-o.first_sample)/m.sample_rate,k};
}
void paired_causal_protocol(int side) {
    const auto c=config();auto model=std::make_unique<HelperPairModel>();
    check(!model->configure(c,7,HelperPairModel::reservation_bytes()-1),"underfunded model configured");
    check(model->configure(c,7,HelperPairModel::reservation_bytes()),"pair model configure failed");
    check(model->retune_variance_hz2()==c.model.retune_variance_floor_hz2,
          "pair model did not expose the configured retune floor");
    OscillatorFrequencyModel oracle;check(oracle.reset(c.model),"oracle configure failed");
    constexpr std::array<double,4> rf{7e6,21e6,35e6,49e6};
    for(std::uint64_t hop=0;hop<rf.size();++hop) {
        auto m=mapping(hop,rf[hop],side);auto p=pair(m);
        const auto activation=p.high.end_sample+500;
        const auto u=model->observe_completed(p,activation);
        check(u.pair_used&&u.reason==HelperPairModel::Reason::accepted,"valid pair was declined");
        check(u.evidence_first_sample==p.high.first_sample&&u.evidence_end_sample==p.high.end_sample&&
              u.activation_frontier==activation&&std::abs(u.measured_audio_fraction-100e-6)<1e-12,
              "pair measurement lost its date or affine scale");
        auto low_mapping=m;low_mapping.helper_audio_hz=550;
        check(oracle.observe_pair(expected(m,p.high),expected(low_mapping,p.low),activation)==OscillatorModelReason::accepted,
              "inflated-variance oracle declined pair");
        const auto next=mapping(hop+1,rf[(hop+1)%rf.size()],side);
        check(!model->predict(next,activation-1,activation+1,7).conditional_valid,
              "late evidence gained authority before its activation frontier");
        check(!model->predict(next,activation,activation,7).conditional_valid,
              "pair prediction used an uncaptured sample");
        const auto q=model->predict(next,activation,activation+1,7);
        const auto r=oracle.predict(next,activation);
        check(q.conditional_valid&&r.valid&&q.activation_frontier==activation&&
              q.state.retune_variance_hz2==model->retune_variance_hz2()&&
              std::abs(q.state.data_residual_hz-r.data_residual_hz)<1e-12&&
              std::abs(q.state.data_variance_hz2-r.data_variance_hz2)<1e-12,
              "pair adapter disagreed with independently called conservative model");
        check(!model->predict(next,activation,activation+1,8).conditional_valid,
              "old generation retained prediction");
        check(!model->observe_completed(p,activation).pair_used,"same physical retune counted twice");
        if(hop==3) {
            check(!model->advance_activation_frontier(activation-1),"activation frontier moved backward");
            check(model->advance_activation_frontier(activation+10),"late commit frontier rejected");
            check(!model->predict(next,activation+9,activation+11,7).conditional_valid,
                  "late commit backdated paired prediction");
            check(model->predict(next,activation+10,activation+11,7).conditional_valid,
                  "captured post-commit sample lost prediction");
        }
    }
    model->close();const auto next=mapping(4,7e6,side);
    check(!model->predict(next,next.settled_first_sample,next.settled_first_sample+1,7).conditional_valid,
          "closed helper retained new prediction");
    check(!model->reset_generation(7,HelperPairModel::reservation_bytes()),"same-generation reset accepted");
    check(!model->reset_generation(8,HelperPairModel::reservation_bytes()-1),"underfunded reset accepted");
    check(model->reset_generation(8,HelperPairModel::reservation_bytes()),"funded generation reset failed");
    check(!model->predict(next,next.settled_first_sample,next.settled_first_sample+1,8).conditional_valid,
          "generation reset retained old prediction");
}
void failures_do_not_commit() {
    auto c=config();auto model=std::make_unique<HelperPairModel>();
    check(model->configure(c,3,HelperPairModel::reservation_bytes()),"configure failed");
    auto m=mapping(0,7e6,1);auto p=pair(m,3);
    auto bad=p;bad.low.frequency_hz+=1;const auto spur=model->observe_completed(bad,p.high.end_sample);
    check(!spur.pair_used&&spur.reason==HelperPairModel::Reason::clock_domain_inconsistent,
          "out-of-domain low spur was used");
    bad=p;bad.low.integral_k_seconds2+=.001;
    check(!model->observe_completed(bad,p.high.end_sample).pair_used,"mismatched kernels were paired");
    bad=p;bad.clear_generation=4;
    check(model->observe_completed(bad,p.high.end_sample).reason==HelperPairModel::Reason::stale_generation,
          "old work survived clear generation");
    bad=p;bad.mapping.provenance=LinkMappingProvenance::fake_schedule_preview;
    check(model->observe_completed(bad,p.high.end_sample).reason==HelperPairModel::Reason::invalid_mapping,
          "fake RF mapping was treated as physical");
    bad=p;bad.mapping.actual_rf_shift_hz=5.8e9;
    check(model->observe_completed(bad,p.high.end_sample).reason==HelperPairModel::Reason::invalid_mapping,
          "unqualified microwave model was silently enabled");
    bad=p;bad.mapping.helper_audio_hz=2300;
    check(model->observe_completed(bad,p.high.end_sample).reason==HelperPairModel::Reason::invalid_mapping,
          "nonstandard high pilot was accepted");
    bad=p;bad.low.frequency_hz=215;
    check(model->observe_completed(bad,p.high.end_sample).reason==HelperPairModel::Reason::invalid_measurement,
          "low-pilot noise band crossed DC clearance");
    bad=p;bad.high.frequency_hz=15810;
    check(model->observe_completed(bad,p.high.end_sample).reason==HelperPairModel::Reason::invalid_measurement,
          "high-pilot noise band crossed Nyquist clearance");
    check(model->observe_completed(p,p.high.end_sample).pair_used,
          "rejected measurements changed the pair state");
    // An in-domain nearby spur remains statistically plausible to this simple
    // box check. Its acceptance is intentionally *conditional* on low-tone
    // selection, never evidence that the pilot identity was authenticated.
    auto ambiguous=std::make_unique<HelperPairModel>();
    check(ambiguous->configure(c,3,HelperPairModel::reservation_bytes()),"ambiguous configure failed");
    bad=p;bad.low.frequency_hz+=.1;
    const auto accepted=ambiguous->observe_completed(bad,bad.high.end_sample);
    check(accepted.pair_used && accepted.measured_audio_fraction<p.clock_domain.upper_fraction &&
          accepted.measured_audio_fraction>p.clock_domain.lower_fraction,
          "in-domain tone-selection limitation disappeared from protocol");
}
void retune_scatter_getter() {
    auto c=config();c.model.learn_repeat_visit_scatter=true;
    auto model=std::make_unique<HelperPairModel>();
    check(model->configure(c,7,HelperPairModel::reservation_bytes()),"scatter model configure failed");
    bool grew=false;
    for(std::uint64_t hop=0;hop<4;++hop) {
        const auto m=mapping(hop,7e6,1);auto p=pair(m);
        const auto common=hop%2?2.:-2.;p.high.frequency_hz+=common;p.low.frequency_hz+=common;
        check(model->observe_completed(p,p.high.end_sample).pair_used,"scatter pair declined");
        const auto next=mapping(hop+1,7e6,1);const auto at=next.settled_first_sample;
        const auto prediction=model->predict(next,at,at+1,7);
        check(prediction.conditional_valid&&prediction.state.retune_variance_hz2==model->retune_variance_hz2(),
              "getter and prediction disagreed on learned retune variance");
        grew=grew||model->retune_variance_hz2()>c.model.retune_variance_floor_hz2;
    }
    check(grew,"scatter control did not grow the retune variance");
}
void forward_gaussian_reference(int side) {
    // Independent one-aperture forward Gaussian calculation in high/low
    // coordinates, not the implementation's high/(low-high) coordinates.
    auto c=config();c.model.rf_drift_prior_sigma_ppm_per_second=0;
    c.model.audio_drift_prior_sigma_ppm_per_second=0;
    c.model.rf_acceleration_psd_ppm2_per_second3=0;
    c.model.audio_acceleration_psd_ppm2_per_second3=0;
    auto model=std::make_unique<HelperPairModel>();
    check(model->configure(c,7,HelperPairModel::reservation_bytes()),"oracle configure failed");
    const auto m=mapping(0,21e6,side);const auto common=side*m.actual_rf_shift_hz*1e-6;
    HelperPairModel::Observation p;p.mapping=m;p.clear_generation=7;p.clock_domain={-300e-6,300e-6};
    p.high=observation(m,2400,common,100e-6,10000);
    p.low=observation(m,550,common,100e-6,10000);
    const auto update=model->observe_completed(p,10000);check(update.pair_used,"oracle pair declined");
    const auto result=model->predict(m,10000,10001,7);check(result.conditional_valid,"oracle prediction declined");
    const long double hr=side*m.actual_rf_shift_hz*1e-6L,hh=2400e-6L,hl=550e-6L;
    const long double pr=4,pa=10000,rv=2*(p.high.independent_variance_hz2+p.high.residual_variance_hz2);
    const long double retune=c.model.retune_variance_floor_hz2;
    const long double s00=hr*hr*pr+hh*hh*pa+rv+retune;
    const long double s11=hr*hr*pr+hl*hl*pa+rv+retune;
    const long double s01=hr*hr*pr+hh*hl*pa+retune;
    const long double determinant=s00*s11-s01*s01;
    const long double inv00=s11/determinant,inv11=s00/determinant,inv01=-s01/determinant;
    const long double yd0=p.high.frequency_hz-2400,yd1=p.low.frequency_hz-550;
    const long double hd0=hr*pr*hr+1500e-6L*pa*hh;
    const long double hd1=hr*pr*hr+1500e-6L*pa*hl;
    const long double expected_mean=hd0*(inv00*yd0+inv01*yd1)+hd1*(inv01*yd0+inv11*yd1);
    const long double expected_variance=hr*hr*pr+1500e-6L*1500e-6L*pa+retune-
        hd0*(inv00*hd0+inv01*hd1)-hd1*(inv01*hd0+inv11*hd1);
    check(std::abs(result.state.data_residual_hz-expected_mean)<1e-9 &&
          std::abs(result.state.data_variance_hz2-expected_variance)<1e-9,
          "paired posterior disagreed with forward Gaussian reference");
}
void forward_positive_process_reference(int side) {
    // Prior is at aperture start; independently form the endpoint state,
    // observations and their joint covariance, then condition in high/low
    // coordinates. The implementation uses a backward-kernel state update in
    // high/(low-high) coordinates, so this is a distinct forward calculation.
    auto c=config();c.model.learn_repeat_visit_scatter=false;
    c.model.rf_acceleration_psd_ppm2_per_second3=.001;
    c.model.audio_acceleration_psd_ppm2_per_second3=.02;
    auto model=std::make_unique<HelperPairModel>();
    check(model->configure(c,7,HelperPairModel::reservation_bytes()),"positive-Q configure failed");
    const auto m=mapping(0,21e6,side);const auto common=side*m.actual_rf_shift_hz*1e-6;
    HelperPairModel::Observation p;p.mapping=m;p.clear_generation=7;p.clock_domain={-300e-6,300e-6};
    p.high=observation(m,2400,common,100e-6,10000);
    p.low=observation(m,550,common,100e-6,10000);
    check(model->observe_completed(p,10000).pair_used,"positive-Q pair declined");
    const auto actual=model->predict(m,10000,10001,7);check(actual.conditional_valid,"positive-Q prediction declined");
    using L=long double;
    const L t=static_cast<L>(p.high.end_sample-1-p.high.first_sample)/m.sample_rate;
    const L mu=p.high.mu_seconds,dt=1.L/m.sample_rate;
    const std::array<L,2> levels{4,10000},rates{.0001L,.01L},q{.001L,.02L};
    const std::array<std::array<L,2>,2> h{{{side*m.actual_rf_shift_hz*1e-6L,2400e-6L},
                                           {side*m.actual_rf_shift_hz*1e-6L,550e-6L}}};
    std::array<std::array<L,4>,4> P{};std::array<std::array<L,2>,4> C{};
    std::array<std::array<L,2>,2> S{};
    const L i1=p.high.integral_k_seconds2,i2=p.high.integral_b_minus_s_k_seconds3;
    const L ik2=p.high.integral_k_squared_seconds3;
    for(std::size_t k=0;k<2;++k) {
        const L level=levels[k],rate=rates[k],density=q[k];
        P[k][k]=level+t*t*rate+density*t*t*t/3;
        P[k][k+2]=P[k+2][k]=t*rate+density*t*t/2;
        P[k+2][k+2]=rate+density*t;
        const L cross_position=level+t*(t-mu)*rate+density*(t*t*t/3-mu*t*t/2+i2);
        const L cross_rate=(t-mu)*rate+density*(t*t/2-mu*t+i1);
        const L obs=level+(t-mu)*(t-mu)*rate+
            density*(t*t*t/3-mu*t*t+mu*mu*t+2*i2-2*mu*i1+ik2);
        for(std::size_t a=0;a<2;++a) {
            C[k][a]=h[a][k]*cross_position;C[k+2][a]=h[a][k]*cross_rate;
            for(std::size_t b=0;b<2;++b)S[a][b]+=h[a][k]*h[b][k]*obs;
        }
    }
    const L retune=c.model.retune_variance_floor_hz2;
    const L vh=2*(p.high.independent_variance_hz2+p.high.residual_variance_hz2);
    const L vl=2*(p.low.independent_variance_hz2+p.low.residual_variance_hz2);
    S[0][0]+=vh+retune;S[1][1]+=vl+retune;S[0][1]+=retune;S[1][0]+=retune;
    const L determinant=S[0][0]*S[1][1]-S[0][1]*S[1][0];
    check(determinant>0,"positive-Q oracle singular");
    const std::array<std::array<L,2>,2> inverse{{{S[1][1]/determinant,-S[0][1]/determinant},
                                                 {-S[1][0]/determinant,S[0][0]/determinant}}};
    const std::array<L,2> y{p.high.frequency_hz-2400,p.low.frequency_hz-550};
    std::array<L,4> mean{};std::array<std::array<L,4>,4> posterior=P;
    for(std::size_t i=0;i<4;++i)for(std::size_t a=0;a<2;++a)for(std::size_t b=0;b<2;++b) {
        mean[i]+=C[i][a]*inverse[a][b]*y[b];
        for(std::size_t j=0;j<4;++j)posterior[i][j]-=C[i][a]*inverse[a][b]*C[j][b];
    }
    const L hr=side*m.actual_rf_shift_hz*1e-6L,ha=1500e-6L;
    const std::array<L,4> g{hr,ha,dt*hr,dt*ha};
    L expected_mean=0,expected_variance=retune+(hr*hr*q[0]+ha*ha*q[1])*dt*dt*dt/3;
    for(std::size_t i=0;i<4;++i) {
        expected_mean+=g[i]*mean[i];
        for(std::size_t j=0;j<4;++j)expected_variance+=g[i]*posterior[i][j]*g[j];
    }
    check(std::abs(actual.state.data_residual_hz-expected_mean)<1e-8 &&
          std::abs(actual.state.data_variance_hz2-expected_variance)<1e-8,
          "positive-Q pair disagreed with forward joint Gaussian reference");
}
}
int main() try {
    paired_causal_protocol(1);paired_causal_protocol(-1);failures_do_not_commit();retune_scatter_getter();
    forward_gaussian_reference(1);forward_gaussian_reference(-1);
    forward_positive_process_reference(1);forward_positive_process_reference(-1);
    std::cout<<"helper pair model numeric protocol passed; conditional, no phase or acquisition authority\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
