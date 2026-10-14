#include "scheduled_acquisition_policy.hpp"
#include "scheduled_exact_source_samples.hpp"
#include "scheduled_work_profile.hpp"
#include "scheduled_acquisition_catalog.hpp"
#include "scheduled_acquisition_tile.hpp"
#include "scheduled_cold_colored_source_rows.hpp"
#include "scheduled_parallel_preparation.hpp"
#include "scheduled_lazy_family_runtime.hpp"
#include "scheduled_template_proxy_certificate.hpp"
#include "scheduled_frequency_postinverse.hpp"
#include "scheduled_diagnostic_journal.hpp"
#include "scheduled_acquisition_memory.hpp"
#include "scheduled_resume_state.hpp"
#include "shared_scheduled_capture.hpp"
#include "scheduled_processing_journal.hpp"
#include "scheduled_carrier_restart.hpp"
#include "scheduled_original_projection_cache.hpp"
#include <cfenv>
#include <deque>
#include <map>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include "datapump/pattern_correlator.hpp"
#include "datapump/execution.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/scheduled_symbol_calendar.hpp"
#include "search_parallel.hpp"
#include "pattern_correlator_batch.hpp"
#include "scheduled_common_simd.hpp"
#include "scheduled_outer_public_replay.hpp"
#include "scheduled_pulse_moments.hpp"
#include "scheduled_frequency_piece.hpp"
#include "scheduled_mapped_piece_groups.hpp"
#include "scheduled_temporal_templates.hpp"
#include "scheduled_private_batch.hpp"
#include "scheduled_r1_export.hpp"
#include "scheduled_flat_export.hpp"
#include "pattern_projection_cache.hpp"
#include "scheduled_float_signal_moments.hpp"
#include "scheduled_float_gram.hpp"
#include "pattern_envelope.hpp"
#include "search_fft.hpp"
#include "audio_trace.hpp"
#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <new>
#include <exception>
#include <numeric>
#include <set>
#include <chrono>
#include <bit>
#include <ctime>
#include <atomic>
#include <mutex>
#include <utility>
#include <tuple>
#include <openssl/crypto.h>

namespace datapump::modem {
namespace rx_cpu = ::datapump::detail::cpu_profile;
namespace {
using Complex = std::complex<double>;
namespace certdiag = detail::scheduled_certificate_diagnostics;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t block_size = 128;
constexpr std::size_t scheduled_block_size = 512;
constexpr std::size_t scheduled_diagnostic_runs = 1024;
void require(bool condition, const char* message) { if (!condition) throw Error(message); }
void cancelled(std::stop_token stop) { if (stop.stop_requested()) throw Error("pattern correlation cancelled"); }
// Bounds only: unlike correlation_pulse_segment(), this does not construct
// seventeen unused pulse values and derivatives. The same knot/closed-support
// endpoint checks delimit the affine table interval in physical samples.
std::uint64_t pulse_span_count(long double offset,std::uint64_t maximum,long double duration) {
    constexpr long double resolution=256;
    const auto position=[&](std::uint64_t n){return (offset+n)/duration-.5L;};
    const auto endpoint=[](long double q){
        for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j) {
            const auto p=static_cast<double>(q+8-static_cast<long double>(j));
            if(p==-8 || p==8)return true;
        }
        return false;
    };
    const auto q=position(0),knot=std::floor(resolution*q);
    if(endpoint(q))return 1;
    const auto distance=((knot+1)/resolution-q)*duration;
    auto count=distance>=maximum?maximum:static_cast<std::uint64_t>(std::max(1.L,std::ceil(distance)));
    while(count>1 && (std::floor(resolution*position(count-1))!=knot || endpoint(position(count-1))))--count;
    return count;
}
using Projection=detail::CorrelationProjection;
using Fit=detail::CorrelationFit;
using DriftFit=detail::CorrelationDriftFit;
using DifferentialFit=detail::CorrelationDifferentialFit;
// Ordinary receivers retain the original compact telemetry footprint.
// The public snapshot remains flat; scheduled-only counters live in ScheduledState.
struct CoreWork {
    PatternCorrelationBackend backend=PatternCorrelationBackend::raw;
    std::size_t hypotheses=0,lattices=0,phase_groups=0,peak_workspace_bytes=0;
    unsigned drift_sections=1;
    std::uint64_t differential_window_samples=0,samples=0,cells=0,segments=0;
    // Kernel preparation is included in frontend time and also shown separately.
    // Opt-in timers affect execution cost; use uninstrumented runs for speedup.
    double frontend_seconds=0,search_seconds=0,kernel_seconds=0;
    double frontend_cpu_seconds=0,search_cpu_seconds=0,kernel_cpu_seconds=0;
    // Validation is outside frontend. Pulse statistics are a frontend subset
    // which includes kernel time; the remainder is downconversion/prefix work.
    double validation_seconds=0,validation_cpu_seconds=0;
    double pulse_statistics_seconds=0,pulse_statistics_cpu_seconds=0;
    std::size_t affine_coefficient_cache_entries=0;
    std::uint64_t affine_preparations=0,affine_reuses=0,affine_interval_fast_paths=0;
    double affine_prepare_seconds=0,affine_fit_seconds=0;
    double affine_prepare_cpu_seconds=0,affine_fit_cpu_seconds=0;
    std::uint64_t amplitude_transforms=0,amplitude_observations=0,amplitude_cache_hits=0,amplitude_channel_updates=0;
    std::size_t amplitude_bins=0,amplitude_fft_samples=0,amplitude_channels=0;
    std::uint64_t amplitude_float_transforms=0,amplitude_precision_fallbacks=0;
    std::uint64_t amplitude_float_envelopes=0;
    std::uint64_t scheduled_null_samples=0,scheduled_omitted_fft_blocks=0;
    std::uint64_t scheduled_null_pushes=0;
    std::uint64_t scheduled_worker_preparations=0;
    // Samples retained for the next fresh source symbol before the current
    // conservative physical completion fence; included in ordinary search cost.
    std::uint64_t scheduled_carry_samples=0;
    std::uint64_t scheduled_colored_guard_activations=0,scheduled_quarter_guard_fallbacks=0;
    std::uint64_t scheduled_template_constructions=0,scheduled_template_cache_hits=0;
    std::size_t scheduled_template_cache_peak_bytes=0;
    std::uint64_t amplitude_publication_polls=0,amplitude_unchanged_polls=0;
};
struct ScheduledExtraWork {
    std::uint64_t scheduled_float_moment_prepared_samples=0;
    std::uint64_t scheduled_float_moment_preparations=0;
    std::uint64_t scheduled_float_moment_prepare_fallbacks=0;
    std::uint64_t scheduled_float_moment_attempts=0;
    std::uint64_t scheduled_float_moment_pieces=0;
    std::uint64_t scheduled_float_moment_condition_fallbacks=0;
    std::uint64_t scheduled_float_moment_error_fallbacks=0;
    std::uint64_t scheduled_float_moment_accepted_pairs=0;
    std::uint64_t scheduled_float_moment_fp64_pairs=0;
    std::uint64_t scheduled_float_moment_gram_fallbacks=0;
    std::size_t scheduled_exact_families=0;
    std::size_t scheduled_family_groups=0;
    std::size_t scheduled_family_metadata_bytes=0;
    std::uint64_t scheduled_family_dispatches=0;
    std::uint64_t scheduled_family_lanes=0;
    std::size_t scheduled_common_peak_bytes=0;
    std::uint64_t scheduled_common_families=0;
    std::uint64_t scheduled_common_lanes=0;
    std::uint64_t scheduled_common_pieces=0;
    std::uint64_t scheduled_common_shapes=0;
    std::uint64_t scheduled_common_raw_samples=0;
    std::uint64_t scheduled_common_raw_lane_samples=0;
    std::uint64_t scheduled_common_affine_preparations=0;
    std::uint64_t scheduled_common_affine_lanes=0;
    std::uint64_t scheduled_common_gram_fallbacks=0;
    std::uint64_t scheduled_common_mapped_pairs=0;
    std::uint64_t scheduled_common_mapped_failed_pairs=0;
    std::uint64_t scheduled_common_mapped_lanes=0;
    std::uint64_t scheduled_common_affine_reused_lanes=0;
    std::uint64_t scheduled_common_phase_attempts=0;
    std::uint64_t scheduled_common_phase_rejects=0;
    std::uint64_t scheduled_common_phase_families=0;
    std::uint64_t scheduled_common_phase_lanes=0;
    std::size_t scheduled_common_raw_peak_bytes=0;
    std::uint64_t scheduled_common_raw_fit_pairs=0;
    std::uint64_t scheduled_common_raw_reused_lane_samples=0;
    std::uint64_t scheduled_common_raw_packed_values=0;
    std::uint64_t scheduled_common_raw_workspace_samples=0;
    std::size_t scheduled_common_simd_peak_bytes=0;
    unsigned scheduled_common_simd_selected_isa=0;
    std::uint64_t scheduled_common_simd_attempts=0;
    std::uint64_t scheduled_common_simd_samples=0;
    std::uint64_t scheduled_common_simd_lane_samples=0;
    std::uint64_t scheduled_common_simd_packed_values=0;
    std::uint64_t scheduled_common_simd_guard_updates=0;
    std::uint64_t scheduled_common_simd_outer_updates=0;
    std::uint64_t scheduled_common_simd_short_samples=0;
    std::uint64_t scheduled_common_simd_workspace_samples=0;
    std::uint64_t scheduled_common_simd_isa_samples=0;
    std::uint64_t scheduled_common_simd_scalar_lane_samples=0;
    std::uint64_t scheduled_common_phase_address_hits=0;
    std::uint64_t scheduled_common_phase_address_misses=0;
    std::uint64_t scheduled_common_phase_address_queries=0;
    std::uint64_t scheduled_float_gram_attempts=0;
    std::uint64_t scheduled_float_gram_accepted_pairs=0;
    std::uint64_t scheduled_float_gram_range_fallbacks=0;
    std::uint64_t scheduled_float_gram_rank_fallbacks=0;
    std::uint64_t scheduled_float_gram_error_fallbacks=0;
    std::uint64_t scheduled_float_gram_helper_fallbacks=0;
    std::uint64_t scheduled_float_gram_signal_attempts=0;
    double scheduled_float_gram_maximum_relative_error=0;
};
struct Bank {
    double frequency=0;
    std::vector<Projection> prefix;
    std::vector<Complex> first_moment;
    std::vector<detail::CorrelationCarrierMoments> affine_carrier;
    std::array<std::uint64_t,4> affine_lengths{};
    std::span<const Projection> shared_prefix;
    std::span<const Complex> shared_moment;
    struct ProjectionView {
        std::span<const Projection> original;detail::PhysicalMomentCache::View physical;
        Projection operator[](std::size_t i)const{return physical?physical.projection(i):original[i];}
    };
    struct MomentView {
        std::span<const Complex> original;detail::PhysicalMomentCache::View physical;
        Complex operator[](std::size_t i)const{return physical?physical.moment(i).signal1:original[i];}
    };
    ProjectionView observations(detail::PhysicalMomentCache::View physical={})const{return {shared_prefix.empty()?std::span<const Projection>(prefix):shared_prefix,physical};}
    MomentView moments(detail::PhysicalMomentCache::View physical={})const{return {shared_moment.empty()?std::span<const Complex>(first_moment):shared_moment,physical};}
    void prepare_carrier(std::size_t block_samples,long double knot_duration,std::uint32_t sample_rate) {
        // Immutable during parallel lane scoring. A complete oscillator block
        // is the dominant length at large chips; retain three common knot
        // clips too, and compute any other length exactly in caller scratch.
        affine_carrier.resize(affine_lengths.size());affine_lengths[0]=block_samples;
        std::size_t used=1;
        const auto remainder=std::fmod(knot_duration,static_cast<long double>(block_samples));
        const auto lower=static_cast<std::uint64_t>(std::floor(remainder));
        const auto upper=static_cast<std::uint64_t>(std::ceil(remainder));
        const auto retain=[&](std::uint64_t count) {
            if(!count || count>block_samples || used==affine_lengths.size() ||
               std::find(affine_lengths.begin(),affine_lengths.begin()+static_cast<std::ptrdiff_t>(used),count)!=
                   affine_lengths.begin()+static_cast<std::ptrdiff_t>(used))return;
            affine_lengths[used++]=count;
        };
        retain(lower);retain(upper);
        if(knot_duration<block_samples)retain(lower?lower-1:0);
        else retain(block_samples-upper);
        for(std::uint64_t count=1;used<affine_lengths.size();++count)retain(count);
        for(std::size_t i=0;i<affine_lengths.size();++i)
            affine_carrier[i]=detail::correlation_carrier_moments(affine_lengths[i],2*tau*frequency/sample_rate);
    }
    const detail::CorrelationCarrierMoments* carrier(std::uint64_t count) const {
        if(affine_carrier.size()>affine_lengths.size())return count && count<=affine_carrier.size()?&affine_carrier[count-1]:nullptr;
        for(std::size_t i=0;i<affine_carrier.size();++i)
            if(affine_lengths[i]==count)return &affine_carrier[i];
        return nullptr;
    }
};
struct WorkTimer {
    bool enabled;
    double& wall;
    double& cpu;
    std::chrono::steady_clock::time_point start{};
    std::clock_t cpu_start=0;
    double weight=1;
    WorkTimer(bool active,double& seconds,double& cpu_seconds,double scale=1):enabled(active),wall(seconds),cpu(cpu_seconds),weight(scale) {
        if(enabled){start=std::chrono::steady_clock::now();cpu_start=std::clock();}
    }
    ~WorkTimer(){finish();}
    void finish() {
        if(!enabled)return;
        wall+=weight*std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        cpu+=weight*static_cast<double>(std::clock()-cpu_start)/CLOCKS_PER_SEC;enabled=false;
    }
};
}

PatternScheduledComplexGeometry pattern_scheduled_complex_geometry(const Config& c,const PatternSearch& search) {
    PatternScheduledComplexGeometry out;
    const auto fail=[&](const char* reason){out.reason=reason;return out;};
    if(!scheduled_bursts(c) || packed_scheduled_symbols(c))
        return fail("complex split geometry requires split scheduled symbols");
    if(!c.data_key || !c.pattern_symbols || c.spreading_mode!=SpreadingMode::pattern)
        return fail("complex split geometry requires keyed pattern-mode symbols");
    if(!search.start_offset_seconds || !std::isfinite(*search.start_offset_seconds) ||
       !std::isfinite(search.start_uncertainty_seconds) || search.start_uncertainty_seconds<0)
        return fail("complex split geometry requires a finite arrival window");
    try {
        const auto total=symbol_sample_count(c),chip=pattern_chip_samples(c);
        if(search.hypotheses.size()>maximum_pattern_frequency_rate_hypotheses ||
           search.frequency_offsets_hz.size()>maximum_pattern_frequency_hypotheses || search.clock_errors_ppm.size()>65)
            return fail("complex split bank exceeds finite hypothesis limits");
        const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
        const auto flat=map.minimum_flat_samples();
        out.physical_aperture_samples=scheduled_physical_span_samples(c,total);
        out.frequency_step_hz=.25*c.sample_rate/static_cast<double>(out.physical_aperture_samples);
        out.clock_step_ppm=static_cast<double>(2e6L*std::min(chip/4.L,flat/8.L)/out.physical_aperture_samples);
        out.receiver_phase_step_samples=canonical_phase_step(c);
        const auto cap=std::max<std::uint64_t>(1,flat/8);
        if(out.receiver_phase_step_samples>cap) {
            const auto old=out.receiver_phase_step_samples;std::uint64_t step=1;
            for(std::uint64_t d=1;d<=old/d;++d)if(old%d==0) {
                if(d<=cap)step=std::max(step,d);
                if(old/d<=cap)step=std::max(step,old/d);
            }
            out.receiver_phase_step_samples=step;
        }
        const auto subdivisions=std::max(1.L,std::ceil(2*static_cast<long double>(chip)/flat));
        while(out.receiver_timing_subdivisions<subdivisions) {
            if(out.receiver_timing_subdivisions>std::numeric_limits<std::uint64_t>::max()/2)
                return fail("complex split timing subdivision overflow");
            out.receiver_timing_subdivisions*=2;
        }
        const auto scan=[&](bool frequency) {
            std::vector<double> values;values.reserve(search.hypotheses.empty()?
                (frequency?search.frequency_offsets_hz.size():search.clock_errors_ppm.size()):search.hypotheses.size());
            if(search.hypotheses.empty())values=frequency?search.frequency_offsets_hz:search.clock_errors_ppm;
            else for(const auto& item:search.hypotheses)values.push_back(frequency?item.frequency_offset_hz:item.clock_error_ppm);
            for(auto value:values)if(!std::isfinite(value))throw Error("complex split bank contains nonfinite coordinates");
            std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());
            return values;
        };
        const auto f=scan(true),r=scan(false);
        const auto flo=f.empty()?0:f.front(),fhi=f.empty()?0:f.back();
        const auto rlo=r.empty()?0:r.front(),rhi=r.empty()?0:r.back();
        const auto domain_radius=[](const std::vector<double>& values,double half) {
            if(values.empty())return half;
            double left=std::numeric_limits<double>::infinity(),right=left;
            for(auto value:values){left=std::min(left,std::abs(value+half));right=std::min(right,std::abs(value-half));}
            auto radius=std::max(left,right);
            for(std::size_t i=1;i<values.size();++i) {
                const auto middle=(values[i-1]+values[i])/2;
                if(middle>=-half && middle<=half)radius=std::max(radius,(values[i]-values[i-1])/2);
            }
            return radius;
        };
        out.admitted_frequency_half_width_hz=search.admitted_frequency_half_width_hz.value_or(std::max(-flo,fhi));
        out.admitted_clock_half_width_ppm=search.admitted_clock_half_width_ppm.value_or(std::max(-rlo,rhi));
        if(c.oscillator_search) {
            const auto original=oscillator_pattern_search(c);
            out.admitted_frequency_half_width_hz=std::max(out.admitted_frequency_half_width_hz,original.frequency.requested_half_width_hz);
            out.admitted_clock_half_width_ppm=std::max(out.admitted_clock_half_width_ppm,original.requested_clock_half_width_ppm);
        }
        const auto rf=out.admitted_frequency_half_width_hz,rr=out.admitted_clock_half_width_ppm;
        if(std::isfinite(rr) && rr>=0 && rr<=10000) {
            // The slowest admitted TX clock lengthens the physical aperture;
            // carrier phase and source-clock displacement use that full time.
            const auto aperture=std::ceil(static_cast<long double>(out.physical_aperture_samples)/(1-rr*1e-6L));
            if(aperture>=std::ldexp(1.L,64))return fail("complex split physical aperture overflow");
            out.physical_aperture_samples=static_cast<std::uint64_t>(aperture);
            out.frequency_step_hz=.25*c.sample_rate/static_cast<double>(out.physical_aperture_samples);
            out.clock_step_ppm=static_cast<double>(2e6L*std::min(chip/4.L,flat/8.L)/out.physical_aperture_samples);
        }
        if(!std::isfinite(rf) || rf<0 || !std::isfinite(rr) || rr<0 || rr>10000 ||
           !(out.frequency_step_hz>0) || !(out.clock_step_ppm>0))return fail("invalid complex split oscillator domain");
        out.clock_cell_radius_ppm=domain_radius(r,rr);
        out.frequency_grid_qualified=domain_radius(f,rf)<=out.frequency_step_hz/2*(1+1e-12);
        out.clock_grid_qualified=out.clock_cell_radius_ppm<=out.clock_step_ppm/2*(1+1e-12);
        if(!search.hypotheses.empty() && (c.oscillator_search || search.admitted_frequency_half_width_hz || search.admitted_clock_half_width_ppm)) {
            out.frequency_grid_qualified=out.frequency_grid_qualified && search.complex_frequency_coverage.value_or(false);
            out.clock_grid_qualified=out.clock_grid_qualified && search.complex_clock_coverage.value_or(false);
        }
        const auto clock_half=std::ceil(rr/out.clock_step_ppm);
        if(static_cast<long double>(clock_half)>static_cast<long double>((std::numeric_limits<std::size_t>::max()-1)/2))return fail("complex split clock count overflow");
        out.clock_hypotheses=rr?2*static_cast<std::size_t>(clock_half)+1:1;
        const auto origins=std::ceil(2*search.start_uncertainty_seconds*c.sample_rate/(chip/(2.L*(1+rr*1e-6L))/out.receiver_timing_subdivisions))+1;
        if(origins>std::numeric_limits<std::size_t>::max())return fail("complex split timing count overflow");
        out.timing_positions=static_cast<std::size_t>(origins);
        const bool stream_phases=search.search_stream_phases && (c.scramble || c.dsss);
        const auto phase_low=stream_phases?0:c.stream_phase_samples;
        const auto phase_high=stream_phases?(canonical_phase_extent(c)-1)/out.receiver_phase_step_samples*out.receiver_phase_step_samples:c.stream_phase_samples;
        std::vector<std::pair<std::uint64_t,std::uint64_t>> phase_ranges;
        if(c.stream_epoch>std::numeric_limits<std::uint64_t>::max()/c.sample_rate)return fail("complex split UTC sample coordinate overflow");
        const auto epoch=c.stream_epoch*c.sample_rate;
        auto cursor=SchedHopCoordinate{epoch,0}.add_samples(phase_low);
        const auto last=SchedHopCoordinate{epoch,0}.add_samples(phase_high);
        while(cursor.distance(last)<=0) {
            const auto group=map.locate(cursor);
            if(!group.active){cursor=map.source_to_physical(group.source,false);continue;}
            const auto lo=std::max(phase_low,group.physical_begin>epoch?group.physical_begin-epoch:0);
            const auto hi=std::min(phase_high,group.physical_end-1-epoch);
            const auto first=phase_low+(lo-phase_low+out.receiver_phase_step_samples-1)/out.receiver_phase_step_samples*out.receiver_phase_step_samples;
            if(first<=hi){phase_ranges.emplace_back(first,hi);out.phase_positions+=(hi-first)/out.receiver_phase_step_samples+1;}
            cursor={group.physical_end,0};
        }
        const auto lower=(static_cast<long double>(*search.start_offset_seconds)-search.start_uncertainty_seconds)*c.sample_rate;
        const auto upper=(static_cast<long double>(*search.start_offset_seconds)+search.start_uncertainty_seconds)*c.sample_rate;
        const bool paired=!search.hypotheses.empty();
        const auto pairs=paired?search.hypotheses.size():search.clock_errors_ppm.size();
        const auto frequencies=paired?1:std::max<std::size_t>(1,search.frequency_offsets_hz.empty()?5:search.frequency_offsets_hz.size());
        long double highest=1;
        for(auto ppm:r)highest=std::max(highest,1+ppm*1e-6L);
        const auto original_step=std::max(1.L,std::floor(chip/(2*highest)));
        long double retained=0,full=0,enumerated=0;
        out.timing_positions=0;
        for(std::size_t pair=0;pair<pairs;++pair) {
            const auto ppm=paired?search.hypotheses[pair].clock_error_ppm:search.clock_errors_ppm[pair];
            const auto ratio=1+ppm*1e-6L;
            const auto step=(paired?chip/(2*ratio):original_step)/out.receiver_timing_subdivisions;
            const auto origins=std::ceil((upper-lower)/step)+1;
            if(origins>std::numeric_limits<std::size_t>::max())return fail("complex split origin count overflow");
            out.timing_positions=std::max(out.timing_positions,static_cast<std::size_t>(origins));
            full+=origins*out.phase_positions*frequencies;
            if(!search.qualified_start_window){retained+=origins*out.phase_positions*frequencies;continue;}
            enumerated+=origins;
            if(enumerated>10000000)return fail("complex split qualified lane enumeration exceeds bounded preflight");
            const auto& q=*search.qualified_start_window;
            if(!std::isfinite(q.epoch_origin_samples) || !std::isfinite(q.phase_scale) || q.phase_scale<=0 ||
                !std::isfinite(q.half_width_samples) || q.half_width_samples<0)return fail("invalid qualified UTC start map");
            for(std::size_t i=0;i<static_cast<std::size_t>(origins);++i) {
                const auto at=std::min(upper,lower+i*step);
                const auto previous=i?std::min(upper,lower+(i-1)*step):lower;
                const auto next=i+1<origins?std::min(upper,lower+(i+1)*step):upper;
                const auto cell_lo=(previous+at)/2,cell_hi=(at+next)/2;
                const auto pad=64*std::numeric_limits<long double>::epsilon()*std::max({1.L,std::abs(cell_lo),std::abs(cell_hi),std::abs(q.epoch_origin_samples),q.half_width_samples});
                auto lo=(cell_lo-q.epoch_origin_samples-q.half_width_samples-pad)/q.phase_scale;
                auto hi=(cell_hi-q.epoch_origin_samples+q.half_width_samples+pad)/q.phase_scale;
                if(search.search_stream_phases)lo-=out.receiver_phase_step_samples-1;
                if(lo>phase_high || hi<phase_low)continue;
                lo=std::clamp(lo,static_cast<long double>(phase_low),static_cast<long double>(phase_high));
                hi=std::clamp(hi,static_cast<long double>(phase_low),static_cast<long double>(phase_high));
                const auto first=std::ceil((lo-phase_low)/out.receiver_phase_step_samples),final=std::floor((hi-phase_low)/out.receiver_phase_step_samples);
                if(first>final)continue;
                const auto pl=phase_low+static_cast<std::uint64_t>(first)*out.receiver_phase_step_samples;
                const auto ph=phase_low+static_cast<std::uint64_t>(final)*out.receiver_phase_step_samples;
                for(const auto& range:phase_ranges) {
                    const auto left=std::max(pl,range.first),right=std::min(ph,range.second);
                    if(left<=right)retained+=((right-left)/out.receiver_phase_step_samples+1)*frequencies;
                }
            }
        }
        if(retained>std::numeric_limits<std::size_t>::max() || full>std::numeric_limits<std::size_t>::max())return fail("complex split lane count overflow");
        out.retained_hypotheses=static_cast<std::size_t>(retained);out.full_hypotheses=static_cast<std::size_t>(full);
        const auto minimum=retained*(4*sizeof(Fit)+sizeof(SchedHopCoordinate)+sizeof(long double)+3*sizeof(std::uint64_t));
        if(minimum<=std::numeric_limits<std::size_t>::max())out.workspace_lower_bound_bytes=static_cast<std::size_t>(minimum);
        out.supported=true;
        if(!out.frequency_grid_qualified || !out.clock_grid_qualified)out.reason="explicit complex bank requires physical-aperture frequency/clock refinement for complete coverage";
        return out;
    } catch(const Error& error){out.reason=error.what();return out;}
}

struct PatternCorrelator::Impl {
    Config config;
    PatternSearch search;
    PatternCode code;
    // Mutable keystream/chip caches belong to one scoring worker. Completions
    // still run in hypothesis order on the caller after these workers finish.
    std::vector<PatternCode> worker_codes;
    struct ScheduledTemplateCache;
    std::size_t budget=0,bit_limit=0,accounted_bytes=0,drift_reserved=0,amplitude_scratch_reservation=0;
    std::uint64_t sample=0,trials=0;
    std::uint64_t phase_step=1;
    std::size_t alternate_groups=0;
    std::size_t block_samples=block_size;
    unsigned drift_sections=1;
    std::uint64_t differential_window=0;
    long double origin_lower=0;
    bool finished=false,shaped=false,pulse_segmented=false,scheduled_affine=false,guard_chains=false,outer_presence=false,timing_enabled=false,timing_dirty=false;
    PatternCorrelatorOptions options;
    CoreWork work;
    std::unique_ptr<PatternAmplitudeGeometry> amplitude_geometry;
    // The compact ordinary receiver has a strict small idle footprint. Sparse
    // calendar state belongs only to scheduled reception; its object
    // and every owned allocation are charged to that bank's workspace below.
    struct ScheduledCarry {
        std::array<Fit,2> fits{};
        std::uint64_t observed_start=0;
    };
    using ScheduledMoment=detail::PhysicalMomentCache::Moment;
    template<class Scalar> struct ScheduledCurveCacheT {
        struct Key {
            std::uint64_t epoch=0,ordinal=0,chip=0;
            // Validated sample rates are at most120MHz, leaving the upper
            // five bits free. Keep the original Key footprint on32/64-bit
            // targets; these local storage bits never enter SourceKey.
            std::uint32_t gate_phase=0;
            static constexpr std::uint32_t phase_mask=0x07ffffffU,shared_storage=0x80000000U;
            unsigned table_revision()const{return 1U+((gate_phase>>27)&15U);}
            std::uint32_t phase()const{return gate_phase&phase_mask;}
            void set_revision(unsigned revision) {
                require(revision==1||revision==2,"invalid local source curve revision");
                gate_phase=(gate_phase&(phase_mask|shared_storage))|((revision-1U)<<27);
            }
            bool operator==(const Key& other) const {
                return epoch==other.epoch&&ordinal==other.ordinal&&chip==other.chip&&
                    (gate_phase&~shared_storage)==(other.gate_phase&~shared_storage);
            }
        };
        struct OriginalKeyLayout {std::uint64_t epoch,ordinal,chip;std::uint32_t gate_phase;};
        static_assert(sizeof(Key)==sizeof(OriginalKeyLayout)&&alignof(Key)==alignof(OriginalKeyLayout));
        using Knots=std::array<std::array<std::complex<Scalar>,2>,257>;
        struct Row {
            Key key;
            union {Knots knots;detail::SharedSourceCurves<>::Lease shared;};
            std::atomic<unsigned> readers{0};
            std::uint64_t stamp=0;
            bool valid=false;
            Row():knots{}{}
            bool is_shared()const{return (key.gate_phase&Key::shared_storage)!=0;}
            void reset_shared()noexcept {if(is_shared())shared.reset();}
            void set_key(Key value)noexcept {
                const auto storage=key.gate_phase&Key::shared_storage;
                key=value;key.gate_phase=(key.gate_phase&~Key::shared_storage)|storage;
            }
            void ensure_knots()noexcept {
                if(!is_shared())return;
                std::destroy_at(&shared);std::construct_at(&knots);
                key.gate_phase&=~Key::shared_storage;
            }
            void adopt_shared(detail::SharedSourceCurves<>::Lease view)noexcept {
                if(is_shared()){shared=std::move(view);return;}
                OPENSSL_cleanse(knots.data(),sizeof(knots));
                std::destroy_at(&knots);std::construct_at(&shared,std::move(view));
                key.gate_phase|=Key::shared_storage;
            }
            ~Row(){if(is_shared())std::destroy_at(&shared);else std::destroy_at(&knots);
                OPENSSL_cleanse(static_cast<void*>(&knots),sizeof(Knots));}
        };
        struct OriginalRowLayout {Key key;Knots knots;std::atomic<unsigned> readers;std::uint64_t stamp;bool valid;};
        static_assert(sizeof(Row)==sizeof(OriginalRowLayout)&&alignof(Row)==alignof(OriginalRowLayout));
        struct Lease {
            Row* row=nullptr;
            detail::SharedSourceCurves<>::Lease shared;
            Lease()=default;
            explicit Lease(Row* p):row(p){}
            explicit Lease(detail::SharedSourceCurves<>::Lease view):shared(std::move(view)){}
            Lease(const Lease&)=delete;
            Lease& operator=(const Lease&)=delete;
            Lease(Lease&& other) noexcept:row(std::exchange(other.row,nullptr)),shared(std::move(other.shared)){}
            Lease& operator=(Lease&& other) noexcept {
                if(this==&other)return *this;
                if(row)row->readers.fetch_sub(1,std::memory_order_release);
                row=std::exchange(other.row,nullptr);shared=std::move(other.shared);return *this;
            }
            ~Lease(){if(row)row->readers.fetch_sub(1,std::memory_order_release);}
        };
        std::array<Row,8> rows;
        std::mutex mutex;
        std::uint64_t stamp=0,hits=0,misses=0,evictions=0,exhaustions=0;
        struct OriginalCacheLayout {std::array<OriginalRowLayout,8> rows;std::mutex mutex;std::uint64_t stamp,hits,misses,evictions,exhaustions;};
        // Synchronous push owns all scoring workers until they join. Retain
        // pins through local hits within that push, never across rolling banks.
        void clear_borrowed_rows() noexcept {
            std::lock_guard lock(mutex);
            for(auto& row:rows)if(row.is_shared()) {
                if(row.readers.load(std::memory_order_acquire))std::terminate();
                row.valid=false;row.reset_shared();
            }
        }
        template<class Pattern>
        Lease get(Key key,Pattern& pattern,std::uint64_t first_chip,std::uint64_t chip,std::stop_token stop,
                detail::SharedSourceCurves<>* shared=nullptr,const detail::SourceFamily* family=nullptr) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::template_cache,true);
            if(chip<1024)key.set_revision(detail::fine_source_curve_revision());
            const bool concurrent=chip<1024;
            const bool borrow_row=concurrent && key.table_revision()==2 &&
                detail::fine_source_curve_cached_borrow_requested() && shared && family && shared->capacity();
            const auto populate=[&](auto& knots) {
                const auto step=key.table_revision()==2?4U:1U;
                if(step!=1)for(auto& pair:knots)pair={};
                for(std::size_t k=0;k<knots.size();k+=step) {
                    if((k&31U)==0)cancelled(stop);
                    const auto within=(static_cast<long double>(key.chip)+static_cast<long double>(k)/256)*chip;
                    const auto values=pattern.shaped_values(first_chip,static_cast<double>(within));
                    for(unsigned bit=0;bit<2;++bit)knots[k][bit]={static_cast<Scalar>(values[bit].real()),static_cast<Scalar>(values[bit].imag())};
                }
            };
            if constexpr(std::is_same_v<Scalar,double>) {
                if(concurrent && key.table_revision()==2 && detail::fine_source_curve_borrow_requested() &&
                   shared && family && shared->capacity()) {
                    // Pin the complete immutable L2 row for this chip/job.
                    // No private evidence is shared, and the original path
                    // handles an in-progress or exhausted optional row.
                    auto identity=detail::source_key(*family,
                        SymbolStreamAddress{key.epoch,key.ordinal,key.phase()},key.chip);
                    identity.table_revision=key.table_revision();
                    auto view=shared->get(identity,populate,stop);
                    if(view){detail::fine_curve_borrow_statistics().direct.fetch_add(1,std::memory_order_relaxed);return Lease(std::move(view));}
                    detail::fine_curve_borrow_statistics().l2_fallbacks.fetch_add(1,std::memory_order_relaxed);return {};
                }
            }
            constexpr auto writing=std::numeric_limits<unsigned>::max();
            std::unique_lock lock(mutex);
            for(auto& row:rows)if(row.valid && row.key==key) {
                if(concurrent && row.readers.load(std::memory_order_acquire)>=writing-1) {
                    ++misses;++exhaustions;return {};
                }
                ++hits;row.stamp=++stamp;row.readers.fetch_add(1,std::memory_order_relaxed);return Lease(&row);
            }
            // Fine-chip construction must not serialize all scoring workers.
            // A writer is neither valid nor evictable. A duplicate uses the
            // original direct evaluator instead of building the row twice.
            if(concurrent)for(auto& row:rows)if(row.readers.load(std::memory_order_acquire)==writing && row.key==key) {
                ++misses;++exhaustions;return {};
            }
            ++misses;Row* selected=nullptr;
            for(auto& row:rows)if(!row.readers.load(std::memory_order_acquire) &&
                (!selected || !row.valid || (selected->valid && row.stamp<selected->stamp)))selected=&row;
            if(!selected){++exhaustions;return {};}
            if(selected->valid)++evictions;
            selected->valid=false;selected->reset_shared();
            if(!borrow_row)selected->ensure_knots();
            if(concurrent) {
                selected->set_key(key);selected->readers.store(writing,std::memory_order_release);
                lock.unlock();
            }
            bool copied=false,unavailable=false;
            detail::SharedSourceCurves<>::Lease pending_shared;
            try {
                if constexpr(std::is_same_v<Scalar,double>) {
                    if(shared && family) {
                    // Both columns belong to this exact canonical symbol. The
                    // existing local key already resolved target_index/carry;
                    // no extra calendar/crypto construction occurs here.
                    auto identity=detail::source_key(*family,
                        SymbolStreamAddress{key.epoch,key.ordinal,key.phase()},key.chip);
                    identity.table_revision=key.table_revision();
                    auto lease=shared->get(identity,populate,stop);
                        if(lease) {
                            if(borrow_row)pending_shared=std::move(lease);
                            else selected->knots=lease.knots();
                            copied=true;
                        } else if(borrow_row) {
                            unavailable=true;detail::fine_curve_borrow_statistics().l2_fallbacks.fetch_add(1,std::memory_order_relaxed);
                        }
                    }
                }
                if(!copied&&!unavailable)populate(selected->knots);
                if(concurrent)cancelled(stop);
            } catch(...) {
                if(concurrent) {
                    lock.lock();selected->reset_shared();
                    if(!selected->is_shared())OPENSSL_cleanse(selected->knots.data(),sizeof(selected->knots));
                    selected->readers.store(0,std::memory_order_release);
                }
                throw;
            }
            if(concurrent)lock.lock();
            if(unavailable){selected->readers.store(0,std::memory_order_release);return {};}
            selected->set_key(key);
            if(pending_shared) {
                selected->adopt_shared(std::move(pending_shared));
                detail::fine_curve_borrow_statistics().cached.fetch_add(1,std::memory_order_relaxed);
            }
            selected->stamp=++stamp;selected->valid=true;
            selected->readers.store(1,std::memory_order_relaxed);return Lease(selected);
        }
        static bool evaluate(const Lease& view,long double within,std::uint64_t chip,std::array<Complex,2>& result) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::template_cache,true);
            if(view.shared)return detail::SharedSourceCurves<>::evaluate(view.shared,within,result);
            if(!view.row)return false;
            if(view.row->is_shared())return detail::SharedSourceCurves<>::evaluate(view.row->shared,within,result);
            return detail::evaluate_source_curve(view.row->knots,view.row->key.chip,within,chip,result,view.row->key.table_revision());
        }
    };
    using ScheduledCurveCache=ScheduledCurveCacheT<double>;
    using ScheduledFloatCurveCache=ScheduledCurveCacheT<float>;
    static_assert(sizeof(ScheduledCurveCache)==sizeof(ScheduledCurveCache::OriginalCacheLayout)&&
        alignof(ScheduledCurveCache)==alignof(ScheduledCurveCache::OriginalCacheLayout));
    static_assert(sizeof(ScheduledFloatCurveCache)==sizeof(ScheduledFloatCurveCache::OriginalCacheLayout)&&
        alignof(ScheduledFloatCurveCache)==alignof(ScheduledFloatCurveCache::OriginalCacheLayout));
    // Immutable logical-lane permutation, with no PCM/template storage.
    // Completion and publication still visit the original hypothesis order.
    struct ScheduledFamilyPlan {
        struct AddressProof {std::uint64_t index=0;bool equal=false,valid=false;};
        using AddressCache=std::array<AddressProof,2>;
        std::vector<AddressCache> phase_addresses;
        std::vector<std::size_t> members,ends;
        std::size_t exact_families=0;
        std::size_t working_bytes() const {
            return sizeof(*this)+(members.capacity()+ends.capacity())*sizeof(std::size_t)+
                phase_addresses.capacity()*sizeof(AddressCache);
        }
    };
    struct ScheduledCommonWork {
        std::uint64_t families=0,lanes=0,pieces=0,shapes=0,raw_samples=0,raw_lane_samples=0,affine_preparations=0,affine_lanes=0,gram_fallbacks=0;
        std::uint64_t raw_fit_pairs=0,raw_reused_lane_samples=0,raw_packed_values=0,raw_workspace_samples=0;
        std::uint64_t simd_attempts=0,simd_samples=0,simd_lane_samples=0,simd_packed_values=0,simd_guard_updates=0,simd_outer_updates=0;
        std::uint64_t simd_short_samples=0,simd_workspace_samples=0,simd_isa_samples=0,simd_scalar_lane_samples=0;
        std::uint64_t phase_attempts=0,phase_rejects=0,phase_families=0,phase_lanes=0;
        std::uint64_t mapped_pairs=0,mapped_failed_pairs=0,mapped_lanes=0,affine_reused_lanes=0;
        std::uint64_t phase_address_hits=0,phase_address_misses=0,phase_address_queries=0;
        std::uint64_t frequency_attempts=0,frequency_prepared_samples=0,frequency_pieces=0,frequency_lanes=0;
        std::uint64_t frequency_geometry_fallbacks=0,frequency_numerical_fallbacks=0,frequency_outer_terms=0;
        double frequency_maximum_relative_error=0;
    };
    struct ScheduledFloatCounts {
        std::uint64_t attempts=0,pieces=0,condition_fallbacks=0,error_fallbacks=0;
        std::uint64_t accepted_pairs=0,fp64_pairs=0,gram_fallbacks=0;
    };
    struct ScheduledFloatGramCounts {
        std::uint64_t attempts=0,accepted=0,range=0,rank=0,error=0,helper=0,signal_attempts=0;
        double maximum_relative_error=0;
    };
    struct LazyState;
    struct ScheduledState {
        ScheduledExtraWork extra_work;
        // Non-owning per-push views exist only in scheduled receivers.
        std::vector<detail::PhysicalMomentCache::View> physical_views;
        bool common_scratch=false,common_attempted=false,common_raw_scratch=false;
        bool frequency_piece_scratch=false;
        std::size_t common_raw_scratch_bytes=0;
        detail::family_simd::Backend common_simd_backend=detail::family_simd::Backend::scalar;
        std::vector<ScheduledCommonWork> common_work;
        std::unique_ptr<ScheduledFamilyPlan> families;
        bool family_plan_attempted=false;
        PatternScheduledComplexGeometry complex_geometry;
        std::vector<std::unique_ptr<SchedHopSourceMap>> worker_maps;
        std::vector<std::unique_ptr<ScheduledTemplateCache>> worker_template_caches;
        std::vector<std::unique_ptr<detail::ScheduledTemporalTemplates>> worker_temporal_templates;
        detail::ScheduledTemporalTemplates::Statistics temporal_retired;
        std::size_t temporal_peak_bytes=0;
        // Key-independent physical carrier moments for one bounded input
        // block. No source-time concatenation and no symbol-length buffering.
        std::vector<std::vector<ScheduledMoment>> moments;
        bool moments_eligible=false,outer_moment_scratch=false;
        // Explicit diagnostic: original FP64 Gram/energy/moments remain.
        // Immutable packed signal rows are shared by scoring workers.
        std::vector<detail::scheduled_float_signal::Moments> float_signal;
        std::vector<ScheduledFloatCounts> float_counts;
        std::vector<ScheduledFloatGramCounts> float_grams;
        std::unique_ptr<ScheduledCurveCache> curves;
        std::unique_ptr<ScheduledFloatCurveCache> float_curves;
        detail::SharedSourceCurves<>* shared_source_curves=nullptr;
        const detail::SourceFamily* shared_source_family=nullptr;
        std::uint64_t curve_hits=0,curve_misses=0,curve_evictions=0,curve_exhaustions=0;
        std::unique_ptr<SchedHopSourceMap> map;
        std::unique_ptr<ScheduledSymbolCalendar> calendar;
        FhssPowerShape fhss_shape;
        std::vector<ScheduledCarry> scheduled_carry;
        std::vector<std::array<DriftFit,2>> scheduled_carry_drift;
        std::vector<std::array<unsigned char,2>> scheduled_quarter_ranks,scheduled_carry_quarter_ranks;
        bool scheduled_cache_possible=false;
        std::vector<std::array<DifferentialFit,2>> scheduled_carry_differential;
        std::vector<std::array<detail::CorrelationChipEvidence,2>> scheduled_carry_chips;
        std::vector<detail::CorrelationOuterEvidence> scheduled_carry_outer;
        std::vector<SchedHopCoordinate> anchors;
        std::vector<long double> observed_begin;
        std::vector<std::uint64_t> last_accepted;
        std::vector<std::uint64_t> source_starts;
        std::vector<std::uint64_t> template_uncached;
        // Fixed physical timing map; refresh only when the permanent source
        // symbol advances. Temporary adjacent-symbol carry must not reuse it.
        std::vector<long double> symbol_ends;
        std::vector<std::uint64_t> end_indices,first_full_indices;
        long double earliest_end=0;
        long double drift_begin=0;
        std::uint64_t null_end=0;
        std::uint64_t epoch_sample=0;
        long double timing_radius=0,clock_radius=0;
        // Destroy replay owners before the maps/caches they borrow.
        std::unique_ptr<LazyState> lazy_acquisition;
    };
    std::unique_ptr<ScheduledState> scheduled;
    detail::PhysicalMomentCache::View physical_view(std::size_t frequency) const {
        return scheduled && frequency<scheduled->physical_views.size()?scheduled->physical_views[frequency]:
            detail::PhysicalMomentCache::View{};
    }
    detail::PhysicalMomentCache::View physical_view(const Bank& bank) const {
        return physical_view(static_cast<std::size_t>(&bank-banks.data()));
    }
    void clear_physical_views() {
        if(scheduled)std::fill(scheduled->physical_views.begin(),scheduled->physical_views.end(),
            detail::PhysicalMomentCache::View{});
    }
    // Exact current-block rows reuse shaped/limited/gated candidates across
    // fine frequencies. Identity excludes frequency, includes every coordinate
    // that changes coefficients. Each worker owns its cache; no shared mutable
    // private templates or shifted-PCM matrices are introduced.
    struct ScheduledTemplateCache {
        struct Row {
            long double origin=0,rate=0;
            std::uint64_t phase=0,index=0,stamp=0;
            std::array<std::array<Complex,2>,scheduled_block_size> value{};
            std::array<std::uint64_t,scheduled_block_size/64> valid{};
            bool occupied=false;
        };
        std::array<Row,8> rows;
        std::uint64_t block=std::numeric_limits<std::uint64_t>::max(),stamp=0,hits=0,constructions=0;
        Row& row(long double origin,long double rate,std::uint64_t phase,std::uint64_t index,std::uint64_t input_block) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::template_cache,true);
            if(block!=input_block || stamp==std::numeric_limits<std::uint64_t>::max()) {
                for(auto& item:rows){item.occupied=false;item.valid={};}
                block=input_block;stamp=0;
            }
            for(auto& item:rows)if(item.occupied && item.origin==origin && item.rate==rate && item.phase==phase && item.index==index) {
                item.stamp=++stamp;return item;
            }
            auto* selected=&rows.front();
            for(auto& item:rows) {
                if(!item.occupied){selected=&item;break;}
                if(item.stamp<selected->stamp)selected=&item;
            }
            selected->origin=origin;selected->rate=rate;selected->phase=phase;selected->index=index;
            selected->stamp=++stamp;selected->valid={};selected->occupied=true;return *selected;
        }
    };
    static auto& worker_template_view() {
        static thread_local std::pair<const Impl*,ScheduledTemplateCache*> view{};
        return view;
    }
    ScheduledTemplateCache* scheduled_template_cache() const {
        const auto& view=worker_template_view();
        if(view.first==this)return view.second;
        return scheduled && worker_codes.empty() && !scheduled->worker_template_caches.empty()?scheduled->worker_template_caches.front().get():nullptr;
    }
    static auto& worker_temporal_view() {
        static thread_local std::pair<const Impl*,detail::ScheduledTemporalTemplates*> view{};
        return view;
    }
    detail::ScheduledTemporalTemplates* scheduled_temporal_templates() const {
        const auto& view=worker_temporal_view();
        if(view.first==this)return view.second;
        return scheduled && worker_codes.empty() && !scheduled->worker_temporal_templates.empty()?
            scheduled->worker_temporal_templates.front().get():nullptr;
    }
    static auto& worker_map_view() {
        static thread_local std::pair<const Impl*,SchedHopSourceMap*> view{};
        return view;
    }
    SchedHopSourceMap* scheduled_map() const {
        const auto& view=worker_map_view();
        return view.first==this?view.second:scheduled?scheduled->map.get():nullptr;
    }
    ScheduledSymbolCalendar* scheduled_calendar() const {return scheduled?scheduled->calendar.get():nullptr;}
    bool amplitude_publication_dirty=true;
    std::vector<PatternAmplitudeChannel> amplitude_channels;
    std::vector<float> amplitude_pcm;
    std::vector<detail::EnvelopeChannelFit> amplitude_fits;
    std::vector<detail::EnvelopeTemplateFit> amplitude_templates;
    struct AmplitudeScratch {
        std::unique_ptr<detail::search_fft::DoublePlan> plan;
        std::unique_ptr<detail::search_fft::FloatPlan> float_plan;
        std::vector<Complex> pcm;
        std::vector<std::complex<float>> float_pcm;
        std::vector<long double> spectrum;
        std::size_t working_bytes()const {
            return (plan?sizeof(*plan)+plan->working_bytes():0)+
                (float_plan?sizeof(*float_plan)+float_plan->working_bytes():0)+
                pcm.capacity()*sizeof(Complex)+float_pcm.capacity()*sizeof(std::complex<float>)+
                spectrum.capacity()*sizeof(long double);
        }
    } amplitude_scratch;
    std::size_t amplitude_scratch_limit=0;
    std::size_t amplitude_partial=0;
    std::uint64_t amplitude_processed=0;
    struct Hypothesis {
        long double origin=0,rate=1;
        std::uint64_t index=0,observed_start=0,phase_lower=0,phase_upper=0;
        std::array<Fit,2> fits{};
        PatternBurst burst;
        // The burst stores committed score/support; only tentative totals need
        // separate accumulators. Its committed endpoint is also the last find.
        double sum_score=0,pending_score=0,sum_support=0;
        std::size_t committed=0,gap_slots=0;
        std::uint64_t committed_end=0;
        // Bank indices are bounded by <=8194 coupled pairs (or 65x65
        // legacy pairs); compact storage leaves more workspace for evidence.
        std::uint32_t frequency=0,rate_index=0,tone_bank_base=0;
        bool admitted=false,pending_gaps=false;
        std::uint8_t affine_groups=0; // Keep ordinary compact-workspace layout.
    };
    struct PhaseGroup {std::uint64_t lower=0,upper=0;};
    struct PhaseGroups {
        std::array<PhaseGroup,3> small{};
        std::uint64_t first=0,step=0;
        PhaseGroup operator[](std::size_t index) const {
            if(step){const auto phase=first+index*step;return {phase,phase};}
            return small.at(index);
        }
    };
    struct AmplitudePhaseEntry {
        PhaseGroups groups;
        std::uint64_t index=std::numeric_limits<std::uint64_t>::max(),lower=0,upper=0;
        std::size_t count=0;
    };
    std::vector<AmplitudePhaseEntry> amplitude_phases;
    std::vector<Hypothesis> hypotheses;
    // Split acquisition stores the immutable logical lattice in a catalogue,
    // not a Fit/guard/descriptor for every lane. Slots never relocate: any lane
    // which can publish or be subscribed is pinned BEFORE complete() executes.
    // Numerical arrays remain slot-indexed; calendar neighbours are logical IDs.
    struct LazyState {
        using Catalog=detail::ScheduledAcquisitionCatalog;
        // Optional replay state for a bounded original-order event window.
        // These numerical owners never enter slots/timing until serial commit.
        struct DetachedReplay {
            static constexpr std::size_t maximum_events=16,maximum_lookahead=128;
            static constexpr std::size_t caller_bytes=8192;
            struct Event {
                std::size_t id=0,slot=std::numeric_limits<std::size_t>::max();
                std::uint64_t index=0,first=0,end=0;
            };
            std::array<Event,maximum_events> events{};
            std::unique_ptr<detail::ScheduledPrivateBatchScratch> pulse;
            std::uint64_t block_first=0,block_end=0,through_index=0;
            std::size_t through_id=0,count=0;
            bool ready=false,numeric_active=false;
            std::size_t working_bytes()const {
                return sizeof(*this)+caller_bytes+(pulse?sizeof(*pulse)+
                    detail::ScheduledPrivateBatchScratch::callee_bytes:0);
            }
        };
        std::unique_ptr<DetachedReplay> detached_replay;
        std::uint64_t detached_batches=0,detached_lanes=0,detached_prefixes=0,detached_blocks=0;
        std::uint64_t detached_private_lanes=0,detached_private_rows=0;
        struct Block {std::uint64_t first=0;std::vector<float> pcm;std::vector<double> energy;double maximum=0;};
        struct DueRange {std::size_t family=0,first=0,end=0,id=0;std::uint64_t index=0;
            std::size_t screen_first=0,numeric_origin_stride=1;std::uint64_t mask_difference=0;double maximum=0;
            certdiag::Cohort certificate_cohort=certdiag::Cohort::unavailable;
            double proxy_residual_square=detail::scheduled_acquisition_certificate::infinity;
            double proxy_residual_l1=detail::scheduled_acquisition_certificate::infinity;
            double proxy_residual_gram=detail::scheduled_acquisition_certificate::infinity;
            std::vector<detail::scheduled_acquisition_tile::Value> screen;
            std::vector<detail::scheduled_acquisition_certificate::ReferenceErrors> reference;
            std::vector<std::array<detail::scheduled_cold_colored::Summary,2>> colored;
            double colored_carrier_radius=0,colored_carrier_magnitude=0;
            std::uint64_t colored_sample_pairs=0;
            std::size_t colored_selected=0;
            unsigned colored_mode=0,colored_gate=0;double colored_lattice_margin=0;
            double colored_residual_square=0,colored_residual_l1=0,colored_residual_gram=0;
        };
        std::vector<DueRange> due_ranges,batch_ranges,prepared_ranges;
        std::optional<detail::scheduled_processing::Block> committing_block;
        long double committing_next_due=std::numeric_limits<long double>::infinity();
        std::uint64_t screen_capture_end=0;
        std::exception_ptr commit_fault;
        detail::scheduled_resume::PushReceipt pending_push;
        struct TemplateKey {
            const void* plan=nullptr;
            long double origin=0,rate=0;
            std::uint64_t phase=0,index=0,offset=0,shift=0;
            std::size_t count=0;
            friend bool operator==(const TemplateKey&,const TemplateKey&)=default;
        };
        template<class T> struct TemplatePiece {
            TemplateKey key;
            std::vector<std::array<Complex,2>> rows;
            std::vector<std::uint8_t> mask,active;
            std::vector<long double> source;
            std::vector<double> gain;
            std::vector<std::uint64_t> frame;
            std::unique_ptr<detail::scheduled_acquisition_tile::PreparedFragment<T>> prepared;
            ~TemplatePiece(){if(!rows.empty())OPENSSL_cleanse(rows.data(),rows.size()*sizeof(rows[0]));}
            std::size_t working_bytes()const{return sizeof(*this)+rows.capacity()*sizeof(rows[0])+
                mask.capacity()+active.capacity()+source.capacity()*sizeof(long double)+gain.capacity()*sizeof(double)+
                frame.capacity()*sizeof(std::uint64_t)+(prepared?prepared->working_bytes():0);}
        };
        // Numeric preparation owns every mutable lookup and cache. Serial
        // guard replay continues using Impl::code and LazyState::cursor.
        struct Execution {
            std::unique_ptr<PatternCode> owned_code;
            std::unique_ptr<Catalog::Cursor> owned_cursor;
            std::unique_ptr<SchedHopSourceMap> owned_map;
            PatternCode& code;
            Catalog::Cursor& cursor;
            SchedHopSourceMap& map;
            std::vector<std::unique_ptr<TemplatePiece<float>>> fp32_templates;
            std::vector<std::unique_ptr<TemplatePiece<double>>> fp64_templates;
            std::vector<std::unique_ptr<detail::scheduled_acquisition_tile::Plan<float>>> fp32_plans;
            std::vector<std::unique_ptr<detail::scheduled_acquisition_tile::Plan<double>>> fp64_plans;
            std::uint64_t spectral_template_hits=0,spectral_template_misses=0,plan_hits=0,plan_misses=0;
            std::uint64_t screen_lanes=0,input_transforms=0,template_transforms=0,inverse_transforms=0;
            std::uint64_t eligible_ranges=0,fallback_ranges=0,proxy_ranges=0,fp32_ranges=0,fp64_ranges=0,origin_class_covers=0;
            std::size_t peak_prepared_bytes=0,available_bytes=0,scratch_reservation=0;
            detail::scheduled_lazy_family_runtime::Work frequency_family_work;
            bool screen_active=false;
            // Only plan/template capacity is immutable between joined numeric
            // preparations. PatternCode, cursor, map and scratch stay live.
            std::optional<std::size_t> joined_numeric_bytes;
            bool cached_workspace_accounting=[] {
                const auto* mode=std::getenv("DATAPUMP_SCHED_WORKSPACE_ACCOUNTING");
                return !mode || std::string_view(mode)!="0";
            }();
            bool verify_workspace_accounting=[] {
                const auto* mode=std::getenv("DATAPUMP_SCHED_WORKSPACE_VERIFY");
                return mode && std::string_view(mode)=="1";
            }();
            certdiag::Preparation certificate_preparation,certificate_pending;
            Execution(PatternCode& serial_code,Catalog::Cursor& serial_cursor,SchedHopSourceMap& serial_map)
                :code(serial_code),cursor(serial_cursor),map(serial_map) {}
            Execution(const Config& config,Catalog& catalog,const SchedHopSourceMap& map)
                :owned_code(std::make_unique<PatternCode>(config,config.stream_epoch)),
                 owned_cursor(std::make_unique<Catalog::Cursor>(catalog.make_cursor())),
                 owned_map(std::make_unique<SchedHopSourceMap>(map)),code(*owned_code),cursor(*owned_cursor),map(*owned_map) {}
            std::size_t numeric_bytes_uncached()const {
                auto n=fp32_plans.capacity()*sizeof(decltype(fp32_plans)::value_type)+
                    fp64_plans.capacity()*sizeof(decltype(fp64_plans)::value_type)+
                    fp32_templates.capacity()*sizeof(decltype(fp32_templates)::value_type)+
                    fp64_templates.capacity()*sizeof(decltype(fp64_templates)::value_type);
                for(const auto& p:fp32_plans)n+=p->working_bytes();
                for(const auto& p:fp64_plans)n+=p->working_bytes();
                for(const auto& t:fp32_templates)n+=t->working_bytes();
                for(const auto& t:fp64_templates)n+=t->working_bytes();
                return n;
            }
            std::size_t numeric_bytes()const {
                if(!joined_numeric_bytes || !cached_workspace_accounting)return numeric_bytes_uncached();
                if(verify_workspace_accounting)require(*joined_numeric_bytes==numeric_bytes_uncached(),
                    "joined implicit numerical workspace accounting is stale");
                return *joined_numeric_bytes;
            }
            void thaw_numeric_bytes()noexcept {joined_numeric_bytes.reset();}
            void freeze_numeric_bytes() {
                require(!screen_active,"cannot freeze implicit accounting while a screen borrows templates");
                if(cached_workspace_accounting)joined_numeric_bytes=numeric_bytes_uncached();
            }
            std::size_t working_bytes()const{return sizeof(*this)+numeric_bytes()+
                (owned_code?owned_code->working_bytes():0)+(owned_cursor?Catalog::cursor_reservation_bytes():0)+
                (owned_map?owned_map->working_bytes():0);}
            void drop_templates() {
                require(!screen_active,"cannot trim implicit templates while a worker borrows them");
                thaw_numeric_bytes();
                decltype(fp32_templates){}.swap(fp32_templates);decltype(fp64_templates){}.swap(fp64_templates);
            }
            void drop_plans() {
                require(!screen_active,"cannot trim implicit plans while a worker borrows them");
                drop_templates(); // PreparedFragment borrows its immutable Plan.
                decltype(fp32_plans){}.swap(fp32_plans);decltype(fp64_plans){}.swap(fp64_plans);
            }
            void reset_costs() {
                frequency_family_work={};
                spectral_template_hits=spectral_template_misses=plan_hits=plan_misses=0;
                screen_lanes=input_transforms=template_transforms=inverse_transforms=0;
                eligible_ranges=fallback_ranges=proxy_ranges=fp32_ranges=fp64_ranges=origin_class_covers=0;peak_prepared_bytes=0;
            }
        };
        std::unique_ptr<Execution> serial_execution;
        std::vector<std::unique_ptr<Execution>> executions;
        bool screens_running=false;
        certdiag::State certificate_profile;
        std::optional<certdiag::Probe> certificate_pending_event;
        std::size_t transient_reservation=0;
        detail::scheduled_lazy_family_runtime::Work frequency_family_work;
        std::uint64_t parallel_dispatches=0,parallel_groups=0,parallel_ranges=0,serial_workspace_fallbacks=0;
        std::size_t parallel_workers=0,parallel_peak_workers=0,parallel_reserved_bytes=0;
        std::size_t actual_worker_slots=0,peak_actual_worker_slots=0,peak_parallel_jobs=0;
        std::uint64_t spectral_template_hits=0,spectral_template_misses=0,plan_hits=0,plan_misses=0;

        // Bound to this LazyState's immutable append-only PCM generation. A
        // shared capture subscription prevents reset throughout this lifetime.
        using OriginalProjections=detail::scheduled_original_projection::Cache<Projection,scheduled_block_size>;
        std::unique_ptr<OriginalProjections> original_projections;
        std::size_t projection_cache_limit=0;
        std::uint64_t projection_pcm_generation=1,projection_hits=0,projection_misses=0,projection_skips=0;
        std::size_t projection_peak_bytes=0;
        struct ProjectionUse {
            std::uint64_t hits=0,hit_samples=0,original_samples=0,stores=0;
            std::uint64_t cross_consumer_hits=0,cross_consumer_hit_samples=0,other_source_hits=0;
        };
        ProjectionUse projection_acquisition,projection_diagnostic;
        detail::scheduled_original_projection::Statistics projection_retired_statistics;
        enum class ProjectionDrop:std::size_t {cap_change,allocation_pressure,workspace_shrink,explicit_trim,count};
        std::array<std::uint64_t,static_cast<std::size_t>(ProjectionDrop::count)> projection_drops{},projection_drop_rows{},projection_rebuilds{};
        std::optional<ProjectionDrop> projection_pending_rebuild;
        std::uint64_t projection_objects=0,projection_shared_consumers=0,projection_shared_samples=0;
        std::vector<Projection> prefix;
        std::size_t prefix_frequency=std::numeric_limits<std::size_t>::max();
        std::unique_ptr<Catalog> catalog;
        std::unique_ptr<Catalog::Cursor> cursor;
        std::deque<Block> capture;
        using SharedCapture=detail::scheduled_capture::SharedScheduledCapture;
        using CaptureFences=detail::scheduled_capture::OriginalCaptureFences;
        std::shared_ptr<SharedCapture> shared_capture;
        SharedCapture::Retention capture_retention;
        std::unique_ptr<CaptureFences> capture_fences;
        std::unique_ptr<detail::scheduled_processing::ProcessingJournal> processing;
        std::vector<double> carrier_restart_radius,carrier_restart_magnitude;
        std::vector<std::uint64_t> carrier_restart_revision;

        std::uint64_t history_first=0;
        std::uint64_t summary_revision=std::numeric_limits<std::uint64_t>::max();
        double summary_maximum=0,summary_union_energy=0;

        std::map<std::size_t,std::size_t> slots;
        std::vector<std::size_t> logical;
        std::vector<std::uint64_t> replayed;
        std::vector<unsigned char> occupied;
        std::size_t capacity=0,history_limit=0,history_samples=0,history_reservation=0,batch_samples=512,capture_allocated_bytes=0;
        std::uint64_t processed=0,block_end=0,capture_end=0;
        std::chrono::steady_clock::time_point pending_since{};
        long double next_due=0,initial_complete_due=0;
        bool replaying=false;
        std::uint64_t null_replay_fences=0,null_replay_samples=0;
        std::uint64_t replay_samples=0,cold_completions=0,promotions=0,screen_rejections=0,history_generation=0;
        std::unique_ptr<detail::scheduled_diagnostics::DeferredEvents> diagnostics;
        std::uint64_t diagnostic_first=std::numeric_limits<std::uint64_t>::max();
        std::uint64_t deferred_diagnostics=0,diagnostic_materializations=0,diagnostic_resolutions=0;
        std::uint64_t screen_lanes=0,refinements=0,input_transforms=0,template_transforms=0,inverse_transforms=0;
        std::uint64_t colored_rejections=0;
        std::array<std::unique_ptr<detail::scheduled_cold_colored::SourceRows>,18> colored_sources;
        std::array<std::uint64_t,18> colored_source_touched{};
        std::uint64_t colored_source_revision=0,colored_source_builds=0,colored_source_hits=0;
        std::uint64_t colored_source_translation_hits=0,colored_source_evictions=0,colored_fast_error_queries=0;
        std::size_t colored_source_peak_bytes=0;
        std::uint64_t colored_demand_queries=0,colored_demand_pairs=0,colored_demand_fallbacks=0;
        std::array<std::uint64_t,12> colored_demand_failure{};
        std::uint64_t colored_demand_excluded_rows=0;
        bool colored_source_active=false;
        std::uint64_t eligible_ranges=0,fallback_ranges=0,invalid_certificates=0,proxy_ranges=0,fp32_ranges=0,fp64_ranges=0,origin_class_covers=0;
        std::size_t peak_prepared_bytes=0;
        std::size_t working_bytes()const {
            auto n=sizeof(*this)+sizeof(detail::scheduled_resume::Binding)+(catalog?catalog->working_bytes():0)+
                (cursor?Catalog::cursor_reservation_bytes():0)+
                logical.capacity()*sizeof(std::size_t)+replayed.capacity()*sizeof(std::uint64_t)+occupied.capacity();
            if(detached_replay)n+=detached_replay->working_bytes();
            // A conservative implementation-specific node allowance covers
            // the map/deque bookkeeping and allocation headers, in addition
            // to every retained original sample and vector object.
            n+=slots.size()*(sizeof(std::pair<const std::size_t,std::size_t>)+64);
            n+=(due_ranges.capacity()+batch_ranges.capacity()+prepared_ranges.capacity())*sizeof(DueRange)+prefix.capacity()*sizeof(Projection);
            for(const auto& r:due_ranges)n+=r.screen.capacity()*sizeof(detail::scheduled_acquisition_tile::Value)+
                r.reference.capacity()*sizeof(detail::scheduled_acquisition_certificate::ReferenceErrors)+
                r.colored.capacity()*sizeof(std::array<detail::scheduled_cold_colored::Summary,2>);
            for(const auto& r:batch_ranges)n+=r.screen.capacity()*sizeof(detail::scheduled_acquisition_tile::Value)+
                r.reference.capacity()*sizeof(detail::scheduled_acquisition_certificate::ReferenceErrors)+
                r.colored.capacity()*sizeof(std::array<detail::scheduled_cold_colored::Summary,2>);
            for(const auto& r:prepared_ranges)n+=r.screen.capacity()*sizeof(detail::scheduled_acquisition_tile::Value)+
                r.reference.capacity()*sizeof(detail::scheduled_acquisition_certificate::ReferenceErrors)+
                r.colored.capacity()*sizeof(std::array<detail::scheduled_cold_colored::Summary,2>);
            n+=serial_execution?serial_execution->working_bytes():0;
            n+=executions.capacity()*sizeof(decltype(executions)::value_type);
            for(const auto& execution:executions)n+=execution->working_bytes();
            n+=processing?processing->accounting().reserved_bytes:0;
            n+=diagnostics?diagnostics->working_bytes():0;
            n+=(carrier_restart_radius.capacity()+carrier_restart_magnitude.capacity())*sizeof(double)+
                carrier_restart_revision.capacity()*sizeof(std::uint64_t);
            for(const auto& source:colored_sources)if(source)n+=source->working_bytes();
            n+=transient_reservation;
            if(original_projections)n+=sizeof(OriginalProjections)+128+original_projections->working_bytes();
            const auto capture_bytes=capture_allocated_bytes;
            // History is required future workspace, not evictable cache RAM.
            // Charge its finite reservation before Live lends any bank space.
            n+=shared_capture?(capture_fences?capture_fences->reserved_bytes():history_reservation):
                std::max(history_reservation,capture_bytes);
            return n;
        }
    };
    LazyState* lazy_state() noexcept {return scheduled?scheduled->lazy_acquisition.get():nullptr;}
    const LazyState* lazy_state() const noexcept {return scheduled?scheduled->lazy_acquisition.get():nullptr;}
    static auto& worker_lazy_view() {
        static thread_local std::pair<const Impl*,LazyState::Execution*> view{};
        return view;
    }
    LazyState::Execution& lazy_execution() {
        const auto& view=worker_lazy_view();
        return view.first==this?*view.second:*lazy_state()->serial_execution;
    }
    bool lazy_slot_active(std::size_t i)const{return !lazy_state() || (i<lazy_state()->occupied.size() && lazy_state()->occupied[i]);}
    std::size_t logical_id(std::size_t slot)const{return lazy_state()?lazy_state()->logical.at(slot):slot;}
    std::size_t logical_count()const{return lazy_state()?lazy_state()->catalog->size():hypotheses.size();}
    std::optional<std::size_t> resident_slot(std::size_t id)const {
        if(!lazy_state())return id<hypotheses.size()?std::optional<std::size_t>{id}:std::nullopt;
        const auto it=lazy_state()->slots.find(id);return it==lazy_state()->slots.end()?std::nullopt:std::optional<std::size_t>{it->second};
    }
    // Scan admitted aliases in immutable original order, even if they were
    // promoted into their stable slots in a different physical block.
    template<class Consumer> void for_each_resident(Consumer consume) {
        if(lazy_state()){for(const auto& [id,slot]:lazy_state()->slots)consume(slot,hypotheses[slot]);}
        else for(std::size_t i=0;i<hypotheses.size();++i)consume(i,hypotheses[i]);
    }
    template<class Consumer> void for_each_resident(Consumer consume) const {
        if(lazy_state()){for(const auto& [id,slot]:lazy_state()->slots)consume(slot,hypotheses[slot]);}
        else for(std::size_t i=0;i<hypotheses.size();++i)consume(i,hypotheses[i]);
    }
    struct Emission {
        const Hypothesis* owner=nullptr;
        std::uint64_t first_symbol=0,last_symbol=0,stream_first_sample=0;
        long double origin=0;
        double frequency=0,reported_frequency=0,carrier_score=0;
        bool ended=false,published_unknown=false;
    };
    std::vector<Emission> emissions;
    struct TimingDecision {
        std::uint64_t index=std::numeric_limits<std::uint64_t>::max(),epoch=0,ordinal=0;
        double score=0;
        bool accepted=false,full=false;
        std::uint8_t bit=0;
    };
    struct TimingRelation {
        std::size_t neighbor=std::numeric_limits<std::size_t>::max();
        std::uint64_t next=0,first_unaccepted=std::numeric_limits<std::uint64_t>::max();
        double owner_score=0,guide_score=0;
        bool invalid=false;
    };
    struct TimingState {
        std::array<TimingDecision,2> decisions{};
        std::array<TimingRelation,2> relations{};
        std::uint64_t first=0,last_accepted=0,terminal_index=0;
        bool tracking=false,pending_terminal=false;
    };
    struct TimingEntry {std::size_t hypothesis=0;TimingState state;};
    // Subscribe on first admission, before a later neighbor can complete the
    // same symbol. Idle long banks retain no duration- or lane-sized guide
    // allocation. Two tagged slots survive serial completion/payload erasure.
    // Actual admitted state is charged to the original workspace; exhaustion
    // is explicit, never permission to publish an unproved terminal event.
    std::unique_ptr<std::vector<TimingEntry>> timing;
    // Only unresolved subsecond schedules need extra fits. Ordinary explicit
    // schedules retain the original pair of per-hypothesis accumulators.
    std::vector<std::array<Fit,2>> alternate_fits;
    std::vector<std::array<detail::CorrelationChipEvidence,2>> chip_evidence;
    std::vector<double> pending_chip_scores;
    std::unique_ptr<std::vector<detail::CorrelationOuterEvidence>> outer_evidence;
    // Allocated only for eligible long patterns; one active section per bit
    // and schedule, independent of symbol duration and input chunk length.
    std::vector<std::array<DriftFit,2>> drift_fits;
    std::vector<std::array<DifferentialFit,2>> differential_fits;
    std::vector<Bank> banks;
    // Only the partial-symbol affine path needs these per-lane diagnostics.
    // Separate counters keep worker accumulation independent and bounded.
    std::vector<std::uint64_t> affine_counts;
    struct AffineCoefficients {
        std::array<Complex,2> value{},slope{};
        std::uint64_t first=0,end=0;
        void invalidate() {
            OPENSSL_cleanse(value.data(),sizeof(value));
            OPENSSL_cleanse(slope.data(),sizeof(slope));
            first=end=0;
        }
        ~AffineCoefficients(){invalidate();}
    };
    static_assert(sizeof(AffineCoefficients)==80);
    // Slots are permanently bound to a lane and phase group. Their absolute
    // ranges never cross a chip, detector boundary or symbol; completion
    // invalidates all slots before changing the private stream address.
    std::vector<AffineCoefficients> affine_coefficients;
    struct PulseLattice {
        long double anchor=0,rate=1,start=0,first_offset=0;
        std::size_t frequency=0;
        std::uint64_t next=0;
        Complex carrier_square{};
        double carrier_norm=1;
        detail::CorrelationPulseKernel kernel;
        detail::CorrelationPulseCell pending;
        // An affine pulse piece survives original oscillator blocks and caller
        // pushes. Only its four measured moments are accumulated until the next
        // table knot; private coefficients never enter this shared frontend.
        detail::CorrelationPulseSegment segment;
        std::uint64_t segment_first=0,segment_observed=0;
        std::complex<long double> segment_sum{},segment_moment{};
        std::vector<detail::CorrelationPulseCell> cells;
        PulseLattice(long double origin,long double ratio,std::size_t bank,
                     std::uint64_t chip,double frequency_hz,std::uint32_t sample_rate)
            :anchor(origin),rate(ratio),frequency(bank),kernel(chip,ratio,frequency_hz,sample_rate) {
            const auto position=std::floor(-anchor*rate/chip);
            require(position>=std::numeric_limits<std::int64_t>::min() &&
                    position<std::numeric_limits<std::int64_t>::max(),"pulse cell coordinate overflow");
            pending.chip=static_cast<std::int64_t>(position);
            start=anchor+static_cast<long double>(pending.chip)*chip/rate;
            next=static_cast<std::uint64_t>(std::max(0.L,std::ceil(anchor+
                static_cast<long double>(pending.chip+1)*chip/rate)));
        }
    };
    std::vector<PulseLattice> pulse_lattices;
    struct PulseAddress {std::size_t lattice=0;std::int64_t shift=0;};
    std::vector<PulseAddress> pulse_addresses;
    std::vector<PatternEvidence> history;
    std::vector<PatternBurst> bursts;
    std::vector<Complex> points;
    std::size_t point_begin=0,point_count=0;
    Complex previous_point{};

    Impl(Config c,PatternSearch search_options,std::size_t bytes,PatternCorrelatorOptions work_options)
        :config(c),search(std::move(search_options)),code(c,c.stream_epoch),budget(bytes),options(work_options) {
        validate(c);
        // Developer-only process override for full GUI/Live paired diagnostics.
        // It changes search preparation only; all precision/reference gates
        // remain below. Unset is default-off until complete cost qualifies it.
        if(const auto* mode=std::getenv("DATAPUMP_RX_TEMPORAL_TEMPLATES");mode && std::string_view(mode)=="1")
            options.scheduled_temporal_template_experiment=true;
        if(c.scheduled_hopping && c.scheduled_hopping->enabled() && !c.scheduled_hopping->continuous()) {
                require(c.data_key.has_value(),"scheduled reception requires a calendar key");
                require(c.stream_epoch<=std::numeric_limits<std::uint64_t>::max()/c.sample_rate,
                    "scheduled UTC epoch exceeds sample coordinates");
                scheduled=std::make_unique<ScheduledState>();
                scheduled->epoch_sample=c.stream_epoch*c.sample_rate;
                scheduled->map=std::make_unique<SchedHopSourceMap>(*c.scheduled_hopping,*c.data_key,c.sample_rate);
                if(packed_scheduled_symbols(c))scheduled->calendar=std::make_unique<ScheduledSymbolCalendar>(c);
                if(c.fake_hopping)scheduled->fhss_shape=FhssPowerShape(*c.fake_hopping,c.sample_rate,
                    pattern_pulse_padding_samples(c),code.symbol_samples());
        }
        if(scheduled_map() && !options.amplitude_only) {
            scheduled->complex_geometry=pattern_scheduled_complex_geometry(c,search);
            if(!scheduled->complex_geometry.supported)throw Error(scheduled->complex_geometry.reason);
        }
        if(options.amplitude_only) {
            amplitude_geometry=std::make_unique<PatternAmplitudeGeometry>(pattern_amplitude_geometry(c,search));
            if(!amplitude_geometry->supported)throw Error(amplitude_geometry->reason);
            amplitude_scratch_reservation=amplitude_geometry->fft_samples*2*sizeof(Complex)+sizeof(detail::search_fft::DoublePlan)+
                (amplitude_geometry->bin_count+1)*sizeof(long double);
            amplitude_scratch_limit=amplitude_scratch_reservation;
            const auto frontend_bytes=static_cast<long double>(amplitude_geometry->fft_samples)*sizeof(float)+amplitude_scratch_reservation;
            require(frontend_bytes<budget,"amplitude frontend exceeds DSP workspace");
            search.hypotheses.clear();search.frequency_offsets_hz.clear();search.clock_errors_ppm={0};
            search.couple_clock_to_carrier=false;search.expand_clock_search=false;
            search.drift_tolerant=false;search.differential_window_seconds=0;
            // A single energy frontend covers every declared carrier position.
            // Its rate bank still covers the entire original clock interval.
            search.hypotheses.push_back({0,0});
            for(std::size_t j=1;j<=(amplitude_geometry->clock_hypotheses-1)/2;++j) {
                const auto ppm=static_cast<double>(j)*amplitude_geometry->clock_step_ppm;
                search.hypotheses.push_back({0,-ppm});search.hypotheses.push_back({0,ppm});
            }
            amplitude_pcm.resize(amplitude_geometry->fft_samples);
            const auto remainder=search.input_sample_origin%amplitude_geometry->fft_samples;
            amplitude_processed=remainder?amplitude_geometry->fft_samples-remainder:0;
        }
        require(!options.measure_affine_work || search.worker_threads==1,
                "detailed affine work timing requires one scoring worker");
        drift_sections=detail::drift_section_count(c,search.drift_tolerant);
        require(std::isfinite(search.differential_window_seconds) && search.differential_window_seconds>=0,
                "invalid differential window duration");
        if(drift_sections>1)differential_window=detail::differential_window_samples(
            code.symbol_samples(),code.chip_samples(),c.sample_rate,search.differential_window_seconds);
        if(search.compact_clock_search) {
            search.candidate_limit=std::min<std::size_t>(search.candidate_limit,32);
            block_samples=32;
        }
        if(scheduled_map() && !options.amplitude_only && !options.raw_reference)block_samples=scheduled_block_size;
        const std::size_t point_capacity=search.compact_clock_search?64:2048;
        phase_step=scheduled_map()?(options.amplitude_only?amplitude_geometry->receiver_phase_step_samples:
            scheduled->complex_geometry.receiver_phase_step_samples):canonical_phase_step(c);
        const auto phase_upper=search.search_stream_phases && (c.scramble || c.dsss)?
            (canonical_phase_extent(c)-1)/phase_step*phase_step:
            c.stream_phase_samples;
        const auto phase_lower=search.search_stream_phases && (c.scramble || c.dsss)?0:c.stream_phase_samples;
        if(!scheduled_map() && phase_lower<phase_upper)alternate_groups=c.fake_hopping&&c.fake_hopping->matched_gaps?
            (phase_upper-phase_lower)/phase_step:(code.symbol_samples()>=c.sample_rate?1:2);
        shaped=pattern_pulse_enabled(c);
        require(c.pattern_symbols,"streaming pattern correlator requires binary pattern transport");
        require(scheduled_map() || !deferred_symbols(c),"deferred FHSS requires the calendar-aware FFT receiver workspace");
        require(search.start_offset_seconds && std::isfinite(*search.start_offset_seconds) &&
                std::isfinite(search.start_uncertainty_seconds) && search.start_uncertainty_seconds>=0,
                "long pattern correlation requires a finite system-clock start window");
        require(std::isfinite(search.false_alarm_probability) && search.false_alarm_probability>0 && search.false_alarm_probability<1 &&
                std::isfinite(search.retain_score) && search.retain_score>=0 && search.bit_limit && search.chunk_bits &&
                search.candidate_limit && search.candidate_limit<=65536 && search.track_limit && search.track_limit<=128,
                "invalid long pattern search limits");
        if(c.oscillator_search && search.hypotheses.empty() && search.frequency_offsets_hz.empty() &&
           search.clock_errors_ppm==std::vector<double>{0} && !search.couple_clock_to_carrier)
            search.hypotheses=oscillator_pattern_search(c).hypotheses;
        const bool paired=!search.hypotheses.empty();
        long double highest_rate=1,lowest_rate=1;
        if(paired) {
            require(search.hypotheses.size()<=maximum_pattern_frequency_rate_hypotheses,
                    "frequency/rate bank exceeds finite hypothesis limit");
            search.frequency_offsets_hz.clear();search.clock_errors_ppm={0};
            search.couple_clock_to_carrier=false;
            for(const auto& pair:search.hypotheses) {
                require(std::isfinite(pair.frequency_offset_hz) && std::isfinite(pair.clock_error_ppm) &&
                        std::abs(pair.clock_error_ppm)<=10000,"invalid paired frequency/rate hypothesis");
                highest_rate=std::max(highest_rate,1+static_cast<long double>(pair.clock_error_ppm)*1e-6L);
                lowest_rate=std::min(lowest_rate,1+static_cast<long double>(pair.clock_error_ppm)*1e-6L);
                if(std::find(search.frequency_offsets_hz.begin(),search.frequency_offsets_hz.end(),pair.frequency_offset_hz)==
                   search.frequency_offsets_hz.end())search.frequency_offsets_hz.push_back(pair.frequency_offset_hz);
            }
            require(search.frequency_offsets_hz.size()<=maximum_pattern_frequency_hypotheses,
                    "frequency bank exceeds finite hypothesis limit");
        } else {
            require(!search.clock_errors_ppm.empty() && search.clock_errors_ppm.size()<=65,"clock-rate bank must contain 1..65 hypotheses");
            for(auto ppm:search.clock_errors_ppm) {
                require(std::isfinite(ppm) && std::abs(ppm)<=10000,"clock-rate hypotheses must fit +/-10000 ppm");
                highest_rate=std::max(highest_rate,1+static_cast<long double>(ppm)*1e-6L);
                lowest_rate=std::min(lowest_rate,1+static_cast<long double>(ppm)*1e-6L);
            }
            if(search.frequency_offsets_hz.empty()) {
                const auto step=.25*c.sample_rate/static_cast<double>(code.symbol_samples());
                require(!search.expand_clock_search || default_pattern_frequency_search(c).count<=5,
                        "expanded carrier competition requires FFT workspace");
                search.frequency_offsets_hz={0,-step,step,-2*step,2*step};
            }
            require(!search.couple_clock_to_carrier && (!search.expand_clock_search || search.frequency_offsets_hz.size()<=5),
                    "coupled carrier competition requires FFT workspace");
            require(search.frequency_offsets_hz.size()<=65,"frequency bank exceeds 65 hypotheses");
        }
        if(scheduled_map() && !options.amplitude_only) {
            std::set<double> exact_rates;
            for(const auto& pair:search.hypotheses)exact_rates.insert(pair.clock_error_ppm);
            scheduled->scheduled_cache_possible=paired?search.hypotheses.size()>exact_rates.size():search.frequency_offsets_hz.size()>1;
        }
        const auto tone_limit=static_cast<double>(c.sample_rate)/(4*static_cast<double>(code.chip_samples()));
        const auto offset_limit=paired?pattern_frequency_offset_limit(c):c.bandwidth_hz/8;
        for(auto frequency:search.frequency_offsets_hz) {
            require(std::isfinite(frequency) && std::abs(frequency)<=offset_limit,"frequency hypothesis exceeds occupied band");
            if(c.spreading_mode==SpreadingMode::tone)
                require(std::abs(frequency)<tone_limit,"tone frequency uncertainty aliases binary labels");
        }
        const auto qualified=search.qualified_start_window;
        if(qualified)require(std::isfinite(qualified->epoch_origin_samples) &&
            std::isfinite(qualified->phase_scale) && qualified->phase_scale>0 &&
            std::isfinite(qualified->half_width_samples) && qualified->half_width_samples>=0,
            "invalid qualified UTC start map");
        // A timing prior prunes the existing lattice; it must not translate
        // that lattice to the edge of the smaller interval. Translating it
        // changes candidate fits and can move physical symbol endpoints.
        // Keep original indices/parity and the conservative full preflight.
        const auto lower=(static_cast<long double>(*search.start_offset_seconds)-search.start_uncertainty_seconds)*c.sample_rate;
        const auto upper=(static_cast<long double>(*search.start_offset_seconds)+search.start_uncertainty_seconds)*c.sample_rate;
        origin_lower=lower;
        if(scheduled_map()) {
            scheduled->timing_radius=lower<upper?code.chip_samples()/(4.L*(options.amplitude_only?amplitude_geometry->receiver_timing_subdivisions:
                scheduled->complex_geometry.receiver_timing_subdivisions)):0;
            scheduled->clock_radius=(options.amplitude_only?(amplitude_geometry->clock_hypotheses>1?
                amplitude_geometry->clock_step_ppm/2:amplitude_geometry->clock_half_width_ppm):
                scheduled->complex_geometry.clock_cell_radius_ppm)*1e-6L;
        }
        require(std::isfinite(lower) && std::isfinite(upper) && std::abs(lower)<1e15L && std::abs(upper)<1e15L,
                "clock start window exceeds precise sample-coordinate range");
        const auto step=std::max(1.L,std::floor(static_cast<long double>(code.chip_samples())/(2*highest_rate)));
        const auto pair_count=paired?search.hypotheses.size():search.frequency_offsets_hz.size()*search.clock_errors_ppm.size();
        const auto origin_step=[&](long double ratio) {
            const auto original=paired?static_cast<long double>(code.chip_samples())/(2*ratio):step;
            return scheduled_map()?original/(options.amplitude_only?amplitude_geometry->receiver_timing_subdivisions:
                scheduled->complex_geometry.receiver_timing_subdivisions):original;
        };
        const auto phase_bounds=[&](long double origin,long double previous,long double next) {
            auto lo=phase_lower,hi=phase_upper;
            if(qualified) {
                const auto cell_lower=(previous+origin)/2,cell_upper=(origin+next)/2;
                const auto pad=64*std::numeric_limits<long double>::epsilon()*
                    std::max({1.L,std::abs(cell_lower),std::abs(cell_upper),
                        std::abs(qualified->epoch_origin_samples),qualified->half_width_samples});
                auto a=(cell_lower-qualified->epoch_origin_samples-qualified->half_width_samples-pad)/qualified->phase_scale;
                auto b=(cell_upper-qualified->epoch_origin_samples+qualified->half_width_samples+pad)/qualified->phase_scale;
                const auto cell_width=search.search_stream_phases?
                    (scheduled_map()?phase_step-1:
                        canonical_phase_cell_upper(config,phase_upper)-phase_upper):0;
                a-=cell_width;
                if(a>phase_upper || b<phase_lower)return std::optional<PhaseGroup>{};
                a=std::clamp(a,static_cast<long double>(phase_lower),static_cast<long double>(phase_upper));
                b=std::clamp(b,static_cast<long double>(phase_lower),static_cast<long double>(phase_upper));
                const auto first=std::ceil((a-phase_lower)/phase_step),last=std::floor((b-phase_lower)/phase_step);
                if(first>last)return std::optional<PhaseGroup>{};
                lo=phase_lower+static_cast<std::uint64_t>(first)*phase_step;
                hi=phase_lower+static_cast<std::uint64_t>(last)*phase_step;
            }
            return std::optional<PhaseGroup>{{lo,hi}};
        };
        std::vector<PhaseGroup> scheduled_phases;
        if(scheduled_calendar()) {
            // The transmitter may begin only at exact global packed slots.
            // Guard offsets need not align with the old cipher-address gcd.
            // Enumerate actual starts, never every sample or elapsed frame.
            std::size_t size=0;
            scheduled_calendar()->for_each_phase_in_epoch(config.stream_epoch,[&](std::uint64_t phase) {
                if(phase>=phase_lower && phase<=phase_upper)++size;
            });
            require(size<=bytes/sizeof(PhaseGroup),"scheduled packed phase list exceeds DSP workspace");
            scheduled_phases.reserve(size);
            scheduled_calendar()->for_each_phase_in_epoch(config.stream_epoch,[&](std::uint64_t phase) {
                if(phase>=phase_lower && phase<=phase_upper)scheduled_phases.push_back({phase,phase});
            });
        } else if(scheduled_map()) {
            scheduled_phases.reserve(8);
            auto cursor=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(phase_lower);
            const auto last=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(phase_upper);
            while(cursor.distance(last)<=0) {
                const auto group=scheduled_map()->locate(cursor);
                if(!group.active) {
                    const auto next=scheduled_map()->source_to_physical(group.source,false);
                    if(next.distance(cursor)<=0)throw Error("scheduled phase inverse did not advance");
                    cursor=next;if(cursor.distance(last)>0)break;
                    continue;
                }
                const auto low=std::max<std::uint64_t>(phase_lower,group.physical_begin>scheduled->epoch_sample?
                    group.physical_begin-scheduled->epoch_sample:0);
                const auto high=std::min<std::uint64_t>(phase_upper,group.physical_end-1-scheduled->epoch_sample);
                const auto first=phase_lower+(low-phase_lower+phase_step-1)/phase_step*phase_step;
                const auto end=phase_lower+(high-phase_lower)/phase_step*phase_step;
                if(first<=end) {
                    require(scheduled_phases.size()<8,"scheduled active phase union exceeds bounded capacity");
                    scheduled_phases.push_back({first,end});
                }
                cursor={group.physical_end,0};
            }
        }
        // Bounded calendar representation retains the original scorer and
        // every required logical alternative. Explicit zero keeps the dense
        // reference; numerical screens retain their own eligibility/fallbacks.
        const auto selection_budget=search.receiver_strategy_workspace_bytes?
            std::min(budget,search.receiver_strategy_workspace_bytes):budget;
        if(scheduled && detail::scheduled_acquisition_policy::select(scheduled->complex_geometry.workspace_lower_bound_bytes,selection_budget) &&
           scheduled_map() && !scheduled_calendar() && !options.amplitude_only &&
           !options.raw_reference && config.spreading_mode==SpreadingMode::pattern &&
           drift_sections==1 && !differential_window &&
           static_cast<long double>(code.symbol_samples())/highest_rate>=pattern_absence_seconds*c.sample_rate) {
            initialize_lazy(point_capacity,(c.spreading_mode==SpreadingMode::tone?2*pair_count:search.frequency_offsets_hz.size()),highest_rate);
            return;
        }
        long double total=0;
        for(std::size_t pair=0;pair<pair_count;++pair) {
            const auto ppm=paired?search.hypotheses[pair].clock_error_ppm:
                search.clock_errors_ppm[pair/search.frequency_offsets_hz.size()];
            const auto step=origin_step(1+static_cast<long double>(ppm)*1e-6L);
            const auto origins=std::ceil((upper-lower)/step)+1;
            if(!scheduled_map()){total+=origins;continue;}
            // A sparse warp can move two canonical phases across different
            // source-symbol boundaries. Retain independent phase state rather
            // than incorrectly sharing one index through a null plateau.
            require(origins<=10000000,"scheduled timing preflight exceeds its bounded enumeration");
            if(!qualified) {
                for(std::size_t g=0;g<scheduled_phases.size();++g)
                    total+=origins*((scheduled_phases[g].upper-scheduled_phases[g].lower)/phase_step+1);
                continue;
            }
            for(std::uint64_t i=0;i<origins;++i) {
                const auto at=std::min(upper,lower+i*step);
                const auto previous=i?std::min(upper,lower+(i-1)*step):lower;
                const auto next=i+1<origins?std::min(upper,lower+(i+1)*step):upper;
                if(const auto phases=phase_bounds(at,previous,next))for(std::size_t g=0;g<scheduled_phases.size();++g) {
                    const auto lo=std::max(phases->lower,scheduled_phases[g].lower);
                    const auto hi=std::min(phases->upper,scheduled_phases[g].upper);
                    if(lo<=hi)total+=(hi-lo)/phase_step+1;
                }
            }
        }
        const auto bank_count=c.spreading_mode==SpreadingMode::tone?2*pair_count:search.frequency_offsets_hz.size();
        long double fixed=sizeof(PatternCorrelator)+sizeof(Impl)+code.working_bytes()+
            total*(sizeof(Hypothesis)+alternate_groups*sizeof(std::array<Fit,2>))+
            bank_count*(sizeof(Bank)+(block_samples+1)*sizeof(Projection))+point_capacity*sizeof(Complex)+
            static_cast<long double>(search.candidate_limit)*sizeof(PatternEvidence)+search.track_limit*(sizeof(PatternBurst)+sizeof(Emission))+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double)+
            search.hypotheses.capacity()*sizeof(PatternFrequencyRateHypothesis);
        if(scheduled_map() && !options.amplitude_only)fixed+=total*(sizeof(ScheduledCarry)+
            3*sizeof(std::uint64_t)+sizeof(long double));
        if(scheduled_map() && !options.amplitude_only)
            fixed+=bank_count*sizeof(detail::PhysicalMomentCache::View);
        if(scheduled_map())fixed+=sizeof(ScheduledState)+scheduled_map()->working_bytes()+total*(sizeof(SchedHopCoordinate)+sizeof(long double)+sizeof(std::uint64_t))+
            scheduled_phases.capacity()*sizeof(PhaseGroup)+(scheduled_calendar()?scheduled_calendar()->working_bytes()+total*sizeof(std::uint64_t):0);
        if(options.amplitude_only) {
            fixed+=sizeof(PatternAmplitudeGeometry)+amplitude_pcm.capacity()*sizeof(float)+amplitude_scratch_reservation;
            // Band selection is detector geometry, not a reward for smaller
            // storage. Preserve the preceding unfactored moment allowance;
            // otherwise the same workspace changes band widths/trial charges.
            // Production also supplies the original planner's explicit cap.
            const auto per_channel=total*(alternate_groups+1)*sizeof(std::array<detail::EnvelopeFit,2>)+sizeof(PatternAmplitudeChannel);
            const auto retention=2*total+2*search.track_limit+2;
            const auto available=std::floor((bytes-fixed-retention)/per_channel);
            require(available>=1,"complete amplitude timing coverage exceeds DSP workspace");
            const auto selected=static_cast<std::size_t>(std::min(available,static_cast<long double>(amplitude_geometry->channel_count)));
            if(selected<amplitude_geometry->channel_count)amplitude_geometry->reason="coarse amplitude channels widened to preserve complete coverage within workspace";
            amplitude_geometry->channel_count=selected;
            amplitude_channels.reserve(selected);
            for(std::size_t j=0;j<selected;++j)amplitude_channels.push_back(pattern_amplitude_channel(*amplitude_geometry,j));
            fixed+=total*(alternate_groups+1)*sizeof(detail::EnvelopeTemplateFit)+total*sizeof(AmplitudePhaseEntry)+
                total*(alternate_groups+1)*selected*sizeof(decltype(amplitude_fits)::value_type)+amplitude_channels.capacity()*sizeof(PatternAmplitudeChannel);
            require(fixed+retention<bytes,"selected amplitude bands exceed actual factored workspace");
        }
        // A symbol at least six seconds long cannot form an unconfirmed
        // multi-symbol acquisition chain before physical absence expires it.
        guard_chains=config.spreading_mode==SpreadingMode::pattern && (c.scramble || c.dsss) &&
            static_cast<long double>(code.symbol_samples())/highest_rate<pattern_absence_seconds*c.sample_rate;
        const bool chip_guard=guard_chains || (scheduled_map() && !options.amplitude_only);
        if(chip_guard)fixed+=total*((alternate_groups+1)*sizeof(decltype(chip_evidence)::value_type)+sizeof(double)+
            (scheduled_map() && !options.amplitude_only?sizeof(decltype(scheduled->scheduled_carry_chips)::value_type):0));
        require(total>=1 && total<=std::numeric_limits<std::size_t>::max() && fixed<bytes,
                "complete half-chip clock/frequency/rate coverage exceeds DSP workspace");
        const auto count=static_cast<std::size_t>(total);
        if(scheduled_map() && !options.amplitude_only){scheduled->scheduled_carry.resize(count);scheduled->template_uncached.resize(count);}
        if(chip_guard){chip_evidence.resize(count*(alternate_groups+1));
            if(guard_chains)pending_chip_scores.resize(count);
            if(scheduled_map() && !options.amplitude_only)scheduled->scheduled_carry_chips.resize(count);}
        const auto denominator=2*count+2*search.track_limit+2;
        if(drift_sections>1) {
            const auto extra=total*(alternate_groups+1)*(sizeof(std::array<DriftFit,2>)*
                (scheduled_map() && !options.amplitude_only?2:1)+
                (scheduled_map() && !options.amplitude_only?2*sizeof(std::array<unsigned char,2>):0));
            // Drift tolerance is optional; it must not displace an otherwise
            // affordable complete clock bank or its minimum bit retention.
            if(fixed+extra+denominator<=bytes) {
                drift_reserved=static_cast<std::size_t>(extra);fixed+=extra;
            } else drift_sections=1;
        }
        if(drift_sections==1)differential_window=0;
        if(differential_window) {
            const auto extra=total*(alternate_groups+1)*sizeof(std::array<DifferentialFit,2>)*
                (scheduled_map() && !options.amplitude_only?2:1);
            if(fixed+extra+denominator<=bytes)fixed+=extra;
            else differential_window=0;
        }
        // New evidence must not displace any previously affordable detector.
        // The coherent independent-code statistic is not a substitute for a
        // phase-diffusion detector; leave that geometry on its original path.
        outer_presence=search.outer_presence_guard && config.dsss_factor>1 &&
            config.spreading_mode==SpreadingMode::pattern && drift_sections==1 && !differential_window;
        if(outer_presence) {
            const auto extra=sizeof(*outer_evidence)+total*(alternate_groups+1)*(sizeof(detail::CorrelationOuterEvidence)+
                (chip_guard?0:sizeof(decltype(chip_evidence)::value_type)))*
                (scheduled_map() && !options.amplitude_only?2:1);
            if(fixed+extra+denominator<=bytes) {
                fixed+=extra;outer_evidence=std::make_unique<std::vector<detail::CorrelationOuterEvidence>>(count*(alternate_groups+1));
                if(!chip_guard)chip_evidence.resize(count*(alternate_groups+1));
                if(scheduled_map() && !options.amplitude_only) {
                    scheduled->scheduled_carry_outer.resize(count);
                    if(!chip_guard)scheduled->scheduled_carry_chips.resize(count);
                }
            } else outer_presence=false;
        }
        timing_enabled=static_cast<long double>(code.symbol_samples())/highest_rate>=
            pattern_absence_seconds*c.sample_rate;
        // Full chip cells are sufficient statistics for this exact finite
        // pulse. Two half-chip origin parities share each projection. Affine
        // spans below cover partial chips or unaffordable shared-cell kernels;
        // unsupported legacy fractional grids retain the raw reference path.
        const bool pulse_geometry=!scheduled_map() && !options.amplitude_only && !options.raw_reference && shaped && code.symbol_samples()>=16ULL*c.sample_rate &&
            // Historical low-level compact banks promise a sub-64-KiB idle
            // footprint. Application oscillator banks provide explicit pairs;
            // retain the bounded raw fallback for legacy long-chip callers.
            (code.chip_samples()<=4096 || paired) &&
            code.symbol_samples()%(4*code.chip_samples())==0 &&
            (paired || (code.chip_samples()%2==0 &&
                std::all_of(search.clock_errors_ppm.begin(),search.clock_errors_ppm.end(),[](double ppm){return ppm==0;})));
        const auto pulse_cell_capacity=static_cast<std::size_t>(std::ceil(block_samples*highest_rate/code.chip_samples()))+2;
        const auto pulse_count=3*pair_count;
        const auto pulse_extra=static_cast<long double>(pulse_count)*
            (sizeof(PulseLattice)+pulse_cell_capacity*sizeof(detail::CorrelationPulseCell))+
            total*sizeof(PulseAddress)+detail::correlation_pulse_scratch_bytes+
            (code.chip_samples()>4096?bank_count*(block_samples+1)*sizeof(Complex):0);
        const bool pulse_enabled=pulse_geometry && fixed+pulse_extra+denominator<=bytes;
        if(pulse_enabled) {fixed+=pulse_extra;pulse_lattices.reserve(pulse_count);pulse_addresses.reserve(count);}
        // Affine spans also retain a whole symbol when its shared-cell kernels
        // cannot fit. They keep the same complete search and pulse geometry
        // without retaining a kernel or observations per lane.
        const auto affine_capacity=std::tuple_size_v<decltype(Bank::affine_lengths)>;
        const auto segment_extra=bank_count*((block_samples+1)*sizeof(Complex)+
            affine_capacity*sizeof(detail::CorrelationCarrierMoments))+total*sizeof(std::uint64_t);
        pulse_segmented=!scheduled_map() && !options.amplitude_only && !options.raw_reference && shaped && paired &&
            code.symbol_samples()>=16ULL*c.sample_rate &&
            code.chip_samples()>=1024 &&
            !pulse_enabled &&
            fixed+segment_extra+denominator<=bytes;
        if(pulse_segmented){fixed+=segment_extra;affine_counts.resize(count);}
        // The source warp is affine INSIDE an active flat interval. Contract
        // table-affine pulse spans there; never interpolate through a gate,
        // limiter, chip ownership, detector window or source-symbol boundary.
        // FP64-force remains an independent original-template reference.
        const auto dense_count=static_cast<std::size_t>(std::min<long double>(block_size,
            std::ceil(code.chip_samples()/(256*lowest_rate))+1));
        const auto mapped_extra=bank_count*((block_samples+1)*sizeof(Complex)+dense_count*sizeof(detail::CorrelationCarrierMoments))+
            total*sizeof(std::uint64_t);
        scheduled_affine=options.scheduled_affine_experiment && scheduled_map() && !options.amplitude_only && !options.raw_reference && shaped && !outer_presence &&
            code.chip_samples()>=1024 && config.search_arithmetic!=SearchArithmetic::fp64 &&
            fixed+mapped_extra+denominator<=bytes;
        if(scheduled_affine){fixed+=mapped_extra;affine_counts.resize(count);}
        if(scheduled_map())scheduled->moments_eligible=!options.amplitude_only && !options.raw_reference &&
            !options.scheduled_moment_reference && !scheduled_affine && shaped &&
            code.chip_samples()>=1024 && config.search_arithmetic!=SearchArithmetic::fp64 &&
            fixed+total*sizeof(std::uint64_t)+denominator<=bytes;
        if(scheduled_map() && scheduled->moments_eligible){fixed+=total*sizeof(std::uint64_t);affine_counts.resize(count);}
        require(fixed+denominator<=bytes,"clock-search workspace cannot retain symbol evidence");
        const auto remaining=bytes-static_cast<std::size_t>(std::ceil(fixed));
        bit_limit=std::min(search.bit_limit,remaining/denominator);
        require(bit_limit>0,"clock-search workspace cannot retain symbol evidence");
        // Coefficient reuse is optional and evictable. Preserve the preceding
        // path's bit limit; room_for() releases these coefficients before a
        // later payload allocation needs their space. Promised detector state
        // is already charged in fixed, and minimum retention must still fit.
        // At shorter chips natural table spans rarely outlive a 32-sample
        // block; retain the preceding arithmetic there without a cache.
        const auto coefficient_count=total*(alternate_groups+1);
        const auto coefficient_extra=coefficient_count*sizeof(AffineCoefficients);
        if(pulse_segmented && !outer_presence && !options.affine_coefficient_reference && code.chip_samples()>=8192 &&
                code.chip_samples()/(256*highest_rate)>block_samples &&
                coefficient_count<=std::numeric_limits<std::size_t>::max() &&
                fixed+coefficient_extra+denominator<=bytes)
            affine_coefficients.resize(static_cast<std::size_t>(coefficient_count));
        hypotheses.reserve(count);if(scheduled_map()){scheduled->anchors.reserve(count);scheduled->observed_begin.resize(count);scheduled->last_accepted.resize(count);if(scheduled_calendar())scheduled->source_starts.resize(count);
            if(!options.amplitude_only){scheduled->symbol_ends.resize(count);scheduled->end_indices.resize(count);scheduled->first_full_indices.resize(count);}}
        emissions.reserve(search.track_limit);banks.resize(bank_count);
        if(scheduled_map() && !options.amplitude_only)scheduled->physical_views.resize(bank_count);
        for(auto& bank:banks) {
            bank.prefix.resize(block_samples+1);
            if(pulse_segmented || scheduled_affine || (pulse_enabled && code.chip_samples()>4096))bank.first_moment.resize(block_samples+1);
            if(scheduled_affine) {
                bank.affine_carrier.resize(dense_count);
                for(std::size_t n=1;n<=dense_count;++n)bank.affine_carrier[n-1]=detail::correlation_carrier_moments(n,
                    2*tau*(c.carrier_hz+search.frequency_offsets_hz[&bank-banks.data()])/c.sample_rate);
            }
        }
        points.resize(point_capacity);
        require(!alternate_groups || count<=std::numeric_limits<std::size_t>::max()/alternate_groups,
                "pattern phase fits exceed address space");
        alternate_fits.resize(count*alternate_groups);
        if(options.amplitude_only) {
            amplitude_fits.resize(count*(alternate_groups+1)*amplitude_channels.size());
            amplitude_templates.resize(count*(alternate_groups+1));
            amplitude_phases.resize(count);
        }
        if(differential_window) {
            differential_fits.resize(count*(alternate_groups+1));
            if(scheduled_map() && !options.amplitude_only)scheduled->scheduled_carry_differential.resize(count);
        }
        if(drift_sections>1) {
            require(count<=std::numeric_limits<std::size_t>::max()/(alternate_groups+1),
                    "pattern drift fits exceed address space");
        }
        // Compact cold epochs can wait hours before there is evidence to keep.
        // Admission still reserves the complete future FIFO; its backing is
        // materialized once, without reducing diagnostic retention.
        if(!defer_cold_history())history.reserve(search.candidate_limit);
        bursts.reserve(search.track_limit);
        for(std::size_t pair=0;pair<pair_count;++pair) {
            const auto rate=paired?pair:pair/search.frequency_offsets_hz.size();
            const auto f=paired?static_cast<std::size_t>(std::find(search.frequency_offsets_hz.begin(),search.frequency_offsets_hz.end(),
                search.hypotheses[pair].frequency_offset_hz)-search.frequency_offsets_hz.begin()):pair%search.frequency_offsets_hz.size();
            const auto ppm=paired?search.hypotheses[pair].clock_error_ppm:search.clock_errors_ppm[rate];
            const auto ratio=1+static_cast<long double>(ppm)*1e-6L;
            const auto pair_step=origin_step(ratio);
            const auto origins=static_cast<std::size_t>(std::ceil((upper-lower)/pair_step)+1);
            const auto lattice_base=pulse_lattices.size();
            const auto make_lattice=[&](long double anchor) {
                pulse_lattices.emplace_back(anchor,ratio,f,code.chip_samples(),
                    c.carrier_hz+search.frequency_offsets_hz[f],c.sample_rate);
                pulse_lattices.back().cells.reserve(pulse_cell_capacity);
            };
            if(pulse_enabled) {
                make_lattice(lower);
                // A clipped second origin has its own lattice below. Do not
                // project an unused half-chip parity outside the search window.
                if(origins>1 && lower+pair_step<=upper)make_lattice(lower+pair_step);
            }
            if(c.spreading_mode==SpreadingMode::tone)for(unsigned bit=0;bit<2;++bit)
                banks[2*pair+bit].frequency=c.carrier_hz+search.frequency_offsets_hz[f]+
                    (bit?1.:-1.)*tone_limit*static_cast<double>(ratio);
            else banks[f].frequency=c.carrier_hz+search.frequency_offsets_hz[f];
            for(std::size_t i=0;i<origins;++i) {
                Hypothesis h;h.origin=std::min(upper,lower+static_cast<long double>(i)*pair_step);
                h.rate=ratio;h.frequency=static_cast<std::uint32_t>(f);
                h.rate_index=static_cast<std::uint32_t>(rate);h.tone_bank_base=static_cast<std::uint32_t>(2*pair);
                h.phase_lower=phase_lower;h.phase_upper=phase_upper;
                if(qualified) {
                    // Retain every phase whose arrival interval intersects
                    // this timing lane's coverage cell. A window narrower
                    // than half a chip still keeps its nearest candidate(s).
                    const auto previous=i?std::min(upper,lower+static_cast<long double>(i-1)*pair_step):lower;
                    const auto next=i+1<origins?std::min(upper,lower+static_cast<long double>(i+1)*pair_step):upper;
                    const auto cell_lower=i?(previous+h.origin)/2:lower;
                    const auto cell_upper=i+1<origins?(h.origin+next)/2:upper;
                    const auto pad=64*std::numeric_limits<long double>::epsilon()*
                        std::max({1.L,std::abs(cell_lower),std::abs(cell_upper),
                            std::abs(qualified->epoch_origin_samples),qualified->half_width_samples});
                    auto lo=(cell_lower-qualified->epoch_origin_samples-qualified->half_width_samples-pad)/qualified->phase_scale;
                    auto hi=(cell_upper-qualified->epoch_origin_samples+qualified->half_width_samples+pad)/qualified->phase_scale;
                    // Intersect every physical phase represented by the exact
                    // cipher cell; do not instantiate a template per sample.
                    const auto cell_width=search.search_stream_phases?
                        (scheduled_map()?phase_step-1:
                        canonical_phase_cell_upper(config,phase_upper)-phase_upper):0;
                    lo-=cell_width;
                    if(lo>phase_upper || hi<phase_lower)continue;
                    lo=std::clamp(lo,static_cast<long double>(phase_lower),static_cast<long double>(phase_upper));
                    hi=std::clamp(hi,static_cast<long double>(phase_lower),static_cast<long double>(phase_upper));
                    const auto first=std::ceil((lo-phase_lower)/phase_step);
                    const auto last=std::floor((hi-phase_lower)/phase_step);
                    if(first>last)continue;
                    h.phase_lower=phase_lower+static_cast<std::uint64_t>(first)*phase_step;
                    h.phase_upper=phase_lower+static_cast<std::uint64_t>(last)*phase_step;
                }
                if(pulse_enabled) {
                    PulseAddress address;
                    if(h.origin==upper && h.origin!=lower+static_cast<long double>(i)*pair_step) {
                        make_lattice(h.origin);address.lattice=pulse_lattices.size()-1;
                    } else {address.lattice=lattice_base+i%2;address.shift=static_cast<std::int64_t>(i/2);}
                    pulse_addresses.push_back(address);
                }
                if(scheduled_map()) {
                    for(const auto& group:scheduled_phases) {
                        const auto lo=std::max(h.phase_lower,group.lower),hi=std::min(h.phase_upper,group.upper);
                        if(lo>hi)continue;
                        for(auto phase=lo;;phase+=phase_step) {
                            auto lane=h;lane.phase_lower=lane.phase_upper=phase;
                            const auto location=scheduled_map()->locate(SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(phase));
                            if(!location.active){if(phase==hi)break;else continue;}
                            const auto anchor=location.source;
                            const auto tick=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(phase).add_signed(-h.origin*ratio);
                            const auto elapsed=std::max(0.L,scheduled_map()->locate(tick).source.distance(anchor));
                            if(scheduled_calendar())lane.index=scheduled_index_at(phase,elapsed,false);
                            else {
                                const auto index=std::floor(elapsed/code.symbol_samples());
                                require(index<std::numeric_limits<std::uint64_t>::max(),"scheduled clock hint exceeds stream symbol counter");
                                lane.index=static_cast<std::uint64_t>(index);
                            }
                            hypotheses.push_back(std::move(lane));
                            scheduled->anchors.push_back(anchor);
                            if(scheduled_calendar())scheduled->source_starts[hypotheses.size()-1]=scheduled_calendar()->offset_source(
                                config.stream_epoch,phase,hypotheses.back().index);
                            scheduled->observed_begin[hypotheses.size()-1]=scheduled_map()->locate(tick).source.distance(anchor);
                            if(phase==hi)break;
                        }
                    }
                } else {
                    const auto elapsed=std::max(0.L,-h.origin)*ratio;
                    const auto index=std::floor(elapsed/code.symbol_samples());
                    require(index<std::numeric_limits<std::uint64_t>::max(),"clock hint exceeds stream symbol counter");
                    h.index=static_cast<std::uint64_t>(index);hypotheses.push_back(std::move(h));
                }
            }
        }
        require(!hypotheses.empty(),"qualified UTC interval does not intersect the existing search lattice");
        if(scheduled_map() && !options.amplitude_only) {
            scheduled->symbol_ends.resize(hypotheses.size());scheduled->end_indices.resize(hypotheses.size());scheduled->first_full_indices.resize(hypotheses.size());
            scheduled->earliest_end=std::numeric_limits<long double>::infinity();
            scheduled->drift_begin=std::numeric_limits<long double>::infinity();
            for(std::size_t i=0;i<hypotheses.size();++i) {
                const auto& h=hypotheses[i];scheduled->end_indices[i]=h.index;
                scheduled->symbol_ends[i]=scheduled_boundary(h,scheduled_source_start(h,h.index)+code.symbol_samples(),true);
                scheduled->earliest_end=std::min(scheduled->earliest_end,scheduled->symbol_ends[i]);
                scheduled->drift_begin=std::min(scheduled->drift_begin,scheduled_boundary(h,scheduled_source_start(h,h.index)+
                    detail::drift_boundary(1,code.symbol_samples(),drift_sections),false));
                scheduled->first_full_indices[i]=scheduled_index_at(h.phase_lower,scheduled->observed_begin[i],true);
            }
        }
        if(pulse_segmented)for(auto& bank:banks)
            bank.prepare_carrier(block_samples,code.chip_samples()/(256*lowest_rate),c.sample_rate);
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"clock-search state exceeds DSP workspace");
        work.backend=options.amplitude_only?PatternCorrelationBackend::amplitude:pulse_segmented?PatternCorrelationBackend::pulse_segments:pulse_lattices.empty()?PatternCorrelationBackend::raw:
            code.chip_samples()>4096?PatternCorrelationBackend::pulse_moments:PatternCorrelationBackend::pulse;
        work.hypotheses=hypotheses.size();work.lattices=pulse_lattices.size();work.phase_groups=alternate_groups+1;
        work.affine_coefficient_cache_entries=affine_coefficients.size();
        work.amplitude_bins=amplitude_geometry?amplitude_geometry->bin_count:0;work.amplitude_fft_samples=amplitude_geometry?amplitude_geometry->fft_samples:0;
        work.amplitude_channels=amplitude_channels.size();
        work.peak_workspace_bytes=sizeof(PatternCorrelator)+accounted_bytes;
        if(scheduled_map() && !options.amplitude_only)audio::trace::record("split_complex_constructed",[&](auto& f) {
            const bool source_curves=scheduled->moments_eligible||fine_curves_eligible();
            f.add("epoch",config.stream_epoch);f.add("sample_rate",config.sample_rate);
            f.add("chip_samples",code.chip_samples());f.add("symbol_samples",code.symbol_samples());
            f.add("physical_aperture_samples",scheduled->complex_geometry.physical_aperture_samples);
            f.add("required_frequency_step_hz",scheduled->complex_geometry.frequency_step_hz);
            f.add("required_clock_step_ppm",scheduled->complex_geometry.clock_step_ppm);
            f.add("clock_cell_radius_ppm",scheduled->complex_geometry.clock_cell_radius_ppm);
            f.add("frequency_grid_qualified",scheduled->complex_geometry.frequency_grid_qualified);
            f.add("clock_grid_qualified",scheduled->complex_geometry.clock_grid_qualified);
            f.add("hypotheses",hypotheses.size());f.add("frequency_count",banks.size());
            f.add("phase_step_samples",phase_step);f.add("timing_subdivisions",scheduled->complex_geometry.receiver_timing_subdivisions);
            f.add("workspace_bytes",work.peak_workspace_bytes);f.add("backend",scheduled_affine?"mapped physical-IQ table-affine diagnostic + exact fallback":
                source_curves?"mapped physical-IQ source curves/eligible bounded moments when workspace permits; direct fallback":"mapped physical-IQ original direct contraction");
            f.add("resolved_search_operands",source_curves?
                "FP64 private coefficients and curve interpolation; FP64 physical projections; original direct fallback":"FP64 original limited templates; FP64 physical projections");
            f.add("score_precision","FP64 real I/Q Gram; source-quarter/window guards");
            f.add("fine_curves_eligible",fine_curves_eligible());
            f.add("reuse_scope","construction eligibility only; work counters report allocation, execution and fallback");
            f.add("template_reuse",source_curves?
                "eligible bounded source-chip curves and intervals; exact worker caches; direct boundary/resource fallback":"exact per-worker private chip cache; approximate interpolation bypassed");
        });
    }

    static constexpr std::size_t lazy_minimum_slots=detail::scheduled_acquisition_memory::StablePool::minimum_slots;
    static std::size_t lazy_slot_bytes() {
        return sizeof(Hypothesis)+sizeof(ScheduledCarry)+sizeof(SchedHopCoordinate)+
            sizeof(long double)*2+sizeof(std::uint64_t)*6+sizeof(std::size_t)+1+
            2*sizeof(std::array<detail::CorrelationChipEvidence,2>)+
            2*sizeof(detail::CorrelationOuterEvidence)+sizeof(TimingEntry)*3+128;
    }
    static std::size_t lazy_slot_capacity(std::size_t available,std::size_t per_slot) {
        return detail::scheduled_acquisition_memory::StablePool::owner_capacity(available,per_slot);
    }
    static std::size_t lazy_minimum_slot_bytes() {
        return detail::scheduled_acquisition_memory::StablePool::minimum_available(lazy_slot_bytes());
    }
    void initialize_lazy(std::size_t point_capacity,std::size_t bank_count,long double highest_rate) {
        scheduled->lazy_acquisition=std::make_unique<LazyState>();
        std::size_t projection_limit=64*1024*1024;
        if(const auto* value=std::getenv("DATAPUMP_SCHED_ORIGINAL_PROJECTION_CACHE_BYTES")) {
            char* end=nullptr;const auto limit=std::strtoull(value,&end,10);
            require(end!=value && *end=='\0' && limit<=64*1024*1024,
                "original projection cache limit requires 0..67108864 bytes");
            projection_limit=static_cast<std::size_t>(limit);
        }
        lazy_state()->projection_cache_limit=projection_limit;
        lazy_state()->catalog=std::make_unique<LazyState::Catalog>(config,search,scheduled->complex_geometry,budget/8);
        lazy_state()->cursor=std::make_unique<LazyState::Catalog::Cursor>(lazy_state()->catalog->make_cursor());
        lazy_state()->serial_execution=std::make_unique<LazyState::Execution>(code,*lazy_state()->cursor,*scheduled_map());
        lazy_state()->batch_samples=lazy_state()->catalog->size()<256?1:std::min<std::size_t>(8192,std::max<std::size_t>(block_samples,config.sample_rate));
        if(const auto* value=std::getenv("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES")) {
            char* end=nullptr;const auto count=std::strtoull(value,&end,10);
            require(end!=value && *end=='\0' && count>=1 && count<=8192,"implicit diagnostic batch samples require 1..8192");
            lazy_state()->batch_samples=static_cast<std::size_t>(count);
        }
        if(search.bit_limit<2)lazy_state()->batch_samples=1;
        if(const auto* value=std::getenv("DATAPUMP_SCHED_LAZY_BATCH");value && std::string_view(value)=="0")lazy_state()->batch_samples=1;

        // This finite-horizon storage is an ELIGIBILITY bound, not permission
        // to omit old capture. A missing replay interval fails explicitly.
        // Unbounded/hour-long physical apertures stay on the original path.
        const auto arrival=2.L*search.start_uncertainty_seconds*config.sample_rate;
        const auto raw=1.L*(scheduled->complex_geometry.physical_aperture_samples+arrival+
            canonical_phase_extent(config)+2*block_samples+lazy_state()->batch_samples);
        require(std::isfinite(raw) && raw>=1 && raw<std::numeric_limits<std::size_t>::max(),
            "implicit acquisition history bound exceeds precise coordinates");
        lazy_state()->history_limit=static_cast<std::size_t>(std::ceil(raw));
        const auto history_bytes=static_cast<long double>(lazy_state()->history_limit)*(sizeof(float)+sizeof(double))+
            (std::ceil(raw/block_samples)+2)*(sizeof(LazyState::Block)+128);
        require(history_bytes<std::numeric_limits<std::size_t>::max(),"implicit history reservation overflow");
        lazy_state()->history_reservation=static_cast<std::size_t>(std::ceil(history_bytes));
        if(search.scheduled_capture) {
            require(search.scheduled_capture->guaranteed_retained_samples()>=lazy_state()->history_limit,
                "shared capture payload reservation cannot cover required original acquisition history");
            lazy_state()->shared_capture=search.scheduled_capture;
            lazy_state()->capture_retention=lazy_state()->shared_capture->subscribe(search.input_sample_origin);
            // RLE normally needs only one run for fixed-size ingress. Bound
            // pathological alternating cuts explicitly without allocating PCM
            // for every rolling bank or claiming arbitrary call geometry fits.
            const auto maximum_runs=std::max<std::size_t>(64,
                std::min<std::size_t>(16384,2*((lazy_state()->history_limit+block_samples-1)/block_samples)+128));
            const auto reservation=LazyState::CaptureFences::required_bytes(maximum_runs);
            require(reservation<budget,"implicit original capture fences exceed bank workspace");
            lazy_state()->capture_fences=std::make_unique<LazyState::CaptureFences>(maximum_runs,0,reservation);
            lazy_state()->history_reservation=reservation;
        }
        const auto journal_runs=std::max<std::size_t>(64,std::min<std::size_t>(16384,
            2*((lazy_state()->history_limit+block_samples-1)/block_samples)+128));
        detail::scheduled_processing::Limits journal_limits{journal_runs,journal_runs,journal_runs,budget};
        lazy_state()->processing=std::make_unique<detail::scheduled_processing::ProcessingJournal>(journal_limits);
        lazy_state()->diagnostics=std::make_unique<detail::scheduled_diagnostics::DeferredEvents>(scheduled_diagnostic_runs,0,budget);
        lazy_state()->carrier_restart_radius.resize(bank_count);lazy_state()->carrier_restart_magnitude.resize(bank_count,2);
        lazy_state()->carrier_restart_revision.resize(bank_count,std::numeric_limits<std::uint64_t>::max());
        const auto per_slot=lazy_slot_bytes();
        const auto fixed=sizeof(*this)+sizeof(PatternCorrelator)+code.working_bytes()+
            lazy_state()->working_bytes()+sizeof(ScheduledState)+scheduled_map()->working_bytes()+
            bank_count*sizeof(Bank)+(block_samples+1)*sizeof(Projection)+
            point_capacity*sizeof(Complex)+search.candidate_limit*sizeof(PatternEvidence)+
            search.track_limit*(sizeof(PatternBurst)+sizeof(Emission));
        require(detail::scheduled_acquisition_memory::StablePool::admits(fixed,budget,per_slot),"implicit split acquisition history, diagnostic scratch and minimum stable tracks exceed DSP workspace");
        require(lazy_state()->catalog->size()<std::numeric_limits<std::size_t>::max(),"implicit owner catalogue capacity overflow");
        lazy_state()->capacity=std::min(lazy_slot_capacity(budget-fixed,per_slot),
            std::max(lazy_minimum_slots,lazy_state()->catalog->size()+1));
        require(lazy_state()->capacity>=lazy_minimum_slots,"implicit split acquisition cannot retain diagnostic scratch and stable timing neighbours");
        const auto count=std::min<std::size_t>(lazy_state()->capacity,std::max(lazy_minimum_slots,std::min<std::size_t>(16,search.track_limit)));
        // Only Hypothesis owners need address stability. Reserve their final
        // bounded pool once, but materialize correlated guard/carry arrays as
        // actual alias state grows. Empty rolling banks do not retain maximum
        // outer/chip rings merely because capture lasts a long physical span.
        hypotheses.reserve(lazy_state()->capacity);
        hypotheses.resize(count);lazy_state()->logical.resize(count,std::numeric_limits<std::size_t>::max());
        lazy_state()->replayed.resize(count);lazy_state()->occupied.resize(count);
        // One already-charged stable owner is permanently reserved for optional
        // historical replay. Diagnostic queries cannot exhaust/grow admitted tracks.
        lazy_state()->occupied[0]=1;
        scheduled->anchors.resize(count);scheduled->observed_begin.resize(count);scheduled->last_accepted.resize(count);
        scheduled->symbol_ends.resize(count);scheduled->end_indices.resize(count);scheduled->first_full_indices.resize(count);
        scheduled->scheduled_carry.resize(count);scheduled->template_uncached.resize(count);
        chip_evidence.resize(count);scheduled->scheduled_carry_chips.resize(count);
        outer_presence=search.outer_presence_guard && config.dsss_factor>1;
        if(outer_presence){outer_evidence=std::make_unique<std::vector<detail::CorrelationOuterEvidence>>(count);
            scheduled->scheduled_carry_outer.resize(count);}
        timing_enabled=true;guard_chains=false;drift_sections=1;differential_window=0;
        emissions.reserve(search.track_limit);history.reserve(search.candidate_limit);bursts.reserve(search.track_limit);
        banks.resize(bank_count);scheduled->physical_views.resize(bank_count);
        for(std::size_t i=0;i<bank_count;++i)banks[i].frequency=config.carrier_hz+search.frequency_offsets_hz[i];
        lazy_state()->prefix.resize(block_samples+1);
        points.resize(point_capacity);
        bit_limit=std::min<std::size_t>(search.bit_limit,4096);
        require(bit_limit>0,"implicit acquisition cannot retain accepted bits");
        lazy_state()->next_due=std::numeric_limits<long double>::infinity();lazy_state()->initial_complete_due=0;
        // Constructor work is proportional to family/range metadata, never
        // the (possibly hundreds of millions of) logical timing lanes.
        for(std::size_t family=0;family<lazy_state()->catalog->families().size();++family){
            const auto& f=lazy_state()->catalog->families()[family];
            for(std::size_t r=f.range_first;r<f.range_first+f.range_count;++r){
                const auto range=lazy_state()->catalog->ranges()[r];if(range.first==range.end)continue;
                const auto a=lazy_family_lane(family,range.first),b=lazy_family_lane(family,range.end-1);
                require(a.index>=b.index && a.first_full_index>=b.first_full_index &&
                    a.index-b.index<=65536 && a.first_full_index-b.first_full_index<=65536,
                    "implicit acquisition initial source-index aperture exceeds bounded traversal");
                for(auto index=b.index;;++index){
                    const auto begin=lazy_first_true(range.first,range.end,[&](std::size_t i){return lazy_family_lane(family,i).index<=index;});
                    const auto until=lazy_first_true(begin,range.end,[&](std::size_t i){return lazy_family_lane(family,i).index<index;});
                    if(begin<until){const auto lane=lazy_family_lane(family,begin);lazy_state()->next_due=std::min(lazy_state()->next_due,
                        static_cast<long double>(lazy_state()->catalog->due(lane,index,*lazy_state()->cursor)));}
                    if(index==a.index)break;
                }
                for(auto index=b.first_full_index;;++index){
                    const auto begin=lazy_first_true(range.first,range.end,[&](std::size_t i){return lazy_family_lane(family,i).first_full_index<=index;});
                    const auto until=lazy_first_true(begin,range.end,[&](std::size_t i){return lazy_family_lane(family,i).first_full_index<index;});
                    if(begin<until){const auto lane=lazy_family_lane(family,until-1);lazy_state()->initial_complete_due=std::max(lazy_state()->initial_complete_due,
                        static_cast<long double>(lazy_state()->catalog->due(lane,index,*lazy_state()->cursor)));}
                    if(index==a.first_full_index)break;
                }
            }
        }
        scheduled->earliest_end=lazy_state()->next_due;
        accounted_bytes=working_bytes()+unallocated_history_bytes();
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,
            "implicit acquisition reservation exceeds DSP workspace");
        work.backend=PatternCorrelationBackend::scheduled_acquisition;
        work.hypotheses=lazy_state()->catalog->size();work.phase_groups=1;
        work.peak_workspace_bytes=sizeof(PatternCorrelator)+accounted_bytes;
        audio::trace::record("scheduled_implicit_constructed",[&](auto& f){
            f.add("epoch",config.stream_epoch);f.add("logical_lanes",lazy_state()->catalog->size());
            f.add("stable_slot_capacity",lazy_state()->capacity);f.add("history_limit_samples",lazy_state()->history_limit);
            f.add("backend","implicit split calendar; original full guard replay; bounded tiled acquisition pending certification");
            f.add("arithmetic","original FP64 operands/refinement; native tile arithmetic independently selected");
        });
        (void)highest_rate;
    }
    void lazy_reset_slot(std::size_t slot,const LazyState::Catalog::Lane& lane,std::uint64_t index,bool register_lane=true) {
        require(slot<hypotheses.size(),"implicit acquisition slot out of range");
        // All calendar work and map allocation precede publication. A failed
        // allocation cannot leave an occupied owner absent from the lane map.
        const auto due=lazy_state()->catalog->due(lane,index,*lazy_state()->cursor);
        const auto begin=lazy_state()->catalog->boundary(lane,static_cast<long double>(index)*code.symbol_samples(),false,*lazy_state()->cursor);
        const auto replayed=static_cast<std::uint64_t>(std::max(0.L,std::ceil(begin)));
        if(register_lane) {
            constexpr auto node_bytes=sizeof(std::pair<const std::size_t,std::size_t>)+64;
            room_for(node_bytes);
            // Pressure can evict optional detached owners; reacquire vacancy
            // after that operation. Stable required owners never relocate.
            require(!lazy_state()->occupied[slot],"implicit alias would overwrite an occupied owner");
            require(lazy_state()->slots.emplace(lane.id,slot).second,"implicit alias map already contains lane");
            accounted_bytes+=node_bytes;
        }
        auto& h=hypotheses[slot];h={};h.origin=lane.origin;h.rate=lane.rate;h.index=index;
        h.phase_lower=lane.phase_lower;h.phase_upper=lane.phase_upper;
        h.frequency=static_cast<std::uint32_t>(lane.frequency);h.rate_index=static_cast<std::uint32_t>(lane.rate_index);
        h.tone_bank_base=static_cast<std::uint32_t>(lane.tone_bank_base);
        scheduled->anchors[slot]=lane.anchor;scheduled->observed_begin[slot]=lane.observed_begin;
        scheduled->last_accepted[slot]=0;scheduled->end_indices[slot]=index;
        scheduled->symbol_ends[slot]=due;
        scheduled->first_full_indices[slot]=lane.first_full_index;
        scheduled->scheduled_carry[slot]={};scheduled->scheduled_carry_chips[slot]={};chip_evidence[slot]={};
        if(outer_presence){(*outer_evidence)[slot]={};scheduled->scheduled_carry_outer[slot]={};}
        lazy_state()->logical[slot]=lane.id;lazy_state()->occupied[slot]=1;
        // A cold lane begins at the first recorded sample, or at its own full
        // source-symbol beginning. Original accumulate() excludes earlier
        // source ownership, preserving truncated first symbols.
        lazy_state()->replayed[slot]=replayed;
    }
    void lazy_grow_slots() {
        const auto old=hypotheses.size();
        require(old<lazy_state()->capacity,"implicit acquisition stable admitted/timing alias capacity exhausted (bounded implementation pool; no hypothesis omitted)");
        const auto count=std::min(lazy_state()->capacity,std::max(old+1,old*2));
        // Existing guard/carry storage and every temporarily doubled vector
        // allocation coexist at growth. This conservative charge covers that
        // peak; allocation never evicts required observations or aliases.
        const auto per_slot=sizeof(ScheduledCarry)+sizeof(SchedHopCoordinate)+sizeof(long double)*2+
            sizeof(std::uint64_t)*5+sizeof(std::size_t)+1+
            2*sizeof(std::array<detail::CorrelationChipEvidence,2>)+
            2*sizeof(detail::CorrelationOuterEvidence)+sizeof(TimingEntry)*3+256;
        require(count<=std::numeric_limits<std::size_t>::max()/(2*per_slot),"implicit alias growth overflow");
        room_for(2*count*per_slot);
        // reserve(capacity) above makes existing Emission owner pointers stable.
        require(hypotheses.capacity()>=count,"implicit owner pool would relocate");
        try {
        hypotheses.resize(count);
        lazy_state()->logical.resize(count,std::numeric_limits<std::size_t>::max());lazy_state()->replayed.resize(count);lazy_state()->occupied.resize(count);
        scheduled->anchors.resize(count);scheduled->observed_begin.resize(count);scheduled->last_accepted.resize(count);
        scheduled->symbol_ends.resize(count,std::numeric_limits<long double>::infinity());
        scheduled->end_indices.resize(count);scheduled->first_full_indices.resize(count);
        scheduled->scheduled_carry.resize(count);scheduled->template_uncached.resize(count);
        chip_evidence.resize(count);scheduled->scheduled_carry_chips.resize(count);
        if(outer_presence){outer_evidence->resize(count);scheduled->scheduled_carry_outer.resize(count);}
        } catch(...) {
            // Shrinking cannot allocate. Preserve every pre-existing owner,
            // ring and carry even if a host allocation failed partway through.
            hypotheses.resize(old);lazy_state()->logical.resize(old);lazy_state()->replayed.resize(old);lazy_state()->occupied.resize(old);
            scheduled->anchors.resize(old);scheduled->observed_begin.resize(old);scheduled->last_accepted.resize(old);
            scheduled->symbol_ends.resize(old);scheduled->end_indices.resize(old);scheduled->first_full_indices.resize(old);
            scheduled->scheduled_carry.resize(old);scheduled->template_uncached.resize(old);
            chip_evidence.resize(old);scheduled->scheduled_carry_chips.resize(old);
            if(outer_presence){outer_evidence->resize(old);scheduled->scheduled_carry_outer.resize(old);}
            accounted_bytes=working_bytes()+unallocated_history_bytes();throw;
        }
        accounted_bytes=working_bytes()+unallocated_history_bytes();require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"implicit required alias state exceeds workspace");
        audio::trace::record("scheduled_implicit_slot_growth",[&](auto& f){f.add("old_slots",old);f.add("new_slots",count);
            f.add("owner_capacity",hypotheses.capacity());f.add("workspace_bytes",accounted_bytes+sizeof(PatternCorrelator));});

    }
    std::size_t lazy_ensure_slot(std::size_t id,std::uint64_t index) {
        if(const auto present=resident_slot(id))return *present;
        auto it=std::find(lazy_state()->occupied.begin(),lazy_state()->occupied.end(),0);
        // Optional uncommitted numerical work cannot consume required alias
        // capacity. Reclaim one detached owner before mandatory growth.
        if(it==lazy_state()->occupied.end() && lazy_evict_detached_replay())
            it=std::find(lazy_state()->occupied.begin(),lazy_state()->occupied.end(),0);
        if(it==lazy_state()->occupied.end()){lazy_grow_slots();it=std::find(lazy_state()->occupied.begin(),lazy_state()->occupied.end(),0);}
        require(it!=lazy_state()->occupied.end(),"implicit required alias growth did not create a stable slot");
        const auto slot=static_cast<std::size_t>(it-lazy_state()->occupied.begin());
        const auto lane=lazy_state()->catalog->describe(id,*lazy_state()->cursor);lazy_reset_slot(slot,lane,index);return slot;
    }
    void lazy_release_cold(std::size_t slot) {
        auto& h=hypotheses[slot];
        if(h.admitted || !h.burst.bits.empty() || timing_state(slot) ||
           std::any_of(emissions.begin(),emissions.end(),[&](const auto& e){return e.owner==&h;}))return;
        const auto removed=lazy_state()->slots.erase(lazy_state()->logical[slot]);
        const auto freed=removed*(sizeof(std::pair<const std::size_t,std::size_t>)+64)+h.burst.bits.capacity();
        require(freed<=accounted_bytes,"implicit released owner accounting underflow");
        accounted_bytes-=freed;lazy_state()->occupied[slot]=0;
        lazy_state()->logical[slot]=std::numeric_limits<std::size_t>::max();h={};
    }
    struct LazyBlockView {std::uint64_t first=0;std::span<const float> pcm;std::span<const double> energy;double maximum=0;};
    std::uint64_t lazy_history_first() const {
        return lazy_state()->shared_capture?lazy_state()->history_first:lazy_state()->capture.empty()?lazy_state()->capture_end:lazy_state()->capture.front().first;
    }
    struct LazyCaptureFence {std::uint64_t first=0;std::size_t count=0;};
    template<class Consumer> void lazy_visit_fences(std::uint64_t first,std::uint64_t end,Consumer consume) const {
        require(first<=end && first>=lazy_history_first() && end<=lazy_state()->capture_end,
            "implicit original fence interval is outside retained capture");
        if(first==end)return;
        if(lazy_state()->shared_capture)lazy_state()->capture_fences->visit(first,end-first,[&](const auto& fence){
            consume(LazyCaptureFence{fence.original_first,fence.original_count});});
        else for(const auto& b:lazy_state()->capture){if(b.first+b.pcm.size()<=first)continue;
            if(b.first>=end)break;consume(LazyCaptureFence{b.first,b.pcm.size()});}
    }
    template<class Consumer> void lazy_visit_blocks(std::uint64_t first,std::uint64_t end,Consumer consume,bool energy_needed=true) const {
        require(first<=end && first>=lazy_history_first() && end<=lazy_state()->capture_end,
            "implicit original block interval is outside retained capture");
        if(first==end)return;
        if(!lazy_state()->shared_capture) {
            for(const auto& b:lazy_state()->capture) {
                if(b.first+b.pcm.size()<=first)continue;
                if(b.first>=end)break;
                consume(LazyBlockView{b.first,b.pcm,b.energy,b.maximum});
            }
            return;
        }
        std::array<float,scheduled_block_size> pcm;
        std::array<double,scheduled_block_size+1> energy;
        lazy_state()->capture_fences->visit(first,end-first,[&](const auto& fence) {
            require(fence.original_count<=pcm.size(),"implicit original fence exceeds scratch");
            require(fence.original_first<=std::numeric_limits<std::uint64_t>::max()-search.input_sample_origin,
                "shared capture absolute replay origin overflow");
            const auto absolute=search.input_sample_origin+fence.original_first;
            auto lease=lazy_state()->shared_capture->borrow(absolute,fence.original_count);
            std::size_t copied=0;
            lease.visit([&](const auto& view) {
                require(view.absolute_first==absolute+copied && view.pcm.size()<=fence.original_count-copied,
                    "shared capture lease changed original sample coordinates");
                std::copy(view.pcm.begin(),view.pcm.end(),pcm.begin()+copied);copied+=view.pcm.size();
            });
            require(copied==fence.original_count,"shared capture lease ended before original fence");
            double maximum=0;
            if(energy_needed){energy[0]=0;
                for(std::size_t i=0;i<copied;++i){const auto x=static_cast<double>(pcm[i]);energy[i+1]=energy[i]+x*x;maximum=std::max(maximum,std::abs(x));}}
            consume(LazyBlockView{fence.original_first,std::span<const float>{pcm}.first(copied),
                energy_needed?std::span<const double>{energy}.first(copied+1):std::span<const double>{},maximum});
        });
    }
    void lazy_capture_summary() {
        if(lazy_state()->summary_revision==lazy_state()->capture_end)return;
        namespace cert=detail::scheduled_acquisition_certificate;
        lazy_state()->summary_maximum=lazy_state()->summary_union_energy=0;
        lazy_visit_blocks(lazy_history_first(),lazy_state()->capture_end,[&](const auto& b) {
            lazy_state()->summary_maximum=std::max(lazy_state()->summary_maximum,b.maximum);
            lazy_state()->summary_union_energy=cert::add(lazy_state()->summary_union_energy,
                cert::up(b.energy.back()/(1-cert::gamma(block_samples,std::numeric_limits<double>::epsilon()/2))));
        });
        lazy_state()->summary_revision=lazy_state()->capture_end;
    }
    struct LazyPcmBlockView {std::uint64_t first=0;std::span<const float> pcm;};
    template<class Consumer,class Skip> void lazy_visit_processing(std::uint64_t first,std::uint64_t end,Consumer consume,Skip skip) const {
        if(first==end)return;
        std::array<float,scheduled_block_size> pcm;
        lazy_state()->processing->processing_fences().visit(first,end-first,[&](const auto& fence) {
            require(fence.original_count<=pcm.size(),"implicit virtual processing fence exceeds scratch");
            if(skip(fence))return;
            std::size_t copied=0;
            lazy_visit_blocks(fence.original_first,fence.original_first+fence.original_count,[&](const auto& raw) {
                const auto a=std::max(raw.first,fence.original_first);
                const auto b=std::min(raw.first+raw.pcm.size(),fence.original_first+fence.original_count);
                require(a==fence.original_first+copied,"virtual processing gather crosses unproved raw gap");
                std::copy(raw.pcm.begin()+(a-raw.first),raw.pcm.begin()+(b-raw.first),pcm.begin()+copied);copied+=b-a;
            },false);
            require(copied==fence.original_count,"virtual processing gather lacks original samples");
            consume(LazyPcmBlockView{fence.original_first,std::span<const float>{pcm}.first(copied)});
        });
    }
    template<class Consumer> void lazy_visit_processing(std::uint64_t first,std::uint64_t end,Consumer consume) const {
        lazy_visit_processing(first,end,consume,[](const auto&){return false;});
    }
    double lazy_carrier_restart(std::size_t frequency) {
        const auto revision=lazy_state()->processing->processing_fences().end();
        if(lazy_state()->carrier_restart_revision.at(frequency)==revision)return lazy_state()->carrier_restart_radius[frequency];
        namespace cert=detail::scheduled_acquisition_certificate;
        double maximum=lazy_state()->carrier_restart_radius[frequency],magnitude=lazy_state()->carrier_restart_magnitude[frequency];
        const auto seed=[&](std::uint64_t first){return std::polar(1.,static_cast<double>(std::remainder(
            static_cast<long double>(first)*tau*banks[frequency].frequency/config.sample_rate,static_cast<long double>(tau))));};
        const auto step=std::polar(1.,tau*banks[frequency].frequency/config.sample_rate);
        if(!lazy_state()->processing->difference_intervals().interval_count()) {
            lazy_state()->carrier_restart_revision[frequency]=revision;return maximum;
        }
        const auto retained=lazy_state()->processing->processing_fences().retained_first();
        const auto previous=lazy_state()->carrier_restart_revision[frequency];
        const auto begin=previous==std::numeric_limits<std::uint64_t>::max()?retained:std::max(previous,retained);
        lazy_state()->processing->processing_fences().visit(begin,revision-begin,[&](const auto& virtual_fence) {
            if(!lazy_state()->processing->difference_intervals().overlaps(virtual_fence.original_first,virtual_fence.original_count))return;
            lazy_visit_fences(virtual_fence.original_first,virtual_fence.original_first+virtual_fence.original_count,[&](const auto& raw) {
                const auto a=std::max(raw.first,virtual_fence.original_first);
                const auto b=std::min(raw.first+raw.count,virtual_fence.original_first+virtual_fence.original_count);
                auto old=seed(raw.first),now=seed(virtual_fence.original_first);
                for(auto n=raw.first;n<a;++n)old*=step;
                for(auto n=virtual_fence.original_first;n<a;++n)now*=step;
                const auto bound=detail::scheduled_carrier_restart::difference(old,now,step,b-a);
                if(!bound.valid){maximum=cert::infinity;return;}
                maximum=std::max(maximum,bound.radius);magnitude=std::max(magnitude,bound.magnitude);
            });
        });
        lazy_state()->carrier_restart_revision[frequency]=revision;lazy_state()->carrier_restart_radius[frequency]=maximum;
        lazy_state()->carrier_restart_magnitude[frequency]=magnitude;return maximum;
    }
    void lazy_prepare_block(const LazyPcmBlockView& block,std::size_t frequency,
            std::uint64_t consumer=std::numeric_limits<std::uint64_t>::max(),bool diagnostic=false,std::size_t consumers=1) {
        sample=block.first;
        require(frequency<banks.size(),"implicit acquisition frequency out of range");
        if(lazy_state()->prefix_frequency<banks.size())banks[lazy_state()->prefix_frequency].shared_prefix={};
        auto& bank=banks[frequency];auto& prefix=lazy_state()->prefix;
        auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(sample)*tau*bank.frequency/config.sample_rate,static_cast<long double>(tau))));
        const auto step=std::polar(1.,tau*bank.frequency/config.sample_rate);
        auto& use=diagnostic?lazy_state()->projection_diagnostic:lazy_state()->projection_acquisition;
        const detail::scheduled_original_projection::Provenance provenance{consumer,diagnostic};
        if(!diagnostic && consumers>1){lazy_state()->projection_shared_consumers+=consumers-1;
            lazy_state()->projection_shared_samples+=(consumers-1)*block.pcm.size();}
        const auto prepare_original=[&] {
            rx_cpu::Scope cpu_prefix(rx_cpu::Stage::mixing);
            use.original_samples+=block.pcm.size();
            prefix[0]={};
            for(std::size_t i=0;i<block.pcm.size();++i){const auto c=oscillator.real(),s=oscillator.imag(),x=static_cast<double>(block.pcm[i]);
                auto p=prefix[i];p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
                prefix[i+1]=p;oscillator*=step;}
        };
        // A bank may receive and return a temporary cohort workspace loan.
        // Keep optional storage below one quarter of its current budget, and
        // recreate at a changed cap before any hit; no cache row is borrowed.
        const auto projection_cap=std::min(lazy_state()->projection_cache_limit,budget/4);
        if(lazy_state()->original_projections && lazy_state()->original_projections->capacity_bytes()+128!=projection_cap)
            drop_lazy_projections(LazyState::ProjectionDrop::cap_change);
        if(!lazy_state()->original_projections && projection_cap>=128+LazyState::OriginalProjections::minimum_enabled_capacity()) {
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            constexpr auto reservation=sizeof(LazyState::OriginalProjections)+128+
                LazyState::OriginalProjections::transient_reservation;
            const auto used=sizeof(PatternCorrelator)+accounted_bytes;
            if(used<=budget && reservation<=budget-used) {
                try {lazy_state()->original_projections=std::make_unique<LazyState::OriginalProjections>(projection_cap-128);++lazy_state()->projection_objects;}
                catch(const std::bad_alloc&) {++lazy_state()->projection_skips;++lazy_state()->projection_retired_statistics.allocation_failures;}
                accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            } else ++lazy_state()->projection_retired_statistics.budget_skips;
        }
        if(lazy_state()->original_projections && lazy_state()->original_projections->enabled() &&
           detail::scheduled_acquisition_certificate::environment<double>()) {
            require(sample<=std::numeric_limits<std::uint64_t>::max()-search.input_sample_origin,
                "original projection cache absolute range overflow");
            std::uint64_t environment=static_cast<unsigned>(std::fegetround());
#if defined(__SSE2__)
            environment|=static_cast<std::uint64_t>(_mm_getcsr()&0xffc0U)<<32; // exception masks, rounding, FTZ and DAZ
#endif
            const detail::scheduled_original_projection::Key key{
                static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(lazy_state()->shared_capture?
                    static_cast<const void*>(lazy_state()->shared_capture.get()):static_cast<const void*>(lazy_state()))),
                lazy_state()->projection_pcm_generation,search.input_sample_origin+sample,sample,block.pcm.size(),config.sample_rate,
                std::bit_cast<std::uint64_t>(bank.frequency),std::bit_cast<std::uint64_t>(oscillator.real()),
                std::bit_cast<std::uint64_t>(oscillator.imag()),std::bit_cast<std::uint64_t>(step.real()),
                std::bit_cast<std::uint64_t>(step.imag()),environment};
            const auto rows=std::span<Projection>{prefix}.first(block.pcm.size()+1);
            detail::scheduled_original_projection::Provenance producer;
            if(lazy_state()->original_projections->copy(key,rows,&producer)) {
                ++lazy_state()->projection_hits;++use.hits;use.hit_samples+=block.pcm.size();
                if(producer.diagnostic!=diagnostic)++use.other_source_hits;
                if(producer.consumer!=std::numeric_limits<std::uint64_t>::max() &&
                   consumer!=std::numeric_limits<std::uint64_t>::max() && producer.consumer!=consumer &&
                   producer.diagnostic==diagnostic){++use.cross_consumer_hits;use.cross_consumer_hit_samples+=block.pcm.size();}
            } else {
                ++lazy_state()->projection_misses;prepare_original();
                accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                const auto used=sizeof(PatternCorrelator)+accounted_bytes;
                // Preserve modest headroom for original completion/publication
                // before investing otherwise free loan space in optional rows.
                constexpr std::size_t headroom=2*1024*1024;
                const auto available=used<=budget && budget-used>headroom?budget-used-headroom:0;
                // This exact full accounting was taken AFTER PatternCode and
                // prefix preparation. store() changes this cache alone, has no
                // callbacks, and may retain metadata or evict on failed insert.
                const auto previous_cache_bytes=lazy_state()->original_projections->working_bytes();
                if(!lazy_state()->original_projections->store(key,rows,available,provenance))++lazy_state()->projection_skips;
                else {
                    ++use.stores;
                    if(lazy_state()->projection_pending_rebuild){
                        ++lazy_state()->projection_rebuilds[static_cast<std::size_t>(*lazy_state()->projection_pending_rebuild)];
                        lazy_state()->projection_pending_rebuild.reset();
                    }
                }
                if(lazy_state()->serial_execution->cached_workspace_accounting) {
                    require(accounted_bytes>=previous_cache_bytes,"implicit cache accounting delta underflow");
                    accounted_bytes=accounted_bytes-previous_cache_bytes+lazy_state()->original_projections->working_bytes();
                    if(lazy_state()->serial_execution->verify_workspace_accounting)
                        require(accounted_bytes==working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation,
                            "original projection cache workspace delta differs from full accounting");
                } else accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                lazy_state()->projection_peak_bytes=std::max(lazy_state()->projection_peak_bytes,sizeof(LazyState::OriginalProjections)+128+lazy_state()->original_projections->working_bytes());
                work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            }
        } else prepare_original();
        // This view borrows the preexisting ORIGINAL scratch, never a cache row.
        bank.shared_prefix=std::span<const Projection>{prefix}.first(block.pcm.size()+1);
        lazy_state()->prefix_frequency=frequency;
    }
    // Two complete PatternCode symbol states retain exact current/carry
    // private preparation. Preparation is cancellable before detector mutation.
    void lazy_prepare_replay_patterns(std::size_t slot,std::uint64_t first,std::uint64_t end,std::stop_token stop) {
        auto& h=hypotheses[slot];code.set_stream_phase_samples(h.phase_lower);
        require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
            "implicit replay chip coordinate overflow");
        code.prepare_symbol(h.index*code.chips_per_symbol(),stop);
        if(first<end) {
            const auto source=scheduled_map()->locate(scheduled_tick(h,end-1)).source.distance(scheduled->anchors[slot]);
            const auto next=(static_cast<long double>(h.index)+1)*code.symbol_samples();
            if(source>=next) {
                require(h.index<std::numeric_limits<std::uint64_t>::max() &&
                    h.index+1<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
                    "implicit replay carry chip coordinate overflow");
                code.prepare_symbol((h.index+1)*code.chips_per_symbol(),stop);
                code.prepare_symbol(h.index*code.chips_per_symbol(),stop);
            }
        }
        cancelled(stop);
    }
    void lazy_replay_to(std::size_t slot,std::uint64_t end,std::stop_token stop) {
        rx_cpu::Scope cpu_refine(rx_cpu::Stage::scheduled_candidate_residual);
        const auto saved_sample=sample;struct Restore {Impl& self;std::uint64_t old;~Restore(){self.sample=old;}} restore{*this,saved_sample};
        auto& h=hypotheses[slot];const auto first=lazy_state()->replayed[slot];
        if(first>=end)return;
        require(first>=lazy_history_first(),
            "implicit acquisition replay history exhausted; required observations cannot be omitted");
        auto cursor=first;
        lazy_visit_processing(first,end,[&](const auto& block) {
            const auto block_end=block.first+block.pcm.size();
            if(block_end<=cursor || block.first>=end)return;
            require(block.first<=cursor,"implicit acquisition capture has an unproved discontinuity");
            const auto limit=std::min(end,block_end);
            cancelled(stop);lazy_prepare_replay_patterns(slot,cursor,limit,stop);
            // One original <=512 fence is an indivisible detector contraction.
            // Mutation and replay cursor publication never observe stop midway.
            try {
                lazy_prepare_block(block,h.frequency,lazy_state()->logical[slot],slot==0);
                cursor=accumulate_scheduled(h,slot,cursor,limit,code,{});
            } catch(...) {if(slot!=0)lazy_state()->commit_fault=std::current_exception();throw;}
            lazy_state()->replay_samples+=cursor-lazy_state()->replayed[slot];lazy_state()->replayed[slot]=cursor;
            detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::replay_fence,cursor);
        },[&](const auto& fence) {
            const auto block_end=fence.original_first+fence.original_count;
            if(block_end<=cursor || fence.original_first>=end)return false;
            require(fence.original_first<=cursor,"implicit acquisition capture has an unproved discontinuity");
            const auto limit=std::min(end,block_end);
            if(const auto* disable=std::getenv("DATAPUMP_SCHED_NULL_REPLAY"))
                if(std::string_view(disable)=="0")return false;
            // Leave unsupported completion/carry clipping to the original path.
            if(static_cast<long double>(limit)>std::ceil(clock_boundary(h,code.symbol_samples())))return false;
            cancelled(stop);
            // Exact exclusion for THIS candidate only. Raw capture, other
            // consumers and original processing fences remain retained. This
            // is the same inactive interval and advance as accumulate_scheduled.
            // Later carrier prefixes restart at their original physical fence;
            // no carrier time, source coordinate or observation is synthesized.
            const auto tick=scheduled_tick(h,cursor);
            const auto location=scheduled_map()->locate(tick);
            if(location.active)return false;
            const auto next=scheduled_map()->source_to_physical(location.source,false);
            const auto count=std::max(1.L,std::ceil(next.distance(tick)/h.rate));
            if(count<static_cast<long double>(limit-cursor))return false;
            const auto old_cursor=cursor;
            cursor=limit;lazy_state()->replay_samples+=cursor-lazy_state()->replayed[slot];lazy_state()->replayed[slot]=cursor;
            ++lazy_state()->null_replay_fences;lazy_state()->null_replay_samples+=cursor-old_cursor;
            detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::replay_fence,cursor);
            return true;
        });
        require(cursor==end,"implicit acquisition did not retain complete replay interval");
    }
    static std::size_t lazy_detached_replay_limit() {
        const auto* value=std::getenv("DATAPUMP_SCHED_REPLAY_BATCH");
        if(!value)return 0;
        if(std::string_view(value)=="4")return 4;
        if(std::string_view(value)=="16")return 16;
        return 0;
    }
    void lazy_clear_detached_replay_slot(std::size_t slot) {
        require(slot && slot<hypotheses.size() && lazy_state()->occupied[slot],"invalid detached replay owner");
        const auto registered=resident_slot(lazy_state()->logical[slot]);
        require(!registered || *registered!=slot,"detached replay cannot clear a registered owner");
        hypotheses[slot]={};chip_evidence[slot]={};
        scheduled->scheduled_carry[slot]={};scheduled->scheduled_carry_chips[slot]={};
        if(outer_presence){(*outer_evidence)[slot]={};scheduled->scheduled_carry_outer[slot]={};}
        scheduled->symbol_ends[slot]=std::numeric_limits<long double>::infinity();
        work.scheduled_template_constructions+=scheduled->template_uncached[slot];
        scheduled->template_uncached[slot]=0;lazy_state()->replayed[slot]=0;
        lazy_state()->occupied[slot]=0;lazy_state()->logical[slot]=std::numeric_limits<std::size_t>::max();
    }
    void lazy_drop_detached_replay() {
        if(!lazy_state()->detached_replay)return;
        for(auto& event:lazy_state()->detached_replay->events)
            if(event.slot!=std::numeric_limits<std::size_t>::max()) {
                lazy_clear_detached_replay_slot(event.slot);
                event.slot=std::numeric_limits<std::size_t>::max();
            }
        lazy_state()->detached_replay.reset();accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
    }
    bool lazy_evict_detached_replay() {
        if(!lazy_state()->detached_replay)return false;
        for(auto& event:lazy_state()->detached_replay->events)
            if(event.slot!=std::numeric_limits<std::size_t>::max()) {
                lazy_clear_detached_replay_slot(event.slot);
                event.slot=std::numeric_limits<std::size_t>::max();return true;
            }
        return false;
    }
    void lazy_accumulate_detached_replay(std::stop_token stop) {
        auto& batch=*lazy_state()->detached_replay;if(batch.ready)return;
        const auto saved_sample=sample;
        struct Restore {Impl& self;std::uint64_t old;~Restore(){self.sample=old;}} restore{*this,saved_sample};
        auto first=std::numeric_limits<std::uint64_t>::max(),end=std::uint64_t{0};
        for(std::size_t i=0;i<batch.count;++i){const auto& event=batch.events[i];
            first=std::min(first,lazy_state()->replayed[event.slot]);end=std::max(end,event.end);}
        bool clean_fence=true;batch.numeric_active=true;
        try {
            if(first<end) {
                require(first>=lazy_history_first(),"detached replay lacks original retained history");
                lazy_visit_processing(first,end,[&](const auto& block) {
                    const auto block_end=block.first+block.pcm.size();
                    std::array<std::size_t,LazyState::DetachedReplay::maximum_events> active{};std::size_t count=0;
                    for(std::size_t i=0;i<batch.count;++i){const auto& event=batch.events[i];
                        if(lazy_state()->replayed[event.slot]<std::min(block_end,event.end) && block.first<event.end)
                            active[count++]=i;}
                    if(!count)return;
                    // All potentially cancellable private preparation precedes any
                    // mutation in this bounded original processing fence.
                    cancelled(stop);
                    for(std::size_t j=0;j<count;++j){const auto& event=batch.events[active[j]];
                        require(block.first<=lazy_state()->replayed[event.slot],"detached replay crosses an unproved gap");
                        lazy_prepare_replay_patterns(event.slot,lazy_state()->replayed[event.slot],
                            std::min(event.end,block_end),stop);}
                    cancelled(stop);sample=block.first;clean_fence=false;
                    const auto private_lanes=batch.pulse?batch.pulse->counts.batched_lanes:0;
                    const auto private_rows=batch.pulse?batch.pulse->counts.consumed_samples:0;
                    // Stable local permutation changes numerical dispatch only.
                    // Each original carrier plane is built once for this fence.
                    std::sort(active.begin(),active.begin()+count,[&](auto a,auto b){
                        return hypotheses[batch.events[a].slot].frequency<hypotheses[batch.events[b].slot].frequency;});
                    for(std::size_t group=0;group<count;) {
                        const auto frequency=hypotheses[batch.events[active[group]].slot].frequency;
                        auto until=group+1;while(until<count &&
                            hypotheses[batch.events[active[until]].slot].frequency==frequency)++until;
                        const auto producer_slot=batch.events[active[group]].slot;
                        lazy_prepare_block(block,frequency,lazy_state()->logical[producer_slot],false,until-group);++lazy_state()->detached_prefixes;
                        const auto consume=[&](std::size_t j,const detail::ScheduledPrivateBatchView* prepared) {
                            const auto& event=batch.events[active[j]];auto& h=hypotheses[event.slot];
                            const auto old=lazy_state()->replayed[event.slot];
                            const auto cursor=accumulate_scheduled(h,event.slot,old,std::min(event.end,block_end),code,{},prepared);
                            lazy_state()->replay_samples+=cursor-old;lazy_state()->replayed[event.slot]=cursor;
                        };
                        if(batch.pulse)accumulate_private_batch_job(group,until,block_end,code,*batch.pulse,
                            [&](std::size_t j){return batch.events[active[j]].slot;},consume,{},false);
                        else for(auto j=group;j<until;++j)consume(j,nullptr);
                        group=until;
                    }
                    if(batch.pulse){lazy_state()->detached_private_lanes+=batch.pulse->counts.batched_lanes-private_lanes;
                        lazy_state()->detached_private_rows+=batch.pulse->counts.consumed_samples-private_rows;}
                    clean_fence=true;
                    ++lazy_state()->detached_blocks;
                    detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::replay_fence,block_end);
                });
            }
            for(std::size_t i=0;i<batch.count;++i)
                require(lazy_state()->replayed[batch.events[i].slot]==batch.events[i].end,
                    "detached replay ended before the original event fence");
            batch.ready=true;batch.numeric_active=false;
            // No prepared source/output borrow escapes numerical execution. Return
            // the large optional pulse loan before mandatory serial allocations.
            batch.pulse.reset();accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::detached_prepared,batch.count);
        } catch(...) {
            batch.numeric_active=false;
            if(stop.stop_requested() && clean_fence)throw;
            // A speculative future lane may fail before serial thresholds would
            // reject it. Discard partial scratch and let ORIGINAL replay decide
            // whether that event actually needs authoritative numerical work.
            lazy_drop_detached_replay();return;
        }
        cancelled(stop);
    }
    void lazy_prepare_detached_replay(std::size_t id,std::uint64_t index,std::stop_token stop) {
        using Batch=LazyState::DetachedReplay;
        // Selected guard screening precedes ORIGINAL replay. Raw-only
        // speculation would replay candidates that this proof can reject.
        // Keep the measured detached backend for other execution geometries.
        if(lazy_colored_demand_enabled())return;
        if(lazy_state()->detached_replay) {
            const auto& batch=*lazy_state()->detached_replay;
            if(batch.block_first==lazy_state()->committing_block->first &&
               std::pair{id,index}<=std::pair{batch.through_id,batch.through_index}) {
                lazy_accumulate_detached_replay(stop);return;
            }
            lazy_drop_detached_replay();
        }
        const auto requested=lazy_detached_replay_limit();
        if(!requested || drift_sections!=1 || differential_window || alternate_groups ||
           guard_chains || options.amplitude_only || options.raw_reference)return;
        const auto free=static_cast<std::size_t>(std::count(lazy_state()->occupied.begin(),lazy_state()->occupied.end(),0));
        const auto limit=std::min(requested,free);if(limit<2)return;
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        const auto minimum=sizeof(Batch)+Batch::caller_bytes;
        if(sizeof(PatternCorrelator)>budget || accounted_bytes>budget-sizeof(PatternCorrelator) ||
           minimum>budget-sizeof(PatternCorrelator)-accounted_bytes)return;
        std::unique_ptr<Batch> prepared;
        try {prepared=std::make_unique<Batch>();}catch(const std::bad_alloc&){return;}
        prepared->block_first=lazy_state()->committing_block->first;
        prepared->block_end=prepared->block_first+lazy_state()->committing_block->count;
        prepared->through_id=id;prepared->through_index=index;
        lazy_state()->detached_replay=std::move(prepared);
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        // Only bounded stream cursors are copied; DueRange contains large
        // screened rows and is never duplicated for speculative lookahead.
        struct Peek {std::size_t range=0,first=0,id=0;};
        std::array<Peek,Batch::maximum_lookahead> cursors{};std::size_t cursor_count=0;
        std::optional<std::size_t> previous_id;
        require(trials<std::numeric_limits<std::uint64_t>::max(),"pattern trial counter overflow");
        const auto t=static_cast<double>(trials+1);
        const auto earliest_threshold=-std::log(search.false_alarm_probability)+std::log(t)+std::log(t+1)+std::log(2.);
        try {
            auto& batch=*lazy_state()->detached_replay;
            for(std::size_t inspected=0;inspected<Batch::maximum_lookahead && batch.count<limit;++inspected) {
                cancelled(stop);std::size_t selected=lazy_state()->due_ranges.size(),selected_cursor=cursor_count;
                std::size_t selected_first=0,selected_id=0;
                for(std::size_t r=0;r<lazy_state()->due_ranges.size();++r){const auto& range=lazy_state()->due_ranges[r];
                    auto first=range.first,candidate=range.id;std::size_t existing=cursor_count;
                    for(std::size_t j=0;j<cursor_count;++j)if(cursors[j].range==r){
                        first=cursors[j].first;candidate=cursors[j].id;existing=j;break;}
                    if(first==range.end)continue;
                    if(selected==lazy_state()->due_ranges.size() || candidate<selected_id ||
                       (candidate==selected_id && range.index<lazy_state()->due_ranges[selected].index)) {
                        selected=r;selected_cursor=existing;selected_first=first;selected_id=candidate;
                    }
                }
                if(selected==lazy_state()->due_ranges.size())break;
                const auto& range=lazy_state()->due_ranges[selected];
                // Multiple source events for one ID depend on the earlier
                // lifecycle/carry decision. Keep only its first event here.
                if(previous_id && *previous_id==selected_id)break;
                previous_id=selected_id;
                const auto lane=lazy_family_lane(range.family,selected_first);
                batch.through_id=lane.id;batch.through_index=range.index;
                if(selected_cursor==cursor_count){require(cursor_count<cursors.size(),"detached cursor bound exhausted");
                    cursors[cursor_count++]={selected,selected_first,selected_id};}
                auto& cursor=cursors[selected_cursor];++cursor.first;
                if(cursor.first<range.end)cursor.id=lazy_family_lane(range.family,cursor.first).id;
                if(resident_slot(lane.id) || lazy_cold_upper(lane,range)<earliest_threshold)continue;
                auto it=std::find(lazy_state()->occupied.begin(),lazy_state()->occupied.end(),0);
                require(it!=lazy_state()->occupied.end(),"detached replay lost its free numerical slot");
                const auto slot=static_cast<std::size_t>(it-lazy_state()->occupied.begin());
                try {lazy_reset_slot(slot,lane,range.index,false);}
                catch(...) {
                    // reset may fail before publishing occupied. This was a
                    // verified free, unregistered owner; clean its partial init
                    // before any Event capability becomes visible.
                    lazy_state()->occupied[slot]=1;lazy_state()->logical[slot]=lane.id;
                    lazy_clear_detached_replay_slot(slot);throw;
                }
                auto& event=batch.events[batch.count++];event.id=lane.id;event.index=range.index;event.slot=slot;
                event.first=lazy_state()->replayed[slot];event.end=lazy_state()->catalog->due(lane,range.index,*lazy_state()->cursor);
            }
            if(batch.count<2){for(std::size_t j=0;j<batch.count;++j){auto& event=batch.events[j];
                    lazy_clear_detached_replay_slot(event.slot);event.slot=std::numeric_limits<std::size_t>::max();}
                batch.count=0;batch.ready=true;return;}
            const auto pulse_bytes=sizeof(detail::ScheduledPrivateBatchScratch)+detail::ScheduledPrivateBatchScratch::callee_bytes;
            const auto* disable=std::getenv("DATAPUMP_SCHED_REPLAY_PRIVATE");
            if((!disable || std::string_view(disable)!="0") && shaped && code.chip_samples()<=1023 &&
               code.symbol_samples()>=18*code.chip_samples() &&
               detail::ScheduledPrivateBatchScratch::Tile::arithmetic_supported() &&
               pulse_bytes<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                try {batch.pulse=std::make_unique<detail::ScheduledPrivateBatchScratch>();}
                catch(const std::bad_alloc&) {} // Exact scalar construction remains available.
            }
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"detached replay exceeds workspace");
            work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            ++lazy_state()->detached_batches;lazy_state()->detached_lanes+=batch.count;
        } catch(...) {lazy_drop_detached_replay();if(stop.stop_requested())throw;return;}
        lazy_accumulate_detached_replay(stop);
    }
    LazyState::DetachedReplay::Event* lazy_detached_event(std::size_t id,std::uint64_t index) {
        if(!lazy_state()->detached_replay || !lazy_state()->detached_replay->ready)return nullptr;
        for(std::size_t j=0;j<lazy_state()->detached_replay->count;++j){auto& event=lazy_state()->detached_replay->events[j];
            if(event.id==id && event.index==index && event.slot!=std::numeric_limits<std::size_t>::max())return &event;}
        return nullptr;
    }
    std::optional<std::size_t> lazy_adopt_detached_replay(std::size_t id,std::uint64_t index) {
        auto* event=lazy_detached_event(id,index);if(!event)return {};
        const auto source=event->slot;auto& shadow=hypotheses[source];
        if(const auto present=resident_slot(id)) {
            auto& h=hypotheses[*present];const auto& a=scheduled->anchors[*present];const auto& b=scheduled->anchors[source];
            const bool pristine=h.index==index && h.origin==shadow.origin && h.rate==shadow.rate &&
                h.phase_lower==shadow.phase_lower && h.phase_upper==shadow.phase_upper && h.frequency==shadow.frequency &&
                h.rate_index==shadow.rate_index && h.tone_bank_base==shadow.tone_bank_base &&
                a.sample==b.sample && a.fraction==b.fraction && scheduled->observed_begin[*present]==scheduled->observed_begin[source] &&
                scheduled->first_full_indices[*present]==scheduled->first_full_indices[source] &&
                scheduled->end_indices[*present]==index && scheduled->symbol_ends[*present]==event->end &&
                lazy_state()->replayed[*present]==event->first && !h.admitted && h.burst.bits.empty() &&
                !h.fits[0].count && !h.fits[1].count && !scheduled->scheduled_carry[*present].fits[0].count &&
                !scheduled->scheduled_carry[*present].fits[1].count &&
                !chip_evidence[*present][0].cell.count && !chip_evidence[*present][1].cell.count &&
                !chip_evidence[*present][0].projected_rank && !chip_evidence[*present][1].projected_rank &&
                !scheduled->scheduled_carry_chips[*present][0].cell.count && !scheduled->scheduled_carry_chips[*present][1].cell.count &&
                !scheduled->scheduled_carry_chips[*present][0].projected_rank && !scheduled->scheduled_carry_chips[*present][1].projected_rank &&
                (!outer_presence || (!(*outer_evidence)[*present].initialized && !scheduled->scheduled_carry_outer[*present].initialized));
            if(!pristine){lazy_clear_detached_replay_slot(source);event->slot=std::numeric_limits<std::size_t>::max();return {};}
            // Preserve timing/burst/admission/last-accepted state of a neighbour
            // materialized by an earlier ORIGINAL serial decision.
            h.fits=shadow.fits;h.observed_start=shadow.observed_start;
            scheduled->scheduled_carry[*present]=scheduled->scheduled_carry[source];
            chip_evidence[*present]=chip_evidence[source];scheduled->scheduled_carry_chips[*present]=scheduled->scheduled_carry_chips[source];
            if(outer_presence){(*outer_evidence)[*present]=(*outer_evidence)[source];
                scheduled->scheduled_carry_outer[*present]=scheduled->scheduled_carry_outer[source];}
            lazy_state()->replayed[*present]=event->end;
            lazy_clear_detached_replay_slot(source);event->slot=std::numeric_limits<std::size_t>::max();return present;
        }
        room_for(sizeof(std::pair<const std::size_t,std::size_t>)+64);
        // Mandatory allocation can evict optional storage. Reacquire this
        // capability after room_for instead of retaining its Event pointer.
        event=lazy_detached_event(id,index);if(!event)return {};
        const auto adopted=event->slot;
        const auto [unused,inserted]=lazy_state()->slots.emplace(id,adopted);(void)unused;
        require(inserted,"detached replay registration changed owner identity");
        event->slot=std::numeric_limits<std::size_t>::max();
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;return adopted;
    }
    std::uint64_t lazy_index_at(const LazyState::Catalog::Lane& lane,std::uint64_t physical) {
        // Monotone inverse gives a close lower index; clock-cell END is the
        // authoritative completion fence and may lag the central inverse.
        auto index=lane.index;
        const auto tick=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(lane.phase_lower).
            add_signed((static_cast<long double>(physical)-lane.origin)*lane.rate);
        const auto source=scheduled_map()->locate(tick).source.distance(lane.anchor);
        if(source>0){const auto approximate=std::floor(source/code.symbol_samples());
            require(approximate<std::numeric_limits<std::uint64_t>::max(),"implicit symbol index overflow");
            index=std::max(index,static_cast<std::uint64_t>(approximate));}
        while(index>lane.index && lazy_state()->catalog->due(lane,index-1,*lazy_state()->cursor)>physical)--index;
        while(lazy_state()->catalog->due(lane,index,*lazy_state()->cursor)<=physical && physical!=0){
            require(index<std::numeric_limits<std::uint64_t>::max(),"implicit symbol index overflow");++index;}
        return index;
    }
    LazyState::Catalog::Lane lazy_family_lane(std::size_t family,std::size_t origin) {
        return lazy_state()->catalog->describe_family(family,origin,lazy_execution().cursor);
    }
    template<class Predicate> static std::size_t lazy_first_true(std::size_t first,std::size_t end,Predicate predicate) {
        while(first<end){const auto mid=first+(end-first)/2;if(predicate(mid))end=mid;else first=mid+1;}
        return first;
    }
    long double lazy_due_ranges(std::uint64_t first,std::uint64_t end,std::stop_token stop) {
        rx_cpu::Scope cpu_calendar(rx_cpu::Stage::calendar);
        lazy_state()->due_ranges.clear();accounted_bytes=working_bytes()+unallocated_history_bytes();auto next=std::numeric_limits<long double>::infinity();
        const auto add=[&](LazyState::DueRange event){
            if(lazy_state()->due_ranges.size()==lazy_state()->due_ranges.capacity()){
                const auto old=lazy_state()->due_ranges.capacity();const auto capacity=std::max<std::size_t>(16,old*2);
                room_for(capacity*sizeof(LazyState::DueRange));lazy_state()->due_ranges.reserve(capacity);
                accounted_bytes+=(lazy_state()->due_ranges.capacity()-old)*sizeof(LazyState::DueRange);}
            lazy_state()->due_ranges.push_back(event);
        };
        for(std::size_t family=0;family<lazy_state()->catalog->families().size();++family){
            cancelled(stop);const auto& f=lazy_state()->catalog->families()[family];
            for(std::size_t r=f.range_first;r<f.range_first+f.range_count;++r){
                const auto range=lazy_state()->catalog->ranges()[r];if(range.first==range.end)continue;
                const auto a=lazy_family_lane(family,range.first),b=lazy_family_lane(family,range.end-1);
                const auto old_min=lazy_index_at(b,first),new_max=lazy_index_at(a,end);
                require(new_max>=old_min && new_max-old_min<=65536,
                    "implicit acquisition event aperture exceeds bounded source-index traversal");
                // For one rate/phase family the completed source index is
                // nonincreasing with original arrival origin. Binary-search
                // exact original index strata, including qualified holes via
                // the catalogue's separately retained contiguous ranges.
                for(auto index=old_min;index<new_max;++index){
                    const auto begin=lazy_first_true(range.first,range.end,[&](std::size_t i){return lazy_index_at(lazy_family_lane(family,i),first)<=index;});
                    const auto until=lazy_first_true(begin,range.end,[&](std::size_t i){return lazy_index_at(lazy_family_lane(family,i),end)<=index;});
                    if(begin<until){const auto lane=lazy_family_lane(family,begin);add({family,begin,until,lane.id,index});}
                }
                // Find the next completion over every original lane without
                // retaining a next-index/event object per lane. Within each
                // fixed source-index stratum, due is affine and increasing in
                // origin, so its first retained origin gives the minimum.
                const auto new_min=lazy_index_at(b,end);
                require(new_max>=new_min && new_max-new_min<=65536,
                    "implicit acquisition future aperture exceeds bounded source-index traversal");
                for(auto index=new_min;;++index){
                    const auto begin=lazy_first_true(range.first,range.end,[&](std::size_t i){return lazy_index_at(lazy_family_lane(family,i),end)<=index;});
                    const auto until=lazy_first_true(begin,range.end,[&](std::size_t i){return lazy_index_at(lazy_family_lane(family,i),end)<index;});
                    if(begin<until){const auto lane=lazy_family_lane(family,begin);next=std::min(next,
                        static_cast<long double>(lazy_state()->catalog->due(lane,index,*lazy_state()->cursor)));}
                    if(index==new_max)break;
                }
            }
        }
        // Enumeration prepares a future frontier; the owner publishes it
        // only after every original consumer in this block commits.
        return next;
    }
    template<class Consumer> void lazy_for_each_due(Consumer consume) {
        // Merge O(family/index-strata) streams. This retains exact original
        // (global lane ID, source symbol) order inside the original input block;
        // it is not a list or priority queue proportional to all hypotheses.
        while(true){auto selected=lazy_state()->due_ranges.end();
            for(auto it=lazy_state()->due_ranges.begin();it!=lazy_state()->due_ranges.end();++it){
                if(it->first==it->end)continue;
                if(selected==lazy_state()->due_ranges.end() || it->id<selected->id ||
                   (it->id==selected->id && it->index<selected->index))selected=it;}
            if(selected==lazy_state()->due_ranges.end())break;
            const auto lane=lazy_family_lane(selected->family,selected->first);const auto index=selected->index;
            consume(lane,index,*selected);
            // A cancelled consumer leaves this exact event selected. Advance
            // is joined to its successful original event commit.
            ++selected->first;if(selected->first<selected->end)selected->id=lazy_family_lane(selected->family,selected->first).id;
            detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::event_committed,lazy_state()->history_generation);
        }
    }
    struct LazyEnergy {
        detail::scheduled_acquisition_certificate::Interval interval;
        std::uint64_t count=0;
    };
    LazyEnergy lazy_energy(std::uint64_t first,std::uint64_t end) const {
        namespace cert=detail::scheduled_acquisition_certificate;
        LazyEnergy result;if(end<=first)return result;
        require(first>=lazy_history_first() && end<=lazy_state()->block_end,
            "implicit acquisition usable energy history is incomplete");
        const auto g=cert::gamma(block_samples,std::numeric_limits<double>::epsilon()/2);
        lazy_visit_blocks(first,end,[&](const auto& b) {
            require(b.first<=first,"implicit acquisition energy crosses unproved capture gap");
            const auto a=static_cast<std::size_t>(first-b.first);
            const auto until=static_cast<std::size_t>(std::min(end,b.first+b.pcm.size())-b.first);
            const auto value=b.energy[until]-b.energy[a];
            const auto radius=cert::add(cert::divide(cert::mul(g,cert::add(b.energy[until],b.energy[a])),cert::down(1-g)),cert::ulp(value));
            auto interval=cert::Interval::disk(value,radius);interval.lo=std::max(0.,interval.lo);
            result.interval=cert::plus(result.interval,interval);result.count+=until-a;first=b.first+until;
        });
        require(first==end,"implicit acquisition energy capture ended early");return result;
    }
    LazyEnergy lazy_usable_energy(const LazyState::Catalog::Lane& lane,std::uint64_t index) {
        namespace cert=detail::scheduled_acquisition_certificate;
        LazyEnergy result;const auto source_first=lane.anchor.add_samples(index*code.symbol_samples());
        const auto source_end=source_first.add_samples(code.symbol_samples());auto at=source_first;
        const auto phase=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(lane.phase_lower);
        std::size_t pieces=0;
        while(at.distance(source_end)<0){
            require(++pieces<=detail::scheduled_acquisition_tile::maximum_fragments,"implicit usable run bound exceeded");
            const auto tick=scheduled_map()->source_to_physical(at,false);const auto location=scheduled_map()->locate(tick);
            require(location.active,"implicit source inverse did not enter active group");
            const auto group_remaining=SchedHopCoordinate{location.physical_end,0}.distance(tick);
            const auto length=std::min(group_remaining,source_end.distance(at));
            const auto right_tick=tick.add_samples(length);
            const auto left=std::max(tick.distance(phase),SchedHopCoordinate{location.rise_begin,0}.distance(phase));
            const auto right=std::min(right_tick.distance(phase),SchedHopCoordinate{location.fall_end,0}.distance(phase));
            if(right>left){
                auto first=static_cast<std::uint64_t>(std::max(0.L,std::ceil(lane.origin+left/lane.rate)));
                auto end=static_cast<std::uint64_t>(std::max(0.L,std::ceil(lane.origin+right/lane.rate)));
                end=std::min(end,lazy_state()->block_end);
                const auto usable=[&](std::uint64_t p){const auto t=phase.add_signed((static_cast<long double>(p)-lane.origin)*lane.rate);
                    const auto l=scheduled_map()->locate(t);const auto source=l.source.distance(lane.anchor)-static_cast<long double>(index)*code.symbol_samples();
                    return l.active && source>=0 && source<code.symbol_samples() && SchedHopSourceMap::gain(l,t)>0;};
                for(unsigned n=0;n<3 && first<end && !usable(first);++n)++first;
                for(unsigned n=0;n<3 && first<end && !usable(end-1);++n)--end;
                require(first>=end || (usable(first)&&usable(end-1)),"implicit usable endpoint rounding is unsupported");
                if(first<end){const auto value=lazy_energy(first,end);result.interval=cert::plus(result.interval,value.interval);result.count+=value.count;}
            }
            require(length>0,"implicit source-energy traversal did not advance");at=at.add_samples(length);
        }
        return result;
    }
    static bool lazy_reference_environment() {
#if defined(__GLIBC__) && defined(__GNUC__) && __GNUC__>=14 && defined(__x86_64__) && !defined(__FAST_MATH__)
        return detail::scheduled_acquisition_certificate::coordinate_environment();
#else
        return false; // Local GCC14/glibc reference contract; unsupported host refines.
#endif
    }
    void drop_lazy_projections(LazyState::ProjectionDrop reason=LazyState::ProjectionDrop::allocation_pressure) {
        if(!lazy_state())return;
        if(lazy_state()->original_projections) {
            const auto rows=lazy_state()->original_projections->entries();const auto at=static_cast<std::size_t>(reason);
            ++lazy_state()->projection_drops[at];lazy_state()->projection_drop_rows[at]+=rows;
            lazy_state()->projection_retired_statistics+=lazy_state()->original_projections->statistics();
            // An empty recreated object does not erase the causal identity of
            // the last actual row drop. Rebuild means first committed new row.
            if(rows)lazy_state()->projection_pending_rebuild=reason;
            lazy_state()->original_projections.reset(); // cache hits copied into stable prefix scratch
        }
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
    }
    void drop_lazy_templates() {
        if(!lazy_state() || lazy_state()->screens_running)return;
        lazy_state()->serial_execution->drop_templates();
        for(auto& execution:lazy_state()->executions)execution->drop_templates();
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
    }
    void drop_lazy_plans() {
        if(!lazy_state() || lazy_state()->screens_running)return;
        lazy_state()->serial_execution->drop_plans();
        // Contexts are optional. Required aliases, capture and processing
        // journal remain independently retained when the loan is withdrawn.
        decltype(lazy_state()->executions){}.swap(lazy_state()->executions);
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
    }
    template<class T> const detail::scheduled_acquisition_tile::Plan<T>& lazy_plan(
            detail::scheduled_acquisition_tile::Shape shape,std::stop_token stop) {
        auto& execution=lazy_execution();
        execution.thaw_numeric_bytes(); // Includes direct plan calls before ScreenPin.
        auto& plans=[&]() -> auto& {if constexpr(std::is_same_v<T,float>)return execution.fp32_plans;else return execution.fp64_plans;}();
        for(const auto& plan:plans)if(plan->shape.start_span==shape.start_span && plan->shape.maximum_fragment==shape.maximum_fragment) {
            ++execution.plan_hits;return *plan;
        }
        ++execution.plan_misses;
        const auto plan_reservation=shape.transform_size()*sizeof(std::complex<double>)+16384;
        if(plans.size()==4 || execution.numeric_bytes()+execution.scratch_reservation+plan_reservation>execution.available_bytes)
            execution.drop_plans();
        require(execution.numeric_bytes()+execution.scratch_reservation+plan_reservation<=execution.available_bytes,
            "implicit exact FFT plan lacks reserved worker workspace");
        if(plans.capacity()<4)plans.reserve(4);
        plans.push_back(std::make_unique<detail::scheduled_acquisition_tile::Plan<T>>(shape,stop));
        require(execution.numeric_bytes()+execution.scratch_reservation<=execution.available_bytes,
            "implicit exact FFT plan cache exceeds reserved worker workspace");
        return *plans.back();
    }
    LazyState::Catalog::Lane lazy_screen_reference(const LazyState::DueRange& range) {
        const auto target=lazy_family_lane(range.family,range.first);
        auto reference=target;
        if(config.search_arithmetic!=SearchArithmetic::fp64) {
            for(std::size_t pair=0;pair<lazy_state()->catalog->pairs().size();++pair) {
                if(lazy_state()->catalog->pairs()[pair].rate!=1)continue;
                const auto id=lazy_state()->catalog->rank(pair,target.origin_index,target.phase_lower);
                if(!id)continue;
                const auto central=lazy_state()->catalog->describe(*id,lazy_execution().cursor);
                if(central.anchor.distance(target.anchor)!=0)continue;
                reference=central;reference.frequency=target.frequency;break;
            }
        }
        return reference;
    }
    static bool lazy_colored_requested() {
        return detail::scheduled_acquisition_policy::colored_requested();
    }
    static constexpr double lazy_colored_carrier_cap=1e-6;
    bool lazy_colored_eligible(const LazyState::Catalog::Lane& reference,
            const LazyState::DueRange& range,std::span<const std::size_t> starts,std::size_t origin_stride=1) {
        // Exact central-clock source arithmetic. Clock proxies do not acquire
        // this proof from rounded displacement or a numerical count bound.
        if(!lazy_colored_requested() || config.search_arithmetic==SearchArithmetic::fp64 ||
           options.scheduled_raw_score_reference || options.raw_reference || options.amplitude_only ||
           drift_sections!=1 || differential_window || !lazy_reference_environment() ||
           std::numeric_limits<long double>::digits<64 || reference.rate!=1 ||
           std::trunc(reference.origin)!=reference.origin || std::abs(reference.origin)>=0x1p31L ||
           reference.anchor.fraction!=0 || reference.anchor.sample>=0x10000000000000ULL ||
           scheduled->epoch_sample>=0x10000000000000ULL ||
           reference.phase_lower>=0x10000000000000ULL-scheduled->epoch_sample ||
           !code.chip_samples() || code.chip_samples()>1023 ||
           range.index>=0x10000000000000ULL/code.symbol_samples() ||
           code.symbol_samples()>=0x10000000000000ULL ||
           scheduled->curves || scheduled->float_curves || scheduled->moments_eligible ||
           scheduled_affine || !scheduled->moments.empty())return false;
        if(range.end<=range.first || !origin_stride || starts.empty() || starts.size()!=1+(range.end-range.first-1)/origin_stride)return false;
        for(std::size_t j=0;j<starts.size();++j){const auto lane=lazy_family_lane(range.family,range.first+j*origin_stride);
            if(lane.rate!=1 || lane.origin!=reference.origin+starts[j] ||
               std::trunc(lane.origin)!=lane.origin || std::abs(lane.origin)>=0x1p31L ||
               lane.phase_lower!=reference.phase_lower || lane.anchor.fraction!=0 ||
               lane.anchor.distance(reference.anchor)!=0)return false;}
        return true;
    }
    bool lazy_colored_proxy_eligible(const LazyState::Catalog::Lane& reference,
            LazyState::DueRange& range,std::span<const std::size_t> starts,long double displacement,std::size_t origin_stride=1) {
        range.colored_gate=1;range.colored_lattice_margin=0;
        if(!lazy_colored_requested() || config.search_arithmetic==SearchArithmetic::fp64 ||
           options.scheduled_raw_score_reference || options.raw_reference || options.amplitude_only ||
           drift_sections!=1 || differential_window || !lazy_reference_environment() ||
           std::numeric_limits<long double>::digits<64 || reference.rate!=1 ||
           std::abs(reference.origin)>=0x1p31L || reference.anchor.sample>=0x10000000000000ULL ||
           scheduled->epoch_sample>=0x10000000000000ULL ||
           reference.phase_lower>=0x10000000000000ULL-scheduled->epoch_sample ||
           !code.chip_samples() || code.chip_samples()>1023 ||
           range.index>=0x10000000000000ULL/code.symbol_samples() || code.symbol_samples()>=0x10000000000000ULL ||
           scheduled->curves || scheduled->float_curves || scheduled->moments_eligible ||
           scheduled_affine || !scheduled->moments.empty() || starts.empty() || range.end<=range.first || !origin_stride ||
           starts.size()!=1+(range.end-range.first-1)/origin_stride)return false;
        range.colored_gate=2;
        if(!std::isfinite(displacement) || displacement<=0 || displacement>=.001L)return false;
        for(std::size_t j=0;j<starts.size();++j){const auto lane=lazy_family_lane(range.family,range.first+j*origin_stride);
            if(std::abs(lane.origin)>=0x1p31L || lane.phase_lower!=reference.phase_lower ||
               lane.anchor.distance(reference.anchor)!=0)return false;}
        // Exact-real central physical ticks share this fractional lattice.
        // Integer physical edges include frame/active/useful/ramp boundaries.
        // A strict margin, including the executed-source producer reserve in
        // displacement, encloses central subtraction/add_signed, ORIGINAL
        // piece restart/addition and chip division rounding as well as clock
        // drift. It proves equal ORIGINAL masks. Per-row SOURCE margins
        // below additionally prove equal window/chip-owner memberships.
        const auto phase=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(reference.phase_lower);
        const auto tick=phase.add_signed(-reference.origin);
        const auto margin=std::min(tick.fraction,1-tick.fraction);
        range.colored_lattice_margin=static_cast<double>(margin);range.colored_gate=3;
        if(!(margin>displacement))return false;
        range.colored_gate=4;return true;
    }
    bool lazy_colored_proxy_rows(const LazyState::Catalog::Lane& reference,const LazyState::DueRange& range,
            std::span<const std::array<Complex,2>> rows,std::span<const long double> source,std::span<const double> gain,
            std::span<const std::uint8_t> active,std::span<const std::uint8_t> mask,
            std::span<detail::scheduled_cold_colored::ProxyRow> result,long double displacement,
            double qmax,double pulse_slope,double gate_slope,double jump) const {
        namespace cert=detail::scheduled_acquisition_certificate;
        if(source.size()!=rows.size() || source.size()!=result.size() || source.size()!=gain.size() || source.size()!=active.size() || source.size()!=mask.size())return false;
        const auto delta=cert::add(cert::up(static_cast<double>(displacement)),
            cert::mul(2*std::numeric_limits<double>::epsilon(),cert::add(1,code.symbol_samples())));
        const auto shape=cert::mul(delta,cert::divide(34*pulse_slope,code.chip_samples()));
        const auto& plan=*config.scheduled_hopping;
        const auto group=scheduled_map()->source_samples_per_group();
        if(group>0x10000000000000ULL)return false;
        const auto left=std::ceil(static_cast<long double>(config.sample_rate)*plan.relative_clock_region_seconds());
        const auto right=std::ceil(static_cast<long double>(config.sample_rate)*plan.relative_clock_region_seconds()+
            static_cast<long double>(config.sample_rate)*plan.audio_error_seconds);
        const auto ramp=std::max(1.L,std::ceil(static_cast<long double>(config.sample_rate)*plan.ramp_seconds));
        const auto base=group?(reference.anchor.sample%group+(range.index*code.symbol_samples())%group)%group:0;
        // Enclose source->double, pulse-table interpolation, radial clipping,
        // smoothstep gain and the original q*gain association. The producer
        // is restricted to finite local coordinates below 2^52; displacement
        // separately includes the larger executed long-double source reserve.
        const auto fp=cert::mul(2048*std::numeric_limits<double>::epsilon(),cert::add(1,qmax));
        for(std::size_t i=0;i<source.size();++i) {
            auto& row=result[i];row={};
            if(!active[i]){if(mask[i])return false;continue;}
            if(!std::isfinite(source[i]) || !std::isfinite(gain[i]) || gain[i]<0 || gain[i]>1)return false;
            if(!detail::scheduled_cold_colored::fractional_membership_margin(source[i],displacement))return false;
            if(!mask[i])continue;
            if(!(gain[i]>0) || source[i]<0 || source[i]>=code.symbol_samples())return false;
            const auto owner=std::floor(source[i]/code.chip_samples());
            if(owner<0 || owner>=code.chips_per_symbol())return false;
            row.owner=static_cast<std::uint64_t>(owner);
            auto local_shape=shape;
            const auto edge=source[i]/code.chip_samples()-.5L;
            if(std::abs(edge-std::round(edge))*code.chip_samples()<=static_cast<long double>(delta)+1e-10L)
                local_shape=cert::add(local_shape,jump);
            const auto tail=code.symbol_samples()%code.chip_samples();
            if(tail){const auto center=static_cast<long double>(code.symbol_samples()-tail)+.5L*tail;
                if(std::min(std::abs(source[i]-center-8.L*code.chip_samples()),std::abs(source[i]-center+8.L*code.chip_samples()))<=static_cast<long double>(delta)+1e-10L)
                    local_shape=cert::add(local_shape,jump);}
            double local_slope=0;
            if(gain[i]!=1 || group) {
                if(!group)return false;
                // Keep the fractional part separate through integer modular
                // reduction; a large group/source base must not erase it.
                const auto whole=std::floor(source[i]);
                const auto modulus=static_cast<std::int64_t>(group);
                auto integer=(static_cast<std::int64_t>(whole)%modulus+static_cast<std::int64_t>(base))%modulus;
                if(integer<0)integer+=modulus;
                const auto fraction=(source[i]-whole)+reference.anchor.fraction;
                const auto carry=std::floor(fraction);
                integer=(integer+static_cast<std::int64_t>(carry))%modulus;
                const auto within=static_cast<long double>(integer)+(fraction-carry);
                const auto modular=cert::up(static_cast<double>(64*std::numeric_limits<long double>::epsilon()*(1.L+group+code.symbol_samples())));
                if(gain[i]==1) {
                    // Smoothstep can round to 1 just inside a ramp. Prove
                    // actual flat geometry before assigning zero slope.
                    if(!(within>=left+ramp+modular && within<=group-right-ramp-modular))local_slope=gate_slope;
                }else {
                long double unit;
                if(within>=left && within<=left+ramp)unit=(within-left)/ramp;
                else if(within>=group-right-ramp && within<=group-right)unit=(group-right-within)/ramp;
                else return false;
                const auto t=static_cast<double>(unit);
                const auto radius=cert::add(cert::divide(cert::add(delta,modular),static_cast<double>(ramp)),32*std::numeric_limits<double>::epsilon());
                const auto lo=std::max(0.,cert::down(t-radius)),hi=std::min(1.,cert::up(t+radius));
                const auto a=cert::mul(hi,hi),b=cert::mul(cert::up(1-lo),cert::up(1-lo));
                local_slope=std::min(gate_slope,cert::divide(cert::mul(30,cert::mul(a,b)),static_cast<double>(ramp)));
                }
            }
            // Recover the pre-gain limited coefficient from the ACTUAL native
            // row. The independent executed-cap bound remains a second upper.
            const auto norm=std::max(cert::norm(rows[i][0]),cert::norm(rows[i][1]));
            const auto cap=std::min(cert::add(qmax,fp),cert::add(cert::add(cert::divide(norm,gain[i]),local_shape),fp));
            const auto gate=cert::mul(cert::mul(delta,local_slope),cap);
            row.residual=cert::add(cert::add(cert::mul(gain[i],local_shape),gate),fp);
        }
        return true;
    }
    void lazy_colored_proxy_residual(LazyState::DueRange& range,
            std::span<const detail::scheduled_cold_colored::ProxyRow> geometry,
            std::span<const std::array<Complex,2>> rows) const {
        namespace cert=detail::scheduled_acquisition_certificate;
        for(std::size_t i=0;i<geometry.size();++i)if(geometry[i].owner!=detail::scheduled_cold_colored::no_owner) {
            const auto e=geometry[i].residual,q=std::max(cert::norm(rows[i][0]),cert::norm(rows[i][1]));
            range.colored_residual_square=cert::add(range.colored_residual_square,cert::mul(e,e));
            range.colored_residual_l1=cert::add(range.colored_residual_l1,e);
            range.colored_residual_gram=cert::add(range.colored_residual_gram,cert::add(cert::mul(2*q,e),cert::mul(e,e)));
        }
    }
    void lazy_colored_select(const LazyState::DueRange& range,std::span<const std::size_t> all,
            std::vector<std::size_t>& starts,std::vector<std::size_t>& positions,std::size_t origin_stride=1) {
        starts.clear();positions.clear();
        // Only the earliest authoritative due group is an initial work target.
        // A +511-block cover may select thousands of unconsumed future lanes.
        // Later events retain original replay; this gate does not narrow the
        // native raw-screen domain or the original processing journal.
        if(!std::isfinite(lazy_state()->next_due))return;
        const auto frontier=std::min<long double>(lazy_state()->capture_end,std::ceil(lazy_state()->next_due));
        for(std::size_t j=0;j<all.size();++j){const auto lane=lazy_family_lane(range.family,range.first+j*origin_stride);
            if(lazy_state()->catalog->due(lane,range.index,lazy_execution().cursor)<=frontier) {
                starts.push_back(all[j]);positions.push_back(j);}
        }
    }
    bool lazy_colored_owners(std::span<const long double> source,std::span<const std::uint8_t> mask,
            std::span<std::uint64_t> owners) const {
        if(source.size()!=mask.size() || source.size()!=owners.size())return false;
        for(std::size_t i=0;i<source.size();++i){owners[i]=detail::scheduled_cold_colored::no_owner;
            if(!mask[i])continue;
            if(source[i]<0 || source[i]>=code.symbol_samples() || std::trunc(source[i])!=source[i])return false;
            owners[i]=static_cast<std::uint64_t>(source[i])/code.chip_samples();}
        return true;
    }
    std::size_t lazy_colored_scratch(std::size_t lanes,std::size_t fragments) const {
        namespace tile=detail::scheduled_acquisition_tile;namespace colored=detail::scheduled_cold_colored;
        return tile::add_size(sizeof(colored::RuntimeDirect)+sizeof(colored::RuntimeProxy)+sizeof(colored::Direct)+8192,
            tile::add_size(tile::multiply_size(lanes,sizeof(colored::Direct::Lane)+4*sizeof(colored::Direct::Errors)+
                2*sizeof(std::array<colored::Summary,2>)+2*sizeof(std::size_t)),tile::multiply_size(fragments,sizeof(std::uint64_t)+sizeof(colored::ProxyRow))));
    }
    #include "scheduled_lazy_frequency_family.inc"
    #include "scheduled_lazy_frequency_postinverse.inc"
    template<class T> bool lazy_screen_range(LazyState::DueRange& range,std::stop_token stop,std::size_t origin_stride=1) {
        rx_cpu::Scope cpu_screen(rx_cpu::Stage::search_residual);
        namespace tile=detail::scheduled_acquisition_tile;
        namespace cert=detail::scheduled_acquisition_certificate;
        if(!lazy_reference_environment() || !shaped || scheduled->fhss_shape.enabled || range.first==range.end)return false;
        auto& execution=lazy_execution();auto& code=execution.code;
        if(lazy_state()->certificate_profile.enabled)execution.certificate_pending={};
        const auto reference=lazy_screen_reference(range);
        require(origin_stride==1 || origin_stride==2,"unsupported numerical origin stride");
        const auto size=1+(range.end-range.first-1)/origin_stride;
        if(size>tile::maximum_start_span)return false;
        std::vector<std::size_t> starts;starts.reserve(size);long double displacement=0;
        const auto source_first=static_cast<long double>(range.index)*code.symbol_samples();
        const auto first_boundary=lazy_state()->catalog->boundary(reference,source_first,false,execution.cursor);
        const auto last_boundary=lazy_state()->catalog->boundary(reference,source_first+code.symbol_samples(),false,execution.cursor);
        const auto original_aperture=std::max(std::abs(first_boundary-reference.origin),std::abs(last_boundary-reference.origin))+tile::maximum_start_span+2;
        for(std::size_t j=0;j<size;++j){const auto lane=lazy_family_lane(range.family,range.first+j*origin_stride);
            const auto delta=lane.origin-reference.origin,integer=std::round(delta);
            if(integer<0 || integer>=tile::maximum_start_span || (!starts.empty() && integer<=starts.back()))return false;
            starts.push_back(static_cast<std::size_t>(integer));
            const auto rate_difference=std::abs(lane.rate-reference.rate);
            const auto origin_difference=std::abs(lane.rate*(reference.origin-lane.origin)+reference.rate*integer);
            displacement=std::max(displacement,origin_difference+rate_difference*original_aperture);
        }
        if(displacement>0 && config.search_arithmetic==SearchArithmetic::fp64)return false;
        displacement+=256*std::numeric_limits<long double>::epsilon()*(1+scheduled->complex_geometry.physical_aperture_samples+
            std::abs(reference.origin)+code.symbol_samples());
        if(displacement>.25L)return false;
        const auto span=starts.back()+1;
        auto& template_cache=[&]() -> auto& {if constexpr(std::is_same_v<T,float>)return execution.fp32_templates;else return execution.fp64_templates;}();
        if(!template_cache.empty()) {
            const auto& previous=template_cache.front()->key;
            if(previous.origin!=reference.origin || previous.rate!=reference.rate ||
               previous.phase!=reference.phase_lower || previous.index!=range.index)execution.drop_templates();
        }

        const auto available=execution.available_bytes;
        const auto shape=detail::scheduled_parallel_preparation::shape(span);const auto fragment_samples=shape.maximum_fragment;
        const auto transform=shape.transform_size();
        const bool native_accumulation=std::is_same_v<T,float> && config.search_arithmetic!=SearchArithmetic::fp64 &&
            detail::scheduled_parallel_preparation::native_float_accumulation_requested();
        const auto reservation=(native_accumulation?36:48)*transform*sizeof(std::complex<double>)+
            (native_accumulation?12*transform*sizeof(std::complex<float>):0)+(span+fragment_samples)*96+
            size*(sizeof(tile::Value)+sizeof(cert::ReferenceErrors))+65536+
            (lazy_colored_requested()?lazy_colored_scratch(size,fragment_samples):0);
        const auto previous_scratch=execution.scratch_reservation;
        struct RestoreScratch {LazyState::Execution& execution;std::size_t previous;
            ~RestoreScratch(){execution.scratch_reservation=previous;}} restore_scratch{execution,previous_scratch};
        execution.scratch_reservation=tile::add_size(previous_scratch,reservation);
        if(execution.scratch_reservation>available)return false;
        if(execution.numeric_bytes()>available-execution.scratch_reservation)execution.drop_plans();
        rx_cpu::Scope cpu_plan(rx_cpu::Stage::setup);
        const auto& plan=lazy_plan<T>(shape,stop);cpu_plan.finish();
        struct ScreenPin {LazyState::Execution& state;explicit ScreenPin(LazyState::Execution& s):state(s){state.screen_active=true;}
            ~ScreenPin(){state.screen_active=false;}} screen_pin{execution};
        tile::Accumulator<T> accumulator(plan,0,starts,available,stop,lazy_state()->certificate_profile.enabled,native_accumulation);
        std::vector<float> pcm(span+fragment_samples-1);
        std::vector<Complex> oscillator(pcm.size());std::vector<std::uint8_t> observed(pcm.size());
        std::vector<std::array<Complex,2>> rows(fragment_samples);std::vector<std::uint8_t> mask(rows.size());
        struct Wipe {decltype(rows)& q;~Wipe(){if(!q.empty())OPENSSL_cleanse(q.data(),q.size()*sizeof(q[0]));}} wipe{rows};
        const auto shift=static_cast<std::uint64_t>(std::ceil(std::max(0.L,-reference.origin))+fragment_samples);
        const auto phase=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(reference.phase_lower);
        const auto begin=lazy_state()->catalog->boundary(reference,static_cast<long double>(range.index)*code.symbol_samples(),false,execution.cursor);
        const auto central_end=lazy_state()->catalog->boundary(reference,static_cast<long double>(range.index+1)*code.symbol_samples(),false,execution.cursor);
        if(static_cast<long double>(lazy_history_first())>std::max(0.L,std::ceil(begin)))return false;

        auto v=static_cast<std::uint64_t>(std::max(0.L,std::floor(begin+shift)-1));
        const auto until=static_cast<std::uint64_t>(std::max(0.L,std::ceil(central_end+shift)+2));
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
        const auto qmax=cert::up(pattern_pcm_radius_limit/amplitude);
        static const auto pulse_slope=[] {double value=0;for(unsigned i=0;i<2048;++i)value=std::max(value,
            std::abs(pattern_pulse((i+1)/256.)-pattern_pulse(i/256.))*256);return std::nextafter(value*1.000000000001,std::numeric_limits<double>::infinity());}();
        const auto gate_slope=cert::divide(1.875,std::max(1.,std::ceil(config.sample_rate*config.scheduled_hopping->ramp_seconds)));
        const auto smooth=cert::mul(static_cast<double>(displacement),cert::add(cert::divide(34*pulse_slope,code.chip_samples()),cert::mul(gate_slope,qmax)));
        const auto jump=cert::up(4*std::abs(pattern_pulse(8.)));
        double residual_square=0,residual_l1=0,residual_gram=0,maximum=0,union_energy=0;
        std::uint64_t groups=0,last_group=std::numeric_limits<std::uint64_t>::max();
        maximum=lazy_state()->summary_maximum;union_energy=lazy_state()->summary_union_energy;
        code.set_stream_phase_samples(reference.phase_lower);
        require(range.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),"implicit template chip coordinate overflow");
        const auto chip_first=range.index*code.chips_per_symbol();code.prepare_symbol(chip_first,stop);
        detail::scheduled_source_samples::Reader source_samples(config,code,chip_first,reference.rate);
        range.colored.clear();range.colored_sample_pairs=0;range.colored_selected=0;range.colored_mode=0;range.colored_gate=0;
        range.colored_residual_square=range.colored_residual_l1=range.colored_residual_gram=0;
        std::unique_ptr<detail::scheduled_cold_colored::RuntimeDirect> colored;
        std::unique_ptr<detail::scheduled_cold_colored::RuntimeProxy> colored_proxy;
        std::vector<detail::scheduled_cold_colored::ProxyRow> colored_rows;
        std::vector<std::uint64_t> colored_owners;
        std::vector<std::size_t> colored_starts,colored_positions;
        const auto colored_exact=lazy_colored_eligible(reference,range,starts,origin_stride);
        const auto colored_fractional=!colored_exact && lazy_colored_proxy_eligible(reference,range,starts,displacement,origin_stride);
        if(range.colored.capacity()>=size && (colored_exact || colored_fractional)) {
            colored_starts.reserve(size);colored_positions.reserve(size);
            lazy_colored_select(range,starts,colored_starts,colored_positions,origin_stride);
            if(!colored_starts.empty()) {
                if(colored_exact) {colored=std::make_unique<detail::scheduled_cold_colored::RuntimeDirect>(colored_starts.size(),code.chip_samples(),
                    union_energy,lazy_colored_carrier_cap,2,block_samples);colored_owners.resize(fragment_samples);range.colored_mode=1;range.colored_gate=7;}
                else {colored_proxy=std::make_unique<detail::scheduled_cold_colored::RuntimeProxy>(colored_starts.size(),code.chip_samples(),
                    union_energy,lazy_colored_carrier_cap,2,block_samples,0);colored_rows.resize(fragment_samples);range.colored_mode=2;}
                require((colored?colored->working_bytes():colored_proxy->working_bytes())+(colored_starts.capacity()+colored_positions.capacity())*sizeof(std::size_t)+
                    colored_owners.capacity()*sizeof(std::uint64_t)+colored_rows.capacity()*sizeof(colored_rows[0])+
                    size*sizeof(std::array<detail::scheduled_cold_colored::Summary,2>)<=lazy_colored_scratch(size,fragment_samples),
                    "original colored scalar scratch exceeds reservation");
            }
        }
        while(v<until){cancelled(stop);
            const auto real=static_cast<long double>(v)-shift;
            const auto tick=phase.add_signed((real-reference.origin)*reference.rate);const auto location=scheduled_map()->locate(tick);
            if(!location.active){const auto next=scheduled_map()->source_to_physical(location.source,false);
                const auto next_real=reference.origin+next.distance(phase)/reference.rate;
                const auto target=static_cast<std::uint64_t>(std::max(0.L,std::floor(next_real+shift)-1));
                if(target>v+1){v=std::min(target,until);continue;}}
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(rows.size(),until-v));
            const typename LazyState::TemplateKey key{&plan,reference.origin,reference.rate,reference.phase_lower,range.index,v,shift,count};
            typename LazyState::template TemplatePiece<T>* cached=nullptr;
            for(const auto& item:template_cache)if(item->key==key){cached=item.get();break;}
            if(cached)++execution.spectral_template_hits;else ++execution.spectral_template_misses;
            std::fill(rows.begin(),rows.end(),std::array<Complex,2>{});std::fill(mask.begin(),mask.end(),0);
            std::vector<long double> source_geometry(count);std::vector<double> gain_geometry(count);
            std::vector<std::uint8_t> active_geometry(count);std::vector<std::uint64_t> frame_geometry(count);
            rx_cpu::Scope cpu_template(cached?rx_cpu::Stage::template_cache:rx_cpu::Stage::template_build);
            for(std::size_t i=0;i<count;++i){
                long double source;double gain;bool active;std::uint64_t frame;
                if(cached) {
                    source=cached->source[i];gain=cached->gain[i];active=cached->active[i];frame=cached->frame[i];
                    rows[i]=cached->rows[i];mask[i]=cached->mask[i];
                } else {
                    const auto p=static_cast<long double>(v+i)-shift;
                    const auto t=phase.add_signed((p-reference.origin)*reference.rate);const auto l=scheduled_map()->locate(t);
                    source=l.source.distance(reference.anchor)-static_cast<long double>(range.index)*code.symbol_samples();
                    gain=l.active?SchedHopSourceMap::gain(l,t):0.;active=l.active;frame=l.frame_index;
                }
                source_geometry[i]=source;gain_geometry[i]=gain;active_geometry[i]=active;frame_geometry[i]=frame;
                double residual=0;
                if(active && frame!=last_group){++groups;last_group=frame;}
                if(gain>0 && source>=0 && source<code.symbol_samples()){
                    if(!cached) {
                        auto q=source_samples.value(source,stop);mask[i]=1;
                        for(unsigned bit=0;bit<2;++bit)rows[i][bit]=gain*pattern_limit_pcm(amplitude*q[bit])/amplitude;
                    }
                    residual=smooth;
                    const auto edge=(source/code.chip_samples()-.5L),nearest=std::round(edge);
                    if(std::abs(edge-nearest)*code.chip_samples()<=displacement+1e-10L)residual=cert::add(residual,jump);
                    const auto tail=code.symbol_samples()%code.chip_samples();
                    if(tail){const auto center=static_cast<long double>(code.symbol_samples()-tail)+.5L*tail;
                        if(std::min(std::abs(source-center-8.L*code.chip_samples()),std::abs(source-center+8.L*code.chip_samples()))<=displacement+1e-10L)
                            residual=cert::add(residual,jump);}
                    if(source<=displacement || code.symbol_samples()-source<=displacement)residual=cert::add(residual,qmax);
                } else {
                    residual=cert::mul(static_cast<double>(displacement),cert::mul(gate_slope,qmax));
                    if(gain>0 && std::min(std::abs(source),std::abs(source-code.symbol_samples()))<=displacement+1e-10L)
                        residual=cert::add(residual,qmax);
                }
                residual_square=cert::add(residual_square,cert::mul(residual,residual));
                residual_l1=cert::add(residual_l1,residual);
                residual_gram=cert::add(residual_gram,cert::add(cert::mul(2*qmax,residual),cert::mul(residual,residual)));
            }
            cpu_template.finish();rx_cpu::Scope cpu_mix(rx_cpu::Stage::mixing);
            std::fill(pcm.begin(),pcm.end(),0);std::fill(observed.begin(),observed.end(),0);std::fill(oscillator.begin(),oscillator.end(),Complex{});
            const auto input_first=static_cast<long double>(v)-shift;
            const auto captured_first=static_cast<std::uint64_t>(std::max<long double>(lazy_history_first(),std::max(0.L,input_first)));
            const auto captured_end=static_cast<std::uint64_t>(std::max(0.L,std::min<long double>(lazy_state()->capture_end,input_first+pcm.size())));
            if(captured_first<captured_end)lazy_visit_blocks(captured_first,captured_end,[&](const auto& block) {
                const auto a=static_cast<std::uint64_t>(std::max<long double>(block.first,std::max(0.L,input_first)));
                const auto b=static_cast<std::uint64_t>(std::min<long double>(block.first+block.pcm.size(),input_first+pcm.size()));
                auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*banks[reference.frequency].frequency/config.sample_rate,static_cast<long double>(tau))));
                const auto step=std::polar(1.,tau*banks[reference.frequency].frequency/config.sample_rate);
                for(std::uint64_t n=block.first;n<b;++n){if(n>=a){const auto at=static_cast<std::size_t>(static_cast<long double>(n)-input_first);
                    pcm[at]=block.pcm[n-block.first];oscillator[at]=o;observed[at]=1;
                    require(cert::norm(o)<=2,"original carrier magnitude exceeded screen contract");}o*=step;}
            },false);
            tile::PreparedInput<T> input(plan,{pcm,oscillator,observed,v},available,stop);cpu_mix.finish();
            rx_cpu::Scope cpu_transform(rx_cpu::Stage::template_build);
            std::unique_ptr<tile::PreparedFragment<T>> temporary;
            const tile::PreparedFragment<T>* fragment=cached?cached->prepared.get():nullptr;
            if(!fragment) {
                temporary=std::make_unique<tile::PreparedFragment<T>>(plan,tile::Fragment{
                    std::span<const std::array<Complex,2>>{rows}.first(count),std::span<const std::uint8_t>{mask}.first(count),v},available,stop);
                fragment=temporary.get();execution.template_transforms+=fragment->work.template_transforms;
            }
            cpu_transform.finish();rx_cpu::Scope cpu_product(rx_cpu::Stage::gram);
            accumulator.add(input,*fragment,stop);
            if(colored) {
                const auto cell_owners=std::span<std::uint64_t>(colored_owners).first(count);
                if(!lazy_colored_owners(source_geometry,std::span<const std::uint8_t>(mask).first(count),cell_owners))colored.reset();
                else colored->add({pcm,oscillator,observed,v},
                    {std::span<const std::array<Complex,2>>(rows).first(count),std::span<const std::uint8_t>(mask).first(count),v},
                    colored_starts,cell_owners,stop);
            }
            if(colored_proxy) {
                const auto proxy_rows=std::span<detail::scheduled_cold_colored::ProxyRow>(colored_rows).first(count);
                if(!lazy_colored_proxy_rows(reference,range,std::span<const std::array<Complex,2>>(rows).first(count),source_geometry,gain_geometry,active_geometry,std::span<const std::uint8_t>(mask).first(count),
                    proxy_rows,displacement,qmax,pulse_slope,gate_slope,jump)){colored_proxy.reset();range.colored_gate=5;range.colored_mode=0;}
                else {lazy_colored_proxy_residual(range,proxy_rows,std::span<const std::array<Complex,2>>(rows).first(count));
                    colored_proxy->add({pcm,oscillator,observed,v},
                    {std::span<const std::array<Complex,2>>(rows).first(count),std::span<const std::uint8_t>(mask).first(count),v},
                    colored_starts,proxy_rows,stop);}
            }
            execution.input_transforms+=input.work.input_transforms;
            execution.peak_prepared_bytes=std::max(execution.peak_prepared_bytes,plan.working_bytes()+accumulator.working_bytes()+input.working_bytes()+fragment->working_bytes());
            if(!cached) {
                const auto entry_reservation=sizeof(typename LazyState::template TemplatePiece<T>)+
                    rows.size()*sizeof(rows[0])+mask.size()+
                    count*(sizeof(long double)+sizeof(double)+sizeof(std::uint64_t)+1)+fragment->working_bytes();
                const auto remaining=execution.available_bytes-execution.numeric_bytes();
                // Cache spectra only after the live accumulator/input capacity
                // is accounted. Optional cache pressure never drops a target.
                const auto next_capacity=std::max<std::size_t>(8,template_cache.capacity()*2);
                // Reserve holds old and complete new pointer backing until
                // publication; charge the entire new capacity before copying.
                const auto pointer_reservation=template_cache.size()==template_cache.capacity()?
                    next_capacity*sizeof(typename std::remove_reference_t<decltype(template_cache)>::value_type):0;
                if(entry_reservation+pointer_reservation+execution.scratch_reservation<remaining && template_cache.size()<tile::maximum_fragments) {
                    try {
                    auto entry=std::make_unique<typename LazyState::template TemplatePiece<T>>();
                    std::vector<std::array<Complex,2>> replacement_rows(fragment_samples);
                    std::vector<std::uint8_t> replacement_mask(fragment_samples);
                    if(template_cache.size()==template_cache.capacity())template_cache.reserve(next_capacity);
                    entry->key=key;
                    // Transfer backing without moving its allocation. The
                    // immutable PreparedFragment spans keep exactly the same
                    // addresses; no second template FFT is performed.
                    entry->rows=std::move(rows);entry->mask=std::move(mask);
                    entry->source=std::move(source_geometry);entry->gain=std::move(gain_geometry);
                    entry->active=std::move(active_geometry);entry->frame=std::move(frame_geometry);
                    entry->prepared=std::move(temporary);
                    rows=std::move(replacement_rows);mask=std::move(replacement_mask);
                    template_cache.push_back(std::move(entry));
                    require(execution.numeric_bytes()+execution.scratch_reservation<=execution.available_bytes,
                        "implicit cached fragment exceeds worker reservation");
                    } catch(const std::bad_alloc&) {
                        // Every throwing allocation precedes backing transfer.
                        // Valid numeric output survives optional cache pressure.
                    }
                }
            }
            v+=count;
        }
        rx_cpu::Scope cpu_certificate(rx_cpu::Stage::gram);
        const auto values=accumulator.finish(stop);execution.inverse_transforms+=accumulator.work().inverse_transforms;
        if constexpr(std::is_same_v<T,float>)++execution.fp32_ranges;else ++execution.fp64_ranges;
        if(displacement>1e-9L){++execution.proxy_ranges;range.certificate_cohort=certdiag::cohort<T>(true);}
        else range.certificate_cohort=certdiag::cohort<T>(false);
        range.screen.assign(values.begin(),values.end());range.reference.resize(size);
        range.numeric_origin_stride=1;
        if(colored || colored_proxy) {
            auto summaries=colored?colored->finish():colored_proxy->finish();range.colored.resize(size);
            for(std::size_t j=0;j<summaries.size();++j)range.colored[colored_positions[j]]=summaries[j];
            range.colored_carrier_radius=lazy_colored_carrier_cap;range.colored_carrier_magnitude=2;
            range.colored_sample_pairs=colored?colored->sample_pairs():colored_proxy->sample_pairs();
            range.colored_selected=summaries.size();
        }
        range.screen_first=range.first;range.mask_difference=4*groups+8;range.maximum=maximum;
        range.proxy_residual_square=residual_square;range.proxy_residual_l1=residual_l1;range.proxy_residual_gram=residual_gram;
        for(std::size_t j=0;j<size;++j){const auto n=range.screen[j].fits[0].count+range.mask_difference;
            if(n>(std::numeric_limits<std::size_t>::max()-128*block_samples-128)/128)return false;
            const auto rho=cert::add(cert::gamma(128*static_cast<std::size_t>(n)+128*block_samples+128,std::numeric_limits<double>::epsilon()/2),
                cert::mul(4*block_samples,cert::gamma(8*block_samples+8,std::numeric_limits<double>::epsilon()/2)));
            const auto dot=cert::mul(8*rho*qmax,cert::root(cert::mul(static_cast<double>(n),union_energy)));
            const auto gram=cert::mul(32*rho*n,qmax*qmax);
            range.reference[j]={dot,gram,cert::mul(4*rho,union_energy),n>2?1e-8/(.5*static_cast<double>(n-2)):0.,true};
        }
        if(lazy_state()->certificate_profile.enabled){execution.certificate_pending.observe(range.certificate_cohort,
            static_cast<double>(displacement),residual_l1,residual_gram,residual_square,accumulator.certificate_profile());
            execution.certificate_preparation.merge(execution.certificate_pending);}
        return true;
    }
    // An odd fine-chip duration can give a half-sample acquisition lattice.
    // Integer FFT starts then have two exact fractional-origin classes. Prepare
    // each with its OWN original reference, and restore catalogue/trial order.
    // This changes numerical cover only: no arrival, phase or clock cell moves.
    template<class T> bool lazy_screen_origin_classes(LazyState::DueRange& range,std::stop_token stop) try {
        namespace tile=detail::scheduled_acquisition_tile;
        namespace cert=detail::scheduled_acquisition_certificate;
        const auto count=range.end-range.first;
        const auto* gate=std::getenv("DATAPUMP_SCHED_ORIGIN_CLASSES");
        // Native coverage is not a cost crossover. The complete Live fixture
        // has not demonstrated a benefit; keep the preceding automatic path.
        if(count<2 || count>tile::maximum_start_span || !gate || std::string_view(gate)!="1")
            return lazy_screen_range<T>(range,stop);
        const auto first=lazy_family_lane(range.family,range.first);
        const auto second=lazy_family_lane(range.family,range.first+1);
        const auto delta=second.origin-first.origin;
        // Other subdivisions/clock geometries retain the preceding path. Each
        // class independently validates ALL translations, displacement/history
        // and numerical certificates below; this is not an endpoint proof.
        if(first.rate!=second.rate || !(delta>0) || std::abs(delta-std::round(delta))!=.5L)
            return lazy_screen_range<T>(range,stop);
        auto& execution=lazy_execution();const auto old_scratch=execution.scratch_reservation;
        const auto temporary_bytes=tile::add_size(2*sizeof(LazyState::DueRange)+512,
            tile::multiply_size(count,sizeof(tile::Value)+sizeof(cert::ReferenceErrors)+
                (lazy_colored_requested()?sizeof(std::array<detail::scheduled_cold_colored::Summary,2>):0)));
        const auto charge=tile::add_size(old_scratch,temporary_bytes);
        if(charge>execution.available_bytes)return false;
        if(execution.numeric_bytes()>execution.available_bytes-charge)execution.drop_plans();
        if(execution.numeric_bytes()>execution.available_bytes-charge)return false;
        execution.scratch_reservation=charge;
        struct Restore {LazyState::Execution& execution;std::size_t old;
            ~Restore(){execution.scratch_reservation=old;}} restore{execution,old_scratch};
        std::array<LazyState::DueRange,2> parts;
        for(std::size_t residue=0;residue<2;++residue) {
            auto& part=parts[residue];part.family=range.family;part.first=range.first+residue;
            part.end=range.end;part.index=range.index;
            if(lazy_colored_requested())part.colored.reserve(1+(part.end-part.first-1)/2);
            if(!lazy_screen_range<T>(part,stop,2))return false;
        }
        if(parts[0].certificate_cohort!=parts[1].certificate_cohort)return false;
        range.screen.resize(count);range.reference.resize(count);range.colored.clear();
        range.colored_mode=std::max(parts[0].colored_mode,parts[1].colored_mode);range.colored_gate=0;
        range.colored_sample_pairs=parts[0].colored_sample_pairs+parts[1].colored_sample_pairs;
        range.colored_selected=parts[0].colored_selected+parts[1].colored_selected;
        if(range.colored_mode)range.colored.resize(count);
        range.colored_carrier_radius=lazy_colored_carrier_cap;range.colored_carrier_magnitude=2;
        range.colored_residual_square=std::max(parts[0].colored_residual_square,parts[1].colored_residual_square);
        range.colored_residual_l1=std::max(parts[0].colored_residual_l1,parts[1].colored_residual_l1);
        range.colored_residual_gram=std::max(parts[0].colored_residual_gram,parts[1].colored_residual_gram);
        range.mask_difference=std::max(parts[0].mask_difference,parts[1].mask_difference);
        range.maximum=std::max(parts[0].maximum,parts[1].maximum);
        range.proxy_residual_square=std::max(parts[0].proxy_residual_square,parts[1].proxy_residual_square);
        range.proxy_residual_l1=std::max(parts[0].proxy_residual_l1,parts[1].proxy_residual_l1);
        range.proxy_residual_gram=std::max(parts[0].proxy_residual_gram,parts[1].proxy_residual_gram);
        range.screen_first=range.first;range.numeric_origin_stride=2;range.certificate_cohort=parts[0].certificate_cohort;
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
        const auto qmax=cert::up(pattern_pcm_radius_limit/amplitude);
        for(std::size_t j=0;j<count;++j) {
            range.screen[j]=parts[j%2].screen[j/2];
            if(j/2<parts[j%2].colored.size())range.colored[j]=parts[j%2].colored[j/2];
            // The merged common mask allowance can be larger; recompute the
            // ORIGINAL reduction radius at that larger count, never reuse a
            // smaller class's radius. Actual proxy statistics remain per-lane.
            const auto n=range.screen[j].fits[0].count+range.mask_difference;
            if(n>(std::numeric_limits<std::size_t>::max()-128*block_samples-128)/128)return false;
            const auto rho=cert::add(cert::gamma(128*static_cast<std::size_t>(n)+128*block_samples+128,std::numeric_limits<double>::epsilon()/2),
                cert::mul(4*block_samples,cert::gamma(8*block_samples+8,std::numeric_limits<double>::epsilon()/2)));
            range.reference[j]={cert::mul(8*rho*qmax,cert::root(cert::mul(static_cast<double>(n),lazy_state()->summary_union_energy))),
                cert::mul(32*rho*n,qmax*qmax),cert::mul(4*rho,lazy_state()->summary_union_energy),
                std::max(parts[j%2].reference[j/2].log_error,n>2?1e-8/(.5*static_cast<double>(n-2)):0.),true};
        }
        ++execution.origin_class_covers;
        return true;
    } catch(const std::bad_alloc&) {
        cancelled(stop);return false; // optional numerical cover; original replay remains required
    }
    void lazy_merge_costs(LazyState::Execution& execution) {
        if(lazy_state()->certificate_profile.enabled){lazy_state()->certificate_profile.preparation.merge(execution.certificate_preparation);
            execution.certificate_preparation={};execution.certificate_pending={};}
        lazy_state()->spectral_template_hits+=execution.spectral_template_hits;
        lazy_state()->spectral_template_misses+=execution.spectral_template_misses;
        lazy_state()->plan_hits+=execution.plan_hits;lazy_state()->plan_misses+=execution.plan_misses;
        lazy_state()->screen_lanes+=execution.screen_lanes;lazy_state()->input_transforms+=execution.input_transforms;
        lazy_state()->template_transforms+=execution.template_transforms;lazy_state()->inverse_transforms+=execution.inverse_transforms;
        lazy_state()->eligible_ranges+=execution.eligible_ranges;lazy_state()->fallback_ranges+=execution.fallback_ranges;
        lazy_state()->proxy_ranges+=execution.proxy_ranges;lazy_state()->fp32_ranges+=execution.fp32_ranges;lazy_state()->fp64_ranges+=execution.fp64_ranges;
        lazy_state()->origin_class_covers+=execution.origin_class_covers;
        lazy_state()->peak_prepared_bytes=std::max(lazy_state()->peak_prepared_bytes,execution.peak_prepared_bytes);
        detail::scheduled_lazy_family_runtime::publish(*lazy_state(),execution.frequency_family_work);
        execution.reset_costs();
        // parallel_search has joined, or the serial screen stack has unwound.
        // Freeze even on cancellation before publishing its exact live charge.
        execution.freeze_numeric_bytes();
    }
    void lazy_prepare_screens(std::stop_token stop) {
        namespace sizing=detail::scheduled_parallel_preparation;
        namespace tile=detail::scheduled_acquisition_tile;
        namespace cert=detail::scheduled_acquisition_certificate;
        if(lazy_state()->due_ranges.empty())return;
        require(!lazy_state()->screens_running,"implicit numeric preparation cannot nest");
        // Every serial/parallel/family/postinverse cache mutation belongs to
        // this preparation. Early returns and failed allocations stay thawed.
        lazy_state()->serial_execution->thaw_numeric_bytes();
        for(auto& execution:lazy_state()->executions)execution->thaw_numeric_bytes();
        lazy_capture_summary(); // Immutable until every numeric worker joins.
        struct Ordered {
            LazyState::DueRange* range=nullptr;
            long double rounded_origin=0,origin=0,rate=0;
            std::uint64_t phase=0,index=0;
            auto key()const{return std::tuple{phase,index,rounded_origin,origin,rate,range->first,range->end};}
        };
        struct Group {std::size_t first=0,end=0;};
        const auto requested=[&] {
            const auto* mode=std::getenv("DATAPUMP_SCHED_LAZY_PARALLEL");
            return mode && std::string_view(mode)=="0"?std::size_t{1}:detail::search_concurrency(search.worker_threads);
        }();
        lazy_state()->prepared_ranges.clear();
        std::size_t existing_workers=lazy_state()->executions.capacity()*sizeof(decltype(lazy_state()->executions)::value_type);
        for(const auto& execution:lazy_state()->executions)existing_workers+=execution->working_bytes();
        const auto context=sizeof(LazyState::Execution)+code.working_bytes()+LazyState::Catalog::cursor_reservation_bytes()+
            scheduled_map()->working_bytes()+sizeof(decltype(lazy_state()->executions)::value_type)+4096;
        const auto base=working_bytes()+drift_reserved+amplitude_scratch_reservation-existing_workers-lazy_state()->serial_execution->numeric_bytes();
        const auto free=budget>sizeof(PatternCorrelator)+base?budget-sizeof(PatternCorrelator)-base:0;
        const auto omit_screens=[&](const char* reason) {
            lazy_state()->prepared_ranges.clear();
            for(auto& range:lazy_state()->due_ranges){range.screen.clear();range.reference.clear();range.colored.clear();++lazy_state()->fallback_ranges;}
            ++lazy_state()->serial_workspace_fallbacks;lazy_state()->parallel_workers=0;
            audio::trace::record("scheduled_implicit_parallel_fallback",[&](auto& f){f.add("reason",reason);f.add("ranges",lazy_state()->due_ranges.size());});
        };
        std::size_t output_bytes=0,full_scratch=0,full_plan=0;
        const bool native_accumulation=config.search_arithmetic!=SearchArithmetic::fp64 && sizing::native_float_accumulation_requested();
        const auto shape_of=[&](const LazyState::DueRange& range) {
            const auto reference=lazy_screen_reference(range);
            const auto last=lazy_family_lane(range.family,range.end-1);
            const auto translated=std::round(last.origin-reference.origin);
            const auto span=translated>=0 && translated<tile::maximum_start_span?static_cast<std::size_t>(translated)+1:0;
            return span?sizing::shape(span):tile::Shape{0,scheduled_block_size};
        };
        for(const auto& range:lazy_state()->due_ranges) {
            output_bytes=tile::add_size(output_bytes,tile::multiply_size(range.end-range.first,sizeof(tile::Value)+sizeof(cert::ReferenceErrors)+(lazy_colored_requested()?sizeof(std::array<detail::scheduled_cold_colored::Summary,2>):0)));
            const auto shape=shape_of(range);const auto lanes=range.end-range.first;
            if(shape.start_span && lanes<=tile::maximum_start_span) {
                full_scratch=std::max(full_scratch,sizing::scratch_bytes(shape.start_span,lanes,shape.maximum_fragment,native_accumulation)+(lazy_colored_requested()?lazy_colored_scratch(lanes,shape.maximum_fragment):0));
                full_plan=std::max(full_plan,sizing::plan_bytes(shape.start_span,shape.maximum_fragment));
            }
        }
        if(output_bytes>free){omit_screens("all logical result cells exceed actual workspace loan");return;}
        const auto initial_metadata=tile::add_size(tile::multiply_size(lazy_state()->due_ranges.size(),sizeof(Ordered)+sizeof(Group)+sizeof(LazyState::DueRange)),8192);
        if(initial_metadata>free-output_bytes){omit_screens("minimum numerical range metadata exceeds actual loan");return;}
        const auto initial_free=free-output_bytes-initial_metadata;
        auto planned_workers=full_scratch?sizing::affordable_workers(initial_free,context,full_scratch,full_plan,lazy_state()->due_ranges.size(),requested):0;
        if(planned_workers<2)planned_workers=1;
        // Adapt only the NUMERIC cover. Every endpoint below is an existing
        // catalogue origin; no search origin/phase or physical fence is added.
        auto quota=initial_free/planned_workers;
        if(planned_workers>1)quota=quota>context?quota-context:0;
        const auto walk=[&](std::size_t allowance,auto consume) {
            std::size_t pieces=0;
            for(const auto& original:lazy_state()->due_ranges)for(auto first=original.first;first<original.end;) {
                auto end=original.end;
                LazyState::DueRange piece;piece.family=original.family;piece.first=first;piece.index=original.index;
                for(;;) {
                    piece.end=end;const auto shape=shape_of(piece);const auto lanes=end-first;
                    const bool bounded=shape.start_span && lanes<=tile::maximum_start_span;
                    const bool affordable=bounded && sizing::scratch_bytes(shape.start_span,lanes,shape.maximum_fragment,native_accumulation)+(lazy_colored_requested()?lazy_colored_scratch(lanes,shape.maximum_fragment):0)+
                        sizing::plan_bytes(shape.start_span,shape.maximum_fragment)<=allowance;
                    if(affordable || end-first==1)break;
                    end=first+(end-first+1)/2;
                }
                piece.id=lazy_family_lane(piece.family,piece.first).id;
                consume(piece);++pieces;first=end;
            }
            return pieces;
        };
        std::size_t pieces=0,metadata=0,range_growth=0;
        // Tighten for all retained result arrays and the actual number of
        // subtiles. More metadata can only reduce the finite allowance.
        for(unsigned pass=0;pass<16;++pass) {
            pieces=walk(quota,[](const auto&){});
            metadata=tile::add_size(tile::multiply_size(pieces,sizeof(Ordered)+sizeof(Group)),8192);
            // Old empty backing remains live until reserve returns: the
            // complete new allocation, rather than only growth, is transient.
            range_growth=pieces>lazy_state()->prepared_ranges.capacity()?tile::multiply_size(pieces,sizeof(LazyState::DueRange)):0;
            const auto extra=tile::add_size(output_bytes,tile::add_size(metadata,range_growth));
            if(extra>free){omit_screens("bounded numerical subtiles cannot retain every logical output cell");return;}
            auto tightened=(free-extra)/planned_workers;
            if(planned_workers>1)tightened=tightened>context?tightened-context:0;
            if(tightened>=quota)break;
            quota=tightened;
            if(pass==15){omit_screens("numerical subtile workspace partition did not converge");return;}
        }
        if(requested>1 || output_bytes+metadata+range_growth>budget-sizeof(PatternCorrelator)-accounted_bytes)
            lazy_state()->serial_execution->drop_plans();
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        try {room_for(tile::add_size(output_bytes,tile::add_size(metadata,range_growth)));}
        catch(const Error& error){omit_screens(error.what());return;}
        struct MetadataReservation {
            Impl& self;std::size_t old;
            MetadataReservation(Impl& s,std::size_t bytes):self(s),old(s.lazy_state()->transient_reservation) {
                self.lazy_state()->transient_reservation+=bytes;self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;
            }
            ~MetadataReservation(){self.lazy_state()->transient_reservation=old;self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;}
        } metadata_reservation{*this,metadata};
        lazy_state()->prepared_ranges.reserve(pieces);
        walk(quota,[&](const auto& piece) {
            lazy_state()->prepared_ranges.push_back(piece);auto& saved=lazy_state()->prepared_ranges.back();
            saved.screen.reserve(piece.end-piece.first);saved.reference.reserve(piece.end-piece.first);
            if(lazy_colored_requested())saved.colored.reserve(piece.end-piece.first);
        });
        std::size_t covered=0;
        for(const auto& range:lazy_state()->prepared_ranges)covered+=range.end-range.first;
        std::size_t expected=0;for(const auto& range:lazy_state()->due_ranges)expected+=range.end-range.first;
        require(covered==expected,"implicit numerical subdivision omitted an original origin cell");
        std::vector<Ordered> ordered;ordered.reserve(pieces);
        std::vector<Group> groups;groups.reserve(pieces);
        std::size_t maximum_scratch=0,maximum_plan=0;
        for(auto& range:lazy_state()->prepared_ranges) {
            const auto reference=lazy_screen_reference(range);const auto shape=shape_of(range);const auto lanes=range.end-range.first;
            if(shape.start_span && lanes<=tile::maximum_start_span) {
                maximum_scratch=std::max(maximum_scratch,sizing::scratch_bytes(shape.start_span,lanes,shape.maximum_fragment,native_accumulation)+(lazy_colored_requested()?lazy_colored_scratch(lanes,shape.maximum_fragment):0));
                maximum_plan=std::max(maximum_plan,sizing::plan_bytes(shape.start_span,shape.maximum_fragment));
            }
            ordered.push_back({&range,std::round(reference.origin),reference.origin,reference.rate,reference.phase_lower,range.index});
        }
        std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) {
            return std::tuple{a.key(),a.range->family}<std::tuple{b.key(),b.range->family};
        });
        for(std::size_t first=0;first<ordered.size();) {
            auto end=first+1;while(end<ordered.size() && ordered[end].key()==ordered[first].key())++end;
            groups.push_back({first,end});first=end;
        }
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        const auto actual_metadata=ordered.capacity()*sizeof(Ordered)+groups.capacity()*sizeof(Group)+8192;
        if(actual_metadata>metadata) {
            const auto extra=actual_metadata-metadata;room_for(extra);lazy_state()->transient_reservation+=extra;
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        }
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"implicit numerical subtile metadata/results exceed workspace");
        existing_workers=lazy_state()->executions.capacity()*sizeof(decltype(lazy_state()->executions)::value_type);
        for(const auto& execution:lazy_state()->executions)existing_workers+=execution->working_bytes();
        const auto current_base=accounted_bytes-existing_workers;
        const auto available=budget-sizeof(PatternCorrelator)-current_base;
        auto workers=maximum_scratch?sizing::affordable_workers(available,context,maximum_scratch,maximum_plan,groups.size(),requested):0;
        if(workers>1) {
            lazy_state()->serial_execution->drop_plans();
            try {
                if(lazy_state()->executions.size()>workers)lazy_state()->executions.resize(workers);
                lazy_state()->executions.reserve(workers);
                while(lazy_state()->executions.size()<workers) {
                    lazy_state()->executions.push_back(std::make_unique<LazyState::Execution>(config,*lazy_state()->catalog,*scheduled_map()));
                    ++work.scheduled_worker_preparations;
                }
            } catch(const std::bad_alloc&) {workers=0;}
            if(workers>1) {
                std::size_t numeric=0;for(const auto& execution:lazy_state()->executions)numeric+=execution->numeric_bytes();
                const auto required=working_bytes()+drift_reserved+amplitude_scratch_reservation-numeric;
                if(sizeof(PatternCorrelator)+required>budget)workers=0;
                else {
                    const auto partition=(budget-sizeof(PatternCorrelator)-required)/workers;
                    if(partition<maximum_scratch+maximum_plan)workers=0;
                    else for(auto& execution:lazy_state()->executions) {
                        execution->available_bytes=partition;
                        if(execution->numeric_bytes()>partition-maximum_scratch-maximum_plan)execution->drop_plans();
                    }
                }
            }
        }
        const auto prepare_one=[&](LazyState::DueRange& range) {
            auto& execution=lazy_execution();
            try {
                const bool ready=config.search_arithmetic==SearchArithmetic::fp64?
                    lazy_screen_origin_classes<double>(range,stop):lazy_screen_origin_classes<float>(range,stop);
                if(!ready){++execution.fallback_ranges;range.screen.clear();range.reference.clear();range.colored.clear();}
                else {++execution.eligible_ranges;execution.screen_lanes+=range.screen.size();}
            } catch(const Error& error){if(stop.stop_requested())throw;
                ++execution.fallback_ranges;range.screen.clear();range.reference.clear();range.colored.clear();
                audio::trace::record("scheduled_implicit_screen_fallback",[&](auto& f){f.add("reason",error.what());});
            }
        };
        const auto prepare_group=[&](const Group& bounds) {
            bool ready=false;
            {
                auto& execution=lazy_execution();const auto previous=execution.scratch_reservation;
                const auto count=bounds.end-bounds.first;
                const auto pointer_bytes=tile::multiply_size(count,sizeof(LazyState::DueRange*));
                if(tile::add_size(execution.numeric_bytes(),tile::add_size(previous,pointer_bytes))<=execution.available_bytes) {
                    std::vector<LazyState::DueRange*> cohort;cohort.reserve(count);
                    const auto charge=tile::add_size(previous,tile::multiply_size(cohort.capacity(),sizeof(cohort[0])));
                    if(tile::add_size(execution.numeric_bytes(),charge)<=execution.available_bytes) {
                        execution.scratch_reservation=charge;
                        struct Restore {LazyState::Execution& execution;std::size_t old;
                            ~Restore(){execution.scratch_reservation=old;}} restore{execution,previous};
                        for(auto i=bounds.first;i<bounds.end;++i)cohort.push_back(ordered[i].range);
                        try {
                            ready=config.search_arithmetic==SearchArithmetic::fp64?
                                lazy_screen_family_optional_post<double>(cohort,stop):lazy_screen_family_optional_post<float>(cohort,stop);
                            if(ready)for(auto* range:cohort) {
                                ++execution.eligible_ranges;execution.screen_lanes+=range->screen.size();
                            }
                        } catch(const Error& error) {if(stop.stop_requested())throw;
                            audio::trace::record("scheduled_implicit_family_fallback",[&](auto& f){f.add("reason",error.what());});
                        }
                    }
                }
            } // Every family pointer/accumulator/borrower is gone before retry.
            if(!ready)for(auto i=bounds.first;i<bounds.end;++i)prepare_one(*ordered[i].range);
        };
        if(workers>1) {
            lazy_state()->parallel_workers=workers;lazy_state()->parallel_peak_workers=std::max(lazy_state()->parallel_peak_workers,workers);
            ++lazy_state()->parallel_dispatches;lazy_state()->parallel_groups+=groups.size();lazy_state()->parallel_ranges+=ordered.size();
            std::size_t numeric=0;for(const auto& execution:lazy_state()->executions)numeric+=execution->numeric_bytes();
            const auto reserved=sizeof(PatternCorrelator)+working_bytes()+drift_reserved+amplitude_scratch_reservation-numeric+
                workers*lazy_state()->executions.front()->available_bytes;
            require(reserved<=budget,"implicit workers exceed the actual global workspace loan");
            lazy_state()->parallel_reserved_bytes=std::max(lazy_state()->parallel_reserved_bytes,reserved);
            work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,reserved);
            struct BatchPin {LazyState& state;explicit BatchPin(LazyState& s):state(s){state.screens_running=true;}
                ~BatchPin(){state.screens_running=false;}} pin{*lazy_state()};
            // Bounded owner/helper reservation covers these per-pool-slot
            // bytes; only that slot's serial job stream writes its own byte.
            std::vector<unsigned char> visited_slots(workers,0);
            const auto permutation_binding=detail::pattern_permutation_binding;
            const auto source_binding=detail::scheduled_source_samples::binding;
            std::atomic<std::size_t> active_jobs{0},peak_jobs{0};
            std::exception_ptr failure;
            try {
                detail::parallel_search(workers,workers,[&](std::size_t cpu_worker,std::size_t worker) {
                    visited_slots[cpu_worker]=1;
                    const auto active=active_jobs.fetch_add(1)+1;auto peak=peak_jobs.load();
                    while(active>peak && !peak_jobs.compare_exchange_weak(peak,active)){}
                    struct JobPin {std::atomic<std::size_t>& count;~JobPin(){count.fetch_sub(1);}} job_pin{active_jobs};
                    detail::PatternPermutationScope permutation_scope(permutation_binding.pools,
                        permutation_binding.key_namespace);
                    detail::scheduled_source_samples::Scope source_scope(source_binding.pool,source_binding.key_namespace);
                    auto& execution=*lazy_state()->executions[worker];
                    auto& view=worker_lazy_view();const auto previous=view;
                    auto& map=worker_map_view();const auto previous_map=map;
                    struct Restore {decltype(view) target;decltype(previous) old;decltype(map) map_target;decltype(previous_map) map_old;
                        ~Restore(){target=old;map_target=map_old;}} restore{view,previous,map,previous_map};
                    view={this,&execution};map={this,execution.owned_map.get()};
                    // Compatible phase/index/rounded-origin families are one
                    // indivisible job. All fine-frequency and certified tiny
                    // clock siblings reuse the same private q FFT context.
                    for(auto group=worker;group<groups.size();group+=workers) {
                        cancelled(stop);const auto bounds=groups[group];
                        prepare_group(bounds);
                    }
                });
            } catch(...){failure=std::current_exception();}
            // parallel_search joins even on cancellation/error. Only this
            // owner now folds counters or walks mutable cache capacities.
            lazy_state()->actual_worker_slots=static_cast<std::size_t>(std::count(visited_slots.begin(),visited_slots.end(),1));
            lazy_state()->peak_actual_worker_slots=std::max(lazy_state()->peak_actual_worker_slots,lazy_state()->actual_worker_slots);
            lazy_state()->peak_parallel_jobs=std::max(lazy_state()->peak_parallel_jobs,peak_jobs.load());
            for(auto& execution:lazy_state()->executions)lazy_merge_costs(*execution);
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            if(failure)std::rethrow_exception(failure);
        } else {
            if(requested>1){++lazy_state()->serial_workspace_fallbacks;
                audio::trace::record("scheduled_implicit_parallel_fallback",[&](auto& f){f.add("reason","complete worker reservation exceeds actual loan or independent groups");f.add("requested",requested);f.add("groups",groups.size());});}
            lazy_state()->parallel_workers=1;lazy_state()->actual_worker_slots=1;
            decltype(lazy_state()->executions){}.swap(lazy_state()->executions);
            auto& execution=*lazy_state()->serial_execution;
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            execution.available_bytes=budget-sizeof(PatternCorrelator)-accounted_bytes+execution.numeric_bytes();
            try {for(const auto& bounds:groups)prepare_group(bounds);}
            catch(...){lazy_merge_costs(execution);accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;throw;}
            lazy_merge_costs(execution);
        }
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"implicit screen results exceed workspace");
        std::size_t plans=0,templates=0;
        const auto cache_counts=[&](const auto& e){plans+=e.fp32_plans.size()+e.fp64_plans.size();templates+=e.fp32_templates.size()+e.fp64_templates.size();};
        cache_counts(*lazy_state()->serial_execution);for(const auto& e:lazy_state()->executions)cache_counts(*e);
        audio::trace::record("scheduled_implicit_parallel_preparation",[&](auto& f) {
            f.add("pulse_shaped",shaped);f.add("coordinate_environment",lazy_reference_environment());
            f.add("realization_fhss_shape",scheduled->fhss_shape.enabled);
            f.add("workers",lazy_state()->parallel_workers);f.add("peak_workers",lazy_state()->parallel_peak_workers);
            f.add("worker_slots",lazy_state()->actual_worker_slots);f.add("peak_worker_slots",lazy_state()->peak_actual_worker_slots);
            f.add("peak_parallel_jobs",lazy_state()->peak_parallel_jobs);
            f.add("groups",groups.size());f.add("ranges",ordered.size());f.add("workspace_limit",budget);
            f.add("contexts",lazy_state()->executions.size());f.add("plans",plans);f.add("template_pieces",templates);
            f.add("reserved_bytes",lazy_state()->parallel_reserved_bytes);f.add("retained_bytes",sizeof(PatternCorrelator)+accounted_bytes);
            f.add("serial_workspace_fallbacks",lazy_state()->serial_workspace_fallbacks);
            const auto& family=lazy_state()->frequency_family_work;
            f.add("frequency_family_groups",family.groups);f.add("frequency_moment_groups",family.moment_groups);
            f.add("frequency_moment_transforms",family.moment_transforms);f.add("frequency_synthesized_products",family.synthesized_products);
            f.add("frequency_recurrence_segments",family.recurrence_segments);f.add("frequency_trace_reuses",family.trace_inverse_reuses);
            f.add("frequency_trace_fallbacks",family.trace_reuse_fallbacks);f.add("frequency_fragments",family.fragments);
            f.add("maximum_numerical_fragment",family.maximum_fragment);
            f.add("frequency_native_float_accumulators",family.native_float_accumulators);
            f.add("frequency_wide_accumulators",family.wide_accumulators);
            std::uint64_t colored_pairs=0;std::size_t colored_ranges=0,colored_lanes=0,colored_valid=0,colored_exact=0,colored_fractional=0;
            std::array<std::size_t,8> colored_gates{};double colored_margin=1;
            for(const auto& range:lazy_state()->prepared_ranges){
                if(range.colored_gate<colored_gates.size())++colored_gates[range.colored_gate];
                if(range.colored_mode==1)colored_exact+=range.colored_selected;
                if(range.colored_mode==2){colored_fractional+=range.colored_selected;colored_margin=std::min(colored_margin,range.colored_lattice_margin);}
                if(range.colored.empty())continue;
                ++colored_ranges;colored_lanes+=range.colored_selected;colored_pairs+=range.colored_sample_pairs;
                for(const auto& pair:range.colored)if(pair[0].valid && pair[1].valid && pair[0].nested && pair[1].nested &&
                    pair[0].owner_set_certified && pair[1].owner_set_certified)++colored_valid;}
            f.add("colored_requested",lazy_colored_requested());f.add("colored_ranges",colored_ranges);
            f.add("colored_lanes",colored_lanes);f.add("colored_sample_pairs",colored_pairs);
            f.add("colored_valid",colored_valid);
            f.add("colored_exact_lanes",colored_exact);f.add("colored_fractional_lanes",colored_fractional);
            f.add("colored_minimum_lattice_margin",colored_margin);
            f.add("colored_unsupported_ranges",colored_gates[1]);f.add("colored_displacement_fallbacks",colored_gates[2]);
            f.add("colored_physical_margin_fallbacks",colored_gates[3]);f.add("colored_source_margin_fallbacks",colored_gates[5]);
            f.add("colored_no_selected_ranges",colored_gates[6]);
            f.add("colored_rejections",lazy_state()->colored_rejections);
            f.add("colored_on_demand_queries",lazy_state()->colored_demand_queries);f.add("colored_on_demand_sample_pairs",lazy_state()->colored_demand_pairs);
            f.add("colored_on_demand_fallbacks",lazy_state()->colored_demand_fallbacks);f.add("colored_source_builds",lazy_state()->colored_source_builds);
            f.add("colored_source_hits",lazy_state()->colored_source_hits);
            std::size_t source_bytes=0;for(const auto& row:lazy_state()->colored_sources)if(row)source_bytes+=row->working_bytes();
            f.add("colored_source_bytes",source_bytes);
        });
    }
    bool lazy_colored_demand_enabled() const {
        return lazy_state() && detail::scheduled_acquisition_policy::demand_requested() && lazy_colored_requested() &&
            config.search_arithmetic!=SearchArithmetic::fp64 && !options.scheduled_raw_score_reference &&
            shaped && !scheduled->fhss_shape.enabled && lazy_reference_environment();
    }
    void drop_lazy_colored_sources() {
        if(!lazy_state() || lazy_state()->colored_source_active)return;
        require(!lazy_state()->screens_running,"colored source cache eviction overlaps numerical workers");
        for(auto& source:lazy_state()->colored_sources)source.reset();
        lazy_state()->colored_source_touched={};
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
    }
    struct ColoredDemand {
        std::array<detail::scheduled_cold_colored::Summary,2> chips;
        double square=0,l1=0,gram=0;
        bool ready=false;
    };
    ColoredDemand lazy_colored_on_demand(const LazyState::DueRange& range,std::size_t j,
                                        std::stop_token stop={}) {
        namespace color=detail::scheduled_cold_colored;namespace cert=detail::scheduled_acquisition_certificate;
        namespace tile=detail::scheduled_acquisition_tile;
        if(!lazy_colored_requested() || !detail::scheduled_acquisition_policy::demand_requested())return {};
        ++lazy_state()->colored_demand_queries;
        require(!lazy_state()->colored_source_active,"nested colored source preparation");
        lazy_state()->colored_source_active=true;
        struct ColorPin {LazyState& s;~ColorPin(){s.colored_source_active=false;}} color_pin{*lazy_state()};
        unsigned failure_stage=0;
        static constexpr std::array<const char*,12> failure_names={"contract","metadata_workspace","arrival_lattice","membership_gate","row_bound","source_workspace","source_margin","row_geometry","runtime_workspace","history_missing","capture_missing","producer_error"};
        const auto fallback=[&] {
            ++lazy_state()->colored_demand_fallbacks;
            auto& count=lazy_state()->colored_demand_failure[failure_stage];
            if(++count==1)audio::trace::record("scheduled_colored_fallback",[&](auto& f){
                f.add("epoch",config.stream_epoch);f.add("reason",failure_names[failure_stage]);
                f.add("index",range.index);f.add("scope","first occurrence per reason and bank; original refinement remains required");
            });
            return ColoredDemand{};
        };
        try {
        if(!cert::coordinate_environment())return fallback();
        if(!shaped || scheduled->fhss_shape.enabled || j>=range.screen.size() || range.end<=range.first ||
           range.end-range.first>tile::maximum_start_span)return fallback();
        struct Reservation {Impl& self;std::size_t amount;Reservation(Impl& s,std::size_t n):self(s),amount(n){
                self.lazy_state()->transient_reservation+=amount;self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;}
            ~Reservation(){self.lazy_state()->transient_reservation-=amount;self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;}};
        failure_stage=1;
        const auto metadata=tile::add_size(8192,tile::multiply_size(2*(range.end-range.first),sizeof(std::size_t)));
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        if(sizeof(PatternCorrelator)+accounted_bytes>budget || metadata>budget-sizeof(PatternCorrelator)-accounted_bytes)return fallback();
        Reservation metadata_reservation(*this,metadata);
        auto& execution=lazy_execution();auto& pattern=execution.code;
        failure_stage=2;
        const auto stride=range.numeric_origin_stride;
        if(stride!=1 && stride!=2)return fallback();
        LazyState::DueRange proof;proof.family=range.family;proof.first=range.first+j%stride;
        proof.end=range.end;proof.index=range.index;
        const auto selected_index=j/stride;
        const auto reference=lazy_screen_reference(proof);std::vector<std::size_t> starts;
        starts.reserve(range.end-range.first);long double displacement=0;
        const auto first=lazy_state()->catalog->boundary(reference,static_cast<long double>(range.index)*pattern.symbol_samples(),false,execution.cursor);
        const auto end=lazy_state()->catalog->boundary(reference,static_cast<long double>(range.index+1)*pattern.symbol_samples(),false,execution.cursor);
        const auto aperture=std::max(std::abs(first-reference.origin),std::abs(end-reference.origin))+tile::maximum_start_span+2;
        const auto covered=1+(proof.end-proof.first-1)/stride;
        for(std::size_t k=0;k<covered;++k){const auto lane=lazy_family_lane(range.family,proof.first+k*stride);const auto integer=std::round(lane.origin-reference.origin);
            if(integer<0 || integer>=tile::maximum_start_span || (!starts.empty() && integer<=starts.back()))return fallback();
            starts.push_back(static_cast<std::size_t>(integer));
            displacement=std::max(displacement,std::abs(lane.rate*(reference.origin-lane.origin)+reference.rate*integer)+std::abs(lane.rate-reference.rate)*aperture);}
        displacement+=256*std::numeric_limits<long double>::epsilon()*(1+scheduled->complex_geometry.physical_aperture_samples+
            std::abs(reference.origin)+pattern.symbol_samples());
        const color::SourceRows::Key key{reference.origin,reference.anchor.fraction,reference.phase_lower,range.index,reference.anchor.sample};
        std::size_t slot=0;bool hit=false;long double source_translation=0;
        std::size_t cache_entries=lazy_state()->colored_sources.size();
        if(const auto* text=std::getenv("DATAPUMP_SCHED_COLORED_SOURCE_CACHE_ENTRIES")) {
            char* until=nullptr;const auto n=std::strtoul(text,&until,10);
            if(until!=text && *until=='\0' && n>=1 && n<=cache_entries)cache_entries=n;
        }
        const auto* translation_mode=std::getenv("DATAPUMP_SCHED_COLORED_SOURCE_TRANSLATION");
        const bool translated_reuse=!translation_mode || std::string_view(translation_mode)!="0";
        for(std::size_t i=0;i<cache_entries;++i)if(lazy_state()->colored_sources[i]) {
            const auto& previous=*lazy_state()->colored_sources[i];
            if(previous.key==key){slot=i;hit=true;break;}
            if(translated_reuse && reference.rate==1)if(const auto delta=previous.translation_to(key)) {
                slot=i;hit=true;source_translation=*delta;break;
            }
        }
        if(!hit) {
            slot=0;for(std::size_t i=0;i<cache_entries;++i) {
                if(!lazy_state()->colored_sources[i]){slot=i;break;}
                if(lazy_state()->colored_source_touched[i]<lazy_state()->colored_source_touched[slot])slot=i;
            }
        }
        if(hit && source_translation!=0) {
            // Central q/source/gain/mask are bit-identical under the exact
            // integer producer translation. Also reserve original-vs-proxy
            // coordinate arithmetic before either membership proof.
            displacement+=512*std::numeric_limits<long double>::epsilon()*(2+
                scheduled->complex_geometry.physical_aperture_samples+pattern.symbol_samples()+
                std::abs(reference.origin)+std::abs(lazy_state()->colored_sources[slot]->key.origin)+std::abs(source_translation));
        }
        failure_stage=3;
        if(selected_index>=starts.size() || !lazy_colored_proxy_eligible(reference,proof,starts,displacement,stride))return fallback();
        failure_stage=4;
        const auto bound=pattern.symbol_samples()+4;
        if(bound>color::SourceRows::maximum_rows)return fallback();
        constexpr std::size_t fragment_samples=512;
        const auto scratch=tile::add_size(lazy_colored_scratch(1,fragment_samples),
            tile::multiply_size(fragment_samples,sizeof(float)+sizeof(Complex)+2*sizeof(std::uint8_t)));
        auto needed=tile::add_size(scratch,hit?0:color::SourceRows::reservation(bound));
        const auto affordable=[&] {accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            return sizeof(PatternCorrelator)+accounted_bytes<=budget && needed<=budget-sizeof(PatternCorrelator)-accounted_bytes;};
        failure_stage=5;
        while(!affordable()) {
            // Only optional LRU victims, never required state or a borrowed
            // row. Keep the matched row while other rows can supply headroom.
            std::optional<std::size_t> victim;
            for(std::size_t i=0;i<lazy_state()->colored_sources.size();++i)
                if(lazy_state()->colored_sources[i] && (!hit || i!=slot) &&
                   (!victim || lazy_state()->colored_source_touched[i]<lazy_state()->colored_source_touched[*victim]))victim=i;
            if(!victim && hit){victim=slot;hit=false;source_translation=0;
                needed=tile::add_size(scratch,color::SourceRows::reservation(bound));}
            if(!victim)return fallback();
            lazy_state()->colored_sources[*victim].reset();lazy_state()->colored_source_touched[*victim]=0;++lazy_state()->colored_source_evictions;
        }
        Reservation reservation(*this,needed);
        const auto phase=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(reference.phase_lower);
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config),qmax=cert::up(pattern_pcm_radius_limit/amplitude);
        static const auto pulse_slope=[] {double value=0;for(unsigned i=0;i<2048;++i)value=std::max(value,
            std::abs(pattern_pulse((i+1)/256.)-pattern_pulse(i/256.))*256);return std::nextafter(value*1.000000000001,std::numeric_limits<double>::infinity());}();
        const auto gate_slope=cert::divide(1.875,std::max(1.,std::ceil(config.sample_rate*config.scheduled_hopping->ramp_seconds)));
        const auto jump=cert::up(4*std::abs(pattern_pulse(8.)));
        if(!hit) {
            cancelled(stop);
            const auto shift=static_cast<std::uint64_t>(std::ceil(std::max(0.L,-reference.origin))+fragment_samples);
            auto source=std::make_unique<color::SourceRows>(key,shift,bound);
            if(source->working_bytes()>color::SourceRows::reservation(bound))return fallback();
            pattern.set_stream_phase_samples(reference.phase_lower);
            require(range.index<=std::numeric_limits<std::uint64_t>::max()/pattern.chips_per_symbol(),"colored source chip index overflow");
            const auto chip_first=range.index*pattern.chips_per_symbol();pattern.prepare_symbol(chip_first,stop);
            auto v=static_cast<std::uint64_t>(std::max(0.L,std::floor(first+shift)-1));
            const auto until=static_cast<std::uint64_t>(std::max(0.L,std::ceil(end+shift)+2));
            while(v<until) {
                cancelled(stop);
                const auto p=static_cast<long double>(v)-shift;
                const auto tick=phase.add_signed((p-reference.origin)*reference.rate);const auto location=scheduled_map()->locate(tick);
                if(!location.active) {
                    const auto next=scheduled_map()->source_to_physical(location.source,false);
                    const auto real=reference.origin+next.distance(phase)/reference.rate;
                    const auto target=static_cast<std::uint64_t>(std::max(0.L,std::floor(real+shift)-1));
                    if(target>v+1){v=std::min(target,until);continue;}
                }else {
                    const auto coordinate=location.source.distance(reference.anchor)-static_cast<long double>(range.index)*pattern.symbol_samples();
                    const auto fraction=coordinate-std::floor(coordinate);
                    source->source_margin=std::min(source->source_margin,std::min(fraction,1-fraction));
                    if(coordinate>=0 && coordinate<pattern.symbol_samples()) {
                        const auto gain=SchedHopSourceMap::gain(location,tick);std::array<Complex,2> q{};std::uint8_t mask=0;
                        if(gain>0){const auto shaped_q=pattern.shaped_values(chip_first,static_cast<double>(coordinate));mask=1;
                            for(unsigned bit=0;bit<2;++bit)q[bit]=gain*pattern_limit_pcm(amplitude*shaped_q[bit])/amplitude;}
                        source->append(v,q,coordinate,gain,mask);
                    }
                }
                ++v;
            }
            source->seal();cancelled(stop);lazy_state()->colored_sources[slot]=std::move(source);++lazy_state()->colored_source_builds;
        }else {++lazy_state()->colored_source_hits;if(source_translation!=0)++lazy_state()->colored_source_translation_hits;}
        if(lazy_state()->colored_source_revision==std::numeric_limits<std::uint64_t>::max()){lazy_state()->colored_source_touched={};lazy_state()->colored_source_revision=0;}
        lazy_state()->colored_source_touched[slot]=++lazy_state()->colored_source_revision;
        auto& source=*lazy_state()->colored_sources[slot];
        failure_stage=6;
        if(!(source.source_margin>displacement))return fallback();
        std::array<std::uint8_t,fragment_samples> active{};active.fill(1);
        failure_stage=7;
        if(!source.geometry_ready || source.geometry_delta<displacement) {
            source.geometry_ready=false;double square=0,l1=0,gram=0;
            for(std::size_t at=0;at<source.rows.size();) {
                cancelled(stop);const auto n=std::min(fragment_samples,source.rows.size()-at);
                const auto rows=std::span<const std::array<Complex,2>>(source.rows).subspan(at,n);
                const auto coordinates=std::span<const long double>(source.source).subspan(at,n);
                const auto gains=std::span<const double>(source.gain).subspan(at,n);
                const auto masks=std::span<const std::uint8_t>(source.mask).subspan(at,n);
                const auto meta=std::span<color::ProxyRow>(source.geometry).subspan(at,n);
                if(!lazy_colored_proxy_rows(reference,proof,rows,coordinates,gains,std::span<const std::uint8_t>(active).first(n),masks,meta,
                    displacement,qmax,pulse_slope,gate_slope,jump))return fallback();
                for(std::size_t i=0;i<n;++i)if(meta[i].owner!=color::no_owner){const auto e=meta[i].residual,q=std::max(source.norms[at+i][0],source.norms[at+i][1]);
                    square=cert::add(square,cert::mul(e,e));l1=cert::add(l1,e);gram=cert::add(gram,cert::add(cert::mul(2*q,e),cert::mul(e,e)));}
                at+=n;
            }
            cancelled(stop);source.geometry_delta=displacement;source.geometry_square=square;source.geometry_l1=l1;source.geometry_gram=gram;source.geometry_ready=true;
        }
        failure_stage=8;
        const auto* error_mode=std::getenv("DATAPUMP_SCHED_COLORED_ERROR_PLANES");
        const bool fast_errors=!error_mode || std::string_view(error_mode)!="0";
        color::RuntimeProxy direct(1,pattern.chip_samples(),lazy_state()->summary_union_energy,lazy_colored_carrier_cap,2,block_samples,0,fast_errors);
        if(fast_errors)++lazy_state()->colored_fast_error_queries;
        if(direct.working_bytes()>lazy_colored_scratch(1,fragment_samples))return fallback();
        std::array<float,fragment_samples> pcm{};std::array<Complex,fragment_samples> oscillator{};
        std::array<std::uint8_t,fragment_samples> observed{};const std::array<std::size_t,1> selected{0};
        ColoredDemand result;result.square=source.geometry_square;result.l1=source.geometry_l1;result.gram=source.geometry_gram;const auto lane=lazy_family_lane(range.family,range.first+j);
        const auto frequency=banks[lane.frequency].frequency;const auto step=std::polar(1.,tau*frequency/config.sample_rate);
        // Use the exact ORIGINAL replay interval. Negative virtual-prefix rows
        // were never captured. Retired positive required PCM is still unknown
        // and must fall back; neither omission creates observations or absence.
        const auto observed_begin=static_cast<std::uint64_t>(std::max(0.L,std::ceil(lazy_state()->catalog->boundary(lane,
            static_cast<long double>(range.index)*pattern.symbol_samples(),false,execution.cursor))));
        const auto observed_end=lazy_state()->catalog->due(lane,range.index,execution.cursor);
        for(const auto& run:source.runs)for(std::size_t offset=0;offset<run.count;) {
            cancelled(stop);
            const auto v=tile::add_coordinate(run.physical_offset,offset);
            const auto real=(static_cast<long double>(v)-source.shift)+source_translation+starts[selected_index];
            failure_stage=2;if(std::trunc(real)!=real)return fallback();
            if(real<observed_begin) {
                const auto excluded=std::min<std::size_t>(run.count-offset,
                    static_cast<std::size_t>(std::ceil(static_cast<long double>(observed_begin)-real)));
                require(excluded>0,"colored observation crop did not advance");
                offset+=excluded;lazy_state()->colored_demand_excluded_rows+=excluded;continue;
            }
            if(real>=observed_end){lazy_state()->colored_demand_excluded_rows+=run.count-offset;break;}
            const auto n=std::min({fragment_samples,run.count-offset,static_cast<std::size_t>(observed_end-real)}),at=run.first+offset;
            failure_stage=9;if(real<lazy_history_first())return fallback();
            failure_stage=10;if(real+n>lazy_state()->capture_end)return fallback();
            const auto first_sample=static_cast<std::uint64_t>(real),end_sample=tile::add_coordinate(first_sample,n);
            observed.fill(0);
            lazy_visit_blocks(first_sample,end_sample,[&](const auto& block){
                const auto a=std::max(first_sample,block.first),b=std::min(end_sample,block.first+block.pcm.size());
                auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                for(auto i=block.first;i<a;++i)o*=step;
                for(auto i=a;i<b;++i){const auto index=static_cast<std::size_t>(i-first_sample);pcm[index]=block.pcm[i-block.first];oscillator[index]=o;observed[index]=1;
                    if(cert::norm(o)>2)throw Error("colored demand carrier exceeded original bound");o*=step;}
            },false);
            const auto rows=std::span<const std::array<Complex,2>>(source.rows).subspan(at,n);
            const auto masks=std::span<const std::uint8_t>(source.mask).subspan(at,n);
            const auto meta=std::span<const color::ProxyRow>(source.geometry).subspan(at,n);
            const auto norms=std::span<const std::array<double,2>>(source.norms).subspan(at,n);
            direct.add({std::span<const float>(pcm).first(n),std::span<const Complex>(oscillator).first(n),std::span<const std::uint8_t>(observed).first(n),v},
                {rows,masks,v},selected,meta,stop,norms);offset+=n;
        }
        std::size_t cache_bytes=0;for(const auto& row:lazy_state()->colored_sources)if(row)cache_bytes+=row->working_bytes();
        lazy_state()->colored_source_peak_bytes=std::max(lazy_state()->colored_source_peak_bytes,cache_bytes);
        auto complete=direct.finish();result.chips=complete.front();result.ready=true;lazy_state()->colored_demand_pairs+=direct.sample_pairs();
        return result;
        }catch(const Error&){cancelled(stop);failure_stage=11;return fallback();}
        catch(const std::bad_alloc&){cancelled(stop);failure_stage=5;return fallback();}
    }
    double lazy_cold_upper(const LazyState::Catalog::Lane& lane,const LazyState::DueRange& range,certdiag::Probe* probe=nullptr, double original_threshold=std::numeric_limits<double>::quiet_NaN(),
            double* raw_upper=nullptr,std::stop_token stop={}) {
        namespace cert=detail::scheduled_acquisition_certificate;
        const auto prepared=std::find_if(lazy_state()->batch_ranges.begin(),lazy_state()->batch_ranges.end(),[&](const auto& item){
            return item.family==range.family && item.index==range.index && lane.origin_index>=item.screen_first &&
                lane.origin_index-item.screen_first<item.screen.size();});
        const auto& source=prepared==lazy_state()->batch_ranges.end()?range:*prepared;
        if(source.screen.empty() || lane.origin_index<source.screen_first || lane.origin_index-source.screen_first>=source.screen.size()){
            if(probe)lazy_state()->certificate_profile.at(probe->cohort).failure.observe(static_cast<std::size_t>(certdiag::Failure::no_screen));return cert::infinity;}
        if(probe)probe->cohort=source.certificate_cohort;
        const auto j=lane.origin_index-source.screen_first;const auto& value=source.screen[j];
        if(probe){auto& diagnostic=lazy_state()->certificate_profile.at(probe->cohort);
            if(value.certificate.valid)certdiag::add(diagnostic.valid_certificates,1,diagnostic.saturated);
            diagnostic.observe(certdiag::Metric::cert_energy_lower,value.certificate.energy_lower);
            diagnostic.observe(certdiag::Metric::cert_energy_upper,value.certificate.energy_upper);}
        auto certificate=value.certificate;
        // These are bounds for a cold REJECTION ONLY. Proxy count/energy are
        // not installed into receiver Fits or counted as observed absence.
        const auto mask_energy_debit=cert::mul(static_cast<double>(source.mask_difference),cert::mul(source.maximum,source.maximum));
        certificate.energy_lower=std::max(0.,cert::down(certificate.energy_lower-mask_energy_debit));
        if(probe)lazy_state()->certificate_profile.at(probe->cohort).observe(certdiag::Metric::mask_energy_debit,mask_energy_debit);
        const auto carrier_radius=lazy_carrier_restart(lane.frequency);
        if(!std::isfinite(carrier_radius)){if(probe)lazy_state()->certificate_profile.at(probe->cohort).failure.observe(
            static_cast<std::size_t>(certdiag::Failure::carrier_nonfinite));return cert::infinity;}
        if(probe)lazy_state()->certificate_profile.at(probe->cohort).observe(certdiag::Metric::carrier_radius,carrier_radius);
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
        const auto qmax=cert::up(pattern_pcm_radius_limit/amplitude);
        ColoredDemand demand;
        bool demand_attempted=false;
        const auto stored=j<source.colored.size() && source.colored_mode && source.colored[j][0].owner_set_certified && source.colored[j][1].owner_set_certified;
        double upper=0,raw=0;
        for(unsigned bit=0;bit<2;++bit){auto fit=value.fits[bit];
            if(source.mask_difference>std::numeric_limits<std::uint64_t>::max()-fit.count){if(probe)lazy_state()->certificate_profile.at(probe->cohort).failure.observe(
                static_cast<std::size_t>(certdiag::Failure::count_overflow));return cert::infinity;}
            fit.count+=source.mask_difference;
            auto original=source.reference[j];const auto n=static_cast<double>(fit.count);
            // The template-proxy error is measured against the SAME original
            // carrier used by the tile certificate. Its original rounded
            // recurrence has magnitude <=2 in this qualified reference path.
            // Retain the restart bound below separately, including its cross
            // terms. No proxy score is installed into observed receiver state.
            certdiag::ProxyDetail proxy_detail;
            const auto proxy=detail::scheduled_template_proxy_certificate::errors(value.fits[bit],
                value.certificate,bit,source.proxy_residual_square,2,source.mask_difference,source.maximum,
                source.proxy_residual_l1,source.proxy_residual_gram,probe?&proxy_detail:nullptr);
            if(!proxy.valid){if(probe)lazy_state()->certificate_profile.at(probe->cohort).failure.observe(
                static_cast<std::size_t>(certdiag::Failure::proxy_invalid));return cert::infinity;}
            original.dot=cert::add(original.dot,proxy.dot);
            original.gram=cert::add(original.gram,proxy.gram);
            original.dot=cert::add(original.dot,cert::mul(carrier_radius*qmax,cert::root(cert::mul(n,lazy_state()->summary_union_energy))));
            original.gram=cert::add(original.gram,cert::mul(n*qmax*qmax,cert::add(
                cert::mul(2*lazy_state()->carrier_restart_magnitude[lane.frequency],carrier_radius),cert::mul(carrier_radius,carrier_radius))));
            certdiag::ScoreDetail score_detail;
            auto bit_upper=cert::score_upper(fit,certificate,bit,original,probe?&score_detail:nullptr);
            raw=std::max(raw,bit_upper);
            if(probe){auto& diagnostic=lazy_state()->certificate_profile.at(probe->cohort);
                diagnostic.failure.observe(static_cast<std::size_t>(score_detail.failure));
                if(original.valid)certdiag::add(diagnostic.valid_references,1,diagnostic.saturated);
                diagnostic.observe(certdiag::Metric::cert_dot,value.certificate.dot_radius[bit]);
                diagnostic.observe(certdiag::Metric::cert_gram,value.certificate.gram_radius[bit]);
                diagnostic.observe(certdiag::Metric::reference_dot,original.dot);diagnostic.observe(certdiag::Metric::reference_gram,original.gram);
                diagnostic.observe(certdiag::Metric::reference_energy,original.energy);
                if(proxy_detail.available){diagnostic.observe(certdiag::Metric::proxy_radius,proxy_detail.radius);
                    diagnostic.observe(certdiag::Metric::proxy_dot_l1,proxy_detail.dot_l1);diagnostic.observe(certdiag::Metric::proxy_dot_l2,proxy_detail.dot_l2);
                    diagnostic.observe(certdiag::Metric::proxy_gram_l1,proxy_detail.gram_l1);diagnostic.observe(certdiag::Metric::proxy_gram_l2,proxy_detail.gram_l2);}
                if(score_detail.has_gram)diagnostic.observe(certdiag::Metric::determinant_lower,score_detail.determinant_lower);
                if(score_detail.has_energy)diagnostic.observe(certdiag::Metric::effective_energy_lower,score_detail.energy_lower);
                if(score_detail.has_fraction)diagnostic.observe(certdiag::Metric::explained_fraction,score_detail.fraction);
            }
            // Do not run an approximate/proxy guard just to discard its result
            // in the independent original-template diagnostic path.
            if(std::isfinite(original_threshold) && !stored && !demand_attempted && bit_upper>=original_threshold &&
               config.search_arithmetic!=SearchArithmetic::fp64 && !options.scheduled_raw_score_reference &&
               carrier_radius<=lazy_colored_carrier_cap && lazy_state()->carrier_restart_magnitude[lane.frequency]<=2) {
                demand_attempted=true;demand=lazy_colored_on_demand(source,j,stop);
            }
            if(std::isfinite(original_threshold) && lazy_colored_requested() && (stored || demand.ready) &&
               carrier_radius<=(demand.ready?lazy_colored_carrier_cap:source.colored_carrier_radius) &&
               lazy_state()->carrier_restart_magnitude[lane.frequency]<=(demand.ready?2:source.colored_carrier_magnitude) &&
               !options.scheduled_raw_score_reference && config.search_arithmetic!=SearchArithmetic::fp64) {
                // Summary state is only a proof for this original event.
                // Both enabled producers certify exact ORIGINAL memberships.
                // Keep the old raw bound unchanged, but avoid its independent
                // coarse mask-count allowance in this stronger cold proof.
                auto colored_original=source.reference[j];
                const auto colored_proxy=detail::scheduled_template_proxy_certificate::errors(value.fits[bit],
                    value.certificate,bit,demand.ready?demand.square:source.colored_residual_square,2,0,source.maximum,
                    demand.ready?demand.l1:source.colored_residual_l1,demand.ready?demand.gram:source.colored_residual_gram);
                if(colored_proxy.valid) {
                    const auto count=static_cast<double>(value.fits[bit].count);
                    colored_original.dot=cert::add(colored_original.dot,colored_proxy.dot);
                    colored_original.gram=cert::add(colored_original.gram,colored_proxy.gram);
                    colored_original.dot=cert::add(colored_original.dot,cert::mul(carrier_radius*qmax,
                        cert::root(cert::mul(count,lazy_state()->summary_union_energy))));
                    colored_original.gram=cert::add(colored_original.gram,cert::mul(count*qmax*qmax,cert::add(
                        cert::mul(2*lazy_state()->carrier_restart_magnitude[lane.frequency],carrier_radius),cert::mul(carrier_radius,carrier_radius))));
                    bit_upper=detail::scheduled_cold_colored::rejection_upper(bit_upper,value.fits[bit],value.certificate,
                        bit,colored_original,demand.ready?demand.chips[bit]:source.colored[j][bit],original_threshold,{colored_original.log_error,true});
                }
            }
            upper=std::max(upper,bit_upper);
        }
        if(raw_upper)*raw_upper=raw;
        if(probe){probe->raw_upper=raw;probe->guarded_bound=upper<raw;}
        return upper;
    }

    // Pure historical diagnostic scoring for a completed cold coherent lane.
    // Replay uses a distinct unregistered scratch owner. It never completes a
    // receiver symbol, changes trial charges, admits a bit, subscribes timing,
    // publishes progress, advances source indices, or classifies absence.
    PatternEvidence lazy_diagnostic_evidence(std::size_t id,std::uint64_t index,
            std::uint64_t event,std::uint64_t original_trials,std::stop_token stop) {
        require(lazy_state() && drift_sections==1 && !differential_window && !options.amplitude_only,
            "implicit diagnostic replay requires the original coherent statistic");
        require(original_trials && event,"implicit diagnostic event lacks original trial identity");
        cancelled(stop);const auto lane=lazy_state()->catalog->describe(id,*lazy_state()->cursor);
        constexpr std::size_t slot=0;
        require(lazy_state()->occupied[slot] && lazy_state()->logical[slot]==std::numeric_limits<std::size_t>::max(),
            "implicit reserved diagnostic scratch is in use");
        lazy_reset_slot(slot,lane,index,false);
        struct Scratch {
            Impl& s;std::size_t slot;
            ~Scratch(){s.hypotheses[slot]={};s.chip_evidence[slot]={};
                s.scheduled->scheduled_carry[slot]={};s.scheduled->scheduled_carry_chips[slot]={};
                if(s.outer_presence){(*s.outer_evidence)[slot]={};s.scheduled->scheduled_carry_outer[slot]={};}
                s.scheduled->symbol_ends[slot]=std::numeric_limits<long double>::infinity();
                s.scheduled->template_uncached[slot]=false;s.lazy_state()->replayed[slot]=0;
                s.lazy_state()->occupied[slot]=1;s.lazy_state()->logical[slot]=std::numeric_limits<std::size_t>::max();}
        } scratch{*this,slot};
        const auto end=lazy_state()->catalog->due(lane,index,*lazy_state()->cursor);
        lazy_replay_to(slot,end,stop);auto& h=hypotheses[slot];
        const auto t=static_cast<double>(original_trials);
        const auto original_threshold=-std::log(search.false_alarm_probability)+std::log(t)+std::log(t+1)+std::log(2.);
        std::array<double,2> scores{};
        for(unsigned bit=0;bit<2;++bit){const auto raw=h.fits[bit].score();const auto& chips=chip_evidence[slot][bit];
            scores[bit]=!options.scheduled_raw_score_reference && chips.colored_excess(h.fits[bit],original_threshold)?
                std::min(raw,chips.score(h.fits[bit])):raw;}
        PatternEvidence evidence{h.observed_start,end,index,config.carrier_hz+search.frequency_offsets_hz[h.frequency],
            std::max(scores[0],scores[1]),std::min(scores[0],scores[1]),scores[1]>scores[0]?1U:0U,h.phase_lower};
        evidence.completed_event=event;evidence.admission_threshold=original_threshold;
        if(outer_presence){code.set_stream_phase_samples(h.phase_lower);
            require(index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),"implicit diagnostic chip coordinate overflow");
            const auto chip=index*code.chips_per_symbol();code.prepare_symbol(chip,stop);auto& outer=(*outer_evidence)[slot];
            outer.finish([&](std::uint64_t local){return code.values(chip+local);});
            evidence.outer_presence_score=outer.score(evidence.bit);
            evidence.outer_presence_active=outer.can_admit(evidence.bit,original_threshold) &&
                chip_evidence[slot][evidence.bit].colored_excess(h.fits[evidence.bit],original_threshold);
        }
        require(evidence.score<original_threshold,"implicit deferred diagnostic violates certified original admission rejection");
        return evidence;
    }
    void lazy_remember_diagnostic(PatternEvidence evidence) {
        if(evidence.score<search.retain_score)return;
        const auto existing=std::lower_bound(history.begin(),history.end(),evidence.completed_event,
            [](const auto& e,std::uint64_t ordinal){return e.completed_event<ordinal;});
        if(existing!=history.end() && existing->completed_event==evidence.completed_event){*existing=evidence;return;}
        // An older resolved event cannot displace C already retained newer
        // events. Leave the exact FIFO unchanged in that case.
        if(history.size()==search.candidate_limit && !history.empty() && evidence.completed_event<history.front().completed_event)return;
        if(history.size()==search.candidate_limit)history.erase(history.begin());
        require(history.size()<history.capacity(),"implicit diagnostic FIFO reservation exhausted");
        const auto where=std::lower_bound(history.begin(),history.end(),evidence.completed_event,
            [](const auto& e,std::uint64_t ordinal){return e.completed_event<ordinal;});
        history.insert(where,evidence);accounted_bytes=working_bytes()+unallocated_history_bytes();
    }

    // Resolve an exact original last-C FIFO. Requested public display limits do
    // not authorize retiring anything older than C confirmed newer retainers.
    // This owner-only query changes no acquisition/admission/timing/END state.
    void lazy_resolve_diagnostics(std::stop_token stop) {
        if(!lazy_state() || !lazy_state()->diagnostics || !lazy_state()->diagnostics->oldest())return;
        require(!lazy_state()->screens_running,"diagnostic replay cannot overlap numeric workers");
        auto cursor=lazy_state()->diagnostics->newest_first();
        while(const auto event=lazy_state()->diagnostics->peek(cursor,stop)) {
            cancelled(stop);
            if(history.size()==search.candidate_limit && !history.empty() &&
               event->ordinal<history.front().completed_event)break;
            const auto existing=std::lower_bound(history.begin(),history.end(),event->ordinal,
                [](const auto& evidence,std::uint64_t ordinal){return evidence.completed_event<ordinal;});
            if(existing==history.end() || existing->completed_event!=event->ordinal) {
                require(event->processing_block_end<=lazy_state()->processing->processing_fences().end(),
                    "deferred diagnostic original processing context is unavailable");
                auto evidence=lazy_diagnostic_evidence(static_cast<std::size_t>(event->global_lane),
                    event->source_index,event->ordinal,event->original_trials,stop);
                cancelled(stop);lazy_remember_diagnostic(evidence);++lazy_state()->diagnostic_materializations;
                detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::diagnostic_materialized,event->ordinal);
            }
            lazy_state()->diagnostics->advance(cursor,*event);
        }
        cancelled(stop);
        // Reaching the oldest event resolved all pending records; an early
        // break proves C strictly newer exact retained events exclude the rest.
        lazy_state()->diagnostics->discard_resolved_through(cursor.through);
        lazy_state()->diagnostic_first=std::numeric_limits<std::uint64_t>::max();
        ++lazy_state()->diagnostic_resolutions;accounted_bytes=working_bytes()+unallocated_history_bytes();
    }
    void lazy_defer_diagnostic(const LazyState::Catalog::Lane& lane,std::uint64_t index,
            std::uint64_t event,std::uint64_t original_trials,std::uint64_t block_end,std::stop_token stop) {
        detail::scheduled_diagnostics::Event pending{event,lane.id,index,original_trials,block_end};
        const auto first=lazy_state()->catalog->boundary(lane,static_cast<long double>(index)*code.symbol_samples(),false,*lazy_state()->cursor);
        const auto retained_first=static_cast<std::uint64_t>(std::max(0.L,std::ceil(first)));
        require(lazy_state()->deferred_diagnostics<std::numeric_limits<std::uint64_t>::max(),"deferred diagnostic counter overflow");
        if(!lazy_state()->diagnostics->can_append(pending))lazy_resolve_diagnostics(stop);
        lazy_state()->diagnostics->append_cold(pending);
        lazy_state()->diagnostic_first=std::min(lazy_state()->diagnostic_first,retained_first);
        ++lazy_state()->deferred_diagnostics;
    }

    void lazy_grow_processing_metadata(detail::scheduled_processing::LedgerKind kind,std::stop_token stop) {
        cancelled(stop);
        require(!lazy_state()->screens_running,"processing metadata growth requires joined numeric workers");
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        const auto before=lazy_state()->processing->accounting().reserved_bytes;
        const auto changed=lazy_state()->processing->grow(kind,[&](std::size_t bytes){room_for(bytes);},stop);
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        cancelled(stop);require(changed,"processing metadata growth was cancelled");
        audio::trace::record("scheduled_metadata_growth",[&](auto& f){
            f.add("ledger",static_cast<unsigned>(kind));f.add("before_bytes",before);
            f.add("after_bytes",lazy_state()->processing->accounting().reserved_bytes);
            f.add("processed",lazy_state()->processed);f.add("captured",lazy_state()->capture_end);
        });
    }
    void flush_lazy(std::stop_token stop) {
        if(lazy_state()->commit_fault)std::rethrow_exception(lazy_state()->commit_fault);
        if(lazy_state()->processed==lazy_state()->capture_end)return;
        const auto saved_capture=lazy_state()->capture_end;
        struct RestoreCapture {Impl& s;std::uint64_t capture;~RestoreCapture(){s.sample=capture;}} restore_capture{*this,saved_capture};
        while(lazy_state()->processed<saved_capture) {
            cancelled(stop);
            if(!lazy_state()->screen_capture_end) {
                require(!lazy_state()->committing_block,"implicit pending block lost its numerical batch");
                lazy_state()->block_end=saved_capture;
                if(static_cast<long double>(saved_capture)>=lazy_state()->next_due) {
                    (void)lazy_due_ranges(lazy_state()->processed,saved_capture,stop);
                    lazy_prepare_screens(stop);
                    detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::numeric_prepared,saved_capture);
                    cancelled(stop);
                    lazy_state()->batch_ranges.swap(lazy_state()->prepared_ranges);lazy_state()->prepared_ranges.clear();lazy_state()->due_ranges.clear();
                }
                lazy_state()->screen_capture_end=saved_capture;
            }
            const auto batch_end=lazy_state()->screen_capture_end;
            bool waiting_for_capture=false;
            while(lazy_state()->processed<batch_end) {
                cancelled(stop);sample=lazy_state()->processed;
                std::optional<detail::scheduled_processing::Block> prepared;
                for(;;) {
                    try {
                        prepared=lazy_state()->processing->prepare([&](std::size_t maximum){return process_count(maximum);},
                            [&](std::uint64_t position) {
                                detail::scheduled_processing::Fence found{};
                                lazy_visit_fences(position,position+1,[&](const auto& raw){found={raw.first,raw.count,position,raw.first+raw.count-position};});
                                return found;
                            },stop,batch_end);
                        break;
                    } catch(const detail::scheduled_processing::CapacityError& error) {
                        // prepare preflights all ledgers before publication;
                        // retry preserves its count, token and original fences.
                        lazy_grow_processing_metadata(error.kind,stop);
                    }
                }
                if(!prepared){cancelled(stop);waiting_for_capture=true;break;}
                const auto block_first=prepared->first,block_end=prepared->first+prepared->count;
                lazy_state()->block_end=block_end;sample=block_first;
                if(lazy_state()->committing_block)require(*lazy_state()->committing_block==*prepared,
                    "implicit continuation changed the original prepared processing block");
                else {
                    auto next=lazy_state()->next_due;
                    if(static_cast<long double>(block_end)>=lazy_state()->next_due)next=lazy_due_ranges(block_first,block_end,stop);
                    else lazy_state()->due_ranges.clear();
                    lazy_state()->committing_next_due=next;lazy_state()->committing_block=*prepared;
                }
                lazy_for_each_due([&](const auto& lane,std::uint64_t index,auto& range){
                    detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::before_event,lane.id);
                    cancelled(stop);
                    lazy_prepare_detached_replay(lane.id,index,stop);
                    certdiag::Probe certificate_probe;certdiag::Probe* probe=nullptr;
                    if(lazy_state()->certificate_profile.enabled && lazy_state()->certificate_pending_event){
                        const auto& pending=*lazy_state()->certificate_pending_event;
                        if(pending.lane==lane.id && pending.index==index){certificate_probe=pending;probe=&certificate_probe;}
                        else certdiag::add(lazy_state()->certificate_profile.pair_identity_mismatches,1,lazy_state()->certificate_profile.saturated);
                    }
                    if(!resident_slot(lane.id)) {
                        if(lazy_state()->certificate_profile.enabled){probe=&certificate_probe;probe->lane=lane.id;probe->index=index;}
                        require(trials<std::numeric_limits<std::uint64_t>::max(),"pattern trial counter overflow");
                        struct TrialReservation {std::uint64_t& trials;std::uint64_t previous;bool committed=false;
                            ~TrialReservation(){if(!committed)trials=previous;}} trial_reservation{trials,trials};
                        ++trials;
                        const auto admission_threshold=threshold();
                        double raw_upper=detail::scheduled_acquisition_certificate::infinity;
                        const auto upper=lazy_cold_upper(lane,range,probe,admission_threshold,&raw_upper,stop);
                        if(!std::isfinite(upper))++lazy_state()->invalid_certificates;
                        if(probe){probe->upper=upper;probe->threshold=admission_threshold;auto& diagnostic=lazy_state()->certificate_profile.at(probe->cohort);
                            certdiag::add(diagnostic.attempts,1,diagnostic.saturated);diagnostic.observe(certdiag::Metric::cold_upper,upper);}
                        if(upper<admission_threshold) {
                            require(lazy_state()->history_generation<std::numeric_limits<std::uint64_t>::max(),"implicit diagnostic ordinal overflow");
                            const auto event=lazy_state()->history_generation+1;
                            if(upper<search.retain_score)lazy_state()->diagnostics->observe_resolved(event);
                            else lazy_defer_diagnostic(lane,index,event,trials,block_end,stop);
                            lazy_state()->history_generation=event;trial_reservation.committed=true;++lazy_state()->screen_rejections;
                            if(raw_upper>=admission_threshold) {
                                ++lazy_state()->colored_rejections;
                                audio::trace::record("scheduled_implicit_colored_rejection",[&](auto& f) {
                                    f.add("lane",lane.id);f.add("index",index);f.add("trial",trials);
                                    f.add("threshold",admission_threshold);f.add("raw_upper",raw_upper);f.add("guarded_upper",upper);
                                    f.add("colored_rejections",lazy_state()->colored_rejections);
                                });
                            }
                            if(probe){auto& diagnostic=lazy_state()->certificate_profile.at(probe->cohort);certdiag::add(diagnostic.rejections,1,diagnostic.saturated);}
                            if(auto* prepared=lazy_detached_event(lane.id,index)){
                                lazy_clear_detached_replay_slot(prepared->slot);
                                prepared->slot=std::numeric_limits<std::size_t>::max();
                            }
                            return;
                        }
                    }
                    // One bounded current-event capability survives cancellable replay.
                    // Restoring it never reruns the old U or a rejection decision.
                    if(probe && (!lazy_state()->certificate_pending_event ||
                        (lazy_state()->certificate_pending_event->lane==lane.id && lazy_state()->certificate_pending_event->index==index)))
                        lazy_state()->certificate_pending_event=*probe;
                    const auto prepared_slot=lazy_adopt_detached_replay(lane.id,index);
                    const auto slot=prepared_slot?*prepared_slot:lazy_ensure_slot(lane.id,index);auto& h=hypotheses[slot];
                    require(h.index==index,"implicit acquisition resident and logical completion indices disagree");
                    const auto fence=lazy_state()->catalog->due(lane,index,*lazy_state()->cursor);
                    lazy_replay_to(slot,fence,stop);
                    cancelled(stop);code.set_stream_phase_samples(h.phase_lower);
                    code.prepare_symbol(index*code.chips_per_symbol(),stop);cancelled(stop);
                    // Original trials, guards, accepted-prefix/timing publication,
                    // source-index/carry and physical absence commit together.
                    const bool was_admitted=h.admitted;sample=block_first;
                    try {complete(h,slot,fence,{},probe);}
                    catch(...) {lazy_state()->commit_fault=std::current_exception();throw;}
                    if(probe){lazy_state()->certificate_profile.completed(*probe);
                        if(lazy_state()->certificate_pending_event && lazy_state()->certificate_pending_event->lane==lane.id &&
                           lazy_state()->certificate_pending_event->index==index)lazy_state()->certificate_pending_event.reset();}
                    else if(lazy_state()->certificate_profile.enabled)certdiag::add(lazy_state()->certificate_profile.resident_completions,1,lazy_state()->certificate_profile.saturated);
                    ++lazy_state()->refinements;if(!was_admitted && h.admitted)++lazy_state()->promotions;
                    ++lazy_state()->cold_completions;lazy_state()->replayed[slot]=fence;lazy_release_cold(slot);
                });
                // No stop separates the final consumer from its block frontier.
                lazy_drop_detached_replay();
                try {settle_timing();lazy_state()->processing->advance(*prepared);}
                catch(...) {lazy_state()->commit_fault=std::current_exception();throw;}
                lazy_state()->processed=block_end;lazy_state()->next_due=lazy_state()->committing_next_due;
                scheduled->earliest_end=lazy_state()->next_due;lazy_state()->committing_block.reset();lazy_state()->due_ranges.clear();
                detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::block_committed,block_end);
            }
            if(lazy_state()->processed==batch_end || waiting_for_capture) {
                require(!lazy_state()->committing_block,"implicit pending consumer cannot lose its original batch");
                lazy_state()->batch_ranges.clear();lazy_state()->due_ranges.clear();lazy_state()->screen_capture_end=0;
            }
            if(waiting_for_capture)break;
        }
        sample=saved_capture;lazy_state()->block_end=saved_capture;accounted_bytes=working_bytes()+unallocated_history_bytes();
        if(lazy_state()->processed==saved_capture)lazy_state()->pending_since={};
    }
    void push_lazy(std::span<const float> input,std::stop_token stop) {
        if(lazy_state()->commit_fault)std::rethrow_exception(lazy_state()->commit_fault);
        if(input.empty() && !lazy_state()->pending_push.active())return;
        std::size_t offset=0;
        if(lazy_state()->pending_push.active())offset=lazy_state()->pending_push.retry_offset(input,lazy_state()->capture_end);
        else {
            for(;;) {
                try {lazy_state()->processing->append_push(sample,input.size());break;}
                catch(const detail::scheduled_processing::CapacityError& error) {
                    lazy_grow_processing_metadata(error.kind,stop);
                }
            }
            lazy_state()->pending_push.begin(sample,input.size());
        }
        const bool original=!lazy_state()->pending_push.fingerprinted();
        try {
        if(offset==input.size() && lazy_state()->processed<lazy_state()->capture_end)flush_lazy(stop);
        for(;offset<input.size();){
            cancelled(stop);
            const auto count=std::min(block_samples,input.size()-offset);const auto first=sample,end=sample+count;
            const auto keep_after=end>lazy_state()->history_limit?end-lazy_state()->history_limit:0;
            const auto retire_before=std::min<std::uint64_t>(keep_after,lazy_state()->processed);
            if(lazy_state()->diagnostics->oldest() && retire_before>lazy_state()->diagnostic_first)
                lazy_resolve_diagnostics(stop);
            const auto virtual_floor=lazy_state()->processing->discard_before(retire_before);
            lazy_state()->summary_revision=std::numeric_limits<std::uint64_t>::max();
            if(lazy_state()->shared_capture) {
                require(end<=std::numeric_limits<std::uint64_t>::max()-search.input_sample_origin,
                    "shared capture absolute input coordinate overflow");
                require(lazy_state()->shared_capture->accounting().captured_end>=search.input_sample_origin+end,
                    "shared capture owner must append original PCM before bank input");
                const auto floor=lazy_state()->capture_fences->replay_floor(virtual_floor);
                lazy_state()->capture_fences->discard_before(floor);
                // Completed old-history retirement is independent of the new
                // append. Publish it before cancellable allocation so a retry
                // never summarizes already-retired oscillator fences.
                lazy_state()->history_first=floor;
                lazy_state()->capture_retention.retain_from(search.input_sample_origin+floor);
                for(;;) {
                    try {lazy_state()->capture_fences->append(first,count);break;}
                    catch(const detail::scheduled_capture::OriginalCaptureFences::CapacityError&) {
                        cancelled(stop);require(!lazy_state()->screens_running,"capture metadata growth requires joined numeric workers");
                        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                        const auto before=lazy_state()->capture_fences->reserved_bytes();
                        const auto changed=lazy_state()->capture_fences->grow([&](std::size_t bytes){room_for(bytes);},stop);
                        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                        cancelled(stop);require(changed,"capture metadata growth was cancelled");
                        audio::trace::record("scheduled_capture_metadata_growth",[&](auto& f){
                            f.add("before_bytes",before);f.add("after_bytes",lazy_state()->capture_fences->reserved_bytes());
                            f.add("processed",lazy_state()->processed);f.add("captured",lazy_state()->capture_end);
                        });
                    }
                }
            } else {
                while(!lazy_state()->capture.empty() && lazy_state()->capture.front().first+lazy_state()->capture.front().pcm.size()<=virtual_floor){
                    require(lazy_state()->capture.front().first+lazy_state()->capture.front().pcm.size()<=lazy_state()->processed,
                        "implicit queued work exceeds required retained history");
                    lazy_state()->history_samples-=lazy_state()->capture.front().pcm.size();
                    lazy_state()->capture_allocated_bytes-=sizeof(LazyState::Block)+128+lazy_state()->capture.front().pcm.capacity()*sizeof(float)+lazy_state()->capture.front().energy.capacity()*sizeof(double);
                    lazy_state()->capture.pop_front();}
                const auto new_capture_bytes=sizeof(LazyState::Block)+128+count*sizeof(float)+(count+1)*sizeof(double);
                const auto old_charge=std::max(lazy_state()->history_reservation,lazy_state()->capture_allocated_bytes);
                const auto new_charge=std::max(lazy_state()->history_reservation,lazy_state()->capture_allocated_bytes+new_capture_bytes);
                if(new_charge>old_charge)room_for(new_charge-old_charge);
                LazyState::Block block;block.first=first;block.pcm.assign(input.begin()+offset,input.begin()+offset+count);
                block.energy.resize(count+1);for(std::size_t i=0;i<count;++i){const auto x=static_cast<double>(block.pcm[i]);
                    block.energy[i+1]=block.energy[i]+x*x;block.maximum=std::max(block.maximum,std::abs(x));}
                lazy_state()->capture_allocated_bytes+=sizeof(LazyState::Block)+128+block.pcm.capacity()*sizeof(float)+block.energy.capacity()*sizeof(double);
                lazy_state()->capture.push_back(std::move(block));lazy_state()->history_samples+=count;
            }
            lazy_state()->pending_push.appended(input.subspan(offset,count));
            lazy_state()->block_end=end;lazy_state()->capture_end=end;
            if(lazy_state()->original_projections)lazy_state()->original_projections->discard_before(search.input_sample_origin+lazy_history_first());
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            work.samples+=count;lazy_state()->summary_revision=std::numeric_limits<std::uint64_t>::max();
            sample=end;offset+=count;
            detail::scheduled_resume::checkpoint(detail::scheduled_resume::Boundary::captured,end);
            cancelled(stop);
            if(static_cast<long double>(end)<lazy_state()->next_due)flush_lazy(stop);
            else {
                if(lazy_state()->pending_since==std::chrono::steady_clock::time_point{})lazy_state()->pending_since=std::chrono::steady_clock::now();
                if(end-lazy_state()->processed>=lazy_state()->batch_samples)flush_lazy(stop);
            }
            accounted_bytes=working_bytes()+unallocated_history_bytes();require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"implicit acquisition workspace exhausted");
            work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
        }
        lazy_state()->pending_push.clear();
        } catch(...) {
            if(stop.stop_requested() && !lazy_state()->commit_fault) {
                if(original)lazy_state()->pending_push.cancelled_original(input,lazy_state()->capture_end);
                if(lazy_state()->pending_since==std::chrono::steady_clock::time_point{})lazy_state()->pending_since=std::chrono::steady_clock::now();
            }
            throw;
        }
    }
    void poll_lazy(std::stop_token stop) {
        if(!lazy_state() || lazy_state()->processed==lazy_state()->capture_end || lazy_state()->pending_since==std::chrono::steady_clock::time_point{})return;
        const auto delay=std::chrono::duration<double>(std::chrono::steady_clock::now()-lazy_state()->pending_since).count();
        if(lazy_state()->committing_block || lazy_state()->screen_capture_end || lazy_state()->pending_push.active() ||
            delay>=static_cast<double>(lazy_state()->batch_samples)/config.sample_rate)flush_lazy(stop);
        if(lazy_state()->pending_push.active() && lazy_state()->capture_end==lazy_state()->pending_push.end() && lazy_state()->processed==lazy_state()->capture_end)
            lazy_state()->pending_push.clear();
    }

    std::uint64_t scheduled_cell_width(const Hypothesis& h) const {
        if(scheduled_calendar())return 0;
        return search.search_stream_phases?(options.amplitude_only?pattern_amplitude_phase_cell_upper(config,*amplitude_geometry,h.phase_lower)-h.phase_lower:
            std::min(phase_step-1,canonical_phase_extent(config)-1-h.phase_lower)):0;
    }
    std::uint64_t scheduled_index_at(std::uint64_t phase,long double position,bool fully_observed) const {
        if(!scheduled_calendar()) {
            const auto index=fully_observed?std::ceil(std::max(0.L,position)/code.symbol_samples()):
                std::floor(std::max(0.L,position)/code.symbol_samples());
            require(index<std::numeric_limits<std::uint64_t>::max(),"scheduled source index overflow");
            return static_cast<std::uint64_t>(index);
        }
        const auto sample=std::ceil(std::max(0.L,position));
        require(sample<=std::numeric_limits<std::uint64_t>::max(),"scheduled source coordinate overflow");
        auto index=scheduled_calendar()->lower_bound_source(config.stream_epoch,phase,static_cast<std::uint64_t>(sample));
        if(!fully_observed && index) {
            const auto previous=scheduled_calendar()->offset_source(config.stream_epoch,phase,index-1);
            if(static_cast<long double>(previous)+code.symbol_samples()>position)--index;
        }
        return index;
    }
    long double scheduled_source_start(const Hypothesis& h,std::uint64_t index) const {
        if(scheduled_calendar()) {
            const auto lane=static_cast<std::size_t>(&h-hypotheses.data());
            if(index==(scheduled->end_indices.empty()?h.index:scheduled->end_indices[lane]))return scheduled->source_starts[lane];
            return scheduled_calendar()->offset_source(config.stream_epoch,h.phase_lower,index);
        }
        return static_cast<long double>(index)*code.symbol_samples();
    }
    long double scheduled_uncertainty(const Hypothesis& h,long double physical) const {
        return scheduled->timing_radius+std::abs(physical-h.origin)*scheduled->clock_radius;
    }
    SchedHopCoordinate scheduled_tick(const Hypothesis& h,long double physical) const {
        return SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(h.phase_lower).add_signed((physical-h.origin)*h.rate);
    }
    long double source_position(const Hypothesis& h,long double physical) const {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::calendar,true);
        if(!scheduled_map())return (physical-symbol_start(h))*h.rate+static_cast<long double>(h.index)*code.symbol_samples();
        const auto at=scheduled_map()->locate(scheduled_tick(h,physical)).source;
        return at.distance(scheduled->anchors[static_cast<std::size_t>(&h-hypotheses.data())]);
    }
    long double scheduled_boundary(const Hypothesis& h,long double source,bool end) const {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::calendar,true);
        const auto index=static_cast<std::size_t>(&h-hypotheses.data());
        const auto target=scheduled->anchors[index].add_signed(source);
        const auto physical=scheduled_map()->source_to_physical(target,end);
        const auto anchor=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(h.phase_lower);
        const auto distance=physical.distance(anchor);
        const auto slow=h.rate-scheduled->clock_radius,fast=h.rate+scheduled->clock_radius;
        auto result=h.origin+distance/h.rate;
        if(end)result=h.origin+scheduled->timing_radius/h.rate+std::max(distance/slow,distance/fast);
        const auto width=scheduled_cell_width(h);
        if(end && width) {
            // Left inverse is discontinuous at a source-group boundary. Wait
            // for the latest endpoint in the represented canonical phase cell;
            // those boundary observations are omitted below, never reassigned.
            const auto upper_anchor=anchor.add_samples(width);
            const auto upper_source=scheduled_map()->locate(upper_anchor).source.add_signed(source);
            const auto upper_physical=scheduled_map()->source_to_physical(upper_source,true);
            const auto delta=upper_physical.distance(upper_anchor);
            result=std::max(result,h.origin+scheduled->timing_radius/h.rate+std::max(delta/slow,delta/fast));
        }
        return result;
    }
    bool scheduled_flat(const Hypothesis& h,std::uint64_t first,std::uint64_t end,bool whole_cell=true) const {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::calendar,true);
        if(!scheduled_map())return true;
        const auto width=whole_cell?scheduled_cell_width(h):0;
        const auto left=scheduled_tick(h,first).add_signed(whole_cell?-scheduled_uncertainty(h,first):0);
        const auto right=scheduled_tick(h,end).add_samples(width).add_signed(whole_cell?scheduled_uncertainty(h,end):0);
        const auto a=scheduled_map()->locate(left),b=scheduled_map()->locate(right.add_signed(-1e-9L));
        if(!a.active || !b.active || a.frame_index!=b.frame_index ||
           left.distance(SchedHopCoordinate{a.flat_begin,0})<0 ||
           right.distance(SchedHopCoordinate{a.flat_end,0})>0)return false;
        if(scheduled->fhss_shape.enabled) {
            // Use the transmitter's rounded guard/support and the same UTC
            // phase calculation. Recomputing unrounded seconds could admit
            // nearly two samples that still lie inside the final power ramp.
            const auto two=2.L*config.sample_rate;
            const auto local=std::fmod(static_cast<long double>(left.sample%(2ULL*config.sample_rate))+
                left.fraction+scheduled->fhss_shape.lag_center,two);
            const auto period=two/5;
            const auto position=local-std::floor(local*5/two)*period;
            const auto length=right.distance(left);
            const auto radius=scheduled->fhss_shape.affected_radius();
            if(position<radius || position+length>period-radius)return false;
        }
        return true;
    }
    long double clock_boundary(const Hypothesis& h,std::uint64_t within=0) const {
        if(scheduled_map() && !scheduled->symbol_ends.empty() && within==code.symbol_samples()) {
            const auto i=static_cast<std::size_t>(&h-hypotheses.data());
            if(scheduled->end_indices[i]==h.index)return scheduled->symbol_ends[i];
        }
        if(scheduled_map())return scheduled_boundary(h,scheduled_source_start(h,h.index)+within,
            within!=0);

        if(pulse_lattices.empty() && search.hypotheses.empty()) {
            if(within==code.symbol_samples())return h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate;
            return h.origin+static_cast<long double>(h.index)*code.symbol_samples()/h.rate+within/h.rate;
        }
        long double anchor=h.origin,shift=0;
        if(!pulse_lattices.empty()) {
            const auto& address=pulse_addresses[static_cast<std::size_t>(&h-hypotheses.data())];
            anchor=pulse_lattices[address.lattice].anchor;shift=address.shift;
        } else if(!search.hypotheses.empty()) {
            // Recover the exact half-chip origin address, including the final
            // clipped endpoint, without enlarging every legacy raw lane.
            const auto step=static_cast<long double>(code.chip_samples())/(2*h.rate);
            const auto address=std::round((h.origin-origin_lower)/step);
            if(h.origin==origin_lower+address*step) {
                const auto parity=std::fmod(address,2.L);
                anchor=origin_lower+parity*step;shift=std::floor(address/2);
            }
        }
        return anchor+(shift*code.chip_samples()+static_cast<long double>(h.index)*code.symbol_samples()+within)/h.rate;
    }
    long double symbol_start(const Hypothesis& h) const {return clock_boundary(h);}
    static bool full_rank(const Fit& fit) {
        return fit.count>2 && fit.cc*fit.ss-fit.cs*fit.cs>1e-12*std::max(1.,fit.cc*fit.ss);
    }
    void advance_drift(DriftFit& fit,const Hypothesis& h,std::uint64_t observed,unsigned char* ranks=nullptr,std::optional<long double> within={}) const {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::guards,true);
        while(fit.section+1<drift_sections && (scheduled_map()?
              (within?*within:source_position(h,observed)-scheduled_source_start(h,h.index))>=
                detail::drift_boundary(fit.section+1,code.symbol_samples(),drift_sections):
              static_cast<long double>(observed)>=std::ceil(clock_boundary(h,
                detail::drift_boundary(fit.section+1,code.symbol_samples(),drift_sections))))) {
            if(ranks && full_rank(fit.active))*ranks|=static_cast<unsigned char>(1U<<fit.section);
            const auto explained=fit.active.explained();fit.explained_sum+=explained;
            fit.largest_explained=std::max(fit.largest_explained,explained);fit.active={};++fit.section;
        }
    }
    void advance_differential(DifferentialFit& fit,const Hypothesis& h,std::uint64_t observed,std::optional<long double> within={}) const {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::guards,true);
        const auto total=code.symbol_samples(),width=differential_window;
        const auto position=std::max(0.L,scheduled_map()?
            (within?*within:source_position(h,observed)-scheduled_source_start(h,h.index)):
            (static_cast<long double>(observed)-symbol_start(h))*h.rate);
        auto next=static_cast<std::uint64_t>(std::min(std::floor(position/width),static_cast<long double>(total/width)));
        if(!scheduled_map()) {
            while(next<total/width && static_cast<long double>(observed)>=std::ceil(clock_boundary(h,(next+1)*width)))++next;
            while(next && static_cast<long double>(observed)<std::ceil(clock_boundary(h,next*width)))--next;
        }
        if(fit.initialized && next==fit.window_index)return;
        if(fit.initialized) {
            if(scheduled_map() && !options.amplitude_only) {
                // Raw pushes are contiguous and mapped-complex omission is
                // disabled. Reaching the next SOURCE window proves every
                // eligible sample of this representative window was visited;
                // null time carries neither signal nor noise degrees. The
                // initial partial source window never receives this proof.
                const auto lane=static_cast<std::size_t>(&h-hypotheses.data());
                const auto begin=scheduled_source_start(h,h.index)+fit.window_index*width;
                fit.expected_count=begin>=scheduled->observed_begin[lane]?fit.active.count:0;
            }
            fit.finish(total,width);
        }
        fit.active={};fit.window_index=next;fit.initialized=true;fit.expected_count=0;
        if(next<total/width)fit.expected_count=static_cast<std::uint64_t>(
            std::ceil(clock_boundary(h,(next+1)*width))-std::ceil(clock_boundary(h,next*width)));
    }
    void prepare_drift(std::uint64_t end) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::allocation);
        if(!drift_reserved)return;
        if(scheduled_map() && !options.amplitude_only && end<scheduled->drift_begin)return;
        const auto first_boundary=detail::drift_boundary(1,code.symbol_samples(),drift_sections);
        const auto needed=std::any_of(hypotheses.begin(),hypotheses.end(),[&](const auto& h) {
            return scheduled_map() && !options.amplitude_only?
                source_position(h,end)>scheduled_source_start(h,h.index)+first_boundary:
                static_cast<long double>(end)>std::ceil(clock_boundary(h,first_boundary));
        });
        if(!needed)return;
        // Until the first boundary the ordinary whole-symbol fit is also the
        // first section fit. Reserve its eventual state from construction, but
        // keep idle long-symbol searches at their historical physical size.
        drift_fits.resize(hypotheses.size()*(alternate_groups+1));
        if(scheduled_map() && !options.amplitude_only) {
            scheduled->scheduled_carry_drift.resize(scheduled->scheduled_carry.size());
            scheduled->scheduled_quarter_ranks.resize(hypotheses.size());scheduled->scheduled_carry_quarter_ranks.resize(hypotheses.size());
        }
        // Earlier cache ranges were prepared before section state existed.
        // Rebuild them with the newly active quarter boundaries before any
        // interval fast path can bypass section advancement.
        for(auto& cached:affine_coefficients)cached.invalidate();
        for(auto& h:hypotheses)h.affine_groups=0;
        for(std::size_t i=0;i<hypotheses.size();++i)for(std::size_t group=0;group<=alternate_groups;++group) {
            auto& drift=section_fits(i,group);const auto& whole=fits(hypotheses[i],i,group);
            for(unsigned bit=0;bit<2;++bit)drift[bit].active=whole[bit];
        }
        drift_reserved=0;accounted_bytes=working_bytes()+unallocated_history_bytes()+amplitude_scratch_reservation;
        room_for(0);
    }
    void prepare_scheduled_families() {
        const auto phase_limit=std::min<std::size_t>(options.scheduled_common_phase_limit,16);
        const bool phase_families=options.scheduled_common_shape_experiment && phase_limit>1 && !scheduled_calendar();
        if(!scheduled || options.amplitude_only || options.raw_reference || (!options.scheduled_family_experiment && !options.scheduled_common_shape_experiment) ||
           scheduled->family_plan_attempted || (banks.size()<2 && !phase_families))return;
        scheduled->family_plan_attempted=true;
        const auto count=hypotheses.size();
        if(count<2 || count>(std::numeric_limits<std::size_t>::max()-sizeof(ScheduledFamilyPlan)-sizeof(std::size_t))/(2*sizeof(std::size_t)))return;
        const auto needed=sizeof(ScheduledFamilyPlan)+(2*count+1)*sizeof(std::size_t);
        if(sizeof(PatternCorrelator)+accounted_bytes>budget || needed>budget-sizeof(PatternCorrelator)-accounted_bytes)return;
        std::unique_ptr<ScheduledFamilyPlan> plan;
        try {
            plan=std::make_unique<ScheduledFamilyPlan>();
            plan->members.resize(count);plan->ends.reserve(count+1);
        } catch(const std::bad_alloc&) {
            // No plan or charge was published; preserve independent lanes.
            return;
        }
        std::iota(plan->members.begin(),plan->members.end(),std::size_t{0});
        const auto identity=[&](std::size_t i) {
            const auto& h=hypotheses[i];
            // Phase variants are candidates for EXACT common construction only.
            // Dynamic preflight below must prove their complete processed flat
            // piece, private address and source coordinates agree; otherwise
            // every original lane executes separately.
            return std::tuple{h.origin,h.rate,phase_families?std::uint64_t{0}:h.phase_lower,
                phase_families?std::uint64_t{0}:h.phase_upper};
        };
        std::sort(plan->members.begin(),plan->members.end(),[&](std::size_t a,std::size_t b) {
            const auto ka=identity(a),kb=identity(b);
            if(ka<kb)return true;if(kb<ka)return false;
            // Adjacent canonical phases are most likely to share the same flat
            // interval. Keep all fine-frequency siblings of one phase adjacent.
            const auto pa=std::pair{hypotheses[a].phase_lower,hypotheses[a].phase_upper};
            const auto pb=std::pair{hypotheses[b].phase_lower,hypotheses[b].phase_upper};
            return pa<pb || (!(pb<pa) && a<b);
        });
        // Bound each same-worker group, retaining multiple stealable jobs even
        // when one family has many frequencies. Current/carry template rows
        // remain checked by exact dynamic origin/rate/phase/index/block keys.
        const auto workers=std::max<std::size_t>(1,worker_codes.size());
        const auto target_jobs=workers>std::numeric_limits<std::size_t>::max()/4?workers:4*workers;
        const auto per_job=std::clamp(count/target_jobs+static_cast<std::size_t>(count%target_jobs!=0),std::size_t{2},std::size_t{16});
        plan->ends.push_back(0);
        for(std::size_t first=0;first<count;) {
            auto last=first+1;
            while(last<count && identity(plan->members[first])==identity(plan->members[last]))++last;
            ++plan->exact_families;
            for(auto at=first;at<last;) {
                auto until=at;std::size_t phases=0;
                std::pair<std::uint64_t,std::uint64_t> prior{};
                while(until<last && until-at<per_job) {
                    const auto& h=hypotheses[plan->members[until]];
                    const auto phase=std::pair{h.phase_lower,h.phase_upper};
                    if(!phases || phase!=prior) {
                        if(phase_families && phases==phase_limit)break;
                        ++phases;prior=phase;
                    }
                    ++until;
                }
                // Never widen a tight timing bank or remove a rejected lane.
                // Small phase batches keep a wide phase union from making the
                // entire physical block fail the common unity-gain test.
                require(until>at,"scheduled phase-family partition did not advance");
                at=until;plan->ends.push_back(at);
            }
            first=last;
        }
        const auto available=budget-sizeof(PatternCorrelator)-accounted_bytes;
        if(plan->exact_families==count || plan->working_bytes()>available)return;
        if(phase_families && options.scheduled_common_phase_cache) {
            const auto groups=plan->ends.size()-1;
            if(groups<=(available-plan->working_bytes())/sizeof(ScheduledFamilyPlan::AddressCache)) {
                try {plan->phase_addresses.resize(groups);}
                catch(const std::bad_alloc&) {
                    // The fresh vector retains its empty state on allocation
                    // failure. Families can run without an address-proof cache.
                }
                // Actual vector capacity owns workspace; optional reuse must
                // never displace a required detector or admitted hypothesis.
                if(plan->working_bytes()>available)
                    std::vector<ScheduledFamilyPlan::AddressCache>{}.swap(plan->phase_addresses);
            }
        }
        scheduled->families=std::move(plan);
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"mapped family metadata exceeds DSP workspace");
        scheduled->extra_work.scheduled_exact_families=scheduled->families->exact_families;
        scheduled->extra_work.scheduled_family_groups=scheduled->families->ends.size()-1;
        scheduled->extra_work.scheduled_family_metadata_bytes=scheduled->families->working_bytes();
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
    }
    void prepare_scheduled_common_shape() {
        if(!scheduled || !options.scheduled_common_shape_experiment || scheduled->common_attempted ||
           !scheduled->families || options.amplitude_only || options.raw_reference)return;
        scheduled->common_attempted=true;
        const auto workers=std::max<std::size_t>(1,worker_codes.size());
        constexpr auto per_worker=scheduled_common_scratch_bytes+sizeof(ScheduledCommonWork);
        if(workers>std::numeric_limits<std::size_t>::max()/per_worker)return;
        const auto bytes=workers*per_worker;
        if(sizeof(PatternCorrelator)+accounted_bytes>budget || bytes>budget-sizeof(PatternCorrelator)-accounted_bytes)return;
        std::vector<ScheduledCommonWork> prepared;
        try {prepared.resize(workers);}
        catch(const std::bad_alloc&) {return;}
        const auto actual=workers*scheduled_common_scratch_bytes+
            prepared.capacity()*sizeof(ScheduledCommonWork);
        if(actual>budget-sizeof(PatternCorrelator)-accounted_bytes)return;
        // Publish only the complete affordable vector; failed preparation owns
        // no persistent allocation and therefore needs no charge rollback.
        scheduled->common_work=std::move(prepared);
        scheduled->common_scratch=true;
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"shared source-family scratch exceeds workspace");
        scheduled->extra_work.scheduled_common_peak_bytes=std::max(scheduled->extra_work.scheduled_common_peak_bytes,
            workers*scheduled_common_scratch_bytes+scheduled->common_work.capacity()*sizeof(ScheduledCommonWork));
        bool native_family=false,duplicate_family=false,small_duplicate_family=false,general_duplicate_family=false;
        // At most sixteen candidates per family; this exact metadata scan runs
        // once during optional preparation, not for every physical sample.
        for(std::size_t group=1;group<scheduled->families->ends.size();++group) {
            std::array<std::size_t,detail::ScheduledRawSampleScratch::maximum_lanes> frequency;
            std::size_t unique=0;bool duplicate=false;
            for(auto at=scheduled->families->ends[group-1];at<scheduled->families->ends[group];++at) {
                const auto f=hypotheses[scheduled->families->members[at]].frequency;
                std::size_t k=0;while(k<unique && frequency[k]!=f)++k;
                if(k<unique)duplicate=true;
                else if(unique<frequency.size())frequency[unique++]=f;
            }
            duplicate_family|=duplicate;small_duplicate_family|=duplicate && unique==1;
            general_duplicate_family|=duplicate && unique>1;
            native_family|=unique>=detail::ScheduledRawSampleScratch::minimum_simd_lanes;
        }
        if(options.scheduled_common_simd_experiment && native_family) {
            scheduled->common_simd_backend=detail::family_simd::selected();
            scheduled->extra_work.scheduled_common_simd_selected_isa=scheduled->common_simd_backend==detail::family_simd::Backend::avx2?2U:
                scheduled->common_simd_backend==detail::family_simd::Backend::sse2?1U:0U;
        }
        const bool native_possible=options.scheduled_common_simd_experiment && native_family &&
            scheduled->common_simd_backend!=detail::family_simd::Backend::scalar;
        if(duplicate_family || native_possible) {
            auto per_worker_raw=(general_duplicate_family || native_possible)?detail::scheduled_raw_sample_scratch_bytes:
                detail::scheduled_raw_single_frequency_scratch_bytes;
            const auto fits=[&](std::size_t per) {
                return workers<=std::numeric_limits<std::size_t>::max()/per &&
                    sizeof(PatternCorrelator)+accounted_bytes<=budget &&
                    workers*per<=budget-sizeof(PatternCorrelator)-accounted_bytes;
            };
            // Native/general reuse scratch is optional. Retain cheap exact
            // single-frequency phase reuse if only its smaller loan fits.
            if(!fits(per_worker_raw) && small_duplicate_family)per_worker_raw=detail::scheduled_raw_single_frequency_scratch_bytes;
            if(fits(per_worker_raw)) {
                const auto extra=workers*per_worker_raw;
                scheduled->common_raw_scratch=true;scheduled->common_raw_scratch_bytes=per_worker_raw;
                accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"common raw family scratch exceeds workspace");
                scheduled->extra_work.scheduled_common_raw_peak_bytes=std::max(scheduled->extra_work.scheduled_common_raw_peak_bytes,extra);
                if(native_possible && per_worker_raw==detail::scheduled_raw_sample_scratch_bytes)
                    scheduled->extra_work.scheduled_common_simd_peak_bytes=std::max(scheduled->extra_work.scheduled_common_simd_peak_bytes,extra);
                scheduled->extra_work.scheduled_common_peak_bytes=std::max(scheduled->extra_work.scheduled_common_peak_bytes,
                    workers*scheduled_common_scratch_bytes+scheduled->common_work.capacity()*sizeof(ScheduledCommonWork)+extra);
            }
        }
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
    }
    void prepare_scheduled_frequency_piece() {
        if(!scheduled || !scheduled->common_scratch || scheduled->frequency_piece_scratch ||
           config.search_arithmetic==SearchArithmetic::fp64 || !detail::scheduled_frequency_piece_requested())return;
        const auto workers=std::max<std::size_t>(1,worker_codes.size());
        constexpr auto per=detail::scheduled_frequency_piece_scratch_bytes;
        if(sizeof(PatternCorrelator)>budget || accounted_bytes>budget-sizeof(PatternCorrelator) ||
           workers>(budget-sizeof(PatternCorrelator)-accounted_bytes)/per)return;
        scheduled->frequency_piece_scratch=true;
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"fine-frequency piece scratch exceeds workspace");
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
    }
    void prepare_scheduled_outer_public_replay() {
        // Preserve all preceding workers, exact caches and arithmetic choices.
        // The larger replay frame is an additive loan from final surplus only.
        if(!scheduled || !scheduled->common_raw_scratch ||
           config.search_arithmetic==SearchArithmetic::fp64 ||
           !detail::scheduled_outer_public_replay_requested() ||
           scheduled->common_raw_scratch_bytes>=detail::scheduled_outer_public_replay_frame)return;
        const auto workers=std::max<std::size_t>(1,worker_codes.size());
        const auto per=detail::scheduled_outer_public_replay_frame-scheduled->common_raw_scratch_bytes;
        if(workers>std::numeric_limits<std::size_t>::max()/per ||
           sizeof(PatternCorrelator)+accounted_bytes>budget ||
           workers*per>budget-sizeof(PatternCorrelator)-accounted_bytes)return;
        scheduled->common_raw_scratch_bytes=detail::scheduled_outer_public_replay_frame;
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"outer public replay frame exceeds workspace");
        const auto raw=workers*scheduled->common_raw_scratch_bytes;
        scheduled->extra_work.scheduled_common_raw_peak_bytes=std::max(
            scheduled->extra_work.scheduled_common_raw_peak_bytes,raw);
        scheduled->extra_work.scheduled_common_peak_bytes=std::max(scheduled->extra_work.scheduled_common_peak_bytes,
            workers*scheduled_common_scratch_bytes+scheduled->common_work.capacity()*sizeof(ScheduledCommonWork)+raw);
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
    }
    void collect_scheduled_common_shape() {
        if(!scheduled || !scheduled->common_scratch)return;
        for(auto& c:scheduled->common_work) {
            if(c.frequency_attempts && audio::trace::active.load()) {
                audio::trace::record("scheduled_frequency_piece_work",[&](auto& f) {f.add("attempts",c.frequency_attempts);
                f.add("prepared_samples",c.frequency_prepared_samples);f.add("accepted_pieces",c.frequency_pieces);
                f.add("accepted_lanes",c.frequency_lanes);f.add("geometry_fallbacks",c.frequency_geometry_fallbacks);
                f.add("numerical_fallbacks",c.frequency_numerical_fallbacks);f.add("outer_terms",c.frequency_outer_terms);
                f.add("maximum_relative_error",c.frequency_maximum_relative_error);
                f.add("arithmetic","FP64 third-order frequency moments; original private shape/limiter; wider physical coordinates");
                f.add("scratch_bytes_per_worker",detail::scheduled_frequency_piece_scratch_bytes);
                });
            }
            scheduled->extra_work.scheduled_common_families+=c.families;scheduled->extra_work.scheduled_common_lanes+=c.lanes;
            scheduled->extra_work.scheduled_common_pieces+=c.pieces;scheduled->extra_work.scheduled_common_shapes+=c.shapes;
            scheduled->extra_work.scheduled_common_raw_samples+=c.raw_samples;scheduled->extra_work.scheduled_common_raw_lane_samples+=c.raw_lane_samples;
            scheduled->extra_work.scheduled_common_affine_preparations+=c.affine_preparations;scheduled->extra_work.scheduled_common_affine_lanes+=c.affine_lanes;
            scheduled->extra_work.scheduled_common_gram_fallbacks+=c.gram_fallbacks;
            scheduled->extra_work.scheduled_common_mapped_pairs+=c.mapped_pairs;
            scheduled->extra_work.scheduled_common_mapped_failed_pairs+=c.mapped_failed_pairs;
            scheduled->extra_work.scheduled_common_mapped_lanes+=c.mapped_lanes;
            scheduled->extra_work.scheduled_common_affine_reused_lanes+=c.affine_reused_lanes;
            scheduled->extra_work.scheduled_common_phase_attempts+=c.phase_attempts;scheduled->extra_work.scheduled_common_phase_rejects+=c.phase_rejects;
            scheduled->extra_work.scheduled_common_phase_families+=c.phase_families;scheduled->extra_work.scheduled_common_phase_lanes+=c.phase_lanes;
            scheduled->extra_work.scheduled_common_raw_fit_pairs+=c.raw_fit_pairs;scheduled->extra_work.scheduled_common_raw_reused_lane_samples+=c.raw_reused_lane_samples;
            scheduled->extra_work.scheduled_common_raw_packed_values+=c.raw_packed_values;scheduled->extra_work.scheduled_common_raw_workspace_samples+=c.raw_workspace_samples;
            scheduled->extra_work.scheduled_common_simd_attempts+=c.simd_attempts;scheduled->extra_work.scheduled_common_simd_samples+=c.simd_samples;
            scheduled->extra_work.scheduled_common_simd_lane_samples+=c.simd_lane_samples;scheduled->extra_work.scheduled_common_simd_packed_values+=c.simd_packed_values;
            scheduled->extra_work.scheduled_common_simd_guard_updates+=c.simd_guard_updates;scheduled->extra_work.scheduled_common_simd_outer_updates+=c.simd_outer_updates;
            scheduled->extra_work.scheduled_common_simd_short_samples+=c.simd_short_samples;scheduled->extra_work.scheduled_common_simd_workspace_samples+=c.simd_workspace_samples;
            scheduled->extra_work.scheduled_common_simd_isa_samples+=c.simd_isa_samples;scheduled->extra_work.scheduled_common_simd_scalar_lane_samples+=c.simd_scalar_lane_samples;
            scheduled->extra_work.scheduled_common_phase_address_hits+=c.phase_address_hits;
            scheduled->extra_work.scheduled_common_phase_address_misses+=c.phase_address_misses;
            scheduled->extra_work.scheduled_common_phase_address_queries+=c.phase_address_queries;c={};
        }
    }
    std::optional<std::size_t> temporal_plan_workers()const {
        if(!scheduled || !options.scheduled_temporal_template_experiment || options.amplitude_only ||
           options.raw_reference || options.scheduled_moment_reference ||
           config.search_arithmetic==SearchArithmetic::fp64 || !shaped || scheduled_affine ||
           scheduled->moments_eligible || scheduled->curves || scheduled->float_curves ||
           !detail::ScheduledTemporalTemplates::supported() || !pulse_lattices.empty())return std::nullopt;
        // Match prepare_workers' retained marker exactly. A preceding serial
        // Temporal alone is not proof: original sizing would run again.
        if(worker_codes.empty() && scheduled->worker_template_caches.empty())return 0;
        const auto count=std::max<std::size_t>(1,worker_codes.size());
        if(scheduled->worker_temporal_templates.size()!=count)return 0;
        for(const auto& batch:scheduled->worker_temporal_templates)if(!batch)return 0;
        return count;
    }
    void prepare_scheduled_temporal_templates() {
        if(!scheduled || !options.scheduled_temporal_template_experiment || options.amplitude_only ||
           options.raw_reference || options.scheduled_moment_reference ||
           config.search_arithmetic==SearchArithmetic::fp64 || !shaped || scheduled_affine ||
           scheduled->moments_eligible || scheduled->curves || scheduled->float_curves ||
           !detail::ScheduledTemporalTemplates::supported() || !scheduled->worker_temporal_templates.empty())return;
        // Optional output scratch is chosen AFTER the original workers/caches;
        // it cannot reduce concurrency, bit_limit or promised detector state.
        const auto count=std::max<std::size_t>(1,worker_codes.size());
        constexpr auto object=sizeof(detail::ScheduledTemporalTemplates)+detail::ScheduledTemporalTemplates::callee_allowance;
        constexpr auto pointer=sizeof(std::unique_ptr<detail::ScheduledTemporalTemplates>);
        if(sizeof(PatternCorrelator)>budget || accounted_bytes>budget-sizeof(PatternCorrelator))return;
        const auto available=budget-sizeof(PatternCorrelator)-accounted_bytes;
        if(count>available/(object+pointer))return;
        std::vector<std::unique_ptr<detail::ScheduledTemporalTemplates>> prepared;
        std::size_t extra=0;
        try {
            prepared.reserve(count);
            // Charge the allocator's ACTUAL capacity before publishing any
            // optional member. Failure preserves every original worker/cache.
            if(prepared.capacity()>available/pointer)return;
            const auto pointers=prepared.capacity()*pointer;
            if(count>(available-pointers)/object)return;
            extra=pointers+count*object;
            for(std::size_t i=0;i<count;++i)prepared.push_back(std::make_unique<detail::ScheduledTemporalTemplates>());
        } catch(const std::bad_alloc&) {return;}
        scheduled->worker_temporal_templates=std::move(prepared);
        accounted_bytes+=extra;
        scheduled->temporal_peak_bytes=std::max(scheduled->temporal_peak_bytes,extra);
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
    }
    static bool temporal_plan_cache_requested() {
        // One immutable experiment gate per process. First queried only AFTER
        // existing worker/template/Temporal choices; public ABI/state unchanged.
        static const bool requested=[] {
            const auto* value=std::getenv("DATAPUMP_SCHED_TEMPORAL_PLAN_CACHE");
            return value && std::string_view(value)=="4";
        }();
        return requested;
    }
    void prepare_scheduled_temporal_plan_caches() {
        if(!scheduled || scheduled->worker_temporal_templates.empty() ||
           !temporal_plan_cache_requested() || scheduled->worker_temporal_templates.front()->plan_cache_attempted())return;
        for(auto& batch:scheduled->worker_temporal_templates)batch->mark_plan_cache_attempted();
        // Every original worker, template cache and Temporal object has already
        // been selected/charged. Optional pools never participate in sizing.
        constexpr auto charge=detail::ScheduledTemporalTemplates::plan_cache_charge;
        const auto count=scheduled->worker_temporal_templates.size();
        if(sizeof(PatternCorrelator)>budget || accounted_bytes>budget-sizeof(PatternCorrelator))return;
        const auto available=budget-sizeof(PatternCorrelator)-accounted_bytes;
        if(count>available/charge)return;
        try {
            for(auto& batch:scheduled->worker_temporal_templates) {
                const auto prior=batch->plan_cache_bytes();batch->allocate_plan_cache();
                accounted_bytes+=batch->plan_cache_bytes()-prior;
            }
        } catch(const std::bad_alloc&) {
            drop_scheduled_temporal_plan_caches();return;
        }
        std::size_t temporal_bytes=scheduled->worker_temporal_templates.capacity()*sizeof(std::unique_ptr<detail::ScheduledTemporalTemplates>);
        for(const auto& batch:scheduled->worker_temporal_templates)temporal_bytes+=sizeof(detail::ScheduledTemporalTemplates)+
            detail::ScheduledTemporalTemplates::callee_allowance+batch->plan_cache_bytes()+batch->fp32_bytes();
        scheduled->temporal_peak_bytes=std::max(scheduled->temporal_peak_bytes,temporal_bytes);
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
    }
    void drop_scheduled_temporal_plan_caches() {
        if(!scheduled)return;
        // Only caller/joined-worker boundaries may reclaim. Each drop detaches
        // numeric borrowers and releases any retained outer Plan pin before
        // storage disappears; detached raw outputs themselves remain usable.
        for(auto& batch:scheduled->worker_temporal_templates)if(batch) {
            const auto bytes=batch->plan_cache_bytes();
            batch->drop_plan_cache();accounted_bytes-=bytes;
        }
    }
    struct TemporalFp32Policy {bool requested=false;double limit=1e-5;};
    static TemporalFp32Policy temporal_fp32_policy() {
        // Separate default-off process diagnostic. Does not enable Temporal
        // reuse, change persisted policy, or select any approximate curve.
        static const auto policy=[] {
            const auto* value=std::getenv("DATAPUMP_SCHED_TEMPORAL_FP32");
            const auto mode=value?std::string_view(value):std::string_view{};
            return TemporalFp32Policy{mode=="1" || mode=="refine",mode=="refine"?0.:1e-5};
        }();return policy;
    }
    void prepare_scheduled_temporal_fp32() {
        const auto policy=temporal_fp32_policy();
        if(!policy.requested || !scheduled || scheduled->worker_temporal_templates.empty() ||
           scheduled->worker_temporal_templates.front()->fp32_attempted() ||
           (config.search_arithmetic!=SearchArithmetic::default_mode && config.search_arithmetic!=SearchArithmetic::fp32))return;
        for(auto& batch:scheduled->worker_temporal_templates)batch->mark_fp32_attempted();
        // AFTER existing worker/Temporal/exact Plan-cache selection. Optional
        // FP32 never reduces mandatory detector state or original concurrency.
        constexpr auto charge=detail::ScheduledTemporalTemplates::fp32_charge;
        const auto count=scheduled->worker_temporal_templates.size();
        if(sizeof(PatternCorrelator)>budget || accounted_bytes>budget-sizeof(PatternCorrelator))return;
        const auto available=budget-sizeof(PatternCorrelator)-accounted_bytes;if(count>available/charge)return;
        try {for(auto& batch:scheduled->worker_temporal_templates) {
            const auto prior=batch->fp32_bytes();batch->allocate_fp32(policy.limit);accounted_bytes+=batch->fp32_bytes()-prior;
        }} catch(const std::bad_alloc&) {drop_scheduled_temporal_fp32();return;}
        std::size_t bytes=scheduled->worker_temporal_templates.capacity()*sizeof(std::unique_ptr<detail::ScheduledTemporalTemplates>);
        for(const auto& batch:scheduled->worker_temporal_templates)bytes+=sizeof(detail::ScheduledTemporalTemplates)+
            detail::ScheduledTemporalTemplates::callee_allowance+batch->plan_cache_bytes()+batch->fp32_bytes();
        scheduled->temporal_peak_bytes=std::max(scheduled->temporal_peak_bytes,bytes);
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
    }
    void prepare_scheduled_temporal_fp32_payloads() {
        if(!detail::ScheduledTemporalFp32Execution::payload_requested() || !scheduled)return;
        // Optional local precision storage only after every original grant and
        // the existing native worker scratch selection. Never allocate in get.
        for(auto& batch:scheduled->worker_temporal_templates)if(batch && batch->fp32_local_payload_pending()) {
            batch->mark_fp32_local_payload_attempted();
            constexpr auto charge=detail::ScheduledTemporalTemplates::fp32_local_payload_charge;
            if(sizeof(PatternCorrelator)>budget || accounted_bytes>budget-sizeof(PatternCorrelator) ||
               charge>budget-sizeof(PatternCorrelator)-accounted_bytes)continue;
            const auto before=batch->plan_cache_bytes();
            try {if(batch->allocate_fp32_local_payload())accounted_bytes+=batch->plan_cache_bytes()-before;}
            catch(const std::bad_alloc&) {} // Existing single-key native fallback.
        }
        std::size_t bytes=scheduled->worker_temporal_templates.capacity()*sizeof(std::unique_ptr<detail::ScheduledTemporalTemplates>);
        for(const auto& batch:scheduled->worker_temporal_templates)if(batch)bytes+=sizeof(detail::ScheduledTemporalTemplates)+
            detail::ScheduledTemporalTemplates::callee_allowance+batch->plan_cache_bytes()+batch->fp32_bytes();
        scheduled->temporal_peak_bytes=std::max(scheduled->temporal_peak_bytes,bytes);
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
    }
    void record_temporal_fp32(const detail::ScheduledTemporalTemplates& batch,const char* disposition)const {
        if(!batch.fp32_bytes() || !audio::trace::active.load())return;
        const auto c=batch.fp32_counters();
        audio::trace::record("temporal_fp32_work",[&](auto& f) {
            f.add("epoch",config.stream_epoch);f.add("samples",sample);f.add("disposition",disposition);
            f.add("counter_scope","one optional worker lifetime; sum retired events and final live events");
            f.add("attempts",c.attempts);f.add("native_tiles",c.native_tiles);f.add("native_rows",c.numeric.native_samples);
            f.add("refined_tiles",c.refined_tiles);f.add("refined_rows",c.refined_rows);
            f.add("consumed_native",c.consumed_native);f.add("consumed_refined",c.consumed_refined);
            f.add("packed_hits",c.packed_hits);f.add("packed_misses",c.packed_misses);
            f.add("entry_payload_hits",c.entry_hits);f.add("entry_payload_builds",c.entry_builds);
            f.add("entry_payload_rejected",c.entry_rejected);f.add("entry_payload_busy",c.entry_busy);
            f.add("entry_payload_unavailable",c.entry_unavailable);
            f.add("weight_narrowings",c.numeric.weight_narrowings);f.add("coefficient_narrowings",c.numeric.coefficient_narrowings);
            f.add("output_widenings",c.numeric.output_widenings);f.add("shape_terms",c.numeric.shape_terms);
            f.add("error_fallback_rows",c.numeric.error_fallback_samples);f.add("range_fallbacks",c.numeric.range_fallbacks);
            f.add("environment_fallbacks",c.numeric.environment_fallbacks);f.add("workspace_bytes",batch.fp32_bytes());
            f.add("output_arithmetic",c.consumed_native?"diagnostic native FP32 ordered products/sums; FP64 limiter/Fits/guards":
                "original FP64 outputs; native bypass/rejection/refinement");
        });
    }
    void drop_scheduled_temporal_fp32() {
        if(!scheduled)return;
        // Same joined-worker boundary as existing exact Plan-cache/Temporal
        // eviction. A drop emits its complete optional lifetime once.
        for(auto& batch:scheduled->worker_temporal_templates)if(batch && batch->fp32_bytes()) {
            record_temporal_fp32(*batch,"retired");const auto bytes=batch->fp32_bytes(),before=batch->plan_cache_bytes();
            batch->drop_fp32();accounted_bytes-=bytes+before-batch->plan_cache_bytes();
        }
    }
    bool fine_curves_eligible()const {
        return scheduled && !options.amplitude_only && !options.raw_reference &&
            !options.scheduled_moment_reference && !options.scheduled_float_curve_experiment &&
            !scheduled_affine && shaped && !code.transmit_trace_enabled() &&
            detail::fine_source_curve_geometry(config,detail::fine_source_curves_requested());
    }
    bool fine_curves_allocated()const {
        return scheduled && scheduled->curves && code.chip_samples()<1024;
    }
    // Separate from the existing4096-byte common caller allowance. Covers the
    // measured compiler-visible fine source/cache helper chain with headroom;
    // external crypto/libm/OS stack qualification remains independent.
    static constexpr std::size_t fine_curve_callee_bytes=8192;
    void prepare_fine_curves() {
        if(!fine_curves_eligible() || scheduled->curves || scheduled->float_curves)return;
        // Final surplus only: preceding original workers/exact/Temporal/native
        // choices and their complete reservations have already been selected.
        if(sizeof(PatternCorrelator)>budget || accounted_bytes>budget-sizeof(PatternCorrelator))return;
        const auto available=budget-sizeof(PatternCorrelator)-accounted_bytes;
        if(sizeof(ScheduledCurveCache)>available ||
           std::max<std::size_t>(1,worker_codes.size())>
               (available-sizeof(ScheduledCurveCache))/fine_curve_callee_bytes)return;
        try {scheduled->curves=std::make_unique<ScheduledCurveCache>();}
        catch(const std::bad_alloc&){return;}
        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
    }
    void prepare_workers() {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::allocation);
        // A fine diagnostic cannot enter fresh original worker/Temporal sizing.
        if(fine_curves_allocated() &&
           ((worker_codes.empty() && scheduled->worker_template_caches.empty()) ||
            (options.scheduled_temporal_template_experiment && scheduled->worker_temporal_templates.empty())))
            drop_scheduled_curves();
        if(scheduled && scheduled->moments_eligible && !scheduled->curves && !scheduled->float_curves &&
           config.scramble && config.spreading_mode==SpreadingMode::pattern && code.symbol_samples()<(1ULL<<40)) {
            // Arithmetic width is independent of curve/template reuse. The
            // native-float prototype was slower in complete paired execution.
            const bool native_float=options.scheduled_float_curve_experiment;
            const auto bytes=native_float?sizeof(ScheduledFloatCurveCache):sizeof(ScheduledCurveCache);
            if(bytes<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                if(native_float)scheduled->float_curves=std::make_unique<ScheduledFloatCurveCache>();
                else scheduled->curves=std::make_unique<ScheduledCurveCache>();
                accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            }
        }
        if(scheduled && scheduled->moments_eligible && scheduled->moments.empty()) {
            // Optional outer-pulse scratch never displaces detector state or
            // the established bit limit. Reserve it only with affordable
            // moments, for every currently executing worker (serial is one).
            const auto outer_scratch=outer_presence?detail::scheduled_outer_pulse_scratch_bytes*
                std::max<std::size_t>(1,worker_codes.size()):0;
            const auto extra=banks.size()*(sizeof(std::vector<ScheduledMoment>)+(block_samples+1)*sizeof(ScheduledMoment))+
                outer_scratch;
            if(extra<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                scheduled->outer_moment_scratch=outer_presence;
                scheduled->moments.resize(banks.size());
                for(auto& row:scheduled->moments)row.resize(block_samples+1);
                accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"mapped moment scratch exceeds DSP workspace");
                work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            }
        }
        if(scheduled && scheduled->moments_eligible &&
           options.scheduled_float_moment_experiment && scheduled->float_signal.empty() &&
           (config.search_arithmetic==SearchArithmetic::default_mode || config.search_arithmetic==SearchArithmetic::fp32)) {
            const auto extra=banks.size()*sizeof(detail::scheduled_float_signal::Moments)+
                hypotheses.size()*sizeof(ScheduledFloatCounts)+
                std::max<std::size_t>(1,worker_codes.size())*detail::scheduled_float_signal::contraction_scratch_bytes;
            if(extra<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                scheduled->float_signal.resize(banks.size());
                scheduled->float_counts.resize(hypotheses.size());
                accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"mapped FP32 signal scratch exceeds DSP workspace");
                work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            }
        }
        if(scheduled && scheduled->moments_eligible && options.scheduled_float_gram_experiment &&
           scheduled->float_grams.empty() && (config.search_arithmetic==SearchArithmetic::default_mode ||
           config.search_arithmetic==SearchArithmetic::fp32)) {
            const auto extra=hypotheses.size()*sizeof(ScheduledFloatGramCounts)+
                std::max<std::size_t>(1,worker_codes.size())*detail::scheduled_float_gram::scratch_bytes;
            if(extra<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                scheduled->float_grams.resize(hypotheses.size());
                accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"mapped FP32 Gram scratch exceeds workspace");
                work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            }
        }
        if(options.amplitude_only)return;
        if(!pulse_lattices.empty())return;
        // A serial bank can retain one exact template cache without any
        // worker_codes. It is already prepared too: replacing it here would
        // lose counters and double-charge its retained allocation each push.
        if(!worker_codes.empty() || (scheduled && !scheduled->worker_template_caches.empty())) {
            prepare_scheduled_families();prepare_scheduled_common_shape();prepare_scheduled_temporal_templates();prepare_scheduled_temporal_plan_caches();prepare_scheduled_temporal_fp32();prepare_scheduled_temporal_fp32_payloads();prepare_scheduled_outer_public_replay();prepare_fine_curves();prepare_scheduled_frequency_piece();return;
        }
        // No original worker/cache marker survived. Release a preceding
        // serial tile before re-running the original worker sizing policy.
        drop_scheduled_temporal_templates();
        const auto concurrency=std::min(hypotheses.size(),detail::search_concurrency(search.worker_threads));
        const auto per_worker=code.working_bytes()+(scheduled_map()?sizeof(SchedHopSourceMap)+sizeof(std::unique_ptr<SchedHopSourceMap>):0)+
            (scheduled && scheduled->outer_moment_scratch?detail::scheduled_outer_pulse_scratch_bytes:0)+
            (scheduled && !scheduled->float_signal.empty()?detail::scheduled_float_signal::contraction_scratch_bytes:0)+
            (scheduled && !scheduled->float_grams.empty()?detail::scheduled_float_gram::scratch_bytes:0);
        const auto workers=std::min(concurrency,(budget-sizeof(PatternCorrelator)-accounted_bytes)/per_worker);
        if(workers>1) {
            if(scheduled_map())++work.scheduled_worker_preparations;
            std::vector<PatternCode> prepared;prepared.reserve(workers);
            for(std::size_t worker=0;worker<workers;++worker)prepared.emplace_back(config,config.stream_epoch);
            if(scheduled_map()) {
                scheduled->worker_maps.reserve(workers);
                for(std::size_t worker=0;worker<workers;++worker)scheduled->worker_maps.push_back(std::make_unique<SchedHopSourceMap>(*scheduled_map()));
            }
            worker_codes=std::move(prepared);
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"parallel pattern caches exceed DSP workspace");
            work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
        }
        if(scheduled_map() && scheduled->scheduled_cache_possible && !options.scheduled_template_cache_reference) {
            const auto count=worker_codes.empty()?1:worker_codes.size();
            const auto pointers=count*sizeof(std::unique_ptr<ScheduledTemplateCache>);
            if(pointers<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                scheduled->worker_template_caches.resize(count);
                accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                for(auto& cache:scheduled->worker_template_caches)if(sizeof(ScheduledTemplateCache)<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                    cache=std::make_unique<ScheduledTemplateCache>();accounted_bytes+=sizeof(ScheduledTemplateCache);
                    work.scheduled_template_cache_peak_bytes=std::max(work.scheduled_template_cache_peak_bytes,
                        static_cast<std::size_t>(std::count_if(scheduled->worker_template_caches.begin(),scheduled->worker_template_caches.end(),[](const auto& item){return static_cast<bool>(item);})) * sizeof(ScheduledTemplateCache));
                }
                work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            }
        }
        prepare_scheduled_families();prepare_scheduled_common_shape();prepare_scheduled_temporal_templates();prepare_scheduled_temporal_plan_caches();prepare_scheduled_temporal_fp32();prepare_scheduled_temporal_fp32_payloads();prepare_scheduled_outer_public_replay();prepare_fine_curves();prepare_scheduled_frequency_piece();
    }
    bool defer_cold_history() const {
        // Preserve eager history/loan behavior for ordinary short links.
        // Long cold symbols can wait minutes or hours before any retained event.
        return search.compact_clock_search && !scheduled_map() && !options.amplitude_only &&
            code.symbol_samples()>=60ULL*config.sample_rate;
    }
    std::size_t unallocated_history_bytes() const {
        if(defer_cold_history() && history.capacity()<search.candidate_limit)
            return (search.candidate_limit-history.capacity())*sizeof(PatternEvidence);
        return 0;
    }
    std::size_t working_bytes() const {
        auto value=sizeof(Impl)+code.working_bytes()+hypotheses.capacity()*sizeof(Hypothesis)+banks.capacity()*sizeof(Bank)+
            emissions.capacity()*sizeof(Emission)+
            alternate_fits.capacity()*sizeof(decltype(alternate_fits)::value_type)+
            chip_evidence.capacity()*sizeof(decltype(chip_evidence)::value_type)+pending_chip_scores.capacity()*sizeof(double)+
            (outer_evidence?sizeof(*outer_evidence)+outer_evidence->capacity()*sizeof(detail::CorrelationOuterEvidence):0)+
            drift_fits.capacity()*sizeof(decltype(drift_fits)::value_type)+
            differential_fits.capacity()*sizeof(decltype(differential_fits)::value_type)+
            points.capacity()*sizeof(Complex)+
            history.capacity()*sizeof(PatternEvidence)+bursts.capacity()*sizeof(PatternBurst)+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double)+
            search.hypotheses.capacity()*sizeof(PatternFrequencyRateHypothesis);
        if(timing)value+=sizeof(*timing)+timing->capacity()*sizeof(TimingEntry);
        value+=pulse_lattices.capacity()*sizeof(PulseLattice);
        value+=pulse_addresses.capacity()*sizeof(PulseAddress);
        value+=affine_counts.capacity()*sizeof(std::uint64_t);
        value+=affine_coefficients.capacity()*sizeof(AffineCoefficients);
        value+=amplitude_fits.capacity()*sizeof(decltype(amplitude_fits)::value_type)+amplitude_pcm.capacity()*sizeof(float);
        value+=amplitude_templates.capacity()*sizeof(decltype(amplitude_templates)::value_type);
        value+=amplitude_phases.capacity()*sizeof(AmplitudePhaseEntry)+amplitude_scratch.working_bytes();
        value+=amplitude_channels.capacity()*sizeof(PatternAmplitudeChannel);
        if(amplitude_geometry)value+=sizeof(PatternAmplitudeGeometry);
        // Kernel preparation has bounded wide polynomial scratch. Retain its
        // reservation across workspace changes and payload allocation too.
        if(!pulse_lattices.empty())value+=detail::correlation_pulse_scratch_bytes;
        for(const auto& lattice:pulse_lattices)value+=lattice.cells.capacity()*sizeof(detail::CorrelationPulseCell);
        for(const auto& h:hypotheses)value+=h.burst.bits.capacity();
        if(lazy_state())value+=lazy_state()->working_bytes();
        for(const auto& burst:bursts)value+=burst.bits.capacity();
        if(scheduled_map())value+=sizeof(ScheduledState)+scheduled_map()->working_bytes()+
            scheduled->scheduled_carry.capacity()*sizeof(ScheduledCarry)+
            scheduled->scheduled_carry_drift.capacity()*sizeof(decltype(scheduled->scheduled_carry_drift)::value_type)+
            scheduled->scheduled_carry_differential.capacity()*sizeof(decltype(scheduled->scheduled_carry_differential)::value_type)+
            scheduled->scheduled_carry_chips.capacity()*sizeof(decltype(scheduled->scheduled_carry_chips)::value_type)+
            scheduled->scheduled_carry_outer.capacity()*sizeof(decltype(scheduled->scheduled_carry_outer)::value_type)+
            (scheduled->scheduled_quarter_ranks.capacity()+scheduled->scheduled_carry_quarter_ranks.capacity())*sizeof(std::array<unsigned char,2>)+
            scheduled->anchors.capacity()*sizeof(SchedHopCoordinate)+scheduled->observed_begin.capacity()*sizeof(long double)+scheduled->last_accepted.capacity()*sizeof(std::uint64_t)+
            (scheduled->source_starts.capacity()+scheduled->template_uncached.capacity()+scheduled->end_indices.capacity()+scheduled->first_full_indices.capacity())*sizeof(std::uint64_t)+
            scheduled->symbol_ends.capacity()*sizeof(long double)+(scheduled_calendar()?scheduled_calendar()->working_bytes():0);
        if(scheduled) {
            value+=scheduled->physical_views.capacity()*sizeof(detail::PhysicalMomentCache::View);
            if(scheduled->families)value+=scheduled->families->working_bytes();
            value+=scheduled->common_work.capacity()*sizeof(ScheduledCommonWork);
            if(scheduled->common_scratch)value+=std::max<std::size_t>(1,worker_codes.size())*scheduled_common_scratch_bytes;
            if(scheduled->common_raw_scratch)value+=std::max<std::size_t>(1,worker_codes.size())*scheduled->common_raw_scratch_bytes;
            if(scheduled->frequency_piece_scratch)value+=std::max<std::size_t>(1,worker_codes.size())*detail::scheduled_frequency_piece_scratch_bytes;
            if(scheduled->outer_moment_scratch)value+=detail::scheduled_outer_pulse_scratch_bytes;
            if(scheduled->curves)value+=sizeof(ScheduledCurveCache)+(fine_curves_allocated()?fine_curve_callee_bytes:0);
            if(scheduled->float_curves)value+=sizeof(ScheduledFloatCurveCache);
            value+=scheduled->moments.capacity()*sizeof(std::vector<ScheduledMoment>);
            for(const auto& row:scheduled->moments)value+=row.capacity()*sizeof(ScheduledMoment);
            value+=scheduled->float_signal.capacity()*sizeof(detail::scheduled_float_signal::Moments)+
                scheduled->float_counts.capacity()*sizeof(ScheduledFloatCounts)+
                (scheduled->float_signal.empty()?0:detail::scheduled_float_signal::contraction_scratch_bytes);
            value+=scheduled->float_grams.capacity()*sizeof(ScheduledFloatGramCounts)+
                (scheduled->float_grams.empty()?0:detail::scheduled_float_gram::scratch_bytes);
        }
        for(const auto& bank:banks)value+=bank.prefix.capacity()*sizeof(Projection)+bank.first_moment.capacity()*sizeof(Complex)+
            bank.affine_carrier.capacity()*sizeof(detail::CorrelationCarrierMoments);
        value+=worker_bytes();
        return value;
    }
    std::size_t projection_cache_headroom(std::size_t input_samples) const {
        if(lazy_state())return std::numeric_limits<std::size_t>::max(); // no optional cache may borrow promised history/track reserve

        // This is an allocation bound for one finite push and its next drain,
        // not a signal-dependent prediction. Optional worker caches may be
        // dropped by room_for(); promised detectors are reserved separately.
        constexpr auto maximum=std::numeric_limits<std::size_t>::max();
        if(input_samples>std::numeric_limits<std::uint64_t>::max()-sample)return maximum;
        // Partial amplitude FFT input only fills its already-reserved PCM
        // array. With no pending publication/timing work, neither the push nor
        // its next drain can grow payload, timing or output storage. Avoid the
        // per-hypothesis long-double forecast for every small audio callback.
        // Equality can complete a block and must retain the original bound.
        // Initial lattice discard only makes this test more conservative.
        if(options.amplitude_only && !amplitude_publication_dirty && !timing_dirty && bursts.empty() &&
           input_samples<amplitude_geometry->fft_samples-amplitude_partial)return 0;
        if(scheduled_map() && !options.amplitude_only && !amplitude_publication_dirty && !timing_dirty && bursts.empty() &&
           static_cast<long double>(sample)+input_samples<scheduled->earliest_end)return 0;
        std::size_t total=0,transient=0,output=0;
        const auto add=[&](std::size_t n){total=n>maximum-total?maximum:total+n;};
        const auto product=[&](std::size_t a,std::size_t b){return a && b>maximum/a?maximum:a*b;};
        std::size_t completions=0;
        for(const auto& h:hypotheses) {
            if(timing_enabled && std::ceil(clock_boundary(h,code.symbol_samples()))<=
                    static_cast<long double>(sample)+input_samples)++completions;
            // A partly accumulated symbol can finish immediately. Each later
            // completion appends at most one bit; gap events carry no bits.
            const auto slots=1+std::ceil(static_cast<long double>(input_samples)*h.rate/code.symbol_samples());
            if(!std::isfinite(slots) || slots>=static_cast<long double>(maximum))return maximum;
            const auto added=static_cast<std::size_t>(slots);
            const auto required=std::min(bit_limit,added>maximum-h.burst.bits.size()?maximum:h.burst.bits.size()+added);
            auto capacity=h.burst.bits.capacity();const auto initial=capacity;
            while(capacity<required) {
                transient=std::max(transient,capacity);
                capacity=capacity? (capacity>bit_limit/2?bit_limit:capacity*2):1;
            }
            // A failed prefix can clear its buffer and regrow from zero in
            // this same push. Include that doubling trajectory's old buffer
            // in the transient bound even if the initial capacity was ample.
            auto reset_capacity=std::size_t{0};
            const auto reset_required=std::min(bit_limit,added);
            while(reset_capacity<reset_required) {
                transient=std::max(transient,reset_capacity);
                reset_capacity=reset_capacity? (reset_capacity>bit_limit/2?bit_limit:reset_capacity*2):1;
            }
            capacity=std::max(capacity,reset_capacity);
            add(capacity-initial);
            output=std::max(output,std::min(required,std::min(search.chunk_bits,bit_limit)));
        }
        if(completions) {
            const auto current=timing?timing->size():0,initial=timing?timing->capacity():0;
            const auto added=std::min(hypotheses.size()-current,product(completions,3));
            const auto required=current+added;
            auto capacity=initial,old_peak=std::size_t{0};
            while(capacity<required) {
                old_peak=std::max(old_peak,capacity);
                capacity=std::min(hypotheses.size(),capacity>hypotheses.size()/2?hypotheses.size():std::max<std::size_t>(1,capacity*2));
            }
            if(!timing)add(sizeof(*timing));
            add(product(capacity-initial,sizeof(TimingEntry)));
            add(product(old_peak,sizeof(TimingEntry)));
        }
        // reserve(wanted) checks the complete new allocation while the old
        // payload remains charged. Publishing likewise creates one temporary
        // copy before queue insertion/replacement, alongside retained events.
        add(transient);add(product(search.track_limit,output));add(output);
        // take_bursts() moves the existing event vector and reserves its next
        // bounded queue before the caller replaces its previous drained vector.
        add(product(search.track_limit,sizeof(PatternBurst)));
        return total;
    }
    std::size_t worker_bytes() const {
        auto bytes=worker_codes.capacity()*sizeof(PatternCode);
        if(scheduled) {
            // working_bytes() already reserves the serial/first worker.
            if(scheduled->outer_moment_scratch && worker_codes.size()>1)
                bytes+=(worker_codes.size()-1)*detail::scheduled_outer_pulse_scratch_bytes;
            if(!scheduled->float_signal.empty() && worker_codes.size()>1)
                bytes+=(worker_codes.size()-1)*detail::scheduled_float_signal::contraction_scratch_bytes;
            if(!scheduled->float_grams.empty() && worker_codes.size()>1)
                bytes+=(worker_codes.size()-1)*detail::scheduled_float_gram::scratch_bytes;
            if(fine_curves_allocated() && worker_codes.size()>1)
                bytes+=(worker_codes.size()-1)*fine_curve_callee_bytes;
            bytes+=scheduled->worker_maps.capacity()*sizeof(std::unique_ptr<SchedHopSourceMap>)+
                scheduled->worker_template_caches.capacity()*sizeof(std::unique_ptr<ScheduledTemplateCache>);
            for(const auto& cache:scheduled->worker_template_caches)if(cache)bytes+=sizeof(ScheduledTemplateCache);
            bytes+=scheduled->worker_temporal_templates.capacity()*sizeof(std::unique_ptr<detail::ScheduledTemporalTemplates>);
            for(const auto& batch:scheduled->worker_temporal_templates)if(batch)
                bytes+=sizeof(detail::ScheduledTemporalTemplates)+detail::ScheduledTemporalTemplates::callee_allowance+batch->plan_cache_bytes()+batch->fp32_bytes();
            for(const auto& map:scheduled->worker_maps)bytes+=map->working_bytes();
        }
        for(const auto& worker:worker_codes)bytes+=worker.working_bytes()-sizeof(PatternCode);
        return bytes;
    }
    void drop_scheduled_temporal_templates() {
        // Same single-caller ownership as drop_workers: all parallel numeric
        // tasks have joined and their scoped TLS views restored before any
        // complete()/room_for()/external trim can reclaim this optional owner.
        if(!scheduled || scheduled->worker_temporal_templates.empty())return;
        drop_scheduled_temporal_fp32();drop_scheduled_temporal_plan_caches();
        auto bytes=scheduled->worker_temporal_templates.capacity()*sizeof(std::unique_ptr<detail::ScheduledTemporalTemplates>);
        for(const auto& batch:scheduled->worker_temporal_templates)if(batch) {
            scheduled->temporal_retired+=batch->statistics;
            bytes+=sizeof(detail::ScheduledTemporalTemplates)+detail::ScheduledTemporalTemplates::callee_allowance;
        }
        accounted_bytes-=bytes;
        decltype(scheduled->worker_temporal_templates)().swap(scheduled->worker_temporal_templates);
    }
    void drop_workers() {
        drop_scheduled_temporal_templates();drop_scheduled_common_shape();
        if(scheduled)for(const auto& cache:scheduled->worker_template_caches)if(cache) {
            work.scheduled_template_cache_hits+=cache->hits;work.scheduled_template_constructions+=cache->constructions;
        }
        accounted_bytes-=worker_bytes();
        std::vector<PatternCode>().swap(worker_codes);
        if(scheduled) {
            decltype(scheduled->worker_template_caches)().swap(scheduled->worker_template_caches);
            decltype(scheduled->worker_temporal_templates)().swap(scheduled->worker_temporal_templates);
            decltype(scheduled->worker_maps)().swap(scheduled->worker_maps);
        }
    }
    void drop_scheduled_common_shape() {
        if(!scheduled || !scheduled->common_scratch)return;
        accounted_bytes-=scheduled->common_work.capacity()*sizeof(ScheduledCommonWork)+
            std::max<std::size_t>(1,worker_codes.size())*scheduled_common_scratch_bytes;
        if(scheduled->common_raw_scratch) {
            accounted_bytes-=std::max<std::size_t>(1,worker_codes.size())*scheduled->common_raw_scratch_bytes;
            scheduled->common_raw_scratch=false;scheduled->common_raw_scratch_bytes=0;
        }
        if(scheduled->frequency_piece_scratch) {
            accounted_bytes-=std::max<std::size_t>(1,worker_codes.size())*detail::scheduled_frequency_piece_scratch_bytes;
            scheduled->frequency_piece_scratch=false;
        }
        scheduled->common_scratch=false;decltype(scheduled->common_work)().swap(scheduled->common_work);
    }
    void drop_scheduled_families() {
        drop_scheduled_common_shape();
        if(!scheduled || !scheduled->families)return;
        accounted_bytes-=scheduled->families->working_bytes();
        scheduled->families.reset();
        scheduled->extra_work.scheduled_exact_families=scheduled->extra_work.scheduled_family_groups=scheduled->extra_work.scheduled_family_metadata_bytes=0;
        // Do not repeat optional sorting on every low-space PCM push.
    }
    void drop_scheduled_float_grams() {
        if(!scheduled || scheduled->float_grams.empty())return;
        auto& target=scheduled->extra_work;
        for(const auto& c:scheduled->float_grams) {
            target.scheduled_float_gram_attempts+=c.attempts;
            target.scheduled_float_gram_accepted_pairs+=c.accepted;
            target.scheduled_float_gram_range_fallbacks+=c.range;
            target.scheduled_float_gram_rank_fallbacks+=c.rank;
            target.scheduled_float_gram_error_fallbacks+=c.error;
            target.scheduled_float_gram_helper_fallbacks+=c.helper;
            target.scheduled_float_gram_signal_attempts+=c.signal_attempts;
            target.scheduled_float_gram_maximum_relative_error=std::max(target.scheduled_float_gram_maximum_relative_error,c.maximum_relative_error);
        }
        accounted_bytes-=scheduled->float_grams.capacity()*sizeof(ScheduledFloatGramCounts)+
            std::max<std::size_t>(1,worker_codes.size())*detail::scheduled_float_gram::scratch_bytes;
        decltype(scheduled->float_grams)().swap(scheduled->float_grams);
    }
    void drop_scheduled_moments() {
        drop_scheduled_float_grams();
        if(!scheduled)return;
        for(const auto& count:scheduled->float_counts) {
            scheduled->extra_work.scheduled_float_moment_attempts+=count.attempts;
            scheduled->extra_work.scheduled_float_moment_pieces+=count.pieces;
            scheduled->extra_work.scheduled_float_moment_condition_fallbacks+=count.condition_fallbacks;
            scheduled->extra_work.scheduled_float_moment_error_fallbacks+=count.error_fallbacks;
            scheduled->extra_work.scheduled_float_moment_accepted_pairs+=count.accepted_pairs;
            scheduled->extra_work.scheduled_float_moment_fp64_pairs+=count.fp64_pairs;
            scheduled->extra_work.scheduled_float_moment_gram_fallbacks+=count.gram_fallbacks;
        }
        accounted_bytes-=scheduled->float_signal.capacity()*sizeof(detail::scheduled_float_signal::Moments)+
            scheduled->float_counts.capacity()*sizeof(ScheduledFloatCounts)+
            (scheduled->float_signal.empty()?0:std::max<std::size_t>(1,worker_codes.size())*
                detail::scheduled_float_signal::contraction_scratch_bytes);
        decltype(scheduled->float_signal)().swap(scheduled->float_signal);
        decltype(scheduled->float_counts)().swap(scheduled->float_counts);
        if(scheduled->moments.empty())return;
        auto bytes=scheduled->moments.capacity()*sizeof(std::vector<ScheduledMoment>);
        for(const auto& row:scheduled->moments)bytes+=row.capacity()*sizeof(ScheduledMoment);
        if(scheduled->outer_moment_scratch) {
            bytes+=std::max<std::size_t>(1,worker_codes.size())*detail::scheduled_outer_pulse_scratch_bytes;
            scheduled->outer_moment_scratch=false;
        }
        accounted_bytes-=bytes;
        decltype(scheduled->moments)().swap(scheduled->moments);
    }
    void drop_scheduled_curves() {
        if(!scheduled)return;
        const auto drop=[&](auto& ptr) {
            if(!ptr)return;
            scheduled->curve_hits+=ptr->hits;scheduled->curve_misses+=ptr->misses;
            scheduled->curve_evictions+=ptr->evictions;scheduled->curve_exhaustions+=ptr->exhaustions;
            using Cache=std::remove_reference_t<decltype(*ptr)>;
            auto bytes=sizeof(Cache);
            if constexpr(std::is_same_v<Cache,ScheduledCurveCache>)if(fine_curves_allocated())
                bytes+=std::max<std::size_t>(1,worker_codes.size())*fine_curve_callee_bytes;
            ptr.reset();accounted_bytes-=bytes;
        };
        drop(scheduled->curves);drop(scheduled->float_curves);
    }
    void drop_affine_coefficients() {
        accounted_bytes-=affine_coefficients.capacity()*sizeof(AffineCoefficients);
        decltype(affine_coefficients)().swap(affine_coefficients);
        work.affine_coefficient_cache_entries=0;
    }
    void room_for(std::size_t extra) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::allocation,true);
        // The joined Live cohort may hold an exact source-only cache outside
        // this receiver. Release that lowest-priority grant BEFORE evicting
        // any original acceleration. Its callback
        // reclaims ONLY unpinned backing, keeps the borrowed Pool tombstone
        // alive, and restores this receiver's wrapper/core loan synchronously.
        // Numeric workers never inherit the pressure callback or mutate Bank.
        if(lazy_state() && !lazy_state()->screens_running && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
           extra>budget-sizeof(PatternCorrelator)-accounted_bytes)) {
            const auto binding=detail::scheduled_source_samples::binding;
            if(binding.reclaim)binding.reclaim(binding.pressure_context);
        }
        if(lazy_state() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
           extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_lazy_colored_sources();
        // Called outside affine accumulation, before retained payload/event
        // allocations. No private coefficient pointer survives this eviction.
        if(lazy_state() && lazy_state()->detached_replay && !lazy_state()->detached_replay->numeric_active &&
           (sizeof(PatternCorrelator)+accounted_bytes>budget ||
            extra>budget-sizeof(PatternCorrelator)-accounted_bytes))lazy_drop_detached_replay();
        if(lazy_state() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
           extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_lazy_projections();
        if(lazy_state() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
           extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_lazy_plans();
        // The new fine diagnostic is the lowest-priority optional allocation.
        if(fine_curves_allocated() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
           extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_scheduled_curves();
        if(sizeof(PatternCorrelator)+accounted_bytes>budget ||
           extra>budget-sizeof(PatternCorrelator)-accounted_bytes)drop_scheduled_temporal_fp32();
        if(sizeof(PatternCorrelator)+accounted_bytes>budget ||
           extra>budget-sizeof(PatternCorrelator)-accounted_bytes)drop_scheduled_temporal_plan_caches();
        if(scheduled && !scheduled->worker_temporal_templates.empty() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_scheduled_temporal_templates();
        if(scheduled && scheduled->families && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_scheduled_families();
        if(scheduled && (scheduled->curves || scheduled->float_curves) && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_scheduled_curves();
        if(scheduled && (!scheduled->moments.empty() || !scheduled->float_signal.empty() || !scheduled->float_grams.empty()) && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_scheduled_moments();
        if(!affine_coefficients.empty() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_affine_coefficients();
        if((!worker_codes.empty() || (scheduled && (!scheduled->worker_template_caches.empty() ||
                !scheduled->worker_temporal_templates.empty()))) && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_workers();
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget && extra<=budget-sizeof(PatternCorrelator)-accounted_bytes,
                "pattern evidence exceeds DSP workspace");
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes+extra);
    }
    double threshold() const {
        const auto t=static_cast<double>(trials);
        return -std::log(search.false_alarm_probability)+std::log(t)+std::log(t+1)+std::log(2.);
    }
    void remember(PatternEvidence evidence) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::publication);
        if(evidence.score<search.retain_score)return;
        evidence.admission_threshold=threshold();
        if(history.capacity()<search.candidate_limit) {
            require(search.candidate_limit<=std::numeric_limits<std::size_t>::max()/sizeof(PatternEvidence),
                "pattern diagnostic history reservation overflow");
            // Future backing is already charged. Reserve only the additional
            // old-buffer overlap, then replace the promise with actual bytes.
            const auto previous=history.capacity()*sizeof(PatternEvidence)+unallocated_history_bytes();
            room_for(history.capacity()*sizeof(PatternEvidence));
            history.reserve(search.candidate_limit);
            accounted_bytes=accounted_bytes-previous+history.capacity()*sizeof(PatternEvidence)+unallocated_history_bytes();
        }
        if(history.size()==search.candidate_limit)history.erase(history.begin());
        history.push_back(evidence);
    }
    PhaseGroups phase_groups(const Hypothesis& h,std::size_t& count)const {
        PhaseGroups groups;count=0;
        if(scheduled_map()){groups.small[0]={h.phase_lower,h.phase_upper};count=1;return groups;}
        if(config.fake_hopping && config.fake_hopping->matched_gaps && h.phase_lower<=h.phase_upper) {
            groups.first=h.phase_lower;groups.step=phase_step;
            count=(h.phase_upper-h.phase_lower)/phase_step+1;return groups;
        }
        auto lower=h.phase_lower;
        while(lower<=h.phase_upper) {
            const auto address=symbol_stream_address(config.stream_epoch,lower,h.index,code.symbol_samples(),config.sample_rate);
            auto low=std::uint64_t{0},high=(h.phase_upper-lower)/phase_step;
            while(low<high) {
                const auto mid=low+(high-low+1)/2;
                const auto candidate=symbol_stream_address(config.stream_epoch,lower+mid*phase_step,h.index,
                    code.symbol_samples(),config.sample_rate);
                if(candidate.epoch==address.epoch && candidate.ordinal==address.ordinal)low=mid;
                else high=mid-1;
            }
            const auto end=lower+low*phase_step;
            require(count<groups.small.size(),"pattern phase groups exceed finite symbol interval");
            groups.small[count++]={lower,end};
            if(end==h.phase_upper)break;
            lower=end+phase_step;
        }
        return groups;
    }
    std::array<Fit,2>& fits(Hypothesis& h,std::size_t hypothesis,std::size_t group) {
        if(!group)return h.fits;
        require(group<=alternate_groups,"pattern phase fit exceeds its allocated bank");
        return alternate_fits[hypothesis*alternate_groups+group-1];
    }
    std::array<DriftFit,2>& section_fits(std::size_t hypothesis,std::size_t group) {
        return drift_fits[hypothesis*(alternate_groups+1)+group];
    }
    std::array<DifferentialFit,2>& local_fits(std::size_t hypothesis,std::size_t group) {
        return differential_fits[hypothesis*(alternate_groups+1)+group];
    }
    Emission& output_stream(Hypothesis& h) {
        const auto same_clock=[&](long double origin,double frequency) {
            return std::abs(origin-h.origin)<static_cast<long double>(code.symbol_samples())/3 &&
                ((!search.hypotheses.empty() && search.frequency_rate_competition) ||
                 std::abs(frequency-h.burst.frequency_hz)<=config.sample_rate/static_cast<double>(code.symbol_samples()));
        };
        for(auto& emission:emissions)if(same_clock(emission.origin,emission.frequency) &&
            (!emission.ended || h.burst.stream_first_symbol<emission.last_symbol))return emission;
        // Choose before output starts. A later timing guide may continue only
        // after proving the entire published prefix; delivered bytes and this
        // emission's physical identity are immutable.
        const Hypothesis* owner=&h;
        for_each_resident([&](std::size_t,const auto& candidate){if(candidate.admitted && same_clock(candidate.origin,candidate.burst.frequency_hz) &&
            (candidate.burst.stream_first_symbol<owner->burst.stream_first_symbol ||
             (candidate.burst.stream_first_symbol==owner->burst.stream_first_symbol && candidate.sum_score>owner->sum_score)))owner=&candidate;});
        Emission emission{owner,owner->burst.stream_first_symbol,owner->burst.first_stream_symbol,
            owner->burst.stream_first_sample,owner->origin,owner->burst.frequency_hz,
            owner->burst.frequency_hz,owner->burst.score,false};
        if(emissions.size()==search.track_limit) {
            const auto reusable=std::find_if(emissions.begin(),emissions.end(),[](const auto& item){return item.ended;});
            require(reusable!=emissions.end(),"pattern output stream quota exhausted");*reusable=emission;return *reusable;
        }
        emissions.push_back(emission);return emissions.back();
    }
    double carrier_estimate(const Hypothesis& owner,Emission& stream) const {
        // Keep immutable bit ownership, but report the strongest accumulated
        // carrier evidence over comparable observations. A startup distortion
        // must not lock the displayed frequency to its first winning grid bin.
        const auto* best=&owner;
        for_each_resident([&](std::size_t,const auto& candidate) {
            if(!candidate.admitted || candidate.burst.stream_first_symbol!=owner.burst.stream_first_symbol)return;
            const auto difference=candidate.committed_end>owner.committed_end?
                candidate.committed_end-owner.committed_end:owner.committed_end-candidate.committed_end;
            if(difference>code.symbol_samples() || std::abs(candidate.origin-owner.origin)>=code.symbol_samples()/3.L ||
               ((!search.hypotheses.empty() && search.frequency_rate_competition)?false:
                std::abs(candidate.burst.frequency_hz-owner.burst.frequency_hz)>
                    config.sample_rate/static_cast<double>(code.symbol_samples())))return;
            if(candidate.burst.score>best->burst.score)best=&candidate;
        });
        // Nearby clocks may retire in a different order at the terminal
        // absence observation. Their retirement must not revert an already
        // stronger accumulated carrier estimate to the startup hypothesis.
        if(best->burst.score>=stream.carrier_score) {
            stream.carrier_score=best->burst.score;stream.reported_frequency=best->burst.frequency_hz;
        }
        return stream.reported_frequency;
    }
    std::size_t process_count(std::size_t count) const {
        if(scheduled_map() && !options.amplitude_only && timing_enabled) {
            bool common_fence=bit_limit<2;
            if(!common_fence)for(const auto& h:hypotheses)if(h.committed)
                for(const auto& stream:emissions)if(stream.owner!=&h && retain_timing_suffix(h,stream))
                    common_fence=true;
            if(!common_fence)return count;
            if(lazy_state()){const auto span=std::ceil(lazy_state()->next_due)>sample?std::ceil(lazy_state()->next_due)-sample:1.L;
                return std::min(count,static_cast<std::size_t>(std::min<long double>(count,std::max(1.L,span))));}
            // Preserve the one-bit budget and any unresolved retained suffix.
            // A later owner settles before the earlier guide can require a
            // further slot or resume across an as-yet-unresolved missing run.
            // Normal blocks have at most two complete symbols (Fs>=64,T>=6s),
            // so newly retained work cannot cross a gap and resume in one block.
            for(const auto& h:hypotheses) {
                const auto boundary=std::ceil(clock_boundary(h,code.symbol_samples()));
                const auto span=boundary>static_cast<long double>(sample)?boundary-sample:1.L;
                count=std::min(count,static_cast<std::size_t>(std::min<long double>(count,std::max(1.L,span))));
            }
        }
        return count;
    }
    void trim_emitted_prefix(Hypothesis& h,const Emission& stream) {
        if(h.burst.first_stream_symbol>=stream.last_symbol || !h.committed)return;
        const auto duplicate=std::min<std::uint64_t>(h.committed,stream.last_symbol-h.burst.first_stream_symbol);
        const auto through=h.burst.first_stream_symbol+duplicate;
        const auto end=duplicate==h.committed?h.committed_end:static_cast<std::uint64_t>(std::max(0.L,
            std::ceil(scheduled_boundary(h,scheduled_source_start(h,through-1)+code.symbol_samples(),true))));
        h.burst.bits.erase(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(duplicate));
        h.burst.first_stream_symbol=through;h.burst.first_sample=end;
        h.committed-=static_cast<std::size_t>(duplicate);
    }
    bool retain_timing_suffix(const Hypothesis& h,const Emission& stream) const {
        if(!scheduled_map() || options.amplitude_only || !timing_enabled || !timing ||
           stream.ended || stream.published_unknown || h.burst.stream_first_symbol!=stream.first_symbol ||
           h.frequency!=stream.owner->frequency || h.rate!=stream.owner->rate ||
           h.phase_lower!=stream.owner->phase_lower || h.phase_upper!=stream.owner->phase_upper)return false;
        const auto index=static_cast<std::size_t>(&h-hypotheses.data());
        // An intermediate qualified guide may later become owner. Keep only
        // suffixes with a live incoming full-prefix proof, never an alias whose
        // complete original-prefix proof has permanently failed.
        for(const auto& entry:*timing)if(entry.state.tracking && entry.state.first==stream.first_symbol)
            for(const auto& relation:entry.state.relations)
                if(!relation.invalid && relation.neighbor==index)return true;
        return false;
    }
    void trim_known_alias_prefix(Hypothesis& h) {
        if(!scheduled_map() || options.amplitude_only || !h.admitted)return;
        for(const auto& stream:emissions)if(stream.owner!=&h && stream.first_symbol==h.burst.stream_first_symbol &&
            std::abs(stream.origin-h.origin)<static_cast<long double>(code.symbol_samples())/3 &&
            ((!search.hypotheses.empty() && search.frequency_rate_competition) ||
             std::abs(stream.frequency-h.burst.frequency_hz)<=config.sample_rate/static_cast<double>(code.symbol_samples()))) {
            if(retain_timing_suffix(h,stream))trim_emitted_prefix(h,stream);
            else {
                // Reclaim permanently unusable aliases before the quota check,
                // including an owner that ended since the last consumer poll.
                h.burst.bits.erase(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(h.committed));
                h.burst.first_stream_symbol+=h.committed;h.burst.first_sample=h.committed_end;h.committed=0;
            }
            return;
        }
    }
    void publish(Hypothesis& h,bool complete=false,bool flush=true,bool draining=false) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::publication);
        if(!h.admitted)return;
        if(draining && !h.committed)return;
        const auto chunk=std::min(search.chunk_bits,bit_limit);
        if(!complete && !flush && h.committed<chunk)return;
        auto& stream=output_stream(h);
        if(stream.owner!=&h) {
            if(retain_timing_suffix(h,stream) && !complete) {
                // A later owner decision may qualify this guide in the same
                // input block. Only already-emitted ordinals are duplicates.
                trim_emitted_prefix(h,stream);
            } else {
                // Ended, unknown, incompatible or disproved aliases cannot
                // replace immutable observations. Terminal retirement happens
                // only after every available ownership proof has settled.
                h.burst.bits.erase(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(h.committed));
                h.burst.first_stream_symbol+=h.committed;h.burst.first_sample=h.committed_end;h.committed=0;
            }
            return;
        }
        do {
            const auto count=std::min(h.committed,chunk);
            if(!count && !complete)break;
            if(draining && bursts.size()==search.track_limit)break;
            room_for(count);
            PatternBurst result;
            result.first_sample=h.burst.first_sample;result.first_stream_symbol=h.burst.first_stream_symbol;
            result.stream_first_sample=stream.stream_first_sample;result.stream_first_symbol=stream.first_symbol;
            result.frequency_hz=carrier_estimate(h,stream);result.stream_phase_samples=h.burst.stream_phase_samples;
            result.bits.assign(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            result.complete=complete && count==h.committed;
            result.end_sample=count==h.committed?h.committed_end:scheduled_map()?
                static_cast<std::uint64_t>(std::max(0.L,std::ceil(scheduled_boundary(h,
                    scheduled_source_start(h,result.first_stream_symbol+count-1)+code.symbol_samples(),true)))):
                result.first_sample+static_cast<std::uint64_t>(count*code.symbol_samples()/h.rate);
            result.score=h.burst.score;
            result.support_samples=h.burst.support_samples;
            const auto duplicate=std::find_if(bursts.begin(),bursts.end(),[&](const auto& prior) {
                const auto delta=prior.first_sample>result.first_sample?prior.first_sample-result.first_sample:result.first_sample-prior.first_sample;
                return prior.first_stream_symbol==result.first_stream_symbol && prior.complete==result.complete &&
                    delta<code.symbol_samples()/3 && ((!search.hypotheses.empty() && search.frequency_rate_competition) ||
                    std::abs(prior.frequency_hz-result.frequency_hz)<=config.sample_rate/static_cast<double>(code.symbol_samples()));
            });
            const auto emitted_end=result.end_sample;
            if(scheduled_map() && !options.amplitude_only)audio::trace::record(result.complete?"split_complex_physical_end":"split_complex_progress",[&](auto& f) {
                f.add("epoch",config.stream_epoch);f.add("input_samples",sample);f.add("first_sample",result.first_sample);
                f.add("end_sample",result.end_sample);f.add("first_stream_symbol",result.first_stream_symbol);
                f.add("accepted_count",count);f.add("complete",result.complete);f.add("trials",trials);
                f.add("score",result.score);f.add("threshold",threshold());
            });
            if(duplicate!=bursts.end()) {
                if(result.score>duplicate->score) {
                    accounted_bytes=accounted_bytes-duplicate->bits.capacity()+result.bits.capacity();*duplicate=std::move(result);
                }
            } else {
                require(bursts.size()<search.track_limit,"pattern output queue requires draining");
                accounted_bytes+=result.bits.capacity();bursts.push_back(std::move(result));
            }
            h.burst.bits.erase(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            h.burst.first_stream_symbol+=count;h.burst.first_sample=emitted_end;h.committed-=count;
            stream.last_symbol=h.burst.first_stream_symbol;
            if(complete && !h.committed)stream.ended=true;
            if(!h.committed)break;
        } while(complete || flush || h.committed>=chunk);
    }
    void clear(Hypothesis& h) {
        if(auto* state=timing_state(static_cast<std::size_t>(&h-hypotheses.data())))
            state->tracking=state->pending_terminal=false;
        // Keep the finite clock hypothesis, but release a terminated message's
        // payload allocation so later hypotheses can use the same workspace.
        accounted_bytes-=h.burst.bits.capacity();Bytes{}.swap(h.burst.bits);
        h.burst.complete=false;h.admitted=h.pending_gaps=false;
        h.sum_score=h.pending_score=h.burst.score=0;
        if(!pending_chip_scores.empty())pending_chip_scores[static_cast<std::size_t>(&h-hypotheses.data())]=0;
        h.committed=0;h.gap_slots=0;
        h.sum_support=h.burst.support_samples=0;
    }
    void append(Hypothesis& h,std::uint8_t bit) {
        require(h.burst.bits.size()<bit_limit,"pattern bit retention limit reached");
        if(h.burst.bits.size()==h.burst.bits.capacity()) {
            const auto capacity=h.burst.bits.capacity();const auto wanted=std::min(bit_limit,std::max<std::size_t>(1,capacity*2));
            room_for(wanted);h.burst.bits.reserve(wanted);accounted_bytes+=h.burst.bits.capacity()-capacity;
        }
        h.burst.bits.push_back(bit);
    }
    void mark_pending_gaps(Hypothesis& h) {
        if(!h.pending_gaps) {
            h.gap_slots=h.burst.bits.size()-h.committed;h.burst.bits.resize(h.committed);
        }
        h.sum_score=h.burst.score;h.pending_score=0;h.pending_gaps=true;
        if(!pending_chip_scores.empty())pending_chip_scores[static_cast<std::size_t>(&h-hypotheses.data())]=0;
        h.sum_support=h.burst.support_samples;
    }
    void emit_gap(Hypothesis& h,std::uint64_t resumed_sample) {
        if(!h.gap_slots)return;
        publish(h);
        auto& stream=output_stream(h);
        if(stream.owner!=&h) {
            // The existing contiguous bit vector cannot join an unpublished
            // committed suffix across an unresolved erasure run. Never silently
            // relabel its ordinals or manufacture completion to free storage.
            require(!h.committed,"timing continuation suffix crosses unresolved missing symbols");
            h.gap_slots=0;h.burst.first_sample=resumed_sample;h.burst.first_stream_symbol=h.index;return;
        }
        require(bursts.size()<search.track_limit,"pattern output queue requires draining");
        PatternBurst event;
        event.first_sample=h.burst.first_sample;event.end_sample=resumed_sample;
        event.first_stream_symbol=h.burst.first_stream_symbol;
        event.stream_first_sample=stream.stream_first_sample;event.stream_first_symbol=stream.first_symbol;
        event.frequency_hz=carrier_estimate(h,stream);event.score=h.burst.score;
        event.support_samples=h.burst.support_samples;
        event.stream_phase_samples=h.burst.stream_phase_samples;event.missing_slots=h.gap_slots;
        bursts.push_back(std::move(event));h.gap_slots=0;
        // Unknown observations are immutable too. A later guide cannot turn
        // already published erasures into received bits through an owner swap.
        stream.published_unknown=true;
        h.burst.first_sample=resumed_sample;h.burst.first_stream_symbol=h.index;stream.last_symbol=h.index;
    }
    TimingState* timing_state(std::size_t hypothesis) {
        if(!timing)return nullptr;
        if(lazy_state()){const auto it=std::find_if(timing->begin(),timing->end(),[&](const auto& e){return e.hypothesis==hypothesis;});
            return it==timing->end()?nullptr:&it->state;}
        const auto it=std::lower_bound(timing->begin(),timing->end(),hypothesis,
            [](const TimingEntry& entry,std::size_t index){return entry.hypothesis<index;});
        return it!=timing->end() && it->hypothesis==hypothesis?&it->state:nullptr;
    }
    void subscribe_timing(std::size_t hypothesis) {
        if(timing_state(hypothesis))return;
        if(!timing) {
            room_for(sizeof(*timing));timing=std::make_unique<std::vector<TimingEntry>>();
            accounted_bytes+=sizeof(*timing);
        }
        if(timing->size()==timing->capacity()) {
            const auto old=timing->capacity();
            const auto capacity=std::min(hypotheses.size(),std::max<std::size_t>(1,old*2));
            // reserve temporarily owns both buffers; account that peak too.
            room_for(capacity*sizeof(TimingEntry));timing->reserve(capacity);
            accounted_bytes+=(timing->capacity()-old)*sizeof(TimingEntry);
        }
        const auto it=std::lower_bound(timing->begin(),timing->end(),logical_id(hypothesis),
            [&](const TimingEntry& entry,std::size_t index){return logical_id(entry.hypothesis)<index;});
        timing->insert(it,TimingEntry{hypothesis,{}});
    }
    void timing_decision(Hypothesis& h,std::size_t hypothesis,const PatternEvidence& evidence,std::size_t selected) {
        if(!timing_enabled)return;
        if(h.admitted) {
            subscribe_timing(hypothesis);
            for(unsigned side=0;side<2;++side) {
                const auto id=logical_id(hypothesis);
                if((!side && !id) || (side && id+1==logical_count()))continue;
                const auto neighbor_id=side?id+1:id-1;
                auto j=neighbor_id;
                if(lazy_state()){
                    const auto descriptor=lazy_state()->catalog->describe(neighbor_id,*lazy_state()->cursor);
                    if(h.frequency!=descriptor.frequency || h.rate!=descriptor.rate ||
                       std::abs(h.origin-descriptor.origin)>=static_cast<long double>(code.symbol_samples())/h.rate)continue;
                    // An earlier unsubscribed lane has already had its original
                    // completion opportunity. Materialize its current index,
                    // NOT a reconstructed past accepted-prefix decision.
                    const auto frontier=neighbor_id<id?lazy_state()->block_end:lazy_state()->processed;
                    j=lazy_ensure_slot(neighbor_id,lazy_index_at(descriptor,frontier));
                }
                const auto& neighbor=hypotheses[j];
                if(h.frequency==neighbor.frequency && h.rate==neighbor.rate &&
                    std::abs(h.origin-neighbor.origin)<static_cast<long double>(code.symbol_samples())/h.rate) {
                    subscribe_timing(j);timing_state(hypothesis)->relations[side].neighbor=j;
                }
            }
        }
        auto* found=timing_state(hypothesis);if(!found)return;
        timing_dirty=true;auto& state=*found;
        const bool accepted=h.admitted && h.committed_end==evidence.end_sample;
        if(h.admitted && (!state.tracking || state.first!=h.burst.stream_first_symbol)) {
            state.tracking=true;state.pending_terminal=false;state.first=h.burst.stream_first_symbol;
            state.last_accepted=state.first;
            for(auto& relation:state.relations) {
                relation.next=state.first;relation.first_unaccepted=std::numeric_limits<std::uint64_t>::max();
                relation.owner_score=relation.guide_score=0;relation.invalid=false;
                if(relation.neighbor!=std::numeric_limits<std::size_t>::max()) {
                    const auto* neighbor=timing_state(relation.neighbor);
                    // A previously failed earlier lane cannot be reconstructed
                    // from a later matching suffix of this admitted prefix.
                    if(hypotheses[relation.neighbor].index>state.first &&
                        (!neighbor || neighbor->decisions[state.first%2].index!=state.first))relation.invalid=true;
                }
            }
        }
        const auto address=scheduled_map()?symbol_stream_address(config,config.stream_epoch,evidence.stream_phase_samples,h.index):
            symbol_stream_address(config.stream_epoch,evidence.stream_phase_samples,h.index,code.symbol_samples(),config.sample_rate);
        state.decisions[h.index%2]={h.index,address.epoch,address.ordinal,evidence.score,accepted,
            (scheduled_map() && !options.amplitude_only?scheduled->observed_begin[hypothesis]<=scheduled_source_start(h,h.index):
                static_cast<long double>(h.observed_start)==std::ceil(symbol_start(h))) &&
            static_cast<long double>(evidence.end_sample)==std::ceil(clock_boundary(h,code.symbol_samples())) &&
            (scheduled_map()?(options.amplitude_only?fits(h,hypothesis,selected)[evidence.bit].count>=
                static_cast<std::uint64_t>(std::floor(code.symbol_samples()/h.rate)):
                scheduled->observed_begin[hypothesis]<=scheduled_source_start(h,h.index) &&
                    fits(h,hypothesis,selected)[evidence.bit].count>2):
                fits(h,hypothesis,selected)[evidence.bit].count==evidence.end_sample-h.observed_start),
            static_cast<std::uint8_t>(evidence.bit)};
        if(accepted){state.last_accepted=h.index;state.pending_terminal=false;}
    }
    bool continue_timing(Hypothesis& owner,const TimingState& state,const TimingRelation& proof) {
        // Continuation is a private, same-clock proof. Never replace an emitted
        // bit, infer an old prefix from a matching suffix, or cross a key/phase
        // address. The relation has checked every full admitted prefix decision.
        if(!scheduled_map() || options.amplitude_only || proof.invalid ||
           proof.neighbor==std::numeric_limits<std::size_t>::max() ||
           proof.next<=state.last_accepted || proof.guide_score<=proof.owner_score)return false;
        auto& guide=hypotheses[proof.neighbor];
        const auto* guide_state=timing_state(proof.neighbor);
        if(!guide.admitted || !guide_state || !guide_state->tracking ||
           guide_state->first!=state.first || guide.burst.stream_first_symbol!=state.first ||
           guide.frequency!=owner.frequency || guide.rate!=owner.rate ||
           guide.phase_lower!=owner.phase_lower || guide.phase_upper!=owner.phase_upper)return false;
        for(auto& stream:emissions) {
            if(stream.ended || stream.published_unknown || stream.owner!=&owner || stream.first_symbol!=state.first ||
               stream.last_symbol>proof.next || stream.last_symbol>proof.first_unaccepted ||
               std::abs(guide.origin-stream.origin)>=static_cast<long double>(code.symbol_samples())/3 ||
               guide.burst.first_stream_symbol>stream.last_symbol)continue;
            const auto duplicate=stream.last_symbol-guide.burst.first_stream_symbol;
            if(duplicate>guide.committed)continue; // A lost suffix cannot be reconstructed.
            if(duplicate) {
                const auto end=static_cast<std::uint64_t>(std::max(0.L,std::ceil(scheduled_boundary(guide,
                    scheduled_source_start(guide,stream.last_symbol-1)+code.symbol_samples(),true))));
                guide.burst.bits.erase(guide.burst.bits.begin(),guide.burst.bits.begin()+
                    static_cast<std::ptrdiff_t>(duplicate));
                guide.burst.first_stream_symbol=stream.last_symbol;guide.burst.first_sample=end;
                guide.committed-=static_cast<std::size_t>(duplicate);
            }
            // Neither absence age nor the established first sample/ordinal is
            // copied from the old timing lane. The guide retains its own fully
            // scored decisions and future source-symbol completion boundary.
            stream.owner=&guide;amplitude_publication_dirty=true;
            return true;
        }
        return false;
    }
    void settle_timing(bool continuation_only=false) {
        if(!timing || !timing_dirty)return;
        // Early settlement can prevent chunk_bits=1 from dropping a newly
        // accepted guide bit. Keep terminal work dirty for its normal fence.
        if(!continuation_only)timing_dirty=false;
        for(auto& entry:*timing) {
            auto& owner=hypotheses[entry.hypothesis];auto& state=entry.state;
            if(!state.tracking || !owner.admitted)continue;
            for(auto& relation:state.relations) {
                if(relation.invalid || relation.neighbor==std::numeric_limits<std::size_t>::max())continue;
                const auto& other=*timing_state(relation.neighbor);
                while(relation.next<owner.index) {
                    const auto& a=state.decisions[relation.next%2];
                    const auto& b=other.decisions[relation.next%2];
                    if((a.index!=std::numeric_limits<std::uint64_t>::max() && a.index>relation.next) ||
                       (b.index!=std::numeric_limits<std::uint64_t>::max() && b.index>relation.next)) {
                        relation.invalid=true;break;
                    }
                    if(a.index!=relation.next || b.index!=relation.next)break;
                    if(a.epoch!=b.epoch || a.ordinal!=b.ordinal ||
                       (a.accepted && (!a.full || !b.full || !b.accepted || a.bit!=b.bit))) {
                        relation.invalid=true;break;
                    }
                    if(a.accepted){relation.owner_score+=a.score;relation.guide_score+=b.score;}
                    else relation.first_unaccepted=std::min(relation.first_unaccepted,a.index);
                    ++relation.next;
                }
            }
        }
        // All original-prefix proofs are current before an earlier guide can
        // be retired. Strict matched-prefix improvement makes promotion acyclic;
        // bounded repeats also cover a guide earlier in the stored lane order.
        for(std::size_t pass=0;pass<timing->size();++pass) {
            bool promoted=false;
            for(auto& entry:*timing) {
                auto& owner=hypotheses[entry.hypothesis];auto& state=entry.state;
                if(!state.tracking || !owner.admitted)continue;
                TimingRelation* best=nullptr;
                for(auto& relation:state.relations)
                    if(!relation.invalid && relation.neighbor!=std::numeric_limits<std::size_t>::max() &&
                       relation.next>state.last_accepted && relation.guide_score>relation.owner_score &&
                       (!best || relation.guide_score-relation.owner_score>best->guide_score-best->owner_score))best=&relation;
                if(best) {
                    bool continued=continue_timing(owner,state,*best);
                    // A stronger stale or incompatible guide must not hide
                    // the other bounded neighbor's eligible continuation.
                    if(!continued)for(auto& relation:state.relations)
                        if(&relation!=best && continue_timing(owner,state,relation)){continued=true;break;}
                    if(continued){state.pending_terminal=false;promoted=true;}
                }
            }
            if(!promoted)break;
            require(pass+1<timing->size(),"timing continuation ownership did not converge");
        }
        if(continuation_only)return;
        // A newly promoted owner may already hold a full publication chunk.
        // Publish it before the next accumulation so admitted bit_limit=1
        // configurations do not need an uncharged second retained slot.
        for(auto& stream:emissions)if(!stream.ended && stream.owner->admitted)
            publish(*const_cast<Hypothesis*>(stream.owner),false,false);
        for(auto& entry:*timing) {
            auto& owner=hypotheses[entry.hypothesis];auto& state=entry.state;
            if(!state.tracking || !owner.admitted || !state.pending_terminal)continue;
            bool has_guide=false;
            for(const auto& relation:state.relations)
                has_guide=has_guide || (!relation.invalid && relation.neighbor!=std::numeric_limits<std::size_t>::max() &&
                    relation.next>state.last_accepted && relation.guide_score>relation.owner_score);
            if(has_guide) {
                bool ready=true,accepted=false;
                for(const auto& relation:state.relations) {
                    if(relation.invalid || relation.neighbor==std::numeric_limits<std::size_t>::max() ||
                        relation.next<=state.last_accepted || relation.guide_score<=relation.owner_score)continue;
                    const auto& proof=timing_state(relation.neighbor)->decisions[state.terminal_index%2];
                    const auto& own=state.decisions[state.terminal_index%2];
                    if(proof.index!=state.terminal_index || !proof.full || own.index!=state.terminal_index ||
                        !own.full || proof.epoch!=own.epoch || proof.ordinal!=own.ordinal)ready=false;
                    else accepted=accepted || proof.accepted;
                }
                if(accepted){state.pending_terminal=false;continue;}
                if(!ready)continue;
            }
            // Either the owner is strongest over the exact same accepted
            // prefix, or its qualified later guide has actually finished the
            // corresponding absent duration. No timer or EOF supplies evidence.
            publish(owner,true);clear(owner);
        }
    }
    void complete(Hypothesis& h,std::size_t hypothesis,std::uint64_t end,std::stop_token stop,certdiag::Probe* certificate_probe=nullptr) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::guards);
        amplitude_publication_dirty=true;
        std::size_t group_count=0;
        const auto groups=phase_groups(h,group_count);
        if(scheduled_map() && !options.amplitude_only && differential_window) {
            auto& local=local_fits(hypothesis,0);
            for(auto& item:local)if(item.initialized) {
                const auto begin=scheduled_source_start(h,h.index)+item.window_index*differential_window;
                item.expected_count=begin>=scheduled->observed_begin[hypothesis]?item.active.count:0;
            }
        }
        PatternEvidence e;e.score=-1;std::size_t selected=0;
        require(group_count<=std::numeric_limits<std::uint64_t>::max()-trials,"pattern trial counter overflow");
        trials+=group_count;
        for(std::size_t group=0;group<group_count;++group) {
            const auto& fit=fits(h,hypothesis,group);
            double a=0,b=0;std::array<std::size_t,2> selected_channel{};
            if(options.amplitude_only) {
                const auto penalty=std::log(static_cast<double>(amplitude_channels.size()));
                for(std::size_t channel=0;channel<amplitude_channels.size();++channel) {
                    const auto& envelope=amplitude_fits[(hypothesis*(alternate_groups+1)+group)*amplitude_channels.size()+channel];
                    const auto zero=std::max(0.,envelope.score(amplitude_templates[hypothesis*(alternate_groups+1)+group],0,amplitude_channels[channel].bin_count)-penalty),one=std::max(0.,envelope.score(amplitude_templates[hypothesis*(alternate_groups+1)+group],1,amplitude_channels[channel].bin_count)-penalty);
                    if(zero>a){a=zero;selected_channel[0]=channel;}
                    if(one>b){b=one;selected_channel[1]=channel;}
                }
            } else if(scheduled_map()) {
                const auto score=[&](unsigned bit) {
                    const auto raw=fit[bit].score();
                    if(certificate_probe && certdiag::less_nonnegative(certificate_probe->raw,raw))certificate_probe->raw=raw;
                    const auto& chips=chip_evidence[hypothesis][bit];
                    // Raw white-noise energy and sample degrees remain paired.
                    // Only certified structured excess activates the existing
                    // projected-chip guard, which can never increase evidence.
                    if(!options.scheduled_raw_score_reference && chips.colored_excess(fit[bit],threshold())) {
                        ++work.scheduled_colored_guard_activations;
                        return std::min(raw,chips.score(fit[bit]));
                    }
                    return raw;
                };
                a=score(0);b=score(1);
            } else {a=fit[0].score();b=fit[1].score();}
            if(drift_sections>1) {
                const auto& drift=section_fits(hypothesis,group);
                const auto section_score=[&](unsigned bit) {
                    if(!scheduled_map())return drift[bit].score(fit[bit],drift_sections,code.chip_samples());
                    const auto& item=drift[bit];
                    const auto all=(1U<<(drift_sections-1))-1;
                    bool aligned=true;
                    for(unsigned section=1;section<drift_sections;++section)
                        aligned&=detail::drift_boundary(section,code.symbol_samples(),drift_sections)%code.chip_samples()==0;
                    if(!aligned || item.section+1!=drift_sections ||
                       scheduled->scheduled_quarter_ranks[hypothesis][bit]!=all || !full_rank(item.active)) {
                        ++work.scheduled_quarter_guard_fallbacks;
                        return item.score(fit[bit],drift_sections,code.chip_samples());
                    }
                    const auto last=item.active.explained(),full=item.explained_sum+last;
                    const auto retained=full-std::max(item.largest_explained,last);
                    const auto raw=detail::drift_evidence(retained,fit[bit].energy,static_cast<double>(fit[bit].count),drift_sections,true);
                    if(options.scheduled_raw_score_reference)return raw;
                    const auto& chips=chip_evidence[hypothesis][bit];
                    const auto [last_energy,last_rank]=detail::CorrelationChipEvidence::projection(chips.cell);
                    const auto rank=chips.projected_rank+last_rank;
                    const auto projected=std::clamp(chips.projected_energy+last_energy,0.,fit[bit].energy);
                    const auto fitted_rank=2U*drift_sections;
                    if(!chips.nested || (!detail::CorrelationChipEvidence::zero_basis(chips.cell) &&
                       last_rank<std::min<std::uint64_t>(2,chips.cell.count)) || rank<=fitted_rank || rank>=fit[bit].count)return raw;
                    const auto rr=static_cast<double>(rank-fitted_rank),dd=static_cast<double>(fit[bit].count-rank);
                    constexpr double tail=30;
                    const auto delta=4*threshold()+32;
                    const auto lower=dd-2*std::sqrt(dd*tail);
                    if(lower<=0)return raw;
                    const auto upper=rr+delta+2*std::sqrt((rr+2*delta)*tail)+2*tail;
                    const auto residual=fit[bit].energy-projected;
                    const auto excess=std::max(0.,projected-full);
                    if(residual<=0 || excess*dd>std::max(2.,(upper/rr)/(lower/dd))*residual*rr) {
                        ++work.scheduled_colored_guard_activations;
                        return std::min(raw,detail::drift_evidence(retained,projected,static_cast<double>(rank),drift_sections,true));
                    }
                    return raw;
                };
                a=detail::combine_drift_evidence(a,section_score(0),drift_sections);
                b=detail::combine_drift_evidence(b,section_score(1),drift_sections);
            }
            if(differential_window) {
                const auto& local=local_fits(hypothesis,group);
                a=detail::combine_differential_evidence(a,local[0].score(code.symbol_samples(),differential_window),true);
                b=detail::combine_differential_evidence(b,local[1].score(code.symbol_samples(),differential_window),true);
            }
            if(std::max(a,b)>e.score) {
                const auto frequency=options.amplitude_only?amplitude_channels[selected_channel[b>a?1:0]].center_hz:
                    config.carrier_hz+search.frequency_offsets_hz[h.frequency];
                e={h.observed_start,end,h.index,frequency,
                    std::max(a,b),std::min(a,b),b>a?1U:0U,groups[group].lower};
                selected=group;
            }
        }
        if(lazy_state()) {
            require(lazy_state()->history_generation<std::numeric_limits<std::uint64_t>::max(),"implicit diagnostic ordinal overflow");
            e.completed_event=lazy_state()->history_generation+1;
        }
        if(outer_presence) {
            code.set_stream_phase_samples(groups[selected].lower);
            require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
                "pattern chip coordinate overflow");
            code.prepare_symbol(h.index*code.chips_per_symbol(),stop);
            auto& outer=(*outer_evidence)[hypothesis*(alternate_groups+1)+selected];
            outer.finish([&](std::uint64_t local){return code.values(h.index*code.chips_per_symbol()+local);});
            e.outer_presence_score=outer.score(e.bit);
            e.outer_presence_active=outer.can_admit(e.bit,threshold()) && chip_evidence[hypothesis*(alternate_groups+1)+selected][e.bit].colored_excess(
                fits(h,hypothesis,selected)[e.bit],threshold());
        }
        const auto presence=!e.outer_presence_active || e.outer_presence_score>=threshold();
        const auto standalone=e.score>=threshold() && presence;
        if(certificate_probe){certificate_probe->guarded=e.score;certificate_probe->original_scored=true;}
        if(options.amplitude_only && scheduled_map() && h.admitted && !standalone &&
           scheduled->observed_begin[hypothesis]<=scheduled_source_start(h,h.index)) {
            const auto& coverage=amplitude_templates[hypothesis*(alternate_groups+1)+selected];
            const bool whole=coverage.whole.observations>=64;
            bool quarters=true;
            for(const auto& quarter:coverage.quarters)quarters=quarters && quarter.observations!=0;
            // Clock-cell uncertainty grows from the original timing reference,
            // never from a restarted per-bit budget. The finite grid cannot
            // promise indefinite flat support. An unavailable observation is
            // not a failed valid-pattern test and must never end a stream.
            // Strong evidence can still admit through the unchanged statistic;
            // a truncated first acquisition symbol is not an absence claim.
            if(!whole || !quarters)throw Error("scheduled amplitude timing coverage exhausted; cannot classify observed absence (retained FFT observations="+
                std::to_string(coverage.whole.observations)+", required=64; every quarter must be observed)");
        }
        if(scheduled_map() && !options.amplitude_only && h.admitted && !standalone &&
           fits(h,hypothesis,selected)[e.bit].count<=2)
            throw Error("scheduled complex symbol has no scored usable samples; cannot classify observed absence");
        remember(e);
        if(lazy_state()) {
            lazy_state()->diagnostics->observe_resolved(e.completed_event);
            lazy_state()->history_generation=e.completed_event;
        }
        const auto symbol_support=pattern_symbol_support(
            static_cast<double>(fits(h,hypothesis,selected)[e.bit].count),e.score,code.chip_samples());
        // Reclaim known duplicate alias bits before the original quota check.
        // Retained unpublished suffixes remain charged to their existing vector.
        trim_known_alias_prefix(h);
        // Unknown slots never select a private phase. Keep the existing clock
        // until an independently confident later symbol resolves the schedule.
        const auto pending_count=h.burst.bits.size()-h.committed;
        const auto next_count=static_cast<double>(pending_count)+1;
        const auto next_score=h.pending_score+e.score;
        const auto next_chain=next_score>next_count?
            next_score-next_count-next_count*std::log(next_score/next_count)-next_count*std::log(2.):0;
        // Preserve a tail whose joint evidence justifies continuation. If it
        // does not, a confident current symbol must still start independently.
        // An unadmitted prefix cannot borrow that later symbol's confidence.
        const auto reliable=presence && e.score>=search.retain_score && e.score-e.alternative_score>=1 &&
            (group_count==1 || standalone);
        const auto can_preserve=h.admitted;
        auto preserve_gap=can_preserve &&
            (h.pending_gaps || !reliable || (standalone && pending_count && next_chain<threshold()));
        if(preserve_gap) {
            // Retain only the admitted clock and symbol positions. Missing
            // slots provide neither bit evidence nor confidence to the track.
            mark_pending_gaps(h);
            h.burst.end_sample=end;
            if(reliable && standalone) {
                h.phase_lower=groups[selected].lower;h.phase_upper=groups[selected].upper;
                h.burst.stream_phase_samples=h.phase_lower;
                emit_gap(h,h.observed_start);append(h,static_cast<std::uint8_t>(e.bit));
                h.sum_score+=e.score;h.committed=h.burst.bits.size();h.committed_end=end;
                h.sum_support+=symbol_support;h.burst.support_samples=h.sum_support;
                h.burst.score=h.sum_score;h.pending_gaps=false;
            } else {
                require(h.gap_slots<std::numeric_limits<std::size_t>::max(),"pattern missing-slot count overflow");++h.gap_slots;
            }
        } else {
            if(standalone && pending_count && (!h.admitted || next_chain<threshold())) {publish(h);clear(h);}
            if(h.burst.bits.size()==bit_limit && e.score>=search.retain_score)
                throw Error("pattern bit retention limit reached");
            // Weak evidence from different possible schedules cannot be added as
            // though it supported one coherent stream. Resolve a split on the
            // current symbol's own evidence before extending its bit chain.
            if(reliable && h.burst.bits.size()<bit_limit) {
                if(standalone) {
                    h.phase_lower=groups[selected].lower;h.phase_upper=groups[selected].upper;
                    h.burst.stream_phase_samples=h.phase_lower;
                }
                if(h.burst.bits.empty()) {
                    if(!h.admitted) {h.burst.stream_first_sample=h.observed_start;h.burst.stream_first_symbol=h.index;}
                    h.burst.first_sample=h.observed_start;h.burst.first_stream_symbol=h.index;
                    h.burst.stream_phase_samples=h.phase_lower;
                    h.burst.frequency_hz=e.frequency_hz;
                    if(!h.admitted){h.sum_score=h.pending_score=h.burst.score=0;h.committed=0;h.gap_slots=0;
                        if(!pending_chip_scores.empty())pending_chip_scores[hypothesis]=0;
                        h.sum_support=h.burst.support_samples=0;}
                }
                append(h,static_cast<std::uint8_t>(e.bit));h.burst.end_sample=end;h.sum_score+=e.score;h.pending_score+=e.score;
                if(!pending_chip_scores.empty())pending_chip_scores[hypothesis]+=
                    chip_evidence[hypothesis*(alternate_groups+1)+selected][e.bit].score(fits(h,hypothesis,selected)[e.bit]);
                h.sum_support+=symbol_support;
                const auto pending=h.burst.bits.size()-h.committed;
                const auto n=static_cast<double>(pending);
                const auto pending_score=h.admitted || pending_chip_scores.empty()?h.pending_score:pending_chip_scores[hypothesis];
                const auto chain=pending_score>n?pending_score-n-n*std::log(pending_score/n)-n*std::log(2.):0;
                if((n==1 && standalone) || chain>=threshold()) {
                    h.admitted=true;h.committed=h.burst.bits.size();h.committed_end=end;
                    h.burst.score=h.sum_score;h.pending_score=0;
                    if(!pending_chip_scores.empty())pending_chip_scores[hypothesis]=0;
                    h.burst.support_samples=h.sum_support;
                }
                if(!h.admitted && static_cast<long double>(pending)*code.symbol_samples()/h.rate>=
                    static_cast<long double>(pattern_absence_seconds)*config.sample_rate)clear(h);
            } else {publish(h);clear(h);}
        }
        timing_decision(h,hypothesis,e,selected);
        if(scheduled_map() && !options.amplitude_only)settle_timing(true);
        if(scheduled_map() && h.admitted && h.committed_end==end)scheduled->last_accepted[hypothesis]=h.index+1;
        // Only integer, fully scored failed source symbols count. A conservative
        // latest-cell END can include a few samples from the next active group;
        // those must not round an incomplete six-second failure run upward.
        const auto failed_duration=scheduled_map()?
            static_cast<long double>(h.index+1-scheduled->last_accepted[hypothesis])*code.symbol_samples()/h.rate:
            static_cast<long double>(end-h.committed_end);
        if(h.admitted && failed_duration>=static_cast<long double>(pattern_absence_seconds)*config.sample_rate) {
            if(!timing_enabled){publish(h,true);clear(h);}
            else {auto& state=*timing_state(hypothesis);state.pending_terminal=true;state.terminal_index=h.index;publish(h,false,false);}
        } else publish(h,false,false);
        if(!affine_coefficients.empty())for(std::size_t group=0;group<=alternate_groups;++group)
            affine_coefficients[hypothesis*(alternate_groups+1)+group].invalidate();
        h.affine_groups=0;
        h.fits={};
        if(options.amplitude_only)for(std::size_t group=0;group<=alternate_groups;++group) {
            amplitude_templates[hypothesis*(alternate_groups+1)+group]={};
            for(std::size_t channel=0;channel<amplitude_channels.size();++channel)
                amplitude_fits[(hypothesis*(alternate_groups+1)+group)*amplitude_channels.size()+channel]={};
        }
        if(outer_presence)for(std::size_t group=0;group<=alternate_groups;++group)
            (*outer_evidence)[hypothesis*(alternate_groups+1)+group]={};
        if(!chip_evidence.empty())for(std::size_t group=0;group<=alternate_groups;++group)
            chip_evidence[hypothesis*(alternate_groups+1)+group]={};
        for(std::size_t group=0;group<alternate_groups;++group)
            alternate_fits[hypothesis*alternate_groups+group]={};
        if(drift_sections>1)for(std::size_t group=0;group<=alternate_groups;++group)
            section_fits(hypothesis,group)={};
        if(differential_window)for(std::size_t group=0;group<=alternate_groups;++group)
            local_fits(hypothesis,group)={};
        require(h.index<std::numeric_limits<std::uint64_t>::max(),"pattern stream symbol counter overflow");++h.index;
        if(scheduled_calendar())scheduled->source_starts[hypothesis]=scheduled_calendar()->offset_source(
            config.stream_epoch,h.phase_lower,h.index);
        if(scheduled_map() && !options.amplitude_only) {
            scheduled->end_indices[hypothesis]=h.index;
            scheduled->symbol_ends[hypothesis]=scheduled_boundary(h,scheduled_source_start(h,h.index)+code.symbol_samples(),true);
            scheduled->earliest_end=*std::min_element(scheduled->symbol_ends.begin(),scheduled->symbol_ends.end());
        }
        if(scheduled_map() && !options.amplitude_only) {
            auto& carry=scheduled->scheduled_carry[hypothesis];
            work.scheduled_carry_samples+=carry.fits[0].count;
            h.fits=carry.fits;
            if(h.fits[0].count)h.observed_start=carry.observed_start;
            if(drift_sections>1 && !drift_fits.empty()) {
                section_fits(hypothesis,0)=scheduled->scheduled_carry_drift[hypothesis];scheduled->scheduled_carry_drift[hypothesis]={};
                scheduled->scheduled_quarter_ranks[hypothesis]=scheduled->scheduled_carry_quarter_ranks[hypothesis];scheduled->scheduled_carry_quarter_ranks[hypothesis]={};
            }
            if(differential_window) {
                local_fits(hypothesis,0)=scheduled->scheduled_carry_differential[hypothesis];scheduled->scheduled_carry_differential[hypothesis]={};
            }
            if(!chip_evidence.empty()) {
                chip_evidence[hypothesis]=scheduled->scheduled_carry_chips[hypothesis];scheduled->scheduled_carry_chips[hypothesis]={};
            }
            if(outer_presence) {
                (*outer_evidence)[hypothesis]=scheduled->scheduled_carry_outer[hypothesis];scheduled->scheduled_carry_outer[hypothesis]={};
            }
            carry={};
        }
    }
    std::uint64_t initial_cursor(const Hypothesis& h,std::uint64_t end) const {
        if(h.origin>static_cast<long double>(sample))
            return static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(h.origin)));
        return sample;
    }
    bool advance_certified_null(std::uint64_t count) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::calendar);
        if(!scheduled_map() || options.amplitude_only || !count)return false;
        const auto end=sample+count;
        // Equality is a genuine completion/publication event, even if the
        // boundary lies in silence. Null time itself supplies no absence.
        if(static_cast<long double>(end)>=scheduled->earliest_end)return false;
        if(scheduled->null_end<=sample) {
            auto until=std::numeric_limits<std::uint64_t>::max();
            for(const auto& h:hypotheses) {
                const auto center=scheduled_tick(h,sample);
                const auto lower=center.add_signed(-scheduled_uncertainty(h,sample));
                const auto upper=center.add_samples(scheduled_cell_width(h)).add_signed(scheduled_uncertainty(h,sample));
                const auto a=scheduled_map()->locate(lower),b=scheduled_map()->locate(upper);
                if(a.active || b.active || a.source.distance(b.source)!=0)return false;
                const auto next=scheduled_map()->source_to_physical(a.source,false);
                const auto duration=std::max(0.L,std::floor(next.distance(upper)/(h.rate+scheduled->clock_radius)));
                if(duration<static_cast<long double>(std::numeric_limits<std::uint64_t>::max()-sample))
                    until=std::min(until,sample+static_cast<std::uint64_t>(duration));
            }
            scheduled->null_end=until;
        }
        if(end>scheduled->null_end)return false;
        sample=end;work.samples+=count;work.scheduled_null_samples+=count;
        ++work.scheduled_null_pushes;settle_timing();room_for(0);return true;
    }
    // The same deterministic node/tap order serves ordinary and mapped sparse
    // observations. The caller supplies source-time coordinates; FFT sample
    // coordinates remain physical and are never concatenated across gaps.
    std::array<double,2> amplitude_weights(std::uint64_t start_chip,std::uint64_t phase,
            std::span<const double> coordinates,std::stop_token stop) {
        const auto quadrature=static_cast<unsigned>(coordinates.size());
        require(quadrature>=8 && quadrature<=17,"amplitude quadrature exceeds its bounded chip support");
        std::array<double,2> weight{};
        const auto amplitude=std::sqrt(2*nominal_signal_power)*(scheduled_map()?outer_dsss_transmit_gain(config):1.);
        code.set_stream_phase_samples(phase);code.prepare_symbol(start_chip,stop);
        if(config.search_arithmetic!=SearchArithmetic::fp64) {
            ++work.amplitude_float_envelopes;
            // Independently generated original coefficients are
            // packed once per neighborhood, not once per tap.
            // Four real lanes preserve both I/Q and both bits;
            // each lane retains original tap order, without FMA.
            // Pulse coordinates/table and final limiter/norm
            // remain FP64. FP64 force uses the original branch.
            std::array<std::array<float,4>,32> packed{};
            std::array<std::uint64_t,32> tags;tags.fill(std::numeric_limits<std::uint64_t>::max());
            for(unsigned node=0;node<quadrature;++node) {
                const auto coordinate=coordinates[node];
                std::array<float,4> values{};
                pattern_pulse_each(coordinate,code.symbol_samples(),code.chip_samples(),
                    [&](std::uint64_t local,double coefficient) {
                        const auto slot=local%tags.size();
                        if(tags[slot]!=local) {
                            require(local<=std::numeric_limits<std::uint64_t>::max()-start_chip,"amplitude chip address overflow");
                            const auto original=code.values(start_chip+local,0);
                            packed[slot]={static_cast<float>(original[0].real()),static_cast<float>(original[0].imag()),
                                static_cast<float>(original[1].real()),static_cast<float>(original[1].imag())};
                            tags[slot]=local;
                        }
                        const float pulse=static_cast<float>(coefficient);
        #if defined(__SSE2__)
                        const auto product=_mm_mul_ps(_mm_set1_ps(pulse),_mm_loadu_ps(packed[slot].data()));
                        _mm_storeu_ps(values.data(),_mm_add_ps(_mm_loadu_ps(values.data()),product));
        #else
                        for(unsigned lane=0;lane<4;++lane)values[lane]+=pulse*packed[slot][lane];
        #endif
                    });
                for(unsigned bit=0;bit<2;++bit) {
                    const Complex value{static_cast<double>(values[2*bit]),static_cast<double>(values[2*bit+1])};
                    weight[bit]+=std::norm(pattern_limit_pcm(amplitude*value))/quadrature;
                }
            }
        } else {
        std::array<std::array<Complex,2>,32> coefficients{};
        std::array<std::uint64_t,32> tags;tags.fill(std::numeric_limits<std::uint64_t>::max());
        for(unsigned node=0;node<quadrature;++node) {
            const auto coordinate=coordinates[node];
            std::array<Complex,2> values{};
            pattern_pulse_each(coordinate,code.symbol_samples(),code.chip_samples(),
                [&](std::uint64_t local,double coefficient) {
                const auto slot=local%tags.size();
                if(tags[slot]!=local) {
                    require(local<=std::numeric_limits<std::uint64_t>::max()-start_chip,"amplitude chip address overflow");
                    coefficients[slot]=code.values(start_chip+local,0);
                    tags[slot]=local;
                }
                for(unsigned bit=0;bit<2;++bit)values[bit]+=coefficient*coefficients[slot][bit];
            });
            for(unsigned bit=0;bit<2;++bit)weight[bit]+=std::norm(pattern_limit_pcm(
                amplitude*values[bit]))/quadrature;
        }
        }
        return weight;
    }
    bool scheduled_block_needed(std::uint64_t first,std::uint64_t end) const {
        return std::any_of(hypotheses.begin(),hypotheses.end(),[&](const auto& h){return scheduled_flat(h,first,end);});
    }
    void amplitude_scheduled_progress(std::uint64_t first,std::uint64_t end,
            const std::array<double,32>* energy,std::stop_token stop) {
        const auto total=code.symbol_samples();
        for(std::size_t hypothesis=0;hypothesis<hypotheses.size();++hypothesis) {
            cancelled(stop);auto& h=hypotheses[hypothesis];
            const auto source_first=source_position(h,first),source_end=source_position(h,end);
            const auto flat=energy && scheduled_flat(h,first,end);
            while(source_end>scheduled_source_start(h,h.index)) {
                const auto begin=scheduled_source_start(h,h.index),next=begin+total;
                const auto observed_begin=std::max(source_first,begin),observed_end=std::min(source_end,next);
                if(!h.fits[0].count && observed_end>observed_begin) {
                    h.observed_start=static_cast<std::uint64_t>(std::max(static_cast<long double>(first),
                        std::max(0.L,std::ceil(scheduled_boundary(h,observed_begin,false)))));
                }
                // scheduled_flat includes the WHOLE represented physical phase
                // cell. Both that cell and its active source anchor are affine:
                // U(q+delta)-U(P+delta) == U(q)-U(P). Thus source symbol and
                // quarter ownership are identical throughout the cell. Do not
                // discard that width again here. Keep physical flat guards and
                // latest-END waiting: the inverse can jump across a null.
                const auto quarter_first=std::floor(4*(source_first-begin)/total);
                const auto quarter_last=std::ceil(4*(source_end-begin)/total)-1;
                if(flat && source_first>=begin && source_end<=next && quarter_first==quarter_last) {
                    require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
                        "scheduled amplitude pattern chip coordinate overflow");
                    const auto quadrature=std::max(8U,static_cast<unsigned>(std::ceil(
                        8*static_cast<long double>(amplitude_geometry->fft_samples)*h.rate/code.chip_samples())));
                    require(quadrature<=17,"scheduled amplitude quadrature exceeds its bounded support");
                    std::array<double,17> coordinates{};
                    for(unsigned node=0;node<quadrature;++node) {
                        const auto physical=static_cast<long double>(first)+
                            (node+.5L)*amplitude_geometry->fft_samples/quadrature;
                        // Proven active-flat interiors have exactly affine U.
                        // No private calendar construction is repeated per node.
                        coordinates[node]=static_cast<double>(source_first+(physical-first)*h.rate-begin);
                    }
                    const bool measured=options.measure_affine_work && (work.amplitude_observations%64==0);
                    WorkTimer timer(measured,work.affine_prepare_seconds,work.affine_prepare_cpu_seconds,64);
                    const auto weight=amplitude_weights(h.index*code.chips_per_symbol(),h.phase_lower,
                        std::span<const double>(coordinates).first(quadrature),stop);
                    ++work.affine_preparations;timer.finish();
                    const auto quarter=static_cast<unsigned>(std::clamp(quarter_first,0.L,3.L));
                    const auto independent=2*static_cast<double>(amplitude_geometry->fft_samples*h.rate/code.chip_samples());
                    amplitude_templates[hypothesis].add(weight,quarter,independent);
                    for(std::size_t channel=0;channel<amplitude_channels.size();++channel)
                        amplitude_fits[hypothesis*amplitude_channels.size()+channel].add((*energy)[channel],weight,quarter);
                    work.amplitude_channel_updates+=amplitude_channels.size();++work.amplitude_observations;
                }
                // This is physical eligible duration, not elapsed null time.
                // Incomplete FFT windows are omitted by the same fixed rule as
                // ordinary quarter/pulse boundaries; no synthetic energy enters.
                const auto retained_begin=std::max(begin,scheduled->observed_begin[hypothesis]);
                const auto count=std::max(0.L,std::floor((observed_end-retained_begin)/h.rate+1e-9L));
                require(count<=std::numeric_limits<std::uint64_t>::max(),"scheduled observation count overflow");
                for(auto& fit:h.fits)fit.count=static_cast<std::uint64_t>(count);
                if(source_end<next)break;
                const auto boundary=static_cast<std::uint64_t>(std::max(0.L,std::ceil(scheduled_boundary(h,next,true))));
                if(end<boundary)break;
                complete(h,hypothesis,boundary,stop);
            }
        }
    }
    void amplitude_block(std::span<const float> input,std::stop_token stop,detail::CorrelationProjectionCache* cache,
                         AmplitudeScratch& scratch,std::vector<long double>& local_spectrum) {
        const auto first=amplitude_processed;
        const auto end=first+amplitude_geometry->fft_samples;
        if(scheduled_map() && !scheduled_block_needed(first,end)) {
            ++work.scheduled_omitted_fft_blocks;
            amplitude_scheduled_progress(first,end,nullptr,stop);
            amplitude_processed=end;settle_timing();room_for(0);return;
        }
        std::array<double,32> energy{};
        {
            WorkTimer frontend_timer(options.measure_work,work.frontend_seconds,work.frontend_cpu_seconds);
            const bool requested_float=config.search_arithmetic!=SearchArithmetic::fp64;
            const auto compute=[&](std::span<long double> out) {
                // The same immutable PCM uniquely determines this fallback.
                // Do not rescan it in every keyed bank on a shared cache hit;
                // forced FP64 needs no range scan. This policy is part of the
                // spectrum identity, independently of template arithmetic.
                double peak=0;
                if(requested_float)for(auto x:amplitude_pcm)peak=std::max(peak,std::abs(static_cast<double>(x)));
                const bool use_float=requested_float && (peak==0 || (peak>=1e-12 && peak<=1e12));
                ++work.amplitude_transforms;
                if(use_float)++work.amplitude_float_transforms;
                else if(requested_float)++work.amplitude_precision_fallbacks;
                // The push-scoped cohort shares immutable spectra below.
                // Use this bank's persistent plan on a miss; recreating a
                // shared plan at every push costs more than these short FFTs.
                // Local API callers and exhausted shared scratch retain an
                // exact bounded fallback. The reservation is charged to this
                // bank. Plans and native buffers persist across pushes;
                // allocated bytes replace, rather than add to, its reservation.
                out[0]=0;long double total=0;
                if(use_float) {
                    if(!scratch.float_plan) {
                        scratch.plan.reset();std::vector<Complex>{}.swap(scratch.pcm);
                        scratch.float_plan=std::make_unique<detail::search_fft::FloatPlan>(amplitude_geometry->fft_samples,stop);
                        scratch.float_pcm.resize(amplitude_geometry->fft_samples);
                    }
                    for(std::size_t j=0;j<amplitude_pcm.size();++j)scratch.float_pcm[j]={amplitude_pcm[j],0};
                    detail::search_fft::fft(scratch.float_pcm,false,*scratch.float_plan,stop);
                    for(std::size_t j=1;j<out.size();++j) {
                        const Complex z=scratch.float_pcm[amplitude_geometry->first_bin+j-1];
                        total+=std::norm(z);out[j]=total;
                    }
                } else {
                    if(!scratch.plan) {
                        scratch.float_plan.reset();std::vector<std::complex<float>>{}.swap(scratch.float_pcm);
                        scratch.plan=std::make_unique<detail::search_fft::DoublePlan>(amplitude_geometry->fft_samples,stop);
                        scratch.pcm.resize(amplitude_geometry->fft_samples);
                    }
                    for(std::size_t j=0;j<amplitude_pcm.size();++j)scratch.pcm[j]={amplitude_pcm[j],0};
                    detail::search_fft::fft(scratch.pcm,false,*scratch.plan,stop);
                    for(std::size_t j=1;j<out.size();++j){total+=std::norm(scratch.pcm[amplitude_geometry->first_bin+j-1]);out[j]=total;}
                }
            };
            require(first<=std::numeric_limits<std::uint64_t>::max()-search.input_sample_origin,
                "amplitude capture coordinate overflow");
            const auto prior=cache?cache->spectrum_hits():0;
            const auto view=cache?cache->spectrum(input,search.input_sample_origin+first,amplitude_geometry->fft_samples,
                amplitude_geometry->first_bin,amplitude_geometry->bin_count,config.sample_rate,compute,stop,requested_float):detail::CorrelationProjectionCache::SpectrumView{};
            std::span<const long double> prefix;
            if(view)prefix=view.prefix;
            else {local_spectrum.resize(amplitude_geometry->bin_count+1);compute(local_spectrum);prefix=local_spectrum;}
            if(cache)work.amplitude_cache_hits+=cache->spectrum_hits()-prior;
            for(std::size_t j=0;j<amplitude_channels.size();++j) {
                const auto& channel=amplitude_channels[j];const auto at=channel.first_bin-amplitude_geometry->first_bin;
                energy[j]=static_cast<double>(std::max(0.L,prefix[at+channel.bin_count]-prefix[at]));
                require(std::isfinite(energy[j]),"nonfinite amplitude energy projection");
            }
        }
        const auto allocated=amplitude_scratch.working_bytes();
        require(allocated<=amplitude_scratch_limit,"amplitude FFT scratch exceeded its reservation");
        amplitude_scratch_reservation=amplitude_scratch_limit-allocated;
        accounted_bytes=working_bytes()+unallocated_history_bytes()+amplitude_scratch_reservation;
        WorkTimer search_timer(options.measure_work,work.search_seconds,work.search_cpu_seconds);
        if(scheduled_map()) {
            amplitude_scheduled_progress(first,end,&energy,stop);
            amplitude_processed=end;settle_timing();room_for(0);return;
        }
        for(std::size_t hypothesis=0;hypothesis<hypotheses.size();++hypothesis) {
            cancelled(stop);auto& h=hypotheses[hypothesis];
            auto cursor=first;
            if(h.origin>static_cast<long double>(cursor))cursor=static_cast<std::uint64_t>(
                std::min(static_cast<long double>(end),std::ceil(h.origin)));
            while(cursor<end) {
                const auto symbol_first=symbol_start(h);
                const auto boundary=static_cast<std::uint64_t>(std::ceil(clock_boundary(h,code.symbol_samples())));
                if(cursor>=boundary){complete(h,hypothesis,boundary,stop);continue;}
                const auto until=std::min(end,boundary);
                if(!h.fits[0].count)h.observed_start=cursor;
                auto& phase=amplitude_phases[hypothesis];
                if(phase.index!=h.index || phase.lower!=h.phase_lower || phase.upper!=h.phase_upper) {
                    phase.groups=phase_groups(h,phase.count);phase.index=h.index;
                    phase.lower=h.phase_lower;phase.upper=h.phase_upper;
                }
                const auto group_count=phase.count;const auto& groups=phase.groups;
                // A fixed frontend coordinate belongs to at most one symbol
                // in a track. Boundary-straddling blocks are omitted rather
                // than counted twice or filled with invented observations.
                const auto within_first=(static_cast<long double>(first)-symbol_first)*h.rate;
                const auto within_last=(static_cast<long double>(end)-symbol_first)*h.rate;
                const auto first_quarter=std::floor(4*within_first/code.symbol_samples());
                const auto last_quarter=std::ceil(4*within_last/code.symbol_samples())-1;
                if(static_cast<long double>(first)>=symbol_first && end<=boundary && first_quarter==last_quarter) {
                    const auto within=(static_cast<long double>(first)+.5L*amplitude_geometry->fft_samples-symbol_first)*h.rate;
                    const auto quarter=static_cast<unsigned>(std::clamp(std::floor(4*within/code.symbol_samples()),0.L,3.L));
                    for(std::size_t group=0;group<group_count;++group) {
                        // Sample fine scopes: timing every small reduction
                        // costs more than the operation. These component fields
                        // are 64x systematic-sample estimates, not exact totals.
                        const bool measured=options.measure_affine_work && (work.amplitude_observations%64==0);
                        WorkTimer preparation_timer(measured,work.affine_prepare_seconds,work.affine_prepare_cpu_seconds,64);
                        require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
                            "amplitude pattern chip coordinate overflow");
                        const auto start_chip=h.index*code.chips_per_symbol();
                        std::array<double,2> weight{};
                        // Deterministic quadrature of the ORIGINAL finite
                        // shaped and radially limited envelope. Approximation
                        // affects sensitivity, never its null distribution.
                        const auto quadrature=std::max(8U,static_cast<unsigned>(std::ceil(
                            8*static_cast<long double>(amplitude_geometry->fft_samples)*h.rate/code.chip_samples())));
                        require(quadrature<=17,"amplitude quadrature exceeds its bounded chip support");
                        ++work.affine_preparations;
                        code.set_stream_phase_samples(groups[group].lower);code.prepare_symbol(start_chip,stop);
                        // Load each original chip coefficient once for the
                        // compact quadrature neighborhood. Accumulate pulse
                        // taps in exactly the original node/tap order.
                        std::array<double,17> coordinates{};
                        for(unsigned node=0;node<quadrature;++node)coordinates[node]=static_cast<double>(
                            (static_cast<long double>(first)+(node+.5L)*amplitude_geometry->fft_samples/quadrature-symbol_first)*h.rate);
                        weight=amplitude_weights(start_chip,groups[group].lower,
                            std::span<const double>(coordinates).first(quadrature),stop);
                        const auto independent=2*static_cast<double>(amplitude_geometry->fft_samples*h.rate/code.chip_samples());
                        preparation_timer.finish();
                        WorkTimer reduction_timer(measured,work.affine_fit_seconds,work.affine_fit_cpu_seconds,64);
                        amplitude_templates[hypothesis*(alternate_groups+1)+group].add(weight,quarter,independent);
                        for(std::size_t channel=0;channel<amplitude_channels.size();++channel) {
                            auto& envelope=amplitude_fits[(hypothesis*(alternate_groups+1)+group)*amplitude_channels.size()+channel];
                            envelope.add(energy[channel],weight,quarter);
                        }
                        work.amplitude_channel_updates+=amplitude_channels.size();
                        ++work.amplitude_observations;
                    }
                }
                for(std::size_t group=0;group<group_count;++group)for(auto& fit:fits(h,hypothesis,group))
                    fit.count+=until-cursor;
                cursor=until;
                if(cursor>=boundary)complete(h,hypothesis,boundary,stop);
            }
        }
        amplitude_processed=end;settle_timing();room_for(0);
    }
    void push_amplitude(std::span<const float> input,std::stop_token stop,detail::CorrelationProjectionCache* cache) {
        work.samples+=input.size();

        for(std::size_t offset=0;offset<input.size();) {
            cancelled(stop);
            // All banks use one absolute block lattice, including banks
            // admitted midstream. An initial partial coordinate is observed
            // only for timing coverage and is never padded into an FFT.
            if(sample<amplitude_processed) {
                const auto skip=std::min<std::uint64_t>(input.size()-offset,amplitude_processed-sample);
                sample+=skip;offset+=static_cast<std::size_t>(skip);continue;
            }
            const auto count=std::min(amplitude_geometry->fft_samples-amplitude_partial,input.size()-offset);
            std::copy_n(input.begin()+offset,count,amplitude_pcm.begin()+amplitude_partial);
            amplitude_partial+=count;offset+=count;sample+=count;
            if(amplitude_partial==amplitude_geometry->fft_samples) {
                amplitude_block(input,stop,cache,amplitude_scratch,amplitude_scratch.spectrum);amplitude_partial=0;
            }
        }
    }
    void fit_affine_piece(const Bank& bank,std::size_t left,std::uint64_t piece_count,
                          const std::array<Complex,2>& value,const std::array<Complex,2>& slope,
                          std::array<Fit,2>& fit,std::array<DriftFit,2>* drift,
                          std::array<DifferentialFit,2>* differential,
                          std::array<detail::CorrelationChipEvidence,2>* chip=nullptr,std::uint64_t chip_index=0) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::gram,true);
        WorkTimer fit_timer(options.measure_affine_work,work.affine_fit_seconds,work.affine_fit_cpu_seconds);
        const auto right=left+static_cast<std::size_t>(piece_count);
        const auto prefix=bank.observations(physical_view(bank));const auto moments=bank.moments(physical_view(bank));
        const auto projection=prefix[right]-prefix[left];
        const Complex measured{projection.xc,projection.xs};
        const auto moment=moments[right]-moments[left]-static_cast<double>(left)*measured;
        const auto first=prefix[left+1]-prefix[left];
        const Complex square{first.cc-first.ss,2*first.cs};
        require(piece_count<=block_samples,"affine pulse span exceeds its oscillator block");
        detail::CorrelationCarrierMoments uncached;
        const auto* carrier=bank.carrier(piece_count);
        if(!carrier) {
            uncached=detail::correlation_carrier_moments(piece_count,2*tau*bank.frequency/config.sample_rate);
            carrier=&uncached;
        }
        for(unsigned bit=0;bit<2;++bit) {
            const auto contribution=detail::correlation_affine_fit(projection,moment,
                piece_count,value[bit],slope[bit],square,first.cc+first.ss,
                *carrier);
            add_fit(fit[bit],contribution);
            if(chip)(*chip)[bit].add_fit(contribution,chip_index);
            if(drift)add_fit((*drift)[bit].active,contribution);
            if(differential)add_fit((*differential)[bit].active,contribution);
        }
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool try_float_gram_piece(std::size_t hypothesis,std::size_t frequency,std::size_t left,
            const detail::ScheduledSpanMoments& moments,const std::array<Complex,2>& value,
            const std::array<Complex,2>& slope,std::array<Fit,2>& fit,std::array<DriftFit,2>* drift,
            std::array<DifferentialFit,2>* differential,std::array<detail::CorrelationChipEvidence,2>* chip,
            std::uint64_t chip_index,detail::ScheduledSpanMoments* accepted,std::array<Fit,2>* piece_result) {
        auto& counts=scheduled->float_grams[hypothesis];++counts.attempts;
        const auto native=detail::scheduled_float_gram::evaluate(moments,value,slope);
        if(std::isfinite(native.maximum_relative_error))counts.maximum_relative_error=std::max(
            counts.maximum_relative_error,native.maximum_relative_error);
        if(!native) {
            using Failure=detail::scheduled_float_gram::Failure;
            if(native.failure==Failure::rank)++counts.rank;
            else if(native.failure==Failure::error)++counts.error;
            else ++counts.range;
            return false;
        }
        detail::scheduled_float_signal::Result signal;
        std::array<Complex,2> dots;
        const std::array<Complex,2>* signal_override=nullptr;
        if(!scheduled->float_signal.empty()) {
            ++counts.signal_attempts;
            signal=scheduled->float_signal[frequency].contract(left,moments.count,value,slope);
            bool certified=static_cast<bool>(signal);
            if(certified)for(unsigned bit=0;bit<2;++bit)certified&=detail::scheduled_float_gram::certify_signal(
                signal.dots[bit],native.lambda_lower[bit],moments.energy);
            if(certified) {
                for(unsigned bit=0;bit<2;++bit)dots[bit]=signal.dots[bit].value;
                signal_override=&dots;
            }
        }
        std::array<Fit,2> contributions;
        // This helper skips BOTH original Gram products and a supplied signal
        // dot. Its original finite/rank guards still run on the FP64 fields.
        if(!detail::scheduled_affine_pair(moments,value,slope,contributions,signal_override,&native.gram)) {
            ++counts.helper;return false;
        }
        ++counts.accepted;
        if(!scheduled->float_signal.empty()) {
            auto& c=scheduled->float_counts[hypothesis];++c.attempts;
            if(signal_override){++c.accepted_pairs;c.pieces+=signal.tiles;}
            else {++c.fp64_pairs;++c.error_fallbacks;}
        }
        for(unsigned bit=0;bit<2;++bit) {
            const auto& f=contributions[bit];add_fit(fit[bit],f);
            if(chip)(*chip)[bit].add_fit(f,chip_index);
            if(drift)add_fit((*drift)[bit].active,f);
            if(differential)add_fit((*differential)[bit].active,f);
        }
        if(accepted)*accepted=moments;
        if(piece_result)*piece_result=contributions;
        return true;
    }
    // Keep the original FP64 formulas in a separate hot function. Float
    // diagnostics retain their complete existing body and numerical fallbacks.
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool fit_mapped_piece_fp64(std::size_t /*hypothesis*/,std::size_t frequency,std::size_t left,std::size_t count,
            const std::array<Complex,2>& value,const std::array<Complex,2>& slope,
            std::array<Fit,2>& fit,std::array<DriftFit,2>* drift,
            std::array<DifferentialFit,2>* differential,
            std::array<detail::CorrelationChipEvidence,2>* chip,std::uint64_t chip_index,
            detail::ScheduledSpanMoments* accepted,std::array<Fit,2>* piece_result) {
        const auto shared=physical_view(frequency);
        const auto a=shared?shared.moment(left):scheduled->moments[frequency][left];
        const auto b=shared?shared.moment(left+count):scheduled->moments[frequency][left+count];
        const auto p=banks[frequency].observations(physical_view(frequency))[left+count]-banks[frequency].observations(physical_view(frequency))[left];
        const double l=static_cast<double>(left);
        const Complex z{p.xc,p.xs},c0{p.cc-p.ss,2*p.cs};
        const auto z1=b.signal1-a.signal1-l*z;
        const auto c1=b.square1-a.square1-l*c0;
        const auto c2=b.square2-a.square2-2*l*(b.square1-a.square1)+l*l*c0;
        const auto n0=p.cc+p.ss,n1=b.norm1-a.norm1-l*n0;
        const auto n2=b.norm2-a.norm2-2*l*(b.norm1-a.norm1)+l*l*n0;
        std::array<Fit,2> contributions;
        for(unsigned bit=0;bit<2;++bit) {
            const auto v=value[bit],s=slope[bit];
            const auto dot=v*z+s*z1;
            const auto trace=std::norm(v)*n0+2*(v*std::conj(s)).real()*n1+std::norm(s)*n2;
            const auto square=v*v*c0+2.*v*s*c1+s*s*c2;
            auto& f=contributions[bit];
            f={dot.real(),dot.imag(),(trace+square.real())/2,(trace-square.real())/2,
                square.imag()/2,p.energy,count};
            // Near-singular/cancellation-sensitive pieces retain the original
            // sample contraction. Never repair an indefinite Gram by clipping.
            if(!std::isfinite(trace+f.xc+f.xs+f.cs) || f.cc<=0 || f.ss<=0 ||
               f.cc*f.ss-f.cs*f.cs<=1e-10*trace*trace)return false;
        }
        for(unsigned bit=0;bit<2;++bit) {
            const auto& f=contributions[bit];add_fit(fit[bit],f);
            if(chip)(*chip)[bit].add_fit(f,chip_index);
            if(drift)add_fit((*drift)[bit].active,f);
            if(differential)add_fit((*differential)[bit].active,f);
        }
        if(accepted)*accepted={{z,z1},{c0,c1,c2},{n0,n1,n2},p.energy,count};
        if(piece_result)*piece_result=contributions;
        return true;
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool fit_mapped_piece_diagnostic(std::size_t hypothesis,std::size_t frequency,std::size_t left,std::size_t count,
            const std::array<Complex,2>& value,const std::array<Complex,2>& slope,
            std::array<Fit,2>& fit,std::array<DriftFit,2>* drift,
            std::array<DifferentialFit,2>* differential,
            std::array<detail::CorrelationChipEvidence,2>* chip,std::uint64_t chip_index,
            detail::ScheduledSpanMoments* accepted,std::array<Fit,2>* piece_result) {
        const auto shared=physical_view(frequency);
        const auto a=shared?shared.moment(left):scheduled->moments[frequency][left];
        const auto b=shared?shared.moment(left+count):scheduled->moments[frequency][left+count];
        const auto p=banks[frequency].observations(physical_view(frequency))[left+count]-banks[frequency].observations(physical_view(frequency))[left];
        const double l=static_cast<double>(left);
        const Complex z{p.xc,p.xs},c0{p.cc-p.ss,2*p.cs};
        const auto z1=b.signal1-a.signal1-l*z;
        const auto c1=b.square1-a.square1-l*c0;
        const auto c2=b.square2-a.square2-2*l*(b.square1-a.square1)+l*l*c0;
        const auto n0=p.cc+p.ss,n1=b.norm1-a.norm1-l*n0;
        const auto n2=b.norm2-a.norm2-2*l*(b.norm1-a.norm1)+l*l*n0;
        if(!scheduled->float_grams.empty() && try_float_gram_piece(hypothesis,frequency,left,
                {{z,z1},{c0,c1,c2},{n0,n1,n2},p.energy,count},value,slope,fit,drift,differential,chip,
                chip_index,accepted,piece_result))return true;
        std::array<Fit,2> contributions;
        if(scheduled->float_signal.empty()) {
            for(unsigned bit=0;bit<2;++bit) {
                const auto v=value[bit],s=slope[bit];
                const auto dot=v*z+s*z1;
                const auto trace=std::norm(v)*n0+2*(v*std::conj(s)).real()*n1+std::norm(s)*n2;
                const auto square=v*v*c0+2.*v*s*c1+s*s*c2;
                auto& f=contributions[bit];
                f={dot.real(),dot.imag(),(trace+square.real())/2,(trace-square.real())/2,
                    square.imag()/2,p.energy,count};
                // Near-singular/cancellation-sensitive pieces retain the original
                // sample contraction. Never repair an indefinite Gram by clipping.
                if(!std::isfinite(trace+f.xc+f.xs+f.cs) || f.cc<=0 || f.ss<=0 ||
                   f.cc*f.ss-f.cs*f.cs<=1e-10*trace*trace)return false;
            }
        } else {
            std::array<double,2> original_trace{};
            for(unsigned bit=0;bit<2;++bit) {
                const auto v=value[bit],s=slope[bit];
                const auto trace=std::norm(v)*n0+2*(v*std::conj(s)).real()*n1+std::norm(s)*n2;
                const auto square=v*v*c0+2.*v*s*c1+s*s*c2;
                auto& f=contributions[bit];
                f={0,0,(trace+square.real())/2,(trace-square.real())/2,
                    square.imag()/2,p.energy,count};
                // Near-singular/cancellation-sensitive pieces retain the original
                // sample contraction. Never repair an indefinite Gram by clipping.
                original_trace[bit]=trace;
                if(f.cc<=0 || f.ss<=0 || f.cc*f.ss-f.cs*f.cs<=1e-10*trace*trace) {
                    if(!scheduled->float_counts.empty())++scheduled->float_counts[hypothesis].gram_fallbacks;
                    return false;
                }
            }
            detail::scheduled_float_signal::Result native;
            bool use_native=false;
            if(!scheduled->float_signal.empty()) {
                auto& counts=scheduled->float_counts[hypothesis];++counts.attempts;
                native=scheduled->float_signal[frequency].contract(left,count,value,slope);
                use_native=static_cast<bool>(native);
                if(use_native)for(unsigned bit=0;bit<2;++bit) {
                    const auto& f=contributions[bit];
                    const auto certificate=detail::scheduled_float_signal::certify(
                        native.dots[bit],f.cc,f.ss,f.cs,p.energy,1e-4);
                    if(certificate!=detail::scheduled_float_signal::Failure::none) {
                        use_native=false;
                        if(certificate==detail::scheduled_float_signal::Failure::ill_conditioned_gram)
                            ++counts.condition_fallbacks;
                        else ++counts.error_fallbacks;
                        break;
                    }
                } else ++counts.error_fallbacks;
                if(use_native){counts.pieces+=native.tiles;++counts.accepted_pairs;}
                else ++counts.fp64_pairs;
            }
            // Local disk bounds exercise numerical fallbacks, not an exact final
            // guard-decision certificate. This explicit diagnostic is NEVER selected
            // automatically; complete paired/threshold/sensitivity checks remain.
            for(unsigned bit=0;bit<2;++bit) {
                const auto dot=use_native?native.dots[bit].value:value[bit]*z+slope[bit]*z1;
                auto& f=contributions[bit];f.xc=dot.real();f.xs=dot.imag();
                if(!std::isfinite(original_trace[bit]+f.xc+f.xs+f.cs))return false;
            }
        }
        for(unsigned bit=0;bit<2;++bit) {
            const auto& f=contributions[bit];add_fit(fit[bit],f);
            if(chip)(*chip)[bit].add_fit(f,chip_index);
            if(drift)add_fit((*drift)[bit].active,f);
            if(differential)add_fit((*differential)[bit].active,f);
        }
        if(accepted)*accepted={{z,z1},{c0,c1,c2},{n0,n1,n2},p.energy,count};
        if(piece_result)*piece_result=contributions;
        return true;
    }
    bool fit_mapped_piece(std::size_t hypothesis,std::size_t frequency,std::size_t left,std::size_t count,
            const std::array<Complex,2>& value,const std::array<Complex,2>& slope,
            std::array<Fit,2>& fit,std::array<DriftFit,2>* drift,
            std::array<DifferentialFit,2>* differential,
            std::array<detail::CorrelationChipEvidence,2>* chip,std::uint64_t chip_index,
            detail::ScheduledSpanMoments* accepted=nullptr,std::array<Fit,2>* piece_result=nullptr) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::gram,true);
        // Prepared state, rather than Options alone, preserves workspace and
        // preparation failures as the original exact FP64 fallback.
        if(scheduled->float_signal.empty() && scheduled->float_grams.empty())
            return fit_mapped_piece_fp64(hypothesis,frequency,left,count,value,slope,fit,drift,differential,chip,
                chip_index,accepted,piece_result);
        return fit_mapped_piece_diagnostic(hypothesis,frequency,left,count,value,slope,fit,drift,differential,chip,
                chip_index,accepted,piece_result);
    }
    // Exact flat-piece descriptor: one keyed calendar lookup. Once left is
    // within this immutable group's flat interval, its public integer affine
    // translation gives the ORIGINAL source coordinate at the last physical
    // sample. Source fractions come from scheduled_tick, never interpolation.
    bool scheduled_phase_flat_coordinates(const Hypothesis& h,std::size_t lane,
            std::uint64_t first,std::uint64_t end,long double& lower,long double& upper,
            detail::ScheduledFlatExportCertificate* export_certificate=nullptr) const {
        if(export_certificate)*export_certificate={};
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::calendar,true);
        if(first>=end)return false;
        const auto left=scheduled_tick(h,first);
        const auto group=scheduled_map()->locate(left);
        if(!group.active || left.distance(SchedHopCoordinate{group.flat_begin,0})<0)return false;
        const auto right=scheduled_tick(h,end);
        if(right.distance(SchedHopCoordinate{group.flat_end,0})>0)return false;
        if(scheduled->fhss_shape.enabled) {
            const auto two=2.L*config.sample_rate;
            const auto local=std::fmod(static_cast<long double>(left.sample%(2ULL*config.sample_rate))+
                left.fraction+scheduled->fhss_shape.lag_center,two);
            const auto period=two/5;
            const auto position=local-std::floor(local*5/two)*period;
            const auto radius=scheduled->fhss_shape.affected_radius();
            if(position<radius || position+right.distance(left)>period-radius)return false;
        }
        const auto last=scheduled_tick(h,end-1);
        if(last.sample<group.physical_begin || last.sample>=group.physical_end)return false;
        // locate_in_frame uses this SAME bounded integer sum and copies the
        // original last.fraction. Validated source_end bounds the addition.
        const SchedHopCoordinate source_last{
            group.source_begin+(last.sample-group.physical_begin),last.fraction};
        const auto start=scheduled_source_start(h,h.index);
        lower=group.source.distance(scheduled->anchors[lane])-start;
        upper=source_last.distance(scheduled->anchors[lane])-start;
        if(export_certificate && detail::scheduled_flat_export_requested())
            *export_certificate=detail::scheduled_flat_export_certificate(this,h.origin,h.rate,h.phase_lower,
                first,end,left,right,group,scheduled->fhss_shape,config.sample_rate,true);
        return true;
    }
    // One worker owns each exact source family. All original fine-frequency
    // prefixes/moments remain; only common source construction is shared.
    struct ScheduledCommonTargets {
        std::array<Fit,2>* whole=nullptr;
        std::array<DriftFit,2>* quarter=nullptr;
        std::array<DifferentialFit,2>* differential=nullptr;
        std::array<detail::CorrelationChipEvidence,2>* chips=nullptr;
        detail::CorrelationOuterEvidence* outer=nullptr;
        std::uint64_t* observed_start=nullptr;
        bool fitted=false;
    };
    static void apply_scheduled_common_pair(const std::array<Fit,2>& pair,
            ScheduledCommonTargets& target,std::uint64_t chip) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::gram,true);
        // Each lane owns every accumulator; preserve its original bit order.
        for(unsigned bit=0;bit<2;++bit) {
            const auto& f=pair[bit];add_fit((*target.whole)[bit],f);
            if(target.chips)(*target.chips)[bit].add_fit(f,chip);
            if(target.quarter)add_fit((*target.quarter)[bit].active,f);
            if(target.differential)add_fit((*target.differential)[bit].active,f);
        }
    }
    static constexpr std::size_t scheduled_common_maximum_lanes=16,scheduled_common_scratch_bytes=4096;
    static_assert(scheduled_common_maximum_lanes*sizeof(ScheduledCommonTargets)+
        sizeof(detail::ScheduledPulseSpan)+sizeof(detail::ScheduledSpanMoments)+
        2*sizeof(Fit)+scheduled_common_maximum_lanes*sizeof(bool)+sizeof(detail::ScheduledMappedPieceCounts)+
        8*sizeof(std::array<Complex,2>)+2*sizeof(detail::CorrelationPulseSegment)+
        sizeof(detail::ScheduledOuterPublicLockstep)+sizeof(detail::ScheduledOuterPublicScope)+512<=scheduled_common_scratch_bytes);
    struct ScheduledCommonRawPulse {std::uint64_t index;double weight;};
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void add_scheduled_common_raw_one_frequency(std::span<const std::size_t> lanes,
            std::array<ScheduledCommonTargets,scheduled_common_maximum_lanes>& target,
            std::size_t left,const std::array<Complex,2>& phase,std::uint64_t local,
            std::uint64_t first_chip,std::span<const ScheduledCommonRawPulse> pulses,
            ScheduledCommonWork& counters,PatternCode& pattern) {
        std::size_t unmatched=0;for(std::size_t j=0;j<lanes.size();++j)unmatched+=!target[j].fitted;
        if(!unmatched)return;
        const auto& prefix=banks[hypotheses[lanes.front()].frequency].observations(physical_view(hypotheses[lanes.front()].frequency));const auto p=prefix[left+1]-prefix[left];
        std::array<Fit,2> contribution;
        for(unsigned bit=0;bit<2;++bit)contribution[bit].add(p,phase[bit],1);
        for(std::size_t j=0;j<lanes.size();++j) {
            auto& t=target[j];if(t.fitted)continue;
            if(t.outer)for(const auto& pulse:pulses)t.outer->add(pulse.index,
                pulse.weight*Complex{p.xc,p.xs},[&](std::uint64_t index){return pattern.values(first_chip+index);});
            for(unsigned bit=0;bit<2;++bit) {
                add_fit((*t.whole)[bit],contribution[bit]);
                if(t.chips)(*t.chips)[bit].add_fit(contribution[bit],local);
                if(t.quarter)add_fit((*t.quarter)[bit].active,contribution[bit]);
                if(t.differential)add_fit((*t.differential)[bit].active,contribution[bit]);
            }
        }
        ++counters.raw_fit_pairs;counters.raw_reused_lane_samples+=unmatched-1;
        counters.raw_lane_samples+=unmatched;
        if(options.scheduled_common_simd_experiment) {
            ++counters.simd_short_samples;++counters.simd_scalar_lane_samples;
        }
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void add_scheduled_common_raw_one_frequency_replayed(std::span<const std::size_t> lanes,
            std::array<ScheduledCommonTargets,scheduled_common_maximum_lanes>& target,
            std::size_t left,const std::array<Complex,2>& phase,std::uint64_t local,
            std::uint64_t first_chip,std::span<const ScheduledCommonRawPulse> pulses,
            ScheduledCommonWork& counters,PatternCode& pattern,detail::ScheduledOuterPublicLockstep& lockstep,
            detail::ScheduledOuterPublicScope& replay_work) {
        std::size_t unmatched=0;for(std::size_t j=0;j<lanes.size();++j)unmatched+=!target[j].fitted;
        if(!unmatched)return;
        const auto& prefix=banks[hypotheses[lanes.front()].frequency].observations(physical_view(hypotheses[lanes.front()].frequency));const auto p=prefix[left+1]-prefix[left];
        std::array<Fit,2> contribution;
        for(unsigned bit=0;bit<2;++bit)contribution[bit].add(p,phase[bit],1);
        const bool was_checked=lockstep.checked;
        bool replay=lockstep.bind(lanes.size()==2?target[0].outer:nullptr,lanes.size()==2?target[1].outer:nullptr,
            first_chip,lanes.size()==2 && unmatched==2 && !pattern.transmit_trace_enabled());
        if(!was_checked && lockstep.checked){++replay_work.count.pieces;if(lockstep.active)++replay_work.count.equal_pieces;}
        if(replay && !detail::ScheduledOuterPublicReplay::bounded(*target[0].outer,pulses)) {
            replay=false;++replay_work.count.bound_fallbacks;
        }
        std::optional<detail::ScheduledOuterPublicReplay> tape;
        if(replay)tape.emplace(*target[0].outer,detail::ScheduledOuterPublicReplay::may_flush(*target[0].outer,pulses));
        for(std::size_t j=0;j<lanes.size();++j) {
            auto& t=target[j];if(t.fitted)continue;
            const auto coefficients=[&](std::uint64_t index){return pattern.values(first_chip+index);};
            if(t.outer) {
                if(replay && j==0)tape->leader(*t.outer,pulses,Complex{p.xc,p.xs},coefficients);
                else if(replay && j==1) {
                    tape->follower(*t.outer,*target[0].outer,pulses,Complex{p.xc,p.xs},coefficients);
                    ++replay_work.count.samples;replay_work.count.events+=tape->events();
                } else for(const auto& pulse:pulses)t.outer->add(pulse.index,pulse.weight*Complex{p.xc,p.xs},coefficients);
            }
            for(unsigned bit=0;bit<2;++bit) {
                add_fit((*t.whole)[bit],contribution[bit]);
                if(t.chips)(*t.chips)[bit].add_fit(contribution[bit],local);
                if(t.quarter)add_fit((*t.quarter)[bit].active,contribution[bit]);
                if(t.differential)add_fit((*t.differential)[bit].active,contribution[bit]);
            }
        }
        ++counters.raw_fit_pairs;counters.raw_reused_lane_samples+=unmatched-1;
        counters.raw_lane_samples+=unmatched;
        if(options.scheduled_common_simd_experiment) {
            ++counters.simd_short_samples;++counters.simd_scalar_lane_samples;
        }
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void add_scheduled_common_raw_family(std::span<const std::size_t> lanes,
            std::array<ScheduledCommonTargets,scheduled_common_maximum_lanes>& target,
            std::size_t left,const std::array<Complex,2>& phase,std::uint64_t local,
            std::uint64_t first_chip,std::span<const ScheduledCommonRawPulse> pulses,
            ScheduledCommonWork& counters,PatternCode& pattern) {
        // Common-entry proofs already establish the same private phase/address,
        // source coordinate and actual gate for this sample. Equality of the
        // original frequency index then identifies the exact projection and
        // BOTH independent raw Fit contributions; detector states never alias.
        detail::ScheduledRawSampleScratch scratch;std::size_t unique=0,unmatched=0;
        for(std::size_t j=0;j<lanes.size();++j) {
            if(target[j].fitted)continue;
            const auto frequency=hypotheses[lanes[j]].frequency;
            std::size_t k=0;while(k<unique && scratch.frequency[k]!=frequency)++k;
            if(k==unique) {
                const auto& prefix=banks[frequency].observations(physical_view(frequency));const auto p=prefix[left+1]-prefix[left];
                scratch.frequency[unique]=frequency;
                scratch.projection[0][unique]=p.xc;scratch.projection[1][unique]=p.xs;
                scratch.projection[2][unique]=p.cc;scratch.projection[3][unique]=p.ss;
                scratch.projection[4][unique]=p.cs;scratch.projection[5][unique]=p.energy;++unique;
            }
            scratch.member[j]=k;++unmatched;
        }
        // Four UNIQUE frequencies, not four phase aliases, qualify for optional
        // native SIMD. A two-phase/one-frequency family uses one original scalar
        // Fit pair. Insufficient optional scratch keeps the preceding lane loop.
        const bool native=options.scheduled_common_simd_experiment &&
            unique>=detail::ScheduledRawSampleScratch::minimum_simd_lanes &&
            scheduled->common_simd_backend!=detail::family_simd::Backend::scalar;
        detail::scheduled_raw_sample_contributions(scratch,unique,phase,
            native?scheduled->common_simd_backend:detail::family_simd::Backend::scalar);
        for(std::size_t j=0;j<lanes.size();++j) {
            auto& t=target[j];if(t.fitted)continue;
            const auto k=scratch.member[j];const auto p=detail::scheduled_raw_sample_projection(scratch,k);
            // Every original lane receives its own original real observation,
            // preserving per-sample outer -> bit0 -> bit1 guard chronology.
            if(t.outer)for(const auto& pulse:pulses)t.outer->add(pulse.index,
                pulse.weight*Complex{p.xc,p.xs},[&](std::uint64_t index){return pattern.values(first_chip+index);});
            for(unsigned bit=0;bit<2;++bit) {
                const auto contribution=detail::scheduled_raw_sample_fit(scratch,bit,k);
                add_fit((*t.whole)[bit],contribution);
                if(t.chips)(*t.chips)[bit].add_fit(contribution,local);
                if(t.quarter)add_fit((*t.quarter)[bit].active,contribution);
                if(t.differential)add_fit((*t.differential)[bit].active,contribution);
                if(native)counters.simd_guard_updates+=1+(t.chips?1U:0U)+(t.quarter?1U:0U)+(t.differential?1U:0U);
            }
            if(native && t.outer)counters.simd_outer_updates+=pulses.size();
        }
        counters.raw_fit_pairs+=unique;counters.raw_reused_lane_samples+=unmatched-unique;
        counters.raw_packed_values+=6*unique;counters.raw_lane_samples+=unmatched;
        if(native) {
            ++counters.simd_samples;counters.simd_lane_samples+=unique;counters.simd_packed_values+=6*unique;
        } else if(options.scheduled_common_simd_experiment) {
            counters.simd_scalar_lane_samples+=unique;
            if(unique<detail::ScheduledRawSampleScratch::minimum_simd_lanes)++counters.simd_short_samples;
            else ++counters.simd_isa_samples;
        }
    }
template<class Source,class Pulses,class Gain,class Prepare>
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool try_scheduled_frequency_piece(std::span<const std::size_t> lanes,
            std::array<ScheduledCommonTargets,scheduled_common_maximum_lanes>& target,
            std::uint64_t first,std::uint64_t count,std::uint64_t input_end,long double within,std::uint64_t local,
            std::uint64_t first_chip,const Hypothesis& owner,
            PatternCode& pattern,ScheduledCommonWork& counters,
            Source source_at,Pulses public_pulses,Gain gain_at,Prepare prepare,std::stop_token stop) {
        rx_cpu::Scope cpu_piece(rx_cpu::Stage::common_raw,true);
        ++counters.frequency_attempts;
        if(count<8 || count>detail::ScheduledFrequencyPiece::maximum_samples || lanes.size()<4 ||
           lanes.size()>detail::ScheduledFrequencyPiece::maximum_lanes ||
           std::any_of(target.begin(),target.begin()+lanes.size(),[](const auto& t){return t.fitted;})) {
            ++counters.frequency_geometry_fallbacks;return false;
        }
        std::size_t center=0;double minimum=banks[hypotheses[lanes.front()].frequency].frequency,maximum=minimum;
        for(auto lane:lanes) {const auto f=banks[hypotheses[lane].frequency].frequency;minimum=std::min(minimum,f);maximum=std::max(maximum,f);}
        std::array<std::size_t,scheduled_common_maximum_lanes> representative{};
        std::size_t unique=0;
        for(std::size_t j=0;j<lanes.size();++j) {
            representative[j]=j;
            for(std::size_t k=0;k<j;++k)if(hypotheses[lanes[k]].frequency==hypotheses[lanes[j]].frequency) {
                representative[j]=representative[k];break;
            }
            unique+=representative[j]==j;
        }
        if(unique<4){++counters.frequency_geometry_fallbacks;return false;}
        const auto midpoint=(minimum+maximum)/2;
        for(std::size_t j=1;j<lanes.size();++j)if(std::abs(banks[hypotheses[lanes[j]].frequency].frequency-midpoint)<
                std::abs(banks[hypotheses[lanes[center]].frequency].frequency-midpoint))center=j;
        const auto frequency=hypotheses[lanes[center]].frequency;
        const auto coarse=banks[frequency].frequency;
        std::array<detail::ScheduledFrequencyPiecePhase,scheduled_common_maximum_lanes> phases{};
        const auto left=static_cast<std::size_t>(first-sample);
        for(std::size_t j=0;j<lanes.size();++j) {
            if(representative[j]!=j){phases[j]=phases[representative[j]];continue;}
            phases[j]=detail::scheduled_frequency_piece_phase(sample,left,block_samples,coarse,
                banks[hypotheses[lanes[j]].frequency].frequency,config.sample_rate);
            if(!phases[j].supported || std::abs(phases[j].radians_per_sample*(count-1))>
                    detail::ScheduledFrequencyPiece::maximum_phase_span) {
                ++counters.frequency_geometry_fallbacks;return false;
            }
        }
        const auto prefix=banks[frequency].observations(physical_view(frequency));
        // Use the actual current input end, never an allocated/stale prefix
        // capacity on a final partial input block.
        if(input_end<sample || input_end-sample>block_samples) {++counters.frequency_geometry_fallbacks;return false;}
        const auto full_count=static_cast<std::size_t>(input_end-sample);
        if(left+count>full_count){++counters.frequency_geometry_fallbacks;return false;}
        detail::ScheduledFrequencyPiece piece(static_cast<std::size_t>(count));
        std::array<std::uint64_t,detail::correlation_pulse_atoms> pulse_position{};
        std::uint32_t pulse_valid=0;
        const auto lower=local>=8?local-8:0;
        auto first_selected=count;
        for(std::uint64_t offset=0;offset<count;++offset) {
            cancelled(stop);
            const auto gain=gain_at(offset);
            if(gain==0) {if(!piece.skip(offset)){++counters.frequency_numerical_fallbacks;return false;}continue;}
            if(first_selected==count)first_selected=offset;
            const auto at=within+offset*owner.rate;
            std::array<Complex,2> source;
            const bool temporal_copied=source_at(first+offset,at,source);
            const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
            for(auto& value:source)value=gain*pattern_limit_pcm(amplitude*value)/amplitude;
            const auto p=prefix[left+offset+1]-prefix[left+offset];
            std::array<double,detail::correlation_pulse_atoms> pulse{};
            if(outer_presence)public_pulses(first+offset,at,temporal_copied,
                [&](std::uint64_t index,double weight) {
                    require(index>=lower && index-lower<pulse.size(),"fine-frequency piece pulse support exceeds chip owner");
                    const auto j=static_cast<std::size_t>(index-lower);pulse[j]=gain*weight;
                    pulse_position[j]=index;pulse_valid|=std::uint32_t{1}<<j;
                });
            ++counters.frequency_prepared_samples;
            if(!piece.add(p,source,pulse,outer_presence,offset)){++counters.frequency_numerical_fallbacks;return false;}
        }
        std::array<detail::ScheduledFrequencyPiece::Evaluation,scheduled_common_maximum_lanes> result;
        for(std::size_t j=0;j<lanes.size();++j) {
            if(representative[j]!=j){result[j]=result[representative[j]];continue;}
            result[j]=piece.evaluate(phases[j],prefix[full_count].energy,full_count);
            counters.frequency_maximum_relative_error=std::max(counters.frequency_maximum_relative_error,
                result[j].maximum_relative_error);
            if(!result[j].supported){++counters.frequency_numerical_fallbacks;return false;}
        }
        // Atomic publication: no whole/chip/quarter/differential/outer state is
        // touched until every requested fine-frequency lane passed validation.
        if(first_selected==count){++counters.frequency_numerical_fallbacks;return false;}
        prepare(first+first_selected,within+first_selected*owner.rate);
        for(std::size_t j=0;j<lanes.size();++j) {
            apply_scheduled_common_pair(result[j].fit,target[j],local);
            if(target[j].outer)for(std::size_t p=0;p<detail::correlation_pulse_atoms;++p)if(pulse_valid&(std::uint32_t{1}<<p)) {
                target[j].outer->add(pulse_position[p],result[j].outer[p],
                    [&](std::uint64_t index){return pattern.values(first_chip+index);});
                ++counters.frequency_outer_terms;
            }
        }
        ++counters.frequency_pieces;counters.frequency_lanes+=lanes.size();return true;
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void prepare_scheduled_common_temporal(detail::ScheduledTemporalTemplates& output,
            const Hypothesis& first,std::size_t leader,std::uint64_t piece_first,
            std::uint64_t selected_first,std::uint64_t limit,std::uint64_t target_index,
            std::uint64_t first_chip,PatternCode& pattern,
            const detail::ScheduledFlatExportCertificate& export_certificate,std::stop_token stop) {
        // Raw-only metadata replay. The original common loop re-anchors source
        // arithmetic at EVERY chip/quarter/differential piece. Never replace
        // its at=piece_within+offset*rate with one whole-block affine sequence.
        rx_cpu::Scope cpu_batch(rx_cpu::Stage::temporal_export,true);
        output.start(first.phase_lower,first_chip,selected_first);
        const auto source_first=scheduled_source_start(first,first.index);
        const auto span=std::max(1.L,std::floor(static_cast<long double>(
            detail::ScheduledTemporalTemplates::Tile::maximum_coefficients-18)*code.chip_samples()/first.rate));
        const auto stop_at=selected_first+static_cast<std::uint64_t>(std::min<long double>(limit-selected_first,span));
        const bool certified_unit=export_certificate.covers(this,first.origin,first.rate,
            first.phase_lower,selected_first,stop_at);
        if(detail::scheduled_r1_export_requested()) {
            detail::scheduled_r1_export_note_attempt();
            const auto cuts_aligned=[&] {
                if(differential_window && differential_window%code.chip_samples())return false;
                if(!drift_fits.empty())for(unsigned k=1;k<drift_sections;++k)
                    if(detail::drift_boundary(k,code.symbol_samples(),drift_sections)%code.chip_samples())return false;
                return true;
            };
            // Cheap unsupported-rate/grid gate precedes the original full-flat
            // calendar proof. The optional exporter never changes admission.
            if(first.rate==1 && detail::scheduled_r1_export_grid(first.origin) &&
               stop_at<(std::uint64_t{1}<<31) && cuts_aligned() &&
               scheduled_flat(first,selected_first,stop_at,false)) {
                const auto location=scheduled_map()->locate(scheduled_tick(first,selected_first));
                const detail::ScheduledR1ExportInput input{first.rate,first.origin,source_first,
                    sample,piece_first,selected_first,stop_at,code.symbol_samples(),code.chip_samples(),
                    first.index,target_index,location.source,scheduled->anchors[leader],true,true};
                const auto proof=detail::scheduled_r1_export_proof(input);
                if(proof.eligible) {
                    const auto exported=detail::scheduled_r1_export_append(output,proof,sample,
                        selected_first,stop_at,[](std::size_t){return false;},[&]{cancelled(stop);});
                    detail::scheduled_r1_export_note_row(exported.appended);
                    output.end(exported.end);output.prepare(pattern,config,stop,outer_presence);return;
                }
            }
        }
        auto probe=piece_first;
        while(probe<stop_at) {
            cancelled(stop);
            const auto tick=scheduled_tick(first,probe);const auto location=scheduled_map()->locate(tick);
            if(!location.active) {
                const auto active=scheduled_map()->source_to_physical(location.source,false);
                const auto count=std::max(1.L,std::ceil(active.distance(tick)/first.rate));
                probe+=static_cast<std::uint64_t>(std::min(count,static_cast<long double>(stop_at-probe)));continue;
            }
            auto within=location.source.distance(scheduled->anchors[leader])-source_first;
            if(within<0){++probe;continue;}
            if(within>=2.L*code.symbol_samples())break;
            const bool next=within>=code.symbol_samples();if(next)within-=code.symbol_samples();
            if(first.index+(next?1U:0U)!=target_index)break;
            const auto local=static_cast<std::uint64_t>(within/code.chip_samples());
            auto source_end=std::min<long double>(code.symbol_samples(),(local+1)*code.chip_samples());
            if(!drift_fits.empty()) {
                unsigned section=0;
                while(section+1<drift_sections && within>=detail::drift_boundary(section+1,code.symbol_samples(),drift_sections))++section;
                source_end=std::min(source_end,static_cast<long double>(detail::drift_boundary(section+1,code.symbol_samples(),drift_sections)));
            }
            if(differential_window) {
                const auto window=static_cast<std::uint64_t>(within/differential_window);
                source_end=std::min(source_end,static_cast<long double>(std::min(code.symbol_samples(),(window+1)*differential_window)));
            }
            const auto active_count=std::max(1.L,std::ceil(SchedHopCoordinate{location.physical_end,0}.distance(tick)/first.rate));
            auto count=static_cast<std::uint64_t>(std::min<long double>({block_size,limit-probe,active_count,
                std::max(1.L,std::ceil((source_end-within)/first.rate))}));
            const auto same_owner=[&](std::uint64_t offset) {
                const auto at=within+offset*first.rate;const auto physical=tick.add_signed(offset*first.rate);
                return physical.sample<location.physical_end && at>=0 && at<source_end && at<code.symbol_samples() &&
                    static_cast<std::uint64_t>(at/code.chip_samples())==local;
            };
            while(count>1 && !same_owner(count-1))--count;
            if(!same_owner(0))break; // original consumer retains its own explicit error
            for(std::uint64_t offset=0;offset<count && probe+offset<stop_at;++offset) {
                const auto physical=probe+offset;if(physical<selected_first)continue;
                if((physical&31U)==0)cancelled(stop);
                if(!certified_unit) {
                    const auto coordinate=tick.add_signed(offset*first.rate);
                    auto gain=SchedHopSourceMap::gain(location,coordinate);
                    if(scheduled->fhss_shape.enabled)gain*=scheduled->fhss_shape.gain(0,
                        coordinate.sample%(2ULL*config.sample_rate)+coordinate.fraction,config.sample_rate);
                    if(gain==0)continue;
                }
                const auto at=within+offset*first.rate;
                if(!output.append(static_cast<std::size_t>(physical-sample),static_cast<double>(at))) {
                    output.end(physical);
                    if(certified_unit)detail::scheduled_flat_export_note(output.exported_points());
                    output.prepare(pattern,config,stop,outer_presence);return;
                }
            }
            probe+=count;
        }
        output.end(std::min(probe,stop_at));
        if(certified_unit)detail::scheduled_flat_export_note(output.exported_points());
        output.prepare(pattern,config,stop,outer_presence);
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool accumulate_scheduled_common_shape(std::span<const std::size_t> lanes,
            std::uint64_t end,PatternCode& pattern,std::size_t worker,std::size_t family,std::stop_token stop,
            const detail::ScheduledPrivateBatchView* precomputed=nullptr) {
        if(!scheduled || !scheduled->common_scratch || lanes.size()<2 || lanes.size()>scheduled_common_maximum_lanes ||
           alternate_groups || options.amplitude_only)return false;
        rx_cpu::Scope common_cpu(rx_cpu::Stage::scheduled_common_residual);
        detail::ScheduledOuterPublicScope public_replay_work;
        auto& first=hypotheses[lanes.front()];
        auto& counters=scheduled->common_work[worker];
        const bool phase_family=std::any_of(lanes.begin()+1,lanes.end(),[&](std::size_t lane) {
            return hypotheses[lane].phase_lower!=first.phase_lower || hypotheses[lane].phase_upper!=first.phase_upper;
        });
        if(phase_family)++counters.phase_attempts;
        const auto reject=[&](){if(phase_family)++counters.phase_rejects;return false;};
        const auto cursor=initial_cursor(first,end);
        const auto fence=clock_boundary(first,code.symbol_samples());
        const auto first_source=scheduled_source_start(first,first.index);
        const auto limit=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(fence)));
        detail::ScheduledFlatExportCertificate export_certificate;
        for(auto lane:lanes) {
            const auto& h=hypotheses[lane];const auto& a=scheduled->anchors[lane];const auto& b=scheduled->anchors[lanes.front()];
            if(h.origin!=first.origin || h.rate!=first.rate || h.index!=first.index || initial_cursor(h,end)!=cursor ||
               static_cast<std::uint64_t>(std::min(static_cast<long double>(end),
                   std::ceil(clock_boundary(h,code.symbol_samples()))))!=limit ||
               scheduled_source_start(h,h.index)!=first_source)return reject();
            if(!phase_family && (h.phase_lower!=first.phase_lower || h.phase_upper!=first.phase_upper ||
               a.sample!=b.sample || a.fraction!=b.fraction))return reject();
        }
        if(phase_family) {
            // Gain/mapping are observed anew every block. Reject nulls/ramps
            // before expensive private inverses; only exact immutable private
            // identity is cached, tagged by the COMPLETE target symbol index.
            if(scheduled_calendar() || cursor>=limit)return reject();
            long double left=0,right=0;
            if(!scheduled_phase_flat_coordinates(first,lanes.front(),cursor,limit,left,right,&export_certificate) ||
               left<0 || right>=2.L*code.symbol_samples())return reject();
            for(std::size_t j=1;j<lanes.size();++j) {
                long double a=0,b=0;
                if(!scheduled_phase_flat_coordinates(hypotheses[lanes[j]],lanes[j],cursor,limit,a,b) ||
                   a!=left || b!=right)return reject();
            }
            const auto private_address=[&](std::size_t lane,std::uint64_t index) {
                ++counters.phase_address_queries;
                const auto physical=scheduled_map()->source_to_physical(scheduled->anchors[lane].add_samples(
                    symbol_schedule_detail::multiply(index,code.symbol_samples())));
                require(physical.fraction==0,"phase-family private symbol origin must be integral");
                const auto sample=static_cast<std::uint32_t>(physical.sample%config.sample_rate);
                return SymbolStreamAddress{physical.sample/config.sample_rate,sample/code.symbol_samples(),sample};
            };
            const auto same_address=[&](const SymbolStreamAddress& a,const SymbolStreamAddress& b) {
                return a.epoch==b.epoch && a.ordinal==b.ordinal &&
                    (!(config.fake_hopping && config.fake_hopping->matched_gaps) || a.sample_in_second==b.sample_in_second);
            };
            const auto identity_equal=[&](std::uint64_t index) {
                ScheduledFamilyPlan::AddressProof* proof=nullptr;
                if(scheduled->families && family<scheduled->families->phase_addresses.size())
                    proof=&scheduled->families->phase_addresses[family][index%2];
                if(proof && proof->valid && proof->index==index) {
                    ++counters.phase_address_hits;return proof->equal;
                }
                ++counters.phase_address_misses;
                const auto reference=private_address(lanes.front(),index);bool equal=true;
                for(std::size_t j=1;j<lanes.size();++j)
                    if(!same_address(private_address(lanes[j],index),reference)){equal=false;break;}
                // Each immutable family has one worker at a time. Positive AND
                // negative proofs require this index, exact members/anchors,
                // original key/config/omission mode; plan eviction drops all.
                if(proof)*proof={index,equal,true};
                return equal;
            };
            const bool has_current=left<code.symbol_samples(),carry=right>=code.symbol_samples();
            if(carry && first.index==std::numeric_limits<std::uint64_t>::max())return reject();
            if((has_current && !identity_equal(first.index)) || (carry && !identity_equal(first.index+1)))return reject();
            ++counters.phase_families;counters.phase_lanes+=lanes.size();
        }
        // Entry proofs above retain the original exact private/current-carry
        // family. This optional tile changes only leader raw construction.
        auto* temporal=worker<scheduled->worker_temporal_templates.size()?
            scheduled->worker_temporal_templates[worker].get():nullptr;
        if(temporal && (scheduled_affine || scheduled->moments_eligible || !scheduled->moments.empty() ||
                scheduled->curves || scheduled->float_curves))temporal=nullptr;
        if(temporal)for(auto lane:lanes)if(physical_view(hypotheses[lane].frequency))temporal=nullptr;
        if(temporal)temporal->begin(lanes.front(),sample);
        auto observed=cursor,prepared_index=std::numeric_limits<std::uint64_t>::max();
        auto curve_index=prepared_index,curve_chip=prepared_index;
        ScheduledCurveCache::Lease curve;ScheduledFloatCurveCache::Lease float_curve;
        pattern.set_stream_phase_samples(first.phase_lower);
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
        ++counters.families;counters.lanes+=lanes.size();
        bool raw_duplicates=false,one_frequency=true;
        for(std::size_t j=1;j<lanes.size();++j) {
            one_frequency&=hypotheses[lanes[j]].frequency==hypotheses[lanes.front()].frequency;
            for(std::size_t k=0;k<j;++k)raw_duplicates|=hypotheses[lanes[j]].frequency==hypotheses[lanes[k]].frequency;
        }
        while(observed<limit) {
            cancelled(stop);
            const auto tick=scheduled_tick(first,observed);
            const auto location=scheduled_map()->locate(tick);
            if(!location.active) {
                const auto next=scheduled_map()->source_to_physical(location.source,false);
                const auto count=std::max(1.L,std::ceil(next.distance(tick)/first.rate));
                observed+=static_cast<std::uint64_t>(std::min(count,static_cast<long double>(limit-observed)));continue;
            }
            auto within=location.source.distance(scheduled->anchors[lanes.front()])-first_source;
            if(within<0){++observed;continue;}
            require(within<2.L*code.symbol_samples(),"common scheduled carry exceeds original completion fence");
            const bool next=within>=code.symbol_samples();if(next)within-=code.symbol_samples();
            require(!next || first.index<std::numeric_limits<std::uint64_t>::max(),"common scheduled carry overflow");
            const auto target_index=first.index+(next?1:0);
            require(target_index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),"common scheduled chip overflow");
            const auto first_chip=target_index*code.chips_per_symbol();
            const auto local=static_cast<std::uint64_t>(within/code.chip_samples());
            auto source_end=std::min<long double>(code.symbol_samples(),(local+1)*code.chip_samples());
            if(!drift_fits.empty()) {
                unsigned section=0;
                while(section+1<drift_sections && within>=detail::drift_boundary(section+1,code.symbol_samples(),drift_sections))++section;
                source_end=std::min(source_end,static_cast<long double>(detail::drift_boundary(section+1,code.symbol_samples(),drift_sections)));
            }
            if(differential_window) {
                const auto window=static_cast<std::uint64_t>(within/differential_window);
                source_end=std::min(source_end,static_cast<long double>(std::min(code.symbol_samples(),(window+1)*differential_window)));
            }
            const auto active_count=std::max(1.L,std::ceil(SchedHopCoordinate{location.physical_end,0}.distance(tick)/first.rate));
            auto count=static_cast<std::uint64_t>(std::min<long double>({block_size,limit-observed,active_count,
                std::max(1.L,std::ceil((source_end-within)/first.rate))}));
            const auto same_owner=[&](std::uint64_t offset) {
                const auto at=within+offset*first.rate;const auto physical=tick.add_signed(offset*first.rate);
                return physical.sample<location.physical_end && at>=0 && at<source_end && at<code.symbol_samples() &&
                    static_cast<std::uint64_t>(at/code.chip_samples())==local;
            };
            while(count>1 && !same_owner(count-1))--count;
            require(same_owner(0),"common scheduled source ownership changed at piece start");
            if(prepared_index!=target_index){pattern.prepare_symbol(first_chip,stop);prepared_index=target_index;}
            const auto shape=[&](long double at) {
                rx_cpu::Scope shape_cpu(rx_cpu::Stage::template_build,true);++counters.shapes;
                std::array<Complex,2> result;
                const auto chip=static_cast<std::uint64_t>(at/code.chip_samples());
                const auto tail=code.symbol_samples()%code.chip_samples(),final=code.chips_per_symbol()-1;
                const bool shifted=tail && final>=chip && final-chip<=8;
                if((scheduled->curves || scheduled->float_curves) && !pattern.transmit_trace_enabled() &&
                   !shifted && chip<code.chips_per_symbol()) {
                    const auto use=[&](auto& cache,auto& lease) {
                        if(curve_index!=target_index || curve_chip!=chip) {
                            lease={};
                            // Avoid unusable endpoint-only fine-lattice preparation.
                            if(code.chip_samples()<1024 && !detail::source_curve_cell_eligible(
                                detail::source_curve_coordinate(at,code.chip_samples()),chip,code.chip_samples(),detail::fine_source_curve_revision()))return false;
                            const auto address=scheduled_map()->source_to_physical(scheduled->anchors[lanes.front()].add_samples(
                                symbol_schedule_detail::multiply(target_index,code.symbol_samples())));
                            require(address.fraction==0,"common scheduled curve address must be integral");
                            const auto phase=static_cast<std::uint32_t>(address.sample%config.sample_rate);
                            const auto gate_phase=config.fake_hopping && config.fake_hopping->matched_gaps?phase:0U;
                            lease=cache->get({address.sample/config.sample_rate,phase/code.symbol_samples(),chip,gate_phase},
                                pattern,first_chip,code.chip_samples(),stop,
                                scheduled->shared_source_curves,scheduled->shared_source_family);curve_index=target_index;curve_chip=chip;
                        }
                        return cache->evaluate(lease,detail::source_curve_coordinate(at,code.chip_samples()),code.chip_samples(),result);
                    };
                    if(scheduled->float_curves?use(scheduled->float_curves,float_curve):use(scheduled->curves,curve))return result;
                }
                return pattern.shaped_values(first_chip,static_cast<double>(at));
            };
            std::array<ScheduledCommonTargets,scheduled_common_maximum_lanes> target{};
            const auto prepare_targets=[&](std::uint64_t physical,long double at) {
                for(std::size_t j=0;j<lanes.size();++j) {
                    const auto lane=lanes[j];auto& h=hypotheses[lane];auto& carry=scheduled->scheduled_carry[lane];auto& t=target[j];
                    t.whole=next?&carry.fits:&h.fits;
                    t.quarter=drift_fits.empty()?nullptr:next?&scheduled->scheduled_carry_drift[lane]:&section_fits(lane,0);
                    t.differential=differential_window?(next?&scheduled->scheduled_carry_differential[lane]:&local_fits(lane,0)):nullptr;
                    t.chips=chip_evidence.empty()?nullptr:next?&scheduled->scheduled_carry_chips[lane]:&chip_evidence[lane];
                    t.outer=outer_presence?(next?&scheduled->scheduled_carry_outer[lane]:&(*outer_evidence)[lane]):nullptr;
                    t.observed_start=next?&carry.observed_start:&h.observed_start;
                    struct RestoreIndex {std::uint64_t& target;std::uint64_t old;~RestoreIndex(){target=old;}} restore{h.index,h.index};h.index=target_index;
                    if(t.quarter)for(unsigned bit=0;bit<2;++bit)advance_drift((*t.quarter)[bit],h,physical,
                        &(next?scheduled->scheduled_carry_quarter_ranks[lane]:scheduled->scheduled_quarter_ranks[lane])[bit],at);
                    if(t.differential)for(auto& fit:*t.differential)advance_differential(fit,h,physical,at);
                    if(!(*t.whole)[0].count)*t.observed_start=physical;
                }
            };
            const auto piece_first=observed;
            const auto has_moments=[&](std::size_t lane){return !scheduled->moments.empty() || static_cast<bool>(physical_view(hypotheses[lane].frequency));};
            const auto flat=(scheduled_affine || has_moments(lanes.front())) && scheduled_flat(first,observed,observed+count,false);
            bool targets_ready=false,any_raw=true;
            detail::ScheduledPulseSpan outer_pulse;
            if(flat) {
                const auto chip=code.chip_samples(),symbol=code.symbol_samples();
                const auto duration=static_cast<long double>(chip)/first.rate;
                const auto offset=(within-static_cast<long double>(local)*chip)/first.rate;
                count=std::min(count,pulse_span_count(offset,count,duration));
                const auto tail=symbol%chip,final=code.chips_per_symbol()-1;
                if(tail && final>=local && final-local<=8)count=std::min(count,pulse_span_count(offset+
                    static_cast<long double>(chip-tail)/(2*first.rate),count,duration));
                if(outer_presence && has_moments(lanes.front())) {
                    outer_pulse=detail::scheduled_pulse_span(within,first.rate,chip,symbol,count);count=std::min(count,outer_pulse.count);
                }
                if(count>1) {
                    const auto value=shape(within),last=shape(within+(count-1)*first.rate);
                    const auto radius=pattern_pcm_radius_limit/amplitude;bool unclipped=true;
                    for(unsigned bit=0;bit<2;++bit)unclipped&=std::norm(value[bit])<radius*radius*(1-1e-10) && std::norm(last[bit])<radius*radius*(1-1e-10);
                    if(unclipped) {
                        std::array<Complex,2> slope;for(unsigned bit=0;bit<2;++bit)slope[bit]=(last[bit]-value[bit])/static_cast<double>(count-1);
                        prepare_targets(observed,within);targets_ready=true;any_raw=false;++counters.affine_preparations;
                        // ONE validated pair/physical moment set is live at a
                        // time. Exact same-bank siblings share only its pure
                        // contribution; all detector and outer-ring state stays
                        // independent. The cache ends with this common piece.
                        const auto reuse=detail::scheduled_mapped_piece_groups<scheduled_common_maximum_lanes>(
                            lanes.size(),options.scheduled_common_affine_reuse,
                            [&](std::size_t j) {
                                const auto lane=lanes[j];
                                return detail::ScheduledMappedPieceKey{hypotheses[lane].frequency,has_moments(lane)};
                            },
                            [&](std::size_t j,std::array<Fit,2>& pair,detail::ScheduledSpanMoments& accepted) {
                                auto& t=target[j];const auto lane=lanes[j];
                                if(has_moments(lane))return fit_mapped_piece(lane,hypotheses[lane].frequency,
                                    static_cast<std::size_t>(observed-sample),count,value,slope,*t.whole,t.quarter,
                                    t.differential,t.chips,local,&accepted,&pair);
                                if(scheduled_affine && !t.outer) {
                                    fit_affine_piece(banks[hypotheses[lane].frequency],static_cast<std::size_t>(observed-sample),count,
                                        value,slope,*t.whole,t.quarter,t.differential,t.chips,local);return true;
                                }
                                return false;
                            },
                            [&](std::size_t j,std::size_t representative,bool fitted,
                                const std::array<Fit,2>& pair,const detail::ScheduledSpanMoments& accepted) {
                                auto& t=target[j];const auto lane=lanes[j];t.fitted=fitted;
                                if(fitted) {
                                    if(j!=representative)apply_scheduled_common_pair(pair,t,local);
                                    ++counters.affine_lanes;++affine_counts[lane];
                                    // Raw physical moments enter each original
                                    // complete-pulse ring, never a covariance
                                    // fragment or another lane's variance.
                                    if(t.outer)detail::scheduled_outer_piece(accepted,outer_pulse,*t.outer,
                                        [&](std::uint64_t at){return pattern.values(first_chip+at);});
                                } else {any_raw=true;++counters.gram_fallbacks;}
                            });
                        counters.mapped_pairs+=reuse.pairs;counters.mapped_failed_pairs+=reuse.failed_pairs;
                        counters.mapped_lanes+=reuse.logical_lanes;counters.affine_reused_lanes+=reuse.reused_lanes;
                    }
                }
            }
            // Source preparation, public pulse tickets and calendar gain are
            // shared by both contraction backends. A Temporal raw ticket is
            // consumed immediately, before another row can reprepare its Plan.
            const bool certified_raw_unit=export_certificate.covers(this,first.origin,first.rate,
                first.phase_lower,piece_first,piece_first+count);
            const auto gain_at=[&](std::uint64_t offset) {
                if(certified_raw_unit)return 1.;
                const auto coordinate=tick.add_signed(offset*first.rate);
                auto gain=SchedHopSourceMap::gain(location,coordinate);
                if(scheduled->fhss_shape.enabled)gain*=scheduled->fhss_shape.gain(0,
                    coordinate.sample%(2ULL*config.sample_rate)+coordinate.fraction,config.sample_rate);
                return gain;
            };
            const auto raw_source=[&](std::uint64_t physical,long double at,std::array<Complex,2>& phase) {
                bool copied=false;
                const bool private_copied=precomputed && precomputed->raw(physical,at,phase);
                if(!private_copied && temporal) {
                    if(!temporal->attempted(lanes.front(),sample,first.phase_lower,first_chip,physical)) {
                        // The outlined replay retains the current ORIGINAL
                        // piece anchor even when first useful gain is offset>0.
                        prepared_index=std::numeric_limits<std::uint64_t>::max();
                        prepare_scheduled_common_temporal(*temporal,first,lanes.front(),piece_first,physical,
                            limit,target_index,first_chip,pattern,export_certificate,stop);
                        pattern.set_stream_phase_samples(first.phase_lower);pattern.prepare_symbol(first_chip,stop);
                        prepared_index=target_index;
                    }
                    copied=temporal->raw(lanes.front(),sample,first.phase_lower,first_chip,physical,
                        static_cast<std::size_t>(physical-sample),static_cast<double>(at),phase);
                }
                if(private_copied || copied)++counters.shapes; // same logical original raw outputs
                else phase=shape(at);
                return copied;
            };
            const auto public_pulses=[&](std::uint64_t physical,long double at,bool copied,auto collect) {
                const bool private_reused=precomputed && precomputed->outer_pulses(physical,at,collect);
                const bool reused=private_reused || (copied && temporal && temporal->outer_pulses(lanes.front(),sample,
                    first.phase_lower,first_chip,physical,static_cast<std::size_t>(physical-sample),
                    static_cast<double>(at),collect));
                if(!reused)pattern_pulse_each(static_cast<double>(at),code.symbol_samples(),code.chip_samples(),collect);
            };
            if(any_raw && scheduled->frequency_piece_scratch &&
               config.search_arithmetic!=SearchArithmetic::fp64 &&
               try_scheduled_frequency_piece(lanes,target,piece_first,count,end,within,local,first_chip,
                    first,pattern,counters,raw_source,public_pulses,gain_at,prepare_targets,stop)) {
                if(certified_raw_unit)detail::scheduled_flat_raw_note(count);
                ++counters.pieces;observed+=count;continue;
            }
            if(any_raw) {
            rx_cpu::Scope cpu_common_raw(rx_cpu::Stage::common_raw,true);
            detail::ScheduledOuterPublicLockstep public_lockstep;
            for(std::uint64_t offset=0;offset<count;++offset) {
                cancelled(stop);const auto physical=piece_first+offset;
                const auto gain=gain_at(offset);
                if(gain==0)continue;
                const auto at=within+offset*first.rate;
                if(!targets_ready){prepare_targets(physical,at);targets_ready=true;}
                std::array<Complex,2> phase;
                const bool copied=raw_source(physical,at,phase);
                for(auto& coefficient:phase)coefficient=gain*pattern_limit_pcm(amplitude*coefficient)/amplitude;
                ++counters.raw_samples;
                // Pulse geometry is public/common, while ring state and the
                // independent private coefficient flush stay owned per lane.
                std::array<ScheduledCommonRawPulse,detail::correlation_pulse_atoms> pulses{};std::size_t pulses_count=0;
                if(outer_presence) {
                    const auto collect=[&](std::uint64_t index,double pulse) {
                        require(pulses_count<pulses.size(),"common outer pulse support exceeded seventeen atoms");
                        pulses[pulses_count++]={index,gain*pulse};
                    };
                    public_pulses(physical,at,copied,collect);
                }
                const auto left=static_cast<std::size_t>(physical-sample);
                if((raw_duplicates || options.scheduled_common_simd_experiment) && scheduled->common_raw_scratch &&
                   (one_frequency || scheduled->common_raw_scratch_bytes>=detail::scheduled_raw_sample_scratch_bytes)) {
                    if(options.scheduled_common_simd_experiment)++counters.simd_attempts;
                    if(one_frequency) {
                        if(config.search_arithmetic!=SearchArithmetic::fp64 && detail::scheduled_outer_public_replay_requested() &&
                           scheduled->common_raw_scratch_bytes>=detail::scheduled_outer_public_replay_frame)
                            add_scheduled_common_raw_one_frequency_replayed(lanes,target,left,phase,local,first_chip,
                                {pulses.data(),pulses_count},counters,pattern,public_lockstep,public_replay_work);
                        else add_scheduled_common_raw_one_frequency(lanes,target,left,phase,local,first_chip,
                            {pulses.data(),pulses_count},counters,pattern);
                    }
                    else add_scheduled_common_raw_family(lanes,target,left,phase,local,first_chip,
                        {pulses.data(),pulses_count},counters,pattern);
                    continue;
                }
                if(raw_duplicates)++counters.raw_workspace_samples;
                if(options.scheduled_common_simd_experiment) {
                    std::size_t unmatched=0;for(std::size_t j=0;j<lanes.size();++j)unmatched+=!target[j].fitted;
                    ++counters.simd_attempts;counters.simd_scalar_lane_samples+=unmatched;
                    if(unmatched<detail::ScheduledRawSampleScratch::minimum_simd_lanes)++counters.simd_short_samples;
                    else if(scheduled->common_simd_backend==detail::family_simd::Backend::scalar)++counters.simd_isa_samples;
                    else ++counters.simd_workspace_samples;
                }
                for(std::size_t j=0;j<lanes.size();++j) {
                    auto& t=target[j];if(t.fitted)continue;
                    const auto& prefix=banks[hypotheses[lanes[j]].frequency].observations(physical_view(hypotheses[lanes[j]].frequency));
                    const auto p=prefix[left+1]-prefix[left];
                    if(t.outer)for(std::size_t pulse=0;pulse<pulses_count;++pulse)t.outer->add(pulses[pulse].index,
                        pulses[pulse].weight*Complex{p.xc,p.xs},[&](std::uint64_t index){return pattern.values(first_chip+index);});
                    for(unsigned bit=0;bit<2;++bit) {
                        Fit contribution;contribution.add(p,phase[bit],1);
                        add_fit((*t.whole)[bit],contribution);
                        if(t.chips)(*t.chips)[bit].add_fit(contribution,local);
                        if(t.quarter)add_fit((*t.quarter)[bit].active,contribution);
                        if(t.differential)add_fit((*t.differential)[bit].active,contribution);
                    }
                    ++counters.raw_lane_samples;++counters.raw_fit_pairs;
                }
            }
            if(certified_raw_unit)detail::scheduled_flat_raw_note(count);
            } // common raw piece
            ++counters.pieces;observed+=count;
        }
        return true;
    }

    using PrivateBatchScratch=detail::ScheduledPrivateBatchScratch;
    struct PrivateBatchOwner {
        Impl* owner=nullptr;
        std::vector<std::unique_ptr<PrivateBatchScratch>> workers;
        struct OrderedFamily {std::uint64_t hash=0;std::size_t item=0;bool eligible=false;};
        std::vector<OrderedFamily> order;
        std::size_t bytes=0,order_requests=0,order_eligible=0;
        std::uint64_t order_coordinate_values=0;
        std::size_t order_largest_job_members=0,order_total_members=0;
        ~PrivateBatchOwner(){if(bytes)owner->accounted_bytes-=bytes;}
    };
    std::unique_ptr<PrivateBatchOwner> prepare_private_batch_owner(std::size_t input_samples) {
        // Independent of family/precision/cache selection. This first crossover
        // covers only the fine-chip RAW mapped path, not a replacement for
        // faster affine/moment execution or original FP64-force diagnostics.
        if(!PrivateBatchScratch::requested() || !scheduled || options.amplitude_only || options.raw_reference ||
           config.search_arithmetic==SearchArithmetic::fp64 || !shaped || alternate_groups ||
           code.chip_samples()>1023 || code.symbol_samples()<18*code.chip_samples() ||
           scheduled_affine || scheduled->moments_eligible || !scheduled->moments.empty() ||
           scheduled->curves || scheduled->float_curves || !PrivateBatchScratch::Tile::arithmetic_supported() ||
           input_samples<4 || input_samples>PrivateBatchScratch::Tile::maximum_samples)return {};
        for(std::size_t f=0;f<banks.size();++f)if(physical_view(f))return {};
        const auto count=std::max<std::size_t>(1,worker_codes.size());
        if(sizeof(PatternCorrelator)>budget || accounted_bytes>budget-sizeof(PatternCorrelator))return {};
        const auto spare=budget-sizeof(PatternCorrelator)-accounted_bytes;
        const auto headroom=projection_cache_headroom(input_samples);
        constexpr auto per=sizeof(PrivateBatchScratch)+PrivateBatchScratch::callee_bytes;
        constexpr auto pointer=sizeof(std::unique_ptr<PrivateBatchScratch>);
        if(headroom>spare || sizeof(PrivateBatchOwner)>spare-headroom ||
           count>(spare-headroom-sizeof(PrivateBatchOwner))/(per+pointer))return {};
        std::unique_ptr<PrivateBatchOwner> owner;
        try {
            owner=std::make_unique<PrivateBatchOwner>();owner->owner=this;owner->workers.reserve(count);
            const auto available=spare-headroom-sizeof(PrivateBatchOwner);
            if(owner->workers.capacity()>available/pointer)return {};
            const auto pointers=owner->workers.capacity()*pointer;
            if(count>(available-pointers)/per)return {};
            for(std::size_t i=0;i<count;++i)owner->workers.push_back(std::make_unique<PrivateBatchScratch>());
            owner->bytes=sizeof(PrivateBatchOwner)+pointers+count*per;
        } catch(const std::bad_alloc&) {return {};}
        accounted_bytes+=owner->bytes;
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
        return owner;
    }
    bool private_batch_request(std::size_t hypothesis,std::uint64_t end,
            PrivateBatchScratch& scratch,PrivateBatchScratch::Request& request,std::size_t& count) {
        ++scratch.counts.considered;const auto& h=hypotheses[hypothesis];
        if(initial_cursor(h,end)!=sample || clock_boundary(h,code.symbol_samples())<end ||
           !scheduled_flat(h,sample,end,false))return false;
        const auto tick=scheduled_tick(h,sample);
        const auto location=scheduled_map()->locate(tick);
        // Original accumulate_scheduled anchors all samples to this ONE active
        // piece. Reject a map re-anchor rather than substitute modulo arithmetic.
        if(!location.active || SchedHopCoordinate{location.physical_end,0}.distance(
                scheduled_tick(h,end-1))<=0)return false;
        const auto original=location.source.distance(scheduled->anchors[hypothesis])-scheduled_source_start(h,h.index);
        const auto last=original+static_cast<long double>(end-sample-1)*h.rate;
        const auto symbol=code.symbol_samples();
        if(original<0 || original>=2.L*symbol || last>=2.L*symbol ||
           (original>=symbol)!=(last>=symbol))return false;
        const bool carry=original>=symbol;
        if(carry && h.index==std::numeric_limits<std::uint64_t>::max())return false;
        const auto index=h.index+(carry?1:0);
        if(index>std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol())return false;
        count=static_cast<std::size_t>(end-sample);
        for(std::size_t n=0;n<count;++n) {
            auto at=original+static_cast<long double>(n)*h.rate;
            if(carry)at-=symbol;
            scratch.temporary[n]=static_cast<double>(at);
        }
        scratch.counts.coordinate_values+=count;
        const auto normalization=detail::scheduled_temporal_normalization(
            {scratch.temporary.data(),count},symbol,code.chip_samples());
        if(!normalization.eligible)return false;
        const auto offset=static_cast<double>(code.chip_samples()*normalization.chip_translation);
        for(std::size_t n=0;n<count;++n)scratch.temporary[n]-=offset;
        request={hypothesis,hypothesis,h.phase_lower,index*code.chips_per_symbol(),normalization.chip_translation,
            original,h.rate,carry};++scratch.counts.eligible;return true;
    }
    void prepare_private_batch_order(PrivateBatchOwner& owner,const ScheduledFamilyPlan* families,
            std::uint64_t end,std::stop_token stop,std::size_t group=4) {
        if(!families || !PrivateBatchScratch::order_requested() || owner.workers.empty() || !owner.order.empty())return;
        rx_cpu::Scope cpu_order(rx_cpu::Stage::setup,true);
        auto& map_view=worker_map_view();const auto previous_map=map_view;
        struct RestoreMap {decltype(map_view) target;decltype(previous_map) old;~RestoreMap(){target=old;}} restore{map_view,previous_map};
        if(!scheduled->worker_maps.empty())map_view={this,scheduled->worker_maps.front().get()};
        const auto items=families->ends.size()-1;
        // Bounded dispatch permutation only. Full coordinate equality and the
        // independent private identity remain checked by the executing job.
        // No H-sized shifted-input or coordinate matrix is retained.
        if(items<4 || items>4096 || sizeof(PatternCorrelator)>budget ||
           accounted_bytes>budget-sizeof(PatternCorrelator))return;
        const auto spare=budget-sizeof(PatternCorrelator)-accounted_bytes;
        const auto headroom=projection_cache_headroom(static_cast<std::size_t>(end-sample));
        if(headroom>spare || items>(spare-headroom)/sizeof(PrivateBatchOwner::OrderedFamily))return;
        std::vector<PrivateBatchOwner::OrderedFamily> rows;
        try {rows.resize(items);}catch(const std::bad_alloc&){return;}
        if(rows.capacity()>(spare-headroom)/sizeof(PrivateBatchOwner::OrderedFamily))return;
        const auto bytes=rows.capacity()*sizeof(PrivateBatchOwner::OrderedFamily);
        owner.order=std::move(rows);owner.bytes+=bytes;accounted_bytes+=bytes;
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
        auto& scratch=*owner.workers.front();const auto previous_coordinates=scratch.counts.coordinate_values;
        for(std::size_t item=0;item<items;++item) {
            cancelled(stop);auto& row=owner.order[item];row.item=item;++owner.order_requests;
            const auto lane=families->members[families->ends[item]];
            PrivateBatchScratch::Request request;std::size_t count=0;
            row.eligible=hypotheses[lane].rate==1 && private_batch_request(lane,end,scratch,request,count);
            if(row.eligible){row.hash=PrivateBatchScratch::hash({scratch.temporary.data(),count});++owner.order_eligible;}
        }
        owner.order_coordinate_values=scratch.counts.coordinate_values-previous_coordinates;
        // std::sort requires no stable_sort auxiliary allocation. Original IDs
        // make equal-key order deterministic; serial completion is unchanged.
        cancelled(stop);
        std::sort(owner.order.begin(),owner.order.end(),[](const auto& a,const auto& b) {
            if(a.eligible!=b.eligible)return a.eligible>b.eligible;
            if(a.hash!=b.hash)return a.hash<b.hash;
            return a.item<b.item;
        });
        cancelled(stop);std::size_t job_members=0;
        for(std::size_t rank=0;rank<owner.order.size();++rank) {
            const auto item=owner.order[rank].item;
            const auto members=families->ends[item+1]-families->ends[item];
            owner.order_total_members+=members;job_members+=members;
            if((rank+1)%group==0 || rank+1==owner.order.size()) {
                owner.order_largest_job_members=std::max(owner.order_largest_job_members,job_members);job_members=0;
            }
        }
    }
    template<class Consume>
    void private_batch_flush(PrivateBatchScratch& scratch,PrivateBatchScratch::Bucket& bucket,
            PatternCode& pattern,std::uint64_t end,Consume consume,std::stop_token stop) {
        if(!bucket.lanes)return;
        ++scratch.counts.occupancy[bucket.lanes];
        bool ready=false;
        if(bucket.lanes>1) {
            ++scratch.counts.groups;
            if(scratch.plan.build_original({bucket.coordinates.data(),bucket.count},
                    code.symbol_samples(),code.chip_samples(),scratch.tile.statistics,stop)) {
                for(std::size_t j=0;j<bucket.lanes;++j) {
                    const auto& r=bucket.requests[j];
                    scratch.bindings[j]={&pattern,&config,r.phase,r.first_chip,
                        r.translation+scratch.plan.first_local};
                }
                ready=scratch.tile.gather_borrowed({scratch.bindings.data(),bucket.lanes},scratch.plan,stop) &&
                    scratch.tile.evaluate(PrivateBatchScratch::Tile::Backend::automatic,stop);
                if(ready) {
                    scratch.counts.batched_lanes+=bucket.lanes;
                    scratch.counts.prepared_samples+=bucket.lanes*bucket.count;
                } else ++scratch.counts.numeric_fallbacks;
            } else ++scratch.counts.plan_fallbacks;
        } else ++scratch.counts.singletons;
        // Gather changes PatternCode even when it fails. Each original replay
        // has a fresh pattern_ready=false/prepared_chip state and restores its
        // own context before ANY private shape or outer-ring callback.
        for(std::size_t j=0;j<bucket.lanes;++j) {
            cancelled(stop);const auto& r=bucket.requests[j];
            const detail::ScheduledPrivateBatchView view{ready?&scratch.tile:nullptr,j,sample,end,
                code.symbol_samples(),r.original_source,r.rate,r.carry,&scratch.counts.consumed_samples,
                ready?&scratch.plan:nullptr,r.translation,&scratch.counts.outer_rows,
                {bucket.coordinates.data(),bucket.count},code.chip_samples()};
            consume(r.consumer,ready?&view:nullptr);
        }
        scratch.tile.reset_binding();bucket.lanes=0;
    }
    template<class Select,class Consume>
    void accumulate_private_batch_job(std::size_t first,std::size_t last,std::uint64_t end,
            PatternCode& pattern,PrivateBatchScratch& scratch,Select select,Consume consume,std::stop_token stop,
            bool common_leaders=false) {
        for(auto& bucket:scratch.groups)bucket.lanes=0;
        for(auto consumer=first;consumer<last;++consumer) {
            cancelled(stop);const auto lane=select(consumer);PrivateBatchScratch::Request request;std::size_t count=0;
            // Common-family loop reanchors at each source piece. Nominal R1
            // additions are exact under the existing normalization range;
            // other clocks retain their preceding common or individual path.
            if((common_leaders && hypotheses[lane].rate!=1) ||
               !private_batch_request(lane,end,scratch,request,count)){consume(consumer,nullptr);continue;}
            request.consumer=consumer;
            if(PrivateBatchScratch::census_requested() && scratch.census_count<scratch.census.size()) {
                auto& c=scratch.census[scratch.census_count++];
                c.exact_hash=PrivateBatchScratch::hash({scratch.temporary.data(),count});
                c.first=scratch.temporary.front();c.last=scratch.temporary[count-1];c.count=count;
                c.half_sample_key=static_cast<std::uint64_t>(std::llround(2*c.first));
                c.phase=request.phase;c.first_chip=request.first_chip;c.translation=request.translation;
                c.origin=hypotheses[lane].origin;c.rate=request.rate;
                c.wide_first=request.original_source-(request.carry?static_cast<long double>(code.symbol_samples()):0.L)-
                    static_cast<long double>(request.translation)*code.chip_samples();
            }
            auto* bucket=scratch.find({scratch.temporary.data(),count});
            if(!bucket){consume(consumer,nullptr);continue;}
            require(bucket->lanes<PrivateBatchScratch::Tile::maximum_lanes,"private batch lane overflow");
            bucket->requests[bucket->lanes++]=request;
            if(bucket->lanes==PrivateBatchScratch::Tile::maximum_lanes)
                private_batch_flush(scratch,*bucket,pattern,end,consume,stop);
        }
        for(auto& bucket:scratch.groups)private_batch_flush(scratch,bucket,pattern,end,consume,stop);
    }
    void report_private_batch_owner(const PrivateBatchOwner& owner,std::size_t leader_group) const {
        if(!audio::trace::active.load())return;
        PrivateBatchScratch::Counts c;
        for(const auto& p:owner.workers) {
            const auto& b=p->counts;
            c.considered+=b.considered;c.eligible+=b.eligible;c.groups+=b.groups;c.batched_lanes+=b.batched_lanes;
            c.singletons+=b.singletons;c.coordinate_values+=b.coordinate_values;c.prepared_samples+=b.prepared_samples;
            c.consumed_samples+=b.consumed_samples;c.outer_rows+=b.outer_rows;c.bucket_exhaustions+=b.bucket_exhaustions;
            c.plan_fallbacks+=b.plan_fallbacks;c.numeric_fallbacks+=b.numeric_fallbacks;
            for(std::size_t n=1;n<c.occupancy.size();++n)c.occupancy[n]+=b.occupancy[n];
        }
        audio::trace::record("scheduled_private_batch_work",[&](auto& f) {
            f.add("first",sample);f.add("workspace",owner.bytes);f.add("workers",owner.workers.size());
            f.add("leader_group",leader_group);f.add("ordered_families",owner.order.size());
            f.add("ordering_requests",owner.order_requests);f.add("ordering_eligible",owner.order_eligible);
            f.add("ordering_coordinate_values",owner.order_coordinate_values);
            f.add("ordering_job_members_max",owner.order_largest_job_members);f.add("ordering_members",owner.order_total_members);
            f.add("rows1",c.occupancy[1]);f.add("rows2",c.occupancy[2]);
            f.add("rows3",c.occupancy[3]);f.add("rows4",c.occupancy[4]);
            f.add("considered",c.considered);f.add("eligible",c.eligible);f.add("groups",c.groups);
            f.add("batched_lanes",c.batched_lanes);f.add("singletons",c.singletons);
            f.add("coordinate_values",c.coordinate_values);f.add("prepared_samples",c.prepared_samples);
            f.add("consumed_samples",c.consumed_samples);f.add("outer_rows",c.outer_rows);f.add("bucket_exhaustions",c.bucket_exhaustions);
            f.add("plan_fallbacks",c.plan_fallbacks);f.add("numeric_fallbacks",c.numeric_fallbacks);
        });
        if(PrivateBatchScratch::census_requested()) {
            // Diagnostic only; a fixed process-wide cap bounds retained trace
            // traffic. No key bytes, private coefficients or payloads emitted.
            static std::atomic<std::uint64_t> emitted{0};
            for(const auto& worker:owner.workers)for(std::size_t n=0;n<worker->census_count;++n) {
                const auto ordinal=emitted.fetch_add(1,std::memory_order_relaxed);if(ordinal>=64)return;
                const auto& x=worker->census[n];audio::trace::record("scheduled_private_census",[&](auto& f) {
                    f.add("first",sample);f.add("ordinal",ordinal);f.add("hash",x.exact_hash);f.add("half_key",x.half_sample_key);
                    f.add("first_norm",x.first);f.add("last_norm",x.last);f.add("wide_first",x.wide_first);
                    f.add("origin",x.origin);f.add("rate",x.rate);f.add("count",x.count);
                    f.add("phase",x.phase);f.add("first_chip",x.first_chip);f.add("translation",x.translation);
                });
            }
        }
    }

    std::uint64_t accumulate_scheduled(Hypothesis& h,std::size_t hypothesis,std::uint64_t cursor,
            std::uint64_t end,PatternCode& pattern,std::stop_token stop,
            const detail::ScheduledPrivateBatchView* precomputed=nullptr) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::scheduled_candidate_residual);
        const auto symbol_source=scheduled_source_start(h,h.index);
        const auto symbol_end=clock_boundary(h,code.symbol_samples());
        const auto limit=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(symbol_end)));
        require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
            "pattern chip coordinate overflow");
        bool pattern_ready=false;
        auto prepared_chip=std::numeric_limits<std::uint64_t>::max();
        auto* cache=scheduled_template_cache();
        auto* temporal=scheduled_temporal_templates();
        if(temporal)temporal->begin(hypothesis,sample);
        auto* current_row=cache?&cache->row(h.origin,h.rate,h.phase_lower,h.index,sample):nullptr;
        ScheduledTemplateCache::Row* next_row=nullptr;
        auto& fit=h.fits;
        auto* drift=drift_fits.empty()?nullptr:&section_fits(hypothesis,0);
        auto* differential=differential_window?&local_fits(hypothesis,0):nullptr;
        std::array<Complex,2> interval_value{},interval_slope{};
        std::uint64_t interval_first=0,interval_end=0,interval_index=0;
        const bool interval_reuse=scheduled->moments_eligible;
        ScheduledCurveCache::Lease curve;
        ScheduledFloatCurveCache::Lease float_curve;
        std::uint64_t curve_index=std::numeric_limits<std::uint64_t>::max(),curve_chip=curve_index;
        auto observed=cursor;
        while(observed<limit) {
            cancelled(stop);
            rx_cpu::Scope cpu_calendar(rx_cpu::Stage::calendar,true);
            const auto tick=scheduled_tick(h,observed);
            const auto location=scheduled_map()->locate(tick);
            cpu_calendar.finish();
            if(!location.active) {
                const auto next=scheduled_map()->source_to_physical(location.source,false);
                const auto advance=next.distance(tick)/h.rate;
                const auto count=std::max(1.L,std::ceil(advance));
                observed+=static_cast<std::uint64_t>(std::min(count,static_cast<long double>(limit-observed)));
                continue;
            }
            // This immutable location is valid to its actual physical end.
            // Source advances only here; carrier prefixes always advance in
            // physical time, including all quiet calendar intervals.
            const auto piece_count=std::max(1.L,std::ceil(SchedHopCoordinate{location.physical_end,0}.distance(tick)/h.rate));
            const auto piece_end=observed+static_cast<std::uint64_t>(std::min(piece_count,static_cast<long double>(limit-observed)));
            const auto source=location.source.distance(scheduled->anchors[hypothesis])-symbol_source;
            const auto piece_first=observed;
            const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
            // This is an algebra shortcut for the actual central candidate,
            // not a proof that an entire timing/clock coverage cell is null.
            // Admission, completion and capture proofs retain whole_cell=true.
            const bool piece_flat=(scheduled_affine || !scheduled->moments.empty() || physical_view(h.frequency)) && scheduled_flat(h,piece_first,piece_end,false);
            interval_end=0;
            for(;observed<piece_end;++observed) {
                auto within=source+static_cast<long double>(observed-piece_first)*h.rate;
                if(within<0)continue;
                if(within>=2.L*code.symbol_samples())throw Error("scheduled complex clock-cell coverage exhausted: more than one adjacent source symbol precedes the completion fence");
                const bool next=within>=code.symbol_samples();
                if(next)within-=code.symbol_samples();
                const auto current_index=h.index;
                require(!next || current_index<std::numeric_limits<std::uint64_t>::max(),"scheduled carry symbol coordinate overflow");
                const auto target_index=current_index+(next?1:0);
                require(target_index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),"scheduled carry chip coordinate overflow");
                const auto target_chip=target_index*code.chips_per_symbol();
                const auto physical=tick.add_signed(static_cast<long double>(observed-piece_first)*h.rate);
                auto gain=SchedHopSourceMap::gain(location,physical);
                if(scheduled->fhss_shape.enabled)gain*=scheduled->fhss_shape.gain(0,
                    physical.sample%(2ULL*config.sample_rate)+physical.fraction,config.sample_rate);
                if(gain==0)continue;
                auto& carry=scheduled->scheduled_carry[hypothesis];
                auto& target_fit=next?carry.fits:fit;
                auto* target_drift=next?(drift?&scheduled->scheduled_carry_drift[hypothesis]:nullptr):drift;
                auto* target_differential=next?(differential?&scheduled->scheduled_carry_differential[hypothesis]:nullptr):differential;
                auto* target_chips=chip_evidence.empty()?nullptr:next?&scheduled->scheduled_carry_chips[hypothesis]:&chip_evidence[hypothesis];
                auto* target_outer=outer_presence?(next?&scheduled->scheduled_carry_outer[hypothesis]:&(*outer_evidence)[hypothesis]):nullptr;
                // Only source-symbol ownership changes temporarily. The fixed
                // physical timing reference, phase and rate never reset.
                struct RestoreIndex {std::uint64_t& index;std::uint64_t value;~RestoreIndex(){index=value;}} restore_index{h.index,current_index};
                h.index=target_index;
                if(target_drift)for(unsigned bit=0;bit<2;++bit)advance_drift((*target_drift)[bit],h,observed,
                    &(next?scheduled->scheduled_carry_quarter_ranks[hypothesis]:scheduled->scheduled_quarter_ranks[hypothesis])[bit],within);
                if(target_differential)for(auto& item:*target_differential)advance_differential(item,h,observed,within);
                if(!target_fit[0].count)(next?carry.observed_start:h.observed_start)=observed;
                if(next && cache && !next_row)next_row=&cache->row(h.origin,h.rate,h.phase_lower,target_index,sample);
                auto* row=next?next_row:current_row;
                const auto left=static_cast<std::size_t>(observed-sample);
                const auto valid=row && (row->valid[left/64]&(1ULL<<(left%64)));
                const auto prepare=[&] {
                    rx_cpu::Scope cpu_prepare(rx_cpu::Stage::template_build,true);
                    if(!pattern_ready){pattern.set_stream_phase_samples(h.phase_lower);pattern_ready=true;}
                    if(target_chip!=prepared_chip){pattern.prepare_symbol(target_chip,stop);prepared_chip=target_chip;}
                };
                const auto shape=[&](long double at) {
                    rx_cpu::Scope cpu_shape(rx_cpu::Stage::template_build,true);
                    std::array<Complex,2> result;
                    // A checked PUBLIC-geometry batch owns only original source
                    // outputs. Limiting/gates, physical projections and every
                    // chronological guard below stay on the original path.
                    if(precomputed && precomputed->raw(observed,at,result))return result;
                    const auto local=static_cast<std::uint64_t>(at/code.chip_samples());
                    const auto partial=code.symbol_samples()%code.chip_samples();
                    const auto final_chip=code.chips_per_symbol()-1;
                    const bool shifted_tail=partial && final_chip>=local && final_chip-local<=8;
                    if((scheduled->curves || scheduled->float_curves) && !pattern.transmit_trace_enabled() &&
                       !shifted_tail && local<code.chips_per_symbol()) {
                        const auto use=[&](auto& cache,auto& lease) {
                            if(curve_index!=target_index || curve_chip!=local) {
                                lease={}; // Release stale ownership before endpoint fallback.
                                if(code.chip_samples()<1024 && !detail::source_curve_cell_eligible(
                                    detail::source_curve_coordinate(at,code.chip_samples()),local,code.chip_samples(),detail::fine_source_curve_revision()))return false;
                                // Same canonical crypto address reproduces both
                                // private candidates and the full outer permutation.
                                const auto address=scheduled_map()->source_to_physical(scheduled->anchors[hypothesis].add_samples(
                                    symbol_schedule_detail::multiply(target_index,code.symbol_samples())));
                                require(address.fraction==0,"scheduled curve symbol address must be integral");
                                const auto phase=static_cast<std::uint32_t>(address.sample%config.sample_rate);
                                const auto gate_phase=config.fake_hopping && config.fake_hopping->matched_gaps?phase:0U;
                                lease=cache->get({address.sample/config.sample_rate,phase/code.symbol_samples(),local,gate_phase},
                                    pattern,target_chip,code.chip_samples(),stop,
                                    scheduled->shared_source_curves,scheduled->shared_source_family);
                                curve_index=target_index;curve_chip=local;
                            }
                            return cache->evaluate(lease,detail::source_curve_coordinate(at,code.chip_samples()),code.chip_samples(),result);
                        };
                        if(scheduled->float_curves?use(scheduled->float_curves,float_curve):use(scheduled->curves,curve))return result;
                    }
                    return pattern.shaped_values(target_chip,static_cast<double>(at));
                };
                if(piece_flat) {
                    const auto chip=code.chip_samples(),symbol=code.symbol_samples();
                    const auto local=static_cast<std::uint64_t>(within/chip);
                    auto source_end=std::min<long double>(symbol,(local+1)*chip);
                    if(target_drift)source_end=std::min(source_end,static_cast<long double>(detail::drift_boundary(
                        (*target_drift)[0].section+1,symbol,drift_sections)));
                    if(target_differential)source_end=std::min(source_end,static_cast<long double>(
                        std::min(symbol,((*target_differential)[0].window_index+1)*differential_window)));
                    const auto maximum=static_cast<std::uint64_t>(std::min<long double>(std::min<std::uint64_t>(piece_end-observed,block_size),
                        std::max(1.L,std::ceil((source_end-within)/h.rate))));
                    const auto duration=static_cast<long double>(chip)/h.rate;
                    const auto offset=(within-static_cast<long double>(local)*chip)/h.rate;
                    auto count=pulse_span_count(offset,maximum,duration);
                    const auto tail=symbol%chip,final_chip=code.chips_per_symbol()-1;
                    if(tail && final_chip>=local && final_chip-local<=8) {
                        const auto last=pulse_span_count(offset+
                            static_cast<long double>(chip-tail)/(2*h.rate),maximum,duration);
                        count=std::min(count,last);
                    }
                    const auto contract=[&](std::uint64_t span_count,detail::ScheduledSpanMoments* accepted) {
                        if(span_count<=1)return false;
                        prepare();const auto first=shape(within);
                        const auto last=shape(within+(span_count-1)*h.rate);
                        const auto radius=pattern_pcm_radius_limit/amplitude;
                        bool unclipped=true;
                        for(unsigned bit=0;bit<2;++bit)unclipped&=std::norm(first[bit])<radius*radius*(1-1e-10) && std::norm(last[bit])<radius*radius*(1-1e-10);
                        if(!unclipped)return false;
                        std::array<Complex,2> slope;
                        for(unsigned bit=0;bit<2;++bit)slope[bit]=(last[bit]-first[bit])/static_cast<double>(span_count-1);
                        if(!scheduled->moments.empty() || physical_view(h.frequency))return fit_mapped_piece(hypothesis,h.frequency,left,span_count,first,slope,target_fit,
                            target_drift,target_differential,target_chips,local,accepted);
                        fit_affine_piece(banks[h.frequency],left,span_count,first,slope,target_fit,
                            target_drift,target_differential,target_chips,local);return true;
                    };
                    std::uint64_t fitted_count=0;
                    if(target_outer && count>1 && (!scheduled->moments.empty() || physical_view(h.frequency))) {
                        rx_cpu::Scope pulse_cpu_scope(rx_cpu::Stage::template_build,true);
                        // The outlined helper owns the charged outer-only
                        // frame. No large pulse scratch enters the ordinary
                        // accumulator when moments are unavailable/evicted.
                        fitted_count=detail::scheduled_with_outer_span(within,h.rate,chip,symbol,count,
                            [&](const detail::ScheduledPulseSpan& pulse,detail::ScheduledSpanMoments& accepted) {
                                if(!contract(pulse.count,&accepted))return std::uint64_t{0};
                                rx_cpu::Scope outer_cpu_scope(rx_cpu::Stage::guards,true);
                                // Complete the original pulse ring; current/
                                // carry ownership and private addresses stay
                                // separate from the shared physical moments.
                                detail::scheduled_outer_piece(accepted,pulse,*target_outer,
                                    [&](std::uint64_t position){return pattern.values(target_chip+position);});
                                return pulse.count;
                            });
                    } else if(!target_outer && contract(count,nullptr))fitted_count=count;
                    if(fitted_count){++affine_counts[hypothesis];observed+=fitted_count-1;continue;}
                }
                if(!valid || target_outer)prepare();
                const auto projection=banks[h.frequency].observations(physical_view(h.frequency))[left+1]-banks[h.frequency].observations(physical_view(h.frequency))[left];
                std::array<Complex,2> phases{};bool temporal_copied=false;
                if(valid){phases=row->value[left];++cache->hits;}
                else {
                    if(precomputed && precomputed->raw(observed,within,phases)) {
                        // Checked cohort output replaces only private source
                        // reconstruction. Original limiting/cache/guard code
                        // below still executes for this actual observation.
                    } else if(interval_reuse && observed<interval_end && interval_index==target_index) {
                        const auto delta=static_cast<double>(observed-interval_first);
                        for(unsigned bit=0;bit<2;++bit)phases[bit]=interval_value[bit]+delta*interval_slope[bit];
                    } else {
                        auto count=std::uint64_t{1};
                        if(interval_reuse) {
                            const auto chip=code.chip_samples(),symbol=code.symbol_samples();
                            const auto local=static_cast<std::uint64_t>(within/chip);
                            const auto source_end=std::min<long double>(symbol,(local+1)*chip);
                            const auto maximum=static_cast<std::uint64_t>(std::min<long double>(std::min<std::uint64_t>(piece_end-observed,block_size),
                                std::max(1.L,std::ceil((source_end-within)/h.rate))));
                            const auto duration=static_cast<long double>(chip)/h.rate;
                            const auto offset=(within-static_cast<long double>(local)*chip)/h.rate;
                            count=pulse_span_count(offset,maximum,duration);
                            const auto tail=symbol%chip,final_chip=code.chips_per_symbol()-1;
                            if(tail && final_chip>=local && final_chip-local<=8)count=std::min(count,
                                pulse_span_count(offset+static_cast<long double>(chip-tail)/(2*h.rate),maximum,duration));
                        }
                        if(temporal && !piece_flat && !interval_reuse && !scheduled->curves && !scheduled->float_curves) {
                            if(!temporal->attempted(hypothesis,sample,h.phase_lower,target_chip,observed)) {
                                rx_cpu::Scope cpu_batch(rx_cpu::Stage::template_build,true);
                                temporal->start(h.phase_lower,target_chip,observed);
                                // Bound the source-chip union for C4/C10 as
                                // well as chip15. The exact Plan still rejects
                                // overflow; another bounded batch handles the
                                // rest without dropping any original sample.
                                const auto span=std::max(1.L,std::floor(static_cast<long double>(
                                    detail::ScheduledTemporalTemplates::Tile::maximum_coefficients-18)*code.chip_samples()/h.rate));
                                const auto planned_end=observed+static_cast<std::uint64_t>(std::min<long double>(piece_end-observed,span));
                                auto future=observed;
                                {
                                rx_cpu::Scope cpu_export(rx_cpu::Stage::temporal_export,true);
                                bool r1_exported=false;
                                if(detail::scheduled_r1_export_requested()) {
                                    detail::scheduled_r1_export_note_attempt();
                                    const auto cuts_aligned=[&] {
                                        if(differential_window && differential_window%code.chip_samples())return false;
                                        if(target_drift)for(unsigned k=1;k<drift_sections;++k)
                                            if(detail::drift_boundary(k,code.symbol_samples(),drift_sections)%code.chip_samples())return false;
                                        return true;
                                    };
                                    if(h.rate==1 && detail::scheduled_r1_export_grid(h.origin) &&
                                       planned_end<(std::uint64_t{1}<<31) && cuts_aligned() &&
                                       scheduled_flat(h,observed,planned_end,false)) {
                                        const auto first_source=location.source.add_samples(observed-piece_first);
                                        const detail::ScheduledR1ExportInput input{h.rate,h.origin,symbol_source,
                                            sample,piece_first,observed,planned_end,code.symbol_samples(),code.chip_samples(),
                                            current_index,target_index,first_source,scheduled->anchors[hypothesis],true,true};
                                        const auto proof=detail::scheduled_r1_export_proof(input);
                                        if(proof.eligible) {
                                            const auto exported=detail::scheduled_r1_export_append(*temporal,proof,
                                                sample,observed,planned_end,[&](std::size_t offset) {
                                                    return row && (row->valid[offset/64]&(1ULL<<(offset%64)));
                                                },[&]{cancelled(stop);});
                                            future=exported.end;r1_exported=true;
                                            detail::scheduled_r1_export_note_row(exported.appended);
                                        }
                                    }
                                }
                                if(!r1_exported) {
                                for(;future<planned_end;++future) {
                                    if((future&31U)==0)cancelled(stop);
                                    // Reproduce the ORIGINAL anchored long-double
                                    // expression, subtraction, cast and gain mask.
                                    auto at=source+static_cast<long double>(future-piece_first)*h.rate;
                                    if(at<0)continue;
                                    if(at>=2.L*code.symbol_samples())break;
                                    const bool future_next=at>=code.symbol_samples();
                                    if(future_next!=next)break;
                                    if(future_next)at-=code.symbol_samples();
                                    const auto physical_at=tick.add_signed(static_cast<long double>(future-piece_first)*h.rate);
                                    auto future_gain=SchedHopSourceMap::gain(location,physical_at);
                                    if(scheduled->fhss_shape.enabled)future_gain*=scheduled->fhss_shape.gain(0,
                                        physical_at.sample%(2ULL*config.sample_rate)+physical_at.fraction,config.sample_rate);
                                    if(future_gain==0)continue;
                                    const auto offset=static_cast<std::size_t>(future-sample);
                                    if(row && (row->valid[offset/64]&(1ULL<<(offset%64))))continue;
                                    if(!temporal->append(offset,static_cast<double>(at)))break;
                                }
                                }
                                }
                                temporal->end(future);
                                // Gather may change a private context even on
                                // fallback. Invalidate assumptions, then restore
                                // before ANY original lookup/outer-ring callback.
                                pattern_ready=false;prepared_chip=std::numeric_limits<std::uint64_t>::max();
                                temporal->prepare(pattern,config,stop,target_outer!=nullptr);prepare();
                            }
                            temporal_copied=temporal->raw(hypothesis,sample,h.phase_lower,target_chip,observed,left,
                                static_cast<double>(within),phases);
                        }
                        if(!temporal_copied)phases=shape(within);
                        if(count>=4) {
                            const auto last=shape(within+(count-1)*h.rate);
                            interval_value=phases;interval_first=observed;interval_end=observed+count;interval_index=target_index;
                            for(unsigned bit=0;bit<2;++bit)interval_slope[bit]=(last[bit]-phases[bit])/static_cast<double>(count-1);
                        } else interval_end=0;
                    }
                    // The row retains the exact production limiter and gates.
                    // A common scalar cancels in the two-basis fit; clipping
                    // belongs in both the dot and complete real I/Q Gram.
                    for(auto& phase:phases)phase=gain*pattern_limit_pcm(amplitude*phase)/amplitude;
                    if(row){row->value[left]=phases;row->valid[left/64]|=1ULL<<(left%64);++cache->constructions;}
                    else ++scheduled->template_uncached[hypothesis];
                }
                const auto chip=static_cast<std::uint64_t>(within/code.chip_samples());
                if(target_outer) {
                    const auto consume=[&](std::uint64_t local,double pulse) {
                        target_outer->add(local,gain*pulse*Complex{projection.xc,projection.xs},
                            [&](std::uint64_t position){return pattern.values(target_chip+position);});
                    };
                    const bool private_reused=precomputed && precomputed->outer_pulses(observed,within,consume);
                    const bool reused=private_reused || (temporal_copied && temporal && temporal->outer_pulses(hypothesis,sample,
                        h.phase_lower,target_chip,observed,left,static_cast<double>(within),consume));
                    if(!reused)pattern_pulse_each(static_cast<double>(within),code.symbol_samples(),code.chip_samples(),consume);
                }
                rx_cpu::Scope cpu_direct_gram(rx_cpu::Stage::gram,true);
                for(unsigned bit=0;bit<2;++bit) {
                    Fit contribution;contribution.add(projection,phases[bit],1);
                    const auto add=[&](Fit& value) {
                        value.xc+=contribution.xc;value.xs+=contribution.xs;value.cc+=contribution.cc;
                        value.ss+=contribution.ss;value.cs+=contribution.cs;value.energy+=contribution.energy;value.count+=contribution.count;
                    };
                    add(target_fit[bit]);
                    if(target_chips)(*target_chips)[bit].add_fit(contribution,chip);
                    if(target_drift)add((*target_drift)[bit].active);
                    if(target_differential)add((*target_differential)[bit].active);
                }
            }
        }
        return limit;
    }
    std::uint64_t accumulate(Hypothesis& h,std::size_t hypothesis,std::uint64_t cursor,
                             std::uint64_t end,PatternCode& pattern,std::stop_token stop) {
        if(scheduled_map())return accumulate_scheduled(h,hypothesis,cursor,end,pattern,stop);
        if(cursor<end && !affine_coefficients.empty() && h.affine_groups) {
            const auto base=hypothesis*(alternate_groups+1);
            bool covered=true;
            for(std::size_t group=0;group<h.affine_groups;++group) {
                const auto& cached=affine_coefficients[base+group];
                covered&=cached.first<=cursor && end<=cached.end;
            }
            if(covered) {
                // Every active group's retained range lies inside the same
                // chip, pulse interval, detector section/window and symbol.
                // The miss path already initialized their active fits. No
                // clock/phase/boundary calculation or state advance is needed
                // until an observation reaches the end of one of those ranges.
                if(!h.fits[0].count)h.observed_start=cursor;
                const auto& bank=banks[h.frequency];
                for(std::size_t group=0;group<h.affine_groups;++group) {
                    const auto& cached=affine_coefficients[base+group];
                    std::array<Complex,2> value{};
                    {
                        WorkTimer timer(options.measure_affine_work,work.affine_prepare_seconds,
                            work.affine_prepare_cpu_seconds);
                        const auto distance=static_cast<double>(cursor-cached.first);
                        for(unsigned bit=0;bit<2;++bit)value[bit]=cached.value[bit]+distance*cached.slope[bit];
                        if(options.measure_affine_work)++work.affine_reuses;
                    }
                    fit_affine_piece(bank,static_cast<std::size_t>(cursor-sample),end-cursor,
                        value,cached.slope,fits(h,hypothesis,group),
                        drift_fits.empty()?nullptr:&section_fits(hypothesis,group),
                        differential_window?&local_fits(hypothesis,group):nullptr);
                    ++affine_counts[hypothesis];
                }
                if(options.measure_affine_work)++work.affine_interval_fast_paths;
                return end;
            }
        }
        const auto symbol_start=this->symbol_start(h);
        const auto symbol_end=clock_boundary(h,code.symbol_samples());
        const auto segment_end=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(symbol_end)));
        std::size_t group_count=0;
        const auto groups=phase_groups(h,group_count);
        // Only the shortcut count is narrow. The full loop below always visits
        // every phase. A large bank bypasses continuation without truncation
        // or enlarging every ordinary hypothesis in tiny workspaces.
        h.affine_groups=group_count<=std::numeric_limits<std::uint8_t>::max()?
            static_cast<std::uint8_t>(group_count):0;
        if(!h.fits[0].count)h.observed_start=cursor;
        for(std::size_t group=0;group<group_count;++group) {
            if(affine_coefficients.empty()) {
                pattern.set_stream_phase_samples(groups[group].lower);
                require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
                    "pattern chip coordinate overflow");
                pattern.prepare_symbol(h.index*code.chips_per_symbol(),stop);
            }
            auto& fit=fits(h,hypothesis,group);
            auto* drift=!drift_fits.empty()?&section_fits(hypothesis,group):nullptr;
            auto* differential=differential_window?&local_fits(hypothesis,group):nullptr;
            auto observed=cursor;
            while(observed<segment_end) {
                const auto within=std::max(0.L,(static_cast<long double>(observed)-symbol_start)*h.rate);
                auto section_end=symbol_end;
                if(drift) {
                    for(auto& item:*drift)advance_drift(item,h,observed);
                    section_end=clock_boundary(h,detail::drift_boundary(
                        (*drift)[0].section+1,code.symbol_samples(),drift_sections));
                }
                if(differential) {
                    for(auto& item:*differential)advance_differential(item,h,observed);
                    const auto boundary=(*differential)[0].window_index<code.symbol_samples()/differential_window?
                        ((*differential)[0].window_index+1)*differential_window:code.symbol_samples();
                    section_end=std::min(section_end,clock_boundary(h,boundary));
                }
                if(shaped) {
                    require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
                            "pattern chip coordinate overflow");
                    const auto first_chip=h.index*code.chips_per_symbol();
                    const auto left=static_cast<std::size_t>(observed-sample);
                    const auto& bank=banks[h.frequency];
                    if(pulse_segmented) {
                        const auto chip=code.chip_samples(),symbol=code.symbol_samples();
                        const auto local=static_cast<std::uint64_t>(std::floor(within/chip));
                        const auto chip_end=clock_boundary(h,local<symbol/chip?(local+1)*chip:symbol);
                        const auto boundary=std::min(static_cast<long double>(segment_end),
                            std::ceil(std::min(chip_end,section_end)));
                        const auto limit=static_cast<std::uint64_t>(
                            std::max(static_cast<long double>(observed+1),boundary))-observed;
                        const auto offset=(within-static_cast<long double>(local)*chip)/h.rate;
                        const auto duration=static_cast<long double>(chip)/h.rate;
                        std::array<Complex,2> value{},slope{};
                        std::array<double,detail::correlation_pulse_atoms> pulse_value{},pulse_slope{};
                        std::uint64_t piece_count=0;
                        {
                            WorkTimer timer(options.measure_affine_work,work.affine_prepare_seconds,
                                work.affine_prepare_cpu_seconds);
                            auto* cached=affine_coefficients.empty()?nullptr:
                                &affine_coefficients[hypothesis*(alternate_groups+1)+group];
                            if(cached && observed>=cached->first && observed<cached->end) {
                                const auto distance=static_cast<double>(observed-cached->first);
                                for(unsigned bit=0;bit<2;++bit) {
                                    value[bit]=cached->value[bit]+distance*cached->slope[bit];
                                    slope[bit]=cached->slope[bit];
                                }
                                piece_count=std::min(limit,cached->end-observed);
                                if(options.measure_affine_work)++work.affine_reuses;
                            } else {
                                // Caller and oscillator block ends do not end a
                                // waveform affine interval. The retained range
                                // is bounded by the actual pulse and detector
                                // geometry, including a shifted partial tail.
                                const auto natural_end=std::min(
                                    static_cast<long double>(std::numeric_limits<std::uint64_t>::max()),
                                    std::ceil(std::min(chip_end,section_end)));
                                const auto natural_limit=cached?static_cast<std::uint64_t>(
                                    std::max(static_cast<long double>(observed+1),natural_end))-observed:limit;
                                auto piece=detail::correlation_pulse_segment(offset,natural_limit,duration);
                                const auto tail=symbol%chip;
                                detail::CorrelationPulseSegment final_piece;
                                const auto final_chip=code.chips_per_symbol()-1;
                                const bool has_final=tail && final_chip>=local && final_chip-local<=8;
                                if(has_final) {
                                    // The final pulse center and amplitude use
                                    // its actual partial-chip duration.
                                    final_piece=detail::correlation_pulse_segment(
                                        offset+static_cast<long double>(chip-tail)/(2*h.rate),natural_limit,duration);
                                    piece.count=std::min(piece.count,final_piece.count);
                                }
                                if(cached) {
                                    pattern.set_stream_phase_samples(groups[group].lower);
                                    pattern.prepare_symbol(first_chip,stop);
                                }
                                for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j) {
                                    if(j<8 && local<8-j)continue;
                                    if(j>8 && local>std::numeric_limits<std::uint64_t>::max()-(j-8))continue;
                                    const auto position=j<8?local-(8-j):local+(j-8);
                                    if(position>=code.chips_per_symbol())continue;
                                    auto a=piece.value[j],b=piece.slope[j];
                                    if(has_final && position==final_chip) {
                                        const auto scale=std::sqrt(static_cast<double>(tail)/static_cast<double>(chip));
                                        a=final_piece.value[j]*scale;b=final_piece.slope[j]*scale;
                                    }
                                    if(outer_presence){pulse_value[j]=static_cast<double>(a);pulse_slope[j]=static_cast<double>(b);}
                                    if(a==0 && b==0)continue;
                                    require(position<=std::numeric_limits<std::uint64_t>::max()-first_chip,
                                            "pattern chip address would overflow");
                                    const auto pair=pattern.values(first_chip+position);
                                    for(unsigned bit=0;bit<2;++bit) {
                                        value[bit]+=static_cast<double>(a)*pair[bit];
                                        slope[bit]+=static_cast<double>(b)*pair[bit];
                                    }
                                }
                                piece_count=std::min(limit,piece.count);
                                if(cached) {
                                    cached->value=value;cached->slope=slope;
                                    cached->first=observed;cached->end=observed+piece.count;
                                }
                                if(options.measure_affine_work)++work.affine_preparations;
                            }
                        }
                        if(outer_presence) {
                            const auto right=left+static_cast<std::size_t>(piece_count);
                            const auto p=bank.observations(physical_view(bank))[right]-bank.observations(physical_view(bank))[left];
                            const Complex measured{p.xc,p.xs};
                            const auto moment=bank.moments(physical_view(bank))[right]-bank.moments(physical_view(bank))[left]-static_cast<double>(left)*measured;
                            for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j) {
                                if(j<8 && local<8-j)continue;
                                const auto position=j<8?local-(8-j):local+(j-8);
                                if(position>=code.chips_per_symbol())continue;
                                (*outer_evidence)[hypothesis*(alternate_groups+1)+group].add(position,
                                    pulse_value[j]*measured+pulse_slope[j]*moment,
                                    [&](std::uint64_t pulse){return pattern.values(first_chip+pulse);});
                            }
                        }
                        fit_affine_piece(bank,left,piece_count,value,slope,fit,drift,differential,
                            outer_presence?&chip_evidence[hypothesis*(alternate_groups+1)+group]:nullptr,local);
                        ++affine_counts[hypothesis];observed+=piece_count;continue;
                    }
                    const auto projection=bank.observations(physical_view(bank))[left+1]-bank.observations(physical_view(bank))[left];
                    // Each alternative schedule fits the same disjoint
                    // raw observations. Its score never borrows samples
                    // or evidence from another possible schedule.
                    if(outer_presence)pattern_pulse_each(static_cast<double>(within),code.symbol_samples(),code.chip_samples(),
                        [&](std::uint64_t local,double pulse) {
                            (*outer_evidence)[hypothesis*(alternate_groups+1)+group].add(local,pulse*Complex{projection.xc,projection.xs},
                                [&](std::uint64_t position){return pattern.values(first_chip+position);});
                        });
                    const auto phases=pattern.shaped_values(first_chip,static_cast<double>(within));
                    for(unsigned bit=0;bit<2;++bit) {
                        const auto phase=phases[bit];
                        fit[bit].add(projection,phase,1);
                        if(!chip_evidence.empty())chip_evidence[hypothesis*(alternate_groups+1)+group][bit].add(
                            projection,phase,1,detail::CorrelationChipEvidence::index(observed,symbol_start,h.rate,code.chip_samples()));
                        if(drift)(*drift)[bit].active.add(projection,phase,1);
                        if(differential)(*differential)[bit].active.add(projection,phase,1);
                    }
                    ++observed;continue;
                }
                const auto local=static_cast<std::uint64_t>(std::floor(within/code.chip_samples()));
                require(h.index<=(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol(),"pattern chip coordinate overflow");
                const auto chip=h.index*code.chips_per_symbol()+local;
                const auto fraction=std::clamp(static_cast<double>(within/code.chip_samples()-local),0.,std::nextafter(1.,0.));
                const auto chip_end=clock_boundary(h,(local+1)*code.chip_samples());
                const auto boundary=std::min(static_cast<long double>(segment_end),std::ceil(std::min(chip_end,section_end)));
                const auto until=static_cast<std::uint64_t>(std::max(static_cast<long double>(observed+1),boundary));
                const auto left=static_cast<std::size_t>(observed-sample),right=static_cast<std::size_t>(until-sample);
                if(outer_presence) {
                    const auto p=banks[h.frequency].observations(physical_view(h.frequency))[right]-banks[h.frequency].observations(physical_view(h.frequency))[left];
                    (*outer_evidence)[hypothesis*(alternate_groups+1)+group].add(local,{p.xc,p.xs},
                        [&](std::uint64_t position){return pattern.values(h.index*code.chips_per_symbol()+position);});
                }
                for(unsigned bit=0;bit<2;++bit) {
                    const auto bank_index=config.spreading_mode==SpreadingMode::tone?h.tone_bank_base+bit:h.frequency;
                    const auto& bank=banks[bank_index];
                    auto phase=pattern.value(chip,bit,fraction);
                    if(config.spreading_mode==SpreadingMode::tone) {
                        const auto tone=bank.frequency-config.carrier_hz-search.frequency_offsets_hz[h.frequency];
                        phase*=std::polar(1.,-static_cast<double>(std::remainder(static_cast<long double>(observed)*tau*tone/config.sample_rate,static_cast<long double>(tau))));
                    }
                    const auto projection=bank.observations(physical_view(bank))[right]-bank.observations(physical_view(bank))[left];
                    fit[bit].add(projection,phase,right-left);
                    if(!chip_evidence.empty())chip_evidence[hypothesis*(alternate_groups+1)+group][bit].add(projection,phase,right-left,local);
                    if(drift)(*drift)[bit].active.add(projection,phase,right-left);
                    if(differential)(*differential)[bit].active.add(projection,phase,right-left);
                }
                observed=until;
            }
        }
        return segment_end;
    }
    void record_point(const Projection& measured) {
        const Complex point{measured.xc,measured.xs};
        const auto delta=std::abs(previous_point)>1e-20?point*std::conj(previous_point)/std::abs(previous_point):point;
        if(point_count==points.size()){point_begin=(point_begin+1)%points.size();--point_count;}
        points[(point_begin+point_count++)%points.size()]=delta;previous_point=point;
    }
    void finish_pulse_cell(PulseLattice& lattice,std::stop_token stop) {
        auto& cell=lattice.pending;cell.end=lattice.next;
        {
            WorkTimer timer(options.measure_work,work.kernel_seconds,work.kernel_cpu_seconds);
            cell.gram=lattice.kernel.evaluate(lattice.first_offset,cell.count,
                lattice.carrier_square,lattice.carrier_norm,stop);
        }
        require(lattice.cells.size()<lattice.cells.capacity(),"pulse projection exceeds bounded cell tile");
        lattice.cells.push_back(cell);++work.cells;
        require(cell.chip<std::numeric_limits<std::int64_t>::max(),"pulse cell coordinate overflow");
        const auto next_chip=cell.chip+1;cell={};cell.chip=next_chip;
        require(next_chip<std::numeric_limits<std::int64_t>::max(),"pulse cell coordinate overflow");
        lattice.start=lattice.anchor+static_cast<long double>(next_chip)*code.chip_samples()/lattice.rate;
        lattice.next=static_cast<std::uint64_t>(std::ceil(lattice.anchor+
            static_cast<long double>(next_chip+1)*code.chip_samples()/lattice.rate));
    }
    void project_pulse_moments(std::size_t count,std::stop_token stop) {
        for(auto& lattice:pulse_lattices) {
            cancelled(stop);lattice.cells.clear();
            const auto& bank=banks[lattice.frequency];
            const auto prefix=bank.observations(physical_view(bank));const auto moments=bank.moments(physical_view(bank));
            const auto duration=static_cast<long double>(code.chip_samples())/lattice.rate;
            for(std::size_t i=0;i<count;) {
                const auto observed=sample+i;auto& cell=lattice.pending;
                if(!cell.count) {
                    cell.first=observed;lattice.first_offset=static_cast<long double>(observed)-lattice.start;
                    const auto projection=prefix[i+1]-prefix[i];
                    lattice.carrier_square={projection.cc-projection.ss,2*projection.cs};
                    lattice.carrier_norm=projection.cc+projection.ss;
                }
                if(!lattice.segment_observed) {
                    lattice.segment=detail::correlation_pulse_segment(
                        static_cast<long double>(observed)-lattice.start,lattice.next-observed,duration);
                    lattice.segment_first=observed;lattice.segment_sum={};lattice.segment_moment={};
                }
                const auto take=static_cast<std::size_t>(std::min<std::uint64_t>(
                    count-i,lattice.segment.count-lattice.segment_observed));
                require(take>0,"empty pulse moment segment");
                const auto projection=prefix[i+take]-prefix[i];
                const std::complex<long double> measured{projection.xc,projection.xs};
                const auto moment=moments[i+take]-moments[i];
                lattice.segment_sum+=measured;
                // Prefix moments use small, block-local coordinates. Shift to
                // the current pulse piece, never to an unbounded stream time.
                lattice.segment_moment+=std::complex<long double>{moment.real(),moment.imag()}+
                    (static_cast<long double>(sample)-lattice.segment_first)*measured;
                lattice.segment_observed+=take;cell.count+=take;cell.energy+=projection.energy;i+=take;
                if(lattice.segment_observed==lattice.segment.count) {
                    for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j) {
                        const auto dot=lattice.segment.value[j]*lattice.segment_sum+
                            lattice.segment.slope[j]*lattice.segment_moment;
                        cell.dot[j]+=Complex{static_cast<double>(dot.real()),static_cast<double>(dot.imag())};
                    }
                    lattice.segment_observed=0;++work.segments;
                }
                if(sample+i==lattice.next) {
                    require(!lattice.segment_observed,"pulse moment crossed cell boundary");
                    finish_pulse_cell(lattice,stop);
                }
            }
        }
    }
    void project_pulse_cells(std::size_t count,std::stop_token stop) {
        if(work.backend==PatternCorrelationBackend::pulse_moments){project_pulse_moments(count,stop);return;}
        const auto chip=code.chip_samples();
        for(auto& lattice:pulse_lattices) {
            cancelled(stop);lattice.cells.clear();
            const auto prefix=banks[lattice.frequency].observations(physical_view(lattice.frequency));
            for(std::size_t i=0;i<count;++i) {
                const auto observed=sample+i;
                auto& cell=lattice.pending;
                const auto projection=prefix[i+1]-prefix[i];
                if(!cell.count) {
                    cell.first=observed;lattice.first_offset=static_cast<long double>(observed)-lattice.start;
                    lattice.carrier_square={projection.cc-projection.ss,2*projection.cs};
                    lattice.carrier_norm=projection.cc+projection.ss;
                }
                const auto q=(static_cast<long double>(observed)-lattice.start)*lattice.rate/chip-.5L;
                const Complex measured{projection.xc,projection.xs};
                for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j)
                    cell.dot[j]+=pattern_pulse(static_cast<double>(q+8-static_cast<long double>(j)))*measured;
                cell.energy+=projection.energy;++cell.count;
                if(observed+1==lattice.next) {
                    finish_pulse_cell(lattice,stop);
                }
            }
        }
    }
    static void add_fit(Fit& target,const Fit& value) {
        target.xc+=value.xc;target.xs+=value.xs;target.cc+=value.cc;target.ss+=value.ss;
        target.cs+=value.cs;target.energy+=value.energy;target.count+=value.count;
    }
    void accumulate_pulse_cells(std::stop_token stop) {
        const auto chips=code.chips_per_symbol();
        for(std::size_t hypothesis=0;hypothesis<hypotheses.size();++hypothesis) {
            cancelled(stop);auto& h=hypotheses[hypothesis];
            const auto& address=pulse_addresses[hypothesis];
            const auto& lattice=pulse_lattices[address.lattice];
            for(const auto& cell:lattice.cells) {
                require(h.index<=(static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())-
                    static_cast<std::uint64_t>(address.shift))/chips,"pattern chip coordinate overflow");
                const auto first=address.shift+static_cast<std::int64_t>(h.index*chips);
                if(cell.chip<first)continue;
                require(cell.chip-first<static_cast<std::int64_t>(chips),"pulse projection skipped a symbol endpoint");
                const auto local=cell.chip-first;
                if(!h.fits[0].count)h.observed_start=cell.first;
                std::size_t group_count=0;const auto groups=phase_groups(h,group_count);
                for(std::size_t group=0;group<group_count;++group) {
                    code.set_stream_phase_samples(groups[group].lower);
                    code.prepare_symbol(h.index*chips,stop);
                    std::array<std::array<Complex,detail::correlation_pulse_atoms>,2> coefficients{};
                    for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j) {
                        const auto position=local+static_cast<std::int64_t>(j)-8;
                        if(position<0 || position>=static_cast<std::int64_t>(chips))continue;
                        const auto values=code.values(h.index*chips+static_cast<std::uint64_t>(position));
                        for(unsigned bit=0;bit<2;++bit)coefficients[bit][j]=values[bit];
                    }
                    auto& fit=fits(h,hypothesis,group);
                    auto* drift=!drift_fits.empty()?&section_fits(hypothesis,group):nullptr;
                    auto* differential=differential_window?&local_fits(hypothesis,group):nullptr;
                    if(outer_presence)for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j) {
                        const auto position=local+static_cast<std::int64_t>(j)-8;
                        if(position<0 || position>=static_cast<std::int64_t>(chips))continue;
                        (*outer_evidence)[hypothesis*(alternate_groups+1)+group].add(static_cast<std::uint64_t>(position),cell.dot[j],
                            [&](std::uint64_t pulse){return code.values(h.index*chips+pulse);});
                    }
                    for(unsigned bit=0;bit<2;++bit) {
                        const auto contribution=cell.fit(coefficients[bit]);add_fit(fit[bit],contribution);
                        if(!chip_evidence.empty())chip_evidence[hypothesis*(alternate_groups+1)+group][bit].add_fit(
                            contribution,static_cast<std::uint64_t>(local));
                        if(drift) {
                            advance_drift((*drift)[bit],h,cell.first);
                            add_fit((*drift)[bit].active,contribution);
                        }
                        if(differential) {
                            advance_differential((*differential)[bit],h,cell.first);
                            add_fit((*differential)[bit].active,contribution);
                        }
                    }
                }
                if(local+1==static_cast<std::int64_t>(chips))complete(h,hypothesis,cell.end,stop);
            }
        }
    }
    std::size_t process_batch(std::span<const float> input,std::stop_token stop) {
        // Preserve the scalar path as a reference, and never collect samples
        // across a caller's progress poll. Small/tight-workspace pushes retain
        // the original one-block path. No completion is allowed in this batch.
        // The compact lane ABI has three phase fits; UTC gated coefficients
        // can distinguish every original phase point. Scalar/pulse execution
        // retains those points with workspace-charged fit arrays.
        if(scheduled_map() || (config.fake_hopping&&config.fake_hopping->matched_gaps))return 0;
        if(!pulse_lattices.empty() || !search.hypotheses.empty() || worker_codes.size()<2 || input.size()<=block_samples)return 0;
        constexpr std::size_t max_blocks=64,max_lanes=65536;
        using Lane=detail::CorrelationLane;
        using Block=detail::CorrelationBlock;
        const auto retained=sizeof(PatternCorrelator)+working_bytes()+unallocated_history_bytes()+drift_reserved;
        if(retained>=budget)return 0;
        const auto spare=budget-retained;
        const auto per_block=banks.size()*(block_samples+1)*sizeof(Projection)+sizeof(Block);
        const auto frequencies_bytes=banks.size()*sizeof(double);
        const auto minimum_lanes=std::min<std::size_t>(64,hypotheses.size());
        const auto minimum_bytes=frequencies_bytes+minimum_lanes*sizeof(Lane);
        if(spare<=minimum_bytes || (spare-minimum_bytes)/per_block<2)return 0;
        const auto block_capacity=std::min(max_blocks,(spare-minimum_bytes)/per_block);
        const auto proposed=std::min(input.size(),block_capacity*block_samples);
        auto boundary=std::numeric_limits<long double>::infinity();
        for(const auto& h:hypotheses) {
            const auto end=std::ceil(h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate);
            boundary=std::min(boundary,end);
        }
        std::size_t consumed=0,block_count=0;
        while(consumed<proposed) {
            const auto count=std::min(block_samples,proposed-consumed);
            if(static_cast<long double>(sample+consumed+count)>=boundary)break;
            consumed+=count;++block_count;
        }
        if(block_count<2)return 0;
        const auto projection_count=block_count*banks.size()*(block_samples+1);
        const auto fixed_bytes=projection_count*sizeof(Projection)+block_count*sizeof(Block)+frequencies_bytes;
        const auto lane_capacity=std::min({max_lanes,hypotheses.size(),(spare-fixed_bytes)/sizeof(Lane)});
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,retained+fixed_bytes+lane_capacity*sizeof(Lane));
        // Exact-size temporary arrays make the peak reservation independent of
        // vector growth policy. They disappear before ordered completion can
        // grow payloads; idle footprints and receiver-bank admission stay intact.
        auto projections=std::make_unique<Projection[]>(projection_count);
        auto blocks=std::make_unique<Block[]>(block_count);
        auto frequencies=std::make_unique<double[]>(banks.size());
        auto lanes=std::make_unique<Lane[]>(lane_capacity);
        WorkTimer frontend_timer(options.measure_work,work.frontend_seconds,work.frontend_cpu_seconds);
        for(std::size_t f=0;f<banks.size();++f)frequencies[f]=banks[f].frequency;
        std::size_t offset=0,row_offset=0;
        for(std::size_t b=0;b<block_count;++b) {
            cancelled(stop);
            const auto count=std::min(block_samples,consumed-offset);
            blocks[b]={sample+offset,count,row_offset};
            for(std::size_t f=0;f<banks.size();++f) {
                auto prefix=std::span(projections.get()+row_offset+f*(count+1),count+1);
                prefix[0]={};
                auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(sample+offset)*tau*
                    banks[f].frequency/config.sample_rate,static_cast<long double>(tau))));
                const auto step=std::polar(1.,tau*banks[f].frequency/config.sample_rate);
                for(std::size_t i=0;i<count;++i) {
                    const auto c=oscillator.real(),s=oscillator.imag(),x=static_cast<double>(input[offset+i]);
                    auto p=prefix[i];p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
                    prefix[i+1]=p;oscillator*=step;
                }
            }
            row_offset+=banks.size()*(count+1);offset+=count;
        }
        frontend_timer.finish();
        WorkTimer search_timer(options.measure_work,work.search_seconds,work.search_cpu_seconds);
        detail::CorrelationBatch batch{
            {config.stream_epoch,code.symbol_samples(),code.chip_samples(),code.chips_per_symbol(),phase_step,
             config.sample_rate,search.frequency_offsets_hz.size(),config.carrier_hz,shaped,
             config.spreading_mode==SpreadingMode::tone,
             {static_cast<std::uint32_t>(config.spreading_mode),config.scramble,config.dsss,
              config.spreading_seed,config.dsss_seed,config.dsss_factor,config.outer_dsss_version,legacy_public_pattern(config)},drift_fits.empty()?1:drift_sections,differential_window,!chip_evidence.empty(),outer_presence},
            {blocks.get(),block_count},{projections.get(),row_offset},{frequencies.get(),banks.size()},search.frequency_offsets_hz};
        // Numeric tiles contain no PatternBurst, heap-owned input, or references
        // to peer admission state. Device implementations can operate on these
        // same indexed lanes without creating one host thread per hypothesis.
        for(std::size_t first=0;first<hypotheses.size();) {
            const auto count=std::min(lane_capacity,hypotheses.size()-first);
            for(std::size_t i=0;i<count;++i) {
                const auto& h=hypotheses[first+i];auto& lane=lanes[i];
                lane.origin=h.origin;lane.rate=h.rate;lane.index=h.index;lane.observed_start=h.observed_start;
                lane.phase_lower=h.phase_lower;lane.phase_upper=h.phase_upper;
                lane.frequency=h.frequency;lane.rate_index=h.rate_index;lane.tone_bank_base=h.tone_bank_base;lane.fits[0]=h.fits;
                for(std::size_t group=0;group<alternate_groups;++group)
                    lane.fits[group+1]=alternate_fits[(first+i)*alternate_groups+group];
                if(!chip_evidence.empty())for(std::size_t group=0;group<=alternate_groups;++group)
                    lane.chip_evidence[group]=chip_evidence[(first+i)*(alternate_groups+1)+group];
                if(outer_presence)for(std::size_t group=0;group<=alternate_groups;++group)
                    lane.outer_evidence[group]=(*outer_evidence)[(first+i)*(alternate_groups+1)+group];
                if(!drift_fits.empty())for(std::size_t group=0;group<=alternate_groups;++group)
                    lane.drift_fits[group]=section_fits(first+i,group);
                if(differential_window)for(std::size_t group=0;group<=alternate_groups;++group)
                    lane.differential_fits[group]=local_fits(first+i,group);
            }
            detail::accumulate_correlator_cpu(batch,{lanes.get(),count},worker_codes,stop);
            for(std::size_t i=0;i<count;++i) {
                auto& h=hypotheses[first+i];const auto& lane=lanes[i];
                h.observed_start=lane.observed_start;h.fits=lane.fits[0];
                for(std::size_t group=0;group<alternate_groups;++group)
                    alternate_fits[(first+i)*alternate_groups+group]=lane.fits[group+1];
                if(!chip_evidence.empty())for(std::size_t group=0;group<=alternate_groups;++group)
                    chip_evidence[(first+i)*(alternate_groups+1)+group]=lane.chip_evidence[group];
                if(outer_presence)for(std::size_t group=0;group<=alternate_groups;++group)
                    (*outer_evidence)[(first+i)*(alternate_groups+1)+group]=lane.outer_evidence[group];
                if(!drift_fits.empty())for(std::size_t group=0;group<=alternate_groups;++group)
                    section_fits(first+i,group)=lane.drift_fits[group];
                if(differential_window)for(std::size_t group=0;group<=alternate_groups;++group)
                    local_fits(first+i,group)=lane.differential_fits[group];
            }
            first+=count;
        }
        for(std::size_t b=0;b<block_count;++b)
            record_point(projections[blocks[b].projection_offset+blocks[b].count]);
        const auto& last=blocks[block_count-1];
        for(std::size_t f=0;f<banks.size();++f)
            std::copy_n(projections.get()+last.projection_offset+f*(last.count+1),last.count+1,banks[f].prefix.begin());
        sample+=consumed;work.samples+=consumed;
        return consumed;
    }
    void process(std::span<const float> input,std::stop_token stop,detail::CorrelationProjectionCache* cache) {
        rx_cpu::Scope cpu_search(rx_cpu::Stage::search_residual);
        rx_cpu::Scope cpu_frontend(rx_cpu::Stage::mixing);
        struct ClearSharedViews {
            Impl& state;bool enabled=true;
            ~ClearSharedViews(){if(enabled) {
                for(auto& bank:state.banks){bank.shared_prefix={};bank.shared_moment={};}
                state.clear_physical_views();
            }}
        } clear_shared{*this};
        WorkTimer frontend_timer(options.measure_work,work.frontend_seconds,work.frontend_cpu_seconds);
        bool certified_null=scheduled_map()!=nullptr;
        if(certified_null)for(const auto& h:hypotheses) {
            const auto first=scheduled_tick(h,sample).add_signed(-scheduled_uncertainty(h,sample));
            const auto last=scheduled_tick(h,sample+input.size()).add_samples(scheduled_cell_width(h)).add_signed(scheduled_uncertainty(h,sample+input.size()));
            if(scheduled_map()->locate(first).source.distance(scheduled_map()->locate(last).source)!=0) {certified_null=false;break;}
        }
        if(certified_null)work.scheduled_null_samples+=input.size();
        clear_shared.enabled=!certified_null;
        if(!certified_null)for(std::size_t b=0;b<banks.size();++b) {
            cancelled(stop);auto& bank=banks[b];bank.shared_prefix={};bank.shared_moment={};
            if(scheduled && !scheduled->physical_views.empty())scheduled->physical_views[b]={};
            if(cache && scheduled && scheduled->moments_eligible && (!outer_presence || scheduled->outer_moment_scratch) &&
               !options.raw_reference && !options.scheduled_moment_reference) {
                require(sample<=std::numeric_limits<std::uint64_t>::max()-search.input_sample_origin,
                    "shared physical capture coordinate overflow");
                rx_cpu::Scope shared_cpu(rx_cpu::Stage::shared_frontend);
                const auto shared=cache->physical(input,search.input_sample_origin+sample,sample,config.sample_rate,bank.frequency,stop);
                if(shared){scheduled->physical_views[b]=shared;continue;}
            }
            if(cache && (scheduled_map() || work.backend==PatternCorrelationBackend::pulse_moments || pulse_segmented)) {
                const auto shared=cache->get(input,sample,config.sample_rate,bank.frequency,block_samples,!scheduled_map() || scheduled_affine,stop);
                if(shared){bank.shared_prefix=shared.prefix;bank.shared_moment=shared.first_moment;continue;}
            }
            bank.prefix[0]={};
            if(!bank.first_moment.empty())bank.first_moment[0]={};
            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(sample)*tau*bank.frequency/config.sample_rate,static_cast<long double>(tau))));
            const auto step=std::polar(1.,tau*bank.frequency/config.sample_rate);
            for(std::size_t i=0;i<input.size();++i) {
                const auto c=oscillator.real(),s=oscillator.imag(),x=static_cast<double>(input[i]);
                auto p=bank.prefix[i];p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
                if(!bank.first_moment.empty())bank.first_moment[i+1]=bank.first_moment[i]+static_cast<double>(i)*Complex{x*c,x*s};
                bank.prefix[i+1]=p;oscillator*=step;
            }
        }
        if(!certified_null && scheduled && !scheduled->moments.empty())for(std::size_t b=0;b<banks.size();++b) {
            rx_cpu::Scope cpu_moments(rx_cpu::Stage::physical_moments);
            if(physical_view(b))continue;
            auto& out=scheduled->moments[b];out[0]={};
            const auto prefix=banks[b].observations(physical_view(b));
            for(std::size_t i=0;i<input.size();++i) {
                const auto p=prefix[i+1]-prefix[i];const auto n=static_cast<double>(i),n2=n*n;
                const Complex square{p.cc-p.ss,2*p.cs};auto next=out[i];
                next.signal1+=n*Complex{p.xc,p.xs};next.square1+=n*square;next.square2+=n2*square;
                next.norm1+=n*(p.cc+p.ss);next.norm2+=n2*(p.cc+p.ss);out[i+1]=next;
            }
        }
        if(!certified_null && scheduled && !scheduled->float_signal.empty()) {
            for(std::size_t b=0;b<banks.size();++b) {
                cancelled(stop);const auto prefix=banks[b].observations(physical_view(b));
                ++scheduled->extra_work.scheduled_float_moment_preparations;
                const auto status=scheduled->float_signal[b].prepare(input.size(),[&](std::size_t i) {
                    const auto p=prefix[i+1]-prefix[i];return Complex{p.xc,p.xs};
                });
                if(status==detail::scheduled_float_signal::Failure::none)
                    scheduled->extra_work.scheduled_float_moment_prepared_samples+=input.size();
                else ++scheduled->extra_work.scheduled_float_moment_prepare_fallbacks;
            }
        }
        const auto end=sample+input.size();
        work.samples+=input.size();
        if(!pulse_lattices.empty()) {
            const auto cells_before=work.cells;
            {
                WorkTimer statistics_timer(options.measure_work,work.pulse_statistics_seconds,
                    work.pulse_statistics_cpu_seconds);
                project_pulse_cells(input.size(),stop);
            }
            frontend_timer.finish();cpu_frontend.finish();
            WorkTimer search_timer(options.measure_work,work.search_seconds,work.search_cpu_seconds);
            // There is no hypothesis work before a shared chip cell is ready.
            // In particular a high PCM rate must not rescan the timing/key
            // candidates once per tiny oscillator block of an hours-long bit.
            if(work.cells!=cells_before)accumulate_pulse_cells(stop);
            settle_timing();
            record_point(banks.front().observations(physical_view(0))[input.size()]);sample=end;room_for(0);return;
        }
        frontend_timer.finish();cpu_frontend.finish();
        WorkTimer search_timer(options.measure_work,work.search_seconds,work.search_cpu_seconds);
        const auto parallel=worker_codes.size()>1;
        auto private_batch=certified_null?std::unique_ptr<PrivateBatchOwner>{}:prepare_private_batch_owner(input.size());
        const auto* family_plan=scheduled?scheduled->families.get():nullptr;
        const bool accumulation_ahead=parallel || family_plan || private_batch;
        const auto accumulate_one=[&](std::size_t worker,std::size_t hypothesis,
                const detail::ScheduledPrivateBatchView* precomputed=nullptr) {
            detail::PatternPermutationScope permutation_scope(cache?cache->permutations():nullptr,
                cache?cache->permutation_namespace():0);
            auto& view=worker_map_view();const auto previous=view;
            struct RestoreMapView {decltype(view) target;decltype(previous) old;~RestoreMapView(){target=old;}} restore{view,previous};
            if(scheduled && !scheduled->worker_maps.empty())view={this,scheduled->worker_maps[worker].get()};
            auto& template_view=worker_template_view();const auto previous_template=template_view;
            struct RestoreTemplateView {decltype(template_view) target;decltype(previous_template) old;~RestoreTemplateView(){target=old;}} restore_template{template_view,previous_template};
            if(scheduled && !scheduled->worker_template_caches.empty())template_view={this,scheduled->worker_template_caches[worker].get()};
            auto& temporal_view=worker_temporal_view();const auto previous_temporal=temporal_view;
            struct RestoreTemporalView {decltype(temporal_view) target;decltype(previous_temporal) old;~RestoreTemporalView(){target=old;}} restore_temporal{temporal_view,previous_temporal};
            if(scheduled && !scheduled->worker_temporal_templates.empty())temporal_view={this,scheduled->worker_temporal_templates[worker].get()};
            cancelled(stop);auto& h=hypotheses[hypothesis];
            const auto cursor=initial_cursor(h,end);
            const auto symbol_end=clock_boundary(h,code.symbol_samples());
            if(cursor<end && static_cast<long double>(cursor)<symbol_end) {
                if(precomputed)accumulate_scheduled(h,hypothesis,cursor,end,
                    worker_codes.empty()?code:worker_codes[worker],stop,precomputed);
                else accumulate(h,hypothesis,cursor,end,worker_codes.empty()?code:worker_codes[worker],stop);
            }
        };
        if(private_batch) {
            if(family_plan) {
                scheduled->extra_work.scheduled_family_dispatches+=family_plan->ends.size()-1;
                scheduled->extra_work.scheduled_family_lanes+=family_plan->members.size();
            }
            // Preserve preceding common-family execution. Batch independent
            // FAMILY LEADERS, then replay each family's original shared path;
            // rejected families still retain its original individual fallback.
            const auto items=family_plan?family_plan->ends.size()-1:hypotheses.size();
            const auto group=family_plan?PrivateBatchScratch::leader_group(items,private_batch->workers.size(),
                PrivateBatchScratch::gather_policy()):PrivateBatchScratch::job_lanes;
            prepare_private_batch_order(*private_batch,family_plan,end,stop,group);
            const auto original_item=[&](std::size_t rank) {
                return private_batch->order.empty()?rank:private_batch->order[rank].item;
            };
            const auto jobs=(items+group-1)/group;
            detail::parallel_search(jobs,private_batch->workers.size(),[&](std::size_t worker,std::size_t job) {
                detail::PatternPermutationScope permutation_scope(cache?cache->permutations():nullptr,
                    cache?cache->permutation_namespace():0);
                auto& view=worker_map_view();const auto previous=view;
                struct RestoreMapView {decltype(view) target;decltype(previous) old;~RestoreMapView(){target=old;}} restore{view,previous};
                if(!scheduled->worker_maps.empty())view={this,scheduled->worker_maps[worker].get()};
                const auto first=job*group;
                accumulate_private_batch_job(first,std::min(items,first+group),end,
                    worker_codes.empty()?code:worker_codes[worker],*private_batch->workers[worker],
                    [&](std::size_t rank){const auto item=original_item(rank);
                        return family_plan?family_plan->members[family_plan->ends[item]]:item;},
                    [&](std::size_t rank,const detail::ScheduledPrivateBatchView* prepared) {
                        const auto item=original_item(rank);
                        if(!family_plan){accumulate_one(worker,item,prepared);execution::checkpoint();return;}
                        const auto members=std::span<const std::size_t>{family_plan->members}.subspan(
                            family_plan->ends[item],family_plan->ends[item+1]-family_plan->ends[item]);
                        const bool shared=scheduled->common_scratch && accumulate_scheduled_common_shape(
                            members,end,worker_codes.empty()?code:worker_codes[worker],worker,item,stop,prepared);
                        if(!shared)for(auto lane:members) {
                            // The checked view belongs only to the leader's
                            // exact public source coordinates/private address.
                            accumulate_one(worker,lane,lane==members.front()?prepared:nullptr);execution::checkpoint();
                        }
                    },stop,family_plan!=nullptr);
            });
            // All worker callbacks, borrowed source views and cancellation
            // writers joined. Release the transient loan BEFORE serial complete
            // or room_for can recompute accounting or evict optional owners.
            report_private_batch_owner(*private_batch,group);private_batch.reset();
        } else if(family_plan) {
            scheduled->extra_work.scheduled_family_dispatches+=family_plan->ends.size()-1;
            scheduled->extra_work.scheduled_family_lanes+=family_plan->members.size();
            detail::parallel_search(family_plan->ends.size()-1,std::max<std::size_t>(1,worker_codes.size()),
                [&](std::size_t worker,std::size_t family) {
                    detail::PatternPermutationScope permutation_scope(cache?cache->permutations():nullptr,
                        cache?cache->permutation_namespace():0);
                    const auto members=std::span<const std::size_t>{family_plan->members}.subspan(
                        family_plan->ends[family],family_plan->ends[family+1]-family_plan->ends[family]);
                    bool shared=false;
                    if(scheduled->common_scratch) {
                        auto& view=worker_map_view();const auto previous=view;
                        struct RestoreMapView {decltype(view) target;decltype(previous) old;~RestoreMapView(){target=old;}} restore{view,previous};
                        if(!scheduled->worker_maps.empty())view={this,scheduled->worker_maps[worker].get()};
                        shared=accumulate_scheduled_common_shape(members,end,worker_codes.empty()?code:worker_codes[worker],worker,family,stop);
                    }
                    if(!shared)for(auto lane:members) {
                        accumulate_one(worker,lane);execution::checkpoint();
                    }
                });
        } else if(parallel)detail::parallel_search(hypotheses.size(),worker_codes.size(),
            [&](std::size_t worker,std::size_t lane){accumulate_one(worker,lane);});
        // Serial completions may evict optional family scratch; collect after
        // all workers join and before any completion can release it.
        collect_scheduled_common_shape();
        for(std::size_t hypothesis=0;hypothesis<hypotheses.size();++hypothesis) {
            auto& h=hypotheses[hypothesis];
            cancelled(stop);auto cursor=initial_cursor(h,end);
            // Only independent fit accumulation was moved ahead. Trial counts,
            // phase selection, peer ownership and every publication retain the
            // original serial hypothesis/complete-symbol order.
            if(accumulation_ahead && cursor<end) {
                const auto symbol_end=clock_boundary(h,code.symbol_samples());
                if(static_cast<long double>(cursor)<symbol_end) {
                    cursor=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(symbol_end)));
                    if(static_cast<long double>(cursor)>=symbol_end)complete(h,hypothesis,cursor,stop);
                }
            }
            while(cursor<end) {
                const auto symbol_end=clock_boundary(h,code.symbol_samples());
                if(static_cast<long double>(cursor)>=symbol_end) { complete(h,hypothesis,cursor,stop);continue; }
                cursor=accumulate(h,hypothesis,cursor,end,code,stop);
                if(static_cast<long double>(cursor)>=symbol_end)complete(h,hypothesis,cursor,stop);
            }
        }
        settle_timing();
        if(pulse_segmented || scheduled_affine || (scheduled && scheduled->moments_eligible))work.segments=std::accumulate(affine_counts.begin(),affine_counts.end(),std::uint64_t{0});
        if(!certified_null)record_point(banks.front().observations(physical_view(0))[input.size()]);
        sample=end;
        room_for(0);
    }
};


struct PatternCorrelatorPolicyAccess {
    static PatternScheduledAcquisitionRequirements requirements(const Config& c,const PatternSearch& search,
            const PatternScheduledComplexGeometry& geometry,std::size_t workspace) {
        PatternScheduledAcquisitionRequirements out;
        out.requested=detail::scheduled_acquisition_policy::select(geometry.workspace_lower_bound_bytes,workspace);
        if(!out.requested){out.reason="original dense representation retained by arithmetic-independent availability policy";return out;}
        if(!geometry.supported || !scheduled_bursts(c) || packed_scheduled_symbols(c) ||
           c.spreading_mode!=SpreadingMode::pattern || !c.data_key || detail::drift_section_count(c,search.drift_tolerant)!=1){
            out.reason="implicit acquisition retains original backend for unsupported geometry/detector";return out;}
        try {
            const auto candidate_limit=search.compact_clock_search?std::min<std::size_t>(search.candidate_limit,32):search.candidate_limit;
            const auto point_capacity=search.compact_clock_search?std::size_t{64}:std::size_t{2048};
            require(c.pattern_symbols && search.start_offset_seconds && std::isfinite(*search.start_offset_seconds) &&
                    std::isfinite(search.start_uncertainty_seconds) && search.start_uncertainty_seconds>=0,
                    "long pattern correlation requires a finite system-clock start window");
            require(std::isfinite(search.false_alarm_probability) && search.false_alarm_probability>0 && search.false_alarm_probability<1 &&
                    std::isfinite(search.retain_score) && search.retain_score>=0 && search.bit_limit && search.chunk_bits &&
                    candidate_limit && candidate_limit<=65536 && search.track_limit && search.track_limit<=128,
                    "invalid long pattern search limits");
            require(std::isfinite(search.differential_window_seconds) && search.differential_window_seconds>=0,
                    "invalid differential window duration");
            long double highest=1;
            std::vector<double> physical_frequencies;
            if(!search.hypotheses.empty()) {
                require(search.hypotheses.size()<=maximum_pattern_frequency_rate_hypotheses,
                        "frequency/rate bank exceeds finite hypothesis limit");
                physical_frequencies.reserve(search.hypotheses.size());
                for(const auto& pair:search.hypotheses) {
                    require(std::isfinite(pair.frequency_offset_hz) && std::isfinite(pair.clock_error_ppm) &&
                            std::abs(pair.clock_error_ppm)<=10000,"invalid paired frequency/rate hypothesis");
                    highest=std::max(highest,1+static_cast<long double>(pair.clock_error_ppm)*1e-6L);
                    physical_frequencies.push_back(pair.frequency_offset_hz);
                }
                std::sort(physical_frequencies.begin(),physical_frequencies.end());
                physical_frequencies.erase(std::unique(physical_frequencies.begin(),physical_frequencies.end()),physical_frequencies.end());
                require(physical_frequencies.size()<=maximum_pattern_frequency_hypotheses,"frequency bank exceeds finite hypothesis limit");
            } else {
                require(!search.clock_errors_ppm.empty() && search.clock_errors_ppm.size()<=65,"clock-rate bank must contain 1..65 hypotheses");
                for(const auto ppm:search.clock_errors_ppm) {
                    require(std::isfinite(ppm) && std::abs(ppm)<=10000,"clock-rate hypotheses must fit +/-10000 ppm");
                    highest=std::max(highest,1+static_cast<long double>(ppm)*1e-6L);
                }
                // Planning is called after scheduled_complex_search resolves
                // an admitted bank. Bare unresolved defaults retain fallback.
                require(!search.frequency_offsets_hz.empty() && search.frequency_offsets_hz.size()<=65 &&
                        !search.couple_clock_to_carrier && (!search.expand_clock_search || search.frequency_offsets_hz.size()<=5),
                        "implicit memory projection requires a resolved carrier bank");
                physical_frequencies=search.frequency_offsets_hz;
            }
            const auto offset_limit=search.hypotheses.empty()?c.bandwidth_hz/8:pattern_frequency_offset_limit(c);
            for(const auto frequency:physical_frequencies)
                require(std::isfinite(frequency) && std::abs(frequency)<=offset_limit,"frequency hypothesis exceeds occupied band");
            if(const auto q=search.qualified_start_window)
                require(std::isfinite(q->epoch_origin_samples) && std::isfinite(q->phase_scale) && q->phase_scale>0 &&
                        std::isfinite(q->half_width_samples) && q->half_width_samples>=0,"invalid qualified UTC start map");
            PatternCode code(c,c.stream_epoch);
            if(static_cast<long double>(code.symbol_samples())/highest<pattern_absence_seconds*c.sample_rate){
                out.reason="implicit cold lifecycle requires nominal coherent symbol at least six seconds";return out;}
            const auto add=[](std::size_t a,std::size_t b) {
                require(b<=std::numeric_limits<std::size_t>::max()-a,"implicit mandatory state byte sum overflow");
                return a+b;
            };
            const auto multiply=[](std::size_t a,std::size_t b) {
                require(!a || b<=std::numeric_limits<std::size_t>::max()/a,"implicit mandatory state byte product overflow");
                return a*b;
            };
            detail::ScheduledAcquisitionCatalog catalog(c,search,geometry,std::max<std::size_t>(1,workspace/8));
            out.logical_lanes=catalog.size();out.batch_samples=catalog.size()<256?1:std::min<std::size_t>(8192,std::max<std::size_t>(scheduled_block_size,c.sample_rate));
            if(const auto* value=std::getenv("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES")) {
                char* end=nullptr;const auto count=std::strtoull(value,&end,10);
                if(end==value || *end!='\0' || count<1 || count>8192){out.reason="implicit diagnostic batch samples require 1..8192";return out;}
                out.batch_samples=static_cast<std::size_t>(count);
            }
            if(search.bit_limit<2)out.batch_samples=1;
            if(const auto* value=std::getenv("DATAPUMP_SCHED_LAZY_BATCH");value && std::string_view(value)=="0")out.batch_samples=1;
            const auto samples=std::ceil(static_cast<long double>(geometry.physical_aperture_samples)+
                2.L*search.start_uncertainty_seconds*c.sample_rate+canonical_phase_extent(c)+2*scheduled_block_size+out.batch_samples);
            const auto capture=samples*(sizeof(float)+sizeof(double))+
                (std::ceil(samples/scheduled_block_size)+2)*(sizeof(PatternCorrelator::Impl::LazyState::Block)+128);
            if(!std::isfinite(capture) || capture>=std::numeric_limits<std::size_t>::max()){
                out.reason="implicit finite capture horizon exceeds addressable workspace";return out;}
            out.local_capture_bytes=static_cast<std::size_t>(capture);
            out.required_history_samples=static_cast<std::size_t>(samples);
            const auto maximum_runs=std::max<std::size_t>(64,std::min<std::size_t>(16384,
                2*((static_cast<std::size_t>(samples)+scheduled_block_size-1)/scheduled_block_size)+128));
            out.capture_fence_bytes=detail::scheduled_capture::OriginalCaptureFences::required_bytes(maximum_runs);
            const auto pairs=search.hypotheses.empty()?multiply(search.frequency_offsets_hz.size(),search.clock_errors_ppm.size()):search.hypotheses.size();
            (void)pairs;const auto frequencies=physical_frequencies.size();
            using I=PatternCorrelator::Impl;
            const auto per_slot=I::lazy_slot_bytes();
            out.slot_unit_bytes=per_slot;out.minimum_slot_admission_bytes=I::lazy_minimum_slot_bytes();
            out.diagnostic_journal_bytes=detail::scheduled_diagnostics::DeferredEvents::required_bytes(scheduled_diagnostic_runs);
            out.serial_execution_bytes=sizeof(I::LazyState::Execution);
            const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
            const auto journal_runs=std::max<std::size_t>(64,std::min<std::size_t>(16384,
                2*((static_cast<std::size_t>(samples)+scheduled_block_size-1)/scheduled_block_size)+128));
            const auto journal_bytes=detail::scheduled_processing::ProcessingJournal::required_bytes(
                {journal_runs,journal_runs,journal_runs,workspace});
            out.per_bank_required_bytes=0;
            for(const auto bytes:{journal_bytes,sizeof(PatternCorrelator),sizeof(I),code.working_bytes(),catalog.working_bytes(),
                detail::ScheduledAcquisitionCatalog::cursor_reservation_bytes(),sizeof(I::LazyState),sizeof(I::LazyState::Execution),
                detail::scheduled_diagnostics::DeferredEvents::required_bytes(scheduled_diagnostic_runs),sizeof(I::ScheduledState),map.working_bytes(),
                multiply(frequencies,sizeof(Bank)),multiply(frequencies,2*sizeof(double)+sizeof(std::uint64_t)),
                (scheduled_block_size+1)*sizeof(Projection),multiply(point_capacity,sizeof(Complex)),
                multiply(candidate_limit,sizeof(PatternEvidence)),
                multiply(search.track_limit,sizeof(PatternBurst)+sizeof(I::Emission)),I::lazy_minimum_slot_bytes()})
                out.per_bank_required_bytes=add(out.per_bank_required_bytes,bytes);
            // Same inclusive minimum capacity condition as initialize_lazy:
            // five stable/scratch aliases at the existing 2x capacity factor.
            out.optional_tile_bytes=2*1024*1024; // Reservation scope; not a measured cost or a requirement to drop lanes.
            out.geometry_supported=true;
            out.reason="implicit acquisition requirements only; capture provision and actual execution eligibility remain separate";
        } catch(const Error& e){out.reason=e.what();}
        return out;
    }
    static PatternScheduledAcquisitionPolicy inspect(const Config& c,const PatternSearch& search,
            const PatternScheduledComplexGeometry& geometry,std::size_t workspace) {
        const auto r=requirements(c,search,geometry,workspace);PatternScheduledAcquisitionPolicy out;
        out.requested=r.requested;out.reason=r.reason;out.logical_lanes=r.logical_lanes;out.batch_samples=r.batch_samples;
        out.per_bank_required_bytes=r.per_bank_required_bytes;out.local_capture_bytes=r.local_capture_bytes;
        out.optional_tile_bytes=r.optional_tile_bytes;
        if(!r.geometry_supported)return out;
        if(search.scheduled_capture) {
            // A REAL owner remains necessary at every execution entry point.
            if(search.scheduled_capture->guaranteed_retained_samples()<r.required_history_samples) {
                out.reason="shared capture payload reservation cannot cover required original acquisition history";return out;
            }
            out.local_capture_bytes=r.capture_fence_bytes;
            out.shared_capture_bytes=search.scheduled_capture->capacity_reservation();out.shared_capture=true;
        }
        out.supported=out.per_bank_required_bytes<=workspace && out.local_capture_bytes<=workspace-out.per_bank_required_bytes;
        out.reason=out.supported?(out.shared_capture?
            "experimental implicit catalogue; actual cohort capture owner bound; original fences per bank; native tiles with exact guard fallback; total cost uncalibrated":
            "experimental implicit catalogue; local capture charged per bank; bounded tiles and exact guard fallback; total cost uncalibrated"):
            "implicit local capture plus minimum exact track state exceeds rolling workspace share; shared capture not yet bound";
        return out;
    }
};
PatternScheduledAcquisitionRequirements pattern_scheduled_acquisition_requirements(const Config& c,const PatternSearch& s,
        const PatternScheduledComplexGeometry& g,std::size_t workspace) {
    return PatternCorrelatorPolicyAccess::requirements(c,s,g,workspace);
}
PatternScheduledAcquisitionPolicy pattern_scheduled_acquisition_policy(const Config& c,const PatternSearch& s,
        const PatternScheduledComplexGeometry& g,std::size_t workspace) {
    return PatternCorrelatorPolicyAccess::inspect(c,s,g,workspace);
}
PatternScheduledAcquisitionMemoryProjection pattern_scheduled_acquisition_memory_projection(const Config& c,const PatternSearch& s,
        const PatternScheduledComplexGeometry& g,std::size_t workspace,std::size_t cohort_workspace,std::size_t consumers) {
    const auto r=PatternCorrelatorPolicyAccess::requirements(c,s,g,workspace);
    PatternScheduledAcquisitionMemoryProjection out;out.requested=r.requested;out.geometry_supported=r.geometry_supported;
    out.reason=r.reason;out.logical_lanes=r.logical_lanes;out.batch_samples=r.batch_samples;
    out.per_bank_required_bytes=r.per_bank_required_bytes;out.capture_fence_bytes=r.capture_fence_bytes;
    out.required_history_samples=r.required_history_samples;out.optional_tile_bytes=r.optional_tile_bytes;
    if(!r.geometry_supported)return out;
    try {
        const auto plan=detail::scheduled_capture::SharedScheduledCapture::cohort_layout(cohort_workspace,consumers);
        out.shared_capture_bytes=plan.reserved_bytes;out.guaranteed_history_samples=plan.guaranteed_samples;
        out.projected_fits=plan.guaranteed_samples>=r.required_history_samples &&
            r.per_bank_required_bytes<=workspace && r.capture_fence_bytes<=workspace-r.per_bank_required_bytes;
        out.reason=out.projected_fits?
            "planning-only necessary implicit complex layout; one cohort capture reservation plus mandatory bank state/fences; actual owner, constructor allocations, subscription and history validation required at execution":
            "planning-only shared capture layout cannot cover original history plus mandatory bank state/fences";
    } catch(const detail::scheduled_capture::Error& e){out.reason=e.what();}
    return out;
}

PatternCorrelator::PatternCorrelator(Config c,PatternSearch search,std::size_t bytes,PatternCorrelatorOptions options)
    :impl_([&]{rx_cpu::Context cpu(rx_cpu::Stage::setup);return std::make_unique<Impl>(c,std::move(search),bytes,options);}()){}
PatternCorrelator::~PatternCorrelator(){
    if(impl_ && impl_->scheduled_map() && !impl_->options.amplitude_only && audio::trace::active.load()) {
        const auto w=work();
        for(const auto& batch:impl_->scheduled->worker_temporal_templates)if(batch)impl_->record_temporal_fp32(*batch,"live");
        auto plan_cache=impl_->scheduled->temporal_retired.plan_cache;
        std::size_t plan_cache_bytes=0;
        for(const auto& batch:impl_->scheduled->worker_temporal_templates)if(batch) {
            plan_cache+=batch->plan_cache_statistics();plan_cache_bytes+=batch->plan_cache_bytes();
        }
        if(impl_->lazy_state())audio::trace::record("scheduled_implicit_work",[&](auto& f) {
            const auto& x=*impl_->lazy_state();
            f.add("epoch",impl_->config.stream_epoch);f.add("backend","implicit scheduled complex acquisition");
            f.add("screen_arithmetic",impl_->config.search_arithmetic==SearchArithmetic::fp64?
                "native FP64 exact original-template tiles or original fallback":
                "eligible native FP32 FFT/products; backend uses FP32/FP64 compensated spectra or FP64 response contractions; per-path diagnostics identify execution");
            f.add("arithmetic_scope","supported coverage; executed range/family counters and per-path preparation/ready/fallback records remain distinct");
            f.add("source_preparation","original shaping, limiter, gain and carrier FP64; absolute coordinates/phase origins wider");
            f.add("moment_synthesis",impl_->config.search_arithmetic==SearchArithmetic::fp64?"bypassed; original frequency execution":
                "when selected: FP32 products, FP64 compensated reductions and pre-cast buffer");
            f.add("scoring","original FP64/wider I/Q, real Gram, covariance, guards and final evidence");
            f.add("reference_refinement","original full real-IQ/Gram/noise/chip/outer guards and threshold/trial order");
            f.add("logical_lanes",x.catalog->size());f.add("processed_physical_samples",x.processed);
            f.add("queued_physical_samples",x.capture_end-x.processed);f.add("replay_samples",x.replay_samples);f.add("null_replay_fences",x.null_replay_fences);f.add("null_replay_samples",x.null_replay_samples);
            f.add("detached_replay_batches",x.detached_batches);f.add("detached_replay_lanes",x.detached_lanes);
            f.add("detached_replay_prefixes",x.detached_prefixes);f.add("detached_replay_blocks",x.detached_blocks);
            f.add("detached_private_lanes",x.detached_private_lanes);f.add("detached_private_rows",x.detached_private_rows);
            f.add("cold_completions",x.cold_completions);f.add("promotions",x.promotions);
            f.add("colored_rejections",x.colored_rejections);f.add("colored_on_demand_queries",x.colored_demand_queries);
            f.add("colored_on_demand_sample_pairs",x.colored_demand_pairs);f.add("colored_on_demand_fallbacks",x.colored_demand_fallbacks);
            f.add("colored_on_demand_failure_stage_counts",certdiag::ArrayView<12>{x.colored_demand_failure});
            f.add("colored_on_demand_excluded_unobserved_rows",x.colored_demand_excluded_rows);
            f.add("colored_on_demand_sample_pair_scope","completed queries only; partial cancellation/fallback work excluded");
            f.add("colored_source_builds",x.colored_source_builds);f.add("colored_source_hits",x.colored_source_hits);
            f.add("colored_source_translation_hits",x.colored_source_translation_hits);
            f.add("colored_source_evictions",x.colored_source_evictions);
            f.add("colored_source_peak_bytes",x.colored_source_peak_bytes);
            f.add("colored_fast_error_queries",x.colored_fast_error_queries);
            std::size_t color_bytes=0;for(const auto& row:x.colored_sources)if(row)color_bytes+=row->working_bytes();
            f.add("colored_source_bytes",color_bytes);
            f.add("original_projection_hits",x.projection_hits);f.add("original_projection_misses",x.projection_misses);
            f.add("original_projection_skips",x.projection_skips);f.add("original_projection_limit_bytes",x.projection_cache_limit);
            f.add("original_projection_peak_bytes",x.projection_peak_bytes);
            f.add("original_projection_effective_limit_bytes",x.original_projections?x.original_projections->capacity_bytes()+128:0);
            f.add("original_projection_objects",x.projection_objects);f.add("input_sample_origin",impl_->search.input_sample_origin);
            const auto projection_use=[&](const char* source,const auto& use) {
                const auto name=[&](const char* suffix){return std::string("original_projection_")+source+suffix;};
                f.add(name("_hits").c_str(),use.hits);f.add(name("_hit_samples").c_str(),use.hit_samples);
                f.add(name("_original_samples").c_str(),use.original_samples);f.add(name("_stores").c_str(),use.stores);
                f.add(name("_cross_consumer_hits").c_str(),use.cross_consumer_hits);
                f.add(name("_cross_consumer_hit_samples").c_str(),use.cross_consumer_hit_samples);
                f.add(name("_other_source_hits").c_str(),use.other_source_hits);
            };
            projection_use("acquisition",x.projection_acquisition);projection_use("diagnostic",x.projection_diagnostic);
            f.add("original_projection_detached_shared_consumers",x.projection_shared_consumers);
            f.add("original_projection_detached_shared_samples",x.projection_shared_samples);
            auto projection_statistics=x.projection_retired_statistics;
            if(x.original_projections)projection_statistics+=x.original_projections->statistics();
            f.add("original_projection_metadata_allocations",projection_statistics.metadata_allocations);
            f.add("original_projection_row_allocations",projection_statistics.row_allocations);
            f.add("original_projection_row_stores",projection_statistics.row_stores);
            f.add("original_projection_collision_evictions",projection_statistics.collision_evictions);
            f.add("original_projection_history_evictions",projection_statistics.history_evictions);
            f.add("original_projection_budget_skips",projection_statistics.budget_skips);
            f.add("original_projection_allocation_failures",projection_statistics.allocation_failures);
            f.add("original_projection_current_rows",x.original_projections?x.original_projections->entries():0);
            f.add("original_projection_current_bytes",x.original_projections?
                sizeof(Impl::LazyState::OriginalProjections)+128+x.original_projections->working_bytes():0);
            constexpr std::array projection_drop_names{"cap_change","allocation_pressure","workspace_shrink","explicit_trim"};
            for(std::size_t i=0;i<projection_drop_names.size();++i){
                const auto name=std::string("original_projection_")+projection_drop_names[i];
                f.add((name+"_drops").c_str(),x.projection_drops[i]);f.add((name+"_drop_rows").c_str(),x.projection_drop_rows[i]);
                f.add((name+"_rebuilds").c_str(),x.projection_rebuilds[i]);
            }
            f.add("original_projection_counter_scope","per-correlator cumulative; acquisition and diagnostic full prepared fences; cross-consumer counts exclude opposite-source producers; detached shared counts are separate from cache hits; drops count nonnull objects, drop_rows counts occupied rows, rebuild is first row store after occupied-row drop; allocation failures include object/index/rows, budget skips include object reservation and row store refusals, configured-disabled or unsupported-environment bypass is uncounted; mixing CPU scope covers original recurrence only");
            f.add("original_projection_scope","per-bank immutable PCM binding; exact original processing fence/seed/step/Fs/frequency/environment; copied FP64 rows");
            f.add("screened_lanes",x.screen_lanes);f.add("rejected_lanes",x.screen_rejections);f.add("refined_lanes",x.refinements);
            f.add("eligible_ranges",x.eligible_ranges);f.add("fallback_ranges",x.fallback_ranges);
            f.add("invalid_certificates",x.invalid_certificates);f.add("proxy_ranges",x.proxy_ranges);
            f.add("fp32_ranges",x.fp32_ranges);f.add("fp64_ranges",x.fp64_ranges);
            f.add("integer_origin_class_covers",x.origin_class_covers);
            f.add("input_transforms",x.input_transforms);f.add("template_transforms",x.template_transforms);
            f.add("inverse_transforms",x.inverse_transforms);f.add("plan_hits",x.plan_hits);f.add("plan_misses",x.plan_misses);
            f.add("spectral_template_hits",x.spectral_template_hits);f.add("spectral_template_misses",x.spectral_template_misses);
            f.add("resident_slots",x.slots.size());f.add("stable_slot_capacity",x.capacity);
            f.add("history_reserved_bytes",x.history_reservation);f.add("peak_prepared_bytes",x.peak_prepared_bytes);
            f.add("peak_workspace_bytes",w.peak_workspace_bytes);f.add("shared_original_capture",bool(x.shared_capture));
            f.add("event_frontier_scope","committed original completion events; does not imply all unfinished-symbol arithmetic is complete");
        });
        if(impl_->lazy_state() && impl_->lazy_state()->certificate_profile.enabled) {
            const auto& profile=impl_->lazy_state()->certificate_profile;
            for(std::size_t i=0;i<certdiag::cohorts;++i) {
                audio::trace::record("scheduled_certificate_preparation",[&](auto& f) {
                    const auto& p=profile.preparation.cohort[i];f.add("epoch",impl_->config.stream_epoch);f.add("cohort",certdiag::cohort_names[i]);
                    f.add("scope","completed numerical ranges; lambda is per candidate estimate, not certified lower bound");
                    f.add("magnitude_bins","negative,zero,infinity,nan,<2^-64,16 eight-exponent bins,>=2^64");
                    f.add("ranges",p.ranges);f.add("candidate_fits",p.native.candidates);f.add("requires_refinement",p.native.requires_refinement);
                    f.add("valid_certificates",p.native.valid_certificates);f.add("saturated",p.saturated||p.native.saturated||
                        p.displacement.saturated||p.l1.saturated||p.gram.saturated||p.l2_squared.saturated||p.native.lambda.saturated);
                    f.add("displacement",certdiag::view(p.displacement));f.add("template_l1",certdiag::view(p.l1));
                    f.add("template_gram",certdiag::view(p.gram));f.add("template_l2_squared",certdiag::view(p.l2_squared));f.add("lambda_estimate",certdiag::view(p.native.lambda));
                });
                audio::trace::record("scheduled_certificate_cold",[&](auto& f) {
                    const auto& p=profile.cold[i];f.add("epoch",impl_->config.stream_epoch);f.add("cohort",certdiag::cohort_names[i]);
                    f.add("scope","cold attempts; certificate/reference/errors per candidate, energy/carrier/mask/U per lane; attempts may repeat after cancellation");
                    f.add("attempts",p.attempts);f.add("rejections",p.rejections);f.add("valid_certificates",p.valid_certificates);f.add("valid_references",p.valid_references);
                    f.add("failure_order","none,no_screen,carrier_nonfinite,count_overflow,invalid_contract,nonfinite_errors,nonpositive_gram,energy_floor,fraction_not_below_one,proxy_invalid,score_overflow");
                    f.add("failures",certdiag::view(p.failure));bool saturated=p.saturated||p.failure.saturated;
                    for(std::size_t j=0;j<certdiag::metrics;++j){saturated|=p.metric[j].saturated;f.add(certdiag::metric_names[j],certdiag::view(p.metric[j]));}
                    f.add("saturated",saturated);
                });
                audio::trace::record("scheduled_certificate_pairs",[&](auto& f) {
                    const auto& p=profile.pairs[i];f.add("epoch",impl_->config.stream_epoch);f.add("cohort",certdiag::cohort_names[i]);
                    f.add("scope","same completed original cold event; U bounds guarded score when colored proof is selected; raw_exceeds_upper uses separate raw bound");
                    f.add("score_bins","negative,zero,(0,1),[1,4),[4,16),[16,32),[32,64),[64,128),[128,512),[512,infinity),infinity,nan");
                    f.add("matrix_order","row-major U bins by original-score bins");f.add("completed",p.completed);f.add("raw_exceeds_upper",p.raw_exceeds_upper);
                    f.add("guarded_bound_pairs",p.guarded_bound_pairs);f.add("guarded_exceeds_upper",p.guarded_exceeds_upper);
                    f.add("raw_below_threshold",p.raw_below_threshold);f.add("guarded_below_threshold",p.guarded_below_threshold);
                    f.add("saturated",p.saturated||p.upper.saturated||p.raw.saturated||p.guarded.saturated||p.upper_raw.saturated||p.upper_guarded.saturated);
                    f.add("upper",certdiag::view(p.upper));f.add("raw",certdiag::view(p.raw));f.add("guarded",certdiag::view(p.guarded));
                    f.add("upper_raw",certdiag::view(p.upper_raw));f.add("upper_guarded",certdiag::view(p.upper_guarded));
                    f.add("resident_completions_separate",profile.resident_completions);f.add("pair_identity_mismatches",profile.pair_identity_mismatches);
                    f.add("pending_pair_at_destruction",bool(impl_->lazy_state()->certificate_pending_event));f.add("state_counter_saturated",profile.saturated);
                });
            }
        }

        audio::trace::record("split_complex_work",[&](auto& f) {
            f.add("epoch",impl_->config.stream_epoch);f.add("samples",w.samples);f.add("hypotheses",w.hypotheses);
            f.add("null_samples",w.scheduled_null_samples);f.add("null_pushes",w.scheduled_null_pushes);
            f.add("worker_preparations",w.scheduled_worker_preparations);
            f.add("temporal_requested",impl_->options.scheduled_temporal_template_experiment);
            f.add("temporal_fp64_backend",detail::ScheduledTemporalTemplates::Tile::component_avx2_available()?
                "AVX2: four FP64 components, ordered FP64 accumulation":"original ordered FP64 scalar/SSE2");
            f.add("temporal_fp64_backend_scope","successful original/refined tiles only; native FP32 counts and raw fallbacks are separate");
            f.add("flat_export_requested",detail::scheduled_flat_export_requested());
            const auto flat_counts=detail::scheduled_flat_export_counts();
            f.add("flat_export_counter_scope","process cumulative diagnostic totals; use latest/max, do not sum banks");
            f.add("flat_export_process_rows",flat_counts.rows);
            f.add("flat_export_process_samples",flat_counts.samples);
            f.add("flat_raw_process_pieces",flat_counts.raw_pieces);
            f.add("flat_raw_process_samples",flat_counts.raw_samples);
            const auto outer_public_counts=detail::scheduled_outer_public_counts();
            f.add("outer_public_replay_requested",detail::scheduled_outer_public_replay_requested());
            f.add("outer_public_counter_scope","process cumulative diagnostic totals; use latest/max, do not sum banks");
            f.add("outer_public_process_pieces",outer_public_counts.pieces);
            f.add("outer_public_process_equal_pieces",outer_public_counts.equal_pieces);
            f.add("outer_public_process_samples",outer_public_counts.samples);
            f.add("outer_public_process_events",outer_public_counts.events);
            f.add("outer_public_process_bound_fallbacks",outer_public_counts.bound_fallbacks);
            f.add("r1_export_requested",detail::scheduled_r1_export_requested());
            const auto r1_counts=detail::scheduled_r1_export_counts();
            f.add("r1_export_counter_scope","process cumulative diagnostic totals; use latest/max, do not sum banks");
            f.add("r1_export_process_attempts",r1_counts.attempts);
            f.add("r1_export_process_rows",r1_counts.rows);
            f.add("r1_export_process_samples",r1_counts.samples);
            f.add("temporal_fp32_requested",impl_->temporal_fp32_policy().requested);
            f.add("temporal_fp32_component_limit",impl_->temporal_fp32_policy().limit);
            f.add("temporal_fp32_entry_payload_requested",detail::ScheduledTemporalFp32Execution::payload_requested());
            f.add("temporal_attempts",w.scheduled_temporal_attempts);f.add("temporal_ready",w.scheduled_temporal_ready);
            f.add("temporal_fallbacks",w.scheduled_temporal_fallbacks);f.add("temporal_samples",w.scheduled_temporal_samples);
            f.add("temporal_plan_reuses",w.scheduled_temporal_plan_reuses);f.add("temporal_normalized_reuses",w.scheduled_temporal_normalized_reuses);
            f.add("temporal_values",w.scheduled_temporal_values);f.add("temporal_shape_terms",w.scheduled_temporal_shape_terms);
            f.add("temporal_peak_bytes",w.scheduled_temporal_peak_bytes);f.add("template_constructions",w.scheduled_template_constructions);
            f.add("temporal_plan_cache_requested",impl_->temporal_plan_cache_requested());
            f.add("temporal_plan_cache_capacity",detail::ScheduledTemporalTemplates::PlanCache::capacity);
            f.add("temporal_plan_cache_live_bytes",plan_cache_bytes);
            f.add("temporal_plan_cache_requests",plan_cache.requests);f.add("temporal_plan_cache_hits",plan_cache.hits);
            f.add("temporal_plan_cache_misses",plan_cache.misses);f.add("temporal_plan_cache_builds",plan_cache.builds);
            f.add("temporal_plan_cache_evictions",plan_cache.evictions);f.add("temporal_plan_cache_unavailable",plan_cache.unavailable);
            f.add("temporal_plan_cache_rejected",plan_cache.rejected);f.add("temporal_plan_cache_cancelled",plan_cache.cancelled);
            f.add("temporal_plan_cache_exceptions",plan_cache.exceptions);f.add("temporal_plan_cache_hash_words",plan_cache.hash_words);
            f.add("temporal_plan_cache_compared_words",plan_cache.compared_words);
            f.add("template_cache_hits",w.scheduled_template_cache_hits);f.add("carry_samples",w.scheduled_carry_samples);
            f.add("colored_guard_activations",w.scheduled_colored_guard_activations);
            f.add("quarter_guard_fallbacks",w.scheduled_quarter_guard_fallbacks);f.add("pulse_spans",w.segments);
            f.add("counter_scope","per-sample template pairs; span endpoints/worker setup are separate");
            f.add("common_families",w.scheduled_common_families);f.add("common_lanes",w.scheduled_common_lanes);
            f.add("common_shapes",w.scheduled_common_shapes);f.add("common_raw_samples",w.scheduled_common_raw_samples);
            f.add("common_raw_lane_samples",w.scheduled_common_raw_lane_samples);f.add("common_affine_lanes",w.scheduled_common_affine_lanes);
            f.add("common_mapped_pairs",w.scheduled_common_mapped_pairs);f.add("common_mapped_failed_pairs",w.scheduled_common_mapped_failed_pairs);
            f.add("common_mapped_lanes",w.scheduled_common_mapped_lanes);f.add("common_affine_reused_lanes",w.scheduled_common_affine_reused_lanes);
            f.add("common_affine_reuse",impl_->options.scheduled_common_affine_reuse);
            f.add("common_gram_fallbacks",w.scheduled_common_gram_fallbacks);f.add("common_peak_bytes",w.scheduled_common_peak_bytes);
            f.add("common_phase_limit",impl_->options.scheduled_common_phase_limit);
            f.add("common_phase_address_hits",w.scheduled_common_phase_address_hits);
            f.add("common_phase_address_misses",w.scheduled_common_phase_address_misses);
            f.add("common_phase_address_queries",w.scheduled_common_phase_address_queries);
            f.add("common_phase_attempts",w.scheduled_common_phase_attempts);f.add("common_phase_rejects",w.scheduled_common_phase_rejects);
            f.add("common_phase_families",w.scheduled_common_phase_families);f.add("common_phase_lanes",w.scheduled_common_phase_lanes);
            f.add("common_raw_fit_pairs",w.scheduled_common_raw_fit_pairs);f.add("common_raw_reused_lane_samples",w.scheduled_common_raw_reused_lane_samples);
            f.add("common_raw_packed_values",w.scheduled_common_raw_packed_values);f.add("common_raw_workspace_samples",w.scheduled_common_raw_workspace_samples);
            f.add("common_raw_peak_bytes",w.scheduled_common_raw_peak_bytes);
            f.add("common_simd_selected_isa",w.scheduled_common_simd_selected_isa);f.add("common_simd_attempts",w.scheduled_common_simd_attempts);
            f.add("common_simd_samples",w.scheduled_common_simd_samples);f.add("common_simd_lane_samples",w.scheduled_common_simd_lane_samples);
            f.add("common_simd_packed_values",w.scheduled_common_simd_packed_values);f.add("common_simd_guard_updates",w.scheduled_common_simd_guard_updates);
            f.add("common_simd_outer_updates",w.scheduled_common_simd_outer_updates);f.add("common_simd_peak_bytes",w.scheduled_common_simd_peak_bytes);
            f.add("common_simd_short_samples",w.scheduled_common_simd_short_samples);f.add("common_simd_workspace_samples",w.scheduled_common_simd_workspace_samples);
            f.add("common_simd_isa_samples",w.scheduled_common_simd_isa_samples);f.add("common_simd_scalar_lane_samples",w.scheduled_common_simd_scalar_lane_samples);
            const auto& state=*impl_->scheduled;const auto* curves=state.curves.get();const auto* floats=state.float_curves.get();
            f.add("curve_hits",state.curve_hits+(curves?curves->hits:0)+(floats?floats->hits:0));
            f.add("curve_misses",state.curve_misses+(curves?curves->misses:0)+(floats?floats->misses:0));
            f.add("curve_evictions",state.curve_evictions+(curves?curves->evictions:0)+(floats?floats->evictions:0));
            f.add("curve_exhaustions",state.curve_exhaustions+(curves?curves->exhaustions:0)+(floats?floats->exhaustions:0));
            f.add("fine_curves_requested",detail::fine_source_curves_requested());
            f.add("fine_curves_eligible",impl_->fine_curves_eligible());
            f.add("fine_curves_allocated",impl_->fine_curves_allocated());
            f.add("fine_curve_requested_table_revision",detail::fine_source_curves_requested()?detail::fine_source_curve_revision():1U);
            const auto& borrow_stats=detail::fine_curve_borrow_statistics();
            f.add("fine_curve_direct_borrows",borrow_stats.direct.load(std::memory_order_relaxed));
            f.add("fine_curve_cached_borrows",borrow_stats.cached.load(std::memory_order_relaxed));
            f.add("fine_curve_l2_borrow_fallbacks",borrow_stats.l2_fallbacks.load(std::memory_order_relaxed));
            f.add("fine_curve_borrow_counter_scope","process cumulative; do not sum bank snapshots; local writer/exhaustion is in curve_exhaustions");
            f.add("fine_curve_borrow_mode",detail::fine_source_curve_cached_borrow_requested()?"cached within joined push":
                detail::fine_source_curve_borrow_requested()?"direct chip/job lease":"copied local rows");
            f.add("curve_arithmetic",impl_->options.scheduled_float_curve_experiment?"native FP32 table/interpolation":"FP64 table/interpolation");
            f.add("native_float_gram_experiment",impl_->options.scheduled_float_gram_experiment);
            f.add("float_gram_attempts",w.scheduled_float_gram_attempts);
            f.add("float_gram_accepted_pairs",w.scheduled_float_gram_accepted_pairs);
            f.add("float_gram_range_fallbacks",w.scheduled_float_gram_range_fallbacks);
            f.add("float_gram_rank_fallbacks",w.scheduled_float_gram_rank_fallbacks);
            f.add("float_gram_error_fallbacks",w.scheduled_float_gram_error_fallbacks);
            f.add("float_gram_helper_fallbacks",w.scheduled_float_gram_helper_fallbacks);
            f.add("float_gram_signal_attempts",w.scheduled_float_gram_signal_attempts);
            f.add("float_gram_maximum_relative_error",w.scheduled_float_gram_maximum_relative_error);
            f.add("gram_arithmetic",w.scheduled_float_gram_accepted_pairs?
                "diagnostic native FP32 normalized Gram products; FP64 fields/state/score and original fallback":
                "original FP64 Gram products/fields/state/score");
            f.add("native_float_moment_experiment",impl_->options.scheduled_float_moment_experiment);
            f.add("float_moment_prepared_samples",w.scheduled_float_moment_prepared_samples);
            f.add("float_moment_prepare_fallbacks",w.scheduled_float_moment_prepare_fallbacks);
            f.add("float_moment_attempts",w.scheduled_float_moment_attempts);
            f.add("float_moment_pieces",w.scheduled_float_moment_pieces);
            f.add("float_moment_condition_fallbacks",w.scheduled_float_moment_condition_fallbacks);
            f.add("float_moment_error_fallbacks",w.scheduled_float_moment_error_fallbacks);
            f.add("float_moment_accepted_pairs",w.scheduled_float_moment_accepted_pairs);
            f.add("float_moment_fp64_pairs",w.scheduled_float_moment_fp64_pairs);
            f.add("float_moment_gram_fallbacks",w.scheduled_float_moment_gram_fallbacks);
            f.add("signal_arithmetic",w.scheduled_float_moment_pieces?
                "diagnostic native FP32 packed signal moments/products; FP64 real Gram/energy/reductions and guards":
                "FP64 physical signal products, real Gram, energy, reductions and guards");
            f.add("peak_workspace_bytes",w.peak_workspace_bytes);f.add("timers_enabled",impl_->options.measure_work);
            if(impl_->options.measure_work){f.add("validation_cpu_seconds",w.validation_cpu_seconds);
                f.add("frontend_cpu_seconds",w.frontend_cpu_seconds);f.add("search_cpu_seconds",w.search_cpu_seconds);}
        });
    }
}
PatternCorrelator::PatternCorrelator(PatternCorrelator&&) noexcept=default;
PatternCorrelator& PatternCorrelator::operator=(PatternCorrelator&&) noexcept=default;
void PatternCorrelator::push(std::span<const float> samples,std::stop_token stop,detail::CorrelationProjectionCache* cache) {
        rx_cpu::Context cpu_context(rx_cpu::Stage::control_residual);
    auto& s=*impl_;cancelled(stop);require(!s.finished,"pattern capture already finished");
    const auto caller_binding=detail::pattern_permutation_binding;
    const auto inherited=s.lazy_state() && !cache?caller_binding:detail::PatternPermutationBinding{};
    detail::PatternPermutationScope permutation_scope(cache?cache->permutations():inherited.pools,
        cache?cache->permutation_namespace():inherited.key_namespace);
    require(samples.size()<=std::numeric_limits<std::uint64_t>::max()-s.sample,"pattern sample counter overflow");
    {
        rx_cpu::Scope cpu_validation(rx_cpu::Stage::input_validation);
        WorkTimer validation_timer(s.options.measure_work,s.work.validation_seconds,s.work.validation_cpu_seconds);
        if(s.options.raw_reference || !cache || !cache->covers(samples))
            for(auto value:samples)require(std::isfinite(value),"nonfinite pattern sample");
    }
    if(s.options.amplitude_only){s.push_amplitude(samples,stop,cache);return;}
    if(s.lazy_state()){s.push_lazy(samples,stop);return;}
    // The prepared capability is borrowed only until this synchronous push
    // joins every scoring worker. A pool/family never survives as a raw pointer
    // across admission, trimming, receiver destruction or key replacement.
    struct ClearSharedSource {
        Impl::ScheduledState* state;
        ~ClearSharedSource(){if(state){
            if(detail::fine_source_curve_cached_borrow_requested()) {
                if(state->curves)state->curves->clear_borrowed_rows();
                if(state->float_curves)state->float_curves->clear_borrowed_rows();
            }
            state->shared_source_curves=nullptr;state->shared_source_family=nullptr;
        }}
    } clear_source{s.scheduled.get()};
    if(s.scheduled && detail::curve_sharing_allowed(s.config,s.scheduled->moments_eligible || s.fine_curves_eligible()) &&
       !s.options.scheduled_float_curve_experiment && cache && cache->source_curves() && cache->source_family()) {
        const auto* family=cache->source_family();
        // Binding is borrowed, so an allocator-reused address is never a
        // compatibility certificate. Exact immutable fields are checked on
        // every push; this constructs no private stream or template row.
        if(*family==detail::source_family(s.config,family->key_namespace)) {
            s.scheduled->shared_source_curves=cache->source_curves();s.scheduled->shared_source_family=family;
        }
    }
    // Still validate the original PCM and cancellation above. This is only a
    // computation omission under an exact monotone calendar proof, not a
    // capture omission or an observation of absent valid symbols.
    if(s.advance_certified_null(samples.size()))return;
    // Ordinary epochs retain their original idle footprint. Mapped workers
    // can be reused between pushes; they remain charged/evictable, and Live
    // reclaims them before required clock/key/epoch admissions.
    struct ReleaseWorkers {
        Impl& state;
        ~ReleaseWorkers() {if(!state.scheduled_map())state.drop_workers();}
    } release{s};
    if(!samples.empty()){s.prepare_drift(s.sample+samples.size());s.prepare_workers();}
    struct ClearSharedTemporalPlans {
        Impl::ScheduledState* state;
        ~ClearSharedTemporalPlans(){if(state)for(auto& batch:state->worker_temporal_templates)
            if(batch)batch->bind_shared_plan_cache(nullptr);}
    } clear_temporal{s.scheduled.get()};
    // After original preparation, bind exact worker slots only. Live's phase-1
    // readiness/headroom grant ensures this cannot size workers using cache RAM.
    if(cache && cache->temporal_plans()) {
        const auto workers=s.temporal_plan_workers();const auto* borrow=cache->temporal_plans();
        if(workers && *workers && *workers<=borrow->workers())
            for(std::size_t i=0;i<*workers;++i)s.scheduled->worker_temporal_templates[i]->bind_shared_plan_cache(borrow->slot(i));
    }
    for(std::size_t offset=0;offset<samples.size();) {
        auto count=s.process_batch(samples.subspan(offset),stop);
        if(!count) {
            count=std::min(s.block_samples,samples.size()-offset);
            count=s.process_count(count);
            s.process(samples.subspan(offset,count),stop,cache);
        }
        offset+=count;
    }
}
bool PatternCorrelator::try_skip_unobserved(std::uint64_t count,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);require(!s.finished,"pattern capture already finished");
    if(!s.scheduled_map() || !s.options.amplitude_only)return false;
    require(count<=std::numeric_limits<std::uint64_t>::max()-s.sample,"pattern sample counter overflow");
    const auto end=s.sample+count;
    for(const auto& h:s.hypotheses) {
        const auto first=s.scheduled_map()->locate(s.scheduled_tick(h,s.sample).add_signed(-s.scheduled_uncertainty(h,s.sample))).source;
        const auto last=s.scheduled_map()->locate(s.scheduled_tick(h,end).add_samples(s.scheduled_cell_width(h)).add_signed(s.scheduled_uncertainty(h,end))).source;
        if(first.distance(last)!=0)return false;
    }
    // U is monotone; equal endpoints prove every interior sample excluded for
    // every retained lane. An unfinished FFT now straddles a known null and
    // cannot become a valid observation. First settle its ALREADY RECEIVED
    // prefix, so a completed bit is not held through the entire quiet group.
    // Skipped samples themselves supply no fit, count or observed absence.
    if(s.amplitude_partial && s.amplitude_processed<s.sample)
        s.amplitude_scheduled_progress(s.amplitude_processed,s.sample,nullptr,stop);
    const auto begin=s.sample;
    s.amplitude_scheduled_progress(begin,end,nullptr,stop);
    s.sample=end;s.amplitude_partial=0;
    const auto remainder=(s.search.input_sample_origin%s.amplitude_geometry->fft_samples+
        end%s.amplitude_geometry->fft_samples)%s.amplitude_geometry->fft_samples;
    const auto tail=remainder?s.amplitude_geometry->fft_samples-remainder:0;
    require(tail<=std::numeric_limits<std::uint64_t>::max()-end,"amplitude block coordinate overflow");
    s.amplitude_processed=end+tail;s.work.scheduled_null_samples+=count;
    return true;
}
std::optional<ScheduledCaptureEnvelope> PatternCorrelator::scheduled_capture_envelope(std::uint64_t sample) const {
    const auto& s=*impl_;if(!s.scheduled_map() || !s.options.amplitude_only || s.hypotheses.empty())return std::nullopt;
    ScheduledCaptureEnvelope result;result.reference_sample=sample;
    bool first=true;
    for(const auto& h:s.hypotheses) {
        const auto center=s.scheduled_tick(h,sample),lower=center.add_signed(-s.scheduled_uncertainty(h,sample));
        const auto upper=center.add_samples(s.scheduled_cell_width(h)).add_signed(s.scheduled_uncertainty(h,sample));
        if(first || lower.distance(result.lower)<0)result.lower=lower;
        if(first || upper.distance(result.upper)>0)result.upper=upper;
        if(first || h.rate-s.scheduled->clock_radius<result.minimum_rate)result.minimum_rate=h.rate-s.scheduled->clock_radius;
        if(first || h.rate+s.scheduled->clock_radius>result.maximum_rate)result.maximum_rate=h.rate+s.scheduled->clock_radius;
        first=false;
    }
    return result;
}
std::optional<std::uint64_t> PatternCorrelator::unobserved_end_from(std::uint64_t sample) const {
    const auto& s=*impl_;if(!s.scheduled_map() || !s.options.amplitude_only)return std::nullopt;
    auto end=std::numeric_limits<std::uint64_t>::max();
    for(const auto& h:s.hypotheses) {
        const auto center=s.scheduled_tick(h,sample),lower=center.add_signed(-s.scheduled_uncertainty(h,sample));
        const auto upper=center.add_samples(s.scheduled_cell_width(h)).add_signed(s.scheduled_uncertainty(h,sample));
        const auto a=s.scheduled_map()->locate(lower),b=s.scheduled_map()->locate(upper);
        if(a.active || b.active || a.source.distance(b.source)!=0)return std::nullopt;
        const auto next=s.scheduled_map()->source_to_physical(a.source,false);
        const auto duration=std::max(0.L,std::floor(next.distance(upper)/(h.rate+s.scheduled->clock_radius)));
        if(duration>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max()-sample))continue;
        end=std::min(end,sample+static_cast<std::uint64_t>(duration));
    }
    if(end<=sample || end==std::numeric_limits<std::uint64_t>::max())return std::nullopt;
    return end;
}
void PatternCorrelator::finish(std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)return;
    if(s.lazy_state()){s.flush_lazy(stop);require(s.lazy_state()->processed==s.lazy_state()->capture_end,
        "implicit finish lacks the required original processing fence; no absence fabricated");}
    s.amplitude_publication_dirty=true;
    s.settle_timing();
    s.for_each_resident([&](std::size_t,auto& h){s.publish(h);s.clear(h);});
    s.finished=true;
}
std::vector<PatternBurst> PatternCorrelator::take_bursts(){
    auto& s=*impl_;

    // Amplitude evidence changes only at scored symbol boundaries. Partial
    // FFT input cannot accept a bit or resolve timing. Retain the empty queue
    // and avoid scanning every rolling-bank hypothesis on such polls.
    if((s.options.amplitude_only || s.scheduled_map()) && !s.amplitude_publication_dirty && !s.timing_dirty && s.bursts.empty()) {
        ++s.work.amplitude_unchanged_polls;return {};
    }
    // A consumer drain is also a presentation boundary. Publish every
    // accepted decision available now, even when a whole message is much
    // shorter than chunk_bits or each symbol takes hours. Draining does not
    // admit weak decisions, release the clock, or imply physical completion.
    s.settle_timing();
    s.for_each_resident([&](std::size_t,auto& h){s.publish(h,false,true,true);});
    if(s.options.amplitude_only || s.scheduled_map()) {
        ++s.work.amplitude_publication_polls;
        // A full output queue can leave committed bits for the next poll.
        // Keep that work pending even without further PCM, including EOF.
        s.amplitude_publication_dirty=s.timing_dirty || std::any_of(s.hypotheses.begin(),s.hypotheses.end(),
            [](const auto& h){return h.committed!=0;});
        if(s.bursts.empty()) {
            s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;return {};
        }
    }
    auto result=std::move(s.bursts);s.bursts={};s.bursts.reserve(s.search.track_limit);
    s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;return result;
}
PatternBurst PatternCorrelator::provisional()const {
    const Impl::Hypothesis* best=nullptr;
    impl_->for_each_resident([&](std::size_t,const auto& candidate) {
        if(candidate.admitted && (!best || candidate.sum_score>best->sum_score))best=&candidate;
    });
    if(!best)return {};
    auto result=best->burst;result.bits.resize(best->committed);result.end_sample=best->committed_end;
    result.complete=false;return result;
}
void PatternCorrelator::process_pending(std::stop_token stop) {
    cancelled(stop);const auto binding=detail::pattern_permutation_binding;
    detail::PatternPermutationScope permutation_scope(binding.pools,binding.key_namespace);
    impl_->poll_lazy(stop);
}
bool PatternCorrelator::deferred_pattern_history()const noexcept{return impl_->lazy_state()!=nullptr;}
std::uint64_t PatternCorrelator::diagnostic_history_generation()const noexcept{return impl_->lazy_state()?impl_->lazy_state()->history_generation:0;}
std::uint64_t PatternCorrelator::diagnostic_history_resolved_through()const noexcept{
    return impl_->lazy_state() && impl_->lazy_state()->diagnostics?impl_->lazy_state()->diagnostics->resolved_through():diagnostic_history_generation();
}
std::uint64_t PatternCorrelator::oldest_unresolved_diagnostic_event()const noexcept{
    if(impl_->lazy_state() && impl_->lazy_state()->diagnostics)if(const auto event=impl_->lazy_state()->diagnostics->oldest())return event->ordinal;
    const auto generation=diagnostic_history_generation();return generation==std::numeric_limits<std::uint64_t>::max()?generation:generation+1;
}
std::optional<std::uint64_t> PatternCorrelator::delayed_processing_frontier()const noexcept{return impl_->lazy_state()?std::optional<std::uint64_t>{impl_->lazy_state()->processed}:std::nullopt;}
std::vector<PatternEvidence> PatternCorrelator::candidates()const {
    impl_->lazy_resolve_diagnostics({});return impl_->history;
}
std::vector<PatternEvidence> PatternCorrelator::candidates(std::size_t limit)const{return candidates(limit,{});}
std::vector<PatternEvidence> PatternCorrelator::candidates(std::size_t limit,std::stop_token stop)const {
    cancelled(stop);if(!limit)return {};impl_->lazy_resolve_diagnostics(stop);
    const auto& history=impl_->history;
    return {history.end()-static_cast<std::ptrdiff_t>(std::min(limit,history.size())),history.end()};
}
std::vector<Complex> PatternCorrelator::take_chip_constellation() {
    auto& s=*impl_;std::vector<Complex> result;result.reserve(s.point_count);
    for(std::size_t i=0;i<s.point_count;++i)result.push_back(s.points[(s.point_begin+i)%s.points.size()]);
    s.point_begin=s.point_count=0;return result;
}
bool PatternCorrelator::acquiring()const {return !impl_->finished && std::any_of(impl_->hypotheses.begin(),impl_->hypotheses.end(),[](const auto& h){return h.admitted || !h.burst.bits.empty();});}
bool PatternCorrelator::synchronized()const{return std::any_of(impl_->hypotheses.begin(),impl_->hypotheses.end(),[](const auto& h){return h.admitted;});}
bool PatternCorrelator::initial_search_complete()const {
    if(impl_->lazy_state())return static_cast<long double>(impl_->lazy_state()->processed)>=impl_->lazy_state()->initial_complete_due;
    const auto& s=*impl_;
    return std::all_of(s.hypotheses.begin(),s.hypotheses.end(),[&](const auto& h) {
        // A negative origin can leave the first scored symbol truncated at
        // capture start. Wait for the next full symbol in that hypothesis's
        // own rate. index advances only after complete() scores every phase;
        // clearing noise candidates or flushing EOF never advances it.
        const auto remainder=s.options.amplitude_only?s.search.input_sample_origin%s.amplitude_geometry->fft_samples:0;
        const auto first_observation=remainder?s.amplitude_geometry->fft_samples-remainder:0;
        const auto first_full=s.scheduled_map() && !s.scheduled->first_full_indices.empty()?
            s.scheduled->first_full_indices[static_cast<std::size_t>(&h-s.hypotheses.data())]:s.scheduled_map()?s.scheduled_index_at(h.phase_lower,s.source_position(h,first_observation),true):
            std::ceil(std::max(0.L,static_cast<long double>(first_observation)-h.origin)*h.rate/s.code.symbol_samples());
        return static_cast<long double>(h.index)>first_full;
    });
}
bool PatternCorrelator::drift_tolerant()const{return impl_->drift_sections>1;}
std::size_t PatternCorrelator::working_bytes()const{return sizeof(PatternCorrelator)+impl_->working_bytes();}
std::size_t PatternCorrelator::projection_cache_headroom(std::size_t input_samples)const{return impl_->projection_cache_headroom(input_samples);}
std::optional<std::size_t> PatternCorrelator::temporal_plan_workers()const{return impl_->temporal_plan_workers();}
std::size_t PatternCorrelator::reserved_workspace_bytes()const{return sizeof(PatternCorrelator)+impl_->accounted_bytes;}
void PatternCorrelator::set_workspace_bytes(std::size_t bytes) {
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_lazy_colored_sources();
    if(impl_->lazy_state() && impl_->lazy_state()->detached_replay && !impl_->lazy_state()->detached_replay->numeric_active &&
       bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->lazy_drop_detached_replay();
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)
        impl_->drop_lazy_projections(Impl::LazyState::ProjectionDrop::workspace_shrink);
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_lazy_plans();
    if(impl_->fine_curves_allocated() && bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)
        impl_->drop_scheduled_curves();
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_scheduled_temporal_plan_caches();
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_scheduled_temporal_templates();
    if(!impl_->affine_coefficients.empty() && bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)
        impl_->drop_affine_coefficients();
    // Deferred detector state is promised even before it is allocated.
    // Reclaim optional reuse against that reservation, not only live storage.
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_scheduled_families();
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_scheduled_curves();
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_scheduled_moments();
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_workers();
    if(impl_->differential_window && bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes) {
        decltype(impl_->differential_fits)().swap(impl_->differential_fits);
        if(impl_->scheduled)decltype(impl_->scheduled->scheduled_carry_differential)().swap(impl_->scheduled->scheduled_carry_differential);
        impl_->differential_window=0;
        impl_->accounted_bytes=impl_->working_bytes()+impl_->unallocated_history_bytes()+impl_->drift_reserved+impl_->amplitude_scratch_reservation;
    }
    if(impl_->drift_sections>1 && bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes) {
        // Whole-symbol coherent fits have always been retained; reducing an
        // optional detector's reservation never resets physical reception.
        decltype(impl_->drift_fits)().swap(impl_->drift_fits);
        if(impl_->scheduled) {
            decltype(impl_->scheduled->scheduled_carry_drift)().swap(impl_->scheduled->scheduled_carry_drift);
            decltype(impl_->scheduled->scheduled_quarter_ranks)().swap(impl_->scheduled->scheduled_quarter_ranks);
            decltype(impl_->scheduled->scheduled_carry_quarter_ranks)().swap(impl_->scheduled->scheduled_carry_quarter_ranks);
        }
        impl_->drift_reserved=0;impl_->drift_sections=1;
        impl_->accounted_bytes=impl_->working_bytes()+impl_->unallocated_history_bytes()+impl_->amplitude_scratch_reservation;
    }
    require(bytes>=working_bytes()+impl_->unallocated_history_bytes()+impl_->amplitude_scratch_reservation,"DSP workspace is smaller than streaming pattern state");impl_->budget=bytes;
}
void PatternCorrelator::trim_optional_workspace(){impl_->drop_lazy_colored_sources();if(impl_->lazy_state() && impl_->lazy_state()->detached_replay && !impl_->lazy_state()->detached_replay->numeric_active)impl_->lazy_drop_detached_replay();impl_->drop_lazy_projections(Impl::LazyState::ProjectionDrop::explicit_trim);impl_->drop_lazy_plans();impl_->drop_scheduled_families();impl_->drop_scheduled_curves();impl_->drop_scheduled_moments();impl_->drop_workers();}
Diagnostics PatternCorrelator::diagnostics()const {
    const auto burst=provisional();Diagnostics result;result.bit_rate=static_cast<double>(impl_->config.sample_rate)/static_cast<double>(impl_->code.symbol_samples());
    result.sample_offset=static_cast<std::size_t>(burst.first_sample);result.pattern_score=burst.score;return result;
}
PatternCorrelatorWork PatternCorrelator::work()const {
    PatternCorrelatorWork result;
    result.backend=impl_->work.backend;
    if(impl_->lazy_state()){const auto& x=*impl_->lazy_state();
        result.implicit_screened_lanes=x.screen_lanes;result.implicit_rejected_lanes=x.screen_rejections;result.implicit_refined_lanes=x.refinements;
        result.implicit_input_transforms=x.input_transforms;result.implicit_template_transforms=x.template_transforms;result.implicit_inverse_transforms=x.inverse_transforms;
        result.implicit_eligible_ranges=x.eligible_ranges;result.implicit_fallback_ranges=x.fallback_ranges;result.implicit_invalid_certificates=x.invalid_certificates;
        result.implicit_proxy_ranges=x.proxy_ranges;result.implicit_fp32_ranges=x.fp32_ranges;result.implicit_fp64_ranges=x.fp64_ranges;
        result.implicit_plan_hits=x.plan_hits;result.implicit_plan_misses=x.plan_misses;
        result.implicit_replay_samples=x.replay_samples;result.implicit_processed_samples=x.processed;result.implicit_queued_samples=x.capture_end-x.processed;
        result.implicit_history_reserved_bytes=x.history_reservation;result.implicit_resident_slots=x.slots.size();result.implicit_peak_prepared_bytes=x.peak_prepared_bytes;
    }

    result.hypotheses=impl_->work.hypotheses;
    result.lattices=impl_->work.lattices;
    result.phase_groups=impl_->work.phase_groups;
    result.peak_workspace_bytes=impl_->work.peak_workspace_bytes;
    result.drift_sections=impl_->work.drift_sections;
    result.differential_window_samples=impl_->work.differential_window_samples;
    result.samples=impl_->work.samples;
    result.cells=impl_->work.cells;
    result.segments=impl_->work.segments;
    result.frontend_seconds=impl_->work.frontend_seconds;
    result.search_seconds=impl_->work.search_seconds;
    result.kernel_seconds=impl_->work.kernel_seconds;
    result.frontend_cpu_seconds=impl_->work.frontend_cpu_seconds;
    result.search_cpu_seconds=impl_->work.search_cpu_seconds;
    result.kernel_cpu_seconds=impl_->work.kernel_cpu_seconds;
    result.validation_seconds=impl_->work.validation_seconds;
    result.validation_cpu_seconds=impl_->work.validation_cpu_seconds;
    result.pulse_statistics_seconds=impl_->work.pulse_statistics_seconds;
    result.pulse_statistics_cpu_seconds=impl_->work.pulse_statistics_cpu_seconds;
    result.affine_coefficient_cache_entries=impl_->work.affine_coefficient_cache_entries;
    result.affine_preparations=impl_->work.affine_preparations;
    result.affine_reuses=impl_->work.affine_reuses;
    result.affine_interval_fast_paths=impl_->work.affine_interval_fast_paths;
    result.affine_prepare_seconds=impl_->work.affine_prepare_seconds;
    result.affine_fit_seconds=impl_->work.affine_fit_seconds;
    result.affine_prepare_cpu_seconds=impl_->work.affine_prepare_cpu_seconds;
    result.affine_fit_cpu_seconds=impl_->work.affine_fit_cpu_seconds;
    result.amplitude_transforms=impl_->work.amplitude_transforms;
    result.amplitude_observations=impl_->work.amplitude_observations;
    result.amplitude_cache_hits=impl_->work.amplitude_cache_hits;
    result.amplitude_channel_updates=impl_->work.amplitude_channel_updates;
    result.amplitude_bins=impl_->work.amplitude_bins;
    result.amplitude_fft_samples=impl_->work.amplitude_fft_samples;
    result.amplitude_channels=impl_->work.amplitude_channels;
    result.amplitude_float_transforms=impl_->work.amplitude_float_transforms;
    result.amplitude_precision_fallbacks=impl_->work.amplitude_precision_fallbacks;
    result.amplitude_float_envelopes=impl_->work.amplitude_float_envelopes;
    result.scheduled_null_samples=impl_->work.scheduled_null_samples;
    result.scheduled_omitted_fft_blocks=impl_->work.scheduled_omitted_fft_blocks;
    result.scheduled_null_pushes=impl_->work.scheduled_null_pushes;
    result.scheduled_worker_preparations=impl_->work.scheduled_worker_preparations;
    result.scheduled_carry_samples=impl_->work.scheduled_carry_samples;
    result.scheduled_colored_guard_activations=impl_->work.scheduled_colored_guard_activations;
    result.scheduled_quarter_guard_fallbacks=impl_->work.scheduled_quarter_guard_fallbacks;
    result.scheduled_template_constructions=impl_->work.scheduled_template_constructions;
    result.scheduled_template_cache_hits=impl_->work.scheduled_template_cache_hits;
    result.scheduled_template_cache_peak_bytes=impl_->work.scheduled_template_cache_peak_bytes;
    if(impl_->scheduled) {
        auto counters=impl_->scheduled->temporal_retired;
        for(const auto& batch:impl_->scheduled->worker_temporal_templates)if(batch)counters+=batch->statistics;
        result.scheduled_temporal_peak_bytes=impl_->scheduled->temporal_peak_bytes;
        result.scheduled_temporal_attempts=counters.attempts;result.scheduled_temporal_ready=counters.ready;
        result.scheduled_temporal_fallbacks=counters.fallbacks;result.scheduled_temporal_samples=counters.consumed;
        result.scheduled_temporal_plan_reuses=counters.plan_reuses;result.scheduled_temporal_values=counters.numeric.value_pairs;
        result.scheduled_temporal_shape_terms=counters.numeric.shape_terms;
        result.scheduled_temporal_normalized_reuses=counters.normalized_reuses;
    }
    result.amplitude_publication_polls=impl_->work.amplitude_publication_polls;
    result.amplitude_unchanged_polls=impl_->work.amplitude_unchanged_polls;
    if(impl_->scheduled) {
        result.scheduled_float_moment_prepared_samples=impl_->scheduled->extra_work.scheduled_float_moment_prepared_samples;
        result.scheduled_float_moment_preparations=impl_->scheduled->extra_work.scheduled_float_moment_preparations;
        result.scheduled_float_moment_prepare_fallbacks=impl_->scheduled->extra_work.scheduled_float_moment_prepare_fallbacks;
        result.scheduled_float_moment_attempts=impl_->scheduled->extra_work.scheduled_float_moment_attempts;
        result.scheduled_float_moment_pieces=impl_->scheduled->extra_work.scheduled_float_moment_pieces;
        result.scheduled_float_moment_condition_fallbacks=impl_->scheduled->extra_work.scheduled_float_moment_condition_fallbacks;
        result.scheduled_float_moment_error_fallbacks=impl_->scheduled->extra_work.scheduled_float_moment_error_fallbacks;
        result.scheduled_float_moment_accepted_pairs=impl_->scheduled->extra_work.scheduled_float_moment_accepted_pairs;
        result.scheduled_float_moment_fp64_pairs=impl_->scheduled->extra_work.scheduled_float_moment_fp64_pairs;
        result.scheduled_float_moment_gram_fallbacks=impl_->scheduled->extra_work.scheduled_float_moment_gram_fallbacks;
        result.scheduled_exact_families=impl_->scheduled->extra_work.scheduled_exact_families;
        result.scheduled_family_groups=impl_->scheduled->extra_work.scheduled_family_groups;
        result.scheduled_family_metadata_bytes=impl_->scheduled->extra_work.scheduled_family_metadata_bytes;
        result.scheduled_family_dispatches=impl_->scheduled->extra_work.scheduled_family_dispatches;
        result.scheduled_family_lanes=impl_->scheduled->extra_work.scheduled_family_lanes;
        result.scheduled_common_peak_bytes=impl_->scheduled->extra_work.scheduled_common_peak_bytes;
        result.scheduled_common_families=impl_->scheduled->extra_work.scheduled_common_families;
        result.scheduled_common_lanes=impl_->scheduled->extra_work.scheduled_common_lanes;
        result.scheduled_common_pieces=impl_->scheduled->extra_work.scheduled_common_pieces;
        result.scheduled_common_shapes=impl_->scheduled->extra_work.scheduled_common_shapes;
        result.scheduled_common_raw_samples=impl_->scheduled->extra_work.scheduled_common_raw_samples;
        result.scheduled_common_raw_lane_samples=impl_->scheduled->extra_work.scheduled_common_raw_lane_samples;
        result.scheduled_common_affine_preparations=impl_->scheduled->extra_work.scheduled_common_affine_preparations;
        result.scheduled_common_affine_lanes=impl_->scheduled->extra_work.scheduled_common_affine_lanes;
        result.scheduled_common_gram_fallbacks=impl_->scheduled->extra_work.scheduled_common_gram_fallbacks;
        result.scheduled_common_mapped_pairs=impl_->scheduled->extra_work.scheduled_common_mapped_pairs;
        result.scheduled_common_mapped_failed_pairs=impl_->scheduled->extra_work.scheduled_common_mapped_failed_pairs;
        result.scheduled_common_mapped_lanes=impl_->scheduled->extra_work.scheduled_common_mapped_lanes;
        result.scheduled_common_affine_reused_lanes=impl_->scheduled->extra_work.scheduled_common_affine_reused_lanes;
        result.scheduled_common_phase_attempts=impl_->scheduled->extra_work.scheduled_common_phase_attempts;
        result.scheduled_common_phase_rejects=impl_->scheduled->extra_work.scheduled_common_phase_rejects;
        result.scheduled_common_phase_families=impl_->scheduled->extra_work.scheduled_common_phase_families;
        result.scheduled_common_phase_lanes=impl_->scheduled->extra_work.scheduled_common_phase_lanes;
        result.scheduled_common_raw_peak_bytes=impl_->scheduled->extra_work.scheduled_common_raw_peak_bytes;
        result.scheduled_common_raw_fit_pairs=impl_->scheduled->extra_work.scheduled_common_raw_fit_pairs;
        result.scheduled_common_raw_reused_lane_samples=impl_->scheduled->extra_work.scheduled_common_raw_reused_lane_samples;
        result.scheduled_common_raw_packed_values=impl_->scheduled->extra_work.scheduled_common_raw_packed_values;
        result.scheduled_common_raw_workspace_samples=impl_->scheduled->extra_work.scheduled_common_raw_workspace_samples;
        result.scheduled_common_simd_peak_bytes=impl_->scheduled->extra_work.scheduled_common_simd_peak_bytes;
        result.scheduled_common_simd_selected_isa=impl_->scheduled->extra_work.scheduled_common_simd_selected_isa;
        result.scheduled_common_simd_attempts=impl_->scheduled->extra_work.scheduled_common_simd_attempts;
        result.scheduled_common_simd_samples=impl_->scheduled->extra_work.scheduled_common_simd_samples;
        result.scheduled_common_simd_lane_samples=impl_->scheduled->extra_work.scheduled_common_simd_lane_samples;
        result.scheduled_common_simd_packed_values=impl_->scheduled->extra_work.scheduled_common_simd_packed_values;
        result.scheduled_common_simd_guard_updates=impl_->scheduled->extra_work.scheduled_common_simd_guard_updates;
        result.scheduled_common_simd_outer_updates=impl_->scheduled->extra_work.scheduled_common_simd_outer_updates;
        result.scheduled_common_simd_short_samples=impl_->scheduled->extra_work.scheduled_common_simd_short_samples;
        result.scheduled_common_simd_workspace_samples=impl_->scheduled->extra_work.scheduled_common_simd_workspace_samples;
        result.scheduled_common_simd_isa_samples=impl_->scheduled->extra_work.scheduled_common_simd_isa_samples;
        result.scheduled_common_simd_scalar_lane_samples=impl_->scheduled->extra_work.scheduled_common_simd_scalar_lane_samples;
        result.scheduled_common_phase_address_hits=impl_->scheduled->extra_work.scheduled_common_phase_address_hits;
        result.scheduled_common_phase_address_misses=impl_->scheduled->extra_work.scheduled_common_phase_address_misses;
        result.scheduled_common_phase_address_queries=impl_->scheduled->extra_work.scheduled_common_phase_address_queries;
        result.scheduled_float_gram_attempts=impl_->scheduled->extra_work.scheduled_float_gram_attempts;
        result.scheduled_float_gram_accepted_pairs=impl_->scheduled->extra_work.scheduled_float_gram_accepted_pairs;
        result.scheduled_float_gram_range_fallbacks=impl_->scheduled->extra_work.scheduled_float_gram_range_fallbacks;
        result.scheduled_float_gram_rank_fallbacks=impl_->scheduled->extra_work.scheduled_float_gram_rank_fallbacks;
        result.scheduled_float_gram_error_fallbacks=impl_->scheduled->extra_work.scheduled_float_gram_error_fallbacks;
        result.scheduled_float_gram_helper_fallbacks=impl_->scheduled->extra_work.scheduled_float_gram_helper_fallbacks;
        result.scheduled_float_gram_signal_attempts=impl_->scheduled->extra_work.scheduled_float_gram_signal_attempts;
        result.scheduled_float_gram_maximum_relative_error=impl_->scheduled->extra_work.scheduled_float_gram_maximum_relative_error;
    }
    result.drift_sections=impl_->drift_sections;
    if(impl_->scheduled) {
        auto& target=result;
        for(const auto& c:impl_->scheduled->float_grams) {
            target.scheduled_float_gram_attempts+=c.attempts;
            target.scheduled_float_gram_accepted_pairs+=c.accepted;
            target.scheduled_float_gram_range_fallbacks+=c.range;
            target.scheduled_float_gram_rank_fallbacks+=c.rank;
            target.scheduled_float_gram_error_fallbacks+=c.error;
            target.scheduled_float_gram_helper_fallbacks+=c.helper;
            target.scheduled_float_gram_signal_attempts+=c.signal_attempts;
            target.scheduled_float_gram_maximum_relative_error=std::max(target.scheduled_float_gram_maximum_relative_error,c.maximum_relative_error);
        }
    }
    if(impl_->scheduled)for(const auto& count:impl_->scheduled->float_counts) {
        result.scheduled_float_moment_attempts+=count.attempts;
        result.scheduled_float_moment_pieces+=count.pieces;
        result.scheduled_float_moment_condition_fallbacks+=count.condition_fallbacks;
        result.scheduled_float_moment_error_fallbacks+=count.error_fallbacks;
        result.scheduled_float_moment_accepted_pairs+=count.accepted_pairs;
        result.scheduled_float_moment_fp64_pairs+=count.fp64_pairs;
        result.scheduled_float_moment_gram_fallbacks+=count.gram_fallbacks;
    }
    result.differential_window_samples=impl_->differential_window;
    if(impl_->scheduled)result.scheduled_template_constructions+=std::accumulate(
        impl_->scheduled->template_uncached.begin(),impl_->scheduled->template_uncached.end(),std::uint64_t{0});
    if(impl_->scheduled)for(const auto& cache:impl_->scheduled->worker_template_caches)if(cache) {
        result.scheduled_template_cache_hits+=cache->hits;result.scheduled_template_constructions+=cache->constructions;
    }
    return result;
}
}
