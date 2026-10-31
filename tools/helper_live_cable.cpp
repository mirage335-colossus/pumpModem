// Bounded physical-cable connectivity probe for the standalone Helper study.
// This does not open audio devices or infer RF-path performance. The separate
// cable_audio_capture tool handles playback and capture through the audio API.
#include "datapump/helper_carrier.hpp"
#include "datapump/helper_measurement.hpp"
#include "datapump/helper_phase_clock.hpp"
#include "datapump/helper_startup_timing.hpp"
#include "datapump/helper_stream_conditioner.hpp"
#include "datapump/resampler.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t rate = 48000;
constexpr double pi = std::numbers::pi;
constexpr std::size_t block = rate / 20; // 50 ms; onset derives from Helper only

void generate(const char* path, bool full_observation, double audio_ppm = 100) {
    datapump::helper::Options options;
    options.power_w = 100;
    options.data_power_w = 1;
    options.callsign = "N0CALL";
    // Actual Helper waveform, with independently injected low-level tone.
    // The sum remains below digital full scale without setting mixer levels.
    const datapump::helper::Plan plan{2400, .12, 0, 100, 950, 550, false};
    datapump::helper::Generator generator(options, plan, 1800000000, true);
    const unsigned duration_seconds = full_observation ? 40U : 2U;
    std::vector<float> data(static_cast<std::size_t>(rate) * duration_seconds);
    double peak = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const long double t = static_cast<long double>(i) / rate;
        const long double audio_scale=full_observation ? 1.L+audio_ppm*1e-6L : 1.L;
        const long double source = t * audio_scale;
        auto mixed = generator.mix_analytic({}, source, rate,
            static_cast<long double>(duration_seconds) * audio_scale, 0.L);
        // Full mode uses the same input-time RF translation and source clock
        // on both pilots and the probe, with the probe after the 38s lead.
        // These physical stimulus inputs must not be used to select receiver
        // correction coefficients or independently synchronize the probe.
        if (!full_observation || source >= 38.L) {
            const double angle = 2 * pi * 1600 * static_cast<double>(source);
            mixed += std::polar(.02, angle);
        }
        if (full_observation) mixed *= std::polar(1., 2 * pi * 4 * static_cast<double>(t));
        data[i] = static_cast<float>(mixed.real());
        peak = std::max(peak, std::abs(static_cast<double>(data[i])));
    }
    auto* file = std::fopen(path, "wbx");
    if (!file) throw std::runtime_error("cannot exclusively create cable stimulus");
    const bool ok = std::fwrite(data.data(), sizeof(float), data.size(), file) == data.size();
    const bool closed = std::fclose(file) == 0;
    if (!ok || !closed) throw std::runtime_error("cannot write cable stimulus");
    std::cout << std::setprecision(9) << "stimulus_seconds=" << duration_seconds << " rate_hz=" << rate
              << " helper_gain=0.12 probe_peak=0.02 composite_peak=" << peak;
    if (full_observation) std::cout << " audio_error_ppm=" << audio_ppm << " common_offset_hz=4 probe_start_seconds=38";
    std::cout << '\n';
}

// Deterministic analog-loopback approximation. This is a simulation, not a
// replay of a hardware recording: one real path with fixed gain and delay,
// optional first-order high-pass, receiver sample-clock error, white thermal-
// like noise and ideal PCM quantization. The measured preset is a compact
// model fitted only to pre-probe Helper tones; it is not a complete analog
// transfer, noise, clock or hardware model.
struct SimulatedCable {
    double gain=.84,delay_ms=20,noise_rms=.000085,adc_ppm=0;
    double highpass_hz=0;
    double dc=0,colored_noise_rms=0,colored_noise_tau_ms=71.4;
    // The fitted recording supplies post-ADC white residual variance. Under
    // the explicit dithered, uniform, uncorrelated S16-quantization model we
    // subtract Delta^2/12 once to infer the latent white-noise parameter.
    // That decomposition is an approximation, not identified from one capture.
    double measured_white_rms_including_quantization=0;
    std::string noise_rms_basis="configured_pre_quantization";
    std::uint64_t seed=901;
    unsigned quantization_bits=16;
    std::string preset="flat";
};

class StableNoise {
public:
    explicit StableNoise(std::uint64_t seed):state_(seed) {
        if(!seed)throw std::runtime_error("simulation seed must be nonzero");
    }
    double sample() {
        // Sum of twelve independent uniform variates has mean zero and unit
        // variance. This is a reproducible Gaussian approximation, not a
        // platform-dependent std::normal_distribution implementation.
        double sum=0;
        for(int i=0;i<12;++i) {
            state_^=state_>>12;state_^=state_<<25;state_^=state_>>27;
            const auto word=state_*UINT64_C(2685821657736338717);
            sum+=static_cast<double>(word>>40)*(1.0/16777216.0);
        }
        return sum-6;
    }
private:
    std::uint64_t state_;
};

