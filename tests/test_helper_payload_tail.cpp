#include "../src/live_receptions.hpp"
#include "../src/live_helper_frontend.hpp"
#include "datapump/types.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/resampler.hpp"
#include "datapump/live.hpp"
#include "datapump/pattern_search.hpp"
#include "../src/audio_trace.hpp"
#include "../src/live_simulation_clock.hpp"
#include <array>
#include <bit>
#include <cfenv>
#include <limits>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <cstdlib>

using namespace datapump;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Bytes payload_bits() {
    constexpr std::uint64_t word=0xD6E4B39A1275CC07ULL;
    Bytes result;for(unsigned i=64;i;--i)result.push_back(static_cast<std::uint8_t>((word>>(i-1))&1));
    return result;
}
std::string binary(const Bytes& bits) {std::string result;for(const auto bit:bits)result+=bit?'1':'0';return result;}
void history_preserves_complete_tail() {
    using History=live::detail::ReceptionHistory;
    const auto expected=payload_bits();Bytes visible;
    History history;std::uint64_t ids=0;
    auto helper=History::Candidate{};helper.id=1;helper.family=9;helper.first=1000;
    helper.symbol_samples=256;helper.end=helper.first+expected.size()*helper.symbol_samples;
    helper.frequency=500;helper.frequency_tolerance=1;helper.absence_samples=6000;
    helper.score=.8*static_cast<double>(expected.size()*helper.symbol_samples);helper.timing_tolerance=5;
    helper.helper_assisted=true;
    const auto first=history.observe(helper,[&]{return ++ids;});if(first.selected)visible=expected;
    std::cout<<"stage=helper_pending selected="<<first.selected<<" signal_id="<<first.signal_id
             <<" revision="<<first.revision<<" visible_count="<<visible.size()<<" visible="<<binary(visible)<<'\n';
    auto ordinary=helper;ordinary.id=2;ordinary.end-=2*ordinary.symbol_samples;
    ordinary.score=.7*static_cast<double>((expected.size()-2)*ordinary.symbol_samples);ordinary.helper_assisted=false;ordinary.complete=true;
    const auto truncated=history.observe(ordinary,[&]{return ++ids;});
    if(truncated.selected)visible.assign(expected.begin(),expected.end()-2);
    std::cout<<"stage=ordinary_short_end selected="<<truncated.selected<<" signal_id="<<truncated.signal_id
             <<" revision="<<truncated.revision<<" visible_count="<<visible.size()<<" visible="<<binary(visible)<<'\n';
    helper.complete=true;
    const auto completed=history.observe(helper,[&]{return ++ids;});if(completed.selected)visible=expected;
    std::cout<<"stage=helper_full_end selected="<<completed.selected<<" signal_id="<<completed.signal_id
             <<" revision="<<completed.revision<<" visible_count="<<visible.size()<<" visible="<<binary(visible)<<'\n';
    check(first.selected&&first.signal_id==truncated.signal_id&&first.signal_id==completed.signal_id&&ids==1,
          "compatible helper/ordinary updates changed the pending identity");
    check(visible==expected,"ordinary/helper presentation arbitration permanently discarded an accepted payload tail");
    auto stronger=helper;stronger.id=3;stronger.helper_assisted=false;stronger.score=.9*static_cast<double>(expected.size()*helper.symbol_samples);
    const auto revised=history.observe(stronger,[&]{return ++ids;});
    check(revised.selected&&revised.signal_id==first.signal_id&&revised.revision>completed.revision&&ids==1,
          "legitimately stronger ordinary reception could not revise the same completed row");
    std::cout<<"stage=ordinary_full_stronger selected="<<revised.selected<<" signal_id="<<revised.signal_id
             <<" revision="<<revised.revision<<" visible_count="<<visible.size()<<'\n';
}
// The raw two-tone helper is valid at8kHz but its group aliases across
// the wanted band at2kHz. A post-decimation notch cannot represent it.
void frontend_physical_notch() {
    constexpr std::uint32_t raw_rate=8000,logical_rate=2000;
    constexpr double helper_hz=1650,auxiliary_hz=helper_hz-400;
    modem::Config c;c.sample_rate=logical_rate;c.carrier_hz=500;c.bandwidth_hz=200;
    c.spreading_factor=16;c.pulse_shaping=false;
    helper::Reference ref;ref.sample_rate=raw_rate;ref.frequency_valid=true;
    ref.frequency_hz=helper_hz;ref.frequency_uncertainty_hz=.01;
    using Frontend=live::detail::HelperInputFrontend;
    const auto old_grant=Frontend::reservation_bound(raw_rate,logical_rate);
    const auto grant=Frontend::reservation_bound(raw_rate,logical_rate,helper_hz);
    check(grant>old_grant,"physical-rate notch did not prepay its larger retained FIR");
    Frontend short_grant(old_grant);bool declined=false;
    try {short_grant.admit_realtime(c,helper_hz,ref,raw_rate,3990,37);}
    catch(const Error&){declined=true;}
    check(declined&&!short_grant.active()&&short_grant.bytes()==sizeof(Frontend),
          "unfunded physical notch allocated or admitted an input basis");
    helper::PayloadFilter filter(raw_rate,helper_hz-200,425);
    const auto suppression=filter.maximum_response(auxiliary_hz-150,helper_hz+150);
    const auto passband=filter.maximum_amplitude_error(368,632);
    check(suppression<=.001&&passband<=.001,
          "actual physical FIR did not certify the full two-tone helper and wanted payload bands");
    helper::Options helper_options;helper_options.power_w=100;helper_options.callsign="N0CALL";
    helper::Plan placement;placement.frequency_hz=helper_hz;placement.auxiliary_frequency_hz=auxiliary_hz;
    placement.helper_gain=.5;placement.data_gain=0;placement.effective_power_w=helper_options.power_w;
    constexpr std::uint64_t raw_first=37;
    const auto quantum=raw_rate/std::gcd(raw_rate,logical_rate);
    const auto remainder=(raw_first+filter.delay())%quantum;
    const auto admitted=raw_first+(remainder?quantum-remainder:0);
    const auto center=admitted+filter.delay();
    const auto origin=center/raw_rate*logical_rate+(center%raw_rate)*logical_rate/raw_rate;
    audio::Resampler support(raw_rate,logical_rate);
    const auto warm=static_cast<std::uint64_t>(std::ceil((
        2.L*helper::AnalyticFrequencyTranslator::group_delay/raw_rate+
        static_cast<long double>(support.source_lookahead_seconds()))*logical_rate))+1;
    const auto expected_first=origin+warm;
    const auto apply=[&](std::size_t chunk) {
        Frontend frontend(grant,19);frontend.admit_realtime(c,helper_hz,ref,raw_rate,3990,raw_first);
        check(frontend.active()&&frontend.bytes()<=grant,"funded physical notch did not admit one immutable view");
        helper::Generator generator(helper_options,placement,1800000000,true);
        std::vector<float> result;std::uint64_t next=0;
        std::vector<float> input(3*raw_rate);
        for(std::size_t i=0;i<input.size();++i) {
            const auto t=static_cast<double>(raw_first+i)/raw_rate;
            input[i]=static_cast<float>(.02*std::cos(2*std::numbers::pi*500*t)+
                generator.mix(0,static_cast<long double>(raw_first+i)/raw_rate,raw_rate));
        }
        for(std::size_t at=0;at<input.size();) {
            const auto count=std::min(chunk,input.size()-at),end=raw_first+at+count;
            frontend.push(std::span(input).subspan(at,count),raw_first+at);at+=count;
            auto blocks=frontend.take(end/raw_rate*logical_rate+(end%raw_rate)*logical_rate/raw_rate,ref,end);
            for(const auto& block:blocks) {
                if(!next) {
                    next=expected_first;
                    if(chunk==1)check(live::detail::helper_physical_notch_support(raw_rate,logical_rate)*raw_rate>=end-raw_first,
                        "helper envelope omitted the actual first published output's complete capture support");
                }
                check(block.first_sample==next&&block.view->budget_serial==19&&block.raw_captured_end==end,
                      "physical notch changed its sample-center grid, input identity or raw frontier");
                next+=block.samples.size();
                const auto raw_last=(next-1)/logical_rate*raw_rate+((next-1)%logical_rate)*raw_rate/logical_rate;
                constexpr auto converter_radius=192; // actual fixed 8k -> 2k FIR
                check(raw_last+filter.delay()+converter_radius<end,
                      "physical notch/converter published an unobserved right support window");
                result.insert(result.end(),block.samples.begin(),block.samples.end());
            }
            check(frontend.bytes()<=grant,"physical notch exceeded its fixed prepayment during capture");
        }
        const auto end=raw_first+input.size();
        check(frontend.take(UINT64_MAX,ref,end).empty(),"physical notch retained hidden future/EOF output");
        frontend.push({},end);
        check(frontend.take(UINT64_MAX,ref,end).empty(),"empty/EOF input flushed unobserved filter samples");
        check(result.size()>2*logical_rate,"physical notch component did not retain a complete measured interval");
        std::complex<double> wanted{},aliased_auxiliary{},aliased_pilot{};
        for(std::size_t i=0;i<result.size();++i) {
            const auto t=static_cast<double>(expected_first+i)/logical_rate;
            wanted+=static_cast<double>(result[i])*std::polar(1.,-2*std::numbers::pi*500*t);
            aliased_auxiliary+=static_cast<double>(result[i])*std::polar(1.,-2*std::numbers::pi*750*t);
            aliased_pilot+=static_cast<double>(result[i])*std::polar(1.,-2*std::numbers::pi*350*t);
        }
        check(std::abs(2*std::abs(wanted)/result.size()-.02)<.0001&&
              2*std::abs(aliased_auxiliary)/result.size()<.0003&&2*std::abs(aliased_pilot)/result.size()<.0003,
              "physical notch/downsampling changed payload gain or retained a bright aliased helper tone");
        return result;
    };
    check(apply(1)==apply(173),"physical-notch output or dates depend on caller chunk boundaries");
    const auto forecast=live::detail::helper_physical_notch_support(raw_rate,logical_rate);
    check(forecast>2.*filter.delay()/raw_rate+2*support.source_lookahead_seconds()&&forecast<2,
          "helper envelope omitted actual physical notch/converter support");
    check(Frontend::reservation_bound(raw_rate,logical_rate,750)==old_grant,
          "valid logical notch changed its original frontend reservation");
    // Literal two-tone logical-notch pipeline: keep its sample values and
    // dates exact, including the analytic translator's delay convention.
    {
        auto valid=c;valid.sample_rate=6000;valid.carrier_hz=1500;
        auto valid_ref=ref;valid_ref.frequency_hz=2450;
        const auto valid_grant=Frontend::reservation_bound(raw_rate,valid.sample_rate);
        Frontend frontend(valid_grant);frontend.admit_realtime(valid,2450,valid_ref,raw_rate,3990,40);
        helper::AnalyticFrequencyTranslator translator({raw_rate,0,{1368,2707},40,false,true},
            helper::AnalyticFrequencyTranslator::workspace_required());
        audio::Resampler converter(raw_rate,valid.sample_rate);
        helper::PayloadFilter logical_filter(valid.sample_rate,2450-200,425);
        auto logical_placement=placement;logical_placement.frequency_hz=2450;logical_placement.auxiliary_frequency_hz=2050;
        helper::Generator generator(helper_options,logical_placement,1800000000,true);
        std::array<float,173> input{},translated{};std::array<float,2048> converted{},filtered{};
        std::vector<float> actual,previous;std::uint64_t actual_first=0;
        for(std::size_t at=0;at<2*raw_rate;) {
            const auto count=std::min(input.size(),2*raw_rate-at),end=40+at+count;
            for(std::size_t i=0;i<count;++i)input[i]=static_cast<float>(
                .02*std::cos(2*std::numbers::pi*1500*(40+at+i)/raw_rate)+
                generator.mix(0,static_cast<long double>(40+at+i)/raw_rate,raw_rate));
            frontend.push(std::span(input).first(count),40+at);
            translator.push(std::span(input).first(count),std::span(translated).first(count),40+at);
            std::size_t consumed=0;
            for(;;) {
                const auto progress=converter.process(std::span(translated).first(count).subspan(consumed),converted);
                consumed+=progress.consumed;
                const auto produced=logical_filter.push(std::span(converted).first(progress.produced),filtered);
                previous.insert(previous.end(),filtered.begin(),filtered.begin()+produced);
                if(!progress.consumed&&!progress.produced)break;
                if(consumed==count&&!progress.produced)break;
            }
            for(const auto& block:frontend.take(end/raw_rate*valid.sample_rate+
                    (end%raw_rate)*valid.sample_rate/raw_rate,valid_ref,end)) {
                if(actual.empty())actual_first=block.first_sample;
                actual.insert(actual.end(),block.samples.begin(),block.samples.end());
            }
            at+=count;
        }
        const auto old_first=30+logical_filter.delay();
        const auto eligible=30+static_cast<std::uint64_t>(std::ceil(
            2.L*helper::AnalyticFrequencyTranslator::group_delay*valid.sample_rate/raw_rate))+
            logical_filter.delay()+1;
        previous.erase(previous.begin(),previous.begin()+static_cast<std::ptrdiff_t>(eligible-old_first));
        check(actual_first==eligible&&actual==previous,
              "valid logical-rate two-tone notch changed its PCM or sample dates");
    }
    bool too_large=false;try {Frontend::reservation_bound(200000,logical_rate,helper_hz);}
    catch(const Error&){too_large=true;}
    check(too_large,"physical helper notch silently exceeded the 4097-tap optional bound");
    std::cout<<"frontend_physical_notch raw_rate="<<raw_rate<<" logical_rate="<<logical_rate
             <<" full_metadata_response="<<suppression<<" payload_error="<<passband
             <<" first="<<expected_first<<" old_grant="<<old_grant<<" grant="<<grant
             <<" envelope_support="<<forecast<<'\n';
}

