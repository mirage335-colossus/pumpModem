#include "../src/pattern_correlator.cpp"
#define main scheduled_lazy_previous_main
#include "test_scheduled_lazy_acquisition.cpp"
#undef main
#include <cfenv>
#include <set>
#include <tuple>

namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using Cohort=detail::scheduled_native_cohort::Batch;
    struct Snapshot {
        std::uint64_t trials=0,history=0,sample=0;
        std::optional<detail::scheduled_processing::Block> block;
        std::vector<std::tuple<std::size_t,std::size_t,std::size_t,std::size_t,std::uint64_t>> due;
        friend bool operator==(const Snapshot&,const Snapshot&)=default;
    };
    static bool qualified(){return PatternCorrelator::Impl::lazy_reference_environment();}
    static Snapshot snapshot(const PatternCorrelator& rx){const auto& s=*rx.impl_;Snapshot out{s.trials,s.lazy_state()->history_generation,s.sample,s.lazy_state()->committing_block,{}};
        for(const auto& r:s.lazy_state()->due_ranges)out.due.emplace_back(r.family,r.first,r.end,r.id,r.index);return out;}
    static bool clean(const PatternCorrelator& rx){const auto& s=*rx.impl_;const auto* x=s.lazy_state();
        return !x->native_cohort && !x->transient_reservation && x->occupied[0] && x->logical[0]==std::numeric_limits<std::size_t>::max() &&
            !x->replayed[0] && !s.hypotheses[0].fits[0].count && !s.hypotheses[0].fits[1].count &&
            !s.scheduled->scheduled_carry[0].fits[0].count && !s.scheduled->scheduled_carry[0].fits[1].count;}
    static std::optional<std::pair<std::size_t,std::uint64_t>> remaining(const PatternCorrelator& rx){const auto& s=*rx.impl_;
        std::optional<std::pair<std::size_t,std::uint64_t>> out;
        for(const auto& r:s.lazy_state()->due_ranges)if(r.first<r.end){const auto key=std::pair{r.id,r.index};if(!out || key<*out)out=key;}return out;}
    static auto first(PatternCorrelator& rx){auto& s=*rx.impl_;auto selected=s.lazy_state()->due_ranges.end();
        for(auto i=s.lazy_state()->due_ranges.begin();i!=s.lazy_state()->due_ranges.end();++i)if(i->first<i->end &&
            (selected==s.lazy_state()->due_ranges.end() || std::pair{i->id,i->index}<std::pair{selected->id,selected->index}))selected=i;
        require(selected!=s.lazy_state()->due_ranges.end(),"paused native fixture has no actual due event");
        return std::pair{selected->id,selected->index};}
    static void prepare(PatternCorrelator& rx,std::stop_token stop={}){const auto key=first(rx);rx.impl_->lazy_prepare_native_cohort(key.first,key.second,stop);}
    static Cohort* batch(PatternCorrelator& rx){return rx.impl_->lazy_state()->native_cohort.get();}
    static void drop(PatternCorrelator& rx){rx.impl_->lazy_drop_native_cohort();}
    static auto stats(const PatternCorrelator& rx){const auto* s=rx.impl_->lazy_state();if(!s)return std::array<std::uint64_t,9>{};
        return std::array<std::uint64_t,9>{s->cohort_preparations,s->cohort_batches,s->cohort_queries,s->cohort_rejections,s->cohort_request_rows,
            s->cohort_unique_rows,s->cohort_dedup_reuses,s->cohort_native_rows,s->cohort_budget_fallbacks};}
    static void budget_fallback(PatternCorrelator& rx){auto& s=*rx.impl_;drop(rx);const auto old=s.budget;
        s.budget=sizeof(PatternCorrelator)+s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        try{prepare(rx);}catch(...){s.budget=old;throw;}s.budget=old;}
    static void trace(PatternCorrelator& rx){rx.impl_->code.enable_transmit_trace(0);}
    static void poison_prefix(PatternCorrelator& rx){const auto nan=std::numeric_limits<double>::quiet_NaN();
        for(auto& p:rx.impl_->lazy_state()->prefix)p={nan,nan,nan,nan,nan,nan};}
    static auto consume(PatternCorrelator& rx,const Cohort::Event& event,double threshold){return rx.impl_->lazy_native_cohort_prefit(event.context.lane,event.context.index,threshold,{});}
    static void capsule(PatternCorrelator& rx,std::size_t index,const detail::scheduled_fp32_prefit::Frozen& frozen){
        auto& event=rx.impl_->lazy_state()->native_cohort->event[index];event.frozen=frozen;event.consumed=false;}
    static std::size_t first_resident(PatternCorrelator& rx){const auto key=first(rx);return rx.impl_->lazy_ensure_slot(key.first,key.second);}
    static std::size_t resident(PatternCorrelator& rx,const Cohort::Event& event){return rx.impl_->lazy_ensure_slot(event.context.lane.id,event.context.index);}
    static void release(PatternCorrelator& rx,std::size_t slot){rx.impl_->lazy_release_cold(slot);}
    static std::vector<std::pair<std::size_t,std::uint64_t>> expected(PatternCorrelator& rx){auto& s=*rx.impl_;
        std::vector<std::pair<std::size_t,std::uint64_t>> all;
        for(const auto& r:s.lazy_state()->due_ranges)for(auto origin=r.first;origin<r.end && origin-r.first<33;++origin)
            all.emplace_back(s.lazy_family_lane(r.family,origin).id,r.index);
        std::sort(all.begin(),all.end());std::vector<std::pair<std::size_t,std::uint64_t>> out;
        for(const auto key:all){if(out.size()==32)break;const bool duplicate=!out.empty() && out.back().first==key.first;out.push_back(key);if(duplicate)break;}
        return out;
    }
    static void repeated_lane(PatternCorrelator& rx){auto& s=*rx.impl_;drop(rx);const auto key=first(rx);
        const auto found=std::find_if(s.lazy_state()->due_ranges.begin(),s.lazy_state()->due_ranges.end(),[&](const auto& r){return r.first<r.end && r.id==key.first && r.index==key.second;});
        auto saved=std::move(s.lazy_state()->due_ranges);
        typename PatternCorrelator::Impl::LazyState::DueRange one{};one.family=found->family;one.first=found->first;one.end=one.first+1;one.id=key.first;one.index=key.second;
        auto second=one;++second.index;s.lazy_state()->due_ranges={one,second};
        try{prepare(rx);const auto* b=batch(rx);require(b && b->inspected==2 && b->count<=1 && b->through_id==second.id && b->through_index==second.index,
            "native duplicate lane was not inspected/covered/stopped");drop(rx);}catch(...){s.lazy_state()->due_ranges=std::move(saved);throw;}
        s.lazy_state()->due_ranges=std::move(saved);
    }
};
}
namespace {
namespace cohort=datapump::modem::detail::scheduled_native_cohort;
namespace proxy=datapump::modem::detail::scheduled_fp32_prefit;
namespace resume=datapump::modem::detail::scheduled_resume;
using Access=modem::PatternCorrelatorTimingTestAccess;
constexpr std::size_t cohort_loan=32*1024*1024;
void cohort_modes(bool enabled){setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);setenv("DATAPUMP_SCHED_LAZY_BATCH","0",1);
    setenv("DATAPUMP_SCHED_COHERENT_PREFIT","1",1);setenv("DATAPUMP_SCHED_NATIVE_COHERENT_PREFIT","0",1);
    setenv("DATAPUMP_SCHED_NATIVE_COHORT",enabled?"1":"0",1);}
