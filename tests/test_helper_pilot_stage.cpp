#include "../src/live_helper_pilot_stage.hpp"
#include "../src/live_helper_frontend.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/pattern_code.hpp"
#include <array>
#include <complex>
#include <ctime>
#include <iostream>
#include <numbers>
#include <random>
#include <vector>

using namespace datapump;
using namespace datapump::live::detail;
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}

void admitted_fifo_preserves_dates_through_loss_and_wrap() {
    constexpr std::uint32_t rate=1000;
    constexpr std::uint64_t origin=(1ULL<<60)+7;
    HelperPilotPcmStage stage(rate,origin);
    std::vector<float> received;std::uint64_t next=origin;
    const auto consume=[&](std::span<const float> input,std::uint64_t first) {
        check(first==next,"phase stage changed a committed sample date");
        received.insert(received.end(),input.begin(),input.end());next+=input.size();
    };
    std::array<float,20> input;
    for(std::size_t block=0;block<150;++block) {
        for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(block*input.size()+i);
        // Strong contexts, then a long fade/final loss. A later phase fit
        // flushes its observed support; loss drains only at the fixed cap.
        const auto fitted_end=block<50 && block%10==9?origin+(block+1)*input.size():0;
        stage.drain(fitted_end,consume);
        stage.make_room(input.size(),true,consume);
        stage.append(input,origin+block*input.size());
        stage.drain(fitted_end,consume);
        check(stage.size()<=HelperPilotPcmStage::sample_capacity(rate),"raw phase stage grew past .4 s");
        check(stage.captured_end_sample()==origin+(block+1)*input.size(),"phase stage advanced ingress incorrectly");
        check(next+stage.size()==stage.captured_end_sample(),"phase stage lost committed PCM during reference loss");
    }
    check(stage.size()==400,"final reference loss did not retain the fixed bounded tail");
    stage.drain(stage.captured_end_sample(),consume);
    check(received.size()==3000,"phase stage lost or duplicated input");
    for(std::size_t i=0;i<received.size();++i)check(received[i]==i,"phase stage reordered PCM across wrap");
    check(stage.workspace_bytes()==HelperPilotPcmStage::reservation_bytes(rate),"raw stage allocation exceeded its exact reserve");
}

void only_waiting_input_may_be_forgotten() {
    HelperPilotPcmStage stage(1000,0);std::array<float,20> input{};
    const auto unexpected=[](std::span<const float>,std::uint64_t){throw Error("unadmitted input was delivered");};
    for(std::uint64_t first=0;first<1000;first+=input.size()) {
        stage.make_room(input.size(),false,unexpected);stage.append(input,first);
    }
    check(stage.first_sample()==600 && stage.size()==400,"waiting FIFO did not retain its bounded recent suffix");
    auto first=stage.first_sample();
    stage.drain(stage.captured_end_sample(),[&](std::span<const float> values,std::uint64_t date) {
        check(date==first,"admission changed retained raw coordinates");first+=values.size();
    });
    bool rejected=false;
    try{stage.append(input,1001);}catch(const Error&){rejected=true;}
    check(rejected && stage.captured_end_sample()==1000,"raw-stage gap changed its frontier");
}

void projection_and_pooled_frequency_never_grant_phase_authority() {
    helper::ToneTrackerSnapshot fit;fit.sample_rate=8000;fit.captured_end_sample=3200;
    fit.phase_qualified=fit.projection_phase_valid=fit.qualified=true;
    fit.phase_fit_first_sample=1000;fit.phase_fit_end_sample=2600;fit.phase_fit_sample=2520;
    fit.phase_fit_sample_fraction=.5;fit.phase_fit_cycles=.2;fit.phase_fit_uncertainty_cycles=.01;
    fit.phase_fit_frequency_hz=2400.25;fit.phase_fit_frequency_uncertainty_hz=.1;
    check(!helper_pilot_phase_observation(fit,2400,8000),"projection or pooled frequency granted phase authority");
    fit.phase_fit_qualified=true;
    const auto accepted=helper_pilot_phase_observation(fit,2400,8000);
    check(accepted && accepted->first_sample==1000 && accepted->end_sample==2600 &&
        accepted->phase_sample_fraction==.5 && accepted->observed_hz==2400.25,"local fit provenance was not preserved");
    fit.phase_fit_uncertainty_cycles=.124;
    check(!helper_pilot_phase_observation(fit,2400,8000),"uncertain local slope passed the endpoint phase gate");
    // A joint prediction can be tighter than independently summing the
    // correlated anchor and slope intervals, while retaining the same gate.
    fit.phase_fit_support_uncertainty_cycles=.06;
    check(helper_pilot_phase_observation(fit,2400,8000).has_value(),"joint supported phase bound was ignored");
    fit.phase_fit_support_uncertainty_cycles=.125;
    check(!helper_pilot_phase_observation(fit,2400,8000),"whole-support phase bound bypassed the original gate");
    fit.phase_fit_support_uncertainty_cycles=std::numeric_limits<double>::quiet_NaN();
    check(!helper_pilot_phase_observation(fit,2400,8000),"invalid present whole-support phase bound used the legacy fallback");
    fit.phase_fit_support_uncertainty_cycles=-.01;
    check(!helper_pilot_phase_observation(fit,2400,8000),"negative whole-support phase bound granted authority");
    fit.phase_fit_support_uncertainty_cycles.reset();
    fit.phase_fit_uncertainty_cycles=.01;fit.phase_fit_end_sample=3201;
    check(!helper_pilot_phase_observation(fit,2400,8000),"future capture granted phase authority");
    fit.phase_fit_end_sample=2600;fit.phase_fit_sample=999;
    check(!helper_pilot_phase_observation(fit,2400,8000),"phase date outside actual support was accepted");
}

