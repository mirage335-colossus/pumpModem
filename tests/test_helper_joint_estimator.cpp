#include "datapump/helper_joint_estimator.hpp"
#include "helper_measurement_hops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>

namespace {
namespace h=datapump::helper;
constexpr double tau=2*std::numbers::pi;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
double wrap(double x){return std::remainder(x,tau);}
struct Source {
    std::mt19937_64 random;
    std::normal_distribution<double> normal;
    std::uniform_real_distribution<double> phase{-std::numbers::pi,std::numbers::pi};
    helper_measurement_hops::Profile hops;
    double audio=50,drift=.04,shift=.8,shift_drift=.0002;
    bool noise=true;double curvature=0,step=0,differential=0,step_time=25;bool scaled_step=false;
    explicit Source(unsigned seed):random(seed),hops(helper_measurement_hops::hf50(seed)){}
    double tx(double t)const{return t+1e-6*(audio*t+.5*drift*t*t);}
    h::HelperJointEstimator::Input point(unsigned i){
        h::HelperJointEstimator::Input x;x.valid=true;x.time_seconds=i*.4+.02+(8640-1)/(2.*24000);
        x.frequency_time_seconds=x.time_seconds+.5/24000;x.shift_hz=hops.frequency(i);
        const double a=audio+drift*x.frequency_time_seconds,r=shift+shift_drift*x.frequency_time_seconds;
        const double change=std::max(0.,x.frequency_time_seconds-18);
        const double common=curvature*change*change+(x.frequency_time_seconds>step_time?step*(scaled_step?x.shift_hz/30e6:1):0);
        constexpr double sh=.07,sl=.10,sp=.06;
        const double z0=noise?normal(random):0,z1=noise?normal(random):0,z2=noise?normal(random):0;
        const double common_noise=noise?.003*normal(random):0;
        const double p1=(-.1-.1*.2)/std::sqrt(.99),p2=std::sqrt(1-.2*.2-p1*p1);
        const double eh=sh*z0,el=sl*(.1*z0+std::sqrt(.99)*z1),ep=sp*(.2*z0+p1*z1+p2*z2);
        x.high_frequency_hz=2400*(1+a*1e-6)+x.shift_hz*r*1e-6+common+eh+common_noise;
        x.low_frequency_hz=550*(1+a*1e-6)+x.shift_hz*r*1e-6+common+el+common_noise;
        const double common_phase=phase(random),offset=differential*std::sin(1.618*(i+1));
        x.high_center_phase_radians=wrap(tau*2400*tx(x.time_seconds)+common_phase+.31+ep);
        x.low_center_phase_radians=wrap(tau*550*tx(x.time_seconds)+common_phase+offset);
        x.thermal_covariance={{{sh*sh,.1*sh*sl,.2*sh*sp},{.1*sh*sl,sl*sl,-.1*sl*sp},{.2*sh*sp,-.1*sl*sp,sp*sp}}};
        x.common_frequency_sigma_hz=.003;
        x.sample_support_valid=true;x.first_sample_time_seconds=i*.4+.02;x.end_sample_time_seconds=i*.4+.38;return x;
    }
};
void matched(){
    unsigned valid=0,covered=0;double max_error=0,max_width=0;
    for(unsigned seed=901;seed<933;++seed){Source source(seed);h::HelperJointEstimator joint({});
        for(unsigned i=0;i<90;++i)(void)joint.observe(source.point(i));
        const auto estimate=joint.predict(1600,30e6,36);
        require(estimate.reference_initialized&&estimate.reference_preserved,"matched reference lost");
        require(estimate.valid&&estimate.recent_guard_available&&estimate.alternatives==1,"matched joint fit unavailable");
        const double truth=1600*(1+(source.audio+source.drift*36)*1e-6)+30e6*(source.shift+source.shift_drift*36)*1e-6;
        const double error=estimate.frequency_hz-truth;++valid;covered+=std::abs(error)<=estimate.conditional_half_width_hz;
        max_error=std::max(max_error,std::abs(error));max_width=std::max(max_width,estimate.conditional_half_width_hz);
        require(estimate.thermal_sigma_hz>0&&estimate.process_sigma_hz>0&&
            std::abs(estimate.stochastic_sigma_hz*estimate.stochastic_sigma_hz-estimate.thermal_sigma_hz*estimate.thermal_sigma_hz-estimate.process_sigma_hz*estimate.process_sigma_hz)<1e-10,
            "joint stochastic covariance not decomposed");
        const double gauge=estimate.reference_input_seconds-source.tx(estimate.reference_input_seconds);
        const double clock_error=estimate.source_seconds-source.tx(36)-gauge;
        require(std::abs(clock_error)<6*estimate.source_stochastic_sigma_seconds+estimate.source_model_half_width_seconds,"fixed source gauge not preserved");
        const auto phase_prediction=joint.predict(2400,30e6,36.2);
        require(phase_prediction.valid&&std::isfinite(phase_prediction.phase_difference_radians),"previous-only phase prediction unavailable");
    }
    require(covered>=31,"synthetic correlated joint frequency intervals undercovered");
    std::cout<<"helper_joint_matched seeds="<<valid<<" covered="<<covered<<" max_frequency_error_hz="<<max_error<<" max_conditional_width_hz="<<max_width<<'\n';
}
void controls(){
    Source source(1001);source.noise=false;h::HelperJointEstimator joint({});
    h::HelperJointEstimator::Estimate estimate;
    for(unsigned i=0;i<8;++i)estimate=joint.observe(source.point(i));
    require(!estimate.valid&&!estimate.recent_guard_available,"sparse fit falsely advertised recent guard");
    for(unsigned i=8;i<30;++i)estimate=joint.observe(source.point(i));
    require(estimate.valid,"noiseless joint control failed");
    const double reference=estimate.reference_input_seconds;
    auto malformed=source.point(30);malformed.thermal_covariance[0][1]=10;
    require(!joint.observe(malformed).valid,"indefinite covariance accepted");
    auto hidden=source.point(30);hidden.time_seconds+=.01;hidden.frequency_time_seconds+=.01;
    hidden.thermal_covariance={{{-.001,0,0},{0,1,0},{0,0,1}}};hidden.common_frequency_sigma_hz=2;
    const auto accepted_before=estimate.accepted_observations;
    const auto hidden_result=joint.observe(hidden);
    require(!hidden_result.valid&&hidden_result.accepted_observations==accepted_before,
        "common process hid invalid thermal covariance");
    malformed=source.point(31);malformed.valid=false;
    require(!joint.observe(malformed).valid,"missing dwell fabricated observation");
    estimate=joint.observe(source.point(32));
    require(estimate.valid&&estimate.reference_input_seconds==reference,"missing dwell changed reference");
    require(!joint.predict(1600,30e6,60).valid,"unbounded clock extrapolation qualified");
    const auto old=joint.predict(1600,30e6,estimate.time_seconds);
    require(!joint.observe(source.point(20)).valid,"backward observation accepted");
    require(joint.predict(1600,30e6,estimate.time_seconds).frequency_hz==old.frequency_hz,"backward observation mutated fit");
    auto contrary=source.point(33);contrary.valid=false;contrary.model_contradicted=true;
    const auto contrary_result=joint.observe(contrary);
    require(!contrary_result.valid&&contrary_result.reference_input_seconds==reference&&
        !joint.predict(1600,30e6,contrary.time_seconds).valid,
        "external model contradiction retained stale prediction");
    auto missing_after=source.point(34);missing_after.valid=false;
    require(!joint.observe(missing_after).valid&&!joint.predict(1600,30e6,missing_after.time_seconds).valid,
        "missing pilot cleared model contradiction");
    const auto restored=joint.observe(source.point(35));
    require(restored.valid&&restored.reference_input_seconds==reference,
        "consistent recovery silently changed reference or stayed unavailable");
    auto duplicate_contrary=source.point(35);duplicate_contrary.valid=false;duplicate_contrary.model_contradicted=true;
    require(!joint.observe(duplicate_contrary).valid&&!joint.predict(1600,30e6,restored.time_seconds).valid,
        "duplicate contradiction retained old valid prediction");
    const auto restored_again=joint.observe(source.point(36));
    require(restored_again.valid&&restored_again.reference_input_seconds==reference,"duplicate notice changed original reference");
    auto nan_contrary=source.point(37);nan_contrary.time_seconds=std::numeric_limits<double>::quiet_NaN();nan_contrary.valid=false;nan_contrary.model_contradicted=true;
    require(!joint.observe(nan_contrary).valid&&!joint.predict(1600,30e6,restored_again.time_seconds).valid,
        "malformed contradiction retained old valid prediction");
    require(joint.observe(source.point(37)).valid,"malformed notification poisoned chronology");
    h::HelperJointEstimator extreme({});auto extreme_phase=source.point(0);
    extreme_phase.high_center_phase_radians=1e308;extreme_phase.low_center_phase_radians=-1e308;
    const auto modular=extreme.observe(extreme_phase);
    require(modular.reference_initialized&&std::isfinite(modular.reference_input_seconds)&&modular.accepted_observations==1,
        "finite modular phase difference overflowed");
    h::HelperJointEstimator::Config bounded;bounded.maximum_observations=12;
    h::HelperJointEstimator work(bounded);Source work_source(1002);work_source.noise=false;
    for(unsigned i=0;i<13;++i)estimate=work.observe(work_source.point(i));
    require(estimate.work_limited&&!estimate.valid,"joint work bound unreported");
    for(const bool use_step:{false,true}){Source mismatch(1003);mismatch.noise=false;if(use_step)mismatch.step=1.2;else mismatch.curvature=.003;
        h::HelperJointEstimator bad({});for(unsigned i=0;i<90;++i)estimate=bad.observe(mismatch.point(i));
        require(!estimate.valid&&estimate.reference_initialized&&estimate.reference_input_seconds==.02+(8640-1)/(2.*24000),
            "model contradiction falsely narrow or reanchored");}
    h::HelperJointEstimator::Config channel;channel.differential_phase_half_width_radians=.02;
    h::HelperJointEstimator differential(channel);Source d(1004);d.noise=false;d.differential=.02;
    for(unsigned i=0;i<90;++i)estimate=differential.observe(d.point(i));
    require(estimate.valid,"bounded differential phase withdrew matched control");
    std::cout<<"helper_joint_controls covariance=reject missing=preserved stale=reject model_mismatch=withdraw bounds=reported\n";
}
void frequency_step(){
    for(const bool scaled:{false,true})for(unsigned seed=1201;seed<1209;++seed){
        Source source(seed);source.step=1.2;source.step_time=36.8;source.scaled_step=scaled;
        h::HelperJointEstimator::Config config;config.allow_frequency_step=true;h::HelperJointEstimator joint(config);
        std::array<h::HelperJointEstimator::Input,100> input;
        for(unsigned i=0;i<100;++i)input[i]=source.point(i);
        for(unsigned i=0;i<=95;++i)(void)joint.observe(input[i]);
        const auto before=joint.predict(1600,30e6,36);
        const double reference=before.reference_input_seconds;
        require(!joint.predict(1600,30e6,input[95].time_seconds).valid,"step control did not contradict old frequency model");
        const auto model=scaled?h::HelperJointEstimator::FrequencyStepModel::rf_scaled:h::HelperJointEstimator::FrequencyStepModel::common_hz;
        const std::span<const h::HelperJointEstimator::Input> replay(input.data()+92,4);
        auto overlapping=std::array<h::HelperJointEstimator::Input,4>{input[92],input[93],input[94],input[95]};
        overlapping[2].first_sample_time_seconds=overlapping[1].end_sample_time_seconds-.01;
        const auto old_count=before.accepted_observations;
        require(!joint.begin_frequency_step(model,input[91].first_sample_time_seconds,input[92].end_sample_time_seconds,overlapping),
            "distinct-center overlapping replay counted the same samples twice");
        const auto rollback=joint.predict(1600,30e6,input[95].time_seconds);
        require(!rollback.frequency_step_active&&rollback.reference_input_seconds==reference&&rollback.accepted_observations==old_count,
            "invalid replay changed retained frequency history or reference");
        require(joint.begin_frequency_step(model,input[91].first_sample_time_seconds,input[92].end_sample_time_seconds,replay),"confirmed bounded frequency step did not recover");
        auto estimate=joint.predict(1600,30e6,input[95].time_seconds);
        require(estimate.valid&&estimate.frequency_step_active&&estimate.frequency_step_confirmed&&estimate.post_step_observations==3&&
            estimate.reference_preserved&&estimate.reference_input_seconds==reference&&estimate.step_excluded_observations==2,
            "frequency step reset the source reference or lacked fresh support");
        require(!joint.predict(1600,30e6,36.8).valid,"prediction inside ambiguous transition bracket qualified");
        require(!joint.begin_frequency_step(model,38.4,38.6,replay),"second step silently replaced first epoch");
        for(unsigned i=96;i<100;++i){estimate=joint.observe(input[i]);require(estimate.valid,"post-step fresh observation lost recovery");}
        estimate=joint.predict(1600,30e6,40);
        const double truth=1600*(1+(source.audio+source.drift*40)*1e-6)+30e6*(source.shift+source.shift_drift*40)*1e-6+1.2;
        require(estimate.valid&&std::abs(estimate.frequency_hz-truth)<=estimate.conditional_half_width_hz,"post-step frequency interval missed synthetic truth");
        const double gauge=reference-source.tx(reference);
        require(std::abs(estimate.source_seconds-source.tx(40)-gauge)<6*estimate.source_stochastic_sigma_seconds+estimate.source_model_half_width_seconds,
            "frequency step changed original relative clock gauge");
    }
    // The actual change can occur inside the first contrary aperture. Its
    // frequency and phase summaries are deliberately corrupted here; the
    // conservative outer bracket must exclude that entire raw observation.
    Source crossing(1291);crossing.noise=false;crossing.step=1.2;crossing.step_time=36.9;
    h::HelperJointEstimator::Config crossing_config;crossing_config.allow_frequency_step=true;
    h::HelperJointEstimator crossing_joint(crossing_config);
    std::array<h::HelperJointEstimator::Input,96> crossing_input;
    for(unsigned i=0;i<96;++i)crossing_input[i]=crossing.point(i);
    crossing_input[92].high_frequency_hz+=50;crossing_input[92].low_frequency_hz-=20;
    crossing_input[92].high_center_phase_radians=wrap(crossing_input[92].high_center_phase_radians+1.1);
    for(unsigned i=0;i<96;++i)(void)crossing_joint.observe(crossing_input[i]);
    const auto crossing_reference=crossing_joint.predict(1600,30e6,38.2).reference_input_seconds;
    auto flagged=std::array<h::HelperJointEstimator::Input,4>{crossing_input[92],crossing_input[93],crossing_input[94],crossing_input[95]};
    flagged[1].model_contradicted=true;
    require(!crossing_joint.begin_frequency_step(h::HelperJointEstimator::FrequencyStepModel::common_hz,36.42,37.18,flagged),
        "replay silently cleared an independent contradiction");
    require(!crossing_joint.begin_frequency_step(h::HelperJointEstimator::FrequencyStepModel::common_hz,36.42,37.18,
        std::span<const h::HelperJointEstimator::Input>(crossing_input.data()+92,3)),
        "two definite post-transition observations falsely met confirmation");
    require(crossing_joint.begin_frequency_step(h::HelperJointEstimator::FrequencyStepModel::common_hz,36.42,37.18,
        std::span<const h::HelperJointEstimator::Input>(crossing_input.data()+92,4)),
        "inside-aperture transition contamination was not excluded");
    const auto crossed=crossing_joint.predict(1600,30e6,38.2);
    require(crossed.valid&&crossed.step_excluded_observations==2&&crossed.reference_input_seconds==crossing_reference&&
        std::abs(crossed.frequency_step_hz-1.2)<1e-7,"excluded transition poisoned the new frequency epoch");

    Source bending(1292);bending.noise=false;bending.curvature=.003;
    h::HelperJointEstimator curved(crossing_config);std::array<h::HelperJointEstimator::Input,96> curved_input;
    for(unsigned i=0;i<96;++i){curved_input[i]=bending.point(i);(void)curved.observe(curved_input[i]);}
    require(!curved.begin_frequency_step(h::HelperJointEstimator::FrequencyStepModel::common_hz,36.42,37.18,
        std::span<const h::HelperJointEstimator::Input>(curved_input.data()+92,4)),
        "smooth nonlinear frequency evolution was reclassified as one supported step");
    // A request without a change is not confirmation; a failed request leaves
    // the previously valid original model and reference unchanged.
    Source clean(1301);clean.noise=false;h::HelperJointEstimator::Config config;config.allow_frequency_step=true;h::HelperJointEstimator joint(config);
    std::array<h::HelperJointEstimator::Input,96> input;for(unsigned i=0;i<96;++i){input[i]=clean.point(i);(void)joint.observe(input[i]);}
    const auto old=joint.predict(1600,30e6,input.back().time_seconds);
    require(old.valid&&!joint.begin_frequency_step(h::HelperJointEstimator::FrequencyStepModel::common_hz,36.42,37.18,
        std::span<const h::HelperJointEstimator::Input>(input.data()+92,4)),"unchanged oscillator fabricated a frequency step");
    const auto unchanged=joint.predict(1600,30e6,input.back().time_seconds);
    require(unchanged.valid&&unchanged.frequency_hz==old.frequency_hz&&unchanged.reference_input_seconds==old.reference_input_seconds,"failed step request mutated old fit");
    auto transient=std::array<h::HelperJointEstimator::Input,4>{input[92],input[93],input[94],input[95]};
    transient[2].high_frequency_hz+=.4;transient[2].low_frequency_hz+=.4;
    h::HelperJointEstimator transient_joint(config);
    for(unsigned i=0;i<92;++i)(void)transient_joint.observe(input[i]);
    const auto transient_before=transient_joint.predict(1600,30e6,input[91].time_seconds);
    require(!transient_joint.begin_frequency_step(h::HelperJointEstimator::FrequencyStepModel::common_hz,36.42,37.18,transient),
        "one isolated frequency transient qualified a sustained step");
    require(transient_joint.predict(1600,30e6,input[91].time_seconds).frequency_hz==transient_before.frequency_hz,
        "failed transient hypothesis changed original fit");

    // If every recent point is post-step, the RF-scaled nuisance is
    // indistinguishable there from the original RF offset. The recent guard
    // must become explicitly unavailable rather than drop a covariance term.
    Source early(1302);early.noise=false;early.step=1.2;early.step_time=8;early.scaled_step=true;
    h::HelperJointEstimator early_joint(config);std::array<h::HelperJointEstimator::Input,100> early_input;
    for(unsigned i=0;i<100;++i)early_input[i]=early.point(i);
    for(unsigned i=0;i<=23;++i)(void)early_joint.observe(early_input[i]);
    require(early_joint.begin_frequency_step(h::HelperJointEstimator::FrequencyStepModel::rf_scaled,7.62,8.38,
        std::span<const h::HelperJointEstimator::Input>(early_input.data()+20,4)),"early scaled step did not establish its initial guarded fit");
    h::HelperJointEstimator::Estimate aged;
    for(unsigned i=24;i<100;++i)aged=early_joint.observe(early_input[i]);
    require(!aged.valid&&!aged.recent_guard_available&&aged.reference_input_seconds==early_input[0].time_seconds,
        "post-only RF-scaled recent fit hid a confounded step parameter");
    std::cout<<"helper_joint_frequency_step models=2 seeds=16 reference=preserved transition=excluded transient=reject curvature=reject no_change=reject second_step=reject\n";
}
void process_and_dates(){
    // Explicit rank-one process must reproduce the original scalar contract,
    // while a supplied zero block must replace rather than double the scalar.
    Source source(1101);h::HelperJointEstimator scalar({}),matrix({});
    for(unsigned i=0;i<90;++i){
        auto x=source.point(i);(void)scalar.observe(x);
        x.process_covariance_valid=true;
        const double q=x.common_frequency_sigma_hz*x.common_frequency_sigma_hz;
        x.process_covariance={{{q,q,0},{q,q,0},{0,0,0}}};
        x.common_frequency_sigma_hz=100;
        (void)matrix.observe(x);
    }
    const auto a=scalar.predict(1600,30e6,36),b=matrix.predict(1600,30e6,36);
    require(a.valid&&b.valid&&std::abs(a.frequency_hz-b.frequency_hz)<1e-12&&
        std::abs(a.process_sigma_hz-b.process_sigma_hz)<1e-12,
        "explicit process block did not replace scalar common covariance");
    auto invalid=source.point(90);invalid.process_covariance_valid=true;
    invalid.process_covariance={{{1,2,0},{2,1,0},{0,0,0}}};
    require(!matrix.observe(invalid).valid,"indefinite process covariance accepted");
    invalid=source.point(90);invalid.time_seconds+=.01;invalid.frequency_time_seconds+=.01;
    invalid.process_covariance_valid=true;
    invalid.process_covariance={{{1,0,0},{0,1e-14,2e-14},{0,2e-14,1e-14}}};
    require(!matrix.observe(invalid).valid,"mixed-unit indefinite process covariance accepted");
    invalid=source.point(90);invalid.time_seconds+=.02;invalid.frequency_time_seconds+=.02;
    invalid.process_covariance_valid=true;
    invalid.process_covariance={{{1,1e-7,0},{1e-7,1e-14,0},{0,0,0}}};
    require(matrix.observe(invalid).valid,"mixed-unit singular PSD process covariance rejected");
    invalid=source.point(90);invalid.time_seconds+=.03;invalid.frequency_time_seconds+=.03;
    invalid.process_covariance_valid=true;
    invalid.process_covariance={{{1,0,1e-20},{0,0,0},{1e-20,0,0}}};
    require(!matrix.observe(invalid).valid,"zero-variance process coordinate had nonzero covariance");
    invalid=source.point(91);invalid.separate_low_frequency_time=true;
    invalid.low_frequency_time_seconds=std::numeric_limits<double>::quiet_NaN();
    require(!matrix.observe(invalid).valid,"nonfinite low frequency date accepted");

    // A PSD non-rank-one process includes frequency-difference and phase noise.
    // The separate sandwich must retain those contributions.
    Source general_source(1102);general_source.noise=false;
    h::HelperJointEstimator general({});
    for(unsigned i=0;i<90;++i){auto x=general_source.point(i);
        x.process_covariance_valid=true;x.common_frequency_sigma_hz=0;
        x.process_covariance={{{.0001,.00002,.00001},{.00002,.0002,-.00001},{.00001,-.00001,.0003}}};
        (void)general.observe(x);
    }
    const auto g=general.predict(1600,30e6,36);
    require(g.valid&&g.process_sigma_hz>0&&g.phase_difference_process_sigma_radians>0&&
        std::abs(g.stochastic_sigma_hz*g.stochastic_sigma_hz-g.thermal_sigma_hz*g.thermal_sigma_hz-
                 g.process_sigma_hz*g.process_sigma_hz)<1e-10,
        "general process covariance was lost or counted as thermal");

    Source dates(1103);dates.noise=false;h::HelperJointEstimator dated({});
    for(unsigned i=0;i<90;++i){auto x=dates.point(i);
        x.frequency_time_seconds=x.time_seconds+.04;
        x.separate_low_frequency_time=true;x.low_frequency_time_seconds=x.time_seconds-.04;
        const auto value=[&](double nominal,double t){return nominal*(1+(dates.audio+dates.drift*t)*1e-6)+
            x.shift_hz*(dates.shift+dates.shift_drift*t)*1e-6;};
        x.high_frequency_hz=value(2400,x.frequency_time_seconds);
        x.low_frequency_hz=value(550,x.low_frequency_time_seconds);
        x.process_covariance_valid=true;x.process_covariance={};
        x.common_frequency_sigma_hz=100;
        (void)dated.observe(x);
    }
    const auto d=dated.predict(1600,30e6,36);
    const double truth=1600*(1+(dates.audio+dates.drift*36)*1e-6)+30e6*(dates.shift+dates.shift_drift*36)*1e-6;
    require(d.valid&&std::abs(d.frequency_hz-truth)<1e-8&&d.process_sigma_hz==0,
        "separate pilot frequency dates biased a linear drift or double-added scalar process");
    std::cout<<"helper_joint_process_dates scalar_equivalent=1 general_psd=1 invalid_psd=reject distinct_dates=1\n";
}
void bounded_gaps(){
    h::HelperJointEstimator::Config config;
    config.maximum_phase_branch_gap_seconds=12;
    config.phase_branch_audio_evolution_ppm_per_second=.2;
    double max_frequency_error=0;
    for(unsigned seed=1401;seed<1409;++seed){
        Source source(seed);std::array<h::HelperJointEstimator::Input,90> input;
        for(unsigned i=0;i<input.size();++i)input[i]=source.point(i);
        h::HelperJointEstimator ordinary({}),bounded(config);
        h::HelperJointEstimator::Estimate recovered;
        for(unsigned i=0;i<63;++i){auto x=input[i];if(i>=40)x.valid=false;
            (void)ordinary.observe(x);(void)bounded.observe(x);}
        const double reference=bounded.predict(1600,30e6,input[39].time_seconds).reference_input_seconds;
        require(!bounded.predict(1600,30e6,input[62].time_seconds).valid,"stale branch model authorized correction");
        for(unsigned i=63;i<input.size();++i){
            const auto old=ordinary.observe(input[i]);recovered=bounded.observe(input[i]);
            require(!old.valid&&old.ambiguity_limited,"default long-gap behavior changed");
            if(i<65)require(!recovered.valid&&recovered.post_gap_qualification_pending&&
                recovered.post_gap_observations==i-62,"stale prediction replaced fresh gap qualification");
        }
        recovered=bounded.predict(1600,30e6,36);
        const double truth=1600*(1+(source.audio+source.drift*36)*1e-6)+30e6*(source.shift+source.shift_drift*36)*1e-6;
        max_frequency_error=std::max(max_frequency_error,std::abs(recovered.frequency_hz-truth));
        const double gauge=reference-source.tx(reference);
        require(recovered.valid&&recovered.phase_branch_propagation_used&&!recovered.post_gap_qualification_pending&&
            recovered.reference_input_seconds==reference&&recovered.reference_preserved&&recovered.alternatives==1&&
            std::abs(recovered.phase_branch_gap_seconds-9.6)<1e-9&&
            std::abs(recovered.frequency_hz-truth)<=recovered.conditional_half_width_hz&&
            std::abs(recovered.source_seconds-source.tx(36)-gauge)<6*recovered.source_stochastic_sigma_seconds+recovered.source_model_half_width_seconds,
            "bounded gap lost frequency coverage or the one original source gauge");
    }
    // Missing and malformed data cannot count toward fresh qualification.
    Source source(1491);source.noise=false;
    std::array<h::HelperJointEstimator::Input,90> input;for(unsigned i=0;i<input.size();++i)input[i]=source.point(i);
    h::HelperJointEstimator missing(config);
    for(unsigned i=0;i<40;++i)(void)missing.observe(input[i]);
    auto resumed=missing.observe(input[63]);
    require(!resumed.valid&&resumed.post_gap_observations==1,"first post-gap point qualified");
    auto invalid=input[64];invalid.valid=false;
    resumed=missing.observe(invalid);
    require(!resumed.valid&&resumed.post_gap_qualification_pending&&resumed.post_gap_observations==0,
        "missing observation retained post-gap qualification streak");
    invalid=input[64];invalid.time_seconds+=.01;invalid.frequency_time_seconds+=.01;
    invalid.thermal_covariance[0][0]=-1;
    resumed=missing.observe(invalid);
    require(!resumed.valid&&resumed.post_gap_observations==0,"invalid covariance advanced gap qualification");
    resumed=missing.observe(input[65]);require(!resumed.valid&&resumed.post_gap_observations==1,"first renewed point qualified gap");
    resumed=missing.observe(input[66]);require(!resumed.valid&&resumed.post_gap_observations==2,"two fresh points qualified gap");
    require(missing.observe(input[67]).valid,"three fresh supported points did not qualify gap");

    h::HelperJointEstimator invalid_first(config);
    for(unsigned i=0;i<40;++i)(void)invalid_first.observe(input[i]);
    auto broken=input[63];broken.thermal_covariance[0][0]=-1;
    resumed=invalid_first.observe(broken);
    require(!resumed.valid&&resumed.post_gap_qualification_pending&&resumed.post_gap_observations==0&&
        resumed.reference_input_seconds==input[0].time_seconds,"failed first gap input erased pending state");
    resumed=invalid_first.observe(input[64]);
    require(!resumed.valid&&resumed.phase_branch_propagation_used&&resumed.post_gap_observations==1,
        "malformed covariance prevented a subsequent valid bounded branch enumeration");

    h::HelperJointEstimator interrupted(config);
    for(unsigned i=0;i<40;++i)(void)interrupted.observe(input[i]);
    (void)interrupted.observe(input[63]);
    broken=input[64];broken.valid=false;broken.model_contradicted=true;
    resumed=interrupted.observe(broken);
    require(!resumed.valid&&resumed.post_gap_observations==0,"contradiction retained gap qualification streak");
    resumed=interrupted.observe(input[65]);
    require(!resumed.valid&&resumed.post_gap_observations==1,"contradiction restored old gap authority");
    require(!interrupted.observe(input[66]).valid&&interrupted.observe(input[67]).valid,
        "fresh evidence after contradiction did not require a new full streak");

    // Explicit contrary evidence prevents a stale fit from choosing branches.
    h::HelperJointEstimator contradicted(config);
    for(unsigned i=0;i<40;++i)(void)contradicted.observe(input[i]);
    auto contrary=input[50];contrary.valid=false;contrary.model_contradicted=true;
    (void)contradicted.observe(contrary);
    resumed=contradicted.observe(input[63]);
    require(!resumed.valid&&!resumed.phase_branch_propagation_used&&resumed.reference_input_seconds==input[0].time_seconds,
        "contradicted model restored stale branch authority");

    // A noisy fresh phase can admit two branches. Keep them both and withhold
    // correction instead of selecting by a probe or the smallest residual.
    h::HelperJointEstimator alternatives(config);
    for(unsigned i=0;i<40;++i)(void)alternatives.observe(input[i]);
    auto weak=input[63];weak.thermal_covariance[2][2]=1;
    resumed=alternatives.observe(weak);
    require(!resumed.valid&&resumed.phase_branch_propagation_used&&resumed.alternatives>=2&&
        resumed.alternatives<=config.maximum_alternatives&&resumed.post_gap_qualification_pending&&
        resumed.reference_input_seconds==input[0].time_seconds,"multiple phase alternatives were silently selected");

    auto step_config=config;step_config.allow_frequency_step=true;
    h::HelperJointEstimator pending_step(step_config);
    for(unsigned i=0;i<40;++i)(void)pending_step.observe(input[i]);
    const auto pending=pending_step.observe(input[63]);
    require(!pending_step.begin_frequency_step(h::HelperJointEstimator::FrequencyStepModel::common_hz,20,20.8,
        std::span<const h::HelperJointEstimator::Input>(input.data()+63,4)),"step replay bypassed pending gap qualification");
    const auto unchanged=pending_step.predict(1600,30e6,input[63].time_seconds);
    require(!unchanged.valid&&unchanged.post_gap_qualification_pending&&unchanged.post_gap_observations==pending.post_gap_observations&&
        unchanged.accepted_observations==pending.accepted_observations&&unchanged.reference_input_seconds==pending.reference_input_seconds,
        "refused gap/step replay changed qualification or reference");

    // An explicitly wider evolution envelope can exceed bounded branch work;
    // it must report unresolved ambiguity, never choose/reseed a convenient one.
    auto overflow_config=config;overflow_config.phase_branch_audio_evolution_ppm_per_second=100;
    h::HelperJointEstimator overflow(overflow_config);
    for(unsigned i=0;i<40;++i)(void)overflow.observe(input[i]);
    resumed=overflow.observe(input[63]);
    require(!resumed.valid&&resumed.phase_branch_propagation_used&&resumed.ambiguity_limited&&
        resumed.post_gap_qualification_pending&&resumed.reference_input_seconds==input[0].time_seconds,
        "overflowed learned alternatives were silently collapsed or reanchored");
    auto short_config=config;short_config.maximum_phase_branch_gap_seconds=8;
    h::HelperJointEstimator too_old(short_config);
    for(unsigned i=0;i<40;++i)(void)too_old.observe(input[i]);
    resumed=too_old.observe(input[63]);
    require(!resumed.valid&&!resumed.phase_branch_propagation_used&&resumed.ambiguity_limited,
        "maximum declared branch gap was ignored");

    // A large clock acceleration during missing data violates the declared
    // evolution bound. Fresh frequency/phase inconsistency must withdraw this
    // example; this is not proof against every unobservable clock change.
    h::HelperJointEstimator accelerated(config);
    for(unsigned i=0;i<40;++i)(void)accelerated.observe(input[i]);
    const double start=input[39].time_seconds;
    for(unsigned i=63;i<70;++i){auto x=input[i];const double dt=x.time_seconds-start;
        constexpr double acceleration=10;
        x.high_frequency_hz+=2400*acceleration*(x.frequency_time_seconds-start)*1e-6;
        x.low_frequency_hz+=550*acceleration*(x.frequency_time_seconds-start)*1e-6;
        x.high_center_phase_radians=wrap(x.high_center_phase_radians+tau*2400*.5*acceleration*dt*dt*1e-6);
        x.low_center_phase_radians=wrap(x.low_center_phase_radians+tau*550*.5*acceleration*dt*dt*1e-6);
        resumed=accelerated.observe(x);require(!resumed.valid,"out-of-model gap acceleration qualified");
    }
    require(resumed.post_gap_qualification_pending&&resumed.phase_branch_propagation_used&&
        resumed.reference_input_seconds==input[0].time_seconds,"failed first gap observation erased its diagnostic/reference");

    auto work_config=config;work_config.maximum_observations=40;
    h::HelperJointEstimator work(work_config);for(unsigned i=0;i<40;++i)(void)work.observe(input[i]);
    resumed=work.observe(input[63]);
    require(!resumed.valid&&resumed.work_limited&&resumed.reference_input_seconds==input[0].time_seconds,
        "long-gap option bypassed retained-observation work bound");
    std::cout<<"helper_joint_bounded_gaps seeds=8 gap_seconds=9.6 max_frequency_error_hz="<<max_frequency_error
        <<" default=unavailable fresh=required contradiction=withdraw overflow=reported acceleration=withdraw work=bounded\n";
}
}
int main(){try{matched();controls();process_and_dates();frequency_step();bounded_gaps();return 0;}catch(const std::exception& e){std::cerr<<"Helper joint estimator failure: "<<e.what()<<'\n';return 1;}}
