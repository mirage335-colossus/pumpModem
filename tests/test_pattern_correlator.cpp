#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <numeric>
#include <random>
#include <string>
#include <tuple>

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
                prior.support_samples=burst.support_samples;
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
void per_symbol_support_does_not_borrow_strong_prefix_evidence() {
    auto c=config();c.integration_seconds=.2;c.pulse_shaping=false;
    constexpr std::size_t start=375;
    const auto symbol=modem::symbol_sample_count(c),chip=modem::pattern_chip_samples(c);
    auto samples=waveform({1,0},c,start);
    set_symbol_evidence(samples,c,start,0,1,500,537);
    set_symbol_evidence(samples,c,start,1,0,56,541);
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.frequency_offsets_hz={0};
    double previous=-1;
    for(const std::size_t chunk:{37U,113U})for(const std::size_t decisions:{1U,1024U}) {
        search.chunk_bits=decisions;
        const auto result=capture(samples,c,search,chunk);
        const auto& observed=best(result);
        check(observed.bits==Bytes({1,0}),"per-symbol support fixture changed independently admitted bits");
        const auto expected=static_cast<double>(symbol)+56*static_cast<double>(chip);
        check(std::abs(observed.score-556)<.05 && std::abs(observed.support_samples-expected)<.5 &&
              observed.support_samples+100<static_cast<double>(2*symbol),
              "strong-symbol evidence was lent to the next weak symbol's arbitration support");
        if(previous>=0)check(std::abs(previous-observed.support_samples)<1e-6,
              "PCM or drain chunk boundaries changed cumulative per-symbol support");
        previous=observed.support_samples;
    }
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
void wide_paired_carriers_publish_one_prefix_and_terminal() {
    auto c=config();c.scramble=c.dsss=false;c.pulse_shaping=false;c.integration_seconds=1;
    constexpr std::size_t start=375;
    const Bytes bits{1,0,1,1,0};
    const auto spacing=1.5*c.sample_rate/static_cast<double>(modem::symbol_sample_count(c));
    modem::PatternSearch search;search.start_offset_seconds=.0625;search.start_uncertainty_seconds=0;
    search.hypotheses={{-spacing,0},{spacing,0},{0,0}};search.compact_clock_search=true;
    search.chunk_bits=1;search.track_limit=8;
    auto samples=waveform(bits,c,start);samples.resize(start+(bits.size()+7)*modem::symbol_sample_count(c));
    modem::PatternCorrelator receiver(c,search,4*1024*1024);
    Bytes observed;unsigned terminals=0;std::uint64_t first=0,next=0;
    for(std::size_t offset=0;offset<samples.size();) {
        const auto n=std::min<std::size_t>(113,samples.size()-offset);
        receiver.push(std::span(samples).subspan(offset,n));offset+=n;
        for(const auto& event:receiver.take_bursts()) {
            if(!observed.empty())check(event.stream_first_sample==first && event.first_stream_symbol==next,
                "wide paired carrier sidelobes republished an immutable prefix");
            else first=event.stream_first_sample;
            observed.insert(observed.end(),event.bits.begin(),event.bits.end());next+=event.bits.size();
            if(event.complete)++terminals;
        }
    }
    receiver.finish();
    for(const auto& event:receiver.take_bursts()) {observed.insert(observed.end(),event.bits.begin(),event.bits.end());if(event.complete)++terminals;}
    check(observed==bits && terminals==1 && !receiver.synchronized(),
          "wide paired frequency competition must publish one bit stream and one observed-absence terminal");
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

void long_pulse_projection_matches_raw_reference() {
    for(const auto carrier:{50.,1500.})for(const auto ppm:{-200.,200.}) {
        auto c=tuning::resolve(100,0,tuning::PatternMode::auto_pattern,true,carrier).config;
        c.integration_seconds=82;c.stream_epoch=1730000111;c.stream_phase_samples=7;
        std::array<std::uint8_t,32> key{};key.fill(0xA9);c.data_key.emplace(key);
        const auto symbol=modem::symbol_sample_count(c),chip=modem::pattern_chip_samples(c);
        check(symbol/chip==4100 && symbol%(4*chip)==0,"projection fixture needs full-chip quarter and differential boundaries");
        const auto rate=1+static_cast<long double>(ppm)*1e-6L;
        const auto origin=-.137L;
        const auto count=static_cast<std::size_t>(std::ceil(origin+symbol/rate));
        std::vector<float> samples(count);
        modem::PatternCode pattern(c,c.stream_epoch);
        std::mt19937 random(831);std::normal_distribution<float> noise(0,.3F);
        for(std::size_t n=0;n<count;++n) {
            const auto within=static_cast<double>((static_cast<long double>(n)-origin)*rate);
            const auto phase=std::polar(1.,2*std::numbers::pi*(carrier+.03125)*n/c.sample_rate+.71);
            samples[n]=static_cast<float>((phase*pattern.shaped_value(0,1,within)).real())+noise(random);
        }
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(origin/c.sample_rate);
        search.start_uncertainty_seconds=0;search.search_stream_phases=false;search.compact_clock_search=true;
        search.hypotheses={{.03125,ppm}};search.retain_score=0;search.candidate_limit=4;
        search.track_limit=1;search.bit_limit=2;search.differential_window_seconds=.32;
        modem::PatternCorrelator projected(c,search,4*1024*1024),raw(c,search,64*1024);
        check(projected.working_bytes()>raw.working_bytes()+16384,
              "bounded-workspace reference must select raw scoring rather than pulse projection");
        const auto compare=[&](const auto& a,const auto& b) {
            check(a.size()==b.size(),"pulse projection changed completed evidence count");
            for(std::size_t i=0;i<a.size();++i) {
                check(a[i].first_sample==b[i].first_sample && a[i].end_sample==b[i].end_sample &&
                      a[i].stream_symbol==b[i].stream_symbol && a[i].bit==b[i].bit &&
                      a[i].stream_phase_samples==b[i].stream_phase_samples &&
                      a[i].admission_threshold==b[i].admission_threshold,
                      "pulse projection changed sampled boundary, trial, epoch or bit identity");
                check(std::abs(a[i].score-b[i].score)<2e-7*std::max(1.,b[i].score) &&
                      std::abs(a[i].alternative_score-b[i].alternative_score)<2e-7*std::max(1.,b[i].alternative_score),
                      "pulse projection changed whole, quarter or differential raw evidence");
            }
        };
        const auto half=count/2;
        projected.push(std::span(samples).first(half));raw.push(std::span(samples).first(half));
        check(projected.candidates().empty() && raw.candidates().empty() && !projected.initial_search_complete(),
              "partial pulse cell or symbol supplied unobserved detection evidence");
        for(std::size_t offset=half;offset<count;) {
            const auto n=std::min<std::size_t>(137,count-offset);
            projected.push(std::span(samples).subspan(offset,n));raw.push(std::span(samples).subspan(offset,n));offset+=n;
        }
        compare(projected.candidates(),raw.candidates());
        check(projected.candidates().size()==1 && projected.candidates()[0].bit==1,
              "fractional paired projection did not finish the captured truncated symbol at its sampled endpoint");
        modem::PatternCorrelator whole(c,search,4*1024*1024);whole.push(samples);
        compare(whole.candidates(),projected.candidates());
        const auto events=projected.take_bursts(),references=raw.take_bursts();
        check(events.size()==1 && references.size()==1 && events[0].bits==Bytes({1}) &&
              references[0].bits==events[0].bits && events[0].end_sample==references[0].end_sample,
              "pulse projection changed independently admitted stream decisions");
    }
}
void high_chip_moments_preserve_evidence_and_progress() {
    for(const auto carrier:{.005,.5,1500.}) {
        auto c=tuning::resolve(.01,4.2185134083910505,tuning::PatternMode::auto_pattern,true,carrier).config;
        c.stream_epoch=1730000123;c.stream_phase_samples=7;
        c.spreading_seed[3]=91;c.dsss_seed[11]=217;
        const auto chip=modem::pattern_chip_samples(c);
        // Sixteen complete chips enable shaping and exercise quarter boundaries at the exact
        // reproduction sample rates without making a regression depend on a
        // planner prediction or transmitting a different reference waveform.
        c.integration_seconds=16.*chip/c.sample_rate;
        const auto symbol=modem::symbol_sample_count(c);
        check(chip>4096 && symbol==16*chip,"high-chip regression did not reach the new backend");
        const auto ppm=carrier==.5?-200.:200.;
        const auto rate=1+static_cast<long double>(ppm)*1e-6L,origin=-.137L;
        const auto count=static_cast<std::size_t>(std::ceil(origin+symbol/rate));
        std::vector<float> samples(count);
        modem::PatternCode pattern(c,c.stream_epoch);
        std::mt19937 random(938);std::normal_distribution<float> noise(0,.1F);
        double phase=.71;
        for(std::size_t n=0;n<count;++n) {
            const auto within=static_cast<double>((static_cast<long double>(n)-origin)*rate);
            phase+=static_cast<double>(noise(random))*.00001;
            const auto rotation=std::polar(1.,2*std::numbers::pi*carrier*n/c.sample_rate+phase);
            const auto limited=modem::pattern_limit_pcm(pattern.shaped_value(0,1,within));
            samples[n]=static_cast<float>((rotation*limited).real())+noise(random)+
                static_cast<float>(.02*std::sin(2*std::numbers::pi*(carrier+.003)*n/c.sample_rate));
        }
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(origin/c.sample_rate);
        search.start_uncertainty_seconds=0;search.search_stream_phases=false;search.compact_clock_search=true;
        search.hypotheses={{0,ppm}};search.retain_score=0;search.candidate_limit=8;
        search.track_limit=1;search.bit_limit=4;search.worker_threads=1;
        modem::PatternCorrelator automatic(c,search,4*1024*1024),reference(c,search,4*1024*1024,{true,false});
        check(automatic.work().backend==modem::PatternCorrelationBackend::pulse_moments &&
              reference.work().backend==modem::PatternCorrelationBackend::raw &&
              automatic.work().hypotheses==reference.work().hypotheses &&
              automatic.drift_tolerant()==reference.drift_tolerant(),"paired receivers changed search or detector coverage");
        for(std::size_t offset=0;offset+1<count;) {
            const auto n=std::min<std::size_t>(65521,count-1-offset);
            automatic.push(std::span(samples).subspan(offset,n));reference.push(std::span(samples).subspan(offset,n));offset+=n;
        }
        check(automatic.candidates().empty() && reference.candidates().empty() && automatic.take_bursts().empty(),
              "partial pulse moments manufactured a complete symbol");
        automatic.push(std::span(samples).last(1));reference.push(std::span(samples).last(1));
        const auto a=automatic.candidates(),b=reference.candidates();
        check(a.size()==1 && b.size()==1 && a[0].bit==b[0].bit && a[0].bit==1 &&
              a[0].first_sample==b[0].first_sample && a[0].end_sample==b[0].end_sample &&
              a[0].admission_threshold==b[0].admission_threshold,"pulse moments changed completed candidate identity");
        check(std::abs(a[0].score-b[0].score)<2e-7*std::max(1.,b[0].score) &&
              std::abs(a[0].alternative_score-b[0].alternative_score)<2e-7*std::max(1.,b[0].alternative_score),
              "pulse moments changed real-sample evidence or covariance");
        const auto accepted=automatic.take_bursts(),raw_accepted=reference.take_bursts();
        check(accepted.size()==1 && raw_accepted.size()==1 && accepted[0].bits==Bytes{1} &&
              !accepted[0].complete,"a newly accepted high-chip bit missed its next progress poll");
        check(automatic.work().segments<=16*260 && automatic.work().cells==16,
              "pulse contractions followed PCM samples instead of table knots");
        modem::PatternCorrelator whole(c,search,4*1024*1024);whole.push(samples);
        check(std::abs(whole.candidates()[0].score-a[0].score)<2e-7*std::max(1.,a[0].score),
              "caller block boundaries changed retained affine moments");
        std::vector<float> silence(6*c.sample_rate);automatic.push(silence);
        const auto pending=automatic.take_bursts();
        check(std::none_of(pending.begin(),pending.end(),[](const auto& event){return event.complete;}),
              "six seconds inside an unscored long symbol manufactured physical completion");
        std::stop_source cancel;cancel.request_stop();
        rejects([&]{automatic.push(silence,cancel.get_token());},"moment frontend ignored cancellation");
        automatic.finish();
        const auto stopped=automatic.take_bursts();
        check(std::none_of(stopped.begin(),stopped.end(),[](const auto& event){return event.complete;}),
              "capture EOF manufactured high-chip physical completion");
    }
}

void high_chip_section_and_differential_evidence() {
    for(const bool differential:{false,true}) {
        auto c=config();c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=128./4097;
        for(unsigned i=0;i<8 && modem::pattern_chip_samples(c)>4097;++i)
            c.bandwidth_hz=std::nextafter(c.bandwidth_hz,std::numeric_limits<double>::infinity());
        const auto chip=modem::pattern_chip_samples(c);
        const std::uint64_t chips=differential?4096:64;
        c.integration_seconds=static_cast<double>(chip*chips)/c.sample_rate;
        c.stream_epoch=1730000311;c.spreading_seed[7]=31;c.dsss_seed[4]=219;
        const auto symbol=modem::symbol_sample_count(c);
        check(chip==4097 && symbol==chip*chips,"long section fixture lost whole-chip geometry");
        const Bytes bits=differential?Bytes{1}:Bytes{1,1,0};
        constexpr long double origin=-.137L;
        const double ppm=differential?200.:-200.;const auto rate=1+static_cast<long double>(ppm)*1e-6L;
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(origin/c.sample_rate);
        search.start_uncertainty_seconds=0;search.search_stream_phases=false;search.compact_clock_search=true;
        search.hypotheses={{0,ppm}};search.retain_score=0;search.candidate_limit=16;
        search.track_limit=1;search.bit_limit=8;search.chunk_bits=1;search.worker_threads=1;
        if(differential)search.differential_window_seconds=16.*chip/c.sample_rate;
        modem::PatternCorrelator automatic(c,search,4*1024*1024),raw(c,search,4*1024*1024,{true,false});
        check(automatic.work().backend==modem::PatternCorrelationBackend::pulse_moments &&
              automatic.drift_tolerant() && raw.drift_tolerant(),"high-chip section detector was omitted");
        if(differential)check(automatic.work().differential_window_samples==16*chip &&
              raw.work().differential_window_samples==16*chip,"high-chip differential coverage was omitted");
        const auto count=static_cast<std::uint64_t>(std::ceil(origin+(bits.size()+(differential?0:1))*symbol/rate));
        modem::PatternCode pattern(c,c.stream_epoch);std::mt19937 random(731);
        std::normal_distribution<float> noise(0,.2F);std::vector<float> block(65521);
        Bytes accepted;unsigned completed=0;
        for(std::uint64_t offset=0;offset<count;) {
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),count-offset));
            for(std::size_t i=0;i<n;++i) {
                const auto position=(static_cast<long double>(offset+i)-origin)*rate;
                const auto index=static_cast<std::uint64_t>(position/symbol);
                if(index>=bits.size()){block[i]=0;continue;}
                const auto within=static_cast<double>(position-index*symbol);
                const auto phase=differential?.0007*std::floor(within/(16*chip)):
                    .25*std::floor(4*within/symbol);
                const auto rotation=std::polar(1.,2*std::numbers::pi*c.carrier_hz*(offset+i)/c.sample_rate+phase);
                block[i]=static_cast<float>((rotation*modem::pattern_limit_pcm(pattern.shaped_value(index*chips,bits[index],within))).real())+noise(random);
            }
            automatic.push(std::span(block).first(n));raw.push(std::span(block).first(n));offset+=n;
            const auto a=automatic.take_bursts(),b=raw.take_bursts();
            check(a.size()==b.size(),"high-chip detector changed next-poll event count");
            for(std::size_t i=0;i<a.size();++i) {
                check(a[i].bits==b[i].bits && a[i].complete==b[i].complete && a[i].end_sample==b[i].end_sample,
                      "high-chip detector changed private prefix or absence completion");
                accepted.insert(accepted.end(),a[i].bits.begin(),a[i].bits.end());completed+=a[i].complete;
            }
        }
        const auto a=automatic.candidates(),b=raw.candidates();
        check(a.size()==b.size() && a.size()>=bits.size(),"high-chip detector lost complete observations");
        for(std::size_t i=0;i<a.size();++i)check(a[i].bit==b[i].bit && a[i].stream_symbol==b[i].stream_symbol &&
            a[i].admission_threshold==b[i].admission_threshold && a[i].end_sample==b[i].end_sample &&
            std::abs(a[i].score-b[i].score)<2e-7*std::max(1.,b[i].score) &&
            std::abs(a[i].alternative_score-b[i].alternative_score)<2e-7*std::max(1.,b[i].alternative_score),
            "high-chip section/differential statistic changed relative to identical raw PCM");
        if(accepted!=bits || completed!=unsigned(!differential)) {
            std::cerr<<"high-chip detector diagnostic differential="<<differential<<" accepted=";
            for(auto bit:accepted)std::cerr<<unsigned(bit);
            std::cerr<<" completed="<<completed<<" evidence="<<a.size()<<'\n';
            for(const auto& item:a)std::cerr<<item.stream_symbol<<' '<<item.score<<' '<<item.alternative_score<<' '<<item.admission_threshold<<'\n';
        }
        check(accepted==bits && completed==unsigned(!differential),
              "successive private bits or fully observed high-chip absence were not preserved");
    }
}

