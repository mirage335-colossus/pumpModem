#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_statistics.hpp"
#include "datapump/tuning.hpp"
#include "datapump/clock_sync.hpp"
#include "datapump/pattern_pulse.hpp"
#include "../src/channel_perturbation.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <optional>
#include <complex>
#include <map>
#include <bit>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <sstream>
#include <set>
#include <tuple>
#include <memory>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
namespace statistics = datapump::triad::statistics;
using Clock = std::chrono::steady_clock;

struct Options {
    std::string mode = "plan";
    std::string channel = "none";
    std::string control = "signal";
    std::string output;
    double seconds = 16;
    double source_window_seconds = 0;
    std::optional<std::uint64_t> source_first_sample;
    bool source_window_center = false;
    double cn0 = 8;
    double rate = 36, rf_hz = 30e6;
    double start_low = 0, start_high = 0, start_step = 1;
    std::optional<std::array<double,2>> arrival_prior_seconds;
    double frequency_low = 0, frequency_high = 0, frequency_step = .1;
    double sample_rate = 0;
    std::uint32_t sample_rate_denominator = 1;
    double timing_loss_db = .5;
    double phase_diffusion = .5*std::numbers::pi/180, white_fm = 0, random_walk_fm = 0;
    double flicker_fm = 0, flicker_min_tau = .1, flicker_max_tau = 86400;
    double white_pm = 0;
    unsigned flicker_bands = 12;
    double dropout_ms = 0, dropout_start_ms = 0;
    std::size_t dropout_every_dwells = 1;
    std::vector<std::uint64_t> missed_dwells;
    double minimum_observed_fraction = .9;
    double correct = .99, false_probability = 1e-4, horizon = 86400;
    double attempt_spacing = 60;
    double outage = 0;
    double retained_energy = 1;
    double wrong_projection = .08;
    std::uint64_t minimum_dwells = 20, seed = 1, epoch = 1793404800;
    std::optional<std::uint64_t> key_seed, channel_seed;
    std::optional<std::uint64_t> oscillator_seed, noise_seed, frontend_seed, path_seed;
    std::uint64_t duration_alternatives = 1;
    unsigned added = 10, threads = 2;
    std::size_t chunk = 4096, block = 1536;
    double block_seconds = -1;
    std::size_t memory_bytes = 64ULL*1024*1024;
    std::uint64_t export_max_samples = 128000000, export_max_bytes = 8ULL*1024*1024*1024;
    unsigned burst_wait_tiles = 0;
    unsigned trials = 1;
    std::uint8_t signal_bit = 0;
    bool encrypt = true, now = false, fhss = false, fhss_center_aligned = false;
    bool performance_trace = false;
    bool assume_model_bounds = false;
    bool stats_noisy = false;
    std::string basis = "flat";
    double tap_delay_ms = 1.5, tap_doppler_hz = .3;
    // Common lower bound on total model-tail support for paired channel runs.
    double selective_tail_floor_seconds = 0;
    std::vector<double> clocks_ppm{0}, frequency_drifts{0};
    std::vector<std::int64_t> epoch_offsets{0};
    std::vector<std::string> command_argv;
    SampledFrontendConfig frontend{};
    PathEvolutionConfig path_evolution{};
    std::optional<std::string> base_oscillator,shift_oscillator,clock_sync_text,spread_preset,selective_perturbation;
    std::string selective_model = "dispersive-dft";
    double selective_scale_hz = 0, selective_center_hz = 0;
    std::optional<double> selective_modulation_rate_hz;
    std::optional<double> spread_delay_ms,spread_doppler_hz;
    double audio_error_seconds=clock_sync::default_audio_error_seconds,margin=1,nominal_if_hz=1500;
    bool audio_error_explicit=false,margin_explicit=false,phase_diffusion_explicit=false;
    unsigned symbol_burst_wait=0;
    std::int64_t truth_epoch_offset=0;
    double truth_frequency_hz=0,truth_frequency_drift=0;
};

bool path_evolution_active(const Options& o) {
    const auto& p=o.path_evolution;
    return p.ray_coherence_seconds>0||p.gaussian_doppler_width_hz>0||p.diurnal_phase_amplitude_radians>0||p.outage_start_seconds>=0;
}

bool configured_physics(const Options& o) {
    return o.base_oscillator||o.shift_oscillator||o.clock_sync_text||o.spread_preset||o.spread_delay_ms||o.spread_doppler_hz||
        o.selective_perturbation||o.selective_modulation_rate_hz||o.audio_error_explicit||o.margin_explicit||o.symbol_burst_wait||
        o.truth_frequency_hz!=0||o.truth_frequency_drift!=0;
}
struct ResolvedConfiguration {
    bool active=false,oscillators=false,shared_shift_clock=false,shift_active=false;
    std::string base_id="manual",shift_id="manual",spread_id="manual",perturbation_id="manual";
    double base_ppm=0,rf_ppm=0,phase_diffusion=0;
    double spread_delay_ms=0,spread_doppler_hz=0,slow_depth=0,fast_depth=0;
    double nominal_spread_delay_ms=0,nominal_spread_doppler_hz=0;
    double selective_rate_hz=0,selective_sideband_hz=0,selective_capture_allowance_hz=0;
    std::optional<clock_sync::Policy> clock;
    double arrival_center=0,arrival_half=0,clock_bound_ppm=0,frequency_bound_hz=0;
};
ResolvedConfiguration resolve_configuration(const Options& o) {
    ResolvedConfiguration r;r.active=configured_physics(o);r.oscillators=o.base_oscillator||o.shift_oscillator;
    r.phase_diffusion=o.phase_diffusion;
    if(o.channel=="hf"){r.spread_delay_ms=1.5;r.spread_doppler_hz=.3;r.slow_depth=r.fast_depth=.05;}
    if(r.oscillators){
        const auto base=tuning::parse_oscillator_preset(o.base_oscillator.value_or("gpsdo-xo"));
        const bool shared=o.shift_oscillator=="baseband-clock";
        const auto shift=shared?base:tuning::parse_oscillator_preset(o.shift_oscillator.value_or("gpsdo-xo"));
        if(!base.baseband_supported)throw std::invalid_argument("selected oscillator does not specify a Baseband clock");
        r.shift_active=o.rf_hz>0;r.shared_shift_clock=r.shift_active&&shared;
        r.base_id=base.id;r.shift_id=shared?"baseband-clock":shift.id;r.base_ppm=base.clock_error_ppm;
        r.rf_ppm=r.shift_active?shift.clock_error_ppm:0;
        if(!o.phase_diffusion_explicit)r.phase_diffusion=(r.shift_active&&!shared?
            std::hypot(base.phase_noise_degrees_per_sqrt_second,shift.phase_noise_degrees_per_sqrt_second):
            base.phase_noise_degrees_per_sqrt_second)*std::numbers::pi/180;
        const double rx_min=1-r.base_ppm*.5e-6;
        if(rx_min<=0)throw std::invalid_argument("base oscillator endpoint clock is nonpositive");
        r.clock_bound_ppm=r.base_ppm/rx_min;
        r.frequency_bound_hz=(o.rf_hz*r.rf_ppm+o.nominal_if_hz*r.base_ppm)*1e-6/rx_min;
    }
    if(o.spread_preset){const auto p=tuning::parse_spread_preset(*o.spread_preset);r.spread_id=p.id;
        r.spread_delay_ms=p.delay_ms*o.margin;r.spread_doppler_hz=p.doppler_hz*o.margin;
        r.slow_depth=r.fast_depth=p.default_perturbation?.65:0;r.perturbation_id=p.default_perturbation?"each-65":"off";}
    // Preset dimensions are nominal and margin is applied once. Keep legacy
    // --channel hf's already-effective 1.5ms/.3Hz unchanged when no override
    // is selected. With overrides, that fallback is the nominal base instead.
    if(o.spread_preset){const auto p=tuning::parse_spread_preset(*o.spread_preset);
        r.nominal_spread_delay_ms=p.delay_ms;r.nominal_spread_doppler_hz=p.doppler_hz;
    }else{r.nominal_spread_delay_ms=r.spread_delay_ms;r.nominal_spread_doppler_hz=r.spread_doppler_hz;}
    if(o.spread_delay_ms||o.spread_doppler_hz){
        for(const auto value:{o.spread_delay_ms,o.spread_doppler_hz})
            if(value&&(!std::isfinite(*value)||*value<0))throw std::invalid_argument("physical spread overrides must be finite and nonnegative");
        if(o.spread_delay_ms)r.nominal_spread_delay_ms=*o.spread_delay_ms;
        if(o.spread_doppler_hz)r.nominal_spread_doppler_hz=*o.spread_doppler_hz;
        r.spread_delay_ms=r.nominal_spread_delay_ms*o.margin;
        r.spread_doppler_hz=r.nominal_spread_doppler_hz*o.margin;
    }
    if(!std::isfinite(r.spread_delay_ms)||!std::isfinite(r.spread_doppler_hz))
        throw std::invalid_argument("effective physical spread exceeds numeric range");
    if(o.selective_perturbation){
        const auto& text=*o.selective_perturbation;r.perturbation_id=text;r.slow_depth=r.fast_depth=0;
        bool found=text=="off";
        for(const auto* kind:{"slow","fast","each"})for(const auto depth:{1,5,10,25,65})
            if(text==std::string(kind)+"-"+std::to_string(depth)){
                r.slow_depth=std::string_view(kind)!="fast"?depth/100.:0;
                r.fast_depth=std::string_view(kind)!="slow"?depth/100.:0;found=true;
            }
        if(!found)throw std::invalid_argument("selective perturbation must be off or slow/fast/each-N (1,5,10,25,65)");
    }
    r.selective_rate_hz=o.selective_modulation_rate_hz.value_or(r.spread_doppler_hz);
    if(!std::isfinite(r.selective_rate_hz)||r.selective_rate_hz<0)
        throw std::invalid_argument("selective modulation rate must be finite and nonnegative");
    // The gains add their sinusoids; the outermost active sideband is the
    // maximum component rate, not the sum of the slow and fast rates.
    r.selective_sideband_hz=r.selective_rate_hz*(r.fast_depth>0?1:r.slow_depth>0?.1:0);
    // Keep default capture acceptance identical to historical replays, which
    // conservatively reserved D even for a slow-only component.
    r.selective_capture_allowance_hz=o.selective_modulation_rate_hz?r.selective_sideband_hz:
        (r.fast_depth>0||r.slow_depth>0?r.spread_doppler_hz:0);
    if(r.active){
        const auto selected=o.clock_sync_text?clock_sync::parse(*o.clock_sync_text):std::optional<clock_sync::Policy>{};
        r.clock=clock_sync::effective_policy(selected,o.fhss);
        if(r.clock){r.arrival_center=r.clock->offset_seconds;
            r.arrival_half=r.clock->half_window_seconds()+o.audio_error_seconds;}
    }
    if(o.arrival_prior_seconds){
        const auto [low,high]=*o.arrival_prior_seconds;
        r.arrival_center=std::midpoint(low,high);
        r.arrival_half=high/2-low/2;
    }
    return r;
}
std::array<double,2> initial_arrival_bounds(const Options& o,const ResolvedConfiguration& r) {
    if(o.arrival_prior_seconds)return *o.arrival_prior_seconds;
    if(r.active)return {r.arrival_center-r.arrival_half,r.arrival_center+r.arrival_half};
    return {o.start_low,o.start_high};
}
std::string channel_name(const Options& o) {
    return o.spread_preset?std::string(tuning::parse_spread_preset(*o.spread_preset).id):o.channel;
}

