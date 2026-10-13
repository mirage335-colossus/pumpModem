#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/scheduled_symbol_calendar.hpp"
#include "search_parallel.hpp"
#include "pattern_correlator_batch.hpp"
#include "pattern_projection_cache.hpp"
#include "pattern_envelope.hpp"
#include "search_fft.hpp"
#include "audio_trace.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <set>
#include <chrono>
#include <bit>
#include <ctime>
#include <atomic>
#include <mutex>
#include <utility>
#include <openssl/crypto.h>

namespace datapump::modem {
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t block_size = 128;
constexpr std::size_t scheduled_block_size = 512;
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
struct Bank {
    double frequency=0;
    std::vector<Projection> prefix;
    std::vector<Complex> first_moment;
    std::vector<detail::CorrelationCarrierMoments> affine_carrier;
    std::array<std::uint64_t,4> affine_lengths{};
    std::span<const Projection> shared_prefix;
    std::span<const Complex> shared_moment;
    std::span<const Projection> observations() const {return shared_prefix.empty()?std::span<const Projection>(prefix):shared_prefix;}
    std::span<const Complex> moments() const {return shared_moment.empty()?std::span<const Complex>(first_moment):shared_moment;}
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
    PatternCorrelatorWork work;
    std::unique_ptr<PatternAmplitudeGeometry> amplitude_geometry;
    // The compact ordinary receiver has a strict small idle footprint. Sparse
    // calendar state belongs only to scheduled reception; its object
    // and every owned allocation are charged to that bank's workspace below.
    struct ScheduledCarry {
        std::array<Fit,2> fits{};
        std::uint64_t observed_start=0;
    };
    struct ScheduledMoment {
        Complex signal1{},square1{},square2{};
        double norm1=0,norm2=0;
    };
    template<class Scalar> struct ScheduledCurveCacheT {
        struct Key {
            std::uint64_t epoch=0,ordinal=0,chip=0;
            std::uint32_t gate_phase=0;
            bool operator==(const Key&) const = default;
        };
        struct Row {
            Key key;
            std::array<std::array<std::complex<Scalar>,2>,257> knots{};
            std::atomic<unsigned> readers{0};
            std::uint64_t stamp=0;
            bool valid=false;
        };
        struct Lease {
            Row* row=nullptr;
            Lease()=default;
            explicit Lease(Row* p):row(p){}
            Lease(const Lease&)=delete;
            Lease& operator=(const Lease&)=delete;
            Lease(Lease&& other) noexcept:row(std::exchange(other.row,nullptr)){}
            Lease& operator=(Lease&& other) noexcept {
                if(row)row->readers.fetch_sub(1,std::memory_order_release);
                row=std::exchange(other.row,nullptr);return *this;
            }
            ~Lease(){if(row)row->readers.fetch_sub(1,std::memory_order_release);}
        };
        std::array<Row,8> rows;
        std::mutex mutex;
        std::uint64_t stamp=0,hits=0,misses=0,evictions=0,exhaustions=0;
        ~ScheduledCurveCacheT(){for(auto& row:rows)OPENSSL_cleanse(row.knots.data(),sizeof(row.knots));}
        Lease get(Key key,PatternCode& pattern,std::uint64_t first_chip,std::uint64_t chip,std::stop_token stop) {
            std::lock_guard lock(mutex);
            for(auto& row:rows)if(row.valid && row.key==key) {
                ++hits;row.stamp=++stamp;row.readers.fetch_add(1,std::memory_order_relaxed);return Lease(&row);
            }
            ++misses;Row* selected=nullptr;
            for(auto& row:rows)if(!row.readers.load(std::memory_order_acquire) &&
                (!selected || !row.valid || (selected->valid && row.stamp<selected->stamp)))selected=&row;
            if(!selected){++exhaustions;return {};}
            if(selected->valid)++evictions;
            selected->valid=false;
            for(std::size_t k=0;k<selected->knots.size();++k) {
                if((k&31U)==0)cancelled(stop);
                const auto within=(static_cast<long double>(key.chip)+static_cast<long double>(k)/256)*chip;
                const auto values=pattern.shaped_values(first_chip,static_cast<double>(within));
                for(unsigned bit=0;bit<2;++bit)selected->knots[k][bit]={static_cast<Scalar>(values[bit].real()),static_cast<Scalar>(values[bit].imag())};
            }
            selected->key=key;selected->stamp=++stamp;selected->valid=true;
            selected->readers.store(1,std::memory_order_relaxed);return Lease(selected);
        }
        static bool evaluate(const Lease& view,long double within,std::uint64_t chip,std::array<Complex,2>& result) {
            if(!view.row)return false;
            const auto position=(within-static_cast<long double>(view.row->key.chip)*chip)*256/chip;
            if(position<0 || position>=256)return false;
            const auto k=static_cast<std::size_t>(std::floor(position));
            // The finite pulse has closed, nonzero support endpoints at the
            // half-chip knot. Never smear either one-sided discontinuity.
            if(k==127 || k==128)return false;
            const auto fraction=static_cast<Scalar>(position-k);
            for(unsigned bit=0;bit<2;++bit) {
                const auto value=view.row->knots[k][bit]*(Scalar{1}-fraction)+view.row->knots[k+1][bit]*fraction;
                result[bit]={static_cast<double>(value.real()),static_cast<double>(value.imag())};
            }
            return true;
        }
    };
    using ScheduledCurveCache=ScheduledCurveCacheT<double>;
    using ScheduledFloatCurveCache=ScheduledCurveCacheT<float>;
    struct ScheduledState {
        PatternScheduledComplexGeometry complex_geometry;
        std::vector<std::unique_ptr<SchedHopSourceMap>> worker_maps;
        std::vector<std::unique_ptr<ScheduledTemplateCache>> worker_template_caches;
        // Key-independent physical carrier moments for one bounded input
        // block. No source-time concatenation and no symbol-length buffering.
        std::vector<std::vector<ScheduledMoment>> moments;
        bool moments_eligible=false;
        std::unique_ptr<ScheduledCurveCache> curves;
        std::unique_ptr<ScheduledFloatCurveCache> float_curves;
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
    };
    std::unique_ptr<ScheduledState> scheduled;
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
    struct Emission {
        const Hypothesis* owner=nullptr;
        std::uint64_t first_symbol=0,last_symbol=0;
        long double origin=0;
        double frequency=0,reported_frequency=0,carrier_score=0;
        bool ended=false;
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
        std::uint64_t next=0;
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
            !options.scheduled_moment_reference && !scheduled_affine && shaped && !outer_presence &&
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
        history.reserve(search.candidate_limit);bursts.reserve(search.track_limit);
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
        accounted_bytes=working_bytes()+drift_reserved+amplitude_scratch_reservation;
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"clock-search state exceeds DSP workspace");
        work.backend=options.amplitude_only?PatternCorrelationBackend::amplitude:pulse_segmented?PatternCorrelationBackend::pulse_segments:pulse_lattices.empty()?PatternCorrelationBackend::raw:
            code.chip_samples()>4096?PatternCorrelationBackend::pulse_moments:PatternCorrelationBackend::pulse;
        work.hypotheses=hypotheses.size();work.lattices=pulse_lattices.size();work.phase_groups=alternate_groups+1;
        work.affine_coefficient_cache_entries=affine_coefficients.size();
        work.amplitude_bins=amplitude_geometry?amplitude_geometry->bin_count:0;work.amplitude_fft_samples=amplitude_geometry?amplitude_geometry->fft_samples:0;
        work.amplitude_channels=amplitude_channels.size();
        work.peak_workspace_bytes=sizeof(PatternCorrelator)+accounted_bytes;
        if(scheduled_map() && !options.amplitude_only)audio::trace::record("split_complex_constructed",[&](auto& f) {
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
                scheduled->moments_eligible?"mapped physical-IQ bounded pulse moments/source curves when workspace permits; direct fallback":"mapped physical-IQ original direct contraction");
            f.add("resolved_search_operands",scheduled->moments_eligible?
                "FP64 private coefficients and curve interpolation; FP64 physical projections; original direct fallback":"FP64 original limited templates; FP64 physical projections");
            f.add("score_precision","FP64 real I/Q Gram; source-quarter/window guards");
            f.add("template_reuse",scheduled->moments_eligible?
                "eligible bounded source-chip curves and intervals; exact worker caches; direct boundary/resource fallback":"exact per-worker private chip cache; approximate interpolation bypassed");
        });
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
        if(!scheduled_map())return (physical-symbol_start(h))*h.rate+static_cast<long double>(h.index)*code.symbol_samples();
        const auto at=scheduled_map()->locate(scheduled_tick(h,physical)).source;
        return at.distance(scheduled->anchors[static_cast<std::size_t>(&h-hypotheses.data())]);
    }
    long double scheduled_boundary(const Hypothesis& h,long double source,bool end) const {
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
        drift_reserved=0;accounted_bytes=working_bytes()+amplitude_scratch_reservation;
        room_for(0);
    }
    void prepare_workers() {
        if(scheduled && scheduled->moments_eligible && !scheduled->curves && !scheduled->float_curves &&
           config.scramble && config.spreading_mode==SpreadingMode::pattern && code.symbol_samples()<(1ULL<<40)) {
            // Arithmetic width is independent of curve/template reuse. The
            // native-float prototype was slower in complete paired execution.
            const bool native_float=options.scheduled_float_curve_experiment;
            const auto bytes=native_float?sizeof(ScheduledFloatCurveCache):sizeof(ScheduledCurveCache);
            if(bytes<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                if(native_float)scheduled->float_curves=std::make_unique<ScheduledFloatCurveCache>();
                else scheduled->curves=std::make_unique<ScheduledCurveCache>();
                accounted_bytes=working_bytes()+drift_reserved+amplitude_scratch_reservation;
                work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            }
        }
        if(scheduled && scheduled->moments_eligible && scheduled->moments.empty()) {
            const auto extra=banks.size()*(sizeof(std::vector<ScheduledMoment>)+(block_samples+1)*sizeof(ScheduledMoment));
            if(extra<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                scheduled->moments.resize(banks.size());
                for(auto& row:scheduled->moments)row.resize(block_samples+1);
                accounted_bytes=working_bytes()+drift_reserved+amplitude_scratch_reservation;
                require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"mapped moment scratch exceeds DSP workspace");
                work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            }
        }
        if(options.amplitude_only)return;
        if(!pulse_lattices.empty())return;
        // A serial bank can retain one exact template cache without any
        // worker_codes. It is already prepared too: replacing it here would
        // lose counters and double-charge its retained allocation each push.
        if(!worker_codes.empty() || (scheduled && !scheduled->worker_template_caches.empty()))return;
        const auto concurrency=std::min(hypotheses.size(),detail::search_concurrency(search.worker_threads));
        const auto per_worker=code.working_bytes()+(scheduled_map()?sizeof(SchedHopSourceMap)+sizeof(std::unique_ptr<SchedHopSourceMap>):0);
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
            accounted_bytes=working_bytes()+drift_reserved+amplitude_scratch_reservation;
            require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"parallel pattern caches exceed DSP workspace");
            work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
        }
        if(scheduled_map() && scheduled->scheduled_cache_possible && !options.scheduled_template_cache_reference) {
            const auto count=worker_codes.empty()?1:worker_codes.size();
            const auto pointers=count*sizeof(std::unique_ptr<ScheduledTemplateCache>);
            if(pointers<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                scheduled->worker_template_caches.resize(count);
                accounted_bytes=working_bytes()+drift_reserved+amplitude_scratch_reservation;
                for(auto& cache:scheduled->worker_template_caches)if(sizeof(ScheduledTemplateCache)<=budget-sizeof(PatternCorrelator)-accounted_bytes) {
                    cache=std::make_unique<ScheduledTemplateCache>();accounted_bytes+=sizeof(ScheduledTemplateCache);
                    work.scheduled_template_cache_peak_bytes=std::max(work.scheduled_template_cache_peak_bytes,
                        static_cast<std::size_t>(std::count_if(scheduled->worker_template_caches.begin(),scheduled->worker_template_caches.end(),[](const auto& item){return static_cast<bool>(item);})) * sizeof(ScheduledTemplateCache));
                }
                work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
            }
        }
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
            if(scheduled->curves)value+=sizeof(ScheduledCurveCache);
            if(scheduled->float_curves)value+=sizeof(ScheduledFloatCurveCache);
            value+=scheduled->moments.capacity()*sizeof(std::vector<ScheduledMoment>);
            for(const auto& row:scheduled->moments)value+=row.capacity()*sizeof(ScheduledMoment);
        }
        for(const auto& bank:banks)value+=bank.prefix.capacity()*sizeof(Projection)+bank.first_moment.capacity()*sizeof(Complex)+
            bank.affine_carrier.capacity()*sizeof(detail::CorrelationCarrierMoments);
        value+=worker_bytes();
        return value;
    }
    std::size_t projection_cache_headroom(std::size_t input_samples) const {
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
            bytes+=scheduled->worker_maps.capacity()*sizeof(std::unique_ptr<SchedHopSourceMap>)+
                scheduled->worker_template_caches.capacity()*sizeof(std::unique_ptr<ScheduledTemplateCache>);
            for(const auto& cache:scheduled->worker_template_caches)if(cache)bytes+=sizeof(ScheduledTemplateCache);
            for(const auto& map:scheduled->worker_maps)bytes+=map->working_bytes();
        }
        for(const auto& worker:worker_codes)bytes+=worker.working_bytes()-sizeof(PatternCode);
        return bytes;
    }
    void drop_workers() {
        if(scheduled)for(const auto& cache:scheduled->worker_template_caches)if(cache) {
            work.scheduled_template_cache_hits+=cache->hits;work.scheduled_template_constructions+=cache->constructions;
        }
        accounted_bytes-=worker_bytes();
        std::vector<PatternCode>().swap(worker_codes);
        if(scheduled) {
            decltype(scheduled->worker_template_caches)().swap(scheduled->worker_template_caches);
            decltype(scheduled->worker_maps)().swap(scheduled->worker_maps);
        }
    }
    void drop_scheduled_moments() {
        if(!scheduled || scheduled->moments.empty())return;
        auto bytes=scheduled->moments.capacity()*sizeof(std::vector<ScheduledMoment>);
        for(const auto& row:scheduled->moments)bytes+=row.capacity()*sizeof(ScheduledMoment);
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
            ptr.reset();accounted_bytes-=sizeof(Cache);
        };
        drop(scheduled->curves);drop(scheduled->float_curves);
    }
    void drop_affine_coefficients() {
        accounted_bytes-=affine_coefficients.capacity()*sizeof(AffineCoefficients);
        decltype(affine_coefficients)().swap(affine_coefficients);
        work.affine_coefficient_cache_entries=0;
    }
    void room_for(std::size_t extra) {
        // Called outside affine accumulation, before retained payload/event
        // allocations. No private coefficient pointer survives this eviction.
        if(scheduled && (scheduled->curves || scheduled->float_curves) && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_scheduled_curves();
        if(scheduled && !scheduled->moments.empty() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_scheduled_moments();
        if(!affine_coefficients.empty() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_affine_coefficients();
        if((!worker_codes.empty() || (scheduled && !scheduled->worker_template_caches.empty())) && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
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
        if(evidence.score<search.retain_score)return;
        evidence.admission_threshold=threshold();
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
        // Choose once before immutable output starts. Other hypotheses retain
        // their evidence but cannot replace already delivered stream bytes.
        const Hypothesis* owner=&h;
        for(const auto& candidate:hypotheses)if(candidate.admitted && same_clock(candidate.origin,candidate.burst.frequency_hz) &&
            (candidate.burst.stream_first_symbol<owner->burst.stream_first_symbol ||
             (candidate.burst.stream_first_symbol==owner->burst.stream_first_symbol && candidate.sum_score>owner->sum_score)))owner=&candidate;
        Emission emission{owner,owner->burst.stream_first_symbol,owner->burst.first_stream_symbol,
            owner->origin,owner->burst.frequency_hz,owner->burst.frequency_hz,owner->burst.score,false};
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
        for(const auto& candidate:hypotheses) {
            if(!candidate.admitted || candidate.burst.stream_first_symbol!=owner.burst.stream_first_symbol)continue;
            const auto difference=candidate.committed_end>owner.committed_end?
                candidate.committed_end-owner.committed_end:owner.committed_end-candidate.committed_end;
            if(difference>code.symbol_samples() || std::abs(candidate.origin-owner.origin)>=code.symbol_samples()/3.L ||
               ((!search.hypotheses.empty() && search.frequency_rate_competition)?false:
                std::abs(candidate.burst.frequency_hz-owner.burst.frequency_hz)>
                    config.sample_rate/static_cast<double>(code.symbol_samples())))continue;
            if(candidate.burst.score>best->burst.score)best=&candidate;
        }
        // Nearby clocks may retire in a different order at the terminal
        // absence observation. Their retirement must not revert an already
        // stronger accumulated carrier estimate to the startup hypothesis.
        if(best->burst.score>=stream.carrier_score) {
            stream.carrier_score=best->burst.score;stream.reported_frequency=best->burst.frequency_hz;
        }
        return stream.reported_frequency;
    }
    void publish(Hypothesis& h,bool complete=false,bool flush=true,bool draining=false) {
        if(!h.admitted)return;
        if(draining && !h.committed)return;
        const auto chunk=std::min(search.chunk_bits,bit_limit);
        if(!complete && !flush && h.committed<chunk)return;
        auto& stream=output_stream(h);
        if(stream.owner!=&h) {
            h.burst.bits.erase(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(h.committed));
            h.burst.first_stream_symbol+=h.committed;h.burst.first_sample=h.committed_end;h.committed=0;
            return;
        }
        do {
            const auto count=std::min(h.committed,chunk);
            if(!count && !complete)break;
            if(draining && bursts.size()==search.track_limit)break;
            room_for(count);
            PatternBurst result;
            result.first_sample=h.burst.first_sample;result.first_stream_symbol=h.burst.first_stream_symbol;
            result.stream_first_sample=h.burst.stream_first_sample;result.stream_first_symbol=h.burst.stream_first_symbol;
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
            h.gap_slots=0;h.burst.first_sample=resumed_sample;h.burst.first_stream_symbol=h.index;return;
        }
        require(bursts.size()<search.track_limit,"pattern output queue requires draining");
        PatternBurst event;
        event.first_sample=h.burst.first_sample;event.end_sample=resumed_sample;
        event.first_stream_symbol=h.burst.first_stream_symbol;
        event.stream_first_sample=h.burst.stream_first_sample;event.stream_first_symbol=h.burst.stream_first_symbol;
        event.frequency_hz=carrier_estimate(h,stream);event.score=h.burst.score;
        event.support_samples=h.burst.support_samples;
        event.stream_phase_samples=h.burst.stream_phase_samples;event.missing_slots=h.gap_slots;
        bursts.push_back(std::move(event));h.gap_slots=0;
        h.burst.first_sample=resumed_sample;h.burst.first_stream_symbol=h.index;stream.last_symbol=h.index;
    }
    TimingState* timing_state(std::size_t hypothesis) {
        if(!timing)return nullptr;
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
        const auto it=std::lower_bound(timing->begin(),timing->end(),hypothesis,
            [](const TimingEntry& entry,std::size_t index){return entry.hypothesis<index;});
        timing->insert(it,TimingEntry{hypothesis,{}});
    }
    void timing_decision(Hypothesis& h,std::size_t hypothesis,const PatternEvidence& evidence,std::size_t selected) {
        if(!timing_enabled)return;
        if(h.admitted) {
            subscribe_timing(hypothesis);
            for(unsigned side=0;side<2;++side) {
                if((!side && !hypothesis) || (side && hypothesis+1==hypotheses.size()))continue;
                const auto j=side?hypothesis+1:hypothesis-1;const auto& neighbor=hypotheses[j];
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
                relation.next=state.first;relation.owner_score=relation.guide_score=0;relation.invalid=false;
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
    void settle_timing() {
        if(!timing || !timing_dirty)return;
        timing_dirty=false;
        for(auto& entry:*timing) {
            auto& owner=hypotheses[entry.hypothesis];auto& state=entry.state;
            if(!state.tracking || !owner.admitted)continue;
            TimingRelation* best=nullptr;
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
                    ++relation.next;
                }
                if(!relation.invalid && relation.next>state.last_accepted &&
                    relation.guide_score>relation.owner_score &&
                    (!best || relation.guide_score-relation.owner_score>best->guide_score-best->owner_score))best=&relation;
            }
            if(!state.pending_terminal)continue;
            if(best) {
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
    void complete(Hypothesis& h,std::size_t hypothesis,std::uint64_t end,std::stop_token stop) {
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
        const auto symbol_support=pattern_symbol_support(
            static_cast<double>(fits(h,hypothesis,selected)[e.bit].count),e.score,code.chip_samples());
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
        accounted_bytes=working_bytes()+amplitude_scratch_reservation;
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
        WorkTimer fit_timer(options.measure_affine_work,work.affine_fit_seconds,work.affine_fit_cpu_seconds);
        const auto right=left+static_cast<std::size_t>(piece_count);
        const auto prefix=bank.observations();const auto moments=bank.moments();
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
    bool fit_mapped_piece(std::size_t frequency,std::size_t left,std::size_t count,
            const std::array<Complex,2>& value,const std::array<Complex,2>& slope,
            std::array<Fit,2>& fit,std::array<DriftFit,2>* drift,
            std::array<DifferentialFit,2>* differential,
            std::array<detail::CorrelationChipEvidence,2>* chip,std::uint64_t chip_index) {
        const auto& prefix=scheduled->moments[frequency];
        const auto& a=prefix[left];const auto& b=prefix[left+count];
        const auto p=banks[frequency].observations()[left+count]-banks[frequency].observations()[left];
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
        return true;
    }
    std::uint64_t accumulate_scheduled(Hypothesis& h,std::size_t hypothesis,std::uint64_t cursor,
            std::uint64_t end,PatternCode& pattern,std::stop_token stop) {
        const auto symbol_source=scheduled_source_start(h,h.index);
        const auto symbol_end=clock_boundary(h,code.symbol_samples());
        const auto limit=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(symbol_end)));
        require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
            "pattern chip coordinate overflow");
        bool pattern_ready=false;
        auto prepared_chip=std::numeric_limits<std::uint64_t>::max();
        auto* cache=scheduled_template_cache();
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
            const auto tick=scheduled_tick(h,observed);
            const auto location=scheduled_map()->locate(tick);
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
            const bool piece_flat=(scheduled_affine || !scheduled->moments.empty()) && scheduled_flat(h,piece_first,piece_end,false);
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
                    if(!pattern_ready){pattern.set_stream_phase_samples(h.phase_lower);pattern_ready=true;}
                    if(target_chip!=prepared_chip){pattern.prepare_symbol(target_chip,stop);prepared_chip=target_chip;}
                };
                const auto shape=[&](long double at) {
                    std::array<Complex,2> result;
                    const auto local=static_cast<std::uint64_t>(at/code.chip_samples());
                    const auto partial=code.symbol_samples()%code.chip_samples();
                    const auto final_chip=code.chips_per_symbol()-1;
                    const bool shifted_tail=partial && final_chip>=local && final_chip-local<=8;
                    if((scheduled->curves || scheduled->float_curves) && !shifted_tail && local<code.chips_per_symbol()) {
                        const auto use=[&](auto& cache,auto& lease) {
                            if(curve_index!=target_index || curve_chip!=local) {
                                lease={}; // Release the obsolete row before seeking an evictable slot.
                                // Same canonical crypto address reproduces both
                                // private candidates and the full outer permutation.
                                const auto address=scheduled_map()->source_to_physical(scheduled->anchors[hypothesis].add_samples(
                                    symbol_schedule_detail::multiply(target_index,code.symbol_samples())));
                                require(address.fraction==0,"scheduled curve symbol address must be integral");
                                const auto phase=static_cast<std::uint32_t>(address.sample%config.sample_rate);
                                const auto gate_phase=config.fake_hopping && config.fake_hopping->matched_gaps?phase:0U;
                                lease=cache->get({address.sample/config.sample_rate,phase/code.symbol_samples(),local,gate_phase},
                                    pattern,target_chip,code.chip_samples(),stop);
                                curve_index=target_index;curve_chip=local;
                            }
                            return cache->evaluate(lease,at,code.chip_samples(),result);
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
                    if(count>1) {
                        prepare();const auto first=shape(within);
                        const auto last=shape(within+(count-1)*h.rate);
                        const auto radius=pattern_pcm_radius_limit/amplitude;
                        bool unclipped=true;
                        for(unsigned bit=0;bit<2;++bit)unclipped&=std::norm(first[bit])<radius*radius*(1-1e-10) && std::norm(last[bit])<radius*radius*(1-1e-10);
                        if(unclipped) {
                            std::array<Complex,2> slope;
                            for(unsigned bit=0;bit<2;++bit)slope[bit]=(last[bit]-first[bit])/static_cast<double>(count-1);
                            bool fitted=false;
                            if(!scheduled->moments.empty())fitted=fit_mapped_piece(h.frequency,left,count,first,slope,target_fit,
                                target_drift,target_differential,target_chips,local);
                            else {
                                fit_affine_piece(banks[h.frequency],left,count,first,slope,target_fit,
                                    target_drift,target_differential,target_chips,local);fitted=true;
                            }
                            if(fitted){++affine_counts[hypothesis];observed+=count-1;continue;}
                        }
                    }
                }
                if(!valid || target_outer)prepare();
                const auto projection=banks[h.frequency].observations()[left+1]-banks[h.frequency].observations()[left];
                std::array<Complex,2> phases{};
                if(valid){phases=row->value[left];++cache->hits;}
                else {
                    if(interval_reuse && observed<interval_end && interval_index==target_index) {
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
                        phases=shape(within);
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
                if(target_outer)pattern_pulse_each(static_cast<double>(within),code.symbol_samples(),code.chip_samples(),
                    [&](std::uint64_t local,double pulse) {
                        target_outer->add(local,gain*pulse*Complex{projection.xc,projection.xs},
                            [&](std::uint64_t position){return pattern.values(target_chip+position);});
                    });
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
                            const auto p=bank.observations()[right]-bank.observations()[left];
                            const Complex measured{p.xc,p.xs};
                            const auto moment=bank.moments()[right]-bank.moments()[left]-static_cast<double>(left)*measured;
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
                    const auto projection=bank.observations()[left+1]-bank.observations()[left];
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
                    const auto p=banks[h.frequency].observations()[right]-banks[h.frequency].observations()[left];
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
                    const auto projection=bank.observations()[right]-bank.observations()[left];
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
            const auto prefix=bank.observations();const auto moments=bank.moments();
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
            const auto prefix=banks[lattice.frequency].observations();
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
        const auto retained=sizeof(PatternCorrelator)+working_bytes()+drift_reserved;
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
        struct ClearSharedViews {
            std::vector<Bank>& banks;bool enabled=true;
            ~ClearSharedViews(){if(enabled)for(auto& bank:banks){bank.shared_prefix={};bank.shared_moment={};}}
        } clear_shared{banks};
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
            auto& out=scheduled->moments[b];out[0]={};
            const auto prefix=banks[b].observations();
            for(std::size_t i=0;i<input.size();++i) {
                const auto p=prefix[i+1]-prefix[i];const auto n=static_cast<double>(i),n2=n*n;
                const Complex square{p.cc-p.ss,2*p.cs};auto next=out[i];
                next.signal1+=n*Complex{p.xc,p.xs};next.square1+=n*square;next.square2+=n2*square;
                next.norm1+=n*(p.cc+p.ss);next.norm2+=n2*(p.cc+p.ss);out[i+1]=next;
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
            frontend_timer.finish();
            WorkTimer search_timer(options.measure_work,work.search_seconds,work.search_cpu_seconds);
            // There is no hypothesis work before a shared chip cell is ready.
            // In particular a high PCM rate must not rescan the timing/key
            // candidates once per tiny oscillator block of an hours-long bit.
            if(work.cells!=cells_before)accumulate_pulse_cells(stop);
            settle_timing();
            record_point(banks.front().observations()[input.size()]);sample=end;room_for(0);return;
        }
        frontend_timer.finish();
        WorkTimer search_timer(options.measure_work,work.search_seconds,work.search_cpu_seconds);
        const auto parallel=worker_codes.size()>1;
        if(parallel)detail::parallel_search(hypotheses.size(),worker_codes.size(),[&](std::size_t worker,std::size_t hypothesis) {
            auto& view=worker_map_view();const auto previous=view;
            struct RestoreMapView {decltype(view) target;decltype(previous) old;~RestoreMapView(){target=old;}} restore{view,previous};
            if(scheduled && !scheduled->worker_maps.empty())view={this,scheduled->worker_maps[worker].get()};
            auto& template_view=worker_template_view();const auto previous_template=template_view;
            struct RestoreTemplateView {decltype(template_view) target;decltype(previous_template) old;~RestoreTemplateView(){target=old;}} restore_template{template_view,previous_template};
            if(scheduled && !scheduled->worker_template_caches.empty())template_view={this,scheduled->worker_template_caches[worker].get()};
            cancelled(stop);auto& h=hypotheses[hypothesis];
            const auto cursor=initial_cursor(h,end);
            const auto symbol_end=clock_boundary(h,code.symbol_samples());
            if(cursor<end && static_cast<long double>(cursor)<symbol_end)
                accumulate(h,hypothesis,cursor,end,worker_codes[worker],stop);
        });
        for(std::size_t hypothesis=0;hypothesis<hypotheses.size();++hypothesis) {
            auto& h=hypotheses[hypothesis];
            cancelled(stop);auto cursor=initial_cursor(h,end);
            // Only independent fit accumulation was moved ahead. Trial counts,
            // phase selection, peer ownership and every publication retain the
            // original serial hypothesis/complete-symbol order.
            if(parallel && cursor<end) {
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
        if(!certified_null)record_point(banks.front().observations()[input.size()]);
        sample=end;
        room_for(0);
    }
};

PatternCorrelator::PatternCorrelator(Config c,PatternSearch search,std::size_t bytes,PatternCorrelatorOptions options)
    :impl_(std::make_unique<Impl>(c,std::move(search),bytes,options)){}
PatternCorrelator::~PatternCorrelator(){
    if(impl_ && impl_->scheduled_map() && !impl_->options.amplitude_only && audio::trace::active.load()) {
        const auto w=work();audio::trace::record("split_complex_work",[&](auto& f) {
            f.add("epoch",impl_->config.stream_epoch);f.add("samples",w.samples);f.add("hypotheses",w.hypotheses);
            f.add("null_samples",w.scheduled_null_samples);f.add("null_pushes",w.scheduled_null_pushes);
            f.add("worker_preparations",w.scheduled_worker_preparations);f.add("template_constructions",w.scheduled_template_constructions);
            f.add("template_cache_hits",w.scheduled_template_cache_hits);f.add("carry_samples",w.scheduled_carry_samples);
            f.add("colored_guard_activations",w.scheduled_colored_guard_activations);
            f.add("quarter_guard_fallbacks",w.scheduled_quarter_guard_fallbacks);f.add("pulse_spans",w.segments);
            f.add("counter_scope","per-sample template pairs; span endpoints/worker setup are separate");
            const auto& state=*impl_->scheduled;const auto* curves=state.curves.get();const auto* floats=state.float_curves.get();
            f.add("curve_hits",state.curve_hits+(curves?curves->hits:0)+(floats?floats->hits:0));
            f.add("curve_misses",state.curve_misses+(curves?curves->misses:0)+(floats?floats->misses:0));
            f.add("curve_evictions",state.curve_evictions+(curves?curves->evictions:0)+(floats?floats->evictions:0));
            f.add("curve_exhaustions",state.curve_exhaustions+(curves?curves->exhaustions:0)+(floats?floats->exhaustions:0));
            f.add("curve_arithmetic",impl_->options.scheduled_float_curve_experiment?"native FP32 table/interpolation; FP64 moments/fits":"FP64 table/interpolation/moments/fits");
            f.add("peak_workspace_bytes",w.peak_workspace_bytes);f.add("timers_enabled",impl_->options.measure_work);
            if(impl_->options.measure_work){f.add("validation_cpu_seconds",w.validation_cpu_seconds);
                f.add("frontend_cpu_seconds",w.frontend_cpu_seconds);f.add("search_cpu_seconds",w.search_cpu_seconds);}
        });
    }
}
PatternCorrelator::PatternCorrelator(PatternCorrelator&&) noexcept=default;
PatternCorrelator& PatternCorrelator::operator=(PatternCorrelator&&) noexcept=default;
void PatternCorrelator::push(std::span<const float> samples,std::stop_token stop,detail::CorrelationProjectionCache* cache) {
    auto& s=*impl_;cancelled(stop);require(!s.finished,"pattern capture already finished");
    require(samples.size()<=std::numeric_limits<std::uint64_t>::max()-s.sample,"pattern sample counter overflow");
    {
        WorkTimer validation_timer(s.options.measure_work,s.work.validation_seconds,s.work.validation_cpu_seconds);
        if(s.options.raw_reference || !cache || !cache->covers(samples))
            for(auto value:samples)require(std::isfinite(value),"nonfinite pattern sample");
    }
    if(s.options.amplitude_only){s.push_amplitude(samples,stop,cache);return;}
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
    for(std::size_t offset=0;offset<samples.size();) {
        auto count=s.process_batch(samples.subspan(offset),stop);
        if(!count) {
            count=std::min(s.block_samples,samples.size()-offset);
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
    s.amplitude_publication_dirty=true;
    s.settle_timing();
    for(auto& h:s.hypotheses) {s.publish(h);s.clear(h);}
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
    for(auto& h:s.hypotheses)s.publish(h,false,true,true);
    if(s.options.amplitude_only || s.scheduled_map()) {
        ++s.work.amplitude_publication_polls;
        // A full output queue can leave committed bits for the next poll.
        // Keep that work pending even without further PCM, including EOF.
        s.amplitude_publication_dirty=s.timing_dirty || std::any_of(s.hypotheses.begin(),s.hypotheses.end(),
            [](const auto& h){return h.committed!=0;});
        if(s.bursts.empty()) {
            s.accounted_bytes=s.working_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;return {};
        }
    }
    auto result=std::move(s.bursts);s.bursts={};s.bursts.reserve(s.search.track_limit);
    s.accounted_bytes=s.working_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;return result;
}
PatternBurst PatternCorrelator::provisional()const {
    const auto& h=impl_->hypotheses;
    const auto best=std::max_element(h.begin(),h.end(),[](const auto& a,const auto& b){return (a.admitted?a.sum_score:-1)<(b.admitted?b.sum_score:-1);});
    if(best==h.end() || !best->admitted)return {};
    auto result=best->burst;result.bits.resize(best->committed);result.end_sample=best->committed_end;
    result.complete=false;return result;
}
std::vector<PatternEvidence> PatternCorrelator::candidates()const{return impl_->history;}
std::vector<PatternEvidence> PatternCorrelator::candidates(std::size_t limit)const{
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
std::size_t PatternCorrelator::reserved_workspace_bytes()const{return sizeof(PatternCorrelator)+impl_->accounted_bytes;}
void PatternCorrelator::set_workspace_bytes(std::size_t bytes) {
    if(!impl_->affine_coefficients.empty() && bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)
        impl_->drop_affine_coefficients();
    // Deferred detector state is promised even before it is allocated.
    // Reclaim optional reuse against that reservation, not only live storage.
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_scheduled_curves();
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_scheduled_moments();
    if(bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes)impl_->drop_workers();
    if(impl_->differential_window && bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes) {
        decltype(impl_->differential_fits)().swap(impl_->differential_fits);
        if(impl_->scheduled)decltype(impl_->scheduled->scheduled_carry_differential)().swap(impl_->scheduled->scheduled_carry_differential);
        impl_->differential_window=0;
        impl_->accounted_bytes=impl_->working_bytes()+impl_->drift_reserved+impl_->amplitude_scratch_reservation;
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
        impl_->accounted_bytes=impl_->working_bytes()+impl_->amplitude_scratch_reservation;
    }
    require(bytes>=working_bytes()+impl_->amplitude_scratch_reservation,"DSP workspace is smaller than streaming pattern state");impl_->budget=bytes;
}
void PatternCorrelator::trim_optional_workspace(){impl_->drop_scheduled_curves();impl_->drop_scheduled_moments();impl_->drop_workers();}
Diagnostics PatternCorrelator::diagnostics()const {
    const auto burst=provisional();Diagnostics result;result.bit_rate=static_cast<double>(impl_->config.sample_rate)/static_cast<double>(impl_->code.symbol_samples());
    result.sample_offset=static_cast<std::size_t>(burst.first_sample);result.pattern_score=burst.score;return result;
}
PatternCorrelatorWork PatternCorrelator::work()const {
    auto result=impl_->work;result.drift_sections=impl_->drift_sections;
    result.differential_window_samples=impl_->differential_window;
    if(impl_->scheduled)result.scheduled_template_constructions+=std::accumulate(
        impl_->scheduled->template_uncached.begin(),impl_->scheduled->template_uncached.end(),std::uint64_t{0});
    if(impl_->scheduled)for(const auto& cache:impl_->scheduled->worker_template_caches)if(cache) {
        result.scheduled_template_cache_hits+=cache->hits;result.scheduled_template_constructions+=cache->constructions;
    }
    return result;
}
}
