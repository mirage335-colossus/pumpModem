#include "datapump/helper_tone_tracker.hpp"
#include "datapump/helper_phase_precision.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
using namespace datapump;
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
constexpr unsigned rate=8000;constexpr double tau=2*std::numbers::pi,carrier=2300.37;
double ramp(double t){const auto local=std::fmod(t,.4);return .5*(1-std::cos(std::numbers::pi*std::clamp(std::min(local/.004,(.4-local)/.004),0.,1.)));}
void continuous_phase_survives_ramps_and_exceeds_old_history() {
    helper::ToneTrackerConfig c{rate,2000,1500,0,.5,true,true};auto old=c;old.continuous_pilot=false;
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));
    helper::ToneTracker baseline(old,helper::ToneTracker::workspace_required(old));
    // Isolate the waveform invariant from statistical model-change false
    // alarms. No phase diffusion or AWGN is injected in this ramp-only check;
    // the separate seeded component ensemble measures noisy availability.
    std::array<float,80> pcm{};double phase=.37;
    for(std::uint64_t n=0;n<40ULL*rate;n+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i){const auto t=(n+i)/static_cast<double>(rate);phase+=tau*carrier/rate;pcm[i]=static_cast<float>(.6*ramp(t)*std::cos(phase));}
        tracker.push(pcm,n);baseline.push(pcm,n);
    }
    const auto s=tracker.snapshot(),b=baseline.snapshot();
    check(s.qualified && s.frequency_continuous_phase,"continuous pilot did not qualify from causal broad acquisition");
    check(s.coherent_aperture_seconds>36 && s.coherent_aperture_seconds<=40,"ramps discarded continuous pilot phase history");
    check(s.uncertainty_hz<.02 && s.uncertainty_hz<b.uncertainty_hz/5,"continuous phase did not improve measured frequency precision");
    check(std::abs(s.frequency_hz-carrier)<=s.uncertainty_hz,"continuous oscillator interval escaped deterministic seeded truth");
    const auto dated=s.frequency_at(s.sample-rate*2/5);
    check(dated.valid && std::abs(dated.frequency_hz-carrier)<=dated.uncertainty_hz,"continuous frequency covariance lost its actual support");
    tracker.begin_pilot_segment(40ULL*rate,2000,1500,true);
    const auto hop=tracker.snapshot();
    check(!hop.frequency_continuous_phase && hop.qualified && hop.frequency_knowledge_valid &&
        hop.frequency_covariance_valid && hop.sample==s.sample && hop.first_sample==s.first_sample &&
        !hop.phase_fit_qualified && !hop.phase_fit_support_uncertainty_cycles,
        "compatible retune lost dated frequency knowledge or retained phase authority");
    std::cout<<"continuous ramp support="<<s.coherent_aperture_seconds<<" uncertainty="<<s.uncertainty_hz<<" baseline="<<b.uncertainty_hz<<'\n';
}
void model_change_restarts_numeric_fit_and_loss_withdraws() {
    helper::ToneTrackerConfig c{rate,2000,1500,0,.5,true,true};helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));
    std::mt19937 random{49172};std::normal_distribution<double> normal;std::array<float,80> pcm{};double phase=.37;
    helper::ToneTrackerSnapshot recovered;std::size_t escaped=0;std::uint64_t last=UINT64_MAX;
    for(std::uint64_t n=0;n<48ULL*rate;n+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i){const auto t=(n+i)/static_cast<double>(rate);phase+=tau*(carrier+.03*std::max(0.,t-30))/rate;pcm[i]=static_cast<float>((t<44?.6*ramp(t)*std::cos(phase):0)+.3*std::sqrt(rate/10000.)*normal(random));}
        tracker.push(pcm,n);const auto s=tracker.snapshot();
        if(s.qualified && s.sample!=last){last=s.sample;const auto t=s.sample/static_cast<double>(rate);escaped+=std::abs(s.frequency_hz-carrier-.03*std::max(0.,t-30))>s.uncertainty_hz;}
        if(s.qualified && s.continuous_model_resets && s.first_sample<s.continuous_last_model_reset_sample)
            std::cout<<"rejected-history publication date="<<s.sample/static_cast<double>(rate)<<" first="<<s.first_sample/static_cast<double>(rate)<<" reset="<<s.continuous_last_model_reset_sample/static_cast<double>(rate)<<" continuous="<<s.frequency_continuous_phase<<" U="<<s.uncertainty_hz<<'\n';
        if(s.qualified && s.continuous_model_resets)check(s.first_sample>=s.continuous_last_model_reset_sample,
            "post-change frequency fallback reused rejected oscillator history");
        if(n==43ULL*rate)recovered=s;
    }
    check(recovered.qualified && recovered.frequency_continuous_phase && recovered.continuous_model_resets>0,
        "late drift change did not restart and recover the numeric oscillator fit");
    check(recovered.uncertainty_hz<.02,"model-change recovery retained broad acquisition cost");
    const auto lost=tracker.snapshot();check(!lost.qualified && !lost.present && lost.frequency_knowledge_valid &&
        lost.frequency_covariance_valid && !lost.phase_fit_qualified && !lost.phase_fit_support_uncertainty_cycles,
        "final Helper loss lost dated knowledge or retained current authority");
    std::cout<<"continuous model changes="<<recovered.continuous_model_resets<<" escaped_estimates_including_detection_latency="<<escaped<<'\n';
}
void long_observation_gap_is_callback_independent() {
    helper::ToneTrackerConfig c{rate,2000,1500,0,.5,true,true};
    helper::ToneTracker small(c,helper::ToneTracker::workspace_required(c));
    helper::ToneTracker large(c,helper::ToneTracker::workspace_required(c));
    std::array<float,80> pcm{};double phase=.37;
    for(std::uint64_t n=0;n<4ULL*rate;n+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i){phase+=tau*carrier/rate;pcm[i]=static_cast<float>(.6*std::cos(phase));}
        small.push(pcm,n);large.push(pcm,n);
    }
    check(small.snapshot().frequency_continuous_phase,"gap fixture did not first acquire continuous phase");
    // One call contains both the genuine missing observation interval and the
    // returning pilot. It must not hide that loss by ending on a good row.
    std::array<float,rate/2> gap_and_return{};
    for(std::size_t i=0;i<gap_and_return.size();++i){phase+=tau*carrier/rate;gap_and_return[i]=i<3*rate/10?0:static_cast<float>(.6*std::cos(phase));}
    large.push(gap_and_return,4ULL*rate);
    for(std::size_t i=0;i<gap_and_return.size();i+=pcm.size())small.push(std::span(gap_and_return).subspan(i,pcm.size()),4ULL*rate+i);
    const auto a=small.snapshot(),b=large.snapshot();
    check(a.continuous_phase_resets==b.continuous_phase_resets && a.continuous_phase_resets==0 &&
        a.frequency_knowledge_valid && b.frequency_knowledge_valid,
        "a resolved sparse phase interval reset knowledge or depended on callback size");
    check(a.frequency_continuous_phase==b.frequency_continuous_phase && a.sample==b.sample &&
        a.first_sample==b.first_sample && std::abs(a.frequency_hz-b.frequency_hz)<1e-12 &&
        std::abs(a.uncertainty_hz-b.uncertainty_hz)<1e-12,
        "sparse accepted observations were misdated by callback size");
}