double number(std::string_view s, const char* name) {
    std::size_t n = 0;
    const auto value = std::stod(std::string(s), &n);
    if (n != s.size() || !std::isfinite(value))
        throw std::invalid_argument(std::string("invalid ") + name);
    return value;
}
std::uint64_t integer(std::string_view s, const char* name) {
    if(s.empty() || s.front()=='-' || s.front()=='+') throw std::invalid_argument(std::string("invalid ")+name);
    std::size_t n = 0;
    const auto value = std::stoull(std::string(s), &n);
    if (n != s.size()) throw std::invalid_argument(std::string("invalid ") + name);
    return value;
}
template<class T> T bounded_integer(std::string_view s,const char* name) {
    const auto v=integer(s,name);
    if(v>std::numeric_limits<T>::max())throw std::invalid_argument(std::string("out-of-range ")+name);
    return static_cast<T>(v);
}
std::size_t mib(std::string_view s,const char* name) {
    const auto n=integer(s,name);
    if(n==0||n>std::numeric_limits<std::size_t>::max()/(1024*1024))throw std::invalid_argument(std::string("out-of-range ")+name);
    return static_cast<std::size_t>(n)*1024*1024;
}
std::int64_t signed_integer(std::string_view s,const char* name) {
    std::size_t n=0; const auto v=std::stoll(std::string(s),&n);
    if(n!=s.size())throw std::invalid_argument(std::string("invalid ")+name);
    return v;
}
template<class T> std::vector<T> list(std::string_view input, const char* name,std::size_t maximum=1000) {
    if(input.empty() || input.back()==',')throw std::invalid_argument(std::string("invalid ")+name+" list");
    std::vector<T> out;
    std::stringstream stream{std::string(input)};
    std::string part;
    while(std::getline(stream,part,',')) {
        if(part.empty()) throw std::invalid_argument(std::string("empty ")+name+" list item");
        if constexpr(std::is_same_v<T,std::int64_t>) out.push_back(signed_integer(part,name));
        else if constexpr(std::is_integral_v<T>) out.push_back(static_cast<T>(integer(part,name)));
        else out.push_back(number(part,name));
    }
    if(out.empty()||out.size()>maximum) throw std::invalid_argument(std::string("invalid ")+name+" list");
    return out;
}
unsigned parse_whole_bit_burst(std::string_view value) {
    if(value=="off")return 0;
    for(const auto wait:{1U,2U,3U,5U,10U})if(value==std::to_string(wait))return wait;
    throw Error("Symbol_Burst accepts off, 1, 2, 3, 5 or 10 symbols");
}
Options parse(int argc, char** argv) {
    Options o;
    for(int i=0;i<argc;++i)o.command_argv.emplace_back(argv[i]);
    for (int i = 1; i < argc; ++i) {
        const std::string_view key(argv[i]);
        if (key == "--help" || key == "-h") {
            std::cout << R"HELP(triad_long --mode configure|plan|run|export|export-source-window|export-sampled|export-stats|export-group-stats [options]
  --seconds S                 Requested active one-bit span (8..315576000 s, ten Julian years); rounded to complete private tiles.
  --rate HZ --added N --sample-rate HZ --rf-hz HZ
  --sample-rate-denominator N Reciprocal-integer sampling: N>1 requires --sample-rate 1; Fs=1/N Hz.
                              Rates below64 Hz require FHSS/dropout/frontend/FM/white-PM off.
                              Constant offsets, Wiener phase, two-ray spread and perturbation remain modeled.
  --base-oscillator ID --shift-oscillator ID|baseband-clock
                              Existing presets; unspecified partner=gpsdo-xo. Shared mode uses
                              one clock per endpoint for RF and audio; TX and RX remain independent.
  --clock-sync GPS_10ms-1ms_region-0ms_offset | default
  --audio-error-ms MS --margin X --nominal-if-hz HZ
                              RF/IF values multiply RF/audio oscillator errors in zero-centered I/Q.
                              RF 0 disables the shift oscillator; IF still models conversion-clock error.
                              Sampling ppm is constant; stochastic sample-clock jitter is not modeled.
  --arrival-prior-seconds MIN MAX
                              Explicit uniform physical initial-arrival prior, independent of --start.
                              Overrides clock/audio initial timing only; oscillator draws are unchanged.
  --spread-preset ID --selective-perturbation off|slow-N|fast-N|each-N
  --selective-model legacy-allpass|dispersive-dft
  --selective-scale-hz HZ --selective-center-hz HZ
                              Dispersive DFT is an explicit dispersive stress model:
                              scale defaults to carrier spacing; center is a fixed channel frame,
                              never recentered from hidden oscillator truth. At .00075-Hz scale,
                              branch memory extends about 889 seconds; this is not millisecond ray delay.
  --selective-tail-floor-seconds S
                              Common minimum total selective-model tail for paired runs.
  --selective-modulation-rate-hz HZ
                              Effective physical fast AM rate; slow=.1*rate. Default inherits
                              effective ray Doppler. Explicit 0 bypasses AM; margin is not reapplied.
  --spread-delay-ms MS --spread-doppler-hz HZ
                              Optional physical nominal overrides; preset base, then margin once.
                              Receiver --tap-* basis remains independently selected.
  --symbol-burst off|1|2|3|5|10  Whole-bit UTC calendar; no waits inside this one-bit source.
  --truth-epoch-offset N --truth-frequency-hz HZ --truth-frequency-drift HZ/S
                              Configured physical truth is independent of the public acquisition bank.
  --mode configure            Resolve physical bounds, capture feasibility and search/calendar estimates only.
  --mode export-source-window Export an absolute-coordinate source-only window; no channel is applied.
  --source-window-seconds S   Required window length for export-source-window.
  --source-first-sample N     Absolute first sample (default: active payload start).
  --source-window-center      Center in active payload; mutually exclusive with --source-first-sample.
  --performance-trace yes|no  Mode run: record bounded setup/read/push/finish stage timings and work counts.
  --cn0 DB-HZ                 Total nominal three-carrier C/N0; N0 normalization uses power1, not faded output.
  --channel none|hf           hf: shared two-ray1.5ms/.3Hz plus slow/fast5% perturbations.
  --basis flat|two-ray        two-ray searches flat and coupled two-ray models; per-carrier fits unsupported.
  --tap-delay-ms MS --tap-doppler-hz HZ
  --start MIN MAX STEP --frequency MIN MAX STEP
  --timing-loss-db DB         Timing-grid recommendation loss (default0.5; 0.000001..60 dB).
                              Random-chip triplet/RRC approximation; does not alter the public bank.
  --clock-ppm LIST --epoch-offsets LIST --frequency-drift LIST
                              Comma lists; all bank hypotheses are fixed before observed noise.
  --block N | --block-seconds S   Coherent score-block length; streaming QR does not buffer whole blocks.
  --chunk N --threads N --memory-mib N
  --phase-diffusion RAD/SQRT(S)   Relative Wiener coefficient; default0.5 degrees/sqrt(s) expressed in radians.
  --white-fm HZ*SQRT(S) --random-walk-fm HZ/SQRT(S)
  --flicker-fm RMS-HZ --flicker-tau MIN MAX --flicker-bands N --white-pm RMS-RAD
  --ray-coherence-seconds S    Independent stationary complex OU ray gains; 0 disables.
  --ray-gaussian-width-hz B    Independent stationary complex FIR ray gains; Gaussian target width B=2*sigma.
                              0 disables; mutually exclusive with positive OU coherence. Ray-center separation is independent.
  --diurnal-phase-radians A --diurnal-period-seconds S   Sinusoidal common path phase; A=0 disables.
  --outage-seconds START END --outage-gain G --outage-ramp-seconds S
                              Physical-path attenuation, separate from planner --outage; -1 -1 disables.
                              Physical time excludes capture lead; OU spectrum requires sampling refinement.
  --dropout-ms MS --dropout-start-ms MS --dropout-every-dwells N
  --missed-dwells LIST         Sorted unique nonnegative receiver dwell indices (at most4096); requires --fhss.
                              Erases received I/Q after channel/noise/frontend; physical state continues.
  --minimum-observed-fraction F  Run-mode coverage gate in (0,1], default0.9; does not alter evidence thresholds.
  --fhss                      Source envelope only; no physical RF-retuning claim.
  --fhss-center-aligned       Opt-in whole-cell centers on the FHSS dwell grid; requires --fhss.
  --burst-wait-tiles N         Silent tile waits within this one-bit v3 observation.
  --seed N --epoch UTC | --now --trials N --signal-bit 0|1 --no-encrypt
  --key-seed N --channel-seed N   Key/channel seeds (default to --seed).
  --oscillator-seed N --noise-seed N --frontend-seed N --path-seed N
                              Independent sample-stream roots; default to the resolved channel seed.
  --control signal|null|wrong-key
  --correct P --false P --horizon S --attempt-spacing S --duration-alternatives N
                              Count every separately attempted duration in the operational false-admission budget.
  --outage FRACTION --retained-energy FRACTION --wrong-projection FRACTION
  --minimum-dwells N --assume-model-bounds
                              Planner bounds are conditional on caller certificates, not empirical recommendations.
  --output DIRECTORY          Fresh export directory for exported arrays.
  --mode export-sampled       Export full complex128 public bases and actual noisy/frontend samples.
  --stats-noisy               export-stats also writes matched actual noisy projections.
  --mode export-group-stats   Export timing-group Grams with independent local residual dimensions.
  --dac-bits N --adc-bits N    0=exact bypass, or 2..24 signed component quantization bits.
  --dac-enob B --adc-enob B    Synthetic Gaussian converter-error effective resolution (0=nominal).
  --dac-full-scale V --adc-full-scale V --dac-gain G --adc-gain G
  --dac-dither-lsb RMS --adc-dither-lsb RMS   Added TPDF dither before quantization.
  --rx-lowpass-hz HZ          Continuous one-pole analog approximation after noise/interference.
  --interference-tone AMP HZ  Receiver-coordinate tone; no fixed-RF retuning claim.
  --tone-every-dwells N --interference-impulse AMP RATE-HZ
  --rx-gain-step TIME FACTOR --overload-recovery-seconds S
                              Experimental actual-sample path; Gaussian projection/analytic null disabled.
  --export-max-samples N       At most128000000 raw samples; default128000000.
  --export-max-mib N           Explicit disk budget, default8192MiB; chunk RAM follows --memory-mib.
Examples:
  triad_long --mode export --seconds 16 --channel none --phase-diffusion 0 --output /tmp/triad-flat
  triad_long --mode run --seconds 60 --cn0 8 --channel hf --basis two-ray --block-seconds 1.6
  triad_long --mode plan --cn0 -10 --correct .9 --false .01 --horizon 43200 --block-seconds 1.6
)HELP";
            std::exit(0);
        }
        auto need = [&]() -> std::string_view {
            if (++i >= argc) throw std::invalid_argument(std::string("missing value for ") + std::string(key));
            return argv[i];
        };
        if (key == "--mode") o.mode = need();
        else if (key == "--performance-trace") {
            const auto value=need();
            if(value=="yes")o.performance_trace=true;
            else if(value=="no")o.performance_trace=false;
            else throw std::invalid_argument("performance trace must be yes or no");
        }
        else if (key == "--seconds") o.seconds = number(need(), "seconds");
        else if (key == "--cn0") o.cn0 = number(need(), "C/N0");
        else if (key == "--rate") o.rate = number(need(), "Rate");
        else if (key == "--rf-hz") o.rf_hz = number(need(), "RF frequency");
        else if (key == "--base-oscillator") o.base_oscillator=std::string(need());
        else if (key == "--shift-oscillator") o.shift_oscillator=std::string(need());
        else if (key == "--clock-sync") o.clock_sync_text=std::string(need());
        else if (key == "--audio-error-ms") {o.audio_error_seconds=number(need(),"audio error milliseconds")*.001;o.audio_error_explicit=true;}
        else if (key == "--margin") {o.margin=number(need(),"search/spread margin");o.margin_explicit=true;}
        else if (key == "--nominal-if-hz") o.nominal_if_hz=number(need(),"nominal conversion IF");
        else if (key == "--arrival-prior-seconds") {
            const auto low=number(need(),"physical arrival minimum");
            const auto high=number(need(),"physical arrival maximum");
            o.arrival_prior_seconds=std::array<double,2>{low,high};
        }
        else if (key == "--spread-preset") o.spread_preset=std::string(need());
        else if (key == "--spread-delay-ms") o.spread_delay_ms=number(need(),"physical spread delay");
        else if (key == "--spread-doppler-hz") o.spread_doppler_hz=number(need(),"physical spread Doppler");
        else if (key == "--selective-perturbation") o.selective_perturbation=std::string(need());
        else if (key == "--selective-model") o.selective_model=std::string(need());
        else if (key == "--selective-scale-hz") o.selective_scale_hz=number(need(),"selective correlation scale");
        else if (key == "--selective-center-hz") o.selective_center_hz=number(need(),"selective frequency frame");
        else if (key == "--selective-modulation-rate-hz") o.selective_modulation_rate_hz=number(need(),"selective modulation rate");
        else if (key == "--selective-tail-floor-seconds") o.selective_tail_floor_seconds=number(need(),"minimum selective model tail");
        else if (key == "--symbol-burst") o.symbol_burst_wait=parse_whole_bit_burst(need());
        else if (key == "--truth-epoch-offset") o.truth_epoch_offset=signed_integer(need(),"truth epoch offset");
        else if (key == "--truth-frequency-hz") o.truth_frequency_hz=number(need(),"independent truth frequency");
        else if (key == "--truth-frequency-drift") o.truth_frequency_drift=number(need(),"independent truth frequency drift");
        else if (key == "--correct") o.correct = number(need(), "correct probability");
        else if (key == "--false") o.false_probability = number(need(), "false probability");
        else if (key == "--horizon") o.horizon = number(need(), "horizon");
        else if (key == "--attempt-spacing") o.attempt_spacing = number(need(), "attempt spacing");
        else if (key == "--duration-alternatives") o.duration_alternatives=integer(need(),"duration alternatives");
        else if (key == "--outage") o.outage = number(need(), "outage fraction");
        else if (key == "--retained-energy") o.retained_energy = number(need(), "retained energy fraction");
        else if (key == "--wrong-projection") o.wrong_projection = number(need(), "wrong projection fraction");
        else if (key == "--clock-ppm") o.clocks_ppm=list<double>(need(),"clock ppm");
        else if (key == "--epoch-offsets") o.epoch_offsets=list<std::int64_t>(need(),"epoch offsets");
        else if (key == "--frequency-drift") o.frequency_drifts=list<double>(need(),"frequency drift");
        else if (key == "--basis") o.basis=need();
        else if (key == "--tap-delay-ms") o.tap_delay_ms=number(need(),"tap delay");
        else if (key == "--tap-doppler-hz") o.tap_doppler_hz=number(need(),"tap Doppler");
        else if (key == "--start") { o.start_low=number(need(), "start minimum"); o.start_high=number(need(), "start maximum"); o.start_step=number(need(), "start step"); }
        else if (key == "--frequency") { o.frequency_low=number(need(), "frequency minimum"); o.frequency_high=number(need(), "frequency maximum"); o.frequency_step=number(need(), "frequency step"); }
        else if (key == "--timing-loss-db") o.timing_loss_db=number(need(),"timing recommendation loss");
        else if (key == "--sample-rate") o.sample_rate = number(need(), "sample rate");
        else if (key == "--source-window-seconds") o.source_window_seconds=number(need(),"source window seconds");
        else if (key == "--source-first-sample") o.source_first_sample=integer(need(),"source first sample");
        else if (key == "--source-window-center") o.source_window_center=true;
        else if (key == "--sample-rate-denominator") o.sample_rate_denominator=bounded_integer<std::uint32_t>(need(),"sample rate denominator");
        else if (key == "--phase-diffusion") {o.phase_diffusion=number(need(),"Wiener phase diffusion");o.phase_diffusion_explicit=true;}
        else if (key == "--white-fm") o.white_fm=number(need(),"white FM");
        else if (key == "--random-walk-fm") o.random_walk_fm=number(need(),"random walk FM");
        else if (key == "--flicker-fm") o.flicker_fm=number(need(),"finite-band flicker FM");
        else if (key == "--flicker-tau") {o.flicker_min_tau=number(need(),"minimum flicker tau");o.flicker_max_tau=number(need(),"maximum flicker tau");}
        else if (key == "--flicker-bands") o.flicker_bands=bounded_integer<unsigned>(need(),"flicker bands");
        else if (key == "--white-pm") o.white_pm=number(need(),"white PM");
        else if (key == "--ray-coherence-seconds") o.path_evolution.ray_coherence_seconds=number(need(),"ray coherence");
        else if (key == "--ray-gaussian-width-hz") o.path_evolution.gaussian_doppler_width_hz=number(need(),"Gaussian ray Doppler width");
        else if (key == "--diurnal-phase-radians") o.path_evolution.diurnal_phase_amplitude_radians=number(need(),"diurnal phase amplitude");
        else if (key == "--diurnal-period-seconds") o.path_evolution.diurnal_period_seconds=number(need(),"diurnal period");
        else if (key == "--outage-seconds") {o.path_evolution.outage_start_seconds=number(need(),"path outage start");o.path_evolution.outage_end_seconds=number(need(),"path outage end");}
        else if (key == "--outage-gain") o.path_evolution.outage_gain=number(need(),"path outage gain");
        else if (key == "--outage-ramp-seconds") o.path_evolution.outage_ramp_seconds=number(need(),"path outage ramp");
        else if (key == "--dropout-ms") o.dropout_ms=number(need(),"dropout milliseconds");
        else if (key == "--dropout-start-ms") o.dropout_start_ms=number(need(),"dropout start milliseconds");
        else if (key == "--dropout-every-dwells") o.dropout_every_dwells=bounded_integer<std::size_t>(need(),"dropout period");
        else if (key == "--missed-dwells") o.missed_dwells=list<std::uint64_t>(need(),"missed receiver dwell",4096);
        else if (key == "--minimum-observed-fraction") o.minimum_observed_fraction=number(need(),"minimum observed fraction");
        else if (key == "--channel") o.channel = need();
        else if (key == "--control") o.control = need();
        else if (key == "--output") o.output = need();
        else if (key == "--epoch") o.epoch = integer(need(), "epoch");
        else if (key == "--seed") o.seed = integer(need(), "seed");
        else if (key == "--key-seed") o.key_seed = integer(need(), "key seed");
        else if (key == "--channel-seed") o.channel_seed = integer(need(), "channel seed");
        else if (key == "--oscillator-seed") o.oscillator_seed = integer(need(), "oscillator seed");
        else if (key == "--noise-seed") o.noise_seed = integer(need(), "noise seed");
        else if (key == "--frontend-seed") o.frontend_seed = integer(need(), "frontend seed");
        else if (key == "--path-seed") o.path_seed = integer(need(), "path seed");
        else if (key == "--trials") o.trials = bounded_integer<unsigned>(need(),"trials");
        else if (key == "--threads") o.threads = bounded_integer<unsigned>(need(),"threads");
        else if (key == "--chunk") o.chunk = bounded_integer<std::size_t>(need(),"chunk");
        else if (key == "--block-seconds") o.block_seconds=number(need(),"block seconds");
        else if (key == "--memory-mib") o.memory_bytes=mib(need(),"memory MiB");
        else if (key == "--export-max-samples") o.export_max_samples=integer(need(),"export sample limit");
        else if (key == "--export-max-mib") o.export_max_bytes=mib(need(),"export MiB limit");
        else if (key == "--burst-wait-tiles") o.burst_wait_tiles=bounded_integer<unsigned>(need(),"burst wait tiles");
        else if (key == "--block") o.block = bounded_integer<std::size_t>(need(),"block");
        else if (key == "--added") o.added = bounded_integer<unsigned>(need(),"Added Symbols");
        else if (key == "--minimum-dwells") o.minimum_dwells = integer(need(), "minimum dwells");
        else if (key == "--signal-bit") o.signal_bit = bounded_integer<std::uint8_t>(need(), "signal bit");
        else if (key == "--no-encrypt") o.encrypt = false;
        else if (key == "--fhss") o.fhss = true;
        else if (key == "--fhss-center-aligned") o.fhss_center_aligned = true;
        else if (key == "--stats-noisy") o.stats_noisy = true;
        else if (key == "--dac-bits") o.frontend.dac.nominal_bits=bounded_integer<unsigned>(need(),"DAC bits");
        else if (key == "--adc-bits") o.frontend.adc.nominal_bits=bounded_integer<unsigned>(need(),"ADC bits");
        else if (key == "--dac-enob") o.frontend.dac.effective_bits=number(need(),"DAC effective bits");
        else if (key == "--adc-enob") o.frontend.adc.effective_bits=number(need(),"ADC effective bits");
        else if (key == "--dac-full-scale") o.frontend.dac.full_scale=number(need(),"DAC full scale");
        else if (key == "--adc-full-scale") o.frontend.adc.full_scale=number(need(),"ADC full scale");
        else if (key == "--dac-gain") o.frontend.dac.gain=number(need(),"DAC gain");
        else if (key == "--adc-gain") o.frontend.adc.gain=number(need(),"ADC gain");
        else if (key == "--dac-dither-lsb") o.frontend.dac.dither_rms_lsb=number(need(),"DAC dither");
        else if (key == "--adc-dither-lsb") o.frontend.adc.dither_rms_lsb=number(need(),"ADC dither");
        else if (key == "--rx-lowpass-hz") o.frontend.lowpass_hz=number(need(),"frontend lowpass");
        else if (key == "--interference-tone") {o.frontend.tone_amplitude=number(need(),"tone amplitude");o.frontend.tone_frequency_hz=number(need(),"tone frequency");}
        else if (key == "--tone-every-dwells") o.frontend.tone_every_n_dwells=bounded_integer<std::size_t>(need(),"tone dwell period");
        else if (key == "--interference-impulse") {o.frontend.impulse_amplitude=number(need(),"impulse amplitude");o.frontend.impulse_rate_hz=number(need(),"impulse event rate");}
        else if (key == "--rx-gain-step") {o.frontend.gain_step_seconds=number(need(),"gain-step time");o.frontend.gain_step_factor=number(need(),"gain-step factor");}
        else if (key == "--overload-recovery-seconds") o.frontend.overload_recovery_seconds=number(need(),"overload recovery");
        else if (key == "--assume-model-bounds") o.assume_model_bounds = true;
        else if (key == "--now") o.now = true;
        else throw std::invalid_argument(std::string("unknown option: ") + std::string(key));
    }
    if (o.now) o.epoch = static_cast<std::uint64_t>(std::time(nullptr));
    if(o.mode=="resolve")o.mode="configure";
    if (o.mode != "configure" && o.mode != "plan" && o.mode != "run" && o.mode != "export" && o.mode != "export-source-window" && o.mode != "export-sampled" && o.mode != "export-stats" && o.mode != "export-group-stats") throw std::invalid_argument("mode must be configure, plan, run, export, export-source-window, export-sampled, export-stats, or export-group-stats");
    if(o.mode!="export-source-window"&&(o.source_window_seconds!=0||o.source_first_sample||o.source_window_center))
        throw std::invalid_argument("source-window options require --mode export-source-window");
    if(o.performance_trace&&o.mode!="run")
        throw std::invalid_argument("--performance-trace yes is supported only by --mode run");
    if (o.channel != "none" && o.channel != "hf") throw std::invalid_argument("channel must be none or hf");
    if (o.selective_model != "legacy-allpass" && o.selective_model != "dispersive-dft")
        throw std::invalid_argument("selective model must be legacy-allpass or dispersive-dft");
    if (!std::isfinite(o.selective_scale_hz) || o.selective_scale_hz < 0 ||
        !std::isfinite(o.selective_center_hz) || o.selective_tail_floor_seconds < 0 ||
        !std::isfinite(o.selective_tail_floor_seconds))
        throw std::invalid_argument("selective scale/center and tail floor must be finite; scale and tail must be nonnegative");
    if (o.basis!="flat" && o.basis!="two-ray" && o.basis!="per-carrier")
        throw std::invalid_argument("basis must be flat, two-ray, or per-carrier");
    if (o.basis=="per-carrier")
        throw std::invalid_argument("per-carrier tap fits are not implemented; use the coupled shared-tap basis");
    if (o.control != "signal" && o.control != "null" && o.control != "wrong-key") throw std::invalid_argument("control must be signal, null, or wrong-key");
    if (!(o.seconds >= 8 && o.seconds <= 315576000.) ||
        !(o.rate > 0 && o.rate <= 30000000) || !(o.rf_hz >= 0 && o.rf_hz <= 10e9) ||
        !(o.sample_rate == 0 || (o.sample_rate >= 1 && o.sample_rate <= 10000000)) ||
        o.trials == 0 || o.trials > 100000 || o.threads == 0 || o.threads > 256 ||
        o.chunk == 0 || o.chunk > 1000000 || o.block == 0 || o.burst_wait_tiles>1000 ||
        o.signal_bit > 1 || o.added == 0 || o.added > 1000 || o.minimum_dwells < 20 || o.duration_alternatives==0)
        throw std::invalid_argument("invalid duration, trial count, resource setting, bit or pattern geometry");
    if(!o.sample_rate_denominator || (o.sample_rate_denominator>1 && o.sample_rate!=1))
        throw std::invalid_argument("sample rate denominator must be positive; denominator >1 requires explicit --sample-rate 1");
    if (!(o.correct > 0 && o.correct < 1 && o.false_probability > 0 && o.false_probability < 1 && o.horizon > 0 && o.attempt_spacing > 0))
        throw std::invalid_argument("probabilities must be in (0,1) and time inputs positive");
    if (!(o.start_step > 0 && o.frequency_step > 0 && o.start_low <= o.start_high && o.frequency_low <= o.frequency_high))
        throw std::invalid_argument("invalid acquisition grid");
    if(o.arrival_prior_seconds&&(*o.arrival_prior_seconds)[0]>(*o.arrival_prior_seconds)[1])
        throw std::invalid_argument("physical arrival prior endpoints must be ordered");
    if(!(o.minimum_observed_fraction>0&&o.minimum_observed_fraction<=1))
        throw std::invalid_argument("minimum observed fraction must be in (0,1]");
    if(!o.missed_dwells.empty()&&(!o.fhss||
        std::adjacent_find(o.missed_dwells.begin(),o.missed_dwells.end(),std::greater_equal<>{})!=o.missed_dwells.end()))
        throw std::invalid_argument("missed receiver dwells require --fhss and strictly ascending unique indices");
    if (!(o.timing_loss_db >= 1e-6 && o.timing_loss_db <= 60) || !std::isfinite(o.timing_loss_db))
        throw std::invalid_argument("timing recommendation loss must be finite and in [0.000001,60] dB");
    for(const auto x:{o.phase_diffusion,o.white_fm,o.random_walk_fm,o.flicker_fm,o.white_pm,
                      o.dropout_ms,o.dropout_start_ms})
        if(!std::isfinite(x)||x<0)throw std::invalid_argument("oscillator/erasure parameters must be finite and nonnegative");
    if(!(o.flicker_min_tau>0&&o.flicker_max_tau>=o.flicker_min_tau&&o.flicker_bands>=1&&o.flicker_bands<=64&&
         o.dropout_every_dwells>0&&o.dropout_start_ms+o.dropout_ms<=400))
        throw std::invalid_argument("invalid flicker band or dwell-erasure parameters");
    const auto& path=o.path_evolution;
    for(const auto x:{path.ray_coherence_seconds,path.gaussian_doppler_width_hz,path.diurnal_phase_amplitude_radians,
                      path.diurnal_period_seconds,path.outage_start_seconds,path.outage_end_seconds,
                      path.outage_gain,path.outage_ramp_seconds})
        if(!std::isfinite(x))throw std::invalid_argument("physical-path evolution parameters must be finite");
    if(!(path.ray_coherence_seconds>=0&&path.gaussian_doppler_width_hz>=0&&path.diurnal_phase_amplitude_radians>=0&&path.diurnal_period_seconds>0&&
         path.outage_gain>=0&&path.outage_gain<=1&&path.outage_ramp_seconds>=0))
        throw std::invalid_argument("invalid physical-path evolution");
    if(path.ray_coherence_seconds>0&&path.gaussian_doppler_width_hz>0)
        throw std::invalid_argument("OU ray coherence and Gaussian ray Doppler width are mutually exclusive");
    if(!std::isfinite(6*path.gaussian_doppler_width_hz))
        throw std::invalid_argument("Gaussian ray capture allowance exceeds numeric range");
    if(!((path.outage_start_seconds==-1&&path.outage_end_seconds==-1)||
         (path.outage_start_seconds>=0&&path.outage_end_seconds>path.outage_start_seconds&&
          path.outage_ramp_seconds<=(path.outage_end_seconds-path.outage_start_seconds)*.5)))
        throw std::invalid_argument("physical-path outage needs ordered endpoints and ramps within its interval");
    if(!std::isfinite(path.diurnal_phase_amplitude_radians/path.diurnal_period_seconds))
        throw std::invalid_argument("physical-path diurnal frequency excursion exceeds numeric range");
    if((o.block_seconds!= -1 && o.block_seconds<=0) || o.export_max_samples==0 || o.export_max_samples>128000000)
        throw std::invalid_argument("invalid block duration or export sample ceiling");
    if(o.tap_delay_ms<0||!std::isfinite(o.tap_doppler_hz))throw std::invalid_argument("invalid public tap model");
    for(const auto ppm:o.clocks_ppm)if(std::abs(ppm)>10000)throw std::invalid_argument("clock hypotheses exceed +/-10000ppm");
    if(o.seed>UINT64_MAX-(o.trials-1))throw std::invalid_argument("trial seed overflow");
    if(o.key_seed && *o.key_seed>UINT64_MAX-(o.trials-1))throw std::invalid_argument("key trial seed overflow");
    if(o.channel_seed && *o.channel_seed>UINT64_MAX-(o.trials-1))throw std::invalid_argument("channel trial seed overflow");
    if(o.oscillator_seed && *o.oscillator_seed>UINT64_MAX-(o.trials-1))throw std::invalid_argument("oscillator trial seed overflow");
    if(o.noise_seed && *o.noise_seed>UINT64_MAX-(o.trials-1))throw std::invalid_argument("noise trial seed overflow");
    if(o.frontend_seed && *o.frontend_seed>UINT64_MAX-(o.trials-1))throw std::invalid_argument("frontend trial seed overflow");
    if(o.path_seed && *o.path_seed>UINT64_MAX-(o.trials-1))throw std::invalid_argument("path trial seed overflow");
    for(const auto* c:{&o.frontend.dac,&o.frontend.adc})
        if(!(c->nominal_bits==0||(c->nominal_bits>=2&&c->nominal_bits<=24))||
           !(c->full_scale>0)||c->gain<0||c->dither_rms_lsb<0||c->effective_bits<0||
           (c->effective_bits!=0&&(!c->nominal_bits||c->effective_bits<1||c->effective_bits>c->nominal_bits))||
           (!c->nominal_bits&&c->dither_rms_lsb!=0))
            throw std::invalid_argument("invalid converter range, nominal/effective bits, gain or dither");
    if(o.frontend.lowpass_hz<0||o.frontend.tone_amplitude<0||o.frontend.impulse_amplitude<0||
       o.frontend.impulse_rate_hz<0||o.frontend.tone_every_n_dwells==0||
       !(o.frontend.gain_step_seconds==-1||o.frontend.gain_step_seconds>=0)||
       o.frontend.gain_step_factor<0||o.frontend.overload_recovery_seconds<0||
       (o.frontend.overload_recovery_seconds>0&&!o.frontend.adc.nominal_bits))
        throw std::invalid_argument("invalid sampled frontend configuration");
    if(o.mode=="plan" && o.frontend.active())
        throw std::invalid_argument("planner cannot represent the sampled frontend; use run or actual-sample export-stats");
    if(o.mode=="export-stats" && o.frontend.active() && !o.stats_noisy)
        throw std::invalid_argument("sampled frontend export-stats requires --stats-noisy; Gaussian projection simulation is unsupported");
    if(o.mode=="export" && o.frontend.active())
        throw std::invalid_argument("raw export cannot represent noise through the sampled frontend; use export-stats --stats-noisy");
    // These point-sampled low-rate models preserve their physical-second
    // parameters. Fast events and sample-rate-sensitive oscillator spectra
    // need a separately validated high-rate integration/decimation path.
    if(o.sample_rate>0 && o.sample_rate/o.sample_rate_denominator<64 &&
       (o.fhss || o.dropout_ms>0 || o.frontend.active() || o.white_fm>0 ||
        o.random_walk_fm>0 || o.flicker_fm>0 || o.white_pm>0))
        throw std::invalid_argument("sub-64-Hz simulation requires FHSS, dropout, sampled frontend, white/flicker/random-walk FM and white-PM off; use at least 64 samples/s for those effects");
    clock_sync::validate_audio_error(o.audio_error_seconds);
    if(o.margin<1||o.margin>1000||o.nominal_if_hz<0||o.nominal_if_hz>1e9)
        throw std::invalid_argument("margin must be 1..1000 and nominal IF 0..1e9 Hz");
    if(o.symbol_burst_wait&&o.burst_wait_tiles)throw std::invalid_argument("whole-bit --symbol-burst cannot be combined with internal --burst-wait-tiles");
    (void)resolve_configuration(o);
    if(o.mode=="plan"&&configured_physics(o))
        throw std::invalid_argument("duration planning for resolved GUI presets is unsupported; inspect --mode configure and supply independently justified manual planner bounds");
    if(o.mode=="plan"&&path_evolution_active(o))
        throw std::invalid_argument("duration planning for physical-path evolution is unsupported; supply independently justified planner bounds separately");
    return o;
}

Crypto key_for(std::uint64_t seed, std::uint64_t trial) {
    std::seed_seq seq{static_cast<std::uint32_t>(seed),static_cast<std::uint32_t>(seed>>32),
                      static_cast<std::uint32_t>(trial),static_cast<std::uint32_t>(trial>>32)};
    std::mt19937 engine(seq);
    std::array<std::uint8_t,32> material{};
    for (auto& b : material) b = static_cast<std::uint8_t>(engine());
    return Crypto(material);
}

std::vector<double> grid(double low, double high, double step) {
    std::vector<double> out;
    const auto count = std::ceil((high-low)/step);
    if (!std::isfinite(count) || count > 100000) throw std::invalid_argument("acquisition grid exceeds 100000 values");
    for (std::size_t i=0;i<=static_cast<std::size_t>(count);++i) {
        const auto x=low+static_cast<double>(i)*step;
        if (x<=high+step*1e-9) out.push_back(x);
    }
    if (out.empty()) out.push_back(low);
    return out;
}

std::uint64_t offset_epoch(std::uint64_t,std::int64_t);
std::size_t block_samples(const Options&,const Format&);
struct UtcSample {std::uint64_t epoch=0;std::uint32_t phase=0;};
std::uint64_t ceil_utc_30(std::uint64_t epoch) {
    const auto add=(30-epoch%30)%30;
    if(epoch>UINT64_MAX-add)throw std::invalid_argument("whole-bit burst UTC grid overflows");
    return epoch+add;
}
UtcSample add_utc_samples(UtcSample at,std::uint64_t samples,const Format& format) {
    if(format.sample_rate_denominator>1){
        // The supported sub-Hz form has an integer number of physical seconds
        // per sample, so no rounded floating timestamp enters the UTC epoch.
        if(format.sample_rate!=1 || at.phase || samples>(UINT64_MAX-at.epoch)/format.sample_rate_denominator)
            throw std::invalid_argument("whole-bit burst UTC support overflows");
        return {at.epoch+samples*format.sample_rate_denominator,0};
    }
    const auto rate=format.sample_rate;
    const auto fraction=at.phase+samples%rate;
    const auto whole=samples/rate+fraction/rate;
    if(at.epoch>UINT64_MAX-whole)throw std::invalid_argument("whole-bit burst UTC support overflows");
    return {at.epoch+whole,static_cast<std::uint32_t>(fraction%rate)};
}
struct WholeBitCalendar {UtcSample support_begin,payload_begin,payload_end,support_end,next_support;std::uint64_t useful_samples=0;};
WholeBitCalendar whole_bit_calendar(const Options& o,const Format& f,const Geometry& g) {
    WholeBitCalendar c;c.support_begin={o.symbol_burst_wait?ceil_utc_30(o.epoch):o.epoch,0};
    c.useful_samples=g.source_samples-2*g.calendar_origin_sample;
    c.payload_begin=add_utc_samples(c.support_begin,g.calendar_origin_sample,f);
    c.payload_end=add_utc_samples(c.payload_begin,c.useful_samples,f);
    c.support_end=add_utc_samples(c.support_begin,g.source_samples,f);
    if(o.symbol_burst_wait){
        if(c.useful_samples>UINT64_MAX/o.symbol_burst_wait)throw std::invalid_argument("whole-bit burst wait overflows");
        auto next=add_utc_samples(c.support_end,c.useful_samples*o.symbol_burst_wait,f);
        if(c.support_begin.epoch>UINT64_MAX-60)throw std::invalid_argument("whole-bit burst UTC support overflows");
        const UtcSample minimum{c.support_begin.epoch+60,c.support_begin.phase};
        if(next.epoch<minimum.epoch||(next.epoch==minimum.epoch&&next.phase<minimum.phase))next=minimum;
        if(next.phase){if(next.epoch==UINT64_MAX)throw std::invalid_argument("whole-bit burst next support overflows");++next.epoch;}
        c.next_support={ceil_utc_30(next.epoch),0};
    }else c.next_support=c.support_end;
    return c;
}

Format format_for(const Options& o) {
    Format f;
    f.inner_rate_hz=o.rate; f.added_symbols=o.added; f.added_interleave=true;
    const auto fs=o.sample_rate ? o.sample_rate : std::max(960.,std::ceil(4.2*(f.inner_rate_hz/6*o.added)));
    if(!std::isfinite(fs)||fs!=std::floor(fs)||fs>std::numeric_limits<std::uint32_t>::max())throw std::invalid_argument("sample rate must be an integer in uint32 range");
    f.sample_rate=static_cast<std::uint32_t>(fs);
    f.sample_rate_denominator=o.sample_rate_denominator;
    f.bit_seconds=o.seconds; f.minimum_inner_coefficients=64;
    f.fhss=o.fhss; f.dwell_seconds=.4; f.quiet_seconds=o.fhss?.055:0;
    f.fhss_center_aligned=o.fhss_center_aligned;
    f.ramp_seconds=o.fhss?(2./3600.):.02; f.margin_seconds=.005;
    f.encrypt=o.encrypt; f.epoch=o.epoch; f.symbol_ordinal=0;f.burst_wait_tiles=o.burst_wait_tiles;
    if(o.symbol_burst_wait){const auto shape=geometry(f);f.epoch=whole_bit_calendar(o,f,shape).payload_begin.epoch;}
    return f;
}
Format transmitter_format(const Options& o,const Format& receiver) {
    auto tx=receiver;tx.epoch=offset_epoch(receiver.epoch,o.truth_epoch_offset);return tx;
}

double configuration_capture_margin(const Options& o,const Format& f,const Geometry& g) {
    const auto r=resolve_configuration(o);
    const double base=r.oscillators?r.base_ppm:0;
    const double rx_min=1-base*.5e-6,tx_max=1+base*.5e-6;
    const double public_frequency=std::max(std::abs(o.frequency_low),std::abs(o.frequency_high));
    const double oscillator=std::max(r.frequency_bound_hz+std::abs(o.truth_frequency_hz),public_frequency);
    const double spread=r.spread_doppler_hz+r.selective_capture_allowance_hz;
    const double drift=std::abs(o.truth_frequency_drift)*(g.bit_seconds+2*r.arrival_half+2);
    const double diurnal=std::abs(o.path_evolution.diurnal_phase_amplitude_radians)/o.path_evolution.diurnal_period_seconds;
    return effective_sample_rate(f)*rx_min-g.occupied_bandwidth_hz*tx_max-2*(oscillator+spread+drift+diurnal)
        -6*o.path_evolution.gaussian_doppler_width_hz;
}

