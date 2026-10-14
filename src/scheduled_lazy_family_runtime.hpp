#pragma once
#include "scheduled_frequency_recurrence.hpp"
#include "scheduled_parallel_preparation.hpp"

namespace datapump::modem::detail::scheduled_lazy_family_runtime {
namespace tile=scheduled_acquisition_tile;
namespace moments=scheduled_frequency_moments;
namespace recurrence=scheduled_frequency_recurrence;

// Only the already gated experimental lazy backend selects the automatic
// larger fragment. Original capture/processing recurrence fences remain 512.
inline std::size_t fragment_size(std::size_t span,bool experimental) {
    return experimental?scheduled_parallel_preparation::shape(span).maximum_fragment:512;
}
template<class T> std::size_t input_bytes(const tile::Plan<T>& plan) {
    const auto length=tile::add_size(plan.shape.start_span,plan.shape.maximum_fragment-1);
    auto bytes=tile::add_size(sizeof(tile::PreparedInput<T>),tile::helper_reservation);
    bytes=tile::add_size(bytes,tile::multiply_size(3*plan.size,sizeof(std::complex<T>)));
    return tile::add_size(bytes,tile::multiply_size(length+1,sizeof(long double)+sizeof(std::uint64_t)+
        sizeof(scheduled_acquisition_certificate::Interval)));
}
template<class T> std::size_t moment_bytes(const tile::Plan<T>& plan,unsigned degree) {
    const auto length=tile::add_size(plan.shape.start_span,plan.shape.maximum_fragment-1);
    auto bytes=tile::add_size(sizeof(moments::MomentInput<T>),input_bytes(plan));
    bytes=tile::add_size(bytes,tile::multiply_size(2*degree*plan.size,sizeof(std::complex<T>)));
    return tile::add_size(bytes,tile::multiply_size(length,sizeof(std::array<double,moments::maximum_degree+1>)));
}
template<class T> std::size_t resolved_bytes(const tile::Plan<T>& plan) {
    // Includes the separately released optional SIMD synthesis before-cast
    // plane; the scalar header currently leaves these reserved bytes unused.
    return tile::add_size(sizeof(moments::ResolvedInput<T>),tile::multiply_size(plan.size,
        2*sizeof(std::complex<T>)+sizeof(std::complex<double>)));
}
template<class T> std::size_t accumulator_bytes(const tile::Plan<T>& plan,std::size_t starts,bool native_float_accumulation=false) {
    if(native_float_accumulation && !std::is_same_v<T,float>)throw Error("native float accumulator reservation conflicts with requested precision");
    auto bytes=tile::add_size(sizeof(tile::Accumulator<T>),tile::helper_reservation);
    bytes=tile::add_size(bytes,tile::multiply_size(12*plan.size,native_float_accumulation?sizeof(std::complex<float>):sizeof(std::complex<double>)));
    bytes=tile::add_size(bytes,tile::multiply_size(plan.size,sizeof(std::complex<T>)));
    return tile::add_size(bytes,tile::multiply_size(starts,sizeof(long double)+sizeof(std::uint64_t)+
        sizeof(tile::Value)+sizeof(scheduled_acquisition_certificate::Interval)+sizeof(std::array<double,2>)));
}
struct OriginalRun {
    std::size_t begin=0,end=0;
    std::uint64_t block_first=0;
    std::size_t block_offset=0;
    std::complex<double> coarse_seed{},coarse_step{};
    bool observed=false;
};
inline bool capture_origin(const recurrence::CaptureOrigin& found,std::uint64_t virtual_first,
                           std::uint64_t shift,bool any_observed) {
    if(!any_observed)return !found.valid;
    const bool negative=virtual_first<shift;
    const auto magnitude=negative?shift-virtual_first:virtual_first-shift;
    return found.valid && found.negative==negative && found.magnitude==magnitude;
}
inline void add_count(std::uint64_t& destination,std::uint64_t value) {
    if(value>std::numeric_limits<std::uint64_t>::max()-destination)
        throw Error("implicit frequency family work count overflow");
    destination+=value;
}
struct Work {
    std::uint64_t groups=0,moment_groups=0,moment_transforms=0,synthesized_products=0;
    std::uint64_t recurrence_segments=0,trace_inverse_reuses=0,trace_reuse_fallbacks=0;
    std::uint64_t fragments=0,native_float_accumulators=0,wide_accumulators=0;std::size_t maximum_fragment=0;
};
// Optional private diagnostic sink; no public CorrelatorWork/ABI is changed.
// Root's journal may add exactly these private execution fields and fold them
// only after worker join. Existing mandatory FFT counters are updated directly.
template<class Execution> void publish(Execution& execution,const Work& work) {
    if constexpr(requires {execution.frequency_family_work;}) {
        auto& out=execution.frequency_family_work;
        add_count(out.groups,work.groups);add_count(out.moment_groups,work.moment_groups);
        add_count(out.moment_transforms,work.moment_transforms);add_count(out.synthesized_products,work.synthesized_products);
        add_count(out.recurrence_segments,work.recurrence_segments);add_count(out.trace_inverse_reuses,work.trace_inverse_reuses);
        add_count(out.trace_reuse_fallbacks,work.trace_reuse_fallbacks);add_count(out.fragments,work.fragments);
        add_count(out.native_float_accumulators,work.native_float_accumulators);add_count(out.wide_accumulators,work.wide_accumulators);
        out.maximum_fragment=std::max(out.maximum_fragment,work.maximum_fragment);
    }
}
} // namespace datapump::modem::detail::scheduled_lazy_family_runtime
