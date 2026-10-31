#include "datapump/helper_measurement.hpp"
#include "datapump/helper_stream_conditioner.hpp"
#include "datapump/helper_startup_timing.hpp"
#include "datapump/helper_phase_clock.hpp"
#include "datapump/fast/modem.hpp"
#include "helper_live_probe_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace hp = helper_live_probe;
namespace h = datapump::helper;
using C = std::complex<double>;
using Sample = std::complex<float>;
constexpr double fs=hp::sample_rate, tau=hp::tau;
constexpr std::size_t settle=480, aperture=8640;

void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
double cpu() {const auto n=std::clock();require(n!=static_cast<std::clock_t>(-1),"CPU clock unavailable");
    return static_cast<double>(n)/CLOCKS_PER_SEC;}
double wrap(double phase) {return std::remainder(phase,tau);}

// Every output carries its absolute source-clock sample number. This is the
// fixed, specified experiment convention: neither probe symbols nor probe
// samples are searched to pick a timing, phase, gain, frequency or delay.
struct Trace {
    std::uint64_t first=0;
    std::vector<Sample> noisy,clean;
    std::vector<unsigned char> phase_supported;
    bool started=false;
    void append(std::uint64_t at,std::span<const Sample> x,std::span<const Sample> y,
                std::span<const unsigned char> supported) {
        require(x.size()==y.size() && x.size()==supported.size(),"different paired output counts");
        if(!started){first=at;started=true;}
        require(at==first+noisy.size(),"output dates not contiguous");
        noisy.insert(noisy.end(),x.begin(),x.end());clean.insert(clean.end(),y.begin(),y.end());
        phase_supported.insert(phase_supported.end(),supported.begin(),supported.end());
    }
    bool contains(std::uint64_t absolute)const{return started && absolute>=first && absolute-first<noisy.size();}
    C at(std::uint64_t absolute,bool clean_only) const {
        require(contains(absolute),"sample outside output trace");
        return C((clean_only?clean:noisy)[static_cast<std::size_t>(absolute-first)]);
    }
    bool supported(std::uint64_t absolute)const {
        return contains(absolute) && phase_supported[static_cast<std::size_t>(absolute-first)];
    }
};

struct ToneMetrics {
    double freq_mean=0,freq_rms=0,phase_mean=0,phase_rms=0,noisy_freq_mean=0,noisy_freq_rms=0;
    double noisy_freq_standard_error=0,noisy_phase_rms=0,phase_max=0;
    unsigned windows=0,noisy_windows=0;
};
ToneMetrics tone_metrics(const Trace& t,bool require_phase,bool noisy_metrology,
                         double expected_constant_phase=0) {
    ToneMetrics m;
    constexpr std::uint64_t lag=128;
    // Each comparison is within one retune and away from the retune ramps.
    // A clean, identically conditioned probe twin is used solely to expose
    // correction error without fitting or feeding back probe information.
    for(unsigned d=95;d<100;++d) {
        const auto lo=static_cast<std::uint64_t>((d*.4+.08)*fs);
        const auto hi=static_cast<std::uint64_t>((d*.4+.32)*fs);
        if(!t.contains(lo) || !t.contains(hi-1))continue;
        C lag_sum{},phase_sum{};unsigned count=0;
        for(auto n=lo;n+lag<hi;++n) {
            if(require_phase && (!t.supported(n)||!t.supported(n+lag)))continue;
            const auto z=t.at(n,true),next=t.at(n+lag,true);
            lag_sum+=next*std::conj(z)*std::polar(1.,-tau*hp::probe_frequency_hz*lag/fs);
            phase_sum+=z*std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*n/fs,1.)-expected_constant_phase);++count;
        }
        if(count<1000 || std::abs(lag_sum)==0 || std::abs(phase_sum)==0)continue;
        const double frequency=std::arg(lag_sum)*fs/(tau*lag);
        const double phase=std::arg(phase_sum);
        m.phase_max=std::max(m.phase_max,std::abs(phase));
        // Independent noisy-tone metrology: 10 ms fixed blocks, unwrap their
        // measured phase and fit its slope. It produces a measurement and an
        // internal residual-based standard error, never a correction input.
        constexpr unsigned blocks=24;constexpr std::uint64_t block_samples=240;
        std::array<double,blocks> angles{};double phase_rms=0;bool observed=true;
        for(unsigned b=0;b<blocks;++b) {
            C block{};
            for(std::uint64_t j=0;j<block_samples;++j) {
                const auto n=lo+b*block_samples+j;
                if(require_phase && !t.supported(n)){observed=false;break;}
                block+=t.at(n,false)*std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*n/fs,1.)-expected_constant_phase);
            }
            if(!observed || std::abs(block)==0){observed=false;break;}
            const double raw=std::arg(block);phase_rms+=raw*raw;
            angles[b]=b?angles[b-1]+wrap(raw-angles[b-1]):raw;
        }
        m.freq_mean+=frequency;m.freq_rms+=frequency*frequency;m.phase_mean+=phase;m.phase_rms+=phase*phase;
        ++m.windows;
        if(noisy_metrology && observed) {
            double mean=0;for(double x:angles)mean+=x/blocks;
            const double center=(blocks-1)*.005;double ss=0,slope=0;
            for(unsigned b=0;b<blocks;++b){const double dt=b*.01-center;ss+=dt*dt;slope+=dt*(angles[b]-mean);}
            slope/=ss;double residual=0;
            for(unsigned b=0;b<blocks;++b){const double dt=b*.01-center;
                residual+=std::pow(angles[b]-(mean+slope*dt),2);}
            const double std_error=std::sqrt(residual/(blocks-2)/ss)/tau;
            m.noisy_freq_mean+=slope/tau;m.noisy_freq_rms+=std::pow(slope/tau,2);
            m.noisy_freq_standard_error+=std_error*std_error;m.noisy_phase_rms+=phase_rms/blocks;
            ++m.noisy_windows;
        }
    }
    if(m.windows){m.freq_mean/=m.windows;m.phase_mean/=m.windows;
        m.freq_rms=std::sqrt(m.freq_rms/m.windows);m.phase_rms=std::sqrt(m.phase_rms/m.windows);
    }
    if(m.noisy_windows){m.noisy_freq_mean/=m.noisy_windows;
        m.noisy_freq_rms=std::sqrt(m.noisy_freq_rms/m.noisy_windows);
        m.noisy_freq_standard_error=std::sqrt(m.noisy_freq_standard_error/m.noisy_windows);
        m.noisy_phase_rms=std::sqrt(m.noisy_phase_rms/m.noisy_windows);}
    return m;
}

struct EvmMetrics {double noisy=0,clean=0;unsigned symbols=0;};
EvmMetrics evm_metrics(const Trace& t,const hp::Source& source,bool require_phase,
                        double fixed_time_gauge=0,double fixed_phase_gauge=0) {
    // 300 sym/s at 24 kHz gives 80 samples/symbol. A fixed 16-symbol RRC
    // matched filter is applied at known symbol dates plus one predeclared
    // fixed evaluation gauge. The gauge is never estimated from the probe.
    // Baseband linear interpolation handles the fractional output sample;
    // neither it nor the fixed carrier rotation is a tracking operation.
    require(std::isfinite(fixed_time_gauge) && std::isfinite(fixed_phase_gauge),
            "nonfinite fixed APSK evaluation gauge");
    constexpr int span=640,sps=80;
    std::array<double,2*span+1> taps{};double energy=0;
    for(int i=-span;i<=span;++i){const double v=datapump::fast::root_raised_cosine(static_cast<double>(i)/sps,.2);
        taps[static_cast<std::size_t>(i+span)]=v;energy+=v*v;}
    const double pulse_norm=std::sqrt(energy/sps);
    const double scale=pulse_norm/energy;
    EvmMetrics result;double expected_power=0;
    for(unsigned k=8;k<592;++k) {
        const double center=(hp::probe_start_seconds+fixed_time_gauge)*fs+k*sps;
        if(center<span || center+span+1>=static_cast<double>(std::numeric_limits<std::uint64_t>::max()))continue;
        const auto first=static_cast<std::uint64_t>(std::floor(center-span));
        const auto last=static_cast<std::uint64_t>(std::floor(center+span));
        const double fraction=center-std::floor(center);
        if(!t.contains(first) || !t.contains(last) || (fraction>0 && !t.contains(last+1)))continue;
        // Exclude any matched filter crossing a hop/unsupported interval.
        const double phase_in_dwell=std::fmod(center/fs-fixed_time_gauge,.4);
        if(phase_in_dwell<.08 || phase_in_dwell>.32)continue;
        C yn{},yc{};bool valid=true;
        for(int j=-span;j<=span;++j) {
            const auto n=static_cast<std::uint64_t>(static_cast<std::int64_t>(std::floor(center))+j);
            if(require_phase && (!t.supported(n) || (fraction>0 && !t.supported(n+1)))){valid=false;break;}
            const auto mix=std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*n/fs,1.)-fixed_phase_gauge);
            C noisy=t.at(n,false)*mix,clean=t.at(n,true)*mix;
            if(fraction>0) {
                const auto next_mix=std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*(n+1)/fs,1.)-fixed_phase_gauge);
                noisy=(1-fraction)*noisy+fraction*t.at(n+1,false)*next_mix;
                clean=(1-fraction)*clean+fraction*t.at(n+1,true)*next_mix;
            }
            const auto weight=taps[static_cast<std::size_t>(j+span)];
            yn+=noisy*weight;yc+=clean*weight;
        }
        if(!valid)continue;
        yn*=scale;yc*=scale;
        const C expected=source.probe_amplitude()*source.symbol(k);
        expected_power+=std::norm(expected);result.noisy+=std::norm(yn-expected);result.clean+=std::norm(yc-expected);
        ++result.symbols;
    }
    if(expected_power>0){result.noisy=std::sqrt(result.noisy/expected_power);result.clean=std::sqrt(result.clean/expected_power);}
    return result;
}