void json_string(std::ostream& out,std::string_view value) {
    static constexpr char hex[]="0123456789abcdef";
    out<<'"';
    for(const unsigned char ch:value){
        if(ch=='"'||ch=='\\')out<<'\\'<<static_cast<char>(ch);
        else if(ch<32)out<<"\\u00"<<hex[ch>>4]<<hex[ch&15];
        else out<<static_cast<char>(ch);
    }
    out<<'"';
}
void json_spread_inputs(std::ostream& out,const Options& o,const ResolvedConfiguration& r) {
    out<<"{\"selected_preset\":";json_string(out,r.spread_id);
    out<<",\"fallback_channel\":";json_string(out,o.channel);
    out<<",\"delay_ms_override_nominal\":";
    if(o.spread_delay_ms)out<<*o.spread_delay_ms;else out<<"null";
    out<<",\"doppler_hz_override_nominal\":";
    if(o.spread_doppler_hz)out<<*o.spread_doppler_hz;else out<<"null";
    out<<",\"resolved_nominal_delay_ms\":"<<r.nominal_spread_delay_ms
       <<",\"resolved_nominal_doppler_hz\":"<<r.nominal_spread_doppler_hz
       <<",\"applied_margin\":"<<((o.spread_preset||o.spread_delay_ms||o.spread_doppler_hz)?o.margin:1)
       <<",\"effective_delay_ms\":"<<r.spread_delay_ms<<",\"effective_doppler_hz\":"<<r.spread_doppler_hz
       <<",\"semantics\":\"selected preset supplies nominal dimensions; explicit physical overrides replace their own nominal dimension before one margin multiplication; without preset, legacy channel values supply the base, and remain already effective when no override is supplied; receiver tap basis is independent\"}";
}
void json_path_evolution(std::ostream& out,const Options& o) {
    const auto& p=o.path_evolution;
    out<<"{\"active\":"<<(path_evolution_active(o)?"true":"false")
       <<",\"ray_coherence_seconds\":"<<p.ray_coherence_seconds
       <<",\"gaussian_doppler_width_hz\":"<<p.gaussian_doppler_width_hz
       <<",\"gaussian_doppler_standard_deviation_hz\":"<<p.gaussian_doppler_width_hz/2
       <<",\"gaussian_capture_total_allowance_hz\":"<<6*p.gaussian_doppler_width_hz
       <<",\"gaussian_target_correlation\":\"exp(-0.5*(pi*B*tau)^2); B=2*sigma of each ray Gaussian Doppler PSD; existing ray-center separation is independent\""
       <<",\"gaussian_teacher\":\"stationary proper-complex white Gaussian innovations convolved with a discrete L2-normalized Gaussian FIR; finite truncation and sampling approximate the target covariance; actual FIR shape is reported separately\""
       <<",\"diurnal_phase_amplitude_radians\":"<<p.diurnal_phase_amplitude_radians
       <<",\"diurnal_period_seconds\":"<<p.diurnal_period_seconds
       <<",\"maximum_diurnal_instantaneous_frequency_hz\":"<<std::abs(p.diurnal_phase_amplitude_radians)/p.diurnal_period_seconds
       <<",\"outage_start_seconds\":"<<p.outage_start_seconds<<",\"outage_end_seconds\":"<<p.outage_end_seconds
       <<",\"outage_gain\":"<<p.outage_gain<<",\"outage_ramp_seconds\":"<<p.outage_ramp_seconds
       <<",\"time_coordinate\":\"physical RX time n/(Fs*rx_clock_scale)-capture_lead_seconds\""
       <<",\"ray_model\":";
    json_string(out,p.gaussian_doppler_width_hz>0?
        "independent stationary proper-complex Gaussian-spectrum FIR gains: zero mean, unit mean power; existing equal-power two-ray delay and Doppler centers retained":
        p.ray_coherence_seconds>0?
        "independent stationary proper-complex OU gains: zero mean, unit mean power, correlation exp(-abs(dt)/coherence); existing equal-power two-ray delay and Doppler retained":
        "fixed unit ray gains; existing equal-power two-ray delay and Doppler retained when selected");
    out
       <<",\"outage_model\":\"signal-only attenuation on [start,end), with cosine descent/ascent inside the interval; thermal noise and path/filter state continue\""
       <<",\"scope\":\"declared physical-path stress, not field calibration; OU is point sampled and has an unbounded analog spectrum; the Gaussian FIR target also has unbounded spectral tails and its six-sigma-per-edge capture allowance is not strict support, so finite deterministic capture bounds are not a bandwidth guarantee and sampling-rate refinement is required\"}";
}
// Expected normalized autocorrelation for independent zero-mean random chips,
// equal mean carrier powers and one shared complex gain. The infinite RRC
// pulse has a raised-cosine autocorrelation; carrier translations contribute
// the three-tone Dirichlet factor. This is a recommendation, not a certificate
// for finite keyed patterns, pulse truncation, sampling or an unknown channel.
double timing_grid_step(const Geometry& g,double loss_db) {
    constexpr long double pi=std::numbers::pi_v<long double>;
    constexpr long double alpha=modem::pattern_pulse_rolloff;
    const auto correlation=[&](long double delay){
        const auto u=delay*g.fine_chip_rate_hz;
        const auto sinc=u==0?1.L:std::sin(pi*u)/(pi*u);
        const auto denominator=1-4*alpha*alpha*u*u;
        const auto pulse=std::abs(denominator)<1e-12L?
            pi/4*std::sin(pi/(2*alpha))/(pi/(2*alpha)):
            sinc*std::cos(pi*alpha*u)/denominator;
        return pulse*(1+2*std::cos(2*pi*g.carrier_spacing_hz*delay))/3;
    };
    // Both factors are positive and decreasing before their first zeros.
    // Restrict the solve to that main lobe, never a later ambiguity peak.
    long double low=0,high=std::min(1.L/g.fine_chip_rate_hz,1.L/(3*g.carrier_spacing_hz));
    const auto target=std::pow(10.L,-static_cast<long double>(loss_db)/10);
    for(unsigned iteration=0;iteration<80;++iteration){
        const auto middle=(low+high)/2;const auto value=correlation(middle);
        if(value*value>=target)low=middle;else high=middle;
    }
    // A uniform grid has at most half a grid step of timing mismatch when
    // endpoints cover the complete prior interval.
    return static_cast<double>(2*low);
}

void json_configuration(std::ostream& out,const Options& o,const Format& f,const Geometry& g) {
    const auto r=resolve_configuration(o);const auto calendar=whole_bit_calendar(o,f,g);
    const auto arrival=initial_arrival_bounds(o,r);
    const auto timing_step=timing_grid_step(g,o.timing_loss_db);
    const auto rate_step=1e6*timing_step/g.bit_seconds;
    const auto frequency_step=1/(4*g.bit_seconds);
    const auto clock_bound=o.margin*r.clock_bound_ppm;
    const auto frequency_bound=o.margin*r.frequency_bound_hz;
    const auto epoch_radius=r.clock?static_cast<std::int64_t>(std::ceil(2*r.clock->accuracy_seconds)):0;
    const auto epoch_count=2*epoch_radius+1;
    bool epoch_covered=epoch_count<=static_cast<std::int64_t>(o.epoch_offsets.size());
    if(epoch_covered)for(std::int64_t i=-epoch_radius;i<=epoch_radius;++i)
        if(std::find(o.epoch_offsets.begin(),o.epoch_offsets.end(),i)==o.epoch_offsets.end())epoch_covered=false;
    double calendar_half=0,calendar_center=0;
    if(r.clock){
        // The shared helper accepts integer rates. Express its entire timing
        // problem in virtual seconds t'=t/D for reciprocal Fs=1/D. Its result
        // is already in actual sample coordinates; no result rescaling applies.
        // Keeping this identity branch unchanged preserves legacy coordinates.
        auto policy=*r.clock;
        const auto divisor=static_cast<long double>(f.sample_rate_denominator);
        if(f.sample_rate_denominator>1){
            policy.accuracy_seconds/=f.sample_rate_denominator;
            policy.region_seconds/=f.sample_rate_denominator;
            policy.offset_seconds/=f.sample_rate_denominator;
        }
        const auto map=clock_sync::calendar_arrival_map(policy,static_cast<long double>(f.epoch)/divisor,
            static_cast<long double>(f.epoch)/divisor,0,f.sample_rate,1./f.sample_rate,
            clock_bound*1e-6,clock_sync::audio_lag_radius(o.audio_error_seconds)/f.sample_rate_denominator,
            g.source_samples,o.audio_error_seconds/f.sample_rate_denominator);
        if(!map)throw std::invalid_argument("resolved clock calendar cannot represent the observation");
        calendar_half=static_cast<double>(map->half_width_samples/effective_sample_rate(f));
        calendar_center=static_cast<double>(map->origin_samples/effective_sample_rate(f));
    }
    if(o.arrival_prior_seconds){
        // This uniform physical prior replaces only the initial clock/audio
        // timing draw. Keep its search estimate separate from the timestamp
        // helper's causal-audio coordinate shift.
        double ratio=clock_bound*1e-6;
        if(!r.active)for(const auto clock:o.clocks_ppm)ratio=std::max(ratio,o.margin*std::abs(clock)*1e-6);
        const auto support=static_cast<double>(g.source_samples)/effective_sample_rate(f);
        calendar_center=r.arrival_center;
        calendar_half=(r.arrival_half+ratio*(support+std::abs(r.arrival_center)))/(1-ratio);
        if(!std::isfinite(calendar_half)||ratio>=1)
            throw std::invalid_argument("explicit arrival prior calendar exceeds numeric range");
    }
    const auto [clock_min,clock_max]=std::minmax_element(o.clocks_ppm.begin(),o.clocks_ppm.end());
    const auto count=[](long double span,long double step){return std::ceil(span/step)+1;};
    const auto supplied_timing=std::floor((o.start_high-o.start_low)/o.start_step+1e-9)+1;
    const auto supplied_frequency=std::floor((o.frequency_high-o.frequency_low)/o.frequency_step+1e-9)+1;
    const long double supplied_bank=2.L*supplied_timing*supplied_frequency*o.clocks_ppm.size()*o.epoch_offsets.size()*o.frequency_drifts.size()*(o.basis=="two-ray"?2:1);
    out<<"{\"new_controls_active\":"<<(r.active?"true":"false")<<",\"oscillator_presets_active\":"<<(r.oscillators?"true":"false")
       <<",\"fhss_center_aligned\":"<<(f.fhss_center_aligned?"true":"false")
       <<",\"base_oscillator\":";json_string(out,r.base_id);out<<",\"shift_oscillator\":";json_string(out,r.shift_id);
    out<<",\"topology\":";
    json_string(out,o.rf_hz==0?"no RF shift oscillator; baseband reference only":r.shared_shift_clock?
        "shared audio/RF clock within each endpoint; independent TX and RX clocks":
        "independent audio and RF references; effective relative preset accuracy split symmetrically between TX and RX");
    out<<",\"shift_oscillator_active\":"<<(o.rf_hz>0?"true":"false")
       <<",\"shift_uses_baseband_clock\":"<<(r.shared_shift_clock?"true":"false")
       <<",\"nominal_conversion_if_hz\":"<<o.nominal_if_hz<<",\"rf_center_hz\":"<<o.rf_hz
       <<",\"frequency_parameter_scope\":\"RF and conversion-IF values multiply oscillator errors; the sampled signal is zero-centered complex baseband\""
       <<",\"sampling_clock_model\":\"constant endpoint ppm; shared mode correlates static RF/audio ppm and counts base phase diffusion once; stochastic phase does not perturb sampling coordinates\""
       <<",\"base_nominal_relative_bound_ppm\":"<<r.base_ppm<<",\"shift_nominal_relative_bound_ppm\":"<<r.rf_ppm
       <<",\"base_per_endpoint_bound_ppm\":"<<r.base_ppm/2<<",\"shift_per_endpoint_bound_ppm\":"<<r.rf_ppm/2
       <<",\"exact_relative_audio_ratio_bound_ppm\":"<<r.clock_bound_ppm
       <<",\"deterministic_oscillator_cfo_bound_hz\":"<<r.frequency_bound_hz
       <<",\"relative_phase_diffusion_rad_sqrt_s\":"<<r.phase_diffusion
       <<",\"per_endpoint_phase_diffusion_rad_sqrt_s\":"<<r.phase_diffusion/std::sqrt(2.)
       <<",\"phase_override_explicit\":"<<(o.phase_diffusion_explicit?"true":"false")
       <<",\"margin\":"<<o.margin<<",\"oscillator_truth_margin_applied\":false,\"spread_margin_applied_once\":"
       <<((o.spread_preset||o.spread_delay_ms||o.spread_doppler_hz)?"true":"false")
       <<",\"spread_preset\":";json_string(out,r.spread_id);out<<",\"selective_perturbation\":";json_string(out,r.perturbation_id);
    const auto selective_scale=o.selective_scale_hz>0?o.selective_scale_hz:g.carrier_spacing_hz;
    out<<",\"selective_channel\":{\"model\":";
    json_string(out,o.selective_model=="legacy-allpass"?"legacy-allpass-v1":"dispersive-dft-v1");
    out<<",\"requested_correlation_scale_hz\":"<<o.selective_scale_hz
       <<",\"resolved_correlation_scale_hz\":"<<selective_scale
       <<",\"center_hz\":"<<o.selective_center_hz
       <<",\"geometry_applied\":"<<(o.selective_model=="dispersive-dft"?"true":"false")
       <<",\"enabled\":"<<(r.selective_rate_hz>0&&(r.slow_depth>0||r.fast_depth>0)?"true":"false")
       <<",\"modulation_rate_override_hz\":";
    if(o.selective_modulation_rate_hz)out<<*o.selective_modulation_rate_hz;else out<<"null";
    out<<",\"resolved_fast_reference_rate_hz\":"<<r.selective_rate_hz
       <<",\"active_sideband_extent_hz\":"<<r.selective_sideband_hz
       <<",\"capture_allowance_hz\":"<<r.selective_capture_allowance_hz
       <<",\"modulation_rate_source\":";
    json_string(out,o.selective_modulation_rate_hz?"explicit effective physical rate; no additional margin":
        "effective spread Doppler; slow component runs at one tenth");
    out
       <<",\"frequency_frame\":\"fixed public channel frame; never recentered from hidden oscillator truth\""
       <<",\"selective_tail_floor_seconds\":"<<o.selective_tail_floor_seconds
       <<",\"interpretation\":\"dispersive-dft-v1 is a spacing-scaled stress law when the scale is auto; selected ray delay and Doppler remain separate physical dimensions\"}"
       <<",\"spread_delay_ms\":"<<r.spread_delay_ms<<",\"spread_doppler_hz\":"<<r.spread_doppler_hz
       <<",\"perturbation_slow_depth\":"<<r.slow_depth<<",\"perturbation_fast_depth\":"<<r.fast_depth
       <<",\"spread_inputs\":";json_spread_inputs(out,o,r);
    out<<",\"path_evolution\":";json_path_evolution(out,o);
    out
       <<",\"audio_error_seconds_per_station\":"<<o.audio_error_seconds
       <<",\"audio_lag_interval_per_station\":[0,"<<o.audio_error_seconds<<"],\"clock_sync\":";
    json_string(out,clock_sync::format(r.clock));
    out<<",\"clock_policy_active\":"<<(r.clock?"true":"false")
       <<",\"clock_accuracy_seconds_per_station\":"<<(r.clock?r.clock->accuracy_seconds:0)
       <<",\"clock_region_total_seconds\":"<<(r.clock?r.clock->region_seconds:0)
       <<",\"clock_propagation_offset_seconds\":"<<(r.clock?r.clock->offset_seconds:0)
       <<",\"initial_arrival_center_seconds\":"<<r.arrival_center<<",\"initial_arrival_half_width_seconds\":"<<r.arrival_half
       <<",\"full_observation_calendar_half_width_seconds\":"<<calendar_half
       <<",\"calendar_timestamp_center_seconds\":"<<calendar_center
       <<",\"calendar_timestamp_audio_center_shift_seconds\":"<<(r.clock&&!o.arrival_prior_seconds?o.audio_error_seconds/2:0)
       <<",\"calendar_timestamp_center_shift_applied_to_raw_samples\":false"
       <<",\"initial_arrival_bounds_seconds\":["<<arrival[0]<<','<<arrival[1]<<']'
       <<",\"explicit_arrival_prior\":"<<(o.arrival_prior_seconds?"true":"false")
       <<",\"initial_arrival_prior_source\":";
    json_string(out,o.arrival_prior_seconds?"explicit uniform physical arrival prior; replaces clock/audio initial timing only":
        r.active?(r.clock?"configured independent clock/audio draws":"configured inactive clock policy: fixed zero timing"):
        "legacy manual timing draw from public --start range");
    out<<",\"explicit_arrival_seed_domain\":\"channel seed XOR 0x94d049bb133111eb; independent mt19937_64 stream\""
       <<",\"timing_convention\":";
    json_string(out,o.arrival_prior_seconds?
        "explicit uniform initial physical arrival relative to source support; clock/audio timing draw replaced, endpoint clock-rate and oscillator models preserved":
        "raw samples use offset + TX lag - RX lag with both lags causal [0,Audio], giving zero-centered +/-Audio relative lag; the calendar timestamp helper has a separate +Audio/2 coordinate center; region is total width and UTC clock bounds are per station");
    out<<",\"truth_selection\":";
    json_string(out,o.arrival_prior_seconds?
        "initial timing is uniform on the explicit physical prior, drawn from a domain-separated channel-seed stream independent of --start; other configured/legacy draws are unchanged":
        r.active?"physical endpoint/clock draws independent of public search; inactive UTC policy means fixed zero initial timing; explicit truth frequency/drift and keyed epoch offset remain independent":"legacy manual mode draws timing/frequency/clock/drift from declared ranges");
    out<<",\"truth_epoch_offset_seconds\":"<<o.truth_epoch_offset<<",\"receiver_pattern_epoch\":"<<f.epoch
       <<",\"transmitter_pattern_epoch\":"<<offset_epoch(f.epoch,o.truth_epoch_offset)
       <<",\"epoch_address_scope\":\"explicit independent keyed-epoch offset; PC timing draws do not silently choose a crypto epoch\""
       <<",\"suggested_epoch_radius_seconds\":"<<epoch_radius<<",\"suggested_epoch_alternatives\":"<<epoch_count
       <<",\"suggested_epoch_offsets\":";
    if(epoch_count>1001)out<<"null";else{out<<'[';for(std::int64_t i=-epoch_radius;i<=epoch_radius;++i){if(i!=-epoch_radius)out<<',';out<<i;}out<<']';}
    out<<",\"public_search\":{\"timing_bounds_seconds\":["<<o.start_low<<','<<o.start_high<<"],\"frequency_bounds_hz\":["<<o.frequency_low<<','<<o.frequency_high
       <<"],\"clock_bounds_ppm\":["<<*clock_min<<','<<*clock_max<<"],\"epoch_alternatives\":"<<o.epoch_offsets.size()
       <<",\"estimated_hypotheses\":"<<supplied_bank<<",\"materialization_cap\":10000"
       <<",\"covers_initial_timing_prior\":"<<(o.start_low<=arrival[0]&&o.start_high>=arrival[1]?"true":"false")
       <<",\"covers_static_oscillator_frequency_prior\":"<<(o.frequency_low<=-r.frequency_bound_hz+o.truth_frequency_hz&&o.frequency_high>=r.frequency_bound_hz+o.truth_frequency_hz?"true":"false")
       <<",\"covers_audio_clock_prior\":"<<(*clock_min<=-r.clock_bound_ppm&&*clock_max>=r.clock_bound_ppm?"true":"false")
       <<",\"covers_suggested_epochs\":"<<(epoch_covered?"true":"false")<<'}'
       <<",\"search_estimates\":{\"margin_frequency_half_width_hz\":"<<frequency_bound<<",\"margin_clock_half_width_ppm\":"<<clock_bound
       <<",\"timing_maximum_step_seconds\":"<<timing_step
       <<",\"timing_loss_budget_db\":"<<o.timing_loss_db
       <<",\"timing_model_fine_rate_hz\":"<<g.fine_chip_rate_hz
       <<",\"timing_model_carrier_spacing_hz\":"<<g.carrier_spacing_hz
       <<",\"timing_model_pulse_rolloff\":"<<modem::pattern_pulse_rolloff
       <<",\"timing_model\":\"squared raised-cosine chip autocorrelation times squared equal-power three-carrier Dirichlet factor; first-main-lobe bisection\""
       <<",\"timing_model_assumptions\":\"independent zero-mean random chips; equal mean carrier powers; one shared complex gain; infinite RRC pulse; worst mismatch half a uniform grid step with both endpoints covered\""
       <<",\"timing_model_is_finite_waveform_certificate\":false"
       <<",\"timing_model_limits\":\"finite keyed patterns, truncated pulse, sampled interpolation, channel uncertainty, outages and clock/timing joint mismatch require independent validation\""
       <<",\"clock_step_model\":\"endpoint displacement over the complete bit equals timing_maximum_step_seconds; same approximate timing loss budget\""
       <<",\"whole_bit_frequency_maximum_step_hz\":"<<frequency_step
       <<",\"block_frequency_maximum_step_hz\":"<<effective_sample_rate(f)/(4.*block_samples(o,f))
       <<",\"clock_maximum_step_ppm\":"<<rate_step<<",\"timing_positions\":"<<count(2*calendar_half,timing_step)
       <<",\"whole_bit_frequency_positions\":"<<count(2*frequency_bound,frequency_step)<<",\"clock_positions\":"<<count(2*clock_bound,rate_step)
       <<",\"scope\":\"finite-grid estimates only; no implicit bank generation, pruning, phase-path count or reception guarantee\"}"
       <<",\"capture\":{\"sample_rate_hz\":"<<effective_sample_rate(f)
       <<",\"sample_rate_numerator\":"<<f.sample_rate<<",\"sample_rate_denominator\":"<<f.sample_rate_denominator
       <<",\"fine_rate_per_carrier_hz\":"<<g.fine_chip_rate_hz
       <<",\"nominal_triplet_bandwidth_hz\":"<<g.occupied_bandwidth_hz<<",\"deterministic_nyquist_margin_hz\":"<<configuration_capture_margin(o,f,g)
       <<",\"feasible_for_deterministic_prior\":"<<(configuration_capture_margin(o,f,g)>0?"true":"false")
       <<",\"scope\":\"whole deterministic RF/audio-error bound, public CFO grid, maximum diurnal instantaneous frequency and Gaussian-ray allowance of six sigma per edge; stochastic oscillator and ray spectral tails are not a bounded capture guarantee\""
       <<",\"sub_64_hz_model\":"<<(effective_sample_rate(f)<64?"true":"false")
       <<",\"sampling_model_limits\":\"below 64 samples/s: FHSS, dropout, active sampled frontend, slow AGC, white/flicker/random-walk FM and white PM are rejected; constant offsets, Wiener phase, fractional two-ray propagation and selected selective perturbations remain modeled in physical seconds; no high-rate integration/decimation equivalence is claimed\"}"
       <<",\"duration\":{\"maximum_requested_active_seconds\":315576000,\"requested_active_seconds\":"<<f.bit_seconds
       <<",\"rounded_active_seconds\":"<<g.tile_count*g.tile_seconds<<",\"elapsed_source_calendar_seconds\":"<<g.bit_seconds
       <<",\"source_support_seconds\":"<<static_cast<long double>(g.source_samples)/effective_sample_rate(f)
       <<",\"scope\":\"ten-Julian-year requested-active limit; complete private tiles, internal burst waits, pulse padding and channel/capture tails may extend actual elapsed duration\"}"
       <<",\"whole_bit_symbol_burst\":{\"enabled\":"<<(o.symbol_burst_wait?"true":"false")<<",\"wait_useful_symbols\":"<<o.symbol_burst_wait
       <<",\"changes_inside_one_bit\":false,\"internal_wait_tiles\":"<<o.burst_wait_tiles
       <<",\"requested_earliest_support_epoch\":"<<o.epoch<<",\"useful_samples\":"<<calendar.useful_samples
       <<",\"pulse_padding_samples_each_side\":"<<g.calendar_origin_sample;
    const auto coordinate=[&](const char* name,UtcSample at){out<<",\""<<name<<"\":{\"epoch\":"<<at.epoch<<",\"sample_phase\":"<<at.phase<<'}';};
    coordinate("support_begin",calendar.support_begin);coordinate("payload_begin",calendar.payload_begin);
    coordinate("payload_end",calendar.payload_end);coordinate("support_end",calendar.support_end);coordinate("next_support_begin",calendar.next_support);
    out<<",\"next_support_formula\":\"ceil_UTC_30s(max(support_end + wait_symbols*whole_useful_bit_samples, support_begin + 60s))\""
       <<",\"scope\":\"one-bit source renders this slot only; successor is public calendar metadata, not three internal wait tiles or multi-bit runtime support\"}"
       <<",\"receiver_observation\":{\"missed_dwell_indices\":[";
    for(std::size_t i=0;i<o.missed_dwells.size();++i){if(i)out<<',';out<<o.missed_dwells[i];}
    out<<"],\"maximum_missed_dwell_indices\":4096,\"dwell_seconds\":"<<f.dwell_seconds
       <<",\"public_calendar_origin_seconds\":"<<static_cast<double>(g.calendar_origin_sample)/effective_sample_rate(f)
       <<",\"missed_dwell_calendar\":\"floor((n/(Fs*RX_clock_scale) - capture_lead_seconds + retune_offset_seconds - calendar_origin_sample/Fs)/dwell_seconds); only nonnegative public receiver dwell indices are maskable\""
       <<",\"erasure_stage\":\"after physical channel, noise and frontend; output I/Q and validity become zero while all physical state advances\""
       <<",\"physical_rf_retuning\":false,\"minimum_observed_fraction\":"<<o.minimum_observed_fraction
       <<",\"coverage_policy_scope\":\"C++ run receiver only; complete-calendar requirement and likelihood thresholds remain unchanged; external scorers declare their own coverage policy\"}}";
}
void json_performance_geometry(std::ostream& out,const Options& o);
void print_configuration(const Options& o) {
    const auto f=format_for(o);const auto g=geometry(f);
    std::cout<<std::setprecision(17)<<"{\"mode\":\"configure\",\"configuration\":";
    json_configuration(std::cout,o,f,g);std::cout<<",\"performance_geometry\":";
    json_performance_geometry(std::cout,o);std::cout<<"}\n";
}
void json_provenance(std::ostream& out,const Options& o,const Format& f,const Geometry& g,std::uint64_t trial=0) {
    const auto channel_seed=o.channel_seed.value_or(o.seed)+trial;
    const auto oscillator_seed=o.oscillator_seed.value_or(o.channel_seed.value_or(o.seed))+trial;
    const auto noise_seed=o.noise_seed.value_or(o.channel_seed.value_or(o.seed))+trial;
    const auto frontend_seed=o.frontend_seed.value_or(o.channel_seed.value_or(o.seed))+trial;
    const auto path_seed=o.path_seed.value_or(o.channel_seed.value_or(o.seed))+trial;
    out<<",\"sample_rate_numerator\":"<<f.sample_rate<<",\"sample_rate_denominator\":"<<f.sample_rate_denominator
       <<",\"duration_alternatives\":"<<o.duration_alternatives
       <<",\"false_admission_probability_per_horizon\":"<<o.false_probability
       <<",\"monitoring_horizon_seconds\":"<<o.horizon<<",\"attempt_spacing_seconds\":"<<o.attempt_spacing
       <<",\"random_seed_roots\":{\"channel\":"<<channel_seed<<",\"oscillator\":"<<oscillator_seed
       <<",\"noise\":"<<noise_seed<<",\"frontend\":"<<frontend_seed<<",\"path\":"<<path_seed
       <<",\"oscillator_explicit\":"<<(o.oscillator_seed?"true":"false")
       <<",\"noise_explicit\":"<<(o.noise_seed?"true":"false")
       <<",\"frontend_explicit\":"<<(o.frontend_seed?"true":"false")
       <<",\"path_explicit\":"<<(o.path_seed?"true":"false")
       <<",\"fallback\":\"unset oscillator/noise/frontend/path roots inherit the resolved channel seed\",\"channel_root_scope\":\"static timing/CFO/clock/drift truth, spread ray, and frequency-selective perturbation\",\"oscillator_root_scope\":\"TX/RX oscillator spectra and FHSS phase resets\",\"noise_root_scope\":\"thermal noise only\",\"frontend_root_scope\":\"DAC/ADC conversion, impulse process, and tone phase\",\"path_root_scope\":\"independent stationary OU or Gaussian-FIR ray gains and initial diurnal path phase\"}"
       <<",\"command_argv\":[";
    for(std::size_t i=0;i<o.command_argv.size();++i){if(i)out<<',';json_string(out,o.command_argv[i]);}
    out<<"],\"source_format\":{\"version\":3,\"inner_rate_hz\":"<<f.inner_rate_hz
       <<",\"fhss_center_aligned\":"<<(f.fhss_center_aligned?"true":"false")
       <<",\"added_symbols\":"<<f.added_symbols<<",\"added_interleave\":"<<(f.added_interleave?"true":"false")
       <<",\"encrypted\":"<<(f.encrypt?"true":"false")<<",\"symbol_ordinal\":"<<f.symbol_ordinal
       <<",\"minimum_inner_coefficients\":"<<f.minimum_inner_coefficients
       <<",\"tile_seconds\":"<<g.tile_seconds<<",\"tile_count\":"<<g.tile_count
       <<",\"calendar_origin_sample\":"<<g.calendar_origin_sample
       <<",\"payload_fine_cells_per_tile\":"<<g.payload_fine_cells_per_tile
       <<",\"dwell_seconds\":"<<f.dwell_seconds<<",\"quiet_seconds\":"<<f.quiet_seconds
       <<",\"ramp_seconds\":"<<f.ramp_seconds<<",\"margin_seconds\":"<<f.margin_seconds
       <<",\"fine_rate_hz\":"<<g.fine_chip_rate_hz<<",\"carrier_spacing_hz\":"<<g.carrier_spacing_hz
       <<",\"occupied_bandwidth_hz\":"<<g.occupied_bandwidth_hz<<"}"
       <<",\"channel_model\":{\"selective_model\":";
    json_string(out,o.selective_model=="legacy-allpass"?"legacy-allpass-v1":"dispersive-dft-v1");
    out<<",\"selective_scale_requested_hz\":"<<o.selective_scale_hz
       <<",\"selective_scale_resolved_hz\":"<<(o.selective_scale_hz>0?o.selective_scale_hz:g.carrier_spacing_hz)
       <<",\"selective_center_hz\":"<<o.selective_center_hz
       <<",\"selective_geometry_applied\":"<<(o.selective_model=="dispersive-dft"?"true":"false")
       <<",\"selective_modulation_rate_override_hz\":";
    if(o.selective_modulation_rate_hz)out<<*o.selective_modulation_rate_hz;else out<<"null";
    out<<",\"selective_resolved_fast_reference_rate_hz\":"<<resolve_configuration(o).selective_rate_hz
       <<",\"selective_active_sideband_extent_hz\":"<<resolve_configuration(o).selective_sideband_hz
       <<",\"selective_capture_allowance_hz\":"<<resolve_configuration(o).selective_capture_allowance_hz
       <<",\"selective_modulation_rate_source\":";
    json_string(out,o.selective_modulation_rate_hz?"explicit effective physical rate; no additional margin":
        "effective spread Doppler; slow component runs at one tenth");
    out
       <<",\"selective_frequency_frame\":\"fixed public channel frame; hidden oscillator truth is never used to recenter\""
       <<",\"selective_tail_floor_seconds\":"<<o.selective_tail_floor_seconds
       <<",\"selective_law_note\":\"dispersive-dft-v1 with auto scale is a spacing-scaled stress law; it does not represent the separately configured ray delay or Doppler\""
       <<",\"spread_delay_ms\":"<<resolve_configuration(o).spread_delay_ms
       <<",\"spread_doppler_hz\":"<<resolve_configuration(o).spread_doppler_hz
       <<",\"perturbation_slow_depth\":"<<resolve_configuration(o).slow_depth
       <<",\"perturbation_fast_depth\":"<<resolve_configuration(o).fast_depth
       <<",\"nominal_conversion_if_hz\":"<<o.nominal_if_hz<<",\"signal_gain\":1,\"rf_hz\":"<<o.rf_hz
       <<",\"flicker_tau_seconds\":["<<o.flicker_min_tau<<','<<o.flicker_max_tau<<']'
       <<",\"flicker_bands\":"<<o.flicker_bands
       <<",\"sample_clock_reference\":";
    json_string(out,resolve_configuration(o).oscillators?"independent TX/RX endpoint errors; truth_clock_ppm reports their exact ratio":"RX0ppm; TX carries declared relative error");
    out<<",\"physical_rf_retuning\":false,\"spread_inputs\":";json_spread_inputs(out,o,resolve_configuration(o));
    out<<",\"path_evolution\":";json_path_evolution(out,o);out<<'}'
       <<",\"source_hash_note\":\"source and binary hashes must be bound externally to this uncommitted study build\"";
    if(configured_physics(o)||path_evolution_active(o)||o.arrival_prior_seconds||!o.missed_dwells.empty()||
       o.minimum_observed_fraction!=.9||o.truth_epoch_offset||o.nominal_if_hz!=1500||effective_sample_rate(f)<64||o.seconds>7*86400.){
        out<<",\"resolved_configuration\":";json_configuration(out,o,f,g);}
    const auto& front=o.frontend;
    auto converter=[&](const ConverterConfig& c){
        out<<"{\"nominal_bits\":"<<c.nominal_bits<<",\"effective_bits\":"<<(c.effective_bits>0?c.effective_bits:c.nominal_bits)
           <<",\"full_scale_component\":"<<c.full_scale<<",\"gain\":"<<c.gain
           <<",\"quantization_step\":"<<(c.nominal_bits?std::ldexp(c.full_scale,1-static_cast<int>(c.nominal_bits)):0)
           <<",\"added_tpdf_dither_rms_lsb_per_component\":"<<c.dither_rms_lsb<<'}';
    };
    out<<",\"gaussian_projection_supported\":"<<(!front.active()?"true":"false")
       <<",\"sampled_frontend\":{\"active\":"<<(front.active()?"true":"false")<<",\"dac\":";
    converter(front.dac);out<<",\"adc\":";converter(front.adc);
    out<<",\"tone_amplitude\":"<<front.tone_amplitude<<",\"tone_frequency_hz\":"<<front.tone_frequency_hz
       <<",\"tone_every_n_dwells\":"<<front.tone_every_n_dwells
       <<",\"impulse_amplitude\":"<<front.impulse_amplitude<<",\"impulse_rate_hz\":"<<front.impulse_rate_hz
       <<",\"impulse_process\":\"one-sample occupancy probability 1-exp(-rate/Fs_RX); at most one event per sample; independent uniform phase\""
       <<",\"lowpass_hz\":"<<front.lowpass_hz<<",\"gain_step_seconds\":"<<front.gain_step_seconds
       <<",\"gain_step_factor\":"<<front.gain_step_factor<<",\"overload_recovery_seconds\":"<<front.overload_recovery_seconds
       <<",\"ordering\":\"shaped source; DAC gain, ENOB error, TPDF and quantization; TX oscillator and gain; shared propagation; RX oscillator; thermal noise; receiver-coordinate interference; one-pole analog filter; legacy slow AGC; gain step and overload recovery; ADC gain, ENOB error, TPDF and quantization; observed erasure mask\""
       <<",\"converter_error_model\":\"synthetic independent Gaussian input error, variance=(step_ENOB^2-step_nominal^2)/12; not calibrated hardware ENOB or distortion\""
       <<",\"tone_coordinate\":\"receiver tuned baseband; not fixed RF; occupancy uses receiver nominal t=0 400-ms dwells\""
       <<",\"dac_dither_guards\":\"converter noise can occupy shaped guards while finite transmitter source exists; absent transmitter disables DAC\""
       <<",\"noise_normalization\":\"reported variance is pre-frontend thermal input variance; transformed samples may be colored and non-Gaussian\""
       <<",\"signal_energy_report_stage\":\"after shared propagation and RX oscillator; before thermal noise and sampled frontend\""
       <<",\"analytic_admission_supported\":"<<(!front.active()?"true":"false")<<'}';
}

