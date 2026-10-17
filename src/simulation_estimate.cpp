#include "live_receiver_layout.hpp"
#include "live_receiver_admission.hpp"
#include "scheduled_acquisition_policy.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/live.hpp"
#include "shared_scheduled_capture.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/scheduled_complex_search.hpp"
#include "datapump/tuning.hpp"
#include "pattern_drift.hpp"
#include "pattern_differential.hpp"
#include "pattern_correlator_batch.hpp"
#include "receiver_probability.hpp"
#include "eme_probability.hpp"
#include "amplitude_probability.hpp"
#include "estimate_cancellation.hpp"
#include "pattern_start_geometry.hpp"
#include "pattern_fft_window.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/correlation_experiment.hpp"
#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <map>
#include <sstream>

namespace datapump::simulation {
namespace {
bool shared_hop_calendar(const modem::Config& config) {
    return config.fake_hopping || (config.scheduled_hopping&&config.scheduled_hopping->enabled());
}
std::optional<clock_sync::Policy> arrival_policy(const transfer::Options& options,const modem::Config& config) {
    return clock_sync::effective_policy(options.clock_sync,shared_hop_calendar(config));
}
bool arrival_model_available(const transfer::Options& options,const modem::Config& config,
        ReceiverWorkMode mode,const ReceiverTimingModel& timing) {
    const bool calendar=shared_hop_calendar(config);
    if((timing.capture_error_seconds&&(!std::isfinite(*timing.capture_error_seconds)||*timing.capture_error_seconds<0))||
       (timing.capture_rate_uncertainty_fraction&&(!std::isfinite(*timing.capture_rate_uncertainty_fraction)||*timing.capture_rate_uncertainty_fraction<0))||
       (timing.capture_seconds_per_frame&&(!std::isfinite(*timing.capture_seconds_per_frame)||*timing.capture_seconds_per_frame<=0)))return false;
    if(mode!=ReceiverWorkMode::hardware_timing_model&&
       !(calendar&&mode==ReceiverWorkMode::hardware_calendar_model))return false;
    if(!arrival_policy(options,config))return false;
    // An unavailable timestamp provider stays the full-window comparator.
    // Dated calendar evidence carries its full U independently of Audio lag.
    return calendar||!timing.capture_error_seconds||
        *timing.capture_error_seconds<=clock_sync::audio_lag_radius(options.audio_timing_error_seconds);
}
std::optional<clock_sync::ArrivalMap> receiver_arrival_map(const transfer::Options& options,
        const modem::Config& config,long double epoch,long double capture_utc,long double capture_sample,
        double seconds_per_sample,double rate_uncertainty,double capture_error,std::uint64_t phase_upper) {
    const auto policy=arrival_policy(options,config);if(!policy)return std::nullopt;
    if(shared_hop_calendar(config))return clock_sync::calendar_arrival_map(*policy,epoch,capture_utc,
        capture_sample,config.sample_rate,seconds_per_sample,rate_uncertainty,capture_error,
        phase_upper,options.audio_timing_error_seconds);
    return clock_sync::audio_lag_arrival_map(*policy,epoch,capture_utc,capture_sample,config.sample_rate,
        seconds_per_sample,rate_uncertainty,capture_error,phase_upper,options.audio_timing_error_seconds);
}

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
constexpr std::uint64_t maximum_probability_windows = 4096;
constexpr std::uint64_t maximum_probability_observations = 524288;
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
    const auto hopping_equal=[&] {
        if(bool(a.fake_hopping)!=bool(b.fake_hopping))return false;
        if(!a.fake_hopping)return true;
        const auto& x=*a.fake_hopping;const auto& y=*b.fake_hopping;
        return x.channels==y.channels&&x.shifts==y.shifts&&x.dwell_seconds==y.dwell_seconds&&
            x.power_ramps==y.power_ramps&&x.defer_symbols==y.defer_symbols&&
            x.matched_gaps==y.matched_gaps&&x.mandatory_ramps==y.mandatory_ramps&&
            x.smooth_ramps==y.smooth_ramps&&x.remote_guard_covered==y.remote_guard_covered&&x.ramp_seconds==y.ramp_seconds&&
            x.clock_error_seconds==y.clock_error_seconds&&x.peer_clock_error_seconds==y.peer_clock_error_seconds&&
            x.audio_error_seconds==y.audio_error_seconds&&x.region_seconds==y.region_seconds&&
            (!x.defer_symbols||(a.stream_epoch%2==b.stream_epoch%2&&a.stream_phase_samples==b.stream_phase_samples));
    };
    const auto scheduled_equal=[&] {
        if(bool(a.scheduled_hopping)!=bool(b.scheduled_hopping))return false;
        if(!a.scheduled_hopping)return true;
        const auto& x=*a.scheduled_hopping;const auto& y=*b.scheduled_hopping;
        return x.time_multiplier==y.time_multiplier&&x.group_hops==y.group_hops&&
            x.clock_error_seconds==y.clock_error_seconds&&x.peer_clock_error_seconds==y.peer_clock_error_seconds&&
            x.region_seconds==y.region_seconds&&x.offset_seconds==y.offset_seconds&&
            x.audio_error_seconds==y.audio_error_seconds&&x.ramp_seconds==y.ramp_seconds;
    };
    return hopping_equal()&&scheduled_equal()&& a.sample_rate==b.sample_rate && a.carrier_hz==b.carrier_hz && a.bandwidth_hz==b.bandwidth_hz && a.dsss_factor==b.dsss_factor &&
        (a.dsss_factor<=1||a.outer_dsss_version==b.outer_dsss_version) &&
        a.spreading_mode==b.spreading_mode && a.scramble==b.scramble && a.dsss==b.dsss &&
        a.pulse_shaping==b.pulse_shaping && modem::legacy_public_pattern(a)==modem::legacy_public_pattern(b) &&
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
        result.requested_clock_ppm=result.legacy_coupled?result.frequency.requested_half_width_hz/config.carrier_hz*1e6:0;
        // Legacy confidence is conditional on the supplied channel being in
        // the actual finite bank. Only an explicit oscillator-domain policy
        // makes incomplete global coverage suppress even a central reference.
    }
    // The phase-coherent aperture spans intervening nulls. Refine the exact
    // original bank before reporting its count or pricing mapped work. Arrival
    // pruning is independent: a zero-width metadata probe avoids phase-grid
    // enumeration here and never changes the actual receiver's arrival window.
    if(modem::scheduled_bursts(config)&&!modem::packed_scheduled_symbols(config)&&
       config.receiver_strategy!=modem::ReceiverStrategy::amplitude_only&&config.data_key) {
        modem::PatternSearch probe;probe.start_offset_seconds=0;probe.start_uncertainty_seconds=0;
        probe.hypotheses=result.hypotheses;
        probe.admitted_frequency_half_width_hz=result.frequency.requested_half_width_hz;
        probe.admitted_clock_half_width_ppm=result.requested_clock_ppm;
        probe.complex_frequency_coverage=!result.frequency.limited;
        probe.complex_clock_coverage=result.clock_ppm>=result.requested_clock_ppm;
        const auto refined=modem::scheduled_complex_search(config,probe,utc?modem::utc_transmit_rate_limit(config):0);
        if(refined.complete_coverage) {
            result.hypotheses=refined.search.hypotheses;
            result.frequency.step_hz=refined.geometry.frequency_step_hz;
            result.frequency.requested_half_width_hz=refined.geometry.admitted_frequency_half_width_hz;
            result.clock_step_ppm=refined.geometry.clock_step_ppm;
            result.requested_clock_ppm=refined.geometry.admitted_clock_half_width_ppm;
            std::map<double,bool> frequencies;
            result.frequency.half_width_hz=result.clock_ppm=0;
            for(const auto& lane:result.hypotheses) {
                frequencies.emplace(lane.frequency_offset_hz,true);
                result.frequency.half_width_hz=std::max(result.frequency.half_width_hz,std::abs(lane.frequency_offset_hz));
                result.clock_ppm=std::max(result.clock_ppm,std::abs(lane.clock_error_ppm));
            }
            result.frequency.count=frequencies.size();
            result.frequency.limited=result.limited=false;
        } else result.limited|=refined.limited; // A finite cap is explicit, never coarsened.
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
    bool cost_available=true;
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
    // FFT acquisition separates host-only construction/setup from hypothetical
    // device arithmetic. Both remain included in parallel for the CPU total.
    long double fft_host_operations=0,fft_gpu_operations=0;
    long double fft_setup_host_operations=0,fft_setup_gpu_operations=0;
    bool fft_cost_split=false;
    bool sustained_modeled=false;
    long double sustained_serial=0,sustained_parallel=0,sustained_tracking=0;
    long double cohort_lifetime=0,sustained_instances=0;
    ReceiverSearchDiagnostics geometry;
    modem::ReceiverStrategyPlan strategy;
};
struct RollingFftWork {
    long double admissions=0,first_batches=0,full_batches=0,input_samples=0,maximum_batches=0;
    long double cadence=0,lifetime=0;
};
// Automatic Live refresh adds one fresh epoch at each observed UTC second.
// This prices the new acquisition cohorts, separately from the initial bank.
// Keep the initial cohort's conservative full observation allowance; do not
// interpret this as a whole-runtime bound (admitted/noise tracks can live longer).
RollingFftWork rolling_fft_work(long double samples,std::uint32_t sample_rate,
        long double bin,long double length,long double hop,long double initial_batch,
        long double symbol_seconds,long double prefix_seconds,unsigned epoch_radius,
        long double default_rate_uncertainty,const ReceiverTimingModel& timing_model,bool early_readiness=false,long double extra_start_samples=0) {
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
    const auto last_start=((2.L*epoch_radius+1)*sample_rate+extra_start_samples)/bin;
    const auto passes=last_start<initial_batch?1.L:2+outward_floor((last_start-initial_batch)/hop);
    const auto sampled_search_end=first_full+(passes-1)*hop_seconds;
    const auto retirement=(prefix_seconds+symbol_seconds+2.L*epoch_radius+1)/minimum_utc_rate;
    // Two UTC intervals conservatively cover strict retirement and integer
    // refresh boundaries. Hardware feeds are shorter than one UTC interval.
    const auto lifetime=std::max(sampled_search_end,retirement)+2/minimum_utc_rate;
    result.cadence=cadence;result.lifetime=lifetime;
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
    long double host_operations=0,gpu_operations=0,max_host_operations=0,max_gpu_operations=0;
};
// Local production-dispatch pipeline calibration, 10 October 2026, three alternating repeats.
// The measured operation is two forward transforms, a complex product and an
// inverse, including row copies/conversion/allocation. Plan creation is timed
// separately. Transfer to other convolution mixes is an approximation, not an
// isolated butterfly measurement or a total receiver runtime upper bound.
// Outside this bounded serial/coherent scope retain the engineering model.
struct FftPricing {
    bool calibrated=false,fp32=false,partition_native=false,template_calibrated=false,reuse=false;
    long double template_work=0;
    static long double interpolate(std::size_t n,bool single,bool plan,bool native_double) {
        constexpr std::array<std::size_t,5> sizes{256,1024,4096,32768,262144};
        constexpr std::array<long double,5> float_pipeline{4.676286e-6L,18.597768e-6L,79.155756e-6L,.844095238e-3L,12.189666667e-3L};
        constexpr std::array<long double,5> double_pipeline{5.690619e-6L,22.139785e-6L,118.255892e-6L,1.196233333e-3L,18.888e-3L};
        constexpr std::array<long double,5> generic_pipeline{9.64e-6L,45.480303e-6L,228.558935e-6L,2.397923077e-3L,28.560666667e-3L};
        constexpr std::array<long double,5> float_plan{4.358201e-6L,16.945799e-6L,63.275253e-6L,.50792e-3L,3.992142857e-3L};
        constexpr std::array<long double,5> double_plan{1.275677e-6L,2.749588e-6L,9.542748e-6L,72.115274e-6L,.593046512e-3L};
        const auto& values=plan?(single?float_plan:double_plan):(single?float_pipeline:(native_double?double_pipeline:generic_pipeline));
        const auto high=static_cast<std::size_t>(std::lower_bound(sizes.begin(),sizes.end(),n)-sizes.begin());
        if(high==0)return values.front()*static_cast<long double>(n)/sizes.front();
        if(high==sizes.size())return values.back()*static_cast<long double>(n)/sizes.back();
        if(n==sizes[high])return values[high];
        // Interpolate normalized cost over log2 size, retaining the measured
        // cache-size dependence rather than extrapolating a precision ratio.
        const auto x=std::log2(static_cast<long double>(n));
        const auto lo=std::log2(static_cast<long double>(sizes[high-1])),hi=std::log2(static_cast<long double>(sizes[high]));
        const auto denominator=[&](std::size_t k){return plan?static_cast<long double>(k):15.L*k*std::log2(static_cast<long double>(k))+6.L*k;};
        return denominator(n)*((hi-x)/(hi-lo)*values[high-1]/denominator(sizes[high-1])+
            (x-lo)/(hi-lo)*values[high]/denominator(sizes[high]));
    }
    long double numeric(long double operations,std::size_t n,bool partitioned=false)const {
        if(!calibrated||n<256||n>262144)return operations;
        return operations*interpolate(n,fp32,false,partitioned&&partition_native)*cpu_scoring_operations_per_second/
            (15.L*n*std::log2(static_cast<long double>(n))+6.L*n);
    }
    long double plan(std::size_t n,bool partitioned=false)const {
        return calibrated&&(fp32||(partitioned&&partition_native))&&n>=256&&n<=262144?
            interpolate(n,fp32,true,true)*cpu_scoring_operations_per_second:0;
    }
    long double templates(std::size_t frequencies,long double length,bool paired)const {
        if(!template_calibrated)return frequencies*length*template_work;
        // Existing five-lane L128000 preparation prototype: genuine pair
        // 215.54ns/bin/lane; separate-bit whole preparation358..369ns;
        // nominal cache200.26ns/bin, lane34.92ns/bin,
        // setup+cleanup5.193ms/group. Rounded local allowances include that
        // setup; whole native evaluates/interpolates the pair once per bit.
        constexpr long double original_pair=225e-9L,original_whole=375e-9L,nominal=250e-9L,lane=40e-9L;
        const auto seconds=reuse?.0052L+length*(nominal+frequencies*lane*(paired?1:2)):
            frequencies*length*(paired?original_pair:original_whole);
        return seconds*cpu_scoring_operations_per_second;
    }
};
// Identical canonical address groups to PatternReceiver::phase_groups. The
// phase lattice and stream index remain original; only work is intersected.
struct FftPhaseGroups {
    std::array<std::pair<std::uint64_t,std::uint64_t>,3> small{};
    std::uint64_t step=0;
    auto operator[](std::size_t i) const {return step?std::pair<std::uint64_t,std::uint64_t>{i*step,i*step}:small.at(i);}
};
FftPhaseGroups fft_phase_groups(
        const modem::Config& config,std::uint64_t index,std::uint64_t upper,
        std::uint64_t step,std::size_t& count) {
    FftPhaseGroups groups;count=0;
    if(config.fake_hopping&&config.fake_hopping->matched_gaps) {
        groups.step=step;count=upper/step+1;return groups;
    }
    auto lower=std::uint64_t{0};const auto symbol=modem::symbol_sample_count(config);
    while(lower<=upper) {
        const auto address=modem::symbol_stream_address(config,config.stream_epoch,lower,index);
        auto low=std::uint64_t{0},high=(upper-lower)/step;
        while(low<high) {
            const auto mid=low+(high-low+1)/2;
            const auto next=modem::symbol_stream_address(config,config.stream_epoch,lower+mid*step,index);
            if(next.epoch==address.epoch&&next.ordinal==address.ordinal)low=mid;else high=mid-1;
        }
        const auto end=lower+low*step;
        if(count==groups.small.size())throw Error("FFT planning phase groups exceed the finite symbol interval");
        groups.small[count++]={lower,end};if(end==upper)break;lower=end+step;
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
        long double minimum_rate,long double maximum_rate,const FftPricing& pricing,std::size_t& checks,std::stop_token stop,
        long double observed_bins=std::numeric_limits<long double>::infinity(),bool minimum_readiness=false,
        bool component_scheduling=false) {
    FftWindowWork result;
    auto active_pricing=pricing; // Exact numerical fallback is sticky per receiver bank.
    const auto symbol=modem::symbol_sample_count(config);
    const auto step=modem::canonical_phase_step(config);
    const auto upper=(modem::canonical_phase_extent(config)-1)/step*step;
    const auto radius=std::max<std::uint64_t>(1,symbol/2);
    const auto candidate_limit=modem::PatternSearch{}.candidate_limit;
    modem::detail::FftSearchGeometry geometry;geometry.sample_fit=sample_fit;
    geometry.drift_sections=1;geometry.differential_window_samples=0;
    const auto initial=modem::deferred_initial_symbols(config);
    if(initial>4096)return std::nullopt; // bounded planner scratch; keep broad upper work
    for(std::size_t b=0;b<batches;++b) {
        estimate_detail::check(stop);
        const auto first=b?initial_batch+(static_cast<std::uint64_t>(b)-1)*hop:0;
        const auto starts=b?hop:initial_batch;
        const auto groups_per_symbol=config.fake_hopping&&config.fake_hopping->matched_gaps?upper/step+1:3;
        if(groups_per_symbol>4096/initial)return std::nullopt; // retain conservative broad work, bounded planner scratch
        std::vector<modem::detail::PatternFftStartSelection> selections(groups_per_symbol*initial);
        std::size_t selection_count=0,ready_end=0;
        bool input_fft=false,selector_uncertain=false;
        modem::detail::PatternFftComponents components;
        for(std::uint64_t index=0;index<initial;++index) {
            std::size_t count=0;const auto groups=fft_phase_groups(config,index,upper,step,count);
            for(std::size_t g=0;g<count;++g) {
                if(++checks>1000000)return std::nullopt;
                auto [lower,last]=groups[g];auto span=symbol,position=index;
                if(modem::deferred_symbols(config)) {
                    const auto first=modem::scheduled_symbol_offset(config,config.stream_epoch,lower,0);
                    const auto delta=modem::scheduled_symbol_offset(config,config.stream_epoch,lower,index)-first;
                    lower+=first;last=lower;span=std::max<std::uint64_t>(1,delta);position=delta?1:0;
                } else last=modem::canonical_phase_cell_upper(config,last);
                auto selected=modem::detail::pattern_fft_select_starts(prior,span,position,
                    lower,last,1,first,starts,bin,minimum_rate,maximum_rate);
                // Widening the anchor can merge >64 narrow phase ranges. A
                // narrower runtime map could then overflow the selector and
                // keep its full job, rather than remain a selected subset.
                // Retain full work for that uncertain group in cohort pricing.
                if(minimum_readiness&&config.fake_hopping&&config.fake_hopping->matched_gaps&&(last-lower)/step>=64) {
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
        long double operations=0,host=0,gpu=0,jobs=0,positions=0,direct=0,transforms=0;
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
            std::vector<modem::detail::PatternFftStartSelection> local(selection_count);
            std::vector<std::optional<modem::detail::partitioned_paired::Requirements>> plans(selection_count);
            std::vector<std::optional<long double>> envelope_numeric(selection_count);
            bool paired=generated&&!sample_fit,direct_member=false;
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
                if(modem::detail::pattern_fft_direct_eligible(geometry,generated,k,!sample_fit)){direct_member=true;continue;}
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
            // A mixed cohort cannot enter whole native scoring after bounded
            // scoring declines it. Runtime then rebuilds original FP64 operands
            // and keeps that fallback for the bank. Unknown anchor subsets can
            // have the same mixed dispatch; do not promise native/cache savings.
            if(!paired&&(direct_member||minimum_readiness)) {
                active_pricing.fp32=false;active_pricing.reuse=false;
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
            const auto overhead=repeats*(2.L*transform+6.L*(length+observed-1));
            operations+=overhead;gpu+=overhead;
            if(!paired&&!minimum_readiness) {
                const auto numeric=repeats*(5*transform*fft_log+2.L*transform);
                operations+=active_pricing.numeric(numeric,transform);gpu+=numeric;
            }
            for(std::size_t j=0;j<selection_count;++j) {
                const auto& selected=local[j];const auto k=selected.selected_count;if(!k)continue;
                const auto generation=(generated||config.scramble||config.dsss)?active_pricing.templates(frequencies,length,paired):0.L;
                const auto evidence=40.L*k*frequencies;
                const auto full_numeric=5*transform*fft_log+
                    frequencies*(20*transform*fft_log+12.L*transform);
                const bool direct_job=modem::detail::pattern_fft_direct_eligible(geometry,generated,k,!sample_fit);
                long double numeric=0,plans_work=0;std::size_t numeric_size=transform;
                if(paired) {
                    if(direct_job) {
                        numeric=frequencies*length*tracking_pair_operations_per_bin*k;
                        numeric_size=0; // Direct products keep the engineering rate.
                        result.paired_direct_jobs+=static_cast<long double>(repeats)*frequencies;
                    } else if(minimum_readiness) {
                        numeric=std::max(4.L/3*(*envelope_numeric[j]),
                            frequencies*length*tracking_pair_operations_per_bin*std::min<std::size_t>(k,32));
                        // Unknown anchor can change the selected tile. Retain
                        // the slower calibrated ratio over every legal size.
                        numeric_size=0;
                        long double factor=1;
                        for(std::size_t n=256;n<=transform&&n<=262144;n*=2)
                            factor=std::max(factor,active_pricing.numeric(1,n,true));
                        numeric*=factor;
                        plans_work=active_pricing.plan(std::min<std::size_t>(32768,transform),true);
                    } else {
                        const auto& r=*plans[j];numeric_size=r.transform;
                        numeric=modem::detail::partitioned_paired::operations(r,frequencies)+
                            paired_numeric_overhead(r,frequencies);
                        plans_work=active_pricing.plan(r.transform,true);
                        result.partitioned_jobs+=frequencies;
                        result.partitioned_input_transforms+=r.input_tiles;
                        result.partitioned_template_transforms+=2.L*frequencies*r.chunks;
                        result.partitioned_inverse_transforms+=2.L*frequencies*r.max_job_output_tiles;
                        if(!result.partitioned_tile_min||r.tile_size<result.partitioned_tile_min)result.partitioned_tile_min=r.tile_size;
                        result.partitioned_tile_max=std::max(result.partitioned_tile_max,r.tile_size);
                    }
                } else if(minimum_readiness) {
                    const auto dot=modem::detail::pattern_fft_direct_eligible(geometry,generated,1,!sample_fit)?
                        frequencies*length*tracking_pair_operations_per_bin*std::min<std::size_t>(k,32):0.L;
                    numeric=std::max(1.2L*full_numeric+2.L*transform,dot);
                    numeric_size=0;
                    long double factor=1;
                    for(std::size_t n=256;n<=transform&&n<=262144;n*=2)
                        factor=std::max(factor,active_pricing.numeric(1,n));
                    numeric*=factor;
                    plans_work=active_pricing.plan(std::min<std::size_t>(32768,transform));
                } else {
                    numeric=frequencies*(direct_job?length*tracking_pair_operations_per_bin*k:
                        20*transform*fft_log+12.L*transform);
                    if(direct_job)numeric_size=0;
                }
                // Private waveform/cache construction and native plans remain
                // host work in the hypothetical GPU projection.
                const auto fixed=generation+plans_work;
                long double cost=fixed+evidence+active_pricing.numeric(numeric,numeric_size,paired);
                host+=repeats*fixed;gpu+=repeats*(numeric+evidence);
                // Rounded engineering allowance for bounded range validation,
                // both chooser inspections and selected-cell bookkeeping.
                if(generated&&!sample_fit) {
                    const auto controls=512.L*(minimum_readiness?65:selected.range_count+1);
                    cost+=controls;host+=repeats*controls;
                }
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
        result.host_operations+=host;result.gpu_operations+=gpu;
        result.max_host_operations=std::max(result.max_host_operations,host);
        result.max_gpu_operations=std::max(result.max_gpu_operations,gpu);
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
        const auto policy=arrival_policy(options,config);if(!policy)return std::nullopt;
        const auto map=receiver_arrival_map(options,config,epoch,
            epoch+policy->offset_seconds-origin_seconds+sign*anchor_half_width,
            0,timing.capture_seconds_per_frame.value_or(1./config.sample_rate),
            timing.capture_rate_uncertainty_fraction.value_or(static_cast<double>(rate_uncertainty)),
            std::max(clock_sync::audio_lag_radius(options.audio_timing_error_seconds),timing.capture_error_seconds.value_or(clock_sync::audio_lag_radius(options.audio_timing_error_seconds))),
            phase_upper);
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
// Resolve scheduled complex geometry before the legacy FFT estimator. Packed
// symbols use exact singleton calendar phases; split symbols use original
// mapped FP64 contraction over a refined physical-aperture bank. Neither null
// waits nor a canonical address-grid step imply contiguous waveform samples.
struct ReceiverLayoutContext {
    std::size_t cohort_capacity=0,maximum_consumers=128;
    bool shared_capture_reserved=false;
};
ReceiverLayoutContext receiver_layout_context(const transfer::Options& options,
        std::span<const modem::Config> profiles,std::size_t keys,ReceiverWorkMode mode,
        const live::Settings* actual) {
    auto settings=actual?*actual:live::Settings{};
    if(!actual) {
        settings.transfer=options;settings.content_limit=options.content_limit;
        settings.dsp_workspace_bytes=options.dsp_workspace_bytes;
        settings.simulation=mode==ReceiverWorkMode::sampled_simulation;
    } else {
        const auto& c=settings.transfer.modem;const auto& n=options.modem;
        if(c.sample_rate!=n.sample_rate || c.carrier_hz!=n.carrier_hz || c.bandwidth_hz!=n.bandwidth_hz ||
           c.spreading_mode!=n.spreading_mode || c.dsss_factor!=n.dsss_factor ||
           (c.dsss_factor>1 && c.outer_dsss_version!=n.outer_dsss_version) ||
           settings.simulation!=(mode==ReceiverWorkMode::sampled_simulation))
            throw Error("receiver settings contradict the planned physical waveform/route");
    }
    if(actual)settings=live::detail::normalized_layout(std::move(settings));
    else if(settings.transfer.key && settings.transfer.modem.spreading_mode==modem::SpreadingMode::pattern)
        settings.transfer.modem.scramble=true;
    for(const auto& profile:profiles)
        if(modem::scheduled_bursts(profile)&&profile.sample_rate!=options.modem.sample_rate)
            throw Error("scheduled shared-capture planning requires one original PCM sample rate");
    ReceiverLayoutContext out;out.cohort_capacity=live::detail::available_bank_capacity(settings);
    if(!modem::detail::scheduled_acquisition_policy::requested())return out;
    // The span can already contain repeated per-key families (GUI advisory);
    // keys is the remaining multiplier. Do not also multiply settings' keys.
    const auto reserve=[&](modem::Config c,std::size_t key_count,std::size_t profile_count,
                            unsigned radius,const std::optional<Crypto>& key) {
        if(key)c.data_key=key;
        if(!modem::scheduled_bursts(c) || !c.data_key || modem::packed_scheduled_symbols(c) ||
           !modem::pattern_pulse_enabled(c))return;
        const auto n=modem::symbol_sample_count(c);
        if(n<6ULL*c.sample_rate || n>=16ULL*c.sample_rate)return;
        const auto count=modem::receiver_strategy_bank_count(c,radius,key_count,profile_count);
        if(count>(std::numeric_limits<std::size_t>::max()-128)/2)
            throw Error("planned shared capture consumer reservation overflow");
        out.maximum_consumers=std::max(out.maximum_consumers,2*count+128);out.shared_capture_reserved=true;
    };
    for(const auto& c:profiles)reserve(c,keys,profiles.size(),options.search_seconds,options.key);
    if(actual) {
        // Reserve at least the same complete owner metadata as Live, including
        // its conservative configured-key count. This is a pure layout; it
        // never supplies an owner to runtime resolve_receiver_strategy.
        const auto& base=settings.transfer;
        const auto family=base.automatic_receive_profiles?
            tuning::receive_profiles(base.modem,base.receive_targets_db_hz,base.receive_pattern_mode,true):
            std::vector<modem::Config>{base.modem};
        const auto count=std::max<std::size_t>(1,settings.receive_keys.size()+std::size_t(base.key.has_value()));
        for(auto c:family) {
            if(!c.data_key && !settings.receive_keys.empty())c.data_key=settings.receive_keys.front();
            reserve(c,count,family.size(),base.search_seconds,base.key);
        }
    }
    return out;
}
void receiver_layout_share(modem::PatternSearch& search,const modem::Config& config,
        const ReceiverLayoutContext& layout,bool keyed) {
    auto available=layout.cohort_capacity;
    if(layout.shared_capture_reserved) {
        const auto capture=modem::detail::scheduled_capture::SharedScheduledCapture::cohort_layout(
            layout.cohort_capacity,layout.maximum_consumers);
        available-=capture.reserved_bytes;
    }
    const auto share=available/std::max<std::size_t>(1,search.receiver_bank_count);
    const auto control=live::receiver_control_margin(config,keyed);
    search.receiver_strategy_workspace_bytes=share>control?share-control:1;
    if(search.amplitude_receiver_bank_count) {
        const auto amplitude_share=available/search.amplitude_receiver_bank_count;
        search.amplitude_strategy_workspace_bytes=amplitude_share>control?amplitude_share-control:1;
    }
}
Work scheduled_complex_receiver_work(modem::Config config,const SearchBank& bank,long double samples,
        const transfer::Options& options,std::size_t profiles,std::size_t keys,
        std::size_t established_stream_bits,double local_window_seconds,ReceiverWorkMode work_mode,
        const ReceiverTimingModel& timing_model,const ReceiverLayoutContext* layout,std::stop_token stop) {
    estimate_detail::check(stop);
    if(options.key)config.data_key=options.key;
    config.stream_epoch=options.timestamp;
    config=modem::scheduled_estimate_config(config);
    const auto symbol=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const bool packed=modem::packed_scheduled_symbols(config);
    const auto epochs=2.L*options.search_seconds+1;
    const auto allowance=static_cast<long double>(options.dsp_workspace_bytes)/
        std::max(2.L,epochs*keys*profiles);
    const auto maximum_clock_ratio=std::max(1-bank.minimum_rate,bank.maximum_rate-1);
    modem::PatternSearch search;
    search.hypotheses=bank.hypotheses;
    search.admitted_frequency_half_width_hz=bank.frequency.requested_half_width_hz;
    search.admitted_clock_half_width_ppm=bank.requested_clock_ppm;
    search.complex_frequency_coverage=!bank.frequency.limited;
    search.complex_clock_coverage=bank.clock_ppm>=bank.requested_clock_ppm;
    search.differential_window_seconds=local_window_seconds;
    search.search_stream_phases=config.scramble||config.dsss;
    search.prefer_streamed_templates=true;
    search.start_offset_seconds=options.search_seconds+1.;
    search.start_uncertainty_seconds=options.search_seconds+1.;
    search.receiver_bank_count=modem::receiver_strategy_bank_count(config,options.search_seconds,keys,profiles);
    search.receiver_schedule_key_count=keys;
    receiver_layout_share(search,config,*layout,options.key.has_value());
    Work result;
    auto& g=result.geometry;
    g.arrival_window_available=true;g.combined_arrival_half_width_seconds=search.start_uncertainty_seconds;
    std::uint64_t first_phase=config.sample_rate,last_phase=0;
    if(packed)modem::for_each_packed_scheduled_phase(config,config.stream_epoch,[&](std::uint64_t phase) {
        estimate_detail::check(stop);
        first_phase=std::min(first_phase,phase);last_phase=std::max(last_phase,phase);
    });
    else {first_phase=0;last_phase=modem::canonical_phase_extent(config)-1;}
    if(first_phase<config.sample_rate&&arrival_model_available(options,config,work_mode,timing_model)&&
       (shared_hop_calendar(config)||(config.oscillator_search&&
        !(config.oscillator_search->reference==modem::OscillatorReference::shared_radio&&
          config.oscillator_search->rf_shift_hz!=0)))) {
        constexpr auto epoch=1800000000.L;
        const auto radius=options.search_seconds+1.L;
        const auto policy=arrival_policy(options,config);
        const auto capture_error=std::max(clock_sync::audio_lag_radius(options.audio_timing_error_seconds),
            timing_model.capture_error_seconds.value_or(clock_sync::audio_lag_radius(options.audio_timing_error_seconds)));
        const auto map=receiver_arrival_map(options,config,epoch,
            epoch+policy->offset_seconds-radius,0,
            timing_model.capture_seconds_per_frame.value_or(1./config.sample_rate),
            timing_model.capture_rate_uncertainty_fraction.value_or(static_cast<double>(maximum_clock_ratio)),
            capture_error,last_phase);
        if(map&&map->origin_samples+map->phase_scale*last_phase+map->half_width_samples>=0&&
           map->origin_samples+map->phase_scale*first_phase-map->half_width_samples<=2*radius*config.sample_rate) {
            search.qualified_start_window=modem::PatternStartWindow{
                map->origin_samples,map->phase_scale,map->half_width_samples};
            g.arrival_window_available=true;
            g.combined_arrival_half_width_seconds=static_cast<double>(map->half_width_samples/config.sample_rate);
        }
    }
    result.strategy=modem::plan_receiver_strategy(config,search,static_cast<std::size_t>(allowance),
        layout->cohort_capacity,layout->maximum_consumers,live::receiver_control_margin(config,options.key.has_value()));
    const auto& p=result.strategy;
    result.cost_available=std::isfinite(p.scheduled_mean_processing_ratio) && p.scheduled_mean_processing_ratio>=0;
    result.work_supported=p.supported&&result.cost_available;result.workspace_supported=!p.workspace_limited;
    result.epoch_hypotheses=epochs;
    result.timing_hypotheses=p.scheduled_timing_positions;
    result.full_timing_hypotheses=p.scheduled_full_timing_positions;
    result.timing_window_modeled=search.qualified_start_window.has_value();
    result.phase_groups=1;
    result.compact=!packed;
    result.bin_samples=packed?modem::pattern_projection_bin_samples(config,bank.frequency.half_width_hz):1;
    result.search_trials=std::max(1.,p.scheduled_full_timing_positions);
    if(established_stream_bits)result.tracking_windows=static_cast<long double>(established_stream_bits-1)+
        static_cast<long double>(modem::pattern_absence_samples(config))/symbol;
    // The strategy's complete-cohort engineering allowance is deliberately
    // retained at full physical input rate. Neither a nominal duty discount nor
    // a calibrated precision speedup is invented for this new calendar path.
    const auto ratio=result.cost_available?std::max(0.,p.scheduled_mean_processing_ratio)/profiles:0.;
    const auto elapsed=samples/config.sample_rate;
    result.serial=ratio*elapsed*serial_operations_per_second;
    const auto frontend=std::min(result.serial,static_cast<long double>(config.sample_rate)*35e-9L*
        search.receiver_bank_count/profiles*elapsed*serial_operations_per_second);
    result.search_serial=result.serial-frontend;
    result.sustained_modeled=p.supported&&result.cost_available;
    result.sustained_serial=ratio*serial_operations_per_second;
    result.sustained_instances=static_cast<long double>(search.receiver_bank_count)/profiles;
    const auto lifetime_source=modem::symbol_schedule_detail::add(symbol,
        modem::symbol_schedule_detail::add(modem::training_sample_count(config),
            modem::pattern_pulse_padding_samples(config)));
    result.cohort_lifetime=static_cast<long double>(modem::scheduled_physical_span_samples(config,lifetime_source))/
        config.sample_rate+2*options.search_seconds+3;
    g.sample_rate=config.sample_rate;g.frequency_search_multiplier=config.frequency_search_multiplier;
    g.dsss_factor=config.dsss_factor;g.outer_dsss_version=config.dsss_factor>1?
        config.outer_dsss_version:modem::OuterDsssVersion::legacy_v1;
    g.workspace_bytes=options.dsp_workspace_bytes;
    g.per_bank_workspace_bytes=finite_seconds(std::min(allowance,static_cast<long double>(search.receiver_strategy_workspace_bytes)));
    g.fine_chip_samples=chip;g.inner_chip_samples=chip*config.dsss_factor;g.symbol_samples=symbol;
    g.fine_chip_seconds=static_cast<double>(chip)/config.sample_rate;
    g.inner_chip_seconds=static_cast<double>(g.inner_chip_samples)/config.sample_rate;
    g.symbol_seconds=static_cast<double>(symbol)/config.sample_rate;
    g.receiver_strategy_requested=tuning::receiver_strategy_id(config.receiver_strategy);
    g.receiver_strategy_selected=modem::receiver_backend_name(p.backend);g.receiver_strategy_reason=p.reason;
    g.receiver_strategy_experimental=p.experimental;g.receiver_cpu_budget=config.receiver_cpu_budget;
    g.existing_processing_ratio=p.existing_processing_ratio;g.frequency_axis_processing_ratio=p.frequency_axis_processing_ratio;
    g.amplitude_processing_ratio=p.amplitude_processing_ratio;
    g.scheduled=p.scheduled;g.scheduled_cost_calibrated=p.scheduled_cost_calibrated;
    g.scheduled_timing_qualified=result.timing_window_modeled;
    g.scheduled_frame_hops=p.scheduled_frame_hops;g.scheduled_active_hops=p.scheduled_active_hops;
    g.scheduled_effective_multiplier=p.scheduled_effective_multiplier;
    g.scheduled_active_seconds=p.scheduled_active_seconds;g.scheduled_frame_seconds=p.scheduled_frame_seconds;
    g.scheduled_minimum_quiet_seconds=p.scheduled_minimum_quiet_seconds;g.scheduled_flat_duty=p.scheduled_flat_duty;
    g.scheduled_recording_union_upper=p.scheduled_recording_union_upper;
    g.scheduled_fft_union_upper=p.scheduled_fft_union_upper;g.scheduled_interval_width_seconds=p.scheduled_interval_width_seconds;
    g.scheduled_capture_frame_bytes_upper=p.scheduled_capture_frame_bytes_upper;g.scheduled_fft_seconds=p.scheduled_fft_seconds;
    g.scheduled_canonical_phases=p.scheduled_canonical_phases;g.canonical_phases=p.scheduled_canonical_phases;
    g.scheduled_timing_positions=p.scheduled_timing_positions;g.scheduled_full_timing_positions=p.scheduled_full_timing_positions;
    g.scheduled_mean_processing_ratio=p.scheduled_mean_processing_ratio;
    g.scheduled_deadline_processing_ratio=p.scheduled_deadline_processing_ratio;
    g.scheduled_guaranteed_recovery_seconds=p.scheduled_guaranteed_recovery_seconds;
    g.timing_grid_seconds=p.scheduled_timing_spacing_seconds;
    g.canonical_phase_step_seconds=p.scheduled_phase_spacing_seconds;
    g.retained_start_positions=p.scheduled_timing_positions;g.full_start_positions=p.scheduled_full_timing_positions;
    g.first_fft_seconds=p.scheduled_fft_seconds;g.backend=p.backend==modem::ReceiverBackend::existing_complex?
        (packed?"Packed calendar complex FFT acquisition":"Mapped split complex direct correlation"):g.receiver_strategy_selected;
    g.scope=packed?"Exact keyed singleton phase geometry at a representative epoch; full-input complete-cohort engineering allowance, not executed counters":
        "Refined physical-aperture complex frequency/clock bank; mapped source-symbol contraction in physical I/Q time; full capture and selected-cohort engineering allowance, not executed counters";
    g.scheduled_limit=packed?"Packed full-symbol complex decoding is experimental; complete live-bank CPU, rolling setup and sensitivity are uncalibrated. "
        "Keyed waits and null intervals are not failed symbols. Broad capture remains fully charged; six-second absence requires fully scored valid calendar slots":
        "Split complex decoding is experimental; mapped pulse/covariance work, constructors, rolling setup and complete live-bank CPU are uncalibrated. "
        "Null time is not observed absence; physical aperture includes every null interval. No symbol-sized FFT or source-time frequency grid is assumed";
    if(arrival_policy(options,config)&&!result.timing_window_modeled)
        g.fallback_reason="Full arrival-window work: capture dating, timing metadata or shared-reference geometry is unsupported";
    g.search_arithmetic=config.search_arithmetic;
    g.arithmetic_experimental=config.search_arithmetic!=modem::SearchArithmetic::fp64;
    const auto transform=p.scheduled_fft_seconds*config.sample_rate/result.bin_samples;
    const auto effective_workspace=std::min(allowance,static_cast<long double>(search.receiver_strategy_workspace_bytes));
    const auto differential=modem::detail::differential_window_samples(symbol,chip,config.sample_rate,local_window_seconds);
    const bool native=packed&&(config.search_arithmetic==modem::SearchArithmetic::default_mode||
        config.search_arithmetic==modem::SearchArithmetic::fp32)&&transform>=65536&&symbol>256&&
        modem::detail::drift_section_count(config)==1&&!differential&&
        static_cast<long double>(p.estimated_workspace_bytes)+24*transform+65536<=effective_workspace;
    g.arithmetic_operands=config.search_arithmetic==modem::SearchArithmetic::fp64?
        "Original unquantized FP64 operands":native?
        "Eligible native FP32 acquisition; FP64 range/cache fallback":"FP64 operands for packed small/unsupported native geometry";
    g.arithmetic_accumulation=native?"FP32 bulk acquisition; FP64 energy, covariance and final evidence":
        "FP64 products, accumulation, energy, covariance and final evidence";
    g.arithmetic_backend="Existing CPU complex FFT/direct execution with exact calendar starts; runtime dispatch retained";
    g.arithmetic_limit="Packed execution and preparation are not separately calibrated by precision; no arithmetic speedup is inferred from storage width. GPU execution unavailable.";
    g.template_reuse=config.search_arithmetic==modem::SearchArithmetic::fp64?
        "Exact caches allowed; original FP64 templates; approximate interpolation prohibited by FP64 force":
        "Exact calendar template/score reuse; eligible native close-clock reuse retains all identity and workspace checks";
    if(!packed) {
        const bool force=config.search_arithmetic==modem::SearchArithmetic::fp64;
        g.arithmetic_operands=force?"Original unquantized FP64 samples and independently generated complex templates":
            "Original FP64 samples and private candidates; native FP64 source-curve interpolation where eligible; original direct fallback";
        g.arithmetic_accumulation="FP64 I/Q products, real Gram terms, energy, reductions and scoring; absolute timing and outer-evidence reductions retain wider arithmetic";
        g.arithmetic_backend=force?"CPU mapped complex original-template direct correlation with eligible exact family reuse":
            "CPU mapped complex shared exact source families; bounded physical moments and source curves where eligible; original direct fallback";
        g.arithmetic_limit="Search-arithmetic policy has partial coverage: Default and FP32 minimum currently use the same FP64 mapped products, moments, covariance and scoring. Optional Temporal execution can dispatch ordered FP64 SIMD; native FP32 packing/execution and fine-chip source tables remain internal diagnostics. Complete-prefix measurements do not establish an additional CPU gain for those paths, so no precision speedup is credited here. Complete rolling-bank arithmetic/reuse crossovers and sensitivity remain uncalibrated outside the documented local scope; GPU execution unavailable";
        g.template_reuse=force?"Exact family/source construction and same-frequency fit reuse allowed; original templates at FP64; no approximate interpolation, including close-clock and finite-table curve reuse":
            "Bounded exact family/source construction and same-frequency raw/affine fit reuse; up to eight candidate phase roots per job. Eligible local/shared source-chip curves and finite-table intervals retain identity/support/partial-tail/limiter/gate checks; unsupported or exhausted scratch uses original execution. No close-clock interpolation. Optional caches are reclaimed for required admissions";
    }
    if(p.scheduled_implicit_acquisition) {
        const bool force=config.search_arithmetic==modem::SearchArithmetic::fp64;
        g.backend="Calendar-aware tiled complex screening + original-template refinement";
        g.scope="Pure bounded shared-capture/cohort layout and implicit admitted-family geometry; runtime requires an actual bound owner, consumer retention and successful allocation. This is not a measured execution counter or a resident-bank proof";
        g.arithmetic_operands=force?"Native FP64 tile/FFT operands and original FP64 templates":
            "Native FP32 tile/FFT operands; original FP64 refinement and fallback";
        g.arithmetic_accumulation="Native tile FFT/products at the selected precision; FP64 sufficient statistics, numerical certificates, real I/Q Gram and original final evidence; precise absolute timing retained";
        g.arithmetic_backend="Bounded calendar tiles, candidate-family transforms and certified rejection; plausible/unsupported ranges use original complex refinement";
        g.arithmetic_limit="Partial coverage: original guard refinement and numerical fallback remain FP64. Complete-execution CPU coefficients for this implicit shared-family path are unavailable; no precision or duty-cycle speedup is inferred. GPU execution unavailable";
        g.template_reuse=force?"Exact family/capture reuse allowed; original FP64 templates and no approximate clock interpolation":
            "Bounded exact family/capture reuse and certified nearby-clock proxies; unsupported or near-decision evidence uses original templates";
        g.scheduled_limit+=" Pure shared-owner memory reservation is modeled; actual runtime retention/allocation remain mandatory. Numerical CPU estimates and full-bank sensitivity are unavailable";
    }
    return result;
}

Work receiver_work(const modem::Config& config,const SearchBank& bank,long double samples,
                   const transfer::Options& options,std::size_t profiles,std::size_t keys,
                   std::size_t established_stream_bits,double local_window_seconds,ReceiverWorkMode work_mode,
                   const ReceiverTimingModel& timing_model,const ReceiverLayoutContext* layout,std::stop_token stop) {
    estimate_detail::check(stop);
    if(modem::scheduled_bursts(config)&&config.receiver_strategy!=modem::ReceiverStrategy::amplitude_only) {
        auto mapped=scheduled_complex_receiver_work(config,bank,samples,options,profiles,keys,
            established_stream_bits,local_window_seconds,work_mode,timing_model,layout,stop);
        if(mapped.strategy.backend==modem::ReceiverBackend::existing_complex)return mapped;
        // Automatic may still select the separate amplitude detector when the
        // complete physical-aperture complex bank/state cannot be represented.
        // Its existing work/eligibility model remains independent.
    }
    const auto symbol=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto drift_sections=modem::detail::drift_section_count(config);
    const auto differential_window=drift_sections>1 && config.pattern_symbols && config.spreading_mode==modem::SpreadingMode::pattern?
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
    // Existing-complex constructors can use the remaining cohort up to half
    // the total budget. Even an empty cohort must first reserve shared frontend
    // and receiver-control state. Keep the multi-bank estimate's legacy cap;
    // an equal strategy share is not the existing constructor's allocation.
    const auto control=live::receiver_control_margin(config,options.key.has_value());
    const auto available=layout->cohort_capacity>control?layout->cohort_capacity-control:0;
    const auto allowance=std::min(static_cast<long double>(available),
        static_cast<long double>(options.dsp_workspace_bytes)/std::max(2.L,banks*profiles));
    // Two uint32 maps plus a rounded fixed allowance for the two bounded
    // coarse/rotation caches and metadata (currently below 64 KiB).
    const auto outer_map_bytes=interleaved?
        64*1024.L+8.L*std::floor(static_cast<long double>(symbol)/chip):0.L;
    // Expanded coupled banks require the FFT path. A compact private hint or
    // insufficient memory cannot silently substitute a different search path.
    const bool correlator=!modem::deferred_symbols(config) && !coupled &&
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
    const auto phase_groups=config.fake_hopping&&config.fake_hopping->matched_gaps?
        static_cast<long double>((modem::canonical_phase_extent(config)-1)/
            modem::canonical_phase_step(config)+1):modem::deferred_symbols(config)?
        (modem::scheduled_symbol_offset(config,config.stream_epoch,0,0)<
            modem::canonical_phase_extent(config)-1?2.L:1.L):
        private_pattern&&symbol%config.sample_rate!=0?(symbol>=config.sample_rate?2.L:3.L):1.L;
    Work result;
    result.epoch_hypotheses=epochs;
    result.timing_hypotheses=starts*frequencies;result.phase_groups=static_cast<std::size_t>(phase_groups);
    result.full_timing_hypotheses=result.timing_hypotheses;
    auto search_starts=starts;
    std::optional<modem::PatternStartWindow> fft_prior,strategy_prior;
    std::uint64_t fft_phase_upper=0;
    // The assumed hardware model narrows only a backend which actually
    // enforces this prior. Compact uses a lattice count; short FFT skips only
    // complete excluded batches, retaining every mixed batch. The lattice count
    // uses a representative anchor with supplied capture metadata or explicit
    // selected bounds; it is not an actual running bank's hypothesis count.
    if((shared_hop_calendar(config)||(correlator&&symbol>=60.L*config.sample_rate)||
        (!correlator&&!coupled&&symbol<16.L*config.sample_rate))&&
       arrival_model_available(options,config,work_mode,timing_model)&&
       private_pattern&&config.spreading_mode==modem::SpreadingMode::pattern&&
       (shared_hop_calendar(config)||(config.oscillator_search&&
        !(config.oscillator_search->reference==modem::OscillatorReference::shared_radio&&
          config.oscillator_search->rf_shift_hz!=0)))) {
        const auto phase_step=modem::canonical_phase_step(config);
        const auto phase_upper=(modem::canonical_phase_extent(config)-1)/phase_step*phase_step;
        auto minimum_phase=std::uint64_t{0},maximum_phase=modem::canonical_phase_cell_upper(config,phase_upper);
        if(modem::deferred_symbols(config)) {
            minimum_phase=modem::scheduled_symbol_offset(config,config.stream_epoch,0,0);
            maximum_phase+=modem::scheduled_symbol_offset(config,config.stream_epoch,phase_upper,0);
        }
        const auto radius=options.search_seconds+1.L;
        constexpr auto epoch=1800000000.L;
        const auto capture_error=std::max(clock_sync::audio_lag_radius(options.audio_timing_error_seconds),
            timing_model.capture_error_seconds.value_or(clock_sync::audio_lag_radius(options.audio_timing_error_seconds)));
        const auto policy=arrival_policy(options,config);
        const auto map=receiver_arrival_map(options,config,epoch,
            epoch+policy->offset_seconds-radius,0,
            timing_model.capture_seconds_per_frame.value_or(1./config.sample_rate),
            timing_model.capture_rate_uncertainty_fraction.value_or(static_cast<double>(maximum_clock_ratio)),capture_error,
            maximum_phase);
        // Match Live: intersect a qualified prior with the original bank; do
        // not require its edge cells to be wholly contained in that bank.
        if(map&&map->origin_samples+map->phase_scale*maximum_phase+map->half_width_samples>=0&&
           map->origin_samples+map->phase_scale*minimum_phase-map->half_width_samples<=2*radius*config.sample_rate) {
            const modem::PatternStartWindow prior{map->origin_samples,map->phase_scale,map->half_width_samples};
            strategy_prior=prior;
            result.geometry.arrival_window_available=true;
            result.geometry.combined_arrival_half_width_seconds=static_cast<double>(map->half_width_samples/config.sample_rate);
            if(!correlator) {fft_prior=prior;fft_phase_upper=maximum_phase;}
            else {
            std::map<double,std::optional<std::size_t>> count_by_rate;
            long double retained=0;std::size_t intersections=0;bool bounded=true;
            for(const auto& hypothesis:bank.hypotheses) {
                estimate_detail::check(stop);
                auto [entry,fresh]=count_by_rate.try_emplace(hypothesis.clock_error_ppm);
                if(fresh) {
                    const auto rate=1+static_cast<long double>(hypothesis.clock_error_ppm)*1e-6L;
                    modem::detail::PatternStartLattice lattice(0,2*radius*config.sample_rate,
                        chip/(2*rate),0,maximum_phase,1,prior);
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
    const bool automatic_native=!correlator&&streamed_templates&&!sample_fit&&drift_sections==1&&
        !differential_window&&private_pattern&&symbol>256&&transform>=65536&&
        (config.search_arithmetic==modem::SearchArithmetic::default_mode||config.search_arithmetic==modem::SearchArithmetic::fp32);
    FftPricing pricing;
    const bool coherent_fft_pricing=!correlator&&!sample_fit&&drift_sections==1&&!differential_window&&
        !modem::search_arithmetic_is_int8(config.search_arithmetic);
    const bool native_scratch=fft_core_bytes+outer_map_bytes+24*transform+65536<=allowance;
    pricing.calibrated=coherent_fft_pricing;
    pricing.fp32=automatic_native&&native_scratch;
    pricing.partition_native=native_scratch;
    pricing.template_work=template_pair_work;
    // Template preparation measurements are for this finite shaped V2 family,
    // not a generic cryptographic/sample throughput coefficient.
    pricing.template_calibrated=coherent_fft_pricing&&interleaved&&config.sample_rate==40000&&
        chip==8&&bin==4&&length>=65536&&length<=262144;
    if(automatic_native&&bank.hypotheses.size()>=3) {
        modem::detail::FftSearchGeometry g;g.bins_per_symbol=static_cast<std::size_t>(length);
        g.bin_samples=bin;g.search_arithmetic=static_cast<std::uint32_t>(modem::SearchArithmetic::fp32);
        g.private_template_reuse=modem::detail::PrivateTemplateReuse::bounded_interpolation;
        g.drift_sections=1;g.pattern.chip_samples=chip;g.pattern.chips_per_symbol=modem::pattern_chips_per_symbol(config);
        g.pattern.symbol_samples=symbol;g.pattern.sample_rate=config.sample_rate;g.pattern.shaped=modem::pattern_pulse_enabled(config);
        g.pattern.legacy_public=modem::legacy_public_pattern(config);
        g.pattern.scramble=config.scramble;g.pattern.dsss=config.dsss;g.pattern.spreading_mode=static_cast<std::uint32_t>(config.spreading_mode);
        bool eligible=true;
        for(const auto& h:bank.hypotheses) {modem::detail::FftSearchJob job;
            job.clock_ratio=1+h.clock_error_ppm*1e-6;eligible&=modem::detail::FftPrivateTemplate::eligible(g,job);}
        // Reserve whole-native buffers/plan plus the bounded direct scratch;
        // this intentionally declines reuse when its actual workspace cannot
        // be proved from the per-bank geometry allowance.
        pricing.reuse=eligible&&fft_core_bytes+outer_map_bytes+24*transform+65536+
            modem::detail::FftPrivateTemplate::required_bytes(g)<=allowance;
    }
    result.compact=correlator;result.bin_samples=bin;
    result.geometry.sample_rate=config.sample_rate;
    result.geometry.frequency_search_multiplier=config.frequency_search_multiplier;
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
    const auto canonical_step=modem::canonical_phase_step(config);
    result.geometry.canonical_phase_step_seconds=static_cast<double>(canonical_step)/config.sample_rate;
    result.geometry.canonical_phases=static_cast<std::size_t>((modem::canonical_phase_extent(config)-1)/canonical_step+1);
    result.geometry.timing_grid_seconds=correlator?
        static_cast<double>((config.oscillator_search?chip/(2.L*bank.maximum_rate):
            std::max(1.L,std::floor(chip/(2.L*bank.maximum_rate))))/config.sample_rate):
        static_cast<double>(bin)/config.sample_rate;
    result.geometry.backend=correlator?"Compact direct correlation":"FFT acquisition";
    result.geometry.scope="Source-derived geometry; representative epoch/key work counts, not executed counters";
    if(config.fake_hopping && config.fake_hopping->matched_gaps)result.geometry.scope+=
        "; UTC gate-phase counts included; added gate-construction cost is not separately calibrated";
    if(modem::deferred_symbols(config))result.geometry.scope+=
        "; calendar slots and waits included; conservative work before optional bounded exact template/score reuse";
    if(fft_prior) {
        result.geometry.arrival_window_available=true;
        result.geometry.combined_arrival_half_width_seconds=static_cast<double>(fft_prior->half_width_samples/config.sample_rate);
    } else if(arrival_policy(options,config)&&result.geometry.fallback_reason.empty())result.geometry.fallback_reason=result.timing_window_modeled?
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
        compact_required<=allowance:fft_core_bytes+outer_map_bytes<=allowance;
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
    const auto initial_symbols=private_pattern&&!correlator?static_cast<long double>(modem::deferred_initial_symbols(config)):1.L;
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
        const auto constructor_templates=streamed_templates?0.L:
            pricing.templates(bank.hypotheses.size(),length,false);
        const auto constructor_numeric=streamed_templates?0.L:frequencies*10*transform*fft_log;
        const auto constructor_scoring=constructor_templates+
            pricing.numeric(constructor_numeric,static_cast<std::size_t>(transform));
        result.fft_setup_host_operations=constructor_templates*banks;
        result.fft_setup_gpu_operations=constructor_numeric*banks;
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
        long double filtered_host=0,filtered_gpu=0;
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
                    pricing,filtered_checks,stop,early_readiness?observed_bins:
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
                operations+=cohort->operations;filtered_host+=cohort->host_operations;
                filtered_gpu+=cohort->gpu_operations;retained_batches+=cohort->batches;
                envelope_fallback_hops+=cohort->envelope_fallback_hops;
            }
            if(bounded) {
                filtered_operations=operations*keys;filtered_host*=keys;filtered_gpu*=keys;blocks=retained_batches/epochs;
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
            const auto offset=modem::deferred_symbols(config)?
                static_cast<long double>(modem::scheduled_symbol_offset(config,config.stream_epoch,0,
                    modem::deferred_initial_symbols(config)-1))/.99L:3.L*symbol/.99L;
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
            long double calendar_extension=0;
            if(modem::deferred_symbols(config)) {
                const auto phase=modem::canonical_phase_extent(config)-1;
                // A 2/5-second calendar repeats at two UTC seconds. Bound both
                // epoch parities, including every retained initial slot.
                for(unsigned parity=0;parity<2;++parity) {
                    auto representative=config;representative.stream_epoch=parity;
                    calendar_extension=std::max(calendar_extension,phase+
                        static_cast<long double>(modem::scheduled_symbol_offset(representative,parity,phase,
                            modem::deferred_initial_symbols(representative)-1))/bank.minimum_rate);
                }
            }
            const auto fresh_full=rolling_fft_work(samples,config.sample_rate,bin,length,hop,initial_batch,
                static_cast<long double>(symbol)/config.sample_rate,prefix,options.search_seconds,
                maximum_clock_ratio,timing_model,false,calendar_extension);
            const auto fresh=early_readiness?rolling_fft_work(samples,config.sample_rate,bin,length,hop,initial_batch,
                static_cast<long double>(symbol)/config.sample_rate,prefix,options.search_seconds,
                maximum_clock_ratio,timing_model,true,calendar_extension):fresh_full;
            result.new_epoch_admissions=fresh.admissions;
            result.new_epoch_full_fft_batches=fresh_full.full_batches;
            result.new_epoch_qualified_ready_batch_slots=fresh.full_batches;
            result.new_epoch_retained_fft_batches=fft_prior?
                std::min(fresh.full_batches,fresh.first_batches*blocks):fresh.full_batches;
            std::optional<FftWindowWork> sustained_cohort;
            if(filtered_operations) {
                const auto prior=fft_anchor_window(options,config,timing_model,maximum_clock_ratio,
                    static_cast<long double>(options.search_seconds)-.5L,.5L,fft_phase_upper);
                const auto cohort=prior&&fresh.maximum_batches<=1000000?
                    fft_window_work(config,bank.hypotheses.size(),static_cast<std::size_t>(bin),
                        static_cast<std::size_t>(length),static_cast<std::size_t>(transform),fft_log,
                        static_cast<std::size_t>(hop),static_cast<std::size_t>(initial_batch),
                        static_cast<std::size_t>(fresh.maximum_batches),*prior,sample_fit,streamed_templates,
                        bank.minimum_rate,bank.maximum_rate,pricing,filtered_checks,stop,
                        std::numeric_limits<long double>::infinity(),true,early_readiness):std::nullopt;
                if(cohort) {
                    sustained_cohort=cohort;
                    result.new_epoch_retained_fft_batches=std::min(fresh.full_batches,fresh.first_batches*cohort->batches);
                    *filtered_operations+=keys*std::min(fresh.full_batches*cohort->max_operations,
                        fresh.first_batches*cohort->operations);
                    filtered_host+=keys*std::min(fresh.full_batches*cohort->max_host_operations,fresh.first_batches*cohort->host_operations);
                    filtered_gpu+=keys*std::min(fresh.full_batches*cohort->max_gpu_operations,fresh.first_batches*cohort->gpu_operations);
                } else {
                    // Unsupported fresh-cohort geometry keeps all its original
                    // work, rather than multiplying a representative discount.
                    const auto jobs=frequencies*phase_groups*initial_symbols;
                    const auto before=*filtered_operations;
                    if(early_readiness)*filtered_operations+=keys*maximum_component_dispatches*
                        (fresh.full_batches*(8.L*transform+initial_symbols*phase_groups*full_group_allowance)+
                        jobs*40*(fresh.first_batches*initial_batch+(fresh.full_batches-fresh.first_batches)*hop));
                    else *filtered_operations+=keys*(fresh.full_batches*(5*transform*fft_log*(1+4*jobs)+
                        jobs*(12*transform+template_pair_work*length))+
                        jobs*40*(fresh.first_batches*initial_batch+(fresh.full_batches-fresh.first_batches)*hop));
                    filtered_host+=*filtered_operations-before;
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
            result.fft_setup_host_operations+=fresh.admissions*constructor_templates*keys;
            result.fft_setup_gpu_operations+=fresh.admissions*constructor_numeric*keys;
            result.serial+=result.new_epoch_frontend_operations+result.new_epoch_setup_operations;

            // Listening outlives a one-bit simulation. Price each newly
            // admitted cohort through its COMPLETE lifetime, independently of
            // how many windows fit inside this draft's observation. Do not
            // differentiate the finite estimate: its initial cohorts retain a
            // conservative full-observation allowance even after Live retires
            // them. This calculation introduces no extra search enumeration.
            const auto cohort_batches=fresh.maximum_batches;
            const auto cohort_starts=initial_batch+(cohort_batches-1)*hop;
            const auto cohort_jobs=frequencies*phase_groups*initial_symbols;
            const bool generated=private_pattern||streamed_templates;
            long double cohort_scoring=0;
            auto guard_batches=cohort_batches;
            if(sustained_cohort) {
                cohort_scoring=sustained_cohort->operations;
                guard_batches=sustained_cohort->batches;
            } else if(early_readiness) {
                // Preserve the existing full-job/component fallback allowance
                // when a bounded union cannot be priced exactly.
                cohort_scoring=maximum_component_dispatches*(cohort_batches*
                    (8.L*transform+initial_symbols*phase_groups*full_group_allowance)+
                    cohort_jobs*40*cohort_starts);
            } else if(drift_sections==1&&!differential_window) {
                const auto generation=generated?cohort_batches*phase_groups*initial_symbols*
                    pricing.templates(bank.hypotheses.size(),length,false):0.L;
                const auto numeric=cohort_batches*(5*transform*fft_log*
                    (1+2*cohort_jobs*(generated?2:1))+cohort_jobs*12*transform)+
                    cohort_jobs*40*cohort_starts;
                cohort_scoring=generation+pricing.numeric(numeric,static_cast<std::size_t>(transform));
            }
            // Section/differential and compact sustained costs need their own
            // complete-cohort model; retain their existing explicit limits.
            if(drift_sections==1&&!differential_window&&fresh.cadence>0) {
                result.sustained_modeled=true;
                result.cohort_lifetime=fresh.lifetime;
                result.sustained_instances=keys*fresh.lifetime/fresh.cadence;
                result.sustained_serial=keys/fresh.cadence*(constructor_setup+
                    fresh.lifetime*config.sample_rate*projection_operations_per_sample);
                result.sustained_parallel=keys/fresh.cadence*(constructor_scoring+
                    cohort_scoring+pricing.plan(static_cast<std::size_t>(transform)));
                if(result.outer_presence) {
                    const auto peaks=std::min(static_cast<long double>(modem::PatternSearch{}.candidate_limit),
                        (1+std::ceil(2*hop*bin/symbol))*frequencies);
                    const auto pass=length*(template_pair_work+outer_operations_per_sample)+
                        std::ceil(static_cast<long double>(symbol)/chip)*outer_operations_per_chip;
                    result.sustained_serial+=keys/fresh.cadence*guard_batches*peaks*pass;
                }
            }
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
                (blocks*(8.L*transform+initial_symbols*phase_groups*full_group_allowance)+jobs*40*scored_starts);
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
        if(coherent_fft_pricing) {
            result.fft_cost_split=true;
            if(filtered_operations) {
                result.fft_host_operations=filtered_host;result.fft_gpu_operations=filtered_gpu;
            } else {
                // Whole FFT acquisition: generation is host work, numerical
                // transforms/products use the local width-specific pipeline.
                const auto generation=generate_templates?blocks*phase_groups*(private_pattern?4:1)*
                    pricing.templates(bank.hypotheses.size(),length,false)*banks:0.L;
                const auto old_generation=generate_templates?blocks*jobs*template_pair_work*length*banks:0.L;
                const auto numeric=std::max(0.L,result.parallel-old_generation);
                result.parallel=generation+pricing.numeric(numeric,static_cast<std::size_t>(transform));
                result.fft_host_operations=generation;result.fft_gpu_operations=numeric;
            }
            // Whole native plans persist per bank. Partition plans are already
            // charged for each generated group above. The persistent allowance
            // is conservative if a bank only executes paired/direct jobs.
            const auto plan=pricing.plan(static_cast<std::size_t>(transform))*
                (banks+result.new_epoch_admissions*keys);
            result.parallel+=plan;result.fft_host_operations+=plan;
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
    const auto setup_scoring=result.initial_epoch_setup_scoring_operations+result.new_epoch_setup_scoring_operations;
    if(result.sustained_modeled&&result.tracking_windows>0)
        result.sustained_tracking=result.tracking_serial/result.tracking_windows*config.sample_rate/symbol;
    result.parallel+=setup_scoring;
    if(result.fft_cost_split) {
        // Template generation remains host work; a hypothetical device can
        // transform cached constructor rows just as it can streamed rows.
        result.fft_host_operations+=result.fft_setup_host_operations;
        result.fft_gpu_operations+=result.fft_setup_gpu_operations;
    }
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
        const bool native=automatic_native&&native_scratch;
        arithmetic.arithmetic_operands=native?"FP32 acquisition operands; FP64 fallback":"FP64 operands for modeled geometry";
        arithmetic.arithmetic_accumulation=native?"FP32 acquisition; FP64 energy, covariance, timing and final evidence":
            "FP64 products, accumulation and evidence";
        arithmetic.arithmetic_backend=native?"Native serial streamed FP32 direct / partitioned / whole FFT acquisition":
            "FP64 compact / unsupported detector / cached or parallel fallback";
        arithmetic.arithmetic_limit="Experimental automatic/minimum policy, not receiver-wide FP32. INT8 is not selected automatically: complete-execution benefit is unproven. Cached/parallel and mixed-cohort generic FFTs retain FP64; memory/range checks can also require original FP64 before upload. Source coefficients, precise coordinates, energies and final scores remain wide. The model is a dispatch reference, not observed counters; local Ryzen native AVX2 FP32/FP64 and generic FP64 pipeline and separate plan setup measurements are interpolated only over 256..262144 points. V2 private-cache construction uses a separately labeled local measured family where applicable; other costs retain engineering allowances. GPU execution is unavailable.";
        arithmetic.template_reuse=native?
            "Exact caches plus separately admitted close-clock interpolation (<=0.001 input sample), only with all identity, even pulse-boundary and workspace checks. No observed paired detection disagreements in conditional tests; sensitivity bounds remain inconclusive and full-bank sensitivity is unqualified.":
            "Exact reuse only for this modeled fallback; approximate interpolation requires a supported native acquisition stage.";
    } else {
        arithmetic.arithmetic_operands="Original unquantized FP64 operands";
        arithmetic.arithmetic_accumulation="FP64 products, accumulation and evidence";
        arithmetic.arithmetic_backend="FP64 direct / partitioned / FFT / compact";
        arithmetic.arithmetic_limit="Independent original-template reference; exact/native partitioned or generic parallel FFT dispatch depends on geometry and workspace. Precise coordinates retain their existing wider precision. Generic FP64 and scratch-eligible native partitioned FP64 pipelines use separate bounded local measurements; original separate-bit and shared-pair template construction are priced separately in the measured family. Other work and rolling-cohort/guard allowances remain uncalibrated. GPU execution is unavailable.";
        arithmetic.template_reuse="Exact caches allowed; approximate interpolation prohibited by FP64 force.";
    }

    // Resolve the same receiver-local policy as execution. Do not multiply
    // authoritative frequency/rate pairs or narrow timing into a wider bank.
    modem::PatternSearch strategy_search;
    strategy_search.hypotheses=bank.hypotheses;
    strategy_search.admitted_frequency_half_width_hz=bank.frequency.requested_half_width_hz;
    strategy_search.admitted_clock_half_width_ppm=bank.requested_clock_ppm;
    strategy_search.complex_frequency_coverage=!bank.frequency.limited;
    strategy_search.complex_clock_coverage=bank.clock_ppm>=bank.requested_clock_ppm;
    strategy_search.differential_window_seconds=local_window_seconds;
    strategy_search.search_stream_phases=private_pattern;
    strategy_search.prefer_streamed_templates=streamed_templates;
    strategy_search.compact_clock_search=correlator;
    strategy_search.start_offset_seconds=options.search_seconds+1.;
    strategy_search.start_uncertainty_seconds=options.search_seconds+1.;
    strategy_search.qualified_start_window=strategy_prior;
    if(!strategy_prior&&arrival_model_available(options,config,work_mode,timing_model)) {
        const auto phase_upper=private_pattern?modem::canonical_phase_cell_upper(config,
            (modem::canonical_phase_extent(config)-1)/modem::canonical_phase_step(config)*modem::canonical_phase_step(config)):
            config.stream_phase_samples;
        constexpr auto epoch=1800000000.L;
        const auto radius=options.search_seconds+1.L;
        const auto policy=arrival_policy(options,config);
        const auto map=receiver_arrival_map(options,config,epoch,epoch+policy->offset_seconds-radius,0,
            timing_model.capture_seconds_per_frame.value_or(1./config.sample_rate),
            timing_model.capture_rate_uncertainty_fraction.value_or(static_cast<double>(maximum_clock_ratio)),
            std::max(clock_sync::audio_lag_radius(options.audio_timing_error_seconds),
                timing_model.capture_error_seconds.value_or(clock_sync::audio_lag_radius(options.audio_timing_error_seconds))),
            phase_upper);
        if(map&&map->origin_samples+map->phase_scale*phase_upper+map->half_width_samples>=0&&
           map->origin_samples-map->half_width_samples<=2*radius*config.sample_rate)
            strategy_search.amplitude_start_window=modem::PatternStartWindow{
                map->origin_samples,map->phase_scale,map->half_width_samples};
    }
    strategy_search.receiver_bank_count=modem::receiver_strategy_bank_count(config,
        options.search_seconds,keys,profiles);
    strategy_search.receiver_schedule_key_count=keys;
    if(!options.timestamp&&!modem::scheduled_bursts(config)) {
        live::Settings amplitude_settings;amplitude_settings.transfer=options;
        amplitude_settings.simulation=work_mode==ReceiverWorkMode::sampled_simulation;
        live::detail::prepare_public_amplitude_windows(config,amplitude_settings,private_pattern,strategy_search,keys,profiles);
        if(!private_pattern)
            strategy_search.amplitude_lookahead_epochs=live::detail::amplitude_epoch_lookahead(config,strategy_search,
                timing_model.capture_seconds_per_frame.value_or(1./config.sample_rate),
                timing_model.capture_rate_uncertainty_fraction.value_or(static_cast<double>(maximum_clock_ratio)));
        strategy_search.amplitude_receiver_bank_count=modem::receiver_strategy_amplitude_bank_count(config,strategy_search,keys,profiles);
    }
    // Both actual and inferred settings use Live's exact reservation arithmetic.
    // Analytical inputs need not satisfy Live device/route admission rules.
    if(layout)receiver_layout_share(strategy_search,config,*layout,options.key.has_value());
    auto strategy_config=config;
    // Planning receives unseeded profile geometry; execution attaches the same
    // selected root before constructing its keyed calendar. Never reject a
    // supported scheduled geometry merely because this public model copy has
    // not yet been through transfer::seeded_config. No bytes are generated here.
    if(options.key)strategy_config.data_key=options.key;
    if(modem::scheduled_bursts(strategy_config)) {
        strategy_config.stream_epoch=options.timestamp;
        strategy_config=modem::scheduled_estimate_config(strategy_config);
    }
    result.strategy=layout?modem::plan_receiver_strategy(strategy_config,strategy_search,
        static_cast<std::size_t>(std::max(0.L,allowance)),layout->cohort_capacity,layout->maximum_consumers,
        live::receiver_control_margin(strategy_config,options.key.has_value())):
        modem::resolve_receiver_strategy(strategy_config,strategy_search,static_cast<std::size_t>(std::max(0.L,allowance)));
    if(result.strategy.backend==modem::ReceiverBackend::amplitude_energy&&strategy_search.amplitude_start_window) {
        result.timing_window_modeled=true;
        result.geometry.arrival_window_available=true;
        result.geometry.combined_arrival_half_width_seconds=static_cast<double>(
            strategy_search.amplitude_start_window->half_width_samples/config.sample_rate);
    }
    auto& diagnostic=result.geometry;
    diagnostic.receiver_strategy_requested=tuning::receiver_strategy_id(config.receiver_strategy);
    diagnostic.receiver_strategy_selected=modem::receiver_backend_name(result.strategy.backend);
    diagnostic.receiver_strategy_reason=result.strategy.reason;
    diagnostic.receiver_strategy_experimental=result.strategy.experimental;
    diagnostic.receiver_cpu_budget=config.receiver_cpu_budget;
    diagnostic.existing_processing_ratio=result.strategy.existing_processing_ratio;
    diagnostic.frequency_axis_processing_ratio=result.strategy.frequency_axis_processing_ratio;
    diagnostic.amplitude_processing_ratio=result.strategy.amplitude_processing_ratio;
    diagnostic.scheduled=result.strategy.scheduled;
    diagnostic.scheduled_cost_calibrated=result.strategy.scheduled_cost_calibrated;
    diagnostic.scheduled_timing_qualified=strategy_search.qualified_start_window.has_value();
    diagnostic.scheduled_frame_hops=result.strategy.scheduled_frame_hops;
    diagnostic.scheduled_active_hops=result.strategy.scheduled_active_hops;
    diagnostic.scheduled_effective_multiplier=result.strategy.scheduled_effective_multiplier;
    diagnostic.scheduled_active_seconds=result.strategy.scheduled_active_seconds;
    diagnostic.scheduled_frame_seconds=result.strategy.scheduled_frame_seconds;
    diagnostic.scheduled_minimum_quiet_seconds=result.strategy.scheduled_minimum_quiet_seconds;
    diagnostic.scheduled_flat_duty=result.strategy.scheduled_flat_duty;
    diagnostic.scheduled_recording_union_upper=result.strategy.scheduled_recording_union_upper;
    diagnostic.scheduled_fft_union_upper=result.strategy.scheduled_fft_union_upper;
    diagnostic.scheduled_interval_width_seconds=result.strategy.scheduled_interval_width_seconds;
    diagnostic.scheduled_capture_frame_bytes_upper=result.strategy.scheduled_capture_frame_bytes_upper;
    diagnostic.scheduled_fft_seconds=result.strategy.scheduled_fft_seconds;
    diagnostic.scheduled_canonical_phases=result.strategy.scheduled_canonical_phases;
    diagnostic.scheduled_timing_positions=result.strategy.scheduled_timing_positions;
    diagnostic.scheduled_full_timing_positions=result.strategy.scheduled_full_timing_positions;
    diagnostic.scheduled_mean_processing_ratio=result.strategy.scheduled_mean_processing_ratio;
    diagnostic.scheduled_deadline_processing_ratio=result.strategy.scheduled_deadline_processing_ratio;
    diagnostic.scheduled_guaranteed_recovery_seconds=result.strategy.scheduled_guaranteed_recovery_seconds;
    if(diagnostic.scheduled) {
        result.timing_hypotheses=diagnostic.scheduled_timing_positions;
        result.full_timing_hypotheses=diagnostic.scheduled_full_timing_positions;
        result.timing_window_modeled=diagnostic.scheduled_timing_qualified;
        result.phase_groups=1;
        diagnostic.canonical_phases=diagnostic.scheduled_canonical_phases;
        diagnostic.timing_grid_seconds=result.strategy.scheduled_timing_spacing_seconds;
        diagnostic.canonical_phase_step_seconds=result.strategy.scheduled_phase_spacing_seconds;
        diagnostic.retained_start_positions=diagnostic.scheduled_timing_positions;
        diagnostic.full_start_positions=diagnostic.scheduled_full_timing_positions;
    }
    if(diagnostic.scheduled) {
        diagnostic.scheduled_limit="Split-symbol energy decoding is experimental; original timing and clock coverage is retained. "
            "Quiet time supplies no observed absence. Raw recording retains its full-input bound unless a complete "
            "live future-admission certificate proves omission; qualified FFT eligibility is separate. "
            "FFT interval allowance includes receiver phase/timing subcells and rate-cell age over an unconfirmed cohort; "
            "long-chip subdivisions preserve the original coverage and can add state. "
            "Split-DSSS construction, rolling setup, established longer-stream age and complete live-bank CPU costs are uncalibrated.";
    }
    result.work_supported&=result.strategy.supported;
    if(result.strategy.backend!=modem::ReceiverBackend::existing_complex) {
        result.work_supported=result.strategy.supported;
        result.workspace_supported=!result.strategy.workspace_limited;
        // New ratios are sustained TOTAL-cohort engineering costs including
        // row construction and frontend, not isolated numeric-kernel rates.
        // Divide out the common profile count before summing profile costs.
        // Startup/rolling lifecycle and component attribution are uncalibrated.
        const auto tracking_requested=result.strategy.backend==modem::ReceiverBackend::frequency_axis?
            result.strategy.complex_tracking_processing_ratio:0.;
        const auto mean_requested=result.strategy.scheduled?result.strategy.scheduled_mean_processing_ratio:
            result.strategy.selected_processing_ratio;
        result.cost_available=std::isfinite(mean_requested)&&mean_requested>=0&&
            std::isfinite(result.strategy.selected_processing_ratio)&&result.strategy.selected_processing_ratio>=0&&
            std::isfinite(tracking_requested)&&tracking_requested>=0;
        const auto tracking_ratio=result.cost_available?tracking_requested:0.;
        const auto mean_ratio=result.cost_available?mean_requested:0.;
        const auto selected_ratio=result.cost_available?result.strategy.selected_processing_ratio:0.;
        const auto per_profile=std::max(0.,mean_ratio-tracking_ratio)/profiles;
        const auto aggregate=std::max(0.,per_profile)*samples/config.sample_rate;
        result.serial=aggregate*serial_operations_per_second;
        result.search_serial=result.serial;
        result.parallel=result.kernel_serial=result.kernel_upper_serial=0;
        // Frequency-axis can accelerate acquisition and continuation; local
        // timing fits/refinement remain. Use the same reserve as selection;
        // acquisition alone must not be compared with the total CPU budget.
        result.tracking_serial=tracking_ratio*result.tracking_windows*symbol/config.sample_rate*serial_operations_per_second;
        result.kernel_upper_bound=false;result.fft_cost_split=false;
        result.fft_host_operations=result.fft_gpu_operations=0;
        result.sustained_modeled=result.strategy.supported&&result.cost_available;
        result.sustained_serial=std::max(0.,selected_ratio-tracking_ratio)/profiles*serial_operations_per_second;
        result.sustained_parallel=0;
        result.sustained_tracking=tracking_ratio*serial_operations_per_second/profiles;
        const auto source_lifetime=modem::symbol_schedule_detail::add(symbol,
            modem::symbol_schedule_detail::add(modem::training_sample_count(config),
                modem::pattern_pulse_padding_samples(config)));
        result.cohort_lifetime=static_cast<long double>(modem::scheduled_physical_span_samples(config,
            source_lifetime))/config.sample_rate+2*options.search_seconds+3;
        if(result.strategy.backend==modem::ReceiverBackend::amplitude_energy&&
           result.strategy.amplitude_cohort_lifetime_seconds>0)
            result.cohort_lifetime=result.strategy.amplitude_cohort_lifetime_seconds;
        result.sustained_instances=static_cast<long double>(result.strategy.selected_bank_count)/profiles;
        diagnostic.backend=diagnostic.receiver_strategy_selected;
        diagnostic.scope+="; alternative-strategy cost is sustained-cohort engineering work times observation duration; component split/startup uncalibrated; legacy FFT work counts are diagnostic only";
        if(result.strategy.backend==modem::ReceiverBackend::frequency_axis) {
            diagnostic.arithmetic_operands="Original FP64 complex templates and observations";
            diagnostic.arithmetic_accumulation="FP64 transforms, energies and scoring";
            diagnostic.arithmetic_backend="CPU frequency-axis CZT; near-decision direct refinement";
            diagnostic.arithmetic_limit="Native FP64 CZT currently selected at every arithmetic policy; unsupported cohorts retain original FP64 FFT execution.";
            diagnostic.template_reuse="Exact clock-row reuse only; independent key/epoch/bit/phase identity";
        }
        if(result.strategy.backend==modem::ReceiverBackend::amplitude_energy) {
            const bool double_fft=config.search_arithmetic==modem::SearchArithmetic::fp64;
            diagnostic.arithmetic_operands=double_fft?"Native FP64 FFT; original FP64 envelope templates":
                "Native FP32 FFT and envelope dots; original FP64 coefficients/shaping";
            diagnostic.arithmetic_accumulation="FP64 I/Q energy products; extended-precision moments and scoring";
            diagnostic.arithmetic_backend="CPU spectral-energy FFT + direct streaming envelope statistics; no GPU projection";
            diagnostic.arithmetic_limit="Experimental amplitude detector; coherent RX probabilities cannot be reused. Original finite-envelope quadrature remains approximate at every setting. Precise epochs and phase origins retained.";
            if(!double_fft)diagnostic.arithmetic_limit+=" Small/large signal ranges use original FP64 FFT.";
            diagnostic.template_reuse="Exact canonical phase groups and local coefficient preload; push-scoped spectra; no interpolated envelope templates";
            diagnostic.scope+="; amplitude per-input-block Live bookkeeping and epoch admission/retirement are not calibrated: this estimate is not a demonstrated real-time ceiling";
        }
    }

    return result;
}

// A bounded isolated-symbol V2 reference. Read the real transmitter's shaped,
// limited analytic PCM, including its finite pulse tails, and use the same
// projected template centers as the complex FFT receiver. This is conditional
// matched-statistic algebra, not an adaptive timing/key/epoch acquisition run.
bool shaped_source_statistics(const transfer::Options& options,const Work& work,
        detail::ReceiverProbabilityParameters& p,unsigned source_bit,ReceiverWorkMode mode,
        std::string& reason,std::stop_token stop,std::uint64_t maximum_source_samples=524288) {
    estimate_detail::check(stop);
    const auto& config=options.modem;
    const auto total=modem::symbol_sample_count(config),bin=work.bin_samples;
    if(work.compact||bin<2||total>maximum_source_samples||!bin||total%bin||total%(4*bin)||
       !modem::pattern_pulse_enabled(config)||work.noise_dimensions<16) {
        reason="Actual-source conditional reference requires a shaped complex FFT geometry, 2+ samples/bin, aligned quarters and at most "+std::to_string(maximum_source_samples)+" source samples";
        return false;
    }
    auto configured=config;
    if(options.timestamp)configured.stream_epoch=options.timestamp;
    if(options.key)configured=transfer::seeded_config(options,configured.stream_epoch);
    modem::detail::FftPatternParameters identity{configured.stream_epoch,modem::pattern_chip_samples(config),
        modem::pattern_chips_per_symbol(config),total,config.sample_rate,static_cast<std::uint32_t>(config.spreading_mode),
        1U,config.scramble?1U:0U,config.dsss?1U:0U,configured.spreading_seed,configured.dsss_seed,
        config.dsss_factor,static_cast<std::uint32_t>(config.outer_dsss_version)};
    identity.legacy_public=modem::legacy_public_pattern(configured);
    if(configured.fake_hopping && configured.fake_hopping->power_ramps) {
        const auto& plan=*configured.fake_hopping;
        identity.fhss_matched_gaps=plan.smooth_ramps?(plan.remote_guard_covered?2U:3U):(plan.matched_gaps?1U:0U);
        identity.fhss_ramp=plan.ramp_seconds;
        identity.fhss_dwell=plan.dwell_seconds;identity.fhss_clock=plan.clock_error_seconds;
        identity.fhss_peer_clock=plan.peer_clock_error_seconds;
        identity.fhss_audio=plan.audio_error_seconds;identity.fhss_region=plan.region_seconds;
    }
    const auto payload_begin=modem::symbol_schedule_detail::add(modem::pattern_pulse_padding_samples(configured),
        modem::scheduled_symbol_offset(configured,configured.stream_epoch,configured.stream_phase_samples,0));
    struct Statistics {
        std::array<double,4> weights{},other{};
        std::array<std::complex<double>,4> correlations{};
        std::array<std::array<std::complex<double>,2>,4> means{};
        std::array<detail::ReceiverProjectionSection,4> noise{};
        bool noncircular=false;
        double simulation_power=0,hardware_projection=0;
    };
    struct Entry {modem::detail::FftPatternParameters identity;std::uint64_t phase,bin,origin;double carrier;unsigned bit;Statistics value;};
    // No PCM or rows are retained. Exact waveform/seed/epoch/phase/source-bit
    // identity prevents sharing a private pattern with another candidate.
    thread_local std::vector<Entry> cache;
    Statistics statistics;bool found=false;
    for(const auto& entry:cache)if(entry.identity==identity&&entry.phase==configured.stream_phase_samples&&
        entry.bin==bin&&entry.origin==payload_begin&&entry.carrier==config.carrier_hz&&entry.bit==source_bit){statistics=entry.value;found=true;break;}
    if(!found) {
        modem::PatternCode code(configured,configured.stream_epoch);
        code.prepare_symbol(0,stop);
        modem::PatternTransmitter source(Bytes{static_cast<std::uint8_t>(source_bit)},configured,
            configured.stream_epoch,0,false);
        const auto amplitude=std::sqrt(2*modem::nominal_signal_power);
        std::array<std::complex<double>,4096> pcm{};
        std::array<std::complex<double>,4> cross{},pseudo_a{},pseudo_b{},pseudo_cross{};
        std::complex<double> cell{};std::uint64_t emitted=0,payload=0;
        long double physical=0,projected=0;
        const auto omega=2*std::numbers::pi*config.carrier_hz/config.sample_rate;
        const auto sine=std::sin(omega);
        const auto image=std::abs(sine)>1e-12?std::sin(static_cast<double>(bin)*omega)/(static_cast<double>(bin)*sine):1.;
        while(payload<total) {
            estimate_detail::check(stop);
            const auto count=source.read_analytic(pcm,stop);
            if(!count){reason="V2 shaped source ended before its complete reference symbol";return false;}
            auto rotation=std::polar(1.,-omega*static_cast<double>(emitted));
            const auto step=std::polar(1.,-omega);
            for(std::size_t i=0;i<count;++i,++emitted,rotation*=step) {
                if(emitted<payload_begin||emitted-payload_begin>=total)continue;
                const auto real=pcm[i].real()/amplitude;
                physical+=2*real*real;
                // Match real PCM input, including its conjugate carrier image.
                cell+=2.*pcm[i].real()*rotation/amplitude;++payload;
                if(payload%bin)continue;
                const auto first=payload-bin,section=static_cast<std::size_t>(first/(total/4));
                const auto position=static_cast<double>(first)+(static_cast<double>(bin)-1)/2;
                const auto pair=code.shaped_values(0,position);
                const auto a=pair[source_bit],b=pair[1-source_bit],s=cell/static_cast<double>(bin);
                projected+=std::norm(s);
                statistics.weights[section]+=std::norm(a);statistics.other[section]+=std::norm(b);
                cross[section]+=a*std::conj(b);
                statistics.means[section][0]+=s*std::conj(a);statistics.means[section][1]+=s*std::conj(b);
                auto& noise=statistics.noise[section];++noise.samples;
                noise.variance={(1+image)/2,(1-image)/2};
                // Principal axes of the real-PCM boxcar covariance. Retain
                // the signed carrier image and the actual source-padding phase.
                auto axis=std::polar(1.,-omega*(static_cast<double>(payload_begin)+position));
                for(unsigned plane=0;plane<2;++plane,axis*=std::complex<double>{0,1}) {
                    const auto x=axis*std::conj(a),y=axis*std::conj(b),z=axis*std::conj(s);
                    const std::array<double,6> v{x.real(),x.imag(),y.real(),y.imag(),z.real(),z.imag()};
                    for(unsigned row=0;row<6;++row)for(unsigned column=0;column<6;++column)
                        noise.gram[plane][6*row+column]+=v[row]*v[column];
                }
                const auto carrier=image*std::polar(1.,2*omega*position);
                pseudo_a[section]+=a*a*carrier;pseudo_b[section]+=b*b*carrier;pseudo_cross[section]+=a*b*carrier;
                cell={};
            }
        }
        const auto projected_reference=static_cast<double>(projected);
        const auto total_a=std::accumulate(statistics.weights.begin(),statistics.weights.end(),0.);
        const auto total_b=std::accumulate(statistics.other.begin(),statistics.other.end(),0.);
        if(!(projected_reference>0&&total_a>0&&total_b>0)) {
            reason="V2 shaped source/template energy is degenerate";return false;
        }
        double represented=0;
        for(std::size_t j=0;j<4;++j) {
            const auto a=statistics.weights[j],b=statistics.other[j],denominator=std::sqrt(a*b);
            if(!(denominator>0)){reason="Interleave source has an empty template section";return false;}
            statistics.noncircular|=std::abs(pseudo_a[j])>.01*a||std::abs(pseudo_b[j])>.01*b||
                std::abs(pseudo_cross[j])>.01*denominator;
            const std::array<double,6> scale{std::sqrt(a),std::sqrt(a),std::sqrt(b),std::sqrt(b),
                std::sqrt(projected_reference),std::sqrt(projected_reference)};
            for(auto& gram:statistics.noise[j].gram)
                for(unsigned row=0;row<6;++row)for(unsigned column=0;column<6;++column)
                    gram[6*row+column]/=scale[row]*scale[column];
            const auto rho=cross[j]/denominator;
            if(std::norm(rho)>=.999999) {reason="V2 alternative templates are nearly rank deficient";return false;}
            statistics.correlations[j]=rho;
            statistics.means[j][0]/=std::sqrt(a*projected_reference);
            statistics.means[j][1]/=std::sqrt(b*projected_reference);
            const auto residual=(statistics.means[j][1]-rho*statistics.means[j][0])/std::sqrt(1-std::norm(rho));
            represented+=std::norm(statistics.means[j][0])+std::norm(residual);
            statistics.weights[j]/=total_a;statistics.other[j]/=total_b;
        }
        if(represented>1+1e-8) {reason="V2 projected signal means exceed complete physical energy";return false;}
        statistics.simulation_power=static_cast<double>(projected*bin/total);
        statistics.hardware_projection=static_cast<double>(projected*bin/physical);
        estimate_detail::check(stop);
        if(cache.size()==8)cache.erase(cache.begin());
        cache.push_back({identity,configured.stream_phase_samples,bin,payload_begin,config.carrier_hz,source_bit,statistics});
    }
    p.weights=statistics.weights;p.alternative_weights=statistics.other;
    p.correlations=statistics.correlations;p.section_signal_coefficients=statistics.means;
    if(statistics.noncircular) {
        for(const auto& section:statistics.noise)if(section.samples<8||
            !(section.variance[0]>0&&section.variance[1]>0)) {
            reason="Noncircular Interleave reference requires eight bins per quarter and two nondegenerate noise planes";
            return false;
        }
        p.projected_noise=statistics.noise;
    }
    // Simulation's AWGN remains referenced to nominal_signal_power. Hardware
    // link budgets instead take the user's measured average transmitted power;
    // retain shape/projection mismatch without charging digital backoff twice.
    p.signal_energy*=mode==ReceiverWorkMode::sampled_simulation?statistics.simulation_power:statistics.hardware_projection;
    return true;
}

// Build the same nominal local template statistics used by FFT matching.
// This work is bounded by projected observations, never represented PCM time.
// Compact receivers use a bounded per-chip quadrature for shaped templates.
// Unsupported cases keep their timing/resource diagnostics without inventing
// a reception percentage from a different detector.
bool real_differential_statistics(const transfer::Options& options,std::uint64_t window,
                                 detail::ReceiverProbabilityParameters& p,unsigned source_bit,ReceiverWorkMode mode,std::stop_token stop) {
    estimate_detail::check(stop);
    const auto& config=options.modem;
    const auto total=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto step=modem::pattern_pulse_enabled(config)?std::max<std::uint64_t>(1,chip/16):chip;
    // Same compact per-chip quadrature limit as the aligned approximation.
    // Intersections and a short final block are length weighted, never padded.
    if(!step || total/step>maximum_probability_observations || total/window>maximum_probability_windows)return false;
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
    if(mode!=ReceiverWorkMode::sampled_simulation) {
        double physical=0;for(const auto& atom:p.real_atoms)physical+=atom.signal_energy[0];
        if(!(physical>0))return false;
        // The radio power is the measured source power. Normalize the fixed
        // initial-phase source, preserving finite Gram/projection losses.
        for(auto& atom:p.real_atoms) {
            for(auto& x:atom.signal_cos)x/=std::sqrt(physical);
            for(auto& x:atom.signal_sin)x/=std::sqrt(physical);
            for(auto& x:atom.signal_energy)x/=physical;
        }
    }
    return true;
}

bool differential_statistics(const transfer::Options& options,const Work& work,
                             std::uint64_t window,detail::ReceiverProbabilityParameters& p,unsigned source_bit,ReceiverWorkMode mode,std::stop_token stop) {
    estimate_detail::check(stop);
    const auto& config=options.modem;
    const auto total=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto windows=total/window;
    if(windows>maximum_probability_windows)return false;
    if(total%window || windows%4) {
        // The old circular approximation has no representation of a tail or
        // a window crossing a drift boundary. Preserve it for aligned cases;
        // raw compact fits use the joint real-covariance atom model instead.
        return work.compact&&real_differential_statistics(options,window,p,source_bit,mode,stop);
    }
    // Private raw-bin FFT fits use a different trace-only legacy score scale;
    // the joint model currently supports compact raw fits or complex FFT bins.
    if(!work.compact&&work.bin_samples==1&&(config.scramble||config.dsss))return false;
    auto step=work.compact?std::max<std::uint64_t>(1,chip/16):work.bin_samples;
    if(!modem::pattern_pulse_enabled(config)&&work.compact)step=chip;
    const auto quadrature=modem::pattern_pulse_enabled(config)&&!work.compact?
        std::min<std::uint64_t>(step,4):1;
    if(!step || total/step>maximum_probability_observations/quadrature || total%step || window%step)return false;
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
    double source_energy=0,physical_energy=0;
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
        if(!modem::pattern_pulse_enabled(config))physical_energy+=std::norm(source);
        if(modem::pattern_pulse_enabled(config)) {
            source={};
            for(std::uint64_t node=0;node<quadrature;++node) {
                const auto sample=static_cast<double>(at)+
                    (static_cast<double>(node)+.5)*static_cast<double>(step)/static_cast<double>(quadrature)-.5;
                const auto node_source=modem::pattern_limit_pcm(amplitude*code.shaped_value(0,source_bit,sample))/amplitude;
                source+=node_source;physical_energy+=std::norm(node_source)/static_cast<double>(quadrature);
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
    p.signal_energy*=source_energy/(mode==ReceiverWorkMode::sampled_simulation?
        static_cast<double>(total/step):physical_energy);
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

static Estimate estimate_impl(const transfer::Estimate& transmission,const transfer::Options& source_options,bool raw_bits,
                  const modem::ChannelConfig& channel,std::span<const modem::Config> profiles,std::size_t keys,
                  bool compute_probability,double local_window_seconds,std::size_t probability_trials,
                  ReceiverWorkMode work_mode,ReceiverTimingModel timing_model,const live::Settings* actual_settings,std::stop_token stop) {
    estimate_detail::check(stop);
    auto options=source_options;
    options.search_seconds=live::detail::effective_epoch_search_seconds(options,false);
    modem::validate(options.modem);modem::validate_channel(options.modem,channel);
    if(!std::isfinite(transmission.total_seconds) || transmission.total_seconds<0 || !keys ||
       !std::isfinite(local_window_seconds) || local_window_seconds<0)
        throw Error("invalid simulation estimate input");
    for(const auto& value:{timing_model.capture_error_seconds,timing_model.capture_rate_uncertainty_fraction})
        if(value&&(!std::isfinite(*value)||*value<0))throw Error("invalid receiver timing model");
    if(timing_model.capture_seconds_per_frame&&(!std::isfinite(*timing_model.capture_seconds_per_frame)||
        *timing_model.capture_seconds_per_frame<=0))throw Error("invalid receiver capture slope");
    if(profiles.empty())profiles=std::span(&options.modem,1);
    std::optional<ReceiverLayoutContext> layout;
    layout=receiver_layout_context(options,profiles,keys,work_mode,actual_settings);
    Estimate result;result.receiver_profiles=profiles.size();result.receiver_work_mode=work_mode;
    result.probability_reference_only=arrival_policy(options,options.modem).has_value()||options.modem.dsss_factor>1;
    result.receiver_work_assumptions=work_mode==ReceiverWorkMode::sampled_simulation?
        "Sampled simulation startup and settling epochs; original oscillator bank":
        work_mode==ReceiverWorkMode::hardware_fallback?
        "Hardware initial epochs; full arrival-window fallback, configured peer UTC correction bank retained":
        work_mode==ReceiverWorkMode::hardware_calendar_model?
        "Assumed UTC hop-calendar model; declared endpoint/Audio/propagation bounds, nominal capture slope; not hardware timing evidence":
        "Supplied capture timing model; admitted UTC correction bank, full arrival-window fallback";
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
    const auto differential_window=result.drift_sections>1 && config.pattern_symbols && config.spreading_mode==modem::SpreadingMode::pattern?
        modem::detail::differential_window_samples(samples_per_symbol,modem::pattern_chip_samples(config),
            config.sample_rate,local_window_seconds):0;
    result.differential_windows=differential_window?samples_per_symbol/differential_window:0;
    result.differential_window_seconds=static_cast<double>(differential_window)/config.sample_rate;
    if(differential_window)result.coherent_reference_only=false;
    const auto chip_seconds=static_cast<long double>(modem::pattern_chip_samples(config))/config.sample_rate;
    // The sampled transport adds this complete-symbol absence and lookahead
    // after the exact waveform, including its settling and suppression noise.
    // Calendar-dependent absence extents require the selected immutable root,
    // just as the transmission and receiver work builders do. The public
    // options profile need not already have passed through seeded_config().
    auto absence_config=config;
    if(options.key)absence_config.data_key=options.key;
    const auto tail=static_cast<long double>(modem::pattern_absence_capture_samples(absence_config))/config.sample_rate+1+
        2.L*modem::pattern_pulse_padding_samples(config)/config.sample_rate;
    // Seeded startup is 50..300 ms plus a fractional sample. Its mean is
    // 175 ms; receiver-clock delay is not shortened by transmitter clock rate.
    const auto media=static_cast<long double>(transmission.total_seconds)/(1+channel.clock_error_ppm*1e-6L)+
        static_cast<long double>(channel.delay_samples)/config.sample_rate+.175L+tail;
    result.simulated_seconds=finite_seconds(media);
    const auto samples=media*config.sample_rate;
    long double serial=samples*channel_operations_per_sample,parallel=0,gpu_host=0,gpu_numeric=0,tracking_serial=0,tracking_windows=0,trials=1;
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
                profile.oscillator_search==entry.profile->oscillator_search&&
                profile.receiver_strategy==entry.profile->receiver_strategy&&
                profile.receiver_cpu_budget==entry.profile->receiver_cpu_budget&&
                profile.frequency_search_multiplier==entry.profile->frequency_search_multiplier;
        });
        if(found!=banks.end())return static_cast<std::size_t>(found-banks.begin());
        auto keyed_profile=profile;
        if(options.key)keyed_profile.data_key=options.key;
        banks.push_back({&profile,search_bank(keyed_profile,arrival_policy(options,keyed_profile).has_value()&&
            work_mode!=ReceiverWorkMode::sampled_simulation)});
        return banks.size()-1;
    };
    std::size_t matching_bank=0;
    bool matching_supported=false,all_work_supported=true,all_cost_available=true;
    bool all_sustained_modeled=true;
    long double sustained_cpu=0,fallback_sustained_cpu=0;
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
                          matches?transmission.wire_bits:0,local_window_seconds,work_mode,timing_model,layout?&*layout:nullptr,stop);
        if(existing==work_entries.end())work_entries.push_back({index,training_samples,work});
        serial+=work.serial;parallel+=work.parallel;all_work_supported&=work.work_supported;
        result.gpu_work_supported&=work.strategy.backend==modem::ReceiverBackend::existing_complex && !work.strategy.scheduled;
        gpu_host+=work.fft_cost_split?work.fft_host_operations:0;
        gpu_numeric+=work.fft_cost_split?work.fft_gpu_operations:work.parallel;
        const auto fallback_work=work.timing_window_modeled?
            receiver_work(profile,candidate_bank,media*profile.sample_rate,options,profiles.size(),keys,
                matches?transmission.wire_bits:0,local_window_seconds,ReceiverWorkMode::hardware_fallback,timing_model,layout?&*layout:nullptr,stop):work;
        fallback_serial+=fallback_work.serial;fallback_parallel+=fallback_work.parallel;
        fallback_tracking+=fallback_work.tracking_serial;
        all_sustained_modeled&=work.sustained_modeled&&fallback_work.sustained_modeled;
        all_cost_available&=work.cost_available&&fallback_work.cost_available;
        sustained_cpu+=(work.sustained_serial+work.sustained_tracking)/serial_operations_per_second+
            work.sustained_parallel/cpu_scoring_operations_per_second;
        fallback_sustained_cpu+=(fallback_work.sustained_serial+fallback_work.sustained_tracking)/serial_operations_per_second+
            fallback_work.sustained_parallel/cpu_scoring_operations_per_second;
        result.hardware_cohort_lifetime_seconds=std::max(result.hardware_cohort_lifetime_seconds,
            finite_seconds(work.cohort_lifetime));
        result.hardware_sustained_receiver_instances+=finite_seconds(work.sustained_instances);
        receiver_frontend+=work.serial-work.kernel_serial-work.search_serial;kernel_serial+=work.kernel_serial;
        kernel_upper_serial+=work.kernel_upper_serial;
        search_serial+=work.search_serial;
        any_pulse_projected|=work.pulse_projected||work.pulse_segmented;
        any_pulse_segmented|=work.pulse_segmented;any_kernel_upper_bound|=work.kernel_upper_bound;
        tracking_serial+=work.tracking_serial;tracking_windows+=work.tracking_windows;
        if(matches) {
            const bool frequency_covered=within_representation_bound(frequency,
                work.strategy.backend==modem::ReceiverBackend::amplitude_energy?
                    work.strategy.amplitude_frequency_half_width_hz:candidate_bank.frequency.half_width_hz);
            const bool clock_covered=work.strategy.backend==modem::ReceiverBackend::amplitude_energy?
                within_representation_bound(channel.clock_error_ppm,work.strategy.amplitude_clock_half_width_ppm):
                (!profile.oscillator_search||paired_clock_coverage(candidate_bank,frequency,channel.clock_error_ppm));
            const bool coverage_limited=candidate_bank.limited&&
                !(work.strategy.full_frequency_coverage&&work.strategy.full_clock_coverage);
            const bool supported=frequency_covered&&clock_covered&&!coverage_limited&&
                work.workspace_supported&&work.strategy.supported;
            if(!matching_profile||(supported&&(!matching_supported||work.acquisition_threshold<matching_work.acquisition_threshold))) {
                matching_work=work;matching_profile=&profile;matching_supported=supported;
                matching_bank=index;
                trials=work.search_trials;
            }
        }
    }
    result.receiver_workspace_supported=matching_profile&&matching_work.workspace_supported&&layout->cohort_capacity>0;
    result.receiver_work_supported=matching_profile&&all_work_supported;
    result.hardware_sustained_modeled=matching_profile&&all_sustained_modeled;
    if(result.hardware_sustained_modeled) {
        result.hardware_sustained_realtime_ratio=finite_seconds(sustained_cpu);
        result.fallback_hardware_sustained_realtime_ratio=finite_seconds(fallback_sustained_cpu);
    }
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
    result.gpu_seconds=finite_seconds(.11L+serial_seconds+gpu_host/cpu_scoring_operations_per_second+gpu_numeric/gpu_scoring_operations_per_second+
        samples*sizeof(float)/gpu_transfer_bytes_per_second+processing);
    if(!result.gpu_work_supported)result.gpu_seconds=std::numeric_limits<double>::quiet_NaN();
    if(!all_cost_available) {
        const auto unknown=std::numeric_limits<double>::quiet_NaN();
        result.cpu_seconds=result.receiver_cpu_seconds=result.fallback_receiver_cpu_seconds=unknown;
        result.gpu_seconds=unknown;result.gpu_work_supported=false;
        result.hardware_sustained_modeled=false;
        result.hardware_sustained_realtime_ratio=result.fallback_hardware_sustained_realtime_ratio=unknown;
        result.receiver_frontend_seconds=result.receiver_search_seconds=unknown;
        result.receiver_kernel_rebuild_seconds=result.receiver_kernel_rebuild_upper_seconds=unknown;
        result.receiver_work_supported=false;
    }
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
    result.carrier_search_half_width_hz=matching_work.strategy.backend==modem::ReceiverBackend::amplitude_energy &&
        matching_work.strategy.full_frequency_coverage?matching_work.strategy.amplitude_frequency_half_width_hz:geometry.half_width_hz;
    result.requested_carrier_search_half_width_hz=geometry.requested_half_width_hz;
    result.clock_search_half_width_ppm=matching_work.strategy.backend==modem::ReceiverBackend::amplitude_energy &&
        matching_work.strategy.full_clock_coverage?matching_work.strategy.amplitude_clock_half_width_ppm:bank.clock_ppm;
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
    const bool scheduled_complex=matching_work.strategy.scheduled&&
        matching_work.strategy.backend==modem::ReceiverBackend::existing_complex;
    const bool packed_complex=scheduled_complex&&modem::packed_scheduled_symbols(receiver_config);
    if(scheduled_complex) {
        result.receiver_work_assumptions=packed_complex?"Packed calendar complex engineering reference: exact keyed singleton phases and "
            "admitted frequency/clock pairs retained; timing-position counts are engineering allowances, not executed counters. "
            "Full physical input and complete-cohort FFT work remain charged without a nominal duty discount. "
            "Preparation, rolling admissions, component split, arithmetic crossover and complete live-bank CPU are uncalibrated":
            "Split mapped complex engineering reference: actual refined physical-aperture frequency/clock alternatives; original private coefficients, eligible bounded source-curve/pulse-moment reuse and real I/Q Gram statistics. "
            "No source-time whole-symbol FFT is priced. Full physical capture remains charged; active/null overlap contraction uses the selected strategy allowance. "
            "Constructors, rolling admissions, DSSS outer-pulse guards, cache exhaustion and complete execution remain uncalibrated outside the documented local scope; no new shared-family or precision speedup is assumed";
        if(matching_work.strategy.scheduled_implicit_acquisition)
            result.receiver_work_assumptions="Implicit calendar-aware shared-family complex layout: actual Live frontend/control reservations and bounded shared-capture owner layout; native tile arithmetic follows Search arithmetic with original-template refinement. Required physical frequency/clock/timing families remain admitted. CPU work is unavailable until complete execution is calibrated; no duty-cycle discount or probability is manufactured. Actual allocation, keyed consumer retention and runtime owner binding remain mandatory";
        if(matching_work.timing_window_modeled)result.receiver_work_assumptions+=
            "; selected GPS/audio arrival prior modeled; full-window fallback reported separately";
        else result.receiver_work_assumptions+="; full arrival-window fallback retained";
    } else if(work_mode==ReceiverWorkMode::hardware_timing_model||work_mode==ReceiverWorkMode::hardware_calendar_model) {
        result.receiver_work_assumptions=matching_work.timing_window_modeled&&!matching_work.compact?
            (matching_work.geometry.restricted_fft_modeled?
            "Restricted FFT acquisition work allowance: original per-phase start cells and empty template jobs are omitted under selected GPS/audio bounds; input FFTs per guarded component, repeated private template dispatches, continuation and original logical-hop trial/threshold charges retained. Initial epochs use a one-second anchor envelope; uncertain partition/selector geometry uses full-job component-count allowances. Fresh cohorts use bounded acquisition-lifetime allowances. Paired direct and partitioned convolution use shared geometry selectors; uncertain anchors retain full-cohort fallback or a translation-independent eligible-option allowance. Borrowed-row initialization, repeated group input caches and component observation preparation are included; native FFT pipeline/plan and the measured V2 template family use bounded local calibration, while other operation coefficients and rolling admission allowances remain engineering assumptions. Full-window fallback is reported separately":
            "FFT acquisition work allowance: whole excluded batches skipped under selected GPS/audio bounds; mixed batches, frontend, continuation and original threshold charges retained. Full-window fallback is reported separately"):matching_work.timing_window_modeled?
            "Compact bank engineering reference: exact lattice count at a representative anchor under selected GPS/audio bounds and supplied capture metadata (nominal slope and full bank rate allowance when omitted). Full-window fallback is reported separately":
            "Full arrival-window bank; timing prior is unavailable for this backend or model geometry. Configured peer UTC correction lanes are retained";
    }
    if(shared_hop_calendar(config)&&work_mode!=ReceiverWorkMode::sampled_simulation) {
        if(work_mode==ReceiverWorkMode::hardware_calendar_model)
            result.receiver_work_assumptions+="; assumed UTC hop-calendar prior, not measured capture or peer TX qualification";
        else if(work_mode==ReceiverWorkMode::hardware_timing_model&&matching_work.timing_window_modeled)
            result.receiver_work_assumptions+="; dated capture calendar prior includes supplied timestamp/rate uncertainty; peer TX timing remains assumed";
        else result.receiver_work_assumptions+="; capture dating unavailable or geometry unsupported: full-window fallback";
    }
    if(!matching_work.compact&&!scheduled_complex) {
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
        result.receiver_work_assumptions+=scheduled_complex&&!packed_complex?
            "Interleave mapped contraction retains the independent complete-symbol permutation and rotations; private construction and cache exhaustion remain uncalibrated":matching_work.compact?
            "V2 compact permutation rebuild work is unsupported; CPU feasibility unavailable":
            "V2 FFT construction uses the bounded local shaped-template/cache calibration when eligible; other V2 geometries retain an uncalibrated permutation-draw allowance";
    }
    if(matching_work.outer_presence) {
        if(!result.receiver_work_assumptions.empty())result.receiver_work_assumptions+="; ";
        result.receiver_work_assumptions+="Includes a conservative coherent outer-code candidate-check allowance; actual retained peaks and throughput are not calibrated";
    }
    result.oscillator_search_limited=bank.limited&&
        !(matching_work.strategy.full_frequency_coverage&&matching_work.strategy.full_clock_coverage);
    if(oscillator_policy&&bank.minimum_rate!=bank.maximum_rate) {
        result.probability_search_approximation=true;
        result.probability_model_limit="Paired clock candidates use the selected timing coherence and full bank trial penalty; joint timing-path covariance is not simulated";
    }
    result.carrier_in_search=within_representation_bound(frequency,result.carrier_search_half_width_hz);
    result.clock_in_search=matching_work.strategy.backend==modem::ReceiverBackend::amplitude_energy?
        within_representation_bound(channel.clock_error_ppm,matching_work.strategy.amplitude_clock_half_width_ppm):
        (!oscillator_policy||paired_clock_coverage(bank,frequency,channel.clock_error_ppm));
    if(matching_work.strategy.backend==modem::ReceiverBackend::amplitude_energy&&!scheduled_complex) {
        result.probability_reference_only=true;
        result.probability_model_limit="Amplitude-only full-bank RX sensitivity is unqualified";
        result.modeled_symbol_snr_db=static_cast<double>(symbol_db);
        result.coherent_reference_only=false;
        if(!compute_probability||!matching_work.strategy.supported||!result.profile_matches||
           !result.carrier_in_search||!result.clock_in_search||!result.receiver_workspace_supported||
           result.oscillator_search_limited)return result;
        if(work_mode==ReceiverWorkMode::sampled_simulation||options.key) {
            result.probability_model_limit="Amplitude hardware mean-power reference does not cover nominal-power simulation or keyed transmit sources";
            return result;
        }
        if(receiver_config.oscillator_search!=config.oscillator_search||
           receiver_config.frequency_search_multiplier!=config.frequency_search_multiplier||
           receiver_config.stream_phase_samples!=config.stream_phase_samples) {
            result.probability_model_limit="Amplitude reference does not cover a different receiver oscillator or phase domain";return result;
        }
        modem::PatternSearch amplitude_search;
        amplitude_search.start_offset_seconds=0;
        amplitude_search.start_uncertainty_seconds=options.search_seconds+1;
        live::Settings amplitude_settings;amplitude_settings.transfer=options;amplitude_settings.simulation=false;
        live::detail::prepare_public_amplitude_windows(config,amplitude_settings,false,amplitude_search,keys,profiles.size());
        if(amplitude_search.amplitude_start_uncertainty_seconds)
            amplitude_search.start_uncertainty_seconds=*amplitude_search.amplitude_start_uncertainty_seconds;
        const auto amplitude_geometry=modem::pattern_amplitude_geometry(config,amplitude_search);
        if(amplitude_geometry.channel_count!=matching_work.strategy.amplitude_channels) {
            result.probability_model_limit="Amplitude reference does not cover workspace-reduced channel geometry";return result;
        }
        const auto amplitude=detail::amplitude_probability(config,amplitude_search,static_cast<double>(symbol_db),
            static_cast<double>(frequency),std::max<std::size_t>(256,probability_trials),stop);
        result.probability_model_limit=amplitude.limit;
        if(profiles.size()>1||keys>1)
            result.probability_model_limit+=" Conditional matching profile only; the configured multi-profile/key union is not qualified.";
        if(!amplitude.available)return result;
        result.one_bit_reference_available=true;
        result.one_bit_success_probability=amplitude.correct;
        result.probability_trials=amplitude.trials;
        result.probability_search_approximation=true;
        // Sampling intervals would omit the explicitly unqualified source and
        // normal-tail errors. Do not display them as RX confidence intervals.
        if(raw_bits&&transmission.wire_bits==1) {
            result.reference_probability_available=true;
            result.success_probability=amplitude.correct;
        }
        return result;
    }
    const bool frequency_axis=matching_work.strategy.backend==modem::ReceiverBackend::frequency_axis;
    const bool eme_reference_geometry=!matching_work.compact&&config.pattern_symbols&&
        config.spreading_mode==modem::SpreadingMode::pattern&&!config.scramble&&!config.dsss&&
        config.dsss_factor==1&&!config.fake_hopping&&!config.scheduled_hopping&&!config.data_key&&!options.key&&
        !arrival_policy(options,config)&&
        config.frequency_search_multiplier==1&&config.stream_phase_samples==0&&
        receiver_config.oscillator_search==config.oscillator_search&&
        receiver_config.frequency_search_multiplier==config.frequency_search_multiplier&&receiver_config.stream_phase_samples==0&&
        config.sample_rate==6000&&config.carrier_hz==1500&&config.bandwidth_hz==1&&
        modem::pattern_chip_samples(config)==12000&&modem::pattern_pulse_enabled(config)&&
        (samples_per_symbol==768000||samples_per_symbol==1536000)&&options.search_seconds==6&&
        config.oscillator_search&&config.oscillator_search->lf==modem::OscillatorModel{.0001,.005}&&
        config.oscillator_search->rf==modem::OscillatorModel{.0001,.005}&&
        config.oscillator_search->rf_shift_hz==5.8e9&&config.oscillator_search->margin==3&&
        config.oscillator_search->reference==modem::OscillatorReference::independent_audio&&
        config.oscillator_search->sideband==modem::OscillatorSideband::upper&&
        ((channel.clock_error_ppm==0&&channel.frequency_offset_hz==0)||
         (channel.clock_error_ppm==modem::oscillator_effects(config).clock_error_ppm&&
          channel.frequency_offset_hz==modem::oscillator_effects(config).frequency_offset_hz))&&
        channel.delay_samples<=6*config.sample_rate&&
        channel.phase_noise_degrees_per_sqrt_second<=modem::oscillator_effects(config).phase_noise_degrees_per_sqrt_second;
    const bool axis_source_model=frequency_axis&&eme_reference_geometry;
    const bool existing_eme_reference=matching_work.strategy.backend==modem::ReceiverBackend::existing_complex&&eme_reference_geometry;
    if((matching_work.strategy.backend!=modem::ReceiverBackend::existing_complex&&!axis_source_model)||scheduled_complex) {
        result.receiver_work_assumptions+="; "+matching_work.geometry.scope+"; "+matching_work.strategy.reason;
        // New strategies do not inherit finite-window coherent probability
        // references. Full amplitude/frequency-axis acquisition is unqualified.
        result.probability_model_limit=scheduled_complex?
            (packed_complex?"Packed scheduled complex full-bank RX sensitivity is unqualified; ordinary contiguous-symbol probabilities are not applicable":
                "Split mapped complex full-bank RX sensitivity is unqualified; contiguous-symbol FFT probabilities are not applicable"):

            matching_work.strategy.backend==modem::ReceiverBackend::amplitude_energy?
            "Amplitude-only full-bank RX sensitivity is unqualified; coherent probabilities are not applicable":
            "Frequency-axis full-bank RX sensitivity is unqualified; coherent finite-bank probabilities are not borrowed";
        return result;
    }
    if(!matching_work.strategy.supported) {
        result.probability_model_limit=matching_work.strategy.reason;
        return result;
    }
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
    if(config.spreading_mode==modem::SpreadingMode::pattern&&modem::pattern_chips_per_symbol(config)<=1) {
        result.probability_model_limit="One-chip pattern alternatives are indistinguishable under unknown complex gain";
        return result;
    }
    // Actual-source Interleave statistics include the configured pulse gaps
    // and the deferred payload origin. Other references remain diagnostic.
    // A conditional symbol fit cannot qualify adaptive acquisition or the
    // hop-dependent emitted-power distribution.
    if(config.fake_hopping&&config.fake_hopping->power_ramps) {
        result.probability_reference_only=true;
        result.probability_model_limit=modem::deferred_symbols(config)?
            "Full-symbol reference: deferred FHSS acquisition and cover transitions remain unqualified":
            config.fake_hopping->smooth_ramps?
            "FHSS smooth envelope: full-energy fading projection; full-bank sensitivity remains unqualified":
            config.fake_hopping->matched_gaps?
            "FHSS matched-gap templates: hop-dependent emitted energy and full acquisition sensitivity remain unqualified":
            "Ungated diagnostic reference: FHSS power gaps and transition loss are not modeled";
    }
    if(axis_source_model||existing_eme_reference)result.probability_reference_only=true;
    const bool interleaved_reference=(config.dsss_factor>1&&config.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2)||
        (config.fake_hopping&&config.fake_hopping->smooth_ramps);
    if(interleaved_reference&&(matching_work.compact||differential_window||
       (result.drift_sections>1&&!matching_work.drift_supported))) {
        result.probability_model_limit="Interleave actual-source reference is unavailable for compact, unsupported section allocation or differential geometry; full acquisition sensitivity remains unqualified";
        return result;
    }
    const auto probability_result=[&] {
        if(result.probability_reference_only) {
            result.reference_probability_available=result.confidence_available;
            result.one_bit_reference_available=result.one_bit_confidence_available;
            result.confidence_available=false;result.one_bit_confidence_available=false;
            result.probability_interval_available=false;
            std::string scope="Conditional matched-template AWGN reference";
            if(arrival_policy(options,config))scope+="; UTC device timing and steering failures are not modeled";
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
    if(matching_work.drift_supported||interleaved_reference) {
        detail::ReceiverProbabilityParameters parameters;
        parameters.sections=matching_work.drift_supported;
        // Coherent planner curves historically request fewer draws because the
        // legacy branch was analytic. This actual-source reference uses the
        // joint statistic engine and must honor its minimum trial budget.
        parameters.requested_trials=(interleaved_reference||axis_source_model)?std::max<std::size_t>(256,probability_trials):probability_trials;
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
        if(axis_source_model||existing_eme_reference) {
            // The payload-aligned EME peak lies in the second logical hop.
            // Charge all its starts, including unselected cells. Earlier
            // partial-window admissions need another timing/ownership model
            // outside the qualified signal levels. Public rows use one phase
            // and one initial symbol. No receiver threshold is changed.
            const auto initial=std::floor(static_cast<long double>(config.sample_rate)/matching_work.bin_samples);
            const auto hop=std::floor(static_cast<long double>(samples_per_symbol)/matching_work.bin_samples/2);
            const auto charged=(initial+hop)*frequencies;
            parameters.acquisition_threshold=static_cast<double>(-std::log(1e-10L)+
                2*std::log(charged+1)+std::log(2*frequencies));
        }
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
            if(interleaved_reference||axis_source_model)return shaped_source_statistics(options,matching_work,p,source_bit,
                work_mode,result.probability_model_limit,stop,axis_source_model?1536000:524288);
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
                if(!differential_window && !modem::pattern_pulse_enabled(config)&&
                   work_mode==ReceiverWorkMode::sampled_simulation)
                    p.signal_energy*=weight/static_cast<double>(samples_per_symbol);
            }
            if(!differential_window)return true;
            // Report the actual bounded-work rejection, including both limits
            // when present. These are probability-model limits, not receiver
            // search limits or evidence that reception is impossible.
            const auto step=matching_work.compact?
                (modem::pattern_pulse_enabled(config)?std::max<std::uint64_t>(1,chip/16):chip):
                matching_work.bin_samples;
            const auto quadrature=modem::pattern_pulse_enabled(config)&&!matching_work.compact?
                std::min<std::uint64_t>(step,4):1;
            const auto windows=samples_per_symbol/differential_window;
            const auto blocks=step?samples_per_symbol/step:0;
            std::string budget_limit;
            if(windows>maximum_probability_windows)
                budget_limit=std::to_string(windows)+" local windows exceed "+std::to_string(maximum_probability_windows);
            if(step&&blocks>maximum_probability_observations/quadrature) {
                if(!budget_limit.empty())budget_limit+="; ";
                budget_limit+=std::to_string(blocks*quadrature)+" template integration blocks exceed "+
                    std::to_string(maximum_probability_observations);
            }
            if(!budget_limit.empty()) {
                result.probability_model_limit="Probability model work limit: "+budget_limit;
                return false;
            }
            // A failed circular fit may have partially normalized its source
            // power and covariance. Retry from pristine parameters so the
            // real-covariance fallback applies that normalization exactly once.
            auto candidate=p;
            if(differential_statistics(options,matching_work,differential_window,candidate,source_bit,work_mode,stop)) {
                p=std::move(candidate);return true;
            }
            if(!matching_work.compact)return false;
            candidate=p;candidate.real_atoms.clear();
            if(!real_differential_statistics(options,differential_window,candidate,source_bit,work_mode,stop))return false;
            p=std::move(candidate);return true;
        };
        auto alternative_parameters=parameters;
        if(!configure_statistics(parameters,0)) {
            result.confidence_available=false;
            if(result.probability_model_limit.empty())result.probability_model_limit="Template geometry exceeds the bounded probability model";
            return probability_result();
        }
        auto probability=detail::ReceiverProbability{};
        if(axis_source_model) {
            detail::EmeProbabilitySettings settings;settings.trials=parameters.requested_trials;
            settings.frequency_offset_hz=channel.frequency_offset_hz;settings.clock_error_ppm=channel.clock_error_ppm;
            const auto joint=detail::eme_receiver_probability(options,work_mode,static_cast<double>(symbol_db),
                channel.phase_noise_degrees_per_sqrt_second,settings,stop);
            if(std::abs(joint.full_bank_threshold-parameters.acquisition_threshold)>1e-10)
                throw Error("EME probability trial charge changed");
            probability=joint.probability;
        } else probability=detail::receiver_probability(parameters,stop);
        bool averaged_candidates=false;
        // The planner has no wire-bit content. Independent finite candidates
        // need an equiprobable-bit estimate, including each source's limiter
        // projections. Common random draws are paired, so retain the smaller
        // trial count rather than counting correlated draws as extra evidence.
        if(probability.available && !modem::legacy_public_pattern(config) &&
           config.spreading_mode==modem::SpreadingMode::pattern &&
           (samples_per_symbol/chip<=1024 || differential_window || interleaved_reference)) {
            if(!configure_statistics(alternative_parameters,1)) {
                result.confidence_available=false;
                if(result.probability_model_limit.empty())
                    result.probability_model_limit="Alternative template geometry exceeds the bounded probability model";
                return probability_result();
            }
            const auto other=axis_source_model?probability:detail::receiver_probability(alternative_parameters,stop);
            if(!other.available)probability=other;
            else {
                averaged_candidates=true;
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
        result.drift_model_available=matching_work.drift_supported;result.coherent_reference_only=false;
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
            if(p.section_signal_coefficients) {
                std::complex<double> fitted{};
                for(std::size_t j=0;j<4;++j)fitted+=std::sqrt(p.weights[j])*(*p.section_signal_coefficients)[j][0];
                fitted_fraction=std::norm(fitted);
            }
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
        const auto modeled_energy=joint*(averaged_candidates?
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
        if(interleaved_reference)result.probability_model_limit=
            "Private isolated-symbol, fixed initial-phase actual-source reference: shaped and radially limited real PCM, selected source FHSS envelope, projected signal energy and finite candidate overlap. Noncircular projection uses both real-noise principal planes, joint template/source covariance and dependent received energy; circular geometry retains its existing approximation. Orthogonal noise energy uses a bounded chi-square approximation. Hardware uses user-entered actual mean radio power; simulation uses nominal-power-referenced noise. Quarter phase/timing changes remain approximate: time-varying limiter weights, opposite carrier-image rotation and unmatched signal/noise cross terms are unresolved. Adjacent unknown symbols, final device resampling/envelope, complete acquisition-bank behavior, outer presence guard and sensitivity qualification are not modeled";
        if(existing_eme_reference) {
            result.probability_model_limit="Existing-only EME first-bit conditional reference; second-batch trial charge corrected; shaped-source and joint peak/timing selection remain approximate; receiver completion is unqualified; no frequency-axis qualification";
            if(!raw_bits||transmission.wire_bits>1) {
                result.confidence_available=false;result.probability_interval_available=false;
                result.success_probability=result.coherent_success_probability=0;
                result.success_probability_low=0;result.success_probability_high=1;
                result.probability_model_limit+="; multi-bit timing ownership and continuation are unqualified";
            }
        }
        if(axis_source_model) {
            const auto effects=modem::oscillator_effects(config);
            const auto at_level=[&](double db){return std::abs(symbol_db-db)<=1e-9L;};
            const bool tested_level=at_level(17)||at_level(18)||at_level(19)||
                (samples_per_symbol==768000&&at_level(1.0926996097583093));
            // Seven fixed-point, exact-one-bit conditional AWGN agreement
            // checks. Neither interpolation nor another physical channel is
            // qualified by this evidence. Fewer draws retain a reference.
            const bool tested=raw_bits&&transmission.wire_bits==1&&tested_level&&probability.trials==4096&&
                profiles.size()==1&&keys==1&&(!actual_settings||actual_settings->receive_keys.empty())&&
                config.search_arithmetic==modem::SearchArithmetic::default_mode&&
                receiver_config.search_arithmetic==modem::SearchArithmetic::default_mode&&
                work_mode==ReceiverWorkMode::hardware_fallback&&
                channel.clock_error_ppm==effects.clock_error_ppm&&
                channel.frequency_offset_hz==effects.frequency_offset_hz&&
                channel.phase_noise_degrees_per_sqrt_second==effects.phase_noise_degrees_per_sqrt_second;
            result.probability_reference_only=!tested;
            result.probability_model_limit=tested?
                "EME first-bit AWGN: tested signal level; accuracy within 15 percentage points at 95% simultaneous confidence for the preset bit/arrival ensemble. Interpolation, radio hardware and continuation are unqualified":
                "EME first-bit acquisition AWGN reference outside tested conditions; receiver completion, radio hardware and continuation are unqualified";
            if(profiles.size()>1||keys>1||(actual_settings&&!actual_settings->receive_keys.empty()))
                result.probability_model_limit+="; conditional matching bank only: other profiles/keys, rolling epochs and arbitration are not modeled";
            if(!raw_bits||transmission.wire_bits>1) {
                result.confidence_available=false;result.probability_interval_available=false;
                result.success_probability=result.coherent_success_probability=0;
                result.success_probability_low=0;result.success_probability_high=1;
                result.probability_model_limit+="; multi-bit timing ownership and continuation are unqualified";
            }
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
Estimate estimate(const transfer::Estimate& transmission,const transfer::Options& options,bool raw_bits,
        const modem::ChannelConfig& channel,std::span<const modem::Config> profiles,std::size_t keys,
        bool compute_probability,double window,std::size_t trials,ReceiverWorkMode mode,
        ReceiverTimingModel timing,std::stop_token stop) {
    return estimate_impl(transmission,options,raw_bits,channel,profiles,keys,compute_probability,window,trials,mode,timing,nullptr,stop);
}
Estimate estimate_with_receiver_settings(const transfer::Estimate& transmission,const transfer::Options& options,bool raw_bits,
        const modem::ChannelConfig& channel,const live::Settings& settings,std::span<const modem::Config> profiles,std::size_t keys,
        bool compute_probability,double window,std::size_t trials,ReceiverWorkMode mode,
        ReceiverTimingModel timing,std::stop_token stop) {
    return estimate_impl(transmission,options,raw_bits,channel,profiles,keys,compute_probability,window,trials,mode,timing,&settings,stop);
}
}
