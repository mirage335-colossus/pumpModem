#pragma once
// Source-only identical-PCM helper for paired scheduling tests.
#include "datapump/pattern_correlator.hpp"
#include "../src/family_simd.hpp"
#include "../src/shared_scheduled_capture.hpp"
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
struct Result {std::size_t prepared_workers_peak=0;std::vector<PollEvent> events;Bytes bits;bool complete=false;std::uint64_t first=0,completed=0;std::size_t peak=0;modem::PatternCorrelatorWork work;std::vector<modem::PatternEvidence> evidence;};
Result receive(const modem::Config& c,const Capture& input,modem::PatternSearch s,std::size_t block=113,bool cache_reference=false,bool spans=false,bool trim=false,bool moment_reference=false,bool float_curve=false,bool family=false,bool all_evidence=false,
               std::span<const std::size_t> cuts={},bool raw_score=false,bool common=false,bool simd=false,bool affine_reuse=true,bool shared=false,std::size_t budget=8*1024*1024) {
    if(all_evidence){s.retain_score=0;s.candidate_limit=std::max<std::size_t>(4096,s.candidate_limit);}
    namespace owner=modem::detail::scheduled_capture;
    std::shared_ptr<owner::SharedScheduledCapture> shared_capture;
    if(shared) {
        owner::Limits limits{budget,(input.pcm.size()+8191)/8192+8,4,8192};
        shared_capture=std::make_shared<owner::SharedScheduledCapture>(limits,owner::Identity{1,1},s.input_sample_origin);
        s.scheduled_capture=shared_capture;
    }
    modem::PatternCorrelator rx(c,s,budget,{.measure_work=true,.scheduled_template_cache_reference=cache_reference,.scheduled_raw_score_reference=raw_score,.scheduled_affine_experiment=spans,.scheduled_moment_reference=moment_reference,.scheduled_float_curve_experiment=float_curve,.scheduled_family_experiment=family,.scheduled_common_shape_experiment=common || family,.scheduled_common_affine_reuse=affine_reuse,.scheduled_common_simd_experiment=simd});Result out;
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
    check(out.work.peak_workspace_bytes<=budget,"numeric preparation reservation exceeded actual loan");
    if(all_evidence)check(out.evidence.size()<s.candidate_limit,"complete-evidence fixture truncated its lane history");
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
}