void unknown_phase_return_keeps_frequency_marginal() {
    helper::ToneTrackerConfig c{rate,2000,1500,0,.5,true,true};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));
    std::array<float,80> pcm{};double phase=.37;helper::ToneTrackerSnapshot before;
    bool saw_phase_reset=false;
    for(std::uint64_t n=0;n<8ULL*rate;n+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i) {
            const auto t=(n+i)/static_cast<double>(rate);phase+=tau*carrier/rate;
            pcm[i]=(t>=4 && t<4.3)?0:static_cast<float>(.6*ramp(t)*std::cos(phase+(t>=4.3?.23*tau:0)));
        }
        tracker.push(pcm,n);const auto s=tracker.snapshot();
        if(n+pcm.size()==4ULL*rate)before=s;
        if(s.continuous_phase_resets) {
            saw_phase_reset=true;
            check(s.frequency_knowledge_valid && s.frequency_covariance_valid,
                "unknown phase return erased dated frequency knowledge");
        }
    }
    const auto s=tracker.snapshot();
    check(before.qualified && saw_phase_reset && s.qualified && s.frequency_continuous_phase,
        "phase jump fixture did not reset only phase and recover");
    check(s.first_sample<=before.first_sample && s.frequency_support_seconds>s.coherent_aperture_seconds+3,
        "independent phase initialization lost earlier oscillator support or invented coherence");
    check(std::abs(s.frequency_hz-carrier)<=s.uncertainty_hz && s.uncertainty_hz<.01,
        "returning phase lost useful supported frequency precision");
}
void local_frequency_does_not_require_phase_correction() {
    helper::ToneTrackerConfig c{rate,2000,1500,1,.5,true,true};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));
    tracker.begin_pilot_segment(0,2000,1500,false,1,0,true);
    std::array<float,80> pcm{};double phase=.37;bool saw_frequency_without_phase=false;
    for(std::uint64_t n=0;n<rate;n+=pcm.size()) {
        for(auto& x:pcm){phase+=tau*carrier/rate;x=static_cast<float>(.6*std::cos(phase));}
        tracker.push(pcm,n);const auto s=tracker.snapshot();
        if(s.qualified && s.phase_fit_support_uncertainty_cycles && *s.phase_fit_support_uncertainty_cycles>=.125) {
            saw_frequency_without_phase=true;
            check(s.frequency_local_aperture && s.frequency_knowledge_valid && s.frequency_covariance_valid &&
                std::abs(s.frequency_hz-carrier)<=s.uncertainty_hz,
                "local frequency certificate borrowed failed phase authority");
        }
    }
    check(saw_frequency_without_phase,"local frequency still required the optional phase gate");
}


