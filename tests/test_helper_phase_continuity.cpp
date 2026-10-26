#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/fhss_schedule.hpp"
#include "../src/live_helper.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <random>
#include <vector>

using namespace datapump;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool value,const char* message) {if(!value)throw Error(message);}

void signed_sample_dates() {
    const auto verify=[](std::uint64_t anchor) {
        const live::detail::HelperPhaseContinuation model{anchor,.73,1.5625};
        check(model.at(anchor,2000)==.73,"phase continuation changed its anchor");
        for(const auto offset:std::array<std::uint64_t,3>{1,3,101}) {
            const auto change=tau*model.frequency_hz*offset/2000;
            check(std::abs(model.at(anchor-offset,2000)-(.73-change))<1e-13,
                  "phase continuation lost a sample date before its anchor");
            check(std::abs(model.at(anchor+offset,2000)-(.73+change))<1e-13,
                  "phase continuation lost a sample date after its anchor");
        }
    };
    verify(1000);verify(std::numeric_limits<std::uint64_t>::max()-1000);
    bool rejected=false;
    try {live::detail::HelperPhaseContinuation{}.at(0,0);}catch(const Error&) {rejected=true;}
    check(rejected,"phase continuation accepted a missing sample clock");
}

void optional_geometry_and_audits() {
    using live::detail::helper_audit_deadline;
    using live::detail::helper_audit_period;
    check(helper_audit_period(4000,8000)==64000,"helper audit cadence is not eight long symbols");
    check(helper_audit_period(4000,100)==24000,"helper audit cadence lost its six second minimum");
    check(helper_audit_deadline(12000,4000,8000)==76000,"helper audit deadline changed sample coordinates");
    check(helper_audit_period(4000,UINT64_MAX)==UINT64_MAX &&
          helper_audit_deadline(UINT64_MAX-1,4000,100)==UINT64_MAX,
          "helper audit arithmetic wrapped");
    live::Settings settings;
    auto& config=settings.transfer.modem;
    config.sample_rate=6000;config.carrier_hz=1500;config.bandwidth_hz=400;
    config.oscillator_search.reset();
    check(!live::detail::helper_occupied_receive_band(settings),
          "unrepresented default clock expansion did not retain the raw helper fit");
    config.oscillator_search=modem::OscillatorSearchConfig{{0,0},{0,0},1e8,1};
    const auto single=live::detail::helper_occupied_receive_band(settings);
    const auto half=helper::occupied_bandwidth(config)/2;
    check(single && single->lower_hz==1500-half && single->upper_hz==1500+half,
          "public helper conditioning band changed nominal payload geometry");
    settings.long_message_modem=config;settings.long_message_modem->bandwidth_hz=800;
    const auto both=live::detail::helper_occupied_receive_band(settings);
    check(both && both->lower_hz<single->lower_hz && both->upper_hz>single->upper_hz,
          "helper conditioning omitted the long-message public band");
    config.carrier_hz=0;
    check(!live::detail::helper_occupied_receive_band(settings),
          "invalid helper conditioning band did not fall back to the raw fit");

    config.carrier_hz=1500;settings.long_message_modem.reset();
    settings.transfer.automatic_receive_profiles=true;
    settings.transfer.receive_pattern_mode=tuning::PatternMode::auto_keystream;
    settings.transfer.receive_targets_db_hz={8,18};
    const std::array<std::uint8_t,32> key{};
    settings.receive_keys.emplace_back(key);
    auto hopping=std::make_shared<modem::FhssPlan>();
    hopping->channels=2;hopping->shifts[0]=1e6;hopping->shifts[1]=2e6;
    hopping->dwell_seconds=.4;hopping->power_ramps=hopping->smooth_ramps=true;
    hopping->ramp_seconds=.02;
    config.fake_hopping=hopping;
    const auto keyed_only=live::detail::helper_occupied_receive_band(settings);
    check(keyed_only.has_value(),"receive-only keys were omitted from the public helper conditioning band");
    for(const bool keyed:{false,true})for(const auto& profile:tuning::receive_profiles(config,
            settings.transfer.receive_targets_db_hz,settings.transfer.receive_pattern_mode,keyed)) {
        const auto support=helper::occupied_bandwidth(profile)/2;
        check(keyed_only->lower_hz<=profile.carrier_hz-support &&
              keyed_only->upper_hz>=profile.carrier_hz+support,
              "public helper conditioning omitted a keyed/plaintext receive profile");
    }
    settings.receive_keys.clear();
    const auto plain_only=live::detail::helper_occupied_receive_band(settings);
    check(plain_only && keyed_only->lower_hz<plain_only->lower_hz &&
          keyed_only->upper_hz>plain_only->upper_hz,
          "receive-only keyed FHSS gate support did not widen the public conditioning union");
    settings.transfer.automatic_receive_profiles=false;
    config.fake_hopping.reset();config.oscillator_search->lf.accuracy_ppm=100;
    settings.long_message_modem=config;settings.long_message_modem->fake_hopping=hopping;
    const auto& long_profile=*settings.long_message_modem;
    const auto long_domain=modem::oscillator_pattern_search(long_profile,
        modem::utc_transmit_rate_limit(long_profile));
    const auto long_half=helper::occupied_bandwidth(long_profile)/2;
    const auto long_uncertainty=long_domain.frequency.requested_half_width_hz+
        (long_profile.carrier_hz+long_half)*long_domain.requested_clock_half_width_ppm*1e-6;
    const auto separate_policy=live::detail::helper_occupied_receive_band(settings);
    check(separate_policy && separate_policy->lower_hz<=long_profile.carrier_hz-long_half-long_uncertainty &&
          separate_policy->upper_hz>=long_profile.carrier_hz+long_half+long_uncertainty,
          "long-profile hopping omitted its independent UTC rate-search allowance");
}

