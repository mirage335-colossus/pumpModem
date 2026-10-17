// Actual private dispatch and numerical range enumeration; no mirror of the chooser.
// A pre-requested stop reaches the real preparation walker and then cancels before
// private templates or FFT plans. Complete receiver cost is measured separately.
#include "../src/pattern_correlator.cpp"
#include "datapump/tuning.hpp"
#include <iostream>
#include <stop_token>
#include <string>
namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using State=PatternCorrelator::Impl;
    using Lazy=State::LazyState;
    using Range=Lazy::DueRange;
    static void check(bool condition,const char* message){if(!condition)throw Error(message);}
    static void mode(const char* key,const char* value) {
#if defined(_WIN32)
        check(_putenv_s(key,value?value:"")==0,"cannot configure clock-reference environment");
#else
        check((value?setenv(key,value,1):unsetenv(key))==0,"cannot configure clock-reference environment");
#endif
    }
    static Config config(unsigned multiplier,SearchArithmetic arithmetic) {
        auto c=tuning::resolve(360,40,tuning::PatternMode::auto_keystream,true,1500,10,
            OuterDsssVersion::interleaved_v2,arithmetic).config;
        c.data_key.emplace(Bytes(32,0x67));
        const auto bytes=c.data_key->stream(StreamPurpose::Scrambler,0,0,32);
        std::copy(bytes.begin(),bytes.end(),c.spreading_seed.begin());
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=multiplier;plan->group_hops=1;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=.01;
        plan->audio_error_seconds=plan->region_seconds=plan->offset_seconds=0;plan->ramp_seconds=.01;
        c.scheduled_hopping=plan;
        auto hops=std::make_shared<FhssPlan>();hops->channels=2;hops->shifts[0]=30000000;hops->shifts[1]=31000000;
        hops->dwell_seconds=.4;hops->power_ramps=hops->mandatory_ramps=hops->smooth_ramps=true;
        hops->defer_symbols=true;hops->clock_error_seconds=hops->peer_clock_error_seconds=.01;
        hops->audio_error_seconds=hops->region_seconds=0;hops->ramp_seconds=8./14400;
        c.fake_hopping=hops;
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto first=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        c.stream_epoch=first.sample/c.sample_rate;c.stream_phase_samples=first.sample%c.sample_rate;
        check(c.sample_rate==14400 && symbol_sample_count(c)==5120 && pattern_chip_samples(c)==8,
            "Spread source geometry changed");
        return c;
    }
    static PatternSearch search() {
        PatternSearch q;q.start_offset_seconds=-.8622221946716309;q.start_uncertainty_seconds=7;
        q.search_stream_phases=false;q.prefer_streamed_templates=true;q.allow_local_clock_fallback=true;
        q.hypotheses={{0,-300},{0,0},{0,1},{0,300}};
        q.worker_threads=1;q.drift_tolerant=false;q.bit_limit=256;q.chunk_bits=1;
        q.candidate_limit=64;q.track_limit=8;q.retain_score=0;return q;
    }
    static std::size_t family_for(State& s,long double rate) {
        for(std::size_t f=0;f<s.lazy_state()->catalog->families().size();++f) {
            const auto& family=s.lazy_state()->catalog->families()[f];
            if(std::abs(s.lazy_state()->catalog->pairs()[family.pair].rate-rate)<1e-15L &&
               family.origin_end-family.origin_first>=4096)return f;
        }
        throw Error("actual Spread catalog lacks the required original clock family");
    }
    static Range range(State& s,std::size_t family,std::size_t count=4096) {
        Range r;r.family=family;r.first=s.lazy_state()->catalog->families()[family].origin_first;
        r.end=r.first+count;r.index=0;r.id=s.lazy_family_lane(family,r.first).id;return r;
    }
    static void verify_actual_rounds(State& s,const Range& r) {
        const auto ref=s.lazy_screen_reference(r);
        const auto first=s.lazy_family_lane(r.family,r.first);
        const auto& pair=s.lazy_state()->catalog->pairs()[first.pair];
        const auto start=std::round(first.origin-ref.origin),step=std::round(pair.step);
        long double previous=-1;
        for(auto i=r.first;i<r.end;++i) {
            const auto lane=s.lazy_family_lane(r.family,i);
            const auto integer=std::round(lane.origin-ref.origin);
            check(integer==start+(i-r.first)*step,"real lane translation escaped its certified affine progression");
            check(integer>=0 && integer<detail::scheduled_acquisition_tile::maximum_start_span && integer>previous,
                "real lane translation is duplicated, negative or outside its tile");
            check(lane.pair==first.pair && lane.rate==first.rate && lane.phase_lower==first.phase_lower &&
                lane.anchor.distance(first.anchor)==0,"numerical cover changed original family identity");
            previous=integer;
        }
    }
    static void one(unsigned multiplier,SearchArithmetic arithmetic) {
        auto c=config(multiplier,arithmetic);
        PatternCorrelator rx(c,search(),128*1024*1024,
            PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
        auto& s=*rx.impl_;check(s.lazy_state() && s.shaped,"Spread did not select actual shaped implicit backend");
        auto broad=range(s,family_for(s,1.0003L));
        mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","0");
        const auto historical=s.lazy_screen_reference(broad);
        if(arithmetic!=SearchArithmetic::fp64)check(historical.rate==1 && s.lazy_screen_numeric_cover_fits(broad),
            "disabled clock experiment changed historical reference/subdivision policy");
        mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","1");
        const auto actual=s.lazy_family_lane(broad.family,broad.first);
        const auto selected=s.lazy_screen_reference(broad);
        check(selected.rate==actual.rate && selected.origin==actual.origin && selected.phase_lower==actual.phase_lower &&
            selected.anchor.distance(actual.anchor)==0,"uncertified rate-one proxy replaced the original-clock FFT reference");
        check(s.lazy_screen_numeric_subdivision_supported(broad),"ordinary Spread grid lost numerical subdivision eligibility");
        // Nonzero original ordinal: the old central reference makes the first
        // translated start negative even though this exact-clock tile is small.
        auto shifted=range(s,broad.family,64);shifted.first+=1024;shifted.end+=1024;
        shifted.id=s.lazy_family_lane(shifted.family,shifted.first).id;
        const auto shifted_lane=s.lazy_family_lane(shifted.family,shifted.first);
        const auto central_family=family_for(s,1);
        const auto central=s.lazy_family_lane(central_family,shifted.first);
        check(std::round(shifted_lane.origin-central.origin)<0 &&
            !s.lazy_screen_translation_fits(shifted,central),
            "nonzero original ordinal no longer reproduces the negative central-clock translation");
        if(arithmetic!=SearchArithmetic::fp64)check(s.lazy_screen_numeric_cover_fits(shifted),
            "bounded exact-clock range still lost its useful numerical cover");
        if(arithmetic!=SearchArithmetic::fp64) {
            check(!s.lazy_screen_numeric_cover_fits(broad),"broad off-rate range unexpectedly fits a single translated tile");
            auto nearby_range=range(s,family_for(s,1.000001L),8);
            const auto near_selected=s.lazy_screen_reference(nearby_range);
            check(near_selected.rate==1 && s.lazy_screen_translation_fits(nearby_range,near_selected),
                "certified close-clock reuse was disabled along with unsupported broad reuse");
        } else {
            auto nearby_range=range(s,family_for(s,1.000001L),8);
            check(s.lazy_screen_reference(nearby_range).rate==s.lazy_family_lane(nearby_range.family,nearby_range.first).rate,
                "FP64 force selected approximate clock interpolation");
        }
        s.lazy_state()->due_ranges={broad};
        const auto templates=s.lazy_state()->serial_execution->fp32_templates.size()+s.lazy_state()->serial_execution->fp64_templates.size();
        const auto numerical=[&]{const auto& x=*s.lazy_state();const auto& e=*x.serial_execution;
            return std::tuple{x.input_transforms,x.template_transforms,x.inverse_transforms,x.plan_misses,
                x.spectral_template_misses,e.input_transforms,e.template_transforms,e.inverse_transforms,
                e.plan_misses,e.spectral_template_misses,x.colored_source_builds};};
        const auto detector=[&]{return std::tuple{s.trials,s.sample,s.lazy_state()->processed,
            s.lazy_state()->history_generation,s.history.size(),s.emissions.size(),s.bursts.size()};};
        const auto before_numerical=numerical();const auto before_detector=detector();
        std::stop_source stop;stop.request_stop();bool cancelled=false;
        try{s.lazy_prepare_screens(stop.get_token());}
        catch(const Error& e){cancelled=std::string_view(e.what())=="pattern correlation cancelled";if(!cancelled)throw;}
        check(cancelled,"actual preparation failed to reach its cancellation fence after range enumeration");
        const auto& pieces=s.lazy_state()->prepared_ranges;
        check(!pieces.empty() && pieces.size()<128,"numeric subdivision vanished or proliferated into singleton FFTs");
        std::size_t next=broad.first,covered=0;
        for(const auto& piece:pieces) {
            check(piece.family==broad.family && piece.index==broad.index && piece.first==next && piece.end>piece.first &&
                piece.end<=broad.end && piece.id==s.lazy_family_lane(piece.family,piece.first).id,
                "real numerical walker omitted/reordered an original lane or changed symbol identity");
            if(arithmetic!=SearchArithmetic::fp64) {
                check(s.lazy_screen_numeric_cover_fits(piece),"real walker retained an uncertified subdividable cover");
                verify_actual_rounds(s,piece);
            }
            next=piece.end;covered+=piece.end-piece.first;
        }
        check(next==broad.end && covered==broad.end-broad.first,"real numerical walker changed original candidate coverage");
        check(s.lazy_state()->serial_execution->fp32_templates.size()+s.lazy_state()->serial_execution->fp64_templates.size()==templates &&
            s.lazy_state()->serial_execution->fp32_plans.empty() && s.lazy_state()->serial_execution->fp64_plans.empty(),
            "metadata regression accidentally executed private template or FFT preparation");
        check(numerical()==before_numerical,"metadata fixture executed template/FFT construction or transforms");
        check(detector()==before_detector,"metadata cancellation altered detector progress, trials or accepted evidence");
        check(!s.lazy_state()->screens_running && s.lazy_state()->transient_reservation==0 && s.bursts.empty(),
            "cancelled metadata preparation leaked state or manufactured accepted evidence");
        check(sizeof(PatternCorrelator)+s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation<=s.budget,
            "cancelled numerical walker exceeded workspace");
        std::cout<<"actual clock-reference M"<<multiplier<<" arithmetic="<<static_cast<unsigned>(arithmetic)
            <<" original_lanes="<<covered<<" numerical_tiles="<<pieces.size()<<"\n";
    }
    static void run(){one(3,SearchArithmetic::default_mode);one(5,SearchArithmetic::default_mode);
        one(3,SearchArithmetic::fp32);one(3,SearchArithmetic::fp64);}
};
}
int main() {
    using A=datapump::modem::PatternCorrelatorTimingTestAccess;
    try {
        A::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");A::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
        A::mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","1");
        A::mode("DATAPUMP_SCHED_ORIGIN_CLASSES","0");A::mode("DATAPUMP_SCHED_COLD_COLORED","0");
        A::mode("DATAPUMP_SCHED_SHARED_INPUT","0");
        if(!A::State::lazy_reference_environment()){
            std::cout<<"clock-reference numeric environment unavailable; actual FFT-dispatch coverage omitted\n";return 77;
        }
        A::run();std::cout<<"actual chooser and numerical range enumeration checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
