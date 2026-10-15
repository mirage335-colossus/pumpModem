#include "datapump/pattern_correlator.hpp"
#include "datapump/receiver_strategy.hpp"
#include "../src/family_simd.hpp"
#include "../src/shared_scheduled_capture.hpp"
#include "../src/scheduled_exact_source_samples.hpp"
#include "../src/stable_owner_store.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/transfer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <bit>
#include <array>
#include <limits>

using namespace datapump;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
struct ThrowingOwnerValue {
    static inline int constructions_before_failure=-1;
    ThrowingOwnerValue(){if(constructions_before_failure==0)throw Error("injected owner construction failure");
        if(constructions_before_failure>0)--constructions_before_failure;}
};
void stable_owner_storage() {
    struct Value {std::vector<int> payload;int value=0;};
    using Store=::datapump::detail::StableOwnerStore<Value>;
    Store dense;dense.reserve(8);dense.resize(2);dense[0].value=17;
    const auto* original=&dense[0];dense.push_back(Value{{1,2},19});
    check(&dense[0]==original && dense.index(dense[2])==2 &&
          dense.storage_bytes()==dense.capacity()*sizeof(Value),"dense owner layout/accounting changed");
    check(dense.data()==original && std::as_const(dense).data()==original,"dense diagnostic payload identity changed");
    Store sparse;sparse.use_indirect();sparse.resize(5);
    bool noncontiguous=false;try{(void)sparse.data();}catch(const std::logic_error&){noncontiguous=true;}
    check(noncontiguous,"sparse owner directory exposed a false contiguous payload");
    check(sparse.storage_bytes()<=5*Store::indirect_slot_bound,"sparse minimum owners exceeded their reservation");
    std::array<const Value*,5> borrowed{};
    for(std::size_t i=0;i<5;++i){borrowed[i]=&sparse[i];sparse[i].value=static_cast<int>(i+1);}
    const auto peak=sparse.storage_bytes()+sparse.growth_peak_bytes(4097);
    sparse.resize(4097);
    check(sparse.storage_bytes()<=peak && sparse.size()==4097,"sparse owner growth exceeded charged peak or imposed old slot cap");
    for(std::size_t i=0;i<5;++i)check(&sparse[i]==borrowed[i] && sparse.index(*borrowed[i])==i &&
        borrowed[i]->value==static_cast<int>(i+1),"owner address or slot changed while growing sparse directory");
    sparse[3]={};check(sparse.index(sparse[3])==3,"base reset erased sparse owner identity");
    const Store& read=sparse;std::size_t visited=0;
    for(const auto& v:read){check(read.index(v)==visited,"sparse range iterator changed logical order");++visited;}
    check(visited==4097,"sparse range omitted materialized owners");
    sparse.resize(5);check(&sparse[0]==borrowed[0] && sparse.size()==5,"rollback resize invalidated a retained owner");
    bool overflow=false;try{(void)sparse.growth_peak_bytes(std::numeric_limits<std::size_t>::max());}
    catch(const std::length_error&){overflow=true;}
    check(overflow,"sparse growth charge accepted overflow");
    ::datapump::detail::StableOwnerStore<ThrowingOwnerValue> failing;failing.use_indirect();failing.resize(3);
    const auto* retained=&failing[1];const auto failure_peak=failing.storage_bytes()+failing.growth_peak_bytes(9);
    ThrowingOwnerValue::constructions_before_failure=2;bool threw=false;
    try{failing.resize(9);}catch(const Error&){threw=true;}
    ThrowingOwnerValue::constructions_before_failure=-1;
    check(threw && failing.size()==3 && &failing[1]==retained && failing.index(*retained)==1 &&
          failing.storage_bytes()<=failure_peak,"failed owner growth invalidated published owners or exceeded its reservation");
}
modem::Config configuration(unsigned multiplier=10,unsigned factor=1,bool fhss=false) {
    modem::Config c;c.sample_rate=512;c.carrier_hz=128;c.bandwidth_hz=8;
    c.spreading_factor=64;c.scramble=true;c.dsss=factor>1;c.dsss_factor=factor;
    c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.data_key.emplace(Bytes(32,0x73));
    for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11);}
    auto plan=std::make_shared<modem::SchedHopPlan>();plan->time_multiplier=multiplier;
    plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
    plan->ramp_seconds=.01;c.scheduled_hopping=plan;
    if(fhss) {
        auto hop=std::make_shared<modem::FhssPlan>();hop->channels=2;hop->shifts[0]=30000000;hop->shifts[1]=31000000;
        hop->dwell_seconds=.4;hop->power_ramps=hop->mandatory_ramps=hop->smooth_ramps=true;
        hop->clock_error_seconds=hop->peer_clock_error_seconds=hop->audio_error_seconds=0;hop->ramp_seconds=.006;c.fake_hopping=hop;
    }
    const modem::SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
    const auto start=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
    c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
    check(!modem::packed_scheduled_symbols(c),"fixture must split a source symbol across active groups");return c;
}
struct Capture {std::vector<float> pcm;double start=0;std::uint64_t last=0,absent_end=0;};
Capture capture(const modem::Config& c,const Bytes& bits,bool absence,double clock=0,double frequency=0,double diffusion=0,double quarter_phase=0,
                std::uint64_t local_phase_window=0,double local_phase_step=0,double arrival_delay=3.375) {
    modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
    check(tx.total_samples()<2*1024*1024,"bounded split capture exceeded fixture limit");
    std::vector<std::complex<double>> analytic(tx.total_samples());
    for(std::size_t at=0;at<analytic.size();) {
        const auto n=tx.read_analytic(std::span(analytic).subspan(at,std::min<std::size_t>(113,analytic.size()-at)));
        check(n!=0,"transmitter stopped before physical endpoint");at+=n;
    }
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const modem::SchedHopCoordinate origin{c.stream_epoch*c.sample_rate+c.stream_phase_samples,0};
    const auto source=map.locate(origin).source;const auto symbol=modem::symbol_sample_count(c);
    const auto failed=(6ULL*c.sample_rate+symbol-1)/symbol;
    const auto ratio=1+clock*1e-6;const double delay=arrival_delay;
    Capture out;out.start=static_cast<double>(origin.sample-tx.physical_output_origin())/ratio+delay;
    out.last=static_cast<std::uint64_t>(std::ceil((map.source_to_physical(source.add_samples(bits.size()*symbol),true).sample-tx.physical_output_origin())/ratio+delay));
    out.absent_end=static_cast<std::uint64_t>(std::ceil((map.source_to_physical(source.add_samples((bits.size()+failed)*symbol),true).sample-tx.physical_output_origin())/ratio+delay));
    // A clock-cell END at an active-group boundary may need the following
    // active group's first observations. Preserve the full two-bit assertion,
    // but provide that independently bounded calendar lookahead; nominal END+2
    // alone does not cover every admitted clock cell. It is far less than a
    // complete absent source symbol, and cannot manufacture physical completion.
    const std::uint64_t frame=c.scheduled_hopping->time_multiplier,active=c.scheduled_hopping->group_hops;
    const auto lookahead=static_cast<std::uint64_t>(std::ceil(.4L*(frame+(frame-active)/2+1)*c.sample_rate));
    const auto end=(absence?out.absent_end:out.last)+2+lookahead;out.pcm.resize(end);
    std::mt19937_64 random(529),phase_random(8831);std::normal_distribution<double> noise(0,.002),phase_step(0,diffusion>0?diffusion*std::numbers::pi/180/std::sqrt(c.sample_rate):1.);double phase=0;
    for(std::size_t i=0;i<out.pcm.size();++i) {
        const auto coordinate=(static_cast<double>(i)-delay)*ratio;std::complex<double> value{};
        if(coordinate>=0 && coordinate+1<analytic.size()) {
            const auto j=static_cast<std::size_t>(coordinate);const auto fraction=coordinate-j;
            value=(1-fraction)*analytic[j]+fraction*analytic[j+1];
            value*=std::polar(1.,2*std::numbers::pi*(frequency-c.carrier_hz*clock*1e-6)*i/c.sample_rate+.37);
            if(quarter_phase!=0 || local_phase_window) {
                const auto physical=modem::SchedHopCoordinate{tx.physical_output_origin(),0}.add_signed(coordinate);
                const auto source_position=map.locate(physical).source.distance(source);
                if(source_position>=0) {
                    const auto within=std::fmod(source_position,static_cast<long double>(symbol));
                    const auto quarter=std::min(3.L,std::floor(4*within/symbol));
                    auto phase=quarter_phase*static_cast<double>(quarter);
                    if(local_phase_window)phase+=local_phase_step*static_cast<double>(std::floor(within/local_phase_window));
                    value*=std::polar(1.,phase);
                }
            }
        }
        if(diffusion>0){phase+=phase_step(phase_random);value*=std::polar(1.,phase);}
        out.pcm[i]=static_cast<float>(value.real()+noise(random));
    }
    return out;
}
modem::PatternSearch search(const modem::Config& c,const Capture& input,bool bank=false) {
    modem::PatternSearch s;s.start_offset_seconds=input.start/c.sample_rate;s.start_uncertainty_seconds=0;
    s.drift_tolerant=false;s.worker_threads=1;s.frequency_offsets_hz={0};s.clock_errors_ppm={0};
    s.bit_limit=64;s.track_limit=8;s.candidate_limit=64;
    if(bank) {
        const auto span=modem::scheduled_physical_span_samples(c,modem::symbol_sample_count(c));
        const auto step=.2*c.sample_rate/static_cast<double>(span);
        s.frequency_offsets_hz={-step,0,step};s.clock_errors_ppm={-10,0,10};
        s.admitted_frequency_half_width_hz=step;s.admitted_clock_half_width_ppm=10;
        s.complex_frequency_coverage=s.complex_clock_coverage=true;
    }
    return s;
}
struct PollEvent {std::size_t poll=0;modem::PatternBurst burst;};
struct Result {std::vector<PollEvent> events;Bytes bits;bool complete=false;std::uint64_t first=0,completed=0;std::size_t peak=0;modem::PatternCorrelatorWork work;std::vector<modem::PatternEvidence> evidence;modem::detail::scheduled_source_samples::Counts source_counts;std::size_t source_bytes=0;};
Result receive(const modem::Config& c,const Capture& input,modem::PatternSearch s,std::size_t block=113,bool cache_reference=false,bool spans=false,bool trim=false,bool moment_reference=false,bool float_curve=false,bool family=false,bool all_evidence=false,
               std::span<const std::size_t> cuts={},bool raw_score=false,bool common=false,bool simd=false,bool affine_reuse=true,bool shared=false,bool exact_source=false) {
    if(all_evidence){s.retain_score=0;s.candidate_limit=4096;}
    constexpr std::size_t budget=8*1024*1024;
    namespace xs=modem::detail::scheduled_source_samples;
    std::unique_ptr<xs::Pool> source_pool;
    if(exact_source)source_pool=std::make_unique<xs::Pool>(512U*1024U);
    const auto source_bytes=source_pool?source_pool->working_bytes():0;
    check(source_bytes<budget,"source pool displaced all original receiver workspace");
    const auto receiver_budget=budget-source_bytes;
    namespace owner=modem::detail::scheduled_capture;
    std::shared_ptr<owner::SharedScheduledCapture> shared_capture;
    if(shared) {
        owner::Limits limits{budget,(input.pcm.size()+8191)/8192+8,4,8192};
        shared_capture=std::make_shared<owner::SharedScheduledCapture>(limits,owner::Identity{1,1},s.input_sample_origin);
        s.scheduled_capture=shared_capture;
    }
    modem::PatternCorrelator rx(c,s,receiver_budget,{.measure_work=true,.scheduled_template_cache_reference=cache_reference,.scheduled_raw_score_reference=raw_score,.scheduled_affine_experiment=spans,.scheduled_moment_reference=moment_reference,.scheduled_float_curve_experiment=float_curve,.scheduled_family_experiment=family,.scheduled_common_shape_experiment=common || family,.scheduled_common_affine_reuse=affine_reuse,.scheduled_common_simd_experiment=simd});Result out;
    xs::Scope source_scope(source_pool.get(),source_pool?1:0);
    const auto geometry=modem::pattern_scheduled_complex_geometry(c,s);
    check(geometry.retained_hypotheses==rx.work().hypotheses,"cheap scheduled geometry differs from actual retained lane count");
    check(!rx.try_skip_unobserved(1),"mapped complex must retain raw capture until an omission certificate exists");
    for(std::size_t at=0;at<input.pcm.size();) {
        auto n=std::min(block,input.pcm.size()-at);
        const auto cut=std::upper_bound(cuts.begin(),cuts.end(),at);
        if(cut!=cuts.end() && *cut<at+n)n=*cut-at;
        if(shared_capture)for(std::size_t copied=0;copied<n;) {
            const auto count=std::min<std::size_t>(8192,n-copied);
            shared_capture->append_copy(s.input_sample_origin+at+copied,std::span(input.pcm).subspan(at+copied,count),{1,1});copied+=count;
        }
        rx.push(std::span(input.pcm).subspan(at,n));at+=n;rx.process_pending();
        if(trim)rx.trim_optional_workspace();
        out.peak=std::max(out.peak,rx.working_bytes()+source_bytes);check(out.peak<=budget,"mapped complex workspace overflow");
        for(const auto& burst:rx.take_bursts()) {
            if(all_evidence)out.events.push_back({at,burst});
            check(!burst.missing_slots,"calendar null manufactured missing symbols");
            if(!burst.bits.empty()){if(!out.first)out.first=at;out.bits.insert(out.bits.end(),burst.bits.begin(),burst.bits.end());}
            if(burst.complete){out.complete=true;out.completed=at;}
        }
    }
    const auto complete=out.complete;rx.finish();for(const auto& burst:rx.take_bursts())check(complete || !burst.complete,"EOF manufactured source absence");
    out.work=rx.work();
    if(rx.deferred_pattern_history()) {
        const auto generation=rx.diagnostic_history_generation();
        const auto unresolved=rx.oldest_unresolved_diagnostic_event();
        const auto before=rx.provisional();
        std::stop_source cancelled_query;cancelled_query.request_stop();bool rejected=false;
        try{(void)rx.candidates(128,cancelled_query.get_token());}catch(const Error&){rejected=true;}
        check(rejected && generation==rx.diagnostic_history_generation() &&
            unresolved==rx.oldest_unresolved_diagnostic_event(),"cancelled diagnostic query changed journal identity");
        const auto last=rx.candidates(1);
        out.evidence=rx.candidates();
        check(last.size()==std::min<std::size_t>(1,out.evidence.size()) &&
            (last.empty() || last.back().completed_event==out.evidence.back().completed_event),
            "small diagnostic request changed the original full-capacity FIFO");
        const auto after=rx.provisional();
        check(generation==rx.diagnostic_history_generation() && before.bits==after.bits &&
            before.end_sample==after.end_sample && before.complete==after.complete,
            "pure diagnostic resolution changed receiver progress or physical completion");
        check(rx.diagnostic_history_resolved_through()==generation,
            "complete diagnostic query left unresolved historical events");
    } else out.evidence=rx.candidates();
    if(all_evidence)check(out.evidence.size()<s.candidate_limit,"complete-evidence fixture truncated its lane history");
    if(source_pool){out.source_counts=source_pool->counts();out.source_bytes=source_bytes;
        check(source_pool->try_reclaim(),"joined acquisition left a source Reader or tile builder alive");}
    return out;
}

