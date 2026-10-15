// Source-inclusion test of actual private accounting and mutation boundaries.
// Link as test_temporal_templates; do not compile a second correlator object.
#include "../src/pattern_correlator.cpp"
#include "scheduled_lazy_test_fixture.hpp"
#include <cstdlib>

namespace {
long array_failure_countdown=-1;
void set_environment(const char* name,const char* value) {
#if defined(_WIN32)
    _putenv_s(name,value);
#else
    ::setenv(name,value,1);
#endif
}
}

// Only explicitly armed Cache::store array allocations fail. Restore on throw,
// so unrelated PatternCode/FFT/worker allocations keep their normal behavior.
void* operator new[](std::size_t bytes) {
    if(array_failure_countdown==0){array_failure_countdown=-1;throw std::bad_alloc();}
    if(array_failure_countdown>0)--array_failure_countdown;
    if(auto* allocation=std::malloc(bytes?bytes:1))return allocation;
    throw std::bad_alloc();
}
void operator delete[](void* allocation)noexcept {std::free(allocation);}
void operator delete[](void* allocation,std::size_t)noexcept {std::free(allocation);}

namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using State=PatternCorrelator::Impl;
    using Execution=State::LazyState::Execution;
    static void same_numeric(const Execution& execution) {
        check(execution.numeric_bytes()==execution.numeric_bytes_uncached(),
            "joined accounting differs from every retained capacity");
    }
    static void owner_growth_uses_current_loan() {
        auto c=configuration(3,10,false);
        c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;
        c.stream_epoch=8312;c.stream_phase_samples=0;
        Capture input;auto s=search(c,input);s.start_uncertainty_seconds=7;
        s.search_stream_phases=true;s.frequency_offsets_hz.clear();
        s.qualified_start_window=PatternStartWindow{0,1,.02L*c.sample_rate};
        for(int j=-9;j<=9;++j)s.hypotheses.push_back({j*.00001,0});
        PatternCorrelator rx(c,s,23045189);
        auto& owner=*rx.impl_;auto& state=*owner.lazy_state();
        const auto initial_capacity=state.capacity;
        check(initial_capacity<state.catalog->size(),"loan fixture lacks additional logical owners");
        auto* borrowed=&owner.hypotheses[0];borrowed->burst.bits={1,0,1};
        const auto old=owner.hypotheses.size();
        rx.trim_optional_workspace();rx.set_workspace_bytes(rx.reserved_workspace_bytes());
        bool rejected=false;try{owner.lazy_grow_slots();}catch(const Error&){rejected=true;}
        check(rejected && owner.hypotheses.size()==old && &owner.hypotheses[0]==borrowed &&
              borrowed->burst.bits==Bytes({1,0,1}),"insufficient current loan changed required owner state");
        constexpr std::size_t loan=128*1024*1024;
        rx.set_workspace_bytes(loan);
        while(owner.hypotheses.size()<=initial_capacity)owner.lazy_grow_slots();
        check(&owner.hypotheses[0]==borrowed && borrowed->burst.bits==Bytes({1,0,1}) &&
              rx.reserved_workspace_bytes()<=loan && rx.working_bytes()<=rx.reserved_workspace_bytes(),
              "larger current loan did not preserve stable pending owners and complete allocation charge");
        bool shrink_rejected=false;try{rx.set_workspace_bytes(1);}catch(const Error&){shrink_rejected=true;}
        check(shrink_rejected && &owner.hypotheses[0]==borrowed && borrowed->burst.bits==Bytes({1,0,1}),
              "loan retirement discarded required pending evidence");
    }
    static void numeric_lifecycle() {
        auto c=configuration();Capture input;input.start=0;
        auto s=search(c,input);PatternCorrelator rx(c,s,64*1024*1024);
        check(static_cast<bool>(rx.impl_->lazy_state()),"accounting fixture did not select implicit backend");
        auto& owner=*rx.impl_;auto& execution=*owner.lazy_state()->serial_execution;
        execution.available_bytes=32*1024*1024;
        execution.freeze_numeric_bytes();same_numeric(execution);
        check(execution.joined_numeric_bytes.has_value(),"joined total was not retained");

        // lazy_plan thaws before both pointer reserve and plan construction.
        (void)owner.lazy_plan<float>({16,32},{});
        check(!execution.joined_numeric_bytes,"direct plan mutation kept a frozen total");
        (void)owner.lazy_plan<double>({16,32},{});
        same_numeric(execution);
        execution.thaw_numeric_bytes();
        auto piece=std::make_unique<State::LazyState::TemplatePiece<float>>();
        piece->rows.resize(32,{{Complex{.25,.125},Complex{-.125,.25}}});
        piece->mask.resize(32,1);piece->source.reserve(41);piece->gain.reserve(43);
        piece->active.reserve(47);piece->frame.reserve(53);
        piece->prepared=std::make_unique<detail::scheduled_acquisition_tile::PreparedFragment<float>>(
            *execution.fp32_plans.front(),detail::scheduled_acquisition_tile::Fragment{piece->rows,piece->mask,0},1024*1024);
        execution.fp32_templates.reserve(8);execution.fp32_templates.push_back(std::move(piece));
        execution.fp64_templates.reserve(4);execution.freeze_numeric_bytes();same_numeric(execution);
        const auto retained=execution.numeric_bytes();
        check(retained>0,"numeric fixture retained no charged storage");

        // The validation oracle must detect an otherwise invisible stale
        // capacity, then explicit thaw must recover the exact live count.
        execution.fp32_templates.front()->gain.reserve(101);bool stale_detected=false;
        try{(void)execution.numeric_bytes();}catch(const Error&){stale_detected=true;}
        check(stale_detected,"workspace verification accepted a stale retained capacity");
        execution.thaw_numeric_bytes();same_numeric(execution);
        execution.freeze_numeric_bytes();
        execution.screen_active=true;bool trim_rejected=false;
        try{execution.drop_templates();}catch(const Error&){trim_rejected=true;}
        check(trim_rejected && execution.joined_numeric_bytes,
            "active borrower trim changed accounting identity");
        execution.screen_active=false;execution.drop_templates();
        check(!execution.joined_numeric_bytes,"template trim failed to invalidate total");same_numeric(execution);

        // A cancelled new Plan keeps existing pointer backing charged and
        // leaves the direct-call numeric total thawed.
        execution.freeze_numeric_bytes();std::stop_source stop;stop.request_stop();bool cancelled_plan=false;
        try{(void)owner.lazy_plan<double>({17,32},stop.get_token());}catch(const Error&){cancelled_plan=true;}
        check(cancelled_plan && !execution.joined_numeric_bytes,
            "cancelled direct plan retained frozen accounting");same_numeric(execution);
        owner.lazy_merge_costs(execution);same_numeric(execution);
        check(execution.joined_numeric_bytes.has_value(),"serial unwind checkpoint did not freeze live capacities");
        execution.drop_plans();same_numeric(execution);
        check(execution.numeric_bytes()==0,"joined trim retained plan/template pointer backing");
    }

    static void projection_store_delta() {
        auto c=configuration();Capture input;input.start=0;
        auto s=search(c,input);PatternCorrelator rx(c,s,64*1024*1024);
        auto& owner=*rx.impl_;auto& state=*owner.lazy_state();
        std::array<float,512> pcm{};for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>((i%23)*.01);
        const State::LazyPcmBlockView block{7,pcm};
        const auto exact=[&] {
            check(owner.accounted_bytes==owner.working_bytes()+owner.drift_reserved+owner.amplitude_scratch_reservation,
                "projection store delta differs from full receiver accounting");
        };
        owner.lazy_prepare_block(block,0,1);exact();
        const auto rows=state.prefix;
        owner.lazy_prepare_block(block,0,2);exact();
        for(std::size_t i=0;i<rows.size();++i)check(std::memcmp(&rows[i],&state.prefix[i],sizeof(rows[i]))==0,
            "cache accounting changed ORIGINAL projection bits");
        // Capture range/fence varies, with each failed store checked through
        // the actual receiver path; no externally escaped cache-row borrower.
        for(std::uint64_t at=100;at<900;at+=100) {
            owner.lazy_prepare_block({at,pcm},0,at);exact();
        }
        if(state.original_projections && state.original_projections->enabled() &&
           detail::scheduled_acquisition_certificate::environment<double>()) {
            owner.drop_lazy_projections();
            // metadata success followed by row failure leaves metadata charged.
            array_failure_countdown=1;owner.lazy_prepare_block({1000,pcm},0,10);array_failure_countdown=-1;exact();
            check(state.original_projections && state.original_projections->statistics().allocation_failures>0,
                "projection fixture did not exercise optional array allocation failure");
            state.projection_cache_limit=128+State::LazyState::OriginalProjections::minimum_enabled_capacity();
            owner.drop_lazy_projections();
            for(std::uint64_t at=2000;at<2400;at+=100){owner.lazy_prepare_block({at,pcm},0,at);exact();}
            check(state.original_projections->entry_capacity()==4 && state.original_projections->entries()==4,
                "receiver eviction fixture did not fill its single cache set");
            const auto before=owner.accounted_bytes;
            array_failure_countdown=0;owner.lazy_prepare_block({2400,pcm},0,24);array_failure_countdown=-1;exact();
            check(owner.accounted_bytes==before-513*sizeof(detail::CorrelationProjection)-128 &&
                state.original_projections->entries()==3 && state.original_projections->statistics().collision_evictions==1,
                "actual receiver failed insert did not account its cleansed eviction");
        }
        rx.trim_optional_workspace();exact();
        state.projection_cache_limit=64*1024*1024;
        rx.set_workspace_bytes(96*1024*1024);owner.lazy_prepare_block({1100,pcm},0,11);exact();
        rx.trim_optional_workspace();rx.set_workspace_bytes(16*1024*1024);exact();
    }

    static void cohort_grant_retries() {
        auto c=configuration();Capture input;input.start=0;
        auto s=search(c,input);PatternCorrelator rx(c,s,64*1024*1024);
        auto& owner=*rx.impl_;auto& execution=*owner.lazy_state()->serial_execution;
        execution.available_bytes=32*1024*1024;
        (void)owner.lazy_plan<float>({16,32},{});
        auto* pinned_plan=execution.fp32_plans.front().get();
        owner.accounted_bytes=owner.working_bytes()+owner.unallocated_history_bytes()+
            owner.drift_reserved+owner.amplitude_scratch_reservation;
        const auto retained=sizeof(PatternCorrelator)+owner.accounted_bytes;
        struct Grant {
            State& owner;unsigned calls=0,remaining=0;bool progress=true;
            static bool reclaim(void* context) {
                auto& self=*static_cast<Grant*>(context);++self.calls;
                if(!self.remaining)return false;
                --self.remaining;
                if(self.progress)self.owner.budget+=256;
                return true;
            }
        } grant{owner,0,8,true};
        owner.budget=retained+128;
        {
            // No source Pool is needed for the joined pressure capability.
            detail::scheduled_source_samples::Scope scope(nullptr,0,&grant,&Grant::reclaim);
            owner.room_for(1024);
        }
        check(grant.calls==4 && grant.remaining==4 && owner.budget==retained+128+4*256,
            "actual room_for failed precise multi-grant retry or overreclaimed");
        check(execution.fp32_plans.size()==1 && execution.fp32_plans.front().get()==pinned_plan,
            "funded retry unnecessarily evicted active acceleration");
        same_numeric(execution);

        owner.budget=retained+128;grant.calls=0;grant.remaining=1;grant.progress=false;
        bool bad_grant=false;
        try {detail::scheduled_source_samples::Scope scope(nullptr,0,&grant,&Grant::reclaim);owner.room_for(1024);}
        catch(const Error&){bad_grant=true;}
        check(bad_grant && grant.calls==1 && execution.fp32_plans.size()==1 &&
            execution.fp32_plans.front().get()==pinned_plan,
            "nonprogressing external grant looped or destroyed active acceleration");
        same_numeric(execution);
    }

    static void helper_failure_capacity() {
        using Cache=State::LazyState::OriginalProjections;
        namespace cache=detail::scheduled_original_projection;
        std::array<detail::CorrelationProjection,513> rows{};
        cache::Key key;key.capture_generation=1;key.processing_count=512;
        Cache value(Cache::minimum_enabled_capacity());
        check(value.entry_capacity()==4,"failure fixture must force a single cache set");
        const auto store=[&](cache::Key identity,std::span<const detail::CorrelationProjection> source,long fail=-1,
                             std::size_t available=1024*1024) {
            array_failure_countdown=fail;const auto stored=value.store(identity,source,available);array_failure_countdown=-1;
            return stored;
        };
        check(!store(key,rows,0) && value.statistics().allocation_failures==1,"metadata failure was not caught");
        check(!store(key,rows,1) && value.statistics().allocation_failures==2 && value.dynamic_bytes()>0,
            "row failure must retain newly allocated metadata");
        for(unsigned i=0;i<4;++i){key.processing_first=i;check(store(key,rows),"bounded cache set could not fill");}
        const auto before=value.working_bytes();key.processing_first=5;
        check(!store(key,rows,0) && value.working_bytes()<before && value.statistics().collision_evictions==1,
            "failed replacement must account cleansed/evicted payload");
        check(store(key,rows),"replacement did not recover after allocation failure");
        key.processing_first=6;key.processing_count=17;
        check(store(key,std::span(rows).first(18)),"smaller replacement failed");
        key.processing_first=7;key.processing_count=512;check(store(key,rows),"larger replacement failed");
        check(store(key,rows,-1,0),"duplicate should not require new workspace");
        key.processing_count=0;check(!store(key,{}),"invalid row unexpectedly stored");
        key.processing_count=512;key.processing_first=8;
        value.clear();
        check(!store(key,rows,-1,0),"budget skip unexpectedly allocated a row");
        value.clear();check(value.dynamic_bytes()==0,"clear retained uncharged cache backing");
    }
};
}

int main(){try {
    set_environment("DATAPUMP_SCHED_LAZY_ACQUISITION","1");
    set_environment("DATAPUMP_SCHED_WORKSPACE_VERIFY","1");
    set_environment("DATAPUMP_SCHED_WORKSPACE_ACCOUNTING","1");
    datapump::modem::PatternCorrelatorTimingTestAccess::numeric_lifecycle();
    datapump::modem::PatternCorrelatorTimingTestAccess::projection_store_delta();
    datapump::modem::PatternCorrelatorTimingTestAccess::helper_failure_capacity();
    datapump::modem::PatternCorrelatorTimingTestAccess::cohort_grant_retries();
    datapump::modem::PatternCorrelatorTimingTestAccess::owner_growth_uses_current_loan();
    set_environment("DATAPUMP_SCHED_WORKSPACE_ACCOUNTING","0");
    datapump::modem::PatternCorrelatorTimingTestAccess::projection_store_delta();
    std::cout<<"scheduled workspace accounting exact capacity/invalidation tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