void explicit_pairs_do_not_add_cartesian_lanes() {
    for(const bool tone:{false,true}) {
        auto c=config();c.integration_seconds=.2;c.pulse_shaping=false;
        c.spreading_mode=tone?modem::SpreadingMode::tone:modem::SpreadingMode::pattern;
        if(tone)c.scramble=c.dsss=false;
        modem::PatternSearch search;search.start_offset_seconds=0;
        search.start_uncertainty_seconds=0;search.retain_score=0;
        search.frequency_offsets_hz={12345};search.clock_errors_ppm={-1,0,1};
        search.hypotheses={{-.03125,-8000},{.03125,8000}};
        const auto symbol=modem::symbol_sample_count(c);
        const auto slow_end=static_cast<std::size_t>(std::ceil(symbol/.992L));
        std::vector<float> silence(slow_end);
        modem::PatternCorrelator receiver(c,search,4*1024*1024);
        receiver.push(std::span(silence).first(slow_end-1));
        check(!receiver.initial_search_complete(),"paired fast clock hid unfinished slower coverage");
        receiver.push(std::span(silence).last(1));
        check(receiver.initial_search_complete(),"paired slow clock did not complete full coverage");
        const auto evidence=receiver.candidates();
        check(evidence.size()==2,"authoritative pairs expanded into a Cartesian search");
        for(const auto& pair:search.hypotheses) {
            const auto end=static_cast<std::uint64_t>(std::ceil(static_cast<long double>(symbol)/
                (1+static_cast<long double>(pair.clock_error_ppm)*1e-6L)));
            check(std::any_of(evidence.begin(),evidence.end(),[&](const auto& observed) {
                return observed.first_sample==0 && observed.end_sample==end &&
                    observed.frequency_hz==c.carrier_hz+pair.frequency_offset_hz;
            }),"paired frequency and rate were detached during compact scoring");
        }
        auto invalid=search;invalid.hypotheses[0].clock_error_ppm=10001;
        rejects([&]{modem::PatternCorrelator rejected(c,invalid,4*1024*1024);},"invalid paired rate accepted");
        invalid=search;invalid.hypotheses[0].frequency_offset_hz=std::numeric_limits<double>::quiet_NaN();
        rejects([&]{modem::PatternCorrelator rejected(c,invalid,4*1024*1024);},"nonfinite paired frequency accepted");
    }
}
void compact_constructor_uses_attached_oscillator_policy() {
    auto c=tuning::resolve(100,0,tuning::PatternMode::auto_pattern,true,50).config;
    c.integration_seconds=32;
    modem::OscillatorSearchConfig policy;policy.lf={.0001,.005};policy.margin=3;
    c.oscillator_search=policy;
    const auto expected=modem::oscillator_pattern_search(c).hypotheses;
    check(expected.size()==3,"tiny attached oscillator model should retain center and both conservative endpoints");
    const auto slow=std::min_element(expected.begin(),expected.end(),[](const auto& a,const auto& b) {
        return a.clock_error_ppm<b.clock_error_ppm;
    });
    const auto count=static_cast<std::size_t>(std::ceil(modem::symbol_sample_count(c)/
        (1+static_cast<long double>(slow->clock_error_ppm)*1e-6L)));
    modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=0;
    search.retain_score=0;search.compact_clock_search=true;
    std::vector<float> silence(count);
    modem::PatternCorrelator receiver(c,search,4*1024*1024);receiver.push(silence);
    const auto evidence=receiver.candidates();check(evidence.size()==expected.size(),
        "direct compact API ignored attached oscillator frequency/rate policy");
    for(const auto& pair:expected)check(std::any_of(evidence.begin(),evidence.end(),[&](const auto& e) {
        return e.frequency_hz==c.carrier_hz+pair.frequency_offset_hz && e.end_sample==
            static_cast<std::uint64_t>(std::ceil(modem::symbol_sample_count(c)/
                (1+static_cast<long double>(pair.clock_error_ppm)*1e-6L)));
    }),"attached oscillator carrier and clock errors became independent compact dimensions");
    search.frequency_offsets_hz={0};
    modem::PatternCorrelator explicit_receiver(c,search,4*1024*1024);explicit_receiver.push(silence);
    check(explicit_receiver.candidates().size()==1,"attached policy overrode explicit compact frequency coverage");
}
void paired_pulse_origin_parities_and_canonical_quarters() {
    auto c=tuning::resolve(100,0,tuning::PatternMode::auto_pattern,true,1500).config;
    c.integration_seconds=16;c.stream_epoch=1730000221;c.stream_phase_samples=11;
    const auto chip=modem::pattern_chip_samples(c),symbol=modem::symbol_sample_count(c);
    check(chip==120 && symbol/chip==800,"canonical quarter fixture geometry changed");
    constexpr long double rate=1.0002L;
    const auto origin=static_cast<long double>(chip)/rate;
    modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(1.125L*chip/rate/c.sample_rate);
    search.start_uncertainty_seconds=*search.start_offset_seconds;search.search_stream_phases=false;
    search.hypotheses={{0,200}};search.compact_clock_search=true;search.retain_score=0;
    search.candidate_limit=64;search.track_limit=1;search.bit_limit=16;
    const auto upper=2*static_cast<long double>(*search.start_offset_seconds)*c.sample_rate;
    const auto count=static_cast<std::size_t>(std::ceil(upper+8*symbol/rate));
    std::vector<float> samples(count);modem::PatternCode pattern(c,c.stream_epoch);
    std::mt19937 random(971);std::normal_distribution<float> noise(0,.3F);
    for(std::size_t n=0;n<count;++n) {
        const auto nominal=(static_cast<long double>(n)-origin)*rate;
        samples[n]=noise(random);if(nominal<0 || nominal>=8*symbol)continue;
        const auto index=static_cast<std::uint64_t>(std::floor(nominal/symbol));
        const auto within=nominal-index*symbol;
        const auto quarter=static_cast<unsigned>(std::min(3.L,std::floor(within/(symbol/4))));
        const auto phase=std::polar(1.,2*std::numbers::pi*c.carrier_hz*n/c.sample_rate+.4+1.2*quarter);
        samples[n]+=static_cast<float>((phase*pattern.shaped_value(index*pattern.chips_per_symbol(),index%2,
            static_cast<double>(within))).real());
    }
    modem::PatternCorrelator projected(c,search,4*1024*1024),raw(c,search,64*1024);
    check(projected.working_bytes()>raw.working_bytes()+16384,"parity reference did not exercise raw fallback");
    std::vector<modem::PatternEvidence> a,b;
    const auto retain_new=[](auto& rows,const auto& recent) {
        for(const auto& row:recent)if(std::none_of(rows.begin(),rows.end(),[&](const auto& previous) {
            return previous.first_sample==row.first_sample && previous.end_sample==row.end_sample &&
                previous.stream_symbol==row.stream_symbol;
        }))rows.push_back(row);
    };
    for(std::size_t offset=0;offset<count;) {
        const auto n=std::min<std::size_t>(137,count-offset);
        projected.push(std::span(samples).subspan(offset,n));raw.push(std::span(samples).subspan(offset,n));offset+=n;
        retain_new(a,projected.candidates());retain_new(b,raw.candidates());
        projected.take_bursts();raw.take_bursts();
    }
    check(a.size()==48 && a.size()==b.size() && projected.initial_search_complete() && raw.initial_search_complete(),
          "paired half-chip parity or clipped endpoint lost complete eight-symbol coverage");
    for(std::size_t i=0;i<a.size();++i) {
        check(a[i].first_sample==b[i].first_sample && a[i].end_sample==b[i].end_sample &&
              a[i].stream_symbol==b[i].stream_symbol && a[i].bit==b[i].bit &&
              a[i].admission_threshold==b[i].admission_threshold,
              "shared parity cells changed canonical sampled boundaries or trial ordering");
        check(std::abs(a[i].score-b[i].score)<2e-6*std::max(1.,b[i].score) &&
              std::abs(a[i].alternative_score-b[i].alternative_score)<2e-6*std::max(1.,b[i].alternative_score),
              "canonical fractional quarters changed pulse raw evidence across epochs");
    }
    check(std::count_if(a.begin(),a.end(),[](const auto& e){return e.stream_symbol==6;})==6,
          "exact paired origin grid failed to retain every seventh-symbol quarter boundary");
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
void partial_projection_affine() {
    struct Geometry {std::uint64_t chip,chips,tail;double ppm,carrier=16;};
    for(const auto item:{Geometry{1280,64,1,-200},Geometry{1280,64,640,200},
            Geometry{1280,64,1279,-200},Geometry{4097,64,27,200},Geometry{1280,17,0,200},
            Geometry{1280,17,27,-10000,.05},Geometry{1280,17,459,10000,.05}}) {
        auto c=config();c.sample_rate=64;c.carrier_hz=item.carrier;c.bandwidth_hz=128./item.chip;
        for(unsigned i=0;i<8 && modem::pattern_chip_samples(c)>item.chip;++i)
            c.bandwidth_hz=std::nextafter(c.bandwidth_hz,std::numeric_limits<double>::infinity());
        const auto wanted=item.chip*item.chips+item.tail;
        c.integration_seconds=(static_cast<double>(wanted)-.25)/c.sample_rate;
        c.stream_epoch=1730000931;c.stream_phase_samples=7;
        c.spreading_seed[9]=73;c.dsss_seed[6]=213;
        const auto symbol=modem::symbol_sample_count(c),chip=modem::pattern_chip_samples(c);
        check(symbol==wanted && chip==item.chip,"partial affine regression lost its exact sampled geometry");
        const auto rate=1+static_cast<long double>(item.ppm)*1e-6L;
        constexpr std::size_t delay=137;
        const auto origin=delay+modem::pattern_pulse_padding_samples(c)/rate;
        const Bytes bits=item.chips==17?Bytes{1,0}:Bytes{0,0,1,1,0};
        // Actual transmitter PCM includes finite pulse tails, adjacent-symbol
        // overlap and radial limiting before the identical paired reception.
        auto samples=waveform(bits,c,delay,static_cast<double>(rate));
        const auto count=static_cast<std::size_t>(std::ceil(origin+(bits.size()+1)*symbol/rate));
        check(count<=samples.size(),"partial affine capture omitted the full physical absence interval");
        samples.resize(count);
        const auto payload_end=static_cast<std::size_t>(std::ceil(origin+bits.size()*symbol/rate));
        // Keep the finite transmitted tail below the absent-symbol threshold;
        // an almost noiseless tail can legitimately admit an extra raw bit.
        std::mt19937 noise_random(4189);std::normal_distribution<double> added_noise(0,.125);
        for(auto& sample:samples)sample+=static_cast<float>(added_noise(noise_random));
        for(std::size_t n=0;n<std::min(count,payload_end);++n)samples[n]+=static_cast<float>(.012*
            std::sin(2*std::numbers::pi*(c.carrier_hz+.03125)*n/c.sample_rate));
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(origin/c.sample_rate);
        search.start_uncertainty_seconds=0;search.search_stream_phases=true;search.compact_clock_search=true;
        search.hypotheses={{c.carrier_hz*static_cast<double>(rate-1),item.ppm}};
        search.retain_score=0;search.candidate_limit=32;search.track_limit=1;
        search.bit_limit=16;search.chunk_bits=1;search.worker_threads=1;
        modem::PatternCorrelator affine(c,search,4*1024*1024),raw(c,search,4*1024*1024,{true,false});
        check(affine.work().backend==modem::PatternCorrelationBackend::pulse_segments &&
              raw.work().backend==modem::PatternCorrelationBackend::raw &&
              affine.work().hypotheses==raw.work().hypotheses &&
              affine.work().phase_groups==raw.work().phase_groups && affine.drift_tolerant()==raw.drift_tolerant(),
              "partial affine optimization changed backend/reference or search and detector coverage");
        check(affine.reserved_workspace_bytes()<raw.reserved_workspace_bytes()+4096,
              "partial affine projections retained symbol-sized or per-lane kernel storage");
        const auto compare=[](const auto& a,const auto& b) {
            check(a.size()==b.size(),"partial affine projections changed completed evidence count");
            for(std::size_t i=0;i<a.size();++i) {
                check(a[i].first_sample==b[i].first_sample && a[i].end_sample==b[i].end_sample &&
                      a[i].stream_symbol==b[i].stream_symbol && a[i].bit==b[i].bit &&
                      a[i].stream_phase_samples==b[i].stream_phase_samples &&
                      a[i].admission_threshold==b[i].admission_threshold,
                      "partial affine projections changed clock, fresh pattern, endpoint or trial identity");
                check(std::abs(a[i].score-b[i].score)<2e-7*std::max(1.,b[i].score) &&
                      std::abs(a[i].alternative_score-b[i].alternative_score)<2e-7*std::max(1.,b[i].alternative_score),
                      "partial affine projections changed raw sample noise, pulse energy or quarter evidence");
            }
        };
        Bytes accepted;unsigned completed=0;std::uint64_t next_symbol=0;
        auto endpoint=static_cast<std::size_t>(std::ceil(origin+symbol/rate));
        for(std::size_t offset=0;offset<count;) {
            auto until=std::min(offset+137,count);
            if(offset<endpoint-1)until=std::min(until,endpoint-1);
            else if(offset<endpoint)until=endpoint;
            affine.push(std::span(samples).subspan(offset,until-offset));
            raw.push(std::span(samples).subspan(offset,until-offset));offset=until;
            compare(affine.candidates(),raw.candidates());
            if(offset==endpoint-1) {
                const auto rows=affine.candidates();
                check(std::none_of(rows.begin(),rows.end(),[&](const auto& row){return row.stream_symbol==next_symbol;}),
                      "an affine pulse piece published an unobserved final sample");
            }
            const auto a=affine.take_bursts(),b=raw.take_bursts();
            check(a.size()==b.size(),"partial affine projection changed next-poll progress count");
            for(std::size_t i=0;i<a.size();++i) {
                check(a[i].bits==b[i].bits && a[i].complete==b[i].complete &&
                      a[i].end_sample==b[i].end_sample && a[i].first_stream_symbol==b[i].first_stream_symbol,
                      "partial affine projection changed accepted prefix or physical absence");
                accepted.insert(accepted.end(),a[i].bits.begin(),a[i].bits.end());completed+=a[i].complete;
            }
            if(offset==endpoint){++next_symbol;endpoint=static_cast<std::size_t>(
                std::ceil(origin+(next_symbol+1)*symbol/rate));}
        }
        if(accepted!=bits || completed!=1) {
            std::cerr<<"partial geometry chip="<<chip<<" symbol="<<symbol<<" ppm="<<item.ppm
                <<" accepted=";for(const auto bit:accepted)std::cerr<<unsigned(bit);
            std::cerr<<" completions="<<completed<<'\n';
            throw Error("partial-chip private patterns or fully scored absence changed");
        }
        check(affine.work().segments>0,"partial affine receiver did not record bounded numerical spans");
        // This block-size comparison deliberately omits the intermediate polls;
        // reserve enough output slots for the same per-bit events plus absence.
        auto whole_search=search;whole_search.track_limit=bits.size()+1;
        modem::PatternCorrelator whole(c,whole_search,4*1024*1024);whole.push(samples);
        compare(whole.candidates(),affine.candidates());
        affine.finish();const auto stopped=affine.take_bursts();
        check(std::none_of(stopped.begin(),stopped.end(),[](const auto& event){return event.complete;}),
              "EOF supplied a second affine physical completion");
    }
}
void partial_projection_affine_differential() {
    auto c=config();c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=.1;
    constexpr std::uint64_t chip=1280,symbol=4096*chip+27;
    c.integration_seconds=(static_cast<double>(symbol)-.25)/c.sample_rate;
    c.stream_epoch=1730000931;c.stream_phase_samples=7;
    check(modem::pattern_chip_samples(c)==chip && modem::symbol_sample_count(c)==symbol,
          "partial differential affine fixture lost exact geometry");
    const auto padding=modem::pattern_pulse_padding_samples(c);
    modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(padding)/c.sample_rate;
    search.start_uncertainty_seconds=0;search.search_stream_phases=true;search.compact_clock_search=true;
    search.hypotheses={{0,0}};search.retain_score=0;search.candidate_limit=8;
    search.track_limit=1;search.bit_limit=4;search.chunk_bits=1;search.worker_threads=1;
    search.differential_window_seconds=16.*chip/c.sample_rate;
    modem::PatternCorrelator affine(c,search,4*1024*1024),raw(c,search,4*1024*1024,{true,false});
    check(affine.work().backend==modem::PatternCorrelationBackend::pulse_segments &&
          affine.work().differential_window_samples==16*chip &&
          raw.work().differential_window_samples==16*chip,"partial affine differential search was omitted");
    modem::PatternTransmitter transmitter(Bytes{1},c,c.stream_epoch,0,false);
    std::vector<float> block(65521);std::mt19937 random(923);std::normal_distribution<float> noise(0,.04F);
    const auto endpoint=padding+symbol;
    for(std::uint64_t offset=0;offset<endpoint;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),endpoint-offset));
        check(transmitter.read(std::span(block).first(count))==count,"partial differential PCM capture truncated");
        for(std::size_t i=0;i<count;++i)block[i]+=noise(random);
        const auto before_last=offset+count==endpoint?count-1:count;
        affine.push(std::span(block).first(before_last));raw.push(std::span(block).first(before_last));
        if(before_last!=count) {
            check(affine.candidates().empty() && affine.take_bursts().empty(),
                  "partial differential tail manufactured a complete affine symbol");
            affine.push(std::span(block).subspan(before_last,1));raw.push(std::span(block).subspan(before_last,1));
        }
        offset+=count;
    }
    const auto a=affine.candidates(),b=raw.candidates();
    check(a.size()==1 && b.size()==1 && a[0].bit==b[0].bit && a[0].bit==1 &&
          a[0].end_sample==b[0].end_sample && a[0].admission_threshold==b[0].admission_threshold &&
          std::abs(a[0].score-b[0].score)<2e-7*std::max(1.,b[0].score) &&
          std::abs(a[0].alternative_score-b[0].alternative_score)<2e-7*std::max(1.,b[0].alternative_score),
          "affine partial differential tail changed complete real-sample evidence");
    const auto progress=affine.take_bursts(),reference=raw.take_bursts();
    check(progress.size()==1 && reference.size()==1 && progress[0].bits==Bytes{1} &&
          reference[0].bits==progress[0].bits && !progress[0].complete,
          "partial differential affine bit was batched behind physical completion");
    std::vector<float> silence(6*c.sample_rate);affine.push(silence);raw.push(silence);
    check(affine.take_bursts().empty() && raw.take_bursts().empty(),
          "six seconds inside a long affine symbol manufactured absence");
    std::stop_source cancel;cancel.request_stop();
    rejects([&]{affine.push(silence,cancel.get_token());},"partial affine input ignored cancellation");
    affine.finish();check(affine.take_bursts().empty(),"cancel/EOF completed an unobserved affine absent symbol");
}
void partial_projection_affine_fallback() {
    auto c=tuning::resolve(.1,-38,tuning::PatternMode::auto_keystream,true,.05).config;
    modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=0;
    search.search_stream_phases=false;search.compact_clock_search=true;search.hypotheses={{0,.0003}};
    search.candidate_limit=4;search.track_limit=1;search.bit_limit=8;search.worker_threads=1;
    modem::PatternCorrelator affine(c,search,4*1024*1024),raw(c,search,4*1024*1024,{true,false});
    check(modem::pattern_chip_samples(c)==1280 && modem::symbol_sample_count(c)==25478859 &&
          affine.work().backend==modem::PatternCorrelationBackend::pulse_segments &&
          affine.reserved_workspace_bytes()<raw.reserved_workspace_bytes()+4096,
          "manual -38 target partial geometry lost its bounded affine backend");
    search.drift_tolerant=false;
    modem::PatternCorrelator coherent_raw(c,search,4*1024*1024,{true,false});
    modem::PatternCorrelator tight(c,search,coherent_raw.reserved_workspace_bytes()+64);
    check(tight.work().backend==modem::PatternCorrelationBackend::raw,
          "unaffordable affine moment arrays did not retain the raw fallback");
    c.bandwidth_hz=128./65;c.carrier_hz=16;c.integration_seconds=(65.*64+27-.25)/c.sample_rate;
    modem::PatternCorrelator small(c,search,4*1024*1024);
    check(small.work().backend==modem::PatternCorrelationBackend::raw,
          "singleton pulse pieces regressed the ordinary small-chip raw path");
    c.pulse_shaping=false;modem::PatternCorrelator rectangular(c,search,4*1024*1024);
    check(rectangular.work().backend==modem::PatternCorrelationBackend::raw,
          "partial affine optimization changed rectangular templates");
}
void aligned_projection_affine_budget() {
    auto c=config();c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=.1;
    constexpr std::uint64_t chip=1280,symbol=32*chip;
    c.integration_seconds=static_cast<double>(symbol)/c.sample_rate;
    c.stream_epoch=1730000931;c.stream_phase_samples=7;
    check(modem::pattern_chip_samples(c)==chip && modem::symbol_sample_count(c)==symbol,
          "aligned budget fixture lost whole pulse geometry");
    constexpr std::size_t delay=137;
    const auto padding=modem::pattern_pulse_padding_samples(c);
    const Bytes bits{0,1};auto samples=waveform(bits,c,delay);
    const auto origin=delay+padding;
    // The slowest clock must observe its entire absent symbol before ending.
    const auto count=static_cast<std::size_t>(std::ceil(origin+(bits.size()+1)*symbol/(1-200e-6)));
    samples.resize(count);
    std::mt19937 random(6155);std::normal_distribution<float> noise(0,.125F);
    for(auto& x:samples)x+=noise(random);
    modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(origin)/c.sample_rate;
    // Retain the known private stream address in this aligned fixture.
    search.start_uncertainty_seconds=0;search.search_stream_phases=false;search.compact_clock_search=true;
    search.drift_tolerant=false;search.retain_score=0;search.candidate_limit=128;
    search.track_limit=1;search.bit_limit=8;search.chunk_bits=1;search.worker_threads=1;
    const auto frequency_step=.25*c.sample_rate/symbol;
    // Use the receiver policy's central-first ordering. Accepted output is
    // immutable; deliberately prioritizing a far carrier tests a different
    // acquisition policy, not the representation under the normal policy.
    for(const auto bin:{0,-1,1,-2,2,-3,3,-4,4})for(const auto ppm:{0.,-200.,200.})
        search.hypotheses.push_back({bin*frequency_step,ppm});
    modem::PatternCorrelator raw(c,search,8*1024*1024,{true,false});
    const auto tight_budget=raw.reserved_workspace_bytes()+24*1024;
    modem::PatternCorrelator affine(c,search,tight_budget),whole(c,search,8*1024*1024);
    check(affine.work().backend==modem::PatternCorrelationBackend::pulse_segments &&
          whole.work().backend==modem::PatternCorrelationBackend::pulse &&
          affine.work().hypotheses==raw.work().hypotheses &&
          affine.work().phase_groups==raw.work().phase_groups && affine.drift_tolerant()==raw.drift_tolerant(),
          "aligned workspace fallback changed complete search or detector coverage");
    check(affine.reserved_workspace_bytes()<=raw.reserved_workspace_bytes()+12*1024,
          "aligned affine fallback retained large kernel tables");
    Bytes accepted;unsigned completed=0;
    for(std::size_t offset=0;offset<samples.size();) {
        const auto count=std::min<std::size_t>(2048,samples.size()-offset);
        const auto input=std::span(samples).subspan(offset,count);offset+=count;
        affine.push(input);raw.push(input);
        const auto a=affine.candidates(),b=raw.candidates();
        check(a.size()==b.size(),"aligned affine fallback omitted a completed hypothesis");
        for(std::size_t i=0;i<a.size();++i)check(a[i].bit==b[i].bit &&
            a[i].first_sample==b[i].first_sample && a[i].end_sample==b[i].end_sample &&
            a[i].stream_symbol==b[i].stream_symbol && a[i].stream_phase_samples==b[i].stream_phase_samples &&
            a[i].admission_threshold==b[i].admission_threshold &&
            std::abs(a[i].score-b[i].score)<2e-7*std::max(1.,b[i].score) &&
            std::abs(a[i].alternative_score-b[i].alternative_score)<2e-7*std::max(1.,b[i].alternative_score),
            "aligned affine fallback changed I/Q, pulse energy, covariance or trial identity");
        const auto x=affine.take_bursts(),y=raw.take_bursts();
        check(x.size()==y.size(),"aligned affine fallback changed next-poll progress");
        for(std::size_t i=0;i<x.size();++i) {
            check(x[i].bits==y[i].bits && x[i].complete==y[i].complete && x[i].end_sample==y[i].end_sample &&
                  x[i].first_stream_symbol==y[i].first_stream_symbol,
                  "aligned affine fallback changed fresh private bits or physical absence");
            accepted.insert(accepted.end(),x[i].bits.begin(),x[i].bits.end());completed+=x[i].complete;
        }
        check(affine.working_bytes()<=tight_budget,"aligned affine fallback exceeded its bounded workspace");
    }
    if(accepted!=bits || completed!=1) {
        std::cerr<<"aligned affine decisions: ";for(auto bit:accepted)std::cerr<<unsigned(bit);
        std::cerr<<", completions: "<<completed<<'\n';
    }
    check(accepted==bits && completed==1,"aligned affine budget fallback lost physical bit decisions");
    affine.finish();check(affine.take_bursts().empty(),"aligned affine EOF supplied physical completion");
    // The manual 30 MHz RF case keeps all 1,181 carrier hypotheses even when
    // shared-cell kernels cannot fit. This is constructor coverage, not timing.
    c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=1;
    c.integration_seconds=16384;
    search.start_offset_seconds=0;search.hypotheses.clear();search.candidate_limit=4;
    const auto rf_symbol=modem::symbol_sample_count(c);
    for(int bin=-590;bin<=590;++bin)search.hypotheses.push_back({bin*.25*c.sample_rate/rf_symbol,0});
    modem::PatternCorrelator rf_raw(c,search,16*1024*1024,{true,false});
    modem::PatternCorrelator rf_affine(c,search,rf_raw.reserved_workspace_bytes()+2*1024*1024);
    check(modem::pattern_chip_samples(c)==12000 && rf_symbol==98304000 &&
          rf_affine.work().backend==modem::PatternCorrelationBackend::pulse_segments &&
          rf_affine.work().hypotheses==1181 && rf_affine.work().hypotheses==rf_raw.work().hypotheses &&
          rf_affine.reserved_workspace_bytes()<rf_raw.reserved_workspace_bytes()+1200*1024,
          "high-frequency-count aligned search fell back to repeated raw templates or lost hypotheses");
}
void projection_cache_headroom_bound() {
    auto c=config();c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=.1;
    constexpr std::uint64_t symbol=17*1280+27;
    c.integration_seconds=(static_cast<double>(symbol)-.25)/c.sample_rate;
    const Bytes bits{0,1,1,0,0,1};auto samples=waveform(bits,c,137);
    modem::PatternSearch search;search.start_offset_seconds=(137.+modem::pattern_pulse_padding_samples(c))/c.sample_rate;
    search.start_uncertainty_seconds=0;search.search_stream_phases=true;search.compact_clock_search=true;
    search.hypotheses={{0,0}};search.drift_tolerant=false;search.worker_threads=1;
    search.retain_score=0;search.candidate_limit=8;search.track_limit=8;search.bit_limit=32;search.chunk_bits=2;
    modem::PatternCorrelator receiver(c,search,4*1024*1024);
    check(receiver.projection_cache_headroom(2048)<4096,
          "idle finite-push headroom retained the arbitrary per-epoch megabytes");
    // One large push crosses several doublings/publications; intermediate
    // pushes retain an ample buffer and exercise its later reset trajectory.
    for(const auto count:{std::size_t{137},std::size_t{2*symbol},samples.size()}) {
        if(samples.empty())break;
        const auto n=std::min(count,samples.size());
        const auto before=receiver.reserved_workspace_bytes(),old_peak=receiver.work().peak_workspace_bytes;
        const auto extra=receiver.projection_cache_headroom(n);
        receiver.set_workspace_bytes(before+extra);
        receiver.push(std::span(samples).first(n));
        check(receiver.projection_cache_headroom(std::numeric_limits<std::size_t>::max())==
              std::numeric_limits<std::size_t>::max(),"overflowing sample count did not disable optional caching");
        check(receiver.work().peak_workspace_bytes<=std::max(old_peak,before+extra),
              "finite-push bound missed a transient reserve or publication allocation");
        const auto events=receiver.take_bursts();
        auto retained=receiver.reserved_workspace_bytes()+events.capacity()*sizeof(modem::PatternBurst);
        for(const auto& event:events)retained+=event.bits.capacity();
        check(retained<=before+extra,"finite-push headroom missed drained event storage");
        samples.erase(samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(n));
    }
}
void affine_coefficient_reuse() {
    struct Geometry {std::uint64_t tail,chips;double ppm,carrier;};
    constexpr std::uint64_t chip=16384;
    for(const auto item:{Geometry{0,17,0,16},Geometry{1,17,200,16},
            Geometry{chip/2,17,-200,.05},Geometry{chip-1,64,10000,.05}}) {
        auto c=config();c.sample_rate=64;c.carrier_hz=item.carrier;c.bandwidth_hz=128./chip;
        const auto symbol=item.chips*chip+item.tail;
        c.integration_seconds=(static_cast<double>(symbol)-.25)/c.sample_rate;
        c.stream_epoch=1730000931;c.stream_phase_samples=7;
        check(modem::pattern_chip_samples(c)==chip && modem::symbol_sample_count(c)==symbol,
              "coefficient reuse fixture lost its exact finite pulse geometry");
        const auto rate=1+static_cast<long double>(item.ppm)*1e-6L;
        constexpr std::size_t delay=137;
        const auto origin=delay+modem::pattern_pulse_padding_samples(c)/rate;
        const Bytes bits{0,1,0};auto samples=waveform(bits,c,delay,static_cast<double>(rate));
        samples.resize(static_cast<std::size_t>(std::ceil(origin+(bits.size()+1)*symbol/rate)));
        // Preserve the finite transmitted tail. This long-chip fixture needs
        // enough noise that a tail-only following interval is absent under
        // the unchanged raw detector as well as both optimized paths.
        std::mt19937 random(9147);std::normal_distribution<float> noise(0,2.F);
        for(auto& x:samples)x+=noise(random);
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(origin/c.sample_rate);
        search.start_uncertainty_seconds=0;search.search_stream_phases=true;search.compact_clock_search=true;
        search.hypotheses={{c.carrier_hz*static_cast<double>(rate-1),item.ppm}};
        search.retain_score=0;search.candidate_limit=32;search.track_limit=1;
        search.bit_limit=16;search.chunk_bits=1;search.worker_threads=1;
        modem::PatternCorrelator cached(c,search,4*1024*1024,{false,false,false,true});
        modem::PatternCorrelator preceding(c,search,4*1024*1024,{false,false,true,true});
        modem::PatternCorrelator raw(c,search,4*1024*1024,{true,false});
        check(cached.work().backend==modem::PatternCorrelationBackend::pulse_segments &&
              preceding.work().backend==cached.work().backend &&
              cached.work().affine_coefficient_cache_entries==cached.work().hypotheses*cached.work().phase_groups &&
              preceding.work().affine_coefficient_cache_entries==0 &&
              raw.work().affine_coefficient_cache_entries==0 && cached.drift_tolerant()==preceding.drift_tolerant(),
              "automatic coefficient reuse changed the backend, hypothesis or detector bank");
        check(cached.reserved_workspace_bytes()==preceding.reserved_workspace_bytes()+
              80*cached.work().affine_coefficient_cache_entries,
              "private coefficient reuse exceeded its bounded lane/group allowance");
        // Production bit quotas often exceed the workspace-derived limit.
        // Optional initial coefficients must remain available in that case,
        // while future payload growth can evict them without reducing the limit.
        auto large_search=search;large_search.bit_limit=std::numeric_limits<std::size_t>::max();
        const auto derived_budget=preceding.reserved_workspace_bytes()+4096;
        modem::PatternCorrelator derived(c,large_search,derived_budget);
        check(derived.work().affine_coefficient_cache_entries==cached.work().affine_coefficient_cache_entries &&
              derived.work().hypotheses==cached.work().hypotheses && derived.drift_tolerant()==cached.drift_tolerant() &&
              derived.reserved_workspace_bytes()<=derived_budget,
              "workspace-derived bit retention incorrectly vetoed optional coefficient reuse");
        const auto denominator=2*cached.work().hypotheses+2*search.track_limit+2;
        modem::PatternCorrelator no_cache(c,search,preceding.reserved_workspace_bytes()+denominator);
        check(no_cache.work().affine_coefficient_cache_entries==0 &&
              no_cache.work().hypotheses==cached.work().hypotheses &&
              no_cache.work().backend==modem::PatternCorrelationBackend::pulse_segments,
              "unaffordable private coefficients changed the complete affine fallback");
        const auto compare=[](const auto& a,const auto& b) {
            check(a.size()==b.size(),"coefficient reuse omitted a completed hypothesis");
            for(std::size_t i=0;i<a.size();++i)check(a[i].bit==b[i].bit &&
                a[i].first_sample==b[i].first_sample && a[i].end_sample==b[i].end_sample &&
                a[i].stream_symbol==b[i].stream_symbol && a[i].stream_phase_samples==b[i].stream_phase_samples &&
                a[i].admission_threshold==b[i].admission_threshold &&
                std::abs(a[i].score-b[i].score)<2e-7*std::max(1.,b[i].score) &&
                std::abs(a[i].alternative_score-b[i].alternative_score)<2e-7*std::max(1.,b[i].alternative_score),
                "coefficient reuse changed fresh private bits, covariance, section evidence or trial identity");
        };
        std::vector<std::size_t> boundaries;
        const auto mark=[&](long double position) {
            const auto at=static_cast<std::size_t>(std::ceil(position));
            if(at)boundaries.push_back(at-1);
            boundaries.push_back(at);boundaries.push_back(at+1);
        };
        mark(origin+chip/(2*rate));
        if(item.tail)mark(origin+((modem::pattern_chips_per_symbol(c)-1-8)*chip+item.tail/2.L)/rate);
        for(std::size_t n=1;n<=bits.size()+1;++n) {
            mark(origin+n*symbol/rate);
            if(item.chips>=64)for(unsigned section=1;section<4;++section)
                mark(origin+((n-1)*symbol+section*(symbol/4))/rate);
        }
        std::sort(boundaries.begin(),boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
        Bytes accepted;unsigned completed=0;std::size_t push_index=0;
        for(std::size_t offset=0;offset<samples.size();) {
            constexpr std::array<std::size_t,4> chunks{17,137,2048,4097};
            auto until=std::min(samples.size(),offset+chunks[push_index++%chunks.size()]);
            const auto next=std::upper_bound(boundaries.begin(),boundaries.end(),offset);
            if(next!=boundaries.end())until=std::min(until,*next);
            const auto input=std::span(samples).subspan(offset,until-offset);offset=until;
            cached.push(input);preceding.push(input);raw.push(input);
            compare(cached.candidates(),preceding.candidates());compare(cached.candidates(),raw.candidates());
            const auto a=cached.take_bursts(),b=preceding.take_bursts(),r=raw.take_bursts();
            check(a.size()==b.size() && a.size()==r.size(),"coefficient reuse batched next-poll bit progress");
            for(std::size_t i=0;i<a.size();++i) {
                check(a[i].bits==b[i].bits && a[i].bits==r[i].bits &&
                      a[i].complete==b[i].complete && a[i].complete==r[i].complete &&
                      a[i].end_sample==b[i].end_sample && a[i].end_sample==r[i].end_sample,
                      "coefficient reuse changed pending bits or physical absence");
                accepted.insert(accepted.end(),a[i].bits.begin(),a[i].bits.end());completed+=a[i].complete;
            }
            check(cached.working_bytes()<=4*1024*1024,"coefficient reuse exceeded configured workspace");
        }
        if(accepted!=bits || completed!=1) {
            std::cerr<<"affine fixture tail="<<item.tail<<" chips="<<item.chips
                <<" ppm="<<item.ppm<<" carrier="<<item.carrier<<" accepted=";
            for(const auto bit:accepted)std::cerr<<unsigned(bit);
            std::cerr<<" completed="<<completed<<'\n';
            for(const auto& e:cached.candidates())std::cerr<<"  symbol="<<e.stream_symbol
                <<" score="<<e.score<<" alternative="<<e.alternative_score
                <<" threshold="<<e.admission_threshold<<'\n';
        }
        check(accepted==bits && completed==1,"cached private affine patterns changed physical bit decisions");
        const auto a=cached.work(),b=preceding.work();
        check(a.affine_reuses>0 && a.affine_interval_fast_paths>0 && a.affine_preparations>0 && b.affine_reuses==0 &&
              a.affine_preparations<b.affine_preparations &&
              a.affine_preparations+a.affine_reuses==a.segments && b.affine_preparations==b.segments,
              "coefficient reuse fixture did not exercise retained natural pulse intervals");
        cached.finish();check(cached.take_bursts().empty(),"coefficient cache manufactured a second physical end");
        // Eviction during a partially observed affine interval retains the
        // preceding bit limit and accumulated observation/evidence state.
        modem::PatternCorrelator evicted(c,search,4*1024*1024),reference(c,search,4*1024*1024,{false,false,true,false});
        const auto start=static_cast<std::size_t>(std::ceil(origin))+17;
        evicted.push(std::span(samples).first(start));reference.push(std::span(samples).first(start));
        evicted.set_workspace_bytes(reference.reserved_workspace_bytes());
        check(evicted.work().affine_coefficient_cache_entries==0 && evicted.drift_tolerant()==reference.drift_tolerant(),
              "cache eviction reset private fitting or displaced a promised detector");
        evicted.set_workspace_bytes(4*1024*1024);
        std::stop_source cancel;cancel.request_stop();
        rejects([&]{evicted.push(std::span(samples).subspan(start,1),cancel.get_token());},
                "coefficient cache ignored cancellation");
        const auto first_end=static_cast<std::size_t>(std::ceil(origin+symbol/rate));
        const auto remaining=std::span(samples).subspan(start,first_end-start);
        evicted.push(remaining);reference.push(remaining);compare(evicted.candidates(),reference.candidates());
        const auto x=evicted.take_bursts(),y=reference.take_bursts();
        check(x.size()==y.size() && x.size()==1 && x[0].bits==Bytes{0} && x[0].bits==y[0].bits &&
              !x[0].complete && !y[0].complete,"mid-span cache eviction lost immediate first-bit progress");
        evicted.finish();reference.finish();
        const auto stopped=evicted.take_bursts();
        check(std::none_of(stopped.begin(),stopped.end(),
              [](const auto& event){return event.complete;}),"cache eviction or EOF manufactured absence");
    }
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
        double support=0;
        const auto drain=[&] {
            for(const auto& event:receiver.take_bursts()) {
                const auto current=std::pair{event.stream_first_sample,event.stream_first_symbol};
                if(!identity)identity=current;
                check(current==*identity,"draining or a short gap changed the established stream identity");
                check(event.first_stream_symbol==observed.size(),"drained decisions lost their original symbol coordinates");
                check(event.bits.size()<=16 && !(event.missing_slots && !event.bits.empty()),"chunk/run storage is not bounded and disjoint");
                check(event.support_samples>=support,"drained confirmed support decreased within one physical stream");
                if(event.missing_slots || event.bits.empty())
                    check(event.support_samples==support,"unknown slots or physical absence added arbitration support");
                support=event.support_samples;
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
        check(support>0 && support<=128*static_cast<double>(symbol),
              "unknown positions were counted as known media support");
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
void parallel_search_preserves_every_poll() {
    const auto burst_fields=[](const modem::PatternBurst& b) {
        return std::tie(b.bits,b.first_sample,b.end_sample,b.first_stream_symbol,b.frequency_hz,b.score,
            b.complete,b.stream_phase_samples,b.stream_first_sample,b.stream_first_symbol,b.missing_slots,b.support_samples);
    };
    const auto evidence_fields=[](const modem::PatternEvidence& e) {
        return std::tie(e.first_sample,e.end_sample,e.stream_symbol,e.frequency_hz,e.score,e.alternative_score,
            e.bit,e.stream_phase_samples,e.admission_threshold);
    };
    for(const bool compact:{false,true})for(const bool shaped:{false,true})for(const std::size_t chunk:{37U,257U}) {
        auto c=config();c.sample_rate=256;c.bandwidth_hz=64;c.carrier_hz=64;c.integration_seconds=.3;
        c.pulse_shaping=shaped;c.stream_phase_samples=17;
        constexpr std::size_t delay=16,workspace=1024*1024;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        const auto first=delay+static_cast<std::size_t>(modem::pattern_pulse_padding_samples(c));
        auto samples=waveform({0,0,1,0,1,1},c,delay);
        std::fill(samples.begin()+static_cast<std::ptrdiff_t>(first+2*symbol),
                  samples.begin()+static_cast<std::ptrdiff_t>(first+3*symbol),0.F);
        samples.resize(samples.size()+7*c.sample_rate);
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(first)/c.sample_rate;
        search.start_uncertainty_seconds=.008;search.frequency_offsets_hz={0,-.5,.5};
        search.clock_errors_ppm={0,500};search.search_stream_phases=true;search.worker_threads=1;
        search.compact_clock_search=compact;
        search.candidate_limit=128;search.bit_limit=128;
        modem::PatternCorrelator serial(c,search,workspace);
        search.worker_threads=4;modem::PatternCorrelator parallel(c,search,workspace);
        const auto serial_bytes=serial.working_bytes();
        bool accepted=false,missing=false,completed=false;
        const auto compare=[&] {
            check(burst_fields(serial.provisional())==burst_fields(parallel.provisional()),
                  "parallel clock scoring changed provisional fields or exact scores");
            check(serial.acquiring()==parallel.acquiring() && serial.synchronized()==parallel.synchronized(),
                  "parallel clock scoring changed acquisition state at a poll");
            const auto left=serial.candidates(),right=parallel.candidates();
            check(left.size()==right.size(),"parallel clock scoring changed retained candidate count");
            for(std::size_t i=0;i<left.size();++i)
                check(evidence_fields(left[i])==evidence_fields(right[i]),
                      "parallel clock scoring changed candidate order, arithmetic or trial thresholds");
            const auto a=serial.diagnostics(),b=parallel.diagnostics();
            check(a.sample_offset==b.sample_offset && a.bit_rate==b.bit_rate && a.pattern_score==b.pattern_score,
                  "parallel clock scoring changed diagnostics at a poll");
            check(serial.take_chip_constellation()==parallel.take_chip_constellation(),
                  "parallel clock scoring changed constellation observations");
            const auto x=serial.take_bursts(),y=parallel.take_bursts();
            check(x.size()==y.size(),"parallel clock scoring changed pending event count");
            for(std::size_t i=0;i<x.size();++i) {
                check(burst_fields(x[i])==burst_fields(y[i]),"parallel clock scoring changed pending event order or fields");
                accepted|=!x[i].bits.empty();missing|=x[i].missing_slots!=0 ||
                    std::find(x[i].bits.begin(),x[i].bits.end(),modem::missing_pattern_bit)!=x[i].bits.end();
                completed|=x[i].complete;
            }
            check(parallel.working_bytes()<=workspace,"parallel clock cache exceeded the workspace");
        };
        for(std::size_t offset=0;offset<samples.size();) {
            const auto count=std::min(chunk,samples.size()-offset);
            const auto input=std::span(samples).subspan(offset,count);
            serial.push(input);parallel.push(input);offset+=count;compare();
        }
        check(accepted && missing && completed,"parallel equivalence fixture must include accepted bits, missing slots and physical completion");
        serial.finish();parallel.finish();compare();
        // Optional worker caches cannot make a previously affordable receiver
        // state reject a smaller workspace, or conceal a cancelled operation.
        search.worker_threads=4;modem::PatternCorrelator reduced(c,search,workspace);
        reduced.set_workspace_bytes(serial_bytes);
        check(reduced.working_bytes()==serial_bytes,"workspace reduction must evict optional private worker caches");
        std::stop_source stopped;stopped.request_stop();
        rejects([&]{reduced.push(samples,stopped.get_token());},"parallel clock scoring ignored cancellation");
        check(reduced.candidates().empty() && reduced.take_bursts().empty(),"cancelled clock scoring published observations");
    }
}
void parallel_long_tiles_preserve_boundaries() {
    const auto burst_fields=[](const modem::PatternBurst& b) {
        return std::tie(b.bits,b.first_sample,b.end_sample,b.first_stream_symbol,b.frequency_hz,b.score,
            b.complete,b.stream_phase_samples,b.stream_first_sample,b.stream_first_symbol,b.missing_slots,b.support_samples);
    };
    const auto evidence_fields=[](const modem::PatternEvidence& e) {
        return std::tie(e.first_sample,e.end_sample,e.stream_symbol,e.frequency_hz,e.score,e.alternative_score,
            e.bit,e.stream_phase_samples,e.admission_threshold);
    };
    // These pushes contain dozens of unchanged 32/128-sample projection
    // blocks before any decision can become available. One worker retains
    // the scalar block order and is the independent arithmetic reference.
    for(const bool compact:{false,true})for(const unsigned mode:{0U,1U,2U})for(const bool tight:{false,true}) {
        auto c=config();c.sample_rate=256;c.bandwidth_hz=64;c.carrier_hz=64;c.integration_seconds=8.125;
        c.pulse_shaping=mode==1;c.stream_phase_samples=32;
        if(mode==2){c.spreading_mode=modem::SpreadingMode::tone;c.scramble=false;c.dsss=false;}
        constexpr std::size_t delay=16,workspace=4*1024*1024;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        const auto first=delay+static_cast<std::size_t>(modem::pattern_pulse_padding_samples(c));
        auto samples=waveform({0,0,1},c,delay);
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(first)/c.sample_rate;
        search.start_uncertainty_seconds=.03;search.frequency_offsets_hz={0,-.01,.01};
        search.clock_errors_ppm={-100,0,100};search.search_stream_phases=true;
        search.compact_clock_search=compact;search.candidate_limit=128;search.bit_limit=16;
        search.worker_threads=1;modem::PatternCorrelator scalar(c,search,workspace);
        search.worker_threads=4;modem::PatternCorrelator tiled(c,search,workspace);
        // The finite-push allowance now includes mandatory admitted timing
        // guides and their transient growth, as well as pending prefixes.
        // The old 512-byte payload-only allowance predates those guides. Keep
        // all arithmetic/progress/physical-end assertions at the declared
        // conservative headroom; optional worker caches must yield to evidence.
        const auto headroom=std::max<std::size_t>(512,scalar.projection_cache_headroom(2*symbol));
        const auto base=std::max(scalar.working_bytes(),tiled.working_bytes());
        auto budget=tight?base+512:workspace;
        scalar.set_workspace_bytes(budget);tiled.set_workspace_bytes(budget);
        bool accepted=false,completed=false;
        const auto compare=[&] {
            check(burst_fields(scalar.provisional())==burst_fields(tiled.provisional()),
                  "long correlation tile changed provisional fields or exact scores");
            check(scalar.acquiring()==tiled.acquiring() && scalar.synchronized()==tiled.synchronized(),
                  "long correlation tile changed acquisition state at a poll");
            const auto a=scalar.candidates(),b=tiled.candidates();
            check(a.size()==b.size(),"long correlation tile changed retained evidence count");
            for(std::size_t i=0;i<a.size();++i)
                check(evidence_fields(a[i])==evidence_fields(b[i]),
                      "long correlation tile changed evidence order, arithmetic or trial thresholds");
            const auto x=scalar.diagnostics(),y=tiled.diagnostics();
            check(x.sample_offset==y.sample_offset && x.bit_rate==y.bit_rate && x.pattern_score==y.pattern_score,
                  "long correlation tile changed diagnostics");
            check(scalar.take_chip_constellation()==tiled.take_chip_constellation(),
                  "long correlation tile changed original-block constellation points");
            const auto left=scalar.take_bursts(),right=tiled.take_bursts();
            check(left.size()==right.size(),"long correlation tile changed pending event count");
            for(std::size_t i=0;i<left.size();++i) {
                check(burst_fields(left[i])==burst_fields(right[i]),
                      "long correlation tile changed pending bits or event order");
                accepted|=!left[i].bits.empty();completed|=left[i].complete;
            }
            check(scalar.working_bytes()<=budget && tiled.working_bytes()<=budget,
                  "long correlation tile exceeded its workspace ceiling");
        };
        std::size_t position=0;
        const auto push_until=[&](std::size_t end) {
            while(position<end) {
                const auto count=std::min<std::size_t>(4096,end-position);
                const auto input=std::span(samples).subspan(position,count);
                scalar.push(input);tiled.push(input);position+=count;compare();
            }
        };
        if(tight) {
            // Preserve the original +512 optional-worker fallback exercise
            // before any lane can complete or need admitted timing evidence.
            check(2*modem::PatternCode(c,c.stream_epoch).working_bytes()>512,
                  "tight prefix unexpectedly admits two optional private caches");
            push_until(symbol/2);
            budget=base+headroom;scalar.set_workspace_bytes(budget);tiled.set_workspace_bytes(budget);
        }
        // Poll on each side of an exact nominal symbol endpoint, including
        // the first block whose fully observed absence may finish the stream.
        for(std::size_t index=1;index<=4;++index) {
            const auto end=first+index*symbol;
            push_until(end-1);push_until(end);push_until(end+1);
        }
        push_until(samples.size());
        check(accepted && completed,"long correlation tile fixture must admit bits and physically complete");
        scalar.finish();tiled.finish();compare();
    }
}
}
namespace {
void initial_search_coverage() {
    auto c=config();c.sample_rate=8192;c.carrier_hz=2048;c.bandwidth_hz=1024;c.pulse_shaping=false;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto seconds=static_cast<double>(symbol)/c.sample_rate;
    const std::vector<float> silence(4*symbol);
    modem::PatternSearch search;search.start_offset_seconds=0;search.frequency_offsets_hz={0};
    search.drift_tolerant=false;search.worker_threads=1;
    modem::PatternCorrelator receiver(c,search,4*1024*1024);
    check(!receiver.initial_search_complete(),"unobserved correlator claimed acquisition coverage");
    receiver.push(std::span(silence).first(symbol-1));
    check(!receiver.initial_search_complete(),"partial correlator symbol claimed acquisition coverage");
    receiver.push(std::span(silence).first(1));
    check(receiver.initial_search_complete(),"full correlator symbol did not complete acquisition coverage");
    check(!receiver.acquiring() && receiver.take_bursts().empty(),"noise coverage manufactured reception");
    receiver.finish();
    check(receiver.initial_search_complete(),"EOF discarded completed correlator coverage");

    search.start_offset_seconds=-seconds/2;
    modem::PatternCorrelator negative(c,search,4*1024*1024);
    negative.push(std::span(silence).first(symbol/2));
    check(!negative.initial_search_complete(),"negative-origin partial first symbol counted as full coverage");
    negative.push(std::span(silence).first(symbol-1));
    check(!negative.initial_search_complete(),"negative-origin coverage preceded the full successor boundary");
    negative.push(std::span(silence).first(1));
    check(negative.initial_search_complete(),"negative-origin full successor did not complete coverage");

    search.start_offset_seconds=seconds/2;search.start_uncertainty_seconds=seconds/4;
    modem::PatternCorrelator future(c,search,4*1024*1024);
    future.push(std::span(silence).first(7*symbol/4-1));
    check(!future.initial_search_complete(),"correlator ignored the last original start hypothesis");
    future.push(std::span(silence).first(1));
    check(future.initial_search_complete(),"last full start hypothesis did not complete correlator coverage");

    search.start_offset_seconds=0;search.start_uncertainty_seconds=0;search.clock_errors_ppm={-10000,10000};
    modem::PatternCorrelator rates(c,search,4*1024*1024);
    const auto slow_end=static_cast<std::size_t>(std::ceil(static_cast<long double>(symbol)/.99L));
    rates.push(std::span(silence).first(slow_end-1));
    check(!rates.initial_search_complete(),"fast clock hypothesis hid unfinished slow-clock coverage");
    rates.push(std::span(silence).first(1));
    check(rates.initial_search_complete(),"slow-clock full symbol did not complete coverage");

    search.clock_errors_ppm={0};
    modem::PatternCorrelator partial(c,search,4*1024*1024);
    partial.push(std::span(silence).first(symbol/2));partial.finish();
    check(!partial.initial_search_complete(),"EOF supplied missing correlator acquisition samples");
    modem::PatternReceiver wrapped(c,4*1024*1024,[&] {auto value=search;value.compact_clock_search=true;return value;}());
    check(wrapped.clock_windowed() && !wrapped.initial_search_complete(),"compact receiver lost initial coverage state");
    wrapped.push(std::span(silence).first(symbol));
    check(wrapped.initial_search_complete(),"compact receiver did not delegate correlator acquisition coverage");
}
}
int main(int argc,char** argv) {
    unsigned failures=0;
    const auto run=[&](const char* name,auto test) {
        if(argc>1 && std::string(name).find(argv[1])==std::string::npos)return;
        try {test();std::cout<<name<<": passed\n";}
        catch(const std::exception& error){++failures;std::cerr<<name<<": "<<error.what()<<'\n';}
    };
    run("initial_search_coverage",initial_search_coverage);
    run("sampled_shaped",[]{sampled_bits_and_rates(true);});
    run("sampled_plain",[]{sampled_bits_and_rates(false);});
    run("late_clock_fragment",late_clock_fragment);
    run("weak_prefix_does_not_borrow_confidence",weak_prefix_does_not_borrow_confidence);
    run("per_symbol_support",per_symbol_support_does_not_borrow_strong_prefix_evidence);
    run("stronger_significance_rejects_marginal_symbol",stronger_significance_rejects_marginal_symbol);
    run("overlapping_carrier_hypotheses_emit_one_stream",overlapping_carrier_hypotheses_emit_one_stream);
    run("wide_paired_carriers_publish_one_prefix_and_terminal",wide_paired_carriers_publish_one_prefix_and_terminal);
    run("shaped_raw_sample_evidence",shaped_raw_sample_evidence);
    run("long_pulse_projection_matches_raw_reference",long_pulse_projection_matches_raw_reference);
    run("high_chip_moments_preserve_evidence_and_progress",high_chip_moments_preserve_evidence_and_progress);
    run("high_chip_section_and_differential_evidence",high_chip_section_and_differential_evidence);
    run("explicit_pairs_do_not_add_cartesian_lanes",explicit_pairs_do_not_add_cartesian_lanes);
    run("compact_constructor_uses_attached_oscillator_policy",compact_constructor_uses_attached_oscillator_policy);
    run("paired_pulse_origin_parities_and_canonical_quarters",paired_pulse_origin_parities_and_canonical_quarters);
    run("shaped_partial_chips",shaped_partial_chips);
    run("partial_projection_affine",partial_projection_affine);
    run("partial_projection_affine_differential",partial_projection_affine_differential);
    run("partial_projection_affine_fallback",partial_projection_affine_fallback);
    run("aligned_projection_affine_budget",aligned_projection_affine_budget);
    run("projection_cache_headroom_bound",projection_cache_headroom_bound);
    run("affine_coefficient_reuse",affine_coefficient_reuse);
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
    run("parallel_search_preserves_every_poll",parallel_search_preserves_every_poll);
    run("parallel_long_tiles_preserve_boundaries",parallel_long_tiles_preserve_boundaries);
    run("independent_epoch",[]{independent_epoch_recovers_fractional_symbol_phase(false);independent_epoch_recovers_fractional_symbol_phase(false,true);independent_epoch_recovers_fractional_symbol_phase(true);});
    return failures?1:0;
}