void compare(const Result& original,const Result& actual) {
    check(original.bits==actual.bits,"implicit acquisition changed original payload bits");
    check(original.first==actual.first && original.completed==actual.completed && original.complete==actual.complete,
        "implicit acquisition changed publication or physical completion");
    check(original.evidence.size()==actual.evidence.size(),"implicit acquisition changed FIFO evidence count");
    for(std::size_t i=0;i<original.evidence.size();++i){const auto& a=original.evidence[i];const auto& b=actual.evidence[i];
        check(a.bit==b.bit && a.first_sample==b.first_sample && a.end_sample==b.end_sample &&
            a.stream_symbol==b.stream_symbol && a.stream_phase_samples==b.stream_phase_samples,
            "implicit acquisition changed original candidate identity/order");
        check(std::abs(a.score-b.score)<1e-8*std::max(1.,std::abs(a.score)) &&
            a.admission_threshold==b.admission_threshold,"implicit acquisition changed original guards/trials");
    }
}
void paired(unsigned factor,bool fhss,bool broad,bool clocks) {
    auto c=configuration(10,factor,fhss);const Bytes bits{0,1};const auto pcm=capture(c,bits,true);
    auto s=search(c,pcm);s.chunk_bits=1;s.retain_score=0;s.candidate_limit=4096;
    if(broad)s.start_uncertainty_seconds=static_cast<double>(modem::pattern_chip_samples(c))/2/c.sample_rate;
    if(clocks)s.clock_errors_ppm={-10,0,10};
    setenv("DATAPUMP_SCHED_LAZY_BATCH","0",1);
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","0",1);const auto original=receive(c,pcm,s,113,false,false,false,false,false,false,true);
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);const auto actual=receive(c,pcm,s,113,false,false,false,false,false,false,true);
    unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
    check(actual.work.backend==modem::PatternCorrelationBackend::scheduled_acquisition,"implicit backend not executed");
    if(original.bits!=bits || !original.complete || actual.bits!=bits || !actual.complete) {
        std::cerr<<"LAZY_COMPLETE factor="<<factor<<" fhss="<<fhss<<" broad="<<broad<<" clocks="<<clocks
            <<" original_bits="<<original.bits.size()<<" original_complete="<<original.complete
            <<" actual_bits="<<actual.bits.size()<<" actual_complete="<<actual.complete
            <<" capture="<<pcm.pcm.size()<<" nominal_absent_end="<<pcm.absent_end<<'\n';
    }
    check(original.bits==bits && original.complete,"original full-reception fixture did not cover physical END");
    check(actual.bits==bits && actual.complete,"implicit production full reception failed");compare(original,actual);
    auto fp64=c;fp64.search_arithmetic=modem::SearchArithmetic::fp64;
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);const auto reference=receive(fp64,pcm,s,113,false,false,false,false,false,false,true);
    unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");compare(original,reference);
    std::cout<<"LAZY_PAIR factor="<<factor<<" fhss="<<fhss<<" broad="<<broad<<" clocks="<<clocks
        <<" hypotheses="<<actual.work.hypotheses<<" peak="<<actual.peak<<"\n";
}

