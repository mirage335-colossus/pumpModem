// Helper-only v9 incremental complete-output study. Existing v8 fixtures and
// evaluator types are reused; no ordinary Robust or audio hardware is involved.
#define HELPER_DELAYED_STUDY_NO_MAIN
#include "test_helper_delayed_study.cpp"
#undef HELPER_DELAYED_STUDY_NO_MAIN
#include "datapump/helper_complete_conditioner.hpp"
#include "datapump/helper_boundary_tracker.hpp"
#include "datapump/helper_inversion_timing.hpp"
#include "datapump/helper_inversion_profile.hpp"
#include "helper_complete_apsk_metrics.hpp"

namespace {
// Only measurements and their original dates are retained here; captured
// samples and all buffering belong to the bounded complete conditioner.
struct CompletePending {
    unsigned index=0;
    double first_seconds=0, shift_hz=0;
    Phase phase{};
    h::HelperDwellMeasurement local_measurement{};
    bool target_accepted=false, missing=false;
    double reference_seconds=0;
};

struct CompletePlan {
    enum class Origin : unsigned { nominal,qualified_joint,joint_point,joint_translated,broad,local,retained,count };
    bool qualified=false,phase_available=false,frequency_available=false,source_available=false;
    double audio_scale=1,common_frequency_hz=0,audio_drift_ppm_per_second=0;
    double source_seconds_at_anchor=0,source_half_width_seconds=0,audio_half_width_ppm=0;
    double anchor_seconds=0,high_phase_radians=0,transport_rate=0,diffusion_q=0;
    double reference_seconds=0;
    std::uint64_t supported_first=0,supported_end=0;
    Origin origin=Origin::nominal;
    h::HelperDelayedSmootherFailure smoother_failure=h::HelperDelayedSmootherFailure::invalid_input;
};

// One actual Chain observes the raw Helper samples once per captured dwell.
// The later-fit queries below are only predictions of that same history; they
// are never ingested as additional measurements and never include probe data.
struct CompleteHelperHistory {
    Chain chain;
    h::HelperDelayedSmoother smoother;
    Coeff last_coeff{};
    double last_rf=0;
    bool have_last=false;
    bool fixed_reference_chosen=false;
    double fixed_reference_seconds=0;
    bool trace_plans=false;
    std::array<std::uint64_t,static_cast<std::size_t>(CompletePlan::Origin::count)> plan_origins{};
    std::array<std::uint64_t,9> smoother_failures{};
    std::uint64_t post_smoother_fixed_reference_mismatch=0,post_smoother_endpoint_invalid=0,post_smoother_endpoint_reference_mismatch=0;
    explicit CompleteHelperHistory(const h::HelperMeasurementConfig& mc,double rf,double delay)
      :chain("complete-history",h::HelperAidedFrequencyMethod::subaperture_slope,true,true,mc,rf,true),
       smoother({delay,3.4,2400,980}) { chain.receiver.use_joint_dwell=true; }
    CompletePending observe(const hp::Dwell& dwell,bool missing,unsigned index){
        chain.observe(dwell,missing,index);
        CompletePending p;p.index=index;p.first_seconds=dwell.first_seconds;p.shift_hz=dwell.shift_hz;
        p.phase=chain.receiver.phase;p.target_accepted=chain.joint_state.valid&&chain.current_joint_accepted&&!chain.joint_suspended;
        p.local_measurement=chain.receiver.measurement;
        p.reference_seconds=chain.joint_state.reference_input_seconds;p.missing=missing;return p;
    }
    CompletePlan plan(const CompletePending& p,double captured_frontier_seconds,bool eof){
        if(!fixed_reference_chosen){
            const auto& state=chain.joint_state;
            fixed_reference_seconds=state.reference_initialized&&state.reference_preserved&&std::isfinite(state.reference_input_seconds)?state.reference_input_seconds:0;
            fixed_reference_chosen=true;
        }
        CompletePlan out;out.anchor_seconds=p.phase.valid?p.phase.date:p.first_seconds+hp::dwell_seconds/2;
        out.high_phase_radians=p.phase.high;out.reference_seconds=fixed_reference_seconds;
        out.phase_available=!p.missing&&p.phase.valid&&std::isfinite(p.phase.date)&&std::isfinite(p.phase.high)&&
            p.phase.date>=p.first_seconds&&p.phase.date<p.first_seconds+hp::dwell_seconds;
        const double start=p.first_seconds+static_cast<double>(settle+360)/fs;
        const double end=p.first_seconds+static_cast<double>(settle+aperture-360)/fs;
        const auto a=chain.joint.predict(2400,p.shift_hz,out.anchor_seconds);
        const auto first=chain.joint.predict(2400,p.shift_hz,p.first_seconds);
        const auto last=chain.joint.predict(2400,p.shift_hz,p.first_seconds+hp::dwell_seconds);
        h::HelperDelayedSmootherInput si;
        si.fresh_phase_valid=p.phase.valid;si.target_observation_qualified=p.target_accepted&&!p.missing&&!chain.joint_suspended;
        si.capture_first_seconds=p.first_seconds;si.capture_end_seconds=p.first_seconds+hp::dwell_seconds;
        si.support_first_seconds=start;si.support_end_seconds=end;si.anchor_seconds=p.phase.date;
        si.phase_first_seconds=p.first_seconds+static_cast<double>(settle)/fs;
        si.phase_end_seconds=p.first_seconds+static_cast<double>(settle+aperture)/fs;
        si.capture_frontier_seconds=captured_frontier_seconds;
        si.latest_observation_end_seconds=std::isfinite(chain.latest_joint_input)?chain.latest_joint_input+static_cast<double>(aperture+1)/(2*fs):0;
        si.retained_reference_seconds=p.reference_seconds;si.support_first=chain.joint.predict(2400,p.shift_hz,start);
        si.anchor=a;si.support_end=chain.joint.predict(2400,p.shift_hz,end);si.end_of_stream=eof;
        const auto sm=smoother.assess(si);
        out.smoother_failure=sm.failure;
        if(sm.valid){
            if(sm.reference_seconds!=fixed_reference_seconds)++post_smoother_fixed_reference_mismatch;
            if(!first.valid||!last.valid)++post_smoother_endpoint_invalid;
            else if(!first.reference_preserved||!last.reference_preserved||
                    std::abs(first.reference_input_seconds-p.reference_seconds)>=1e-9||
                    std::abs(last.reference_input_seconds-p.reference_seconds)>=1e-9)++post_smoother_endpoint_reference_mismatch;
        }
        out.qualified=sm.valid&&sm.reference_seconds==fixed_reference_seconds&&first.valid&&last.valid&&first.reference_preserved&&last.reference_preserved&&
            std::abs(first.reference_input_seconds-p.reference_seconds)<1e-9&&std::abs(last.reference_input_seconds-p.reference_seconds)<1e-9;
        if(out.qualified){
            out.origin=CompletePlan::Origin::qualified_joint;
            out.frequency_available=true;
            out.audio_scale=sm.audio_scale;out.common_frequency_hz=sm.common_frequency_offset_hz;
            out.audio_drift_ppm_per_second=a.audio_drift_ppm_per_second;
            out.source_seconds_at_anchor=sm.source_seconds_at_anchor;out.source_half_width_seconds=sm.source_half_width_seconds;
            out.audio_half_width_ppm=sm.audio_half_width_ppm;out.reference_seconds=sm.reference_seconds;
            out.source_available=true;
            out.transport_rate=sm.transport_rate_radians_per_second;out.diffusion_q=chain.receiver.aided_config.phase_noise_radians_per_sqrt_second;
            const auto block=static_cast<std::uint64_t>(p.index)*hp::dwell_samples;
            out.supported_first=block+settle+360;out.supported_end=block+settle+aperture-360;
            last_coeff={out.audio_scale,out.common_frequency_hz};last_rf=p.shift_hz;have_last=true;
        }else{
            // Point estimation and authority are separate. A rejected target
            // phase does not invalidate a unique, consistent joint clock and
            // frequency prediction from the existing Helper history. This
            // uses the posterior once as a prediction, never as new evidence;
            // the unchanged smoother gate still controls output authority.
            const bool joint_point=a.valid&&a.reference_initialized&&a.reference_preserved&&a.model_consistent&&
                !a.ambiguity_limited&&!a.work_limited&&!a.post_gap_qualification_pending&&a.alternatives==1&&!chain.joint_suspended&&
                std::isfinite(a.reference_input_seconds)&&std::isfinite(a.audio_scale)&&std::abs(a.audio_scale-1)<=.001&&
                std::isfinite(a.audio_drift_ppm_per_second)&&std::isfinite(a.frequency_hz)&&std::isfinite(a.source_seconds)&&
                std::isfinite(a.source_stochastic_sigma_seconds)&&a.source_stochastic_sigma_seconds>=0&&
                std::isfinite(a.source_model_half_width_seconds)&&a.source_model_half_width_seconds>=0&&
                std::isfinite(a.audio_stochastic_sigma_ppm)&&a.audio_stochastic_sigma_ppm>=0&&
                std::isfinite(a.audio_model_half_width_ppm)&&a.audio_model_half_width_ppm>=0;
            if(joint_point){
                out.audio_scale=a.audio_scale;out.common_frequency_hz=a.frequency_hz-2400*a.audio_scale;
                out.audio_drift_ppm_per_second=a.audio_drift_ppm_per_second;
                out.audio_half_width_ppm=3*a.audio_stochastic_sigma_ppm+a.audio_model_half_width_ppm;
                out.source_half_width_seconds=3*a.source_stochastic_sigma_seconds+a.source_model_half_width_seconds;
                if(a.reference_input_seconds==fixed_reference_seconds){
                    out.source_seconds_at_anchor=a.source_seconds;out.origin=CompletePlan::Origin::joint_point;
                }else{
                    // Change coordinates to the immutable receiver origin,
                    // not a new acquisition reference. The long extrapolation
                    // is only a point estimate with an explicit broad radius;
                    // it cannot confer qualified phase/timing authority.
                    const double dt=fixed_reference_seconds-out.anchor_seconds;
                    const double estimated_at_fixed=a.source_seconds+dt*a.audio_scale+
                        .5*a.audio_drift_ppm_per_second*1e-6*dt*dt;
                    out.source_seconds_at_anchor=a.source_seconds+fixed_reference_seconds-estimated_at_fixed;
                    out.source_half_width_seconds=std::max(.01,out.source_half_width_seconds+
                        std::abs(dt)*out.audio_half_width_ppm*1e-6);
                    out.origin=CompletePlan::Origin::joint_translated;
                }
                out.frequency_available=std::isfinite(out.common_frequency_hz);
                out.source_available=out.frequency_available&&std::isfinite(out.source_seconds_at_anchor)&&
                    std::isfinite(out.source_half_width_seconds);
                if(!out.source_available){out.frequency_available=false;out.origin=CompletePlan::Origin::nominal;}
            }
            // Broad accumulated frequency fit is independent of the target's
            // phase and does not confer qualified clock/phase authority. It is
            // available during startup before relative-clock qualification.
            const auto fitted=out.frequency_available?h::HelperMeasurementSnapshot{}:chain.receiver.snapshot(true);
            if(!out.frequency_available&&fitted.valid&&fitted.model_consistent){
                const auto c=coefficient(fitted,p.shift_hz,p.first_seconds+hp::dwell_seconds/2);
                if(std::isfinite(c.audio)&&std::abs(c.audio-1)<=.001&&std::isfinite(c.offset)){
                    out.audio_scale=c.audio;out.common_frequency_hz=c.offset;out.frequency_available=true;
                    out.audio_drift_ppm_per_second=fitted.audio_drift_ppm_per_second;
                    out.source_seconds_at_anchor=fixed_reference_seconds+elapsed_clock(fitted,fixed_reference_seconds,out.anchor_seconds);
                    out.source_half_width_seconds=.01+std::abs(out.anchor_seconds-fixed_reference_seconds)*.001;
                    out.audio_half_width_ppm=1000;out.source_available=true;
                    out.origin=CompletePlan::Origin::broad;
                }
            }
            if(!out.frequency_available&&p.local_measurement.valid){
                // Last-resort two-pilot frequency evidence for this dwell;
                // this is deliberately unqualified and may be noisy.
                const auto& m=p.local_measurement;
                const double audio=(m.high_frequency_hz-m.low_frequency_hz)/1850.;
                const double frequency=m.high_frequency_hz-2400*audio;
                if(std::isfinite(audio)&&std::abs(audio-1)<=.001&&std::isfinite(frequency)){
                    out.audio_scale=audio;out.common_frequency_hz=frequency;out.frequency_available=true;
                    out.origin=CompletePlan::Origin::local;
                }
            }
        }
        if(!out.frequency_available&&have_last){
            // Unqualified broad fallback: the empirical frequency scaling is
            // carried solely as a best-effort correction, never authority.
            out.audio_scale=last_coeff.audio;out.common_frequency_hz=last_coeff.offset*p.shift_hz/last_rf;out.frequency_available=true;
            out.origin=CompletePlan::Origin::retained;
        }
        if(!out.source_available){
            // Preserve the one origin selected before the first output. The
            // startup map is a broad data-only affine prediction; its generous
            // uncertainty and model_qualified=false prevent false authority.
            out.source_seconds_at_anchor=fixed_reference_seconds+
                (out.anchor_seconds-fixed_reference_seconds)*out.audio_scale;
            out.source_half_width_seconds=.01+std::abs(out.anchor_seconds-fixed_reference_seconds)*.001;
            out.audio_half_width_ppm=1000;out.source_available=true;
        }
        ++plan_origins[static_cast<std::size_t>(out.origin)];
        const auto failure=static_cast<std::size_t>(out.smoother_failure);if(failure<smoother_failures.size())++smoother_failures[failure];
        if(trace_plans)std::cerr<<std::setprecision(12)<<"helper_complete_plan index="<<p.index<<" origin="<<static_cast<unsigned>(out.origin)
            <<" smoother_failure="<<failure<<" qualified="<<out.qualified<<" target_accepted="<<p.target_accepted
            <<" phase_available="<<out.phase_available<<" anchor="<<out.anchor_seconds<<" joint_anchor_valid="<<a.valid
            <<" joint_first_valid="<<first.valid<<" joint_last_valid="<<last.valid<<" joint_suspended="<<chain.joint_suspended
            <<" fixed_reference="<<fixed_reference_seconds<<" joint_reference="<<a.reference_input_seconds
            <<" audio_scale="<<out.audio_scale<<" frequency_offset_hz="<<out.common_frequency_hz
            <<" source_seconds="<<out.source_seconds_at_anchor<<" source_half_width_seconds="<<out.source_half_width_seconds<<'\n';
        return out;
    }
};

struct CompleteBoundaryHistory {
    static constexpr std::size_t edge=432;
    h::HelperBoundaryTracker tracker{{fs,.008,.00025,.004,16}};
    std::array<S,edge> previous_tail{};
    h::HelperBoundaryTracker::Hop previous{};
    bool have_previous=false,available=false;
    double epoch_offset_samples=0,audio_scale=1,latest_observation_end_sample=0;
    std::uint64_t attempts=0,accepted=0,evaluator_count=0;
    long double nominal_error_square=0,tracked_error_square=0;double nominal_error_worst=0,tracked_error_worst=0;
    double cpu_seconds=0,evaluator_cpu_seconds=0;
    void observe(const hp::Dwell& d,bool missing,unsigned index,CompleteHelperHistory& history,const ContinuationSource& source){
        const auto measured_before=history.chain.receiver.total_cpu(),evaluator_before=evaluator_cpu_seconds;const auto begin=cpu();const auto& m=history.chain.receiver.measurement;const auto& phase=history.chain.receiver.phase;
        h::HelperBoundaryTracker::Hop current;
        if(!missing&&m.valid&&phase.valid){
            const auto aperture_samples=std::span<const S>(d.samples.data()+settle,aperture);
            const double at=d.first_seconds*fs+settle,ps=std::max(.02,phase.high_sigma),ls=std::max(ps,phase.difference_sigma);
            current.pilots[0]=h::HelperBoundaryTracker::settled_pilot(aperture_samples,at,fs,m.high_frequency_hz,ps,m.high_sigma_hz+m.common_phase_frequency_sigma_hz);
            current.pilots[1]=h::HelperBoundaryTracker::settled_pilot(aperture_samples,at,fs,m.low_frequency_hz,ls,m.low_sigma_hz+m.common_phase_frequency_sigma_hz);
            current.observation_first_sample=at;current.observation_end_sample=at+aperture;
        }
        const auto fitted=history.chain.receiver.snapshot(true);
        if(fitted.valid&&fitted.model_consistent){const auto c=coefficient(fitted,d.shift_hz,d.first_seconds+.2);
            if(std::isfinite(c.audio)&&std::abs(c.audio-1)<.001)audio_scale=c.audio;}
        if(index&&have_previous&&current.pilots[0].valid&&current.pilots[1].valid){
            std::array<S,2*edge> window{};std::copy(previous_tail.begin(),previous_tail.end(),window.begin());
            std::copy_n(d.samples.begin(),edge,window.begin()+edge);
            h::HelperBoundaryTracker::Input input;input.samples=window;input.first_sample=index*hp::dwell_samples-edge;
            input.predicted_boundary_sample=index*hp::dwell_samples/audio_scale;input.audio_scale=audio_scale;
            input.before=previous;input.after=current;input.latest_observation_end_sample=current.observation_end_sample;
            ++attempts;const auto result=tracker.observe(input);
            if(result.available){available=true;epoch_offset_samples=result.epoch_offset_samples;
                latest_observation_end_sample=result.latest_observation_end_sample;++accepted;
                // Evaluator-only comparison; no source truth enters tracker or
                // correction. The actual TX calendar is solved independently.
                const auto eval_start=cpu();double lo=d.first_seconds-.02,hi=d.first_seconds+.02;for(unsigned n=0;n<32;++n){const double mid=(lo+hi)/2;
                    if(source.tx_time(mid)<d.first_seconds)lo=mid;else hi=mid;}
                const double truth=.5*(lo+hi)*fs;const double nominal=index*hp::dwell_samples-truth;
                const double tracked=result.boundary_sample-truth;nominal_error_square+=nominal*nominal;tracked_error_square+=tracked*tracked;
                nominal_error_worst=std::max(nominal_error_worst,std::abs(nominal));tracked_error_worst=std::max(tracked_error_worst,std::abs(tracked));++evaluator_count;
                evaluator_cpu_seconds+=cpu()-eval_start;
            }
        }
        if(!missing&&current.pilots[0].valid&&current.pilots[1].valid){std::copy_n(d.samples.end()-static_cast<std::ptrdiff_t>(edge),edge,previous_tail.begin());
            previous=current;have_previous=true;}else have_previous=false;
        // snapshot(true) is already charged by Receiver::total_cpu.
        cpu_seconds+=cpu()-begin-(history.chain.receiver.total_cpu()-measured_before)-(evaluator_cpu_seconds-evaluator_before);
    }
};

// Optional marker evidence, separate from both pilot oscillator history and
// the permanent correction reference. No marker or prediction is reingested
// as an independent pilot observation. Storage covers the maximum delay.
struct CompleteInversionHistory {
    struct Record {
        bool available=false,usable=false;
        unsigned dwell=0;
        h::PunctuationObservation observation{};
        double boundary_sample=0,boundary_radius_samples=0,audio_scale=1,audio_half_width_ppm=1000;
    };
    std::array<Record,16> records{};
    std::unique_ptr<h::HelperInversionTiming> detector;
    h::HelperInversionTiming::Config centers;
    std::uint64_t attempts=0,markers=0,usable=0,recenters=0,gaps=0,covered=0,available36=0,selected_boundaries=0,evaluator_count=0;
    double cpu_seconds=0,evaluator_cpu_seconds=0,error_square=0,error_worst=0,radius_max=0;
    double latency_max=0;std::uint64_t previous_marker=0;
    const Record* find(unsigned dwell) const {
        const auto& r=records[dwell%records.size()];return r.available&&r.dwell==dwell?&r:nullptr;
    }
    void observe(const hp::Dwell& d,bool missing,unsigned index,const CompletePending& pending,
                 CompleteHelperHistory& history,const ContinuationSource& source){
        const auto begin=cpu();
        if(missing){if(detector)detector->missing((index+1ULL)*hp::dwell_samples);++gaps;
            records[index%records.size()]={};cpu_seconds+=cpu()-begin;return;}
        const auto& m=pending.local_measurement;
        if(m.valid&&(!detector||std::abs(m.high_frequency_hz-centers.high_hz)>8||std::abs(m.low_frequency_hz-centers.low_hz)>8)){
            centers.high_hz=m.high_frequency_hz;centers.low_hz=m.low_frequency_hz;
            centers.auxiliary_hz=centers.low_hz+400./1850.*(centers.high_hz-centers.low_hz);
            if(detector)++recenters;
            detector=std::make_unique<h::HelperInversionTiming>(centers);
        }
        if(!detector){cpu_seconds+=cpu()-begin;return;}
        ++attempts;detector->observe(d.samples,static_cast<std::uint64_t>(index)*hp::dwell_samples);
        std::array<Record,4> emitted{};std::size_t emitted_count=0;
        for(auto p=detector->take_update();p.known;p=detector->take_update()){
            require(!markers||p.sample>previous_marker,"auxiliary inversion duplicate/backward observation");
            require(p.last_sample<(index+1ULL)*hp::dwell_samples,"auxiliary inversion future observation");
            previous_marker=p.sample;++markers;
            Record r;r.available=true;r.observation=p;
            const double marker=static_cast<double>(p.sample)+p.fraction;
            const auto rough=std::llround((marker/fs-.160)/hp::dwell_seconds);
            if(rough<0||rough>static_cast<long long>(index))continue;
            r.dwell=static_cast<unsigned>(rough);
            // A prediction is used once to express the 160 ms source offset;
            // it contributes uncertainty, never another timing observation.
            const auto clock=history.chain.joint.predict(950,d.shift_hz,marker/fs);
            if(clock.valid&&clock.model_consistent&&clock.reference_preserved&&!clock.ambiguity_limited&&
               std::isfinite(clock.audio_scale)&&std::abs(clock.audio_scale-1)<=.001){
                r.audio_scale=clock.audio_scale;
                r.audio_half_width_ppm=3*clock.audio_stochastic_sigma_ppm+clock.audio_model_half_width_ppm;
            }else if(m.valid){
                const double a=(m.high_frequency_hz-m.low_frequency_hz)/1850.;
                if(std::isfinite(a)&&std::abs(a-1)<=.001){r.audio_scale=a;
                    r.audio_half_width_ppm=1000.+std::abs(a-1)*1e6;}
            }
            // No unsupported covariance cancellation: use a bounded sum. A
            // broad startup prior does not make a precise-clock certificate.
            if(!std::isfinite(r.audio_half_width_ppm)||r.audio_half_width_ppm<0)r.audio_half_width_ppm=1000;
            const double scale_low=r.audio_scale-r.audio_half_width_ppm*1e-6;
            r.boundary_sample=marker-.160*fs/r.audio_scale;
            r.boundary_radius_samples=std::isfinite(scale_low)&&scale_low>0?
                p.uncertainty_samples+.160*fs*r.audio_half_width_ppm*1e-6/(scale_low*scale_low)+1:
                std::numeric_limits<double>::infinity();
            const double nominal=r.dwell*hp::dwell_samples;
            r.usable=std::isfinite(r.boundary_sample)&&std::isfinite(r.boundary_radius_samples)&&
                r.boundary_radius_samples<.008*fs&&std::abs(r.boundary_sample-nominal)+r.boundary_radius_samples<=.008*fs;
            if(r.usable)++usable;records[r.dwell%records.size()]=r;
            require(emitted_count<emitted.size(),"too many auxiliary marker publications in one dwell");emitted[emitted_count++]=r;
        }
        cpu_seconds+=cpu()-begin;
        // Truth-based accuracy and logging occur after publication and never
        // affect the record, selector, correction, or its fixed reference.
        const auto eval_started=cpu();
        for(std::size_t i=0;i<emitted_count;++i){const auto& r=emitted[i];const auto& p=r.observation;
            const double date=(p.sample+p.fraction)/fs;
            const double error=std::remainder(source.tx_time(date)-.160,.4)/source.audio_scale_at(date);
            ++evaluator_count;error_square+=error*error;error_worst=std::max(error_worst,std::abs(error));
            covered+=std::abs(error)*fs<=p.uncertainty_samples;radius_max=std::max(radius_max,p.uncertainty_samples/fs);
            latency_max=std::max(latency_max,(index+1)*.4-date);
            std::cout<<std::setprecision(12)<<"helper_complete_inversion dwell="<<r.dwell<<" marker_sample="<<p.sample
                <<" marker_fraction="<<p.fraction<<" first_sample="<<p.first_sample<<" last_sample="<<p.last_sample
                <<" marker_radius_samples="<<p.uncertainty_samples<<" dwell_start_sample="<<r.boundary_sample
                <<" dwell_radius_samples="<<r.boundary_radius_samples<<" audio_scale="<<r.audio_scale
                <<" audio_half_width_ppm="<<r.audio_half_width_ppm<<" usable_within_schedule_prior="<<r.usable
                <<" modulo_dwell_only=1 receiver_clock_feedback=0 evaluator_error_us="<<error*1e6<<'\n';
        }
        evaluator_cpu_seconds+=cpu()-eval_started;
    }
};

// The optional weak-marker path retains original data likelihoods. Its pilot
// clock is a coordinate model, never an additional independent observation.
struct CompleteProfileHistory {
    std::unique_ptr<h::HelperInversionProfile> tracker;
    h::HelperInversionProfile::ClockModel clock{};
    h::HelperInversionProfile::Estimate latest{},frozen36{};
    struct LocalRecord {unsigned index=0;bool present=false;h::HelperInversionProfile::Estimate value{};};
    std::array<LocalRecord,16> local_records{};
    std::uint64_t attempts=0,stored=0,dwell_qualified=0,pooled_qualified=0,selected=0,missing=0;
    std::uint64_t frontier=0,covered=0,scored=0,raw_samples_processed=0;
    std::uint64_t local_scored=0,local_covered=0;
    std::uint64_t local_selected=0,pooled_selected=0;
    std::array<std::uint64_t,12> reasons{};
    std::array<std::uint64_t,4> within{};
    double cpu_seconds=0,release_cpu_seconds=0,evaluator_cpu_seconds=0,error_square=0,worst_error=0,frozen36_error=0;
    double first_qualified_capture_end=0,maximum_radius=0;
    double local_error_square=0,local_worst_error=0;
    explicit CompleteProfileHistory(bool enabled){const auto begin=cpu();if(enabled)
        tracker=std::make_unique<h::HelperInversionProfile>(h::HelperInversionProfile::Config{});cpu_seconds=enabled?cpu()-begin:0;}
    static bool usable_clock(const h::HelperJointEstimator::Estimate& a){
        return a.valid&&a.reference_initialized&&a.reference_preserved&&a.model_consistent&&
            !a.ambiguity_limited&&!a.work_limited&&!a.post_gap_qualification_pending&&a.alternatives==1&&
            std::isfinite(a.reference_input_seconds)&&std::isfinite(a.source_seconds)&&
            std::isfinite(a.audio_scale)&&std::abs(a.audio_scale-1)<.001&&std::isfinite(a.audio_drift_ppm_per_second)&&
            std::isfinite(a.source_stochastic_sigma_seconds)&&a.source_stochastic_sigma_seconds>=0&&
            std::isfinite(a.source_model_half_width_seconds)&&a.source_model_half_width_seconds>=0&&
            std::isfinite(a.audio_stochastic_sigma_ppm)&&a.audio_stochastic_sigma_ppm>=0&&
            std::isfinite(a.audio_model_half_width_ppm)&&a.audio_model_half_width_ppm>=0;
    }
    long double marker_prior(double nominal)const{
        if(!clock.available)return nominal*fs;
        const long double delta=nominal-clock.source_seconds_at_anchor;
        long double seconds=delta/clock.audio_scale;
        for(unsigned k=0;k<3;++k)seconds-=(clock.audio_scale*seconds+.5e-6L*clock.audio_drift_ppm_per_second*seconds*seconds-delta)/
            (clock.audio_scale+1e-6L*clock.audio_drift_ppm_per_second*seconds);
        return clock.anchor_input_sample+seconds*fs;
    }
    void observe(const hp::Dwell& d,bool gap,unsigned index,const CompletePending& pending,
                 CompleteHelperHistory& history,const ContinuationSource& source){
        const auto begin=cpu();frontier=(index+1ULL)*hp::dwell_samples;
        const double nominal=index*hp::dwell_seconds+.160;
        const auto a=history.chain.joint.predict(950,d.shift_hz,nominal);
        // The fitted map is anchored at its first accepted phase center.
        // predict(0) is intentionally unavailable before that center; using
        // it as a gate would disable useful relative timing indefinitely.
        const auto first=history.chain.joint.predict(950,d.shift_hz,a.reference_input_seconds);
        clock={};
        if(!history.chain.joint_suspended&&usable_clock(a)&&usable_clock(first)&&a.reference_input_seconds==first.reference_input_seconds){
            clock.available=true;clock.reference_preserved=true;clock.reference_input_sample=a.reference_input_seconds*fs;
            clock.anchor_input_sample=nominal*fs;clock.source_seconds_at_anchor=a.source_seconds;
            clock.audio_scale=a.audio_scale;clock.audio_drift_ppm_per_second=a.audio_drift_ppm_per_second;
            // Whole-history endpoint enclosure: this shared uncertainty must
            // not decrease as though repeated predictions were new evidence.
            clock.source_half_width_seconds=std::max(3*a.source_stochastic_sigma_seconds+a.source_model_half_width_seconds,
                3*first.source_stochastic_sigma_seconds+first.source_model_half_width_seconds);
            clock.audio_half_width_ppm=std::max(3*a.audio_stochastic_sigma_ppm+a.audio_model_half_width_ppm,
                3*first.audio_stochastic_sigma_ppm+first.audio_model_half_width_ppm);
            // The clock model remains conditional on the same bounded
            // oscillator evolution before its first accepted phase center.
            // Keep that short backward transport allowance shared.
            clock.source_half_width_seconds+=std::max(0.,a.reference_input_seconds-.160)*clock.audio_half_width_ppm*1e-6;
            clock.latest_observation_end_sample=static_cast<std::uint64_t>(std::ceil(history.chain.receiver.measurement.end_sample_time_seconds*fs));
        }
        h::HelperInversionProfile::Observation observation;
        const auto& m=pending.local_measurement;
        if(gap)++missing;
        else if(m.valid&&m.time_seconds>=d.first_seconds&&m.time_seconds<d.first_seconds+.4){
            const auto center=marker_prior(nominal);const auto start=static_cast<long long>(std::floor(center-.040*fs));
            constexpr std::size_t length=1920;
            const auto block=static_cast<long long>(index)*hp::dwell_samples;
            if(start>=block&&start+static_cast<long long>(length)<=block+hp::dwell_samples){
                h::HelperInversionProfile::Input input;input.samples=std::span(d.samples).subspan(static_cast<std::size_t>(start-block),length);
                input.first_sample=static_cast<std::uint64_t>(start);input.capture_frontier_end_sample=frontier;input.dwell=index;
                input.prediction_observation_end_sample=std::max(clock.latest_observation_end_sample,
                    static_cast<std::uint64_t>(std::ceil(m.end_sample_time_seconds*fs)));
                input.predicted_marker_sample=center;input.nominal_marker_source_seconds=nominal;
                constexpr double weight=400./1850.;
                input.auxiliary_frequency_hz=(1-weight)*m.low_frequency_hz+weight*m.high_frequency_hz;
                input.frequency_half_width_hz=3*((1-weight)*m.low_sigma_hz+weight*m.high_sigma_hz+m.common_phase_frequency_sigma_hz);
                input.nuisance_frequency_hz={m.low_frequency_hz,m.high_frequency_hz};
                input.audio_scale=clock.available?clock.audio_scale:1.;input.audio_half_width_ppm=clock.available?clock.audio_half_width_ppm:1000.;
                ++attempts;observation=tracker->observe(input);stored+=observation.stored;dwell_qualified+=observation.dwell.qualified;
                raw_samples_processed+=observation.raw_samples_processed;
            }
        }
        latest=tracker->estimate(clock,nominal,frontier);
        local_records[index%local_records.size()]={index,observation.dwell.qualified,observation.dwell};
        const auto reason=static_cast<std::size_t>(latest.reason);if(reason<reasons.size())++reasons[reason];
        if(latest.qualified){++pooled_qualified;if(!first_qualified_capture_end)first_qualified_capture_end=(index+1)*.4;}
        if(index==89)frozen36=latest;
        cpu_seconds+=cpu()-begin;
        const auto eval_begin=cpu();
        double error=0;
        double local_error=0;
        if(observation.dwell.available){const double date=static_cast<double>(observation.dwell.marker_input_sample/fs);
            local_error=(source.tx_time(date)-nominal)/source.audio_scale_at(date);
            if(observation.dwell.qualified){++local_scored;local_error_square+=local_error*local_error;local_worst_error=std::max(local_worst_error,std::abs(local_error));
                local_covered+=std::abs(local_error)<=observation.dwell.conditional_half_width_seconds;}}
        if(latest.available){const double date=static_cast<double>(latest.marker_input_sample/fs);
            error=(source.tx_time(date)-nominal)/source.audio_scale_at(date);
            if(index==89)frozen36_error=error;
            if(latest.qualified){++scored;error_square+=error*error;worst_error=std::max(worst_error,std::abs(error));
                covered+=std::abs(error)<=latest.conditional_half_width_seconds;maximum_radius=std::max(maximum_radius,latest.conditional_half_width_seconds);
                constexpr std::array<double,4> scales{.000025,.000125,.001,.010};for(std::size_t j=0;j<scales.size();++j)within[j]+=std::abs(error)<=scales[j];}}
        std::cout<<std::setprecision(12)<<"helper_complete_inversion_profile dwell="<<index<<" stored="<<observation.stored
            <<" local_available="<<observation.dwell.available<<" local_qualified="<<observation.dwell.qualified
            <<" local_radius_seconds="<<observation.dwell.conditional_half_width_seconds<<" evaluator_local_error_us="<<local_error*1e6
            <<" local_reason="<<h::HelperInversionProfile::reason_text(observation.reason)
            <<" available="<<latest.available<<" qualified="<<latest.qualified<<" reason="<<h::HelperInversionProfile::reason_text(latest.reason)
            <<" reason_code="<<static_cast<unsigned>(latest.reason)<<" clock_available="<<clock.available
            <<" observations="<<latest.observations<<" marker_sample="<<static_cast<double>(latest.marker_input_sample)
            <<" offset_seconds="<<latest.offset_seconds<<" radius_seconds="<<latest.conditional_half_width_seconds
            <<" score="<<latest.likelihood_score<<" threshold="<<latest.detection_threshold
            <<" observation_end_sample="<<latest.latest_observation_end_sample<<" capture_end_sample="<<frontier
            <<" clock_shared_radius_seconds="<<clock.source_half_width_seconds<<" evaluator_error_us="<<error*1e6
            <<" receiver_clock_feedback=0 modulo_dwell_only=1\n";
        evaluator_cpu_seconds+=cpu()-eval_begin;
    }
    h::HelperInversionProfile::Estimate estimate(unsigned index){
        // This cost is already inside the conditioner's provider callback.
        const auto begin=cpu();auto result=tracker->estimate(clock,index*hp::dwell_seconds+.160,frontier);
        if(result.qualified)++pooled_selected;
        else{const auto& local=local_records[index%local_records.size()];
            if(local.present&&local.index==index){result=local.value;++local_selected;}}
        release_cpu_seconds+=cpu()-begin;return result;
    }
};
}

