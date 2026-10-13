#pragma once

#include "pattern_correlator_batch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace datapump::modem::detail {

// Internal diagnostic helper. Algorithm selection is independent of arithmetic
// and template reuse. This approximation is not enabled by the public Default
// policy until complete receiver execution and sensitivity support it. FP64
// force must bypass it and retain the original fine-frequency templates.
//
// One instance belongs to one exact source-piece / clock / timing / phase /
// key / epoch / bit identity. It never owns PCM, keys, clocks, or publication.
// The caller retains the current bounded piece until checking its certificate,
// and can then recompute that piece exactly. No later whole-symbol PCM rewind is
// available: propagate accepted component errors into the final fit instead.
enum class ScheduledFusionArithmetic { fp32, fp64 };
enum class ScheduledFusionBackend { portable, native };

struct ScheduledFusionSample {
    CorrelationFit coarse;
    double normalized_time=0; // physical time about this piece's center, [-1,1]
};

inline ScheduledFusionSample scheduled_fusion_sample(const CorrelationProjection& projection,
        std::complex<double> private_phase,double normalized_time) {
    CorrelationFit fit;fit.add(projection,private_phase,1);
    return {fit,normalized_time};
}

struct ScheduledFusionPhase {
    std::complex<double> value{1,0};
    double error=0;
    bool supported=true;
};

// Keep physical coordinates and cycle reduction wide. This routine assumes the
// host long-double sin/cos implementation is within four ulps. The arithmetic
// reservation includes that assumption; it is not a hardware timing guarantee.
// A caller with an independently certified oscillator may supply its own phase.
inline ScheduledFusionPhase scheduled_fusion_phase(double delta_hz,
        long double center_relative_samples,std::uint32_t sample_rate) {
    ScheduledFusionPhase result;
    if(!sample_rate || !std::isfinite(delta_hz) || !std::isfinite(center_relative_samples)) {
        result.supported=false;return result;
    }
    constexpr auto tau=2*std::numbers::pi_v<long double>;
    const auto cycles=static_cast<long double>(delta_hz)*center_relative_samples/sample_rate;
    if(!std::isfinite(cycles)){result.supported=false;return result;}
    const auto angle=tau*std::remainder(cycles,1.L);
    result.value={static_cast<double>(std::cos(angle)),static_cast<double>(std::sin(angle))};
    result.error=static_cast<double>(32*std::numeric_limits<long double>::epsilon()*
        std::max(1.L,std::abs(cycles))+16*std::numeric_limits<double>::epsilon());
    // Excessively remote phase origins require an exact/rebased oscillator.
    result.supported=std::isfinite(result.error) && result.error<=1e-6;
    return result;
}

struct ScheduledFusionEvaluation {
    CorrelationFit fit;
    // Componentwise complex-vector and Gram operator-norm bounds relative to
    // the original unquantized coarse fits rotated by the exact exponential.
    double dot_error=0,gram_error=0,energy_error=0,template_relative_error=0;
    double score_lower=0,score_upper=std::numeric_limits<double>::infinity();
    bool supported=false,well_conditioned=false;
    bool above(double threshold)const {
        return supported && well_conditioned && score_lower>threshold;
    }
    bool below(double threshold)const {
        return supported && well_conditioned && score_upper<threshold;
    }
    bool ambiguous(double threshold)const {return !above(threshold) && !below(threshold);}
};

