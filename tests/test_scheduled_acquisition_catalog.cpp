#include "datapump/scheduled_complex_search.hpp"
#include "datapump/tuning.hpp"
#include "datapump/clock_sync.hpp"
#include "../src/scheduled_acquisition_catalog.hpp"
#include "resource_test_margin.hpp"

#include <iostream>
#include <map>
#include <set>
#include <vector>

using namespace datapump;
using namespace datapump::modem;
using Catalog=datapump::modem::detail::ScheduledAcquisitionCatalog;

namespace {
void check(bool ok,const char* message){if(!ok)throw Error(message);}
template<class F>void rejects(F run,const char* message) {
    bool rejected=false;try{run();}catch(const Error&){rejected=true;}check(rejected,message);
}
Config small() {
    Config c;c.sample_rate=200;c.bandwidth_hz=20;c.carrier_hz=40;c.pattern_symbols=true;
    c.spreading_mode=SpreadingMode::pattern;c.spreading_factor=64;c.scramble=true;
    c.data_key.emplace(Bytes(32,0x67));c.stream_epoch=1800000000;
    auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=1;
    c.scheduled_hopping=plan;return c;
}
PatternSearch search() {
    PatternSearch s;s.start_offset_seconds=.031;s.start_uncertainty_seconds=.31;
    s.search_stream_phases=true;s.hypotheses={{0,0},{.01,-1000},{-.01,1000},{0,0}};return s;
}
PatternScheduledComplexGeometry active(Config& c,const PatternSearch& s) {
    for(unsigned i=0;i<40;++i,++c.stream_epoch) {
        auto g=pattern_scheduled_complex_geometry(c,s);
        check(g.supported,"oracle split geometry unsupported");
        if(g.phase_positions)return g;
    }
    throw Error("oracle did not find active epoch");
}
struct Original {
    std::size_t pair=0,origin_index=0,frequency=0,rate_index=0,tone_bank_base=0;
    long double origin=0,rate=1;
    std::uint64_t phase=0;
};
// Independent original constructor loops. Deliberately do NOT call the catalog
// rank, interval, source-index, boundary, or phase-cell helpers as an oracle.
template<class Consumer>std::size_t original(const Config& c,const PatternSearch& s,
        const PatternScheduledComplexGeometry& g,Consumer consume) {
    const bool paired=!s.hypotheses.empty();std::vector<double> f;
    long double highest=1;
    if(paired)for(const auto& p:s.hypotheses) {
        highest=std::max(highest,1+static_cast<long double>(p.clock_error_ppm)*1e-6L);
        if(std::find(f.begin(),f.end(),p.frequency_offset_hz)==f.end())f.push_back(p.frequency_offset_hz);
    } else {
        f=s.frequency_offsets_hz;
        if(f.empty()){const auto step=.25*c.sample_rate/static_cast<double>(symbol_sample_count(c));f={0,-step,step,-2*step,2*step};}
        for(const auto ppm:s.clock_errors_ppm)highest=std::max(highest,1+static_cast<long double>(ppm)*1e-6L);
    }
    const auto pair_count=paired?s.hypotheses.size():f.size()*s.clock_errors_ppm.size();
    const auto lower=(static_cast<long double>(*s.start_offset_seconds)-s.start_uncertainty_seconds)*c.sample_rate;
    const auto upper=(static_cast<long double>(*s.start_offset_seconds)+s.start_uncertainty_seconds)*c.sample_rate;
    const auto phase_step=g.receiver_phase_step_samples;
    const auto phase_low=s.search_stream_phases&&(c.scramble||c.dsss)?0:c.stream_phase_samples;
    const auto phase_high=s.search_stream_phases&&(c.scramble||c.dsss)?
        (canonical_phase_extent(c)-1)/phase_step*phase_step:c.stream_phase_samples;
    const auto epoch=c.stream_epoch*c.sample_rate;
    SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges;
    auto cursor=SchedHopCoordinate{epoch,0}.add_samples(phase_low);
    const auto last=SchedHopCoordinate{epoch,0}.add_samples(phase_high);
    while(cursor.distance(last)<=0) {
        const auto group=map.locate(cursor);
        if(!group.active) {
            const auto next=map.source_to_physical(group.source,false);check(next.distance(cursor)>0,"oracle inverse failed");
            cursor=next;if(cursor.distance(last)>0)break;continue;
        }
        const auto lo=std::max<std::uint64_t>(phase_low,group.physical_begin>epoch?group.physical_begin-epoch:0);
        const auto hi=std::min<std::uint64_t>(phase_high,group.physical_end-1-epoch);
        const auto first=phase_low+(lo-phase_low+phase_step-1)/phase_step*phase_step;
        const auto end=phase_low+(hi-phase_low)/phase_step*phase_step;
        if(first<=end)ranges.emplace_back(first,end);
        cursor={group.physical_end,0};
    }
    std::size_t id=0;
    for(std::size_t pair=0;pair<pair_count;++pair) {
        const auto ri=paired?pair:pair/f.size();
        const auto fi=paired?static_cast<std::size_t>(std::find(f.begin(),f.end(),s.hypotheses[pair].frequency_offset_hz)-f.begin()):pair%f.size();
        const auto ppm=paired?s.hypotheses[pair].clock_error_ppm:s.clock_errors_ppm[ri];
        const auto rate=1+static_cast<long double>(ppm)*1e-6L;
        const auto chip=pattern_chip_samples(c);
        const auto step=(paired?static_cast<long double>(chip)/(2*rate):std::max(1.L,std::floor(chip/(2*highest))))/g.receiver_timing_subdivisions;
        const auto origins=static_cast<std::size_t>(std::ceil((upper-lower)/step)+1);
        for(std::size_t i=0;i<origins;++i) {
            const auto at=std::min(upper,lower+static_cast<long double>(i)*step);
            auto pl=phase_low,ph=phase_high;
            if(s.qualified_start_window) {
                const auto previous=i?std::min(upper,lower+static_cast<long double>(i-1)*step):lower;
                const auto next=i+1<origins?std::min(upper,lower+static_cast<long double>(i+1)*step):upper;
                const auto cell_lo=(previous+at)/2,cell_hi=(at+next)/2;const auto& q=*s.qualified_start_window;
                const auto pad=64*std::numeric_limits<long double>::epsilon()*std::max({1.L,std::abs(cell_lo),std::abs(cell_hi),std::abs(q.epoch_origin_samples),q.half_width_samples});
                auto a=(cell_lo-q.epoch_origin_samples-q.half_width_samples-pad)/q.phase_scale;
                auto b=(cell_hi-q.epoch_origin_samples+q.half_width_samples+pad)/q.phase_scale;
                if(s.search_stream_phases)a-=phase_step-1;
                if(a>phase_high||b<phase_low)continue;
                a=std::clamp(a,static_cast<long double>(phase_low),static_cast<long double>(phase_high));
                b=std::clamp(b,static_cast<long double>(phase_low),static_cast<long double>(phase_high));
                const auto first=std::ceil((a-phase_low)/phase_step),end=std::floor((b-phase_low)/phase_step);
                if(first>end)continue;
                pl=phase_low+static_cast<std::uint64_t>(first)*phase_step;ph=phase_low+static_cast<std::uint64_t>(end)*phase_step;
            }
            for(const auto& range:ranges) {
                const auto lo=std::max(pl,range.first),hi=std::min(ph,range.second);if(lo>hi)continue;
                for(auto phase=lo;;phase+=phase_step) {
                    consume(id++,Original{pair,i,fi,ri,2*pair,at,rate,phase});if(phase==hi)break;
                }
            }
        }
    }
    return id;
}
long double original_boundary(const Config& c,const PatternSearch& s,const PatternScheduledComplexGeometry& g,
        const Catalog::Lane& lane,long double source,bool end) {
    SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto anchor=SchedHopCoordinate{c.stream_epoch*c.sample_rate,0}.add_samples(lane.phase_lower);
    const auto source_anchor=map.locate(anchor).source;
    const auto physical=map.source_to_physical(source_anchor.add_signed(source),end);
    const auto distance=physical.distance(anchor);
    const auto radius=g.clock_cell_radius_ppm*1e-6L;
    const auto timing=s.start_uncertainty_seconds>0?pattern_chip_samples(c)/(4.L*g.receiver_timing_subdivisions):0;
    auto result=lane.origin+distance/lane.rate;
    if(end)result=lane.origin+timing/lane.rate+std::max(distance/(lane.rate-radius),distance/(lane.rate+radius));
    const auto width=s.search_stream_phases?std::min(g.receiver_phase_step_samples-1,canonical_phase_extent(c)-1-lane.phase_lower):0;
    if(end&&width) {
        const auto upper=anchor.add_samples(width);const auto target=map.locate(upper).source.add_signed(source);
        const auto p=map.source_to_physical(target,true);const auto delta=p.distance(upper);
        result=std::max(result,lane.origin+timing/lane.rate+std::max(delta/(lane.rate-radius),delta/(lane.rate+radius)));
    }
    return result;
}
void compare(Config c,PatternSearch s,bool nonempty=true) {
    const auto g=active(c,s);Catalog catalog(c,s,g,2*1024*1024);auto cursor=catalog.make_cursor();
    if(nonempty)check(catalog.size()>0,"catalog oracle case accidentally has no retained lane");
    // Carrier labels may differ while their timing/calendars are identical.
    // Verify shared metadata as well as every independent original lane below.
    for(std::size_t p=0;p<catalog.pairs().size();++p) {
        const auto& pair=catalog.pairs()[p];const auto& shared=catalog.pairs()[pair.timing_pair];
        check(pair.timing_pair<=p && pair.rate==shared.rate && pair.step==shared.step &&
            pair.origins==shared.origins && pair.family_count==shared.family_count &&
            pair.lane_count==shared.lane_count,"exact-rate timing representative changed its domain");
        for(std::size_t f=0;f<pair.family_count;++f) {
            const auto& a=catalog.families()[pair.family_first+f];
            const auto& b=catalog.families()[shared.family_first+f];
            check(a.pair==p && a.phase==b.phase && a.range_first==b.range_first &&
                a.range_count==b.range_count && a.anchor.distance(b.anchor)==0,
                "frequency siblings duplicated or changed immutable calendar membership");
        }
    }
    std::map<std::pair<std::size_t,std::uint64_t>,std::size_t> previous;
    const auto count=original(c,s,g,[&](std::size_t id,const Original& expected) {
        const auto lane=catalog.describe(id,cursor);
        const auto key=std::pair{expected.pair,expected.phase};
        const auto prior=previous.find(key);
        if(prior==previous.end())check(!catalog.adjacent_origin(lane,false),"first original phase lane has an earlier neighbor");
        else {
            check(catalog.adjacent_origin(lane,false)==std::optional<std::size_t>{prior->second},
                "catalog earlier neighbor differs from independent original phase order");
            check(catalog.adjacent_origin(catalog.describe(prior->second,cursor),true)==std::optional<std::size_t>{id},
                "catalog later neighbor differs from independent original phase order");
        }
        previous[key]=id;
        check(lane.id==id&&lane.pair==expected.pair&&lane.origin_index==expected.origin_index&&lane.frequency==expected.frequency&&
            lane.rate_index==expected.rate_index&&lane.tone_bank_base==expected.tone_bank_base&&lane.origin==expected.origin&&
            lane.rate==expected.rate&&lane.phase_lower==expected.phase&&lane.phase_upper==expected.phase,"catalog changed original logical lane identity");
        check(catalog.rank(lane.pair,lane.origin_index,lane.phase_lower)==std::optional<std::size_t>{id},"catalog rank did not invert describe");
        const auto& pair=catalog.pairs()[lane.pair];
        auto family=pair.family_first;
        while(family<pair.family_first+pair.family_count && catalog.families()[family].phase!=lane.phase_lower)++family;
        const auto direct=catalog.describe_family(family,lane.origin_index,cursor);
        check(direct.id==lane.id && direct.pair==lane.pair && direct.origin_index==lane.origin_index &&
            direct.origin==lane.origin && direct.rate==lane.rate && direct.frequency==lane.frequency &&
            direct.rate_index==lane.rate_index && direct.tone_bank_base==lane.tone_bank_base &&
            direct.phase_lower==lane.phase_lower && direct.phase_upper==lane.phase_upper &&
            direct.anchor.sample==lane.anchor.sample && direct.anchor.fraction==lane.anchor.fraction &&
            direct.observed_begin==lane.observed_begin && direct.index==lane.index &&
            direct.first_full_index==lane.first_full_index,"family traversal changed original lattice identity");
        SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
        const auto anchor=SchedHopCoordinate{c.stream_epoch*c.sample_rate,0}.add_samples(expected.phase);
        const auto source=map.locate(anchor).source;
        const auto tick=anchor.add_signed(-expected.origin*expected.rate);
        const auto observed=map.locate(tick).source.distance(source);
        check(lane.anchor.sample==source.sample&&lane.anchor.fraction==source.fraction&&lane.observed_begin==observed,"catalog changed signed observed source coordinate");
        check(lane.index==static_cast<std::uint64_t>(std::floor(std::max(0.L,observed)/symbol_sample_count(c)))&&
            lane.first_full_index==static_cast<std::uint64_t>(std::ceil(std::max(0.L,observed)/symbol_sample_count(c))),"catalog changed initial/full source index");
        for(const auto index:{lane.index,lane.first_full_index}) {
            const auto source_end=static_cast<long double>(index)*symbol_sample_count(c)+symbol_sample_count(c);
            const auto end=original_boundary(c,s,g,lane,source_end,true);
            check(catalog.boundary(lane,source_end,true,cursor)==end,"catalog changed latest admitted END");
            check(catalog.due(lane,index,cursor)==static_cast<std::uint64_t>(std::max(0.L,std::ceil(end))),"catalog changed integer due fence");
            check(catalog.boundary(lane,source_end,false,cursor)==original_boundary(c,s,g,lane,source_end,false),"catalog changed START inverse");
        }
    });
    check(count==catalog.size()&&count==g.retained_hypotheses,"catalog changed complete retained lane count");
    for(const auto& [key,id]:previous)
        check(!catalog.adjacent_origin(catalog.describe(id,cursor),true),"last original phase lane has a later neighbor");
    std::size_t visited=0;catalog.for_each(0,catalog.size(),cursor,[&](const auto& lane){check(lane.id==visited++,"catalog range reordered lanes");});
    if(catalog.size()>3) {
        std::size_t next=1;catalog.for_each(1,catalog.size()-2,cursor,[&](const auto& lane){check(lane.id==next++,"catalog partial range reordered lanes");});
        check(next==catalog.size()-1,"catalog partial range lost lanes");
    }
    rejects([&]{catalog.describe(catalog.size(),cursor);},"catalog accepted invalid rank");
    rejects([&]{Catalog refused(c,s,g,sizeof(Catalog));},"catalog ignored metadata budget");
    Catalog other(c,s,g);auto wrong=other.make_cursor();
    if(catalog.size())rejects([&]{catalog.describe(0,wrong);},"catalog accepted wrong calendar cursor");
    check(!catalog.rank(catalog.pairs().size(),0,0),"catalog accepted invalid pair");
    check(catalog.construction_peak_bytes()>=catalog.working_bytes()+Catalog::cursor_reservation_bytes(),
        "catalog omitted constructor/cursor scratch peak");
    const auto peak=catalog.construction_peak_bytes();Catalog exact_budget(c,s,g,peak);
    check(exact_budget.size()==catalog.size(),"exact catalog metadata budget changed coverage");
    rejects([&]{Catalog refused(c,s,g,peak-1);},"catalog exceeded exact constructor reservation");
}
void user36() {
    auto c=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,true,1500,10,OuterDsssVersion::interleaved_v2).config;
    c.data_key.emplace(Bytes(32,0x67));c.stream_epoch=1799999969;
    OscillatorSearchConfig o;o.lf=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-xo"));
    o.rf=o.lf;o.rf_shift_hz=3e7;o.margin=3;c.oscillator_search=o;
    auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->audio_error_seconds=0;c.scheduled_hopping=plan;
    PatternSearch s;s.start_offset_seconds=0;s.start_uncertainty_seconds=7;s.search_stream_phases=true;
    const auto coarse=oscillator_pattern_search(c);s.hypotheses=coarse.hypotheses;
    s.admitted_frequency_half_width_hz=coarse.frequency.requested_half_width_hz;
    s.admitted_clock_half_width_ppm=coarse.requested_clock_half_width_ppm;s.complex_frequency_coverage=s.complex_clock_coverage=true;
    const auto refined=scheduled_complex_search(c,s);check(refined.complete_coverage,"user36 refined bank unavailable");s=refined.search;
    Catalog catalog(c,s,refined.geometry,256*1024);
    check(catalog.size()==1179702&&catalog.pairs().size()==13&&catalog.families().size()==234,
        "user36 original full bank changed");
    check(testing::benchmark_below(catalog.working_bytes(),std::size_t{64*1024},"test_scheduled_acquisition_catalog.catalog_working_bytes","workspace_bytes"),"user36 catalog allocated origin/lane-sized metadata");
    auto cursor=catalog.make_cursor();const std::set<std::size_t> samples{0,1,17,18,5041,catalog.size()/2,catalog.size()-2,catalog.size()-1};
    std::size_t checked=0;
    const auto count=original(c,s,refined.geometry,[&](std::size_t id,const Original& expected) {
        if(!samples.contains(id))return;
        const auto lane=catalog.describe(id,cursor);
        check(lane.pair==expected.pair&&lane.origin_index==expected.origin_index&&lane.origin==expected.origin&&
            lane.rate==expected.rate&&lane.phase_lower==expected.phase&&lane.frequency==expected.frequency,"user36 catalog boundary identity mismatch");
        check(catalog.rank(lane.pair,lane.origin_index,lane.phase_lower)==std::optional<std::size_t>{id},"user36 inverse rank mismatch");++checked;
    });
    check(count==catalog.size()&&checked==samples.size(),"user36 independent full enumerator count mismatch");
    std::cout<<"user36 lanes="<<catalog.size()<<" metadata="<<catalog.working_bytes()<<" cursor="<<cursor.working_bytes()<<'\n';
}
void shared_default_calendar() {
    for(const auto multiplier:{3u,5u}) {
        auto c=small();auto plan=std::make_shared<SchedHopPlan>(*c.scheduled_hopping);
        plan->time_multiplier=multiplier;c.scheduled_hopping=plan;
        auto s=search();s.start_offset_seconds=0;s.start_uncertainty_seconds=7;
        const auto broad_geometry=active(c,s);Catalog broad(c,s,broad_geometry);
        const auto policy=*clock_sync::effective_policy(std::nullopt,true);
        const auto map=clock_sync::calendar_arrival_map(policy,c.stream_epoch,c.stream_epoch,0,
            c.sample_rate,1./c.sample_rate,.00001,.003,c.sample_rate-1,0);
        check(map.has_value(),"Default calendar mapping unavailable");
        s.qualified_start_window=PatternStartWindow{map->origin_samples,map->phase_scale,map->half_width_samples};
        const auto narrow_geometry=pattern_scheduled_complex_geometry(c,s);
        Catalog narrow(c,s,narrow_geometry);
        check(s.start_uncertainty_seconds==7 && narrow.pairs().size()==broad.pairs().size() &&
            narrow.families().size()==broad.families().size(),
            "shared calendar removed message phases or oscillator hypotheses");
        check(narrow.size()>0 && narrow.size()<broad.size()/10,
            "payload origin still translates the calendar independently across 14 seconds");
        // The independent original enumerator retains original indices, clipped
        // cells and boundaries. This checks the diagonal, not a replaced grid.
        compare(c,s);
        auto cursor=narrow.make_cursor();
        narrow.for_each(0,narrow.size(),cursor,[&](const auto& lane) {
            check(lane.origin>=-7.L*c.sample_rate && lane.origin<=7.L*c.sample_rate,
                "calendar prior enlarged the original message-origin domain");
        });
    }
}
}
int main() {
    try {
        auto c=small();auto s=search();compare(c,s);
        auto qualified_config=c;const auto broad_geometry=active(qualified_config,s);
        Catalog broad(qualified_config,s,broad_geometry);auto broad_cursor=broad.make_cursor();
        const auto chosen=broad.describe(broad.size()/2,broad_cursor);
        for(const auto width:{0.L,.00001L,.031L,.17L})for(const auto scale:{.9999997L,1.L,1.0000003L}) {
            auto q=s;q.qualified_start_window=PatternStartWindow{chosen.origin-chosen.phase_lower*scale,scale,width*c.sample_rate};
            const auto g=pattern_scheduled_complex_geometry(qualified_config,q);
            check(g.retained_hypotheses>0,"qualified oracle fixture failed to retain its chosen lane");
            if(width<.031L)check(g.retained_hypotheses<broad.size(),"tight qualified fixture did not reduce admitted lanes");
            compare(qualified_config,q);
        }
        auto empty=s;empty.qualified_start_window=PatternStartWindow{1e6L,1,0};compare(qualified_config,empty,false);
        auto unpaired=s;unpaired.hypotheses.clear();unpaired.frequency_offsets_hz={.01,0,.01,-.01};
        unpaired.clock_errors_ppm={-1000,-.0003,0,.0003,1000};compare(c,unpaired);
        auto implicit=s;implicit.hypotheses.clear();implicit.frequency_offsets_hz.clear();implicit.clock_errors_ppm={-1000};compare(c,implicit);
        auto singleton=s;singleton.search_stream_phases=false;singleton.start_offset_seconds=-.001;singleton.start_uncertainty_seconds=0;
        // Choose a genuinely active sample phase before disabling phase search.
        const auto g=active(c,s);Catalog all(c,s,g);auto cursor=all.make_cursor();c.stream_phase_samples=all.describe(0,cursor).phase_lower;
        compare(c,singleton);singleton.start_offset_seconds=.001;compare(c,singleton);
        auto fractional=c;fractional.sample_rate=201;fractional.bandwidth_hz=20.1;fractional.carrier_hz=40;
        compare(fractional,s);auto dilution=std::make_shared<SchedHopPlan>(*c.scheduled_hopping);dilution->group_hops=2;
        c.scheduled_hopping=dilution;compare(c,s);
        auto partial=small();partial.integration_seconds=6.415;
        check(symbol_sample_count(partial)%pattern_chip_samples(partial)!=0,"partial final chip fixture is vacuous");
        compare(partial,s);
        // Deliberate positive/negative/zero source starts, not a merely mixed
        // broad bank. Zero is exact; either sign must survive unclamped.
        auto signed_config=small();const auto signed_g=active(signed_config,s);
        Catalog signed_all(signed_config,s,signed_g);auto signed_cursor=signed_all.make_cursor();
        check(signed_all.families().size()>1,"signed source fixture has no interior phase");
        signed_config.stream_phase_samples=signed_all.families()[1].phase;
        for(const double offset:{-.001,0.,.001}) {
            auto one=s;one.search_stream_phases=false;one.start_offset_seconds=offset;one.start_uncertainty_seconds=0;
            const auto g=pattern_scheduled_complex_geometry(signed_config,one);Catalog catalog(signed_config,one,g);auto cursor=catalog.make_cursor();
            const auto lane=catalog.describe(0,cursor);
            if(offset==0)check(lane.observed_begin==0&&lane.index==0&&lane.first_full_index==0,"exact source boundary shifted");
            else if(offset<0)check(lane.observed_begin>0&&lane.index==0&&lane.first_full_index==1,"truncated first symbol lost following-full requirement");
            else check(lane.observed_begin<0&&lane.index==0&&lane.first_full_index==0,"negative observed begin was clamped");
        }
        auto exact=small();exact.sample_rate=256;exact.bandwidth_hz=32;exact.carrier_hz=64;
        auto only=search();only.hypotheses={{0,0}};const auto eg=active(exact,only);
        Catalog choices(exact,only,eg);check(!choices.families().empty(),"exact-N fixture has no active phase");
        exact.stream_phase_samples=choices.families().front().phase;
        SchedHopSourceMap exact_map(*exact.scheduled_hopping,*exact.data_key,exact.sample_rate);
        const auto anchor=SchedHopCoordinate{exact.stream_epoch*exact.sample_rate,0}.add_samples(exact.stream_phase_samples);
        const auto target=exact_map.source_to_physical(exact_map.locate(anchor).source.add_samples(symbol_sample_count(exact)),false);
        only.search_stream_phases=false;only.start_uncertainty_seconds=0;
        only.start_offset_seconds=static_cast<double>(-target.distance(anchor)/exact.sample_rate);
        const auto exact_g=pattern_scheduled_complex_geometry(exact,only);Catalog exact_catalog(exact,only,exact_g);auto exact_cursor=exact_catalog.make_cursor();
        const auto exact_lane=exact_catalog.describe(0,exact_cursor);
        check(exact_lane.observed_begin==symbol_sample_count(exact)&&exact_lane.index==1&&exact_lane.first_full_index==1,
            "exact whole-source-symbol boundary changed floor/ceil ownership");
        user36();shared_default_calendar();std::cout<<"scheduled acquisition catalog passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