struct StreamPair {
    std::unique_ptr<h::HelperStreamConditioner> mixed,clean;
    Trace output;
    double receiver_cpu=0;
    explicit StreamPair(h::HelperStreamConditioner::Config config,
                        h::HelperStreamConditioner::Correction correction) {
        const auto begin=cpu();
        mixed=std::make_unique<h::HelperStreamConditioner>(config,correction);
        receiver_cpu+=cpu()-begin;
        clean=std::make_unique<h::HelperStreamConditioner>(config,correction);
    }
    void update(h::HelperStreamConditioner::Correction c,bool timed) {
        const auto begin=cpu();mixed->update(c);if(timed)receiver_cpu+=cpu()-begin;clean->update(c);
    }
    bool phase(const h::HelperStreamConditioner::LocalPhase& p,bool timed) {
        const auto begin=cpu();const bool a=mixed->set_phase(p);if(timed)receiver_cpu+=cpu()-begin;
        const bool b=clean->set_phase(p);require(a==b,"paired phase acceptance differed");return a;
    }
    void feed(const hp::Dwell& d,bool timed) {
        constexpr std::array<std::size_t,6> sizes{31,251,73,192,17,223};
        const auto first=static_cast<std::uint64_t>(std::llround(d.first_seconds*fs));
        require(mixed->next_input_sample()==clean->next_input_sample() && mixed->next_input_sample()>=first &&
                mixed->next_input_sample()<=first+d.samples.size(),"stream input origin outside captured dwell");
        std::size_t position=static_cast<std::size_t>(mixed->next_input_sample()-first),chunk=0;
        while(position<d.samples.size()) {
            const auto end=std::min(d.samples.size(),position+sizes[chunk++%sizes.size()]);
            while(position<end) {
                std::array<Sample,256> x{},y{};std::array<long double,256> dx{},dy{};
                const auto in=end-position;
                const auto begin=cpu();
                const auto p=mixed->process(std::span<const Sample>(d.samples.data()+position,in),x,first+position,false,dx);
                if(timed)receiver_cpu+=cpu()-begin;
                const auto q=clean->process(std::span<const Sample>(d.clean_probe.data()+position,in),y,first+position,false,dy);
                require(p.consumed==q.consumed && p.produced==q.produced && p.output_first_sample==q.output_first_sample,
                        "complex clean and noisy output clocks differ");
                require(p.consumed || p.produced,"stream conditioner stalled");
                std::array<unsigned char,256> supported{};
                const auto support_started=cpu();
                for(std::size_t j=0;j<p.produced;++j) {
                    require(std::abs(dx[j]-dy[j])<1e-8L,"paired output source dates differ");
                    supported[j]=static_cast<unsigned char>(mixed->phase_supported_at(dx[j]) &&
                        mixed->timing_supported_at(dx[j],p.output_first_sample+j));
                }
                if(timed)receiver_cpu+=cpu()-support_started;
                if(p.produced)output.append(p.output_first_sample,std::span<const Sample>(x.data(),p.produced),
                    std::span<const Sample>(y.data(),q.produced),std::span<const unsigned char>(supported.data(),p.produced));
                position+=p.consumed;
            }
        }
    }
};

struct Coeff {double audio=1,shift_ppm=0;};
Coeff project(const h::HelperMeasurementSnapshot& s,double date) {
    const double dt=date-s.time_seconds;
    return {1+(s.audio_ppm+dt*s.audio_drift_ppm_per_second)*1e-6,
            s.shift_ppm+dt*s.shift_drift_ppm_per_second};
}

h::HelperStreamConditioner::Config stream_config(std::uint64_t input,double audio,long double clock_at_input) {
    const long double nominal_output=std::ceil(clock_at_input*fs);
    require(nominal_output>=0 && nominal_output<static_cast<long double>(UINT64_MAX),"invalid source time");
    double fraction=static_cast<double>((nominal_output-clock_at_input*fs)/audio);
    // At a slightly sub-unity clock ratio, the next integer TX output may lie
    // just beyond the next raw sample. Advance only the input origin; retain
    // the same target output date and its continuously integrated clock.
    if(fraction>=1) {const auto advance=static_cast<std::uint64_t>(std::floor(fraction));
        input+=advance;clock_at_input+=audio*advance/fs;fraction-=static_cast<double>(advance);}
    require(fraction>=0 && fraction<1,"invalid helper-derived fractional source alignment");
    return {hp::sample_rate,input,static_cast<std::uint64_t>(nominal_output),clock_at_input,fraction};
}

bool set_helper_phase(StreamPair& stream,const h::HelperDwellMeasurement& m,
                      const h::HelperPhaseClock::Observation& witness,std::uint64_t first,bool timed) {
    const auto begin=cpu();
    if(!m.valid || !witness.valid || !std::isfinite(witness.input_seconds) ||
       std::abs(witness.input_seconds-m.first_sample_time_seconds)>1e-9) {
        stream.mixed->clear_phase();if(timed)stream.receiver_cpu+=cpu()-begin;
        stream.clean->clear_phase();return false;
    }
    const auto anchor=static_cast<long double>(first+settle);
    const auto clock=stream.mixed->clock_time_at_input(anchor);
    // Do not re-anchor or authorize old output when the continuously advanced
    // sample clock has left the current Helper-only phase-clock interval.
    if(clock<witness.lower_source_seconds || clock>witness.upper_source_seconds) {
        stream.mixed->clear_phase();if(timed)stream.receiver_cpu+=cpu()-begin;
        stream.clean->clear_phase();return false;
    }
    const double phase=wrap(static_cast<double>(tau*hp::high_frequency_hz*clock)-m.high_phase_radians);
    const h::HelperStreamConditioner::LocalPhase local{anchor,phase,first+settle,first+settle+aperture,first+hp::dwell_samples};
    if(timed)stream.receiver_cpu+=cpu()-begin;
    require(stream.phase(local,timed),"valid same-hop Helper phase rejected");return true;
}

void clock_conflict_control() {
    StreamPair stream({hp::sample_rate,0,0,0,0},{1,0,.001});
    h::HelperDwellMeasurement m;m.valid=true;m.first_sample_time_seconds=.02;
    m.end_sample_time_seconds=.38;m.high_phase_radians=0;
    h::HelperPhaseClock::Observation witness;
    witness.valid=true;witness.input_seconds=.02;witness.source_seconds=.0305;
    witness.lower_source_seconds=.030;witness.upper_source_seconds=.031;
    require(!set_helper_phase(stream,m,witness,0,false),"conflicting source clocks falsely qualified phase");
    require(!stream.mixed->phase_supported_at(settle),"conflicting source clock retained phase authority");
    witness.input_seconds=.021;witness.lower_source_seconds=.019;witness.upper_source_seconds=.021;
    require(!set_helper_phase(stream,m,witness,0,false),"mismatched phase-clock date falsely qualified phase");
}

void set_ideal_phase(StreamPair& stream,const hp::Dwell& d,std::uint64_t first) {
    // This is an explicitly simulator-truth oracle for evaluation only. It is
    // not an estimate from the probe and never feeds the Helper receiver.
    const auto anchor=static_cast<long double>(first+settle);
    const double phase=-std::arg(C(d.common_phasor[settle]));
    const h::HelperStreamConditioner::LocalPhase local{anchor,phase,first+settle,first+settle+aperture,first+hp::dwell_samples};
    require(stream.phase(local,false),"simulator reference phase rejected");
}

void late_phase_authority_control() {
    h::HelperStreamConditioner::Config config{hp::sample_rate,0,0,0,0};
    h::HelperStreamConditioner receiver(config,{1,0,.001});
    std::array<Sample,256> input{},out{};
    auto progress=receiver.process(std::span<const Sample>(input.data(),200),out,0);
    require(progress.consumed>0,"late-phase control did not advance input");
    const auto applied=receiver.next_input_sample();
    const h::HelperStreamConditioner::LocalPhase p{0,0,0,1000,1000};
    require(receiver.set_phase(p),"late phase scheduling failed");
    (void)receiver.process(input,out,applied);
    require(!receiver.phase_supported_at(applied),"late phase incorrectly authorized historical filter support");
    const auto before_replacement=receiver.next_input_sample();
    const h::HelperStreamConditioner::LocalPhase q{0,.2,0,1000,1000};
    require(receiver.set_phase(q),"replacement phase scheduling failed");
    (void)receiver.process(input,out,before_replacement);
    require(!receiver.phase_supported_at(before_replacement),"replacement phase incorrectly authorized past samples");
}