namespace scheduled_fusion_detail {
inline long double gamma(std::uint64_t operations,long double unit) {
    const auto value=operations*unit;
    return value<.5L?value/(1-value):std::numeric_limits<long double>::infinity();
}

template<class T>struct Vector {
    static constexpr unsigned width=1;
    T value=0;
    static Vector load(const T* p){return {*p};}
    static Vector one(){return {1};}
    friend Vector operator+(Vector a,Vector b){return {a.value+b.value};}
    friend Vector operator*(Vector a,Vector b){return {a.value*b.value};}
    double sum()const{return static_cast<double>(value);}
};
#if defined(__SSE2__)
template<>struct Vector<float> {
    static constexpr unsigned width=4;
    __m128 value=_mm_setzero_ps();
    static Vector load(const float* p){return {_mm_loadu_ps(p)};}
    static Vector one(){return {_mm_set1_ps(1)};}
    friend Vector operator+(Vector a,Vector b){return {_mm_add_ps(a.value,b.value)};}
    friend Vector operator*(Vector a,Vector b){return {_mm_mul_ps(a.value,b.value)};}
    double sum()const {
        alignas(16)float terms[4];_mm_store_ps(terms,value);
        return static_cast<double>(terms[0])+terms[1]+terms[2]+terms[3];
    }
};
template<>struct Vector<double> {
    static constexpr unsigned width=2;
    __m128d value=_mm_setzero_pd();
    static Vector load(const double* p){return {_mm_loadu_pd(p)};}
    static Vector one(){return {_mm_set1_pd(1)};}
    friend Vector operator+(Vector a,Vector b){return {_mm_add_pd(a.value,b.value)};}
    friend Vector operator*(Vector a,Vector b){return {_mm_mul_pd(a.value,b.value)};}
    double sum()const {
        alignas(16)double terms[2];_mm_store_pd(terms,value);return terms[0]+terms[1];
    }
};
#endif
// Separate scalar type: portable tests must execute scalar reductions even
// when this translation unit also supports the SSE2 baseline.
template<class T>struct Scalar {
    static constexpr unsigned width=1;T value=0;
    static Scalar load(const T* p){return {*p};}
    static Scalar one(){return {1};}
    friend Scalar operator+(Scalar a,Scalar b){return {a.value+b.value};}
    friend Scalar operator*(Scalar a,Scalar b){return {a.value*b.value};}
    double sum()const{return static_cast<double>(value);}
};
} // namespace scheduled_fusion_detail

class ScheduledFrequencyFusion {
public:
    static constexpr unsigned order=4;
    static constexpr std::size_t block_samples=128;
    // Bound the piece whose original PCM must still be available for fallback.
    // Longer physical intervals can be split without changing source identity.
    static constexpr std::size_t maximum_piece_samples=4096;
    static constexpr double maximum_local_radians=.125;

    explicit ScheduledFrequencyFusion(ScheduledFusionArithmetic arithmetic=ScheduledFusionArithmetic::fp32,
            ScheduledFusionBackend backend=ScheduledFusionBackend::native)
        :arithmetic_(arithmetic),backend_(backend) {}

    static bool reuse_worth_trying(std::size_t samples,std::size_t fine_frequencies) {
        // Conservative prepared-kernel crossover, not an automatic complete-
        // receiver selection. Include guard/template/setup costs before dispatch.
        return samples>=32 && fine_frequencies>=3;
    }
    ScheduledFusionArithmetic arithmetic()const{return arithmetic_;}
    ScheduledFusionBackend backend()const {
#if defined(__SSE2__)
        return backend_;
#else
        return ScheduledFusionBackend::portable;
#endif
    }
    bool valid()const{return valid_;}
    std::uint64_t count()const{return count_;}
    std::size_t working_bytes()const{return sizeof(*this);}
    static constexpr std::size_t scratch_bytes() {
        // Seven bounded native arrays plus vector reductions. No allocation.
        return 7*block_samples*sizeof(double)+40*16+512;
    }
    void reset(){const auto a=arithmetic_;const auto b=backend_;*this=ScheduledFrequencyFusion(a,b);}

    bool add(std::span<const ScheduledFusionSample> samples) {
        if(!valid_ || samples.size()>maximum_piece_samples-count_){valid_=false;return false;}
        for(std::size_t first=0;first<samples.size();first+=block_samples) {
            const auto block=samples.subspan(first,std::min(block_samples,samples.size()-first));
            if(!validate(block)){valid_=false;return false;}
            if(arithmetic_==ScheduledFusionArithmetic::fp32) {
                if(backend()==ScheduledFusionBackend::native)accumulate<float,scheduled_fusion_detail::Vector<float>>(block);
                else accumulate<float,scheduled_fusion_detail::Scalar<float>>(block);
            } else {
                if(backend()==ScheduledFusionBackend::native)accumulate<double,scheduled_fusion_detail::Vector<double>>(block);
                else accumulate<double,scheduled_fusion_detail::Scalar<double>>(block);
            }
            ++chunks_;
        }
        return true;
    }

