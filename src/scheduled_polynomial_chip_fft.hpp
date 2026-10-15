#pragma once
// Bounded physical overlap-save producer for ALL polynomial C34 moments.
// No source templates, owner decisions, scores or receiver state live here.
#include "scheduled_polynomial_chip_moments.hpp"
#include "search_fft.hpp"
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
#include <vector>

namespace datapump::modem::detail::scheduled_polynomial_chip_fft {
namespace poly=scheduled_polynomial_chip;
namespace cert=scheduled_acquisition_certificate;
using Complex=std::complex<double>;using Interval=cert::Interval;
inline constexpr std::size_t maximum_fft=8192,helper_reservation=8192;
struct Config {std::size_t fft_size=4096,workspace_bytes=16*1024*1024;};
inline void stop_check(std::stop_token stop){if(stop.stop_requested())throw Error("polynomial chip FFT cancelled");}
struct InputTile {
    // Actual stored raw carrier samples, including ORIGINAL per-block seed,
    // multiply recurrence and restart conventions. Opaque key labels alone do
    // not prove this producer identity. Caller retains capture/fence provenance.
    poly::PhysicalKey key;
    std::span<const float> pcm;
    std::span<const Complex> oscillator;
    std::span<const std::uint8_t> observed; // empty: every supplied row exists
};
struct Entry {poly::PhysicalKey key;poly::Moments moments;};
struct Work {
    std::uint64_t input_transforms=0,inverse_transforms=0,spectral_products=0,input_rows=0,selected_windows=0,cache_installs=0;
    std::size_t plan_bytes=0,execution_bytes=0,workspace_bytes=0,fft_size=0;
};
struct Result {std::vector<Entry> entries;Work work;bool complete=false;};
class Plan {
    std::array<std::vector<Complex>,7> kernels;
    std::array<cert::Operand,7> kernel_certificates;
    static Config checked(Config config) {
        if(!cert::environment<double>() || !std::has_single_bit(config.fft_size) ||
           config.fft_size<64 || config.fft_size>maximum_fft)throw Error("unsupported polynomial chip FFT plan");
        if(reservation(config)>config.workspace_bytes)throw Error("polynomial chip FFT plan budget exceeded");
        return config;
    }
public:
    const Config config;
    const search_fft::DoublePlan native;
    const cert::Plan certificate;
    static std::size_t reservation(Config config={}) {
        if(!std::has_single_bit(config.fft_size) || config.fft_size<64 || config.fft_size>maximum_fft)
            throw Error("unsupported polynomial chip FFT reservation");
        auto bytes=poly::add_size(sizeof(Plan),helper_reservation);
        // Native table N-1 plus seven N spectra. Capacity-rounded certificate
        // stages and local construction Basis are covered before any allocation.
        bytes=poly::add_size(bytes,poly::mul_size(8,poly::mul_size(config.fft_size,sizeof(Complex))));
        return poly::add_size(bytes,poly::add_size(sizeof(cert::Plan),
            poly::mul_size(2*std::bit_width(config.fft_size),sizeof(cert::Stage))));
    }
    explicit Plan(Config dimensions={},std::stop_token stop={})
        :config(checked(dimensions)),native(config.fft_size,stop),certificate(native,stop) {
        if(!certificate.valid || certificate.size!=config.fft_size)throw Error("polynomial chip FFT certificate unavailable");
        // Owned construction scratch, not a hidden process-static or TLS table.
        const poly::Basis basis;
        for(auto& kernel:kernels)kernel.resize(config.fft_size);
        if(working_bytes()>config.workspace_bytes)throw Error("polynomial chip FFT actual plan budget exceeded");
        for(std::size_t n=0;n<poly::cell_samples;++n) {
            stop_check(stop);double power=1;
            for(std::size_t k=0;k<7;++k) {
                auto& actual=kernels[k][33-n];actual={power,0};
                const auto difference=cert::minus(Interval::point(power),basis.powers[n][k]);
                const auto error=std::max(std::abs(difference.lo),std::abs(difference.hi));
                kernel_certificates[k].observe(actual,actual,error);power*=basis.t[n];
            }
        }
        for(std::size_t k=0;k<7;++k) {
            search_fft::fft(std::span<Complex>(kernels[k]),false,native,stop);
            kernel_certificates[k].finish<double>(certificate,std::span<const Complex>(kernels[k]));
            if(!std::isfinite(kernel_certificates[k].forward_radius) || !std::isfinite(kernel_certificates[k].norm_upper))
                throw Error("polynomial chip FFT kernel overflow");
        }
        stop_check(stop);
        if(!cert::environment<double>())throw Error("polynomial chip FFT plan environment changed");
    }
    Plan(const Plan&)=delete;Plan& operator=(const Plan&)=delete;
    std::size_t maximum_input_samples()const{return config.fft_size;}
    std::size_t maximum_payload_samples()const{return config.fft_size-33;}
    std::uint64_t kernel_transforms()const{return 7;}
    std::span<const Complex> kernel(std::size_t k)const{return kernels.at(k);}
    const cert::Operand& kernel_certificate(std::size_t k)const{return kernel_certificates.at(k);}
    std::size_t working_bytes()const {
        auto bytes=poly::add_size(poly::add_size(sizeof(*this),helper_reservation),
            poly::add_size(native.working_bytes(),certificate.working_bytes()));
        for(const auto& kernel:kernels)bytes=poly::add_size(bytes,poly::mul_size(kernel.capacity(),sizeof(Complex)));
        return bytes;
    }
    // Excludes this Plan and borrowed PCM/carrier/mask storage. Includes all
    // simultaneous producer/result arrays and outlined stack allowance.
    std::size_t execution_reservation(std::size_t input_samples,std::size_t selected_windows)const {
        if(input_samples<34 || input_samples>config.fft_size || selected_windows>input_samples-33)
            throw Error("invalid polynomial chip FFT execution reservation");
        auto bytes=poly::add_size(sizeof(Result),helper_reservation);
        if(!selected_windows)return bytes;
        bytes=poly::add_size(bytes,poly::mul_size(4,poly::mul_size(config.fft_size,sizeof(Complex))));
        bytes=poly::add_size(bytes,poly::mul_size(input_samples+1,sizeof(double)+sizeof(Interval)+sizeof(std::uint32_t)));
        return poly::add_size(bytes,poly::mul_size(selected_windows,sizeof(Entry)));
    }
};
inline bool present(const InputTile& input,std::size_t i){return input.observed.empty() || input.observed[i]!=0;}
inline void validate(const InputTile& input,std::span<const std::size_t> selected,const Plan& plan,std::stop_token stop) {
    if(!cert::environment<double>() || input.pcm.size()<34 || input.pcm.size()>plan.maximum_input_samples() ||
       input.oscillator.size()!=input.pcm.size() || (!input.observed.empty() && input.observed.size()!=input.pcm.size()) ||
       selected.size()>input.pcm.size()-33 || !input.key.capture || !input.key.carrier || !input.key.fence ||
       !input.key.frequency || !input.key.layout ||
       input.pcm.size()>std::numeric_limits<std::uint64_t>::max()-input.key.physical_first ||
       input.key.screen_capture_end<input.key.physical_first+input.pcm.size())
        throw Error("invalid polynomial chip FFT physical tile");
    for(std::size_t i=0;i<input.pcm.size();++i) {
        if((i&255U)==0)stop_check(stop);
        if((!input.observed.empty() && input.observed[i]>1) ||
           (present(input,i) && (!std::isfinite(input.pcm[i]) || !cert::finite(input.oscillator[i]))))
            throw Error("nonfinite polynomial chip FFT physical observation");
    }
    for(std::size_t j=0;j<selected.size();++j) {
        if((j&255U)==0)stop_check(stop);
        if(selected[j]>input.pcm.size()-34 || (j && selected[j]<=selected[j-1]))
            throw Error("invalid polynomial chip FFT selected windows");
    }
}

// Circular convolution aliases only full linear outputs N..N+32 into0..32.
// Requested start j has indexj+33, with33<=j+33<=input_size-1<=N-1;
// hence EVERY requested coefficient equals its ordinary34-row linear dot.
// Result-by-value is the publication boundary. All18 planes must complete.
inline Result execute(const InputTile& input,std::span<const std::size_t> selected,
                      const Plan& plan,std::stop_token stop={}) {
    validate(input,selected,plan,stop);Result result;
    result.work.plan_bytes=plan.working_bytes();result.work.fft_size=plan.config.fft_size;
    const auto reserved=plan.execution_reservation(input.pcm.size(),selected.size());
    if(poly::add_size(result.work.plan_bytes,reserved)>plan.config.workspace_bytes)
        throw Error("polynomial chip FFT execution budget exceeded");
    if(selected.empty()){result.work.execution_bytes=reserved;result.work.workspace_bytes=result.work.plan_bytes+reserved;result.complete=true;return result;}
    std::array<std::vector<Complex>,3> spectra;
    for(auto& row:spectra)row.resize(plan.config.fft_size);
    std::array<cert::Operand,3> input_certificates;
    std::vector<Complex> scratch(plan.config.fft_size);
    std::vector<double> energy_prefix(input.pcm.size()+1);
    std::vector<Interval> energy_bounds(input.pcm.size()+1);
    std::vector<std::uint32_t> observed_prefix(input.pcm.size()+1);
    result.entries.resize(selected.size());
    auto bytes=poly::add_size(sizeof(Result),helper_reservation);
    for(const auto& row:spectra)bytes=poly::add_size(bytes,poly::mul_size(row.capacity(),sizeof(Complex)));
    bytes=poly::add_size(bytes,poly::mul_size(scratch.capacity(),sizeof(Complex)));
    bytes=poly::add_size(bytes,poly::mul_size(energy_prefix.capacity(),sizeof(double)));
    bytes=poly::add_size(bytes,poly::mul_size(energy_bounds.capacity(),sizeof(Interval)));
    bytes=poly::add_size(bytes,poly::mul_size(observed_prefix.capacity(),sizeof(std::uint32_t)));
    bytes=poly::add_size(bytes,poly::mul_size(result.entries.capacity(),sizeof(Entry)));
    result.work.execution_bytes=bytes;result.work.workspace_bytes=poly::add_size(result.work.plan_bytes,bytes);
    if(result.work.workspace_bytes>plan.config.workspace_bytes)throw Error("polynomial chip FFT actual execution budget exceeded");
    double carrier_square_upper=0;
    for(std::size_t i=0;i<input.pcm.size();++i) {
        if((i&255U)==0)stop_check(stop);
        observed_prefix[i+1]=observed_prefix[i];energy_prefix[i+1]=energy_prefix[i];energy_bounds[i+1]=energy_bounds[i];
        if(!present(input,i))continue;++observed_prefix[i+1];
        const auto x=static_cast<double>(input.pcm[i]);const auto o=input.oscillator[i];
        const Complex dot=o*x,image=o*o,trace{std::norm(o),0};
        const std::array<Complex,3> values{dot,image,trace};
        const std::array<double,3> errors{cert::complex_preproduct_radius({x,0},o),
            cert::complex_preproduct_radius(o,o),cert::norm_preproduct_radius(o)};
        for(std::size_t kind=0;kind<3;++kind) {
            if(!cert::finite(values[kind]) || !poly::nonnegative(errors[kind]))throw Error("polynomial chip FFT input preproduct overflow");
            spectra[kind][i]=values[kind];input_certificates[kind].observe(values[kind],values[kind],errors[kind]);
        }
        carrier_square_upper=std::max(carrier_square_upper,cert::add(cert::mul(o.real(),o.real()),cert::mul(o.imag(),o.imag())));
        energy_prefix[i+1]+=x*x;
        energy_bounds[i+1]=poly::interval_plus(energy_bounds[i+1],cert::pcm_square(input.pcm[i]));
    }
    result.work.input_rows=input.pcm.size();result.work.selected_windows=selected.size();
    for(std::size_t j=0;j<selected.size();++j) {
        const auto first=selected[j],end=first+34;
        if(observed_prefix[end]-observed_prefix[first]!=34)throw Error("polynomial chip FFT requested missing physical rows");
        auto& entry=result.entries[j];entry.key=input.key;entry.key.physical_first+=first;
        auto& m=entry.moments;m.count=34;m.carrier_square_upper=carrier_square_upper;
        m.energy.value=energy_prefix[end]-energy_prefix[first];m.energy.interval=cert::energy_slice(energy_bounds[end],energy_bounds[first]);
        if(!poly::nonnegative(m.energy.value) || !m.energy.interval.bounded() || !poly::nonnegative(carrier_square_upper))
            throw Error("polynomial chip FFT prefix energy or carrier overflow");
    }
    for(std::size_t kind=0;kind<3;++kind) {
        search_fft::fft(std::span<Complex>(spectra[kind]),false,plan.native,stop);
        input_certificates[kind].finish<double>(plan.certificate,std::span<const Complex>(spectra[kind]));++result.work.input_transforms;
    }
    for(std::size_t kind=0;kind<3;++kind)for(std::size_t k=0;k<(kind==0?4U:7U);++k) {
        stop_check(stop);const auto& a=input_certificates[kind];const auto& b=plan.kernel_certificate(k);
        const auto kernel=plan.kernel(k);
        cert::Operand product;
        for(std::size_t i=0;i<plan.config.fft_size;++i) {
            if((i&1023U)==0)stop_check(stop);scratch[i]=spectra[kind][i]*kernel[i];
            if(!cert::finite(scratch[i]))throw Error("polynomial chip FFT spectral product overflow");
            product.observe(scratch[i],scratch[i],0);
        }
        const auto radius=cert::add(cert::convolution_radius(a,b,
            cert::product_radius_l1<double>(a.norm_upper,b.norm_upper,plan.config.fft_size),plan.config.fft_size),
            cert::fft_radius<double>(plan.certificate,cert::root(product.native_square_upper),0,true));
        if(!poly::nonnegative(radius))throw Error("polynomial chip FFT radius overflow");
        search_fft::fft(std::span<Complex>(scratch),true,plan.native,stop);
        ++result.work.inverse_transforms;result.work.spectral_products+=plan.config.fft_size;
        for(std::size_t j=0;j<selected.size();++j) {
            if((j&255U)==0)stop_check(stop);const auto value=scratch[selected[j]+33];
            if(!cert::finite(value))throw Error("polynomial chip FFT inverse overflow");
            auto& moments=result.entries[j].moments;
            if(kind<2) {
                auto& output=kind==0?moments.dot[k]:moments.image[k];
                output={value,Interval::disk(value.real(),radius),Interval::disk(value.imag(),radius)};
            } else {
                auto& output=moments.trace[k];output={value.real(),Interval::disk(value.real(),radius)};
                if((k&1U)==0)output.interval.lo=std::max(0.,output.interval.lo); // known even-power positivity
            }
        }
    }
    stop_check(stop);
    for(auto& entry:result.entries) {
        entry.moments.valid=true;
        if(!poly::valid_moments(entry.moments))throw Error("polynomial chip FFT invalid completed moment certificate");
    }
    if(!cert::environment<double>())throw Error("polynomial chip FFT environment changed");
    result.complete=true;return result;
}

// All arithmetic/FFT completion precedes cache installation. Each installed
// item is a complete certified34-row result. Cancellation during installation
// may retain an eligible subset of complete optional cache entries; it never
// publishes a partial moment or receiver result. Caller serializes cache use.
inline Work fill_cache(const InputTile& input,std::span<const std::size_t> selected,
        const Plan& plan,poly::MomentCache& cache,std::stop_token stop={}) {
    if(selected.size()>cache.capacity())throw Error("polynomial chip FFT cache cannot retain selected outputs");
    auto result=execute(input,selected,plan,stop);stop_check(stop);
    for(const auto& entry:result.entries) {
        const auto before=cache.counters().certified_builds;cache.insert_certified(entry.key,entry.moments,stop);
        const auto after=cache.counters().certified_builds;if(after>before)result.work.cache_installs+=after-before;
    }
    return result.work;
}
} // namespace datapump::modem::detail::scheduled_polynomial_chip_fft
