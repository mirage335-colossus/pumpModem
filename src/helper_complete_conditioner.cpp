#include "datapump/helper_complete_conditioner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace datapump::helper {
namespace {
constexpr long double tau=2*std::numbers::pi_v<long double>;
constexpr std::size_t maximum_epochs_per_block=64;
bool finite_nonnegative(long double x){return std::isfinite(x)&&x>=0;}
bool usable(const HelperCompleteConditioner::CorrectionEpoch& e,std::uint32_t fs,std::uint64_t frontier,std::uint64_t helper_frontier){
    const long double first_audio=e.audio_scale+e.audio_drift_ppm_per_second*1e-6L*
        (static_cast<long double>(e.first_sample)-e.anchor_input_sample)/fs;
    const long double end_audio=e.audio_scale+e.audio_drift_ppm_per_second*1e-6L*
        (static_cast<long double>(e.end_sample)-e.anchor_input_sample)/fs;
    return e.first_sample<e.end_sample&&std::isfinite(e.anchor_input_sample)&&
        std::isfinite(e.audio_scale)&&std::abs(e.audio_scale-1)<=.001&&
        std::isfinite(e.audio_drift_ppm_per_second)&&std::isfinite(first_audio)&&std::isfinite(end_audio)&&
        std::abs(first_audio-1)<=.001L&&std::abs(end_audio-1)<=.001L&&
        std::isfinite(e.common_frequency_hz)&&std::abs(e.common_frequency_hz)<fs/2.&&
        std::isfinite(e.pilot_frequency_hz)&&std::abs(e.pilot_frequency_hz)<fs/2.&&
        std::isfinite(e.measured_pilot_phase_radians)&&finite_nonnegative(e.boundary_half_width_samples)&&
        finite_nonnegative(e.phase_sigma_radians)&&finite_nonnegative(e.frequency_half_width_hz)&&
        finite_nonnegative(e.audio_half_width_ppm)&&finite_nonnegative(e.latest_helper_observation_end_sample)&&
        e.latest_helper_observation_end_sample<=frontier&&
        (!(e.phase_valid||e.clock_valid||e.source_time_valid)||
            (e.latest_helper_observation_monotonic_nanoseconds&&e.latest_helper_observation_monotonic_nanoseconds<=helper_frontier))&&
        (!e.source_time_valid||(std::isfinite(e.source_seconds_at_anchor)&&
            finite_nonnegative(e.source_half_width_seconds)&&finite_nonnegative(e.reference_input_sample)));
}
}
HelperCompleteConditioner::HelperCompleteConditioner(Config c)
    :config_(c),buffer_({c.maximum_samples,c.maximum_blocks,c.delay_nanoseconds,c.clock_jump_tolerance_nanoseconds}),
     raw_(c.maximum_samples){
    if(c.sample_rate<64||c.sample_rate>120'000'000)throw std::invalid_argument("invalid complete Helper sample rate");
}
HelperDelayedBuffer::PushResult HelperCompleteConditioner::push(const CaptureBlock& b,std::span<const Sample> data){
    if(drained_)return HelperDelayedBuffer::PushResult::invalid;
    for(const auto z:data)if(!std::isfinite(z.real())||!std::isfinite(z.imag()))return HelperDelayedBuffer::PushResult::invalid;
    const auto result=buffer_.push(b,data);
    if(result==HelperDelayedBuffer::PushResult::accepted||result==HelperDelayedBuffer::PushResult::reference_mismatch)
        capture_frontier_input_=b.first_sample+static_cast<std::uint64_t>(b.sample_count);
    return result;
}
std::size_t HelperCompleteConditioner::allocated_bytes()const noexcept {
    return sizeof(*this)+buffer_.allocated_bytes()+raw_.capacity()*sizeof(Sample);
}
void HelperCompleteConditioner::emit(const Point& left,const Point& right,long double fraction,const OutputSink& sink,bool endpoint){
    fraction=std::clamp(fraction,0.L,1.L);
    Event e;
    const auto delta=right.input-left.input;
    e.source_input_sample=static_cast<long double>(left.input)+fraction*delta;
    if(e.source_input_sample<=last_output_input_)return;
    const bool exact_left=fraction==0,exact_right=fraction==1;
    const auto& selected=exact_right?right:left;
    e.sample=exact_left?left.sample:exact_right?right.sample:
        static_cast<Sample>(std::complex<long double>(left.sample)*(1-fraction)+std::complex<long double>(right.sample)*fraction);
    e.output_sample=output_samples_++;e.segment_endpoint=endpoint;
    e.relative_source_seconds=left.clock+fraction*(right.clock-left.clock);
    e.release_monotonic_nanoseconds=std::max({left.release_ns,right.release_ns,active_release_ns_});
    e.capture_clock_epoch=selected.capture.capture_clock_epoch;
    e.capture_time_discontinuity=selected.discontinuity||(!exact_left&&!exact_right&&(left.discontinuity||right.discontinuity));
    e.interpolation_supported=exact_left||exact_right||delta==1;
    long double lt=0,lu=0,rt=0,ru=0;
    const bool lok=HelperDelayedBuffer::capture_system_time_at(left.capture,left.input,lt,lu);
    const bool rok=HelperDelayedBuffer::capture_system_time_at(right.capture,right.input,rt,ru);
    const long double lm=static_cast<long double>(left.capture.capture_monotonic_nanoseconds)+
        (left.input-left.capture.first_sample)*left.capture.sample_period_nanoseconds;
    const long double rm=static_cast<long double>(right.capture.capture_monotonic_nanoseconds)+
        (right.input-right.capture.first_sample)*right.capture.sample_period_nanoseconds;
    // Interpolate real capture anchors only within one explicitly supplied
    // capture-clock epoch. A wall-clock jump never changes release order.
    const bool same_block=left.capture.first_sample==right.capture.first_sample;
    const auto period_error=left.capture.sample_period_uncertainty_nanoseconds+right.capture.sample_period_uncertainty_nanoseconds;
    const auto monotonic_error=left.capture.capture_monotonic_uncertainty_nanoseconds+right.capture.capture_monotonic_uncertainty_nanoseconds;
    const bool anchors_agree=std::abs((rt-lt)-(rm-lm))<=lu+ru+period_error+monotonic_error;
    const bool same_clock=left.capture.capture_clock_epoch==right.capture.capture_clock_epoch&&
        (same_block||(!right.discontinuity&&anchors_agree));
    e.capture_timestamp_valid=exact_left?lok:exact_right?rok:lok&&rok&&same_clock;
    e.capture_system_nanoseconds=lt+fraction*(rt-lt);
    e.capture_monotonic_nanoseconds=lm+fraction*(rm-lm);
    e.capture_uncertainty_nanoseconds=lu+fraction*(ru-lu);
    const auto& ep=selected.epoch;
    const bool use_left=!exact_right,use_right=!exact_left;
    e.phase_applied=(!use_left||(left.epoch_available&&left.epoch.phase_valid))&&
                    (!use_right||(right.epoch_available&&right.epoch.phase_valid));
    e.clock_applied=(!use_left||(left.epoch_available&&left.epoch.clock_valid))&&
                    (!use_right||(right.epoch_available&&right.epoch.clock_valid));
    e.schedule_valid=selected.epoch_available;
    e.schedule_slot=ep.schedule_slot;e.correction_epoch_first_sample=ep.first_sample;
    e.latest_helper_observation_end_sample=std::max(left.epoch.latest_helper_observation_end_sample,
                                                   right.epoch.latest_helper_observation_end_sample);
    e.latest_helper_observation_monotonic_nanoseconds=std::max(left.epoch.latest_helper_observation_monotonic_nanoseconds,
        right.epoch.latest_helper_observation_monotonic_nanoseconds);
    const bool same_epoch=left.epoch_available&&right.epoch_available&&
        left.epoch.first_sample==right.epoch.first_sample&&left.epoch.end_sample==right.epoch.end_sample;
    const auto guarded=[](const Point& p){return p.epoch_available&&p.epoch.model_qualified&&
        p.input>=p.epoch.phase_support_first_sample&&p.input<p.epoch.phase_support_end_sample;};
    const long double boundary_margin=std::max(left.epoch.boundary_half_width_samples,right.epoch.boundary_half_width_samples);
    e.boundary_ambiguous=!e.schedule_valid||
        e.source_input_sample-static_cast<long double>(ep.first_sample)<=boundary_margin||
        static_cast<long double>(ep.end_sample)-e.source_input_sample<=boundary_margin||
        (!exact_left&&!exact_right&&!same_epoch);
    e.reference_mismatch=reference_mismatch_;
    e.transmitter_time_valid=selected.epoch_available&&ep.source_time_valid&&have_reference_&&
        ep.reference_input_sample==reference_&&!reference_mismatch_;
    e.reference_input_sample=have_reference_?reference_:0;
    if(e.transmitter_time_valid){
        const long double offset=e.source_input_sample-ep.anchor_input_sample;
        const long double seconds=offset/config_.sample_rate;
        e.transmitter_seconds=ep.source_seconds_at_anchor+seconds*ep.audio_scale+
            .5L*ep.audio_drift_ppm_per_second*1e-6L*seconds*seconds;
        e.transmitter_uncertainty_seconds=ep.source_half_width_seconds+
            std::abs(offset)/config_.sample_rate*ep.audio_half_width_ppm*1e-6L;
        e.clock_model_discrepancy_valid=true;
        e.clock_model_discrepancy_seconds=e.relative_source_seconds-e.transmitter_seconds;
    }
    const bool model_agrees=!e.clock_model_discrepancy_valid||
        std::abs(e.clock_model_discrepancy_seconds)<=e.transmitter_uncertainty_seconds;
    e.well_supported=e.phase_applied&&e.clock_applied&&e.capture_timestamp_valid&&e.interpolation_supported&&
        (!use_left||guarded(left))&&(!use_right||guarded(right))&&!e.boundary_ambiguous&&
        !e.reference_mismatch&&model_agrees;
    sink(e);last_output_input_=e.source_input_sample;
}
void HelperCompleteConditioner::finish_segment(const OutputSink& sink){
    if(have_previous_)emit(previous_,previous_,0,sink,true);
    have_previous_=false;have_target_=false;
}
long double HelperCompleteConditioner::extrapolated_clock_after_gap(std::uint64_t input)const noexcept {
    if(!have_clock_)return static_cast<long double>(input)/config_.sample_rate;
    const long double dt=static_cast<long double>(input-next_clock_input_)/config_.sample_rate;
    const auto& e=previous_.epoch;
    const long double physical_scale=e.audio_scale+e.audio_drift_ppm_per_second*1e-6L*
        (static_cast<long double>(next_clock_input_)-e.anchor_input_sample)/config_.sample_rate;
    // Retain the clock actually attained at the gap start, then extrapolate
    // oscillator evolution. A temporary correction servo must not continue
    // past its fixed target through samples that were never captured.
    return next_clock_seconds_+dt*physical_scale+.5L*e.audio_drift_ppm_per_second*1e-6L*dt*dt;
}
void HelperCompleteConditioner::emit_gap(std::uint64_t first,std::uint64_t count,const ReleasedBlock& block,const OutputSink& sink){
    if(!count)return;
    Event e;e.samples_present=false;e.missing_first_sample=first;e.missing_sample_count=count;
    e.source_input_sample=first;e.relative_source_seconds=extrapolated_clock_after_gap(first);
    e.release_monotonic_nanoseconds=block.release_monotonic_nanoseconds;
    e.capture_clock_epoch=block.capture.capture_clock_epoch;e.capture_time_discontinuity=block.capture_time_discontinuity;
    e.interpolation_supported=false;e.reference_input_sample=have_reference_?reference_:0;
    e.reference_mismatch=reference_mismatch_;sink(e);missing_samples_+=count;
}
std::size_t HelperCompleteConditioner::release(std::uint64_t now,std::uint64_t frontier,
    const PlanProvider& provider,const OutputSink& sink,bool drain){
    if(!provider||!sink)throw std::invalid_argument("complete Helper callback is empty");
    if(drained_)return 0;
    active_release_ns_=now;
    const auto initial_outputs=output_samples_,initial_missing=missing_samples_;
    std::size_t gap_events=0;
    for(;;){
        ReleasedBlock block;
        const auto status=buffer_.pop(now,frontier,raw_,block,false);
        if(status==HelperDelayedBuffer::PopResult::empty||status==HelperDelayedBuffer::PopResult::not_ready)break;
        if(status!=HelperDelayedBuffer::PopResult::released)throw std::runtime_error("complete Helper release failed");
        if(block.missing_samples_before){
            finish_segment(sink);
            emit_gap(block.capture.first_sample-block.missing_samples_before,block.missing_samples_before,block,sink);++gap_events;
        }
        if(!block.capture.samples_present){
            finish_segment(sink);emit_gap(block.capture.first_sample,block.capture.sample_count,block,sink);++gap_events;
            continue;
        }
        const auto plans=provider(block);
        bool plans_valid=plans.size()<=maximum_epochs_per_block;
        for(std::size_t i=0;plans_valid&&i<plans.size();++i)
            plans_valid=usable(plans[i],config_.sample_rate,capture_frontier_input_,std::min(frontier,now))&&
                (!i||plans[i-1].end_sample<=plans[i].first_sample);
        std::size_t epoch_index=0;
        const CorrectionEpoch* active=nullptr;
        std::complex<double> phasor{1,0},increment{1,0};double active_clock_scale=1;
        for(std::size_t n=0;n<block.capture.sample_count;++n){
            const auto input=block.capture.first_sample+static_cast<std::uint64_t>(n);
            const CorrectionEpoch* epoch=nullptr;
            if(plans_valid){
                while(epoch_index<plans.size()&&plans[epoch_index].end_sample<=input)++epoch_index;
                if(epoch_index<plans.size()&&plans[epoch_index].first_sample<=input)epoch=&plans[epoch_index];
            }
            Point point;point.input=input;point.capture=block.capture;point.release_ns=block.release_monotonic_nanoseconds;
            point.discontinuity=block.capture_time_discontinuity;point.epoch_available=epoch!=nullptr;
            if(epoch)point.epoch=*epoch;
            else {point.epoch.first_sample=block.capture.first_sample;
                point.epoch.end_sample=block.capture.first_sample+block.capture.sample_count;}
            const auto modeled_clock=[&](long double at){
                const long double dt=(at-point.epoch.anchor_input_sample)/config_.sample_rate;
                return point.epoch.source_seconds_at_anchor+dt*point.epoch.audio_scale+
                    .5L*point.epoch.audio_drift_ppm_per_second*1e-6L*dt*dt;
            };
            if(!have_clock_){
                // This is the only initialization, before the first payload
                // leaves the object. It cannot be repeated after a dropout.
                reference_=point.epoch.source_time_valid?point.epoch.reference_input_sample:0;
                have_reference_=true;
                point.clock=point.epoch.source_time_valid?modeled_clock(input):static_cast<long double>(input)/config_.sample_rate;
                have_clock_=true;
            }else if(have_previous_){
                const auto distance=input-previous_.input;
                point.clock=previous_.clock+static_cast<long double>(distance)*previous_.clock_scale/config_.sample_rate;
            }else point.clock=extrapolated_clock_after_gap(input);
            if(point.epoch.source_time_valid&&reference_!=point.epoch.reference_input_sample)reference_mismatch_=true;
            const bool refresh_epoch=epoch!=active||n==0;
            if(refresh_epoch){
                active=epoch;
                active_clock_scale=point.epoch.audio_scale;
                if(point.epoch.source_time_valid&&!reference_mismatch_){
                    // Correct accumulated rate error continuously. A new fit
                    // supplies one desired map, not independent clock evidence.
                    // The epoch end is fixed, so subdividing capture chunks
                    // cannot move the servo target or choose a fresh reference.
                    const auto remaining=static_cast<long double>(point.epoch.end_sample-input);
                    if(remaining>0)active_clock_scale=static_cast<double>(std::clamp(
                        (modeled_clock(point.epoch.end_sample)-point.clock)*config_.sample_rate/remaining,.999L,1.001L));
                }
                long double angle=-tau*point.epoch.common_frequency_hz*
                    (static_cast<long double>(input)-point.epoch.first_sample)/config_.sample_rate;
                if(point.epoch.phase_valid){
                    const long double dt=(static_cast<long double>(input)-point.epoch.anchor_input_sample)/config_.sample_rate;
                    const auto predicted_phase=point.epoch.measured_pilot_phase_radians+
                        tau*(point.epoch.pilot_frequency_hz*point.epoch.audio_scale+point.epoch.common_frequency_hz)*dt+
                        .5L*tau*point.epoch.pilot_frequency_hz*point.epoch.audio_drift_ppm_per_second*1e-6L*dt*dt;
                    angle=tau*point.epoch.pilot_frequency_hz*point.clock-predicted_phase;
                }
                phasor=std::polar(1.,static_cast<double>(std::remainder(angle,tau)));
            }
            point.clock_scale=active_clock_scale;
            if(refresh_epoch||point.epoch.audio_drift_ppm_per_second!=0){
                const long double model_audio=point.epoch.audio_scale+point.epoch.audio_drift_ppm_per_second*1e-6L*
                    (static_cast<long double>(input)-point.epoch.anchor_input_sample+.5L)/config_.sample_rate;
                const auto frequency_correction=point.epoch.phase_valid?
                    point.epoch.common_frequency_hz+point.epoch.pilot_frequency_hz*(model_audio-active_clock_scale):point.epoch.common_frequency_hz;
                increment=std::polar(1.,static_cast<double>(-tau*frequency_correction/config_.sample_rate));
            }
            const auto value=raw_[n];
            if(!std::isfinite(value.real())||!std::isfinite(value.imag()))throw std::runtime_error("nonfinite complete Helper input");
            point.sample=static_cast<Sample>(std::complex<double>(value)*phasor);
            phasor*=increment;
            if((input&255U)==255U)phasor/=std::abs(phasor);
            if(!have_previous_){
                next_target_seconds_=point.clock;have_target_=true;
                emit(point,point,0,sink);next_target_seconds_+=1.L/config_.sample_rate;
            }else{
                const long double interval=point.clock-previous_.clock;
                if(!(interval>0))throw std::runtime_error("nonmonotonic complete Helper source clock");
                // At most two output samples per input interval: supported
                // clock corrections are bounded to 1000 ppm.
                unsigned work=0;
                while(have_target_&&next_target_seconds_<=point.clock){
                    if(++work>2)throw std::runtime_error("complete Helper interpolation work bound");
                    emit(previous_,point,(next_target_seconds_-previous_.clock)/interval,sink);
                    next_target_seconds_+=1.L/config_.sample_rate;
                }
            }
            previous_=point;have_previous_=true;
            next_clock_seconds_=point.clock+point.clock_scale/config_.sample_rate;next_clock_input_=input+1;++processed_samples_;
        }
    }
    if(drain&&!buffer_.queued_blocks()){finish_segment(sink);drained_=true;}
    (void)initial_missing;
    return static_cast<std::size_t>(output_samples_-initial_outputs)+gap_events;
}
}