    static ScheduledFusionEvaluation certify(CorrelationFit fit,double dot_error,double gram_error,double energy_error) {
        ScheduledFusionEvaluation out;out.fit=fit;out.dot_error=dot_error;out.gram_error=gram_error;out.energy_error=energy_error;
        out.supported=std::isfinite(dot_error) && dot_error>=0 && std::isfinite(gram_error) && gram_error>=0 &&
            std::isfinite(energy_error) && energy_error>=0 && std::isfinite(fit.xc) && std::isfinite(fit.xs) &&
            std::isfinite(fit.cc) && std::isfinite(fit.ss) && std::isfinite(fit.cs) &&
            std::isfinite(fit.energy) && fit.energy>=0 && fit.cc>=0 && fit.ss>=0;
        if(out.supported)score_interval(out);
        return out;
    }

    ScheduledFusionEvaluation evaluate(double local_radians,ScheduledFusionPhase phase={})const {
        ScheduledFusionEvaluation out;out.fit.count=count_;
        if(!valid_ || count_<=2 || !phase.supported || !std::isfinite(local_radians) ||
           std::abs(local_radians)>maximum_local_radians || !std::isfinite(phase.error) || phase.error<0 ||
           !std::isfinite(phase.value.real()) || !std::isfinite(phase.value.imag()))return out;
        const auto phase_norm=std::norm(phase.value);
        if(!(phase_norm>.99 && phase_norm<1.01))return out;
        const std::complex<double> argument{0,local_radians};
        std::array<std::complex<double>,order+1> p{};p[0]=1;
        double dot_weight=1,square_weight=0;
        for(unsigned k=1;k<=order;++k){p[k]=p[k-1]*argument/static_cast<double>(k);dot_weight+=std::abs(p[k]);}
        std::complex<double> d{},square{};
        for(unsigned k=0;k<=order;++k)d+=p[k]*dot_[k];
        for(unsigned k=0;k<=2*order;++k) {
            std::complex<double> coefficient{};
            for(unsigned j=k>order?k-order:0;j<=std::min(k,order);++j)coefficient+=p[j]*p[k-j];
            square+=coefficient*square_[k];square_weight+=std::abs(coefficient);
        }
        const auto a2=local_radians*local_radians,a6=a2*a2*a2,a8=a6*a2;
        const auto trace_before_phase=static_cast<double>(norm_)-a6*norm6_/72+a8*norm8_/576;
        const auto trace=phase_norm*trace_before_phase;
        d*=phase.value;square*=phase.value*phase.value;
        out.fit={d.real(),d.imag(),(trace+square.real())/2,(trace-square.real())/2,
            square.imag()/2,static_cast<double>(energy_),count_};
        const auto magnitude=std::abs(local_radians);
        const auto remainder=magnitude*magnitude*magnitude*magnitude*magnitude/120;
        const auto absolute_dot=static_cast<double>(abs_dot_),absolute_square=static_cast<double>(abs_square_);
        const auto norm=static_cast<double>(norm_),energy=static_cast<double>(energy_);
        const auto phase_error=phase.error+std::abs(std::sqrt(phase_norm)-1);
        const auto template_change=remainder+phase_error*(1+remainder);
        out.template_relative_error=template_change;
        // Products, u conversion/powers and native reduction error. Use a
        // componentwise absolute envelope so cancellation never hides error.
        const auto unit=arithmetic_==ScheduledFusionArithmetic::fp32?
            std::numeric_limits<float>::epsilon()/2.L:std::numeric_limits<double>::epsilon()/2.L;
        const auto terms=backend()==ScheduledFusionBackend::native?
            (arithmetic_==ScheduledFusionArithmetic::fp32?32U:64U):128U;
        const auto local_gamma=scheduled_fusion_detail::gamma(terms+2*order*2+8,unit);
        const auto wide_gamma=scheduled_fusion_detail::gamma(chunks_+8,std::numeric_limits<double>::epsilon()/2.L);
        const auto reference_gamma=scheduled_fusion_detail::gamma(count_+32,std::numeric_limits<double>::epsilon()/2.L);
        const auto rounded=static_cast<double>(local_gamma+wide_gamma+local_gamma*wide_gamma+reference_gamma);
        constexpr auto evaluation_gamma=256*std::numeric_limits<double>::epsilon();
        const auto phase_size=std::sqrt(phase_norm);
        const auto underflow=count_*64.*(arithmetic_==ScheduledFusionArithmetic::fp32?
            std::numeric_limits<float>::min():std::numeric_limits<double>::min());
        const auto numerical_dot=((rounded+evaluation_gamma)*absolute_dot+underflow)*dot_weight*phase_size;
        const auto numerical_square=((rounded+evaluation_gamma)*absolute_square+underflow)*square_weight*phase_norm;
        const auto numerical_norm=((rounded*(a6/72+a8/576)+evaluation_gamma)*norm+underflow)*phase_norm;
        // CorrelationFit Gram spectral norm <= |trace error| + |square error|;
        // the factor2 is conservative for component/evaluation rounding.
        // The absolute observed-dot envelope also bounds arbitrary accepted
        // diagnostic fits; no unvalidated Cauchy relationship is assumed.
        out.dot_error=numerical_dot+template_change*absolute_dot;
        out.gram_error=2*(numerical_square+numerical_norm+(2*template_change+template_change*template_change)*norm);
        out.energy_error=static_cast<double>(scheduled_fusion_detail::gamma(count_+2,
            std::numeric_limits<long double>::epsilon()/2.L)*energy_)+
            static_cast<double>(reference_gamma)*energy+8*std::numeric_limits<double>::epsilon()*energy;
        out.supported=std::isfinite(out.dot_error) && std::isfinite(out.gram_error) &&
            std::isfinite(out.energy_error) && std::isfinite(out.fit.xc) && std::isfinite(out.fit.xs) &&
            std::isfinite(out.fit.cc) && std::isfinite(out.fit.ss) && std::isfinite(out.fit.cs);
        if(out.supported)score_interval(out);
        return out;
    }

private:
    ScheduledFusionArithmetic arithmetic_;
    ScheduledFusionBackend backend_;
    bool valid_=true;
    std::uint64_t count_=0,chunks_=0;
    std::array<std::complex<double>,order+1> dot_{};
    std::array<std::complex<double>,2*order+1> square_{};
    double norm6_=0,norm8_=0;
    long double norm_=0,energy_=0,abs_dot_=0,abs_square_=0;

