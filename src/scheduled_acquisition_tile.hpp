#pragma once

// Whole-symbol acquisition over bounded, disjoint PHYSICAL fragments.  A
// fragment contains the complete shaped/limited coefficient at each sample,
// not an individual overlapping pulse.  The caller supplies exact calendar,
// source ownership, fractional-template and original-carrier constructions.
// This file neither enumerates hypotheses nor scores guards/trials/payloads.
#include "pattern_correlator_batch.hpp"
#include "search_fft.hpp"
#include "scheduled_acquisition_certificate.hpp"
#include "scheduled_spectral_products.hpp"
#include "scheduled_float_spectral_accumulator.hpp"
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <type_traits>
#include <vector>

namespace datapump::modem::detail::scheduled_acquisition_tile {

inline constexpr std::size_t maximum_start_span=8192;
inline constexpr std::size_t maximum_fragment_samples=2048;
inline constexpr std::size_t maximum_fragments=65536;
inline constexpr std::size_t helper_reservation=8192;

inline void check_stop(std::stop_token stop) {
    if(stop.stop_requested())throw Error("scheduled acquisition tile cancelled");
}
inline std::size_t add_size(std::size_t a,std::size_t b) {
    if(b>std::numeric_limits<std::size_t>::max()-a)throw Error("scheduled acquisition size overflow");
    return a+b;
}
inline std::size_t multiply_size(std::size_t a,std::size_t b) {
    if(a && b>std::numeric_limits<std::size_t>::max()/a)throw Error("scheduled acquisition size overflow");
    return a*b;
}
inline std::uint64_t add_coordinate(std::uint64_t a,std::uint64_t b) {
    if(b>std::numeric_limits<std::uint64_t>::max()-a)throw Error("scheduled acquisition coordinate overflow");
    return a+b;
}
inline bool finite(std::complex<double> value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}
inline bool finite(const CorrelationFit& value) {
    return std::isfinite(value.xc) && std::isfinite(value.xs) && std::isfinite(value.cc) &&
        std::isfinite(value.ss) && std::isfinite(value.cs) && std::isfinite(value.energy);
}
template<class T> void wipe(std::vector<T>& values) {
    if(!values.empty())OPENSSL_cleanse(values.data(),values.size()*sizeof(T));
}
struct Shape {
    // Integer translation s is in [0,start_span); no extra timing positions are
    // generated.  An irregular admitted subset is passed separately.
    std::size_t start_span=0,maximum_fragment=0;
    std::size_t transform_size()const {
        if(!start_span || start_span>maximum_start_span || !maximum_fragment ||
           maximum_fragment>maximum_fragment_samples)throw Error("scheduled acquisition geometry exceeds bound");
        return std::bit_ceil(add_size(start_span,maximum_fragment-1));
    }
};
struct Input {
    // Borrowed immutable ORIGINAL capture.  It must outlive its PreparedInput.
    std::span<const float> pcm;
    std::span<const std::complex<double>> oscillator;
    std::span<const std::uint8_t> observed; // Empty: every captured sample exists.
    std::uint64_t physical_first=0;
};
struct Fragment {
    // Both independent candidates include final shaping, limiting and public
    // ramp gain, but not the physical carrier.  Fractional source positions are
    // supplied as their own exact rows; this operator never interpolates them.
    std::span<const std::array<std::complex<double>,2>> rows;
    // Exact original usable-sample predicate, including zero-power exclusion.
    // Empty: all samples. A zero private coefficient alone does NOT exclude it.
    std::span<const std::uint8_t> mask;
    std::uint64_t physical_offset=0; // From this family's physical origin.
};
struct Value {
    std::array<CorrelationFit,2> fits{};
    std::uint64_t physical_origin=0;
    std::array<bool,2> requires_refinement{};
    // Engineering estimates only, NOT certified score intervals.  They must
    // not be used alone to exclude detector hypotheses.  Root integration owns
    // conservative score bounds, original refinement and sensitivity checks.
    std::array<double,2> dot_roundoff{},gram_roundoff{};
    scheduled_acquisition_certificate::FitCertificate certificate;
};
struct Work {
    std::uint64_t input_transforms=0,template_transforms=0,inverse_transforms=0;
    std::uint64_t fragments=0,spectral_products=0,prefix_queries=0,energy_refinements=0;
    std::uint64_t direct_sample_pairs=0;
    std::uint64_t trace_inverse_reuses=0,trace_reuse_fallbacks=0;
    std::size_t working_bytes=0,transform=0;
};
struct Run {std::size_t first=0,end=0;};

inline bool present(const Input& input,std::size_t index) {
    return input.observed.empty() || input.observed[index]!=0;
}
inline bool selected(const Fragment& fragment,std::size_t index) {
    return fragment.mask.empty() || fragment.mask[index]!=0;
}
inline void validate(const Input& input,const Fragment& fragment,std::span<const std::size_t> starts,
                     std::stop_token stop={}) {
    if(input.pcm.size()>maximum_start_span+maximum_fragment_samples-1 ||
       input.oscillator.size()!=input.pcm.size() ||
       (!input.observed.empty() && input.observed.size()!=input.pcm.size()) ||
       fragment.rows.empty() || fragment.rows.size()>maximum_fragment_samples ||
       (!fragment.mask.empty() && fragment.mask.size()!=fragment.rows.size()) ||
       fragment.rows.size()>input.pcm.size() || starts.size()>maximum_start_span)
        throw Error("invalid scheduled acquisition operands");
    add_coordinate(input.physical_first,input.pcm.size());
    for(std::size_t i=0;i<input.pcm.size();++i) {
        if((i&255U)==0)check_stop(stop);
        if(!std::isfinite(input.pcm[i]) || !finite(input.oscillator[i]) ||
           (!input.observed.empty() && input.observed[i]>1))throw Error("invalid scheduled acquisition capture");
    }
    for(std::size_t i=0;i<fragment.rows.size();++i) {
        if((i&255U)==0)check_stop(stop);
        if(!finite(fragment.rows[i][0]) || !finite(fragment.rows[i][1]) ||
           (!fragment.mask.empty() && fragment.mask[i]>1))throw Error("invalid scheduled acquisition template");
    }
    for(std::size_t i=0;i<starts.size();++i)
        if(starts[i]>=maximum_start_span || starts[i]>input.pcm.size()-fragment.rows.size() ||
           (i && starts[i]<=starts[i-1]))
            throw Error("invalid scheduled acquisition admitted starts");
}

template<class T> class Plan {
    static_assert(std::is_same_v<T,float> || std::is_same_v<T,double>);
public:
    using Complex=std::complex<T>;
    using Native=std::conditional_t<std::is_same_v<T,float>,search_fft::FloatPlan,search_fft::DoublePlan>;
    const Shape shape;
    const std::size_t size;
    const Native native;
    const scheduled_acquisition_certificate::Plan certificate;
    explicit Plan(Shape dimensions,std::stop_token stop={})
        :shape(dimensions),size(shape.transform_size()),native(size,stop),certificate(native,stop) {}
    std::size_t working_bytes()const{return add_size(add_size(sizeof(*this),native.working_bytes()),certificate.working_bytes());}
};
template<class T> std::complex<T> cast(std::complex<double> value) {
    if(!finite(value))throw Error("nonfinite scheduled acquisition operand");
    if constexpr(std::is_same_v<T,float>) {
        if(std::max(std::abs(value.real()),std::abs(value.imag()))>search_fft::float_component_limit(16384))
            throw Error("scheduled acquisition FP32 range needs FP64 fallback");
    }
    return {static_cast<T>(value.real()),static_cast<T>(value.imag())};
}
template<class T> void range(double peak,std::size_t size) {
    if constexpr(std::is_same_v<T,float>) {
        const auto low=8*std::sqrt(static_cast<double>(std::numeric_limits<float>::min()))*size;
        if(peak && (peak<low || peak>search_fft::float_component_limit(size)))
            throw Error("scheduled acquisition FP32 range needs FP64 fallback");
    }
}
template<class T> void transform(std::vector<std::complex<T>>& values,bool inverse,
                                 const Plan<T>& plan,std::stop_token stop) {
    search_fft::fft(std::span<std::complex<T>>(values),inverse,plan.native,stop);
    if(!search_fft::finite(std::span<std::complex<T>>(values)))
        throw Error("scheduled acquisition transform overflow");
}

// Reuse identity: same original capture, observation fence/mask, physical first,
// exact oscillator samples and Plan shape.  Not keyed by receiver-local offset
// alone.  Caller charges this object once if several keys/epochs borrow it.
template<class T> class PreparedInput {
public:
    using Complex=std::complex<T>;
    const Plan<T>& plan;
    const Input original;
    std::array<std::vector<Complex>,3> spectra;
    std::vector<long double> energy;
    std::vector<scheduled_acquisition_certificate::Interval> certified_energy;
    std::array<scheduled_acquisition_certificate::Operand,3> certificates;
    std::vector<std::uint64_t> count;
    std::array<long double,3> norms{};
    Work work;
    PreparedInput(const Plan<T>& p,Input input,std::size_t budget,std::stop_token stop={})
        :plan(p),original(input) {
        const auto length=add_size(plan.shape.start_span,plan.shape.maximum_fragment-1);
        if(input.pcm.size()!=length || input.oscillator.size()!=length ||
           (!input.observed.empty() && input.observed.size()!=length))
            throw Error("scheduled acquisition input tile has wrong shape");
        add_coordinate(input.physical_first,length);
        auto estimate=add_size(sizeof(*this),helper_reservation);
        estimate=add_size(estimate,multiply_size(3*plan.size,sizeof(Complex)));
        estimate=add_size(estimate,multiply_size(length+1,sizeof(long double)+sizeof(std::uint64_t)+sizeof(scheduled_acquisition_certificate::Interval)));
        if(estimate>budget)throw Error("scheduled acquisition input exceeds workspace");
        try {
            check_stop(stop);
            for(auto& plane:spectra)plane.resize(plan.size);
            energy.resize(length+1);count.resize(length+1);certified_energy.resize(length+1);
            std::array<double,3> peaks{};
            for(std::size_t i=0;i<length;++i) {
                if((i&255U)==0)check_stop(stop);
                if(!std::isfinite(input.pcm[i]) || !finite(input.oscillator[i]) ||
                   (!input.observed.empty() && input.observed[i]>1))
                    throw Error("invalid scheduled acquisition capture");
                energy[i+1]=energy[i];count[i+1]=count[i];certified_energy[i+1]=certified_energy[i];
                if(!present(input,i))continue;
                const auto x=static_cast<double>(input.pcm[i]);
                const auto o=input.oscillator[i];
                const std::array<std::complex<double>,3> values{x*o,o*o,std::complex<double>{std::norm(o),0}};
                energy[i+1]+=static_cast<long double>(x)*x;++count[i+1];
                certified_energy[i+1]=scheduled_acquisition_certificate::plus(certified_energy[i+1],
                    scheduled_acquisition_certificate::pcm_square(input.pcm[i]));
                for(unsigned plane=0;plane<3;++plane) {
                    spectra[plane][i]=cast<T>(values[plane]);
                    const auto preparation=plane==0?scheduled_acquisition_certificate::complex_preproduct_radius({x,0},o):
                        (plane==1?scheduled_acquisition_certificate::complex_preproduct_radius(o,o):
                         scheduled_acquisition_certificate::norm_preproduct_radius(o));
                    certificates[plane].observe(values[plane],spectra[plane][i],preparation);
                    norms[plane]+=static_cast<long double>(std::norm(values[plane]));
                    peaks[plane]=std::max({peaks[plane],std::abs(values[plane].real()),std::abs(values[plane].imag())});
                }
            }
            for(unsigned plane=0;plane<3;++plane) {
                range<T>(peaks[plane],plan.size);transform(spectra[plane],false,plan,stop);++work.input_transforms;
                norms[plane]=std::sqrt(norms[plane]);
                certificates[plane].finish<T>(plan.certificate,std::span<const Complex>(spectra[plane]));
            }
            work.transform=plan.size;work.working_bytes=working_bytes();
            if(work.working_bytes>budget)throw Error("scheduled acquisition input capacity exceeds workspace");
        } catch(...) {clean();throw;}
    }
    PreparedInput(const PreparedInput&)=delete;
    PreparedInput& operator=(const PreparedInput&)=delete;
    ~PreparedInput(){clean();}
    void clean(){for(auto& plane:spectra)wipe(plane);wipe(energy);wipe(count);wipe(certified_energy);}
    std::size_t working_bytes()const {
        auto bytes=add_size(sizeof(*this),helper_reservation);
        for(const auto& plane:spectra)bytes=add_size(bytes,multiply_size(plane.capacity(),sizeof(Complex)));
        bytes=add_size(bytes,multiply_size(energy.capacity(),sizeof(long double)));
        bytes=add_size(bytes,multiply_size(certified_energy.capacity(),sizeof(scheduled_acquisition_certificate::Interval)));
        return add_size(bytes,multiply_size(count.capacity(),sizeof(std::uint64_t)));
    }
};

// Frequency-independent reusable private spectra: exact original key/address,
// bit pair, epoch, stream phase, source symbol, clock/fractional row, public gain,
// usable mask, physical offset and Plan shape must ALL match.  No implicit cache.
template<class T> class PreparedFragment {
public:
    using Complex=std::complex<T>;
    const Plan<T>& plan;
    const Fragment original;
    std::array<std::vector<Complex>,6> spectra;
    std::vector<Run> runs;
    std::array<long double,6> norms{};
    std::array<scheduled_acquisition_certificate::Operand,6> certificates;
    Work work;
    PreparedFragment(const Plan<T>& p,Fragment fragment,std::size_t budget,std::stop_token stop={})
        :plan(p),original(fragment) {
        const auto length=fragment.rows.size();
        if(!length || length>plan.shape.maximum_fragment ||
           (!fragment.mask.empty() && fragment.mask.size()!=length))
            throw Error("scheduled acquisition fragment has wrong shape");
        add_coordinate(fragment.physical_offset,length);
        auto estimate=add_size(sizeof(*this),helper_reservation);
        estimate=add_size(estimate,multiply_size(6*plan.size,sizeof(Complex)));
        estimate=add_size(estimate,multiply_size((length+1)/2,sizeof(Run)));
        if(estimate>budget)throw Error("scheduled acquisition template exceeds workspace");
        try {
            check_stop(stop);
            for(auto& plane:spectra)plane.resize(plan.size);
            runs.reserve((length+1)/2);
            std::array<double,6> peaks{};
            bool active=false;
            for(std::size_t i=0;i<length;++i) {
                if((i&255U)==0)check_stop(stop);
                if(!finite(fragment.rows[i][0]) || !finite(fragment.rows[i][1]) ||
                   (!fragment.mask.empty() && fragment.mask[i]>1))
                    throw Error("invalid scheduled acquisition template");
                if(!selected(fragment,i)){active=false;continue;}
                if(!active){runs.push_back({i,i+1});active=true;}else runs.back().end=i+1;
                for(unsigned bit=0;bit<2;++bit) {
                    const auto q=fragment.rows[i][bit];
                    const std::array<std::complex<double>,3> values{q,q*q,std::complex<double>{std::norm(q),0}};
                    for(unsigned plane=0;plane<3;++plane) {
                        const auto at=3*bit+plane;
                        // Every fragment produces its valid result at D+s,
                        // regardless of its length.  Circular wrap reaches
                        // only indices <D, so those outputs are linear/exact.
                        spectra[at][plan.shape.maximum_fragment-1-i]=cast<T>(values[plane]);
                        const auto preparation=plane==0?0.:(plane==1?
                            scheduled_acquisition_certificate::complex_preproduct_radius(q,q):
                            scheduled_acquisition_certificate::norm_preproduct_radius(q));
                        certificates[at].observe(values[plane],spectra[at][plan.shape.maximum_fragment-1-i],preparation);
                        norms[at]+=static_cast<long double>(std::norm(values[plane]));
                        peaks[at]=std::max({peaks[at],std::abs(values[plane].real()),std::abs(values[plane].imag())});
                    }
                }
            }
            for(unsigned plane=0;plane<6;++plane) {
                range<T>(peaks[plane],plan.size);transform(spectra[plane],false,plan,stop);++work.template_transforms;
                norms[plane]=std::sqrt(norms[plane]);
                certificates[plane].finish<T>(plan.certificate,std::span<const Complex>(spectra[plane]));
            }
            work.transform=plan.size;work.working_bytes=working_bytes();
            if(work.working_bytes>budget)throw Error("scheduled acquisition template capacity exceeds workspace");
        } catch(...) {clean();throw;}
    }
    PreparedFragment(const PreparedFragment&)=delete;
    PreparedFragment& operator=(const PreparedFragment&)=delete;
    ~PreparedFragment(){clean();}
    void clean(){for(auto& plane:spectra)wipe(plane);}
    std::size_t working_bytes()const {
        auto bytes=add_size(sizeof(*this),helper_reservation);
        for(const auto& plane:spectra)bytes=add_size(bytes,multiply_size(plane.capacity(),sizeof(Complex)));
        return add_size(bytes,multiply_size(runs.capacity(),sizeof(Run)));
    }
};

inline void compensated(double value,double& sum,double& correction,double& residual_mass) {
    const auto next=sum+value;
    // Magnitude-selected FastTwoSum gives the exact original addition error
    // in an eligible IEEE environment. Only accumulating those errors rounds.
    const auto residual=std::abs(sum)>=std::abs(value)?(sum-next)+value:(value-next)+sum;
    correction+=residual;residual_mass+=std::abs(residual);
    sum=next;
}

// Reuse requires exact executed norm spectra and matching plan/start identities.
// This is not an assumption that a rounded physical carrier has unit modulus.
// Borrowed token: the completed exporting Accumulator, its Plan and immutable
// admitted starts must outlive every consumer. Copying the token owns nothing.
template<class T> class Accumulator;
template<class T> class TraceReuse {
    template<class> friend class Accumulator;
    const Plan<T>* plan=nullptr;
    std::uint64_t origin=0;
    std::span<const std::size_t> starts;
    std::array<std::span<const std::complex<double>>,2> sums,corrections;
    std::array<std::span<const std::complex<float>>,2> native_sums,native_corrections;
    bool native_float=false;
    std::span<const std::array<double,2>> trace;
public:
    TraceReuse()=default;
};

template<class T> class Accumulator {
    using Complex=std::complex<T>;
    const Plan<T>& plan_;
    std::uint64_t origin_=0,last_end_=0;
    std::span<const std::size_t> starts_;
    std::array<std::vector<std::complex<double>>,6> sums_,corrections_;
    std::array<std::vector<std::complex<float>>,6> native_sums_,native_corrections_;
    bool native_float_=false;
    std::vector<Complex> scratch_;
    std::vector<long double> energy_;
    std::vector<scheduled_acquisition_certificate::Interval> certified_energy_;
    std::vector<std::uint64_t> count_;
    std::vector<Value> values_;
    std::vector<std::array<double,2>> trace_values_;
    std::array<long double,6> roundoff_{};
    std::array<double,6> convolution_radius_{},residual_mass_upper_{};
    bool failed_=false,finished_=false;
    bool certificate_diagnostics_=false;
    scheduled_certificate_diagnostics::NativeProfile certificate_profile_;
    Work work_;
    void clean(){for(auto& plane:sums_)wipe(plane);for(auto& plane:corrections_)wipe(plane);
        for(auto& plane:native_sums_)wipe(plane);for(auto& plane:native_corrections_)wipe(plane);
        wipe(scratch_);wipe(energy_);wipe(count_);wipe(values_);wipe(certified_energy_);wipe(trace_values_);}
public:
    // Starts and supplied prepared objects stay immutable. Input/template plans
    // may be separate objects with identical shape; their spectra are public-API
    // values, not handles to the accumulator. No whole-symbol PCM is retained.
    Accumulator(const Plan<T>& plan,std::uint64_t physical_origin,std::span<const std::size_t> starts,
                std::size_t budget,std::stop_token stop={},bool certificate_diagnostics=false,bool native_float_accumulation=false)
        :plan_(plan),origin_(physical_origin),starts_(starts),native_float_(native_float_accumulation),certificate_diagnostics_(certificate_diagnostics) {
        if(native_float_ && (!std::is_same_v<T,float> || !scheduled_float_spectral_accumulator::environment()))
            throw Error("scheduled native float accumulator unsupported for requested precision/environment");
        if(starts.size()>maximum_start_span)throw Error("scheduled acquisition start count exceeds bound");
        for(std::size_t i=0;i<starts.size();++i)
            if(starts[i]>=plan.shape.start_span || (i && starts[i]<=starts[i-1]))
                throw Error("invalid scheduled acquisition admitted starts");
        add_coordinate(origin_,plan.shape.start_span-1);
        auto estimate=add_size(sizeof(*this),helper_reservation);
        if(!starts.empty()) {
            estimate=add_size(estimate,multiply_size(12*plan.size,native_float_?sizeof(std::complex<float>):sizeof(std::complex<double>)));
            estimate=add_size(estimate,multiply_size(plan.size,sizeof(Complex)));
            estimate=add_size(estimate,multiply_size(starts.size(),sizeof(long double)+sizeof(std::uint64_t)+sizeof(Value)+sizeof(scheduled_acquisition_certificate::Interval)+sizeof(std::array<double,2>)));
        }
        if(estimate>budget)throw Error("scheduled acquisition accumulator exceeds workspace");
        try {
            check_stop(stop);
            // An empty admitted set does not allocate/execute FFT work.
            if(!starts.empty()) {
                if(native_float_) {
                    for(auto& plane:native_sums_)plane.resize(plan.size);
                    for(auto& plane:native_corrections_)plane.resize(plan.size);
                } else {
                    for(auto& plane:sums_)plane.resize(plan.size);
                    for(auto& plane:corrections_)plane.resize(plan.size);
                }
                scratch_.resize(plan.size);
                energy_.resize(starts.size());count_.resize(starts.size());values_.resize(starts.size());certified_energy_.resize(starts.size());trace_values_.resize(starts.size());
            }
            work_.transform=plan.size;work_.working_bytes=working_bytes();
            if(work_.working_bytes>budget)throw Error("scheduled acquisition accumulator capacity exceeds workspace");
        } catch(...) {clean();throw;}
    }
    Accumulator(const Accumulator&)=delete;
    Accumulator& operator=(const Accumulator&)=delete;
    ~Accumulator(){clean();}
    std::size_t working_bytes()const {
        auto bytes=add_size(sizeof(*this),helper_reservation);
        for(const auto& plane:sums_)bytes=add_size(bytes,multiply_size(plane.capacity(),sizeof(std::complex<double>)));
        for(const auto& plane:corrections_)bytes=add_size(bytes,multiply_size(plane.capacity(),sizeof(std::complex<double>)));
        for(const auto& plane:native_sums_)bytes=add_size(bytes,multiply_size(plane.capacity(),sizeof(std::complex<float>)));
        for(const auto& plane:native_corrections_)bytes=add_size(bytes,multiply_size(plane.capacity(),sizeof(std::complex<float>)));
        bytes=add_size(bytes,multiply_size(scratch_.capacity(),sizeof(Complex)));
        bytes=add_size(bytes,multiply_size(energy_.capacity(),sizeof(long double)));
        bytes=add_size(bytes,multiply_size(certified_energy_.capacity(),sizeof(scheduled_acquisition_certificate::Interval)));
        bytes=add_size(bytes,multiply_size(count_.capacity(),sizeof(std::uint64_t)));
        bytes=add_size(bytes,multiply_size(values_.capacity(),sizeof(Value)));
        return add_size(bytes,multiply_size(trace_values_.capacity(),sizeof(std::array<double,2>)));
    }
    bool native_float_accumulation()const noexcept{return native_float_;}
    const Work& work()const{return work_;}
    const scheduled_certificate_diagnostics::NativeProfile& certificate_profile()const noexcept{return certificate_profile_;}
    template<class InputLike> void add(const InputLike& input,const PreparedFragment<T>& fragment,std::stop_token stop={}) {
        if(failed_ || finished_)throw Error("scheduled acquisition accumulator is no longer writable");
        try {
            check_stop(stop);
            if(input.plan.size!=plan_.size || fragment.plan.size!=plan_.size ||
               input.plan.shape.start_span!=plan_.shape.start_span ||
               fragment.plan.shape.start_span!=plan_.shape.start_span ||
               input.plan.shape.maximum_fragment!=plan_.shape.maximum_fragment ||
               fragment.plan.shape.maximum_fragment!=plan_.shape.maximum_fragment ||
               input.original.physical_first!=add_coordinate(origin_,fragment.original.physical_offset) ||
               (work_.fragments && fragment.original.physical_offset<last_end_) || work_.fragments==maximum_fragments)
                throw Error("incompatible/overlapping scheduled acquisition fragment");
            last_end_=add_coordinate(fragment.original.physical_offset,fragment.original.rows.size());
            if(starts_.empty())return;
            for(unsigned plane=0;plane<6;++plane) {
                const auto input_plane=plane%3;
                scheduled_spectral_products::Result accumulation;
                if constexpr(std::is_same_v<T,float>) {
                    if(native_float_)accumulation=scheduled_float_spectral_accumulator::accumulate(
                        input.spectra[input_plane],fragment.spectra[plane],native_sums_[plane],native_corrections_[plane],stop);
                    else accumulation=scheduled_spectral_products::accumulate<T>(
                        input.spectra[input_plane],fragment.spectra[plane],sums_[plane],corrections_[plane],stop);
                } else accumulation=scheduled_spectral_products::accumulate<T>(
                    input.spectra[input_plane],fragment.spectra[plane],sums_[plane],corrections_[plane],stop);
                residual_mass_upper_[plane]=scheduled_acquisition_certificate::add(
                    residual_mass_upper_[plane],accumulation.residual_mass_upper);
                const auto product_error=scheduled_acquisition_certificate::product_radius_l1<T>(
                    input.certificates[input_plane].norm_upper,fragment.certificates[plane].norm_upper,plan_.size);
                convolution_radius_[plane]=scheduled_acquisition_certificate::add(convolution_radius_[plane],
                    scheduled_acquisition_certificate::convolution_radius(input.certificates[input_plane],
                        fragment.certificates[plane],product_error,plan_.size));
                const auto eps=std::numeric_limits<T>::epsilon();
                roundoff_[plane]+=128.L*eps*(std::bit_width(plan_.size)+1)*
                    input.norms[input_plane]*fragment.norms[plane];
            }
            for(std::size_t at=0;at<starts_.size();++at) {
                check_stop(stop);
                long double energy=0;std::uint64_t count=0;bool refine=false;
                scheduled_acquisition_certificate::Interval bounded_energy;
                for(const auto run:fragment.runs) {
                    const auto first=starts_[at]+run.first,end=starts_[at]+run.end;
                    const auto delta=input.energy[end]-input.energy[first];
                    const auto bound=8*std::numeric_limits<long double>::epsilon()*
                        (std::abs(input.energy[end])+std::abs(input.energy[first]));
                    refine|=delta<0 || bound>1e-12L*std::max(1e-30L,std::abs(delta));
                    energy+=delta;count+=input.count[end]-input.count[first];++work_.prefix_queries;
                    bounded_energy=scheduled_acquisition_certificate::plus(bounded_energy,
                        scheduled_acquisition_certificate::energy_slice(input.certified_energy[end],input.certified_energy[first]));
                }
                if(refine) {
                    energy=0;bounded_energy={};
                    for(const auto run:fragment.runs)for(auto i=run.first;i<run.end;++i) {
                        const auto sample=starts_[at]+i;
                        if(present(input.original,sample)) {
                            const auto x=static_cast<long double>(input.original.pcm[sample]);energy+=x*x;
                            bounded_energy=scheduled_acquisition_certificate::plus(bounded_energy,
                                scheduled_acquisition_certificate::pcm_square(input.original.pcm[sample]));
                        }
                    }
                    ++work_.energy_refinements;
                }
                if(count>std::numeric_limits<std::uint64_t>::max()-count_[at])
                    throw Error("scheduled acquisition observation count overflow");
                energy_[at]+=energy;count_[at]+=count;
                certified_energy_[at]=scheduled_acquisition_certificate::plus(certified_energy_[at],bounded_energy);
            }
            ++work_.fragments;work_.spectral_products+=6*plan_.size;
        } catch(...) {failed_=true;throw;}
    }
    TraceReuse<T> trace_reuse()const {
        if(!finished_ || failed_ || !plan_.certificate.valid || !scheduled_acquisition_certificate::environment<T>())return {};
        TraceReuse<T> result;result.plan=&plan_;result.origin=origin_;result.starts=starts_;
        result.native_float=native_float_;
        if(native_float_) {
            result.native_sums={native_sums_[2],native_sums_[5]};result.native_corrections={native_corrections_[2],native_corrections_[5]};
        } else {result.sums={sums_[2],sums_[5]};result.corrections={corrections_[2],corrections_[5]};}
        result.trace=trace_values_;
        return result;
    }
    bool same_trace(const TraceReuse<T>& reuse)const {
        if(reuse.native_float!=native_float_ || reuse.plan!=&plan_ || reuse.origin!=origin_ || reuse.starts.size()!=starts_.size() ||
           reuse.trace.size()!=starts_.size() || !scheduled_acquisition_certificate::environment<T>())return false;
        if(!std::equal(starts_.begin(),starts_.end(),reuse.starts.begin()))return false;
        for(unsigned bit=0;bit<2;++bit) {
            const unsigned plane=3*bit+2;
            if(native_float_) {
                if(reuse.native_sums[bit].size()!=plan_.size || reuse.native_corrections[bit].size()!=plan_.size)return false;
                for(std::size_t i=0;i<plan_.size;++i) {
                    const auto equal=[](std::complex<float> a,std::complex<float> b) {
                        return std::bit_cast<std::uint32_t>(a.real())==std::bit_cast<std::uint32_t>(b.real()) &&
                               std::bit_cast<std::uint32_t>(a.imag())==std::bit_cast<std::uint32_t>(b.imag());
                    };
                    if(!equal(native_sums_[plane][i],reuse.native_sums[bit][i]) ||
                       !equal(native_corrections_[plane][i],reuse.native_corrections[bit][i]))return false;
                }
            } else {
                if(reuse.sums[bit].size()!=plan_.size || reuse.corrections[bit].size()!=plan_.size)return false;
                for(std::size_t i=0;i<plan_.size;++i) {
                    const auto equal=[](std::complex<double> a,std::complex<double> b) {
                        return std::bit_cast<std::uint64_t>(a.real())==std::bit_cast<std::uint64_t>(b.real()) &&
                               std::bit_cast<std::uint64_t>(a.imag())==std::bit_cast<std::uint64_t>(b.imag());
                    };
                    if(!equal(sums_[plane][i],reuse.sums[bit][i]) ||
                       !equal(corrections_[plane][i],reuse.corrections[bit][i]))return false;
                }
            }
        }
        return true;
    }
    std::span<const Value> finish(std::stop_token stop={},const TraceReuse<T>* reuse=nullptr) {
        if(failed_ || finished_)throw Error("scheduled acquisition accumulator cannot finish");
        try {
            check_stop(stop);
            if(starts_.empty()){finished_=true;return values_;}
            const bool shared_trace=reuse && same_trace(*reuse);
            if(reuse && !shared_trace)++work_.trace_reuse_fallbacks;
            std::array<double,6> certified_radii{};
            const bool native_environment=!native_float_ || scheduled_float_spectral_accumulator::environment();
            for(unsigned plane=0;plane<6;++plane) {
                scheduled_acquisition_certificate::Operand inverse_operand;
                double final_roundoff_l1=0;
                for(std::size_t i=0;i<plan_.size;++i) {
                    if((i&1023U)==0)check_stop(stop);
                    if(native_float_) {
                        const auto joined=scheduled_float_spectral_accumulator::finish_qualified(native_sums_[plane][i],native_corrections_[plane][i],native_environment);
                        scratch_[i]={static_cast<T>(joined.value.real()),static_cast<T>(joined.value.imag())};
                        // No conversion error: the inverse sees this exact FP32
                        // join. Its exact join residual is charged separately.
                        inverse_operand.observe({joined.value.real(),joined.value.imag()},scratch_[i],0.);
                        final_roundoff_l1=scheduled_acquisition_certificate::add(final_roundoff_l1,joined.radius_l1);
                    } else {
                        const auto before=sums_[plane][i]+corrections_[plane][i];
                        scratch_[i]=cast<T>(before);
                        inverse_operand.observe(before,scratch_[i],0.);
                        final_roundoff_l1=scheduled_acquisition_certificate::add(final_roundoff_l1,
                            scheduled_acquisition_certificate::add(scheduled_acquisition_certificate::ulp(before.real()),
                                                                   scheduled_acquisition_certificate::ulp(before.imag())));
                    }
                }
                const auto inverse_error=scheduled_acquisition_certificate::fft_radius<T>(plan_.certificate,
                    scheduled_acquisition_certificate::root(inverse_operand.native_square_upper),
                    scheduled_acquisition_certificate::root(inverse_operand.radius_square_upper),true);
                const auto u=std::numeric_limits<double>::epsilon()/2;
                const auto mass_terms=2*plan_.size*static_cast<std::size_t>(work_.fragments);
                // Each helper block has already enclosed the exact residual
                // mass; outward merges cover their own rounding. Correction,
                // underflow, final-add/cast/inverse bounds remain unchanged.
                const auto mass_upper=residual_mass_upper_[plane];
                const auto sum_gamma=scheduled_acquisition_certificate::gamma(work_.fragments,u);
                const auto underflow=scheduled_acquisition_certificate::divide(
                    scheduled_acquisition_certificate::mul(mass_terms,std::numeric_limits<double>::denorm_min()),
                    scheduled_acquisition_certificate::down(1-sum_gamma));
                const auto sum_error=native_float_?
                    scheduled_float_spectral_accumulator::accumulation_radius(static_cast<std::size_t>(work_.fragments),plan_.size,mass_upper,final_roundoff_l1,native_environment):
                    scheduled_acquisition_certificate::add(final_roundoff_l1,
                        scheduled_acquisition_certificate::add(scheduled_acquisition_certificate::mul(sum_gamma,mass_upper),underflow));
                certified_radii[plane]=scheduled_acquisition_certificate::add(convolution_radius_[plane],
                    scheduled_acquisition_certificate::add(scheduled_acquisition_certificate::divide(sum_error,plan_.size),inverse_error));
                if(native_float_)roundoff_[plane]+=scheduled_acquisition_certificate::divide(sum_error,plan_.size);
                const bool borrow=shared_trace && plane%3==2;
                if(borrow)++work_.trace_inverse_reuses;
                else {transform(scratch_,true,plan_,stop);++work_.inverse_transforms;}
                for(std::size_t at=0;at<starts_.size();++at) {
                    const auto value=borrow?Complex{static_cast<T>(reuse->trace[at][plane/3]),0}:
                        scratch_[plan_.shape.maximum_fragment-1+starts_[at]];
                    auto& fit=values_[at].fits[plane/3];
                    if(plane%3==0){fit.xc=value.real();fit.xs=value.imag();}
                    else if(plane%3==1){fit.cc=.5*value.real();fit.ss=-.5*value.real();fit.cs=.5*value.imag();}
                    else {fit.cc+=.5*value.real();fit.ss+=.5*value.real();trace_values_[at][plane/3]=value.real();}
                }
            }
            for(std::size_t at=0;at<starts_.size();++at) {
                auto& value=values_[at];value.physical_origin=add_coordinate(origin_,starts_[at]);
                value.certificate.energy_lower=std::max(0.,certified_energy_[at].lo);
                value.certificate.energy_upper=std::max(0.,certified_energy_[at].hi);
                value.certificate.valid=plan_.certificate.valid && native_environment &&
                    (!native_float_ || scheduled_float_spectral_accumulator::environment()) && scheduled_acquisition_certificate::environment<T>() &&
                    std::isfinite(value.certificate.energy_lower) && std::isfinite(value.certificate.energy_upper);
                for(unsigned bit=0;bit<2;++bit) {
                    auto& fit=value.fits[bit];fit.energy=static_cast<double>(energy_[at]);fit.count=count_[at];
                    if(!fit.count)fit={};
                    if(!finite(fit) || fit.energy<0)throw Error("scheduled acquisition result overflow");
                    const auto determinant=fit.cc*fit.ss-fit.cs*fit.cs;
                    value.certificate.dot_radius[bit]=certified_radii[3*bit];
                    const auto reconstruction=scheduled_acquisition_certificate::add(
                        scheduled_acquisition_certificate::add(scheduled_acquisition_certificate::ulp(fit.cc),
                                                              scheduled_acquisition_certificate::ulp(fit.ss)),
                        scheduled_acquisition_certificate::add(scheduled_acquisition_certificate::mul(2,
                            scheduled_acquisition_certificate::ulp(fit.cs)),scheduled_acquisition_certificate::mul(6,
                            std::numeric_limits<double>::denorm_min())));
                    value.certificate.gram_radius[bit]=scheduled_acquisition_certificate::add(reconstruction,
                        scheduled_acquisition_certificate::mul(.5,scheduled_acquisition_certificate::add(
                            certified_radii[3*bit+1],certified_radii[3*bit+2])));
                    value.certificate.valid=value.certificate.valid &&
                        std::isfinite(value.certificate.dot_radius[bit]) && std::isfinite(value.certificate.gram_radius[bit]);
                    value.dot_roundoff[bit]=static_cast<double>(roundoff_[3*bit]);
                    value.gram_roundoff[bit]=static_cast<double>(roundoff_[3*bit+1]+roundoff_[3*bit+2]);
                    const auto trace=fit.cc+fit.ss;
                    const auto minimum=.5*(trace-std::hypot(fit.cc-fit.ss,2*fit.cs));
                    const auto scale=std::sqrt(std::max(0.,fit.energy)*std::max(0.,trace));
                    value.requires_refinement[bit]=fit.count && (fit.count<=2 || fit.cc<0 || fit.ss<0 ||
                        !std::isfinite(determinant) || determinant<=1e-10*std::max(1.,fit.cc*fit.ss) ||
                        !std::isfinite(value.dot_roundoff[bit]) || !std::isfinite(value.gram_roundoff[bit]) ||
                        minimum<=2*value.gram_roundoff[bit] ||
                        value.dot_roundoff[bit]>.01*std::max(1e-30,scale));
                    if(certificate_diagnostics_){auto& p=certificate_profile_;
                        p.lambda.observe(minimum);scheduled_certificate_diagnostics::add(p.candidates,1,p.saturated);
                        if(value.requires_refinement[bit])scheduled_certificate_diagnostics::add(p.requires_refinement,1,p.saturated);}
                }
                if(certificate_diagnostics_ && value.certificate.valid)
                    scheduled_certificate_diagnostics::add(certificate_profile_.valid_certificates,1,certificate_profile_.saturated);
            }
            finished_=true;return values_;
        } catch(...) {failed_=true;throw;}
    }
};

// Sparse original-operand execution, with the SAME two independent rows and
// exact physical oscillator. Caller chooses measured algorithm crossover;
// storage/arithmetic choice does not force broad searches into direct work.
// This ADDS to the output. Refinement must reset the destination, then replay
// every fragment. The caller preserves sorted disjoint-fragment ownership, as
// Accumulator::add does; individual-pulse or overlapping-fragment sums are wrong.
inline void direct_add(const Input& input,const Fragment& fragment,std::uint64_t origin,
                       std::span<const std::size_t> starts,std::span<Value> values,
                       Work* work=nullptr,std::stop_token stop={}) {
    validate(input,fragment,starts,stop);
    if(values.size()!=starts.size() || input.physical_first!=add_coordinate(origin,fragment.physical_offset))
        throw Error("incompatible scheduled acquisition direct output");
    for(std::size_t at=0;at<starts.size();++at) {
        std::array<std::array<long double,6>,2> sums{};std::uint64_t count=0;
        for(std::size_t i=0;i<fragment.rows.size();++i) {
            if((i&255U)==0)check_stop(stop);
            const auto sample=starts[at]+i;
            if(!selected(fragment,i) || !present(input,sample))continue;
            ++count;
            const auto x=static_cast<double>(input.pcm[sample]);
            for(unsigned bit=0;bit<2;++bit) {
                const auto q=fragment.rows[i][bit]*input.oscillator[sample];
                const auto c=q.real(),s=q.imag();
                const std::array<double,6> p{x*c,x*s,c*c,s*s,c*s,x*x};
                for(unsigned j=0;j<6;++j) {
                    if(!std::isfinite(p[j]))throw Error("scheduled acquisition direct product overflow");
                    sums[bit][j]+=p[j];
                }
            }
        }
        auto& value=values[at];value.physical_origin=add_coordinate(origin,starts[at]);value.certificate.valid=false;
        for(unsigned bit=0;bit<2;++bit) {
            auto& fit=value.fits[bit];
            if(count>std::numeric_limits<std::uint64_t>::max()-fit.count)
                throw Error("scheduled acquisition observation count overflow");
            fit.xc+=static_cast<double>(sums[bit][0]);fit.xs+=static_cast<double>(sums[bit][1]);
            fit.cc+=static_cast<double>(sums[bit][2]);fit.ss+=static_cast<double>(sums[bit][3]);
            fit.cs+=static_cast<double>(sums[bit][4]);fit.energy+=static_cast<double>(sums[bit][5]);fit.count+=count;
            if(!finite(fit))throw Error("scheduled acquisition direct result overflow");
        }
        if(work)work->direct_sample_pairs+=2*count;
    }
    if(work)++work->fragments;
}
} // namespace datapump::modem::detail::scheduled_acquisition_tile
