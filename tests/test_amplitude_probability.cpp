#include "../src/amplitude_probability.hpp"
#include "../src/estimate_cancellation.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/tuning.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "datapump/channel.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/pattern_code.hpp"
#include <map>
#include <fstream>
#include <iomanip>
#include <chrono>
using namespace datapump;
namespace {
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
transfer::Options eme(double seconds) {
    transfer::Options o;o.modem=tuning::resolve(1,-3.0720996964786846,tuning::PatternMode::auto_pattern,false,1500).config;
    o.modem.integration_seconds=seconds;o.modem.receiver_strategy=modem::ReceiverStrategy::amplitude_only;
    o.timestamp=o.modem.stream_epoch=1800000041;o.search_seconds=6;o.dsp_workspace_bytes=128*1024*1024;
    modem::OscillatorSearchConfig osc;osc.lf={.0001,.005};osc.rf=osc.lf;osc.rf_shift_hz=5.8e9;osc.margin=3;
    osc.reference=modem::OscillatorReference::independent_audio;o.modem.oscillator_search=osc;return o;
}
transfer::Estimate wire(const modem::Config& c,unsigned bits=1) {
    transfer::Estimate t;t.wire_bits=bits;t.coded_bytes=(bits+7)/8;
    t.total_seconds=(modem::training_sample_count(c)+2*modem::pattern_pulse_padding_samples(c)+
        modem::suppression_sample_count(c)+bits*modem::symbol_sample_count(c))/double(c.sample_rate);return t;
}
modem::ChannelConfig channel(const modem::Config& c,double cn0) {
    modem::ChannelConfig ch;ch.clock_error_ppm=0;ch.frequency_offset_hz=0;ch.phase_noise_degrees_per_sqrt_second=0;ch.snr_db=cn0-10*std::log10(c.sample_rate/2.);return ch;
}
}
namespace {
modem::ChannelConfig channel(const modem::Config& c,double es,std::uint64_t seed) {
    modem::ChannelConfig ch;ch.snr_db=es-10*std::log10(modem::symbol_sample_count(c)/2.);
    ch.seed=100003+104729*seed;
    // Independent startup, phase and noise streams in the production channel.
    // Spread true arrivals across the configured positive six-second window;
    // neither the true arrival nor the seed is supplied to receiver search.
    ch.delay_samples=137+(seed*1543)%35000;
    ch.clock_error_ppm=modem::oscillator_effects(c).clock_error_ppm;
    ch.frequency_offset_hz=modem::oscillator_effects(c).frequency_offset_hz;
    ch.phase_noise_degrees_per_sqrt_second=modem::oscillator_effects(c).phase_noise_degrees_per_sqrt_second;
    return ch;
}
std::uint64_t bit_draw(std::uint64_t value) {
    value+=0x9e3779b97f4a7c15ULL;value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;return value^(value>>31);
}
double source_power(const modem::Config& c,unsigned bit) {
    struct Entry {std::uint64_t samples;unsigned bit;double mean;};
    static std::vector<Entry> cache;
    const auto n=modem::symbol_sample_count(c);
    for(const auto& e:cache)if(e.samples==n&&e.bit==bit)return e.mean;
    modem::PatternTransmitter tx(Bytes{static_cast<std::uint8_t>(bit)},c,c.stream_epoch,0,false);
    std::array<std::complex<double>,4096> block{};std::uint64_t emitted=0,counted=0;
    const auto begin=modem::pattern_pulse_padding_samples(c);
    long double energy=0,real_energy=0;std::complex<long double> image{};
    while(counted<n) {
        const auto got=tx.read_analytic(block);require(got>0,"source ended before useful-symbol power measurement");
        for(std::size_t j=0;j<got;++j,++emitted) {
            if(emitted<begin||emitted-begin>=n)continue;
            const std::complex<long double> z{block[j].real(),block[j].imag()};
            energy+=std::norm(z);real_energy+=z.real()*z.real();image+=z*z;++counted;
        }
    }
    const auto mean=double(energy/(2*n*modem::nominal_signal_power));
    const auto fixed=double(real_energy/(n*modem::nominal_signal_power));
    const auto ratio=double(std::abs(image)/energy);
    std::cout<<"source seconds="<<double(n)/c.sample_rate<<" bit="<<bit<<" mean_power="<<mean
        <<" fixed_power="<<fixed<<" image_ratio="<<ratio<<'\n';
    require(ratio<1e-6&&std::abs(fixed/mean-1)<1e-6,"fixed-phase source normalization needs separate qualification");
    cache.push_back({n,bit,mean});return mean;
}
void amplitude_capture(unsigned seconds,double watts,std::uint64_t seed,unsigned bits,unsigned extra_keys) {
 const auto started=std::chrono::steady_clock::now();auto o=eme(seconds);auto& c=o.modem;c.receiver_strategy=modem::ReceiverStrategy::amplitude_only;
 const auto cn0=10*std::log10(watts*1000.)-220+164;
 Bytes expected;for(unsigned i=0;i<bits;i++)expected.push_back(bit_draw(seed^0x2f614c5ab7213d89ULL^i)&1);
 auto ch=channel(c,cn0+10*std::log10(seconds),seed);ch.snr_db-=10*std::log10(source_power(c,expected.front()));
 // One-bit mean-power convention is independently measured. Multibit is only a diagnostic until its own normalization is qualified.
 modem::StreamingTransmitter tx(modem::RawBits{expected},c,o.dsp_workspace_bytes);
 modem::SampledSimulationChannel sampled(c,ch);std::vector<float> pcm;std::array<float,4096> block{};
 for(;;){const auto n=sampled.read(tx,block);if(!n)break;pcm.insert(pcm.end(),block.begin(),block.begin()+n);}
 auto silence=modem::symbol_sample_count(c)+c.sample_rate*15;
 while(silence){const auto n=std::min<std::uint64_t>(silence,block.size());sampled.read_noise(std::span(block).first(n));pcm.insert(pcm.end(),block.begin(),block.begin()+n);silence-=n;}
 modem::PatternSearch search;search.start_offset_seconds=double(modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c))/c.sample_rate;
 search.start_uncertainty_seconds=o.search_seconds+1;search.worker_threads=1;search.chunk_bits=1;search.bit_limit=32;search.candidate_limit=128;
 const auto plan=modem::resolve_receiver_strategy(c,search,o.dsp_workspace_bytes);
 require(plan.supported&&plan.backend==modem::ReceiverBackend::amplitude_energy,"native capture did not select amplitude");
 std::vector<std::unique_ptr<modem::PatternReceiver>> receivers;
 receivers.push_back(std::make_unique<modem::PatternReceiver>(c,o.dsp_workspace_bytes,search));
 for(unsigned key=0;key<extra_keys;++key){auto other=c;other.scramble=true;other.data_key.emplace(Bytes(32,static_cast<std::uint8_t>(0x39+key)));receivers.push_back(std::make_unique<modem::PatternReceiver>(other,o.dsp_workspace_bytes,search));}
 struct Stream{Bytes bits;std::uint64_t end=0,complete_at=0;unsigned completions=0;double score=0;};
 std::map<std::pair<std::uint64_t,std::uint64_t>,Stream> streams;std::size_t extras=0;bool early=false;std::uint64_t first_poll=0;
 for(size_t at=0;at<pcm.size();){const auto n=std::min<size_t>(4096,pcm.size()-at);
  for(size_t bank=0;bank<receivers.size();++bank){auto& rx=*receivers[bank];rx.push(std::span(pcm).subspan(at,n));
   for(const auto& b:rx.take_bursts()){
    if(bank){extras+=b.bits.size();continue;}
    require(!b.missing_slots,"native amplitude capture admitted missing slots");auto& s=streams[{b.stream_first_sample,b.stream_first_symbol}];
    require(b.first_stream_symbol==b.stream_first_symbol+s.bits.size(),"native amplitude pending identity changed");
    s.bits.insert(s.bits.end(),b.bits.begin(),b.bits.end());s.score=b.score;
    if(!b.bits.empty()){if(!first_poll)first_poll=at+n;s.end=b.end_sample;}
    if(b.complete){s.complete_at=at+n;++s.completions;
     const auto absence=static_cast<long double>(modem::symbol_sample_count(c))/(1+plan.amplitude_clock_half_width_ppm*1e-6L);
     early=early||s.complete_at+1<s.end+std::floor(absence);
    }
   }
   require(rx.working_bytes()<=o.dsp_workspace_bytes,"native amplitude workspace exceeded");
  }
  at+=n;
 }
 bool correct=streams.size()==1&&extras==0&&!early;size_t observed=0,complete=0;double score=0;
 for(const auto& [id,s]:streams){correct=correct&&s.bits==expected&&s.completions==1;observed+=s.bits.size();complete+=s.completions;score=std::max(score,s.score);}
 std::cout<<"amplitude_capture seconds="<<seconds<<" watts="<<watts<<" cn0="<<cn0<<" seed="<<seed<<" bits="<<bits<<" extra_keys="<<extra_keys<<" correct="<<correct<<" observed="<<observed<<" streams="<<streams.size()<<" extras="<<extras<<" complete="<<complete<<" early="<<early<<" score="<<score<<" first_poll="<<first_poll<<" fft="<<modem::pattern_amplitude_geometry(c,search).fft_samples<<" channels="<<plan.amplitude_channels<<" elapsed="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<'\n'<<std::flush;
 require(!early,"native amplitude physical-end contract failed");
}
}
int main(int argc,char** argv){try {
    if(argc>=7&&std::string_view(argv[1])=="--native-capture") {
        std::cout<<std::setprecision(17);
        const auto seconds=std::stoul(argv[2]);const auto watts=std::stod(argv[3]);const auto seed=std::stoull(argv[4]);
        const auto count=std::stoul(argv[5]),bits=std::stoul(argv[6]);const auto keys=argc>7?std::stoul(argv[7]):0;
        require(seconds>=16&&seconds<=86400&&watts>0&&std::isfinite(watts)&&bits>=1&&bits<=32&&keys<=8,"invalid native capture input");
        if(argc>8){std::ifstream input(argv[8]);require(bool(input),"cannot open frozen seed list");std::uint64_t trial_seed;unsigned read=0;while(input>>trial_seed){amplitude_capture(static_cast<unsigned>(seconds),watts,trial_seed,static_cast<unsigned>(bits),static_cast<unsigned>(keys));++read;}require(read==count,"frozen seed list count mismatch");}
        else for(std::uint64_t i=0;i<count;++i)amplitude_capture(static_cast<unsigned>(seconds),watts,seed+i,static_cast<unsigned>(bits),static_cast<unsigned>(keys));
        return 0;
    }

    require(argc==1,"unknown amplitude probability test arguments");
    auto o=eme(202866.3286667);modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=7;
    const auto low=simulation::detail::amplitude_probability(o.modem,search,-19.979400086720375+10*std::log10(modem::symbol_seconds(o.modem)),0,256);
    require(low.available&&low.correct<.01&&low.physical_blocks>70000&&low.source_blocks<=2048,
        "reported4W EME preview needs bounded, low amplitude reference rather than a coherent estimate");
    auto useful=eme(512);
    const auto strong=simulation::detail::amplitude_probability(useful.modem,search,15.760912590557+10*std::log10(512.),0,256);
    require(strong.available&&strong.correct>.9&&strong.wrong<.01&&strong.threshold>25,
        "high-power amplitude positive control did not retain threshold/alternative/quarter checks");
    std::cout<<"amplitude_reference low="<<low.correct<<" strong="<<strong.correct<<" blocks="<<low.physical_blocks<<" source_blocks="<<low.source_blocks<<'\n';
    const auto estimated=simulation::estimate(wire(o.modem),o,true,channel(o.modem,-19.979400086720375),{},1,true,0,256,simulation::ReceiverWorkMode::hardware_fallback);
    std::cout<<"estimate model="<<estimated.probability_model_limit<<" profile="<<estimated.profile_matches<<" carrier="<<estimated.carrier_in_search<<" clock="<<estimated.clock_in_search<<" ram="<<estimated.receiver_workspace_supported<<" limited="<<estimated.oscillator_search_limited<<" strategy="<<estimated.receiver_geometry.receiver_strategy_selected<<std::endl;
    require(estimated.one_bit_reference_available&&estimated.reference_probability_available&&estimated.probability_reference_only&&
        !estimated.confidence_available&&!estimated.one_bit_confidence_available&&!estimated.probability_interval_available&&
        estimated.probability_model_limit.starts_with("Amplitude matched-envelope"),"amplitude reference must reach consumer fields without inventing qualification");
    auto private_profile=o.modem;private_profile.scramble=true;
    const std::array profiles{o.modem,private_profile};
    const auto mixed=simulation::estimate(wire(o.modem),o,true,channel(o.modem,-19.979400086720375),profiles,1,true,0,256,
        simulation::ReceiverWorkMode::hardware_fallback);
    require(mixed.reference_probability_available&&!mixed.confidence_available&&mixed.probability_model_limit.find("multi-profile/key")!=std::string::npos,
        "console's mixed receive bank must expose conditional matching-profile reference without full-bank confidence");
    const auto longer=simulation::estimate(wire(o.modem,3),o,true,channel(o.modem,-19.979400086720375),{},1,true,0,256,simulation::ReceiverWorkMode::hardware_fallback);
    require(longer.one_bit_reference_available&&!longer.reference_probability_available&&!longer.confidence_available,
        "single-symbol amplitude reference must not fabricate complete-draft success");
    auto endpoint=eme(3*365.25*86400);
    const auto endpoint_reference=simulation::detail::amplitude_probability(endpoint.modem,search,
        -19.979400086720375+10*std::log10(modem::symbol_seconds(endpoint.modem)),0,256);
    require(endpoint_reference.available&&endpoint_reference.correct>.99&&endpoint_reference.source_blocks<=2048,
        "three-year endpoint must remain a bounded conditional reference, without materializing PCM");
    o.modem.integration_seconds=3*365.25*86400+1;
    require(!simulation::detail::amplitude_probability(o.modem,search,100,0,256).available,
        "reference should refuse symbols beyond the user's relevant3year domain");
    auto private_config=useful.modem;private_config.scramble=true;private_config.data_key.emplace(Bytes(32,0x39));
    require(!simulation::detail::amplitude_probability(private_config,search,100,0,256).available,
        "public reference cannot claim private source coverage");
    std::stop_source cancellation;cancellation.request_stop();bool cancelled=false;
    try{(void)simulation::detail::amplitude_probability(useful.modem,search,40,0,256,cancellation.get_token());}
    catch(const estimate_detail::Cancelled&){cancelled=true;}
    require(cancelled,"amplitude source/reference work must honor cancellation");
    std::cout<<"amplitude probability reference tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