// Independent Gaussian impulse construction: integrate endpoint and actual
// discrete-Hann phase-average responses, not the production polynomials.
void frequency_process_dense_gaussian_oracle() {
    std::cout<<std::setprecision(14);
    constexpr std::size_t count=160,steps=10000;
    constexpr long double q=.0009L,thermal=1e-4L;
    const long double duration=static_cast<long double>(count-1)/rate,du=duration/steps;
    std::array<long double,count> weights{},times{};long double sum=0;
    for(std::size_t i=0;i<count;++i){times[i]=static_cast<long double>(i)/rate;weights[i]=.5L-.5L*std::cos(2*std::numbers::pi_v<long double>*i/(count-1));sum+=weights[i];}
    long double lag=0,lag2=0;
    for(std::size_t i=0;i<count;++i){weights[i]/=sum;const auto d=duration-times[i];lag+=weights[i]*d;lag2+=weights[i]*d*d;}
    const std::array<long double,3> h{1,-lag,.5L*lag2};
    std::array<std::array<long double,4>,4> joint{};
    std::array<std::array<long double,3>,3> covariance{};std::array<long double,3> cross{};long double residual_variance=0;
    for(std::size_t j=0;j<steps;++j){
        const auto u=(j+.5L)*du,d=duration-u;
        const std::array<long double,3> endpoint{.5L*d*d,d,1};
        long double measured=0,predicted=0;
        for(std::size_t i=0;i<count;++i)if(times[i]>u){const auto elapsed=times[i]-u;measured+=weights[i]*elapsed*elapsed/2;}
        for(unsigned i=0;i<3;++i)predicted+=h[i]*endpoint[i];
        const auto residual=measured-predicted;
        const std::array<long double,4> responses{endpoint[0],endpoint[1],endpoint[2],measured};
        for(unsigned i=0;i<4;++i)for(unsigned k=0;k<4;++k)joint[i][k]+=q*du*responses[i]*responses[k];
        for(unsigned i=0;i<3;++i){cross[i]+=q*du*endpoint[i]*residual;for(unsigned k=0;k<3;++k)covariance[i][k]+=q*du*endpoint[i]*endpoint[k];}
        residual_variance+=q*du*residual*residual;
    }
    const auto analytic=helper::phase_detail::frequency_process_covariance(static_cast<double>(q),static_cast<double>(duration));
    const auto projection=helper::phase_detail::hann_frequency_process(count,rate);long double max_relative=0;
    const auto compare=[&](long double a,long double b){const auto relative=std::abs(a-b)/std::max(std::abs(b),1e-30L);max_relative=std::max(max_relative,relative);check(relative<5e-7L,"dense Gaussian aperture covariance disagrees with analytic process");};
    for(unsigned i=0;i<3;++i){for(unsigned k=0;k<3;++k)compare(analytic[i][k],covariance[i][k]);compare(q*projection.cross[i],cross[i]);}
    compare(q*projection.variance,residual_variance);
    // An independent unknown initial phase makes this observation unable to
    // change the frequency/rate marginal. Condition the directly constructed
    // joint Gaussian with a broad phase prior, without calling private code.
    const std::array<long double,4> initial_f{duration,1,0,duration-lag};
    const std::array<long double,4> initial_r{duration*duration/2,duration,1,(duration*duration-2*duration*lag+lag2)/2};
    for(unsigned i=0;i<4;++i)for(unsigned k=0;k<4;++k)joint[i][k]+=.04L*initial_f[i]*initial_f[k]+.003L*(initial_f[i]*initial_r[k]+initial_r[i]*initial_f[k])+.01L*initial_r[i]*initial_r[k];
    for(const auto i:{0U,3U})for(const auto k:{0U,3U})joint[i][k]+=1e6L;
    joint[3][3]+=thermal;
    long double marginal_error=0;
    for(unsigned i=1;i<3;++i)for(unsigned k=1;k<3;++k){const auto posterior=joint[i][k]-joint[i][3]*joint[k][3]/joint[3][3];marginal_error=std::max(marginal_error,std::abs(posterior-joint[i][k]));}
    check(marginal_error<1e-11L,"independent-phase Gaussian observation changed frequency/rate marginal");
    std::cout<<"frequency process dense oracle steps="<<steps<<" aperture="<<static_cast<double>(duration)<<" max_relative="<<static_cast<double>(max_relative)<<" independent_phase_marginal_error="<<static_cast<double>(marginal_error)<<" private_initializer_directly_called=0\n";
}
void interval_dates(const helper::ToneFrequencyInterval& interval,std::uint64_t first,
                    std::uint64_t end,std::uint64_t captured) {
    check(interval.valid && interval.first_sample==first && interval.end_sample==end && first<end,
          "buffered frequency query changed its requested interval");
    check(interval.captured_end_sample==captured && interval.evidence_first_sample<=first &&
          end<=interval.evidence_end_sample && interval.evidence_end_sample<=captured &&
          interval.evidence_first_sample<=interval.anchor_sample && interval.anchor_sample<interval.evidence_end_sample,
          "buffered frequency query used future or reversed dated evidence");
    check(std::isfinite(interval.frequency_hz) && std::isfinite(interval.uncertainty_hz) && interval.uncertainty_hz>=0,
          "buffered frequency interval was nonfinite or negative");
}
helper::ToneFrequencyQuery backward_interval(const helper::ToneTrackerSnapshot& snapshot,
                                            std::uint64_t first,std::uint64_t end) {
    helper::ToneFrequencyQuery out;if(first>=end)return out;
    const auto middle=first+(end-first-1)/2;const auto fraction=.5*((end-first-1)%2);
    const auto a=snapshot.frequency_at(first),b=snapshot.frequency_at(end-1),m=snapshot.frequency_at(middle,fraction);
    if(!a.valid || !b.valid || !m.valid)return out;
    out.valid=true;out.frequency_hz=m.frequency_hz;
    out.uncertainty_hz=std::max(a.uncertainty_hz+std::abs(a.frequency_hz-m.frequency_hz),
                              b.uncertainty_hz+std::abs(b.frequency_hz-m.frequency_hz));
    return out;
}
struct LagMetrics {
    std::size_t queries=0,eligible=0,valid=0,smoothed=0,narrower=0,escapes=0,startup_escapes=0,loss_escapes=0;
    double max_error=0,max_ratio=0,last_u=0,last_backward_u=0,cpu=0;
};
template<class Truth>void observe_fixed_lag(helper::ToneTracker& tracker,const helper::ToneTrackerSnapshot& snapshot,
        double cn0,unsigned seed,bool drift,double q,const Truth& truth,LagMetrics& metrics) {
    constexpr std::uint64_t lag=rate*2/5,width=rate/100;
    const auto captured=snapshot.captured_end_sample;
    check(!tracker.frequency_interval(captured,captured+1).valid &&
          !tracker.frequency_interval(captured+1,captured+2).valid &&
          !tracker.frequency_interval(captured,captured).valid,
          "fixed-lag query manufactured future or empty support");
    if(captured<=lag+width)return;
    const auto first=captured-lag-width,end=captured-lag;
    const auto start=std::clock();const auto interval=tracker.frequency_interval(first,end);
    const auto backward=backward_interval(snapshot,first,end);
    metrics.cpu+=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
    ++metrics.queries;metrics.eligible+=backward.valid;
    check(!backward.valid || interval.valid,"fixed-lag API discarded its valid ordinary backward fallback");
    const auto after=tracker.snapshot();
    check(after.captured_end_sample==captured && after.sample==snapshot.sample &&
          after.phase_fit_qualified==snapshot.phase_fit_qualified && after.phase_fit_sample==snapshot.phase_fit_sample &&
          after.phase_fit_first_sample==snapshot.phase_fit_first_sample && after.phase_fit_end_sample==snapshot.phase_fit_end_sample,
          "frequency-only lag query changed the captured frontier or phase authority");
    if(!interval.valid)return;
    interval_dates(interval,first,end,captured);++metrics.valid;metrics.smoothed+=interval.smoothed;
    const bool narrower=backward.valid && interval.uncertainty_hz<backward.uncertainty_hz*(1-1e-12);
    metrics.narrower+=narrower;
    const auto error=std::max(std::abs(interval.frequency_hz-truth(first/static_cast<double>(rate))),
                              std::abs(interval.frequency_hz-truth((end-1)/static_cast<double>(rate))));
    const bool escape=error>interval.uncertainty_hz;
    metrics.escapes+=escape;metrics.startup_escapes+=escape && end<2ULL*rate;
    metrics.loss_escapes+=escape && first>=44ULL*rate;
    metrics.max_error=std::max(metrics.max_error,error);
    metrics.max_ratio=std::max(metrics.max_ratio,error/std::max(interval.uncertainty_hz,1e-30));
    metrics.last_u=interval.uncertainty_hz;metrics.last_backward_u=backward.valid?backward.uncertainty_hz:0;
    if(escape)std::cout<<"fixed-lag escape CN0="<<cn0<<" seed="<<seed<<" drift="<<drift<<" q="<<q
        <<" query_first="<<first/static_cast<double>(rate)<<" query_end="<<end/static_cast<double>(rate)
        <<" captured="<<captured/static_cast<double>(rate)<<" smoothed="<<interval.smoothed
        <<" anchor="<<interval.anchor_sample/static_cast<double>(rate)
        <<" evidence_first="<<interval.evidence_first_sample/static_cast<double>(rate)
        <<" evidence_end="<<interval.evidence_end_sample/static_cast<double>(rate)
        <<" error="<<error<<" U="<<interval.uncertainty_hz<<" backward_U="<<metrics.last_backward_u<<'\n';
}
struct ProcessMetrics {
    std::size_t polls=0,qualified=0,unique=0,covered=0,escapes=0,startup_escapes=0,loss_escapes=0,dated=0,dated_escapes=0;
    std::size_t loss_failures=0;bool previous_present=false;
    std::uint64_t last=UINT64_MAX;double first=-1,last_date=0,max_error=0,max_ratio=0,final_u=0,final_error=0,cpu=0;
    helper::ToneTrackerSnapshot final;
    LagMetrics lag;
};
ProcessMetrics positive_process_pair(double cn0,unsigned seed,bool drift,bool positive_summary_only=false) {
    constexpr double process_q=9e-4;helper::ToneTrackerConfig control{rate,2000,1500,0,.5,true,true};auto process=control;process.frequency_acceleration_psd_hz2_per_second3=process_q;
    helper::ToneTracker baseline(control,helper::ToneTracker::workspace_required(control)),tracker(process,helper::ToneTracker::workspace_required(process));
    std::array<helper::ToneTracker*,2> owners{&baseline,&tracker};std::array<ProcessMetrics,2> metrics;
    std::mt19937 random{seed};std::normal_distribution<double> normal;std::array<float,80> pcm{};double phase=.37;
    const auto truth=[&](double t){return carrier+(drift?.03*std::max(0.,t-30):0);};
    for(std::uint64_t n=0;n<48ULL*rate;n+=pcm.size()){
        for(std::size_t i=0;i<pcm.size();++i){const auto t=(n+i)/static_cast<double>(rate);phase+=tau*truth(t)/rate;pcm[i]=static_cast<float>((t<44?.6*ramp(t)*std::cos(phase):0)+.3*std::sqrt(rate/std::pow(10.,cn0/10))*normal(random));}
        for(unsigned variant=0;variant<2;++variant){auto& m=metrics[variant];auto& owner=*owners[variant];const auto begin=std::clock();owner.push(pcm,n);m.cpu+=static_cast<double>(std::clock()-begin)/CLOCKS_PER_SEC;const auto s=owner.snapshot();
            if(n>=44ULL*rate && ((s.qualified&&s.sample!=m.last)||s.present!=m.previous_present))std::cout<<"process loss diagnostic CN0="<<cn0<<" seed="<<seed<<" drift="<<drift<<" q="<<(variant?process_q:0)<<" captured="<<s.captured_end_sample/static_cast<double>(rate)<<" date="<<s.sample/static_cast<double>(rate)<<" qualified="<<s.qualified<<" present="<<s.present<<" knowledge="<<s.frequency_knowledge_valid<<" CNO="<<s.carrier_to_noise_db_hz<<" age="<<s.observation_age_seconds<<" projection_valid="<<s.projection_phase_valid<<" projection_date="<<(s.phase_sample+s.phase_sample_fraction)/rate<<" phase_fit="<<s.phase_fit_qualified<<" phase_first="<<s.phase_fit_first_sample/static_cast<double>(rate)<<" phase_end="<<s.phase_fit_end_sample/static_cast<double>(rate)<<" context_first="<<s.frequency_context_first_sample/static_cast<double>(rate)<<" context_end="<<s.frequency_context_end_sample/static_cast<double>(rate)<<'\n';
            m.previous_present=s.present;
            if(n+pcm.size()<=44ULL*rate){++m.polls;m.qualified+=s.qualified;}
            if(s.qualified){check(s.first_sample<=s.sample&&s.sample<n+pcm.size()&&s.captured_end_sample<=n+pcm.size(),"process publication exceeds captured support");check(!s.frequency_at(s.sample+1).valid,"process query manufactured future support");}
            if(!s.qualified||s.sample==m.last)continue;
            observe_fixed_lag(owner,s,cn0,seed,drift,variant?process_q:0,truth,m.lag);
            m.last=s.sample;++m.unique;const auto date=s.sample/static_cast<double>(rate),error=s.frequency_hz-truth(date);const bool escape=std::abs(error)>s.uncertainty_hz;
            if(m.first<0)m.first=date;m.last_date=date;m.max_error=std::max(m.max_error,std::abs(error));m.max_ratio=std::max(m.max_ratio,std::abs(error)/std::max(s.uncertainty_hz,1e-30));
            if(date<2)m.startup_escapes+=escape;else if(date>=44)m.loss_escapes+=escape;else{++m.covered;m.escapes+=escape;m.final_u=s.uncertainty_hz;m.final_error=error;}
            if(escape)std::cout<<"process escape CN0="<<cn0<<" seed="<<seed<<" drift="<<drift<<" q="<<(variant?process_q:0)<<" date="<<date<<" error="<<error<<" U="<<s.uncertainty_hz<<" first="<<s.first_sample/static_cast<double>(rate)<<" model_resets="<<s.continuous_model_resets<<'\n';
            if(s.sample>=rate*2/5){const auto at=s.sample-rate*2/5;const auto old=s.frequency_at(at);if(old.valid){++m.dated;const auto past_error=old.frequency_hz-truth(at/static_cast<double>(rate));const auto past_escape=std::abs(past_error)>old.uncertainty_hz;m.dated_escapes+=past_escape;if(past_escape)std::cout<<"process dated escape CN0="<<cn0<<" seed="<<seed<<" drift="<<drift<<" q="<<(variant?process_q:0)<<" query="<<at/static_cast<double>(rate)<<" current="<<date<<" error="<<past_error<<" U="<<old.uncertainty_hz<<'\n';}}
        }
    }
    check(baseline.workspace_bytes()==tracker.workspace_bytes(),"process model grew tracker storage");
    for(unsigned variant=0;variant<2;++variant){auto& m=metrics[variant];m.final=owners[variant]->snapshot();m.loss_failures=m.final.qualified||m.final.present||!m.final.frequency_knowledge_valid;if(positive_summary_only&&!variant)continue;
        std::cout<<"process coverage CN0="<<cn0<<" seed="<<seed<<" drift="<<drift<<" q="<<(variant?process_q:0)<<" qualified="<<m.qualified<<" polls="<<m.polls<<" unique="<<m.unique<<" first="<<m.first<<" last="<<m.last_date<<" covered="<<m.covered<<" escapes="<<m.escapes<<" startup_escapes="<<m.startup_escapes<<" loss_escapes="<<m.loss_escapes<<" dated="<<m.dated<<" dated_escapes="<<m.dated_escapes<<" max_error="<<m.max_error<<" max_ratio="<<m.max_ratio<<" finalU="<<m.final_u<<" final_error="<<m.final_error<<" model_resets="<<m.final.continuous_model_resets<<" phase_resets="<<m.final.continuous_phase_resets<<" final_qualified="<<m.final.qualified<<" final_present="<<m.final.present<<" final_knowledge="<<m.final.frequency_knowledge_valid<<" loss_failure="<<m.loss_failures<<" CPU="<<m.cpu<<" bytes="<<owners[variant]->workspace_bytes()<<'\n';
        const auto& lag=m.lag;
        std::cout<<"fixed-lag coverage CN0="<<cn0<<" seed="<<seed<<" drift="<<drift<<" q="<<(variant?process_q:0)
            <<" queries="<<lag.queries<<" eligible="<<lag.eligible<<" valid="<<lag.valid<<" smoothed="<<lag.smoothed
            <<" narrower="<<lag.narrower<<" escapes="<<lag.escapes<<" startup_escapes="<<lag.startup_escapes
            <<" loss_escapes="<<lag.loss_escapes<<" max_error="<<lag.max_error<<" max_ratio="<<lag.max_ratio
            <<" last_U="<<lag.last_u<<" last_backward_U="<<lag.last_backward_u<<" CPU="<<lag.cpu<<'\n';
    }
    metrics[1].loss_failures+=metrics[0].loss_failures;return metrics[1];
}
std::size_t positive_frequency_process_ensemble() {
    std::size_t estimates=0,escapes=0,dated=0,dated_escapes=0,loss_failures=0;
    std::size_t lag_eligible=0,lag_valid=0,lag_smoothed=0,lag_narrower=0,lag_escapes=0;double cpu=0,lag_cpu=0;
    for(const auto cn0:{30.,40.})for(unsigned seed=49172;seed<49176;++seed)for(const bool drift:{false,true}){
        const auto m=positive_process_pair(cn0,seed,drift);estimates+=m.covered;escapes+=m.escapes;dated+=m.dated;dated_escapes+=m.dated_escapes;cpu+=m.cpu;loss_failures+=m.loss_failures;
        check(m.covered>0&&m.dated>0&&m.qualified>m.polls*3/4,"positive process lost useful acquisition availability");
        lag_eligible+=m.lag.eligible;lag_valid+=m.lag.valid;lag_smoothed+=m.lag.smoothed;
        lag_narrower+=m.lag.narrower;lag_escapes+=m.lag.escapes;lag_cpu+=m.lag.cpu;
        check(m.lag.valid && m.lag.smoothed && m.lag.narrower,"positive process never supplied useful actual fixed-lag refinement");
    }
    std::cout<<"process ensemble estimates="<<estimates<<" escapes="<<escapes<<" dated="<<dated<<" dated_escapes="<<dated_escapes<<" CPU="<<cpu<<" loss_failures="<<loss_failures<<" calibrated_confidence=0\n";
    // Empirical regression sentinels, not a calibrated confidence claim.
    // Current estimates exclude the first two seconds. Backward coverage
    // retains every valid query, including startup; no escape is hidden.
    check(estimates>0 && escapes<=estimates/100,"positive process current empirical coverage fell below99percent");
    check(dated>0 && dated_escapes<=dated/100,"positive process backward empirical coverage fell below99percent");
    std::cout<<"fixed-lag ensemble eligible="<<lag_eligible<<" valid="<<lag_valid<<" smoothed="<<lag_smoothed
        <<" narrower="<<lag_narrower<<" escapes="<<lag_escapes<<" CPU="<<lag_cpu<<" calibrated_confidence=0\n";
    check(lag_valid>=lag_eligible && lag_smoothed>=lag_valid/2 && lag_narrower>=lag_valid/2,
          "fixed-lag refinement lost observed support or its ordinary acquisition savings");
    check(lag_valid && lag_escapes<=lag_valid/100,"positive process fixed-lag empirical coverage fell below99percent");
    return loss_failures;
}
void positive_process_phase_reset_and_zero_slew() {
    for(const double slew:{.5,0.}){
        helper::ToneTrackerConfig c{rate,2000,1500,0,slew,true,true};c.frequency_acceleration_psd_hz2_per_second3=9e-4;
        helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));std::array<float,80> pcm{};double phase=.37;helper::ToneTrackerSnapshot before;bool reset=false;
        for(std::uint64_t n=0;n<8ULL*rate;n+=pcm.size()){
            for(std::size_t i=0;i<pcm.size();++i){const auto t=(n+i)/static_cast<double>(rate);phase+=tau*carrier/rate;pcm[i]=t>=4&&t<4.8?0:static_cast<float>(.6*ramp(t)*std::cos(phase+(t>=4.8?.23*tau:0)));}
            tracker.push(pcm,n);const auto s=tracker.snapshot();if(n+pcm.size()==4ULL*rate)before=s;
            if(s.continuous_phase_resets){reset=true;check(s.frequency_knowledge_valid&&s.frequency_covariance_valid,"positive process phase reset discarded frequency marginal");}
            if(s.qualified&&s.frequency_covariance_valid){const auto product=s.frequency_variance_hz2*s.frequency_rate_variance_hz2_per_second2,cross=s.frequency_rate_covariance_hz2_per_second;check(product-cross*cross>=-64*std::numeric_limits<double>::epsilon()*std::max(product,1e-30),"positive process frequency/rate covariance is indefinite");}
        }
        const auto s=tracker.snapshot();check(before.qualified&&reset&&s.qualified&&s.frequency_continuous_phase,"positive process phase return failed causal recovery");
        check(s.first_sample<=before.first_sample&&s.coherent_aperture_seconds<3.3&&s.phase_fit_first_sample>=static_cast<std::uint64_t>(4.8*rate),"positive process bridged unknown phase or lost dated frequency support");
        check(std::abs(s.frequency_hz-carrier)<=s.uncertainty_hz,"positive process clean return escaped its interval");
        std::cout<<"process phase reset slew="<<slew<<" support="<<s.frequency_support_seconds<<" coherent="<<s.coherent_aperture_seconds<<" phase_first="<<s.phase_fit_first_sample/static_cast<double>(rate)<<" U="<<s.uncertainty_hz<<" resets="<<s.continuous_phase_resets<<'\n';
    }
}

