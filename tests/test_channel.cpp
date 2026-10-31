#include "datapump/channel.hpp"
#include "../src/channel_perturbation.hpp"
#include "datapump/stream_codec.hpp"
#include "datapump/transfer.hpp"
#include "datapump/symbol_schedule.hpp"
#include "resource_test_margin.hpp"
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
    const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-before).count();
    require(testing::benchmark_below(wall,1.0,"channel.long_coherence","wall_seconds"),"long simulation iterated through samples or carrier cycles");
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

void graceful_endpoint() {
    for(const bool before:{false,true}) {
        auto cfg=config();cfg.scramble=true;cfg.integration_seconds=1;
        m::StreamingTransmitter source(m::RawBits{Bytes(12,0)},cfg);
        m::SampledSimulationChannel channel(cfg,ideal());std::array<float,31> chunk{};
        std::uint64_t received=0;
        if(!before)received+=channel.read(source,chunk);
        const auto emitted=source.samples_emitted(),original=source.total_samples();
        source.request_finish();const auto final=source.total_samples();
        require(final<original && final>=emitted,"finish did not retain prefetched PCM");
        while(const auto count=channel.read(source,chunk))received+=count;
        require(source.samples_emitted()==final,"channel did not drain the shortened source");
        require(!before || received==0,"finish before output manufactured a startup burst");
        const auto coordinate=channel.received_samples();channel.read_noise(chunk);
        require(channel.received_samples()==coordinate+chunk.size(),"observed absence did not advance independently");
    }
}
void selective_perturbation() {
    auto cfg=config();cfg.bandwidth_hz=1200;cfg.integration_seconds=.02;cfg.stream_epoch=1800000001;
    auto off=ideal(),zero=off;zero.perturbation_slow_depth=zero.perturbation_fast_depth=.65;
    auto selected=zero;selected.spread_doppler_hz=5;
    const auto bits=m::RawBits{{1,0,1,0}};
    m::StreamingTransmitter a(bits,cfg),b(bits,cfg),c(bits,cfg),d(bits,cfg);
    m::SampledSimulationChannel original(cfg,off),unity(cfg,zero),one(cfg,selected),chunks(cfg,selected);
    require(capture(original,a,101)==capture(unity,b,31),"zero Doppler perturbation changed samples");
    require(!unity.perturbation_metadata().enabled,"zero Doppler created a perturbation bank");
    auto disabled=selected;disabled.perturbation_slow_depth=disabled.perturbation_fast_depth=0;
    m::SampledSimulationChannel no_filter(cfg,disabled);
    require(!no_filter.perturbation_metadata().enabled,"Off allocated a perturbation bank at nonzero Doppler");
    const auto x=capture(one,c,4096),y=capture(chunks,d,13);
    require(x==y,"perturbation changed with block partition");
    std::array<float,32> idle_a{},idle_b{};
    one.read_noise(idle_a);chunks.read_noise(std::span(idle_b).first(11));
    chunks.read_noise(std::span(idle_b).subspan(11));
    require(idle_a==idle_b,"perturbation tail changed with idle partition");
    one.begin_burst();chunks.begin_burst();
    m::StreamingTransmitter next_a(bits,cfg),next_b(bits,cfg);
    require(capture(one,next_a,17)==capture(chunks,next_b,257),"perturbation startup or phases changed between bursts");
    const auto metadata=one.perturbation_metadata();
    require(metadata.enabled && metadata.rates_hz[0]==.5 && metadata.rates_hz[1]==5 && metadata.seed==selected.seed,
            "perturbation metadata did not record the actual rates and seed");
    require(metadata.initial_phases_radians[0][0]!=metadata.initial_phases_radians[1][0] &&
            metadata.initial_phases_radians[0][0]!=metadata.initial_phases_radians[0][1],
            "perturbation phases are not independent");
    require(one.working_bytes()<=m::SampledSimulationChannel::workspace_bound,"perturbation exceeded channel reservation");
    // Compare energy in three windowed frequency bins only over real DATA.
    m::RawBits data_bits;data_bits.bits.resize(64);
    for(std::size_t i=0;i<data_bits.bits.size();++i)data_bits.bits[i]=i%2;
    m::StreamingTransmitter waveform(data_bits,cfg);
    // The begin accessor is absolute UTC PCM; read offsets and valid end are
    // relative to this burst's physical output origin.
    const auto payload_begin=waveform.physical_payload_begin()-waveform.physical_output_origin();
    const auto payload_end=waveform.physical_valid_payload_end();
    std::array<std::complex<double>,256> block{};
    std::array<double,3> energy{};double total_energy=0;
    std::uint64_t sample=0;
    while(const auto count=waveform.read_analytic(block)) {
        if(sample>=payload_begin && sample+count<=payload_end && count==block.size()) {
            for(std::size_t band=0;band<3;++band) {
                std::complex<double> projection{};
                for(std::size_t i=0;i<count;++i) {
                    const auto window=.5-.5*std::cos(tau*static_cast<double>(i)/(count-1));
                    projection+=window*block[i]*std::polar(1.,-tau*metadata.band_centers_hz[band]*
                        static_cast<double>(sample+i)/cfg.sample_rate);
                }
                energy[band]+=std::norm(projection)/count;
            }
            for(const auto value:block)total_energy+=std::norm(value);
        }
        sample+=count;
    }
    require(total_energy>0 && std::all_of(energy.begin(),energy.end(),[&](double e){return e/total_energy>1e-6;}),
            "Robust DATA waveform did not populate all three perturbation ranges");
    for(const auto frequency:metadata.band_centers_hz) {
        m::detail::FrequencySelectivePerturbation filter(cfg.sample_rate,cfg.carrier_hz,cfg.bandwidth_hz,5,0,.65,1);
        double least=100,most=0;
        for(unsigned i=0;i<cfg.sample_rate;++i) {
            const auto output=filter.process(std::polar(1.,tau*frequency*i/cfg.sample_rate));
            if(i>1000) {least=std::min(least,std::abs(output));most=std::max(most,std::abs(output));}
        }
        require(most-least>.2,"a populated frequency range was not modulated");
    }
}
void spread_helper_tail() {
    // Keep the 15 kW Helper's certified low-tone leakage guard clear of DATA.
    m::Config cfg;cfg.sample_rate=8000;cfg.carrier_hz=2000;cfg.bandwidth_hz=200;
    cfg.spreading_factor=4;cfg.integration_seconds=.02;cfg.stream_epoch=1800000001;
    cfg.stream_phase_samples=137;
    helper::Options helper;helper.power_w=15000;helper.data_power_w=4;helper.timecode=false;
    // Six seconds puts the delayed DATA slot after the Helper's normal tail
    // as well as exercising output beyond that tail.
    auto model=ideal();model.spread_delay_ms=6000;
    m::SampledSimulationChannel channel(cfg,model,helper),helper_only(cfg,model,helper);
    // Align the seeded startup draws; the control carries the same Helper
    // envelope and channel tail but suppresses both DATA rays.
    channel.begin_burst();helper_only.begin_burst(true);
    m::StreamingTransmitter source(m::RawBits{{0}},cfg),silent_data(m::RawBits{{0}},cfg);
    const auto data_begin=source.physical_payload_begin()-source.physical_output_origin();
    const auto output=capture(channel,source,2048);
    const auto baseline=capture(helper_only,silent_data,2048);
    require(output.size()==baseline.size(),"helper-only control changed the spread capture extent");
    require(channel.payload_end_sample().has_value(),"helper spread did not expose source endpoint");
    require(output.size()>=*channel.payload_end_sample()+cfg.sample_rate*6,
            "physical helper capture truncated the delayed ray");
    double energy=0,helper_energy=0;
    for(std::size_t i=output.size()-2*cfg.sample_rate;i<output.size()-cfg.sample_rate;++i) {
        energy+=static_cast<double>(output[i])*output[i];
        helper_energy+=static_cast<double>(baseline[i])*baseline[i];
    }
    require(helper_energy<1e-12,"late delayed-ray window still contains the Helper signal");
    require(energy>1e-8,"physical helper capture emitted no delayed-ray signal");
    const auto delayed_begin=static_cast<std::size_t>(std::ceil(channel.startup_offset_samples()+
        data_begin+cfg.sample_rate*6+8));
    const auto delayed_end=static_cast<std::size_t>(std::floor(channel.startup_offset_samples()+
        data_begin+m::symbol_sample_count(cfg)+cfg.sample_rate*6-8));
    require(delayed_begin<delayed_end && delayed_end<=output.size(),"delayed DATA window is unavailable");
    double data_energy=0,data_helper_energy=0;
    for(auto i=delayed_begin;i<delayed_end;++i) {
        data_energy+=static_cast<double>(output[i])*output[i];
        data_helper_energy+=static_cast<double>(baseline[i])*baseline[i];
    }
    require(data_helper_energy<1e-12,"delayed DATA window still contains the Helper signal");
    require(data_energy>1e-8,"physical helper capture omitted the delayed DATA slot");
    require(channel.working_bytes()<=m::SampledSimulationChannel::workspace_bound,"helper spread exceeded workspace");
}
void bulk_spread_and_perturbation() {
    auto cfg=config();cfg.bandwidth_hz=1200;cfg.integration_seconds=.02;
    std::vector<float> source(cfg.sample_rate);
    for(std::size_t i=0;i<source.size();++i) {
        const auto t=static_cast<double>(i)/cfg.sample_rate;
        source[i]=static_cast<float>(.25*std::cos(tau*1100*t)+.25*std::cos(tau*1500*t)+.25*std::cos(tau*1900*t));
    }
    auto off=ideal(),zero=off;zero.perturbation_slow_depth=zero.perturbation_fast_depth=.65;
    require(m::simulate(source,cfg,off)==m::simulate(source,cfg,zero),"zero-Doppler bulk perturbation changed PCM");
    auto spread=off;spread.spread_delay_ms=200;spread.spread_doppler_hz=5;
    auto both=spread;both.perturbation_slow_depth=.25;both.perturbation_fast_depth=.25;
    const auto reference=m::simulate(source,cfg,off);
    const auto spread_pcm=m::simulate(source,cfg,spread);
    const auto modulated=m::simulate(source,cfg,both);
    require(spread_pcm==m::simulate(source,cfg,spread) && modulated==m::simulate(source,cfg,both),
            "bulk propagation or modulation was not reproducible");
    require(spread_pcm.size()==reference.size()+cfg.sample_rate/5 && modulated.size()>spread_pcm.size(),
            "bulk channel did not retain both the delayed ray and modulation-filter tail");
    double tail_energy=0,spread_difference=0,modulation_difference=0;
    for(std::size_t i=0;i<reference.size();++i) {
        const auto a=static_cast<double>(spread_pcm[i])-reference[i];
        const auto b=static_cast<double>(modulated[i])-spread_pcm[i];
        spread_difference+=a*a;modulation_difference+=b*b;
    }
    for(std::size_t i=reference.size()+32;i+32<spread_pcm.size();++i)
        tail_energy+=static_cast<double>(spread_pcm[i])*spread_pcm[i];
    require(spread_difference>1 && modulation_difference>1 && tail_energy>1,
            "bulk spread, independent perturbation or delayed signal was absent");
}
void invalid(){
    const auto cfg=config();auto model=ideal();model.clock_error_ppm=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{m::SimulationChannel channel(cfg,model);},"NaNclockaccepted");
    model=ideal();model.phase_noise_degrees_per_sqrt_second=-1;
    rejects([&]{m::SimulationChannel channel(cfg,model);},"negativephasediffusionaccepted");
    model=ideal();model.clock_error_ppm=10001;
    rejects([&]{m::SimulationChannel channel(cfg,model);},"unboundedclockerroraccepted");
    model=ideal();model.spread_doppler_hz=1;
    rejects([&]{m::SimulationChannel unsupported(cfg,model);},"accelerated simulation silently ignored spread");
    m::SimulationChannel channel(cfg,ideal());rejects([&]{channel.process({{1,0},0});},"zeroobservationaccepted");
    require(m::ChannelConfig{}.clock_error_ppm==100 && m::ChannelConfig{}.phase_noise_degrees_per_sqrt_second==.5,"simulationdefaultsarenotbadcrystalmodel");
}
void hardware_test_channel() {
    auto cfg=config();cfg.bandwidth_hz=1200;cfg.integration_seconds=.02;cfg.stream_epoch=1800000001;
    const m::RawBits bits{{0,0,1}};
    const auto read_all=[](m::HardwareTestChannel& channel,m::StreamingTransmitter& source,std::size_t block) {
        std::vector<float> output,chunk(block);
        while(const auto count=channel.read(source,chunk))output.insert(output.end(),chunk.begin(),chunk.begin()+count);
        return output;
    };
    m::StreamingTransmitter raw(bits,cfg),bypass_source(bits,cfg);
    std::vector<float> expected(raw.total_samples());std::size_t written=0;
    while(written<expected.size())written+=raw.read(std::span(expected).subspan(written));
    auto zero=ideal();zero.perturbation_slow_depth=zero.perturbation_fast_depth=.65;
    zero.snr_db=-80;zero.clock_error_ppm=100;zero.frequency_offset_hz=300;
    m::HardwareTestChannel bypass(cfg,zero);
    require(!bypass.active() && bypass.headroom_gain()==1,"zero-reference hardware test instantiated processing");
    require(read_all(bypass,bypass_source,29)==expected,"hardware bypass changed original source PCM");

    auto model=ideal();model.spread_delay_ms=.05;model.spread_doppler_hz=.1;
    model.perturbation_slow_depth=model.perturbation_fast_depth=.65;
    m::StreamingTransmitter source_a(bits,cfg),source_b(bits,cfg);
    m::HardwareTestChannel one(cfg,model),many(cfg,model);
    const auto a=read_all(one,source_a,4096),b=read_all(many,source_b,17);
    require(one.active() && one.headroom_gain()>0 && one.headroom_gain()<1,"active hardware channel omitted headroom");
    require(a==b,"hardware stage changed seeded waveform with block partition");
    require(a.size()==one.source_end_sample(source_a),"hardware channel endpoint omitted its tail");
    require(std::all_of(a.begin(),a.end(),[](float value){return std::isfinite(value) && std::abs(value)<=1;}),
            "hardware channel exceeded full-scale output");

    m::StreamingTransmitter delayed_source(bits,cfg);
    const auto source_end=delayed_source.total_samples();
    const auto begin=delayed_source.physical_payload_begin()-delayed_source.physical_output_origin();
    auto delayed=ideal();delayed.spread_delay_ms=1000.*(source_end+cfg.sample_rate)/cfg.sample_rate;
    m::HardwareTestChannel delayed_channel(cfg,delayed);
    const auto delayed_output=read_all(delayed_channel,delayed_source,257);
    const auto delay_samples=source_end+cfg.sample_rate;
    const auto data_begin=begin+delay_samples+8,data_end=begin+delay_samples+m::symbol_sample_count(cfg)-8;
    require(data_begin>source_end && data_begin<data_end && data_end<delayed_output.size(),"delayed DATA interval was not drained");
    double energy=0;
    for(auto i=data_begin;i<data_end;++i)energy+=double(delayed_output[i])*delayed_output[i];
    require(energy>1e-8,"hardware channel omitted delayed DATA after original source end");

    for(const bool before:{true,false}) {
        m::StreamingTransmitter source(m::RawBits{Bytes(12,0)},cfg);
        m::HardwareTestChannel channel(cfg,model);std::array<float,137> chunk{};
        std::uint64_t emitted=before?0:channel.read(source,chunk);
        source.request_finish();
        while(const auto count=channel.read(source,chunk))emitted+=count;
        require(emitted==channel.source_end_sample(source),"graceful hardware stop did not drain its new endpoint");
        require(!before || emitted==0,"hardware stop before output created a delayed burst");
    }
}
void fhss_channel_order() {
    auto cfg=config();cfg.carrier_hz=2000;cfg.bandwidth_hz=200;
    cfg.scramble=true; // FHSS uses the production private shaped waveform.
    cfg.spreading_factor=64;cfg.stream_epoch=1800000001;cfg.stream_phase_samples=137;
    cfg.data_key.emplace(Bytes(32,0x65));
    m::FhssPlan hops;hops.channels=2;hops.shifts[1]=30000000;hops.dwell_seconds=.4;
    hops.clock_error_seconds=.01;hops.peer_clock_error_seconds=.01;hops.audio_error_seconds=0;
    cfg.fake_hopping=m::resolve_fhss_power(hops,cfg);
    require(cfg.fake_hopping->smooth_ramps,"FHSS channel fixture did not select smooth transmitter ramps");
    const m::RawBits bits{{0,0,1}};
    m::StreamingTransmitter transmitter(bits,cfg);
    const auto total=transmitter.total_samples();
    const auto origin=transmitter.physical_output_origin();
    const auto data_begin=transmitter.physical_payload_begin()-origin;
    const auto data_end=data_begin+m::symbol_sample_count(cfg);
    std::vector<float> sent(total);transmitter.read(sent);
    // Independent UTC distance oracle: this is the TRANSMITTER guard, not a
    // requirement that a propagated echo vanish at its arrival coordinate.
    const auto distance=[&](long double sample) {
        const auto local=std::fmod(static_cast<long double>(origin% (2ULL*cfg.sample_rate))+sample,
                                  static_cast<long double>(cfg.sample_rate)*.4L);
        return std::min(local,static_cast<long double>(cfg.sample_rate)*.4L-local);
    };
    const auto quiet=std::ceil(cfg.sample_rate*cfg.fake_hopping->power_clock_region_seconds());
    std::size_t guarded=0;
    for(std::size_t i=0;i<sent.size();++i)if(distance(i)<quiet-2) {
        ++guarded;require(sent[i]==0,"original transmitter violated its FHSS quiet guard");
    }
    require(guarded>100,"FHSS transmitter guard oracle covered no samples");
    auto model=ideal();model.spread_delay_ms=80;model.spread_doppler_hz=.1;
    const auto delay=static_cast<std::uint64_t>(.08*cfg.sample_rate);
    const auto collect=[&](const m::ChannelConfig& effect,std::size_t block) {
        m::StreamingTransmitter source(bits,cfg);m::HardwareTestChannel channel(cfg,effect);
        std::vector<float> output,chunk(block);
        while(const auto count=channel.read(source,chunk))output.insert(output.end(),chunk.begin(),chunk.begin()+count);
        require(source.samples_emitted()==total,"channel echo advanced the original transmitter source");
        require(output.size()==channel.source_end_sample(source),"FHSS channel tail was truncated");
        return output;
    };
    auto zero=ideal();zero.perturbation_slow_depth=zero.perturbation_fast_depth=.65;
    // Exact bypass compares the same source read partition: the established
    // source's floating carrier reconstruction need not be bit-identical
    // between a whole-wave read and short callback reads.
    m::StreamingTransmitter bypass_reference(bits,cfg);std::vector<float> bypass_pcm;
    std::array<float,31> bypass_block{};
    while(const auto count=bypass_reference.read(bypass_block))
        bypass_pcm.insert(bypass_pcm.end(),bypass_block.begin(),bypass_block.begin()+count);
    require(collect(zero,31)==bypass_pcm,"FHSS zero-reference bypass changed transmitter samples");
    const auto spread=collect(model,4096);
    auto each=model;each.perturbation_slow_depth=each.perturbation_fast_depth=.25;
    const auto modulated=collect(each,4096);
    const auto partitioned=collect(each,17);
    require(partitioned==collect(each,17),"FHSS channel did not reproduce its seeded waveform");
    require(modulated.size()==partitioned.size(),"FHSS channel partition changed output extent");
    double partition_error=0,peak=0;
    for(std::size_t i=0;i<modulated.size();++i) {
        partition_error=std::max(partition_error,std::abs(double(modulated[i])-partitioned[i]));
        peak=std::max(peak,std::abs(double(modulated[i])));
    }
    // The private source reconstructs its carrier per read; double roundoff
    // can cross a final float rounding boundary. Allow only that precision,
    // not a discontinuity or a restarted modulation/filter state.
    require(partition_error<=std::numeric_limits<float>::epsilon()*peak,
            "FHSS channel state reset at a block or hop boundary");
    double delayed_data_energy=0,history_energy=0,modulated_energy=0;
    std::size_t echoes=0;
    std::vector<std::size_t> echo_samples;
    for(std::size_t i=data_begin+delay+16;i<data_end;++i) {
        if(distance(i)<quiet-8 && distance(i-delay)>quiet+cfg.sample_rate*.01+8) {
            ++echoes;echo_samples.push_back(i);
            require(sent[i]==0,"echo window was not in original transmitter silence");
            history_energy+=double(sent[i-delay])*sent[i-delay];
            delayed_data_energy+=double(spread[i])*spread[i];
            modulated_energy+=double(modulated[i])*modulated[i];
        }
    }
    require(echoes>16 && history_energy>1e-6,"FHSS delayed DATA oracle has no populated boundary window");
    require(delayed_data_energy>1e-6 && modulated_energy>1e-6,
            "a second FHSS gate erased delayed DATA in the transmitter quiet interval");
    // The seeded sampled simulator has fractional startup/carrier phase but
    // must retain the same emission-time gate on each propagation ray.
    m::SampledSimulationChannel normal(cfg,ideal()),simulated(cfg,model),perturbed(cfg,each);
    m::StreamingTransmitter plain_source(bits,cfg),spread_source(bits,cfg),each_source(bits,cfg);
    const auto plain=capture(normal,plain_source,137),sim=capture(simulated,spread_source,137),amp=capture(perturbed,each_source,137);
    require(amp.size()>sim.size() && sim.size()>=plain.size()+delay,
            "sampled FHSS channel omitted delayed or modulation-filter output tail");
    double plain_energy=0,sim_energy=0,amp_energy=0;
    for(const auto i:echo_samples) {
        const auto receiver=static_cast<std::size_t>(std::llround(normal.startup_offset_samples()+i));
        require(receiver<plain.size() && receiver<sim.size() && receiver<amp.size(),"sampled FHSS echo window missing");
        plain_energy+=double(plain[receiver])*plain[receiver];
        sim_energy+=double(sim[receiver])*sim[receiver];amp_energy+=double(amp[receiver])*amp[receiver];
    }
    require(plain_energy<1e-16 && sim_energy>1e-6 && amp_energy>1e-6,
            "sampled simulation suppressed a delayed DATA echo with the current hop gate");
    const auto bulk_plain=m::simulate(sent,cfg,ideal()),bulk_spread=m::simulate(sent,cfg,model),bulk_amp=m::simulate(sent,cfg,each);
    double bulk_before=0,bulk_echo=0,bulk_modulated=0;
    for(const auto i:echo_samples) {
        const auto receiver=static_cast<std::size_t>(std::llround(normal.startup_offset_samples()+i));
        bulk_before+=double(bulk_plain[receiver])*bulk_plain[receiver];
        bulk_echo+=double(bulk_spread[receiver])*bulk_spread[receiver];
        bulk_modulated+=double(bulk_amp[receiver])*bulk_amp[receiver];
    }
    // The bulk Hilbert representation has finite-record leakage, so compare
    // the channel echo to that independently retained source-only floor.
    require(bulk_echo>1e-6 && bulk_echo>100*bulk_before && bulk_modulated>1e-6,
            "bulk PCM channel re-gated the delayed FHSS DATA echo");
}
}
int main(int argc,char** argv){try{if(argc==2 && std::string_view(argv[1])=="--fhss-channel") {fhss_channel_order();std::cout<<"FHSS channel ordering passed\n";return 0;}require(argc==1,"unknown channel test selector");clock_and_carrier();long_coherence();phase_diffusion();pcm_clock();analytic_source_matches_hardware_pcm();sampled_startup_and_carrier();sampled_chunking_and_idle();stream_and_preview();preview_interpolation_edges();graceful_endpoint();selective_perturbation();spread_helper_tail();bulk_spread_and_perturbation();hardware_test_channel();fhss_channel_order();invalid();std::cout<<"channel tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