void json_frontend_report(std::ostream& out,const StreamChannelReport& r) {
    out<<",\"path_truth_simulation_only\":{\"diurnal_initial_phase_radians\":"<<r.path_diurnal_initial_phase_radians
       <<",\"gaussian_ray_teacher\":{\"sample_interval_seconds\":"<<r.path_gaussian_sample_interval_seconds
       <<",\"fir_coefficients\":[";
    for(std::size_t i=0;i<r.path_gaussian_fir_coefficients.size();++i){if(i)out<<',';out<<r.path_gaussian_fir_coefficients[i];}
    out<<"],\"covariance_formula\":\"R[m]=sum(k=0..L-1-m,h[k]*h[k+m]) for 0<=m<L, otherwise 0; lag_seconds=m*sample_interval_seconds; proper-complex variance R[0]=1 when active\""
       <<",\"scope\":\"actual finite sampled teacher covariance; empty coefficients and zero sample interval when disabled; evaluation/oracle diagnostics only, distinct from ideal Gaussian target\"}"
       <<",\"selective_perturbation\":{\"enabled\":"<<(r.selective_perturbation?"true":"false");
    if(r.selective_perturbation) {
        const auto& p=*r.selective_perturbation;
        out<<",\"model\":";
        json_string(out,p.model==SelectiveModel::LegacyAllpass?"legacy-allpass-v1":"dispersive-dft-v1");
        out<<",\"sample_rate_hz\":"<<p.sample_rate_hz<<",\"seed\":"<<p.seed
           <<",\"correlation_spacing_hz\":"<<p.correlation_spacing_hz
           <<",\"center_hz\":"<<p.center_hz
           <<",\"response_period_hz\":"<<p.response_period_hz
           <<",\"response_period_scope\":";
        json_string(out,p.model==SelectiveModel::DispersiveDft?
            "ideal continuous-delay law; sampled cubic FIR perturbs this periodicity and has exact digital period Fs":
            "not applicable to the legacy all-pass bank");
        out
           <<",\"branch_delays_seconds\":["<<p.branch_delays_seconds[0]<<','<<p.branch_delays_seconds[1]<<','<<p.branch_delays_seconds[2]<<']'
           <<",\"memory_samples\":"<<p.memory_samples
           <<",\"component_order\":[\"slow\",\"fast\"],\"band_order\":";
        out<<(p.model==SelectiveModel::DispersiveDft?
            "[\"negative_scale_peak\",\"center_peak\",\"positive_scale_peak\"]":
            "[\"low\",\"middle\",\"high\"]");
        out
           <<",\"component_depths\":["<<p.component_depths[0]<<','<<p.component_depths[1]<<']'
           <<",\"component_rates_hz\":["<<p.component_rates_hz[0]<<','<<p.component_rates_hz[1]<<']'
           <<",\"band_centers_hz\":["<<p.band_centers_hz[0]<<','<<p.band_centers_hz[1]<<','<<p.band_centers_hz[2]<<']'
           <<",\"band_width_hz\":"<<p.band_width_hz<<",\"decay_tail_samples\":"<<p.decay_tail_samples
           <<",\"branch_interpolation\":";
        json_string(out,p.model==SelectiveModel::DispersiveDft
            ?"causal cubic Lagrange fractional delay; four taps at floor(delay_samples)+{-1,0,1,2}; exact integer-delay shortcut"
            :"rotated first-order all-pass crossovers");
        out<<",\"channel_tail_samples\":"<<r.tail_samples
           <<",\"total_channel_samples\":"<<r.samples
           <<",\"processed_rows\":"<<p.processed_rows
           <<",\"pre_selective_energy_samples\":"<<p.pre_selective_energy_samples
           <<",\"post_selective_energy_samples\":"<<p.post_selective_energy_samples
           <<",\"initial_phases_radians\":[";
        for(std::size_t band=0;band<p.initial_phases_radians.size();++band) {
            if(band)out<<',';
            out<<'['<<p.initial_phases_radians[band][0]<<','<<p.initial_phases_radians[band][1]<<']';
        }
        out<<"],\"phase_origin\":\"first physical RX capture row (index 0), before the first phase step; not calendar time zero\""
           <<",\"gain_law\":\"1 + slow_depth*sin(slow_phase) + fast_depth*sin(fast_phase); no power normalization\""
           <<",\"energy_scope\":\"sum of squared complex signal magnitudes over all processed capture rows, including captured tails; immediately before/after selective processing, before RX oscillator, thermal noise, frontend and observed erasures; divide by sample_rate_hz for physical-time energy\""
           <<",\"tail_scope\":";
        json_string(out,p.model==SelectiveModel::DispersiveDft
            ?"finite causal delay-filter memory plus the configured total-tail floor; no selected stage is truncated"
            :"zero-input IIR pole-state decay by 1e6 plus the configured total-tail floor; not an absolute silence threshold");
    }
    out<<"},\"scope\":\"seeded physical truth; never a practical receiver search input\"}";
    out<<",\"observation_endpoint_report\":{\"processed_samples\":"<<r.samples
       <<",\"natural_complete_samples\":"<<r.natural_complete_samples
       <<",\"public_calendar_padding_samples\":"<<r.observation_padding_samples
       <<",\"scope\":\"natural endpoint includes all source/interpolation/ray/selective/frontend tails; public fixed calendar may append zero-input observation rows\"}"
       <<",\"frontend_report\":{\"dac_clipped_components\":"<<r.dac_clipped_components
       <<",\"adc_clipped_components\":"<<r.adc_clipped_components<<",\"impulse_events\":"<<r.impulse_events
       <<",\"overload_samples\":"<<r.overload_samples<<",\"maximum_adc_input_component\":"<<r.maximum_adc_input_component
       <<",\"gaussian_projection_supported\":"<<(r.gaussian_projection_supported?"true":"false")<<'}';
}
void json_endpoint_truth(std::ostream& out,const StreamChannelConfig& c) {
    out<<",\"endpoint_truth\":{\"tx_audio_ppm\":"<<c.effects.tx.audio_ppm
       <<",\"rx_audio_ppm\":"<<c.effects.rx.audio_ppm<<",\"tx_rf_ppm\":"<<c.effects.tx.rf_ppm
       <<",\"rx_rf_ppm\":"<<c.effects.rx.rf_ppm<<",\"tx_phase_diffusion_rad_sqrt_s\":"<<c.effects.tx.phase_noise_rad_per_sqrt_second
       <<",\"rx_phase_diffusion_rad_sqrt_s\":"<<c.effects.rx.phase_noise_rad_per_sqrt_second
       <<",\"supplemental_tx_frequency_drift_hz_per_second\":"<<c.tx_frequency_drift_hz_per_second
       <<",\"scope\":\"simulation evaluation only; endpoint realizations are never receiver search inputs\"}";
}

std::size_t block_samples(const Options&,const Format&);

statistics::PlannerInput planner_input(const Options& o, std::uint64_t candidates) {
    statistics::PlannerInput p;
    const auto f=format_for(o);const auto g=geometry(f);
    const auto block=block_samples(o,f);
    p.cn0_db_hz=o.cn0; p.target_correct_probability=o.correct;
    p.false_admission_probability_per_horizon=o.false_probability;
    p.search.monitoring_horizon_seconds=o.horizon; p.search.attempt_spacing_seconds=o.attempt_spacing;
    p.search.duration_alternatives=o.duration_alternatives;
    p.search.timing_hypotheses=grid(o.start_low,o.start_high,o.start_step).size();
    p.search.frequency_hypotheses=grid(o.frequency_low,o.frequency_high,o.frequency_step).size();
    p.search.bit_hypotheses=2; p.search.epoch_hypotheses=1;
    p.search.sample_clock_hypotheses=o.clocks_ppm.size();
    p.search.epoch_hypotheses=o.epoch_offsets.size();
    p.search.phase_hypotheses=o.frequency_drifts.size();
    p.search.wrong_message_competitors=std::max<std::uint64_t>(1,candidates/2);
    p.dwell_seconds=.4; p.minimum_dwells=o.minimum_dwells;
    p.inner_rate_hz=o.rate; p.added_symbols=o.added; p.minimum_inner_coefficients=64;
    p.coherence_block_seconds=static_cast<double>(block)/effective_sample_rate(f);
    p.rank_per_block=o.basis=="two-ray"?2:1;
    // Use the complete source-tile calendar, including silent burst waits.
    // The steady-state fraction is a lower bound even though the final wait
    // is omitted from a finite transmission.
    p.duration_quantum_seconds=g.tile_seconds*(static_cast<double>(f.burst_wait_tiles)+1);
    p.duration_offset_seconds=-g.tile_seconds*f.burst_wait_tiles;
    p.usable_time_fraction=static_cast<double>(g.payload_fine_cells_per_tile)/
        g.fine_chip_rate_hz/p.duration_quantum_seconds;
    p.minimum_dwells=std::max(p.minimum_dwells,static_cast<std::uint64_t>(std::ceil(g.tile_seconds/.4)));
    p.maximum_outage_fraction=o.outage; p.retained_energy_fraction=o.retained_energy;
    p.wrong_projection_fraction=o.wrong_projection;
    p.baseband_samples_per_second=effective_sample_rate(f);
    const auto workers=std::min<std::uint64_t>(o.threads,candidates);
    p.candidate_batch_size=workers;p.sample_tile_size=std::min(o.chunk,block);
    p.bytes_per_complex_sample=sizeof(Complex); // Generic planner charges three streams: conservative for <=2 taps.
    const long double fixed=65536.L+candidates*(static_cast<long double>(g.source_workspace_bytes)+8192+4*p.rank_per_block*p.rank_per_block*sizeof(Complex))+
        workers*(4*16*16*sizeof(Complex));
    if(fixed>UINT64_MAX)throw std::invalid_argument("planner receiver workspace overflow");
    p.fixed_workspace_bytes=static_cast<std::uint64_t>(fixed);p.maximum_workspace_bytes=o.memory_bytes;
    p.operations_per_sample_per_hypothesis=64;
    p.search.channel_hypotheses=o.basis=="two-ray"?2:1;
    p.noise_model_supported=true;
    // Geometry alone cannot substantiate these channel/codeword bounds.
    p.retained_energy_bound_supported=o.assume_model_bounds;
    p.wrong_projection_bound_supported=o.assume_model_bounds;
    p.outage_bound_supported=o.assume_model_bounds;
    p.equal_rank_fixed_bank_supported=o.assume_model_bounds&&o.basis=="flat";
    return p;
}

std::uint64_t candidate_count(const Options& o);

void print_plan(const Options& o) {
    const auto starts=grid(o.start_low,o.start_high,o.start_step);
    const auto freqs=grid(o.frequency_low,o.frequency_high,o.frequency_step);
    const auto bank=candidate_count(o);
    const auto input=planner_input(o,bank);
    const auto result=statistics::plan_duration(input);
    const auto format=format_for(o);const auto shape=geometry(format);
    const auto active_seconds=result.duration_found?
        std::round((result.duration_seconds-input.duration_offset_seconds)/input.duration_quantum_seconds)*shape.tile_seconds:0;
    std::cout<<std::setprecision(12)<<"{\"mode\":\"plan\",\"target_correct_probability\":"<<o.correct
        <<",\"fhss_center_aligned\":"<<(format.fhss_center_aligned?"true":"false")
        <<",\"false_admission_probability_per_horizon\":"<<o.false_probability
        <<",\"monitoring_horizon_seconds\":"<<o.horizon<<",\"duration_alternatives\":"<<o.duration_alternatives<<",\"cn0_db_hz\":"<<o.cn0
        <<",\"status\":\""<<result.status_name
        <<"\",\"duration_found\":"<<(result.duration_found?"true":"false")
        <<",\"duration_seconds\":"<<result.duration_seconds
        <<",\"duration_quantum_seconds\":"<<input.duration_quantum_seconds
        <<",\"duration_offset_seconds\":"<<input.duration_offset_seconds
        <<",\"requested_active_seconds_for_transmitter\":"<<active_seconds
        <<",\"useful_energy_seconds\":"<<result.useful_energy_seconds
        <<",\"signal_noncentrality\":"<<result.signal_noncentrality
        <<",\"rank\":"<<result.rank<<",\"minimum_inner_coefficients\":"<<result.minimum_inner_coefficients
        <<",\"attempts_per_horizon\":"<<result.attempts_per_horizon
        <<",\"search_hypotheses_per_attempt\":"<<bank<<",\"search_tests_per_horizon_saturated\":"<<result.total_search_hypotheses
        <<",\"log_null_tests\":"<<result.log_null_tests_per_horizon
        <<",\"null_score_upper\":"<<result.null_score_upper
        <<",\"true_score_lower\":"<<result.guaranteed_true_score_lower
        <<",\"wrong_score_upper\":"<<result.wrong_score_upper
        <<",\"separation_margin\":"<<result.separation_margin
        <<",\"estimated_operations\":"<<result.estimated_operations
        <<",\"estimated_workspace_bytes\":"<<result.estimated_workspace_bytes
        <<",\"assumptions_supported\":"<<(result.status==statistics::PlannerStatus::conditional_model_bound?"true":"false")
        <<",\"assumption_note\":\"conditional only on caller-supplied retained-energy, wrong-codeword projection, outage, and white-noise bounds, plus fixed-rank bank if selected; this CLI has no empirical calibration\"";
    if(o.arrival_prior_seconds||!o.missed_dwells.empty()||o.minimum_observed_fraction!=.9){
        std::cout<<",\"simulation_controls_not_planner_certificates\":true,\"resolved_configuration\":";
        json_configuration(std::cout,o,format,shape);
    }
    std::cout<<"}\n";
}

void json_candidate(std::ostream& out, const Candidate& c) {
    out<<"{\"bit\":"<<static_cast<unsigned>(c.source_bit)<<",\"epoch_offset_seconds\":"<<c.epoch_offset_seconds
       <<",\"arrival_seconds\":"<<c.arrival_seconds<<",\"frequency_hz\":"<<c.frequency_hz
       <<",\"clock_ppm\":"<<c.clock_ppm<<",\"frequency_drift_hz_per_second\":"<<c.frequency_drift_hz_per_second
       <<",\"tap_count\":"<<c.channel.taps.size()<<",\"taps\":[";
    for(std::size_t i=0;i<c.channel.taps.size();++i){if(i)out<<',';out<<"{\"delay_seconds\":"<<c.channel.taps[i].delay_seconds<<",\"doppler_hz\":"<<c.channel.taps[i].doppler_hz<<"}";}
    out<<"]}";
}

std::uint64_t candidate_count(const Options& o) {
    const auto starts=grid(o.start_low,o.start_high,o.start_step);
    const auto frequencies=grid(o.frequency_low,o.frequency_high,o.frequency_step);
    std::uint64_t count=2;
    for(const auto n:{starts.size(),frequencies.size(),o.clocks_ppm.size(),o.epoch_offsets.size(),o.frequency_drifts.size(),
                      static_cast<std::size_t>(o.basis=="two-ray"?2:1)}) {
        if(n==0||count>10000/n)throw std::invalid_argument("candidate bank exceeds the 10000-hypothesis budget");
        count*=n;
    }
    return count;
}

std::vector<Candidate> candidates_for(const Options& o, const Format& f, const Crypto& key) {
    (void)f; (void)key;
    std::vector<Candidate> out;
    const auto starts=grid(o.start_low,o.start_high,o.start_step);
    const auto frequencies=grid(o.frequency_low,o.frequency_high,o.frequency_step);
    (void)candidate_count(o);
    const unsigned models=o.basis=="two-ray"?2U:1U;
    for (const auto start:starts) for(const auto frequency:frequencies)
    for(const auto clock:o.clocks_ppm) for(const auto epoch:o.epoch_offsets)
    for(const auto drift:o.frequency_drifts) for(unsigned model=0;model<models;++model)
    for(std::uint8_t bit=0;bit<2;++bit) {
        Candidate c; c.source_bit=bit; c.arrival_seconds=start; c.frequency_hz=frequency;
        c.clock_ppm=clock; c.epoch_offset_seconds=epoch; c.frequency_drift_hz_per_second=drift;
        c.channel.taps={{0,0}};
        if(model==1)c.channel.taps.push_back({o.tap_delay_ms/1000,o.tap_doppler_hz});
        out.push_back(c);
    }
    return out;
}

std::size_t block_samples(const Options& o,const Format& f) {
    if(o.block_seconds<0)return o.block;
    auto scaled=static_cast<long double>(o.block_seconds)*effective_sample_rate(f);
    const auto nearest=std::round(scaled);
    if(std::abs(scaled-nearest)<=4*std::numeric_limits<double>::epsilon()*std::max(1.L,std::abs(scaled)))scaled=nearest;
    const long double samples=std::ceil(scaled);
    if(samples<1||samples>std::numeric_limits<std::size_t>::max())throw std::invalid_argument("coherence block coordinate overflow");
    return static_cast<std::size_t>(samples);
}

