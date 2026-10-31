#include "datapump/modem.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/symbol_burst.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/crypto.hpp"
#include "datapump/helper_carrier.hpp"
#include "channel_perturbation.hpp"
#include "channel_spread.hpp"
#include "audio_trace.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <istream>
#include <initializer_list>
#include <utility>
#include <limits>
#include <numbers>
#include <optional>
#include <ostream>
#include <random>

namespace datapump::modem {
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;

void check(bool ok, const char* message) { if (!ok) throw Error(message); }
void check_cancelled(const std::stop_token& stop) {
    if (stop.stop_requested()) throw Error("modem operation cancelled");
}
void periodic_cancel(std::size_t position, const std::stop_token& stop) {
    if ((position & 4095U) == 0) check_cancelled(stop);
}
std::size_t product(std::size_t a, std::size_t b, std::size_t limit) {
    check(b == 0 || a <= limit / b, "modem memory limit exceeded");
    return a * b;
}
void budget(std::size_t limit, std::initializer_list<std::pair<std::size_t,std::size_t>> allocations) {
    for (auto [count, width] : allocations) {
        const auto bytes = product(count, width, limit);
        limit -= bytes;
    }
}
std::size_t chip_samples(const Config& c) {
    return static_cast<std::size_t>(std::ceil(2.*c.sample_rate/waveform_bandwidth_hz(c)));
}
void finite_samples(std::span<const float> samples, std::size_t limit, std::stop_token stop = {}) {
    product(samples.size(), sizeof(float), limit);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        periodic_cancel(i, stop);
        check(std::isfinite(samples[i]), "non-finite audio sample");
    }
}
void fft(std::vector<Complex>& data, bool inverse, std::stop_token stop = {}) {
    check_cancelled(stop);
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        periodic_cancel(i, stop);
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= n; length *= 2) {
        check_cancelled(stop);
        const Complex root = std::polar(1.0, (inverse ? tau : -tau) / static_cast<double>(length));
        for (std::size_t i = 0; i < n; i += length) {
            periodic_cancel(i, stop);
            Complex w{1, 0};
            for (std::size_t j = 0; j < length / 2; ++j) {
                periodic_cancel(i + j, stop);
                Complex u = data[i+j], v = data[i+j+length/2] * w;
                data[i+j] = u + v; data[i+j+length/2] = u - v; w *= root;
            }
        }
        if (length == n) break;
    }
    if (inverse) for (std::size_t i = 0; i < n; ++i) {
        periodic_cancel(i, stop);
        data[i] /= static_cast<double>(n);
    }
}
std::size_t fft_size(std::size_t minimum, std::size_t limit, std::size_t bytes_per_item) {
    std::size_t n = 1;
    while (n < minimum) { check(n <= limit / 2, "FFT size exceeds memory limit"); n *= 2; }
    product(n, bytes_per_item, limit);
    return n;
}
void put16(std::ostream& out, std::uint16_t value) {
    const char b[2]{static_cast<char>(value), static_cast<char>(value >> 8)}; out.write(b,2);
}
void put32(std::ostream& out, std::uint32_t value) {
    put16(out, static_cast<std::uint16_t>(value)); put16(out, static_cast<std::uint16_t>(value >> 16));
}
void exact(std::istream& in, char* destination, std::size_t size) {
    check(size <= static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()), "WAV read too large");
    check(static_cast<bool>(in.read(destination, static_cast<std::streamsize>(size))), "truncated WAV");
}
std::uint16_t get16(const unsigned char* b) { return static_cast<std::uint16_t>(b[0] | (b[1] << 8)); }
std::uint32_t get32(const unsigned char* b) { return get16(b) | (static_cast<std::uint32_t>(get16(b+2)) << 16); }
void discard(std::istream& in, std::size_t count) {
    std::array<char,4096> buffer{};
    while (count) { const auto n = std::min(count, buffer.size()); exact(in, buffer.data(), n); count -= n; }
}
}
unsigned parse_symbol_burst_wait(std::string_view value) {
    if(value=="off")return 0;
    if(value=="1")return 1;
    if(value=="2")return 2;
    if(value=="3")return 3;
    if(value=="5")return 5;
    if(value=="10")return 10;
    throw Error("Symbol_Burst accepts off, 1, 2, 3, 5 or 10 symbols");
}
unsigned parse_symbol_burst_random(std::string_view value) {
    if(value=="0")return 0;
    if(value=="1")return 1;
    if(value=="2")return 2;
    if(value=="3")return 3;
    throw Error("Symbol_Burst random extra accepts 0, 1, 2 or 3 symbols");
}
void validate(const Config& c) {
    check(c.search_arithmetic!=SearchArithmetic::matrix4,
          "INT4 search operands are not implemented on the CPU");
    check(static_cast<unsigned>(c.search_arithmetic)<=static_cast<unsigned>(SearchArithmetic::fp64),
          "unknown search arithmetic");
    check(static_cast<unsigned>(c.receiver_strategy)<=static_cast<unsigned>(ReceiverStrategy::amplitude_only),
          "unknown receiver strategy");
    check(std::isfinite(c.receiver_cpu_budget)&&c.receiver_cpu_budget>0,
          "receiver CPU budget must be finite and positive");
    check(std::isfinite(c.frequency_search_multiplier) && c.frequency_search_multiplier>=1 &&
          c.frequency_search_multiplier<=32,"carrier search expansion must be finite and within 1..32x");
    check(c.frequency_search_multiplier==1 || c.oscillator_search.has_value(),
          "carrier search expansion requires an explicit oscillator search policy");
    if(c.scheduled_hopping)c.scheduled_hopping->validate();
    const auto wait=c.symbol_burst.wait_symbols;
    check(wait==0 || wait==1 || wait==2 || wait==3 || wait==5 || wait==10,
          "Symbol_Burst wait must be Off, 1, 2, 3, 5 or 10 symbols");
    check(c.symbol_burst.random_limit<=3,"Symbol_Burst random extra must be 0..3 symbols");
    check(!c.symbol_burst.enabled() || !c.scheduled_hopping || !c.scheduled_hopping->enabled(),
          "Symbol_Burst cannot be combined with Sched_Hop");
    check(!c.symbol_burst.enabled() || !c.fake_hopping || !c.fake_hopping->defer_symbols,
          "Symbol_Burst cannot be combined with diagnostic FHSS whole-symbol deferral");
    if(c.oscillator_search)validate_oscillator_search(*c.oscillator_search);
    check(c.pattern_symbols && c.constellation_bits==1,
          "APSK transport has been removed; use one-bit pattern transport");
    check(c.sample_rate >= 64 && c.sample_rate <= 120000000, "internal sample rate must be 64..120000000 Hz");
    check(c.stream_phase_samples < c.sample_rate, "symbol stream phase must be within its whole second");
    check(std::isfinite(c.bandwidth_hz) && c.bandwidth_hz >= minimum_bandwidth_hz && waveform_bandwidth_hz(c) <= maximum_bandwidth_hz && waveform_bandwidth_hz(c) <= c.sample_rate / 2.0,
          "bandwidth must be finite and within 0.001 Hz..30 MHz and internal Nyquist");
    check(std::isfinite(c.training_seconds) && c.training_seconds == 2,
          "normal training duration is fixed at 2 seconds");
    check(std::isfinite(c.integration_seconds) && c.integration_seconds>=0,"invalid integration duration");
    check(c.spreading_factor >= 1 && c.spreading_factor <= 16384, "spreading factor must be 1..16384");
    check(c.spreading_mode == SpreadingMode::pattern || c.spreading_mode == SpreadingMode::tone,"unknown spreading mode");
    check(c.dsss_factor==1||c.dsss_factor==10||c.dsss_factor==100||c.dsss_factor==1000,
          "outer DSSS factor must be 1, 10, 100, or 1000");
    check(c.dsss_factor==1 || (c.scramble && c.spreading_mode==SpreadingMode::pattern),
          "outer DSSS requires independent private inner patterns");
    check(c.outer_dsss_version==OuterDsssVersion::legacy_v1 ||
          c.outer_dsss_version==OuterDsssVersion::interleaved_v2,"unknown outer DSSS wire version");
    const auto samples=symbol_sample_count(c);
    const auto chip=chip_samples(c);
    check(c.dsss_factor==1 || c.outer_dsss_version!=OuterDsssVersion::interleaved_v2 ||
          samples/chip<=maximum_interleaved_dsss_chips,
          "interleaved DSSS supports at most 1048576 complete fine chips per symbol; select legacy explicitly for larger geometries");
    // Do not call pattern_pulse_enabled here: its chip helper validates this
    // same configuration. Use the identical complete-chip eligibility rule.
    const bool shaped=c.pulse_shaping && c.spreading_mode==SpreadingMode::pattern &&
        samples/chip>=2*pattern_pulse_half_span;
    // This is the intended RRC support including rolloff, not a certified
    // emission mask. Finite pulse truncation and limiting still leave tails.
    const auto half_band=shaped ? (1+pattern_pulse_rolloff)*c.sample_rate/(2.*static_cast<double>(chip)) : waveform_bandwidth_hz(c)/2;
    check(std::isfinite(c.carrier_hz) && c.carrier_hz>=half_band &&
          c.carrier_hz+half_band<=c.sample_rate/2.,
          "carrier and waveform must fit above DC and below internal Nyquist; raise the carrier for short unshaped patterns or tone modes");
    if(c.oscillator_search) {
        const auto& policy=*c.oscillator_search;
        const auto rf=policy.rf_shift_hz==0?static_cast<long double>(c.carrier_hz):
            static_cast<long double>(policy.rf_shift_hz)+
                (policy.sideband==OscillatorSideband::upper?1.L:-1.L)*c.carrier_hz;
        check(std::isfinite(rf) && rf>0 && rf<=std::numeric_limits<double>::max(),
            "RF LO and sideband must describe a finite positive on-air carrier");
        const bool shared=policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0;
        const auto accuracy=shared?rf*policy.rf.accuracy_ppm:
            static_cast<long double>(c.carrier_hz)*policy.lf.accuracy_ppm+
                static_cast<long double>(policy.rf_shift_hz)*policy.rf.accuracy_ppm;
        const auto bound=accuracy*policy.margin*1e-6L;
        check(std::isfinite(bound) && bound<=std::numeric_limits<double>::max() &&
              bound*c.frequency_search_multiplier<=std::numeric_limits<double>::max(),
            "oscillator frequency search allowance exceeds numeric range");
        const auto phase=shared?policy.rf.phase_noise_degrees_per_sqrt_second:
            policy.rf_shift_hz==0?policy.lf.phase_noise_degrees_per_sqrt_second:
                std::hypot(policy.lf.phase_noise_degrees_per_sqrt_second,policy.rf.phase_noise_degrees_per_sqrt_second);
        check(phase<=180,"combined oscillator phase diffusion exceeds simulation range");
    }
    check(c.spreading_mode != SpreadingMode::tone || (!c.scramble && !c.dsss && !c.data_key),
          "tone mode is unencrypted and cannot enable Data, Scrambler or DSSS keystreams");
    check(c.memory_limit >= 1024, "modem memory limit must be at least 1024 bytes");
    check(chip_samples(c) >= 4, "chip sampling is too fast");
    product(chip_samples(c), c.spreading_factor, std::numeric_limits<std::size_t>::max());
}
void validate_channel(const Config& c,const ChannelConfig& channel) {
    check(std::isfinite(channel.spread_delay_ms) && channel.spread_delay_ms>=0 &&
          std::isfinite(channel.spread_doppler_hz) && channel.spread_doppler_hz>=0, "spread must be finite and nonnegative");
    check(std::isfinite(channel.perturbation_slow_depth) && channel.perturbation_slow_depth>=0 && channel.perturbation_slow_depth<=1 &&
          std::isfinite(channel.perturbation_fast_depth) && channel.perturbation_fast_depth>=0 && channel.perturbation_fast_depth<=1, "perturbation depths must be between 0 and 1");
    check(std::isfinite(channel.snr_db) && channel.snr_db>=-300 && channel.snr_db<=300,"channel SNR must be -300..300 dB");
    check(std::isfinite(channel.frequency_offset_hz) && std::abs(channel.frequency_offset_hz)<c.sample_rate/2.,"channel frequency offset must fit internal Nyquist");
    check(std::isfinite(channel.clock_error_ppm) && std::abs(channel.clock_error_ppm)<=10000,"relative clock error must be -10000..10000 ppm");
    check(std::isfinite(channel.phase_noise_degrees_per_sqrt_second) && channel.phase_noise_degrees_per_sqrt_second>=0 && channel.phase_noise_degrees_per_sqrt_second<=180,
          "phase diffusion must be 0..180 degrees per square-root second");
}
std::uint64_t symbol_sample_count(const Config& c) {
    if(c.integration_seconds>0) {
        const long double samples=std::ceil(static_cast<long double>(c.integration_seconds)*c.sample_rate);
        check(samples>=4 && samples<static_cast<long double>(std::numeric_limits<std::uint64_t>::max()),"integration duration exceeds 64-bit sample counter");
        return static_cast<std::uint64_t>(samples);
    }
    const long double samples=std::ceil(2.L*c.sample_rate*c.spreading_factor/c.bandwidth_hz);
    check(samples>=4 && samples<static_cast<long double>(std::numeric_limits<std::uint64_t>::max()),"symbol duration exceeds 64-bit sample counter");
    return static_cast<std::uint64_t>(samples);
}
std::uint64_t training_sample_count(const Config& c) {
    const auto target=static_cast<std::uint64_t>(c.sample_rate)*2;
    const auto symbol=symbol_sample_count(c);
    const auto count=target/symbol+(target%symbol>=symbol/2+symbol%2);
    check(count<=std::numeric_limits<std::uint64_t>::max()/symbol,"hardware preamble duration overflow");
    return count*symbol;
}
bool private_cover_enabled(const Config& c) {
    // PatternCode enables the private/outer construction only inside its
    // Scrambler-or-inner-DSSS path. A factor alone does not enable that path.
    return c.spreading_mode==SpreadingMode::pattern && (c.scramble || (c.dsss && c.data_key));
}
namespace {
std::pair<std::uint64_t,std::uint64_t> scheduled_flat_capacity(const Config& c) {
    const auto& outer=*c.scheduled_hopping;
    const auto group=symbol_schedule_detail::multiply(2ULL*c.sample_rate/5,outer.group_hops);
    const auto bound=static_cast<long double>(c.sample_rate)*outer.relative_clock_region_seconds();
    const auto left=static_cast<std::uint64_t>(std::ceil(bound));
    const auto right=static_cast<std::uint64_t>(std::ceil(bound+static_cast<long double>(c.sample_rate)*outer.audio_error_seconds));
    const auto ramp=static_cast<std::uint64_t>(std::ceil(static_cast<long double>(c.sample_rate)*outer.ramp_seconds));
    check(left<group && right<group-left && ramp<(group-left-right+1)/2,
          "scheduled guards leave no full-power source interval");
    auto good=group-left-right-2*ramp;
    if(c.fake_hopping) {
        const FhssPowerShape shape(*c.fake_hopping,c.sample_rate,pattern_pulse_padding_samples(c),symbol_sample_count(c));
        check(!shape.enabled||shape.smooth,"scheduled cover requires production smooth FHSS ramps");
        if(shape.enabled) {
            const auto& plan=*c.scheduled_hopping;
            const auto left=static_cast<std::uint64_t>(std::ceil(c.sample_rate*plan.relative_clock_region_seconds()))+
                static_cast<std::uint64_t>(std::ceil(c.sample_rate*plan.ramp_seconds));
            const auto right=static_cast<std::uint64_t>(std::ceil(c.sample_rate*(plan.relative_clock_region_seconds()+plan.audio_error_seconds)))+
                static_cast<std::uint64_t>(std::ceil(c.sample_rate*plan.ramp_seconds));
            good=std::numeric_limits<std::uint64_t>::max();
            // Five rational residues repeat after exactly two seconds. Sum
            // full-power integer samples with a five-window prefix, rather
            // than walking G hops for every template/calendar construction.
            const auto cycle=2ULL*c.sample_rate;
            std::array<std::pair<std::uint64_t,std::uint64_t>,5> windows{};
            std::uint64_t cycle_good=0;
            for(unsigned h=0;h<5;++h) {
                const auto lo=std::ceil(h*(2.L*c.sample_rate)/5+shape.quiet+shape.support-shape.lag_center)+1;
                const auto hi=std::floor((h+1)*(2.L*c.sample_rate)/5-shape.quiet-shape.support-shape.lag_center);
                check(lo>=0 && lo<=cycle && hi>=0 && hi<=cycle,"invalid scheduled FHSS flat window");
                windows[h]={static_cast<std::uint64_t>(lo),static_cast<std::uint64_t>(std::max(lo,hi))};
                cycle_good+=windows[h].second-windows[h].first;
            }
            const auto before=[&](std::uint64_t tick) {
                auto count=symbol_schedule_detail::multiply(tick/cycle,cycle_good);
                const auto remainder=tick%cycle;
                for(const auto& [a,b]:windows)if(remainder>a)
                    count=symbol_schedule_detail::add(count,std::min<std::uint64_t>(remainder,b)-a);
                return count;
            };
            for(unsigned residue=0;residue<5;++residue) {
                const auto begin=SchedHopSchedule::sample_boundary(residue,c.sample_rate,true);
                const auto flat_begin=symbol_schedule_detail::add(begin,left);
                const auto flat_end=symbol_schedule_detail::add(begin,group-right);
                good=std::min(good,before(flat_end)-before(flat_begin));
            }
        }
    }
    check(good!=0,"scheduled/FHSS guards leave no useful cover samples");
    return {group,good};
}
}
namespace {
long double scheduled_minimum_flat_count(const Config& c,std::uint64_t duration) {
    const auto [group,good]=scheduled_flat_capacity(c);
    if(!duration)return 0;
    // Minimum overlap with a periodic single flat interval. Whole periods
    // contribute F; a remainder can first occupy the entire P-F quiet part.
    const auto retained=[&](std::uint64_t period,std::uint64_t flat) {
        return static_cast<long double>(duration/period)*flat+
            (duration%period>period-flat?duration%period-(period-flat):0);
    };
    long double count=0;
    if(!c.fake_hopping || !c.fake_hopping->power_ramps) count=retained(group,good);
    else if(c.scheduled_hopping->group_hops==1) {
        // Every group contains a single joint flat. Five rounding residues
        // differ by at most one sample at either endpoint; reserve both
        // endpoint samples and retain their common
        // subset so arbitrary relative placement cannot gain a hidden gap.
        count=retained(group,good>2?good-2:0);
    } else if(c.sample_rate%5==0) {
        const auto& plan=*c.scheduled_hopping;
        const auto outer_loss=static_cast<std::uint64_t>(std::ceil(c.sample_rate*plan.relative_clock_region_seconds()))+
            static_cast<std::uint64_t>(std::ceil(c.sample_rate*(plan.relative_clock_region_seconds()+plan.audio_error_seconds)))+
            2*static_cast<std::uint64_t>(std::ceil(c.sample_rate*plan.ramp_seconds));
        const FhssPowerShape shape(*c.fake_hopping,c.sample_rate,pattern_pulse_padding_samples(c),symbol_sample_count(c));
        const auto period=2ULL*c.sample_rate/5;
        const auto lo=std::ceil(shape.quiet+shape.support-shape.lag_center)+1;
        const auto hi=std::floor(shape.period-shape.quiet-shape.support-shape.lag_center);
        const auto inner=hi>lo?static_cast<std::uint64_t>(hi-lo):0;
        // Exact D-periodicity in source time when Fs is divisible by five.
        // Union bound for the two independent excluded sets, no G-sized scan.
        count=std::max(0.L,retained(group,group-outer_loss)+retained(period,inner)-duration);
    } else {
        const auto touched=symbol_schedule_detail::add(1,1+(duration-1)/group);
        count=std::max(0.L,static_cast<long double>(duration)-static_cast<long double>(touched)*(group-good));
        const auto& plan=*c.scheduled_hopping;
        const auto outer_loss=static_cast<std::uint64_t>(std::ceil(c.sample_rate*plan.relative_clock_region_seconds()))+
            static_cast<std::uint64_t>(std::ceil(c.sample_rate*(plan.relative_clock_region_seconds()+plan.audio_error_seconds)))+
            2*static_cast<std::uint64_t>(std::ceil(c.sample_rate*plan.ramp_seconds));
        const FhssPowerShape shape(*c.fake_hopping,c.sample_rate,pattern_pulse_padding_samples(c),symbol_sample_count(c));
        const auto period=2ULL*c.sample_rate;std::uint64_t inner_good=0;
        for(unsigned h=0;h<5;++h) {
            const auto lo=std::ceil(h*(2.L*c.sample_rate)/5+shape.quiet+shape.support-shape.lag_center)+1;
            const auto hi=std::floor((h+1)*(2.L*c.sample_rate)/5-shape.quiet-shape.support-shape.lag_center);
            if(hi>lo)inner_good+=static_cast<std::uint64_t>(hi-lo);
        }
        // Five fractional hop residues repeat after exactly 2*Fs samples.
        // Each source-group piece has arbitrary FHSS phase, but loses at most
        // one cycle's quiet budget beyond its duty-average loss. Sum over the
        // bounded number of pieces, then subtract outer edge guards. This
        // avoids an O(G) scan or a whole-G wait when G is very large.
        const auto piece_loss=symbol_schedule_detail::add(period-inner_good,outer_loss);
        const auto periodic=static_cast<long double>(duration)*inner_good/period-
            static_cast<long double>(touched)*piece_loss;
        count=std::max(count,periodic);
    }
    return std::clamp(count,0.L,static_cast<long double>(duration));
}
}
double scheduled_minimum_flat_fraction(const Config& c,std::uint64_t source_samples) {
    if(!scheduled_bursts(c))return 1.;
    const auto duration=source_samples?source_samples:symbol_sample_count(c);
    if(!duration)return 0.;
    return static_cast<double>(scheduled_minimum_flat_count(c,duration)/duration);
}
bool packed_scheduled_symbols(const Config& c) {
    // Geometry only; never instantiate a keyed calendar from validate(). Long
    // symbols retain split source time, while potentially mostly-muted short
    // symbols require complete full-gain slots with both pulse shoulders.
    if(!scheduled_bursts(c))return false;
    const auto hop=2ULL*c.sample_rate/5;
    const auto gate_period=c.fake_hopping && c.fake_hopping->power_ramps?hop:
        symbol_schedule_detail::multiply(hop,c.scheduled_hopping->group_hops);
    // A long symbol necessarily spans guards and must remain splittable even
    // when their overall duty costs substantial energy. Whole-slot deferral is
    // only meaningful for a bit shorter than the applicable gate period.
    return symbol_sample_count(c)<gate_period && scheduled_minimum_flat_fraction(c)<.5;
}
double waveform_gate_bandwidth_allowance_hz(const Config& c) {
    // Additional FULL nominal occupied-bandwidth allowance. This is an
    // engineering rolloff scale, not compact support or a regulatory mask.
    // Independent cascaded gates convolve spectra, so add their scales.
    double extra=0;
    if(scheduled_bursts(c))extra+=2/c.scheduled_hopping->ramp_seconds;
    if(c.fake_hopping && c.fake_hopping->power_ramps && c.fake_hopping->smooth_ramps)
        extra+=2/c.fake_hopping->ramp_seconds;
    return extra;
}
std::uint64_t protected_cover_span_samples(const Config& c) {
    const auto useful=static_cast<std::uint64_t>(c.sample_rate)*3;
    if(!scheduled_bursts(c)) {
        if(!c.fake_hopping)return useful;
        return FhssPowerShape(*c.fake_hopping,c.sample_rate,pattern_pulse_padding_samples(c),symbol_sample_count(c)).protected_span(useful);
    }
    // The logical shaper clock pauses during nulls. Reserve enough COMPLETE
    // active groups to retain three full-power seconds, plus one group for an
    // arbitrary initial partial group. This is intermittent masking; its wall
    // duration is the secret calendar's exact inverse, not three wall seconds.
    const auto [group,good]=scheduled_flat_capacity(c);
    const auto groups=symbol_schedule_detail::add(1,1+(useful-1)/good);
    auto high=symbol_schedule_detail::multiply(groups,group);
    const auto certified=[&](std::uint64_t span) {
        const auto count=scheduled_minimum_flat_count(c,span);
        const auto rounding=std::max(1.L,8*std::numeric_limits<long double>::epsilon()*span);
        return count-rounding>=useful;
    };
    // Large G must not force a cancellation to wait through an entire active
    // group. Every adopted endpoint has its own conservative >=3s certificate.
    // The lower bound need not be monotone: replacing HIGH only on a proved
    // endpoint preserves correctness; a nonmonotone bound can merely be less
    // economical. Retain the independent full-group proof as the fallback.
    if(!certified(high))return high;
    std::uint64_t low=0;
    while(high-low>1) {
        const auto middle=low+(high-low)/2;
        if(certified(middle))high=middle;else low=middle;
    }
    return high;
}
std::uint64_t suppression_sample_count(const Config& c) {
    const auto useful=protected_cover_span_samples(c);
    if(!private_cover_enabled(c))return useful;
    const auto support=pattern_pulse_padding_samples(c);
    const auto extra=support?2*support+1:std::max<std::uint64_t>(1,c.sample_rate/100);
    check(extra<=std::numeric_limits<std::uint64_t>::max()-useful,"suppression duration overflow");
    return useful+extra;
}
std::uint64_t transmit_stop_sample_count(const Config& c) {
    const auto support=pattern_pulse_padding_samples(c);
    const auto pending=support?2*support+1:pattern_chip_samples(c);
    const auto tail=suppression_sample_count(c);
    check(pending<=std::numeric_limits<std::uint64_t>::max()-tail,"stop duration overflow");
    return pending+tail;
}
std::uint64_t scheduled_physical_span_samples(const Config& c,std::uint64_t source_samples) {
    if(!source_samples || !scheduled_bursts(c))return source_samples;
    const auto& plan=*c.scheduled_hopping;
    plan.validate();
    const auto twice_rate=2ULL*c.sample_rate;
    const auto group=symbol_schedule_detail::multiply(twice_rate/5,plan.group_hops);
    check(group!=0,"scheduled source group has no samples");
    const auto groups=1+(source_samples-1)/group;
    const auto jitter=(plan.time_multiplier-plan.group_hops)/2;
    // At most ceil(N/H) quiet intervals can interrupt N source samples, even
    // when observation starts inside a quiet interval. Across those intervals
    // the keyed frame offsets telescope: only the endpoint difference (<=J),
    // not a fresh J per interval, remains. Exact rational hop boundaries give
    // N + ceil(2*Fs*(groups*M+J)/5) - groups*H. The left inverse at an exact
    // source-group end excludes the following quiet interval.
    //
    // Form the nonnegative extra span before multiplying. Quotient/remainder
    // decomposition computes ceil((groups*gap+2*Fs*J)/5) exactly, without
    // overflowing intermediate products when the final uint64 span still fits
    // and without requiring a compiler-specific 128-bit integer type.
    using symbol_schedule_detail::add;
    using symbol_schedule_detail::multiply;
    const auto gap=twice_rate*plan.time_multiplier-5*group;
    const auto whole=add(multiply(groups,gap/5),multiply(groups/5,gap%5));
    const auto fraction=((groups%5)*(gap%5)+twice_rate*jitter+4)/5;
    return add(source_samples,add(whole,fraction));
}
Config scheduled_estimate_config(Config c) {
    if(c.symbol_burst.enabled()) {
        const SymbolBurstPlan plan(c);
        try {
            if(c.stream_epoch) {
                (void)plan.bind_payload(c.stream_epoch,c.stream_phase_samples);
                return c;
            }
        } catch(const Error&) {
            // Geometry may exist before a real transmit origin is selected.
            // Only estimation chooses this representative; actual binding is strict.
        }
        constexpr std::uint64_t representative_epoch=1710000000;
        const auto first=plan.first_slot({std::max(c.stream_epoch,representative_epoch),0});
        c.stream_epoch=first.payload_begin.epoch;
        c.stream_phase_samples=first.payload_begin.phase;
        return c;
    }
    if(!scheduled_bursts(c))return c;
    check(c.data_key.has_value(),"scheduled estimate requires its immutable encryption key");
    const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto prefix=symbol_schedule_detail::add(training_sample_count(c),pattern_pulse_padding_samples(c));
    const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(c.stream_epoch,c.sample_rate),c.stream_phase_samples);
    const auto at=map.locate(anchor);
    std::optional<ScheduledSymbolCalendar> calendar;
    if(packed_scheduled_symbols(c))calendar.emplace(c);
    if(at.active && at.source.sample>=prefix && (!calendar ||
       calendar->source_slot(calendar->first_rank_source(at.source.sample))==at.source.sample))return c;
    constexpr std::uint64_t representative_epoch=1710000000;
    const auto request=symbol_schedule_detail::multiply(std::max(c.stream_epoch,representative_epoch),c.sample_rate);
    const auto output=map.source_to_physical(map.locate(request).source);
    auto source=map.locate(output).source.add_samples(prefix);
    if(calendar)source={calendar->source_slot(calendar->first_rank_source(source.sample)),0};
    const auto payload=map.source_to_physical(source);
    check(payload.fraction==0,"representative payload anchor must be an integer sample");
    c.stream_epoch=payload.sample/c.sample_rate;c.stream_phase_samples=payload.sample%c.sample_rate;
    return c;
}
std::uint64_t pattern_transmission_source_sample_count(const Config& c,std::uint64_t symbols,bool surrounding_noise) {
    check(symbols!=0,"waveform requires at least one payload symbol");
    const auto duration=symbol_sample_count(c);
    const auto payload=packed_scheduled_symbols(c)?symbol_schedule_detail::add(
        ScheduledSymbolCalendar(c).offset_source(c.stream_epoch,c.stream_phase_samples,symbols-1),duration):
        (scheduled_bursts(c)?symbol_schedule_detail::multiply(symbols,duration):
        symbol_schedule_detail::add(scheduled_symbol_offset(c,c.stream_epoch,c.stream_phase_samples,symbols-1),duration));
    const auto prefix=surrounding_noise?training_sample_count(c):0;
    const auto padding=pattern_pulse_padding_samples(c);
    auto total=symbol_schedule_detail::add(symbol_schedule_detail::add(payload,prefix),symbol_schedule_detail::multiply(2,padding));
    if(surrounding_noise)total=symbol_schedule_detail::add(total,suppression_sample_count(c));
    return total;
}
std::uint64_t pattern_transmission_sample_count(const Config& c,std::uint64_t symbols,bool surrounding_noise) {
    const auto total=pattern_transmission_source_sample_count(c,symbols,surrounding_noise);
    if(!scheduled_bursts(c))return total;
    check(c.data_key.has_value(),"scheduled duration requires its immutable encryption key");
    const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(c.stream_epoch,c.sample_rate),c.stream_phase_samples);
    const auto payload=map.locate(anchor).source;
    const auto prefix=symbol_schedule_detail::add(surrounding_noise?training_sample_count(c):0,pattern_pulse_padding_samples(c));
    check(payload.sample>=prefix,"scheduled prefix precedes the representable source clock");
    const auto source_origin=payload.sample-prefix;
    const auto begin=map.source_to_physical({source_origin});
    const auto end=map.source_to_physical({symbol_schedule_detail::add(source_origin,total)},true);
    check(begin.fraction==0 && end.fraction==0 && end.sample>=begin.sample,"invalid scheduled waveform extent");
    return end.sample-begin.sample;
}
double symbol_seconds(const Config& c) { validate(c); return c.integration_seconds>0?c.integration_seconds:2.*c.spreading_factor/c.bandwidth_hz; }
double bit_rate(const Config& c) {
    if(scheduled_bursts(c)) {
        if(packed_scheduled_symbols(c)) {
            const ScheduledSymbolCalendar slots(c);
            return c.constellation_bits*static_cast<double>(slots.slots_per_group())/(.4*c.scheduled_hopping->time_multiplier);
        }
        const auto source_group=static_cast<long double>(2ULL*c.sample_rate/5)*c.scheduled_hopping->group_hops;
        const auto physical_frame=2.L*c.sample_rate*c.scheduled_hopping->time_multiplier/5;
        return c.constellation_bits/symbol_seconds(c)*static_cast<double>(source_group/physical_frame);
    }
    if(deferred_symbols(c)) {
        const FhssSymbolCalendar slots(*c.fake_hopping,c.sample_rate,pattern_pulse_padding_samples(c),symbol_sample_count(c));
        return c.constellation_bits*static_cast<double>(slots.per_hop)/c.fake_hopping->dwell_seconds;
    }
    return c.constellation_bits/symbol_seconds(c);
}
std::size_t payload_symbol_count(std::size_t payload_bytes,const Config& c) {
    validate(c);
    return product(payload_bytes,8,std::numeric_limits<std::size_t>::max());
}
std::size_t waveform_sample_count(std::size_t wire_bytes,const Config& c) {
    validate(c);
    check(wire_bytes>0,"waveform requires at least one payload byte");
    const auto count=payload_symbol_count(wire_bytes,c);
    const auto samples=pattern_transmission_sample_count(c,count);
    check(samples<=std::numeric_limits<std::size_t>::max(),"modem sample count exceeds platform address space");
    return static_cast<std::size_t>(samples);
}
bool memory_supported(std::size_t wire_bytes,std::size_t preamble_bytes,const Config& c) {
    validate(c);
    try {
        check(preamble_bytes==0 && wire_bytes>0,
              "invalid estimated training length");
        const auto samples=waveform_sample_count(wire_bytes,c);
        budget(c.memory_limit,{{samples,sizeof(float)+sizeof(Complex)},{wire_bytes,2},{8*1024*1024,1}});
        return true;
    } catch(const Error&) {return false;}
}
Bytes preamble(const Config& c) {
    validate(c);
    // Hardware settling is generated directly by PatternTransmitter under
    // the same selected protections, using its separate preamble counters.
    return {};
}
std::vector<float> modulate(std::span<const std::uint8_t> bytes, const Config& c, std::stop_token stop) {
    check_cancelled(stop); validate(c);
    const auto count=waveform_sample_count(bytes.size(),c);
    budget(c.memory_limit,{{count,sizeof(float)},{bytes.size(),2},{65536,1}});
    StreamingTransmitter source(Bytes(bytes.begin(),bytes.end()),c);
    std::vector<float> result(count);
    std::size_t position=0;
    while(position<result.size())position+=source.read(std::span(result).subspan(position,std::min<std::size_t>(4096,result.size()-position)),stop);
    return result;
}
DecodeResult demodulate(std::span<const float> samples, const Config& c,
                        std::span<const std::uint8_t>, std::stop_token stop) {
    check_cancelled(stop); validate(c); finite_samples(samples,c.memory_limit,stop);
    throw Error("binary patterns require PatternReceiver blind sample acquisition");
}
std::vector<float> simulate(std::span<const float> samples, const Config& c, const ChannelConfig& channel) {
    validate(c); finite_samples(samples, c.memory_limit);
    validate_channel(c,channel);
    const auto rate=1+static_cast<long double>(channel.clock_error_ppm)*1e-6L;
    const bool spread_enabled=channel.spread_delay_ms!=0 || channel.spread_doppler_hz!=0;
    const bool perturbation_enabled=channel.spread_doppler_hz!=0 &&
        (channel.perturbation_slow_depth!=0 || channel.perturbation_fast_depth!=0);
    const auto occupied=spread_enabled?helper::placement_band(c).waveform_bandwidth_hz:0.;
    if(spread_enabled) {
        const auto modulation=channel.perturbation_fast_depth>0?channel.spread_doppler_hz:
            channel.perturbation_slow_depth>0?.1*channel.spread_doppler_hz:0;
        const auto upper=(c.carrier_hz+occupied/2)*rate+channel.frequency_offset_hz+
            channel.spread_doppler_hz+modulation;
        const auto lower=(c.carrier_hz-occupied/2)*rate+channel.frequency_offset_hz-modulation;
        check(channel.spread_doppler_hz<c.sample_rate/2. && upper<=c.sample_rate/2. && lower>=0,
              "Doppler-spread payload leaves the sampled channel passband");
    }
    std::mt19937_64 startup_random(channel.seed^0x8ebc6af09c88c6e3ULL);
    const auto uniform=[&]{return (static_cast<double>(startup_random()>>11)+.5)/9007199254740992.;};
    const auto initial_phase=static_cast<long double>(tau)*(uniform()-.5L);
    const auto startup=channel.delay_samples+std::floor((.05L+.25L*uniform())*c.sample_rate)+.05L+.9L*uniform();
    const auto delay=spread_enabled && !samples.empty()?
        static_cast<long double>(channel.spread_delay_ms)*c.sample_rate/1000:0;
    const auto receiver_count=std::ceil(startup+static_cast<long double>(samples.size())/rate+delay);
    check(receiver_count<=c.memory_limit/sizeof(float),"simulation duration exceeds memory limit");
    const auto count=static_cast<std::size_t>(receiver_count);
    budget(c.memory_limit, {{samples.size(),sizeof(float)}, {count,sizeof(float)}});
    std::vector<float> out(count);
    double power = 0;
    for (auto v : samples) power += static_cast<double>(v) * v;
    power = samples.empty() ? 0 : power / static_cast<double>(samples.size());
    if (!samples.empty()) {
        const auto n = fft_size(samples.size(), c.memory_limit, sizeof(Complex));
        budget(c.memory_limit, {{samples.size(),sizeof(float)}, {out.size(),sizeof(float)}, {n,sizeof(Complex)}});
        std::vector<Complex> analytic(n);
        std::copy(samples.begin(), samples.end(), analytic.begin());
        fft(analytic, false);
        for (std::size_t k = 1; k < n/2; ++k) analytic[k] *= 2;
        for (std::size_t k = n/2+1; k < n; ++k) analytic[k] = 0;
        fft(analytic, true);
        std::optional<detail::SpreadRay> spread;
        std::optional<detail::FrequencySelectivePerturbation> perturbation;
        if(spread_enabled) {
            spread.emplace(c.sample_rate,channel.spread_doppler_hz,channel.seed);
            audio::trace::record("simulation_two_ray_spread",[&](auto& f) {
                f.add("seed",channel.seed);f.add("delay_ms",channel.spread_delay_ms);
                f.add("doppler_hz",spread->doppler_hz());f.add("phase_rad",spread->phase_radians());
            });
        }
        if(perturbation_enabled) {
            perturbation.emplace(c.sample_rate,c.carrier_hz*rate,occupied*rate,channel.spread_doppler_hz,
                channel.perturbation_slow_depth,channel.perturbation_fast_depth,channel.seed);
            audio::trace::record("simulation_frequency_selective_perturbation",[&](auto& f) {
                f.add("seed",channel.seed);f.add("doppler_reference_hz",channel.spread_doppler_hz);
                f.add("slow_depth",channel.perturbation_slow_depth);f.add("fast_depth",channel.perturbation_fast_depth);
                f.add("slow_rate_hz",perturbation->rates_hz[0]);f.add("fast_rate_hz",perturbation->rates_hz[1]);
                f.add("band_width_hz",perturbation->band_width_hz);
                for(std::size_t band=0;band<3;++band) {
                    f.add(("band"+std::to_string(band)+"_center_hz").c_str(),perturbation->band_centers_hz[band]);
                    f.add(("band"+std::to_string(band)+"_slow_phase_rad").c_str(),perturbation->initial_phases[band][0]);
                    f.add(("band"+std::to_string(band)+"_fast_phase_rad").c_str(),perturbation->initial_phases[band][1]);
                }
            });
        }
        const auto interpolate=[&](long double position) {
            if(position<0 || position>=static_cast<long double>(samples.size()))return Complex{};
            const auto center=static_cast<std::int64_t>(std::floor(position));
            Complex value{};double weight=0;
            if(std::abs(position-center)<1e-10)return analytic[static_cast<std::size_t>(center)];
            for(auto tap=center-7;tap<=center+8;++tap) {
                const auto distance=static_cast<double>(position-tap);
                const auto window=.42+.5*std::cos(std::numbers::pi*distance/8)+.08*std::cos(std::numbers::pi*distance/4);
                const auto angle=std::numbers::pi*distance;
                const auto coefficient=(std::abs(angle)<1e-8?1:std::sin(angle)/angle)*window;
                weight+=coefficient;
                if(tap>=0 && tap<static_cast<std::int64_t>(samples.size()))value+=analytic[static_cast<std::size_t>(tap)]*coefficient;
            }
            return weight?value/weight:value;
        };
        std::mt19937_64 phase_random(channel.seed^0xa0761d6478bd642fULL);
        const auto diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(c.sample_rate);
        std::normal_distribution<double> phase_step(0,1);double phase=0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto position=(static_cast<long double>(i)-startup)*rate;
            if(position<0 && !spread && !perturbation) {if(diffusion)phase+=phase_step(phase_random)*diffusion;continue;}
            auto value=interpolate(position);
            if(spread) value=spread->process(value,delay==0?value:interpolate(position-delay*rate));
            if(perturbation)value=perturbation->process(value);
            const auto angle=std::remainder(initial_phase+static_cast<long double>(tau)*
                (channel.frequency_offset_hz*static_cast<long double>(i)+c.carrier_hz*rate*startup)/c.sample_rate+phase,static_cast<long double>(tau));
            out[i]=static_cast<float>((value*std::polar(1.,static_cast<double>(angle))).real());
            if(diffusion)phase+=phase_step(phase_random)*diffusion;
        }
    }
    const double sigma = std::sqrt(power * std::pow(10.0, -channel.snr_db / 10));
    std::mt19937_64 generator(channel.seed);
    std::normal_distribution<double> normal(0, sigma > 0 ? sigma : 1);
    if (sigma > 0) for (auto& v : out) v = static_cast<float>(v + normal(generator));
    finite_samples(out, c.memory_limit);
    return out;
}
void write_wav(std::ostream& out, std::span<const float> samples, std::uint32_t rate) {
    check(rate >= 64 && rate <= 120000000, "invalid WAV sample rate");
    check(samples.size() <= (std::numeric_limits<std::uint32_t>::max() - 36) / 2, "WAV is too large");
    finite_samples(samples, std::numeric_limits<std::size_t>::max());
    out.write("RIFF",4); put32(out, static_cast<std::uint32_t>(36+samples.size()*2)); out.write("WAVEfmt ",8);
    put32(out,16); put16(out,1); put16(out,1); put32(out,rate); put32(out,rate*2); put16(out,2); put16(out,16);
    out.write("data",4); put32(out,static_cast<std::uint32_t>(samples.size()*2));
    for (auto v : samples) {
        const auto quantized = static_cast<std::int16_t>(std::lround(std::clamp(static_cast<double>(v), -1.0, 32767.0/32768) * 32768));
        put16(out, static_cast<std::uint16_t>(quantized));
    }
    check(static_cast<bool>(out), "failed to write WAV");
}
Wav read_wav(std::istream& in, std::size_t limit) {
    std::array<unsigned char,16> b{};
    exact(in,reinterpret_cast<char*>(b.data()),12);
    check(std::memcmp(b.data(),"RIFF",4)==0 && std::memcmp(b.data()+8,"WAVE",4)==0, "not a RIFF WAVE file");
    auto remaining = static_cast<std::uint64_t>(get32(b.data()+4));
    check(remaining >= 4, "invalid RIFF size"); remaining -= 4;
    check(remaining <= 1024*1024 || (remaining-1024*1024+1)/2 <= limit, "WAV container exceeds memory policy");
    Wav result{}; bool have_format = false, have_data = false;
    while (remaining) {
        check(remaining >= 8, "truncated WAV chunk header");
        exact(in,reinterpret_cast<char*>(b.data()),8); remaining -= 8;
        const auto size = static_cast<std::uint64_t>(get32(b.data()+4));
        const auto padded_size = size + (size & 1);
        check(padded_size <= remaining, "WAV chunk extends beyond RIFF container");
        if (std::memcmp(b.data(),"fmt ",4)==0) {
            check(!have_format && size >= 16, "invalid or repeated WAV format chunk");
            exact(in,reinterpret_cast<char*>(b.data()),16);
            result.sample_rate = get32(b.data()+4);
            check(get16(b.data())==1 && get16(b.data()+2)==1 && get16(b.data()+14)==16 && get16(b.data()+12)==2,
                  "only PCM16 mono WAV is supported");
            check(result.sample_rate >= 64 && result.sample_rate <= 120000000 && get32(b.data()+8)==result.sample_rate*2,
                  "invalid WAV sample rate or byte rate");
            discard(in,static_cast<std::size_t>(size-16)); have_format = true;
        } else if (std::memcmp(b.data(),"data",4)==0) {
            check(have_format && !have_data && size % 2 == 0, "invalid or repeated WAV data chunk");
            product(static_cast<std::size_t>(size/2),sizeof(float),limit);
            result.samples.resize(static_cast<std::size_t>(size/2));
            for (auto& sample : result.samples) {
                exact(in,reinterpret_cast<char*>(b.data()),2);
                const auto value = get16(b.data());
                sample = static_cast<float>((value < 32768 ? static_cast<int>(value) : static_cast<int>(value)-65536) / 32768.0);
            }
            have_data = true;
        } else discard(in,static_cast<std::size_t>(size));
        if (size & 1) discard(in,1);
        remaining -= padded_size;
    }
    check(have_format && have_data, "WAV is missing format or data"); return result;
}
std::vector<float> modulate_status(std::span<const std::uint8_t> bits, const Config& c) {
    validate(c);
    PatternTransmitter source(Bytes(bits.begin(),bits.end()),c,c.stream_epoch);
    check(source.total_samples()<=c.memory_limit/sizeof(float),"pattern waveform exceeds memory limit");
    const auto count=static_cast<std::size_t>(source.total_samples());
    budget(c.memory_limit,{{count,sizeof(float)},{source.working_bytes(),1}});
    std::vector<float> output(count);
    for(std::size_t position=0;position<output.size();)
        position+=source.read(std::span(output).subspan(position,std::min<std::size_t>(4096,output.size()-position)));
    return output;
}
double detect_status(std::span<const float> samples, std::span<const std::uint8_t> bits, const Config& c) {
    validate(c); finite_samples(samples,c.memory_limit);
    check(!bits.empty(),"known status bits are required");
    const auto duration=symbol_sample_count(c);
    check(duration<=std::numeric_limits<std::size_t>::max(),"status symbol duration exceeds platform sample counter");
    const auto payload_count = product(bits.size(),static_cast<std::size_t>(duration),c.memory_limit/sizeof(float));
    const auto training=training_sample_count(c);
    check(training<=c.memory_limit/sizeof(float)-payload_count,"status waveform exceeds memory limit");
    const auto padding=pattern_pulse_padding_samples(c);
    const auto content_count=payload_count+static_cast<std::size_t>(training);
    check(padding<=(c.memory_limit/sizeof(float)-content_count)/2,"status pulse tails exceed memory limit");
    const auto expected_count=content_count+2*static_cast<std::size_t>(padding);
    budget(c.memory_limit,{{samples.size(),sizeof(float)},{expected_count,sizeof(float)},{bits.size(),1},{16384,1}});
    const auto reference = modulate_status(bits,c);
    check(samples.size() == reference.size(),"status detector requires exactly the known symbol duration");
    double dot=0, signal=0, expected=0;
    for (std::size_t i=0; i<samples.size(); ++i) { dot+=samples[i]*reference[i]; signal+=samples[i]*samples[i]; expected+=reference[i]*reference[i]; }
    return signal > 0 && expected > 0 ? dot/std::sqrt(signal*expected) : 0;
}
}