// Changing caller chunk sizes and output capacity must not change the
// committed mixer state when the resampler accepts only a prefix. Fresh hop
// anchors and a mid-hop replacement are deliberate; no phase continuity is
// assumed. All dates come from the production resampler, not a tone fit.
void mixer_chunk_invariance_control() {
    struct Collected {std::vector<Sample> value;std::vector<long double> date;std::vector<unsigned char> support;};
    const auto run=[](bool tiny_output,bool recursive) {
        h::HelperStreamConditioner::Config config{hp::sample_rate,0,0,0,0};
        config.use_recursive_oscillator=recursive;
        h::HelperStreamConditioner receiver(config,{1,0,.001});
        std::vector<Sample> input(8*hp::dwell_samples);std::array<Sample,256> output{};std::array<long double,256> dates{};
        std::mt19937 rng(816);std::uniform_real_distribution<float> random(-1,1);
        for(auto& x:input)x={random(rng),random(rng)};
        constexpr std::array<double,8> offsets{0,.0001,24,-24,11900,-11900,127,-127};
        Collected out;std::uint64_t position=0;
        for(unsigned hop=0;hop<offsets.size();++hop){
            const auto first=static_cast<std::uint64_t>(hop)*hp::dwell_samples;
            receiver.update({1.+(static_cast<double>(hop)-3)*1e-6,offsets[hop],.001});
            const h::HelperStreamConditioner::LocalPhase phase{static_cast<long double>(first+4800),.17+hop*.37,
                first+480,first+hp::dwell_samples-480,first+hp::dwell_samples};
            require(receiver.set_phase(phase),"mixer chunk control phase rejected");
            for(unsigned half=0;half<2;++half){
                if(half){const auto middle=receiver.next_input_sample();
                    const h::HelperStreamConditioner::LocalPhase replace{static_cast<long double>(middle+200),-.3+hop*.21,
                        middle,middle+2000,first+hp::dwell_samples};
                    require(receiver.set_phase(replace),"mixer chunk control replacement rejected");}
                const auto end=first+static_cast<std::uint64_t>(half+1)*(hp::dwell_samples/2);
                while(position<end){
                    const auto n=std::min<std::size_t>(tiny_output?173:251,static_cast<std::size_t>(end-position));
                    const auto capacity=tiny_output?std::size_t{13}:output.size();
                    const auto p=receiver.process(std::span<const Sample>(input.data()+position,n),std::span<Sample>(output.data(),capacity),position,false,dates);
                    require(p.consumed||p.produced,"mixer chunk control stalled");
                    for(std::size_t i=0;i<p.produced;++i){out.value.push_back(output[i]);out.date.push_back(dates[i]);
                        out.support.push_back(static_cast<unsigned char>(receiver.phase_supported_at(dates[i])&&
                            receiver.timing_supported_at(dates[i],p.output_first_sample+i)));}
                    position+=p.consumed;
                }
            }
        }
        return out;
    };
    const auto a=run(false,true),b=run(true,true),old=run(false,false),old_tiny=run(true,false);
    require(a.value.size()==b.value.size()&&a.value.size()==old.value.size()&&a.value.size()==old_tiny.value.size(),
        "mixer chunk output count changed");
    double chunk_worst=0,scalar_worst=0;for(std::size_t i=0;i<a.value.size();++i){
        chunk_worst=std::max(chunk_worst,std::abs(C(a.value[i])-C(b.value[i])));
        scalar_worst=std::max(scalar_worst,std::abs(C(a.value[i])-C(old.value[i])));
        require(std::abs(a.date[i]-b.date[i])<1e-12L&&std::abs(a.date[i]-old.date[i])<1e-12L&&
            std::abs(a.date[i]-old_tiny.date[i])<1e-12L,"mixer source dates changed");
        require(a.support[i]==b.support[i]&&a.support[i]==old.support[i]&&a.support[i]==old_tiny.support[i],
            "mixer support changed");
        require(std::abs(C(old.value[i])-C(old_tiny.value[i]))<1e-6,"scalar mixer depends on chunk size");
    }
    require(chunk_worst<1e-6,"mixer phase depends on partial consumption/chunk size");
    require(scalar_worst<1e-6,"recursive mixer differs excessively from scalar reference");
    std::cout<<"helper_mixer_chunk samples="<<a.value.size()<<" max_chunk_error="<<chunk_worst
             <<" max_scalar_error="<<scalar_worst<<'\n';
}

// Full-dwell best effort installs each observed hop phase at the start of its
// captured block. A missing fresh phase must not reuse the prior random hop's
// multiplier; its samples still flow, explicitly without phase authority.
void full_dwell_missing_phase_control() {
    struct Collected {std::vector<Sample> value;std::vector<long double> date;std::vector<unsigned char> authority;};
    const auto run=[](bool recursive,std::size_t cap){
        h::HelperStreamConditioner::Config cfg{hp::sample_rate,0,0,0,0};cfg.use_recursive_oscillator=recursive;
        h::HelperStreamConditioner receiver(cfg,{1,0,.001});Collected result;
        std::array<Sample,256> input{},out{};std::array<long double,256> date{};
        input.fill(Sample{1,0});
        constexpr std::size_t block=2400;constexpr std::array<double,4> phase{1.1,0,-.7,0};
        std::uint64_t position=0;
        for(std::size_t hop=0;hop<phase.size();++hop){
            const auto first=static_cast<std::uint64_t>(hop*block),end=first+block;
            receiver.clear_phase_and_multiplier();
            if(hop%2==0){const h::HelperStreamConditioner::LocalPhase p{static_cast<long double>(first+block/2),phase[hop],first,end,end};
                require(receiver.set_phase(p),"full-dwell fresh phase rejected");}
            while(position<end){const auto n=std::min<std::size_t>(173,static_cast<std::size_t>(end-position));
                const auto p=receiver.process(std::span<const Sample>(input.data(),n),std::span<Sample>(out.data(),cap),position,false,date);
                require(p.consumed||p.produced,"full-dwell missing-phase control stalled");
                for(std::size_t i=0;i<p.produced;++i){result.value.push_back(out[i]);result.date.push_back(date[i]);
                    result.authority.push_back(static_cast<unsigned char>(receiver.phase_supported_at(date[i])));}
                position+=p.consumed;
            }
        }
        return result;
    };
    const auto a=run(true,13),b=run(true,256),c=run(false,13);
    require(a.value.size()==b.value.size()&&a.value.size()==c.value.size(),"full-dwell phase output count changed");
    std::array<unsigned,4> checked{};
    for(std::size_t i=0;i<a.value.size();++i){
        require(a.date[i]==b.date[i]&&a.date[i]==c.date[i],"full-dwell phase source dates changed");
        require(std::abs(C(a.value[i])-C(b.value[i]))<1e-6&&std::abs(C(a.value[i])-C(c.value[i]))<1e-6,
            "full-dwell phase depends on chunking or mixer mode");
        const auto hop=static_cast<std::size_t>(a.date[i]/2400);
        if(hop>=4||a.date[i]-hop*2400<300||a.date[i]-hop*2400>=2100)continue;
        constexpr std::array<double,4> phase{1.1,0,-.7,0};
        require(std::abs(C(a.value[i])-std::polar(1.,phase[hop]))<1e-4,"fresh or missing-hop multiplier incorrect");
        require(static_cast<bool>(a.authority[i])==(hop%2==0),"missing hop acquired phase authority");
        ++checked[hop];
    }
    for(auto n:checked)require(n>1000,"full-dwell phase control did not cover a hop");
    std::cout<<"helper_full_dwell_phase_control samples="<<a.value.size()<<" unknown_hops=2\n";
}

void rate_slew_control(double next_audio,bool expect_large_error) {
    // Drive a real paired production resampler across a commanded clock step.
    // These are explicit component-control inputs, never measured from probe.
    h::HelperStreamConditioner receiver({hp::sample_rate,0,0,0,0},{1,0,.001});
    std::array<Sample,256> input{},out{};std::array<long double,256> dates{};
    std::uint64_t first=0,old_output=0;long double old_source=0,old_error=0;bool saved=false;
    const auto consume=[&](std::size_t count,bool after,double& maximum,unsigned& unsupported,unsigned& supported) {
        std::size_t position=0;
        while(position<count) {
            const auto n=std::min<std::size_t>(input.size(),count-position);
            const auto p=receiver.process(std::span<const Sample>(input.data(),n),out,first,false,dates);
            require(p.consumed || p.produced,"rate-slew conditioner stalled");
            require(!p.correction_supported || (p.phase_supported && p.timing_supported),"full correction bypassed source-time support");
            for(std::size_t i=0;i<p.produced;++i) {
                const auto index=p.output_first_sample+i;
                const auto error=receiver.timing_error_at(dates[i],index);
                if(!saved && !after && error && index>100) {
                    old_output=index;old_source=dates[i];old_error=*error;saved=true;
                }
                if(after && error) {
                    maximum=std::max(maximum,std::abs(static_cast<double>(*error)));
                    if(std::abs(*error)>50e-6L) {
                        require(!receiver.timing_supported_at(dates[i],index),"large slew discrepancy falsely qualified time");
                        require(!p.correction_supported,"large slew discrepancy falsely qualified full correction");
                        ++unsupported;
                    }
                    if(receiver.phase_supported_at(dates[i]) && receiver.timing_supported_at(dates[i],index))++supported;
                }
            }
            position+=p.consumed;first+=p.consumed;
        }
    };
    double maximum=0;unsigned unsupported=0,supported=0;
    consume(48000,false,maximum,unsupported,supported);
    require(saved,"rate-slew control did not retain an old source date");
    receiver.update({next_audio,0,.001});
    const auto historical=receiver.timing_error_at(old_source,old_output);
    require(!historical || std::abs(*historical-old_error)<1e-12L,"rate update rewrote past time map");
    const auto start=receiver.next_input_sample();
    const h::HelperStreamConditioner::LocalPhase p{static_cast<long double>(start),0,start,start+24000,start+24000};
    require(receiver.set_phase(p),"rate-slew local phase failed");
    consume(24000,true,maximum,unsupported,supported);
    if(expect_large_error)require(maximum>50e-6 && unsupported>0,"large in-range rate step did not expose time-map mismatch");
    else require(supported>0 && maximum<1e-8,"small rate step failed normal time-map support");
    std::cout<<"helper_rate_slew next_audio="<<next_audio<<" max_time_error_us="<<maximum*1e6
        <<" unsupported_over_50us="<<unsupported<<" qualified_samples="<<supported<<'\n';
}