modem::Config cohort_config(){auto c=configuration(3,10,false);c.sample_rate=600;c.carrier_hz=150;
    c.integration_seconds=std::nextafter(9630./600,0.);const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto anchor=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);c.stream_epoch=anchor.sample/c.sample_rate;c.stream_phase_samples=anchor.sample%c.sample_rate;return c;}
modem::PatternSearch cohort_search(const modem::Config& c,const Capture& input){auto s=search(c,input);s.search_stream_phases=false;s.chunk_bits=1;s.retain_score=5;s.candidate_limit=4096;
    const auto df=.2*c.sample_rate/static_cast<double>(modem::scheduled_physical_span_samples(c,modem::symbol_sample_count(c)));
    s.frequency_offsets_hz={-df,0,df};return s;}
struct StopBefore {std::stop_source stop;bool fired=false;
    static void observe(resume::Boundary b,std::uint64_t,void* p)noexcept{auto& self=*static_cast<StopBefore*>(p);
        if(!self.fired && b==resume::Boundary::before_event){self.fired=true;self.stop.request_stop();}}};
std::unique_ptr<modem::PatternCorrelator> paused(const modem::Config& c,const Capture& input,modem::PatternSearch s){
    auto rx=std::make_unique<modem::PatternCorrelator>(c,s,cohort_loan,modem::PatternCorrelatorOptions{.measure_work=true});StopBefore gate;bool stopped=false;
    {resume::Scope scope({&StopBefore::observe,&gate});try{rx->push(input.pcm,gate.stop.get_token());}catch(const Error&){if(!gate.fired)throw;stopped=true;}}
    check(stopped && gate.fired && Access::clean(*rx),"native pre-event stop did not preserve a clean actual receiver");return rx;
}
struct Fault {
    cohort::Phase target=cohort::Phase::native;std::stop_source stop;bool fired=false,bad_alloc=false;
    static void observe(cohort::Phase phase,std::uint64_t value,void* p){auto& self=*static_cast<Fault*>(p);
        if(self.fired || phase!=self.target || (!value && phase!=cohort::Phase::consume))return;
        self.fired=true;if(self.bad_alloc)throw std::bad_alloc{};self.stop.request_stop();}
};
struct HookScope {cohort::Hook old=cohort::test_hook;void* context=cohort::test_context;
    explicit HookScope(Fault& fault){cohort::test_hook=&Fault::observe;cohort::test_context=&fault;}
    ~HookScope(){cohort::test_hook=old;cohort::test_context=context;}};