StreamChannelConfig channel_config(const Options& o,const Format& base,const Geometry& nominal,
                                   std::uint64_t seed,double noise,double pad,double trail,
                                   std::uint64_t observation_samples) {
    StreamChannelConfig channel_cfg; channel_cfg.frontend=o.frontend; channel_cfg.path_evolution=o.path_evolution;
    channel_cfg.selective_model.model=o.selective_model=="legacy-allpass"
        ?SelectiveModel::LegacyAllpass:SelectiveModel::DispersiveDft;
    channel_cfg.selective_model.correlation_spacing_hz=o.selective_scale_hz>0
        ?o.selective_scale_hz:nominal.carrier_spacing_hz;
    channel_cfg.selective_model.center_hz=o.selective_center_hz;
    channel_cfg.selective_model.modulation_rate_hz=o.selective_modulation_rate_hz;
    channel_cfg.selective_tail_floor_seconds=o.selective_tail_floor_seconds;
    channel_cfg.minimum_capture_samples=observation_samples;
    channel_cfg.effects.sample_rate=effective_sample_rate(base);
    channel_cfg.effects.occupied_bandwidth_hz=nominal.occupied_bandwidth_hz;
    channel_cfg.effects.rf_hz=o.rf_hz; channel_cfg.effects.nominal_conversion_if_hz=o.nominal_if_hz;
    channel_cfg.effects.seed=seed; channel_cfg.effects.signal_enabled=o.control!="null";
    const auto channel_seed_base=o.channel_seed.value_or(o.seed);
    if(seed<channel_seed_base)throw std::invalid_argument("channel trial seed precedes its configured root");
    const auto trial=seed-channel_seed_base;
    channel_cfg.oscillator_seed=o.oscillator_seed.value_or(channel_seed_base)+trial;
    channel_cfg.noise_seed=o.noise_seed.value_or(channel_seed_base)+trial;
    channel_cfg.frontend_seed=o.frontend_seed.value_or(channel_seed_base)+trial;
    channel_cfg.path_seed=o.path_seed.value_or(channel_seed_base)+trial;
    channel_cfg.effects.capture_lead_seconds=pad;
    channel_cfg.effects.capture_trail_seconds=trail;
    channel_cfg.signal_gain=1; // PatternSource already divides total triplet power by three.
    std::mt19937_64 truth_rng(seed^0xd1b54a32d192ed03ULL);
    std::uniform_real_distribution<double> time_truth(o.start_low,o.start_high);
    std::uniform_real_distribution<double> frequency_truth(o.frequency_low,o.frequency_high);
    const auto [clock_min,clock_max]=std::minmax_element(o.clocks_ppm.begin(),o.clocks_ppm.end());
    const auto [drift_min,drift_max]=std::minmax_element(o.frequency_drifts.begin(),o.frequency_drifts.end());
    std::uniform_real_distribution<double> clock_truth(*clock_min,*clock_max);
    std::uniform_real_distribution<double> drift_truth(*drift_min,*drift_max);
    channel_cfg.effects.timing_offset_seconds=time_truth(truth_rng);
    channel_cfg.effects.static_doppler_hz=frequency_truth(truth_rng);
    const auto true_clock=clock_truth(truth_rng);
    channel_cfg.effects.tx.audio_ppm=true_clock;
    channel_cfg.effects.rx.audio_ppm=0;
    const auto tx_scale=1+channel_cfg.effects.tx.audio_ppm*1e-6;
    const auto rx_scale=1+channel_cfg.effects.rx.audio_ppm*1e-6;
    // The requested frequency grid is the residual per nominal RX second,
    // including the independently clocked conversion IF contribution.
    channel_cfg.effects.static_doppler_hz=channel_cfg.effects.static_doppler_hz*rx_scale-
        channel_cfg.effects.nominal_conversion_if_hz*(tx_scale-rx_scale);
    channel_cfg.effects.tx.phase_noise_rad_per_sqrt_second=o.phase_diffusion/std::sqrt(2.);
    channel_cfg.effects.rx.phase_noise_rad_per_sqrt_second=o.phase_diffusion/std::sqrt(2.);
    const auto true_drift=drift_truth(truth_rng);
    if(o.rf_hz>0)channel_cfg.effects.tx.rf_drift_ppm_per_second=true_drift*1e6/o.rf_hz;
    else channel_cfg.tx_frequency_drift_hz_per_second=true_drift;
    channel_cfg.effects.static_doppler_hz+=true_drift*(pad+channel_cfg.effects.timing_offset_seconds);
    const auto resolved=resolve_configuration(o);
    if(resolved.active){
        // Preset values are effective relative bounds, not per-end bounds.
        // Public receiver grids never choose these physical realizations.
        channel_cfg.effects.tx={};channel_cfg.effects.rx={};
        channel_cfg.tx_frequency_drift_hz_per_second=0;
        const auto uniform=[&](double low,double high){return std::uniform_real_distribution<double>(low,high)(truth_rng);};
        if(resolved.oscillators){
            channel_cfg.effects.tx.audio_ppm=uniform(-resolved.base_ppm/2,resolved.base_ppm/2);
            channel_cfg.effects.rx.audio_ppm=uniform(-resolved.base_ppm/2,resolved.base_ppm/2);
            channel_cfg.effects.tx.rf_ppm=uniform(-resolved.rf_ppm/2,resolved.rf_ppm/2);
            channel_cfg.effects.rx.rf_ppm=uniform(-resolved.rf_ppm/2,resolved.rf_ppm/2);
            if(resolved.shared_shift_clock){
                channel_cfg.effects.tx.rf_ppm=channel_cfg.effects.tx.audio_ppm;
                channel_cfg.effects.rx.rf_ppm=channel_cfg.effects.rx.audio_ppm;
            }
        }
        channel_cfg.effects.tx.phase_noise_rad_per_sqrt_second=resolved.phase_diffusion/std::sqrt(2.);
        channel_cfg.effects.rx.phase_noise_rad_per_sqrt_second=resolved.phase_diffusion/std::sqrt(2.);
        channel_cfg.effects.timing_offset_seconds=0;
        if(resolved.clock){const auto& policy=*resolved.clock;
            channel_cfg.effects.timing_offset_seconds=resolved.arrival_center+
                uniform(-policy.accuracy_seconds,policy.accuracy_seconds)-uniform(-policy.accuracy_seconds,policy.accuracy_seconds)+
                uniform(-policy.region_seconds/2,policy.region_seconds/2)+
                uniform(0,o.audio_error_seconds)-uniform(0,o.audio_error_seconds);
        }
        const auto rx=1+channel_cfg.effects.rx.audio_ppm*1e-6;
        const auto physical_drift=o.truth_frequency_drift*rx*rx;
        if(o.rf_hz>0)channel_cfg.effects.tx.rf_drift_ppm_per_second=physical_drift*1e6/o.rf_hz;
        else channel_cfg.tx_frequency_drift_hz_per_second=physical_drift;
        channel_cfg.effects.static_doppler_hz=o.truth_frequency_hz*rx+
            physical_drift*(pad+channel_cfg.effects.timing_offset_seconds);
    }
    if(o.arrival_prior_seconds){
        // Preserve every old draw and its ordering. A separate stream from
        // the same physical channel root makes this timing independent of
        // public acquisition grids and of optional endpoint/policy draws.
        std::mt19937_64 arrival_rng(seed^0x94d049bb133111ebULL);
        const auto old_arrival=channel_cfg.effects.timing_offset_seconds;
        channel_cfg.effects.timing_offset_seconds=std::uniform_real_distribution<double>(
            (*o.arrival_prior_seconds)[0],(*o.arrival_prior_seconds)[1])(arrival_rng);
        const auto physical_drift=(channel_cfg.effects.tx.rf_drift_ppm_per_second-
            channel_cfg.effects.rx.rf_drift_ppm_per_second)*o.rf_hz*1e-6+
            channel_cfg.tx_frequency_drift_hz_per_second;
        channel_cfg.effects.static_doppler_hz+=physical_drift*(channel_cfg.effects.timing_offset_seconds-old_arrival);
    }
    channel_cfg.effects.dropout_ms=o.dropout_ms;
    channel_cfg.effects.dropout_start_ms=o.dropout_start_ms;
    channel_cfg.effects.dropout_every_n_dwells=o.dropout_every_dwells;
    channel_cfg.missed_receiver_dwell_indices=o.missed_dwells;
    channel_cfg.tx_spectrum.white_fm_hz_sqrt_seconds=o.white_fm/std::sqrt(2.);
    channel_cfg.rx_spectrum.white_fm_hz_sqrt_seconds=o.white_fm/std::sqrt(2.);
    channel_cfg.tx_spectrum.random_walk_fm_hz_per_sqrt_second=o.random_walk_fm/std::sqrt(2.);
    channel_cfg.rx_spectrum.random_walk_fm_hz_per_sqrt_second=o.random_walk_fm/std::sqrt(2.);
    channel_cfg.tx_spectrum.flicker_fm_rms_hz=o.flicker_fm/std::sqrt(2.);
    channel_cfg.rx_spectrum.flicker_fm_rms_hz=o.flicker_fm/std::sqrt(2.);
    channel_cfg.tx_spectrum.flicker_min_tau_seconds=channel_cfg.rx_spectrum.flicker_min_tau_seconds=o.flicker_min_tau;
    channel_cfg.tx_spectrum.flicker_max_tau_seconds=channel_cfg.rx_spectrum.flicker_max_tau_seconds=o.flicker_max_tau;
    channel_cfg.tx_spectrum.flicker_bands=channel_cfg.rx_spectrum.flicker_bands=o.flicker_bands;
    channel_cfg.tx_spectrum.white_pm_rms_radians=o.white_pm/std::sqrt(2.);
    channel_cfg.rx_spectrum.white_pm_rms_radians=o.white_pm/std::sqrt(2.);
    channel_cfg.effects.snr_db=0;
    channel_cfg.effects.spread_delay_ms=resolved.spread_delay_ms;
    channel_cfg.effects.spread_doppler_hz=resolved.spread_doppler_hz;
    channel_cfg.effects.perturbation_slow_depth=resolved.slow_depth;
    channel_cfg.effects.perturbation_fast_depth=resolved.fast_depth;
    channel_cfg.complex_noise_variance=noise; channel_cfg.maximum_chunk_samples=o.chunk;
    return channel_cfg;
}

double truth_clock(const StreamChannelConfig& c) {
    if(c.effects.rx.audio_ppm==0)return c.effects.tx.audio_ppm;
    return ((1+c.effects.tx.audio_ppm*1e-6)/(1+c.effects.rx.audio_ppm*1e-6)-1)*1e6;
}
double truth_drift(const StreamChannelConfig& c) {
    const auto rx=1+c.effects.rx.audio_ppm*1e-6;
    return ((c.effects.tx.rf_drift_ppm_per_second-c.effects.rx.rf_drift_ppm_per_second)*c.effects.rf_hz*1e-6+
        c.tx_frequency_drift_hz_per_second)/(rx*rx);
}
double truth_arrival(const StreamChannelConfig& c) {
    return (c.effects.capture_lead_seconds+c.effects.timing_offset_seconds)*(1+c.effects.rx.audio_ppm*1e-6);
}
double truth_frequency(const StreamChannelConfig& c) {
    const auto tx=1+c.effects.tx.audio_ppm*1e-6,rx=1+c.effects.rx.audio_ppm*1e-6;
    if(c.effects.rx.audio_ppm==0&&c.effects.tx.rf_ppm==0&&c.effects.rx.rf_ppm==0)
        return c.effects.static_doppler_hz+c.effects.nominal_conversion_if_hz*c.effects.tx.audio_ppm*1e-6-
            truth_drift(c)*(c.effects.capture_lead_seconds+c.effects.timing_offset_seconds);
    const auto drift=(c.effects.tx.rf_drift_ppm_per_second-c.effects.rx.rf_drift_ppm_per_second)*c.effects.rf_hz*1e-6+
        c.tx_frequency_drift_hz_per_second;
    return (c.effects.static_doppler_hz+c.effects.rf_hz*(c.effects.tx.rf_ppm-c.effects.rx.rf_ppm)*1e-6+
        c.effects.nominal_conversion_if_hz*(tx-rx)-drift*(c.effects.capture_lead_seconds+c.effects.timing_offset_seconds))/rx;
}

struct ObservationCalendar {
    double pad=0,trail=0;
    std::uint64_t samples=0;
    // Conservative channel allocation before constructing any sampled path.
    std::size_t channel_working_bytes=0;
};
ObservationCalendar observation_calendar(const Options& o,const Format& f,const Geometry& g) {
    const auto resolved=resolve_configuration(o);
    const auto [lo,hi]=std::minmax_element(o.clocks_ppm.begin(),o.clocks_ppm.end());
    (void)hi;
    const long double tx_min=resolved.oscillators?1-resolved.base_ppm*.5e-6L:
        (resolved.active?1.L:1+*lo*1e-6L);
    const long double tx_max=resolved.oscillators?1+resolved.base_ppm*.5e-6L:
        (resolved.active?1.L:1+*std::max_element(o.clocks_ppm.begin(),o.clocks_ppm.end())*1e-6L);
    const long double rx_max=resolved.oscillators?1+resolved.base_ppm*.5e-6L:1.L;
    if(!(tx_min>0&&tx_max>0&&rx_max>0))throw std::invalid_argument("public endpoint clock bound is nonpositive");
    const auto fs=effective_sample_rate(f);
    const double physical_fs_max=static_cast<double>(fs*rx_max);
    const long double source_seconds=static_cast<long double>(g.source_samples)/effective_sample_rate(f);
    const auto delay=std::max(resolved.spread_delay_ms*.001,o.basis=="two-ray"?o.tap_delay_ms*.001:0.);
    const auto arrival=initial_arrival_bounds(o,resolved);
    const auto start_low=std::min(o.start_low,arrival[0]);
    const auto start_high=std::max(o.start_high,arrival[1]);
    ObservationCalendar c;c.pad=std::max(0.,-start_low)+.05;
    long double selective_tail=std::ceil(o.selective_tail_floor_seconds*physical_fs_max);
    long double selective_memory=0;
    if(resolved.selective_rate_hz>0&&(resolved.slow_depth>0||resolved.fast_depth>0)) {
        if(o.selective_model=="legacy-allpass") {
            selective_tail=std::max(selective_tail,static_cast<long double>(
                modem::detail::FrequencySelectivePerturbation::decay_samples(
                    physical_fs_max,g.occupied_bandwidth_hz))+1);
            selective_memory=sizeof(modem::detail::FrequencySelectivePerturbation);
        } else {
            const auto spacing=o.selective_scale_hz>0?o.selective_scale_hz:g.carrier_spacing_hz;
            // Validate the bounded helper before any large allocation. A
            // fractional endpoint arbitrarily near an integer may use two
            // extra taps, hence this monotone conservative support bound.
            (void)modem::detail::DispersiveSelectivePerturbation::memory_samples(physical_fs_max,spacing);
            const long double support=std::ceil(2.L*physical_fs_max/(3.L*spacing))+2;
            selective_tail=std::max(selective_tail,support);
            selective_memory=sizeof(modem::detail::DispersiveSelectivePerturbation)+(support+1)*sizeof(Complex);
        }
    }
    const long double interpolation_tail=std::ceil(8*rx_max/tx_min)+2;
    const long double frontend_tail=o.frontend.lowpass_hz>0?
        std::ceil(std::log(1e6L)*physical_fs_max/(2*std::numbers::pi*o.frontend.lowpass_hz)):0;
    const long double complete_tail=std::ceil(delay*physical_fs_max)+selective_tail+
        interpolation_tail+frontend_tail;
    // No second copy of these tails is passed as capture_trail_seconds.
    // The channel receives this public fixed floor, then must fit completely
    // inside it for every seed; hidden physical realizations cannot pick rows.
    c.trail=0;
    const long double end_rows=fs*rx_max*(c.pad+std::max(0.,start_high)+source_seconds/tx_min)+
        complete_tail+2;
    long double gaussian_bytes=0;
    if(o.path_evolution.gaussian_doppler_width_hz>0) {
        const auto radius=std::ceil(5.L*physical_fs_max/(std::numbers::pi*o.path_evolution.gaussian_doppler_width_hz));
        if(2*radius+1>8193)throw std::invalid_argument("Gaussian ray history exceeds its 8193-tap bound");
        gaussian_bytes=(2*radius+1)*(2*sizeof(Complex)+2*sizeof(double))+16384;
    }
    const long double history_samples=std::ceil(resolved.spread_delay_ms*.001L*fs*tx_max)+64;
    const long double channel_bytes=65536+history_samples*sizeof(Complex)+selective_memory+
        gaussian_bytes+static_cast<long double>(o.flicker_bands)*4*sizeof(double)+
        static_cast<long double>(o.missed_dwells.capacity())*sizeof(std::uint64_t);
    if(!std::isfinite(channel_bytes)||channel_bytes>std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument("public channel workspace bound overflows");
    c.channel_working_bytes=static_cast<std::size_t>(std::ceil(channel_bytes));
    if(c.channel_working_bytes>o.memory_bytes)
        throw std::invalid_argument("public channel workspace exceeds --memory-mib before channel allocation");
    // The requested active span is capped separately. Complete private tiles,
    // source pulse support and channel/capture tails can extend that span.
    if(!std::isfinite(end_rows)||end_rows>=UINT64_MAX)throw std::invalid_argument("public capture exceeds the coordinate study guard");
    c.samples=static_cast<std::uint64_t>(std::ceil(end_rows));return c;
}

void json_performance_geometry(std::ostream& out,const Options& o) {
    const auto f=format_for(o);const auto g=geometry(f);const auto calendar=observation_calendar(o,f,g);
    const auto key_seed=o.key_seed.value_or(o.seed);
    auto key=key_for(o.control=="wrong-key"?key_seed^0x6a09e667f3bcc909ULL:key_seed,0);
    const auto candidates=candidates_for(o,f,key);
    std::uint64_t columns=0;
    for(const auto& candidate:candidates) {
        const auto width=candidate.channel.taps.size()*(candidate.channel.linear_gain?2U:1U);
        if(width>UINT64_MAX-columns)throw std::invalid_argument("public candidate basis columns overflow");
        columns+=static_cast<std::uint64_t>(width);
    }
    if(columns&&calendar.samples>UINT64_MAX/columns)
        throw std::invalid_argument("public full-basis work count overflows");
    const auto basis_values=calendar.samples*columns;
    const auto block=block_samples(o,f);
    const auto block_count=calendar.samples/block+(calendar.samples%block?1:0);
    const auto fs=effective_sample_rate(f);
    out<<std::setprecision(17)<<"{\"sample_rate_hz\":"<<fs
       <<",\"fhss_center_aligned\":"<<(f.fhss_center_aligned?"true":"false")
       <<",\"sample_rate_numerator\":"<<f.sample_rate
       <<",\"sample_rate_denominator\":"<<f.sample_rate_denominator
       <<",\"source_samples\":"<<g.source_samples
       <<",\"capture_samples\":"<<calendar.samples
       <<",\"candidate_count\":"<<candidates.size()
       <<",\"total_basis_columns\":"<<columns
       <<",\"basis_values\":"<<basis_values
       <<",\"block_samples\":"<<block
       <<",\"block_count\":"<<block_count
       <<",\"maximum_chunk_samples\":"<<o.chunk
       <<",\"threads_requested\":"<<o.threads
       <<",\"memory_limit_bytes\":"<<o.memory_bytes
       <<",\"channel_working_bytes_bound\":"<<calendar.channel_working_bytes
       <<",\"channel\":";json_string(out,o.channel);
    out<<",\"basis_model\":";json_string(out,o.basis);
    out<<",\"arithmetic_used\":\"fp64\""
       <<",\"geometry_scope\":\"public deterministic full-calendar row and basis-work estimate; configuration creates no sampled observation or receiver workspace\"}";
}

void run(const Options& o) {
    const auto base=format_for(o); const auto nominal=geometry(base);
    const auto calendar=observation_calendar(o,base,nominal);
    const auto pad=calendar.pad,trail=calendar.trail;
    const auto noise=static_cast<double>(effective_sample_rate(base))/std::pow(10.,o.cn0/10.);
    if (!std::isfinite(noise) || noise<=0) throw std::invalid_argument("C/N0 is outside numeric range");
    std::size_t total_correct=0,total_wrong=0,total_missed=0,total_false=0;
    for (unsigned trial=0;trial<o.trials;++trial) {
        const auto trial_started=Clock::now();const auto trial_cpu=std::clock();
        const auto seed=o.seed+trial,key_seed=o.key_seed.value_or(o.seed)+trial,channel_seed=o.channel_seed.value_or(o.seed)+trial; auto txkey=key_for(key_seed,0); auto rxkey=o.control=="wrong-key"?key_for(key_seed^0x6a09e667f3bcc909ULL,0):key_for(key_seed,0);
        auto txf=transmitter_format(o,base); txf.symbol_ordinal=0;
        PatternSource source(txf,txkey,o.signal_bit);
        auto channel_cfg=channel_config(o,base,nominal,channel_seed,noise,pad,trail,calendar.samples);
        const auto true_clock=truth_clock(channel_cfg),true_drift=truth_drift(channel_cfg);
        StreamingChannel channel(source,channel_cfg);
        auto candidate_bank=candidates_for(o,base,rxkey);
        for (auto& candidate : candidate_bank) candidate.arrival_seconds += pad;
        ReceiverConfig rc; rc.format=base; rc.candidates=std::move(candidate_bank);
        rc.observation_first_sample=0; rc.observation_samples=calendar.samples;
        if(channel.total_samples()!=calendar.samples)throw std::runtime_error("natural sampled channel endpoint exceeds public observation calendar");
        rc.block_samples=block_samples(o,base); rc.maximum_chunk_samples=o.chunk; rc.threads=o.threads;
        const long double caller_bytes=source.working_bytes()+static_cast<long double>(channel.report().working_bytes)+
            o.chunk*(sizeof(Complex)+sizeof(std::uint8_t))+65536;
        if(caller_bytes>=o.memory_bytes)throw std::invalid_argument("source/channel exceed pipeline --memory-mib");
        rc.memory_limit_bytes=o.memory_bytes-static_cast<std::size_t>(caller_bytes); rc.complex_noise_variance=noise;
        rc.known_white_noise=channel.report().gaussian_projection_supported;
        rc.log_family_hypotheses=std::log(static_cast<double>(rc.candidates.size()))+
            std::log(std::ceil(o.horizon/o.attempt_spacing))+std::log(static_cast<double>(o.duration_alternatives));
        rc.family_false_admission_probability=o.false_probability;
        rc.minimum_observed_fraction=o.minimum_observed_fraction;
        StreamingReceiver receiver(rc,rxkey);
        std::vector<Complex> samples(o.chunk); std::vector<std::uint8_t> validity(o.chunk);
        double setup_seconds=0,channel_read_seconds=0,receiver_push_seconds=0,receiver_finish_seconds=0;
        std::uint64_t measured_chunks=0,measured_rows=0,minimum_chunk_samples=UINT64_MAX,maximum_chunk_samples=0;
        if(o.performance_trace)
            setup_seconds=std::chrono::duration<double>(Clock::now()-trial_started).count();
        auto last_progress=Clock::now();
        while(channel.position()<calendar.samples) {
            const auto wanted=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),calendar.samples-channel.position()));
            const auto read_started=o.performance_trace?Clock::now():Clock::time_point{};
            const auto count=channel.read(std::span(samples).first(wanted),std::span(validity).first(wanted));
            if(o.performance_trace)
                channel_read_seconds+=std::chrono::duration<double>(Clock::now()-read_started).count();
            if (!count) break;
            const auto chunk_first=channel.position()-count;
            const auto push_started=o.performance_trace?Clock::now():Clock::time_point{};
            receiver.push(chunk_first,std::span<const Complex>(samples.data(),count),std::span<const std::uint8_t>(validity.data(),count));
            if(o.performance_trace) {
                receiver_push_seconds+=std::chrono::duration<double>(Clock::now()-push_started).count();
                ++measured_chunks;measured_rows+=count;
                minimum_chunk_samples=std::min<std::uint64_t>(minimum_chunk_samples,count);
                maximum_chunk_samples=std::max<std::uint64_t>(maximum_chunk_samples,count);
            }
            const auto now=Clock::now();
            if(now-last_progress>=std::chrono::seconds(2)) {
                std::cerr<<"trial="<<trial<<" source_seconds="<<std::fixed<<std::setprecision(1)
                         <<static_cast<double>(channel.position())/effective_sample_rate(base)<<"/"
                         <<static_cast<double>(calendar.samples)/effective_sample_rate(base)<<" working_bytes="
                         <<(source.working_bytes()+channel.report().working_bytes+receiver.working_bytes())<<"\n";
                last_progress=now;
            }
        }
        const auto finish_started=o.performance_trace?Clock::now():Clock::time_point{};
        auto report=receiver.finish(); const auto got=report.source_bit;
        if(o.performance_trace)
            receiver_finish_seconds=std::chrono::duration<double>(Clock::now()-finish_started).count();
        const bool positive=o.control=="signal";
        const bool correct_epoch=rc.candidates[report.best_candidate].epoch_offset_seconds==o.truth_epoch_offset;
        const bool correct=report.accepted&&got==o.signal_bit&&correct_epoch&&positive;
        const bool wrong=report.accepted&&(got!=o.signal_bit||!correct_epoch)&&positive;
        const bool missed=positive&&!report.accepted;
        const bool false_admission=report.accepted&&!positive;
        total_correct+=correct;total_wrong+=wrong;total_missed+=missed;total_false+=false_admission;
        const auto pipeline_wall=std::chrono::duration<double>(Clock::now()-trial_started).count();
        const auto pipeline_cpu=static_cast<double>(std::clock()-trial_cpu)/CLOCKS_PER_SEC;
        std::cout<<std::setprecision(12)<<"{\"mode\":\"run\",\"version\":3,\"trial\":"<<trial
            <<",\"seed\":"<<seed<<",\"key_seed\":"<<key_seed<<",\"channel_seed\":"<<channel_seed<<",\"epoch\":"<<base.epoch<<",\"control\":\""<<o.control
            <<"\",\"channel\":\""<<channel_name(o)<<"\",\"cn0_db_hz\":"<<o.cn0
            <<",\"complex_noise_variance\":"<<noise<<",\"noise_reference_power\":1,\"sample_rate\":"<<effective_sample_rate(base)
            <<",\"occupied_bandwidth_hz\":"<<nominal.occupied_bandwidth_hz
            <<",\"requested_active_seconds\":"<<base.bit_seconds<<",\"bit_seconds\":"<<nominal.bit_seconds<<",\"inner_rate_hz\":"<<base.inner_rate_hz
            <<",\"added_symbols\":"<<base.added_symbols<<",\"fine_rate_hz\":"<<nominal.fine_chip_rate_hz
            <<",\"fhss_center_aligned\":"<<(base.fhss_center_aligned?"true":"false")
            <<",\"rf_hz\":"<<o.rf_hz<<",\"truth_arrival_seconds\":"<<truth_arrival(channel_cfg)
            <<",\"truth_frequency_hz\":"<<truth_frequency(channel_cfg)
            <<",\"sample_clock_reference\":";
        json_string(std::cout,resolve_configuration(o).oscillators?"independent TX/RX endpoint errors; truth_clock_ppm reports their exact ratio":"RX0ppm; TX carries declared relative error");
        std::cout<<",\"truth_clock_ppm\":"<<true_clock<<",\"truth_frequency_drift_hz_per_second\":"<<true_drift
            <<",\"basis_model\":\""<<o.basis<<"\",\"tap_delay_ms\":"<<o.tap_delay_ms
            <<",\"tap_doppler_hz\":"<<o.tap_doppler_hz
            <<",\"relative_phase_diffusion_rad_sqrt_s\":"<<resolve_configuration(o).phase_diffusion
            <<",\"relative_white_fm_hz_sqrt_s\":"<<o.white_fm
            <<",\"relative_random_walk_fm_hz_sqrt_s\":"<<o.random_walk_fm
            <<",\"relative_flicker_fm_rms_hz\":"<<o.flicker_fm
            <<",\"flicker_tau_seconds\":["<<o.flicker_min_tau<<','<<o.flicker_max_tau<<']'
            <<",\"relative_white_pm_rms_rad\":"<<o.white_pm
            <<",\"dropout_ms\":"<<o.dropout_ms<<",\"dropout_start_ms\":"<<o.dropout_start_ms
            <<",\"dropout_every_dwells\":"<<o.dropout_every_dwells
            <<",\"fine_cells\":"<<nominal.fine_cells<<",\"source_samples\":"<<nominal.source_samples
            <<",\"capture_samples\":"<<calendar.samples<<",\"candidate_count\":"<<rc.candidates.size()
            <<",\"truth_bit\":"<<static_cast<unsigned>(o.signal_bit)<<",\"accepted\":"<<(report.accepted?"true":"false")
            <<",\"presence\":"<<(report.presence?"true":"false")<<",\"complete_observation\":"<<(report.complete_observation?"true":"false")
            <<",\"correct\":"<<(correct?"true":"false")<<",\"wrong_accepted\":"<<(wrong?"true":"false")<<",\"missed\":"<<(missed?"true":"false")<<",\"false_admission\":"<<(false_admission?"true":"false")<<",\"decoded_bit\":"<<static_cast<unsigned>(got)
            <<",\"score\":"<<report.score<<",\"threshold\":"<<report.threshold<<",\"margin\":"<<report.margin
            <<",\"analytical_null_supported\":"<<(report.analytic_null_supported?"true":"false")
            <<",\"uncalibrated_candidate_pass\":"<<(!report.analytic_null_supported&&report.presence&&report.margin>0?"true":"false")
            <<",\"all_samples_observed\":"<<(report.all_samples_observed?"true":"false")
            <<",\"missing_samples\":"<<report.missing_samples<<",\"blocks\":"<<report.blocks
            <<",\"basis_values\":"<<report.basis_values<<",\"working_bytes\":"<<report.working_bytes
            <<",\"pipeline_working_bytes_bound\":"<<report.working_bytes+static_cast<std::size_t>(caller_bytes)
            <<",\"end_to_end_wall_seconds\":"<<pipeline_wall<<",\"end_to_end_cpu_seconds\":"<<pipeline_cpu
            <<",\"coherent_block_samples\":"<<rc.block_samples<<",\"burst_wait_tiles\":"<<o.burst_wait_tiles
            <<",\"nominal_payload_energy_seconds_lower\":"<<nominal.nominal_payload_energy_seconds
            <<",\"signal_energy_pre_erasure_samples\":"<<channel.report().signal_energy<<",\"signal_energy\":"<<channel.report().signal_energy<<",\"observed_energy\":"<<channel.report().observed_energy
            <<",\"channel_samples\":"<<channel.report().samples<<",\"channel_tail_samples\":"<<channel.report().tail_samples
            <<",\"threads_used\":"<<report.threads_used<<",\"wall_seconds\":"<<report.wall_seconds
            <<",\"cpu_seconds\":"<<report.cpu_seconds;
        if(o.performance_trace) {
            const double stage_sum=setup_seconds+channel_read_seconds+receiver_push_seconds+receiver_finish_seconds;
            const double other_seconds=std::max(0.,pipeline_wall-stage_sum);
            const auto arithmetic=report.arithmetic_used==Arithmetic::fp32?"fp32":
                report.arithmetic_used==Arithmetic::fp64?"fp64":"automatic";
            std::cout<<",\"performance_trace\":{\"schema\":\"triad-native-run-timing-v1\""
                <<",\"stage_seconds\":{\"setup\":"<<setup_seconds
                <<",\"channel_read\":"<<channel_read_seconds
                <<",\"receiver_push\":"<<receiver_push_seconds
                <<",\"receiver_finish\":"<<receiver_finish_seconds
                <<",\"other\":"<<other_seconds<<'}'
                <<",\"stage_reconciliation_wall_seconds\":"<<stage_sum+other_seconds
                <<",\"chunk_count\":"<<measured_chunks
                <<",\"minimum_chunk_samples\":"<<(measured_chunks?minimum_chunk_samples:0)
                <<",\"maximum_chunk_samples\":"<<maximum_chunk_samples
                <<",\"processed_rows\":"<<measured_rows
                <<",\"candidate_count\":"<<rc.candidates.size()
                <<",\"basis_values\":"<<report.basis_values
                <<",\"scored_blocks\":"<<report.blocks
                <<",\"channel_samples\":"<<channel.report().samples
                <<",\"channel_tail_samples\":"<<channel.report().tail_samples
                <<",\"missing_samples\":"<<report.missing_samples
                <<",\"complete_observation\":"<<(report.complete_observation?"true":"false")
                <<",\"threads_used\":"<<report.threads_used
                <<",\"arithmetic_used\":\""<<arithmetic<<"\""
                <<",\"receiver_timing_crosscheck\":{\"push_plus_finish_seconds\":"
                <<receiver_push_seconds+receiver_finish_seconds<<",\"receiver_report_wall_seconds\":"
                <<report.wall_seconds<<"}}";
        }
        json_provenance(std::cout,o,base,nominal,trial);
        json_endpoint_truth(std::cout,channel_cfg);
        json_frontend_report(std::cout,channel.report());
        std::cout<<",\"candidates\":[";
        for(std::size_t j=0;j<rc.candidates.size();++j){if(j)std::cout<<',';json_candidate(std::cout,rc.candidates[j]);}
        std::cout<<"]}\n";
    }
    std::cerr<<"summary correct="<<total_correct<<" wrong_accepted="<<total_wrong<<" missed="<<total_missed<<" control_false_admissions="<<total_false<<"\n";
}

