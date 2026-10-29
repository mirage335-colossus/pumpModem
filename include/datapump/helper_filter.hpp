#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

namespace datapump::helper {
// Optional receiver front end for an adjacent helper. This is a real linear
// bandstop filter, not tone subtraction. Its symmetric FIR returns only fully
// observed windows: output zero is input delay(), and no missing PCM or EOF
// zeros are inserted. The caller must retain that input-coordinate offset.
class PayloadFilter {
public:
    struct NotchBand { double center_hz=0,half_width_hz=225; };
private:
    std::vector<double> taps_;
    std::vector<float> history_;
    std::size_t next_=0,seen_=0;
    std::uint64_t output_=0;
    std::uint32_t rate_=0;
    double center_=0,half_width_=225;
    std::array<NotchBand,3> bands_{};
    std::size_t band_count_=0;
    // Every bound operation rounds away from the exact nonnegative result.
    // This certificate is deliberately unavailable under altered FP modes.
    static double upper(double value) noexcept {
        return std::nextafter(value,std::numeric_limits<double>::infinity());
    }
    static double lower(double value) noexcept {
        return std::nextafter(value,-std::numeric_limits<double>::infinity());
    }
    static double add_upper(double a,double b) noexcept {return upper(a+b);}
    static double multiply_upper(double a,double b) noexcept {return upper(a*b);}
    struct Interval {double low,high;};
    static Interval plus(Interval a,Interval b) noexcept {
        return {lower(a.low+b.low),upper(a.high+b.high)};
    }
    static Interval times(Interval a,Interval b) noexcept {
        const std::array products{a.low*b.low,a.low*b.high,a.high*b.low,a.high*b.high};
        return {lower(*std::min_element(products.begin(),products.end())),
                upper(*std::max_element(products.begin(),products.end()))};
    }
    static Interval negative_divide(Interval value,double denominator) noexcept {
        return {lower(-value.high/denominator),upper(-value.low/denominator)};
    }
    static bool certificate_environment() noexcept {
#if defined(__FAST_MATH__)
        return false;
#endif
        if(!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits!=53 ||
           std::numeric_limits<double>::max_exponent!=1024 || std::fegetround()!=FE_TONEAREST)return false;
        volatile double normal=std::numeric_limits<double>::min();
        volatile double half=normal*.5;
        return half>0 && half<normal;
    }
    // Enclose mathematical sin/cos, without a libm accuracy assumption. The
    // angle is in [0,pi]. Twenty Taylor terms have remainder below 2^-80:
    // pi<22/7 and (22/7)^40/40!<2^-80. Interval operations include polynomial
    // and argument rounding, including endpoints just outside rounded pi.
    static std::pair<Interval,Interval> rotation(Interval angle) noexcept {
        const auto square=times(angle,angle);
        Interval cosine{1,1},sine=angle,cosine_term=cosine,sine_term=sine;
        for(unsigned n=1;n<20;++n) {
            cosine_term=negative_divide(times(cosine_term,square),static_cast<double>((2*n-1)*(2*n)));
            sine_term=negative_divide(times(sine_term,square),static_cast<double>((2*n)*(2*n+1)));
            cosine=plus(cosine,cosine_term);sine=plus(sine,sine_term);
        }
        const auto enclose=[](Interval value) {
            return Interval{std::max(-1.,lower(value.low-0x1p-80)),
                            std::min(1.,upper(value.high+0x1p-80))};
        };
        return {enclose(cosine),enclose(sine)};
    }
    bool valid_center(double center)const noexcept {
        return rate_ && std::isfinite(center) && std::isfinite(half_width_) && half_width_>0 &&
            center>half_width_ && center+half_width_<rate_/2.;
    }
    double coefficient(std::size_t i,double center)const noexcept {
        const auto middle=taps_.size()/2;
        const auto k=static_cast<double>(i)-middle;
        const auto window=.42-.5*std::cos(2*std::numbers::pi*i/(taps_.size()-1))+
            .08*std::cos(4*std::numbers::pi*i/(taps_.size()-1));
        const auto low=k==0?2*half_width_/rate_:
            std::sin(2*std::numbers::pi*half_width_*k/rate_)/(std::numbers::pi*k);
        return (i==middle?1.:0.)-2*low*std::cos(2*std::numbers::pi*center*k/rate_)*window;
    }
    double coefficient(std::size_t i,std::span<const NotchBand> bands)const noexcept {
        const auto middle=taps_.size()/2;
        const auto k=static_cast<double>(i)-middle;
        const auto window=.42-.5*std::cos(2*std::numbers::pi*i/(taps_.size()-1))+
            .08*std::cos(4*std::numbers::pi*i/(taps_.size()-1));
        double value=i==middle?1.:0.;
        for(const auto& band:bands) {
            const auto low=k==0?2*band.half_width_hz/rate_:
                std::sin(2*std::numbers::pi*band.half_width_hz*k/rate_)/(std::numbers::pi*k);
            value-=2*low*std::cos(2*std::numbers::pi*band.center_hz*k/rate_)*window;
        }
        return value;
    }
    double response_error(double frequency)const noexcept {
        // These adjacent binary64 values bracket mathematical pi.
        constexpr double pi_low=0x1.921fb54442d18p+1,pi_high=0x1.921fb54442d19p+1;
        const Interval angle{lower(lower(2*pi_low*frequency)/rate_),
                             upper(upper(2*pi_high*frequency)/rate_)};
        const auto [cosine,sine]=rotation(angle);
        const auto a=(cosine.low+cosine.high)*.5,b=(sine.low+sine.high)*.5;
        const auto radius=add_upper(upper(std::max(a-cosine.low,cosine.high-a)),
                                    upper(std::max(b-sine.low,sine.high-b)));
        constexpr auto epsilon=std::numeric_limits<double>::epsilon();
        double real=1,imaginary=0,rotation_error=0;
        double value=taps_[delay()]-1,error=multiply_upper(epsilon,std::abs(taps_[delay()])+1);
        for(std::size_t k=1;k<=delay();++k) {
            const auto next_real=real*a-imaginary*b,next_imaginary=real*b+imaginary*a;
            real=next_real;imaginary=next_imaginary;
            if(!std::isfinite(real)||!std::isfinite(imaginary)||std::abs(real)>2||std::abs(imaginary)>2)
                return std::numeric_limits<double>::infinity();
            // Exact rotations have unit norm. The L1 enclosure radius also
            // bounds the complex step error; 32 eps covers the two native
            // complex multiply outputs while their components are <=2.
            rotation_error=add_upper(add_upper(multiply_upper(add_upper(1,radius),rotation_error),radius),32*epsilon);
            const auto coefficient=2*taps_[delay()-k],term=coefficient*real;
            const auto rounding=multiply_upper(8*epsilon,add_upper(add_upper(std::abs(value),std::abs(term)),std::abs(coefficient)));
            error=add_upper(error,add_upper(multiply_upper(std::abs(coefficient),rotation_error),rounding));
            value+=term;
        }
        return add_upper(std::abs(value),error);
    }
    double response_magnitude(double frequency)const noexcept {
        // These adjacent binary64 values bracket mathematical pi.
        constexpr double pi_low=0x1.921fb54442d18p+1,pi_high=0x1.921fb54442d19p+1;
        const Interval angle{lower(lower(2*pi_low*frequency)/rate_),
                             upper(upper(2*pi_high*frequency)/rate_)};
        const auto [cosine,sine]=rotation(angle);
        const auto a=(cosine.low+cosine.high)*.5,b=(sine.low+sine.high)*.5;
        const auto radius=add_upper(upper(std::max(a-cosine.low,cosine.high-a)),
                                    upper(std::max(b-sine.low,sine.high-b)));
        constexpr auto epsilon=std::numeric_limits<double>::epsilon();
        double real=1,imaginary=0,rotation_error=0;
        double value=taps_[delay()],error=multiply_upper(epsilon,std::abs(taps_[delay()]));
        for(std::size_t k=1;k<=delay();++k) {
            const auto next_real=real*a-imaginary*b,next_imaginary=real*b+imaginary*a;
            real=next_real;imaginary=next_imaginary;
            if(!std::isfinite(real)||!std::isfinite(imaginary)||std::abs(real)>2||std::abs(imaginary)>2)
                return std::numeric_limits<double>::infinity();
            // Exact rotations have unit norm. The L1 enclosure radius also
            // bounds the complex step error; 32 eps covers the two native
            // complex multiply outputs while their components are <=2.
            rotation_error=add_upper(add_upper(multiply_upper(add_upper(1,radius),rotation_error),radius),32*epsilon);
            const auto coefficient=2*taps_[delay()-k],term=coefficient*real;
            const auto rounding=multiply_upper(8*epsilon,add_upper(add_upper(std::abs(value),std::abs(term)),std::abs(coefficient)));
            error=add_upper(error,add_upper(multiply_upper(std::abs(coefficient),rotation_error),rounding));
            value+=term;
        }
        return add_upper(std::abs(value),error);
    }
public:
    PayloadFilter()=default;
    // Width changes the one stored kernel, never its tap count or delay.
    // Legacy single-tone users retain their original +/-225-Hz notch.
    PayloadFilter(std::uint32_t rate,double center,double half_width=225):rate_(rate),half_width_(half_width) {
        if(!valid_center(center))
            throw Error("adjacent helper filter is outside the receiver passband");
        const auto count=static_cast<std::size_t>(std::ceil(rate*.04))|1U;
        if(count<3||count>4097)throw Error("adjacent helper filter exceeds its optional workspace bound");
        taps_.resize(count);history_.resize(count);
        if(!retune(center))throw Error("adjacent helper filter cannot establish a finite kernel in this FP environment");
    }
    PayloadFilter(std::uint32_t rate,std::span<const NotchBand> bands):rate_(rate) {
        if(bands.empty()||bands.size()>bands_.size())throw Error("helper filter needs one to three stopbands");
        for(const auto& band:bands)if(!valid_center(band.center_hz)||!std::isfinite(band.half_width_hz)||band.half_width_hz<=0||
                band.center_hz<=band.half_width_hz||band.center_hz+band.half_width_hz>=rate_/2.)
            throw Error("helper stopband is outside the receiver passband");
        for(std::size_t i=0;i<bands.size();++i)for(std::size_t j=0;j<i;++j)
            if(bands[i].center_hz-bands[i].half_width_hz<=bands[j].center_hz+bands[j].half_width_hz &&
               bands[j].center_hz-bands[j].half_width_hz<=bands[i].center_hz+bands[i].half_width_hz)
                throw Error("helper stopbands must be merged when they overlap");
        const auto count=static_cast<std::size_t>(std::ceil(rate*.04))|1U;
        if(count<3||count>4097)throw Error("adjacent helper filter exceeds its optional workspace bound");
        taps_.resize(count);history_.resize(count);
        if(!retune(bands))throw Error("helper filter cannot establish finite multi-notch coefficients");
    }
    template<std::size_t N>
    PayloadFilter(std::uint32_t rate,const std::array<NotchBand,N>& bands):
        PayloadFilter(rate,std::span<const NotchBand>(bands)) {}
    // Replace only the coefficients of this SAME-rate, SAME-length symmetric
    // FIR. The input ring, warmup, output counter, delay and capacities remain
    // untouched. A rejected center/FP mode cannot mutate even one tap. The
    // first pass validates the complete normalization; the second repeats the
    // fixed formula and commits without allocation. Recomputing an old center
    // in an unchanged FP environment restores its coefficient bits exactly.
    // l1_change bounds an instantaneous coefficient transition for bounded PCM:
    // |new_output-old_output| <= l1_change * max|input| (before float rounding).
    // This is NOT an all-time spectral certificate for a time-varying kernel.
    bool retune(double center,double* l1_change=nullptr)noexcept {
        const NotchBand band{center,half_width_};
        return retune(std::span<const NotchBand>(&band,1),l1_change);
    }
    bool retune(std::span<const NotchBand> bands,double* l1_change=nullptr)noexcept {
        if(!enabled() || bands.empty()||bands.size()>bands_.size()||!certificate_environment())return false;
        for(const auto& band:bands)if(!valid_center(band.center_hz)||!std::isfinite(band.half_width_hz)||band.half_width_hz<=0||
                band.center_hz<=band.half_width_hz||band.center_hz+band.half_width_hz>=rate_/2.)return false;
        for(std::size_t i=0;i<bands.size();++i)for(std::size_t j=0;j<i;++j)
            if(bands[i].center_hz-bands[i].half_width_hz<=bands[j].center_hz+bands[j].half_width_hz &&
               bands[j].center_hz-bands[j].half_width_hz<=bands[i].center_hz+bands[i].half_width_hz)return false;
        if(bands.size()==band_count_ && std::equal(bands.begin(),bands.end(),bands_.begin(),[](auto a,auto b){return a.center_hz==b.center_hz&&a.half_width_hz==b.half_width_hz;})) {
            if(l1_change)*l1_change=0;return true;
        }
        double sum=0,largest=0;
        for(std::size_t i=0;i<taps_.size();++i) {
            const auto value=coefficient(i,bands);
            if(!std::isfinite(value))return false;
            sum+=value;largest=std::max(largest,std::abs(value));
        }
        if(!std::isfinite(sum) || sum==0 ||
           !std::isfinite(largest/std::abs(sum)) ||
           largest/std::abs(sum)>std::numeric_limits<double>::max()/(4*taps_.size()))return false;
        double change=0;
        for(std::size_t i=0;i<taps_.size();++i) {
            const auto value=coefficient(i,bands)/sum;
            change=add_upper(change,upper(std::abs(value-taps_[i])));
            taps_[i]=value;
        }
        bands_.fill({});std::copy(bands.begin(),bands.end(),bands_.begin());band_count_=bands.size();
        center_=bands.front().center_hz;half_width_=bands.front().half_width_hz;
        if(l1_change)*l1_change=change;
        return true;
    }
    double center_hz()const noexcept{return center_;}
    double notch_center_hz(std::size_t i)const noexcept{return i<band_count_?bands_[i].center_hz:0;}
    double half_width_hz()const noexcept{return half_width_;}
    std::span<const double> coefficients()const noexcept{return taps_;}
    bool enabled()const noexcept{return !taps_.empty();}
    std::size_t delay()const noexcept{return taps_.size()/2;}
    std::size_t allocated_bytes()const noexcept {
        return taps_.capacity()*sizeof(double)+history_.capacity()*sizeof(float);
    }
    std::uint64_t output_samples()const noexcept{return output_;}
    // Continuous-band bound on |H(f)-1| for the centered real FIR kernel used
    // by push(), using its actual stored LEFT taps (which push mirrors).
    // This qualifies the linear response, not float PCM rounding or a noise
    // false-alarm model. No allocation or retained workspace is required.
    // Invalid bands/FP modes fail closed. A bounded work budget may return a
    // looser L1 certificate, so callers must check their required tolerance.
    double maximum_amplitude_error(double low_hz,double high_hz)const noexcept {
        constexpr auto infinity=std::numeric_limits<double>::infinity();
        if(!std::isfinite(low_hz)||!std::isfinite(high_hz)||low_hz<0||high_hz<low_hz)return infinity;
        if(!enabled())return 0;
        if(high_hz>rate_/2.||!certificate_environment())return infinity;
        constexpr double pi_high=0x1.921fb54442d19p+1;
        double triangle=upper(std::abs(taps_[delay()]-1)),second_derivative=0;
        for(std::size_t k=1;k<=delay();++k) {
            const auto coefficient=upper(2*std::abs(taps_[delay()-k]));
            const auto omega=upper(multiply_upper(2*pi_high,static_cast<double>(k))/rate_);
            triangle=add_upper(triangle,coefficient);
            second_derivative=add_upper(second_derivative,multiply_upper(coefficient,multiply_upper(omega,omega)));
        }
        if(!std::isfinite(triangle)||!std::isfinite(second_derivative))return infinity;
        if(low_hz==high_hz)return std::min(triangle,response_error(low_hz));
        // Linear interpolation error is <=sup|H''|*width^2/8 on EACH complete
        // cell. Endpoint bounds therefore certify all frequencies between
        // them; this is not a sampled-response claim. Aim for 1e-5 slack, but
        // cap work at about two million tap visits and 16384 cells.
        const auto span=upper(high_hz-low_hz);
        const auto requested=std::ceil(span*std::sqrt(second_derivative/(8e-5)));
        const auto limit=std::min<std::size_t>(16384,std::max<std::size_t>(1,2000000/std::max<std::size_t>(1,delay())));
        const auto cells=std::isfinite(requested)&&requested<static_cast<double>(limit)?
            std::max<std::size_t>(1,static_cast<std::size_t>(requested)):limit;
        double previous_frequency=low_hz,previous=response_error(low_hz),maximum=previous;
        for(std::size_t cell=1;cell<=cells;++cell) {
            const auto frequency=cell==cells?high_hz:low_hz+(high_hz-low_hz)*(static_cast<double>(cell)/static_cast<double>(cells));
            const auto response=response_error(frequency),width=upper(frequency-previous_frequency);
            const auto remainder=upper(multiply_upper(second_derivative,multiply_upper(width,width))/8);
            maximum=std::max(maximum,add_upper(std::max(previous,response),remainder));
            previous_frequency=frequency;previous=response;
        }
        return std::min(triangle,maximum);
    }
    // Continuous-band bound on |H(f)| from the same actual centered kernel.
    // This is a stopband certificate, not a promise about input energy, float
    // PCM rounding, or receiver false alarms. No state or allocation is added.
    double maximum_response(double low_hz,double high_hz)const noexcept {
        constexpr auto infinity=std::numeric_limits<double>::infinity();
        if(!std::isfinite(low_hz)||!std::isfinite(high_hz)||low_hz<0||high_hz<low_hz)return infinity;
        if(!enabled())return 1;
        if(high_hz>rate_/2.||!certificate_environment())return infinity;
        constexpr double pi_high=0x1.921fb54442d19p+1;
        double triangle=upper(std::abs(taps_[delay()])),second_derivative=0;
        for(std::size_t k=1;k<=delay();++k) {
            const auto coefficient=upper(2*std::abs(taps_[delay()-k]));
            const auto omega=upper(multiply_upper(2*pi_high,static_cast<double>(k))/rate_);
            triangle=add_upper(triangle,coefficient);
            second_derivative=add_upper(second_derivative,multiply_upper(coefficient,multiply_upper(omega,omega)));
        }
        if(!std::isfinite(triangle)||!std::isfinite(second_derivative))return infinity;
        if(low_hz==high_hz)return std::min(triangle,response_magnitude(low_hz));
        // Linear interpolation error is <=sup|H''|*width^2/8 on EACH complete
        // cell. Endpoint bounds therefore certify all frequencies between
        // them; this is not a sampled-response claim. Aim for 1e-5 slack, but
        // cap work at about two million tap visits and 16384 cells.
        const auto span=upper(high_hz-low_hz);
        const auto requested=std::ceil(span*std::sqrt(second_derivative/(8e-5)));
        const auto limit=std::min<std::size_t>(16384,std::max<std::size_t>(1,2000000/std::max<std::size_t>(1,delay())));
        const auto cells=std::isfinite(requested)&&requested<static_cast<double>(limit)?
            std::max<std::size_t>(1,static_cast<std::size_t>(requested)):limit;
        double previous_frequency=low_hz,previous=response_magnitude(low_hz),maximum=previous;
        for(std::size_t cell=1;cell<=cells;++cell) {
            const auto frequency=cell==cells?high_hz:low_hz+(high_hz-low_hz)*(static_cast<double>(cell)/static_cast<double>(cells));
            const auto response=response_magnitude(frequency),width=upper(frequency-previous_frequency);
            const auto remainder=upper(multiply_upper(second_derivative,multiply_upper(width,width))/8);
            maximum=std::max(maximum,add_upper(std::max(previous,response),remainder));
            previous_frequency=frequency;previous=response;
        }
        return std::min(triangle,maximum);
    }
    std::size_t push(std::span<const float> input,std::span<float> output) {
        if(output.size()<input.size())throw Error("helper filter output span is too small");
        if(!enabled()) {std::copy(input.begin(),input.end(),output.begin());output_+=input.size();return input.size();}
        std::size_t count=0;
        for(const auto sample:input) {
            if(!std::isfinite(sample))throw Error("helper filter input contains a nonfinite sample");
            history_[next_]=sample;const auto newest=next_;
            next_=(next_+1)%history_.size();
            if(seen_<history_.size())++seen_;
            if(seen_<history_.size())continue;
            const auto size=history_.size(),middle=delay();
            const auto center=newest>=middle?newest-middle:newest+size-middle;
            auto left=newest,right=newest+1;if(right==size)right=0;
            double value=taps_[middle]*history_[center];
            // Walk the same symmetric pairs in the same accumulation order.
            // Keep variable-length modulo outside the per-tap hot loop.
            for(std::size_t i=0;i<middle;++i) {
                value+=taps_[i]*(static_cast<double>(history_[left])+history_[right]);
                if(left==0)left=size-1;else --left;
                if(++right==size)right=0;
            }
            output[count++]=static_cast<float>(value);++output_;
        }
        return count;
    }
};
}