void phase_clock_controls() {
    h::HelperStartupTimingResult onset;onset.valid=true;onset.onset_seconds=.003197;
    onset.support_radius_seconds=.000020;
    h::HelperPhaseClock clock({});require(clock.seed(onset),"phase clock did not accept qualified startup");
    std::mt19937_64 rng(628);std::uniform_real_distribution<double> uniform(-std::numbers::pi,std::numbers::pi);
    const auto sample=[&](unsigned i) {
        h::HelperDwellMeasurement m;m.valid=true;m.first_sample_time_seconds=i*.4+.02;
        m.end_sample_time_seconds=m.first_sample_time_seconds+.36;
        const double source=1.00005*(m.first_sample_time_seconds-onset.onset_seconds),common=uniform(rng);
        m.high_phase_radians=wrap(tau*2400*source+common);m.low_phase_radians=wrap(tau*550*source+common);
        m.high_cn0_db_hz=80;m.low_cn0_db_hz=77;return m;
    };
    for(unsigned i=0;i<6;++i) {
        auto m=sample(i);if(i==4)m.valid=false;
        const auto estimate=clock.observe(m);
        if(i==4){require(!estimate.valid,"missing pilot generated a phase clock");continue;}
        require(estimate.valid,"random common hop phase broke phase-clock tracking");
        const double truth=1.00005*(m.first_sample_time_seconds-onset.onset_seconds);
        require(truth>=estimate.lower_source_seconds && truth<=estimate.upper_source_seconds,
                "Helper phase-clock interval missed modeled source time");
    }
    auto gap=sample(6);gap.valid=false;(void)clock.observe(gap);
    gap=sample(7);gap.valid=false;(void)clock.observe(gap);
    require(!clock.observe(sample(8)).valid,"long Helper phase gap falsely preserved an unambiguous clock");
    h::HelperPhaseClock weak({});require(weak.seed(onset),"weak clock seed failed");
    auto ambiguous=sample(0);ambiguous.high_cn0_db_hz=5;ambiguous.low_cn0_db_hz=2;
    require(!weak.observe(ambiguous).valid,"unreliable pilot phase falsely resolved timing branch");
    h::HelperPhaseClock::Config bounded_config;
    bounded_config.differential_phase_half_width_radians=.10;
    h::HelperPhaseClock bounded(bounded_config);require(bounded.seed(onset),"bounded differential phase seed failed");
    for(unsigned i=0;i<4;++i) {
        auto m=sample(i);m.low_phase_radians=wrap(m.low_phase_radians+.05);
        const auto estimate=bounded.observe(m);
        require(estimate.valid,"within-bound differential phase was declined");
        const double truth=1.00005*(m.first_sample_time_seconds-onset.onset_seconds);
        require(truth>=estimate.lower_source_seconds && truth<=estimate.upper_source_seconds,
                "declared differential phase allowance did not cover modeled source time");
    }
}

void startup_channel_consistency_control() {
    // Actual startup ramps with independently imposed per-pilot channel phases.
    // Exact audio/frequency inputs here are explicit component-test premises,
    // not inputs to the end-to-end Helper-only correction path below.
    h::Options opts;opts.power_w=4000;opts.data_power_w=1;opts.callsign="N0CALL";
    const h::Plan hi_plan{2400,.4,0,4000,0,0,true};
    const h::Plan hl_plan{2400,.4,0,4000,0,550,true};
    const h::Plan all_plan{2400,.4,0,4000,950,550,true};
    h::Generator hi(opts,hi_plan,1800000000,true),hl(opts,hl_plan,1800000000,true),
                 all(opts,all_plan,1800000000,true);
    constexpr double onset=.003197,a=1.00005,b=24;
    h::HelperStartupTimingConfig config;
    config.sample_rate=fs;config.first_sample_seconds=0;config.search_min_seconds=0;
    config.search_max_seconds=.010;config.audio_scale=a;config.common_offset_hz=b;config.hopping=true;
    unsigned valid_clean=0,valid_mismatch=0,valid_noise=0,valid_weak=0;
    const auto trial=[&](unsigned seed,double cn0,bool signal,bool mismatch) {
        std::mt19937_64 rng(seed);std::normal_distribution<double> normal;
        std::vector<Sample> input(960);
        const double sigma=std::sqrt(fs/(2*std::pow(10.,cn0/10)));
        for(std::size_t i=0;i<input.size();++i) {
            const double t=static_cast<double>(i)/fs, tx=a*(t-onset);
            const auto x=hi.mix_analytic({},tx,hp::sample_rate,40.L,0.L);
            const auto y=hl.mix_analytic({},tx,hp::sample_rate,40.L,0.L);
            const auto z=all.mix_analytic({},tx,hp::sample_rate,40.L,0.L);
            const auto low_phase=std::polar(1.,mismatch?1.5:0.);
            const auto aux_phase=std::polar(1.,mismatch?-1.5:0.);
            const auto clean=(x+(y-x)*low_phase+(z-y)*aux_phase)/(.4*h::pilot_amplitude_fraction);
            const auto received=(signal?clean:std::complex<double>{})*std::polar(1.,tau*b*t+.37)+
                std::complex<double>(sigma*normal(rng),sigma*normal(rng));
            input[i]={static_cast<float>(received.real()),static_cast<float>(received.imag())};
        }
        const auto result=h::estimate_helper_startup_timing(input,config);
        if(result.valid && signal)
            require(std::abs(result.onset_seconds-onset)<.5/(2400-550),"qualified Helper startup chose wrong phase branch");
        return result.valid;
    };
    for(unsigned i=0;i<8;++i)valid_clean+=trial(610+i,48,true,false);
    const bool noiseless=trial(609,std::numeric_limits<double>::infinity(),true,false);
    for(unsigned i=0;i<4;++i)valid_mismatch+=trial(710+i,48,true,true);
    for(unsigned i=0;i<16;++i)valid_noise+=trial(810+i,48,false,false);
    for(unsigned i=0;i<4;++i)valid_weak+=trial(910+i,28,true,false);
    require(noiseless && valid_clean==8,"clean startup tone controls failed");
    require(valid_mismatch==0,"differential pilot channel phase falsely qualified startup");
    require(valid_noise==0,"startup noise-only controls falsely qualified");
    std::cout<<"helper_startup_controls noiseless="<<noiseless<<" clean="<<valid_clean<<"/8 differential_phase="<<valid_mismatch
        <<"/4 noise="<<valid_noise<<"/16 weak="<<valid_weak<<"/4\n";
}

bool pilot_phase_usable(const h::HelperDwellMeasurement& m) {
    if(!m.valid || !std::isfinite(m.high_cn0_db_hz) || !std::isfinite(m.low_cn0_db_hz))return false;
    const double duration=m.end_sample_time_seconds-m.first_sample_time_seconds;
    const double high=std::pow(10.,m.high_cn0_db_hz/10),low=std::pow(10.,m.low_cn0_db_hz/10);
    return high>0 && low>0 && 3*std::sqrt(2/(high*duration)+2/(low*duration))<.5;
}

bool set_relative_helper_phase(StreamPair& stream,const h::HelperDwellMeasurement& m,
                               bool model_consistent,std::uint64_t first) {
    const auto begin=cpu();
    const double duration=m.end_sample_time_seconds-m.first_sample_time_seconds;
    const double high=std::pow(10.,m.high_cn0_db_hz/10);
    // This first-sample phase witness uses only the high pilot and its own
    // conservative measured noise floor. It grants no absolute TX time.
    if(!model_consistent || !m.valid || !std::isfinite(m.high_phase_radians) ||
       !std::isfinite(high) || !std::isfinite(m.first_sample_time_seconds) ||
       !std::isfinite(m.end_sample_time_seconds) || !std::isfinite(duration) || !(duration>0) || !(high>0) ||
       3*std::sqrt(2/(high*duration))>=.5 ||
       std::abs(m.first_sample_time_seconds-static_cast<double>(first+settle)/fs)>1e-9 ||
       std::abs(m.end_sample_time_seconds-static_cast<double>(first+settle+aperture)/fs)>1e-9) {
        stream.mixed->clear_phase();stream.receiver_cpu+=cpu()-begin;
        stream.clean->clear_phase();return false;
    }
    const auto anchor=static_cast<long double>(first+settle);
    const auto clock=stream.mixed->clock_time_at_input(anchor);
    if(!std::isfinite(clock)) {
        stream.mixed->clear_phase();stream.receiver_cpu+=cpu()-begin;
        stream.clean->clear_phase();return false;
    }
    // The continuous clock has an explicitly arbitrary origin. A fixed
    // unknown time/phase remains for the ordinary receiver to search; a
    // startup interval or a low/high-pilot integer branch is not asserted.
    const double phase=wrap(static_cast<double>(tau*hp::high_frequency_hz*clock)-m.high_phase_radians);
    const h::HelperStreamConditioner::LocalPhase local{anchor,phase,first+settle,
        first+settle+aperture,first+hp::dwell_samples};
    stream.receiver_cpu+=cpu()-begin;
    return stream.phase(local,true);
}

