#include "datapump/channel.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/transfer.hpp"
#include "../src/pattern_correlator_batch.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Exact AWGN sufficient-statistic Monte Carlo for the complete single-symbol
// coherent admission branch. The configured bank is one authoritative pair and
// one exact fractional start; 32 chips plus an optional partial chip have neither the section detector nor
// local differential windows nor a short-symbol acquisition chain. All four
// private 0/1 real basis vectors, the finite limited transmitted waveform and
// optional interference share one joint orthonormal noise span. Orthogonal noise
// energy is chi-square, not an independent substitute for the fitted energy.
// This is conditional on a fixed impaired signal and is not a receiver runtime
// benchmark or qualification of an untested search bank.
namespace {
using namespace datapump;using namespace datapump::modem;using namespace datapump::modem::detail;
using Complex=std::complex<double>;
// Match the receiver's double constant, block restarts and multiplication order.
constexpr double tau=2*std::numbers::pi;
void check(bool condition,const char* message){if(!condition)throw Error(message);}
template<class T>T number(std::string_view input){T out{};auto r=std::from_chars(input.data(),input.data()+input.size(),out);if(r.ec!=std::errc{}||r.ptr!=input.data()+input.size())throw Error("invalid argument");return out;}
struct Args {
    std::string csv;std::uint32_t rate=64;std::uint64_t chip=4097,tail=0,seed=719;
    double carrier=16,ppm=0,offset=0,diffusion=0,interference=0,interference_frequency=17;
    double minimum=-60,maximum=20,root_tolerance=1e-7;unsigned trials=20000,bit=0,verify=8;
};
Args arguments(int argc,char** argv){
    Args a;for(int i=1;i<argc;++i){const std::string flag=argv[i];
        if(flag=="--help") {std::cout<<"benchmark_pulse_statistics --csv PATH --trials 20000 --bit 0|1\n"
            " --sample-rate HZ --carrier HZ --chip-samples N --tail-samples N --clock-ppm N --frequency-offset HZ\n"
            " --phase-diffusion N --interference-amplitude N --interference-frequency HZ\n"
            " --minimum-cn0 DB-Hz --maximum-cn0 DB-Hz --root-tolerance-db N --seed N --verify-pcm-seeds N\n"
            "Conditional exact joint-noise statistic Monte Carlo, single paired coherent bank.\n"
            "Whole symbols require chip>4096; a partial final chip permits chip>=1024, 0<tail<chip, and duration>=16s.\n";std::exit(0);}
        if(i+1==argc)throw Error("missing argument");const std::string v=argv[++i];
        if(flag=="--csv")a.csv=v;else if(flag=="--trials")a.trials=number<unsigned>(v);else if(flag=="--bit")a.bit=number<unsigned>(v);
        else if(flag=="--sample-rate")a.rate=number<std::uint32_t>(v);else if(flag=="--chip-samples")a.chip=number<std::uint64_t>(v);
        else if(flag=="--tail-samples")a.tail=number<std::uint64_t>(v);
        else if(flag=="--carrier")a.carrier=number<double>(v);else if(flag=="--clock-ppm")a.ppm=number<double>(v);
        else if(flag=="--frequency-offset")a.offset=number<double>(v);else if(flag=="--phase-diffusion")a.diffusion=number<double>(v);
        else if(flag=="--interference-amplitude")a.interference=number<double>(v);else if(flag=="--interference-frequency")a.interference_frequency=number<double>(v);
        else if(flag=="--minimum-cn0")a.minimum=number<double>(v);else if(flag=="--maximum-cn0")a.maximum=number<double>(v);
        else if(flag=="--root-tolerance-db")a.root_tolerance=number<double>(v);else if(flag=="--seed")a.seed=number<std::uint64_t>(v);
        else if(flag=="--verify-pcm-seeds")a.verify=number<unsigned>(v);else throw Error("unknown option: "+flag);
    }
    check(a.bit<2&&a.trials&&a.trials<=1000000&&a.chip<=250000&&a.tail<a.chip&&
          (a.tail?a.chip>=1024:a.chip>4096),"invalid bit, trials, chip or partial-tail bound");
    check(a.rate&&(!a.tail||32*a.chip+a.tail>=16ULL*a.rate),"partial statistic geometry requires at least16 seconds");
    check(a.minimum<a.maximum&&a.maximum-a.minimum<=200&&a.root_tolerance>0&&a.root_tolerance<=.01,"invalid C/N0 root bracket");
    check(std::isfinite(a.ppm)&&std::abs(a.ppm)<=10000,"invalid clock scaling");
    return a;
}
long double dot(std::span<const long double>a,std::span<const long double>b){long double result=0;for(std::size_t i=0;i<a.size();++i)result+=a[i]*b[i];return result;}
long double gamma(long double operations,long double epsilon){check(operations*epsilon<.01L,"numerical roundoff allowance is too large");return operations*epsilon/(1-operations*epsilon);}
struct Model {
    Config config;long double origin=0,rate=1;double frequency=0;
    PatternCorrelatorWork work;
    std::uint64_t first=0,end=0,count=0;
    // raw0I,raw0Q,raw1I,raw1Q,signal,interference, four optimized-minus-raw columns
    std::array<std::vector<long double>,10> vectors;
    std::vector<std::vector<long double>> orthonormal;
    std::array<std::vector<long double>,10> coordinates;
    std::array<CorrelationFit,2> raw_gram{},optimized_gram{};
    std::array<long double,10> signal_dots{},interference_dots{};
    std::array<long double,2> moment_envelope_energy{};
    std::uint64_t segments=0;
    long double signal_energy=0,interference_energy=0,signal_interference=0;
    double vector_error=0,gram_error=0,covariance_error=0,false_accept_bound=0;
    std::array<double,2> noise_eigenvalue_upper{};
};
PatternSearch paired_search(const Args& a,const Model& model) {
    PatternSearch search;search.start_offset_seconds=static_cast<double>(model.origin/model.config.sample_rate);search.start_uncertainty_seconds=0;
    search.hypotheses={{model.frequency-model.config.carrier_hz,a.ppm}};search.search_stream_phases=false;
    search.compact_clock_search=true;search.worker_threads=1;search.retain_score=0;search.chunk_bits=1;
    return search;
}
PatternCorrelationBackend optimized_backend(const Args& a) {
    return a.tail?PatternCorrelationBackend::pulse_segments:PatternCorrelationBackend::pulse_moments;
}
void add_fit(CorrelationFit& a,const CorrelationFit& b){a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;}
Model prepare(const Args& a){
    const auto symbol=32*a.chip+a.tail;
    Model m;m.config.sample_rate=a.rate;m.config.carrier_hz=a.carrier;m.config.bandwidth_hz=2.*a.rate/static_cast<double>(a.chip);
    m.config.spreading_factor=32;m.config.integration_seconds=static_cast<double>(symbol)/a.rate;m.config.scramble=true;
    for(unsigned i=0;i<8&&symbol_sample_count(m.config)>symbol;++i)
        m.config.integration_seconds=std::nextafter(m.config.integration_seconds,0.);
    for(unsigned i=0;i<8&&pattern_chip_samples(m.config)>a.chip;++i)m.config.bandwidth_hz=std::nextafter(m.config.bandwidth_hz,std::numeric_limits<double>::infinity());
    check(pattern_chip_samples(m.config)==a.chip&&symbol_sample_count(m.config)==symbol,"statistic geometry requires32 whole chips and the requested partial tail");
    transfer::Options options;options.modem=m.config;std::array<std::uint8_t,32> key{};
    for(std::size_t i=0;i<key.size();++i)key[i]=static_cast<std::uint8_t>(37+17*i);options.key.emplace(key);options.timestamp=1800000000;
    m.config=transfer::seeded_config(options,options.timestamp);validate(m.config);
    m.rate=1+static_cast<long double>(a.ppm)*1e-6L;m.frequency=a.carrier*static_cast<double>(m.rate)+a.offset;
    // Use the strongest valid channel SNR. Its tiny fixed noise is included
    // in the conditioned float signal vector; no waveform sample is discarded.
    ChannelConfig impairment;impairment.seed=a.seed;impairment.snr_db=300;
    impairment.clock_error_ppm=a.ppm;impairment.frequency_offset_hz=a.offset;impairment.phase_noise_degrees_per_sqrt_second=a.diffusion;
    SampledSimulationChannel channel(m.config,impairment);StreamingTransmitter source(RawBits{Bytes{static_cast<std::uint8_t>(a.bit)}},m.config,8*1024*1024);
    m.origin=channel.startup_offset_samples()+static_cast<long double>(training_sample_count(m.config)+pattern_pulse_padding_samples(m.config))/m.rate;
    m.origin=static_cast<long double>(static_cast<double>(m.origin/m.config.sample_rate))*m.config.sample_rate;
    m.first=static_cast<std::uint64_t>(std::ceil(m.origin));m.end=static_cast<std::uint64_t>(std::ceil(m.origin+symbol_sample_count(m.config)/m.rate));m.count=m.end-m.first;
    {
        PatternCorrelator probe(m.config,paired_search(a,m),8*1024*1024);
        m.work=probe.work();
        check(m.work.backend==optimized_backend(a),"automatic receiver did not select the represented statistic backend");
        check(m.work.drift_sections==1&&!m.work.differential_window_samples&&m.work.hypotheses==1&&m.work.phase_groups==1,
              "statistic geometry does not use a single coherent paired detector");
    }
    for(auto& v:m.vectors)v.resize(static_cast<std::size_t>(m.count));
    std::array<float,2048> block{};std::uint64_t position=0;
    while(position<m.end){const auto wanted=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),m.end-position));
        auto n=channel.read(source,std::span(block).first(wanted));if(!n){n=wanted;channel.read_noise(std::span(block).first(n));}
        for(std::size_t i=0;i<n;++i)if(position+i>=m.first)m.vectors[4][position+i-m.first]=block[i];position+=n;
    }
    PatternCode pattern(m.config,m.config.stream_epoch);std::vector<Complex> oscillator(static_cast<std::size_t>(m.count));
    const auto step=std::polar(1.,tau*m.frequency/a.rate);Complex phase{};
    for(std::uint64_t sample=m.first-m.first%32;sample<m.end;++sample){
        if(sample%32==0)phase=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(sample)*tau*m.frequency/a.rate,static_cast<long double>(tau))));
        if(sample<m.first){phase*=step;continue;}
        const auto n=static_cast<std::size_t>(sample-m.first);oscillator[n]=phase;
        const auto within=static_cast<double>((static_cast<long double>(sample)-m.origin)*m.rate);
        const auto values=pattern.shaped_values(0,within);
        for(unsigned bit=0;bit<2;++bit){const auto base=phase*values[bit];m.vectors[2*bit][n]=base.real();m.vectors[2*bit+1][n]=base.imag();
            const double c=phase.real(),s=phase.imag();m.raw_gram[bit].add({0,0,c*c,s*s,c*s,0},values[bit],1);}
        m.vectors[5][n]=a.interference*std::cos(static_cast<double>(tau*a.interference_frequency*sample/a.rate+.47L));phase*=step;
    }
    if(a.tail) {
        // Match the bank's original32-sample oscillator blocks and covariance
        // prefixes, including observations preceding a fractional symbol start.
        // Prefix subtraction also matters for affine_fit's singleton shortcut.
        std::array<CorrelationCarrierMoments,33> carrier_moments{};
        for(std::size_t n=1;n<carrier_moments.size();++n)
            carrier_moments[n]=correlation_carrier_moments(n,2*tau*m.frequency/a.rate);
        for(auto block_first=m.first-m.first%32;block_first<m.end;block_first+=32) {
            std::array<CorrelationProjection,33> prefix{};
            auto carrier=std::polar(1.,static_cast<double>(std::remainder(
                static_cast<long double>(block_first)*tau*m.frequency/a.rate,static_cast<long double>(tau))));
            for(std::size_t n=0;n<32;++n) {
                prefix[n+1]=prefix[n];prefix[n+1].cc+=carrier.real()*carrier.real();
                prefix[n+1].ss+=carrier.imag()*carrier.imag();prefix[n+1].cs+=carrier.real()*carrier.imag();
                carrier*=step;
            }
            const auto block_end=std::min(m.end,block_first+32);
            for(auto sample=std::max(m.first,block_first);sample<block_end;) {
                const auto within=std::max(0.L,(static_cast<long double>(sample)-m.origin)*m.rate);
                const auto local=static_cast<std::uint64_t>(std::floor(within/a.chip));
                const auto chip_end=m.origin+static_cast<long double>(local<32?(local+1)*a.chip:symbol)/m.rate;
                const auto boundary=std::min(static_cast<long double>(block_end),std::ceil(chip_end));
                const auto limit=static_cast<std::uint64_t>(std::max(static_cast<long double>(sample+1),boundary))-sample;
                const auto offset=(within-static_cast<long double>(local)*a.chip)/m.rate;
                const auto duration=static_cast<long double>(a.chip)/m.rate;
                auto piece=correlation_pulse_segment(offset,limit,duration);
                CorrelationPulseSegment final_piece;
                const bool has_final=local<=32&&32-local<=8;
                if(has_final) {
                    final_piece=correlation_pulse_segment(offset+static_cast<long double>(a.chip-a.tail)/(2*m.rate),limit,duration);
                    piece.count=std::min(piece.count,final_piece.count);
                }
                std::array<Complex,2> value{},slope{};
                std::array<long double,2> envelope_value{},envelope_slope{};
                for(std::size_t j=0;j<correlation_pulse_atoms;++j) {
                    const auto position=static_cast<std::int64_t>(local)+static_cast<std::int64_t>(j)-8;
                    if(position<0||position>32)continue;
                    auto pulse_value=piece.value[j],pulse_slope=piece.slope[j];
                    if(has_final&&position==32) {
                        const auto scale=std::sqrt(static_cast<double>(a.tail)/static_cast<double>(a.chip));
                        pulse_value=final_piece.value[j]*scale;pulse_slope=final_piece.slope[j]*scale;
                    }
                    if(pulse_value==0&&pulse_slope==0)continue;
                    const auto pair=pattern.values(static_cast<std::uint64_t>(position));
                    for(unsigned bit=0;bit<2;++bit) {
                        // Runtime rounds every atom before the ascending double
                        // sum. Retain that representation in the joint noise span.
                        value[bit]+=static_cast<double>(pulse_value)*pair[bit];
                        slope[bit]+=static_cast<double>(pulse_slope)*pair[bit];
                        envelope_value[bit]+=std::abs(pair[bit])*std::abs(pulse_value);
                        envelope_slope[bit]+=std::abs(pair[bit])*std::abs(pulse_slope);
                    }
                }
                const auto left=static_cast<std::size_t>(sample-block_first),right=left+piece.count;
                const auto projection=prefix[right]-prefix[left],first=prefix[left+1]-prefix[left];
                for(unsigned bit=0;bit<2;++bit)
                    add_fit(m.optimized_gram[bit],correlation_affine_fit(projection,{},piece.count,
                        value[bit],slope[bit],{first.cc-first.ss,2*first.cs},first.cc+first.ss,carrier_moments[piece.count]));
                ++m.segments;
                for(std::uint64_t n=0;n<piece.count;++n) {
                    const auto index=sample+n-m.first;
                    const std::complex<long double> phase{oscillator[index].real(),oscillator[index].imag()};
                    for(unsigned bit=0;bit<2;++bit) {
                        const std::complex<long double> v{value[bit].real(),value[bit].imag()},s{slope[bit].real(),slope[bit].imag()};
                        const auto base=phase*(v+static_cast<long double>(n)*s);
                        m.vectors[6+2*bit][index]=base.real()-m.vectors[2*bit][index];
                        m.vectors[7+2*bit][index]=base.imag()-m.vectors[2*bit+1][index];
                        const auto envelope=envelope_value[bit]+(static_cast<long double>(n)+128)*envelope_slope[bit];
                        m.moment_envelope_energy[bit]+=std::norm(phase)*envelope*envelope;
                    }
                }
                sample+=piece.count;
            }
        }
    } else {
    CorrelationPulseKernel kernel(a.chip,m.rate,m.frequency,a.rate);
    for(std::uint64_t cell_index=0;cell_index<32;++cell_index){
        const auto start=m.origin+static_cast<long double>(cell_index)*a.chip/m.rate;
        const auto begin=static_cast<std::uint64_t>(std::ceil(start));const auto end=static_cast<std::uint64_t>(std::ceil(start+a.chip/m.rate));
        std::array<std::array<Complex,17>,2> coefficients{};
        for(std::size_t j=0;j<17;++j){const auto index=static_cast<std::int64_t>(cell_index)+static_cast<std::int64_t>(j)-8;
            if(index<0||index>=32)continue;const auto value=pattern.values(static_cast<std::uint64_t>(index));for(unsigned bit=0;bit<2;++bit)coefficients[bit][j]=value[bit];}
        CorrelationPulseCell cell;cell.first=begin;cell.end=end;cell.count=end-begin;
        const auto carrier=oscillator[begin-m.first];cell.gram=kernel.evaluate(static_cast<long double>(begin)-start,cell.count,carrier*carrier,std::norm(carrier));
        for(unsigned bit=0;bit<2;++bit)add_fit(m.optimized_gram[bit],cell.fit(coefficients[bit]));
        for(auto sample=begin;sample<end;){
            const auto segment=correlation_pulse_segment(static_cast<long double>(sample)-start,end-sample,a.chip/m.rate);
            ++m.segments;
            std::array<std::complex<long double>,2> value{},slope{};
            for(unsigned bit=0;bit<2;++bit)for(std::size_t j=0;j<17;++j){
                const std::complex<long double> coefficient{coefficients[bit][j].real(),coefficients[bit][j].imag()};value[bit]+=coefficient*segment.value[j];slope[bit]+=coefficient*segment.slope[j];}
            for(std::uint64_t n=0;n<segment.count;++n){const auto index=sample+n-m.first;
                const std::complex<long double> carrier{oscillator[index].real(),oscillator[index].imag()};
                for(unsigned bit=0;bit<2;++bit){const auto base=carrier*(value[bit]+static_cast<long double>(n)*slope[bit]);
                    m.vectors[6+2*bit][index]=base.real()-m.vectors[2*bit][index];m.vectors[7+2*bit][index]=base.imag()-m.vectors[2*bit+1][index];
                    long double envelope=0;for(std::size_t j=0;j<17;++j)envelope+=std::abs(coefficients[bit][j])*(std::abs(segment.value[j])+(static_cast<long double>(n)+128)*std::abs(segment.slope[j]));
                    m.moment_envelope_energy[bit]+=std::norm(carrier)*envelope*envelope;}}
            sample+=segment.count;
        }
    }
    }
    // Twice-reorthogonalized span includes even the very small effective-basis
    // differences. A tiny difference is not made independent of the original
    // template/noise or silently rounded out of the Monte Carlo covariance.
    for(const auto& input:m.vectors){auto residual=input;const auto input_energy=dot(input,input);if(input_energy==0)continue;
        for(unsigned pass=0;pass<2;++pass)for(const auto& q:m.orthonormal){const auto projection=dot(residual,q);for(std::size_t n=0;n<residual.size();++n)residual[n]-=projection*q[n];}
        const auto residual_energy=dot(residual,residual);if(residual_energy<input_energy*1e-24L)continue;
        const auto normalization=std::sqrt(residual_energy);for(auto& value:residual)value/=normalization;m.orthonormal.push_back(std::move(residual));}
    for(std::size_t i=0;i<m.vectors.size();++i)for(const auto& q:m.orthonormal)m.coordinates[i].push_back(dot(m.vectors[i],q));
    for(std::size_t i=0;i<m.vectors.size();++i)for(std::size_t j=0;j<m.vectors.size();++j){
        const auto scale=std::sqrt(dot(m.vectors[i],m.vectors[i])*dot(m.vectors[j],m.vectors[j]));if(scale>0)
            m.covariance_error=std::max(m.covariance_error,static_cast<double>(std::abs(dot(m.vectors[i],m.vectors[j])-dot(m.coordinates[i],m.coordinates[j]))/scale));}
    check(m.covariance_error<1e-10,"joint statistic noise covariance did not preserve the complete basis span");
    m.signal_energy=dot(m.vectors[4],m.vectors[4]);m.interference_energy=dot(m.vectors[5],m.vectors[5]);m.signal_interference=dot(m.vectors[4],m.vectors[5]);
    for(std::size_t i=0;i<m.vectors.size();++i){m.signal_dots[i]=dot(m.vectors[i],m.vectors[4]);m.interference_dots[i]=dot(m.vectors[i],m.vectors[5]);}
    const auto threshold=-std::log(1e-10)+std::log(4.);
    for(unsigned bit=0;bit<2;++bit){
        const auto d0=dot(m.vectors[6+2*bit],m.vectors[6+2*bit]),d1=dot(m.vectors[7+2*bit],m.vectors[7+2*bit]);
        m.vector_error=std::max(m.vector_error,static_cast<double>(std::sqrt((d0+d1)/(m.raw_gram[bit].cc+m.raw_gram[bit].ss))));
        const auto& raw=m.raw_gram[bit];const auto& optimized=m.optimized_gram[bit];
        m.gram_error=std::max({m.gram_error,std::abs(raw.cc-optimized.cc)/std::max(1.,raw.cc),std::abs(raw.ss-optimized.ss)/std::max(1.,raw.ss),std::abs(raw.cs-optimized.cs)/std::max(1.,std::sqrt(raw.cc*raw.ss))});
        std::vector<long double> actual_c(m.count),actual_s(m.count);for(std::size_t i=0;i<m.count;++i){actual_c[i]=m.vectors[2*bit][i]+m.vectors[6+2*bit][i];actual_s[i]=m.vectors[2*bit+1][i]+m.vectors[7+2*bit][i];}
        const auto l00=std::sqrt(static_cast<long double>(optimized.cc)),l10=optimized.cs/l00,l11=std::sqrt(optimized.ss-l10*l10);
        const auto cc=dot(actual_c,actual_c),ss=dot(actual_s,actual_s),cs=dot(actual_c,actual_s);
        const auto aa=cc/(l00*l00),bb=(cs/l00-l10*aa)/l11,dd=(ss-2*l10*cs/l00+l10*l10*aa)/(l11*l11);
        const auto eigen=.5L*(aa+dd)+std::hypot(.5L*(aa-dd),bb);
        const auto minimum_eigenvalue=.5L*(optimized.cc+optimized.ss)-std::hypot(.5L*(optimized.cc-optimized.ss),static_cast<long double>(optimized.cs));
        check(minimum_eigenvalue>0,"singular optimized basis Gram");
        // Dot products are accumulated in long double; the outward margin
        // includes N-term accumulation and conditioning of the 2x2 whitening.
        const auto condition=(cc+ss)/minimum_eigenvalue;
        const auto allowance=4*gamma(4*m.count,std::numeric_limits<long double>::epsilon())*condition+
            gamma(256,std::numeric_limits<double>::epsilon())*condition;
        const auto outward=eigen+allowance;
        m.noise_eigenvalue_upper[bit]=std::nextafter(static_cast<double>(outward),std::numeric_limits<double>::infinity());
        const auto q=-std::expm1(-2*threshold/(m.count-2));
        m.false_accept_bound+=std::exp(.5*static_cast<double>(m.count-2)*std::log1p(-q/m.noise_eigenvalue_upper[bit]));
    }
    return m;
}
struct Noise {std::vector<long double> coefficients;long double energy=0;};
Noise noise(const Model& model,std::uint64_t seed){
    std::mt19937_64 rng(seed);std::normal_distribution<double> normal(0,1);Noise result;
    for(std::size_t i=0;i<model.orthonormal.size();++i){const auto z=normal(rng);result.coefficients.push_back(z);result.energy+=z*z;}
    std::gamma_distribution<double> residual(.5*static_cast<double>(model.count-model.orthonormal.size()),2.);result.energy+=residual(rng);return result;
}
std::array<CorrelationFit,2> fits(const Model& model,const Noise& draw,double cn0,bool optimized){
    const auto amplitude=std::sqrt(std::pow(10.L,cn0/10)/(nominal_signal_power*model.config.sample_rate/2));
    const auto sn=dot(model.coordinates[4],draw.coefficients),in=dot(model.coordinates[5],draw.coefficients);
    const auto energy=amplitude*amplitude*model.signal_energy+2*amplitude*(model.signal_interference+sn)+model.interference_energy+2*in+draw.energy;
    auto result=optimized?model.optimized_gram:model.raw_gram;
    for(unsigned bit=0;bit<2;++bit){
        std::array<long double,2> dots{};
        for(unsigned dimension=0;dimension<2;++dimension){const auto column=2*bit+dimension;
            dots[dimension]=amplitude*model.signal_dots[column]+model.interference_dots[column]+dot(model.coordinates[column],draw.coefficients);
            if(optimized){const auto difference=6+column;dots[dimension]+=amplitude*model.signal_dots[difference]+model.interference_dots[difference]+dot(model.coordinates[difference],draw.coefficients);}}
        result[bit].xc=static_cast<double>(dots[0]);result[bit].xs=static_cast<double>(dots[1]);result[bit].energy=static_cast<double>(energy);result[bit].count=model.count;
    }
    return result;
}
bool accepted(const Model& model,const Noise& draw,double cn0,bool optimized,unsigned bit){
    const auto fitted=fits(model,draw,cn0,optimized);const auto correct=fitted[bit].score(),alternative=fitted[1-bit].score();
    return correct>=-std::log(1e-10)+std::log(4.)&&correct-alternative>=1;
}
struct Root {double value=0;bool bracketed=false,monotone=true;std::string curve;};
using Polynomial=std::array<long double,3>;
long double evaluate(const Polynomial& p,long double amplitude){return p[0]+amplitude*(p[1]+amplitude*p[2]);}
std::array<long double,2> extrema(const Polynomial& p,long double lo,long double hi){
    auto minimum=std::min(evaluate(p,lo),evaluate(p,hi)),maximum=std::max(evaluate(p,lo),evaluate(p,hi));
    if(p[2]!=0){const auto vertex=-p[1]/(2*p[2]);if(vertex>lo&&vertex<hi){const auto value=evaluate(p,vertex);minimum=std::min(minimum,value);maximum=std::max(maximum,value);}}
    return {minimum,maximum};
}
long double amplitude(const Model& model,double cn0){return std::sqrt(std::pow(10.L,cn0/10)/(nominal_signal_power*model.config.sample_rate/2));}
double cn0(const Model& model,long double value){return static_cast<double>(10*std::log10(value*value*nominal_signal_power*model.config.sample_rate/2));}
std::vector<long double> polynomial_roots(const Polynomial& p){
    if(p[2]==0)return p[1]==0?std::vector<long double>{}:std::vector<long double>{-p[0]/p[1]};
    const auto discriminant=p[1]*p[1]-4*p[2]*p[0];if(discriminant<0)return {};
    const auto q=-.5L*(p[1]+std::copysign(std::sqrt(discriminant),p[1]));
    if(q==0)return {-p[1]/(2*p[2])};return {q/p[2],p[0]/q};
}
struct AdmissionPolynomials {Polynomial energy;std::array<Polynomial,2> explained,margin;long double q=0,r=0;};
AdmissionPolynomials polynomials(const Model& model,const Noise& draw,bool optimized,unsigned bit){
    AdmissionPolynomials result;result.energy={model.interference_energy+2*dot(model.coordinates[5],draw.coefficients)+draw.energy,
        2*(model.signal_interference+dot(model.coordinates[4],draw.coefficients)),model.signal_energy};
    const auto& grams=optimized?model.optimized_gram:model.raw_gram;
    for(unsigned b=0;b<2;++b){std::array<long double,2> constant{},linear{};
        for(unsigned d=0;d<2;++d){const auto column=2*b+d;linear[d]=model.signal_dots[column];constant[d]=model.interference_dots[column]+dot(model.coordinates[column],draw.coefficients);
            if(optimized){linear[d]+=model.signal_dots[6+column];constant[d]+=model.interference_dots[6+column]+dot(model.coordinates[6+column],draw.coefficients);}}
        const auto& g=grams[b];const auto determinant=g.cc*g.ss-g.cs*g.cs;
        check(determinant>1e-12*std::max(1.,g.cc*g.ss),"statistic polynomial fit hits determinant guard");
        const auto product=[&](const std::array<long double,2>& x,const std::array<long double,2>& y){return (g.ss*x[0]*y[0]+g.cc*x[1]*y[1]-g.cs*(x[0]*y[1]+x[1]*y[0]))/determinant;};
        result.explained[b]={product(constant,constant),2*product(constant,linear),product(linear,linear)};
    }
    result.q=-std::expm1(-2*(-std::log(1e-10)+std::log(4.))/static_cast<double>(model.count-2));
    const auto delta=-std::expm1(-2.L/(model.count-2));result.r=1-delta;
    for(unsigned i=0;i<3;++i){result.margin[0][i]=result.explained[bit][i]-result.q*result.energy[i];
        result.margin[1][i]=(result.explained[bit][i]-result.explained[1-bit][i])+delta*(result.explained[1-bit][i]-result.energy[i]);}
    return result;
}
// Exact continuous coherent admission has two quadratic inequalities in the
// positive signal amplitude. Inspect every root-separated interval; a finite
// dB grid alone could miss a narrow acceptance island or downward excursion.
Root polynomial_crossing(const Args& a,const Model& model,const AdmissionPolynomials& p,
                         std::array<long double,2> shift={}){
    Root result;auto margins=p.margin;for(unsigned i=0;i<2;++i)margins[i][0]+=shift[i];
    const auto minimum=amplitude(model,a.minimum),maximum=amplitude(model,a.maximum);
    std::vector<long double> points{minimum,maximum};
    for(const auto& margin:margins)for(const auto root:polynomial_roots(margin))if(root>minimum&&root<maximum)points.push_back(root);
    std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());
    const auto success=[&](long double value,bool root_point=false){
        for(const auto& margin:margins){const auto allowance=root_point?256*std::numeric_limits<long double>::epsilon()*(std::abs(margin[0])+value*(std::abs(margin[1])+value*std::abs(margin[2]))):0;
            if(evaluate(margin,value)<-allowance)return false;}return true;};
    bool previous=success(minimum);unsigned transitions=0;long double crossing=0;
    const auto observe=[&](bool current,long double location){
        if(current&&!previous){++transitions;if(transitions==1)crossing=location;}
        if(!current&&previous)result.monotone=false;previous=current;};
    for(std::size_t i=0;i+1<points.size();++i){observe(success(.5L*(points[i]+points[i+1])),points[i]);
        observe(success(points[i+1],i+2<points.size()),points[i+1]);}
    const auto last=success(maximum);
    result.bracketed=!success(minimum)&&last&&transitions==1;
    if(result.bracketed)result.value=cn0(model,crossing);
    if(!result.monotone){std::ostringstream description;for(std::size_t i=0;i+1<points.size();++i)description<<"interval["<<cn0(model,points[i])<<':'<<cn0(model,points[i+1])<<"]="<<success(.5L*(points[i]+points[i+1]))<<';';result.curve=description.str();}
    return result;
}
Root crossing(const Args& a,const Model& model,const Noise& draw,bool optimized){
    const auto p=polynomials(model,draw,optimized,a.bit);auto out=polynomial_crossing(a,model,p);
    const auto retain_curve=[&]{if(!out.monotone){std::ostringstream curve;curve<<out.curve;for(auto value=a.minimum;value<a.maximum;value+=.025)curve<<value<<':'<<accepted(model,draw,value,optimized,a.bit)<<';';curve<<a.maximum<<':'<<accepted(model,draw,a.maximum,optimized,a.bit);out.curve=curve.str();}};
    if(!out.bracketed||accepted(model,draw,a.minimum,optimized,a.bit)||!accepted(model,draw,a.maximum,optimized,a.bit)){out.bracketed=false;retain_curve();return out;}
    double lower=a.minimum,upper=a.maximum;
    while(upper-lower>a.root_tolerance){const auto middle=.5*(upper+lower);if(accepted(model,draw,middle,optimized,a.bit))upper=middle;else lower=middle;}
    out.value=.5*(lower+upper);
    retain_curve();
    return out;
}
double numerical_root_enclosure(const Args& args,const Model& model,const Noise& draw,bool optimized,const Root& root){
    check(root.bracketed&&root.monotone,"cannot enclose an unqualified statistic crossing");
    Args window=args;window.minimum=args.minimum;window.maximum=std::min(args.maximum,root.value+.05);
    const auto p=polynomials(model,draw,optimized,args.bit);
    const auto lo=amplitude(model,window.minimum),hi=amplitude(model,window.maximum);
    const auto energy_range=extrema(p.energy,lo,hi);const auto minimum_energy=energy_range[0],maximum_energy=energy_range[1];
    check(minimum_energy>1e-30L,"received energy guard is active in numerical root enclosure");
    const auto norm=std::sqrt(maximum_energy);
    const auto pcm_error=std::numeric_limits<float>::epsilon()*.5L*norm+
        .5L*std::sqrt(static_cast<long double>(model.count))*std::numeric_limits<float>::denorm_min();
    // Conservatively allow all real operations in raw accumulation or the
    // atom/pair moment contractions, using absolute intermediate pulse bounds
    // rather than the possibly cancelled final template norm. This is an IEEE
    // numerical allowance, not a proof about arbitrary libm implementations.
    const auto operations=128.L*(model.count+(optimized?153.L*model.segments:0));
    const auto roundoff=gamma(operations,std::numeric_limits<double>::epsilon());
    const auto energy_error=2*norm*pcm_error+pcm_error*pcm_error+roundoff*(norm+pcm_error)*(norm+pcm_error);
    std::array<long double,2> explained_error{};
    const auto& grams=optimized?model.optimized_gram:model.raw_gram;
    for(unsigned bit=0;bit<2;++bit){const auto& g=grams[bit];const auto minimum_eigenvalue=.5L*(g.cc+g.ss)-std::hypot(.5L*(g.cc-g.ss),static_cast<long double>(g.cs));
        check(minimum_eigenvalue>0,"singular numerical bound Gram");
        const auto envelope=optimized?model.moment_envelope_energy[bit]:static_cast<long double>(g.cc+g.ss);
        const auto relative_gram_error=roundoff*envelope/minimum_eigenvalue;check(relative_gram_error<.01L,"numerical Gram allowance is inconclusive");
        const auto lambda=optimized?model.noise_eigenvalue_upper[bit]:1+relative_gram_error;
        const auto dot_error=std::sqrt(lambda)*pcm_error+roundoff*std::sqrt(envelope/minimum_eigenvalue)*(norm+pcm_error);
        const auto maximum_explained=std::max(evaluate(p.explained[bit],lo),evaluate(p.explained[bit],hi));
        check(maximum_explained>=0&&maximum_explained/minimum_energy<1-1e-12L,"score clamp is active in numerical root enclosure");
        explained_error[bit]=(2*std::sqrt(maximum_explained)*dot_error+dot_error*dot_error+relative_gram_error*maximum_explained)/(1-relative_gram_error);
        check(minimum_energy>energy_error&&(maximum_explained+explained_error[bit])/(minimum_energy-energy_error)<1-1e-15L,
              "perturbed score guard/clamp allowance is inconclusive");
    }
    // Include threshold/expm1/log and scalar score arithmetic with a further
    // outward allowance; validate the actual float receiver at selected seeds.
    const auto scalar_allowance=512*std::numeric_limits<double>::epsilon()*maximum_energy;
    std::array<long double,2> margin_error{explained_error[args.bit]+p.q*energy_error+scalar_allowance,
        explained_error[args.bit]+p.r*explained_error[1-args.bit]+(1-p.r)*energy_error+scalar_allowance};
    const auto earliest=polynomial_crossing(window,model,p,margin_error);
    for(auto& value:margin_error)value=-value;
    const auto latest=polynomial_crossing(window,model,p,margin_error);
    check(earliest.bracketed&&earliest.monotone&&latest.bracketed&&latest.monotone,"numerical root enclosure is inconclusive");
    return std::max(root.value-earliest.value,latest.value-root.value)+args.root_tolerance;
}
double verify_pcm(const Args& a,const Model& model,const Noise& draw,double cn0,std::uint64_t seed){
    // Realize precisely the same joint draw as a real PCM vector. Generate an
    // independent Gaussian residual in the orthogonal complement and normalize
    // its length to the already drawn residual chi-square energy.
    std::mt19937_64 rng(seed^0xf7037ed1a0b428dbULL);std::normal_distribution<double> normal(0,1);
    std::vector<long double> residual(model.count);for(auto& v:residual)v=normal(rng);
    for(unsigned pass=0;pass<2;++pass)for(const auto& q:model.orthonormal){const auto projection=dot(residual,q);for(std::size_t n=0;n<residual.size();++n)residual[n]-=projection*q[n];}
    const auto remaining=draw.energy-dot(draw.coefficients,draw.coefficients),scale=std::sqrt(std::max(0.L,remaining)/dot(residual,residual));
    const auto amplitude=std::sqrt(std::pow(10.L,cn0/10)/(nominal_signal_power*model.config.sample_rate/2));
    std::vector<float> pcm(static_cast<std::size_t>(model.end));
    for(std::size_t n=0;n<model.count;++n){auto value=scale*residual[n]+amplitude*model.vectors[4][n]+model.vectors[5][n];
        for(std::size_t j=0;j<model.orthonormal.size();++j)value+=model.orthonormal[j][n]*draw.coefficients[j];pcm[model.first+n]=static_cast<float>(value);}
    double error=0;
    std::array<PatternEvidence,2> paired_candidates{};
    for(bool optimized:{false,true}){
        PatternCorrelator receiver(model.config,paired_search(a,model),8*1024*1024,PatternCorrelatorOptions{!optimized,false});
        for(std::size_t pos=0;pos<pcm.size();pos+=2048) {
            const auto count=std::min<std::size_t>(2048,pcm.size()-pos);
            receiver.push(std::span(pcm).subspan(pos,count));
            if(pos+count<pcm.size()) {
                check(receiver.candidates().empty(),"actual PCM receiver scored an incomplete symbol");
                check(receiver.take_bursts().empty(),"actual PCM receiver published a bit before its physical endpoint");
            }
        }
        const auto candidates=receiver.candidates();check(candidates.size()==1,"actual PCM statistic replay did not retain one complete candidate");
        paired_candidates[optimized]=candidates.front();
        check(candidates.front().first_sample==model.first&&candidates.front().end_sample==model.end&&candidates.front().stream_symbol==0,
              "actual PCM and statistic reconstruction changed the symbol endpoints");
        check(receiver.initial_search_complete(),"actual PCM detector did not finish scoring the complete symbol");
        const auto fitted=fits(model,draw,cn0,optimized);const auto zero=fitted[0].score(),one=fitted[1].score(),expected=std::max(zero,one),alternative=std::min(zero,one);
        error=std::max(error,std::abs(candidates.front().score-expected)/std::max(1.,expected));
        error=std::max(error,std::abs(candidates.front().alternative_score-alternative)/std::max(1.,alternative));
        check(candidates.front().bit==(one>zero?1U:0U),"actual PCM and statistic reconstruction selected different private bit patterns");
        check(std::abs(candidates.front().admission_threshold-(-std::log(1e-10)+std::log(4.)))<1e-12,"actual PCM and statistic admission thresholds differ");
        auto events=receiver.take_bursts();check(events.size()<=1,"actual PCM receiver duplicated a pending bit");
        bool correct=events.size()==1&&events.front().bits==Bytes{static_cast<std::uint8_t>(a.bit)};
        const auto winner=one>zero?1U:0U;
        check((events.size()==1)==accepted(model,draw,cn0,optimized,winner),"actual PCM and reconstructed winner admission differ");
        for(const auto& event:events) {
            check(event.bits==Bytes{static_cast<std::uint8_t>(winner)}&&event.first_sample==model.first&&event.end_sample==model.end&&
                  event.first_stream_symbol==0&&event.stream_first_sample==model.first&&event.stream_first_symbol==0,
                  "actual PCM receiver changed pending-bit identity or endpoints");
            check(!event.complete,"actual PCM receiver completed without physical absence");
        }
        // Away from a crossing, quantized PCM must preserve the complete actual
        // receiver admission decision as well as its paired score.
        check(correct==accepted(model,draw,cn0,optimized,a.bit),"actual PCM and statistic reconstruction changed admission away from threshold");
        check(receiver.work().drift_sections==1&&!receiver.work().differential_window_samples&&receiver.work().hypotheses==1,"statistic and actual PCM detector/search coverage differ");
        check(receiver.work().backend==(optimized?optimized_backend(a):PatternCorrelationBackend::raw),"actual PCM receiver selected an unexpected backend");
        if(optimized&&a.tail)check(receiver.work().segments==model.segments,"actual PCM receiver and statistic representation split different affine spans");
        receiver.finish();for(const auto& event:receiver.take_bursts())check(!event.complete,"capture EOF substituted for physical absence");
    }
    check(paired_candidates[0].first_sample==paired_candidates[1].first_sample&&paired_candidates[0].end_sample==paired_candidates[1].end_sample,
          "paired actual PCM receivers changed symbol endpoints");
    check(error<1e-6,"actual float PCM and reconstructed statistic score differ materially");return error;
}
void csv_value(std::ostream& out,const std::string& text){out<<'"';for(const auto c:text){if(c=='"')out<<'"';out<<c;}out<<'"';}
}
int main(int argc,char** argv){try{
    const auto a=arguments(argc,argv);const auto model=prepare(a);std::ofstream file;std::ostream* out=&std::cout;
    if(!a.csv.empty()){file.open(a.csv);check(bool(file),"cannot open statistics CSV");out=&file;}*out<<std::setprecision(17);
    *out<<"mode,case,variant,backend,carrier_hz,bandwidth_hz,sample_rate,chip_samples,symbol_samples,symbol_seconds,workspace_bytes,keys,epochs,frequency_offset_hz,clock_ppm,phase_diffusion,start_uncertainty_seconds,noise_only,seed,repeat,input_cn0_db_hz,instrumented,hypotheses,phase_groups,drift_sections,differential_window_samples,samples,capture_begin_sample,pcm_samples_identical,threshold_cn0_db_hz,root_error_db,bracketed,monotone,maximum_template_relative_error,maximum_gram_relative_error,joint_noise_covariance_error,awgn_false_accept_union_bound,actual_pcm_maximum_score_relative_error,noise_span_rank,interference_amplitude,interference_frequency,waveform_seed,actual_pcm_verified_pairs,actual_pcm_verified_seed,numerical_root_error_bound_db,numerical_enclosure_valid,numerical_enclosure_method,numerical_enclosure_scope,nonmonotone_curve,tail_samples,represented_affine_spans,automatic_constructor_workspace_bytes\n";
    unsigned invalid=0,verified_pairs=0;double pcm_error=0;
    for(unsigned trial=0;trial<a.trials;++trial){const auto seed=a.seed+104729ULL*trial;const auto draw=noise(model,seed);
        auto raw=crossing(a,model,draw,false),optimized=crossing(a,model,draw,true);
        if(!raw.bracketed||!optimized.bracketed||!raw.monotone||!optimized.monotone)++invalid;
        std::array<double,2> numerical_bounds{};std::array<bool,2> numerical_valid{};
        for(unsigned variant=0;variant<2;++variant){auto& root=variant?optimized:raw;if(!root.bracketed||!root.monotone)continue;
            try{numerical_bounds[variant]=numerical_root_enclosure(a,model,draw,variant,root);numerical_valid[variant]=true;}
            catch(const std::exception& error){++invalid;root.curve+="numerical-enclosure-error:"+std::string(error.what());}}
        const bool verified=trial<a.verify&&raw.bracketed&&optimized.bracketed&&numerical_valid[0]&&numerical_valid[1];
        if(verified){const auto center=.5*(raw.value+optimized.value);pcm_error=std::max({pcm_error,verify_pcm(a,model,draw,center-.05,seed),verify_pcm(a,model,draw,center+.05,seed)});verified_pairs+=2;}
        for(bool current:{false,true}){const auto& root=current?optimized:raw;
            *out<<"statistics_crossing,statistics-bit"<<a.bit<<','<<(current?(a.tail?"automatic,pulse_segments":"automatic,pulse_moments"):"raw_reference,raw")<<','
                <<a.carrier<<','<<model.config.bandwidth_hz<<','<<a.rate<<','<<a.chip<<','<<symbol_sample_count(model.config)<<','
                <<static_cast<double>(symbol_sample_count(model.config))/a.rate<<",8388608,1,1,"<<a.offset<<','<<a.ppm<<','<<a.diffusion
                <<",0,0,"<<seed<<','<<trial<<",,0,"<<model.work.hypotheses<<','<<model.work.phase_groups<<','<<model.work.drift_sections<<','
                <<model.work.differential_window_samples<<','<<model.count<<','<<model.first<<",1,";
            if(root.bracketed)*out<<root.value;
            *out<<','<<a.root_tolerance<<','<<root.bracketed<<','<<root.monotone<<','<<model.vector_error<<','<<model.gram_error<<','
                <<model.covariance_error<<','<<model.false_accept_bound<<','<<pcm_error<<','<<model.orthonormal.size()<<','<<a.interference<<','<<a.interference_frequency<<','<<a.seed<<','<<verified_pairs<<','<<verified<<',';
            if(numerical_valid[current])*out<<numerical_bounds[current];
            *out<<','<<numerical_valid[current]<<",ieee_roundoff_estimate,entire_lower_bracket,";
            csv_value(*out,root.curve);*out<<','<<a.tail<<','<<model.segments<<','<<model.work.peak_workspace_bytes<<'\n';}
        if((trial+1)%1024==0){out->flush();std::cerr<<"joint-statistic paired seeds completed "<<trial+1<<'/'<<a.trials<<'\n';}
    }
    out->flush();check(bool(*out),"statistics CSV write failed");
    std::cerr<<"paired statistics completed; numerical enclosure uses a conservative IEEE roundoff estimate, not an arbitrary-libm interval proof; invalid/nonmonotone="<<invalid<<" vector_relative_error="<<model.vector_error
        <<" gram_relative_error="<<model.gram_error<<" covariance_relative_error="<<model.covariance_error<<" actual_pcm_relative_score_error="<<pcm_error<<'\n';
    return invalid?2:0;
}catch(const std::exception& error){std::cerr<<"pulse statistic experiment failed: "<<error.what()<<'\n';return 1;}}