void simulate_long(const char* input_path,const char* output_path,const SimulatedCable& c) {
    if(!std::isfinite(c.gain)||c.gain<0||c.gain>4||!std::isfinite(c.delay_ms)||c.delay_ms<0||c.delay_ms>500||
       !std::isfinite(c.noise_rms)||c.noise_rms<0||c.noise_rms>1||!std::isfinite(c.adc_ppm)||
       !std::isfinite(c.highpass_hz)||c.highpass_hz<0||c.highpass_hz>1000||
       !std::isfinite(c.dc)||std::abs(c.dc)>1||!std::isfinite(c.colored_noise_rms)||
       c.colored_noise_rms<0||c.colored_noise_rms>1||!std::isfinite(c.colored_noise_tau_ms)||
       c.colored_noise_tau_ms<=0||c.colored_noise_tau_ms>10000||
       std::abs(c.adc_ppm)>1000||!c.seed||(c.quantization_bits!=0&&(c.quantization_bits<8||c.quantization_bits>24)))
        throw std::runtime_error("invalid simulated cable configuration");
    if constexpr(std::endian::native!=std::endian::little)
        throw std::runtime_error("simulated cable float PCM requires a little-endian host");
    std::ifstream f(input_path,std::ios::binary|std::ios::ate);
    constexpr auto input_samples=static_cast<std::size_t>(40)*rate;
    if(!f||f.tellg()!=static_cast<std::streamoff>(input_samples*sizeof(float)))
        throw std::runtime_error("simulation requires exactly 40s of mono f32le stimulus");
    std::vector<float> input(input_samples);
    f.seekg(0);if(!f.read(reinterpret_cast<char*>(input.data()),static_cast<std::streamsize>(input.size()*sizeof(float))))
        throw std::runtime_error("cannot read simulated cable stimulus");
    for(float value:input)if(!std::isfinite(value))throw std::runtime_error("nonfinite simulated cable stimulus");
    constexpr double preroll=.75,postroll=1.;
    const double start=preroll+c.delay_ms/1000;
    const double adc_scale=1+c.adc_ppm*1e-6;
    const double physical_sample_rate=rate*adc_scale;
    const auto n=static_cast<std::size_t>(std::ceil((start+40+postroll)*physical_sample_rate));
    std::vector<float> output(n);
    StableNoise rng(c.seed);
    const double scale=c.quantization_bits?std::ldexp(1.,static_cast<int>(c.quantization_bits)-1):0;
    // The analog model evolves in physical seconds. Both filter poles must
    // therefore use the actual ADC rate, just like its source sample times.
    const double hp_a=std::exp(-2*pi*c.highpass_hz/physical_sample_rate);
    double hp_previous_input=0,hp_previous_output=0;
    const double color_a=std::exp(-1/(physical_sample_rate*c.colored_noise_tau_ms/1000));
    const double color_step=c.colored_noise_rms*std::sqrt(1-color_a*color_a);
    // A stationary initial draw avoids treating the prerecorded 0.75-second
    // preroll as an artificially noiseless filter startup.
    double colored_noise=c.colored_noise_rms>0?c.colored_noise_rms*rng.sample():0;
    std::size_t clipped=0;double sum_squares=0,peak=0;
    for(std::size_t i=0;i<n;++i) {
        // Receiver samples occur at its physical clock instants. Cubic
        // interpolation gives a flatter audio-band fractional-delay response
        // than a two-point interpolator, which otherwise adds substantial
        // frequency-dependent pilot attenuation and phase. It is still an
        // approximation, not a calibrated analog-channel response. Neither
        // the delay nor simulation truth is supplied to the analyzer.
        const double source=(static_cast<double>(i)/physical_sample_rate-start)*rate;
        double value=0;
        if(source>=0&&source<static_cast<double>(input.size())) {
            const auto k=static_cast<std::size_t>(source);
            const double fraction=source-static_cast<double>(k);
            const double p0=k?input[k-1]:0;
            const double p1=input[k];
            const double p2=k+1<input.size()?input[k+1]:0;
            const double p3=k+2<input.size()?input[k+2]:0;
            value=c.gain*(p1+.5*fraction*(p2-p0+fraction*(2*p0-5*p1+4*p2-p3+
                  fraction*(3*(p1-p2)+p3-p0))));
        }
        if(c.highpass_hz>0) {
            const double filtered=hp_a*hp_previous_output+(1+hp_a)*.5*(value-hp_previous_input);
            hp_previous_input=value;hp_previous_output=filtered;value=filtered;
        }
        value+=c.dc;
        if(c.noise_rms>0)value+=c.noise_rms*rng.sample();
        if(c.colored_noise_rms>0) {
            colored_noise=color_a*colored_noise+color_step*rng.sample();
            value+=colored_noise;
        }
        if(c.quantization_bits) {
            if(value < -1 || value > 1-1/scale)++clipped;
            value=std::clamp(value,-1.,1-1/scale);
            value=std::round(value*scale)/scale;
        }
        output[i]=static_cast<float>(value);sum_squares+=value*value;peak=std::max(peak,std::abs(value));
    }
    auto* file=std::fopen(output_path,"wbx");
    if(!file)throw std::runtime_error("cannot exclusively create simulated cable capture");
    const bool written=std::fwrite(output.data(),sizeof(float),output.size(),file)==output.size();
    const bool closed=std::fclose(file)==0;
    if(!written||!closed)throw std::runtime_error("cannot write simulated cable capture");
    std::cout<<std::setprecision(9)<<"simulation=mono_cable preset="<<c.preset<<" input_seconds=40 preroll_seconds="<<preroll
             <<" postroll_seconds="<<postroll<<" delay_ms="<<c.delay_ms<<" gain="<<c.gain
             <<" highpass_hz="<<c.highpass_hz<<" noise_rms="<<c.noise_rms<<" dc="<<c.dc
             <<" colored_noise_rms="<<c.colored_noise_rms<<" colored_noise_tau_ms="<<c.colored_noise_tau_ms
             <<" noise_model=12_uniform_approximation seed="<<c.seed
             <<" noise_rms_basis="<<c.noise_rms_basis
             <<" measured_white_rms_including_quantization="<<c.measured_white_rms_including_quantization
             <<" adc_ppm="<<c.adc_ppm<<" physical_sample_rate_hz="<<physical_sample_rate
             <<" highpass_pole="<<hp_a<<" colored_noise_pole="<<color_a
             <<" quantization_bits="<<c.quantization_bits
             <<" capture_samples="<<n<<" capture_rms="<<std::sqrt(sum_squares/static_cast<double>(n))
             <<" capture_peak="<<peak<<" clipped_samples="<<clipped<<'\n';
}

