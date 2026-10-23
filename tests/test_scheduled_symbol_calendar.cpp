#include "datapump/scheduled_symbol_calendar.hpp"
#include "resource_test_margin.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace datapump;
using namespace datapump::modem;

namespace {
void check(bool value,const char* text) {if(!value)throw std::runtime_error(text);}
template<class F> void rejects(F&& operation,const char* text) {
    try {operation();} catch(const Error&) {return;}
    throw std::runtime_error(text);
}
Config configuration(std::uint32_t fs,std::uint64_t samples,unsigned groups,
                     bool hopping,bool shaped=true) {
    Config c;
    c.sample_rate=fs;c.bandwidth_hz=fs/2.;c.carrier_hz=fs/4.;
    // Keep ceil(T*Fs) exact despite a nonbinary Fs denominator.
    c.integration_seconds=(static_cast<double>(samples)-.25)/fs;
    c.pulse_shaping=shaped;c.scramble=true;c.data_key.emplace(Bytes(32,0x73));
    auto schedule=std::make_shared<SchedHopPlan>();
    schedule->time_multiplier=groups<10?10:10000;schedule->group_hops=groups;
    schedule->clock_error_seconds=schedule->peer_clock_error_seconds=.001;
    schedule->region_seconds=.001;schedule->audio_error_seconds=.01;
    schedule->ramp_seconds=.003;
    c.scheduled_hopping=schedule;
    if(hopping) {
        auto fhss=std::make_shared<FhssPlan>();
        fhss->channels=2;fhss->shifts[0]=30000000;fhss->shifts[1]=31000000;
        fhss->dwell_seconds=.4;
        fhss->power_ramps=fhss->mandatory_ramps=fhss->smooth_ramps=true;
        fhss->clock_error_seconds=fhss->peer_clock_error_seconds=.001;
        fhss->region_seconds=.001;fhss->audio_error_seconds=.01;fhss->ramp_seconds=.003;
        c.fake_hopping=fhss;
    }
    check(symbol_sample_count(c)==samples,"fixture quantized to a different symbol length");
    return c;
}

// Independent small-geometry oracle: inspect the analytic full-gain gate at every sample,
// form each full-gain run, reserve the documented single interior sample on
// either FHSS edge, intersect outer flat, then pack the full finite support.
// It deliberately performs no capacity-prefix or rank/select arithmetic.
std::vector<std::uint64_t> brute_offsets(const Config& c,std::uint64_t first_hop) {
    const auto& plan=*c.scheduled_hopping;
    const auto fs=c.sample_rate;
    const auto T=symbol_sample_count(c),H=pattern_pulse_padding_samples(c);
    const auto D=2ULL*fs/5,L=D*plan.group_hops;
    const auto origin=SchedHopSchedule::sample_boundary(first_hop,fs,true);
    const SchedHopSourceMap map(plan,*c.data_key,fs);
    const auto model=map.locate(map.source_to_physical({0,0}));
    const auto flat_begin=origin+model.flat_begin-model.physical_begin;
    const auto flat_end=origin+L-(model.physical_end-model.flat_end);
    std::vector<std::uint64_t> result;
    const auto pack=[&](std::uint64_t begin,std::uint64_t end) {
        begin=std::max(begin,flat_begin);end=std::min<std::uint64_t>(end,flat_end);
        if(end<=begin || H>(end-begin)/2)return;
        for(auto start=begin+H;start<=end-H && T<=end-H-start;start+=T)
            result.push_back(start-origin);
    };
    if(!c.fake_hopping) {pack(flat_begin,flat_end);return result;}
    const FhssPowerShape shape(*c.fake_hopping,fs,H,T);
    for(std::uint64_t n=0;n<plan.group_hops;++n) {
        const auto begin=SchedHopSchedule::sample_boundary(first_hop+n,fs);
        const auto end=SchedHopSchedule::sample_boundary(first_hop+n+1,fs,true);
        // Evaluate the five exact local gate phases. A rounded smootherstep
        // result can equal 1 just outside its full-gain branch; it must not
        // expand an exact template window. Absolute UTC never enters a float.
        const auto residue=(first_hop+n)%5;
        const auto low=residue*(2.L*fs)/5+shape.quiet+shape.support-shape.lag_center;
        const auto high=(residue+1)*(2.L*fs)/5-shape.quiet-shape.support-shape.lag_center;
        auto first=end,last=begin;
        for(auto p=begin;p<end;++p) {
            const auto local=p%(2ULL*fs);
            if(local>=low && local<=high) {
                first=std::min(first,p);last=std::max(last,p);
            }
        }
        if(first<last)pack(first+1,last);
    }
    return result;
}

void compare_brute(Config c) {
    const auto T=symbol_sample_count(c),H=pattern_pulse_padding_samples(c);
    const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    std::uint64_t K=std::numeric_limits<std::uint64_t>::max();
    for(unsigned r=0;r<5;++r)K=std::min<std::uint64_t>(K,brute_offsets(c,r).size());
    if(!K) {
        rejects([&]{ScheduledSymbolCalendar unused(c);},"empty joint window admitted a payload symbol");
        return;
    }
    const ScheduledSymbolCalendar calendar(c);
    check(calendar.slots_per_group()==K,"uniform minimum group capacity disagrees with gate oracle");
    std::optional<FhssPowerShape> shape;
    if(c.fake_hopping)shape.emplace(*c.fake_hopping,c.sample_rate,H,T);
    const auto L=map.source_samples_per_group();
    for(std::uint64_t q=0;q<12;++q) {
        const auto f=map.schedule().frame(q);
        const auto offsets=brute_offsets(c,f.active_begin_hop);
        check(offsets.size()>=K,"keyed group has fewer slots than the uniform bound");
        const auto group_start=SchedHopSchedule::sample_boundary(f.active_begin_hop,c.sample_rate,true);
        for(std::uint64_t j=0;j<K;++j) {
            const auto rank=q*K+j,U=q*L+offsets[j],p=group_start+offsets[j];
            check(calendar.source_slot(rank)==U,"source rank/select disagrees with independent packed windows");
            check(calendar.physical_slot(rank)==p,"physical rank/select used the wrong inverse or hop residue");
            check(calendar.first_rank_source(U)==rank,"source lower_bound lost equality at a slot");
            check(calendar.first_rank_source(U-1)==rank,"source lower_bound skipped a slot before its boundary");
            check(calendar.first_rank_source(U+1)==rank+1,"source lower_bound retained an already-started slot");
            check(calendar.first_rank(p/c.sample_rate,p%c.sample_rate)==rank,"physical anchor did not select its own source slot");
            check(calendar.offset_source(p/c.sample_rate,p%c.sample_rate,0)==0,"exact anchor acquired source wait");
            check(calendar.lower_bound_source(p/c.sample_rate,p%c.sample_rate,0)==0,"relative lower_bound lost its anchor");
            check(calendar.lower_bound_source(p/c.sample_rate,p%c.sample_rate,1)==1,"relative lower_bound failed to advance");
            // Every sampled contribution, including both H shoulders, remains
            // at unity in both post-shaper envelopes.
            for(auto s=p-H;s<p+T+H;++s) {
                const auto location=map.locate(s);
                check(SchedHopSourceMap::gain(location,{s,0})==1.,"packed finite pulse enters an outer ramp or null");
                if(shape)check(shape->gain(s/c.sample_rate,s%c.sample_rate,c.sample_rate)==1.,
                               "packed finite pulse enters an FHSS ramp or null");
            }
        }
        check(calendar.first_rank_source(q*L)==q*K,"source group boundary did not choose the first eligible slot");
        check(calendar.first_rank_source((q+1)*L-1)==(q+1)*K,"discarded extra residue slots leaked into the calendar");
        // A physical anchor in the null tail advances to the NEXT group,
        // independent of the amount of historical null time.
        const auto tail=SchedHopSchedule::sample_boundary(f.end_hop,c.sample_rate)-1;
        check(calendar.first_rank(tail/c.sample_rate,tail%c.sample_rate)==(q+1)*K,
              "null anchor reused the preceding active group");
    }
    for(std::uint64_t epoch=0;epoch<12;++epoch) {
        std::vector<std::uint64_t> phases;
        calendar.for_each_phase_in_epoch(epoch,[&](std::uint64_t phase){phases.push_back(phase);});
        auto rank=calendar.first_rank(epoch,0);
        for(const auto phase:phases) {
            check(phase<c.sample_rate,"phase enumeration escaped its UTC second");
            check(calendar.physical_slot(rank++)==epoch*c.sample_rate+phase,
                  "phase enumeration missed or duplicated a global rank");
        }
        check(calendar.physical_slot(rank)>=(epoch+1)*c.sample_rate,"phase enumeration stopped early");
    }
}

void geometry_and_boundaries() {
    for(const auto fs:{64U,512U,6120U})for(const auto G:{1U,2U,7U}) {
        compare_brute(configuration(fs,4,G,false,false));
        compare_brute(configuration(fs,64,G,false));
        compare_brute(configuration(fs,64,G,true));
    }
    // Common scheduler fixtures: fractional 512-Hz and integral 6120-Hz hop grids,
    // full finite pulse support,
    // combined endpoint clocks, region and nonnegative audio lag.
    for(const auto fs:{512U,6120U})for(const bool hopping:{false,true}) {
        auto c=configuration(fs,static_cast<std::uint64_t>(std::ceil(.125L*fs)),1,hopping);
        auto plan=std::make_shared<SchedHopPlan>(*c.scheduled_hopping);
        plan->clock_error_seconds=plan->peer_clock_error_seconds=.01;
        plan->audio_error_seconds=.05;plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        if(hopping) {
            auto fhss=std::make_shared<FhssPlan>(*c.fake_hopping);
            fhss->clock_error_seconds=fhss->peer_clock_error_seconds=.01;
            fhss->audio_error_seconds=.05;fhss->ramp_seconds=.00572;c.fake_hopping=fhss;
        }
        compare_brute(c);
        check(ScheduledSymbolCalendar(c).slots_per_group()>0,"common short scheduled fixture has no safe slot");
        plan=std::make_shared<SchedHopPlan>(*plan);plan->audio_error_seconds=.25;c.scheduled_hopping=plan;
        rejects([&]{ScheduledSymbolCalendar unused(c);},"large audio uncertainty admitted an unsupported short symbol");
    }
}

void bounds_and_separation() {
    auto c=configuration(512,64,1,true);
    const ScheduledSymbolCalendar ordinary(c);
    auto many=c;
    auto plan=std::make_shared<SchedHopPlan>(*many.scheduled_hopping);
    plan->time_multiplier=10000;plan->group_hops=9999;many.scheduled_hopping=plan;
    const ScheduledSymbolCalendar large(many);
    check(large.working_bytes()==ordinary.working_bytes() && testing::benchmark_below(large.working_bytes(),std::size_t{4096},"test_scheduled_symbol_calendar.large_working_bytes","workspace_bytes"),
          "calendar allocated memory proportional to group or elapsed symbols");
    const auto K=large.slots_per_group();
    const std::array<std::uint64_t,4> ranks{0,K-1,K,100000000ULL*K+K/2};
    for(const auto rank:ranks) {
        const auto source=large.source_slot(rank);
        check(large.first_rank_source(source)==rank,"large random-access calendar scanned or lost rank identity");
        (void)large.physical_slot(rank);
    }
    auto other=c;other.data_key.emplace(Bytes(32,0x74));const ScheduledSymbolCalendar second(other);
    bool changed=false;
    for(std::uint64_t q=0;q<16;++q)
        changed|=ordinary.physical_slot(q*ordinary.slots_per_group())!=second.physical_slot(q*second.slots_per_group());
    check(changed,"independent FHSS-purpose schedule keys produced an identical burst calendar");
    const auto stream=c.data_key->stream(StreamPurpose::Scrambler,41,0,128,StreamDomain::PatternZeroV2);
    for(std::uint64_t q=0;q<16;++q)(void)ordinary.source_slot(q*ordinary.slots_per_group());
    check(stream==c.data_key->stream(StreamPurpose::Scrambler,41,0,128,StreamDomain::PatternZeroV2),
          "calendar consumed or restarted private-pattern stream addresses");
    rejects([&]{(void)ordinary.first_rank(std::numeric_limits<std::uint64_t>::max(),0);},"UTC multiplication wrapped");
    rejects([&]{(void)ordinary.first_rank(0,c.sample_rate);},"out-of-second phase accepted");
    rejects([&]{(void)ordinary.source_slot(std::numeric_limits<std::uint64_t>::max());},"exhausted rank wrapped to old source bytes");
    rejects([&]{(void)ordinary.physical_slot(std::numeric_limits<std::uint64_t>::max());},"exhausted physical rank wrapped");
    rejects([&]{(void)ordinary.lower_bound_source(0,0,std::numeric_limits<std::uint64_t>::max());},"relative source addition wrapped");
    auto no_key=c;no_key.data_key.reset();
    rejects([&]{ScheduledSymbolCalendar unused(no_key);},"secret schedule accepted without a key");
    auto disabled=c;disabled.scheduled_hopping.reset();
    rejects([&]{ScheduledSymbolCalendar unused(disabled);},"disabled schedule selected packed calendar");
    auto diagnostic=c;
    auto fhss=std::make_shared<FhssPlan>(*c.fake_hopping);fhss->smooth_ramps=false;fhss->defer_symbols=true;
    diagnostic.fake_hopping=fhss;
    rejects([&]{ScheduledSymbolCalendar unused(diagnostic);},"diagnostic coefficient omissions silently became full-gain windows");
}
}

int main() {
    try {
        geometry_and_boundaries();bounds_and_separation();
        std::cout<<"scheduled symbol calendar tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