void fixed_lag_freezes_on_phase_loss_and_mapping_revokes_it() {
    helper::ToneTrackerConfig config{rate,2000,1500,0,.5,true,true};
    config.frequency_acceleration_psd_hz2_per_second3=9e-4;
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
    helper::ToneTracker mapped(config,helper::ToneTracker::workspace_required(config));
    std::array<float,80> pcm{};double phase=.37;
    for(std::uint64_t first=0;first<4ULL*rate;first+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i) {
            const auto time=(first+i)/static_cast<double>(rate);phase+=tau*carrier/rate;
            pcm[i]=static_cast<float>(.6*ramp(time)*std::cos(phase));
        }
        tracker.push(pcm,first);mapped.push(pcm,first);
    }
    const auto before=tracker.snapshot();
    const auto query_end=4ULL*rate-rate*2/5,query_first=query_end-pcm.size();
    const auto saved=tracker.frequency_interval(query_first,query_end);
    interval_dates(saved,query_first,query_end,4ULL*rate);
    check(before.qualified && saved.smoothed,"phase-loss fixture never acquired an actual buffered frequency fit");
    bool observed_clear=false;
    constexpr std::uint64_t return_first=rate*24/5;
    for(std::uint64_t first=4ULL*rate;first<rate*26/5;first+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i) {
            const auto sample=first+i;const auto time=sample/static_cast<double>(rate);phase+=tau*carrier/rate;
            pcm[i]=sample<return_first?0:static_cast<float>(.6*ramp(time)*std::cos(phase+.23*tau));
        }
        tracker.push(pcm,first);const auto current=tracker.snapshot();
        if(observed_clear || current.continuous_phase_resets==before.continuous_phase_resets)continue;
        observed_clear=true;
        const auto frozen=tracker.frequency_interval(query_first,query_end);
        interval_dates(frozen,query_first,query_end,current.captured_end_sample);
        check(frozen.smoothed && frozen.frequency_hz==saved.frequency_hz && frozen.uncertainty_hz==saved.uncertainty_hz &&
              frozen.anchor_sample==saved.anchor_sample && frozen.evidence_first_sample==saved.evidence_first_sample &&
              frozen.evidence_end_sample==saved.evidence_end_sample,
              "independent phase clear changed qualified historical frequency evidence");
        check(current.frequency_knowledge_valid && current.frequency_covariance_valid &&
              (!current.phase_fit_qualified || current.phase_fit_first_sample>=return_first),
              "frozen frequency evidence erased knowledge or manufactured a returning phase");
        const auto gap=tracker.frequency_interval(4ULL*rate,4ULL*rate+pcm.size());
        check(!gap.smoothed,"pre-loss numeric node granted smoothed support inside the unobserved gap");
        const auto returning=tracker.frequency_interval(current.captured_end_sample-pcm.size(),current.captured_end_sample);
        check(!returning.smoothed || returning.evidence_first_sample>=return_first,
              "pre-loss numeric node qualified a new independent phase aperture");
        std::cout<<"fixed-lag phase clear query="<<query_first/static_cast<double>(rate)
            <<" captured="<<current.captured_end_sample/static_cast<double>(rate)
            <<" evidence_end="<<frozen.evidence_end_sample/static_cast<double>(rate)
            <<" preserved_U="<<frozen.uncertainty_hz<<" gap_smoothed="<<gap.smoothed<<'\n';
    }
    check(observed_clear,"fixed-lag fixture did not observe an independent phase clear");

    // An explicit RF mapping invalidates the old ring's cross covariance.
    // The compatible mapped frequency marginal keeps its actual dates.
    mapped.begin_pilot_segment(4ULL*rate,2200,1500,true,1.5);
    const auto changed=mapped.snapshot();const auto remapped=mapped.frequency_interval(query_first,query_end);
    check(changed.frequency_knowledge_valid && changed.frequency_covariance_valid &&
          changed.sample==before.sample && changed.first_sample==before.first_sample &&
          changed.frequency_hz==2200+(before.frequency_hz-2000)*1.5 && !changed.phase_fit_qualified &&
          !remapped.smoothed && !remapped.independent_aperture,
          "explicit retune retained raw-record/lag authority or lost its dated compatible frequency marginal");
    if(remapped.valid)interval_dates(remapped,query_first,query_end,4ULL*rate);
    tracker.reset(tracker.snapshot().captured_end_sample);
    check(!tracker.frequency_interval(query_first,query_end).valid && !tracker.snapshot().frequency_knowledge_valid,
          "new capture provider retained old lag prediction or dated oscillator knowledge");
}

