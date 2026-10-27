#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
namespace pilot_reference {
constexpr double tau=2*std::numbers::pi,period=.4;
constexpr std::size_t capacity=96;
inline double elapsed(std::uint64_t a,std::uint64_t b,std::uint32_t fs){return a>=b?static_cast<double>(a-b)/fs:-static_cast<double>(b-a)/fs;}
struct Config {
    std::uint32_t rate=24000;
    double phase_sigma=.01;
    // Explicit experimental supported condition, NOT inferred from static ppm
    // and NOT a new application setting.
    double supported_slew_hz_per_second=.03;
    double maximum_reference_age=4.1;
};
struct ModelBound {bool valid=false;double radians=0,minimum_mean_amplitude=0,center=0,radius=0,real_lower=0,imaginary_upper=0;};
// Continuous unmodulated real PCM only. No periodic envelope, punctuation,
// metadata, source calendar or decoded time is used by this component.
inline ModelBound bound(const Config& c,double residual_hz,double nco_hz=2450.3125) {
    ModelBound b;
    if(!(residual_hz>=0) || !std::isfinite(residual_hz) ||
       !(nco_hz>=200 && nco_hz<=c.rate/2.-200))return b;
    const auto samples=std::max<std::size_t>(1,std::llround(c.rate*period));
    const auto aperture=static_cast<double>(samples)/c.rate;
    if(samples<2 || !(residual_hz*aperture<1))return b;
    // The centered discrete rectangular CW response is exactly real.
    // A locally linear chirp has a separate integrated phase disk.
    const auto pure_min=residual_hz>0?std::sin(std::numbers::pi*residual_hz*aperture)/
        (c.rate*std::sin(std::numbers::pi*residual_hz/c.rate)):aperture;
    const auto chirp=std::numbers::pi*c.supported_slew_hz_per_second*aperture*aperture*aperture/12;
    // Discrete summation by parts bounds the real-PCM conjugate image,
    // including residual phase total variation. A continuous denominator
    // would underbound this image at high audio frequencies.
    const auto variation=tau*residual_hz*aperture;
    const auto image=(2+variation)/(2*c.rate*std::abs(std::sin(std::numbers::pi*2*nco_hz/c.rate)));
    const auto disk=chirp+image,lower=pure_min-disk;
    if(!(lower>0) || !std::isfinite(disk))return b;
    b.valid=true;b.center=pure_min;b.radius=disk;b.real_lower=lower;
    b.imaginary_upper=disk;b.radians=std::atan2(disk,lower);
    b.minimum_mean_amplitude=lower/aperture;return b;
}

struct Point {
    double seconds=0,phase=0,thermal=0,aperture_bound=0,window_process=0;
    std::uint64_t first=0,end=0;
    double coarse_phase=0,coarse_thermal=0,coarse_bound=0,coarse_window_process=0;
};
struct Estimate {
    bool valid=false;double hz=0,slope=0,uncertainty=0,slope_uncertainty=0;
    double thermal_sigma=0,process_sigma=0,aperture_hz=0,chi=0,seconds=0,support=0;
    std::uint64_t first=0,end=0;std::size_t count=0;
};
inline Estimate fit(std::span<const Point> p,double reference_hz,const Config& cfg) {
    Estimate out;if(p.size()<4 || p.size()>capacity)return out;
    const auto now=p.back().seconds,span=now-p.front().seconds;if(span<.7)return out;
    using Real=long double;
    std::array<std::array<Real,6>,3> m{};std::array<Real,3> y{};
    for(const auto& q:p){if(!(q.thermal>0) || !(q.aperture_bound>=0) || !(q.window_process>=0))return out;
        const auto t=(q.seconds-now)/span;const std::array<Real,3> x{1,t,.5*t*t};const auto w=1/q.thermal;
        for(unsigned i=0;i<3;++i){y[i]+=w*x[i]*q.phase;for(unsigned j=0;j<3;++j)m[i][j]+=w*x[i]*x[j];}}
    for(unsigned i=0;i<3;++i)m[i][i+3]=1;
    for(unsigned col=0;col<3;++col){auto pivot=col;for(unsigned row=col+1;row<3;++row)if(std::abs(m[row][col])>std::abs(m[pivot][col]))pivot=row;
        if(!(std::abs(m[pivot][col])>1e-18L))return out;std::swap(m[pivot],m[col]);
        const auto scale=m[col][col];for(auto& v:m[col])v/=scale;
        for(unsigned row=0;row<3;++row)if(row!=col){const auto factor=m[row][col];for(unsigned j=0;j<6;++j)m[row][j]-=factor*m[col][j];}}
    std::array<Real,3> coefficient{};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)coefficient[i]+=m[i][j+3]*y[j];
    out.hz=reference_hz+static_cast<double>(coefficient[1])/span;
    out.slope=static_cast<double>(coefficient[2])/(span*span);
    std::array<double,capacity> fweight{},sweight{};
    double fthermal=0,sthermal=0,fwindow=0,swindow=0,fbounded=0,sbounded=0;
    for(std::size_t i=0;i<p.size();++i){const auto& q=p[i];const auto t=(q.seconds-now)/span;
        const std::array<Real,3> x{1,t,.5*t*t};Real f=0,s=0;
        for(unsigned j=0;j<3;++j){f+=m[1][j+3]*x[j]/q.thermal/span;s+=m[2][j+3]*x[j]/q.thermal/(span*span);}
        fweight[i]=static_cast<double>(f);sweight[i]=static_cast<double>(s);
        fthermal+=fweight[i]*fweight[i]*q.thermal;sthermal+=sweight[i]*sweight[i]*q.thermal;
        fwindow+=fweight[i]*fweight[i]*q.window_process;swindow+=sweight[i]*sweight[i]*q.window_process;
        fbounded+=std::abs(fweight[i])*q.aperture_bound;sbounded+=std::abs(sweight[i])*q.aperture_bound;
        const auto prediction=coefficient[0]+coefficient[1]*t+.5L*coefficient[2]*t*t;
        const auto remainder=std::max(0.,std::abs(q.phase-static_cast<double>(prediction))-q.aperture_bound);
        out.chi+=remainder*remainder/(q.thermal+q.window_process+2*cfg.phase_sigma*cfg.phase_sigma*span/(tau*tau));}
    // Shared Brownian point increments use actual dates and exact coefficient
    // tails. Average-within-window errors are disjoint increments; their
    // correlation with point terms is retained by Minkowski, not cancelled.
    double fpoint=0,spoint=0,ftail=0,stail=0;
    for(std::size_t i=p.size();i-->1;){ftail+=fweight[i];stail+=sweight[i];const auto dt=p[i].seconds-p[i-1].seconds;
        if(!(dt>0))return {};
        fpoint+=2*cfg.phase_sigma*cfg.phase_sigma*dt*ftail*ftail/(tau*tau);
        spoint+=2*cfg.phase_sigma*cfg.phase_sigma*dt*stail*stail/(tau*tau);}
    const auto fprocess=std::pow(std::sqrt(fpoint)+std::sqrt(fwindow),2);
    const auto sprocess=std::pow(std::sqrt(spoint)+std::sqrt(swindow),2);
    out.thermal_sigma=std::sqrt(fthermal);out.process_sigma=std::sqrt(fprocess);out.aperture_hz=fbounded;
    // Retained empirical engineering allowance; not a numerical or physical bound.
    out.uncertainty=3*std::sqrt(fthermal+fprocess)+fbounded+.005;
    out.slope_uncertainty=3*std::sqrt(sthermal+sprocess)+sbounded;
    out.seconds=now;out.support=span;out.first=p.front().first;out.end=p.back().end;out.count=p.size();
    out.valid=std::isfinite(out.hz)&&std::isfinite(out.uncertainty)&&out.hz>=200&&out.hz<=cfg.rate/2.-200&&out.chi<=4*p.size()&&
        std::abs(out.slope)<=cfg.supported_slew_hz_per_second+out.slope_uncertainty;
    return out;
}
// Isolated continuous-pilot engineering uncertainty, not a calibrated confidence
// interval. White-noise phase linearization and supported local Brownian/linear
// drift assumptions are tested against truth by the private discriminator.
struct Reference {bool valid=false;std::uint64_t sample=0,captured_end=0;double hz=0,uncertainty=0,noise_psd=0,sample_fraction=0;};
struct BlockResult {
    std::uint64_t first=0,end=0,reference_sample=0,reference_captured_end=0;
    double seconds=0,cn0=0,ratio=0,phase=0,beta=0,residual_bound=0;
    double nco_hz=0,nco_start_cycles=0,nco_mid_cycles=0,reference_age=0,reference_fraction=0;
    bool accepted=false,steering=false,bridge_tested=false;Estimate estimate,past;
    double coarse_phase=0,coarse_hz=0,coarse_R=0,coarse_beta=0,coarse_ratio=0,coarse_nco_start=0,coarse_nco_mid=0;
    double fine_lift=0,coarse_lift=0,branch_difference=0,branch_bound=0;
};
class Tracker {
public:
    Config config;double seed_hz=0,seed_uncertainty=0,noise_psd=0;
    std::uint64_t origin=0,next=0,block_first=0;std::size_t size=0,used=0;
    std::array<Point,capacity> points{};std::size_t point_count=0,ambiguities=0,low_level=0,unsupported=0,steered_blocks=0,disjoint_resets=0,branch_disagreements=0;
    std::complex<double> oscillator{1,0},increment{1,0},sum{};
    std::complex<double> coarse_oscillator{1,0},coarse_increment{1,0},coarse_sum{};
    double residual_bound=0,nco_hz=0,nco_residual_cycles=0,block_residual_cycles=0;
    double coarse_hz=0,coarse_R=0,coarse_residual_cycles=0,block_coarse_residual=0;bool block_steering=false;Estimate block_past;
    Reference reference{},block_reference{};bool block_domain=false;Estimate result;BlockResult block;
    Tracker(Config c,double seed,double uncertainty,double psd,std::uint64_t first):config(c),seed_hz(seed),seed_uncertainty(uncertainty),noise_psd(psd),origin(first),next(first),block_first(first),nco_hz(seed) {
        if(c.rate<1000 || c.rate>192000 || !(c.phase_sigma>=0) || !std::isfinite(c.phase_sigma) ||
           !(c.supported_slew_hz_per_second>=0) || !std::isfinite(c.supported_slew_hz_per_second) ||
           !(c.maximum_reference_age>0) || !std::isfinite(c.maximum_reference_age) ||
           !(seed>=200 && seed<=c.rate/2.-200) || !(uncertainty>=0) || !std::isfinite(uncertainty) ||
           !(psd>0) || !std::isfinite(psd))throw std::runtime_error("invalid continuous-pilot configuration");
        size=std::max<std::size_t>(1,std::llround(config.rate*period));
    }
    void observe_reference(Reference r) {
        if(!r.valid || !std::isfinite(r.hz) || !(r.hz>=200 && r.hz<=config.rate/2.-200) || !std::isfinite(r.noise_psd) || r.captured_end>next || r.sample>=r.captured_end || !(r.sample_fraction>=0 && r.sample_fraction<1) || !(r.uncertainty>=0) || !std::isfinite(r.uncertainty) || !(r.noise_psd>0))
            throw std::runtime_error("invalid or future independent pilot reference");
        if(reference.valid && r.sample<reference.sample)throw std::runtime_error("independent pilot date regressed");
        reference=r;
    }
    template<class Callback>void push(std::span<const float> data,std::uint64_t first,Callback&& callback){
        if(first!=next || data.size()>std::numeric_limits<std::uint64_t>::max()-next)
            throw std::runtime_error("pilot reference lost sample/date continuity or overflowed");
        for(const auto value:data){
            if(!used)begin_block();
            sum+=static_cast<double>(value)*oscillator;oscillator*=increment;
            coarse_sum+=static_cast<double>(value)*coarse_oscillator;coarse_oscillator*=coarse_increment;++used;++next;
            if(used==size){finish();callback(block);sum=coarse_sum={};used=0;block_first=next;
                nco_residual_cycles+=(nco_hz-seed_hz)*size/config.rate;
                coarse_residual_cycles+=(coarse_hz-seed_hz)*size/config.rate;
                coarse_oscillator/=std::max(std::abs(coarse_oscillator),1e-30);
                oscillator/=std::max(std::abs(oscillator),1e-30);
            }
        }
    }
private:
    void begin_block(){
        if(size>std::numeric_limits<std::uint64_t>::max()-block_first)
            throw std::runtime_error("pilot aperture date overflow");
        // The independent coarse NCO uses a previously captured FFT only.
        // Eligible completed past-fit state may steer the second sum; no
        // observation from this new aperture chooses its own NCO/domain.
        block_reference=reference;block_domain=reference.valid && reference.captured_end<=block_first;
        const auto age=block_domain?(static_cast<double>(block_first-reference.sample)-reference.sample_fraction)/config.rate:999.;
        block_domain=block_domain && age>=0 && age<=config.maximum_reference_age;
        block_steering=false;block_past={};
        if(block_domain){coarse_hz=reference.hz;noise_psd=reference.noise_psd;
            coarse_R=reference.uncertainty+config.supported_slew_hz_per_second*(age+period);
            nco_hz=coarse_hz;residual_bound=coarse_R;
            const auto block_end=elapsed(block_first+size,origin,config.rate);
            if(result.valid && result.end<=block_first && result.seconds<=elapsed(block_first,origin,config.rate)){
                const auto past_R=result.uncertainty+config.supported_slew_hz_per_second*(block_end-result.seconds);
                if(std::abs(result.hz-coarse_hz)>past_R+coarse_R){
                    // Contrary independent current evidence withdraws the old
                    // chain; no invalid/overlapping context can republish it.
                    point_count=0;result.valid=false;++disjoint_resets;
                }else if(past_R<coarse_R){
                    block_past=result;block_steering=true;++steered_blocks;
                    nco_hz=block_past.hz;residual_bound=past_R;
                }
            }
        }else residual_bound=coarse_R=std::numeric_limits<double>::infinity();
        block_residual_cycles=nco_residual_cycles;increment=std::polar(1.,-tau*nco_hz/config.rate);
        block_coarse_residual=coarse_residual_cycles;coarse_increment=std::polar(1.,-tau*coarse_hz/config.rate);
    }

