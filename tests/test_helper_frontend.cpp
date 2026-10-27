#include "datapump/helper_frontend.hpp"
#include "datapump/types.hpp"
#include "../src/live_helper_frontend.hpp"
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
void realtime_retuning_preserves_phase() {
    using Translator=helper::AnalyticFrequencyTranslator;
    constexpr std::uint32_t rate=48000;
    constexpr std::uint64_t origin=90000001;
    Translator t({rate,0,{4000,6000},origin,false,true},Translator::workspace_required());
    std::vector<float> input(4096),output(4096);
    for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(std::cos(tau*5000*(origin+i)/rate));
    t.push(input,output,origin);
    auto at=origin+input.size();
    for(const auto correction:{-3000.,-3000.125,-2999.75,0.}) {
        const auto phase=t.phase_at(at);
        check(t.track_frequency(correction),"continuous helper frequency update was declined");
        check(std::abs(std::remainder(t.phase_at(at)-phase,tau))<1e-10,
              "helper update reset the established NCO phase");
        check(std::abs(std::remainder(t.phase_at(at+1234)-phase-tau*correction*1234/rate,tau))<1e-10,
              "helper update did not apply its measured frequency slope");
        t.push(input,output,at);at+=input.size();
    }
    const auto phase=t.phase_at(at);
    check(!t.track_frequency(-10000),"helper update moved payload through DC");
    check(t.phase_at(at)==phase,"declined helper update changed phase");
}
void two_tone_frontend_removes_auxiliary_before_decimation() {
    using Frontend=live::detail::HelperInputFrontend;
    constexpr std::uint32_t raw_rate=8000,logical_rate=2000;
    constexpr std::uint64_t origin=37;
    constexpr double pilot=1650,data=500;
    modem::Config config;config.sample_rate=logical_rate;config.carrier_hz=data;
    config.bandwidth_hz=200;config.spreading_factor=16;config.pulse_shaping=false;
    helper::Reference reference;reference.sample_rate=raw_rate;reference.frequency_valid=true;
    reference.frequency_hz=pilot;reference.frequency_uncertainty_hz=.01;
    const auto grant=Frontend::reservation_bound(raw_rate,logical_rate,pilot);
    check(grant>Frontend::reservation_bound(raw_rate,logical_rate),
          "two-tone physical notch did not prepay its one larger-rate FIR");
    check(!Frontend::logical_notch_supported(logical_rate,pilot) &&
          Frontend::logical_notch_supported(raw_rate,pilot) &&
          !Frontend::logical_notch_supported(raw_rate,625),
          "notch placement omitted the group's lower auxiliary edge");
    struct Output {std::uint64_t first=0;std::vector<float> samples;};
    const auto collect=[&](bool helper_enabled,std::size_t chunk) {
        Frontend frontend(grant,71);frontend.admit_realtime(config,pilot,reference,raw_rate,3990,origin);
        check(frontend.active() && frontend.bytes()<=grant,"safe two-tone physical view was not admitted");
        Output result;std::uint64_t next=0;
        std::vector<float> input(3*raw_rate);
        for(std::size_t i=0;i<input.size();++i) {
            const auto t=static_cast<double>(origin+i)/raw_rate;
            double value=.02*std::cos(tau*data*t);
            if(helper_enabled) {
                // 90/10 power, with an independent shaped auxiliary reversal
                // each dwell. Natural polarity alternation avoids a boundary flip.
                const auto dwell=static_cast<std::uint64_t>(std::floor(t/.4));
                const auto local=t-.4*dwell;
                auto punctuation=local<.236?0.:local>=.244?std::numbers::pi:
                    .5*std::numbers::pi*(1-std::cos(std::numbers::pi*(local-.236)/.008));
                punctuation+=(dwell&1)?std::numbers::pi:0.;
                value+=.6*std::cos(tau*pilot*t)+.2*std::cos(tau*(pilot-400)*t+punctuation);
            }
            input[i]=static_cast<float>(value);
        }
        for(std::size_t at=0;at<input.size();) {
            const auto count=std::min(chunk,input.size()-at),end=origin+at+count;
            frontend.push(std::span<const float>(input).subspan(at,count),origin+at);at+=count;
            const auto frontier=end/raw_rate*logical_rate+(end%raw_rate)*logical_rate/raw_rate;
            for(const auto& block:frontend.take(frontier,reference,end)) {
                if(result.samples.empty())result.first=next=block.first_sample;
                check(block.first_sample==next && block.raw_captured_end==end && block.view->budget_serial==71,
                      "two-tone physical filtering changed sample dates, frontier or view identity");
                next+=block.samples.size();result.samples.insert(result.samples.end(),block.samples.begin(),block.samples.end());
            }
            check(frontend.bytes()<=grant,"two-tone capture grew beyond its fixed workspace grant");
        }
        check(frontend.take(UINT64_MAX,reference,origin+input.size()).empty(),
              "two-tone physical FIR published unobserved EOF samples");
        return result;
    };
    const auto baseline=collect(false,137),mixed=collect(true,137),chunked=collect(true,1024);
    check(baseline.first==mixed.first && mixed.first==chunked.first &&
          baseline.samples.size()==mixed.samples.size() && mixed.samples==chunked.samples,
          "two-tone filtering changed payload count/dates or depended on input chunking");
    check(mixed.samples.size()>2*logical_rate,"two-tone component did not retain a useful measured aperture");
    const auto begin=mixed.samples.size()-2*logical_rate;
    double interference=0;
    for(std::size_t i=begin;i<mixed.samples.size();++i) {
        const auto difference=static_cast<double>(mixed.samples[i])-baseline.samples[i];
        interference+=difference*difference;
    }
    const auto leakage=std::sqrt(interference/(mixed.samples.size()-begin));
    const auto gain=std::abs(projection(std::span<const float>(mixed.samples).subspan(begin),
        logical_rate,data,mixed.first+begin));
    check(leakage<.0005 && std::abs(gain-.02)<.0001,
          "auxiliary/pilot aliases survived the single physical notch or corrupted DATA");
    std::cout<<"two_tone_physical payload_gain="<<gain<<" helper_residual_rms="<<leakage
             <<" first="<<mixed.first<<" output_samples="<<mixed.samples.size()<<" workspace_grant="<<grant<<'\n';

    // The old pilot-only geometry looked safe at 1250 Hz. Its new auxiliary
    // approaches the full DATA hull and must fail before any PCM is committed.
    auto unsafe=reference;unsafe.frequency_hz=1250;
    Frontend overlap(Frontend::reservation_bound(raw_rate,logical_rate,unsafe.frequency_hz));
    rejects([&]{overlap.admit_realtime(config,unsafe.frequency_hz,unsafe,raw_rate,3990,origin);},
            "auxiliary-overlapping DATA was admitted using a pilot-only passband certificate");
    check(!overlap.active() && overlap.bytes()==sizeof(Frontend),"declined geometry retained a partial PCM owner");
}
void raw_notch_domain_includes_clock_scaled_auxiliary() {
    modem::Config config;config.sample_rate=4000;config.carrier_hz=500;config.bandwidth_hz=1000;
    config.spreading_factor=16;config.pulse_shaping=false;
    config.oscillator_search=modem::OscillatorSearchConfig{{100,.5},{.5,.1},30000000,3,
        modem::OscillatorReference::independent_audio,modem::OscillatorSideband::upper};
    const auto domain=live::detail::raw_helper_notch_domain(config,2500,3990,.0003);
    const auto bands=live::detail::raw_helper_notch_bands(domain,{2499.8,2500.2});
    check(domain.valid && bands.valid && bands.data.lower_hz==0,
          "two-tone raw domain lost the complete folded DATA image");
    for(const auto clock:{domain.one_plus_clock.lower,1.,domain.one_plus_clock.upper})
        for(const auto hz:{2499.8,2500.2})for(const auto edge:{-550.,150.}) {
            const auto truth=hz+edge*clock;
            check(bands.helper.lower_hz<=truth && truth<=bands.helper.upper_hz,
                  "raw stopband omitted an original-clock auxiliary/pilot boundary");
        }
    check(bands.helper.lower_hz<1950 && bands.helper.upper_hz>2650,
          "raw notch still certified only the old pilot metadata band");
}
}
int main() {
    try {
        coarse_seed_is_wide_bounded_and_source_independent();coarse_noise_has_no_persistent_peak();
        translation_removes_offset_without_a_mixer_image();capture_limits_and_exact_delay();
        realtime_retuning_preserves_phase();two_tone_frontend_removes_auxiliary_before_decimation();
        raw_notch_domain_includes_clock_scaled_auxiliary();
        std::cout<<"helper coarse discovery and analytic frequency translation passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
