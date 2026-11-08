#include "datapump/triad_shaping.hpp"
#include "datapump/triad_pulse.hpp"
#include "search_fft.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <string_view>
#include <vector>

namespace datapump::triad::v3 {
namespace {
using Spectrum = std::vector<double>;
constexpr double span = 8;
double smooth(double x) { x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x)); }
void require(bool ok,const char* why) { if(!ok)throw Error(why); }
void validate(const ShapingOptions& o) {
    require(std::isfinite(o.tail_energy_fraction)&&o.tail_energy_fraction>0&&o.tail_energy_fraction<=.005,
        "triad shaping energy-tail budget must be in (0,.005]");
    require(o.maximum_phase_samples>=8&&o.maximum_phase_samples<=256&&
        o.maximum_fft_size>=4096&&o.maximum_fft_size<=131072,
        "triad shaping work bounds outside supported range");
}
double gate(const Format& f,long double seconds) {
    if(!f.fhss)return 1;
    const auto within=seconds-std::floor(seconds/f.dwell_seconds)*f.dwell_seconds;
    return smooth(static_cast<double>((std::min(within,f.dwell_seconds-within)-f.quiet_seconds)/f.ramp_seconds));
}
bool same_allocation(const Geometry& a,const Geometry& b) {
    return a.tile_seconds==b.tile_seconds&&a.tile_count==b.tile_count&&
        a.payload_fine_cells_per_tile==b.payload_fine_cells_per_tile&&
        a.fine_cells==b.fine_cells&&a.source_samples==b.source_samples&&a.fine_coefficients==b.fine_coefficients;
}
bool repairable_aperture_failure(std::string_view message) {
    return message=="triad v3 FHSS has no protected dwell cells"||
        message=="triad v3 FHSS aperture cannot contain a protected fine cell"||
        message=="triad v3 center-aligned FHSS requires integer fine cells per dwell";
}
struct Profile {
    Spectrum energy;
    double df=0,width=0,total=0,far=0;
    unsigned phases=0;
    std::size_t nfft=0,ffts=0;
};
// Periodic gate phases are exact when the fine grid is commensurate and small.
// Large/noncommensurate phase banks use bounded midpoint quadrature, checked
// independently at twice the phase count before a certificate is accepted.
Profile profile(const Format& f,const Geometry& g,const ShapingOptions& o,unsigned oversample,unsigned phase_limit,
                std::size_t& total_kernel_ffts) {
    const double rate=g.fine_chip_rate_hz;
    const auto wanted=static_cast<std::size_t>(oversample)*128;
    require(wanted<=o.maximum_fft_size,"triad shaping FFT work cap");
    const auto nfft=std::bit_ceil(wanted);
    require(nfft<=o.maximum_fft_size,"triad shaping FFT work cap");
    const auto n=static_cast<std::size_t>(2*span*oversample)+1;
    const auto nfft_double=static_cast<double>(nfft);
    const double df=rate*oversample/nfft_double;
    const auto cells_per_dwell=rate*f.dwell_seconds;
    const auto nearest=std::round(cells_per_dwell);
    const bool commensurate=nearest>=1&&std::abs(cells_per_dwell-nearest)<=
        4*std::numeric_limits<double>::epsilon()*nearest;
    const auto phases=f.fhss?static_cast<unsigned>(commensurate&&nearest<=phase_limit?nearest:phase_limit):1U;
    const double offset=f.fhss_center_aligned?0:.5;
    const bool exact_phase=!f.fhss||(commensurate&&nearest<=phase_limit);
    require(!f.burst_wait_tiles||commensurate||g.tile_count<=8,
        "triad shaping noncommensurate burst calendar exceeds bounded endpoint model");
    const auto tile_cells=g.tile_seconds*rate;
    if(f.burst_wait_tiles&&g.tile_count>8)
        require(std::abs(tile_cells-std::round(tile_cells))<=1e-9*std::max(1.,tile_cells),
            "triad shaping burst phase does not repeat");
    modem::detail::search_fft::DoublePlan fft_plan(nfft);
    std::vector<Complex> work(nfft);
    std::vector<double> pulse(n),chip_time(n);
    for(std::size_t j=0;j<n;++j){chip_time[j]=static_cast<double>(j)/oversample-span;
        pulse[j]=shaping_pulse(f.pulse_shape,chip_time[j]);}
    std::size_t fft_count=0;
    const auto kernel=[&](long double center,bool actual) {
        std::fill(work.begin(),work.end(),Complex{});
        for(std::size_t j=0;j<n;++j){const long double t=center+chip_time[j]/rate;
            work[j]=pulse[j]*(actual?shaping_envelope(f,g,t):gate(f,t));}
        modem::detail::search_fft::fft(work,false,fft_plan);++fft_count;++total_kernel_ffts;
        Spectrum result(nfft);for(std::size_t j=0;j<nfft;++j)result[j]=std::norm(work[j]);
        return result;
    };
    std::vector<Spectrum> base;
    base.reserve(phases);
    for(unsigned j=0;j<phases;++j){const auto center=exact_phase?(j+offset)/rate:
        (j+.5)*f.dwell_seconds/phases;base.push_back(kernel(center,false));}
    Spectrum sum(nfft);
    bool has_active_cells=false;
    const auto add_chunk=[&](std::uint64_t logical,double weight) {
        const long double start=logical*static_cast<long double>(f.burst_wait_tiles+1)*g.tile_seconds;
        const long double end=start+(f.burst_wait_tiles?g.tile_seconds:g.bit_seconds);
        const auto begin=static_cast<std::uint64_t>(std::max(0.L,std::ceil(start*rate-offset)));
        const auto finish=static_cast<std::uint64_t>(std::max(static_cast<long double>(begin),std::ceil(end*rate-offset)));
        const auto count=finish-begin;
        has_active_cells=has_active_cells||(count>0&&weight>0);
        if(exact_phase){
            for(unsigned j=0;j<phases;++j){
                const auto remainder=begin%phases;
                const auto first=begin+(j+phases-remainder)%phases;
                const auto multiplicity=first<finish?1+(finish-1-first)/phases:0;
                for(std::size_t k=0;k<nfft;++k)sum[k]+=base[j][k]*(static_cast<double>(multiplicity)*weight);
            }
        } else {
            for(unsigned j=0;j<phases;++j)for(std::size_t k=0;k<nfft;++k)
                sum[k]+=base[j][k]*(static_cast<double>(count)*weight/phases);
        }
        // Only these cells see the finite global/burst endpoint envelope.
        // Complete finite pulses are transformed, including the gate ramps.
        const auto edge=std::max(f.ramp_seconds,span/rate)+span/rate;
        const auto endpoint_cells=static_cast<std::uint64_t>(std::ceil(edge*rate))+2;
        require(endpoint_cells<=128,"triad shaping endpoint-kernel work cap");
        const auto correct=[&](std::uint64_t cell){
            const auto center=(static_cast<long double>(cell)+offset)/rate;
            auto actual=kernel(center,true);
            const auto reference=exact_phase?base[cell%phases]:kernel(center,false);
            for(std::size_t k=0;k<nfft;++k)sum[k]+=(actual[k]-reference[k])*weight;
        };
        const auto left_end=std::min(finish,begin+endpoint_cells);
        for(auto cell=begin;cell<left_end;++cell)correct(cell);
        for(auto cell=std::max(left_end,finish-std::min(count,endpoint_cells));cell<finish;++cell)correct(cell);
    };
    if(!f.burst_wait_tiles)add_chunk(0,1);
    else if(g.tile_count<=8){for(std::uint64_t tile=0;tile<g.tile_count;++tile)add_chunk(tile,1);}
    else {add_chunk(0,1);add_chunk(1,static_cast<double>(g.tile_count-2));add_chunk(g.tile_count-1,1);}
    require(has_active_cells,"triad shaping has no active coefficients");
    // The phase-quadrature baseline and exact endpoint corrections can produce
    // tiny negative bins; a material negative value invalidates this model.
    const auto peak=*std::max_element(sum.begin(),sum.end());
    for(auto& x:sum){require(x>=-1e-6*peak,"triad shaping phase quadrature unresolved at endpoints");x=std::max(0.,x);}
    Spectrum centered(nfft);
    for(std::size_t j=0;j<nfft;++j)centered[j]=sum[(j+nfft/2)%nfft];
    const auto interpolate=[&](double index){
        if(index<0||index>=nfft_double-1)return 0.;
        const auto k=static_cast<std::size_t>(index);
        return std::lerp(centered[k],centered[k+1],index-static_cast<double>(k));
    };
    Spectrum triple(nfft);
    for(std::size_t j=0;j<nfft;++j)for(int c=-1;c<=1;++c)
        triple[j]+=interpolate(static_cast<double>(j)-c*g.carrier_spacing_hz/df);
    const double total=std::accumulate(triple.begin(),triple.end(),0.);
    require(std::isfinite(total)&&total>0,"triad shaping nonfinite spectrum");
    std::vector<std::size_t> order(nfft);std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](auto a,auto b){return std::abs(static_cast<long long>(a)-static_cast<long long>(nfft/2))<
        std::abs(static_cast<long long>(b)-static_cast<long long>(nfft/2));});
    double collected=0,width=0,far=0;
    for(const auto j:order){collected+=triple[j];if(collected>=total*(1-o.tail_energy_fraction)){
        width=2*static_cast<double>(std::abs(static_cast<long long>(j)-static_cast<long long>(nfft/2)))*df;break;}}
    for(std::size_t j=0;j<nfft;++j){const auto fbin=(static_cast<double>(j)-nfft_double/2)*df;
        if(std::abs(fbin)>2*g.occupied_bandwidth_hz)far+=triple[j];}
    return {std::move(triple),df,width,total,far/total,phases,nfft,fft_count};
}
}

