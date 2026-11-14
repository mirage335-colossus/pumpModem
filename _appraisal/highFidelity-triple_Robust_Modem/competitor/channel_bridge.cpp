// Native external-codec boundary. Codec algorithms remain in upstream programs;
// physical propagation is the same appraisal::Channel used by the Robust tests.
#include "appraisal/channel.hpp"
#include "datapump/modem.hpp"
#include "datapump/robust_audio_frontend.hpp"
#include "triad_probe_json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#ifndef APPRAISAL_SOURCE_SHA
#define APPRAISAL_SOURCE_SHA "unavailable"
#endif
#ifndef DATAPUMP_TRIAD_PROBE_SOURCE_SHA
#define DATAPUMP_TRIAD_PROBE_SOURCE_SHA "unavailable"
#define DATAPUMP_TRIAD_PROBE_BUILD_SHA "unavailable"
#define DATAPUMP_TRIAD_PROBE_COMPILER "unavailable"
#endif

namespace {
using appraisal::Complex;
using namespace datapump::triad::probe;
constexpr std::size_t limit_samples = 8'000'000;
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
struct Capture {
    std::uint32_t rate = 0;
    std::vector<Complex> iq;
    std::size_t raw_samples = 0, projection_halo = 0;
    bool c2 = false;
};
std::vector<float> floats(const std::filesystem::path& path) {
    const auto bytes = std::filesystem::file_size(path);
    check(bytes % sizeof(float) == 0 && bytes / sizeof(float) <= limit_samples, "invalid bounded float PCM file");
    std::vector<float> result(bytes / sizeof(float));
    std::ifstream in(path, std::ios::binary);
    check(static_cast<bool>(in.read(reinterpret_cast<char*>(result.data()), bytes)), "failed float PCM read");
    for (auto sample : result) check(std::isfinite(sample), "nonfinite float PCM input");
    return result;
}
Capture read(const std::filesystem::path& path, double carrier, double halfband) {
    Capture c;
    if (path.extension() == ".c2") {
        // Upstream WSJT-X c2: 14-byte name, int32 period minutes, float64 dial
        // frequency, 45000 interleaved binary32 I,-Q values at 375 samples/s.
        check(std::filesystem::file_size(path) == 26 + 45000 * 8, "unsupported c2 extent");
        std::ifstream in(path, std::ios::binary);
        std::array<char,14> name{};std::int32_t period = 0;double dial = 0;
        in.read(name.data(),name.size());in.read(reinterpret_cast<char*>(&period),4);in.read(reinterpret_cast<char*>(&dial),8);
        check(period == 2 && std::isfinite(dial), "only ordinary two-minute WSPR c2 is supported");
        c.c2 = true;c.rate = 375;c.iq.resize(45000);c.raw_samples = 45000;
        for (auto& value : c.iq) {
            std::array<float,2> pair{};in.read(reinterpret_cast<char*>(pair.data()),8);
            check(std::isfinite(pair[0]) && std::isfinite(pair[1]), "nonfinite c2 sample");
            value = {pair[0],-pair[1]};
        }
        check(static_cast<bool>(in), "truncated c2");
        return c;
    }
    std::vector<float> pcm;
    if (path.extension() == ".f32") { c.rate = 8000;pcm = floats(path); }
    else {
        std::ifstream input(path,std::ios::binary);
        const auto wav = datapump::modem::read_wav(input, limit_samples * sizeof(float));
        c.rate = wav.sample_rate;pcm = wav.samples;
    }
    check(c.rate == 8000 || c.rate == 12000, "comparator WAV supports native 8/12 kHz rates only");
    c.raw_samples = pcm.size();
    datapump::live::detail::AudioInput projection(c.rate,carrier,carrier-halfband,carrier+halfband);
    c.projection_halo = projection.halo_samples();
    // A source is known to be exactly zero outside its stored TX waveform.
    // Padding here is TX support, never synthesized receiver completion.
    std::vector<float> padded(c.projection_halo,0);
    padded.insert(padded.end(),pcm.begin(),pcm.end());
    padded.resize(padded.size()+c.projection_halo,0);
    std::vector<Complex> projected;
    const auto first = projection.project(padded,projected);
    check(first == c.projection_halo && projected.size() == pcm.size(), "native analytic projection coordinate mismatch");
    // AudioInput used padded coordinates; restore the original carrier origin.
    const auto phase = std::polar(1.,2*std::numbers::pi*carrier*c.projection_halo/c.rate);
    for (auto& x : projected) x *= phase;
    c.iq = std::move(projected);
    return c;
}
void c2_write(const std::filesystem::path& path, const std::vector<Complex>& iq, double scale, double rf_hz) {
    check(iq.size() == 45000, "c2 output requires exactly 120 seconds; physical tails must fit stored guard");
    std::ofstream out(path,std::ios::binary);
    const std::array<char,14> name{'0','0','0','0','0','0','_','0','0','0','1','.','c','2'};
    const std::int32_t period=2;const double dial=rf_hz/1e6-.0015;
    out.write(name.data(),name.size());out.write(reinterpret_cast<const char*>(&period),4);out.write(reinterpret_cast<const char*>(&dial),8);
    for (auto x : iq) {
        const std::array<float,2> pair{static_cast<float>(x.real()*scale),static_cast<float>(-x.imag()*scale)};
        out.write(reinterpret_cast<const char*>(pair.data()),8);
    }
    check(static_cast<bool>(out), "c2 output failed");
}
Json self_test() {
    constexpr std::uint32_t fs=12000;constexpr double cn0=20, carrier=1500;
    constexpr std::size_t n=120000;
    std::vector<float> pcm(n);
    for (std::size_t i=0;i<n;++i) pcm[i]=datapump::live::detail::real_pcm({1,0},i,carrier,fs);
    datapump::live::detail::AudioInput p(fs,carrier,1300,1700);
    std::vector<Complex> iq;p.project(pcm,iq);
    long double energy=0,error=0;for(auto x:iq) {energy+=std::norm(x);error+=std::norm(x-Complex{1,0});}
    const double gain=static_cast<double>(energy/iq.size()),mse=static_cast<double>(error/iq.size());
    check(std::abs(gain-1)<.025 && mse<.001,"native real/analytic tone normalization failed");
    const double sigma2=fs/(2*std::pow(10.,cn0/10));
    std::mt19937_64 rng(0x52f06a11);std::normal_distribution<double> normal(0,std::sqrt(sigma2));
    long double total=0;
    // Independent Fourier-bin estimator: real-noise one-sided density is
    // 2*E|DFT|^2/(N*Fs). Average 1000 disjoint 120-sample records at bin15.
    long double spectral=0;
    for(std::size_t block=0;block<1000;++block) {
        Complex transform{};
        for(std::size_t j=0;j<120;++j) {
            const double x=normal(rng);total+=x*x;
            transform+=x*std::polar(1.,-2*std::numbers::pi*15*j/120);
        }
        spectral+=2*std::norm(transform)/(120.*fs);
    }
    const double variance=static_cast<double>(total/n),density=static_cast<double>(spectral/1000);
    check(std::abs(variance/sigma2-1)<.025,"real AWGN variance convention failed");
    check(std::abs(density/std::pow(10.,-cn0/10)-1)<.12,"one-sided spectral density convention failed");
    return object({{"status","passed"},{"tone_analytic_power",gain},{"tone_complex_mse",mse},
        {"real_noise_variance",variance},{"expected_variance",sigma2},{"one_sided_noise_psd",density},
        {"expected_one_sided_noise_psd",std::pow(10.,-cn0/10)},{"fourier_records",1000}});
}
Json inspect_f32(const std::filesystem::path& path) {
    const auto samples=floats(path);
    constexpr unsigned n=1024;
    check(samples.size()>=n,"spectral inspection requires one complete native 8-kHz block");
    const auto blocks=samples.size()/n;
    std::array<double,513> powers{};
    for(unsigned k=100;k<=300;++k) {
        const auto step=std::polar(1.,-2*std::numbers::pi*k/n);
        for(std::size_t first=0;first+n<=samples.size();first+=n) {
            Complex sum{},z{1,0};
            for(unsigned j=0;j<n;++j) {
                sum+=static_cast<double>(samples[first+j])*(.5-.5*std::cos(2*std::numbers::pi*j/n))*z;
                z*=step;
            }
            powers[k]+=std::norm(sum);
        }
    }
    double total=0,moment=0,out140=0,out280=0;
    for(unsigned k=100;k<=300;++k) {
        const double delta=8000.*k/n-1500;
        total+=powers[k];moment+=powers[k]*delta*delta;
        if(std::abs(delta)>140)out140+=powers[k];
        if(std::abs(delta)>280)out280+=powers[k];
    }
    check(total>0,"empty spectral inspection");
    return object({{"status","completed"},{"sample_rate_hz",8000},{"assumed_center_hz",1500},
        {"method","independent native direct DFT, 1024-sample Hann blocks, no overlap; positive bins100..300"},
        {"frequency_low_hz",781.25},{"frequency_high_hz",2343.75},{"complete_blocks",blocks},
        {"rms_offset_hz",std::sqrt(moment/total)},
        {"power_fraction_outside_140_hz_within_reported_band",out140/total},
        {"power_fraction_outside_280_hz_within_reported_band",out280/total},
        {"appraisal_source_sha256",APPRAISAL_SOURCE_SHA}});
}
}