void relative_phase_controls() {
    StreamPair stream({hp::sample_rate,0,0,0,0},{1,0,.001});
    h::HelperDwellMeasurement m;m.valid=true;m.first_sample_time_seconds=.02;
    m.end_sample_time_seconds=.38;m.high_phase_radians=.3;m.high_cn0_db_hz=60;
    m.low_cn0_db_hz=-std::numeric_limits<double>::infinity();
    require(set_relative_helper_phase(stream,m,true,0),"relative high-pilot phase required an absolute or low-pilot clock witness");
    require(!set_relative_helper_phase(stream,m,false,0),"inconsistent model retained relative phase authority");
    m.high_cn0_db_hz=5;
    require(!set_relative_helper_phase(stream,m,true,0),"weak high pilot qualified relative phase");
    m.high_cn0_db_hz=60;m.valid=false;
    require(!set_relative_helper_phase(stream,m,true,0),"invalid pilot qualified relative phase");
    m.valid=true;m.end_sample_time_seconds=std::numeric_limits<double>::infinity();
    require(!set_relative_helper_phase(stream,m,true,0),"nonfinite pilot aperture qualified relative phase");
}

double integrated_clock(const h::HelperMeasurementSnapshot& s,double from,double to) {
    const double dt=to-from;
    return dt+1e-6*(s.audio_ppm*dt+.5*s.audio_drift_ppm_per_second*
        (std::pow(to-s.time_seconds,2)-std::pow(from-s.time_seconds,2)));
}

struct BranchBudget {
    double onset_radius=0,audio_three_sigma=0,phase_three_sigma=0,total=0,limit=0;
    bool available=false;
};
std::optional<double> helper_clock_at_anchor(const h::HelperDwellMeasurement& m,
    const h::HelperStartupTimingResult& onset,const h::HelperMeasurementSnapshot& s,BranchBudget* budget=nullptr) {
    if(!onset.valid || !pilot_phase_usable(m))return std::nullopt;
    const double separation=hp::high_frequency_hz-550;
    const double duration=m.end_sample_time_seconds-m.first_sample_time_seconds;
    const double high=std::pow(10.,m.high_cn0_db_hz/10),low=std::pow(10.,m.low_cn0_db_hz/10);
    const double sigma=std::sqrt(2/(high*duration)+2/(low*duration))/(tau*separation);
    const double from=onset.onset_seconds,to=m.first_sample_time_seconds;
    const double v0=(to-from)*1e-6;
    const double v2=.5*(std::pow(to-s.time_seconds,2)-std::pow(from-s.time_seconds,2))*1e-6;
    const double variance=v0*v0*s.covariance[0][0]+2*v0*v2*s.covariance[0][2]+v2*v2*s.covariance[2][2];
    if(!std::isfinite(variance) || variance<0)return std::nullopt;
    // The onset and phase-separation witnesses must jointly resolve which of
    // the 1/1850-second branches is intended; no probe may select a branch.
    const double total=project(s,from).audio*onset.support_radius_seconds+3*(sigma+std::sqrt(variance));
    if(budget)*budget={project(s,from).audio*onset.support_radius_seconds,3*std::sqrt(variance),3*sigma,total,
                       .5/separation,true};
    if(total >= .5/separation)
        return std::nullopt;
    const double cycles=wrap(m.high_phase_radians-m.low_phase_radians)/tau;
    const double predicted=integrated_clock(s,from,to);
    return (cycles+std::round(separation*predicted-cycles))/separation;
}

void print_tone(const char* name,const ToneMetrics& m,bool phase_available) {
    std::cout<<' '<<name<<"_windows="<<m.windows;
    if(m.windows)std::cout<<' '<<name<<"_twin_frequency_mean_hz="<<m.freq_mean
        <<' '<<name<<"_twin_frequency_rms_hz="<<m.freq_rms;
    if(m.windows && phase_available)std::cout<<' '<<name<<"_twin_phase_mean_deg="<<m.phase_mean*180/std::numbers::pi
        <<' '<<name<<"_twin_phase_rms_deg="<<m.phase_rms*180/std::numbers::pi
        <<' '<<name<<"_twin_phase_max_deg="<<m.phase_max*180/std::numbers::pi;
    else std::cout<<' '<<name<<"_phase=unavailable";
    if(m.noisy_windows)std::cout<<' '<<name<<"_noisy_frequency_mean_hz="<<m.noisy_freq_mean
        <<' '<<name<<"_noisy_frequency_rms_hz="<<m.noisy_freq_rms
        <<' '<<name<<"_noisy_frequency_fit_se_hz="<<m.noisy_freq_standard_error;
    else std::cout<<' '<<name<<"_noisy_probe_metrology=unavailable";
    if(m.noisy_windows && phase_available)std::cout<<' '<<name<<"_noisy_phase_rms_deg="<<m.noisy_phase_rms*180/std::numbers::pi;
}
void print_evm(const char* name,const EvmMetrics& m) {
    std::cout<<' '<<name<<"_symbols="<<m.symbols;
    if(m.symbols)std::cout<<' '<<name<<"_evm="<<m.noisy<<' '<<name<<"_twin_evm="<<m.clean
        <<' '<<name<<"_constellation_signal_error_db="<<-20*std::log10(std::max(m.noisy,1e-30));
}

