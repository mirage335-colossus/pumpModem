#include "datapump/simulation_estimate.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "pattern_drift.hpp"
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
constexpr long double model_implementation_loss_db = 3;

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
        a.pulse_shaping==b.pulse_shaping && modem::symbol_sample_count(a)==modem::symbol_sample_count(b) &&
        modem::pattern_chip_samples(a)==modem::pattern_chip_samples(b);
}
long double phase_coherence(long double x) {
    if(x<1e-4L)return 1-x/3+x*x/12;
    return 2*(1+(std::expm1(-x)/x))/x;
}
struct Work {
    long double serial=0,parallel=0,tracking_serial=0,tracking_windows=0,search_trials=1;
    double noise_dimensions=0,coherent_dimensions=0,section_dimensions=0,noise_condition=1;
    double timing_uncertainty_chips=0,acquisition_threshold=0;
    double projection_bin_chips=0;
    double following_search_ratio=0;
    bool drift_supported=false;
    bool workspace_supported=true;
};
Work receiver_work(const modem::Config& config,long double samples,
                   const transfer::Options& options,std::size_t profiles,std::size_t keys,
                   std::size_t established_stream_bits) {
    const auto symbol=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto drift_sections=modem::detail::drift_section_count(config);
    const bool private_pattern=config.scramble || config.dsss;
    const auto epochs=private_pattern?2.L*options.search_seconds+1+
        (options.timestamp?0:std::ceil((static_cast<long double>(modem::training_sample_count(config))+
            modem::pattern_pulse_padding_samples(config))/config.sample_rate)):1.L;
    const auto banks=epochs*keys;
    const auto geometry=modem::default_pattern_frequency_search(config);
    const bool coupled=geometry.count>5 && config.spreading_mode==modem::SpreadingMode::pattern;
    const auto frequencies=static_cast<long double>(geometry.count)*(coupled?2:1);
    const auto maximum_clock_ratio=coupled?geometry.half_width_hz/config.carrier_hz:0.;
    auto bin=modem::pattern_projection_bin_samples(config,geometry.half_width_hz);
    const auto omega=2*std::numbers::pi*config.carrier_hz/config.sample_rate;
    const auto sine=std::sin(omega);
    auto image=std::abs(sine)>1e-12?std::abs(std::sin(static_cast<double>(bin)*omega)/sine):static_cast<double>(bin);
    if(symbol<=256 && (!private_pattern || image>1e-10*static_cast<double>(bin)))bin=1;
    image=std::abs(sine)>1e-12?std::abs(std::sin(static_cast<double>(bin)*omega)/sine):static_cast<double>(bin);
    const bool sample_fit=bin==1 && (symbol<=256 || !private_pattern);
    const auto observation_samples=static_cast<long double>(symbol)/(1-maximum_clock_ratio);
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
    const bool separate_tracking_reference=coupled || transform<2*length;
    // Retaining transformed template rows is optional. The streamed path
    // keeps FFT/ring/tracking scratch and empty row metadata, then generates
    // each template in existing per-job scratch without reducing coverage.
    const auto fft_core_bytes=transform*16*5+(4*length+2*hop)*16+
        (separate_tracking_reference?2*length*16:0)+(transform+1)*8+
        frequencies*(2*24+2*8+2*16+18*8+2*8)+
        4096*16+4096*sizeof(modem::PatternEvidence)+(drift_sections>1?48*hop:0);
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
        (drift_sections>1 || fft_bytes>allowance || (coupled && banks*profiles>1));
    const auto starts=std::ceil(2.L*(options.search_seconds+1.L)*config.sample_rate/
                               std::max(1.L,std::floor(chip/(2.L*(1+maximum_clock_ratio)))))+1;
    const auto phase_groups=private_pattern&&symbol%config.sample_rate!=0?(symbol>=config.sample_rate?2.L:3.L):1.L;
    Work result;
    result.workspace_supported=!coupled || fft_core_bytes<=allowance;
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
    const auto initial_symbols=private_pattern?4.L:1.L;
    const auto acquisition_trials=(correlator?starts:initial_batch)*frequencies*phase_groups*initial_symbols;
    result.acquisition_threshold=static_cast<double>(-std::log(1e-10L)+2*std::log(acquisition_trials+1)+
        std::log(2*frequencies*initial_symbols));
    result.following_search_ratio=correlator?0:static_cast<double>(nominal_length/initial_batch);
    // Compact layout is not allocated by the planner. Use a deliberately
    // generous per-lane upper allowance before crediting optional section
    // state; tighter compact budgets retain the coherent reference.
    const auto compact_bound=starts*frequencies*phase_groups*4096+2*1024*1024;
    result.drift_supported=drift_sections>1&&result.workspace_supported&&
        (!correlator||compact_bound<=allowance)&&result.noise_dimensions>=16;
    result.serial=samples*projection_operations_per_sample*banks;
    // Admission thresholds belong to one receiver; unrelated keys and
    // waveform profiles add compute work, not evidence against this signal.
    result.search_trials=std::max(1.L,starts*frequencies*phase_groups);
    if(correlator) {
        // Bounded streaming projections are reused by half-chip start lanes;
        // each lane still evaluates two candidate bit fits per observation.
        const auto observations=std::ceil(samples/std::min(32.L,static_cast<long double>(chip)));
        // Eligible lanes retain the coherent fit and one active section fit;
        // completed sections contribute only fixed-size summary statistics.
        result.parallel=observations*starts*frequencies*phase_groups*64*
            (drift_sections>1?2:1)*banks;
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
                    12*drift_sections*transform+template_pair_operations_per_bin*length)+
                direct_starts*jobs*length*(template_pair_operations_per_bin+fit_operations)+
                jobs*40*(drift_sections+1)*scored_starts)*banks;
        } else {
            result.parallel=(blocks*(5*transform*fft_log*(1+2*jobs*(generate_templates?2:1))+
                jobs*(12*transform+(generate_templates?template_pair_operations_per_bin*length:0)))+
                jobs*40*scored_starts)*banks;
        }
        if(established_stream_bits) {
            // Acquisition supplies the first bit. An established track then
            // scores each remaining bit and complete absent symbols covering
            // six seconds. This desired-stream allowance is independent of
            // unrelated key/epoch banks; extra competing/noise tracks and
            // reacquisition can add work, so it is not a runtime upper bound.
            result.tracking_windows=static_cast<long double>(established_stream_bits-1)+
                static_cast<long double>(modem::pattern_absence_samples(config))/symbol;
            // continue_tracks() refines five starts for each possible private
            // phase group, then compares the chosen start against every other
            // carrier/clock hypothesis. measure() reuses a template pair for
            // those five starts, regenerating it when frequency/phase changes.
            const auto fits=5*phase_groups+frequencies-1;
            const auto generated_pairs=phase_groups+frequencies-1;
            const auto fit_operations=sample_fit?tracking_real_pair_operations_per_bin:
                tracking_pair_operations_per_bin;
            result.tracking_serial=result.tracking_windows*(length*(
                generated_pairs*template_pair_operations_per_bin+fits*fit_operations)+
                fits*tracking_evidence_operations_per_fit*(drift_sections>1?drift_sections+1:1));
        }
    }
    return result;
}
}