namespace {
void verify_complete_reservations(){
    // New schedule and thermal seeds are reserved before tuning. Compare the
    // actual 32-byte schedule keys, not merely integer seed values.
    std::set<std::array<std::uint8_t,32>> keys;
    const auto append=[&](std::uint64_t base,unsigned count){
        for(unsigned i=0;i<count;++i){const auto value=helper_measurement_hops::fixture_key(base+i,true);
            require(value.size()==32,"complete-study schedule key size");std::array<std::uint8_t,32> key{};
            std::copy(value.begin(),value.end(),key.begin());require(keys.insert(key).second,"complete-study reserved schedule key collision");}
    };
    append(1666973697ULL,8);append(1667039233ULL,32);append(1667104769ULL,8);append(1667170305ULL,8);
    for(const auto base:{1680000001ULL,1690000001ULL,1700000001ULL,1710000001ULL}){
        append(base,8);append(base+100000,32);append(base+200000,8);append(base+300000,8);
    }
    append(1810000001ULL,8);append(1810100001ULL,32);append(1810200001ULL,8);append(1810300001ULL,8);
    append(1810400001ULL,32); // newly reserved, post-repair HF holdouts
    append(1911000001ULL,8); // auxiliary timing: reserved before matched checks
    append(1930000001ULL,8);append(1930100001ULL,32);append(1930200001ULL,8);
}
}