void scenario(const char* label,hp::Config c,bool expect_qualified,bool expect_frequency_improvement=true,
              bool expect_direct_branch_ambiguous=false,bool enforce_available_quality=true,
              bool relative_mode=false,bool expect_relative_qualified=false) {
    hp::Source source(c);
    const double maximum_rf=c.hopping?(c.shift_center_hz>1e9?5846.8e6:31.225e6):c.shift_center_hz;
    const double shift_prior=c.shift_center_hz>1e9?.0001:1;
    const double width=3*(maximum_rf*shift_prior+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,
        c.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.rx_scale())};
    double generation_cpu=0,helper_cpu=0;
    const auto create_measurement=cpu();h::HelperMeasurement receiver(mc);helper_cpu+=cpu()-create_measurement;
    std::optional<hp::Dwell> first;
    std::optional<h::HelperMeasurementSnapshot> frozen;
    h::HelperStartupTimingResult onset;
    std::vector<h::HelperDwellMeasurement> earlier;
    std::unique_ptr<h::HelperPhaseClock> phase_clock;
    h::HelperPhaseClock::Observation phase_observation;
    std::unique_ptr<StreamPair> raw,fixed,continued,oracle,relative;
    unsigned valid_measurements=0,phase_frozen=0,phase_continued=0;
    unsigned phase_relative=0;
    unsigned hop_count=0,heldout_hop_count=0;
    double hop_error2=0,hop_error_max=0,heldout_hop_error2=0,heldout_hop_error_max=0;
    std::optional<h::HelperDwellMeasurement> previous_dwell;
    double previous_truth_phase=0,previous_truth_frequency=0;
    bool absolute_sync=false,continued_sync=false;
    BranchBudget budget{};
    for(unsigned dwell=0;dwell<100;++dwell) {
        const auto gen_start=cpu();auto d=source.generate(dwell);generation_cpu+=cpu()-gen_start;
        if(dwell==0)first=d;
        const auto start=cpu();
        const auto m=receiver.observe(std::span<const Sample>(d.samples.data()+settle,aperture),
                                      d.shift_hz,d.first_seconds+static_cast<double>(settle)/fs);
        if(m.valid)++valid_measurements;
        const auto measured=receiver.snapshot();
        helper_cpu+=cpu()-start;
        // This is evaluation only. Compare the receiver's hop-boundary phase
        // estimate with the same nominal-boundary definition applied to the
        // simulator's noiseless high-pilot phase and instantaneous frequencies.
        // Simulator values never enter the receiver or conditioner.
        const double anchor=d.first_seconds+static_cast<double>(settle)/fs;
        const double truth_phase=wrap(tau*std::remainder(hp::high_frequency_hz*source.tx_time(anchor),1.)+
                                      std::arg(C(d.common_phasor[settle])));
        const double truth_frequency=hp::high_frequency_hz*source.audio_scale_at(anchor)+
                                     source.shift_error_hz(d.shift_hz,anchor);
        if(c.hopping && previous_dwell) {
            const auto hop_cpu=cpu();
            const auto hop=h::helper_hop_phase_change(*previous_dwell,m,d.first_seconds);
            helper_cpu+=cpu()-hop_cpu;
            if(hop) {
                const double advance=previous_truth_frequency*(d.first_seconds-previous_dwell->first_sample_time_seconds)+
                    truth_frequency*(anchor-d.first_seconds);
                const double reference=wrap(truth_phase-previous_truth_phase-tau*advance);
                const double error=wrap(*hop-reference);
                ++hop_count;hop_error2+=error*error;hop_error_max=std::max(hop_error_max,std::abs(error));
                if(dwell>=95){++heldout_hop_count;heldout_hop_error2+=error*error;
                    heldout_hop_error_max=std::max(heldout_hop_error_max,std::abs(error));}
            }
        }
        previous_dwell=m;previous_truth_phase=truth_phase;previous_truth_frequency=truth_frequency;
        if(dwell<90)earlier.push_back(m);
        else if(phase_clock) {const auto begin=cpu();phase_observation=phase_clock->observe(m);helper_cpu+=cpu()-begin;}
        if(dwell==89) {
            if(measured.valid) {
                frozen=measured;
                h::HelperStartupTimingConfig sc;
                sc.sample_rate=fs;sc.first_sample_seconds=0;sc.search_min_seconds=0;sc.search_max_seconds=.010;
                const auto initial=project(*frozen,.02);
                sc.audio_scale=initial.audio;sc.common_offset_hz=first->shift_hz*initial.shift_ppm*1e-6;
                sc.hopping=c.hopping;
                const auto begin=cpu();onset=h::estimate_helper_startup_timing(first->samples,sc);helper_cpu+=cpu()-begin;
                if(onset.valid) {
                    const auto replay=cpu();
                    h::HelperPhaseClock::Config pc;
                    pc.phase_noise_radians_per_sqrt_second=mc.phase_noise_radians_per_sqrt_second;
                    phase_clock=std::make_unique<h::HelperPhaseClock>(pc);
                    require(phase_clock->seed(onset),"valid startup did not seed Helper phase clock");
                    for(const auto& previous:earlier)phase_observation=phase_clock->observe(previous);
                    helper_cpu+=cpu()-replay;
                }
            }
        }
        if(dwell<94)continue;
        const auto first_sample=static_cast<std::uint64_t>(std::llround(d.first_seconds*fs));
        const auto middle=d.first_seconds+.2;
        const auto coefficients_started=cpu();
        const auto cf=frozen?project(*frozen,middle):Coeff{};
        helper_cpu+=cpu()-coefficients_started;
        const auto cc=measured.valid?project(measured,middle):cf;
        const auto ideal_audio=source.audio_scale_at(middle);
        const auto ideal_b=source.shift_error_hz(d.shift_hz,middle);
        if(dwell==94) {
            raw=std::make_unique<StreamPair>(h::HelperStreamConditioner::Config{hp::sample_rate,first_sample,first_sample,d.first_seconds,0},
                                             h::HelperStreamConditioner::Correction{1,0,.001});
            // The long-interval direct extrapolation is retained only as a
            // diagnostic. The actual source-time branch follows successive
            // high-minus-low Helper phases; random common hop phase cancels.
            const auto origin_started=cpu();
            if(frozen)(void)helper_clock_at_anchor(m,onset,*frozen,&budget);
            absolute_sync=phase_observation.valid && frozen.has_value();
            const double clock=absolute_sync?phase_observation.source_seconds-integrated_clock(*frozen,d.first_seconds,m.first_sample_time_seconds):d.first_seconds;
            const auto fc=stream_config(first_sample,cf.audio,clock);
            helper_cpu+=cpu()-origin_started;
            fixed=std::make_unique<StreamPair>(fc,h::HelperStreamConditioner::Correction{cf.audio,d.shift_hz*cf.shift_ppm*1e-6,.001});
            if(relative_mode && frozen) {
                // Receiver-defined origin: shat(36 s)=36 s. No simulator or
                // probe value selects this source coordinate or its phase.
                const auto relative_origin_started=cpu();
                const double relative_clock=36+integrated_clock(*frozen,36,d.first_seconds);
                const auto rc=stream_config(first_sample,cf.audio,relative_clock);
                const double relative_origin_cpu=cpu()-relative_origin_started;
                relative=std::make_unique<StreamPair>(rc,h::HelperStreamConditioner::Correction{
                    cf.audio,d.shift_hz*cf.shift_ppm*1e-6,.001});
                relative->receiver_cpu+=relative_origin_cpu;
            }
            continued_sync=phase_observation.valid && measured.valid;
            const double clock_cont=continued_sync?phase_observation.source_seconds-integrated_clock(measured,d.first_seconds,m.first_sample_time_seconds):d.first_seconds;
            const auto dc=stream_config(first_sample,cc.audio,clock_cont);
            continued=std::make_unique<StreamPair>(dc,h::HelperStreamConditioner::Correction{cc.audio,d.shift_hz*cc.shift_ppm*1e-6,.001});
            const auto ic=stream_config(first_sample,ideal_audio,source.tx_time(d.first_seconds));
            oracle=std::make_unique<StreamPair>(ic,h::HelperStreamConditioner::Correction{ideal_audio,ideal_b,.001});
        } else {
            fixed->update({cf.audio,d.shift_hz*cf.shift_ppm*1e-6,.001},true);
            if(relative)relative->update({cf.audio,d.shift_hz*cf.shift_ppm*1e-6,.001},true);
            continued->update({cc.audio,d.shift_hz*cc.shift_ppm*1e-6,.001},false);
            oracle->update({ideal_audio,ideal_b,.001},false);
        }
        raw->mixed->clear_phase();raw->clean->clear_phase();
        const auto clear_started=cpu();fixed->mixed->clear_phase();fixed->receiver_cpu+=cpu()-clear_started;
        fixed->clean->clear_phase();
        continued->mixed->clear_phase();continued->clean->clear_phase();
        oracle->mixed->clear_phase();oracle->clean->clear_phase();
        const auto phase_check_started=cpu();
        const bool fixed_phase_available=absolute_sync && phase_observation.valid && pilot_phase_usable(m);
        fixed->receiver_cpu+=cpu()-phase_check_started;
        if(fixed_phase_available && set_helper_phase(*fixed,m,phase_observation,first_sample,true) && dwell>=95)++phase_frozen;
        if(continued_sync && phase_observation.valid && pilot_phase_usable(m) &&
           set_helper_phase(*continued,m,phase_observation,first_sample,false) && dwell>=95)++phase_continued;
        if(relative && set_relative_helper_phase(*relative,m,measured.valid,first_sample) && dwell>=95)++phase_relative;
        set_ideal_phase(*oracle,d,first_sample);
        raw->feed(d,false);fixed->feed(d,true);continued->feed(d,false);oracle->feed(d,false);
        if(relative)relative->feed(d,true);
    }
    require(raw && fixed && continued && oracle,"probe stream was not created");
    std::cout<<std::setprecision(9)<<"helper_live_probe case="<<label
        <<" seed="<<c.seed<<" hop_schedule="<<(c.hopping?(c.shift_center_hz>1e9?"keyed_fcc100":"keyed_hf50"):"none")
        <<" probe="<<(c.probe==hp::Probe::tone?"tone":"apsk64")
        <<" helper_w="<<c.helper_w<<" probe_w="<<c.probe_w<<" loss_db="<<c.path_loss_db
        <<" arrival_offset_s="<<c.arrival_offset_seconds<<" tx_audio_ppm="<<c.tx_audio_ppm<<" rx_audio_ppm="<<c.rx_audio_ppm
        <<" temperature_K="<<c.temperature_kelvin<<" noise_figure_db="<<c.noise_figure_db
        <<" high_cn0_db_hz="<<source.high_cn0_db_hz()<<" probe_cn0_db_hz="<<source.probe_cn0_db_hz()
        <<" probe_360hz_nominal_snr_db="<<(source.probe_cn0_db_hz()-10*std::log10(360.))
        <<" helper_fit="<<bool(frozen)<<" valid_dwells="<<valid_measurements
        <<" startup_valid="<<onset.valid<<" startup_radius_s="<<(onset.valid?onset.support_radius_seconds:-1)
        <<" startup_evidence="<<onset.evidence<<" startup_evidence_threshold="<<onset.evidence_threshold
        <<" startup_channel_score="<<onset.channel_consistency_score
        <<" startup_channel_threshold="<<onset.channel_consistency_threshold
        <<" phase_clock_valid="<<phase_observation.valid<<" phase_clock_observations="<<phase_observation.observations
        <<" phase_clock_transfer=common_complex_path_assumed"
        <<" frozen_scope=36s_frequency_audio_fresh_same_hop_phase_clock"
        <<" absolute_sync="<<absolute_sync<<" continued_sync="<<continued_sync<<" phase_frozen_dwells="<<phase_frozen
        <<" phase_continued_dwells="<<phase_continued<<" buffering_latency_s=.4"
        <<" conditioning_cpu_s="<<fixed->receiver_cpu<<" helper_cpu_s="<<helper_cpu
        <<" receiver_cpu_s="<<(helper_cpu+fixed->receiver_cpu)<<" generation_cpu_s="<<generation_cpu;
    if(relative_mode) {
        std::cout<<" relative_clock_origin=receiver_36s relative_absolute_time_claim=0"
                 <<" relative_clock_model=frozen36s_linear relative_fresh_model_gate=snapshot_valid"
                 <<" relative_phase_anchors="<<phase_relative<<" relative_phase_possible=5";
        if(relative)std::cout<<" relative_conditioning_cpu_s="<<relative->receiver_cpu
            <<" relative_receiver_cpu_s="<<(helper_cpu+relative->receiver_cpu)
            <<" relative_cpu_includes_absolute_diagnostics=1";
    }
    if(c.hopping) {
        std::cout<<" nominal_boundary_hop_evaluated="<<hop_count<<" nominal_boundary_hop_possible=99"
                 <<" heldout_nominal_boundary_hop_evaluated="<<heldout_hop_count<<" heldout_nominal_boundary_hop_possible=5";
        if(hop_count)std::cout<<" nominal_boundary_hop_error_rms_deg="<<std::sqrt(hop_error2/hop_count)*180/std::numbers::pi
                             <<" nominal_boundary_hop_error_max_deg="<<hop_error_max*180/std::numbers::pi;
        if(heldout_hop_count)std::cout<<" heldout_nominal_boundary_hop_error_rms_deg="<<std::sqrt(heldout_hop_error2/heldout_hop_count)*180/std::numbers::pi
                                    <<" heldout_nominal_boundary_hop_error_max_deg="<<heldout_hop_error_max*180/std::numbers::pi;
    }
    if(budget.available)std::cout<<" branch_onset_radius_us="<<budget.onset_radius*1e6
        <<" branch_audio_three_sigma_us="<<budget.audio_three_sigma*1e6
        <<" branch_phase_three_sigma_us="<<budget.phase_three_sigma*1e6
        <<" branch_total_us="<<budget.total*1e6<<" branch_limit_us="<<budget.limit*1e6;
    if(frozen) {
        const double v[]{hp::probe_frequency_hz*1e-6,c.shift_center_hz*1e-6,0,0};double variance=0;
        for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j)variance+=v[i]*frozen->covariance[i][j]*v[j];
        std::cout<<" frozen_audio_ppm="<<frozen->audio_ppm<<" frozen_shift_ppm="<<frozen->shift_ppm
                 <<" frozen_data_sigma_hz="<<std::sqrt(std::max(0.,variance));
    }
    if(expect_direct_branch_ambiguous)
        require(budget.available && budget.total>=budget.limit,"direct-extrapolation ambiguity was not reproduced");
    const auto metrology_started=cpu();
    if(c.probe==hp::Probe::tone) {
        // The noise-limited availability here is an explicit experimental
        // metrology rule, not a receiver gate or signal-derived correction.
        // Ten-ms blocks below four expected probe cycles of C/N0 cannot support
        // reliable phase unwrap; retain the clean-twin measurement instead.
        const bool metrology=!c.noise || std::pow(10.,source.probe_cn0_db_hz()/10)*.01>=4;
        const auto before=tone_metrics(raw->output,false,metrology),after=tone_metrics(fixed->output,absolute_sync,metrology);
        const auto after_cont=tone_metrics(continued->output,continued_sync,metrology),ideal=tone_metrics(oracle->output,true,metrology);
        print_tone("before",before,true);print_tone("frozen",after,absolute_sync);
        print_tone("continued",after_cont,continued_sync);print_tone("oracle",ideal,true);
        if(relative_mode) {
            if(relative) {
                // Evaluation only: exactly one fixed unknown gauge C is
                // computed from simulator CLOCK truth at the declared origin.
                // No tone sample, phase, timing or frequency is fitted, and
                // neither C nor the reference phase feeds any receiver path.
                const double gauge=36-source.tx_time(36);
                const double expected_phase=wrap(tau*(hp::high_frequency_hz-hp::probe_frequency_hz)*gauge);
                const auto result=tone_metrics(relative->output,true,metrology,expected_phase);
                std::cout<<" relative_evaluator_fixed_time_gauge_s="<<gauge
                         <<" relative_evaluator_fixed_phase_deg="<<expected_phase*180/std::numbers::pi;
                print_tone("relative",result,true);
                if(expect_relative_qualified) {
                    require(phase_relative>=4 && result.windows>=4,"relative Helper phase correction unavailable");
                    require(result.freq_rms<.1 && result.phase_rms<.5,"relative Helper correction failed modeled tone");
                }
            } else {
                std::cout<<" relative_correction=unavailable";
                require(!expect_relative_qualified,"required relative Helper model unavailable");
            }
        }
        if(enforce_available_quality && !expect_qualified && absolute_sync && after.windows)
            require(after.freq_rms<1 && after.phase_rms<.5,"available Helper correction failed modeled tone");
        if(expect_qualified) {
            require(absolute_sync && phase_frozen>=4 && phase_continued>=4,"Helper-only absolute phase/time unavailable");
            require(before.windows>=4 && after.windows>=4 && ideal.windows>=4,"insufficient tone windows");
            if(expect_frequency_improvement)
                require(after.freq_rms < before.freq_rms*.5 && after.freq_rms<1,"Helper frequency correction did not improve tone");
            require(after.phase_rms < before.phase_rms && after.phase_rms<.5,"Helper phase correction did not improve tone");
        }
    } else {
        const auto before=evm_metrics(raw->output,source,false);
        const auto ideal=evm_metrics(oracle->output,source,true);
        print_evm("before",before);
        if(relative_mode) {
            if(relative) {
                // Evaluator-only fixed gauge from the simulator clock at the
                // receiver's chosen origin. It does not enter any receiver
                // decision or depend on samples, symbols or a best-fit search.
                const double gauge=36-source.tx_time(36);
                const double expected_phase=wrap(tau*(hp::high_frequency_hz-hp::probe_frequency_hz)*gauge);
                const auto result=evm_metrics(relative->output,source,true,gauge,expected_phase);
                std::cout<<" relative_evaluator_fixed_time_gauge_s="<<gauge
                         <<" relative_evaluator_fixed_phase_deg="<<expected_phase*180/std::numbers::pi;
                print_evm("relative",result);
                if(expect_relative_qualified) {
                    require(phase_relative>=4 && result.symbols>=250,
                            "relative Helper APSK correction unavailable");
                    require(result.clean<.2 && result.noisy<.3,
                            "relative Helper correction failed fixed-gauge APSK");
                    // A wrong *fixed* one-symbol gauge must remain wrong. No
                    // evaluator search may relabel or align the probe.
                    const double wrong_gauge=gauge+1/hp::symbol_rate;
                    const auto wrong=evm_metrics(relative->output,source,true,wrong_gauge,
                        wrap(tau*(hp::high_frequency_hz-hp::probe_frequency_hz)*wrong_gauge));
                    require(wrong.symbols>=250 && wrong.clean>result.clean+.3,
                            "fixed-gauge APSK evaluator concealed a timing error");
                }
            } else {
                std::cout<<" relative_correction=unavailable";
                require(!expect_relative_qualified,"required relative Helper model unavailable");
            }
        }
        if(absolute_sync) {
            const auto after=evm_metrics(fixed->output,source,true);
            print_evm("frozen",after);
            if(continued_sync)print_evm("continued",evm_metrics(continued->output,source,true));
            else std::cout<<" continued_evm=unavailable";
            if(expect_qualified) {
                require(after.symbols>=250 && ideal.symbols>=250,"insufficient known-date APSK symbols");
                require(after.clean < before.clean*.7 && after.noisy < before.noisy*.8,
                        "Helper correction did not improve fixed-convention constellation EVM");
            }
        } else std::cout<<" frozen_evm=unavailable continued_evm=unavailable";
        print_evm("oracle",ideal);
    }
    std::cout<<" metrology_cpu_s="<<(cpu()-metrology_started);
    std::cout<<'\n';
}

