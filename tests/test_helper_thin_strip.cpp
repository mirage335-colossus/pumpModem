#include "../src/live_helper_prior.hpp"
#include "datapump/channel.hpp"
#include "datapump/receiver_strategy.hpp"
#include <array>
#include <iostream>
#include <map>
#include <string_view>

using namespace datapump;
namespace {
void check(bool value,const char* reason){if(!value)throw Error(reason);}
modem::Config config() {
    modem::Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=200;
    c.spreading_factor=32;c.scramble=true;c.stream_epoch=1800000003;
    c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,.5},{1,.05},3e7,3};
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=static_cast<std::uint8_t>(7+11*i);
    return c;
}
constexpr double helper_hz=2212.5;
live::detail::HelperPriorMask mask(const modem::Config& c,const modem::OscillatorPatternSearch& original,
                                  double cfo,double clock) {
    return live::detail::helper_prior_mask(original.hypotheses,cfo+helper_hz*clock*1e-6,1e-5,
        c.carrier_hz-helper_hz,.125*c.sample_rate/static_cast<double>(modem::symbol_sample_count(c)),
        original.clock_step_ppm/2,original.requested_clock_half_width_ppm);
}
void geometry() {
    const auto c=config();const auto original=modem::oscillator_pattern_search(c);
    check(original.hypotheses.size()==233 && !original.limited,
          "thin-strip fixture changed the complete ordinary233-pair catalogue");
    check(original.clock_step_ppm/2>=600 && original.clock_step_ppm/2<600.001,
          "thin-strip fixture lost the original represented clock-cell radius");
    const auto result=mask(c,original,0,0);
    std::cout<<"thin_strip pairs="<<original.hypotheses.size()<<" rates="<<result.rates
             <<" selected="<<result.selected<<" measured="<<result.measured_selected
             <<" represented="<<result.represented_rates<<" radius="<<result.radius_hz<<'\n';
    check(result.admitted && result.rates==3 && result.represented_rates==3 && result.selected==3,
          "qualified near-zero helper failed conservative3/233 original-cell acquisition");
    for(const double cfo:{-.05,0.,.05})for(const double clock:{-300.,300.}) {
        const auto selected=mask(c,original,cfo,clock);
        check(selected.admitted && selected.represented_rates==3 && selected.selected<original.hypotheses.size(),
              "thin helper discarded an unresolved original audio-clock cell");
    }
    const auto wide=live::detail::helper_prior_mask(original.hypotheses,0,1000,-712.5,.1,600,300);
    check(!wide.admitted,"unshifted full mask invented acquisition-work reduction");
    const std::array<modem::PatternFrequencyRateHypothesis,2> empty{{{-10,-300},{10,300}}};
    const auto absent=live::detail::helper_prior_mask(empty,0,0,0,.1,0,300);
    check(!absent.admitted && !absent.measured_selected,"empty measured strip invented a reference cell");
    const std::array<modem::PatternFrequencyRateHypothesis,4> ties{{{0,0},{-1,300},{1,300},{8,0}}};
    const auto tied=live::detail::helper_prior_mask(ties,0,0,0,.1,300,300);
    check(tied.admitted && tied.selected==3 && tied.cells[0] && tied.cells[1] && tied.cells[2] && !tied.cells[3],
          "nearest-clock completion discarded an equidistant original frequency tie");
    const std::array<modem::PatternFrequencyRateHypothesis,3> tiny{{{-1,-300},{0,0},{1,300}}};
    const auto aligned=live::detail::helper_prior_mask(tiny,30,0,0,1.1,300,300);
    check(aligned.admitted && aligned.origin_hz==30 && aligned.selected==tiny.size(),
          "shifted full catalogue lost RF alignment without acquisition-work reduction");
    std::array<modem::PatternFrequencyRateHypothesis,65> coupled{};
    for(std::size_t i=0;i<coupled.size();++i)coupled[i]={static_cast<double>(i)-32,(static_cast<double>(i)-32)*9.375};
    const auto no_gain=live::detail::helper_prior_mask(coupled,0,0,0,.1,0,300);
    check(!no_gain.admitted,"distinct-clock coupled catalogue lost original clock coverage");
}
void receive(double cfo,double clock) {
    const auto c=config();const auto original=modem::oscillator_pattern_search(c);const auto selected=mask(c,original,cfo,clock);
    check(selected.admitted,"actual thin-strip receiver did not receive a qualified original-cell mask");
    modem::PatternSearch search;search.hypotheses=original.hypotheses;search.worker_threads=1;
    search.admitted_frequency_half_width_hz=original.frequency.requested_half_width_hz;
    search.admitted_clock_half_width_ppm=original.requested_clock_half_width_ppm;
    search.admitted_clock_cell_half_width_ppm=original.clock_step_ppm/2;
    search.initial_stream_symbols=2;search.chunk_bits=1;
    const auto policy=modem::resolve_receiver_strategy(c,search,32*1024*1024);
    check(policy.supported && policy.backend==modem::ReceiverBackend::existing_complex,
          "thin-strip fixture changed ordinary Auto backend policy");
    modem::StreamingReceiver receiver(c,32*1024*1024,search);
    check(receiver.set_acquisition_prior(selected.origin_hz,std::span(selected.cells).first(original.hypotheses.size())),
          "actual ordinary receiver rejected the conservative thin-strip cells");
    const Bytes expected{1,0,1,1,0,0,1,0};
    modem::StreamingTransmitter source(modem::RawBits{expected},c,32*1024*1024);
    modem::ChannelConfig effects;effects.snr_db=35;effects.frequency_offset_hz=cfo;
    effects.clock_error_ppm=clock;effects.phase_noise_degrees_per_sqrt_second=0;effects.seed=5181;
    modem::SampledSimulationChannel channel(c,effects);
    struct Progress {Bytes bits;bool complete=false;std::uint64_t at=0,end=0;};
    std::map<std::pair<std::uint64_t,std::uint64_t>,Progress> streams;
    std::array<float,137> pcm{};std::uint64_t observed=0;
    const auto drain=[&] {
        for(const auto& burst:receiver.take_pattern_bursts()) {
            auto& stream=streams[{burst.stream_first_sample,burst.stream_first_symbol}];
            stream.bits.insert(stream.bits.end(),burst.bits.begin(),burst.bits.end());
            if(burst.stream_first_symbol==0)check(stream.bits.size()<=expected.size() &&
                std::equal(stream.bits.begin(),stream.bits.end(),expected.begin()),
                "thin-strip ordinary progress changed the exact source prefix");
            if(burst.complete){stream.complete=true;stream.at=observed;stream.end=burst.end_sample;}
        }
    };
    const auto feed=[&](std::span<const float> input) {
        for(std::size_t at=0;at<input.size();) {
            if(receiver.acquisition_state().ready) {
                receiver.process_prior_acquisition();receiver.resolve_acquisition(true);drain();continue;
            }
            const auto count=receiver.observe_acquisition(input.subspan(at));
            check(count,"actual thin-strip observation failed to advance");at+=count;observed+=count;drain();
        }
    };
    for(;;){const auto count=channel.read(source,pcm);if(!count)break;feed(std::span(pcm).first(count));}
    for(std::uint64_t remaining=10ULL*c.sample_rate;remaining;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(remaining,pcm.size()));
        channel.read_noise(std::span(pcm).first(count));feed(std::span(pcm).first(count));remaining-=count;
    }
    if(receiver.acquisition_state().ready){receiver.process_prior_acquisition();receiver.resolve_acquisition(true);drain();}
    bool exact=false;
    for(const auto& [identity,stream]:streams)if(identity.second==0 && stream.complete && stream.bits==expected) {
        check(stream.at>=stream.end+modem::pattern_absence_samples(c),
              "thin-strip receiver completed before fully scored physical absence");exact=true;
    }
    check(exact,"thin-strip ordinary Auto reception lost complete source bits or physical END");
    std::cout<<"thin_strip_receive cfo="<<cfo<<" clock="<<clock<<" exact_bits="<<expected.size()
             <<" conditional_jobs="<<receiver.fft_work().conditional_template_jobs<<'\n';
}
}
int main(int argc,char** argv){try {
    geometry();if(argc==2 && std::string_view(argv[1])=="--mask-only")return 0;
    check(argc==1,"unknown thin-strip fixture mode");
    for(const double cfo:{-.05,0.,.05})for(const double clock:{-300.,300.})receive(cfo,clock);
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
