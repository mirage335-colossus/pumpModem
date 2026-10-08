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
#include "datapump/correlation_experiment.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

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
// Pulse-cell fitting separates sample ingestion from private-chip work. The
// cache allowance deliberately charges a full rebuild on every fractional-rate
// cell; the runtime often reuses or incrementally updates its quadratic table.
constexpr long double pulse_frontend_operations_per_sample = 220;
constexpr long double pulse_pair_operations_per_chip = 6000;
constexpr long double pulse_gram_operations_per_cell = 4000;
constexpr long double pulse_kernel_operations_per_sample = 16000;
constexpr long double pulse_moment_operations_per_block = 48;
constexpr long double pulse_moment_operations_per_segment = 256;
constexpr long double pulse_moment_kernel_operations_per_segment = 22000;
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
    return a.sample_rate==b.sample_rate && a.carrier_hz==b.carrier_hz && a.bandwidth_hz==b.bandwidth_hz &&
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
SearchBank search_bank(const modem::Config& config) {
    SearchBank result;
    if(config.oscillator_search) {
        const auto plan=modem::oscillator_pattern_search(config);
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
    long double serial=0,parallel=0,tracking_serial=0,tracking_windows=0,search_trials=1,kernel_serial=0,search_serial=0;
    double noise_dimensions=0,coherent_dimensions=0,section_dimensions=0,noise_condition=1;
    double timing_uncertainty_chips=0,acquisition_threshold=0;
    double projection_bin_chips=0;
    double following_search_ratio=0;
    bool drift_supported=false;
    bool differential_supported=false;
    bool compact=false,pulse_projected=false,kernel_upper_bound=false;
    std::uint64_t bin_samples=1;
    bool workspace_supported=true;
};
Work receiver_work(const modem::Config& config,const SearchBank& bank,long double samples,
                   const transfer::Options& options,std::size_t profiles,std::size_t keys,
                   std::size_t established_stream_bits,double local_window_seconds) {
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
    const auto template_pair_work=template_pair_operations_per_bin+private_pair_generation;
    const auto epochs=private_pattern?2.L*options.search_seconds+1+
        (options.timestamp?0:std::ceil((static_cast<long double>(modem::training_sample_count(config))+
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
    // Expanded coupled banks require the FFT path. A compact private hint or
    // insufficient memory cannot silently substitute a different search path.
    const bool correlator=!coupled &&
        ((private_pattern && symbol>=60.L*config.sample_rate) || fft_core_bytes>allowance);
    // Live banks stream expanded template rows when several profiles, keys or
    // epochs share the budget, so early banks cannot consume it with caches.
    const bool streamed_templates=!correlator &&
        (drift_sections>1 || differential_window || fft_bytes>allowance || (scaled && banks*profiles>1));
    auto starts=std::ceil(2.L*(options.search_seconds+1.L)*config.sample_rate/
                          std::max(1.L,std::floor(chip/(2.L*(1+maximum_clock_ratio)))))+1;
    if(config.oscillator_search) {
        long double lanes=0;
        for(const auto& hypothesis:bank.hypotheses) {
            const auto rate=1+static_cast<long double>(hypothesis.clock_error_ppm)*1e-6L;
            lanes+=std::ceil(4.L*(options.search_seconds+1.L)*config.sample_rate*rate/chip)+1;
        }
        starts=lanes/frequencies;
    }
    const auto phase_groups=private_pattern&&symbol%config.sample_rate!=0?(symbol>=config.sample_rate?2.L:3.L):1.L;
    Work result;
    result.compact=correlator;result.bin_samples=bin;
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
    const auto compact_required=256*1024.L+
        lanes*(512+(phase_groups-1)*sizeof(std::array<modem::detail::CorrelationFit,2>)+2+chain_state)+
        projection_banks*(32+(block_samples+1)*sizeof(modem::detail::CorrelationProjection))+
        candidate_count*sizeof(modem::PatternEvidence)+points*sizeof(std::complex<double>)+
        frequencies*sizeof(modem::PatternFrequencyRateHypothesis);
    result.workspace_supported=correlator?
        (!config.oscillator_search||compact_required<=allowance):fft_core_bytes<=allowance;
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
    const auto acquisition_trials=(correlator?starts:initial_batch)*frequencies*phase_groups*initial_symbols;
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
    // The compact receiver mixes each unique real carrier bank. Clock lanes
    // sharing that carrier reuse its prefix; FFT receives one baseband stream.
    // Cross-key/epoch Live cache hits depend on per-push spare workspace and
    // origin identity, so this work model conservatively charges cache misses.
    result.serial=samples*projection_operations_per_sample*(correlator?projection_banks:1)*banks;
    // Admission thresholds belong to one receiver; unrelated keys and
    // waveform profiles add compute work, not evidence against this signal.
    result.search_trials=std::max(1.L,starts*frequencies*phase_groups);
    if(established_stream_bits)
        result.tracking_windows=static_cast<long double>(established_stream_bits-1)+
            static_cast<long double>(modem::pattern_absence_samples(config))/symbol;
    if(correlator) {
        if(result.pulse_projected) {
            long double lattice_cells=0,kernel_samples=0;
            for(const auto& hypothesis:bank.hypotheses) {
                const auto rate=1+static_cast<long double>(hypothesis.clock_error_ppm)*1e-6L;
                lattice_cells+=3*std::ceil(samples*rate/chip);
                if(pulse_moments) {
                    // Each finite table cell has at most 256 affine pieces,
                    // plus sampled endpoint singletons. Fractional clocks may
                    // rebuild every cell, but no rebuild scans the PCM chip.
                    const auto pieces=260.L;
                    kernel_samples+=hypothesis.clock_error_ppm==0?9*pieces:
                        3*std::ceil(samples*rate/chip)*pieces;
                    if(hypothesis.clock_error_ppm!=0)result.kernel_upper_bound=true;
                } else if(hypothesis.clock_error_ppm==0)kernel_samples+=9.L*chip;
                else {kernel_samples+=3*samples;result.kernel_upper_bound=true;}
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
            } else {
                result.serial+=(3*samples*frequencies*pulse_frontend_operations_per_sample+
                    lattice_cells*pulse_gram_operations_per_cell)*banks;
                result.kernel_serial=kernel_samples*pulse_kernel_operations_per_sample*banks;
            }
            result.serial+=result.kernel_serial;
            // The projected backend consumes shared cells and commits lane
            // work in order on the caller; it does not use search workers.
            result.search_serial=std::ceil(samples*bank.maximum_rate/chip)*lanes*phase_groups*
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
            result.parallel=(observations*lanes*phase_groups*operations_per_pair*
                (1+(result.drift_supported?1:0)+(result.differential_supported?1:0))+
                (result.differential_supported?samples/symbol*differential_windows*lanes*phase_groups*
                    differential_operations_per_window:0))*banks;
        }
    } else {
        auto blocks=std::ceil(samples/(bin*hop));
        auto scored_starts=blocks*hop;
        if(bounded_acquisition) {
            // Live input scores only fully observed windows. The first small
            // batch follows one complete symbol; later batches use the fixed
            // bounded hop without an EOF-triggered partial transform.
            const auto observed_bins=std::floor(samples/bin);
            const auto first_batch_end=length+initial_batch-1;
            blocks=observed_bins<first_batch_end?0:1+std::floor((observed_bins-first_batch_end)/hop);
            scored_starts=blocks>0?initial_batch+(blocks-1)*hop:0;
        }
        const auto jobs=frequencies*phase_groups*(private_pattern?4:1);
        // Five real operations per complex FFT element per stage. Streamed
        // public templates need the same additional forward transform and
        // generation allowance as regenerated private templates.
        const bool generate_templates=private_pattern || streamed_templates;
        if(drift_sections>1) {
            // Section transforms reuse one full-size work buffer. Their sum
            // also supplies the coherent dot; there is no fifth template FFT.
            // The input spectrum is still computed once per acquisition hop.
            // Tiny batches directly match their <=4 complete start windows.
            const auto direct_blocks=blocks>0?(hop<=4?blocks:(initial_batch<=4?1.L:0.L)):0.L;
            const auto direct_starts=hop<=4?scored_starts:(direct_blocks>0?initial_batch:0.L);
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
        }
        if(differential_window) {
            const auto direct_limit=std::max(4.L,static_cast<long double>(differential_windows)*fft_log);
            const auto direct_blocks=blocks>0?(hop<=direct_limit?blocks:(initial_batch<=direct_limit?1.L:0.L)):0.L;
            const auto direct_starts=hop<=direct_limit?scored_starts:(direct_blocks>0?initial_batch:0.L);
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
            result.tracking_serial=result.tracking_windows*(length*(
                generated_pairs*template_pair_work+fits*fit_operations)+
                fits*tracking_evidence_operations_per_fit*(drift_sections>1?drift_sections+1:1));
            if(differential_window)result.tracking_serial+=result.tracking_windows*fits*
                (length*fit_operations+differential_windows*differential_operations_per_window);
        }
    }
    return result;
}

// Build the same nominal local template statistics used by FFT matching.
// This work is bounded by projected observations, never represented PCM time.
// Compact receivers use a bounded per-chip quadrature for shaped templates.
// Unsupported cases keep their timing/resource diagnostics without inventing
// a reception percentage from a different detector.
bool differential_statistics(const transfer::Options& options,const Work& work,
                             std::uint64_t window,detail::ReceiverProbabilityParameters& p,unsigned source_bit=0) {
    const auto& config=options.modem;
    const auto total=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto windows=total/window;
    if(windows>4096 || total%window || windows%4)return false;
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
                  bool compute_probability,double local_window_seconds,std::size_t probability_trials) {
    modem::validate(options.modem);modem::validate_channel(options.modem,channel);
    if(!std::isfinite(transmission.total_seconds) || transmission.total_seconds<0 || !keys ||
       !std::isfinite(local_window_seconds) || local_window_seconds<0)
        throw Error("invalid simulation estimate input");
    if(profiles.empty())profiles=std::span(&options.modem,1);
    Estimate result;result.receiver_profiles=profiles.size();
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
    long double receiver_frontend=0,kernel_serial=0,search_serial=0;
    bool any_pulse_projected=false,any_kernel_upper_bound=false;
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
        banks.push_back({&profile,search_bank(profile)});
        return banks.size()-1;
    };
    std::size_t matching_bank=0;
    bool matching_supported=false;
    const auto frequency=channel.frequency_offset_hz+static_cast<long double>(config.carrier_hz)*channel.clock_error_ppm*1e-6L;
    for(const auto& profile:profiles) {
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
                          matches?transmission.wire_bits:0,local_window_seconds);
        if(existing==work_entries.end())work_entries.push_back({index,training_samples,work});
        serial+=work.serial;parallel+=work.parallel;
        receiver_frontend+=work.serial-work.kernel_serial-work.search_serial;kernel_serial+=work.kernel_serial;
        search_serial+=work.search_serial;
        any_pulse_projected|=work.pulse_projected;any_kernel_upper_bound|=work.kernel_upper_bound;
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
    result.receiver_frontend_seconds=finite_seconds(receiver_frontend/serial_operations_per_second);
    result.receiver_search_seconds=finite_seconds(search_serial/serial_operations_per_second+parallel/cpu_scoring_operations_per_second);
    result.receiver_kernel_rebuild_seconds=finite_seconds(kernel_serial/serial_operations_per_second);
    result.pulse_projection_modeled=any_pulse_projected;
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
    result.frequency_rate_hypotheses=bank.hypotheses.size();
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
    if(differential_window&&!matching_work.differential_supported) {
        result.probability_model_limit="Local detector allocation is outside the modeled workspace allowance";
        return result;
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
            return !differential_window || differential_statistics(options,matching_work,differential_window,p,source_bit);
        };
        auto alternative_parameters=parameters;
        if(!configure_statistics(parameters,0)) {
            result.confidence_available=false;
            result.probability_model_limit="Template geometry exceeds the bounded probability model";
            return result;
        }
        auto probability=detail::receiver_probability(parameters);
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
                return result;
            }
            const auto other=detail::receiver_probability(alternative_parameters);
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
            return result;
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
        return result;
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
    return result;
}
}