std::uint64_t offset_epoch(std::uint64_t epoch,std::int64_t offset) {
    if(offset<0){const auto magnitude=static_cast<std::uint64_t>(-(offset+1))+1;
        if(magnitude>epoch)throw std::invalid_argument("epoch hypothesis underflow");
        return epoch-magnitude;}
    if(static_cast<std::uint64_t>(offset)>UINT64_MAX-epoch)throw std::invalid_argument("epoch hypothesis overflow");
    return epoch+static_cast<std::uint64_t>(offset);
}

void export_basis(const Options& o) {
    if(o.output.empty())throw std::invalid_argument("--output directory is required for --mode export");
    if(o.dropout_ms>0||!o.missed_dwells.empty())throw std::invalid_argument("GPU export with erased observations is unsupported; use run or validity-aware sampled/statistics exports for selected erasures");
    const auto f=format_for(o);const auto g=geometry(f);
    const auto key_seed=o.key_seed.value_or(o.seed),channel_seed=o.channel_seed.value_or(o.seed);
    const auto txkey=key_for(key_seed,0);
    const auto rxkey=o.control=="wrong-key"?key_for(key_seed^0x6a09e667f3bcc909ULL,0):key_for(key_seed,0);
    auto candidates=candidates_for(o,f,rxkey);
    const auto calendar=observation_calendar(o,f,g);
    const auto pad=calendar.pad,trail=calendar.trail;
    const auto noise=static_cast<double>(effective_sample_rate(f))/std::pow(10.,o.cn0/10.);
    if(!std::isfinite(noise)||noise<=0)throw std::invalid_argument("C/N0 is outside numeric range");
    auto config=channel_config(o,f,g,channel_seed,noise,pad,trail,calendar.samples);config.effects.noise_enabled=false;
    PatternSource truth(transmitter_format(o,f),txkey,o.signal_bit);StreamingChannel channel(truth,config);
    for(auto& c:candidates)c.arrival_seconds+=pad;
    const auto rows=calendar.samples;
    if(channel.total_samples()!=rows)throw std::runtime_error("natural channel endpoint exceeds public export calendar");
    if(rows>o.export_max_samples)throw std::invalid_argument("export exceeds --export-max-samples (at most128000000)");
    std::size_t columns=0;
    for(const auto& c:candidates)columns=std::max(columns,c.channel.taps.size()*(c.channel.linear_gain?2:1));
    const long double total_bytes=(static_cast<long double>(candidates.size())*columns+1)*rows*sizeof(std::complex<float>);
    if(total_bytes>o.export_max_bytes)throw std::invalid_argument("export exceeds explicit --export-max-mib disk budget");
    const auto scratch_bytes=static_cast<long double>(o.chunk)*(columns*(sizeof(Complex)+sizeof(std::complex<float>))+sizeof(Complex))+65536;
    if(scratch_bytes+2*g.source_workspace_bytes+channel.report().working_bytes>o.memory_bytes)
        throw std::invalid_argument("export scratch and sources exceed --memory-mib");
    std::filesystem::create_directories(o.output);
    const auto directory=std::filesystem::path(o.output);
    for(const auto* name:{"basis.c64","signal.c64","manifest.json"})
        if(std::filesystem::exists(directory/name))throw std::invalid_argument("export files already exist; choose a fresh output directory");
    std::ofstream basis(directory/"basis.c64.partial",std::ios::binary),signal(directory/"signal.c64.partial",std::ios::binary);
    if(!basis||!signal)throw std::runtime_error("cannot create export files");
    std::vector<Complex> signal_chunk(o.chunk);
    std::vector<std::complex<float>> packed(o.chunk*columns);
    auto pack=[](Complex v){
        if(!std::isfinite(v.real())||!std::isfinite(v.imag())||std::abs(v.real())>std::numeric_limits<float>::max()||std::abs(v.imag())>std::numeric_limits<float>::max())
            throw std::runtime_error("export value is not representable as FP32");
        return std::complex<float>{static_cast<float>(v.real()),static_cast<float>(v.imag())};};
    while(channel.position()<rows){const auto wanted=static_cast<std::size_t>(std::min<std::uint64_t>(signal_chunk.size(),rows-channel.position()));
        const auto n=channel.read(std::span(signal_chunk).first(wanted));
        if(!n)throw std::runtime_error("channel ended before declared export extent");
        for(std::size_t j=0;j<n;++j)packed[j]=pack(signal_chunk[j]);
        signal.write(reinterpret_cast<const char*>(packed.data()),static_cast<std::streamsize>(n*sizeof(packed[0])));
    }
    std::size_t candidate_index=0;
    for(const auto& c:candidates){
        auto addressed=f;addressed.epoch=offset_epoch(f.epoch,c.epoch_offset_seconds);
        PatternSource candidate(addressed,rxkey,c.source_bit);
        const auto actual_columns=c.channel.taps.size()*(c.channel.linear_gain?2:1);
        std::vector<Complex> values(o.chunk*actual_columns);
        for(std::uint64_t at=0;at<rows;){const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,rows-at));
            shaped_basis(candidate,c,effective_sample_rate(f),at,n,std::span(values).first(n*actual_columns));
            for(std::size_t j=0;j<n;++j)for(std::size_t k=0;k<columns;++k)
                packed[j*columns+k]=k<actual_columns?pack(values[k*n+j]):std::complex<float>{};
            basis.write(reinterpret_cast<const char*>(packed.data()),static_cast<std::streamsize>(n*columns*sizeof(packed[0])));at+=n;
        }
        std::cerr<<"export candidate="<<++candidate_index<<'/'<<candidates.size()<<" samples="<<rows<<" columns="<<columns<<'\n';
    }
    basis.close();signal.close();if(!basis||!signal)throw std::runtime_error("failed writing GPU study files");
    std::ofstream manifest(directory/"manifest.json.partial");
    manifest<<std::setprecision(17)<<"{\"format\":\"triad-long-basis-v1\",\"byte_order\":\"native\",\"complex_type\":\"complex64\",\"layout\":\"candidate,sample,column\",\"basis_file\":\"basis.c64\",\"basis_shape\":["<<candidates.size()<<','<<rows<<','<<columns
        <<"],\"signal_file\":\"signal.c64\",\"signal_shape\":["<<rows<<"],\"sample_rate\":"<<effective_sample_rate(f)
        <<",\"inner_rate_hz\":"<<f.inner_rate_hz<<",\"fine_rate_hz\":"<<g.fine_chip_rate_hz
        <<",\"carrier_spacing_hz\":"<<g.carrier_spacing_hz<<",\"occupied_bandwidth_hz\":"<<g.occupied_bandwidth_hz
        <<",\"added_symbols\":"<<f.added_symbols<<",\"encrypted\":"<<(f.encrypt?"true":"false")
        <<",\"cn0_db_hz\":"<<o.cn0<<",\"noise_reference_power\":1,\"noise_variance\":"<<noise
        <<",\"signal_start_samples\":0,\"signal_bit\":"<<static_cast<unsigned>(o.signal_bit)
        <<",\"seed\":"<<o.seed<<",\"key_seed\":"<<key_seed<<",\"channel_seed\":"<<channel_seed<<",\"epoch\":"<<f.epoch<<",\"source_samples\":"<<g.source_samples
        <<",\"requested_active_seconds\":"<<f.bit_seconds<<",\"actual_elapsed_seconds\":"<<g.bit_seconds
        <<",\"nominal_payload_energy_seconds_lower\":"<<g.nominal_payload_energy_seconds
        <<",\"channel_signal_energy_pre_erasure_samples\":"<<channel.report().signal_energy
        <<",\"channel_signal_energy_samples\":"<<channel.report().signal_energy
        <<",\"channel_observed_energy_samples\":"<<channel.report().observed_energy
        <<",\"channel\":\""<<channel_name(o)<<"\",\"control\":\""<<o.control
        <<"\",\"truth_arrival_seconds\":"<<truth_arrival(config)
        <<",\"truth_frequency_hz\":"<<truth_frequency(config)<<",\"truth_clock_ppm\":"<<truth_clock(config)
        <<",\"truth_frequency_drift_hz_per_second\":"<<truth_drift(config)
        <<",\"relative_phase_diffusion_rad_sqrt_s\":"<<resolve_configuration(o).phase_diffusion
        <<",\"relative_white_fm_hz_sqrt_s\":"<<o.white_fm<<",\"relative_random_walk_fm_hz_sqrt_s\":"<<o.random_walk_fm
        <<",\"relative_flicker_fm_rms_hz\":"<<o.flicker_fm<<",\"relative_white_pm_rms_rad\":"<<o.white_pm
        <<",\"flicker_tau_seconds\":["<<o.flicker_min_tau<<','<<o.flicker_max_tau<<"],\"flicker_bands\":"<<o.flicker_bands
        <<",\"burst_wait_tiles\":"<<o.burst_wait_tiles<<",\"fhss_envelope_only\":"<<(o.fhss?"true":"false")
        <<",\"fhss_center_aligned\":"<<(f.fhss_center_aligned?"true":"false")
        <<",\"export_bytes\":"<<static_cast<std::uint64_t>(total_bytes)<<",\"bitlabels\":[";
    for(std::size_t i=0;i<candidates.size();++i){if(i)manifest<<',';manifest<<static_cast<unsigned>(candidates[i].source_bit);}
    manifest<<"],\"candidateparams\":[";
    for(std::size_t i=0;i<candidates.size();++i){if(i)manifest<<',';json_candidate(manifest,candidates[i]);}
    manifest<<"],\"message\":";
    json_string(manifest,o.frontend.active()?"sampled no-thermal channel pass including selected converter noise and interference; templates use only declared public channel bases":"noiseless sampled channel realization; templates use only declared public channel bases");
    json_provenance(manifest,o,f,g);
    json_endpoint_truth(manifest,config);
    json_frontend_report(manifest,channel.report());
    // This optional diagnostic label must not tell the detector hidden channel
    // truth. Only flat deterministic truth lying on the public grid gets one.
    // Independent interpolation and direct template evaluation may still differ.
    if(!o.frontend.active()&&!path_evolution_active(o)&&o.control=="signal"&&o.channel=="none"&&o.phase_diffusion==0&&o.white_fm==0&&o.random_walk_fm==0&&o.flicker_fm==0&&o.white_pm==0&&truth_clock(config)==0&&truth_drift(config)==0){
        for(std::size_t i=0;i<candidates.size();++i){const auto& c=candidates[i];
            if(c.source_bit==o.signal_bit&&c.epoch_offset_seconds==o.truth_epoch_offset&&c.clock_ppm==0&&c.frequency_drift_hz_per_second==0&&
               c.arrival_seconds==truth_arrival(config)&&c.frequency_hz==truth_frequency(config)&&c.channel.taps.size()==1){manifest<<",\"true_candidate_index\":"<<i<<",\"true_candidate_note\":\"nominal parameter match; sampled channel interpolation is independently evaluated\"";break;}}
    }
    manifest<<"}\n";manifest.close();if(!manifest)throw std::runtime_error("failed writing manifest");
    std::filesystem::rename(directory/"basis.c64.partial",directory/"basis.c64");
    std::filesystem::rename(directory/"signal.c64.partial",directory/"signal.c64");
    std::filesystem::rename(directory/"manifest.json.partial",directory/"manifest.json");
}

void export_source_window(const Options& o) {
    if(o.output.empty())throw std::invalid_argument("--output directory is required for --mode export-source-window");
    if(!(std::isfinite(o.source_window_seconds)&&o.source_window_seconds>0))
        throw std::invalid_argument("--source-window-seconds must be finite and positive");
    if(o.source_window_center&&o.source_first_sample)
        throw std::invalid_argument("--source-window-center cannot be combined with --source-first-sample");
    if(o.control!="signal")throw std::invalid_argument("source-window export supports --control signal only; candidate arrays always contain public bit 0 and bit 1 sources");
    // This mode exports the exact maintained keyed source before propagation.
    // FHSS and Symbol_Burst are Format-level source envelope/calendar features
    // and remain enabled. Channel, oscillator, converter and erasure effects
    // are intentionally not applied and therefore must not be selected.
    const bool channel_effect=(o.channel!="none")||o.base_oscillator||o.shift_oscillator||o.clock_sync_text||
        o.spread_preset||(o.spread_delay_ms&&*o.spread_delay_ms!=0)||(o.spread_doppler_hz&&*o.spread_doppler_hz!=0)||
        (o.selective_perturbation&&*o.selective_perturbation!="off")||
        (o.selective_modulation_rate_hz&&*o.selective_modulation_rate_hz!=0)||o.audio_error_explicit||
        (o.margin_explicit&&o.margin!=1)||o.phase_diffusion_explicit&&o.phase_diffusion!=0||
        o.white_fm!=0||o.random_walk_fm!=0||o.flicker_fm!=0||o.white_pm!=0||path_evolution_active(o)||
        o.dropout_ms!=0||!o.missed_dwells.empty()||o.frontend.active()||o.truth_epoch_offset!=0||
        o.truth_frequency_hz!=0||o.truth_frequency_drift!=0;
    if(channel_effect)
        throw std::invalid_argument("export-source-window is source-only; remove propagation/oscillator/frontend/erasure selections or use a propagated export mode");
    const auto f=format_for(o);const auto g=geometry(f);const double fs=effective_sample_rate(f);
    const long double requested=std::ceil(static_cast<long double>(o.source_window_seconds)*fs);
    if(!std::isfinite(static_cast<double>(requested))||requested<1||requested>UINT64_MAX)
        throw std::invalid_argument("source window sample count exceeds numeric range");
    const auto rows=static_cast<std::uint64_t>(requested);
    if(rows>o.export_max_samples)throw std::invalid_argument("source window exceeds --export-max-samples");
    const auto payload_begin=g.calendar_origin_sample;
    const auto payload_end=g.source_samples-g.calendar_origin_sample;
    if(payload_end<payload_begin||rows>payload_end-payload_begin)
        throw std::invalid_argument("source window is longer than the active payload; lower --source-window-seconds or increase --seconds");
    std::uint64_t first=payload_begin;
    if(o.source_first_sample)first=*o.source_first_sample;
    else if(o.source_window_center)first=payload_begin+(payload_end-payload_begin-rows)/2;
    if(first>g.source_samples||rows>g.source_samples-first)
        throw std::invalid_argument("source window [first,first+count) exceeds complete source geometry");
    const long double output_bytes=static_cast<long double>(rows)*3*sizeof(std::complex<float>);
    if(output_bytes>o.export_max_bytes)throw std::invalid_argument("source window exceeds explicit --export-max-mib disk budget");
    const long double scratch=static_cast<long double>(o.chunk)*(sizeof(Complex)+sizeof(std::complex<float>))+65536;
    if(scratch+g.source_workspace_bytes>o.memory_bytes)
        throw std::invalid_argument("source-window render scratch exceeds --memory-mib");

    const auto directory=std::filesystem::path(o.output);std::filesystem::create_directories(directory);
    for(const auto* name:{"source_candidates.c64","signal.c64","manifest.json"}){
        if(std::filesystem::exists(directory/name)||std::filesystem::exists(directory/(std::string(name)+".partial")))
            throw std::invalid_argument("source-window final or partial files already exist; choose a fresh output directory");
    }
    std::ofstream candidates(directory/"source_candidates.c64.partial",std::ios::binary);
    std::ofstream signal(directory/"signal.c64.partial",std::ios::binary);
    if(!candidates||!signal)throw std::runtime_error("cannot create source-window export files");
    std::vector<Complex> values(o.chunk);std::vector<std::complex<float>> packed(o.chunk);
    const auto pack=[](Complex v){
        if(!std::isfinite(v.real())||!std::isfinite(v.imag())||std::abs(v.real())>std::numeric_limits<float>::max()||std::abs(v.imag())>std::numeric_limits<float>::max())
            throw std::runtime_error("source-window sample is not representable as FP32");
        return std::complex<float>{static_cast<float>(v.real()),static_cast<float>(v.imag())};};
    const auto txkey=key_for(o.key_seed.value_or(o.seed),0);
    const auto candidate_key=txkey;
    auto render=[&](PatternSource& source,std::ofstream& out){
        std::uint64_t done=0;
        while(done<rows){const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,rows-done));
            source.render(first+done,std::span(values).first(n));
            for(std::size_t j=0;j<n;++j)packed[j]=pack(values[j]);
            out.write(reinterpret_cast<const char*>(packed.data()),static_cast<std::streamsize>(n*sizeof(packed[0])));
            if(!out)throw std::runtime_error("failed writing source-window samples");
            done+=n;
        }
    };
    {
        PatternSource truth(transmitter_format(o,f),txkey,o.signal_bit);
        render(truth,signal);
    }
    for(std::uint8_t bit=0;bit<2;++bit){PatternSource candidate(f,candidate_key,bit);render(candidate,candidates);}
    candidates.close();signal.close();if(!candidates||!signal)throw std::runtime_error("failed finalizing source-window sample files");
    std::ofstream manifest(directory/"manifest.json.partial");
    manifest<<std::setprecision(17)<<"{\"format\":\"triad-source-window-v1\",\"byte_order\":\"native\",\"complex_type\":\"complex64\",\"layout\":\"candidate,sample\",\"source_candidates_file\":\"source_candidates.c64\",\"source_candidates_shape\":[2,"<<rows
        <<"],\"signal_file\":\"signal.c64\",\"signal_shape\":["<<rows<<"],\"partial_source_window\":true,\"source_only\":true,\"channel_applied\":false,\"propagated_waveform\":false"
        <<",\"candidate_model\":\"nominal-flat-keyed-source\",\"channel_model\":\"none-source-only\",\"source_origin_sample\":0"
        <<",\"sample_rate\":"<<fs<<",\"inner_rate_hz\":"<<f.inner_rate_hz<<",\"fine_rate_hz\":"<<g.fine_chip_rate_hz
        <<",\"carrier_spacing_hz\":"<<g.carrier_spacing_hz<<",\"occupied_bandwidth_hz\":"<<g.occupied_bandwidth_hz
        <<",\"added_symbols\":"<<f.added_symbols<<",\"encrypted\":"<<(f.encrypt?"true":"false")
        <<",\"seed\":"<<o.seed<<",\"key_seed\":"<<o.key_seed.value_or(o.seed)<<",\"epoch\":"<<f.epoch
        <<",\"full_source_samples\":"<<g.source_samples<<",\"full_calendar_seconds\":"<<static_cast<double>(g.source_samples)/fs
        <<",\"requested_active_seconds\":"<<f.bit_seconds<<",\"minimum_pattern_cells\":"<<f.minimum_inner_coefficients
        <<",\"source_fraction\":"<<static_cast<double>(rows)/g.source_samples<<",\"first_sample\":"<<first
        <<",\"window_samples\":"<<rows<<",\"window_seconds\":"<<static_cast<double>(rows)/fs
        <<",\"active_payload_first_sample\":"<<payload_begin<<",\"active_payload_end_sample_exclusive\":"<<payload_end
        <<",\"signal_bit\":"<<static_cast<unsigned>(o.signal_bit)<<",\"candidate_bits\":[0,1]"
        <<",\"fhss_envelope_only\":"<<(f.fhss?"true":"false")<<",\"burst_wait_tiles\":"<<f.burst_wait_tiles
        <<",\"fhss_center_aligned\":"<<(f.fhss_center_aligned?"true":"false")
        <<",\"export_bytes\":"<<static_cast<std::uint64_t>(output_bytes)<<",\"note\":\"Source samples at original absolute sample coordinates; no skipped prefix was rendered. Channel, oscillator, converter, noise, and erasure effects are absent. Full propagated waveform validation requires export/export-sampled.\"}\n";
    manifest.close();if(!manifest)throw std::runtime_error("failed writing source-window manifest");
    std::filesystem::rename(directory/"source_candidates.c64.partial",directory/"source_candidates.c64");
    std::filesystem::rename(directory/"signal.c64.partial",directory/"signal.c64");
    std::filesystem::rename(directory/"manifest.json.partial",directory/"manifest.json");
}

struct ColumnRef { std::size_t candidate=0, local=0; };
using ColumnKey=std::string;
void key_part(ColumnKey& k,const auto& value) {
    k.append(reinterpret_cast<const char*>(&value),sizeof(value));
}
ColumnKey column_key(const Candidate& c,std::size_t local) {
    const auto taps=c.channel.taps.size();
    const auto tap_index=local/(c.channel.linear_gain?2:1);
    const auto gain_index=c.channel.linear_gain?local%2:0;
    const auto& tap=c.channel.taps[tap_index];
    ColumnKey k;
    key_part(k,c.source_bit); key_part(k,c.epoch_offset_seconds);
    key_part(k,c.arrival_seconds); key_part(k,c.frequency_hz); key_part(k,c.clock_ppm);
    key_part(k,c.frequency_drift_hz_per_second); key_part(k,tap.delay_seconds); key_part(k,tap.doppler_hz);
    key_part(k,c.channel.linear_gain); key_part(k,gain_index);
    if(c.channel.linear_gain){key_part(k,c.channel.gain_reference_seconds);key_part(k,c.channel.gain_scale_seconds);}
    (void)taps;
    return k;
}
void write_le_u64(std::ostream& out,std::uint64_t v) {
    std::array<unsigned char,8> b{}; for(unsigned i=0;i<8;++i)b[i]=static_cast<unsigned char>(v>>(8*i));
    out.write(reinterpret_cast<const char*>(b.data()),b.size());
}
void write_le_f64(std::ostream& out,double v) { write_le_u64(out,std::bit_cast<std::uint64_t>(v)); }
void write_le_complex(std::ostream& out,Complex v) { write_le_f64(out,v.real());write_le_f64(out,v.imag()); }