struct Waveform {
    modem::Config config;
    Bytes bits{0,1,0,1};
    std::vector<float> pcm;
    std::uint64_t first=0,end=0,handoff=0;
    double frequency_hz=0;
};
Waveform waveform(double noise,double cycles,std::uint64_t handoff_offset,
                  const Bytes& bits=Bytes{0,1,0,1}) {
    Waveform wave;wave.bits=bits;auto& c=wave.config;
    c.sample_rate=2000;c.carrier_hz=500;c.bandwidth_hz=200;
    c.spreading_factor=128;c.scramble=true;c.pulse_shaping=false;c.stream_epoch=1789312671;
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=std::uint8_t(3*i+7);
    modem::StreamingTransmitter tx(modem::RawBits{wave.bits},c,8*1024*1024);
    std::vector<std::complex<double>> analytic(tx.total_samples());
    check(tx.read_analytic(analytic)==analytic.size(),"phase-tail source PCM was truncated");
    const auto symbol=modem::symbol_sample_count(c);
    wave.first=modem::training_sample_count(c);wave.end=tx.physical_valid_payload_end();
    check(wave.end>wave.first+symbol,"phase-tail source has no final symbol");
    wave.handoff=wave.end-symbol/2+handoff_offset;
    wave.frequency_hz=cycles*c.sample_rate/double(symbol);
    // Capture real transmitted PCM and actual post-transmission noise. Neither
    // the input size nor the source bit count is supplied to the receiver.
    wave.pcm.resize(analytic.size()+10*c.sample_rate);
    std::mt19937_64 random(5181);std::normal_distribution<double> gaussian(0,noise);
    for(auto& sample:wave.pcm)sample=float(gaussian(random));
    for(std::size_t i=0;i<analytic.size();++i)
        wave.pcm[i]+=float(.04*std::real(analytic[i]*std::polar(1.,.73+tau*wave.frequency_hz*i/c.sample_rate)));
    return wave;
}