namespace {
using CompleteConditioner=h::HelperCompleteConditioner;
using CompleteEvent=CompleteConditioner::Event;
struct CompleteEvaluation {
    Trace output;
    std::vector<long double> output_seconds;
    std::vector<std::uint32_t> output_segments;
    std::vector<unsigned char> output_segment_endpoint;
    std::vector<std::uint64_t> output_schedule_slot;
    std::vector<unsigned char> output_schedule_valid;
    double evaluator_erasure_seconds=0;
    std::vector<CompleteEvent> noisy_pending;
    std::size_t paired=0;
    std::uint64_t missing_events=0,missing_samples=0,phase_known=0,clock_known=0,well=0,ambiguous=0;
    std::uint64_t capture_timestamp_unavailable=0,tx_unavailable=0,reference_mismatch=0,conditioned_samples=0;
    std::uint64_t outputs_before_eof=0,outputs_before_36=0,physical_schedule_mismatch=0,supported_schedule_mismatch=0,pretransmission_samples=0;
    std::uint64_t utc_vs_release_slot_differences=0,synthetic_time_failures=0,trace_discontinuities=0;
    bool gap_pending=false;std::uint32_t segment=0;
    long double max_helper_end_for_pre36=0,max_helper_end_for_heldout=0;std::uint64_t pre36_using_post36=0;
    std::optional<long double> reference;
    bool evaluator_reference_initialized=false;double evaluator_reference_seconds=0,evaluator_gauge_seconds=0;
    long double previous_capture=-1,min_latency_ns=1e30L,max_latency_ns=0;
    double timing_first=0,timing_last=0,timing_max_abs=0,model_discrepancy_max_abs=0;std::uint64_t timing_samples=0,timing_cycle_slips=0;
    long long previous_timing_cycle=0;
    void begin_release(){noisy_pending.clear();paired=0;}
    void noisy(const CompleteEvent& e,const ContinuationSource& source,const hp::Config& cfg,bool eof,double now_seconds,bool clock_adjust){
        noisy_pending.push_back(e);
        if(!e.samples_present){++missing_events;missing_samples+=e.missing_sample_count;return;}
        require(std::isfinite(e.source_input_sample)&&e.source_input_sample>=0&&e.source_input_sample>previous_capture,"complete output capture ordering");
        previous_capture=e.source_input_sample;++conditioned_samples;if(!eof)++outputs_before_eof;if(now_seconds<=36.)++outputs_before_36;
        if(e.phase_applied)++phase_known;if(e.clock_applied)++clock_known;if(e.well_supported)++well;if(e.boundary_ambiguous)++ambiguous;
        if(e.source_input_sample<36*fs){max_helper_end_for_pre36=std::max(max_helper_end_for_pre36,e.latest_helper_observation_end_sample);
            if(e.latest_helper_observation_end_sample>36*fs)++pre36_using_post36;}
        if(e.source_input_sample>=38*fs&&e.source_input_sample<40*fs)
            max_helper_end_for_heldout=std::max(max_helper_end_for_heldout,e.latest_helper_observation_end_sample);
        if(!e.capture_timestamp_valid)++capture_timestamp_unavailable;
        if(!e.transmitter_time_valid)++tx_unavailable;
        if(e.reference_mismatch)++reference_mismatch;
        if(e.transmitter_time_valid){if(!reference)reference=e.reference_input_sample;else if(*reference!=e.reference_input_sample)++reference_mismatch;}
        // Evaluator-only relative timing error under a single sample-zero
        // convention, never consumed by tracking or correction.
        require(evaluator_reference_initialized,"evaluator reference was not fixed before output");
        const double timing=static_cast<double>(e.relative_source_seconds)-source.tx_time(static_cast<double>(e.source_input_sample/fs))-evaluator_gauge_seconds;
        if(!timing_samples)timing_first=timing;timing_last=timing;timing_max_abs=std::max(timing_max_abs,std::abs(timing));++timing_samples;
        const auto cycle=static_cast<long long>(std::llround(timing*1850));if(cycle!=previous_timing_cycle)++timing_cycle_slips;previous_timing_cycle=cycle;
        if(e.clock_model_discrepancy_valid)model_discrepancy_max_abs=std::max(model_discrepancy_max_abs,std::abs(static_cast<double>(e.clock_model_discrepancy_seconds)));
        if(e.well_supported)require(e.phase_applied&&e.clock_applied&&!e.boundary_ambiguous&&e.capture_timestamp_valid&&e.transmitter_time_valid,
            "qualified complete output lacks its stated authority");
        const auto latency=static_cast<long double>(e.release_monotonic_nanoseconds)-e.capture_monotonic_nanoseconds;
        require(latency>=0,"complete output released before capture");min_latency_ns=std::min(min_latency_ns,latency);max_latency_ns=std::max(max_latency_ns,latency);
        // Evaluator-only physical schedule/timestamp comparison. These values
        // never feed the conditioner, its selection or the Helper estimator.
        const auto true_slot=source.base().rf_hop_at_input(static_cast<double>(e.source_input_sample/fs));
        if(true_slot<0)++pretransmission_samples;
        else if(e.schedule_valid&&static_cast<long long>(e.schedule_slot)!=true_slot){++physical_schedule_mismatch;if(e.well_supported)++supported_schedule_mismatch;}
        if(e.capture_timestamp_valid){
            const auto block=static_cast<unsigned>(e.source_input_sample/hp::dwell_samples);
            const long double expected=1'700'000'000'000'000'000.L+(e.source_input_sample+cfg.fractional_input_sample)*1e9L/(fs*source.base().rx_scale())-
                ((clock_adjust&&block>=50)?2'000'000'000.L:0.L);
            if(std::abs(e.capture_system_nanoseconds-expected)>e.capture_uncertainty_nanoseconds)++synthetic_time_failures;
            // A release-time lookup deliberately uses a different clock. The
            // mismatch count is illustrative, not a Robust interoperability claim.
            const auto release_slot=static_cast<long long>(std::floor((static_cast<long double>(e.release_monotonic_nanoseconds)-1'000'000'000.L)/400'000'000.L));
            const auto capture_slot=static_cast<long long>(std::floor((e.capture_system_nanoseconds-1'700'000'000'000'000'000.L)/400'000'000.L));
            if(capture_slot!=release_slot)++utc_vs_release_slot_differences;
        }
    }
    void clean(const CompleteEvent& e,const ContinuationSource& source){
        require(paired<noisy_pending.size(),"unpaired evaluator output");const auto& n=noisy_pending[paired++];
        require(n.samples_present==e.samples_present&&n.output_sample==e.output_sample&&n.source_input_sample==e.source_input_sample,
            "clean twin changed receiver output geometry");
        if(!e.samples_present){gap_pending=true;return;}
        // Preserve every emitted payload for evaluator-only measurements. A
        // physical gap is recorded separately, never filled; output_seconds
        // is the real per-event source clock rather than first+i timing.
        if(gap_pending){++trace_discontinuities;++segment;gap_pending=false;}
        if(output.noisy.empty())output.first=n.output_sample;
        require(n.output_sample==output.first+output.noisy.size(),"complete output event ordinal discontinuity");
        output.noisy.push_back(n.sample);output.clean.push_back(e.sample);output.input_dates.push_back(n.source_input_sample);
        output_seconds.push_back(n.relative_source_seconds);
        output_segments.push_back(segment);output_segment_endpoint.push_back(static_cast<unsigned char>(n.segment_endpoint));
        output_schedule_slot.push_back(n.schedule_slot);output_schedule_valid.push_back(static_cast<unsigned char>(n.schedule_valid));
        output.supported.push_back(static_cast<unsigned char>(n.well_supported));
        const auto local=std::fmod(static_cast<double>(n.source_input_sample/fs),hp::dwell_seconds);
        const bool erased=evaluator_erasure_seconds>0&&(local<evaluator_erasure_seconds+1/fs||local>=hp::dwell_seconds-1/fs);
        output.physically_disturbed.push_back(static_cast<unsigned char>(erased||(source.has_transients()&&source.physical_disturbance(n.source_input_sample,1))));
    }
    void end_release()const{require(paired==noisy_pending.size(),"clean twin did not reproduce receiver output");}
};
}

namespace {
// Evaluator-only fixed-gauge measurements use the actual relative output time
// of each event, not the event ordinal. No tone estimate feeds the receiver.
void print_complete_tone(const CompleteEvaluation& eval,double gauge,double first,double end,const char* scope){
    struct A {std::uint64_t count=0,physical=0,zero_clean_energy=0,pairs=0,dwell_pairs=0,frequency_dwells=0;
        long double square=0,noisy=0,freq_square=0,dwell_frequency_square=0;std::complex<long double> dwell_lag{};
        double worst=0,freq_worst=0,dwell_frequency_worst=0;std::array<std::uint64_t,720> hist{};
        void finish_dwell(){if(dwell_pairs>=256&&std::norm(dwell_lag)>0){const double f=static_cast<double>(std::arg(dwell_lag))*fs/(tau*128);
                dwell_frequency_square+=f*f;dwell_frequency_worst=std::max(dwell_frequency_worst,std::abs(f));++frequency_dwells;}
            dwell_lag={};dwell_pairs=0;}};
    struct Previous {std::complex<double> residual{};long double time=0;std::uint64_t dwell=0;unsigned region=0;bool supported=false,usable=false;};
    std::array<std::array<A,3>,4> a{};std::array<Previous,128> previous{};
    const auto& t=eval.output;require(eval.output_seconds.size()==t.noisy.size(),"complete tone dates unavailable");
    std::uint64_t previous_dwell=std::numeric_limits<std::uint64_t>::max();
    for(std::size_t i=0;i<t.noisy.size();++i){const auto pos=t.input_dates[i];const double seconds=static_cast<double>(pos/fs);if(seconds<first||seconds>=end)continue;
        const auto dwell=static_cast<std::uint64_t>(pos/hp::dwell_samples);const double local=static_cast<double>(pos-dwell*hp::dwell_samples)/fs;
        if(previous_dwell!=dwell){if(previous_dwell!=std::numeric_limits<std::uint64_t>::max())for(auto& row:a)for(auto& x:row)x.finish_dwell();previous_dwell=dwell;}
        const unsigned region=local<.06?0:local>=.34?2:1;const unsigned select=t.supported[i]?1:2;
        const double expected=tau*std::remainder(hp::probe_frequency_hz*static_cast<double>(eval.output_seconds[i]),1.)+tau*(2400-hp::probe_frequency_hz)*gauge;
        const C clean=C(t.clean[i])*std::polar(1.,-expected),noisy=C(t.noisy[i])*std::polar(1.,-expected);
        const bool usable=t.physically_disturbed[i]<2&&std::norm(clean)>1e-12;const double phase=std::abs(std::arg(clean))*180/std::numbers::pi;
        const auto old=previous[i%previous.size()];
        const std::array<unsigned,2> regions{region,3};for(const auto r:regions){if(r==3&&region==1)continue;
            for(const auto s:{0U,select}){auto& x=a[r][s];++x.count;if(t.physically_disturbed[i]==1)++x.physical;if(std::norm(clean)<=1e-12)++x.zero_clean_energy;if(!usable)continue;
                x.square+=phase*phase;const double nphase=std::abs(std::arg(noisy))*180/std::numbers::pi;x.noisy+=nphase*nphase;
                x.worst=std::max(x.worst,phase);++x.hist[std::min<std::size_t>(719,static_cast<std::size_t>(phase*4))];
                if(i>=previous.size()&&old.usable&&old.dwell==dwell&&old.region==region&&(s==0||old.supported==static_cast<bool>(t.supported[i]))){
                    const auto dt=eval.output_seconds[i]-old.time;if(dt>0&&dt<1.5L*128/fs){const double f=std::arg(clean*std::conj(old.residual))/(tau*static_cast<double>(dt));
                        x.freq_square+=f*f;x.freq_worst=std::max(x.freq_worst,std::abs(f));++x.pairs;
                        const auto pair_amplitude=std::abs(clean*std::conj(old.residual));
                        x.dwell_lag+=static_cast<long double>(pair_amplitude)*std::polar(1.L,static_cast<long double>(tau*f*128/fs));++x.dwell_pairs;}}
            }}
        previous[i%previous.size()]={clean,eval.output_seconds[i],dwell,region,t.supported[i]!=0,usable};
    }
    for(auto& row:a)for(auto& x:row)x.finish_dwell();
    constexpr std::array<const char*,4> regions{"beginning","interior","end","boundary"};constexpr std::array<const char*,3> selects{"all","supported","uncertain"};
    for(std::size_t r=0;r<regions.size();++r)for(std::size_t s=0;s<selects.size();++s){const auto& x=a[r][s];std::uint64_t usable=0;for(auto n:x.hist)usable+=n;
        const auto percentile=[&](double q){if(!usable)return 0.;std::uint64_t n=0;const auto threshold=static_cast<std::uint64_t>(std::ceil(q*usable));
            for(std::size_t k=0;k<x.hist.size();++k){n+=x.hist[k];if(n>=threshold)return (k+1)*.25;}return 180.;};
        std::cout<<std::setprecision(10)<<"helper_complete_tone scope="<<scope<<" region="<<regions[r]<<" selection="<<selects[s]
          <<" receiver_feedback=0 probe_gauge=single_constant passed="<<x.count<<" phase_samples="<<usable<<" zero_clean_energy="<<x.zero_clean_energy<<" physical_disturbance="<<x.physical
          <<" clean_phase_rms_deg="<<(usable?std::sqrt(static_cast<double>(x.square/usable)):0)
          <<" clean_phase_p95_deg="<<percentile(.95)<<" clean_phase_p99_deg="<<percentile(.99)<<" clean_phase_worst_deg="<<x.worst
          <<" noisy_phase_composite_rms_deg="<<(usable&&std::string_view(scope)=="heldout"?std::sqrt(static_cast<double>(x.noisy/usable)):0)
          <<" fixed_lag_frequency_pairs="<<x.pairs<<" fixed_lag_per_pair_frequency_rms_hz="<<(x.pairs?std::sqrt(static_cast<double>(x.freq_square/x.pairs)):0)
          <<" fixed_lag_per_pair_frequency_worst_hz="<<x.freq_worst
          <<" fixed_lag_frequency_dwells="<<x.frequency_dwells<<" fixed_lag_per_dwell_frequency_rms_hz="<<(x.frequency_dwells?std::sqrt(static_cast<double>(x.dwell_frequency_square/x.frequency_dwells)):0)
          <<" fixed_lag_per_dwell_frequency_worst_hz="<<x.dwell_frequency_worst<<" frequency_lag_unaliased_abs_hz_lt=93.75\n";
    }
}
void print_physical_boundary_bins(const CompleteEvaluation& eval,const ContinuationSource& source,double gauge,double first,double end,const char* scope){
    // Diagnostic only: solve the simulator's actual RF-hop dates independently
    // after correction. Receiver boundary estimates never see these values.
    std::array<double,101> boundary{};boundary[100]=40.;
    for(unsigned hop=0;hop<100;++hop){double lo=hop*hp::dwell_seconds-.025,hi=hop*hp::dwell_seconds+.025;
        for(unsigned k=0;k<36;++k){const double mid=(lo+hi)/2;
            if(source.base().rf_hop_at_input(mid)<static_cast<int>(hop))lo=mid;else hi=mid;}
        boundary[hop]=(lo+hi)/2;}
    struct A {std::uint64_t count=0,usable=0,mismatch=0,pretransmission=0;long double square=0;double worst=0;std::array<std::uint64_t,720> hist{};};
    std::array<std::array<A,3>,6> values{};const auto& t=eval.output;
    for(std::size_t i=0;i<t.noisy.size();++i){const double date=static_cast<double>(t.input_dates[i]/fs);if(date<first||date>=end)continue;
        const auto it=std::lower_bound(boundary.begin(),boundary.end(),date);double distance=std::numeric_limits<double>::infinity();
        if(it!=boundary.end())distance=std::min(distance,std::abs(*it-date));if(it!=boundary.begin())distance=std::min(distance,std::abs(*std::prev(it)-date));
        const unsigned bin=distance<.0005?0:distance<.002?1:distance<.008?2:distance<.03?3:distance<.06?4:5;
        const unsigned selected=t.supported[i]?1:2;const int actual=source.base().rf_hop_at_input(date);
        const double expected=tau*std::remainder(hp::probe_frequency_hz*static_cast<double>(eval.output_seconds[i]),1.)+tau*(2400-hp::probe_frequency_hz)*gauge;
        const bool usable=t.physically_disturbed[i]<2&&std::norm(t.clean[i])>1e-12;
        const double phase=std::abs(wrap(std::arg(C(t.clean[i]))-expected))*180/std::numbers::pi;
        for(const auto selection:{0U,selected}){auto& a=values[bin][selection];++a.count;
            if(actual<0)++a.pretransmission;
            else if(eval.output_schedule_valid[i]&&static_cast<int>(eval.output_schedule_slot[i])!=actual)++a.mismatch;
            if(usable){++a.usable;a.square+=phase*phase;a.worst=std::max(a.worst,phase);
                ++a.hist[std::min<std::size_t>(719,static_cast<std::size_t>(phase*4))];}}
    }
    constexpr std::array<const char*,6> bins{"lt_0p5ms","0p5_to_2ms","2_to_8ms","8_to_30ms","30_to_60ms","interior"};
    constexpr std::array<const char*,3> selections{"all","supported","uncertain"};
    for(std::size_t r=0;r<bins.size();++r)for(std::size_t s=0;s<selections.size();++s){const auto& a=values[r][s];std::uint64_t seen=0;double p99=0;
        const auto target=static_cast<std::uint64_t>(std::ceil(.99*a.usable));if(target)for(std::size_t k=0;k<a.hist.size();++k){seen+=a.hist[k];if(seen>=target){p99=(k+1)*.25;break;}}
        std::cout<<std::setprecision(10)<<"helper_complete_physical_boundary scope="<<scope<<" bin="<<bins[r]<<" selection="<<selections[s]
          <<" evaluator_only=1 receiver_feedback=0 capture_end_is_boundary=1 passed="<<a.count<<" phase_samples="<<a.usable
          <<" wrong_hop_assignment="<<a.mismatch<<" pretransmission="<<a.pretransmission
          <<" clean_phase_rms_deg="<<(a.usable?std::sqrt(static_cast<double>(a.square/a.usable)):0)
          <<" clean_phase_p99_deg="<<p99<<" clean_phase_worst_deg="<<a.worst<<"\n";
    }
}
// Build a uniform evaluator grid only from samples that already lie on it;
// off-grid actual endpoints are omitted from APSK matched filtering, and any
// actual gap starts a new trace. No interpolation, alignment or branch fit.
struct UniformEvaluatorSegment {Trace trace;double offset_seconds=0;};
std::vector<UniformEvaluatorSegment> exact_uniform_segments(const CompleteEvaluation& eval,std::uint64_t& excluded){
    std::vector<UniformEvaluatorSegment> segments;const auto& t=eval.output;if(t.noisy.empty())return segments;
    require(eval.output_segments.size()==t.noisy.size()&&eval.output_segment_endpoint.size()==t.noisy.size(),"missing evaluator segment metadata");
    std::uint32_t segment=std::numeric_limits<std::uint32_t>::max();long double offset_samples=0;
    for(std::size_t i=0;i<t.noisy.size();++i){
        if(eval.output_segment_endpoint[i]){++excluded;continue;}
        const auto sample=eval.output_seconds[i]*fs;if(!std::isfinite(sample)||sample<0){++excluded;continue;}
        if(segment!=eval.output_segments[i]){segment=eval.output_segments[i];offset_samples=sample-std::floor(sample);
            require(segments.size()<101,"too many evaluator segments");segments.emplace_back();segments.back().offset_seconds=static_cast<double>(offset_samples/fs);}
        const auto shifted=sample-offset_samples,rounded=std::round(shifted);
        if(rounded<0||std::abs(shifted-rounded)>1e-4){++excluded;continue;}
        const auto index=static_cast<std::uint64_t>(rounded);
        auto* out=&segments.back().trace;
        if(out->noisy.empty())out->first=index;
        if(index!=out->first+out->noisy.size()){
            require(segments.size()<101,"too many disjoint evaluator segments");segments.emplace_back();segments.back().offset_seconds=static_cast<double>(offset_samples/fs);
            out=&segments.back().trace;out->first=index;
        }
        out->noisy.push_back(t.noisy[i]);out->clean.push_back(t.clean[i]);out->input_dates.push_back(t.input_dates[i]);
        out->supported.push_back(t.supported[i]);out->physically_disturbed.push_back(t.physically_disturbed[i]);
    }return segments;
}
void print_common_tone(const CompleteEvaluation& eval,const Stream& baseline,double gauge,const std::string& baseline_mode){
    // Nearest dates are accepted only within a fixed predeclared capture-date
    // tolerance; this is evaluator-only, with no phase/timing fit or feedback.
    constexpr long double tolerance=.05L;const auto& a=eval.output;const auto& b=baseline.trace;
    if(!baseline.initialized||b.input_dates.size()!=b.noisy.size()){
        std::cout<<"helper_complete_common_tone v8_baseline_mode="<<baseline_mode<<" available=0 reason=baseline_uninitialized\n";return;}
    std::size_t j=0;std::uint64_t count=0;long double aa=0,bb=0,max_date=0;double max_a=0,max_b=0;
    for(std::size_t i=0;i<a.noisy.size();++i){const auto pos=a.input_dates[i];if(pos<38*fs||pos>=40*fs||!a.supported[i])continue;
        while(j<b.input_dates.size()&&b.input_dates[j]<pos-tolerance)++j;
        if(j>=b.input_dates.size()||!b.supported[j]||std::abs(b.input_dates[j]-pos)>tolerance)continue;
        if(std::norm(a.clean[i])<=1e-12||std::norm(b.clean[j])<=1e-12||a.physically_disturbed[i]>=2||b.physically_disturbed[j]>=2)continue;
        const double ar=std::abs(wrap(std::arg(C(a.clean[i]))-tau*std::remainder(hp::probe_frequency_hz*static_cast<double>(eval.output_seconds[i]),1.)-
            tau*(2400-hp::probe_frequency_hz)*gauge));
        const double br=std::abs(wrap(std::arg(C(b.clean[j]))-tau*std::remainder(hp::probe_frequency_hz*static_cast<double>(b.first+j)/fs,1.)-
            tau*(2400-hp::probe_frequency_hz)*baseline.gauge));
        aa+=ar*ar;bb+=br*br;max_a=std::max(max_a,ar);max_b=std::max(max_b,br);max_date=std::max(max_date,std::abs(b.input_dates[j]-pos));++count;
    }
    const auto deg=180/std::numbers::pi;
    std::cout<<std::setprecision(10)<<"helper_complete_common_tone v8_baseline_mode="<<baseline_mode<<" available="<<(count>0)<<" samples="<<count
      <<" capture_date_tolerance_samples="<<static_cast<double>(tolerance)<<" max_date_difference_samples="<<static_cast<double>(max_date)
      <<" v9_clean_phase_rms_deg="<<(count?std::sqrt(static_cast<double>(aa/count))*deg:0)<<" v8_clean_phase_rms_deg="<<(count?std::sqrt(static_cast<double>(bb/count))*deg:0)
      <<" v9_worst_deg="<<max_a*deg<<" v8_worst_deg="<<max_b*deg
      <<" v9_fixed_gauge_seconds="<<gauge<<" v8_fixed_gauge_seconds="<<baseline.gauge<<" evaluator_only_no_adaptive_alignment=1\n";
}
}

int main(int argc,char** argv){try{
    verify_complete_reservations();
    const std::string duration=argc>1?argv[1]:"short";require(duration=="short"||duration=="full","duration must be short or full");
    const unsigned dwell_count=duration=="full"?100:10;
    hp::Config cfg;cfg.path_loss_db=argc>2?std::stod(argv[2]):180;cfg.seed=argc>3?std::stoull(argv[3]):1810000001ULL;
    cfg.expanded_schedule_key=true;cfg.noise_seed=cfg.seed+20'000'000ULL;
    const std::string mode=argc>4?argv[4]:"clean",probe=argc>5?argv[5]:"tone";
    const double delay=argc>6?std::stod(argv[6]):2.;const std::string rf=argc>7?argv[7]:"hf";
    const std::string boundary=argc>8?argv[8]:"nominal";
    require(boundary=="nominal"||boundary=="tracked"||boundary=="auxiliary"||boundary=="combined"||boundary=="pooled-inversion","invalid boundary method");
    const bool use_ramps=boundary=="tracked"||boundary=="combined";
    const bool use_inversion=boundary=="auxiliary"||boundary=="combined";
    const bool use_profile=boundary=="pooled-inversion";
    const std::string baseline_mode=argc>9?argv[9]:"full";require(baseline_mode=="full"||baseline_mode=="guarded","invalid v8 baseline mode");
    require(argc<=11,"too many complete-study arguments");const bool trace_plans=argc>10&&std::string_view(argv[10])=="trace-plan";
    if(argc>10)require(trace_plans,"invalid complete-study diagnostic flag");
    if(probe=="apsk")cfg.probe=hp::Probe::apsk64;else require(probe=="tone","invalid probe");
    if(rf=="microwave"||rf=="microwave-eu"){cfg.shift_center_hz=5.8e9;cfg.relative_shift_ppm=.00008;
        cfg.phase_noise_degrees_per_sqrt_second=.005;cfg.microwave_eu20=rf=="microwave-eu";}else require(rf=="hf","invalid RF fixture");
    if(mode=="aligned")cfg.arrival_offset_seconds=0;
    if(mode=="boundary-outside")cfg.arrival_offset_seconds=.0098;
    if(mode=="rf-retune-early")cfg.rf_retune_offset_seconds=-.002;
    if(mode=="rf-retune-late")cfg.rf_retune_offset_seconds=.002;
    if(mode=="drift"){cfg.tx_audio_drift_ppm_per_second=.04;cfg.relative_shift_drift_ppm_per_second=.00002;}
    if(mode=="negative-clock"){cfg.tx_audio_ppm=-37;cfg.rx_audio_ppm=13;}
    if(mode=="no-pilot-ramps")cfg.pilot_dwell_ramps=false;
    if(mode=="no-auxiliary")cfg.auxiliary_enabled=false;
    if(mode=="straight-auxiliary")cfg.auxiliary_punctuation=false;
    const bool adc_gap=mode=="adc-gap"||mode=="adc-gap-probe",clock_adjust=mode=="clock-adjust";
    const bool nominal_impairment=mode=="aligned"||mode=="boundary-outside"||mode=="rf-retune-early"||mode=="rf-retune-late"||adc_gap||clock_adjust||mode=="retune-erasure"||mode=="retune-erasure-45"||mode=="no-pilot-ramps"||mode=="no-auxiliary"||mode=="straight-auxiliary";
    const auto imp=nominal_impairment?ht::Impairments{}:impairment_named(mode);
    const auto setup=cpu();ContinuationSource source(cfg,imp,mode=="frequency-pulse",mode=="scaled-step",36.8,
        mode=="gap-evolution"?.04:(mode=="gap-evolution-outside"?.6:0),mode=="gap-clock-cycle"?1./1850:0,
        mode=="second-step",mode=="step-invalid"?95:100,
        mode.starts_with("transient-")?std::string_view(mode).substr(10):std::string_view{});
    const double setup_cpu=cpu()-setup,max_rf=cfg.shift_center_hz>1e9?(cfg.microwave_eu20?5868.4e6:5846.8e6):31.225e6;
    const double prior_ppm=cfg.shift_center_hz>1e9?.0001:1,width=3*(max_rf*prior_ppm+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,cfg.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
    CompleteHelperHistory history(mc,cfg.shift_center_hz,delay);
    history.trace_plans=trace_plans;
    CompleteBoundaryHistory boundary_history;
    CompleteInversionHistory inversion_history;
    CompleteProfileHistory profile_history(use_profile);
    CompleteConditioner::Config cc;cc.delay_nanoseconds=static_cast<std::uint64_t>(std::llround(delay*1e9));
    CompleteConditioner receiver(cc),clean_twin(cc);
    // Same-capture v8 full-dwell baseline by default; the former guarded
    // baseline remains independently selectable for matched comparisons.
    DelayedRun baseline(baseline_mode=="full"?"v8-full":"v8-guarded",mc,cfg.shift_center_hz,delay,true);baseline.edge_inner=360;
    baseline.full=baseline_mode=="full";baseline.chain.stream.best_effort_full_dwell=baseline.full;
    baseline.capture_rate_scale=source.base().rx_scale();baseline.capture_fraction=cfg.fractional_input_sample;baseline.system_step=clock_adjust;
    std::array<CompletePending,100> pending{};std::array<CompletePlan,100> plans{};std::array<bool,100> plan_cached{};
    std::array<CompleteConditioner::CaptureBlock,100> capture_metadata{};long double latest_helper_observation_end_sample=0;
    std::array<CompleteConditioner::CorrectionEpoch,100> epoch_cache{};
    std::array<CompleteConditioner::CorrectionEpoch,3> selected_epochs{};
    std::array<std::uint64_t,101> boundary_sample{};std::array<double,101> boundary_radius{};std::array<bool,101> boundary_frozen{};
    std::array<std::uint64_t,101> inversion_evidence_end{};
    boundary_frozen[0]=true;boundary_sample[0]=0;boundary_radius[0]=.008*fs;
    boundary_frozen[dwell_count]=true;boundary_sample[dwell_count]=static_cast<std::uint64_t>(dwell_count)*hp::dwell_samples;boundary_radius[dwell_count]=.008*fs;
    CompleteEvaluation eval;double latest_frontier_seconds=0,component_cpu=0,twin_cpu=0,generation_cpu=0,evaluator_generation_cpu=0,history_evaluator_cpu=0;
    double receiver_cpu36=0,generation_cpu36=0,buffer_push_cpu=0,measurement_cpu36=0,profile_cpu36=0,conditioning_cpu36=0;
    std::uint64_t conditioned_inputs36=0;bool releasing_eof=false;std::uint64_t latest_frontier_ns=0,last_now=0;std::size_t plan_cursor=0,observed_dwells=0;
    const auto ensure_plan=[&](std::size_t target){
      require(target<observed_dwells,"correction requested before target Helper capture");
      while(plan_cursor<=target){const auto i=plan_cursor++;
            plans[i]=history.plan(pending[i],latest_frontier_seconds,releasing_eof);plan_cached[i]=true;
            const auto& p=plans[i];auto& ep=epoch_cache[i];ep.first_sample=i*hp::dwell_samples;ep.end_sample=(i+1)*hp::dwell_samples;ep.schedule_slot=i;
            ep.anchor_input_sample=p.anchor_seconds*fs;ep.audio_scale=p.audio_scale;ep.common_frequency_hz=p.common_frequency_hz;
            ep.audio_drift_ppm_per_second=p.audio_drift_ppm_per_second;
            ep.measured_pilot_phase_radians=p.high_phase_radians;ep.phase_valid=p.phase_available;ep.clock_valid=p.frequency_available;ep.model_qualified=p.qualified;
            ep.phase_support_first_sample=p.supported_first;ep.phase_support_end_sample=p.supported_end;
            ep.boundary_half_width_samples=.008*fs;ep.phase_sigma_radians=p.qualified?pending[i].phase.high_sigma:1000;
            ep.frequency_half_width_hz=p.qualified?p.transport_rate/tau:1000;ep.audio_half_width_ppm=p.qualified?p.audio_half_width_ppm:10000;
            if(p.qualified){
                // Same conditional probe-band budget as the guarded v8 path;
                // actual best-effort mixing is independent of this authority.
                // The fitted source-time uncertainty and actual conditioner
                // discrepancy are distinct. The component admits only points
                // whose discrepancy is within this model radius; a factor two
                // encloses both without treating them as independent errors.
                const double b0=3*pending[i].phase.high_sigma+tau*980*2*p.source_half_width_seconds;
                if(!(b0<.5))ep.model_qualified=false;
                else{
                    const double anchor=p.anchor_seconds*fs;
                    double lo=0,hi=hp::dwell_seconds;
                    for(int k=0;k<28;++k){const double mid=(lo+hi)/2;
                        // Actual map discrepancy can grow with the audio-rate
                        // uncertainty as well as the fitted source bound. The
                        // quadratic nominal-pilot audio-drift term is enclosed
                        // separately; RF drift can cancel it in the measured
                        // high-pilot frequency range.
                        if(b0+(p.transport_rate+tau*980*p.audio_half_width_ppm*1e-6)*mid+
                            std::numbers::pi*2400*std::abs(p.audio_drift_ppm_per_second)*1e-6*mid*mid+
                            4.5*p.diffusion_q*std::sqrt(mid)<.5)lo=mid;else hi=mid;}
                    ep.phase_support_first_sample=std::max(ep.phase_support_first_sample,static_cast<std::uint64_t>(std::max(0.,std::ceil(anchor-lo*fs))));
                    ep.phase_support_end_sample=std::min(ep.phase_support_end_sample,static_cast<std::uint64_t>(std::max(0.,std::floor(anchor+lo*fs))));
                    if(ep.phase_support_first_sample>=ep.phase_support_end_sample)ep.model_qualified=false;
                }
            }
            ep.latest_helper_observation_end_sample=std::max<long double>(pending[i].phase.valid?(pending[i].first_seconds+static_cast<double>(settle+aperture)/fs)*fs:0.,
                latest_helper_observation_end_sample);
            if(ep.latest_helper_observation_end_sample>0){
                const long double last=std::ceil(ep.latest_helper_observation_end_sample)-1;
                const auto b=static_cast<std::size_t>(last/hp::dwell_samples);require(b<dwell_count&&capture_metadata[b].sample_count,"missing Helper observation timestamp");
                const auto& cap=capture_metadata[b];const long double delta=last-cap.first_sample;
                const long double end_ns=cap.capture_monotonic_nanoseconds+delta*cap.sample_period_nanoseconds+
                    cap.capture_monotonic_uncertainty_nanoseconds+delta*cap.sample_period_uncertainty_nanoseconds;
                ep.latest_helper_observation_monotonic_nanoseconds=static_cast<std::uint64_t>(std::ceil(end_ns));
            }
            ep.source_time_valid=p.source_available;ep.source_seconds_at_anchor=p.source_seconds_at_anchor;
            ep.source_half_width_seconds=p.source_half_width_seconds;ep.reference_input_sample=p.reference_seconds*fs;
      }
    };
    const auto freeze_boundary=[&](std::size_t i){
        require(i<=dwell_count,"invalid boundary index");if(boundary_frozen[i])return;
        const double predicted=use_ramps?i*hp::dwell_samples/boundary_history.audio_scale:i*hp::dwell_samples;
        const double shift=use_ramps&&boundary_history.available?boundary_history.epoch_offset_samples:0;
        double point=std::clamp(predicted+shift,0.,static_cast<double>(dwell_count)*hp::dwell_samples);
        if(use_inversion){const auto* marker=inversion_history.find(static_cast<unsigned>(i));
            if(marker&&marker->usable){point=marker->boundary_sample;
                inversion_evidence_end[i]=marker->observation.last_sample+1;++inversion_history.selected_boundaries;}}
        if(use_profile){const auto result=profile_history.estimate(static_cast<unsigned>(i));
            if(result.qualified&&std::isfinite(result.dwell_start_input_sample)&&
                result.conditional_half_width_seconds<.008&&
                std::abs(result.dwell_start_input_sample-i*hp::dwell_samples)+result.conditional_half_width_seconds*fs<=.008*fs){
                point=static_cast<double>(result.dwell_start_input_sample);
                inversion_evidence_end[i]=result.latest_observation_end_sample;++profile_history.selected;
            }}
        // A fractional auxiliary boundary selects the first input sample at
        // or after the new hop. Keep the historical baseline rounding intact.
        boundary_sample[i]=static_cast<std::uint64_t>(inversion_evidence_end[i]?std::ceil(point):std::llround(point));
        // A data-selected point must not silently move/shrink the original
        // ±8 ms prior. This symmetric interval encloses the original prior.
        const double nominal=static_cast<double>(i)*hp::dwell_samples;
        boundary_radius[i]=.008*fs+std::max(std::abs(point-predicted),std::abs(point-nominal))+1;boundary_frozen[i]=true;
    };
    const auto provider=[&](const CompleteConditioner::ReleasedBlock& release)->std::span<const CompleteConditioner::CorrectionEpoch>{
        const auto i=static_cast<std::size_t>(release.capture.first_sample/hp::dwell_samples);require(i<dwell_count,"unexpected correction block");
        freeze_boundary(i);freeze_boundary(i+1);
        const auto block_first=release.capture.first_sample,block_end=block_first+release.capture.sample_count;
        std::size_t count=0;
        const auto add=[&](std::size_t target){
            if(target>=dwell_count||target>=observed_dwells)return;
            freeze_boundary(target);freeze_boundary(target+1);ensure_plan(target);
            auto ep=epoch_cache[target];ep.first_sample=boundary_sample[target];ep.end_sample=boundary_sample[target+1];
            ep.boundary_half_width_samples=std::max(boundary_radius[target],boundary_radius[target+1]);
            const double boundary_frontier=std::max(use_ramps&&boundary_history.available?boundary_history.latest_observation_end_sample:0.,
                static_cast<double>(std::max(inversion_evidence_end[target],inversion_evidence_end[target+1])));
            if(boundary_frontier>0){
                ep.latest_helper_observation_end_sample=std::max<long double>(ep.latest_helper_observation_end_sample,boundary_frontier);
                if(ep.latest_helper_observation_end_sample>0){const long double last=std::ceil(ep.latest_helper_observation_end_sample)-1;
                    const auto b=static_cast<std::size_t>(last/hp::dwell_samples);require(b<dwell_count&&capture_metadata[b].sample_count,"missing boundary observation timestamp");
                    const auto& cap=capture_metadata[b];const long double d=last-cap.first_sample;
                    ep.latest_helper_observation_monotonic_nanoseconds=static_cast<std::uint64_t>(std::ceil(cap.capture_monotonic_nanoseconds+
                        d*cap.sample_period_nanoseconds+cap.capture_monotonic_uncertainty_nanoseconds+d*cap.sample_period_uncertainty_nanoseconds));
                }
            }
            if(ep.first_sample<ep.end_sample&&ep.first_sample<block_end&&ep.end_sample>block_first){
                require(count<selected_epochs.size(),"too many correction epochs");selected_epochs[count++]=ep;}
        };
        if(i&&boundary_sample[i]>block_first)add(i-1);
        add(i);
        if(boundary_sample[i+1]<block_end)add(i+1);
        return std::span<const CompleteConditioner::CorrectionEpoch>(selected_epochs.data(),count);
    };
    const auto release=[&](std::uint64_t now,bool eof){
        releasing_eof=eof;last_now=now;eval.begin_release();const auto measured_before=history.chain.receiver.total_cpu();const auto t=cpu();
        const auto no=receiver.release(now,latest_frontier_ns,provider,[&](const CompleteEvent& e){
            if(e.samples_present&&!eval.evaluator_reference_initialized){
                require(history.fixed_reference_chosen,"no fixed receiver reference before first output");
                const auto chosen=receiver.chosen_reference_input_sample();
                if(!chosen||std::abs(*chosen-history.fixed_reference_seconds*fs)>=1e-9L){
                    const auto& p=epoch_cache[0];
                    std::cerr<<std::setprecision(12)<<"complete initial reference diagnostics desired_samples="<<history.fixed_reference_seconds*fs
                      <<" chosen_samples="<<(chosen?static_cast<double>(*chosen):-1)<<" first_phase_valid="<<p.phase_valid
                      <<" first_clock_valid="<<p.clock_valid<<" first_source_valid="<<p.source_time_valid
                      <<" first_ref_samples="<<static_cast<double>(p.reference_input_sample)
                      <<" first_observation_end_sample="<<static_cast<double>(p.latest_helper_observation_end_sample)
                      <<" first_observation_end_mono_ns="<<p.latest_helper_observation_monotonic_nanoseconds
                      <<" helper_frontier_ns="<<latest_frontier_ns<<" first_audio="<<p.audio_scale<<" first_drift="<<p.audio_drift_ppm_per_second
                      <<" first_frequency="<<p.common_frequency_hz<<" first_source="<<static_cast<double>(p.source_seconds_at_anchor)<<"\n";
                }
                require(chosen&&std::abs(*chosen-history.fixed_reference_seconds*fs)<1e-9L,
                    "conditioner chose a different initial relative reference");
                eval.evaluator_reference_seconds=history.fixed_reference_seconds;
                eval.evaluator_gauge_seconds=history.fixed_reference_seconds-source.tx_time(history.fixed_reference_seconds);
                eval.evaluator_reference_initialized=true;
            }
            eval.noisy(e,source,cfg,eof,latest_frontier_seconds,clock_adjust);
        },eof);
        // Snapshot calls made by the provider are already counted in the
        // receiver's own CPU counters; subtract that overlapping interval.
        component_cpu+=cpu()-t-(history.chain.receiver.total_cpu()-measured_before);const auto q=cpu();
        const auto yes=clean_twin.release(now,latest_frontier_ns,provider,[&](const CompleteEvent& e){eval.clean(e,source);},eof);
        twin_cpu+=cpu()-q;require(no==yes,"paired complete stream event count mismatch");eval.end_release();
    };
    std::unique_ptr<h::Generator> erasure_generator;
    if(mode=="retune-erasure"||mode=="retune-erasure-45"){
        h::Options o;o.power_w=cfg.helper_w;o.data_power_w=1;o.callsign=cfg.callsign;
        erasure_generator=std::make_unique<h::Generator>(o,h::Plan{2400,.4,0,cfg.helper_w,950,550,cfg.hopping},cfg.epoch,true);
    }
    const double erasure_seconds=mode=="retune-erasure-45"?.045:.03;
    if(erasure_generator)eval.evaluator_erasure_seconds=erasure_seconds;
    for(unsigned i=0;i<dwell_count;++i){
        const auto g=cpu();auto d=source.generate(i);
        if(erasure_generator)for(std::size_t j=0;j<static_cast<std::size_t>(std::llround(erasure_seconds*fs));++j){
            const double date=d.first_seconds+static_cast<double>(j)/fs;
            const C helper=erasure_generator->mix_analytic({},source.tx_time(date),fs,40.L,0.L)/(.4*h::pilot_amplitude_fraction);
            d.samples[j]=S(C(d.samples[j])-helper*C(d.common_phasor[j])-C(d.clean_probe[j]));d.clean_probe[j]={};
        }
        generation_cpu+=cpu()-g;
        if(probe=="tone"&&mode!="noise"&&i<95){const auto t=cpu();for(std::size_t j=0;j<d.clean_probe.size();++j){
            const double date=d.first_seconds+static_cast<double>(j)/fs;const double tx=source.tx_time(date);
            const bool erased=erasure_generator&&j<static_cast<std::size_t>(std::llround(erasure_seconds*fs));
            const C z=tx<0||erased?C{}:source.base().probe_amplitude()*std::polar(1.,tau*std::remainder(hp::probe_frequency_hz*tx,1.))*C(d.common_phasor[j]);d.clean_probe[j]=S(z);
        }evaluator_generation_cpu+=cpu()-t;}
        const unsigned adc_gap_dwell=mode=="adc-gap-probe"?std::min(96U,dwell_count-1):std::min(50U,dwell_count/2);
        const bool missing=source.missing(i)||(adc_gap&&i==adc_gap_dwell);
        pending[i]=history.observe(d,missing,i);observed_dwells=i+1;
        {const auto t=cpu();history.chain.evaluate(source,d,i);history_evaluator_cpu+=cpu()-t;}
        if(!missing&&history.chain.receiver.measurement.valid)
            latest_helper_observation_end_sample=history.chain.receiver.measurement.end_sample_time_seconds*fs;
        if(use_ramps)boundary_history.observe(d,missing,i,history,source);
        if(use_inversion)inversion_history.observe(d,missing,i,pending[i],history,source);
        if(use_profile)profile_history.observe(d,missing,i,pending[i],history,source);
        // Baseline has no missing-ADC facility; in that control it is omitted.
        if(!adc_gap)baseline.observe(d,missing,i,source);
        CompleteConditioner::CaptureBlock cap;cap.first_sample=static_cast<std::uint64_t>(i)*hp::dwell_samples;cap.sample_count=hp::dwell_samples;
        cap.samples_present=!(adc_gap&&i==adc_gap_dwell);
        const long double physical=(static_cast<long double>(cap.first_sample)+cfg.fractional_input_sample)*1e9L/(fs*source.base().rx_scale());
        cap.capture_monotonic_nanoseconds=1'000'000'000ULL+static_cast<std::uint64_t>(std::llround(physical));
        cap.capture_system_nanoseconds=1'700'000'000'000'000'000LL+static_cast<std::int64_t>(std::llround(physical));
        cap.sample_period_nanoseconds=1e9L/(fs*source.base().rx_scale());cap.capture_uncertainty_nanoseconds=100;
        cap.capture_monotonic_uncertainty_nanoseconds=100;cap.sample_period_uncertainty_nanoseconds=cap.sample_period_nanoseconds*1e-7;cap.capture_clock_epoch=1;
        if(clock_adjust&&i>=50){cap.capture_system_nanoseconds-=2'000'000'000LL;cap.capture_clock_epoch=2;}
        capture_metadata[i]=cap;
        const auto t=cpu();const auto push=receiver.push(cap,cap.samples_present?std::span<const S>(d.samples):std::span<const S>{});const double pushed_cpu=cpu()-t;component_cpu+=pushed_cpu;buffer_push_cpu+=pushed_cpu;
        const auto tc=cpu();const auto push_clean=clean_twin.push(cap,cap.samples_present?std::span<const S>(d.clean_probe):std::span<const S>{});twin_cpu+=cpu()-tc;
        require(push==h::HelperDelayedBuffer::PushResult::accepted&&push==push_clean,"complete stream buffer push rejected");
        latest_frontier_seconds=(i+1)*hp::dwell_seconds;latest_frontier_ns=cap.capture_monotonic_nanoseconds+
            static_cast<std::uint64_t>(std::ceil((cap.sample_count-1)*cap.sample_period_nanoseconds));
        release(latest_frontier_ns,false);
        if(!adc_gap)baseline.release(latest_frontier_ns,latest_frontier_ns,latest_frontier_seconds,i+1==dwell_count,source);
        if(i==89){inversion_history.available36=inversion_history.markers;
            measurement_cpu36=history.chain.receiver.total_cpu()+history.chain.extra_cpu;
            profile_cpu36=profile_history.cpu_seconds;conditioning_cpu36=component_cpu;
            receiver_cpu36=history.chain.receiver.total_cpu()+history.chain.extra_cpu+component_cpu+boundary_history.cpu_seconds+inversion_history.cpu_seconds+profile_history.cpu_seconds;generation_cpu36=generation_cpu;conditioned_inputs36=receiver.captured_samples_processed();}
    }
    // Age the fixed-capacity buffer without claiming any future Helper samples.
    for(unsigned k=0;k<12&&receiver.queued_blocks();++k){last_now=latest_frontier_ns+static_cast<std::uint64_t>((k+1)*400'000'000ULL);release(last_now,true);}
    require(receiver.queued_blocks()==0&&clean_twin.queued_blocks()==0,"complete delayed queue did not drain");
    if(!adc_gap){for(unsigned k=0;k<12&&baseline.buffer.queued_blocks();++k)
        baseline.release(latest_frontier_ns+static_cast<std::uint64_t>((k+1)*400'000'000ULL),latest_frontier_ns,latest_frontier_seconds,true,source);
        require(!baseline.buffer.queued_blocks(),"baseline queue did not drain");baseline.drain_conditioner();}
    // Explicit flush of the last interpolation endpoint uses the real last
    // sample, never a fabricated zero or a false later Helper frontier.
    release(last_now,true);
    const auto metrology_start=cpu();
    // One fixed evaluator gauge was established at the receiver-selected
    // reference before the first payload. It never feeds receiver correction.
    const double evaluator_gauge=eval.evaluator_gauge_seconds;
    if(probe=="tone"){
        if(duration=="full"){print_complete_tone(eval,evaluator_gauge,38,40,"heldout");
            print_physical_boundary_bins(eval,source,evaluator_gauge,38,40,"heldout");}
        if(mode!="noise"){print_complete_tone(eval,evaluator_gauge,0,dwell_count*hp::dwell_seconds,"continuous_synthetic_clean_twin");
            print_physical_boundary_bins(eval,source,evaluator_gauge,0,dwell_count*hp::dwell_seconds,"continuous_synthetic_clean_twin");}
        if(!adc_gap&&duration=="full"){
            print_common_tone(eval,baseline.chain.stream,evaluator_gauge,baseline_mode);
            if(baseline.chain.stream.initialized){helper_complete_stream_metrics::Options bopt;bopt.fixed_gauge_seconds=baseline.chain.stream.gauge;
                const auto b=helper_complete_stream_metrics::evaluate(baseline.chain.stream.trace,bopt);
                constexpr std::array<const char*,4> regions{"beginning","interior","end","boundary"};
                constexpr std::array<const char*,3> selections{"all","supported","uncertain"};
                for(std::size_t r=0;r<regions.size();++r)for(std::size_t s=0;s<selections.size();++s){const auto& z=b.region[r][s];
                    std::cout<<std::setprecision(10)<<"helper_complete_tone_v8 v8_baseline_mode="<<baseline_mode<<" region="<<regions[r]<<" selection="<<selections[s]
                      <<" scope=heldout receiver_feedback=0 passed="<<z.passed<<" phase_samples="<<z.phase_samples
                      <<" clean_phase_rms_deg="<<z.phase_rms_deg<<" clean_phase_p95_deg="<<z.phase_p95_deg
                      <<" clean_phase_p99_deg="<<z.phase_p99_deg<<" clean_phase_worst_deg="<<z.phase_worst_deg
                      <<" fixed_lag_frequency_dwells="<<z.frequency_dwells<<" fixed_lag_frequency_rms_hz="<<z.frequency_rms_hz<<"\n";
                }}
        }
    }
    if(probe=="apsk"&&duration=="full"){
        std::uint64_t excluded=0;const auto segments=exact_uniform_segments(eval,excluded);
        const auto pulse=[](double t){return datapump::fast::root_raised_cosine(t,.2);};
        const auto symbol=[&](unsigned k){return source.base().symbol(k);};
        constexpr std::array<const char*,5> regions{"beginning","interior","end","boundary","all"};
        constexpr std::array<const char*,3> selections{"all","supported","uncertain"};
        std::array<std::array<helper_complete_apsk_metrics::Statistic,3>,5> combined{};
        std::uint64_t attempted=0,unavailable=0,uniform_samples=0;
        for(const auto& segment:segments){helper_complete_apsk_metrics::Options opt;opt.fixed_gauge_seconds=evaluator_gauge;
            opt.output_time_offset_seconds=segment.offset_seconds;uniform_samples+=segment.trace.noisy.size();
            const auto result=helper_complete_apsk_metrics::evaluate(segment.trace,opt,source.base().probe_amplitude(),pulse,symbol);
            attempted+=result.considered;unavailable+=result.footprint_unavailable;
            for(std::size_t r=0;r<regions.size();++r)for(std::size_t s=0;s<selections.size();++s){auto& z=combined[r][s];const auto& y=result.region[r][s];
                z.symbols+=y.symbols;z.footprint_crosses_nominal_hop+=y.footprint_crosses_nominal_hop;
                z.footprint_boundary_ambiguous+=y.footprint_boundary_ambiguous;z.reference_power+=y.reference_power;
                z.clean_error_power+=y.clean_error_power;z.noisy_error_power+=y.noisy_error_power;}
        }
        for(std::size_t r=0;r<regions.size();++r)for(std::size_t s=0;s<selections.size();++s){const auto& z=combined[r][s];
            std::cout<<std::setprecision(10)<<"helper_complete_apsk_v9 region="<<regions[r]<<" selection="<<selections[s]
              <<" evaluator_scope=all_contiguous_metadata_grids offgrid_or_endpoint_samples_excluded="<<excluded<<" uniform_grid_samples="<<uniform_samples
              <<" segment_count="<<segments.size()<<" receiver_feedback=0 probe_gauge=single_constant unique_symbols_considered=584"
              <<" segment_symbol_attempts="<<attempted<<" unavailable_segment_footprint_attempts="<<unavailable<<" symbols="<<z.symbols
              <<" clean_twin_evm="<<(z.reference_power>0?std::sqrt(z.clean_error_power/z.reference_power):0)
              <<" noisy_composite_evm="<<(z.reference_power>0?std::sqrt(z.noisy_error_power/z.reference_power):0)
              <<" footprint_crosses_nominal_hop="<<z.footprint_crosses_nominal_hop
              <<" footprint_boundary_ambiguous="<<z.footprint_boundary_ambiguous<<"\n";}
        if(!adc_gap&&baseline.chain.stream.initialized){helper_complete_apsk_metrics::Options bopt;bopt.fixed_gauge_seconds=baseline.chain.stream.gauge;
            const auto b=helper_complete_apsk_metrics::evaluate(baseline.chain.stream.trace,bopt,source.base().probe_amplitude(),pulse,symbol);
            for(std::size_t r=0;r<regions.size();++r)for(std::size_t s=0;s<selections.size();++s){const auto& z=b.region[r][s];
                std::cout<<std::setprecision(10)<<"helper_complete_apsk_v8 v8_baseline_mode="<<baseline_mode<<" region="<<regions[r]<<" selection="<<selections[s]
                  <<" receiver_feedback=0 symbols="<<z.symbols<<" clean_twin_evm="<<z.clean_evm<<" noisy_composite_evm="<<z.noisy_evm<<"\n";}}
    }
    const double metrology_cpu=cpu()-metrology_start;
    const auto& frozen=history.chain.frozen;
    std::cout<<std::setprecision(12)<<"helper_complete_study mode="<<mode<<" duration="<<duration<<" seed="<<cfg.seed<<" noise_seed="<<*cfg.noise_seed
      <<" v8_baseline_mode="<<baseline_mode<<" v8_baseline_available="<<(!adc_gap&&baseline.chain.stream.initialized)
      <<" helper_w="<<cfg.helper_w<<" probe_w="<<cfg.probe_w<<" combined_w="<<cfg.helper_w+cfg.probe_w<<" loss_db="<<cfg.path_loss_db
      <<" helper_power_reference="<<((mode=="no-pilot-ramps"||mode=="no-auxiliary"||mode=="straight-auxiliary")?"standard_waveform_variant_energy_changes":"actual_standard_waveform")
      <<" temperature_K="<<cfg.temperature_kelvin<<" noise_figure_db="<<cfg.noise_figure_db<<" schedule="<<schedule(cfg)
      <<" boundary_prior=nominal_zero boundary_method="<<boundary<<" prior_half_width_ms=8 synthetic_arrival_offset_ms="<<cfg.arrival_offset_seconds*1000
      <<" synthetic_rf_retune_offset_ms="<<cfg.rf_retune_offset_seconds*1000
      <<" boundary_tracker_attempts="<<boundary_history.attempts<<" boundary_tracker_accepted="<<boundary_history.accepted
      <<" boundary_evaluator_count="<<boundary_history.evaluator_count
      <<" boundary_nominal_rms_ms="<<(boundary_history.evaluator_count?1000*std::sqrt(static_cast<double>(boundary_history.nominal_error_square/boundary_history.evaluator_count))/fs:0)
      <<" boundary_tracked_rms_ms="<<(boundary_history.evaluator_count?1000*std::sqrt(static_cast<double>(boundary_history.tracked_error_square/boundary_history.evaluator_count))/fs:0)
      <<" boundary_nominal_worst_ms="<<1000*boundary_history.nominal_error_worst/fs<<" boundary_tracked_worst_ms="<<1000*boundary_history.tracked_error_worst/fs
      <<" inversion_attempts="<<inversion_history.attempts<<" inversion_markers="<<inversion_history.markers
      <<" inversion_markers_available36="<<inversion_history.available36<<" inversion_usable="<<inversion_history.usable
      <<" inversion_selected_boundaries="<<inversion_history.selected_boundaries<<" inversion_recenters="<<inversion_history.recenters<<" inversion_gaps="<<inversion_history.gaps
      <<" inversion_evaluator_count="<<inversion_history.evaluator_count<<" inversion_interval_covered="<<inversion_history.covered
      <<" inversion_rms_us="<<(inversion_history.evaluator_count?1e6*std::sqrt(inversion_history.error_square/inversion_history.evaluator_count):0)
      <<" inversion_worst_us="<<inversion_history.error_worst*1e6<<" inversion_max_radius_us="<<inversion_history.radius_max*1e6
      <<" inversion_max_publication_latency_seconds="<<inversion_history.latency_max
      <<" profile_attempts="<<profile_history.attempts<<" profile_stored="<<profile_history.stored
      <<" profile_local_qualified="<<profile_history.dwell_qualified<<" profile_calendar_qualified_updates="<<profile_history.pooled_qualified
      <<" profile_selected_boundaries="<<profile_history.selected<<" profile_missing_dwells="<<profile_history.missing
      <<" profile_eligible_local_boundaries="<<profile_history.local_selected<<" profile_eligible_pooled_boundaries="<<profile_history.pooled_selected
      <<" profile_calendar_updates_scored="<<profile_history.scored<<" profile_calendar_interval_covered="<<profile_history.covered
      <<" profile_calendar_rms_us="<<(profile_history.scored?1e6*std::sqrt(profile_history.error_square/profile_history.scored):0)
      <<" profile_calendar_worst_us="<<profile_history.worst_error*1e6<<" profile_max_radius_us="<<profile_history.maximum_radius*1e6
      <<" profile_within_25us="<<profile_history.within[0]<<" profile_within_125us="<<profile_history.within[1]
      <<" profile_within_1ms="<<profile_history.within[2]<<" profile_within_10ms="<<profile_history.within[3]
      <<" profile_local_interval_covered="<<profile_history.local_covered
      <<" profile_local_rms_us="<<(profile_history.local_scored?1e6*std::sqrt(profile_history.local_error_square/profile_history.local_scored):0)
      <<" profile_local_worst_us="<<profile_history.local_worst_error*1e6
      <<" profile_first_qualified_capture_end_seconds="<<profile_history.first_qualified_capture_end
      <<" profile36_available="<<profile_history.frozen36.available<<" profile36_qualified="<<profile_history.frozen36.qualified
      <<" profile36_error_us="<<profile_history.frozen36_error*1e6
      <<" profile36_radius_us="<<profile_history.frozen36.conditional_half_width_seconds*1e6
      <<" profile36_observations="<<profile_history.frozen36.observations
      <<" profile36_latest_observation_end_sample="<<profile_history.frozen36.latest_observation_end_sample
      <<" observation_seconds="<<(duration=="full"?36:0)<<" lead_seconds=38 capture_seconds="<<dwell_count*hp::dwell_seconds
      <<" delay_seconds="<<delay<<" physical_gap="<<adc_gap<<" output_samples="<<eval.conditioned_samples
      <<" missing_events="<<eval.missing_events<<" missing_samples="<<eval.missing_samples<<" processed_captured_samples="<<receiver.captured_samples_processed()
      <<" emitted_before_eof="<<eval.outputs_before_eof<<" emitted_before_capture36="<<eval.outputs_before_36
      <<" phase_applied="<<eval.phase_known<<" clock_applied="<<eval.clock_known<<" well_supported="<<eval.well<<" boundary_ambiguous="<<eval.ambiguous
      <<" delayed_pre36_max_helper_end_seconds="<<static_cast<double>(eval.max_helper_end_for_pre36/fs)<<" pre36_samples_using_post36_helper="<<eval.pre36_using_post36
      <<" heldout_max_helper_end_seconds="<<static_cast<double>(eval.max_helper_end_for_heldout/fs)
      <<" timestamp_unavailable="<<eval.capture_timestamp_unavailable<<" tx_time_unavailable="<<eval.tx_unavailable
      <<" timestamp_failures="<<eval.synthetic_time_failures<<" reference_mismatches="<<eval.reference_mismatch
      <<" physical_schedule_mismatches="<<eval.physical_schedule_mismatch<<" supported_schedule_mismatches="<<eval.supported_schedule_mismatch
      <<" pretransmission_samples="<<eval.pretransmission_samples
      <<" capture_utc_vs_release_slot_differences="<<eval.utc_vs_release_slot_differences
      <<" relative_timing_max_abs_us="<<eval.timing_max_abs*1e6<<" relative_timing_change_us="<<(eval.timing_last-eval.timing_first)*1e6
      <<" timing_cycle_slips="<<eval.timing_cycle_slips<<" timing_cycle_counter_definition=truth_error_cell_crossings_not_estimator_branch_slips"
      <<" model_discrepancy_max_abs_us="<<eval.model_discrepancy_max_abs*1e6
      <<" capture_to_release_min_seconds="<<static_cast<double>(eval.min_latency_ns*1e-9L)<<" capture_to_release_max_seconds="<<static_cast<double>(eval.max_latency_ns*1e-9L)
      <<" relative_time_reference=one_constant_unknown estimator_model="<<(frozen.valid?"available":"unavailable")
      <<" frequency36_available="<<frozen.valid<<" frequency36_error_hz="<<history.chain.frozen_error
      <<" frequency36_conditional_half_width_hz="<<frozen.half_width
      <<" frequency36_interval_covered="<<(frozen.valid&&std::abs(history.chain.frozen_error)<=frozen.half_width)
      <<" frequency36_thermal_sigma_hz="<<frozen.thermal_sigma<<" frequency36_process_sigma_hz="<<frozen.process_sigma<<" frequency36_model_half_width_hz="<<frozen.model_half
      <<" later_frozen_frequency_count="<<history.chain.heldout_count<<" later_frozen_frequency_rms_hz="<<(history.chain.heldout_count?std::sqrt(history.chain.frozen_heldout_square/history.chain.heldout_count):0)
      <<" fresh_hop_phase_count="<<history.chain.current_phase.count<<" fresh_hop_phase_rms_deg="<<history.chain.current_phase.rms()*180/std::numbers::pi
      <<" fresh_hop_phase_worst_deg="<<history.chain.current_phase.maximum*180/std::numbers::pi
      <<" heldout_fresh_hop_count="<<history.chain.heldout_phase.count<<" heldout_fresh_hop_phase_rms_deg="<<history.chain.heldout_phase.rms()*180/std::numbers::pi
      <<" helper_clock_error_max_us="<<history.chain.clock_error_max*1e6
      <<" plan_origin_nominal="<<history.plan_origins[static_cast<std::size_t>(CompletePlan::Origin::nominal)]
      <<" plan_origin_qualified_joint="<<history.plan_origins[static_cast<std::size_t>(CompletePlan::Origin::qualified_joint)]
      <<" plan_origin_joint_point="<<history.plan_origins[static_cast<std::size_t>(CompletePlan::Origin::joint_point)]
      <<" plan_origin_joint_translated="<<history.plan_origins[static_cast<std::size_t>(CompletePlan::Origin::joint_translated)]
      <<" plan_origin_broad="<<history.plan_origins[static_cast<std::size_t>(CompletePlan::Origin::broad)]
      <<" plan_origin_local="<<history.plan_origins[static_cast<std::size_t>(CompletePlan::Origin::local)]
      <<" plan_origin_retained="<<history.plan_origins[static_cast<std::size_t>(CompletePlan::Origin::retained)]
      <<" smoother_failure_none="<<history.smoother_failures[0]<<" smoother_failure_not_ready="<<history.smoother_failures[1]
      <<" smoother_failure_invalid="<<history.smoother_failures[2]<<" smoother_failure_order="<<history.smoother_failures[3]
      <<" smoother_failure_future="<<history.smoother_failures[4]<<" smoother_failure_missing_phase="<<history.smoother_failures[5]
      <<" smoother_failure_unavailable_model="<<history.smoother_failures[6]<<" smoother_failure_reference="<<history.smoother_failures[7]
      <<" smoother_failure_conflicting_history="<<history.smoother_failures[8]
      <<" post_smoother_fixed_reference_mismatch="<<history.post_smoother_fixed_reference_mismatch
      <<" post_smoother_endpoint_invalid="<<history.post_smoother_endpoint_invalid
      <<" post_smoother_endpoint_reference_mismatch="<<history.post_smoother_endpoint_reference_mismatch
      <<" receiver_cpu36_including_output_instrumentation_seconds="<<receiver_cpu36<<" receiver_cpu_capture_drain_including_output_instrumentation_seconds="<<(history.chain.receiver.total_cpu()+history.chain.extra_cpu+component_cpu+boundary_history.cpu_seconds+inversion_history.cpu_seconds+profile_history.cpu_seconds)
      <<" receiver_measurement_and_history_cpu36_seconds="<<measurement_cpu36
      <<" profile_observe_cpu36_seconds="<<profile_cpu36<<" conditioner_cpu36_seconds="<<conditioning_cpu36
      <<" receiver_startup_cpu_seconds="<<history.chain.receiver.startup_cpu
      <<" receiver_tracking_cpu_seconds="<<history.chain.receiver.tracking_cpu
      <<" receiver_fallback_cpu_seconds="<<history.chain.receiver.fallback_cpu
      <<" receiver_fit_cpu_seconds="<<history.chain.receiver.fit_cpu
      <<" receiver_joint_history_cpu_seconds="<<history.chain.extra_cpu
      <<" receiver_capture_timeline_seconds="<<dwell_count*hp::dwell_seconds
      <<" pilot_aperture_seconds_per_dwell="<<static_cast<double>(aperture)/fs
      <<" broad_acquisition_calls="<<history.chain.receiver.broad_calls<<" aided_tracking_calls="<<history.chain.receiver.aided_calls
      <<" full_conditioned_input_seconds="<<static_cast<double>(receiver.captured_samples_processed())/fs
      <<" conditioned_input_seconds_at36="<<static_cast<double>(conditioned_inputs36)/fs
      <<" buffer_push_cpu_seconds="<<buffer_push_cpu
      <<" release_pop_condition_plan_consumer_cpu_seconds="<<component_cpu-buffer_push_cpu
      <<" conditioner_push_release_cpu_seconds="<<component_cpu<<" boundary_tracker_cpu_seconds="<<boundary_history.cpu_seconds
      <<" inversion_cpu_seconds="<<inversion_history.cpu_seconds<<" inversion_processed_seconds="<<inversion_history.attempts*hp::dwell_seconds
      <<" inversion_evaluator_cpu_seconds="<<inversion_history.evaluator_cpu_seconds
      <<" inversion_workspace_bytes="<<(inversion_history.detector?inversion_history.detector->workspace_bytes():0)
      <<" profile_observe_cpu_seconds="<<profile_history.cpu_seconds<<" profile_release_cpu_seconds_included_in_conditioner="<<profile_history.release_cpu_seconds
      <<" profile_evaluator_cpu_seconds="<<profile_history.evaluator_cpu_seconds<<" profile_window_input_seconds="<<static_cast<double>(profile_history.raw_samples_processed)/fs
      <<" profile_workspace_bytes="<<(profile_history.tracker?profile_history.tracker->workspace_bytes():0)
      <<" boundary_evaluator_cpu_seconds="<<boundary_history.evaluator_cpu_seconds<<" clean_twin_cpu_seconds="<<twin_cpu<<" generation_cpu_seconds="<<generation_cpu
      <<" generation_cpu36_seconds="<<generation_cpu36<<" evaluator_synthetic_generation_cpu_seconds="<<evaluator_generation_cpu
      <<" helper_history_evaluator_cpu_seconds="<<history_evaluator_cpu
      <<" metrology_cpu_seconds="<<metrology_cpu<<" evaluator_reference_input_sample="<<eval.evaluator_reference_seconds*fs<<" evaluator_constant_gauge_seconds="<<evaluator_gauge
      <<" setup_cpu_seconds="<<setup_cpu<<" component_allocated_bytes="<<receiver.allocated_bytes()
      <<" evaluator_pending_capacity_bytes="<<eval.noisy_pending.capacity()*sizeof(CompleteEvent)
      <<" evaluator_trace_capacity_bytes="<<(eval.output.noisy.capacity()+eval.output.clean.capacity())*sizeof(S)+eval.output.input_dates.capacity()*sizeof(long double)+eval.output_seconds.capacity()*sizeof(long double)+eval.output.supported.capacity()+eval.output.physically_disturbed.capacity()+eval.output_segments.capacity()*sizeof(std::uint32_t)+eval.output_segment_endpoint.capacity()+eval.output_schedule_slot.capacity()*sizeof(std::uint64_t)+eval.output_schedule_valid.capacity()
      <<" trace_discontinuities="<<eval.trace_discontinuities<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"complete study: "<<e.what()<<'\n';return 1;}}
