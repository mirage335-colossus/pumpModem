#include "datapump/helper_receiver.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <utility>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr std::size_t vertices=9;
constexpr std::size_t narrow_bins=72;
using Pair=std::array<double,2>;
struct Block { double xx=0,xy=0,yy=0; };
struct Tri { std::array<Block,vertices> diagonal{};std::array<Block,vertices-1> upper{}; };
struct Cross { std::array<Block,vertices> diagonal{};std::array<Block,vertices-1> upper{},lower{}; };
struct Matrix { double a=0,b=0,c=0,d=0; };
Matrix matrix(Block b) {return {b.xx,b.xy,b.xy,b.yy};}
Matrix operator-(Matrix x,Matrix y) {return {x.a-y.a,x.b-y.b,x.c-y.c,x.d-y.d};}
Matrix operator*(Matrix x,Matrix y) {
    return {x.a*y.a+x.b*y.c,x.a*y.b+x.b*y.d,x.c*y.a+x.d*y.c,x.c*y.b+x.d*y.d};
}
Pair operator*(Matrix m,Pair x) {return {m.a*x[0]+m.b*x[1],m.c*x[0]+m.d*x[1]};}
Pair operator-(Pair x,Pair y) {return {x[0]-y[0],x[1]-y[1]};}
bool inverse(Matrix m,Matrix& out) {
    const auto det=m.a*m.d-m.b*m.c;
    if(!std::isfinite(det)||det<=1e-18)return false;
    out={m.d/det,-m.b/det,-m.c/det,m.a/det};return true;
}
Block add(Block a,Block b,double scale=1) {return {a.xx+scale*b.xx,a.xy+scale*b.xy,a.yy+scale*b.yy};}
Block outer(Pair p,double weight) {return {weight*p[0]*p[0],weight*p[0]*p[1],weight*p[1]*p[1]};}
double dot(Pair a,Pair b) {return a[0]*b[0]+a[1]*b[1];}
double phase_wrap(double x) {return std::remainder(x,tau);}
double smooth(double x) {x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));}
double smooth_derivative(double x) {return x<=0||x>=1?0:30*x*x*(1-x)*(1-x);}
bool solve3(std::array<std::array<double,3>,3> a,std::array<double,3> b,
            std::array<double,3>& x) {
    for(unsigned i=0;i<3;++i) {
        unsigned pivot=i;
        for(unsigned j=i+1;j<3;++j)if(std::abs(a[j][i])>std::abs(a[pivot][i]))pivot=j;
        if(std::abs(a[pivot][i])<1e-18)return false;
        std::swap(a[i],a[pivot]);std::swap(b[i],b[pivot]);
        for(unsigned j=i+1;j<3;++j) {
            const auto f=a[j][i]/a[i][i];
            for(unsigned k=i+1;k<3;++k)a[j][k]-=f*a[i][k];
            b[j]-=f*b[i];
        }
    }
    for(int i=2;i>=0;--i) {
        auto value=b[static_cast<unsigned>(i)];
        for(unsigned j=static_cast<unsigned>(i)+1;j<3;++j)value-=a[static_cast<unsigned>(i)][j]*x[j];
        x[static_cast<unsigned>(i)]=value/a[static_cast<unsigned>(i)][static_cast<unsigned>(i)];
    }
    return true;
}
bool solve6(std::array<std::array<double,6>,6> a,std::array<double,6> b,
            std::array<double,6>& x) {
    for(unsigned i=0;i<6;++i) {
        unsigned pivot=i;
        for(unsigned j=i+1;j<6;++j)if(std::abs(a[j][i])>std::abs(a[pivot][i]))pivot=j;
        if(std::abs(a[pivot][i])<1e-14)return false;
        std::swap(a[i],a[pivot]);std::swap(b[i],b[pivot]);
        for(unsigned j=i+1;j<6;++j) {
            const auto f=a[j][i]/a[i][i];
            for(unsigned k=i+1;k<6;++k)a[j][k]-=f*a[i][k];
            b[j]-=f*b[i];
        }
    }
    for(int i=5;i>=0;--i) {
        auto v=b[static_cast<unsigned>(i)];
        for(unsigned j=static_cast<unsigned>(i)+1;j<6;++j)v-=a[static_cast<unsigned>(i)][j]*x[j];
        x[static_cast<unsigned>(i)]=v/a[static_cast<unsigned>(i)][static_cast<unsigned>(i)];
    }
    return true;
}
}
struct Receiver::Impl {
    struct Bin {std::complex<double> z{};std::uint64_t first=0;double noise_energy=0;};
    struct FrequencyPoint {
        double time=0,phase=0,frequency=0;
        float phase_variance=0,frequency_variance=0,phase_bias=0,frequency_bias=0;
    };
    struct NarrowFit {Pair amplitude{};Block gram{};double score=0,energy=0;};
    struct State {
        std::uint32_t rate=0;std::uint64_t next=0;
        std::size_t bin_size=0,bin_count=0,ring_count=0,ring_position=0;
        std::uint64_t bin_first=0;
        std::complex<double> bin_osc{1,0},bin_step{},bin_sum{};
        double bin_cc=0,bin_ss=0,bin_cs=0,coarse_hz=0;
        // The physical search grid remains centered on immutable expected.
        // A basis chosen from past PCM is used only for later complete bins.
        double basis_hz=0,pending_basis_hz=0;
        std::uint64_t last_basis_sample=0;
        std::array<Bin,narrow_bins> ring{};
        std::array<FrequencyPoint,12> frequency_points{};
        // Fixed-grid Fisher evidence uses disjoint raw windows. It can qualify
        // a weak beacon without demanding significance in one 144 ms window.
        std::array<double,81> group_evidence{};
        std::uint64_t group_last_end=0,completed_groups=0,group_admitted_sample=0;
        std::size_t group_windows=0;
        double group_frequency=0;
        std::size_t frequency_count=0,frequency_position=0,frequency_tick=0;
        std::uint64_t frequency_groups=0,last_frequency_end=0;
        double bin_energy=0;
        std::uint64_t frequency_sample=0;
        double frequency_value=0,frequency_uncertainty=0;
        bool coarse_valid=false;
        std::complex<double> previous_bin{};
        std::complex<double> increment_sum{};std::size_t increments=0;
        double guess_hz=0;
        std::size_t fit_count=0,fit_size=0;
        std::uint64_t fit_first=0;
        std::complex<double> fit_osc{1,0},fit_step{};
        std::array<std::array<double,6>,6> fit_gram{};
        std::array<double,6> fit_rhs{};
        double fit_energy=0,fit_hz=0;
        std::optional<Reference> candidate,reference,update;
        double amplitude=0;
        double punctuation=-1,punctuation_uncertainty=0,anchor=0,timing_uncertainty=0;
        double boundary_notch=-1,boundary_level=1;
        bool timing_valid=false;
        std::uint64_t punctuation_count=0;
        double metadata_dwell=0,metadata_last_dwell=-1;
        bool metadata_active=false;
        std::complex<double> metadata_osc{1,0},metadata_step{};
        Tri bb{},dd{};Cross bd{};
        std::array<Pair,vertices> rhs{},rhs_derivative{};
        double metadata_energy=0,metadata_gain=0;
        std::size_t metadata_samples=0;
    } state;
    double expected,width;
    std::vector<Crypto> keys;
    explicit Impl(double e,double w,std::vector<Crypto> k):expected(e),width(w),keys(std::move(k)) {}

