#include "live_helper_oscillator.hpp"
#include "datapump/channel.hpp"
#include "datapump/transfer.hpp"
#include <array>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
using namespace datapump;using namespace datapump::live::detail;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct Metric{std::size_t n=0,escapes=0;double error2=0,max_error=0;void add(double e,double u){++n;escapes+=std::abs(e)>u;error2+=e*e;max_error=std::max(max_error,std::abs(e));}};
void mapping_identity(){
    double max_error=0;
    for(const int side:std::array{1,-1})for(double rf:std::array{7e6,30e6,49e6})for(double a:std::array{-100e-6,100e-6})for(double c:std::array{-70e-6,70e-6}){
        const auto b=a+c+a*c,r_effective=100e-6*(1+a),H=2450.,D=1500.;
        const auto M=side*rf*r_effective+H*b,F=side*rf*r_effective+D*b,Mhat=M+.031,q=-Mhat;
        const auto corrected=F+q,selected=corrected+(H-D)*b;
        max_error=std::max(max_error,std::abs(selected-(M-Mhat)));
        check(std::abs(selected-(M-Mhat))<2e-12,"ordinary RF/B/UTC selector lost equivalence");
    }
    const auto exact=declared_hf_oscillator_tracking(2,0,0),expanded=declared_hf_oscillator_tracking(2,70,0);
    check(*exact.model.audio_acceleration_psd_ppm2_per_second3==0&&exact.model.audio_drift_prior_sigma_ppm_per_second==0&&*expanded.model.audio_acceleration_psd_ppm2_per_second3==.01,"exact/UTC-expanded source domain changed");
    LiveHelperOscillator absent;HelperOscillatorConfig unknown;check(!absent.configure(unknown,{1,0},absent.storage_required()),"unknown evolution silently became zero");
    check(!HelperOscillatorQuery{}.data_frequency_valid,"default zero DATA fields granted a certificate");
    std::cout<<"live_oscillator_mapping,PASS,effective_source_B=a+c+a*c,RF_coordinate=capture_effective,DATA_projection_not_mask=1,LF_UTC_union_unchanged=1,max_error="<<max_error<<'\n';
}
void numeric_protocol(){
    LiveHelperOscillator adapter;auto config=declared_hf_oscillator_tracking(2,100,.04);config.model.learn_repeat_visit_scatter=false;const HelperOscillatorProvider provider{29,0};check(adapter.configure(config,provider,adapter.storage_required()),"protocol configure");
    helper::LinkMapping m;HelperOscillatorSeed stale,last_observed;helper::ToneFrequencyObservation stale_raw,last_raw;std::size_t vetoes=0;
    for(std::uint64_t hop=0;hop<24;++hop){
        m={};m.capture_generation=1;m.timebase_identity=29;m.reference_identity_known=true;m.sample_rate=32000;m.phase_segment=hop+1;m.settled_first_sample=hop*12800+384;m.settled_end_sample=(hop+1)*12800;m.actual_rf_shift_hz=std::array{7e6,21e6,35e6,49e6}[hop%4];m.helper_audio_hz=2450;m.data_audio_hz=1500;m.provenance=helper::LinkMappingProvenance::modelled_physical_capture;
        check(adapter.begin_segment(hop*12800,m,provider,hop*12800),"protocol segment");const auto seed=adapter.seed();const auto before=adapter.query(seed,m.settled_first_sample,hop*12800,1500);check(!before.available,"uncaptured next-hop date granted frequency");
        if(hop>=22){check(!before.valid,"model age did not withdraw during helper loss");continue;}
        if(hop>=14){
            adapter.close_reference();
            if(hop==14){
                // A later retune with no pilot cannot revoke the frequency
                // already observed in the bounded FIFO's previous-hop PCM.
                for(const auto at:std::array{last_raw.first_sample,last_raw.state_endpoint_sample}){
                    check(adapter.direct(last_raw,last_observed,at,hop*12800).available,"next-hop closure revoked observed old raw support");
                    check(adapter.query(last_observed,at,hop*12800,1500).available,"next-hop closure revoked supported old prior");
                }
                check(!adapter.direct(last_raw,last_observed,last_raw.first_sample-1,hop*12800).available,"closed direct reached before raw support");
                check(!adapter.direct(last_raw,last_observed,last_raw.end_sample,hop*12800).available,"closed direct reached beyond raw support");
                check(!adapter.direct(last_raw,last_observed,last_raw.first_sample,last_raw.end_sample-1).available,"closed direct used future raw evidence");
                check(!adapter.query(last_observed,last_raw.end_sample,hop*12800,1500).available,"closed prior reached unobserved old-hop suffix");
                check(!adapter.query(seed,m.settled_first_sample,m.settled_first_sample+1,1500).available,"closed next-hop seed armed without pilot");
                check(!adapter.current(m.settled_first_sample,m.settled_first_sample+1,1500).available,"closed current state armed without pilot");
            }
            continue;
        }
        helper::ToneFrequencyObservation o;o.valid=true;o.first_sample=m.settled_first_sample;o.end_sample=o.first_sample+6400;o.state_endpoint_sample=o.end_sample-1;const auto t=static_cast<double>(o.state_endpoint_sample-o.first_sample)/32000.;o.mass=o.k_prime_at_end=1;o.mu_seconds=o.k_at_end_seconds=t/2;o.integral_k_seconds2=3*t*t/20;o.integral_b_minus_s_k_seconds3=t*t*t/30;o.integral_k_squared_seconds3=23*t*t*t/504;const auto lag_samples=o.mu_seconds*32000,lag=std::ceil(lag_samples);o.mean_sample=o.state_endpoint_sample-static_cast<std::uint64_t>(lag);o.mean_sample_fraction=lag-lag_samples;o.frequency_hz=2450+m.actual_rf_shift_hz*1e-6+.245+(hop>=12?m.actual_rf_shift_hz*.7e-6:0);o.independent_variance_hz2=.0001;
        const auto update=adapter.observe(o,provider,o.end_sample);check(update.accepted,"protocol raw fit");vetoes+=update.innovation_veto;
        const auto joint=adapter.current(o.state_endpoint_sample,o.end_sample,1500);
        check(joint.valid&&joint.available&&joint.data_frequency_valid,"finite joint state omitted DATA validity");
        const auto direct=adapter.direct(o,update.seed,o.state_endpoint_sample,o.end_sample);
        check(direct.valid&&direct.available&&!direct.data_frequency_valid,"direct Helper aperture granted DATA validity");
        last_observed=update.seed;last_raw=o;
        if(hop==11){stale=update.seed;stale_raw=o;}
        if(update.innovation_veto){
            check(!adapter.query(stale,stale.mapping.settled_first_sample,o.end_sample,1500).valid,"veto retained old-model seed");
            check(!adapter.query(stale,stale.mapping.settled_first_sample,o.end_sample,1500).data_frequency_valid,"vetoed state retained DATA validity");
            check(adapter.assumptions().model.retune_variance_floor_hz2==.04,"veto erased random-retune floor");
            check(adapter.direct(o,update.seed,o.first_sample,o.end_sample).available,"veto erased measured direct fallback");
            for(const auto at:std::array{stale_raw.first_sample,stale_raw.state_endpoint_sample})
                check(adapter.direct(stale_raw,stale,at,o.end_sample).available,"model veto revoked independent old raw aperture");
            check(!adapter.direct(stale_raw,stale,stale_raw.first_sample-1,o.end_sample).available,"veto direct reached before support");
            check(!adapter.direct(stale_raw,stale,stale_raw.end_sample,o.end_sample).available,"veto direct reached beyond support");
            check(!adapter.direct(stale_raw,stale,stale_raw.first_sample,stale_raw.end_sample-1).available,"veto direct used uncaptured evidence");
        }
    }
    std::cout<<"live_oscillator_protocol_veto_count,"<<vetoes<<'\n';
    check(vetoes==1,"6sigma control did not exercise one veto");
    std::cout<<"live_oscillator_protocol,PASS,acquisition_evidence=0,innovation_vetoes="<<vetoes<<",retune_floor_preserved=1,age_withdrawal=1,loss_closes_future_authority=1,observed_old_hop_support_retained=1,next_hop_without_pilot_unavailable=1\n";
}
template<class T> concept CarriesPhaseOrTimeAuthority=requires(T value){value.phase_valid;}||requires(T value){value.timing_valid;};
static_assert(!CarriesPhaseOrTimeAuthority<HelperOscillatorQuery>);
void numeric_frequency_coast(){
    LiveHelperOscillator adapter;auto config=declared_hf_oscillator_tracking(2,100,.04);config.model.learn_repeat_visit_scatter=false;
    const HelperOscillatorProvider provider{41,0};check(adapter.configure(config,provider,adapter.storage_required()),"coast configure");
    helper::LinkMapping m;m.capture_generation=1;m.timebase_identity=41;m.reference_identity_known=true;m.sample_rate=32000;m.phase_segment=1;
    m.settled_first_sample=384;m.settled_end_sample=12800;m.actual_rf_shift_hz=7e6;m.helper_audio_hz=2450;m.data_audio_hz=1500;m.provenance=helper::LinkMappingProvenance::modelled_physical_capture;
    check(adapter.begin_segment(0,m,provider,0),"coast initial segment");
    helper::ToneFrequencyObservation o;o.valid=true;o.first_sample=m.settled_first_sample;o.end_sample=o.first_sample+6400;o.state_endpoint_sample=o.end_sample-1;
    const auto t=static_cast<double>(o.state_endpoint_sample-o.first_sample)/m.sample_rate;o.mass=o.k_prime_at_end=1;o.mu_seconds=o.k_at_end_seconds=t/2;
    o.integral_k_seconds2=3*t*t/20;o.integral_b_minus_s_k_seconds3=t*t*t/30;o.integral_k_squared_seconds3=23*t*t*t/504;
    const auto lag_samples=o.mu_seconds*m.sample_rate,lag=std::ceil(lag_samples);o.mean_sample=o.state_endpoint_sample-static_cast<std::uint64_t>(lag);o.mean_sample_fraction=lag-lag_samples;
    o.frequency_hz=2457.245;o.independent_variance_hz2=.0001;check(adapter.observe(o,provider,o.end_sample).accepted,"coast initial observation");
    m.phase_segment=2;m.settled_first_sample=13184;m.settled_end_sample=25600;m.actual_rf_shift_hz=21e6;
    check(adapter.begin_segment(12800,m,provider,12800),"coast retune");const auto saved=adapter.seed();
    check(!saved.pilot_end_sample&&saved.state.valid&&saved.state.evidence_end_sample==o.end_sample,"retune fabricated current-hop frequency evidence");
    check(!adapter.query(saved,m.settled_first_sample,m.settled_first_sample,1500).available,"coast granted uncaptured date");
    check(!adapter.query(saved,m.settled_first_sample-1,m.settled_first_sample+1,1500).available,"coast granted unsettled date");
    const auto early=adapter.query(saved,m.settled_first_sample,m.settled_first_sample+1,1500);
    check(early.valid&&early.available&&early.data_frequency_valid,"recent frequency witness failed captured settled next-hop date");
    check(early.state.evidence_end_sample==o.end_sample&&early.state.observation_age_seconds==static_cast<double>(m.settled_first_sample-o.state_endpoint_sample)/m.sample_rate,"retune renewed frequency witness age");
    const auto coast_end=o.state_endpoint_sample+static_cast<std::uint64_t>(adapter.frequency_witness_coast_seconds*m.sample_rate);
    const auto boundary=adapter.query(saved,coast_end,coast_end+1,1500),expired=adapter.query(saved,coast_end+1,coast_end+2,1500);
    check(boundary.valid&&boundary.available&&boundary.state.observation_age_seconds==.5,"frequency witness withdrew before inclusive coast boundary");
    check(expired.valid&&!expired.available&&expired.state.observation_age_seconds>.5&&expired.state.observation_age_seconds<3,"frequency availability did not expire separately from numeric model");
    check(!adapter.current(coast_end+1,coast_end+2,1500).available,"current frequency ignored witness coast");
    m.phase_segment=3;m.settled_first_sample=25984;m.settled_end_sample=38400;m.actual_rf_shift_hz=35e6;
    check(adapter.begin_segment(25600,m,provider,25600),"coast second retune");const auto next=adapter.seed();
    const auto later=adapter.query(next,m.settled_first_sample,m.settled_first_sample+1,1500);
    check(later.valid&&!later.available&&later.state.evidence_end_sample==o.end_sample&&later.state.observation_age_seconds>expired.state.observation_age_seconds,"second retune renewed expired frequency witness");
    adapter.close_reference();
    check(!adapter.query(saved,saved.mapping.settled_first_sample,m.settled_first_sample+1,1500).available,"closed reference granted a pilot-free saved coast");
    check(!adapter.current(m.settled_first_sample,m.settled_first_sample+1,1500).available,"closed reference granted current frequency");
    std::cout<<"live_oscillator_frequency_coast,PASS,acquisition_evidence=0,current_hop_fit_required=0,captured_settled_dates_only=1,retune_does_not_renew_age=1,coast_seconds="<<adapter.frequency_witness_coast_seconds<<",numeric_model_seconds="<<config.model.maximum_prediction_age_seconds<<",phase_UTC_authority=0\n";
}
void numeric_scatter_support(){
    // Numerical frequency observations only: a known random-retune floor and
    // one declared 3-Hz innovation exercise adaptive reweighting without PCM.
    LiveHelperOscillator adapter;auto config=declared_hf_oscillator_tracking(2,100,1);
    config.model.rf_drift_prior_sigma_ppm_per_second=config.model.audio_drift_prior_sigma_ppm_per_second=0;
    config.model.rf_acceleration_psd_ppm2_per_second3=config.model.audio_acceleration_psd_ppm2_per_second3=0;
    const HelperOscillatorProvider provider{37,0};check(adapter.configure(config,provider,adapter.storage_required()),"scatter configure");
    helper::LinkMapping m;HelperOscillatorSeed saved;helper::ToneFrequencyObservation raw;std::uint64_t final_end=0;
    for(std::uint64_t hop=0;hop<4;++hop){
        m={};m.capture_generation=1;m.timebase_identity=37;m.reference_identity_known=true;m.sample_rate=32000;m.phase_segment=hop+1;
        m.settled_first_sample=hop*12800+384;m.settled_end_sample=(hop+1)*12800;m.actual_rf_shift_hz=7e6;m.helper_audio_hz=2450;m.data_audio_hz=1500;m.provenance=helper::LinkMappingProvenance::modelled_physical_capture;
        check(adapter.begin_segment(hop*12800,m,provider,hop*12800),"scatter segment");
        helper::ToneFrequencyObservation o;o.valid=true;o.first_sample=m.settled_first_sample;o.end_sample=o.first_sample+6400;o.state_endpoint_sample=o.end_sample-1;
        const auto t=static_cast<double>(o.state_endpoint_sample-o.first_sample)/32000.;o.mass=o.k_prime_at_end=1;o.mu_seconds=o.k_at_end_seconds=t/2;
        o.integral_k_seconds2=3*t*t/20;o.integral_b_minus_s_k_seconds3=t*t*t/30;o.integral_k_squared_seconds3=23*t*t*t/504;
        const auto lag_samples=o.mu_seconds*32000,lag=std::ceil(lag_samples);o.mean_sample=o.state_endpoint_sample-static_cast<std::uint64_t>(lag);o.mean_sample_fraction=lag-lag_samples;
        o.frequency_hz=2450+(hop==3?3.:0.);o.independent_variance_hz2=.0001;final_end=o.end_sample;
        const auto update=adapter.observe(o,provider,final_end);check(update.accepted&&!update.innovation_veto,"scatter control rejected numerical observation");
        if(hop==2){saved=update.seed;raw=o;}
        if(hop==3){
            check(update.seed.epoch!=saved.epoch,"scatter control did not reweight model epoch");
            check(adapter.current(o.state_endpoint_sample,final_end,1500).state.retune_variance_hz2>1,"scatter control did not retain excess variance");
            check(!adapter.query(saved,raw.state_endpoint_sample,final_end,1500).valid,"scatter growth retained old-model prior");
            for(const auto at:std::array{raw.first_sample,raw.state_endpoint_sample})
                check(adapter.direct(raw,saved,at,final_end).available,"scatter growth revoked independent raw aperture");
            check(!adapter.direct(raw,saved,raw.first_sample-1,final_end).available,"scatter direct reached before support");
            check(!adapter.direct(raw,saved,raw.end_sample,final_end).available,"scatter direct reached beyond support");
            check(!adapter.direct(raw,saved,raw.first_sample,raw.end_sample-1).available,"scatter direct used uncaptured evidence");
        }
    }
    // Reuse every mapping-domain identifier after an explicit same-provider
    // reset: origin generation must still revoke the old direct certificate.
    check(adapter.reset_provider(provider),"same-provider origin reset");
    check(!adapter.current(raw.state_endpoint_sample,final_end,1500).data_frequency_valid,"provider reset retained DATA validity");
    const auto first=m.settled_end_sample;m.phase_segment++;m.settled_first_sample=first+384;m.settled_end_sample=first+12800;
    check(adapter.begin_segment(first,m,provider,first),"same-domain post-reset segment");
    check(!adapter.direct(raw,saved,raw.first_sample,first).available,"provider reset retained raw aperture with reused domain identities");
    std::cout<<"live_oscillator_scatter_support,PASS,acquisition_evidence=0,model_epoch_reweighted=1,old_model_prior_invalid=1,raw_aperture_retained=1,explicit_provider_reset_revokes_raw=1\n";
}
void run(double cn0,int side,unsigned seed){
    modem::Config c;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;c.spreading_factor=128;c.pulse_shaping=false;c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{2,0},7e6,3};
    helper::Options h;h.power_w=40;h.data_power_w=4;h.callsign="N0CALL";h.timecode=true;transfer::Options options;options.modem=c;options.timestamp=1800000000;options.key=Crypto(std::array<std::uint8_t,32>{});
    Bytes bits(64);for(std::size_t i=0;i<bits.size();++i)bits[i]=(i*7+i/3)%2;auto source=transfer::binary_transmitter(bits,options);
    const auto plan=helper::plan(h,helper::placement_band(c),120000000,0,modem::outer_dsss_transmit_gain(c));
    modem::ChannelConfig impairment;impairment.clock_error_ppm=100;impairment.frequency_offset_hz=side*7.3;impairment.seed=seed;impairment.phase_noise_degrees_per_sqrt_second=.5;
    impairment.snr_db=cn0-10*std::log10(c.sample_rate/2.)-10*std::log10(h.power_w/h.data_power_w)-20*std::log10(modem::outer_dsss_transmit_gain(c))-10*std::log10(helper::pilot_power_fraction);
    modem::SampledSimulationChannel channel(c,impairment,h);channel.set_helper_time_key(options.key);const auto rate=channel.physical_sample_rate();
    helper::ToneTrackerConfig tc{rate,plan.frequency_hz,320,.5*std::numbers::pi/180,.5,true,false};helper::ToneTracker tracker(tc,helper::ToneTracker::workspace_required(tc));
    auto adapter=std::make_unique<LiveHelperOscillator>();const HelperOscillatorProvider provider{17,tc.phase_noise_radians_per_sqrt_second};check(adapter->configure(declared_hf_oscillator_tracking(2,100,0),provider,adapter->storage_required()),"adapter configure");
    modem::SampledSimulationChannel::RfRetuneSimulation retunes;retunes.rf_shift_hz={7e6,21e6,35e6,49e6};retunes.sideband_sign=side;retunes.phase_seed=49273+seed;retunes.rf_fractional_slew_per_second=3e-9;channel.set_rf_retune_simulation(retunes);
    helper::LinkMapping mapping;HelperOscillatorSeed saved,last_accepted;helper::ToneFrequencyObservation last_raw;std::uint64_t raw_end=0,last_end=0;std::size_t hops=0,fits=0,frequency_only=0,vetoes=0;
    Metric raw,extra,prediction,current,direct;double first_fit=0,first_extra=0,model_cpu=0,last_current_U=0,last_prior_U=0;
    const auto truth=[&](double carrier,double RF,double t){return carrier*100e-6+side*7.3*RF/7e6+side*RF*3e-9*t;};
    const auto add=[&](Metric& metric,double e,double u,const char* kind,std::uint64_t date){metric.add(e,u);if(std::abs(e)>u)std::cout<<"live_oscillator_escape,kind="<<kind<<",cn0="<<cn0<<",side="<<side<<",seed="<<seed<<",phase_segment="<<mapping.phase_segment<<",sample="<<date<<",RF="<<mapping.actual_rf_shift_hz<<",error="<<e<<",U="<<u<<'\n';};
    channel.set_physical_rf_observer([&](const auto& s,std::uint32_t r){
        check(r==rate&&s.first_sample==raw_end,"RF source frontier changed");++hops;mapping={};mapping.capture_generation=1;mapping.timebase_identity=17;mapping.reference_identity=0;mapping.reference_identity_known=true;mapping.sample_rate=rate;mapping.settled_first_sample=s.settled_first_sample;mapping.settled_end_sample=s.end_sample;mapping.actual_rf_shift_hz=s.rf_shift_hz;mapping.helper_audio_hz=plan.frequency_hz;mapping.data_audio_hz=c.carrier_hz;mapping.sideband_sign=s.sideband_sign;mapping.phase_segment=s.phase_segment;mapping.provenance=helper::LinkMappingProvenance::modelled_physical_capture;
        const auto start=std::clock();check(adapter->begin_segment(s.first_sample,mapping,provider,raw_end),"actual segment declined");saved=adapter->seed();const auto p=adapter->query(saved,s.settled_first_sample,raw_end,c.carrier_hz);model_cpu+=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;check(!p.available,"prior granted frequency at uncaptured next-hop date");
        if(p.valid){check(p.state.evidence_end_sample<=s.first_sample,"next prediction used current fit");add(prediction,p.helper_residual_hz-truth(plan.frequency_hz,s.rf_shift_hz,static_cast<double>(s.settled_first_sample)/rate),p.helper_uncertainty_hz,"prior_before_current_fit",s.settled_first_sample);last_prior_U=p.helper_uncertainty_hz;}
        tracker.begin_pilot_segment(s.first_sample,plan.frequency_hz,320,false,1,s.settled_first_sample,true,s.end_sample-rate*4/1000);
    });
    channel.set_physical_observer([&](std::span<const float> pcm,std::uint32_t r,std::uint64_t first){
        check(r==rate&&first==raw_end,"PCM gap");raw_end+=pcm.size();tracker.push(pcm,first);const auto fit=tracker.snapshot();const auto& o=fit.frequency_observation;
        if(!o.valid||o.end_sample==last_end)return;last_end=o.end_sample;const auto start=std::clock();const auto update=adapter->observe(o,provider,raw_end);model_cpu+=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;check(update.accepted,"raw frequency-only certificate declined");saved=update.seed;last_accepted=saved;last_raw=o;++fits;vetoes+=update.innovation_veto;
        const auto t=static_cast<double>(o.mean_sample)/rate+o.mean_sample_fraction/rate,e=o.frequency_hz-plan.frequency_hz-truth(plan.frequency_hz,mapping.actual_rf_shift_hz,t),U=3*std::sqrt(o.independent_variance_hz2+o.residual_variance_hz2);add(raw,e,U,"raw_full_fit",o.mean_sample);
        const bool phase_correction_qualified=fit.phase_fit_qualified&&fit.phase_fit_support_uncertainty_cycles&&*fit.phase_fit_support_uncertainty_cycles<.125;
        if(!phase_correction_qualified){++frequency_only;add(extra,e,U,"additional_frequency_only",o.mean_sample);if(!first_extra)first_extra=static_cast<double>(raw_end)/rate;}
        const auto p=adapter->current(o.state_endpoint_sample,raw_end,c.carrier_hz);check(p.valid&&p.available,"accepted current frequency unavailable");add(current,p.helper_residual_hz-truth(plan.frequency_hz,mapping.actual_rf_shift_hz,static_cast<double>(o.state_endpoint_sample)/rate),p.helper_uncertainty_hz,"current_model",o.state_endpoint_sample);last_current_U=p.helper_uncertainty_hz;
        const auto earlier=adapter->query(saved,o.first_sample,raw_end,c.carrier_hz);if(saved.state.valid)check(earlier.valid&&earlier.available,"captured causal prior lost its recent independent frequency witness");
        for(const auto at:std::array{o.first_sample,o.state_endpoint_sample}){const auto q=adapter->direct(o,saved,at,raw_end);check(q.available,"direct observed frequency fallback declined");add(direct,q.helper_residual_hz-truth(plan.frequency_hz,mapping.actual_rf_shift_hz,static_cast<double>(at)/rate),q.helper_uncertainty_hz,"direct_slew_interval",at);}
        check(adapter->observe(o,provider,raw_end).duplicate,"same raw snapshot counted twice");if(!first_fit)first_fit=static_cast<double>(raw_end)/rate;
        std::cout<<"live_oscillator_row,cn0="<<cn0<<",side="<<side<<",seed="<<seed<<",phase_segment="<<mapping.phase_segment<<",RF="<<mapping.actual_rf_shift_hz<<",first="<<o.first_sample<<",end="<<o.end_sample<<",mean_sample="<<o.mean_sample<<",mean_fraction="<<o.mean_sample_fraction<<",raw_hz="<<o.frequency_hz<<",Rind="<<o.independent_variance_hz2<<",Rextra="<<o.residual_variance_hz2<<",frequency_qualified="<<fit.qualified<<",phase_fit_qualified="<<fit.phase_fit_qualified<<",phase_qualified_subset="<<phase_correction_qualified<<",veto="<<update.innovation_veto<<'\n';
    });
    std::array<float,257> discard{};std::uint64_t logical=0;const auto clock=std::clock();while(logical<32ULL*c.sample_rate){const auto n=channel.read(*source,std::span(discard).first(std::min<std::uint64_t>(discard.size(),32ULL*c.sample_rate-logical)));check(n,"source ended");logical+=n;}const auto all_cpu=static_cast<double>(std::clock()-clock)/CLOCKS_PER_SEC;
    adapter->close_reference();
    for(const auto at:std::array{last_raw.first_sample,last_raw.state_endpoint_sample})
        check(adapter->direct(last_raw,last_accepted,at,raw_end).available,"helper loss revoked actually observed raw support");
    check(!adapter->direct(last_raw,last_accepted,last_raw.first_sample-1,raw_end).available,"helper loss direct reached before observed support");
    check(!adapter->direct(last_raw,last_accepted,last_raw.end_sample,raw_end).available,"helper loss direct reached beyond observed support");
    check(!adapter->direct(last_raw,last_accepted,last_raw.first_sample,last_raw.end_sample-1).available,"helper loss direct used uncaptured evidence");
    if(last_accepted.state.valid){
        check(adapter->query(last_accepted,last_raw.state_endpoint_sample,raw_end,c.carrier_hz).available,"helper loss revoked observed saved prior");
        check(!adapter->query(last_accepted,last_raw.end_sample,raw_end,c.carrier_hz).available,"helper loss prior armed unobserved suffix");
    }
    check(!adapter->current(last_raw.state_endpoint_sample,raw_end,c.carrier_hz).available,"helper loss retained current frequency authority");
    auto bad=mapping;bad.reference_identity++;bad.phase_segment++;bad.settled_first_sample=mapping.settled_end_sample+384;bad.settled_end_sample=mapping.settled_end_sample+12800;check(!adapter->begin_segment(mapping.settled_end_sample,bad,provider,mapping.settled_end_sample),"reference change retained model");check(!adapter->query(last_accepted,last_raw.first_sample,raw_end,c.carrier_hz).valid,"reference change left dated seed live");
    check(adapter->reset_provider({18,provider.phase_noise_radians_per_sqrt_second}),"explicit provider reset");auto microwave=bad;microwave.actual_rf_shift_hz=5.8e9;check(!adapter->begin_segment(mapping.settled_end_sample,microwave,{18,provider.phase_noise_radians_per_sqrt_second},mapping.settled_end_sample),"HF model qualified microwave mapping");
    const auto rms=[](const Metric& m){return std::sqrt(m.error2/std::max<std::size_t>(1,m.n));};
    std::cout<<"live_oscillator_summary,cn0_target="<<cn0<<",side="<<side<<",seed="<<seed<<",hops="<<hops<<",fits="<<fits<<",new_frequency_only="<<frequency_only<<",first_fit_s="<<first_fit<<",first_extra_s="<<first_extra<<",raw_escapes="<<raw.escapes<<",extra_escapes="<<extra.escapes<<",raw_RMSE="<<rms(raw)<<",predictions_before_fit="<<prediction.n<<",prediction_escapes="<<prediction.escapes<<",prediction_RMSE="<<rms(prediction)<<",last_prior_U="<<last_prior_U<<",current_escapes="<<current.escapes<<",current_RMSE="<<rms(current)<<",last_current_U="<<last_current_U<<",direct_endpoint_escapes="<<direct.escapes<<",innovation_vetoes="<<vetoes<<",model_CPU_s="<<model_cpu<<",all_CPU_s="<<all_cpu<<",storage_bytes="<<adapter->storage_required()<<",phase_UTC_authority=0,truth_seed=0,actual_RF_and_independent_phases=1,constant_fractional_drift_only=1,hardware_PSD_calibration=0,nominal_Hann_kernel_approximation=1\n";
}
int main(int argc,char**)try{std::cout<<std::setprecision(12);mapping_identity();numeric_protocol();numeric_frequency_coast();numeric_scatter_support();if(argc>1)return 0;run(40,1,5181);run(40,-1,5181);run(30,1,5181);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