double tone_amplitude(const float* data, double frequency) {
    const auto step = std::polar(1., -2 * pi * frequency / rate);
    std::complex<double> phase{1, 0}, sum{};
    for (std::size_t i = 0; i < block; ++i) {
        sum += static_cast<double>(data[i]) * phase;
        phase *= step;
    }
    return 2 * std::abs(sum) / static_cast<double>(block);
}

void analyze(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot read cable capture");
    const auto size = f.tellg();
    if (size <= 0 || size % static_cast<std::streamoff>(sizeof(float)) != 0 ||
        size > static_cast<std::streamoff>(rate * 15 * sizeof(float)))
        throw std::runtime_error("invalid cable capture length");
    std::vector<float> data(static_cast<std::size_t>(size) / sizeof(float));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(data.data()), size)) throw std::runtime_error("truncated capture");
    double peak = 0, square = 0;
    std::size_t clipped = 0;
    for (float x : data) {
        if (!std::isfinite(x)) throw std::runtime_error("nonfinite cable capture");
        peak = std::max(peak, std::abs(static_cast<double>(x)));
        square += static_cast<double>(x) * x;
        clipped += std::abs(x) >= .999F;
    }
    const auto n = data.size() / block;
    if (n < 30) throw std::runtime_error("capture too short for Helper-onset analysis");
    std::vector<double> high(n), low(n), probe(n);
    for (std::size_t i = 0; i < n; ++i) {
        high[i] = tone_amplitude(data.data() + i * block, 2400);
        low[i] = tone_amplitude(data.data() + i * block, 550);
        probe[i] = tone_amplitude(data.data() + i * block, 1600);
    }
    // The first 300 ms precede playback under cable_audio_capture's 750 ms
    // preroll. Only the high Helper tone selects a first active block.
    const double baseline = *std::max_element(high.begin(), high.begin() + 6);
    const double threshold = std::max(1e-6, 6 * baseline);
    std::size_t onset = n;
    for (std::size_t i = 6; i + 1 < n; ++i)
        if (high[i] > threshold && high[i + 1] > threshold) { onset = i; break; }
    const auto max_amplitude = [](const std::vector<double>& values) {
        return *std::max_element(values.begin(), values.end());
    };
    std::cout << std::setprecision(9) << "capture_seconds=" << static_cast<double>(data.size()) / rate
              << " capture_rms=" << std::sqrt(square / static_cast<double>(data.size()))
              << " capture_peak=" << peak << " near_fullscale_samples=" << clipped
              << " helper_high_baseline_amplitude=" << baseline
              << " helper_high_peak_amplitude=" << max_amplitude(high)
              << " helper_low_peak_amplitude=" << max_amplitude(low)
              << " fixed_probe_1600_peak_amplitude=" << max_amplitude(probe)
              << " helper_onset_50ms_block=";
    if (onset == n) std::cout << "unavailable";
    else std::cout << onset << " helper_onset_seconds=" << static_cast<double>(onset) / 20;
    std::cout << '\n';
    if (onset == n) throw std::runtime_error("Helper tone was not detected above the preroll baseline");
}