int main(int argc,char** argv) {
    std::cout<<std::setprecision(17);
    try {
        if(argc==2 && std::string(argv[1])=="--self-test") {write_json(std::cout,self_test());std::cout<<'\n';return 0;}
        if(argc==3 && std::string(argv[1])=="--inspect-f32") {write_json(std::cout,inspect_f32(argv[2]));std::cout<<'\n';return 0;}
        std::string input,output,raw_output,scenario="awgn",control="wanted";
        double cn0=30,carrier=1500,halfband=300,padding=0;std::uint64_t seed=12001;
        for(int i=1;i<argc;i+=2) {
            check(i+1<argc,"missing argument value");const std::string key=argv[i],value=argv[i+1];
            if(key=="--input")input=value;else if(key=="--output")output=value;else if(key=="--raw-output")raw_output=value;
            else if(key=="--scenario")scenario=value;else if(key=="--control")control=value;
            else if(key=="--cn0")cn0=std::stod(value);else if(key=="--carrier")carrier=std::stod(value);
            else if(key=="--halfband")halfband=std::stod(value);else if(key=="--seed")seed=std::stoull(value);
            else if(key=="--padding-seconds")padding=std::stod(value);
            else throw std::runtime_error("unknown option "+key);
        }
        check(!input.empty()&&!output.empty(),"--input and --output required");
        check(std::isfinite(cn0)&&cn0>=-80&&cn0<=120,"C/N0 outside finite bounded range");
        check(control=="wanted"||control=="noise","control must be wanted or noise");
        check(std::isfinite(padding)&&padding>=0&&padding<=60,"padding must be 0..60 physical seconds");
        const auto start=std::chrono::steady_clock::now();
        auto capture=read(input,carrier,halfband);
        auto spec=appraisal::make_scenario(scenario).channel;
        check(!spec.lunar&&!spec.transmitter.enabled,"comparator bridge supports linear HF/AWGN scenarios");
        for(const auto& path:spec.paths)
            check(!path.hf||!path.hf->measured_envelope,
                "measured HF scalar envelope has no qualified broadband external-codec transfer");
        check(spec.noise.colored_variance==0&&spec.noise.impulse_rate_hz==0&&spec.noise.impulse_rms==0&&spec.noise.tone_amplitude==0,
            "external comparator currently supports white receiver noise only");
        check(spec.frontend.lowpass_hz==0&&spec.frontend.agc_target==0&&spec.frontend.clip_magnitude==0&&spec.frontend.adc_bits==0,
            "external comparator has no calibrated nonlinear frontend");
        spec.noise.white_variance=0;
        std::size_t first=capture.iq.size(),stop=0;long double tx_energy=0;
        for(std::size_t i=0;i<capture.iq.size();++i) {
            tx_energy+=std::norm(capture.iq[i]);
            if(std::norm(capture.iq[i])>1e-16) {first=std::min(first,i);stop=i+1;}
        }
        check(stop>first&&tx_energy>0,"empty clean transmitter waveform");
        const double tx_power=static_cast<double>(tx_energy/(stop-first));
        const double tx_scale=1/std::sqrt(tx_power);
        for(auto& x:capture.iq)x*=tx_scale;
        // Normalize the fixed clean TX once. No faded output or receiver data
        // changes this scale. Noise controls retain this same source reference.
        appraisal::Source source;source.format.sample_rate=capture.rate;
        source.geometry.source_samples=stop;source.working_bytes=capture.iq.capacity()*sizeof(Complex);
        source.minimum_frequency_hz=-halfband;source.maximum_frequency_hz=halfband;
        source.sample_at=[&](std::uint64_t i){return control=="wanted"&&i<capture.iq.size()?capture.iq[i]:Complex{};};
        const double tail=appraisal::channel_tail_seconds(spec,static_cast<double>(capture.iq.size())/capture.rate)+
            appraisal::channel_interpolation_tail_seconds(spec,capture.rate);
        const auto horizon=std::max<std::uint64_t>(capture.raw_samples+static_cast<std::uint64_t>(std::ceil(padding*capture.rate)),
            stop+static_cast<std::uint64_t>(std::ceil(tail*capture.rate)));
        check(horizon<=limit_samples,"comparator sample limit exceeded");
        if(capture.c2)check(horizon==45000,"WSPR physical tail exceeds the 120-second capture");
        appraisal::Channel channel(source,spec,seed,horizon);
        std::vector<Complex> out(horizon);check(channel.read(out)==horizon,"short channel render");
        const double n0=std::pow(10.,-cn0/10);
        std::mt19937_64 rng(seed^0x9a7cd18463a102d5ULL);
        const double sigma=std::sqrt(capture.rate*n0/2);
        std::normal_distribution<double> normal(0,sigma);
        std::vector<float> pcm;long double noise_energy=0,signal_energy=0;double peak=0;
        if(capture.c2) {
            for(auto& x:out) {signal_energy+=std::norm(x);const Complex n{normal(rng),normal(rng)};noise_energy+=std::norm(n);x+=n;peak=std::max(peak,std::abs(x));}
            c2_write(output,out,1,spec.rf_hz); // IEEE float container has no PCM clipping.
        } else {
            pcm.resize(out.size());
            for(std::size_t i=0;i<out.size();++i) {
                const double signal=datapump::live::detail::real_pcm(out[i],i,carrier,capture.rate);
                const double noise=normal(rng);signal_energy+=signal*signal;noise_energy+=noise*noise;
                pcm[i]=static_cast<float>(signal+noise);peak=std::max(peak,std::abs(static_cast<double>(pcm[i])));
            }
            // Fixed gain from declared noise variance, not observed peak or
            // realization. Reject rather than silently clip an outlier.
            const double output_scale=1/(16*std::max(1.,sigma));
            check(peak*output_scale<.98,"fixed WAV headroom exceeded; no adaptive scaling or clipping permitted");
            for(auto& x:pcm)x=static_cast<float>(x*output_scale);
            std::ofstream file(output,std::ios::binary);datapump::modem::write_wav(file,pcm,capture.rate);
            if(!raw_output.empty()) {
                std::ofstream raw(raw_output,std::ios::binary);raw.write(reinterpret_cast<const char*>(pcm.data()),pcm.size()*sizeof(float));
                check(static_cast<bool>(raw),"raw PCM output failed");
            }
        }
        const auto& report=channel.report();
        auto result=object({{"status","completed"},{"scenario",scenario},{"control",control},{"seed",seed},
            {"appraisal_source_sha256",APPRAISAL_SOURCE_SHA},{"native_source_sha256",DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
            {"build_options_sha256",DATAPUMP_TRIAD_PROBE_BUILD_SHA},{"compiler",DATAPUMP_TRIAD_PROBE_COMPILER},
            {"rf_carrier_hz",spec.rf_hz},
            {"cn0_db_hz",cn0},{"snr_in_2500_hz_db",cn0-10*std::log10(2500.)},{"sample_rate_hz",capture.rate},
            {"input_samples",capture.raw_samples},{"output_samples",horizon},{"projection_halo_samples",capture.projection_halo},
            {"tx_active_first_sample",first},{"tx_active_stop_sample",stop},{"tx_active_seconds",static_cast<double>(stop-first)/capture.rate},
            {"tx_original_mean_power",tx_power},{"tx_voltage_scale",tx_scale},
            {"tx_energy_unit_power_seconds",static_cast<double>(tx_energy)*tx_scale*tx_scale/capture.rate},
            {"channel_wanted_energy_seconds",static_cast<double>(report.signal_energy)/capture.rate},
            {"output_signal_energy_seconds",static_cast<double>(signal_energy)/capture.rate},
            {"output_noise_energy_seconds",static_cast<double>(noise_energy)/capture.rate},
            {"noise_reference",capture.c2?"proper complex E|n|^2=Fs*N0; c2 stores I,-Q":"real white E[n^2]=Fs*N0/2; one-sided N0; SNR2500=C/N0-10log10(2500)"},
            {"output_voltage_scale",capture.c2?1.:1/(16*std::max(1.,sigma))},
            {"normalization","clean transmitter active interval only; never realized channel or noise power"},
            {"channel_tail_seconds",tail},{"channel_workspace_bytes",report.workspace_bytes},
            {"wall_seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()}});
        write_json(std::cout,result);std::cout<<'\n';return 0;
    } catch(const std::exception& e) {
        write_json(std::cout,object({{"status","error"},{"reason",e.what()}}));std::cout<<'\n';return 2;
    }
}