void fixed_lag_survives_128_second_numeric_refit() {
    helper::ToneTrackerConfig config{rate,2000,1500,0,.5,true,true};
    config.frequency_acceleration_psd_hz2_per_second3=9e-4;
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
    std::array<float,80> pcm{};double phase=.37;helper::ToneTrackerSnapshot earlier;
    const auto cpu_begin=std::clock();
    for(std::uint64_t first=0;first<132ULL*rate;first+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i) {
            const auto time=(first+i)/static_cast<double>(rate);phase+=tau*carrier/rate;
            pcm[i]=static_cast<float>(.6*ramp(time)*std::cos(phase));
        }
        tracker.push(pcm,first);
        if(first+pcm.size()==127ULL*rate)earlier=tracker.snapshot();
    }
    const auto current=tracker.snapshot();
    const auto end=current.captured_end_sample-rate*2/5,first=end-pcm.size();
    const auto interval=tracker.frequency_interval(first,end);
    const auto backward=backward_interval(current,first,end);
    interval_dates(interval,first,end,current.captured_end_sample);
    check(earlier.qualified && earlier.frequency_continuous_phase && earlier.coherent_aperture_seconds>120 &&
          current.qualified && current.frequency_continuous_phase && current.coherent_aperture_seconds>126 &&
          current.coherent_aperture_seconds<129 && current.first_sample>earlier.first_sample &&
          !current.continuous_phase_resets && !current.continuous_model_resets,
          "long continuous control did not cross the bounded128s numeric refit without a reset");
    check(interval.smoothed && backward.valid && interval.uncertainty_hz<backward.uncertainty_hz &&
          interval.uncertainty_hz<.1 && std::abs(interval.frequency_hz-carrier)<=interval.uncertainty_hz &&
          interval.evidence_first_sample>=current.first_sample,
          "numeric refit lost correct, useful and narrower captured buffered frequency support");
    check(!tracker.frequency_interval(current.captured_end_sample,current.captured_end_sample+1).valid,
          "numeric refit manufactured future frequency evidence");
    std::cout<<"fixed-lag128s refit captured="<<current.captured_end_sample/static_cast<double>(rate)
        <<" query="<<first/static_cast<double>(rate)<<" evidence_first="<<interval.evidence_first_sample/static_cast<double>(rate)
        <<" evidence_end="<<interval.evidence_end_sample/static_cast<double>(rate)
        <<" coherent="<<current.coherent_aperture_seconds<<" error="<<interval.frequency_hz-carrier
        <<" U="<<interval.uncertainty_hz<<" backward_U="<<backward.uncertainty_hz
        <<" CPU="<<static_cast<double>(std::clock()-cpu_begin)/CLOCKS_PER_SEC
        <<" bytes="<<tracker.workspace_bytes()<<" phase_authority_from_query=0\n";
}