Estimate estimate(const transfer::Estimate& transmission,const transfer::Options& options,bool raw_bits,
                  const modem::ChannelConfig& channel,std::span<const modem::Config> profiles,std::size_t keys,
                  bool compute_probability) {
    modem::validate(options.modem);modem::validate_channel(options.modem,channel);
    if(!std::isfinite(transmission.total_seconds) || transmission.total_seconds<0 || !keys)
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
    Work matching_work;
    for(const auto& profile:profiles) {
        modem::validate(profile);
        const auto matches=same_profile(config,profile);
        result.profile_matches|=matches;
        const auto work=receiver_work(profile,media*profile.sample_rate,options,profiles.size(),keys,
                                      matches?transmission.wire_bits:0);
        serial+=work.serial;parallel+=work.parallel;
        tracking_serial+=work.tracking_serial;tracking_windows+=work.tracking_windows;
        if(matches) {
            matching_work=work;
            trials=std::max(trials,work.search_trials);
            result.receiver_workspace_supported|=work.workspace_supported;
        }
    }
    result.tracking_seconds=finite_seconds(tracking_serial/serial_operations_per_second);
    result.tracking_symbol_windows=finite_seconds(tracking_windows);
    // Budget track refinement at the serial rate even when long-symbol carrier
    // fits can share CPU workers. The hypothetical GPU
    // model offloads FFT scoring only, so this term remains in both totals.
    const auto serial_seconds=(serial+tracking_serial)/serial_operations_per_second;
    result.cpu_seconds=finite_seconds(.03L+serial_seconds+parallel/cpu_scoring_operations_per_second);
    result.receiver_cpu_seconds=finite_seconds(.03L+
        (serial-samples*channel_operations_per_sample+tracking_serial)/serial_operations_per_second+
        parallel/cpu_scoring_operations_per_second);
    result.gpu_seconds=finite_seconds(.11L+serial_seconds+parallel/gpu_scoring_operations_per_second+
        samples*sizeof(float)/gpu_transfer_bytes_per_second);
    if(!transmission.wire_bits)return result;

    // The simulator's SNR is per Fs/2 noise bandwidth, so Es/N0=snr*Fs*T/2.
    const auto symbol_db=channel.snr_db+10*std::log10(static_cast<long double>(samples_per_symbol)/2);
    const auto frequency=channel.frequency_offset_hz+config.carrier_hz*channel.clock_error_ppm*1e-6L;
    const auto geometry=modem::default_pattern_frequency_search(config);
    const bool coupled=geometry.count>5 && config.spreading_mode==modem::SpreadingMode::pattern;
    const auto frequencies=static_cast<long double>(geometry.count)*(coupled?2:1);
    const auto spacing=static_cast<long double>(geometry.step_hz);
    const auto outer_bin=static_cast<long double>(geometry.count/2);
    result.carrier_offset_hz=static_cast<double>(frequency);
    result.carrier_search_half_width_hz=geometry.half_width_hz;
    result.carrier_in_search=std::abs(frequency)<=geometry.half_width_hz;
    const auto nearest_bin=std::clamp(std::round(frequency/spacing),-outer_bin,outer_bin);
    // Generate the same double-valued offset as the receiver's bank before
    // evaluating the residual; endpoints cannot drift beyond modeled coverage.
    const auto nearest=static_cast<long double>(static_cast<double>(nearest_bin)*geometry.step_hz);
    const auto angle=std::numbers::pi_v<long double>*(frequency-nearest)*seconds;
    const auto carrier_loss=std::abs(angle)<1e-10L?1.L:std::pow(std::sin(angle)/angle,2);
    const auto diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi_v<long double>/180;
    const auto phase_loss=phase_coherence(.5L*diffusion*diffusion*seconds);
    result.phase_coherence_loss_db=static_cast<double>(std::max(0.L,-10*std::log10(phase_loss)));
    const auto section_phase_loss=phase_coherence(.5L*diffusion*diffusion*result.drift_section_seconds);
    result.section_phase_coherence_loss_db=static_cast<double>(std::max(0.L,-10*std::log10(section_phase_loss)));
    auto residual_clock_ppm=std::abs(static_cast<long double>(channel.clock_error_ppm));
    if(coupled)residual_clock_ppm=std::min(residual_clock_ppm,
        std::abs(channel.clock_error_ppm-nearest/config.carrier_hz*1e6L));
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
    if(!result.profile_matches || !result.carrier_in_search || !result.receiver_workspace_supported)return result;
    if(!compute_probability)return result;
    result.confidence_available=true;
    if(matching_work.drift_supported) {
        detail::ReceiverProbabilityParameters parameters;
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
        // The two unshaped bit templates share an amplitude envelope and a
        // balanced eight-chip sign mask. Resolve finite quarter correlations
        // exactly for small patterns; dense patterns use the orthogonal mean.
        const auto chip=modem::pattern_chip_samples(config);
        if(samples_per_symbol/chip<=1024) {
            auto code_config=config;
            if(options.timestamp)code_config.stream_epoch=options.timestamp;
            if(options.key)code_config=transfer::seeded_config(options,code_config.stream_epoch);
            modem::PatternCode code(code_config,code_config.stream_epoch);
            for(unsigned j=0;j<4;++j) {
                const auto begin=modem::detail::drift_boundary(j,samples_per_symbol,4);
                const auto end=modem::detail::drift_boundary(j+1,samples_per_symbol,4);
                double norm0=0,norm1=0,cross=0;
                for(auto at=begin;at<end;) {
                    const auto index=at/chip,until=std::min(end,(index+1)*chip);
                    const auto a=code.value(index,0,0),b=code.value(index,1,0);
                    const auto weight=static_cast<double>(until-at);
                    norm0+=weight*std::norm(a);norm1+=weight*std::norm(b);
                    cross+=weight*(a*std::conj(b)).real();at=until;
                }
                parameters.correlations[j]=std::clamp(cross/std::sqrt(norm0*norm1),-.999,.999);
                parameters.weights[j]=norm0;
            }
            const auto weight=std::accumulate(parameters.weights.begin(),parameters.weights.end(),0.);
            for(auto& section_weight:parameters.weights)section_weight/=weight;
        }
        const auto probability=detail::receiver_probability(parameters);
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
        // Diagnostic coherent energy uses the joint frequency/diffusion mean;
        // the success calculation uses individual path statistics above.
        const auto joint=expected_correlation_coherence(parameters.seconds,parameters.diffusion_degrees,
            parameters.residual_frequency)*parameters.timing_coherence;
        result.modeled_symbol_snr_db=joint>0?static_cast<double>(symbol_db)+10*std::log10(joint):-300;
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
    return result;
}
}