// Test-local real-mono-to-I/Q adapter. The production resampler performs the
// 48 kHz to 12 kHz conversion; a causal, 129-tap Blackman-windowed Hilbert
// filter then forms an analytic signal. Its 64-sample delay is carried in the
// observed Helper onset, rather than hidden by an assumed playback timestamp.
std::vector<std::complex<float>> mono_to_iq(const std::vector<float>& input) {
    constexpr std::uint32_t fs = 12000;
    datapump::audio::Resampler resampler(rate, fs);
    std::vector<float> real;
    std::array<float,4096> buffer{};
    std::size_t consumed = 0;
    while (!resampler.finished()) {
        const auto p = resampler.process(std::span<const float>(input).subspan(consumed), buffer, true);
        consumed += p.consumed;
        real.insert(real.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(p.produced));
        if (!p.consumed && !p.produced && !resampler.finished())
            throw std::runtime_error("cable downsampler stalled");
    }
    constexpr std::size_t delay = 64;
    std::array<double,32> taps{};
    for (std::size_t j = 0; j < taps.size(); ++j) {
        const double k = static_cast<double>(2*j+1);
        const double w = .42 + .5 * std::cos(pi*k/delay) + .08 * std::cos(2*pi*k/delay);
        taps[j] = 2*w/(pi*k);
    }
    std::vector<std::complex<float>> result(real.size());
    for (std::size_t i = 2*delay; i < real.size(); ++i) {
        double q = 0;
        for (std::size_t j = 0; j < taps.size(); ++j) {
            const auto k = 2*j+1;
            q += taps[j] * (static_cast<double>(real[i-delay-k]) - real[i-delay+k]);
        }
        result[i] = {real[i-delay], static_cast<float>(q)};
    }
    return result;
}

double high_block_amplitude(std::span<const std::complex<float>> data) {
    std::complex<double> sum{}, phase{1, 0};
    const auto step = std::polar(1., -2*pi*2400/12000);
    for (auto x : data) {sum += std::complex<double>(x)*phase; phase *= step;}
    return std::abs(sum)/static_cast<double>(data.size());
}

