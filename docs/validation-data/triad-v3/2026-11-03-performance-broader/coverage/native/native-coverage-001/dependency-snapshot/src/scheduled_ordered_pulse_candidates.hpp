#pragma once
#include "scheduled_work_profile.hpp"
#include "family_simd.hpp"
// Native binary64 original-order template primitive. Availability is explicit:
// the build must assert separate rounding; unsupported compilers/fenv retain
// original shaped_values. No unsafe arithmetic is consumed on fallback.
#include "datapump/pattern_pulse.hpp"
#include <array>
#include <bit>
#include <bitset>
#include <cfenv>
#include <cfloat>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <type_traits>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#if !defined(DATAPUMP_ORDERED_COMPONENT_PORTABLE_ONLY) && \
    (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DATAPUMP_ORDERED_COMPONENT_AVX2_COMPILED 1
#else
#define DATAPUMP_ORDERED_COMPONENT_AVX2_COMPILED 0
#endif

namespace datapump::modem::detail {
class ScheduledTemporalFp32Execution;

template<class Plan,std::size_t MaximumSamples,std::size_t Entries>
class ScheduledTemporalPlanCacheT;

// Candidate-axis shape only. PCM, physical gates/carriers, whole/chip/quarter/
// differential/outer Fits, admission, trials and publication remain with caller.
// Output is a bounded SOURCE-template tile, not shifted PCM or symbol history.
template<std::size_t CandidateLimit=16,std::size_t SampleLimit=128,std::size_t CoefficientLimit=64,
         bool OwnPlan=true>
class ScheduledOrderedPulseCandidatesT {
    friend class ScheduledTemporalFp32Execution; // optional native facade; exact gather unchanged
public:
    static constexpr std::size_t maximum_lanes=CandidateLimit,maximum_samples=SampleLimit,
        maximum_taps=18,maximum_coefficients=CoefficientLimit;
    static_assert(maximum_taps<=std::numeric_limits<std::uint8_t>::max());
    // Existing explicit oracle values remain unchanged. Automatic is used
    // only at the optional Temporal integration seam.
    enum class Backend {scalar=0,sse2=1,automatic=2,avx2=3};
    static constexpr bool component_avx2_compiled=
        DATAPUMP_ORDERED_COMPONENT_AVX2_COMPILED && maximum_lanes==1;
    static constexpr bool candidate_avx2_compiled=
        DATAPUMP_ORDERED_COMPONENT_AVX2_COMPILED && maximum_lanes>=4 && maximum_lanes%4==0;
    static constexpr std::size_t component_storage_bytes=
        component_avx2_compiled?maximum_coefficients*4*sizeof(double):0;
    static bool component_avx2_available()noexcept {
        if constexpr(component_avx2_compiled)
            return family_simd::available(family_simd::Backend::avx2);
        else return false;
    }

    static bool candidate_avx2_available()noexcept {
        if constexpr(candidate_avx2_compiled)
            return family_simd::available(family_simd::Backend::avx2);
        else return false;
    }

    struct Statistics {
        std::uint64_t weight_rows=0,weight_callbacks=0,prepares=0,value_pairs=0;
        std::uint64_t packed_doubles=0,shape_terms=0,output_pairs=0,limiter_pairs=0;
    };
    struct PulseRow {
        std::array<double,maximum_taps> weight{};
        std::uint32_t nonzero=0;
        // Derived public metadata. Built once and checked at every untrusted
        // Plan boundary; fits the existing uint32->size_t padding on64-bit ABI.
        std::uint8_t nonzero_count=0;
        std::size_t first_offset=0,taps=0;
    };
    struct Plan {
        std::array<PulseRow,maximum_samples> rows{};
        std::uint64_t first_local=0,symbol_samples=0,chip_samples=0;
        std::size_t samples=0,coefficients=0;
        std::bitset<maximum_coefficients> active_taps;
        bool valid=false;

        // Original table/lerp/partial-chip weights; one callback traversal per
        // adjacent sample. No canonicalization or floating argument aliasing.
        // Caller can separately copy qualified exact cache rows into this owned
        // plan. Never retain direct-mapped cache View spans across later gets.
        bool build_original(std::span<const double> coordinates,std::uint64_t symbol,
                std::uint64_t chip,Statistics& statistics,std::stop_token stop={}) {
            ::datapump::detail::cpu_profile::Scope cpu_plan_build(::datapump::detail::cpu_profile::Stage::temporal_plan_build,true);
            for(auto& row:rows)row={};
            first_local=symbol_samples=chip_samples=0;samples=coefficients=0;
            active_taps.reset();valid=false;
            if(coordinates.empty() || coordinates.size()>maximum_samples || !symbol || !chip)
                return false;
            symbol_samples=symbol;chip_samples=chip;
            std::array<std::uint64_t,maximum_samples> first{};
            auto minimum=std::numeric_limits<std::uint64_t>::max();
            std::uint64_t maximum=0;
            for(std::size_t sample=0;sample<coordinates.size();++sample) {
                if((sample&31U)==0 && stop.stop_requested())throw Error("ordered pulse planning cancelled");
                bool found=false,overflow=false;
                ++statistics.weight_rows;
                pattern_pulse_each(coordinates[sample],symbol,chip,
                    [&](std::uint64_t local,double weight) {
                        ++statistics.weight_callbacks;
                        if(!found){first[sample]=local;found=true;}
                        const auto offset=local-first[sample];
                        if(offset>=maximum_taps){overflow=true;return;}
                        rows[sample].weight[static_cast<std::size_t>(offset)]=weight;
                        rows[sample].nonzero|=std::uint32_t{1}<<offset;
                        // pattern_pulse_each visits each local index once and
                        // emits only nonzero weights, in ascending order.
                        ++rows[sample].nonzero_count;
                        rows[sample].taps=std::max(rows[sample].taps,static_cast<std::size_t>(offset+1));
                        minimum=std::min(minimum,local);maximum=std::max(maximum,local);
                    });
                if(overflow)return false;
            }
            samples=coordinates.size();
            if(minimum==std::numeric_limits<std::uint64_t>::max()) {
                // Selected zero-basis observations still belong to caller's E/N.
                valid=true;return true;
            }
            if(maximum-minimum>=maximum_coefficients)return false;
            first_local=minimum;coefficients=static_cast<std::size_t>(maximum-minimum+1);
            for(std::size_t sample=0;sample<samples;++sample) {
                auto& row=rows[sample];if(!row.nonzero)continue;
                row.first_offset=static_cast<std::size_t>(first[sample]-minimum);
                for(std::size_t p=0;p<row.taps;++p)if(row.nonzero&(std::uint32_t{1}<<p))
                    active_taps.set(row.first_offset+p);
            }
            valid=true;return true;
        }
        bool well_formed()const {
            if(!valid || !samples || samples>maximum_samples || !symbol_samples || !chip_samples ||
               coefficients>maximum_coefficients)return false;
            std::bitset<maximum_coefficients> active;
            for(std::size_t n=0;n<samples;++n) {
                const auto& row=rows[n];
                if(row.taps>maximum_taps || row.first_offset>coefficients ||
                   row.taps>coefficients-row.first_offset)return false;
                std::uint32_t mask=0;std::uint8_t nonzero_count=0;
                for(std::size_t p=0;p<maximum_taps;++p) {
                    if(!std::isfinite(row.weight[p]))return false;
                    if(row.weight[p]!=0) {
                        if(p>=row.taps)return false;
                        mask|=std::uint32_t{1}<<p;active.set(row.first_offset+p);++nonzero_count;
                    }
                }
                if(mask!=row.nonzero || nonzero_count!=row.nonzero_count)return false;
            }
            return active==active_taps;
        }
        // Proof gate for independently constructed per-lane original plans.
        // Local chip base MAY differ. Shared rows do NOT imply private equality.
        bool same_weights(const Plan& b)const {
            if(!well_formed() || !b.well_formed() || symbol_samples!=b.symbol_samples || chip_samples!=b.chip_samples ||
               samples!=b.samples || coefficients!=b.coefficients || active_taps!=b.active_taps)
                return false;
            for(std::size_t n=0;n<samples;++n) {
                if(rows[n].nonzero!=b.rows[n].nonzero || rows[n].first_offset!=b.rows[n].first_offset ||
                   rows[n].taps!=b.rows[n].taps)return false;
                for(std::size_t p=0;p<rows[n].taps;++p)
                    if(std::bit_cast<std::uint64_t>(rows[n].weight[p])!=
                       std::bit_cast<std::uint64_t>(b.rows[n].weight[p]))return false;
            }
            return true;
        }
    };
    struct Lane {
        PatternCode* code=nullptr; // Config/key/stream_epoch remain in this code.
        const Config* original_config=nullptr; // exact immutable constructor Config, root-owned
        std::uint64_t phase_samples=0,first_chip=0,first_local=0;
        // first_chip is target_index*chips_per_symbol, including current/carry.
        void activate(std::stop_token stop={})const {
            code->set_stream_phase_samples(phase_samples);
            code->prepare_symbol(first_chip,stop);
        }
    };

    ScheduledOrderedPulseCandidatesT()=default;
    ScheduledOrderedPulseCandidatesT(const ScheduledOrderedPulseCandidatesT&) requires(OwnPlan)=default;
    ScheduledOrderedPulseCandidatesT& operator=(const ScheduledOrderedPulseCandidatesT&) requires(OwnPlan)=default;
    ScheduledOrderedPulseCandidatesT(ScheduledOrderedPulseCandidatesT&&) requires(OwnPlan)=default;
    ScheduledOrderedPulseCandidatesT& operator=(ScheduledOrderedPulseCandidatesT&&) requires(OwnPlan)=default;
    // A live borrowed pointer cannot escape a local pin by copying this Tile.
    ScheduledOrderedPulseCandidatesT(const ScheduledOrderedPulseCandidatesT&) requires(!OwnPlan)=delete;
    ScheduledOrderedPulseCandidatesT& operator=(const ScheduledOrderedPulseCandidatesT&) requires(!OwnPlan)=delete;
    ScheduledOrderedPulseCandidatesT(ScheduledOrderedPulseCandidatesT&&) requires(!OwnPlan)=delete;
    ScheduledOrderedPulseCandidatesT& operator=(ScheduledOrderedPulseCandidatesT&&) requires(!OwnPlan)=delete;

    Statistics statistics;

    // Root must reserve sizeof(object), any Plan candidates and a separately
    // qualified callee-frame allowance before creation. Do not put on the
    // existing4KiB common frame. No heap allocations occur inside this object.
    static constexpr std::size_t external_callee_allowance=maximum_samples*sizeof(std::uint64_t)+2048;
    // Includes build_original's explicit first-index array plus proposed local
    // allowance; NOT a measured/transitive ABI stack certificate. PatternCode
    // setup/mapping scratch/allocations retain their separate original charge.
    static bool arithmetic_supported() {
#if !defined(DATAPUMP_ORDERED_PULSE_CONTRACT_OFF) || defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0)
        return false;
#endif
        if(!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits!=53 ||
           FLT_EVAL_METHOD!=0 || std::fegetround()!=FE_TONEAREST)return false;
#if defined(__SSE2__)
        // Match ordinary gradual underflow and nearest SSE rounding.
        return (_mm_getcsr()&((1U<<15)|(1U<<6)|(3U<<13)))==0;
#else
        return false; // native prototype is intentionally x86-SSE2 bounded
#endif
    }
    static bool worth_measuring(std::size_t lanes,std::size_t samples) {
        // Engineering minimum, not a measured crossover or production policy.
        return lanes>=4 && lanes<=maximum_lanes && samples>=4 && samples<=maximum_samples;
    }
    // Default API retains an independent owned copy. The explicit false
    // specialization has NO embedded Plan and never silently borrows here.
    bool gather(std::span<const Lane> lanes,const Plan& plan,std::stop_token stop={}) {
        reset_binding();
        if constexpr(OwnPlan)return gather_impl(lanes,plan,false,stop);
        else return false;
    }
    // Caller pins an immutable Plan until evaluate returns, then either resets
    // on failure or detaches evaluated outputs BEFORE releasing that Plan.
    bool gather_borrowed(std::span<const Lane> lanes,const Plan& plan,std::stop_token stop={}) requires(!OwnPlan) {
        reset_binding();return gather_impl(lanes,plan,true,stop);
    }
    // INTERNAL checked certificate: only this exact cache's move-only Lease
    // can enter. Cache publishes READY only after full Plan::well_formed; its
    // row stays immutable while this CONSUMED lease pins it. Original owned
    // and untrusted borrowed APIs retain every structural validation.
    template<std::size_t PlanSamples,std::size_t Entries>
    bool gather_evaluate_leased(std::span<const Lane> lanes,
            typename ScheduledTemporalPlanCacheT<Plan,PlanSamples,Entries>::Lease lease,
            Backend backend,std::stop_token stop={}) requires(!OwnPlan) {
        return gather_evaluate_pinned<PlanSamples,Entries>(lanes,lease,backend,stop);
    }
    // Caller retains this exact immutable row pin through later public outer
    // callbacks. Only output/scalar metadata survives in Tile; the borrowed
    // numeric pointer is still detached before returning on every path.
    template<std::size_t PlanSamples,std::size_t Entries>
    bool gather_evaluate_pinned(std::span<const Lane> lanes,
            const typename ScheduledTemporalPlanCacheT<Plan,PlanSamples,Entries>::Lease& lease,
            Backend backend,std::stop_token stop={}) requires(!OwnPlan) {
        reset_binding();
        const auto* plan=lease.validated_plan();if(!plan)return false;
        // Caller holds the immutable pin throughout this synchronous call.
        // The local guard clears every numeric borrowed pointer before return
        // or exception; the caller may then retain or release its public row.
        struct BindingGuard {
            ScheduledOrderedPulseCandidatesT& tile;bool success=false;
            ~BindingGuard()noexcept {
                if(success)tile.detach_evaluated_plan();else tile.reset_binding();
            }
        } clear{*this};
        clear.success=gather_impl(lanes,*plan,true,stop,true) && evaluate(backend,stop);
        return clear.success;
    }
    void reset_binding()noexcept {
        borrowed_plan_=nullptr;binding_={};lanes_=evaluated_samples_=0;gathered_=evaluated_=false;
        evaluated_backend_=Backend::scalar;
    }
    void clear_borrowed()noexcept {reset_binding();}
    void detach_evaluated_plan()noexcept {
        if(evaluated_){borrowed_plan_=nullptr;gathered_=false;}
        else reset_binding();
    }
private:
    struct BorrowFailureGuard {
        ScheduledOrderedPulseCandidatesT& tile;bool borrowed=false,succeeded=false;
        ~BorrowFailureGuard()noexcept {if(borrowed && !succeeded)tile.reset_binding();}
    };
    const Plan* bound_plan()const noexcept {
        if(borrowed_plan_)return borrowed_plan_;
        if constexpr(OwnPlan)return &owned_plan_;
        else return nullptr;
    }
    bool gather_impl(std::span<const Lane> lanes,const Plan& plan,bool borrowed,std::stop_token stop,
                     bool prevalidated=false) {
        ::datapump::detail::cpu_profile::Scope cpu_gather(::datapump::detail::cpu_profile::Stage::temporal_gather,true);
        if(!arithmetic_supported() || (!prevalidated && !plan.well_formed()) ||
           lanes.empty() || lanes.size()>maximum_lanes)return false;
        for(const auto& lane:lanes) {
            if(!lane.code || !lane.original_config ||
               lane.original_config->spreading_mode!=SpreadingMode::pattern ||
               !pattern_pulse_enabled(*lane.original_config) ||
               lane.code->chip_samples()!=plan.chip_samples ||
               lane.code->symbol_samples()!=plan.symbol_samples)return false;
            // Only taps actually consumed by at least one original sample are
            // checked/looked up. Padded zero taps must not add overflow errors
            // or private stream reads that original zero-skipping never made.
            for(std::size_t p=0;p<plan.coefficients;++p)if(plan.active_taps.test(p)) {
                if(p>std::numeric_limits<std::uint64_t>::max()-lane.first_local)return false;
                const auto local=lane.first_local+p;
                if(local>=lane.code->chips_per_symbol() ||
                   local>std::numeric_limits<std::uint64_t>::max()-lane.first_chip)return false;
            }
        }
        if(borrowed)borrowed_plan_=&plan;
        else {if constexpr(OwnPlan)owned_plan_=plan;else return false;}
        BorrowFailureGuard clear{*this,borrowed};lanes_=lanes.size();
        for(std::size_t j=0;j<lanes_;++j) {
            if(stop.stop_requested())throw Error("ordered pulse gather cancelled");
            binding_[j]=lanes[j];binding_[j].activate(stop);++statistics.prepares;
            for(std::size_t p=0;p<plan.coefficients;++p)if(plan.active_taps.test(p)) {
                if((p&7U)==0 && stop.stop_requested())throw Error("ordered pulse gather cancelled");
                const auto value=binding_[j].code->values(binding_[j].first_chip+binding_[j].first_local+p,0);
                ++statistics.value_pairs;
                for(unsigned bit=0;bit<2;++bit) {
                    if(!std::isfinite(value[bit].real()) || !std::isfinite(value[bit].imag()))return false;
                    coefficient_[2*bit][p][j]=value[bit].real();
                    coefficient_[2*bit+1][p][j]=value[bit].imag();
                    statistics.packed_doubles+=2;
                }
            }
        }
        gathered_=true;clear.succeeded=true;return true;
    }
    static Backend resolve_backend(Backend requested) {
        if(requested==Backend::avx2) {
            // Explicit unsupported requests leave existing outputs/bindings
            // untouched; no guard, Scope or arithmetic mutation has started.
            if(!component_avx2_available() && !candidate_avx2_available())throw Error("ordered pulse AVX2 unavailable");
            return Backend::avx2;
        }
        if(requested!=Backend::automatic)return requested;
        if(component_avx2_available() || candidate_avx2_available())return Backend::avx2;
#if defined(__SSE2__)
        return Backend::sse2;
#else
        return Backend::scalar;
#endif
    }
    void pack_components(const Plan& plan,std::stop_token stop) requires(component_avx2_compiled) {
        for(std::size_t p=0;p<plan.coefficients;++p) {
            if((p&31U)==0 && stop.stop_requested())throw Error("ordered pulse component packing cancelled");
            if(plan.active_taps.test(p)) {
                for(unsigned plane=0;plane<4;++plane)
                    component_coefficient_[p][plane]=coefficient_[plane][p][0];
                statistics.packed_doubles+=4;
            }
        }
    }
#if DATAPUMP_ORDERED_COMPONENT_AVX2_COMPILED
    // One ISA boundary per Tile, not per tap or row. Every component keeps
    // its original ascending-tap binary64 multiply-then-add recurrence.
    __attribute__((target("avx2"),noinline))
    void evaluate_components_avx2(const Plan& plan,std::stop_token stop) requires(component_avx2_compiled) {
        for(std::size_t n=0;n<plan.samples;++n) {
            if(stop.stop_requested())throw Error("ordered pulse evaluate cancelled");
            auto sum=_mm256_setzero_pd();
            const auto& row=plan.rows[n];
            for(std::size_t p=0;p<row.taps;++p) {
                const auto w=row.weight[p];if(w==0)continue;
                const auto weight=_mm256_set1_pd(w);
                const auto product=_mm256_mul_pd(weight,
                    _mm256_loadu_pd(component_coefficient_[row.first_offset+p].data()));
                sum=_mm256_add_pd(sum,product);
            }
            // A32-byte row result is the only numeric temporary. Coefficient
            // packing is Tile-owned and already part of sizeof accounting.
            std::array<double,4> result;_mm256_storeu_pd(result.data(),sum);
            for(unsigned plane=0;plane<4;++plane)output_[n][plane][0]=result[plane];
            ++statistics.output_pairs;
            statistics.shape_terms+=row.nonzero_count;
        }
    }
    // Optional four-candidate native path. Every independent component keeps
    // its own original ascending nonzero-tap multiply/add recurrence. No FMA,
    // horizontal sum, changed source weights or global AVX2 requirement.
    __attribute__((target("avx2"),noinline))
    void evaluate_candidates_avx2(const Plan& plan,std::stop_token stop) requires(candidate_avx2_compiled) {
        for(std::size_t n=0;n<plan.samples;++n) {
            if(stop.stop_requested())throw Error("ordered pulse candidate AVX2 cancelled");
            std::size_t lane=0;
            for(;lane+3<lanes_;lane+=4) {
                __m256d sum[4]{_mm256_setzero_pd(),_mm256_setzero_pd(),_mm256_setzero_pd(),_mm256_setzero_pd()};
                const auto& row=plan.rows[n];
                for(std::size_t p=0;p<row.taps;++p) {
                    const auto w=row.weight[p];if(w==0)continue;const auto weight=_mm256_set1_pd(w);
                    for(unsigned plane=0;plane<4;++plane) {
                        const auto product=_mm256_mul_pd(weight,_mm256_loadu_pd(&coefficient_[plane][row.first_offset+p][lane]));
                        sum[plane]=_mm256_add_pd(sum[plane],product);
                    }
                }
                for(unsigned plane=0;plane<4;++plane)_mm256_storeu_pd(&output_[n][plane][lane],sum[plane]);
            }
            // Two-row tails use the existing SSE2 width; do not turn a
            // useful two-candidate cohort into an all-scalar AVX2 tail.
            for(;lane+1<lanes_;lane+=2) {
                __m128d sum[4]{_mm_setzero_pd(),_mm_setzero_pd(),_mm_setzero_pd(),_mm_setzero_pd()};
                const auto& row=plan.rows[n];
                for(std::size_t p=0;p<row.taps;++p) {
                    const auto w=row.weight[p];if(w==0)continue;const auto weight=_mm_set1_pd(w);
                    for(unsigned plane=0;plane<4;++plane) {
                        const auto product=_mm_mul_pd(weight,_mm_loadu_pd(&coefficient_[plane][row.first_offset+p][lane]));
                        sum[plane]=_mm_add_pd(sum[plane],product);
                    }
                }
                for(unsigned plane=0;plane<4;++plane)_mm_storeu_pd(&output_[n][plane][lane],sum[plane]);
            }
            // Incomplete cohorts remain valid and preserve portable arithmetic.
            for(;lane<lanes_;++lane) {
                std::array<double,4> sum{};const auto& row=plan.rows[n];
                for(std::size_t p=0;p<row.taps;++p) {
                    const auto w=row.weight[p];if(w==0)continue;
                    for(unsigned plane=0;plane<4;++plane)sum[plane]+=w*coefficient_[plane][row.first_offset+p][lane];
                }
                for(unsigned plane=0;plane<4;++plane)output_[n][plane][lane]=sum[plane];
            }
            statistics.output_pairs+=lanes_;statistics.shape_terms+=lanes_*plan.rows[n].nonzero_count;
        }
    }
#endif
public:
    bool evaluate(Backend backend,std::stop_token stop={}) {
        backend=resolve_backend(backend); // feature check once, before mutation
        ::datapump::detail::cpu_profile::Scope cpu_evaluate(::datapump::detail::cpu_profile::Stage::temporal_evaluate,true);
        evaluated_=false;evaluated_samples_=0;
        evaluated_backend_=Backend::scalar;
        BorrowFailureGuard clear{*this,!OwnPlan};
        if(!gathered_ || !arithmetic_supported())return false;
        const auto* bound=bound_plan();if(!bound)return false;
        const auto& plan=*bound;
#if DATAPUMP_ORDERED_COMPONENT_AVX2_COMPILED
        if constexpr(component_avx2_compiled)if(backend==Backend::avx2) {
            pack_components(plan,stop);
            evaluate_components_avx2(plan,stop);
            evaluated_samples_=plan.samples;evaluated_=true;
            evaluated_backend_=Backend::avx2;clear.succeeded=true;return true;
        }
        if constexpr(candidate_avx2_compiled)if(backend==Backend::avx2) {
            evaluate_candidates_avx2(plan,stop);
            evaluated_samples_=plan.samples;evaluated_=true;
            evaluated_backend_=Backend::avx2;clear.succeeded=true;return true;
        }
#endif
        for(std::size_t n=0;n<plan.samples;++n) {
            if(stop.stop_requested())throw Error("ordered pulse evaluate cancelled");
            std::size_t lane=0;
#if defined(__SSE2__)
            if(backend==Backend::sse2)for(;lane+1<lanes_;lane+=2) {
                __m128d sum[4]{_mm_setzero_pd(),_mm_setzero_pd(),_mm_setzero_pd(),_mm_setzero_pd()};
                // No horizontal/tap reduction, FMA, frequency rotation or
                // moments: ORIGINAL ascending nonzero pulse order per lane.
                for(std::size_t p=0;p<plan.rows[n].taps;++p) {
                    const auto w=plan.rows[n].weight[p];if(w==0)continue;
                    const auto weight=_mm_set1_pd(w);
                    for(unsigned plane=0;plane<4;++plane) {
                        const auto at=plan.rows[n].first_offset+p;
                        const auto product=_mm_mul_pd(weight,_mm_loadu_pd(&coefficient_[plane][at][lane]));
                        sum[plane]=_mm_add_pd(sum[plane],product);
                    }
                }
                for(unsigned plane=0;plane<4;++plane)_mm_storeu_pd(&output_[n][plane][lane],sum[plane]);
            }
#else
            if(backend==Backend::sse2)return false;
#endif
            for(;lane<lanes_;++lane) {
                std::array<double,4> sum{};
                for(std::size_t p=0;p<plan.rows[n].taps;++p) {
                    const auto w=plan.rows[n].weight[p];if(w==0)continue;
                    for(unsigned plane=0;plane<4;++plane) {
                        const double product=w*coefficient_[plane][plan.rows[n].first_offset+p][lane];
                        sum[plane]+=product;
                    }
                }
                for(unsigned plane=0;plane<4;++plane)output_[n][plane][lane]=sum[plane];
            }
            statistics.output_pairs+=lanes_;
            statistics.shape_terms+=lanes_*plan.rows[n].nonzero_count;
        }
        evaluated_backend_=backend;
        evaluated_samples_=plan.samples;evaluated_=true;clear.succeeded=true;return true;
    }
    // Lane-major consumer restores the private context ONCE before original
    // ordered sample/outer-ring work. A ring flush may request old coefficients
    // OUTSIDE this slab; returning packed coefficients for it is invalid.
    void activate_lane(std::size_t lane,std::stop_token stop={}) {
        check(evaluated_ && lane<lanes_,"invalid ordered pulse lane");binding_[lane].activate(stop);
        ++statistics.prepares;
    }
    std::array<std::complex<double>,2> raw(std::size_t sample,std::size_t lane)const {
        check(evaluated_ && sample<evaluated_samples_ && lane<lanes_,"invalid ordered pulse output");
        return {{{output_[sample][0][lane],output_[sample][1][lane]},
                 {output_[sample][2][lane],output_[sample][3][lane]}}};
    }
    std::array<std::complex<double>,2> limited(std::size_t sample,std::size_t lane,
            double amplitude,double original_physical_gain) {
        auto result=raw(sample,lane);
        // Same scalar expression/order as current mapped accumulator. Caller
        // retains the physical gain==0 skip and all mask/Fit/guard ownership.
        for(auto& phase:result)phase=original_physical_gain*pattern_limit_pcm(amplitude*phase)/amplitude;
        ++statistics.limiter_pairs;return result;
    }
    std::size_t lanes()const{return lanes_;}
    Backend evaluated_backend()const noexcept{return evaluated_?evaluated_backend_:Backend::scalar;}
    std::size_t samples()const noexcept {
        if(evaluated_)return evaluated_samples_;
        const auto* plan=bound_plan();return gathered_ && plan?plan->samples:0;
    }
    const Lane& binding(std::size_t lane)const{return binding_[lane];}

private:
    static void check(bool condition,const char* message){if(!condition)throw Error(message);}
    using Plane=std::array<double,maximum_lanes>;
    alignas(64)std::array<std::array<Plane,maximum_coefficients>,4> coefficient_{};
    using ComponentPack=std::array<std::array<double,4>,maximum_coefficients>;
    static_assert(sizeof(ComponentPack)==maximum_coefficients*4*sizeof(double));
    struct EmptyComponentPack {};
    // Only single-candidate, compiled-capable optional Tiles own this slab.
    // It is not charged to an existing callee frame or allocated at evaluate.
    alignas(64)[[no_unique_address]]std::conditional_t<component_avx2_compiled,
        ComponentPack,EmptyComponentPack> component_coefficient_{};
    alignas(64)std::array<std::array<Plane,4>,maximum_samples> output_{};
    struct EmptyPlan {};
    [[no_unique_address]]std::conditional_t<OwnPlan,Plan,EmptyPlan> owned_plan_;
    const Plan* borrowed_plan_=nullptr;
    std::array<Lane,maximum_lanes> binding_{};
    std::size_t lanes_=0,evaluated_samples_=0;
    bool gathered_=false,evaluated_=false;
    Backend evaluated_backend_=Backend::scalar;
};

using ScheduledOrderedPulseCandidates=ScheduledOrderedPulseCandidatesT<>;
static_assert(sizeof(ScheduledOrderedPulseCandidates)<128*1024,
    "default ordered pulse payload exceeds proposed128KiB reservation");
} // namespace datapump::modem::detail

#undef DATAPUMP_ORDERED_COMPONENT_AVX2_COMPILED
