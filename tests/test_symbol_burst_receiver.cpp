#include "datapump/pattern_code.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "../src/pattern_start_geometry.hpp"
#include "../src/audio_trace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <string>

using namespace datapump;
using namespace datapump::modem;
namespace {
void check(bool value,const std::string& message){if(!value)throw Error(message);}

Config configuration(bool secret,bool amplitude=false) {
    Config c;c.sample_rate=256;c.carrier_hz=64;c.bandwidth_hz=32;
    c.spreading_factor=amplitude?256:32;c.integration_seconds=amplitude?16:2;
    c.symbol_burst={2,secret && !amplitude?3U:0U};
    c.scramble=secret;c.spreading_seed[0]=31;c.spreading_seed[17]=213;
    if(secret) {
        std::array<std::uint8_t,32> key{};
        for(unsigned i=0;i<key.size();++i)key[i]=static_cast<std::uint8_t>(i*7+13);
        c.data_key.emplace(key);
    }
    // Independently selected support UTC 1800000000 is divisible by30.
    const auto padding=pattern_pulse_padding_samples(c);
    c.stream_epoch=1800000000+padding/c.sample_rate;
    c.stream_phase_samples=padding%c.sample_rate;
    return c;
}
PatternSearch search_for(const Config& c) {
    PatternSearch s;s.frequency_offsets_hz={0};s.clock_errors_ppm={0};
    s.start_offset_seconds=static_cast<double>(37+pattern_pulse_padding_samples(c))/c.sample_rate;
    s.start_uncertainty_seconds=0;s.initial_stream_symbols=1;s.search_stream_phases=true;
    s.worker_threads=1;s.chunk_bits=1;s.track_limit=4;s.candidate_limit=32;s.bit_limit=32;
    return s;
}
struct Fixture {
    std::vector<float> pcm;
    Bytes expected;
    std::size_t complete_after=0;
    std::vector<std::pair<long double,long double>> guarded_windows;
};
Fixture independent_pcm(const Config& c,bool missing_middle=false,bool partial_end=false,double rate=1,
                        std::size_t payload_symbols=3,std::span<const int> phase_corrections={}) {
    // Placement uses literal UTC/sample arithmetic and the documented crypto
    // address. Neither SymbolBurstPlan nor PatternTransmitter places samples.
    const auto symbol=static_cast<std::size_t>(symbol_sample_count(c));
    const auto padding=static_cast<std::size_t>(pattern_pulse_padding_samples(c));
    const auto absent=(6ULL*c.sample_rate+symbol-1)/symbol;
    const auto last=payload_symbols-1+absent;
    std::vector<std::uint64_t> starts(static_cast<std::size_t>(last+2));
    for(std::size_t i=0;i+1<starts.size();++i) {
        unsigned extra=0;
        if(c.data_key && c.symbol_burst.random_limit) {
            const auto payload=starts[i]+padding;
            const auto bytes=c.data_key->stream(StreamPurpose::Fhss,1800000000+payload/c.sample_rate,
                32*(payload%c.sample_rate),32,StreamDomain::SymbolBurstWaitV1);
            const auto choices=c.symbol_burst.random_limit+1,limit=256U-256U%choices;
            const auto selected=std::find_if(bytes.begin(),bytes.end(),[&](auto b){return b<limit;});
            check(selected!=bytes.end(),"independent Burst draw exhausted");extra=*selected%choices;
        }
        const auto earliest=std::max<std::uint64_t>(starts[i]+2*padding+
            (c.symbol_burst.wait_symbols+extra+1)*symbol,starts[i]+60ULL*c.sample_rate);
        const auto grid=30ULL*c.sample_rate;
        starts[i+1]=((earliest+grid-1)/grid)*grid;
    }
    const auto phase=[&](std::size_t index){return phase_corrections.empty()?0:phase_corrections[index%phase_corrections.size()];};
    const auto completed=37.L+phase(static_cast<std::size_t>(last))+
        (starts[static_cast<std::size_t>(last)]+padding+symbol)/rate;
    const auto count=partial_end?completed-symbol/(2*rate):completed+22*c.sample_rate;
    Fixture f;f.pcm.resize(static_cast<std::size_t>(count));
    f.expected.resize(payload_symbols);
    for(std::size_t i=0;i<payload_symbols;++i)f.expected[i]=i%3==2?1:0;
    if(missing_middle)f.expected[1]=missing_pattern_bit;
    f.complete_after=static_cast<std::size_t>(std::ceil(completed));
    for(std::size_t index=0;index<starts.size();++index)f.guarded_windows.emplace_back(
        37.L+phase(index)+starts[index]/rate-20.L*c.sample_rate,
        37.L+phase(index)+(starts[index]+symbol+2*padding)/rate+20.L*c.sample_rate);
    std::mt19937_64 random(61851);std::normal_distribution<double> noise(0,.002);
    for(auto& value:f.pcm)value=static_cast<float>(noise(random));
    PatternCode code(c,c.stream_epoch);
    for(std::uint64_t index=0;index<payload_symbols;++index) {
        if(missing_middle && index==1)continue;
        const auto support=starts[index];
        const auto core=support+padding;
        code.prepare_symbol(index*code.chips_per_symbol());
        for(std::size_t n=static_cast<std::size_t>(std::floor(37+phase(index)+support/rate));
            n<f.pcm.size() && n<37+phase(index)+(core+symbol+padding)/rate;++n) {
            const auto within=(static_cast<double>(n)-37-phase(index))*rate-static_cast<double>(core);
            const auto analytic=code.shaped_value(index*code.chips_per_symbol(),index%3==2?1:0,within);
            const auto carrier=std::polar(1.7,.371+2*std::numbers::pi*c.carrier_hz*rate*n/c.sample_rate);
            f.pcm[n]+=static_cast<float>((analytic*carrier).real());
        }
    }
    return f;
}
struct Result {
    Bytes bits;
    std::uint64_t skipped=0,first_skip=std::numeric_limits<std::uint64_t>::max(),last_skip=0;
    unsigned complete=0;
};
template<class Receiver>
Result exercise(Receiver& receiver,const Config& c,const Fixture& f,bool skip,bool expect_end,
                const std::string& stage="unspecified") {
    Result result;bool suspended=false;double public_support=0;
    std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;
    const auto drain=[&](std::size_t observed) {
        for(const auto& event:receiver.take_bursts()) {
            if(event.bits.empty() && !event.missing_slots && !event.complete)continue;
            check(event.first_sample<=event.end_sample,"Burst published a reversed sample range: stage="+stage);
            if(!event.bits.empty()) {
                check(event.support_samples+1e-6>=public_support,
                    "Burst reset cumulative public support after a timing-guide change: stage="+stage);
                public_support=event.support_samples;
            }
            const auto id=std::pair{event.stream_first_sample,event.stream_first_symbol};
            if(!identity)identity=id;
            check(*identity==id,"Burst changed its pending stream identity");
            check(event.first_stream_symbol==result.bits.size(),"Burst changed a logical slot position");
            result.bits.insert(result.bits.end(),event.bits.begin(),event.bits.end());
            result.bits.insert(result.bits.end(),event.missing_slots,missing_pattern_bit);
            check(result.bits.size()<=f.expected.size(),"Burst published guard/quiet as payload");
            check(std::equal(result.bits.begin(),result.bits.end(),f.expected.begin()),"Burst pending prefix differs");
            if(event.complete) {
                ++result.complete;
                check(observed>=f.complete_after,"Burst quiet contributed to physical END: stage="+stage+
                    " secret="+std::to_string(c.data_key.has_value())+" skip="+std::to_string(skip)+
                    " observed="+std::to_string(observed)+" expected_after="+std::to_string(f.complete_after)+
                    " published="+std::to_string(result.bits.size())+" event_slot="+
                    std::to_string(event.first_stream_symbol)+" event_start="+std::to_string(event.first_sample)+
                    " event_end="+std::to_string(event.end_sample));
            }
        }
        if(!suspended && !receiver.synchronized())
            check(!receiver.symbol_burst_tracking_active(),"tentative Burst evidence froze fresh acquisition");
        if(!suspended && receiver.synchronized()) {
            receiver.suspend_fresh_acquisition();receiver.suspend_fresh_acquisition();suspended=true;
        }
    };
    check(!receiver.unobserved_end_from(0) && !receiver.try_skip_unobserved(1),
        "cold acquisition discarded possible first-symbol input");
    for(std::size_t at=0;at<f.pcm.size();) {
        if(skip)if(const auto end=receiver.unobserved_end_from(at);end && *end>at) {
            const auto count=std::min<std::uint64_t>(*end-at,f.pcm.size()-at);
            for(const auto& [begin,last]:f.guarded_windows)
                check(static_cast<long double>(at+count)<=begin || static_cast<long double>(at)>=last,
                    "Burst discarded part of an independently dated20-second guard");
            check(receiver.try_skip_unobserved(count),"Burst exclusion proof did not authorize its own interval");
            result.first_skip=std::min(result.first_skip,static_cast<std::uint64_t>(at));
            result.last_skip=at+count;
            result.skipped+=count;at+=static_cast<std::size_t>(count);drain(at);continue;
        }
        const auto count=std::min<std::size_t>(127,f.pcm.size()-at);
        try {receiver.push(std::span(f.pcm).subspan(at,count));}
        catch(const std::exception& error) {
            throw Error("Burst push failed: stage="+stage+" sample="+std::to_string(at)+
                " published="+std::to_string(result.bits.size())+" cause="+error.what());
        }
        at+=count;drain(at);
        check(receiver.working_bytes()<=8*1024*1024,"Burst receiver exceeded its workspace");
    }
    check(suspended,"fixture did not acquire normally before scheduled silence");
    check(result.bits==f.expected,"Burst failed to expose every accepted prefix");
    try {receiver.finish();}
    catch(const std::exception& error) {
        throw Error("Burst finish failed: stage="+stage+" sample="+std::to_string(f.pcm.size())+
            " published="+std::to_string(result.bits.size())+" cause="+error.what());
    }
    drain(f.pcm.size());
    check(result.complete==(expect_end?1U:0U),"Burst physical completion count differs");
    if(skip)check(result.skipped>c.sample_rate,"Burst did not provide a usable capture-free interval");
    return result;
}
void sampled_receivers() {
    for(const bool secret:{false,true})for(const bool skip:{false,true}) {
        const auto c=configuration(secret);const auto f=independent_pcm(c);auto search=search_for(c);
        PatternReceiver fft(c,8*1024*1024,search);
        check(!fft.clock_windowed(),"Burst FFT fixture unexpectedly selected compact reception");
        exercise(fft,c,f,skip,true,"native");
        search.compact_clock_search=true;
        PatternReceiver compact(c,8*1024*1024,search);
        check(compact.clock_windowed(),"Burst compact fixture did not retain its selected strategy");
        exercise(compact,c,f,skip,true,"compact");
    }
    for(const bool secret:{false,true}) {
        const auto c=configuration(secret,true);const auto f=independent_pcm(c);const auto search=search_for(c);
        PatternCorrelatorOptions options;options.amplitude_only=true;
        PatternCorrelator amplitude(c,search,8*1024*1024,options);
        exercise(amplitude,c,f,true,true,"amplitude");
    }
}
void missing_and_partial_slots() {
    const auto c=configuration(true);auto search=search_for(c);search.compact_clock_search=true;
    PatternReceiver missing(c,8*1024*1024,search);
    exercise(missing,c,independent_pcm(c,true),true,true,"missing");
    PatternReceiver partial(c,8*1024*1024,search);
    exercise(partial,c,independent_pcm(c,false,true),true,false,"partial");
}
void native_original_acquisition_cutoff() {
    const auto c=configuration(true);auto search=search_for(c);
    search.initial_stream_symbols=4;
    PatternReceiver receiver(c,8*1024*1024,search);
    check(!receiver.clock_windowed(),"native four-start fixture unexpectedly selected compact reception");
    exercise(receiver,c,independent_pcm(c,false,false,1,6),true,true,"native-initial-four");
}
void native_parallel_drift_window() {
    const auto c=configuration(true,true);auto search=search_for(c);
    search.initial_stream_symbols=4;search.worker_threads=2;
    search.frequency_offsets_hz={0,.01};search.start_uncertainty_seconds=.01;
    PatternReceiver receiver(c,8*1024*1024,search);
    check(!receiver.clock_windowed(),"parallel drift fixture unexpectedly selected compact reception");
    exercise(receiver,c,independent_pcm(c,false,false,1,6),true,true,"native-parallel-drift");
}
void native_guard_dates_waveform_support() {
    auto c=configuration(false);c.spreading_factor=64;c.integration_seconds=8;
    c.pulse_shaping=false;c.symbol_burst.wait_symbols=10;
    c.stream_epoch=1800000000;c.stream_phase_samples=0;
    auto search=search_for(c);search.start_uncertainty_seconds=.5;
    PatternReceiver receiver(c,8*1024*1024,search);
    check(!receiver.clock_windowed(),"native guard fixture unexpectedly selected compact reception");
    const auto result=exercise(receiver,c,independent_pcm(c),true,true,"native-guard-support");
    // The latest eligible arrival finishes its whole symbol, then its20-second
    // capture guard. One extra second covers bin enclosure and a127-sample
    // polling chunk; an unrelated whole FFT hop must not extend this deadline.
    const auto guard_deadline=37+symbol_sample_count(c)+static_cast<std::uint64_t>(21.5*c.sample_rate);
    check(result.first_skip<=guard_deadline,"native silence guard was extended by FFT processing lookahead");
}
void variable_waits_and_clock_lanes() {
    auto c=configuration(true,true);c.symbol_burst.random_limit=3;
    auto search=search_for(c);search.initial_stream_symbols=4;search.chunk_bits=2;search.worker_threads=2;
    search.start_uncertainty_seconds=.01;search.clock_errors_ppm={0,50};
    search.frequency_offsets_hz={0,c.carrier_hz*.00005};
    PatternCorrelator complex(c,search,8*1024*1024);
    exercise(complex,c,independent_pcm(c,false,false,1.00005),true,true,"variable-complex");
    PatternCorrelatorOptions options;options.amplitude_only=true;
    PatternCorrelator amplitude(c,search,8*1024*1024,options);
    exercise(amplitude,c,independent_pcm(c),true,true,"variable-amplitude");
}
void residual_clock_repeated_slots() {
    auto c=configuration(true,true);c.symbol_burst.random_limit=3;
    auto search=search_for(c);search.initial_stream_symbols=4;search.chunk_bits=2;search.worker_threads=2;
    search.start_uncertainty_seconds=.01;search.clock_errors_ppm={0,50};
    search.frequency_offsets_hz={0,c.carrier_hz*.00005};
    //25ppm is between the complex rate lanes and inside the amplitude
    // frontend's one-lane cell. The original .2-second arrival prior covers
    // this finite forty-slot horizon; no adaptive translation is permitted.
    search.start_uncertainty_seconds=.2;
    const auto fixture=independent_pcm(c,false,false,1.000025,40);
    PatternCorrelatorOptions options;options.amplitude_only=true;
    PatternCorrelator amplitude(c,search,8*1024*1024,options);
    exercise(amplitude,c,fixture,true,true,"residual-amplitude");
    PatternCorrelator complex(c,search,8*1024*1024);
    exercise(complex,c,fixture,true,true,"residual-complex");
}
void original_clock_cell_standalone_continuation() {
    auto c=configuration(true,true);c.symbol_burst={1,0};
    auto search=search_for(c);search.initial_stream_symbols=4;
    // Six payloads and the following whole absent slot stay inside the
    // original finite arrival lattice at both original clock-cell endpoints.
    // Later independently qualified guides must not copy a weaker old prefix.
    search.start_uncertainty_seconds=.2;search.admitted_clock_half_width_ppm=500;
    PatternCorrelatorOptions options;options.amplitude_only=true;
    for(const auto rate:{1.,1.0005,.9995}) {
        const auto fixture=independent_pcm(c,false,false,rate,6);
        PatternCorrelator receiver(c,search,8*1024*1024,options);
        exercise(receiver,c,fixture,true,true,"original-clock-cell-"+std::to_string(rate));
    }
}
void explicit_gps_policy_keeps_original_clock_cone() {
    auto c=configuration(true,true);c.symbol_burst={1,0};
    auto search=search_for(c);search.initial_stream_symbols=4;
    search.start_uncertainty_seconds=.02;search.admitted_clock_half_width_ppm=0;
    search.qualified_start_window=PatternStartWindow{37,1,.02L*c.sample_rate};
    const std::array corrections{0,2,-2,1,-1,0};
    const auto fixture=independent_pcm(c,false,false,1,40,corrections);
    PatternCorrelatorOptions options;options.amplitude_only=true;
    PatternCorrelator receiver(c,search,8*1024*1024,options);
    exercise(receiver,c,fixture,true,true,"explicit-GPS-bounded-phase-corrections");
    PatternReceiver native(c,8*1024*1024,search);
    exercise(native,c,fixture,true,true,"native-explicit-GPS-bounded-phase-corrections");
    auto gpsdo=search_for(c);gpsdo.initial_stream_symbols=4;
    gpsdo.start_uncertainty_seconds=.08;gpsdo.admitted_clock_half_width_ppm=.0006;
    gpsdo.qualified_start_window=PatternStartWindow{37,1,.08L*c.sample_rate};
    const std::array delayed_corrections{13,15,11,14,12,13};
    const auto delayed=independent_pcm(c,false,false,1,40,delayed_corrections);
    PatternCorrelator disciplined(c,gpsdo,8*1024*1024,options);
    const auto amplitude_result=exercise(disciplined,c,delayed,true,true,"GPSDO-noncentral-50ms-phase");
    check(amplitude_result.last_skip>delayed.guarded_windows[35].second,
        "GPSDO phase offset unnecessarily lost later capture-free intervals");
    PatternReceiver disciplined_native(c,8*1024*1024,gpsdo);
    const auto native_result=exercise(disciplined_native,c,delayed,true,true,"native-GPSDO-noncentral-50ms-phase");
    check(native_result.last_skip>delayed.guarded_windows[35].second,
        "native GPSDO phase offset unnecessarily lost later capture-free intervals");
}
void delayed_public_rolling_acquisition() {
    auto c=configuration(false,true);c.symbol_burst={1,0};
    auto search=search_for(c);search.start_offset_seconds.reset();
    search.start_uncertainty_seconds=.02;search.admitted_clock_half_width_ppm=0;
    const auto prelude=static_cast<int>(120*c.sample_rate);
    const std::array corrections{prelude,prelude+2,prelude-2};
    const auto fixture=independent_pcm(c,false,false,1,6,corrections);
    PatternReceiver receiver(c,8*1024*1024,search);
    check(!receiver.clock_windowed(),"public rolling fixture unexpectedly selected compact reception");
    exercise(receiver,c,fixture,true,true,"delayed-public-rolling");
}
void shared_pulse_calendar_projection() {
    auto c=configuration(true,true);c.symbol_burst.random_limit=3;
    auto search=search_for(c);search.initial_stream_symbols=4;search.start_uncertainty_seconds=.2;
    search.admitted_clock_half_width_ppm=50;
    search.hypotheses={{0,0},{c.carrier_hz*.00005,50}};
    const auto fixture=independent_pcm(c,false,false,1.000025,6);
    PatternCorrelator shared(c,search,8*1024*1024);
    check(shared.work().backend==PatternCorrelationBackend::pulse && shared.work().lattices>0,
        "Burst did not retain its original shared pulse-cell strategy");
    exercise(shared,c,fixture,true,true,"shared-pulse-calendar");
    PatternCorrelatorOptions reference;reference.raw_reference=true;
    PatternCorrelator original(c,search,8*1024*1024,reference);
    exercise(original,c,fixture,true,true,"shared-pulse-original-reference");

    auto long_chip=configuration(true,true);long_chip.sample_rate=64;long_chip.carrier_hz=16;long_chip.bandwidth_hz=1./64;
    long_chip.spreading_factor=64;long_chip.integration_seconds=8192;long_chip.symbol_burst={1,0};
    long_chip.stream_phase_samples=0;
    const auto padding=pattern_pulse_padding_samples(long_chip);
    long_chip.stream_epoch=1800000000+padding/long_chip.sample_rate;
    long_chip.stream_phase_samples=padding%long_chip.sample_rate;
    auto long_search=search_for(long_chip);long_search.hypotheses={{0,0}};
    long_search.start_uncertainty_seconds=.01;long_search.admitted_clock_half_width_ppm=0;
    PatternCorrelator moments(long_chip,long_search,8*1024*1024);
    check(moments.work().backend==PatternCorrelationBackend::pulse_moments,
        "Burst did not retain long-chip shared pulse moments");
    exercise(moments,long_chip,independent_pcm(long_chip,false,false,1,1),true,true,"shared-pulse-moments");
}
}
int main(int argc,char** argv) {
    try {
        std::unique_ptr<audio::trace::Scope> trace;std::string selected;
        for(int i=1;i<argc;++i) {
            if(std::string(argv[i])=="--trace") {
                check(++i<argc,"missing receiver trace path");trace=std::make_unique<audio::trace::Scope>(argv[i]);
            } else selected=argv[i];
        }
        if(selected=="--residual-clock")residual_clock_repeated_slots();
        else if(selected=="--local-clock-anchor")original_clock_cell_standalone_continuation();
        else if(selected=="--gps-policy")explicit_gps_policy_keeps_original_clock_cone();
        else if(selected=="--public-rolling")delayed_public_rolling_acquisition();
        else if(selected=="--shared-pulse")shared_pulse_calendar_projection();
        else {sampled_receivers();missing_and_partial_slots();native_original_acquisition_cutoff();
            native_guard_dates_waveform_support();native_parallel_drift_window();
            variable_waits_and_clock_lanes();residual_clock_repeated_slots();original_clock_cell_standalone_continuation();
            explicit_gps_policy_keeps_original_clock_cone();delayed_public_rolling_acquisition();
            shared_pulse_calendar_projection();}
        std::cout<<"Symbol_Burst receiver tests passed\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"Symbol_Burst receiver failure: "<<e.what()<<'\n';return 1;}
}
