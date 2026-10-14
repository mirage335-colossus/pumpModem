#pragma once

#include "pattern_correlator_batch.hpp"
#include <array>
#include <bit>
#include <cstdlib>
#include <numbers>
#include <string_view>

namespace datapump::modem::detail {

// Experimental execution of the SAME two-real-basis fit. Only physical
// frequency varies; source coordinates, private candidates, masks and owners
// must already have passed the common-family identity proof. No waveform,
// threshold, phase/clock/timing hypothesis or trial is removed here.
inline bool scheduled_frequency_piece_requested() {
    static const bool requested=[] {
        const auto* value=std::getenv("DATAPUMP_SCHED_FREQUENCY_PIECE");
        return value && std::string_view(value)=="1";
    }();
    return requested;
}

struct ScheduledFrequencyPiecePhase {
    std::complex<double> initial{1,0};
    double radians_per_sample=0,relative_error=0;
    bool supported=false;
};

// Reproduce the ORIGINAL block seed/step, rather than subtracting two huge
// absolute phase angles. The ratio is evaluated only over one <=512 sample
// oscillator block. Remaining original recurrence/ratio rounding is bounded
// separately from the third-order frequency polynomial.
inline ScheduledFrequencyPiecePhase scheduled_frequency_piece_phase(
        std::uint64_t block_first,std::size_t offset,std::size_t block_samples,
        double coarse,double fine,std::uint32_t sample_rate) {
    ScheduledFrequencyPiecePhase out;
    if(!sample_rate || !block_samples || block_samples>512 || offset>=block_samples ||
       !std::isfinite(coarse) || !std::isfinite(fine))return out;
    constexpr double tau=2*std::numbers::pi;
    const auto seed=[&](double frequency) {
        return std::polar(1.,static_cast<double>(std::remainder(
            static_cast<long double>(block_first)*tau*frequency/sample_rate,
            static_cast<long double>(tau))));
    };
    const auto base_seed=seed(coarse),target_seed=seed(fine);
    const auto base_step=std::polar(1.,tau*coarse/sample_rate);
    const auto target_step=std::polar(1.,tau*fine/sample_rate);
    auto ratio=target_step/base_step,power=std::complex<double>{1,0};
    for(auto n=offset;n;n>>=1) {
        if(n&1U)power*=ratio;
        if(n>1)ratio*=ratio;
    }
    const auto step_ratio=target_step/base_step;
    out.initial=(target_seed/base_seed)*power;
    out.radians_per_sample=std::arg(step_ratio);
    // Includes complex recurrence, seed division and integer-power rounding.
    // This assumes ordinary correctly bounded native sin/cos/polar execution;
    // it is not a universal libm or final guard-decision certificate.
    out.relative_error=128*std::numeric_limits<double>::epsilon()*(block_samples+1);
    out.supported=std::isfinite(out.initial.real()) && std::isfinite(out.initial.imag()) &&
        std::isfinite(out.radians_per_sample) && std::norm(out.initial)>.99 &&
        std::norm(out.initial)<1.01;
    return out;
}

class ScheduledFrequencyPiece {
public:
    static constexpr unsigned order=3;
    static constexpr std::size_t maximum_samples=128,maximum_lanes=16;
    static constexpr double maximum_phase_span=.0005;
    struct Evaluation {
        std::array<CorrelationFit,2> fit{};
        std::array<std::complex<double>,correlation_pulse_atoms> outer{};
        std::array<double,2> dot_error{},gram_error{};
        std::array<double,correlation_pulse_atoms> outer_error{};
        double maximum_relative_error=0;
        bool supported=false;
    };
    explicit ScheduledFrequencyPiece(std::size_t span):span_(span) {
        valid_=span>=8 && span<=maximum_samples;
    }
    bool add(const CorrelationProjection& projection,
             const std::array<std::complex<double>,2>& phase,
             std::span<const double,correlation_pulse_atoms> pulse,
             bool outer,std::size_t offset) {
        if(!valid_ || offset>=span_ || offset!=visited_)return valid_=false;
        ++visited_;
        const auto z=std::complex<double>{projection.xc,projection.xs};
        const auto c=std::complex<double>{projection.cc-projection.ss,2*projection.cs};
        const auto n=projection.cc+projection.ss;
        if(!std::isfinite(z.real()+z.imag()+c.real()+c.imag()+n+projection.energy) ||
           projection.energy<0 || n<0)return valid_=false;
        // An actual zero gate is excluded by the caller. An incidental zero
        // private template is still a selected physical noise observation.
        energy_+=projection.energy;++count_;
        const double u=static_cast<double>(offset)/static_cast<double>(span_-1);
        std::array<double,7> powers{};powers[0]=1;
        for(unsigned k=1;k<powers.size();++k)powers[k]=powers[k-1]*u;
        for(unsigned bit=0;bit<2;++bit) {
            const auto a=phase[bit],a2=a*a;const auto norm=std::norm(a);
            if(!std::isfinite(a.real()+a.imag()+norm))return valid_=false;
            for(unsigned k=0;k<=order;++k)dot_[bit][k]+=powers[k]*(a*z);
            for(unsigned k=0;k<=2*order;++k)square_[bit][k]+=powers[k]*(a2*c);
            norm_[bit][0]+=norm*n;norm_[bit][1]+=powers[4]*norm*n;norm_[bit][2]+=powers[6]*norm*n;
            absolute_dot_[bit]+=std::abs(a)*(std::abs(z.real())+std::abs(z.imag()));
            absolute_gram_[bit]+=norm*(std::abs(projection.cc)+std::abs(projection.ss)+2*std::abs(projection.cs));
            absolute_template_[bit]+=std::abs(a);template_power_[bit]+=norm;
        }
        if(outer)for(std::size_t j=0;j<correlation_pulse_atoms;++j) {
            if(!std::isfinite(pulse[j]))return valid_=false;
            for(unsigned k=0;k<=order;++k)outer_[j][k]+=powers[k]*pulse[j]*z;
            outer_absolute_[j]+=std::abs(pulse[j])*(std::abs(z.real())+std::abs(z.imag()));
            outer_template_[j]+=std::abs(pulse[j]);outer_power_[j]+=pulse[j]*pulse[j]*n;
        }
        return true;
    }
    // Sparse gain masks retain the original physical normalized coordinate.
    // Skipping zero-gain observations advances visited_, never N or E.
    bool skip(std::size_t offset) {
        if(!valid_ || offset>=span_ || offset!=visited_)return valid_=false;
        ++visited_;return true;
    }
    Evaluation evaluate(ScheduledFrequencyPiecePhase phase,double whole_block_energy,
                        std::size_t block_samples)const {
        Evaluation out;
        const double a=phase.radians_per_sample*static_cast<double>(span_-1);
        if(!valid_ || visited_!=span_ || count_<=2 || !phase.supported ||
           !std::isfinite(a) || std::abs(a)>maximum_phase_span ||
           !std::isfinite(whole_block_energy) || whole_block_energy<energy_ ||
           !block_samples || block_samples>512)return out;
        std::array<std::complex<double>,order+1> p{};p[0]=1;
        for(unsigned k=1;k<=order;++k)p[k]=p[k-1]*std::complex<double>{0,a}/static_cast<double>(k);
        std::array<std::complex<double>,2*order+1> s{};
        for(unsigned j=0;j<=order;++j)for(unsigned k=0;k<=order;++k)s[j+k]+=p[j]*p[k];
        const auto a2=a*a,a4=a2*a2,a6=a4*a2;
        const auto radius=std::abs(phase.initial),radius2=std::norm(phase.initial);
        const auto remainder=std::pow(std::abs(a),4)/24;
        const auto u=std::numeric_limits<double>::epsilon();
        const auto arithmetic=256*u*(span_+1);
        const auto relative=remainder+phase.relative_error+arithmetic;
        // Original prefix differences can cancel. Bound their endpoint errors
        // using total block energy and duration, not the small resulting dot.
        const auto prefix_roundoff=32*u*(block_samples+1);
        const auto prefix_dot=prefix_roundoff*std::sqrt(static_cast<double>(block_samples)*whole_block_energy);
        const auto prefix_gram=prefix_roundoff*block_samples;
        for(unsigned bit=0;bit<2;++bit) {
            std::complex<double> dot{},square{};
            for(unsigned k=0;k<=order;++k)dot+=p[k]*dot_[bit][k];
            for(unsigned k=0;k<=2*order;++k)square+=s[k]*square_[bit][k];
            dot*=phase.initial;square*=phase.initial*phase.initial;
            // Exact trace of P3: |P3(i a u)|²=1-a^4u^4/12+a^6u^6/36.
            const auto trace=radius2*(norm_[bit][0]-a4*norm_[bit][1]/12+a6*norm_[bit][2]/36);
            auto& f=out.fit[bit];f={dot.real(),dot.imag(),(trace+square.real())/2,
                (trace-square.real())/2,square.imag()/2,energy_,count_};
            const auto ed=radius*(relative*absolute_dot_[bit]+2*prefix_dot*absolute_template_[bit]);
            const auto eg=radius2*((2*relative+relative*relative)*absolute_gram_[bit]+
                2*prefix_gram*template_power_[bit]);
            out.dot_error[bit]=ed;out.gram_error[bit]=eg;
            const auto lambda=.5*(trace-std::hypot(f.cc-f.ss,2*f.cs));
            const auto determinant=f.cc*f.ss-f.cs*f.cs;
            if(!std::isfinite(trace+f.xc+f.xs+f.cs+ed+eg+lambda) ||
               f.cc<=0 || f.ss<=0 || determinant<=1e-10*trace*trace ||
               !(lambda>16*eg))return out;
            const auto dot_scale=std::sqrt(energy_*lambda);
            const auto normalized=std::max(eg/lambda,dot_scale>0?ed/dot_scale:ed);
            out.maximum_relative_error=std::max(out.maximum_relative_error,normalized);
            if(!std::isfinite(normalized) || normalized>1e-7)return out;
        }
        for(std::size_t j=0;j<correlation_pulse_atoms;++j) {
            std::complex<double> q{};
            for(unsigned k=0;k<=order;++k)q+=p[k]*outer_[j][k];
            out.outer[j]=phase.initial*q;
            const auto e=radius*(relative*outer_absolute_[j]+2*prefix_dot*outer_template_[j]);
            out.outer_error[j]=e;
            const auto scale=std::sqrt(energy_*outer_power_[j]);
            const auto normalized=scale>0?e/scale:e;
            out.maximum_relative_error=std::max(out.maximum_relative_error,normalized);
            if(!std::isfinite(out.outer[j].real()+out.outer[j].imag()+normalized) || normalized>1e-7)return out;
        }
        out.supported=true;return out;
    }
private:
    std::array<std::array<std::complex<double>,order+1>,2> dot_{};
    std::array<std::array<std::complex<double>,2*order+1>,2> square_{};
    std::array<std::array<double,3>,2> norm_{};
    std::array<std::array<std::complex<double>,order+1>,correlation_pulse_atoms> outer_{};
    std::array<double,correlation_pulse_atoms> outer_absolute_{},outer_template_{},outer_power_{};
    std::array<double,2> absolute_dot_{},absolute_gram_{},absolute_template_{},template_power_{};
    double energy_=0;
    std::size_t span_=0,visited_=0;
    std::uint64_t count_=0;
    bool valid_=false;
};

// Separate outlined frame; a final-surplus loan per worker is required BEFORE
// invoking it. This includes descriptor/result arrays and callback headroom,
// not a claim to bound transitive crypto/libm/OS stacks on every platform.
inline constexpr std::size_t scheduled_frequency_piece_scratch_bytes=32768;
static_assert(sizeof(ScheduledFrequencyPiece)+ScheduledFrequencyPiece::maximum_lanes*
    sizeof(ScheduledFrequencyPiece::Evaluation)+4096<=scheduled_frequency_piece_scratch_bytes);

} // namespace datapump::modem::detail