    bool validate(std::span<const ScheduledFusionSample> samples)const {
        for(const auto& sample:samples) {
            const auto& f=sample.coarse;
            if(f.count!=1 || !std::isfinite(sample.normalized_time) || std::abs(sample.normalized_time)>1 ||
               !std::isfinite(f.xc) || !std::isfinite(f.xs) || !std::isfinite(f.cc) || !std::isfinite(f.ss) ||
               !std::isfinite(f.cs) || !std::isfinite(f.energy) || f.energy<0)return false;
            const auto norm=f.cc+f.ss;
            // Rotating a rank-one original projection can cancel in cc or ss.
            // Preserve those finite original rounding residues instead of
            // rejecting a legitimate private template at a quadrature zero.
            // This is an arithmetic-envelope check, not covariance clipping.
            const auto scale=std::abs(f.cc)+std::abs(f.ss)+2*std::abs(f.cs);
            const auto tolerance=128*std::numeric_limits<double>::epsilon()*std::max(1e-300,scale);
            if(!std::isfinite(norm) || !std::isfinite(2*f.cs) || norm<0 ||
               f.cc < -tolerance || f.ss < -tolerance ||
               std::hypot(f.cc-f.ss,2*f.cs)>norm+tolerance)return false;
            if(arithmetic_==ScheduledFusionArithmetic::fp32) {
                const std::array<double,7> values{f.xc,f.xs,f.cc-f.ss,2*f.cs,norm,f.energy,sample.normalized_time};
                for(auto x:values)if(x!=0 && (std::abs(x)<std::numeric_limits<float>::min()*256. ||
                    std::abs(x)>std::numeric_limits<float>::max()/256.))return false;
            }
        }
        return true;
    }

