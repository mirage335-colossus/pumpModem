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
#include <string>

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
    std::vector<modem::PatternBurst> result;
    const auto drain=[&] {
        for(auto& burst:receiver.take_bursts()) {
            if(burst.missing_slots){burst.bits.assign(burst.missing_slots,modem::missing_pattern_bit);burst.missing_slots=0;}
            // Drains can interleave independent frequency hypotheses. Join
            // only contiguous chunks of the same physical candidate, even
            // when another candidate was the last item delivered.
            auto match=result.end();
            auto frequency_distance=static_cast<double>(c.sample_rate)/modem::symbol_sample_count(c);
            for(auto candidate=result.begin();candidate!=result.end();++candidate) {
                const auto distance=std::abs(candidate->frequency_hz-burst.frequency_hz);
                if(!candidate->complete && candidate->stream_first_sample==burst.stream_first_sample &&
                   candidate->stream_first_symbol==burst.stream_first_symbol &&
                   candidate->first_stream_symbol+candidate->bits.size()==burst.first_stream_symbol &&
                   candidate->end_sample==burst.first_sample && distance<=frequency_distance) {
                    match=candidate;frequency_distance=distance;
                }
            }
            if(match!=result.end()) {
                auto& prior=*match;prior.bits.insert(prior.bits.end(),burst.bits.begin(),burst.bits.end());
                prior.complete=burst.complete;prior.end_sample=burst.end_sample;prior.score=burst.score;prior.stream_phase_samples=burst.stream_phase_samples;
                prior.frequency_hz=burst.frequency_hz;
            } else result.push_back(std::move(burst));
        }
    };
    for(std::size_t offset=0;offset<samples.size();) {
        const auto n=std::min(chunk,samples.size()-offset);receiver.push(std::span(samples).subspan(offset,n));offset+=n;
        check(receiver.working_bytes()<=4*1024*1024,"streaming correlator exceeded its workspace");drain();
    }
    receiver.finish();drain();check(!receiver.synchronized(),"capture stop must release active correlator state without claiming stream end");
    return result;
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
    if(best(altered).bits!=longer) {
        std::string detail="finite clock-rate and frequency hypotheses must jointly recover physical rate error:";
        for(const auto& burst:altered) {
            detail+=" ["+std::to_string(burst.first_sample)+","+std::to_string(burst.end_sample)+")@"+std::to_string(burst.frequency_hz)+" ";
            for(auto bit:burst.bits)detail+=std::to_string(bit);
        }
        throw Error(detail);
    }
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
    for(const std::size_t chunk:{37U,127U}) {
        const auto result=capture(samples,c,search,chunk);
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
void stronger_significance_rejects_marginal_symbol() {
    auto c=config();c.integration_seconds=.1;c.pulse_shaping=false;
    constexpr std::size_t start=375;
    auto samples=waveform({1},c,start);
    set_symbol_evidence(samples,c,start,0,1,23,537);
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.frequency_offsets_hz={0};
    auto previous=search;previous.false_alarm_probability=1e-8;
    check(best(capture(samples,c,previous,113)).bits==Bytes({1}),
          "marginal symbol fixture must be admitted by the former significance threshold");
    check(capture(samples,c,search,113).empty(),
          "stronger default significance must leave marginal isolated evidence unadmitted");
    check(best(capture(waveform({1},c,start),c,search,113)).bits==Bytes({1}),
          "stronger significance must retain independently confident sampled symbols");
}
void overlapping_carrier_hypotheses_emit_one_stream() {
    auto c=config();c.scramble=c.dsss=false;c.pulse_shaping=false;
    constexpr std::size_t start=375;
    const Bytes bits{1,0,1,1,0,0,1,0};
    const auto half_width=.5*c.sample_rate/static_cast<double>(modem::symbol_sample_count(c));
    modem::PatternSearch search;search.start_offset_seconds=.0625;
    // Both sides of an unresolved main lobe can independently pass the
    // noise test. They are alternative observations of one physical stream.
    search.frequency_offsets_hz={-half_width,half_width};search.chunk_bits=2;
    const auto result=capture(waveform(bits,c,start),c,search,113);
    check(result.size()==1 && result[0].bits==bits,
          "overlapping carrier hypotheses must not emit duplicate raw streams for one sampled signal");
    check(std::abs(result[0].frequency_hz-c.carrier_hz)==half_width,
          "an unresolved carrier bank must report measured grid evidence without inventing the center");
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
    check(std::abs(evidence.front().admission_threshold-std::log(4/search.false_alarm_probability))<1e-12,
          "first single-hypothesis observation must carry the actual admission reference, not the retention floor");
    for(std::size_t index=0;index<bits.size();++index) {
        const auto start=payload_start+index*symbol;
        const auto observed=std::span(samples).subspan(static_cast<std::size_t>(start),symbol);
        const auto a=direct_score(observed,code,c,start,index,0),b=direct_score(observed,code,c,start,index,1);
        const auto& actual=evidence[index];
        check(std::isfinite(actual.admission_threshold) && actual.admission_threshold>0 &&
              (!index || actual.admission_threshold>evidence[index-1].admission_threshold),
              "correlator evidence must retain its increasing trial-dependent admission reference");
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
    const auto pending=receiver.take_bursts();
    check(pending.size()==1 && pending[0].bits==Bytes({1}) && !pending[0].complete && receiver.take_bursts().empty(),
          "an admitted first symbol must drain once as pending without requiring a second symbol");
    receiver.push(std::span(samples).subspan(first_end));receiver.finish();
    check(best(receiver.take_bursts()).bits==Bytes({0}),
          "a mostly obscured first symbol must retain the correct next-epoch continuation without repeating its first bit");
}
void completely_obscured_symbols_do_not_block_later_symbols() {
    auto c=config();c.integration_seconds=2;c.pulse_shaping=false;
    constexpr std::size_t start=375;const auto symbol=modem::symbol_sample_count(c);
    auto samples=waveform({1,0,1,0},c,start);
    for(const auto index:{0U,2U})std::fill_n(samples.begin()+static_cast<std::ptrdiff_t>(start+index*symbol),symbol,0.F);
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.frequency_offsets_hz={0};
    const auto result=capture(samples,c,search,113);
    check(best(result).bits==Bytes({0,modem::missing_pattern_bit,0}) && best(result).first_stream_symbol==1,
          "a missing first symbol must not obstruct acquisition and an interior gap must retain its coordinate");
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
    search.false_alarm_probability=1e-8; // The 6/23 score pair below brackets this specific admission boundary.
    const auto expired=capture(samples,c,search,113);
    check(expired.size()==2 && expired[0].bits==Bytes({1}) && expired[0].first_stream_symbol==0 &&
          expired[0].complete && expired[1].bits==Bytes({1}) && expired[1].first_stream_symbol==6,
          "six seconds of weak tails must expire while preserving a later independent symbol");
    auto shorter=c;shorter.integration_seconds=.5;
    auto compact=waveform(bits,shorter,payload_start);
    for(std::uint64_t index=1;index<=5;++index)
        set_symbol_evidence(compact,shorter,payload_start,index,bits[index],9,static_cast<std::uint32_t>(991+index));
    check(best(capture(compact,shorter,search,113)).bits==bits,
          "weak symbols must retain their aggregate confidence when their duration stays within six seconds");

    auto boundary=waveform({1,0,1},c,payload_start);
    set_symbol_evidence(boundary,c,payload_start,1,0,6,711);
    set_symbol_evidence(boundary,c,payload_start,2,1,23,713);
    const auto recovered=capture(boundary,c,search,113);
    check(recovered.size()==1 && recovered[0].bits==Bytes({1,modem::missing_pattern_bit,1}) &&
          recovered[0].first_stream_symbol==0 && !recovered[0].complete,
          "a newly confident symbol must preserve an earlier weak position without inventing its bit or ending the stream");

    auto combined=waveform({1,0,1},c,payload_start);
    set_symbol_evidence(combined,c,payload_start,1,0,6,711);
    set_symbol_evidence(combined,c,payload_start,2,1,100,713);
    check(best(capture(combined,c,search,113)).bits==Bytes({1,0,1}),
          "a valid aggregate-chain admission must preserve its pending bit without splitting the stream");
}
void timed_gaps_preserve_admitted_clock_and_trim_silence() {
    auto c=config();c.integration_seconds=2;c.pulse_shaping=false;
    constexpr std::size_t start=375;const auto symbol=modem::symbol_sample_count(c);
    auto samples=waveform({1,0,1,0,1,0},c,start);
    for(const auto index:{0U,2U,3U})std::fill_n(samples.begin()+static_cast<std::ptrdiff_t>(start+index*symbol),symbol,0.F);
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.frequency_offsets_hz={0};
    const auto result=capture(samples,c,search,113);
    check(best(result).bits==Bytes({0,modem::missing_pattern_bit,modem::missing_pattern_bit,1,0}) && !best(result).complete,
          "an interior compact gap must retain positions; short trailing capture noise and EOF never end the stream");
    check(best(capture(samples,c,search,37)).bits==best(result).bits,"chunk boundaries must not change compact-gap recovery");
}

void timed_gap_expiry() {
    auto c=config();c.integration_seconds=2;c.pulse_shaping=false;
    constexpr std::size_t start=375;const auto symbol=modem::symbol_sample_count(c);
    auto samples=waveform({1,0,1,0,1,0},c,start);
    std::fill_n(samples.begin()+static_cast<std::ptrdiff_t>(start+symbol),3*symbol,0.F);
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.frequency_offsets_hz={0};
    const auto expired=capture(samples,c,search,113);
    check(expired.size()==2 && expired[0].bits==Bytes({1}) && expired[0].complete &&
          expired[1].bits==Bytes({1,0}) && !expired[1].complete,
          "six seconds of failed whole symbols must end once; a later acquired segment remains incomplete at EOF");
    c.integration_seconds=1;const auto shorter_symbol=modem::symbol_sample_count(c);
    auto shorter=waveform({1,0,1,0,1,0},c,start);
    std::fill_n(shorter.begin()+static_cast<std::ptrdiff_t>(start+shorter_symbol),3*shorter_symbol,0.F);
    search.bit_limit=2;
    const auto preserved=capture(shorter,c,search,113);
    check(best(preserved).bits==Bytes({1,modem::missing_pattern_bit,modem::missing_pattern_bit,modem::missing_pattern_bit,1,0}),
          "compact gap counters must span a gap larger than decision capacity without shifting later symbols");
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
    search.frequency_offsets_hz={0};
    modem::PatternCorrelator receiver(c,search,1024*1024);
    const auto baseline=receiver.working_bytes();
    receiver.push(std::span(samples).first(gap_start));
    check(receiver.provisional().bits==prefix && receiver.working_bytes()>baseline,
          "a confirmed prefix must allocate its bounded active payload buffer");
    receiver.push(std::span(samples).subspan(gap_start,2*symbol));
    check(receiver.synchronized() && receiver.provisional().bits==prefix,
          "four seconds of missing symbols must retain the established clock for recovery");
    const auto pending=receiver.take_bursts();
    check(pending.size()==1 && pending[0].bits==prefix && !pending[0].complete,
          "confirmed data must drain during a pending gap without ending the message");
    receiver.push(std::span(samples).subspan(gap_start+2*symbol,symbol));
    check(!receiver.synchronized() && receiver.provisional().bits.empty(),
          "three failed two-second symbols must end the active message at six seconds");
    const auto ended=receiver.take_bursts();
    check(ended.size()==1 && ended[0].bits.empty() && ended[0].complete && ended[0].end_sample==gap_start,
          "gap termination must complete the drained prefix without repeating data or adding trailing placeholders");
    check(receiver.working_bytes()==baseline,
          "draining an expired message must reclaim its payload allocation from the correlator workspace");
    receiver.push(std::span(samples).subspan(gap_start+3*symbol,(suffix.size()+1)*symbol));
    const auto resumed=receiver.provisional();
    check(resumed.bits==suffix && resumed.first_stream_symbol==prefix.size()+4,
          "a terminated message must not block independent acquisition on the continuing clock");
    receiver.finish();
    const auto finished=receiver.take_bursts();
    check(finished.size()==1 && finished[0].bits==suffix && !finished[0].complete && receiver.working_bytes()==baseline,
          "end of capture must publish the final message and reclaim its active payload allocation");
}
void drained_stream_keeps_acquisition_state() {
    auto c=config();c.integration_seconds=1;c.pulse_shaping=false;
    constexpr std::size_t start=375;
    const auto symbol=modem::symbol_sample_count(c);
    auto samples=waveform({1},c,start);samples.resize(start+symbol+modem::pattern_absence_samples(c),0.F);
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.frequency_offsets_hz={0};
    modem::PatternCorrelator receiver(c,search,1024*1024);
    receiver.push(std::span(samples).first(start+symbol));
    const auto chunk=receiver.take_bursts();
    check(chunk.size()==1 && chunk[0].bits==Bytes({1}) && !chunk[0].complete && receiver.provisional().bits.empty(),
          "fixture must drain every stored bit while retaining its admitted clock");
    check(receiver.acquiring() && receiver.synchronized(),"draining decisions must not make an admitted stream appear idle to epoch retirement");
    receiver.push(std::span(samples).subspan(start+symbol));
    const auto ended=receiver.take_bursts();
    check(ended.size()==1 && ended[0].complete && !receiver.acquiring(),
          "only physical absence may release the drained stream's acquisition state");
}
void drainable_chunks_and_compact_gaps() {
    for(const bool clock_window:{false,true}) {
        auto c=config();c.integration_seconds=.02;c.pulse_shaping=false;
        constexpr std::size_t delay=375,workspace=1024*1024;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        Bytes bits(64);for(std::size_t i=0;i<bits.size();++i)bits[i]=static_cast<std::uint8_t>((i*7+i/3)&1);
        const auto prefix=bits;bits.insert(bits.end(),200,0);bits.insert(bits.end(),prefix.begin(),prefix.end());
        auto samples=waveform(bits,c,delay);
        std::fill_n(samples.begin()+static_cast<std::ptrdiff_t>(delay+64*symbol),200*symbol,0.F);
        samples.resize(delay+bits.size()*symbol+modem::pattern_absence_samples(c)+3*symbol,0.F);
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(delay)/c.sample_rate;
        search.frequency_offsets_hz={0};search.chunk_bits=16;search.bit_limit=32;search.compact_clock_search=clock_window;
        search.start_uncertainty_seconds=clock_window?.002:0;
        modem::PatternReceiver receiver(c,workspace,search);
        check(receiver.clock_windowed()==clock_window,"chunk fixture selected the wrong physical receiver path");
        Bytes observed;std::size_t terminals=0,runs=0;std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;
        const auto drain=[&] {
            for(const auto& event:receiver.take_bursts()) {
                const auto current=std::pair{event.stream_first_sample,event.stream_first_symbol};
                if(!identity)identity=current;
                check(current==*identity,"draining or a short gap changed the established stream identity");
                check(event.first_stream_symbol==observed.size(),"drained decisions lost their original symbol coordinates");
                check(event.bits.size()<=16 && !(event.missing_slots && !event.bits.empty()),"chunk/run storage is not bounded and disjoint");
                observed.insert(observed.end(),event.bits.begin(),event.bits.end());
                observed.insert(observed.end(),event.missing_slots,modem::missing_pattern_bit);
                runs+=event.missing_slots!=0;
                if(event.complete){++terminals;check(event.bits.empty() && !event.missing_slots && observed.size()==bits.size(),
                    "an empty terminal event must follow all previously drained stream decisions");}
            }
        };
        for(std::size_t pos=0;pos<samples.size();) {
            const auto count=std::min<std::size_t>(113,samples.size()-pos);
            receiver.push(std::span(samples).subspan(pos,count));pos+=count;drain();
            check(receiver.working_bytes()<=workspace,"continuous chunk draining exceeded receiver workspace");
        }
        receiver.finish();drain();
        std::fill(bits.begin()+64,bits.begin()+264,modem::missing_pattern_bit);
        check(observed==bits && terminals==1 && runs==1,"bounded chunks did not preserve the compact gap and unique six-second terminal event");
    }
}
void output_pressure_never_claims_stream_end() {
    auto c=config();c.integration_seconds=.02;c.pulse_shaping=false;
    constexpr std::size_t delay=375;
    const auto samples=waveform({1,0,1,0,0,1,1,0},c,delay);
    for(const bool clock_window:{false,true}) {
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(delay)/c.sample_rate;
        search.frequency_offsets_hz={0};search.chunk_bits=1;search.track_limit=1;search.compact_clock_search=clock_window;
        modem::PatternReceiver receiver(c,1024*1024,search);
        rejects([&]{receiver.push(samples);},"a stalled output consumer must encounter bounded queue pressure");
        const auto pending=receiver.take_bursts();
        check(!pending.empty() && std::none_of(pending.begin(),pending.end(),[](const auto& event){return event.complete;}),
              "output quota failure must never be reported as physical stream completion");
    }
}
void one_missing_long_symbol_ends_but_eof_does_not() {
    auto c=config();c.sample_rate=256;c.bandwidth_hz=64;c.carrier_hz=64;c.integration_seconds=8;c.pulse_shaping=false;
    constexpr std::size_t delay=16;const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    auto samples=waveform({1,0},c,delay);
    std::fill(samples.begin()+static_cast<std::ptrdiff_t>(delay+symbol),samples.end(),0.F);
    modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(delay)/c.sample_rate;search.frequency_offsets_hz={0};
    modem::PatternCorrelator receiver(c,search,1024*1024);
    receiver.push(std::span(samples).first(delay+symbol+6*c.sample_rate));
    check(receiver.synchronized() && !receiver.provisional().complete,
          "six seconds within an unfinished long symbol must not substitute for its full symbol evidence");
    const auto pending=receiver.take_bursts();
    check(pending.size()==1 && pending[0].bits==Bytes({1}) && !pending[0].complete,
          "a long symbol must be visible while the next full-symbol observation remains unfinished");
    receiver.push(std::span(samples).subspan(delay+symbol+6*c.sample_rate,2*c.sample_rate));
    const auto ended=receiver.take_bursts();
    check(ended.size()==1 && ended[0].complete && ended[0].bits.empty() && !receiver.synchronized(),
          "one fully missed eight-second symbol must end the stream without waiting for a second symbol");
    for(const bool clock_window:{false,true}) {
        search.compact_clock_search=clock_window;
        modem::PatternReceiver cropped(c,1024*1024,search);
        cropped.push(std::span(samples).first(delay+symbol));cropped.finish();
        const auto partial=cropped.take_bursts();
        check(partial.size()==1 && partial[0].bits==Bytes({1}) && !partial[0].complete,
              "capture EOF must only flush observed decisions and never establish stream end in either physical path");
    }
}
void four_hour_symbols_drain_individually() {
    // Use the lowest supported sample rate so this exercises actual
    // multi-hour stream coordinates with only a bounded PCM block in RAM.
    auto c=config();c.sample_rate=64;c.bandwidth_hz=1;c.carrier_hz=16;
    c.integration_seconds=4*3600;c.pulse_shaping=false;
    const Bytes bits{0,0,1};const auto symbol=modem::symbol_sample_count(c);
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    check(source.total_samples()==3*symbol,"three slow raw bits must contain exactly three pattern symbols");
    modem::PatternSearch search;search.start_offset_seconds=0;search.frequency_offsets_hz={0};search.compact_clock_search=true;
    constexpr std::size_t workspace=1024*1024;
    modem::PatternReceiver receiver(c,workspace,search);
    std::array<float,4096> block{};
    std::uint64_t consumed=0;
    for(std::size_t bit=0;bit<bits.size();++bit) {
        const auto end=(bit+1)*symbol;
        while(consumed<end) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),end-consumed));
            check(source.read(std::span(block).first(count))==count,"slow transmitter ended before its next symbol");
            receiver.push(std::span(block).first(count));consumed+=count;
            const auto events=receiver.take_bursts();
            if(consumed<end)check(events.empty(),"an unfinished four-hour symbol must not fabricate a pending decision");
            else check(events.size()==1 && events[0].bits==Bytes({bits[bit]}) && !events[0].complete &&
                       events[0].first_stream_symbol==bit && events[0].stream_first_symbol==0 && events[0].stream_first_sample==0,
                       "each four-hour symbol must become pending at its endpoint with one stable stream identity");
            check(receiver.take_bursts().empty(),"a second consumer drain must never duplicate a slow symbol");
            check(receiver.working_bytes()<=workspace,"multi-hour per-bit presentation exceeded bounded DSP workspace");
        }
    }
    block.fill(0);
    // Six seconds inside the next four-hour symbol is insufficient evidence
    // to reject that entire symbol under the mandatory whole-symbol rule.
    receiver.push(std::span(block).first(6*c.sample_rate));
    check(receiver.take_bursts().empty() && receiver.synchronized(),
          "partial silence must not preempt full observation of the next long symbol");
    std::uint64_t absent=6*c.sample_rate;
    while(absent<symbol) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),symbol-absent));
        receiver.push(std::span(block).first(count));absent+=count;
        const auto events=receiver.take_bursts();
        if(absent<symbol)check(events.empty(),"an unfinished missing long symbol must not end the stream");
        else check(events.size()==1 && events[0].complete && events[0].bits.empty() && events[0].missing_slots==0 &&
                   events[0].first_stream_symbol==bits.size() && !receiver.synchronized(),
                   "one fully absent long symbol must complete the exact three-bit message without padding or duplication");
    }
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
    const auto result=capture(samples,received,search,113);
    check(result.size()==1 && result[0].bits==Bytes({1,modem::missing_pattern_bit,modem::missing_pattern_bit,0,1}) &&
          result[0].first_stream_symbol==0 && !result[0].complete,
          "unknown slots must preserve stream identity across a private schedule split until independent evidence selects its phase");
    for(const auto index:{0U,3U,4U}) {
        const auto expected=modem::symbol_stream_address(transmitted.stream_epoch,transmitted.stream_phase_samples,index,symbol,transmitted.sample_rate);
        const auto actual=modem::symbol_stream_address(received.stream_epoch,result[0].stream_phase_samples,index,symbol,received.sample_rate);
        check(expected.epoch==actual.epoch && expected.ordinal==actual.ordinal,"resumed independent evidence selected an inconsistent private schedule");
    }
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
    modem::PatternCorrelator seconds_receiver(c,seconds_window,16*1024*1024);
    check(seconds_receiver.working_bytes()<16*1024*1024,
          "a four-hour symbol with the full five-frequency +/-2-second clock bank fits sixteen MiB");
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
int main(int argc,char** argv) {
    unsigned failures=0;
    const auto run=[&](const char* name,auto test) {
        if(argc>1 && std::string(name).find(argv[1])==std::string::npos)return;
        try {test();std::cout<<name<<": passed\n";}
        catch(const std::exception& error){++failures;std::cerr<<name<<": "<<error.what()<<'\n';}
    };
    run("sampled_shaped",[]{sampled_bits_and_rates(true);});
    run("sampled_plain",[]{sampled_bits_and_rates(false);});
    run("late_clock_fragment",late_clock_fragment);
    run("weak_prefix_does_not_borrow_confidence",weak_prefix_does_not_borrow_confidence);
    run("stronger_significance_rejects_marginal_symbol",stronger_significance_rejects_marginal_symbol);
    run("overlapping_carrier_hypotheses_emit_one_stream",overlapping_carrier_hypotheses_emit_one_stream);
    run("shaped_raw_sample_evidence",shaped_raw_sample_evidence);
    run("shaped_partial_chips",shaped_partial_chips);
    run("majority_obscured_symbol_is_independent",majority_obscured_symbol_is_independent);
    run("completely_obscured_symbols_do_not_block_later_symbols",completely_obscured_symbols_do_not_block_later_symbols);
    run("weak_tails_expire_without_blocking_independent_symbols",weak_tails_expire_without_blocking_independent_symbols);
    run("timed_gaps_preserve_admitted_clock_and_trim_silence",timed_gaps_preserve_admitted_clock_and_trim_silence);
    run("timed_gap_expiry",timed_gap_expiry);
    run("default_gap_expiry_releases_payload_workspace",default_gap_expiry_releases_payload_workspace);
    run("drained_stream_keeps_acquisition_state",drained_stream_keeps_acquisition_state);
    run("timed_gaps_cannot_resolve_stream_phase",timed_gaps_cannot_resolve_stream_phase);
    run("drainable_chunks_and_compact_gaps",drainable_chunks_and_compact_gaps);
    run("output_pressure_never_claims_stream_end",output_pressure_never_claims_stream_end);
    run("one_missing_long_symbol_ends_but_eof_does_not",one_missing_long_symbol_ends_but_eof_does_not);
    run("four_hour_symbols_drain_individually",four_hour_symbols_drain_individually);
    run("compact_clock_search_preserves_evidence_and_bounds",compact_clock_search_preserves_evidence_and_bounds);
    run("bounded_hours_and_noise",bounded_hours_and_noise);
    run("independent_epoch",[]{independent_epoch_recovers_fractional_symbol_phase(false);independent_epoch_recovers_fractional_symbol_phase(false,true);independent_epoch_recovers_fractional_symbol_phase(true);});
    return failures?1:0;
}
