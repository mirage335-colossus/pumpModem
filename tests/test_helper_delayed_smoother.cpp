#include "datapump/helper_delayed_smoother.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace datapump::helper;
namespace {
void require(bool x,const char* s){if(!x)throw std::runtime_error(s);}
HelperJointEstimator::Estimate estimate(double t,double reference=.2){
    HelperJointEstimator::Estimate e;
    e.valid=true;e.reference_initialized=true;e.reference_preserved=true;e.model_consistent=true;
    e.alternatives=1;e.accepted_observations=90;e.rejected_observations=2;e.reference_input_seconds=reference;
    e.time_seconds=t;e.audio_scale=1.00003;e.frequency_hz=2400.25+.001*(t-38.2);
    e.source_seconds=t+(.00003*(t-reference));e.source_stochastic_sigma_seconds=1e-6;e.source_model_half_width_seconds=2e-6;
    e.audio_stochastic_sigma_ppm=.1;e.audio_model_half_width_ppm=.05;e.conditional_half_width_hz=.12;
    return e;
}
HelperDelayedSmootherInput input(){
    HelperDelayedSmootherInput x;x.fresh_phase_valid=true;x.target_observation_qualified=true;
    x.capture_first_seconds=38;x.capture_end_seconds=38.4;x.support_first_seconds=38.06;x.support_end_seconds=38.34;
    x.anchor_seconds=38.2;x.phase_first_seconds=38.02;x.phase_end_seconds=38.38;
    x.capture_frontier_seconds=40.4;x.latest_observation_end_seconds=40.38;x.retained_reference_seconds=.2;
    x.support_first=estimate(x.support_first_seconds);x.anchor=estimate(x.anchor_seconds);x.support_end=estimate(x.support_end_seconds);
    return x;
}
}
int main(){try{
    HelperDelayedSmoother d;
    auto early=input();early.capture_frontier_seconds=40.0;early.latest_observation_end_seconds=39.98;
    require(d.assess(early).failure==HelperDelayedSmootherFailure::not_ready,"released before configured delay");
    const auto x=input();const auto r=d.assess(x);
    require(r.ready&&r.valid&&!r.partial_drain&&r.reference_seconds==.2,"valid retrospective fit rejected");
    require(std::abs(r.audio_scale-1.00003)<1e-12&&std::abs(r.common_frequency_offset_hz-(2400.25-2400*1.00003))<1e-9,
        "wrong high-pilot frequency/audio coordinate");
    require(r.support_first_seconds==x.support_first_seconds&&r.support_end_seconds==x.support_end_seconds&&
        std::abs(r.source_half_width_seconds-5e-6)<1e-15&&r.audio_half_width_ppm>.34&&r.transport_rate_radians_per_second>.75,
        "support or uncertainty transport was reduced");
    require(d.assess(x).failure==HelperDelayedSmootherFailure::order,"replayed target permitted");
    auto later=x;later.capture_first_seconds=38.4;later.capture_end_seconds=38.8;
    later.support_first_seconds=38.46;later.support_end_seconds=38.74;later.anchor_seconds=38.6;
    later.phase_first_seconds=38.42;later.phase_end_seconds=38.78;later.capture_frontier_seconds=40.8;
    later.latest_observation_end_seconds=40.78;later.retained_reference_seconds=.3;
    later.support_first=estimate(later.support_first_seconds,.3);later.anchor=estimate(later.anchor_seconds,.3);later.support_end=estimate(later.support_end_seconds,.3);
    require(d.assess(later).failure==HelperDelayedSmootherFailure::reference_mismatch,"gap silently selected new reference");
    later.capture_first_seconds=38.8;later.capture_end_seconds=39.2;later.support_first_seconds=38.86;later.support_end_seconds=39.14;
    later.anchor_seconds=39;later.phase_first_seconds=38.82;later.phase_end_seconds=39.18;later.capture_frontier_seconds=41.2;
    later.latest_observation_end_seconds=41.18;later.retained_reference_seconds=.2;
    later.support_first=estimate(later.support_first_seconds);later.anchor=estimate(later.anchor_seconds);later.support_end=estimate(later.support_end_seconds);
    require(d.assess(later).valid,"valid original reference did not recover after contradiction");
    {HelperDelayedSmoother s;auto a=x;a.latest_observation_end_seconds=40.41;
        require(s.assess(a).failure==HelperDelayedSmootherFailure::future_observation,"future sample leaked past frontier");}
    {HelperDelayedSmoother s;auto a=x;a.fresh_phase_valid=false;
        require(s.assess(a).failure==HelperDelayedSmootherFailure::missing_phase,"missing hop borrowed another phase");
        auto b=later;b.retained_reference_seconds=.3;b.anchor.reference_input_seconds=.3;
        require(s.assess(b).failure==HelperDelayedSmootherFailure::reference_mismatch,
            "reference changed after initial missing hop");}
    {HelperDelayedSmoother s;auto a=x;a.anchor.post_gap_qualification_pending=true;
        require(s.assess(a).failure==HelperDelayedSmootherFailure::unavailable_model,"ambiguous gap admitted");}
    {HelperDelayedSmoother s;auto a=x;a.support_first.frequency_step_active=true;
        require(s.assess(a).failure==HelperDelayedSmootherFailure::unavailable_model,"unhandled frequency epoch admitted");}
    {HelperDelayedSmoother s;auto a=x;a.support_end.accepted_observations++;
        require(s.assess(a).failure==HelperDelayedSmootherFailure::conflicting_history,"mixed estimator snapshots admitted");}
    {HelperDelayedSmoother s;auto a=x;a.anchor.time_seconds+=.01;
        require(s.assess(a).failure==HelperDelayedSmootherFailure::unavailable_model,"misdated estimate admitted");}
    {HelperDelayedSmoother s;auto a=x;a.anchor.source_stochastic_sigma_seconds=std::numeric_limits<double>::infinity();
        require(!s.assess(a).valid,"nonfinite uncertainty admitted");}
    {HelperDelayedSmoother s;auto a=early;a.end_of_stream=true;
        const auto q=s.assess(a);require(q.valid&&q.ready&&q.partial_drain,"EOF partial drain lost");}
    {HelperDelayedSmoother s;auto a=x;a.capture_frontier_seconds=42;
        require(s.assess(a).failure==HelperDelayedSmootherFailure::future_observation,"unbounded lookahead accepted");}
    for(double delay:{.4,1.,2.,3.}){HelperDelayedSmootherConfig c;c.delay_seconds=delay;
        HelperDelayedSmoother s(c);auto a=x;a.capture_frontier_seconds=a.capture_end_seconds+delay;
        a.latest_observation_end_seconds=a.capture_frontier_seconds-.02;
        require(s.assess(a).valid,"legal configured delay rejected");}
    bool rejected=false;try{HelperDelayedSmootherConfig c;c.delay_seconds=11;HelperDelayedSmoother s(c);(void)s;}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"unbounded configuration accepted");
    std::cout<<"helper_delayed_smoother short checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