    void finish(){
        block={};block.first=block_first;block.end=next;block.seconds=(static_cast<double>(block_first-origin)+.5*(size-1))/config.rate;
        block.residual_bound=residual_bound;block.nco_hz=nco_hz;
        block.reference_sample=block_reference.sample;block.reference_captured_end=block_reference.captured_end;
        block.reference_age=(static_cast<double>(block_first-block_reference.sample)-block_reference.sample_fraction)/config.rate;block.reference_fraction=block_reference.sample_fraction;
        block.nco_start_cycles=seed_hz*static_cast<double>(block_first-origin)/config.rate+block_residual_cycles;
        block.nco_mid_cycles=seed_hz*block.seconds+block_residual_cycles+(nco_hz-seed_hz)*.5*(size-1)/config.rate;
        block.steering=block_steering;block.past=block_past;block.coarse_hz=coarse_hz;block.coarse_R=coarse_R;
        block.coarse_nco_start=seed_hz*elapsed(block_first,origin,config.rate)+block_coarse_residual;
        block.coarse_nco_mid=seed_hz*block.seconds+block_coarse_residual+(coarse_hz-seed_hz)*.5*(size-1)/config.rate;
        const auto z=sum/static_cast<double>(size),cz=coarse_sum/static_cast<double>(size);
        const auto variance=noise_psd*config.rate/(2*size);
        const auto power=std::max(0.,std::norm(z)-variance),coarse_power=std::max(0.,std::norm(cz)-variance);
        block.ratio=variance>0?power/variance:0;block.coarse_ratio=variance>0?coarse_power/variance:0;
        block.cn0=power>0&&noise_psd>0?10*std::log10(2*power/noise_psd):-999;
        const auto b=bound(config,residual_bound,nco_hz),cb=bound(config,coarse_R,coarse_hz);block.beta=b.radians;block.coarse_beta=cb.radians;
        if(!block_domain || !b.valid || !cb.valid){++unsupported;result.valid=false;return;}
        if(block.ratio<16 || block.coarse_ratio<16){++low_level;result.valid=false;return;}
        Point q;q.seconds=block.seconds;
        q.phase=std::arg(z)/tau+block_residual_cycles+(nco_hz-seed_hz)*.5*(size-1)/config.rate;
        q.coarse_phase=std::arg(cz)/tau+block_coarse_residual+(coarse_hz-seed_hz)*.5*(size-1)/config.rate;
        q.first=block_first;q.end=next;
        q.thermal=variance/(2*power*tau*tau);q.aperture_bound=b.radians/tau;
        q.window_process=2*config.phase_sigma*config.phase_sigma*period/(tau*tau*b.minimum_mean_amplitude*b.minimum_mean_amplitude);
        q.coarse_thermal=variance/(2*coarse_power*tau*tau);q.coarse_bound=cb.radians/tau;
        q.coarse_window_process=2*config.phase_sigma*config.phase_sigma*period/(tau*tau*cb.minimum_mean_amplitude*cb.minimum_mean_amplitude);
        if(point_count){block.bridge_tested=true;const auto& previous=points[point_count-1];const auto dt=q.seconds-previous.seconds;
            // Coarse branch validation is UNCHANGED and independent of the
            // narrower past-state steering interval. Both physical sums are
            // current400ms data with exact known NCO phases undone.
            const auto predicted=(block_reference.hz-seed_hz)*dt;
            const auto reference_date=elapsed(block_reference.sample,origin,config.rate)+block_reference.sample_fraction/config.rate;
            const auto maximum_age=std::max(std::abs(q.seconds-reference_date),std::abs(previous.seconds-reference_date));
            const auto gate=(block_reference.uncertainty+config.supported_slew_hz_per_second*maximum_age)*dt+
                q.coarse_bound+previous.coarse_bound+
                3*std::sqrt(q.coarse_thermal+previous.coarse_thermal+q.coarse_window_process+previous.coarse_window_process+2*config.phase_sigma*config.phase_sigma*dt/(tau*tau));
            const auto raw=q.coarse_phase-std::remainder(previous.coarse_phase,1.);const auto coarse_lift=raw+std::round(predicted-raw);
            const auto fine_raw=q.phase-std::remainder(previous.phase,1.);const auto fine_lift=fine_raw+std::round(predicted-fine_raw);
            const auto difference=std::abs(fine_lift-coarse_lift);
            // The sums share PCM. Minkowski retains arbitrary covariance;
            // common underlying Brownian point phases cancel in this contrast.
            const auto difference_bound=q.aperture_bound+previous.aperture_bound+q.coarse_bound+previous.coarse_bound+
                3*(std::sqrt(q.thermal)+std::sqrt(previous.thermal)+std::sqrt(q.coarse_thermal)+std::sqrt(previous.coarse_thermal)+
                   std::sqrt(q.window_process)+std::sqrt(previous.window_process)+std::sqrt(q.coarse_window_process)+std::sqrt(previous.coarse_window_process));
            block.fine_lift=fine_lift;block.coarse_lift=coarse_lift;block.branch_difference=difference;block.branch_bound=difference_bound;
            const bool coarse_ok=dt<=1 && gate<.45 && std::abs(coarse_lift-predicted)<=gate;
            const bool same_branch=std::abs(fine_lift-predicted)<=gate && difference<=std::min(.45,difference_bound);
            if(!coarse_ok || !same_branch){point_count=0;++ambiguities;branch_disagreements+=coarse_ok&&!same_branch;result.valid=false;}
            else {q.phase=previous.phase+fine_lift;q.coarse_phase=previous.coarse_phase+coarse_lift;}
        }
        block.coarse_phase=q.coarse_phase;
        std::size_t keep=0;for(std::size_t i=0;i<point_count;++i)if(q.seconds-points[i].seconds<=36)points[keep++]=points[i];point_count=keep;
        if(point_count==points.size()){std::move(points.begin()+1,points.end(),points.begin());--point_count;}
        points[point_count++]=q;result=fit(std::span(points).first(point_count),seed_hz,config);
        block.accepted=true;block.phase=q.phase;block.estimate=result;
    }
};
}
