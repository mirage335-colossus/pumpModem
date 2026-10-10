#include "datapump/simulation_estimate.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "pattern_drift.hpp"
#include "pattern_differential.hpp"
#include "pattern_correlator_batch.hpp"
#include "receiver_probability.hpp"
#include "estimate_cancellation.hpp"
#include "pattern_start_geometry.hpp"
#include "pattern_fft_window.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/correlation_experiment.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <map>
#include <sstream>

namespace datapump::simulation {
namespace {
// Fixed, deliberately rounded engineering assumptions for the named laptop.
// These are neither device peaks nor measured application throughput.
constexpr long double serial_operations_per_second = 1.5e9L;
constexpr long double cpu_scoring_operations_per_second = 8e9L;
constexpr long double gpu_scoring_operations_per_second = 80e9L;
constexpr long double gpu_transfer_bytes_per_second = 8e9L;
constexpr long double channel_operations_per_sample = 400;
constexpr long double projection_operations_per_sample = 40;
constexpr long double template_pair_operations_per_bin = 40;
constexpr long double tracking_pair_operations_per_bin = 32;
constexpr long double tracking_real_pair_operations_per_bin = 64;
constexpr long double tracking_evidence_operations_per_fit = 64;
constexpr long double differential_operations_per_window = 128;
// Bounded scalar operation allowances for the coherent outer-code guard.
// These count the added pulse dots/rank fits; they are not measured throughput.
constexpr long double outer_operations_per_sample = 17*8+64;
constexpr long double outer_operations_per_chip = 384;
// Pulse-cell fitting separates sample ingestion from private-chip work. The
// Central work follows changed table knots/counts; a separate full-cell rebuild
// allowance covers cache misses and closed endpoint/numerical edge geometries.
constexpr long double pulse_frontend_operations_per_sample = 220;
constexpr long double pulse_pair_operations_per_chip = 6000;
constexpr long double pulse_gram_operations_per_cell = 4000;
constexpr long double pulse_kernel_operations_per_sample = 16000;
constexpr long double pulse_moment_operations_per_block = 48;
constexpr long double pulse_moment_operations_per_segment = 256;
constexpr long double pulse_moment_kernel_operations_per_segment = 22000;
constexpr long double pulse_affine_pair_operations_per_segment = 1800;
// Fine paired RF/Sub9 diagnostics attributed 57--60% of uncached component
// time to private 17-atom preparation. Conservatively assign 55% of the old
// aggregate allowance to preparation, with hit-offset adjustment covered by
// fitting. Their sum remains 1800; this rounded attribution is not calibrated
// throughput or a measured duration on the user's computer. See the measured
// cases and instrumentation limitations in docs/pulse-moment-validation.md.
constexpr long double pulse_affine_prepare_operations_per_segment = 990;
constexpr long double pulse_affine_fit_operations_per_segment =
    pulse_affine_pair_operations_per_segment-pulse_affine_prepare_operations_per_segment;
static_assert(pulse_affine_prepare_operations_per_segment>0&&
    pulse_affine_fit_operations_per_segment>0);
constexpr long double model_implementation_loss_db = 3;

struct PayloadWork { long double baseline=0,mitigation=0; };
PayloadWork payload_work(const transfer::Estimate& transmission,const transfer::Options& options,bool raw) {
    // Rounded extrapolation from paired pre/post-hardening Robust workloads;
    // see docs/robust-cpu-costs.md. These are allowances for the fixed reference
    // model, not a benchmark of the user's computer or an upper runtime bound.
    // Search/DSP coefficients stay unchanged. Exceptional alignment/assignment
    // recovery depends on actual missing data and remains a separate job.
    constexpr long double us=1e-6L,ns=1e-9L;
    PayloadWork work;
    if(!transmission.wire_bits)return work;
    work.baseline=2*us;
    if(raw) {
        // The receiver attempts the same short interpretation for explicit
        // binary and dictionary transmissions, including incomplete one-bit
        // tokens. Their transmitter content_bytes need not be the same.
        work.baseline+=2*ns*std::min<std::size_t>(transmission.wire_bits,4096);
        work.mitigation=.05L*us; // Final permitted-view validation boundary.
        if(transmission.wire_bits<=transfer::short_message_bits)
            work.mitigation+=.4L*us+3*ns*transmission.wire_bits;
        return work;
    }
    const auto intervals=std::ceil(static_cast<long double>(transmission.coded_bytes)/stream_interval_bytes);
    const auto parity=interval_parity_bytes(options.fec);
    // Budget a repair-capable interval rather than only a clean syndrome pass.
    // The smaller parity tier is an extrapolation, not a separate calibration.
    const auto repair=(parity==48?60.L:parity?20.L:0.L)*us;
    // Keyed stream work includes Data unmasking/setup and bookkeeping as well
    // as interval authentication; 100 us is not an isolated HMAC measurement.
    work.baseline+=intervals*(25*us+repair+(options.key?100*us:0));
    work.mitigation+=intervals*(.05L*us+.05L*repair+(options.key?.02L*us:0));
    const auto source=static_cast<long double>(transmission.content_bytes);
    work.baseline+=5*ns*source; // Copying/permitted text view; conservative for attachments.
    if(options.compression) {
        const auto decoder=25*us+2*ns*source;
        work.baseline+=decoder;
        work.mitigation+=.05L*decoder;
    }
    return work;
}

double finite_seconds(long double value) {
    return static_cast<double>(std::min(value,static_cast<long double>(std::numeric_limits<double>::max())));
}
double probability_power(double probability,long double count) {
    if(count<=0)return 1;
    if(probability<=0)return 0;
    if(probability>=1)return 1;
    return static_cast<double>(std::exp(count*std::log(probability)));
}
double normal_above(double energy,double threshold) {
    return .5*std::erfc((threshold-energy)/std::sqrt(2*(1+2*energy)));
}
double binomial_at_most(std::size_t n,std::size_t limit,double bad) {
    if(bad<=0)return 1;
    if(bad>=1)return limit>=n?1:0;
    long double sum=0;
    for(std::size_t k=0;k<=std::min(n,limit);++k)
        sum+=std::exp(std::lgamma(static_cast<long double>(n)+1)-std::lgamma(static_cast<long double>(k)+1)-
            std::lgamma(static_cast<long double>(n-k)+1)+static_cast<long double>(k)*std::log(bad)+
            static_cast<long double>(n-k)*std::log1p(-bad));
    return std::clamp(static_cast<double>(sum),0.,1.);
}
double interval_probability(double admitted,double bit_error,FecMode fec) {
    const auto parity=interval_parity_bytes(fec);
    const auto observed=probability_power(admitted,8);
    const auto correct=probability_power(admitted*(1-bit_error),8);
    const auto erased=1-observed,wrong=std::max(0.,observed-correct);
    // One erasure or two unknown-error units per RS byte, bounded by the
    // existing fixed interval and its actual parity. No decoder is invoked.
    std::array<long double,stream_interval_bytes+1> current{},next{};
    current[0]=1;
    for(std::size_t byte=0;byte<stream_interval_bytes;++byte) {
        next.fill(0);
        for(std::size_t cost=0;cost<=parity;++cost) {
            next[cost]+=current[cost]*correct;
            if(cost+1<=parity)next[cost+1]+=current[cost]*erased;
            if(cost+2<=parity)next[cost+2]+=current[cost]*wrong;
        }
        current=next;
    }
    return std::clamp(static_cast<double>(std::accumulate(current.begin(),current.end(),0.L)),0.,1.);
}
bool same_profile(const modem::Config& a,const modem::Config& b) {
    return a.sample_rate==b.sample_rate && a.carrier_hz==b.carrier_hz && a.bandwidth_hz==b.bandwidth_hz && a.dsss_factor==b.dsss_factor &&
        (a.dsss_factor<=1||a.outer_dsss_version==b.outer_dsss_version) &&
        a.spreading_mode==b.spreading_mode && a.scramble==b.scramble && a.dsss==b.dsss &&
        a.pulse_shaping==b.pulse_shaping &&
        modem::symbol_sample_count(a)==modem::symbol_sample_count(b) &&
        modem::pattern_chip_samples(a)==modem::pattern_chip_samples(b);
}
struct SearchBank {
    modem::PatternFrequencySearch frequency;
    std::vector<modem::PatternFrequencyRateHypothesis> hypotheses;
    double requested_clock_ppm=0,clock_ppm=0,clock_step_ppm=0;
    double minimum_rate=1,maximum_rate=1;
    bool legacy_coupled=false,limited=false;
};
SearchBank search_bank(const modem::Config& config,bool utc) {
    SearchBank result;
    if(config.oscillator_search) {
        const auto plan=modem::oscillator_pattern_search(config,utc?modem::utc_transmit_rate_limit(config):0);
        result.frequency=plan.frequency;result.hypotheses=plan.hypotheses;
        result.requested_clock_ppm=plan.requested_clock_half_width_ppm;
        result.clock_ppm=plan.clock_half_width_ppm;result.clock_step_ppm=plan.clock_step_ppm;
        result.limited=plan.limited;
    } else {
        result.frequency=modem::default_pattern_frequency_search(config);
        result.legacy_coupled=result.frequency.count>5&&config.spreading_mode==modem::SpreadingMode::pattern;
        const auto offsets=modem::default_pattern_frequency_offsets(config);
        for(const auto offset:offsets)result.hypotheses.push_back({offset,0});
        if(result.legacy_coupled)for(const auto offset:offsets)
            result.hypotheses.push_back({offset,offset/config.carrier_hz*1e6});
        result.clock_ppm=result.legacy_coupled?result.frequency.half_width_hz/config.carrier_hz*1e6:0;
    }
    for(const auto& hypothesis:result.hypotheses) {
        const auto rate=1+hypothesis.clock_error_ppm*1e-6;
        result.minimum_rate=std::min(result.minimum_rate,rate);
        result.maximum_rate=std::max(result.maximum_rate,rate);
    }
    return result;
}
bool within_representation_bound(long double value,long double bound) {
    // Policy endpoints and public channel values are doubles. Round the
    // intermediate product back to that representation before comparing;
    // the next representable double outside the endpoint stays outside.
    return static_cast<double>(std::abs(value))<=static_cast<double>(bound);
}
bool paired_clock_coverage(const SearchBank& bank,long double frequency,double clock) {
    if(!within_representation_bound(clock,bank.clock_ppm))return false;
    return std::any_of(bank.hypotheses.begin(),bank.hypotheses.end(),[&](const auto& hypothesis) {
        return within_representation_bound(frequency-hypothesis.frequency_offset_hz,bank.frequency.step_hz/2)&&
            within_representation_bound(static_cast<long double>(clock)-hypothesis.clock_error_ppm,bank.clock_step_ppm/2);
    });
}
long double phase_coherence(long double x) {
    if(x<1e-4L)return 1-x/3+x*x/12;
    return 2*(1+(std::expm1(-x)/x))/x;
}
struct Work {
    long double serial=0,parallel=0,tracking_serial=0,tracking_windows=0,search_trials=1,kernel_serial=0,
        kernel_upper_serial=0,search_serial=0;
    double noise_dimensions=0,coherent_dimensions=0,section_dimensions=0,noise_condition=1;
    double timing_uncertainty_chips=0,acquisition_threshold=0;
    double projection_bin_chips=0;
    double following_search_ratio=0;
    bool drift_supported=false;
    bool differential_supported=false;
    bool compact=false,pulse_projected=false,pulse_segmented=false,kernel_upper_bound=false,outer_presence=false;
    std::uint64_t bin_samples=1;
    bool workspace_supported=true,work_supported=true;
    long double epoch_hypotheses=1,timing_hypotheses=1,full_timing_hypotheses=1;
    std::size_t phase_groups=1;
    bool timing_window_modeled=false;
    long double fft_acquisition_batches=0,fft_retained_acquisition_batches=0;
    long double new_epoch_admissions=0,new_epoch_full_fft_batches=0,new_epoch_retained_fft_batches=0;
    long double new_epoch_qualified_ready_batch_slots=0;
    long double initial_epoch_setup_operations=0,new_epoch_frontend_operations=0,new_epoch_setup_operations=0;
    long double initial_epoch_setup_scoring_operations=0,new_epoch_setup_scoring_operations=0;
    ReceiverSearchDiagnostics geometry;
};
struct RollingFftWork {
    long double admissions=0,first_batches=0,full_batches=0,input_samples=0,maximum_batches=0;
};
// Automatic Live refresh adds one fresh epoch at each observed UTC second.
// This prices the new acquisition cohorts, separately from the initial bank.
// Keep the initial cohort's conservative full observation allowance; do not
// interpret this as a whole-runtime bound (admitted/noise tracks can live longer).
RollingFftWork rolling_fft_work(long double samples,std::uint32_t sample_rate,
        long double bin,long double length,long double hop,long double initial_batch,
        long double symbol_seconds,long double prefix_seconds,unsigned epoch_radius,
        long double default_rate_uncertainty,const ReceiverTimingModel& timing_model,bool early_readiness=false) {
    RollingFftWork result;
    if(!(samples>0))return result;
    const auto seconds=samples/sample_rate;
    const auto slope=sample_rate*static_cast<long double>(
        timing_model.capture_seconds_per_frame.value_or(1./sample_rate));
    const auto uncertainty=static_cast<long double>(
        timing_model.capture_rate_uncertainty_fraction.value_or(static_cast<double>(default_rate_uncertainty)));
    const auto minimum_utc_rate=std::min(1.L,slope-uncertainty);
    const auto maximum_utc_rate=std::max(1.L,slope+uncertainty);
    if(!(minimum_utc_rate>0) || !std::isfinite(maximum_utc_rate))
        throw Error("Automatic Live epoch-refresh work requires a finite positive capture-rate interval");
    const auto cadence=1/maximum_utc_rate;
    result.admissions=std::ceil(seconds/cadence);
    const auto outward_floor=[](long double value) {
        const auto allowance=64*std::numeric_limits<long double>::epsilon()*std::max(1.L,std::abs(value));
        return std::floor(value+allowance);
    };
    const auto hop_seconds=hop*bin/sample_rate;
    const auto first_full=(length+initial_batch-1)*bin/sample_rate;
    const auto first_pass=early_readiness?length*bin/sample_rate:first_full;
    // New epochs enter no later than epoch-radius. The ORIGINAL legacy hint
    // ends at epoch+radius+1, independently of a qualified arrival prior.
    const auto last_start=(2.L*epoch_radius+1)*sample_rate/bin;
    const auto passes=last_start<initial_batch?1.L:2+outward_floor((last_start-initial_batch)/hop);
    const auto sampled_search_end=first_full+(passes-1)*hop_seconds;
    const auto retirement=(prefix_seconds+symbol_seconds+2.L*epoch_radius+1)/minimum_utc_rate;
    // Two UTC intervals conservatively cover strict retirement and integer
    // refresh boundaries. Hardware feeds are shorter than one UTC interval.
    const auto lifetime=std::max(sampled_search_end,retirement)+2/minimum_utc_rate;
    const auto lifetime_batches=1+outward_floor((lifetime-first_pass)/hop_seconds);
    result.maximum_batches=lifetime_batches;
    result.first_batches=std::clamp(outward_floor((seconds-first_pass)/cadence)+1,0.L,result.admissions);
    // Sum an upper affine envelope of each integer batch count, truncated by
    // remaining input and the legacy lifetime. O(1), including hours-long input.
    const auto ready=result.first_batches;
    const auto surplus=ready*std::max(0.L,seconds-first_pass-cadence*(ready-1)/2);
    result.full_batches=std::min(ready*lifetime_batches,
        std::ceil(std::nextafter(ready+surplus/hop_seconds,std::numeric_limits<long double>::infinity())));
    const auto capped=std::clamp(outward_floor((seconds-lifetime)/cadence)+1,0.L,result.admissions);
    const auto remaining=result.admissions-capped;
    const auto exposure=capped*lifetime+remaining*std::max(0.L,seconds-cadence*(capped+result.admissions-1)/2);
    result.input_samples=std::nextafter(exposure*sample_rate+
        64*std::numeric_limits<long double>::epsilon()*std::max(1.L,exposure*sample_rate),
        std::numeric_limits<long double>::infinity());
    if(!std::isfinite(result.admissions) || !std::isfinite(result.full_batches) ||
       !std::isfinite(result.input_samples) || result.full_batches<ready)
        throw Error("Automatic Live epoch-refresh work exceeds the finite FFT model");
    return result;
}
struct FftWindowWork {
    long double first_ready_bins=0,first_component_ready_bins=0,early_batches=0;
    long double input_transforms=0,components=0,component_fallback_hops=0,envelope_fallback_hops=0;
    long double max_input_transforms=0;
    long double paired_direct_jobs=0,partitioned_jobs=0,partitioned_input_transforms=0;
    long double partitioned_template_transforms=0,partitioned_inverse_transforms=0,paired_fallback_components=0;
    std::size_t partitioned_tile_min=0,partitioned_tile_max=0;
    long double batches=0,full_jobs=0,jobs=0,direct_jobs=0,full_positions=0,positions=0,operations=0;
    long double max_operations=0,max_jobs=0,max_positions=0,max_direct_jobs=0;
};
// Identical canonical address groups to PatternReceiver::phase_groups. The
// phase lattice and stream index remain original; only work is intersected.
std::array<std::pair<std::uint64_t,std::uint64_t>,3> fft_phase_groups(
        const modem::Config& config,std::uint64_t index,std::uint64_t upper,
        std::uint64_t step,std::size_t& count) {
    std::array<std::pair<std::uint64_t,std::uint64_t>,3> groups{};count=0;
    auto lower=std::uint64_t{0};const auto symbol=modem::symbol_sample_count(config);
    while(lower<=upper) {
        const auto address=modem::symbol_stream_address(0,lower,index,symbol,config.sample_rate);
        auto low=std::uint64_t{0},high=(upper-lower)/step;
        while(low<high) {
            const auto mid=low+(high-low+1)/2;
            const auto next=modem::symbol_stream_address(0,lower+mid*step,index,symbol,config.sample_rate);
            if(next.epoch==address.epoch&&next.ordinal==address.ordinal)low=mid;else high=mid-1;
        }
        const auto end=lower+low*step;
        if(count==groups.size())throw Error("FFT planning phase groups exceed the finite symbol interval");
        groups[count++]={lower,end};if(end==upper)break;lower=end+step;
    }
    return groups;
}
// Upper numeric-work option for every supported subset/segment-origin
// translation of an anchor envelope. This proves that runtime choose has an
// eligible option; it does not predict which tile size that runtime selects.
std::optional<long double> paired_envelope_numeric(std::span<const modem::detail::FftStartRange> ranges,
        std::size_t length,std::size_t transform,std::size_t frequencies) {
    if(ranges.empty())return std::nullopt;
    const auto span=ranges.back().first+ranges.back().count-1-ranges.front().first;
    const auto n=static_cast<long double>(transform);
    auto best=.9L*(5*n*std::log2(n)+frequencies*(20*n*std::log2(n)+12*n));
    bool found=false;
    for(std::size_t b=16;b<=transform/2&&b<=16384;b*=2) {
        modem::detail::partitioned_paired::Requirements r;
        r.tile_size=b;r.transform=2*b;r.length=length;r.chunks=1+(length-1)/b;
        const auto delta=span/b+(span%b!=0);
        std::size_t q=0;
        for(const auto range:ranges)q+=1+(range.count-1)/b+((range.count-1)%b!=0);
        r.max_job_output_tiles=std::min(q,delta+1);
        r.input_tiles=r.chunks+delta;
        const auto complex_count=(static_cast<long double>(r.input_tiles)+2.L*r.max_job_output_tiles+2)*r.transform;
        if(complex_count>2*n)continue;
        r.complex_count=static_cast<std::size_t>(complex_count);
        const auto work=modem::detail::partitioned_paired::operations(r,frequencies);
        if(work<best){best=work;found=true;}
    }
    return found?std::optional<long double>{best}:std::nullopt;
}
// Shared operations() counts the two complex products. Execution also adds
// them to sums and initializes/copies borrowed rows. No allocation is added.
long double paired_numeric_overhead(const modem::detail::partitioned_paired::Requirements& r,
        std::size_t frequencies) {
    const auto p=static_cast<long double>(r.transform);
    return 4.L*frequencies*r.chunks*r.max_job_output_tiles*p+
        4*p*(r.input_tiles+static_cast<long double>(frequencies)*(r.chunks+r.max_job_output_tiles));
}
std::optional<FftWindowWork> fft_window_work(const modem::Config& config,std::size_t frequencies,
        std::size_t bin,std::size_t length,std::size_t transform,long double fft_log,
        std::size_t hop,std::size_t initial_batch,std::size_t batches,
        const modem::PatternStartWindow& prior,bool sample_fit,bool generated,
        long double minimum_rate,long double maximum_rate,long double template_work,std::size_t& checks,std::stop_token stop,
        long double observed_bins=std::numeric_limits<long double>::infinity(),bool minimum_readiness=false,
        bool component_scheduling=false) {
    FftWindowWork result;
    const auto symbol=modem::symbol_sample_count(config);
    const auto step=std::gcd(symbol,static_cast<std::uint64_t>(config.sample_rate));
    const auto upper=(std::min(symbol,static_cast<std::uint64_t>(config.sample_rate))-1)/step*step;
    const auto radius=std::max<std::uint64_t>(1,symbol/2);
    const auto candidate_limit=modem::PatternSearch{}.candidate_limit;
    modem::detail::FftSearchGeometry geometry;geometry.sample_fit=sample_fit;
    geometry.drift_sections=1;geometry.differential_window_samples=0;
    for(std::size_t b=0;b<batches;++b) {
        estimate_detail::check(stop);
        const auto first=b?initial_batch+(static_cast<std::uint64_t>(b)-1)*hop:0;
        const auto starts=b?hop:initial_batch;
        std::array<modem::detail::PatternFftStartSelection,12> selections{};
        std::size_t selection_count=0,ready_end=0;
        bool input_fft=false,selector_uncertain=false;
        modem::detail::PatternFftComponents components;
        for(std::uint64_t index=0;index<4;++index) {
            std::size_t count=0;const auto groups=fft_phase_groups(config,index,upper,step,count);
            for(std::size_t g=0;g<count;++g) {
                if(++checks>1000000)return std::nullopt;
                const auto [lower,last]=groups[g];
                auto selected=modem::detail::pattern_fft_select_starts(prior,symbol,index,
                    lower,last,step,first,starts,bin,minimum_rate,maximum_rate);
                // Widening the anchor can merge >64 narrow phase ranges. A
                // narrower runtime map could then overflow the selector and
                // keep its full job, rather than remain a selected subset.
                // Retain full work for that uncertain group in cohort pricing.
                if(minimum_readiness&&(last-lower)/step>=64) {
                    selected={};selected.selected_count=starts;selector_uncertain=true;
                }
                selections[selection_count++]=selected;
                components.include(selected,bin,radius);
                if(selected.selected_count) {
                    input_fft=true;
                    ready_end=std::max(ready_end,selected.full?starts:
                        selected.ranges[selected.range_count-1].first+selected.ranges[selected.range_count-1].count);
                }
            }
        }
        const auto full_ready=static_cast<long double>(length)+first+starts-1;
        const auto union_ready=static_cast<long double>(length)+first+(input_fft?ready_end:starts)-1;
        if(input_fft&&!result.first_ready_bins)result.first_ready_bins=union_ready;
        const bool separated=component_scheduling&&initial_batch==hop&&
            components.eligible(bin,radius,candidate_limit,true);
        const auto first_component_ready=separated?static_cast<long double>(length)+first+
            components.ranges[0].first+components.ranges[0].count+3:union_ready;
        if(input_fft&&separated&&!result.first_component_ready_bins)result.first_component_ready_bins=first_component_ready;
        // An eligible enlarged component has diameter <radius, so an actual
        // subset cannot split it into several runtime components. If the
        // envelope cannot prove this, narrower actual maps may still split.
        // Price both the broad fallback and up to Cmax successful components.
        const auto separation=radius/bin+(radius%bin!=0);
        const auto component_bound=std::min({std::size_t{256},candidate_limit,
            std::size_t{1}+(starts-1)/separation});
        const bool uncertain_partition=minimum_readiness&&component_scheduling&&!separated;
        const auto dispatches=separated?components.count:std::size_t{1};
        long double operations=0,jobs=0,positions=0,direct=0,transforms=0;
        bool started=false;std::size_t executed_components=0;
        for(std::size_t c=0;c<dispatches;++c) {
            estimate_detail::check(stop);
            const auto low=separated?components.ranges[c].first:0;
            const auto end=separated?low+components.ranges[c].count:starts;
            const auto exact_ready=separated?static_cast<long double>(length)+first+end+3:union_ready;
            // A subset can be ready as soon as its first retained cell has
            // the same four-bin guard, irrespective of the envelope's end.
            const auto priced_ready=minimum_readiness&&input_fft?
                static_cast<long double>(length)+first+(separated?low+4:0):exact_ready;
            if(observed_bins<priced_ready)break;
            if(!input_fft)continue;
            const auto repeats=uncertain_partition?component_bound:std::size_t{1};
            started=true;
            if(separated)++executed_components;
            const auto segment_begin=separated&&c?components.ranges[c-1].first+components.ranges[c-1].count:0;
            const auto segment_count=end-segment_begin;
            std::array<modem::detail::PatternFftStartSelection,12> local{};
            std::array<std::optional<modem::detail::partitioned_paired::Requirements>,12> plans{};
            std::array<std::optional<long double>,12> envelope_numeric{};
            bool paired=generated&&!sample_fit;
            for(std::size_t j=0;j<selection_count;++j) {
                if(++checks>1000000)return std::nullopt;
                const auto& selected=selections[j];auto& target=local[j];target.full=selected.full;
                if(selected.full)target.selected_count=segment_count;
                else for(std::size_t r=0;r<selected.range_count;++r) {
                    if(++checks>1000000)return std::nullopt;
                    const auto begin=std::max(low,selected.ranges[r].first);
                    const auto last=std::min(end,selected.ranges[r].first+selected.ranges[r].count);
                    if(last>begin) {
                        target.ranges[target.range_count++]={begin-segment_begin,last-begin};
                        target.selected_count+=last-begin;
                    }
                }
                const auto k=target.selected_count;if(!k)continue;
                if(target.full){paired=false;continue;}
                if(modem::detail::pattern_fft_direct_eligible(geometry,generated,k,!sample_fit))continue;
                const auto ranges=std::span(target.ranges).first(target.range_count);
                if(minimum_readiness) {
                    envelope_numeric[j]=paired_envelope_numeric(ranges,length,transform,frequencies);
                    if(!envelope_numeric[j])paired=false;
                } else {
                    plans[j]=modem::detail::partitioned_paired::choose_geometry(length,segment_count,
                        ranges,frequencies,transform,2*transform);
                    if(!plans[j])paired=false;
                }
            }
            // Runtime decides the whole component before overwriting scratch.
            // One unsupported group keeps its original shared input spectrum.
            if(!paired) {
                transforms+=repeats;
                if(generated&&!sample_fit)result.paired_fallback_components+=repeats;
            }
            // work zeroing, original observation copy and energy prefix remain
            // even for all-direct execution. A cohort envelope keeps the full
            // transform-sized allowance rather than assuming a tiny PCM copy.
            const auto observed=minimum_readiness?hop:
                separated?std::min(hop,segment_count+4):std::min(starts,ready_end);
            operations+=repeats*(2.L*transform+6.L*(length+observed-1));
            if(!paired&&!minimum_readiness)operations+=repeats*(5*transform*fft_log+2.L*transform);
            for(std::size_t j=0;j<selection_count;++j) {
                const auto& selected=local[j];const auto k=selected.selected_count;if(!k)continue;
                const auto direct_cost=[&](std::size_t n) {
                    return length*(template_work+tracking_pair_operations_per_bin*n)+40.L*n;
                };
                const auto generation_evidence=frequencies*(template_work*length+40.L*k);
                const auto full_numeric=5*transform*fft_log+
                    frequencies*(20*transform*fft_log+12.L*transform);
                const bool direct_job=modem::detail::pattern_fft_direct_eligible(geometry,generated,k,!sample_fit);
                long double cost=0;
                if(paired) {
                    if(direct_job) {
                        cost=frequencies*direct_cost(k);
                        result.paired_direct_jobs+=static_cast<long double>(repeats)*frequencies;
                    } else if(minimum_readiness) {
                        // P>=32. Additions/row initialization are bounded
                        // termwise by1/3 of shared numeric operations. Runtime
                        // choose minimizes that same numeric work; the fixed
                        // envelope option therefore bounds all actual choices.
                        cost=generation_evidence+std::max(4.L/3*(*envelope_numeric[j]),
                            frequencies*length*tracking_pair_operations_per_bin*std::min<std::size_t>(k,32));
                    } else {
                        const auto& r=*plans[j];
                        cost=generation_evidence+modem::detail::partitioned_paired::operations(r,frequencies)+
                            paired_numeric_overhead(r,frequencies);
                        result.partitioned_jobs+=frequencies;
                        result.partitioned_input_transforms+=r.input_tiles;
                        result.partitioned_template_transforms+=2.L*frequencies*r.chunks;
                        result.partitioned_inverse_transforms+=2.L*frequencies*r.max_job_output_tiles;
                        if(!result.partitioned_tile_min||r.tile_size<result.partitioned_tile_min)result.partitioned_tile_min=r.tile_size;
                        result.partitioned_tile_max=std::max(result.partitioned_tile_max,r.tile_size);
                    }
                } else if(minimum_readiness) {
                    // Possible paired success has a separate input cache PER
                    // group. Its chooser ceiling plus4/3 initialization bound
                    // covers that path and the shared-input full fallback.
                    const auto dot=modem::detail::pattern_fft_direct_eligible(geometry,generated,1,!sample_fit)?
                        frequencies*length*tracking_pair_operations_per_bin*std::min<std::size_t>(k,32):0.L;
                    cost=generation_evidence+std::max(1.2L*full_numeric+2.L*transform,dot);
                } else cost=frequencies*(direct_job?direct_cost(k):
                    (20*transform*fft_log+12.L*transform+template_work*length+40.L*k));
                // Rounded engineering allowance for bounded range validation,
                // both chooser inspections and selected-cell bookkeeping.
                if(generated&&!sample_fit)cost+=512.L*(minimum_readiness?65:selected.range_count+1);
                jobs+=static_cast<long double>(repeats)*frequencies;
                positions+=static_cast<long double>(repeats)*k*frequencies;
                if(direct_job)direct+=static_cast<long double>(repeats)*frequencies;
                operations+=repeats*cost;
            }
        }
        if(!started&&(input_fft||observed_bins<union_ready))break;
        // Trial/work denominators describe the ORIGINAL logical hop, prepaid
        // once at its first component; component dispatches are not new trials.
        result.full_jobs+=static_cast<long double>(selection_count)*frequencies;
        result.full_positions+=static_cast<long double>(selection_count)*starts*frequencies;
        if(started) {
            ++result.batches;
            if(observed_bins<full_ready)++result.early_batches;
            if(component_scheduling&&!separated)++result.component_fallback_hops;
            if(uncertain_partition||selector_uncertain)++result.envelope_fallback_hops;
        }
        result.input_transforms+=transforms;result.components+=executed_components;
        result.jobs+=jobs;result.positions+=positions;result.direct_jobs+=direct;result.operations+=operations;
        result.max_operations=std::max(result.max_operations,operations);
        result.max_input_transforms=std::max(result.max_input_transforms,transforms);
        result.max_jobs=std::max(result.max_jobs,jobs);result.max_positions=std::max(result.max_positions,positions);
        result.max_direct_jobs=std::max(result.max_direct_jobs,direct);
    }
    return result;
}
// Union of both fractional-capture-anchor endpoints; the map is affine in
// capture UTC. Rate/error corners are evaluated at both ends, so the one-second
// envelope includes every anchor, without pretending it was observed metadata.
std::optional<modem::PatternStartWindow> fft_anchor_window(const transfer::Options& options,
        const modem::Config& config,const ReceiverTimingModel& timing,long double rate_uncertainty,
        long double origin_seconds,long double anchor_half_width,std::uint64_t phase_upper) {
    constexpr auto epoch=1800000000.L;
    std::optional<modem::PatternStartWindow> result;
    for(const auto sign:{-1,1}) {
        const auto map=clock_sync::arrival_map(*options.clock_sync,epoch,
            epoch+options.clock_sync->offset_seconds-origin_seconds+sign*anchor_half_width,
            0,config.sample_rate,timing.capture_seconds_per_frame.value_or(1./config.sample_rate),
            timing.capture_rate_uncertainty_fraction.value_or(static_cast<double>(rate_uncertainty)),
            std::max(options.audio_timing_error_seconds,timing.capture_error_seconds.value_or(options.audio_timing_error_seconds)),
            phase_upper,options.audio_timing_error_seconds);
        if(!map)return std::nullopt;
        const modem::PatternStartWindow next{map->origin_samples,map->phase_scale,map->half_width_samples};
        if(!result)result=next;
        else {
            const auto low=std::min(result->epoch_origin_samples-result->half_width_samples,
                next.epoch_origin_samples-next.half_width_samples);
            const auto high=std::max(result->epoch_origin_samples+result->half_width_samples,
                next.epoch_origin_samples+next.half_width_samples);
            result->epoch_origin_samples=(low+high)/2;result->half_width_samples=(high-low)/2;
        }
    }
    return result;
}
Work receiver_work(const modem::Config& config,const SearchBank& bank,long double samples,
                   const transfer::Options& options,std::size_t profiles,std::size_t keys,
                   std::size_t established_stream_bits,double local_window_seconds,ReceiverWorkMode work_mode,
                   const ReceiverTimingModel& timing_model,std::stop_token stop) {
    estimate_detail::check(stop);
    const auto symbol=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto drift_sections=modem::detail::drift_section_count(config);
    const auto differential_window=config.pattern_symbols && config.spreading_mode==modem::SpreadingMode::pattern?
        modem::detail::differential_window_samples(symbol,chip,config.sample_rate,
            local_window_seconds):0;
    const auto differential_windows=differential_window?symbol/differential_window:0;
    const bool private_pattern=config.scramble || config.dsss;
    // Rounded allowance for the additional independent candidate stream/map;
    // the detector/FFT costs already include fitting both candidate rows.
    const auto private_pair_generation=private_pattern?40.L:0.L;

    const auto epochs=private_pattern?2.L*options.search_seconds+1+
        (options.timestamp||work_mode!=ReceiverWorkMode::sampled_simulation?0:std::ceil((static_cast<long double>(modem::training_sample_count(config))+
            modem::pattern_pulse_padding_samples(config))/config.sample_rate)):1.L;
    const auto banks=epochs*keys;
    const auto& geometry=bank.frequency;
    const bool coupled=bank.legacy_coupled;
    const bool scaled=bank.minimum_rate!=1||bank.maximum_rate!=1;
    const auto frequencies=static_cast<long double>(bank.hypotheses.size());
    const auto maximum_clock_ratio=std::max(1-bank.minimum_rate,bank.maximum_rate-1);
    auto bin=modem::pattern_projection_bin_samples(config,geometry.half_width_hz);
    const auto omega=2*std::numbers::pi*config.carrier_hz/config.sample_rate;
    const auto sine=std::sin(omega);
    auto image=std::abs(sine)>1e-12?std::abs(std::sin(static_cast<double>(bin)*omega)/sine):static_cast<double>(bin);
    if(symbol<=256 && (!private_pattern || image>1e-10*static_cast<double>(bin)))bin=1;
    image=std::abs(sine)>1e-12?std::abs(std::sin(static_cast<double>(bin)*omega)/sine):static_cast<double>(bin);
    const bool sample_fit=bin==1 && (symbol<=256 || !private_pattern);
    const auto observation_samples=static_cast<long double>(symbol)/bank.minimum_rate;
    const auto length=std::max(4.L,std::ceil(observation_samples/bin));
    const auto nominal_length=std::ceil(static_cast<long double>(symbol)/bin);
    const bool interleaved=config.dsss_factor>1&&config.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2;
    // Bounded rejection-draw allowance for one generated V2 template pair.
    // The two-bit pair shares one isolated-symbol map, including finite
    // shaping tails. Reuse across jobs is not guaranteed or credited. The
    // rounded 40 operations/draw-byte allowance is an uncalibrated engineering
    // assumption, not measured V2 cryptographic throughput.
    const auto permutation_pair_work=interleaved?(2.L*std::floor(static_cast<long double>(symbol)/chip)+1024)*4*40:0.L;
    const auto template_pair_work=template_pair_operations_per_bin+private_pair_generation+permutation_pair_work/length;
    auto fft_log=std::max(std::ceil(std::log2(2*std::max(4.L,nominal_length))),std::ceil(std::log2(length)));
    auto transform=std::exp2(fft_log),hop=transform-length+1;
    const bool bounded_acquisition=symbol>=16.L*config.sample_rate;
    auto initial_batch=hop;
    if(bounded_acquisition) {
        const auto compact=std::exp2(std::ceil(std::log2(length+4)));
        if(compact-length+1>=std::max(4.L,std::floor(length/4)))transform=std::min(transform,compact);
        fft_log=std::log2(transform);
        hop=std::min(transform-length+1,std::max(1.L,std::floor(nominal_length/2)));
        initial_batch=std::min(hop,std::max(1.L,std::floor(static_cast<long double>(config.sample_rate)/bin)));
    }
    const bool separate_tracking_reference=scaled || transform<2*length;
    // Retaining transformed template rows is optional. The streamed path
    // keeps FFT/ring/tracking scratch and empty row metadata, then generates
    // each template in existing per-job scratch without reducing coverage.
    const auto fft_core_bytes=transform*16*5+(4*length+2*hop)*16+
        (separate_tracking_reference?2*length*16:0)+(transform+1)*8+
        frequencies*(2*24+2*8+2*16+18*8+2*8)+
        4096*16+4096*sizeof(modem::PatternEvidence)+(drift_sections>1?48*hop:0)+
        (differential_window?hop*sizeof(modem::detail::DifferentialAccumulator):0);
    const auto fft_bytes=fft_core_bytes+2*frequencies*transform*16;
    // Live reserves at least half of the total for peer receivers, transmit
    // work and plots even when there is only one requested receive bank.
    const auto allowance=static_cast<long double>(options.dsp_workspace_bytes)/std::max(2.L,banks*profiles);
    // Two uint32 maps plus a rounded fixed allowance for the two bounded
    // coarse/rotation caches and metadata (currently below 64 KiB).
    const auto outer_map_bytes=interleaved?
        64*1024.L+8.L*std::floor(static_cast<long double>(symbol)/chip):0.L;
    // Expanded coupled banks require the FFT path. A compact private hint or
    // insufficient memory cannot silently substitute a different search path.
    const bool correlator=!coupled &&
        ((private_pattern && symbol>=60.L*config.sample_rate) || fft_core_bytes+outer_map_bytes>allowance);
    // Live banks stream expanded template rows when several profiles, keys or
    // epochs share the budget, so early banks cannot consume it with caches.
    bool streamed_templates=!correlator &&
        (drift_sections>1 || differential_window || fft_bytes+outer_map_bytes>allowance || (scaled && banks*profiles>1) ||
         (config.search_arithmetic!=modem::SearchArithmetic::fp64 && transform>=65536 &&
          !sample_fit && drift_sections==1 && !differential_window && private_pattern && symbol>256));
    auto starts=std::ceil(2.L*(options.search_seconds+1.L)*config.sample_rate/
                          std::max(1.L,std::floor(chip/(2.L*(1+maximum_clock_ratio)))))+1;
    if(config.oscillator_search) {
        long double lanes=0;
        for(const auto& hypothesis:bank.hypotheses) {
                estimate_detail::check(stop);
            const auto rate=1+static_cast<long double>(hypothesis.clock_error_ppm)*1e-6L;
            lanes+=std::ceil(4.L*(options.search_seconds+1.L)*config.sample_rate*rate/chip)+1;
        }
        starts=lanes/frequencies;
    }
    const auto phase_groups=private_pattern&&symbol%config.sample_rate!=0?(symbol>=config.sample_rate?2.L:3.L):1.L;
    Work result;
    result.epoch_hypotheses=epochs;
    result.timing_hypotheses=starts*frequencies;result.phase_groups=static_cast<std::size_t>(phase_groups);
    result.full_timing_hypotheses=result.timing_hypotheses;
    auto search_starts=starts;
    std::optional<modem::PatternStartWindow> fft_prior;
    std::uint64_t fft_phase_upper=0;
    // The assumed hardware model narrows only a backend which actually
    // enforces this prior. Compact uses a lattice count; short FFT skips only
    // complete excluded batches, retaining every mixed batch. The lattice count
    // uses a representative anchor with supplied capture metadata or explicit
    // selected bounds; it is not an actual running bank's hypothesis count.
    if(((correlator&&symbol>=60.L*config.sample_rate)||
        (!correlator&&!coupled&&symbol<16.L*config.sample_rate))&&
       work_mode==ReceiverWorkMode::hardware_timing_model&&options.clock_sync&&
       (!timing_model.capture_error_seconds||
        *timing_model.capture_error_seconds<=options.audio_timing_error_seconds)&&
       private_pattern&&config.spreading_mode==modem::SpreadingMode::pattern&&config.oscillator_search&&
       !(config.oscillator_search->reference==modem::OscillatorReference::shared_radio&&
         config.oscillator_search->rf_shift_hz!=0)) {
        const auto phase_step=std::gcd(symbol,static_cast<std::uint64_t>(config.sample_rate));
        const auto phase_upper=(std::min(symbol,static_cast<std::uint64_t>(config.sample_rate))-1)/phase_step*phase_step;
        const auto radius=options.search_seconds+1.L;
        constexpr auto epoch=1800000000.L;
        const auto capture_error=std::max(options.audio_timing_error_seconds,
            timing_model.capture_error_seconds.value_or(options.audio_timing_error_seconds));
        const auto map=clock_sync::arrival_map(*options.clock_sync,epoch,
            epoch+options.clock_sync->offset_seconds-radius,0,config.sample_rate,
            timing_model.capture_seconds_per_frame.value_or(1./config.sample_rate),
            timing_model.capture_rate_uncertainty_fraction.value_or(static_cast<double>(maximum_clock_ratio)),capture_error,
            phase_upper,options.audio_timing_error_seconds);
        if(map&&map->origin_samples-map->half_width_samples>=0&&
           map->origin_samples+map->phase_scale*phase_upper+map->half_width_samples<=2*radius*config.sample_rate) {
            const modem::PatternStartWindow prior{map->origin_samples,map->phase_scale,map->half_width_samples};
            result.geometry.arrival_window_available=true;
            result.geometry.combined_arrival_half_width_seconds=static_cast<double>(map->half_width_samples/config.sample_rate);
            if(!correlator) {fft_prior=prior;fft_phase_upper=phase_upper;}
            else {
            std::map<double,std::optional<std::size_t>> count_by_rate;
            long double retained=0;std::size_t intersections=0;bool bounded=true;
            for(const auto& hypothesis:bank.hypotheses) {
                estimate_detail::check(stop);
                auto [entry,fresh]=count_by_rate.try_emplace(hypothesis.clock_error_ppm);
                if(fresh) {
                    const auto rate=1+static_cast<long double>(hypothesis.clock_error_ppm)*1e-6L;
                    modem::detail::PatternStartLattice lattice(0,2*radius*config.sample_rate,
                        chip/(2*rate),0,phase_upper,phase_step,prior);
                    const auto range=lattice.retained_index_range();
                    const auto checks=range.second-range.first;
                    // Graph sweeps must not enumerate an unbounded fine-chip
                    // bank merely to obtain a planning number. Such cases
                    // retain the full-window engineering fallback.
                    if(checks>1000000-intersections){bounded=false;break;}
                    intersections+=checks;std::size_t count=0;
                    for(auto i=range.first;i<range.second;++i) {
                        if(i%256==0)estimate_detail::check(stop);
                        if(lattice.retained_phases(i))++count;
                    }
                    entry->second=count;
                }
                retained+=*entry->second;
            }
            if(bounded&&retained>=1&&retained<=starts*frequencies) {
                search_starts=retained/frequencies;
                result.timing_hypotheses=retained;
                result.timing_window_modeled=true;
            }
            }
        }
    }
    // Match Live's prefer-streamed constructor for qualified paired scratch.
    // The prior must be admitted first; merely selecting Clock sync is insufficient.
    if(fft_prior&&!sample_fit&&banks*profiles>1)streamed_templates=true;
    result.compact=correlator;result.bin_samples=bin;
    result.geometry.sample_rate=config.sample_rate;
    result.geometry.dsss_factor=config.dsss_factor;
    result.geometry.outer_dsss_version=config.dsss_factor>1?config.outer_dsss_version:modem::OuterDsssVersion::legacy_v1;
    result.geometry.workspace_bytes=options.dsp_workspace_bytes;
    result.geometry.per_bank_workspace_bytes=finite_seconds(allowance);
    if(!correlator) {
        result.geometry.first_fft_seconds=static_cast<double>((length+initial_batch-1)*bin/config.sample_rate);
        result.geometry.fft_hop_seconds=static_cast<double>(hop*bin/config.sample_rate);
    }
    if(interleaved&&correlator) {
        result.work_supported=false;
        result.geometry.fallback_reason="V2 compact permutation-cache rebuild work is outside this model";
    }
    result.geometry.fine_chip_samples=chip;result.geometry.inner_chip_samples=chip*config.dsss_factor;
    result.geometry.symbol_samples=symbol;
    result.geometry.fine_chip_seconds=static_cast<double>(chip)/config.sample_rate;
    result.geometry.inner_chip_seconds=static_cast<double>(result.geometry.inner_chip_samples)/config.sample_rate;
    result.geometry.symbol_seconds=static_cast<double>(symbol)/config.sample_rate;
    result.geometry.canonical_phase_step_seconds=static_cast<double>(std::gcd(symbol,
        static_cast<std::uint64_t>(config.sample_rate)))/config.sample_rate;
    result.geometry.canonical_phases=static_cast<std::size_t>((std::min(symbol,
        static_cast<std::uint64_t>(config.sample_rate))-1)/std::gcd(symbol,
        static_cast<std::uint64_t>(config.sample_rate))+1);
    result.geometry.timing_grid_seconds=correlator?
        static_cast<double>((config.oscillator_search?chip/(2.L*bank.maximum_rate):
            std::max(1.L,std::floor(chip/(2.L*bank.maximum_rate))))/config.sample_rate):
        static_cast<double>(bin)/config.sample_rate;
    result.geometry.backend=correlator?"Compact direct correlation":"FFT acquisition";
    result.geometry.scope="Source-derived geometry; representative epoch/key work counts, not executed counters";
    if(fft_prior) {
        result.geometry.arrival_window_available=true;
        result.geometry.combined_arrival_half_width_seconds=static_cast<double>(fft_prior->half_width_samples/config.sample_rate);
    } else if(options.clock_sync&&result.geometry.fallback_reason.empty())result.geometry.fallback_reason=result.timing_window_modeled?
        "Compact representative lattice; rolling compact admissions remain unmodeled":
        "Full arrival-window work: timing metadata or backend geometry is unsupported";
    const bool compact_hint=private_pattern&&symbol>=60.L*config.sample_rate;
    const auto block_samples=compact_hint?32.L:128.L;
    const auto candidate_count=compact_hint?32.L:2048.L;
    const auto points=compact_hint?64.L:2048.L;
    const auto lanes=starts*frequencies;
    const auto projection_banks=config.spreading_mode==modem::SpreadingMode::tone?2*frequencies:
        static_cast<long double>(geometry.count);
    // Rounded upper allowances cover private control/PatternCode state and
    // lane metadata without constructing any receiver or payload in a planner.
    const bool guard_chains=private_pattern && config.spreading_mode==modem::SpreadingMode::pattern &&
        static_cast<long double>(symbol)/bank.maximum_rate<modem::pattern_absence_seconds*config.sample_rate;
    const auto chain_state=guard_chains?
        phase_groups*sizeof(std::array<modem::detail::CorrelationChipEvidence,2>)+sizeof(double):0.L;
    const auto compact_required=256*1024.L+outer_map_bytes+
        lanes*(512+(phase_groups-1)*sizeof(std::array<modem::detail::CorrelationFit,2>)+2+chain_state)+
        projection_banks*(64+(block_samples+1)*sizeof(modem::detail::CorrelationProjection))+
        candidate_count*sizeof(modem::PatternEvidence)+points*sizeof(std::complex<double>)+
        frequencies*sizeof(modem::PatternFrequencyRateHypothesis);
    result.workspace_supported=correlator?
        (!config.oscillator_search||compact_required<=allowance):fft_core_bytes+outer_map_bytes<=allowance;
    const auto real_rank=static_cast<long double>(bin)-image<=1e-10L*bin;
    const auto count=static_cast<double>(length);
    result.noise_dimensions=correlator?static_cast<double>(symbol)/2:count*(real_rank?.5:1.);
    result.coherent_dimensions=correlator?result.noise_dimensions:
        (sample_fit?std::min(count,4*count/static_cast<double>(chip)):count)*(real_rank?.5:1.);
    result.section_dimensions=correlator?std::min(static_cast<double>(symbol),4.*static_cast<double>(symbol)/static_cast<double>(chip))/2:
        result.coherent_dimensions;
    result.noise_condition=correlator||real_rank?1:(static_cast<double>(bin)+image)/(static_cast<double>(bin)-image);
    result.timing_uncertainty_chips=correlator?.25:static_cast<double>(bin)/(2*static_cast<double>(chip));
    result.projection_bin_chips=correlator||bin==1?0:static_cast<double>(bin)/static_cast<double>(chip);
    // Only the FFT path searches four initial private stream templates.
    // Compact lanes follow their absolute stream address and charge each
    // completed phase group once, using the correlator's own alpha spending.
    const auto initial_symbols=private_pattern&&!correlator?4.L:1.L;
    const auto acquisition_trials=(correlator?search_starts:initial_batch)*frequencies*phase_groups*initial_symbols;
    result.acquisition_threshold=static_cast<double>(-std::log(1e-10L)+
        (correlator?std::log(acquisition_trials)+std::log(acquisition_trials+1)+std::log(2.L):
         2*std::log(acquisition_trials+1)+std::log(2*frequencies*initial_symbols)));
    result.following_search_ratio=correlator?1:static_cast<double>(nominal_length/initial_batch);
    // Compact layout is not allocated by the planner. Use a deliberately
    // generous per-lane upper allowance before crediting optional section
    // state; tighter compact budgets retain the coherent reference.
    const auto compact_bound=starts*frequencies*phase_groups*(4096+
        (differential_window?sizeof(std::array<modem::detail::CorrelationDifferentialFit,2>):0))+2*1024*1024;
    auto compact_allocated=compact_required;
    const auto drift_extra=lanes*phase_groups*sizeof(std::array<modem::detail::CorrelationDriftFit,2>);
    result.drift_supported=drift_sections>1&&result.workspace_supported&&
        (!correlator||(config.oscillator_search?compact_allocated+drift_extra<=allowance:compact_bound<=allowance))&&
        result.noise_dimensions>=16;
    if(correlator&&result.drift_supported)compact_allocated+=drift_extra;
    const auto differential_extra=lanes*phase_groups*sizeof(std::array<modem::detail::CorrelationDifferentialFit,2>);
    result.differential_supported=differential_window&&result.drift_supported&&
        (!correlator||!config.oscillator_search||compact_allocated+differential_extra<=allowance);
    if(correlator&&result.differential_supported)compact_allocated+=differential_extra;
    result.outer_presence=config.spreading_mode==modem::SpreadingMode::pattern&&config.dsss_factor>1&&
        (correlator?(!result.drift_supported&&!result.differential_supported):(drift_sections==1&&!differential_window));
    if(correlator&&result.outer_presence) {
        const auto extra=sizeof(std::vector<modem::detail::CorrelationOuterEvidence>)+
            lanes*phase_groups*(sizeof(modem::detail::CorrelationOuterEvidence)+
            (guard_chains?0:sizeof(std::array<modem::detail::CorrelationChipEvidence,2>)));
        if(compact_allocated+extra<=allowance)compact_allocated+=extra;
        else result.outer_presence=false;
    }
    // Long-symbol timing guides subscribe only after admission and are charged
    // to retained evidence, not every idle search lane. No constructor reserve
    // may displace the complete bank or its optional coefficient-cache gate.
    const bool pulse_geometry=modem::pattern_pulse_enabled(config)&&symbol>=16.L*config.sample_rate&&
        (chip<=4096||config.oscillator_search)&&
        symbol%(4*chip)==0&&
        (config.oscillator_search||(chip%2==0&&!scaled));
    const bool pulse_moments=chip>4096;
    const auto cell_capacity=std::ceil(block_samples*bank.maximum_rate/chip)+2;
    const auto pulse_extra=3*frequencies*(192+sizeof(modem::detail::CorrelationPulseKernel)+
        sizeof(modem::detail::CorrelationPulseSegment)+2*sizeof(std::complex<long double>)+2*sizeof(std::uint64_t)+
        (1+cell_capacity)*sizeof(modem::detail::CorrelationPulseCell))+lanes*16+32768+
        (pulse_moments?frequencies*(block_samples+1)*sizeof(std::complex<double>):0);
    result.pulse_projected=correlator&&pulse_geometry&&result.workspace_supported&&
        compact_allocated+pulse_extra<=allowance;
    // Four immutable carrier-moment lengths per frequency; a missing count
    // uses the exact bounded geometric helper on the caller's stack. Tags
    // are included in the per-bank state allowance above.
    constexpr auto affine_capacity=4.L;
    const auto segment_extra=projection_banks*((block_samples+1)*sizeof(std::complex<double>)+
        affine_capacity*sizeof(modem::detail::CorrelationCarrierMoments))+lanes*phase_groups*sizeof(std::uint64_t);
    result.pulse_segmented=correlator&&config.oscillator_search&&modem::pattern_pulse_enabled(config)&&
        symbol>=16.L*config.sample_rate&&chip>=1024&&!result.pulse_projected&&result.workspace_supported&&
        compact_allocated+segment_extra<=allowance;
    // Match the optional runtime cache: 80 bytes per lane/phase group, with
    // the original logical bit limit retained. The runtime evicts this cache
    // before required payload growth, so this central estimate credits reuse
    // while admitted; memory pressure can return future spans to the fallback
    // allowance. Do not reserve every possible retained bit and thereby veto
    // the production banks whose bit limit already consumes the spare budget.
    constexpr auto affine_coefficient_bytes=80.L;
    const auto coefficient_count=lanes*phase_groups;
    const auto minimum_retention=2*lanes+2*modem::PatternSearch{}.track_limit+2;
    const bool affine_coefficient_reuse=result.pulse_segmented&&!result.outer_presence&&chip>=8192&&
        chip/(256*bank.maximum_rate)>block_samples&&
        coefficient_count<=std::numeric_limits<std::size_t>::max()&&
        compact_allocated+segment_extra+coefficient_count*affine_coefficient_bytes+
            minimum_retention<=allowance;
    // The compact receiver mixes each unique real carrier bank. Clock lanes
    // sharing that carrier reuse its prefix; FFT receives one baseband stream.
    // Cross-key/epoch Live cache hits depend on per-push spare workspace and
    // origin identity, so this work model conservatively charges cache misses.
    result.serial=samples*projection_operations_per_sample*(correlator?projection_banks:1)*banks+
        outer_map_bytes/sizeof(std::uint32_t)*banks;
    // Admission thresholds belong to one receiver; unrelated keys and
    // waveform profiles add compute work, not evidence against this signal.
    const auto search_lanes=search_starts*frequencies;
    result.search_trials=std::max(1.L,search_lanes*phase_groups);
    if(established_stream_bits)
        result.tracking_windows=static_cast<long double>(established_stream_bits-1)+
            static_cast<long double>(modem::pattern_absence_samples(config))/symbol;
    if(correlator) {
        if(result.outer_presence) {
            const auto cells=std::ceil(samples*bank.maximum_rate/chip);
            // Projected pulse paths reuse shared observations; raw shaped
            // fallback also accumulates each observed pulse contribution.
            result.serial+=(cells*outer_operations_per_chip+
                ((!result.pulse_projected&&!result.pulse_segmented)?samples*outer_operations_per_sample:0))*
                search_lanes*phase_groups*banks;
        }
        if(result.pulse_segmented) {
            // Natural private preparations end at table knots, finite pulse
            // endpoints, chips, symbol invalidation and detector boundaries.
            // A partial final chip intersects a second shifted knot train in
            // at most its last nine chip positions. Rounded upper allowances
            // count those cuts independently and cap them at observations.
            // This corrects the preceding 256-knot-only span allowance even
            // when reuse cannot fit; the fallback still charges 1800 per fit.
            const auto symbols=std::ceil(samples*bank.maximum_rate/symbol);
            const auto boundaries=symbols*(4+(differential_window?differential_windows:0));
            const auto partial_tail=symbol%chip?257*symbols*
                std::min(9.L,std::ceil(static_cast<long double>(symbol)/chip)):0.L;
            const auto natural_spans=std::min(samples,1+257*
                std::ceil(samples*bank.maximum_rate/chip)+partial_tail+boundaries);
            const auto spans=std::min(samples,std::ceil(samples/block_samples)+natural_spans);
            const auto preparations=affine_coefficient_reuse?natural_spans:spans;
            result.serial+=samples*8*projection_banks*banks;
            result.search_serial=(preparations*pulse_affine_prepare_operations_per_segment+
                spans*(pulse_affine_fit_operations_per_segment+
                    128*(result.drift_supported+result.differential_supported)))*
                search_lanes*phase_groups*banks;
            // The small immutable palette is prepared once per frequency.
            // Unusual knot/quarter/window clips can miss it; conservatively
            // charge one geometric helper per such boundary and lane. Full
            // oscillator blocks use the pinned entry. Caller push sizes that
            // are not multiples of the oscillator block add unmodeled misses.
            const auto clipped_spans=std::min(spans,natural_spans);
            result.kernel_serial=projection_banks*affine_capacity*200*
                std::ceil(std::log2(std::max(2.L,block_samples)))*banks+
                clipped_spans*search_lanes*phase_groups*200*
                std::ceil(std::log2(std::max(2.L,block_samples)))*banks;
            result.kernel_upper_serial=result.kernel_serial;
            result.serial+=result.search_serial+result.kernel_serial;
        } else if(result.pulse_projected) {
            long double lattice_cells=0,kernel_samples=0,kernel_upper_samples=0;
            for(const auto& hypothesis:bank.hypotheses) {
                estimate_detail::check(stop);
                const auto rate=1+static_cast<long double>(hypothesis.clock_error_ppm)*1e-6L;
                const auto cells=3*std::ceil(samples*rate/chip);lattice_cells+=cells;
                // Engineering estimate of changed-knot density: 256 finite
                // table knots and one sample-count boundary per chip. Initial
                // lattice/count variants retain the historical nine setups.
                // It is not a cache-hit guarantee: exact closed endpoints or
                // roundoff-sized motion can force the separate upper path.
                const auto prepared=hypothesis.clock_error_ppm==0?9.L:
                    std::min(cells,9+3*257*samples*std::abs(rate-1));
                const auto upper=hypothesis.clock_error_ppm==0?9.L:std::max(9.L,cells);
                if(hypothesis.clock_error_ppm!=0)result.kernel_upper_bound=true;
                if(pulse_moments) {
                    // No affine Gram preparation scans the original PCM chip.
                    const auto pieces=260.L;
                    kernel_samples+=prepared*pieces;kernel_upper_samples+=upper*pieces;
                } else {kernel_samples+=prepared*chip;kernel_upper_samples+=upper*chip;}
            }
            // Two parity lattices plus a clipped endpoint is a conservative
            // frontend allowance. Private fitting contracts the same 17 pulse
            // atoms and 153 Gram pairs once per chip and candidate bit pair.
            if(pulse_moments) {
                result.serial+=(samples*8*projection_banks+
                    3*std::ceil(samples/block_samples)*frequencies*pulse_moment_operations_per_block+
                    lattice_cells*260*pulse_moment_operations_per_segment+
                    lattice_cells*pulse_gram_operations_per_cell)*banks;
                // Binary geometric-moment concatenation is logarithmic in
                // samples per table piece, including a zero image frequency.
                const auto moment_work=200*std::ceil(std::log2(std::max(1.L,static_cast<long double>(chip)/256)));
                result.kernel_serial=kernel_samples*(pulse_moment_kernel_operations_per_segment+moment_work)*banks;
                result.kernel_upper_serial=kernel_upper_samples*(pulse_moment_kernel_operations_per_segment+moment_work)*banks;
            } else {
                result.serial+=(3*samples*frequencies*pulse_frontend_operations_per_sample+
                    lattice_cells*pulse_gram_operations_per_cell)*banks;
                result.kernel_serial=kernel_samples*pulse_kernel_operations_per_sample*banks;
                result.kernel_upper_serial=kernel_upper_samples*pulse_kernel_operations_per_sample*banks;
            }
            result.serial+=result.kernel_serial;
            // The projected backend consumes shared cells and commits lane
            // work in order on the caller; it does not use search workers.
            result.search_serial=std::ceil(samples*bank.maximum_rate/chip)*search_lanes*phase_groups*
                (pulse_pair_operations_per_chip+128*(result.drift_supported+result.differential_supported))*banks;
            result.serial+=result.search_serial;
        } else {
            // Shaped fallback retains every original sample. Rectangular
            // prefix projections can still share each bounded chip interval.
            const bool shaped_policy=config.oscillator_search&&modem::pattern_pulse_enabled(config);
            const auto observations=shaped_policy?samples:
                std::ceil(samples/std::min(32.L,static_cast<long double>(chip)));
            // Full shaped evaluation includes the 17-atom private waveform
            // pair as well as its two real fits; prefix-only work is cheaper.
            const auto operations_per_pair=(shaped_policy?512.L:64.L)+(guard_chains?64.L:0.L);
            result.parallel=(observations*search_lanes*phase_groups*operations_per_pair*
                (1+(result.drift_supported?1:0)+(result.differential_supported?1:0))+
                (result.differential_supported?samples/symbol*differential_windows*search_lanes*phase_groups*
                    differential_operations_per_window:0))*banks;
        }
    } else {
        // Constructor work occurs even before the first complete FFT window.
        // Streamed V2 still prepares its first permutation without FFT rows.
        const auto constructor_setup=(fft_core_bytes+outer_map_bytes)/sizeof(double)+
            (streamed_templates?permutation_pair_work:0.L);
        // The same numerical template/FFT work has the same engineering rate
        // whether performed once during setup or repeatedly in acquisition.
        // Zeroing/control/permutation-only setup retains the serial allowance.
        const auto constructor_scoring=streamed_templates?0.L:
            frequencies*(10*transform*fft_log+template_pair_work*length);
        result.initial_epoch_setup_operations=constructor_setup*banks;
        result.initial_epoch_setup_scoring_operations=constructor_scoring*banks;
        result.serial+=result.initial_epoch_setup_operations;
        auto blocks=std::ceil(samples/(bin*hop));
        auto scored_starts=blocks*hop;
        if(bounded_acquisition || work_mode!=ReceiverWorkMode::sampled_simulation) {
            // Live input scores only fully observed windows. The first small
            // batch (or the full short-FFT hop) follows one complete symbol;
            // later batches use the fixed hop. Hardware never calls finish()
            // and therefore cannot add EOF-triggered partial transforms.
            const auto observed_bins=std::floor(samples/bin);
            const auto first_batch_end=length+initial_batch-1;
            blocks=observed_bins<first_batch_end?0:1+std::floor((observed_bins-first_batch_end)/hop);
            scored_starts=blocks>0?initial_batch+(blocks-1)*hop:0;
        }
        result.fft_acquisition_batches=blocks;
        std::optional<long double> filtered_operations;
        std::size_t filtered_checks=0;
        const bool restrict_jobs=fft_prior&&drift_sections==1&&!differential_window&&!options.timestamp;
        const bool early_readiness=restrict_jobs&&initial_batch==hop;
        const auto observed_bins=std::floor(samples/bin);
        const auto qualified_slots=early_readiness?(observed_bins<length?0:
            1+std::floor((observed_bins-length)/hop)):blocks;
        result.geometry.qualified_ready_batch_slots=qualified_slots;
        const auto component_radius=std::max<std::uint64_t>(1,symbol/2);
        const auto component_separation=component_radius/static_cast<std::size_t>(bin)+
            (component_radius%static_cast<std::size_t>(bin)!=0);
        const auto maximum_component_dispatches=std::min({std::size_t{256},modem::PatternSearch{}.candidate_limit,
            std::size_t{1}+(static_cast<std::size_t>(hop)-1)/component_separation});
        // If exact enumeration is unavailable, cover each potential group's
        // separate paired input cache as well as a whole-component fallback.
        const auto full_group_allowance=frequencies*template_pair_work*length+
            std::max(1.2L*(5*transform*fft_log+frequencies*(20*transform*fft_log+12.L*transform))+2.L*transform,
                frequencies*length*tracking_pair_operations_per_bin*std::min(hop,32.L))+
            512.L*65;
        if(restrict_jobs&&(epochs>256||qualified_slots>1000000))
            result.geometry.fallback_reason="Restricted cohort enumeration limit";
        if(restrict_jobs&&epochs<=256&&qualified_slots<=1000000) {
            const auto count=[&](const modem::PatternStartWindow& prior,std::size_t passes,bool minimum_readiness=false) {
                return fft_window_work(config,bank.hypotheses.size(),static_cast<std::size_t>(bin),
                    static_cast<std::size_t>(length),static_cast<std::size_t>(transform),fft_log,
                    static_cast<std::size_t>(hop),static_cast<std::size_t>(initial_batch),passes,
                    prior,sample_fit,streamed_templates,bank.minimum_rate,bank.maximum_rate,
                    template_pair_work,filtered_checks,stop,early_readiness?observed_bins:
                        std::numeric_limits<long double>::infinity(),minimum_readiness,early_readiness);
            };
            const auto representative=count(*fft_prior,static_cast<std::size_t>(qualified_slots));
            long double operations=0,retained_batches=0,envelope_fallback_hops=0;bool bounded=representative.has_value();
            for(long double e=-static_cast<long double>(options.search_seconds);
                bounded&&e<=options.search_seconds;++e) {
                const auto prior=fft_anchor_window(options,config,timing_model,maximum_clock_ratio,
                    e-.5L,.5L,fft_phase_upper);
                const auto cohort=prior?count(*prior,static_cast<std::size_t>(qualified_slots),true):std::nullopt;
                if(!cohort){bounded=false;break;}
                operations+=cohort->operations;retained_batches+=cohort->batches;
                envelope_fallback_hops+=cohort->envelope_fallback_hops;
            }
            if(bounded) {
                filtered_operations=operations*keys;blocks=retained_batches/epochs;
                result.geometry.restricted_fft_modeled=true;result.timing_window_modeled=true;
                result.geometry.first_qualified_window_seconds=finite_seconds(representative->first_ready_bins*bin/config.sample_rate);
                result.geometry.first_component_window_seconds=finite_seconds(representative->first_component_ready_bins*bin/config.sample_rate);
                result.geometry.input_fft_transforms=finite_seconds(representative->input_transforms);
                result.geometry.timing_component_dispatches=finite_seconds(representative->components);
                result.geometry.component_fallback_hops=finite_seconds(representative->component_fallback_hops);
                result.geometry.paired_direct_template_jobs=finite_seconds(representative->paired_direct_jobs);
                result.geometry.partitioned_template_jobs=finite_seconds(representative->partitioned_jobs);
                result.geometry.partitioned_input_transforms=finite_seconds(representative->partitioned_input_transforms);
                result.geometry.partitioned_template_transforms=finite_seconds(representative->partitioned_template_transforms);
                result.geometry.partitioned_inverse_transforms=finite_seconds(representative->partitioned_inverse_transforms);
                result.geometry.paired_fallback_components=finite_seconds(representative->paired_fallback_components);
                result.geometry.partitioned_tile_min=representative->partitioned_tile_min;
                result.geometry.partitioned_tile_max=representative->partitioned_tile_max;
                if(envelope_fallback_hops)result.geometry.fallback_reason=
                    "Some anchor envelopes use full-job/component-count allowances to cover narrower-map partition or selector fallback";
                result.geometry.full_template_jobs=finite_seconds(representative->full_jobs);
                result.geometry.retained_template_jobs=finite_seconds(representative->jobs);
                result.geometry.direct_template_jobs=finite_seconds(representative->direct_jobs);
                result.geometry.full_start_positions=finite_seconds(representative->full_positions);
                result.geometry.retained_start_positions=finite_seconds(representative->positions);
                result.geometry.backend="Restricted FFT acquisition";
                result.geometry.backend+=representative->components>0?" + guarded components":"; broad union fallback";
                if(representative->partitioned_jobs>0)result.geometry.backend+=" + paired partitioned tiles";
                if(representative->paired_direct_jobs>0)result.geometry.backend+=" + paired direct <=32-position jobs";
                else if(representative->direct_jobs>0)result.geometry.backend+=" + direct <=32-position jobs";
                result.fft_retained_acquisition_batches=representative->batches;
            } else result.geometry.fallback_reason="Whole-batch FFT fallback: bounded cohort enumeration exhausted";
        }
        if(!filtered_operations&&early_readiness) {
            blocks=qualified_slots;scored_starts=blocks*hop;
            result.geometry.fallback_reason+=(result.geometry.fallback_reason.empty()?"":"; ")+
                std::string("Full-job qualified-readiness allowance; bounded cohort enumeration unavailable");
        }
        if(!filtered_operations&&!early_readiness&&fft_prior && blocks>0) {
            // Upper bound over every epoch alignment: fill all gaps between
            // initial stream/phase groups, and retain edge-overlapping batches.
            // The runtime uses the tighter per-group predicate. This changes
            // acquisition only; ingestion, tracks and all trial charges remain.
            const auto lower=fft_prior->epoch_origin_samples-fft_prior->half_width_samples;
            const auto offset=3.L*symbol/.99L;
            const auto upper=fft_prior->epoch_origin_samples+
                fft_prior->phase_scale*fft_phase_upper+fft_prior->half_width_samples+offset;
            const auto magnitude=std::max({1.L,std::abs(lower),std::abs(upper),samples,
                std::abs(fft_prior->epoch_origin_samples),std::abs(offset),
                std::abs(fft_prior->phase_scale*fft_phase_upper)});
            if(std::isfinite(magnitude)&&magnitude<1e15L) {
                const auto margin=2.5L*bin+64*std::numeric_limits<double>::epsilon()*magnitude;
                const auto retained=std::min(blocks,std::ceil((upper-lower+2*margin+(hop-1)*bin)/(hop*bin))+1);
                blocks=retained;scored_starts=std::min(scored_starts,retained*hop);
                result.timing_window_modeled=true;
            }
        }
        if(!filtered_operations)result.fft_retained_acquisition_batches=blocks;
        long double first_batches=blocks>0?1.L:0.L;
        if(private_pattern && !options.timestamp && work_mode!=ReceiverWorkMode::sampled_simulation) {
            const auto prefix=static_cast<long double>(modem::training_sample_count(config)+
                modem::pattern_pulse_padding_samples(config))/config.sample_rate;
            const auto fresh_full=rolling_fft_work(samples,config.sample_rate,bin,length,hop,initial_batch,
                static_cast<long double>(symbol)/config.sample_rate,prefix,options.search_seconds,
                maximum_clock_ratio,timing_model);
            const auto fresh=early_readiness?rolling_fft_work(samples,config.sample_rate,bin,length,hop,initial_batch,
                static_cast<long double>(symbol)/config.sample_rate,prefix,options.search_seconds,
                maximum_clock_ratio,timing_model,true):fresh_full;
            result.new_epoch_admissions=fresh.admissions;
            result.new_epoch_full_fft_batches=fresh_full.full_batches;
            result.new_epoch_qualified_ready_batch_slots=fresh.full_batches;
            result.new_epoch_retained_fft_batches=fft_prior?
                std::min(fresh.full_batches,fresh.first_batches*blocks):fresh.full_batches;
            if(filtered_operations) {
                const auto prior=fft_anchor_window(options,config,timing_model,maximum_clock_ratio,
                    static_cast<long double>(options.search_seconds)-.5L,.5L,fft_phase_upper);
                const auto cohort=prior&&fresh.maximum_batches<=1000000?
                    fft_window_work(config,bank.hypotheses.size(),static_cast<std::size_t>(bin),
                        static_cast<std::size_t>(length),static_cast<std::size_t>(transform),fft_log,
                        static_cast<std::size_t>(hop),static_cast<std::size_t>(initial_batch),
                        static_cast<std::size_t>(fresh.maximum_batches),*prior,sample_fit,streamed_templates,
                        bank.minimum_rate,bank.maximum_rate,template_pair_work,filtered_checks,stop,
                        std::numeric_limits<long double>::infinity(),true,early_readiness):std::nullopt;
                if(cohort) {
                    result.new_epoch_retained_fft_batches=std::min(fresh.full_batches,fresh.first_batches*cohort->batches);
                    *filtered_operations+=keys*std::min(fresh.full_batches*cohort->max_operations,
                        fresh.first_batches*cohort->operations);
                } else {
                    // Unsupported fresh-cohort geometry keeps all its original
                    // work, rather than multiplying a representative discount.
                    const auto jobs=frequencies*phase_groups*4;
                    if(early_readiness)*filtered_operations+=keys*maximum_component_dispatches*
                        (fresh.full_batches*(8.L*transform+4*phase_groups*full_group_allowance)+
                        jobs*40*(fresh.first_batches*initial_batch+(fresh.full_batches-fresh.first_batches)*hop));
                    else *filtered_operations+=keys*(fresh.full_batches*(5*transform*fft_log*(1+4*jobs)+
                        jobs*(12*transform+template_pair_work*length))+
                        jobs*40*(fresh.first_batches*initial_batch+(fresh.full_batches-fresh.first_batches)*hop));
                    result.new_epoch_retained_fft_batches=fresh.full_batches;
                    result.geometry.fallback_reason="Fresh-cohort bounded enumeration unavailable; fresh work uses full FFT allowance";
                }
            }
            const auto fresh_starts=fft_prior?result.new_epoch_retained_fft_batches*hop:
                fresh.first_batches*initial_batch+(fresh.full_batches-fresh.first_batches)*hop;
            // Subsequent equations multiply by epochs*keys. Fresh cohort totals
            // are per key/profile, so divide only by the initial epoch count.
            blocks+=result.new_epoch_retained_fft_batches/epochs;
            scored_starts+=fresh_starts/epochs;
            if(!fft_prior)first_batches+=fresh.first_batches/epochs;
            result.new_epoch_frontend_operations=fresh.input_samples*projection_operations_per_sample*keys;
            // Constructor zero-initialization plus its initial template pair.
            // Streamed rows construct no transforms; every later row generation,
            // input FFT and inverse FFT is already priced by the added batches.
            const auto setup=constructor_setup;
            result.new_epoch_setup_operations=fresh.admissions*setup*keys;
            result.new_epoch_setup_scoring_operations=fresh.admissions*constructor_scoring*keys;
            result.serial+=result.new_epoch_frontend_operations+result.new_epoch_setup_operations;
        }
        const auto jobs=frequencies*phase_groups*(private_pattern?4:1);
        if(result.outer_presence) {
            // Retained peaks are separated by half a symbol per carrier.
            // Charge that bounded candidate capacity, not every scored start.
            // This is a conservative workload allowance, not measured traffic.
            const auto peaks_per_block=std::min(static_cast<long double>(modem::PatternSearch{}.candidate_limit),
                (1+std::ceil(2*hop*bin/symbol))*frequencies);
            const auto pass=length*(template_pair_work+outer_operations_per_sample)+
                std::ceil(static_cast<long double>(symbol)/chip)*outer_operations_per_chip;
            const auto guard_work=blocks*peaks_per_block*pass*banks;
            result.serial+=guard_work;result.search_serial+=guard_work;
            result.tracking_serial+=result.tracking_windows*pass;
        }
        // Five real operations per complex FFT element per stage. Streamed
        // public templates need the same additional forward transform and
        // generation allowance as regenerated private templates.
        const bool generate_templates=private_pattern || streamed_templates;
        if(drift_sections>1) {
            // Section transforms reuse one full-size work buffer. Their sum
            // also supplies the coherent dot; there is no fifth template FFT.
            // The input spectrum is still computed once per acquisition hop.
            // Tiny batches directly match their <=4 complete start windows.
            const auto direct_blocks=blocks>0?(hop<=4?blocks:(initial_batch<=4?first_batches:0.L)):0.L;
            const auto direct_starts=hop<=4?scored_starts:direct_blocks*initial_batch;
            const auto transformed_blocks=blocks-direct_blocks;
            const auto fit_operations=sample_fit?tracking_real_pair_operations_per_bin:
                tracking_pair_operations_per_bin;
            result.parallel=(blocks*5*transform*fft_log+
                transformed_blocks*jobs*(20*drift_sections*transform*fft_log+
                    12*drift_sections*transform+template_pair_work*length)+
                direct_starts*jobs*length*(template_pair_work+fit_operations)+
                jobs*40*(drift_sections+1)*scored_starts)*banks;
        } else {
            result.parallel=(blocks*(5*transform*fft_log*(1+2*jobs*(generate_templates?2:1))+
                jobs*(12*transform+(generate_templates?template_pair_work*length:0)))+
                jobs*40*scored_starts)*banks;
            if(filtered_operations)result.parallel=*filtered_operations;
            else if(early_readiness)result.parallel=maximum_component_dispatches*
                (blocks*(8.L*transform+4*phase_groups*full_group_allowance)+jobs*40*scored_starts);
        }
        if(differential_window) {
            const auto direct_limit=std::max(4.L,static_cast<long double>(differential_windows)*fft_log);
            const auto direct_blocks=blocks>0?(hop<=direct_limit?blocks:(initial_batch<=direct_limit?first_batches:0.L)):0.L;
            const auto direct_starts=hop<=direct_limit?scored_starts:direct_blocks*initial_batch;
            const auto fit_operations=sample_fit?tracking_real_pair_operations_per_bin:
                tracking_pair_operations_per_bin;
            // Each complete local window uses its own template transforms,
            // reusing the input spectrum and a bounded per-start accumulator.
            // Allow a second full template-generation pass; optional waveform
            // caching is not credited as a guaranteed saving.
            // Batches below the window-count/log-size crossover instead make
            // one additional full-symbol dot pass per fully observed start.
            result.parallel+=((blocks-direct_blocks)*jobs*differential_windows*
                (20*transform*fft_log+12*transform)+
                blocks*jobs*length*template_pair_work+
                direct_starts*jobs*length*fit_operations+
                jobs*differential_operations_per_window*differential_windows*scored_starts)*banks;
        }
        if(established_stream_bits) {
            // Acquisition supplies the first bit. An established track then
            // scores each remaining bit and complete absent symbols covering
            // six seconds. This desired-stream allowance is independent of
            // unrelated key/epoch banks; extra competing/noise tracks and
            // reacquisition can add work, so it is not a runtime upper bound.
            // continue_tracks() refines five starts for each possible private
            // phase group, then compares the chosen start against every other
            // carrier/clock hypothesis. measure() reuses a template pair for
            // those five starts, regenerating it when frequency/phase changes.
            const auto fits=5*phase_groups+frequencies-1;
            const auto generated_pairs=phase_groups+frequencies-1;
            const auto fit_operations=sample_fit?tracking_real_pair_operations_per_bin:
                tracking_pair_operations_per_bin;
            result.tracking_serial+=result.tracking_windows*(length*(
                generated_pairs*template_pair_work+fits*fit_operations)+
                fits*tracking_evidence_operations_per_fit*(drift_sections>1?drift_sections+1:1));
            if(differential_window)result.tracking_serial+=result.tracking_windows*fits*
                (length*fit_operations+differential_windows*differential_operations_per_window);
        }
    }
    result.parallel+=result.initial_epoch_setup_scoring_operations+result.new_epoch_setup_scoring_operations;
    auto& arithmetic=result.geometry;
    arithmetic.search_arithmetic=config.search_arithmetic;
    arithmetic.arithmetic_experimental=config.search_arithmetic!=modem::SearchArithmetic::fp64;
    if(modem::search_arithmetic_is_int8(config.search_arithmetic)) {
        arithmetic.arithmetic_operands="INT8 screening operands; original FP64 verification";
        arithmetic.arithmetic_accumulation="INT32 bounded chunks / INT64 dot totals; FP64 scales and evidence";
        arithmetic.arithmetic_backend=arithmetic.paired_direct_template_jobs>0?
            "Bounded direct INT8 screening; FP64 FFT/compact/tracking fallback":
            "FP64 FFT/compact fallback: no modeled eligible INT8 direct jobs";
        arithmetic.arithmetic_limit="Experimental partial INT8 coverage. Supported FFTs use native FP64 SIMD; template construction, compact and tracking remain FP64. Packing, exact refinements and FFT plan setup are not calibrated in this work model; no universal INT8 speedup is predicted. GPU execution is unavailable.";
    } else if(config.search_arithmetic==modem::SearchArithmetic::fp32 ||
              config.search_arithmetic==modem::SearchArithmetic::default_mode) {
        const bool native=!correlator && streamed_templates && !sample_fit && drift_sections==1 && !differential_window;
        arithmetic.arithmetic_operands=native?"FP32 acquisition operands; FP64 fallback":"FP64 operands for modeled geometry";
        arithmetic.arithmetic_accumulation=native?"FP32 acquisition; FP64 energy, covariance, timing and final evidence":
            "FP64 products, accumulation and evidence";
        arithmetic.arithmetic_backend=native?"Native serial streamed FP32 direct / partitioned / whole FFT acquisition":
            "FP64 compact / unsupported detector / cached or parallel fallback";
        arithmetic.arithmetic_limit="Experimental automatic/minimum policy, not receiver-wide FP32. INT8 is not selected automatically: complete-execution benefit is unproven. Cached/parallel and mixed-cohort generic FFTs retain FP64; memory/range checks can also require original FP64 before upload. Source coefficients, precise coordinates, energies and final scores remain wide. The model is a dispatch reference, not observed counters; native setup coefficients are uncalibrated and conservative template charges remain. GPU deferred.";
        arithmetic.template_reuse=native?
            "Exact caches plus separately admitted close-clock interpolation (<=0.001 input sample), only with all identity, even pulse-boundary and workspace checks. No observed paired detection disagreements in conditional tests; sensitivity bounds remain inconclusive and full-bank sensitivity is unqualified.":
            "Exact reuse only for this modeled fallback; approximate interpolation requires a supported native acquisition stage.";
    } else {
        arithmetic.arithmetic_operands="Original unquantized FP64 operands";
        arithmetic.arithmetic_accumulation="FP64 products, accumulation and evidence";
        arithmetic.arithmetic_backend="FP64 direct / partitioned / FFT / compact";
        arithmetic.arithmetic_limit="Independent original-template reference with native SIMD FFTs where supported; precise coordinates retain their existing wider precision. Plan setup is not calibrated in the work model. GPU execution is unavailable.";
        arithmetic.template_reuse="Exact caches allowed; approximate interpolation prohibited by FP64 force.";
    }

    return result;
}

// Build the same nominal local template statistics used by FFT matching.
// This work is bounded by projected observations, never represented PCM time.
// Compact receivers use a bounded per-chip quadrature for shaped templates.
// Unsupported cases keep their timing/resource diagnostics without inventing
// a reception percentage from a different detector.
bool real_differential_statistics(const transfer::Options& options,std::uint64_t window,
                                 detail::ReceiverProbabilityParameters& p,unsigned source_bit,std::stop_token stop) {
    estimate_detail::check(stop);
    const auto& config=options.modem;
    const auto total=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto step=modem::pattern_pulse_enabled(config)?std::max<std::uint64_t>(1,chip/16):chip;
    // Same compact per-chip quadrature limit as the aligned approximation.
    // Intersections and a short final block are length weighted, never padded.
    if(!step || total/step>524288 || total/window>4096)return false;
    auto configured=config;
    if(options.timestamp)configured.stream_epoch=options.timestamp;
    if(options.key)configured=transfer::seeded_config(options,configured.stream_epoch);
    modem::PatternCode code(configured,configured.stream_epoch);
    p.differential_windows=static_cast<std::uint32_t>(total/window);
    p.differential_window_seconds=static_cast<double>(window)/config.sample_rate;
    p.differential_tail_seconds=static_cast<double>(total%window)/config.sample_rate;
    p.real_samples=total;p.real_window_samples=window;p.real_sample_rate=config.sample_rate;
    // The carrier-square covariance is periodic in pi. Reduce before the
    // geometric sum so both DC and Nyquist use the same stable sinc limit.
    const auto omega=std::remainder(2*std::numbers::pi*config.carrier_hz/config.sample_rate,
        std::numbers::pi);
    const auto sinc=[](double x){return std::abs(x)<1e-4?1-x*x/6+x*x*x*x/120:std::sin(x)/x;};
    const auto amplitude=std::sqrt(2*modem::nominal_signal_power);
    for(std::uint64_t first=0;first<total;) {
        unsigned section=0;
        while(section<3&&first>=modem::detail::drift_boundary(section+1,total,4))++section;
        const auto end=std::min({total,(first/window+1)*window,
            modem::detail::drift_boundary(section+1,total,4)});
        detail::ReceiverProbabilityAtom atom;atom.first_sample=first;atom.samples=end-first;atom.section=section;
        for(auto at=first;at<end;) {
            estimate_detail::check(stop);
            const auto until=std::min(end,(at/step+1)*step),count=until-at;
            const auto position=static_cast<double>(at)+(static_cast<double>(count)-1)/2;
            const auto index=static_cast<std::uint64_t>(position/chip);
            const auto fraction=position/chip-static_cast<double>(index);
            const auto a=modem::pattern_pulse_enabled(config)?code.shaped_value(0,source_bit,position):
                code.value(index,source_bit,fraction);
            const auto b=modem::pattern_pulse_enabled(config)?code.shaped_value(0,1-source_bit,position):
                code.value(index,1-source_bit,fraction);
            const auto source=modem::pattern_pulse_enabled(config)?
                modem::pattern_limit_pcm(amplitude*a)/amplitude:a;
            // Exact real carrier covariance inside this constant-envelope
            // quadrature block, including DC/Nyquist and finite-pulse images.
            const auto image=sinc(static_cast<double>(count)*omega)/sinc(omega);
            const auto phase=2*omega*position;
            const auto half_count=static_cast<double>(count)*.5;
            const std::array<double,3> carrier{half_count*(1+image*std::cos(phase)),
                half_count*(1-image*std::cos(phase)),half_count*image*std::sin(phase)};
            const std::array<std::array<double,2>,6> vectors{{
                {a.real(),-a.imag()},{a.imag(),a.real()},
                {b.real(),-b.imag()},{b.imag(),b.real()},
                {source.real(),-source.imag()},{source.imag(),source.real()}}};
            const auto product=[&](unsigned i,unsigned j) {
                return vectors[i][0]*vectors[j][0]*carrier[0]+vectors[i][1]*vectors[j][1]*carrier[1]+
                    (vectors[i][0]*vectors[j][1]+vectors[i][1]*vectors[j][0])*carrier[2];
            };
            for(unsigned i=0;i<4;++i) {
                for(unsigned j=0;j<4;++j)atom.gram[4*i+j]+=product(i,j);
                atom.signal_cos[i]+=product(i,4);atom.signal_sin[i]+=product(i,5);
            }
            atom.signal_energy[0]+=product(4,4);atom.signal_energy[1]+=product(5,5);
            atom.signal_energy[2]+=product(4,5);at=until;
        }
        for(auto& value:atom.signal_cos)value*=std::sqrt(2./static_cast<double>(total));
        for(auto& value:atom.signal_sin)value*=std::sqrt(2./static_cast<double>(total));
        for(auto& value:atom.signal_energy)value*=2./static_cast<double>(total);
        p.real_atoms.push_back(atom);first=end;
    }
    return true;
}

bool differential_statistics(const transfer::Options& options,const Work& work,
                             std::uint64_t window,detail::ReceiverProbabilityParameters& p,unsigned source_bit,std::stop_token stop) {
    estimate_detail::check(stop);
    const auto& config=options.modem;
    const auto total=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto windows=total/window;
    if(windows>4096)return false;
    if(total%window || windows%4) {
        // The old circular approximation has no representation of a tail or
        // a window crossing a drift boundary. Preserve it for aligned cases;
        // raw compact fits use the joint real-covariance atom model instead.
        return work.compact&&real_differential_statistics(options,window,p,source_bit,stop);
    }
    // Private raw-bin FFT fits use a different trace-only legacy score scale;
    // the joint model currently supports compact raw fits or complex FFT bins.
    if(!work.compact&&work.bin_samples==1&&(config.scramble||config.dsss))return false;
    auto step=work.compact?std::max<std::uint64_t>(1,chip/16):work.bin_samples;
    if(!modem::pattern_pulse_enabled(config)&&work.compact)step=chip;
    const auto quadrature=modem::pattern_pulse_enabled(config)&&!work.compact?
        std::min<std::uint64_t>(step,4):1;
    constexpr std::uint64_t maximum_observations=524288;
    if(!step || total/step>maximum_observations/quadrature || total%step || window%step)return false;
    auto configured=config;
    if(options.timestamp)configured.stream_epoch=options.timestamp;
    if(options.key)configured=transfer::seeded_config(options,configured.stream_epoch);
    modem::PatternCode code(configured,configured.stream_epoch);
    p.differential_windows=static_cast<std::uint32_t>(windows);
    p.differential_window_seconds=static_cast<double>(window)/config.sample_rate;
    p.differential_weights.assign(static_cast<std::size_t>(windows),0);
    p.differential_correlations.assign(static_cast<std::size_t>(windows),{});
    p.differential_signal_coefficients.assign(static_cast<std::size_t>(windows),{});
    std::vector<double> other(static_cast<std::size_t>(windows));
    std::vector<std::array<std::complex<double>,3>> pseudo(static_cast<std::size_t>(windows));
    const bool real_sample_fits=work.compact||work.bin_samples==1;
    // Compact fits whiten a real-sample Gram matrix. Circular-complex model
    // draws are accurate only if the local image term is small. Integrate the
    // carrier exactly across each quadrature block, keeping template work
    // bounded even for hours-long chips.
    const auto omega=2*std::numbers::pi*config.carrier_hz/config.sample_rate;
    const auto image=std::abs(std::sin(omega))<1e-12?1.:
        std::sin(static_cast<double>(step)*omega)/(static_cast<double>(step)*std::sin(omega));
    const auto amplitude=std::sqrt(2*modem::nominal_signal_power);
    double source_energy=0;
    for(std::uint64_t at=0;at<total;at+=step) {
        if((at/step)%128==0)estimate_detail::check(stop);
        const auto position=static_cast<long double>(at)+(step-1)/2.L;
        const auto index=static_cast<std::uint64_t>(position/chip);
        const auto fraction=static_cast<double>(position/chip-index);
        const auto a=modem::pattern_pulse_enabled(config)?code.shaped_value(0,source_bit,static_cast<double>(position)):
            code.value(index,source_bit,fraction);
        const auto b=modem::pattern_pulse_enabled(config)?code.shaped_value(0,1-source_bit,static_cast<double>(position)):
            code.value(index,1-source_bit,fraction);
        const auto local=static_cast<std::size_t>(at/window);
        auto source=a;
        if(modem::pattern_pulse_enabled(config)) {
            source={};
            for(std::uint64_t node=0;node<quadrature;++node) {
                const auto sample=static_cast<double>(at)+
                    (static_cast<double>(node)+.5)*static_cast<double>(step)/static_cast<double>(quadrature)-.5;
                source+=modem::pattern_limit_pcm(amplitude*code.shaped_value(0,source_bit,sample))/amplitude;
            }
            source/=static_cast<double>(quadrature);
        }
        source_energy+=std::norm(source);
        p.differential_signal_coefficients[local][0]+=source*std::conj(a);
        p.differential_signal_coefficients[local][1]+=source*std::conj(b);
        p.differential_weights[local]+=std::norm(a);other[local]+=std::norm(b);
        p.differential_correlations[local]+=a*std::conj(b);
        if(real_sample_fits) {
            const auto rotation=image*std::polar(1.,2*omega*static_cast<double>(position));
            pseudo[local][0]+=a*a*rotation;pseudo[local][1]+=b*b*rotation;pseudo[local][2]+=a*b*rotation;
        }
    }
    const auto sum=std::accumulate(p.differential_weights.begin(),p.differential_weights.end(),0.);
    const auto other_sum=std::accumulate(other.begin(),other.end(),0.);
    if(!(sum>0)||!(other_sum>0)||!(source_energy>0))return false;
    // Retain actual finite-code power, radial limiting and projected-bin
    // averaging. Signal fits can differ from the unmodified receive templates;
    // the joint model retains their unfitted energy in the denominator.
    p.signal_energy*=source_energy/static_cast<double>(total/step);
    p.differential_alternative_weights=other;
    p.weights.fill(0);p.correlations.fill(0);
    p.alternative_weights.emplace().fill(0);
    for(std::size_t i=0;i<windows;++i) {
        const auto denominator=std::sqrt(p.differential_weights[i]*other[i]);
        if(!(denominator>0))return false;
        p.differential_signal_coefficients[i][0]/=std::sqrt(p.differential_weights[i]*source_energy);
        p.differential_signal_coefficients[i][1]/=std::sqrt(other[i]*source_energy);
        if(real_sample_fits&&(std::abs(pseudo[i][0])>.01*p.differential_weights[i] ||
            std::abs(pseudo[i][1])>.01*other[i] || std::abs(pseudo[i][2])>.01*denominator))return false;
        p.differential_correlations[i]/=denominator;
        if(std::abs(p.differential_correlations[i])>=.999999)return false;
        p.differential_weights[i]/=sum;
        p.differential_alternative_weights[i]/=other_sum;
        const auto quarter=std::min<std::size_t>(3,i*4/windows);
        p.weights[quarter]+=p.differential_weights[i];
        (*p.alternative_weights)[quarter]+=p.differential_alternative_weights[i];
        p.correlations[quarter]+=std::sqrt(p.differential_weights[i]*p.differential_alternative_weights[i])*
            p.differential_correlations[i];
    }
    for(unsigned j=0;j<4;++j)p.correlations[j]/=std::sqrt(p.weights[j]*(*p.alternative_weights)[j]);
    return true;
}

std::array<double,2> sampling_interval(double probability,std::size_t trials,double z=1.959963984540054) {
    if(!trials)return {0,1};
    // Wilson interval for the fixed Monte Carlo draws only. Model mismatch
    // and physical oscillator/propagation uncertainty are separate limits.
    const auto n=static_cast<double>(trials),denominator=1+z*z/n;
    const auto center=(probability+z*z/(2*n))/denominator;
    const auto radius=z/denominator*std::sqrt(probability*(1-probability)/n+z*z/(4*n*n));
    return {probability<=0?0:std::max(0.,center-radius),probability>=1?1:std::min(1.,center+radius)};
}
}

Estimate estimate(const transfer::Estimate& transmission,const transfer::Options& options,bool raw_bits,
                  const modem::ChannelConfig& channel,std::span<const modem::Config> profiles,std::size_t keys,
                  bool compute_probability,double local_window_seconds,std::size_t probability_trials,
                  ReceiverWorkMode work_mode,ReceiverTimingModel timing_model,std::stop_token stop) {
    estimate_detail::check(stop);
    modem::validate(options.modem);modem::validate_channel(options.modem,channel);
    if(!std::isfinite(transmission.total_seconds) || transmission.total_seconds<0 || !keys ||
       !std::isfinite(local_window_seconds) || local_window_seconds<0)
        throw Error("invalid simulation estimate input");
    for(const auto& value:{timing_model.capture_error_seconds,timing_model.capture_rate_uncertainty_fraction})
        if(value&&(!std::isfinite(*value)||*value<0))throw Error("invalid receiver timing model");
    if(timing_model.capture_seconds_per_frame&&(!std::isfinite(*timing_model.capture_seconds_per_frame)||
        *timing_model.capture_seconds_per_frame<=0))throw Error("invalid receiver capture slope");
    if(profiles.empty())profiles=std::span(&options.modem,1);
    Estimate result;result.receiver_profiles=profiles.size();result.receiver_work_mode=work_mode;
    result.probability_reference_only=options.clock_sync.has_value()||options.modem.dsss_factor>1;
    result.receiver_work_assumptions=work_mode==ReceiverWorkMode::sampled_simulation?
        "Sampled simulation startup and settling epochs; original oscillator bank":
        work_mode==ReceiverWorkMode::hardware_fallback?
        "Hardware initial epochs; full arrival-window fallback, configured peer UTC correction bank retained":
        "Hardware timing model assumed; admitted UTC correction bank, full arrival-window fallback";
    const auto& config=options.modem;
    const auto samples_per_symbol=modem::symbol_sample_count(config);
    const auto seconds=static_cast<long double>(samples_per_symbol)/config.sample_rate;
    result.drift_sections=modem::detail::drift_section_count(config);
    result.coherent_reference_only=result.drift_sections>1;
    // Integer quarter boundaries differ by at most one sample. Expose the
    // longest section so the phase diagnostic never understates its duration.
    const auto section_samples=samples_per_symbol/result.drift_sections+
        (samples_per_symbol%result.drift_sections!=0);
    result.drift_section_seconds=static_cast<double>(section_samples)/config.sample_rate;
    const auto differential_window=config.pattern_symbols && config.spreading_mode==modem::SpreadingMode::pattern?
        modem::detail::differential_window_samples(samples_per_symbol,modem::pattern_chip_samples(config),
            config.sample_rate,local_window_seconds):0;
    result.differential_windows=differential_window?samples_per_symbol/differential_window:0;
    result.differential_window_seconds=static_cast<double>(differential_window)/config.sample_rate;
    if(differential_window)result.coherent_reference_only=false;
    const auto chip_seconds=static_cast<long double>(modem::pattern_chip_samples(config))/config.sample_rate;
    // The sampled transport adds this complete-symbol absence and lookahead
    // after the exact waveform, including its settling and suppression noise.
    const auto tail=static_cast<long double>(modem::pattern_absence_samples(config))/config.sample_rate+1+
        2.L*modem::pattern_pulse_padding_samples(config)/config.sample_rate;
    // Seeded startup is 50..300 ms plus a fractional sample. Its mean is
    // 175 ms; receiver-clock delay is not shortened by transmitter clock rate.
    const auto media=static_cast<long double>(transmission.total_seconds)/(1+channel.clock_error_ppm*1e-6L)+
        static_cast<long double>(channel.delay_samples)/config.sample_rate+.175L+tail;
    result.simulated_seconds=finite_seconds(media);
    const auto samples=media*config.sample_rate;
    long double serial=samples*channel_operations_per_sample,parallel=0,tracking_serial=0,tracking_windows=0,trials=1;
    long double receiver_frontend=0,kernel_serial=0,kernel_upper_serial=0,search_serial=0;
    long double fallback_serial=0,fallback_parallel=0,fallback_tracking=0;
    bool any_pulse_projected=false,any_pulse_segmented=false,any_kernel_upper_bound=false;
    Work matching_work;
    const modem::Config* matching_profile=nullptr;
    struct BankEntry {const modem::Config* profile;SearchBank bank;};
    struct WorkEntry {std::size_t bank;std::uint64_t training_samples;Work work;};
    // This cache lasts only for one estimate. Keys/epochs do not change the
    // search geometry; each receive profile still contributes its full cost.
    // Keep waveform identity separate from oscillator policy so matching
    // profiles with different coverage assumptions cannot share a bank.
    std::vector<BankEntry> banks;
    std::vector<WorkEntry> work_entries;
    banks.reserve(profiles.size()+1);work_entries.reserve(profiles.size());
    const auto bank_index=[&](const modem::Config& profile) {
        const auto found=std::find_if(banks.begin(),banks.end(),[&](const auto& entry) {
            return same_profile(profile,*entry.profile)&&
                profile.pattern_symbols==entry.profile->pattern_symbols&&
                profile.oscillator_search==entry.profile->oscillator_search;
        });
        if(found!=banks.end())return static_cast<std::size_t>(found-banks.begin());
        banks.push_back({&profile,search_bank(profile,options.clock_sync.has_value()&&
            work_mode!=ReceiverWorkMode::sampled_simulation)});
        return banks.size()-1;
    };
    std::size_t matching_bank=0;
    bool matching_supported=false,all_work_supported=true;
    const auto frequency=channel.frequency_offset_hz+static_cast<long double>(config.carrier_hz)*channel.clock_error_ppm*1e-6L;
    for(const auto& profile:profiles) {
        estimate_detail::check(stop);
        modem::validate(profile);
        const auto matches=same_profile(config,profile);
        result.profile_matches|=matches;
        const auto index=bank_index(profile);
        const auto& candidate_bank=banks[index].bank;
        // receiver_work uses training only for rounded private epoch coverage.
        // All its remaining profile inputs are included in the bank identity;
        // media, options, key/profile counts and matching wire bits are fixed.
        const auto training_samples=modem::training_sample_count(profile);
        const auto existing=std::find_if(work_entries.begin(),work_entries.end(),[&](const auto& entry) {
            return entry.bank==index&&entry.training_samples==training_samples;
        });
        const auto work=existing!=work_entries.end()?existing->work:
            receiver_work(profile,candidate_bank,media*profile.sample_rate,options,profiles.size(),keys,
                          matches?transmission.wire_bits:0,local_window_seconds,work_mode,timing_model,stop);
        if(existing==work_entries.end())work_entries.push_back({index,training_samples,work});
        serial+=work.serial;parallel+=work.parallel;all_work_supported&=work.work_supported;
        const auto fallback_work=work.timing_window_modeled?
            receiver_work(profile,candidate_bank,media*profile.sample_rate,options,profiles.size(),keys,
                matches?transmission.wire_bits:0,local_window_seconds,ReceiverWorkMode::hardware_fallback,timing_model,stop):work;
        fallback_serial+=fallback_work.serial;fallback_parallel+=fallback_work.parallel;
        fallback_tracking+=fallback_work.tracking_serial;
        receiver_frontend+=work.serial-work.kernel_serial-work.search_serial;kernel_serial+=work.kernel_serial;
        kernel_upper_serial+=work.kernel_upper_serial;
        search_serial+=work.search_serial;
        any_pulse_projected|=work.pulse_projected||work.pulse_segmented;
        any_pulse_segmented|=work.pulse_segmented;any_kernel_upper_bound|=work.kernel_upper_bound;
        tracking_serial+=work.tracking_serial;tracking_windows+=work.tracking_windows;
        if(matches) {
            const bool covered=within_representation_bound(frequency,candidate_bank.frequency.half_width_hz)&&
                (!profile.oscillator_search||paired_clock_coverage(candidate_bank,frequency,channel.clock_error_ppm));
            const bool supported=covered&&!candidate_bank.limited&&work.workspace_supported;
            if(!matching_profile||(supported&&(!matching_supported||work.acquisition_threshold<matching_work.acquisition_threshold))) {
                matching_work=work;matching_profile=&profile;matching_supported=supported;
                matching_bank=index;
                trials=work.search_trials;
            }
        }
    }
    result.receiver_workspace_supported=matching_profile&&matching_work.workspace_supported;
    result.receiver_work_supported=matching_profile&&all_work_supported;
    result.receiver_frontend_seconds=finite_seconds(receiver_frontend/serial_operations_per_second);
    result.receiver_search_seconds=finite_seconds(search_serial/serial_operations_per_second+parallel/cpu_scoring_operations_per_second);
    result.receiver_kernel_rebuild_seconds=finite_seconds(kernel_serial/serial_operations_per_second);
    result.receiver_kernel_rebuild_upper_seconds=finite_seconds(kernel_upper_serial/serial_operations_per_second);
    result.pulse_projection_modeled=any_pulse_projected;
    result.pulse_segment_projection_modeled=any_pulse_segmented;
    result.kernel_rebuild_upper_bound=any_kernel_upper_bound;
    result.tracking_seconds=finite_seconds(tracking_serial/serial_operations_per_second);
    result.tracking_symbol_windows=finite_seconds(tracking_windows);
    const auto payload=result.profile_matches?payload_work(transmission,options,raw_bits):PayloadWork{};
    const auto processing=payload.baseline+payload.mitigation;
    result.payload_processing_seconds=finite_seconds(processing);
    result.mitigation_seconds=finite_seconds(payload.mitigation);
    // Budget track refinement at the serial rate even when long-symbol carrier
    // fits can share CPU workers. The hypothetical GPU
    // model offloads FFT scoring only, so this term remains in both totals.
    const auto serial_seconds=(serial+tracking_serial)/serial_operations_per_second;
    result.cpu_seconds=finite_seconds(.03L+serial_seconds+parallel/cpu_scoring_operations_per_second+processing);
    result.receiver_cpu_seconds=finite_seconds(.03L+
        (serial-samples*channel_operations_per_sample+tracking_serial)/serial_operations_per_second+
        parallel/cpu_scoring_operations_per_second+processing);
    result.fallback_receiver_cpu_seconds=finite_seconds(.03L+
        (fallback_serial+fallback_tracking)/serial_operations_per_second+
        fallback_parallel/cpu_scoring_operations_per_second+processing);
    result.gpu_seconds=finite_seconds(.11L+serial_seconds+parallel/gpu_scoring_operations_per_second+
        samples*sizeof(float)/gpu_transfer_bytes_per_second+processing);
    if(!transmission.wire_bits)return result;

    // The simulator's SNR is per Fs/2 noise bandwidth, so Es/N0=snr*Fs*T/2.
    const auto symbol_db=channel.snr_db+10*std::log10(static_cast<long double>(samples_per_symbol)/2);
    const auto& receiver_config=matching_profile?*matching_profile:config;
    const bool oscillator_policy=receiver_config.oscillator_search.has_value();
    const auto& bank=banks[matching_profile?matching_bank:bank_index(receiver_config)].bank;
    const auto& geometry=bank.frequency;
    const bool coupled=bank.legacy_coupled;
    const auto frequencies=static_cast<long double>(bank.hypotheses.size());
    const auto spacing=static_cast<long double>(geometry.step_hz);
    const auto outer_bin=static_cast<long double>(geometry.count/2);
    result.carrier_offset_hz=static_cast<double>(frequency);
    result.carrier_search_half_width_hz=geometry.half_width_hz;
    result.requested_carrier_search_half_width_hz=geometry.requested_half_width_hz;
    result.clock_search_half_width_ppm=bank.clock_ppm;
    result.requested_clock_search_half_width_ppm=bank.requested_clock_ppm;
    result.receiver_geometry=matching_work.geometry;
    result.frequency_rate_hypotheses=bank.hypotheses.size();
    result.epoch_hypotheses=static_cast<std::size_t>(matching_work.epoch_hypotheses);
    result.timing_hypotheses=static_cast<double>(matching_work.timing_hypotheses);
    result.timing_phase_groups=matching_work.phase_groups;
    result.fallback_timing_hypotheses=static_cast<double>(matching_work.full_timing_hypotheses);
    result.timing_window_modeled=matching_work.timing_window_modeled;
    result.fft_acquisition_batches=static_cast<double>(matching_work.fft_acquisition_batches);
    result.fft_retained_acquisition_batches=static_cast<double>(matching_work.fft_retained_acquisition_batches);
    result.new_epoch_admissions=static_cast<double>(matching_work.new_epoch_admissions);
    result.new_epoch_full_fft_batches=static_cast<double>(matching_work.new_epoch_full_fft_batches);
    result.new_epoch_qualified_ready_batch_slots=static_cast<double>(matching_work.new_epoch_qualified_ready_batch_slots);
    result.new_epoch_retained_fft_batches=static_cast<double>(matching_work.new_epoch_retained_fft_batches);
    result.initial_epoch_setup_seconds=finite_seconds(matching_work.initial_epoch_setup_operations/serial_operations_per_second+
        matching_work.initial_epoch_setup_scoring_operations/cpu_scoring_operations_per_second);
    result.new_epoch_frontend_seconds=finite_seconds(matching_work.new_epoch_frontend_operations/serial_operations_per_second);
    result.new_epoch_setup_seconds=finite_seconds(matching_work.new_epoch_setup_operations/serial_operations_per_second+
        matching_work.new_epoch_setup_scoring_operations/cpu_scoring_operations_per_second);
    if(work_mode==ReceiverWorkMode::hardware_timing_model) {
        result.receiver_work_assumptions=matching_work.timing_window_modeled&&!matching_work.compact?
            (matching_work.geometry.restricted_fft_modeled?
            "Restricted FFT acquisition work allowance: original per-phase start cells and empty template jobs are omitted under selected GPS/audio bounds; input FFTs per guarded component, repeated private template dispatches, continuation and original logical-hop trial/threshold charges retained. Initial epochs use a one-second anchor envelope; uncertain partition/selector geometry uses full-job component-count allowances. Fresh cohorts use bounded acquisition-lifetime allowances. Paired direct and partitioned convolution use shared geometry selectors; uncertain anchors retain full-cohort fallback or a translation-independent eligible-option allowance. Borrowed-row initialization, repeated group input caches and component observation preparation are included; operation coefficients and V2 generation remain uncalibrated. Full-window fallback is reported separately":
            "FFT acquisition work allowance: whole excluded batches skipped under selected GPS/audio bounds; mixed batches, frontend, continuation and original threshold charges retained. Full-window fallback is reported separately"):matching_work.timing_window_modeled?
            "Compact bank engineering reference: exact lattice count at a representative anchor under selected GPS/audio bounds and supplied capture metadata (nominal slope and full bank rate allowance when omitted). Full-window fallback is reported separately":
            "Full arrival-window bank; timing prior is unavailable for this backend or model geometry. Configured peer UTC correction lanes are retained";
    }
    if(!matching_work.compact) {
        std::ostringstream diagnostic;
        diagnostic<<(result.receiver_work_assumptions.empty()?"":"; ")<<"Initial epoch cohort: "<<result.epoch_hypotheses
            <<"; initial FFT acquisition batches retained "<<result.fft_retained_acquisition_batches
            <<"; original full-hop batches "<<result.fft_acquisition_batches<<" at the representative epoch";
        if(result.new_epoch_admissions>0)
            diagnostic<<"; Automatic Live refresh: up to "<<result.new_epoch_admissions
                <<" newly admitted epochs per key/profile, with "<<result.new_epoch_retained_fft_batches
                <<" retained batches, "<<result.new_epoch_qualified_ready_batch_slots<<" qualified-ready slots and "
                <<result.new_epoch_full_fft_batches<<" original full-hop batches. Includes repeated constructor/template and input processing; "
                <<"initial cohort remains conservatively charged over the whole observation. "
                <<"Resident-bank RAM and extra candidate-track lifetimes are unqualified";
        result.receiver_work_assumptions+=diagnostic.str();
        if(!result.receiver_geometry.fallback_reason.empty())result.receiver_work_assumptions+="; "+result.receiver_geometry.fallback_reason;
    }
    if(!options.timestamp&&work_mode!=ReceiverWorkMode::sampled_simulation) {
        if(matching_work.compact)
            result.receiver_work_assumptions+="; Automatic Live compact rolling-admission work is not modeled";
        result.receiver_work_assumptions+="; Workspace support covers the initial cohort, not peak resident Live banks. "
            "Synchronized/noise tracks, reconstruction retries and irregular refresh gaps can add work; "
            "this estimate is not a total runtime upper bound";
    }
    if(config.dsss_factor>1&&config.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2) {
        if(!result.receiver_work_assumptions.empty())result.receiver_work_assumptions+="; ";
        result.receiver_work_assumptions+=matching_work.compact?
            "V2 compact permutation rebuild work is unsupported; CPU feasibility unavailable":
            "V2 FFT template generation includes a bounded permutation-draw allowance; its cryptographic throughput is uncalibrated";
    }
    if(matching_work.outer_presence) {
        if(!result.receiver_work_assumptions.empty())result.receiver_work_assumptions+="; ";
        result.receiver_work_assumptions+="Includes a conservative coherent outer-code candidate-check allowance; actual retained peaks and throughput are not calibrated";
    }
    result.oscillator_search_limited=bank.limited;
    if(oscillator_policy&&bank.minimum_rate!=bank.maximum_rate) {
        result.probability_search_approximation=true;
        result.probability_model_limit="Paired clock candidates use the selected timing coherence and full bank trial penalty; joint timing-path covariance is not simulated";
    }
    result.carrier_in_search=within_representation_bound(frequency,geometry.half_width_hz);
    result.clock_in_search=!oscillator_policy||paired_clock_coverage(bank,frequency,channel.clock_error_ppm);
    auto nearest_bin=std::clamp(std::round(frequency/spacing),-outer_bin,outer_bin);
    // Generate the same double-valued offset as the receiver's bank before
    // evaluating the residual; endpoints cannot drift beyond modeled coverage.
    auto nearest=static_cast<long double>(static_cast<double>(nearest_bin)*geometry.step_hz);
    auto carrier_loss=1.L;
    auto residual_clock_ppm=std::abs(static_cast<long double>(channel.clock_error_ppm));
    if(oscillator_policy) {
        long double best=-1;
        for(const auto& hypothesis:bank.hypotheses) {
                estimate_detail::check(stop);
            const auto angle=std::numbers::pi_v<long double>*(frequency-hypothesis.frequency_offset_hz)*seconds;
            const auto loss=std::abs(angle)<1e-10L?1.L:std::pow(std::sin(angle)/angle,2);
            const auto clock=std::abs(static_cast<long double>(channel.clock_error_ppm)-hypothesis.clock_error_ppm);
            const auto smear=clock*1e-6L*seconds/chip_seconds;
            const auto timing=config.spreading_mode==modem::SpreadingMode::tone?1.L:
                std::pow(std::max(0.L,1-smear/2),2);
            if(loss*timing>best) {
                best=loss*timing;nearest=hypothesis.frequency_offset_hz;
                carrier_loss=loss;residual_clock_ppm=clock;
            }
        }
        nearest_bin=std::round(nearest/spacing);
    } else {
        const auto angle=std::numbers::pi_v<long double>*(frequency-nearest)*seconds;
        carrier_loss=std::abs(angle)<1e-10L?1.L:std::pow(std::sin(angle)/angle,2);
        if(coupled)residual_clock_ppm=std::min(residual_clock_ppm,
            std::abs(channel.clock_error_ppm-nearest/config.carrier_hz*1e6L));
    }
    const auto diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi_v<long double>/180;
    const auto phase_loss=phase_coherence(.5L*diffusion*diffusion*seconds);
    result.phase_coherence_loss_db=static_cast<double>(std::max(0.L,-10*std::log10(phase_loss)));
    const auto section_phase_loss=phase_coherence(.5L*diffusion*diffusion*result.drift_section_seconds);
    result.section_phase_coherence_loss_db=static_cast<double>(std::max(0.L,-10*std::log10(section_phase_loss)));
    // Expanded default banks retain both nominal symbol timing and timing
    // scaled by each carrier hypothesis. Independent carrier error can favor
    // the nominal-clock alternative; shared sample-clock error favors coupling.
    const auto smear=residual_clock_ppm*1e-6L*seconds/chip_seconds;
    const auto timing_loss=config.spreading_mode==modem::SpreadingMode::tone?1.L:
        std::pow(std::max(0.L,1-smear/2),2);
    const auto coherence=carrier_loss*phase_loss*timing_loss;
    const auto effective_db=coherence>0?symbol_db-model_implementation_loss_db+10*std::log10(coherence):-300.L;
    result.modeled_symbol_snr_db=static_cast<double>(std::max(-300.L,effective_db));
    // The real receiver scores explained / total energy. Signal energy that
    // misses its finite carrier bank remains in that denominator, imposing a
    // fit ceiling even at high SNR. Attenuating Es/N0 alone cannot predict that
    // regime. Do not extrapolate a numeric probability beyond the bank, nor
    // claim zero: some out-of-bank signals can still produce admitted fits.
    if(!result.profile_matches || !result.carrier_in_search || !result.clock_in_search ||
       !result.receiver_workspace_supported || result.oscillator_search_limited)return result;
    if(!compute_probability)return result;
    if(config.dsss_factor>1&&config.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2) {
        result.probability_model_limit="Interleaved V2 outer DSSS has a new permutation and pre-limiter amplitude; waveform energy and detection sensitivity are not qualified by this probability model";
        return result;
    }
    const auto probability_result=[&] {
        if(result.probability_reference_only) {
            result.reference_probability_available=result.confidence_available;
            result.one_bit_reference_available=result.one_bit_confidence_available;
            result.confidence_available=false;result.one_bit_confidence_available=false;
            result.probability_interval_available=false;
            std::string scope="Conditional matched-template AWGN reference";
            if(options.clock_sync)scope+="; UTC device timing and steering failures are not modeled";
            if(config.dsss_factor>1)scope+="; outer DSSS code-presence rejection is not modeled";
            if(!result.probability_model_limit.empty())scope+="; "+result.probability_model_limit;
            result.probability_model_limit=std::move(scope);
        }
        return result;
    };
    if(differential_window&&!matching_work.differential_supported) {
        result.probability_model_limit="Local detector allocation is outside the modeled workspace allowance";
        return probability_result();
    }
    result.confidence_available=true;
    if(matching_work.drift_supported) {
        detail::ReceiverProbabilityParameters parameters;
        parameters.requested_trials=probability_trials;
        parameters.signal_energy=static_cast<double>(std::pow(10.L,symbol_db/10));
        parameters.seconds=static_cast<double>(seconds);
        parameters.diffusion_degrees=channel.phase_noise_degrees_per_sqrt_second;
        parameters.residual_frequency=static_cast<double>(frequency-nearest);
        parameters.frequency_step_hz=geometry.step_hz;
        parameters.frequency_bin_min=static_cast<int>(-outer_bin-nearest_bin);
        parameters.frequency_bin_max=static_cast<int>(outer_bin-nearest_bin);
        parameters.timing_coherence=static_cast<double>(timing_loss);
        parameters.timing_uncertainty_chips=matching_work.timing_uncertainty_chips;
        parameters.projection_bin_chips=matching_work.projection_bin_chips;
        parameters.pulse_shaping=modem::pattern_pulse_enabled(config);
        parameters.noise_dimensions=matching_work.noise_dimensions;
        parameters.coherent_dimensions=matching_work.coherent_dimensions;
        parameters.section_dimensions=matching_work.section_dimensions;
        parameters.noise_condition=matching_work.noise_condition;
        parameters.acquisition_threshold=matching_work.acquisition_threshold;
        // Every eligible symbol lasts longer than the physical six-second
        // absence interval: a merely retained, unconfirmed bit cannot wait
        // for later bits to establish a chain. It needs standalone evidence.
        // Approximate the growing search threshold at the middle of the
        // remaining draft; acquisition retains its separate first-bit value.
        parameters.continuation_threshold=parameters.acquisition_threshold+2*std::log1p(
            matching_work.following_search_ratio*std::max(1.,static_cast<double>(transmission.wire_bits)/2));
        // Resolve finite complex overlap and both energy envelopes. Dense
        // templates retain the orthogonal ensemble approximation. The bounded
        // local model below also accounts for shaping and limiter distortion.
        const auto chip=modem::pattern_chip_samples(config);
        const auto configure_statistics=[&](detail::ReceiverProbabilityParameters& p,unsigned source_bit) {
    estimate_detail::check(stop);
            if(!differential_window && samples_per_symbol/chip<=1024) {
                auto code_config=config;
                if(options.timestamp)code_config.stream_epoch=options.timestamp;
                if(options.key)code_config=transfer::seeded_config(options,code_config.stream_epoch);
                modem::PatternCode code(code_config,code_config.stream_epoch);
                p.alternative_weights.emplace();
                for(unsigned j=0;j<4;++j) {
                    const auto begin=modem::detail::drift_boundary(j,samples_per_symbol,4);
                    const auto end=modem::detail::drift_boundary(j+1,samples_per_symbol,4);
                    double norm0=0,norm1=0;std::complex<double> cross{};
                    for(auto at=begin;at<end;) {
                        const auto index=at/chip,until=std::min(end,(index+1)*chip);
                        const auto a=code.value(index,source_bit,0),b=code.value(index,1-source_bit,0);
                        const auto weight=static_cast<double>(until-at);
                        norm0+=weight*std::norm(a);norm1+=weight*std::norm(b);
                        cross+=weight*a*std::conj(b);at=until;
                    }
                    if(!(norm0>0) || !(norm1>0))return false;
                    auto rho=cross/std::sqrt(norm0*norm1);
                    if(std::abs(rho)>.999999)rho*=.999999/std::abs(rho);
                    p.correlations[j]=rho;p.weights[j]=norm0;(*p.alternative_weights)[j]=norm1;
                }
                const auto weight=std::accumulate(p.weights.begin(),p.weights.end(),0.);
                const auto other=std::accumulate(p.alternative_weights->begin(),p.alternative_weights->end(),0.);
                for(auto& w:p.weights)w/=weight;
                for(auto& w:*p.alternative_weights)w/=other;
                if(!differential_window && !modem::pattern_pulse_enabled(config))
                    p.signal_energy*=weight/static_cast<double>(samples_per_symbol);
            }
            if(!differential_window)return true;
            // A failed circular fit may have partially normalized its source
            // power and covariance. Retry from pristine parameters so the
            // real-covariance fallback applies that normalization exactly once.
            auto candidate=p;
            if(differential_statistics(options,matching_work,differential_window,candidate,source_bit,stop)) {
                p=std::move(candidate);return true;
            }
            if(!matching_work.compact)return false;
            candidate=p;candidate.real_atoms.clear();
            if(!real_differential_statistics(options,differential_window,candidate,source_bit,stop))return false;
            p=std::move(candidate);return true;
        };
        auto alternative_parameters=parameters;
        if(!configure_statistics(parameters,0)) {
            result.confidence_available=false;
            result.probability_model_limit="Template geometry exceeds the bounded probability model";
            return probability_result();
        }
        auto probability=detail::receiver_probability(parameters,stop);
        bool averaged_private_candidates=false;
        // The planner has no wire-bit content. Independent finite candidates
        // need an equiprobable-bit estimate, including each source's limiter
        // projections. Common random draws are paired, so retain the smaller
        // trial count rather than counting correlated draws as extra evidence.
        if(probability.available && (config.scramble || config.dsss) &&
           (samples_per_symbol/chip<=1024 || differential_window)) {
            if(!configure_statistics(alternative_parameters,1)) {
                result.confidence_available=false;
                result.probability_model_limit="Alternative template geometry exceeds the bounded probability model";
                return probability_result();
            }
            const auto other=detail::receiver_probability(alternative_parameters,stop);
            if(!other.available)probability=other;
            else {
                averaged_private_candidates=true;
                using P=detail::ReceiverProbability;
                for(auto field:{&P::acquired_correct,&P::acquired_wrong,&P::retained_correct,&P::retained_wrong,
                    &P::coherent_acquired_correct,&P::coherent_acquired_wrong,
                    &P::coherent_retained_correct,&P::coherent_retained_wrong,
                    &P::differential_acquired_correct})probability.*field=((probability.*field)+(other.*field))/2;
                probability.trials=std::min(probability.trials,other.trials);
            }
        }
        if(!probability.available) {
            result.confidence_available=false;
            result.probability_model_limit=probability.unsupported_reason;
            return probability_result();
        }
        result.probability_trials=probability.trials;
        result.one_bit_confidence_available=true;
        result.probability_search_approximation|=probability.frequency_search_approximation;
        result.probability_carrier_candidates=probability.frequency_candidates;
        result.differential_model_available=probability.differential_model;
        result.differential_added_detection_probability=probability.differential_acquired_correct;
        result.drift_model_available=true;result.coherent_reference_only=false;
        result.one_bit_success_probability=probability.acquired_correct;
        const auto count=static_cast<long double>(transmission.wire_bits);
        const auto draft_probability=[&](double acquired_correct,double acquired_wrong,double retained_correct,double retained_wrong) {
            if(raw_bits)return acquired_correct*probability_power(retained_correct,count-1);
            const auto retained=retained_correct+retained_wrong;
            const auto error=retained>0?retained_wrong/retained:0.;
            const auto intervals=static_cast<long double>(transmission.coded_bytes)/stream_interval_bytes;
            const auto marker=binomial_at_most(boundary_sync::marker_bits,boundary_sync::maximum_marker_errors,1-retained_correct);
            const auto absent=std::ceil(static_cast<long double>(modem::pattern_absence_samples(config))/samples_per_symbol);
            const auto premature=std::max(0.L,count-absent+1)*probability_power(1-retained,absent);
            return (acquired_correct+acquired_wrong)*
                probability_power(marker*interval_probability(retained,error,options.fec),intervals)*
                static_cast<double>(std::max(0.L,1-premature));
        };
        result.success_probability=draft_probability(probability.acquired_correct,probability.acquired_wrong,
            probability.retained_correct,probability.retained_wrong);
        result.coherent_success_probability=draft_probability(probability.coherent_acquired_correct,probability.coherent_acquired_wrong,
            probability.coherent_retained_correct,probability.coherent_retained_wrong);
        result.success_probability=std::clamp(result.success_probability,0.,1.);
        const auto one_interval=sampling_interval(probability.acquired_correct,probability.trials);
        result.one_bit_probability_low=one_interval[0];result.one_bit_probability_high=one_interval[1];
        if(raw_bits&&probability.trials) {
            // Simultaneous 95% intervals for acquisition and continuation
            // (Bonferroni for multi-bit drafts), propagated under the same
            // independent-bit assumption as the draft probability above.
            const auto z=count>1?2.241402727604947:1.959963984540054;
            const auto acquired=sampling_interval(probability.acquired_correct,probability.trials,z);
            const auto retained=sampling_interval(probability.retained_correct,probability.trials,z);
            result.success_probability_low=acquired[0]*probability_power(retained[0],count-1);
            result.success_probability_high=acquired[1]*probability_power(retained[1],count-1);
            result.probability_interval_available=true;
        }
        // Diagnostic coherent energy uses the joint frequency/diffusion mean;
        // the success calculation uses individual path statistics above.
        const auto joint=expected_correlation_coherence(parameters.seconds,parameters.diffusion_degrees,
            parameters.residual_frequency)*parameters.timing_coherence;
        const auto fitted_energy=[](const detail::ReceiverProbabilityParameters& p) {
            double fitted_fraction=1;
            if(!p.differential_signal_coefficients.empty()) {
                std::complex<double> fitted{};
                for(std::size_t i=0;i<p.differential_windows;++i)
                    fitted+=std::sqrt(p.differential_weights[i])*p.differential_signal_coefficients[i][0];
                fitted_fraction=std::norm(fitted);
            }
            if(!p.real_atoms.empty()) {
                modem::detail::CorrelationFit fit;std::array<double,2> c{},s{};
                for(const auto& atom:p.real_atoms) {
                    fit.cc+=atom.gram[0];fit.ss+=atom.gram[5];fit.cs+=atom.gram[1];fit.count+=atom.samples;
                    for(unsigned j=0;j<2;++j) {c[j]+=atom.signal_cos[j];s[j]+=atom.signal_sin[j];}
                }
                // Unknown initial carrier phase: average the two real source
                // quadratures. The actual limited source is already normalized
                // in the atoms, so nominal signal energy is applied only once.
                fit.energy=std::numeric_limits<double>::max();fit.xc=c[0];fit.xs=c[1];
                const auto cosine=fit.explained();fit.xc=s[0];fit.xs=s[1];
                fitted_fraction=(cosine+fit.explained())/2;
            }
            return p.signal_energy*fitted_fraction;
        };
        const auto modeled_energy=joint*(averaged_private_candidates?
            (fitted_energy(parameters)+fitted_energy(alternative_parameters))/2:fitted_energy(parameters));
        result.modeled_symbol_snr_db=modeled_energy>0?10*std::log10(modeled_energy):-300;
        if(differential_window&&matching_work.compact&&transmission.wire_bits>1) {
            // Compact reception keeps the first admitted timing lane, which
            // can precede the nearest lane. Subsequent success is conditional
            // on that choice; independent nearest-lane bit probabilities
            // measurably overestimate complete drafts in sampled captures.
            result.confidence_available=false;result.probability_interval_available=false;
            result.success_probability=result.coherent_success_probability=0;
            result.success_probability_low=0;result.success_probability_high=1;
            result.probability_model_limit="Compact multi-bit timing ownership is not modeled; the first-bit estimate remains available";
        }
        return probability_result();
    }
    const auto energy=static_cast<double>(std::pow(10.L,std::clamp(effective_db/10,-30.L,12.L)));
    const auto bit_error=.5*std::exp(-energy/2);
    // Keep the eligible coherent reference with the two-detector choice cost.
    // A compact bank can fall back when the added state cannot fit; retaining
    // the choice cost is conservative in that case, without claiming that the
    // reference bounds actual reception or that section fitting is active.
    // Merely substituting section phase loss here would omit the section
    // statistic's higher rank and correlated alternative-bit comparisons.
    const auto detector_choice_penalty=result.coherent_reference_only?std::log(2.):0.;
    const auto admitted=normal_above(energy,5+detector_choice_penalty);
    const auto threshold=static_cast<double>(-std::log(1e-10L)+2*std::log(trials+1)+std::log(2*frequencies))+
        detector_choice_penalty;
    const auto acquired=normal_above(energy,threshold);
    const auto correct_bit=admitted*(1-bit_error);
    result.one_bit_success_probability=acquired*correct_bit;
    const auto count=static_cast<long double>(transmission.wire_bits);
    double probability=0;
    if(raw_bits)probability=acquired*probability_power(correct_bit,count);
    else {
        const auto intervals=static_cast<long double>(transmission.coded_bytes)/stream_interval_bytes;
        const auto marker=binomial_at_most(boundary_sync::marker_bits,boundary_sync::maximum_marker_errors,1-correct_bit);
        probability=acquired*probability_power(marker*interval_probability(admitted,bit_error,options.fec),intervals);
    }
    // A run of fully failed durations can physically end reception before the
    // draft ends. Use the union bound under the same independence assumption.
    const auto absent=std::ceil(static_cast<long double>(modem::pattern_absence_samples(config))/samples_per_symbol);
    const auto premature=std::max(0.L,count-absent+1)*probability_power(1-admitted,absent);
    // Raw success already requires every symbol; do not count its missing
    // runs a second time. Interval FEC can otherwise conceal a physical end.
    if(!raw_bits)probability*=static_cast<double>(std::max(0.L,1-premature));
    result.success_probability=result.profile_matches?std::clamp(probability,0.,1.):0;
    result.one_bit_confidence_available=result.confidence_available;
    return probability_result();
}
}