void independent_frequency_records_remain_dated_and_disjoint() {
    helper::ToneTrackerConfig config{rate,2000,1500,0,.5,true,true};
    config.frequency_acceleration_psd_hz2_per_second3=9e-4;
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
    std::array<float,80> pcm{};std::uint64_t next=0;double phase=.37;
    const auto push=[&](bool gap,double phase_offset) {
        for(auto& value:pcm) {phase+=tau*carrier/rate;value=gap?0:static_cast<float>(.6*std::cos(phase+phase_offset));}
        tracker.push(pcm,next);next+=pcm.size();
    };
    helper::ToneFrequencyInterval saved;
    while(next<rate && !saved.valid) {
        push(false,0);const auto snapshot=tracker.snapshot();
        if(snapshot.frequency_context_first_sample<snapshot.frequency_context_end_sample) {
            const auto query=tracker.frequency_interval(snapshot.frequency_context_first_sample,snapshot.frequency_context_end_sample);
            if(query.valid && query.independent_aperture)saved=query;
        }
    }
    check(saved.valid && saved.independent_aperture && !saved.smoothed,
          "independent-record fixture failed causal unseeded acquisition");
    interval_dates(saved,saved.first_sample,saved.end_sample,next);
    check(std::abs(saved.frequency_hz-carrier)<=saved.uncertainty_hz &&
          saved.uncertainty_hz>=config.maximum_tracking_slew_hz_per_second*
              (saved.evidence_end_sample-saved.evidence_first_sample)/rate,
          "independent record omitted truth or its whole-support slew allowance");
    // The first accepted record precedes continuous qualification. A short
    // missing interval and new phase then prevent a global/lag model from
    // supplying the boundary query; the older disjoint record must survive.
    push(true,0);
    helper::ToneTrackerSnapshot later;
    while(next<saved.captured_end_sample+rate) {
        push(false,.23*tau);later=tracker.snapshot();
        if(later.frequency_context_first_sample>=saved.evidence_end_sample &&
           later.frequency_context_end_sample>saved.evidence_end_sample)break;
    }
    const auto latest=tracker.frequency_interval(later.frequency_context_first_sample,later.frequency_context_end_sample);
    check(latest.valid && latest.independent_aperture &&
          latest.evidence_first_sample>=saved.evidence_end_sample && latest.evidence_end_sample>saved.evidence_end_sample,
          "independent-record fixture did not retain another accepted context after the phase gap");
    interval_dates(latest,latest.first_sample,latest.end_sample,next);
    const auto fine_before=tracker.snapshot();
    const auto retained=tracker.frequency_interval(saved.first_sample,saved.end_sample);
    interval_dates(retained,saved.first_sample,saved.end_sample,next);
    check(retained.independent_aperture && !retained.smoothed && retained.frequency_hz==saved.frequency_hz &&
          retained.uncertainty_hz==saved.uncertainty_hz && retained.anchor_sample==saved.anchor_sample &&
          retained.evidence_first_sample==saved.evidence_first_sample && retained.evidence_end_sample==saved.evidence_end_sample,
          "new context replaced or redated an older independent frequency record");
    const auto cross_first=saved.evidence_end_sample-1,cross_end=saved.evidence_end_sample+1;
    check(!backward_interval(fine_before,cross_first,cross_end).valid &&
          !tracker.frequency_interval(cross_first,cross_end).valid &&
          !tracker.frequency_interval(next,next+1).valid,
          "independent frequency lookup joined records or extrapolated beyond observed support");
    const auto fine_after=tracker.snapshot();
    check(fine_after.sample==fine_before.sample && fine_after.first_sample==fine_before.first_sample &&
          fine_after.frequency_hz==fine_before.frequency_hz && fine_after.uncertainty_hz==fine_before.uncertainty_hz &&
          fine_after.qualified==fine_before.qualified && fine_after.captured_end_sample==fine_before.captured_end_sample &&
          fine_after.phase_fit_qualified==fine_before.phase_fit_qualified &&
          fine_after.phase_fit_sample==fine_before.phase_fit_sample && fine_after.phase_fit_cycles==fine_before.phase_fit_cycles,
          "independent-record query altered fine frequency, capture dates or phase authority");
    std::cout<<"independent record history first="<<saved.evidence_first_sample
        <<" end="<<saved.evidence_end_sample<<" later_first="<<latest.evidence_first_sample
        <<" later_end="<<latest.evidence_end_sample<<" captured="<<next
        <<" U="<<retained.uncertainty_hz<<" cross_record_declined=1 phase_authority_from_record=0\n";

    for(const auto frequency:{190.,rate/2.-190.}) {
        helper::ToneTrackerConfig margin{rate,frequency,100,0,.5,true,true};
        margin.frequency_acceleration_psd_hz2_per_second3=9e-4;
        helper::ToneTracker outside(margin,helper::ToneTracker::workspace_required(margin));double carrier_phase=.37;
        bool accepted_context=false;
        for(std::uint64_t first=0;first<rate;first+=pcm.size()) {
            for(auto& value:pcm) {carrier_phase+=tau*frequency/rate;value=static_cast<float>(.6*std::cos(carrier_phase));}
            outside.push(pcm,first);const auto snapshot=outside.snapshot();
            if(snapshot.frequency_context_first_sample<snapshot.frequency_context_end_sample) {
                accepted_context=true;
                check(!outside.frequency_interval(snapshot.frequency_context_first_sample,snapshot.frequency_context_end_sample).valid,
                      "independent raw frequency record bypassed the physical200Hz capture margin");
            }
            check(!snapshot.qualified,"out-of-margin tone acquired ordinary reference authority");
        }
        check(accepted_context,"capture-margin control never exercised an accepted numeric frequency context");
    }
}


