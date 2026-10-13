#include "datapump/pattern_correlator.hpp"
#include "../src/family_simd.hpp"
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

using namespace datapump;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
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
                std::uint64_t local_phase_window=0,double local_phase_step=0) {
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
    const auto ratio=1+clock*1e-6;constexpr double delay=3.375;
    Capture out;out.start=static_cast<double>(origin.sample-tx.physical_output_origin())/ratio+delay;
    out.last=static_cast<std::uint64_t>(std::ceil((map.source_to_physical(source.add_samples(bits.size()*symbol),true).sample-tx.physical_output_origin())/ratio+delay));
    out.absent_end=static_cast<std::uint64_t>(std::ceil((map.source_to_physical(source.add_samples((bits.size()+failed)*symbol),true).sample-tx.physical_output_origin())/ratio+delay));
    // A clock-cell END at an active-group boundary may need the following
    // active group's first observations. Preserve the full two-bit assertion,
    // but provide that independently bounded calendar lookahead; nominal END+2
    // alone does not cover every admitted clock cell. It is far less than a
    // complete absent source symbol, and cannot manufacture physical completion.
    const std::uint64_t frame=c.scheduled_hopping->time_multiplier,active=c.scheduled_hopping->group_hops;
    const auto lookahead=clock==0?0ULL:static_cast<std::uint64_t>(std::ceil(.4L*(frame+(frame-active)/2+1)*c.sample_rate));
    const auto end=absence?out.absent_end+2:out.last+2+lookahead;out.pcm.resize(end);
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
struct Result {std::vector<PollEvent> events;Bytes bits;bool complete=false;std::uint64_t first=0,completed=0;std::size_t peak=0;modem::PatternCorrelatorWork work;std::vector<modem::PatternEvidence> evidence;};
Result receive(const modem::Config& c,const Capture& input,modem::PatternSearch s,std::size_t block=113,bool cache_reference=false,bool spans=false,bool trim=false,bool moment_reference=false,bool float_curve=false,bool family=false,bool all_evidence=false,
               std::span<const std::size_t> cuts={},bool raw_score=false,bool common=false,bool simd=false,bool affine_reuse=true) {
    if(all_evidence){s.retain_score=0;s.candidate_limit=4096;}
    constexpr std::size_t budget=8*1024*1024;
    modem::PatternCorrelator rx(c,s,budget,{.measure_work=true,.scheduled_template_cache_reference=cache_reference,.scheduled_raw_score_reference=raw_score,.scheduled_affine_experiment=spans,.scheduled_moment_reference=moment_reference,.scheduled_float_curve_experiment=float_curve,.scheduled_family_experiment=family,.scheduled_common_shape_experiment=common || family,.scheduled_common_affine_reuse=affine_reuse,.scheduled_common_simd_experiment=simd});Result out;
    const auto geometry=modem::pattern_scheduled_complex_geometry(c,s);
    check(geometry.retained_hypotheses==rx.work().hypotheses,"cheap scheduled geometry differs from actual retained lane count");
    check(!rx.try_skip_unobserved(1),"mapped complex must retain raw capture until an omission certificate exists");
    for(std::size_t at=0;at<input.pcm.size();) {
        auto n=std::min(block,input.pcm.size()-at);
        const auto cut=std::upper_bound(cuts.begin(),cuts.end(),at);
        if(cut!=cuts.end() && *cut<at+n)n=*cut-at;
        rx.push(std::span(input.pcm).subspan(at,n));at+=n;
        if(trim)rx.trim_optional_workspace();
        out.peak=std::max(out.peak,rx.working_bytes());check(out.peak<=budget,"mapped complex workspace overflow");
        for(const auto& burst:rx.take_bursts()) {
            if(all_evidence)out.events.push_back({at,burst});
            check(!burst.missing_slots,"calendar null manufactured missing symbols");
            if(!burst.bits.empty()){if(!out.first)out.first=at;out.bits.insert(out.bits.end(),burst.bits.begin(),burst.bits.end());}
            if(burst.complete){out.complete=true;out.completed=at;}
        }
    }
    const auto complete=out.complete;rx.finish();for(const auto& burst:rx.take_bursts())check(complete || !burst.complete,"EOF manufactured source absence");
    out.work=rx.work();out.evidence=rx.candidates();
    if(all_evidence)check(out.evidence.size()<s.candidate_limit,"complete-evidence fixture truncated its lane history");
    return out;
}
void actual_waveform_progress_absence() {
    const auto c=configuration();const Bytes bits{0,1,1};const auto input=capture(c,bits,true);const auto s=search(c,input);
    const auto actual=receive(c,input,s);check(actual.bits==bits,"split coherent receiver lost or added production bits");
    check(actual.first && actual.first<input.last,"accepted bits were batched behind the full transmission");
    check(actual.complete && actual.completed>=input.absent_end,"physical end preceded six fully scored absent source seconds");
    const auto partial=capture(c,bits,false);const auto stopped=receive(c,partial,search(c,partial));
    check(stopped.bits==bits && !stopped.complete,"EOF/null changed split pending reception");
    auto fp64=c;fp64.search_arithmetic=modem::SearchArithmetic::fp64;
    check(receive(fp64,input,s,257).bits==bits,"FP64 original-template split contraction changed bits");
    // Config stores already separated private roots. Replacing only Data
    // changes the calendar, but deliberately leaves the same private alphabet.
    // Rebind through the production key path to test a genuinely wrong key.
    transfer::Options wrong_options;wrong_options.modem=c;wrong_options.key.emplace(Bytes(32,0x74));
    const auto wrong=transfer::seeded_config(wrong_options,c.stream_epoch);
    bool wrong_rejected=false;
    try{wrong_rejected=receive(wrong,input,s).bits.empty();}
    catch(const Error& error){std::cerr<<"WRONG_KEY_EXCEPTION "<<error.what()<<"\n";wrong_rejected=std::string(error.what()).find("qualified UTC interval does not intersect")!=std::string::npos;}
    check(wrong_rejected,"wrong calendar/private key admitted payload");
    auto broad=s;broad.start_uncertainty_seconds=static_cast<double>(modem::pattern_chip_samples(c))/2/c.sample_rate;
    const auto broad_result=receive(c,input,broad);
    check(broad_result.bits==bits,"retaining adjacent admitted starts changed split bits");
    check(broad_result.work.scheduled_carry_samples>0,"fixture did not exercise next-symbol prefix before latest-END fence");
    auto noise=input;std::mt19937_64 random(529);std::normal_distribution<float> gaussian(0,.002F);
    for(auto& sample:noise.pcm)sample=gaussian(random);
    const auto rejected=receive(c,noise,s);check(rejected.bits.empty() && !rejected.complete,"noise/null manufactured admission or completion");
    modem::PatternCorrelator cancelled(c,s,8*1024*1024);cancelled.push(std::span(input.pcm).first(113));
    std::stop_source stop;stop.request_stop();bool threw=false;
    try{cancelled.push(std::span(input.pcm).subspan(113,113),stop.get_token());}catch(const Error&){threw=true;}
    check(threw,"cancelled split correlation ignored stop token");cancelled.finish();
    for(const auto& event:cancelled.take_bursts())check(!event.complete,"cancellation manufactured observed absence");
    auto long_symbol=c;long_symbol.spreading_factor=128;const auto long_input=capture(long_symbol,{1},true);
    const auto long_result=receive(long_symbol,long_input,search(long_symbol,long_input));
    check(long_result.bits==Bytes({1}) && long_result.complete && long_result.completed>=long_input.absent_end,
        "one long fully scored source symbol did not preserve physical completion");
}
void clock_frequency_and_fhss() {
    for(const auto factor:{1U,10U})for(const auto sign:{-1.,1.}) {
        auto c=configuration(10,factor,true);
        const auto span=modem::scheduled_physical_span_samples(c,modem::symbol_sample_count(c));
        const auto f=.2*c.sample_rate/static_cast<double>(span);const auto input=capture(c,{1,0},false,sign*10,sign*f);
        auto s=search(c,input,true);const auto g=modem::pattern_scheduled_complex_geometry(c,s);
        check(g.supported && g.frequency_grid_qualified && g.clock_grid_qualified,"proper physical-aperture bank was not qualified");
        const auto actual=receive(c,input,s);
        if(actual.bits!=Bytes({1,0})) {std::cerr<<"ENDPOINT factor="<<factor<<" sign="<<sign<<" bits=";for(auto bit:actual.bits)std::cerr<<unsigned(bit);std::cerr<<" guard="<<actual.work.scheduled_colored_guard_activations<<"\n";for(const auto& e:actual.evidence)std::cerr<<e.first_sample<<","<<e.end_sample<<","<<e.bit<<","<<e.score<<","<<e.admission_threshold<<"\n";}
        check(actual.bits==Bytes({1,0}),"clock/frequency endpoint with FHSS changed split bits");
        check(!actual.complete,"clock-cell lookahead manufactured six absent source seconds");
        const auto reference=receive(c,input,s,113,true);
        check(reference.bits==actual.bits && reference.first==actual.first && reference.completed==actual.completed,
            "exact frequency-shared templates changed decisions or publication");
        check(actual.work.scheduled_template_cache_hits>0 && reference.work.scheduled_template_cache_hits==0,
            "paired fixture did not exercise the exact block-template cache");
        s.worker_threads=2;const auto parallel=receive(c,input,s,257);
        check(parallel.bits==actual.bits,"worker-local calendar changed split contraction");
        check(parallel.work.scheduled_worker_preparations==1,"mapped workers were reconstructed on every PCM push");
        const auto trimmed=receive(c,input,s,257,false,false,true);
        check(trimmed.bits==parallel.bits && trimmed.first==parallel.first && trimmed.completed==parallel.completed &&
            trimmed.work.scheduled_worker_preparations>parallel.work.scheduled_worker_preparations,
            "optional scratch reclamation changed evidence/progress or failed to free reusable workers");
    }
}
void local_complex_phase_diffusion() {
    // Differential evidence retains its existing >=256 disjoint-window
    // eligibility. Fine DSSS chips exercise that branch without an hours-long
    // test; the earlier 64-chip fixture was not eligible even continuously.
    auto c=configuration(10,100);c.sample_rate=4096;c.carrier_hz=1024;
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto start=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
    c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
    const auto input=capture(c,{1},true,0,0,12);
    auto s=search(c,input);s.drift_tolerant=true;s.differential_window_seconds=.01;
    const auto result=receive(c,input,s,257);
    check(result.work.drift_sections==4 && result.work.differential_window_samples>0,
        "split calendar unnecessarily disabled quarter or differential complex evidence");
    check(result.bits==Bytes({1}) && result.complete && result.completed>=input.absent_end,
        "split local complex evidence changed phase-diffused payload or physical completion");
}
void affine_physical_statistics() {
    auto c=configuration();c.sample_rate=4096;c.carrier_hz=1024;
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto start=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
    c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
    const auto input=capture(c,{0},false);auto s=search(c,input);s.drift_tolerant=true;
    const auto first_clock=std::clock();const auto actual=receive(c,input,s,257,false,true);const auto actual_cpu=double(std::clock()-first_clock)/CLOCKS_PER_SEC;
    auto reference=c;reference.search_arithmetic=modem::SearchArithmetic::fp64;
    const auto reference_clock=std::clock();const auto original=receive(reference,input,s,257);const auto reference_cpu=double(std::clock()-reference_clock)/CLOCKS_PER_SEC;
    check(actual.bits==Bytes({0}) && actual.bits==original.bits && actual.first==original.first && !actual.complete && !original.complete,
        "table-affine physical contraction changed bits, publication or pending completion");
    check(actual.work.segments>0 && original.work.segments==0,"mapped pulse spans or FP64 original reference was not executed");
    check(actual.evidence.size()==original.evidence.size(),"mapped affine scoring dropped a hypothesis");
    for(std::size_t i=0;i<actual.evidence.size();++i) {
        const auto& a=actual.evidence[i];const auto& b=original.evidence[i];
        check(a.bit==b.bit && a.first_sample==b.first_sample && a.end_sample==b.end_sample &&
            std::abs(a.score-b.score)<1e-7*std::max(1.,std::abs(b.score)),"mapped affine original statistic disagreement");
    }
    std::cout<<"AFFINE_PARITY cpu="<<actual_cpu<<" original_cpu="<<reference_cpu
        <<" frontend="<<actual.work.frontend_cpu_seconds<<" search="<<actual.work.search_cpu_seconds
        <<" original_frontend="<<original.work.frontend_cpu_seconds<<" original_search="<<original.work.search_cpu_seconds
        <<" segments="<<actual.work.segments<<" null_pushes="<<actual.work.scheduled_null_pushes<<'\n';
}
void serial_retained_cache_accounting() {
    const auto c=configuration();const auto input=capture(c,{0},false);auto s=search(c,input);
    s.frequency_offsets_hz={-.001,0,.001};s.worker_threads=1;
    constexpr std::size_t budget=8*1024*1024;
    // Exercise the original retained-cache path explicitly. Automatic common
    // source construction can avoid these per-lane cache lookups entirely;
    // family sharing has separate accounting and parity fixtures below.
    modem::PatternCorrelator rx(c,s,budget,{.scheduled_family_experiment=false,
        .scheduled_common_shape_experiment=false});std::uint64_t prior_hits=0,prior_constructions=0;
    for(std::size_t at=0;at<22600;) {
        const auto n=std::min<std::size_t>(113,22600-at);rx.push(std::span(input.pcm).subspan(at,n));at+=n;
        const auto w=rx.work();
        check(rx.working_bytes()<=rx.reserved_workspace_bytes() && rx.reserved_workspace_bytes()<=budget,
            "serial retained cache was double-charged without a publication drain");
        check(w.scheduled_template_cache_hits>=prior_hits && w.scheduled_template_constructions>=prior_constructions,
            "serial retained cache replacement discarded cumulative work counters");
        prior_hits=w.scheduled_template_cache_hits;prior_constructions=w.scheduled_template_constructions;
    }
    check(prior_hits>0 && prior_constructions>0,"serial retained cache accounting fixture did not execute cache work");
    const auto before=rx.working_bytes();rx.trim_optional_workspace();
    check(rx.working_bytes()<before && rx.reserved_workspace_bytes()>=rx.working_bytes(),
        "serial optional cache could not be reclaimed for required rolling admission");
    rx.finish();for(const auto& e:rx.take_bursts())check(!e.complete,"cache reclamation or EOF manufactured observed absence");
}
void optional_reuse_preserves_workspace() {
    auto c=configuration();c.sample_rate=4096;c.carrier_hz=1024;
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto start=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
    c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
    const auto input=capture(c,{0},false);auto s=search(c,input);
    s.bit_limit=1;s.track_limit=1;s.candidate_limit=1;
    const auto fits=[&](std::size_t bytes) {
        try {modem::PatternCorrelator ref(c,s,bytes,{.scheduled_moment_reference=true});return true;}
        catch(const Error&){return false;}
    };
    std::size_t lo=0,hi=1024*1024;check(fits(hi),"direct workspace reference cannot construct");
    while(lo+1<hi){const auto mid=lo+(hi-lo)/2;if(fits(mid))hi=mid;else lo=mid;}
    modem::PatternCorrelator small(c,s,hi);
    check(small.reserved_workspace_bytes()<=hi,"optional moment counters displaced an affordable direct bank");
    small.push(std::span(input.pcm).first(512));
    check(small.working_bytes()<=hi,"small workspace optional reuse overflow");
    s.bit_limit=64;s.track_limit=8;s.candidate_limit=64;s.drift_tolerant=true;
    constexpr std::size_t budget=8*1024*1024;
    modem::PatternCorrelator rx(c,s,budget);
    // Remain inside the first source quarter, with optional reuse allocated
    // but the later-quarter detector state still promised rather than live.
    rx.push(std::span(input.pcm).first(4096));
    const auto loan=rx.working_bytes();
    check(rx.drift_tolerant() && rx.reserved_workspace_bytes()>loan,
        "workspace loan fixture did not retain a deferred quarter reservation");
    rx.set_workspace_bytes(loan);
    check(rx.drift_tolerant() && rx.reserved_workspace_bytes()<=loan,
        "optional reuse displaced promised quarter coverage during workspace reduction");
    // Family metadata is optional too. Reclaim it before reducing a promised
    // detector when Live returns a workspace loan.
    s.frequency_offsets_hz={-.0002,0,.0002};s.worker_threads=2;
    modem::PatternCorrelator grouped(c,s,budget,{.scheduled_family_experiment=true});
    grouped.push(std::span(input.pcm).first(4096));
    check(grouped.work().scheduled_exact_families>0,
        "workspace fixture did not materialize exact frequency families");
    const auto family_loan=grouped.working_bytes();
    check(grouped.drift_tolerant() && grouped.reserved_workspace_bytes()>family_loan,
        "family workspace fixture lacks deferred quarter state");
    grouped.set_workspace_bytes(family_loan);
    check(grouped.drift_tolerant() && grouped.reserved_workspace_bytes()<=family_loan &&
        grouped.work().scheduled_family_metadata_bytes==0,
        "optional family metadata displaced promised quarter coverage");
}
void shared_physical_moments() {
    for(const auto fhss:{false,true}) {
        auto c=configuration(10,1,fhss);c.sample_rate=4096;c.carrier_hz=1024;
        const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
        const auto start=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
        const auto clean=capture(c,{1},false,10,.0002,3);
        auto s=search(c,clean);s.drift_tolerant=true;s.clock_errors_ppm={-10,0,10};s.frequency_offsets_hz={-.0002,0,.0002};
        for(const auto noise:{0.,10.,30.}) {
            auto input=clean;std::mt19937_64 random(9031);std::normal_distribution<double> normal(0,1);
            for(auto& x:input.pcm)x+=static_cast<float>(noise*normal(random));
            for(const auto arithmetic:{modem::SearchArithmetic::default_mode,modem::SearchArithmetic::fp32}) {
                c.search_arithmetic=arithmetic;
                const auto a=receive(c,input,s,257,false,false,false,false,arithmetic==modem::SearchArithmetic::fp32),b=receive(c,input,s,257,false,false,false,true);
                check(a.work.segments>0 && b.work.segments==0,"shared physical moments or direct reference did not execute");
                check(a.bits==b.bits && a.complete==b.complete && a.first==b.first && a.completed==b.completed,
                    "shared physical moments changed publication, decisions or completion");
                check(a.evidence.size()==b.evidence.size(),"shared physical moments changed admitted evidence");
                for(std::size_t i=0;i<a.evidence.size();++i) {
                    const auto& x=a.evidence[i];const auto& y=b.evidence[i];
                    check(x.bit==y.bit && x.first_sample==y.first_sample && x.end_sample==y.end_sample &&
                        std::abs(x.score-y.score)<1e-7*std::max(1.,std::abs(y.score)) &&
                        std::abs(x.alternative_score-y.alternative_score)<1e-7*std::max(1.,std::abs(y.alternative_score)),
                        "shared physical moments changed original I/Q and covariance statistic");
                }
            }
        }
        c.search_arithmetic=modem::SearchArithmetic::fp64;
        check(receive(c,clean,s,257).work.segments==0,"FP64 force used approximate pulse interval reuse");
    }
}
bool near_evidence(double a,double b) {
    return std::isfinite(a) && std::isfinite(b) &&
        std::abs(a-b)<=1e-7*std::max({1.,std::abs(a),std::abs(b)});
}
void same_complete_result(const Result& a,const Result& b) {
    check(a.bits==b.bits && a.complete==b.complete && a.first==b.first && a.completed==b.completed,
        "shared family changed accepted bits or sampled publication/completion");
    check(a.work.hypotheses==b.work.hypotheses && a.work.phase_groups==b.work.phase_groups &&
        a.work.drift_sections==b.work.drift_sections &&
        a.work.differential_window_samples==b.work.differential_window_samples &&
        a.work.scheduled_colored_guard_activations==b.work.scheduled_colored_guard_activations &&
        a.work.scheduled_quarter_guard_fallbacks==b.work.scheduled_quarter_guard_fallbacks,
        "shared family changed detector coverage or guard activation/fallback");
    check(a.evidence.size()==b.evidence.size(),"shared family dropped completed lane evidence");
    check(a.evidence.size()>=a.work.hypotheses,"all-lane fixture did not complete every retained lane");
    for(std::size_t i=0;i<a.evidence.size();++i) {
        const auto& x=a.evidence[i];const auto& y=b.evidence[i];
        check(x.first_sample==y.first_sample && x.end_sample==y.end_sample &&
            x.stream_symbol==y.stream_symbol && x.frequency_hz==y.frequency_hz &&
            x.bit==y.bit && x.stream_phase_samples==y.stream_phase_samples &&
            x.frequency_hypothesis==y.frequency_hypothesis &&
            x.admission_threshold==y.admission_threshold &&
            x.outer_presence_active==y.outer_presence_active &&
            near_evidence(x.score,y.score) && near_evidence(x.alternative_score,y.alternative_score) &&
            near_evidence(x.outer_presence_score,y.outer_presence_score),
            "shared family changed complete lane identity, trials, Gram/noise score or outer gate");
    }
    check(a.events.size()==b.events.size(),"shared family changed the number of next-poll events");
    for(std::size_t i=0;i<a.events.size();++i) {
        const auto& x=a.events[i].burst;const auto& y=b.events[i].burst;
        check(a.events[i].poll==b.events[i].poll && x.bits==y.bits &&
            x.first_sample==y.first_sample && x.end_sample==y.end_sample &&
            x.first_stream_symbol==y.first_stream_symbol && x.frequency_hz==y.frequency_hz &&
            x.complete==y.complete && x.stream_phase_samples==y.stream_phase_samples &&
            x.stream_first_sample==y.stream_first_sample && x.stream_first_symbol==y.stream_first_symbol &&
            x.missing_slots==y.missing_slots && near_evidence(x.score,y.score) &&
            near_evidence(x.support_samples,y.support_samples),
            "shared family changed next-poll prefix, reception identity, support or physical absence");
    }
}
void reanchor_fixture(modem::Config& c) {
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto start=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
    c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
    check(!modem::packed_scheduled_symbols(c),"guard fixture stopped splitting source symbols");
}
void guard_group_plan(modem::Config& c) {
    // Keep a production-supported M=10 calendar; G=4 bounds its physical
    // dilation while these long source symbols still span several groups.
    auto plan=std::make_shared<modem::SchedHopPlan>(*c.scheduled_hopping);
    plan->group_hops=4;c.scheduled_hopping=plan;
}
void append_guard_lookahead(const modem::Config& c,Capture& input) {
    // Observe the following calendar groups so the latest timing-cell fence
    // can complete. These are actual zero PCM observations, never an EOF proof.
    input.pcm.resize(input.pcm.size()+2ULL*c.scheduled_hopping->time_multiplier*c.sample_rate);
    check(input.pcm.size()<=2*1024*1024,"guard regression exceeded its 2-Mi-sample PCM budget");
}
std::vector<std::size_t> guard_boundary_cuts(const modem::Config& c,const Capture& input,unsigned symbols) {
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const modem::SchedHopCoordinate origin{c.stream_epoch*c.sample_rate+c.stream_phase_samples,0};
    const auto source=map.locate(origin).source;
    const auto total=modem::symbol_sample_count(c),chip=modem::pattern_chip_samples(c);
    std::vector<std::size_t> cuts;
    const auto add=[&](std::uint64_t at) {
        const auto physical=map.source_to_physical(source.add_samples(at),true);
        const auto observed=input.start+static_cast<double>(physical.distance(origin));
        if(observed<2 || observed+2>=input.pcm.size())return;
        const auto n=static_cast<std::size_t>(std::ceil(observed));
        cuts.insert(cuts.end(),{n-1,n,n+1});
    };
    for(unsigned index=0;index<symbols;++index) {
        add(index*total+chip/2);add(index*total+chip);
        for(unsigned section=1;section<=4;++section)
            add(index*total+total/4*section+(total%4)*section/4);
    }
    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
    return cuts;
}

void diagnose_quarter_result(const char* path,unsigned tail,const modem::Config& c,
                             const Capture& input,const Bytes& expected,const Result& result) {
    std::cerr<<"GUARD_QUARTER_RESULT path="<<path<<" tail="<<tail<<" bits=";
    for(const auto bit:result.bits)std::cerr<<unsigned(bit);
    std::cerr<<" expected=";for(const auto bit:expected)std::cerr<<unsigned(bit);
    std::cerr<<" carry="<<result.work.scheduled_carry_samples
        <<" complete="<<result.complete<<" first="<<result.first<<" last="<<input.last
        <<" completed="<<result.completed<<" absent_end="<<input.absent_end
        <<" samples="<<input.pcm.size()<<" hypotheses="<<result.work.hypotheses
        <<" sections="<<result.work.drift_sections<<" segments="<<result.work.segments
        <<" quarter_fallbacks="<<result.work.scheduled_quarter_guard_fallbacks
        <<" colored="<<result.work.scheduled_colored_guard_activations
        <<" carry_ok="<<(result.work.scheduled_carry_samples>0)
        <<" bits_ok="<<(result.bits==expected)<<" complete_ok="<<result.complete
        <<" progress_ok="<<(result.first<input.last)
        <<" absence_ok="<<(result.completed>=input.absent_end)<<'\n';
    for(const auto& e:result.evidence)
        std::cerr<<"GUARD_QUARTER_EVIDENCE path="<<path<<" tail="<<tail
            <<" symbol="<<e.stream_symbol<<" first="<<e.first_sample<<" end="<<e.end_sample
            <<" phase="<<e.stream_phase_samples<<" frequency="<<e.frequency_hz
            <<" frequency_hypothesis="<<e.frequency_hypothesis<<" bit="<<e.bit
            <<" score="<<e.score<<" alternative="<<e.alternative_score
            <<" threshold="<<e.admission_threshold<<" outer_active="<<e.outer_presence_active
            <<" outer_score="<<e.outer_presence_score<<'\n';
    for(const auto& event:result.events) {
        const auto& e=event.burst;
        std::cerr<<"GUARD_QUARTER_EVENT path="<<path<<" tail="<<tail<<" poll="<<event.poll
            <<" symbol="<<e.first_stream_symbol<<" first="<<e.first_sample<<" end="<<e.end_sample
            <<" stream_first_sample="<<e.stream_first_sample<<" stream_first_symbol="<<e.stream_first_symbol
            <<" complete="<<e.complete<<" bits=";
        for(const auto bit:e.bits)std::cerr<<unsigned(bit);
        std::cerr<<" score="<<e.score<<" support="<<e.support_samples<<'\n';
    }
}
void diagnose_quarter_geometry(unsigned tail,const modem::Config& c,const Capture& input) {
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const modem::SchedHopCoordinate origin{c.stream_epoch*c.sample_rate+c.stream_phase_samples,0};
    const auto source=map.locate(origin).source;
    const auto total=modem::symbol_sample_count(c),group=map.source_samples_per_group();
    std::cerr<<"GUARD_QUARTER_GEOMETRY tail="<<tail<<" symbol_samples="<<total
        <<" chip="<<modem::pattern_chip_samples(c)<<" source_group="<<group
        <<" origin_physical="<<origin.sample<<" origin_source="<<source.sample
        <<" initial_source_remainder="<<(source.sample%group)
        <<" symbol_remainder="<<(total%group)<<" input_start="<<input.start<<'\n';
    for(unsigned i=1;i<=4;++i) {
        const auto target=source.add_samples(i*total);
        const auto left=map.source_to_physical(target,true),right=map.source_to_physical(target,false);
        const auto location=map.locate(left);
        std::cerr<<"GUARD_QUARTER_BOUNDARY tail="<<tail<<" symbol="<<i
            <<" target_source="<<target.sample<<" source_remainder="<<(target.sample%group)
            <<" left="<<left.sample<<" right="<<right.sample
            <<" input_left="<<input.start+static_cast<double>(left.distance(origin))
            <<" frame="<<location.frame_index<<" active="<<location.active
            <<" physical_begin="<<location.physical_begin<<" physical_end="<<location.physical_end
            <<" flat_begin="<<location.flat_begin<<" flat_end="<<location.flat_end
            <<" gain_at_left="<<modem::SchedHopSourceMap::gain(location,left)<<'\n';
    }
}

void family_complete_quarter_carry_and_noise() {
    const Bytes bits{0,1,0};
    for(const auto tail:{0U,7U}) {
        auto c=configuration();c.sample_rate=4096;c.carrier_hz=1024;guard_group_plan(c);
        const auto chips=tail?68U:64U;
        if(tail)c.integration_seconds=(static_cast<double>(chips)*1024+tail)/c.sample_rate;
        reanchor_fixture(c);
        const auto chip=modem::pattern_chip_samples(c),total=modem::symbol_sample_count(c);
        check(chip==1024 && total==chips*chip+tail,"quarter/carry fixture lost its whole/partial chip geometry");
        auto input=capture(c,bits,true,0,0,0,std::numbers::pi/2);append_guard_lookahead(c,input);
        auto s=search(c,input);s.drift_tolerant=true;s.frequency_offsets_hz={0,.0002};
        s.start_uncertainty_seconds=static_cast<double>(chip)/2/c.sample_rate;s.worker_threads=2;
        const auto cuts=guard_boundary_cuts(c,input,4);
        const auto actual=receive(c,input,s,257,false,false,false,false,false,true,true,cuts);
        const auto direct=receive(c,input,s,257,false,false,false,true,false,false,true,cuts);
        same_complete_result(actual,direct);
        check(actual.work.segments>0 && direct.work.segments==0 && actual.work.drift_sections==4,
            "quarter/carry fixture omitted moments, direct oracle or four-quarter evidence");
        diagnose_quarter_geometry(tail,c,input);
        diagnose_quarter_result("shared",tail,c,input,bits,actual);
        diagnose_quarter_result("direct",tail,c,input,bits,direct);
        check(actual.work.scheduled_carry_samples>0 && actual.bits==bits && actual.complete &&
            actual.first<input.last && actual.completed>=input.absent_end,
            "quarter carry lost fresh private bits, next-poll progress or scored absence");
        if(tail)check(actual.work.scheduled_quarter_guard_fallbacks>0,
            "partial-chip quarter geometry bypassed its conservative guard fallback");
        if(!tail) {
            auto noise=input;std::mt19937_64 random(92371);std::normal_distribution<float> gaussian(0,.05F);
            for(auto& x:noise.pcm)x=gaussian(random);
            const auto a=receive(c,noise,s,257,false,false,false,false,false,true,true,cuts);
            const auto b=receive(c,noise,s,257,false,false,false,true,false,false,true,cuts);
            same_complete_result(a,b);
            check(a.bits.empty() && !a.complete,"shared quarter/carry noise manufactured a reception");
            modem::PatternCorrelator stopped(c,s,8*1024*1024,{.scheduled_family_experiment=true});
            const auto cut=cuts.front();stopped.push(std::span(input.pcm).first(cut));
            check(stopped.candidates().empty(),"partial source support manufactured a complete lane decision");
            std::stop_source cancel;cancel.request_stop();bool threw=false;
            try{stopped.push(std::span(input.pcm).subspan(cut,113),cancel.get_token());}
            catch(const Error&){threw=true;}
            check(threw && stopped.candidates().empty(),"cancelled family published partially accumulated evidence");
            stopped.finish();for(const auto& e:stopped.take_bursts())
                check(!e.complete,"family cancellation or EOF manufactured physical absence");
        }
    }
}
void family_complete_differential_carry() {
    auto c=configuration(10,100);c.sample_rate=4096;c.carrier_hz=1024;guard_group_plan(c);reanchor_fixture(c);
    const auto window=16*modem::pattern_chip_samples(c);
    // Constant phase within each receiver-local window and repeated phase
    // turns across a quarter suppress its coherent alternative while leaving
    // the original disjoint differential pairs independently observable.
    const Bytes bits{1,0,1};auto input=capture(c,bits,true,0,0,0,0,window,.37);append_guard_lookahead(c,input);
    auto s=search(c,input);s.drift_tolerant=true;s.differential_window_seconds=.01;
    s.frequency_offsets_hz={0,.0002};s.worker_threads=2;
    s.start_uncertainty_seconds=static_cast<double>(modem::pattern_chip_samples(c))/2/c.sample_rate;
    const auto cuts=guard_boundary_cuts(c,input,4);
    const auto a=receive(c,input,s,257,false,false,false,false,false,true,true,cuts);
    const auto b=receive(c,input,s,257,false,false,false,true,false,false,true,cuts);
    same_complete_result(a,b);
    check(a.work.drift_sections==4 && a.work.differential_window_samples==window && a.work.scheduled_carry_samples>0,
        "complete family fixture omitted differential, quarter or adjacent-symbol carry coverage");
    check(a.bits==bits && a.complete && a.first<input.last && a.completed>=input.absent_end,
        "family differential carry changed local phase-winding bits or observed absence");
    for(const auto& e:a.evidence)check(!e.outer_presence_active,
        "differential evidence acquired an unqualified coherent outer-code gate");
}
void family_complete_outer_presence() {
    auto c=configuration(10,10);c.sample_rate=4096;c.carrier_hz=1024;c.bandwidth_hz=.8;c.spreading_factor=16;guard_group_plan(c);
    c.integration_seconds=(160.*1024+7)/c.sample_rate;reanchor_fixture(c);
    check(modem::pattern_chip_samples(c)==1024 && modem::symbol_sample_count(c)==160*1024+7,
        "outer moment fixture lost its shifted partial final pulse");
    const Bytes bits{0,1};auto input=capture(c,bits,true);append_guard_lookahead(c,input);
    auto s=search(c,input);s.frequency_offsets_hz={0,.0002};s.worker_threads=2;
    s.start_uncertainty_seconds=static_cast<double>(modem::pattern_chip_samples(c))/2/c.sample_rate;
    s.outer_presence_guard=true;s.drift_tolerant=false;
    const auto cuts=guard_boundary_cuts(c,input,3);
    const auto a=receive(c,input,s,257,false,false,false,false,false,true,true,cuts);
    const auto b=receive(c,input,s,257,false,false,false,true,false,false,true,cuts);
    same_complete_result(a,b);
    check(a.work.segments>0 && b.work.segments==0 && a.work.scheduled_carry_samples>0,
        "outer regression omitted pulse moments, direct oracle or separate carry state");
    check(a.bits==bits && a.complete && a.first<input.last && a.completed>=input.absent_end,
        "outer family changed fresh symbol coefficients, pending bits or physical completion");
    // Changing only the dedicated outer root preserves the Data calendar and
    // inner private alphabet, exposing structured excess rather than a new map.
    auto wrong=c;wrong.dsss_seed[0]^=0x40;
    const auto wrong_a=receive(wrong,input,s,257,false,false,false,false,false,true,true,cuts);
    const auto wrong_b=receive(wrong,input,s,257,false,false,false,true,false,false,true,cuts);
    same_complete_result(wrong_a,wrong_b);
    check(wrong_a.bits.empty(),"family shared pulse work admitted a different outer root");
    check(std::any_of(wrong_a.evidence.begin(),wrong_a.evidence.end(),[](const auto& e) {
        return e.outer_presence_active && e.outer_presence_score<e.admission_threshold;
    }),"wrong-outer family fixture never activated its independent rejection guard");
    // The production scheduled chip-excess correction may already reject this
    // wrong code. The explicit raw-score diagnostic proves that outer presence
    // itself also rejects a candidate whose preceding raw statistic would pass.
    const auto raw=receive(wrong,input,s,257,false,false,false,true,false,false,true,cuts,true);
    check(raw.bits.empty() && std::any_of(raw.evidence.begin(),raw.evidence.end(),[](const auto& e) {
        return e.score>=e.admission_threshold && e.outer_presence_active && e.outer_presence_score<e.admission_threshold;
    }),"independent outer guard did not reject the diagnostic raw-pass wrong code");
}

void phase_expanded_outer_affine_reuse() {
    // Keep the original full-outer fixture unchanged. This separate geometry
    // explicitly enumerates same-F phase siblings for the reuse-only checks.
    auto c=configuration(10,10);c.sample_rate=64;c.carrier_hz=16;c.stream_phase_samples=0;
    c.bandwidth_hz=.0125;c.spreading_factor=16;guard_group_plan(c);
    c.integration_seconds=(160.*1024+7)/c.sample_rate;
    check(modem::pattern_chip_samples(c)==1024 && modem::symbol_sample_count(c)==160*1024+7,
        "phase-expanded outer fixture lost its exact chip/partial-symbol geometry");
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto initial=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
    const auto active=map.locate(initial);
    check(active.active,"fixed outer reuse anchor did not resolve an active source group");
    // The UTC second containing a fixed group's midpoint has at least fifty
    // active phases when group=100/Fs=64. Selecting the public midpoint avoids
    // a one-phase boundary epoch without searching/changing any key or seed.
    const auto anchor=active.physical_begin+map.source_samples_per_group()/2;
    c.stream_epoch=anchor/c.sample_rate;c.stream_phase_samples=anchor%c.sample_rate;
    check(map.source_samples_per_group()==100 && map.minimum_flat_samples()==98 &&
        modem::canonical_phase_step(c)==1 && !modem::packed_scheduled_symbols(c),
        "phase-expanded outer fixture lost split-calendar or unit phase cells");
    const Bytes bits{0,1};auto input=capture(c,bits,true);append_guard_lookahead(c,input);
    // ceil((8192+3*163847)/100)+2 frames, 256 samples/frame,
    // plus the existing delay/end rounding and1280-sample lookahead <1.3M.
    check(input.pcm.size()<=1300000,"phase-expanded outer capture exceeded its analytical sample bound");
    auto s=search(c,input);s.search_stream_phases=true;s.frequency_offsets_hz={0};
    s.start_uncertainty_seconds=.001;s.worker_threads=2;
    s.outer_presence_guard=true;s.drift_tolerant=false;s.differential_window_seconds=0;
    const auto geometry=modem::pattern_scheduled_complex_geometry(c,s);
    check(geometry.supported && geometry.receiver_phase_step_samples==1 &&
        geometry.receiver_timing_subdivisions==32 && geometry.phase_positions>=50 && geometry.phase_positions<=64 &&
        geometry.timing_positions==2 && geometry.clock_hypotheses==1 &&
        geometry.retained_hypotheses==2*geometry.phase_positions && geometry.retained_hypotheses<=128,
        "reuse fixture did not enumerate the bounded same-frequency phase bank");
    const auto cuts=guard_boundary_cuts(c,input,3);
    constexpr std::size_t block=17;
    const auto a=receive(c,input,s,block,false,false,false,false,false,true,true,cuts);
    const auto unshared_pairs=receive(c,input,s,block,false,false,false,false,false,true,true,cuts,false,false,false,false);
    same_complete_result(a,unshared_pairs);
    check(a.bits==bits && a.complete && a.first<input.last && a.completed>=input.absent_end,
        "phase-expanded affine reuse changed fixed bits, pending progress or fully scored absence");
    check(a.work.scheduled_common_phase_families>0 && a.work.scheduled_common_phase_lanes>a.work.scheduled_common_phase_families &&
        a.work.scheduled_common_mapped_pairs>0 && a.work.scheduled_common_affine_reused_lanes>0 &&
        a.work.scheduled_common_mapped_pairs+a.work.scheduled_common_affine_reused_lanes==a.work.scheduled_common_mapped_lanes,
        "phase-expanded outer fixture omitted actual same-frequency affine reuse or counter identity");
    check(unshared_pairs.work.scheduled_common_affine_reused_lanes==0 &&
        unshared_pairs.work.scheduled_common_mapped_pairs==unshared_pairs.work.scheduled_common_mapped_lanes &&
        unshared_pairs.work.scheduled_common_mapped_lanes==a.work.scheduled_common_mapped_lanes &&
        unshared_pairs.work.scheduled_common_affine_lanes==a.work.scheduled_common_affine_lanes &&
        unshared_pairs.work.scheduled_common_mapped_pairs>a.work.scheduled_common_mapped_pairs,
        "affine paired reference skipped logical updates or misreported actual evaluations");
    check(std::any_of(a.evidence.begin(),a.evidence.end(),[](const auto& e){return e.outer_presence_active;}),
        "phase-expanded reuse fixture never exercised the full outer-presence guard");
    for(std::size_t i=0;i<a.evidence.size();++i) {
        const auto& x=a.evidence[i];const auto& y=unshared_pairs.evidence[i];
        check(x.score==y.score && x.alternative_score==y.alternative_score &&
            x.outer_presence_score==y.outer_presence_score,
            "exact affine pair reuse changed scalar score or complete outer-pulse arithmetic");
    }
    for(std::size_t i=0;i<a.events.size();++i)
        check(a.events[i].burst.score==unshared_pairs.events[i].burst.score &&
            a.events[i].burst.support_samples==unshared_pairs.events[i].burst.support_samples,
            "exact affine pair reuse changed published score/support");
}

// PROPOSAL ONLY: include in tests/test_sched_hop_correlator.cpp after its
// guard fixture helpers. No fixture seed, thresholds or expected bits changed.
// Also call check_timing_continuation_events(actual,bits,257) and the same for
// direct in family_complete_quarter_carry_and_noise() after diagnostics.
void check_timing_continuation_events(const Result& result,const Bytes& expected,std::size_t quantum,
                                     bool expect_complete=true) {
    check(!result.events.empty(),"timing continuation did not expose event history");
    const auto& first=result.events.front().burst;
    const auto identity=std::pair{first.stream_first_sample,first.stream_first_symbol};
    Bytes published;std::size_t terminals=0;std::vector<std::size_t> polls;
    for(const auto& item:result.events) {
        const auto& event=item.burst;
        check(std::pair{event.stream_first_sample,event.stream_first_symbol}==identity,
            "timing handoff changed physical reception identity");
        check(event.first_stream_symbol==first.stream_first_symbol+published.size(),
            "timing handoff repeated or skipped a canonical source ordinal");
        check(!event.missing_slots,"timing handoff replaced observations with missing slots");
        if(!event.bits.empty()) {
            check(!terminals,"timing handoff emitted bits after physical end");
            published.insert(published.end(),event.bits.begin(),event.bits.end());
            check(published.size()<=expected.size() && std::equal(published.begin(),published.end(),expected.begin()),
                "timing handoff rewrote published prefix or appended wrong private bits");
            for(std::size_t i=0;i<event.bits.size();++i)polls.push_back(item.poll);
        }
        if(event.complete)check(++terminals==1 && published==expected,"timing completion lost or duplicated source bits");
    }
    check(published==expected && terminals==static_cast<std::size_t>(expect_complete),
        "timing continuation changed exact endpoint or physical completion state");
    // In this long-symbol fixture each accepted bit has its own poll. A third
    // standalone central decision must be visible within the input quantum,
    // rather than being withheld until a subsequent source symbol or absence.
    std::uint64_t accepted_end=0;
    for(const auto& e:result.evidence)
        if(e.stream_symbol==2 && e.bit==expected.at(2) && e.score>=e.admission_threshold)
            accepted_end=std::max(accepted_end,e.end_sample);
    check(accepted_end && polls.at(2)>=accepted_end && polls.at(2)<=accepted_end+quantum,
        "fresh fully admitted guide bit missed its next progress poll");
}
void timing_continuation_chunk_and_frequency_order() {
    const Bytes bits{0,1,0};auto c=configuration();c.sample_rate=4096;c.carrier_hz=1024;guard_group_plan(c);
    constexpr unsigned tail=7,chips=68;
    c.integration_seconds=(static_cast<double>(chips)*1024+tail)/c.sample_rate;reanchor_fixture(c);
    auto input=capture(c,bits,true,0,0,0,std::numbers::pi/2);append_guard_lookahead(c,input);
    auto s=search(c,input);s.drift_tolerant=true;s.frequency_offsets_hz={.0002,0};
    s.start_uncertainty_seconds=static_cast<double>(modem::pattern_chip_samples(c))/2/c.sample_rate;
    s.worker_threads=2;s.chunk_bits=1;
    const auto cuts=guard_boundary_cuts(c,input,4);
    for(const bool direct:{false,true}) {
        const auto got=receive(c,input,s,257,false,false,false,direct,false,!direct,true,cuts);
        check(got.bits==bits && got.complete && got.completed>=input.absent_end,
            "chunk-one/reversed frequency enumeration broke exact timing continuation or absence");
        check_timing_continuation_events(got,bits,257);
        // Keep all captured PCM through the selected third-bit decision and
        // stop before a complete failed source-symbol interval. This EOF run
        // must retain010 under the same pending identity without physical end.
        std::uint64_t end=0;
        for(const auto& e:got.evidence)
            if(e.stream_symbol==2 && e.bit==0 && e.score>=e.admission_threshold)end=std::max(end,e.end_sample);
        check(end+2<input.absent_end,"pending capture accidentally included source absence");
        auto pending=input;pending.pcm.resize(static_cast<std::size_t>(end+2));
        const auto stopped=receive(c,pending,s,257,false,false,false,direct,false,!direct,true,cuts);
        check(stopped.bits==bits && !stopped.complete,"EOF after timing continuation manufactured physical completion");
        check_timing_continuation_events(stopped,bits,257,false);
    }
}

void common_raw_simd_progress_and_guards() {
    namespace family = datapump::modem::detail::family_simd;
    auto c=configuration();c.sample_rate=128;c.carrier_hz=32;reanchor_fixture(c);
    const Bytes bits{0,1,0};auto input=capture(c,bits,true);append_guard_lookahead(c,input);
    auto s=search(c,input);s.drift_tolerant=true;
    s.frequency_offsets_hz={-.0004,-.0002,0,.0002,.0004}; // Five lanes exercise a vector tail.
    s.start_uncertainty_seconds=static_cast<double>(modem::pattern_chip_samples(c))/2/c.sample_rate;
    const auto cuts=guard_boundary_cuts(c,input,4);
    const auto run=[&](bool simd,bool trim=false) {
        return receive(c,input,s,113,false,false,trim,true,false,false,true,cuts,false,true,simd);
    };
    // Force original raw template/contraction in both runs; common construction
    // and every source/guard/serial completion fence remain identical.
    const auto reference=run(false),actual=run(true);same_complete_result(actual,reference);
    check(actual.bits==bits && actual.complete && actual.first<input.last && actual.completed>=input.absent_end,
        "common raw SIMD changed next-poll bits or calendar-scored physical absence");
    check(actual.work.scheduled_common_raw_fit_pairs+actual.work.scheduled_common_raw_reused_lane_samples==actual.work.scheduled_common_raw_lane_samples,
        "common raw unique-frequency reuse dropped an original lane observation");
    check(actual.work.scheduled_common_raw_lane_samples==reference.work.scheduled_common_raw_lane_samples &&
        actual.work.scheduled_common_shapes==reference.work.scheduled_common_shapes,
        "common raw SIMD changed represented raw work or private template construction");
    if(family::selected()!=family::Backend::scalar) {
        check(actual.work.scheduled_common_simd_samples>0 && actual.work.scheduled_common_simd_lane_samples>0 &&
            actual.work.scheduled_common_simd_peak_bytes>0 && actual.work.scheduled_common_simd_guard_updates>0,
            "common raw SIMD fixture never executed its packed FP64/guard path");
        check(actual.work.scheduled_common_simd_packed_values==6*actual.work.scheduled_common_simd_lane_samples,
            "common raw SIMD omitted projection packing from its diagnostic counts");
    } else check(actual.work.scheduled_common_simd_samples==0 && actual.work.scheduled_common_simd_peak_bytes==0,
        "portable scalar fallback reserved or claimed native SIMD execution");
    same_complete_result(run(true,true),run(false,true)); // Reclaim before later pushes.
    auto one_bank=s;one_bank.frequency_offsets_hz={0};one_bank.search_stream_phases=true;
    const auto one_reference=receive(c,input,one_bank,113,false,false,false,true,false,false,true,cuts);
    const auto one_reuse=receive(c,input,one_bank,113,false,false,false,true,false,false,true,cuts,false,true,false);
    same_complete_result(one_reuse,one_reference);
    check(one_reuse.work.scheduled_common_phase_families>0 && one_reuse.work.scheduled_common_raw_reused_lane_samples>0 &&
        one_reuse.work.scheduled_common_raw_fit_pairs<one_reuse.work.scheduled_common_raw_lane_samples &&
        one_reuse.work.scheduled_common_simd_samples==0 && one_reuse.work.scheduled_common_raw_peak_bytes>0 &&
        one_reuse.work.scheduled_common_raw_packed_values==0,
        "one-frequency phase twins did not reuse the exact scalar Fit pair with SIMD disabled");
    check(one_reuse.work.scheduled_common_raw_fit_pairs+one_reuse.work.scheduled_common_raw_reused_lane_samples==
        one_reuse.work.scheduled_common_raw_lane_samples,
        "exact phase duplicate reuse dropped or invented an independent lane observation");
    auto short_bank=s;short_bank.frequency_offsets_hz={0,.0002,.0004};
    const auto short_result=receive(c,input,short_bank,113,false,false,false,true,false,false,true,cuts,false,true,true);
    check(short_result.work.scheduled_common_simd_samples==0 && short_result.work.scheduled_common_simd_short_samples>0,
        "common raw SIMD ignored its explicit four-unfitted-lane crossover");
    modem::PatternCorrelator stopped(c,s,8*1024*1024,
        {.scheduled_moment_reference=true,.scheduled_common_shape_experiment=true,.scheduled_common_simd_experiment=true});
    stopped.push(std::span(input.pcm).first(113));std::stop_source cancel;cancel.request_stop();bool threw=false;
    try{stopped.push(std::span(input.pcm).subspan(113,113),cancel.get_token());}catch(const Error&){threw=true;}
    check(threw && stopped.candidates().empty(),"cancelled common raw SIMD published partial lane evidence");
    stopped.finish();for(const auto& burst:stopped.take_bursts())check(!burst.complete,
        "common raw SIMD cancellation/EOF manufactured physical absence");
}

}
int main(){try {
    if(const auto scope=std::getenv("DATAPUMP_SCHED_GUARD_ONLY");scope && std::string(scope)=="quarter") {
        family_complete_quarter_carry_and_noise();return 0;
    }
    if(const auto scope=std::getenv("DATAPUMP_SCHED_GUARD_ONLY");scope && std::string(scope)=="common-simd") {
        common_raw_simd_progress_and_guards();return 0;
    }
    if(const auto scope=std::getenv("DATAPUMP_SCHED_GUARD_ONLY");scope && std::string(scope)=="affine-reuse") {
        phase_expanded_outer_affine_reuse();return 0;
    }
    common_raw_simd_progress_and_guards();actual_waveform_progress_absence();clock_frequency_and_fhss();local_complex_phase_diffusion();affine_physical_statistics();serial_retained_cache_accounting();optional_reuse_preserves_workspace();shared_physical_moments();family_complete_quarter_carry_and_noise();family_complete_differential_carry();family_complete_outer_presence();phase_expanded_outer_affine_reuse();timing_continuation_chunk_and_frequency_order();std::cout<<"scheduled complex correlator checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