    void configure(std::uint32_t rate,std::uint64_t first) {
        state={};auto& s=state;
        if(rate<1000||expected-width<10||expected+width>=rate/2.-10)
            throw Error("Helper Carrier precision band is outside capture passband");
        s.rate=rate;s.next=first;s.bin_first=first;
        s.bin_size=std::max<std::size_t>(2,rate/500);
        s.fit_size=std::max<std::size_t>(16,rate/25);
        s.bin_step=std::polar(1.,-tau*expected/rate);
        s.guess_hz=expected;s.coarse_hz=expected;s.basis_hz=expected;
    }
    double seconds(std::uint64_t sample) const {return static_cast<double>(sample)/state.rate;}
    double local(double time) const {
        auto t=std::fmod(time-state.anchor,.4);return t<0?t+.4:t;
    }
    bool tone_time(double time) const {
        if(!state.timing_valid)return true;
        const auto t=local(time);return (t>=.008&&t<.212)||(t>=.268&&t<.308);
    }
    Reference fuse_frequency(Reference reference) const {
        const auto& s=state;
        if(s.frequency_sample&&s.frequency_sample<=reference.sample&&
           reference.sample-s.frequency_sample<=s.rate&&s.frequency_uncertainty>0&&
           s.frequency_uncertainty<reference.frequency_uncertainty_hz&&
           std::abs(s.frequency_value-reference.frequency_hz)<=
               s.frequency_uncertainty+reference.frequency_uncertainty_hz) {
            reference.frequency_hz=s.frequency_value;
            reference.frequency_uncertainty_hz=s.frequency_uncertainty;
            reference.frequency_sample=s.frequency_sample;
        }
        return reference;
    }
    void finish_fit() {
        auto& s=state;
        std::array<double,6> x{};
        if(!solve6(s.fit_gram,s.fit_rhs,x))return;
        double explained=0;for(unsigned i=0;i<6;++i)explained+=x[i]*s.fit_rhs[i];
        const auto residual=std::max(0.,s.fit_energy-explained);
        const auto amplitude=std::hypot(x[0],x[1]);
        if(!(amplitude>1e-9)||s.fit_energy<=0||explained<s.fit_energy*.90)return;
        const auto duration=static_cast<double>(s.fit_size)/s.rate;
        const std::complex<double> c0{x[0],-x[1]},c1{x[2],-x[3]},c2{x[4],-x[5]};
        const auto correction=std::imag(c1*std::conj(c0))/std::norm(c0);
        // The quadratic envelope must agree with a slowly rotating phasor,
        // rather than admitting an arbitrary smooth burst as a reference.
        const auto curvature_error=std::abs(c2+.5*c0*correction*correction)/amplitude;
        if(std::abs(correction)>.12||curvature_error>.01)return;
        Reference r;
        r.sample=s.fit_first+s.fit_size-1;r.sample_rate=s.rate;
        const auto u=static_cast<double>(s.fit_size-1)/s.fit_size-.5;
        const auto c=c0+u*c1+u*u*c2,derivative=c1+2*u*c2;
        const auto frontier_amplitude=std::abs(c);
        if(frontier_amplitude<amplitude*.98||frontier_amplitude>amplitude*1.02)return;
        const auto frequency=s.fit_hz+std::imag(derivative*std::conj(c))/
            (std::norm(c)*tau*duration);
        if(!std::isfinite(frequency)||std::abs(frequency-expected)>width+1e-6)return;
        r.frequency_hz=frequency;
        auto phase=std::arg(c)+tau*s.fit_hz*static_cast<double>(s.fit_size-1)/s.rate;
        if(s.timing_valid&&local(seconds(r.sample))>=.244)phase-=std::numbers::pi;
        r.phase_radians=phase_wrap(phase);
        const auto noise=std::sqrt(residual/std::max<double>(1,s.fit_size-6));
        // Endpoint evaluation has greater variance than a center phasor. Use
        // the actual finite-window six-column Gram covariance, including the
        // quadratic envelope and noninteger carrier cycles.
        std::array<double,6> phase_gradient{},frequency_gradient{},covariance{};
        const auto norm=std::norm(c),numerator=std::imag(derivative*std::conj(c));
        for(unsigned j=0;j<6;++j) {
            const auto power=j/2;
            const auto q=power==0?1.:power==1?u:u*u;
            const auto dq=power==0?0.:power==1?1.:2*u;
            const auto direction=j%2?std::complex<double>{0,-1}:std::complex<double>{1,0};
            const auto dc=q*direction,dd=dq*direction;
            phase_gradient[j]=std::imag(dc*std::conj(c))/norm;
            frequency_gradient[j]=(std::imag(dd*std::conj(c)+derivative*std::conj(dc))/norm-
                numerator*2*std::real(dc*std::conj(c))/(norm*norm))/(tau*duration);
        }
        if(!solve6(s.fit_gram,phase_gradient,covariance))return;
        double phase_variance=0;
        for(unsigned j=0;j<6;++j)phase_variance+=phase_gradient[j]*covariance[j];
        if(!solve6(s.fit_gram,frequency_gradient,covariance))return;
        double frequency_variance=0;
        for(unsigned j=0;j<6;++j)frequency_variance+=frequency_gradient[j]*covariance[j];
        if(!std::isfinite(phase_variance)||!std::isfinite(frequency_variance)||
           phase_variance<=0||frequency_variance<=0)return;
        const auto phase_noise=std::max(2e-6,8*noise*std::sqrt(phase_variance));
        const auto frequency_noise=8*noise*std::sqrt(frequency_variance);
        const auto radial=std::real(c1*std::conj(c0))/std::norm(c0);
        const auto curvature=2*std::imag(c2*std::conj(c0))/std::norm(c0)-2*radial*correction;
        // A downstream constant-frequency reference omits measured phase
        // acceleration. Include its half-window frequency span and linear
        // phase remainder. The fitted quadratic phase's third derivative is
        // bounded by M^3+3*M*|curvature|, M=|slope|+|curvature|/2. Endpoint
        // polynomial projection/remainder bounds supply the remaining terms;
        // deterministic model error must not be divided by sqrt(sample count).
        const auto slope_bound=std::abs(correction)+.5*std::abs(curvature);
        const auto third=slope_bound*slope_bound*slope_bound+3*slope_bound*std::abs(curvature);
        r.phase_uncertainty_radians=phase_noise+std::abs(curvature)/8+third/4;
        r.frequency_uncertainty_hz=std::max(1e-5,frequency_noise)+
            (std::abs(curvature)/2+third)/(tau*duration);
        r.frequency_valid=r.phase_valid=true;
        r.timing_valid=s.timing_valid;r.dwell_uncertainty_seconds=s.timing_uncertainty;
        r.dwell_start_seconds=seconds(r.sample)-local(seconds(r.sample));
        if(s.candidate) {
            const auto dt=static_cast<double>(r.sample-s.candidate->sample)/s.rate;
            auto error=phase_wrap(r.phase_radians-s.candidate->phase_radians-tau*s.candidate->frequency_hz*dt);
            // Prior to timing acquisition an observed reversal may change the
            // helper sign. It can never establish a payload phase convention.
            if(!s.timing_valid&&std::abs(std::abs(error)-std::numbers::pi)<.03)
                error=phase_wrap(error-std::numbers::pi);
            const auto allowance=std::max(.01,6*(r.phase_uncertainty_radians+
                s.candidate->phase_uncertainty_radians+tau*dt*s.candidate->frequency_uncertainty_hz));
            if(std::abs(error)<allowance&&std::abs(r.frequency_hz-s.candidate->frequency_hz)<
               std::max(.05,6*(r.frequency_uncertainty_hz+s.candidate->frequency_uncertainty_hz))) {
                s.reference=r;
                if(!s.update||!s.update->metadata)s.update=fuse_frequency(r);
                s.amplitude=frontier_amplitude;s.guess_hz=frequency;
            }
        }
        s.candidate=r;
    }
    void fit_sample(float sample,std::uint64_t index) {
        auto& s=state;
        if(!tone_time(seconds(index))) {s.fit_count=0;return;}
        if(!s.fit_count) {
            s.fit_first=index;s.fit_gram={};s.fit_rhs={};s.fit_energy=0;
            s.fit_hz=s.guess_hz;s.fit_osc={1,0};s.fit_step=std::polar(1.,tau*s.fit_hz/s.rate);
        }
        const auto u=static_cast<double>(s.fit_count)/s.fit_size-.5;
        const auto c=s.fit_osc.real(),v=s.fit_osc.imag();
        const std::array<double,6> basis{c,v,u*c,u*v,u*u*c,u*u*v};
        for(unsigned i=0;i<6;++i) {
            s.fit_rhs[i]+=sample*basis[i];
            for(unsigned j=i;j<6;++j)s.fit_gram[i][j]+=basis[i]*basis[j];
        }
        s.fit_energy+=static_cast<double>(sample)*sample;s.fit_osc*=s.fit_step;
        if(++s.fit_count==s.fit_size) {
            for(unsigned i=0;i<6;++i)for(unsigned j=0;j<i;++j)s.fit_gram[i][j]=s.fit_gram[j][i];
            finish_fit();s.fit_count=0;
        }
    }
    const Bin& bin(std::size_t age) const {
        const auto& s=state;return s.ring[(s.ring_position+s.ring.size()-1-age)%s.ring.size()];
    }
    std::complex<double> aligned(const Bin& b,double center) const {
        const auto t=(static_cast<double>(b.first)+.5*state.bin_size)/state.rate;
        return b.z*std::polar(1.,-tau*(state.guess_hz-state.basis_hz)*(t-center));
    }
    void find_punctuation() {
        auto& s=state;if(s.ring_count<32||!s.coarse_valid)return;
        const auto center=(static_cast<double>(bin(13).first)+.5*s.bin_size)/s.rate;
        if(s.punctuation>=0&&center-s.punctuation<.1)return;
        std::complex<double> left{},right{};double left_power=0,right_power=0;
        for(std::size_t age=18;age<=28;++age) {const auto z=aligned(bin(age),center);left+=z;left_power+=std::norm(z);}
        for(std::size_t age=0;age<=10;++age) {const auto z=aligned(bin(age),center);right+=z;right_power+=std::norm(z);}
        left/=11;right/=11;
        if(std::norm(left)<1e-12||std::norm(right)<1e-12||
           11*std::norm(left)<.998*left_power||11*std::norm(right)<.998*right_power||
           std::abs(std::abs(right)/std::abs(left)-1)>.03||std::abs(phase_wrap(std::arg(-right/left)))>.015)return;
        const auto gain=(left-right)*.5;
        // A changing real-tone envelope also changes the carrier image in
        // each finite bin. Project the candidate reversal through the same
        // sampled carrier/Gram model as finish_bin; a scalar envelope average
        // biases timing with carrier phase and can corrupt dense metadata.
        struct ReversalBin {
            double begin;
            std::complex<double> carrier,alignment,observed;
        };
        std::array<ReversalBin,8> reversal_bins;
        const auto difference=s.guess_hz-s.basis_hz;
        const auto basis_step=std::polar(1.,tau*s.basis_hz/s.rate);
        const auto carrier_step=std::polar(1.,tau*s.guess_hz/s.rate);
        const auto sample_seconds=1./s.rate;
        const auto envelope_step=sample_seconds/.008;
        // The s.basis_hz-carrier oscillator restarts in every bin, so all
        // completed bins have this same actual finite Gram matrix.
        const auto determinant=s.bin_cc*s.bin_ss-s.bin_cs*s.bin_cs;
        if(determinant<=1e-12)return;
        for(std::size_t i=0;i<reversal_bins.size();++i) {
            const auto& b=bin(i+10);
            const auto begin=seconds(b.first);
            const auto cycles=std::fmod(static_cast<long double>(s.basis_hz)*b.first/s.rate,1.L);
            const auto phase=std::polar(1.,tau*static_cast<double>(cycles));
            reversal_bins[i]={begin,
                gain*phase*std::polar(1.,tau*difference*(begin-center)),
                std::conj(phase)*std::polar(1.,-tau*difference*(begin+.5*static_cast<double>(s.bin_size)/s.rate-center)),
                aligned(b,center)};
        }
        const auto objective=[&](double crossing) {
            double error=0;
            for(const auto& b:reversal_bins) {
                auto carrier=b.carrier,basis=std::complex<double>{1,0};
                auto position=(b.begin-crossing+.004)/.008;
                double cosine_rhs=0,sine_rhs=0;
                for(std::size_t j=0;j<s.bin_size;++j) {
                    const auto sample=carrier.real()*(1-2*smooth(position));
                    cosine_rhs+=sample*basis.real();sine_rhs+=sample*basis.imag();
                    carrier*=carrier_step;basis*=basis_step;position+=envelope_step;
                }
                const std::complex<double> projected{
                    (s.bin_ss*cosine_rhs-s.bin_cs*sine_rhs)/determinant,
                    -(s.bin_cc*sine_rhs-s.bin_cs*cosine_rhs)/determinant};
                error+=std::norm(b.observed-projected*b.alignment);
            }
            return error/std::norm(gain);
        };
        double lo=center-.004,hi=center+.004;
        for(unsigned i=0;i<28;++i) {
            const auto a=lo+(hi-lo)/3,b=hi-(hi-lo)/3;
            if(objective(a)<objective(b))hi=b;else lo=a;
        }
        const auto crossing=.5*(lo+hi),error=objective(crossing);
        if(error>2e-4)return;
        const auto uncertainty=std::max(1./s.rate,.002*std::sqrt(error));
        if(s.punctuation>=0) {
            const auto intervals=std::round((crossing-s.punctuation)/.4);
            if(intervals>=1&&intervals<=3&&std::abs(crossing-s.punctuation-.4*intervals)<.0005&&
               std::abs(s.boundary_notch-(s.punctuation+.16))<.004) {
                s.timing_valid=true;s.anchor=crossing-.24;
                s.timing_uncertainty=std::max(uncertainty,std::abs(crossing-s.punctuation-.4*intervals));
                ++s.punctuation_count;
            } else {s.timing_valid=false;s.punctuation_count=1;}
        } else s.punctuation_count=1;
        s.punctuation=crossing;s.punctuation_uncertainty=uncertainty;
        s.boundary_notch=-1;s.boundary_level=1;
        // The preceding accepted candidate is before the reversal. Keeping it
        // allows the short post-reversal tone to validate the canonical phase
        // without requiring two additional 40 ms fits before metadata.
    }
    template<class F> void narrow_samples(double frequency,F&& visit) const {
        const auto& s=state;
        const auto step_seconds=static_cast<double>(s.bin_size)/s.rate;
        const auto offset=-.5*(narrow_bins-1)*step_seconds;
        const auto difference=frequency-s.basis_hz;
        const auto cycles=std::fmod(static_cast<long double>(s.basis_hz)*bin(narrow_bins-1).first/s.rate,1.L);
        auto rotation=std::polar(1.,-tau*difference*offset);
        auto covariance_rotation=std::polar(1.,-tau*static_cast<double>(cycles)-tau*difference*offset);
        const auto rotation_step=std::polar(1.,-tau*difference*step_seconds);
        const auto covariance_step=std::polar(1.,-tau*frequency*step_seconds);
        for(std::size_t i=0;i<narrow_bins;++i) {
            const auto q=bin(narrow_bins-1-i).z*rotation;
            const auto c=covariance_rotation.real(),v=covariance_rotation.imag();
            const Block weight{s.bin_cc*c*c+s.bin_ss*v*v+2*s.bin_cs*c*v,
                (s.bin_cc-s.bin_ss)*c*v-s.bin_cs*(c*c-v*v),
                s.bin_cc*v*v+s.bin_ss*c*c-2*s.bin_cs*c*v};
            visit(Pair{q.real(),q.imag()},weight,offset+i*step_seconds);
            rotation*=rotation_step;covariance_rotation*=covariance_step;
        }
    }
    NarrowFit narrow_fit(double frequency) const {
        NarrowFit fit;Pair rhs{};
        narrow_samples(frequency,[&](Pair q,Block weight,double) {
            fit.gram=add(fit.gram,weight);
            const auto projected=matrix(weight)*q;
            rhs[0]+=projected[0];rhs[1]+=projected[1];fit.energy+=dot(q,projected);
        });
        Matrix covariance;
        if(!inverse(matrix(fit.gram),covariance))return fit;
        fit.amplitude=covariance*rhs;fit.score=std::max(0.,dot(fit.amplitude,rhs));return fit;
    }
    std::optional<Pair> long_frequency(double logarithmic_error) const {
        const auto& s=state;const auto count=s.frequency_count;
        if(count<4)return {};
        const auto point=[&](std::size_t i)->const FrequencyPoint& {
            return s.frequency_points[(s.frequency_position+s.frequency_points.size()-count+i)%s.frequency_points.size()];
        };
        const auto duration=point(count-1).time-point(0).time;
        if(duration<1)return {};
        double frequency_weight=0,frequency_sum=0,frequency_bias=0;
        for(std::size_t i=0;i<count;++i) {
            const auto& p=point(i);const auto weight=1./p.frequency_variance;
            frequency_weight+=weight;frequency_sum+=weight*p.frequency;
            frequency_bias=std::max<double>(frequency_bias,p.frequency_bias);
        }
        const auto z=std::max(8.,std::sqrt(2*logarithmic_error));
        const auto center=frequency_sum/frequency_weight;
        const auto radius=z/std::sqrt(frequency_weight)+frequency_bias;
        // Doubling phase removes each dwell's possible pi reversal. Fit its
        // free constant phase without selecting one unwrap/alias. For local
        // independent Gaussian phase errors, the weighted chord norm is at
        // most the chi-square phase-error radius plus model-bias radius.
        double wsum=0,weighted_time=0,bias_norm2=0;
        for(std::size_t i=0;i<count;++i) {
            const auto& p=point(i);const auto w=1./(4*p.phase_variance);
            wsum+=w;weighted_time+=w*p.time;
            bias_norm2+=p.phase_bias*p.phase_bias/p.phase_variance;
        }
        const auto center_time=weighted_time/wsum;
        // exp(-32) retains the eight-standard-error scale used by the
        // within-window prior. This remains a local noise/model interval.
        const auto level=std::max(32.,logarithmic_error);
        const auto k=static_cast<double>(count);
        const auto allowed=std::pow(std::sqrt(k+2*std::sqrt(k*level)+2*level)+std::sqrt(bias_norm2),2);
        double lipschitz=0;
        for(std::size_t i=0;i<count;++i) {
            const auto& p=point(i);
            lipschitz+=4*tau/(4*p.phase_variance)*std::abs(p.time-center_time);
        }
        // Bound CPU work. Each whole cell is excluded only by a global
        // Lipschitz lower bound; retaining all other cells covers aliases.
        // If the prior cannot be covered within this cap, keep it wide.
        const auto max_step=std::min(.0025,.02/duration);
        const auto cells=std::ceil(2*radius/max_step);
        if(!std::isfinite(cells)||cells<1||cells>4096)return {};
        const auto step=2*radius/cells;
        double union_low=std::numeric_limits<double>::infinity(),union_high=-union_low;
        for(unsigned cell=0;cell<static_cast<unsigned>(cells);++cell) {
            const auto low=center-radius+cell*step,high=low+step;
            const auto f=.5*(low+high);std::complex<double> resultant{};
            for(std::size_t i=0;i<count;++i) {
                const auto& p=point(i);const auto w=1./(4*p.phase_variance);
                resultant+=std::polar(w,2*p.phase-2*tau*(f-s.basis_hz)*(p.time-center_time));
            }
            const auto residual=std::max(0.,2*wsum-2*std::abs(resultant));
            const auto numerical=128*std::numeric_limits<double>::epsilon()*wsum;
            if(residual-lipschitz*step/2>allowed+numerical)continue;
            union_low=std::min(union_low,low);union_high=std::max(union_high,high);
        }
        if(!std::isfinite(union_low)||!std::isfinite(union_high))return {};
        return Pair{.5*(union_low+union_high),.5*(union_high-union_low)};
    }

