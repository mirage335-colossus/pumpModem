// Actual mapped implementation regression. Root owns compilation/execution.
#include "../src/pattern_correlator.cpp"
#include <iostream>
#include <stdexcept>
#include <random>
#include <set>

namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using State=PatternCorrelator::Impl;
    using Scratch=detail::ScheduledPrivateBatchScratch;
    static void check(bool v,const char* m){if(!v)throw Error(m);}
    static bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
    static void same(const Fit& a,const Fit& b) {
        check(bits(a.xc,b.xc)&&bits(a.xs,b.xs)&&bits(a.cc,b.cc)&&bits(a.ss,b.ss)&&bits(a.cs,b.cs)&&
            bits(a.energy,b.energy)&&a.count==b.count,"public batch changed original ordered Fit");
    }
    static Config config() {
        Config c;c.sample_rate=6120;c.carrier_hz=1500;c.bandwidth_hz=36;c.integration_seconds=std::nextafter(87040./6120,0.);
        c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.dsss_factor=10;c.dsss=true;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
        c.data_key.emplace(Bytes(32,0x61));
        const auto p=c.data_key->stream(StreamPurpose::Scrambler,0,0,32),d=c.data_key->stream(StreamPurpose::Dsss,0,0,32);
        std::copy(p.begin(),p.end(),c.spreading_seed.begin());std::copy(d.begin(),d.end(),c.dsss_seed.begin());
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=9;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto first=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        const auto location=map.locate(first);const auto midpoint=location.physical_begin+map.source_samples_per_group()/2;
        c.stream_epoch=midpoint/c.sample_rate;c.stream_phase_samples=midpoint%c.sample_rate;return c;
    }
    static PatternSearch search() {
        PatternSearch s;s.start_offset_seconds=0;s.start_uncertainty_seconds=0;s.search_stream_phases=true;
        s.frequency_offsets_hz={0,.13};s.clock_errors_ppm={0};s.worker_threads=1;s.drift_tolerant=false;
        s.bit_limit=4;s.chunk_bits=1;s.track_limit=32;s.candidate_limit=256;s.retain_score=0;return s;
    }
    static void fill(State& s,std::span<const float> pcm) {
        for(auto& bank:s.banks) {
            bank.prefix[0]={};
            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(
                static_cast<long double>(s.sample)*tau*bank.frequency/s.config.sample_rate,static_cast<long double>(tau))));
            const auto step=std::polar(1.,tau*bank.frequency/s.config.sample_rate);
            for(std::size_t n=0;n<pcm.size();++n) {
                const auto x=static_cast<double>(pcm[n]),c=oscillator.real(),q=oscillator.imag();auto p=bank.prefix[n];
                p.xc+=x*c;p.xs+=x*q;p.cc+=c*c;p.ss+=q*q;p.cs+=c*q;p.energy+=x*x;bank.prefix[n+1]=p;oscillator*=step;
            }
        }
    }
    static void states(const State& a,const State& b) {
        check(a.hypotheses.size()==b.hypotheses.size()&&a.trials==b.trials,"public batch changed bank/trials");
        for(std::size_t i=0;i<a.hypotheses.size();++i) {
            for(unsigned bit=0;bit<2;++bit) {
                same(a.hypotheses[i].fits[bit],b.hypotheses[i].fits[bit]);
                same(a.scheduled->scheduled_carry[i].fits[bit],b.scheduled->scheduled_carry[i].fits[bit]);
                if(!a.chip_evidence.empty()) {
                    const auto compare=[](const auto& x,const auto& y){same(x.cell,y.cell);
                        check(bits(x.projected_energy,y.projected_energy)&&x.projected_rank==y.projected_rank&&
                            x.cell_index==y.cell_index&&x.nested==y.nested,"public batch changed chip guard");};
                    compare(a.chip_evidence[i][bit],b.chip_evidence[i][bit]);
                    compare(a.scheduled->scheduled_carry_chips[i][bit],b.scheduled->scheduled_carry_chips[i][bit]);
                }
            }
            if(a.outer_presence) {
                const auto compare=[](const auto& x,const auto& y){check(x.first==y.first&&x.last==y.last&&
                    x.initialized==y.initialized&&x.pulse==y.pulse&&x.dot==y.dot&&x.variance==y.variance&&
                    x.contributing_terms==y.contributing_terms,"public batch changed independent outer ring");};
                compare((*a.outer_evidence)[i],(*b.outer_evidence)[i]);
                compare(a.scheduled->scheduled_carry_outer[i],b.scheduled->scheduled_carry_outer[i]);
            }
        }
    }
    static void test_case(bool families,bool ordered=false) {
        const bool arithmetic=Scratch::Tile::arithmetic_supported();
        check(arithmetic||(!families&&!ordered),"qualified family batching unavailable for this arithmetic");
        auto c=config();PatternCode code(c,c.stream_epoch);
        check(code.chip_samples()==34&&code.symbol_samples()==87040,"intended C34/N87040 geometry changed");
        auto options=PatternCorrelatorOptions{};options.scheduled_family_experiment=options.scheduled_common_shape_experiment=families;
        PatternCorrelator a(c,search(),32*1024*1024,options),b(c,search(),32*1024*1024,options);
        auto& x=*a.impl_;auto& y=*b.impl_;check(x.hypotheses.size()>=4,"fixture has no independent cohort");
        if(families){x.prepare_workers();y.prepare_workers();check(x.scheduled->families&&y.scheduled->families&&
            x.scheduled->common_scratch&&y.scheduled->common_scratch,"original common family did not qualify");}
        check(x.outer_presence&&y.outer_presence,"outer-ring protection fixture became vacuous");
        // Conditional finite prefix, not a complete transmission or omission
        // certificate. Both references start at the SAME actual capture time.
        x.sample=y.sample=4096;
        std::array<float,512> pcm{};for(std::size_t n=0;n<pcm.size();++n)pcm[n]=static_cast<float>(.31*std::sin(.031*n)+.13*std::cos(.271*n));
        fill(x,pcm);fill(y,pcm);auto end=x.sample+pcm.size();
        auto scratch=std::make_unique<Scratch>();
        const auto original_one=[&](State& state,std::size_t i,const detail::ScheduledPrivateBatchView* view) {
            auto& h=state.hypotheses[i];const auto cursor=state.initial_cursor(h,end);
            if(cursor<end && static_cast<long double>(cursor)<state.clock_boundary(h,code.symbol_samples()))
                state.accumulate_scheduled(h,i,cursor,end,state.code,{},view);
        };
        std::set<std::pair<std::uint64_t,std::uint64_t>> consumed_identities;
        const auto execute=[&](State& state,std::size_t item,const detail::ScheduledPrivateBatchView* view) {
            const auto consumed_before=view && view->consumed?*view->consumed:0;
            if(!families){original_one(state,item,view);
                if(view && view->consumed && *view->consumed>consumed_before)
                    consumed_identities.emplace(state.hypotheses[item].phase_lower,state.hypotheses[item].index);
                return;}
            const auto& plan=*state.scheduled->families;
            const auto members=std::span<const std::size_t>{plan.members}.subspan(plan.ends[item],plan.ends[item+1]-plan.ends[item]);
            const bool shared=state.accumulate_scheduled_common_shape(members,end,state.code,0,item,{},view);
            if(!shared)for(auto i:members)original_one(state,i,i==members.front()?view:nullptr);
            if(view && view->consumed && *view->consumed>consumed_before)
                consumed_identities.emplace(state.hypotheses[members.front()].phase_lower,state.hypotheses[members.front()].index);
        };
        const auto items=families?x.scheduled->families->ends.size()-1:x.hypotheses.size();
        for(std::size_t i=0;i<items;++i)execute(x,i,nullptr);
        auto order_owner=ordered?y.prepare_private_batch_owner(pcm.size()):std::unique_ptr<State::PrivateBatchOwner>{};
        if(ordered) {
            check(bool(order_owner),"optional ordered family owner did not fit");
            y.prepare_private_batch_order(*order_owner,y.scheduled->families.get(),end,{});
            check(order_owner->order.size()==items&&order_owner->order_eligible>0,"actual coordinate-order prepass did not qualify");
            const auto loan=order_owner->bytes,accounted=y.accounted_bytes,requests=order_owner->order_requests;
            y.prepare_private_batch_order(*order_owner,y.scheduled->families.get(),end,{});
            check(order_owner->bytes==loan&&y.accounted_bytes==accounted&&order_owner->order_requests==requests,
                "repeated ordering preparation double-charged its temporary workspace");
            std::set<std::size_t> unique;
            for(const auto& row:order_owner->order)check(row.item<items&&unique.insert(row.item).second,"ordering duplicated/dropped a family");
        }
        const auto original_item=[&](std::size_t rank){return order_owner?order_owner->order[rank].item:rank;};
        const auto consume=[&](std::size_t i,const detail::ScheduledPrivateBatchView* view){execute(y,original_item(i),view);};
        const auto select=[&](std::size_t rank){const auto item=original_item(rank);
            return families?y.scheduled->families->members[y.scheduled->families->ends[item]]:item;};
        const auto group=families?Scratch::Tile::maximum_lanes:Scratch::job_lanes;
        for(std::size_t i=0;i<items;i+=group)y.accumulate_private_batch_job(i,std::min(items,i+group),end,
            y.code,*scratch,select,consume,{},families);
        states(x,y);
        order_owner.reset();
        if(ordered) {
            // Reuse warmed ORIGINAL family proof slots across a second capture
            // block. Dispatch ranks are deliberately reordered again.
            x.sample=y.sample=end;end+=pcm.size();fill(x,pcm);fill(y,pcm);
            for(std::size_t i=0;i<items;++i)execute(x,i,nullptr);
            order_owner=y.prepare_private_batch_owner(pcm.size());check(bool(order_owner),"warm ordering loan unavailable");
            y.prepare_private_batch_order(*order_owner,y.scheduled->families.get(),end,{});
            for(std::size_t i=0;i<items;i+=group)y.accumulate_private_batch_job(i,std::min(items,i+group),end,
                y.code,*scratch,select,consume,{},families);
            check(y.scheduled->common_work[0].phase_address_hits>0,"ordered test did not exercise warmed original phase proof identities");
            states(x,y);order_owner.reset();
        }
        if(arithmetic) {
            check(scratch->counts.batched_lanes>=2 && scratch->counts.consumed_samples>=2*pcm.size(),
                "actual mapped-path public batch did not consume independent source rows");
            check(consumed_identities.size()>=2,"batched rows were only duplicate frequencies of one private identity");
            check(scratch->counts.outer_rows>=2*pcm.size(),"private batch discarded its exact public outer pulse plan");
            if(families)check(y.scheduled->common_work[0].families>0,"new family dispatch bypassed original common execution");
            check(scratch->counts.plan_fallbacks==0&&scratch->counts.numeric_fallbacks==0,"positive batch fell back");
        } else {
            check(scratch->counts.batched_lanes==0&&scratch->counts.prepared_samples==0&&
                scratch->counts.consumed_samples==0&&scratch->counts.outer_rows==0&&consumed_identities.empty(),
                "unsupported arithmetic consumed prepared private rows");
            check(std::any_of(y.hypotheses.begin(),y.hypotheses.end(),[](const auto& h){return h.fits[0].count||h.fits[1].count;}),
                "unsupported-arithmetic fallback parity had no actual observations");
        }
        check(x.bursts.empty()&&y.bursts.empty(),"partial prefix manufactured admission or END");
        const auto before=y.accounted_bytes;auto owner=y.prepare_private_batch_owner(pcm.size());
        if(arithmetic)check(owner&&owner->bytes>sizeof(Scratch)&&y.accounted_bytes==before+owner->bytes,"optional batch loan absent or uncharged");
        else check(!owner&&y.accounted_bytes==before,"unsupported arithmetic acquired an optional batch loan");
        owner.reset();check(y.accounted_bytes==before,"joined batch loan was not returned");
        const auto original_budget=y.budget;y.budget=sizeof(PatternCorrelator)+y.accounted_bytes+8;
        check(!y.prepare_private_batch_owner(pcm.size()),"required banks displaced by optional batch");y.budget=original_budget;
        const auto original_precision=y.config.search_arithmetic;y.config.search_arithmetic=SearchArithmetic::fp64;
        check(!y.prepare_private_batch_owner(pcm.size()),"FP64force admitted public batch");y.config.search_arithmetic=original_precision;
        std::stop_source cancel;cancel.request_stop();bool stopped=false;
        try{y.accumulate_private_batch_job(0,items,end,y.code,*scratch,select,consume,cancel.get_token(),families);}
        catch(const Error&){stopped=true;}check(stopped,"public batch cancellation ignored");states(x,y);
        std::cout<<"families="<<families<<" actual mapped private rows="<<scratch->counts.batched_lanes<<" source samples="<<scratch->counts.consumed_samples
            <<" scratch="<<sizeof(Scratch)<<" callee="<<Scratch::callee_bytes<<"; original Fits/chip/outer state identical\n";
    }
    // Actual common engine, including source Plan preparation and one-shot
    // outer tickets. The old exact private-batch assertions above stay exact;
    // this separate experiment permits the bounded P3 association change.
    static void frequency_source_case() {
        auto c=config();auto q=search();q.frequency_offsets_hz={0,-.002,-.001,.001,.002};
        auto options=PatternCorrelatorOptions{};options.scheduled_temporal_template_experiment=true;
        PatternCorrelator a(c,q,32*1024*1024,options),b(c,q,32*1024*1024,options);
        auto& x=*a.impl_;auto& y=*b.impl_;x.prepare_workers();y.prepare_workers();
        check(x.scheduled->families&&y.scheduled->families&&x.scheduled->frequency_piece_scratch&&
            y.scheduled->frequency_piece_scratch,"frequency source regression lacks original common/P3 engine");
        check(!y.scheduled->worker_temporal_templates.empty()&&y.scheduled->worker_temporal_templates[0],
            "frequency source regression lacks Temporal preparation");
        x.sample=y.sample=4096;std::array<float,16> pcm{};
        for(std::size_t n=0;n<pcm.size();++n)pcm[n]=static_cast<float>(.31*std::sin(.031*n)+.13*std::cos(.271*n));
        fill(x,pcm);fill(y,pcm);const auto end=x.sample+pcm.size();
        const auto execute=[&](State& state,std::size_t group) {
            const auto& p=*state.scheduled->families;
            const auto lanes=std::span<const std::size_t>{p.members}.subspan(p.ends[group],p.ends[group+1]-p.ends[group]);
            if(!state.accumulate_scheduled_common_shape(lanes,end,state.code,0,group,{}))for(auto i:lanes) {
                auto& h=state.hypotheses[i];const auto cursor=state.initial_cursor(h,end);
                if(cursor<end&&static_cast<long double>(cursor)<state.clock_boundary(h,state.code.symbol_samples()))
                    state.accumulate_scheduled(h,i,cursor,end,state.code,{});
            }
        };
        x.scheduled->frequency_piece_scratch=false;
        for(std::size_t g=0;g+1<x.scheduled->families->ends.size();++g)execute(x,g);
        x.scheduled->frequency_piece_scratch=true; // restore the still-reserved loan before any accounting/eviction
        std::size_t witnesses=0;const auto r1=detail::scheduled_r1_export_counts();
        for(std::size_t g=0;g+1<y.scheduled->families->ends.size();++g) {
            const auto before=y.scheduled->common_work[0];
            const auto copied=y.scheduled->worker_temporal_templates[0]->statistics.consumed;
            execute(y,g);const auto& after=y.scheduled->common_work[0];
            if(after.frequency_pieces>before.frequency_pieces&&after.raw_samples==before.raw_samples&&
               after.frequency_prepared_samples>before.frequency_prepared_samples&&
               y.scheduled->worker_temporal_templates[0]->statistics.consumed-copied==
                   after.frequency_prepared_samples-before.frequency_prepared_samples)++witnesses;
        }
        check(witnesses>0,"successful P3 bypassed preceding Temporal source acceleration");
        check(detail::scheduled_r1_export_counts().samples>r1.samples,"P3 lost preceding R1 export");
        const auto approximately_equal=[](double a,double b){return std::abs(a-b)<=1e-7*std::max({1.,std::abs(a),std::abs(b)});};
        const auto fit=[&](const Fit& a,const Fit& b) {
            check(a.count==b.count&&std::abs(a.energy-b.energy)<=1e-12*std::max({1.,a.energy,b.energy}),"P3 changed actual observations/noise energy");
            check(approximately_equal(a.xc,b.xc)&&approximately_equal(a.xs,b.xs)&&approximately_equal(a.cc,b.cc)&&approximately_equal(a.ss,b.ss)&&approximately_equal(a.cs,b.cs),
                "P3 real I/Q Fit differs beyond bounded association tolerance");
            check(approximately_equal(a.score(),b.score()),"P3 score differs beyond bounded association tolerance");
            for(double threshold:{1.,3.,10.,40.})check((a.score()>=threshold)==(b.score()>=threshold),
                "P3 changed fixed-threshold comparison");
        };
        check(x.trials==y.trials&&x.hypotheses.size()==y.hypotheses.size(),"P3 changed trial/coverage");
        for(std::size_t i=0;i<x.hypotheses.size();++i) {
            const auto& a=x.hypotheses[i];const auto& b=y.hypotheses[i];
            check(a.index==b.index&&a.observed_start==b.observed_start,"P3 changed symbol ownership");
            for(unsigned bit=0;bit<2;++bit) {
                fit(a.fits[bit],b.fits[bit]);fit(x.scheduled->scheduled_carry[i].fits[bit],y.scheduled->scheduled_carry[i].fits[bit]);
                if(!x.chip_evidence.empty()) {
                    const auto& ca=x.chip_evidence[i][bit];const auto& cb=y.chip_evidence[i][bit];fit(ca.cell,cb.cell);
                    check(ca.projected_rank==cb.projected_rank&&ca.cell_index==cb.cell_index&&ca.nested==cb.nested&&
                        approximately_equal(ca.projected_energy,cb.projected_energy),"P3 changed chip guard identity/energy");
                    check(ca.colored_excess(a.fits[bit],40)==cb.colored_excess(b.fits[bit],40),"P3 changed excess guard");
                }
            }
            const auto outer=[&](const auto& a,const auto& b) {
                check(a.first==b.first&&a.last==b.last&&a.initialized==b.initialized&&a.contributing_terms==b.contributing_terms,
                    "P3 changed original outer ring chronology");
                for(std::size_t k=0;k<a.pulse.size();++k)check(std::abs(a.pulse[k]-b.pulse[k])<=1e-7*std::max({1.,std::abs(a.pulse[k]),std::abs(b.pulse[k])}),
                    "P3 changed complete outer pulse evidence");
                for(unsigned bit=0;bit<2;++bit)check(approximately_equal(a.variance[bit],b.variance[bit])&&
                    std::abs(a.dot[bit]-b.dot[bit])<=1e-7L*std::max({1.L,std::abs(a.dot[bit]),std::abs(b.dot[bit])})&&
                    a.can_admit(bit,40)==b.can_admit(bit,40),"P3 changed outer evidence");
            };
            if(x.outer_presence){outer((*x.outer_evidence)[i],(*y.outer_evidence)[i]);
                outer(x.scheduled->scheduled_carry_outer[i],y.scheduled->scheduled_carry_outer[i]);}
        }
        check(x.bursts.empty()&&y.bursts.empty(),"bounded P3 fixture manufactured admission/END");
        std::cout<<"P3 actual source/outer engine witnesses="<<witnesses<<"; no full-bank sensitivity claim\n";
    }
    static void gather_policy() {
        check(Scratch::leader_group(147,9,0)==8,"adaptive gather lost two jobs per worker");
        check(Scratch::leader_group(4,9,0)==4,"adaptive gather widened a small bank");
        check(Scratch::leader_group(100000,9,0)==32,"adaptive gather exceeded/lost its upper bound");
        for(std::size_t items=1;items<=1024;++items)for(std::size_t workers=1;workers<=16;++workers) {
            const auto n=Scratch::leader_group(items,workers,0);
            check(n==4||n==8||n==16||n==32,"adaptive gather invalid size");
            if(n>4)check(items/n+(items%n!=0)>=2*workers,"adaptive gather starved independent jobs");
            for(const auto forced:{4U,8U,16U,32U})check(Scratch::leader_group(items,workers,forced)==forced,"diagnostic gather override lost");
        }
    }
    static void public_plan_provenance() {
        auto scratch=std::make_unique<Scratch>();std::array<double,16> coordinates{};
        for(std::size_t n=0;n<coordinates.size();++n)coordinates[n]=500.+n;
        check(scratch->plan.build_original(coordinates,87040,34,scratch->tile.statistics),"public Plan fixture unavailable");
        check(scratch->plan.well_formed(),"public Plan fixture malformed");
        detail::ScheduledPrivateBatchView view{&scratch->tile,0,100,116,87040,840,1,false,nullptr,
            &scratch->plan,10,nullptr,coordinates,34};
        for(std::size_t n=0;n<coordinates.size();++n) {
            std::vector<std::pair<std::uint64_t,double>> original,replayed;
            pattern_pulse_each(840.+n,87040,34,[&](auto i,auto w){original.emplace_back(i,w);});
            check(view.outer_pulses(100+n,840.L+n,[&](auto i,auto w){replayed.emplace_back(i,w);}),"exact public Plan provenance rejected");
            check(original==replayed,"translated public Plan changed original pulse labels or weights");
        }
        unsigned callbacks=0;const auto consume=[&](auto,auto){++callbacks;};
        auto changed=coordinates;changed[0]=std::nextafter(changed[0],INFINITY);auto wrong=view;
        wrong.public_coordinates=changed;
        check(!wrong.outer_pulses(100,840,consume)&&callbacks==0,"near-coordinate Plan replay mutated a guard before fallback");
        wrong=view;wrong.chip_samples=35;
        check(!wrong.outer_pulses(100,840,consume)&&callbacks==0,"wrong-chip Plan replay reached a guard");
        wrong=view;wrong.translation=UINT64_MAX;
        check(!wrong.outer_pulses(100,840,consume)&&callbacks==0,"overflowing Plan replay reached a guard");
        const auto row=scratch->plan.rows[0];scratch->plan.rows[0].nonzero|=1U<<31;
        check(!view.outer_pulses(100,840,consume)&&callbacks==0,"out-of-row public Plan mask reached a guard");
        scratch->plan.rows[0]=row;
        bool propagated=false;try{view.outer_pulses(100,840,[&](auto,auto){throw Error("callback exception");});}
        catch(const Error&){propagated=true;}check(propagated,"public Plan swallowed a partial-consumption exception");
    }
    static void run(){gather_policy();public_plan_provenance();test_case(false);
        if(Scratch::Tile::arithmetic_supported()){test_case(true);test_case(true,true);frequency_source_case();}
        else std::cout<<"qualified family/P3 acceleration unavailable; mapped fallback and public Plan checks passed\n";
    }
};
}
int main(){
#if defined(_WIN32)
 _putenv_s("DATAPUMP_SCHED_PRIVATE_BATCH","4");
 _putenv_s("DATAPUMP_SCHED_PRIVATE_ORDER","coordinates");
 _putenv_s("DATAPUMP_SCHED_FREQUENCY_PIECE","1");
 _putenv_s("DATAPUMP_SCHED_R1_EXPORT","1");
 _putenv_s("DATAPUMP_SCHED_FLAT_EXPORT","1");
#else
 setenv("DATAPUMP_SCHED_PRIVATE_BATCH","4",1);
 setenv("DATAPUMP_SCHED_PRIVATE_ORDER","coordinates",1);
 setenv("DATAPUMP_SCHED_FREQUENCY_PIECE","1",1);
 setenv("DATAPUMP_SCHED_R1_EXPORT","1",1);
 setenv("DATAPUMP_SCHED_FLAT_EXPORT","1",1);
#endif
 try{datapump::modem::PatternCorrelatorTimingTestAccess::run();}
 catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
