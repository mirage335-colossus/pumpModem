#include "datapump/channel.hpp"
#include "datapump/packet.hpp"
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
    std::vector<float> source(cfg.sample_rate*2);
    for(std::size_t i=0;i<source.size();++i)source[i]=static_cast<float>(.7*std::cos(tau*cfg.carrier_hz*static_cast<double>(i)/cfg.sample_rate));
    const auto output=m::simulate(source,cfg,impairment);
    require(output.size()==static_cast<std::size_t>(std::ceil(source.size()/1.0001L)),"PCM clock drift did not change capture duration");
    double error=0;std::complex<double> received{};
    for(std::size_t i=32;i+32<output.size();++i){
        const auto expected=.7*std::cos(tau*cfg.carrier_hz*1.0001*static_cast<double>(i)/cfg.sample_rate);
        error+=std::pow(output[i]-expected,2);
        received+=2.*output[i]*std::polar(1.,-tau*cfg.carrier_hz*static_cast<double>(i)/cfg.sample_rate);
    }
    require(std::sqrt(error/static_cast<double>(output.size()-64))<.002,"PCM interpolation lost crystal-shifted carrier");
    const auto expectation=.7*integral(.15,32,output.size()-64,cfg.sample_rate);
    require(std::abs(received/static_cast<double>(output.size()-64)-expectation)<.002,"PCM and accelerated carrier integrations disagree");
    impairment.phase_noise_degrees_per_sqrt_second=4;
    const auto noisy=m::simulate(source,cfg,impairment);
    require(noisy==m::simulate(source,cfg,impairment),"PCM phase diffusion is not reproducible");
    require(noisy!=output,"PCM phase diffusion did not affect the carrier");
}
void packet_and_preview(){
    auto cfg=config();cfg.sample_rate=9600;cfg.bandwidth_hz=2400;cfg.carrier_hz=1800;cfg.constellation_bits=6;
    Message message;message.id[0]=83;message.data={'1','0','0','p','p','m'};
    PacketOptions options;options.fec=FecMode::rs60;
    auto wire=m::preamble(cfg);const auto frame=encode_packet(message,options);wire.insert(wire.end(),frame.begin(),frame.end());
    m::StreamingTransmitter source(wire,cfg);
    m::StreamingReceiver receiver(cfg,m::preamble(cfg));
    m::ChannelConfig model;model.snr_db=35;model.seed=517;m::SimulationChannel channel(cfg,model),without_plots(cfg,model);
    Bytes received;unsigned observations=0;
    while(const auto observation=source.next_symbol()){
        const auto output=channel.process(*observation),control=without_plots.process(*observation);
        require(output.has_value()==control.has_value(),"plotting changed channel timing");
        if(output){require(output->value==control->value,"plotting changed channel noise");const auto part=receiver.push_symbols(std::span(&*output,1));received.insert(received.end(),part.begin(),part.end());}
        if(++observations%257==0){
            std::array<float,2048> first{},second{};channel.preview_last(source,first);channel.preview_last(source,second);
            require(first==second,"same simulation review frame changed between polls");
            for(const auto sample:first)require(std::isfinite(sample),"nonfinite simulation preview");
        }
    }
    const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
    require(received.size()>=wire.size(),"ordinary100ppm packet did not acquire");
    const auto decoded=decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(wire.size())),options);
    require(decoded.message.data==message.data,"ordinary100ppm packet changed content");
    std::array<float,2048> impaired{},raw{};channel.preview_last(source,impaired);source.preview_last(raw);
    require(impaired!=raw,"simulation review omitted oscillator impairments");
}
void preview_interpolation_edges(){
    const auto cfg=config();auto wire=m::preamble(cfg);wire.resize(wire.size()+1024,0);
    for(const auto ppm:{-10000.,100.,10000.}){
        auto model=ideal();model.clock_error_ppm=ppm;
        m::StreamingTransmitter source(wire,cfg);m::SimulationChannel channel(cfg,model);
        while(const auto observation=source.next_symbol())channel.process(*observation);
        std::array<std::complex<double>,m::StreamingTransmitter::analytic_preview_limit> analytic{};
        source.preview_last_analytic(analytic);
        const auto baseband=analytic.back()*std::polar(1.,-tau*cfg.carrier_hz*static_cast<double>(source.samples_emitted()-1)/cfg.sample_rate);
        std::array<float,2048> preview{};channel.preview_last(source,preview);
        const auto start=channel.preview_end_samples()-preview.size();
        double maximum_error=0;
        for(std::size_t i=0;i<preview.size();++i){
            const auto phase=tau*cfg.carrier_hz*(1+ppm*1e-6)*static_cast<double>(start+i)/cfg.sample_rate;
            maximum_error=std::max(maximum_error,std::abs(preview[i]-(baseband*std::polar(1.,phase)).real()));
        }
        require(maximum_error<.002,"simulation preview interpolation discarded valid edge samples");
    }
}
void sdr_and_missing_training(){
    for(const bool omit_training:{false,true}){
        auto cfg=config();cfg.sample_rate=120000000;cfg.bandwidth_hz=30000000;cfg.carrier_hz=22500000;cfg.constellation_bits=6;
        Message message;message.id[0]=0x43;message.data={'S','D','R'};
        const auto frame=encode_packet(message);auto wire=m::preamble(cfg);wire.insert(wire.end(),frame.begin(),frame.end());
        m::StreamingTransmitter source(wire,cfg);std::size_t validation_calls=0;
        m::StreamingReceiver receiver(cfg,m::preamble(cfg),8*1024*1024,[&](const Bytes& prefix){
            ++validation_calls;
            try{return packet_bootstrap_possible(prefix) && packet_frame_size(prefix).has_value();}
            catch(const Error&){return false;}
        });
        m::ChannelConfig model;model.snr_db=40;m::SimulationChannel channel(cfg,model);
        std::size_t observations=0;Bytes received;
        while(const auto observation=source.next_symbol()){
            ++observations;
            const auto output=channel.process(*observation);
            if(omit_training && source.samples_emitted()<=m::training_sample_count(cfg))continue;
            if(output){const auto bytes=receiver.push_symbols(std::span(&*output,1));received.insert(received.end(),bytes.begin(),bytes.end());}
        }
        const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
        require(observations<10000,"SDR-rate training expanded into sample-rate-sized simulation work");
        // Eight timing origins and at most eight gain hypotheses for64APSK.
        // Each training segment replaces at most96 retained header symbols,
        // then one identical-window rejection enables the constant-run skip.
        const auto search_bound=64*8*(96+1)*8+observations*8*8;
        require(validation_calls<=search_bound,"SDR-rate training exceeded its segment/window bootstrap-search work bound");
        require(received.size()>=wire.size(),"100ppm high-bandwidth or missing-training packet did not acquire");
        const auto decoded=decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(wire.size())));
        require(decoded.message.data==message.data,"high-bandwidth impaired packet changed content");
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
int main(){try{clock_and_carrier();long_coherence();phase_diffusion();pcm_clock();packet_and_preview();preview_interpolation_edges();sdr_and_missing_training();invalid();std::cout<<"channel tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