void export_sampled(const Options& o) {
    if(o.output.empty())throw std::invalid_argument("--output directory is required for --mode export-sampled");
    const auto f=format_for(o);const auto g=geometry(f);
    const auto key_seed=o.key_seed.value_or(o.seed),channel_seed=o.channel_seed.value_or(o.seed);
    const auto txkey=key_for(key_seed,0);
    const auto rxkey=o.control=="wrong-key"?key_for(key_seed^0x6a09e667f3bcc909ULL,0):key_for(key_seed,0);
    auto candidates=candidates_for(o,f,rxkey);
    const auto calendar=observation_calendar(o,f,g);const auto pad=calendar.pad,trail=calendar.trail;
    const auto noise=static_cast<double>(effective_sample_rate(f))/std::pow(10.,o.cn0/10.);
    if(!std::isfinite(noise)||noise<=0)throw std::invalid_argument("C/N0 is outside numeric range");
    auto config=channel_config(o,f,g,channel_seed,noise,pad,trail,calendar.samples);
    config.effects.noise_enabled=true;
    PatternSource truth(transmitter_format(o,f),txkey,o.signal_bit);StreamingChannel channel(truth,config);
    for(auto& c:candidates)c.arrival_seconds+=pad;
    const auto rows=calendar.samples;
    if(channel.total_samples()!=rows)throw std::runtime_error("natural channel endpoint exceeds public sampled-export calendar");
    if(rows>o.export_max_samples)throw std::invalid_argument("export-sampled exceeds --export-max-samples (at most128000000)");
    std::size_t columns=0,max_local=0;
    for(const auto& c:candidates){
        const auto n=c.channel.taps.size()*(c.channel.linear_gain?2:1);
        columns=std::max(columns,n);max_local=std::max(max_local,n);
    }
    const long double complex_values=(static_cast<long double>(candidates.size())*columns+1)*rows;
    const long double total_bytes=complex_values*16+rows;
    if(total_bytes>o.export_max_bytes)throw std::invalid_argument("export-sampled exceeds explicit --export-max-mib disk budget");
    const long double scratch_bytes=static_cast<long double>(o.chunk)*(max_local+columns+1)*sizeof(Complex)+o.chunk+65536;
    if(scratch_bytes+2*g.source_workspace_bytes+channel.report().working_bytes>o.memory_bytes)
        throw std::invalid_argument("export-sampled scratch/source estimate exceeds --memory-mib");

    std::filesystem::create_directories(o.output);const auto directory=std::filesystem::path(o.output);
    const std::array<const char*,4> names{"basis.c128","observation.c128","validity.u8","manifest.json"};
    for(const auto* name:names)if(std::filesystem::exists(directory/name)||std::filesystem::exists(directory/(std::string(name)+".partial")))
        throw std::invalid_argument("export files already exist; choose a fresh output directory");
    std::ofstream basis(directory/"basis.c128.partial",std::ios::binary),
        observation(directory/"observation.c128.partial",std::ios::binary),
        validity_file(directory/"validity.u8.partial",std::ios::binary);
    if(!basis||!observation||!validity_file)throw std::runtime_error("cannot create sampled export files");

    const auto sampled_started=Clock::now();
    std::vector<Complex> sample_chunk(o.chunk);std::vector<std::uint8_t> valid(o.chunk);
    while(channel.position()<rows){
        const auto wanted=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,rows-channel.position()));
        const auto n=channel.read(std::span(sample_chunk).first(wanted),std::span(valid).first(wanted));
        if(!n)throw std::runtime_error("channel ended before sampled-export calendar");
        for(std::size_t i=0;i<n;++i){write_le_complex(observation,sample_chunk[i]);validity_file.put(static_cast<char>(valid[i]));}
    }
    const double sampled_seconds=std::chrono::duration<double>(Clock::now()-sampled_started).count();

    const auto basis_started=Clock::now();
    std::vector<Complex> row_values(columns);
    auto write_row=[&](std::span<const Complex> row){
        for(std::size_t k=0;k<columns;++k){const Complex value=k<row.size()?row[k]:Complex{};
            if(!std::isfinite(value.real())||!std::isfinite(value.imag()))throw std::runtime_error("sampled basis value is not finite");
            write_le_complex(basis,value);
        }
    };
    for(std::size_t ci=0;ci<candidates.size();++ci){
        const auto& c=candidates[ci];auto addressed=f;addressed.epoch=offset_epoch(f.epoch,c.epoch_offset_seconds);
        PatternSource candidate(addressed,rxkey,c.source_bit);
        const auto actual_columns=c.channel.taps.size()*(c.channel.linear_gain?2:1);
        std::vector<Complex> values(o.chunk*actual_columns);
        for(std::uint64_t at=0;at<rows;){
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,rows-at));
            shaped_basis(candidate,c,effective_sample_rate(f),at,n,std::span(values).first(n*actual_columns));
            for(std::size_t r=0;r<n;++r){
                for(std::size_t k=0;k<actual_columns;++k)row_values[k]=values[k*n+r];
                write_row(std::span(row_values).first(actual_columns));
            }
            at+=n;
        }
        std::cerr<<"sampled export candidate="<<ci+1<<'/'<<candidates.size()<<" samples="<<rows<<'\n';
    }
    const double basis_seconds=std::chrono::duration<double>(Clock::now()-basis_started).count();
    basis.close();observation.close();validity_file.close();
    if(!basis||!observation||!validity_file)throw std::runtime_error("failed writing sampled waveform exports");

    const auto& report=channel.report();
    std::ofstream manifest(directory/"manifest.json.partial");
    manifest<<std::setprecision(17)<<"{\"format\":\"triad-sampled-waveforms-v1\",\"byte_order\":\"little\",\"complex_type\":\"complex128\",\"integer_type\":\"uint8\",\"layout\":\"candidate,sample,column\",\"basis_file\":\"basis.c128\",\"basis_shape\":["<<candidates.size()<<','<<rows<<','<<columns
        <<"],\"observation_file\":\"observation.c128\",\"observation_shape\":["<<rows<<"],\"validity_file\":\"validity.u8\",\"validity_shape\":["<<rows<<']'
        <<",\"sample_rate\":"<<effective_sample_rate(f)<<",\"sample_count\":"<<rows<<",\"template_sample_count\":"<<rows<<",\"observation_sample_count\":"<<rows<<",\"block_samples\":"<<block_samples(o,f)<<",\"chunk_samples\":"<<o.chunk
        <<",\"noise_variance_pre_frontend\":"<<noise<<",\"cn0_db_hz\":"<<o.cn0<<",\"noise_reference_power\":1"
        <<",\"signal_bit_simulation_truth\":"<<static_cast<unsigned>(o.signal_bit)<<",\"signal_enabled\":"<<(o.control!="null"?"true":"false")
        <<",\"seed\":"<<o.seed<<",\"key_seed\":"<<key_seed<<",\"channel_seed\":"<<channel_seed<<",\"epoch\":"<<f.epoch
        <<",\"source_samples\":"<<g.source_samples<<",\"requested_active_seconds\":"<<f.bit_seconds<<",\"actual_elapsed_seconds\":"<<g.bit_seconds
        <<",\"channel_samples\":"<<report.samples<<",\"channel_tail_samples\":"<<report.tail_samples
        <<",\"channel_signal_energy_samples\":"<<report.signal_energy<<",\"channel_observed_energy_samples\":"<<report.observed_energy
        <<",\"sampled_observation_wall_seconds\":"<<sampled_seconds<<",\"basis_generation_and_write_wall_seconds\":"<<basis_seconds
        <<",\"export_bytes\":"<<static_cast<std::uint64_t>(total_bytes)<<",\"channel\":";
    json_string(manifest,o.channel);manifest<<",\"control\":";json_string(manifest,o.control);
    manifest<<",\"fhss_envelope_only\":"<<(o.fhss?"true":"false")
        <<",\"fhss_center_aligned\":"<<(f.fhss_center_aligned?"true":"false")
        <<",\"oscillator_model\":{\"relative_phase_diffusion_rad_sqrt_s\":"<<resolve_configuration(o).phase_diffusion
        <<",\"relative_white_fm_hz_sqrt_s\":"<<o.white_fm<<",\"relative_random_walk_fm_hz_sqrt_s\":"<<o.random_walk_fm
        <<",\"relative_flicker_fm_rms_hz\":"<<o.flicker_fm<<",\"flicker_tau_seconds\":["<<o.flicker_min_tau<<','<<o.flicker_max_tau
        <<"],\"flicker_bands\":"<<o.flicker_bands<<",\"relative_white_pm_rms_rad\":"<<o.white_pm<<'}'
        <<",\"candidate_count\":"<<candidates.size()<<",\"candidates\":[";
    for(std::size_t i=0;i<candidates.size();++i){if(i)manifest<<',';json_candidate(manifest,candidates[i]);}
    manifest<<"],\"candidate_bits\":[";for(std::size_t i=0;i<candidates.size();++i){if(i)manifest<<',';manifest<<static_cast<unsigned>(candidates[i].source_bit);}manifest<<']';
    manifest<<",\"file_sizes\":{\"basis.c128\":"<<std::filesystem::file_size(directory/"basis.c128.partial")<<",\"observation.c128\":"<<std::filesystem::file_size(directory/"observation.c128.partial")<<",\"validity.u8\":"<<std::filesystem::file_size(directory/"validity.u8.partial")<<'}';
    json_provenance(manifest,o,f,g);json_endpoint_truth(manifest,config);json_frontend_report(manifest,report);manifest<<"}\n";manifest.close();
    if(!manifest)throw std::runtime_error("failed writing sampled-export manifest");
    for(const auto* name:{"basis.c128","observation.c128","validity.u8"})std::filesystem::rename(directory/(std::string(name)+".partial"),directory/name);
    std::filesystem::rename(directory/"manifest.json.partial",directory/"manifest.json");
}

void export_stats(const Options& o) {
    if(o.output.empty())throw std::invalid_argument("--output directory is required for --mode export-stats");
    const auto f=format_for(o);const auto g=geometry(f);
    const auto key_seed=o.key_seed.value_or(o.seed),channel_seed=o.channel_seed.value_or(o.seed);
    const auto txkey=key_for(key_seed,0);
    const auto rxkey=o.control=="wrong-key"?key_for(key_seed^0x6a09e667f3bcc909ULL,0):key_for(key_seed,0);
    auto candidates=candidates_for(o,f,rxkey);
    const auto calendar=observation_calendar(o,f,g);const auto pad=calendar.pad,trail=calendar.trail;
    const auto noise=static_cast<double>(effective_sample_rate(f))/std::pow(10.,o.cn0/10.);
    if(!std::isfinite(noise)||noise<=0)throw std::invalid_argument("C/N0 is outside numeric range");
    for(auto& c:candidates)c.arrival_seconds+=pad;
    const auto rows=calendar.samples;
    const auto block=block_samples(o,f);
    const auto blocks=rows/block+(rows%block!=0);
    std::map<ColumnKey,std::size_t> indices;
    std::vector<ColumnRef> columns;
    std::vector<std::vector<std::size_t>> mapping(candidates.size());
    for(std::size_t ci=0;ci<candidates.size();++ci){
        const auto local_count=candidates[ci].channel.taps.size()*(candidates[ci].channel.linear_gain?2:1);
        mapping[ci].reserve(local_count);
        for(std::size_t j=0;j<local_count;++j){auto key=column_key(candidates[ci],j);auto [it,inserted]=indices.emplace(std::move(key),columns.size());
            if(inserted)columns.push_back({ci,j});mapping[ci].push_back(it->second);}
    }
    const auto d=columns.size();
    if(d==0||d>64)throw std::invalid_argument("export-stats unique design dimension exceeds explicit cap D=64");
    const long double gram_bytes=static_cast<long double>(blocks)*d*d*16;
    const long double vector_bytes=static_cast<long double>(blocks)*d*16;
    const long double fixed_bytes=static_cast<long double>(blocks)*16+static_cast<long double>(blocks)*8+static_cast<long double>(blocks)*8;
    const long double total_bytes=gram_bytes+vector_bytes+fixed_bytes+(o.stats_noisy?vector_bytes:0)+
        (o.stats_noisy?rows:0)+(o.stats_noisy?static_cast<long double>(blocks)*8:0);
    if(rows>o.export_max_samples)throw std::invalid_argument("export-stats exceeds --export-max-samples (at most128000000)");
    if(total_bytes>o.export_max_bytes)throw std::invalid_argument("export-stats exceeds explicit --export-max-mib disk budget");
    std::size_t max_local=0;for(const auto& c:candidates)max_local=std::max(max_local,c.channel.taps.size()*(c.channel.linear_gain?2:1));
    const long double scratch=static_cast<long double>(o.chunk)*(d+max_local+1)*sizeof(Complex)+
        static_cast<long double>(d)*d*sizeof(Complex)+static_cast<long double>(d)*sizeof(Complex)+65536;
    long double sources_bytes=0;
    for(const auto& c:candidates){(void)c;sources_bytes+=g.source_workspace_bytes;}
    const long double channel_bytes=static_cast<long double>(g.source_workspace_bytes)+
        static_cast<long double>(rows?std::min<std::uint64_t>(rows,o.chunk):0)*sizeof(Complex)*3+
        calendar.channel_working_bytes;
    if(scratch+sources_bytes+channel_bytes>o.memory_bytes)throw std::invalid_argument("export-stats bounded scratch/source estimate exceeds --memory-mib");
    std::filesystem::create_directories(o.output);const auto directory=std::filesystem::path(o.output);
    std::vector<std::string> names{"gram.c128","mean.c128","intervals.u64","valid_rows.u64","signal_energy.f64","manifest.json"};
    if(o.stats_noisy){names.push_back("observation.c128");names.push_back("received_energy.f64");}
    for(const auto& name:names)if(std::filesystem::exists(directory/name)||std::filesystem::exists(directory/(name+".partial")))
        throw std::invalid_argument("export files already exist; choose a fresh output directory");
    if(std::filesystem::exists(directory/"validity-mask.tmp"))throw std::invalid_argument("export files already exist; choose a fresh output directory");
    std::ofstream gram(directory/"gram.c128.partial",std::ios::binary),mean(directory/"mean.c128.partial",std::ios::binary),
        intervals(directory/"intervals.u64.partial",std::ios::binary),valid_rows(directory/"valid_rows.u64.partial",std::ios::binary),
        energy(directory/"signal_energy.f64.partial",std::ios::binary);
    std::ofstream observation;
    std::ofstream received_energy;
    if(o.stats_noisy){observation.open(directory/"observation.c128.partial",std::ios::binary);received_energy.open(directory/"received_energy.f64.partial",std::ios::binary);}
    if(!gram||!mean||!intervals||!valid_rows||!energy||(o.stats_noisy&&(!observation||!received_energy)))throw std::runtime_error("cannot create export-stats files");
    std::vector<std::unique_ptr<PatternSource>> sources; sources.reserve(candidates.size());
    for(const auto& c:candidates){auto addressed=f;addressed.epoch=offset_epoch(f.epoch,c.epoch_offset_seconds);sources.push_back(std::make_unique<PatternSource>(addressed,rxkey,c.source_bit));}
    std::vector<Complex> basis_values(o.chunk*d),local_values(o.chunk*max_local),samples(o.chunk);
    std::vector<std::uint8_t> validity(o.chunk);
    double actual_signal_energy=0,actual_observed_energy=0,selected_truth_arrival=0,selected_truth_frequency=0;
    double selected_truth_clock=0,selected_truth_drift=0;
    std::uint64_t actual_channel_samples=0,actual_channel_tail=0;
    StreamChannelReport frontend_report{};
    auto pass=[&](bool noisy_pass){
        PatternSource truth(transmitter_format(o,f),txkey,o.signal_bit);
        auto config=channel_config(o,f,g,channel_seed,noise,pad,trail,calendar.samples);
        config.effects.noise_enabled=noisy_pass;
        StreamingChannel channel(truth,config);
        if(channel.total_samples()!=rows)throw std::runtime_error("natural channel endpoint exceeds public export-stats calendar");
        if(!noisy_pass){
            std::ofstream mask;
            if(o.stats_noisy){mask.open(directory/"validity-mask.tmp",std::ios::binary);if(!mask)throw std::runtime_error("cannot create temporary validity mask");}
            std::uint64_t block_first=0;std::size_t block_rows=0;std::uint64_t block_valid=0;double block_energy=0;
            std::vector<Complex> G(d*d),m(d);
            std::uint64_t at=0;
            while(at<rows){const auto want=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,rows-at));
                const auto n=channel.read(std::span(samples).first(want),std::span(validity).first(want));
                if(!n)throw std::runtime_error("channel ended before export-stats calendar");
                if(channel.position()-n!=at)throw std::runtime_error("noncontiguous channel samples");
                if(o.stats_noisy)mask.write(reinterpret_cast<const char*>(validity.data()),static_cast<std::streamsize>(n));
                std::fill(basis_values.begin(),basis_values.begin()+n*d,Complex{});
                for(std::size_t ci=0;ci<candidates.size();++ci){const auto cols=mapping[ci].size();
                    shaped_basis(*sources[ci],candidates[ci],effective_sample_rate(f),at,n,std::span(local_values).first(n*cols));
                    for(std::size_t j=0;j<cols;++j){const auto target=mapping[ci][j];
                        for(std::size_t r=0;r<n;++r)basis_values[target*n+r]=local_values[j*n+r];}}
                for(std::size_t r=0;r<n;++r){const bool valid=validity[r]!=0;
                    if(valid){++block_valid;block_energy+=std::norm(samples[r]);
                        for(std::size_t i=0;i<d;++i){const auto ai=std::conj(basis_values[i*n+r]);m[i]+=ai*samples[r];
                            for(std::size_t j=0;j<d;++j)G[i*d+j]+=ai*basis_values[j*n+r];}}
                    ++block_rows;
                    if(block_rows==block||at+r+1==rows){
                        const auto stop=block_first+block_rows;
                        write_le_u64(intervals,block_first);write_le_u64(intervals,stop);write_le_u64(valid_rows,block_valid);write_le_f64(energy,block_energy);
                        for(auto v:G)write_le_complex(gram,v);for(auto v:m)write_le_complex(mean,v);
                        block_first=stop;block_rows=0;block_valid=0;block_energy=0;std::fill(G.begin(),G.end(),Complex{});std::fill(m.begin(),m.end(),Complex{});
                    }
                }
                at+=n;
            }
            actual_signal_energy=channel.report().signal_energy;actual_observed_energy=channel.report().observed_energy;
            actual_channel_samples=channel.report().samples;actual_channel_tail=channel.report().tail_samples;
            selected_truth_arrival=truth_arrival(config);selected_truth_frequency=truth_frequency(config);
            selected_truth_clock=truth_clock(config);selected_truth_drift=truth_drift(config);
            if(o.stats_noisy){mask.close();if(!mask)throw std::runtime_error("failed writing temporary validity mask");}
        } else {
            std::ifstream mask(directory/"validity-mask.tmp",std::ios::binary);
            if(!mask)throw std::runtime_error("cannot read temporary validity mask");
            std::uint64_t at=0,block_count=0;double block_received_energy=0;
            std::vector<Complex> projection(d);std::vector<std::uint8_t> expected_mask(o.chunk);
            while(at<rows){const auto want=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,rows-at));
                const auto n=channel.read(std::span(samples).first(want),std::span(validity).first(want));
                if(!n)throw std::runtime_error("noisy channel ended before export-stats calendar");
                mask.read(reinterpret_cast<char*>(expected_mask.data()),static_cast<std::streamsize>(n));
                if(mask.gcount()!=static_cast<std::streamsize>(n)||!std::equal(expected_mask.begin(),expected_mask.begin()+n,validity.begin()))
                    throw std::runtime_error("noisy and noiseless channel erasure masks differ");
                std::fill(basis_values.begin(),basis_values.begin()+n*d,Complex{});
                for(std::size_t ci=0;ci<candidates.size();++ci){const auto cols=mapping[ci].size();shaped_basis(*sources[ci],candidates[ci],effective_sample_rate(f),at,n,std::span(local_values).first(n*cols));
                    for(std::size_t j=0;j<cols;++j)for(std::size_t r=0;r<n;++r)basis_values[mapping[ci][j]*n+r]=local_values[j*n+r];}
                for(std::size_t r=0;r<n;++r){if(validity[r]){block_received_energy+=std::norm(samples[r]);for(std::size_t j=0;j<d;++j)projection[j]+=std::conj(basis_values[j*n+r])*samples[r];}
                    ++block_count;if(block_count==block||at+r+1==rows){
                        for(auto v:projection)write_le_complex(observation,v);write_le_f64(received_energy,block_received_energy);
                        std::fill(projection.begin(),projection.end(),Complex{});block_received_energy=0;block_count=0;
                    }}
                at+=n;
            }
            mask.close();
        }
        frontend_report=channel.report();
    };
    pass(false);
    if(o.stats_noisy){
        // Retain the first pass's mask counts before opening the second channel pass.
        pass(true);
        std::filesystem::remove(directory/"validity-mask.tmp");
    }
    gram.close();mean.close();intervals.close();valid_rows.close();energy.close();if(o.stats_noisy){observation.close();received_energy.close();}
    if(!gram||!mean||!intervals||!valid_rows||!energy||(o.stats_noisy&&(!observation||!received_energy)))throw std::runtime_error("failed writing export-stats files");
    auto size_of=[&](const std::string& n){return std::filesystem::file_size(directory/(n+".partial"));};
    const auto resolved=resolve_configuration(o);
    const auto arrival=initial_arrival_bounds(o,resolved);
    const auto arrival_low=pad+arrival[0];
    const auto arrival_high=pad+arrival[1];
    const auto rx_clock_half=resolved.oscillators?resolved.base_ppm*.5e-6:0.;
    const std::array<double,4> physical_arrival_bounds{
        arrival_low*(1-rx_clock_half),arrival_low*(1+rx_clock_half),
        arrival_high*(1-rx_clock_half),arrival_high*(1+rx_clock_half)};
    const auto [physical_arrival_low,physical_arrival_high]=std::minmax_element(
        physical_arrival_bounds.begin(),physical_arrival_bounds.end());
    std::ofstream manifest(directory/"manifest.json.partial");
    manifest<<std::setprecision(17)<<"{\"format\":\"triad-block-stats-v1\",\"byte_order\":\"little\",\"complex_type\":\"complex128\",\"real_type\":\"float64\",\"integer_type\":\"uint64\",\"layout\":\"block,row,column\",\"block_count\":"<<blocks
       <<",\"dimension\":"<<d<<",\"sample_rate\":"<<effective_sample_rate(f)<<",\"noise_variance\":"<<noise
       <<",\"gram_file\":\"gram.c128\",\"gram_shape\":["<<blocks<<','<<d<<','<<d<<"],\"mean_file\":\"mean.c128\",\"mean_shape\":["<<blocks<<','<<d<<']'
       <<",\"intervals_file\":\"intervals.u64\",\"intervals_shape\":["<<blocks<<",2],\"valid_rows_file\":\"valid_rows.u64\",\"valid_rows_shape\":["<<blocks<<']'
       <<",\"signal_energy_file\":\"signal_energy.f64\",\"signal_energy_shape\":["<<blocks<<']'
       <<",\"sample_count\":"<<rows<<",\"block_samples\":"<<block<<",\"signal_bit\":"<<static_cast<unsigned>(o.signal_bit)
       <<",\"key_seed\":"<<key_seed<<",\"channel_seed\":"<<channel_seed<<",\"legacy_seed\":"<<o.seed
       <<",\"epoch\":"<<f.epoch<<",\"source_samples\":"<<g.source_samples<<",\"requested_active_seconds\":"<<f.bit_seconds<<",\"actual_elapsed_seconds\":"<<g.bit_seconds
       <<",\"nominal_payload_energy_seconds_lower\":"<<g.nominal_payload_energy_seconds<<",\"channel_signal_energy_samples\":"<<actual_signal_energy
       <<",\"channel_observed_energy_samples\":"<<actual_observed_energy<<",\"channel_samples\":"<<actual_channel_samples<<",\"channel_tail_samples\":"<<actual_channel_tail
       <<",\"source_hash_binding\":\"not included; bind externally to the exact source and binary\""
       <<",\"capture_lead_seconds\":"<<pad
       <<",\"arrival_coordinate\":\"candidate and selected-truth arrivals are nominal RX capture seconds; resolved_configuration initial/public timing bounds exclude capture lead\""
       <<",\"public_timing_bounds_capture_seconds\":["<<pad+o.start_low<<','<<pad+o.start_high<<']'
       <<",\"initial_physical_arrival_bounds_capture_seconds\":["<<*physical_arrival_low<<','<<*physical_arrival_high<<']'
       <<",\"physical_arrival_bounds_include_rx_sample_clock\":true"
       <<",\"selected_truth_arrival_seconds_simulation_only\":"<<selected_truth_arrival<<",\"selected_truth_frequency_hz_simulation_only\":"<<selected_truth_frequency
       <<",\"selected_truth_clock_ppm_simulation_only\":"<<selected_truth_clock<<",\"selected_truth_frequency_drift_hz_per_second_simulation_only\":"<<selected_truth_drift
       <<",\"cn0_db_hz\":"<<o.cn0<<",\"channel\":\""<<channel_name(o)<<"\",\"control\":\""<<o.control<<"\",\"signal_enabled\":"<<(o.control!="null"?"true":"false")
       <<",\"oscillator_model\":{\"relative_phase_diffusion_rad_sqrt_s\":"<<resolve_configuration(o).phase_diffusion<<",\"relative_white_fm_hz_sqrt_s\":"<<o.white_fm
       <<",\"relative_random_walk_fm_hz_sqrt_s\":"<<o.random_walk_fm<<",\"relative_flicker_fm_rms_hz\":"<<o.flicker_fm<<",\"flicker_tau_seconds\":["<<o.flicker_min_tau<<','<<o.flicker_max_tau
       <<"],\"flicker_bands\":"<<o.flicker_bands<<",\"relative_white_pm_rms_rad\":"<<o.white_pm<<",\"truth_fields_simulation_only\":true}"
       <<",\"column_mapping\":[";
    for(std::size_t i=0;i<mapping.size();++i){if(i)manifest<<',';manifest<<'[';for(std::size_t j=0;j<mapping[i].size();++j){if(j)manifest<<',';manifest<<mapping[i][j];}manifest<<']';}
    manifest<<"],\"column_descriptors\":[";
    for(std::size_t i=0;i<columns.size();++i){if(i)manifest<<',';const auto& ref=columns[i];const auto& c=candidates[ref.candidate];const auto local=ref.local;const auto tap_i=local/(c.channel.linear_gain?2:1);const auto gain_i=c.channel.linear_gain?local%2:0;const auto& tap=c.channel.taps[tap_i];
        manifest<<"{\"source_bit\":"<<static_cast<unsigned>(c.source_bit)<<",\"epoch_offset_seconds\":"<<c.epoch_offset_seconds<<",\"arrival_seconds\":"<<c.arrival_seconds<<",\"frequency_hz\":"<<c.frequency_hz<<",\"clock_ppm\":"<<c.clock_ppm<<",\"frequency_drift_hz_per_second\":"<<c.frequency_drift_hz_per_second<<",\"tap_delay_seconds\":"<<tap.delay_seconds<<",\"tap_doppler_hz\":"<<tap.doppler_hz<<",\"linear_gain\":"<<(c.channel.linear_gain?"true":"false")<<",\"gain_column\":"<<gain_i<<",\"candidate_index\":"<<ref.candidate<<'}';}
    manifest<<"],\"candidates\":[";for(std::size_t i=0;i<candidates.size();++i){if(i)manifest<<',';json_candidate(manifest,candidates[i]);}manifest<<']';
    manifest<<",\"candidate_count\":"<<candidates.size()<<",\"signal_energy_semantics\":";
    json_string(manifest,o.frontend.active()?"post-erasure transformed no-thermal pass energy, including converter noise/interference; not pure signal energy":"post-erasure actual signal energy; simulation evaluation only");
    manifest<<",\"mean_semantics\":";
    json_string(manifest,o.frontend.active()?"A^H W applied to transformed no-thermal pass; not a Gaussian signal mean; simulation evaluation only":"A^H W s_actual; simulation evaluation only");
    manifest<<",\"receiver_metadata_warning\":\"Do not read hidden means or energies as receiver metadata\",\"stats_noisy\":"<<(o.stats_noisy?"true":"false");
    if(o.stats_noisy)manifest<<",\"observation_file\":\"observation.c128\",\"observation_shape\":["<<blocks<<','<<d<<"],\"observation_semantics\":\"A^H W y from a second actual channel pass; simulation evaluation only\",\"received_energy_file\":\"received_energy.f64\",\"received_energy_shape\":["<<blocks<<"],\"received_energy_semantics\":\"per-block sum |y|^2 of actual noisy samples over valid rows from the same second channel pass; simulation evaluation only\"";
    manifest<<",\"file_sizes\":{\"gram.c128\":"<<size_of("gram.c128")<<",\"mean.c128\":"<<size_of("mean.c128")<<",\"intervals.u64\":"<<size_of("intervals.u64")<<",\"valid_rows.u64\":"<<size_of("valid_rows.u64")<<",\"signal_energy.f64\":"<<size_of("signal_energy.f64");if(o.stats_noisy)manifest<<",\"observation.c128\":"<<size_of("observation.c128")<<",\"received_energy.f64\":"<<size_of("received_energy.f64");manifest<<'}';
    json_provenance(manifest,o,f,g);json_endpoint_truth(manifest,channel_config(o,f,g,channel_seed,noise,pad,trail,calendar.samples));json_frontend_report(manifest,frontend_report);manifest<<"}\n";manifest.close();if(!manifest)throw std::runtime_error("failed writing export-stats manifest");
    for(const auto& n:names)if(n!="manifest.json")std::filesystem::rename(directory/(n+".partial"),directory/n);
    std::filesystem::rename(directory/"manifest.json.partial",directory/"manifest.json");
}

struct GroupColumnSet {
    std::vector<std::size_t> candidate_indices;
    std::vector<ColumnRef> columns;
    std::vector<std::vector<std::size_t>> mapping;
    std::vector<std::size_t> generation_order;
};
using SyncKey=std::tuple<double,double,double,std::int64_t,double>;
SyncKey sync_key(const Candidate& c) {
    return {c.arrival_seconds,c.frequency_hz,c.clock_ppm,c.epoch_offset_seconds,
            c.frequency_drift_hz_per_second};
}

