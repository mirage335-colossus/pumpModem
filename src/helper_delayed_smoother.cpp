#include "datapump/helper_delayed_smoother.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace datapump::helper {
namespace {
bool finite(double x){return std::isfinite(x);}
bool near(double x,double y){return std::abs(x-y)<=1e-7;}
bool nonnegative(double x){return finite(x)&&x>=0;}
}
HelperDelayedSmoother::HelperDelayedSmoother(HelperDelayedSmootherConfig c):config_(c){
    if(!finite(c.delay_seconds)||c.delay_seconds<0||!finite(c.maximum_delay_seconds)||
       c.maximum_delay_seconds<0||c.maximum_delay_seconds>10||c.delay_seconds>c.maximum_delay_seconds||
       !finite(c.high_pilot_hz)||c.high_pilot_hz<=0||!finite(c.maximum_pilot_to_output_hz)||
       c.maximum_pilot_to_output_hz<0||c.maximum_pilot_to_output_hz>c.high_pilot_hz)
        throw std::invalid_argument("invalid delayed Helper smoother geometry");
}
HelperDelayedSmootherResult HelperDelayedSmoother::assess(const HelperDelayedSmootherInput& x){
    HelperDelayedSmootherResult out;
    const std::array<double,10> vals{x.capture_first_seconds,x.capture_end_seconds,x.support_first_seconds,
        x.support_end_seconds,x.anchor_seconds,x.phase_first_seconds,x.phase_end_seconds,
        x.capture_frontier_seconds,x.latest_observation_end_seconds,x.retained_reference_seconds};
    for(double v:vals)if(!finite(v))return out;
    if(x.capture_first_seconds<0||x.capture_end_seconds<=x.capture_first_seconds||
       x.capture_frontier_seconds<x.capture_end_seconds||x.latest_observation_end_seconds<0||
       x.retained_reference_seconds<0)return out;
    if(have_last_&&x.capture_first_seconds<last_capture_end_-1e-8){out.failure=HelperDelayedSmootherFailure::order;return out;}
    if(x.capture_frontier_seconds+1e-8<x.capture_end_seconds+config_.delay_seconds&&!x.end_of_stream){
        out.failure=HelperDelayedSmootherFailure::not_ready;return out;
    }
    out.ready=true;
    out.partial_drain=x.end_of_stream&&x.capture_frontier_seconds+1e-8<x.capture_end_seconds+config_.delay_seconds;
    // Whether accepted or rejected, do not silently revisit/overlap a hop.
    have_last_=true;last_capture_end_=x.capture_end_seconds;
    // Latch the tracker's already-established reference even when this hop
    // itself is missing or the current model is unavailable. Otherwise a
    // dropout before the first released valid phase could silently rebase it.
    if(x.anchor.reference_initialized){
        if(!finite(x.anchor.reference_input_seconds)||
           x.anchor.reference_input_seconds!=x.retained_reference_seconds||
           (have_reference_&&reference_!=x.retained_reference_seconds)){
            out.failure=HelperDelayedSmootherFailure::reference_mismatch;return out;
        }
        if(!have_reference_){reference_=x.retained_reference_seconds;have_reference_=true;}
    }else if(have_reference_&&reference_!=x.retained_reference_seconds){
        out.failure=HelperDelayedSmootherFailure::reference_mismatch;return out;
    }
    if(x.latest_observation_end_seconds>x.capture_frontier_seconds+1e-8){out.failure=HelperDelayedSmootherFailure::future_observation;return out;}
    if(x.capture_frontier_seconds-x.capture_end_seconds>config_.maximum_delay_seconds+1e-7){out.failure=HelperDelayedSmootherFailure::future_observation;return out;}
    if(!x.fresh_phase_valid||!x.target_observation_qualified){out.failure=HelperDelayedSmootherFailure::missing_phase;return out;}
    if(x.support_first_seconds<x.capture_first_seconds||x.support_end_seconds>x.capture_end_seconds||
       x.support_first_seconds>=x.support_end_seconds||x.anchor_seconds<x.support_first_seconds||
       x.anchor_seconds>=x.support_end_seconds||x.phase_first_seconds<x.capture_first_seconds||
       x.phase_end_seconds>x.capture_end_seconds||x.phase_first_seconds>=x.phase_end_seconds)return out;
    if(x.phase_end_seconds>x.capture_frontier_seconds+1e-8){out.failure=HelperDelayedSmootherFailure::future_observation;return out;}
    if(x.latest_observation_end_seconds+1e-8<x.phase_end_seconds){out.failure=HelperDelayedSmootherFailure::missing_phase;return out;}
    const std::array<const HelperJointEstimator::Estimate*,3> e{&x.support_first,&x.anchor,&x.support_end};
    const std::array<double,3> dates{x.support_first_seconds,x.anchor_seconds,x.support_end_seconds};
    for(std::size_t i=0;i<e.size();++i){const auto& p=*e[i];
        if(!p.valid||!p.reference_initialized||!p.reference_preserved||!p.model_consistent||
           p.ambiguity_limited||p.work_limited||p.post_gap_qualification_pending||p.alternatives!=1||
           p.frequency_step_active||!near(p.time_seconds,dates[i])){out.failure=HelperDelayedSmootherFailure::unavailable_model;return out;}
        if(!finite(p.reference_input_seconds)||p.reference_input_seconds!=x.retained_reference_seconds||
           (have_reference_&&reference_!=p.reference_input_seconds)){out.failure=HelperDelayedSmootherFailure::reference_mismatch;return out;}
        if(!finite(p.audio_scale)||p.audio_scale<=0||!finite(p.frequency_hz)||!finite(p.source_seconds)||
           !nonnegative(p.source_stochastic_sigma_seconds)||!nonnegative(p.source_model_half_width_seconds)||
           !nonnegative(p.audio_stochastic_sigma_ppm)||!nonnegative(p.audio_model_half_width_ppm)||
           !nonnegative(p.conditional_half_width_hz)){out.failure=HelperDelayedSmootherFailure::invalid_input;return out;}
        if(i&&(p.accepted_observations!=e[0]->accepted_observations||
               p.rejected_observations!=e[0]->rejected_observations)){
            out.failure=HelperDelayedSmootherFailure::conflicting_history;return out;
        }
    }
    const auto& a=x.anchor;
    double frequency_radius=0,audio_radius=0;
    for(const auto* p:e){
        // Endpoints and anchor are the same fit, not independent evidence.
        // L1 enclosing the fitted trajectory, the model radius and the
        // stochastic radii deliberately makes no cross-covariance assumption.
        frequency_radius=std::max(frequency_radius,std::abs(p->frequency_hz-a.frequency_hz)+p->conditional_half_width_hz);
        audio_radius=std::max(audio_radius,std::abs(p->audio_scale-a.audio_scale)+
            1e-6*(3*p->audio_stochastic_sigma_ppm+p->audio_model_half_width_ppm));
    }
    if(!have_reference_){reference_=x.retained_reference_seconds;have_reference_=true;}
    out.reference_seconds=reference_;out.audio_scale=a.audio_scale;
    out.common_frequency_offset_hz=a.frequency_hz-config_.high_pilot_hz*a.audio_scale;
    out.source_seconds_at_anchor=a.source_seconds;
    out.source_half_width_seconds=3*a.source_stochastic_sigma_seconds+a.source_model_half_width_seconds;
    out.audio_half_width_ppm=3*a.audio_stochastic_sigma_ppm+a.audio_model_half_width_ppm;
    out.transport_rate_radians_per_second=2*std::numbers::pi*(frequency_radius+config_.maximum_pilot_to_output_hz*audio_radius);
    out.support_first_seconds=x.support_first_seconds;out.support_end_seconds=x.support_end_seconds;
    if(!finite(out.common_frequency_offset_hz)||!nonnegative(out.source_half_width_seconds)||
       !nonnegative(out.audio_half_width_ppm)||!nonnegative(out.transport_rate_radians_per_second))return out;
    out.valid=true;out.failure=HelperDelayedSmootherFailure::none;return out;
}
}