const char* pulse_shape_name(PulseShape p) {
    switch(p){case PulseShape::Rrc:return "rrc-025-span8";
        case PulseShape::TaperedRrc:return "rrc-025-span8-c2-tail2";}
    throw Error("triad shaping invalid pulse descriptor");
}
double shaping_pulse(PulseShape p,double t) {
    if(p==PulseShape::Rrc)return rrc_pulse(t);
    require(p==PulseShape::TaperedRrc,"triad shaping invalid pulse descriptor");
    constexpr unsigned resolution=256,count=8*resolution;
    static const auto table=[] {
        std::array<double,count+1> values{};
        for(unsigned j=0;j<=count;++j){const double coordinate=static_cast<double>(j)/resolution;
            values[j]=rrc_pulse(coordinate)*smooth((8-coordinate)/2);}
        double energy=values[0]*values[0]+values[count]*values[count];
        for(unsigned j=1;j<count;++j)energy+=2*values[j]*values[j];
        const auto scale=std::sqrt(resolution/energy);for(auto& x:values)x*=scale;
        return values;
    }();
    const auto position=std::abs(t)*resolution;
    if(!(position<=count))return 0;
    const auto index=static_cast<unsigned>(position);
    if(index==count)return table[count];
    return std::lerp(table[index],table[index+1],position-index);
}
double shaping_envelope(const Format& f,const Geometry& g,long double t) {
    if(t<0||t>=g.bit_seconds)return 0;
    const long double edge=std::max(f.ramp_seconds,span/g.fine_chip_rate_hz);
    double result=smooth(static_cast<double>(t/edge))*smooth(static_cast<double>((g.bit_seconds-t)/edge));
    if(f.burst_wait_tiles){const auto id=static_cast<std::uint64_t>(std::floor(t/g.tile_seconds));
        if(id%(f.burst_wait_tiles+1)!=0||id/(f.burst_wait_tiles+1)>=g.tile_count)return 0;
        const auto within=t-id*static_cast<long double>(g.tile_seconds);
        result*=smooth(static_cast<double>(within/edge))*smooth(static_cast<double>((g.tile_seconds-within)/edge));}
    if(f.fhss)result*=gate(f,t);
    return result;
}
namespace {
SpectralCertificate shaping_spectrum_impl(const Format& f,const ShapingOptions& o,std::size_t& total_kernel_ffts) {
    validate(o);const auto g=geometry(f);(void)pulse_shape_name(f.pulse_shape);
    // Every accepted certificate needs the256-samples/chip refinement. Do not
    // execute an otherwise eligible coarse profile when that mandatory second
    // resolution cannot fit the caller's FFT cap.
    require(o.maximum_fft_size>=256U*128U,"triad shaping FFT work cap");
    const auto initial_work=total_kernel_ffts;
    struct Cached {unsigned oversample,phases;Profile value;};
    // At most7 distinct profiles for the64/128/256 ladder. Retain completed
    // public spectra so a later coarse look reuses an earlier phase check.
    std::vector<Cached> cache;cache.reserve(8);
    const auto get=[&](unsigned oversample,unsigned phases)->const Profile& {
        for(const auto& entry:cache)if(entry.oversample==oversample&&entry.phases==phases)return entry.value;
        require(cache.size()<8,"triad shaping adaptive profile cache cap");
        auto value=profile(f,g,o,oversample,phases,total_kernel_ffts);
        cache.push_back({oversample,phases,std::move(value)});return cache.back().value;
    };
    unsigned phases=std::min(64U,o.maximum_phase_samples),attempts=0;
    for(;;){
        ++attempts;
        try{
    const auto& coarse=get(128,std::max(8U,phases/2));
    const auto& refined=get(256,phases);
    const auto& phase_check=coarse.phases!=refined.phases?get(128,phases):coarse;
    const auto allowance=std::max(4*coarse.df,.03*refined.width);
    const bool sampling_converged=std::abs(phase_check.width-refined.width)<=allowance;
    const bool phase_converged=std::abs(coarse.width-phase_check.width)<=allowance;
    const bool converged=sampling_converged&&phase_converged;
    // More gate phases cannot resolve a sampling-only discrepancy. Keep the
    // conservative fallback in that case instead of repeating identical work.
    if(!phase_converged&&phases<o.maximum_phase_samples){phases=std::min(o.maximum_phase_samples,phases*2);continue;}
    const auto measured=std::max({coarse.width,phase_check.width,refined.width});
    const auto placement=std::max(g.occupied_bandwidth_hz,measured+4*coarse.df);
    double outside=0;for(std::size_t j=0;j<refined.energy.size();++j)
        if(std::abs((static_cast<double>(j)-static_cast<double>(refined.nfft/2))*refined.df)>placement/2)
            outside+=refined.energy[j];
    double pulse_energy=0;for(unsigned j=0;j<=4096;++j){const auto value=shaping_pulse(f.pulse_shape,-8+j/256.);
        pulse_energy+=value*value*(j==0||j==4096?.5:1)/256;}
    SpectralCertificate result;
    result.measured_bandwidth_hz=measured;result.placement_bandwidth_hz=placement;
    result.measured_tail_fraction=outside/refined.total;result.requested_tail_fraction=o.tail_energy_fraction;
    result.coarse_width_hz=coarse.width;result.refined_width_hz=refined.width;
    result.phase_refinement_width_hz=phase_check.width;
    result.phase_refinement_error_hz=std::abs(coarse.width-phase_check.width);
    result.sampling_refinement_error_hz=std::abs(phase_check.width-refined.width);
    result.frequency_bin_hz=refined.df;result.far_outside_energy_fraction=refined.far;
    result.normalized_pulse_energy=pulse_energy;result.phase_samples=refined.phases;
    result.fft_size=refined.nfft;result.kernel_ffts=coarse.ffts+refined.ffts+
        (coarse.phases!=refined.phases?phase_check.ffts:0);
    result.total_kernel_ffts=total_kernel_ffts-initial_work;result.phase_refinement_attempts=attempts;
    result.converged=converged;result.method="independent-zero-mean-coefficient finite-kernel energy spectrum; pulse/gate FFT with endpoint corrections";
    result.limitation="Expected emitted DATA energy, not compact support or a worst-key/channel bound. Residual tails remain emitted; receiver filtering/channel tails are unchanged. Sampling and phase-quadrature checks are engineering diagnostics.";
    return result;
        }catch(const Error& error){
            // Only this phase-quadrature failure can improve with more phase
            // samples. FFT/resource/geometry caps do not repeat ineligible work.
            if(std::string_view(error.what())!="triad shaping phase quadrature unresolved at endpoints"||
                phases>=o.maximum_phase_samples)throw;
            phases=std::min(o.maximum_phase_samples,phases*2);
        }
    }
}
}
SpectralCertificate shaping_spectrum(const Format& f,const ShapingOptions& o) {
    std::size_t work=0;return shaping_spectrum_impl(f,o,work);
}
ShapingPlan plan_shaping(const Format& input,const ShapingOptions& o) {
    validate(o);(void)pulse_shape_name(input.pulse_shape);
    std::optional<Geometry> original;
    std::string original_aperture_failure;
    try{original=geometry(input);}catch(const Error& e){
        // Only an aperture/lattice failure can be remedied by ramp/placement
        // selection. In particular preserve source workspace/coordinate caps
        // instead of misreporting them as a physical no-pulse condition.
        if(!input.fhss||!repairable_aperture_failure(e.what()))throw;
        original_aperture_failure=e.what();
    }
    std::vector<Format> candidates;
    std::string candidate_resource_failure;
    for(const bool aligned:{input.fhss_center_aligned,!input.fhss_center_aligned}){
        if(aligned&&(!input.fhss||(!o.allow_center_alignment&&!input.fhss_center_aligned)))continue;
        Format f=input;f.fhss_center_aligned=aligned;
        const double rate=f.inner_rate_hz*f.added_symbols/6;
        std::vector<double> ramps{f.ramp_seconds};
        if(f.fhss){
            const auto cells=rate*f.dwell_seconds,offset=aligned?0.:.5;
            const auto initial_guard=(f.quiet_seconds+f.margin_seconds+span/rate+f.ramp_seconds)*rate;
            const auto first=std::ceil(initial_guard-offset),last=std::floor(cells-initial_guard-offset);
            const auto maximum=std::min(first+offset,cells-last-offset)/rate-f.quiet_seconds-f.margin_seconds-span/rate;
            const auto feasible=f.dwell_seconds/2-f.quiet_seconds-f.margin_seconds-span/rate;
            const auto limit=first<=last?maximum:feasible;
            if(limit>1/effective_sample_rate(f))ramps.push_back(limit-1/effective_sample_rate(f));
            if(!original&&limit>0)ramps.push_back(limit*.5);
        }
        for(const auto ramp:ramps)for(const auto pulse:{PulseShape::Rrc,PulseShape::TaperedRrc}){
            f.ramp_seconds=ramp;f.pulse_shape=pulse;
            try{const auto g=geometry(f);if(original&&!same_allocation(*original,g))continue;
                if(std::none_of(candidates.begin(),candidates.end(),[&](const Format& x){return x.ramp_seconds==ramp&&
                    x.pulse_shape==pulse&&x.fhss_center_aligned==aligned;}))candidates.push_back(f);
            }catch(const Error& e){if(!repairable_aperture_failure(e.what()))candidate_resource_failure=e.what();}
        }
        if(!input.fhss)break;
    }
    if(candidates.empty()){
        if(!candidate_resource_failure.empty())throw Error(candidate_resource_failure);
        if(!original_aperture_failure.empty())throw Error(original_aperture_failure);
        throw Error("triad shaping cannot place a complete protected pulse within this dwell");
    }
    std::optional<ShapingPlan> best;
    std::string failures;
    std::size_t total_kernel_ffts=0;
    for(const auto& f:candidates){
        try{auto spectrum=shaping_spectrum_impl(f,o,total_kernel_ffts);if(!spectrum.converged){failures="sampling/phase quadrature did not converge";continue;}
            const auto g=geometry(f);
            if(!best||spectrum.placement_bandwidth_hz<best->spectrum.placement_bandwidth_hz-
                2*spectrum.frequency_bin_hz||
               (std::abs(spectrum.placement_bandwidth_hz-best->spectrum.placement_bandwidth_hz)<=
                2*spectrum.frequency_bin_hz&&spectrum.far_outside_energy_fraction<best->spectrum.far_outside_energy_fraction))
                best=ShapingPlan{f,g,std::move(spectrum),false,"validated bounded spectral candidate",static_cast<unsigned>(candidates.size())};
        }catch(const Error& e){failures=e.what();}
    }
    if(best){best->total_kernel_ffts=total_kernel_ffts;return *best;}
    const auto fallback=original?input:candidates.front();const auto g=geometry(fallback);
    SpectralCertificate certificate;certificate.requested_tail_fraction=o.tail_energy_fraction;
    certificate.placement_bandwidth_hz=g.occupied_bandwidth_hz+2/fallback.ramp_seconds;
    certificate.method="legacy nominal plus full 2/ramp engineering allowance";
    certificate.limitation="No validated spectral reduction; finite spectral tails are not compact support.";
    return {fallback,g,std::move(certificate),true,failures.empty()?"no validated spectral candidate":failures,
        static_cast<unsigned>(candidates.size()),total_kernel_ffts};
}
}
