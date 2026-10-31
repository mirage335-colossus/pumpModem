#include "datapump/helper_inversion_profile.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace datapump::helper {
namespace {
using C=std::complex<double>;
constexpr double tau=2*std::numbers::pi;
bool nn(double x){return std::isfinite(x)&&x>=0;}
double shape(double t){
    const auto smooth=[](double x){x=std::clamp(x,0.,1.);return x*x*x*x*(35+x*(-84+x*(70-20*x)));};
    return t<0?1-smooth((t+.008)/.008):-smooth(t/.008);
}
double threshold(std::size_t rows,std::size_t candidates,double trials,double tail){
    const double x=std::log(static_cast<double>(candidates)/tail)+trials;
    // A union bound over the bounded frequency choices and timing grid.
    // Each equal-rank complex projection difference has trace zero and
    // eigenvalues in [-1,1]. Estimated/colored PSD remains conditional.
    return 2*std::sqrt(static_cast<double>(rows)*x)+x;
}
C dot(std::span<const C> a,std::span<const C> b){C z{};for(std::size_t i=0;i<a.size();++i)z+=std::conj(a[i])*b[i];return z;}
double norm(std::span<const C> a){double n=0;for(auto z:a)n+=std::norm(z);return n;}
long double source_at(const HelperInversionProfile::ClockModel& c,long double sample,std::uint32_t fs){
    const auto dt=(sample-c.anchor_input_sample)/fs;
    return c.source_seconds_at_anchor+dt*c.audio_scale+.5L*c.audio_drift_ppm_per_second*1e-6L*dt*dt;
}
bool input_at(const HelperInversionProfile::ClockModel& c,long double source,std::uint32_t fs,long double& input){
    const auto d=source-c.source_seconds_at_anchor,k=static_cast<long double>(c.audio_drift_ppm_per_second)*1e-6L;
    const auto disc=static_cast<long double>(c.audio_scale)*c.audio_scale+2*k*d;
    if(!(disc>0))return false;
    const auto denominator=c.audio_scale+std::sqrt(disc);
    if(!(denominator>0))return false;
    input=c.anchor_input_sample+fs*2*d/denominator;return std::isfinite(input);
}
}
HelperInversionProfile::HelperInversionProfile(Config c):config_(c){
    if(c.sample_rate<8000||c.sample_rate>96000||!nn(c.search_half_seconds)||c.search_half_seconds<=0||c.search_half_seconds>.008||
       !nn(c.grid_step_seconds)||c.grid_step_seconds<=0||!nn(c.aperture_half_seconds)||c.aperture_half_seconds<.024||
       c.aperture_half_seconds>.040||!nn(c.bin_seconds)||c.bin_seconds<=0||!c.history_capacity||c.history_capacity>maximum_history||
       !nn(c.false_alarm_tail)||c.false_alarm_tail<=0||c.false_alarm_tail>=.1||!nn(c.likelihood_drop)||c.likelihood_drop<=0||
       !nn(c.maximum_frequency_half_width_hz)||c.maximum_frequency_half_width_hz>2)
        throw std::invalid_argument("invalid Helper inversion profile bounds");
    const auto intervals=std::llround(2*c.search_half_seconds/c.grid_step_seconds);
    if(intervals<2||intervals+1>static_cast<long long>(maximum_candidates)||
       std::abs(intervals*c.grid_step_seconds-2*c.search_half_seconds)>1e-10||
       2*c.aperture_half_seconds* c.sample_rate/std::max(1.,std::round(c.bin_seconds*c.sample_rate))>maximum_bins)
        throw std::invalid_argument("inversion grid/bin bound exceeded");
    candidates_=static_cast<std::size_t>(intervals+1);
}
const char* HelperInversionProfile::reason_text(Reason r)noexcept {
    switch(r){case Reason::accepted:return "conditional inversion profile accepted";case Reason::invalid_input:return "invalid received-sample prediction";
    case Reason::insufficient_samples:return "complete guarded marker aperture unavailable";case Reason::overlapping_observation:return "data aperture already used or overlaps";
    case Reason::no_marker_evidence:return "inversion does not beat straight auxiliary/noise allowance";case Reason::unresolved_timing:return "timing support reaches available search boundary";
    case Reason::frequency_mismatch:return "auxiliary frequency allowance outside bounded fit";case Reason::reference_mismatch:return "original relative clock reference changed";
    case Reason::unavailable_clock:return "unique retained relative clock unavailable";case Reason::clock_map_mismatch:return "current clock cannot align original data profiles";
    case Reason::history_full:return "bounded history unavailable";}return "unknown inversion profile state";
}
HelperInversionProfile::Observation HelperInversionProfile::observe(const Input& in){
    Observation out;
    const auto fs=config_.sample_rate;
    if(in.samples.empty()||in.first_sample>UINT64_MAX-in.samples.size()||in.capture_frontier_end_sample<in.first_sample+in.samples.size()||
       in.prediction_observation_end_sample>in.capture_frontier_end_sample||
       !std::isfinite(in.predicted_marker_sample)||!std::isfinite(in.nominal_marker_source_seconds)||
       !std::isfinite(in.auxiliary_frequency_hz)||std::abs(in.auxiliary_frequency_hz)>=fs/2.||
       !nn(in.frequency_half_width_hz)||!std::isfinite(in.audio_scale)||std::abs(in.audio_scale-1)>.001||!nn(in.audio_half_width_ppm))return out;
    if(in.frequency_half_width_hz>config_.maximum_frequency_half_width_hz){out.reason=Reason::frequency_mismatch;return out;}
    if(in.project_nuisance_pilots)for(auto f:in.nuisance_frequency_hz)if(!std::isfinite(f)||std::abs(f)>=fs/2.)return out;
    const auto lo=std::ceil(in.predicted_marker_sample-config_.aperture_half_seconds*fs);
    const auto hi=std::floor(in.predicted_marker_sample+config_.aperture_half_seconds*fs);
    if(lo<in.first_sample||hi>static_cast<long double>(in.first_sample+in.samples.size())||lo<0||hi<=lo){out.reason=Reason::insufficient_samples;return out;}
    const auto first=static_cast<std::uint64_t>(lo),end=static_cast<std::uint64_t>(hi);
    if(count_&&first<last_end_){out.reason=Reason::overlapping_observation;return out;}
    const auto bin_count=static_cast<std::size_t>(std::max(1.,std::round(config_.bin_seconds*fs)));
    const auto bins=static_cast<std::size_t>((end-first+bin_count-1)/bin_count);
    if(bins>maximum_bins||bins<8)return out;
    const auto data=in.samples.subspan(static_cast<std::size_t>(first-in.first_sample),static_cast<std::size_t>(end-first));
    for(auto z:data)if(!std::isfinite(z.real())||!std::isfinite(z.imag()))return out;
    // Count processed apertures even when the residual-frequency witness
    // subsequently rejects them. This is work accounting, not admission.
    out.raw_samples_processed=data.size();
    std::fill_n(y_.begin(),bins,C{});for(auto& c:nuisance_)std::fill_n(c.begin(),bins,C{});
    const C down_step=std::polar(1.,-tau*in.auxiliary_frequency_hz/fs);
    const std::array<C,2> nuisance_step{std::polar(1.,tau*(in.nuisance_frequency_hz[0]-in.auxiliary_frequency_hz)/fs),
        std::polar(1.,tau*(in.nuisance_frequency_hz[1]-in.auxiliary_frequency_hz)/fs)};
    C down{1,0};std::array<C,2> tones{C{1,0},C{1,0}};
    double raw_energy=0;
    for(std::size_t j=0;j<data.size();++j){const auto b=j/bin_count;y_[b]+=C(data[j])*down;raw_energy+=std::norm(data[j]);
        if(in.project_nuisance_pilots)for(std::size_t k=0;k<2;++k){nuisance_[k][b]+=tones[k];tones[k]*=nuisance_step[k];}
        down*=down_step;
    }
    for(std::size_t b=0;b<bins;++b){const auto n=std::min(bin_count,data.size()-b*bin_count);roots_[b]=std::sqrt(static_cast<double>(n));
        times_[b]=(static_cast<long double>(first)+b*bin_count+(n-1)/2.L-in.predicted_marker_sample)/fs;
        y_[b]/=roots_[b];for(auto& c:nuisance_)c[b]/=roots_[b];}
    const auto ys=std::span<const C>(y_.data(),bins);
    std::array<C,2> raw_gains{};
    unsigned rank=0;
    if(in.project_nuisance_pilots){
        const auto a=std::span<const C>(nuisance_[0].data(),bins),b=std::span<const C>(nuisance_[1].data(),bins);
        const auto aa=norm(a),bb=norm(b);const auto ab=dot(a,b),ay=dot(a,ys),by=dot(b,ys);const double determinant=aa*bb-std::norm(ab);
        if(!(determinant>1e-12*aa*bb))return out;
        raw_gains[0]=(bb*ay-ab*by)/determinant;raw_gains[1]=(aa*by-std::conj(ab)*ay)/determinant;
        for(std::size_t k=0;k<2;++k){for(std::size_t r=0;r<k;++r){const auto projection=dot(std::span<const C>(nuisance_[r].data(),bins),std::span<const C>(nuisance_[k].data(),bins));
                for(std::size_t j=0;j<bins;++j)nuisance_[k][j]-=projection*nuisance_[r][j];}
            const auto length=std::sqrt(norm(std::span<const C>(nuisance_[k].data(),bins)));if(!(length>1e-8))return out;
            for(std::size_t j=0;j<bins;++j)nuisance_[k][j]/=length;
            ++rank;}
    }
    std::copy_n(y_.begin(),bins,projected_.begin());
    for(unsigned r=0;r<rank;++r){const auto projection=dot(std::span<const C>(nuisance_[r].data(),bins),ys);
        for(std::size_t j=0;j<bins;++j)projected_[j]-=projection*nuisance_[r][j];}
    // Received-sample first differences after fitted pure-pilot removal.
    // White thermal noise is unbiased; residual auxiliary/probe structure and
    // imperfect removal inflate this PSD rather than provide extra evidence.
    down={1,0};tones={C{1,0},C{1,0}};C previous{};double difference_energy=0;
    for(std::size_t j=0;j<data.size();++j){C residual=C(data[j])*down;
        if(in.project_nuisance_pilots)for(std::size_t k=0;k<2;++k){residual-=raw_gains[k]*tones[k];tones[k]*=nuisance_step[k];}
        if(j)difference_energy+=std::norm(residual-previous);
        previous=residual;down*=down_step;}
    const double noise=std::max(difference_energy/(2*(data.size()-1))*(1+6/std::sqrt(data.size()/2.)),
        128*std::numeric_limits<double>::epsilon()*raw_energy/data.size());
    if(!(noise>0)||!std::isfinite(noise))return out;
    const std::size_t frequencies=in.frequency_half_width_hz>1e-9?3:1;
    std::array<double,3> offsets{0,-in.frequency_half_width_hz,in.frequency_half_width_hz};
    const auto projection_energy=[&](std::span<const C> q){
        double denominator=norm(q);for(unsigned r=0;r<rank;++r)denominator-=std::norm(dot(std::span<const C>(nuisance_[r].data(),bins),q));
        return denominator>1e-10?std::norm(dot(q,std::span<const C>(projected_.data(),bins)))/denominator:0;
    };
    double null_energy=0;
    for(std::size_t f=0;f<frequencies;++f){for(std::size_t b=0;b<bins;++b)carrier_[f][b]=roots_[b]*std::polar(1.,tau*offsets[f]*times_[b]);
        null_energy=std::max(null_energy,projection_energy(std::span<const C>(carrier_[f].data(),bins)));}
    Row row;row.valid=true;row.dwell=in.dwell;row.first=first;row.end=end;row.center=in.predicted_marker_sample;row.nominal=in.nominal_marker_source_seconds;
    row.prediction_end=in.prediction_observation_end_sample?in.prediction_observation_end_sample:in.capture_frontier_end_sample;
    row.audio=in.audio_scale;row.noise=noise;row.frequency_trials_log=std::log(static_cast<double>(frequencies));
    row.clock_radius=.008*in.audio_half_width_ppm*1e-6/(.999*.999);
    std::size_t best=0;
    for(std::size_t k=0;k<candidates_;++k){const double offset=-config_.search_half_seconds+k*config_.grid_step_seconds;
        for(std::size_t b=0;b<bins;++b){const auto n=std::min(bin_count,data.size()-b*bin_count);
            const double left=static_cast<double>((static_cast<long double>(first)+b*bin_count-in.predicted_marker_sample)/fs-offset)*in.audio_scale;
            const double right=left+(n-1)*in.audio_scale/fs;
            if(right<=-.008)envelope_[b]=1;else if(left>=.008)envelope_[b]=-1;
            else{double sum=0;for(std::size_t j=0;j<n;++j)sum+=shape(left+j*in.audio_scale/fs);envelope_[b]=sum/n;}}
        double signal=0;std::size_t selected=0;
        for(std::size_t f=0;f<frequencies;++f){for(std::size_t b=0;b<bins;++b)temporary_[b]=envelope_[b]*carrier_[f][b];
            const auto energy=projection_energy(std::span<const C>(temporary_.data(),bins));if(energy>signal){signal=energy;selected=f;}}
        row.score[k]=(signal-null_energy)/noise;row.residual_frequency[k]=offsets[selected];
        if(row.score[k]>row.score[best])best=k;
    }
    Estimate e;e.observations=1;e.candidates=candidates_;e.bins=bins;e.first_sample=first;e.end_sample=end;e.latest_observation_end_sample=std::max(end,row.prediction_end);
    e.estimated_complex_noise_power=noise;e.likelihood_score=row.score[best];e.detection_threshold=threshold(1,candidates_,row.frequency_trials_log,config_.false_alarm_tail);
    e.offset_seconds=-config_.search_half_seconds+best*config_.grid_step_seconds;
    std::size_t lower=best,upper=best;for(std::size_t k=0;k<candidates_;++k)if(row.score[k]>=row.score[best]-config_.likelihood_drop){lower=std::min(lower,k);upper=std::max(upper,k);}
    const double model_radius=.5*(config_.grid_step_seconds+static_cast<double>(bin_count)/fs)+row.clock_radius;
    e.lower_offset_seconds=-config_.search_half_seconds+lower*config_.grid_step_seconds-model_radius;
    e.upper_offset_seconds=-config_.search_half_seconds+upper*config_.grid_step_seconds+model_radius;
    e.conditional_half_width_seconds=std::max(e.offset_seconds-e.lower_offset_seconds,e.upper_offset_seconds-e.offset_seconds);
    e.marker_input_sample=in.predicted_marker_sample+e.offset_seconds*fs;e.dwell_start_input_sample=e.marker_input_sample-.160*fs/in.audio_scale;
    e.marker_lower_input_sample=in.predicted_marker_sample+e.lower_offset_seconds*fs;
    e.marker_upper_input_sample=in.predicted_marker_sample+e.upper_offset_seconds*fs;
    const double lower_audio=in.audio_scale-in.audio_half_width_ppm*1e-6;
    if(!(lower_audio>0)){out.reason=Reason::invalid_input;return out;}
    const double boundary_rate_radius=.160*in.audio_half_width_ppm*1e-6/(lower_audio*lower_audio);
    e.dwell_lower_input_sample=e.marker_lower_input_sample-.160*fs/in.audio_scale-boundary_rate_radius*fs;
    e.dwell_upper_input_sample=e.marker_upper_input_sample-.160*fs/in.audio_scale+boundary_rate_radius*fs;
    e.conditional_half_width_seconds+=boundary_rate_radius;
    e.residual_frequency_hz=row.residual_frequency[best];
    // A cheap residual-frequency witness uses the SAME samples, not an
    // independent observation or another likelihood contribution. Weak halves
    // need not individually pass a detection threshold.
    const auto chosen=std::abs(row.residual_frequency[best])<1e-12?0U:row.residual_frequency[best]<0?1U:2U;
    std::array<C,2> halves{};std::array<double,2> weights{},dates{};
    for(std::size_t b=0;b<bins;++b){
        const double left=static_cast<double>((static_cast<long double>(first)+b*bin_count-in.predicted_marker_sample)/fs-e.offset_seconds)*in.audio_scale;
        const auto n=std::min(bin_count,data.size()-b*bin_count);double mean=0;
        for(std::size_t j=0;j<n;++j)mean+=shape(left+j*in.audio_scale/fs);
        const auto q=(mean/n)*carrier_[chosen][b];const auto side=times_[b]<e.offset_seconds?0U:1U;
        halves[side]+=std::conj(q)*projected_[b];weights[side]+=std::norm(q);dates[side]+=std::norm(q)*times_[b];
    }
    if(weights[0]>0&&weights[1]>0&&std::norm(halves[0])>0&&std::norm(halves[1])>0){
        const auto dt=dates[1]/weights[1]-dates[0]/weights[0];
        if(dt>0){
            e.residual_frequency_hz+=std::arg(halves[1]*std::conj(halves[0]))/(tau*dt);
            const auto variance=noise*.5*(weights[0]/std::norm(halves[0])+weights[1]/std::norm(halves[1]));
            const auto sigma=std::sqrt(variance)/(tau*dt);
            if(std::abs(e.residual_frequency_hz)>in.frequency_half_width_hz+6*sigma+.05){
                e.reason=Reason::frequency_mismatch;out.dwell=e;out.reason=e.reason;return out;
            }
        }
    }
    e.available=e.likelihood_score>e.detection_threshold;
    e.qualified=e.available&&lower>0&&upper+1<candidates_&&e.conditional_half_width_seconds<config_.search_half_seconds;
    e.reason=e.qualified?Reason::accepted:e.available?Reason::unresolved_timing:Reason::no_marker_evidence;
    rows_[next_]=row;next_=(next_+1)%config_.history_capacity;count_=std::min(count_+1,config_.history_capacity);
    last_end_=end;frontier_=std::max(frontier_,in.capture_frontier_end_sample);out.stored=true;out.reason=Reason::accepted;out.dwell=e;
    out.history_observations=count_;return out;
}
HelperInversionProfile::Estimate HelperInversionProfile::estimate(const ClockModel& clock,double target,std::uint64_t frontier){
    Estimate out;out.candidates=candidates_;out.observations=count_;out.reference_input_sample=have_reference_?reference_:clock.reference_input_sample;
    if(!count_||!clock.available||!clock.reference_preserved){out.reason=Reason::unavailable_clock;return out;}
    if(!std::isfinite(clock.reference_input_sample)||clock.reference_input_sample<0||!std::isfinite(clock.anchor_input_sample)||
       !std::isfinite(clock.source_seconds_at_anchor)||!std::isfinite(clock.audio_scale)||std::abs(clock.audio_scale-1)>.001||
       !std::isfinite(clock.audio_drift_ppm_per_second)||!nn(clock.source_half_width_seconds)||!nn(clock.audio_half_width_ppm)||
       !std::isfinite(target)||clock.latest_observation_end_sample>frontier||frontier<frontier_){out.reason=Reason::invalid_input;return out;}
    if(!have_reference_){reference_=clock.reference_input_sample;have_reference_=true;}
    if(reference_!=clock.reference_input_sample)reference_mismatch_=true;
    if(reference_mismatch_){out.reason=Reason::reference_mismatch;return out;}
    double common_low=-config_.search_half_seconds,common_high=config_.search_half_seconds,trials=0,max_clock=clock.source_half_width_seconds;
    long double first_center=std::numeric_limits<long double>::infinity(),last_center=-std::numeric_limits<long double>::infinity();
    for(std::size_t r=0;r<count_;++r){first_center=std::min(first_center,rows_[r].center);last_center=std::max(last_center,rows_[r].center);}
    const auto midpoint=(first_center+last_center)/2;
    std::array<std::size_t,2> group_counts{};std::array<double,2> group_trials{};
    for(std::size_t r=0;r<count_;++r){const auto group=rows_[r].center<midpoint?0U:1U;++group_counts[group];group_trials[group]+=rows_[r].frequency_trials_log;}
    out.first_sample=UINT64_MAX;
    for(std::size_t r=0;r<count_;++r){const auto& row=rows_[r];out.first_sample=std::min(out.first_sample,row.first);out.end_sample=std::max(out.end_sample,row.end);
        const auto low=source_at(clock,row.center-config_.search_half_seconds*config_.sample_rate,config_.sample_rate)-row.nominal;
        const auto high=source_at(clock,row.center+config_.search_half_seconds*config_.sample_rate,config_.sample_rate)-row.nominal;
        common_low=std::max(common_low,static_cast<double>(low));common_high=std::min(common_high,static_cast<double>(high));trials+=row.frequency_trials_log;
        max_clock=std::max(max_clock,clock.source_half_width_seconds+row.clock_radius+
            static_cast<double>(std::abs(row.center-clock.anchor_input_sample)/config_.sample_rate)*clock.audio_half_width_ppm*1e-6);
        out.estimated_complex_noise_power+=row.noise/count_;
    }
    out.latest_observation_end_sample=std::max(out.end_sample,clock.latest_observation_end_sample);
    for(std::size_t r=0;r<count_;++r)out.latest_observation_end_sample=std::max(out.latest_observation_end_sample,rows_[r].prediction_end);
    if(!(common_low<common_high)){out.reason=Reason::clock_map_mismatch;out.clock_model_consistent=false;return out;}
    std::array<double,maximum_candidates> total;total.fill(-std::numeric_limits<double>::infinity());
    std::array<std::array<double,maximum_candidates>,2> groups{};
    for(auto& group:groups)group.fill(-std::numeric_limits<double>::infinity());
    std::size_t best=0,first_valid=candidates_,last_valid=0;
    for(std::size_t k=0;k<candidates_;++k){const double offset=-config_.search_half_seconds+k*config_.grid_step_seconds;
        if(offset<common_low||offset>common_high)continue;
        double score=0;std::array<double,2> group_score{};bool valid=true;
        for(std::size_t r=0;r<count_;++r){const auto& row=rows_[r];long double at=0;if(!input_at(clock,row.nominal+offset,config_.sample_rate,at)){valid=false;break;}
            const double position=(static_cast<double>((at-row.center)/config_.sample_rate)+config_.search_half_seconds)/config_.grid_step_seconds;
            if(position<0||position>candidates_-1){valid=false;break;}
            const auto left=static_cast<std::size_t>(std::floor(position));const auto right=std::min(left+1,candidates_-1);const auto fraction=position-left;
            const auto contribution=row.score[left]*(1-fraction)+row.score[right]*fraction;score+=contribution;
            group_score[row.center<midpoint?0U:1U]+=contribution;
        }
        if(valid){total[k]=score;groups[0][k]=group_score[0];groups[1][k]=group_score[1];first_valid=std::min(first_valid,k);last_valid=std::max(last_valid,k);if(score>total[best])best=k;}
    }
    if(first_valid==candidates_){out.reason=Reason::clock_map_mismatch;out.clock_model_consistent=false;return out;}
    out.offset_seconds=-config_.search_half_seconds+best*config_.grid_step_seconds;out.likelihood_score=total[best];
    out.detection_threshold=threshold(count_,candidates_,trials,config_.false_alarm_tail);
    std::size_t lower=best,upper=best;for(std::size_t k=first_valid;k<=last_valid;++k)if(total[k]>=total[best]-config_.likelihood_drop){lower=std::min(lower,k);upper=std::max(upper,k);}
    const double radius=.5*(config_.grid_step_seconds+config_.bin_seconds)+max_clock;
    out.lower_offset_seconds=-config_.search_half_seconds+lower*config_.grid_step_seconds-radius;
    out.upper_offset_seconds=-config_.search_half_seconds+upper*config_.grid_step_seconds+radius;
    out.conditional_half_width_seconds=std::max(out.offset_seconds-out.lower_offset_seconds,out.upper_offset_seconds-out.offset_seconds);
    if(!input_at(clock,target+out.offset_seconds,config_.sample_rate,out.marker_input_sample)||
       !input_at(clock,target-.160+out.offset_seconds,config_.sample_rate,out.dwell_start_input_sample)){out.reason=Reason::clock_map_mismatch;return out;}
    if(!input_at(clock,target+out.lower_offset_seconds,config_.sample_rate,out.marker_lower_input_sample)||
       !input_at(clock,target+out.upper_offset_seconds,config_.sample_rate,out.marker_upper_input_sample)||
       !input_at(clock,target-.160+out.lower_offset_seconds,config_.sample_rate,out.dwell_lower_input_sample)||
       !input_at(clock,target-.160+out.upper_offset_seconds,config_.sample_rate,out.dwell_upper_input_sample)){
        out.reason=Reason::clock_map_mismatch;return out;
    }
    const auto lower_scale=clock.audio_scale+clock.audio_drift_ppm_per_second*1e-6L*
        (out.dwell_lower_input_sample-clock.anchor_input_sample)/config_.sample_rate-clock.audio_half_width_ppm*1e-6L;
    const auto upper_scale=clock.audio_scale+clock.audio_drift_ppm_per_second*1e-6L*
        (out.marker_upper_input_sample-clock.anchor_input_sample)/config_.sample_rate-clock.audio_half_width_ppm*1e-6L;
    const auto minimum_scale=std::min(lower_scale,upper_scale);
    if(!(minimum_scale>0)){out.reason=Reason::clock_map_mismatch;return out;}
    const auto rate_radius=.160L*clock.audio_half_width_ppm*1e-6L/(minimum_scale*minimum_scale)*config_.sample_rate;
    out.dwell_lower_input_sample-=rate_radius;out.dwell_upper_input_sample+=rate_radius;
    out.conditional_half_width_seconds=static_cast<double>(std::max({out.marker_input_sample-out.marker_lower_input_sample,
        out.marker_upper_input_sample-out.marker_input_sample,out.dwell_start_input_sample-out.dwell_lower_input_sample,
        out.dwell_upper_input_sample-out.dwell_start_input_sample})/config_.sample_rate);
    out.available=out.likelihood_score>out.detection_threshold;
    out.qualified=out.available&&lower>first_valid&&upper<last_valid&&out.conditional_half_width_seconds<config_.search_half_seconds;
    // A fixed clock curve must align early and late DATA, not just give a
    // narrow optimum after inconsistent profiles have been added. Groups are
    // disjoint raw apertures, and the current clock allowance is added once
    // as a shared bound. A weak group need not independently detect a marker.
    if(group_counts[0]>=4&&group_counts[1]>=4){
        std::array<std::size_t,2> maxima{first_valid,first_valid},lows{},highs{};
        std::array<bool,2> detected{};
        for(unsigned group=0;group<2;++group){
            for(std::size_t k=first_valid;k<=last_valid;++k)if(groups[group][k]>groups[group][maxima[group]])maxima[group]=k;
            lows[group]=highs[group]=maxima[group];
            for(std::size_t k=first_valid;k<=last_valid;++k)if(groups[group][k]>=groups[group][maxima[group]]-config_.likelihood_drop){
                lows[group]=std::min(lows[group],k);highs[group]=std::max(highs[group],k);}
            detected[group]=groups[group][maxima[group]]>threshold(group_counts[group],candidates_,group_trials[group],config_.false_alarm_tail);
        }
        out.first_half_offset_seconds=-config_.search_half_seconds+maxima[0]*config_.grid_step_seconds;
        out.second_half_offset_seconds=-config_.search_half_seconds+maxima[1]*config_.grid_step_seconds;
        const double padding=.5*(config_.grid_step_seconds+config_.bin_seconds)+max_clock;
        const double low0=-config_.search_half_seconds+lows[0]*config_.grid_step_seconds-padding;
        const double high0=-config_.search_half_seconds+highs[0]*config_.grid_step_seconds+padding;
        const double low1=-config_.search_half_seconds+lows[1]*config_.grid_step_seconds-padding;
        const double high1=-config_.search_half_seconds+highs[1]*config_.grid_step_seconds+padding;
        if(detected[0]&&detected[1]&&std::max(low0,low1)>std::min(high0,high1)){
            out.clock_model_consistent=false;out.qualified=false;out.reason=Reason::clock_map_mismatch;return out;
        }
    }
    out.reason=out.qualified?Reason::accepted:out.available?Reason::unresolved_timing:Reason::no_marker_evidence;return out;
}
}