// Pure optional-front-end checks. No payload endpoint, message timestamp,
// decoder, sound device or expected receive length participates in these.
void filter_retune_preserves_state() {
    constexpr std::uint32_t rate=8000;constexpr double helper_hz=2500;
    helper::PayloadFilter filter(rate,helper_hz),control(rate,helper_hz);
    const auto bytes=filter.allocated_bytes(),delay=filter.delay();
    std::vector<double> original(filter.coefficients().begin(),filter.coefficients().end());
    const auto same_coefficients=[&] {
        return original.size()==filter.coefficients().size() &&
            std::equal(original.begin(),original.end(),filter.coefficients().begin(),
                [](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);});
    };
    std::array<float,173> input{},actual{},expected{};
    std::uint64_t at=0;
    const auto consume=[&] {
        for(std::size_t i=0;i<input.size();++i) {
            const auto n=at+i;
            input[i]=static_cast<float>(.2*std::sin(.037*n)+.3*std::cos(.793*n)+.1*std::sin(1.79*n));
        }
        const auto a=filter.push(input,actual),b=control.push(input,expected);at+=input.size();
        check(a==b && std::equal(actual.begin(),actual.begin()+a,expected.begin()),
            "retuning reset filter history, sample counts or the previous kernel");
    };
    consume();consume();
    const auto count=filter.output_samples();
    for(const auto invalid:{0.,225.,rate/2.-225.,std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
        check(!filter.retune(invalid) && same_coefficients() && filter.center_hz()==helper_hz,
              "invalid filter retune changed the kernel or its center");
    }
    const auto previous_round=std::fegetround();
    if(std::fesetround(FE_DOWNWARD)==0) {
        const auto rejected=!filter.retune(helper_hz+1);
        const auto restored=std::fesetround(previous_round)==0;
        check(rejected && restored && same_coefficients(),"unsupported FP retune mutated a coefficient");
    }
    check(filter.retune(helper_hz) && same_coefficients(),"same-center retune changed coefficient bits");
    double transition=0;
    check(filter.retune(helper_hz+1,&transition) && transition>0 && std::isfinite(transition),
          "valid allocation-free filter retune failed to report its coefficient transition bound");
    check(filter.retune(helper_hz) && same_coefficients(),
          "transactional candidate rollback did not restore exact coefficient bits");
    check(filter.output_samples()==count && filter.delay()==delay && filter.allocated_bytes()==bytes,
          "retune changed filter output count, delay or fixed memory allocation");
    for(unsigned i=0;i<9;++i) {check(filter.retune(helper_hz),"same-center filter renewal failed");consume();}
    std::cout<<"filter_retune same_center_bits=exact rollback_coefficients=exact output_samples="
             <<filter.output_samples()<<" allocated_bytes="<<bytes<<" one_hz_l1_transition="<<transition<<'\n';
}
void raw_notch_drift(bool physical_notch) {
    using Frontend=live::detail::HelperInputFrontend;
    constexpr std::uint32_t raw_rate=8000;
    const auto logical_rate=physical_notch?4000U:8000U;
    const double helper_hz=physical_notch?2500:3000,data_hz=physical_notch?500:1000;
    modem::Config c;c.sample_rate=logical_rate;c.carrier_hz=data_hz;c.bandwidth_hz=2*data_hz;
    c.spreading_factor=16;c.pulse_shaping=false;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,.5},{.5,.1},30000000,3,
        modem::OscillatorReference::independent_audio,modem::OscillatorSideband::upper};
    constexpr double utc_rate_limit=.0003;
    const auto domain=live::detail::raw_helper_notch_domain(c,helper_hz,3990,utc_rate_limit);
    check(domain.valid && domain.one_plus_clock.lower<.9994 && domain.one_plus_clock.upper>1.0006,
          "raw notch did not retain the full original LF plus unresolved UTC clock domain");
    const auto bands=live::detail::raw_helper_notch_bands(domain,{helper_hz-.2,helper_hz+.2});
    check(bands.valid && bands.data.lower_hz==0,"folded raw notch lost its conjugate DATA image union");
    for(const auto b:{domain.one_plus_clock.lower,1.,domain.one_plus_clock.upper})
        for(const auto h:{helper_hz-.2,helper_hz+.2})for(const auto side:{-1.,1.}) {
            const auto truth=std::abs(h+(data_hz-helper_hz+side*helper::occupied_bandwidth(c)/2)*b);
            check(truth>=bands.data.lower_hz && truth<=bands.data.upper_hz,
                  "raw notch certificate excluded an original DATA clock/frequency boundary");
        }
    helper::Reference ref;ref.sample_rate=raw_rate;ref.frequency_valid=true;
    ref.frequency_hz=helper_hz;ref.frequency_uncertainty_hz=.01;
    const auto grant=Frontend::reservation_bound(raw_rate,logical_rate,helper_hz);
    Frontend frontend(grant,27);frontend.admit_realtime(c,helper_hz,ref,raw_rate,3990,0,utc_rate_limit);
    check(frontend.active() && frontend.reference_passthrough(),"folded raw notch fixture did not select q=0");
    const auto retained=frontend.bytes();std::uint64_t first=0,next=0,total=0;
    double maximum_transition=0;
    std::vector<float> input(2*raw_rate),observed;observed.reserve(64*logical_rate);
    // 64 seconds at the tracker's existing .5-Hz/s operational gate traverses
    // 32 Hz, exceeding the former forever-frozen 25-Hz admission guard.
    for(std::uint64_t at=0;at<64ULL*raw_rate;at+=input.size()) {
        ref.sample=at;ref.frequency_hz=helper_hz+.5*at/raw_rate;
        check(frontend.track_frequency(ref,helper_hz),"raw notch lost valid cumulative drift beyond its first 25 Hz");
        maximum_transition=std::max(maximum_transition,frontend.last_notch_transition_l1());
        for(std::size_t i=0;i<input.size();++i) {
            const auto t=static_cast<double>(at+i)/raw_rate;
            input[i]=static_cast<float>(.02*std::cos(2*std::numbers::pi*(data_hz*t+.25*t*t))+
                .5*std::cos(2*std::numbers::pi*(helper_hz*t+.25*t*t)));
        }
        frontend.push(input,at);
        for(const auto& block:frontend.take((at+input.size())/raw_rate*logical_rate,ref,at+input.size())) {
            if(!total)first=next=block.first_sample;
            check(block.first_sample==next && block.view->correction_hz==0 && !block.view->conjugate &&
                block.view->reference_passthrough && block.view->budget_serial==27,
                "raw notch retune changed NCO phase convention, row-view identity or sample coordinates");
            next+=block.samples.size();total+=block.samples.size();
            observed.insert(observed.end(),block.samples.begin(),block.samples.end());
        }
        check(frontend.bytes()==retained && retained<=grant,"raw notch drift grew its optional allocation");
    }
    check(total>62ULL*logical_rate && first+total==next,
          "raw notch drift discarded history or inserted/removed capture samples");
    std::complex<double> data{},helper_tone{};const auto begin=observed.size()-8*logical_rate;
    for(std::size_t i=begin;i<observed.size();++i) {
        const auto t=static_cast<double>(first+i)/logical_rate-
            static_cast<double>(helper::AnalyticFrequencyTranslator::group_delay)/raw_rate;
        const auto common=.25*t*t;
        data+=static_cast<double>(observed[i])*std::polar(1.,-2*std::numbers::pi*(data_hz*t+common));
        helper_tone+=static_cast<double>(observed[i])*std::polar(1.,-2*std::numbers::pi*(helper_hz*t+common));
    }
    const auto count=static_cast<double>(observed.size()-begin);
    const auto data_gain=2*std::abs(data)/count,helper_leak=2*std::abs(helper_tone)/count;
    check(std::abs(data_gain-.02)<.0001 && helper_leak<.0005,
          "drift-following raw notch damaged DATA or retained the strong helper after 25 Hz");
    ref.sample=64ULL*raw_rate;ref.frequency_hz=3900;
    check(!frontend.track_frequency(ref,helper_hz) && frontend.tracking_decline_reason(),
          "raw notch moved to an unsupported capture/immutable-notch boundary");
    const auto before=next;
    frontend.push(input,64ULL*raw_rate);
    for(const auto& block:frontend.take(66ULL*logical_rate,ref,66ULL*raw_rate)) {
        check(block.first_sample==next,"declined raw retune reset established payload coordinates");
        next+=block.samples.size();
    }
    check(next>before && frontend.bytes()==retained,"declined raw retune stopped established PCM draining");
    std::cout<<"raw_notch_drift physical_notch="<<physical_notch<<" drift_hz=32 output_samples="<<total
             <<" first="<<first<<" data_gain="<<data_gain<<" helper_leak="<<helper_leak
             <<" maximum_l1_transition="<<maximum_transition<<" optional_bytes="<<retained
             <<" boundary_decline="<<frontend.tracking_decline_reason()<<'\n';
}
void raw_notch_converter_boundary(bool physical_notch) {
    using Frontend=live::detail::HelperInputFrontend;
    constexpr std::uint32_t raw_rate=8000,logical_rate=4000;
    const double helper_hz=physical_notch?2500:700,data_hz=physical_notch?500:1500;
    const double data_width=physical_notch?1000:200,offset=physical_notch?700:100;
    modem::Config c;c.sample_rate=logical_rate;c.carrier_hz=data_hz;c.bandwidth_hz=data_width;
    c.spreading_factor=16;c.pulse_shaping=false;
    // LAB manually supplied references isolate input-basis/passband policy;
    // this is not causal acquisition outside the zero RF prior. An overlapping
    // reference selects raw q=0; a disjoint reference may select correction.
    // The other arrangement is folded and always uses a physical FIR.
    c.oscillator_search=modem::OscillatorSearchConfig{{100,.5},{0,.1},30000000,3,
        modem::OscillatorReference::independent_audio,modem::OscillatorSideband::upper};
    helper::Reference ref;ref.sample_rate=raw_rate;ref.frequency_valid=true;
    ref.frequency_hz=helper_hz;ref.frequency_uncertainty_hz=.01;
    const auto grant=Frontend::reservation_bound(raw_rate,logical_rate,helper_hz);
    audio::Resampler converter(raw_rate,logical_rate);
    const auto domain=live::detail::raw_helper_notch_domain(c,helper_hz,3990,0);
    const auto moved=live::detail::raw_helper_notch_bands(domain,
        {helper_hz+offset-.01,helper_hz+offset+.01});
    check(moved.valid && moved.data.upper_hz>converter.passband_hz() &&
          moved.data.upper_hz<logical_rate/2.,
          "converter-boundary fixture lost its ADC-valid DATA inside the conversion transition below Nyquist");
    auto outside=ref;outside.frequency_hz+=offset;
    Frontend frontend(grant);frontend.admit_realtime(c,helper_hz,ref,raw_rate,3990,0);
    check(frontend.active() && frontend.reference_passthrough(),"converter-boundary fixture failed initial q=0 admission");
    const auto bytes=frontend.bytes();std::array<float,raw_rate> zeros{};
    frontend.push(zeros,0);auto before=frontend.take(logical_rate,ref,raw_rate);
    check(!before.empty(),"converter-boundary fixture did not establish ordinary dated PCM");
    const auto& raw_view=*before.front().view;
    check(raw_view.reference_passthrough && raw_view.correction_hz==0 &&
          raw_view.payload_output_band.upper_hz<converter.passband_hz(),
          "overlapping LAB reference did not establish an actual safe q=0 DATA view");
    auto next=before.back().first_sample+before.back().samples.size();outside.sample=raw_rate;
    check(!frontend.track_frequency(outside,helper_hz) && frontend.tracking_decline_reason() &&
          std::string_view(frontend.tracking_decline_reason()).find("immutable converter passband")!=std::string_view::npos,
          "raw notch renewal silently moved DATA into converter rolloff");
    frontend.push(zeros,raw_rate);
    for(const auto& block:frontend.take(2ULL*logical_rate,outside,2ULL*raw_rate)) {
        check(block.first_sample==next,"converter decline changed established PCM coordinates");
        next+=block.samples.size();
    }
    check(next>before.back().first_sample+before.back().samples.size() && frontend.bytes()==bytes,
          "converter decline stopped PCM draining or changed optional allocations");

    Frontend unborn(grant);bool birth_declined=false,birth_corrected=false;
    double birth_q=0,translated_low=0,translated_high=0;
    auto birth_reference=outside;birth_reference.sample=0;
    if(physical_notch) {
        try {unborn.admit_realtime(c,helper_hz,birth_reference,raw_rate,3990,0);}
        catch(const Error& error){birth_declined=std::string_view(error.what()).find("immutable converter passband")!=std::string_view::npos;}
        check(birth_declined && !unborn.active(),
              "folded raw notch admitted DATA already outside the converter flat passband");
    } else {
        unborn.admit_realtime(c,helper_hz,birth_reference,raw_rate,3990,0);
        check(unborn.active() && !unborn.reference_passthrough(),
              "disjoint LAB reference did not select a corrected input basis");
        unborn.push(zeros,0);const auto born=unborn.take(logical_rate,birth_reference,raw_rate);
        check(!born.empty(),"corrected LAB birth did not publish its actual dated input view");
        const auto& view=*born.front().view;birth_q=view.correction_hz;
        translated_low=moved.data.lower_hz+birth_q;translated_high=moved.data.upper_hz+birth_q;
        check(!view.reference_passthrough && birth_q==-offset &&
              translated_low>0 && translated_high<converter.passband_hz() &&
              view.payload_output_band.lower_hz<=translated_low &&
              translated_high<=view.payload_output_band.upper_hz &&
              view.payload_output_band.upper_hz<converter.passband_hz(),
              "corrected LAB birth did not preserve the complete DATA band inside the flat passband");
        check(unborn.bytes()<=grant,"corrected LAB birth exceeded its immutable workspace grant");
        birth_corrected=true;
    }
    std::cout<<"raw_notch_converter_boundary physical_notch="<<physical_notch
             <<" reference_source=LAB_MANUAL_NOT_CAUSAL_ACQUISITION"
             <<" raw_view="<<raw_view.id<<" raw_q="<<raw_view.correction_hz
             <<" raw_data_low="<<raw_view.payload_output_band.lower_hz
             <<" raw_data_high="<<raw_view.payload_output_band.upper_hz
             <<" moved_data_low="<<moved.data.lower_hz<<" moved_data_high="<<moved.data.upper_hz
             <<" converter_passband="<<converter.passband_hz()<<" birth_declined="<<birth_declined
             <<" birth_corrected="<<birth_corrected<<" birth_q="<<birth_q
             <<" translated_data_low="<<translated_low<<" translated_data_high="<<translated_high
             <<" renewal_declined=1 continued_pcm=1\n";
}
void filter_retune_transition_peak() {
    // An independently specified complex linear chirp provides the exact
    // center phase for EVERY observed output, including the first sample after
    // a coefficient swap. Unlike an eight-second average this catches peak
    // transition gain/phase errors. It is still a controlled waveform test,
    // not an all-time certificate for arbitrary helper metadata or DATA.
    constexpr std::uint32_t rate=8000;constexpr double data_hz=500,helper_hz=2500;
    constexpr double slope=.5,amplitude=.02;
    helper::PayloadFilter real_filter(rate,helper_hz),imaginary_filter(rate,helper_hz);
    const auto delay=real_filter.delay(),bytes=real_filter.allocated_bytes();
    std::array<float,173> real_input{},imaginary_input{},real_output{},imaginary_output{};
    std::uint64_t output_count=0,next_retune=0;double peak_error=0,peak_phase=0,relative_bound=0,maximum_transition=0;
    for(std::uint64_t at=0;at<16ULL*rate;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(real_input.size(),16ULL*rate-at));
        if(at>=next_retune) {
            const auto center=helper_hz+slope*at/rate;double transition=0;
            check(real_filter.retune(center,&transition) && imaginary_filter.retune(center),
                  "complex-chirp control could not renew its existing FIR kernel");
            maximum_transition=std::max(maximum_transition,transition);next_retune=at+2ULL*rate;
            // For this chirp, departure from the local linear-phase tone at
            // lag k is <= pi*slope*(k/rate)^2. Triangle-summing actual FIR
            // coefficients bounds that remainder, without assuming that the
            // time-varying system is LTI. Input/output float rounding is added
            // separately below. This assertion does not cover arbitrary phase
            // steps, metadata, aliased capture or unqualified reference loss.
            double quadratic=0;
            for(std::size_t k=1;k<=delay;++k) {
                const auto seconds=static_cast<double>(k)/rate;
                // push() mirrors the stored LEFT taps, not the separately
                // rounded right entries. Use exactly those coefficients.
                const auto term=2*std::abs(real_filter.coefficients()[delay-k])*
                    0x1.921fb54442d19p+1*slope*seconds*seconds;
                quadratic=std::nextafter(quadratic+term,std::numeric_limits<double>::infinity());
            }
            const auto linear=real_filter.maximum_amplitude_error(data_hz-1,data_hz+10);
            check(linear<=.001,"complex-chirp DATA passband failed its static-kernel control");
            relative_bound=std::max(relative_bound,linear+quadratic);
        }
        for(std::size_t i=0;i<count;++i) {
            const auto t=static_cast<double>(at+i)/rate;
            const auto angle=2*std::numbers::pi*(data_hz*t+.5*slope*t*t);
            real_input[i]=static_cast<float>(amplitude*std::cos(angle));
            imaginary_input[i]=static_cast<float>(amplitude*std::sin(angle));
        }
        const auto produced=real_filter.push(std::span(real_input).first(count),real_output);
        check(produced==imaginary_filter.push(std::span(imaginary_input).first(count),imaginary_output),
              "complex-chirp FIR quadratures changed sample counts at a retune");
        for(std::size_t i=0;i<produced;++i) {
            const auto t=static_cast<double>(output_count+i+delay)/rate;
            const auto ideal=std::polar(amplitude,2*std::numbers::pi*(data_hz*t+.5*slope*t*t));
            const std::complex<double> actual{real_output[i],imaginary_output[i]};
            peak_error=std::max(peak_error,std::abs(actual-ideal));
            peak_phase=std::max(peak_phase,std::abs(std::arg(actual*std::conj(ideal))));
        }
        output_count+=produced;at+=count;
        check(real_filter.output_samples()==output_count && imaginary_filter.output_samples()==output_count &&
              real_filter.allocated_bytes()==bytes && imaginary_filter.allocated_bytes()==bytes,
              "complex-chirp retune changed FIR counters or allocations");
    }
    constexpr double pcm_rounding=1e-7;
    const auto error_bound=amplitude*relative_bound+pcm_rounding;
    check(output_count==16ULL*rate-2*delay && peak_error<=error_bound &&
          peak_phase<=std::asin(std::min(.99,error_bound/amplitude))+1e-5,
          "changing FIR kernel exceeded its pointwise chirp amplitude/phase/coordinate control");
    std::cout<<"filter_retune_transition_peak output_samples="<<output_count<<" peak_complex_error="<<peak_error
             <<" pointwise_error_bound="<<error_bound<<" peak_phase_radians="<<peak_phase
             <<" maximum_l1_transition="<<maximum_transition<<'\n';
}
void notch_retune_components() {
    filter_retune_preserves_state();filter_retune_transition_peak();
    raw_notch_converter_boundary(false);raw_notch_converter_boundary(true);
    raw_notch_drift(false);raw_notch_drift(true);
}

