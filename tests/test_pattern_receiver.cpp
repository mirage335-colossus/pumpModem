#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/channel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <string>

using namespace datapump;
namespace {
void check(bool condition,const char* message) { if(!condition)throw Error(message); }
template<class Work>void rejects(Work work,const char* message) {
    try { work(); } catch(const Error&) { return; }
    throw Error(message);
}
modem::Config config(unsigned chips=128,bool keyed=false) {
    modem::Config c;c.pattern_symbols=true;c.constellation_bits=1;
    c.spreading_factor=chips;c.scramble=keyed;c.stream_epoch=1789312671;
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);
    return c;
}
// The receiver gets real noisy PCM only: no source bits, start, phase, count,
// aligned symbol observations or a transmitter callback reach acquisition.
std::vector<float> waveform(const modem::Config& c,const Bytes& bits,std::size_t delay,
                            std::size_t trailing,double phase,double sigma=0,std::uint64_t seed=173) {
    // Bare payload models capture after the hardware lead-in was lost.
    modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(tx.total_samples()));tx.read_analytic(analytic);
    std::vector<float> samples(delay+analytic.size()+trailing);
    std::mt19937_64 random(seed);std::normal_distribution<double> noise(0,sigma);
    for(auto& sample:samples)sample=static_cast<float>(noise(random));
    for(std::size_t i=0;i<analytic.size();++i)samples[delay+i]+=static_cast<float>((analytic[i]*std::polar(1.,phase)).real());
    return samples;
}
struct Reception {
    std::vector<modem::PatternBurst> bursts;
    std::vector<modem::PatternEvidence> candidates;
    std::vector<std::complex<double>> points;
    std::size_t peak_working=0;
};
Reception receive(const std::vector<float>& samples,const modem::Config& c,
                  std::span<const std::size_t> chunks,modem::PatternSearch search={},std::size_t workspace=8*1024*1024) {
    modem::PatternReceiver receiver(c,workspace,search);Reception result;
    const auto drain=[&] {
        const auto work=receiver.working_bytes();result.peak_working=std::max(result.peak_working,work);
        check(work<=workspace,"receiver exceeded its declared working-memory limit");
        check(receiver.candidates().size()<=search.candidate_limit,"candidate history exceeded its limit");
        auto bursts=receiver.take_bursts();
        result.bursts.insert(result.bursts.end(),std::make_move_iterator(bursts.begin()),std::make_move_iterator(bursts.end()));
    };
    for(std::size_t position=0,chunk=0;position<samples.size();++chunk) {
        const auto count=std::min(chunks[chunk%chunks.size()],samples.size()-position);
        receiver.push(std::span(samples).subspan(position,count));position+=count;drain();
    }
    receiver.finish();drain();receiver.finish();
    check(receiver.take_bursts().empty(),"finish emitted a duplicate burst");
    result.candidates=receiver.candidates();result.points=receiver.take_chip_constellation();
    check(result.points.size()<=2048 && receiver.take_chip_constellation().empty(),"chip constellation is not bounded/drainable");
    rejects([&]{receiver.push({});},"receiver accepted samples after finish");
    return result;
}
const modem::PatternBurst& exact(const Reception& result,const Bytes& bits) {
    if(result.bursts.size()!=1) {
        std::string detail="expected one detected burst, got "+std::to_string(result.bursts.size());
        for(const auto& burst:result.bursts) {
            detail+=" ["+std::to_string(burst.first_sample)+","+std::to_string(burst.end_sample)+") "+
                std::to_string(burst.bits.size())+"bits@"+std::to_string(burst.frequency_hz)+"/stream"+std::to_string(burst.first_stream_symbol)+" ";
            for(std::size_t i=0;i<std::min<std::size_t>(32,burst.bits.size());++i)detail+=burst.bits[i]?'1':'0';
        }
        throw Error(detail);
    }
    if(result.bursts.front().bits!=bits) {
        std::string observed;for(auto bit:result.bursts.front().bits)observed+=bit?'1':'0';
        throw Error("detected bits differ from exact payload: "+observed);
    }
    check(result.bursts.front().complete,"observed post-signal noise did not close the burst");
    return result.bursts.front();
}
void exact_blind_bits() {
    constexpr std::array<std::size_t,6> chunks{1,7,131,19,503,47};
    for(bool keyed:{false,true})for(double phase:{.27,1.73,3.11}) {
        const auto c=config(128,keyed);const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        const auto result=receive(waveform(c,{0,0,1},137,2*symbol,phase,.03),c,chunks);
        const auto& burst=exact(result,{0,0,1});
        check(burst.first_sample>=127 && burst.first_sample<=147,"blind start estimate missed arbitrary PCM delay");
        check(burst.end_sample>=137+3*symbol-10 && burst.end_sample<=137+3*symbol+10,"raw burst gained training or a padding symbol");
    }
}
void short_pattern_sample_timing() {
    constexpr std::array<std::size_t,3> chunks{13,97,7};
    const Bytes bits{0,1,0,0,1};
    for(unsigned chips:{3U,4U,6U,8U,12U,16U}) {
        auto c=config(chips);c.carrier_hz=1200;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        for(std::size_t delay=135;delay<=139;++delay)for(double phase:{.27,1.73,3.11}) {
            try {
                const auto result=receive(waveform(c,bits,delay,3*symbol,phase,.001),c,chunks);
                const auto& burst=exact(result,bits);
                // A hop can admit a nearby start before the exact peak is
                // available. Continuation must still recover the exact end.
                check(burst.first_sample+2>=delay && burst.first_sample<=delay+2 &&
                      burst.end_sample==delay+bits.size()*symbol,
                      "short circular patterns must retain all bits, a start within two samples, and the exact endpoint");
            } catch(const Error& error) {
                throw Error(std::to_string(chips)+" chips / delay "+std::to_string(delay)+
                            " / phase "+std::to_string(phase)+": "+error.what());
            }
        }
    }
}
void short_pattern_wrong_key_and_noise() {
    auto c=config(16,true);c.carrier_hz=1200;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    constexpr std::array<std::size_t,3> chunks{13,97,7};
    const Bytes bits{0,1,0,0,1};
    exact(receive(waveform(c,bits,137,3*symbol,.73,.001),c,chunks),bits);
    auto wrong=c;wrong.spreading_seed[7]^=0x80;
    Bytes long_bits(300);std::mt19937_64 random(713);
    for(auto& bit:long_bits)bit=static_cast<std::uint8_t>(random()&1U);
    const auto samples=waveform(c,long_bits,137,3*symbol,.73,.001);
    check(receive(samples,wrong,chunks).bursts.empty(),
          "short-pattern acquisition must not admit a different key");
    std::normal_distribution<double> noise(0,std::sqrt(modem::nominal_signal_power));
    std::vector<float> background(300*symbol);
    for(auto& sample:background)sample=static_cast<float>(noise(random));
    check(receive(background,c,chunks).bursts.empty(),
          "private short-template projection must not admit independent real Gaussian noise");
    c.scramble=false;
    check(receive(background,c,chunks).bursts.empty(),
          "exact public short-template projection must not inflate independent real Gaussian noise into a burst");
}
void private_template_energy_normalization() {
    // Select a valid secret template with substantially less than unit mean
    // energy. A perfectly matching observation must still explain essentially
    // all its energy; using the number of chips as the norm penalizes this key.
    for(unsigned mode=1;mode<=3;++mode) {
        auto c=config(16,(mode&1U)!=0);c.dsss=(mode&2U)!=0;c.dsss_seed[11]=139;
        c.carrier_hz=1200;
        double mean_energy=1;
        for(unsigned trial=0;trial<1000 && mean_energy>=.75;++trial,++c.stream_epoch) {
            modem::PatternCode code(c,c.stream_epoch);mean_energy=0;
            for(std::uint64_t chip=0;chip<code.chips_per_symbol();++chip)
                mean_energy+=std::norm(code.value(chip,0));
            mean_energy/=static_cast<double>(code.chips_per_symbol());
        }
        --c.stream_epoch;
        check(mean_energy<.75,"private template fixture must have a below-average energy norm");
        modem::PatternSearch search;search.frequency_offsets_hz={0};search.initial_stream_symbols=1;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        constexpr std::array<std::size_t,3> chunks{13,97,7};
        const auto result=receive(waveform(c,{0,1},0,2*symbol,.31),c,chunks,search);
        exact(result,{0,1});
        // At this carrier, each private half-chip observation contains a full
        // carrier cycle, so noiseless PCM must explain essentially all energy.
        const auto degrees=static_cast<double>(2*modem::pattern_chips_per_symbol(c)-1);
        const auto first=std::find_if(result.candidates.begin(),result.candidates.end(),[](const auto& item) {
            return item.first_sample==0 && item.stream_symbol==0;
        });
        check(first!=result.candidates.end() && first->score>25*degrees,
              "private waveform evidence must use its measured template energy");
    }
}
void changing_chunks_and_late_start() {
    const auto c=config();const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto samples=waveform(c,{0,0,1},37*symbol+143,2*symbol,2.81,.04);
    const std::array<std::size_t,1> whole{samples.size()};const std::array<std::size_t,5> changing{17,1,233,3,1024};
    const auto a=receive(samples,c,whole),b=receive(samples,c,changing);
    const auto& first=exact(a,{0,0,1});const auto& second=exact(b,{0,0,1});
    check(first.first_sample==second.first_sample && first.end_sample==second.end_sample && first.score==second.score,
          "PCM push chunk boundaries changed acquisition evidence");
}
void weak_prefix_cannot_borrow_payload_confidence() {
    const auto c=config(64);const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    constexpr std::size_t delay=137;
    auto samples=waveform(c,{1,0,0,1},delay,3*symbol,.73);
    std::mt19937_64 random(197);std::normal_distribution<double> noise(0,2.);
    for(std::size_t i=0;i<delay+symbol;++i)samples[i]+=static_cast<float>(noise(random));
    constexpr std::array<std::size_t,4> chunks{137,503,17,1021};
    const auto result=receive(samples,c,chunks);
    const auto& burst=exact(result,{0,0,1});
    check(burst.first_sample>=delay+symbol-10,
          "a strong payload symbol must not retroactively confirm a weak candidate before its start");
}
void unconfirmed_tail_cannot_veto_later_start() {
    auto c=config(64);c.bandwidth_hz=100;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto chip=static_cast<std::size_t>(modem::pattern_chip_samples(c));
    // Two admitted symbols followed by a weak partial extension overlap an
    // independently timed strong burst. Noise is added to the actual circular
    // template: old random real-sign fixtures no longer resemble this waveform.
    // Disabling the pending-tail admission guard loses the entire later burst
    // in the first fixture and its first bit in the second.
    struct Fixture { std::uint64_t seed; double sigma; unsigned shift_chips; };
    for(const auto fixture:{Fixture{1,.9,24},Fixture{15,1.3,16}}) {
        const auto before=2*symbol+fixture.shift_chips*chip;
        const Bytes bits{0,0,1};auto samples=waveform(c,bits,before,2*symbol,0);
        const auto earlier=waveform(c,{1,0,1},0,0,0);
        std::mt19937_64 random(fixture.seed);std::normal_distribution<double> noise(0,fixture.sigma);
        for(std::size_t i=0;i<before;++i)
            samples[i]=earlier[i]+(i>=symbol?static_cast<float>(noise(random)):0);
        constexpr std::array<std::size_t,3> chunks{509,37,1021};
        const auto result=receive(samples,c,chunks);
        check(result.bursts.size()==2,"weak-tail fixture must retain exactly two confirmed spans");
        const auto& first=result.bursts.front();const auto& later=result.bursts.back();
        check(first.bits==Bytes({1,0}) && first.first_sample==0 && first.end_sample==2*symbol,
              "weak pending evidence must not extend the earlier confirmed span");
        check(later.bits==bits && later.first_sample==before && later.end_sample==before+3*symbol,
              "an admitted candidate's weak tail must not discard a stronger later signal's first symbol");
    }
}
void noise_hidden_chips() {
    const auto c=config(4096,true);const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto sigma=std::sqrt(modem::nominal_signal_power*std::pow(10.,16./10.));
    constexpr std::array<std::size_t,3> chunks{1021,997,512};
    const auto result=receive(waveform(c,{0,0,1},173,2*symbol,.91,sigma,927),c,chunks);exact(result,{0,0,1});
    std::size_t off_axis=0;for(auto point:result.points)off_axis+=std::abs(point.imag())>.25*std::abs(point);
    check(!result.points.empty() && off_axis>result.points.size()/2,"reported chip observations were snapped to legal points");
}
void wrong_key_and_background() {
    const auto c=config(256,true);const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto samples=waveform(c,{0,0,1},131,3*symbol,1.29,.5);constexpr std::array<std::size_t,2> chunks{613,79};
    auto wrong=c;wrong.spreading_seed[5]^=0xa7;
    check(receive(samples,wrong,chunks).bursts.empty(),"wrong pattern key admitted a payload");
    wrong=c;++wrong.stream_epoch;
    check(receive(samples,wrong,chunks).bursts.empty(),"wrong pattern epoch admitted a payload");
    for(std::uint64_t seed:{17,731,9011}) {
        std::mt19937_64 random(seed);std::normal_distribution<double> noise;std::vector<float> background(40*symbol);
        for(auto& sample:background)sample=static_cast<float>(noise(random));
        check(receive(background,c,chunks).bursts.empty(),"finite noise-only capture admitted a message");
    }
    // Finite captures do not calibrate a claimed 1e-8 lifetime false-alarm rate.
}
void multiple_bursts() {
    const auto c=config();const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    auto samples=waveform(c,{0,0,1},137,3*symbol,.73,.02,32);
    const auto second=waveform(c,{1,0},311,3*symbol,2.1,.02,47);samples.insert(samples.end(),second.begin(),second.end());
    constexpr std::array<std::size_t,3> chunks{17,1024,401};const auto result=receive(samples,c,chunks);
    if(result.bursts.size()!=2) {
        std::string detail="separate transmissions produced "+std::to_string(result.bursts.size())+" bursts";
        for(const auto& burst:result.bursts){detail+=" ["+std::to_string(burst.first_sample)+","+std::to_string(burst.end_sample)+") ";for(auto bit:burst.bits)detail+=bit?'1':'0';}
        for(const auto& candidate:result.candidates)if(candidate.first_sample>7000 && candidate.score>30)
            detail+=" candidate="+std::to_string(candidate.first_sample)+":"+std::to_string(candidate.score);
        throw Error(detail);
    }
    check(result.bursts[0].bits==Bytes({0,0,1}) && result.bursts[1].bits==Bytes({1,0}),"independent burst payloads differ");
    check(result.bursts[0].end_sample<result.bursts[1].first_sample,"separate burst measurements overlap");
}
void bounds_and_cancellation() {
    auto c=config(64);modem::PatternSearch search;search.candidate_limit=7;search.track_limit=2;search.bit_limit=32;
    constexpr std::size_t workspace=256*1024;const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto samples=waveform(c,{0,0,1},137,40*symbol,.91,.1);constexpr std::array<std::size_t,1> chunks{127};
    const auto result=receive(samples,c,chunks,search,workspace);exact(result,{0,0,1});
    check(result.candidates.size()<=7 && result.peak_working<=workspace,"small memory/candidate caps were not respected");
    rejects([&]{modem::PatternReceiver small(c,1024);},"impossible FFT workspace accepted");
    auto invalid_search=search;invalid_search.start_offset_seconds=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{modem::PatternReceiver invalid(c,workspace,invalid_search);},"FFT mode ignored a nonfinite clock origin");
    invalid_search=search;invalid_search.clock_errors_ppm={std::numeric_limits<double>::infinity()};
    rejects([&]{modem::PatternReceiver invalid(c,workspace,invalid_search);},"FFT mode ignored a nonfinite clock-rate hypothesis");
    c.integration_seconds=4*3600;
    rejects([&]{modem::PatternReceiver long_symbol(c,workspace,search);},"multi-hour FFT ignored its memory ceiling");
    c=config();modem::PatternReceiver receiver(c);std::stop_source stop;stop.request_stop();
    rejects([&]{receiver.push(samples,stop.get_token());},"receiver ignored cancelled push");
    check(receiver.candidates().empty() && receiver.take_bursts().empty(),"cancelled push retained unobserved evidence");
    rejects([&]{receiver.finish(stop.get_token());},"receiver ignored cancelled finish");
    rejects([&]{const std::array<float,1> invalid{std::numeric_limits<float>::quiet_NaN()};receiver.push(invalid);},"nonfinite PCM accepted");
}
void fractional_symbol_timing() {
    for(bool keyed:{false,true}) {
        auto c=config(128,keyed);c.bandwidth_hz=1100;c.integration_seconds=.029;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        const Bytes bits{0,0,1,0,1,1,0,0,1,1,0,1};constexpr std::array<std::size_t,3> chunks{11,239,71};
        exact(receive(waveform(c,bits,139,3*symbol,1.1,.01),c,chunks),bits);
    }
}
void keyed_capture_missing_first_symbol() {
    const auto c=config(256,true);const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    auto samples=waveform(c,{1,0,0,1},0,3*symbol,.37,.03);samples.erase(samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(symbol));
    constexpr std::array<std::size_t,3> chunks{127,19,503};const auto result=receive(samples,c,chunks);
    check(exact(result,{0,0,1}).first_stream_symbol==1,"late keyed burst lost its data-keystream position");
}
void independent_sampled_channel() {
    for(bool keyed:{false,true})for(std::size_t length:{std::size_t{3},std::size_t{1536}}) {
        auto c=config(64,keyed);Bytes bits(length);std::mt19937 random(731);
        for(auto& bit:bits)bit=static_cast<std::uint8_t>(random()&1U);
        modem::StreamingTransmitter transmitter(modem::RawBits{bits},c);
        modem::ChannelConfig impairment;impairment.snr_db=6;impairment.clock_error_ppm=100;
        impairment.phase_noise_degrees_per_sqrt_second=.5;impairment.seed=1;
        modem::SampledSimulationChannel channel(c,impairment);
        std::array<float,509> block{};std::vector<float> samples;
        while(const auto count=channel.read(transmitter,block))samples.insert(samples.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
        const auto tail=static_cast<std::size_t>(3*modem::symbol_sample_count(c));
        const auto offset=samples.size();samples.resize(offset+tail);channel.read_noise(std::span(samples).subspan(offset));
        constexpr std::array<std::size_t,4> chunks{137,503,17,1021};
        try { exact(receive(samples,c,chunks),bits); }
        catch(const Error& error){throw Error(std::string(keyed?"keyed ":"public ")+std::to_string(length)+": "+error.what());}
    }
}
void shared_projection_and_workspace_update() {
    const auto c=config(64);const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto samples=waveform(c,{0,0,1},137,3*symbol,.83,.04);
    modem::PatternSearch search;search.candidate_limit=7;search.track_limit=2;search.bit_limit=32;
    modem::PatternReceiver projected(c,512*1024,search);
    projected.set_workspace_bytes(256*1024);
    rejects([&]{projected.set_workspace_bytes(1024);},"workspace reduction discarded live transform state");
    std::vector<std::complex<double>> common(samples.size());
    for(std::size_t i=0;i<samples.size();++i)common[i]=static_cast<double>(samples[i])*std::polar(1.,
        -2*std::numbers::pi*c.carrier_hz*static_cast<double>(i)/c.sample_rate+.73);
    rejects([&]{projected.push(samples,std::span(common).first(common.size()-1));},"shared projection accepted mismatched lengths");
    projected.push(samples,common);projected.finish();
    check(projected.working_bytes()<=256*1024,"workspace reduction did not cap future retained state");
    const auto bursts=projected.take_bursts();
    check(bursts.size()==1 && bursts.front().bits==Bytes({0,0,1}),"shared carrier projection changed decoded bits");
    check(projected.diagnostics().pattern_score && projected.diagnostics().snr_db==0,
          "pattern evidence was reported as measured SNR");
    check(!projected.synchronized(),"completed transmission left pattern lock set through silence");
    search.bit_limit=2;modem::PatternReceiver limited(c,256*1024,search);
    rejects([&]{limited.push(samples);limited.finish();},"bit-cap exhaustion silently truncated a valid signal");
}
void short_pattern_shared_projection_phase() {
    auto c=config(16);c.carrier_hz=1200;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const Bytes bits{0,1,0,0,1};
    const auto samples=waveform(c,bits,137,3*symbol,.31,.001);
    std::vector<std::complex<double>> common(samples.size());
    for(std::size_t i=0;i<samples.size();++i)common[i]=static_cast<double>(samples[i])*std::polar(1.,
        -2*std::numbers::pi*c.carrier_hz*static_cast<double>(i)/c.sample_rate+.73);
    modem::PatternReceiver direct(c),projected(c);
    direct.push(samples);projected.push(samples,common);direct.finish();projected.finish();
    const auto original=direct.take_bursts(),shared=projected.take_bursts();
    check(original.size()==1 && shared.size()==1 && original.front().bits==bits && shared.front().bits==bits,
          "short-pattern Gram fitting must preserve the shared projection's arbitrary constant phase");
    check(original.front().first_sample==shared.front().first_sample &&
          original.front().end_sample==shared.front().end_sample && original.front().score==shared.front().score,
          "sharing a carrier projection must not change exact short-template acquisition evidence");
}
void long_clock_window_fallback() {
    auto c=config(64,true);c.integration_seconds=3600;
    modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};search.clock_errors_ppm={0};search.track_limit=2;search.candidate_limit=7;search.bit_limit=32;
    modem::PatternReceiver receiver(c,256*1024,search);
    check(receiver.working_bytes()<=256*1024,"hour-long clock-window fallback allocated duration-sized state");
    receiver.set_workspace_bytes(256*1024);
    std::array<float,31> quiet{};receiver.push(quiet);
    check(receiver.take_bursts().empty(),"incomplete hour-long observation fabricated a symbol");
    std::stop_source stop;stop.request_stop();
    rejects([&]{receiver.push(quiet,stop.get_token());},"long correlation fallback ignored cancellation");
}
void hardware_settling_is_not_payload() {
    constexpr std::array<std::size_t,3> chunks{509,37,1021};
    constexpr std::size_t workspace=2*1024*1024;
    constexpr std::array<const char*,4> modes{"public","scrambler only","DSSS only","scrambler and DSSS"};
    const auto verify=[&](double bandwidth,unsigned chips,std::uint64_t epoch,unsigned mode,bool data=false) {try {
        auto c=config(chips,(mode&1U)!=0);c.dsss=(mode&2U)!=0;
        c.bandwidth_hz=bandwidth;c.stream_epoch=epoch;
        for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11);
        if(data){
            std::array<std::uint8_t,32> key{};
            for(std::size_t i=0;i<key.size();++i)key[i]=static_cast<std::uint8_t>(7*i+13);
            c.data_key.emplace(key);
        }
        const Bytes bits{0,0,1};
        modem::PatternTransmitter source(bits,c,c.stream_epoch);
        std::vector<float> samples(static_cast<std::size_t>(source.total_samples()));source.read(samples);
        c.data_key.reset(); // Acquisition needs no knowledge of the settling Data stream.
        const auto prefix=static_cast<std::size_t>(modem::training_sample_count(c));
        check(prefix>0,"hardware-settling fixture must contain a physical prefix");
        const std::vector<float> settling(samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(prefix));
        check(receive(settling,c,chunks,{},workspace).bursts.empty(),"hardware settling must not become extra decoded payload bits");
        samples.resize(samples.size()+2*modem::symbol_sample_count(c));
        const auto result=receive(samples,c,chunks,{},workspace);
        const auto& burst=exact(result,bits);
        check(burst.first_sample>=prefix-10 && burst.first_sample<=prefix+10,
              "acquisition must select the payload pattern start, not the hardware lead-in");
        check(burst.first_stream_symbol==0,"hardware settling must not advance the payload stream index");
        const std::vector<float> without_prefix(samples.begin()+static_cast<std::ptrdiff_t>(prefix),samples.end());
        const auto late_result=receive(without_prefix,c,chunks,{},workspace);
        const auto& late=exact(late_result,bits);
        check(late.first_sample<=10 && late.first_stream_symbol==0,
              "losing the entire hardware prefix must preserve payload acquisition and stream position");
    } catch(const Error& error){throw Error(std::string(modes[mode])+(data?" with preamble Data":"")+
        " / "+std::to_string(chips)+" chips / "+std::to_string(bandwidth)+" Hz / epoch "+
        std::to_string(epoch)+": "+error.what());}};
    for(const auto bandwidth:{100.,1200.})for(const auto chips:{64U,128U})
    for(const auto epoch:{1800000025ULL,1800000174ULL})for(unsigned mode=0;mode<modes.size();++mode)
        verify(bandwidth,chips,epoch,mode);
    for(const auto mode:{0U,3U})verify(100.,64,1800000174ULL,mode,true);
}
}
int main() {
    unsigned failures=0;
    const auto run=[&](const char* name,auto test) {
        try { test();std::cout<<name<<": passed\n"; }
        catch(const std::exception& error){++failures;std::cerr<<name<<": "<<error.what()<<'\n';}
    };
    run("exact blind bits",exact_blind_bits);run("chunk invariance and late start",changing_chunks_and_late_start);
    run("short pattern sample timing",short_pattern_sample_timing);
    run("short pattern wrong keys and noise",short_pattern_wrong_key_and_noise);
    run("private template energy normalization",private_template_energy_normalization);
    run("weak prefix confidence",weak_prefix_cannot_borrow_payload_confidence);
    run("unconfirmed tail and later start",unconfirmed_tail_cannot_veto_later_start);
    run("noise-hidden chip observations",noise_hidden_chips);run("wrong keys and finite noise captures",wrong_key_and_background);
    run("multiple bursts",multiple_bursts);run("memory limits and cancellation",bounds_and_cancellation);
    run("fractional symbol timing",fractional_symbol_timing);run("keyed capture missing first symbol",keyed_capture_missing_first_symbol);
    run("independent sampled crystal and phase",independent_sampled_channel);
    run("shared projection and workspace updates",shared_projection_and_workspace_update);
    run("short pattern shared projection phase",short_pattern_shared_projection_phase);
    run("bounded long clock-window fallback",long_clock_window_fallback);
    run("hardware settling remains outside payload",hardware_settling_is_not_payload);
    return failures?1:0;
}