void produced_certificates_bound_actual_correction_and_expire_phase() {
    using Frontend=HelperInputFrontend;
    constexpr unsigned rate=8000,step=160;constexpr double pilot=2400;
    modem::Config config;config.sample_rate=rate;config.carrier_hz=1200;config.bandwidth_hz=36;
    config.spreading_factor=16;config.pulse_shaping=false;
    helper::Reference raw;raw.sample_rate=rate;raw.frequency_hz=pilot+.25;
    raw.frequency_uncertainty_hz=.01;raw.frequency_valid=true;
    const auto grant=Frontend::reservation_bound(rate,rate,pilot);
    Frontend frontend(grant,79);frontend.admit_realtime(config,pilot,raw,rate,rate/2.-10,0);
    check(frontend.active() && !frontend.reference_passthrough(),"certificate fixture did not admit a corrected branch");
    bool phase_seen=false,expired_seen=false,unqualified_seen=false;std::uint64_t next=0;
    std::array<float,step> input{};
    for(std::uint64_t first=0;first<rate;first+=step) {
        auto update=raw;update.frequency_hz=first<3200?pilot+.25:pilot+.5;
        check(frontend.track_frequency(update,pilot),"certificate fixture frequency update failed");
        if(first==2720) {
            helper::AnalyticFrequencyTranslator::PilotObservation observation{pilot,pilot+.25,.2,3000,.5,
                1600,4800,4800,pilot+.25};
            check(frontend.track_pilot(observation),"fully settled laboratory phase support was declined");
        }
        for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(.02*std::cos(
            2*std::numbers::pi*(config.carrier_hz+.25)*(first+i)/rate));
        frontend.push(input,first,first<6000?std::optional<double>{.02}:std::nullopt);
        for(const auto& block:frontend.take(UINT64_MAX,raw,first+step)) {
            if(!next)next=block.first_sample;
            check(block.first_sample==next && block.reference && block.reference->frequency_hz==raw.frequency_hz,
                "correction certificate redated PCM or rewrote the raw measured reference");
            next+=block.samples.size();phase_seen=phase_seen || block.common_phase_corrected;
            if(block.corrected_rf_uncertainty_hz) {
                check(*block.corrected_rf_uncertainty_hz==.02 && block.applied_correction_hz.lower_hz<=-.25 &&
                    block.applied_correction_hz.upper_hz>=-.5,"produced certificate lost an actual applied q or residual bound");
            }
            if(block.first_sample>5200){check(!block.common_phase_corrected,"phase assistance survived its actual support");expired_seen=true;}
            if(block.first_sample>6800){check(!block.corrected_rf_uncertainty_hz,
                "unsupported drained PCM acquired a qualified RF mask");unqualified_seen=true;}
        }
        check(frontend.bytes()<=grant,"numeric correction certificates exceeded the prepaid frontend bound");
    }
    check(phase_seen && expired_seen && unqualified_seen,"production certificate fixture omitted phase, expiry or loss coverage");
}