// The grouped layout deliberately omits cross-group Gram products. Each
// synchronization group retains both bit alternatives and all channel models,
// so its orthogonal residual is independent of every score in that group.
// Noise observations are still projected onto every group from one shared
// sampled channel pass; group dependence is not discarded in simulation.
void export_group_stats(const Options& o) {
    if(o.output.empty())throw std::invalid_argument("--output directory is required for --mode export-group-stats");
    const auto f=format_for(o);const auto g=geometry(f);
    const auto key_seed=o.key_seed.value_or(o.seed),channel_seed=o.channel_seed.value_or(o.seed);
    const auto txkey=key_for(key_seed,0);
    const auto rxkey=o.control=="wrong-key"?key_for(key_seed^0x6a09e667f3bcc909ULL,0):key_for(key_seed,0);
    auto candidates=candidates_for(o,f,rxkey);
    const auto calendar=observation_calendar(o,f,g);const auto pad=calendar.pad,trail=calendar.trail;
    const auto noise=static_cast<double>(effective_sample_rate(f))/std::pow(10.,o.cn0/10.);
    if(!std::isfinite(noise)||noise<=0)throw std::invalid_argument("C/N0 is outside numeric range");
    for(auto& c:candidates)c.arrival_seconds+=pad;

    std::vector<GroupColumnSet> groups;
    std::map<SyncKey,std::size_t> group_indices;
    std::set<std::pair<std::uint8_t,std::int64_t>> source_keys;
    for(std::size_t ci=0;ci<candidates.size();++ci) {
        const auto [it,inserted]=group_indices.emplace(sync_key(candidates[ci]),groups.size());
        if(inserted)groups.emplace_back();
        auto& group=groups[it->second];group.candidate_indices.push_back(ci);
        const auto& candidate=candidates[ci];source_keys.emplace(candidate.source_bit,candidate.epoch_offset_seconds);
        const auto local_count=candidate.channel.taps.size()*(candidate.channel.linear_gain?2:1);
        group.mapping.emplace_back();group.mapping.back().reserve(local_count);
        for(std::size_t j=0;j<local_count;++j) {
            auto key=column_key(candidate,j);
            auto where=std::find_if(group.columns.begin(),group.columns.end(),[&](const ColumnRef& ref) {
                return column_key(candidates[ref.candidate],ref.local)==key;
            });
            if(where==group.columns.end()) {
                group.columns.push_back({ci,j});group.mapping.back().push_back(group.columns.size()-1);
            } else {
                group.mapping.back().push_back(static_cast<std::size_t>(where-group.columns.begin()));
            }
        }
    }
    // Build the broadest channel alternatives first. Their shared columns
    // cover narrower alternatives, which can then be omitted at synthesis
    // time without changing the public candidate or column-map ordering.
    for(auto& group:groups) {
        group.generation_order.resize(group.candidate_indices.size());
        std::iota(group.generation_order.begin(),group.generation_order.end(),0);
        std::stable_sort(group.generation_order.begin(),group.generation_order.end(),[&](std::size_t a,std::size_t b) {
            return group.mapping[a].size()>group.mapping[b].size();
        });
    }
    if(groups.empty())throw std::invalid_argument("grouped export has no candidates");
    if(groups.size()>10000)throw std::invalid_argument("grouped export exceeds explicit 10000 synchronization-group cap");
    const auto d=groups.front().columns.size();
    if(d==0||d>64)throw std::invalid_argument("grouped export group dimension is outside the existing D<=64 local design bound");
    for(const auto& group:groups)if(group.columns.size()!=d)
        throw std::invalid_argument("grouped export requires the same local design dimension in every synchronization group");

    const auto rows=calendar.samples;const auto block=block_samples(o,f);const auto blocks=rows/block+(rows%block!=0);
    const long double group_count=groups.size(),dimension=d,block_count=blocks;
    const long double gram_bytes=block_count*group_count*dimension*dimension*16;
    const long double vector_bytes=block_count*group_count*dimension*16;
    const long double shared_bytes=block_count*(16+8+8+(o.stats_noisy?8:0));
    const long double total_bytes=gram_bytes+vector_bytes+shared_bytes+
        (o.stats_noisy?(vector_bytes+rows):0);
    if(rows>o.export_max_samples)throw std::invalid_argument("export-group-stats exceeds --export-max-samples (at most128000000)");
    if(total_bytes>o.export_max_bytes)throw std::invalid_argument("export-group-stats exceeds explicit --export-max-mib disk budget");

    const auto fs=effective_sample_rate(f);std::size_t max_local=0;
    for(const auto& c:candidates)max_local=std::max(max_local,c.channel.taps.size()*(c.channel.linear_gain?2:1));
    const long double scratch=static_cast<long double>(o.chunk)*(dimension+max_local+1)*sizeof(Complex)+
        group_count*(dimension*dimension+2*dimension)*sizeof(Complex)+
        static_cast<long double>(o.chunk)*2*sizeof(std::uint8_t)+65536;
    const long double source_bytes=static_cast<long double>(source_keys.size())*g.source_workspace_bytes;
    const long double channel_bytes=static_cast<long double>(g.source_workspace_bytes)+
        static_cast<long double>(std::min<std::uint64_t>(rows,o.chunk))*sizeof(Complex)*3+calendar.channel_working_bytes;
    if(scratch+source_bytes+channel_bytes>o.memory_bytes)
        throw std::invalid_argument("export-group-stats bounded scratch/source estimate exceeds --memory-mib");
    // Independent physical channels retain their own source/filter/RNG state.
    // When both fit, advance them together and synthesize each public basis
    // once. Preserve the existing sequential passes for smaller RAM budgets.
    const long double paired_bytes=scratch+source_bytes+2*channel_bytes+
        static_cast<long double>(o.chunk)*sizeof(Complex);
    const bool paired_pass=o.stats_noisy&&paired_bytes<=o.memory_bytes;

    const auto directory=std::filesystem::path(o.output);
    std::vector<std::string> names{"group_gram.c128","group_mean.c128","intervals.u64","valid_rows.u64","signal_energy.f64","manifest.json"};
    if(o.stats_noisy){names.push_back("group_observation.c128");names.push_back("received_energy.f64");}
    for(const auto& name:names)if(std::filesystem::exists(directory/name)||std::filesystem::exists(directory/(name+".partial")))
        throw std::invalid_argument("grouped export files already exist; choose a fresh output directory");
    if(std::filesystem::exists(directory/"validity-mask.tmp"))
        throw std::invalid_argument("grouped export files already exist; choose a fresh output directory");
    std::filesystem::create_directories(directory);
    std::ofstream gram(directory/"group_gram.c128.partial",std::ios::binary),
        mean(directory/"group_mean.c128.partial",std::ios::binary),
        intervals(directory/"intervals.u64.partial",std::ios::binary),
        valid_rows(directory/"valid_rows.u64.partial",std::ios::binary),
        energy(directory/"signal_energy.f64.partial",std::ios::binary);
    std::ofstream observation,received_energy;
    if(o.stats_noisy){observation.open(directory/"group_observation.c128.partial",std::ios::binary);
        received_energy.open(directory/"received_energy.f64.partial",std::ios::binary);}
    if(!gram||!mean||!intervals||!valid_rows||!energy||
       (o.stats_noisy&&(!observation||!received_energy)))
        throw std::runtime_error("cannot create grouped statistics files");

    // One keyed source object per bit/epoch, reused through every public
    // frequency, clock, drift, and arrival hypothesis.
    std::map<std::pair<std::uint8_t,std::int64_t>,std::unique_ptr<PatternSource>> sources;
    for(const auto& source_key:source_keys) {
        auto addressed=f;addressed.epoch=offset_epoch(f.epoch,source_key.second);
        sources.emplace(source_key,std::make_unique<PatternSource>(addressed,rxkey,source_key.first));
    }
    std::vector<Complex> local_values(o.chunk*max_local),basis_values(o.chunk*d),samples(o.chunk);
    std::vector<Complex> paired_samples(paired_pass?o.chunk:0);
    std::vector<std::uint8_t> validity(o.chunk),expected_mask(o.chunk);
    std::vector<Complex> block_gram(groups.size()*d*d),block_projection(groups.size()*d);
    double block_energy=0,actual_signal_energy=0,actual_observed_energy=0;
    double selected_truth_arrival=0,selected_truth_frequency=0,selected_truth_clock=0,selected_truth_drift=0;
    std::uint64_t block_first=0,block_rows=0,block_valid=0,actual_channel_samples=0,actual_channel_tail=0;
    StreamChannelReport frontend_report{};

    auto emit_noiseless_block=[&] {
        const auto stop=block_first+block_rows;
        write_le_u64(intervals,block_first);write_le_u64(intervals,stop);write_le_u64(valid_rows,block_valid);write_le_f64(energy,block_energy);
        for(auto value:block_gram)write_le_complex(gram,value);
        for(auto value:block_projection)write_le_complex(mean,value);
        block_first=stop;block_rows=0;block_valid=0;block_energy=0;
        std::fill(block_gram.begin(),block_gram.end(),Complex{});std::fill(block_projection.begin(),block_projection.end(),Complex{});
    };
    auto pass=[&](bool noisy_pass,bool paired=false) {
        PatternSource truth(transmitter_format(o,f),txkey,o.signal_bit);
        auto config=channel_config(o,f,g,channel_seed,noise,pad,trail,calendar.samples);
        config.effects.noise_enabled=noisy_pass;StreamingChannel channel(truth,config);
        if(channel.total_samples()!=rows)throw std::runtime_error("natural channel endpoint exceeds grouped-export calendar");
        std::unique_ptr<PatternSource> noisy_truth;
        std::unique_ptr<StreamingChannel> noisy_channel;
        if(paired) {
            noisy_truth=std::make_unique<PatternSource>(transmitter_format(o,f),txkey,o.signal_bit);
            auto noisy_config=config;noisy_config.effects.noise_enabled=true;
            noisy_channel=std::make_unique<StreamingChannel>(*noisy_truth,noisy_config);
            if(noisy_channel->total_samples()!=rows)
                throw std::runtime_error("natural noisy channel endpoint exceeds grouped-export calendar");
        }
        std::ifstream mask;
        std::ofstream mask_output;
        if(noisy_pass) {
            mask.open(directory/"validity-mask.tmp",std::ios::binary);
            if(!mask)throw std::runtime_error("cannot read grouped validity mask");
        } else if(o.stats_noisy&&!paired) {
            mask_output.open(directory/"validity-mask.tmp",std::ios::binary);
            if(!mask_output)throw std::runtime_error("cannot create grouped validity mask");
        }
        std::uint64_t at=0,received_block_rows=0;double received_block_energy=0;
        std::vector<Complex> received_projection(groups.size()*d);
        auto emit_noisy_block=[&] {
            for(auto value:received_projection)write_le_complex(observation,value);
            write_le_f64(received_energy,received_block_energy);
            std::fill(received_projection.begin(),received_projection.end(),Complex{});received_block_energy=0;received_block_rows=0;
        };
        while(at<rows) {
            const auto wanted=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,rows-at));
            const auto n=channel.read(std::span(samples).first(wanted),std::span(validity).first(wanted));
            if(!n)throw std::runtime_error("channel ended before grouped-export calendar");
            if(channel.position()-n!=at)throw std::runtime_error("noncontiguous grouped channel samples");
            if(paired) {
                const auto noisy_n=noisy_channel->read(std::span(paired_samples).first(wanted),std::span(expected_mask).first(wanted));
                if(noisy_n!=n||noisy_channel->position()-noisy_n!=at)
                    throw std::runtime_error("grouped noiseless and noisy channel calendars differ");
                if(!std::equal(expected_mask.begin(),expected_mask.begin()+n,validity.begin()))
                    throw std::runtime_error("grouped noiseless and noisy channel erasure masks differ");
            } else if(noisy_pass) {
                mask.read(reinterpret_cast<char*>(expected_mask.data()),static_cast<std::streamsize>(n));
                if(mask.gcount()!=static_cast<std::streamsize>(n)||!std::equal(expected_mask.begin(),expected_mask.begin()+n,validity.begin()))
                    throw std::runtime_error("grouped noiseless and noisy channel erasure masks differ");
            } else if(o.stats_noisy) {
                mask_output.write(reinterpret_cast<const char*>(validity.data()),static_cast<std::streamsize>(n));
            }
            std::size_t chunk_offset=0;
            while(chunk_offset<n) {
                const auto current_block_rows=noisy_pass?received_block_rows:block_rows;
                const auto capacity=block-static_cast<std::size_t>(current_block_rows);
                const auto segment=std::min(capacity,n-chunk_offset);
                double segment_received_energy=0;
                for(std::size_t group_index=0;group_index<groups.size();++group_index) {
                    const auto& group=groups[group_index];
                    std::fill(basis_values.begin(),basis_values.begin()+segment*d,Complex{});
                    std::array<std::uint8_t,64> covered{};
                    for(const auto ci_index:group.generation_order) {
                        const auto& mapping=group.mapping[ci_index];
                        if(std::all_of(mapping.begin(),mapping.end(),[&](std::size_t target){return covered[target]!=0;}))continue;
                        const auto candidate_index=group.candidate_indices[ci_index];const auto& candidate=candidates[candidate_index];
                        const auto columns=mapping.size();
                        auto source=sources.at({candidate.source_bit,candidate.epoch_offset_seconds}).get();
                        shaped_basis(*source,candidate,fs,at+chunk_offset,segment,
                                     std::span(local_values).first(segment*columns));
                        for(std::size_t column=0;column<columns;++column) {
                            const auto target=mapping[column];
                            if(covered[target])continue;
                            for(std::size_t row=0;row<segment;++row)
                                basis_values[target*segment+row]=local_values[column*segment+row];
                            covered[target]=1;
                        }
                    }
                    for(std::size_t row=0;row<segment;++row) {
                        const auto sample_index=chunk_offset+row;const bool valid=validity[sample_index]!=0;
                        if(valid) {
                            const auto y=samples[sample_index];
                            for(std::size_t i=0;i<d;++i) {
                                const auto ai=std::conj(basis_values[i*segment+row]);
                                const auto projection_index=group_index*d+i;
                                if(noisy_pass)received_projection[projection_index]+=ai*y;
                                else {
                                    block_projection[projection_index]+=ai*y;
                                    for(std::size_t j=0;j<d;++j)
                                        block_gram[(group_index*d+i)*d+j]+=ai*basis_values[j*segment+row];
                                }
                                if(paired)received_projection[projection_index]+=ai*paired_samples[sample_index];
                            }
                        }
                    }
                }
                if(!noisy_pass) {
                    for(std::size_t row=0;row<segment;++row)if(validity[chunk_offset+row]) {
                        ++block_valid;block_energy+=std::norm(samples[chunk_offset+row]);
                    }
                    block_rows+=segment;
                    if(block_rows==block||at+chunk_offset+segment==rows)emit_noiseless_block();
                }
                if(noisy_pass||paired) {
                    const auto& noisy_samples=paired?paired_samples:samples;
                    for(std::size_t row=0;row<segment;++row)if(validity[chunk_offset+row])
                        segment_received_energy+=std::norm(noisy_samples[chunk_offset+row]);
                    received_block_energy+=segment_received_energy;received_block_rows+=segment;
                    if(received_block_rows==block||at+chunk_offset+segment==rows)emit_noisy_block();
                }
                chunk_offset+=segment;
            }
            at+=n;
        }
        if(mask.is_open())mask.close();if(mask_output.is_open()){mask_output.close();if(!mask_output)throw std::runtime_error("failed writing grouped validity mask");}
        frontend_report=paired?noisy_channel->report():channel.report();
        if(!noisy_pass) {
            actual_signal_energy=channel.report().signal_energy;actual_observed_energy=channel.report().observed_energy;
            actual_channel_samples=channel.report().samples;actual_channel_tail=channel.report().tail_samples;
            selected_truth_arrival=truth_arrival(config);selected_truth_frequency=truth_frequency(config);
            selected_truth_clock=truth_clock(config);selected_truth_drift=truth_drift(config);
        }
    };
    pass(false,paired_pass);
    if(o.stats_noisy&&!paired_pass){pass(true);std::filesystem::remove(directory/"validity-mask.tmp");}
    gram.close();mean.close();intervals.close();valid_rows.close();energy.close();
    if(o.stats_noisy){observation.close();received_energy.close();}
    if(!gram||!mean||!intervals||!valid_rows||!energy||(o.stats_noisy&&(!observation||!received_energy)))
        throw std::runtime_error("failed writing grouped statistics files");

    auto size_of=[&](const std::string& name){return std::filesystem::file_size(directory/(name+".partial"));};
    const auto resolved=resolve_configuration(o);const auto rx_clock_half=resolved.oscillators?resolved.base_ppm*.5e-6:0.;
    const auto arrival=initial_arrival_bounds(o,resolved);
    const auto arrival_low=pad+arrival[0];
    const auto arrival_high=pad+arrival[1];
    const auto physical_arrival_bounds=std::array<double,4>{arrival_low*(1-rx_clock_half),arrival_low*(1+rx_clock_half),
        arrival_high*(1-rx_clock_half),arrival_high*(1+rx_clock_half)};
    const auto [arrival_bound_low,arrival_bound_high]=std::minmax_element(physical_arrival_bounds.begin(),physical_arrival_bounds.end());
    std::ofstream manifest(directory/"manifest.json.partial");
    manifest<<std::setprecision(17)<<"{\"format\":\"triad-group-block-stats-v1\",\"byte_order\":\"little\",\"complex_type\":\"complex128\",\"real_type\":\"float64\",\"integer_type\":\"uint64\",\"layout\":\"block,group,row,column\",\"block_count\":"<<blocks
       <<",\"group_count\":"<<groups.size()<<",\"dimension\":"<<d<<",\"sample_rate\":"<<fs<<",\"noise_variance\":"<<noise
       <<",\"gram_file\":\"group_gram.c128\",\"gram_shape\":["<<blocks<<','<<groups.size()<<','<<d<<','<<d<<']'
       <<",\"mean_file\":\"group_mean.c128\",\"mean_shape\":["<<blocks<<','<<groups.size()<<','<<d<<']'
       <<",\"intervals_file\":\"intervals.u64\",\"intervals_shape\":["<<blocks<<",2]"
       <<",\"valid_rows_file\":\"valid_rows.u64\",\"valid_rows_shape\":["<<blocks<<']'
       <<",\"signal_energy_file\":\"signal_energy.f64\",\"signal_energy_shape\":["<<blocks<<']'
       <<",\"sample_count\":"<<rows<<",\"block_samples\":"<<block<<",\"signal_bit_simulation_truth\":"<<static_cast<unsigned>(o.signal_bit)
       <<",\"key_seed\":"<<key_seed<<",\"channel_seed\":"<<channel_seed<<",\"legacy_seed\":"<<o.seed<<",\"epoch\":"<<f.epoch
       <<",\"source_samples\":"<<g.source_samples<<",\"requested_active_seconds\":"<<f.bit_seconds<<",\"actual_elapsed_seconds\":"<<g.bit_seconds
       <<",\"nominal_payload_energy_seconds_lower\":"<<g.nominal_payload_energy_seconds<<",\"channel_signal_energy_samples\":"<<actual_signal_energy
       <<",\"channel_observed_energy_samples\":"<<actual_observed_energy<<",\"channel_samples\":"<<actual_channel_samples<<",\"channel_tail_samples\":"<<actual_channel_tail
       <<",\"capture_lead_seconds\":"<<pad<<",\"arrival_coordinate\":\"candidate arrivals include capture lead and use nominal RX capture seconds\""
       <<",\"public_timing_bounds_capture_seconds\":["<<pad+o.start_low<<','<<pad+o.start_high<<']'
       <<",\"initial_physical_arrival_bounds_capture_seconds\":["<<*arrival_bound_low<<','<<*arrival_bound_high<<']'
       <<",\"physical_arrival_bounds_include_rx_sample_clock\":true"
       <<",\"selected_truth_arrival_seconds_simulation_only\":"<<selected_truth_arrival<<",\"selected_truth_frequency_hz_simulation_only\":"<<selected_truth_frequency
       <<",\"selected_truth_clock_ppm_simulation_only\":"<<selected_truth_clock<<",\"selected_truth_frequency_drift_hz_per_second_simulation_only\":"<<selected_truth_drift
       <<",\"cn0_db_hz\":"<<o.cn0<<",\"control\":\""<<o.control<<"\",\"signal_enabled\":"<<(o.control!="null"?"true":"false")
       <<",\"oscillator_model\":{\"relative_phase_diffusion_rad_sqrt_s\":"<<resolved.phase_diffusion<<",\"relative_white_fm_hz_sqrt_s\":"<<o.white_fm
       <<",\"relative_random_walk_fm_hz_sqrt_s\":"<<o.random_walk_fm<<",\"relative_flicker_fm_rms_hz\":"<<o.flicker_fm
       <<",\"flicker_tau_seconds\":["<<o.flicker_min_tau<<','<<o.flicker_max_tau<<"],\"flicker_bands\":"<<o.flicker_bands
       <<",\"relative_white_pm_rms_rad\":"<<o.white_pm<<",\"truth_fields_simulation_only\":true}"
       <<",\"candidate_count\":"<<candidates.size()<<",\"candidates\":[";
    for(std::size_t ci=0;ci<candidates.size();++ci){if(ci)manifest<<',';json_candidate(manifest,candidates[ci]);}
    manifest<<"],\"groups\":[";
    for(std::size_t gi=0;gi<groups.size();++gi) {
        if(gi)manifest<<',';const auto& group=groups[gi];const auto& c=candidates[group.candidate_indices.front()];
        manifest<<"{\"sync_key\":{\"arrival_seconds\":"<<c.arrival_seconds<<",\"frequency_hz\":"<<c.frequency_hz
            <<",\"clock_ppm\":"<<c.clock_ppm<<",\"epoch_offset_seconds\":"<<c.epoch_offset_seconds
            <<",\"frequency_drift_hz_per_second\":"<<c.frequency_drift_hz_per_second<<"},\"candidate_indices\":[";
        for(std::size_t i=0;i<group.candidate_indices.size();++i){if(i)manifest<<',';manifest<<group.candidate_indices[i];}
        manifest<<"],\"column_mapping\":[";
        for(std::size_t i=0;i<group.mapping.size();++i){if(i)manifest<<',';manifest<<'[';for(std::size_t j=0;j<group.mapping[i].size();++j){if(j)manifest<<',';manifest<<group.mapping[i][j];}manifest<<']';}
        manifest<<"],\"column_descriptors\":[";
        for(std::size_t i=0;i<group.columns.size();++i){if(i)manifest<<',';const auto& ref=group.columns[i];const auto& candidate=candidates[ref.candidate];
            const auto local=ref.local,tap_i=local/(candidate.channel.linear_gain?2:1),gain_i=candidate.channel.linear_gain?local%2:0;
            const auto& tap=candidate.channel.taps[tap_i];
            manifest<<"{\"source_bit\":"<<static_cast<unsigned>(candidate.source_bit)<<",\"epoch_offset_seconds\":"<<candidate.epoch_offset_seconds
                <<",\"arrival_seconds\":"<<candidate.arrival_seconds<<",\"frequency_hz\":"<<candidate.frequency_hz<<",\"clock_ppm\":"<<candidate.clock_ppm
                <<",\"frequency_drift_hz_per_second\":"<<candidate.frequency_drift_hz_per_second<<",\"tap_delay_seconds\":"<<tap.delay_seconds
                <<",\"tap_doppler_hz\":"<<tap.doppler_hz<<",\"linear_gain\":"<<(candidate.channel.linear_gain?"true":"false")
                <<",\"gain_column\":"<<gain_i<<",\"candidate_index\":"<<ref.candidate<<'}';}
        manifest<<"]}";
    }
    manifest<<"],\"cross_group_gram_saved\":false,\"conditional_noise_resampling_supported\":false"
       <<",\"calibration_scope\":\"all timing candidates and both bits are retained; residual variance must be calibrated independently within each fixed group, with a family allocation across groups\""
       <<",\"source_hash_binding\":\"not included; bind externally to the exact source and binary\""
       <<",\"signal_energy_semantics\":";
    json_string(manifest,o.frontend.active()?"post-erasure transformed no-thermal pass energy; not pure signal energy":"post-erasure actual signal energy; simulation evaluation only");
    manifest<<",\"mean_semantics\":";json_string(manifest,o.frontend.active()?"grouped A^H W transformed no-thermal pass; simulation evaluation only":"grouped A^H W actual signal; simulation evaluation only");
    manifest<<",\"receiver_metadata_warning\":\"All means/energies are simulation truth and are not receiver metadata\",\"stats_noisy\":"<<(o.stats_noisy?"true":"false")
       <<",\"gaussian_projection_supported\":"<<(frontend_report.gaussian_projection_supported?"true":"false");
    manifest<<",\"grouped_export_execution\":{\"channel_pass_mode\":\""<<(paired_pass?"paired":"sequential")
       <<"\",\"basis_synthesis_passes\":"<<(o.stats_noisy&&!paired_pass?2:1)
       <<",\"worker_threads_used\":1,\"bounded_workspace_bytes\":"
       <<static_cast<std::uint64_t>(paired_pass?paired_bytes:scratch+source_bytes+channel_bytes)<<'}';
    if(o.stats_noisy)manifest<<",\"observation_file\":\"group_observation.c128\",\"observation_shape\":["<<blocks<<','<<groups.size()<<','<<d<<"],\"observation_semantics\":\"A^H W y from the one shared actual noisy channel pass, projected separately for every fixed group\",\"received_energy_file\":\"received_energy.f64\",\"received_energy_shape\":["<<blocks<<"],\"received_energy_semantics\":\"common per-block actual noisy sample energy over valid rows\"";
    manifest<<",\"file_sizes\":{\"group_gram.c128\":"<<size_of("group_gram.c128")<<",\"group_mean.c128\":"<<size_of("group_mean.c128")
       <<",\"intervals.u64\":"<<size_of("intervals.u64")<<",\"valid_rows.u64\":"<<size_of("valid_rows.u64")<<",\"signal_energy.f64\":"<<size_of("signal_energy.f64");
    if(o.stats_noisy)manifest<<",\"group_observation.c128\":"<<size_of("group_observation.c128")<<",\"received_energy.f64\":"<<size_of("received_energy.f64");
    manifest<<'}';json_provenance(manifest,o,f,g);
    json_endpoint_truth(manifest,channel_config(o,f,g,channel_seed,noise,pad,trail,calendar.samples));json_frontend_report(manifest,frontend_report);
    manifest<<"}\n";manifest.close();if(!manifest)throw std::runtime_error("failed writing grouped statistics manifest");
    for(const auto& name:names)if(name!="manifest.json")std::filesystem::rename(directory/(name+".partial"),directory/name);
    std::filesystem::rename(directory/"manifest.json.partial",directory/"manifest.json");
}

} // namespace

int main(int argc,char** argv) {
    try {
        const auto options=parse(argc,argv);
        if(options.mode=="configure")print_configuration(options);
        else if(options.mode=="plan") print_plan(options);
        else {
        if(configured_physics(options)||path_evolution_active(options)){
            const auto f=format_for(options);const auto g=geometry(f);
            if(configuration_capture_margin(options,f,g)<=0)
                throw std::invalid_argument("configured RF/audio oscillator prior cannot fit the sampled baseband; inspect --mode configure or widen/coarsely acquire the RF capture before simulation");
        }
        if(options.mode=="run") run(options);
        else if(options.mode=="export") export_basis(options);
        else if(options.mode=="export-source-window") export_source_window(options);
        else if(options.mode=="export-sampled") export_sampled(options);
        else if(options.mode=="export-stats") export_stats(options);
        else export_group_stats(options);
        }
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"triad_long: "<<e.what()<<"\n";
        return 2;
    }
}