void smooth_fhss_native_parity() {
    // Same original PCM and complete dense scorer, including full guards and
    // observed END. Scheduling changes no source waveform in this test.
    for(const auto multiplier:{3U,5U}) {
        auto c=configuration(multiplier,10,true);const Bytes bits{0,1};
        const auto pcm=capture(c,bits,true);
        auto s=search(c,pcm);s.start_uncertainty_seconds=4.*modem::pattern_chip_samples(c)/c.sample_rate;
        s.clock_errors_ppm={-0.0003,0,0.0003};s.retain_score=5;s.candidate_limit=128;s.chunk_bits=1;
        for(const auto noise_only:{false,true}) {
            auto input=pcm;
            if(noise_only){std::mt19937_64 rng(7719);std::normal_distribution<float> noise(0,.2F);for(auto& x:input.pcm)x=noise(rng);}
            setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","0",1);setenv("DATAPUMP_SCHED_LAZY_BATCH","0",1);
            const auto original=receive(c,input,s,257);
            setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);
            const auto native=receive(c,input,s,257);
            unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");compare(original,native);
            check(native.work.implicit_screened_lanes>0 && native.work.implicit_fp32_ranges>0,
                "smooth FHSS never executed its native physical-calendar template screen");
            if(noise_only)check(native.bits.empty()&&!native.complete,"smooth FHSS screen manufactured noise bits or END");
            else check(native.bits==bits && native.complete && native.completed>=pcm.absent_end,
                "smooth FHSS native acquisition lost payload or physical END");
            std::cout<<"FHSS_NATIVE multiplier="<<multiplier<<" noise="<<noise_only
                <<" screened="<<native.work.implicit_screened_lanes<<" rejected="<<native.work.implicit_rejected_lanes
                <<" original_refined="<<native.work.implicit_refined_lanes<<" fp32="<<native.work.implicit_fp32_ranges<<'\n';
        }
    }
}
void original_push_fences() {
    auto c=configuration(10,10,true);const Bytes bits{0,1};const auto pcm=capture(c,bits,true);
    auto s=search(c,pcm);s.start_uncertainty_seconds=static_cast<double>(modem::pattern_chip_samples(c))/2/c.sample_rate;
    s.chunk_bits=1;s.retain_score=0;s.candidate_limit=4096;s.track_limit=2;
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","0",1);const auto original=receive(c,pcm,s,8192,false,false,false,false,false,false,true);
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);unsetenv("DATAPUMP_SCHED_LAZY_BATCH");
    const auto actual=receive(c,pcm,s,8192,false,false,false,false,false,false,true);
    unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");compare(original,actual);
    check(actual.bits==bits && actual.complete,"original push/fence split lost intended payload or physical END");
}
void screened_noise_and_payload() {
    auto c=configuration(10,10,false);const Bytes bits{1,0};const auto pcm=capture(c,bits,true);
    auto s=search(c,pcm);s.start_uncertainty_seconds=4.*modem::pattern_chip_samples(c)/c.sample_rate;
    s.clock_errors_ppm={-0.0003,0,0.0003};s.retain_score=5;s.candidate_limit=128;s.chunk_bits=1;
    for(bool noise_only:{false,true}) {
        auto input=pcm;if(noise_only){std::mt19937_64 rng(7719);std::normal_distribution<float> noise(0,.2F);for(auto& x:input.pcm)x=noise(rng);}
        setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","0",1);setenv("DATAPUMP_SCHED_LAZY_BATCH","0",1);
        const auto original=receive(c,input,s,257);
        setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);const auto actual=receive(c,input,s,257);
        unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");compare(original,actual);
        check(actual.work.implicit_screened_lanes>0 && actual.work.implicit_rejected_lanes>0,
            "normal threshold fixture did not execute/reject through native tile certificates");
        if(!noise_only)check(actual.work.implicit_refined_lanes>0 && actual.bits==bits,"native survivors did not run original guards");
        else check(actual.bits.empty() && !actual.complete,"screened noise manufactured bits or END");
        if(actual.work.implicit_proxy_ranges==0)std::cerr<<"PROXY_DETAIL noise="<<noise_only
            <<" eligible="<<actual.work.implicit_eligible_ranges<<" fallback="<<actual.work.implicit_fallback_ranges
            <<" invalid="<<actual.work.implicit_invalid_certificates<<" fp32="<<actual.work.implicit_fp32_ranges
            <<" screened="<<actual.work.implicit_screened_lanes<<'\n';
        // A deliberate multi-block tile below separately exercises near-clock
        // translation. Immediate one-origin events are not proxy evidence.

        std::cout<<"LAZY_SCREEN noise="<<noise_only<<" screened="<<actual.work.implicit_screened_lanes
            <<" rejected="<<actual.work.implicit_rejected_lanes<<" refined="<<actual.work.implicit_refined_lanes<<'\n';
    }
    // Batching may add bounded acquisition latency. It cannot change the
    // accepted prefixes, original physical endpoints or final completion.
    unsetenv("DATAPUMP_SCHED_LAZY_BATCH");setenv("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES","1024",1);setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);
    const auto batched=receive(c,pcm,s,257);unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
    check(batched.bits==bits && batched.complete && batched.completed>=pcm.absent_end,
        "bounded tiled batching changed payload or physical completion");
    check(batched.work.implicit_proxy_ranges>0,"near-clock proxy eligibility became vacuous");
    unsetenv("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES");setenv("DATAPUMP_SCHED_LAZY_BATCH","0",1);
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);
    // Preserve the original PCM-duplication storage oracle independently of
    // optional cache occupancy. Separate parity cases exercise cache storage.
    setenv("DATAPUMP_SCHED_ORIGINAL_PROJECTION_CACHE_BYTES","0",1);
    // This is the unchanged RAW PCM-duplication oracle. Optional template
    // cache occupancy is independently validated by parity/pressure cases.
    std::optional<std::string> demand_mode;
    if(const auto* mode=std::getenv("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND"))demand_mode=mode;
    setenv("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND","0",1);
    const auto shared=receive(c,pcm,s,257,false,false,false,false,false,false,false,{},false,false,false,true,true);
    const auto local=receive(c,pcm,s,257);
    if(demand_mode)setenv("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND",demand_mode->c_str(),1);
    else unsetenv("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND");
    unsetenv("DATAPUMP_SCHED_ORIGINAL_PROJECTION_CACHE_BYTES");
    unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");compare(local,shared);
    check(shared.peak<local.peak,"bound shared capture retained duplicated bank PCM/energy history");

}
void exact_guard_demand_parity() {
    auto c=configuration(3,10,false);c.integration_seconds=8;
    const Bytes bits{0,1};const auto pcm=capture(c,bits,true,0,0,0,0,0,0,4.);
    auto s=search(c,pcm);s.chunk_bits=1;s.retain_score=0;s.candidate_limit=4096;
    s.start_uncertainty_seconds=17.*modem::pattern_chip_samples(c)/c.sample_rate;
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);
    setenv("DATAPUMP_SCHED_LAZY_BATCH","0",1);
    setenv("DATAPUMP_SCHED_COLORED_EXACT_DEMAND","0",1);
    const auto ref_cpu=std::clock();const auto ref_wall=std::chrono::steady_clock::now();
    const auto reference=receive(c,pcm,s,113,false,false,false,false,false,false,true);
    const auto reference_cpu=double(std::clock()-ref_cpu)/CLOCKS_PER_SEC;
    const auto reference_wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-ref_wall).count();
    setenv("DATAPUMP_SCHED_COLORED_EXACT_DEMAND","1",1);
    const auto new_cpu=std::clock();const auto new_wall=std::chrono::steady_clock::now();
    const auto actual=receive(c,pcm,s,113,false,false,false,false,false,false,true);
    const auto actual_cpu=double(std::clock()-new_cpu)/CLOCKS_PER_SEC;
    const auto actual_wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-new_wall).count();
    compare(reference,actual);
    check(actual.bits==bits && actual.complete,"exact demand lost payload or observed END");
    check(actual.work.implicit_exact_guard_queries>0,"exact demand branch never completed an original-owner query");
    std::cout<<"EXACT_DEMAND completed="<<actual.work.implicit_exact_guard_queries
        <<" reference_cpu="<<reference_cpu<<" actual_cpu="<<actual_cpu
        <<" reference_wall="<<reference_wall<<" actual_wall="<<actual_wall
        <<" reference_peak="<<reference.peak<<" actual_peak="<<actual.peak
        <<" physical_seconds="<<double(pcm.pcm.size())/c.sample_rate
        <<" publication_sample="<<actual.first<<" completion_sample="<<actual.completed
        <<" scope=complete_single_bank_not_live_cohort"<<'\n';
    unsetenv("DATAPUMP_SCHED_COLORED_EXACT_DEMAND");unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
}
void original_projection_cache_parity() {
    auto c=configuration(10,10,false);const Bytes bits{0,1};const auto pcm=capture(c,bits,true);
    auto s=search(c,pcm,true);s.chunk_bits=1;s.retain_score=0;s.candidate_limit=4096;
    s.start_uncertainty_seconds=static_cast<double>(modem::pattern_chip_samples(c))/2/c.sample_rate;
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);setenv("DATAPUMP_SCHED_LAZY_BATCH","0",1);
    for(bool shared:{false,true}) {
        setenv("DATAPUMP_SCHED_ORIGINAL_PROJECTION_CACHE_BYTES","0",1);
        const auto reference=receive(c,pcm,s,113,false,false,false,false,false,false,true,{},false,false,false,true,shared);
        setenv("DATAPUMP_SCHED_ORIGINAL_PROJECTION_CACHE_BYTES","2097152",1);
        const auto cached=receive(c,pcm,s,113,false,false,false,false,false,false,true,{},false,false,false,true,shared);
        const auto trimmed=receive(c,pcm,s,113,false,false,true,false,false,false,true,{},false,false,false,true,shared);
        compare(reference,cached);compare(reference,trimmed);
        for(const auto* actual:{&cached,&trimmed}) {
            check(actual->bits==bits && actual->complete,"ORIGINAL projection reuse lost full payload/observed END");
            for(std::size_t i=0;i<reference.evidence.size();++i) {
                check(std::bit_cast<std::uint64_t>(reference.evidence[i].score)==
                    std::bit_cast<std::uint64_t>(actual->evidence[i].score),
                    "ORIGINAL projection cache changed exact final FP64 score bits");
            }
        }
    }
    unsetenv("DATAPUMP_SCHED_ORIGINAL_PROJECTION_CACHE_BYTES");unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
}
void exact_source_binding_parity(bool odd_chips=false) {
    namespace xs=modem::detail::scheduled_source_samples;
    if(!xs::environment()){std::cout<<"OMITTED: native exact-source positive-hit coverage; unqualified numeric environment\n";return;} // The independently tested original fallback remains valid.
    auto c=configuration(10,10,false);c.integration_seconds=8;
    if(odd_chips)setenv("DATAPUMP_SCHED_ORIGIN_CLASSES","1",1);
    if(odd_chips)c.bandwidth_hz=16;
    check(modem::symbol_sample_count(c)==8ULL*c.sample_rate,"source binding fixture lost its fixed complete symbol");
    const Bytes bits{0};const auto pcm=capture(c,bits,true);
    auto s=search(c,pcm,true);s.clock_errors_ppm={0};s.admitted_clock_half_width_ppm=0;
    s.start_uncertainty_seconds=static_cast<double>(modem::pattern_chip_samples(c))/2/c.sample_rate;
    s.worker_threads=2;
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);setenv("DATAPUMP_SCHED_LAZY_BATCH","0",1);
    const auto original=receive(c,pcm,s,113,false,false,false,false,false,true,true,{},false,false,false,true,false,false);
    const auto cached=receive(c,pcm,s,113,false,false,true,false,false,true,true,{},false,false,false,true,false,true);
    compare(original,cached);
    check(original.bits==bits && original.complete,"positive exact source fixture failed physical reception");
    check(cached.source_bytes>0 && cached.source_counts.queries>0 && cached.source_counts.cached_points>0 &&
          cached.source_counts.original_build_calls>0,"native acquisition never borrowed the bound exact source Pool");
    check(xs::binding.pool==nullptr && xs::binding.reclaim==nullptr,"joined native receiver leaked source TLS binding");
    if(odd_chips) {
        auto forced=c;forced.search_arithmetic=modem::SearchArithmetic::fp64;
        const auto reference=receive(forced,pcm,s,113,false,false,false,false,false,true,true);
        compare(original,reference);
    }
    std::cout<<"source binding queries="<<cached.source_counts.queries<<" cached="<<cached.source_counts.cached_points
        <<" build="<<cached.source_counts.original_build_calls<<" direct="<<cached.source_counts.direct_calls
        <<" bytes="<<cached.source_bytes<<" peak_combined="<<cached.peak<<"\n";
    unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
    if(odd_chips)unsetenv("DATAPUMP_SCHED_ORIGIN_CLASSES");
}
void automatic_dense_footprint() {
    constexpr std::size_t budget=23045189;
    // An implicit eligibility exclusion is not a rejection of affordable
    // original complex/drift reception, even under the diagnostic override.
    for(const auto* mode:{"auto","1"}) {
        setenv("DATAPUMP_SCHED_LAZY_ACQUISITION",mode,1);
        auto c=configuration(3,1,false);c.integration_seconds=16;
        c.receiver_strategy=modem::ReceiverStrategy::complex_only;
        auto s=modem::PatternSearch{};s.start_offset_seconds=0;s.start_uncertainty_seconds=0;
        s.drift_tolerant=true;s.frequency_offsets_hz={0};s.clock_errors_ppm={0};
        const auto g=modem::pattern_scheduled_complex_geometry(c,s);
        const auto policy=modem::pattern_scheduled_acquisition_policy(c,s,g,budget);
        check(!policy.requested,"unsupported implicit drift geometry displaced original dense fallback");
        const auto selected=modem::resolve_receiver_strategy(c,s,budget);
        check(selected.supported && selected.backend==modem::ReceiverBackend::existing_complex,
            "automatic memory policy made an affordable dense drift receiver unavailable");
    }
    for(const auto multiplier:{3U,5U})for(const auto precision:{modem::SearchArithmetic::default_mode,
            modem::SearchArithmetic::fp32,modem::SearchArithmetic::fp64}) {
        auto c=configuration(multiplier,10,false);
        c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;c.spreading_factor=64;
        c.search_arithmetic=precision;c.stream_epoch=8312;c.stream_phase_samples=0;
        auto s=modem::PatternSearch{};s.start_offset_seconds=0;s.start_uncertainty_seconds=7;
        s.search_stream_phases=true;s.drift_tolerant=false;s.frequency_offsets_hz.clear();
        s.worker_threads=1;s.receiver_strategy_workspace_bytes=budget;
        s.qualified_start_window=modem::PatternStartWindow{0,1,.02L*c.sample_rate};
        for(int j=-9;j<=9;++j)s.hypotheses.push_back({j*.00001,0});
        const auto g=modem::pattern_scheduled_complex_geometry(c,s);
        check(g.supported && g.retained_hypotheses>10000 && g.workspace_lower_bound_bytes<budget,
            "calendar footprint fixture no longer exposes the cheap-bound gap");
        unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
        const auto policy=modem::pattern_scheduled_acquisition_policy(c,s,g,budget);
        check(policy.requested && policy.supported,
            "automatic used the cheap fit-only bound instead of complete mandatory eager state");
        modem::PatternCorrelator rx(c,s,budget);
        check(rx.work().backend==modem::PatternCorrelationBackend::scheduled_acquisition &&
              rx.work().hypotheses==g.retained_hypotheses && rx.working_bytes()<=budget,
            "automatic constructor and planner disagree or lose logical lanes/rolling workspace");
    }
}