void actual_capability_tests(const modem::Config& c,const Capture& input,modem::PatternSearch s){
    auto rx=paused(c,input,s);const auto before=Access::snapshot(*rx);
    for(const auto phase:{cohort::Phase::allocation,cohort::Phase::enumeration,cohort::Phase::exported,cohort::Phase::deduplicated,
                          cohort::Phase::native,cohort::Phase::scatter,cohort::Phase::ready}){
        Fault fault;fault.target=phase;bool rejected=false;
        {HookScope scope(fault);try{Access::prepare(*rx,fault.stop.get_token());}catch(const Error&){if(!fault.stop.stop_requested())throw;rejected=true;}}
        check(fault.fired && rejected && Access::clean(*rx) && Access::snapshot(*rx)==before,
            "native mid-preparation cancellation changed actual trial/event/frontier or retained charged scratch");
    }
    Fault allocation;allocation.target=cohort::Phase::allocation;allocation.bad_alloc=true;
    {HookScope scope(allocation);Access::prepare(*rx);}
    check(allocation.fired && Access::clean(*rx) && Access::snapshot(*rx)==before,"native allocation failure published state or leaked reservation");
    const auto budget_before=Access::stats(*rx)[8];Access::budget_fallback(*rx);
    check(Access::clean(*rx) && Access::snapshot(*rx)==before && Access::stats(*rx)[8]==budget_before+1,
          "native optional budget exhaustion changed receiver state");
    Access::prepare(*rx);auto* batch=Access::batch(*rx);check(batch && batch->count>=2 && batch->inspected<=32,"actual native cohort did not publish multiple members");
    std::set<std::size_t> frequencies;for(std::size_t i=0;i<batch->count;++i)frequencies.insert(batch->event[i].context.lane.frequency);
    check(frequencies.size()>=2 && Access::stats(*rx)[6]>0 && Access::stats(*rx)[7]>0,"actual cohort never shared raw source across carriers");
    const auto expected=Access::expected(*rx);check(batch->inspected==expected.size() &&
        std::pair{batch->through_id,batch->through_index}==expected.back(),"cohort through_key differs from actual inspected event stream");
    const auto event0=batch->event[0],event1=batch->event[1];
    Access::poison_prefix(*rx);const auto n=event0.frozen.whole[0].count;
    const auto log_error=n>2?proxy::cert::divide(1e-8,.5*static_cast<double>(n-2)):0.;
    const auto expected_score=proxy::evaluate(event0.frozen,37,{log_error,true});const auto actual_score=Access::consume(*rx,event0,37);
    check(event0.frozen.ready && expected_score.ready && actual_score.ready &&
        (std::bit_cast<std::uint64_t>(expected_score.raw)==std::bit_cast<std::uint64_t>(actual_score.raw) &&
         std::bit_cast<std::uint64_t>(expected_score.upper)==std::bit_cast<std::uint64_t>(actual_score.upper)),
          "cohort borrowed overwritten carrier prefix or froze an earlier threshold");
    // Build a genuine generic-State capsule with strong chip mismatch and
    // finite residual. Its colored predicate changes with the charged T.
    proxy::State routed;const proxy::Pair unit{{{1,0},{1,0}}};proxy::Projection prefix{};
    for(std::size_t i=0;i<4000;++i){const auto angle=.37*i,co=std::cos(angle),si=std::sin(angle);
        const auto x=((i/200)%2?-.5:1.)*co+.1*std::sin(1.713*i);auto next=prefix;
        next.xc+=x*co;next.xs+=x*si;next.cc+=co*co;next.ss+=si*si;next.cs+=co*si;next.energy+=x*x;
        routed.append(false,next-prefix,unit,1,0,i/200);prefix=next;}
    auto uncertain=routed;const proxy::Projection extra{.01,.02,1,1,.1,.03};
    uncertain.append(false,extra,{},0,1e-7,21);uncertain.append(false,extra,{},0,1e-7,21);
    const auto raw_frozen=proxy::freeze(uncertain);const auto raw_result=proxy::evaluate(raw_frozen,37,{1e-8,true});
    check(raw_frozen.ready && !raw_frozen.chips[0].valid && raw_result.ready && raw_result.upper==raw_result.raw,
          "uncertain chip rank discarded an independently finite whole raw bound");
    const auto capsule=proxy::freeze(routed);const auto error=proxy::cert::divide(1e-8,.5*3998);
    const auto low=proxy::evaluate(capsule,0,{error,true}),high=proxy::evaluate(capsule,1e9,{error,true});
    check(low.ready && high.ready && low.upper<high.upper,"threshold capsule did not cover colored-predicate change");
    Access::capsule(*rx,0,capsule);const auto low_actual=Access::consume(*rx,event0,0);
    Access::capsule(*rx,0,capsule);const auto high_actual=Access::consume(*rx,event0,1e9);
    check(low_actual.ready && high_actual.ready && low_actual.upper==low.upper && high_actual.upper==high.upper,
          "actual native seam ignored the serial charged threshold");
    const auto slot=Access::resident(*rx,event1);check(Access::batch(*rx)==batch && batch->event[1].frozen.ready,
        "residency fixture evicted the capability before its veto");check(!Access::consume(*rx,event1,37).ready,"future materialized resident consumed a cold native capability");
    Access::release(*rx,slot);Access::drop(*rx);Access::repeated_lane(*rx);
    // The fenv and trace checks execute the actual preparation entry point.
    const auto rounding=std::fegetround();std::fesetround(FE_DOWNWARD);Access::prepare(*rx);std::fesetround(rounding);
    check(!Access::batch(*rx),"unsupported arithmetic prepared a native cohort");
    Access::trace(*rx);Access::prepare(*rx);check(!Access::batch(*rx),"transmit tracing entered native cohort preparation");
    // Independently cover the32-inspected cap with real wider catalogue lanes.
    auto wide=s;wide.start_uncertainty_seconds=8.*modem::pattern_chip_samples(c)/c.sample_rate;
    auto many=paused(c,input,wide);const auto order=Access::expected(*many);check(order.size()==32,"wide fixture did not reach the declared32 inspected events");
    const auto skipped=Access::first(*many);const auto skipped_slot=Access::first_resident(*many);
    Access::prepare(*many);check(Access::batch(*many) && Access::batch(*many)->inspected==32 && Access::batch(*many)->count<32 &&
        std::pair{Access::batch(*many)->through_id,Access::batch(*many)->through_index}==order.back(),"cohort cap counted eligible members instead of actual events");
    for(std::size_t i=0;i<Access::batch(*many)->count;++i)check(Access::batch(*many)->event[i].context.lane.id!=skipped.first,
        "ineligible inspected resident became a prepared member");Access::release(*many,skipped_slot);
}
struct Run {Result result;std::array<std::uint64_t,9> stats{};};
struct AfterCommit {
    modem::PatternCorrelator* rx=nullptr;std::stop_source stop;bool fired=false;
    static void observe(resume::Boundary boundary,std::uint64_t,void* p)noexcept{auto& self=*static_cast<AfterCommit*>(p);
        if(self.fired || boundary!=resume::Boundary::event_committed)return;
        auto* b=Access::batch(*self.rx);if(!b || b->count<2 || !Access::stats(*self.rx)[2])return;
        const auto next=Access::remaining(*self.rx);if(!next)return;
        for(std::size_t i=0;i<b->count;++i)if(!b->event[i].consumed && b->event[i].frozen.ready &&
            std::pair{b->event[i].context.lane.id,b->event[i].context.index}>=*next){self.fired=true;self.stop.request_stop();return;}}
};
Run run_cohort(const modem::Config& c,const Capture& input,modem::PatternSearch s,bool enabled,
               bool raw_reference=false,Fault* fault=nullptr,bool after_commit=false){
    cohort_modes(enabled);modem::PatternCorrelatorOptions options;options.raw_reference=raw_reference;options.measure_work=true;
    modem::PatternCorrelator rx(c,s,cohort_loan,options);Run out;AfterCommit commit{&rx};
    const auto old_hook=cohort::test_hook;const auto old_context=cohort::test_context;
    struct RestoreHook{cohort::Hook hook;void* context;~RestoreHook(){cohort::test_hook=hook;cohort::test_context=context;}} restore{old_hook,old_context};
    if(fault){cohort::test_hook=&Fault::observe;cohort::test_context=fault;}
    resume::Scope commit_scope({after_commit?&AfterCommit::observe:nullptr,&commit});
    for(std::size_t at=0;at<input.pcm.size();){const auto count=std::min<std::size_t>(113,input.pcm.size()-at);const auto span=std::span(input.pcm).subspan(at,count);
        const auto token=fault&&!fault->fired?fault->stop.get_token():after_commit&&!commit.fired?commit.stop.get_token():std::stop_token{};
        try{rx.push(span,token);}catch(const Error&){if(!token.stop_requested())throw;
            check(Access::clean(rx),"public native cancellation retained READY proof/diagnostic scratch/reservation");rx.push(span);}
        at+=count;rx.process_pending();out.result.peak=std::max({out.result.peak,rx.working_bytes(),rx.work().peak_workspace_bytes});check(out.result.peak<=cohort_loan,"cohort escaped its charged workspace");
        for(const auto& burst:rx.take_bursts()){out.result.events.push_back({at,burst});check(!burst.missing_slots,"cohort manufactured an unknown slot");
            if(!burst.bits.empty()){if(!out.result.first)out.result.first=at;out.result.bits.insert(out.result.bits.end(),burst.bits.begin(),burst.bits.end());}
            if(burst.complete){out.result.complete=true;out.result.completed=at;}}
    }
    const auto complete=out.result.complete;rx.finish();for(const auto& burst:rx.take_bursts())check(complete || !burst.complete,"cohort EOF manufactured physical END");
    out.result.evidence=rx.candidates();out.result.work=rx.work();out.stats=Access::stats(rx);
    if(after_commit)check(commit.fired,"public cancellation never interrupted a READY cohort after serial commit");return out;
}
void exact_parity(const Result& a,const Result& b){compare(a,b);check(a.events.size()==b.events.size(),"cohort changed pending poll count");
    for(std::size_t i=0;i<a.events.size();++i)check(a.events[i].poll==b.events[i].poll && a.events[i].burst.bits==b.events[i].burst.bits &&
        a.events[i].burst.complete==b.events[i].burst.complete,"cohort changed next-poll publication or scored END");
    for(std::size_t i=0;i<a.evidence.size();++i){const auto& x=a.evidence[i];const auto& y=b.evidence[i];
        check(std::bit_cast<std::uint64_t>(x.score)==std::bit_cast<std::uint64_t>(y.score) &&
            std::bit_cast<std::uint64_t>(x.alternative_score)==std::bit_cast<std::uint64_t>(y.alternative_score) &&
            std::bit_cast<std::uint64_t>(x.outer_presence_score)==std::bit_cast<std::uint64_t>(y.outer_presence_score) &&
            x.outer_presence_active==y.outer_presence_active && x.admission_threshold==y.admission_threshold,
            "cohort changed exact retained diagnostics/charged trial threshold");}
}
void actual_runtime_tests(){cohort_modes(true);auto c=cohort_config();const Bytes bits{0,1};const auto input=capture(c,bits,true);auto s=cohort_search(c,input);
    setenv("DATAPUMP_SCHED_NATIVE_COHERENT_PREFIT","1",1);bool exclusive=false;
    try{modem::PatternCorrelator invalid(c,s,cohort_loan);}catch(const Error&){exclusive=true;}
    setenv("DATAPUMP_SCHED_NATIVE_COHERENT_PREFIT","0",1);check(exclusive,"native one-event/cohort selector ambiguity was accepted");
    auto noise=input;std::mt19937_64 rng(771921);std::normal_distribution<double> normal(0,.002);for(auto& x:noise.pcm)x=static_cast<float>(normal(rng));
    actual_capability_tests(c,noise,s);
    const auto no=run_cohort(c,input,s,false),yes=run_cohort(c,input,s,true);exact_parity(no.result,yes.result);
    check(yes.result.bits==bits && yes.result.complete && yes.result.completed>=input.absent_end,"cohort lost exact payload/scored physical absence");
    const auto narrow_reference=run_cohort(c,noise,s,false),narrow_actual=run_cohort(c,noise,s,true);
    exact_parity(narrow_reference.result,narrow_actual.result);
    // Keep the exact-cell control above. This fixed unresolved clock/origin
    // case also reaches the native seam after the existing cold certificate.
    auto positive=s;positive.clock_errors_ppm={-600,0,600};
    positive.start_uncertainty_seconds=2.*modem::pattern_chip_samples(c)/c.sample_rate;
    const auto reference=run_cohort(c,noise,positive,false),actual=run_cohort(c,noise,positive,true);exact_parity(reference.result,actual.result);
    std::cout<<"cohort queries="<<actual.stats[2]<<" rejected="<<actual.stats[3]<<" requests="<<actual.stats[4]<<" unique="<<actual.stats[5]
        <<" reuse="<<actual.stats[6]<<" completed_native_rows="<<actual.stats[7]<<" peak="<<actual.result.peak<<'\n';
    check(actual.stats[0] && actual.stats[2]>1 && actual.stats[3] && actual.stats[6] && actual.stats[7] &&
          actual.stats[5]<actual.stats[4] && actual.result.bits.empty()&&!actual.result.complete,"actual cohort produced no deduplicated native rejection");
    Fault consume;consume.target=cohort::Phase::consume;const auto interrupted=run_cohort(c,noise,positive,true,false,&consume);check(consume.fired,"public preconsume native cancellation never fired");exact_parity(reference.result,interrupted.result);
    const auto committed=run_cohort(c,noise,positive,true,false,nullptr,true);exact_parity(reference.result,committed.result);
    auto narrow=s;narrow.frequency_offsets_hz={0};auto fp64=c;fp64.search_arithmetic=modem::SearchArithmetic::fp64;
    const auto fp64_result=run_cohort(fp64,input,narrow,true),raw_result=run_cohort(c,input,narrow,true,true);
    check(fp64_result.stats[0]==0 && raw_result.stats[0]==0 && fp64_result.result.bits==bits && raw_result.result.bits==bits &&
          fp64_result.result.complete && raw_result.result.complete,"FP64/raw reference policy entered cohort or lost actual Original reception");
}
// Pure core numeric/exporter tests are appended below before main.
}
namespace {
bool same_bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
void contains(const proxy::color::FitIntervals& a,const proxy::Fit& b){const auto has=[](auto x,double y){return x.lo<=y && y<=x.hi;};
    check(proxy::color::finite(a) && a.count==b.count && has(a.xc,b.xc)&&has(a.xs,b.xs)&&has(a.cc,b.cc)&&has(a.ss,b.ss)&&has(a.cs,b.cs)&&has(a.energy,b.energy),
          "cohort omitted an executed Original field");}
modem::Config numeric_config(std::uint64_t chip){modem::Config c;c.sample_rate=chip==8?14400:6120;c.carrier_hz=chip==8?3600:1500;c.bandwidth_hz=chip==8?360:36;
    c.dsss=true;c.dsss_factor=10;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;c.pattern_symbols=true;c.constellation_bits=1;
    c.scramble=true;c.data_key.emplace(Bytes(32,0x61));c.integration_seconds=std::nextafter(static_cast<double>(200*chip+13)/c.sample_rate,0.);
    for(std::size_t n=0;n<c.spreading_seed.size();++n){c.spreading_seed[n]=3*n+7;c.dsss_seed[n]=5*n+11;}
    check(modem::pattern_chip_samples(c)==chip && modem::symbol_sample_count(c)%chip,"numeric C8/C34 partial-tail geometry changed");return c;}
cohort::Context source_context(modem::PatternCode& code,const modem::Config& c){cohort::Context x;x.lane.phase_lower=c.stream_phase_samples;
    code.set_stream_phase_samples(x.lane.phase_lower);x.address={code.resolved_symbol_address(0),code.resolved_symbol_address(code.chips_per_symbol())};return x;}
void dedup_numeric(std::uint64_t chip){const auto c=numeric_config(chip);modem::PatternCode code(c,c.stream_epoch),original(c,c.stream_epoch);
    auto window=std::make_unique<cohort::Window>();window->reset();auto raw=std::make_unique<cohort::RawScratch>();std::array<cohort::Member,2> member{};
    for(unsigned event=0;event<2;++event){member[event].context=source_context(code,c);member[event].context.lane.frequency=event;member[event].request_first=window->count;
        proxy::Projection prefix{};auto oscillator=std::polar(1.,.21+.09*event);const auto step=std::polar(1.,.317+.043*event);
        for(unsigned n=0;n<32;++n){const auto at=103.375+n*1.00001,gain=event?.65+.08*std::sin(.071*n):.25+.06*std::cos(.031*n);
            check(window->add(event,member[event].context,{n,static_cast<std::uint64_t>(at/code.chip_samples()),0,at,gain,false},code.symbol_samples(),code.chip_samples()),"dedup request rejected");
            const auto x=static_cast<double>(static_cast<float>(.002*std::sin(.731*(n+7*event)))),co=oscillator.real(),si=oscillator.imag();auto next=prefix;
            next.xc+=x*co;next.xs+=x*si;next.cc+=co*co;next.ss+=si*si;next.cs+=co*si;next.energy+=x*x;
            window->row[window->count-1].p=next-prefix;prefix=next;oscillator*=step;}
        member[event].request_end=window->count;}
    check(window->count==64 && window->unique_count==32 && window->reuses==32,"carrier/gain differences prevented exact raw sharing");
    check(raw->evaluate(*window,code,c,{}),"deduplicated native source unavailable");std::array<proxy::Pair,32> saved{};
    for(std::size_t i=0;i<saved.size();++i)saved[i]=window->unique[i].raw;
    constexpr double amplitude=.05;
    for(unsigned event=0;event<2;++event){std::array<proxy::Fit,2> expected{};original.set_stream_phase_samples(c.stream_phase_samples);original.prepare_symbol(0);
        for(auto i=member[event].request_first;i<member[event].request_end;++i){const auto& request=window->row[i];const auto& source=window->unique[request.unique];
            auto q=original.shaped_values(0,std::bit_cast<double>(source.key.argument));for(auto& z:q)z=request.gain*modem::pattern_limit_pcm(amplitude*z)/amplitude;
            for(unsigned bit=0;bit<2;++bit){proxy::Fit contribution;contribution.add(request.p,q[bit],1);proxy::add_fit(expected[bit],contribution);}}
        check(cohort::scatter(member[event],*window,amplitude,{}),"qualified dedup scatter unavailable");
        for(unsigned bit=0;bit<2;++bit)contains(member[event].state.current.whole.budget.enclose(member[event].state.current.whole.fit[bit]),expected[bit]);}
    for(std::size_t i=0;i<saved.size();++i)for(unsigned bit=0;bit<2;++bit)check(same_bits(saved[i][bit].real(),window->unique[i].raw[bit].real()) &&
        same_bits(saved[i][bit].imag(),window->unique[i].raw[bit].imag()),"scatter mutated shared raw q");
    check(!same_bits(window->row[0].p.xc,window->row[32].p.xc) && window->row[0].gain!=window->row[32].gain,"independent carrier/gain operands collapsed");
    // A carry source and another event's current source can be identical;
    // current private symbol0 remains a distinct full resolved address.
    window->reset();const auto a=source_context(code,c);auto b=a;b.index=1;b.address[0]=a.address[1];
    check(window->add(0,a,{0,12,0,103.375,.5,false},code.symbol_samples(),code.chip_samples()) &&
          window->add(0,a,{1,12,code.chips_per_symbol(),103.375,.7,true},code.symbol_samples(),code.chip_samples()) &&
          window->add(1,b,{2,12,code.chips_per_symbol(),103.375,.9,false},code.symbol_samples(),code.chip_samples()),"crossindex source requests rejected");
    check(window->unique_count==2 && window->reuses==1 && raw->evaluate(*window,code,c,{}),"crossindex/carry full identity was not preserved");
    for(std::size_t i=0;i<window->unique_count;++i){const auto& u=window->unique[i];original.set_stream_phase_samples(u.phase);original.prepare_symbol(u.target_chip);
        const auto q=original.shaped_values(u.target_chip,std::bit_cast<double>(u.key.argument));for(unsigned bit=0;bit<2;++bit)check(
            std::abs(q[bit].real()-u.raw[bit].real())<=u.radius && std::abs(q[bit].imag()-u.raw[bit].imag())<=u.radius,"crossindex raw certificate omitted Original");}
}
void capacity_and_splits(){const auto c=numeric_config(8);modem::PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
    auto window=std::make_unique<cohort::Window>();window->reset();auto raw=std::make_unique<cohort::RawScratch>();const auto ctx=source_context(code,c);
    for(std::size_t n=0;n<64;++n){const auto at=(1.+3.*n)*code.chip_samples()+.125;
        check(window->add(0,ctx,{n,static_cast<std::uint64_t>(at/code.chip_samples()),0,at,1,false},code.symbol_samples(),code.chip_samples()),"sparse source request rejected");}
    check(raw->evaluate(*window,code,c,{}) && raw->splits>0 && raw->native_tiles>1 && raw->native_rows==64,"96-coefficient span did not split");
    reference.set_stream_phase_samples(c.stream_phase_samples);reference.prepare_symbol(0);
    for(std::size_t i=0;i<window->unique_count;++i){const auto& u=window->unique[i];check(u.valid,"split lost a unique requested row");
        const auto q=reference.shaped_values(0,std::bit_cast<double>(u.key.argument));for(unsigned bit=0;bit<2;++bit)check(
            std::abs(q[bit].real()-u.raw[bit].real())<=u.radius && std::abs(q[bit].imag()-u.raw[bit].imag())<=u.radius,"split source enclosure omitted Original");}
    window->reset();std::vector<double> bucket(cohort::cells,std::numeric_limits<double>::quiet_NaN());double left=0,right=0;bool found=false;
    for(std::size_t n=0;n<=cohort::cells && !found;++n){const auto at=(n+.25)/8;const cohort::Key key{ctx.address[0],code.symbol_samples(),code.chip_samples(),std::bit_cast<std::uint64_t>(at)};
        auto& previous=bucket[cohort::hash(key)&(cohort::cells-1)];if(std::isnan(previous))previous=at;else{left=previous;right=at;found=true;}}
    check(found && !same_bits(left,right),"bounded collision fixture found no distinct full keys");
    for(const auto at:{left,right,left})check(window->add(0,ctx,{0,0,0,at,.5,false},code.symbol_samples(),code.chip_samples()),"collision request rejected");
    check(window->unique_count==2 && window->reuses==1 && window->collisions>0,"hash collision aliased distinct full keys");
    const auto key=window->unique[0].key;for(unsigned field=0;field<6;++field){auto changed=key;if(field==0)++changed.address.epoch;else if(field==1)++changed.address.ordinal;
        else if(field==2)++changed.address.sample_in_second;else if(field==3)++changed.symbol;else if(field==4)++changed.chip;else changed.argument^=1;
        check(!(changed==key),"full key equality omitted an identity field");}
    window->reset();for(std::size_t n=0;n<cohort::requests;++n)check(window->add(n/cohort::physical_rows,ctx,{n,0,0,(n+.25)/4,1,false},code.symbol_samples(),code.chip_samples()),"2048-row capacity was shortened");
    check(window->count==2048 && window->unique_count==2048 && !window->add(0,ctx,{0,0,0,.125,1,false},code.symbol_samples(),code.chip_samples()) &&
          window->count==2048 && window->unique_count==2048,"capacity failure mutated complete bounded request table");
}
// Independent scalar traversal of one full Original fence, without Exporter.
void scalar_fence(const cohort::Context& c,const modem::SchedHopSourceMap& map,std::uint64_t first,std::uint64_t end,std::uint64_t symbol,
                  std::uint64_t chip,std::uint64_t chips,std::vector<cohort::SourceRow>& out,std::size_t& nulls,std::size_t& ramps){
    auto observed=first;while(observed<end){const auto tick=modem::SchedHopCoordinate{c.epoch_sample,0}.add_samples(c.lane.phase_lower).
        add_signed((static_cast<long double>(observed)-c.lane.origin)*c.lane.rate);const auto location=map.locate(tick);
        if(!location.active){++nulls;const auto next=map.source_to_physical(location.source,false);observed+=static_cast<std::uint64_t>(std::min(
            std::max(1.L,std::ceil(next.distance(tick)/c.lane.rate)),static_cast<long double>(end-observed)));continue;}
        const auto count=std::max(1.L,std::ceil(modem::SchedHopCoordinate{location.physical_end,0}.distance(tick)/c.lane.rate));
        const auto stop=observed+static_cast<std::uint64_t>(std::min(count,static_cast<long double>(end-observed))),piece_first=observed;
        const auto source=location.source.distance(c.lane.anchor)-c.symbol_source;
        for(;observed<stop;++observed){auto at=source+static_cast<long double>(observed-piece_first)*c.lane.rate;if(at<0)continue;
            check(at<2.L*symbol,"scalar fixture exceeded adjacent carry");const bool next=at>=symbol;if(next)at-=symbol;
            const auto physical=tick.add_signed(static_cast<long double>(observed-piece_first)*c.lane.rate);const auto gain=modem::SchedHopSourceMap::gain(location,physical);
            if(gain==0)continue;if(gain<1)++ramps;out.push_back({observed,static_cast<std::uint64_t>(at/chip),(c.index+(next?1:0))*chips,static_cast<double>(at),gain,next});}}
}
void exporter_fences(){for(const auto chip:{8ULL,34ULL})for(const auto rate:{.999988L,1.000012L}){auto c=numeric_config(chip);auto plan=std::make_shared<modem::SchedHopPlan>();
    plan->time_multiplier=3;plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;plan->ramp_seconds=.01;c.scheduled_hopping=plan;
    modem::SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate),oracle=map;
    const auto symbol=modem::symbol_sample_count(c);
    const auto chips=symbol/chip+(symbol%chip!=0);
    const auto base=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source.add_samples(1),false);const auto loc=map.locate(base);check(loc.active,"export fixture initial group inactive");
    const modem::SchedHopCoordinate anchor{loc.physical_end-std::min(symbol/2,(loc.physical_end-loc.physical_begin)/2),0};
    cohort::Context ctx;ctx.epoch_sample=anchor.sample/c.sample_rate*c.sample_rate;ctx.lane.phase_lower=anchor.sample%c.sample_rate;
    ctx.lane.anchor=map.locate(anchor).source;ctx.lane.origin=.375L;ctx.lane.rate=rate;
    const auto endpoint=map.source_to_physical(ctx.lane.anchor.add_samples(7*symbol/4),false);
    ctx.end=ctx.limit=static_cast<std::uint64_t>(std::ceil(ctx.lane.origin+endpoint.distance(anchor)/rate));
    std::vector<cohort::SourceRow> expected,actual;std::size_t nulls=0,ramps=0,sequence=0;const std::array<std::uint64_t,5> fences{113,306,7,512,191};
    for(std::uint64_t first=0;first<ctx.end;){const auto end=std::min(ctx.end,first+fences[sequence++%fences.size()]);
        scalar_fence(ctx,oracle,first,end,symbol,chip,chips,expected,nulls,ramps);cohort::Exporter exporter;exporter.begin(first,end);
        for(auto window=first;window<end;window+=cohort::physical_rows)check(exporter.visit(ctx,map,std::min(end,window+cohort::physical_rows),symbol,chip,chips,
            [](double gain,const auto&){return gain;},[&](const cohort::SourceRow& row){actual.push_back(row);return true;}),"fractional export unavailable");
        check(exporter.observed==end,"physical64 yield lost full Original fence completion");first=end;}
    check(actual.size()==expected.size() && nulls>0 && ramps>0,"null/ramp exporter coverage missing");bool current=false,carry=false;
    for(std::size_t n=0;n<actual.size();++n){const auto& a=actual[n];const auto& b=expected[n];current|=!a.carry;carry|=a.carry;
        check(a.observed==b.observed && a.owner==b.owner && a.target_chip==b.target_chip && a.carry==b.carry && same_bits(a.argument,b.argument) && same_bits(a.gain,b.gain),
              "physical64 export changed Original argument/gain/long-double ownership");}check(current&&carry,"fractional export omitted current/carry");}}
}
int main(){try{if(!cohort::environment() || !Access::qualified()){std::cout<<"qualified native/reference arithmetic unavailable\n";return 77;}
    dedup_numeric(8);dedup_numeric(34);capacity_and_splits();exporter_fences();actual_runtime_tests();
    std::cout<<"native cohort Original preservation passed sizeofBuild="<<sizeof(cohort::Build)<<" sizeofBatch="<<sizeof(cohort::Batch)
        <<" builderCallee="<<cohort::Build::callee_bytes<<" savedCaller="<<cohort::Batch::caller_bytes<<'\n';
    unsetenv("DATAPUMP_SCHED_NATIVE_COHORT");unsetenv("DATAPUMP_SCHED_NATIVE_COHERENT_PREFIT");unsetenv("DATAPUMP_SCHED_COHERENT_PREFIT");
    unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");unsetenv("DATAPUMP_SCHED_LAZY_BATCH");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
