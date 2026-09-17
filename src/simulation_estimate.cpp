#include "datapump/simulation_estimate.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
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
struct Work {long double serial=0,parallel=0,search_trials=1;};
Work receiver_work(const modem::Config& config,long double samples,
                   const transfer::Options& options,std::size_t profiles,std::size_t keys) {
    const auto symbol=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const bool private_pattern=config.scramble || config.dsss;
    const auto epochs=private_pattern?2.L*options.search_seconds+1+
        (options.timestamp?0:std::ceil((static_cast<long double>(modem::training_sample_count(config))+
            modem::pattern_pulse_padding_samples(config))/config.sample_rate)):1.L;
    const auto banks=epochs*keys;
    auto bin=std::gcd(std::gcd(chip,symbol),std::max<std::uint64_t>(1,chip/2));
    const auto omega=2*std::numbers::pi*config.carrier_hz/config.sample_rate;
    const auto sine=std::sin(omega);
    const auto image=std::abs(sine)>1e-12?std::abs(std::sin(static_cast<double>(bin)*omega)/sine):static_cast<double>(bin);
    if(symbol<=256 && (!private_pattern || image>1e-10*static_cast<double>(bin)))bin=1;
    const auto length=std::max(4.L,std::ceil(static_cast<long double>(symbol)/bin));
    const auto fft_log=std::ceil(std::log2(2*length));
    const auto transform=std::exp2(fft_log),hop=transform-length+1;
    const auto fft_bytes=transform*16*15+(4*length+2*hop)*16;
    const auto allowance=static_cast<long double>(options.dsp_workspace_bytes)/std::max(1.L,banks*profiles);
    const bool correlator=(private_pattern && symbol>=60.L*config.sample_rate) || fft_bytes>allowance;
    const auto starts=std::ceil(2.L*(options.search_seconds+1.L)*config.sample_rate/
                               std::max(1.L,std::floor(chip/2.L)))+1;
    const auto phase_groups=private_pattern?3.L:1.L;
    Work result;
    result.serial=samples*projection_operations_per_sample*banks;
    // Admission thresholds belong to one receiver; unrelated keys and
    // waveform profiles add compute work, not evidence against this signal.
    result.search_trials=std::max(1.L,starts*5*phase_groups);
    if(correlator) {
        // Bounded streaming projections are reused by half-chip start lanes;
        // each lane still evaluates two candidate bit fits per observation.
        const auto observations=std::ceil(samples/std::min(32.L,static_cast<long double>(chip)));
        result.parallel=observations*starts*5*phase_groups*64*banks;
    } else {
        const auto blocks=std::ceil(samples/(bin*hop));
        const auto jobs=5*phase_groups*(private_pattern?4:1);
        // Five real operations per complex FFT element per stage. Both bit
        // templates need inverse transforms; private templates may regenerate.
        result.parallel=blocks*(5*transform*fft_log*(1+2*jobs*(private_pattern?2:1))+
            jobs*(12*transform+40*hop))*banks;
    }
    return result;
}
}

Estimate estimate(const transfer::Estimate& transmission,const transfer::Options& options,bool raw_bits,
                  const modem::ChannelConfig& channel,std::span<const modem::Config> profiles,std::size_t keys) {
    modem::validate(options.modem);modem::validate_channel(options.modem,channel);
    if(!std::isfinite(transmission.total_seconds) || transmission.total_seconds<0 || !keys)
        throw Error("invalid simulation estimate input");
    if(profiles.empty())profiles=std::span(&options.modem,1);
    Estimate result;result.receiver_profiles=profiles.size();
    const auto& config=options.modem;
    const auto samples_per_symbol=modem::symbol_sample_count(config);
    const auto seconds=static_cast<long double>(samples_per_symbol)/config.sample_rate;
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
    long double serial=samples*channel_operations_per_sample,parallel=0,trials=1;
    for(const auto& profile:profiles) {
        modem::validate(profile);
        const auto matches=same_profile(config,profile);
        result.profile_matches|=matches;
        const auto work=receiver_work(profile,media*profile.sample_rate,options,profiles.size(),keys);
        serial+=work.serial;parallel+=work.parallel;
        if(matches)trials=std::max(trials,work.search_trials);
    }
    const auto serial_seconds=serial/serial_operations_per_second;
    result.cpu_seconds=finite_seconds(.03L+serial_seconds+parallel/cpu_scoring_operations_per_second);
    result.gpu_seconds=finite_seconds(.11L+serial_seconds+parallel/gpu_scoring_operations_per_second+
        samples*sizeof(float)/gpu_transfer_bytes_per_second);
    if(!transmission.wire_bits)return result;
    result.confidence_available=true;

    // The simulator's SNR is per Fs/2 noise bandwidth, so Es/N0=snr*Fs*T/2.
    const auto symbol_db=channel.snr_db+10*std::log10(static_cast<long double>(samples_per_symbol)/2);
    const auto frequency=channel.frequency_offset_hz+config.carrier_hz*channel.clock_error_ppm*1e-6L;
    const auto spacing=.25L/seconds;
    const auto nearest=std::clamp(std::round(frequency/spacing),-2.L,2.L)*spacing;
    const auto angle=std::numbers::pi_v<long double>*(frequency-nearest)*seconds;
    const auto carrier_loss=std::abs(angle)<1e-10L?1.L:std::pow(std::sin(angle)/angle,2);
    const auto diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi_v<long double>/180;
    const auto phase_loss=phase_coherence(.5L*diffusion*diffusion*seconds);
    const auto smear=std::abs(channel.clock_error_ppm)*1e-6L*seconds/chip_seconds;
    const auto timing_loss=config.spreading_mode==modem::SpreadingMode::tone?1.L:
        std::pow(std::max(0.L,1-smear/2),2);
    const auto coherence=carrier_loss*phase_loss*timing_loss;
    const auto effective_db=coherence>0?symbol_db-model_implementation_loss_db+10*std::log10(coherence):-300.L;
    result.modeled_symbol_snr_db=static_cast<double>(std::max(-300.L,effective_db));
    const auto energy=static_cast<double>(std::pow(10.L,std::clamp(effective_db/10,-30.L,12.L)));
    const auto bit_error=.5*std::exp(-energy/2);
    const auto admitted=normal_above(energy,5);
    const auto threshold=static_cast<double>(-std::log(1e-10L)+2*std::log(trials+1)+std::log(10.L));
    const auto acquired=normal_above(energy,threshold);
    const auto correct_bit=admitted*(1-bit_error);
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
