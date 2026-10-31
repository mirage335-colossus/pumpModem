#include "datapump/helper_frequency_recovery.hpp"
#include "helper_measurement_hops.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {
namespace h=datapump::helper;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Source {
    std::mt19937_64 rng;
    std::normal_distribution<double> normal;
    helper_measurement_hops::Profile hops;
    double shared_prediction_error=0;
    bool noise=true;
    explicit Source(unsigned seed):rng(seed),hops(helper_measurement_hops::hf50(seed)){
        shared_prediction_error=.025*normal(rng);
    }
    h::HelperFrequencyRecovery::Input input(unsigned i,double change=0){
        h::HelperFrequencyRecovery::Input x;x.valid=x.prediction_valid=x.reference_preserved=true;
        x.reference_input_seconds=2.1999791666666667;x.time_seconds=i*.4+.2;
        x.first_sample_seconds=i*.4+.02;x.end_sample_seconds=i*.4+.38;
        const double rf=hops.frequency(i);
        x.predicted_high_hz=2400*1.00005+rf*.8e-6+shared_prediction_error;
        x.predicted_low_hz=550*1.00005+rf*.8e-6+shared_prediction_error;
        const double common=noise?.003*normal(rng):0,z0=noise?normal(rng):0,z1=noise?normal(rng):0;
        x.high_frequency_hz=x.predicted_high_hz-shared_prediction_error+change+.07*z0+common;
        x.low_frequency_hz=x.predicted_low_hz-shared_prediction_error+change+.10*(.1*z0+std::sqrt(.99)*z1)+common;
        x.thermal_covariance={{{.07*.07,.1*.07*.10},{.1*.07*.10,.10*.10}}};
        x.common_frequency_sigma_hz=.003;
        x.prediction_last_observation_seconds=x.time_seconds-.4;
        x.prediction_high_sigma_hz=.04;x.prediction_low_sigma_hz=.04;
        return x;
    }
};
void matched_steps(){
    unsigned confirmed=0,provisional=0,covered=0;double max_error=0,max_radius=0;
    for(unsigned seed=901;seed<965;++seed){Source source(seed);h::HelperFrequencyRecovery detector({});
        bool saw=false,saw_provisional=false;h::HelperFrequencyRecovery::Result result;
        for(unsigned i=0;i<100;++i){
            result=detector.observe(source.input(i,i>=92?1.2:0));
            if(i<92)require(!result.epoch_requested,"clean pre-step history requested epoch");
            if(result.provisional)saw_provisional=true;
            if(result.epoch_requested){
                require(result.reference_preserved&&result.reference_input_seconds==2.1999791666666667,"step confirmation changed timing reference");
                require(result.change_lower_seconds<=36.8&&result.change_upper_seconds>=36.8,"change-point bracket omitted between-aperture change");
                require(result.candidate_observations>=3&&result.last_candidate_time_seconds-result.first_candidate_time_seconds>=.8-1e-8,"step confirmed without bounded fresh evidence");
                const double error=result.additive_correction_hz-1.2+source.shared_prediction_error;
                ++confirmed;covered+=std::abs(error)<=result.conditional_half_width_hz;
                max_error=std::max(max_error,std::abs(error));max_radius=std::max(max_radius,result.conditional_half_width_hz);saw=true;break;
            }
        }
        provisional+=saw_provisional;
        // A failed low-SNR change witness must remain unavailable, never
        // relabel a guessed correction as qualified phase synchronization.
        if(!saw)require(!result.epoch_requested,"unconfirmed step got authority");
    }
    require(confirmed>=60&&covered==confirmed&&provisional>=confirmed,"bounded common-step witness failed synthetic sensitivity/coverage");
    std::cout<<"helper_frequency_recovery_matched seeds=64 confirmed="<<confirmed<<" provisional="<<provisional
             <<" covered="<<covered<<" max_delta_error_hz="<<max_error<<" max_conditional_radius_hz="<<max_radius<<'\n';
}
void controls(){
    for(unsigned seed=101;seed<133;++seed){
        for(unsigned mode=0;mode<4;++mode){Source source(seed);h::HelperFrequencyRecovery detector({});
            for(unsigned i=0;i<100;++i){double change=0;
                if(mode==1&&i==92)change=1.2;
                if(mode==2)change=.03*(i*.4);
                if(mode==3){const double dt=std::max(0.,i*.4-18);change=.003*dt*dt;}
                const auto result=detector.observe(source.input(i,change));
                require(!result.epoch_requested,"clean/outlier/smooth drift was falsely declared localized step");
            }
        }
    }
    Source source(1001);source.noise=false;h::HelperFrequencyRecovery detector({});
    for(unsigned i=0;i<8;++i)(void)detector.observe(source.input(i));
    auto p=source.input(8,1.2);auto result=detector.observe(p);require(!result.epoch_requested,"one contradictory dwell confirmed epoch");
    auto missing=source.input(9,1.2);missing.valid=false;(void)detector.observe(missing);
    for(unsigned i=10;i<14;++i)require(!detector.observe(source.input(i,1.2)).epoch_requested,"missing observation preserved candidate sequence");
    h::HelperFrequencyRecovery unknown_missing({});Source dropout(1006);dropout.noise=false;
    for(unsigned i=0;i<8;++i)(void)unknown_missing.observe(dropout.input(i));
    h::HelperFrequencyRecovery::Input empty;empty.time_seconds=3.4;
    require(unknown_missing.observe(empty).reference_preserved,"default missing input destroyed known reference");
    empty.time_seconds=3.8;empty.reference_status_known=true;
    require(unknown_missing.observe(empty).reference_changed,"explicit known reference loss was ignored");
    h::HelperFrequencyRecovery differential({});Source diff(1002);diff.noise=false;
    for(unsigned i=0;i<8;++i)(void)differential.observe(diff.input(i));
    for(unsigned i=8;i<12;++i){auto x=diff.input(i,1.2);x.low_frequency_hz-=2;
        require(!differential.observe(x).epoch_requested,"audio/differential contradiction confirmed common RF step");}
    h::HelperFrequencyRecovery changed_reference({});Source refs(1003);refs.noise=false;
    for(unsigned i=0;i<8;++i)(void)changed_reference.observe(refs.input(i));
    auto x=refs.input(8,1.2);x.reference_input_seconds+=.0005;
    require(changed_reference.observe(x).reference_changed,"changed relative reference not reported");
    require(!changed_reference.observe(refs.input(9,1.2)).reference_preserved,"original date silently reseeded after reference change");
    h::HelperFrequencyRecovery malformed({});Source bad(1004);bad.noise=false;
    for(unsigned i=0;i<8;++i)(void)malformed.observe(bad.input(i));
    x=bad.input(8,1.2);x.thermal_covariance[0][1]=2;
    require(!malformed.observe(x).input_accepted,"indefinite current covariance accepted");
    x=bad.input(9,1.2);x.prediction_last_observation_seconds=x.time_seconds;
    require(!malformed.observe(x).input_accepted,"current-data prediction treated as independent old model");
    x=bad.input(10,1.2);x.process_covariance_valid=true;x.process_covariance={{{1,2},{2,1}}};
    require(!malformed.observe(x).input_accepted,"indefinite process covariance accepted");
    x=bad.input(11);x.process_covariance_valid=true;x.process_covariance={{{.00001,.000015},{.000015,.00004}}};
    x.frequency_dates_valid=true;x.high_frequency_time_seconds=x.time_seconds-.001;x.low_frequency_time_seconds=x.time_seconds+.002;
    require(malformed.observe(x).input_accepted,"valid unequal process kernels/dates declined");
    x=bad.input(12);x.frequency_dates_valid=true;x.high_frequency_time_seconds=x.end_sample_seconds+1;
    require(!malformed.observe(x).input_accepted,"outside-aperture frequency date accepted");
    h::HelperFrequencyRecovery::Config bounded;bounded.maximum_observations=3;bounded.confirmation_observations=3;
    bounded.minimum_confirmation_span_seconds=2;
    h::HelperFrequencyRecovery work(bounded);Source w(1005);w.noise=false;
    for(unsigned i=0;i<8;++i)(void)work.observe(w.input(i));
    for(unsigned i=8;i<12;++i)result=work.observe(w.input(i,1.2));
    require(result.work_limited&&!result.epoch_requested,"candidate work cap silently discarded alternatives");
    std::cout<<"helper_frequency_recovery_controls clean=reject outlier=reject smooth_change=reject missing=reset reference=preserved covariance=validated work=bounded\n";
}
void model_ambiguity(){
    // A small RF-proportional frequency change may look like an additive
    // common-Hz change within the local noise. The detector is not a model
    // selector: its result can only request a separately declared epoch model.
    unsigned candidates=0;
    for(unsigned seed=201;seed<233;++seed){Source source(seed);h::HelperFrequencyRecovery detector({});
        for(unsigned i=0;i<100;++i){const double change=i>=92?1.2*source.hops.frequency(i)/30e6:0;
            const auto r=detector.observe(source.input(i,change));
            if(r.epoch_requested){require(r.reference_preserved,"RF-scaled comparison changed reference");++candidates;break;}
        }
    }
    std::cout<<"helper_frequency_recovery_model_ambiguity rf_scaled_trials=32 additive_candidate_requests="<<candidates
             <<" interpretation=declared_model_required_not_identified\n";
}
void transition_support(){
    for(const double step_seconds:{36.779,36.83}){
        Source source(303);source.noise=false;h::HelperFrequencyRecovery detector({});
        bool saw=false;
        for(unsigned i=0;i<100;++i){
            auto x=source.input(i);const double duration=x.end_sample_seconds-x.first_sample_seconds;
            const double post=std::clamp((x.end_sample_seconds-step_seconds)/duration,0.,1.);
            // A valid fitted aperture average may hide an edge transition;
            // no subwindow/purity certificate is available to this detector.
            x.high_frequency_hz+=1.2*post;x.low_frequency_hz+=1.2*post;
            const auto r=detector.observe(x);
            if(r.epoch_requested){
                require(r.change_lower_seconds<=step_seconds&&r.change_upper_seconds>=step_seconds,
                    "reported change bracket excluded an in-aperture transition");
                const unsigned transition_dwell=static_cast<unsigned>(std::floor(step_seconds/.4));
                const double first=transition_dwell*.4+.02,end=transition_dwell*.4+.38;
                require(first<r.change_upper_seconds&&end>r.change_lower_seconds,
                    "transition aperture was falsely marked safe for frequency replay");
                saw=true;break;
            }
        }
        require(saw,"transition support control never exercised confirmed bracket");
    }
    std::cout<<"helper_frequency_recovery_transition support=outer_bracket boundary_frequency_rows=omit\n";
}
}
int main(){try{matched_steps();controls();model_ambiguity();transition_support();return 0;}catch(const std::exception& e){std::cerr<<"Helper frequency recovery failure: "<<e.what()<<'\n';return 1;}}
