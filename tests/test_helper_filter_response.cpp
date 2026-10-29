#include "datapump/helper_filter.hpp"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>

using namespace datapump;
namespace {
void check(bool condition,const char* message) {if(!condition)throw Error(message);}
struct Impulse {
    std::vector<float> kernel;
    double rounding=0;
};
Impulse observed_kernel(std::uint32_t rate,double center,double half_width=225) {
    helper::PayloadFilter filter(rate,center,half_width);
    const auto delay=filter.delay();
    // Every zero here is actually observed PCM. Center the impulse far enough
    // into it that every returned coefficient owns a complete physical window.
    std::vector<float> input(4*delay+1),output(input.size());input[2*delay]=1;
    const auto count=filter.push(input,output);output.resize(count);
    check(count==2*delay+1,"observed impulse did not cover the complete centered kernel");
    double mass=0;for(const auto value:output)mass+=std::abs(static_cast<double>(value));
    constexpr auto u=std::numeric_limits<float>::epsilon()/2;
    // Each returned impulse coefficient has just one final binary32 rounding.
    // Bound that separate PCM effect; it is not part of the linear certificate.
    const auto rounding=mass*u/(1-u)+64*static_cast<double>(output.size())*std::numeric_limits<double>::epsilon();
    return {std::move(output),rounding};
}
Impulse observed_kernel(std::uint32_t rate,std::span<const helper::PayloadFilter::NotchBand> bands) {
    helper::PayloadFilter filter(rate,bands);
    const auto delay=filter.delay();std::vector<float> input(4*delay+1),output(input.size());input[2*delay]=1;
    const auto count=filter.push(input,output);output.resize(count);
    check(count==2*delay+1,"observed multi-notch impulse did not cover complete centered kernel");
    double mass=0;for(const auto value:output)mass+=std::abs(static_cast<double>(value));
    constexpr auto u=std::numeric_limits<float>::epsilon()/2;
    return {std::move(output),mass*u/(1-u)+64*static_cast<double>(output.size())*std::numeric_limits<double>::epsilon()};
}
std::complex<double> observed_gain(const Impulse& impulse,std::uint32_t rate,double frequency) {
    std::complex<double> gain{};
    const auto middle=impulse.kernel.size()/2;
    for(std::size_t i=0;i<impulse.kernel.size();++i) {
        const auto phase=2*std::numbers::pi*frequency*(static_cast<double>(i)-static_cast<double>(middle))/rate;
        gain+=static_cast<double>(impulse.kernel[i])*std::polar(1.,phase);
    }
    return gain;
}
double observed_error(const Impulse& impulse,std::uint32_t rate,double frequency) {
    return std::abs(observed_gain(impulse,rate,frequency)-1.);
}
void continuous_response_qualifies_only_safe_bands() {
    for(const auto rate:{2000U,6120U,8000U,48000U}) {
        const auto center=rate==2000?750.:rate==6120?2065.:2450.;
        helper::PayloadFilter filter(rate,center);
        const auto impulse=observed_kernel(rate,center);
        const auto low=rate==2000?100.:rate==6120?1375.:1000.;
        const auto high=center-450;
        const auto bound=filter.maximum_amplitude_error(low,high);
        check(std::isfinite(bound)&&bound<=.001,"well separated full data band failed the fixed amplitude criterion");
        for(unsigned i=0;i<=1024;++i) {
            const auto frequency=low+(high-low)*i/1024.;
            check(observed_error(impulse,rate,frequency)<=bound+impulse.rounding,
                  "continuous certificate missed the actual impulse response between qualification nodes");
        }
        const auto unsafe=filter.maximum_amplitude_error(center-100,center+100);
        check(std::isfinite(unsafe)&&unsafe>.9,"notch-overlapping payload band incorrectly qualified");
        check(observed_error(impulse,rate,center)<=unsafe+impulse.rounding,
              "stopband certificate missed the actual impulse response");
        const auto complete=filter.maximum_amplitude_error(0,rate/2.);
        check(std::isfinite(complete)&&complete>.9&&observed_error(impulse,rate,center)<=complete+impulse.rounding,
              "safe outer endpoints concealed an interior stopband");
        const auto point=filter.maximum_amplitude_error(high,high);
        check(std::isfinite(point)&&observed_error(impulse,rate,high)<=point+impulse.rounding,
              "degenerate frequency interval was not conservatively bounded");
    }
}
void shifted_full_band_needs_its_own_certificate() {
    constexpr std::uint32_t rate=6120;
    constexpr double center=2065;
    helper::PayloadFilter filter(rate,center);
    // Nominally narrow payload fits. A 100 Hz RF/clock/profile expansion can
    // enter the FIR transition, so checking just the nominal edge is unsafe.
    check(filter.maximum_amplitude_error(1400,1615)<=.001,"nominal HF data band did not qualify");
    const auto expanded=filter.maximum_amplitude_error(1300,1815);
    check(expanded>.001,"full shifted data band improperly reused nominal passband qualification");
    const auto impulse=observed_kernel(rate,center);
    check(observed_error(impulse,rate,1815)<=expanded+impulse.rounding,
          "shifted-band certificate missed actual transition loss");
}
void full_hf_controller_band() {
    // The user's HF Controller profile: Fs=6120, carrier=1500, Rate=36,
    // outer DSSS=10, SF=256. Its chip sampling gives 225 Hz RRC support.
    // The fixed 1500 W / 4 W helper plan places the adjacent tone at 2062.5.
    constexpr std::uint32_t rate=6120;
    constexpr double carrier=1500,half_band=112.5,helper_hz=2062.5;
    constexpr double rf_allowance=100,clock_allowance_ppm=300;
    constexpr auto expansion=rf_allowance+(carrier+half_band)*clock_allowance_ppm*1e-6;
    constexpr auto low=carrier-half_band-expansion,high=carrier+half_band+expansion;
    helper::PayloadFilter filter(rate,helper_hz);
    const auto bound=filter.maximum_amplitude_error(low,high);
    check(std::isfinite(bound)&&bound<=.001,"full HF data/clock/RF union failed the fixed amplitude criterion");
    const auto impulse=observed_kernel(rate,helper_hz);
    for(unsigned i=0;i<=1024;++i)
        check(observed_error(impulse,rate,low+(high-low)*i/1024.)<=bound+impulse.rounding,
              "full HF certificate missed actual shifted payload response");
}
void three_tone_group_uses_one_fixed_fir() {
    // Fixed low/aux group is 550 and 950 +/-150 Hz; the high pilot is 2450.
    // The wider FIR design cutoff leaves 75 Hz transition on either side.
    // DATA occupies 1500 +/-112.5, with room for independent RF/audio allowance.
    constexpr double pilot=2450,center=750,half_width=425;
    constexpr double expansion=100+1612.5*300e-6;
    for(const auto rate:{6120U,8000U,48000U}) {
        const std::array bands{helper::PayloadFilter::NotchBand{center,half_width},
            helper::PayloadFilter::NotchBand{pilot,225}};
        helper::PayloadFilter group(rate,bands),legacy(rate,pilot);
        check(group.delay()==legacy.delay() && group.allocated_bytes()==legacy.allocated_bytes() &&
              group.coefficients().size()==legacy.coefficients().size(),
              "three-tone suppression increased FIR length, delay or fixed storage");
        check(group.center_hz()==center && group.half_width_hz()==half_width && legacy.half_width_hz()==225,
              "three-tone group geometry changed the legacy filter default");
        const auto payload=group.maximum_amplitude_error(1387.5-expansion,1612.5+expansion);
        const auto stopped_low=group.maximum_response(400,1100);
        const auto stopped_high=group.maximum_response(pilot-150,pilot+150);
        check(payload<=.001 && stopped_low<=.001 && stopped_high<=.001,
              "one three-tone FIR did not certify DATA and both disjoint helper groups");
        const auto impulse=observed_kernel(rate,bands);
        for(unsigned i=0;i<=1024;++i) {
            const auto frequency=400+700.*i/1024.;
            check(std::abs(observed_gain(impulse,rate,frequency))<=stopped_low+impulse.rounding,
                  "three-tone certificate missed actual rejection inside the low/aux group band");
            const auto high_frequency=pilot-150+300.*i/1024.;
            check(std::abs(observed_gain(impulse,rate,high_frequency))<=stopped_high+impulse.rounding,
                  "three-tone certificate missed actual rejection inside the high-pilot band");
        }
        check(group.maximum_response(550,550)<=.001 && group.maximum_response(950,950)<=.001 &&
              group.maximum_response(pilot,pilot)<=.001,
              "one of the fixed 550/950/high pilots remained audible");
        check(group.maximum_response(325,325)>.49 && group.maximum_response(1175,1175)>.49 &&
              group.maximum_response(pilot-225,pilot-225)>.49 &&
              group.maximum_response(pilot+225,pilot+225)>.49,
              "design cutoff was incorrectly treated as an already-rejected stopband edge");
        check(group.maximum_amplitude_error(1120,1430)>.9,
              "DATA overlapping a stopband transition was falsely certified safe");
        check(legacy.maximum_response(950,950)>.99 &&
              std::abs(observed_gain(impulse,rate,950))<.001,
              "three-tone test did not distinguish the old auxiliary leak from the new notch group");
    }
}
void invalid_or_unsupported_qualification_fails_closed() {
    helper::PayloadFilter filter(8000,2450),disabled;
    const auto infinity=std::numeric_limits<double>::infinity();
    for(const auto& band:{std::pair{-1.,100.},std::pair{100.,99.},std::pair{0.,4000.01},
                         std::pair{0.,infinity},std::pair{std::numeric_limits<double>::quiet_NaN(),100.}})
        check(std::isinf(filter.maximum_amplitude_error(band.first,band.second)),"invalid response band did not fail closed");
    check(disabled.maximum_amplitude_error(0,4000)==0,"disabled filter changed unity response");
    const auto previous=std::fegetround();
    if(std::fesetround(FE_UPWARD)==0) {
        const auto result=filter.maximum_amplitude_error(1000,1500);
        std::fesetround(previous);
        check(std::isinf(result),"changed floating-point rounding mode retained a certificate");
    }
}
}
int main() {
    try {continuous_response_qualifies_only_safe_bands();shifted_full_band_needs_its_own_certificate();full_hf_controller_band();
        three_tone_group_uses_one_fixed_fir();invalid_or_unsupported_qualification_fails_closed();
        std::cout<<"continuous helper FIR response certificates passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