void analyze_long(const char* path) {
    constexpr std::uint32_t fs=12000;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot read cable capture");
    const auto bytes=f.tellg();
    if (bytes <= static_cast<std::streamoff>(40*rate*sizeof(float)) ||
        bytes > static_cast<std::streamoff>(55*rate*sizeof(float)) ||
        bytes % static_cast<std::streamoff>(sizeof(float)))
        throw std::runtime_error("invalid long cable capture length");
    std::vector<float> input(static_cast<std::size_t>(bytes)/sizeof(float));
    f.seekg(0);if (!f.read(reinterpret_cast<char*>(input.data()),bytes))throw std::runtime_error("truncated capture");
    std::size_t clipped=0;
    for(float x:input) {if(!std::isfinite(x))throw std::runtime_error("nonfinite capture");clipped+=std::abs(x)>=.999F;}
    auto iq=mono_to_iq(input);
    // Coarse start uses only the high Helper pilot. Baseline comes from the
    // capture's first 300 ms, before the standalone capture tool's 750 ms
    // playback preroll. Five-millisecond bins keep the subsequent search
    // window independent of the probe and resolve the startup neighborhood.
    constexpr std::size_t bin=fs/200;
    const auto count=iq.size()/bin;
    // Only the first two capture seconds may determine onset and threshold.
    // The diagnostic payload is not present until the much later fixed lead,
    // so even leakage into the Helper bin cannot influence acquisition.
    const auto early_count=std::min<std::size_t>(count,2*fs/bin);
    std::vector<double> high(early_count);
    for(std::size_t i=0;i<early_count;++i)high[i]=high_block_amplitude(std::span(iq).subspan(i*bin,bin));
    const auto peak=*std::max_element(high.begin(),high.begin()+static_cast<std::ptrdiff_t>(early_count));
    const auto baseline=*std::max_element(high.begin(),high.begin()+60);
    const auto threshold=std::max(6*baseline,.02*peak);
    std::size_t coarse=count;
    for(std::size_t i=60;i+1<early_count;++i)if(high[i]>threshold && high[i+1]>threshold){coarse=i;break;}
    if(coarse==count || coarse<3)throw std::runtime_error("Helper-only coarse onset unavailable");
    const auto coarse_time=static_cast<double>(coarse*bin)/fs;
    // Only complete settled Helper apertures support the clock fit. No
    // payload samples or playback marker are used to choose these positions.
    datapump::helper::HelperMeasurement receiver({fs,550,2400,2385,2415,300,0});
    unsigned accepted=0;
    std::vector<datapump::helper::HelperDwellMeasurement> history;
    history.reserve(100);
    for(unsigned i=0;i<90;++i) {
        const auto first=static_cast<std::size_t>(std::llround((coarse_time+.02+.4*i)*fs));
        constexpr std::size_t length=static_cast<std::size_t>(.36*fs);
        if(first+length>iq.size())throw std::runtime_error("capture ends before Helper observation");
        const auto m=receiver.observe(std::span(iq).subspan(first,length),30e6,static_cast<double>(first)/fs);
        accepted+=m.valid;
        history.push_back(m);
    }
    const auto snap=receiver.snapshot();
    std::cout<<std::setprecision(9)<<"cable_helper_apertures="<<accepted<<"/90 fitted="<<snap.valid
             <<" coarse_onset_seconds="<<coarse_time<<" near_fullscale_samples="<<clipped;
    if(!snap.valid) {std::cout<<" correction=unavailable\n";return;}
    const double a=1+snap.audio_ppm*1e-6,b=30e6*snap.shift_ppm*1e-6;
    std::cout<<" measured_audio_ppm="<<snap.audio_ppm<<" measured_common_offset_hz="<<b;
    const auto start=static_cast<std::size_t>(std::llround((coarse_time-.010)*fs));
    if(start+fs/25>iq.size())throw std::runtime_error("missing startup capture");
    datapump::helper::HelperStartupTimingConfig config;
    config.sample_rate=fs;config.first_sample_seconds=static_cast<double>(start)/fs;
    config.search_min_seconds=coarse_time-.005;config.search_max_seconds=coarse_time+.005;
    config.audio_scale=a;config.common_offset_hz=b;config.hopping=false;
    const auto onset=datapump::helper::estimate_helper_startup_timing(std::span(iq).subspan(start,fs/25),config);
    std::cout<<" helper_startup_valid="<<onset.valid<<" onset_seconds="<<onset.onset_seconds
             <<" onset_radius_seconds="<<onset.support_radius_seconds
             <<" startup_channel_score="<<onset.channel_consistency_score
             <<" startup_channel_threshold="<<onset.channel_consistency_threshold;
    if(!onset.valid) {std::cout<<" phase_correction=unavailable\n";return;}
    // Propagate the actual dated audio-frequency-and-drift covariance across
    // the entire Helper-to-probe interval. Startup alone does not establish
    // the pilot phase branch 38 seconds later. This is still conditional on
    // the reported local model and the startup channel-consistency witness.
    const double reference=snap.time_seconds;
    const double audio_rate=snap.audio_drift_ppm_per_second*1e-6;
    const auto clock_at=[&](double t) {
        return a*(t-onset.onset_seconds)+.5*audio_rate*
            (std::pow(t-reference,2)-std::pow(onset.onset_seconds-reference,2));
    };
    const auto input_at=[&](double source_seconds) {
        double t=onset.onset_seconds+source_seconds/a;
        for(int i=0;i<4;++i)t-=(clock_at(t)-source_seconds)/(a+audio_rate*(t-reference));
        return t;
    };
    const double probe_end_time=input_at(40);
    const double j0=probe_end_time-onset.onset_seconds;
    const double j2=.5*(std::pow(probe_end_time-reference,2)-std::pow(onset.onset_seconds-reference,2));
    const double variance=1e-12*(j0*j0*snap.covariance[0][0]+2*j0*j2*snap.covariance[0][2]+
        j2*j2*snap.covariance[2][2]);
    const double timing_bound=onset.support_radius_seconds*std::abs(a)+3*std::sqrt(std::max(0.,variance));
    constexpr double half_branch=.5/(2400-550);
    const bool affine_timing_available=variance>=0 && std::isfinite(timing_bound) && timing_bound<half_branch;
    std::cout<<" audio_drift_ppm_s="<<snap.audio_drift_ppm_per_second
             <<" timing_bound_3sigma_seconds="<<timing_bound
             <<" timing_half_branch_seconds="<<half_branch
             <<" timing_model=conditional_linear"
             <<" affine_timing_valid="<<affine_timing_available;
    datapump::helper::HelperPhaseClock phase_clock({});
    bool phase_valid=phase_clock.seed(onset);
    datapump::helper::HelperPhaseClock::Observation phase_observation;
    for(const auto& measurement:history) {
        phase_observation=phase_clock.observe(measurement);
        phase_valid=phase_valid && phase_observation.valid;
    }
    // Observe the five detection dwells and the five probe dwells in calendar
    // order. Their pilots alone—not any payload sample—maintain the source
    // clock branch. A failed branch cannot be recovered using the probe.
    for(unsigned i=90;i<100;++i) {
        const auto pfirst=static_cast<std::size_t>(std::llround((coarse_time+.02+.4*i)*fs));
        constexpr std::size_t length=static_cast<std::size_t>(.36*fs);
        if(pfirst+length>iq.size())throw std::runtime_error("capture ends during Helper detection lead");
        history.push_back(receiver.observe(std::span(iq).subspan(pfirst,length),30e6,static_cast<double>(pfirst)/fs));
    }
    for(unsigned i=90;i<96;++i) {
        phase_observation=phase_clock.observe(history[i]);
        phase_valid=phase_valid && phase_observation.valid;
    }
    std::cout<<" sequential_phase_clock_valid="<<phase_valid
             <<" phase_clock_observations="<<phase_observation.observations;
    if(phase_valid)std::cout<<" phase_clock_radius_seconds="
        <<(phase_observation.upper_source_seconds-phase_observation.lower_source_seconds)/2;
    // Frequency correction remains useful even when the absolute phase/time
    // branch is unqualified. In that case the mixer phase is an explicitly
    // arbitrary gauge and no phase-sensitive metric is claimed.
    // The fixed 38s source lead, Helper-only time origin and measured audio
    // scale identify the probe observation interval. This is a protocol
    // schedule, not a tone-based synchronization or tone search.
    auto first=static_cast<std::size_t>(std::llround((coarse_time+38)*fs));
    const auto stop=static_cast<std::size_t>(std::llround((coarse_time+40)*fs));
    if(stop>iq.size()||stop<=first+fs)throw std::runtime_error("missing fixed-schedule probe samples");
    datapump::helper::HelperStreamConditioner::Config cc;
    cc.sample_rate=fs;cc.first_input_sample=first;
    cc.clock_time_at_first_input_seconds=phase_valid ?
        phase_observation.source_seconds-(phase_observation.input_seconds-static_cast<double>(first)/fs)*a :
        clock_at(static_cast<double>(first)/fs);
    double audio_at_first=a+audio_rate*(static_cast<double>(first)/fs-reference);
    // The production time-map witness compares the Helper source clock with
    // the absolute output index. Derive the first index and sub-sample offset
    // from the current Helper observation, never from the diagnostic probe.
    cc.first_output_sample=static_cast<std::uint64_t>(std::ceil(cc.clock_time_at_first_input_seconds*fs));
    cc.fractional_input_sample=static_cast<double>((
        static_cast<long double>(cc.first_output_sample)/fs-cc.clock_time_at_first_input_seconds)*fs/audio_at_first);
    if(cc.fractional_input_sample>=1) {
        // For a<1 the next output tick can lie beyond the next input sample.
        // Advance the raw start rather than passing an invalid fraction to the
        // resampler. The output tick and its Helper-only origin stay fixed.
        ++first;cc.first_input_sample=first;
        cc.clock_time_at_first_input_seconds=phase_valid ?
            phase_observation.source_seconds-(phase_observation.input_seconds-static_cast<double>(first)/fs)*a :
            clock_at(static_cast<double>(first)/fs);
        audio_at_first=a+audio_rate*(static_cast<double>(first)/fs-reference);
        cc.fractional_input_sample=static_cast<double>((
            static_cast<long double>(cc.first_output_sample)/fs-cc.clock_time_at_first_input_seconds)*fs/audio_at_first);
    }
    datapump::helper::HelperStreamConditioner conditioner(cc,{audio_at_first,b,.001});
    std::vector<std::complex<float>> corrected;std::vector<long double> dates;
    std::vector<bool> phase_authority,timing_authority;
    std::size_t timing_supported_samples=0;
    corrected.reserve(stop-first);dates.reserve(stop-first);
    std::array<std::complex<float>,256> output{};std::array<long double,256> produced_dates{};
    std::size_t cursor=first;unsigned phase_map_disagreements=0;
    const bool phase_initially_available=phase_valid;
    for(unsigned dwell=0;dwell<5;++dwell) {
        const auto dfirst=dwell?static_cast<std::size_t>(std::llround((coarse_time+38+.4*dwell)*fs)):first;
        const auto dend=std::min(stop,static_cast<std::size_t>(std::llround((coarse_time+38+.4*(dwell+1))*fs)));
        const auto& m=history[95+dwell];
        const auto pfirst=static_cast<std::size_t>(std::llround(m.first_sample_time_seconds*fs));
        const auto pend=static_cast<std::size_t>(std::llround(m.end_sample_time_seconds*fs));
        if(cursor!=dfirst||pend<=pfirst||pend>iq.size())throw std::runtime_error("invalid captured probe dwell geometry");
        if(dwell)conditioner.update({a+audio_rate*(static_cast<double>(dfirst)/fs-reference),b,.001});
        if(dwell) {
            phase_observation=phase_clock.observe(m);
            phase_valid=phase_valid && phase_observation.valid;
            if(!phase_valid)conditioner.clear_phase();
        }
        if(phase_valid) {
            // The resampler's already-published time map is continuous. A
            // later pilot phase may not retroactively rewrite that map; only
            // grant an anchor if the current map agrees with this independent
            // Helper-only source-time interval.
            const auto mapped=conditioner.clock_time_at_input(pfirst);
            if(mapped<phase_observation.lower_source_seconds ||
               mapped>phase_observation.upper_source_seconds) {
                ++phase_map_disagreements;phase_valid=false;conditioner.clear_phase();
            }
        }
        if(phase_valid) {
            // Anchor to the unchanged continuous conditioner time map, which
            // was just checked against the current Helper phase interval.
            // The phase-clock midpoint may differ slightly and must not shift
            // the output's independent time reference at a later dwell.
            const double phase=std::remainder(2*pi*2400*
                static_cast<double>(conditioner.clock_time_at_input(pfirst))-m.high_phase_radians,2*pi);
            const datapump::helper::HelperStreamConditioner::LocalPhase anchor{
                static_cast<long double>(pfirst),phase,pfirst,pend,pend};
            if(!conditioner.set_phase(anchor)){std::cout<<" phase_correction=unavailable\n";return;}
        }
        while(cursor<dend) {
            const auto p=conditioner.process(std::span(iq).subspan(cursor,dend-cursor),output,cursor,false,produced_dates);
            if(!p.consumed&&!p.produced)throw std::runtime_error("conditioner stalled");
            cursor+=p.consumed;
            corrected.insert(corrected.end(),output.begin(),output.begin()+static_cast<std::ptrdiff_t>(p.produced));
            dates.insert(dates.end(),produced_dates.begin(),produced_dates.begin()+static_cast<std::ptrdiff_t>(p.produced));
            for(std::size_t j=0;j<p.produced;++j) {
                const bool timing=conditioner.timing_supported_at(produced_dates[j],p.output_first_sample+j);
                timing_supported_samples+=timing;
                timing_authority.push_back(timing);
                phase_authority.push_back(timing && conditioner.phase_supported_at(produced_dates[j]));
            }
        }
    }
    if(phase_initially_available && !phase_valid) {
        // Some earlier samples were emitted under a pilot phase anchor. Once
        // that anchor is withdrawn, the remainder has an unrelated phase
        // gauge. Fitting one frequency through both would be misleading.
        std::cout<<" phase_correction=unavailable frequency_evaluation=unavailable"
                 <<" phase_map_disagreements="<<phase_map_disagreements<<"\n";
        return;
    }
    // Tone metrology is an independent evaluator; it never feeds an estimator
    // or changes time/phase correction. Demodulate against the known 1600 Hz
    // source convention, unwrap 50 ms phase differences, and regress slope.
    constexpr std::size_t measure=fs/20;
    std::vector<double> residual_phase,raw_phase,elapsed;
    const long double output_origin=static_cast<long double>(cc.first_output_sample)/fs;
    for(std::size_t i=0;i+measure<=corrected.size();i+=measure) {
        std::complex<double> sum{}, raw_sum{};unsigned n=0;
        for(std::size_t j=0;j<measure;++j) {
            const auto k=i+j;
            const auto source=dates[k];
            const double dwell=std::fmod(clock_at(static_cast<double>(source)/fs)-38+.4*100, .4);
            if(dwell<.04 || dwell>.36)continue;
            if(!timing_authority[k])continue;
            if(phase_valid && !phase_authority[k])continue;
            const double t=static_cast<double>(output_origin)+static_cast<double>(k)/fs;
            const auto reference=std::polar(1.,-2*pi*std::remainder(1600*t,1.));
            sum+=std::complex<double>(corrected[k])*reference;
            raw_sum+=std::complex<double>(iq[first+k])*reference;
            ++n;
        }
        if(n<measure/3 || std::abs(sum)<1e-10)continue;
        double phase=std::arg(sum);
        if(!residual_phase.empty())phase=residual_phase.back()+std::remainder(phase-residual_phase.back(),2*pi);
        double raw=std::arg(raw_sum);
        // Resolve phase wraps with the Helper-predicted carrier offset, not a
        // probe-derived frequency search. The actual tone is used only to
        // measure the departure after that common reference is established.
        if(!raw_phase.empty()) {
            const double t=static_cast<double>(i+measure/2)/fs;
            const double predicted_advance=2*pi*(b+(audio_at_first-1)*1600)*(t-elapsed.back());
            raw=raw_phase.back()+predicted_advance+
                std::remainder(raw-raw_phase.back()-predicted_advance,2*pi);
        }
        residual_phase.push_back(phase);raw_phase.push_back(raw);elapsed.push_back(static_cast<double>(i+measure/2)/fs);
    }
    if(residual_phase.size()<8)throw std::runtime_error("insufficient phase-supported probe output");
    double tm=0,pm=0;for(std::size_t i=0;i<elapsed.size();++i){tm+=elapsed[i];pm+=residual_phase[i];}
    tm/=elapsed.size();pm/=elapsed.size();double numerator=0,raw_numerator=0,denominator=0,phase_squared=0,raw_phase_squared=0;
    for(std::size_t i=0;i<elapsed.size();++i){numerator+=(elapsed[i]-tm)*(residual_phase[i]-pm);raw_numerator+=(elapsed[i]-tm)*raw_phase[i];denominator+=std::pow(elapsed[i]-tm,2);phase_squared+=std::pow(std::remainder(residual_phase[i],2*pi),2);raw_phase_squared+=std::pow(std::remainder(raw_phase[i],2*pi),2);}
    const double slope=numerator/denominator;double squared_fit_error=0;
    for(std::size_t i=0;i<elapsed.size();++i)
        squared_fit_error+=std::pow(residual_phase[i]-pm-slope*(elapsed[i]-tm),2);
    const double slope_sigma=std::sqrt(squared_fit_error/(static_cast<double>(elapsed.size())-2)/denominator)/(2*pi);
    std::cout<<" phase_correction="<<(phase_valid?"observed_same_dwell":"unavailable")
             <<" phase_map_disagreements="<<phase_map_disagreements
             <<" timing_supported_samples="<<timing_supported_samples
             <<" output_samples="<<corrected.size()
             <<" raw_tone_frequency_error_hz="<<raw_numerator/denominator/(2*pi);
    if(phase_valid)std::cout<<" raw_phase_rms_degrees="<<std::sqrt(raw_phase_squared/elapsed.size())*180/pi;
    std::cout
             <<" residual_tone_frequency_hz="<<numerator/denominator/(2*pi)
             <<" residual_frequency_fit_sigma_hz="<<slope_sigma;
    if(phase_valid)std::cout<<" residual_phase_rms_degrees="<<std::sqrt(phase_squared/elapsed.size())*180/pi;
    std::cout<<" evaluated_blocks="<<elapsed.size()<<"\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("usage: helper_live_cable --generate|--generate-long|--generate-long-negative|--analyze|--analyze-long path; --simulate-long stimulus capture [--preset flat|cable-20261030 --gain G --delay-ms D --highpass-hz H --noise-rms N --dc D --colored-noise-rms N --colored-noise-tau-ms T --seed S --adc-ppm P --quantization-bits B]");
        const std::string mode(argv[1]);
        if(mode=="--simulate-long") {
            if(argc<4)throw std::runtime_error("--simulate-long requires stimulus and new capture paths");
            SimulatedCable config;
            for(int i=4;i<argc;i+=2) {
                if(i+1>=argc)throw std::runtime_error("missing simulated cable option value");
                const std::string option(argv[i]),value(argv[i+1]);std::size_t end=0;
                if(option=="--preset") {
                    if(value=="flat")config=SimulatedCable{};
                    else if(value=="cable-20261030") {
                        config=SimulatedCable{};config.preset=value;config.gain=.84150;
                        config.delay_ms=35.0795;config.highpass_hz=11.34796;
                        config.measured_white_rms_including_quantization=.00004630874542665937;
                        const double quantization_step=1./32768;
                        config.noise_rms=std::sqrt(std::max(0.,
                            config.measured_white_rms_including_quantization*
                            config.measured_white_rms_including_quantization-
                            quantization_step*quantization_step/12));
                        config.noise_rms_basis="inferred_uniform_uncorrelated_s16_quantization";
                        config.dc=-.0000828714;config.colored_noise_rms=.000084666;
                    } else throw std::runtime_error("unknown simulated cable preset: "+value);
                    end=value.size();
                } else if(option=="--seed") {
                    if(value.empty()||value.front()=='-')throw std::runtime_error("invalid simulation seed");
                    config.seed=std::stoull(value,&end);
                } else if(option=="--quantization-bits") {
                    const auto n=std::stoul(value,&end);
                    if(n>24)throw std::runtime_error("invalid simulated quantization bits");
                    config.quantization_bits=static_cast<unsigned>(n);
                    config.measured_white_rms_including_quantization=0;
                    config.noise_rms_basis="configured_pre_quantization_override";
                } else {
                    const double number=std::stod(value,&end);
                    if(option=="--gain")config.gain=number;
                    else if(option=="--delay-ms")config.delay_ms=number;
                    else if(option=="--highpass-hz")config.highpass_hz=number;
                    else if(option=="--noise-rms") {
                        config.noise_rms=number;
                        config.measured_white_rms_including_quantization=0;
                        config.noise_rms_basis="configured_pre_quantization_override";
                    }
                    else if(option=="--dc")config.dc=number;
                    else if(option=="--colored-noise-rms")config.colored_noise_rms=number;
                    else if(option=="--colored-noise-tau-ms")config.colored_noise_tau_ms=number;
                    else if(option=="--adc-ppm")config.adc_ppm=number;
                    else throw std::runtime_error("unknown simulated cable option: "+option);
                }
                if(end!=value.size())throw std::runtime_error("invalid simulated cable option value");
            }
            simulate_long(argv[2],argv[3],config);
            return 0;
        }
        if(argc!=3)throw std::runtime_error("this cable mode requires exactly one path");
        if (mode == "--generate") generate(argv[2], false);
        else if (mode == "--generate-long") generate(argv[2], true);
        else if (mode == "--generate-long-negative") generate(argv[2], true, -100);
        else if (mode == "--analyze") analyze(argv[2]);
        else if (mode == "--analyze-long") analyze_long(argv[2]);
        else throw std::runtime_error("unknown mode");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "helper_live_cable: " << e.what() << '\n';
        return 1;
    }
}
