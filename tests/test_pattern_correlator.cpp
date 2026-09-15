#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <numeric>
#include <random>

using namespace datapump;
namespace {
void check(bool condition,const char* message){if(!condition)throw Error(message);}
template<class Action>void rejects(Action action,const char* message){try{action();}catch(const Error&){return;}throw Error(message);}
modem::Config config() {
    modem::Config c;c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=128;
    c.scramble=true;c.dsss=true;c.stream_epoch=8312;
    c.spreading_seed[3]=51;c.dsss_seed[13]=171;return c;
}
std::vector<float> waveform(const Bytes& bits,const modem::Config& c,std::size_t delay,double rate=1) {
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(source.total_samples()));source.read_analytic(analytic);
    const auto count=delay+static_cast<std::size_t>(std::ceil(static_cast<double>(analytic.size())/rate))+2*modem::symbol_sample_count(c);
    std::vector<float> pcm(count);
    std::mt19937 random(529);std::normal_distribution<double> noise(0,.025);
    const auto phase=std::polar(1.7,.83);
    for(std::size_t i=0;i<count;++i) {
        std::complex<double> sample{};
        if(i>=delay) {
            const auto position=static_cast<double>(i-delay)*rate;
            const auto index=static_cast<std::size_t>(position);
            if(index<analytic.size()) {
                const auto fraction=position-static_cast<double>(index);
                sample=analytic[index]*(1-fraction);
                if(index+1<analytic.size())sample+=analytic[index+1]*fraction;
            }
        }
        pcm[i]=static_cast<float>((phase*sample).real()+noise(random));
    }
    return pcm;
}
std::vector<modem::PatternBurst> capture(const std::vector<float>& samples,const modem::Config& c,
                                      modem::PatternSearch search,std::size_t chunk) {
    modem::PatternCorrelator receiver(c,search,4*1024*1024);
    for(std::size_t offset=0;offset<samples.size();) {
        const auto n=std::min(chunk,samples.size()-offset);receiver.push(std::span(samples).subspan(offset,n));offset+=n;
        check(receiver.working_bytes()<=4*1024*1024,"streaming correlator exceeded its workspace");
    }
    receiver.finish();check(!receiver.synchronized(),"silence/end must clear correlator synchronization");
    return receiver.take_bursts();
}
const modem::PatternBurst& best(const std::vector<modem::PatternBurst>& bursts) {
    check(!bursts.empty(),"clock-window search did not detect the sampled bit burst");
    return *std::max_element(bursts.begin(),bursts.end(),[](const auto& a,const auto& b){return a.score<b.score;});
}
void sampled_bits_and_rates(bool shaped) {
    auto c=config();c.pulse_shaping=shaped;const Bytes bits{0,0,1};const auto samples=waveform(bits,c,137);
    modem::PatternSearch search;search.start_offset_seconds=.023+
        static_cast<double>(modem::pattern_pulse_padding_samples(c))/c.sample_rate;search.start_uncertainty_seconds=.003;
    search.frequency_offsets_hz={0};
    const auto all=capture(samples,c,search,samples.size()),chunks=capture(samples,c,search,37);
    check(best(all).bits==bits && best(chunks).bits==bits,"unknown phase and non-chip-aligned three-bit burst must recover exactly");
    check(best(all).first_sample==best(chunks).first_sample,"input chunk boundaries must not select a different clock hypothesis");
    auto rate_search=search;rate_search.clock_errors_ppm={0,5000};rate_search.frequency_offsets_hz={0,c.carrier_hz*.005};
    const Bytes longer{0,1,1,0,1,0};
    const auto altered=capture(waveform(longer,c,137,1.005),c,rate_search,173);
    check(best(altered).bits==longer,"finite clock-rate and frequency hypotheses must jointly recover physical rate error");
    auto wrong=c;wrong.spreading_seed[0]^=0x5a;
    check(capture(samples,wrong,search,128).empty(),"incorrect pattern key must not acquire a strong waveform");
}
void late_clock_fragment() {
    const auto c=config();const Bytes bits{1,1,0,1,0};
    const auto full=waveform(bits,c,0);
    const auto padding=modem::pattern_pulse_padding_samples(c);
    const auto crop=padding+2*modem::symbol_sample_count(c);
    std::vector<float> fragment(full.begin()+static_cast<std::ptrdiff_t>(crop),full.end());
    modem::PatternSearch search;search.start_offset_seconds=-static_cast<double>(crop-padding)/c.sample_rate;
    search.start_uncertainty_seconds=1./c.sample_rate;search.frequency_offsets_hz={0};
    const auto result=capture(fragment,c,search,211);
    check(best(result).bits==Bytes({0,1,0}) && best(result).first_stream_symbol==2,
          "clock-derived cropped reception must use the surviving symbols' actual stream positions");
}
void weak_prefix_does_not_borrow_confidence() {
    auto c=config();c.scramble=false;c.dsss=false;c.spreading_factor=64;
    constexpr std::size_t delay=137;
    const auto payload_start=delay+modem::pattern_pulse_padding_samples(c);
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    auto samples=waveform({1,0,0,1},c,delay);
    std::mt19937_64 random(197);std::normal_distribution<double> noise(0,3.5);
    for(std::size_t i=0;i<payload_start+symbol;++i)samples[i]+=static_cast<float>(noise(random));
    modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(payload_start)/c.sample_rate;
    search.start_uncertainty_seconds=0;search.frequency_offsets_hz={0};
    modem::PatternCorrelator prefix(c,search,4*1024*1024);
    prefix.push(std::span(samples).first(payload_start+symbol+2));
    check(prefix.acquiring() && !prefix.synchronized(),
          "weak-prefix fixture must retain a candidate without admitting it");
    for(const bool preserve:{false,true}) {
        search.preserve_symbol_gaps=preserve;
        const auto result=capture(samples,c,search,127);
        check(best(result).bits==Bytes({0,0,1}) && best(result).first_sample>=payload_start+symbol,
              "a strong clock-window symbol cannot confirm an earlier weak noise candidate");
    }
}
double direct_score(std::span<const float> pcm,modem::PatternCode& code,const modem::Config& c,
                    std::uint64_t first_sample,std::uint64_t stream_symbol,unsigned bit) {
    double xc=0,xs=0,cc=0,ss=0,cs=0,energy=0;
    for(std::size_t i=0;i<pcm.size();++i) {
        const auto carrier=std::polar(1.,2*std::numbers::pi*c.carrier_hz*
            static_cast<double>(first_sample+i)/c.sample_rate);
        const auto reference=carrier*code.shaped_value(stream_symbol*code.chips_per_symbol(),bit,static_cast<double>(i));
        const auto real=reference.real(),imaginary=reference.imag(),x=static_cast<double>(pcm[i]);
        xc+=x*real;xs+=x*imaginary;cc+=real*real;ss+=imaginary*imaginary;cs+=real*imaginary;energy+=x*x;
    }
    const auto explained=(ss*xc*xc+cc*xs*xs-2*cs*xc*xs)/(cc*ss-cs*cs);
    const auto fraction=std::clamp(explained/energy,0.,1.-1e-15);
    return -.5*static_cast<double>(pcm.size()-2)*std::log1p(-fraction);
}
void set_symbol_evidence(std::vector<float>& samples,const modem::Config& c,std::size_t payload_start,
                        std::uint64_t index,unsigned bit,double target,std::uint32_t seed) {
    const auto count=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto start=payload_start+static_cast<std::size_t>(index)*count;
    const std::vector<float> clean(samples.begin()+static_cast<std::ptrdiff_t>(start),
                                   samples.begin()+static_cast<std::ptrdiff_t>(start+count));
    std::vector<float> noise(count);std::mt19937 random(seed);std::normal_distribution<float> distribution(0,.5F);
    for(auto& value:noise)value=distribution(random);
    modem::PatternCode code(c,c.stream_epoch);
    const auto measure=[&](double scale) {
        for(std::size_t i=0;i<count;++i)samples[start+i]=static_cast<float>(scale*clean[i])+noise[i];
        return direct_score(std::span(samples).subspan(start,count),code,c,start,index,bit);
    };
    check(measure(0)<target && measure(1)>target,"noise fixture must bracket its intended pattern evidence");
    double low=0,high=1;
    for(unsigned iteration=0;iteration<28;++iteration) {
        const auto middle=(low+high)/2;
        if(measure(middle)<target)low=middle;else high=middle;
    }
    const auto actual=measure((low+high)/2);
    const auto alternative=direct_score(std::span(samples).subspan(start,count),code,c,start,index,1-bit);
    check(std::abs(actual-target)<.01 && actual-alternative>1,
          "weak-symbol fixture must retain a clear bit preference at its intended evidence level");
}
void shaped_raw_sample_evidence() {
    const auto c=config();const Bytes bits{0,1,0};
    const auto padding=modem::pattern_pulse_padding_samples(c);
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    // This binary-exact clock hint has no fractional-sample rounding; the
    // independent reference below can use integer sample coordinates.
    constexpr std::uint64_t payload_start=375;
    auto samples=waveform(bits,c,static_cast<std::size_t>(payload_start-padding));
    std::mt19937 random(81812);std::normal_distribution<double> noise(0,.5);
    for(auto& sample:samples)sample+=static_cast<float>(noise(random));
    modem::PatternSearch search;search.start_offset_seconds=.0625;
    search.start_uncertainty_seconds=0;search.frequency_offsets_hz={0};search.retain_score=0;
    modem::PatternCorrelator receiver(c,search,4*1024*1024);
    receiver.push(samples);receiver.finish();
    const auto evidence=receiver.candidates();modem::PatternCode code(c,c.stream_epoch);
    check(evidence.size()>=bits.size(),"shaped symbol scores must remain available independently of admission");
    for(std::size_t index=0;index<bits.size();++index) {
        const auto start=payload_start+index*symbol;
        const auto observed=std::span(samples).subspan(static_cast<std::size_t>(start),symbol);
        const auto a=direct_score(observed,code,c,start,index,0),b=direct_score(observed,code,c,start,index,1);
        const auto& actual=evidence[index];
        check(actual.first_sample==start && actual.end_sample==start+symbol,
              "pulse overlap cannot expand or duplicate a symbol's raw noise samples");
        check(std::abs(actual.score-std::max(a,b))<1e-6*std::max(1.,actual.score) &&
              std::abs(actual.alternative_score-std::min(a,b))<1e-6*std::max(1.,actual.alternative_score),
              "shaped pattern confidence must equal a direct two-basis fit on independent raw samples");
    }
    check(best(receiver.take_bursts()).bits==bits,"raw-sample shaped confidence must recover private symbols in noise");
}
void shaped_partial_chips() {
    auto c=config();c.integration_seconds=172.25/c.sample_rate;
    check(modem::pattern_pulse_enabled(c),"partial-chip fixture must exercise pulse shaping");
    const Bytes bits{0,1,1,0,1,0};constexpr std::size_t delay=137;
    modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(delay+
        modem::pattern_pulse_padding_samples(c))/c.sample_rate;
    search.start_uncertainty_seconds=0;search.frequency_offsets_hz={0};
    check(best(capture(waveform(bits,c,delay),c,search,113)).bits==bits,
          "continuous shaped partial-chip symbols must preserve absolute stream addresses and bit labels");
}
void majority_obscured_symbol_is_independent() {
    auto c=config();c.integration_seconds=2;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    constexpr std::size_t payload_start=375;
    auto samples=waveform({1,0},c,payload_start-modem::pattern_pulse_padding_samples(c));
    std::mt19937 random(27219);std::normal_distribution<float> noise(0,.55F);
    // Preserve time while completely replacing three quarters of the first
    // symbol. Its remaining quarter must supply its own admission evidence.
    for(std::size_t i=payload_start;i<payload_start+3*symbol/4;++i)samples[i]=noise(random);
    modem::PatternSearch search;search.start_offset_seconds=.0625;
    search.start_uncertainty_seconds=0;search.frequency_offsets_hz={0};
    modem::PatternCorrelator receiver(c,search,1024*1024);
    const auto first_end=payload_start+symbol;
    receiver.push(std::span(samples).first(first_end-1));
    check(receiver.provisional().bits.empty() && receiver.take_bursts().empty(),
          "an incomplete partially obscured symbol must not be reported before its endpoint");
    receiver.push(std::span(samples).subspan(first_end-1,1));
    const auto first=receiver.provisional();
    check(first.bits==Bytes({1}) && first.first_stream_symbol==0 && first.end_sample==first_end,
          "a symbol with a mostly obscured prefix must be visible at its endpoint on its own evidence");
    check(receiver.take_bursts().empty(),"provisional symbol must not be duplicated as a completed burst");
    receiver.push(std::span(samples).subspan(first_end));receiver.finish();
    check(best(receiver.take_bursts()).bits==Bytes({1,0}),
          "a mostly obscured first symbol must retain the correct next-epoch continuation");
}
void completely_obscured_symbols_do_not_block_later_symbols() {
    auto c=config();c.integration_seconds=2;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    constexpr std::size_t payload_start=375;
    auto samples=waveform({1,0,1,0},c,payload_start-modem::pattern_pulse_padding_samples(c));
    std::mt19937 random(53371);std::normal_distribution<float> noise(0,.55F);
    // The receiver is running before the transmitter. First and middle
    // symbols are wholly lost, with no deleted samples or supplied bit hints.
    for(const std::size_t index:{0U,2U})
        for(std::size_t i=payload_start+index*symbol;i<payload_start+(index+1)*symbol;++i)samples[i]=noise(random);
    modem::PatternSearch search;search.start_offset_seconds=.0625;
    search.start_uncertainty_seconds=0;search.frequency_offsets_hz={0};
    modem::PatternCorrelator receiver(c,search,1024*1024);
    receiver.push(std::span(samples).first(payload_start+symbol));
    check(receiver.provisional().bits.empty(),"a completely obscured first symbol must not acquire");
    receiver.push(std::span(samples).subspan(payload_start+symbol,symbol));
    const auto second=receiver.provisional();
    check(second.bits==Bytes({0}) && second.first_stream_symbol==1 && second.end_sample==payload_start+2*symbol,
          "a missed first symbol must not block the independently confident second symbol");
    receiver.push(std::span(samples).subspan(payload_start+2*symbol,symbol));
    check(receiver.provisional().bits.empty(),"a wholly obscured middle symbol must not borrow earlier confidence");
    receiver.push(std::span(samples).subspan(payload_start+3*symbol,symbol));
    const auto fourth=receiver.provisional();
    check(fourth.bits==Bytes({0}) && fourth.first_stream_symbol==3 && fourth.end_sample==payload_start+4*symbol,
          "a missed middle symbol must not block a later independently confident symbol at a new epoch");
    receiver.push(std::span(samples).subspan(payload_start+4*symbol));receiver.finish();
    const auto bursts=receiver.take_bursts();
    check(bursts.size()==2 && bursts[0].bits==Bytes({0}) && bursts[0].first_stream_symbol==1 &&
          bursts[1].bits==Bytes({0}) && bursts[1].first_stream_symbol==3,
          "missing symbols must remain gaps rather than fabricated bits in recovered bursts");
}
void weak_tails_expire_without_blocking_independent_symbols() {
    auto c=config();c.integration_seconds=2;c.pulse_shaping=false;
    constexpr std::size_t payload_start=375;
    const Bytes bits{1,0,1,0,1,0,1};
    auto samples=waveform(bits,c,payload_start);
    // Each intervening symbol exceeds retention but cannot be admitted alone.
    // Their five-symbol chain can eventually pass if the gap is not bounded.
    for(std::uint64_t index=1;index<=5;++index)
        set_symbol_evidence(samples,c,payload_start,index,bits[index],9,static_cast<std::uint32_t>(991+index));
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};
    for(const auto gap:{0.,6.}) {
        search.max_gap_seconds=gap;
        const auto result=capture(samples,c,search,113);
        check(result.size()==2 && result[0].bits==Bytes({1}) && result[0].first_stream_symbol==0 &&
              result[1].bits==Bytes({1}) && result[1].first_stream_symbol==6,
              "default and custom gap limits must discard weak tails while preserving a later independent symbol");
    }
    search.max_gap_seconds=100;
    check(best(capture(samples,c,search,113)).bits==bits,
          "weak-tail fixture must establish its own chain confidence when the configured gap allows it");

    auto boundary=waveform({1,0,1},c,payload_start);
    set_symbol_evidence(boundary,c,payload_start,1,0,6,711);
    set_symbol_evidence(boundary,c,payload_start,2,1,23,713);
    search.max_gap_seconds=0;
    const auto recovered=capture(boundary,c,search,113);
    check(recovered.size()==2 && recovered[0].bits==Bytes({1}) && recovered[0].first_stream_symbol==0 &&
          recovered[1].bits==Bytes({1}) && recovered[1].first_stream_symbol==2,
          "a newly confident symbol must survive gap expiry without confirming an earlier weak bit");
    search.preserve_symbol_gaps=true;
    const auto timed=capture(boundary,c,search,113);
    check(best(timed).bits==Bytes({1,modem::missing_pattern_bit,1}) && best(timed).first_stream_symbol==0,
          "opt-in recovery must turn an unconfirmed middle bit into a timed placeholder");
    check(std::abs(best(timed).score-recovered[0].score-recovered[1].score)<1e-8*best(timed).score,
          "discarded weak bit evidence must not contribute to the preserved burst's confidence");
    search.preserve_symbol_gaps=false;

    auto combined=waveform({1,0,1},c,payload_start);
    set_symbol_evidence(combined,c,payload_start,1,0,6,711);
    set_symbol_evidence(combined,c,payload_start,2,1,100,713);
    search.max_gap_seconds=6;
    check(best(capture(combined,c,search,113)).bits==Bytes({1,0,1}),
          "a valid aggregate-chain admission must preserve its pending bit instead of splitting the packet");
}
void timed_gaps_preserve_admitted_clock_and_trim_silence() {
    auto c=config();c.integration_seconds=2;c.pulse_shaping=false;
    constexpr std::size_t payload_start=375;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    auto samples=waveform({1,0,1,0,1,0},c,payload_start);
    for(const std::size_t index:{0U,2U,3U})
        std::fill_n(samples.begin()+static_cast<std::ptrdiff_t>(payload_start+index*symbol),symbol,0.F);
    std::fill(samples.begin()+static_cast<std::ptrdiff_t>(payload_start+6*symbol),samples.end(),0.F);
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};search.preserve_symbol_gaps=true;search.max_gap_seconds=4;
    modem::PatternCorrelator receiver(c,search,1024*1024);
    receiver.push(std::span(samples).first(payload_start+symbol));
    check(receiver.provisional().bits.empty(),"timing gaps must not fabricate a leading symbol before admission");
    receiver.push(std::span(samples).subspan(payload_start+symbol,symbol));
    const auto admitted=receiver.provisional();
    check(admitted.bits==Bytes({0}) && admitted.first_stream_symbol==1,
          "timed gap recovery must acquire on independent bit evidence");
    receiver.push(std::span(samples).subspan(payload_start+2*symbol,2*symbol));
    const auto pending=receiver.provisional();
    check(pending.bits==admitted.bits && pending.end_sample==admitted.end_sample && pending.score==admitted.score &&
          pending.complete && receiver.synchronized() && receiver.take_bursts().empty(),
          "two missing slots must expose the completed observed span while retaining its clock without adding bits or confidence");
    receiver.push(std::span(samples).subspan(payload_start+4*symbol,symbol));
    const auto resumed=receiver.provisional();
    check(resumed.bits==Bytes({0,modem::missing_pattern_bit,modem::missing_pattern_bit,1}) &&
          resumed.first_stream_symbol==1 && resumed.end_sample==payload_start+5*symbol,
          "a later independent symbol must publish the exact count of missing clock slots");
    receiver.push(std::span(samples).subspan(payload_start+5*symbol));receiver.finish();
    const auto result=receiver.take_bursts();
    check(result.size()==1 && result[0].bits==Bytes({0,modem::missing_pattern_bit,modem::missing_pattern_bit,1,0}) &&
          result[0].first_stream_symbol==1 && result[0].end_sample==payload_start+6*symbol,
          "finish must trim all unresolved trailing silence slots from a preserved burst");
    check(best(capture(samples,c,search,37)).bits==result[0].bits,
          "timed gap preservation must be independent of input chunk boundaries");
    search.preserve_symbol_gaps=false;
    const auto split=capture(samples,c,search,113);
    check(split.size()==2 && split[0].bits==Bytes({0}) && split[0].first_stream_symbol==1 &&
          split[1].bits==Bytes({1,0}) && split[1].first_stream_symbol==4,
          "default recovery must continue splitting bursts at missing symbols");
}
void timed_gap_expiry() {
    auto c=config();c.integration_seconds=2;c.pulse_shaping=false;
    constexpr std::size_t payload_start=375;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    auto samples=waveform({1,0,1,0,1,0},c,payload_start);
    std::fill_n(samples.begin()+static_cast<std::ptrdiff_t>(payload_start+symbol),3*symbol,0.F);
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};search.preserve_symbol_gaps=true;search.max_gap_seconds=0;
    const auto expired=capture(samples,c,search,113);
    check(expired.size()==2 && expired[0].bits==Bytes({1}) && expired[0].first_stream_symbol==0 &&
          expired[1].bits==Bytes({1,0}) && expired[1].first_stream_symbol==4,
          "gap expiry must trim missing slots and allow independent acquisition afterward");
    search.max_gap_seconds=6;
    check(best(capture(samples,c,search,113)).bits==Bytes({1,modem::missing_pattern_bit,modem::missing_pattern_bit,
          modem::missing_pattern_bit,1,0}),"configured finite gap duration must permit the corresponding missing slots");
    search.bit_limit=2;
    const auto bounded=capture(samples,c,search,113);
    check(bounded.size()==2 && bounded[0].bits==Bytes({1}) && bounded[1].bits==Bytes({1,0}),
          "gap expansion must stop at the bit limit without rejecting an exact-limit clean suffix");
}
void default_gap_expiry_releases_payload_workspace() {
    auto c=config();c.integration_seconds=2;c.pulse_shaping=false;
    constexpr std::size_t payload_start=375;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const Bytes prefix{1,0,1,0,0,1,1,0},suffix{0,1,1};
    Bytes bits=prefix;bits.insert(bits.end(),4,0);bits.insert(bits.end(),suffix.begin(),suffix.end());
    auto samples=waveform(bits,c,payload_start);
    const auto gap_start=payload_start+prefix.size()*symbol;
    std::fill_n(samples.begin()+static_cast<std::ptrdiff_t>(gap_start),4*symbol,0.F);
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};search.preserve_symbol_gaps=true;
    modem::PatternCorrelator receiver(c,search,1024*1024);
    const auto baseline=receiver.working_bytes();
    receiver.push(std::span(samples).first(gap_start));
    check(receiver.provisional().bits==prefix && receiver.working_bytes()>baseline,
          "a confirmed prefix must allocate its bounded active payload buffer");
    receiver.push(std::span(samples).subspan(gap_start,3*symbol));
    check(receiver.synchronized() && receiver.provisional().bits==prefix && receiver.take_bursts().empty(),
          "exactly six seconds of missing symbols must retain the established clock for recovery");
    receiver.push(std::span(samples).subspan(gap_start+3*symbol,symbol));
    check(!receiver.synchronized() && receiver.provisional().bits.empty(),
          "the first failed symbol beyond six seconds must terminate the active message");
    const auto ended=receiver.take_bursts();
    check(ended.size()==1 && ended[0].bits==prefix && ended[0].complete && ended[0].end_sample==gap_start,
          "gap termination must publish only the confirmed prefix without trailing placeholders");
    check(receiver.working_bytes()==baseline,
          "draining an expired message must reclaim its payload allocation from the correlator workspace");
    receiver.push(std::span(samples).subspan(gap_start+4*symbol,suffix.size()*symbol));
    const auto resumed=receiver.provisional();
    check(resumed.bits==suffix && resumed.first_stream_symbol==prefix.size()+4,
          "a terminated message must not block independent acquisition on the continuing clock");
    receiver.finish();
    const auto finished=receiver.take_bursts();
    check(finished.size()==1 && finished[0].bits==suffix && receiver.working_bytes()==baseline,
          "end of capture must publish the final message and reclaim its active payload allocation");
}
void timed_gaps_cannot_resolve_stream_phase() {
    auto transmitted=config();transmitted.integration_seconds=.3;transmitted.pulse_shaping=false;
    transmitted.stream_phase_samples=1200;
    constexpr std::size_t payload_start=375;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(transmitted));
    auto samples=waveform({1,0,1,0,1},transmitted,payload_start);
    std::fill_n(samples.begin()+static_cast<std::ptrdiff_t>(payload_start+symbol),2*symbol,0.F);
    auto received=transmitted;received.stream_phase_samples=0;
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};search.search_stream_phases=true;
    search.preserve_symbol_gaps=true;search.max_gap_seconds=1;
    const auto result=capture(samples,received,search,113);
    check(result.size()==2 && result[0].bits==Bytes({1}) && result[0].first_stream_symbol==0 &&
          result[1].bits==Bytes({0,1}) && result[1].first_stream_symbol==3,
          "a pending gap must split before a new phase group even when the new symbol is independently confident");
}
void complete_packet_closes_before_first_timed_gap() {
    auto c=config();c.integration_seconds=2;c.pulse_shaping=false;
    constexpr std::size_t payload_start=375;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    static std::size_t calls=0,observed_size=0,observed_confirmed=0;
    static std::uint64_t expected_epoch=0;expected_epoch=c.stream_epoch;
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};search.max_gap_seconds=6;search.packet_content_limit=17;
    search.packet_complete=[](const modem::PatternBurst& burst,std::size_t confirmed,
                              const modem::Config& current,std::size_t limit) {
        ++calls;observed_size=burst.bits.size();observed_confirmed=confirmed;
        check(current.stream_epoch==expected_epoch && limit==17,
              "packet completion callback must receive the current config and content limit");
        return confirmed==2 && burst.bits[0]==1 && burst.bits[1]==0;
    };
    auto samples=waveform({1,0,1,0,1,1},c,payload_start);
    samples.resize(payload_start+6*symbol);
    std::fill_n(samples.begin()+static_cast<std::ptrdiff_t>(payload_start+2*symbol),2*symbol,0.F);
    for(const bool preserve:{false,true}) {
        calls=0;search.preserve_symbol_gaps=preserve;
        const auto result=capture(samples,c,search,113);
        check(result.size()==2 && result[0].bits==Bytes({1,0}) && result[0].end_sample==payload_start+2*symbol &&
              result[1].bits==Bytes({1,1}) && result[1].first_stream_symbol==4,
              "a completed prefix must close before a gap and preserve the next burst's clock index");
        check(calls==(preserve?1U:0U),"packet completion callback must run only on the first opt-in gap");
    }
    auto incomplete=waveform({1,1,1,0,1,1},c,payload_start);
    incomplete.resize(payload_start+6*symbol);
    std::fill_n(incomplete.begin()+static_cast<std::ptrdiff_t>(payload_start+2*symbol),2*symbol,0.F);
    calls=0;
    check(best(capture(incomplete,c,search,113)).bits==Bytes({1,1,modem::missing_pattern_bit,
          modem::missing_pattern_bit,1,1}) && calls==1,
          "an incomplete prefix must preserve timed gaps without repeatedly invoking its completion callback");
    auto weak=waveform({1,0,1,0,1,1},c,payload_start);
    weak.resize(payload_start+6*symbol);
    set_symbol_evidence(weak,c,payload_start,2,1,6,771);
    std::fill_n(weak.begin()+static_cast<std::ptrdiff_t>(payload_start+3*symbol),symbol,0.F);
    calls=0;
    const auto trimmed=capture(weak,c,search,113);
    check(trimmed.size()==2 && trimmed[0].bits==Bytes({1,0}) && trimmed[0].end_sample==payload_start+2*symbol &&
          trimmed[1].bits==Bytes({1,1}) && trimmed[1].first_stream_symbol==4 &&
          calls==1 && observed_size==3 && observed_confirmed==2,
          "completion must inspect only the confirmed prefix and discard its unconfirmed weak tail");
    weak=waveform({1,0,1,0,1,1},c,payload_start);weak.resize(payload_start+6*symbol);
    set_symbol_evidence(weak,c,payload_start,2,1,6,771);
    set_symbol_evidence(weak,c,payload_start,3,0,23,773);
    calls=0;
    const auto resumed=capture(weak,c,search,113);
    check(resumed.size()==2 && resumed[0].bits==Bytes({1,0}) && resumed[1].bits==Bytes({0,1,1}) &&
          resumed[1].first_stream_symbol==3 && calls==1 && observed_confirmed==2,
          "closing a completed prefix before weak-tail conversion must preserve the current independent symbol");
}
void independent_epoch_recovers_fractional_symbol_phase(bool short_fallback,bool missing_first=false) {
    auto transmitted=config();transmitted.integration_seconds=short_fallback?.3:7.3;
    const auto symbol=modem::symbol_sample_count(transmitted);
    const auto step=std::gcd(symbol,static_cast<std::uint64_t>(transmitted.sample_rate));
    transmitted.stream_phase_samples=(std::min(symbol,static_cast<std::uint64_t>(transmitted.sample_rate))-1)/step*step;
    const Bytes bits{1,0,0,1,0,1,1,0,1,0};
    constexpr std::size_t payload_start=375;
    auto samples=waveform(bits,transmitted,payload_start-modem::pattern_pulse_padding_samples(transmitted));
    if(missing_first) {
        std::mt19937 random(95731);std::normal_distribution<float> noise(0,.55F);
        for(std::size_t i=payload_start;i<payload_start+symbol;++i)samples[i]=noise(random);
    }
    auto received=transmitted;received.stream_phase_samples=0;
    modem::PatternSearch search;search.search_stream_phases=true;search.start_offset_seconds=.0625;
    search.start_uncertainty_seconds=0;search.frequency_offsets_hz={0};
    std::vector<modem::PatternBurst> bursts;
    if(short_fallback) {
        modem::PatternReceiver receiver(received,256*1024,search);
        check(receiver.clock_windowed(),"fractional short-symbol fixture must use the streaming fallback");
        for(std::size_t offset=0;offset<samples.size();) {
            const auto count=std::min<std::size_t>(113,samples.size()-offset);
            receiver.push(std::span(samples).subspan(offset,count));offset+=count;
            check(receiver.working_bytes()<=256*1024,"phase recovery exceeded the streaming fallback workspace");
        }
        receiver.finish();bursts=receiver.take_bursts();
    } else bursts=capture(samples,received,search,113);
    const auto& recovered=best(bursts);
    const auto first=missing_first?1U:0U;
    const Bytes expected(bits.begin()+first,bits.end());
    check(recovered.bits==expected && recovered.first_stream_symbol==first,
          "an independently admitted epoch must retain all bits across fractional second boundaries");
    // Several phases can remain indistinguishable. The reported phase must
    // reproduce every observed epoch/counter address, not an arbitrary exact
    // phase inferred from the transmitter fixture.
    for(std::uint64_t index=first;index<bits.size();++index) {
        const auto actual=modem::symbol_stream_address(transmitted.stream_epoch,transmitted.stream_phase_samples,
            index,symbol,transmitted.sample_rate);
        const auto acquired=modem::symbol_stream_address(received.stream_epoch,recovered.stream_phase_samples,
            index,symbol,received.sample_rate);
        check(actual.epoch==acquired.epoch && actual.ordinal==acquired.ordinal,
              "acquired subsecond phase must reproduce the observed per-symbol stream positions");
    }
}
void compact_clock_search_preserves_evidence_and_bounds() {
    auto transmitted=config();transmitted.integration_seconds=.3;transmitted.stream_phase_samples=1200;
    constexpr std::size_t payload_start=375;
    const Bytes bits{1,0,0,1,0,1,1,0,1,0};
    const auto samples=waveform(bits,transmitted,payload_start-modem::pattern_pulse_padding_samples(transmitted));
    auto received=transmitted;received.stream_phase_samples=0;
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};search.search_stream_phases=true;search.candidate_limit=32;
    modem::PatternCorrelator ordinary(received,search,2*1024*1024);
    search.compact_clock_search=true;
    modem::PatternReceiver compact(received,2*1024*1024,search);
    check(compact.clock_windowed(),"compact mode must select the correlator even when FFT storage would fit");
    for(std::size_t offset=0;offset<samples.size();) {
        const auto count=std::min<std::size_t>(113,samples.size()-offset);
        ordinary.push(std::span(samples).subspan(offset,count));compact.push(std::span(samples).subspan(offset,count));offset+=count;
    }
    ordinary.finish();compact.finish();
    check(best(ordinary.take_bursts()).bits==bits && best(compact.take_bursts()).bits==bits,
          "compact diagnostics and scratch must preserve every decoded symbol");
    const auto original=ordinary.candidates(),reduced=compact.candidates();
    check(original.size()==reduced.size(),"compact mode must retain the requested bounded evidence history");
    for(std::size_t i=0;i<original.size();++i) {
        const auto& a=original[i];const auto& b=reduced[i];
        check(a.first_sample==b.first_sample && a.end_sample==b.end_sample && a.stream_symbol==b.stream_symbol &&
              a.bit==b.bit && a.stream_phase_samples==b.stream_phase_samples &&
              std::abs(a.score-b.score)<1e-6*std::max(1.,a.score) &&
              std::abs(a.alternative_score-b.alternative_score)<1e-6*std::max(1.,a.alternative_score),
              "smaller projection blocks must preserve the full correlation evidence and phase decisions");
    }
    check(compact.take_chip_constellation().size()<=64,"compact mode must cap only its diagnostic point history");
    auto hours=config();hours.integration_seconds=4*3600;hours.bandwidth_hz=1;
    modem::PatternSearch full;full.start_offset_seconds=0;full.start_uncertainty_seconds=7;
    full.search_stream_phases=true;full.compact_clock_search=true;
    modem::PatternReceiver long_receiver(hours,2*1024*1024,full);
    const auto bytes=long_receiver.working_bytes();
    check(long_receiver.clock_windowed() && bytes<64*1024,
          "four-hour minimum-bandwidth epoch must retain full timing/frequency coverage within 64 KiB");
    std::array<float,4096> noise{};std::mt19937 random(3181);std::normal_distribution<float> distribution(0,.1F);
    for(auto& sample:noise)sample=distribution(random);
    long_receiver.push(noise);
    check(long_receiver.working_bytes()==bytes,"idle compact epoch must not allocate PCM or duration-proportional state");
    hours.integration_seconds+=.3;
    modem::PatternReceiver fractional_hours(hours,2*1024*1024,full);
    const auto fractional_bytes=fractional_hours.working_bytes();
    check(fractional_bytes<64*1024,"compact four-hour phase search must retain its additional fits within 64 KiB");
    fractional_hours.push(noise);
    check(fractional_hours.working_bytes()==fractional_bytes,"unresolved subsecond phase must not grow idle compact state");
}
void bounded_hours_and_noise() {
    auto c=config();c.integration_seconds=4*3600;
    modem::PatternSearch search;search.start_offset_seconds=.03;search.start_uncertainty_seconds=.002;
    search.clock_errors_ppm={-100,0,100};search.frequency_offsets_hz={0};
    auto phase_config=c;phase_config.integration_seconds+=.3;
    auto phase_search=search;phase_search.search_stream_phases=true;
    modem::PatternCorrelator long_receiver(phase_config,phase_search,1024*1024);
    const auto initial=long_receiver.working_bytes();
    check(long_receiver.diagnostics().pattern_score.has_value() && long_receiver.diagnostics().snr_db==0,
          "pattern evidence must not be reported as an SNR measurement");
    std::array<float,4096> noise{};std::mt19937 rng(8927);std::normal_distribution<float> normal(0,.1F);
    for(auto& sample:noise)sample=normal(rng);
    for(unsigned i=0;i<6;++i)long_receiver.push(noise);
    check(long_receiver.working_bytes()==initial && initial<256*1024,
          "hour-long symbol accumulation with unresolved epoch phase must retain only the finite hypothesis bank");
    long_receiver.finish();check(long_receiver.take_bursts().empty(),"a short prefix cannot fabricate a completed hour-long symbol");
    auto enormous=search;enormous.start_uncertainty_seconds=1000000;
    rejects([&]{modem::PatternCorrelator rejected(c,enormous,1024*1024);},"unaffordable half-chip coverage must reject, not silently coarsen");
    auto no_clock=search;no_clock.start_offset_seconds.reset();
    rejects([&]{modem::PatternCorrelator rejected(c,no_clock,1024*1024);},"constant-memory long search must require its clock coverage");
    auto seconds_window=search;seconds_window.start_uncertainty_seconds=2;
    seconds_window.frequency_offsets_hz.clear();seconds_window.clock_errors_ppm={0};
    modem::PatternCorrelator seconds_receiver(c,seconds_window,8*1024*1024);
    check(seconds_receiver.working_bytes()<8*1024*1024,
          "a four-hour symbol with the full five-frequency +/-2-second clock bank fits eight MiB");
    seconds_window.clock_errors_ppm={-100,0,100};
    rejects([&]{modem::PatternCorrelator rejected(c,seconds_window,8*1024*1024);},
            "three clock rates must not be silently admitted inside a one-rate memory budget");
    modem::PatternCorrelator three_rates(c,seconds_window,32*1024*1024);
    check(three_rates.working_bytes()<32*1024*1024,"finite three-rate seconds-wide coverage has an explicit affordable ceiling");
    c=config();modem::PatternCorrelator receiver(c,search,1024*1024);
    for(unsigned i=0;i<6;++i)receiver.push(noise);
    receiver.finish();check(receiver.take_bursts().empty(),"noise-only clock search must not manufacture a bit burst");
    modem::PatternCorrelator cancelled(c,search,1024*1024);std::stop_source stop;stop.request_stop();
    rejects([&]{cancelled.push(noise,stop.get_token());},"streaming long search must honor cancellation");
}
}
int main(){try{sampled_bits_and_rates(true);sampled_bits_and_rates(false);late_clock_fragment();weak_prefix_does_not_borrow_confidence();shaped_raw_sample_evidence();shaped_partial_chips();majority_obscured_symbol_is_independent();completely_obscured_symbols_do_not_block_later_symbols();weak_tails_expire_without_blocking_independent_symbols();timed_gaps_preserve_admitted_clock_and_trim_silence();timed_gap_expiry();default_gap_expiry_releases_payload_workspace();timed_gaps_cannot_resolve_stream_phase();complete_packet_closes_before_first_timed_gap();independent_epoch_recovers_fractional_symbol_phase(false);independent_epoch_recovers_fractional_symbol_phase(false,true);independent_epoch_recovers_fractional_symbol_phase(true);compact_clock_search_preserves_evidence_and_bounds();bounded_hours_and_noise();std::cout<<"Streaming clock-window pattern correlator tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