struct Stream {
    Bytes bits;
    std::uint64_t end=0,completed_at=0;
    unsigned completions=0;
    std::size_t missing=0;
};
Stream receive_after_helper_loss(const Waveform& wave,std::size_t chunk,bool frequency_grid,bool lose_helper=true) {
    const auto& c=wave.config;
    modem::PatternSearch search;search.hypotheses={{0,0}};search.worker_threads=1;
    if(frequency_grid)search.hypotheses={{-wave.frequency_hz,0},{0,0},{wave.frequency_hz,0}};
    search.drift_tolerant=false;search.differential_window_seconds=0;
    search.initial_stream_symbols=1;search.chunk_bits=1;
    search.start_offset_seconds=double(wave.first)/c.sample_rate;search.start_uncertainty_seconds=.01;
    modem::StreamingReceiver receiver(c,8*1024*1024,search);
    check(receiver.phase_assistance_supported(),"phase-tail fixture selected an unsupported receiver");
    const auto bin=receiver.phase_assistance_bin_samples();
    check(bin>0,"phase-tail receiver has no phase bin");
    live::detail::HelperPhaseContinuation continuation{0,0,wave.frequency_hz};
    std::map<std::pair<std::uint64_t,std::uint64_t>,Stream> streams;
    bool handed_off=false,used_partial_bin=false;
    for(std::size_t at=0;at<wave.pcm.size();) {
        auto count=std::min(chunk,wave.pcm.size()-at);
        // The final helper observation ends halfway through the last payload
        // symbol. This also permits a handoff inside the native projection bin.
        if(at<wave.handoff)count=std::min<std::size_t>(count,wave.handoff-at);
        else {handed_off=true;used_partial_bin|=at%bin!=0;}
        const auto begin=at>bin?at-bin:0,end=at+count+bin;
        const std::array<modem::PatternPhaseKnot,2> knots{{
            {begin,continuation.at(begin,c.sample_rate)},
            {end,continuation.at(end,c.sample_rate)}}};
        check(receiver.push_phase_assisted(std::span(wave.pcm).subspan(at,count),knots,end-begin),
              "phase continuation rejected captured PCM at helper loss");
        if(!lose_helper || at<wave.handoff) {
            // Match Live's successful measured-phase push. After helper loss,
            // its last model is retained, including its frequency slope.
            continuation={at+count,continuation.at(at+count,c.sample_rate),wave.frequency_hz};
        }
        at+=count;receiver.process_pending();
        for(const auto& burst:receiver.take_pattern_bursts()) {
            auto& stream=streams[{burst.stream_first_sample,burst.stream_first_symbol}];
            stream.bits.insert(stream.bits.end(),burst.bits.begin(),burst.bits.end());
            stream.missing+=burst.missing_slots;stream.end=burst.end_sample;
            if(burst.complete) {
                ++stream.completions;stream.completed_at=at;
                check(at>=burst.end_sample+modem::pattern_absence_samples(c),
                      "phase-tail receiver completed before fully scored physical absence");
            }
            if(burst.stream_first_sample==wave.first) {
                check(stream.bits.size()<=wave.bits.size() &&
                      std::equal(stream.bits.begin(),stream.bits.end(),wave.bits.begin()),
                      "helper loss changed an accepted Robust bit prefix");
            }
        }
    }
    check(handed_off,"phase-tail capture never reached helper loss");
    if(wave.handoff%bin)check(used_partial_bin,"phase-tail handoff missed its partial projection bin");
    const auto exact=std::find_if(streams.begin(),streams.end(),[&](const auto& entry) {
        const auto& [identity,stream]=entry;
        return identity.first==wave.first && stream.bits==wave.bits && stream.end==wave.end &&
               stream.completions==1 && !stream.missing;
    });
    check(exact!=streams.end(),"helper phase loss truncated Robust bits or their physical END");
    // Deliberately no finish(): only the observed noise samples may score END.
    std::cout<<"phase-tail chunk="<<chunk<<" grid="<<frequency_grid<<" loss="<<lose_helper<<" handoff="<<wave.handoff
             <<" slope="<<wave.frequency_hz<<" bits="<<exact->second.bits.size()
             <<" end="<<exact->second.end<<" completed_at="<<exact->second.completed_at<<'\n';
    return exact->second;
}

void long_patterned_tail() {
    constexpr std::uint64_t pattern=0xD6E4B39A1275CC07ULL;
    Bytes bits;
    for(unsigned remaining=64;remaining;--remaining)
        bits.push_back(static_cast<std::uint8_t>((pattern>>(remaining-1))&1));
    const auto wave=waveform(.025,2,3,bits);
    // This compares the native phase API with continued observations versus
    // retaining its last model. It does not simulate physical helper tone loss.
    const auto continuous=receive_after_helper_loss(wave,137,false,false);
    const auto retained=receive_after_helper_loss(wave,137,false);
    check(continuous.bits==retained.bits && continuous.end==retained.end &&
          continuous.completions==retained.completions,
          "phase qualification loss changed the complete patterned Robust stream");
}
}
int main() {try {
    signed_sample_dates();
    optional_geometry_and_audits();
    const auto marginal=waveform(.05,2,0);
    for(const auto chunk:std::array<std::size_t,3>{37,137,4096})receive_after_helper_loss(marginal,chunk,false);
    receive_after_helper_loss(marginal,137,true);
    receive_after_helper_loss(waveform(.05,2,3),137,false);
    receive_after_helper_loss(waveform(.05,0,0),137,false);
    receive_after_helper_loss(waveform(.025,2,0),137,false);
    long_patterned_tail();
    return 0;
}catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
