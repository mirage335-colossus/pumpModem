// Evaluator-only diagnostic of curvature allowances on the existing joint fit.
// No result is fed back to acquisition, correction, branch choice or gating.
#include "../src/helper_joint_estimator.cpp"
#define main helper_continuation_included_main
#include "test_helper_continuation_study.cpp"
#undef main

namespace {
using Point=datapump::helper::Point;
using Fit=datapump::helper::Fit;
using Step=datapump::helper::Step;
using Q=datapump::helper::Q;
using V=datapump::helper::V;

// The bounded perturbation model is a Taylor expansion at the QUERY time.
// A small ADDITIVE common frequency component has bounded curvature and
// bounded endpoint value/slope. Since the fit models RF coefficients scaled
// by hop frequency, bound the difference between its additive affine tangent
// and that RF-scaled tangent explicitly. Audio-rate curvature affects pilot frequency and the
// integrated phase-difference observation; the unknown constant is absorbed
// by the phase-intercept nuisance. This L1 calculation deliberately makes no
// independence assumption about bounded errors or shared GLS observations.
double curvature_radius(const h::HelperJointEstimator::Config& c,
                        const std::vector<Point>& points,const Fit& fit,
                        double query,double rf_curvature,double audio_curvature){
    const auto row=h::frequency_row(c,1600,c.reference_shift_hz,query,fit.center,Step{});
    const auto influence=h::mult(fit.covariance,row);
    double result=0;
    for(const auto& p:points){
        const auto matrix=h::rows(c,p,fit.center,Step{});
        const double ratio=p.input.shift_hz/c.reference_shift_hz;
        const double dh=std::abs(p.input.frequency_time_seconds-query);
        const double dl=std::abs((p.input.separate_low_frequency_time?p.input.low_frequency_time_seconds:p.input.frequency_time_seconds)-query);
        const double dp=std::abs(p.input.time_seconds-query);
        constexpr double endpoint_offset_bound=.006,endpoint_slope_bound=.0007;
        const double tangent_h=std::abs(1-ratio)*(endpoint_offset_bound+endpoint_slope_bound*dh);
        const double tangent_l=std::abs(1-ratio)*(endpoint_offset_bound+endpoint_slope_bound*dl);
        const double bounds[]={.5*dh*dh*(rf_curvature+c.high_hz*1e-6*audio_curvature)+tangent_h,
                               .5*dl*dl*(rf_curvature+c.low_hz*1e-6*audio_curvature)+tangent_l,
                               tau*(c.high_hz-c.low_hz)*1e-6*audio_curvature*dp*dp*dp/6};
        for(unsigned s=0;s<3;++s){double weight=0;for(unsigned r=0;r<3;++r)weight+=h::dot(influence,matrix[r])*p.precision[r][s];
            result+=std::abs(weight)*bounds[s];}
    }
    return result;
}

int run(unsigned seed,double actual_curvature,double start){
    hp::Config config;config.path_loss_db=180;config.seed=seed;config.probe=hp::Probe::tone;
    ht::Impairments impairment;impairment.frequency_change_seconds=start;
    impairment.frequency_curvature_hz_per_second2=actual_curvature/2;
    ContinuationSource source(config,impairment,false,false);
    const double max_rf=31.225e6,prior_ppm=1.;
    const double width=3*(max_rf*prior_ppm+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,
        config.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
    Chain chain("dynamics-evaluator",h::HelperAidedFrequencyMethod::subaperture_slope,true,true,mc,config.shift_center_hz,true);
    h::HelperJointEstimator::Config c;c.reference_shift_hz=config.shift_center_hz;
    std::vector<Point> points;double reference=0,last_phase=0;
    for(unsigned dwell=0;dwell<90;++dwell){
        const auto data=source.generate(dwell);chain.observe(data,false,dwell);chain.evaluate(source,data,dwell);
        if(!chain.current_joint_accepted)continue;
        const auto& a=chain.receiver.last_aided;
        h::HelperJointEstimator::Input x;x.valid=true;x.time_seconds=a.center_time_seconds;
        x.frequency_time_seconds=a.high_frequency_time_seconds;x.separate_low_frequency_time=true;
        x.low_frequency_time_seconds=a.low_frequency_time_seconds;x.shift_hz=data.shift_hz;
        x.high_frequency_hz=a.measurement.high_frequency_hz;x.low_frequency_hz=a.measurement.low_frequency_hz;
        x.high_center_phase_radians=a.high_center_phase_radians;x.low_center_phase_radians=a.low_center_phase_radians;
        x.thermal_covariance=a.thermal_covariance;x.process_covariance_valid=a.process_covariance_valid;
        x.process_covariance=a.process_covariance;x.common_frequency_sigma_hz=a.measurement.common_phase_frequency_sigma_hz;
        x.frequency_model_half_width_hz=std::max(a.high_frequency_model_half_width_hz,a.low_frequency_model_half_width_hz);
        x.sample_support_valid=true;x.first_sample_time_seconds=a.measurement.first_sample_time_seconds;
        x.end_sample_time_seconds=a.measurement.end_sample_time_seconds;
        if(points.empty())reference=x.time_seconds;
        const double nominal=(c.high_hz-c.low_hz)*(x.time_seconds-reference);
        const double raw=wrap(wrap(x.high_center_phase_radians)-wrap(x.low_center_phase_radians)-tau*std::remainder(nominal,1.));
        const double unwrapped=points.empty()?raw:raw+tau*std::round((last_phase-raw)/tau);
        Point point;point.input=x;point.phase_residual=unwrapped;point.process=h::process_covariance(x);
        Q covariance=x.thermal_covariance;
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)covariance[i][j]+=point.process[i][j];
        require(h::inverse(covariance,point.precision),"reconstructed covariance is invalid");
        points.push_back(point);last_phase=unwrapped;
    }
    const auto snap=chain.joint.predict(1600,config.shift_center_hz,36);
    const double truth=1600*source.audio_scale_at(36)+source.shift_error_hz(config.shift_center_hz,36);
    const auto fit=h::fit(c,points,0,Step{});
    std::cout<<std::setprecision(10)<<"dynamics_evaluator seed="<<seed<<" rf_curvature_actual_hz_per_s2="<<actual_curvature
        <<" start_seconds="<<start<<" accepted="<<points.size()<<" valid="<<chain.frozen.valid;
    if(chain.frozen.valid&&fit.valid){
        const auto row=h::frequency_row(c,1600,c.reference_shift_hz,36,fit.center,Step{});
        const double reconstructed=1600+h::dot(row,fit.beta);
        require(std::abs(reconstructed-chain.frozen.frequency)<1e-5,"reconstruction differs from active selected branch");
        // Stated assumptions fixed BEFORE any trial: RF acceleration bounded by
        // 0.00004 Hz/s² and audio-rate acceleration by 0.0001 ppm/s².
        constexpr double rf_bound=.00004,audio_bound=.0001;
        const double extra=curvature_radius(c,points,fit,36,rf_bound,audio_bound);
        const double conditional=3*snap.stochastic_sigma_hz+snap.model_half_width_hz+extra;
        const double error=std::abs(chain.frozen.frequency-truth);
        const double midpoint=.5*(points.front().input.time_seconds+points.back().input.time_seconds);
        std::size_t begin=0;while(begin+1<points.size()&&points[begin].input.time_seconds<midpoint)++begin;
        const auto recent=h::fit(c,points,begin,Step{});
        const auto recent_row=h::frequency_row(c,1600,c.reference_shift_hz,36,recent.center,Step{});
        require(points.size()==snap.accepted_observations&&std::abs(reference-snap.reference_input_seconds)<1e-8,
            "accepted observation count or reference differs");
        require(std::abs(std::sqrt(h::variance(fit.covariance,row))-snap.stochastic_sigma_hz)<1e-6&&
            std::abs(h::model_radius(c,points,fit,row,Step{})-snap.model_half_width_hz)<1e-6&&
            std::abs((1600+h::dot(recent_row,recent.beta))-snap.recent_frequency_hz)<1e-5&&
            std::abs(std::sqrt(h::variance(recent.thermal_covariance,recent_row))-snap.recent_thermal_sigma_hz)<1e-6,
            "reconstructed uncertainty or recent fit differs");
        const auto full_influence=h::mult(fit.covariance,row),recent_influence=h::mult(recent.covariance,recent_row);
        double difference_variance=0,difference_model=0;
        for(std::size_t n=0;n<points.size();++n){
            const auto& point=points[n];const auto a=h::rows(c,point,fit.center,Step{});
            const auto b=h::rows(c,point,recent.center,Step{});
            std::array<double,3> weight{};
            for(unsigned s=0;s<3;++s)for(unsigned r=0;r<3;++r){
                weight[s]+=(n>=begin?h::dot(recent_influence,b[r]):0)*point.precision[r][s];
                weight[s]-=h::dot(full_influence,a[r])*point.precision[r][s];}
            for(unsigned s=0;s<3;++s){difference_model+=std::abs(weight[s])*h::bounds(c,point)[s];
                for(unsigned r=0;r<3;++r)difference_variance+=weight[s]*(point.input.thermal_covariance[s][r]+point.process[s][r])*weight[r];}
        }
        const double difference_sigma=std::sqrt(std::max(0.,difference_variance));
        const double difference=std::abs((1600+h::dot(recent_row,recent.beta))-reconstructed);
        const double detector_score=std::max(0.,difference-difference_model)/(difference_sigma+1e-30);
        std::cout<<" abs_error_hz="<<error<<" baseline_half_width_hz="<<chain.frozen.half_width
            <<" stochastic_sigma_hz="<<snap.stochastic_sigma_hz<<" model_half_width_hz="<<snap.model_half_width_hz
            <<" curvature_allowance_hz="<<extra<<" candidate_half_width_hz="<<conditional
            <<" baseline_covered="<<(error<=chain.frozen.half_width)<<" candidate_covered="<<(error<=conditional)
            <<" candidate_below_point1="<<(conditional<=.1)<<" recent_difference_hz="<<difference
            <<" difference_stochastic_sigma_hz="<<difference_sigma<<" difference_model_hz="<<difference_model
            <<" detector_score="<<detector_score;
    }
    std::cout<<'\n';return 0;
}
}
int main(int argc,char** argv){
 try{if(argc!=4)throw std::runtime_error("usage: SEED RF_CURVATURE_HZ_PER_S2 START_SECONDS");
    return run(static_cast<unsigned>(std::stoul(argv[1])),std::stod(argv[2]),std::stod(argv[3]));
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
