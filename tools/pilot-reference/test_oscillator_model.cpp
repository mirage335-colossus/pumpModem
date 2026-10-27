#include "helper_oscillator_model.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_tone_tracker.hpp"
#include "datapump/transfer.hpp"
#include <array>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string_view>
using namespace datapump;using namespace datapump::helper;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
OscillatorFrequencyModelConfig config() {
    OscillatorFrequencyModelConfig c;c.rf_prior_sigma_ppm=2;c.audio_prior_sigma_ppm=100;
    c.rf_drift_prior_sigma_ppm_per_second=.01;c.audio_drift_prior_sigma_ppm_per_second=.1;
    c.rf_acceleration_psd_ppm2_per_second3=0;c.audio_acceleration_psd_ppm2_per_second3=0;
    c.maximum_prediction_age_seconds=3;return c;
}
LinkMapping mapping(std::uint64_t hop,std::uint32_t rate,double rf,int sideband=1) {
    LinkMapping m;m.capture_generation=1;m.timebase_identity=2;m.reference_identity=3;m.reference_identity_known=true;
    m.sample_rate=rate;m.settled_first_sample=hop*rate*2/5+rate*12/1000;m.settled_end_sample=(hop+1)*rate*2/5;
    m.actual_rf_shift_hz=rf;m.helper_audio_hz=2450;m.data_audio_hz=1500;m.sideband_sign=sideband;
    m.provenance=LinkMappingProvenance::modelled_physical_capture;m.phase_segment=hop+1;return m;
}
struct Metric {std::size_t n=0,escapes=0;double error2=0,u=0,last_error=0,max_error=0;void add(double error,double bound){++n;escapes+=std::abs(error)>bound;error2+=error*error;u=bound;last_error=error;max_error=std::max(max_error,std::abs(error));}};
void gaussian_model_only(bool scatter,bool learn=true,double floor=0) {
    constexpr std::uint32_t rate=48000;constexpr std::array<double,4> rf{7e6,21e6,35e6,49e6};
    Metric aggregate,warmup;double final_rf_sigma=0,final_audio_sigma=0,learned=0;std::size_t accepted=0;
    for(unsigned seed=1;seed<=32;++seed) {
        OscillatorFrequencyModel model;auto c=config();c.learn_repeat_visit_scatter=scatter&&learn;c.retune_variance_floor_hz2=floor;require(model.reset(c),"model-only reset");
        std::mt19937 random(7351+seed);std::normal_distribution<double> normal;
        for(std::uint64_t hop=0;hop<200;++hop) {
            const auto m=mapping(hop,rate,rf[hop%4],seed%2?1:-1);
            const auto sample=(m.settled_first_sample+m.settled_end_sample-1)/2;const auto t=static_cast<double>(sample)/rate;
            const auto truth=(m.sideband_sign*m.actual_rf_shift_hz*(1.04+.0005*t)+m.helper_audio_hz*(100+.02*t))*1e-6;
            const auto prediction=model.predict(m,m.settled_first_sample);
            const auto predict_t=static_cast<double>(m.settled_first_sample)/rate;
            const auto predict_truth=(m.sideband_sign*m.actual_rf_shift_hz*(1.04+.0005*predict_t)+m.helper_audio_hz*(100+.02*predict_t))*1e-6;
            const auto retune_error=scatter?.2*normal(random):0.;
            if(prediction.valid){const auto error=prediction.helper_residual_hz-predict_truth-retune_error;const auto bound=prediction.helper_three_sigma_hz();
                (hop>=20?aggregate:warmup).add(error,bound);
                if(std::abs(error)>bound)std::cout<<"oscillator_model_escape,model_only=1,scatter="<<scatter<<",learn="<<learn<<",floor_hz2="<<floor<<",seed="<<seed<<",hop="<<hop<<",date_s="<<predict_t<<",rf_hz="<<m.actual_rf_shift_hz<<",error_hz="<<error<<",U_hz="<<bound<<",warmup="<<(hop<20)<<'\n';}
            const auto duration=.36,variance=3/(2*std::numbers::pi*std::numbers::pi*10000*duration*duration*duration);
            IndependentFrequencyAperture measurement{m,m.settled_first_sample,m.settled_end_sample,sample,0,
                m.helper_audio_hz+truth+retune_error+std::sqrt(variance)*normal(random),variance,duration};
            require(model.observe(measurement,m.settled_end_sample)==OscillatorModelReason::accepted,"model-only observe");++accepted;
            if(hop==199) {const auto p=model.predict(m,m.settled_end_sample-1);final_rf_sigma=p.rf_fraction_sigma*1e6;final_audio_sigma=p.audio_fraction_sigma*1e6;learned=p.retune_variance_hz2;require(p.evidence_first_sample<rate,"history older than36s was discarded");}
        }
    }
    std::cout<<"oscillator_model_only,scatter_estimator=signed_average,causal_acquisition=0,random_retune_error="<<scatter<<",learn_scatter="<<learn<<",configured_retune_variance_floor_hz2="<<floor<<",predictions="<<aggregate.n<<",three_sigma_escapes="<<aggregate.escapes<<",rmse_hz="<<std::sqrt(aggregate.error2/aggregate.n)<<",last_U_hz="<<aggregate.u<<",max_error_hz="<<aggregate.max_error<<",last_rf_sigma_ppm="<<final_rf_sigma<<",last_audio_sigma_ppm="<<final_audio_sigma<<",learned_retune_variance_hz2="<<learned<<",accepted="<<accepted<<",warmup_predictions="<<warmup.n<<",warmup_escapes="<<warmup.escapes<<",warmup_rmse_hz="<<std::sqrt(warmup.error2/warmup.n)<<'\n';
}
void boundaries() {
    OscillatorFrequencyModel model;auto c=config();auto unknown=c;unknown.rf_acceleration_psd_ppm2_per_second3.reset();require(!model.reset(unknown),"unknown drift became zero");require(model.reset(c),"reset");
    auto m=mapping(0,48000,7e6);IndependentFrequencyAperture a{m,m.settled_first_sample,m.settled_end_sample,10000,0,2457.3,.01,.3};
    require(model.observe(a,a.end_sample-1)==OscillatorModelReason::invalid_measurement,"future support accepted");
    require(model.observe(a,a.end_sample)==OscillatorModelReason::accepted,"initial measurement");
    require(model.observe(a,a.end_sample)==OscillatorModelReason::overlapping_support,"duplicate fit gained information");
    const auto p=model.predict(m,m.settled_end_sample-1);require(p.valid&&p.audio_fraction_sigma>99e-6,"one RF spuriously resolved audio clock");
    auto bad=m;bad.reference_identity=4;require(!model.predict(bad,m.settled_end_sample-1).valid,"reference mismatch");
    bad=m;bad.provenance=LinkMappingProvenance::fake_schedule_preview;require(!model.predict(bad,m.settled_end_sample-1).valid,"fake RF mapping accepted");
    OscillatorFrequencyModel ring;require(ring.reset(c),"ring reset");
    for(std::uint64_t hop=0;hop<400;++hop){const auto r=mapping(hop,48000,7e6);IndependentFrequencyAperture v{r,r.settled_first_sample,r.settled_end_sample,(r.settled_first_sample+r.settled_end_sample)/2,0,2457.3,.01,.3};require(ring.observe(v,v.end_sample)==OscillatorModelReason::accepted,"bounded ring update");if(hop==399){const auto q=ring.predict(r,r.settled_end_sample-1);require(q.valid&&q.retained_measurements==256&&q.measurements==400&&q.evidence_first_sample>0&&(q.sample-q.evidence_first_sample)/48000.>100,"bounded ring retention/support");}}
    auto shared=c;shared.topology=LinkTopology::shared_radio;OscillatorFrequencyModel shared_model;require(shared_model.reset(shared),"shared reset");auto zero=m;zero.actual_rf_shift_hz=0;auto z=a;z.mapping=zero;require(shared_model.observe(z,z.end_sample)==OscillatorModelReason::invalid_mapping,"shared RF0 silently used RF prior");
    auto reused=a;reused.mapping=mapping(1,48000,7e6);reused.mapping.phase_segment=m.phase_segment;reused.first_sample=reused.mapping.settled_first_sample;reused.end_sample=reused.mapping.settled_end_sample;reused.sample=reused.first_sample+10000;require(model.observe(reused,reused.end_sample)==OscillatorModelReason::reused_phase_segment,"shared retune error averaged twice");
    auto changed=mapping(1,48000,21e6);changed.provenance=LinkMappingProvenance::fixed_configuration;require(!model.predict(changed,changed.settled_first_sample).valid,"fixed configuration manufactured RF transition");
    OscillatorFrequencyModel small_origin,large_origin;require(small_origin.reset(c)&&large_origin.reset(c),"large origin reset");
    constexpr auto origin=UINT64_MAX-1000000ULL;
    for(std::uint64_t hop=0;hop<4;++hop){auto near=mapping(hop,48000,std::array{7e6,21e6,35e6,49e6}[hop]);IndependentFrequencyAperture x{near,near.settled_first_sample,near.settled_end_sample,near.settled_first_sample+(near.settled_end_sample-near.settled_first_sample)/2,0,2450+near.actual_rf_shift_hz*1e-6,.01,.3};auto far=x;far.first_sample+=origin;far.end_sample+=origin;far.sample+=origin;far.mapping.settled_first_sample+=origin;far.mapping.settled_end_sample+=origin;require(small_origin.observe(x,x.end_sample)==OscillatorModelReason::accepted&&large_origin.observe(far,far.end_sample)==OscillatorModelReason::accepted,"large sample origin observation");const auto u=small_origin.predict(near,near.settled_end_sample-1),v=large_origin.predict(far.mapping,far.end_sample-1);require(u.valid&&v.valid&&std::abs(u.helper_residual_hz-v.helper_residual_hz)<1e-10&&std::abs(u.helper_variance_hz2-v.helper_variance_hz2)<1e-10,"absolute epoch lost sample precision");}
    auto acceleration=c;acceleration.rf_acceleration_psd_ppm2_per_second3=.001;OscillatorFrequencyModel acceleration_model;require(acceleration_model.reset(acceleration),"explicit positive process declined");require(acceleration_model.observe(a,a.end_sample)==OscillatorModelReason::invalid_measurement,"positive process accepted a missing aperture kernel");
    auto cancelled=m;cancelled.actual_rf_shift_hz=cancelled.helper_audio_hz;cancelled.sideband_sign=-1;auto cancel_measure=a;cancel_measure.mapping=cancelled;require(shared_model.observe(cancel_measure,cancel_measure.end_sample)==OscillatorModelReason::invalid_mapping,"zero shared helper coefficient accepted");
    OscillatorFrequencyModel scalar_shared;require(scalar_shared.reset(shared),"scalar shared reset");auto measurement=a;const auto h=(m.actual_rf_shift_hz+m.helper_audio_hz)*1e-6;measurement.frequency_hz=m.helper_audio_hz+h;require(scalar_shared.observe(measurement,measurement.end_sample)==OscillatorModelReason::accepted,"scalar shared measurement");const auto q=scalar_shared.predict(m,measurement.end_sample-1);
    // Independent forward scalar reference: prior at support START, measured
    // at its dated mean, posterior queried at the last observed sample.
    const auto t=static_cast<double>(measurement.end_sample-1-measurement.first_sample)/m.sample_rate;
    const auto u=static_cast<double>(measurement.sample-measurement.first_sample)/m.sample_rate;
    const auto d=shared.rf_drift_prior_sigma_ppm_per_second*shared.rf_drift_prior_sigma_ppm_per_second;
    const auto s=h*h*(4+u*u*d)+measurement.variance_hz2,cross=h*(4+t*u*d);
    const auto mean=cross*h/s,variance=4+t*t*d-cross*cross/s;
    require(q.valid&&std::abs(q.helper_residual_hz-h*mean)<1e-12&&std::abs(q.helper_variance_hz2-h*h*variance)<1e-11&&q.fractional_covariance[0][1]==q.fractional_covariance[0][0]&&q.audio_fraction==q.rf_fraction,"shared posterior disagreed with scalar reference");
    std::cout<<"oscillator_model_boundaries,PASS,unresolved_audio_sigma_ppm="<<p.audio_fraction_sigma*1e6<<",model_bytes="<<model.workspace_bytes()<<'\n';
}
FrequencyApertureProcessMoments parabolic_kernel(std::uint64_t first,std::uint64_t end,std::uint32_t rate) {
    const auto t=static_cast<double>(end-1-first)/rate;
    return {true,first,end,end-1,1,t/2,3*t*t/20,t*t*t/30,23*t*t*t/504,t/2,1};
}
void attach_kernel(IndependentFrequencyAperture& a,const FrequencyApertureProcessMoments& k) {
    const auto lag_samples=k.mu_seconds*a.mapping.sample_rate;
    const auto lag=static_cast<std::uint64_t>(std::ceil(lag_samples));
    a.sample=k.state_endpoint_sample-lag;a.sample_fraction=static_cast<double>(lag)-lag_samples;a.process_moments=k;
}
void process_reference() {
    // Independent FORWARD joint Gaussian calculation, prior at aperture START.
    // This does not reuse the implementation's endpoint H/C/R update formula.
    double maximum_covariance_error=0,maximum_mean_error=0;
    for(const auto topology:std::array{LinkTopology::independent_audio,LinkTopology::shared_radio})
    for(const auto side:std::array{1,-1})for(const bool actual_geometry:std::array{false,true}) {
        auto c=config();c.topology=topology;c.rf_acceleration_psd_ppm2_per_second3=.001;c.audio_acceleration_psd_ppm2_per_second3=.02;c.learn_repeat_visit_scatter=false;
        auto m=mapping(0,32000,49e6,side);
        IndependentFrequencyAperture a{m,384,12672,0,0,m.helper_audio_hz+3.7,.02,.384};
        auto k=parabolic_kernel(a.first_sample,a.end_sample,m.sample_rate);
        if(actual_geometry)k={true,a.first_sample,a.end_sample,a.end_sample-1,1,.193605017277584,.0223581789636109,.00190916771321556,.00263785388378363,.193605017277584,1};
        attach_kernel(a,k);OscillatorFrequencyModel model;require(model.reset(c)&&model.observe(a,a.end_sample)==OscillatorModelReason::accepted,"process oracle observation");
        const auto p=model.predict(m,a.end_sample-1);require(p.valid&&p.sample==a.end_sample-1&&p.evidence_end_sample==a.end_sample,"process endpoint contract");
        const auto t=static_cast<double>(a.end_sample-1-a.first_sample)/m.sample_rate,mu=k.mu_seconds;
        const bool shared=topology==LinkTopology::shared_radio;
        const std::array<double,2> coefficient{(side*m.actual_rf_shift_hz+(shared?m.helper_audio_hz:0))*1e-6,shared?0:m.helper_audio_hz*1e-6};
        const std::array<double,2> level{4,shared?0:10000},rate{.0001,shared?0:.01},density{.001,shared?0:.02};
        std::array<double,4> cross{},mean{};std::array<std::array<double,4>,4> covariance{};double variance=a.variance_hz2;
        for(std::size_t i=0;i<2;++i) {
            const auto q=density[i],h=coefficient[i];
            covariance[i][i]=level[i]+t*t*rate[i]+q*t*t*t/3;
            covariance[i][i+2]=covariance[i+2][i]=t*rate[i]+q*t*t/2;
            covariance[i+2][i+2]=rate[i]+q*t;
            cross[i]=h*(level[i]+t*(t-mu)*rate[i]+q*(t*t*t/3-mu*t*t/2+k.integral_b_minus_s_k_seconds3));
            cross[i+2]=h*((t-mu)*rate[i]+q*(t*t/2-mu*t+k.integral_k_seconds2));
            variance+=h*h*(level[i]+(t-mu)*(t-mu)*rate[i]+q*(t*t*t/3-mu*t*t+mu*mu*t+2*k.integral_b_minus_s_k_seconds3-2*mu*k.integral_k_seconds2+k.integral_k_squared_seconds3));
        }
        for(std::size_t i=0;i<4;++i){mean[i]=cross[i]*3.7/variance;for(std::size_t j=0;j<4;++j)covariance[i][j]-=cross[i]*cross[j]/variance;}
        const auto map_index=[&](std::size_t i){return shared?(i==1?0:i==3?2:i):i;};
        for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j){const auto expected=covariance[map_index(i)][map_index(j)];const auto error=std::abs(p.fractional_covariance[i][j]*1e12-expected);maximum_covariance_error=std::max(maximum_covariance_error,error);require(error<2e-9*(1+std::abs(expected)),"correlated process covariance disagreed with forward Gaussian oracle");}
        const std::array<double,4> observed{p.rf_fraction*1e6,p.audio_fraction*1e6,p.rf_drift_fraction_per_second*1e6,p.audio_drift_fraction_per_second*1e6};
        for(std::size_t i=0;i<4;++i){const auto error=std::abs(observed[i]-mean[map_index(i)]);maximum_mean_error=std::max(maximum_mean_error,error);require(error<1e-10,"process posterior mean disagreed with forward Gaussian oracle");}
        const auto data0=(side*m.actual_rf_shift_hz+(shared?m.data_audio_hz:0))*1e-6,data1=shared?0:m.data_audio_hz*1e-6;
        const auto data_mean=data0*mean[0]+data1*mean[1],data_variance=data0*data0*covariance[0][0]+2*data0*data1*covariance[0][1]+data1*data1*covariance[1][1];
        require(std::abs(p.data_residual_hz-data_mean)<1e-10&&std::abs(p.data_variance_hz2-data_variance)<2e-8,"DATA projection lost full process covariance");
        require(!model.predict(m,a.end_sample-2).valid,"endpoint model manufactured an earlier state");
        // Every malformed certificate is declined before it can change state.
        for(unsigned mutation=0;mutation<5;++mutation){auto invalid=a;auto& x=*invalid.process_moments;if(mutation==0)x.state_endpoint_sample++;if(mutation==1)x.mu_seconds+=1e-4;if(mutation==2)x.integral_k_squared_seconds3=0;if(mutation==3)x.mass=.9;if(mutation==4)x.integral_k_seconds2=std::numeric_limits<double>::infinity();OscillatorFrequencyModel declined;require(declined.reset(c)&&declined.observe(invalid,invalid.end_sample)==OscillatorModelReason::invalid_measurement,"invalid process kernel accepted");}
    }
    // Q0 is the exact zero-density limit with the SAME START prior and dates.
    // The old archived mean-prior model is reported separately in numeric tests.
    auto c=config();c.learn_repeat_visit_scatter=false;OscillatorFrequencyModel dated,certified;
    require(dated.reset(c)&&certified.reset(c),"zero process limit reset");
    for(std::uint64_t hop=0;hop<40;++hop){const auto m=mapping(hop,32000,std::array{7e6,21e6,35e6,49e6}[hop%4],hop%2?1:-1);IndependentFrequencyAperture a{m,m.settled_first_sample,m.settled_end_sample,0,0,2450+m.sideband_sign*m.actual_rf_shift_hz*1.04e-6,.001,.38};attach_kernel(a,parabolic_kernel(a.first_sample,a.end_sample,m.sample_rate));auto plain=a;plain.process_moments.reset();require(dated.observe(plain,plain.end_sample)==OscillatorModelReason::accepted&&certified.observe(a,a.end_sample)==OscillatorModelReason::accepted,"zero process limit observation");const auto p=dated.predict(m,a.end_sample-1),q=certified.predict(m,a.end_sample-1);require(p.valid&&q.valid&&std::abs(p.helper_residual_hz-q.helper_residual_hz)<1e-12&&std::abs(p.helper_variance_hz2-q.helper_variance_hz2)<1e-12,"Q0 kernel and dated models differ");}
    // Positive process moments retain integer sample coordinates near UINT64_MAX.
    c.rf_acceleration_psd_ppm2_per_second3=.001;c.audio_acceleration_psd_ppm2_per_second3=.02;OscillatorFrequencyModel near,far;require(near.reset(c)&&far.reset(c),"positive process origin reset");
    constexpr auto origin=UINT64_MAX-1000000ULL;
    for(std::uint64_t hop=0;hop<4;++hop){auto m=mapping(hop,32000,std::array{7e6,21e6,35e6,49e6}[hop]);IndependentFrequencyAperture a{m,m.settled_first_sample,m.settled_end_sample,0,0,2457.3,.01,.38};attach_kernel(a,parabolic_kernel(a.first_sample,a.end_sample,m.sample_rate));auto shifted=a;shifted.first_sample+=origin;shifted.end_sample+=origin;shifted.sample+=origin;shifted.mapping.settled_first_sample+=origin;shifted.mapping.settled_end_sample+=origin;shifted.process_moments->first_sample+=origin;shifted.process_moments->end_sample+=origin;shifted.process_moments->state_endpoint_sample+=origin;require(near.observe(a,a.end_sample)==OscillatorModelReason::accepted&&far.observe(shifted,shifted.end_sample)==OscillatorModelReason::accepted,"positive process large origin observation");const auto p=near.predict(m,a.end_sample-1),q=far.predict(shifted.mapping,shifted.end_sample-1);require(p.valid&&q.valid&&std::abs(p.helper_residual_hz-q.helper_residual_hz)<1e-12&&std::abs(p.helper_variance_hz2-q.helper_variance_hz2)<1e-12,"positive process lost large-origin sample dates");}
    OscillatorFrequencyModel bounded;require(bounded.reset(c),"positive process ring reset");
    for(std::uint64_t hop=0;hop<400;++hop){const auto m=mapping(hop,32000,std::array{7e6,21e6,35e6,49e6}[hop%4]);IndependentFrequencyAperture a{m,m.settled_first_sample,m.settled_end_sample,0,0,2450+m.actual_rf_shift_hz*1e-6,.01,.38};attach_kernel(a,parabolic_kernel(a.first_sample,a.end_sample,m.sample_rate));require(bounded.observe(a,a.end_sample)==OscillatorModelReason::accepted,"positive process bounded replay failed");if(hop==399){const auto p=bounded.predict(m,a.end_sample-1);require(p.valid&&p.retained_measurements==256&&p.measurements==400&&(p.sample-p.evidence_first_sample)/32000.>100,"positive process numeric history was unbounded or expired at36s");}}
    std::cout<<"oscillator_process_reference,PASS,acquisition_evidence=0,oracle=forward_joint_Gaussian,endpoint=last_observed_sample,first_prior=aperture_start,Q0_same_prior_equivalence=1,actual_kernel_geometry_included=1,max_covariance_error_ppm2="<<maximum_covariance_error<<",max_mean_error_ppm="<<maximum_mean_error<<'\n';
}
void decreasing_then_increasing_scatter(bool positive=false) {
    auto c=config();if(positive){c.rf_acceleration_psd_ppm2_per_second3=1e-6;c.audio_acceleration_psd_ppm2_per_second3=.01;}OscillatorFrequencyModel adaptive;require(adaptive.reset(c),"adaptive replay reset");
    std::array<IndependentFrequencyAperture,100> history{};double previous=0,maximum=0;std::size_t checked=0;
    for(std::uint64_t hop=0;hop<history.size();++hop){
        const auto m=mapping(hop,48000,std::array{7e6,21e6,35e6,49e6}[hop%4]);
        const auto extra=hop<8?(hop%2?2.:-2.):hop==71?.8:0.;
        history[hop]={m,m.settled_first_sample,m.settled_end_sample,(m.settled_first_sample+m.settled_end_sample)/2,0,2450+m.actual_rf_shift_hz*1e-6+extra,.001,.36};
        if(positive)attach_kernel(history[hop],parabolic_kernel(history[hop].first_sample,history[hop].end_sample,m.sample_rate));
        require(adaptive.observe(history[hop],m.settled_end_sample)==OscillatorModelReason::accepted,"adaptive replay observe");
        const auto p=adaptive.predict(m,m.settled_end_sample-1);require(p.valid,"adaptive replay predict");
        if(p.retune_variance_hz2>previous&&p.retune_variance_hz2<maximum){
            auto fixed=c;fixed.learn_repeat_visit_scatter=false;fixed.retune_variance_floor_hz2=p.retune_variance_hz2;
            OscillatorFrequencyModel replay;require(replay.reset(fixed),"fixedR replay reset");
            for(std::size_t i=0;i<=hop;++i)require(replay.observe(history[i],history[i].end_sample)==OscillatorModelReason::accepted,"fixedR replay observe");
            const auto expected=replay.predict(m,m.settled_end_sample-1);
            require(std::abs(p.helper_residual_hz-expected.helper_residual_hz)<1e-9&&std::abs(p.helper_variance_hz2-expected.helper_variance_hz2)<1e-9,"decreased-then-increased scatter left stale information");++checked;
        }
        previous=p.retune_variance_hz2;maximum=std::max(maximum,previous);
    }
    require(checked>0,"scatter regression did not exercise sub-high-water increases");
    std::cout<<"oscillator_scatter_reweight_regression,PASS,positive_Q="<<positive<<",sub_highwater_increases="<<checked<<'\n';
}
void actual_channel(double cn0,unsigned seed) {
    modem::Config cfg;cfg.sample_rate=8000;cfg.carrier_hz=1500;cfg.bandwidth_hz=200;cfg.spreading_factor=128;cfg.pulse_shaping=false;
    cfg.oscillator_search=modem::OscillatorSearchConfig{{100,0},{2,0},7e6,3};
    transfer::Options options;options.modem=cfg;options.timestamp=1800000000;options.key=Crypto(std::array<std::uint8_t,32>{});
    Bytes bits(64);for(std::size_t i=0;i<bits.size();++i)bits[i]=(i*7+i/3)%2;auto source=transfer::binary_transmitter(bits,options);
    helper::Options helper;helper.power_w=40;helper.data_power_w=4;helper.callsign="N0CALL";helper.timecode=true;
    const auto plan=helper::plan(helper,helper::placement_band(cfg),120000000,0,modem::outer_dsss_transmit_gain(cfg));
    modem::ChannelConfig impair;impair.clock_error_ppm=100;impair.frequency_offset_hz=7.3;impair.seed=seed;
    impair.snr_db=cn0-10*std::log10(cfg.sample_rate/2.)-10*std::log10(helper.power_w/helper.data_power_w)-20*std::log10(modem::outer_dsss_transmit_gain(cfg))-10*std::log10(pilot_power_fraction);
    modem::SampledSimulationChannel channel(cfg,impair,helper);channel.set_helper_time_key(options.key);
    const auto rate=channel.physical_sample_rate();ToneTrackerConfig tc{rate,plan.frequency_hz,180,impair.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180,.5,true};
    ToneTracker tracker(tc,ToneTracker::workspace_required(tc));OscillatorFrequencyModel model;auto mc=config();mc.rf_drift_prior_sigma_ppm_per_second=0;mc.audio_drift_prior_sigma_ppm_per_second=0;require(model.reset(mc),"channel model reset");
    modem::SampledSimulationChannel::RfRetuneSimulation retunes;retunes.rf_shift_hz={7e6,21e6,35e6,49e6};retunes.phase_seed=49273+seed;channel.set_rf_retune_simulation(retunes);
    modem::SampledSimulationChannel::PhysicalRfSegment segment;LinkMapping m;Metric local,predicted,updated;std::size_t fits=0,hops=0;std::uint64_t accepted_segment=0,last_context=0;double first=0;std::clock_t model_cpu=0;double last_aperture=0;
    channel.set_physical_rf_observer([&](const auto& next,std::uint32_t observed_rate){require(observed_rate==rate,"rate changed");segment=next;m=mapping(next.phase_segment-1,rate,next.rf_shift_hz,next.sideband_sign);m.helper_audio_hz=plan.frequency_hz;m.data_audio_hz=cfg.carrier_hz;++hops;
        tracker.begin_pilot_segment(next.first_sample,plan.frequency_hz,(next.rf_shift_hz*2+plan.frequency_hz*100)*3e-6,false,1,next.settled_first_sample,true);
        const auto start=std::clock();const auto p=model.predict(m,next.settled_first_sample);model_cpu+=std::clock()-start;
        if(p.valid){const auto truth=plan.frequency_hz*100e-6+7.3*next.rf_shift_hz/7e6;predicted.add(p.helper_residual_hz-truth,p.helper_three_sigma_hz());}
    });
    channel.set_physical_observer([&](std::span<const float> raw,std::uint32_t,std::uint64_t first_sample){tracker.push(raw,first_sample);const auto fit=tracker.snapshot();if(!fit.qualified||fit.frequency_context_end_sample==last_context)return;last_context=fit.frequency_context_end_sample;
        if(accepted_segment==segment.phase_segment)return;const auto date=fit.first_sample+(fit.sample-fit.first_sample)/2;const auto q=fit.frequency_at(date);if(!q.valid)return;
        const auto truth=plan.frequency_hz*(1+100e-6)+7.3*segment.rf_shift_hz/7e6;local.add(q.frequency_hz-truth,q.uncertainty_hz);
        IndependentFrequencyAperture a{m,fit.first_sample,fit.sample+1,date,0,q.frequency_hz,std::pow(q.uncertainty_hz/3,2),fit.coherent_aperture_seconds};
        const auto start=std::clock();const auto reason=model.observe(a,fit.captured_end_sample);const auto p=model.predict(m,fit.sample);model_cpu+=std::clock()-start;
        require(reason==OscillatorModelReason::accepted,"actual fit rejected by model");require(p.valid,"current model prediction unavailable");updated.add(p.helper_residual_hz-(truth-plan.frequency_hz),p.helper_three_sigma_hz());
        accepted_segment=segment.phase_segment;++fits;last_aperture=p.aperture_cube_seconds3;if(!first)first=static_cast<double>(fit.captured_end_sample)/rate;
    });
    std::array<float,257> discarded{};std::uint64_t logical=0;const auto start=std::clock();while(logical<32ULL*cfg.sample_rate){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(discarded.size(),32ULL*cfg.sample_rate-logical));const auto n=channel.read(*source,std::span(discarded).first(count));require(n,"source ended");logical+=n;}
    const auto cpu=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
    std::cout<<"oscillator_actual_channel,scatter_estimator=signed_average,sampled_pilot_cn0_target_db_hz="<<cn0<<",seed="<<seed<<",truth_frequency_seed=0,RF_changes=1,independent_hop_phases=1,raw_fit_aperture=200ms_fallback,phase_noise_degrees_per_sqrt_s=.5,tracker_phase_noise_matches=1,hops="<<hops<<",fits="<<fits<<",first_fit_s="<<first<<",local_escapes="<<local.escapes<<",local_rmse_hz="<<std::sqrt(local.error2/std::max<std::size_t>(1,local.n))<<",last_local_U_hz="<<local.u<<",next_hop_predictions="<<predicted.n<<",prediction_escapes="<<predicted.escapes<<",prediction_rmse_hz="<<std::sqrt(predicted.error2/std::max<std::size_t>(1,predicted.n))<<",last_prediction_U_hz="<<predicted.u<<",updated_escapes="<<updated.escapes<<",updated_rmse_hz="<<std::sqrt(updated.error2/std::max<std::size_t>(1,updated.n))<<",last_updated_U_hz="<<updated.u<<",aperture_cube_sum_s3="<<last_aperture<<",model_CPU_s="<<static_cast<double>(model_cpu)/CLOCKS_PER_SEC<<",all_CPU_s="<<cpu<<",model_bytes="<<model.workspace_bytes()<<",tracker_bytes="<<tracker.workspace_bytes()<<'\n';
}
void actual_rf_drift(double cn0,int side,double slew,unsigned seed) {
    modem::Config cfg;cfg.sample_rate=8000;cfg.carrier_hz=1500;cfg.bandwidth_hz=200;cfg.spreading_factor=128;cfg.pulse_shaping=false;
    cfg.oscillator_search=modem::OscillatorSearchConfig{{100,0},{2,0},7e6,3};
    transfer::Options options;options.modem=cfg;options.timestamp=1800000000;options.key=Crypto(std::array<std::uint8_t,32>{});
    Bytes bits(64);for(std::size_t i=0;i<bits.size();++i)bits[i]=(i*7+i/3)%2;auto source=transfer::binary_transmitter(bits,options);
    helper::Options helper;helper.power_w=40;helper.data_power_w=4;helper.callsign="N0CALL";helper.timecode=true;
    const auto plan=helper::plan(helper,helper::placement_band(cfg),120000000,0,modem::outer_dsss_transmit_gain(cfg));
    modem::ChannelConfig impair;impair.clock_error_ppm=100;impair.frequency_offset_hz=side*7.3;impair.seed=seed;
    impair.phase_noise_degrees_per_sqrt_second=.5;
    impair.snr_db=cn0-10*std::log10(cfg.sample_rate/2.)-10*std::log10(helper.power_w/helper.data_power_w)-20*std::log10(modem::outer_dsss_transmit_gain(cfg))-10*std::log10(pilot_power_fraction);
    modem::SampledSimulationChannel channel(cfg,impair,helper);channel.set_helper_time_key(options.key);
    const auto rate=channel.physical_sample_rate();ToneTrackerConfig tc{rate,plan.frequency_hz,320,.5*std::numbers::pi/180,.5,true,false};
    ToneTracker tracker(tc,ToneTracker::workspace_required(tc));
    auto evolving_config=config(),constant_config=config();constant_config.rf_drift_prior_sigma_ppm_per_second=0;constant_config.audio_drift_prior_sigma_ppm_per_second=0;
    std::array<OscillatorFrequencyModel,2> models;require(models[0].reset(constant_config)&&models[1].reset(evolving_config),"sampled drift model reset");
    // Covariance-only, prior-conditioned, noise-only Fisher reference using
    // the SAME causally acquired dates/RF rows. It neither seeds acquisition
    // nor supplies frequency output, runtime correction or qualification.
    auto reference_config=evolving_config;reference_config.learn_repeat_visit_scatter=false;
    OscillatorFrequencyModel noise_reference;require(noise_reference.reset(reference_config),"noise reference reset");
    double last_reference_prediction_U=0,last_reference_data_U=0,last_reference_current_U=0;
    modem::SampledSimulationChannel::RfRetuneSimulation retunes;retunes.rf_shift_hz={7e6,21e6,35e6,49e6};retunes.phase_seed=49273+seed;
    retunes.sideband_sign=side;retunes.rf_fractional_slew_per_second=slew;channel.set_rf_retune_simulation(retunes);
    modem::SampledSimulationChannel::PhysicalRfSegment segment;LinkMapping m;
    std::array<Metric,2> prediction,updated,data_prediction,warmup;Metric local_mean,local_endpoint;
    std::array<OscillatorFrequencyPrediction,2> latest{};std::array<double,2> cpu{};
    std::vector<double> measured_cn0;std::uint64_t accepted_segment=0,last_context=0,raw_end=0;
    std::size_t hops=0,fits=0;double first_fit=0,fit_cube=0,last_mean_fraction=0;
    const auto truth_residual=[&](double audio,double rf,double t){return audio*100e-6+side*7.3*rf/7e6+side*rf*slew*t;};
    const auto add=[&](Metric& metric,double error,double bound,const char* kind,std::size_t model,double date) {
        metric.add(error,bound);
        if(std::abs(error)>bound)std::cout<<"sampled_rf_drift_escape,kind="<<kind<<",model="<<(std::string_view(kind).starts_with("local_")?"local_aperture":model?"unknown_constant_drift":"constant_frequency")
            <<",cn0_target_db_hz="<<cn0<<",sideband="<<side<<",RF_slew_fraction_per_s="<<slew<<",seed="<<seed
            <<",phase_segment="<<segment.phase_segment<<",date_s="<<date<<",rf_hz="<<segment.rf_shift_hz
            <<",error_hz="<<error<<",U_hz="<<bound<<",warmup="<<(date<8)<<'\n';
    };
    channel.set_physical_rf_observer([&](const auto& next,std::uint32_t observed_rate) {
        require(observed_rate==rate&&next.first_sample==raw_end,"drifting RF descriptor lost physical ADC frontier");segment=next;++hops;
        m=mapping(next.phase_segment-1,rate,next.rf_shift_hz,next.sideband_sign);m.helper_audio_hz=plan.frequency_hz;m.data_audio_hz=cfg.carrier_hz;
        require(m.settled_first_sample==next.settled_first_sample&&m.settled_end_sample==next.end_sample,"drifting RF mapping changed settling/support");
        tracker.begin_pilot_segment(next.first_sample,plan.frequency_hz,(next.rf_shift_hz*2+plan.frequency_hz*100)*3e-6,false,1,next.settled_first_sample,true);
        require(!tracker.snapshot().qualified,"drifting RF hop retained old phase/frequency qualification");
        const auto t=static_cast<double>(next.settled_first_sample)/rate;
        const auto reference=noise_reference.predict(m,next.settled_first_sample);
        if(reference.valid){last_reference_prediction_U=reference.helper_three_sigma_hz();last_reference_data_U=reference.data_three_sigma_hz();}
        for(std::size_t j=0;j<models.size();++j) {
            const auto start=std::clock();const auto p=models[j].predict(m,next.settled_first_sample);cpu[j]+=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
            if(!p.valid)continue;
            require(p.evidence_end_sample<=next.first_sample,"next-hop prediction used current or future aperture");
            const auto error=p.helper_residual_hz-truth_residual(plan.frequency_hz,next.rf_shift_hz,t);
            add(prediction[j],error,p.helper_three_sigma_hz(),"next_hop_helper",j,t);
            if(t<8)warmup[j].add(error,p.helper_three_sigma_hz());
            add(data_prediction[j],p.data_residual_hz-truth_residual(cfg.carrier_hz,next.rf_shift_hz,t),p.data_three_sigma_hz(),"next_hop_data",j,t);
        }
    });
    channel.set_physical_observer([&](std::span<const float> raw,std::uint32_t observed_rate,std::uint64_t first_sample) {
        require(observed_rate==rate&&first_sample==raw_end,"sampled RF drift dropped or redated PCM");raw_end+=raw.size();tracker.push(raw,first_sample);
        const auto fit=tracker.snapshot();if(!fit.qualified||fit.frequency_context_end_sample==last_context)return;last_context=fit.frequency_context_end_sample;
        if(accepted_segment==segment.phase_segment)return;
        require(fit.frequency_local_aperture&&fit.first_sample>=segment.settled_first_sample&&fit.sample<segment.end_sample&&
            fit.frequency_context_first_sample==fit.first_sample&&fit.frequency_context_end_sample==fit.sample+1,
            "sampled RF drift input was pooled or crossed retune/settling");
        // Ten symmetric20ms projections: this slope estimates frequency at
        // their actual mean date, not the publication endpoint. Preserve the
        // half-sample when the support has an even number of ADC samples.
        const auto delta=fit.sample-fit.first_sample;
        const auto date=fit.first_sample+delta/2;const auto fraction=.5*(delta%2);
        const auto q=fit.frequency_at(date,fraction);require(q.valid,"local RF drift aperture lost its mean date");
        const auto t=(static_cast<double>(date)+fraction)/rate;
        add(local_mean,q.frequency_hz-plan.frequency_hz-truth_residual(plan.frequency_hz,segment.rf_shift_hz,t),q.uncertainty_hz,"local_mean",0,t);
        for(const auto endpoint:std::array{fit.first_sample,fit.sample}) {const auto x=fit.frequency_at(endpoint);require(x.valid,"local RF drift endpoint unavailable");const auto et=static_cast<double>(endpoint)/rate;add(local_endpoint,x.frequency_hz-plan.frequency_hz-truth_residual(plan.frequency_hz,segment.rf_shift_hz,et),x.uncertainty_hz,"local_endpoint",0,et);}
        IndependentFrequencyAperture a{m,fit.first_sample,fit.sample+1,date,fraction,q.frequency_hz,std::pow(q.uncertainty_hz/3,2),fit.coherent_aperture_seconds};
        for(std::size_t j=0;j<models.size();++j) {
            const auto start=std::clock();const auto reason=models[j].observe(a,fit.captured_end_sample);latest[j]=models[j].predict(m,fit.sample);cpu[j]+=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
            require(reason==OscillatorModelReason::accepted&&latest[j].valid,"sampled RF drift aperture/model provenance failed");
            const auto et=static_cast<double>(fit.sample)/rate;
            add(updated[j],latest[j].helper_residual_hz-truth_residual(plan.frequency_hz,segment.rf_shift_hz,et),latest[j].helper_three_sigma_hz(),"updated_helper",j,et);
        }
        auto reference_aperture=a;
        reference_aperture.frequency_hz=plan.frequency_hz; // arbitrary zero residual: covariance is independent of frequency value
        reference_aperture.variance_hz2=3/(2*std::numbers::pi*std::numbers::pi*std::pow(10.,cn0/10)*std::pow(fit.coherent_aperture_seconds,3));
        require(noise_reference.observe(reference_aperture,fit.captured_end_sample)==OscillatorModelReason::accepted,"noise covariance reference provenance");
        const auto reference=noise_reference.predict(m,fit.sample);require(reference.valid,"current covariance reference unavailable");
        last_reference_current_U=reference.helper_three_sigma_hz();
        accepted_segment=segment.phase_segment;++fits;fit_cube+=std::pow(fit.coherent_aperture_seconds,3);last_mean_fraction=fraction;
        measured_cn0.push_back(fit.carrier_to_noise_db_hz);if(!first_fit)first_fit=static_cast<double>(fit.captured_end_sample)/rate;
    });
    std::array<float,257> discarded{};std::uint64_t logical=0;const auto start=std::clock();
    while(logical<32ULL*cfg.sample_rate){const auto n=channel.read(*source,std::span(discarded).first(static_cast<std::size_t>(std::min<std::uint64_t>(discarded.size(),32ULL*cfg.sample_rate-logical))));require(n,"sampled RF drift source ended");logical+=n;}
    const auto all_cpu=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
    std::sort(measured_cn0.begin(),measured_cn0.end());const auto measured=measured_cn0.empty()?0:measured_cn0[measured_cn0.size()/2];
    const auto noise_scale=3*std::sqrt(3/(2*std::numbers::pi*std::numbers::pi*std::pow(10.,cn0/10)));
    std::cout<<"sampled_rf_drift_reference,scope=prior_conditioned_noise_only_geometry_and_constant_drift,no_frequency_output=1,no_operational_authority=1"
        <<",sampled_pilot_cn0_target_db_hz="<<cn0<<",sideband="<<side<<",RF_slew_fraction_per_s="<<slew<<",seed="<<seed
        <<",same_acquired_apertures="<<fits<<",last_next_hop_helper_S3_hz="<<last_reference_prediction_U<<",last_next_hop_data_S3_hz="<<last_reference_data_U
        <<",last_current_helper_S3_hz="<<last_reference_current_U<<",actual_200ms_scalar_segmented_S3_hz="<<noise_scale/std::sqrt(fit_cube)
        <<",ideal_80_full400ms_scalar_segmented_S3_hz="<<noise_scale/std::sqrt(80*.4*.4*.4)
        <<",ideal_uninterrupted_32s_S3_hz="<<noise_scale/std::pow(32.,1.5)<<",ramps_settling_phase_noise_self_interference_and_estimator_inefficiency_omitted=1"<<'\n';
    for(std::size_t j=0;j<models.size();++j) {
        const auto& p=latest[j];const auto rms=[](const Metric& x){return std::sqrt(x.error2/std::max<std::size_t>(1,x.n));};
        std::cout<<"sampled_rf_drift_model,model="<<(j?"unknown_constant_drift":"constant_frequency")<<",Q=0,sampled_pilot_cn0_target_db_hz="<<cn0
            <<",qualified_fit_cn0_median_db_hz="<<measured<<",sideband="<<side<<",RF_slew_fraction_per_s="<<slew<<",seed="<<seed
            <<",truth_frequency_seed=0,RF_changes=1,independent_hop_phases=1,phase_noise_degrees_per_sqrt_s=.5,tracker_phase_noise_matches=1,aperture=200ms_fallback"
            <<",hops="<<hops<<",fits="<<fits<<",first_fit_s="<<first_fit<<",last_mean_date_fraction="<<last_mean_fraction
            <<",local_mean_escapes="<<local_mean.escapes<<",local_mean_rmse_hz="<<rms(local_mean)<<",last_local_U_hz="<<local_mean.u<<",local_endpoint_escapes="<<local_endpoint.escapes
            <<",next_hop_predictions="<<prediction[j].n<<",next_hop_escapes="<<prediction[j].escapes<<",next_hop_rmse_hz="<<rms(prediction[j])<<",last_prediction_error_hz="<<prediction[j].last_error<<",last_prediction_U_hz="<<prediction[j].u
            <<",next_hop_data_escapes="<<data_prediction[j].escapes<<",last_data_U_hz="<<data_prediction[j].u<<",warmup_predictions="<<warmup[j].n<<",warmup_escapes="<<warmup[j].escapes
            <<",updated_escapes="<<updated[j].escapes<<",updated_rmse_hz="<<rms(updated[j])<<",last_updated_error_hz="<<updated[j].last_error<<",last_updated_U_hz="<<updated[j].u
            <<",estimated_RF_drift_ppm_per_s="<<p.rf_drift_fraction_per_second*1e6<<",RF_drift_sigma_ppm_per_s="<<std::sqrt(p.fractional_covariance[2][2])*1e6
            <<",estimated_audio_drift_ppm_per_s="<<p.audio_drift_fraction_per_second*1e6<<",audio_drift_sigma_ppm_per_s="<<std::sqrt(p.fractional_covariance[3][3])*1e6
            <<",RF_fraction_sigma_ppm="<<p.rf_fraction_sigma*1e6<<",audio_fraction_sigma_ppm="<<p.audio_fraction_sigma*1e6
            <<",support_first="<<p.evidence_first_sample<<",support_end="<<p.evidence_end_sample<<",state_sample="<<p.sample<<",observation_age_s="<<p.observation_age_seconds
            <<",aperture_cube_sum_s3="<<fit_cube<<",model_CPU_s="<<cpu[j]<<",all_CPU_s="<<all_cpu<<",model_bytes="<<models[j].workspace_bytes()<<",tracker_bytes="<<tracker.workspace_bytes()<<'\n';
    }
}

}
int main(int argc,char** argv)try{std::cout<<std::setprecision(10);
if(argc>1&&std::string_view(argv[1])=="--actual-rf-drift") {for(const auto side:std::array{1,-1})for(const auto slew:std::array{3e-9,-3e-9})actual_rf_drift(40,side,slew,5181);actual_rf_drift(30,1,3e-9,5181);actual_rf_drift(60,1,3e-9,5181);return 0;}
boundaries();process_reference();decreasing_then_increasing_scatter();decreasing_then_increasing_scatter(true);if(argc>1&&std::string_view(argv[1])=="--process-reference")return 0;gaussian_model_only(false);gaussian_model_only(true);gaussian_model_only(true,false,.04);gaussian_model_only(true,true,.04);if(argc>1)return 0;for(const auto cn0:std::array{30.,40.})for(unsigned seed=1;seed<=3;++seed)actual_channel(cn0,seed);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