struct PhysicalResult {Bytes bits;std::vector<std::size_t> progress;std::uint64_t first=0,physical_end=0,completed_at=0;std::size_t completed=0;};
PhysicalResult physical(bool helper_on) {
    modem::Config c;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=1000;
    c.spreading_factor=64;c.stream_epoch=1800000000;
    helper::Options helper;helper.power_w=helper_on?1500:0;helper.callsign="N0CALL";
    modem::ChannelConfig model;model.snr_db=30;model.clock_error_ppm=0;model.phase_noise_degrees_per_sqrt_second=0;model.seed=5181;
    modem::SampledSimulationChannel channel(c,model,helper);
    modem::StreamingTransmitter tx(modem::RawBits{payload_bits()},c);
    modem::PatternSearch search;search.worker_threads=1;search.chunk_bits=1;
    search.start_offset_seconds=0;search.start_uncertainty_seconds=6;
    modem::StreamingReceiver receiver(c,32*1024*1024,search);
    helper::PayloadFilter filter;
    if(helper_on) {
        const auto placement=helper::plan(helper,c.carrier_hz,helper::occupied_bandwidth(c),channel.physical_sample_rate(),0,
            modem::outer_dsss_transmit_gain(c));
        check(placement.frequency_hz+225<c.sample_rate/2.,"physical payload-tail fixture did not exercise adjacent-helper FIR");
        filter=helper::PayloadFilter(c.sample_rate,placement.frequency_hz-200,425);
    }
    audio::Resampler playback(c.sample_rate,48000),capture(48000,c.sample_rate);
    std::map<std::pair<std::uint64_t,std::uint64_t>,PhysicalResult> streams;
    std::uint64_t supplied=0;
    const auto score=[&](std::span<const float> block) {
        std::vector<float> filtered(block.size());const auto count=filter.push(block,filtered);
        receiver.push(std::span(filtered).first(count));receiver.process_pending();supplied+=count;
        for(const auto& burst:receiver.take_pattern_bursts()) {
            const auto identity=std::pair{burst.stream_first_sample,burst.stream_first_symbol};auto& result=streams[identity];
            result.first=identity.first+filter.delay();
            check(!burst.missing_slots,"physical payload-tail fixture lost timed slots");
            check(burst.first_stream_symbol==result.bits.size(),"physical payload chunks skipped or repeated accepted bit coordinates");
            check(burst.bits.size()<=1,"physical payload-tail fixture delayed per-bit progress");
            for(const auto bit:burst.bits) {result.bits.push_back(bit);result.progress.push_back(result.bits.size());}
            result.physical_end=burst.end_sample+filter.delay();
            if(burst.complete){++result.completed;result.completed_at=supplied+filter.delay();}
        }
    };
    const auto captured=[&](std::span<const float> hardware,bool end=false) {
        std::array<float,1024> logical{};std::size_t consumed=0;
        do {const auto p=capture.process(hardware.subspan(consumed),logical,end);consumed+=p.consumed;
            if(p.produced)score(std::span(logical).first(p.produced));
            if(!p.consumed&&!p.produced)break;
        }while(consumed<hardware.size()||(end&&!capture.finished()));
        check(consumed==hardware.size(),"capture resampler failed to consume finite payload-tail input");
    };
    const auto route=[&](std::span<const float> logical,bool end=false) {
        std::array<float,4096> hardware{};std::size_t consumed=0;
        do {const auto p=playback.process(logical.subspan(consumed),hardware,end);consumed+=p.consumed;
            if(p.produced)captured(std::span(hardware).first(p.produced));
            if(!p.consumed&&!p.produced)break;
        }while(consumed<logical.size()||(end&&!playback.finished()));
        check(consumed==logical.size(),"playback resampler failed to consume finite payload-tail input");
    };
    std::array<float,173> logical{};
    while(const auto count=channel.read(tx,logical))route(std::span(logical).first(count));
    std::uint64_t trailing=10ULL*c.sample_rate;
    while(trailing){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,logical.size()));channel.read_noise(std::span(logical).first(count));route(std::span(logical).first(count));trailing-=count;}
    route({},true);captured({},true);
    check(streams.size()==1,"physical helper payload-tail fixture found multiple independent payload streams");
    const auto result=streams.begin()->second;
    std::cout<<"physical helper="<<helper_on<<" bits="<<result.bits.size()<<" complete="<<result.completed
             <<" first="<<result.first<<" physical_end="<<result.physical_end<<" completed_at="<<result.completed_at
             <<" prefix="<<binary(result.bits)<<'\n';
    check(result.bits==payload_bits(),"sampled helper/resampler/filter route truncated or changed the exact 64-bit payload");
    check(result.completed==1,"sampled helper/resampler/filter route omitted or duplicated physical END");
    check(result.progress.size()==payload_bits().size(),"sampled route omitted accepted bit progress");
    for(std::size_t i=0;i<result.progress.size();++i)check(result.progress[i]==i+1,"sampled route batched/reordered retained bit coordinates");
    check(result.completed_at>=result.physical_end+6ULL*c.sample_rate,
          "sampled helper route completed without six seconds of actual absent-symbol scoring");
    return result;
}
void physical_matched() {
    const auto off=physical(false),on=physical(true);
    check(off.bits==on.bits&&off.progress==on.progress,"helper changed complete physical payload bits or progress");
}