void run_suite() {
    late_phase_authority_control();
    mixer_chunk_invariance_control();
    rate_slew_control(1.0005,true);
    rate_slew_control(1.000001,false);
    phase_clock_controls();
    clock_conflict_control();
    startup_channel_consistency_control();
    relative_phase_controls();
    hp::Config control;control.helper_w=4000;control.probe_w=4000;control.path_loss_db=180;
    control.noise=false;control.phase_noise_degrees_per_sqrt_second=0;control.hopping=false;
    control.tx_audio_ppm=0;control.rx_audio_ppm=0;control.relative_shift_ppm=.8;
    control.probe=hp::Probe::tone;control.seed=810;scenario("control-offset-only",control,true);
    control.tx_audio_ppm=37;control.rx_audio_ppm=-13;control.relative_shift_ppm=0;
    control.seed=811;scenario("control-audio-only",control,true);
    control.tx_audio_ppm=0;control.rx_audio_ppm=0;control.hopping=true;control.seed=812;
    scenario("control-random-hop-phase",control,true,false);
    control.tx_audio_ppm=-37;control.rx_audio_ppm=13;control.relative_shift_ppm=.8;control.seed=813;
    scenario("control-negative-audio-clock",control,true);
    hp::Config c;c.helper_w=4000;c.probe_w=4000;c.path_loss_db=180;c.hopping=true;
    c.probe=hp::Probe::tone;c.seed=901;scenario("hf-keyed-fhss",c,true,true,true);
    c.probe=hp::Probe::apsk64;scenario("hf-keyed-fhss",c,true);
    {
        // The Helper and diagnostic probe each use a transceiver-scale 40 W.
        // This is a required positive random-hop control, alongside the
        // exploratory loss/seed sweep which makes no success assertion.
        auto matched=c;matched.helper_w=40;matched.probe_w=40;matched.path_loss_db=160;
        matched.probe=hp::Probe::tone;scenario("hf-40w-160db",matched,true);
        scenario("hf-40w-relative-160db",matched,false,true,false,true,true,true);
        matched.probe=hp::Probe::apsk64;scenario("hf-40w-160db",matched,true);
        scenario("hf-40w-relative-apsk-160db",matched,false,true,false,true,true,true);
    }
    c.shift_center_hz=5.8e9;c.relative_shift_ppm=.00008;c.phase_noise_degrees_per_sqrt_second=.005;
    c.probe=hp::Probe::tone;c.seed=902;scenario("microwave-keyed-fhss",c,true);
    c.probe=hp::Probe::apsk64;scenario("microwave-keyed-fhss",c,true);
    c.shift_center_hz=30e6;c.relative_shift_ppm=.8;c.phase_noise_degrees_per_sqrt_second=.05;
    c.hopping=false;c.probe=hp::Probe::tone;c.seed=903;scenario("hf-no-fhss",c,true);
    c.probe=hp::Probe::apsk64;scenario("hf-no-fhss",c,true);
    c.helper_w=40;c.probe_w=4000;c.hopping=true;c.probe=hp::Probe::tone;c.seed=904;
    scenario("hf-weak-helper",c,false);
    c.helper_w=4000;c.tx_audio_drift_ppm_per_second=.04;c.seed=906;
    scenario("hf-audio-drift",c,false);
    c.tx_audio_drift_ppm_per_second=0;
    c.helper_w=15000;c.path_loss_db=220;c.shift_center_hz=5.8e9;c.relative_shift_ppm=.00008;
    c.phase_noise_degrees_per_sqrt_second=.005;c.probe=hp::Probe::tone;c.seed=905;
    scenario("microwave-15kw-220db",c,false);
}

