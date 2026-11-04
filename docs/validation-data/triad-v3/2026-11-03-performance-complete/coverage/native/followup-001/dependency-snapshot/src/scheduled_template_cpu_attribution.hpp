#pragma once
// Local opt-in component diagnostics. No detector/global Stage/clock changes.
#include "scheduled_work_profile.hpp"
#include <string>
#include <type_traits>

namespace datapump::modem::detail::scheduled_template_cpu_attribution {
namespace cpu=::datapump::detail::cpu_profile;
enum class Bucket:std::size_t {
    source_prepare,source_reader,table_setup,row_geometry,fallback_endpoints,
    fallback_reader,workspace_checks,proof_inclusive,callback,derivative_queries,difference_norm,
    target_residual,gram_append,prepared_fragments,count
};
enum class Counter:std::size_t {rows,selected_rows,target_pairs,proof_attempts,proof_valid,
    fallback_rows,derivative_table_queries,aggregate_lookups,count};
struct Slot {std::uint64_t ns=0,calls=0,invalid=0;};
inline bool requested()noexcept {
    const auto* value=std::getenv("DATAPUMP_SCHED_TEMPLATE_COMPONENT_CPU");
    return value && std::strcmp(value,"1")==0; // Live per invocation; tests may toggle it.
}
class Span;
class Recorder {
    friend class Span;
    bool enabled_=false,saturated_=false;
    cpu::ReadClock read_=cpu::native_thread_cpu;void* opaque_=nullptr;
    std::array<Slot,static_cast<std::size_t>(Bucket::count)> slots_{};
    std::array<std::uint64_t,static_cast<std::size_t>(Counter::count)> counts_{};
    std::uint64_t clock_reads_=0,clock_failures_=0,overflows_=0,active_=0;
    void add(std::uint64_t& value,std::uint64_t amount=1)noexcept {
        if(amount>UINT64_MAX-value){value=UINT64_MAX;saturated_=true;if(overflows_!=UINT64_MAX)++overflows_;}
        else value+=amount;
    }
    cpu::Tick now()noexcept {
        add(clock_reads_);const auto value=read_?read_(opaque_):cpu::Tick{};
        if(!value.valid)add(clock_failures_);return value;
    }
public:
    explicit Recorder(bool enabled=false,cpu::ReadClock read=cpu::native_thread_cpu,void* opaque=nullptr)noexcept:
        enabled_(enabled),read_(read),opaque_(opaque){}
    bool enabled()const noexcept{return enabled_;}
    bool valid()const noexcept{return enabled_ && !saturated_ && !clock_failures_ && !active_;}
    bool saturated()const noexcept{return saturated_;}
    std::uint64_t clock_reads()const noexcept{return clock_reads_;}
    std::uint64_t clock_failures()const noexcept{return clock_failures_;}
    std::uint64_t overflows()const noexcept{return overflows_;}
    std::uint64_t active_spans()const noexcept{return active_;}
    const Slot& at(Bucket bucket)const noexcept{return slots_[static_cast<std::size_t>(bucket)];}
    std::uint64_t count(Counter counter)const noexcept{return counts_[static_cast<std::size_t>(counter)];}
    void observe(Counter counter,std::uint64_t amount=1)noexcept {
        if(enabled_)add(counts_[static_cast<std::size_t>(counter)],amount);
    }
    bool proof_parts(std::uint64_t& non_callback,std::uint64_t& other)const noexcept {
        non_callback=other=0;if(!valid())return false;
        const auto proof=at(Bucket::proof_inclusive).ns,callback=at(Bucket::callback).ns,query=at(Bucket::derivative_queries).ns;
        if(proof<callback || proof-callback<query)return false;
        non_callback=proof-callback;other=non_callback-query;return true;
    }
    template<class Fields>void fields(Fields& f)const {
        f.add("component_cpu_enabled",enabled_);f.add("component_cpu_valid",valid());
        f.add("component_clock_reads",clock_reads_);f.add("component_clock_failures",clock_failures_);
        f.add("component_overflows",overflows_);f.add("component_active_spans",active_);
        constexpr std::array<const char*,static_cast<std::size_t>(Bucket::count)> names{
            "source_prepare","source_reader","table_setup","row_geometry","fallback_endpoints",
            "fallback_reader","workspace_checks","proof_inclusive","callback","derivative_queries","difference_norm",
            "target_residual","gram_append","prepared_fragments"};
        for(std::size_t i=0;i<names.size();++i){const auto key=std::string("component_")+names[i];const auto& s=slots_[i];
            f.add((key+"_cpu_ns").c_str(),s.ns);f.add((key+"_calls").c_str(),s.calls);f.add((key+"_invalid").c_str(),s.invalid);}
        constexpr std::array<const char*,static_cast<std::size_t>(Counter::count)> counters{
            "rows","selected_rows","target_pairs","proof_attempts","proof_valid","fallback_rows","derivative_table_queries","aggregate_lookups"};
        for(std::size_t i=0;i<counters.size();++i)f.add((std::string("component_")+counters[i]).c_str(),counts_[i]);
        std::uint64_t non_callback=0,other=0;const bool derived=proof_parts(non_callback,other);
        f.add("component_proof_parts_valid",derived);f.add("component_proof_non_callback_cpu_ns",non_callback);
        f.add("component_proof_other_cpu_ns",other);
    }
};
class Span {
    Recorder* recorder_=nullptr;Bucket bucket_=Bucket::source_prepare;cpu::Tick begin_{};
public:
    Span(Recorder* recorder,Bucket bucket)noexcept:bucket_(bucket) {
        if(!recorder || !recorder->enabled())return;recorder_=recorder;
        recorder_->add(recorder_->slots_[static_cast<std::size_t>(bucket_)].calls);
        recorder_->add(recorder_->active_);begin_=recorder_->now();
    }
    ~Span(){finish();}
    void finish()noexcept {
        if(!recorder_)return;auto* r=recorder_;recorder_=nullptr;const auto end=r->now();
        auto& slot=r->slots_[static_cast<std::size_t>(bucket_)];
        if(begin_.valid && end.valid && end.ns>=begin_.ns)r->add(slot.ns,end.ns-begin_.ns);
        else {r->add(slot.invalid);if(begin_.valid && end.valid)r->add(r->clock_failures_);}
        if(r->active_)--r->active_;else r->add(r->clock_failures_);
    }
    Span(const Span&)=delete;Span& operator=(const Span&)=delete;
};
template<class Emit>struct OnExit {Emit emit;~OnExit(){emit();}};
static_assert(sizeof(Recorder)<=1024 && sizeof(Span)<=64);
// Observer is outermost: wrap_original(recorder, exact_node_callback) preserves
// the existing callback/cache and forwards exactly the same arguments/result.
// The callback bucket measures supplied callback work, including cache lookups.
template<class Original>class ObservedOriginal {
    Recorder* recorder_;Original original_;
public:
    ObservedOriginal(Recorder* recorder,Original&& original):
        recorder_(recorder && recorder->enabled()?recorder:nullptr),original_(std::forward<Original>(original)){}
    Recorder* template_cpu_recorder()const noexcept{return recorder_;}
    template<class... Args>decltype(auto)operator()(Args&&... args) {
        Span span(recorder_,Bucket::callback);return original_(std::forward<Args>(args)...);
    }
};
template<class Original>auto wrap_original(Recorder* recorder,Original&& original) {
    return ObservedOriginal<Original>{recorder,std::forward<Original>(original)};
}
template<class Original>Recorder* recorder_of(Original& original)noexcept {
    if constexpr(requires {original.template_cpu_recorder();}) {
        if constexpr(std::is_same_v<decltype(original.template_cpu_recorder()),Recorder*>)return original.template_cpu_recorder();
    }
    return nullptr;
}
} // namespace datapump::modem::detail::scheduled_template_cpu_attribution