void independent_frequency_survives_phase_failure_without_joining_records() {
    constexpr std::uint32_t rate=48000;constexpr std::size_t chunk=960;
    constexpr std::uint64_t retune=rate*6/5,record_end=retune+rate/5;
    constexpr double tau=2*std::numbers::pi,truth=2413.37;
    helper::ToneTrackerConfig config{rate,2400,9000,0,.5,true};
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
    std::mt19937 random{89471};std::normal_distribution<double> normal;
    std::array<float,chunk> pcm{};
    for(std::uint64_t first=0;first<record_end;first+=chunk) {
        if(first==retune)tracker.begin_pilot_segment(first,2400,9000,true,1);
        for(std::size_t i=0;i<chunk;++i) {
            const auto sample=first+i;const auto t=static_cast<double>(sample)/rate;
            // The new aperture has a second unknown phase halfway through it.
            // Frequency remains measurable while its straight phase fit fails.
            const auto jump=sample<retune?0.:.83+(sample>=retune+rate/10?2.:0.);
            pcm[i]=static_cast<float>(.6*std::cos(.37+tau*truth*t+jump)+.01*normal(random));
        }
        tracker.push(pcm,first);
    }
    const auto before=tracker.snapshot();
    const auto query_first=retune+1024,query_end=record_end-512;
    constexpr auto delay=helper::AnalyticFrequencyTranslator::group_delay;
    const auto old=helper_pilot_frequency_plan(before,query_first+delay,query_end+delay);
    const auto independent=tracker.frequency_interval(query_first,query_end);
    const auto crossing=tracker.frequency_interval(retune-1,query_first);
    const auto after=tracker.snapshot();
    const bool publication_unchanged=before.sample==after.sample && before.first_sample==after.first_sample &&
        before.frequency_hz==after.frequency_hz && before.uncertainty_hz==after.uncertainty_hz &&
        before.frequency_rate_hz_per_second==after.frequency_rate_hz_per_second && before.qualified==after.qualified &&
        before.frequency_covariance_valid==after.frequency_covariance_valid &&
        before.frequency_knowledge_valid==after.frequency_knowledge_valid &&
        before.frequency_variance_hz2==after.frequency_variance_hz2 &&
        before.frequency_rate_covariance_hz2_per_second==after.frequency_rate_covariance_hz2_per_second &&
        before.frequency_rate_variance_hz2_per_second2==after.frequency_rate_variance_hz2_per_second2;
    std::cout<<"INDEPENDENT_PHASE_UNQUALIFIED_CONTROL global_qualified="<<before.qualified
        <<" local_phase_qualified="<<before.phase_fit_qualified<<" snapshot_plan_valid="<<old.valid
        <<" independent_valid="<<independent.valid<<" independent_aperture="<<independent.independent_aperture
        <<" smoothed="<<independent.smoothed<<" frequency_error_hz="<<std::abs(independent.frequency_hz-truth)
        <<" U_hz="<<independent.uncertainty_hz<<" crossing_valid="<<crossing.valid
        <<" publication_unchanged="<<publication_unchanged<<'\n';
    check(before.qualified && !before.phase_fit_qualified && !helper_pilot_phase_observation(before,2400,rate),
        "phase-unqualified frequency control unexpectedly had phase authority");
    check(!old.valid && independent.valid && independent.independent_aperture && !independent.smoothed &&
        independent.first_sample==query_first && independent.end_sample==query_end &&
        independent.evidence_first_sample==retune && independent.evidence_end_sample==record_end &&
        independent.anchor_sample==retune && independent.captured_end_sample==record_end &&
        std::abs(independent.frequency_hz-truth)<=independent.uncertainty_hz,
        "independent accepted frequency required phase or changed its dated support");
    check(!crossing.valid,"independent frequency query joined records across a boundary");
    check(publication_unchanged,"independent frequency query changed the fine publication");
}

