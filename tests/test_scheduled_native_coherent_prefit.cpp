#include "test_environment.hpp"
// Actual receiver internals plus the independent existing capture/poll helpers.
// This target runs only the new cases below; the original suite stays intact.
#include "../src/pattern_correlator.cpp"
#define main scheduled_lazy_previous_main
#include "test_scheduled_lazy_acquisition.cpp"
#undef main
#include <cfenv>

namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    static bool qualified(){return PatternCorrelator::Impl::lazy_reference_environment();}
    static std::array<std::uint64_t,6> native(const PatternCorrelator& rx) {
        const auto* s=rx.impl_->lazy_state();
        if(!s)return {};
        return {s->native_prefit_queries,s->native_prefit_completed,s->native_prefit_rejections,
                s->native_prefit_rows,s->native_prefit_tiles,s->native_prefit_fallbacks};
    }
};
}
namespace {
namespace proxy=datapump::modem::detail::scheduled_fp32_prefit;
using Fit=datapump::modem::detail::CorrelationFit;
using Chip=datapump::modem::detail::CorrelationChipEvidence;
using Projection=datapump::modem::detail::CorrelationProjection;
void contains(const proxy::color::FitIntervals& i,const Fit& f) {
    check(proxy::color::finite(i) && i.count==f.count,"native field enclosure/count unavailable");
    const auto has=[](auto a,double x){return a.lo<=x && x<=a.hi;};
    check(has(i.xc,f.xc)&&has(i.xs,f.xs)&&has(i.cc,f.cc)&&has(i.ss,f.ss)&&has(i.cs,f.cs)&&has(i.energy,f.energy),
          "native field interval omitted an executed Original field");
}
modem::Config numeric_config(std::uint64_t chip) {
    modem::Config c;c.sample_rate=chip==8?14400:6120;c.carrier_hz=chip==8?3600:1500;c.bandwidth_hz=chip==8?360:36;
    c.dsss=true;c.dsss_factor=10;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.data_key.emplace(Bytes(32,0x61));
    c.integration_seconds=std::nextafter(static_cast<double>(200*chip+13)/c.sample_rate,0.);
    for(std::size_t n=0;n<c.spreading_seed.size();++n){c.spreading_seed[n]=3*n+7;c.dsss_seed[n]=5*n+11;}
    check(modem::pattern_chip_samples(c)==chip,"native numeric fixture lost C8/C34 geometry");return c;
}
void source_and_field_enclosures() {
    std::size_t rejections=0,rows=0;
    for(const auto chip:{8ULL,34ULL}) {
        auto c=numeric_config(chip);modem::PatternCode code(c,c.stream_epoch);
        check(code.symbol_samples()%chip!=0,"native fixture lost partial final chip");
        auto scratch=std::make_unique<proxy::Scratch>();
        std::array<Fit,2> original{},carry{};std::array<Chip,2> chips{},carry_chips{};
        const auto amplitude=std::sqrt(2*modem::nominal_signal_power)*modem::outer_dsss_transmit_gain(c);
        Projection prefix{1e5,-1e5,1e5,1e5,-1e5,1e5};
        for(unsigned target=0;target<2;++target) {
            const std::size_t total=target?128:512;
            const long double first=target?.125L:static_cast<long double>(code.symbol_samples())-512.25L;
            for(std::size_t offset=0;offset<total;offset+=64) {
                const auto count=std::min<std::size_t>(64,total-offset);
                for(std::size_t n=0;n<count;++n){const auto at=first+static_cast<long double>(offset+n)*1.00001L;
                    scratch->coordinates[n]=static_cast<double>(at);scratch->gain[n]=.75+.2*std::sin((offset+n)*.031);
                    scratch->carry[n]=target!=0;}
                check(scratch->evaluate(count,code,c,c.stream_phase_samples,target*code.chips_per_symbol(),amplitude),
                      "qualified native paired source tile did not execute");
                code.set_stream_phase_samples(c.stream_phase_samples);code.prepare_symbol(target*code.chips_per_symbol());
                for(std::size_t n=0;n<count;++n) {
                    const auto at=first+static_cast<long double>(offset+n)*1.00001L;
                    const auto owner=static_cast<std::uint64_t>(at/chip);
                    auto q=code.shaped_values(target*code.chips_per_symbol(),static_cast<double>(at));
                    for(auto& z:q)z=scratch->gain[n]*modem::pattern_limit_pcm(amplitude*z)/amplitude;
                    for(unsigned bit=0;bit<2;++bit)check(std::abs(q[bit].real()-scratch->q[n][bit].real())<=scratch->final_radius &&
                        std::abs(q[bit].imag()-scratch->q[n][bit].imag())<=scratch->final_radius,"post-limiter/gain radius omitted Original q");
                    const auto x=.002*std::sin(.731*(offset+n+17*target)),angle=.37*(offset+n);
                    const auto co=std::cos(angle),si=std::sin(angle);auto next=prefix;
                    next.xc+=x*co;next.xs+=x*si;next.cc+=co*co;next.ss+=si*si;next.cs+=co*si;next.energy+=x*x;
                    const auto p=next-prefix;prefix=next;
                    auto& whole=target?carry:original;auto& ce=target?carry_chips:chips;
                    for(unsigned bit=0;bit<2;++bit){Fit contribution;contribution.add(p,q[bit],1);
                        proxy::add_fit(whole[bit],contribution);ce[bit].add_fit(contribution,owner);}
                    scratch->state.append(target!=0,p,scratch->q[n],scratch->final_upper,scratch->final_radius,owner);++rows;
                }
            }
        }
        const auto result=proxy::finish(scratch->state,30,{1e-8,true});
        check(result.ready && scratch->native_rows==640 && scratch->native_tiles==10,"native positive producer path missing");
        for(unsigned bit=0;bit<2;++bit){contains(scratch->state.current.whole.budget.enclose(scratch->state.current.whole.fit[bit]),original[bit]);
            contains(scratch->state.carry.whole.budget.enclose(scratch->state.carry.whole.fit[bit]),carry[bit]);
            check(scratch->state.current.chips[bit].count==original[bit].count &&
                  scratch->state.carry.chips[bit].count==carry[bit].count,"native chip flush changed current/carry counts");
            const auto raw=original[bit].score();const auto score=chips[bit].colored_excess(original[bit],30)?std::min(raw,chips[bit].score(original[bit])):raw;
            check(result.raw>=raw && result.upper>=score,"native certificate fell below executed Original raw/chip score");}
        rejections+=result.upper<5;
    }
    check(rows==1280 && rejections>0,"native source executed no certified rejection below retention floor");
}
void domain_and_cancellation() {
    proxy::Budget b;Projection p{0x1p20,-0x1p20,0x1p20,0x1p20,-0x1p20,1};b.observe(p,16,0);
    check(b.errors().valid,"bounded endpoint arithmetic lost its certificate");
    p.cs=std::nextafter(0x1p20,std::numeric_limits<double>::infinity());b.observe(p,1,0);
    check(!b.errors().valid,"out-of-domain projection retained a certificate");
    proxy::Budget q;q.observe({},16,1e-8);check(!q.errors().valid,"out-of-domain phase retained a certificate");
    proxy::Budget boundary;boundary.count=std::min<std::uint64_t>(proxy::count_limit,
        (std::numeric_limits<std::size_t>::max()-32)/12);boundary.d=boundary.h=1;boundary.a=16;
    check(boundary.errors().valid,"maximum qualified count overflowed the error graph");
    ++boundary.count;check(!boundary.errors().valid,"out-of-domain count retained a certificate");
    proxy::State zero;Projection z{.01,.02,1,1,.1,.03};zero.append(false,z,{},0,1e-7,0);
    auto interval=zero.current.whole.budget.enclose(zero.current.whole.fit[0]);
    check(interval.valid && !interval.zero_basis && interval.count==1,"native zero erased an Original owner/zero-basis uncertainty");
    proxy::State subnormal;std::array<Fit,2> original{};Projection tiny_p{0x1p20,-0x1p20,0x1p20,0x1p20,-0x1p20,1};
    const proxy::Pair tiny_q{{{0x1p-530,0x1p-531},{-0x1p-531,0x1p-530}}};
    for(unsigned n=0;n<128;++n){subnormal.append(false,tiny_p,{},0,0x1p-528,0);
        for(unsigned bit=0;bit<2;++bit){Fit contribution;contribution.add(tiny_p,tiny_q[bit],1);proxy::add_fit(original[bit],contribution);}}
    for(unsigned bit=0;bit<2;++bit)contains(subnormal.current.whole.budget.enclose(subnormal.current.whole.fit[bit]),original[bit]);
    proxy::State cells;const Projection cell_p{.01,.02,1,1,0,.03};
    const proxy::Pair cell_q{{{1,0},{.5,.5}}};
    const std::array<std::uint64_t,7> owners{0,0,1,1,1,2,2};
    for(std::size_t n=0;n<owners.size();++n){cells.append(false,cell_p,cell_q,1,0,owners[n]);
        const auto expected=n<2?0ULL:n<5?2ULL:5ULL;
        check(cells.current.chips[0].count==expected && cells.current.chips[1].count==expected,
              "native chip flush count changed before actual owner transition");}
    check(cells.current.finish() && cells.current.chips[0].count==7 && cells.current.chips[1].count==7,
          "native last chip was not retained exactly once");
    const auto saved=cells.current.chips;
    check(cells.current.finish(),"native second finish invalidated completed cells");
    for(unsigned bit=0;bit<2;++bit)check(saved[bit].count==cells.current.chips[bit].count &&
        saved[bit].rank==cells.current.chips[bit].rank &&
        std::bit_cast<std::uint64_t>(saved[bit].projected.lo)==std::bit_cast<std::uint64_t>(cells.current.chips[bit].projected.lo) &&
        std::bit_cast<std::uint64_t>(saved[bit].projected.hi)==std::bit_cast<std::uint64_t>(cells.current.chips[bit].projected.hi),
        "native finish retained the last chip twice");
    check(!proxy::unclipped(8,1e-6,.3,1).valid,"ambiguous/clipped source acquired an unclipped certificate");
    const auto rounding=std::fegetround();std::fesetround(FE_DOWNWARD);
    const auto rejected=!proxy::unclipped(1,1e-6,.3,1).valid && !proxy::Scratch::Native::supported();
    std::fesetround(rounding);check(rejected,"unqualified fenv retained native source execution");
    auto c=numeric_config(8);modem::PatternCode code(c,c.stream_epoch);auto scratch=std::make_unique<proxy::Scratch>();
    scratch->coordinates[0]=150;scratch->gain[0]=1;std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{(void)scratch->evaluate(1,code,c,0,0,.3,stop.get_token());}catch(const Error&){cancelled=true;}
    check(cancelled && scratch->native_rows==0 && scratch->state.current.whole.fit[0].count==0,
          "cancelled native source adopted a partial Fit");
    scratch->coordinates[1]=151;scratch->gain[1]=1;scratch->carry[1]=true;
    check(!scratch->evaluate(2,code,c,0,0,.3),"mixed current/carry private target entered one native tile");
    code.enable_transmit_trace(0);
    check(!scratch->evaluate(1,code,c,0,0,.3),"native gather changed Original transmit tracing");
}
struct Runtime {Result result;std::array<std::uint64_t,6> counts{};};
Runtime runtime_receive(const modem::Config& c,const Capture& input,modem::PatternSearch s,bool native) {
    datapump::testing::set_environment("DATAPUMP_SCHED_NATIVE_COHERENT_PREFIT",native?"1":"0");
    modem::PatternCorrelator rx(c,s,8*1024*1024,{.measure_work=true});Runtime out;
    for(std::size_t at=0;at<input.pcm.size();) {
        const auto n=std::min<std::size_t>(113,input.pcm.size()-at);rx.push(std::span(input.pcm).subspan(at,n));at+=n;rx.process_pending();
        out.result.peak=std::max(out.result.peak,rx.working_bytes());check(out.result.peak<=8*1024*1024,"native temporary scratch escaped workspace charge");
        for(const auto& burst:rx.take_bursts()){out.result.events.push_back({at,burst});
            check(!burst.missing_slots,"native proxy manufactured a missing slot");
            if(!burst.bits.empty()){if(!out.result.first)out.result.first=at;out.result.bits.insert(out.result.bits.end(),burst.bits.begin(),burst.bits.end());}
            if(burst.complete){out.result.complete=true;out.result.completed=at;}}
    }
    const auto complete=out.result.complete;rx.finish();for(const auto& burst:rx.take_bursts())check(complete || !burst.complete,"native EOF manufactured absence");
    out.result.evidence=rx.candidates();out.result.work=rx.work();out.counts=modem::PatternCorrelatorTimingTestAccess::native(rx);return out;
}
void exact_runtime_parity() {
    datapump::testing::set_environment("DATAPUMP_SCHED_LAZY_ACQUISITION","1");datapump::testing::set_environment("DATAPUMP_SCHED_LAZY_BATCH","0");
    datapump::testing::set_environment("DATAPUMP_SCHED_COHERENT_PREFIT","1");
    auto c=configuration(3,10,false);c.sample_rate=600;c.carrier_hz=150;c.integration_seconds=std::nextafter(9630./600,0.);
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto anchor=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);c.stream_epoch=anchor.sample/c.sample_rate;c.stream_phase_samples=anchor.sample%c.sample_rate;
    const Bytes bits{0,1};auto input=capture(c,bits,true);auto s=search(c,input);s.chunk_bits=1;s.retain_score=5;s.candidate_limit=4096;
    s.clock_errors_ppm={-12,0,12};s.start_uncertainty_seconds=modem::pattern_chip_samples(c)/2./c.sample_rate;
    const auto reference=runtime_receive(c,input,s,false),actual=runtime_receive(c,input,s,true);compare(reference.result,actual.result);
    check(actual.result.bits==bits && actual.result.complete && actual.result.completed>=input.absent_end,"native runtime lost exact bits/scored absence");
    check(reference.result.events.size()==actual.result.events.size(),"native pending poll count changed");
    for(std::size_t n=0;n<actual.result.events.size();++n){const auto& a=reference.result.events[n];const auto& b=actual.result.events[n];
        check(a.poll==b.poll && a.burst.bits==b.burst.bits && a.burst.complete==b.burst.complete,
              "native receiver changed next-poll payload/completion");}
    for(std::size_t n=0;n<actual.result.evidence.size();++n){const auto& a=reference.result.evidence[n];const auto& b=actual.result.evidence[n];
        check(std::bit_cast<std::uint64_t>(a.score)==std::bit_cast<std::uint64_t>(b.score) &&
              std::bit_cast<std::uint64_t>(a.alternative_score)==std::bit_cast<std::uint64_t>(b.alternative_score) &&
              a.outer_presence_score==b.outer_presence_score && a.outer_presence_active==b.outer_presence_active,
              "native rejection changed retained exact diagnostics");}
    std::mt19937_64 random(72119);std::normal_distribution<double> noise(0,.002);for(auto& x:input.pcm)x=static_cast<float>(noise(random));
    s.clock_errors_ppm={-600,0,600};s.start_uncertainty_seconds=2.*modem::pattern_chip_samples(c)/c.sample_rate;
    const auto no=runtime_receive(c,input,s,false),yes=runtime_receive(c,input,s,true);compare(no.result,yes.result);
    check(yes.result.bits.empty()&&!yes.result.complete && yes.counts[0]>0 && yes.counts[1]>0 && yes.counts[2]>0 &&
          yes.counts[3]>0 && yes.counts[4]>0,"qualified native runtime executed no native probe/rejection");
    std::cout<<"native probe queries="<<yes.counts[0]<<" completed="<<yes.counts[1]<<" rejected="<<yes.counts[2]
        <<" rows="<<yes.counts[3]<<" tiles="<<yes.counts[4]<<" fallback="<<yes.counts[5]<<'\n';
    datapump::testing::set_environment("DATAPUMP_SCHED_NATIVE_COHERENT_PREFIT",nullptr);datapump::testing::set_environment("DATAPUMP_SCHED_COHERENT_PREFIT",nullptr);
    datapump::testing::set_environment("DATAPUMP_SCHED_LAZY_ACQUISITION",nullptr);datapump::testing::set_environment("DATAPUMP_SCHED_LAZY_BATCH",nullptr);
}
}
int main(){try {
    if(!proxy::Scratch::Native::supported() || !modem::PatternCorrelatorTimingTestAccess::qualified()){
        std::cout<<"native paired q / Original reference arithmetic unavailable\n";return 77;}
    source_and_field_enclosures();domain_and_cancellation();exact_runtime_parity();
    std::cout<<"native FP32 q / exact Original rejection preservation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
