#include "datapump/channel.hpp"
#include "datapump/stream_codec.hpp"
#include "datapump/transfer.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace datapump;
namespace m=datapump::modem;
namespace {
constexpr double tau=2*std::numbers::pi;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Function>void rejects(Function function,const char* message){
    bool rejected=false;try{function();}catch(const Error&){rejected=true;}require(rejected,message);
}
m::ChannelConfig ideal(){m::ChannelConfig value;value.clock_error_ppm=0;value.phase_noise_degrees_per_sqrt_second=0;value.snr_db=300;return value;}
m::Config config(){m::Config value;value.sample_rate=8000;value.carrier_hz=1500;return value;}
std::complex<double> integral(double frequency,std::uint64_t begin,std::uint64_t count,std::uint32_t rate){
    const auto half=tau*frequency*static_cast<double>(count)/rate/2;
    return (std::abs(half)<1e-12?1:std::sin(half)/half)*std::polar(1.,tau*frequency*(static_cast<double>(begin)+static_cast<double>(count)/2)/rate);
}
void clock_and_carrier(){
    const auto cfg=config();auto impairment=ideal();impairment.clock_error_ppm=100;impairment.frequency_offset_hz=.07;
    m::SimulationChannel channel(cfg,impairment);std::uint64_t received=0;
    for(unsigned i=0;i<37;++i){
        const auto output=channel.process({{.7,.2},3217});require(output.has_value(),"nonempty clock interval dropped");
        const auto expected=std::complex<double>{.7,.2}*integral(.22,received,output->sample_count,cfg.sample_rate);
        require(std::abs(output->value-expected)<1e-10,"carrier drift was not integrated over elapsed receiver time");
        received+=output->sample_count;
    }
    require(received==static_cast<std::uint64_t>(std::ceil(37.L*3217/1.0001L)),"clock timing drift did not accumulate across bins");
    require(channel.received_samples()==received,"receiver clock count mismatch");
    require(channel.working_bytes()<=m::SimulationChannel::workspace_bound,"channel memory accounting exceeded its reservation");
    for(const auto ppm:{-100.,100.}){
        auto model=ideal();model.clock_error_ppm=ppm;m::SimulationChannel single(cfg,model),chunks(cfg,model);
        const auto combined=single.process({{1,0},100000});std::uint64_t count=0;unsigned skipped=0;
        for(unsigned i=0;i<100000;++i){if(const auto part=chunks.process({{1,0},1}))count+=part->sample_count;else ++skipped;}
        require(count==combined->sample_count,"clock conversion rounded each source bin independently");
        require(ppm<0 || skipped>0,"faster source clock did not occasionally skip a receiver sample");
    }
}
void long_coherence(){
    const auto cfg=config();auto clean=ideal(),bad=ideal();bad.clock_error_ppm=100;
    m::SimulationChannel reference(cfg,clean),channel(cfg,bad);
    const auto before=std::chrono::steady_clock::now();
    const auto samples=static_cast<std::uint64_t>(cfg.sample_rate)*60*60*3;
    const auto expected=reference.process({{.7,0},samples}),actual=channel.process({{.7,0},samples});
    require(std::abs(expected->value-.7)<1e-10,"ideal long coherent integration lost signal");
    require(std::abs(actual->value)<.001,"three-hour100ppm integration retained impossible carrier coherence");
    require(std::chrono::steady_clock::now()-before<std::chrono::seconds(1),"long simulation iterated through samples or carrier cycles");
}
void phase_diffusion(){
    const auto cfg=config();auto impairment=ideal();impairment.phase_noise_degrees_per_sqrt_second=4;
    double first_square=0,last_square=0,cross=0,mean=0;
    constexpr unsigned trials=512;
    for(unsigned seed=0;seed<trials;++seed){
        impairment.seed=seed;m::SimulationChannel channel(cfg,impairment);
        channel.process({{1,0},cfg.sample_rate});const auto first=channel.phase_noise_radians();
        channel.process({{1,0},3*cfg.sample_rate});const auto last=channel.phase_noise_radians();
        first_square+=first*first;last_square+=last*last;cross+=first*last;mean+=last;
    }
    const auto variance=std::pow(4*std::numbers::pi/180,2);
    require(std::abs(first_square/trials/variance-1)<.2,"one-second phase diffusion variance");
    require(std::abs(last_square/trials/(4*variance)-1)<.2,"phase diffusion did not grow with elapsed time");
    require(std::abs(cross/trials/variance-1)<.25,"phase errors lost temporal correlation");
    require(std::abs(mean/trials)<std::sqrt(variance)*.2,"phase diffusion has unexpected bias");
}
void pcm_clock(){
    const auto cfg=config();auto impairment=ideal();impairment.clock_error_ppm=100;
    m::SampledSimulationChannel timing(cfg,impairment);
    const auto startup=timing.startup_offset_samples(),phase=timing.carrier_phase_radians();
    std::vector<float> source(cfg.sample_rate*2);
    for(std::size_t i=0;i<source.size();++i)source[i]=static_cast<float>(.7*std::cos(tau*cfg.carrier_hz*static_cast<double>(i)/cfg.sample_rate));
    const auto output=m::simulate(source,cfg,impairment);
    require(output.size()==static_cast<std::size_t>(std::ceil(startup+source.size()/1.0001L)),"PCM clock drift and free-running startup did not change capture duration");
    double error=0;std::complex<double> received{};
    const auto begin=static_cast<std::size_t>(std::ceil(startup))+32,length=output.size()-32-begin;
    for(std::size_t i=begin;i+32<output.size();++i){
        const auto expected=.7*std::cos(phase+tau*cfg.carrier_hz*1.0001*static_cast<double>(i)/cfg.sample_rate);
        error+=std::pow(output[i]-expected,2);
        received+=2.*output[i]*std::polar(1.,-tau*cfg.carrier_hz*static_cast<double>(i)/cfg.sample_rate);
    }
    require(std::sqrt(error/static_cast<double>(length))<.002,"PCM interpolation lost crystal-shifted carrier");
    const auto expectation=.7*std::polar(1.,phase)*integral(.15,begin,length,cfg.sample_rate);
    require(std::abs(received/static_cast<double>(length)-expectation)<.002,"PCM and accelerated carrier integrations disagree");
    impairment.phase_noise_degrees_per_sqrt_second=4;
    const auto noisy=m::simulate(source,cfg,impairment);
    require(noisy==m::simulate(source,cfg,impairment),"PCM phase diffusion is not reproducible");
    require(noisy!=output,"PCM phase diffusion did not affect the carrier");
}
std::vector<float> capture(m::SampledSimulationChannel& channel,m::StreamingTransmitter& source,std::size_t block){
    std::vector<float> result,buffer(block);
    while(const auto count=channel.read(source,buffer))result.insert(result.end(),buffer.begin(),buffer.begin()+static_cast<std::ptrdiff_t>(count));
    return result;
}
void analytic_source_matches_hardware_pcm(){
    auto cfg=config();cfg.spreading_factor=4;
    cfg.scramble=true;cfg.dsss=true;cfg.spreading_seed[0]=43;cfg.dsss_seed[0]=97;
    auto wire=m::preamble(cfg);for(unsigned i=0;i<129;++i)wire.push_back(static_cast<std::uint8_t>(i*37));
    m::StreamingTransmitter hardware(wire,cfg),analytic(wire,cfg);
    std::array<float,797> real{};std::array<std::complex<double>,797> complex{};
    std::size_t iteration=0;
    while(!hardware.finished()){
        const auto block=++iteration%3?real.size():std::size_t{19};
        const auto count=hardware.read(std::span(real).first(block));
        require(analytic.read_analytic(std::span(complex).first(block))==count,"analytic source changed waveform duration");
        for(std::size_t i=0;i<count;++i)
            require(real[i]==static_cast<float>(complex[i].real()),"simulation analytic source disagreed with hardware PCM across training or keyed spreading");
    }
    require(analytic.finished(),"analytic source retained an unsent waveform tail");
}
void sampled_startup_and_carrier(){
    auto cfg=config();cfg.integration_seconds=.4;
    double previous_start=0,previous_phase=0;
    for(unsigned seed=1;seed<=8;++seed){
        auto model=ideal();model.seed=seed;model.clock_error_ppm=seed%2?100:-10000;model.frequency_offset_hz=.37;
        m::SampledSimulationChannel channel(cfg,model);
        const auto start=channel.startup_offset_samples(),phase=channel.carrier_phase_radians();
        require(start>=.05*cfg.sample_rate && start<=.3*cfg.sample_rate+1,"sampled startup was not independent of waveform timing");
        require(std::abs(start-std::round(start))>=.049,"sampled startup was rounded to the receiver clock");
        require(start!=previous_start && phase!=previous_phase,"different seeds retained shared start timing or phase");
        previous_start=start;previous_phase=phase;
        m::StreamingTransmitter source(m::RawBits{{0,0,0,0}},cfg);
        const auto output=capture(channel,source,137);
        const auto rate=1+model.clock_error_ppm*1e-6;
        require(output.size()==static_cast<std::size_t>(std::ceil(start+source.total_samples()/static_cast<long double>(rate))),"sampled duration was rounded per source chunk");
        require(std::all_of(output.begin(),output.end(),[](float value){return std::isfinite(value);}),"sampled private prefix and pattern PCM must stay finite");
        require(channel.received_samples()==output.size(),"sampled receiver time did not match PCM count");
        require(channel.transmitted_samples()==source.total_samples(),"sampled transmitter progress did not reach the received source endpoint");
        require(channel.working_bytes()<=m::SampledSimulationChannel::workspace_bound,"sampled channel exceeded its fixed workspace");
    }
}
void sampled_chunking_and_idle(){
    auto cfg=config();cfg.integration_seconds=.02;
    auto model=ideal();model.snr_db=14;model.phase_noise_degrees_per_sqrt_second=8;model.clock_error_ppm=100;model.delay_samples=73;
    m::SampledSimulationChannel whole(cfg,model),chunked(cfg,model);
    auto bits=m::RawBits{{1,0,0,0,1,1,0,1,0,1,0,0,1,1,1,0}};
    m::StreamingTransmitter a(bits,cfg),b(bits,cfg);
    require(capture(whole,a,4096)==capture(chunked,b,1),"sampled channel randomness or resampling depended on read block size");
    std::array<float,997> first{},second{};
    const auto before=whole.received_samples();
    whole.read_noise(first);
    chunked.read_noise(std::span(second).first(13));chunked.read_noise(std::span(second).subspan(13,573));chunked.read_noise(std::span(second).subspan(586));
    require(first==second,"idle noise depended on block size or restarted its generator");
    require(whole.received_samples()==before+first.size(),"idle noise did not advance receiver time");
    require(whole.carrier_phase_radians()==chunked.carrier_phase_radians(),"idle phase evolution depended on block size");
    const auto phase=whole.carrier_phase_radians(),startup=whole.startup_offset_samples();
    whole.begin_burst();chunked.begin_burst();
    require(whole.carrier_phase_radians()==phase && whole.received_samples()==before+first.size(),"new burst reset receiver time or oscillator");
    require(whole.startup_offset_samples()!=startup,"new burst reused its synchronized start boundary");
    // Replacing a transmitter at the same address must still start a new
    // explicitly prepared burst, without resetting the physical channel.
    a=m::StreamingTransmitter(bits,cfg);b=m::StreamingTransmitter(bits,cfg);
    require(capture(whole,a,3)==capture(chunked,b,4096),"later burst acquired source identity or chunk synchronization");
    std::stop_source stop;stop.request_stop();
    rejects([&]{whole.read_noise(first,stop.get_token());},"cancelled sampled idle read was accepted");
}
void stream_and_preview(){
    transfer::Options options;options.timestamp=1800000000;options.search_seconds=0;
    options.modem=config();options.modem.sample_rate=9600;options.modem.bandwidth_hz=2400;
    options.modem.carrier_hz=1800;options.modem.spreading_factor=64;
    Message message;message.kind=MessageKind::file;message.filename="channel.bin";
    message.local_id[0]=83;message.data=Bytes(16,0x5c);
    auto source=transfer::message_transmitter(message,options),control=transfer::message_transmitter(message,options);
    m::ChannelConfig model;model.snr_db=35;model.seed=517;
    m::SampledSimulationChannel channel(options.modem,model),without_plots(options.modem,model);
    std::vector<float> samples;std::array<float,1024> a{},b{},preview{};
    while(const auto count=channel.read(*source,a)) {
        require(without_plots.read(*control,b)==count,"plotting changed channel timing");
        require(std::equal(a.begin(),a.begin()+static_cast<std::ptrdiff_t>(count),b.begin()),"plotting changed channel waveform or noise");
        samples.insert(samples.end(),a.begin(),a.begin()+static_cast<std::ptrdiff_t>(count));
        source->preview_last(preview);
        require(std::all_of(preview.begin(),preview.end(),[](float value){return std::isfinite(value);}),"nonfinite simulation preview");
    }
    auto remaining=m::pattern_absence_samples(options.modem)+options.modem.sample_rate+
                   2*m::symbol_sample_count(options.modem);
    while(remaining) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(remaining,a.size()));
        channel.read_noise(std::span(a).first(count));without_plots.read_noise(std::span(b).first(count));
        require(std::equal(a.begin(),a.begin()+static_cast<std::ptrdiff_t>(count),b.begin()),
                "plotting changed post-transmission capture noise");
        samples.insert(samples.end(),a.begin(),a.begin()+static_cast<std::ptrdiff_t>(count));remaining-=count;
    }
    const auto decoded=transfer::receive(samples,options);
    require(decoded.stream_complete && decoded.content_validated && decoded.content.message.data==message.data,
            "ordinary 100 ppm pattern stream changed content");
}
void preview_interpolation_edges(){
    const auto cfg=config();const Bytes wire(32,0xa5);
    for(const auto ppm:{-10000.,100.,10000.}) {
        auto model=ideal();model.clock_error_ppm=ppm;
        m::StreamingTransmitter source(wire,cfg);m::SimulationChannel channel(cfg,model);
        std::array<float,257> chunk{};
        while(const auto count=source.read(chunk))channel.process({{},count});
        std::array<float,2048> a{},b{};channel.preview_last(source,a);channel.preview_last(source,b);
        require(a==b,"preview reconstruction depends on polling");
        require(std::all_of(a.begin(),a.end(),[](float value){return std::isfinite(value);}),"interpolation edge produced nonfinite samples");
        require(std::any_of(a.begin(),a.end(),[](float value){return std::abs(value)>.01F;}),"preview discarded the surviving patterned signal");
    }
}

void invalid(){
    const auto cfg=config();auto model=ideal();model.clock_error_ppm=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{m::SimulationChannel channel(cfg,model);},"NaNclockaccepted");
    model=ideal();model.phase_noise_degrees_per_sqrt_second=-1;
    rejects([&]{m::SimulationChannel channel(cfg,model);},"negativephasediffusionaccepted");
    model=ideal();model.clock_error_ppm=10001;
    rejects([&]{m::SimulationChannel channel(cfg,model);},"unboundedclockerroraccepted");
    m::SimulationChannel channel(cfg,ideal());rejects([&]{channel.process({{1,0},0});},"zeroobservationaccepted");
    require(m::ChannelConfig{}.clock_error_ppm==100 && m::ChannelConfig{}.phase_noise_degrees_per_sqrt_second==.5,"simulationdefaultsarenotbadcrystalmodel");
}
}
int main(){try{clock_and_carrier();long_coherence();phase_diffusion();pcm_clock();analytic_source_matches_hardware_pcm();sampled_startup_and_carrier();sampled_chunking_and_idle();stream_and_preview();preview_interpolation_edges();invalid();std::cout<<"channel tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
