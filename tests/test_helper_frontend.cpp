#include "datapump/helper_frontend.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <source_location>
#include <string>
#include <vector>

using namespace datapump;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool condition,const char* message,
           std::source_location where=std::source_location::current()) {
    if(!condition)throw Error(std::string(where.file_name())+":"+std::to_string(where.line())+" "+message);
}
template<class F>void rejects(F f,const char* message) {
    bool rejected=false;try{f();}catch(const Error&){rejected=true;}check(rejected,message);
}
std::complex<double> projection(std::span<const float> samples,std::uint32_t rate,
                                double hz,std::uint64_t first) {
    std::complex<double> value{};
    for(std::size_t i=0;i<samples.size();++i)
        value+=static_cast<double>(samples[i])*std::polar(1.,-tau*hz*(first+i)/rate);
    return value*(2./samples.size());
}
void coarse_seed_is_wide_bounded_and_source_independent() {
    constexpr std::uint32_t rate=48000;
    helper::CoarseToneSearch::Config config{rate,2062.5,9000,8192,3};
    check(helper::CoarseToneSearch::seed_half_width_bound(config)==13.71875,
          "coarse planning bound changed its FFT-bin/guard envelope");
    const helper::CoarseToneSearch::Config acceptance{48960,2062.5,9000,4096,3};
    check(helper::CoarseToneSearch::seed_half_width_bound(acceptance)==25.90625,
          "actual HF capture planning bound missed its fixed FFT geometry");
    const auto grant=helper::CoarseToneSearch::workspace_required(config);
    rejects([&]{helper::CoarseToneSearch bad(config,grant-1);},"coarse workspace grant was ignored");
    constexpr std::uint64_t origin=10003;
    helper::CoarseToneSearch search(config,grant,origin);
    check(search.workspace_bytes()<=grant,"coarse allocation exceeded its grant");
    const auto band=search.capture_band();
    check(band.lower_hz==10 && band.upper_hz==11062.5,"coarse search did not clip physical positive capture");
    const auto count=6*config.fft_size;
    std::vector<float> input(count);
    std::mt19937 rng(9127);std::normal_distribution<double> noise(0,.006);
    for(std::size_t i=0;i<count;++i) {
        const auto t=static_cast<double>(origin+i)/rate;
        input[i]=static_cast<float>(.7*std::cos(tau*5062.5*t+.3)+
                                     .03*std::cos(tau*4500*t)+noise(rng));
    }
    std::optional<helper::ToneCandidate> last;
    for(std::size_t i=0;i<count;i+=137) {
        const auto n=std::min<std::size_t>(137,count-i);
        search.push(std::span<const float>(input).subspan(i,n),origin+i);
        if(auto candidate=search.take_candidate()) {
            check(candidate->end_sample<=origin+i+n,"coarse seed used future PCM");
            check(candidate->first_sample==origin,"coarse support lost absolute capture coordinates");
            check(candidate->coherent_support>=3,"coarse seed did not require independent windows");
            check(candidate->frequency_half_width_hz<20,"coarse seed cannot fit the independent narrow observer");
            check(candidate->frequency_half_width_hz==helper::CoarseToneSearch::seed_half_width_bound(config),
                  "coarse runtime publication diverged from its planning bound");
            check(candidate->peak_to_noise_db>=6,"coarse seed missed its diagnostic level gate");
            last=candidate;
        }
    }
    check(last.has_value(),"+3000 Hz helper was not discovered outside the old +/-100 Hz limit");
    check(std::abs(last->frequency_hz-5062.5)<1,"coarse helper frequency missed the physical tone");
    check(search.transformed_windows()==6,"coarse FFT count did not use disjoint fixed windows");
    rejects([&]{search.push(std::span<const float>(input).first(1),origin+count+1);},"coarse gap silently reset state");
    search.reset(900000);
    check(!search.take_candidate() && search.transformed_windows()==0,"explicit coarse reset retained a stale seed");
    std::fill(input.begin(),input.end(),0);
    search.push(input,900000);check(!search.take_candidate(),"zero PCM produced a coarse helper seed");
    config.sample_rate=4800000;
    rejects([&]{(void)helper::CoarseToneSearch::seed_half_width_bound(config);},
            "coarse planning bound skipped the existing precision validation");
    rejects([&]{helper::CoarseToneSearch bad(config,1024*1024);},"insufficient FFT resolution pretended narrow precision");
}
void coarse_noise_has_no_persistent_peak() {
    helper::CoarseToneSearch::Config config{48000,2062.5,9000,8192,3};
    helper::CoarseToneSearch search(config,helper::CoarseToneSearch::workspace_required(config));
    std::vector<float> block(config.fft_size);
    std::mt19937 rng(88241);std::normal_distribution<float> noise(0,.1f);
    for(std::uint64_t window=0;window<32;++window) {
        for(auto& x:block)x=noise(rng);
        search.push(block,window*block.size());
        check(!search.take_candidate(),"fixed noise control yielded a persistent strongest-tone seed");
    }
}
std::vector<float> translate(const helper::AnalyticFrequencyTranslator::Config& config,
                             std::span<const float> input,std::size_t chunk) {
    helper::AnalyticFrequencyTranslator translator(config,helper::AnalyticFrequencyTranslator::workspace_required());
    check(translator.workspace_bytes()<=helper::AnalyticFrequencyTranslator::workspace_required(),"translator grant exceeded");
    check(translator.maximum_amplitude_error()<=.001 && translator.image_leakage_bound()<=.001,
          "translator did not qualify its actual stored FIR");
    std::vector<float> output(input.size());
    for(std::size_t i=0;i<input.size();i+=chunk) {
        const auto n=std::min(chunk,input.size()-i);
        translator.push(input.subspan(i,n),std::span<float>(output).subspan(i,n),config.first_sample+i);
    }
    return output;
}
void translation_removes_offset_without_a_mixer_image() {
    constexpr std::uint32_t rate=48000;
    constexpr std::uint64_t origin=170003;
    constexpr std::size_t count=49152,skip=count-rate;
    constexpr double input_hz=4500,phase=.7;
    std::vector<float> input(count);
    for(std::size_t i=0;i<count;++i)input[i]=static_cast<float>(std::cos(tau*input_hz*(origin+i)/rate+phase));
    helper::AnalyticFrequencyTranslator::Config config{rate,-3000,{4400,5200},origin,false};
    const auto output=translate(config,input,input.size());
    const auto chunked=translate(config,input,137);
    check(output==chunked,"translation phase/filter state changed at caller chunk boundaries");
    helper::AnalyticFrequencyTranslator mapping(config,helper::AnalyticFrequencyTranslator::workspace_required());
    check(mapping.delay()==512,"translator delay changed without an explicit coordinate contract");
    check(mapping.output_band().lower_hz==1400 && mapping.output_band().upper_hz==2200,"translator moved its passband incorrectly");
    const auto measured=projection(std::span<const float>(output).subspan(skip),rate,1500,origin+skip);
    const auto expected=std::polar(1.,phase-tau*1500*mapping.delay()/rate);
    check(std::abs(measured-expected)<.0011,"+3000 Hz correction changed payload gain or the absolute phase convention");
    const auto image=projection(std::span<const float>(output).subspan(skip),rate,7500,origin+skip);
    check(std::abs(image)<.0011,"real mixing image leaked into translated PCM");
    const auto uncorrected=projection(std::span<const float>(input).subspan(skip),rate,1500,origin+skip);
    check(std::abs(uncorrected)<.001 && std::abs(measured)>.999,"correction did not improve narrowband carrier acquisition");
    const auto k=origin+3000;
    check(std::abs(std::remainder(mapping.phase_at(k+mapping.delay())-tau*config.correction_hz*k/rate,tau))<1e-9,
          "reference phase mapping did not use the physical input coordinate");
    config.conjugate=true;config.correction_hz=6000;config.input_band={4490,4510};
    const auto reversed=translate(config,input,193);
    const auto conjugated=projection(std::span<const float>(reversed).subspan(skip),rate,1500,origin+skip);
    const auto reversed_expected=std::polar(1.,-phase-tau*1500*mapping.delay()/rate);
    check(std::abs(conjugated-reversed_expected)<.0011,"conjugate branch lost signed-frequency or phase mapping");
    check(std::abs(projection(std::span<const float>(reversed).subspan(skip),rate,10500,origin+skip))<.0011,
          "conjugate branch retained its analytic image");
}
void capture_limits_and_exact_delay() {
    for(const auto rate:{24480U,48000U,57600U}) {
        helper::AnalyticFrequencyTranslator::Config c{rate,-3000,{4400,5200},1000,false};
        helper::AnalyticFrequencyTranslator t(c,helper::AnalyticFrequencyTranslator::workspace_required());
        check(t.maximum_amplitude_error()<=.001,"actual HF physical-rate passband failed qualification");
    }
    const auto grant=helper::AnalyticFrequencyTranslator::workspace_required();
    helper::AnalyticFrequencyTranslator::Config c{48000,0,{100,10000},0,false};
    rejects([&]{helper::AnalyticFrequencyTranslator t(c,grant-1);},"translator workspace grant was ignored");
    helper::AnalyticFrequencyTranslator delay(c,grant);
    std::vector<float> input(3000),output(3000);
    for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(std::sin(.13*i));
    delay.push(input,output,0);
    for(std::size_t i=0;i<output.size();++i)
        check(output[i]==(i<delay.delay()?0:input[i-delay.delay()]),"zero correction was not an exact constant delay");
    rejects([&]{delay.push(std::span<const float>(input).first(1),output,3001);},"translator silently reset after a gap");
    c.correction_hz=-3000;c.input_band={1000,2000};
    rejects([&]{helper::AnalyticFrequencyTranslator t(c,grant);},"translator accepted a payload crossing DC");
    c.input_band={1,5000};c.correction_hz=1000;
    rejects([&]{helper::AnalyticFrequencyTranslator t(c,grant);},"unsupported near-DC Hilbert band pretended precision");
    c.input_band={4400,24001};
    rejects([&]{helper::AnalyticFrequencyTranslator t(c,grant);},"translator accepted an uncaptured input band");
    c.input_band={4400,5200};c.correction_hz=20000;
    rejects([&]{helper::AnalyticFrequencyTranslator t(c,grant);},"translator accepted output above Nyquist");
    input[0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{delay.push(std::span<const float>(input).first(1),output,3000);},"translator accepted nonfinite PCM");
}
}
int main() {
    try {
        coarse_seed_is_wide_bounded_and_source_independent();coarse_noise_has_no_persistent_peak();
        translation_removes_offset_without_a_mixer_image();capture_limits_and_exact_delay();
        std::cout<<"helper coarse discovery and analytic frequency translation passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
