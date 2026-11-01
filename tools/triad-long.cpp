#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_statistics.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
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
    double cn0 = 8;
    double rate = 36, rf_hz = 30e6;
    double start_low = 0, start_high = 0, start_step = 1;
    double frequency_low = 0, frequency_high = 0, frequency_step = .1;
    double sample_rate = 0;
    double phase_diffusion = .5*std::numbers::pi/180, white_fm = 0, random_walk_fm = 0;
    double flicker_fm = 0, flicker_min_tau = .1, flicker_max_tau = 86400;
    double white_pm = 0;
    unsigned flicker_bands = 12;
    double dropout_ms = 0, dropout_start_ms = 0;
    std::size_t dropout_every_dwells = 1;
    double correct = .99, false_probability = 1e-4, horizon = 86400;
    double attempt_spacing = 60;
    double outage = 0;
    double retained_energy = 1;
    double wrong_projection = .08;
    std::uint64_t minimum_dwells = 20, seed = 1, epoch = 1793404800;
    std::optional<std::uint64_t> key_seed, channel_seed;
    std::optional<std::uint64_t> oscillator_seed, noise_seed, frontend_seed;
    std::uint64_t duration_alternatives = 1;
    unsigned added = 10, threads = 2;
    std::size_t chunk = 4096, block = 1536;
    double block_seconds = -1;
    std::size_t memory_bytes = 64ULL*1024*1024;
    std::uint64_t export_max_samples = 128000000, export_max_bytes = 8ULL*1024*1024*1024;
    unsigned burst_wait_tiles = 0;
    unsigned trials = 1;
    std::uint8_t signal_bit = 0;
    bool encrypt = true, now = false, fhss = false;
    bool assume_model_bounds = false;
    bool stats_noisy = false;
    std::string basis = "flat";
    double tap_delay_ms = 1.5, tap_doppler_hz = .3;
    std::vector<double> clocks_ppm{0}, frequency_drifts{0};
    std::vector<std::int64_t> epoch_offsets{0};
    std::vector<std::string> command_argv;
    SampledFrontendConfig frontend{};
};

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
template<class T> std::vector<T> list(std::string_view input, const char* name) {
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
    if(out.empty()||out.size()>1000) throw std::invalid_argument(std::string("invalid ")+name+" list");
    return out;
}
Options parse(int argc, char** argv) {
    Options o;
    for(int i=0;i<argc;++i)o.command_argv.emplace_back(argv[i]);
    for (int i = 1; i < argc; ++i) {
        const std::string_view key(argv[i]);
        if (key == "--help" || key == "-h") {
            std::cout << R"HELP(triad_long --mode plan|run|export|export-sampled|export-stats [options]
  --seconds S                 Requested active one-bit span (8..604800 s); rounded to complete private tiles.
  --rate HZ --added N --sample-rate HZ --rf-hz HZ
  --cn0 DB-HZ                 Total nominal three-carrier C/N0; N0 normalization uses power1, not faded output.
  --channel none|hf           hf: shared two-ray1.5ms/.3Hz plus slow/fast5% perturbations.
  --basis flat|two-ray        two-ray searches flat and coupled two-ray models; per-carrier fits unsupported.
  --tap-delay-ms MS --tap-doppler-hz HZ
  --start MIN MAX STEP --frequency MIN MAX STEP
  --clock-ppm LIST --epoch-offsets LIST --frequency-drift LIST
                              Comma lists; all bank hypotheses are fixed before observed noise.
  --block N | --block-seconds S   Coherent score-block length; streaming QR does not buffer whole blocks.
  --chunk N --threads N --memory-mib N
  --phase-diffusion RAD/SQRT(S)   Relative Wiener coefficient; default0.5 degrees/sqrt(s) expressed in radians.
  --white-fm HZ*SQRT(S) --random-walk-fm HZ/SQRT(S)
  --flicker-fm RMS-HZ --flicker-tau MIN MAX --flicker-bands N --white-pm RMS-RAD
  --dropout-ms MS --dropout-start-ms MS --dropout-every-dwells N
  --fhss                      Source envelope only; no physical RF-retuning claim.
  --burst-wait-tiles N         Silent tile waits within this one-bit v3 observation.
  --seed N --epoch UTC | --now --trials N --signal-bit 0|1 --no-encrypt
  --key-seed N --channel-seed N   Key/channel seeds (default to --seed).
  --oscillator-seed N --noise-seed N --frontend-seed N
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
        else if (key == "--seconds") o.seconds = number(need(), "seconds");
        else if (key == "--cn0") o.cn0 = number(need(), "C/N0");
        else if (key == "--rate") o.rate = number(need(), "Rate");
        else if (key == "--rf-hz") o.rf_hz = number(need(), "RF frequency");
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
        else if (key == "--sample-rate") o.sample_rate = number(need(), "sample rate");
        else if (key == "--phase-diffusion") o.phase_diffusion=number(need(),"Wiener phase diffusion");
        else if (key == "--white-fm") o.white_fm=number(need(),"white FM");
        else if (key == "--random-walk-fm") o.random_walk_fm=number(need(),"random walk FM");
        else if (key == "--flicker-fm") o.flicker_fm=number(need(),"finite-band flicker FM");
        else if (key == "--flicker-tau") {o.flicker_min_tau=number(need(),"minimum flicker tau");o.flicker_max_tau=number(need(),"maximum flicker tau");}
        else if (key == "--flicker-bands") o.flicker_bands=bounded_integer<unsigned>(need(),"flicker bands");
        else if (key == "--white-pm") o.white_pm=number(need(),"white PM");
        else if (key == "--dropout-ms") o.dropout_ms=number(need(),"dropout milliseconds");
        else if (key == "--dropout-start-ms") o.dropout_start_ms=number(need(),"dropout start milliseconds");
        else if (key == "--dropout-every-dwells") o.dropout_every_dwells=bounded_integer<std::size_t>(need(),"dropout period");
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
    if (o.mode != "plan" && o.mode != "run" && o.mode != "export" && o.mode != "export-sampled" && o.mode != "export-stats") throw std::invalid_argument("mode must be plan, run, export, export-sampled, or export-stats");
    if (o.channel != "none" && o.channel != "hf") throw std::invalid_argument("channel must be none or hf");
    if (o.basis!="flat" && o.basis!="two-ray" && o.basis!="per-carrier")
        throw std::invalid_argument("basis must be flat, two-ray, or per-carrier");
    if (o.basis=="per-carrier")
        throw std::invalid_argument("per-carrier tap fits are not implemented; use the coupled shared-tap basis");
    if (o.control != "signal" && o.control != "null" && o.control != "wrong-key") throw std::invalid_argument("control must be signal, null, or wrong-key");
    if (!(o.seconds >= 8 && o.seconds <= 7*86400.) ||
        !(o.rate > 0 && o.rate <= 30000000) || !(o.rf_hz > 0 && o.rf_hz <= 10e9) ||
        !(o.sample_rate == 0 || (o.sample_rate >= 64 && o.sample_rate <= 10000000)) ||
        o.trials == 0 || o.trials > 100000 || o.threads == 0 || o.threads > 256 ||
        o.chunk == 0 || o.chunk > 1000000 || o.block == 0 || o.burst_wait_tiles>1000 ||
        o.signal_bit > 1 || o.added == 0 || o.added > 1000 || o.minimum_dwells < 20 || o.duration_alternatives==0)
        throw std::invalid_argument("invalid duration, trial count, resource setting, bit or pattern geometry");
    if (!(o.correct > 0 && o.correct < 1 && o.false_probability > 0 && o.false_probability < 1 && o.horizon > 0 && o.attempt_spacing > 0))
        throw std::invalid_argument("probabilities must be in (0,1) and time inputs positive");
    if (!(o.start_step > 0 && o.frequency_step > 0 && o.start_low <= o.start_high && o.frequency_low <= o.frequency_high))
        throw std::invalid_argument("invalid acquisition grid");
    for(const auto x:{o.phase_diffusion,o.white_fm,o.random_walk_fm,o.flicker_fm,o.white_pm,
                      o.dropout_ms,o.dropout_start_ms})
        if(!std::isfinite(x)||x<0)throw std::invalid_argument("oscillator/erasure parameters must be finite and nonnegative");
    if(!(o.flicker_min_tau>0&&o.flicker_max_tau>=o.flicker_min_tau&&o.flicker_bands>=1&&o.flicker_bands<=64&&
         o.dropout_every_dwells>0&&o.dropout_start_ms+o.dropout_ms<=400))
        throw std::invalid_argument("invalid flicker band or dwell-erasure parameters");
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

Format format_for(const Options& o) {
    Format f;
    f.inner_rate_hz=o.rate; f.added_symbols=o.added; f.added_interleave=true;
    const auto fs=o.sample_rate ? o.sample_rate : std::max(960.,std::ceil(4.2*(f.inner_rate_hz/6*o.added)));
    if(!std::isfinite(fs)||fs!=std::floor(fs)||fs>std::numeric_limits<std::uint32_t>::max())throw std::invalid_argument("sample rate must be an integer in uint32 range");
    f.sample_rate=static_cast<std::uint32_t>(fs);
    f.bit_seconds=o.seconds; f.minimum_inner_coefficients=64;
    f.fhss=o.fhss; f.dwell_seconds=.4; f.quiet_seconds=o.fhss?.055:0;
    f.ramp_seconds=o.fhss?(2./3600.):.02; f.margin_seconds=.005;
    f.encrypt=o.encrypt; f.epoch=o.epoch; f.symbol_ordinal=0;f.burst_wait_tiles=o.burst_wait_tiles;
    return f;
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
void json_provenance(std::ostream& out,const Options& o,const Format& f,const Geometry& g,std::uint64_t trial=0) {
    const auto channel_seed=o.channel_seed.value_or(o.seed)+trial;
    const auto oscillator_seed=o.oscillator_seed.value_or(o.channel_seed.value_or(o.seed))+trial;
    const auto noise_seed=o.noise_seed.value_or(o.channel_seed.value_or(o.seed))+trial;
    const auto frontend_seed=o.frontend_seed.value_or(o.channel_seed.value_or(o.seed))+trial;
    out<<",\"duration_alternatives\":"<<o.duration_alternatives
       <<",\"false_admission_probability_per_horizon\":"<<o.false_probability
       <<",\"monitoring_horizon_seconds\":"<<o.horizon<<",\"attempt_spacing_seconds\":"<<o.attempt_spacing
       <<",\"random_seed_roots\":{\"channel\":"<<channel_seed<<",\"oscillator\":"<<oscillator_seed
       <<",\"noise\":"<<noise_seed<<",\"frontend\":"<<frontend_seed
       <<",\"oscillator_explicit\":"<<(o.oscillator_seed?"true":"false")
       <<",\"noise_explicit\":"<<(o.noise_seed?"true":"false")
       <<",\"frontend_explicit\":"<<(o.frontend_seed?"true":"false")
       <<",\"fallback\":\"unset oscillator/noise/frontend roots inherit the resolved channel seed\",\"channel_root_scope\":\"static timing/CFO/clock/drift truth, spread ray, and frequency-selective perturbation\",\"oscillator_root_scope\":\"TX/RX oscillator spectra and FHSS phase resets\",\"noise_root_scope\":\"thermal noise only\",\"frontend_root_scope\":\"DAC/ADC conversion, impulse process, and tone phase\"}"
       <<",\"command_argv\":[";
    for(std::size_t i=0;i<o.command_argv.size();++i){if(i)out<<',';json_string(out,o.command_argv[i]);}
    out<<"],\"source_format\":{\"version\":3,\"inner_rate_hz\":"<<f.inner_rate_hz
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
       <<",\"channel_model\":{\"spread_delay_ms\":"<<(o.channel=="hf"?1.5:0)
       <<",\"spread_doppler_hz\":"<<(o.channel=="hf"?.3:0)
       <<",\"perturbation_slow_depth\":"<<(o.channel=="hf"?.05:0)
       <<",\"perturbation_fast_depth\":"<<(o.channel=="hf"?.05:0)
       <<",\"nominal_conversion_if_hz\":1500,\"signal_gain\":1,\"rf_hz\":"<<o.rf_hz
       <<",\"flicker_tau_seconds\":["<<o.flicker_min_tau<<','<<o.flicker_max_tau<<']'
       <<",\"flicker_bands\":"<<o.flicker_bands
       <<",\"sample_clock_reference\":\"RX0ppm; TX carries declared relative error\",\"physical_rf_retuning\":false}"
       <<",\"source_hash_note\":\"source and binary hashes must be bound externally to this uncommitted study build\"";
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
    out<<",\"frontend_report\":{\"dac_clipped_components\":"<<r.dac_clipped_components
       <<",\"adc_clipped_components\":"<<r.adc_clipped_components<<",\"impulse_events\":"<<r.impulse_events
       <<",\"overload_samples\":"<<r.overload_samples<<",\"maximum_adc_input_component\":"<<r.maximum_adc_input_component
       <<",\"gaussian_projection_supported\":"<<(r.gaussian_projection_supported?"true":"false")<<'}';
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
    p.coherence_block_seconds=static_cast<double>(block)/f.sample_rate;
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
    p.baseband_samples_per_second=f.sample_rate;
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
        <<",\"assumption_note\":\"conditional only on caller-supplied retained-energy, wrong-codeword projection, outage, and white-noise bounds, plus fixed-rank bank if selected; this CLI has no empirical calibration\"}\n";
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
    auto scaled=static_cast<long double>(o.block_seconds)*f.sample_rate;
    const auto nearest=std::round(scaled);
    if(std::abs(scaled-nearest)<=4*std::numeric_limits<double>::epsilon()*std::max(1.L,std::abs(scaled)))scaled=nearest;
    const long double samples=std::ceil(scaled);
    if(samples<1||samples>std::numeric_limits<std::size_t>::max())throw std::invalid_argument("coherence block coordinate overflow");
    return static_cast<std::size_t>(samples);
}

StreamChannelConfig channel_config(const Options& o,const Format& base,const Geometry& nominal,
                                   std::uint64_t seed,double noise,double pad,double trail) {
    StreamChannelConfig channel_cfg; channel_cfg.frontend=o.frontend; channel_cfg.effects.sample_rate=base.sample_rate;
    channel_cfg.effects.occupied_bandwidth_hz=nominal.occupied_bandwidth_hz;
    channel_cfg.effects.rf_hz=o.rf_hz; channel_cfg.effects.nominal_conversion_if_hz=1500;
    channel_cfg.effects.seed=seed; channel_cfg.effects.signal_enabled=o.control!="null";
    const auto channel_seed_base=o.channel_seed.value_or(o.seed);
    if(seed<channel_seed_base)throw std::invalid_argument("channel trial seed precedes its configured root");
    const auto trial=seed-channel_seed_base;
    channel_cfg.oscillator_seed=o.oscillator_seed.value_or(channel_seed_base)+trial;
    channel_cfg.noise_seed=o.noise_seed.value_or(channel_seed_base)+trial;
    channel_cfg.frontend_seed=o.frontend_seed.value_or(channel_seed_base)+trial;
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
    channel_cfg.effects.tx.rf_drift_ppm_per_second=true_drift*1e6/o.rf_hz;
    channel_cfg.effects.static_doppler_hz+=true_drift*(pad+channel_cfg.effects.timing_offset_seconds);
    channel_cfg.effects.dropout_ms=o.dropout_ms;
    channel_cfg.effects.dropout_start_ms=o.dropout_start_ms;
    channel_cfg.effects.dropout_every_n_dwells=o.dropout_every_dwells;
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
    if(o.channel=="hf") { channel_cfg.effects.spread_delay_ms=1.5; channel_cfg.effects.spread_doppler_hz=.3; channel_cfg.effects.perturbation_slow_depth=.05; channel_cfg.effects.perturbation_fast_depth=.05; }
    channel_cfg.complex_noise_variance=noise; channel_cfg.maximum_chunk_samples=o.chunk;
    return channel_cfg;
}

double truth_clock(const StreamChannelConfig& c) {return c.effects.tx.audio_ppm;}
double truth_drift(const StreamChannelConfig& c) {return c.effects.tx.rf_drift_ppm_per_second*c.effects.rf_hz*1e-6;}
double truth_frequency(const StreamChannelConfig& c) {
    return c.effects.static_doppler_hz+c.effects.nominal_conversion_if_hz*c.effects.tx.audio_ppm*1e-6-
           truth_drift(c)*(c.effects.capture_lead_seconds+c.effects.timing_offset_seconds);
}

struct ObservationCalendar {double pad=0,trail=0;std::uint64_t samples=0;};
ObservationCalendar observation_calendar(const Options& o,const Format& f,const Geometry& g) {
    const auto [lo,hi]=std::minmax_element(o.clocks_ppm.begin(),o.clocks_ppm.end());
    const long double slow=1+*lo*1e-6,fast=1+*hi*1e-6;
    const long double source_seconds=static_cast<long double>(g.source_samples)/f.sample_rate;
    const auto delay=std::max(o.channel=="hf"?.0015:0.,o.basis=="two-ray"?o.tap_delay_ms*.001:0.);
    ObservationCalendar c;c.pad=std::max(0.,-o.start_low)+.05;
    const double frontend_tail=o.frontend.lowpass_hz>0?std::log(1e6)/(2*std::numbers::pi*o.frontend.lowpass_hz):0;
    c.trail=std::max(0.,o.start_high)-o.start_low+static_cast<double>(source_seconds*(1/slow-1/fast))+delay+2+frontend_tail;
    const long double end=c.pad+std::max(0.,o.start_high)+source_seconds/slow+delay+2+frontend_tail;
    if(!std::isfinite(end)||end>7*86400.L||end*f.sample_rate>UINT64_MAX)throw std::invalid_argument("public capture exceeds seven-day/coordinate study guard");
    c.samples=static_cast<std::uint64_t>(std::ceil(end*f.sample_rate));return c;
}

void run(const Options& o) {
    const auto base=format_for(o); const auto nominal=geometry(base);
    const auto calendar=observation_calendar(o,base,nominal);
    const auto pad=calendar.pad,trail=calendar.trail;
    const auto noise=static_cast<double>(base.sample_rate)/std::pow(10.,o.cn0/10.);
    if (!std::isfinite(noise) || noise<=0) throw std::invalid_argument("C/N0 is outside numeric range");
    std::size_t total_correct=0,total_wrong=0,total_missed=0,total_false=0;
    for (unsigned trial=0;trial<o.trials;++trial) {
        const auto trial_started=Clock::now();const auto trial_cpu=std::clock();
        const auto seed=o.seed+trial,key_seed=o.key_seed.value_or(o.seed)+trial,channel_seed=o.channel_seed.value_or(o.seed)+trial; auto txkey=key_for(key_seed,0); auto rxkey=o.control=="wrong-key"?key_for(key_seed^0x6a09e667f3bcc909ULL,0):key_for(key_seed,0);
        auto txf=base; txf.symbol_ordinal=0;
        PatternSource source(txf,txkey,o.signal_bit);
        auto channel_cfg=channel_config(o,base,nominal,channel_seed,noise,pad,trail);
        const auto true_clock=truth_clock(channel_cfg),true_drift=truth_drift(channel_cfg);
        StreamingChannel channel(source,channel_cfg);
        auto candidate_bank=candidates_for(o,base,rxkey);
        for (auto& candidate : candidate_bank) candidate.arrival_seconds += pad;
        ReceiverConfig rc; rc.format=base; rc.candidates=std::move(candidate_bank);
        rc.observation_first_sample=0; rc.observation_samples=calendar.samples;
        if(channel.total_samples()<calendar.samples)throw std::runtime_error("sampled channel does not cover public observation calendar");
        rc.block_samples=block_samples(o,base); rc.maximum_chunk_samples=o.chunk; rc.threads=o.threads;
        const long double caller_bytes=source.working_bytes()+static_cast<long double>(channel.report().working_bytes)+
            o.chunk*(sizeof(Complex)+sizeof(std::uint8_t))+65536;
        if(caller_bytes>=o.memory_bytes)throw std::invalid_argument("source/channel exceed pipeline --memory-mib");
        rc.memory_limit_bytes=o.memory_bytes-static_cast<std::size_t>(caller_bytes); rc.complex_noise_variance=noise;
        rc.known_white_noise=channel.report().gaussian_projection_supported;
        rc.log_family_hypotheses=std::log(static_cast<double>(rc.candidates.size()))+
            std::log(std::ceil(o.horizon/o.attempt_spacing))+std::log(static_cast<double>(o.duration_alternatives));
        rc.family_false_admission_probability=o.false_probability;
        StreamingReceiver receiver(rc,rxkey);
        std::vector<Complex> samples(o.chunk); std::vector<std::uint8_t> validity(o.chunk);
        auto last_progress=Clock::now();
        while(channel.position()<calendar.samples) {
            const auto wanted=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),calendar.samples-channel.position()));
            const auto count=channel.read(std::span(samples).first(wanted),std::span(validity).first(wanted));
            if (!count) break;
            const auto chunk_first=channel.position()-count;
            receiver.push(chunk_first,std::span<const Complex>(samples.data(),count),std::span<const std::uint8_t>(validity.data(),count));
            const auto now=Clock::now();
            if(now-last_progress>=std::chrono::seconds(2)) {
                std::cerr<<"trial="<<trial<<" source_seconds="<<std::fixed<<std::setprecision(1)
                         <<static_cast<double>(channel.position())/base.sample_rate<<"/"
                         <<static_cast<double>(calendar.samples)/base.sample_rate<<" working_bytes="
                         <<(source.working_bytes()+channel.report().working_bytes+receiver.working_bytes())<<"\n";
                last_progress=now;
            }
        }
        auto report=receiver.finish(); const auto got=report.source_bit;
        const bool positive=o.control=="signal";
        const bool correct_epoch=rc.candidates[report.best_candidate].epoch_offset_seconds==0;
        const bool correct=report.accepted&&got==o.signal_bit&&correct_epoch&&positive;
        const bool wrong=report.accepted&&(got!=o.signal_bit||!correct_epoch)&&positive;
        const bool missed=positive&&!report.accepted;
        const bool false_admission=report.accepted&&!positive;
        total_correct+=correct;total_wrong+=wrong;total_missed+=missed;total_false+=false_admission;
        const auto pipeline_wall=std::chrono::duration<double>(Clock::now()-trial_started).count();
        const auto pipeline_cpu=static_cast<double>(std::clock()-trial_cpu)/CLOCKS_PER_SEC;
        std::cout<<std::setprecision(12)<<"{\"mode\":\"run\",\"version\":3,\"trial\":"<<trial
            <<",\"seed\":"<<seed<<",\"key_seed\":"<<key_seed<<",\"channel_seed\":"<<channel_seed<<",\"epoch\":"<<base.epoch<<",\"control\":\""<<o.control
            <<"\",\"channel\":\""<<o.channel<<"\",\"cn0_db_hz\":"<<o.cn0
            <<",\"complex_noise_variance\":"<<noise<<",\"noise_reference_power\":1,\"sample_rate\":"<<base.sample_rate
            <<",\"occupied_bandwidth_hz\":"<<nominal.occupied_bandwidth_hz
            <<",\"requested_active_seconds\":"<<base.bit_seconds<<",\"bit_seconds\":"<<nominal.bit_seconds<<",\"inner_rate_hz\":"<<base.inner_rate_hz
            <<",\"added_symbols\":"<<base.added_symbols<<",\"fine_rate_hz\":"<<nominal.fine_chip_rate_hz
            <<",\"rf_hz\":"<<o.rf_hz<<",\"truth_arrival_seconds\":"<<pad+channel_cfg.effects.timing_offset_seconds
            <<",\"truth_frequency_hz\":"<<truth_frequency(channel_cfg)
            <<",\"sample_clock_reference\":\"RX0ppm; TX carries declared relative error\",\"truth_clock_ppm\":"<<true_clock<<",\"truth_frequency_drift_hz_per_second\":"<<true_drift
            <<",\"basis_model\":\""<<o.basis<<"\",\"tap_delay_ms\":"<<o.tap_delay_ms
            <<",\"tap_doppler_hz\":"<<o.tap_doppler_hz
            <<",\"relative_phase_diffusion_rad_sqrt_s\":"<<o.phase_diffusion
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
        json_provenance(std::cout,o,base,nominal,trial);
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
    if(o.dropout_ms>0)throw std::invalid_argument("GPU export with erased observations is unsupported; use --mode run for selected erasures");
    const auto f=format_for(o);const auto g=geometry(f);
    const auto key_seed=o.key_seed.value_or(o.seed),channel_seed=o.channel_seed.value_or(o.seed);
    const auto txkey=key_for(key_seed,0);
    const auto rxkey=o.control=="wrong-key"?key_for(key_seed^0x6a09e667f3bcc909ULL,0):key_for(key_seed,0);
    auto candidates=candidates_for(o,f,rxkey);
    const auto calendar=observation_calendar(o,f,g);
    const auto pad=calendar.pad,trail=calendar.trail;
    const auto noise=static_cast<double>(f.sample_rate)/std::pow(10.,o.cn0/10.);
    if(!std::isfinite(noise)||noise<=0)throw std::invalid_argument("C/N0 is outside numeric range");
    auto config=channel_config(o,f,g,channel_seed,noise,pad,trail);config.effects.noise_enabled=false;
    PatternSource truth(f,txkey,o.signal_bit);StreamingChannel channel(truth,config);
    for(auto& c:candidates)c.arrival_seconds+=pad;
    const auto rows=calendar.samples;
    if(channel.total_samples()<rows)throw std::runtime_error("channel does not cover public export calendar");
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
            shaped_basis(candidate,c,f.sample_rate,at,n,std::span(values).first(n*actual_columns));
            for(std::size_t j=0;j<n;++j)for(std::size_t k=0;k<columns;++k)
                packed[j*columns+k]=k<actual_columns?pack(values[k*n+j]):std::complex<float>{};
            basis.write(reinterpret_cast<const char*>(packed.data()),static_cast<std::streamsize>(n*columns*sizeof(packed[0])));at+=n;
        }
        std::cerr<<"export candidate="<<++candidate_index<<'/'<<candidates.size()<<" samples="<<rows<<" columns="<<columns<<'\n';
    }
    basis.close();signal.close();if(!basis||!signal)throw std::runtime_error("failed writing GPU study files");
    std::ofstream manifest(directory/"manifest.json.partial");
    manifest<<std::setprecision(17)<<"{\"format\":\"triad-long-basis-v1\",\"byte_order\":\"native\",\"complex_type\":\"complex64\",\"layout\":\"candidate,sample,column\",\"basis_file\":\"basis.c64\",\"basis_shape\":["<<candidates.size()<<','<<rows<<','<<columns
        <<"],\"signal_file\":\"signal.c64\",\"signal_shape\":["<<rows<<"],\"sample_rate\":"<<f.sample_rate
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
        <<",\"channel\":\""<<o.channel<<"\",\"control\":\""<<o.control
        <<"\",\"truth_arrival_seconds\":"<<pad+config.effects.timing_offset_seconds
        <<",\"truth_frequency_hz\":"<<truth_frequency(config)<<",\"truth_clock_ppm\":"<<truth_clock(config)
        <<",\"truth_frequency_drift_hz_per_second\":"<<truth_drift(config)
        <<",\"relative_phase_diffusion_rad_sqrt_s\":"<<o.phase_diffusion
        <<",\"relative_white_fm_hz_sqrt_s\":"<<o.white_fm<<",\"relative_random_walk_fm_hz_sqrt_s\":"<<o.random_walk_fm
        <<",\"relative_flicker_fm_rms_hz\":"<<o.flicker_fm<<",\"relative_white_pm_rms_rad\":"<<o.white_pm
        <<",\"flicker_tau_seconds\":["<<o.flicker_min_tau<<','<<o.flicker_max_tau<<"],\"flicker_bands\":"<<o.flicker_bands
        <<",\"burst_wait_tiles\":"<<o.burst_wait_tiles<<",\"fhss_envelope_only\":"<<(o.fhss?"true":"false")
        <<",\"export_bytes\":"<<static_cast<std::uint64_t>(total_bytes)<<",\"bitlabels\":[";
    for(std::size_t i=0;i<candidates.size();++i){if(i)manifest<<',';manifest<<static_cast<unsigned>(candidates[i].source_bit);}
    manifest<<"],\"candidateparams\":[";
    for(std::size_t i=0;i<candidates.size();++i){if(i)manifest<<',';json_candidate(manifest,candidates[i]);}
    manifest<<"],\"message\":";
    json_string(manifest,o.frontend.active()?"sampled no-thermal channel pass including selected converter noise and interference; templates use only declared public channel bases":"noiseless sampled channel realization; templates use only declared public channel bases");
    json_provenance(manifest,o,f,g);
    json_frontend_report(manifest,channel.report());
    // This optional diagnostic label must not tell the detector hidden channel
    // truth. Only flat deterministic truth lying on the public grid gets one.
    // Independent interpolation and direct template evaluation may still differ.
    if(!o.frontend.active()&&o.control=="signal"&&o.channel=="none"&&o.phase_diffusion==0&&o.white_fm==0&&o.random_walk_fm==0&&o.flicker_fm==0&&o.white_pm==0&&truth_clock(config)==0&&truth_drift(config)==0){
        for(std::size_t i=0;i<candidates.size();++i){const auto& c=candidates[i];
            if(c.source_bit==o.signal_bit&&c.epoch_offset_seconds==0&&c.clock_ppm==0&&c.frequency_drift_hz_per_second==0&&
               c.arrival_seconds==pad+config.effects.timing_offset_seconds&&c.frequency_hz==truth_frequency(config)&&c.channel.taps.size()==1){manifest<<",\"true_candidate_index\":"<<i<<",\"true_candidate_note\":\"nominal parameter match; sampled channel interpolation is independently evaluated\"";break;}}
    }
    manifest<<"}\n";manifest.close();if(!manifest)throw std::runtime_error("failed writing manifest");
    std::filesystem::rename(directory/"basis.c64.partial",directory/"basis.c64");
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
    const auto noise=static_cast<double>(f.sample_rate)/std::pow(10.,o.cn0/10.);
    if(!std::isfinite(noise)||noise<=0)throw std::invalid_argument("C/N0 is outside numeric range");
    auto config=channel_config(o,f,g,channel_seed,noise,pad,trail);
    config.effects.noise_enabled=true;
    PatternSource truth(f,txkey,o.signal_bit);StreamingChannel channel(truth,config);
    for(auto& c:candidates)c.arrival_seconds+=pad;
    const auto rows=calendar.samples;
    if(channel.total_samples()<rows)throw std::runtime_error("channel does not cover public sampled-export calendar");
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
            shaped_basis(candidate,c,f.sample_rate,at,n,std::span(values).first(n*actual_columns));
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
        <<",\"sample_rate\":"<<f.sample_rate<<",\"sample_count\":"<<rows<<",\"template_sample_count\":"<<rows<<",\"observation_sample_count\":"<<rows<<",\"block_samples\":"<<block_samples(o,f)<<",\"chunk_samples\":"<<o.chunk
        <<",\"noise_variance_pre_frontend\":"<<noise<<",\"cn0_db_hz\":"<<o.cn0<<",\"noise_reference_power\":1"
        <<",\"signal_bit_simulation_truth\":"<<static_cast<unsigned>(o.signal_bit)<<",\"signal_enabled\":"<<(o.control!="null"?"true":"false")
        <<",\"seed\":"<<o.seed<<",\"key_seed\":"<<key_seed<<",\"channel_seed\":"<<channel_seed<<",\"epoch\":"<<f.epoch
        <<",\"source_samples\":"<<g.source_samples<<",\"requested_active_seconds\":"<<f.bit_seconds<<",\"actual_elapsed_seconds\":"<<g.bit_seconds
        <<",\"channel_samples\":"<<report.samples<<",\"channel_tail_samples\":"<<report.tail_samples
        <<",\"channel_signal_energy_samples\":"<<report.signal_energy<<",\"channel_observed_energy_samples\":"<<report.observed_energy
        <<",\"sampled_observation_wall_seconds\":"<<sampled_seconds<<",\"basis_generation_and_write_wall_seconds\":"<<basis_seconds
        <<",\"export_bytes\":"<<static_cast<std::uint64_t>(total_bytes)<<",\"channel\":";
    json_string(manifest,o.channel);manifest<<",\"control\":";json_string(manifest,o.control);
    manifest<<",\"fhss_envelope_only\":"<<(o.fhss?"true":"false")<<",\"oscillator_model\":{\"relative_phase_diffusion_rad_sqrt_s\":"<<o.phase_diffusion
        <<",\"relative_white_fm_hz_sqrt_s\":"<<o.white_fm<<",\"relative_random_walk_fm_hz_sqrt_s\":"<<o.random_walk_fm
        <<",\"relative_flicker_fm_rms_hz\":"<<o.flicker_fm<<",\"flicker_tau_seconds\":["<<o.flicker_min_tau<<','<<o.flicker_max_tau
        <<"],\"flicker_bands\":"<<o.flicker_bands<<",\"relative_white_pm_rms_rad\":"<<o.white_pm<<'}'
        <<",\"candidate_count\":"<<candidates.size()<<",\"candidates\":[";
    for(std::size_t i=0;i<candidates.size();++i){if(i)manifest<<',';json_candidate(manifest,candidates[i]);}
    manifest<<"],\"candidate_bits\":[";for(std::size_t i=0;i<candidates.size();++i){if(i)manifest<<',';manifest<<static_cast<unsigned>(candidates[i].source_bit);}manifest<<']';
    manifest<<",\"file_sizes\":{\"basis.c128\":"<<std::filesystem::file_size(directory/"basis.c128.partial")<<",\"observation.c128\":"<<std::filesystem::file_size(directory/"observation.c128.partial")<<",\"validity.u8\":"<<std::filesystem::file_size(directory/"validity.u8.partial")<<'}';
    json_provenance(manifest,o,f,g);json_frontend_report(manifest,report);manifest<<"}\n";manifest.close();
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
    const auto noise=static_cast<double>(f.sample_rate)/std::pow(10.,o.cn0/10.);
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
    const long double total_bytes=gram_bytes+vector_bytes+fixed_bytes+(o.stats_noisy?vector_bytes:0)+(o.stats_noisy?rows:0);
    if(rows>o.export_max_samples)throw std::invalid_argument("export-stats exceeds --export-max-samples (at most128000000)");
    if(total_bytes>o.export_max_bytes)throw std::invalid_argument("export-stats exceeds explicit --export-max-mib disk budget");
    std::size_t max_local=0;for(const auto& c:candidates)max_local=std::max(max_local,c.channel.taps.size()*(c.channel.linear_gain?2:1));
    const long double scratch=static_cast<long double>(o.chunk)*(d+max_local+1)*sizeof(Complex)+
        static_cast<long double>(d)*d*sizeof(Complex)+static_cast<long double>(d)*sizeof(Complex)+65536;
    long double sources_bytes=0;
    for(const auto& c:candidates){(void)c;sources_bytes+=g.source_workspace_bytes;}
    const long double channel_bytes=static_cast<long double>(g.source_workspace_bytes)+
        static_cast<long double>(rows?std::min<std::uint64_t>(rows,o.chunk):0)*sizeof(Complex)*3+65536;
    if(scratch+sources_bytes+channel_bytes>o.memory_bytes)throw std::invalid_argument("export-stats bounded scratch/source estimate exceeds --memory-mib");
    std::filesystem::create_directories(o.output);const auto directory=std::filesystem::path(o.output);
    std::vector<std::string> names{"gram.c128","mean.c128","intervals.u64","valid_rows.u64","signal_energy.f64","manifest.json"};
    if(o.stats_noisy)names.push_back("observation.c128");
    for(const auto& name:names)if(std::filesystem::exists(directory/name)||std::filesystem::exists(directory/(name+".partial")))
        throw std::invalid_argument("export files already exist; choose a fresh output directory");
    if(std::filesystem::exists(directory/"validity-mask.tmp"))throw std::invalid_argument("export files already exist; choose a fresh output directory");
    std::ofstream gram(directory/"gram.c128.partial",std::ios::binary),mean(directory/"mean.c128.partial",std::ios::binary),
        intervals(directory/"intervals.u64.partial",std::ios::binary),valid_rows(directory/"valid_rows.u64.partial",std::ios::binary),
        energy(directory/"signal_energy.f64.partial",std::ios::binary);
    std::ofstream observation;
    if(o.stats_noisy)observation.open(directory/"observation.c128.partial",std::ios::binary);
    if(!gram||!mean||!intervals||!valid_rows||!energy||(o.stats_noisy&&!observation))throw std::runtime_error("cannot create export-stats files");
    std::vector<std::unique_ptr<PatternSource>> sources; sources.reserve(candidates.size());
    for(const auto& c:candidates){auto addressed=f;addressed.epoch=offset_epoch(f.epoch,c.epoch_offset_seconds);sources.push_back(std::make_unique<PatternSource>(addressed,rxkey,c.source_bit));}
    std::vector<Complex> basis_values(o.chunk*d),local_values(o.chunk*max_local),samples(o.chunk);
    std::vector<std::uint8_t> validity(o.chunk);
    double actual_signal_energy=0,actual_observed_energy=0,selected_truth_arrival=0,selected_truth_frequency=0;
    double selected_truth_clock=0,selected_truth_drift=0;
    std::uint64_t actual_channel_samples=0,actual_channel_tail=0;
    StreamChannelReport frontend_report{};
    auto pass=[&](bool noisy_pass){
        PatternSource truth(f,txkey,o.signal_bit);
        auto config=channel_config(o,f,g,channel_seed,noise,pad,trail);
        config.effects.noise_enabled=noisy_pass;
        StreamingChannel channel(truth,config);
        if(channel.total_samples()<rows)throw std::runtime_error("channel does not cover public export-stats calendar");
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
                    shaped_basis(*sources[ci],candidates[ci],f.sample_rate,at,n,std::span(local_values).first(n*cols));
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
            selected_truth_arrival=pad+config.effects.timing_offset_seconds;selected_truth_frequency=truth_frequency(config);
            selected_truth_clock=truth_clock(config);selected_truth_drift=truth_drift(config);
            if(o.stats_noisy){mask.close();if(!mask)throw std::runtime_error("failed writing temporary validity mask");}
        } else {
            std::ifstream mask(directory/"validity-mask.tmp",std::ios::binary);
            if(!mask)throw std::runtime_error("cannot read temporary validity mask");
            std::uint64_t at=0,block_count=0;std::vector<Complex> projection(d);std::vector<std::uint8_t> expected_mask(o.chunk);
            while(at<rows){const auto want=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,rows-at));
                const auto n=channel.read(std::span(samples).first(want),std::span(validity).first(want));
                if(!n)throw std::runtime_error("noisy channel ended before export-stats calendar");
                mask.read(reinterpret_cast<char*>(expected_mask.data()),static_cast<std::streamsize>(n));
                if(mask.gcount()!=static_cast<std::streamsize>(n)||!std::equal(expected_mask.begin(),expected_mask.begin()+n,validity.begin()))
                    throw std::runtime_error("noisy and noiseless channel erasure masks differ");
                std::fill(basis_values.begin(),basis_values.begin()+n*d,Complex{});
                for(std::size_t ci=0;ci<candidates.size();++ci){const auto cols=mapping[ci].size();shaped_basis(*sources[ci],candidates[ci],f.sample_rate,at,n,std::span(local_values).first(n*cols));
                    for(std::size_t j=0;j<cols;++j)for(std::size_t r=0;r<n;++r)basis_values[mapping[ci][j]*n+r]=local_values[j*n+r];}
                for(std::size_t r=0;r<n;++r){if(validity[r]){for(std::size_t j=0;j<d;++j)projection[j]+=std::conj(basis_values[j*n+r])*samples[r];}
                    ++block_count;if(block_count==block||at+r+1==rows){
                        for(auto v:projection)write_le_complex(observation,v);std::fill(projection.begin(),projection.end(),Complex{});block_count=0;
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
    gram.close();mean.close();intervals.close();valid_rows.close();energy.close();if(o.stats_noisy)observation.close();
    if(!gram||!mean||!intervals||!valid_rows||!energy||(o.stats_noisy&&!observation))throw std::runtime_error("failed writing export-stats files");
    auto size_of=[&](const std::string& n){return std::filesystem::file_size(directory/(n+".partial"));};
    std::ofstream manifest(directory/"manifest.json.partial");
    manifest<<std::setprecision(17)<<"{\"format\":\"triad-block-stats-v1\",\"byte_order\":\"little\",\"complex_type\":\"complex128\",\"real_type\":\"float64\",\"integer_type\":\"uint64\",\"layout\":\"block,row,column\",\"block_count\":"<<blocks
       <<",\"dimension\":"<<d<<",\"sample_rate\":"<<f.sample_rate<<",\"noise_variance\":"<<noise
       <<",\"gram_file\":\"gram.c128\",\"gram_shape\":["<<blocks<<','<<d<<','<<d<<"],\"mean_file\":\"mean.c128\",\"mean_shape\":["<<blocks<<','<<d<<']'
       <<",\"intervals_file\":\"intervals.u64\",\"intervals_shape\":["<<blocks<<",2],\"valid_rows_file\":\"valid_rows.u64\",\"valid_rows_shape\":["<<blocks<<']'
       <<",\"signal_energy_file\":\"signal_energy.f64\",\"signal_energy_shape\":["<<blocks<<']'
       <<",\"sample_count\":"<<rows<<",\"block_samples\":"<<block<<",\"signal_bit\":"<<static_cast<unsigned>(o.signal_bit)
       <<",\"key_seed\":"<<key_seed<<",\"channel_seed\":"<<channel_seed<<",\"legacy_seed\":"<<o.seed
       <<",\"epoch\":"<<f.epoch<<",\"source_samples\":"<<g.source_samples<<",\"requested_active_seconds\":"<<f.bit_seconds<<",\"actual_elapsed_seconds\":"<<g.bit_seconds
       <<",\"nominal_payload_energy_seconds_lower\":"<<g.nominal_payload_energy_seconds<<",\"channel_signal_energy_samples\":"<<actual_signal_energy
       <<",\"channel_observed_energy_samples\":"<<actual_observed_energy<<",\"channel_samples\":"<<actual_channel_samples<<",\"channel_tail_samples\":"<<actual_channel_tail
       <<",\"source_hash_binding\":\"not included; bind externally to the exact source and binary\""
       <<",\"selected_truth_arrival_seconds_simulation_only\":"<<selected_truth_arrival<<",\"selected_truth_frequency_hz_simulation_only\":"<<selected_truth_frequency
       <<",\"selected_truth_clock_ppm_simulation_only\":"<<selected_truth_clock<<",\"selected_truth_frequency_drift_hz_per_second_simulation_only\":"<<selected_truth_drift
       <<",\"cn0_db_hz\":"<<o.cn0<<",\"channel\":\""<<o.channel<<"\",\"control\":\""<<o.control<<"\",\"signal_enabled\":"<<(o.control!="null"?"true":"false")
       <<",\"oscillator_model\":{\"relative_phase_diffusion_rad_sqrt_s\":"<<o.phase_diffusion<<",\"relative_white_fm_hz_sqrt_s\":"<<o.white_fm
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
    if(o.stats_noisy)manifest<<",\"observation_file\":\"observation.c128\",\"observation_shape\":["<<blocks<<','<<d<<"],\"observation_semantics\":\"A^H W y from a second actual channel pass; simulation evaluation only\"";
    manifest<<",\"file_sizes\":{\"gram.c128\":"<<size_of("gram.c128")<<",\"mean.c128\":"<<size_of("mean.c128")<<",\"intervals.u64\":"<<size_of("intervals.u64")<<",\"valid_rows.u64\":"<<size_of("valid_rows.u64")<<",\"signal_energy.f64\":"<<size_of("signal_energy.f64");if(o.stats_noisy)manifest<<",\"observation.c128\":"<<size_of("observation.c128");manifest<<'}';
    json_provenance(manifest,o,f,g);json_frontend_report(manifest,frontend_report);manifest<<"}\n";manifest.close();if(!manifest)throw std::runtime_error("failed writing export-stats manifest");
    for(const auto& n:names)if(n!="manifest.json")std::filesystem::rename(directory/(n+".partial"),directory/n);
    std::filesystem::rename(directory/"manifest.json.partial",directory/"manifest.json");
}

} // namespace

int main(int argc,char** argv) {
    try {
        const auto options=parse(argc,argv);
        if(options.mode=="plan") print_plan(options);
        else if(options.mode=="run") run(options);
        else if(options.mode=="export") export_basis(options);
        else if(options.mode=="export-sampled") export_sampled(options);
        else export_stats(options);
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"triad_long: "<<e.what()<<"\n";
        return 2;
    }
}