void short_split_lifecycle_parity() {
    const Bytes bits{0,1,0,1,1,0};
    for(const auto multiplier:{3U,5U})for(const bool fhss:{false,true}) {
        auto c=configuration(multiplier,10,fhss);
        c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;c.spreading_factor=64;
        auto plan=std::make_shared<modem::SchedHopPlan>(*c.scheduled_hopping);
        plan->clock_error_seconds=plan->peer_clock_error_seconds=.01;
        c.scheduled_hopping=plan;
        const modem::SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto start=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
        check(modem::symbol_sample_count(c)==5120&&modem::pattern_chip_samples(c)==8,
            "short Spread lifecycle changed waveform geometry");
        check(!modem::packed_scheduled_symbols(c),"short Spread fixture did not exercise split-source lifecycle");
        const auto pcm=capture(c,bits,true);
        auto s=search(c,pcm);s.chunk_bits=1;s.retain_score=0;s.candidate_limit=4096;
        setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","0",1);
        const auto reference=receive(c,pcm,s,113,false,false,false,false,false,false,true);
        setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);
        const auto actual=receive(c,pcm,s,113,false,false,false,false,false,false,true);
        unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
        compare(reference,actual);
        check(actual.work.backend==modem::PatternCorrelationBackend::scheduled_acquisition,
            "short-symbol fixture silently retained dense fallback");
        check(reference.bits==bits&&reference.complete&&reference.completed>=pcm.absent_end,
            "original short fixture did not finish intended bits after full observed absence");
        check(actual.bits==bits&&actual.complete&&actual.completed>=pcm.absent_end,
            "implicit short fixture lost bits or manufactured END");
    }
}


}
int main(){try {
    stable_owner_storage();
    smooth_fhss_native_parity();
    automatic_dense_footprint();
    short_split_lifecycle_parity();
    paired(1,false,false,false);paired(10,true,true,false);paired(10,false,true,true);original_push_fences();screened_noise_and_payload();original_projection_cache_parity();exact_guard_demand_parity();exact_source_binding_parity();exact_source_binding_parity(true);
    std::cout<<"scheduled implicit acquisition lifecycle tests passed\n";return 0;
} catch(const std::exception& error){unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");std::cerr<<error.what()<<'\n';return 1;}}
