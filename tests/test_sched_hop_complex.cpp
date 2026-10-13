#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/scheduled_symbol_calendar.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <string>

using namespace datapump;
namespace {
void check(bool value,const std::string& message) {if(!value)throw Error(message);}
modem::Config configuration(unsigned chips=128,unsigned multiplier=10,unsigned factor=1,bool fhss=false) {
    modem::Config c;c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=3600./factor;
    c.spreading_factor=chips;c.scramble=true;c.dsss=factor>1;c.dsss_factor=factor;
    c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.receiver_strategy=modem::ReceiverStrategy::complex_only;
    c.data_key.emplace(Bytes(32,0x73));
    for(std::size_t i=0;i<c.spreading_seed.size();++i) {
        c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);
        c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11);
    }
    auto plan=std::make_shared<modem::SchedHopPlan>();plan->time_multiplier=multiplier;
    c.scheduled_hopping=plan;
    if(fhss) {
        auto hop=std::make_shared<modem::FhssPlan>();hop->channels=2;
        hop->shifts[0]=30000000;hop->shifts[1]=31000000;hop->dwell_seconds=.4;
        hop->power_ramps=hop->mandatory_ramps=hop->smooth_ramps=true;
        hop->clock_error_seconds=hop->peer_clock_error_seconds=.01;
        hop->audio_error_seconds=.05;hop->ramp_seconds=.006;c.fake_hopping=hop;
    }
    check(modem::packed_scheduled_symbols(c),"fixture must use packed short slots");
    const modem::ScheduledSymbolCalendar calendar(c);
    // Fixed synthetic root and timestamp; no seed search around failures.
    const auto physical=calendar.physical_slot(calendar.first_rank(8312,0));
    c.stream_epoch=physical/c.sample_rate;c.stream_phase_samples=physical%c.sample_rate;
    return c;
}
struct Capture {
    std::vector<float> pcm;
    std::uint64_t origin=0,first=0,last=0,absent_end=0;
};
Capture capture(const modem::Config& c,const Bytes& bits,bool absence=false,
                double clock_ppm=0,double delay=3.375,double sigma=.002,double frequency_offset=0) {
    modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
    const auto total=tx.total_samples();check(total<16ULL*1024*1024,"bounded fixture waveform too large");
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(total));
    for(std::size_t at=0;at<analytic.size();) {
        const auto count=tx.read_analytic(std::span(analytic).subspan(at,std::min<std::size_t>(113,analytic.size()-at)));
        check(count!=0,"transmitter ended before its promised physical endpoint");at+=count;
    }
    const modem::ScheduledSymbolCalendar calendar(c);
    const auto rank=calendar.first_rank(c.stream_epoch,c.stream_phase_samples);
    const auto symbol=modem::symbol_sample_count(c);
    const auto absent=(6ULL*c.sample_rate+symbol-1)/symbol;
    Capture result;result.origin=tx.physical_output_origin();
    result.first=calendar.physical_slot(rank)-result.origin;
    result.last=calendar.physical_slot(rank+bits.size()-1)+symbol-result.origin;
    result.absent_end=calendar.physical_slot(rank+bits.size()+absent-1)+symbol-result.origin;
    const auto source_end=absence?result.absent_end+2*symbol:total+3*symbol;
    const auto ratio=1+clock_ppm*1e-6;
    const auto count=static_cast<std::size_t>(std::ceil(source_end/ratio+delay));
    check(count<16ULL*1024*1024,"bounded fixture capture too large");result.pcm.resize(count);
    std::mt19937_64 random(529);std::normal_distribution<double> noise(0,sigma);
    for(std::size_t i=0;i<count;++i) {
        const auto coordinate=(static_cast<double>(i)-delay)*ratio;
        std::complex<double> value{};
        if(coordinate>=0 && coordinate+1<analytic.size()) {
            const auto at=static_cast<std::size_t>(coordinate);const auto fraction=coordinate-at;
            value=analytic[at]*(1-fraction)+analytic[at+1]*fraction;
            // Independent RF correction leaves the carrier nominal while the
            // complete physical calendar retains the selected clock scaling.
            value*=std::polar(1.,-2*std::numbers::pi*(c.carrier_hz*clock_ppm*1e-6-frequency_offset)*i/c.sample_rate+.37);
        }
        result.pcm[i]=static_cast<float>(value.real()+noise(random));
    }
    result.first=static_cast<std::uint64_t>(std::ceil(result.first/ratio+delay));
    result.last=static_cast<std::uint64_t>(std::ceil(result.last/ratio+delay));
    result.absent_end=static_cast<std::uint64_t>(std::ceil(result.absent_end/ratio+delay));
    return result;
}
modem::PatternSearch search(const modem::Config& c,const Capture& capture,bool qualified,bool clock=false) {
    modem::PatternSearch s;s.start_offset_seconds=(static_cast<long double>(c.stream_epoch)*c.sample_rate-capture.origin+3.375)/c.sample_rate;
    s.start_uncertainty_seconds=4;s.search_stream_phases=true;s.drift_tolerant=false;
    s.differential_window_seconds=0;s.worker_threads=1;s.initial_stream_symbols=4;
    s.candidate_limit=128;s.track_limit=8;s.bit_limit=128;
    s.hypotheses=clock?std::vector<modem::PatternFrequencyRateHypothesis>{{0,-100},{0,0},{0,100}}:
        std::vector<modem::PatternFrequencyRateHypothesis>{{0,0}};
    s.admitted_clock_half_width_ppm=clock?100:0;s.admitted_frequency_half_width_hz=0;
    s.complex_clock_coverage=s.complex_frequency_coverage=true;
    if(qualified)s.qualified_start_window=modem::PatternStartWindow{
        static_cast<long double>(*s.start_offset_seconds)*c.sample_rate,1,.002L*c.sample_rate};
    return s;
}
struct Result {Bytes bits;bool complete=false;std::uint64_t first_publish=0,completion=0;std::size_t peak=0;modem::PatternFftWork work;};
Result receive(const modem::Config& c,const Capture& input,modem::PatternSearch s,std::size_t block=113,bool stop_early=false,bool allow_erasure=false) {
    constexpr std::size_t budget=32*1024*1024;modem::PatternReceiver rx(c,budget,s);Result result;
    check(!rx.clock_windowed(),"packed coherent reception must retain FFT search, not an amplitude/direct fallback");
    const auto limit=stop_early?std::min<std::size_t>(input.pcm.size(),input.last):input.pcm.size();
    const auto drain=[&](std::uint64_t at) {
        result.peak=std::max(result.peak,rx.working_bytes());check(result.peak<=budget,"packed coherent receiver exceeded workspace");
        for(const auto& event:rx.take_bursts()) {
            check(allow_erasure || event.missing_slots==0,"scheduled quiet time manufactured missing payload slots");
            if(event.missing_slots)result.bits.insert(result.bits.end(),event.missing_slots,modem::missing_pattern_bit);
            if(!event.bits.empty()) {
                if(!result.first_publish)result.first_publish=at;
                result.bits.insert(result.bits.end(),event.bits.begin(),event.bits.end());
            }
            if(event.complete){result.complete=true;result.completion=at;}
        }
    };
    for(std::size_t at=0;at<limit;) {
        const auto count=std::min(block,limit-at);rx.push(std::span(input.pcm).subspan(at,count));at+=count;drain(at);
    }
    const auto before=result.complete;rx.finish();drain(limit);
    check(before || !result.complete,"EOF manufactured packed observed-absence completion");
    result.work=rx.fft_work();return result;
}
void defaults_and_arithmetic() {
    for(const auto chips:{128U,64U}) {
        auto c=configuration(chips);const Bytes bits{0,1,1,0,1};const auto input=capture(c,bits);
        const auto broad=receive(c,input,search(c,input,false));
        const auto tight=receive(c,input,search(c,input,true));
        check(broad.bits==bits && tight.bits==bits,"default short/long packed FFT reception lost or added bits");
        check(!broad.complete && !tight.complete,"short trailing null must not replace six seconds of scored absence");
        check(broad.first_publish>=input.first+modem::symbol_sample_count(c) &&
              broad.first_publish<input.first+3*modem::symbol_sample_count(c)+113,
              "packed accepted bit was delayed behind a null group");
        check(tight.work.scored_start_trials<broad.work.scored_start_trials,
              "qualified packed timing must reduce actual scored timing positions");
        c.search_arithmetic=modem::SearchArithmetic::fp64;
        check(receive(c,input,search(c,input,true),17).bits==bits,"FP64 original packed templates disagree with Default");
    }
}
void fhss_and_long_null_clock() {
    for(const bool fhss:{false,true}) {
        const auto c=configuration(64,10,1,fhss);const Bytes bits{1,0,1,1,0};const auto input=capture(c,bits);
        check(receive(c,input,search(c,input,true),71).bits==bits,"joint FHSS/Sched_Hop calendar lost packed bits");
    }
    {
        auto c=configuration(64);c.sample_rate=48000;c.carrier_hz=9000;c.bandwidth_hz=1200;
        c.dsss=true;c.dsss_factor=10;
        const modem::ScheduledSymbolCalendar calendar(c);
        const auto physical=calendar.physical_slot(calendar.first_rank(8312,0));
        c.stream_epoch=physical/c.sample_rate;c.stream_phase_samples=physical%c.sample_rate;
        const Bytes bits{1,0,1};const auto input=capture(c,bits);
        check(receive(c,input,search(c,input,true),211).bits==bits,
              "full Interleave DSSS packed templates lost independent bit candidates");
    }
    for(const auto sign:{-1.,1.}) {
        const auto c=configuration(64);const auto edge=.25*c.sample_rate/modem::symbol_sample_count(c);
        const Bytes bits{0,1};const auto input=capture(c,bits,false,0,3.375,.002,sign*edge);
        auto s=search(c,input,true);s.hypotheses={{-edge,0},{0,0},{edge,0}};s.admitted_frequency_half_width_hz=edge;
        check(receive(c,input,std::move(s),89).bits==bits,"packed frequency boundary hypothesis was lost");
    }
    for(const auto ppm:{-100.,50.,100.}) {
        const auto c=configuration(128,100);const Bytes bits{1,0,1,0,1};const auto input=capture(c,bits,false,ppm);
        check(receive(c,input,search(c,input,true,true),137).bits==bits,
              "packed continuation lost clock endpoint or interior rate across a long null");
    }
}
void erased_middle_slot_across_null() {
    const auto c=configuration(128,100);const modem::ScheduledSymbolCalendar calendar(c);
    const auto count=calendar.slots_per_group();
    check(count>=2 && count<16,"bounded erasure fixture must end its first group with a middle payload slot");
    Bytes bits(static_cast<std::size_t>(count+2));
    for(std::size_t i=0;i<bits.size();++i)bits[i]=static_cast<std::uint8_t>((i+1)%2);
    auto input=capture(c,bits,false,50);const auto control=receive(c,input,search(c,input,true,true),137);
    check(control.bits==bits && !control.complete,"long-null erasure control changed the original payload or absence result");
    const auto rank=calendar.first_rank(c.stream_epoch,c.stream_phase_samples);
    const auto erased=count-1,physical=calendar.physical_slot(rank+erased);
    const auto pad=modem::pattern_pulse_padding_samples(c),symbol=modem::symbol_sample_count(c);
    const auto low=static_cast<std::size_t>(std::floor((physical-pad-input.origin)/1.00005+3.375));
    const auto high=static_cast<std::size_t>(std::ceil((physical+symbol+pad-input.origin)/1.00005+3.375));
    check(low<high && high<input.pcm.size(),"erasure interval must fit the original physical capture");
    // Replace the entire shaped contribution interval by the ORIGINAL seeded
    // channel noise. Do not change the seed or rewind any transmitter address.
    // Masking pulse shoulders can also impair adjacent edge samples, as a
    // real interruption would; every surviving bit remains required below.
    std::mt19937_64 random(529);std::normal_distribution<double> noise(0,.002);
    for(std::size_t i=0;i<input.pcm.size();++i) {
        const auto value=static_cast<float>(noise(random));if(i>=low && i<high)input.pcm[i]=value;
    }
    const auto actual=receive(c,input,search(c,input,true,true),137,false,true);
    auto expected=bits;expected[static_cast<std::size_t>(erased)]=modem::missing_pattern_bit;
    check(actual.bits==expected,"failed last-in-group slot reset timing or lost recovery across the long null");
    check(!actual.complete,"erased slot/null manufactured six seconds of source-symbol absence");
}
void broad_arrival_bounds() {
    const auto c=configuration(128,100);
    const auto input=capture(c,Bytes{1});
    for(const auto side:{-1.,1.}) {
        auto s=search(c,input,false);s.start_uncertainty_seconds=.0003;
        // The ordinary prediction dates SYMBOL ZERO, independent of the
        // canonical pattern phase. Both fractional boundary cells must survive.
        s.start_offset_seconds=static_cast<double>(input.first)/c.sample_rate+
            side*s.start_uncertainty_seconds;
        const auto result=receive(c,input,s,137);
        check(result.bits==Bytes{1},"broad arrival boundary lost a fractional symbol-zero cell");
        check(result.work.restricted_timing_active,"packed broad window remained an unbounded FFT bank");
        check(result.work.timing_window==modem::PatternFftWindowStatus::not_requested,
              "ordinary broad arrival bound was advertised as qualified UTC timing");
    }
    const auto late=capture(c,Bytes{1},false,0,20.*c.sample_rate+3.375);
    auto s=search(c,late,false);s.start_uncertainty_seconds=.01;
    const auto rejected=receive(c,late,s,137);
    check(rejected.bits.empty()&&!rejected.complete,
          "packed acquisition admitted a private row outside its broad arrival window");
    check(rejected.work.skipped_start_trials>0,
          "excluded broad timing cells still required full acquisition execution");
    s=search(c,input,false);
    s.qualified_start_window=modem::PatternStartWindow{0,1,-1};
    const auto fallback=receive(c,input,s,137);
    check(fallback.bits==Bytes{1}&&fallback.work.restricted_timing_active&&
          fallback.work.timing_window==modem::PatternFftWindowStatus::invalid_numerics,
          "invalid qualified timing failed to restore bounded broad acquisition");
}
void absence_and_unrelated_evidence() {
    auto c=configuration(64);c.sample_rate=4000;c.bandwidth_hz=1000;c.carrier_hz=1000;
    const modem::ScheduledSymbolCalendar calendar(c);const auto physical=calendar.physical_slot(calendar.first_rank(8312,0));
    c.stream_epoch=physical/c.sample_rate;c.stream_phase_samples=physical%c.sample_rate;
    const auto input=capture(c,{1},true,0,3.375);
    const auto result=receive(c,input,search(c,input,true),257);
    check(result.bits==Bytes{1} && result.complete,"packed reception must complete after fully scored absent slots");
    check(result.completion>=input.absent_end,"packed receiver counted planned nulls/waits as absent symbols");
    const auto bound=modem::pattern_absence_capture_samples(c);
    check(bound>=input.absent_end-input.last,"packed capture-tail bound omitted waiting positions");
    check(!receive(c,input,search(c,input,true),97,true).complete,"partial final bit/EOF must remain pending");
    auto wrong=c;wrong.spreading_seed[0]^=0x4d;wrong.dsss_seed[0]^=0x71;
    check(receive(wrong,input,search(wrong,input,false),257).bits.empty(),"wrong private-pattern key admitted packed bits");
    auto noise=input;std::mt19937_64 random(173);std::normal_distribution<double> normal(0,.2);
    for(auto& value:noise.pcm)value=static_cast<float>(normal(random));
    const auto empty=receive(c,noise,search(c,noise,true),257);
    check(empty.bits.empty() && !empty.complete,"noise-only input manufactured a packed stream/completion");
}
}
int main() {
    try {defaults_and_arithmetic();fhss_and_long_null_clock();erased_middle_slot_across_null();broad_arrival_bounds();absence_and_unrelated_evidence();
        std::cout<<"packed scheduled complex receiver checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
