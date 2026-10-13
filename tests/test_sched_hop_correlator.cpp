#include "datapump/pattern_correlator.hpp"
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
Capture capture(const modem::Config& c,const Bytes& bits,bool absence,double clock=0,double frequency=0,double diffusion=0) {
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
struct Result {Bytes bits;bool complete=false;std::uint64_t first=0,completed=0;std::size_t peak=0;modem::PatternCorrelatorWork work;std::vector<modem::PatternEvidence> evidence;};
Result receive(const modem::Config& c,const Capture& input,modem::PatternSearch s,std::size_t block=113,bool cache_reference=false,bool spans=false,bool trim=false,bool moment_reference=false,bool float_curve=false) {
    constexpr std::size_t budget=8*1024*1024;
    modem::PatternCorrelator rx(c,s,budget,{.measure_work=true,.scheduled_template_cache_reference=cache_reference,.scheduled_affine_experiment=spans,.scheduled_moment_reference=moment_reference,.scheduled_float_curve_experiment=float_curve});Result out;
    const auto geometry=modem::pattern_scheduled_complex_geometry(c,s);
    check(geometry.retained_hypotheses==rx.work().hypotheses,"cheap scheduled geometry differs from actual retained lane count");
    check(!rx.try_skip_unobserved(1),"mapped complex must retain raw capture until an omission certificate exists");
    for(std::size_t at=0;at<input.pcm.size();) {
        const auto n=std::min(block,input.pcm.size()-at);rx.push(std::span(input.pcm).subspan(at,n));at+=n;
        if(trim)rx.trim_optional_workspace();
        out.peak=std::max(out.peak,rx.working_bytes());check(out.peak<=budget,"mapped complex workspace overflow");
        for(const auto& burst:rx.take_bursts()) {
            check(!burst.missing_slots,"calendar null manufactured missing symbols");
            if(!burst.bits.empty()){if(!out.first)out.first=at;out.bits.insert(out.bits.end(),burst.bits.begin(),burst.bits.end());}
            if(burst.complete){out.complete=true;out.completed=at;}
        }
    }
    const auto complete=out.complete;rx.finish();for(const auto& burst:rx.take_bursts())check(complete || !burst.complete,"EOF manufactured source absence");
    out.work=rx.work();out.evidence=rx.candidates(8);return out;
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
    modem::PatternCorrelator rx(c,s,budget);std::uint64_t prior_hits=0,prior_constructions=0;
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
}
int main(){try {actual_waveform_progress_absence();clock_frequency_and_fhss();local_complex_phase_diffusion();affine_physical_statistics();serial_retained_cache_accounting();optional_reuse_preserves_workspace();shared_physical_moments();std::cout<<"scheduled complex correlator checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
