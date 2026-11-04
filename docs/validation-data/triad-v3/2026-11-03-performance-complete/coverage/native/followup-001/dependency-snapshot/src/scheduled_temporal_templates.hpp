#pragma once
#include "scheduled_work_profile.hpp"
#include "scheduled_ordered_pulse_candidates.hpp"
#include "scheduled_borrowed_plan_prepare.hpp"
#include <memory>
#include "scheduled_temporal_normalization.hpp"
#include "scheduled_temporal_fp32_execution.hpp"

namespace datapump::modem::detail {
// One worker's optional bounded SOURCE-output scratch. Neither PCM nor any
// detector/clock/crypto decision is stored here. Each attempted range is one
// original active piece and one original current/carry target symbol.
class ScheduledTemporalTemplates {
public:
    using Tile=ScheduledOrderedPulseCandidatesT<1,512,96,false>;
    static constexpr std::size_t maximum_samples=Tile::maximum_samples;
#ifndef DATAPUMP_TEMPORAL_PLAN_CACHE_ENTRIES
#define DATAPUMP_TEMPORAL_PLAN_CACHE_ENTRIES 4
#endif
    using PlanCache=ScheduledTemporalPlanCacheT<typename Tile::Plan,maximum_samples,DATAPUMP_TEMPORAL_PLAN_CACHE_ENTRIES>;
#ifndef DATAPUMP_SCHED_CROSSBANK_PLAN_CACHE_ENTRIES
#define DATAPUMP_SCHED_CROSSBANK_PLAN_CACHE_ENTRIES 4
#endif
    // Independent internal experiment capacity. Original per-bank cache stays
    // at its preceding capacity; exact sizeof of this type is charged by Live.
    using SharedPlanCache=ScheduledTemporalPlanCacheT<typename Tile::Plan,maximum_samples,DATAPUMP_SCHED_CROSSBANK_PLAN_CACHE_ENTRIES>;
    void bind_shared_plan_cache(SharedPlanCache* pool)noexcept {
        // Includes same-pointer/null rebind at every joined-push boundary.
        release_outer_plan();shared_plan_cache_=pool;
    }
    ~ScheduledTemporalTemplates(){release_outer_plan();}
    // This allocation is SEPARATE from the original Temporal object/worker
    // selection. Actual compiler sizeof is used, never a guessed slab count.
    static constexpr std::size_t plan_cache_charge=sizeof(PlanCache)+PlanCache::lookup_callee_allowance;
    static constexpr std::uint16_t absent=std::numeric_limits<std::uint16_t>::max();
    static constexpr std::size_t callee_allowance=Tile::external_callee_allowance;
    struct Statistics {
        std::uint64_t attempts=0,ready=0,fallbacks=0,consumed=0,plan_reuses=0,normalized_reuses=0;
        Tile::Statistics numeric;
        PlanCache::Statistics plan_cache; // retired pool counts; live counts remain in pool
        Statistics& operator+=(const Statistics& b) {
            attempts+=b.attempts;ready+=b.ready;fallbacks+=b.fallbacks;consumed+=b.consumed;plan_reuses+=b.plan_reuses;normalized_reuses+=b.normalized_reuses;
            numeric.weight_rows+=b.numeric.weight_rows;numeric.weight_callbacks+=b.numeric.weight_callbacks;
            numeric.prepares+=b.numeric.prepares;numeric.value_pairs+=b.numeric.value_pairs;
            numeric.packed_doubles+=b.numeric.packed_doubles;numeric.shape_terms+=b.numeric.shape_terms;
            numeric.output_pairs+=b.numeric.output_pairs;numeric.limiter_pairs+=b.numeric.limiter_pairs;
            plan_cache+=b.plan_cache;return *this;
        }
    } statistics;
    using Fp32Execution=ScheduledTemporalFp32Execution;
    static constexpr std::size_t fp32_charge=sizeof(Fp32Execution);
    bool fp32_attempted()const noexcept{return fp32_attempted_;}
    void mark_fp32_attempted()noexcept{fp32_attempted_=true;}
    void allocate_fp32(double limit=1e-5){if(!fp32_)fp32_=std::make_unique<Fp32Execution>(tile_,limit);}
    std::size_t fp32_bytes()const noexcept{return fp32_?fp32_charge:0;}
    Fp32Execution::Counters fp32_counters()const{return fp32_?fp32_->counters():Fp32Execution::Counters{};}
    void drop_fp32()noexcept {
        release_outer_plan();fp32_.reset();tile_.reset_binding();ready_=false;
        if(plan_cache_ && !plan_cache_->try_drop_payload())std::terminate();
    }
    static constexpr std::size_t fp32_local_payload_charge=PlanCache::template payload_charge<typename Fp32Execution::Native::PackedPlan>();
    bool fp32_local_payload_pending()const noexcept {
        return fp32_ && plan_cache_ && !fp32_->local_payload_attempted();
    }
    void mark_fp32_local_payload_attempted()noexcept{if(fp32_)fp32_->mark_local_payload_attempted();}
    bool allocate_fp32_local_payload(){return plan_cache_ && plan_cache_->template allocate_payload<typename Fp32Execution::Native::PackedPlan>();}
    bool plan_cache_attempted()const noexcept{return plan_cache_attempted_;}
    void mark_plan_cache_attempted()noexcept{plan_cache_attempted_=true;}
    void allocate_plan_cache(){if(!plan_cache_)plan_cache_=std::make_unique<PlanCache>();}
    std::size_t plan_cache_bytes()const noexcept{return plan_cache_?plan_cache_charge+plan_cache_->payload_bytes():0;}
    PlanCache::Statistics plan_cache_statistics()const noexcept {
        auto result=statistics.plan_cache;if(plan_cache_)result+=plan_cache_->statistics;return result;
    }
    void drop_plan_cache()noexcept {
        // Joined boundary: detach numeric bindings and release retained outer
        // pins BEFORE destroying storage. Detached raw outputs stay usable.
        release_outer_plan();
        if(plan_cache_){statistics.plan_cache+=plan_cache_->statistics;plan_cache_.reset();}
    }
    static bool supported(){
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
        return Tile::arithmetic_supported();
#else
        return false; // no qualified portable noinline/callee-frame contract
#endif
    }
    void begin(std::size_t hypothesis,std::uint64_t block) {
        release_outer_plan();
        tile_.reset_binding();if(fp32_)fp32_->reset_request();
        hypothesis_=hypothesis;block_=block;attempt_begin_=attempt_end_=0;ready_=false;
    }
    bool attempted(std::size_t hypothesis,std::uint64_t block,std::uint64_t phase,
                   std::uint64_t first_chip,std::uint64_t observed)const {
        return hypothesis==hypothesis_ && block==block_ && phase==phase_ && first_chip==first_chip_ &&
            attempt_begin_<=observed && observed<attempt_end_;
    }
    void start(std::uint64_t phase,std::uint64_t first_chip,std::uint64_t observed) {
        release_outer_plan();
        tile_.reset_binding();if(fp32_)fp32_->reset_request();
        phase_=phase;first_chip_=first_chip;attempt_begin_=attempt_end_=observed;
        count_=0;ready_=false;row_.fill(absent);selected_.reset();++statistics.attempts;
    }
    // The exporter supplies ONLY original uncached, nonzero-gain raw visits.
    // Its physical-row mapping is independent from the shared public Plan.
    bool append(std::size_t physical_offset,double exact_within) {
        if(physical_offset>=maximum_samples || count_>=maximum_samples || row_[physical_offset]!=absent)return false;
        row_[physical_offset]=static_cast<std::uint16_t>(count_);selected_.set(physical_offset);coordinates_[count_++]=exact_within;return true;
    }
    void end(std::uint64_t observed){attempt_end_=observed;}
    std::size_t exported_points()const noexcept{return count_;}
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool prepare(PatternCode& pattern,const Config& config,std::stop_token stop={},bool retain_outer=false) {
        release_outer_plan();
        ready_=false;tile_.reset_binding();if(fp32_)fp32_->reset_request();
        ++statistics.numeric.prepares; // caller always performs one explicit context restore after return
#if !defined(_MSC_VER) && !defined(__GNUC__) && !defined(__clang__)
        (void)pattern;(void)config;(void)stop;++statistics.fallbacks;return false;
#else
        if(count_<4 || !supported()){++statistics.fallbacks;return false;}
        // Exact integer-chip translation of the whole ORIGINAL vector. Under
        // nine-chip margins, C1..1023 and s<=2^31, every nonzero callback has the
        // same original argument/weight bits after integer private relabeling.
        // Rounded raw-loop bounds may differ only by exterior ZERO terms.
        ::datapump::detail::cpu_profile::Scope cpu_coordinates(::datapump::detail::cpu_profile::Stage::temporal_coordinates,true);
        const auto normalized_plan=scheduled_temporal_normalization(
            {coordinates_.data(),count_},pattern.symbol_samples(),pattern.chip_samples());
        const bool normalized=normalized_plan.eligible;
        const auto translate=normalized_plan.chip_translation;
        for(std::size_t i=0;i<count_;++i)
            key_coordinates_[i]=coordinates_[i]-static_cast<double>(pattern.chip_samples()*translate);
        bool same=plan_.valid && selected_==planned_selected_ && plan_.samples==count_ && plan_.symbol_samples==pattern.symbol_samples() &&
            plan_.chip_samples==pattern.chip_samples();
        for(std::size_t i=0;same && i<count_;++i)
            same=std::bit_cast<std::uint64_t>(key_coordinates_[i])==std::bit_cast<std::uint64_t>(planned_coordinates_[i]);
        cpu_coordinates.finish();
        const typename Tile::Lane binding{&pattern,&config,phase_,first_chip_,0};
        const auto prepare_with=[&](auto* pool,auto* retained) {
            // Precision is independent from exact Plan/gather reuse. Force64
            // and all unsupported policies retain the existing FP64 Tile.
            if(fp32_ && (config.search_arithmetic==SearchArithmetic::default_mode ||
                         config.search_arithmetic==SearchArithmetic::fp32)) {
                fp32_->begin_request({key_coordinates_.data(),count_},pattern.symbol_samples(),pattern.chip_samples());
                struct RequestGuard {Fp32Execution& value;~RequestGuard(){value.end_request();}} end{*fp32_};
                const auto result=scheduled_prepare_borrowed_plan(*fp32_,pool,plan_,same,
                    {key_coordinates_.data(),count_},pattern.symbol_samples(),pattern.chip_samples(),
                    translate,binding,statistics.numeric,stop,retain_outer?retained:nullptr);
                tile_.statistics=fp32_->statistics;return result;
            }
            return scheduled_prepare_borrowed_plan(tile_,pool,plan_,same,
                {key_coordinates_.data(),count_},pattern.symbol_samples(),pattern.chip_samples(),
                translate,binding,statistics.numeric,stop,retain_outer?retained:nullptr);
        };
        const auto outcome=shared_plan_cache_?prepare_with(shared_plan_cache_,&shared_outer_lease_):
            prepare_with(plan_cache_.get(),&local_outer_lease_);
        if(outcome.plan_reused){++statistics.plan_reuses;if(normalized)++statistics.normalized_reuses;}
        // Pool misses build directly into a leased row; the legacy owned
        // fallback key changes ONLY when that fallback was actually rebuilt.
        if(outcome.fallback_rebuilt) {
            for(std::size_t i=0;i<count_;++i)planned_coordinates_[i]=key_coordinates_[i];planned_selected_=selected_;
        }
        ready_=outcome.ready;
        if(ready_ && retain_outer) {
            // Owned fallback remains in this object; pooled Plans are pinned
            // through all copied row consumers. No whole Plan copy is made.
            outer_plan_=outcome.pooled?(shared_outer_lease_?shared_outer_lease_.validated_plan():
                local_outer_lease_.validated_plan()):&plan_;
            if(outer_plan_)outer_first_local_=outer_plan_->first_local+translate;
        }
        statistics.numeric.prepares+=tile_.statistics.prepares;
        statistics.numeric.value_pairs+=tile_.statistics.value_pairs;
        statistics.numeric.packed_doubles+=tile_.statistics.packed_doubles;
        statistics.numeric.shape_terms+=tile_.statistics.shape_terms;
        statistics.numeric.output_pairs+=tile_.statistics.output_pairs;
        if(ready_)++statistics.ready;else ++statistics.fallbacks;
        return ready_;
#endif
    }
    bool raw(std::size_t hypothesis,std::uint64_t block,std::uint64_t phase,std::uint64_t first_chip,
             std::uint64_t observed,std::size_t physical_offset,double exact_within,
             std::array<std::complex<double>,2>& value) {
        raw_row_=absent;
        if(!ready_ || !attempted(hypothesis,block,phase,first_chip,observed) || physical_offset>=maximum_samples)return false;
        const auto row=row_[physical_offset];
        if(row==absent || std::bit_cast<std::uint64_t>(coordinates_[row])!=std::bit_cast<std::uint64_t>(exact_within))return false;
        if(fp32_ && fp32_->attempted()) {
            if(!fp32_->raw(row,value))return false;
        } else value=tile_.raw(row,0);
        raw_row_=row;raw_offset_=physical_offset;raw_observed_=observed;
        ++statistics.consumed;return true;
    }
    // Only the just-copied ORIGINAL raw row may supply outer pulse chronology.
    // Consume performs the unchanged gain product and private ring callback.
    template<class Consume>
    bool outer_pulses(std::size_t hypothesis,std::uint64_t block,std::uint64_t phase,
            std::uint64_t first_chip,std::uint64_t observed,std::size_t physical_offset,
            double exact_within,Consume consume) {
        if(!ready_ || !outer_plan_ || raw_row_==absent || physical_offset!=raw_offset_ ||
           observed!=raw_observed_ || !attempted(hypothesis,block,phase,first_chip,observed) ||
           physical_offset>=maximum_samples || row_[physical_offset]!=raw_row_ ||
           raw_row_>=count_ || raw_row_>=outer_plan_->samples ||
           std::bit_cast<std::uint64_t>(coordinates_[raw_row_])!=std::bit_cast<std::uint64_t>(exact_within))return false;
        const auto& row=outer_plan_->rows[raw_row_];
        // Full structural validity was certified at build/gather. Check every
        // scalar label range BEFORE invoking the first callback/fallback.
        const auto last=row.nonzero?std::bit_width(row.nonzero)-1U:0U;
        if(row.first_offset>std::numeric_limits<std::uint64_t>::max()-outer_first_local_ ||
           last>std::numeric_limits<std::uint64_t>::max()-outer_first_local_-row.first_offset)return false;
        const auto first=outer_first_local_+row.first_offset;
        const auto chips=outer_plan_->symbol_samples/outer_plan_->chip_samples+
            (outer_plan_->symbol_samples%outer_plan_->chip_samples!=0);
        if(row.nonzero && first+last>=chips)return false;
        raw_row_=absent; // one-shot chronology ticket; invalid even if callback throws
        for(std::size_t p=0;p<row.taps;++p)if(row.weight[p]!=0)consume(first+p,row.weight[p]);
        return true;
    }
    bool outer_plan_retained()const noexcept{return outer_plan_!=nullptr;}
    void release_outer_plan()noexcept {
        // Numeric borrowers detach BEFORE row pin release, including native
        // diagnostics. Outputs themselves own storage and survive this step.
        if(fp32_ && fp32_->attempted())fp32_->detach_evaluated_plan();
        tile_.detach_evaluated_plan();outer_plan_=nullptr;raw_row_=absent;
        local_outer_lease_.reset();shared_outer_lease_.reset();
    }
private:
    Tile tile_;
    typename Tile::Plan plan_; // exact public coordinate plan; no private coefficients
    std::array<double,maximum_samples> coordinates_{},planned_coordinates_{},key_coordinates_{};
    std::array<std::uint16_t,maximum_samples> row_{};
    std::bitset<maximum_samples> selected_,planned_selected_;
    std::size_t hypothesis_=0,count_=0;
    std::uint64_t block_=0,phase_=0,first_chip_=0,attempt_begin_=0,attempt_end_=0;
    bool ready_=false,plan_cache_attempted_=false,fp32_attempted_=false;
    SharedPlanCache* shared_plan_cache_=nullptr; // scoped to synchronous push
    std::unique_ptr<PlanCache> plan_cache_; // never moves after attachment
    // All numeric payload/counters are optional heap storage. Temporal itself
    // is already optional; ordinary correlator/Work/Options sizes stay fixed.
    std::unique_ptr<Fp32Execution> fp32_;
    // AFTER owned pool/native members: reverse destruction unpins first.
    typename PlanCache::Lease local_outer_lease_;
    typename SharedPlanCache::Lease shared_outer_lease_;
    const typename Tile::Plan* outer_plan_=nullptr;
    std::uint64_t outer_first_local_=0,raw_observed_=0;
    std::size_t raw_offset_=0;
    std::uint16_t raw_row_=absent;
};
} // namespace datapump::modem::detail