void independent_frequency_veto_reports_model_rejection_but_phase_jump_does_not() {
    // Ordinary discovery consumes8192 samples before independent200ms rows.
    // Place the step on that ACTUAL context lattice, without a frequency seed.
    // Continuous fitting is disabled so its CUSUM cannot supply the counter.
    constexpr unsigned raw_rate=48000,chunk=960,context=9600;
    constexpr std::uint64_t event=8192+10*context,count=raw_rate*16/5;
    constexpr double expected=2400,before_hz=5400;
    bool frequency_veto_reported=false;
    for(const bool phase_control:{false,true}) {
        helper::ToneTrackerConfig config{raw_rate,expected,9000,0,.5,true,false};
        helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
        std::mt19937 random{49271};std::normal_distribution<double> noise;
        std::array<float,chunk> pcm{};
        helper::ToneTrackerSnapshot prior,final;
        helper::ToneFrequencyInterval replacement;
        std::uint64_t first_qualified=0,first_counter_capture=0,replacement_capture=0,recovery_capture=0;
        std::size_t qualified_contexts_before=0,escapes=0;bool phase_fallback=false;
        for(std::uint64_t first=0;first<count;first+=chunk) {
            for(std::size_t i=0;i<pcm.size();++i) {
                const auto sample=first+i;const auto time=static_cast<double>(sample)/raw_rate;
                const auto after=std::max(0.,static_cast<double>(sample)-event)/raw_rate;
                // The+.7Hz arm remains phase-continuous. The other arm has
                // constant frequency and one phase jump INSIDE a context.
                const auto angle=.37+tau*(before_hz*time+(phase_control?0:.7*after))+
                    (phase_control&&sample>=event+context/2?1.1:0.);
                pcm[i]=static_cast<float>(.6*std::cos(angle)+.001*noise(random));
            }
            tracker.push(pcm,first);const auto snapshot=tracker.snapshot();final=snapshot;
            check(snapshot.captured_end_sample==first+chunk,"frequency-veto fixture changed actual ingress dates");
            if(snapshot.qualified&&!first_qualified)first_qualified=first+chunk;
            if(snapshot.qualified&&first+chunk<=event) {
                prior=snapshot;
                if((first+chunk)%context==0)++qualified_contexts_before;
            }
            if(snapshot.continuous_model_resets&&!first_counter_capture)first_counter_capture=first+chunk;
            if(first+chunk>=event+context) {
                const auto interval=tracker.frequency_interval(event,event+context);
                if(interval.valid) {
                    interval_dates(interval,event,event+context,snapshot.captured_end_sample);
                    const auto truth=before_hz+(phase_control?0:.7);
                    escapes+=std::abs(interval.frequency_hz-truth)>interval.uncertainty_hz+1e-7;
                    if(!replacement_capture){replacement=interval;replacement_capture=first+chunk;}
                }
                if(snapshot.qualified&&!recovery_capture&&snapshot.first_sample>=event)recovery_capture=first+chunk;
            }
            if(phase_control&&snapshot.frequency_used_fallback&&snapshot.frequency_context_first_sample<=event+context/2&&
               event+context/2<snapshot.frequency_context_end_sample)phase_fallback=true;
            const auto after_query=tracker.snapshot();
            check(after_query.captured_end_sample==snapshot.captured_end_sample&&
                  after_query.phase_fit_qualified==snapshot.phase_fit_qualified&&
                  after_query.phase_fit_first_sample==snapshot.phase_fit_first_sample&&
                  after_query.phase_fit_end_sample==snapshot.phase_fit_end_sample&&
                  after_query.phase_fit_sample==snapshot.phase_fit_sample&&after_query.phase_fit_cycles==snapshot.phase_fit_cycles,
                  "frequency-veto interval query advanced capture or manufactured phase authority");
        }
        check(first_qualified&&prior.qualified&&qualified_contexts_before>=4,
              "frequency-veto fixture did not causally acquire four qualified pre-event contexts");
        check(replacement_capture&&replacement.first_sample==event&&replacement.end_sample==event+context&&
              replacement.evidence_end_sample<=replacement_capture&&escapes==0,
              "frequency-veto replacement lost dated support or escaped its interval");
        check(!tracker.frequency_interval(final.captured_end_sample,final.captured_end_sample+1).valid,
              "frequency-veto replacement manufactured future frequency evidence");
        const auto retained=tracker.frequency_interval(prior.first_sample,prior.sample+1);
        if(phase_control) {
            // Compatible older frequency evidence remains useful across an
            // unknown phase. The gap fit must not act as a frequency rejection.
            check(phase_fallback&&final.qualified&&final.frequency_knowledge_valid&&final.frequency_covariance_valid&&
                  !final.continuous_model_resets&&retained.valid&&retained.evidence_first_sample<event,
                  "inside-context phase jump revoked compatible frequency knowledge");
        } else {
            check(replacement.evidence_first_sample>=event&&recovery_capture&&!retained.valid,
                  "independent frequency contradiction reused rejected pre-event support");
            frequency_veto_reported=final.continuous_model_resets==1&&
                final.continuous_last_model_reset_sample==event-1&&first_counter_capture>=event+context;
        }
        std::cout<<"independent frequency veto phase_control="<<phase_control
            <<" counter="<<final.continuous_model_resets<<" inclusive_boundary="<<final.continuous_last_model_reset_sample
            <<" replacement_capture="<<replacement_capture<<" evidence_first="<<replacement.evidence_first_sample
            <<" recovery_capture="<<recovery_capture<<" escapes="<<escapes<<" truth_seed=0\n";
    }
    check(frequency_veto_reported,
          "independent frequency contradiction did not publish exactly one model-rejection counter and boundary");
}

}
int main(){try{independent_frequency_veto_reports_model_rejection_but_phase_jump_does_not();continuous_phase_survives_ramps_and_exceeds_old_history();model_change_restarts_numeric_fit_and_loss_withdraws();long_observation_gap_is_callback_independent();unknown_phase_return_keeps_frequency_marginal();local_frequency_does_not_require_phase_correction();frequency_process_dense_gaussian_oracle();const auto loss_failures=positive_frequency_process_ensemble();positive_process_phase_reset_and_zero_slew();fixed_lag_freezes_on_phase_loss_and_mapping_revokes_it();fixed_lag_survives_128_second_numeric_refit();independent_frequency_records_remain_dated_and_disjoint();check(loss_failures==0,"process model retained loss authority or discarded dated knowledge");return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