    void finish_narrowband() {
        auto& s=state;
        if(s.ring_count<narrow_bins||++s.frequency_tick<20)return;
        s.frequency_tick=0;
        if(s.frequency_groups==std::numeric_limits<std::uint64_t>::max())return;
        ++s.frequency_groups;
        const auto frontier=bin(0).first+s.bin_size-1;
        const bool strong_reference=s.reference&&frontier-s.reference->sample<s.rate/2&&
           s.reference->frequency_uncertainty_hz<=.1;
        const auto eigenvalue=.5*(s.bin_cc+s.bin_ss-std::hypot(s.bin_cc-s.bin_ss,2*s.bin_cs));
        if(eigenvalue<s.bin_size*.2)return;
        double noise_energy=0;for(std::size_t i=0;i<narrow_bins;++i)noise_energy+=bin(i).noise_energy;
        const auto degrees=static_cast<double>(narrow_bins*(s.bin_size-2));
        if(degrees<=0||noise_energy<=0)return;
        const auto spending=std::log(static_cast<double>(s.frequency_groups))+
            std::log(static_cast<double>(s.frequency_groups)+1);
        const auto variance_log=std::log(200.)+spending;
        const auto variance_denominator=degrees-2*std::sqrt(degrees*variance_log);
        if(variance_denominator<=0)return;
        const auto variance_upper=noise_energy/variance_denominator;
        const auto cells=static_cast<unsigned>(std::ceil(width/2.5));
        const auto spacing=cells?width/cells:0;
        const auto bank_size=2*cells+1;
        const auto admission_log=std::log(400.)+std::log(static_cast<double>(bank_size))+spending;
        double frequency=expected,best_score=-1;
        const auto group_first=bin(narrow_bins-1).first;
        const auto independent=group_first>s.group_last_end;
        for(unsigned i=0;i<bank_size;++i) {
            const auto candidate=expected+(static_cast<double>(i)-cells)*spacing;
            const auto score=narrow_fit(candidate).score;
            if(independent)s.group_evidence[i]+=degrees*.5*std::log1p(score/noise_energy);
            if(score>best_score) {best_score=score;frequency=candidate;}
        }
        if(independent) {
            s.group_last_end=frontier;
            const auto target=4+static_cast<std::size_t>(std::min(28.,
                std::ceil(2*std::log1p(static_cast<double>(s.completed_groups)))));
            if(++s.group_windows>=target) {
                if(s.completed_groups!=UINT64_MAX) {
                    ++s.completed_groups;
                    const auto alpha=std::log(400.)+std::log(static_cast<double>(bank_size))+
                        std::log(static_cast<double>(s.completed_groups))+std::log(static_cast<double>(s.completed_groups)+1);
                    unsigned best=0;
                    for(unsigned i=1;i<bank_size;++i)if(s.group_evidence[i]>s.group_evidence[best])best=i;
                    const auto evidence=s.group_evidence[best];double log_sum=0,term=0;
                    if(evidence>0)for(std::size_t j=1;j<target;++j) {
                        term+=std::log(evidence)-std::log(static_cast<double>(j));
                        const auto high=std::max(log_sum,term),low=std::min(log_sum,term);
                        log_sum=high+std::log1p(std::exp(low-high));
                    }
                    if(evidence>0 && -evidence+log_sum<=-alpha) {
                        s.group_admitted_sample=frontier;
                        s.group_frequency=expected+(static_cast<double>(best)-cells)*spacing;
                    }
                }
                s.group_evidence={};s.group_windows=0;
            }
        }
        // Existence groups follow raw time even when a strong reference makes
        // another precision fit unnecessary. Data-dependent window selection
        // would invalidate the independent-window Fisher calibration.
        if(strong_reference)return;
        // Fixed-grid score is an exact two-dimensional whitened projection.
        // Its raw residual denominator is orthogonal under independent AWGN.
        // The spending union bound covers every grid cell and every look;
        // continuous local refinement below never grants fresh admission.
        const bool grouped=s.group_admitted_sample && frontier>=s.group_admitted_sample &&
            frontier-s.group_admitted_sample<=12ULL*s.rate &&
            std::abs(frequency-s.group_frequency)<=std::max(2.5,spacing);
        if(!grouped && -degrees*.5*std::log1p(best_score/noise_energy)>-admission_log)return;
        auto lower=std::max(expected-width,frequency-spacing),upper=std::min(expected+width,frequency+spacing);
        for(unsigned iteration=0;iteration<18&&upper>lower;++iteration) {
            const auto a=lower+(upper-lower)/3,b=upper-(upper-lower)/3;
            if(narrow_fit(a).score>narrow_fit(b).score)upper=b;else lower=a;
        }
        frequency=.5*(lower+upper);
        const auto fit=narrow_fit(frequency);
        const auto amplitude=std::hypot(fit.amplitude[0],fit.amplitude[1]);
        if(amplitude<=0)return;
        const auto residual=std::max(0.,fit.energy-fit.score);
        const auto narrow_degrees=static_cast<double>(2*narrow_bins-3);
        // Lack of fit is a fixed per-window CW consistency check. Reusing
        // lifetime admission spending here would make this acceptance gate
        // progressively looser and admit punctuation/modulation side peaks.
        const auto consistency_log=std::log(20.);
        if(residual>variance_upper*(narrow_degrees+2*std::sqrt(narrow_degrees*consistency_log)+2*consistency_log))return;
        std::array<std::array<double,3>,3> fisher{};
        fisher[0][0]=fit.gram.xx;fisher[0][1]=fisher[1][0]=fit.gram.xy;fisher[1][1]=fit.gram.yy;
        narrow_samples(frequency,[&](Pair,Block weight,double time) {
            const Pair derivative{-tau*time*fit.amplitude[1],tau*time*fit.amplitude[0]};
            const auto weighted=matrix(weight)*derivative;
            fisher[0][2]+=weighted[0];fisher[1][2]+=weighted[1];fisher[2][2]+=dot(derivative,weighted);
        });
        fisher[2][0]=fisher[0][2];fisher[2][1]=fisher[1][2];
        const std::array<double,3> amplitude_gradient{fit.amplitude[0]/amplitude,fit.amplitude[1]/amplitude,0};
        const std::array<double,3> phase_gradient{-fit.amplitude[1]/(amplitude*amplitude),fit.amplitude[0]/(amplitude*amplitude),0};
        std::array<double,3> covariance{};
        if(!solve3(fisher,amplitude_gradient,covariance))return;
        auto amplitude_variance=0.;for(unsigned i=0;i<3;++i)amplitude_variance+=amplitude_gradient[i]*covariance[i]*variance_upper;
        if(!solve3(fisher,phase_gradient,covariance))return;
        auto phase_variance=0.;for(unsigned i=0;i<3;++i)phase_variance+=phase_gradient[i]*covariance[i]*variance_upper;
        const auto phase_influence=covariance;
        if(!solve3(fisher,{0,0,1},covariance))return;
        const auto frequency_influence=covariance;
        const auto frequency_variance=covariance[2]*variance_upper;
        if(amplitude_variance<=0||phase_variance<=0||frequency_variance<=0)return;
        const auto z=std::max(8.,std::sqrt(2*admission_log));
        // The user's floor concerns the observed tone level, independently
        // of the fixed-grid lifetime existence test. Remove both quadrature
        // noise contributions (radial + A^2*tangential phase variance). This
        // is a locally noise-corrected observed level, not a lower confidence
        // bound on the true C/N0 or an unbiased selected-maximum estimator.
        const auto observed_amplitude_squared=std::max(0.,amplitude*amplitude-
            amplitude_variance-amplitude*amplitude*phase_variance);
        if(observed_amplitude_squared*s.rate/(4*variance_upper)<10*std::pow(10.,.6))return;
        // This completed, admitted window can choose a future LS basis, but
        // cannot publish precision from that adaptively chosen basis itself.
        // Keep the physical candidate grid and all confidence spending fixed.
        // A noise-scaled hysteresis and one-second cooldown prevent weak fits
        // from repeatedly discarding otherwise usable future observations.
        const auto recenter_distance=std::max(2.,4*z*std::sqrt(frequency_variance));
        if(std::abs(frequency-s.basis_hz)>recenter_distance &&
           (!s.last_basis_sample || frontier-s.last_basis_sample>=s.rate)) {
            s.pending_basis_hz=frequency;return;
        }
        double phase_gain=0,frequency_gain=0;
        narrow_samples(frequency,[&](Pair,Block weight,double time) {
            const Pair derivative{-tau*time*fit.amplitude[1],tau*time*fit.amplitude[0]};
            const auto sensitivity=[&](const std::array<double,3>& influence) {
                const auto value=matrix(weight)*Pair{influence[0]+influence[2]*derivative[0],
                    influence[1]+influence[2]*derivative[1]};
                return std::hypot(value[0],value[1]);
            };
            phase_gain+=sensitivity(phase_influence);frequency_gain+=sensitivity(frequency_influence);
        });
        const auto first=bin(narrow_bins-1).first;
        const auto start=local(seconds(first));
        const auto span=static_cast<double>(frontier-first)/s.rate;
        const bool straight=s.timing_valid && std::isfinite(s.timing_uncertainty) &&
            s.timing_uncertainty>=0 && start>=.008+s.timing_uncertainty &&
            start+span<=.212-s.timing_uncertainty;
        double frequency_bias=0,coefficient_error=0,phase_bias=0;
        if(!straight) {
            // Without a qualified dwell date this window can straddle a
            // reversal or metadata. Preserve the original conservative model;
            // centered-CW cancellation cannot justify a tighter phase history.
            const auto upper=amplitude+z*std::sqrt(amplitude_variance);
            phase_gain*=upper;frequency_gain*=upper;
            const auto error_per_hz=tau*s.bin_size/(2*s.rate)*std::sqrt(s.bin_size/eigenvalue);
            const auto feedback=frequency_gain*error_per_hz;
            if(feedback>=.5)return;
            frequency_bias=frequency_gain*error_per_hz*
                (std::abs(frequency-s.basis_hz)+z*std::sqrt(frequency_variance))/(1-feedback);
            coefficient_error=error_per_hz*(std::abs(frequency-s.basis_hz)+
                z*std::sqrt(frequency_variance)+frequency_bias);
            phase_bias=phase_gain*coefficient_error;
        } else {
        // The model bound below is relative to true amplitude. For e<=.25,
        // factor two also covers the 1/(1-e) coefficient-bias feedback.
        const auto amplitude_upper=2*(amplitude+z*std::sqrt(amplitude_variance));
        phase_gain*=amplitude_upper;frequency_gain*=amplitude_upper;
        // In a centered finite bin the first-order DC envelope term cancels.
        // Only its real-carrier image moment remains. Taylor's remainder is
        // bounded by dw^2 sum(t^2)/2, for arbitrary phase. Dividing both RHS
        // bounds by the ACTUAL Gram eigenvalue bounds coefficient error.
        const auto sine=std::abs(std::sin(tau*s.basis_hz/s.rate));
        if(!(sine>0))return;
        const auto n=static_cast<double>(s.bin_size),rate=static_cast<double>(s.rate);
        const auto moment=(n/sine+1/(sine*sine))/(2*rate);
        const auto second=n*(n*n-1)/(12*rate*rate);
        // Published bin dates use first+N/2 rather than first+(N-1)/2.
        // Keep the additional half-sample envelope phase error explicitly.
        const auto a=(tau*moment/(2*eigenvalue)+std::numbers::pi/rate)*(1+1e-12);
        const auto b=tau*tau*second/(2*eigenvalue)*(1+1e-12);
        const auto delta=std::abs(frequency-s.basis_hz)+z*std::sqrt(frequency_variance);
        const auto A=frequency_gain*b,B=1-frequency_gain*a-2*frequency_gain*b*delta;
        const auto C=frequency_gain*(a*delta+b*delta*delta+1e-12);
        const auto discriminant=B*B-4*A*C;
        if(!std::isfinite(discriminant)||!std::isfinite(B)||B<=0||discriminant<=0)return;
        // Small stable root of u=K e(delta+u). Reject an unstable feedback
        // model instead of shrinking its interval or choosing another alias.
        frequency_bias=2*C/(B+std::sqrt(discriminant));
        const auto extent=delta+frequency_bias;
        if(!std::isfinite(extent)||frequency_bias<0||frequency_gain*(a+2*b*extent)>=1-1e-12)return;
        coefficient_error=a*extent+b*extent*extent+1e-12;
        phase_bias=phase_gain*coefficient_error;
        }
        if(coefficient_error>.25||phase_bias>.5)return;
        if(s.frequency_count&&first<=s.last_frequency_end)return;
        const auto time=(static_cast<double>(first)+.5*narrow_bins*s.bin_size)/s.rate;
        if(s.frequency_count) {
            const auto& previous=s.frequency_points[(s.frequency_position+s.frequency_points.size()-1)%s.frequency_points.size()];
            if(time-previous.time>1.2) {s.frequency_count=0;s.frequency_position=0;}
        }
        s.frequency_points[s.frequency_position]={time,std::atan2(fit.amplitude[1],fit.amplitude[0]),frequency,
            static_cast<float>(phase_variance*(1+1e-6)),static_cast<float>(frequency_variance*(1+1e-6)),
            static_cast<float>(phase_bias*(1+1e-6)),static_cast<float>(frequency_bias*(1+1e-6))};
        s.frequency_position=(s.frequency_position+1)%s.frequency_points.size();
        s.frequency_count=std::min(s.frequency_count+1,s.frequency_points.size());s.last_frequency_end=frontier;
        Reference reference;reference.sample=frontier;reference.sample_rate=s.rate;
        reference.frequency_hz=frequency;reference.frequency_valid=true;
        reference.frequency_uncertainty_hz=z*std::sqrt(frequency_variance)+frequency_bias;
        reference.phase_uncertainty_radians=std::numbers::pi;reference.dwell_uncertainty_seconds=.4;
        if(const auto refined=long_frequency(admission_log)) {
            reference.frequency_hz=(*refined)[0];reference.frequency_uncertainty_hz=(*refined)[1];
        }
        reference.frequency_sample=frontier;
        s.frequency_sample=frontier;s.frequency_value=reference.frequency_hz;
        s.frequency_uncertainty=reference.frequency_uncertainty_hz;
        s.coarse_valid=true;s.coarse_hz=frequency;s.guess_hz=frequency;
        // A current qualified phase publication retains priority. Keep the
        // independent frequency component for the next measured strong phase,
        // rather than alternating phase qualification on every frequency fit.
        if(s.reference&&s.reference->phase_valid&&s.reference->timing_valid&&
           s.reference->phase_uncertainty_radians<=.25&&frontier-s.reference->sample<s.rate/2)return;
        if(!s.update||!s.update->metadata)s.update=reference;
    }
    void recenter_basis() {
        auto& s=state;if(!s.pending_basis_hz)return;
        // Called after every consumer has processed the completed bin's last
        // sample. New fits therefore use only future PCM, conditionally on
        // this past basis choice. Capture indices and alpha ordinals never
        // restart; discard partial evidence and every mixed-basis phase fit.
        s.basis_hz=s.pending_basis_hz;s.pending_basis_hz=0;s.last_basis_sample=s.next;
        s.bin_step=std::polar(1.,-tau*s.basis_hz/s.rate);s.bin_osc={1,0};
        s.ring={};s.ring_count=s.ring_position=0;
        s.frequency_points={};s.frequency_count=s.frequency_position=0;s.last_frequency_end=0;
        s.group_evidence={};s.group_windows=0;
        s.group_admitted_sample=0;s.group_frequency=0;
        // group_last_end, frequency_groups and completed_groups retain their
        // absolute raw-window dates and lifetime confidence-budget ordinals.
        s.previous_bin={};s.increment_sum={};s.increments=0;
        s.coarse_valid=true;s.coarse_hz=s.guess_hz=s.basis_hz;
        s.fit_count=0;s.candidate.reset();s.reference.reset();
        s.frequency_sample=0;s.frequency_value=s.frequency_uncertainty=0;
        s.timing_valid=false;s.punctuation=-1;s.boundary_notch=-1;s.boundary_level=1;
        s.amplitude=0;s.metadata_active=false;s.metadata_last_dwell=-1;
    }
    void finish_bin() {
        auto& s=state;
        const auto determinant=s.bin_cc*s.bin_ss-s.bin_cs*s.bin_cs;
        std::complex<double> z{};
        if(determinant>1e-12)z={(s.bin_ss*s.bin_sum.real()-s.bin_cs*(-s.bin_sum.imag()))/determinant,
            -(s.bin_cc*(-s.bin_sum.imag())-s.bin_cs*s.bin_sum.real())/determinant};
        // bin_sum uses exp(-i wt), hence its negative imaginary component is
        // the sine RHS; the returned phasor uses the conventional +i phase.
        const auto cycles=std::fmod(static_cast<long double>(s.basis_hz)*s.bin_first/s.rate,1.L);
        z*=std::polar(1.,-tau*static_cast<double>(cycles));
        if(std::norm(z)>1e-14&&std::norm(s.previous_bin)>1e-14) {
            const auto increment=z*std::conj(s.previous_bin);
            const auto delta=std::arg(increment);
            const auto expected_delta=tau*(s.guess_hz-s.basis_hz)*s.bin_size/s.rate;
            if((s.coarse_valid?std::abs(phase_wrap(delta-expected_delta))<.35:
                std::abs(delta)<=tau*(width+1)*s.bin_size/s.rate)&&
               std::abs(std::abs(z)/std::abs(s.previous_bin)-1)<.1) {
                s.increment_sum+=increment/std::abs(increment);++s.increments;
                if(s.increments>=16) {
                    const auto value=s.basis_hz+std::arg(s.increment_sum)*s.rate/(tau*s.bin_size);
                    if(std::abs(s.increment_sum)>s.increments*.995&&std::abs(value-expected)<=width+1e-6) {
                        s.coarse_hz=value;s.coarse_valid=true;if(!s.reference)s.guess_hz=value;
                    }
                    s.increment_sum={};s.increments=0;
                }
            } else {s.increment_sum={};s.increments=0;}
        }
        s.previous_bin=z;
        const auto local=std::complex<double>{z}*std::polar(1.,tau*static_cast<double>(cycles));
        const auto projected=s.bin_cc*local.real()*local.real()-2*s.bin_cs*local.real()*local.imag()+
            s.bin_ss*local.imag()*local.imag();
        s.ring[s.ring_position]={z,s.bin_first,std::max(0.,s.bin_energy-projected)};s.ring_position=(s.ring_position+1)%s.ring.size();
        s.ring_count=std::min(s.ring.size(),s.ring_count+1);
        // The reversal alone supplies precise timing. A following dwell ramp
        // must also have its independent amplitude notch at +160 ms before
        // another reversal is allowed to establish the 400 ms grid. Metadata
        // need not be a straight tone near that boundary.
        const auto bin_center=(static_cast<double>(s.bin_first)+.5*s.bin_size)/s.rate;
        if(s.punctuation>=0&&s.amplitude>0&&std::abs(bin_center-s.punctuation-.16)<.004) {
            const auto level=std::abs(z)/s.amplitude;
            if(level<.3&&level<s.boundary_level) {s.boundary_notch=bin_center;s.boundary_level=level;}
        }
        finish_narrowband();find_punctuation();
        s.bin_first+=s.bin_size;s.bin_count=0;s.bin_osc={1,0};s.bin_sum={};s.bin_cc=s.bin_ss=s.bin_cs=s.bin_energy=0;
    }
    void bin_sample(float sample) {
        auto& s=state;const auto c=s.bin_osc.real(),v=-s.bin_osc.imag();
        s.bin_sum+=static_cast<double>(sample)*s.bin_osc;
        s.bin_energy+=static_cast<double>(sample)*sample;
        s.bin_cc+=c*c;s.bin_ss+=v*v;s.bin_cs+=c*v;s.bin_osc*=s.bin_step;
        if(++s.bin_count==s.bin_size)finish_bin();
    }
    bool metadata_fit(double shift,std::array<Pair,vertices>& points,double& residual) const {
        const auto& s=state;
        const auto block=[&](Block bb,Block bd1,Block bd2,Block dd) {
            return matrix(add(add(add(bb,bd1,-shift),bd2,-shift),dd,shift*shift));
        };
        std::array<Matrix,vertices> diagonal{},inverse_diagonal{};
        std::array<Matrix,vertices-1> upper{};
        std::array<Pair,vertices> rhs{};
        for(std::size_t i=0;i<vertices;++i) {
            diagonal[i]=block(s.bb.diagonal[i],s.bd.diagonal[i],s.bd.diagonal[i],s.dd.diagonal[i]);
            rhs[i]={s.rhs[i][0]-shift*s.rhs_derivative[i][0],s.rhs[i][1]-shift*s.rhs_derivative[i][1]};
            if(i+1<vertices)upper[i]=block(s.bb.upper[i],s.bd.upper[i],s.bd.lower[i],s.dd.upper[i]);
        }
        points.front()={1,0};points.back()={1,0};
        rhs[1]=rhs[1]-upper[0]*points.front();rhs[vertices-2]=rhs[vertices-2]-upper[vertices-2]*points.back();
        for(std::size_t i=1;i+1<vertices;++i) {
            if(i>1) {
                const auto multiplier=upper[i-1]*inverse_diagonal[i-1];
                diagonal[i]=diagonal[i]-multiplier*upper[i-1];
                rhs[i]=rhs[i]-multiplier*rhs[i-1];
            }
            if(!inverse(diagonal[i],inverse_diagonal[i]))return false;
        }
        for(std::size_t i=vertices-2;i>0;--i) {
            auto value=rhs[i];if(i+1<vertices-1)value=value-upper[i]*points[i+1];
            points[i]=inverse_diagonal[i]*value;
            if(!std::isfinite(points[i][0])||!std::isfinite(points[i][1]))return false;
        }
        residual=s.metadata_energy;
        for(std::size_t i=0;i<vertices;++i) {
            const Pair b{s.rhs[i][0]-shift*s.rhs_derivative[i][0],s.rhs[i][1]-shift*s.rhs_derivative[i][1]};
            const auto a=block(s.bb.diagonal[i],s.bd.diagonal[i],s.bd.diagonal[i],s.dd.diagonal[i]);
            residual+=dot(points[i],a*points[i])-2*dot(points[i],b);
            if(i+1<vertices)residual+=2*dot(points[i],upper[i]*points[i+1]);
        }
        residual=std::max(0.,residual);return true;
    }
    void finish_metadata() {
        auto& s=state;s.metadata_active=false;
        if(s.metadata_samples<static_cast<std::size_t>(.063*s.rate)||!s.reference)return;
        std::array<Pair,vertices> points{};double residual=0;
        double lo=-.0002,hi=.0002;
        for(unsigned i=0;i<24;++i) {
            const auto a=lo+(hi-lo)/3,b=hi-(hi-lo)/3;double ea=0,eb=0;
            if(!metadata_fit(a,points,ea)||!metadata_fit(b,points,eb))return;
            if(ea<eb)hi=b;else lo=a;
        }
        const auto shift=.5*(lo+hi);
        if(!metadata_fit(shift,points,residual)||s.metadata_energy<=0||residual/s.metadata_energy>1e-6)return;
        std::array<std::uint32_t,vertices-2> words{};
        std::array<bool,vertices-2> valid{};
        for(std::size_t i=0;i<words.size();++i) {
            const std::complex<double> p{points[i+1][0],points[i+1][1]};
            const auto radius=std::abs(p),phase=std::arg(p);
            const auto ring=std::lround((radius-.9)*1023/.1);
            const auto sector=std::lround(phase*2048/tau);
            if(ring<0||ring>1023)continue;
            const auto word=(static_cast<std::uint32_t>(ring)<<11)|
                (static_cast<std::uint32_t>(sector)&2047U);
            const auto ideal=apsk_point(word);
            if(std::abs(radius-std::abs(ideal))>.48*.1/1023||
               std::abs(phase_wrap(phase-std::arg(ideal)))>.48*tau/2048)continue;
            words[i]=word;valid[i]=true;
        }
        // Compact frames end at 388 ms and contain one through seven symbols.
        // Fit the unchanged complete interval once, then try only suffixes
        // whose preceding fitted vertices are all the unmodulated unit tone.
        // Canonical type/length/padding checks belong to the compact codec.
        const auto selected_key=std::span<const Crypto>(keys).first(std::min<std::size_t>(1,keys.size()));
        std::optional<Metadata> metadata;
        for(std::size_t count=1;count<=7;++count) {
            const auto first=words.size()-count;
            bool complete=true;
            for(std::size_t i=0;i<first;++i)
                if(std::hypot(points[i+1][0]-1,points[i+1][1])>=.00015)complete=false;
            for(std::size_t i=first;i<words.size();++i)if(!valid[i])complete=false;
            if(!complete)continue;
            metadata=decode_metadata_packet(std::span<const std::uint32_t>(words).subspan(first),selected_key);
            if(metadata)break;
        }
        if(metadata) {
            // Compact metadata is unauthenticated, and its optional CRC never
            // grants timing authority. The interpolation shift is only a local
            // nuisance fit; preserve the independent punctuation-derived grid.
            auto update=*s.reference;
            const auto frontier=s.next-1;
            const auto age=static_cast<double>(frontier-update.sample)/s.rate;
            update.frequency_sample=update.frequency_sample?update.frequency_sample:update.sample;
            update.phase_radians=phase_wrap(update.phase_radians+tau*update.frequency_hz*age);
            update.phase_uncertainty_radians+=tau*update.frequency_uncertainty_hz*age;
            update.sample=frontier;update.metadata=std::move(metadata);
            update.timing_valid=s.timing_valid;
            update.dwell_start_seconds=s.metadata_dwell;
            update.dwell_uncertainty_seconds=s.timing_uncertainty;s.update=fuse_frequency(std::move(update));
        }
    }
    void metadata_sample(float sample,std::uint64_t index) {
        auto& s=state;if(!s.timing_valid||!s.reference)return;
        const auto time=seconds(index);
        const auto dwell=s.anchor+.4*std::floor((time-s.anchor)/.4),t=time-dwell;
        if(s.metadata_active&&(dwell!=s.metadata_dwell||t>=.396))finish_metadata();
        if(t<.332||t>=.396)return;
        if(!s.metadata_active) {
            if(std::abs(s.metadata_last_dwell-dwell)<.1||t>.332+1.5/s.rate)return;
            s.metadata_last_dwell=dwell;
            const auto& r=*s.reference;
            const auto age=time-seconds(r.sample);
            if(age<0||age>.08||r.phase_uncertainty_radians>.0002||r.frequency_uncertainty_hz>.002)return;
            s.metadata_active=true;s.metadata_dwell=dwell;s.bb={};s.dd={};s.bd={};s.rhs={};s.rhs_derivative={};
            s.metadata_energy=0;s.metadata_samples=0;s.metadata_gain=s.amplitude;
            s.metadata_osc=std::polar(1.,r.phase_radians+std::numbers::pi+tau*r.frequency_hz*age);
            s.metadata_step=std::polar(1.,tau*r.frequency_hz/s.rate);
        }
        const auto position=(t-.332)/.008;
        const auto i=std::min<std::size_t>(vertices-2,static_cast<std::size_t>(std::floor(std::max(0.,position))));
        const auto u=position-i,h=smooth(u),d=smooth_derivative(u)/.008;
        const Pair v{s.metadata_gain*s.metadata_osc.real(),-s.metadata_gain*s.metadata_osc.imag()};
        const auto vv=outer(v,1);
        const std::array<double,2> b{1-h,h},derivative{-d,d};
        for(std::size_t j=0;j<2;++j) {
            const auto vertex=i+j;
            s.bb.diagonal[vertex]=add(s.bb.diagonal[vertex],vv,b[j]*b[j]);
            s.dd.diagonal[vertex]=add(s.dd.diagonal[vertex],vv,derivative[j]*derivative[j]);
            s.bd.diagonal[vertex]=add(s.bd.diagonal[vertex],vv,b[j]*derivative[j]);
            for(unsigned k=0;k<2;++k) {
                s.rhs[vertex][k]+=sample*b[j]*v[k];
                s.rhs_derivative[vertex][k]+=sample*derivative[j]*v[k];
            }
        }
        s.bb.upper[i]=add(s.bb.upper[i],vv,b[0]*b[1]);
        s.dd.upper[i]=add(s.dd.upper[i],vv,derivative[0]*derivative[1]);
        s.bd.upper[i]=add(s.bd.upper[i],vv,b[0]*derivative[1]);
        s.bd.lower[i]=add(s.bd.lower[i],vv,b[1]*derivative[0]);
        s.metadata_energy+=static_cast<double>(sample)*sample;++s.metadata_samples;s.metadata_osc*=s.metadata_step;
    }
};
Receiver::Receiver(double expected,double width,std::vector<Crypto> keys) {
    if(!std::isfinite(expected)||expected<=0||!std::isfinite(width)||width<0||width>100||keys.size()>8)
        throw Error("invalid Helper Carrier precision band or receive-key count");
    // Do not inherit an unbounded caller vector capacity into the workspace.
    std::vector<Crypto> compact;
    if(!keys.empty())compact.push_back(keys.front());
    if(sizeof(*this)+sizeof(Impl)+compact.capacity()*sizeof(Crypto)+2048>workspace_bound)
        throw Error("Helper Carrier precision receiver exceeds its bounded workspace");
    impl_=std::make_unique<Impl>(expected,width,std::move(compact));
}
Receiver::~Receiver()=default;
Receiver::Receiver(Receiver&&) noexcept=default;
Receiver& Receiver::operator=(Receiver&&) noexcept=default;
void Receiver::reset() {impl_->state={};}
void Receiver::push(std::span<const float> samples,std::uint32_t rate,std::uint64_t first) {
    auto& s=impl_->state;
    if(!rate)throw Error("invalid Helper Carrier precision sample rate");
    if(s.rate!=rate||(s.rate&&s.next!=first))impl_->configure(rate,first);
    for(const auto sample:samples) {
        if(!std::isfinite(sample)||s.next==std::numeric_limits<std::uint64_t>::max()) {reset();return;}
        const auto index=s.next++;
        if(s.timing_valid&&impl_->seconds(index)-s.punctuation>.85) {
            s.timing_valid=false;s.candidate.reset();
        }
        impl_->bin_sample(sample);impl_->fit_sample(sample,index);impl_->metadata_sample(sample,index);
        if(!s.bin_count)impl_->recenter_basis();
    }
}
std::optional<Reference> Receiver::take_update() {auto out=std::move(impl_->state.update);impl_->state.update.reset();return out;}
}