    template<class T,class V>void accumulate(std::span<const ScheduledFusionSample> samples) {
        alignas(16)std::array<T,block_samples> dr{},di{},cr{},ci{},norm{},u{};
        for(std::size_t i=0;i<samples.size();++i) {
            const auto& f=samples[i].coarse;
            dr[i]=static_cast<T>(f.xc);di[i]=static_cast<T>(f.xs);
            cr[i]=static_cast<T>(f.cc-f.ss);ci[i]=static_cast<T>(2*f.cs);
            norm[i]=static_cast<T>(f.cc+f.ss);u[i]=static_cast<T>(samples[i].normalized_time);
            norm_+=static_cast<long double>(f.cc)+f.ss;energy_+=f.energy;
            abs_dot_+=std::abs(static_cast<long double>(f.xc))+std::abs(static_cast<long double>(f.xs));
            abs_square_+=std::abs(static_cast<long double>(f.cc)-f.ss)+2*std::abs(static_cast<long double>(f.cs));
        }
        std::array<V,order+1> jr{},ji{};
        std::array<V,2*order+1> sr{},si{};
        V b6,b8;
        std::size_t i=0;
        for(;i+V::width<=samples.size();i+=V::width) {
            const auto r=V::load(dr.data()+i),d=V::load(di.data()+i),c=V::load(cr.data()+i),
                s=V::load(ci.data()+i),b=V::load(norm.data()+i),v=V::load(u.data()+i);
            auto power=V::one();
            for(unsigned k=0;k<=2*order;++k) {
                if(k<=order){jr[k]=jr[k]+r*power;ji[k]=ji[k]+d*power;}
                sr[k]=sr[k]+c*power;si[k]=si[k]+s*power;
                if(k==6)b6=b6+b*power;
                if(k==8)b8=b8+b*power;
                power=power*v;
            }
        }
        for(unsigned k=0;k<=order;++k)dot_[k]+=std::complex<double>{jr[k].sum(),ji[k].sum()};
        for(unsigned k=0;k<=2*order;++k)square_[k]+=std::complex<double>{sr[k].sum(),si[k].sum()};
        norm6_+=b6.sum();norm8_+=b8.sum();
        // A small tail is scalar at the selected operand precision; avoid an
        // undocumented FP64 tail and retain the same rounding reservation.
        for(;i<samples.size();++i) {
            T power=1;
            for(unsigned k=0;k<=2*order;++k) {
                if(k<=order)dot_[k]+=std::complex<double>{static_cast<double>(dr[i]*power),static_cast<double>(di[i]*power)};
                square_[k]+=std::complex<double>{static_cast<double>(cr[i]*power),static_cast<double>(ci[i]*power)};
                if(k==6)norm6_+=static_cast<double>(norm[i]*power);
                if(k==8)norm8_+=static_cast<double>(norm[i]*power);
                power=static_cast<T>(power*u[i]);
            }
        }
        count_+=samples.size();
    }

    static void score_interval(ScheduledFusionEvaluation& out) {
        const auto& f=out.fit;
        const auto trace=f.cc+f.ss;
        const auto separation=std::hypot(f.cc-f.ss,2*f.cs);
        const auto lambda=(trace-separation)/2;
        if(!(lambda>out.gram_error) || !(f.energy>out.energy_error) || f.count<=2)return;
        const auto determinant=f.cc*f.ss-f.cs*f.cs;
        const auto determinant_error=(trace+2*out.gram_error)*out.gram_error;
        if(!(determinant>1e-12*std::max(1.,f.cc*f.ss)+determinant_error))return;
        const auto dnorm=std::hypot(f.xc,f.xs),denominator=lambda-out.gram_error;
        auto error=(2*dnorm*out.dot_error+out.dot_error*out.dot_error)/denominator+
            dnorm*dnorm*out.gram_error/(lambda*denominator);
        // Account conservatively for the final double Gram solve/subtraction.
        error+=256*std::numeric_limits<double>::epsilon()*std::max(1.,trace/lambda)*
            std::max(f.energy,dnorm*dnorm/lambda);
        const auto explained=f.explained();
        const auto low=std::clamp((explained-error)/(f.energy+out.energy_error),0.,1.);
        const auto high=std::clamp((explained+error)/(f.energy-out.energy_error),0.,1.);
        const auto scale=.5*static_cast<double>(f.count-2);
        out.score_lower=low>=1?std::numeric_limits<double>::infinity():-scale*std::log1p(-low);
        out.score_upper=high>=1?std::numeric_limits<double>::infinity():-scale*std::log1p(-high);
        const auto reserve=32*std::numeric_limits<double>::epsilon()*
            std::max(1.,std::isfinite(out.score_upper)?out.score_upper:out.score_lower);
        out.score_lower=std::max(0.,std::nextafter(out.score_lower-reserve,0.));
        if(std::isfinite(out.score_upper))out.score_upper=std::nextafter(out.score_upper+reserve,
            std::numeric_limits<double>::infinity());
        out.well_conditioned=std::isfinite(out.score_lower) && out.score_lower<=out.score_upper;
    }
};

} // namespace datapump::modem::detail