void causal_mixed_pilot_corrects_ordinary_robust(double cn0_db_hz,unsigned seed,bool actual_rf_mapping=false) {
    using Translator=helper::AnalyticFrequencyTranslator;
    constexpr std::uint32_t rate=48000;
    constexpr std::size_t dwell=rate*2/5,chunk=rate/50,count=rate*24/5;
    constexpr double tau=2*std::numbers::pi,pilot=2400,data_gain=.04,helper_gain=.6;
    constexpr std::uint64_t epoch=1800000000;
    constexpr std::array actual_rf_hz{7e6,7.7e6,8.4e6,9.1e6};
    const auto rf_scale=[&](std::uint64_t sample) {
        return actual_rf_mapping?actual_rf_hz[(sample/dwell)%actual_rf_hz.size()]/actual_rf_hz.front():1.;
    };
    modem::Config modem_config;modem_config.sample_rate=rate;modem_config.carrier_hz=1200;modem_config.bandwidth_hz=36;
    modem_config.dsss_factor=10;modem_config.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    modem_config.spreading_factor=64;modem_config.integration_seconds=14.2;modem_config.training_seconds=2;
    modem_config.memory_limit=64ULL*1024*1024;modem_config.scramble=true;
    modem_config.data_key=Crypto{std::array<std::uint8_t,32>{}};
    modem::PatternTransmitter transmitter(Bytes{1,0,1,1,0,0,1,0},modem_config,epoch,0,false);
    std::vector<std::complex<double>> data(count);
    check(transmitter.read_analytic(data)==data.size(),"causal experiment exhausted its actual Robust generator");
    helper::Options options;options.power_w=40;options.data_power_w=4;options.timecode=true;
    const helper::Plan plan{pilot,helper_gain,data_gain,40,pilot-helper::auxiliary_pilot_spacing_hz};
    helper::Generator generator(options,plan,epoch,true,modem_config.data_key);
    std::mt19937 random(seed);std::normal_distribution<double> normal;
    std::uniform_real_distribution<double> phase(-std::numbers::pi,std::numbers::pi);
    std::array<double,count/dwell> hop_phase{};for(auto& value:hop_phase)value=phase(random);
    const auto pilot_amplitude=helper_gain*helper::pilot_amplitude_fraction;
    const auto noise_sigma=pilot_amplitude*std::sqrt(rate/(4*std::pow(10.,cn0_db_hz/10)));
    std::vector<float> mixed(count),impaired_payload(count),ideal_payload(count);
    for(std::size_t n=0;n<count;++n) {
        const auto t=static_cast<double>(n)/rate;
        // Laboratory channel truth is used only to generate impairment and
        // post-hoc error checks. Neither acquisition nor correction sees it.
        const auto dt=std::max(0.,t-1.6);
        const auto common=std::polar(1.,hop_phase[n/dwell]+tau*rf_scale(n)*(3000*t+.015*dt*dt));
        const bool helper_present=t<3.6 && !(t>=2.08 && t<2.16);
        const auto payload=data_gain*data[n];
        const auto composite=helper_present?generator.mix_analytic(data[n],
            static_cast<long double>(n)/rate,rate):payload;
        mixed[n]=static_cast<float>((composite*common).real()+noise_sigma*normal(random));
        impaired_payload[n]=static_cast<float>((payload*common).real());
        ideal_payload[n]=static_cast<float>(payload.real());
    }

    // The only acquisition prior is a broad oscillator-derived search band.
    // +3000 Hz is never supplied as a seed. Known retune dates invalidate
    // phase. The optional LAB mapping uses known actual RF and upper sideband,
    // with zero audio-clock error explicitly controlled. It supplies no hidden
    // tone frequency/phase seed and does not hook a production RF adapter.
    helper::ToneTrackerConfig tracker_config{rate,pilot,9000,0,.5,true};
    helper::ToneTracker tracker(tracker_config,helper::ToneTracker::workspace_required(tracker_config));
    HelperPilotPcmStage stage(rate,0);
    std::unique_ptr<Translator> mixed_corrected,payload_corrected,frequency_only;
    std::vector<float> corrected(count),uncorrected(count),mixed_scratch(stage.sample_capacity(rate));
    std::vector<float> corrected_nco_phase_cycles(count);
    struct Fit {
        std::uint64_t first=0,end=0,applied_first=0;
        Translator::PilotObservation observation;
        double phase_uncertainty_cycles=0,frequency_uncertainty_hz=0;
    };
    std::vector<Fit> fits;
    std::uint64_t last_phase_end=0,last_applied_phase_end=0,admitted_first=0,next_processed=0,first_qualified=0;
    helper::ToneTrackerSnapshot frequency_model;std::optional<Translator::PilotObservation> phase_model;
    double phase_model_uncertainty=0,phase_model_frequency_uncertainty=0;
    std::size_t phase_updates=0,qualified=0,escapes=0,wrong_tones=0,max_staged=0;
    std::size_t phase_fit_polls=0,phase_gate_polls=0,frequency_query_valid=0,frequency_query_declined=0,phase_declined=0;
    std::size_t dated_interval_escapes=0,dated_truth_checks=0;unsigned qualified_rf_mask=0;
    double max_frequency_error=0,max_pooled_local_difference=0,max_dated_frequency_error=0;
    const auto started=std::clock();
    const auto consume=[&](std::span<const float> input,std::uint64_t first) {
        check(mixed_corrected && first==next_processed,"causal phase stage dropped or redated admitted PCM");
        for(std::size_t offset=0;offset<input.size();) {
            const auto at=first+offset;auto used=std::min(chunk,input.size()-offset);
            if(actual_rf_mapping && at>=Translator::group_delay) {
                // One constant NCO segment may not span different dated RF
                // mappings of its delayed input centers.
                const auto center=at-Translator::group_delay;
                const auto boundary=(center/dwell+1)*dwell+Translator::group_delay;
                used=static_cast<std::size_t>(std::min<std::uint64_t>(used,boundary-at));
            }
            if(phase_model && phase_model->end_sample>last_applied_phase_end) {
                const auto activation=phase_model->first_sample+2*Translator::group_delay;
                if(at<activation && activation-at<used)used=static_cast<std::size_t>(activation-at);
            }
            auto dated_model=frequency_model;
            if(actual_rf_mapping && at>=Translator::group_delay) {
                // A snapshot query is in its CURRENT RF geometry. The explicit
                // laboratory RF map converts the normalized common oscillator
                // quantity and its covariance to the actual older input hop.
                const auto target_scale=rf_scale(at-Translator::group_delay);
                const auto ratio=target_scale/frequency_model.frequency_model_residual_scale;
                dated_model.frequency_hz=pilot+(frequency_model.frequency_hz-
                    frequency_model.frequency_model_expected_hz)*ratio;
                dated_model.frequency_rate_hz_per_second*=ratio;
                dated_model.frequency_variance_hz2*=ratio*ratio;
                dated_model.frequency_rate_covariance_hz2_per_second*=ratio*ratio;
                dated_model.frequency_rate_variance_hz2_per_second2*=ratio*ratio;
                dated_model.frequency_model_expected_hz=pilot;
                dated_model.frequency_model_residual_scale=target_scale;
            }
            auto plan=helper_pilot_frequency_plan(dated_model,at,at+used);
            // Match Live's fixed-RF selection. The LAB control additionally
            // assumes zero independent audio-clock error, so its known RF
            // ratio maps the interval's CURRENT geometry back to these actual
            // input dates. Production actual RF uses the separate joint model.
            constexpr auto delay=Translator::group_delay;
            if(at>=delay) {
                auto buffered=tracker.frequency_interval(at-delay,at+used-delay);
                if(actual_rf_mapping && buffered.valid) {
                    if(buffered.independent_aperture) {
                        // This fixture settles at each supplied retune date.
                        // A new local record must belong wholly to the current
                        // settled segment; its original dates never change.
                        const auto settled_first=(frequency_model.captured_end_sample-1)/dwell*dwell;
                        if(buffered.evidence_first_sample<settled_first ||
                           buffered.evidence_end_sample>settled_first+dwell)buffered.valid=false;
                    }
                    const auto ratio=rf_scale(at-delay)/frequency_model.frequency_model_residual_scale;
                    buffered.frequency_hz=pilot+(buffered.frequency_hz-
                        frequency_model.frequency_model_expected_hz)*ratio;
                    buffered.uncertainty_hz*=std::abs(ratio);
                }
                if(buffered.valid && buffered.evidence_end_sample<=frequency_model.captured_end_sample &&
                   (!plan.valid || buffered.uncertainty_hz<plan.residual_uncertainty_hz))
                    plan={true,buffered.frequency_hz,buffered.uncertainty_hz};
            }
            if(plan.valid)++frequency_query_valid;else ++frequency_query_declined;
            if(plan.valid) {
                // Post-hoc only: verify the actual RF geometry at each consumed
                // endpoint. These truth values never choose or qualify q.
                for(const auto center:std::array{at-Translator::group_delay,at+used-1-Translator::group_delay}) {
                    const auto t=static_cast<double>(center)/rate;
                    const auto truth=pilot+rf_scale(center)*(3000+.03*std::max(0.,t-1.6));
                    const auto error=std::abs(plan.tracking_frequency_hz-truth);
                    max_dated_frequency_error=std::max(max_dated_frequency_error,error);
                    dated_interval_escapes+=error>plan.residual_uncertainty_hz+1e-7;++dated_truth_checks;
                }
                const auto correction=pilot-plan.tracking_frequency_hz;
                check(mixed_corrected->track_frequency(correction) && payload_corrected->track_frequency(correction) &&
                    frequency_only->track_frequency(correction),"dated causal frequency update left the acquired physical band");
                if(phase_model && phase_model->end_sample>last_applied_phase_end &&
                   at>=phase_model->first_sample+2*Translator::group_delay && at<phase_model->end_sample) {
                    auto phase=*phase_model;phase.tracking_frequency_hz=plan.tracking_frequency_hz;
                    const auto applied=mixed_corrected->track_pilot(phase);
                    if(!applied)++phase_declined;
                    check(payload_corrected->track_pilot(phase)==applied,"diagnostic payload phase path diverged from composite correction");
                    if(applied) {
                        last_applied_phase_end=phase.end_sample;++phase_updates;
                        fits.push_back({phase.first_sample,phase.end_sample,at,phase,
                            phase_model_uncertainty,phase_model_frequency_uncertainty});
                    }
                }
            }
            auto nco_cycles=payload_corrected->phase_at(at)/tau;
            const auto nco_step=std::remainder(payload_corrected->phase_at(at+1)/tau-nco_cycles,1.);
            for(std::size_t i=0;i<used;++i) {
                corrected_nco_phase_cycles[at+i]=static_cast<float>(nco_cycles);
                nco_cycles=std::remainder(nco_cycles+nco_step,1.);
            }
            mixed_corrected->push(input.subspan(offset,used),std::span<float>(mixed_scratch).first(used),at);
            // Parallel linear diagnostic paths isolate actual ordinary Robust.
            // They receive the same acquired correction and never feed tracking.
            const auto payload=std::span<const float>(impaired_payload).subspan(at,used);
            payload_corrected->push(payload,std::span<float>(corrected).subspan(at,used),at);
            frequency_only->push(payload,std::span<float>(uncorrected).subspan(at,used),at);
            next_processed+=used;offset+=used;
            check(mixed_corrected->next_sample()==next_processed && payload_corrected->next_sample()==next_processed &&
                frequency_only->next_sample()==next_processed,"causal correction changed an actual processed frontier");
        }
    };
    for(std::uint64_t first=0;first<count;first+=chunk) {
        if(first && first%dwell==0) {
            tracker.begin_pilot_segment(first,pilot,9000,true,rf_scale(first));
            check(!tracker.snapshot().phase_fit_qualified,"retune carried phase authority into a new hop");
        }
        const auto input=std::span<const float>(mixed).subspan(first,chunk);
        tracker.push(input,first);const auto tracked=tracker.snapshot();frequency_model=tracked;
        check(tracked.captured_end_sample==first+chunk,"causal tracker changed actual ingress dates");
        if(tracked.qualified) {
            ++qualified;if(!first_qualified)first_qualified=first+chunk;
            qualified_rf_mask|=1U<<((first/dwell)%actual_rf_hz.size());
            const auto date=static_cast<double>(tracked.sample)/rate;
            const auto truth=pilot+(3000+.03*std::max(0.,date-1.6))*tracked.frequency_model_residual_scale;
            const auto error=std::abs(tracked.frequency_hz-truth);
            max_frequency_error=std::max(max_frequency_error,error);
            escapes+=error>tracked.uncertainty_hz+1e-7;wrong_tones+=error>100;
            if(!mixed_corrected) {
                admitted_first=next_processed=stage.first_sample();
                const auto correction=pilot-tracked.frequency_hz;
                auto low=std::min(modem_config.carrier_hz-correction-100,tracked.frequency_hz-625)-32;
                auto high=std::max(modem_config.carrier_hz-correction+100,tracked.frequency_hz+225)+32;
                if(actual_rf_mapping) {
                    // Admit the full known RF calendar's positive capture hull
                    // using the acquired oscillator value, never channel truth.
                    const auto common=(tracked.frequency_hz-pilot)/tracked.frequency_model_residual_scale;
                    const auto a=common*actual_rf_hz.front()/actual_rf_hz.front();
                    const auto b=common*actual_rf_hz.back()/actual_rf_hz.front();
                    low=std::min(modem_config.carrier_hz+std::min(a,b)-100,pilot+std::min(a,b)-625)-32;
                    high=std::max(modem_config.carrier_hz+std::max(a,b)+100,pilot+std::max(a,b)+225)+32;
                }
                const Translator::Config config{rate,correction,{low,high},admitted_first,false,true};
                mixed_corrected=std::make_unique<Translator>(config,Translator::workspace_required());
                payload_corrected=std::make_unique<Translator>(config,Translator::workspace_required());
                frequency_only=std::make_unique<Translator>(config,Translator::workspace_required());
            }
        }
        const auto observation=helper_pilot_phase_observation(tracked,pilot,rate);
        phase_fit_polls+=tracked.phase_fit_qualified;phase_gate_polls+=bool(observation);
        if(mixed_corrected && tracked.qualified && observation && observation->end_sample>last_phase_end) {
            max_pooled_local_difference=std::max(max_pooled_local_difference,
                std::abs(tracked.frequency_hz-observation->observed_hz));
            check(observation->end_sample<=first+chunk && observation->first_sample/dwell==
                (observation->end_sample-1)/dwell,"causal phase fit crossed capture or an independent retune");
            phase_model=observation;last_phase_end=observation->end_sample;
            phase_model_uncertainty=tracked.phase_fit_uncertainty_cycles;
            phase_model_frequency_uncertainty=tracked.phase_fit_frequency_uncertainty_hz;
        }
        if(mixed_corrected)stage.drain(last_phase_end,consume);
        stage.make_room(chunk,bool(mixed_corrected),consume);stage.append(input,first);
        if(mixed_corrected)stage.drain(last_phase_end,consume);
        max_staged=std::max(max_staged,stage.size());
        check(stage.size()<=stage.sample_capacity(rate),"causal raw stage exceeded .4 seconds");
        if(mixed_corrected)check(next_processed+stage.size()==first+chunk,
            "causal helper loss discarded or manufactured admitted payload samples");
    }
    const auto cpu_seconds=static_cast<double>(std::clock()-started)/CLOCKS_PER_SEC;
    std::cout<<"CAUSAL_PILOT_STAGE_QUALIFICATION pilot_cn0_db_hz="<<cn0_db_hz<<" seed="<<seed
        <<" actual_rf_mapping_lab="<<actual_rf_mapping
        <<" first_qualified_s="<<static_cast<double>(first_qualified)/rate<<" qualified_polls="<<qualified
        <<" phase_fit_polls="<<phase_fit_polls<<" phase_gate_polls="<<phase_gate_polls
        <<" dated_query_valid="<<frequency_query_valid<<" dated_query_declined="<<frequency_query_declined
        <<" phase_updates="<<phase_updates<<" phase_declined="<<phase_declined
        <<" admitted="<<bool(mixed_corrected)<<" last_phase_end="<<last_phase_end
        <<" processed_end="<<next_processed<<" staged="<<stage.size()<<" cpu_s="<<cpu_seconds<<std::endl;
    check(mixed_corrected && phase_updates>=8 && first_qualified<=rate,
        "strong causal mixture did not acquire and transfer useful independent phase fits");
    check(!tracker.snapshot().qualified && !tracker.snapshot().phase_fit_qualified,
        "final Helper loss retained frequency or phase authority");
    check(next_processed+stage.size()==count && stage.size()==stage.sample_capacity(rate),
        "final Helper loss did not retain exactly its bounded unprocessed tail");
    double assisted_error=0,frequency_error=0,energy=0;std::size_t measured=0;
    for(std::uint64_t output=std::max<std::uint64_t>(rate,admitted_first+Translator::tap_count);output<next_processed;++output) {
        const auto center=output-Translator::group_delay;
        const auto local=center%dwell;
        if(local<Translator::tap_count || local+Translator::tap_count>=dwell)continue;
        // Score phase only where an actual accepted same-hop model supports
        // the input center. No multi-hop coherent assumption is made.
        const bool supported=std::any_of(fits.begin(),fits.end(),[&](const auto& fit){return
            fit.first<=center && center<fit.end && output>=fit.first+2*Translator::group_delay;});
        if(!supported || center>=static_cast<std::uint64_t>(3.6*rate))continue;
        const auto truth=static_cast<double>(ideal_payload[center]);
        assisted_error+=std::pow(corrected[output]-truth,2);
        frequency_error+=std::pow(uncorrected[output]-truth,2);energy+=truth*truth;++measured;
    }
    const auto assisted_rms=std::sqrt(assisted_error/energy),frequency_rms=std::sqrt(frequency_error/energy);
    std::cout<<"CAUSAL_PILOT_STAGE pilot_cn0_db_hz="<<cn0_db_hz<<" seed="<<seed
        <<" actual_rf_mapping_lab="<<actual_rf_mapping
        <<" first_qualified_s="<<static_cast<double>(first_qualified)/rate<<" phase_updates="<<phase_updates
        <<" qualified_polls="<<qualified<<" interval_escapes="<<escapes<<" wrong_tones="<<wrong_tones
        <<" max_frequency_error_hz="<<max_frequency_error<<" max_pooled_local_difference_hz="<<max_pooled_local_difference
        <<" dated_truth_checks="<<dated_truth_checks<<" dated_interval_escapes="<<dated_interval_escapes
        <<" max_dated_frequency_error_hz="<<max_dated_frequency_error<<" qualified_rf_mask="<<qualified_rf_mask
        <<" measured_samples="<<measured<<" payload_relative_rms="<<assisted_rms
        <<" frequency_only_relative_rms="<<frequency_rms<<" admitted_first="<<admitted_first
        <<" processed_end="<<next_processed<<" captured_end="<<count<<" max_staged="<<max_staged
        <<" stage_bytes="<<stage.workspace_bytes()<<" tracker_bytes="<<tracker.workspace_bytes()
        <<" translators_bytes="<<3*Translator::workspace_required()<<" component_cpu_s="<<cpu_seconds
        <<" cpu_per_audio_s="<<cpu_seconds/(static_cast<double>(count)/rate)
        <<" acquisition_seed=NONE retune_dates=LABORATORY_SUPPLIED rf_mapping="
        <<(actual_rf_mapping?"LAB_ACTUAL_7_7.7_8.4_9.1_MHz_USB_audio_clock_ZERO":"UNTESTED_constant_scale_lab_control")
        <<" production_rf_adapter=UNIMPLEMENTED\n";
    check(wrong_tones==0 && max_frequency_error<2,"causal acquisition selected a wrong tone or failed to remove broad RF uncertainty");
    if(actual_rf_mapping)check(qualified_rf_mask==15 && dated_truth_checks>100 && max_dated_frequency_error<2,
        "laboratory actual-RF mapping omitted a radio frequency or miscorrected dated buffered input");
    check(measured>rate && energy>0 && assisted_rms<.35 && frequency_rms>.5 && assisted_rms<frequency_rms*.4,
        "causally acquired pilot phase did not materially improve ordinary Robust PCM over frequency-only correction");

    // Whole-aperture discriminator. Unlike the existing supported-subset RMS,
    // these scores use EVERY produced sample, including startup, actual
    // Hilbert/retune transients, unsupported phase gaps and the brief fade.
    // This is an analytic truth template used only AFTER causal correction.
    // The ordinary payload notch is not part of this Translator fixture.
    constexpr std::array rf_grid{-2.,-1.,-.5,-.25,-.125,-.0625,0.,.0625,.125,.25,.5,1.,2.};
    constexpr std::size_t zero=6;
    const auto score_interval=[&](const char* name,std::uint64_t first,std::uint64_t end) {
        check(first>=admitted_first && first<end && end<=next_processed,
            "whole-aperture score escaped actual admitted produced input");
        struct Score {std::array<std::complex<double>,rf_grid.size()> sum{};double energy=0;};
        Score assisted,frequency,ideal;
        std::array<std::complex<double>,rf_grid.size()> oscillator{},step{};
        oscillator.fill({1,0});
        for(std::size_t f=0;f<rf_grid.size();++f)step[f]=std::polar(1.,-tau*rf_grid[f]/rate);
        double template_energy=0,covered_energy=0,max_phase_error=0,max_phase_bound=0,max_uncovered_phase_error=0;
        std::size_t phase_checks=0,phase_escapes=0;
        for(auto output=first;output<end;++output) {
            const auto center=output-Translator::group_delay;
            const auto reference=data_gain*data[center];
            const auto weight=std::norm(reference);template_energy+=weight;
            const auto a=static_cast<double>(corrected[output]);
            const auto f=static_cast<double>(uncorrected[output]);
            const auto i=reference.real();
            assisted.energy+=2*a*a;frequency.energy+=2*f*f;ideal.energy+=2*i*i;
            for(std::size_t bin=0;bin<rf_grid.size();++bin) {
                const auto matched=2.*std::conj(reference)*oscillator[bin];
                assisted.sum[bin]+=a*matched;frequency.sum[bin]+=f*matched;ideal.sum[bin]+=i*matched;
                oscillator[bin]*=step[bin];
            }
            const auto fit=std::find_if(fits.begin(),fits.end(),[&](const auto& fit) {
                return fit.applied_first<=output && output<fit.end;
            });
            const auto t=static_cast<double>(center)/rate,dt=std::max(0.,t-1.6);
            const auto true_common_cycles=hop_phase[center/dwell]/tau+rf_scale(center)*(3000*t+.015*dt*dt);
            const auto nco_cycles=static_cast<double>(corrected_nco_phase_cycles[output]);
            const auto error=std::abs(std::remainder(true_common_cycles+nco_cycles,1.));
            if(fit!=fits.end()) {
                covered_energy+=weight;++phase_checks;max_phase_error=std::max(max_phase_error,error);
                const auto& observation=fit->observation;
                const auto date_delta=static_cast<double>(static_cast<long double>(center)-
                    observation.phase_sample-observation.phase_sample_fraction)/rate;
                // Include the actual accumulated difference between the local
                // phase-fit slope and every subsequently applied pooled NCO
                // slope. This is not the old instantaneous-frequency interval.
                const auto modeled_cycles=observation.phase_cycles+observation.observed_hz*date_delta;
                const auto deterministic=std::abs(std::remainder(modeled_cycles+nco_cycles-pilot*t,1.));
                const auto bound=fit->phase_uncertainty_cycles+
                    fit->frequency_uncertainty_hz*std::abs(date_delta)+deterministic;
                max_phase_bound=std::max(max_phase_bound,bound);phase_escapes+=error>bound+1e-7;
            } else max_uncovered_phase_error=std::max(max_uncovered_phase_error,error);
        }
        check(template_energy>0 && ideal.energy>0,"whole-aperture truth template has no energy");
        const auto ideal_zero=std::norm(ideal.sum[zero]);
        const auto ideal_coherence=ideal_zero/(ideal.energy*template_energy);
        const auto print=[&](const char* path,const Score& score) {
            std::size_t peak=0;
            for(std::size_t bin=1;bin<rf_grid.size();++bin)
                if(std::norm(score.sum[bin])>std::norm(score.sum[peak]))peak=bin;
            const auto zero_power=std::norm(score.sum[zero])/ideal_zero;
            const auto peak_power=std::norm(score.sum[peak])/ideal_zero;
            const auto energy_ratio=score.energy/ideal.energy;
            const auto coherence=std::norm(score.sum[zero])/(score.energy*template_energy);
            std::cout<<"WHOLE_APERTURE_SCORE pilot_cn0_db_hz="<<cn0_db_hz<<" seed="<<seed
                <<" actual_rf_mapping_lab="<<actual_rf_mapping<<" interval="<<name<<" path="<<path
                <<" first_output="<<first<<" end_output="<<end<<" samples="<<end-first
                <<" zero_score_amplitude="<<std::sqrt(zero_power)
                <<" zero_score_loss_db="<<-10*std::log10(std::max(zero_power,1e-30))
                <<" signal_energy_ratio="<<energy_ratio
                <<" zero_coherence_loss_db="<<-10*std::log10(std::max(coherence/ideal_coherence,1e-30))
                <<" peak_rf_hz="<<rf_grid[peak]<<" peak_score_amplitude="<<std::sqrt(peak_power)
                <<" peak_at_grid_edge="<<(peak==0 || peak+1==rf_grid.size())
                <<" covered_energy_fraction="<<covered_energy/template_energy
                <<" covered_phase_checks="<<phase_checks<<" phase_error_escapes="<<phase_escapes
                <<" max_covered_common_phase_error_cycles="<<max_phase_error
                <<" max_reported_common_phase_bound_cycles="<<max_phase_bound
                <<" max_uncovered_common_phase_error_cycles="<<max_uncovered_phase_error
                <<" frontend=HILBERT_TRANSLATOR_ONLY payload_notch=UNTESTED startup_included=1"
                   " phase_diagnostics=PHASE_CORRECTED_NCO_COMMON_RESIDUAL"
                   " signal_path=ISOLATED_LINEAR_ROBUST_CONTRIBUTION"
                   " truth_template=EVALUATION_ONLY symbol_complete=0 symbol128s=UNTESTED\n";
        };
        print("phase_corrected",assisted);print("frequency_only",frequency);print("ideal",ideal);
        return end-first;
    };
    const auto loss_output=static_cast<std::uint64_t>(3.6*rate)+Translator::group_delay;
    const auto present_samples=score_interval("helper_present_all_samples",admitted_first,loss_output);
    const auto loss_samples=score_interval("later_helper_loss_all_samples",loss_output,next_processed);
    check(present_samples+loss_samples==next_processed-admitted_first,
        "whole-aperture discriminator excluded admitted samples");
}
}
int main() {
    try {
        admitted_fifo_preserves_dates_through_loss_and_wrap();only_waiting_input_may_be_forgotten();
        projection_and_pooled_frequency_never_grant_phase_authority();
        produced_certificates_bound_actual_correction_and_expire_phase();
        independent_frequency_survives_phase_failure_without_joining_records();
        causal_mixed_pilot_corrects_ordinary_robust(40,49271);
        causal_mixed_pilot_corrects_ordinary_robust(60,49272);
        causal_mixed_pilot_corrects_ordinary_robust(60,49273,true);
        std::cout<<"bounded raw pilot phase stage passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