void run_probe() {
    full_dwell_missing_phase_control();
    hp::Config c;c.helper_w=4000;c.probe_w=4000;c.path_loss_db=180;
    hp::Source source(c);
    h::HelperMeasurement receiver({fs,550,2400,2300,2500,300,.05*std::numbers::pi/180});
    for(unsigned i=0;i<2;++i) {
        const auto d=source.generate(i);
        (void)receiver.observe(std::span<const Sample>(d.samples.data()+settle,aperture),
                               d.shift_hz,d.first_seconds+static_cast<double>(settle)/fs);
    }
    require(!receiver.snapshot().valid,"two-dwell probe manufactured a full clock fit");
    h::HelperStartupTimingConfig onset_config;
    onset_config.sample_rate=fs;onset_config.first_sample_seconds=0;
    onset_config.search_min_seconds=0;onset_config.search_max_seconds=.010;
    std::mt19937_64 rng(512);std::normal_distribution<float> normal;
    std::vector<Sample> noise(960);
    for(auto& z:noise)z={normal(rng),normal(rng)};
    const auto noise_result=h::estimate_helper_startup_timing(noise,onset_config);
    require(!noise_result.valid,"startup noise-only falsely qualified absolute timing");
    std::cout<<"helper_live_short seconds=.8 fit=unavailable high_cn0_db_hz="<<source.high_cn0_db_hz()<<'\n';
}

void run_40w_short(double loss,std::uint64_t seed) {
    hp::Config c;c.helper_w=40;c.probe_w=4000;c.path_loss_db=loss;c.hopping=true;c.seed=seed;
    hp::Source source(c);
    const auto d=source.generate(0);
    const double width=3*(31.225e6+2400*100)*1e-6;
    h::HelperMeasurement receiver({fs,550,2400,2400-width,2400+width,300,
                                    c.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.rx_scale())});
    const auto started=cpu();
    const auto m=receiver.observe(std::span<const Sample>(d.samples.data()+settle,aperture),
                                  d.shift_hz,d.first_seconds+static_cast<double>(settle)/fs);
    h::HelperStartupTimingResult onset;
    if(m.valid) {
        // A single pilot aperture supplies the local frequency/scale used in
        // this feasibility probe. No simulator clock or frequency is supplied.
        h::HelperStartupTimingConfig sc;
        sc.sample_rate=fs;sc.first_sample_seconds=0;sc.search_min_seconds=0;sc.search_max_seconds=.010;
        sc.audio_scale=(m.high_frequency_hz-m.low_frequency_hz)/(2400-550);
        sc.common_offset_hz=m.high_frequency_hz-2400*sc.audio_scale;
        onset=h::estimate_helper_startup_timing(d.samples,sc);
    }
    const double receiver_cpu=cpu()-started;
    std::cout<<std::setprecision(9)<<"helper_live_40w_short seed="<<seed<<" loss_db="<<loss
             <<" helper_w=40 hop_schedule=keyed_hf50 seconds=.4"
             <<" high_cn0_db_hz="<<source.high_cn0_db_hz()<<" first_dwell_valid="<<m.valid
             <<" startup_valid="<<onset.valid<<" startup_evidence="<<onset.evidence
             <<" startup_evidence_threshold="<<onset.evidence_threshold
             <<" startup_channel_score="<<onset.channel_consistency_score
             <<" startup_channel_threshold="<<onset.channel_consistency_threshold
             <<" receiver_cpu_s="<<receiver_cpu;
    if(onset.valid) {
        // Compare to source truth solely after the receiver has returned.
        const double actual=c.arrival_offset_seconds*source.rx_scale()-c.fractional_input_sample/fs;
        const double error=onset.onset_seconds-actual;
        std::cout<<" onset_error_us="<<error*1e6<<" onset_radius_us="<<onset.support_radius_seconds*1e6
                 <<" onset_truth_covered="<<(std::abs(error)<=onset.support_radius_seconds);
    }
    std::cout<<'\n';
}

double command_number(const char* text,const char* name) {
    std::size_t used=0;const std::string s(text);
    const double value=std::stod(s,&used);
    if(used!=s.size() || !std::isfinite(value))throw std::invalid_argument(std::string("invalid ")+name);
    return value;
}
std::uint64_t command_seed(const char* text) {
    const std::string s(text);
    if(s.empty() || s.find_first_not_of("0123456789")!=std::string::npos)
        throw std::invalid_argument("invalid seed");
    std::size_t used=0;const auto seed=std::stoull(s,&used);
    if(used!=s.size() || seed>std::numeric_limits<unsigned>::max())throw std::invalid_argument("invalid seed");
    return seed;
}
}

int main(int argc,char** argv) {
    try {
        if(argc>1 && (std::string(argv[1])=="--40w" || std::string(argv[1])=="--40w-short" ||
                     std::string(argv[1])=="--40w-relative")) {
            const bool short_run=std::string(argv[1])=="--40w-short";
            const bool relative_mode=std::string(argv[1])=="--40w-relative";
            if((short_run && argc!=4) || (!short_run && (argc<4 || argc>9)))
                throw std::invalid_argument("expected --40w-short LOSS_DB SEED or --40w[-relative] LOSS_DB SEED [tone|apsk] [PROBE_W] [ARRIVAL_S] [TX_AUDIO_PPM] [RX_AUDIO_PPM]");
            const double loss=command_number(argv[2],"loss");const auto seed=command_seed(argv[3]);
            if(loss<0 || loss>400)throw std::invalid_argument("loss outside 0 to 400 dB");
            if(short_run){run_40w_short(loss,seed);return 0;}
            hp::Config c;c.helper_w=40;c.probe_w=relative_mode?40:4000;c.path_loss_db=loss;c.hopping=true;c.seed=seed;
            if(argc>=5) {
                const std::string probe(argv[4]);
                if(probe=="apsk")c.probe=hp::Probe::apsk64;
                else if(probe=="tone")c.probe=hp::Probe::tone;
                else throw std::invalid_argument("expected tone or apsk");
            }
            if(argc>=6)c.probe_w=command_number(argv[5],"probe power");
            if(argc>=7)c.arrival_offset_seconds=command_number(argv[6],"arrival offset");
            if(argc>=8)c.tx_audio_ppm=command_number(argv[7],"TX audio ppm");
            if(argc>=9)c.rx_audio_ppm=command_number(argv[8],"RX audio ppm");
            if(c.probe_w<=0 || c.probe_w>1e9)throw std::invalid_argument("probe power outside (0, 1e9] W");
            scenario(relative_mode?"hf-40w-relative-sweep":"hf-40w-sweep",c,false,true,false,false,relative_mode);return 0;
        }
        if(argc>1 && std::string(argv[1])=="--probe"){run_probe();return 0;}
        if(argc>1 && (std::string(argv[1])=="--bright" || std::string(argv[1])=="--very-bright" || std::string(argv[1])=="--branch")) {
            const bool bright=std::string(argv[1])!="--branch";
            const bool lower_loss=std::string(argv[1])=="--very-bright";
            hp::Config c;c.helper_w=lower_loss?4000:(bright?15000:4000);c.probe_w=4000;
            c.path_loss_db=lower_loss?160:180;c.hopping=true;c.seed=901;
            scenario(lower_loss?"hf-very-bright":(bright?"hf-bright":"hf-branch-ambiguous"),c,true,true,!lower_loss);return 0;
        }
        if(argc>1 && std::string(argv[1])!="--suite")throw std::invalid_argument("expected --suite, --probe, --bright, --branch, --40w, --40w-relative or --40w-short");
        run_suite();return 0;
    } catch(const std::exception& e) {std::cerr<<"Helper live probe failure: "<<e.what()<<'\n';return 1;}
}