bool tail_trace_filter(const char* name) noexcept {
    const std::string_view event{name};return event=="trace_begin"||event=="receiver_signal_commit"||
        event=="receiver_selection"||event=="receiver_bank_admitted"||event.starts_with("receiver_helper_");
}
bool framed_tail_trace_filter(const char* name) noexcept {
    return tail_trace_filter(name)||std::string_view(name)=="receiver_acquisition_work";
}
std::uint64_t trace_number(const std::string& line,const std::string& key) {
    const auto needle='"'+key+"\":";const auto at=line.find(needle);
    check(at!=std::string::npos,"payload-tail trace omitted required physical coordinates");
    return std::stoull(line.substr(at+needle.size()));
}
double trace_real(const std::string& line,const std::string& key) {
    const auto needle='"'+key+"\":";const auto at=line.find(needle);
    check(at!=std::string::npos,"payload-tail trace omitted required measured frequency");
    return std::stod(line.substr(at+needle.size()));
}
struct TailDates {
    std::atomic<std::uint64_t> transmit{0};
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& observation) {
        if(observation.phase==live::detail::SimulationPhase::transmit_epoch)
            static_cast<TailDates*>(opaque)->transmit=static_cast<std::uint64_t>(observation.epoch);
    },1800000000.125};
    TailDates() {live::detail::SimulationHook* empty=nullptr;check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"payload-tail simulation hook already installed");}
    ~TailDates(){live::detail::simulation_hook.store(nullptr);}
};
struct LiveTailResult {Bytes wire;std::uint64_t transmit_epoch=0;};
LiveTailResult live_tail(bool helper_on,int kind=0) {
    live::Settings value;value.simulation=true;value.full_duplex=true;
    auto& c=value.transfer.modem;c.sample_rate=2000;c.carrier_hz=500;c.bandwidth_hz=200;
    c.spreading_factor=16;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    std::array<std::uint8_t,32> secret{};for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(i*11+7);
    value.transfer.key=Crypto(secret);value.transfer.timestamp=0;value.transfer.search_seconds=0;
    value.transfer.recovery_options.enabled=false;value.simulation_snr_db=30;
    value.simulation_clock_error_ppm=0;value.simulation_frequency_offset_hz=0;
    value.simulation_phase_noise_degrees_per_sqrt_second=0;value.simulation_seed=5181;
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=64*1024*1024;value.content_limit=1024*1024;
    value.helper.power_w=helper_on?1500:0;value.helper.callsign.clear();
    Message message;
    if(kind==1)message.data=Bytes{'t','a','i','l',' ','c','o','m','p','l','e','t','e',' ','t','e','s','t'};
    if(kind==2) {message.kind=MessageKind::file;message.filename="tail.bin";for(unsigned i=0;i<17;++i)message.data.push_back(static_cast<std::uint8_t>(i*37+11));}
    if(kind) {value.transfer.fec=FecMode::rs60;value.transfer.compression=true;value.transfer.recovery_options.enabled=true;
        value.transfer.recovery_options.workers=1;value.transfer.recovery_options.budget=std::chrono::seconds(5);}
    auto expected=kind?transfer::message_wire_bits(message,value.transfer):payload_bits();
    if(kind)transfer::xor_binary_bits(expected,value.transfer);
    check(expected.size()<=4096,"tail fixture must keep its entire expected wire in the ordinary diagnostic prefix");
    auto expected_text=binary(expected);Bytes dated_wire;
    const auto base=std::getenv("DATAPUMP_HELPER_TAIL_EVIDENCE");
    const auto path=(base?std::filesystem::path(base):std::filesystem::temp_directory_path())/
        (std::string("helper-tail-rolling-")+std::to_string(kind)+"-"+(helper_on?"on":"off")+"-"+std::to_string(audio::trace::identity())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::filesystem::create_directories(path.parent_path());
    std::atomic<std::int64_t> replay_ms{0};bool finished=false,exact=false;std::uint64_t admitted=0,samples=0,transmit_epoch=0;
    {
        audio::trace::Scope trace(path,false,kind?&framed_tail_trace_filter:&tail_trace_filter);TailDates dates;
        // Control only the initial station clock. The receiver still has no
        // fixed message timestamp and must admit later virtual-media epochs.
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        session.start(value);if(kind)session.transmit(message);else session.transmit_bits(expected);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(kind==2?90:60);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();samples=snapshot.samples_received;admitted=snapshot.receiver_admissions;
            if(!snapshot.error.empty())std::cerr<<"live helper="<<helper_on<<" error="<<snapshot.error<<'\n';
            check(snapshot.error.empty()&&!snapshot.receiver_health.failed()&&!snapshot.receiver_health.dropped_samples,
                  "rolling Auto helper payload-tail fixture caused an ordinary receiver error");
            if(snapshot.transmission_finished&&snapshot.simulation_replay) {
                replay_ms=4000;finished=true;
                transmit_epoch=dates.transmit.load();check(transmit_epoch>0,"rolling payload-tail fixture missed actual transmit date");
                auto dated=value.transfer;dated.timestamp=transmit_epoch;
                if(kind) {dated_wire=transfer::message_wire_bits(message,dated);expected=dated_wire;
                    transfer::xor_binary_bits(expected,dated);expected_text=binary(expected);}
                else {dated_wire=expected;transfer::xor_binary_bits(dated_wire,dated);}
                const auto visible=session.snapshot();
                for(const auto& signal:visible.signals) {
                    if(signal.helper_carrier||!signal.received_bits)continue;
                    const auto raw=signal.binary?signal.text:signal.raw_bits;
                    std::cout<<"live_visible helper="<<helper_on<<" id="<<signal.id<<" revision="<<signal.revision
                             <<" count="<<signal.received_bits<<" complete="<<signal.complete<<" raw="<<raw<<'\n';
                    if(signal.complete&&!kind)check(signal.received_bits==expected.size()&&raw==expected_text,
                        "rolling Auto helper completed with missing or changed final payload bits");
                    if(!kind)exact=exact||(signal.complete&&signal.received_bits==expected.size()&&raw==expected_text);
                }
                if(kind)for(const auto& received:visible.received) {
                    std::cout<<"live_source kind="<<kind<<" helper="<<helper_on<<" observed="<<received.observed_bits
                             <<" diagnostic_bits="<<received.raw_bits.size()<<" source_bytes="<<received.content.message.data.size()
                             <<" complete="<<received.stream_complete<<" validated="<<received.content_validated
                             <<" authenticated="<<received.content.authenticated
                             <<" wire_equal="<<(received.raw_bits==expected)<<" data_equal="<<(received.content.message.data==message.data)
                             <<" kind_equal="<<(received.content.message.kind==message.kind)<<" filename_equal="<<(received.content.message.filename==message.filename)<<'\n';
                    exact=exact||(received.stream_complete&&received.content_validated&&received.content.authenticated&&
                        received.observed_bits==expected.size()&&received.raw_bits==expected&&
                        received.content.message.data==message.data&&received.content.message.kind==message.kind&&
                        received.content.message.filename==message.filename);
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        session.stop();
    }
    std::ifstream input(path);std::string line;std::uint64_t id=0,revision=0,last=0;bool complete=false,pending=false,observed_end=false,assisted_committed=false,phase_applied=false;
    // Work counters belong to one numeric owner. Commit records identify the
    // same constructor by its immutable input revision/epoch/family/origin.
    // This join proves actual payload work rather than unrelated row maxima.
    using Owner=std::array<std::uint64_t,3>; // revision, instance, input origin
    using Row=std::array<std::uint64_t,4>; // revision, epoch, family, input origin
    using Admission=std::array<std::uint64_t,3>; // revision, epoch, family
    std::map<Owner,std::array<std::uint64_t,4>> row_work;
    std::map<Row,std::uint64_t> qualified_work;
    std::map<Admission,std::uint64_t> admitted_at;
    std::set<Row> payload_rows,narrowed_rows;
    std::uint64_t first_admission=UINT64_MAX,first_epoch=UINT64_MAX;
    bool raw_qualified=false,realtime_seen=false,parallel_assistance=false,later_epoch_work=false;
    while(std::getline(input,line)) {
        phase_applied=phase_applied||line.find("\"event\":\"receiver_helper_phase_applied\"")!=std::string::npos;
        if(kind) {
            parallel_assistance|=line.find("\"event\":\"receiver_helper_assistance_admitted\"")!=std::string::npos ||
                line.find("\"event\":\"receiver_helper_conditioning_admitted\"")!=std::string::npos;
            if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos && trace_number(line,"qualified")) {
                const auto sample=trace_number(line,"sample"),end=trace_number(line,"captured_end");
                const auto frequency=trace_real(line,"frequency_hz"),uncertainty=trace_real(line,"uncertainty_hz");
                check(sample<end && std::isfinite(frequency) && frequency>0 && std::isfinite(uncertainty) && uncertainty>=0,
                      "framed helper reference lost its actual measured frequency or captured support");
                raw_qualified=true;
            }
            if(line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos) {
                const auto sample=trace_number(line,"sample"),epoch=trace_number(line,"epoch");
                first_admission=std::min(first_admission,sample);first_epoch=std::min(first_epoch,epoch);
                admitted_at[{trace_number(line,"revision"),epoch,trace_number(line,"family")}]=sample;
            }
            if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
                const Owner owner{trace_number(line,"revision"),trace_number(line,"instance_id"),trace_number(line,"sample_origin")};
                auto& previous=row_work[owner];
                const std::array current{trace_number(line,"template_jobs"),trace_number(line,"input_transforms"),
                    trace_number(line,"fft_jobs"),trace_number(line,"direct_jobs")};
                for(std::size_t i=0;i<current.size();++i)
                    check(current[i]>=previous[i],"framed tail numeric work reset within one receiver owner");
                const bool joined=trace_number(line,"numeric_owner") && !trace_number(line,"incomplete") &&
                    line.find("\"reason\":\"feed_complete\"")!=std::string::npos;
                const bool executed=current[0]>previous[0] && (current[2]>previous[2] || current[3]>previous[3]);
                const bool realtime=trace_number(line,"realtime_helper");realtime_seen|=realtime;
                const auto epoch=trace_number(line,"epoch"),family=trace_number(line,"family");
                if(joined && executed) {
                    const auto jobs=current[0]-previous[0];
                    if(realtime && trace_number(line,"realtime_locked")) {
                        auto& total=qualified_work[{owner[0],epoch,family,owner[2]}];
                        check(jobs<=UINT64_MAX-total,"framed helper joined work total overflowed");total+=jobs;
                        const auto selected=trace_number(line,"helper_window_selected_pairs"),original=trace_number(line,"helper_window_original_pairs");
                        // A backend may use corrected PCM without supporting
                        // a pair mask. Report narrowing only when this SAME
                        // active row really executed a smaller supported set.
                        if(trace_number(line,"helper_window_supported") && trace_number(line,"helper_window_active") &&
                           selected && selected<original)narrowed_rows.insert({owner[0],epoch,family,owner[2]});
                    }
                    const auto found=admitted_at.find({owner[0],epoch,family});
                    if(!realtime && found!=admitted_at.end() && found->second>first_admission && epoch>first_epoch &&
                       trace_number(line,"sample")>found->second)later_epoch_work=true;
                }
                previous=current;
            }
        }
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos)continue;
        const auto count=trace_number(line,"received_bits");if(!count)continue;
        if(kind)payload_rows.insert({trace_number(line,"input_revision"),trace_number(line,"epoch"),
            trace_number(line,"family"),trace_number(line,"sample_origin")});
        assisted_committed=assisted_committed||line.find("\"helper_assisted\":1")!=std::string::npos;
        const auto current_id=trace_number(line,"signal_id");const auto current_revision=trace_number(line,"revision");
        if(!id){id=current_id;revision=current_revision;}
        check(current_id==id,"rolling helper payload-tail fixture replaced the pending row identity");
        check(count>=last,"rolling helper payload-tail fixture withdrew an accepted tail");
        last=count;revision=current_revision;
        const std::string marker="\"raw_bits_prefix\":\"";const auto at=line.find(marker);
        check(at!=std::string::npos,"rolling helper commit omitted available raw-bit prefix");
        const auto begin=at+marker.size(),end=line.find('"',begin);
        if(line.find("\"complete\":1")==std::string::npos)
            check(line.substr(begin,end-begin)==expected_text.substr(0,std::min<std::uint64_t>(count,128)),
                  "rolling helper commit changed the exact accepted payload prefix");
        if(line.find("\"complete\":1")!=std::string::npos) {
            complete=count==expected.size();observed_end=trace_number(line,"feed_end_sample")>=
                trace_number(line,"physical_end")+6ULL*c.sample_rate;
        } else pending=true;
    }
    std::uint64_t payload_jobs=0;bool payload_narrowed_work=false;
    for(const auto& row:payload_rows)if(const auto found=qualified_work.find(row);found!=qualified_work.end()) {
        check(found->second<=UINT64_MAX-payload_jobs,"framed payload work total overflowed");payload_jobs+=found->second;
        payload_narrowed_work|=narrowed_rows.contains(row);
    }
    std::cout<<"rolling kind="<<kind<<" helper="<<helper_on<<" finished="<<finished<<" exact="<<exact<<" trace_bits="<<last
             <<" transmit_epoch="<<transmit_epoch<<" id="<<id<<" revision="<<revision<<" pending="<<pending<<" complete="<<complete
             <<" observed_end="<<observed_end<<" helper_assisted_committed="<<assisted_committed<<" phase_applied="<<phase_applied<<" admissions="<<admitted<<" samples="<<samples
             <<" raw_qualified="<<raw_qualified<<" payload_joined_jobs="<<payload_jobs<<" payload_narrowed_work="<<payload_narrowed_work<<" later_epoch_work="<<later_epoch_work<<" trace="<<path<<'\n';
    if(!kind)check(admitted>8,"controlled-clock tail fixture did not retain actual rolling receiver admissions");
    else if(helper_on)check(raw_qualified && payload_jobs && !parallel_assistance,
        "framed helper did not feed qualified actual work to its ordinary payload receiver");
    else check(!raw_qualified && !realtime_seen && !parallel_assistance && later_epoch_work,
        "framed helper-OFF control lost later ordinary epoch scoring or acquired helper authority");
    check(finished&&exact&&pending&&complete&&observed_end,
          "rolling Auto helper payload-tail fixture lost full payload, progress or physical completion");
    if(!base){std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");}
    return {dated_wire,transmit_epoch};
}
void live_matched(int kind=0) {
    const auto ordinary=live_tail(false,kind),helper=live_tail(true,kind);
    // Each run above compares its entire dated wire and recovered source. A
    // helper lead reserves a future private epoch rather than delaying a source
    // already keyed for the current epoch; its masked wire can therefore differ.
    check(ordinary.transmit_epoch && helper.transmit_epoch,"matched helper fixture omitted its actual transmit date");
    if(ordinary.transmit_epoch==helper.transmit_epoch)
        check(ordinary.wire==helper.wire,"matched same-epoch helper fixture changed the complete dated wire");
    std::cout<<"matched_source kind="<<kind<<" off_epoch="<<ordinary.transmit_epoch
             <<" on_epoch="<<helper.transmit_epoch<<" dated_wire_equal="<<(ordinary.wire==helper.wire)<<'\n';
}

}
int main(int argc,char** argv) {try {
    if(argc==2&&std::string(argv[1])=="--retune-only"){notch_retune_components();return 0;}
    if(argc==2&&std::string(argv[1])=="--history-only"){history_preserves_complete_tail();return 0;}
    if(argc==2&&std::string(argv[1])=="--frontend-only"){frontend_physical_notch();notch_retune_components();return 0;}
    if(argc==2&&std::string(argv[1])=="--physical-only"){physical_matched();return 0;}
    if(argc==2&&std::string(argv[1])=="--live-only"){live_matched();return 0;}
    if(argc==2&&std::string(argv[1])=="--framed-text-only"){live_matched(1);return 0;}
    if(argc==2&&std::string(argv[1])=="--attachment-only"){live_matched(2);return 0;}
    if(argc==1){history_preserves_complete_tail();frontend_physical_notch();notch_retune_components();physical_matched();live_matched();live_matched(1);live_matched(2);return 0;}
    throw std::runtime_error("unknown helper payload-tail test mode");
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
